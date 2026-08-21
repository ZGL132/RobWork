#ifndef RWS_STRUCTUREOPTIMIZATION_DESIGNSPACEREGISTRY_HPP
#define RWS_STRUCTUREOPTIMIZATION_DESIGNSPACEREGISTRY_HPP

#include "AdapterCapability.hpp"
#include "CanonicalKinematicModel.hpp"

#include <map>

namespace rws {

enum class SemanticApplicability
{
    Any,
    MovableJoint,
    ParameterizedLink,
    BaseFrame,
    FlangeFrame,
    ToolBinding,
    ParameterizedGeometry
};

struct SemanticMetadata
{
    SemanticKind semanticKind = SemanticKind::Unknown;
    VariableDomain domain = VariableDomain::Continuous;
    DesignVariableUnit unit = DesignVariableUnit::Unitless;
    SemanticApplicability applicability = SemanticApplicability::Any;
};

struct DesignVariableSuggestion
{
    DesignVariableDefinition variable;
    ParameterBinding binding;
};

/** First-phase whitelist and capability-gated design-variable suggestions. */
class DesignSpaceRegistry
{
  public:
    bool registerSemantic(const SemanticMetadata& metadata);
    const SemanticMetadata* find(SemanticKind semanticKind) const;
    std::vector< DesignVariableSuggestion > suggest(
        const CanonicalKinematicModel& model, const AdapterCapabilityQuery& capabilities) const;

    static DesignSpaceRegistry firstPhase();

  private:
    std::map< SemanticKind, SemanticMetadata > _metadata;
};

}    // namespace rws

#endif    // RWS_STRUCTUREOPTIMIZATION_DESIGNSPACEREGISTRY_HPP
