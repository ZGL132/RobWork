// -----------------------------------------------------------------------------
//  GeometryParameterAdapters.cpp - ParameterizedLink / ParameterizedGeometry /
//  ParameterizedCollision / MeshTransform
//  Merged from four translation units; every adapter keeps its original
//  namespace, method definitions, registry IDs and diagnostics codes.  The
//  identical hasFrame and exactTarget are kept once; findBinding / target /
//  collectDimensions / addError differ per adapter and carry a prefix (an
//  adapter-local addError would otherwise hide the detail overload from
//  unqualified lookup).  Shared geometry_adapter_detail utilities come in
//  through a using-directive.  Logic
//  and validation order are line-identical to the pre-merge files.
// -----------------------------------------------------------------------------
#include "MeshTransformAdapter.hpp"
#include "ParameterizedCollisionAdapter.hpp"
#include "ParameterizedGeometryAdapter.hpp"
#include "ParameterizedLinkAdapter.hpp"

#include "GeometryAdapterSupport.hpp"

#include <algorithm>
#include <cmath>

namespace rws {
namespace {

using namespace geometry_adapter_detail;

// ---- shared: identical across the geometry adapters ----
bool hasFrame(const CanonicalKinematicModel& model, const std::string& id)
{
    for (const FrameNode& frame : model.frames) if (frame.id == id) return true;
    return false;
}

bool exactTarget(const std::vector< ReadWriteTarget >& values, const ReadWriteTarget& expected)
{
    return values.size() == 1 && values.front() == expected;
}


const double kMinimumLinkLengthMetres = 1e-6;

const JointEdge* findJoint(const CanonicalKinematicModel& baseline, const std::string& id)
{
    for (const JointEdge& joint : baseline.joints)
        if (joint.id == id)
            return &joint;
    return nullptr;
}

std::vector< ReadWriteTarget > translationTargets(const ParameterBinding& binding)
{
    return {{TargetObjectType::Joint, binding.targetObjectId,
             TargetPropertyId::ParentToJointTranslationX, binding.coordinateFrameId},
            {TargetObjectType::Joint, binding.targetObjectId,
             TargetPropertyId::ParentToJointTranslationY, binding.coordinateFrameId},
            {TargetObjectType::Joint, binding.targetObjectId,
             TargetPropertyId::ParentToJointTranslationZ, binding.coordinateFrameId}};
}

bool exactTargets(const std::vector< ReadWriteTarget >& actual,
                  const std::vector< ReadWriteTarget >& expected)
{
    if (actual.size() != expected.size())
        return false;
    for (std::size_t index = 0; index < actual.size(); ++index) {
        if (std::find(expected.begin(), expected.end(), actual[index]) == expected.end())
            return false;
        for (std::size_t other = index + 1; other < actual.size(); ++other)
            if (actual[index] == actual[other])
                return false;
    }
    return true;
}

bool isTranslationProperty(TargetPropertyId property)
{
    return property == TargetPropertyId::ParentToJointTranslationX ||
           property == TargetPropertyId::ParentToJointTranslationY ||
           property == TargetPropertyId::ParentToJointTranslationZ;
}

void link_addError(std::vector< StructureOptimizationDiagnostic >& diagnostics,
              const std::string& bindingId, const std::string& objectId,
              const std::string& field, const std::string& code, const std::string& message)
{
    diagnostics.push_back(makeAdapterDiagnostic("ParameterizedLinkAdapter", bindingId, objectId,
                                                field, code, message));
}


using namespace geometry_adapter_detail;

const GeometryBinding* geometryFindBinding(const CanonicalKinematicModel& model, const std::string& id)
{
    for (const GeometryBinding& value : model.geometryBindings)
        if (value.id == id) return &value;
    return nullptr;
}

ReadWriteTarget geometryTarget(const ParameterBinding& binding)
{
    return {TargetObjectType::Geometry, binding.targetObjectId, propertyFor(binding.semanticKind),
            binding.coordinateFrameId};
}

bool geometryCollectDimensions(const AdapterPatchCompileRequest& request, const GeometryBinding& binding,
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

using namespace geometry_adapter_detail;

const CollisionBinding* collisionFindBinding(const CanonicalKinematicModel& model, const std::string& id)
{
    for (const CollisionBinding& value : model.collisionBindings)
        if (value.id == id) return &value;
    return nullptr;
}
ReadWriteTarget collisionTarget(const ParameterBinding& binding)
{ return {TargetObjectType::CollisionGeometry, binding.targetObjectId, propertyFor(binding.semanticKind), binding.coordinateFrameId}; }
bool collisionCollectDimensions(const AdapterPatchCompileRequest& request, const CollisionBinding& binding,
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
ReadWriteTarget meshTarget(const ParameterBinding& binding)
{ return {binding.targetObjectType, binding.targetObjectId, TargetPropertyId::GeometryRigidTransform,
          binding.coordinateFrameId}; }
}    // namespace


std::string ParameterizedLinkAdapter::adapterId() const { return "ParameterizedLinkAdapter"; }
int ParameterizedLinkAdapter::adapterVersion() const { return 1; }
std::vector< SemanticKind > ParameterizedLinkAdapter::supportedSemanticKinds() const
{
    return {SemanticKind::LinkLength};
}
std::vector< AdapterCapability > ParameterizedLinkAdapter::requiredCapabilities() const
{
    return {AdapterCapability::ParameterizedLink};
}

AdapterBindingValidationResult ParameterizedLinkAdapter::validateBinding(
    const ParameterBinding& binding, const CanonicalKinematicModel& baseline) const
{
    AdapterBindingValidationResult result;
    const JointEdge* joint = findJoint(baseline, binding.targetObjectId);
    if (binding.semanticKind != SemanticKind::LinkLength) {
        result.valid = false;
        link_addError(result.diagnostics, binding.id, binding.targetObjectId, "semanticKind",
                 "PARAMETERIZED_LINK_SEMANTIC_UNSUPPORTED",
                 "ParameterizedLinkAdapter supports LinkLength only.");
    }
    if (binding.targetObjectType != TargetObjectType::Joint || joint == nullptr ||
        joint->type == CanonicalJointType::Fixed) {
        result.valid = false;
        link_addError(result.diagnostics, binding.id, binding.targetObjectId, "targetObjectId",
                 "PARAMETERIZED_LINK_MOVABLE_JOINT_REQUIRED",
                 "LinkLength requires a known movable joint.");
        return result;
    }
    if (binding.coordinateFrameId != joint->parentFrameId ||
        binding.referenceDirectionFrameId != joint->parentFrameId) {
        result.valid = false;
        link_addError(result.diagnostics, binding.id, binding.targetObjectId, "referenceDirectionFrameId",
                 "PARAMETERIZED_LINK_REFERENCE_DIRECTION_PARENT_FRAME_REQUIRED",
                 "LinkLength reference direction must be explicitly expressed in the target joint parent frame.");
    }
    if (!isTranslationProperty(binding.targetPropertyId)) {
        result.valid = false;
        link_addError(result.diagnostics, binding.id, binding.targetObjectId, "targetPropertyId",
                 "PARAMETERIZED_LINK_PRIMARY_PROPERTY_INVALID",
                 "LinkLength requires a parent-to-joint translation primary property.");
    }
    const std::vector< ReadWriteTarget > expected = translationTargets(binding);
    if (!exactTargets(binding.readSet, expected) || !exactTargets(binding.writeSet, expected)) {
        result.valid = false;
        link_addError(result.diagnostics, binding.id, binding.targetObjectId, "readSet/writeSet",
                 "PARAMETERIZED_LINK_TRANSLATION_SET_INVALID",
                 "LinkLength bindings must declare exactly X/Y/Z parent-to-joint translation targets.");
    }
    return result;
}

std::vector< ReadWriteTarget > ParameterizedLinkAdapter::declaredReadSet(
    const ParameterBinding& binding) const
{
    return translationTargets(binding);
}
std::vector< ReadWriteTarget > ParameterizedLinkAdapter::declaredWriteSet(
    const ParameterBinding& binding) const
{
    return translationTargets(binding);
}

AdapterPatchCompileResult ParameterizedLinkAdapter::compilePatch(
    const AdapterPatchCompileRequest& request) const
{
    AdapterPatchCompileResult result;
    if (request.baseline == nullptr || request.binding == nullptr) {
        link_addError(result.diagnostics, "", "", "request", "PARAMETERIZED_LINK_REQUEST_REQUIRED",
                 "ParameterizedLinkAdapter requires immutable baseline and binding inputs.");
        return result;
    }
    const ParameterBinding& binding = *request.binding;
    if (request.values.size() != 1 || request.values[0].unit != DesignVariableUnit::Metres ||
        !request.values[0].discreteOptionId.empty() || !std::isfinite(request.values[0].engineeringValue)) {
        link_addError(result.diagnostics, binding.id, binding.targetObjectId, "values",
                 "PARAMETERIZED_LINK_SINGLE_METRE_VALUE_REQUIRED",
                 "LinkLength compilation requires exactly one finite scalar metre value.");
        return result;
    }
    const double requestedLength = request.values[0].engineeringValue;
    if (requestedLength <= kMinimumLinkLengthMetres) {
        link_addError(result.diagnostics, binding.id, binding.targetObjectId, "values[0]",
                 "PARAMETERIZED_LINK_LENGTH_TOO_SMALL",
                 "LinkLength must exceed the positive minimum length.");
        return result;
    }
    const JointEdge* joint = findJoint(*request.baseline, binding.targetObjectId);
    if (joint == nullptr)
        return result;
    const double nominalLength = rw::math::dot(joint->parentToJointZero.P(), binding.referenceDirection);
    const rw::math::Vector3D<> next = joint->parentToJointZero.P() +
        (requestedLength - nominalLength) * binding.referenceDirection;
    result.ok = true;
    result.patch.adapterId = adapterId();
    result.patch.adapterVersion = adapterVersion();
    result.patch.bindingId = binding.id;
    const std::vector< ReadWriteTarget > targets = translationTargets(binding);
    result.patch.writes = {{targets[0], CandidatePatchValue::scalar(next(0))},
                           {targets[1], CandidatePatchValue::scalar(next(1))},
                           {targets[2], CandidatePatchValue::scalar(next(2))}};
    return result;
}

std::string ParameterizedLinkAdapter::describeEffect(const ParameterBinding&) const
{
    return "Sets baseline-relative link length along an explicit parent-frame reference direction.";
}



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
    const GeometryBinding* geometry = geometryFindBinding(baseline, binding.targetObjectId);
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
        !exactTarget(binding.readSet, geometryTarget(binding)) || !exactTarget(binding.writeSet, geometryTarget(binding))) {
        result.valid = false;
        addError(result.diagnostics, adapterId().c_str(), binding.id, binding.targetObjectId,
                 "binding", "PARAMETERIZED_GEOMETRY_BINDING_INVALID",
                 "Visual geometry bindings require matching property, reference frame, group, and exact geometryTarget sets.");
    }
    return result;
}

std::vector< ReadWriteTarget > ParameterizedGeometryAdapter::declaredReadSet(const ParameterBinding& b) const
{ return isGeometrySemantic(b.semanticKind) ? std::vector< ReadWriteTarget >{geometryTarget(b)} : std::vector< ReadWriteTarget >(); }
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
    const GeometryBinding* geometry = geometryFindBinding(*request.baseline, binding.targetObjectId);
    double radius, length, width, height, depth, wall;
    if (geometry == nullptr || !geometryCollectDimensions(request, *geometry, radius, length, width, height, depth,
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
            result.patch.writes = {{geometryTarget(binding), CandidatePatchValue::scalar(value.engineeringValue)}};
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
    const CollisionBinding* collision = collisionFindBinding(baseline, binding.targetObjectId);
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
        !exactTarget(binding.readSet, collisionTarget(binding)) || !exactTarget(binding.writeSet, collisionTarget(binding))) {
        result.valid = false;
        addError(result.diagnostics, adapterId().c_str(), binding.id, binding.targetObjectId,
                 "binding", "PARAMETERIZED_COLLISION_BINDING_INVALID",
                 "Collision bindings require matching property, reference frame, group, and exact collisionTarget sets.");
    }
    return result;
}
std::vector< ReadWriteTarget > ParameterizedCollisionAdapter::declaredReadSet(const ParameterBinding& b) const
{ return isGeometrySemantic(b.semanticKind) ? std::vector< ReadWriteTarget >{collisionTarget(b)} : std::vector< ReadWriteTarget >(); }
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
    const CollisionBinding* collision = collisionFindBinding(*request.baseline, binding.targetObjectId);
    double radius, length, width, height, depth, wall;
    if (collision == nullptr || !collisionCollectDimensions(request, *collision, radius, length, width, height,
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
            result.patch.writes = {{collisionTarget(binding), CandidatePatchValue::scalar(value.engineeringValue)}};
    result.patch.generatedArtifacts = {fingerprint("collision", collision->id, collision->kind, radius,
                                                    length, width, height, depth, wall,
                                                    &collision->referenceToGeometry)};
    return result;
}
std::string ParameterizedCollisionAdapter::describeEffect(const ParameterBinding&) const
{ return "Rebuilds an explicit owned collision primitive and records a separate collision artifact fingerprint."; }



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
    const ReadWriteTarget expected = meshTarget(binding);
    if (binding.targetPropertyId != TargetPropertyId::GeometryRigidTransform ||
        binding.coordinateFrameId != referenceFrame ||
        binding.geometryGroupId != "geometry:mesh:" + binding.targetObjectId ||
        binding.readSet.size() != 1 || binding.writeSet.size() != 1 ||
        !(binding.readSet.front() == expected) || !(binding.writeSet.front() == expected)) {
        result.valid = false;
        addError(result.diagnostics, adapterId().c_str(), binding.id, binding.targetObjectId,
                 "binding", "MESH_RIGID_TRANSFORM_BINDING_INVALID",
                 "Rigid mesh transform bindings require their exact meshTarget, reference frame, and mesh group.");
    }
    return result;
}
std::vector< ReadWriteTarget > MeshTransformAdapter::declaredReadSet(const ParameterBinding& b) const
{ return b.semanticKind == SemanticKind::GeometryRigidTransform ? std::vector< ReadWriteTarget >{meshTarget(b)} : std::vector< ReadWriteTarget >(); }
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
    result.patch.writes = {{meshTarget(binding), CandidatePatchValue::artifactReference(artifact)}};
    result.patch.generatedArtifacts = {artifact};
    return result;
}
std::string MeshTransformAdapter::describeEffect(const ParameterBinding&) const
{ return "Emits an explicitly authorized rigid mesh-transform artifact; it never derives a mesh section or scale."; }

}    // namespace rws
