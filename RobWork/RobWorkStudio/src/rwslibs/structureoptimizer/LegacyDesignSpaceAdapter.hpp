#ifndef RWS_STRUCTUREOPTIMIZATION_LEGACYDESIGNSPACEADAPTER_HPP
#define RWS_STRUCTUREOPTIMIZATION_LEGACYDESIGNSPACEADAPTER_HPP

#include "ParameterBinding.hpp"
#include "StructureOptimizationTypes.hpp"

namespace rws {

/** Explicit source-to-binding evidence for a legacy variable whose old kind is ambiguous. */
struct LegacyDesignSpaceBindingHint
{
    std::string legacyVariableId;
    SemanticKind semanticKind = SemanticKind::Unknown;
    ParameterBinding binding;
};

struct LegacyDesignSpaceMigrationEntry
{
    StructureDesignVariable source;
    bool mapped = false;
    std::string disposition;
    DesignVariableDefinition variable;
    ParameterBinding binding;
};

/** Read-only compatibility preview.  It never writes the legacy project or JSON. */
struct LegacyDesignSpaceMigrationPreview
{
    std::vector< LegacyDesignSpaceMigrationEntry > entries;
    std::vector< DesignVariableDefinition > mappedVariables;
    std::vector< ParameterBinding > bindings;
    std::vector< StructureOptimizationDiagnostic > diagnostics;
};

class LegacyDesignSpaceAdapter
{
  public:
    static LegacyDesignSpaceMigrationPreview preview(
        const StructureOptimizationProblem& legacyProblem,
        const std::vector< LegacyDesignSpaceBindingHint >& bindingHints = {});
};

}    // namespace rws

#endif    // RWS_STRUCTUREOPTIMIZATION_LEGACYDESIGNSPACEADAPTER_HPP
