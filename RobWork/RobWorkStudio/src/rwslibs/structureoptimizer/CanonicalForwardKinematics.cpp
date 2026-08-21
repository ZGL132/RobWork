#include "CanonicalForwardKinematics.hpp"

#include "KinematicConventions.hpp"

#include <map>

namespace rws {
namespace {

void addError(CanonicalForwardKinematicsResult& result,
              const std::string& code,
              const std::string& fieldPath,
              const std::string& message)
{
    result.valid = false;
    StructureOptimizationDiagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.severity = "Error";
    diagnostic.subsystem = "canonical-kinematics";
    diagnostic.stage = "forward-kinematics";
    diagnostic.fieldPath = fieldPath;
    diagnostic.message = message;
    result.diagnostics.push_back(diagnostic);
}

CanonicalJointMotion motionFor(CanonicalJointType type)
{
    switch (type) {
    case CanonicalJointType::Revolute:
        return CanonicalJointMotion::Revolute;
    case CanonicalJointType::Prismatic:
        return CanonicalJointMotion::Prismatic;
    case CanonicalJointType::Fixed:
        return CanonicalJointMotion::Fixed;
    }
    return CanonicalJointMotion::Fixed;
}

}    // namespace

CanonicalForwardKinematicsResult CanonicalForwardKinematics::evaluate(
    const CanonicalKinematicModel& model, const std::vector< double >& q)
{
    CanonicalForwardKinematicsResult result;
    const CanonicalKinematicModelValidationResult validation =
        CanonicalKinematicModelValidator::validate(model);
    if (!validation.valid) {
        result.diagnostics = validation.diagnostics;
        return result;
    }
    if (q.size() != model.dofs.size()) {
        addError(result, "KINEMATIC_FK_Q_DIMENSION_MISMATCH", "q",
                 "The input Q dimension must equal the canonical DOF count.");
        return result;
    }

    std::map< std::string, const JointEdge* > joints;
    for (const JointEdge& joint : model.joints)
        joints[joint.id] = &joint;
    std::map< std::string, const DofDefinition* > dofs;
    for (const DofDefinition& dof : model.dofs)
        dofs[dof.id] = &dof;
    std::map< std::string, const DeviceChain* > chains;
    for (const DeviceChain& chain : model.deviceChains)
        chains[chain.id] = &chain;

    const DeviceChain& chain = *chains.at(model.activeDeviceChainId);
    result.frameTransforms[chain.rootFrameId] = rw::math::Transform3D<>();
    rw::math::Transform3D<> parentTransform = result.frameTransforms[chain.rootFrameId];

    for (const std::string& jointId : chain.orderedJointIds) {
        const JointEdge& joint = *joints.at(jointId);
        double inputCoordinate = 0.0;
        if (joint.type != CanonicalJointType::Fixed)
            inputCoordinate = q[dofs.at(joint.dofId)->qIndex];
        parentTransform = KinematicConventions::composeJointTransform(
            parentTransform * joint.parentToJointZero, motionFor(joint.type), joint.motionAxisInJoint,
            inputCoordinate, joint.zeroPositionOffset, joint.jointMotionToChild);
        result.frameTransforms[joint.childFrameId] = parentTransform;
    }

    for (const ToolBinding& binding : model.toolBindings) {
        const auto flange = result.frameTransforms.find(binding.flangeFrameId);
        if (flange != result.frameTransforms.end())
            result.frameTransforms[binding.tcpFrameId] = flange->second * binding.flangeToTcp;
    }
    result.valid = true;
    return result;
}

bool CanonicalForwardKinematics::frameTransform(const CanonicalForwardKinematicsResult& result,
                                                const std::string& frameId,
                                                rw::math::Transform3D<>& transform,
                                                StructureOptimizationDiagnostic* diagnostic)
{
    const auto found = result.frameTransforms.find(frameId);
    if (found != result.frameTransforms.end()) {
        transform = found->second;
        return true;
    }
    if (diagnostic != nullptr) {
        diagnostic->code = "KINEMATIC_FK_FRAME_NOT_FOUND";
        diagnostic->severity = "Error";
        diagnostic->subsystem = "canonical-kinematics";
        diagnostic->stage = "forward-kinematics";
        diagnostic->fieldPath = "frameId";
        diagnostic->objectId = frameId;
        diagnostic->message = "The requested frame does not have a computed FK transform.";
    }
    return false;
}

}    // namespace rws
