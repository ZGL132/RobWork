#ifndef RWS_STRUCTUREOPTIMIZATION_STRUCTUREOBJECTIVESCORER_HPP
#define RWS_STRUCTUREOPTIMIZATION_STRUCTUREOBJECTIVESCORER_HPP

#include "StructureOptimizationTypes.hpp"

namespace rws {

class StructureObjectiveScorer {
  public:
    void score(const StructureOptimizationProblem& problem,
               StructureCandidateResult& candidate) const;
    static double percentile10(std::vector<double> values);
    static void sortForDecision(std::vector<StructureCandidateResult>& candidates);
};

} // namespace rws
#endif
