#include "KinematicAnalysisPoseReachability.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

using namespace rws;

namespace {

// =============================================================================
//  匿名命名空间 helper(只在本编译单元内可见,避免污染 rws:: 命名空间)
// =============================================================================

// multiplyCapped:有上限的乘法,中途溢出保护。
//
//   算法: 比较 lhs > cap / rhs 等价于 lhs × rhs > cap(避免先乘后比较溢出);
//   0 × anything → 0 + capped=false;
//   真实乘积 ≤ cap → 正常乘 + capped=false;
//   真实乘积 > cap → 返回 cap + capped=true。
//
//   capped 输出参数区分"恰好等于 cap"和"超出 cap 被截断":
//     - capped=false 表示实际乘积 ≤ cap;
//     - capped=true  表示真实乘积 > cap(被截断为 cap)。
std::size_t multiplyCapped (std::size_t lhs, std::size_t rhs, std::size_t cap,
                            bool* capped = nullptr)
{
    if (lhs == 0 || rhs == 0) {
        if (capped != nullptr) *capped = false;
        return 0;
    }
    if (lhs > cap / rhs) {
        if (capped != nullptr) *capped = true;
        return cap;
    }
    if (capped != nullptr) *capped = false;
    return lhs * rhs;
}

// includeCoverage:用第一个 sample 的 coverage 初始化 min/max,
// 之后每个 sample 用 std::min/max 更新累计值。
// 实现用"if first then init else update"避免大量 if (initialized) 分支。
void includeCoverage (PoseReachabilitySummary& summary, double coverage)
{
    if (summary.totalPositions == 0) {
        summary.minCoverage = coverage;
        summary.maxCoverage = coverage;
    }
    else {
        summary.minCoverage = std::min (summary.minCoverage, coverage);
        summary.maxCoverage = std::max (summary.maxCoverage, coverage);
    }
}

}    // namespace

// =============================================================================
//  sanitizePoseReachabilityConfig:清洗用户配置
// =============================================================================
//
// 注意几个边界:
//   - directionSamples 可以 = 0(界面允许"只采方向不采滚动测试"等极端用例);
//     此时 sanitize 后依旧是 0,所有方向都被跳过;
//   - rollSamples 必须 ≥ 1:0 等于"无滚动",但插件约定至少有 1 次滚动;
//   - 原始请求值无论正负都被记录到 diagnostics->requested* 字段,
//     让 UI diagnostics label 显示"用户原本输入"而不是"修正后"的值。
//
// clamps 触发后写 diagnostics->*Clamped = true,UI 据此显示 "(capped)" 后缀。
PoseReachabilityConfig rws::sanitizePoseReachabilityConfig (
    const PoseReachabilityConfig& config,
    PoseReachabilityDiagnostics* diagnostics)
{
    if (diagnostics != nullptr)
        *diagnostics = PoseReachabilityDiagnostics ();

    PoseReachabilityConfig sanitized = config;

    // 记录原始请求值(夹到 ≥ 0,方便 UI 显示)
    if (diagnostics != nullptr) {
        diagnostics->requestedDirectionSamples =
            static_cast< std::size_t > (std::max (0, config.directionSamples));
        diagnostics->requestedRollSamples =
            static_cast< std::size_t > (std::max (0, config.rollSamples));
    }

    // 1. directionSamples 范围 [0, MaxPoseDirectionSamples]
    if (sanitized.directionSamples < 0) {
        sanitized.directionSamples = 0;
        if (diagnostics != nullptr) diagnostics->directionSamplesClamped = true;
    }
    if (sanitized.directionSamples > MaxPoseDirectionSamples) {
        sanitized.directionSamples = MaxPoseDirectionSamples;
        if (diagnostics != nullptr) diagnostics->directionSamplesClamped = true;
    }
    // 2. rollSamples 范围 [1, MaxPoseRollSamples]
    //    0 不是合法值 — 强制 ≥ 1
    if (sanitized.rollSamples < 1) {
        sanitized.rollSamples = 1;
        if (diagnostics != nullptr) diagnostics->rollSamplesClamped = true;
    }
    if (sanitized.rollSamples > MaxPoseRollSamples) {
        sanitized.rollSamples = MaxPoseRollSamples;
        if (diagnostics != nullptr) diagnostics->rollSamplesClamped = true;
    }
    return sanitized;
}

// =============================================================================
//  plannedPoseReachabilityTargetCount:受上限保护的计划 IK 数(诊断用)
// =============================================================================
//
// 算法:
//   1) sanitize config 拿 cleaned 值 + 是否修正;
//   2) perPosition = directionSamples × rollSamples;
//   3) total = positionCount × perPosition;
//
// 上限: MaxPoseReachabilityTargets (1,000,000)。两个乘法都用 multiplyCapped 避免溢出。
// targetCountCapped = 任一乘法溢出即真(溢出大小 > 上限)。
//
// 注意这是"诊断用"上限,实际 progress 分母应该用 poseReachabilityExecutionTargetCount。
std::size_t rws::plannedPoseReachabilityTargetCount (
    const PoseReachabilityConfig& config,
    std::size_t positionCount,
    PoseReachabilityDiagnostics* diagnostics)
{
    PoseReachabilityDiagnostics local;
    const PoseReachabilityConfig sanitized =
        sanitizePoseReachabilityConfig (config, &local);
    local.positionCount = positionCount;

    const std::size_t directions =
        static_cast< std::size_t > (std::max (0, sanitized.directionSamples));
    // 注意:若 directions = 0,rolls 也必须为 0(否则 IK 目标无穷大)
    const std::size_t rolls = directions == 0 ? 0 :
        static_cast< std::size_t > (std::max (1, sanitized.rollSamples));
    bool perPositionCapped = false;
    const std::size_t perPosition = multiplyCapped (
        directions, rolls, MaxPoseReachabilityTargets, &perPositionCapped);
    bool totalCapped = false;
    const std::size_t total = multiplyCapped (
        positionCount, perPosition, MaxPoseReachabilityTargets, &totalCapped);

    local.plannedDirectionsPerPosition = perPosition;
    local.plannedIkTargets = total;
    // 任一阶段溢出都标记 capped(虽然 totalCap 不必等于 perPositionCap,但简单起见)
    local.targetCountCapped = perPositionCapped || totalCapped;
    if (diagnostics != nullptr)
        *diagnostics = local;
    return total;
}

// =============================================================================
//  poseReachabilityTargetsPerPosition:单个 position 的精确 IK 数
// =============================================================================
// 不受 MaxPoseReachabilityTargets 上限限制(那是 UI 诊断用,而这里是 progress 真分母)。
//   directionSamples ≤ 0 → 0(没有 IK 目标);
//   否则返回 directionSamples × rollSamples。
std::size_t rws::poseReachabilityTargetsPerPosition (
    const PoseReachabilityConfig& config)
{
    const PoseReachabilityConfig sanitized =
        sanitizePoseReachabilityConfig (config, nullptr);
    if (sanitized.directionSamples <= 0)
        return 0;
    return static_cast< std::size_t > (sanitized.directionSamples) *
           static_cast< std::size_t > (sanitized.rollSamples);
}

// =============================================================================
//  poseReachabilityExecutionTargetCount:所有 positions 的执行目标总数(uncapped)
// =============================================================================
// 这是 progress 的"分母"。
// 注意 cap = std::numeric_limits<size_t>::max()(允许真实大值),
// 而 plannedPoseReachabilityTargetCount 用 MaxPoseReachabilityTargets (1e6)。
// 两者分母不同是有意的 — 详见 hpp 注释。
//
// overflowed 输出参数:真实乘积超过 size_t 时设 true(然后函数返回 size_t::max())。
// UI 据此可以选择忽略 UI 显示或显示 "too many"。
std::size_t rws::poseReachabilityExecutionTargetCount (
    const PoseReachabilityConfig& config,
    std::size_t positionCount,
    bool* overflowed)
{
    const std::size_t perPosition =
        poseReachabilityTargetsPerPosition (config);
    bool capped = false;
    const std::size_t total = multiplyCapped (
        positionCount,
        perPosition,
        std::numeric_limits< std::size_t >::max (),
        &capped);
    if (overflowed != nullptr)
        *overflowed = capped;
    return total;
}

// =============================================================================
//  summarizePoseReachabilitySamples:一次性算样本集统计
// =============================================================================
//
// 注意几点:
//   1) sampledDirections / reachableDirections 用 std::max(0, value) 防止负数
//      污染求和(异常容错);
//   2) coverage 可能为 NaN(无法达的方向),includeCoverage 用 std::min/max 比较;
//   3) partialCount / planned/completedIkTargets 用于评估"取消的影响";
//      sampled 与 planned 的差值(planned > completed)是取消导致的未完成 IK 数。
//
// 用途:Report tab 顶部 summary 行 + Visualization 标签 + 用户审核。
PoseReachabilitySummary rws::summarizePoseReachabilitySamples (
    const std::vector< PoseReachabilitySample >& samples)
{
    PoseReachabilitySummary summary;
    double coverageSum = 0.0;
    for (const PoseReachabilitySample& sample : samples) {
        ++summary.totalPositions;
        // 状态分布
        switch (sample.status) {
            case AnalysisStatus::Pass:    ++summary.passCount; break;
            case AnalysisStatus::Warning: ++summary.warningCount; break;
            case AnalysisStatus::Fail:    ++summary.failCount; break;
            case AnalysisStatus::Unknown:
            default:                      ++summary.unknownCount; break;
        }
        // 注意:clamp 到 ≥ 0 防止异常值(防御性编程)
        summary.sampledDirections +=
            static_cast< std::size_t > (std::max (0, sample.sampledDirections));
        summary.reachableDirections +=
            static_cast< std::size_t > (std::max (0, sample.reachableDirections));
        coverageSum += sample.coverage;
        includeCoverage (summary, sample.coverage);   // 用 helper 更新 min/max
        // 取消 / 部分统计
        if (sample.partial)
            ++summary.partialCount;
        // IK 计划 vs 完成(给取消 UI 使用)
        summary.plannedIkTargets   += sample.plannedIkTargets;
        summary.completedIkTargets += sample.completedIkTargets;
    }
    // 平均 coverage;totalPositions == 0 时避免除零
    if (summary.totalPositions != 0)
        summary.averageCoverage = coverageSum /
            static_cast< double > (summary.totalPositions);
    return summary;
}

// =============================================================================
//  isPoseDirectionReachable:判定一个方向的 IK 解集是否可达
// =============================================================================
//
// 判定规则:
//   - 存在至少一个 inCollision == false 的解;
//   - 且其 status == Pass 或 Warning(不能是 Fail);
//   返回 true 表示该方向可达。
//
// 用法:替换 analyzePoseReachability 中原来的内联判定循环,集中到 helper
// 既便单测,也让以后扩展判定条件(例如最低 manipulability 阈值)只需一处。
//
// 注意:inCollision 的解通常意味着状态无效,但有时"轻微碰撞"也可以接受;
// 当前实现保守起见,有碰撞就忽略该解(视方向不可达)。
bool rws::isPoseDirectionReachable (
    const std::vector< KinematicIkSolution >& solutions)
{
    for (const KinematicIkSolution& solution : solutions) {
        // 跳过所有碰撞解
        if (solution.inCollision)
            continue;
        // Pass = 完全可行,Warning = 可达但有退化(奇异 / 接近限位等),都算可达
        if (solution.status == AnalysisStatus::Pass ||
            solution.status == AnalysisStatus::Warning)
            return true;
    }
    return false;
}
