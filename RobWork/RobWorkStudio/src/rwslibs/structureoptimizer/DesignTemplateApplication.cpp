#include "DesignTemplateApplication.hpp"

#include <algorithm>
#include <set>

namespace rws {
namespace {

bool containsSemantic(const std::vector< SemanticKind >& semantics, SemanticKind semantic)
{
    return std::find(semantics.begin(), semantics.end(), semantic) != semantics.end();
}

bool suggestionLess(const DesignVariableSuggestion& first, const DesignVariableSuggestion& second)
{
    return first.variable.id < second.variable.id;
}

}    // namespace

TemplateApplicationPreview DesignTemplateApplication::preview(
    DesignIntentTemplateKind kind, const CanonicalKinematicModel& model,
    const DesignSpaceRegistry& registry, const AdapterCapabilityQuery& capabilities,
    const std::vector< DesignVariableDefinition >& existingVariables)
{
    TemplateApplicationPreview preview;
    const DesignIntentTemplateInfo* const definition =
        StructureOptimizationTemplate::designIntent(kind);
    if (definition == nullptr) {
        StructureOptimizationDiagnostic diagnostic;
        diagnostic.code = "DESIGN_TEMPLATE_UNKNOWN";
        diagnostic.severity = "Error";
        diagnostic.subsystem = "design-space";
        diagnostic.stage = "template-preview";
        diagnostic.fieldPath = "template";
        diagnostic.message = "The requested design-intent template is not registered.";
        preview.diagnostics.push_back(diagnostic);
        return preview;
    }
    preview.templateId = definition->id;
    preview.templateVersion = definition->version;

    std::set< SemanticKind > generatedSemantics;
    const std::vector< DesignVariableSuggestion > suggestions = registry.suggest(model, capabilities);
    for (const DesignVariableSuggestion& suggestion : suggestions) {
        if (!containsSemantic(definition->semanticKinds, suggestion.variable.semanticKind))
            continue;
        generatedSemantics.insert(suggestion.variable.semanticKind);
        const auto existing = std::find_if(
            existingVariables.begin(), existingVariables.end(),
            [&suggestion](const DesignVariableDefinition& variable) {
                return variable.id == suggestion.variable.id;
            });
        if (existing != existingVariables.end())
            preview.alreadyPresent.push_back(*existing);
        else
            preview.toAdd.push_back(suggestion);
    }

    for (const SemanticKind semantic : definition->semanticKinds) {
        if (generatedSemantics.find(semantic) == generatedSemantics.end())
            preview.inapplicable.push_back({semantic, std::string(), "BindingUnavailable"});
    }
    std::sort(preview.toAdd.begin(), preview.toAdd.end(), suggestionLess);
    std::sort(preview.alreadyPresent.begin(), preview.alreadyPresent.end(),
              [](const DesignVariableDefinition& first, const DesignVariableDefinition& second) {
                  return first.id < second.id;
              });
    return preview;
}

}    // namespace rws
