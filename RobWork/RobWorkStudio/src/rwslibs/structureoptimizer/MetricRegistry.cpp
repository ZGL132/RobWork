#include "MetricRegistry.hpp"

namespace rws {

bool MetricRegistry::registerMetric(const MetricDefinition& definition, std::string* error)
{
    if (definition.id.empty() || definition.producer.empty()) {
        if (error) *error = "Metric id and producer are required.";
        return false;
    }
    if (_definitions.count(definition.id) != 0) {
        if (error) *error = "Metric id is already registered: " + definition.id;
        return false;
    }
    _definitions.emplace(definition.id, definition);
    return true;
}

const MetricDefinition* MetricRegistry::find(const std::string& id) const
{
    const auto it = _definitions.find(id);
    return it == _definitions.end() ? nullptr : &it->second;
}

std::vector<MetricDefinition> MetricRegistry::definitions() const
{
    std::vector<MetricDefinition> result;
    for (const auto& item : _definitions) result.push_back(item.second);
    return result;
}

MetricRegistry MetricRegistry::standard()
{
    MetricRegistry registry;
    const auto add = [&registry](const char* id, const char* unit, const char* producer,
                                 bool higher, bool objective, bool hard, bool soft) {
        registry.registerMetric({id, unit, producer, higher, objective, hard, soft});
    };
    add("task.reachability", "ratio", "TargetEvaluator", true, true, true, true);
    add("task.position_residual", "m", "TargetEvaluator", false, true, true, true);
    add("workspace.estimated_coverage", "ratio", "EstimatedWorkspace", true, true, false, true);
    add("region.position_coverage", "ratio", "VerifiedRegion", true, true, true, true);
    add("region.orientation_coverage", "ratio", "OrientationCoverage", true, true, true, true);
    add("joint.margin_p10", "ratio", "KinematicAggregator", true, true, true, true);
    add("jacobian.sigma_min", "", "KinematicAggregator", true, true, false, true);
    add("jacobian.condition_number", "", "KinematicAggregator", false, true, false, true);
    add("jacobian.manipulability_normalized", "", "KinematicAggregator", true, true, false, true);
    add("collision.task_free_rate", "ratio", "CollisionEvaluator", true, true, true, true);
    return registry;
}

const char* toString(MetricAvailability availability)
{
    switch (availability) {
    case MetricAvailability::Available: return "Available";
    case MetricAvailability::Unavailable: return "Unavailable";
    case MetricAvailability::InsufficientData: return "InsufficientData";
    case MetricAvailability::Partial: return "Partial";
    }
    return "Unavailable";
}

} // namespace rws
