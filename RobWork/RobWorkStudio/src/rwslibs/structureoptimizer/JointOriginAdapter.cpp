#include "JointOriginAdapter.hpp"

#include <algorithm>
#include <cmath>

namespace rws {
namespace {

const JointEdge* findJoint(const CanonicalKinematicModel& baseline, const std::string& id)
{
    for (const JointEdge& joint : baseline.joints)
        if (joint.id == id)
            return &joint;
    return nullptr;
}

std::vector< ReadWriteTarget > translationTargets(const ParameterBinding& binding)
{
    return {{TargetObjectType::Joint, binding.targetObjectId,
             TargetPropertyId::ParentToJointTranslationX, binding.coordinateFrameId},
            {TargetObjectType::Joint, binding.targetObjectId,
             TargetPropertyId::ParentToJointTranslationY, binding.coordinateFrameId},
            {TargetObjectType::Joint, binding.targetObjectId,
             TargetPropertyId::ParentToJointTranslationZ, binding.coordinateFrameId}};
}

void addError(std::vector< StructureOptimizationDiagnostic >& diagnostics,
              const std::string& bindingId, const std::string& objectId,
              const std::string& field, const std::string& code, const std::string& message)
{
    diagnostics.push_back(makeAdapterDiagnostic("JointOriginAdapter", bindingId, objectId, field,
                                                code, message));
}

bool supports(SemanticKind kind)
{
    return kind == SemanticKind::JointOriginOffsetX || kind == SemanticKind::JointOriginOffsetY ||
           kind == SemanticKind::JointOriginOffsetZ || kind == SemanticKind::JointOffsetAlongAxis;
}

bool exactTargets(const std::vector< ReadWriteTarget >& actual,
                  const std::vector< ReadWriteTarget >& expected)
{
    if (actual.size() != expected.size())
        return false;
    for (std::size_t index = 0; index < actual.size(); ++index) {
        if (std::find(expected.begin(), expected.end(), actual[index]) == expected.end())
            return false;
        for (std::size_t other = index + 1; other < actual.size(); ++other)
            if (actual[index] == actual[other])
                return false;
    }
    return true;
}

bool isTranslationProperty(TargetPropertyId property)
{
    return property == TargetPropertyId::ParentToJointTranslationX ||
           property == TargetPropertyId::ParentToJointTranslationY ||
           property == TargetPropertyId::ParentToJointTranslationZ;
}

TargetPropertyId cartesianProperty(SemanticKind kind)
{
    if (kind == SemanticKind::JointOriginOffsetX)
        return TargetPropertyId::ParentToJointTranslationX;
    if (kind == SemanticKind::JointOriginOffsetY)
        return TargetPropertyId::ParentToJointTranslationY;
    return TargetPropertyId::ParentToJointTranslationZ;
}

bool validAxis(const rw::math::Vector3D<>& axis)
{
    return std::isfinite(axis(0)) && std::isfinite(axis(1)) && std::isfinite(axis(2)) &&
           axis.norm2() > 1e-12;
}

}    // namespace

std::string JointOriginAdapter::adapterId() const { return "JointOriginAdapter"; }
int JointOriginAdapter::adapterVersion() const { return 1; }
std::vector< SemanticKind > JointOriginAdapter::supportedSemanticKinds() const
{
    return {SemanticKind::JointOriginOffsetX, SemanticKind::JointOriginOffsetY,
            SemanticKind::JointOriginOffsetZ, SemanticKind::JointOffsetAlongAxis};
}
std::vector< AdapterCapability > JointOriginAdapter::requiredCapabilities() const
{
    return {AdapterCapability::JointOrigin};
}

AdapterBindingValidationResult JointOriginAdapter::validateBinding(
    const ParameterBinding& binding, const CanonicalKinematicModel& baseline) const
{
    AdapterBindingValidationResult result;
    const JointEdge* joint = findJoint(baseline, binding.targetObjectId);
    if (!supports(binding.semanticKind)) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId, "semanticKind",
                 "JOINT_ORIGIN_SEMANTIC_UNSUPPORTED", "JointOriginAdapter received an unsupported semantic.");
    }
    if (binding.targetObjectType != TargetObjectType::Joint || joint == nullptr ||
        joint->type == CanonicalJointType::Fixed) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId, "targetObjectId",
                 "JOINT_ORIGIN_MOVABLE_JOINT_REQUIRED", "Joint-origin offsets require a known movable joint.");
        return result;
    }
    if (binding.coordinateFrameId != joint->parentFrameId) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId, "coordinateFrameId",
                 "JOINT_ORIGIN_PARENT_FRAME_REQUIRED", "Joint-origin writes must use the target joint parent frame.");
    }
    if (binding.semanticKind == SemanticKind::JointOffsetAlongAxis) {
        if (!isTranslationProperty(binding.targetPropertyId)) {
            result.valid = false;
            addError(result.diagnostics, binding.id, binding.targetObjectId, "targetPropertyId",
                     "JOINT_ORIGIN_PRIMARY_PROPERTY_INVALID",
                     "Along-axis offsets require a parent-to-joint translation primary property.");
        }
        if (!validAxis(joint->motionAxisInJoint)) {
            result.valid = false;
            addError(result.diagnostics, binding.id, binding.targetObjectId, "motionAxisInJoint",
                     "JOINT_ORIGIN_AXIS_INVALID",
                     "Along-axis offsets require a finite non-zero baseline joint axis.");
        }
    } else if (supports(binding.semanticKind) &&
               binding.targetPropertyId != cartesianProperty(binding.semanticKind)) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId, "targetPropertyId",
                 "JOINT_ORIGIN_PRIMARY_PROPERTY_INVALID",
                 "Cartesian joint-origin semantics require their matching translation primary property.");
    }
    const std::vector< ReadWriteTarget > expected = translationTargets(binding);
    if (!exactTargets(binding.readSet, expected) || !exactTargets(binding.writeSet, expected)) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId, "readSet/writeSet",
                 "JOINT_ORIGIN_TRANSLATION_SET_INVALID",
                 "Joint-origin bindings must declare exactly X/Y/Z parent-to-joint translation targets.");
    }
    return result;
}

std::vector< ReadWriteTarget > JointOriginAdapter::declaredReadSet(const ParameterBinding& binding) const
{
    return translationTargets(binding);
}
std::vector< ReadWriteTarget > JointOriginAdapter::declaredWriteSet(const ParameterBinding& binding) const
{
    return translationTargets(binding);
}

AdapterPatchCompileResult JointOriginAdapter::compilePatch(const AdapterPatchCompileRequest& request) const
{
    AdapterPatchCompileResult result;
    if (request.baseline == nullptr || request.binding == nullptr) {
        addError(result.diagnostics, "", "", "request", "JOINT_ORIGIN_REQUEST_REQUIRED",
                 "JointOriginAdapter requires immutable baseline and binding inputs.");
        return result;
    }
    const ParameterBinding& binding = *request.binding;
    if (request.values.size() != 1 || request.values[0].unit != DesignVariableUnit::Metres ||
        !request.values[0].discreteOptionId.empty() || !std::isfinite(request.values[0].engineeringValue)) {
        addError(result.diagnostics, binding.id, binding.targetObjectId, "values",
                 "JOINT_ORIGIN_SINGLE_METRE_VALUE_REQUIRED",
                 "Joint-origin compilation requires exactly one finite scalar metre value.");
        return result;
    }
    const JointEdge* joint = findJoint(*request.baseline, binding.targetObjectId);
    if (joint == nullptr)
        return result;
    rw::math::Vector3D<> delta;
    const double value = request.values[0].engineeringValue;
    if (binding.semanticKind == SemanticKind::JointOriginOffsetX)
        delta(0) = value;
    else if (binding.semanticKind == SemanticKind::JointOriginOffsetY)
        delta(1) = value;
    else if (binding.semanticKind == SemanticKind::JointOriginOffsetZ)
        delta(2) = value;
    else if (binding.semanticKind == SemanticKind::JointOffsetAlongAxis) {
        if (!validAxis(joint->motionAxisInJoint)) {
            addError(result.diagnostics, binding.id, binding.targetObjectId, "motionAxisInJoint",
                     "JOINT_ORIGIN_AXIS_INVALID",
                     "Along-axis offsets require a finite non-zero baseline joint axis.");
            return result;
        }
        delta = joint->parentToJointZero.R() * rw::math::normalize(joint->motionAxisInJoint) * value;
    }
    else {
        addError(result.diagnostics, binding.id, binding.targetObjectId, "semanticKind",
                 "JOINT_ORIGIN_SEMANTIC_UNSUPPORTED", "JointOriginAdapter received an unsupported semantic.");
        return result;
    }
    const rw::math::Vector3D<> next = joint->parentToJointZero.P() + delta;
    result.ok = true;
    result.patch.adapterId = adapterId();
    result.patch.adapterVersion = adapterVersion();
    result.patch.bindingId = binding.id;
    const std::vector< ReadWriteTarget > targets = translationTargets(binding);
    result.patch.writes = {{targets[0], CandidatePatchValue::scalar(next(0))},
                           {targets[1], CandidatePatchValue::scalar(next(1))},
                           {targets[2], CandidatePatchValue::scalar(next(2))}};
    return result;
}

std::string JointOriginAdapter::describeEffect(const ParameterBinding&) const
{
    return "Applies a baseline-relative parent-to-joint translation offset.";
}

}    // namespace rws
