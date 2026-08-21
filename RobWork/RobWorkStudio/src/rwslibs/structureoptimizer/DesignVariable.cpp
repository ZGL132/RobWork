#include "DesignVariable.hpp"

#include <cmath>
#include <set>

namespace rws {
namespace {

void addError(DesignVariableValidationResult& result, const std::string& code,
              const std::string& fieldPath, const std::string& message)
{
    result.valid = false;
    StructureOptimizationDiagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.severity = "Error";
    diagnostic.subsystem = "design-space";
    diagnostic.stage = "validation";
    diagnostic.fieldPath = fieldPath;
    diagnostic.message = message;
    result.diagnostics.push_back(diagnostic);
}

bool finite(double value) { return std::isfinite(value); }

bool requiresFrame(SemanticKind kind)
{
    switch (kind) {
    case SemanticKind::JointOriginOffsetX: case SemanticKind::JointOriginOffsetY:
    case SemanticKind::JointOriginOffsetZ: case SemanticKind::JointOffsetAlongAxis:
    case SemanticKind::JointAxisTiltU: case SemanticKind::JointAxisTiltV:
    case SemanticKind::BaseTx: case SemanticKind::BaseTy: case SemanticKind::BaseTz:
    case SemanticKind::BaseRotationVectorX: case SemanticKind::BaseRotationVectorY:
    case SemanticKind::BaseRotationVectorZ: case SemanticKind::TcpTx:
    case SemanticKind::TcpTy: case SemanticKind::TcpTz:
    case SemanticKind::TcpRotationVectorX: case SemanticKind::TcpRotationVectorY:
    case SemanticKind::TcpRotationVectorZ: case SemanticKind::FlangeTx:
    case SemanticKind::FlangeTy: case SemanticKind::FlangeTz:
    case SemanticKind::FlangeRotationVectorX: case SemanticKind::FlangeRotationVectorY:
    case SemanticKind::FlangeRotationVectorZ:
        return true;
    default:
        return false;
    }
}

}    // namespace

DesignVariableValidationResult DesignVariableValidator::validate(
    const std::vector< DesignVariableDefinition >& variables)
{
    DesignVariableValidationResult result;
    std::set< std::string > ids;
    for (std::size_t index = 0; index < variables.size(); ++index) {
        const DesignVariableDefinition& variable = variables[index];
        const std::string prefix = "variables[" + std::to_string(index) + "]";
        if (variable.id.empty() || !ids.insert(variable.id).second)
            addError(result, "DESIGN_VARIABLE_ID_DUPLICATE", prefix + ".id",
                     "Design variable IDs must be non-empty and unique.");
        if (variable.semanticKind == SemanticKind::Unknown)
            addError(result, "DESIGN_VARIABLE_SEMANTIC_UNKNOWN", prefix + ".semanticKind",
                     "A design variable must use a registered semantic kind.");
        if (!finite(variable.nominalValue) || !finite(variable.currentValue) ||
            !finite(variable.minimum) || !finite(variable.maximum) || !finite(variable.step))
            addError(result, "DESIGN_VARIABLE_NONFINITE", prefix,
                     "All design variable numeric fields must be finite.");
        if (requiresFrame(variable.semanticKind) && variable.frameId.empty())
            addError(result, "DESIGN_VARIABLE_FRAME_REQUIRED", prefix + ".frameId",
                     "Position and rotation semantics must name their coordinate frame.");

        if (variable.role == VariableRole::Derived) {
            if (variable.derivedExpressionId.empty())
                addError(result, "DESIGN_VARIABLE_DERIVED_EXPRESSION_REQUIRED",
                         prefix + ".derivedExpressionId",
                         "Derived variables require a stable derived-expression ID.");
            continue;
        }

        if (variable.bindingId.empty())
            addError(result, "DESIGN_VARIABLE_BINDING_REQUIRED", prefix + ".bindingId",
                     "Independent variables require a typed parameter binding.");
        if (variable.domain == VariableDomain::Discrete) {
            if (variable.discreteOptions.empty())
                addError(result, "DESIGN_VARIABLE_DISCRETE_OPTIONS_REQUIRED",
                         prefix + ".discreteOptions",
                         "Discrete variables require at least one stable option ID.");
            std::set< std::string > optionIds;
            for (std::size_t optionIndex = 0; optionIndex < variable.discreteOptions.size();
                 ++optionIndex) {
                const std::string& id = variable.discreteOptions[optionIndex].id;
                if (id.empty() || !optionIds.insert(id).second)
                    addError(result, "DESIGN_VARIABLE_DISCRETE_OPTION_ID_INVALID",
                             prefix + ".discreteOptions[" + std::to_string(optionIndex) + "].id",
                             "Discrete option IDs must be non-empty and unique.");
            }
            continue;
        }
        if (variable.minimum > variable.maximum || variable.nominalValue < variable.minimum ||
            variable.nominalValue > variable.maximum || variable.currentValue < variable.minimum ||
            variable.currentValue > variable.maximum)
            addError(result, "DESIGN_VARIABLE_RANGE_INVALID", prefix,
                     "Independent values must satisfy minimum <= nominal/current <= maximum.");
        if (variable.step <= 0.0)
            addError(result, "DESIGN_VARIABLE_STEP_INVALID", prefix + ".step",
                     "Continuous and integer variables require a positive step.");
    }
    return result;
}

std::string variableRoleToString(VariableRole role)
{
    return role == VariableRole::Independent ? "Independent" : "Derived";
}

bool variableRoleFromString(const std::string& value, VariableRole& role)
{
    if (value == "Independent") { role = VariableRole::Independent; return true; }
    if (value == "Derived") { role = VariableRole::Derived; return true; }
    return false;
}

std::string variableDomainToString(VariableDomain domain)
{
    switch (domain) {
    case VariableDomain::Continuous: return "Continuous";
    case VariableDomain::Integer: return "Integer";
    case VariableDomain::Discrete: return "Discrete";
    }
    return "Unknown";
}

bool variableDomainFromString(const std::string& value, VariableDomain& domain)
{
    if (value == "Continuous") { domain = VariableDomain::Continuous; return true; }
    if (value == "Integer") { domain = VariableDomain::Integer; return true; }
    if (value == "Discrete") { domain = VariableDomain::Discrete; return true; }
    return false;
}

std::string semanticKindToString(SemanticKind kind)
{
    static const std::pair< SemanticKind, const char* > values[] = {
        {SemanticKind::LinkLength, "LinkLength"},
        {SemanticKind::JointOriginOffsetX, "JointOriginOffsetX"},
        {SemanticKind::JointOriginOffsetY, "JointOriginOffsetY"},
        {SemanticKind::JointOriginOffsetZ, "JointOriginOffsetZ"},
        {SemanticKind::JointOffsetAlongAxis, "JointOffsetAlongAxis"},
        {SemanticKind::JointAxisTiltU, "JointAxisTiltU"},
        {SemanticKind::JointAxisTiltV, "JointAxisTiltV"},
        {SemanticKind::JointZeroOffset, "JointZeroOffset"},
        {SemanticKind::JointLimitLower, "JointLimitLower"},
        {SemanticKind::JointLimitUpper, "JointLimitUpper"},
        {SemanticKind::BaseTx, "BaseTx"}, {SemanticKind::BaseTy, "BaseTy"},
        {SemanticKind::BaseTz, "BaseTz"},
        {SemanticKind::BaseRotationVectorX, "BaseRotationVectorX"},
        {SemanticKind::BaseRotationVectorY, "BaseRotationVectorY"},
        {SemanticKind::BaseRotationVectorZ, "BaseRotationVectorZ"},
        {SemanticKind::TcpTx, "TcpTx"}, {SemanticKind::TcpTy, "TcpTy"},
        {SemanticKind::TcpTz, "TcpTz"},
        {SemanticKind::TcpRotationVectorX, "TcpRotationVectorX"},
        {SemanticKind::TcpRotationVectorY, "TcpRotationVectorY"},
        {SemanticKind::TcpRotationVectorZ, "TcpRotationVectorZ"},
        {SemanticKind::FlangeTx, "FlangeTx"}, {SemanticKind::FlangeTy, "FlangeTy"},
        {SemanticKind::FlangeTz, "FlangeTz"},
        {SemanticKind::FlangeRotationVectorX, "FlangeRotationVectorX"},
        {SemanticKind::FlangeRotationVectorY, "FlangeRotationVectorY"},
        {SemanticKind::FlangeRotationVectorZ, "FlangeRotationVectorZ"},
        {SemanticKind::LinkRadius, "LinkRadius"}, {SemanticKind::LinkWidth, "LinkWidth"},
        {SemanticKind::LinkHeight, "LinkHeight"},
        {SemanticKind::LinkCrossSectionX, "LinkCrossSectionX"},
        {SemanticKind::LinkCrossSectionY, "LinkCrossSectionY"},
        {SemanticKind::LinkWallThickness, "LinkWallThickness"},
        {SemanticKind::LinkScale, "LinkScale"},
        {SemanticKind::GeometryRadius, "GeometryRadius"},
        {SemanticKind::GeometryLength, "GeometryLength"},
        {SemanticKind::GeometryWidth, "GeometryWidth"},
        {SemanticKind::GeometryHeight, "GeometryHeight"},
        {SemanticKind::GeometryDepth, "GeometryDepth"},
        {SemanticKind::GeometryWallThickness, "GeometryWallThickness"},
        {SemanticKind::GeometryRigidTransform, "GeometryRigidTransform"},
        {SemanticKind::ParameterizedMaterial, "ParameterizedMaterial"}};
    for (const auto& value : values)
        if (value.first == kind) return value.second;
    return "Unknown";
}

bool semanticKindFromString(const std::string& value, SemanticKind& kind)
{
    for (int index = static_cast< int >(SemanticKind::LinkLength);
         index <= static_cast< int >(SemanticKind::ParameterizedMaterial); ++index) {
        const SemanticKind candidate = static_cast< SemanticKind >(index);
        if (semanticKindToString(candidate) == value) { kind = candidate; return true; }
    }
    return false;
}

std::string designVariableUnitToString(DesignVariableUnit unit)
{
    switch (unit) {
    case DesignVariableUnit::Unitless: return "Unitless";
    case DesignVariableUnit::Metres: return "Metres";
    case DesignVariableUnit::Radians: return "Radians";
    case DesignVariableUnit::Degrees: return "Degrees";
    case DesignVariableUnit::Kilograms: return "Kilograms";
    case DesignVariableUnit::NewtonMetres: return "NewtonMetres";
    }
    return "Unknown";
}

bool designVariableUnitFromString(const std::string& value, DesignVariableUnit& unit)
{
    for (int index = static_cast< int >(DesignVariableUnit::Unitless);
         index <= static_cast< int >(DesignVariableUnit::NewtonMetres); ++index) {
        const DesignVariableUnit candidate = static_cast< DesignVariableUnit >(index);
        if (designVariableUnitToString(candidate) == value) { unit = candidate; return true; }
    }
    return false;
}

}    // namespace rws
