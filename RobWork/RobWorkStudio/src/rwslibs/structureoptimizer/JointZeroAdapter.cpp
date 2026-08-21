#include "JointZeroAdapter.hpp"

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

DesignVariableUnit coordinateUnit(const JointEdge& joint)
{
    return joint.type == CanonicalJointType::Prismatic ? DesignVariableUnit::Metres :
                                                        DesignVariableUnit::Radians;
}

ReadWriteTarget ownTarget(const ParameterBinding& binding)
{
    return {TargetObjectType::Joint, binding.targetObjectId,
            TargetPropertyId::ZeroPositionOffset, binding.coordinateFrameId};
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
    diagnostics.push_back(makeAdapterDiagnostic("JointZeroAdapter", bindingId, objectId, field,
                                                code, message));
}

}    // namespace

std::string JointZeroAdapter::adapterId() const { return "JointZeroAdapter"; }
int JointZeroAdapter::adapterVersion() const { return 1; }
std::vector< SemanticKind > JointZeroAdapter::supportedSemanticKinds() const
{
    return {SemanticKind::JointZeroOffset};
}
std::vector< AdapterCapability > JointZeroAdapter::requiredCapabilities() const
{
    return {AdapterCapability::JointZeroOffset};
}

AdapterBindingValidationResult JointZeroAdapter::validateBinding(
    const ParameterBinding& binding, const CanonicalKinematicModel& baseline) const
{
    AdapterBindingValidationResult result;
    const JointEdge* const joint = findJoint(baseline, binding.targetObjectId);
    if (binding.semanticKind != SemanticKind::JointZeroOffset) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId, "semanticKind",
                 "JOINT_ZERO_SEMANTIC_UNSUPPORTED",
                 "JointZeroAdapter supports the JointZeroOffset semantic only.");
    }
    if (binding.targetObjectType != TargetObjectType::Joint || joint == nullptr ||
        joint->type == CanonicalJointType::Fixed) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId, "targetObjectId",
                 "JOINT_ZERO_MOVABLE_JOINT_REQUIRED",
                 "Joint zero offsets require a known revolute or prismatic joint.");
        return result;
    }
    if (binding.coordinateFrameId != joint->parentFrameId) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId, "coordinateFrameId",
                 "JOINT_ZERO_PARENT_FRAME_REQUIRED",
                 "Joint-zero bindings must name the target joint parent frame.");
    }
    if (binding.targetPropertyId != TargetPropertyId::ZeroPositionOffset) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId, "targetPropertyId",
                 "JOINT_ZERO_PROPERTY_INVALID",
                 "JointZeroOffset requires the typed ZeroPositionOffset target.");
    }
    const ReadWriteTarget expected = ownTarget(binding);
    if (!exactSingleTarget(binding.readSet, expected) ||
        !exactSingleTarget(binding.writeSet, expected)) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId, "readSet/writeSet",
                 "JOINT_ZERO_TARGET_SET_INVALID",
                 "Joint-zero bindings must declare exactly their own typed target once.");
    }
    return result;
}

std::vector< ReadWriteTarget > JointZeroAdapter::declaredReadSet(
    const ParameterBinding& binding) const
{
    return binding.semanticKind == SemanticKind::JointZeroOffset ?
        std::vector< ReadWriteTarget >{ownTarget(binding)} : std::vector< ReadWriteTarget >();
}

std::vector< ReadWriteTarget > JointZeroAdapter::declaredWriteSet(
    const ParameterBinding& binding) const
{
    return declaredReadSet(binding);
}

AdapterPatchCompileResult JointZeroAdapter::compilePatch(
    const AdapterPatchCompileRequest& request) const
{
    AdapterPatchCompileResult result;
    if (request.baseline == nullptr || request.binding == nullptr) {
        addError(result.diagnostics, "", "", "request", "JOINT_ZERO_REQUEST_REQUIRED",
                 "JointZeroAdapter requires immutable baseline and binding inputs.");
        return result;
    }
    const ParameterBinding& binding = *request.binding;
    const AdapterBindingValidationResult bindingValidation =
        validateBinding(binding, *request.baseline);
    if (!bindingValidation.valid) {
        result.diagnostics = bindingValidation.diagnostics;
        return result;
    }
    const JointEdge* const joint = findJoint(*request.baseline, binding.targetObjectId);
    if (joint == nullptr || joint->type == CanonicalJointType::Fixed) {
        addError(result.diagnostics, binding.id, binding.targetObjectId, "targetObjectId",
                 "JOINT_ZERO_MOVABLE_JOINT_REQUIRED",
                 "Joint zero offsets require a known revolute or prismatic joint.");
        return result;
    }
    if (request.values.size() != 1) {
        addError(result.diagnostics, binding.id, binding.targetObjectId, "values",
                 "JOINT_ZERO_VALUE_COUNT_INVALID",
                 "Joint zero offsets require exactly one scalar coordinate value.");
        return result;
    }
    const ResolvedAdapterValue& value = request.values.front();
    if (value.semanticKind != SemanticKind::JointZeroOffset || !value.groupId.empty() ||
        !value.discreteOptionId.empty() || !std::isfinite(value.engineeringValue)) {
        addError(result.diagnostics, binding.id, binding.targetObjectId, "values",
                 "JOINT_ZERO_VALUE_INVALID",
                 "Joint zero offsets require one finite non-discrete coordinate value.");
        return result;
    }
    if (value.unit != coordinateUnit(*joint)) {
        addError(result.diagnostics, binding.id, binding.targetObjectId, "values.unit",
                 "JOINT_ZERO_VALUE_UNIT_INVALID",
                 "Revolute zero offsets use radians and prismatic zero offsets use metres.");
        return result;
    }
    result.ok = true;
    result.patch.adapterId = adapterId();
    result.patch.adapterVersion = adapterVersion();
    result.patch.bindingId = binding.id;
    result.patch.writes = {{ownTarget(binding), CandidatePatchValue::scalar(value.engineeringValue)}};
    return result;
}

std::string JointZeroAdapter::describeEffect(const ParameterBinding&) const
{
    return "Sets q_model = q_input + zeroPositionOffset without changing the joint axis or mount transform.";
}

}    // namespace rws
