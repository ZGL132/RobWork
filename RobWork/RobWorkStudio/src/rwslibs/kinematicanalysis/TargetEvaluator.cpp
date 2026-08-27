// =============================================================================
//  TargetEvaluator.cpp —— 任务点评估器实现
// =============================================================================
//
// 实现 TargetEvaluator::evaluate 的完整流水线:
//   1. 目标合法性校验(位置 / RPY / 容差 / 权重必须为有限非负值);
//   2. 参考系解析:有 WorkCell 时用 TaskPointResolver 支持任意参考帧与 TCP 帧,
//      否则回退到"仅设备 base 帧"的旧式解析;
//   3. 生成多样化的 IK 种子,用 JacobianIKSolver 求候选解;
//   4. 逐候选复用 ConfigurationEvaluator 做配置评估,并计算位置 / 姿态残差、
//      离当前 Q 距离与综合评分;
//   5. 汇总全部候选得到任务点级 feasibility / quality。
//
// 所有异常在 evaluate 入口被捕获为 SolverError,保证对外永远返回完整结果。
#include "TargetEvaluator.hpp"

#include "ConfigurationEvaluator.hpp"
#include "TaskPointResolver.hpp"

#include <rw/invkin/JacobianIKSolver.hpp>
#include <rw/kinematics/Kinematics.hpp>
#include <rw/math/EAA.hpp>
#include <rw/math/RPY.hpp>
#include <rw/math/Vector3D.hpp>

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>

namespace rws {
namespace {

// -----------------------------------------------------------------------------
// 内部辅助函数(匿名命名空间,仅本翻译单元可见)
// -----------------------------------------------------------------------------
//
// appendFailure:去重地向任务点结果追加失败原因。任务点级 failureReasons 是
// 各候选解失败原因的上卷集合,去重可避免同一根因被多个候选重复放大。
void appendFailure(TargetEvaluation& result, KinematicFailureReason reason)
{
    if (std::find (result.failureReasons.begin (), result.failureReasons.end (), reason) ==
        result.failureReasons.end ())
        result.failureReasons.push_back (reason);
}

// appendWarning:构造来源为 "TargetEvaluator" 的告警并加入任务点结果。
// 告警用于解释评估为何降级(如参考帧缺失、残差超差等),code 供 UI / 测试匹配。
void appendWarning(TargetEvaluation& result,
                   const char* code,
                   const std::string& message,
                   AnalysisStatus severity = AnalysisStatus::Warning)
{
    AnalysisWarning warning;
    warning.code = code;
    warning.message = message;
    warning.source = "TargetEvaluator";
    warning.severity = severity;
    result.warnings.push_back (warning);
}

// appendConfigurationDiagnostics:把单个候选解的失败原因与告警"上卷"到任务点级结果。
// 设计动机:任务点的可行性依赖其所有候选解 —— 若候选解存在超出限位 / 奇异 / 碰撞
// 等硬约束问题,必须在任务点层面可见,否则用户只看到"IK 有解"却不知解已退化。
void appendConfigurationDiagnostics(TargetEvaluation& result,
                                    const ConfigurationEvaluation& configuration)
{
    for (const KinematicFailureReason reason : configuration.failureReasons)
        appendFailure (result, reason);
    result.warnings.insert (result.warnings.end (), configuration.warnings.begin (),
                            configuration.warnings.end ());
}

// appendConfigurationFailure:去重地向候选解的 ConfigurationEvaluation 追加失败原因。
// 用于标注"该候选虽然 IK 有解,但 FK 残差超出目标容差"(TargetResidual)。
void appendConfigurationFailure(ConfigurationEvaluation& configuration,
                                KinematicFailureReason reason)
{
    if (std::find (configuration.failureReasons.begin (), configuration.failureReasons.end (), reason) ==
        configuration.failureReasons.end ())
        configuration.failureReasons.push_back (reason);
}

// taskPointToTransform:把 TaskPoint(位置 + 度制 RPY)转换为齐次变换矩阵。
// 注意 RPY 使用 RobWork 默认的 Roll→Pitch→Yaw 旋转顺序,这一约定在整个插件
// 中与 TaskPointResolver 解析、UI 输入保持一致,避免姿态解释的歧义。
rw::math::Transform3D<> taskPointToTransform(const TaskPoint& target)
{
    const double toRad = rw::math::Pi / 180.0;
    return rw::math::Transform3D<> (
        rw::math::Vector3D<> (target.position[0], target.position[1], target.position[2]),
        rw::math::RPY<> (target.rpyDeg[0] * toRad,
                         target.rpyDeg[1] * toRad,
                         target.rpyDeg[2] * toRad));
}

// qDistance:计算两个关节配置的 L2 距离;维度不一致时返回 +inf。
// 用途:候选解去重(与 ikDuplicateQThreshold 比较)以及"离当前 Q 距离"排序依据。
double qDistance(const rw::math::Q& lhs, const rw::math::Q& rhs)
{
    if (lhs.size () != rhs.size ())
        return std::numeric_limits< double >::infinity ();
    return (lhs - rhs).norm2 ();
}

// orientationErrorDeg:计算实际姿态相对目标姿态的误差角(度)。
// 利用 R_diff = R_target^T * R_actual 再转成 EAA 取转角绝对值,结果恒非负,
// 物理含义是"绕某轴旋转的等效角",与位置残差一起构成 FK 残差的两部分。
double orientationErrorDeg(const rw::math::Transform3D<>& actual,
                           const rw::math::Transform3D<>& target)
{
    const rw::math::Rotation3D<> difference = inverse (target.R ()) * actual.R ();
    return std::fabs (rw::math::EAA<> (difference).angle ()) * 180.0 / rw::math::Pi;
}

// effectiveTolerance:容差"兜底"规则 —— 请求值 > 0 用之,否则用回退值。
// 保证即使 TaskPoint 未显式给定容差,残差判定仍有一个明确的、非零的尺度。
double effectiveTolerance(double requested, double fallback)
{
    return requested > 0.0 ? requested : fallback;
}

// clampToBounds:把关节值逐维夹到关节限位区间内(忽略非有限的限位维)。
// 用途:保证 IK 种子始终落在关节限位内,避免从越界种子出发导致求解异常;
// 该函数只"收紧"不会越界;若限位维度与 q 不匹配则原样返回。
rw::math::Q clampToBounds(const rw::math::Q& q,
                          const std::pair< rw::math::Q, rw::math::Q >& bounds)
{
    rw::math::Q result = q;
    if (bounds.first.size () != q.size () || bounds.second.size () != q.size ())
        return result;
    for (std::size_t i = 0; i < q.size (); ++i) {
        if (std::isfinite (bounds.first (i)))
            result (i) = std::max (result (i), bounds.first (i));
        if (std::isfinite (bounds.second (i)))
            result (i) = std::min (result (i), bounds.second (i));
    }
    return result;
}

// makeSeeds:生成一组互不重复的 IK 起始种子。
// 种子来源依次为:当前关节值、关节区间中点、零向量,以及按位掩码在区间
// 上 / 下 25% 处构造的确定性组合;最终裁剪到 seedCount 个。
// 选择多样种子是为了让 JacobianIK 跳出局部收敛,尽量覆盖多个解分支,
// 提高"找到满足全部约束的最优解"的概率。
std::vector< rw::math::Q > makeSeeds(
    const rw::math::Q& current,
    const std::pair< rw::math::Q, rw::math::Q >& bounds,
    int seedCount)
{
    std::vector< rw::math::Q > seeds;
    if (current.size () == 0 || seedCount <= 0)
        return seeds;
    const auto addUnique = [&seeds] (const rw::math::Q& candidate) {
        for (const rw::math::Q& existing : seeds) {
            if (existing.size () == candidate.size () && (existing - candidate).norm2 () <= 1e-6)
                return;
        }
        seeds.push_back (candidate);
    };
    addUnique (clampToBounds (current, bounds));
    rw::math::Q center = current;
    rw::math::Q zero = current;
    for (std::size_t i = 0; i < current.size (); ++i) {
        const double lo = std::isfinite (bounds.first (i)) ? bounds.first (i) : current (i) - rw::math::Pi;
        const double hi = std::isfinite (bounds.second (i)) ? bounds.second (i) : current (i) + rw::math::Pi;
        center (i) = 0.5 * (lo + hi);
        zero (i) = 0.0;
    }
    addUnique (clampToBounds (center, bounds));
    addUnique (clampToBounds (zero, bounds));
    for (int mask = 0; static_cast< int > (seeds.size ()) < seedCount && mask < 64; ++mask) {
        rw::math::Q seed = current;
        for (std::size_t i = 0; i < current.size (); ++i) {
            const double lo = std::isfinite (bounds.first (i)) ? bounds.first (i) : current (i) - rw::math::Pi;
            const double hi = std::isfinite (bounds.second (i)) ? bounds.second (i) : current (i) + rw::math::Pi;
            seed (i) = ((mask >> (i % 6)) & 1) != 0 ? 0.75 * hi + 0.25 * lo :
                                                        0.25 * hi + 0.75 * lo;
        }
        addUnique (clampToBounds (seed, bounds));
    }
    if (static_cast< int > (seeds.size ()) > seedCount)
        seeds.resize (static_cast< std::size_t > (seedCount));
    return seeds;
}

// isFiniteTarget:校验任务点数值合法性 —— 位置、RPY、容差、权重必须全部有限
// 且容差非负。任何非法值都会被上层判定为 InvalidTarget,而不是让 NaN 传染到
// 后续 FK / 残差计算(保证评估结果永远可序列化、可比较)。
bool isFiniteTarget(const TaskPoint& target)
{
    for (const double value : target.position)
        if (!std::isfinite (value))
            return false;
    for (const double value : target.rpyDeg)
        if (!std::isfinite (value))
            return false;
    return std::isfinite (target.tolerance.positionMeters) &&
           target.tolerance.positionMeters >= 0.0 &&
           std::isfinite (target.tolerance.orientationDeg) &&
           target.tolerance.orientationDeg >= 0.0 &&
           std::isfinite (target.weight) &&
           target.weight >= 0.0;
}

} // namespace

// -----------------------------------------------------------------------------
// TargetEvaluator::evaluate —— 任务点完整评估
// -----------------------------------------------------------------------------
//
// 流程:校验目标 -> 解析参考系 -> 生成种子并求解 IK -> 逐候选做配置评估与
// 残差计算 -> 汇总状态。任何底层异常都被捕获并记录为 SolverError。
TargetEvaluation TargetEvaluator::evaluate(const AnalysisContext& context,
                                            const TaskPoint& target,
                                            const TargetEvaluationOptions& options) const
{
    TargetEvaluation result;
    result.stage = options.evidenceStage;
    result.target = target;
    result.provenance = RequirementExecutionProvenance ();
    result.warnings = context.capabilityWarnings;

    if (!isFiniteTarget (target)) {
        appendFailure (result, KinematicFailureReason::InvalidTarget);
        appendWarning (result, "KIN_INVALID_TARGET", "Target contains a non-finite or invalid value.",
                       AnalysisStatus::Fail);
        result.feasibility = Feasibility::Infeasible;
        result.quality = Quality::Critical;
        return result;
    }

    if (context.device == nullptr) {
        appendFailure (result, KinematicFailureReason::NoDevice);
        appendWarning (result, "KIN_TARGET_NO_DEVICE", "Target analysis requires a Device.",
                       AnalysisStatus::Fail);
        result.feasibility = Feasibility::DataInsufficient;
        result.quality = Quality::Critical;
        return result;
    }

    // ---- 阶段 1:解析任务点参考系 ------------------------------------------
    // 有 WorkCell 时用 TaskPointResolver 支持任意参考帧与 TCP 帧;无 WorkCell
    // 时回退到"仅设备 base 帧"的旧式解析(其余参考帧一律 FrameNotFound)。
    ResolvedTaskPoint resolved;
    if (context.workcell != nullptr) {
        resolved = resolveTaskPoint (
            context.workcell.get (), context.device, context.tcpFrame, context.baseState, target);
        result.warnings.insert (result.warnings.end (), resolved.warnings.begin (), resolved.warnings.end ());
    }
    else {
        const std::string baseName = context.device->getBase ()->getName ();
        const bool worldReference = target.refFrame == "WORLD" || target.refFrame == "world";
        if (!target.refFrame.empty () && target.refFrame != baseName && !worldReference) {
            resolved.failure = KinematicFailureReason::FrameNotFound;
            appendWarning (result, "KIN_TASK_REF_NOT_FOUND",
                           "Legacy target evaluation only accepts the device base frame.",
                           AnalysisStatus::Fail);
        }
        else {
            resolved.targetInDeviceBase = target;
            resolved.tcpFrame = context.tcpFrame != nullptr ? context.tcpFrame : context.device->getEnd ();
            resolved.valid = resolved.tcpFrame != nullptr;
            if (!resolved.valid)
                resolved.failure = KinematicFailureReason::NoTcpFrame;
        }
    }
    if (!resolved.valid) {
        if (resolved.failure == KinematicFailureReason::InvalidTarget) {
            bool frameMissing = false;
            for (const AnalysisWarning& warning : resolved.warnings)
                frameMissing = frameMissing || warning.code == "KIN_TASK_REF_NOT_FOUND";
            appendFailure (result, frameMissing ? KinematicFailureReason::FrameNotFound : resolved.failure);
        }
        else {
            appendFailure (result, resolved.failure);
        }
        result.feasibility = Feasibility::Infeasible;
        result.quality = Quality::Critical;
        return result;
    }

    // ---- 阶段 2:构造目标位姿与 IK 求解参数 --------------------------------
    const rw::math::Transform3D<> targetPose = taskPointToTransform (resolved.targetInDeviceBase);
    const rw::math::Q currentQ = context.device->getQ (context.baseState);
    const std::pair< rw::math::Q, rw::math::Q > bounds = context.device->getBounds ();
    const std::vector< rw::math::Q > seeds = makeSeeds (currentQ, bounds, options.seedCount);
    const int maxSolutions = std::max (0, options.maxSolutions);
    if (maxSolutions == 0 || seeds.empty ()) {
        appendFailure (result, KinematicFailureReason::IkNoSolution);
        result.feasibility = Feasibility::Infeasible;
        result.quality = Quality::Critical;
        return result;
    }

    try {
        rw::core::Ptr< const rw::kinematics::Frame > tcpFrame = resolved.tcpFrame;
        rw::invkin::JacobianIKSolver::Ptr solver = rw::core::ownedPtr (
            new rw::invkin::JacobianIKSolver (context.device, tcpFrame, context.baseState));
        ConfigurationEvaluator configurationEvaluator;
        ConfigurationEvaluationOptions configurationOptions;
        configurationOptions.evidenceStage = options.evidenceStage;
        configurationOptions.checkCollision = options.checkCollision;
        configurationOptions.requireCollisionFree = options.requireCollisionFree;
        const double positionTolerance = effectiveTolerance (
            target.tolerance.positionMeters, options.positionToleranceMeters);
        const double orientationTolerance = effectiveTolerance (
            target.tolerance.orientationDeg, options.orientationToleranceDeg);

        // ---- 阶段 3:逐个种子求解并评估候选解 ------------------------------
        for (const rw::math::Q& seed : seeds) {
            if (static_cast< int > (result.candidates.size ()) >= maxSolutions)
                break;
            rw::kinematics::State seedState = context.baseState;
            context.device->setQ (seed, seedState);
            const std::vector< rw::math::Q > solutions = solver->solve (targetPose, seedState);
            for (const rw::math::Q& q : solutions) {
                if (static_cast< int > (result.candidates.size ()) >= maxSolutions)
                    break;
                bool duplicate = false;
                for (const TargetCandidate& existing : result.candidates) {
                    if (existing.configuration.q.size () == q.size () &&
                        qDistance (existing.configuration.q, q) <= context.thresholds.ikDuplicateQThreshold) {
                        duplicate = true;
                        break;
                    }
                }
                if (duplicate)
                    continue;

                TargetCandidate candidate;
                candidate.configuration = configurationEvaluator.evaluate (
                    context, q, configurationOptions);
                candidate.positionErrorMeters =
                    (candidate.configuration.tcpPose.P () - targetPose.P ()).norm2 ();
                candidate.orientationErrorDeg = orientationErrorDeg (
                    candidate.configuration.tcpPose, targetPose);
                candidate.distanceToReferenceQ = qDistance (currentQ, q);
                candidate.score = candidate.positionErrorMeters * 1000.0 +
                                 candidate.orientationErrorDeg + candidate.distanceToReferenceQ -
                                 candidate.configuration.minimumJointMargin -
                                 candidate.configuration.manipulability;
                if (candidate.positionErrorMeters > positionTolerance ||
                    candidate.orientationErrorDeg > orientationTolerance)
                    appendConfigurationFailure (candidate.configuration,
                                                 KinematicFailureReason::TargetResidual);
                appendConfigurationDiagnostics (result, candidate.configuration);
                result.candidates.push_back (candidate);
            }
        }
    }
    catch (const std::exception& ex) {
        appendFailure (result, KinematicFailureReason::SolverError);
        appendWarning (result, "KIN_TARGET_SOLVER_ERROR", ex.what (), AnalysisStatus::Fail);
        result.feasibility = Feasibility::Infeasible;
        result.quality = Quality::Critical;
        return result;
    }

    sortTargetCandidatesForDisplay (result.candidates);
    if (result.candidates.empty ()) {
        appendFailure (result, KinematicFailureReason::IkNoSolution);
        result.feasibility = Feasibility::Infeasible;
        result.quality = Quality::Critical;
        return result;
    }

    bool feasible = false;
    bool degraded = false;
    for (const TargetCandidate& candidate : result.candidates) {
        const bool residualOk = candidate.positionErrorMeters <=
                                     effectiveTolerance (target.tolerance.positionMeters,
                                                         options.positionToleranceMeters) &&
                                 candidate.orientationErrorDeg <=
                                     effectiveTolerance (target.tolerance.orientationDeg,
                                                         options.orientationToleranceDeg);
        if (candidate.configuration.feasibility == Feasibility::DataInsufficient) {
            result.feasibility = Feasibility::DataInsufficient;
            result.quality = Quality::Critical;
            return result;
        }
        if (candidate.configuration.feasibility == Feasibility::Feasible && residualOk)
            feasible = true;
        degraded = degraded || candidate.configuration.quality == Quality::Degraded;
    }
    if (feasible) {
        result.feasibility = Feasibility::Feasible;
        result.quality = degraded ? Quality::Degraded : Quality::Good;
    }
    else {
        appendFailure (result, KinematicFailureReason::TargetResidual);
        result.feasibility = Feasibility::Infeasible;
        result.quality = Quality::Critical;
    }
    return result;
}

// -----------------------------------------------------------------------------
// sortTargetCandidatesForDisplay —— 候选解展示排序(就地)
// -----------------------------------------------------------------------------
//
// 排序键优先级(先满足者胜):无碰撞 > 位置残差小 > 姿态残差小 >
// 最小关节裕度大 > 可操作度大 > 离当前 Q 距离小;最后以关节值字典序兜底,
// 保证相同输入得到完全相同的排序结果(确定性,便于 UI 与测试复现)。
void rws::sortTargetCandidatesForDisplay(std::vector< TargetCandidate >& candidates)
{
    std::sort (candidates.begin (), candidates.end (), [] (const TargetCandidate& lhs,
                                                            const TargetCandidate& rhs) {
        if (lhs.configuration.inCollision != rhs.configuration.inCollision)
            return !lhs.configuration.inCollision;
        if (lhs.positionErrorMeters != rhs.positionErrorMeters)
            return lhs.positionErrorMeters < rhs.positionErrorMeters;
        if (lhs.orientationErrorDeg != rhs.orientationErrorDeg)
            return lhs.orientationErrorDeg < rhs.orientationErrorDeg;
        if (lhs.configuration.minimumJointMargin != rhs.configuration.minimumJointMargin)
            return lhs.configuration.minimumJointMargin > rhs.configuration.minimumJointMargin;
        if (lhs.configuration.manipulability != rhs.configuration.manipulability)
            return lhs.configuration.manipulability > rhs.configuration.manipulability;
        if (lhs.distanceToReferenceQ != rhs.distanceToReferenceQ)
            return lhs.distanceToReferenceQ < rhs.distanceToReferenceQ;
        const rw::math::Q& lhsQ = lhs.configuration.q;
        const rw::math::Q& rhsQ = rhs.configuration.q;
        const std::size_t common = std::min (lhsQ.size (), rhsQ.size ());
        for (std::size_t i = 0; i < common; ++i) {
            if (lhsQ (i) != rhsQ (i))
                return lhsQ (i) < rhsQ (i);
        }
        return lhsQ.size () < rhsQ.size ();
    });
}

} // namespace rws
