#include "KinematicAnalysisWorkspace.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

using namespace rws;

namespace {

// 把有限数收集进 vector;inf / NaN 跳过,防止后续 max/avg/分位出错。
void addFinite (std::vector< double >& values, double value)
{
    if (std::isfinite (value))
        values.push_back (value);
}

double averageOf (const std::vector< double >& values)
{
    if (values.empty ())
        return 0.0;
    double sum = 0.0;
    for (double value : values)
        sum += value;
    return sum / static_cast< double > (values.size ());
}

// ratio ∈ [0, 1] 的分位;ratio=0.10 即 P10。
double lowerPercentile (std::vector< double > values, double ratio)
{
    if (values.empty ())
        return 0.0;
    std::sort (values.begin (), values.end ());
    const std::size_t index = static_cast< std::size_t > (
        std::floor (ratio * static_cast< double > (values.size () - 1)));
    return values[index];
}

// 乘法 + 上限:中途一旦超出 cap 就直接返回 cap,避免溢出。
// *capped 在返回值为 cap 且实际乘积 > cap 时设 true,用于区分"刚好等于上限"
// 和"被截断到上限",避免 theoreticalGridSamples 的 gridCountTruncated 误判。
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

WorkspaceSamplingConfig rws::sanitizeWorkspaceSamplingConfig (
    const WorkspaceSamplingConfig& config,
    WorkspaceSamplingDiagnostics* diagnostics)
{
    if (diagnostics != nullptr)
        *diagnostics = WorkspaceSamplingDiagnostics ();

    WorkspaceSamplingConfig sanitized = config;
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
    if (sanitized.randomSeed == 0) {
        sanitized.randomSeed = 1;
        if (diagnostics != nullptr)
            diagnostics->randomSeedAdjusted = true;
    }
    if (diagnostics != nullptr)
        diagnostics->requestedSamples =
            static_cast< std::size_t > (std::max (0, config.sampleCount));
    return sanitized;
}

std::size_t rws::plannedWorkspaceSampleCount (
    const WorkspaceSamplingConfig& config,
    std::size_t dof,
    WorkspaceSamplingDiagnostics* diagnostics)
{
    WorkspaceSamplingDiagnostics local;
    WorkspaceSamplingConfig sanitized = sanitizeWorkspaceSamplingConfig (config, &local);
    if (dof == 0 || sanitized.sampleCount <= 0) {
        if (diagnostics != nullptr)
            *diagnostics = local;
        return 0;
    }

    std::size_t planned = static_cast< std::size_t > (sanitized.sampleCount);
    if (sanitized.mode == WorkspaceSamplingMode::Grid) {
        const std::size_t cap = static_cast< std::size_t > (MaxWorkspaceSampleCount);
        std::size_t total = 1;
        bool anyCapped = false;
        for (std::size_t i = 0; i < dof; ++i) {
            bool stepCapped = false;
            total = multiplyCapped (
                total, static_cast< std::size_t > (sanitized.gridStepsPerJoint),
                cap, &stepCapped);
            anyCapped = anyCapped || stepCapped;
        }
        local.theoreticalGridSamples = total;
        planned = std::min (planned, total);
        local.gridCountTruncated = anyCapped || total > planned;
    }

    local.plannedSamples = planned;
    if (diagnostics != nullptr)
        *diagnostics = local;
    return planned;
}

WorkspaceSummary rws::summarizeWorkspaceSamples (
    const std::vector< WorkspaceSample >& samples)
{
    WorkspaceSummary summary;
    summary.totalCount = samples.size ();

    std::vector< double > manipulabilityValues;
    std::vector< double > conditionValues;
    std::vector< double > marginValues;
    manipulabilityValues.reserve (samples.size ());
    conditionValues.reserve (samples.size ());
    marginValues.reserve (samples.size ());

    for (const WorkspaceSample& sample : samples) {
        switch (sample.status) {
            case AnalysisStatus::Pass:
                ++summary.passCount;
                break;
            case AnalysisStatus::Warning:
                ++summary.warningCount;
                break;
            case AnalysisStatus::Fail:
                ++summary.failCount;
                break;
            case AnalysisStatus::Unknown:
            default:
                ++summary.unknownCount;
                break;
        }
        if (sample.inCollision)
            ++summary.collisionCount;
        else
            ++summary.collisionFreeCount;

        addFinite (manipulabilityValues, sample.manipulability);
        addFinite (conditionValues,     sample.conditionNumber);
        addFinite (marginValues,         sample.minJointLimitMargin);
    }

    if (!manipulabilityValues.empty ()) {
        summary.hasManipulability  = true;
        summary.minManipulability   = *std::min_element (
            manipulabilityValues.begin (), manipulabilityValues.end ());
        summary.maxManipulability   = *std::max_element (
            manipulabilityValues.begin (), manipulabilityValues.end ());
        summary.avgManipulability   = averageOf (manipulabilityValues);
        summary.p10Manipulability   = lowerPercentile (manipulabilityValues, 0.10);
    }
    if (!conditionValues.empty ()) {
        summary.hasCondition       = true;
        summary.minCondition       = *std::min_element (
            conditionValues.begin (), conditionValues.end ());
        summary.maxCondition       = *std::max_element (
            conditionValues.begin (), conditionValues.end ());
        summary.avgCondition       = averageOf (conditionValues);
    }
    if (!marginValues.empty ()) {
        summary.hasJointLimitMargin = true;
        summary.minJointLimitMargin = *std::min_element (
            marginValues.begin (), marginValues.end ());
    }
    return summary;
}