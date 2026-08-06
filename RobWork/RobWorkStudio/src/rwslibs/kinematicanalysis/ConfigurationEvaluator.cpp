// =============================================================================
//  ConfigurationEvaluator.cpp —— 关节配置评估器实现
// =============================================================================
//
// 实现 ConfigurationEvaluator::evaluate 的完整评估流水线:
//   1. 前置校验:Device / TCP 帧 / 碰撞检测器是否可用,缺失时以
//      DataInsufficient + Critical 快速返回,并写入明确的诊断码(如
//      KIN_CONTEXT_NO_DEVICE / KIN_COLLISION_DETECTOR_UNAVAILABLE);
//   2. 正运动学与关节裕度:setQ(q) 后计算 TCP 位姿与各关节归一化裕度,
//      按阈值判定是否接近 / 超出关节限位;
//   3. 奇异分析:对 TCP 雅可比做 SVD,得到奇异值 / 条件数 / 可操作度,
//      按阈值判定是否奇异 / 近奇异;
//   4. 碰撞检查:可选执行,命中碰撞则记录 Collision 失败原因;
//   5. 状态聚合:finalizeStatus 把"硬性失败原因"聚合为 Infeasible,
//      把告警聚合为 Good / Degraded / Critical。
//
// 底层任何异常(如 Device::setQ 抛错)都被捕获并记录为 SolverError,
// 保证本评估器对外始终返回一个字段结构完整的 ConfigurationEvaluation。
#include "ConfigurationEvaluator.hpp"

#include "KinematicMetrics.hpp"

#include <rw/kinematics/Kinematics.hpp>

#include <algorithm>
#include <exception>

namespace rws {
namespace {

// -----------------------------------------------------------------------------
// 内部辅助函数(位于匿名命名空间,仅本翻译单元可见)
// -----------------------------------------------------------------------------
//
// appendReason:去重地向 result.failureReasons 追加一个失败原因。
// 失败原因最终会被 finalizeStatus 用来判定 hardFailure,重复记录会扭曲
// 该统计(hardFailure 只需"存在"即可触发 Infeasible),故先查重再追加。
void appendReason(ConfigurationEvaluation& result, KinematicFailureReason reason)
{
    if (std::find (result.failureReasons.begin (), result.failureReasons.end (), reason) ==
        result.failureReasons.end ()) {
        result.failureReasons.push_back (reason);
    }
}

// appendWarning:构造一条来源固定为 "ConfigurationEvaluator" 的分析告警并加入结果。
// 告警用于向用户解释"为什么评估被降级或拒绝",code 是供 UI / 测试匹配的稳定标识。
void appendWarning(ConfigurationEvaluation& result,
                   const char* code,
                   const char* message,
                   AnalysisStatus severity = AnalysisStatus::Warning)
{
    AnalysisWarning warning;
    warning.code = code;
    warning.message = message;
    warning.source = "ConfigurationEvaluator";
    warning.severity = severity;
    result.warnings.push_back (warning);
}

// finalizeStatus:根据已收集的失败原因与告警,最终确定结果的状态。
// 聚合不变量:
//   - DataInsufficient 是"最高优先级"状态,一旦出现立即返回 Critical;
//   - 除 NearJointLimit / NearSingular 外的失败原因均视为"硬失败",
//     存在任一硬失败 => Infeasible + Critical;
//   - 无硬失败 => Feasible;若同时存在告警则质量为 Degraded,否则 Good。
void finalizeStatus(ConfigurationEvaluation& result)
{
    if (result.feasibility == Feasibility::DataInsufficient) {
        result.quality = Quality::Critical;
        return;
    }

    const bool hardFailure = std::any_of (
        result.failureReasons.begin (), result.failureReasons.end (),
        [] (KinematicFailureReason reason) {
            return reason != KinematicFailureReason::NearJointLimit &&
                   reason != KinematicFailureReason::NearSingular;
        });
    if (hardFailure) {
        result.feasibility = Feasibility::Infeasible;
        result.quality = Quality::Critical;
        return;
    }

    result.feasibility = Feasibility::Feasible;
    result.quality = result.warnings.empty () ? Quality::Good : Quality::Degraded;
}

} // namespace

// -----------------------------------------------------------------------------
// ConfigurationEvaluator::evaluate —— 完整配置评估
// -----------------------------------------------------------------------------
//
// 评估流程与返回约定见头文件。内部各阶段被 try/catch 包住,任何异常
// (如 setQ / FK / SVD 抛错)都会被转换成 SolverError,不会向调用方抛出。
ConfigurationEvaluation ConfigurationEvaluator::evaluate(
    const AnalysisContext& context,
    const rw::math::Q& q,
    const ConfigurationEvaluationOptions& options) const
{
    ConfigurationEvaluation result;
    result.stage = options.evidenceStage;
    result.q = q;
    result.warnings = context.capabilityWarnings;

    if (context.device == nullptr) {
        appendReason (result, KinematicFailureReason::NoDevice);
        appendWarning (result, "KIN_CONTEXT_NO_DEVICE", "The analysis Device is unavailable.",
                       AnalysisStatus::Fail);
        result.feasibility = Feasibility::DataInsufficient;
        result.quality = Quality::Critical;
        return result;
    }
    if (context.tcpFrame == nullptr) {
        appendReason (result, KinematicFailureReason::NoTcpFrame);
        appendWarning (result, "KIN_CONTEXT_NO_TCP", "The analysis TCP frame is unavailable.",
                       AnalysisStatus::Fail);
        result.feasibility = Feasibility::DataInsufficient;
        result.quality = Quality::Critical;
        return result;
    }

    // 碰撞要求取"上下文声明"与"本次选项"的并集:
    // 任一要求无碰撞,就必须能拿到可用的碰撞检测器,否则以 DataInsufficient 拒绝。
    const bool collisionRequired = context.collisionRequired || options.requireCollisionFree;
    if (collisionRequired && (!options.checkCollision || context.collisionDetector == nullptr)) {
        appendReason (result, KinematicFailureReason::CollisionDetectorUnavailable);
        appendWarning (result, "KIN_COLLISION_DETECTOR_UNAVAILABLE",
                       "Collision-free evaluation was required but could not be completed.",
                       AnalysisStatus::Fail);
        result.feasibility = Feasibility::DataInsufficient;
        result.quality = Quality::Critical;
        return result;
    }

    // ---- 阶段 1:正运动学与关节裕度 ----------------------------------------
    // 基于 baseState 派生新状态并写入 q,再 FK 得到 TCP 位姿;随后计算各关节
    // 的归一化裕度,并按阈值判定是否接近/超出限位。
    rw::kinematics::State state = context.baseState;
    try {
        context.device->setQ (q, state);
        result.tcpPose = rw::kinematics::Kinematics::frameTframe (
            context.device->getBase (), context.tcpFrame.get (), state);

        const std::pair< rw::math::Q, rw::math::Q > bounds = context.device->getBounds ();
        result.jointLimitMargins = calculateJointLimitMargins (q, bounds);
        result.minimumJointMargin = minimumJointLimitMargin (result.jointLimitMargins);
        std::vector< AnalysisWarning > marginWarnings;
        const AnalysisStatus marginStatus = classifyJointLimitMargins (
            q, bounds, context.thresholds, &marginWarnings);
        result.warnings.insert (result.warnings.end (), marginWarnings.begin (), marginWarnings.end ());
        if (marginStatus == AnalysisStatus::Fail)
            appendReason (result, KinematicFailureReason::JointLimit);
        else if (marginStatus == AnalysisStatus::Warning)
            appendReason (result, KinematicFailureReason::NearJointLimit);

        // ---- 阶段 2:奇异分析 ------------------------------------------------
        // 对 TCP 雅可比做 SVD 得到奇异值/条件数/可操作度,按阈值判定是否奇异。
        const rw::math::Jacobian jacobian =
            context.device->baseJframe (context.tcpFrame.get (), state);
        result.jacobianRows = static_cast< int > (jacobian.size1 ());
        result.jacobianCols = static_cast< int > (jacobian.size2 ());
        if (result.jacobianRows > 0 && result.jacobianCols > 0) {
            result.jacobianRowMajor.assign (
                jacobian.e ().data (), jacobian.e ().data () + jacobian.e ().size ());
        }
        const SingularMetrics singular =
            calculateSingularMetrics (jacobian, context.thresholds);
        result.singularValues = singular.singularValues;
        result.conditionNumber = singular.conditionNumber;
        result.manipulability = singular.manipulability;
        result.warnings.insert (result.warnings.end (), singular.warnings.begin (), singular.warnings.end ());
        if (singular.status == AnalysisStatus::Fail)
            appendReason (result, KinematicFailureReason::Singular);
        else if (singular.status == AnalysisStatus::Warning)
            appendReason (result, KinematicFailureReason::NearSingular);

        // ---- 阶段 3:碰撞检查 ------------------------------------------------
        // 仅当选项开启且检测器可用时执行;结果独立记录,不因碰撞检测器缺失而中断。
        if (options.checkCollision && context.collisionDetector != nullptr) {
            rw::proximity::CollisionDetector::QueryResult queryResult;
            result.collisionChecked = true;
            result.inCollision = context.collisionDetector->inCollision (state, &queryResult);
            if (result.inCollision)
                appendReason (result, KinematicFailureReason::Collision);
        }
    }
    catch (const std::exception& ex) {
        appendReason (result, KinematicFailureReason::SolverError);
        appendWarning (result, "KIN_CONFIGURATION_EVALUATION_ERROR", ex.what (),
                       AnalysisStatus::Fail);
    }

    finalizeStatus (result);
    return result;
}

} // namespace rws
