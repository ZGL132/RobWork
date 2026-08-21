#include "DependencyGraph.hpp"

#include <algorithm>
#include <cmath>
#include <functional>

namespace rws {
namespace {

void addError(DerivedExpressionEvaluationResult& result, const std::string& code,
              const std::string& fieldPath, const std::string& message)
{
    StructureOptimizationDiagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.severity = "Error";
    diagnostic.subsystem = "design-space";
    diagnostic.stage = "derived-expression";
    diagnostic.fieldPath = fieldPath;
    diagnostic.message = message;
    result.diagnostics.push_back(diagnostic);
}

bool multiplyUnit(DesignVariableUnit first, DesignVariableUnit second, DesignVariableUnit& result)
{
    if (first == DesignVariableUnit::Unitless) { result = second; return true; }
    if (second == DesignVariableUnit::Unitless) { result = first; return true; }
    return false;
}

bool divideUnit(DesignVariableUnit first, DesignVariableUnit second, DesignVariableUnit& result)
{
    if (second == DesignVariableUnit::Unitless) { result = first; return true; }
    if (first == second) { result = DesignVariableUnit::Unitless; return true; }
    return false;
}

}    // namespace

DerivedExpressionEvaluationResult DependencyGraph::evaluate(
    const std::vector< DerivedExpression >& expressions,
    const std::map< std::string, DerivedValue >& resolvedVariables)
{
    DerivedExpressionEvaluationResult result;
    std::map< std::string, const DerivedExpression* > expressionById;
    for (const DerivedExpression& expression : expressions) {
        if (expression.id.empty() || !expressionById.emplace(expression.id, &expression).second) {
            addError(result, "DERIVED_EXPRESSION_ID_INVALID", expression.id,
                     "Derived expression IDs must be non-empty and unique.");
            return result;
        }
    }
    std::map< std::string, int > state;
    std::map< std::string, DerivedValue > values;
    std::vector< std::string > order;
    std::function< bool(const std::string&) > evaluateExpression;
    evaluateExpression = [&](const std::string& id) {
        const int currentState = state[id];
        if (currentState == 2) return true;
        if (currentState == 1) {
            addError(result, "DERIVED_EXPRESSION_CYCLE", id, "Derived expressions contain a dependency cycle.");
            return false;
        }
        const auto expression = expressionById.find(id);
        if (expression == expressionById.end()) return false;
        state[id] = 1;
        std::vector< DerivedValue > operands;
        for (const DerivedExpressionOperand& operand : expression->second->operands) {
            if (!operand.isVariableReference) {
                operands.push_back(operand.constantValue);
                continue;
            }
            const auto source = resolvedVariables.find(operand.variableId);
            if (source != resolvedVariables.end()) {
                operands.push_back(source->second);
                continue;
            }
            if (expressionById.find(operand.variableId) == expressionById.end()) {
                addError(result, "DERIVED_EXPRESSION_REFERENCE_UNKNOWN", id,
                         "An expression references an unknown resolved variable or expression.");
                return false;
            }
            if (!evaluateExpression(operand.variableId)) return false;
            operands.push_back(values[operand.variableId]);
        }
        if (operands.empty()) {
            addError(result, "DERIVED_EXPRESSION_OPERAND_MISSING", id, "An expression requires operands.");
            return false;
        }
        DerivedValue output = operands.front();
        const DerivedExpressionKind kind = expression->second->kind;
        if (kind == DerivedExpressionKind::RegisteredFunction) {
            if (expression->second->registeredFunctionId != "abs" || operands.size() != 1) {
                addError(result, "DERIVED_EXPRESSION_FUNCTION_UNREGISTERED", id,
                         "Only the registered unary abs function is available in the first phase.");
                return false;
            }
            output.value = std::fabs(operands.front().value);
        } else if (kind == DerivedExpressionKind::VariableRef || kind == DerivedExpressionKind::Constant) {
            if (operands.size() != 1) {
                addError(result, "DERIVED_EXPRESSION_OPERAND_INVALID", id,
                         "Constant and variable-reference expressions have exactly one operand.");
                return false;
            }
        } else if (kind == DerivedExpressionKind::Clamp) {
            if (operands.size() != 3 || operands[0].unit != operands[1].unit ||
                operands[0].unit != operands[2].unit || operands[1].value > operands[2].value) {
                addError(result, "DERIVED_EXPRESSION_OPERAND_INVALID", id,
                         "Clamp requires value, lower, and upper operands with one unit and lower <= upper.");
                return false;
            }
            output.value = std::max(operands[1].value,
                                    std::min(operands[0].value, operands[2].value));
        } else if (kind == DerivedExpressionKind::Norm) {
            for (const DerivedValue& operand : operands)
                if (operand.unit != output.unit) {
                    addError(result, "DERIVED_EXPRESSION_UNIT_MISMATCH", id,
                             "Norm operands require matching units.");
                    return false;
                }
            output.value = 0.0;
            for (const DerivedValue& operand : operands)
                output.value += operand.value * operand.value;
            output.value = std::sqrt(output.value);
        } else if (operands.size() != 2) {
            addError(result, "DERIVED_EXPRESSION_OPERAND_INVALID", id, "Binary expressions require two operands.");
            return false;
        } else if (kind == DerivedExpressionKind::Multiply) {
            if (!multiplyUnit(operands[0].unit, operands[1].unit, output.unit)) {
                addError(result, "DERIVED_EXPRESSION_UNIT_INVALID", id, "Unsupported multiplication unit propagation.");
                return false;
            }
            output.value = operands[0].value * operands[1].value;
        } else if (kind == DerivedExpressionKind::Divide) {
            if (operands[1].value == 0.0) {
                addError(result, "DERIVED_EXPRESSION_DIVIDE_BY_ZERO", id, "Division by zero is invalid.");
                return false;
            }
            if (!divideUnit(operands[0].unit, operands[1].unit, output.unit)) {
                addError(result, "DERIVED_EXPRESSION_UNIT_INVALID", id, "Unsupported division unit propagation.");
                return false;
            }
            output.value = operands[0].value / operands[1].value;
        } else {
            if (operands[0].unit != operands[1].unit) {
                addError(result, "DERIVED_EXPRESSION_UNIT_MISMATCH", id, "Additive operands require matching units.");
                return false;
            }
            output.value = kind == DerivedExpressionKind::Add ? operands[0].value + operands[1].value :
                kind == DerivedExpressionKind::Subtract ? operands[0].value - operands[1].value :
                kind == DerivedExpressionKind::Min ? std::min(operands[0].value, operands[1].value) :
                std::max(operands[0].value, operands[1].value);
        }
        if (!std::isfinite(output.value)) {
            addError(result, "DERIVED_EXPRESSION_NONFINITE", id, "An expression produced a non-finite value.");
            return false;
        }
        values[id] = output;
        order.push_back(id);
        state[id] = 2;
        return true;
    };
    for (const auto& entry : expressionById)
        if (!evaluateExpression(entry.first)) {
            result.values.clear();
            result.evaluationOrder.clear();
            return result;
        }
    result.ok = true;
    result.values = values;
    result.evaluationOrder = order;
    return result;
}

}    // namespace rws
