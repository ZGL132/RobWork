#include "KinematicAnalyzer.hpp"
#include "ConfigurationEvaluator.hpp"
#include "TargetEvaluator.hpp"
#include "TaskPointResolver.hpp"
#include "KinematicAnalysisWorkspace.hpp"
#include "KinematicAnalysisPoseReachability.hpp"
#include "OrientationCoverageEvaluator.hpp"

// 引入 IK 求解器和必要的运动学/数学工具。
#include <rw/core/Ptr.hpp>
#include <rw/invkin/JacobianIKSolver.hpp>
#include <rw/kinematics/Kinematics.hpp>
#include <rw/math/EAA.hpp>
#include <rw/math/Jacobian.hpp>
#include <rw/math/Q.hpp>
#include <rw/math/RPY.hpp>
#include <rw/math/Vector3D.hpp>
#include <rw/models/JointDevice.hpp>
#include <rw/models/RevoluteJoint.hpp>
#include <rw/proximity/CollisionDetector.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <utility>

using namespace rws;

// 默认阈值由 KinematicThresholds 自身的成员初始值给出。
KinematicAnalyzer::KinematicAnalyzer () : _thresholds () {}

namespace {

// 把 Quality 等级映射为可比较的整数(升序:Unknown < Good < Degraded < Critical)。
// 用于 buildRequirementValidationSummary 中"取所有 Must 项中最差质量"的判定。
int requirementQualityRank (Quality quality)
{
    switch (quality) {
    case Quality::Critical: return 3;
    case Quality::Degraded: return 2;
    case Quality::Good: return 1;
    case Quality::Unknown: return 0;
    }
    return 0;
}

// 给汇总追加一条告警:自动补全 source 为 "KinematicAnalyzer",并在 message 尾部
// 附上需求 id(非空时),方便用户在告警列表中定位到具体需求条目。
void appendRequirementSummaryWarning (RequirementValidationSummary& summary,
                                      const char* code,
                                      const std::string& message,
                                      const std::string& requirementId)
{
    AnalysisWarning warning;
    warning.code = code;
    warning.message = message;
    warning.source = "KinematicAnalyzer";
    warning.severity = AnalysisStatus::Warning;
    if (!requirementId.empty ())
        warning.message += " [" + requirementId + "]";
    summary.warnings.push_back (warning);
}

// 把候选证据阶段合并进累计值:只保留更"高"的阶段(Estimated < Quick < Verified)。
// 用于让汇总的阶段反映"已达到的最强证据",而不是被后处理的低阶段覆盖。
void includeRequirementStage (AnalysisEvidenceStage& stage,
                              AnalysisEvidenceStage candidate)
{
    if (static_cast< int > (candidate) > static_cast< int > (stage))
        stage = candidate;
}

} // namespace

RequirementValidationSummary rws::buildRequirementValidationSummary (
    const RequirementExecutionSet& requirements,
    const std::vector< TargetEvaluation >& taskResults,
    const std::vector< RegionCoverageResult >& regionResults)
{
    RequirementValidationSummary summary;
    summary.provenance = requirements.provenance;
    summary.taskResults = taskResults;
    summary.regionResults = regionResults;
    summary.stage = AnalysisEvidenceStage::Estimated;

    bool hasInfeasibleMust = false;
    bool hasDataInsufficientMust = false;
    Quality mustQuality = Quality::Unknown;
    const auto includeQuality = [&mustQuality] (Quality quality) {
        if (requirementQualityRank (quality) > requirementQualityRank (mustQuality))
            mustQuality = quality;
    };

    for (std::size_t index = 0; index < requirements.tasks.size (); ++index) {
        const RequirementExecutionTask& requirement = requirements.tasks[index];
        if (index >= taskResults.size ()) {
            if (requirement.compileState == RequirementExecutionCompileState::Included &&
                requirement.level == RequirementExecutionLevel::Must) {
                ++summary.mustTaskCount;
                hasDataInsufficientMust = true;
                appendRequirementSummaryWarning (
                    summary, "KIN_MUST_TASK_DATA_INSUFFICIENT",
                    "Included Must task has no evaluation result.", requirement.id);
            }
            continue;
        }
        const TargetEvaluation& result = taskResults[index];
        includeRequirementStage (summary.stage, result.stage);
        if (requirement.compileState != RequirementExecutionCompileState::Included ||
            requirement.level != RequirementExecutionLevel::Must)
            continue;
        ++summary.mustTaskCount;
        includeQuality (result.quality);
        if (result.feasibility == Feasibility::Feasible) {
            ++summary.mustTaskFeasibleCount;
        }
        else if (result.feasibility == Feasibility::DataInsufficient ||
                 result.feasibility == Feasibility::NotEvaluated) {
            hasDataInsufficientMust = true;
            appendRequirementSummaryWarning (
                summary, "KIN_MUST_TASK_DATA_INSUFFICIENT",
                "Included Must task lacks sufficient evaluation evidence.", requirement.id);
        }
        else {
            hasInfeasibleMust = true;
            appendRequirementSummaryWarning (
                summary, "KIN_MUST_TASK_INFEASIBLE",
                "Included Must task is infeasible.", requirement.id);
        }
    }

    for (std::size_t index = 0; index < requirements.workspaceRegions.size (); ++index) {
        const RequirementExecutionRegion& requirement = requirements.workspaceRegions[index];
        if (index >= regionResults.size ()) {
            if (requirement.compileState == RequirementExecutionCompileState::Included &&
                requirement.level == RequirementExecutionLevel::Must) {
                ++summary.mustRegionCount;
                hasDataInsufficientMust = true;
                appendRequirementSummaryWarning (
                    summary, "KIN_MUST_REGION_DATA_INSUFFICIENT",
                    "Included Must region has no evaluation result.", requirement.id);
            }
            continue;
        }
        const RegionCoverageResult& result = regionResults[index];
        includeRequirementStage (summary.stage, result.stage);
        if (requirement.compileState != RequirementExecutionCompileState::Included ||
            requirement.level != RequirementExecutionLevel::Must)
            continue;
        ++summary.mustRegionCount;
        includeQuality (result.quality);
        if (result.feasibility == Feasibility::Feasible) {
            ++summary.mustRegionFeasibleCount;
        }
        else if (result.feasibility == Feasibility::DataInsufficient ||
                 result.feasibility == Feasibility::NotEvaluated) {
            hasDataInsufficientMust = true;
            appendRequirementSummaryWarning (
                summary, "KIN_MUST_REGION_DATA_INSUFFICIENT",
                "Included Must region lacks sufficient evaluation evidence.", requirement.id);
        }
        else {
            hasInfeasibleMust = true;
            appendRequirementSummaryWarning (
                summary, "KIN_MUST_REGION_INFEASIBLE",
                "Included Must region is infeasible.", requirement.id);
        }
    }

    const int mustCount = summary.mustTaskCount + summary.mustRegionCount;
    if (mustCount == 0) {
        summary.feasibility = Feasibility::NotEvaluated;
        summary.quality = Quality::Unknown;
        if (summary.stage == AnalysisEvidenceStage::Estimated)
            summary.stage = AnalysisEvidenceStage::Verified;
    }
    else if (hasDataInsufficientMust) {
        summary.feasibility = Feasibility::DataInsufficient;
        summary.quality = Quality::Critical;
    }
    else if (hasInfeasibleMust) {
        summary.feasibility = Feasibility::Infeasible;
        summary.quality = Quality::Critical;
    }
    else {
        summary.feasibility = Feasibility::Feasible;
        summary.quality = mustQuality;
    }
    return summary;
}

// 新执行契约的 Must-only 批量验证入口:把参数打包后委托给 KinematicBatchRunner
// 执行。批量逻辑(去重、批次拆分、取消检查)集中在 BatchRunner,本类只做参数
// 转发;旧 API 的批量结果仍由下方 analyzeTaskPoints 提供。
RequirementValidationSummary KinematicAnalyzer::validateRequirements (
    const AnalysisContext& context,
    const RequirementExecutionSet& requirements,
    const BatchRunOptions& options,
    const CancellationToken& cancellation) const
{
    return KinematicBatchRunner ().validateRequirements (
        context, requirements, options, cancellation);
}

namespace {

// =============================================================================
//  worstStatus — 状态半格聚合
// =============================================================================
// 把两个 AnalysisStatus 合并成一个"更糟"的状态,优先级
// Fail > Warning > Pass > Unknown。该半格是 total order(可任意两两合并),
// 没有"最小上界冲突"。两个 Unknown 合并仍为 Unknown。
// 主要用于 buildAggregateResult 把 currentPose / 任务点 / workspace / pose 四
// 类子结果合并为单一 status,以及 analyzeIk 把多解的状态折叠成一个。
AnalysisStatus worstStatus (AnalysisStatus lhs, AnalysisStatus rhs)
{
    if (lhs == AnalysisStatus::Fail || rhs == AnalysisStatus::Fail)
        return AnalysisStatus::Fail;
    if (lhs == AnalysisStatus::Warning || rhs == AnalysisStatus::Warning)
        return AnalysisStatus::Warning;
    if (lhs == AnalysisStatus::Pass || rhs == AnalysisStatus::Pass)
        return AnalysisStatus::Pass;
    return AnalysisStatus::Unknown;
}

// =============================================================================
//  taskPointToTransform — TaskPoint → Transform3D
// =============================================================================
// TaskPoint 在 UI/CSV 层用 "位置 (m) + RPY (度)" 描述,而 RobWork 内部使用
// Transform3D<Vector3D, RPY>(Vector3D, RPY in rad)。本函数只做单位换算与
// 封装:不解释、不做有效性检查(NaN/Inf 应在更上层拦截)。
//   - toRad = π/180,固定常量;直接 180.0 / rw::math::Pi 也可以,但用 toRad 更直观;
//   - RPY 顺序 RobWork 默认是 extrinsic XYZ,即绕固定坐标轴 X→Y→Z 依次旋转,
//     与 TaskPoint 的约定一致。
rw::math::Transform3D<> taskPointToTransform (const TaskPoint& target)
{
    const double toRad = rw::math::Pi / 180.0;
    return rw::math::Transform3D<> (
        rw::math::Vector3D<> (target.position[0], target.position[1], target.position[2]),
        rw::math::RPY<> (target.rpyDeg[0] * toRad,
                         target.rpyDeg[1] * toRad,
                         target.rpyDeg[2] * toRad));
}

// =============================================================================
//  qDistance — 关节空间 L2 距离
// =============================================================================
//   d = ||q_lhs - q_rhs||_2
// 用途:在 IK 评分中表示"当前解与当前 state 的距离",用作"路径长度偏好"。
// 维度不一致返回 +inf(而非 NaN 或抛异常),让上层评分始终保持有限数。
double qDistance (const rw::math::Q& lhs, const rw::math::Q& rhs)
{
    if (lhs.size () != rhs.size ())
        return std::numeric_limits< double >::infinity ();
    return (lhs - rhs).norm2 ();
}

// =============================================================================
//  positionError — 位置误差
// =============================================================================
//   e_pos = ||P_actual - P_target||_2  (单位:m)
// 直接用 Transform3D 的平移分量做差。RobWork 的 Vector3D::norm2() 即 L2 长度。
double positionError (const rw::math::Transform3D<>& actual,
                      const rw::math::Transform3D<>& target)
{
    return (actual.P () - target.P ()).norm2 ();
}

// =============================================================================
//  orientationErrorDeg — 姿态误差(度)
// =============================================================================
// 数学上:
//   R_diff = R_target^T · R_actual
//   e_ori  = |angle(EAA(R_diff))| · 180/π
// EAA(等效轴角)将旋转矩阵映射为 (axis, angle) 对,|angle| 就是从 R_target
// 转到 R_actual 所需的最短旋转角度。该公式等价于 ||log(R_target^T · R_actual)||。
// 注意:fabs 保证取正值(最短旋转方向),不会出现"绕远路到目标"。
double orientationErrorDeg (const rw::math::Transform3D<>& actual,
                            const rw::math::Transform3D<>& target)
{
    const rw::math::Rotation3D<> diff = inverse (target.R ()) * actual.R ();
    const rw::math::EAA<> eaa (diff);
    return std::fabs (eaa.angle ()) * 180.0 / rw::math::Pi;
}

// =============================================================================
//  qToVector — Q → std::vector<double>
// =============================================================================
// 用于把关节值传入不依赖 RobWork 的接口(QTableWidget / Qt::UserRole / JSON)
// 或共享给下游模块。预 reserve 一次,避免 push_back 时的多次扩容。
std::vector< double > qToVector (const rw::math::Q& q)
{
    std::vector< double > values;
    values.reserve (q.size ());
    for (std::size_t i = 0; i < q.size (); ++i)
        values.push_back (q (i));
    return values;
}

// =============================================================================
//  lexicographicQLess — 关节向量字典序
// =============================================================================
// 在 sortIkSolutionsForDisplay 中作为最后一道 tie-breaker:当所有数值
// 指标都相同时,按 q0→q1→... 的顺序给出稳定排序,这样 UI 的"同一份数据
// 重排后顺序一致"。
bool lexicographicQLess (const std::vector< double >& lhs, const std::vector< double >& rhs)
{
    return std::lexicographical_compare (lhs.begin (), lhs.end (), rhs.begin (), rhs.end ());
}

// =============================================================================
//  makeWarning — 告警构造器
// =============================================================================
// 简化告警构造:固定 source 字段为 "KinematicAnalyzer",这样下游无论
// 是 UI 列表、CSV、JSON 都能识别本插件产生的告警。
AnalysisWarning makeWarning (const std::string& code,
                             const std::string& message,
                             AnalysisStatus severity)
{
    AnalysisWarning w;
    w.code     = code;
    w.message  = message;
    w.source   = "KinematicAnalyzer";
    w.severity = severity;
    return w;
}

// 校验任务点目标数据是否可用于 IK:
//   - position / rpyDeg 必须全部有限(NaN/Inf 会让 IK/FK 数值崩溃);
//   - 位置/姿态容差必须有限且非负;
//   - weight 必须有限。
// 失败时写入 error 并返回 false;调用方(analyzeIk)据此标记 InvalidTarget。
bool validateTaskPointTarget (const TaskPoint& target, std::string* error)
{
    for (double value : target.position) {
        if (!std::isfinite (value)) {
            if (error != nullptr)
                *error = "Target position contains a non-finite value.";
            return false;
        }
    }
    for (double value : target.rpyDeg) {
        if (!std::isfinite (value)) {
            if (error != nullptr)
                *error = "Target orientation contains a non-finite value.";
            return false;
        }
    }
    if (!std::isfinite (target.tolerance.positionMeters) ||
        target.tolerance.positionMeters < 0.0) {
        if (error != nullptr)
            *error = "Target position tolerance must be finite and non-negative.";
        return false;
    }
    if (!std::isfinite (target.tolerance.orientationDeg) ||
        target.tolerance.orientationDeg < 0.0) {
        if (error != nullptr)
            *error = "Target orientation tolerance must be finite and non-negative.";
        return false;
    }
    if (!std::isfinite (target.weight)) {
        if (error != nullptr)
            *error = "Target weight must be finite.";
        return false;
    }
    return true;
}

double effectiveTolerance (double taskTolerance, double defaultTolerance);

// 从目标评估的失败原因列表中挑选"最具代表性"的一个,用于在 UI 高亮主因。
// 优先级数组(从最严重到最轻)手工排序:求解器错误 > 无解 > 碰撞 > ...
// 返回 None 表示没有匹配到任何已知原因(可视为成功)。
KinematicFailureReason primaryFailureFromTarget(const TargetEvaluation& evaluation)
{
    const KinematicFailureReason priority[] = {
        KinematicFailureReason::SolverError,
        KinematicFailureReason::IkNoSolution,
        KinematicFailureReason::Collision,
        KinematicFailureReason::CollisionDetectorUnavailable,
        KinematicFailureReason::FrameNotFound,
        KinematicFailureReason::TargetResidual,
        KinematicFailureReason::JointLimit,
        KinematicFailureReason::Singular,
        KinematicFailureReason::NearJointLimit,
        KinematicFailureReason::NearSingular,
        KinematicFailureReason::InvalidTarget,
        KinematicFailureReason::NoTcpFrame,
        KinematicFailureReason::NoDevice};
    for (const KinematicFailureReason candidate : priority) {
        if (std::find (evaluation.failureReasons.begin (), evaluation.failureReasons.end (), candidate) !=
            evaluation.failureReasons.end ())
            return candidate;
    }
    return KinematicFailureReason::None;
}

// 把新执行契约的 TargetEvaluation 转回旧版 KinematicIkAnalysisResult,供既有
// IK tab / 旧批量 API 使用。关键映射:
//   - status:Feasible+Good -> Pass,Feasible+Degraded -> Warning,NotEvaluated ->
//     Unknown,其余 -> Fail;
//   - 每个候选解:残差超容差 / 不可行 / 碰撞 -> Fail,Degraded -> Warning,否则 Pass;
//   - usableSolutionCount 只统计"无碰撞且非 Fail"的解。
KinematicIkAnalysisResult legacyIkResultFromTarget(const TargetEvaluation& evaluation,
                                                   const TaskPoint& target)
{
    KinematicIkAnalysisResult result;
    result.target = target;
    result.rawCandidateCount = evaluation.candidates.size ();
    result.warnings = evaluation.warnings;
    result.failureReason = primaryFailureFromTarget (evaluation);
    result.status = evaluation.feasibility == Feasibility::Feasible ?
        (evaluation.quality == Quality::Degraded ? AnalysisStatus::Warning : AnalysisStatus::Pass) :
        (evaluation.feasibility == Feasibility::NotEvaluated ? AnalysisStatus::Unknown :
                                                                AnalysisStatus::Fail);

    for (const TargetCandidate& candidate : evaluation.candidates) {
        KinematicIkSolution solution;
        solution.q = qToVector (candidate.configuration.q);
        solution.distanceToCurrentQ = candidate.distanceToReferenceQ;
        solution.minJointLimitMargin = candidate.configuration.minimumJointMargin;
        solution.manipulability = candidate.configuration.manipulability;
        solution.conditionNumber = candidate.configuration.conditionNumber;
        solution.positionErrorMeters = candidate.positionErrorMeters;
        solution.orientationErrorDeg = candidate.orientationErrorDeg;
        solution.inCollision = candidate.configuration.inCollision;
        solution.score = candidate.score;
        solution.failureReasons = candidate.configuration.failureReasons;
        const double positionTolerance = effectiveTolerance (
            target.tolerance.positionMeters, 0.001);
        const double orientationTolerance = effectiveTolerance (
            target.tolerance.orientationDeg, 1.0);
        const bool residualOk = solution.positionErrorMeters <= positionTolerance &&
                                solution.orientationErrorDeg <= orientationTolerance;
        if (candidate.configuration.feasibility == Feasibility::DataInsufficient ||
            candidate.configuration.feasibility == Feasibility::Infeasible || !residualOk ||
            solution.inCollision)
            solution.status = AnalysisStatus::Fail;
        else if (candidate.configuration.quality == Quality::Degraded)
            solution.status = AnalysisStatus::Warning;
        else
            solution.status = AnalysisStatus::Pass;
        if (!solution.inCollision && solution.status != AnalysisStatus::Fail)
            ++result.usableSolutionCount;
        result.solutions.push_back (solution);
    }
    sortIkSolutionsForDisplay (result.solutions);
    return result;
}

// 把旧版 TaskPoint 投影为执行契约任务(RequirementExecutionTask),供新的
// validateRequirements 批量流程消费:
//   - enabled -> Included,否则 Excluded;
//   - 历史 API 把碰撞检测器当作"可选证据"而非硬性要求,故 collisionFreeRequired
//     恒为 false(与 legacy 行为保持一致)。
RequirementExecutionTask requirementTaskFromLegacyTaskPoint(const TaskPoint& point)
{
    RequirementExecutionTask task;
    task.id = point.id;
    task.name = point.name;
    task.refFrame = point.refFrame;
    task.tcpFrame = point.tcpFrame;
    task.position = point.position;
    task.rpyDeg = point.rpyDeg;
    task.positionToleranceMeters = point.tolerance.positionMeters;
    task.orientationToleranceDeg = point.tolerance.orientationDeg;
    task.allowToolRollFree = point.tolerance.allowToolRollFree;
    task.compileState = point.enabled ? RequirementExecutionCompileState::Included :
                                        RequirementExecutionCompileState::Excluded;
    // The historical API treats a detector as optional evidence, rather than as
    // a hard requirement for every task point.
    task.collisionFreeRequired = false;
    return task;
}

// 把目标评估转回旧版任务级结果。disabled 任务点直接返回 Unknown + KIN_TASK_DISABLED
// 告警,且不计入可达率分母;其余情况沿用 legacyIkResultFromTarget 的状态映射。
TaskPointReachabilityResult legacyTaskResultFromTarget(const TargetEvaluation& evaluation,
                                                       const TaskPoint& point)
{
    TaskPointReachabilityResult result;
    result.taskPoint = point;
    if (!point.enabled) {
        result.status = AnalysisStatus::Unknown;
        result.ik.target = point;
        AnalysisWarning warning;
        warning.code = "KIN_TASK_DISABLED";
        warning.message = "Task point is disabled; skipped from reachability denominator.";
        warning.source = "KinematicAnalyzer";
        warning.severity = AnalysisStatus::Warning;
        result.ik.warnings.push_back (warning);
        return result;
    }
    result.ik = legacyIkResultFromTarget (evaluation, point);
    result.primaryFailure = primaryFailureFromTarget (evaluation);
    if (result.primaryFailure != KinematicFailureReason::None)
        result.failureReasons.push_back (result.primaryFailure);
    if (evaluation.feasibility == Feasibility::Feasible)
        result.status = evaluation.quality == Quality::Degraded ? AnalysisStatus::Warning :
                                                                   AnalysisStatus::Pass;
    else if (evaluation.feasibility == Feasibility::NotEvaluated)
        result.status = AnalysisStatus::Unknown;
    else
        result.status = AnalysisStatus::Fail;
    return result;
}

// 有效容差:任务点显式给定了正的容差就用它,否则回退到全局默认值。
// 约定 0 或负值视为"未指定",避免用户把容差填 0 导致所有解都被判为残差超限。
double effectiveTolerance (double taskTolerance, double defaultTolerance)
{
    return taskTolerance > 0.0 ? taskTolerance : defaultTolerance;
}

// 判断某个解是否带指定失败原因,供 primaryFailureFromIk 按优先级扫描。
bool hasFailureReason (const KinematicIkSolution& solution, KinematicFailureReason reason)
{
    return std::find (solution.failureReasons.begin (), solution.failureReasons.end (), reason) !=
           solution.failureReasons.end ();
}

// 两个关节向量的无穷范数距离:max_i |lhs(i) - rhs(i)|。
// 维度不一致返回 +inf(而非异常),让调用方(去重)总是安全比较。
double qInfDistance (const rw::math::Q& lhs, const rw::math::Q& rhs)
{
    if (lhs.size () != rhs.size ())
        return std::numeric_limits< double >::infinity ();
    double distance = 0.0;
    for (std::size_t i = 0; i < lhs.size (); ++i)
        distance = std::max (distance, std::fabs (lhs (i) - rhs (i)));
    return distance;
}

// 周期性关节(旋转关节)在圆环上的最短角度距离,取值 [0, PI]:
//   |delta| 对 2PI 取模后,再取 min(mod, 2PI - mod)。
// 这样 q 值相差 359 度与相差 1 度的旋转关节被视为"几乎同一个姿态"。
double wrappedAngularDistance (double lhs, double rhs)
{
    const double raw = std::fabs (lhs - rhs);
    if (!std::isfinite (raw))
        return std::numeric_limits< double >::infinity ();
    const double twoPi = 2.0 * rw::math::Pi;
    const double mod   = std::fmod (raw, twoPi);
    return std::min (mod, twoPi - mod);
}

// qInfDistance 的旋转关节感知版本:对掩码中标记为 revolute 的关节用
// wrappedAngularDistance(环形最短距离),其余关节仍用绝对值距离。
// revoluteJoints 掩码由 revoluteJointMask 从设备关节类型生成。
double qInfDistance (const rw::math::Q& lhs,
                     const rw::math::Q& rhs,
                     const std::vector< bool >& revoluteJoints)
{
    if (lhs.size () != rhs.size ())
        return std::numeric_limits< double >::infinity ();
    double distance = 0.0;
    for (std::size_t i = 0; i < lhs.size (); ++i) {
        const bool revolute =
            i < revoluteJoints.size () && revoluteJoints[i];
        const double jointDistance = revolute ?
            wrappedAngularDistance (lhs (i), rhs (i)) :
            std::fabs (lhs (i) - rhs (i));
        distance = std::max (distance, jointDistance);
    }
    return distance;
}

// 生成"每个 q 分量是否为旋转关节"的掩码。关键点:
//   - 只有 JointDevice 能枚举其关节;非 JointDevice 的 Device 无法判断,返回全 false
//     (即全部按绝对距离处理,去重会更保守);
//   - 一个关节可能占多个 q 分量(如自由度 > 1 的关节),这里把整块都标记成
//     revolute;仅 RevoluteJoint 视为旋转关节。
std::vector< bool > revoluteJointMask (
    rw::core::Ptr< rw::models::Device > device)
{
    std::vector< bool > mask;
    if (device == NULL)
        return mask;
    mask.assign (device->getDOF (), false);

    const rw::models::JointDevice* const jointDevice =
        dynamic_cast< const rw::models::JointDevice* > (device.get ());
    if (jointDevice == NULL)
        return mask;

    std::size_t qIndex = 0;
    for (const rw::models::Joint* const joint : jointDevice->getJoints ()) {
        if (qIndex >= mask.size ())
            break;
        const bool revolute =
            dynamic_cast< const rw::models::RevoluteJoint* > (joint) != NULL;
        const std::pair< rw::math::Q, rw::math::Q > bounds =
            joint != NULL ? joint->getBounds () :
                            std::make_pair (rw::math::Q (), rw::math::Q ());
        const std::size_t jointDof =
            bounds.first.size () > 0 ? bounds.first.size () : 1;
        for (std::size_t local = 0;
             local < jointDof && qIndex < mask.size (); ++local, ++qIndex)
            mask[qIndex] = revolute;
    }
    return mask;
}

// 有界性兜底:关节限位可能是正负无穷(未限制),此时用 fallback 代替,避免
// 后续插值/裁剪计算产生 NaN 或溢出。
double finiteBoundOrFallback (double bound, double fallback)
{
    return std::isfinite (bound) ? bound : fallback;
}

// 在关节区间 [lo, hi] 上按 fraction(0..1)线性插值得到种子关节值。
// 对无限限位回退到 current +/- PI;若 hi <= lo(异常区间)则强制用 current +/- PI,
// 保证总能产出有效种子。
double interpolateBound (const rw::math::Q& lower,
                         const rw::math::Q& upper,
                         const rw::math::Q& current,
                         std::size_t index,
                         double fraction)
{
    double lo = finiteBoundOrFallback (lower (index), current (index) - rw::math::Pi);
    double hi = finiteBoundOrFallback (upper (index), current (index) + rw::math::Pi);
    if (!(hi > lo)) {
        lo = current (index) - rw::math::Pi;
        hi = current (index) + rw::math::Pi;
    }
    return lo + fraction * (hi - lo);
}

// 构造"零位"种子:每个关节取 0 并裁剪到 [lo, hi] 内(0 在区间内则保持 0)。
// 对无限限位同样用 current +/- PI 兜底;区间无效时直接用当前值,避免 NaN。
rw::math::Q clampedZeroSeed (const rw::math::Q& lower,
                             const rw::math::Q& upper,
                             const rw::math::Q& current)
{
    rw::math::Q q (current.size ());
    for (std::size_t i = 0; i < current.size (); ++i) {
        const double lo = finiteBoundOrFallback (lower (i), current (i) - rw::math::Pi);
        const double hi = finiteBoundOrFallback (upper (i), current (i) + rw::math::Pi);
        if (hi > lo)
            q (i) = std::min (hi, std::max (lo, 0.0));
        else
            q (i) = current (i);
    }
    return q;
}

// 生成一组确定性 IK 起始种子,使同一 target / state 下重复求解结果稳定:
//   1) 当前 q 本身(最贴近现状的解最容易被迭代收敛到);
//   2) 关节区间中点(避免被当前姿态"吸引"到同一个分支);
//   3) 裁剪到限位内的零位;
//   4) 限位区间内 2^dof 种高低组合的端点插值(0.25 / 0.75),dof <= 16 时枚举全部,
//      否则按位抽取至多 128 个组合,覆盖不同关节分支。
// 用 addUniqueIkCandidate 去重,避免重复起点重复计算。
std::vector< rw::math::Q > deterministicIkSeeds (
    const rw::math::Q& current,
    const std::pair< rw::math::Q, rw::math::Q >& bounds)
{
    std::vector< rw::math::Q > seeds;
    if (current.size () == 0)
        return seeds;

    const double seedProximity = 1e-6;
    rws::addUniqueIkCandidate (seeds, current, seedProximity);

    rw::math::Q center (current.size ());
    for (std::size_t i = 0; i < current.size (); ++i)
        center (i) = interpolateBound (bounds.first, bounds.second, current, i, 0.5);
    rws::addUniqueIkCandidate (seeds, center, seedProximity);
    rws::addUniqueIkCandidate (seeds, clampedZeroSeed (bounds.first, bounds.second, current),
                               seedProximity);

    const std::size_t dof = current.size ();
    const std::size_t exactMaskCount =
        dof < 16 ? (static_cast< std::size_t > (1) << dof) : 0;
    const std::size_t maskCount =
        exactMaskCount == 0 ? static_cast< std::size_t > (128) :
        std::min< std::size_t > (exactMaskCount, 128);
    for (std::size_t mask = 0; mask < maskCount; ++mask) {
        rw::math::Q seed (dof);
        for (std::size_t joint = 0; joint < dof; ++joint) {
            const bool highSide =
                joint < 8 * sizeof (std::size_t) ?
                ((mask & (static_cast< std::size_t > (1) << joint)) != 0) :
                (((mask + joint) % 2) != 0);
            seed (joint) = interpolateBound (
                bounds.first, bounds.second, current, joint, highSide ? 0.75 : 0.25);
        }
        rws::addUniqueIkCandidate (seeds, seed, seedProximity);
    }
    return seeds;
}

// =============================================================================
//  poseReachabilityTarget — 把 (position, rotation) 打包为 TaskPoint
// =============================================================================
// analyzeIk 接受 TaskPoint,所以这里把"内部用的 Vector3D + Rotation3D"反向
// 转换为 "TaskPoint(position, rpyDeg)"。note 字段写上 (direction, roll) 索引,
// 一旦某次 IK 失败,日志/警告可以立刻定位是哪个方向的问题。
TaskPoint poseReachabilityTarget (const std::array< double, 3 >& position,
                                  const rw::math::Rotation3D<>& rotation,
                                  int directionIndex,
                                  int rollIndex)
{
    TaskPoint target;
    target.id       = "pose_reachability";
    target.name     = "Pose reachability target";
    target.position = position;
    const rw::math::RPY<> rpy (rotation);
    const double toDeg = 180.0 / rw::math::Pi;
    target.rpyDeg = {{rpy (0) * toDeg, rpy (1) * toDeg, rpy (2) * toDeg}};
    target.note = std::string ("direction=") + std::to_string (directionIndex) +
                  ", roll=" + std::to_string (rollIndex);
    return target;
}

// =============================================================================
//  meanValue — 算术平均
// =============================================================================
// 求均值;空 vector 返回 0.0(而不是 NaN),让 buildAggregateResult 的
// "manipulability_mean" 始终是有限数。
double meanValue (const std::vector< double >& values)
{
    if (values.empty ())
        return 0.0;
    double sum = 0.0;
    for (double value : values)
        sum += value;
    return sum / static_cast< double > (values.size ());
}

// 构造单个 Q 对应的 WorkspaceSample:FK + 关节裕度 + Jacobian 指标 + 碰撞检测 +
// 状态分类。匿名命名空间内,只依赖 KinematicMetrics.h 与 RobWork 几何。

}    // namespace

void KinematicAnalyzer::setThresholds (const KinematicThresholds& thresholds)
{
    _thresholds = thresholds;
}

const KinematicThresholds& KinematicAnalyzer::thresholds () const
{
    return _thresholds;
}

// =============================================================================
//  analyzeCurrentPose
// =============================================================================
// 评估"当前 state 在选定 device / TCP 帧下的运动学质量"。
// 流程:
//   1) 校验 device / TCP 帧(空 device 直接 Fail,空 TCP 退化到 device 末端);
//   2) 读 q,跑 FK 得到 TCP 位姿;
//   3) 计算 baseJframe 的 6×n Jacobian;
//   4) 关节裕度 + 奇异指标(KinematicMetrics);
//   5) worstStatus 合并两类状态。
//
// 整个方法对调用方的 state 是 const 的,内部不修改它。
KinematicCurrentPoseResult KinematicAnalyzer::analyzeCurrentPose (
    rw::core::Ptr< rw::models::Device > device,
    rw::core::Ptr< const rw::kinematics::Frame > tcpFrame,
    const rw::kinematics::State& state) const
{
    KinematicCurrentPoseResult result;
    result.status = AnalysisStatus::Unknown;   // 默认 Unknown,遇到降级条件会被覆盖。

    // --------------------------------------------------------------------
    // 1) 没有设备 → 立即返回 Fail,不发部分指标(无 device 谈 FK 没意义)。
    // --------------------------------------------------------------------
    if (device == NULL) {
        result.status                       = AnalysisStatus::Fail;
        AnalysisWarning w;
        w.code     = "KIN_NO_DEVICE";
        w.message  = "No device available for kinematic analysis.";
        w.source   = "KinematicAnalyzer";
        w.severity = AnalysisStatus::Fail;
        result.warnings.push_back (w);
        return result;
    }

    // --------------------------------------------------------------------
    // 2) TCP 帧解析:传 NULL 时回退到 device 末端帧(Warning 而非 Fail)。
    //    只有当 device->getEnd() 也为 NULL 时才真正 Fail。
    // --------------------------------------------------------------------
    rw::core::Ptr< const rw::kinematics::Frame > resolvedTcpFrame = tcpFrame;
    if (resolvedTcpFrame == NULL) {
        resolvedTcpFrame = device->getEnd ();
        AnalysisWarning w;
        w.code     = "KIN_TCP_FALLBACK";
        w.message  = "No TCP frame provided; using device end as fallback.";
        w.source   = "KinematicAnalyzer";
        w.severity = AnalysisStatus::Warning;
        result.warnings.push_back (w);
    }
    if (resolvedTcpFrame == NULL) {
        result.status = AnalysisStatus::Fail;
        AnalysisWarning w;
        w.code     = "KIN_NO_TCP";
        w.message  = "Device has no end frame; cannot compute forward kinematics.";
        w.source   = "KinematicAnalyzer";
        w.severity = AnalysisStatus::Fail;
        result.warnings.push_back (w);
        return result;
    }

    AnalysisContext context;
    context.device = device;
    context.tcpFrame = resolvedTcpFrame;
    context.baseState = state;
    context.deviceName = device->getName ();
    context.tcpFrameName = resolvedTcpFrame->getName ();
    context.thresholds = _thresholds;

    ConfigurationEvaluationOptions options;
    options.checkCollision = false;
    const rw::math::Q q = device->getQ (state);
    const ConfigurationEvaluation evaluation =
        ConfigurationEvaluator ().evaluate (context, q, options);

    result.deviceName = context.deviceName;
    result.tcpFrameName = context.tcpFrameName;
    result.q.assign (q.e ().begin (), q.e ().end ());
    result.q.resize (static_cast< std::size_t > (q.size ()));
    result.tcpPosition = {{evaluation.tcpPose.P ()[0], evaluation.tcpPose.P ()[1],
                           evaluation.tcpPose.P ()[2]}};
    const rw::math::RPY<> rpy (evaluation.tcpPose.R ());
    const double toDeg = 180.0 / rw::math::Pi;
    result.tcpRpyDeg = {{rpy[0] * toDeg, rpy[1] * toDeg, rpy[2] * toDeg}};
    result.jointLimitMargins = evaluation.jointLimitMargins;
    result.minJointLimitMargin = evaluation.minimumJointMargin;
    result.jacobianRowMajor = evaluation.jacobianRowMajor;
    result.jacobianRows = evaluation.jacobianRows;
    result.jacobianCols = evaluation.jacobianCols;
    result.singularValues = evaluation.singularValues;
    result.conditionNumber = evaluation.conditionNumber;
    result.manipulability = evaluation.manipulability;
    result.warnings.insert (result.warnings.end (), evaluation.warnings.begin (),
                            evaluation.warnings.end ());

    if (evaluation.feasibility == Feasibility::Infeasible ||
        evaluation.feasibility == Feasibility::DataInsufficient ||
        evaluation.quality == Quality::Critical) {
        result.status = AnalysisStatus::Fail;
    }
    else if (evaluation.quality == Quality::Degraded) {
        result.status = AnalysisStatus::Warning;
    }
    else if (evaluation.feasibility == Feasibility::Feasible) {
        result.status = AnalysisStatus::Pass;
    }

    return result;
}

// =============================================================================
//  analyzeIk
// =============================================================================
// 对一个目标 TaskPoint 解 IK,并对每个原始解计算综合指标 + 评分。
//
// 流程:
//   1) 解析 device / TCP 帧(降级逻辑同 analyzeCurrentPose);
//   2) 用 JacobianIKSolver + 固定 seed 列表求解:
//        - JacobianIKSolver 是基于雅可比伪逆的迭代 IK,seed 决定起点;
//        - seed 列表由当前 Q、关节中心、零位和关节限位内固定组合构成;
//        - 不使用全局随机源,保证同一 target / state 下重复 Solve 结果稳定;
//   3) 对每个候选 q:
//        a) 在副本 state 上 setQ → 副本 state 用来验算 FK / 雅可比 / 碰撞;
//        b) 关节裕度、关节状态(失败原因);
//        c) FK 与目标位姿的位置 L2 / 姿态 EAA 误差;
//        d) 在该 q 处重算 Jacobian 的奇异指标;
//        e) 可选碰撞检查;
//        f) 综合评分(见下方公式);
//        g) 该解的 status:Pass → 叠加 limitStatus → 叠加 singular → 碰撞降级为 Fail;
//   4) sortIkSolutionsForDisplay 按 UI 偏好排序;
//   5) 解集总 status:存在 Pass → Pass,否则存在 Warning → Warning,否则 Fail。
//
// 注:整个方法对 state 是 const 的——所有 setQ 都在副本 solutionState 上。
KinematicIkAnalysisResult KinematicAnalyzer::analyzeIk (
    rw::core::Ptr< rw::models::Device > device,
    rw::core::Ptr< const rw::kinematics::Frame > tcpFrame,
    const rw::kinematics::State& state,
    const TaskPoint& target,
    rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector) const
{
    AnalysisContext context;
    context.device = device;
    context.tcpFrame = tcpFrame;
    context.baseState = state;
    context.collisionDetector = collisionDetector;
    context.thresholds = _thresholds;

    TargetEvaluationOptions options;
    options.checkCollision = collisionDetector != NULL;
    options.positionToleranceMeters = _thresholds.positionToleranceMeters;
    options.orientationToleranceDeg = _thresholds.orientationToleranceDeg;
    const TargetEvaluation evaluation = TargetEvaluator ().evaluate (context, target, options);
    return legacyIkResultFromTarget (evaluation, target);

    KinematicIkAnalysisResult result;
    result.target = target;          // 把目标点也写到结果里,UI 列表不用额外维护
    result.status = AnalysisStatus::Unknown;

    std::string validationError;
    if (!validateTaskPointTarget (target, &validationError) ||
        !std::isfinite (_thresholds.positionToleranceMeters) ||
        _thresholds.positionToleranceMeters < 0.0 ||
        !std::isfinite (_thresholds.orientationToleranceDeg) ||
        _thresholds.orientationToleranceDeg < 0.0) {
        result.status = AnalysisStatus::Fail;
        result.failureReason = KinematicFailureReason::InvalidTarget;
        result.warnings.push_back (makeWarning (
            "KIN_INVALID_TARGET",
            validationError.empty () ? "Kinematic target thresholds are invalid." : validationError,
            AnalysisStatus::Fail));
        return result;
    }

    if (device == NULL) {
        result.status = AnalysisStatus::Fail;
        result.failureReason = KinematicFailureReason::NoDevice;
        AnalysisWarning w;
        w.code     = "KIN_NO_DEVICE";
        w.message  = "No device available for IK analysis.";
        w.source   = "KinematicAnalyzer";
        w.severity = AnalysisStatus::Fail;
        result.warnings.push_back (w);
        return result;
    }

    rw::core::Ptr< const rw::kinematics::Frame > resolvedTcpFrame = tcpFrame;
    if (resolvedTcpFrame == NULL)
        resolvedTcpFrame = device->getEnd ();
    if (resolvedTcpFrame == NULL) {
        result.status = AnalysisStatus::Fail;
        result.failureReason = KinematicFailureReason::NoTcpFrame;
        AnalysisWarning w;
        w.code     = "KIN_NO_TCP";
        w.message  = "No TCP frame available for IK analysis.";
        w.source   = "KinematicAnalyzer";
        w.severity = AnalysisStatus::Fail;
        result.warnings.push_back (w);
        return result;
    }

    // 把目标点(UI 度数)转到 RobWork 的 Transform3D。
    const rw::math::Transform3D<> targetBaseTtcp = taskPointToTransform (target);
    std::vector< rw::math::Q > rawSolutions;
    const double duplicateQThreshold =
        std::isfinite (_thresholds.ikDuplicateQThreshold) &&
        _thresholds.ikDuplicateQThreshold >= 0.0 ?
        _thresholds.ikDuplicateQThreshold : 1e-4;
    const std::vector< bool > duplicateRevoluteMask =
        revoluteJointMask (device);

    // ---- IK 求解 ----
    // 两层 try:
    //   - 内层 catch std::exception:已知异常,带上 ex.what() 帮助定位;
    //   - 外层 catch (...):未知异常兜底,不让 UI 崩溃。
    try {
        // ownedPtr 构造堆上 JacobianIKSolver;Ptr 是 RobWork 的引用计数智能指针。
        rw::invkin::JacobianIKSolver::Ptr solver =
            rw::core::ownedPtr (new rw::invkin::JacobianIKSolver (device, resolvedTcpFrame, state));
        const rw::math::Q seedCurrentQ = device->getQ (state);
        const std::pair< rw::math::Q, rw::math::Q > seedBounds = device->getBounds ();
        const std::vector< rw::math::Q > seeds =
            deterministicIkSeeds (seedCurrentQ, seedBounds);
        for (const rw::math::Q& seed : seeds) {
            rw::kinematics::State seedState = state;
            device->setQ (seed, seedState);
            const std::vector< rw::math::Q > seedSolutions =
                solver->solve (targetBaseTtcp, seedState);
            result.rawCandidateCount += seedSolutions.size ();
            for (const rw::math::Q& q : seedSolutions)
                addUniqueIkCandidate (
                    rawSolutions, q, duplicateQThreshold, duplicateRevoluteMask);
        }
    }
    catch (const std::exception& ex) {
        result.status = AnalysisStatus::Fail;
        result.failureReason = KinematicFailureReason::SolverError;
        AnalysisWarning w;
        w.code     = "KIN_IK_SOLVER_ERROR";
        w.message  = std::string ("IK solver failed: ") + ex.what ();
        w.source   = "KinematicAnalyzer";
        w.severity = AnalysisStatus::Fail;
        result.warnings.push_back (w);
        return result;
    }
    catch (...) {
        result.status = AnalysisStatus::Fail;
        result.failureReason = KinematicFailureReason::SolverError;
        AnalysisWarning w;
        w.code     = "KIN_IK_SOLVER_ERROR";
        w.message  = "IK solver failed with an unknown error.";
        w.source   = "KinematicAnalyzer";
        w.severity = AnalysisStatus::Fail;
        result.warnings.push_back (w);
        return result;
    }

    if (rawSolutions.empty ()) {
        result.status = AnalysisStatus::Fail;
        result.failureReason = KinematicFailureReason::IkNoSolution;
        AnalysisWarning w;
        w.code     = "KIN_IK_NO_SOLUTION";
        w.message  = "No IK solution found for the target pose.";
        w.source   = "KinematicAnalyzer";
        w.severity = AnalysisStatus::Fail;
        result.warnings.push_back (w);
        return result;
    }

    // 缓存当前 q 与 bounds,循环里多次复用。
    const rw::math::Q currentQ = device->getQ (state);
    if (currentQ.size () != device->getDOF ()) {
        result.status = AnalysisStatus::Fail;
        result.failureReason = KinematicFailureReason::SolverError;
        result.warnings.push_back (makeWarning (
            "KIN_CURRENT_Q_DIMENSION",
            "Current joint vector dimension does not match the selected device DOF.",
            AnalysisStatus::Fail));
        return result;
    }
    const std::pair< rw::math::Q, rw::math::Q > bounds = device->getBounds ();
    const double positionTolerance = effectiveTolerance (
        target.tolerance.positionMeters, _thresholds.positionToleranceMeters);
    const double orientationTolerance = effectiveTolerance (
        target.tolerance.orientationDeg, _thresholds.orientationToleranceDeg);
    for (const rw::math::Q& q : rawSolutions) {
        if (q.size () != device->getDOF ()) {
            result.warnings.push_back (makeWarning (
                "KIN_IK_Q_DIMENSION",
                "IK solver returned a joint vector with an unexpected dimension.",
                AnalysisStatus::Fail));
            continue;
        }
        KinematicIkSolution solution;
        solution.q = qToVector (q);
        solution.distanceToCurrentQ = qDistance (currentQ, q);

        // 关键:在副本 state 上跑 setQ,绝不污染调用方传入的 state。
        rw::kinematics::State solutionState = state;
        try {
            device->setQ (q, solutionState);
        }
        catch (const std::exception& ex) {
            result.warnings.push_back (makeWarning (
                "KIN_IK_STATE_ERROR",
                std::string ("Could not apply an IK solution: ") + ex.what (),
                AnalysisStatus::Fail));
            continue;
        }

        // ---- (a) 关节裕度 + 失败原因 ----
        const std::vector< double > margins = calculateJointLimitMargins (q, bounds);
        solution.minJointLimitMargin =
            margins.empty () ? 0.0 : minimumJointLimitMargin (margins);

        std::vector< AnalysisWarning > limitWarnings;
        const AnalysisStatus limitStatus =
            classifyJointLimitMargins (q, bounds, _thresholds, &limitWarnings);
        if (limitStatus == AnalysisStatus::Fail)
            solution.failureReasons.push_back (KinematicFailureReason::JointLimit);
        else if (limitStatus == AnalysisStatus::Warning)
            solution.failureReasons.push_back (KinematicFailureReason::NearJointLimit);

        // ---- (c) 用副本 state 验算 FK,与目标位姿比较 ----
        // 重要:即便 IK 求解器声称"已收敛",由于数值误差,FK 与目标仍可能
        // 有微小偏差。这两个字段就是给用户看"实际能到多准"的指标。
        rw::math::Transform3D<> actualBaseTtcp;
        try {
            actualBaseTtcp = rw::kinematics::Kinematics::frameTframe (
                device->getBase (), resolvedTcpFrame, solutionState);
            solution.positionErrorMeters = positionError (actualBaseTtcp, targetBaseTtcp);
            solution.orientationErrorDeg = orientationErrorDeg (actualBaseTtcp, targetBaseTtcp);
        }
        catch (const std::exception& ex) {
            result.warnings.push_back (makeWarning (
                "KIN_IK_FK_ERROR",
                std::string ("Could not validate an IK solution with FK: ") + ex.what (),
                AnalysisStatus::Fail));
            continue;
        }
        std::vector< AnalysisWarning > residualWarnings;
        const AnalysisStatus residualStatus = classifyTargetResidual (
            solution.positionErrorMeters, solution.orientationErrorDeg,
            positionTolerance, orientationTolerance,
            &solution.failureReasons, &residualWarnings);
        result.warnings.insert (
            result.warnings.end (), residualWarnings.begin (), residualWarnings.end ());

        // ---- (d) 在该 q 处重新算雅可比的奇异指标 ----
        SingularMetrics singular;
        try {
            singular = calculateSingularMetrics (
                device->baseJframe (resolvedTcpFrame, solutionState), _thresholds);
        }
        catch (const std::exception& ex) {
            result.warnings.push_back (makeWarning (
                "KIN_IK_JACOBIAN_ERROR",
                std::string ("Could not evaluate an IK solution Jacobian: ") + ex.what (),
                AnalysisStatus::Fail));
            continue;
        }
        solution.manipulability  = singular.manipulability;
        solution.conditionNumber = singular.conditionNumber;
        if (singular.status == AnalysisStatus::Fail)
            solution.failureReasons.push_back (KinematicFailureReason::Singular);
        else if (singular.status == AnalysisStatus::Warning)
            solution.failureReasons.push_back (KinematicFailureReason::NearSingular);

        // ---- (e) 碰撞检查(可选)----
        // 注意:inCollision 标志决定该解是否计入"reachable"。
        AnalysisStatus collisionStatus = AnalysisStatus::Pass;
        if (collisionDetector != NULL) {
            try {
                rw::proximity::CollisionDetector::QueryResult queryResult;
                solution.inCollision = collisionDetector->inCollision (solutionState, &queryResult);
                if (solution.inCollision)
                    solution.failureReasons.push_back (KinematicFailureReason::Collision);
            }
            catch (const std::exception& ex) {
                collisionStatus = AnalysisStatus::Fail;
                solution.failureReasons.push_back (KinematicFailureReason::SolverError);
                result.warnings.push_back (makeWarning (
                    "KIN_COLLISION_CHECK_ERROR",
                    std::string ("Collision checking failed: ") + ex.what (),
                    AnalysisStatus::Fail));
            }
        }

        // ---- (f) 评分(越小越优)----
        // 公式各项的物理含义:
        //   1e6 (碰撞?)        —— 碰撞是硬否决,加巨额常数让排序时排到最后;
        //   1000 · e_pos       —— 位置误差,放大到与姿态误差同量级(米 × 1000);
        //   e_ori (度)         —— 姿态误差,1:1 计入;
        //   dist_to_q          —— 与当前 q 的 L2 距离,鼓励"少动";
        //   -min_margin        —— 越大越好,所以减去;
        //   -manipulability    —— 越大越好,所以减去。
        // 这只是线性加权和,实际是工程经验值;要更严谨可以归一化后再加权。
        solution.score =
            (solution.inCollision ? 1000000.0 : 0.0) +
            solution.positionErrorMeters * 1000.0 +
            solution.orientationErrorDeg +
            solution.distanceToCurrentQ -
            solution.minJointLimitMargin -
            solution.manipulability;

        // ---- (g) 该解的 status ----
        // 初始 Pass,然后叠加 limitStatus / singular.status / 碰撞(强制 Fail)。
        // worstStatus(A, B) 选 A、B 中"更糟"的那个。
        solution.status = AnalysisStatus::Pass;
        solution.status = worstStatus (solution.status, residualStatus);
        solution.status = worstStatus (solution.status, limitStatus);
        solution.status = worstStatus (solution.status, singular.status);
        solution.status = worstStatus (solution.status, collisionStatus);
        if (solution.inCollision)
            solution.status = AnalysisStatus::Fail;

        result.solutions.push_back (solution);
    }

    if (result.solutions.empty ()) {
        result.status = AnalysisStatus::Fail;
        result.failureReason = KinematicFailureReason::SolverError;
        result.warnings.push_back (makeWarning (
            "KIN_IK_NO_VALID_SOLUTIONS",
            "IK returned candidates, but none could be validated safely.",
            AnalysisStatus::Fail));
        return result;
    }

    // 按 UI 偏好排序(详见 sortIkSolutionsForDisplay 的注释)。
    sortIkSolutionsForDisplay (result.solutions);
    result.usableSolutionCount = countUsableIkSolutions (result.solutions);

    // 解集总状态:Pass 优先 → 否则 Warning → 否则 Fail。
    // 这里不用 worstStatus,因为 worstStatus 在 Pass + Pass 时也是 Pass,
    // 但只要有一个 Pass 就足够——所以用"首个 Pass 即胜出"的短路逻辑。
    result.status = AnalysisStatus::Fail;
    for (const KinematicIkSolution& solution : result.solutions) {
        if (solution.status == AnalysisStatus::Pass) {
            result.status = AnalysisStatus::Pass;
            break;
        }
        if (solution.status == AnalysisStatus::Warning)
            result.status = AnalysisStatus::Warning;
    }
    return result;
}

// =============================================================================
//  sortIkSolutionsForDisplay — UI 排序
// =============================================================================
// 优先级链(从强到弱):
//   1) 无碰撞优先 (inCollision=false 排前)
//   2) 位置误差小者优先
//   3) 姿态误差小者优先
//   4) 关节裕度大者优先
//   5) 可操作度大者优先
//   6) 与当前 q 的距离小者优先
//   7) 关节向量字典序(全部相同时保证稳定排序)
//
// 前 6 条都与 IK 评分公式的方向一致,确保 UI 显示顺序与"机器自动选择最
// 优解"的预期一致。
void rws::sortIkSolutionsForDisplay (std::vector< KinematicIkSolution >& solutions)
{
    std::sort (solutions.begin (), solutions.end (),
               [] (const KinematicIkSolution& lhs, const KinematicIkSolution& rhs) {
                   if (lhs.inCollision != rhs.inCollision)
                       return !lhs.inCollision;
                   if (lhs.positionErrorMeters != rhs.positionErrorMeters)
                       return lhs.positionErrorMeters < rhs.positionErrorMeters;
                   if (lhs.orientationErrorDeg != rhs.orientationErrorDeg)
                       return lhs.orientationErrorDeg < rhs.orientationErrorDeg;
                   if (lhs.minJointLimitMargin != rhs.minJointLimitMargin)
                       return lhs.minJointLimitMargin > rhs.minJointLimitMargin;
                   if (lhs.manipulability != rhs.manipulability)
                       return lhs.manipulability > rhs.manipulability;
                   if (lhs.distanceToCurrentQ != rhs.distanceToCurrentQ)
                       return lhs.distanceToCurrentQ < rhs.distanceToCurrentQ;
                   return lexicographicQLess (lhs.q, rhs.q);
               });
}

void rws::addUniqueIkCandidate (std::vector< rw::math::Q >& candidates,
                                const rw::math::Q& candidate,
                                double proximityLimit)
{
    if (proximityLimit <= 0.0) {
        candidates.push_back (candidate);
        return;
    }
    for (const rw::math::Q& existing : candidates) {
        if (qInfDistance (existing, candidate) <= proximityLimit)
            return;
    }
    candidates.push_back (candidate);
}

void rws::addUniqueIkCandidate (std::vector< rw::math::Q >& candidates,
                                const rw::math::Q& candidate,
                                double proximityLimit,
                                const std::vector< bool >& revoluteJoints)
{
    if (proximityLimit <= 0.0) {
        candidates.push_back (candidate);
        return;
    }
    for (const rw::math::Q& existing : candidates) {
        if (qInfDistance (existing, candidate, revoluteJoints) <= proximityLimit)
            return;
    }
    candidates.push_back (candidate);
}

std::size_t rws::countUsableIkSolutions (
    const std::vector< KinematicIkSolution >& solutions)
{
    std::size_t count = 0;
    for (const KinematicIkSolution& solution : solutions) {
        if (!solution.inCollision && solution.status != AnalysisStatus::Fail)
            ++count;
    }
    return count;
}

// summarizeIkSolutions:单次遍历统计 5 个计数,避免 UI 多次遍历。
// 与 countUsableIkSolutions 行为一致:usable = !inCollision && status != Fail;
// 同时按 status 单独计数。
KinematicIkSummary rws::summarizeIkSolutions (
    const std::vector< KinematicIkSolution >& solutions)
{
    KinematicIkSummary summary;
    summary.totalCount = solutions.size ();
    for (const KinematicIkSolution& solution : solutions) {
        if (!solution.inCollision && solution.status != AnalysisStatus::Fail)
            ++summary.usableCount;
        if (solution.status == AnalysisStatus::Pass)
            ++summary.passCount;
        else if (solution.status == AnalysisStatus::Warning)
            ++summary.warningCount;
        else if (solution.status == AnalysisStatus::Fail)
            ++summary.failCount;
    }
    return summary;
}

// =============================================================================
//  primaryFailureFromIk — 主要失败原因归类
// =============================================================================
// 在 IK 任务点的失败原因归纳中,优先级:
//   IkNoSolution (无解)
//   > Collision  (全部碰撞)
//   > JointLimit > Singular > NearJointLimit > NearSingular(首个匹配)
//   > None       (成功)
//
// 算法:
//   - 没有解 → IkNoSolution;
//   - 扫所有解,只要遇到无碰撞解就标记 anyCollisionFree,并在它的
//     failureReasons 中按优先级挑首个匹配;
//   - 如果解集全碰撞(anyCollisionFree 仍为 false)→ Collision;
//   - 否则 None。
KinematicFailureReason primaryFailureFromIk (const KinematicIkAnalysisResult& ik)
{
    if (ik.solutions.empty ())
        return ik.failureReason == KinematicFailureReason::None ?
            KinematicFailureReason::IkNoSolution : ik.failureReason;
    bool anyCollisionFree = false;
    for (const KinematicIkSolution& s : ik.solutions) {
        if (s.inCollision)
            continue;
        anyCollisionFree = true;
        if (s.status == AnalysisStatus::Pass)
            return KinematicFailureReason::None;
    }
    if (!anyCollisionFree)
        return KinematicFailureReason::Collision;

    const KinematicFailureReason priority[] = {
        KinematicFailureReason::SolverError,
        KinematicFailureReason::TargetResidual,
        KinematicFailureReason::JointLimit,
        KinematicFailureReason::Singular,
        KinematicFailureReason::NearJointLimit,
        KinematicFailureReason::NearSingular
    };
    for (KinematicFailureReason reason : priority) {
        for (const KinematicIkSolution& solution : ik.solutions) {
            if (!solution.inCollision && hasFailureReason (solution, reason))
                return reason;
        }
    }
    return KinematicFailureReason::None;
}

// =============================================================================
//  analyzeTaskPoints — 批量 IK + 任务级状态归类
// =============================================================================
// 对每个 TaskPoint:
//   1) 若 disabled:挂一条 KIN_TASK_DISABLED 警告,IK 留空,推到结果里;
//   2) 否则调 analyzeIk → 根据 IK 结果归类:
//
//      IK 整体状态           → 任务级 status
//      ------------------------------------------------------------
//      无解                  → Fail + IkNoSolution
//      全碰撞                → Fail + Collision
//      primary=JointLimit    → Fail + JointLimit
//      primary=Singular      → Fail + Singular
//      primary=NearJointLim  → Warning + NearJointLimit
//      primary=NearSingular  → Warning + NearSingular
//      primary=None          → Pass
//
//   3) 后校正:若本应 Warning,但实际不存在"无碰撞的 Warning 解"
//      (即所有无碰撞解都 Pass),降级为 Pass,避免"primary 看着像 Warning
//      但实际只是某些解被选为首选"的假阳性。
std::vector< TaskPointReachabilityResult > KinematicAnalyzer::analyzeTaskPoints (
    rw::core::Ptr< rw::models::Device > device,
    rw::core::Ptr< const rw::kinematics::Frame > tcpFrame,
    const rw::kinematics::State& state,
    const std::vector< TaskPoint >& taskPoints,
    rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector) const
{
    AnalysisContext context;
    context.device = device;
    context.tcpFrame = tcpFrame;
    context.baseState = state;
    context.collisionDetector = collisionDetector;
    context.thresholds = _thresholds;

    RequirementExecutionSet requirements;
    requirements.tasks.reserve (taskPoints.size ());
    for (const TaskPoint& point : taskPoints)
        requirements.tasks.push_back (requirementTaskFromLegacyTaskPoint (point));

    BatchRunOptions options;
    options.evidenceStage = AnalysisEvidenceStage::Quick;
    options.targetOptions.evidenceStage = AnalysisEvidenceStage::Quick;
    options.targetOptions.checkCollision = collisionDetector != NULL;
    options.targetOptions.requireCollisionFree = false;
    const RequirementValidationSummary summary =
        validateRequirements (context, requirements, options, CancellationToken ());

    std::vector< TaskPointReachabilityResult > results;
    results.reserve (taskPoints.size ());
    for (std::size_t index = 0; index < taskPoints.size (); ++index) {
        if (index < summary.taskResults.size ())
            results.push_back (legacyTaskResultFromTarget (
                summary.taskResults[index], taskPoints[index]));
        else {
            TaskPointReachabilityResult incomplete;
            incomplete.taskPoint = taskPoints[index];
            incomplete.status = AnalysisStatus::Unknown;
            results.push_back (incomplete);
        }
    }
    return results;
}

// P1 单点分析:workcell-aware。
//   - disabled 任务点:沿用旧逻辑,不跑 resolver,不计入分母;
//   - enabled:先调 TaskPointResolver 解析 refFrame / tcpFrame;
//     * 解析失败 → TaskPointReachabilityResult.status = Fail,
//       primaryFailure 来自 resolver;
//     * 解析成功 → 用 resolved.targetInDeviceBase + resolved.tcpFrame 调旧 analyzeIk。
//   - r.taskPoint 始终保留原 taskPoint(用户输入语义),避免 Report / UI 丢字段。
TaskPointReachabilityResult KinematicAnalyzer::analyzeTaskPoint (
    rw::models::WorkCell* workcell,
    rw::core::Ptr< rw::models::Device > device,
    rw::core::Ptr< const rw::kinematics::Frame > defaultTcpFrame,
    const rw::kinematics::State& state,
    const TaskPoint& taskPoint,
    rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector) const
{
    TaskPointReachabilityResult r;
    r.taskPoint = taskPoint;
    r.status    = AnalysisStatus::Unknown;

    // disabled:直接 Skipped,不跑 resolver,不计 reachable。
    if (!taskPoint.enabled) {
        r.status = AnalysisStatus::Warning;
        r.primaryFailure = KinematicFailureReason::None;
        AnalysisWarning w;
        w.code     = "KIN_TASK_DISABLED";
        w.message  = "Task point is disabled; skipped from reachability denominator.";
        w.source   = "KinematicAnalyzer";
        w.severity = AnalysisStatus::Warning;
        r.ik.warnings.push_back (w);
        r.ik.target = taskPoint;
        return r;
    }

    const ResolvedTaskPoint resolved = resolveTaskPoint (
        workcell, device, defaultTcpFrame, state, taskPoint);
    if (!resolved.valid) {
        r.status         = AnalysisStatus::Fail;
        r.primaryFailure = resolved.failure;
        r.failureReasons.push_back (resolved.failure);
        for (const AnalysisWarning& w : resolved.warnings)
            r.ik.warnings.push_back (w);
        // 把原 target 留在 r.ik.target,便于 Report / UI 展示用户输入;
        // 失败 reason 已经在 warnings 里说明。
        r.ik.target = taskPoint;
        return r;
    }

    // 解析成功:用 resolved 后的 target + tcpFrame 调旧 analyzeIk;
    // warnings 合并 resolver 警告 + IK 警告,保留完整诊断链。
    r.ik = analyzeIk (device, resolved.tcpFrame, state,
                      resolved.targetInDeviceBase, collisionDetector);
    for (const AnalysisWarning& w : resolved.warnings)
        r.ik.warnings.push_back (w);
    if (r.ik.solutions.empty ()) {
        r.status         = AnalysisStatus::Fail;
        r.primaryFailure = primaryFailureFromIk (r.ik);
        r.failureReasons.push_back (r.primaryFailure);
    }
    else {
        r.primaryFailure = primaryFailureFromIk (r.ik);
        r.status = r.ik.status;
        if (r.primaryFailure != KinematicFailureReason::None)
            r.failureReasons.push_back (r.primaryFailure);
    }
    return r;
}

// P1 批量分析:workcell-aware,逐点调 analyzeTaskPoint(workcell-aware)。
std::vector< TaskPointReachabilityResult > KinematicAnalyzer::analyzeTaskPoints (
    rw::models::WorkCell* workcell,
    rw::core::Ptr< rw::models::Device > device,
    rw::core::Ptr< const rw::kinematics::Frame > defaultTcpFrame,
    const rw::kinematics::State& state,
    const std::vector< TaskPoint >& taskPoints,
    rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector) const
{
    std::vector< TaskPointReachabilityResult > results;
    results.reserve (taskPoints.size ());
    for (const TaskPoint& point : taskPoints) {
        results.push_back (analyzeTaskPoint (
            workcell, device, defaultTcpFrame, state, point, collisionDetector));
    }
    return results;
}

// =============================================================================
//  calculateReachableRate — 任务点可达率
// =============================================================================
// 分子:Pass 或 Warning 的任务点数(都算"reachable");
// 分母:启用的任务点总数(disabled 不计入分母);
// 全 disabled → 返回 0.0(避免除零)。
//
// 这就是 README 中"Task point reachable rate counts Pass and Warning as
// reachable and excludes disabled task points" 的实现位置。
double KinematicAnalyzer::calculateReachableRate (
    const std::vector< TaskPointReachabilityResult >& results) const
{
    std::size_t reachable = 0;
    std::size_t enabled   = 0;
    for (const TaskPointReachabilityResult& r : results) {
        if (!r.taskPoint.enabled)
            continue;
        ++enabled;
        if (r.status == AnalysisStatus::Pass || r.status == AnalysisStatus::Warning)
            ++reachable;
    }
    if (enabled == 0)
        return 0.0;
    return static_cast< double > (reachable) / static_cast< double > (enabled);
}

namespace {

// =============================================================================
//  makeWorkspaceSample(匿名命名空间)
// =============================================================================
// 构造单个 Q 对应的 WorkspaceSample:
//   1) 在副本 state 上跑 FK,得到 TCP 位置;
//   2) 关节裕度;
//   3) Jacobian 的奇异指标;
//   4) 可选碰撞检查;
//   5) 按优先级合并状态:inCollision > 奇异 Fail > 奇异 Warning 或 近限位 > Pass。
//
// 设计要点:
//   - 始终在副本 state 上 setQ,不污染调用方;
//   - FK / Jacobian / 碰撞三处都可能抛错,这里用 catch(...) 兜底,
//     但只降级 status,不让一次坏样本阻断整个采样循环;
//   - 状态优先级"碰撞 > 奇异 Fail > ..."是工程经验:即便奇异,只要碰了,
//     物理上也不可达,所以 Fail 优先。
WorkspaceSample makeWorkspaceSample (
    rw::core::Ptr< rw::models::Device > device,
    rw::core::Ptr< const rw::kinematics::Frame > tcpFrame,
    const rw::kinematics::State& baseState,
    const rw::math::Q& q,
    const KinematicThresholds& thresholds,
    bool checkCollision,
    rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector)
{
    WorkspaceSample sample;
    AnalysisContext context;
    context.device = device;
    context.tcpFrame = tcpFrame;
    context.baseState = baseState;
    context.collisionDetector = collisionDetector;
    context.thresholds = thresholds;
    context.collisionRequired = checkCollision;

    ConfigurationEvaluationOptions options;
    options.evidenceStage = AnalysisEvidenceStage::Estimated;
    options.checkCollision = checkCollision;
    const ConfigurationEvaluation evaluation =
        ConfigurationEvaluator ().evaluate (context, q, options);

    sample.stage = evaluation.stage;
    sample.feasibility = evaluation.feasibility;
    sample.quality = evaluation.quality;
    sample.q.reserve (q.size ());
    for (std::size_t i = 0; i < q.size (); ++i)
        sample.q.push_back (q (i));
    sample.tcpPosition[0] = evaluation.tcpPose.P () (0);
    sample.tcpPosition[1] = evaluation.tcpPose.P () (1);
    sample.tcpPosition[2] = evaluation.tcpPose.P () (2);
    sample.manipulability = evaluation.manipulability;
    sample.minJointLimitMargin = evaluation.minimumJointMargin;
    sample.conditionNumber = evaluation.conditionNumber;
    sample.collisionChecked = evaluation.collisionChecked;
    sample.inCollision = evaluation.inCollision;
    if (sample.feasibility == Feasibility::DataInsufficient)
        sample.status = AnalysisStatus::Fail;
    else if (sample.feasibility == Feasibility::Infeasible)
        sample.status = AnalysisStatus::Fail;
    else if (sample.quality == Quality::Degraded)
        sample.status = AnalysisStatus::Warning;
    else if (sample.feasibility == Feasibility::Feasible)
        sample.status = AnalysisStatus::Pass;
    else
        sample.status = AnalysisStatus::Unknown;
    return sample;
}

}    // namespace

// =============================================================================
//  sampleWorkspace — 关节空间采样
// =============================================================================
// 两种模式:
//   - RandomUniform:每关节一个 uniform_real_distribution,用固定种子的
//     std::mt19937 抽样 N 个 q;
//   - Grid:每关节 steps 等距分,总组合 steps^dof;高 DOF 时总组合会爆,
//     故对 steps^dof 增长做 early break,截断到 config.sampleCount
//     (等价于取字典序前 sampleCount 个网格点)。
//
// 入口校验(返回空 vector 而非抛异常):
//   - device / tcpFrame 不能为 NULL;
//   - sampleCount > 0;
//   - dof > 0 且 bounds 维度与 dof 一致;
//   - 每个关节 lo/hi 都是有限数且 hi > lo。
//
// 固定种子的好处:同一份 WorkCell + 同一阈值,在不同机器、不同时间跑出来的
// 样本序列一致,方便做回归测试与对比分析。
std::vector< WorkspaceSample > KinematicAnalyzer::sampleWorkspace (
    rw::core::Ptr< rw::models::Device > device,
    rw::core::Ptr< const rw::kinematics::Frame > tcpFrame,
    const rw::kinematics::State& state,
    const WorkspaceSamplingConfig& config,
    rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector) const
{
    return sampleWorkspace (
        device, tcpFrame, state, config, collisionDetector,
        WorkspaceSamplingRunCallbacks ());
}

std::vector< WorkspaceSample > KinematicAnalyzer::sampleWorkspace (
    rw::core::Ptr< rw::models::Device > device,
    rw::core::Ptr< const rw::kinematics::Frame > tcpFrame,
    const rw::kinematics::State& state,
    const WorkspaceSamplingConfig& config,
    rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector,
    const WorkspaceSamplingRunCallbacks& callbacks) const
{
    std::vector< WorkspaceSample > samples;

    // 把回调封装为本地 lambda:canceled() 返回是否请求取消(回调为空则恒 false),
    // progress(completed, planned) 在回调非空时触发进度通知。
    const auto canceled = [&callbacks] () -> bool {
        return callbacks.isCancellationRequested != NULL &&
               callbacks.isCancellationRequested (callbacks.userData);
    };
    const auto progress = [&callbacks] (std::size_t completed,
                                        std::size_t planned) {
        if (callbacks.onProgress != NULL)
            callbacks.onProgress (completed, planned, callbacks.userData);
    };

    const WorkspaceSamplingConfig sanitized =
        sanitizeWorkspaceSamplingConfig (config, nullptr);

    if (device == NULL)
        return samples;
    if (sanitized.sampleCount <= 0)
        return samples;
    if (tcpFrame == NULL)
        return samples;

    const std::pair< rw::math::Q, rw::math::Q > bounds = device->getBounds ();
    const rw::math::Q& lower = bounds.first;
    const rw::math::Q& upper = bounds.second;
    const std::size_t dof = device->getDOF ();
    if (dof == 0)
        return samples;
    if (lower.size () != dof || upper.size () != dof)
        return samples;
    for (std::size_t i = 0; i < dof; ++i) {
        if (!std::isfinite (lower (i)) || !std::isfinite (upper (i)) || upper (i) <= lower (i))
            return samples;
    }

    // ---- RandomUniform with cancel/progress ----
    if (sanitized.mode == WorkspaceSamplingMode::RandomUniform) {
        std::mt19937 rng (sanitized.randomSeed);
        std::vector< std::uniform_real_distribution< double > > distributions;
        distributions.reserve (dof);
        for (std::size_t i = 0; i < dof; ++i)
            distributions.emplace_back (lower (i), upper (i));

        const std::size_t planned =
            static_cast< std::size_t > (sanitized.sampleCount);
        progress (0, planned);
        samples.reserve (planned);
        for (int sampleIndex = 0; sampleIndex < sanitized.sampleCount; ++sampleIndex) {
            if (canceled ())
                break;
            rw::math::Q q (dof);
            for (std::size_t j = 0; j < dof; ++j)
                q (j) = distributions[j] (rng);
            WorkspaceSample sample = makeWorkspaceSample (
                device, tcpFrame, state, q, _thresholds,
                sanitized.checkCollision, collisionDetector);
            sample.sampleSeed = sanitized.randomSeed;
            sample.sampleCount = sanitized.sampleCount;
            samples.push_back (sample);
            progress (samples.size (), planned);
        }
        return samples;
    }

    // ---- Grid with cancel/progress ----
    const int steps = sanitized.gridStepsPerJoint;
    std::size_t target = 0;
    {
        rws::WorkspaceSamplingDiagnostics gd;
        target = static_cast< std::size_t > (
            rws::plannedWorkspaceSampleCount (sanitized, dof, &gd));
    }

    progress (0, target);
    samples.reserve (target);
    for (std::size_t index = 0; index < target; ++index) {
        if (canceled ())
            break;
        std::size_t cursor = index;
        rw::math::Q q (dof);
        for (std::size_t joint = 0; joint < dof; ++joint) {
            const std::size_t stepIndex = steps <= 1 ? 0u : (cursor % static_cast< std::size_t > (steps));
            cursor /= static_cast< std::size_t > (steps);
            if (steps <= 1) {
                q (joint) = 0.5 * (lower (joint) + upper (joint));
            }
            else {
                const double ratio = static_cast< double > (stepIndex) /
                                     static_cast< double > (steps - 1);
                q (joint) = lower (joint) + ratio * (upper (joint) - lower (joint));
            }
        }
        WorkspaceSample sample = makeWorkspaceSample (
            device, tcpFrame, state, q, _thresholds,
            sanitized.checkCollision, collisionDetector);
        sample.sampleSeed = sanitized.randomSeed;
        sample.sampleCount = sanitized.sampleCount;
        samples.push_back (sample);
        progress (samples.size (), target);
    }
    return samples;
}

// =============================================================================
//  analyzePoseReachability (无回调) — 委托给有回调重载,默认空回调。
// =============================================================================
std::vector< PoseReachabilitySample > KinematicAnalyzer::analyzePoseReachability (
    rw::core::Ptr< rw::models::Device > device,
    rw::core::Ptr< const rw::kinematics::Frame > tcpFrame,
    const rw::kinematics::State& state,
    const std::vector< std::array< double, 3 > >& positions,
    const PoseReachabilityConfig& config,
    rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector) const
{
    return analyzePoseReachability (
        device, tcpFrame, state, positions, config,
        collisionDetector, PoseReachabilityRunCallbacks ());
}

// =============================================================================
//  analyzePoseReachability (带回调) — 协作取消 + progress 通知
// =============================================================================
// 目标:在给定的若干空间位置周围,工具能在多大程度上旋转到任意朝向。
//
// 流程:
//   - sampleUnitDirections 在单位球面上均匀采 directionCount 个方向;
//   - 每个方向绕 Z 轴等分成 rollCount 份滚动;
//   - 总共 directionCount × rollCount 个姿态目标;
//   - 每个姿态构造成 TaskPoint 后调 analyzeIk;
//   - 只要 IK 至少有一个无碰撞 Pass/Warning 解,该方向就算 reachable。
//
// 结果:
//   - sampled       = directionCount × rollCount;
//   - reachable     = 至少一个 Pass/Warning 解的方向数;
//   - coverage      = reachable / sampled ∈ [0, 1];
//   - 状态:reachable=0 → Fail,reachable=sampled → Pass,部分 → Warning。
//
// 用 Fibonacci 螺旋而不是经纬度网格,是为了避免"两极聚集",
// 保证所有方向被同等采样,这样 coverage 指标反映真实可达性而非采样偏差。
std::vector< PoseReachabilitySample > KinematicAnalyzer::analyzePoseReachability (
    rw::core::Ptr< rw::models::Device > device,
    rw::core::Ptr< const rw::kinematics::Frame > tcpFrame,
    const rw::kinematics::State& state,
    const std::vector< std::array< double, 3 > >& positions,
    const PoseReachabilityConfig& config,
    rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector,
    const PoseReachabilityRunCallbacks& callbacks) const
{
    std::vector< PoseReachabilitySample > results;
    results.reserve (positions.size ());

    // P4:用 sanitize + plan helper 统一边界检查和诊断。
    const PoseReachabilityConfig sanitized =
        sanitizePoseReachabilityConfig (config, nullptr);

    const int directionCount = sanitized.directionSamples;
    const std::size_t ikPerPosition =
        poseReachabilityTargetsPerPosition (sanitized);
    const int totalOrientations = static_cast< int > (ikPerPosition);
    bool targetCountOverflowed = false;
    const std::size_t plannedTotal =
        poseReachabilityExecutionTargetCount (
            sanitized, positions.size (), &targetCountOverflowed);
    (void) targetCountOverflowed;
    const std::vector< OrientationTargetSample > orientationTargets =
        generateOrientationTargetSamples (sanitized);

    rw::core::Ptr< const rw::kinematics::Frame > resolvedTcpFrame = tcpFrame;
    if (resolvedTcpFrame == NULL && device != NULL)
        resolvedTcpFrame = device->getEnd ();

    AnalysisContext targetContext;
    targetContext.device = device;
    targetContext.tcpFrame = resolvedTcpFrame;
    targetContext.baseState = state;
    targetContext.collisionDetector = collisionDetector;
    targetContext.thresholds = _thresholds;
    TargetEvaluationOptions targetOptions;
    targetOptions.checkCollision = sanitized.checkCollision && collisionDetector != NULL;
    targetOptions.positionToleranceMeters = _thresholds.positionToleranceMeters;
    targetOptions.orientationToleranceDeg = _thresholds.orientationToleranceDeg;
    TargetEvaluator targetEvaluator;

    std::size_t completedTargets = 0;
    for (const std::array< double, 3 >& position : positions) {
        PoseReachabilitySample sample;
        sample.position          = position;
        sample.sampledDirections = directionCount;
        sample.sampledOrientationSamples = totalOrientations;
        sample.plannedIkTargets  = ikPerPosition;
        std::size_t completedTargetsForSample = 0;
        std::vector< bool > reachableDirectionFlags (
            static_cast< std::size_t > (std::max (0, directionCount)), false);

        // 兜底:device / TCP / 总方向数任一为 0,直接报 Fail。
        if (device == NULL || resolvedTcpFrame == NULL || totalOrientations == 0) {
            sample.completedIkTargets = 0;
            sample.partial = false;
            sample.status = AnalysisStatus::Fail;
            results.push_back (sample);
            continue;
        }

        const auto finishCanceledSample =
            [&sample, directionCount, totalOrientations, ikPerPosition,
             &completedTargetsForSample] () {
            sample.completedIkTargets = completedTargetsForSample;
            sample.plannedIkTargets = ikPerPosition;
            sample.partial = completedTargetsForSample < ikPerPosition;
            // Keep any representative Q already found before cancellation.
            sample.status = sample.reachableOrientationSamples == 0 ?
                AnalysisStatus::Fail : AnalysisStatus::Warning;
            sample.directionCoverage = directionCount == 0 ? 0.0 :
                static_cast< double > (sample.reachableDirections) /
                    static_cast< double > (directionCount);
            sample.orientationCoverage = totalOrientations == 0 ? 0.0 :
                static_cast< double > (sample.reachableOrientationSamples) /
                    static_cast< double > (totalOrientations);
            sample.coverage = sample.orientationCoverage;
        };
        const auto cancellationRequested = [&callbacks] () {
            return callbacks.isCancellationRequested != NULL &&
                   callbacks.isCancellationRequested (callbacks.userData);
        };

        // P5:先在 position 边界检查一次,避免已经取消时继续进入 IK 循环。
        if (cancellationRequested ()) {
            finishCanceledSample ();
            results.push_back (sample);
            return results;
        }

        // 每个确定性方向/滚转样本对应一次 IK。
        for (const OrientationTargetSample& orientation : orientationTargets) {
                const int directionIndex = orientation.directionIndex;
                const int rollIndex = orientation.rollIndex;
                if (cancellationRequested ()) {
                    finishCanceledSample ();
                    results.push_back (sample);
                    return results;
                }

                // 把 (position, rotation) 包成 TaskPoint。
                const TaskPoint target =
                    poseReachabilityTarget (position, orientation.rotation,
                                            directionIndex, rollIndex);
                const TargetEvaluation evaluation = targetEvaluator.evaluate (
                    targetContext, target, targetOptions);
                const bool directionReachable = isDirectionTargetReachable (
                    evaluation, orientation.rotation,
                    targetOptions.positionToleranceMeters,
                    targetOptions.orientationToleranceDeg);
                const bool orientationReachable = isOrientationTargetReachable (
                    evaluation, targetOptions.positionToleranceMeters,
                    targetOptions.orientationToleranceDeg);
                if (directionReachable) {
                    const std::size_t directionOffset =
                        static_cast< std::size_t > (directionIndex);
                    if (!reachableDirectionFlags[directionOffset]) {
                        reachableDirectionFlags[directionOffset] = true;
                        ++sample.reachableDirections;
                    }
                }
                if (orientationReachable) {
                    ++sample.reachableOrientationSamples;
                    // P10:保存第一个可达解的代表性 Q。
                    if (!sample.hasRepresentativeQ) {
                        for (const TargetCandidate& candidate : evaluation.candidates) {
                            if (candidate.configuration.feasibility != Feasibility::Feasible ||
                                candidate.configuration.inCollision ||
                                candidate.positionErrorMeters >
                                    targetOptions.positionToleranceMeters ||
                                candidate.orientationErrorDeg >
                                    targetOptions.orientationToleranceDeg)
                                continue;
                            if (!candidate.configuration.q.empty ()) {
                                    sample.hasRepresentativeQ = true;
                                    sample.representativeQ = qToVector (candidate.configuration.q);
                                    sample.representativeDirectionIndex =
                                        directionIndex;
                                    sample.representativeRollIndex = rollIndex;
                                break;
                            }
                        }
                    }
                }
                ++completedTargets;
                ++completedTargetsForSample;
                sample.completedIkTargets = completedTargetsForSample;

                if (callbacks.onProgress != NULL)
                    callbacks.onProgress (completedTargets, plannedTotal, callbacks.userData);
                if (cancellationRequested ()) {
                    finishCanceledSample ();
                    results.push_back (sample);
                    return results;
                }
        }

        sample.directionCoverage =
            directionCount == 0 ? 0.0 :
            static_cast< double > (sample.reachableDirections) /
                static_cast< double > (directionCount);
        sample.orientationCoverage =
            totalOrientations == 0 ? 0.0 :
            static_cast< double > (sample.reachableOrientationSamples) /
                static_cast< double > (totalOrientations);
        sample.coverage = sample.orientationCoverage;
        sample.completedIkTargets = completedTargetsForSample;
        sample.partial = false;
        if (sample.reachableOrientationSamples == 0)
            sample.status = AnalysisStatus::Fail;
        else if (sample.reachableOrientationSamples == totalOrientations)
            sample.status = AnalysisStatus::Pass;
        else
            sample.status = AnalysisStatus::Warning;
        results.push_back (sample);

    }
    return results;
}

// =============================================================================
//  buildAggregateResult — 报告聚合
// =============================================================================
// 把四种分析结果聚合成一个总报告:
//   1) 写 header(pluginName + pluginVersion);
//   2) reachableRate = calculateReachableRate(taskPointResults);
//   3) status 用 worstStatus 合并四类子结果(从 currentPose.status 起步,
//      依次与任务点 / workspace / poseReachability 合并);
//   4) manipulabilityMap:收集 currentPose 与所有 workspace 样本的可操作度,
//      排序后取 min / max / mean / p10(p10 取 10% 分位);
//   5) 按 warning.code 子串匹配把奇异 / 关节告警分桶到
//      singularityWarnings / jointLimitWarnings;
//   6) workspace 样本中:有碰撞的 Fail → 通用警告 KIN_WORKSPACE_COLLISION;
//      状态为 Warning 的 → jointLimitWarnings 中追加 KIN_WORKSPACE_QUALITY_WARNING。
//
// 注意:这里用 warning.code 子串匹配分桶是一种"松散约定",
// 字符串不变则分类就不变。如果将来重命名告警 code,需同步改这里。
KinematicAnalysisResult KinematicAnalyzer::buildAggregateResult (
    const KinematicCurrentPoseResult& currentPose,
    const std::vector< TaskPointReachabilityResult >& taskPointResults,
    const std::vector< WorkspaceSample >& workspaceSamples,
    const std::vector< PoseReachabilitySample >& poseReachability) const
{
    KinematicAnalysisResult result;
    result.header.pluginName    = "KinematicAnalysis";
    result.header.pluginVersion = "1.0.0";
    // 四类结果原样塞进聚合体,JSON / CSV 导出按这些字段读取。
    result.currentPose          = currentPose;
    result.taskPointResults     = taskPointResults;
    result.workspaceSamples     = workspaceSamples;
    result.poseReachability     = poseReachability;
    // 任务点可达率放在 result 顶层(便于 Report tab 一行展示)。
    result.reachableRate        = calculateReachableRate (taskPointResults);

    // worstStatus 从 currentPose 开始,逐步合并四类结果的状态。
    result.status = currentPose.status;
    for (const TaskPointReachabilityResult& task : taskPointResults)
        result.status = worstStatus (result.status, task.status);
    for (const WorkspaceSample& sample : workspaceSamples)
        result.status = worstStatus (result.status, sample.status);
    for (const PoseReachabilitySample& sample : poseReachability)
        result.status = worstStatus (result.status, sample.status);

    switch (result.status) {
    case AnalysisStatus::Fail:
        result.feasibility = Feasibility::Infeasible;
        result.quality = Quality::Critical;
        break;
    case AnalysisStatus::Warning:
        result.feasibility = Feasibility::Feasible;
        result.quality = Quality::Degraded;
        break;
    case AnalysisStatus::Pass:
        result.feasibility = Feasibility::Feasible;
        result.quality = Quality::Good;
        break;
    case AnalysisStatus::Unknown:
    default:
        result.feasibility = Feasibility::NotEvaluated;
        result.quality = Quality::Unknown;
        break;
    }

    // 收集可操作度样本:currentPose + 所有 workspace(过滤掉 0 值)。
    std::vector< double > manipulabilityValues;
    manipulabilityValues.reserve (workspaceSamples.size () + 1);
    if (currentPose.manipulability > 0.0)
        manipulabilityValues.push_back (currentPose.manipulability);
    for (const WorkspaceSample& sample : workspaceSamples) {
        if (sample.manipulability > 0.0)
            manipulabilityValues.push_back (sample.manipulability);

        // workspace 碰撞:即便单个样本 Fail,仍作为"工程提醒"以 Warning 上报。
        if (sample.status == AnalysisStatus::Fail && sample.inCollision)
            result.warnings.push_back (makeWarning (
                "KIN_WORKSPACE_COLLISION",
                "At least one workspace sample is in collision.",
                AnalysisStatus::Warning));
        // workspace 警告样本:归到 jointLimitWarnings 桶(名称虽含 limit,
        // 实际承载"关节限位 / 奇异"两类质量退化告警)。
        if (sample.status == AnalysisStatus::Warning)
            result.jointLimitWarnings.push_back (makeWarning (
                "KIN_WORKSPACE_QUALITY_WARNING",
                "A workspace sample is near a joint limit or singularity.",
                AnalysisStatus::Warning));
    }

    if (!manipulabilityValues.empty ()) {
        // 排序后用索引取 min / max / p10,mean 用单独函数。
        // p10 取 10% 分位:在已排序数组上用线性插值近似,
        // 这里简化为 floor(0.1 * (n - 1)) 直接索引,精度够 UI 显示。
        std::sort (manipulabilityValues.begin (), manipulabilityValues.end ());
        MetricValue minMetric;
        minMetric.name  = "manipulability_min";
        minMetric.value = manipulabilityValues.front ();
        MetricValue maxMetric;
        maxMetric.name  = "manipulability_max";
        maxMetric.value = manipulabilityValues.back ();
        MetricValue meanMetric;
        meanMetric.name  = "manipulability_mean";
        meanMetric.value = meanValue (manipulabilityValues);
        MetricValue p10Metric;
        p10Metric.name = "manipulability_p10";
        const std::size_t p10Index =
            static_cast< std::size_t > (0.1 * static_cast< double > (manipulabilityValues.size () - 1));
        p10Metric.value = manipulabilityValues[p10Index];
        result.manipulabilityMap.push_back (minMetric);
        result.manipulabilityMap.push_back (maxMetric);
        result.manipulabilityMap.push_back (meanMetric);
        result.manipulabilityMap.push_back (p10Metric);
    }

    // currentPose 自带的告警:既入 result.warnings,又按 code 子串分桶。
    for (const AnalysisWarning& warning : currentPose.warnings) {
        result.warnings.push_back (warning);
        if (warning.code.find ("SINGULAR") != std::string::npos ||
            warning.code.find ("CONDITION") != std::string::npos)
            result.singularityWarnings.push_back (warning);
        if (warning.code.find ("JOINT") != std::string::npos ||
            warning.code.find ("LIMIT") != std::string::npos)
            result.jointLimitWarnings.push_back (warning);
    }

    return result;
}
