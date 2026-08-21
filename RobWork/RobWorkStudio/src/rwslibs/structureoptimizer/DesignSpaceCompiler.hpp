#ifndef RWS_STRUCTUREOPTIMIZATION_DESIGNSPACECOMPILER_HPP
#define RWS_STRUCTUREOPTIMIZATION_DESIGNSPACECOMPILER_HPP

#include "CanonicalKinematicModel.hpp"
#include "CompiledDesignSpace.hpp"
#include "DerivedExpression.hpp"
#include "DesignSpaceRegistry.hpp"
#include "ParameterizationMode.hpp"

namespace rws {

class AdapterRegistry;

struct DesignSpaceCompileRequest
{
    const CanonicalKinematicModel* model = nullptr;
    const DesignSpaceRegistry* registry = nullptr;
    const AdapterCapabilityQuery* capabilities = nullptr;
    /** Borrowed trusted adapter registry; its material is read internally. */
    const AdapterRegistry* adapterRegistry = nullptr;
    std::vector< DesignVariableDefinition > variables;
    std::vector< ParameterBinding > bindings;
    std::vector< ParameterizationSelection > parameterizationSelections;
    std::vector< DerivedExpression > derivedExpressions;
};

struct DesignSpaceCompileResult
{
    bool ok = false;
    CompiledDesignSpace designSpace;
    std::vector< StructureOptimizationDiagnostic > diagnostics;
};

class DesignSpaceCompiler
{
  public:
    static DesignSpaceCompileResult compile(const DesignSpaceCompileRequest& request);
};

}    // namespace rws

#endif    // RWS_STRUCTUREOPTIMIZATION_DESIGNSPACECOMPILER_HPP
