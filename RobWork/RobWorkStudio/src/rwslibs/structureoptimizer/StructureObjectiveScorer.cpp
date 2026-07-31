#include "StructureObjectiveScorer.hpp"

#include "StructureOptimizationObjectiveProfile.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace rws {

namespace {

// ---------------------------------------------------------------------------
//  Helper: clamp double to [lo, hi]
// ---------------------------------------------------------------------------
double clampVal(double v, double lo, double hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// ---------------------------------------------------------------------------
//  Score a "high-value-is-better" metric using linear interpolation.
//  score = clamp((value - bad) / (good - bad), 0, 1)
// ---------------------------------------------------------------------------
double scoreHighValueIsBetter(double value, double bad, double good)
{
    if (good <= bad) return value >= good ? 1.0 : 0.0;
    return clampVal((value - bad) / (good - bad), 0.0, 1.0);
}

// ---------------------------------------------------------------------------
//  Score a "low-value-is-better" metric using linear interpolation.
//  score = 1 - clamp((value - good) / (bad - good), 0, 1)
// ---------------------------------------------------------------------------
double scoreLowValueIsBetter(double value, double good, double bad)
{
    if (bad <= good) return value <= good ? 1.0 : 0.0;
    return 1.0 - clampVal((value - good) / (bad - good), 0.0, 1.0);
}

bool metricValue(const StructureRawMetrics& raw, const std::string& metricId,
                 double& value)
{
    if (metricId == "kinematics.reachability.weighted")
        value = raw.weightedReachability;
    else if (metricId == "kinematics.manipulability.p10")
        value = raw.manipulabilityP10;
    else if (metricId == "kinematics.joint_margin.p10")
        value = raw.jointMarginP10;
    else if (metricId == "kinematics.joint_margin.minimum")
        value = raw.minimumJointMargin;
    else if (metricId == "kinematics.workspace.coverage")
        value = raw.workspaceCoverage;
    else if (metricId == "collision.free_rate")
        value = raw.collisionFreeRate;
    else if (metricId == "geometry.compactness")
        value = scoreLowValueIsBetter(raw.totalKinematicLength, 0.8, 2.5);
    else if (metricId == "structure.preference")
        value = raw.engineeringPreference;
    else
        return false;
    return true;
}

double normalizeMetric(double value, const ObjectiveTerm& objective)
{
    const double good = objective.normalization.good;
    const double bad = objective.normalization.bad;
    double score = objective.direction == OptimizationDirection::Maximize
        ? (good <= bad ? (value >= good ? 1.0 : 0.0) : (value - bad) / (good - bad))
        : (bad <= good ? (value <= good ? 1.0 : 0.0) : 1.0 - (value - good) / (bad - good));
    return objective.normalization.clamp ? clampVal(score, 0.0, 1.0) : score;
}

bool satisfies(double value, const ConstraintRule& constraint)
{
    switch (constraint.comparison) {
    case ComparisonOperator::LessThanOrEqual:
        return value <= constraint.threshold;
    case ComparisonOperator::GreaterThanOrEqual:
        return value >= constraint.threshold;
    case ComparisonOperator::Equal:
        return value == constraint.threshold;
    }
    return false;
}

} // anonymous namespace

// ===========================================================================
//  score()
// ===========================================================================
void StructureObjectiveScorer::score(
    const StructureOptimizationProblem& problem,
    StructureCandidateResult& candidate) const
{
    const auto& raw     = candidate.raw;
    auto& scores        = candidate.scores;

    // Reset
    scores = StructureComponentScores{};
    candidate.violatedConstraints.clear();

    // =====================================================================
    //  Component scores
    // =====================================================================

    // Reachability: high-value-is-better, weightedReachability already in [0,1]
    scores.reachability = clampVal(raw.weightedReachability, 0.0, 1.0);

    // Manipulability: high-value-is-better, fixed thresholds
    //   manipulabilityBad = 1e-5, manipulabilityGood = 1e-2
    scores.manipulability = scoreHighValueIsBetter(
        raw.manipulabilityP10, 1e-5, 1e-2);

    // JointMargin: high-value-is-better, fixed thresholds
    //   jointMarginBad = 0.02, jointMarginGood = 0.20
    scores.jointMargin = scoreHighValueIsBetter(
        raw.jointMarginP10, 0.02, 0.20);

    // Collision: high-value-is-better, collisionFreeRate already in [0,1]
    scores.collision = clampVal(raw.collisionFreeRate, 0.0, 1.0);

    // Compactness: low-value-is-better, fixed thresholds
    //   compactLengthGood = 0.8, compactLengthBad = 2.5
    scores.compactness = scoreLowValueIsBetter(
        raw.totalKinematicLength, 0.8, 2.5);

    // Preference: high-value-is-better, engineeringPreference already in [0,1]
    scores.preference = clampVal(raw.engineeringPreference, 0.0, 1.0);

    // =====================================================================
    //  Weighted total score  →  [0, 100]
    // =====================================================================
    double total = 0.0;
    for (const ObjectiveTerm& objective :
         StructureOptimizationObjectiveProfile::effectiveObjectives(problem))
    {
        if (!objective.enabled || objective.weight <= 0.0)
            continue;
        double value = 0.0;
        if (metricValue(raw, objective.metricId, value))
            total += objective.weight * normalizeMetric(value, objective);
    }
    candidate.totalScore = clampVal(total * 100.0, 0.0, 100.0);

    // =====================================================================
    //  Hard-constraint checking
    // =====================================================================
    candidate.feasible = true;

    for (const auto& constraint : problem.constraints)
    {
        if (!constraint.enabled || !constraint.hard)
            continue;

        bool satisfied = true;

        switch (constraint.kind)
        {
        case StructureConstraintKind::ModelValid:
            satisfied = raw.modelValid;
            break;

        case StructureConstraintKind::RequiredTaskReachable:
            satisfied = (raw.requiredReachableCount >= raw.requiredTaskCount);
            break;

        case StructureConstraintKind::RequiredTaskCollisionFree:
            satisfied = (raw.collisionFreeRate >= constraint.threshold);
            break;

        case StructureConstraintKind::MinimumJointMargin:
            satisfied = (raw.minimumJointMargin >= constraint.threshold);
            break;

        case StructureConstraintKind::MaximumTotalLength:
            satisfied = (raw.totalKinematicLength <= constraint.threshold);
            break;

        case StructureConstraintKind::MaximumBaseHeight:
            satisfied = (raw.baseHeight <= constraint.threshold);
            break;

        case StructureConstraintKind::MaximumCrossSection:
            satisfied = (raw.maxCrossSection <= constraint.threshold);
            break;

        case StructureConstraintKind::MaximumLinkSlenderness:
            satisfied = (raw.maxLinkSlenderness <= constraint.threshold);
            break;

        case StructureConstraintKind::MinimumWorkspaceCoverage:
            // 冻结需求会为每个覆盖区域创建带 targetName 的独立约束。只有旧项目未提供
            // 区域 ID 时才回退到兼容汇总值，避免多个 Must 区域被错误共用同一覆盖率。
            if (!constraint.targetName.empty()) {
                const auto metric = std::find_if(raw.workspaceRegionMetrics.begin(),
                                                 raw.workspaceRegionMetrics.end(),
                    [&constraint] (const StructureWorkspaceRegionMetric& value) {
                        return value.id == constraint.targetName;
                    });
                satisfied = metric != raw.workspaceRegionMetrics.end() &&
                            metric->coverage >= constraint.threshold;
            }
            else {
                satisfied = (raw.workspaceCoverage >= constraint.threshold);
            }
            break;
        }

        if (!satisfied)
        {
            candidate.feasible = false;
            candidate.violatedConstraints.push_back(constraint.id);
        }
    }

    for (const ConstraintRule& constraint : problem.metricConstraints)
    {
        if (!constraint.enabled || !constraint.hard)
            continue;

        double value = 0.0;
        if (!metricValue(raw, constraint.metricId, value) ||
            !satisfies(value, constraint))
        {
            candidate.feasible = false;
            candidate.violatedConstraints.push_back(constraint.metricId);
        }
    }

    candidate.status = candidate.feasible
        ? StructureCandidateStatus::Feasible
        : StructureCandidateStatus::Infeasible;
}

// ===========================================================================
//  percentile10()
// ===========================================================================
double StructureObjectiveScorer::percentile10(std::vector<double> values)
{
    if (values.empty())
        return 0.0;

    std::sort(values.begin(), values.end());

    // Position: ceil(0.1 * n) - 1  (1-based index → 0-based)
    std::size_t idx = static_cast<std::size_t>(std::ceil(0.1 * values.size())) - 1;
    if (idx >= values.size())
        idx = values.size() - 1;

    return values[idx];
}

// ===========================================================================
//  sortForDecision()
// ===========================================================================
void StructureObjectiveScorer::sortForDecision(
    std::vector<StructureCandidateResult>& candidates)
{
    std::sort(candidates.begin(), candidates.end(),
        [](const StructureCandidateResult& a, const StructureCandidateResult& b) {

            // 1. Feasibility descending (feasible first)
            if (a.feasible != b.feasible)
                return a.feasible > b.feasible;

            // 2. Required reachability descending
            if (a.raw.requiredReachableCount != b.raw.requiredReachableCount)
                return a.raw.requiredReachableCount > b.raw.requiredReachableCount;

            // 3. Collision-free rate descending
            if (a.raw.collisionFreeRate != b.raw.collisionFreeRate)
                return a.raw.collisionFreeRate > b.raw.collisionFreeRate;

            // 4. Total score descending
            if (a.totalScore != b.totalScore)
                return a.totalScore > b.totalScore;

            // 5. Total length ascending (shorter is better)
            if (a.raw.totalKinematicLength != b.raw.totalKinematicLength)
                return a.raw.totalKinematicLength < b.raw.totalKinematicLength;

            // 6. Index ascending (stable tie-break)
            return a.index < b.index;
        });
}

} // namespace rws
