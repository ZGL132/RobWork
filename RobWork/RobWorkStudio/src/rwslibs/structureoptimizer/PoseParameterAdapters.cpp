// -----------------------------------------------------------------------------
//  PoseParameterAdapters.cpp - BasePlacement / FlangePose / TcpPose
//  Merged from three translation units; every adapter keeps its original
//  namespace, method definitions, registry IDs and diagnostics codes.  The
//  identical exactSingleTarget and findFrame are kept once; the colliding
//  propertyFor / isTranslation / translationIndex / ownTarget / addError
//  helpers carry an adapter prefix.  Logic and validation order are
//  line-identical to the pre-merge files.
// -----------------------------------------------------------------------------
#include "BasePlacementAdapter.hpp"
#include "FlangePoseAdapter.hpp"
#include "TcpPoseAdapter.hpp"

#include <algorithm>
#include <cmath>

namespace rws {
namespace {

// ---- shared: identical across the three pose adapters ----
bool exactSingleTarget(const std::vector< ReadWriteTarget >& targets,
                       const ReadWriteTarget& expected)
{
    return targets.size() == 1 && targets.front() == expected;
}

const FrameNode* findFrame(const CanonicalKinematicModel& baseline, const std::string& id)
{
    for (const FrameNode& frame : baseline.frames)
        if (frame.id == id)
            return &frame;
    return nullptr;
}


bool isBaseSemantic(const SemanticKind semantic)
{
    return semantic == SemanticKind::BaseTx || semantic == SemanticKind::BaseTy ||
           semantic == SemanticKind::BaseTz || semantic == SemanticKind::BaseRotationVectorX ||
           semantic == SemanticKind::BaseRotationVectorY ||
           semantic == SemanticKind::BaseRotationVectorZ;
}

TargetPropertyId basePlacement_propertyFor(const SemanticKind semantic)
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

bool basePlacement_isTranslation(const SemanticKind semantic)
{
    return semantic == SemanticKind::BaseTx || semantic == SemanticKind::BaseTy ||
           semantic == SemanticKind::BaseTz;
}

ReadWriteTarget basePlacement_ownTarget(const ParameterBinding& binding)
{
    return {TargetObjectType::Frame, binding.targetObjectId, basePlacement_propertyFor(binding.semanticKind),
            binding.coordinateFrameId};
}

void basePlacement_addError(std::vector< StructureOptimizationDiagnostic >& diagnostics,
              const std::string& bindingId, const std::string& objectId,
              const std::string& field, const std::string& code, const std::string& message)
{
    diagnostics.push_back(makeAdapterDiagnostic("BasePlacementAdapter", bindingId, objectId, field,
                                                code, message));
}



bool isTcpSemantic(const SemanticKind semantic)
{
    return semantic == SemanticKind::TcpTx || semantic == SemanticKind::TcpTy ||
           semantic == SemanticKind::TcpTz || semantic == SemanticKind::TcpRotationVectorX ||
           semantic == SemanticKind::TcpRotationVectorY ||
           semantic == SemanticKind::TcpRotationVectorZ;
}

bool tcpPose_isTranslation(const SemanticKind semantic)
{
    return semantic == SemanticKind::TcpTx || semantic == SemanticKind::TcpTy ||
           semantic == SemanticKind::TcpTz;
}

int tcpPose_translationIndex(const SemanticKind semantic)
{
    return semantic == SemanticKind::TcpTx ? 0 : semantic == SemanticKind::TcpTy ? 1 : 2;
}

TargetPropertyId tcpPose_propertyFor(const SemanticKind semantic)
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

ReadWriteTarget tcpPose_ownTarget(const ParameterBinding& binding)
{
    return {TargetObjectType::ToolBinding, binding.targetObjectId, tcpPose_propertyFor(binding.semanticKind),
            binding.coordinateFrameId};
}

void tcpPose_addError(std::vector< StructureOptimizationDiagnostic >& diagnostics,
              const std::string& bindingId, const std::string& objectId,
              const std::string& field, const std::string& code, const std::string& message)
{
    diagnostics.push_back(makeAdapterDiagnostic("TcpPoseAdapter", bindingId, objectId, field,
                                                code, message));
}



bool isFlangeSemantic(const SemanticKind semantic)
{
    return semantic == SemanticKind::FlangeTx || semantic == SemanticKind::FlangeTy ||
           semantic == SemanticKind::FlangeTz || semantic == SemanticKind::FlangeRotationVectorX ||
           semantic == SemanticKind::FlangeRotationVectorY ||
           semantic == SemanticKind::FlangeRotationVectorZ;
}

bool flangePose_isTranslation(const SemanticKind semantic)
{
    return semantic == SemanticKind::FlangeTx || semantic == SemanticKind::FlangeTy ||
           semantic == SemanticKind::FlangeTz;
}

int flangePose_translationIndex(const SemanticKind semantic)
{
    return semantic == SemanticKind::FlangeTx ? 0 : semantic == SemanticKind::FlangeTy ? 1 : 2;
}

TargetPropertyId flangePose_propertyFor(const SemanticKind semantic)
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

ReadWriteTarget flangePose_ownTarget(const ParameterBinding& binding)
{
    return {TargetObjectType::Frame, binding.targetObjectId, flangePose_propertyFor(binding.semanticKind),
            binding.coordinateFrameId};
}

void flangePose_addError(std::vector< StructureOptimizationDiagnostic >& diagnostics,
              const std::string& bindingId, const std::string& objectId,
              const std::string& field, const std::string& code, const std::string& message)
{
    diagnostics.push_back(makeAdapterDiagnostic("FlangePoseAdapter", bindingId, objectId, field,
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
        basePlacement_addError(result.diagnostics, binding.id, binding.targetObjectId, "semanticKind",
                 "BASE_PLACEMENT_SEMANTIC_UNSUPPORTED",
                 "BasePlacementAdapter accepts only Base translation and rotation-vector semantics.");
    }
    if (binding.targetObjectType != TargetObjectType::Frame ||
        binding.targetObjectId != baseline.baseFrameId) {
        result.valid = false;
        basePlacement_addError(result.diagnostics, binding.id, binding.targetObjectId, "targetObjectId",
                 "BASE_PLACEMENT_BASE_FRAME_REQUIRED",
                 "Base placement must target the canonical base frame only.");
    }
    if (binding.coordinateFrameId != baseline.rootFrameId) {
        result.valid = false;
        basePlacement_addError(result.diagnostics, binding.id, binding.targetObjectId, "coordinateFrameId",
                 "BASE_PLACEMENT_ROOT_FRAME_REQUIRED",
                 "Base translation must be declared in the immutable system root frame.");
    }
    if (binding.targetPropertyId != basePlacement_propertyFor(binding.semanticKind)) {
        result.valid = false;
        basePlacement_addError(result.diagnostics, binding.id, binding.targetObjectId, "targetPropertyId",
                 "BASE_PLACEMENT_PROPERTY_INVALID",
                 "Base semantics require their matching typed placement property.");
    }
    if (binding.poseDeltaGroupId != "base-pose:" + baseline.baseFrameId) {
        result.valid = false;
        basePlacement_addError(result.diagnostics, binding.id, binding.targetObjectId, "poseDeltaGroupId",
                 "BASE_PLACEMENT_GROUP_INVALID",
                 "Base placement groups must be derived from the canonical base frame.");
    }
    if (binding.poseDeltaComposition != PoseDeltaComposition::Right) {
        result.valid = false;
        basePlacement_addError(result.diagnostics, binding.id, binding.targetObjectId, "poseDeltaComposition",
                 "BASE_PLACEMENT_ROTATION_COMPOSITION_INVALID",
                 "Base SO(3) deltas use the frozen right-multiplied convention only.");
    }
    const ReadWriteTarget expected = basePlacement_ownTarget(binding);
    if (!exactSingleTarget(binding.readSet, expected) ||
        !exactSingleTarget(binding.writeSet, expected)) {
        result.valid = false;
        basePlacement_addError(result.diagnostics, binding.id, binding.targetObjectId, "readSet/writeSet",
                 "BASE_PLACEMENT_TARGET_SET_INVALID",
                 "Base placement bindings must declare exactly their own typed target once.");
    }
    return result;
}

std::vector< ReadWriteTarget > BasePlacementAdapter::declaredReadSet(
    const ParameterBinding& binding) const
{
    return isBaseSemantic(binding.semanticKind) ? std::vector< ReadWriteTarget >{basePlacement_ownTarget(binding)} :
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
        basePlacement_addError(result.diagnostics, "", "", "request", "BASE_PLACEMENT_REQUEST_REQUIRED",
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
        request.values.front().unit != (basePlacement_isTranslation(binding.semanticKind) ?
                                           DesignVariableUnit::Metres : DesignVariableUnit::Radians)) {
        basePlacement_addError(result.diagnostics, binding.id, binding.targetObjectId, "values",
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
    result.patch.writes = {{basePlacement_ownTarget(binding), CandidatePatchValue::scalar(
                                                 request.values.front().engineeringValue)}};
    return result;
}

std::string BasePlacementAdapter::describeEffect(const ParameterBinding&) const
{
    return "Applies a SystemPlacement base delta in the root frame; world tasks and environment remain fixed.";
}



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
        tcpPose_addError(result.diagnostics, binding.id, binding.targetObjectId, "semanticKind",
                 "TCP_POSE_SEMANTIC_UNSUPPORTED",
                 "TcpPoseAdapter accepts only TCP translation and rotation-vector semantics.");
    }
    const FrameNode* flange = tool == nullptr ? nullptr : findFrame(baseline, tool->flangeFrameId);
    const FrameNode* tcp = tool == nullptr ? nullptr : findFrame(baseline, tool->tcpFrameId);
    if (binding.targetObjectType != TargetObjectType::ToolBinding || tool == nullptr ||
        flange == nullptr || tcp == nullptr || flange->type != CanonicalFrameType::Flange ||
        tcp->type != CanonicalFrameType::Tool) {
        result.valid = false;
        tcpPose_addError(result.diagnostics, binding.id, binding.targetObjectId, "targetObjectId",
                 "TCP_POSE_TOOL_BINDING_REQUIRED",
                 "TCP pose variables require a valid canonical Flange-to-Tool binding.");
        return result;
    }
    if (binding.coordinateFrameId != tool->flangeFrameId) {
        result.valid = false;
        tcpPose_addError(result.diagnostics, binding.id, binding.targetObjectId, "coordinateFrameId",
                 "TCP_POSE_FLANGE_FRAME_REQUIRED",
                 "TCP translation must be declared in the ToolBinding flange frame.");
    }
    if (binding.targetPropertyId != tcpPose_propertyFor(binding.semanticKind)) {
        result.valid = false;
        tcpPose_addError(result.diagnostics, binding.id, binding.targetObjectId, "targetPropertyId",
                 "TCP_POSE_PROPERTY_INVALID",
                 "TCP semantics require their matching typed Flange-to-TCP property.");
    }
    if (binding.poseDeltaGroupId != "tcp-pose:" + binding.targetObjectId) {
        result.valid = false;
        tcpPose_addError(result.diagnostics, binding.id, binding.targetObjectId, "poseDeltaGroupId",
                 "TCP_POSE_GROUP_INVALID",
                 "TCP pose groups must be derived from their typed ToolBinding.");
    }
    if (binding.poseDeltaComposition != PoseDeltaComposition::Right) {
        result.valid = false;
        tcpPose_addError(result.diagnostics, binding.id, binding.targetObjectId, "poseDeltaComposition",
                 "TCP_POSE_ROTATION_COMPOSITION_INVALID",
                 "TCP SO(3) deltas use the frozen right-multiplied convention only.");
    }
    const ReadWriteTarget expected = tcpPose_ownTarget(binding);
    if (!exactSingleTarget(binding.readSet, expected) ||
        !exactSingleTarget(binding.writeSet, expected)) {
        result.valid = false;
        tcpPose_addError(result.diagnostics, binding.id, binding.targetObjectId, "readSet/writeSet",
                 "TCP_POSE_TARGET_SET_INVALID",
                 "TCP bindings must declare exactly their own typed target once.");
    }
    return result;
}

std::vector< ReadWriteTarget > TcpPoseAdapter::declaredReadSet(const ParameterBinding& binding) const
{
    return isTcpSemantic(binding.semanticKind) ? std::vector< ReadWriteTarget >{tcpPose_ownTarget(binding)} :
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
        tcpPose_addError(result.diagnostics, "", "", "request", "TCP_POSE_REQUEST_REQUIRED",
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
        request.values.front().unit != (tcpPose_isTranslation(binding.semanticKind) ?
                                           DesignVariableUnit::Metres : DesignVariableUnit::Radians)) {
        tcpPose_addError(result.diagnostics, binding.id, binding.targetObjectId, "values",
                 "TCP_POSE_VALUE_INVALID",
                 "TCP pose requires one finite scalar in its typed metre/radian coordinate.");
        return result;
    }
    const ToolBinding* tool = findToolBinding(*request.baseline, binding.targetObjectId);
    if (tool == nullptr) {
        tcpPose_addError(result.diagnostics, binding.id, binding.targetObjectId, "targetObjectId",
                 "TCP_POSE_TOOL_BINDING_REQUIRED",
                 "TCP pose variables require a valid canonical Flange-to-Tool binding.");
        return result;
    }
    double value = request.values.front().engineeringValue;
    if (tcpPose_isTranslation(binding.semanticKind))
        value += tool->flangeToTcp.P()(tcpPose_translationIndex(binding.semanticKind));
    result.ok = true;
    result.patch.adapterId = adapterId();
    result.patch.adapterVersion = adapterVersion();
    result.patch.bindingId = binding.id;
    result.patch.poseDeltaComposition = binding.poseDeltaComposition;
    result.patch.poseDeltaGroupId = binding.poseDeltaGroupId;
    result.patch.writes = {{tcpPose_ownTarget(binding), CandidatePatchValue::scalar(value)}};
    return result;
}

std::string TcpPoseAdapter::describeEffect(const ParameterBinding&) const
{
    return "Applies a ToolConfiguration delta to the explicit Flange-to-TCP transform with tool artifacts required.";
}



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
        flangePose_addError(result.diagnostics, binding.id, binding.targetObjectId, "semanticKind",
                 "FLANGE_POSE_SEMANTIC_UNSUPPORTED",
                 "FlangePoseAdapter accepts only Flange translation and rotation-vector semantics.");
    }
    if (binding.targetObjectType != TargetObjectType::Frame || frame == nullptr ||
        frame->type != CanonicalFrameType::Flange || mount == nullptr) {
        result.valid = false;
        flangePose_addError(result.diagnostics, binding.id, binding.targetObjectId, "targetObjectId",
                 "FLANGE_POSE_INDEPENDENT_FLANGE_REQUIRED",
                 "Flange variables require a known Flange frame with exactly one fixed independent mount.");
        return result;
    }
    if (binding.coordinateFrameId != mount->parentFrameId) {
        result.valid = false;
        flangePose_addError(result.diagnostics, binding.id, binding.targetObjectId, "coordinateFrameId",
                 "FLANGE_POSE_PARENT_FRAME_REQUIRED",
                 "Flange installation translation must be declared in its fixed mount parent frame.");
    }
    if (binding.targetPropertyId != flangePose_propertyFor(binding.semanticKind)) {
        result.valid = false;
        flangePose_addError(result.diagnostics, binding.id, binding.targetObjectId, "targetPropertyId",
                 "FLANGE_POSE_PROPERTY_INVALID",
                 "Flange semantics require their matching typed installation-transform property.");
    }
    if (binding.poseDeltaGroupId != "flange-pose:" + binding.targetObjectId) {
        result.valid = false;
        flangePose_addError(result.diagnostics, binding.id, binding.targetObjectId, "poseDeltaGroupId",
                 "FLANGE_POSE_GROUP_INVALID",
                 "Flange pose groups must be derived from their typed flange frame.");
    }
    if (binding.poseDeltaComposition != PoseDeltaComposition::Right) {
        result.valid = false;
        flangePose_addError(result.diagnostics, binding.id, binding.targetObjectId, "poseDeltaComposition",
                 "FLANGE_POSE_ROTATION_COMPOSITION_INVALID",
                 "Flange SO(3) deltas use the frozen right-multiplied convention only.");
    }
    const ReadWriteTarget expected = flangePose_ownTarget(binding);
    if (!exactSingleTarget(binding.readSet, expected) ||
        !exactSingleTarget(binding.writeSet, expected)) {
        result.valid = false;
        flangePose_addError(result.diagnostics, binding.id, binding.targetObjectId, "readSet/writeSet",
                 "FLANGE_POSE_TARGET_SET_INVALID",
                 "Flange bindings must declare exactly their own typed target once.");
    }
    return result;
}

std::vector< ReadWriteTarget > FlangePoseAdapter::declaredReadSet(
    const ParameterBinding& binding) const
{
    return isFlangeSemantic(binding.semanticKind) ? std::vector< ReadWriteTarget >{flangePose_ownTarget(binding)} :
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
        flangePose_addError(result.diagnostics, "", "", "request", "FLANGE_POSE_REQUEST_REQUIRED",
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
        request.values.front().unit != (flangePose_isTranslation(binding.semanticKind) ?
                                           DesignVariableUnit::Metres : DesignVariableUnit::Radians)) {
        flangePose_addError(result.diagnostics, binding.id, binding.targetObjectId, "values",
                 "FLANGE_POSE_VALUE_INVALID",
                 "Flange pose requires one finite scalar in its typed metre/radian coordinate.");
        return result;
    }
    const JointEdge* mount = independentFlangeMount(*request.baseline, binding.targetObjectId);
    if (mount == nullptr) {
        flangePose_addError(result.diagnostics, binding.id, binding.targetObjectId, "targetObjectId",
                 "FLANGE_POSE_INDEPENDENT_FLANGE_REQUIRED",
                 "Flange variables require a known Flange frame with exactly one fixed independent mount.");
        return result;
    }
    double value = request.values.front().engineeringValue;
    if (flangePose_isTranslation(binding.semanticKind))
        value += mount->parentToJointZero.P()(flangePose_translationIndex(binding.semanticKind));
    result.ok = true;
    result.patch.adapterId = adapterId();
    result.patch.adapterVersion = adapterVersion();
    result.patch.bindingId = binding.id;
    result.patch.poseDeltaComposition = binding.poseDeltaComposition;
    result.patch.poseDeltaGroupId = binding.poseDeltaGroupId;
    result.patch.writes = {{flangePose_ownTarget(binding), CandidatePatchValue::scalar(value)}};
    return result;
}

std::string FlangePoseAdapter::describeEffect(const ParameterBinding&) const
{
    return "Applies an independent fixed-mount flange pose delta in its declared parent frame.";
}

}    // namespace rws
