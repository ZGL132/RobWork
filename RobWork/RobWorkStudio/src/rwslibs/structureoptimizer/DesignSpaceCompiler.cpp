#include "DesignSpaceCompiler.hpp"

#include "AdapterRegistry.hpp"
#include "DependencyGraph.hpp"
#include "KinematicFingerprint.hpp"
#include "WriteSetValidator.hpp"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <locale>
#include <map>
#include <set>
#include <sstream>

namespace rws {
namespace {

void addDiagnostic(DesignSpaceCompileResult& result, const std::string& code,
                   const std::string& field, const std::string& message,
                   const std::string& severity = "Error")
{
    StructureOptimizationDiagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.severity = severity;
    diagnostic.subsystem = "design-space";
    diagnostic.stage = "compile";
    diagnostic.fieldPath = field;
    diagnostic.message = message;
    result.diagnostics.push_back(diagnostic);
}

void appendText(std::ostringstream& stream, const std::string& value)
{
    stream << value.size() << ':' << value << ';';
}

void appendDouble(std::ostringstream& stream, const double value)
{
    stream << std::hexfloat << (value == 0.0 ? 0.0 : value) << ';';
}

void appendTarget(std::ostringstream& stream, const ReadWriteTarget& target)
{
    stream << static_cast< int >(target.objectType) << '|';
    appendText(stream, target.objectId);
    stream << static_cast< int >(target.propertyId) << '|';
    appendText(stream, target.coordinateFrameId);
}

void appendBinding(std::ostringstream& stream, const ParameterBinding& binding)
{
    appendText(stream, binding.id);
    stream << static_cast< int >(binding.semanticKind) << '|'
           << static_cast< int >(binding.targetObjectType) << '|';
    appendText(stream, binding.targetObjectId);
    stream << static_cast< int >(binding.targetPropertyId) << '|';
    appendText(stream, binding.coordinateFrameId);
    appendText(stream, binding.parameterizationModeId);
    appendText(stream, binding.ownerAdapterId);
    stream << binding.ownerAdapterVersion << '|';
    appendText(stream, binding.referenceDirectionFrameId);
    appendDouble(stream, binding.referenceDirection(0));
    appendDouble(stream, binding.referenceDirection(1));
    appendDouble(stream, binding.referenceDirection(2));
    appendDouble(stream, binding.maxAxisTiltAngle);
    appendText(stream, binding.axisTiltGroupId);
    stream << static_cast< int >(binding.jointLimitScope) << '|';
    appendText(stream, binding.jointLimitGroupId);
    appendDouble(stream, binding.minimumJointLimitRange);
    stream << (binding.allowPhysicalLimitModification ? '1' : '0') << '|';
    appendDouble(stream, binding.absoluteJointLimitLower);
    appendDouble(stream, binding.absoluteJointLimitUpper);
    stream << static_cast< int >(binding.jointLimitCoordinateConvention) << '|';
    appendText(stream, binding.poseDeltaGroupId);
    stream << static_cast< int >(binding.poseDeltaComposition) << '|';
    appendText(stream, binding.geometryGroupId);
    std::vector< std::string > capabilities = binding.requiredCapabilityIds;
    std::sort(capabilities.begin(), capabilities.end());
    for (const std::string& capability : capabilities)
        appendText(stream, capability);
    const auto targetLess = [](const ReadWriteTarget& first, const ReadWriteTarget& second) {
        if (first.objectType != second.objectType) return first.objectType < second.objectType;
        if (first.objectId != second.objectId) return first.objectId < second.objectId;
        if (first.propertyId != second.propertyId) return first.propertyId < second.propertyId;
        return first.coordinateFrameId < second.coordinateFrameId;
    };
    std::vector< ReadWriteTarget > readSet = binding.readSet;
    std::vector< ReadWriteTarget > writeSet = binding.writeSet;
    std::sort(readSet.begin(), readSet.end(), targetLess);
    std::sort(writeSet.begin(), writeSet.end(), targetLess);
    stream << "|read|";
    for (const ReadWriteTarget& target : readSet)
        appendTarget(stream, target);
    stream << "|write|";
    for (const ReadWriteTarget& target : writeSet)
        appendTarget(stream, target);
    stream << binding.bindingVersion << ';';
}

void appendVariable(std::ostringstream& stream, const DesignVariableDefinition& variable)
{
    appendText(stream, variable.id);
    stream << static_cast< int >(variable.semanticKind) << '|'
           << static_cast< int >(variable.role) << '|'
           << static_cast< int >(variable.domain) << '|'
           << static_cast< int >(variable.unit) << '|';
    appendText(stream, variable.groupId);
    appendText(stream, variable.parameterizationModeId);
    appendDouble(stream, variable.nominalValue);
    appendDouble(stream, variable.currentValue);
    appendDouble(stream, variable.minimum);
    appendDouble(stream, variable.maximum);
    appendDouble(stream, variable.step);
    appendText(stream, variable.frameId);
    appendText(stream, variable.derivedExpressionId);
    appendText(stream, variable.bindingId);
    for (const DiscreteOption& option : variable.discreteOptions) {
        appendText(stream, option.id);
        appendText(stream, option.payloadReference);
    }
}

void appendExpression(std::ostringstream& stream, const DerivedExpression& expression)
{
    appendText(stream, expression.id);
    stream << static_cast< int >(expression.kind) << '|';
    appendText(stream, expression.registeredFunctionId);
    for (const DerivedExpressionOperand& operand : expression.operands) {
        stream << (operand.isVariableReference ? 'v' : 'c') << '|';
        appendText(stream, operand.variableId);
        appendDouble(stream, operand.constantValue.value);
        stream << static_cast< int >(operand.constantValue.unit) << ';';
    }
}

std::string fnv1a64(const std::string& content)
{
    std::uint64_t hash = UINT64_C(14695981039346656037);
    for (const unsigned char value : content) {
        hash ^= value;
        hash *= UINT64_C(1099511628211);
    }
    std::ostringstream encoded;
    encoded.imbue(std::locale::classic());
    encoded << std::hex << std::setfill('0') << std::setw(16) << hash;
    return encoded.str();
}

std::string fingerprint(const CompiledDesignSpace& space, const std::string& modelFingerprint,
                        const AdapterCapabilityQuery& capabilities,
                        const AdapterRegistry& adapterRegistry)
{
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    appendText(stream, "compiled-design-space-v1");
    appendText(stream, modelFingerprint);
    appendText(stream, capabilities.fingerprintMaterial());
    appendText(stream, adapterRegistry.fingerprintMaterial());
    stream << space.schemaVersion << ';';
    for (const DesignVariableDefinition& variable : space.independentVariables)
        appendVariable(stream, variable);
    stream << "|derived|";
    for (const DesignVariableDefinition& variable : space.derivedVariables)
        appendVariable(stream, variable);
    stream << "|groups|";
    for (const CompiledVariableGroup& group : space.variableGroups) {
        appendText(stream, group.id);
        for (const std::string& variableId : group.variableIds)
            appendText(stream, variableId);
    }
    stream << "|modes|";
    for (const ParameterizationSelection& selection : space.parameterizationModes) {
        appendText(stream, selection.groupId);
        appendText(stream, selection.modeId);
    }
    stream << "|bindings|";
    for (const ParameterBinding& binding : space.resolvedBindings)
        appendBinding(stream, binding);
    stream << "|expressions|";
    for (const DerivedExpression& expression : space.derivedExpressions)
        appendExpression(stream, expression);
    stream << "|dependencies|";
    for (const std::string& dependency : space.dependencyOrder)
        appendText(stream, dependency);
    stream << "|schema|";
    for (const CanonicalVectorSchemaEntry& entry : space.canonicalVectorSchema) {
        stream << entry.index << '|';
        appendText(stream, entry.variableId);
        stream << static_cast< int >(entry.unit) << ';';
    }
    stream << "|disabled|";
    for (const auto& disabled : space.disabledReasons) {
        appendText(stream, disabled.first);
        appendText(stream, disabled.second);
    }
    return fnv1a64(stream.str());
}

bool containsErrors(const std::vector< StructureOptimizationDiagnostic >& diagnostics)
{
    return std::any_of(diagnostics.begin(), diagnostics.end(),
                       [](const StructureOptimizationDiagnostic& diagnostic) {
                           return diagnostic.severity == "Error";
                       });
}

bool isJointCoordinateSemantic(const SemanticKind semantic)
{
    return semantic == SemanticKind::JointZeroOffset ||
           semantic == SemanticKind::JointLimitLower ||
           semantic == SemanticKind::JointLimitUpper;
}

/** Only S34 spatial variables carry a declared pose coordinate frame. */
bool isS34SpatialSemantic(const SemanticKind semantic)
{
    switch (semantic) {
    case SemanticKind::BaseTx:
    case SemanticKind::BaseTy:
    case SemanticKind::BaseTz:
    case SemanticKind::BaseRotationVectorX:
    case SemanticKind::BaseRotationVectorY:
    case SemanticKind::BaseRotationVectorZ:
    case SemanticKind::TcpTx:
    case SemanticKind::TcpTy:
    case SemanticKind::TcpTz:
    case SemanticKind::TcpRotationVectorX:
    case SemanticKind::TcpRotationVectorY:
    case SemanticKind::TcpRotationVectorZ:
    case SemanticKind::FlangeTx:
    case SemanticKind::FlangeTy:
    case SemanticKind::FlangeTz:
    case SemanticKind::FlangeRotationVectorX:
    case SemanticKind::FlangeRotationVectorY:
    case SemanticKind::FlangeRotationVectorZ:
        return true;
    default:
        return false;
    }
}

const JointEdge* findJoint(const CanonicalKinematicModel& model, const std::string& id)
{
    for (const JointEdge& joint : model.joints)
        if (joint.id == id)
            return &joint;
    return nullptr;
}

DesignVariableUnit expectedVariableUnit(const SemanticMetadata& metadata,
                                        const ParameterBinding* binding,
                                        const CanonicalKinematicModel& model)
{
    if (binding != nullptr && isJointCoordinateSemantic(metadata.semanticKind)) {
        const JointEdge* const joint = findJoint(model, binding->targetObjectId);
        if (joint != nullptr && joint->type == CanonicalJointType::Prismatic)
            return DesignVariableUnit::Metres;
    }
    return metadata.unit;
}

}    // namespace

DesignSpaceCompileResult DesignSpaceCompiler::compile(const DesignSpaceCompileRequest& request)
{
    DesignSpaceCompileResult result;
    if (request.model == nullptr || request.registry == nullptr || request.capabilities == nullptr) {
        addDiagnostic(result, "DESIGN_SPACE_INPUT_MISSING", "request",
                      "Canonical model, semantic registry, and adapter capabilities are required.");
        return result;
    }
    if (request.adapterRegistry == nullptr) {
        addDiagnostic(result, "DESIGN_SPACE_ADAPTER_REGISTRY_REQUIRED", "adapterRegistry",
                      "Every design-space compilation requires a trusted adapter registry.");
        return result;
    }

    const CanonicalKinematicModelValidationResult modelValidation =
        CanonicalKinematicModelValidator::validate(*request.model);
    result.diagnostics.insert(result.diagnostics.end(), modelValidation.diagnostics.begin(),
                              modelValidation.diagnostics.end());
    if (!modelValidation.valid) {
        addDiagnostic(result, "DESIGN_SPACE_MODEL_INVALID", "model",
                      "Canonical model validation failed.");
        return result;
    }
    const KinematicFingerprintResult modelFingerprint = KinematicFingerprint::forModel(*request.model);
    result.diagnostics.insert(result.diagnostics.end(), modelFingerprint.diagnostics.begin(),
                              modelFingerprint.diagnostics.end());
    if (!modelFingerprint.ok) return result;

    const ParameterizationResolution modes = ParameterizationModeResolver::resolve(
        request.variables, ParameterizationModeRegistry::firstPhase(), request.parameterizationSelections);
    result.diagnostics.insert(result.diagnostics.end(), modes.diagnostics.begin(), modes.diagnostics.end());
    if (!modes.valid) return result;
    result.designSpace.disabledReasons = modes.disabledReasons;
    for (const auto& disabled : modes.disabledReasons)
        addDiagnostic(result, "DESIGN_SPACE_VARIABLE_DISABLED", disabled.first, disabled.second, "Info");

    const DesignVariableValidationResult variables = DesignVariableValidator::validate(modes.variables);
    result.diagnostics.insert(result.diagnostics.end(), variables.diagnostics.begin(), variables.diagnostics.end());
    const DerivedExpressionTargetValidationResult targets =
        DerivedExpressionTargetValidator::validate(modes.variables);
    result.diagnostics.insert(result.diagnostics.end(), targets.diagnostics.begin(), targets.diagnostics.end());
    if (!variables.valid || !targets.valid) return result;

    std::map< std::string, const ParameterBinding* > bindingById;
    for (const ParameterBinding& binding : request.bindings) {
        const ParameterBindingValidationResult bindingValidation =
            ParameterBindingValidator::validate(binding);
        result.diagnostics.insert(result.diagnostics.end(), bindingValidation.diagnostics.begin(),
                                  bindingValidation.diagnostics.end());
        if (!bindingById.emplace(binding.id, &binding).second)
            addDiagnostic(result, "PARAMETER_BINDING_ID_DUPLICATE", binding.id,
                          "Parameter binding IDs must be non-empty and unique.");
    }
    if (containsErrors(result.diagnostics)) return result;

    const WriteSetValidationResult writes = WriteSetValidator::validate(modes.variables, request.bindings);
    result.diagnostics.insert(result.diagnostics.end(), writes.diagnostics.begin(), writes.diagnostics.end());
    if (!writes.valid) return result;

    std::map< std::string, const DesignVariableDefinition* > derivedByExpressionId;
    std::set< std::string > resolvedBindingIds;
    std::map< std::string, DerivedValue > resolvedIndependentValues;
    for (const DesignVariableDefinition& variable : modes.variables) {
        if (!variable.enabled || variable.status == DesignVariableStatus::DisabledByParameterization)
            continue;
        const SemanticMetadata* const metadata = request.registry->find(variable.semanticKind);
        if (metadata == nullptr) {
            addDiagnostic(result, "DESIGN_SPACE_SEMANTIC_UNREGISTERED", variable.id,
                          "Variable semantic is not registered.");
            continue;
        }
        const auto binding = bindingById.find(variable.bindingId);
        const ParameterBinding* const variableBinding = binding == bindingById.end() ?
            nullptr : binding->second;
        if (expectedVariableUnit(*metadata, variableBinding, *request.model) != variable.unit) {
            addDiagnostic(result, "DESIGN_SPACE_VARIABLE_UNIT_MISMATCH", variable.id,
                          "Variable unit does not match its registered or target-joint coordinate unit.");
            continue;
        }
        if (variable.role == VariableRole::Independent && metadata->domain != variable.domain) {
            addDiagnostic(result, "DESIGN_SPACE_VARIABLE_DOMAIN_MISMATCH", variable.id,
                          "Variable domain does not match its registered semantic domain.");
            continue;
        }
        if (!variable.bindingId.empty()) {
            if (binding == bindingById.end()) {
                addDiagnostic(result, "PARAMETER_BINDING_MISSING", variable.id,
                              "An active variable must have a declared parameter binding.");
                continue;
            }
            if (binding->second->semanticKind != variable.semanticKind) {
                addDiagnostic(result, "PARAMETER_BINDING_SEMANTIC_MISMATCH", variable.id,
                              "A parameter binding semantic must match its variable semantic.");
                continue;
            }
            if (isS34SpatialSemantic(variable.semanticKind) &&
                variable.frameId != binding->second->coordinateFrameId) {
                addDiagnostic(result, "DESIGN_SPACE_VARIABLE_BINDING_FRAME_MISMATCH", variable.id,
                              "A spatial variable frame must exactly match its binding coordinate frame.");
                continue;
            }
            const IModelParameterAdapter* const adapter =
                request.adapterRegistry->find(binding->second->ownerAdapterId);
            if (adapter == nullptr) {
                addDiagnostic(result, "DESIGN_SPACE_ADAPTER_OWNER_UNREGISTERED", variable.id,
                              "An active binding owner must exist in the trusted adapter registry.");
                continue;
            }
            if (binding->second->ownerAdapterVersion != adapter->adapterVersion()) {
                addDiagnostic(result, "DESIGN_SPACE_ADAPTER_VERSION_MISMATCH", variable.id,
                              "An active binding adapter version must match the trusted registry.");
                continue;
            }
            const std::vector< SemanticKind > supported = adapter->supportedSemanticKinds();
            if (std::find(supported.begin(), supported.end(), variable.semanticKind) == supported.end()) {
                addDiagnostic(result, "DESIGN_SPACE_ADAPTER_SEMANTIC_MISMATCH", variable.id,
                              "An active binding semantic must be supported by its registered adapter.");
                continue;
            }
            resolvedBindingIds.insert(variable.bindingId);
        } else if (variable.role == VariableRole::Independent) {
            addDiagnostic(result, "PARAMETER_BINDING_MISSING", variable.id,
                          "An active independent variable must have a declared parameter binding.");
            continue;
        }
        if (variable.role == VariableRole::Derived) {
            if (!derivedByExpressionId.emplace(variable.derivedExpressionId, &variable).second)
                addDiagnostic(result, "DERIVED_EXPRESSION_TARGET_DUPLICATE", variable.derivedExpressionId,
                              "Only one active derived variable may own an expression ID.");
            continue;
        }
        resolvedIndependentValues[variable.id] = {variable.currentValue, variable.unit};
        result.designSpace.independentVariables.push_back(variable);
    }
    if (containsErrors(result.diagnostics)) return result;

    std::set< std::string > expressionIds;
    for (const DerivedExpression& expression : request.derivedExpressions) {
        if (!expressionIds.insert(expression.id).second)
            continue;    // DependencyGraph produces the primary stable duplicate-ID diagnostic.
        if (derivedByExpressionId.find(expression.id) == derivedByExpressionId.end())
            addDiagnostic(result, "DERIVED_EXPRESSION_ORPHAN", expression.id,
                          "A derived expression must be owned by one active derived variable.");
    }
    for (const auto& derived : derivedByExpressionId)
        if (expressionIds.find(derived.first) == expressionIds.end())
            addDiagnostic(result, "DERIVED_EXPRESSION_MISSING", derived.second->id,
                          "An active derived variable requires its declared expression.");
    if (containsErrors(result.diagnostics)) return result;

    if (!request.derivedExpressions.empty()) {
        const DerivedExpressionEvaluationResult evaluated =
            DependencyGraph::evaluate(request.derivedExpressions, resolvedIndependentValues);
        result.diagnostics.insert(result.diagnostics.end(), evaluated.diagnostics.begin(),
                                  evaluated.diagnostics.end());
        if (!evaluated.ok) return result;
        for (const std::string& expressionId : evaluated.evaluationOrder) {
            const auto derived = derivedByExpressionId.find(expressionId);
            if (derived == derivedByExpressionId.end()) continue;
            const auto value = evaluated.values.find(expressionId);
            if (value == evaluated.values.end() || value->second.unit != derived->second->unit) {
                addDiagnostic(result, "DESIGN_SPACE_DERIVED_UNIT_MISMATCH", derived->second->id,
                              "A derived expression result must use the variable's declared unit.");
                continue;
            }
            result.designSpace.dependencyOrder.push_back(derived->second->id);
            result.designSpace.derivedVariables.push_back(*derived->second);
        }
        if (containsErrors(result.diagnostics)) return result;
    }

    const auto byId = [](const DesignVariableDefinition& first, const DesignVariableDefinition& second) {
        return first.id < second.id;
    };
    std::sort(result.designSpace.independentVariables.begin(), result.designSpace.independentVariables.end(), byId);
    std::sort(result.designSpace.derivedVariables.begin(), result.designSpace.derivedVariables.end(), byId);
    for (std::size_t index = 0; index < result.designSpace.independentVariables.size(); ++index) {
        const DesignVariableDefinition& variable = result.designSpace.independentVariables[index];
        result.designSpace.canonicalVectorSchema.push_back({variable.id, index, variable.unit});
    }
    std::map< std::string, std::vector< std::string > > groups;
    for (const DesignVariableDefinition& variable : result.designSpace.independentVariables)
        if (!variable.groupId.empty()) groups[variable.groupId].push_back(variable.id);
    for (const DesignVariableDefinition& variable : result.designSpace.derivedVariables)
        if (!variable.groupId.empty()) groups[variable.groupId].push_back(variable.id);
    for (const auto& group : groups)
        result.designSpace.variableGroups.push_back({group.first, group.second});
    std::map< std::string, std::string > selectedModes;
    for (const ParameterizationSelection& selection : request.parameterizationSelections)
        selectedModes[selection.groupId] = selection.modeId;
    for (const auto& selection : selectedModes)
        result.designSpace.parameterizationModes.push_back({selection.first, selection.second});
    for (const ParameterBinding& binding : request.bindings)
        if (resolvedBindingIds.find(binding.id) != resolvedBindingIds.end())
            result.designSpace.resolvedBindings.push_back(binding);
    std::sort(result.designSpace.resolvedBindings.begin(), result.designSpace.resolvedBindings.end(),
              [](const ParameterBinding& first, const ParameterBinding& second) {
                  return first.id < second.id;
              });
    result.designSpace.derivedExpressions = request.derivedExpressions;
    std::sort(result.designSpace.derivedExpressions.begin(), result.designSpace.derivedExpressions.end(),
              [](const DerivedExpression& first, const DerivedExpression& second) {
                  return first.id < second.id;
              });
    result.designSpace.fingerprint = fingerprint(result.designSpace, modelFingerprint.value,
                                                 *request.capabilities,
                                                 *request.adapterRegistry);
    result.ok = true;
    return result;
}

}    // namespace rws
