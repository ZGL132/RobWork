#include "StructureOptimizationProjectFactory.hpp"

#include "StructureOptimizationUiLogic.hpp"

namespace rws {

bool StructureOptimizationProjectFactory::create(const RobotModelSpec& spec,
                                                 StructureOptimizationProblem& problem,
                                                 std::string* error)
{
    if (spec.robotName.empty() || spec.transformJoints.empty()) {
        if (error != nullptr) {
            *error = "StructureOptimization.Context.Invalid: complete RobotModelSpec is required.";
        }
        return false;
    }

    StructureOptimizationProblem created;
    created.context.modelSpec = spec;
    created.context.projectName = spec.robotName;
    created.context.robotName = spec.robotName;
    created.context.deviceName = spec.robotName;
    created.variables = StructureOptimizationUiLogic::suggestVariables(created.context);

    problem = std::move(created);
    if (error != nullptr)
        error->clear();
    return true;
}

} // namespace rws
