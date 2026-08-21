#include "ParameterizedCollisionAdapter.hpp"

#include "GeometryAdapterSupport.hpp"

namespace rws {
namespace {
using namespace geometry_adapter_detail;

const CollisionBinding* findBinding(const CanonicalKinematicModel& model, const std::string& id)
{
    for (const CollisionBinding& value : model.collisionBindings)
        if (value.id == id) return &value;
    return nullptr;
}
bool hasFrame(const CanonicalKinematicModel& model, const std::string& id)
{
    for (const FrameNode& frame : model.frames) if (frame.id == id) return true;
    return false;
}
ReadWriteTarget target(const ParameterBinding& binding)
{ return {TargetObjectType::CollisionGeometry, binding.targetObjectId, propertyFor(binding.semanticKind), binding.coordinateFrameId}; }
bool exactTarget(const std::vector< ReadWriteTarget >& values, const ReadWriteTarget& expected)
{ return values.size() == 1 && values.front() == expected; }

bool collectDimensions(const AdapterPatchCompileRequest& request, const CollisionBinding& binding,
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
            addError(errors, "ParameterizedCollisionAdapter", request.binding->id,
                     request.binding->targetObjectId, "values", "PARAMETERIZED_COLLISION_VALUE_INVALID",
                     "Collision variables require finite metre scalar values from their explicit group.");
            return false;
        }
        if (value.semanticKind == request.binding->semanticKind) {
            if (foundPrimary) {
                addError(errors, "ParameterizedCollisionAdapter", request.binding->id,
                         request.binding->targetObjectId, "values",
                         "PARAMETERIZED_COLLISION_PRIMARY_VALUE_DUPLICATE",
                         "A collision binding may consume its primary dimension exactly once.");
                return false;
            }
            foundPrimary = true;
        }
        overrideDimension(value.semanticKind, value.engineeringValue, radius, length, width, height,
                          depth, wall);
    }
    if (!foundPrimary) {
        addError(errors, "ParameterizedCollisionAdapter", request.binding->id,
                 request.binding->targetObjectId, "values",
                 "PARAMETERIZED_COLLISION_PRIMARY_VALUE_REQUIRED",
                 "Collision compilation requires its primary dimension in the resolved group.");
        return false;
    }
    return validDimensions(binding.kind, radius, length, width, height, depth, wall);
}
} // namespace

std::string ParameterizedCollisionAdapter::adapterId() const { return "ParameterizedCollisionAdapter"; }
int ParameterizedCollisionAdapter::adapterVersion() const { return 1; }
std::vector< SemanticKind > ParameterizedCollisionAdapter::supportedSemanticKinds() const
{ return {SemanticKind::GeometryRadius, SemanticKind::GeometryLength, SemanticKind::GeometryWidth,
          SemanticKind::GeometryHeight, SemanticKind::GeometryDepth, SemanticKind::GeometryWallThickness}; }
std::vector< AdapterCapability > ParameterizedCollisionAdapter::requiredCapabilities() const
{ return {AdapterCapability::ParameterizedCollision}; }

AdapterBindingValidationResult ParameterizedCollisionAdapter::validateBinding(
    const ParameterBinding& binding, const CanonicalKinematicModel& baseline) const
{
    AdapterBindingValidationResult result;
    const CollisionBinding* collision = findBinding(baseline, binding.targetObjectId);
    if (!isGeometrySemantic(binding.semanticKind) || binding.targetObjectType != TargetObjectType::CollisionGeometry ||
        collision == nullptr) {
        result.valid = false;
        addError(result.diagnostics, adapterId().c_str(), binding.id, binding.targetObjectId,
                 "targetObjectId", "PARAMETERIZED_COLLISION_TARGET_REQUIRED",
                 "Collision parameterization requires a known canonical CollisionGeometry binding.");
        return result;
    }
    if (!collision->optimizationOwned) {
        result.valid = false;
        addError(result.diagnostics, adapterId().c_str(), binding.id, binding.targetObjectId,
                 "optimizationOwned", "PARAMETERIZED_COLLISION_OWNER_REQUIRED",
                 "User-authored collision geometry may not be rebuilt without explicit binding ownership.");
    }
    if (!hasFrame(baseline, collision->referenceFrameId)) {
        result.valid = false;
        addError(result.diagnostics, adapterId().c_str(), binding.id, binding.targetObjectId,
                 "referenceFrameId", "PARAMETERIZED_COLLISION_REF_FRAME_INVALID",
                 "Collision geometry must name an existing canonical reference frame.");
    }
    if (collision->kind == CanonicalGeometryKind::Mesh || !supportsProperty(collision->kind, binding.targetPropertyId)) {
        result.valid = false;
        addError(result.diagnostics, adapterId().c_str(), binding.id, binding.targetObjectId,
                 "targetPropertyId", "PARAMETERIZED_COLLISION_SECTION_UNSUPPORTED",
                 "Only explicit collision primitive dimensions may be parameterized; meshes have no inferred section.");
    }
    if (binding.targetPropertyId != propertyFor(binding.semanticKind) ||
        binding.coordinateFrameId != collision->referenceFrameId ||
        binding.geometryGroupId != "geometry:collision:" + collision->id ||
        !exactTarget(binding.readSet, target(binding)) || !exactTarget(binding.writeSet, target(binding))) {
        result.valid = false;
        addError(result.diagnostics, adapterId().c_str(), binding.id, binding.targetObjectId,
                 "binding", "PARAMETERIZED_COLLISION_BINDING_INVALID",
                 "Collision bindings require matching property, reference frame, group, and exact target sets.");
    }
    return result;
}
std::vector< ReadWriteTarget > ParameterizedCollisionAdapter::declaredReadSet(const ParameterBinding& b) const
{ return isGeometrySemantic(b.semanticKind) ? std::vector< ReadWriteTarget >{target(b)} : std::vector< ReadWriteTarget >(); }
std::vector< ReadWriteTarget > ParameterizedCollisionAdapter::declaredWriteSet(const ParameterBinding& b) const
{ return declaredReadSet(b); }
AdapterPatchCompileResult ParameterizedCollisionAdapter::compilePatch(const AdapterPatchCompileRequest& request) const
{
    AdapterPatchCompileResult result;
    if (request.baseline == nullptr || request.binding == nullptr) {
        addError(result.diagnostics, adapterId().c_str(), "", "", "request",
                 "PARAMETERIZED_COLLISION_REQUEST_REQUIRED", "Collision compilation requires baseline and binding.");
        return result;
    }
    const ParameterBinding& binding = *request.binding;
    const ParameterBindingValidationResult generic = ParameterBindingValidator::validate(binding);
    if (!generic.valid) { result.diagnostics = generic.diagnostics; return result; }
    const AdapterBindingValidationResult valid = validateBinding(binding, *request.baseline);
    if (!valid.valid) { result.diagnostics = valid.diagnostics; return result; }
    const CollisionBinding* collision = findBinding(*request.baseline, binding.targetObjectId);
    double radius, length, width, height, depth, wall;
    if (collision == nullptr || !collectDimensions(request, *collision, radius, length, width, height,
                                                    depth, wall, result.diagnostics)) {
        if (result.diagnostics.empty()) addError(result.diagnostics, adapterId().c_str(), binding.id,
            binding.targetObjectId, "values", "PARAMETERIZED_COLLISION_DIMENSIONS_INVALID",
            "Resolved collision primitive dimensions violate the declared collision contract.");
        return result;
    }
    result.ok = true;
    result.patch.adapterId = adapterId(); result.patch.adapterVersion = adapterVersion();
    result.patch.bindingId = binding.id;
    for (const ResolvedAdapterValue& value : request.values)
        if (value.semanticKind == binding.semanticKind)
            result.patch.writes = {{target(binding), CandidatePatchValue::scalar(value.engineeringValue)}};
    result.patch.generatedArtifacts = {fingerprint("collision", collision->id, collision->kind, radius,
                                                    length, width, height, depth, wall,
                                                    &collision->referenceToGeometry)};
    return result;
}
std::string ParameterizedCollisionAdapter::describeEffect(const ParameterBinding&) const
{ return "Rebuilds an explicit owned collision primitive and records a separate collision artifact fingerprint."; }

} // namespace rws
