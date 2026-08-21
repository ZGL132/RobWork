#include "ParameterizationMode.hpp"

#include <algorithm>

namespace rws {
namespace {

void addError(ParameterizationResolution& result, const std::string& code,
              const std::string& fieldPath, const std::string& message)
{
    result.valid = false;
    StructureOptimizationDiagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.severity = "Error";
    diagnostic.subsystem = "design-space";
    diagnostic.stage = "parameterization";
    diagnostic.fieldPath = fieldPath;
    diagnostic.message = message;
    result.diagnostics.push_back(diagnostic);
}

}    // namespace

ParameterizationModeRegistry ParameterizationModeRegistry::firstPhase()
{
    ParameterizationModeRegistry registry;
    const ParameterizationMode modes[] = {
        {"LinkPlacementMode=AlongReferenceDirection", "LinkPlacementMode",
         {SemanticKind::LinkLength}},
        {"LinkPlacementMode=CartesianJointOrigin", "LinkPlacementMode",
         {SemanticKind::JointOriginOffsetX, SemanticKind::JointOriginOffsetY,
          SemanticKind::JointOriginOffsetZ}},
        {"JointOriginMode=Cartesian", "JointOriginMode",
         {SemanticKind::JointOriginOffsetX, SemanticKind::JointOriginOffsetY,
          SemanticKind::JointOriginOffsetZ}},
        {"JointOriginMode=AlongAxis", "JointOriginMode",
         {SemanticKind::JointOffsetAlongAxis}},
        {"ToolPoseMode=Tcp", "ToolPoseMode",
         {SemanticKind::TcpTx, SemanticKind::TcpTy, SemanticKind::TcpTz,
          SemanticKind::TcpRotationVectorX, SemanticKind::TcpRotationVectorY,
          SemanticKind::TcpRotationVectorZ}},
        {"ToolPoseMode=Flange", "ToolPoseMode",
         {SemanticKind::FlangeTx, SemanticKind::FlangeTy, SemanticKind::FlangeTz,
          SemanticKind::FlangeRotationVectorX, SemanticKind::FlangeRotationVectorY,
          SemanticKind::FlangeRotationVectorZ}}};
    for (const ParameterizationMode& mode : modes)
        registry._modes[mode.id] = mode;
    return registry;
}

const ParameterizationMode* ParameterizationModeRegistry::find(const std::string& id) const
{
    const auto found = _modes.find(id);
    return found == _modes.end() ? nullptr : &found->second;
}

ParameterizationResolution ParameterizationModeResolver::resolve(
    const std::vector< DesignVariableDefinition >& variables,
    const ParameterizationModeRegistry& registry,
    const std::vector< ParameterizationSelection >& selections)
{
    ParameterizationResolution result;
    result.variables = variables;
    std::map< std::string, std::string > selectedModeByGroup;
    for (const ParameterizationSelection& selection : selections) {
        const ParameterizationMode* const mode = registry.find(selection.modeId);
        if (mode == nullptr || mode->groupId != selection.groupId) {
            addError(result, "PARAMETERIZATION_SELECTION_INVALID", selection.groupId,
                     "The selected parameterization mode is not registered for its group.");
            continue;
        }
        const auto inserted = selectedModeByGroup.emplace(selection.groupId, selection.modeId);
        if (!inserted.second && inserted.first->second != selection.modeId)
            addError(result, "PARAMETERIZATION_SELECTION_DUPLICATE", selection.groupId,
                     "Only one parameterization mode may be selected per group.");
    }
    for (DesignVariableDefinition& variable : result.variables) {
        if (variable.parameterizationModeId.empty())
            continue;
        const ParameterizationMode* const mode = registry.find(variable.parameterizationModeId);
        if (mode == nullptr) {
            addError(result, "PARAMETERIZATION_MODE_UNKNOWN", variable.id,
                     "A variable references an unknown parameterization mode.");
            variable.status = DesignVariableStatus::Invalid;
            continue;
        }
        const auto selected = selectedModeByGroup.find(mode->groupId);
        if (selected != selectedModeByGroup.end() && selected->second == mode->id)
            continue;
        variable.enabled = false;
        variable.status = DesignVariableStatus::DisabledByParameterization;
        const std::string selectedId = selected == selectedModeByGroup.end() ?
            mode->groupId + "=Unselected" : selected->second;
        result.disabledReasons[variable.id] = "DisabledByParameterization: " + selectedId;
    }
    return result;
}

}    // namespace rws
