#include "BasePlacementAdapter.hpp"

#include <cmath>

namespace rws {
namespace {

bool isBaseSemantic(const SemanticKind semantic)
{
    return semantic == SemanticKind::BaseTx || semantic == SemanticKind::BaseTy ||
           semantic == SemanticKind::BaseTz || semantic == SemanticKind::BaseRotationVectorX ||
           semantic == SemanticKind::BaseRotationVectorY ||
           semantic == SemanticKind::BaseRotationVectorZ;
}

TargetPropertyId propertyFor(const SemanticKind semantic)
{
    switch (semantic) {
    case SemanticKind::BaseTx: return TargetPropertyId::BaseTranslationX;
    case SemanticKind::BaseTy: return TargetPropertyId::BaseTranslationY;
    case SemanticKind::BaseTz: return TargetPropertyId::BaseTranslationZ;
    case SemanticKind::BaseRotationVectorX: return TargetPropertyId::BaseRotationVectorX;
    case SemanticKind::BaseRotationVectorY: return TargetPropertyId::BaseRotationVectorY;
    case SemanticKind::BaseRotationVectorZ: return TargetPropertyId::BaseRotationVectorZ;
    default: return TargetPropertyId::Unknown;
    }
}

bool isTranslation(const SemanticKind semantic)
{
    return semantic == SemanticKind::BaseTx || semantic == SemanticKind::BaseTy ||
           semantic == SemanticKind::BaseTz;
}

ReadWriteTarget ownTarget(const ParameterBinding& binding)
{
    return {TargetObjectType::Frame, binding.targetObjectId, propertyFor(binding.semanticKind),
            binding.coordinateFrameId};
}

bool exactSingleTarget(const std::vector< ReadWriteTarget >& targets,
                       const ReadWriteTarget& expected)
{
    return targets.size() == 1 && targets.front() == expected;
}

void addError(std::vector< StructureOptimizationDiagnostic >& diagnostics,
              const std::string& bindingId, const std::string& objectId,
              const std::string& field, const std::string& code, const std::string& message)
{
    diagnostics.push_back(makeAdapterDiagnostic("BasePlacementAdapter", bindingId, objectId, field,
                                                code, message));
}

}    // namespace

std::string BasePlacementAdapter::adapterId() const { return "BasePlacementAdapter"; }
int BasePlacementAdapter::adapterVersion() const { return 1; }
std::vector< SemanticKind > BasePlacementAdapter::supportedSemanticKinds() const
{
    return {SemanticKind::BaseTx, SemanticKind::BaseTy, SemanticKind::BaseTz,
            SemanticKind::BaseRotationVectorX, SemanticKind::BaseRotationVectorY,
            SemanticKind::BaseRotationVectorZ};
}
std::vector< AdapterCapability > BasePlacementAdapter::requiredCapabilities() const
{
    return {AdapterCapability::BasePlacement};
}

AdapterBindingValidationResult BasePlacementAdapter::validateBinding(
    const ParameterBinding& binding, const CanonicalKinematicModel& baseline) const
{
    AdapterBindingValidationResult result;
    if (!isBaseSemantic(binding.semanticKind)) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId, "semanticKind",
                 "BASE_PLACEMENT_SEMANTIC_UNSUPPORTED",
                 "BasePlacementAdapter accepts only Base translation and rotation-vector semantics.");
    }
    if (binding.targetObjectType != TargetObjectType::Frame ||
        binding.targetObjectId != baseline.baseFrameId) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId, "targetObjectId",
                 "BASE_PLACEMENT_BASE_FRAME_REQUIRED",
                 "Base placement must target the canonical base frame only.");
    }
    if (binding.coordinateFrameId != baseline.rootFrameId) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId, "coordinateFrameId",
                 "BASE_PLACEMENT_ROOT_FRAME_REQUIRED",
                 "Base translation must be declared in the immutable system root frame.");
    }
    if (binding.targetPropertyId != propertyFor(binding.semanticKind)) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId, "targetPropertyId",
                 "BASE_PLACEMENT_PROPERTY_INVALID",
                 "Base semantics require their matching typed placement property.");
    }
    if (binding.poseDeltaGroupId != "base-pose:" + baseline.baseFrameId) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId, "poseDeltaGroupId",
                 "BASE_PLACEMENT_GROUP_INVALID",
                 "Base placement groups must be derived from the canonical base frame.");
    }
    if (binding.poseDeltaComposition != PoseDeltaComposition::Right) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId, "poseDeltaComposition",
                 "BASE_PLACEMENT_ROTATION_COMPOSITION_INVALID",
                 "Base SO(3) deltas use the frozen right-multiplied convention only.");
    }
    const ReadWriteTarget expected = ownTarget(binding);
    if (!exactSingleTarget(binding.readSet, expected) ||
        !exactSingleTarget(binding.writeSet, expected)) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId, "readSet/writeSet",
                 "BASE_PLACEMENT_TARGET_SET_INVALID",
                 "Base placement bindings must declare exactly their own typed target once.");
    }
    return result;
}

std::vector< ReadWriteTarget > BasePlacementAdapter::declaredReadSet(
    const ParameterBinding& binding) const
{
    return isBaseSemantic(binding.semanticKind) ? std::vector< ReadWriteTarget >{ownTarget(binding)} :
                                                   std::vector< ReadWriteTarget >();
}

std::vector< ReadWriteTarget > BasePlacementAdapter::declaredWriteSet(
    const ParameterBinding& binding) const
{
    return declaredReadSet(binding);
}

AdapterPatchCompileResult BasePlacementAdapter::compilePatch(
    const AdapterPatchCompileRequest& request) const
{
    AdapterPatchCompileResult result;
    if (request.baseline == nullptr || request.binding == nullptr) {
        addError(result.diagnostics, "", "", "request", "BASE_PLACEMENT_REQUEST_REQUIRED",
                 "BasePlacementAdapter requires immutable baseline and binding inputs.");
        return result;
    }
    const ParameterBinding& binding = *request.binding;
    const ParameterBindingValidationResult genericValidation =
        ParameterBindingValidator::validate(binding);
    if (!genericValidation.valid) {
        result.diagnostics = genericValidation.diagnostics;
        return result;
    }
    const AdapterBindingValidationResult validation = validateBinding(binding, *request.baseline);
    if (!validation.valid) {
        result.diagnostics = validation.diagnostics;
        return result;
    }
    if (request.values.size() != 1 || request.values.front().semanticKind != binding.semanticKind ||
        request.values.front().groupId != binding.poseDeltaGroupId ||
        !request.values.front().discreteOptionId.empty() ||
        !std::isfinite(request.values.front().engineeringValue) ||
        request.values.front().unit != (isTranslation(binding.semanticKind) ?
                                           DesignVariableUnit::Metres : DesignVariableUnit::Radians)) {
        addError(result.diagnostics, binding.id, binding.targetObjectId, "values",
                 "BASE_PLACEMENT_VALUE_INVALID",
                 "Base placement requires one finite scalar in its typed metre/radian coordinate.");
        return result;
    }
    result.ok = true;
    result.patch.adapterId = adapterId();
    result.patch.adapterVersion = adapterVersion();
    result.patch.bindingId = binding.id;
    result.patch.poseDeltaComposition = binding.poseDeltaComposition;
    result.patch.poseDeltaGroupId = binding.poseDeltaGroupId;
    result.patch.writes = {{ownTarget(binding), CandidatePatchValue::scalar(
                                                 request.values.front().engineeringValue)}};
    return result;
}

std::string BasePlacementAdapter::describeEffect(const ParameterBinding&) const
{
    return "Applies a SystemPlacement base delta in the root frame; world tasks and environment remain fixed.";
}

}    // namespace rws
