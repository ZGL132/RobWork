#include "StructureCandidateComparison.hpp"

#include <algorithm>

namespace rws {

namespace {

const StructureCandidateResult* findCandidate(
    const StructureOptimizationResult& result, int index)
{
    for (const StructureCandidateResult& candidate : result.candidates) {
        if (candidate.index == index)
            return &candidate;
    }
    return nullptr;
}

} // namespace

StructureCandidateComparison StructureCandidateComparison::compare(
    const StructureOptimizationResult& result,
    const std::vector<int>& selectedCandidateIndices)
{
    StructureCandidateComparison comparison;
    comparison.baselineCandidateIndex = result.baselineCandidateIndex;
    const StructureCandidateResult* baseline = findCandidate(
        result, result.baselineCandidateIndex);
    if (baseline == nullptr) {
        comparison.error = "Baseline candidate was not found.";
        return comparison;
    }
    if (selectedCandidateIndices.empty()) {
        comparison.error = "Select at least one candidate to compare.";
        return comparison;
    }
    if (selectedCandidateIndices.size() > 3) {
        comparison.error = "Select no more than three candidates to compare.";
        return comparison;
    }

    for (std::size_t i = 0; i < selectedCandidateIndices.size(); ++i) {
        if (std::find(selectedCandidateIndices.begin(), selectedCandidateIndices.begin() + i,
                      selectedCandidateIndices[i]) != selectedCandidateIndices.begin() + i) {
            comparison.error = "Candidate selections must be unique.";
            return comparison;
        }
        const StructureCandidateResult* candidate = findCandidate(
            result, selectedCandidateIndices[i]);
        if (candidate == nullptr) {
            comparison.error = "Selected candidate was not found.";
            return comparison;
        }
        StructureCandidateComparisonRow row;
        row.candidateIndex = candidate->index;
        row.feasible = candidate->feasible;
        row.score = candidate->totalScore;
        row.scoreDelta = candidate->totalScore - baseline->totalScore;
        row.reachabilityDelta = candidate->scores.reachability - baseline->scores.reachability;
        row.manipulabilityDelta = candidate->scores.manipulability - baseline->scores.manipulability;
        row.jointMarginDelta = candidate->scores.jointMargin - baseline->scores.jointMargin;
        row.collisionDelta = candidate->scores.collision - baseline->scores.collision;
        row.lengthDelta = candidate->raw.totalKinematicLength - baseline->raw.totalKinematicLength;
        row.violatedConstraints = candidate->violatedConstraints;
        comparison.rows.push_back(std::move(row));
    }
    comparison.valid = true;
    return comparison;
}

} // namespace rws
