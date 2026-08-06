#include "KinematicEngineeringEvaluator.hpp"
#include "StructureCandidateEvaluator.hpp"

#include <algorithm>
#include <exception>
#include <sstream>

namespace rws {

namespace {

// 把结构优化候选状态映射为公共工程评价状态，供上层工作流统一消费。
EngineeringEvaluationStatus statusFor(const StructureCandidateResult& candidate)
{
    switch (candidate.status) {
    case StructureCandidateStatus::Feasible:
        return EngineeringEvaluationStatus::Success;
    case StructureCandidateStatus::Infeasible:
        return EngineeringEvaluationStatus::Infeasible;
    case StructureCandidateStatus::Canceled:
        return EngineeringEvaluationStatus::Cancelled;
    case StructureCandidateStatus::Failed:
    case StructureCandidateStatus::Pending:
    default:
        return EngineeringEvaluationStatus::Failed;
    }
}

// 便捷写入函数：把 (id, 值, 单位, 提供者) 折叠为一条 EngineeringMetric 追加到结果。
void addMetric(EngineeringEvaluationResult& result, const std::string& id,
               double value, const std::string& unit, const std::string& provider)
{
    EngineeringMetric metric;
    metric.metricId = id;
    metric.value = value;
    metric.unit = unit;
    metric.providerId = provider;
    result.metrics.push_back(metric);
}

// 生成 IK 求解汇总的紧凑 JSON（每任务可用解数与可达性），
// 作为 "kinematics.ik-solutions" 工件交给上层做进一步解析与展示。
std::string ikSummary(const StructureRawMetrics& raw)
{
    std::ostringstream stream;
    stream << "{\"tasks\":[";
    for (std::size_t i = 0; i < raw.taskMetrics.size(); ++i) {
        const StructureTaskMetric& task = raw.taskMetrics[i];
        if (i != 0)
            stream << ',';
        stream << "{\"id\":\"" << task.taskId
               << "\",\"usableSolutionCount\":" << task.usableSolutionCount
               << ",\"reachable\":" << (task.reachable ? "true" : "false")
               << "}";
    }
    stream << "]}";
    return stream.str();
}

// 生成工作空间覆盖汇总 JSON（覆盖率、占用/总栅格数）。
std::string workspaceSummary(const StructureRawMetrics& raw)
{
    std::ostringstream stream;
    stream << "{\"coverage\":" << raw.workspaceCoverage
           << ",\"occupiedCellCount\":" << raw.workspaceOccupiedCellCount
           << ",\"totalCellCount\":" << raw.workspaceTotalCellCount
           << "}";
    return stream.str();
}

} // namespace

// 构造函数：持有问题引用。评价所需全部参数（阈值、采样、需求契约）都来自该
// 问题对象，使评估器可被无状态复用并保证相同输入产出可复现结果。
KinematicEngineeringEvaluator::KinematicEngineeringEvaluator(
    const StructureOptimizationProblem& problem) : _problem(problem)
{}

// 返回评估器标识；项目未配置时使用默认值 "structure.kinematics"。
std::string KinematicEngineeringEvaluator::id() const
{
    return _problem.evaluation.evaluatorId.empty()
        ? "structure.kinematics" : _problem.evaluation.evaluatorId;
}

// 返回评估器版本，标识产生该评价结果的算法版本，便于结果追溯与对比。
std::string KinematicEngineeringEvaluator::version() const
{
    return _problem.evaluation.evaluatorVersion.empty()
        ? "1" : _problem.evaluation.evaluatorVersion;
}

// 声明本评估器会产出的工件 ID，供上层订阅、导出与一致性校验。
std::vector<std::string> KinematicEngineeringEvaluator::providedArtifactIds() const
{
    return {"kinematics.ik-solutions", "kinematics.workspace.coverage-summary",
            "structure.raw-metrics"};
}

// 公共评价入口：把候选变量值交给底层 evaluateLegacy 完成完整运动学流程，再把
// 原始指标映射为工程级结果（状态、指标、约束违背与数据工件）。
// 语义要点：
//  - 底层异常统一捕获并降级为 Failed，绝不向上抛出，保证返回结构完整；
//  - DataInsufficient（证据不足）优先于 Infeasible 上报，避免把缺少证据当成不满足；
//  - 即使评分未触发约束失败，必需任务点未全部可达仍强制判为 Infeasible。
EngineeringEvaluationResult KinematicEngineeringEvaluator::evaluate(
    const CandidateEvaluationContext& context, const EvaluationRequest& request,
    const EvaluationCallbacks& callbacks)
{
    // 判断问题是否携带工作空间覆盖要求：旧覆盖盒或冻结需求的 Must 区域任一存在即视为有。
    const bool hasWorkspaceRequirements =
        !_problem.evaluation.coverageBoxes.empty() ||
        _problem.evaluation.coverageBox.enabled ||
        std::any_of(
            _problem.requirementExecution.workspaceRegions.begin(),
            _problem.requirementExecution.workspaceRegions.end(),
            [] (const RequirementExecutionRegion& region) {
                return region.level == RequirementExecutionLevel::Must &&
                       region.compileState == RequirementExecutionCompileState::Included;
            });
    StructureCandidateResult candidate;
    candidate.values = context.variableValues;
    StructureOptimizationCallbacks legacyCallbacks;
    legacyCallbacks.isCancellationRequested = callbacks.isCancellationRequested;
    EngineeringEvaluationResult result;
    result.providerId = id();
    result.providerVersion = version();
    result.inputSnapshot = context.inputSnapshot;
    // 底层评价可能抛出异常（模型构建失败、求解器崩溃等），统一在此转成带错误码
    // 的 Failed 结果，保证调用方总能拿到结构完整的返回值。
    try {
        evaluateLegacy(candidate,
                       request.stage == EngineeringEvaluationStage::Verified
                           ? StructureEvaluationStage::Verified
                           : StructureEvaluationStage::Quick,
                       legacyCallbacks, nullptr);
    } catch (const std::exception& error) {
        result.status = EngineeringEvaluationStatus::Failed;
        AnalysisWarning warning;
        warning.code = "KinematicEngineeringEvaluator.Exception";
        warning.message = error.what();
        warning.source = id();
        warning.severity = AnalysisStatus::Fail;
        result.warnings.push_back(warning);
        return result;
    } catch (...) {
        result.status = EngineeringEvaluationStatus::Failed;
        AnalysisWarning warning;
        warning.code = "KinematicEngineeringEvaluator.UnknownException";
        warning.message = "Unknown kinematic evaluation failure.";
        warning.source = id();
        warning.severity = AnalysisStatus::Fail;
        result.warnings.push_back(warning);
        return result;
    }
    // 状态映射后做两次修正：证据不足优先上报；必需任务点未全部可达则判为不可行。
    result.status = statusFor(candidate);
    const StructureRawMetrics& raw = candidate.raw;
    if (raw.taskEvaluationDataInsufficient || raw.workspaceCoverageDataInsufficient) {
        result.status = EngineeringEvaluationStatus::DataInsufficient;
    }
    if (result.status == EngineeringEvaluationStatus::Success &&
        raw.requiredReachableCount < raw.requiredTaskCount) {
        result.status = EngineeringEvaluationStatus::Infeasible;
    }
    addMetric(result, "kinematics.reachability.weighted", raw.weightedReachability,
              "ratio", id());
    addMetric(result, "kinematics.manipulability.p10", raw.manipulabilityP10,
              "ratio", id());
    addMetric(result, "kinematics.joint_margin.p10", raw.jointMarginP10,
              "ratio", id());
    addMetric(result, "kinematics.joint_margin.minimum", raw.minimumJointMargin,
              "ratio", id());
    if (hasWorkspaceRequirements && !raw.workspaceCoverageDataInsufficient) {
        addMetric(result, "kinematics.workspace.coverage", raw.workspaceCoverage,
                  "ratio", id());
    }
    addMetric(result, "kinematics.task.required.count", raw.requiredTaskCount,
              "count", id());
    addMetric(result, "kinematics.task.required.reachable_count",
              raw.requiredReachableCount, "count", id());
    addMetric(result, "kinematics.task.optional.count", raw.optionalTaskCount,
              "count", id());
    addMetric(result, "kinematics.task.optional.reachable_count",
              raw.optionalReachableCount, "count", id());
    addMetric(result, "collision.free_rate", raw.collisionFreeRate, "ratio", id());
    addMetric(result, "geometry.compactness", candidate.scores.compactness,
              "ratio", id());
    addMetric(result, "geometry.kinematic_length", raw.totalKinematicLength,
              "m", id());
    addMetric(result, "geometry.base_height", raw.baseHeight, "m", id());
    addMetric(result, "geometry.cross_section.maximum", raw.maxCrossSection,
              "m2", id());
    addMetric(result, "geometry.link_slenderness.maximum", raw.maxLinkSlenderness,
              "ratio", id());
    addMetric(result, "structure.preference", raw.engineeringPreference,
              "ratio", id());
    addMetric(result, "evaluation.model_build_seconds", raw.modelBuildSeconds,
              "s", id());
    addMetric(result, "evaluation.kinematic_seconds",
              raw.kinematicEvaluationSeconds, "s", id());
    addMetric(result, "evaluation.workspace_seconds",
              raw.workspaceEvaluationSeconds, "s", id());
    // 组装本评估器声明的数据工件；覆盖相关工件仅在存在覆盖要求且有可用数据时输出。
    result.artifacts.push_back({"kinematics.ik-solutions", "application/json", ikSummary(raw)});
    if (hasWorkspaceRequirements && !raw.workspaceCoverageDataInsufficient) {
        result.artifacts.push_back({"kinematics.workspace.coverage-summary",
                                    "application/json", workspaceSummary(raw)});
    }
    result.artifacts.push_back({"structure.raw-metrics", "application/json", "{}"});

    // 把候选违反的硬约束逐条转成工程级约束结果，供上层决策器识别阻断项。
    for (const std::string& violation : candidate.violatedConstraints) {
        EngineeringConstraintResult constraint;
        constraint.metricId = violation;
        constraint.hard = true;
        constraint.satisfied = false;
        constraint.failureReason = "Structure constraint was not satisfied.";
        result.constraints.push_back(constraint);
    }
    for (const std::string& message : candidate.warnings) {
        AnalysisWarning warning;
        warning.code = "KinematicEngineeringEvaluator.Warning";
        warning.message = message;
        warning.source = id();
        warning.severity = AnalysisStatus::Warning;
        result.warnings.push_back(warning);
    }
    return result;
}

// 兼容包装：结构优化器传统的无状态入口最终委托给 KinematicEngineeringEvaluator，
// 使新旧两条调用路径共享同一套运动学评价实现，避免行为分叉。
void StructureCandidateEvaluator::evaluate(
    const StructureOptimizationProblem& problem, StructureCandidateResult& candidate,
    StructureEvaluationStage stage, const StructureOptimizationCallbacks& callbacks,
    StructureCandidateCache* cache)
{
    KinematicEngineeringEvaluator evaluator(problem);
    evaluator.evaluateLegacy(candidate, stage, callbacks, cache);
}

} // namespace rws
