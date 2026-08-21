#include "MeshTransformAdapter.hpp"

#include "GeometryAdapterSupport.hpp"

#include <cmath>

namespace rws {
namespace {
using namespace geometry_adapter_detail;

bool knownOwnedMesh(const CanonicalKinematicModel& model, const ParameterBinding& binding,
                    std::string& referenceFrame)
{
    if (binding.targetObjectType == TargetObjectType::Geometry) {
        for (const GeometryBinding& geometry : model.geometryBindings) if (geometry.id == binding.targetObjectId) {
            referenceFrame = geometry.referenceFrameId;
            return geometry.kind == CanonicalGeometryKind::Mesh && geometry.optimizationOwned &&
                   geometry.allowRigidTransform;
        }
    }
    if (binding.targetObjectType == TargetObjectType::CollisionGeometry) {
        for (const CollisionBinding& collision : model.collisionBindings) if (collision.id == binding.targetObjectId) {
            referenceFrame = collision.referenceFrameId;
            return collision.kind == CanonicalGeometryKind::Mesh && collision.optimizationOwned &&
                   collision.allowRigidTransform;
        }
    }
    return false;
}
ReadWriteTarget target(const ParameterBinding& binding)
{ return {binding.targetObjectType, binding.targetObjectId, TargetPropertyId::GeometryRigidTransform,
          binding.coordinateFrameId}; }
}

std::string MeshTransformAdapter::adapterId() const { return "MeshTransformAdapter"; }
int MeshTransformAdapter::adapterVersion() const { return 1; }
std::vector< SemanticKind > MeshTransformAdapter::supportedSemanticKinds() const
{ return {SemanticKind::GeometryRigidTransform}; }
std::vector< AdapterCapability > MeshTransformAdapter::requiredCapabilities() const
{ return {AdapterCapability::ParameterizedGeometry}; }

AdapterBindingValidationResult MeshTransformAdapter::validateBinding(
    const ParameterBinding& binding, const CanonicalKinematicModel& baseline) const
{
    AdapterBindingValidationResult result;
    std::string referenceFrame;
    if (binding.semanticKind != SemanticKind::GeometryRigidTransform ||
        (binding.targetObjectType != TargetObjectType::Geometry &&
         binding.targetObjectType != TargetObjectType::CollisionGeometry) ||
        !knownOwnedMesh(baseline, binding, referenceFrame)) {
        result.valid = false;
        addError(result.diagnostics, adapterId().c_str(), binding.id, binding.targetObjectId,
                 "targetObjectId", "MESH_RIGID_TRANSFORM_NOT_AUTHORIZED",
                 "Rigid mesh transforms require an explicitly owned Mesh binding with transform capability.");
        return result;
    }
    const ReadWriteTarget expected = target(binding);
    if (binding.targetPropertyId != TargetPropertyId::GeometryRigidTransform ||
        binding.coordinateFrameId != referenceFrame ||
        binding.geometryGroupId != "geometry:mesh:" + binding.targetObjectId ||
        binding.readSet.size() != 1 || binding.writeSet.size() != 1 ||
        !(binding.readSet.front() == expected) || !(binding.writeSet.front() == expected)) {
        result.valid = false;
        addError(result.diagnostics, adapterId().c_str(), binding.id, binding.targetObjectId,
                 "binding", "MESH_RIGID_TRANSFORM_BINDING_INVALID",
                 "Rigid mesh transform bindings require their exact target, reference frame, and mesh group.");
    }
    return result;
}
std::vector< ReadWriteTarget > MeshTransformAdapter::declaredReadSet(const ParameterBinding& b) const
{ return b.semanticKind == SemanticKind::GeometryRigidTransform ? std::vector< ReadWriteTarget >{target(b)} : std::vector< ReadWriteTarget >(); }
std::vector< ReadWriteTarget > MeshTransformAdapter::declaredWriteSet(const ParameterBinding& b) const
{ return declaredReadSet(b); }
AdapterPatchCompileResult MeshTransformAdapter::compilePatch(const AdapterPatchCompileRequest& request) const
{
    AdapterPatchCompileResult result;
    if (request.baseline == nullptr || request.binding == nullptr) {
        addError(result.diagnostics, adapterId().c_str(), "", "", "request",
                 "MESH_RIGID_TRANSFORM_REQUEST_REQUIRED", "Mesh transform compilation requires baseline and binding.");
        return result;
    }
    const ParameterBinding& binding = *request.binding;
    const ParameterBindingValidationResult generic = ParameterBindingValidator::validate(binding);
    if (!generic.valid) { result.diagnostics = generic.diagnostics; return result; }
    const AdapterBindingValidationResult valid = validateBinding(binding, *request.baseline);
    if (!valid.valid) { result.diagnostics = valid.diagnostics; return result; }
    if (request.values.size() != 1 || request.values.front().semanticKind != binding.semanticKind ||
        request.values.front().groupId != binding.geometryGroupId ||
        request.values.front().unit != DesignVariableUnit::Unitless ||
        !request.values.front().discreteOptionId.empty() || !std::isfinite(request.values.front().engineeringValue)) {
        addError(result.diagnostics, adapterId().c_str(), binding.id, binding.targetObjectId, "values",
                 "MESH_RIGID_TRANSFORM_VALUE_INVALID",
                 "Rigid mesh transforms require one finite unitless transform-token value; scale is not supported.");
        return result;
    }
    const std::string artifact = fingerprint(binding.targetObjectType == TargetObjectType::Geometry ? "geometry-mesh" : "collision-mesh",
                                             binding.targetObjectId, CanonicalGeometryKind::Mesh,
                                             request.values.front().engineeringValue, 0, 0, 0, 0, 0);
    result.ok = true;
    result.patch.adapterId = adapterId(); result.patch.adapterVersion = adapterVersion();
    result.patch.bindingId = binding.id;
    result.patch.writes = {{target(binding), CandidatePatchValue::artifactReference(artifact)}};
    result.patch.generatedArtifacts = {artifact};
    return result;
}
std::string MeshTransformAdapter::describeEffect(const ParameterBinding&) const
{ return "Emits an explicitly authorized rigid mesh-transform artifact; it never derives a mesh section or scale."; }

} // namespace rws
