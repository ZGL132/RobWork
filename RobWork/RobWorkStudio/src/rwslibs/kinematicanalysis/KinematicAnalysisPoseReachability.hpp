#ifndef RWS_KINEMATICANALYSIS_KINEMATICANALYSISPOSEREACHABILITY_HPP
#define RWS_KINEMATICANALYSIS_KINEMATICANALYSISPOSEREACHABILITY_HPP

#include "KinematicAnalysisTypes.hpp"

#include <cstddef>
#include <vector>

namespace rws {

// =============================================================================
//  位姿可达性辅助模块(独立于 KinematicAnalyzer,纯函数,无 Qt 依赖)
// =============================================================================
//
// 与 Workspace 辅助模块设计相同:把"sanitize / planned count / summary / 可达性判定"
// 这些纯计算从 KinematicAnalyzer 抽出。
// 注意该模块把"诊断上限"和"执行目标计数"分开了:
//   - plannedPoseReachabilityTargetCount 受 MaxPoseReachabilityTargets 限制,
//     仅用于诊断(用户可见的 UI diagnostics 文本);
//   - poseReachabilityExecutionTargetCount 不受该上限限制,用于 progress
//     真实分母,避免出现 progress > 100% 的视觉错误。

// 硬上限:方向采样 / 滚动采样 / 总 IK 目标数,防止 UI 给离谱值炸内存。
//   - MaxPoseDirectionSamples  = 1000:单位球方向采样数上限(超过即截断);
//   - MaxPoseRollSamples       = 360  :每个方向的滚动采样上限(超过即截断);
//   - MaxPoseReachabilityTargets = 1,000,000:诊断用总目标数上限
//                                     (超过即标记 capped,不影响实际执行)。
static const int MaxPoseDirectionSamples = 1000;
static const int MaxPoseRollSamples = 360;
static const std::size_t MaxPoseReachabilityTargets = 1000000;

//! @brief 把用户的 PoseReachabilityConfig 清洗为合法值,记下被修正的字段。
//!
//! 规则:
//!   - directionSamples < 0  → 0(directionSamplesClamped = true);
//!   - directionSamples > MaxPoseDirectionSamples → MaxPoseDirectionSamples;
//!   - rollSamples < 1  → 1(rollSamplesClamped = true);
//!   - rollSamples > MaxPoseRollSamples → MaxPoseRollSamples。
//! 注意 rollSamples 必须 ≥ 1(0 方向会让所有方向都退化,直接失败)。
//!
//! @param config      输入配置
//! @param diagnostics 可选;nullptr 时不写诊断
//! @return sanitized config
PoseReachabilityConfig sanitizePoseReachabilityConfig (
    const PoseReachabilityConfig& config,
    PoseReachabilityDiagnostics* diagnostics = nullptr);

//! @brief 算 positionCount × directions × rolls 后的总 IK 目标数,
//!        超过 MaxPoseReachabilityTargets 时截断并标记 targetCountCapped。
//!
//! 这是一个受诊断上限保护的安全版本,只用于给用户展示"理论总 IK 数"。
//! 真实 progress 分母应使用 poseReachabilityExecutionTargetCount 以保证不
//! 出现"progress > 100%"的视觉异常。
std::size_t plannedPoseReachabilityTargetCount (
    const PoseReachabilityConfig& config,
    std::size_t positionCount,
    PoseReachabilityDiagnostics* diagnostics = nullptr);

//! @brief 返回单个 position 的精确执行 IK 目标数(sanitized directions × rolls)。
//!        不经过 MaxPoseReachabilityTargets 上限,用于 progress denominator。
//! 若 directionSamples <= 0 则返回 0(没有 IK 目标)。
std::size_t poseReachabilityTargetsPerPosition (
    const PoseReachabilityConfig& config);

//! @brief 返回所有 positions 的精确执行 IK 目标总数。
//!        不经过 MaxPoseReachabilityTargets 上限;overflowed 在 size_t 溢出时设 true。
//! 实际乘法使用 multiplyCapped 避免溢出,cap 为 std::size_t::max()(允许真实值)。
//!
//! @param config      sanitized config
//! @param positionCount 总位置数
//! @param overflowed  可选输出参数;true 表示发生过 size_t 溢出
//! @return 总目标数(= positionCount × perPosition)
std::size_t poseReachabilityExecutionTargetCount (
    const PoseReachabilityConfig& config,
    std::size_t positionCount,
    bool* overflowed = nullptr);

//! @brief 一次性算 samples 的状态分布 + min/max/avg coverage。
//!
//! status 分类计数 + 所有位置 coverage 求和 + 取 min/max/avg。
//! 注意 totalPositions == 0 时 averageCoverage = 0(避免除零)。
//! 供 UI summary 与 Report 复用,避免 UI 端重新遍历 samples 列表。
PoseReachabilitySummary summarizePoseReachabilitySamples (
    const std::vector< PoseReachabilitySample >& samples);

//! @brief 判断 IK 解集中是否至少有一个无碰撞的 Pass / Warning 解。
//!
//! 判定标准:
//!   - 解 inCollision == false;
//!   - 解 status == Pass 或 Warning(不能是 Fail)。
//! 用于替换 analyzePoseReachability 中原来的内联 reachable 判定循环,
//! 集中到 helper 后既便于单测,又方便未来扩展判定条件(如最低 manipulability 阈值)。
bool isPoseDirectionReachable (
    const std::vector< KinematicIkSolution >& solutions);

}    // namespace rws

#endif    // RWS_KINEMATICANALYSIS_KINEMATICANALYSISPOSEREACHABILITY_HPP
