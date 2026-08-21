#include "DerivedExpression.hpp"

namespace rws {

DerivedExpressionOperand DerivedExpressionOperand::variable(const std::string& id)
{
    DerivedExpressionOperand operand;
    operand.isVariableReference = true;
    operand.variableId = id;
    return operand;
}

DerivedExpressionOperand DerivedExpressionOperand::constant(double value, DesignVariableUnit unit)
{
    DerivedExpressionOperand operand;
    operand.constantValue = {value, unit};
    return operand;
}

DerivedExpression DerivedExpression::variableReference(const std::string& id,
                                                       const std::string& variableId)
{
    DerivedExpression expression;
    expression.id = id;
    expression.kind = DerivedExpressionKind::VariableRef;
    expression.operands = {DerivedExpressionOperand::variable(variableId)};
    return expression;
}

DerivedExpressionTargetValidationResult DerivedExpressionTargetValidator::validate(
    const std::vector< DesignVariableDefinition >& variables)
{
    DerivedExpressionTargetValidationResult result;
    for (const DesignVariableDefinition& variable : variables) {
        if (variable.role != VariableRole::Derived ||
            (variable.semanticKind != SemanticKind::JointLimitLower &&
             variable.semanticKind != SemanticKind::JointLimitUpper))
            continue;
        result.valid = false;
        StructureOptimizationDiagnostic diagnostic;
        diagnostic.code = "DERIVED_EXPRESSION_TARGET_CONSTRAINT_ONLY";
        diagnostic.severity = "Error";
        diagnostic.subsystem = "design-space";
        diagnostic.stage = "derived-expression";
        diagnostic.fieldPath = variable.id;
        diagnostic.message = "Joint-limit bounds are constraints and may not be derived overwrite targets.";
        result.diagnostics.push_back(diagnostic);
    }
    return result;
}

}    // namespace rws
