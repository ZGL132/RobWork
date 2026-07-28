#ifndef RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONPROJECTFACTORY_HPP
#define RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONPROJECTFACTORY_HPP

#include "StructureOptimizationTypes.hpp"

#include <string>

namespace rws {

//! Creates a structure-optimization project from an explicit model snapshot.
class StructureOptimizationProjectFactory
{
  public:
    static bool create(const RobotModelSpec& spec,
                       StructureOptimizationProblem& problem,
                       std::string* error = nullptr);
};

} // namespace rws

#endif // RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONPROJECTFACTORY_HPP
