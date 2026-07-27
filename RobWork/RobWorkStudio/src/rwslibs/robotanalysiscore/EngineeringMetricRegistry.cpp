#include "EngineeringMetricRegistry.hpp"

#include <cmath>
#include <set>

namespace rws {

namespace {

AnalysisWarning warning(const std::string& code, const std::string& message)
{
    AnalysisWarning value;
    value.code = code;
    value.message = message;
    value.source = "EngineeringMetricRegistry";
    value.severity = AnalysisStatus::Fail;
    return value;
}

} // namespace

EngineeringMetricRegistry::EngineeringMetricRegistry()
{
    const EngineeringMetricDefinition definitions[] = {
        {"kinematics.reachability.weighted", "Weighted reachability", "ratio", "Kinematics"},
        {"kinematics.manipulability.p10", "Manipulability P10", "ratio", "Kinematics"},
        {"kinematics.joint_margin.p10", "Joint margin P10", "ratio", "Kinematics"},
        {"kinematics.joint_margin.minimum", "Minimum joint margin", "ratio", "Kinematics"},
        {"kinematics.workspace.coverage", "Workspace coverage", "ratio", "Kinematics"},
        {"geometry.compactness", "Structure compactness", "ratio", "Geometry"},
        {"collision.free_rate", "Collision-free solution rate", "ratio", "Collision"},
        {"collision.minimum_clearance", "Minimum collision clearance", "m", "Collision"},
        {"structure.preference", "Structure engineering preference", "ratio", "Structure"},
        {"trajectory.feasible", "Trajectory feasible", "bool", "Trajectory"},
        {"trajectory.cycle_time", "Trajectory cycle time", "s", "Trajectory"},
        {"trajectory.path_clearance.minimum", "Trajectory minimum clearance", "m", "Trajectory"},
        {"dynamics.torque_peak_ratio", "Peak torque ratio", "ratio", "Dynamics"},
        {"dynamics.velocity_peak_ratio", "Peak velocity ratio", "ratio", "Dynamics"},
        {"dynamics.energy_per_cycle", "Energy per cycle", "J", "Dynamics"},
        {"drive.selection.feasible", "Drive selection feasible", "bool", "Drive selection"},
        {"drive.motor_thermal_margin", "Motor thermal margin", "ratio", "Drive selection"},
        {"drive.reducer_torque_margin", "Reducer torque margin", "ratio", "Drive selection"},
        {"drive.bom_cost", "Drive bill of materials cost", "currency", "Drive selection"}};
    for (const EngineeringMetricDefinition& definition : definitions)
        _definitions[definition.metricId] = definition;
}

const EngineeringMetricRegistry& EngineeringMetricRegistry::standard()
{
    static const EngineeringMetricRegistry registry;
    return registry;
}

const EngineeringMetricDefinition* EngineeringMetricRegistry::find(
    const std::string& metricId) const
{
    const auto found = _definitions.find(metricId);
    return found == _definitions.end() ? nullptr : &found->second;
}

bool EngineeringMetricRegistry::validate(const EngineeringEvaluationResult& result,
                                         std::vector<AnalysisWarning>* warnings) const
{
    std::vector<AnalysisWarning> localWarnings;
    std::set<std::string> ids;
    for (const EngineeringMetric& metric : result.metrics) {
        if (!ids.insert(metric.metricId).second) {
            localWarnings.push_back(warning("EngineeringMetric.DuplicateId",
                                            "Duplicate metric ID: " + metric.metricId));
            continue;
        }
        const EngineeringMetricDefinition* definition = find(metric.metricId);
        if (definition == nullptr) {
            localWarnings.push_back(warning("EngineeringMetric.UnknownId",
                                            "Unknown metric ID: " + metric.metricId));
            continue;
        }
        if (definition->unit != metric.unit) {
            localWarnings.push_back(warning("EngineeringMetric.UnitMismatch",
                                            "Metric unit does not match registry: " + metric.metricId));
        }
        if (!std::isfinite(metric.value)) {
            localWarnings.push_back(warning("EngineeringMetric.Value.NonFinite",
                                            "Metric value is not finite: " + metric.metricId));
        }
    }
    if (warnings != nullptr)
        *warnings = localWarnings;
    return localWarnings.empty();
}

} // namespace rws
