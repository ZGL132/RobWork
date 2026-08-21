#include "KinematicModelImporter.hpp"

#include <rw/kinematics/FixedFrame.hpp>
#include <rw/kinematics/Frame.hpp>
#include <rw/math/Q.hpp>
#include <rw/models/Device.hpp>
#include <rw/models/Joint.hpp>
#include <rw/models/PrismaticJoint.hpp>
#include <rw/models/RevoluteJoint.hpp>
#include <rw/models/SerialDevice.hpp>
#include <rw/models/WorkCell.hpp>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <vector>

namespace rws {
namespace {

void addError(KinematicImportResult& result,
              const std::string& code,
              const std::string& objectId,
              const std::string& fieldPath,
              const std::string& message)
{
    StructureOptimizationDiagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.severity = "Error";
    diagnostic.subsystem = "canonical-kinematics";
    diagnostic.stage = "import";
    diagnostic.objectId = objectId;
    diagnostic.fieldPath = fieldPath;
    diagnostic.message = message;
    result.diagnostics.push_back(diagnostic);
}

std::string deviceId(const rw::models::Device* device)
{
    return device == nullptr ? std::string() : device->getName();
}

std::string framePath(const std::string& deviceName, std::size_t index)
{
    std::ostringstream stream;
    stream << "workcell.devices['" << deviceName << "'].frames[" << index << "]";
    return stream.str();
}

std::string rootFramePath(std::size_t index)
{
    return index == 0 ? "workcell.worldFrame" :
                        "workcell.baseAncestors[" + std::to_string(index - 1) + "]";
}

bool containsDevice(const rw::models::WorkCell& workcell, const rw::models::Device* device)
{
    const std::vector< rw::core::Ptr< rw::models::Device > >& devices = workcell.getDevices();
    return std::any_of(devices.begin(), devices.end(), [device](const rw::core::Ptr< rw::models::Device >& item) {
        return item.get() == device;
    });
}

CanonicalJointType jointType(const rw::models::Joint& joint, bool& supported)
{
    supported = true;
    if (dynamic_cast< const rw::models::RevoluteJoint* >(&joint) != nullptr)
        return CanonicalJointType::Revolute;
    if (dynamic_cast< const rw::models::PrismaticJoint* >(&joint) != nullptr)
        return CanonicalJointType::Prismatic;
    supported = false;
    return CanonicalJointType::Fixed;
}

CanonicalCoordinateUnit coordinateUnit(CanonicalJointType type)
{
    return type == CanonicalJointType::Prismatic ? CanonicalCoordinateUnit::Metres :
                                                   CanonicalCoordinateUnit::Radians;
}

bool importJointLimits(const rw::models::Joint& source,
                       CanonicalJointLimits& destination,
                       KinematicImportResult& result,
                       const std::string& path)
{
    const std::pair< rw::math::Q, rw::math::Q >& bounds = source.getBounds();
    if (bounds.first.size() != 1 || bounds.second.size() != 1) {
        addError(result, "KINEMATIC_IMPORT_LIMIT_DIMENSION_INVALID", source.getName(), path + ".bounds",
                 "A supported one-DOF joint must provide one lower and one upper bound.");
        return false;
    }
    const double lower = bounds.first(0);
    const double upper = bounds.second(0);
    if (!std::isfinite(lower) || !std::isfinite(upper) || !(lower < upper)) {
        addError(result, "KINEMATIC_IMPORT_LIMIT_INVALID", source.getName(), path + ".bounds",
                 "Joint limits must be finite and ordered from lower to upper.");
        return false;
    }
    destination.enabled = true;
    destination.lower = lower;
    destination.upper = upper;
    return true;
}

CanonicalFrameType frameTypeFor(const rw::kinematics::Frame& frame,
                                const rw::kinematics::Frame& tcp,
                                bool isRoot,
                                bool isDeviceBase)
{
    if (isRoot)
        return CanonicalFrameType::Auxiliary;
    if (isDeviceBase)
        return CanonicalFrameType::Base;
    if (&frame == &tcp)
        return CanonicalFrameType::Tool;
    if (dynamic_cast< const rw::kinematics::FixedFrame* >(&frame) != nullptr)
        return CanonicalFrameType::Fixed;
    return CanonicalFrameType::Link;
}

}    // namespace

KinematicImportResult KinematicModelImporter::import(const KinematicImportRequest& request)
{
    KinematicImportResult result;
    if (request.workcell == nullptr) {
        addError(result, "KINEMATIC_IMPORT_WORKCELL_MISSING", deviceId(request.device), "workcell",
                 "A WorkCell must be explicitly supplied for canonical kinematic import.");
        return result;
    }
    if (request.device == nullptr) {
        addError(result, "KINEMATIC_IMPORT_DEVICE_MISSING", request.workcell->getName(), "device",
                 "A target Device must be explicitly supplied; no device is selected implicitly.");
        return result;
    }
    if (request.tcpFrame == nullptr) {
        addError(result, "KINEMATIC_IMPORT_TCP_MISSING", request.device->getName(), "tcpFrame",
                 "A TCP frame must be explicitly supplied; no TCP is selected implicitly.");
        return result;
    }
    if (!containsDevice(*request.workcell, request.device)) {
        addError(result, "KINEMATIC_IMPORT_DEVICE_NOT_IN_WORKCELL", request.device->getName(), "device",
                 "The requested Device is not registered in the supplied WorkCell.");
        return result;
    }

    const rw::models::SerialDevice* serial =
        dynamic_cast< const rw::models::SerialDevice* >(request.device);
    if (serial == nullptr) {
        addError(result, "KINEMATIC_IMPORT_DEVICE_UNSUPPORTED", request.device->getName(), "device",
                 "Only explicitly selected SerialDevice chains are supported by this import stage.");
        return result;
    }
    const std::vector< rw::kinematics::Frame* >& sourceFrames = serial->frames();
    if (sourceFrames.empty() || sourceFrames.front() != request.device->getBase()) {
        addError(result, "KINEMATIC_IMPORT_CHAIN_INVALID", request.device->getName(), "device.frames",
                 "The serial Device must expose a non-empty chain beginning at its base frame.");
        return result;
    }
    if (std::find(sourceFrames.begin(), sourceFrames.end(), request.tcpFrame) == sourceFrames.end()) {
        addError(result, "KINEMATIC_IMPORT_TCP_NOT_IN_CHAIN", request.tcpFrame->getName(), "tcpFrame",
                 "The requested TCP frame is not part of the explicitly selected Device chain.");
        return result;
    }
    if (sourceFrames.back() != request.tcpFrame) {
        addError(result, "KINEMATIC_IMPORT_TCP_NOT_CHAIN_TIP", request.tcpFrame->getName(), "tcpFrame",
                 "The requested TCP frame must be the tip of the explicitly selected Device chain.");
        return result;
    }

    // The canonical chain begins at the WorkCell root so a non-zero device
    // base survives import.  A device's serial chain deliberately starts at
    // its base, therefore collect only the fixed ancestors that precede it.
    std::vector< rw::kinematics::Frame* > chainFrames;
    const rw::kinematics::Frame* world = request.workcell->getWorldFrame();
    if (world == nullptr) {
        addError(result, "KINEMATIC_IMPORT_WORKCELL_ROOT_MISSING", request.workcell->getName(),
                 "workcell.worldFrame", "The supplied WorkCell does not expose a world root frame.");
        return result;
    }
    if (sourceFrames.front() != world) {
        std::vector< rw::kinematics::Frame* > ancestors;
        const rw::kinematics::Frame* current = sourceFrames.front()->getParent();
        while (current != nullptr && current != world) {
            ancestors.push_back(const_cast< rw::kinematics::Frame* >(current));
            current = current->getParent();
        }
        if (current != world) {
            addError(result, "KINEMATIC_IMPORT_BASE_NOT_IN_WORKCELL_TREE",
                     sourceFrames.front()->getName(), "device.base.parent",
                     "The selected Device base must be connected to the WorkCell world frame.");
            return result;
        }
        chainFrames.push_back(const_cast< rw::kinematics::Frame* >(world));
        chainFrames.insert(chainFrames.end(), ancestors.rbegin(), ancestors.rend());
    }
    const std::size_t serialFrameOffset = chainFrames.size();
    chainFrames.insert(chainFrames.end(), sourceFrames.begin(), sourceFrames.end());

    CanonicalKinematicModel model;
    model.modelId = request.modelId.empty() ? request.device->getName() : request.modelId;
    model.sourceFingerprint = request.sourceFingerprint;
    model.environmentFingerprint = request.environmentFingerprint;
    model.rootFrameId = chainFrames.front()->getName();
    model.baseFrameId = sourceFrames.front()->getName();
    model.activeDeviceChainId = request.device->getName() + ":chain";

    DeviceChain chain;
    chain.id = model.activeDeviceChainId;
    chain.rootFrameId = model.rootFrameId;
    chain.tipFrameId = request.tcpFrame->getName();

    for (std::size_t index = 0; index < chainFrames.size(); ++index) {
        const rw::kinematics::Frame* source = chainFrames[index];
        const std::string path = index < serialFrameOffset ? rootFramePath(index) :
                                                             framePath(request.device->getName(),
                                                                       index - serialFrameOffset);
        if (source == nullptr) {
            addError(result, "KINEMATIC_IMPORT_FRAME_MISSING", request.device->getName(), path,
                     "A serial Device chain cannot contain a null frame.");
            return result;
        }
        if (source->getName().empty() ||
            std::any_of(model.frames.begin(), model.frames.end(), [source](const FrameNode& frame) {
                return frame.id == source->getName();
            })) {
            addError(result, "KINEMATIC_IMPORT_FRAME_ID_DUPLICATE", source->getName(), path + ".name",
                     "Canonical frame IDs are derived from source names and must be non-empty and unique.");
            return result;
        }
        if (index > 0 && source->getParent() != chainFrames[index - 1]) {
            addError(result, "KINEMATIC_IMPORT_CHAIN_DISCONNECTED", source->getName(), path + ".parent",
                     "Each imported chain frame must have the preceding chain frame as its parent.");
            return result;
        }

        FrameNode frame;
        frame.id = source->getName();
        frame.name = source->getName();
        frame.type = frameTypeFor(*source, *request.tcpFrame, index == 0,
                                  source == sourceFrames.front());
        frame.sourceObjectId = source->getName();
        model.frames.push_back(frame);
        result.sourceMappings.push_back({frame.id, source->getName(), "Frame", path});

        if (index == 0)
            continue;

        JointEdge edge;
        edge.id = "edge:" + source->getName();
        edge.name = source->getName();
        edge.parentFrameId = chainFrames[index - 1]->getName();
        edge.childFrameId = source->getName();
        edge.sourceObjectId = source->getName();
        edge.parentToJointZero = rw::math::Transform3D<>();
        edge.jointMotionToChild = rw::math::Transform3D<>();

        const bool isDeviceFrame = index >= serialFrameOffset;
        const rw::models::Joint* sourceJoint =
            isDeviceFrame ? dynamic_cast< const rw::models::Joint* >(source) : nullptr;
        if (sourceJoint != nullptr) {
            bool supported = false;
            edge.type = jointType(*sourceJoint, supported);
            if (!supported) {
                addError(result, "KINEMATIC_IMPORT_JOINT_TYPE_UNSUPPORTED", sourceJoint->getName(),
                         path + ".jointType", "The source joint type is not supported by the canonical importer.");
                return result;
            }
            edge.parentToJointZero = sourceJoint->getFixedTransform();
            edge.motionAxisInJoint = rw::math::Vector3D<>::z();
            if (edge.motionAxisInJoint.norm2() == 0.0) {
                addError(result, "KINEMATIC_IMPORT_JOINT_AXIS_ZERO", sourceJoint->getName(), path + ".axis",
                         "A movable joint must have a non-zero motion axis.");
                return result;
            }
            edge.dofId = "dof:" + edge.id;
            edge.physicalLimits.unit = coordinateUnit(edge.type);
            edge.operationalLimits.unit = coordinateUnit(edge.type);
            // RobWork joint bounds constrain the raw device Q supplied to the
            // source joint; the canonical offset is applied later by FK.
            edge.physicalLimits.coordinateConvention = JointCoordinateConvention::QInput;
            edge.operationalLimits.coordinateConvention = JointCoordinateConvention::QInput;
            if (!importJointLimits(*sourceJoint, edge.physicalLimits, result, path))
                return result;

            DofDefinition dof;
            dof.id = edge.dofId;
            dof.jointId = edge.id;
            dof.qIndex = model.dofs.size();
            dof.type = edge.type;
            dof.unit = coordinateUnit(edge.type);
            model.dofs.push_back(dof);
            chain.orderedDofIds.push_back(dof.id);
            result.sourceMappings.push_back({edge.id, sourceJoint->getName(), "Joint", path + ".joint"});
        }
        else {
            const rw::kinematics::FixedFrame* fixed =
                dynamic_cast< const rw::kinematics::FixedFrame* >(source);
            if (fixed == nullptr) {
                addError(result, "KINEMATIC_IMPORT_FRAME_UNSUPPORTED", source->getName(), path + ".type",
                         "Only FixedFrame and supported one-DOF Joint frames may appear in this import chain.");
                return result;
            }
            edge.type = CanonicalJointType::Fixed;
            edge.parentToJointZero = fixed->getFixedTransform();
            result.sourceMappings.push_back({edge.id, source->getName(), "FixedFrame", path + ".fixedTransform"});
        }
        chain.orderedJointIds.push_back(edge.id);
        model.joints.push_back(edge);
    }
    model.deviceChains.push_back(chain);

    const CanonicalKinematicModelValidationResult validation =
        CanonicalKinematicModelValidator::validate(model);
    if (!validation.valid) {
        result.diagnostics = validation.diagnostics;
        return result;
    }

    result.model = model;
    result.provenance.workcellName = request.workcell->getName();
    result.provenance.deviceId = request.device->getName();
    result.provenance.tcpFrameId = request.tcpFrame->getName();
    result.provenance.sourceFingerprint = request.sourceFingerprint;
    result.provenance.environmentFingerprint = request.environmentFingerprint;
    if (request.sourceSnapshot != nullptr) {
        result.sourceSnapshot = *request.sourceSnapshot;
        result.hasSourceSnapshot = true;
    }
    result.ok = true;
    return result;
}

}    // namespace rws
