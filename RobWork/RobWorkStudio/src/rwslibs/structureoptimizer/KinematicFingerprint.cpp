#include "KinematicFingerprint.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <locale>
#include <sstream>

namespace rws {
namespace {

void appendText(std::ostringstream& stream, const std::string& value)
{
    stream << value.size() << ':' << value << ';';
}

bool appendDouble(std::ostringstream& stream, double value)
{
    if (!std::isfinite(value))
        return false;
    stream << std::hexfloat << (value == 0.0 ? 0.0 : value) << ';';
    return true;
}

bool appendVector(std::ostringstream& stream, const rw::math::Vector3D<>& vector)
{
    return appendDouble(stream, vector(0)) && appendDouble(stream, vector(1)) &&
           appendDouble(stream, vector(2));
}

bool appendTransform(std::ostringstream& stream, const rw::math::Transform3D<>& transform)
{
    if (!appendVector(stream, transform.P()))
        return false;
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            if (!appendDouble(stream, transform.R()(row, column)))
                return false;
        }
    }
    return true;
}

bool isFiniteVector(const rw::math::Vector3D<>& vector)
{
    return std::isfinite(vector(0)) && std::isfinite(vector(1)) && std::isfinite(vector(2));
}

bool isFiniteTransform(const rw::math::Transform3D<>& transform)
{
    if (!isFiniteVector(transform.P()))
        return false;
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            if (!std::isfinite(transform.R()(row, column)))
                return false;
        }
    }
    return true;
}

bool allCanonicalNumericsAreFinite(const CanonicalKinematicModel& model)
{
    for (const JointEdge& joint : model.joints) {
        if (!isFiniteTransform(joint.parentToJointZero) ||
            !isFiniteVector(joint.motionAxisInJoint) ||
            !isFiniteTransform(joint.jointMotionToChild) ||
            !std::isfinite(joint.zeroPositionOffset) ||
            !std::isfinite(joint.physicalLimits.lower) ||
            !std::isfinite(joint.physicalLimits.upper) ||
            !std::isfinite(joint.operationalLimits.lower) ||
            !std::isfinite(joint.operationalLimits.upper))
            return false;
    }
    for (const ToolBinding& binding : model.toolBindings) {
        if (!isFiniteTransform(binding.flangeToTcp))
            return false;
    }
    for (const GeometryBinding& binding : model.geometryBindings) {
        if (!isFiniteTransform(binding.referenceToGeometry) || !std::isfinite(binding.radius) ||
            !std::isfinite(binding.length) || !std::isfinite(binding.width) ||
            !std::isfinite(binding.height) || !std::isfinite(binding.depth) ||
            !std::isfinite(binding.wallThickness))
            return false;
    }
    for (const CollisionBinding& binding : model.collisionBindings) {
        if (!isFiniteTransform(binding.referenceToGeometry) || !std::isfinite(binding.radius) ||
            !std::isfinite(binding.length) || !std::isfinite(binding.width) ||
            !std::isfinite(binding.height) || !std::isfinite(binding.depth) ||
            !std::isfinite(binding.wallThickness))
            return false;
    }
    return true;
}

template< class Binding >
bool appendGeometryBinding(std::ostringstream& stream, const Binding& binding)
{
    appendText(stream, binding.id);
    appendText(stream, binding.referenceFrameId);
    appendText(stream, std::to_string(static_cast< int >(binding.kind)));
    appendText(stream, binding.optimizationOwned ? "1" : "0");
    appendText(stream, binding.allowRigidTransform ? "1" : "0");
    if (!appendTransform(stream, binding.referenceToGeometry) || !appendDouble(stream, binding.radius) ||
        !appendDouble(stream, binding.length) || !appendDouble(stream, binding.width) ||
        !appendDouble(stream, binding.height) || !appendDouble(stream, binding.depth) ||
        !appendDouble(stream, binding.wallThickness))
        return false;
    appendText(stream, binding.sourceObjectId);
    return true;
}

template< class Value > std::vector< Value > sortedById(const std::vector< Value >& values)
{
    std::vector< Value > sorted = values;
    std::sort(sorted.begin(), sorted.end(), [](const Value& first, const Value& second) {
        return first.id < second.id;
    });
    return sorted;
}

void appendSortedStrings(std::ostringstream& stream, const std::vector< std::string >& values)
{
    std::vector< std::string > sorted = values;
    std::sort(sorted.begin(), sorted.end());
    for (const std::string& value : sorted)
        appendText(stream, value);
}

enum class FingerprintScope
{
    Model,
    Environment,
    Tool
};

void appendHeader(std::ostringstream& stream, const CanonicalKinematicModel& model,
                  const char* scope)
{
    appendText(stream, "canonical-kinematic-model-v1");
    appendText(stream, scope);
    appendText(stream, std::to_string(model.schemaVersion));
}

bool serializeModelFingerprint(const CanonicalKinematicModel& model, std::string& serialized)
{
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    appendHeader(stream, model, "model");
    appendText(stream, model.modelId);
    appendText(stream, model.sourceFingerprint);
    appendText(stream, model.rootFrameId);
    appendText(stream, model.baseFrameId);
    appendText(stream, model.activeDeviceChainId);

    for (const FrameNode& frame : sortedById(model.frames)) {
        appendText(stream, frame.id);
        appendText(stream, frame.name);
        appendText(stream, std::to_string(static_cast< int >(frame.type)));
        appendText(stream, frame.sourceObjectId);
    }
    for (const JointEdge& joint : sortedById(model.joints)) {
        appendText(stream, joint.id);
        appendText(stream, joint.name);
        appendText(stream, std::to_string(static_cast< int >(joint.type)));
        appendText(stream, joint.parentFrameId);
        appendText(stream, joint.childFrameId);
        if (!appendTransform(stream, joint.parentToJointZero) ||
            !appendVector(stream, joint.motionAxisInJoint) ||
            !appendTransform(stream, joint.jointMotionToChild) ||
            !appendDouble(stream, joint.zeroPositionOffset) ||
            !appendDouble(stream, joint.physicalLimits.lower) ||
            !appendDouble(stream, joint.physicalLimits.upper) ||
            !appendDouble(stream, joint.operationalLimits.lower) ||
            !appendDouble(stream, joint.operationalLimits.upper))
            return false;
        appendText(stream, joint.physicalLimits.enabled ? "1" : "0");
        appendText(stream, std::to_string(static_cast< int >(joint.physicalLimits.unit)));
        appendText(stream, std::to_string(
            static_cast< int >(joint.physicalLimits.coordinateConvention)));
        appendText(stream, joint.operationalLimits.enabled ? "1" : "0");
        appendText(stream, std::to_string(static_cast< int >(joint.operationalLimits.unit)));
        appendText(stream, std::to_string(
            static_cast< int >(joint.operationalLimits.coordinateConvention)));
        appendText(stream, joint.dofId);
        appendText(stream, joint.sourceObjectId);
    }
    for (const DofDefinition& dof : sortedById(model.dofs)) {
        appendText(stream, dof.id);
        appendText(stream, dof.jointId);
        appendText(stream, std::to_string(dof.qIndex));
        appendText(stream, std::to_string(static_cast< int >(dof.type)));
        appendText(stream, std::to_string(static_cast< int >(dof.unit)));
    }
    for (const DeviceChain& chain : sortedById(model.deviceChains)) {
        appendText(stream, chain.id);
        appendText(stream, chain.rootFrameId);
        appendText(stream, chain.tipFrameId);
        // Chain order is a semantic Q/topology mapping and must be preserved.
        for (const std::string& jointId : chain.orderedJointIds)
            appendText(stream, jointId);
        appendText(stream, "|dofs|");
        for (const std::string& dofId : chain.orderedDofIds)
            appendText(stream, dofId);
    }
    for (const GeometryBinding& binding : sortedById(model.geometryBindings))
        if (!appendGeometryBinding(stream, binding)) return false;
    for (const CollisionBinding& binding : sortedById(model.collisionBindings))
        if (!appendGeometryBinding(stream, binding)) return false;
    serialized = stream.str();
    return true;
}

bool serializeEnvironmentFingerprint(const CanonicalKinematicModel& model,
                                     std::string& serialized)
{
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    appendHeader(stream, model, "environment");
    appendText(stream, model.environmentFingerprint);
    for (const ToolBinding& binding : sortedById(model.toolBindings)) {
        appendText(stream, binding.id);
        appendSortedStrings(stream, binding.collisionBindingIds);
    }
    for (const CollisionBinding& binding : sortedById(model.collisionBindings))
        if (!appendGeometryBinding(stream, binding)) return false;
    serialized = stream.str();
    return true;
}

bool serializeToolFingerprint(const CanonicalKinematicModel& model, std::string& serialized)
{
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    appendHeader(stream, model, "tool");
    appendText(stream, model.activeDeviceChainId);
    for (const ToolBinding& binding : sortedById(model.toolBindings)) {
        appendText(stream, binding.id);
        appendText(stream, binding.flangeFrameId);
        appendText(stream, binding.tcpFrameId);
        if (!appendTransform(stream, binding.flangeToTcp))
            return false;
        appendSortedStrings(stream, binding.geometryBindingIds);
    }
    for (const GeometryBinding& binding : sortedById(model.geometryBindings))
        if (!appendGeometryBinding(stream, binding)) return false;
    serialized = stream.str();
    return true;
}

std::string fnv1a64(const std::string& content)
{
    std::uint64_t hash = UINT64_C(14695981039346656037);
    for (const unsigned char character : content) {
        hash ^= character;
        hash *= UINT64_C(1099511628211);
    }
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::hex << std::setfill('0') << std::setw(16) << hash;
    return stream.str();
}

void addError(KinematicFingerprintResult& result, const std::string& code, const std::string& message)
{
    StructureOptimizationDiagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.severity = "Error";
    diagnostic.subsystem = "canonical-kinematics";
    diagnostic.stage = "fingerprint";
    diagnostic.fieldPath = "canonicalModel";
    diagnostic.message = message;
    result.diagnostics.push_back(diagnostic);
}

KinematicFingerprintResult fingerprintForScope(const CanonicalKinematicModel& model,
                                               const FingerprintScope scope)
{
    KinematicFingerprintResult result;
    if (!allCanonicalNumericsAreFinite(model)) {
        addError(result, "KINEMATIC_FINGERPRINT_NONFINITE",
                 "Canonical kinematic data contains a non-finite numeric value.");
        return result;
    }
    const CanonicalKinematicModelValidationResult validation =
        CanonicalKinematicModelValidator::validate(model);
    if (!validation.valid) {
        result.diagnostics = validation.diagnostics;
        return result;
    }
    std::string serialized;
    bool serializedSuccessfully = false;
    switch (scope) {
    case FingerprintScope::Model:
        serializedSuccessfully = serializeModelFingerprint(model, serialized);
        break;
    case FingerprintScope::Environment:
        serializedSuccessfully = serializeEnvironmentFingerprint(model, serialized);
        break;
    case FingerprintScope::Tool:
        serializedSuccessfully = serializeToolFingerprint(model, serialized);
        break;
    }
    if (!serializedSuccessfully) {
        addError(result, "KINEMATIC_FINGERPRINT_NONFINITE",
                 "Canonical kinematic data contains a non-finite numeric value.");
        return result;
    }
    result.value = fnv1a64(serialized);
    result.ok = true;
    return result;
}

}    // namespace

KinematicFingerprintResult KinematicFingerprint::forModel(const CanonicalKinematicModel& model)
{
    return fingerprintForScope(model, FingerprintScope::Model);
}

KinematicFingerprintResult KinematicFingerprint::forEnvironment(
    const CanonicalKinematicModel& model)
{
    return fingerprintForScope(model, FingerprintScope::Environment);
}

KinematicFingerprintResult KinematicFingerprint::forTool(const CanonicalKinematicModel& model)
{
    return fingerprintForScope(model, FingerprintScope::Tool);
}

}    // namespace rws
