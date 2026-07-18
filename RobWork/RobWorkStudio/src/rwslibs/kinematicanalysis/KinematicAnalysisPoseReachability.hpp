#ifndef RWS_KINEMATICANALYSIS_KINEMATICANALYSISPOSEREACHABILITY_HPP
#define RWS_KINEMATICANALYSIS_KINEMATICANALYSISPOSEREACHABILITY_HPP

#include "KinematicAnalysisTypes.hpp"

#include <cstddef>
#include <vector>

namespace rws {

// 硬上限:方向采样 / 滚动采样 / 总 IK 目标数,防止 UI 给离谱值炸内存。
static const int MaxPoseDirectionSamples = 1000;
static const int MaxPoseRollSamples = 360;
static const std::size_t MaxPoseReachabilityTargets = 1000000;

//! @brief 把用户的 PoseReachabilityConfig 清洗为合法值,记下被修正的字段。
PoseReachabilityConfig sanitizePoseReachabilityConfig (
    const PoseReachabilityConfig& config,
    PoseReachabilityDiagnostics* diagnostics = nullptr);

//! @brief 算 positionCount × directions × rolls 后的总 IK 目标数,
//!        超过 MaxPoseReachabilityTargets 时截断并标记 targetCountCapped。
std::size_t plannedPoseReachabilityTargetCount (
    const PoseReachabilityConfig& config,
    std::size_t positionCount,
    PoseReachabilityDiagnostics* diagnostics = nullptr);

//! @brief 返回单个 position 的精确执行 IK 目标数(sanitized directions × rolls)。
//!        不经过 MaxPoseReachabilityTargets 上限,用于 progress denominator。
std::size_t poseReachabilityTargetsPerPosition (
    const PoseReachabilityConfig& config);

//! @brief 返回所有 positions 的精确执行 IK 目标总数。
//!        不经过 MaxPoseReachabilityTargets 上限;overflowed 在 size_t 溢出时设 true。
std::size_t poseReachabilityExecutionTargetCount (
    const PoseReachabilityConfig& config,
    std::size_t positionCount,
    bool* overflowed = nullptr);

//! @brief 一次性算 samples 的状态分布 + min/max/avg coverage。
PoseReachabilitySummary summarizePoseReachabilitySamples (
    const std::vector< PoseReachabilitySample >& samples);

//! @brief 判断 IK 解集中是否至少有一个无碰撞的 Pass / Warning 解。
//!        替换 analyzePoseReachability 中的内联 reachable 判定。
bool isPoseDirectionReachable (
    const std::vector< KinematicIkSolution >& solutions);

}    // namespace rws

#endif    // RWS_KINEMATICANALYSIS_KINEMATICANALYSISPOSEREACHABILITY_HPP
