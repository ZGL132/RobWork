#include "CandidatePatchApply.hpp"

#include "KinematicConventions.hpp"
#include "PoseDelta.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace rws {
namespace {

void addError(std::vector< StructureOptimizationDiagnostic >& diagnostics,
              const CandidatePatch& patch, const ReadWriteTarget& target,
              const char* code, const char* message)
{
    diagnostics.push_back(makeAdapterDiagnostic(
        patch.adapterId, patch.bindingId, target.objectId, "writes",
        code, message));
}

bool scalarWrite(const CandidatePatchWrite& write, double& value,
                 std::vector< StructureOptimizationDiagnostic >& diagnostics,
                 const CandidatePatch& patch)
{
    if (write.value.kind != CandidatePatchValue::Kind::Scalar ||
        !std::isfinite(write.value.scalarValue)) {
        addError(diagnostics, patch, write.target,
                 "CANDIDATE_PATCH_SCALAR_REQUIRED",
                 "This canonical target requires one finite scalar patch value.");
        return false;
    }
    value = write.value.scalarValue;
    return true;
}

JointEdge* findJoint(CanonicalKinematicModel& model, const std::string& id)
{
    for (JointEdge& joint : model.joints)
        if (joint.id == id)
            return &joint;
    return nullptr;
}

const JointEdge* incomingFixedJoint(const CanonicalKinematicModel& model,
                                    const std::string& frameId)
{
    const JointEdge* found = nullptr;
    for (const JointEdge& joint : model.joints) {
        if (joint.childFrameId != frameId)
            continue;
        if (found != nullptr || joint.type != CanonicalJointType::Fixed)
            return nullptr;
        found = &joint;
    }
    return found;
}

JointEdge* incomingFixedJoint(CanonicalKinematicModel& model,
                              const std::string& frameId)
{
    const JointEdge* found = incomingFixedJoint(
        static_cast<const CanonicalKinematicModel&>(model), frameId);
    return found == nullptr ? nullptr : findJoint(model, found->id);
}

GeometryBinding* findGeometry(CanonicalKinematicModel& model, const std::string& id)
{
    for (GeometryBinding& geometry : model.geometryBindings)
        if (geometry.id == id)
            return &geometry;
    return nullptr;
}

CollisionBinding* findCollision(CanonicalKinematicModel& model, const std::string& id)
{
    for (CollisionBinding& collision : model.collisionBindings)
        if (collision.id == id)
            return &collision;
    return nullptr;
}

ToolBinding* findTool(CanonicalKinematicModel& model, const std::string& id)
{
    for (ToolBinding& tool : model.toolBindings)
        if (tool.id == id)
            return &tool;
    return nullptr;
}

bool frameExists(const CanonicalKinematicModel& model, const std::string& id)
{
    return std::any_of(model.frames.begin(), model.frames.end(),
                       [&id](const FrameNode& frame) { return frame.id == id; });
}

bool isJointTranslation(TargetPropertyId property)
{
    return property == TargetPropertyId::ParentToJointTranslationX ||
           property == TargetPropertyId::ParentToJointTranslationY ||
           property == TargetPropertyId::ParentToJointTranslationZ;
}

bool isAxisTilt(TargetPropertyId property)
{
    return property == TargetPropertyId::MotionAxisTiltU ||
           property == TargetPropertyId::MotionAxisTiltV;
}

bool isPoseRotation(TargetPropertyId property)
{
    return property == TargetPropertyId::BaseRotationVectorX ||
           property == TargetPropertyId::BaseRotationVectorY ||
           property == TargetPropertyId::BaseRotationVectorZ ||
           property == TargetPropertyId::ParentToFlangeRotationVectorX ||
           property == TargetPropertyId::ParentToFlangeRotationVectorY ||
           property == TargetPropertyId::ParentToFlangeRotationVectorZ ||
           property == TargetPropertyId::FlangeToTcpRotationVectorX ||
           property == TargetPropertyId::FlangeToTcpRotationVectorY ||
           property == TargetPropertyId::FlangeToTcpRotationVectorZ;
}

bool hasAxisTarget(const std::map< std::string, double >& values,
                   const std::string& id)
{
    return values.find(id) != values.end();
}

int axisIndex(TargetPropertyId property)
{
    switch (property) {
    case TargetPropertyId::ParentToJointTranslationX:
    case TargetPropertyId::BaseTranslationX:
    case TargetPropertyId::ParentToFlangeTranslationX:
    case TargetPropertyId::FlangeToTcpTranslationX:
        return 0;
    case TargetPropertyId::ParentToJointTranslationY:
    case TargetPropertyId::BaseTranslationY:
    case TargetPropertyId::ParentToFlangeTranslationY:
    case TargetPropertyId::FlangeToTcpTranslationY:
        return 1;
    default:
        return 2;
    }
}

int rotationIndex(TargetPropertyId property)
{
    switch (property) {
    case TargetPropertyId::BaseRotationVectorX:
    case TargetPropertyId::ParentToFlangeRotationVectorX:
    case TargetPropertyId::FlangeToTcpRotationVectorX:
        return 0;
    case TargetPropertyId::BaseRotationVectorY:
    case TargetPropertyId::ParentToFlangeRotationVectorY:
    case TargetPropertyId::FlangeToTcpRotationVectorY:
        return 1;
    default:
        return 2;
    }
}

}    // namespace

CandidatePatchApplyResult CandidatePatchApplier::apply(
    const CanonicalKinematicModel& baseline, const CandidatePatch& patch)
{
    CandidatePatchApplyResult result;
    result.diagnostics = patch.diagnostics;
    const CandidatePatchValidationResult patchValidation =
        CandidatePatchValidator::validate(patch, patch.writes.empty()
                                                    ? std::vector< ReadWriteTarget >{{}}
                                                    : [&patch]() {
                                                        std::vector< ReadWriteTarget > targets;
                                                        for (const CandidatePatchWrite& write : patch.writes)
                                                            targets.push_back(write.target);
                                                        return targets;
                                                    }());
    result.diagnostics.insert(result.diagnostics.end(),
                              patchValidation.diagnostics.begin(),
                              patchValidation.diagnostics.end());
    if (!patchValidation.valid)
        return result;

    CanonicalKinematicModel candidate = baseline;
    std::map< std::string, double > axisU;
    std::map< std::string, double > axisV;
    std::map< std::string, rw::math::Vector3D<> > baseRotation;
    std::map< std::string, rw::math::Vector3D<> > flangeRotation;
    std::map< std::string, rw::math::Vector3D<> > tcpRotation;
    std::set< std::string > touchedAxes;
    std::set< std::string > touchedBase;
    std::set< std::string > touchedFlange;
    std::set< std::string > touchedTcp;

    for (const CandidatePatchWrite& write : patch.writes) {
        double value = 0.0;
        switch (write.target.objectType) {
        case TargetObjectType::Joint: {
            JointEdge* joint = findJoint(candidate, write.target.objectId);
            if (joint == nullptr) {
                addError(result.diagnostics, patch, write.target,
                         "CANDIDATE_PATCH_TARGET_NOT_FOUND",
                         "The patch target does not identify a canonical joint.");
                continue;
            }
            if (!scalarWrite(write, value, result.diagnostics, patch))
                continue;
            if (isJointTranslation(write.target.propertyId)) {
                rw::math::Vector3D<> translation = joint->parentToJointZero.P();
                translation(axisIndex(write.target.propertyId)) = value;
                joint->parentToJointZero = rw::math::Transform3D<>(
                    translation, joint->parentToJointZero.R());
            } else if (write.target.propertyId == TargetPropertyId::MotionAxisTiltU) {
                axisU[joint->id] = value;
                touchedAxes.insert(joint->id);
            } else if (write.target.propertyId == TargetPropertyId::MotionAxisTiltV) {
                axisV[joint->id] = value;
                touchedAxes.insert(joint->id);
            } else if (write.target.propertyId == TargetPropertyId::ZeroPositionOffset) {
                joint->zeroPositionOffset = value;
            } else if (write.target.propertyId == TargetPropertyId::PhysicalLimitLower) {
                joint->physicalLimits.lower = value;
                joint->physicalLimits.enabled = true;
            } else if (write.target.propertyId == TargetPropertyId::PhysicalLimitUpper) {
                joint->physicalLimits.upper = value;
                joint->physicalLimits.enabled = true;
            } else if (write.target.propertyId == TargetPropertyId::OperationalLimitLower) {
                joint->operationalLimits.lower = value;
                joint->operationalLimits.enabled = true;
            } else if (write.target.propertyId == TargetPropertyId::OperationalLimitUpper) {
                joint->operationalLimits.upper = value;
                joint->operationalLimits.enabled = true;
            } else {
                addError(result.diagnostics, patch, write.target,
                         "CANDIDATE_PATCH_PROPERTY_UNSUPPORTED",
                         "The joint target property is not supported by the S36 applier.");
            }
            break;
        }
        case TargetObjectType::Frame: {
            if (!frameExists(candidate, write.target.objectId)) {
                addError(result.diagnostics, patch, write.target,
                         "CANDIDATE_PATCH_TARGET_NOT_FOUND",
                         "The patch target does not identify a canonical frame.");
                continue;
            }
            if (!scalarWrite(write, value, result.diagnostics, patch))
                continue;
            JointEdge* installation = incomingFixedJoint(candidate, write.target.objectId);
            if (installation == nullptr) {
                addError(result.diagnostics, patch, write.target,
                         "CANDIDATE_PATCH_FRAME_TRANSFORM_UNREPRESENTABLE",
                         "This frame has no unique incoming fixed edge to carry the pose update.");
                continue;
            }
            if (write.target.propertyId >= TargetPropertyId::BaseTranslationX &&
                write.target.propertyId <= TargetPropertyId::BaseRotationVectorZ) {
                if (isPoseRotation(write.target.propertyId)) {
                    if (write.target.propertyId == TargetPropertyId::BaseRotationVectorX ||
                        write.target.propertyId == TargetPropertyId::BaseRotationVectorY ||
                        write.target.propertyId == TargetPropertyId::BaseRotationVectorZ) {
                        baseRotation[write.target.objectId](rotationIndex(write.target.propertyId)) = value;
                        touchedBase.insert(write.target.objectId);
                    }
                } else {
                    rw::math::Vector3D<> translation = installation->parentToJointZero.P();
                    translation(axisIndex(write.target.propertyId)) = value;
                    installation->parentToJointZero = rw::math::Transform3D<>(
                        translation, installation->parentToJointZero.R());
                }
            } else if (write.target.propertyId >= TargetPropertyId::ParentToFlangeTranslationX &&
                       write.target.propertyId <= TargetPropertyId::ParentToFlangeRotationVectorZ) {
                if (isPoseRotation(write.target.propertyId)) {
                    flangeRotation[write.target.objectId](rotationIndex(write.target.propertyId)) = value;
                    touchedFlange.insert(write.target.objectId);
                } else {
                    rw::math::Vector3D<> translation = installation->parentToJointZero.P();
                    translation(axisIndex(write.target.propertyId)) = value;
                    installation->parentToJointZero = rw::math::Transform3D<>(
                        translation, installation->parentToJointZero.R());
                }
            } else {
                addError(result.diagnostics, patch, write.target,
                         "CANDIDATE_PATCH_PROPERTY_UNSUPPORTED",
                         "The frame target property is not supported by the S36 applier.");
            }
            break;
        }
        case TargetObjectType::ToolBinding: {
            ToolBinding* tool = findTool(candidate, write.target.objectId);
            if (tool == nullptr) {
                addError(result.diagnostics, patch, write.target,
                         "CANDIDATE_PATCH_TARGET_NOT_FOUND",
                         "The patch target does not identify a canonical tool binding.");
                continue;
            }
            if (!scalarWrite(write, value, result.diagnostics, patch))
                continue;
            if (write.target.propertyId >= TargetPropertyId::FlangeToTcpTranslationX &&
                write.target.propertyId <= TargetPropertyId::FlangeToTcpRotationVectorZ) {
                if (isPoseRotation(write.target.propertyId)) {
                    tcpRotation[tool->id](rotationIndex(write.target.propertyId)) = value;
                    touchedTcp.insert(tool->id);
                } else {
                    rw::math::Vector3D<> translation = tool->flangeToTcp.P();
                    translation(axisIndex(write.target.propertyId)) = value;
                    tool->flangeToTcp = rw::math::Transform3D<>(
                        translation, tool->flangeToTcp.R());
                }
            } else {
                addError(result.diagnostics, patch, write.target,
                         "CANDIDATE_PATCH_PROPERTY_UNSUPPORTED",
                         "The tool target property is not supported by the S36 applier.");
            }
            break;
        }
        case TargetObjectType::Geometry: {
            GeometryBinding* geometry = findGeometry(candidate, write.target.objectId);
            if (geometry == nullptr || !geometry->optimizationOwned) {
                addError(result.diagnostics, patch, write.target,
                         "CANDIDATE_PATCH_TARGET_NOT_FOUND",
                         "The patch target must identify an owned canonical visual geometry.");
                continue;
            }
            if (write.target.propertyId == TargetPropertyId::GeometryRigidTransform) {
                if (write.value.kind != CandidatePatchValue::Kind::ArtifactReference ||
                    write.value.textValue.empty() || !geometry->allowRigidTransform) {
                    addError(result.diagnostics, patch, write.target,
                             "CANDIDATE_PATCH_ARTIFACT_NOT_AUTHORIZED",
                             "Rigid visual geometry artifacts require an owned transform-capable binding.");
                    continue;
                }
                result.generatedArtifacts.push_back(write.value.textValue);
            } else if (!scalarWrite(write, value, result.diagnostics, patch)) {
                continue;
            } else {
                switch (write.target.propertyId) {
                case TargetPropertyId::GeometryRadius: geometry->radius = value; break;
                case TargetPropertyId::GeometryLength: geometry->length = value; break;
                case TargetPropertyId::GeometryWidth: geometry->width = value; break;
                case TargetPropertyId::GeometryHeight: geometry->height = value; break;
                case TargetPropertyId::GeometryDepth: geometry->depth = value; break;
                case TargetPropertyId::GeometryWallThickness: geometry->wallThickness = value; break;
                default:
                    addError(result.diagnostics, patch, write.target,
                             "CANDIDATE_PATCH_PROPERTY_UNSUPPORTED",
                             "The visual geometry property is not supported by the S36 applier.");
                    break;
                }
            }
            break;
        }
        case TargetObjectType::CollisionGeometry: {
            CollisionBinding* collision = findCollision(candidate, write.target.objectId);
            if (collision == nullptr || !collision->optimizationOwned) {
                addError(result.diagnostics, patch, write.target,
                         "CANDIDATE_PATCH_TARGET_NOT_FOUND",
                         "The patch target must identify an owned canonical collision geometry.");
                continue;
            }
            if (write.target.propertyId == TargetPropertyId::GeometryRigidTransform) {
                if (write.value.kind != CandidatePatchValue::Kind::ArtifactReference ||
                    write.value.textValue.empty() || !collision->allowRigidTransform) {
                    addError(result.diagnostics, patch, write.target,
                             "CANDIDATE_PATCH_ARTIFACT_NOT_AUTHORIZED",
                             "Rigid collision geometry artifacts require an owned transform-capable binding.");
                    continue;
                }
                result.generatedArtifacts.push_back(write.value.textValue);
            } else if (!scalarWrite(write, value, result.diagnostics, patch)) {
                continue;
            } else {
                switch (write.target.propertyId) {
                case TargetPropertyId::GeometryRadius: collision->radius = value; break;
                case TargetPropertyId::GeometryLength: collision->length = value; break;
                case TargetPropertyId::GeometryWidth: collision->width = value; break;
                case TargetPropertyId::GeometryHeight: collision->height = value; break;
                case TargetPropertyId::GeometryDepth: collision->depth = value; break;
                case TargetPropertyId::GeometryWallThickness: collision->wallThickness = value; break;
                default:
                    addError(result.diagnostics, patch, write.target,
                             "CANDIDATE_PATCH_PROPERTY_UNSUPPORTED",
                             "The collision geometry property is not supported by the S36 applier.");
                    break;
                }
            }
            break;
        }
        default:
            addError(result.diagnostics, patch, write.target,
                     "CANDIDATE_PATCH_TARGET_UNSUPPORTED",
                     "The S36 applier does not support this target object type.");
            break;
        }
    }

    for (const std::string& id : touchedAxes) {
        JointEdge* joint = findJoint(candidate, id);
        if (!hasAxisTarget(axisU, id) || !hasAxisTarget(axisV, id)) {
            result.diagnostics.push_back(makeAdapterDiagnostic(
                patch.adapterId, patch.bindingId, id, "writes",
                "CANDIDATE_PATCH_AXIS_SIBLING_REQUIRED",
                "Motion-axis tilt requires both U and V coordinates for one joint."));
            continue;
        }
        if (joint != nullptr)
            joint->motionAxisInJoint = KinematicConventions::tiltedAxis(
                joint->motionAxisInJoint, axisU.at(id), axisV.at(id));
    }
    for (const std::string& id : touchedBase) {
        JointEdge* installation = incomingFixedJoint(candidate, id);
        if (installation != nullptr)
            installation->parentToJointZero = PoseDelta::applyRotationVectorDelta(
                installation->parentToJointZero, baseRotation[id],
                patch.poseDeltaComposition);
    }
    for (const std::string& id : touchedFlange) {
        JointEdge* installation = incomingFixedJoint(candidate, id);
        if (installation != nullptr)
            installation->parentToJointZero = PoseDelta::applyRotationVectorDelta(
                installation->parentToJointZero, flangeRotation[id],
                patch.poseDeltaComposition);
    }
    for (const std::string& id : touchedTcp) {
        ToolBinding* tool = findTool(candidate, id);
        if (tool != nullptr)
            tool->flangeToTcp = PoseDelta::applyRotationVectorDelta(
                tool->flangeToTcp, tcpRotation[id], patch.poseDeltaComposition);
    }

    result.generatedArtifacts.insert(result.generatedArtifacts.end(),
                                     patch.generatedArtifacts.begin(),
                                     patch.generatedArtifacts.end());
    std::sort(result.generatedArtifacts.begin(), result.generatedArtifacts.end());
    result.generatedArtifacts.erase(
        std::unique(result.generatedArtifacts.begin(), result.generatedArtifacts.end()),
        result.generatedArtifacts.end());

    const CanonicalKinematicModelValidationResult validation =
        CanonicalKinematicModelValidator::validate(candidate);
    result.diagnostics.insert(result.diagnostics.end(), validation.diagnostics.begin(),
                              validation.diagnostics.end());
    if (!result.diagnostics.empty() || !validation.valid)
        return result;

    result.model = std::move(candidate);
    result.ok = true;
    return result;
}

}    // namespace rws
