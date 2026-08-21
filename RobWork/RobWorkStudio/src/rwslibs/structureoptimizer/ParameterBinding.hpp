#ifndef RWS_STRUCTUREOPTIMIZATION_PARAMETERBINDING_HPP
#define RWS_STRUCTUREOPTIMIZATION_PARAMETERBINDING_HPP

#include "DesignVariable.hpp"
#include "CanonicalKinematicModel.hpp"

#include <rw/math/Vector3D.hpp>

#include <limits>

namespace rws {

enum class TargetObjectType { Unknown, Frame, Joint, Dof, DeviceChain, ToolBinding, Geometry, CollisionGeometry };
enum class TargetPropertyId {
    Unknown,
    ParentToJointTranslationX, ParentToJointTranslationY, ParentToJointTranslationZ,
    MotionAxisTiltU, MotionAxisTiltV, ZeroPositionOffset,
    PhysicalLimitLower, PhysicalLimitUpper,
    OperationalLimitLower, OperationalLimitUpper,
    BaseTranslationX, BaseTranslationY, BaseTranslationZ,
    BaseRotationVectorX, BaseRotationVectorY, BaseRotationVectorZ,
    ParentToFlangeTranslationX, ParentToFlangeTranslationY, ParentToFlangeTranslationZ,
    ParentToFlangeRotationVectorX, ParentToFlangeRotationVectorY, ParentToFlangeRotationVectorZ,
    FlangeToTcpTranslationX, FlangeToTcpTranslationY, FlangeToTcpTranslationZ,
    FlangeToTcpRotationVectorX, FlangeToTcpRotationVectorY, FlangeToTcpRotationVectorZ,
    GeometryRadius, GeometryLength, GeometryWidth, GeometryHeight, GeometryDepth,
    GeometryWallThickness, GeometryRigidTransform, GeometryScale, Material
};

/** A limit change is mechanically physical or an operational policy bound; never implicit. */
enum class JointLimitScope { Unknown, Physical, Operational };

/** Explicit SO(3) composition side for a rotation-vector pose delta. */
enum class PoseDeltaComposition { Unknown, Right };

struct ReadWriteTarget
{
    TargetObjectType objectType = TargetObjectType::Unknown;
    std::string objectId;
    TargetPropertyId propertyId = TargetPropertyId::Unknown;
    std::string coordinateFrameId;

    bool operator==(const ReadWriteTarget& other) const;
};

struct ParameterBinding
{
    std::string id;
    SemanticKind semanticKind = SemanticKind::Unknown;
    TargetObjectType targetObjectType = TargetObjectType::Unknown;
    std::string targetObjectId;
    TargetPropertyId targetPropertyId = TargetPropertyId::Unknown;
    std::string coordinateFrameId;
    std::string parameterizationModeId;
    std::string ownerAdapterId;
    /** Version of the adapter contract that owns this binding; checked against the registry. */
    int ownerAdapterVersion = 0;
    std::vector< std::string > requiredCapabilityIds;
    std::vector< ReadWriteTarget > readSet;
    std::vector< ReadWriteTarget > writeSet;
    /** LinkLength direction in the target joint's parent frame; never implicit. */
    std::string referenceDirectionFrameId;
    rw::math::Vector3D<> referenceDirection;
    /** Explicit radians cone for MotionAxisTiltU/V; no implicit default is permitted. */
    double maxAxisTiltAngle = std::numeric_limits< double >::quiet_NaN();
    /** Stable per-joint identity coupling the U and V tangent coordinates. */
    std::string axisTiltGroupId;
    /** Explicitly distinguishes mechanical bounds from operational policy bounds. */
    JointLimitScope jointLimitScope = JointLimitScope::Unknown;
    /** Stable lower/upper pair identity; must be `joint-limits:<jointId>`. */
    std::string jointLimitGroupId;
    /** Positive finite range required between the resolved lower and upper values. */
    double minimumJointLimitRange = std::numeric_limits< double >::quiet_NaN();
    /** Physical limits remain locked unless a project binding explicitly authorizes them. */
    bool allowPhysicalLimitModification = false;
    /** Explicit absolute mechanical envelope for either scoped limit pair. */
    double absoluteJointLimitLower = std::numeric_limits< double >::quiet_NaN();
    double absoluteJointLimitUpper = std::numeric_limits< double >::quiet_NaN();
    /** The Q coordinate in which this pair's bounds are expressed; never a spatial frame. */
    JointCoordinateConvention jointLimitCoordinateConvention =
        JointCoordinateConvention::Unknown;
    /** Stable owner-local identity coupling the translation/rotation pose coordinates. */
    std::string poseDeltaGroupId;
    /** S34 freezes all pose updates to right-multiplied SO(3) increments. */
    PoseDeltaComposition poseDeltaComposition = PoseDeltaComposition::Unknown;
    /** Geometry dimensions in one primitive are coupled only by explicit identity. */
    std::string geometryGroupId;
    int bindingVersion = 1;
    std::string displayPath;

    /** Compiler identity intentionally excludes UI-only displayPath. */
    bool runtimeEquals(const ParameterBinding& other) const;
};

struct ParameterBindingValidationResult
{
    bool valid = true;
    std::vector< StructureOptimizationDiagnostic > diagnostics;
};

class ParameterBindingValidator
{
  public:
    static ParameterBindingValidationResult validate(const ParameterBinding& binding);
};

std::string targetObjectTypeToString(TargetObjectType type);
bool targetObjectTypeFromString(const std::string& value, TargetObjectType& type);
std::string targetPropertyIdToString(TargetPropertyId property);
bool targetPropertyIdFromString(const std::string& value, TargetPropertyId& property);
std::string jointLimitScopeToString(JointLimitScope scope);
bool jointLimitScopeFromString(const std::string& value, JointLimitScope& scope);

}    // namespace rws

#endif    // RWS_STRUCTUREOPTIMIZATION_PARAMETERBINDING_HPP
