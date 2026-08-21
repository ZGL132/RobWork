#include "FlangePoseAdapter.hpp"

#include <algorithm>
#include <cmath>

namespace rws {
namespace {

bool isFlangeSemantic(const SemanticKind semantic)
{
    return semantic == SemanticKind::FlangeTx || semantic == SemanticKind::FlangeTy ||
           semantic == SemanticKind::FlangeTz || semantic == SemanticKind::FlangeRotationVectorX ||
           semantic == SemanticKind::FlangeRotationVectorY ||
           semantic == SemanticKind::FlangeRotationVectorZ;
}

bool isTranslation(const SemanticKind semantic)
{
    return semantic == SemanticKind::FlangeTx || semantic == SemanticKind::FlangeTy ||
           semantic == SemanticKind::FlangeTz;
}

int translationIndex(const SemanticKind semantic)
{
    return semantic == SemanticKind::FlangeTx ? 0 : semantic == SemanticKind::FlangeTy ? 1 : 2;
}

TargetPropertyId propertyFor(const SemanticKind semantic)
{
    switch (semantic) {
    case SemanticKind::FlangeTx: return TargetPropertyId::ParentToFlangeTranslationX;
    case SemanticKind::FlangeTy: return TargetPropertyId::ParentToFlangeTranslationY;
    case SemanticKind::FlangeTz: return TargetPropertyId::ParentToFlangeTranslationZ;
    case SemanticKind::FlangeRotationVectorX: return TargetPropertyId::ParentToFlangeRotationVectorX;
    case SemanticKind::FlangeRotationVectorY: return TargetPropertyId::ParentToFlangeRotationVectorY;
    case SemanticKind::FlangeRotationVectorZ: return TargetPropertyId::ParentToFlangeRotationVectorZ;
    default: return TargetPropertyId::Unknown;
    }
}

const FrameNode* findFrame(const CanonicalKinematicModel& baseline, const std::string& id)
{
    for (const FrameNode& frame : baseline.frames)
        if (frame.id == id)
            return &frame;
    return nullptr;
}

const JointEdge* independentFlangeMount(const CanonicalKinematicModel& baseline,
                                        const std::string& flangeId)
{
    const JointEdge* mount = nullptr;
    std::size_t incomingCount = 0;
    for (const JointEdge& joint : baseline.joints) {
        if (joint.childFrameId != flangeId)
            continue;
        ++incomingCount;
        mount = &joint;
    }
    if (incomingCount != 1 || mount == nullptr || mount->type != CanonicalJointType::Fixed)
        return nullptr;
    for (const DeviceChain& chain : baseline.deviceChains) {
        if (chain.id != baseline.activeDeviceChainId)
            continue;
        return std::find(chain.orderedJointIds.begin(), chain.orderedJointIds.end(), mount->id) !=
                chain.orderedJointIds.end() ?
            mount : nullptr;
    }
    return nullptr;
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
    diagnostics.push_back(makeAdapterDiagnostic("FlangePoseAdapter", bindingId, objectId, field,
                                                code, message));
}

}    // namespace

std::string FlangePoseAdapter::adapterId() const { return "FlangePoseAdapter"; }
int FlangePoseAdapter::adapterVersion() const { return 1; }
std::vector< SemanticKind > FlangePoseAdapter::supportedSemanticKinds() const
{
    return {SemanticKind::FlangeTx, SemanticKind::FlangeTy, SemanticKind::FlangeTz,
            SemanticKind::FlangeRotationVectorX, SemanticKind::FlangeRotationVectorY,
            SemanticKind::FlangeRotationVectorZ};
}
std::vector< AdapterCapability > FlangePoseAdapter::requiredCapabilities() const
{
    return {AdapterCapability::FlangePose};
}

AdapterBindingValidationResult FlangePoseAdapter::validateBinding(
    const ParameterBinding& binding, const CanonicalKinematicModel& baseline) const
{
    AdapterBindingValidationResult result;
    const FrameNode* frame = findFrame(baseline, binding.targetObjectId);
    const JointEdge* mount = independentFlangeMount(baseline, binding.targetObjectId);
    if (!isFlangeSemantic(binding.semanticKind)) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId, "semanticKind",
                 "FLANGE_POSE_SEMANTIC_UNSUPPORTED",
                 "FlangePoseAdapter accepts only Flange translation and rotation-vector semantics.");
    }
    if (binding.targetObjectType != TargetObjectType::Frame || frame == nullptr ||
        frame->type != CanonicalFrameType::Flange || mount == nullptr) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId, "targetObjectId",
                 "FLANGE_POSE_INDEPENDENT_FLANGE_REQUIRED",
                 "Flange variables require a known Flange frame with exactly one fixed independent mount.");
        return result;
    }
    if (binding.coordinateFrameId != mount->parentFrameId) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId, "coordinateFrameId",
                 "FLANGE_POSE_PARENT_FRAME_REQUIRED",
                 "Flange installation translation must be declared in its fixed mount parent frame.");
    }
    if (binding.targetPropertyId != propertyFor(binding.semanticKind)) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId, "targetPropertyId",
                 "FLANGE_POSE_PROPERTY_INVALID",
                 "Flange semantics require their matching typed installation-transform property.");
    }
    if (binding.poseDeltaGroupId != "flange-pose:" + binding.targetObjectId) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId, "poseDeltaGroupId",
                 "FLANGE_POSE_GROUP_INVALID",
                 "Flange pose groups must be derived from their typed flange frame.");
    }
    if (binding.poseDeltaComposition != PoseDeltaComposition::Right) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId, "poseDeltaComposition",
                 "FLANGE_POSE_ROTATION_COMPOSITION_INVALID",
                 "Flange SO(3) deltas use the frozen right-multiplied convention only.");
    }
    const ReadWriteTarget expected = ownTarget(binding);
    if (!exactSingleTarget(binding.readSet, expected) ||
        !exactSingleTarget(binding.writeSet, expected)) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId, "readSet/writeSet",
                 "FLANGE_POSE_TARGET_SET_INVALID",
                 "Flange bindings must declare exactly their own typed target once.");
    }
    return result;
}

std::vector< ReadWriteTarget > FlangePoseAdapter::declaredReadSet(
    const ParameterBinding& binding) const
{
    return isFlangeSemantic(binding.semanticKind) ? std::vector< ReadWriteTarget >{ownTarget(binding)} :
                                                     std::vector< ReadWriteTarget >();
}

std::vector< ReadWriteTarget > FlangePoseAdapter::declaredWriteSet(
    const ParameterBinding& binding) const
{
    return declaredReadSet(binding);
}

AdapterPatchCompileResult FlangePoseAdapter::compilePatch(
    const AdapterPatchCompileRequest& request) const
{
    AdapterPatchCompileResult result;
    if (request.baseline == nullptr || request.binding == nullptr) {
        addError(result.diagnostics, "", "", "request", "FLANGE_POSE_REQUEST_REQUIRED",
                 "FlangePoseAdapter requires immutable baseline and binding inputs.");
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
                 "FLANGE_POSE_VALUE_INVALID",
                 "Flange pose requires one finite scalar in its typed metre/radian coordinate.");
        return result;
    }
    const JointEdge* mount = independentFlangeMount(*request.baseline, binding.targetObjectId);
    if (mount == nullptr) {
        addError(result.diagnostics, binding.id, binding.targetObjectId, "targetObjectId",
                 "FLANGE_POSE_INDEPENDENT_FLANGE_REQUIRED",
                 "Flange variables require a known Flange frame with exactly one fixed independent mount.");
        return result;
    }
    double value = request.values.front().engineeringValue;
    if (isTranslation(binding.semanticKind))
        value += mount->parentToJointZero.P()(translationIndex(binding.semanticKind));
    result.ok = true;
    result.patch.adapterId = adapterId();
    result.patch.adapterVersion = adapterVersion();
    result.patch.bindingId = binding.id;
    result.patch.poseDeltaComposition = binding.poseDeltaComposition;
    result.patch.poseDeltaGroupId = binding.poseDeltaGroupId;
    result.patch.writes = {{ownTarget(binding), CandidatePatchValue::scalar(value)}};
    return result;
}

std::string FlangePoseAdapter::describeEffect(const ParameterBinding&) const
{
    return "Applies an independent fixed-mount flange pose delta in its declared parent frame.";
}

}    // namespace rws
