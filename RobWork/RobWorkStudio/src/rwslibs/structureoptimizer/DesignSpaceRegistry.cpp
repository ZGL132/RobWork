#include "DesignSpaceRegistry.hpp"

#include <sstream>

namespace rws {

void AdapterCapabilityQuery::grant(TargetObjectType objectType, const std::string& objectId,
                                   AdapterCapability capability)
{
    _capabilities[ObjectKey(objectType, objectId)].insert(capability);
}

bool AdapterCapabilityQuery::supports(TargetObjectType objectType, const std::string& objectId,
                                      AdapterCapability capability) const
{
    const auto entry = _capabilities.find(ObjectKey(objectType, objectId));
    return entry != _capabilities.end() && entry->second.find(capability) != entry->second.end();
}

std::string AdapterCapabilityQuery::fingerprintMaterial() const
{
    std::ostringstream stream;
    stream << "adapter-capabilities-v1\\n";
    for (const auto& entry : _capabilities) {
        stream << static_cast< int >(entry.first.first) << '|' << entry.first.second.size() << ':'
               << entry.first.second << '|';
        for (const AdapterCapability capability : entry.second)
            stream << static_cast< int >(capability) << ',';
        stream << '\\n';
    }
    return stream.str();
}

bool DesignSpaceRegistry::registerSemantic(const SemanticMetadata& metadata)
{
    if (metadata.semanticKind == SemanticKind::Unknown ||
        _metadata.find(metadata.semanticKind) != _metadata.end())
        return false;
    _metadata[metadata.semanticKind] = metadata;
    return true;
}

const SemanticMetadata* DesignSpaceRegistry::find(SemanticKind semanticKind) const
{
    const auto found = _metadata.find(semanticKind);
    return found == _metadata.end() ? nullptr : &found->second;
}

std::vector< DesignVariableSuggestion > DesignSpaceRegistry::suggest(
    const CanonicalKinematicModel& model, const AdapterCapabilityQuery& capabilities) const
{
    std::vector< DesignVariableSuggestion > result;
    const bool supportsJointAxisU = find(SemanticKind::JointAxisTiltU) != nullptr;
    const bool supportsJointAxisV = find(SemanticKind::JointAxisTiltV) != nullptr;
    if (!supportsJointAxisU && !supportsJointAxisV)
        return result;
    for (const JointEdge& joint : model.joints) {
        if (joint.type == CanonicalJointType::Fixed)
            continue;
        if (capabilities.supports(TargetObjectType::Joint, joint.id,
                                  AdapterCapability::JointAxisTilt)) {
            const struct AxisTiltDefinition {
                SemanticKind semantic;
                TargetPropertyId property;
                const char* idSuffix;
            } definitions[] = {
                {SemanticKind::JointAxisTiltU, TargetPropertyId::MotionAxisTiltU, "U"},
                {SemanticKind::JointAxisTiltV, TargetPropertyId::MotionAxisTiltV, "V"}};
            for (const AxisTiltDefinition& definition : definitions) {
                if ((definition.semantic == SemanticKind::JointAxisTiltU && !supportsJointAxisU) ||
                    (definition.semantic == SemanticKind::JointAxisTiltV && !supportsJointAxisV))
                    continue;
                DesignVariableSuggestion suggestion;
                suggestion.variable.id = std::string("JointAxisTilt") + definition.idSuffix + ":" + joint.id;
                suggestion.variable.displayName = std::string("Joint axis tilt ") +
                    definition.idSuffix + " " + joint.name;
                suggestion.variable.semanticKind = definition.semantic;
                suggestion.variable.role = VariableRole::Independent;
                suggestion.variable.domain = VariableDomain::Continuous;
                suggestion.variable.minimum = -0.5235987755982988;
                suggestion.variable.maximum = 0.5235987755982988;
                suggestion.variable.step = 0.01;
                suggestion.variable.unit = DesignVariableUnit::Radians;
                suggestion.variable.frameId = joint.parentFrameId;
                suggestion.variable.groupId = "axis-tilt:" + joint.id;
                suggestion.variable.bindingId = "binding:" + suggestion.variable.id;
                suggestion.variable.source = DesignVariableSource::Template;
                suggestion.binding.id = suggestion.variable.bindingId;
                suggestion.binding.semanticKind = definition.semantic;
                suggestion.binding.targetObjectType = TargetObjectType::Joint;
                suggestion.binding.targetObjectId = joint.id;
                suggestion.binding.targetPropertyId = definition.property;
                suggestion.binding.coordinateFrameId = joint.parentFrameId;
                suggestion.binding.ownerAdapterId = "JointAxisAdapter";
                suggestion.binding.ownerAdapterVersion = 1;
                suggestion.binding.maxAxisTiltAngle = 0.5235987755982988;
                suggestion.binding.axisTiltGroupId = suggestion.variable.groupId;
                suggestion.binding.writeSet = {{TargetObjectType::Joint, joint.id,
                                                definition.property, joint.parentFrameId}};
                suggestion.binding.readSet = suggestion.binding.writeSet;
                result.push_back(suggestion);
            }
        }
    }
    return result;
}

DesignSpaceRegistry DesignSpaceRegistry::firstPhase()
{
    DesignSpaceRegistry registry;
    const SemanticMetadata definitions[] = {
        {SemanticKind::LinkLength, VariableDomain::Continuous, DesignVariableUnit::Metres,
         SemanticApplicability::ParameterizedLink},
        {SemanticKind::JointOriginOffsetX, VariableDomain::Continuous, DesignVariableUnit::Metres,
         SemanticApplicability::MovableJoint},
        {SemanticKind::JointOriginOffsetY, VariableDomain::Continuous, DesignVariableUnit::Metres,
         SemanticApplicability::MovableJoint},
        {SemanticKind::JointOriginOffsetZ, VariableDomain::Continuous, DesignVariableUnit::Metres,
         SemanticApplicability::MovableJoint},
        {SemanticKind::JointOffsetAlongAxis, VariableDomain::Continuous,
         DesignVariableUnit::Metres, SemanticApplicability::MovableJoint},
        {SemanticKind::JointAxisTiltU, VariableDomain::Continuous, DesignVariableUnit::Radians,
         SemanticApplicability::MovableJoint},
        {SemanticKind::JointAxisTiltV, VariableDomain::Continuous, DesignVariableUnit::Radians,
         SemanticApplicability::MovableJoint},
        {SemanticKind::JointZeroOffset, VariableDomain::Continuous, DesignVariableUnit::Radians,
         SemanticApplicability::MovableJoint},
        {SemanticKind::JointLimitLower, VariableDomain::Continuous, DesignVariableUnit::Radians,
         SemanticApplicability::MovableJoint},
        {SemanticKind::JointLimitUpper, VariableDomain::Continuous, DesignVariableUnit::Radians,
         SemanticApplicability::MovableJoint},
        {SemanticKind::BaseTx, VariableDomain::Continuous, DesignVariableUnit::Metres,
         SemanticApplicability::BaseFrame},
        {SemanticKind::BaseTy, VariableDomain::Continuous, DesignVariableUnit::Metres,
         SemanticApplicability::BaseFrame},
        {SemanticKind::BaseTz, VariableDomain::Continuous, DesignVariableUnit::Metres,
         SemanticApplicability::BaseFrame},
        {SemanticKind::BaseRotationVectorX, VariableDomain::Continuous,
         DesignVariableUnit::Radians, SemanticApplicability::BaseFrame},
        {SemanticKind::BaseRotationVectorY, VariableDomain::Continuous,
         DesignVariableUnit::Radians, SemanticApplicability::BaseFrame},
        {SemanticKind::BaseRotationVectorZ, VariableDomain::Continuous,
         DesignVariableUnit::Radians, SemanticApplicability::BaseFrame},
        {SemanticKind::TcpTx, VariableDomain::Continuous, DesignVariableUnit::Metres,
         SemanticApplicability::ToolBinding},
        {SemanticKind::TcpTy, VariableDomain::Continuous, DesignVariableUnit::Metres,
         SemanticApplicability::ToolBinding},
        {SemanticKind::TcpTz, VariableDomain::Continuous, DesignVariableUnit::Metres,
         SemanticApplicability::ToolBinding},
        {SemanticKind::TcpRotationVectorX, VariableDomain::Continuous,
         DesignVariableUnit::Radians, SemanticApplicability::ToolBinding},
        {SemanticKind::TcpRotationVectorY, VariableDomain::Continuous,
         DesignVariableUnit::Radians, SemanticApplicability::ToolBinding},
        {SemanticKind::TcpRotationVectorZ, VariableDomain::Continuous,
         DesignVariableUnit::Radians, SemanticApplicability::ToolBinding},
        {SemanticKind::FlangeTx, VariableDomain::Continuous, DesignVariableUnit::Metres,
         SemanticApplicability::FlangeFrame},
        {SemanticKind::FlangeTy, VariableDomain::Continuous, DesignVariableUnit::Metres,
         SemanticApplicability::FlangeFrame},
        {SemanticKind::FlangeTz, VariableDomain::Continuous, DesignVariableUnit::Metres,
         SemanticApplicability::FlangeFrame},
        {SemanticKind::FlangeRotationVectorX, VariableDomain::Continuous,
         DesignVariableUnit::Radians, SemanticApplicability::FlangeFrame},
        {SemanticKind::FlangeRotationVectorY, VariableDomain::Continuous,
         DesignVariableUnit::Radians, SemanticApplicability::FlangeFrame},
        {SemanticKind::FlangeRotationVectorZ, VariableDomain::Continuous,
         DesignVariableUnit::Radians, SemanticApplicability::FlangeFrame},
        {SemanticKind::LinkRadius, VariableDomain::Continuous, DesignVariableUnit::Metres,
         SemanticApplicability::ParameterizedGeometry},
        {SemanticKind::LinkWidth, VariableDomain::Continuous, DesignVariableUnit::Metres,
         SemanticApplicability::ParameterizedGeometry},
        {SemanticKind::LinkHeight, VariableDomain::Continuous, DesignVariableUnit::Metres,
         SemanticApplicability::ParameterizedGeometry},
        {SemanticKind::LinkCrossSectionX, VariableDomain::Continuous,
         DesignVariableUnit::Metres, SemanticApplicability::ParameterizedGeometry},
        {SemanticKind::LinkCrossSectionY, VariableDomain::Continuous,
         DesignVariableUnit::Metres, SemanticApplicability::ParameterizedGeometry},
        {SemanticKind::LinkWallThickness, VariableDomain::Continuous,
         DesignVariableUnit::Metres, SemanticApplicability::ParameterizedGeometry},
        {SemanticKind::LinkScale, VariableDomain::Continuous, DesignVariableUnit::Unitless,
         SemanticApplicability::ParameterizedGeometry},
        {SemanticKind::GeometryRadius, VariableDomain::Continuous, DesignVariableUnit::Metres,
         SemanticApplicability::ParameterizedGeometry},
        {SemanticKind::GeometryLength, VariableDomain::Continuous, DesignVariableUnit::Metres,
         SemanticApplicability::ParameterizedGeometry},
        {SemanticKind::GeometryWidth, VariableDomain::Continuous, DesignVariableUnit::Metres,
         SemanticApplicability::ParameterizedGeometry},
        {SemanticKind::GeometryHeight, VariableDomain::Continuous, DesignVariableUnit::Metres,
         SemanticApplicability::ParameterizedGeometry},
        {SemanticKind::GeometryDepth, VariableDomain::Continuous, DesignVariableUnit::Metres,
         SemanticApplicability::ParameterizedGeometry},
        {SemanticKind::GeometryWallThickness, VariableDomain::Continuous,
         DesignVariableUnit::Metres, SemanticApplicability::ParameterizedGeometry},
        {SemanticKind::GeometryRigidTransform, VariableDomain::Continuous,
         DesignVariableUnit::Unitless, SemanticApplicability::ParameterizedGeometry},
        {SemanticKind::ParameterizedMaterial, VariableDomain::Discrete,
         DesignVariableUnit::Unitless, SemanticApplicability::ParameterizedGeometry}};
    for (const SemanticMetadata& metadata : definitions)
        registry.registerSemantic(metadata);
    return registry;
}

}    // namespace rws
