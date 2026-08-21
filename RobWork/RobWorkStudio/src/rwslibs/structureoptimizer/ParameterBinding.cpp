#include "ParameterBinding.hpp"

#include <cmath>

namespace rws {
namespace {

void addError(ParameterBindingValidationResult& result, const std::string& code,
              const std::string& fieldPath, const std::string& message)
{
    result.valid = false;
    StructureOptimizationDiagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.severity = "Error";
    diagnostic.subsystem = "design-space";
    diagnostic.stage = "binding-validation";
    diagnostic.fieldPath = fieldPath;
    diagnostic.message = message;
    result.diagnostics.push_back(diagnostic);
}

bool validTarget(const ReadWriteTarget& target)
{
    return target.objectType != TargetObjectType::Unknown && !target.objectId.empty() &&
           target.propertyId != TargetPropertyId::Unknown;
}

const double kPi = std::acos(-1.0);

bool sameRuntimeDouble(const double first, const double second)
{
    return first == second || (std::isnan(first) && std::isnan(second));
}

bool isJointLimitSemantic(const SemanticKind semantic)
{
    return semantic == SemanticKind::JointLimitLower || semantic == SemanticKind::JointLimitUpper;
}

TargetPropertyId expectedJointLimitProperty(const SemanticKind semantic,
                                            const JointLimitScope scope)
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

}    // namespace

bool ReadWriteTarget::operator==(const ReadWriteTarget& other) const
{
    return objectType == other.objectType && objectId == other.objectId &&
           propertyId == other.propertyId && coordinateFrameId == other.coordinateFrameId;
}

bool ParameterBinding::runtimeEquals(const ParameterBinding& other) const
{
    return id == other.id && semanticKind == other.semanticKind &&
           targetObjectType == other.targetObjectType && targetObjectId == other.targetObjectId &&
           targetPropertyId == other.targetPropertyId && coordinateFrameId == other.coordinateFrameId &&
           parameterizationModeId == other.parameterizationModeId &&
           ownerAdapterId == other.ownerAdapterId && ownerAdapterVersion == other.ownerAdapterVersion &&
           requiredCapabilityIds == other.requiredCapabilityIds && readSet == other.readSet &&
           writeSet == other.writeSet && referenceDirectionFrameId == other.referenceDirectionFrameId &&
           referenceDirection(0) == other.referenceDirection(0) &&
           referenceDirection(1) == other.referenceDirection(1) &&
           referenceDirection(2) == other.referenceDirection(2) &&
           sameRuntimeDouble(maxAxisTiltAngle, other.maxAxisTiltAngle) &&
           axisTiltGroupId == other.axisTiltGroupId &&
           jointLimitScope == other.jointLimitScope &&
           jointLimitGroupId == other.jointLimitGroupId &&
           sameRuntimeDouble(minimumJointLimitRange, other.minimumJointLimitRange) &&
           allowPhysicalLimitModification == other.allowPhysicalLimitModification &&
           sameRuntimeDouble(absoluteJointLimitLower, other.absoluteJointLimitLower) &&
           sameRuntimeDouble(absoluteJointLimitUpper, other.absoluteJointLimitUpper) &&
           jointLimitCoordinateConvention == other.jointLimitCoordinateConvention &&
           poseDeltaGroupId == other.poseDeltaGroupId &&
           poseDeltaComposition == other.poseDeltaComposition &&
           geometryGroupId == other.geometryGroupId &&
           bindingVersion == other.bindingVersion;
}

ParameterBindingValidationResult ParameterBindingValidator::validate(const ParameterBinding& binding)
{
    ParameterBindingValidationResult result;
    if (binding.id.empty())
        addError(result, "PARAMETER_BINDING_ID_REQUIRED", "id", "A binding requires a stable ID.");
    if (binding.semanticKind == SemanticKind::Unknown)
        addError(result, "PARAMETER_BINDING_SEMANTIC_UNKNOWN", "semanticKind",
                 "A binding must use a registered semantic kind.");
    if (binding.targetObjectType == TargetObjectType::Unknown || binding.targetObjectId.empty())
        addError(result, "PARAMETER_BINDING_TARGET_OBJECT_REQUIRED", "targetObjectId",
                 "A binding must name a typed target object.");
    if (binding.targetPropertyId == TargetPropertyId::Unknown)
        addError(result, "PARAMETER_BINDING_TARGET_PROPERTY_REQUIRED", "targetPropertyId",
                 "A binding must name a typed target property.");
    if (binding.ownerAdapterId.empty())
        addError(result, "PARAMETER_BINDING_OWNER_REQUIRED", "ownerAdapterId",
                 "A binding must name its owning adapter.");
    if (binding.ownerAdapterVersion <= 0)
        addError(result, "PARAMETER_BINDING_OWNER_VERSION_INVALID", "ownerAdapterVersion",
                 "A binding must name a positive owning adapter version.");
    if (binding.bindingVersion <= 0)
        addError(result, "PARAMETER_BINDING_VERSION_INVALID", "bindingVersion",
                 "A binding version must be positive.");
    if (binding.semanticKind == SemanticKind::LinkLength) {
        const double directionNorm = binding.referenceDirection.norm2();
        if (binding.referenceDirectionFrameId.empty())
            addError(result, "PARAMETER_BINDING_REFERENCE_DIRECTION_FRAME_REQUIRED",
                     "referenceDirectionFrameId",
                     "LinkLength bindings require an explicit parent-frame reference direction.");
        if (!std::isfinite(binding.referenceDirection(0)) ||
            !std::isfinite(binding.referenceDirection(1)) ||
            !std::isfinite(binding.referenceDirection(2)) ||
            std::fabs(directionNorm - 1.0) > 1e-9)
            addError(result, "PARAMETER_BINDING_REFERENCE_DIRECTION_INVALID", "referenceDirection",
                     "LinkLength reference directions must be finite unit vectors.");
    }
    if (binding.semanticKind == SemanticKind::JointAxisTiltU ||
        binding.semanticKind == SemanticKind::JointAxisTiltV) {
        if (!std::isfinite(binding.maxAxisTiltAngle) || binding.maxAxisTiltAngle < 0.0 ||
            binding.maxAxisTiltAngle > kPi)
            addError(result, "PARAMETER_BINDING_AXIS_TILT_CONE_INVALID", "maxAxisTiltAngle",
                     "Joint-axis tilt bindings require an explicit finite cone in [0, pi] radians.");
        if (binding.axisTiltGroupId.empty())
            addError(result, "PARAMETER_BINDING_AXIS_TILT_GROUP_REQUIRED", "axisTiltGroupId",
                     "Joint-axis tilt bindings require a stable per-joint tangent-coordinate group ID.");
        else if (binding.axisTiltGroupId != "axis-tilt:" + binding.targetObjectId)
            addError(result, "PARAMETER_BINDING_AXIS_TILT_GROUP_INVALID", "axisTiltGroupId",
                     "Joint-axis tilt group IDs must be derived from their typed target joint.");
    }
    if (isJointLimitSemantic(binding.semanticKind)) {
        if (binding.jointLimitScope == JointLimitScope::Unknown)
            addError(result, "PARAMETER_BINDING_JOINT_LIMIT_SCOPE_REQUIRED", "jointLimitScope",
                     "Joint-limit bindings require an explicit physical or operational scope.");
        if (binding.jointLimitGroupId.empty())
            addError(result, "PARAMETER_BINDING_JOINT_LIMIT_GROUP_REQUIRED", "jointLimitGroupId",
                     "Joint-limit bindings require a stable lower/upper group ID.");
        else if (binding.jointLimitGroupId != "joint-limits:" + binding.targetObjectId)
            addError(result, "PARAMETER_BINDING_JOINT_LIMIT_GROUP_INVALID", "jointLimitGroupId",
                     "Joint-limit group IDs must be derived from their typed target joint.");
        if (!std::isfinite(binding.minimumJointLimitRange) ||
            binding.minimumJointLimitRange <= 0.0)
            addError(result, "PARAMETER_BINDING_JOINT_LIMIT_MINIMUM_RANGE_INVALID",
                     "minimumJointLimitRange",
                     "Joint-limit bindings require a finite positive minimum range.");
        if (!std::isfinite(binding.absoluteJointLimitLower) ||
            !std::isfinite(binding.absoluteJointLimitUpper) ||
            !(binding.absoluteJointLimitLower < binding.absoluteJointLimitUpper))
            addError(result, "PARAMETER_BINDING_JOINT_LIMIT_ABSOLUTE_BOUNDS_INVALID",
                     "absoluteJointLimitLower/absoluteJointLimitUpper",
                     "Joint-limit bindings require explicit finite ordered absolute bounds.");
        if (binding.jointLimitScope == JointLimitScope::Physical &&
            !binding.allowPhysicalLimitModification)
            addError(result, "PARAMETER_BINDING_JOINT_LIMIT_PHYSICAL_LOCKED",
                     "allowPhysicalLimitModification",
                     "Physical mechanical limits require explicit project authorization.");
        if (binding.targetPropertyId != expectedJointLimitProperty(binding.semanticKind,
                                                                     binding.jointLimitScope))
            addError(result, "PARAMETER_BINDING_JOINT_LIMIT_PROPERTY_INVALID", "targetPropertyId",
                     "Joint-limit semantics require the matching typed scoped limit property.");
        if (!isValidJointCoordinateConvention(binding.jointLimitCoordinateConvention))
            addError(result, "PARAMETER_BINDING_JOINT_LIMIT_COORDINATE_INVALID",
                     "jointLimitCoordinateConvention",
                     "Joint-limit bindings must explicitly declare a valid q_input or q_model coordinate.");
    }
    for (std::size_t index = 0; index < binding.readSet.size(); ++index)
        if (!validTarget(binding.readSet[index]))
            addError(result, "PARAMETER_BINDING_READ_TARGET_INVALID",
                     "readSet[" + std::to_string(index) + "]",
                     "Read sets contain typed object and property targets.");
    for (std::size_t index = 0; index < binding.writeSet.size(); ++index)
        if (!validTarget(binding.writeSet[index]))
            addError(result, "PARAMETER_BINDING_WRITE_TARGET_INVALID",
                     "writeSet[" + std::to_string(index) + "]",
                     "Write sets contain typed object and property targets.");
    return result;
}

std::string targetObjectTypeToString(TargetObjectType type)
{
    static const char* const values[] = {"Unknown", "Frame", "Joint", "Dof", "DeviceChain",
                                         "ToolBinding", "Geometry", "CollisionGeometry"};
    const int index = static_cast< int >(type);
    return index >= 0 && index < static_cast< int >(sizeof(values) / sizeof(values[0])) ?
        values[index] : "Unknown";
}

bool targetObjectTypeFromString(const std::string& value, TargetObjectType& type)
{
    for (int index = static_cast< int >(TargetObjectType::Frame);
         index <= static_cast< int >(TargetObjectType::CollisionGeometry); ++index) {
        const TargetObjectType candidate = static_cast< TargetObjectType >(index);
        if (targetObjectTypeToString(candidate) == value) { type = candidate; return true; }
    }
    return false;
}

std::string targetPropertyIdToString(TargetPropertyId property)
{
    static const char* const values[] = {
        "Unknown", "ParentToJointTranslationX", "ParentToJointTranslationY",
        "ParentToJointTranslationZ", "MotionAxisTiltU", "MotionAxisTiltV",
        "ZeroPositionOffset", "PhysicalLimitLower", "PhysicalLimitUpper",
        "OperationalLimitLower", "OperationalLimitUpper",
        "BaseTranslationX", "BaseTranslationY", "BaseTranslationZ", "BaseRotationVectorX",
        "BaseRotationVectorY", "BaseRotationVectorZ", "ParentToFlangeTranslationX",
        "ParentToFlangeTranslationY", "ParentToFlangeTranslationZ",
        "ParentToFlangeRotationVectorX", "ParentToFlangeRotationVectorY",
        "ParentToFlangeRotationVectorZ", "FlangeToTcpTranslationX",
        "FlangeToTcpTranslationY", "FlangeToTcpTranslationZ", "FlangeToTcpRotationVectorX",
        "FlangeToTcpRotationVectorY", "FlangeToTcpRotationVectorZ", "GeometryRadius",
        "GeometryLength", "GeometryWidth", "GeometryHeight", "GeometryDepth",
        "GeometryWallThickness", "GeometryRigidTransform", "GeometryScale", "Material"};
    const int index = static_cast< int >(property);
    return index >= 0 && index < static_cast< int >(sizeof(values) / sizeof(values[0])) ?
        values[index] : "Unknown";
}

bool targetPropertyIdFromString(const std::string& value, TargetPropertyId& property)
{
    for (int index = static_cast< int >(TargetPropertyId::ParentToJointTranslationX);
         index <= static_cast< int >(TargetPropertyId::Material); ++index) {
        const TargetPropertyId candidate = static_cast< TargetPropertyId >(index);
        if (targetPropertyIdToString(candidate) == value) { property = candidate; return true; }
    }
    return false;
}

std::string jointLimitScopeToString(JointLimitScope scope)
{
    switch (scope) {
    case JointLimitScope::Physical: return "Physical";
    case JointLimitScope::Operational: return "Operational";
    case JointLimitScope::Unknown: default: return "Unknown";
    }
}

bool jointLimitScopeFromString(const std::string& value, JointLimitScope& scope)
{
    if (value == "Physical") { scope = JointLimitScope::Physical; return true; }
    if (value == "Operational") { scope = JointLimitScope::Operational; return true; }
    return false;
}

}    // namespace rws
