#include "StructureOptimizationValidation.hpp"

#include <cmath>
#include <set>
#include <string>
#include <vector>

namespace rws {

namespace {

// 辅助: 创建一个 AnalysisWarning
AnalysisWarning makeWarning(
    const std::string& code,
    const std::string& message,
    AnalysisStatus severity = AnalysisStatus::Fail)
{
    AnalysisWarning w;
    w.code     = code;
    w.message  = message;
    w.source   = "StructureOptimization";
    w.severity = severity;
    return w;
}

// 辅助: 判断一个变量是否为 DH 类
bool isDhVariable(StructureVariableKind kind)
{
    return kind == StructureVariableKind::DhA ||
           kind == StructureVariableKind::DhD;
}

// 辅助: 判断 double 是否有限
bool isFinite(double v)
{
    return std::isfinite(v);
}

} // anonymous namespace

std::vector< AnalysisWarning > StructureOptimizationValidation::validateProblem(
    const StructureOptimizationProblem& problem)
{
    std::vector< AnalysisWarning > warnings;

    // ── 1. 检查上下文完整性 ─────────────────────────────────────────────
    const RobotModelSpec& spec = problem.context.modelSpec;
    if (spec.robotName.empty() || spec.transformJoints.empty())
    {
        warnings.push_back(makeWarning(
            "StructureOptimization.Context.Invalid",
            "Robot design context is incomplete: robotName must be non-empty "
            "and transformJoints must contain at least one joint."));
    }

    // ── 2. 至少一个启用的设计变量 ──────────────────────────────────────
    bool hasEnabledVariable = false;
    for (const auto& v : problem.variables)
    {
        if (v.enabled)
        {
            hasEnabledVariable = true;
            break;
        }
    }
    if (!hasEnabledVariable)
    {
        warnings.push_back(makeWarning(
            "StructureOptimization.Variable.NoneEnabled",
            "At least one design variable must be enabled."));
    }

    // ── 3. 变量 ID 唯一性 ───────────────────────────────────────────────
    {
        std::set< std::string > ids;
        for (const auto& v : problem.variables)
        {
            if (!ids.insert(v.id).second)
            {
                warnings.push_back(makeWarning(
                    "StructureOptimization.Variable.DuplicateId",
                    "Duplicate variable ID: '" + v.id + "'."));
            }
        }
    }

    // ── 4. 每个变量: targetName 非空, 有限值, 边界合法, step > 0 ──────
    for (const auto& v : problem.variables)
    {
        if (!v.enabled)
            continue;

        if (v.targetName.empty())
        {
            warnings.push_back(makeWarning(
                "StructureOptimization.Variable.InvalidBounds",
                "Variable '" + v.id + "' has an empty targetName."));
        }

        if (!isFinite(v.currentValue) || !isFinite(v.minimum) ||
            !isFinite(v.maximum) || !isFinite(v.step))
        {
            warnings.push_back(makeWarning(
                "StructureOptimization.Variable.InvalidBounds",
                "Variable '" + v.id + "' has non-finite value (current/min/max/step)."));
        }

        if (v.minimum > v.currentValue || v.currentValue > v.maximum)
        {
            warnings.push_back(makeWarning(
                "StructureOptimization.Variable.InvalidBounds",
                "Variable '" + v.id + "' current value is outside [minimum, maximum]."));
        }

        if (v.step <= 0.0)
        {
            warnings.push_back(makeWarning(
                "StructureOptimization.Variable.InvalidBounds",
                "Variable '" + v.id + "' step must be > 0."));
        }
    }

    // ── 5. 不混用 DH 与 Transform 变量 ──────────────────────────────────
    {
        bool hasDh       = false;
        bool hasTransform = false;
        for (const auto& v : problem.variables)
        {
            if (!v.enabled)
                continue;
            if (isDhVariable(v.kind))
                hasDh = true;
            else
                hasTransform = true;
        }
        if (hasDh && hasTransform)
        {
            warnings.push_back(makeWarning(
                "StructureOptimization.Variable.MixedKinematicsSource",
                "Cannot mix DH variables (DhA/DhD) with Transform variables "
                "(JointPositionX/Y/Z, JointRotationRoll/Pitch/Yaw, etc.)."));
        }
    }

    // ── 6. 至少一个启用的任务点 ─────────────────────────────────────────
    {
        bool hasEnabledTask = false;
        for (const auto& t : problem.tasks)
        {
            if (t.point.enabled)
            {
                hasEnabledTask = true;
                break;
            }
        }
        if (!hasEnabledTask)
        {
            warnings.push_back(makeWarning(
                "StructureOptimization.Task.NoneEnabled",
                "At least one task point must be enabled."));
        }
    }

    // ── 7. 权重非负且总和 > 0 ──────────────────────────────────────────
    {
        const auto& w = problem.weights;
        double sum = w.reachability + w.manipulability + w.jointMargin +
                     w.collision + w.compactness + w.preference;
        if (w.reachability < 0.0 || w.manipulability < 0.0 ||
            w.jointMargin < 0.0 || w.collision < 0.0 ||
            w.compactness < 0.0 || w.preference < 0.0 ||
            sum <= 0.0)
        {
            warnings.push_back(makeWarning(
                "StructureOptimization.Weights.Invalid",
                "All weights must be non-negative and their sum must be > 0."));
        }
    }

    // ── 8. 候选/精英数合法性 ───────────────────────────────────────────
    {
        const auto& r = problem.run;
        if (r.candidateCount <= 0 || r.eliteCount <= 0 ||
            r.eliteCount > r.candidateCount ||
            r.localEliteCount <= 0 || r.localEliteCount > r.eliteCount ||
            r.finalVerificationCount <= 0 ||
            r.finalVerificationCount > r.eliteCount ||
            r.maxLocalSweeps <= 0 || r.gridSteps <= 0)
        {
            warnings.push_back(makeWarning(
                "StructureOptimization.Run.InvalidCounts",
                "Candidate/elite counts are invalid: ensure candidateCount > 0, "
                "eliteCount <= candidateCount, and all sweep/step counts > 0."));
        }
    }

    // ── 9. 覆盖网格单元格数在 [1, 100] ──────────────────────────────────
    {
        const auto& box = problem.evaluation.coverageBox;
        if (box.enabled)
        {
            for (int i = 0; i < 3; ++i)
            {
                if (box.cells[i] < 1 || box.cells[i] > 100)
                {
                    warnings.push_back(makeWarning(
                        "StructureOptimization.Workspace.InvalidGrid",
                        "Coverage grid cells[" + std::to_string(i) +
                        "] = " + std::to_string(box.cells[i]) +
                        " is outside the valid range [1, 100]."));
                }
            }
        }
    }

    return warnings;
}

} // namespace rws
