#ifndef RWS_STRUCTUREOPTIMIZATION_HYBRIDSTRUCTUREOPTIMIZER_HPP
#define RWS_STRUCTUREOPTIMIZATION_HYBRIDSTRUCTUREOPTIMIZER_HPP

#include "StructureOptimizationStrategy.hpp"
#include "StructureCandidateCache.hpp"

namespace rws {

class HybridStructureOptimizer : public StructureOptimizationStrategy {
  public:
    StructureOptimizationResult optimize(
        const StructureOptimizationProblem&, IStructureCandidateEvaluator&,
        const StructureOptimizationCallbacks&) override;
};

} // namespace rws
#endif
