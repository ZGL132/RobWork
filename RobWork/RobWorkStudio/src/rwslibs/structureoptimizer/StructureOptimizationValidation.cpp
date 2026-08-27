#include "StructureOptimizationValidation.hpp"

#include "StructureOptimizationUiLogic.hpp"


#include <rwslibs/robotanalysiscore/EngineeringMetricRegistry.hpp>

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

// Only variables that change a kinematic transform participate in the
// DH/Transform exclusivity check. Geometry parameters can coexist with
// either representation.
bool isTransformVariable(StructureVariableKind kind)
{
    switch (kind) {
    case StructureVariableKind::JointPositionX:
    case StructureVariableKind::JointPositionY:
    case StructureVariableKind::JointPositionZ:
    case StructureVariableKind::JointRotationRoll:
    case StructureVariableKind::JointRotationPitch:
    case StructureVariableKind::JointRotationYaw:
    case StructureVariableKind::BaseHeight:
    case StructureVariableKind::TcpOffsetX:
    case StructureVariableKind::TcpOffsetY:
    case StructureVariableKind::TcpOffsetZ:
        return true;
    default:
        return false;
    }
}

// 辅助: 判断 double 是否有限
bool isFinite(double v)
{
    return std::isfinite(v);
}

} // anonymous namespace

bool StructureOptimizationValidation::hasCompleteModel(const RobotModelSpec& spec,
                                                       std::string* reason)
{
    if (spec.robotName.empty())
    {
        if (reason != nullptr)
        {
            *reason = "StructureOptimization.Context.Invalid: robotName must be non-empty.";
        }
        return false;
    }
    if (spec.transformJoints.empty())
    {
        if (reason != nullptr)
        {
            *reason = "StructureOptimization.Context.Invalid: transformJoints must "
                      "contain at least one joint.";
        }
        return false;
    }
    if (reason != nullptr)
        reason->clear();
    return true;
}

std::vector< AnalysisWarning > StructureOptimizationValidation::validateProblem(
    const StructureOptimizationProblem& problem)
{
    std::vector< AnalysisWarning > warnings;

    // ── 1. 检查上下文完整性 ─────────────────────────────────────────────
    const RobotModelSpec& spec = problem.context.modelSpec;
    if (!hasCompleteModel(spec))
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
            else if (isTransformVariable(v.kind))
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
        std::set<std::string> taskIds;
        for (const auto& t : problem.tasks)
        {
            if (t.point.id.empty())
                warnings.push_back(makeWarning(
                    "StructureOptimization.Task.InvalidId",
                    "Task ID must not be empty."));
            else if (!taskIds.insert(t.point.id).second)
                warnings.push_back(makeWarning(
                    "StructureOptimization.Task.DuplicateId",
                    "Duplicate task ID: '" + t.point.id + "'."));
            if (t.point.enabled)
            {
                hasEnabledTask = true;
                bool finitePose = true;
                for (double value : t.point.position)
                    finitePose = finitePose && isFinite(value);
                for (double value : t.point.rpyDeg)
                    finitePose = finitePose && isFinite(value);
                if (!finitePose || !isFinite(t.point.weight) || t.point.weight <= 0.0)
                    warnings.push_back(makeWarning(
                        "StructureOptimization.Task.InvalidNumericValue",
                        "Enabled task '" + t.point.id +
                        "' has non-finite pose values or a non-positive weight."));
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
        std::vector<WorkspaceCoverageBox> boxes = problem.evaluation.coverageBoxes;
        if (boxes.empty() && problem.evaluation.coverageBox.enabled)
            boxes.push_back(problem.evaluation.coverageBox);
        for (const WorkspaceCoverageBox& box : boxes) {
            if (!box.enabled) continue;
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

    const EngineeringMetricRegistry& registry = EngineeringMetricRegistry::standard();
    std::set<std::string> objectiveMetricIds;
    for (const ObjectiveTerm& objective : problem.objectives)
    {
        if (!objectiveMetricIds.insert(objective.metricId).second)
            warnings.push_back(makeWarning(
                "StructureOptimization.Objective.DuplicateMetric",
                "Duplicate objective metric ID: '" + objective.metricId + "'."));
        if (registry.find(objective.metricId) == nullptr)
            warnings.push_back(makeWarning(
                "StructureOptimization.Objective.UnknownMetric",
                "Objective references an unknown metric: '" + objective.metricId + "'."));
        if (!isFinite(objective.weight) || objective.weight < 0.0)
            warnings.push_back(makeWarning(
                "StructureOptimization.Objective.InvalidWeight",
                "Objective '" + objective.metricId + "' has an invalid weight."));
        if (!isFinite(objective.normalization.good) ||
            !isFinite(objective.normalization.bad) ||
            objective.normalization.good == objective.normalization.bad)
            warnings.push_back(makeWarning(
                "StructureOptimization.Objective.InvalidNormalization",
                "Objective '" + objective.metricId + "' has an invalid normalization range."));
    }

    for (const ConstraintRule& constraint : problem.metricConstraints)
    {
        if (registry.find(constraint.metricId) == nullptr)
            warnings.push_back(makeWarning(
                "StructureOptimization.MetricConstraint.UnknownMetric",
                "Metric constraint references an unknown metric: '" + constraint.metricId + "'."));
        if (!isFinite(constraint.threshold))
            warnings.push_back(makeWarning(
                "StructureOptimization.MetricConstraint.InvalidThreshold",
                "Metric constraint '" + constraint.metricId + "' has a non-finite threshold."));
    }

    for (const StructureDesignVariable& variable : problem.variables)
    {
        if (variable.enabled &&
            variable.domainDefinition.domain != DesignVariableDomain::Continuous)
            warnings.push_back(makeWarning(
                "StructureOptimization.Variable.Domain.Unsupported",
                "Integer and discrete variables are not supported by the P1 search strategies: '" +
                    variable.id + "'."));
    }

    // ── N. 冻结契约一致性(C1.1/D1): stale/未验证为阻断级发现,使所有直接
    // 使用 validateProblem 的调用方与 Preflight/Controller 共享同一结论。──
    if (rws::StructureOptimizationUiLogic::frozenContractStale(problem))
    {
        const bool hasReference = !rws::StructureOptimizationUiLogic::
                                       frozenReferenceFingerprint(problem).empty();
        warnings.push_back(makeWarning(
            "StructureOptimization.FrozenContract.Stale",
            hasReference
                ? "Frozen requirements are stale: edits diverge from the frozen execution contract."
                : "Frozen execution contract is unverified: no editable-contract reference fingerprint."));
    }

    return warnings;
}

} // namespace rws
