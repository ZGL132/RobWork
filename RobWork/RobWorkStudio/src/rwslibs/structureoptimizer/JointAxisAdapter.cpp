#include "JointAxisAdapter.hpp"

#include "KinematicConventions.hpp"

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

bool isTiltSemantic(const SemanticKind semantic)
{
    return semantic == SemanticKind::JointAxisTiltU || semantic == SemanticKind::JointAxisTiltV;
}

TargetPropertyId propertyFor(const SemanticKind semantic)
{
    return semantic == SemanticKind::JointAxisTiltU ? TargetPropertyId::MotionAxisTiltU :
                                                      TargetPropertyId::MotionAxisTiltV;
}

ReadWriteTarget ownTarget(const ParameterBinding& binding)
{
    return {TargetObjectType::Joint, binding.targetObjectId, propertyFor(binding.semanticKind),
            binding.coordinateFrameId};
}

bool exactSingleTarget(const std::vector< ReadWriteTarget >& actual,
                       const ReadWriteTarget& expected)
{
    return actual.size() == 1 && actual.front() == expected;
}

bool validAxis(const rw::math::Vector3D<>& axis)
{
    return std::isfinite(axis(0)) && std::isfinite(axis(1)) && std::isfinite(axis(2)) &&
           axis.norm2() > 1e-12;
}

void addError(std::vector< StructureOptimizationDiagnostic >& diagnostics,
              const std::string& bindingId, const std::string& objectId,
              const std::string& field, const std::string& code, const std::string& message)
{
    diagnostics.push_back(makeAdapterDiagnostic("JointAxisAdapter", bindingId, objectId, field,
                                                code, message));
}

bool readGroupValues(const std::vector< ResolvedAdapterValue >& values,
                     const ParameterBinding& binding, double& alpha, double& beta,
                     std::vector< StructureOptimizationDiagnostic >& diagnostics)
{
    bool haveU = false;
    bool haveV = false;
    alpha = 0.0;
    beta = 0.0;
    for (const ResolvedAdapterValue& value : values) {
        if (!isTiltSemantic(value.semanticKind)) {
            addError(diagnostics, binding.id, binding.targetObjectId, "values.semanticKind",
                     "JOINT_AXIS_TILT_GROUP_SEMANTIC_INVALID",
                     "Joint-axis tilt groups accept concrete U/V semantics only.");
            return false;
        }
        if (value.groupId != binding.axisTiltGroupId) {
            addError(diagnostics, binding.id, binding.targetObjectId, "values.groupId",
                     "JOINT_AXIS_TILT_GROUP_MISMATCH",
                     "Joint-axis tilt values must belong to the binding's explicit U/V group.");
            return false;
        }
        if (value.unit != DesignVariableUnit::Radians || !value.discreteOptionId.empty() ||
            !std::isfinite(value.engineeringValue)) {
            addError(diagnostics, binding.id, binding.targetObjectId, "values",
                     "JOINT_AXIS_TILT_GROUP_VALUE_INVALID",
                     "Joint-axis tilt groups require finite scalar radian values.");
            return false;
        }
        if (value.semanticKind == SemanticKind::JointAxisTiltU) {
            if (haveU) {
                addError(diagnostics, binding.id, binding.targetObjectId, "values",
                         "JOINT_AXIS_TILT_GROUP_VALUE_DUPLICATE",
                         "Joint-axis tilt groups may contain U at most once.");
                return false;
            }
            haveU = true;
            alpha = value.engineeringValue;
        } else {
            if (haveV) {
                addError(diagnostics, binding.id, binding.targetObjectId, "values",
                         "JOINT_AXIS_TILT_GROUP_VALUE_DUPLICATE",
                         "Joint-axis tilt groups may contain V at most once.");
                return false;
            }
            haveV = true;
            beta = value.engineeringValue;
        }
    }
    if (!haveU || !haveV) {
        addError(diagnostics, binding.id, binding.targetObjectId, "values",
                 "JOINT_AXIS_TILT_GROUP_VALUE_REQUIRED",
                 "Joint-axis tilt compilation requires exactly one U and one V value for its group.");
        return false;
    }
    return true;
}

}    // namespace

std::string JointAxisAdapter::adapterId() const { return "JointAxisAdapter"; }
int JointAxisAdapter::adapterVersion() const { return 1; }
std::vector< SemanticKind > JointAxisAdapter::supportedSemanticKinds() const
{
    return {SemanticKind::JointAxisTiltU, SemanticKind::JointAxisTiltV};
}
std::vector< AdapterCapability > JointAxisAdapter::requiredCapabilities() const
{
    return {AdapterCapability::JointAxisTilt};
}

AdapterBindingValidationResult JointAxisAdapter::validateBinding(
    const ParameterBinding& binding, const CanonicalKinematicModel& baseline) const
{
    AdapterBindingValidationResult result;
    const JointEdge* joint = findJoint(baseline, binding.targetObjectId);
    if (!isTiltSemantic(binding.semanticKind)) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId, "semanticKind",
                 "JOINT_AXIS_SEMANTIC_UNSUPPORTED",
                 "JointAxisAdapter supports tangent-coordinate U/V semantics only.");
    }
    if (binding.targetObjectType != TargetObjectType::Joint || joint == nullptr ||
        joint->type == CanonicalJointType::Fixed) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId, "targetObjectId",
                 "JOINT_AXIS_MOVABLE_JOINT_REQUIRED",
                 "Joint-axis tilt requires a known revolute or prismatic joint.");
        return result;
    }
    if (binding.coordinateFrameId != joint->parentFrameId) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId, "coordinateFrameId",
                 "JOINT_AXIS_PARENT_FRAME_REQUIRED",
                 "Joint-axis tilt bindings must name the target joint parent frame.");
    }
    if (isTiltSemantic(binding.semanticKind) && binding.targetPropertyId != propertyFor(binding.semanticKind)) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId, "targetPropertyId",
                 "JOINT_AXIS_PRIMARY_PROPERTY_INVALID",
                 "Each joint-axis tilt semantic requires its matching typed U/V target.");
    }
    if (!std::isfinite(binding.maxAxisTiltAngle) || binding.maxAxisTiltAngle < 0.0 ||
        binding.maxAxisTiltAngle > std::acos(-1.0)) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId, "maxAxisTiltAngle",
                 "JOINT_AXIS_TILT_CONE_INVALID",
                 "Joint-axis tilt requires an explicit finite cone in [0, pi] radians.");
    }
    if (binding.axisTiltGroupId.empty()) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId, "axisTiltGroupId",
                 "JOINT_AXIS_TILT_GROUP_REQUIRED",
                 "Joint-axis tilt requires a stable per-joint U/V group ID.");
    } else if (binding.axisTiltGroupId != "axis-tilt:" + binding.targetObjectId) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId, "axisTiltGroupId",
                 "JOINT_AXIS_TILT_GROUP_INVALID",
                 "Joint-axis tilt group IDs must be derived from their typed target joint.");
    }
    if (!validAxis(joint->motionAxisInJoint)) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId, "motionAxisInJoint",
                 "JOINT_AXIS_NOMINAL_AXIS_INVALID",
                 "Joint-axis tilt requires a finite non-zero nominal motion axis.");
    }
    if (isTiltSemantic(binding.semanticKind)) {
        const ReadWriteTarget expected = ownTarget(binding);
        if (!exactSingleTarget(binding.readSet, expected) ||
            !exactSingleTarget(binding.writeSet, expected)) {
            result.valid = false;
            addError(result.diagnostics, binding.id, binding.targetObjectId, "readSet/writeSet",
                     "JOINT_AXIS_TILT_TARGET_SET_INVALID",
                     "Each U/V binding must declare exactly its own typed target once.");
        }
    }
    return result;
}

std::vector< ReadWriteTarget > JointAxisAdapter::declaredReadSet(const ParameterBinding& binding) const
{
    return isTiltSemantic(binding.semanticKind) ? std::vector< ReadWriteTarget >{ownTarget(binding)} :
                                                   std::vector< ReadWriteTarget >();
}

std::vector< ReadWriteTarget > JointAxisAdapter::declaredWriteSet(const ParameterBinding& binding) const
{
    return declaredReadSet(binding);
}

AdapterPatchCompileResult JointAxisAdapter::compilePatch(
    const AdapterPatchCompileRequest& request) const
{
    AdapterPatchCompileResult result;
    if (request.baseline == nullptr || request.binding == nullptr) {
        addError(result.diagnostics, "", "", "request", "JOINT_AXIS_REQUEST_REQUIRED",
                 "JointAxisAdapter requires immutable baseline and binding inputs.");
        return result;
    }
    const ParameterBinding& binding = *request.binding;
    if (!isTiltSemantic(binding.semanticKind)) {
        addError(result.diagnostics, binding.id, binding.targetObjectId, "semanticKind",
                 "JOINT_AXIS_SEMANTIC_UNSUPPORTED",
                 "JointAxisAdapter supports tangent-coordinate U/V semantics only.");
        return result;
    }
    if (!std::isfinite(binding.maxAxisTiltAngle) || binding.maxAxisTiltAngle < 0.0 ||
        binding.maxAxisTiltAngle > std::acos(-1.0)) {
        addError(result.diagnostics, binding.id, binding.targetObjectId, "maxAxisTiltAngle",
                 "JOINT_AXIS_TILT_CONE_INVALID",
                 "Joint-axis tilt requires an explicit finite cone in [0, pi] radians.");
        return result;
    }
    if (binding.axisTiltGroupId.empty()) {
        addError(result.diagnostics, binding.id, binding.targetObjectId, "axisTiltGroupId",
                 "JOINT_AXIS_TILT_GROUP_REQUIRED",
                 "Joint-axis tilt requires a stable per-joint U/V group ID.");
        return result;
    }
    if (binding.axisTiltGroupId != "axis-tilt:" + binding.targetObjectId) {
        addError(result.diagnostics, binding.id, binding.targetObjectId, "axisTiltGroupId",
                 "JOINT_AXIS_TILT_GROUP_INVALID",
                 "Joint-axis tilt group IDs must be derived from their typed target joint.");
        return result;
    }
    const JointEdge* joint = findJoint(*request.baseline, binding.targetObjectId);
    if (joint == nullptr || joint->type == CanonicalJointType::Fixed) {
        addError(result.diagnostics, binding.id, binding.targetObjectId, "targetObjectId",
                 "JOINT_AXIS_MOVABLE_JOINT_REQUIRED",
                 "Joint-axis tilt requires a known revolute or prismatic joint.");
        return result;
    }
    if (!validAxis(joint->motionAxisInJoint)) {
        addError(result.diagnostics, binding.id, binding.targetObjectId, "motionAxisInJoint",
                 "JOINT_AXIS_NOMINAL_AXIS_INVALID",
                 "Joint-axis tilt requires a finite non-zero nominal motion axis.");
        return result;
    }
    double alpha = 0.0;
    double beta = 0.0;
    if (!readGroupValues(request.values, binding, alpha, beta, result.diagnostics))
        return result;
    const double rho = std::hypot(alpha, beta);
    if (!std::isfinite(rho) || rho > binding.maxAxisTiltAngle) {
        addError(result.diagnostics, binding.id, binding.targetObjectId, "values",
                 "JOINT_AXIS_TILT_CONE_EXCEEDED",
                 "The tangent-coordinate tilt magnitude exceeds the configured cone.");
        return result;
    }
    // Retain the frozen exact mapping when the Patch is subsequently applied.
    const rw::math::Vector3D<> tilted = KinematicConventions::tiltedAxis(
        joint->motionAxisInJoint, alpha, beta);
    const double actualTilt = KinematicConventions::angleBetween(joint->motionAxisInJoint, tilted);
    if (!std::isfinite(tilted(0)) || !std::isfinite(tilted(1)) ||
        !std::isfinite(tilted(2)) || tilted.norm2() <= 1e-12 || !std::isfinite(actualTilt)) {
        addError(result.diagnostics, binding.id, binding.targetObjectId, "values",
                 "JOINT_AXIS_TILT_RESULT_INVALID",
                 "The frozen tangent-coordinate formula must produce a finite unit motion axis.");
        return result;
    }
    result.ok = true;
    result.patch.adapterId = adapterId();
    result.patch.adapterVersion = adapterVersion();
    result.patch.bindingId = binding.id;
    const double scalar = binding.semanticKind == SemanticKind::JointAxisTiltU ? alpha : beta;
    result.patch.writes = {{ownTarget(binding), CandidatePatchValue::scalar(scalar)}};
    return result;
}

std::string JointAxisAdapter::describeEffect(const ParameterBinding&) const
{
    return "Sets one baseline-relative tangent coordinate of the joint motion-axis tilt.";
}

}    // namespace rws
