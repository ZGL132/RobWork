#ifndef RWS_STRUCTUREOPTIMIZATION_STRUCTUREDESIGNMUTATOR_HPP
#define RWS_STRUCTUREOPTIMIZATION_STRUCTUREDESIGNMUTATOR_HPP

#include "StructureOptimizationTypes.hpp"
#include <rwslibs/robotmodelbuilder/RobotModelSpec.hpp>

namespace rws {

struct StructureMutationResult {
    bool ok = false;
    RobotModelSpec spec;
    std::vector<AnalysisWarning> warnings;
};

class StructureDesignMutator {
  public:
    static StructureMutationResult apply(
        const RobotModelSpec& baseline,
        const std::vector<StructureDesignVariable>& variables,
        const std::vector<double>& values);
};

} // namespace rws
#endif
