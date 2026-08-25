// -----------------------------------------------------------------------------
//  JointParameterAdapters.cpp - JointLimit / JointZero / JointOrigin / JointAxis
//  Merged from four translation units; every adapter keeps its original
//  namespace, method definitions, registry IDs and diagnostics codes.  The
//  byte-identical anonymous helpers (findJoint / coordinateUnit /
//  exactSingleTarget / validAxis) are kept once; the colliding ownTarget /
//  addError helpers carry an adapter prefix.  Logic and validation order are
//  line-identical to the pre-merge files.
// -----------------------------------------------------------------------------
#include "JointAxisAdapter.hpp"
#include "JointLimitAdapter.hpp"
#include "JointOriginAdapter.hpp"
#include "JointZeroAdapter.hpp"

#include "KinematicConventions.hpp"

#include <algorithm>
#include <cmath>

namespace rws {
namespace {

// ---- shared: identical across the four joint adapters ----
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

bool exactSingleTarget(const std::vector< ReadWriteTarget >& targets,
                       const ReadWriteTarget& expected)
{
    return targets.size() == 1 && targets.front() == expected;
}

bool validAxis(const rw::math::Vector3D<>& axis)
{
    return std::isfinite(axis(0)) && std::isfinite(axis(1)) && std::isfinite(axis(2)) &&
           axis.norm2() > 1e-12;
}


bool isLimitSemantic(const SemanticKind semantic)
{
    return semantic == SemanticKind::JointLimitLower || semantic == SemanticKind::JointLimitUpper;
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

ReadWriteTarget jointLimit_ownTarget(const ParameterBinding& binding)
{
    return {TargetObjectType::Joint, binding.targetObjectId,
            propertyFor(binding.semanticKind, binding.jointLimitScope), binding.coordinateFrameId};
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

void jointLimit_addError(std::vector< StructureOptimizationDiagnostic >& diagnostics,
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
            jointLimit_addError(diagnostics, binding.id, binding.targetObjectId, "values.semanticKind",
                     "JOINT_LIMIT_GROUP_VALUE_INVALID",
                     "Joint-limit groups accept concrete lower/upper semantics only.");
            return false;
        }
        if (value.jointLimitScope != binding.jointLimitScope) {
            jointLimit_addError(diagnostics, binding.id, binding.targetObjectId, "values.jointLimitScope",
                     "JOINT_LIMIT_GROUP_SCOPE_MISMATCH",
                     "Joint-limit pairs may not mix physical and operational boundary values.");
            return false;
        }
        if (value.groupId != binding.jointLimitGroupId) {
            jointLimit_addError(diagnostics, binding.id, binding.targetObjectId, "values.groupId",
                     "JOINT_LIMIT_GROUP_MISMATCH",
                     "Joint-limit pair values must belong to the binding's canonical target-joint group.");
            return false;
        }
        if (value.unit != unit || !value.discreteOptionId.empty() ||
            !std::isfinite(value.engineeringValue)) {
            jointLimit_addError(diagnostics, binding.id, binding.targetObjectId, "values",
                     "JOINT_LIMIT_GROUP_VALUE_INVALID",
                     "Joint-limit groups require finite scalar lower/upper values in the joint coordinate unit.");
            return false;
        }
        if (value.semanticKind == SemanticKind::JointLimitLower) {
            if (haveLower) {
                jointLimit_addError(diagnostics, binding.id, binding.targetObjectId, "values",
                         "JOINT_LIMIT_GROUP_VALUE_DUPLICATE",
                         "Joint-limit groups may contain each lower/upper semantic exactly once.");
                return false;
            }
            haveLower = true;
            lower = value.engineeringValue;
        } else {
            if (haveUpper) {
                jointLimit_addError(diagnostics, binding.id, binding.targetObjectId, "values",
                         "JOINT_LIMIT_GROUP_VALUE_DUPLICATE",
                         "Joint-limit groups may contain each lower/upper semantic exactly once.");
                return false;
            }
            haveUpper = true;
            upper = value.engineeringValue;
        }
    }
    if (values.size() != 2 || !haveLower || !haveUpper) {
        jointLimit_addError(diagnostics, binding.id, binding.targetObjectId, "values",
                 "JOINT_LIMIT_GROUP_VALUE_REQUIRED",
                 "Joint-limit compilation requires exactly one lower and one upper value for its group.");
        return false;
    }
    return true;
}



ReadWriteTarget jointZero_ownTarget(const ParameterBinding& binding)
{
    return {TargetObjectType::Joint, binding.targetObjectId,
            TargetPropertyId::ZeroPositionOffset, binding.coordinateFrameId};
}

void jointZero_addError(std::vector< StructureOptimizationDiagnostic >& diagnostics,
              const std::string& bindingId, const std::string& objectId,
              const std::string& field, const std::string& code, const std::string& message)
{
    diagnostics.push_back(makeAdapterDiagnostic("JointZeroAdapter", bindingId, objectId, field,
                                                code, message));
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

void jointOrigin_addError(std::vector< StructureOptimizationDiagnostic >& diagnostics,
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



bool isTiltSemantic(const SemanticKind semantic)
{
    return semantic == SemanticKind::JointAxisTiltU || semantic == SemanticKind::JointAxisTiltV;
}

TargetPropertyId propertyFor(const SemanticKind semantic)
{
    return semantic == SemanticKind::JointAxisTiltU ? TargetPropertyId::MotionAxisTiltU :
                                                      TargetPropertyId::MotionAxisTiltV;
}

ReadWriteTarget jointAxis_ownTarget(const ParameterBinding& binding)
{
    return {TargetObjectType::Joint, binding.targetObjectId, propertyFor(binding.semanticKind),
            binding.coordinateFrameId};
}

void jointAxis_addError(std::vector< StructureOptimizationDiagnostic >& diagnostics,
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
            jointAxis_addError(diagnostics, binding.id, binding.targetObjectId, "values.semanticKind",
                     "JOINT_AXIS_TILT_GROUP_SEMANTIC_INVALID",
                     "Joint-axis tilt groups accept concrete U/V semantics only.");
            return false;
        }
        if (value.groupId != binding.axisTiltGroupId) {
            jointAxis_addError(diagnostics, binding.id, binding.targetObjectId, "values.groupId",
                     "JOINT_AXIS_TILT_GROUP_MISMATCH",
                     "Joint-axis tilt values must belong to the binding's explicit U/V group.");
            return false;
        }
        if (value.unit != DesignVariableUnit::Radians || !value.discreteOptionId.empty() ||
            !std::isfinite(value.engineeringValue)) {
            jointAxis_addError(diagnostics, binding.id, binding.targetObjectId, "values",
                     "JOINT_AXIS_TILT_GROUP_VALUE_INVALID",
                     "Joint-axis tilt groups require finite scalar radian values.");
            return false;
        }
        if (value.semanticKind == SemanticKind::JointAxisTiltU) {
            if (haveU) {
                jointAxis_addError(diagnostics, binding.id, binding.targetObjectId, "values",
                         "JOINT_AXIS_TILT_GROUP_VALUE_DUPLICATE",
                         "Joint-axis tilt groups may contain U at most once.");
                return false;
            }
            haveU = true;
            alpha = value.engineeringValue;
        } else {
            if (haveV) {
                jointAxis_addError(diagnostics, binding.id, binding.targetObjectId, "values",
                         "JOINT_AXIS_TILT_GROUP_VALUE_DUPLICATE",
                         "Joint-axis tilt groups may contain V at most once.");
                return false;
            }
            haveV = true;
            beta = value.engineeringValue;
        }
    }
    if (!haveU || !haveV) {
        jointAxis_addError(diagnostics, binding.id, binding.targetObjectId, "values",
                 "JOINT_AXIS_TILT_GROUP_VALUE_REQUIRED",
                 "Joint-axis tilt compilation requires exactly one U and one V value for its group.");
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
        jointLimit_addError(result.diagnostics, binding.id, binding.targetObjectId, "semanticKind",
                 "JOINT_LIMIT_SEMANTIC_UNSUPPORTED",
                 "JointLimitAdapter supports lower and upper limit semantics only.");
    }
    if (binding.targetObjectType != TargetObjectType::Joint || joint == nullptr ||
        joint->type == CanonicalJointType::Fixed) {
        result.valid = false;
        jointLimit_addError(result.diagnostics, binding.id, binding.targetObjectId, "targetObjectId",
                 "JOINT_LIMIT_MOVABLE_JOINT_REQUIRED",
                 "Joint limits require a known revolute or prismatic joint.");
        return result;
    }
    if (binding.coordinateFrameId != joint->parentFrameId) {
        result.valid = false;
        jointLimit_addError(result.diagnostics, binding.id, binding.targetObjectId, "coordinateFrameId",
                 "JOINT_LIMIT_PARENT_FRAME_REQUIRED",
                 "Joint-limit bindings must name the target joint parent frame.");
    }
    if (binding.jointLimitScope == JointLimitScope::Unknown) {
        result.valid = false;
        jointLimit_addError(result.diagnostics, binding.id, binding.targetObjectId, "jointLimitScope",
                 "JOINT_LIMIT_SCOPE_REQUIRED",
                 "Joint limits require an explicit physical or operational scope.");
    }
    if (!isValidJointCoordinateConvention(binding.jointLimitCoordinateConvention)) {
        result.valid = false;
        jointLimit_addError(result.diagnostics, binding.id, binding.targetObjectId,
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
        jointLimit_addError(result.diagnostics, binding.id, binding.targetObjectId,
                 "jointLimitCoordinateConvention",
                 "JOINT_LIMIT_COORDINATE_CONVENTION_MISMATCH",
                 "Joint-limit bindings must use the same explicit q_input/q_model convention as their scoped canonical limits.");
    }
    if (binding.jointLimitGroupId != "joint-limits:" + binding.targetObjectId) {
        result.valid = false;
        jointLimit_addError(result.diagnostics, binding.id, binding.targetObjectId, "jointLimitGroupId",
                 "JOINT_LIMIT_GROUP_INVALID",
                 "Joint-limit group IDs must be derived from their typed target joint.");
    }
    if (!std::isfinite(binding.minimumJointLimitRange) || binding.minimumJointLimitRange <= 0.0) {
        result.valid = false;
        jointLimit_addError(result.diagnostics, binding.id, binding.targetObjectId, "minimumJointLimitRange",
                 "JOINT_LIMIT_MINIMUM_RANGE_INVALID",
                 "Joint limits require a finite positive minimum range.");
    }
    if (!std::isfinite(binding.absoluteJointLimitLower) ||
        !std::isfinite(binding.absoluteJointLimitUpper) ||
        !(binding.absoluteJointLimitLower < binding.absoluteJointLimitUpper)) {
        result.valid = false;
        jointLimit_addError(result.diagnostics, binding.id, binding.targetObjectId,
                 "absoluteJointLimitLower/absoluteJointLimitUpper",
                 "JOINT_LIMIT_ABSOLUTE_BOUNDS_INVALID",
                 "Joint limits require explicit finite ordered absolute bounds.");
    }
    if (binding.jointLimitScope == JointLimitScope::Physical &&
        !binding.allowPhysicalLimitModification) {
        result.valid = false;
        jointLimit_addError(result.diagnostics, binding.id, binding.targetObjectId,
                 "allowPhysicalLimitModification", "JOINT_LIMIT_PHYSICAL_LOCKED",
                 "Physical mechanical limits require explicit project authorization.");
    }
    if (binding.targetPropertyId != propertyFor(binding.semanticKind, binding.jointLimitScope)) {
        result.valid = false;
        jointLimit_addError(result.diagnostics, binding.id, binding.targetObjectId, "targetPropertyId",
                 "JOINT_LIMIT_PROPERTY_INVALID",
                 "Joint-limit semantics require the matching typed scoped limit property.");
    }
    const ReadWriteTarget expected = jointLimit_ownTarget(binding);
    if (!exactSingleTarget(binding.readSet, expected) ||
        !exactSingleTarget(binding.writeSet, expected)) {
        result.valid = false;
        jointLimit_addError(result.diagnostics, binding.id, binding.targetObjectId, "readSet/writeSet",
                 "JOINT_LIMIT_TARGET_SET_INVALID",
                 "Each lower/upper binding must declare exactly its own typed target once.");
    }
    return result;
}

std::vector< ReadWriteTarget > JointLimitAdapter::declaredReadSet(
    const ParameterBinding& binding) const
{
    return isLimitSemantic(binding.semanticKind) ?
        std::vector< ReadWriteTarget >{jointLimit_ownTarget(binding)} : std::vector< ReadWriteTarget >();
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
        jointLimit_addError(result.diagnostics, "", "", "request", "JOINT_LIMIT_REQUEST_REQUIRED",
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
        jointLimit_addError(result.diagnostics, binding.id, binding.targetObjectId, "targetObjectId",
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
        jointLimit_addError(result.diagnostics, binding.id, binding.targetObjectId, "values",
                 "JOINT_LIMIT_RANGE_ORDER_INVALID",
                 "The resolved lower limit must be strictly less than the upper limit.");
        return result;
    }
    if (upper - lower < binding.minimumJointLimitRange) {
        jointLimit_addError(result.diagnostics, binding.id, binding.targetObjectId, "values",
                 "JOINT_LIMIT_MINIMUM_RANGE_INVALID",
                 "The resolved lower/upper pair is smaller than the explicit minimum range.");
        return result;
    }
    if (lower < binding.absoluteJointLimitLower || upper > binding.absoluteJointLimitUpper) {
        jointLimit_addError(result.diagnostics, binding.id, binding.targetObjectId, "values",
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
            jointLimit_addError(result.diagnostics, binding.id, binding.targetObjectId,
                     "physicalLimits/zeroPositionOffset",
                     "JOINT_LIMIT_COORDINATE_CONVERSION_INVALID",
                     "Physical and operational limit comparisons require finite explicit q_input/q_model conversion data.");
            return result;
        }
        if (lower < physicalLowerInOperationalCoordinates ||
            upper > physicalUpperInOperationalCoordinates) {
            jointLimit_addError(result.diagnostics, binding.id, binding.targetObjectId, "values",
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
    result.patch.writes = {{jointLimit_ownTarget(binding), CandidatePatchValue::scalar(ownValue)}};
    return result;
}

std::string JointLimitAdapter::describeEffect(const ParameterBinding& binding) const
{
    return binding.jointLimitScope == JointLimitScope::Physical ?
        "Sets one explicitly authorized physical lower/upper mechanical bound." :
        "Sets one operational lower/upper policy bound without structural-capability credit.";
}



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
        jointZero_addError(result.diagnostics, binding.id, binding.targetObjectId, "semanticKind",
                 "JOINT_ZERO_SEMANTIC_UNSUPPORTED",
                 "JointZeroAdapter supports the JointZeroOffset semantic only.");
    }
    if (binding.targetObjectType != TargetObjectType::Joint || joint == nullptr ||
        joint->type == CanonicalJointType::Fixed) {
        result.valid = false;
        jointZero_addError(result.diagnostics, binding.id, binding.targetObjectId, "targetObjectId",
                 "JOINT_ZERO_MOVABLE_JOINT_REQUIRED",
                 "Joint zero offsets require a known revolute or prismatic joint.");
        return result;
    }
    if (binding.coordinateFrameId != joint->parentFrameId) {
        result.valid = false;
        jointZero_addError(result.diagnostics, binding.id, binding.targetObjectId, "coordinateFrameId",
                 "JOINT_ZERO_PARENT_FRAME_REQUIRED",
                 "Joint-zero bindings must name the target joint parent frame.");
    }
    if (binding.targetPropertyId != TargetPropertyId::ZeroPositionOffset) {
        result.valid = false;
        jointZero_addError(result.diagnostics, binding.id, binding.targetObjectId, "targetPropertyId",
                 "JOINT_ZERO_PROPERTY_INVALID",
                 "JointZeroOffset requires the typed ZeroPositionOffset target.");
    }
    const ReadWriteTarget expected = jointZero_ownTarget(binding);
    if (!exactSingleTarget(binding.readSet, expected) ||
        !exactSingleTarget(binding.writeSet, expected)) {
        result.valid = false;
        jointZero_addError(result.diagnostics, binding.id, binding.targetObjectId, "readSet/writeSet",
                 "JOINT_ZERO_TARGET_SET_INVALID",
                 "Joint-zero bindings must declare exactly their own typed target once.");
    }
    return result;
}

std::vector< ReadWriteTarget > JointZeroAdapter::declaredReadSet(
    const ParameterBinding& binding) const
{
    return binding.semanticKind == SemanticKind::JointZeroOffset ?
        std::vector< ReadWriteTarget >{jointZero_ownTarget(binding)} : std::vector< ReadWriteTarget >();
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
        jointZero_addError(result.diagnostics, "", "", "request", "JOINT_ZERO_REQUEST_REQUIRED",
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
        jointZero_addError(result.diagnostics, binding.id, binding.targetObjectId, "targetObjectId",
                 "JOINT_ZERO_MOVABLE_JOINT_REQUIRED",
                 "Joint zero offsets require a known revolute or prismatic joint.");
        return result;
    }
    if (request.values.size() != 1) {
        jointZero_addError(result.diagnostics, binding.id, binding.targetObjectId, "values",
                 "JOINT_ZERO_VALUE_COUNT_INVALID",
                 "Joint zero offsets require exactly one scalar coordinate value.");
        return result;
    }
    const ResolvedAdapterValue& value = request.values.front();
    if (value.semanticKind != SemanticKind::JointZeroOffset || !value.groupId.empty() ||
        !value.discreteOptionId.empty() || !std::isfinite(value.engineeringValue)) {
        jointZero_addError(result.diagnostics, binding.id, binding.targetObjectId, "values",
                 "JOINT_ZERO_VALUE_INVALID",
                 "Joint zero offsets require one finite non-discrete coordinate value.");
        return result;
    }
    if (value.unit != coordinateUnit(*joint)) {
        jointZero_addError(result.diagnostics, binding.id, binding.targetObjectId, "values.unit",
                 "JOINT_ZERO_VALUE_UNIT_INVALID",
                 "Revolute zero offsets use radians and prismatic zero offsets use metres.");
        return result;
    }
    result.ok = true;
    result.patch.adapterId = adapterId();
    result.patch.adapterVersion = adapterVersion();
    result.patch.bindingId = binding.id;
    result.patch.writes = {{jointZero_ownTarget(binding), CandidatePatchValue::scalar(value.engineeringValue)}};
    return result;
}

std::string JointZeroAdapter::describeEffect(const ParameterBinding&) const
{
    return "Sets q_model = q_input + zeroPositionOffset without changing the joint axis or mount transform.";
}



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
        jointOrigin_addError(result.diagnostics, binding.id, binding.targetObjectId, "semanticKind",
                 "JOINT_ORIGIN_SEMANTIC_UNSUPPORTED", "JointOriginAdapter received an unsupported semantic.");
    }
    if (binding.targetObjectType != TargetObjectType::Joint || joint == nullptr ||
        joint->type == CanonicalJointType::Fixed) {
        result.valid = false;
        jointOrigin_addError(result.diagnostics, binding.id, binding.targetObjectId, "targetObjectId",
                 "JOINT_ORIGIN_MOVABLE_JOINT_REQUIRED", "Joint-origin offsets require a known movable joint.");
        return result;
    }
    if (binding.coordinateFrameId != joint->parentFrameId) {
        result.valid = false;
        jointOrigin_addError(result.diagnostics, binding.id, binding.targetObjectId, "coordinateFrameId",
                 "JOINT_ORIGIN_PARENT_FRAME_REQUIRED", "Joint-origin writes must use the target joint parent frame.");
    }
    if (binding.semanticKind == SemanticKind::JointOffsetAlongAxis) {
        if (!isTranslationProperty(binding.targetPropertyId)) {
            result.valid = false;
            jointOrigin_addError(result.diagnostics, binding.id, binding.targetObjectId, "targetPropertyId",
                     "JOINT_ORIGIN_PRIMARY_PROPERTY_INVALID",
                     "Along-axis offsets require a parent-to-joint translation primary property.");
        }
        if (!validAxis(joint->motionAxisInJoint)) {
            result.valid = false;
            jointOrigin_addError(result.diagnostics, binding.id, binding.targetObjectId, "motionAxisInJoint",
                     "JOINT_ORIGIN_AXIS_INVALID",
                     "Along-axis offsets require a finite non-zero baseline joint axis.");
        }
    } else if (supports(binding.semanticKind) &&
               binding.targetPropertyId != cartesianProperty(binding.semanticKind)) {
        result.valid = false;
        jointOrigin_addError(result.diagnostics, binding.id, binding.targetObjectId, "targetPropertyId",
                 "JOINT_ORIGIN_PRIMARY_PROPERTY_INVALID",
                 "Cartesian joint-origin semantics require their matching translation primary property.");
    }
    const std::vector< ReadWriteTarget > expected = translationTargets(binding);
    if (!exactTargets(binding.readSet, expected) || !exactTargets(binding.writeSet, expected)) {
        result.valid = false;
        jointOrigin_addError(result.diagnostics, binding.id, binding.targetObjectId, "readSet/writeSet",
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
        jointOrigin_addError(result.diagnostics, "", "", "request", "JOINT_ORIGIN_REQUEST_REQUIRED",
                 "JointOriginAdapter requires immutable baseline and binding inputs.");
        return result;
    }
    const ParameterBinding& binding = *request.binding;
    if (request.values.size() != 1 || request.values[0].unit != DesignVariableUnit::Metres ||
        !request.values[0].discreteOptionId.empty() || !std::isfinite(request.values[0].engineeringValue)) {
        jointOrigin_addError(result.diagnostics, binding.id, binding.targetObjectId, "values",
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
            jointOrigin_addError(result.diagnostics, binding.id, binding.targetObjectId, "motionAxisInJoint",
                     "JOINT_ORIGIN_AXIS_INVALID",
                     "Along-axis offsets require a finite non-zero baseline joint axis.");
            return result;
        }
        delta = joint->parentToJointZero.R() * rw::math::normalize(joint->motionAxisInJoint) * value;
    }
    else {
        jointOrigin_addError(result.diagnostics, binding.id, binding.targetObjectId, "semanticKind",
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
        jointAxis_addError(result.diagnostics, binding.id, binding.targetObjectId, "semanticKind",
                 "JOINT_AXIS_SEMANTIC_UNSUPPORTED",
                 "JointAxisAdapter supports tangent-coordinate U/V semantics only.");
    }
    if (binding.targetObjectType != TargetObjectType::Joint || joint == nullptr ||
        joint->type == CanonicalJointType::Fixed) {
        result.valid = false;
        jointAxis_addError(result.diagnostics, binding.id, binding.targetObjectId, "targetObjectId",
                 "JOINT_AXIS_MOVABLE_JOINT_REQUIRED",
                 "Joint-axis tilt requires a known revolute or prismatic joint.");
        return result;
    }
    if (binding.coordinateFrameId != joint->parentFrameId) {
        result.valid = false;
        jointAxis_addError(result.diagnostics, binding.id, binding.targetObjectId, "coordinateFrameId",
                 "JOINT_AXIS_PARENT_FRAME_REQUIRED",
                 "Joint-axis tilt bindings must name the target joint parent frame.");
    }
    if (isTiltSemantic(binding.semanticKind) && binding.targetPropertyId != propertyFor(binding.semanticKind)) {
        result.valid = false;
        jointAxis_addError(result.diagnostics, binding.id, binding.targetObjectId, "targetPropertyId",
                 "JOINT_AXIS_PRIMARY_PROPERTY_INVALID",
                 "Each joint-axis tilt semantic requires its matching typed U/V target.");
    }
    if (!std::isfinite(binding.maxAxisTiltAngle) || binding.maxAxisTiltAngle < 0.0 ||
        binding.maxAxisTiltAngle > std::acos(-1.0)) {
        result.valid = false;
        jointAxis_addError(result.diagnostics, binding.id, binding.targetObjectId, "maxAxisTiltAngle",
                 "JOINT_AXIS_TILT_CONE_INVALID",
                 "Joint-axis tilt requires an explicit finite cone in [0, pi] radians.");
    }
    if (binding.axisTiltGroupId.empty()) {
        result.valid = false;
        jointAxis_addError(result.diagnostics, binding.id, binding.targetObjectId, "axisTiltGroupId",
                 "JOINT_AXIS_TILT_GROUP_REQUIRED",
                 "Joint-axis tilt requires a stable per-joint U/V group ID.");
    } else if (binding.axisTiltGroupId != "axis-tilt:" + binding.targetObjectId) {
        result.valid = false;
        jointAxis_addError(result.diagnostics, binding.id, binding.targetObjectId, "axisTiltGroupId",
                 "JOINT_AXIS_TILT_GROUP_INVALID",
                 "Joint-axis tilt group IDs must be derived from their typed target joint.");
    }
    if (!validAxis(joint->motionAxisInJoint)) {
        result.valid = false;
        jointAxis_addError(result.diagnostics, binding.id, binding.targetObjectId, "motionAxisInJoint",
                 "JOINT_AXIS_NOMINAL_AXIS_INVALID",
                 "Joint-axis tilt requires a finite non-zero nominal motion axis.");
    }
    if (isTiltSemantic(binding.semanticKind)) {
        const ReadWriteTarget expected = jointAxis_ownTarget(binding);
        if (!exactSingleTarget(binding.readSet, expected) ||
            !exactSingleTarget(binding.writeSet, expected)) {
            result.valid = false;
            jointAxis_addError(result.diagnostics, binding.id, binding.targetObjectId, "readSet/writeSet",
                     "JOINT_AXIS_TILT_TARGET_SET_INVALID",
                     "Each U/V binding must declare exactly its own typed target once.");
        }
    }
    return result;
}

std::vector< ReadWriteTarget > JointAxisAdapter::declaredReadSet(const ParameterBinding& binding) const
{
    return isTiltSemantic(binding.semanticKind) ? std::vector< ReadWriteTarget >{jointAxis_ownTarget(binding)} :
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
        jointAxis_addError(result.diagnostics, "", "", "request", "JOINT_AXIS_REQUEST_REQUIRED",
                 "JointAxisAdapter requires immutable baseline and binding inputs.");
        return result;
    }
    const ParameterBinding& binding = *request.binding;
    if (!isTiltSemantic(binding.semanticKind)) {
        jointAxis_addError(result.diagnostics, binding.id, binding.targetObjectId, "semanticKind",
                 "JOINT_AXIS_SEMANTIC_UNSUPPORTED",
                 "JointAxisAdapter supports tangent-coordinate U/V semantics only.");
        return result;
    }
    if (!std::isfinite(binding.maxAxisTiltAngle) || binding.maxAxisTiltAngle < 0.0 ||
        binding.maxAxisTiltAngle > std::acos(-1.0)) {
        jointAxis_addError(result.diagnostics, binding.id, binding.targetObjectId, "maxAxisTiltAngle",
                 "JOINT_AXIS_TILT_CONE_INVALID",
                 "Joint-axis tilt requires an explicit finite cone in [0, pi] radians.");
        return result;
    }
    if (binding.axisTiltGroupId.empty()) {
        jointAxis_addError(result.diagnostics, binding.id, binding.targetObjectId, "axisTiltGroupId",
                 "JOINT_AXIS_TILT_GROUP_REQUIRED",
                 "Joint-axis tilt requires a stable per-joint U/V group ID.");
        return result;
    }
    if (binding.axisTiltGroupId != "axis-tilt:" + binding.targetObjectId) {
        jointAxis_addError(result.diagnostics, binding.id, binding.targetObjectId, "axisTiltGroupId",
                 "JOINT_AXIS_TILT_GROUP_INVALID",
                 "Joint-axis tilt group IDs must be derived from their typed target joint.");
        return result;
    }
    const JointEdge* joint = findJoint(*request.baseline, binding.targetObjectId);
    if (joint == nullptr || joint->type == CanonicalJointType::Fixed) {
        jointAxis_addError(result.diagnostics, binding.id, binding.targetObjectId, "targetObjectId",
                 "JOINT_AXIS_MOVABLE_JOINT_REQUIRED",
                 "Joint-axis tilt requires a known revolute or prismatic joint.");
        return result;
    }
    if (!validAxis(joint->motionAxisInJoint)) {
        jointAxis_addError(result.diagnostics, binding.id, binding.targetObjectId, "motionAxisInJoint",
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
        jointAxis_addError(result.diagnostics, binding.id, binding.targetObjectId, "values",
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
        jointAxis_addError(result.diagnostics, binding.id, binding.targetObjectId, "values",
                 "JOINT_AXIS_TILT_RESULT_INVALID",
                 "The frozen tangent-coordinate formula must produce a finite unit motion axis.");
        return result;
    }
    result.ok = true;
    result.patch.adapterId = adapterId();
    result.patch.adapterVersion = adapterVersion();
    result.patch.bindingId = binding.id;
    const double scalar = binding.semanticKind == SemanticKind::JointAxisTiltU ? alpha : beta;
    result.patch.writes = {{jointAxis_ownTarget(binding), CandidatePatchValue::scalar(scalar)}};
    return result;
}

std::string JointAxisAdapter::describeEffect(const ParameterBinding&) const
{
    return "Sets one baseline-relative tangent coordinate of the joint motion-axis tilt.";
}

}    // namespace rws
