#ifndef RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONUILOGIC_HPP
#define RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONUILOGIC_HPP

#include "StructureOptimizationTypes.hpp"

namespace rws {

class StructureOptimizationUiLogic
{
public:
    static std::vector<StructureDesignVariable> suggestVariables(
        const RobotDesignContext& context);

    static bool hasRunnableInputs(const StructureOptimizationProblem& problem,
                                  std::string* reason = nullptr);
};

} // namespace rws

#endif // RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONUILOGIC_HPP
