#include "CanonicalKinematicModel.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace rws {
namespace {

void addError(CanonicalKinematicModelValidationResult& result,
              const std::string& code,
              const std::string& fieldPath,
              const std::string& message)
{
    result.valid = false;
    StructureOptimizationDiagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.severity = "Error";
    diagnostic.subsystem = "canonical-kinematics";
    diagnostic.stage = "validation";
    diagnostic.fieldPath = fieldPath;
    diagnostic.message = message;
    result.diagnostics.push_back(diagnostic);
}

bool hasFrame(const std::map< std::string, const FrameNode* >& frames, const std::string& id)
{
    return frames.find(id) != frames.end();
}

bool isMovable(CanonicalJointType type)
{
    return type == CanonicalJointType::Revolute || type == CanonicalJointType::Prismatic;
}

bool isFiniteVector(const rw::math::Vector3D<>& vector)
{
    return std::isfinite(vector(0)) && std::isfinite(vector(1)) &&
           std::isfinite(vector(2));
}

bool isFiniteTransform(const rw::math::Transform3D<>& transform)
{
    if (!isFiniteVector(transform.P())) return false;
    for (std::size_t row = 0; row < 3; ++row)
        for (std::size_t column = 0; column < 3; ++column)
            if (!std::isfinite(transform.R()(row, column))) return false;
    return true;
}

CanonicalCoordinateUnit expectedUnit(CanonicalJointType type)
{
    return type == CanonicalJointType::Prismatic ? CanonicalCoordinateUnit::Metres :
                                                    CanonicalCoordinateUnit::Radians;
}

void validateLimits(CanonicalKinematicModelValidationResult& result,
                    const CanonicalJointLimits& limits,
                    const CanonicalJointType jointType,
                    const std::string& prefix,
                    const char* scope)
{
    if (!limits.enabled)
        return;
    const std::string scopePrefix = std::string("KINEMATIC_") + scope + "_LIMITS_";
    if (jointType == CanonicalJointType::Fixed) {
        addError(result, "KINEMATIC_FIXED_JOINT_LIMITS_FORBIDDEN", prefix,
                 "Fixed joints cannot own enabled physical or operational coordinate limits.");
        return;
    }
    if (!std::isfinite(limits.lower) || !std::isfinite(limits.upper))
        addError(result, scopePrefix + "NONFINITE", prefix,
                 "Enabled joint limits must use finite lower and upper values.");
    else if (!(limits.lower < limits.upper))
        addError(result, scopePrefix + "ORDER_INVALID", prefix,
                 "Enabled joint limits require a strictly ordered lower and upper bound.");
    if (limits.unit != expectedUnit(jointType))
        addError(result, scopePrefix + "UNIT_MISMATCH", prefix + ".unit",
                 "Revolute limits use radians and prismatic limits use metres.");
    if (!isValidJointCoordinateConvention(limits.coordinateConvention))
        addError(result, scopePrefix + "COORDINATE_INVALID", prefix + ".coordinateConvention",
                 "Enabled joint limits must explicitly use q_input or q_model coordinates.");
}

template< class Binding >
void validateGeometryBinding(CanonicalKinematicModelValidationResult& result,
                             const Binding& binding,
                             const std::map< std::string, const FrameNode* >& frames,
                             const std::string& prefix, const char* scope,
                             std::set< std::string >& ids)
{
    const std::string diagnosticPrefix = std::string("KINEMATIC_") + scope + "_";
    if (binding.id.empty() || !ids.insert(binding.id).second)
        addError(result, diagnosticPrefix + "ID_DUPLICATE", prefix + ".id",
                 "Geometry binding IDs must be non-empty and unique within their artifact scope.");
    if (!hasFrame(frames, binding.referenceFrameId))
        addError(result, diagnosticPrefix + "REF_FRAME_INVALID", prefix + ".referenceFrameId",
                 "Geometry bindings must reference a canonical frame.");
    const double values[] = {binding.radius, binding.length, binding.width, binding.height,
                             binding.depth, binding.wallThickness};
    for (const double value : values) {
        if (!std::isfinite(value)) {
            addError(result, diagnosticPrefix + "NONFINITE", prefix,
                     "Geometry dimensions must be finite.");
            return;
        }
    }
    if (binding.kind == CanonicalGeometryKind::Unknown)
        addError(result, diagnosticPrefix + "KIND_UNKNOWN", prefix + ".kind",
                 "Geometry bindings require an explicit primitive or mesh kind.");
    if (binding.kind == CanonicalGeometryKind::Cylinder &&
        !(binding.radius > 0.0 && binding.length > 0.0))
        addError(result, diagnosticPrefix + "CYLINDER_DIMENSIONS_INVALID", prefix,
                 "Cylinder geometry requires positive radius and length.");
    if (binding.kind == CanonicalGeometryKind::Box &&
        !(binding.width > 0.0 && binding.height > 0.0 && binding.depth > 0.0))
        addError(result, diagnosticPrefix + "BOX_DIMENSIONS_INVALID", prefix,
                 "Box geometry requires positive width, height, and depth.");
    if (binding.kind == CanonicalGeometryKind::Tube &&
        !(binding.radius > 0.0 && binding.length > 0.0 && binding.wallThickness > 0.0 &&
          binding.wallThickness < binding.radius))
        addError(result, diagnosticPrefix + "TUBE_DIMENSIONS_INVALID", prefix,
                 "Tube geometry requires positive radius/length and wall thickness below radius.");
}

}    // namespace

CanonicalKinematicModelValidationResult CanonicalKinematicModelValidator::validate(
    const CanonicalKinematicModel& model)
{
    CanonicalKinematicModelValidationResult result;
    std::map< std::string, const FrameNode* > frames;
    for (std::size_t index = 0; index < model.frames.size(); ++index) {
        const FrameNode& frame = model.frames[index];
        const std::string path = "frames[" + std::to_string(index) + "].id";
        if (frame.id.empty() || frames.find(frame.id) != frames.end())
            addError(result, "KINEMATIC_FRAME_ID_DUPLICATE", path,
                     "Frame IDs must be non-empty and unique.");
        else
            frames[frame.id] = &frame;
    }

    if (!hasFrame(frames, model.rootFrameId))
        addError(result, "KINEMATIC_ROOT_FRAME_INVALID", "rootFrameId",
                 "The root frame must reference an existing frame.");
    if (!hasFrame(frames, model.baseFrameId))
        addError(result, "KINEMATIC_BASE_FRAME_INVALID", "baseFrameId",
                 "The base frame must reference an existing frame.");

    std::map< std::string, const JointEdge* > joints;
    std::map< std::string, const JointEdge* > movableDofOwners;
    for (std::size_t index = 0; index < model.joints.size(); ++index) {
        const JointEdge& joint = model.joints[index];
        const std::string prefix = "joints[" + std::to_string(index) + "]";
        if (joint.id.empty() || joints.find(joint.id) != joints.end())
            addError(result, "KINEMATIC_JOINT_ID_DUPLICATE", prefix + ".id",
                     "Joint IDs must be non-empty and unique.");
        else
            joints[joint.id] = &joint;
        if (!hasFrame(frames, joint.parentFrameId) || !hasFrame(frames, joint.childFrameId))
            addError(result, "KINEMATIC_JOINT_FRAME_REFERENCE_INVALID", prefix,
                     "A joint must reference existing parent and child frames.");
        if (!isFiniteTransform(joint.parentToJointZero) ||
            !isFiniteTransform(joint.jointMotionToChild))
            addError(result, "KINEMATIC_JOINT_TRANSFORM_NONFINITE", prefix,
                     "Joint transforms must contain only finite values.");
        if (!isFiniteVector(joint.motionAxisInJoint))
            addError(result, "KINEMATIC_JOINT_AXIS_NONFINITE", prefix + ".motionAxisInJoint",
                     "Joint motion axes must contain only finite values.");
        else if (isMovable(joint.type) && joint.motionAxisInJoint.norm2() <= 1e-12)
            addError(result, "KINEMATIC_JOINT_AXIS_ZERO", prefix + ".motionAxisInJoint",
                     "Movable joint motion axes must be non-zero.");
        if (!std::isfinite(joint.zeroPositionOffset))
            addError(result, "KINEMATIC_JOINT_OFFSET_NONFINITE", prefix + ".zeroPositionOffset",
                     "Joint zero-position offsets must be finite.");
        if (joint.type == CanonicalJointType::Fixed && !joint.dofId.empty())
            addError(result, "KINEMATIC_FIXED_JOINT_HAS_DOF", prefix + ".dofId",
                     "A fixed joint must not own a degree of freedom.");
        if (isMovable(joint.type) && joint.dofId.empty())
            addError(result, "KINEMATIC_MOVABLE_JOINT_MISSING_DOF", prefix + ".dofId",
                     "A movable joint must own a degree of freedom.");
        if (isMovable(joint.type) && !joint.dofId.empty()) {
            if (movableDofOwners.find(joint.dofId) != movableDofOwners.end())
                addError(result, "KINEMATIC_MOVABLE_JOINT_DOF_DUPLICATE", prefix + ".dofId",
                         "Each movable joint must own a unique degree of freedom.");
            else
                movableDofOwners[joint.dofId] = &joint;
        }
        validateLimits(result, joint.physicalLimits, joint.type, prefix + ".physicalLimits",
                       "PHYSICAL");
        validateLimits(result, joint.operationalLimits, joint.type, prefix + ".operationalLimits",
                       "OPERATIONAL");
    }

    std::map< std::string, const DofDefinition* > dofs;
    std::set< std::size_t > qIndices;
    for (std::size_t index = 0; index < model.dofs.size(); ++index) {
        const DofDefinition& dof = model.dofs[index];
        const std::string prefix = "dofs[" + std::to_string(index) + "]";
        if (dof.id.empty() || dofs.find(dof.id) != dofs.end())
            addError(result, "KINEMATIC_DOF_ID_DUPLICATE", prefix + ".id",
                     "DOF IDs must be non-empty and unique.");
        else
            dofs[dof.id] = &dof;
        if (!qIndices.insert(dof.qIndex).second)
            addError(result, "KINEMATIC_DOF_QINDEX_DUPLICATE", prefix + ".qIndex",
                     "Each active DOF must use a unique Q index.");
        const auto joint = joints.find(dof.jointId);
        if (joint == joints.end() || !isMovable(dof.type) || joint->second->type != dof.type ||
            joint->second->dofId != dof.id) {
            addError(result, "KINEMATIC_DOF_JOINT_MAPPING_INVALID", prefix + ".jointId",
                     "A DOF must map one-to-one to a movable joint of the same type.");
        }
        if (dof.unit != expectedUnit(dof.type))
            addError(result, "KINEMATIC_DOF_UNIT_MISMATCH", prefix + ".unit",
                     "Revolute DOFs use radians and prismatic DOFs use metres.");
    }
    for (std::size_t expected = 0; expected < model.dofs.size(); ++expected) {
        if (qIndices.find(expected) == qIndices.end()) {
            addError(result, "KINEMATIC_DOF_QINDEX_NOT_CONTIGUOUS", "dofs",
                     "Q indices must form a contiguous range beginning at zero.");
            break;
        }
    }

    std::map< std::string, const DeviceChain* > chains;
    for (std::size_t index = 0; index < model.deviceChains.size(); ++index) {
        const DeviceChain& chain = model.deviceChains[index];
        const std::string prefix = "deviceChains[" + std::to_string(index) + "]";
        if (chain.id.empty() || chains.find(chain.id) != chains.end())
            addError(result, "KINEMATIC_CHAIN_ID_DUPLICATE", prefix + ".id",
                     "Device-chain IDs must be non-empty and unique.");
        else
            chains[chain.id] = &chain;
        if (!hasFrame(frames, chain.rootFrameId) || !hasFrame(frames, chain.tipFrameId)) {
            addError(result, "KINEMATIC_CHAIN_FRAME_REFERENCE_INVALID", prefix,
                     "A device chain must reference existing root and tip frames.");
            continue;
        }
        std::string currentFrame = chain.rootFrameId;
        std::vector< std::string > chainDofs;
        bool connected = true;
        for (const std::string& jointId : chain.orderedJointIds) {
            const auto joint = joints.find(jointId);
            if (joint == joints.end() || joint->second->parentFrameId != currentFrame) {
                connected = false;
                break;
            }
            currentFrame = joint->second->childFrameId;
            if (isMovable(joint->second->type))
                chainDofs.push_back(joint->second->dofId);
        }
        if (!connected || currentFrame != chain.tipFrameId)
            addError(result, "KINEMATIC_CHAIN_DISCONNECTED", prefix,
                     "Ordered joints must form a continuous path from root to tip.");
        if (chain.orderedDofIds != chainDofs)
            addError(result, "KINEMATIC_CHAIN_DOF_ORDER_INVALID", prefix + ".orderedDofIds",
                     "The chain DOF order must match its movable joint order.");
    }
    if (chains.find(model.activeDeviceChainId) == chains.end())
        addError(result, "KINEMATIC_ACTIVE_CHAIN_INVALID", "activeDeviceChainId",
                 "The active device chain must reference an existing chain.");

    std::set< std::string > toolBindingIds;
    for (std::size_t index = 0; index < model.toolBindings.size(); ++index) {
        const ToolBinding& binding = model.toolBindings[index];
        const std::string prefix = "toolBindings[" + std::to_string(index) + "]";
        if (binding.id.empty() || !toolBindingIds.insert(binding.id).second)
            addError(result, "KINEMATIC_TOOL_BINDING_ID_DUPLICATE", prefix + ".id",
                     "Tool binding IDs must be non-empty and unique.");
        const auto flange = frames.find(binding.flangeFrameId);
        const auto tcp = frames.find(binding.tcpFrameId);
        if (flange == frames.end() || tcp == frames.end() ||
            flange->second->type != CanonicalFrameType::Flange ||
            tcp->second->type != CanonicalFrameType::Tool)
            addError(result, "KINEMATIC_TOOL_BINDING_INVALID", prefix,
                     "A tool binding must reference a Flange frame and a Tool frame.");
        if (!isFiniteTransform(binding.flangeToTcp))
            addError(result, "KINEMATIC_TOOL_TRANSFORM_NONFINITE", prefix + ".flangeToTcp",
                     "Tool transforms must contain only finite values.");
    }
    std::set< std::string > geometryIds;
    for (std::size_t index = 0; index < model.geometryBindings.size(); ++index)
        validateGeometryBinding(result, model.geometryBindings[index], frames,
                                "geometryBindings[" + std::to_string(index) + "]", "GEOMETRY",
                                geometryIds);
    std::set< std::string > collisionIds;
    for (std::size_t index = 0; index < model.collisionBindings.size(); ++index)
        validateGeometryBinding(result, model.collisionBindings[index], frames,
                                "collisionBindings[" + std::to_string(index) + "]", "COLLISION",
                                collisionIds);
    return result;
}

std::string jointCoordinateConventionToString(JointCoordinateConvention convention)
{
    switch (convention) {
    case JointCoordinateConvention::QInput: return "QInput";
    case JointCoordinateConvention::QModel: return "QModel";
    case JointCoordinateConvention::Unknown: default: return "Unknown";
    }
}

bool jointCoordinateConventionFromString(const std::string& value,
                                         JointCoordinateConvention& convention)
{
    if (value == "QInput") { convention = JointCoordinateConvention::QInput; return true; }
    if (value == "QModel") { convention = JointCoordinateConvention::QModel; return true; }
    if (value == "Unknown") { convention = JointCoordinateConvention::Unknown; return true; }
    return false;
}

bool isValidJointCoordinateConvention(const JointCoordinateConvention convention)
{
    return convention == JointCoordinateConvention::QInput ||
           convention == JointCoordinateConvention::QModel;
}

}    // namespace rws
