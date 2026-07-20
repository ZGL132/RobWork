#ifndef RWS_KINEMATICANALYSIS_KINEMATICANALYSISWORKSPACE_HPP
#define RWS_KINEMATICANALYSIS_KINEMATICANALYSISWORKSPACE_HPP

#include "KinematicAnalysisTypes.hpp"

#include <cstddef>
#include <vector>

namespace rws {

// =============================================================================
//  工作空间采样辅助模块(独立于 KinematicAnalyzer,纯函数,无 Qt 依赖)
// =============================================================================
//
// 设计目标:把"sanitize / planned count / summary"等纯计算从 KinematicAnalyzer 抽出:
//   - UI 可以独立测试 sanitize 边界;
//   - Unit 测试可以不带 device 验证 sanitize/summary 行为;
//   - 避免分析器承担"硬上限 clamp"等与算法无关的策略。
// 没有任何 Qt Widget 依赖,可被测试和 Plugin 直接调用。

// 硬上限:防止 UI / 脚本给 Workspace 一个离谱配置炸内存。
//   - MaxWorkspaceSampleCount      = 1,000,000:随机模式下 sampleCount 上限
//                                    超过即截断;
//   - MaxWorkspaceGridStepsPerJoint = 100:网格模式 steps^dof 上限
//                                       超过即截断(组合数爆炸防护)。
static const int MaxWorkspaceSampleCount     = 1000000;
static const int MaxWorkspaceGridStepsPerJoint = 100;

//! @brief 把用户的 WorkspaceSamplingConfig 清洗为合法值,记下被修正的字段。
//!
//! 规则:
//!   - sampleCount < 0  → 0(sampleCountClamped = true);
//!   - sampleCount > MaxWorkspaceSampleCount → MaxWorkspaceSampleCount
//!     (sampleCountClamped = true);
//!   - gridStepsPerJoint < 1 → 1(gridStepsClamped = true);
//!   - gridStepsPerJoint > MaxWorkspaceGridStepsPerJoint → MaxWorkspaceGridStepsPerJoint
//!     (gridStepsClamped = true);
//!   - randomSeed == 0 → 1(randomSeedAdjusted = true,
//!     避免 mt19937(0) 行为退化)。
//!
//! @param config      输入配置(可能含非法值)
//! @param diagnostics 可选;nullptr 时不写诊断但仍返回 sanitized config
//! @return sanitized config,所有字段均在合法范围内
WorkspaceSamplingConfig sanitizeWorkspaceSamplingConfig (
    const WorkspaceSamplingConfig& config,
    WorkspaceSamplingDiagnostics* diagnostics = nullptr);

//! @brief 给定 dof 算"Grid 模式理论总组合 + sampleCount 上限"后的实际 plan。
//!
//! @param config      sanitized config(此函数不再做二次 sanitize,但若不
//!                     sanitize 直接调用,行为由 multiplyCapped 的 overflowed 标志负责)
//! @param dof         设备 DOF(关节数)
//! @param diagnostics 可选;nullptr 时不写
//! @return 实际计划样本数
//!
//! RandomUniform 模式直接返回 sanitized.sampleCount;
//! Grid 模式按 dof × steps 算理论值,与 sampleCount 取小;
//! 理论值 > plan 时把 gridCountTruncated 标 true。
//! 真实实现内部用 multiplyCapped 做"乘法 + cap"以避免溢出。
std::size_t plannedWorkspaceSampleCount (
    const WorkspaceSamplingConfig& config,
    std::size_t dof,
    WorkspaceSamplingDiagnostics* diagnostics = nullptr);

//! @brief 一次性算 samples 的状态分布 + 关键指标(manip / cond / margin)。
//!
//! 非有限数(+inf / -inf / NaN)会被剔除,所以 avg / min / max 不会因为 inf 失真。
//! hasManipulability / hasCondition / hasJointLimitMargin 标记哪些字段有有效数据。
//! 用于 UI summary 与 Report 共享同一份统计,避免各处重写循环。
WorkspaceSummary summarizeWorkspaceSamples (
    const std::vector< WorkspaceSample >& samples);

}    // namespace rws

#endif    // RWS_KINEMATICANALYSIS_KINEMATICANALYSISWORKSPACE_HPP
