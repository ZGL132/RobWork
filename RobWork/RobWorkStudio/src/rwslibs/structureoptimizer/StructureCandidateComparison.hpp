#ifndef RWS_STRUCTUREOPTIMIZATION_STRUCTURECANDIDATECOMPARISON_HPP
#define RWS_STRUCTUREOPTIMIZATION_STRUCTURECANDIDATECOMPARISON_HPP

#include "StructureOptimizationTypes.hpp"

#include <string>
#include <vector>

namespace rws {

struct StructureCandidateComparisonRow
{
    int candidateIndex = -1;
    bool feasible = false;
    double score = 0.0;
    double scoreDelta = 0.0;
    double reachabilityDelta = 0.0;
    double manipulabilityDelta = 0.0;
    double jointMarginDelta = 0.0;
    double collisionDelta = 0.0;
    double lengthDelta = 0.0;
    std::vector<std::string> violatedConstraints;
};

struct StructureCandidateComparison
{
    bool valid = false;
    std::string error;
    int baselineCandidateIndex = -1;
    std::vector<StructureCandidateComparisonRow> rows;

    static StructureCandidateComparison compare(
        const StructureOptimizationResult& result,
        const std::vector<int>& selectedCandidateIndices);
};

} // namespace rws

#endif // RWS_STRUCTUREOPTIMIZATION_STRUCTURECANDIDATECOMPARISON_HPP
