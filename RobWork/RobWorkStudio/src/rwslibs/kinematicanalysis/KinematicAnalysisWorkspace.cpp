#include "KinematicAnalysisWorkspace.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

using namespace rws;

namespace {

// =============================================================================
//  匿名命名空间:纯 helper,不暴露在 rws:: 命名空间
// =============================================================================

// addFinite:把"有限数"加入聚合向量;inf / NaN 跳过。
// 必要性:
//   - WorkspaceSample.manipulability 在奇异位姿下可能为 0,或意外为 NaN;
//   - WorkspaceSample.conditionNumber 在奇异位姿下为 +Inf;
//   若把这些非有限值混入 min/max/avg/分位 计算,会污染所有指标。
// 因此只把 std::isfinite() 通过的"可解释"值放入聚合容器。
void addFinite (std::vector< double >& values, double value)
{
    if (std::isfinite (value))
        values.push_back (value);
}

// averageOf:对一组有限值算平均;空数组返回 0.0(避免除零)。
// 由调用方保证 values 里没有 NaN/Inf(由 addFinite 过滤)。
double averageOf (const std::vector< double >& values)
{
    if (values.empty ())
        return 0.0;
    double sum = 0.0;
    for (double value : values)
        sum += value;
    return sum / static_cast< double > (values.size ());
}

// lowerPercentile:对一组有限值算"低端分位数"。
//   - ratio=0.0  → 最小值;
//   - ratio=1.0  → 最大值;
//   - ratio=0.10 → P10(10 分位数,衡量退化样本尾部)。
// 实现:排序后取 floor(ratio × (n-1)) 索引(归一化到 [0, n-1] 区间)。
// 注意:用"复制-排序"避免影响外部容器的顺序。
double lowerPercentile (std::vector< double > values, double ratio)
{
    if (values.empty ())
        return 0.0;
    std::sort (values.begin (), values.end ());
    const std::size_t index = static_cast< std::size_t > (
        std::floor (ratio * static_cast< double > (values.size () - 1)));
    return values[index];
}

// multiplyCapped:有上限的乘法。
//   - lhs=0 或 rhs=0 → 返回 0,cap=false;
//   - lhs × rhs 超出 cap → 返回 cap 并标记 capped=true;
//   - 否则正常乘法;
// 数值溢出保护:用 lhs > cap / rhs 检测即将溢出(被 0 整除的边界除外)。
// capped 输出参数区分两种"返回 cap"的情况:
//   - 真实乘积恰好 = cap(没溢出);
//   - 真实乘积 > cap(已溢出,被截断)。
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

}    // namespace

// =============================================================================
//  sanitizeWorkspaceSamplingConfig:清洗用户配置
// =============================================================================
//
// 顺序很重要:从最严格(最小/最大范围)到最宽松,确保最终结果在合法范围内。
//
// 规则链(详见 KinematicAnalysisWorkspace.hpp 注释):
//   1) sampleCount < 0            → 0  + sampleCountClamped
//   2) sampleCount > Max          → Max + sampleCountClamped
//   3) gridStepsPerJoint < 1      → 1  + gridStepsClamped
//   4) gridStepsPerJoint > Max    → Max + gridStepsClamped
//   5) randomSeed == 0            → 1  + randomSeedAdjusted
//
// 此外无条件把 config.sampleCount 原值(夹到 ≥ 0)写入 diagnostics->requestedSamples,
// 供 UI 显示"用户原本想要的数量"。
WorkspaceSamplingConfig rws::sanitizeWorkspaceSamplingConfig (
    const WorkspaceSamplingConfig& config,
    WorkspaceSamplingDiagnostics* diagnostics)
{
    // diagnostics 非空时,先重置为默认值,作为 sanitize 的输出载体。
    if (diagnostics != nullptr)
        *diagnostics = WorkspaceSamplingDiagnostics ();

    WorkspaceSamplingConfig sanitized = config;

    // 1. sampleCount 范围
    if (sanitized.sampleCount < 0) {
        sanitized.sampleCount = 0;
        if (diagnostics != nullptr)
            diagnostics->sampleCountClamped = true;
    }
    if (sanitized.sampleCount > MaxWorkspaceSampleCount) {
        sanitized.sampleCount = MaxWorkspaceSampleCount;
        if (diagnostics != nullptr)
            diagnostics->sampleCountClamped = true;
    }

    // 2. gridStepsPerJoint 范围(Max = 100,防止 steps^dof 爆炸)
    if (sanitized.gridStepsPerJoint < 1) {
        sanitized.gridStepsPerJoint = 1;
        if (diagnostics != nullptr)
            diagnostics->gridStepsClamped = true;
    }
    if (sanitized.gridStepsPerJoint > MaxWorkspaceGridStepsPerJoint) {
        sanitized.gridStepsPerJoint = MaxWorkspaceGridStepsPerJoint;
        if (diagnostics != nullptr)
            diagnostics->gridStepsClamped = true;
    }

    // 3. randomSeed == 0 → 1,避免 mt19937(0) 行为退化
    if (sanitized.randomSeed == 0) {
        sanitized.randomSeed = 1;
        if (diagnostics != nullptr)
            diagnostics->randomSeedAdjusted = true;
    }

    // 用户原始输入的 sampleCount(夹到 ≥ 0),供 UI diagnostics label 显示。
    if (diagnostics != nullptr)
        diagnostics->requestedSamples =
            static_cast< std::size_t > (std::max (0, config.sampleCount));
    return sanitized;
}

// =============================================================================
//  plannedWorkspaceSampleCount:根据 dof 算实际计划采样数
// =============================================================================
//
// 流程:
//   1) 内部先调用 sanitizeWorkspaceSamplingConfig 拿到 cleaned config;
//   2) dof == 0 或 sampleCount ≤ 0 → 直接返回 0(GUI 早退);
//   3) RandomUniform 模式: planned = sampleCount(实际计数 = 上限);
//   4) Grid 模式:用 multiplyCapped 累乘 steps,得到"理论总组合数";
//      planned = min(sampleCount, theoretical);
//      gridCountTruncated = 任何一次 multiplyCapped 触顶 OR theoretical > planned。
//
// 这是受 MaxWorkspaceSampleCount (1,000,000) 保护的"诊断用"上限;
// 注意 progress 分母应该用 poseReachabilityExecutionTargetCount 或直接传进来
// planned,避免用 theoretical(超过 1000000 之后会被截断)。
std::size_t rws::plannedWorkspaceSampleCount (
    const WorkspaceSamplingConfig& config,
    std::size_t dof,
    WorkspaceSamplingDiagnostics* diagnostics)
{
    // 先 sanitize 一份独立 diagnostics(local),最后选要写到 caller 还是丢弃。
    WorkspaceSamplingDiagnostics local;
    WorkspaceSamplingConfig sanitized = sanitizeWorkspaceSamplingConfig (config, &local);
    if (dof == 0 || sanitized.sampleCount <= 0) {
        if (diagnostics != nullptr)
            *diagnostics = local;
        return 0;
    }

    std::size_t planned = static_cast< std::size_t > (sanitized.sampleCount);
    if (sanitized.mode == WorkspaceSamplingMode::Grid) {
        // 网格模式:累乘 steps^dof,cap = MaxWorkspaceSampleCount 防止溢出。
        const std::size_t cap = static_cast< std::size_t > (MaxWorkspaceSampleCount);
        std::size_t total = 1;
        bool anyCapped = false;
        for (std::size_t i = 0; i < dof; ++i) {
            bool stepCapped = false;
            total = multiplyCapped (
                total, static_cast< std::size_t > (sanitized.gridStepsPerJoint),
                cap, &stepCapped);
            // 任一维触顶都意味着真实 total 已 > cap
            anyCapped = anyCapped || stepCapped;
        }
        local.theoreticalGridSamples = total;
        // 取实际执行数 = min(用户上限, 理论总数);若有触顶,标记 truncated
        planned = std::min (planned, total);
        local.gridCountTruncated = anyCapped || total > planned;
    }

    local.plannedSamples = planned;
    if (diagnostics != nullptr)
        *diagnostics = local;
    return planned;
}

// =============================================================================
//  summarizeWorkspaceSamples:一次性算样本集的统计摘要
// =============================================================================
//
// 一次性遍历 samples 列表:
//   1) 统计状态分布(Pass/Warning/Fail/Unknown);
//   2) 统计 collision / collision-free 计数;
//   3) 用 addFinite 收集"有限"的 manipulability / condition / margin;
//   4) 算 has* 标志 + min / max / avg(必要时算 P10)。
//
// 注意:
//   - averages / min / max 只对有限值参与的元素算;
//   - condition 通常在奇异位姿下 +Inf,被 addFinite 过滤;
//   - p10 操纵度衡量尾部退化区域(机器人手册常用,代表"仍有 10% 数据劣于")。
WorkspaceSummary rws::summarizeWorkspaceSamples (
    const std::vector< WorkspaceSample >& samples)
{
    WorkspaceSummary summary;
    summary.totalCount = samples.size ();

    // 预 reserve 减少 push_back 扩容次数(典型规模 100~100000)
    std::vector< double > manipulabilityValues;
    std::vector< double > conditionValues;
    std::vector< double > marginValues;
    manipulabilityValues.reserve (samples.size ());
    conditionValues.reserve (samples.size ());
    marginValues.reserve (samples.size ());

    // 单次遍历:状态分布 + 碰撞 + 收集有效数值
    for (const WorkspaceSample& sample : samples) {
        // 状态分布
        switch (sample.status) {
            case AnalysisStatus::Pass:    ++summary.passCount; break;
            case AnalysisStatus::Warning: ++summary.warningCount; break;
            case AnalysisStatus::Fail:    ++summary.failCount; break;
            case AnalysisStatus::Unknown:
            default:                      ++summary.unknownCount; break;
        }
        // 碰撞 vs collision-free
        if (sample.inCollision)
            ++summary.collisionCount;
        else
            ++summary.collisionFreeCount;

        // 收集有限数值(过滤 ±Inf / NaN)
        addFinite (manipulabilityValues, sample.manipulability);
        addFinite (conditionValues,     sample.conditionNumber);
        addFinite (marginValues,         sample.minJointLimitMargin);
    }

    // manipulability 指标(若有数据)
    if (!manipulabilityValues.empty ()) {
        summary.hasManipulability  = true;
        // std::min_element / std::max_element 范围算法
        summary.minManipulability  = *std::min_element (
            manipulabilityValues.begin (), manipulabilityValues.end ());
        summary.maxManipulability  = *std::max_element (
            manipulabilityValues.begin (), manipulabilityValues.end ());
        summary.avgManipulability  = averageOf (manipulabilityValues);
        summary.p10Manipulability  = lowerPercentile (manipulabilityValues, 0.10);
    }
    // condition 指标(只算 min/max/avg,不计算分位)
    if (!conditionValues.empty ()) {
        summary.hasCondition       = true;
        summary.minCondition       = *std::min_element (
            conditionValues.begin (), conditionValues.end ());
        summary.maxCondition       = *std::max_element (
            conditionValues.begin (), conditionValues.end ());
        summary.avgCondition       = averageOf (conditionValues);
    }
    // margin 指标(只算 min,avoid showing huge average that hides the worst joint)
    if (!marginValues.empty ()) {
        summary.hasJointLimitMargin = true;
        summary.minJointLimitMargin = *std::min_element (
            marginValues.begin (), marginValues.end ());
    }
    return summary;
}
