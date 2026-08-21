#ifndef RWS_STRUCTUREOPTIMIZATION_COMPILEDDESIGNSPACE_HPP
#define RWS_STRUCTUREOPTIMIZATION_COMPILEDDESIGNSPACE_HPP

#include "ParameterBinding.hpp"
#include "ParameterizationMode.hpp"
#include "DerivedExpression.hpp"

#include <map>

namespace rws {

struct CanonicalVectorSchemaEntry
{
    std::string variableId;
    std::size_t index = 0;
    DesignVariableUnit unit = DesignVariableUnit::Unitless;

    bool operator==(const CanonicalVectorSchemaEntry& other) const
    {
        return variableId == other.variableId && index == other.index && unit == other.unit;
    }
};

struct CompiledVariableGroup
{
    std::string id;
    std::vector< std::string > variableIds;
};

struct CompiledDesignSpace
{
    int schemaVersion = 1;
    std::string fingerprint;
    std::vector< DesignVariableDefinition > independentVariables;
    std::vector< DesignVariableDefinition > derivedVariables;
    std::vector< CompiledVariableGroup > variableGroups;
    std::vector< ParameterizationSelection > parameterizationModes;
    std::vector< ParameterBinding > resolvedBindings;
    /** Expressions are retained so candidate compilation is self-contained. */
    std::vector< DerivedExpression > derivedExpressions;
    std::vector< std::string > dependencyOrder;
    std::vector< CanonicalVectorSchemaEntry > canonicalVectorSchema;
    std::map< std::string, std::string > disabledReasons;
};

}    // namespace rws

#endif    // RWS_STRUCTUREOPTIMIZATION_COMPILEDDESIGNSPACE_HPP
