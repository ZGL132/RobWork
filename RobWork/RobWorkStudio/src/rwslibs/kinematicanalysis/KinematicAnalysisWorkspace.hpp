#ifndef RWS_KINEMATICANALYSIS_KINEMATICANALYSISWORKSPACE_HPP
#define RWS_KINEMATICANALYSIS_KINEMATICANALYSISWORKSPACE_HPP

#include "KinematicAnalysisTypes.hpp"

#include <cstddef>
#include <vector>

namespace rws {

// 硬上限:防止 UI / 脚本给 Workspace 一个离谱配置炸内存。
static const int MaxWorkspaceSampleCount     = 1000000;
static const int MaxWorkspaceGridStepsPerJoint = 100;

//! @brief 把用户的 WorkspaceSamplingConfig 清洗为合法值,记下被修正的字段。
//! 规则:
//!   - sampleCount < 0  → 0(sampleCountClamped = true);
//!   - sampleCount > Max  → Max;
//!   - gridStepsPerJoint < 1 → 1(gridStepsClamped = true);
//!   - gridStepsPerJoint > Max → Max;
//!   - randomSeed == 0    → 1(randomSeedAdjusted = true)。
//! diagnostics 为 nullptr 时不写诊断但仍返回 sanitized config。
WorkspaceSamplingConfig sanitizeWorkspaceSamplingConfig (
    const WorkspaceSamplingConfig& config,
    WorkspaceSamplingDiagnostics* diagnostics = nullptr);

//! @brief 给定 dof 算"Grid 模式理论总组合 + sampleCount 上限"后的实际 plan。
//! RandomUniform 模式直接返回 sanitized.sampleCount;
//! Grid 模式按 dof × steps 算理论值,与 sampleCount 取小;
//! 理论值 > plan 时把 gridCountTruncated 标 true。
std::size_t plannedWorkspaceSampleCount (
    const WorkspaceSamplingConfig& config,
    std::size_t dof,
    WorkspaceSamplingDiagnostics* diagnostics = nullptr);

//! @brief 一次性算 samples 的状态分布 + 关键指标(manip / cond / margin)。
//! 非有限数会被剔除,所以 avg / min / max 不会因为 inf 失真。
WorkspaceSummary summarizeWorkspaceSamples (
    const std::vector< WorkspaceSample >& samples);

}    // namespace rws

#endif    // RWS_KINEMATICANALYSIS_KINEMATICANALYSISWORKSPACE_HPP