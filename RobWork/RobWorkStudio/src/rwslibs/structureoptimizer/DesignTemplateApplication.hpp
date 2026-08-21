#ifndef RWS_STRUCTUREOPTIMIZATION_DESIGNTEMPLATEAPPLICATION_HPP
#define RWS_STRUCTUREOPTIMIZATION_DESIGNTEMPLATEAPPLICATION_HPP

#include "DesignSpaceRegistry.hpp"
#include "StructureOptimizationTemplate.hpp"

namespace rws {

struct TemplateApplicationIssue
{
    SemanticKind semanticKind = SemanticKind::Unknown;
    std::string objectId;
    std::string reason;
};

/** A non-mutating design-intent application result for a future UI to display. */
struct TemplateApplicationPreview
{
    std::string templateId;
    std::string templateVersion;
    std::vector< DesignVariableSuggestion > toAdd;
    std::vector< DesignVariableDefinition > alreadyPresent;
    std::vector< TemplateApplicationIssue > conflicts;
    std::vector< TemplateApplicationIssue > inapplicable;
    std::vector< TemplateApplicationIssue > disabled;
    std::vector< StructureOptimizationDiagnostic > diagnostics;
};

class DesignTemplateApplication
{
  public:
    static TemplateApplicationPreview preview(
        DesignIntentTemplateKind kind, const CanonicalKinematicModel& model,
        const DesignSpaceRegistry& registry, const AdapterCapabilityQuery& capabilities,
        const std::vector< DesignVariableDefinition >& existingVariables);
};

}    // namespace rws

#endif    // RWS_STRUCTUREOPTIMIZATION_DESIGNTEMPLATEAPPLICATION_HPP
