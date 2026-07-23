#include "StructureObjectiveScorer.hpp"

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

} // anonymous namespace

// ===========================================================================
//  score()
// ===========================================================================
void StructureObjectiveScorer::score(
    const StructureOptimizationProblem& problem,
    StructureCandidateResult& candidate) const
{
    const auto& raw     = candidate.raw;
    const auto& weights = problem.weights;
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
    total += weights.reachability   * scores.reachability;
    total += weights.manipulability * scores.manipulability;
    total += weights.jointMargin    * scores.jointMargin;
    total += weights.collision      * scores.collision;
    total += weights.compactness    * scores.compactness;
    total += weights.preference     * scores.preference;
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
            satisfied = (raw.workspaceCoverage >= constraint.threshold);
            break;
        }

        if (!satisfied)
        {
            candidate.feasible = false;
            candidate.violatedConstraints.push_back(constraint.id);
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
