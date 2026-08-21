#include "TcpPoseAdapter.hpp"

#include <cmath>

namespace rws {
namespace {

bool isTcpSemantic(const SemanticKind semantic)
{
    return semantic == SemanticKind::TcpTx || semantic == SemanticKind::TcpTy ||
           semantic == SemanticKind::TcpTz || semantic == SemanticKind::TcpRotationVectorX ||
           semantic == SemanticKind::TcpRotationVectorY ||
           semantic == SemanticKind::TcpRotationVectorZ;
}

bool isTranslation(const SemanticKind semantic)
{
    return semantic == SemanticKind::TcpTx || semantic == SemanticKind::TcpTy ||
           semantic == SemanticKind::TcpTz;
}

int translationIndex(const SemanticKind semantic)
{
    return semantic == SemanticKind::TcpTx ? 0 : semantic == SemanticKind::TcpTy ? 1 : 2;
}

TargetPropertyId propertyFor(const SemanticKind semantic)
{
    switch (semantic) {
    case SemanticKind::TcpTx: return TargetPropertyId::FlangeToTcpTranslationX;
    case SemanticKind::TcpTy: return TargetPropertyId::FlangeToTcpTranslationY;
    case SemanticKind::TcpTz: return TargetPropertyId::FlangeToTcpTranslationZ;
    case SemanticKind::TcpRotationVectorX: return TargetPropertyId::FlangeToTcpRotationVectorX;
    case SemanticKind::TcpRotationVectorY: return TargetPropertyId::FlangeToTcpRotationVectorY;
    case SemanticKind::TcpRotationVectorZ: return TargetPropertyId::FlangeToTcpRotationVectorZ;
    default: return TargetPropertyId::Unknown;
    }
}

const ToolBinding* findToolBinding(const CanonicalKinematicModel& baseline, const std::string& id)
{
    for (const ToolBinding& tool : baseline.toolBindings)
        if (tool.id == id)
            return &tool;
    return nullptr;
}

const FrameNode* findFrame(const CanonicalKinematicModel& baseline, const std::string& id)
{
    for (const FrameNode& frame : baseline.frames)
        if (frame.id == id)
            return &frame;
    return nullptr;
}

ReadWriteTarget ownTarget(const ParameterBinding& binding)
{
    return {TargetObjectType::ToolBinding, binding.targetObjectId, propertyFor(binding.semanticKind),
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
    diagnostics.push_back(makeAdapterDiagnostic("TcpPoseAdapter", bindingId, objectId, field,
                                                code, message));
}

}    // namespace

std::string TcpPoseAdapter::adapterId() const { return "TcpPoseAdapter"; }
int TcpPoseAdapter::adapterVersion() const { return 1; }
std::vector< SemanticKind > TcpPoseAdapter::supportedSemanticKinds() const
{
    return {SemanticKind::TcpTx, SemanticKind::TcpTy, SemanticKind::TcpTz,
            SemanticKind::TcpRotationVectorX, SemanticKind::TcpRotationVectorY,
            SemanticKind::TcpRotationVectorZ};
}
std::vector< AdapterCapability > TcpPoseAdapter::requiredCapabilities() const
{
    // A free TCP motion without matching tool geometry and collision artifacts
    // would manufacture reachability, so first release blocks it safely.
    return {AdapterCapability::TcpPose, AdapterCapability::ParameterizedGeometry,
            AdapterCapability::ParameterizedCollision};
}

AdapterBindingValidationResult TcpPoseAdapter::validateBinding(
    const ParameterBinding& binding, const CanonicalKinematicModel& baseline) const
{
    AdapterBindingValidationResult result;
    const ToolBinding* tool = findToolBinding(baseline, binding.targetObjectId);
    if (!isTcpSemantic(binding.semanticKind)) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId, "semanticKind",
                 "TCP_POSE_SEMANTIC_UNSUPPORTED",
                 "TcpPoseAdapter accepts only TCP translation and rotation-vector semantics.");
    }
    const FrameNode* flange = tool == nullptr ? nullptr : findFrame(baseline, tool->flangeFrameId);
    const FrameNode* tcp = tool == nullptr ? nullptr : findFrame(baseline, tool->tcpFrameId);
    if (binding.targetObjectType != TargetObjectType::ToolBinding || tool == nullptr ||
        flange == nullptr || tcp == nullptr || flange->type != CanonicalFrameType::Flange ||
        tcp->type != CanonicalFrameType::Tool) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId, "targetObjectId",
                 "TCP_POSE_TOOL_BINDING_REQUIRED",
                 "TCP pose variables require a valid canonical Flange-to-Tool binding.");
        return result;
    }
    if (binding.coordinateFrameId != tool->flangeFrameId) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId, "coordinateFrameId",
                 "TCP_POSE_FLANGE_FRAME_REQUIRED",
                 "TCP translation must be declared in the ToolBinding flange frame.");
    }
    if (binding.targetPropertyId != propertyFor(binding.semanticKind)) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId, "targetPropertyId",
                 "TCP_POSE_PROPERTY_INVALID",
                 "TCP semantics require their matching typed Flange-to-TCP property.");
    }
    if (binding.poseDeltaGroupId != "tcp-pose:" + binding.targetObjectId) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId, "poseDeltaGroupId",
                 "TCP_POSE_GROUP_INVALID",
                 "TCP pose groups must be derived from their typed ToolBinding.");
    }
    if (binding.poseDeltaComposition != PoseDeltaComposition::Right) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId, "poseDeltaComposition",
                 "TCP_POSE_ROTATION_COMPOSITION_INVALID",
                 "TCP SO(3) deltas use the frozen right-multiplied convention only.");
    }
    const ReadWriteTarget expected = ownTarget(binding);
    if (!exactSingleTarget(binding.readSet, expected) ||
        !exactSingleTarget(binding.writeSet, expected)) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId, "readSet/writeSet",
                 "TCP_POSE_TARGET_SET_INVALID",
                 "TCP bindings must declare exactly their own typed target once.");
    }
    return result;
}

std::vector< ReadWriteTarget > TcpPoseAdapter::declaredReadSet(const ParameterBinding& binding) const
{
    return isTcpSemantic(binding.semanticKind) ? std::vector< ReadWriteTarget >{ownTarget(binding)} :
                                                  std::vector< ReadWriteTarget >();
}

std::vector< ReadWriteTarget > TcpPoseAdapter::declaredWriteSet(const ParameterBinding& binding) const
{
    return declaredReadSet(binding);
}

AdapterPatchCompileResult TcpPoseAdapter::compilePatch(
    const AdapterPatchCompileRequest& request) const
{
    AdapterPatchCompileResult result;
    if (request.baseline == nullptr || request.binding == nullptr) {
        addError(result.diagnostics, "", "", "request", "TCP_POSE_REQUEST_REQUIRED",
                 "TcpPoseAdapter requires immutable baseline and binding inputs.");
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
                 "TCP_POSE_VALUE_INVALID",
                 "TCP pose requires one finite scalar in its typed metre/radian coordinate.");
        return result;
    }
    const ToolBinding* tool = findToolBinding(*request.baseline, binding.targetObjectId);
    if (tool == nullptr) {
        addError(result.diagnostics, binding.id, binding.targetObjectId, "targetObjectId",
                 "TCP_POSE_TOOL_BINDING_REQUIRED",
                 "TCP pose variables require a valid canonical Flange-to-Tool binding.");
        return result;
    }
    double value = request.values.front().engineeringValue;
    if (isTranslation(binding.semanticKind))
        value += tool->flangeToTcp.P()(translationIndex(binding.semanticKind));
    result.ok = true;
    result.patch.adapterId = adapterId();
    result.patch.adapterVersion = adapterVersion();
    result.patch.bindingId = binding.id;
    result.patch.poseDeltaComposition = binding.poseDeltaComposition;
    result.patch.poseDeltaGroupId = binding.poseDeltaGroupId;
    result.patch.writes = {{ownTarget(binding), CandidatePatchValue::scalar(value)}};
    return result;
}

std::string TcpPoseAdapter::describeEffect(const ParameterBinding&) const
{
    return "Applies a ToolConfiguration delta to the explicit Flange-to-TCP transform with tool artifacts required.";
}

}    // namespace rws
