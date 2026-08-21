#ifndef RWS_STRUCTUREOPTIMIZATION_DERIVEDEXPRESSION_HPP
#define RWS_STRUCTUREOPTIMIZATION_DERIVEDEXPRESSION_HPP

#include "DesignVariable.hpp"

namespace rws {

enum class DerivedExpressionKind { Constant, VariableRef, Add, Subtract, Multiply, Divide, Min, Max, Clamp, Norm, RegisteredFunction };

struct DerivedValue
{
    double value = 0.0;
    DesignVariableUnit unit = DesignVariableUnit::Unitless;
};

struct DerivedExpressionOperand
{
    bool isVariableReference = false;
    std::string variableId;
    DerivedValue constantValue;

    static DerivedExpressionOperand variable(const std::string& id);
    static DerivedExpressionOperand constant(double value, DesignVariableUnit unit);
};

struct DerivedExpression
{
    std::string id;
    DerivedExpressionKind kind = DerivedExpressionKind::Constant;
    std::string registeredFunctionId;
    std::vector< DerivedExpressionOperand > operands;

    static DerivedExpression variableReference(const std::string& id, const std::string& variableId);
};

struct DerivedExpressionTargetValidationResult
{
    bool valid = true;
    std::vector< StructureOptimizationDiagnostic > diagnostics;
};

/** Limit bounds are constraints, never derived overwrite targets. */
class DerivedExpressionTargetValidator
{
  public:
    static DerivedExpressionTargetValidationResult validate(
        const std::vector< DesignVariableDefinition >& variables);
};

}    // namespace rws

#endif    // RWS_STRUCTUREOPTIMIZATION_DERIVEDEXPRESSION_HPP
