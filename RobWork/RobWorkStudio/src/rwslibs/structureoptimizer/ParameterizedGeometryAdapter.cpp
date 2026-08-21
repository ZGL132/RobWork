#include "ParameterizedGeometryAdapter.hpp"

#include "GeometryAdapterSupport.hpp"

#include <cmath>

namespace rws {
namespace {
using namespace geometry_adapter_detail;

const GeometryBinding* findBinding(const CanonicalKinematicModel& model, const std::string& id)
{
    for (const GeometryBinding& value : model.geometryBindings)
        if (value.id == id) return &value;
    return nullptr;
}

bool hasFrame(const CanonicalKinematicModel& model, const std::string& id)
{
    for (const FrameNode& frame : model.frames) if (frame.id == id) return true;
    return false;
}

ReadWriteTarget target(const ParameterBinding& binding)
{
    return {TargetObjectType::Geometry, binding.targetObjectId, propertyFor(binding.semanticKind),
            binding.coordinateFrameId};
}

bool exactTarget(const std::vector< ReadWriteTarget >& values, const ReadWriteTarget& expected)
{
    return values.size() == 1 && values.front() == expected;
}

bool collectDimensions(const AdapterPatchCompileRequest& request, const GeometryBinding& binding,
                       double& radius, double& length, double& width, double& height,
                       double& depth, double& wall, std::vector< StructureOptimizationDiagnostic >& errors)
{
    radius = binding.radius; length = binding.length; width = binding.width;
    height = binding.height; depth = binding.depth; wall = binding.wallThickness;
    bool foundPrimary = false;
    for (const ResolvedAdapterValue& value : request.values) {
        if (!isGeometrySemantic(value.semanticKind) || value.groupId != request.binding->geometryGroupId ||
            value.unit != DesignVariableUnit::Metres || !value.discreteOptionId.empty() ||
            !std::isfinite(value.engineeringValue)) {
            addError(errors, "ParameterizedGeometryAdapter", request.binding->id,
                     request.binding->targetObjectId, "values", "PARAMETERIZED_GEOMETRY_VALUE_INVALID",
                     "Geometry variables require finite metre scalar values from their explicit group.");
            return false;
        }
        if (value.semanticKind == request.binding->semanticKind) {
            if (foundPrimary) {
                addError(errors, "ParameterizedGeometryAdapter", request.binding->id,
                         request.binding->targetObjectId, "values",
                         "PARAMETERIZED_GEOMETRY_PRIMARY_VALUE_DUPLICATE",
                         "A geometry binding may consume its primary dimension exactly once.");
                return false;
            }
            foundPrimary = true;
        }
        overrideDimension(value.semanticKind, value.engineeringValue, radius, length, width, height,
                          depth, wall);
    }
    if (!foundPrimary) {
        addError(errors, "ParameterizedGeometryAdapter", request.binding->id,
                 request.binding->targetObjectId, "values",
                 "PARAMETERIZED_GEOMETRY_PRIMARY_VALUE_REQUIRED",
                 "Geometry compilation requires its primary dimension in the resolved group.");
        return false;
    }
    return validDimensions(binding.kind, radius, length, width, height, depth, wall);
}
} // namespace

std::string ParameterizedGeometryAdapter::adapterId() const { return "ParameterizedGeometryAdapter"; }
int ParameterizedGeometryAdapter::adapterVersion() const { return 1; }
std::vector< SemanticKind > ParameterizedGeometryAdapter::supportedSemanticKinds() const
{
    return {SemanticKind::GeometryRadius, SemanticKind::GeometryLength, SemanticKind::GeometryWidth,
            SemanticKind::GeometryHeight, SemanticKind::GeometryDepth,
            SemanticKind::GeometryWallThickness};
}
std::vector< AdapterCapability > ParameterizedGeometryAdapter::requiredCapabilities() const
{
    return {AdapterCapability::ParameterizedGeometry};
}

AdapterBindingValidationResult ParameterizedGeometryAdapter::validateBinding(
    const ParameterBinding& binding, const CanonicalKinematicModel& baseline) const
{
    AdapterBindingValidationResult result;
    const GeometryBinding* geometry = findBinding(baseline, binding.targetObjectId);
    if (!isGeometrySemantic(binding.semanticKind) || binding.targetObjectType != TargetObjectType::Geometry ||
        geometry == nullptr) {
        result.valid = false;
        addError(result.diagnostics, adapterId().c_str(), binding.id, binding.targetObjectId,
                 "targetObjectId", "PARAMETERIZED_GEOMETRY_TARGET_REQUIRED",
                 "Visual parameterization requires a known canonical Geometry binding.");
        return result;
    }
    if (!geometry->optimizationOwned) {
        result.valid = false;
        addError(result.diagnostics, adapterId().c_str(), binding.id, binding.targetObjectId,
                 "optimizationOwned", "PARAMETERIZED_GEOMETRY_OWNER_REQUIRED",
                 "User-authored visual geometry may not be rebuilt without explicit binding ownership.");
    }
    if (!hasFrame(baseline, geometry->referenceFrameId)) {
        result.valid = false;
        addError(result.diagnostics, adapterId().c_str(), binding.id, binding.targetObjectId,
                 "referenceFrameId", "PARAMETERIZED_GEOMETRY_REF_FRAME_INVALID",
                 "Visual geometry must name an existing canonical reference frame.");
    }
    if (geometry->kind == CanonicalGeometryKind::Mesh || !supportsProperty(geometry->kind, binding.targetPropertyId)) {
        result.valid = false;
        addError(result.diagnostics, adapterId().c_str(), binding.id, binding.targetObjectId,
                 "targetPropertyId", "PARAMETERIZED_GEOMETRY_SECTION_UNSUPPORTED",
                 "Only explicit primitive dimensions may be parameterized; meshes have no inferred section.");
    }
    if (binding.targetPropertyId != propertyFor(binding.semanticKind) ||
        binding.coordinateFrameId != geometry->referenceFrameId ||
        binding.geometryGroupId != "geometry:visual:" + geometry->id ||
        !exactTarget(binding.readSet, target(binding)) || !exactTarget(binding.writeSet, target(binding))) {
        result.valid = false;
        addError(result.diagnostics, adapterId().c_str(), binding.id, binding.targetObjectId,
                 "binding", "PARAMETERIZED_GEOMETRY_BINDING_INVALID",
                 "Visual geometry bindings require matching property, reference frame, group, and exact target sets.");
    }
    return result;
}

std::vector< ReadWriteTarget > ParameterizedGeometryAdapter::declaredReadSet(const ParameterBinding& b) const
{ return isGeometrySemantic(b.semanticKind) ? std::vector< ReadWriteTarget >{target(b)} : std::vector< ReadWriteTarget >(); }
std::vector< ReadWriteTarget > ParameterizedGeometryAdapter::declaredWriteSet(const ParameterBinding& b) const
{ return declaredReadSet(b); }

AdapterPatchCompileResult ParameterizedGeometryAdapter::compilePatch(const AdapterPatchCompileRequest& request) const
{
    AdapterPatchCompileResult result;
    if (request.baseline == nullptr || request.binding == nullptr) {
        addError(result.diagnostics, adapterId().c_str(), "", "", "request",
                 "PARAMETERIZED_GEOMETRY_REQUEST_REQUIRED", "Geometry compilation requires baseline and binding.");
        return result;
    }
    const ParameterBinding& binding = *request.binding;
    const ParameterBindingValidationResult generic = ParameterBindingValidator::validate(binding);
    if (!generic.valid) { result.diagnostics = generic.diagnostics; return result; }
    const AdapterBindingValidationResult valid = validateBinding(binding, *request.baseline);
    if (!valid.valid) { result.diagnostics = valid.diagnostics; return result; }
    const GeometryBinding* geometry = findBinding(*request.baseline, binding.targetObjectId);
    double radius, length, width, height, depth, wall;
    if (geometry == nullptr || !collectDimensions(request, *geometry, radius, length, width, height, depth,
                                                   wall, result.diagnostics)) {
        if (result.diagnostics.empty()) addError(result.diagnostics, adapterId().c_str(), binding.id,
            binding.targetObjectId, "values", "PARAMETERIZED_GEOMETRY_DIMENSIONS_INVALID",
            "Resolved primitive dimensions violate the declared geometry contract.");
        return result;
    }
    result.ok = true;
    result.patch.adapterId = adapterId(); result.patch.adapterVersion = adapterVersion();
    result.patch.bindingId = binding.id;
    for (const ResolvedAdapterValue& value : request.values)
        if (value.semanticKind == binding.semanticKind)
            result.patch.writes = {{target(binding), CandidatePatchValue::scalar(value.engineeringValue)}};
    result.patch.generatedArtifacts = {fingerprint("geometry", geometry->id, geometry->kind, radius, length,
                                                    width, height, depth, wall,
                                                    &geometry->referenceToGeometry)};
    return result;
}

std::string ParameterizedGeometryAdapter::describeEffect(const ParameterBinding&) const
{ return "Rebuilds an explicit owned visual primitive and records its geometry artifact fingerprint."; }

rw::math::Transform3D<> ParameterizedGeometryAdapter::segmentTransform(
    const rw::math::Vector3D<>& start, const rw::math::Vector3D<>& end)
{
    const rw::math::Vector3D<> delta = end - start;
    const double length = delta.norm2();
    if (!std::isfinite(length) || length <= 1e-12) return rw::math::Transform3D<>(start);
    const rw::math::Vector3D<> z = rw::math::Vector3D<>::z();
    const rw::math::Vector3D<> direction = delta / length;
    const double cosine = rw::math::dot(z, direction);
    rw::math::Rotation3D<> rotation;
    if (cosine < -1.0 + 1e-12) {
        // +Z to -Z is a 180 degree turn about X, never a meaningless turn about Z.
        rotation = rw::math::Rotation3D<>(1.0, 0.0, 0.0, 0.0, -1.0, 0.0, 0.0, 0.0, -1.0);
    } else if (cosine < 1.0 - 1e-12) {
        const rw::math::Vector3D<> axis = rw::math::cross(z, direction);
        const double sine = axis.norm2();
        const rw::math::Vector3D<> unit = axis / sine;
        const double c = cosine, s = sine, t = 1.0 - c;
        rotation = rw::math::Rotation3D<>(
            t * unit(0) * unit(0) + c, t * unit(0) * unit(1) - s * unit(2), t * unit(0) * unit(2) + s * unit(1),
            t * unit(0) * unit(1) + s * unit(2), t * unit(1) * unit(1) + c, t * unit(1) * unit(2) - s * unit(0),
            t * unit(0) * unit(2) - s * unit(1), t * unit(1) * unit(2) + s * unit(0), t * unit(2) * unit(2) + c);
    }
    return rw::math::Transform3D<>((start + end) / 2.0, rotation);
}

} // namespace rws
