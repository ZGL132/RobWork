#include "StructureOptimizationObjectiveProfile.hpp"

namespace rws {

std::vector<ObjectiveTerm> StructureOptimizationObjectiveProfile::legacyObjectives(
    const StructureOptimizationWeights& weights)
{
    return {
        {"kinematics.reachability.weighted", OptimizationDirection::Maximize, {1.0, 0.0, true}, weights.reachability, true},
        {"kinematics.manipulability.p10", OptimizationDirection::Maximize, {0.05, 0.0, true}, weights.manipulability, true},
        {"kinematics.joint_margin.p10", OptimizationDirection::Maximize, {0.20, 0.0, true}, weights.jointMargin, true},
        {"collision.free_rate", OptimizationDirection::Maximize, {1.0, 0.0, true}, weights.collision, true},
        {"geometry.compactness", OptimizationDirection::Maximize, {1.0, 0.0, true}, weights.compactness, true},
        {"structure.preference", OptimizationDirection::Maximize, {1.0, 0.0, true}, weights.preference, true}};
}

const std::vector<ObjectiveTerm>& StructureOptimizationObjectiveProfile::effectiveObjectives(
    const StructureOptimizationProblem& problem)
{
    if (!problem.objectives.empty())
        return problem.objectives;

    static thread_local std::vector<ObjectiveTerm> legacy;
    legacy = legacyObjectives(problem.weights);
    return legacy;
}

bool StructureOptimizationObjectiveProfile::isLegacyProfile(
    const std::vector<ObjectiveTerm>& objectives)
{
    static const char* const metricIds[] = {
        "kinematics.reachability.weighted",
        "kinematics.manipulability.p10",
        "kinematics.joint_margin.p10",
        "collision.free_rate",
        "geometry.compactness",
        "structure.preference"};
    if (objectives.size() != sizeof(metricIds) / sizeof(metricIds[0]))
        return false;
    for (std::size_t index = 0; index < objectives.size(); ++index) {
        if (objectives[index].metricId != metricIds[index])
            return false;
    }
    return true;
}

} // namespace rws
