#ifndef RWS_STRUCTUREOPTIMIZATION_DEPENDENCYGRAPH_HPP
#define RWS_STRUCTUREOPTIMIZATION_DEPENDENCYGRAPH_HPP

#include "DerivedExpression.hpp"

#include <map>

namespace rws {

struct DerivedExpressionEvaluationResult
{
    bool ok = false;
    std::vector< std::string > evaluationOrder;
    std::map< std::string, DerivedValue > values;
    std::vector< StructureOptimizationDiagnostic > diagnostics;
};

class DependencyGraph
{
  public:
    static DerivedExpressionEvaluationResult evaluate(
        const std::vector< DerivedExpression >& expressions,
        const std::map< std::string, DerivedValue >& resolvedVariables);
};

}    // namespace rws

#endif    // RWS_STRUCTUREOPTIMIZATION_DEPENDENCYGRAPH_HPP
