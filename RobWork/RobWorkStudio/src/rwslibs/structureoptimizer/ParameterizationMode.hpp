#ifndef RWS_STRUCTUREOPTIMIZATION_PARAMETERIZATIONMODE_HPP
#define RWS_STRUCTUREOPTIMIZATION_PARAMETERIZATIONMODE_HPP

#include "DesignVariable.hpp"

#include <map>

namespace rws {

struct ParameterizationMode
{
    std::string id;
    std::string groupId;
    std::vector< SemanticKind > semanticKinds;
};

struct ParameterizationSelection
{
    std::string groupId;
    std::string modeId;
};

struct ParameterizationResolution
{
    bool valid = true;
    std::vector< DesignVariableDefinition > variables;
    std::map< std::string, std::string > disabledReasons;
    std::vector< StructureOptimizationDiagnostic > diagnostics;
};

class ParameterizationModeRegistry
{
  public:
    static ParameterizationModeRegistry firstPhase();
    const ParameterizationMode* find(const std::string& id) const;

  private:
    std::map< std::string, ParameterizationMode > _modes;
    friend class ParameterizationModeResolver;
};

class ParameterizationModeResolver
{
  public:
    static ParameterizationResolution resolve(
        const std::vector< DesignVariableDefinition >& variables,
        const ParameterizationModeRegistry& registry,
        const std::vector< ParameterizationSelection >& selections);
};

}    // namespace rws

#endif    // RWS_STRUCTUREOPTIMIZATION_PARAMETERIZATIONMODE_HPP
