#include "EliteSelector.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace rws {
namespace {

bool isUncertain(const EliteSelectionCandidate& candidate)
{
    return candidate.screeningDecision == QuickScreeningDecision::Uncertain ||
           candidate.result.feasibility == Feasibility::DataInsufficient ||
           candidate.result.feasibility == Feasibility::NotEvaluated;
}

int evidenceRank(const CandidateResult& result)
{
    if (result.feasibility == Feasibility::Feasible)
        return result.evidenceStage == AnalysisEvidenceStage::Verified ? 4 : 3;
    if (result.feasibility == Feasibility::DataInsufficient)
        return 2;
    if (result.feasibility == Feasibility::Infeasible)
        return 1;
    return 0;
}

std::pair<int, double> violationRank(const CandidateResult& result)
{
    int highestPriority = std::numeric_limits<int>::min();
    double total = 0.0;
    bool found = false;
    for (const ConstraintResult& constraint : result.constraints) {
        if (!constraint.hard || constraint.satisfied)
            continue;
        found = true;
        // Constraint aggregation defines larger priority values as the more
        // important hard constraints.  Preserve that ordering when a
        // candidate violates more than one constraint; using the minimum
        // would allow a low-priority sibling to hide a critical violation.
        highestPriority = std::max(highestPriority, constraint.priority);
        if (std::isfinite(constraint.normalizedViolation))
            total += std::max(0.0, constraint.normalizedViolation);
    }
    if (!found)
        return {std::numeric_limits<int>::min(), 0.0};
    return {highestPriority, total};
}

bool baseBetter(const EliteSelectionCandidate& left, const EliteSelectionCandidate& right)
{
    const int leftEvidence = evidenceRank(left.result);
    const int rightEvidence = evidenceRank(right.result);
    if (leftEvidence != rightEvidence)
        return leftEvidence > rightEvidence;

    if (leftEvidence <= 1) {
        const auto leftViolation = violationRank(left.result);
        const auto rightViolation = violationRank(right.result);
        if (leftViolation.first != rightViolation.first)
            return leftViolation.first < rightViolation.first;
        if (leftViolation.second != rightViolation.second)
            return leftViolation.second < rightViolation.second;
    }

    const double leftScore = EliteSelector::score(left.result);
    const double rightScore = EliteSelector::score(right.result);
    if (leftScore != rightScore)
        return leftScore > rightScore;
    return left.stableIndex < right.stableIndex;
}

} // namespace

double EliteSelector::score(const CandidateResult& result)
{
    double total = 0.0;
    for (const ObjectiveResult& objective : result.objectives) {
        if (objective.usable && std::isfinite(objective.contribution))
            total += objective.contribution;
    }
    return total;
}

double EliteSelector::diversityDistance(const EliteSelectionCandidate& left,
                                        const EliteSelectionCandidate& right)
{
    const std::size_t count = std::min(left.normalizedDesign.size(),
                                       right.normalizedDesign.size());
    double squared = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        if (!std::isfinite(left.normalizedDesign[i]) ||
            !std::isfinite(right.normalizedDesign[i]))
            continue;
        const double delta = left.normalizedDesign[i] - right.normalizedDesign[i];
        squared += delta * delta;
    }
    return std::sqrt(squared);
}

EliteSelectionResult EliteSelector::select(
    const std::vector<EliteSelectionCandidate>& candidates,
    const EliteSelectorConfig& config)
{
    EliteSelectionResult result;
    if (config.eliteCount == 0 || candidates.empty())
        return result;

    std::vector<const EliteSelectionCandidate*> remaining;
    remaining.reserve(candidates.size());
    for (const EliteSelectionCandidate& candidate : candidates)
        remaining.push_back(&candidate);

    // The first candidate is the strongest deterministic result.  Subsequent
    // candidates retain feasibility/evidence ordering while using distance as
    // a bounded tie-breaker inside the same evidence tier.
    std::stable_sort(remaining.begin(), remaining.end(),
                     [](const auto* left, const auto* right) {
                         return baseBetter(*left, *right);
                     });

    std::vector<const EliteSelectionCandidate*> selected;
    selected.reserve(std::min(config.eliteCount, remaining.size()));
    std::size_t uncertainSelected = 0;
    while (!remaining.empty() && selected.size() < config.eliteCount) {
        const EliteSelectionCandidate* best = nullptr;
        double bestDiversity = -1.0;
        for (const EliteSelectionCandidate* candidate : remaining) {
            if (isUncertain(*candidate) && uncertainSelected >= config.uncertainQuota)
                continue;

            double minDistance = std::numeric_limits<double>::max();
            if (selected.empty())
                minDistance = 0.0;
            else {
                for (const EliteSelectionCandidate* prior : selected)
                    minDistance = std::min(minDistance,
                                           diversityDistance(*candidate, *prior));
            }

            bool choose = false;
            if (best == nullptr)
                choose = true;
            else {
                const int candidateTier = evidenceRank(candidate->result);
                const int bestTier = evidenceRank(best->result);
                if (candidateTier != bestTier)
                    choose = candidateTier > bestTier;
                else if (candidateTier <= 1) {
                    choose = baseBetter(*candidate, *best);
                }
                else {
                    const double candidateUtility =
                        EliteSelector::score(candidate->result) +
                        config.diversityWeight * minDistance;
                    const double bestUtility =
                        EliteSelector::score(best->result) +
                        config.diversityWeight * bestDiversity;
                    if (candidateUtility != bestUtility)
                        choose = candidateUtility > bestUtility;
                    else
                        choose = candidate->stableIndex < best->stableIndex;
                }
            }
            if (choose) {
                best = candidate;
                bestDiversity = minDistance;
            }
        }

        if (best == nullptr)
            break;
        selected.push_back(best);
        if (isUncertain(*best))
            ++uncertainSelected;
        remaining.erase(std::find(remaining.begin(), remaining.end(), best));
    }

    for (const EliteSelectionCandidate* candidate : selected)
        result.indices.push_back(candidate->stableIndex);
    return result;
}

} // namespace rws
