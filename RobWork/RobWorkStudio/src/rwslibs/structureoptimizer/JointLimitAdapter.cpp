#include "JointLimitAdapter.hpp"

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

bool isLimitSemantic(const SemanticKind semantic)
{
    return semantic == SemanticKind::JointLimitLower || semantic == SemanticKind::JointLimitUpper;
}

DesignVariableUnit coordinateUnit(const JointEdge& joint)
{
    return joint.type == CanonicalJointType::Prismatic ? DesignVariableUnit::Metres :
                                                        DesignVariableUnit::Radians;
}

TargetPropertyId propertyFor(const SemanticKind semantic, const JointLimitScope scope)
{
    if (semantic == SemanticKind::JointLimitLower)
        return scope == JointLimitScope::Physical ? TargetPropertyId::PhysicalLimitLower :
               scope == JointLimitScope::Operational ? TargetPropertyId::OperationalLimitLower :
                                                        TargetPropertyId::Unknown;
    if (semantic == SemanticKind::JointLimitUpper)
        return scope == JointLimitScope::Physical ? TargetPropertyId::PhysicalLimitUpper :
               scope == JointLimitScope::Operational ? TargetPropertyId::OperationalLimitUpper :
                                                        TargetPropertyId::Unknown;
    return TargetPropertyId::Unknown;
}

ReadWriteTarget ownTarget(const ParameterBinding& binding)
{
    return {TargetObjectType::Joint, binding.targetObjectId,
            propertyFor(binding.semanticKind, binding.jointLimitScope), binding.coordinateFrameId};
}

bool exactSingleTarget(const std::vector< ReadWriteTarget >& targets,
                       const ReadWriteTarget& expected)
{
    return targets.size() == 1 && targets.front() == expected;
}

bool convertJointCoordinate(const double value,
                            const JointCoordinateConvention from,
                            const JointCoordinateConvention to,
                            const double zeroPositionOffset,
                            double& converted)
{
    if (!std::isfinite(value) || !std::isfinite(zeroPositionOffset))
        return false;
    if (from == to) {
        converted = value;
        return true;
    }
    if (from == JointCoordinateConvention::QInput &&
        to == JointCoordinateConvention::QModel) {
        converted = KinematicConventions::modelCoordinate(value, zeroPositionOffset);
        return std::isfinite(converted);
    }
    if (from == JointCoordinateConvention::QModel &&
        to == JointCoordinateConvention::QInput) {
        converted = value - zeroPositionOffset;
        return std::isfinite(converted);
    }
    return false;
}

void addError(std::vector< StructureOptimizationDiagnostic >& diagnostics,
              const std::string& bindingId, const std::string& objectId,
              const std::string& field, const std::string& code, const std::string& message)
{
    diagnostics.push_back(makeAdapterDiagnostic("JointLimitAdapter", bindingId, objectId, field,
                                                code, message));
}

bool resolvePair(const std::vector< ResolvedAdapterValue >& values,
                 const ParameterBinding& binding, const DesignVariableUnit unit,
                 double& lower, double& upper,
                 std::vector< StructureOptimizationDiagnostic >& diagnostics)
{
    bool haveLower = false;
    bool haveUpper = false;
    for (const ResolvedAdapterValue& value : values) {
        if (!isLimitSemantic(value.semanticKind)) {
            addError(diagnostics, binding.id, binding.targetObjectId, "values.semanticKind",
                     "JOINT_LIMIT_GROUP_VALUE_INVALID",
                     "Joint-limit groups accept concrete lower/upper semantics only.");
            return false;
        }
        if (value.jointLimitScope != binding.jointLimitScope) {
            addError(diagnostics, binding.id, binding.targetObjectId, "values.jointLimitScope",
                     "JOINT_LIMIT_GROUP_SCOPE_MISMATCH",
                     "Joint-limit pairs may not mix physical and operational boundary values.");
            return false;
        }
        if (value.groupId != binding.jointLimitGroupId) {
            addError(diagnostics, binding.id, binding.targetObjectId, "values.groupId",
                     "JOINT_LIMIT_GROUP_MISMATCH",
                     "Joint-limit pair values must belong to the binding's canonical target-joint group.");
            return false;
        }
        if (value.unit != unit || !value.discreteOptionId.empty() ||
            !std::isfinite(value.engineeringValue)) {
            addError(diagnostics, binding.id, binding.targetObjectId, "values",
                     "JOINT_LIMIT_GROUP_VALUE_INVALID",
                     "Joint-limit groups require finite scalar lower/upper values in the joint coordinate unit.");
            return false;
        }
        if (value.semanticKind == SemanticKind::JointLimitLower) {
            if (haveLower) {
                addError(diagnostics, binding.id, binding.targetObjectId, "values",
                         "JOINT_LIMIT_GROUP_VALUE_DUPLICATE",
                         "Joint-limit groups may contain each lower/upper semantic exactly once.");
                return false;
            }
            haveLower = true;
            lower = value.engineeringValue;
        } else {
            if (haveUpper) {
                addError(diagnostics, binding.id, binding.targetObjectId, "values",
                         "JOINT_LIMIT_GROUP_VALUE_DUPLICATE",
                         "Joint-limit groups may contain each lower/upper semantic exactly once.");
                return false;
            }
            haveUpper = true;
            upper = value.engineeringValue;
        }
    }
    if (values.size() != 2 || !haveLower || !haveUpper) {
        addError(diagnostics, binding.id, binding.targetObjectId, "values",
                 "JOINT_LIMIT_GROUP_VALUE_REQUIRED",
                 "Joint-limit compilation requires exactly one lower and one upper value for its group.");
        return false;
    }
    return true;
}

}    // namespace

std::string JointLimitAdapter::adapterId() const { return "JointLimitAdapter"; }
int JointLimitAdapter::adapterVersion() const { return 1; }
std::vector< SemanticKind > JointLimitAdapter::supportedSemanticKinds() const
{
    return {SemanticKind::JointLimitLower, SemanticKind::JointLimitUpper};
}
std::vector< AdapterCapability > JointLimitAdapter::requiredCapabilities() const
{
    return {AdapterCapability::JointLimits};
}

AdapterBindingValidationResult JointLimitAdapter::validateBinding(
    const ParameterBinding& binding, const CanonicalKinematicModel& baseline) const
{
    AdapterBindingValidationResult result;
    // Defense in depth for direct adapter users: no limit patch may be
    // compiled from malformed canonical physical or operational limits.
    const CanonicalKinematicModelValidationResult modelValidation =
        CanonicalKinematicModelValidator::validate(baseline);
    if (!modelValidation.valid) {
        result.valid = false;
        result.diagnostics = modelValidation.diagnostics;
        return result;
    }
    const JointEdge* const joint = findJoint(baseline, binding.targetObjectId);
    if (!isLimitSemantic(binding.semanticKind)) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId, "semanticKind",
                 "JOINT_LIMIT_SEMANTIC_UNSUPPORTED",
                 "JointLimitAdapter supports lower and upper limit semantics only.");
    }
    if (binding.targetObjectType != TargetObjectType::Joint || joint == nullptr ||
        joint->type == CanonicalJointType::Fixed) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId, "targetObjectId",
                 "JOINT_LIMIT_MOVABLE_JOINT_REQUIRED",
                 "Joint limits require a known revolute or prismatic joint.");
        return result;
    }
    if (binding.coordinateFrameId != joint->parentFrameId) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId, "coordinateFrameId",
                 "JOINT_LIMIT_PARENT_FRAME_REQUIRED",
                 "Joint-limit bindings must name the target joint parent frame.");
    }
    if (binding.jointLimitScope == JointLimitScope::Unknown) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId, "jointLimitScope",
                 "JOINT_LIMIT_SCOPE_REQUIRED",
                 "Joint limits require an explicit physical or operational scope.");
    }
    if (!isValidJointCoordinateConvention(binding.jointLimitCoordinateConvention)) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId,
                 "jointLimitCoordinateConvention",
                 "JOINT_LIMIT_COORDINATE_CONVENTION_INVALID",
                 "Joint-limit bindings must explicitly declare a valid q_input or q_model coordinate.");
        return result;
    }
    const CanonicalJointLimits& scopedLimits =
        binding.jointLimitScope == JointLimitScope::Physical ? joint->physicalLimits :
                                                               joint->operationalLimits;
    if (binding.jointLimitCoordinateConvention != scopedLimits.coordinateConvention) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId,
                 "jointLimitCoordinateConvention",
                 "JOINT_LIMIT_COORDINATE_CONVENTION_MISMATCH",
                 "Joint-limit bindings must use the same explicit q_input/q_model convention as their scoped canonical limits.");
    }
    if (binding.jointLimitGroupId != "joint-limits:" + binding.targetObjectId) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId, "jointLimitGroupId",
                 "JOINT_LIMIT_GROUP_INVALID",
                 "Joint-limit group IDs must be derived from their typed target joint.");
    }
    if (!std::isfinite(binding.minimumJointLimitRange) || binding.minimumJointLimitRange <= 0.0) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId, "minimumJointLimitRange",
                 "JOINT_LIMIT_MINIMUM_RANGE_INVALID",
                 "Joint limits require a finite positive minimum range.");
    }
    if (!std::isfinite(binding.absoluteJointLimitLower) ||
        !std::isfinite(binding.absoluteJointLimitUpper) ||
        !(binding.absoluteJointLimitLower < binding.absoluteJointLimitUpper)) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId,
                 "absoluteJointLimitLower/absoluteJointLimitUpper",
                 "JOINT_LIMIT_ABSOLUTE_BOUNDS_INVALID",
                 "Joint limits require explicit finite ordered absolute bounds.");
    }
    if (binding.jointLimitScope == JointLimitScope::Physical &&
        !binding.allowPhysicalLimitModification) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId,
                 "allowPhysicalLimitModification", "JOINT_LIMIT_PHYSICAL_LOCKED",
                 "Physical mechanical limits require explicit project authorization.");
    }
    if (binding.targetPropertyId != propertyFor(binding.semanticKind, binding.jointLimitScope)) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId, "targetPropertyId",
                 "JOINT_LIMIT_PROPERTY_INVALID",
                 "Joint-limit semantics require the matching typed scoped limit property.");
    }
    const ReadWriteTarget expected = ownTarget(binding);
    if (!exactSingleTarget(binding.readSet, expected) ||
        !exactSingleTarget(binding.writeSet, expected)) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId, "readSet/writeSet",
                 "JOINT_LIMIT_TARGET_SET_INVALID",
                 "Each lower/upper binding must declare exactly its own typed target once.");
    }
    return result;
}

std::vector< ReadWriteTarget > JointLimitAdapter::declaredReadSet(
    const ParameterBinding& binding) const
{
    return isLimitSemantic(binding.semanticKind) ?
        std::vector< ReadWriteTarget >{ownTarget(binding)} : std::vector< ReadWriteTarget >();
}

std::vector< ReadWriteTarget > JointLimitAdapter::declaredWriteSet(
    const ParameterBinding& binding) const
{
    return declaredReadSet(binding);
}

AdapterPatchCompileResult JointLimitAdapter::compilePatch(
    const AdapterPatchCompileRequest& request) const
{
    AdapterPatchCompileResult result;
    if (request.baseline == nullptr || request.binding == nullptr) {
        addError(result.diagnostics, "", "", "request", "JOINT_LIMIT_REQUEST_REQUIRED",
                 "JointLimitAdapter requires immutable baseline and binding inputs.");
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
                 "JOINT_LIMIT_MOVABLE_JOINT_REQUIRED",
                 "Joint limits require a known revolute or prismatic joint.");
        return result;
    }
    double lower = 0.0;
    double upper = 0.0;
    if (!resolvePair(request.values, binding, coordinateUnit(*joint), lower, upper,
                     result.diagnostics))
        return result;
    if (!(lower < upper)) {
        addError(result.diagnostics, binding.id, binding.targetObjectId, "values",
                 "JOINT_LIMIT_RANGE_ORDER_INVALID",
                 "The resolved lower limit must be strictly less than the upper limit.");
        return result;
    }
    if (upper - lower < binding.minimumJointLimitRange) {
        addError(result.diagnostics, binding.id, binding.targetObjectId, "values",
                 "JOINT_LIMIT_MINIMUM_RANGE_INVALID",
                 "The resolved lower/upper pair is smaller than the explicit minimum range.");
        return result;
    }
    if (lower < binding.absoluteJointLimitLower || upper > binding.absoluteJointLimitUpper) {
        addError(result.diagnostics, binding.id, binding.targetObjectId, "values",
                 "JOINT_LIMIT_ABSOLUTE_BOUNDS_EXCEEDED",
                 "The resolved limit pair must remain within its explicit absolute bounds.");
        return result;
    }
    if (binding.jointLimitScope == JointLimitScope::Operational && joint->physicalLimits.enabled) {
        double physicalLowerInOperationalCoordinates = 0.0;
        double physicalUpperInOperationalCoordinates = 0.0;
        if (!convertJointCoordinate(joint->physicalLimits.lower,
                                    joint->physicalLimits.coordinateConvention,
                                    binding.jointLimitCoordinateConvention,
                                    joint->zeroPositionOffset,
                                    physicalLowerInOperationalCoordinates) ||
            !convertJointCoordinate(joint->physicalLimits.upper,
                                    joint->physicalLimits.coordinateConvention,
                                    binding.jointLimitCoordinateConvention,
                                    joint->zeroPositionOffset,
                                    physicalUpperInOperationalCoordinates)) {
            addError(result.diagnostics, binding.id, binding.targetObjectId,
                     "physicalLimits/zeroPositionOffset",
                     "JOINT_LIMIT_COORDINATE_CONVERSION_INVALID",
                     "Physical and operational limit comparisons require finite explicit q_input/q_model conversion data.");
            return result;
        }
        if (lower < physicalLowerInOperationalCoordinates ||
            upper > physicalUpperInOperationalCoordinates) {
            addError(result.diagnostics, binding.id, binding.targetObjectId, "values",
                     "JOINT_LIMIT_OPERATIONAL_OUTSIDE_PHYSICAL",
                     "Operational limits may not exceed enabled physical mechanical limits after q_input/q_model conversion.");
            return result;
        }
    }
    const double ownValue = binding.semanticKind == SemanticKind::JointLimitLower ? lower : upper;
    result.ok = true;
    result.patch.adapterId = adapterId();
    result.patch.adapterVersion = adapterVersion();
    result.patch.bindingId = binding.id;
    result.patch.affectsStructuralCapability = binding.jointLimitScope == JointLimitScope::Physical;
    result.patch.writes = {{ownTarget(binding), CandidatePatchValue::scalar(ownValue)}};
    return result;
}

std::string JointLimitAdapter::describeEffect(const ParameterBinding& binding) const
{
    return binding.jointLimitScope == JointLimitScope::Physical ?
        "Sets one explicitly authorized physical lower/upper mechanical bound." :
        "Sets one operational lower/upper policy bound without structural-capability credit.";
}

}    // namespace rws
