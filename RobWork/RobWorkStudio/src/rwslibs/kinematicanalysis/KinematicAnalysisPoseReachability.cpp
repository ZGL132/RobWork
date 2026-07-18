#include "KinematicAnalysisPoseReachability.hpp"

#include <algorithm>
#include <cmath>

using namespace rws;

namespace {

// 乘法 + 上限:中途一旦超出 cap 就直接返回 cap,避免溢出。
// *capped 在返回值为 cap 且实际乘积 > cap 时设 true,用于区分"刚好等于上限"
// 和"被截断到上限"。
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

// 初始化 / 更新 PoseReachabilitySummary 的 min/max coverage。
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

PoseReachabilityConfig rws::sanitizePoseReachabilityConfig (
    const PoseReachabilityConfig& config,
    PoseReachabilityDiagnostics* diagnostics)
{
    if (diagnostics != nullptr)
        *diagnostics = PoseReachabilityDiagnostics ();

    PoseReachabilityConfig sanitized = config;
    if (diagnostics != nullptr) {
        diagnostics->requestedDirectionSamples =
            static_cast< std::size_t > (std::max (0, config.directionSamples));
        diagnostics->requestedRollSamples =
            static_cast< std::size_t > (std::max (0, config.rollSamples));
    }

    if (sanitized.directionSamples < 0) {
        sanitized.directionSamples = 0;
        if (diagnostics != nullptr) diagnostics->directionSamplesClamped = true;
    }
    if (sanitized.directionSamples > MaxPoseDirectionSamples) {
        sanitized.directionSamples = MaxPoseDirectionSamples;
        if (diagnostics != nullptr) diagnostics->directionSamplesClamped = true;
    }
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
    local.targetCountCapped = perPositionCapped || totalCapped;
    if (diagnostics != nullptr)
        *diagnostics = local;
    return total;
}

PoseReachabilitySummary rws::summarizePoseReachabilitySamples (
    const std::vector< PoseReachabilitySample >& samples)
{
    PoseReachabilitySummary summary;
    double coverageSum = 0.0;
    for (const PoseReachabilitySample& sample : samples) {
        ++summary.totalPositions;
        switch (sample.status) {
            case AnalysisStatus::Pass: ++summary.passCount; break;
            case AnalysisStatus::Warning: ++summary.warningCount; break;
            case AnalysisStatus::Fail: ++summary.failCount; break;
            case AnalysisStatus::Unknown:
            default: ++summary.unknownCount; break;
        }
        summary.sampledDirections +=
            static_cast< std::size_t > (std::max (0, sample.sampledDirections));
        summary.reachableDirections +=
            static_cast< std::size_t > (std::max (0, sample.reachableDirections));
        coverageSum += sample.coverage;
        includeCoverage (summary, sample.coverage);
        if (sample.partial)
            ++summary.partialCount;
        summary.plannedIkTargets += sample.plannedIkTargets;
        summary.completedIkTargets += sample.completedIkTargets;
    }
    if (summary.totalPositions != 0)
        summary.averageCoverage = coverageSum /
            static_cast< double > (summary.totalPositions);
    return summary;
}

bool rws::isPoseDirectionReachable (
    const std::vector< KinematicIkSolution >& solutions)
{
    for (const KinematicIkSolution& solution : solutions) {
        if (solution.inCollision)
            continue;
        if (solution.status == AnalysisStatus::Pass ||
            solution.status == AnalysisStatus::Warning)
            return true;
    }
    return false;
}
