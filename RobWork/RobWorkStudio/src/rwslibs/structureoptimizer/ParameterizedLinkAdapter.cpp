#include "ParameterizedLinkAdapter.hpp"

#include <algorithm>
#include <cmath>

namespace rws {
namespace {

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

void addError(std::vector< StructureOptimizationDiagnostic >& diagnostics,
              const std::string& bindingId, const std::string& objectId,
              const std::string& field, const std::string& code, const std::string& message)
{
    diagnostics.push_back(makeAdapterDiagnostic("ParameterizedLinkAdapter", bindingId, objectId,
                                                field, code, message));
}

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
        addError(result.diagnostics, binding.id, binding.targetObjectId, "semanticKind",
                 "PARAMETERIZED_LINK_SEMANTIC_UNSUPPORTED",
                 "ParameterizedLinkAdapter supports LinkLength only.");
    }
    if (binding.targetObjectType != TargetObjectType::Joint || joint == nullptr ||
        joint->type == CanonicalJointType::Fixed) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId, "targetObjectId",
                 "PARAMETERIZED_LINK_MOVABLE_JOINT_REQUIRED",
                 "LinkLength requires a known movable joint.");
        return result;
    }
    if (binding.coordinateFrameId != joint->parentFrameId ||
        binding.referenceDirectionFrameId != joint->parentFrameId) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId, "referenceDirectionFrameId",
                 "PARAMETERIZED_LINK_REFERENCE_DIRECTION_PARENT_FRAME_REQUIRED",
                 "LinkLength reference direction must be explicitly expressed in the target joint parent frame.");
    }
    if (!isTranslationProperty(binding.targetPropertyId)) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId, "targetPropertyId",
                 "PARAMETERIZED_LINK_PRIMARY_PROPERTY_INVALID",
                 "LinkLength requires a parent-to-joint translation primary property.");
    }
    const std::vector< ReadWriteTarget > expected = translationTargets(binding);
    if (!exactTargets(binding.readSet, expected) || !exactTargets(binding.writeSet, expected)) {
        result.valid = false;
        addError(result.diagnostics, binding.id, binding.targetObjectId, "readSet/writeSet",
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
        addError(result.diagnostics, "", "", "request", "PARAMETERIZED_LINK_REQUEST_REQUIRED",
                 "ParameterizedLinkAdapter requires immutable baseline and binding inputs.");
        return result;
    }
    const ParameterBinding& binding = *request.binding;
    if (request.values.size() != 1 || request.values[0].unit != DesignVariableUnit::Metres ||
        !request.values[0].discreteOptionId.empty() || !std::isfinite(request.values[0].engineeringValue)) {
        addError(result.diagnostics, binding.id, binding.targetObjectId, "values",
                 "PARAMETERIZED_LINK_SINGLE_METRE_VALUE_REQUIRED",
                 "LinkLength compilation requires exactly one finite scalar metre value.");
        return result;
    }
    const double requestedLength = request.values[0].engineeringValue;
    if (requestedLength <= kMinimumLinkLengthMetres) {
        addError(result.diagnostics, binding.id, binding.targetObjectId, "values[0]",
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

}    // namespace rws
