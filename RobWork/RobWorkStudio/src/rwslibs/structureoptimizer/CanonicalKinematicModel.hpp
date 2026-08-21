#ifndef RWS_STRUCTUREOPTIMIZATION_CANONICALKINEMATICMODEL_HPP
#define RWS_STRUCTUREOPTIMIZATION_CANONICALKINEMATICMODEL_HPP

#include "StructureOptimizationContracts.hpp"

#include <rw/math/Transform3D.hpp>
#include <rw/math/Vector3D.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace rws {

/** Frame roles are topological annotations; only JointEdge instances own motion. */
enum class CanonicalFrameType
{
    Base,
    Link,
    Fixed,
    Flange,
    Tool,
    Auxiliary
};

/** Joint motion semantic used by the canonical SE(3) model. */
enum class CanonicalJointType
{
    Revolute,
    Prismatic,
    Fixed
};

/** Explicit coordinate unit for an active degree of freedom. */
enum class CanonicalCoordinateUnit
{
    Radians,
    Metres
};

/** Explicit semantic coordinate for bounds: raw input Q or q_input + zero offset. */
enum class JointCoordinateConvention
{
    Unknown,
    QInput,
    QModel
};

struct FrameNode
{
    std::string id;
    std::string name;
    CanonicalFrameType type = CanonicalFrameType::Auxiliary;
    std::string sourceObjectId;
};

struct CanonicalJointLimits
{
    bool enabled = false;
    double lower = 0.0;
    double upper = 0.0;
    CanonicalCoordinateUnit unit = CanonicalCoordinateUnit::Radians;
    /** Never infer whether a bound applies before or after the zero offset. */
    JointCoordinateConvention coordinateConvention = JointCoordinateConvention::Unknown;
};

/**
 * Parent-to-child joint relation.  The full SE(3) transforms and the explicit
 * axis are the only kinematic truth; no Euler, DH, or nominal-length field is
 * present in this POD.
 */
struct JointEdge
{
    std::string id;
    std::string name;
    CanonicalJointType type = CanonicalJointType::Fixed;
    std::string parentFrameId;
    std::string childFrameId;
    rw::math::Transform3D<> parentToJointZero;
    rw::math::Vector3D<> motionAxisInJoint = rw::math::Vector3D<>::z();
    rw::math::Transform3D<> jointMotionToChild;
    double zeroPositionOffset = 0.0;
    CanonicalJointLimits physicalLimits;
    CanonicalJointLimits operationalLimits;
    std::string dofId;
    std::string sourceObjectId;
};

struct DofDefinition
{
    std::string id;
    std::string jointId;
    std::size_t qIndex = 0;
    CanonicalJointType type = CanonicalJointType::Fixed;
    CanonicalCoordinateUnit unit = CanonicalCoordinateUnit::Radians;
};

struct DeviceChain
{
    std::string id;
    std::string rootFrameId;
    std::string tipFrameId;
    std::vector< std::string > orderedJointIds;
    std::vector< std::string > orderedDofIds;
};

struct ToolBinding
{
    std::string id;
    std::string flangeFrameId;
    std::string tcpFrameId;
    rw::math::Transform3D<> flangeToTcp;
    std::vector< std::string > geometryBindingIds;
    std::vector< std::string > collisionBindingIds;
};

/** Parametric visual geometry owned by the canonical model, never by the UI. */
enum class CanonicalGeometryKind { Unknown, Cylinder, Box, Tube, Mesh };

struct GeometryBinding
{
    std::string id;
    std::string referenceFrameId;
    CanonicalGeometryKind kind = CanonicalGeometryKind::Unknown;
    /** User-authored geometry stays immutable unless this binding explicitly owns it. */
    bool optimizationOwned = false;
    /** Meshes have no inferred section; only an explicitly enabled rigid transform is legal. */
    bool allowRigidTransform = false;
    rw::math::Transform3D<> referenceToGeometry;
    double radius = 0.0;
    double length = 0.0;
    double width = 0.0;
    double height = 0.0;
    double depth = 0.0;
    double wallThickness = 0.0;
    std::string sourceObjectId;
};

/** Collision geometry remains independently validated; visual geometry is no fallback. */
struct CollisionBinding
{
    std::string id;
    std::string referenceFrameId;
    CanonicalGeometryKind kind = CanonicalGeometryKind::Unknown;
    bool optimizationOwned = false;
    bool allowRigidTransform = false;
    rw::math::Transform3D<> referenceToGeometry;
    double radius = 0.0;
    double length = 0.0;
    double width = 0.0;
    double height = 0.0;
    double depth = 0.0;
    double wallThickness = 0.0;
    std::string sourceObjectId;
};

/**
 * Immutable-by-convention data representation for imported kinematics.  It is
 * deliberately independent from Qt Widgets and RobWork WorkCell ownership.
 */
struct CanonicalKinematicModel
{
    int schemaVersion = 1;
    std::string modelId;
    std::string sourceFingerprint;
    std::string environmentFingerprint;
    std::string rootFrameId;
    std::string baseFrameId;
    std::string activeDeviceChainId;
    std::vector< FrameNode > frames;
    std::vector< JointEdge > joints;
    std::vector< DofDefinition > dofs;
    std::vector< DeviceChain > deviceChains;
    std::vector< ToolBinding > toolBindings;
    std::vector< GeometryBinding > geometryBindings;
    std::vector< CollisionBinding > collisionBindings;
};

struct CanonicalKinematicModelValidationResult
{
    bool valid = true;
    std::vector< StructureOptimizationDiagnostic > diagnostics;
};

/** Pure validator for canonical topology and its explicit Q mapping. */
class CanonicalKinematicModelValidator
{
  public:
    static CanonicalKinematicModelValidationResult validate(const CanonicalKinematicModel& model);
};

std::string jointCoordinateConventionToString(JointCoordinateConvention convention);
bool jointCoordinateConventionFromString(const std::string& value,
                                         JointCoordinateConvention& convention);
bool isValidJointCoordinateConvention(JointCoordinateConvention convention);

}    // namespace rws

#endif    // RWS_STRUCTUREOPTIMIZATION_CANONICALKINEMATICMODEL_HPP
