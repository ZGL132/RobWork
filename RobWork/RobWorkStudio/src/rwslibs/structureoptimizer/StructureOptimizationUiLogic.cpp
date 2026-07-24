#include "StructureOptimizationUiLogic.hpp"

#include "StructureOptimizationValidation.hpp"

#include <cmath>
#include <sstream>

using namespace rws;

namespace {

bool nonZero(double value)
{
    return std::isfinite(value) && std::abs(value) > 1e-12;
}

StructureDesignVariable makeLengthVariable(const std::string& id,
                                           const std::string& label,
                                           const std::string& target,
                                           StructureVariableKind kind,
                                           double currentValue)
{
    StructureDesignVariable variable;
    variable.id = id;
    variable.label = label;
    variable.targetName = target;
    variable.unit = "m";
    variable.kind = kind;
    variable.currentValue = currentValue;
    variable.minimum = currentValue * 0.7;
    variable.maximum = currentValue * 1.3;
    if (variable.minimum > variable.maximum)
        std::swap(variable.minimum, variable.maximum);
    variable.step = 0.001;
    variable.preferredValue = currentValue;
    variable.enabled = true;
    return variable;
}

void appendTransformPositionVariable(std::vector<StructureDesignVariable>& variables,
                                     const JointTransformSpec& joint,
                                     int axis)
{
    static const char* axisNames[] = {"x", "y", "z"};
    static const StructureVariableKind kinds[] = {
        StructureVariableKind::JointPositionX,
        StructureVariableKind::JointPositionY,
        StructureVariableKind::JointPositionZ
    };
    const double value = joint.pos[static_cast<std::size_t>(axis)];
    if (!nonZero(value))
        return;

    std::ostringstream id;
    id << joint.name << "_pos_" << axisNames[axis];
    std::ostringstream label;
    label << joint.name << " Pos " << axisNames[axis];
    variables.push_back(makeLengthVariable(
        id.str(), label.str(), joint.name, kinds[axis], value));
}

void appendTcpVariable(std::vector<StructureDesignVariable>& variables,
                       const JointTransformSpec& joint,
                       int axis)
{
    static const char* axisNames[] = {"x", "y", "z"};
    static const StructureVariableKind kinds[] = {
        StructureVariableKind::TcpOffsetX,
        StructureVariableKind::TcpOffsetY,
        StructureVariableKind::TcpOffsetZ
    };
    const double value = joint.pos[static_cast<std::size_t>(axis)];
    if (!nonZero(value))
        return;

    std::ostringstream id;
    id << joint.name << "_tcp_" << axisNames[axis];
    std::ostringstream label;
    label << joint.name << " TCP " << axisNames[axis];
    variables.push_back(makeLengthVariable(
        id.str(), label.str(), joint.name, kinds[axis], value));
}

void appendDrawableVariables(std::vector<StructureDesignVariable>& variables,
                             const DrawableSpec& drawable)
{
    if (!drawable.autoLinkGeometry)
        return;

    if (nonZero(drawable.radius)) {
        StructureDesignVariable variable = makeLengthVariable(
            drawable.name + "_radius",
            drawable.name + " Radius",
            drawable.name,
            StructureVariableKind::LinkRadius,
            drawable.radius);
        variable.syncAssociatedGeometry = true;
        variables.push_back(variable);
    }

    for (int axis = 0; axis < 3; ++axis) {
        const double value = drawable.dimensions[static_cast<std::size_t>(axis)];
        if (!nonZero(value))
            continue;
        StructureDesignVariable variable = makeLengthVariable(
            drawable.name + "_dim_" + std::to_string(axis),
            drawable.name + (axis == 1 ? " Width" : " Height"),
            drawable.name,
            axis == 1 ? StructureVariableKind::LinkWidth
                      : StructureVariableKind::LinkHeight,
            value);
        variable.syncAssociatedGeometry = true;
        variables.push_back(variable);
    }
}

} // namespace

std::vector<StructureDesignVariable>
StructureOptimizationUiLogic::suggestVariables(const RobotDesignContext& context)
{
    std::vector<StructureDesignVariable> variables;
    const RobotModelSpec& spec = context.modelSpec;

    for (const JointTransformSpec& joint : spec.transformJoints) {
        for (int axis = 0; axis < 3; ++axis)
            appendTransformPositionVariable(variables, joint, axis);

        if (typeToKind(joint.type) == JointKind::ToolFrame) {
            for (int axis = 0; axis < 3; ++axis)
                appendTcpVariable(variables, joint, axis);
        }
    }

    if (nonZero(spec.robotBaseFrame.pos[2])) {
        variables.push_back(makeLengthVariable(
            "base_height",
            "Base Height",
            spec.robotBaseFrame.name.empty() ? "RobotBase" : spec.robotBaseFrame.name,
            StructureVariableKind::BaseHeight,
            spec.robotBaseFrame.pos[2]));
    }

    for (const DrawableSpec& drawable : spec.drawables)
        appendDrawableVariables(variables, drawable);

    return variables;
}

bool StructureOptimizationUiLogic::hasRunnableInputs(
    const StructureOptimizationProblem& problem, std::string* reason)
{
    const std::vector<AnalysisWarning> warnings =
        StructureOptimizationValidation::validateProblem(problem);
    for (const AnalysisWarning& warning : warnings) {
        if (warning.severity == AnalysisStatus::Fail ||
            warning.code == "StructureOptimization.Context.Invalid") {
            if (reason != nullptr) {
                if (warning.code == "StructureOptimization.Context.Invalid")
                    *reason = "RobotDesignContext.ModelSpec.Incomplete: " +
                              warning.message;
                else
                    *reason = warning.code + ": " + warning.message;
            }
            return false;
        }
    }

    bool hasEnabledVariable = false;
    for (const StructureDesignVariable& variable : problem.variables) {
        if (variable.enabled) {
            hasEnabledVariable = true;
            break;
        }
    }
    if (!hasEnabledVariable) {
        if (reason != nullptr)
            *reason = "至少需要一个启用的设计变量。";
        return false;
    }

    bool hasEnabledTask = false;
    for (const OptimizationTaskPoint& task : problem.tasks) {
        if (task.point.enabled) {
            hasEnabledTask = true;
            break;
        }
    }
    if (!hasEnabledTask) {
        if (reason != nullptr)
            *reason = "至少需要一个启用的任务点。";
        return false;
    }

    if (reason != nullptr)
        reason->clear();
    return true;
}
