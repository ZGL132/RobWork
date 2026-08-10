#include "KinematicEngineeringEvaluator.hpp"
#include "StructureCandidateEvaluator.hpp"

#include <algorithm>
#include <exception>
#include <sstream>

namespace rws {

namespace {

/**
 * @brief 将底层结构优化候选解状态映射为系统工程公共评价状态。
 * 
 * 供上层工作流与 UI 统一消费呈现。
 * 
 * @param candidate 底层候选解结果对象
 * @return EngineeringEvaluationStatus 对应的公共工程评价状态枚举
 */
EngineeringEvaluationStatus statusFor(const StructureCandidateResult& candidate)
{
    switch (candidate.status) {
    case StructureCandidateStatus::Feasible:
        return EngineeringEvaluationStatus::Success;      // 可行 -> 成功
    case StructureCandidateStatus::Infeasible:
        return EngineeringEvaluationStatus::Infeasible;    // 不可行 -> 不可行
    case StructureCandidateStatus::Canceled:
        return EngineeringEvaluationStatus::Cancelled;     // 用户取消 -> 已取消
    case StructureCandidateStatus::Failed:
    case StructureCandidateStatus::Pending:
    default:
        return EngineeringEvaluationStatus::Failed;        // 异常/未处理 -> 失败
    }
}

/**
 * @brief 便捷写入函数：把 (metricId, value, unit, providerId) 打包为一条 EngineeringMetric 追加到结果对象中。
 * 
 * @param result [in, out] 目标工程评估结果结构体
 * @param id 指标唯一标识符 (如 "kinematics.reachability.weighted")
 * @param value 指标双精度浮点数值
 * @param unit 物理单位字符串 (如 "ratio", "m", "m2", "count", "s")
 * @param provider 评估器标识符
 */
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

/**
 * @brief 生成 IK 逆运动学求解汇总的紧凑 JSON 格式字符串。
 * 
 * 包含每个任务点的可用解数量 (usableSolutionCount) 和可达性 (reachable)，
 * 作为 "kinematics.ik-solutions" 工件提交给上层进行详细图表解析与展示。
 * 
 * @param raw 原始评估指标结构体
 * @return std::string JSON 格式字符串
 */
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

/**
 * @brief 生成工作空间覆盖率汇总 JSON 格式字符串。
 * 
 * 包含工作空间覆盖率 (coverage)、被占用的体素网格数 (occupiedCellCount) 及总网格数 (totalCellCount)。
 * 
 * @param raw 原始评估指标结构体
 * @return std::string JSON 格式字符串
 */
std::string workspaceSummary(const StructureRawMetrics& raw)
{
    std::ostringstream stream;
    stream << "{\"coverage\":" << raw.workspaceCoverage
           << ",\"occupiedCellCount\":" << raw.workspaceOccupiedCellCount
           << ",\"totalCellCount\":" << raw.workspaceTotalCellCount
           << "}";
    return stream.str();
}

} // 匿名命名空间

/**
 * @brief 构造函数：持有全局结构优化问题的只读引用。
 * 
 * 评价所需的全部阈值、采样密度及需求契约均来自 _problem 对象，
 * 使得评估器实例可被安全地无状态复用，并保证相同的输入必定产生可复现的评估结果。
 */
KinematicEngineeringEvaluator::KinematicEngineeringEvaluator(
    const StructureOptimizationProblem& problem) : _problem(problem)
{}

/**
 * @brief 获取当前评估器的唯一 ID 标识符。
 * 
 * @return std::string 若问题配置中未指定，则使用默认的 "structure.kinematics"
 */
std::string KinematicEngineeringEvaluator::id() const
{
    return _problem.evaluation.evaluatorId.empty()
        ? "structure.kinematics" : _problem.evaluation.evaluatorId;
}

/**
 * @brief 获取当前评估器的算法版本号。
 * 
 * 用于标识产生该评价结果的算法版本，便于评估结果追溯与版本对比。
 * 
 * @return std::string 若未指定则缺省返回 "1"
 */
std::string KinematicEngineeringEvaluator::version() const
{
    return _problem.evaluation.evaluatorVersion.empty()
        ? "1" : _problem.evaluation.evaluatorVersion;
}

/**
 * @brief 声明本评估器计算完成后能够向管线产出的工件 (Artifact) ID 列表。
 * 
 * @return std::vector<std::string> 包含 IK 解汇总、工作空间覆盖汇总及原始指标三项工件 ID
 */
std::vector<std::string> KinematicEngineeringEvaluator::providedArtifactIds() const
{
    return {"kinematics.ik-solutions", "kinematics.workspace.coverage-summary",
            "structure.raw-metrics"};
}

/**
 * @brief 系统工程管线公共评价入口：执行完整运动学评估，并将原始指标映射为工程级结果。
 * 
 * 语义与设计要点：
 *  1. 异常隔离：底层模型构建或 IK 求解时抛出的异常统一在此捕获并降级为 Failed，绝不向上抛出；
 *  2. 证据不足优先：DataInsufficient 状态优先于 Infeasible 上报；
 *  3. 必需任务门禁：即便综合得分未触发约束失败，只要必需任务点（Must Tasks）未全部可达，仍强判为 Infeasible。
 * 
 * @param context 当前候选解上下文（包含设计变量数值与模型快照）
 * @param request 评估请求参数（包含 Quick/Verified 精度阶段及输入工件）
 * @param callbacks 控制回调接口（包含取消检查）
 * @return EngineeringEvaluationResult 包含状态、物理指标、约束违背与 JSON 工件的终态报告
 */
EngineeringEvaluationResult KinematicEngineeringEvaluator::evaluate(
    const CandidateEvaluationContext& context, const EvaluationRequest& request,
    const EvaluationCallbacks& callbacks)
{
    // 1. 判断问题是否携带工作空间覆盖率校验要求：旧版的 coverageBox/coverageBoxes 或冻结需求的 Must 区域存在即算有
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

    // 2. 调用底层 evaluateLegacy 求解运动学指标。底层的模型构建失败或求解异常统一捕获降级
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

    // 3. 基础状态映射与两次状态强制修正
    result.status = statusFor(candidate);
    const StructureRawMetrics& raw = candidate.raw;

    // 修正一：若原始指标指示任务或工作空间计算数据不足，优先归类为 DataInsufficient
    if (raw.taskEvaluationDataInsufficient || raw.workspaceCoverageDataInsufficient) {
        result.status = EngineeringEvaluationStatus::DataInsufficient;
    }

    // 修正二：若当前状态为成功，但必需任务点的可达数量小于总要求数量，强制修正为不可行 (Infeasible)
    if (result.status == EngineeringEvaluationStatus::Success &&
        raw.requiredReachableCount < raw.requiredTaskCount) {
        result.status = EngineeringEvaluationStatus::Infeasible;
    }

    // 4. 将原始物理数值打包为标准的 EngineeringMetric 写入结果集
    addMetric(result, "kinematics.reachability.weighted", raw.weightedReachability,
              "ratio", id());
    addMetric(result, "kinematics.manipulability.p10", raw.manipulabilityP10,
              "ratio", id());
    addMetric(result, "kinematics.joint_margin.p10", raw.jointMarginP10,
              "ratio", id());
    addMetric(result, "kinematics.joint_margin.minimum", raw.minimumJointMargin,
              "ratio", id());

    // 仅在明确配置了工作空间覆盖要求且数据充足时输出工作空间覆盖率指标
    if (hasWorkspaceRequirements && !raw.workspaceCoverageDataInsufficient) {
        addMetric(result, "kinematics.workspace.coverage", raw.workspaceCoverage,
                  "ratio", id());
    }

    // 写入任务点统计指标（必需/可选任务总数与可达数）
    addMetric(result, "kinematics.task.required.count", raw.requiredTaskCount,
              "count", id());
    addMetric(result, "kinematics.task.required.reachable_count",
              raw.requiredReachableCount, "count", id());
    addMetric(result, "kinematics.task.optional.count", raw.optionalTaskCount,
              "count", id());
    addMetric(result, "kinematics.task.optional.reachable_count",
              raw.optionalReachableCount, "count", id());

    // 写入碰撞与几何尺寸指标
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

    // 写入各阶段评估耗时（模型构建耗时、运动学求解耗时、工作空间计算耗时）
    addMetric(result, "evaluation.model_build_seconds", raw.modelBuildSeconds,
              "s", id());
    addMetric(result, "evaluation.kinematic_seconds",
              raw.kinematicEvaluationSeconds, "s", id());
    addMetric(result, "evaluation.workspace_seconds",
              raw.workspaceEvaluationSeconds, "s", id());

    // 5. 组装并写入声明的数据工件 (Artifacts)
    result.artifacts.push_back({"kinematics.ik-solutions", "application/json", ikSummary(raw)});
    if (hasWorkspaceRequirements && !raw.workspaceCoverageDataInsufficient) {
        result.artifacts.push_back({"kinematics.workspace.coverage-summary",
                                    "application/json", workspaceSummary(raw)});
    }
    result.artifacts.push_back({"structure.raw-metrics", "application/json", "{}"});

    // 6. 将候选解违反的硬约束逐条映射为工程级的约束结果 (EngineeringConstraintResult)
    for (const std::string& violation : candidate.violatedConstraints) {
        EngineeringConstraintResult constraint;
        constraint.metricId = violation;
        constraint.hard = true;
        constraint.satisfied = false;
        constraint.failureReason = "Structure constraint was not satisfied.";
        result.constraints.push_back(constraint);
    }

    // 7. 转发告警消息
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

/**
 * @brief 兼容包装接口：供旧版 StructureCandidateEvaluator 调用。
 * 
 * 使新旧两条调用路径共享同一套 KinematicEngineeringEvaluator 运动学评估实现，避免行为分叉。
 */
void StructureCandidateEvaluator::evaluate(
    const StructureOptimizationProblem& problem, StructureCandidateResult& candidate,
    StructureEvaluationStage stage, const StructureOptimizationCallbacks& callbacks,
    StructureCandidateCache* cache)
{
    KinematicEngineeringEvaluator evaluator(problem);
    evaluator.evaluateLegacy(candidate, stage, callbacks, cache);
}

} // namespace rws