#ifndef RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONOBJECTIVEPROFILE_HPP
#define RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONOBJECTIVEPROFILE_HPP

#include "StructureOptimizationTypes.hpp"

namespace rws {

class StructureOptimizationObjectiveProfile
{
public:
    static std::vector<ObjectiveTerm> legacyObjectives(
        const StructureOptimizationWeights& weights);

    static const std::vector<ObjectiveTerm>& effectiveObjectives(
        const StructureOptimizationProblem& problem);

    static bool isLegacyProfile(const std::vector<ObjectiveTerm>& objectives);
};

} // namespace rws

#endif // RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONOBJECTIVEPROFILE_HPP
