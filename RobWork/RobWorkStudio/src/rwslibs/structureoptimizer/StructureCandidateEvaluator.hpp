#ifndef RWS_STRUCTUREOPTIMIZATION_STRUCTURECANDIDATEEVALUATOR_HPP
#define RWS_STRUCTUREOPTIMIZATION_STRUCTURECANDIDATEEVALUATOR_HPP

#include "StructureOptimizationStrategy.hpp"

namespace rws {

class StructureCandidateEvaluator : public IStructureCandidateEvaluator {
  public:
    void evaluate(
        const StructureOptimizationProblem&,
        StructureCandidateResult&,
        StructureEvaluationStage,
        const StructureOptimizationCallbacks&,
        StructureCandidateCache* cache = nullptr) override;
};

} // namespace rws
#endif
