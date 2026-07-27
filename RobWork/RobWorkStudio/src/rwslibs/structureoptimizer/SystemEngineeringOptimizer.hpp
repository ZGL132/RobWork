#ifndef RWS_STRUCTUREOPTIMIZATION_SYSTEMENGINEERINGOPTIMIZER_HPP
#define RWS_STRUCTUREOPTIMIZATION_SYSTEMENGINEERINGOPTIMIZER_HPP

#include "EngineeringEvaluatorPipeline.hpp"
#include "StructureOptimizationStrategy.hpp"

namespace rws {

class SystemEngineeringOptimizer
{
  public:
    StructureOptimizationResult optimize(const StructureOptimizationProblem& problem,
                                         EngineeringEvaluatorPipeline& pipeline,
                                         const StructureOptimizationCallbacks& callbacks) const;
};

} // namespace rws

#endif
