#ifndef RWS_STRUCTUREOPTIMIZATION_WRITESETVALIDATOR_HPP
#define RWS_STRUCTUREOPTIMIZATION_WRITESETVALIDATOR_HPP

#include "ParameterBinding.hpp"

namespace rws {

struct WriteSetValidationResult
{
    bool valid = true;
    std::vector< StructureOptimizationDiagnostic > diagnostics;
};

/** Rejects active variables whose bindings claim the same physical write target. */
class WriteSetValidator
{
  public:
    static WriteSetValidationResult validate(
        const std::vector< DesignVariableDefinition >& variables,
        const std::vector< ParameterBinding >& bindings);
};

}    // namespace rws

#endif    // RWS_STRUCTUREOPTIMIZATION_WRITESETVALIDATOR_HPP
