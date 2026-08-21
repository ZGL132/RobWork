#include "DhProjection.hpp"

#include <cmath>

namespace rws {
namespace {

constexpr double kTolerance = 1e-10;

bool near(double first, double second = 0.0)
{
    return std::fabs(first - second) <= kTolerance;
}

bool isZAxis(const rw::math::Vector3D<>& axis)
{
    return near(axis(0)) && near(axis(1)) && near(axis(2), 1.0);
}

bool isRotationAboutZ(const rw::math::Rotation3D<>& rotation)
{
    return near(rotation(0, 2)) && near(rotation(1, 2)) && near(rotation(2, 0)) &&
           near(rotation(2, 1)) && near(rotation(2, 2), 1.0);
}

bool isRotationAboutX(const rw::math::Rotation3D<>& rotation)
{
    return near(rotation(0, 0), 1.0) && near(rotation(0, 1)) && near(rotation(0, 2)) &&
           near(rotation(1, 0)) && near(rotation(2, 0));
}

void addDiagnostic(DhProjectionResult& result,
                   const std::string& code,
                   const std::string& objectId,
                   const std::string& fieldPath,
                   const std::string& message,
                   bool unsupported = false)
{
    StructureOptimizationDiagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.severity = unsupported ? "Error" : "Warning";
    diagnostic.subsystem = "canonical-kinematics";
    diagnostic.stage = "dh-projection";
    diagnostic.objectId = objectId;
    diagnostic.fieldPath = fieldPath;
    diagnostic.message = message;
    result.diagnostics.push_back(diagnostic);
    result.lostComponents.push_back(objectId + "." + fieldPath);
    if (unsupported)
        result.status = DhProjectionStatus::Unsupported;
    else if (result.status == DhProjectionStatus::Exact)
        result.status = DhProjectionStatus::Lossy;
}

}    // namespace

DhProjectionResult DhProjection::project(const CanonicalKinematicModel& model)
{
    DhProjectionResult result;
    const CanonicalKinematicModelValidationResult validation =
        CanonicalKinematicModelValidator::validate(model);
    if (!validation.valid) {
        result.status = DhProjectionStatus::Unsupported;
        result.diagnostics = validation.diagnostics;
        return result;
    }

    for (const JointEdge& joint : model.joints) {
        if (joint.type == CanonicalJointType::Fixed)
            continue;

        DhProjectionRow row;
        row.jointId = joint.id;
        row.jointType = joint.type;
        const rw::math::Transform3D<>& parent = joint.parentToJointZero;
        const rw::math::Transform3D<>& child = joint.jointMotionToChild;
        row.thetaOffset = std::atan2(parent.R()(1, 0), parent.R()(0, 0));
        row.d = parent.P()(2);
        row.a = child.P()(0);
        row.alpha = std::atan2(child.R()(2, 1), child.R()(1, 1));
        result.rows.push_back(row);

        const std::string path = "joints['" + joint.id + "']";
        if (!isZAxis(joint.motionAxisInJoint)) {
            addDiagnostic(result, "DH_PROJECTION_AXIS_UNSUPPORTED", joint.id, path + ".motionAxisInJoint",
                          "The canonical motion axis is not the local +Z axis required by this DH view.",
                          true);
        }
        if (!isRotationAboutZ(parent.R())) {
            addDiagnostic(result, "DH_PROJECTION_PARENT_ROTATION_LOSSY", joint.id,
                          path + ".parentToJointZero.rotation",
                          "The parent-to-joint rotation contains components outside a Z-axis DH offset.");
        }
        if (!near(parent.P()(0)) || !near(parent.P()(1))) {
            addDiagnostic(result, "DH_PROJECTION_PARENT_TRANSLATION_LOSSY", joint.id,
                          path + ".parentToJointZero.translation",
                          "The parent-to-joint translation contains X or Y components outside this DH form.");
        }
        if (!isRotationAboutX(child.R())) {
            addDiagnostic(result, "DH_PROJECTION_CHILD_ROTATION_LOSSY", joint.id,
                          path + ".jointMotionToChild.rotation",
                          "The joint-to-child rotation contains components outside an X-axis DH twist.");
        }
        if (!near(child.P()(1)) || !near(child.P()(2))) {
            addDiagnostic(result, "DH_PROJECTION_CHILD_TRANSLATION_LOSSY", joint.id,
                          path + ".jointMotionToChild.translation",
                          "The joint-to-child translation contains Y or Z components outside this DH form.");
        }
    }
    return result;
}

}    // namespace rws
