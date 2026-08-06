// =============================================================================
//  KinematicBatchRunner.cpp —— 批量评估运行器实现
// =============================================================================
//
// 实现 validateRequirements 的批量流水线:
//   1. 把 RequirementExecutionTask 转换为 TaskPoint(映射进程类型与权重,
//      未纳入执行集的任务以 NotEvaluated 占位);
//   2. 逐任务调用 TargetEvaluator::evaluate,透传选项并叠加任务的碰撞要求;
//   3. 统计 Must 级任务的可行 / 数据不足情况,聚合成整体可行性;
//   4. 在批次边界与每任务完成时触发进度回调,并随时响应取消。
// 单个任务求解抛出的异常被捕获并降级为 DataInsufficient + SolverError,
// 保证批量循环不会因单点异常而整体崩溃。
#include "KinematicBatchRunner.hpp"

#include <algorithm>
#include <sstream>

namespace rws {
namespace {

// -----------------------------------------------------------------------------
// 内部辅助函数(匿名命名空间,仅本翻译单元可见)
// -----------------------------------------------------------------------------
//
// toTaskPointType:把需求进程类型映射为任务点类型。
// 多个进程类型可收敛到同一任务点类型(如 Pick 单独映射,Place 由三类
// 放置类进程共用),便于 UI 按任务点类型统一着色与统计。
TaskPointType toTaskPointType(RequirementExecutionProcessType processType)
{
    switch (processType) {
    case RequirementExecutionProcessType::Pick: return TaskPointType::Pick;
    case RequirementExecutionProcessType::Place:
    case RequirementExecutionProcessType::MachineLoad:
    case RequirementExecutionProcessType::MachineUnload: return TaskPointType::Place;
    case RequirementExecutionProcessType::Inspect: return TaskPointType::Inspect;
    case RequirementExecutionProcessType::WeldStart:
    case RequirementExecutionProcessType::WeldEnd: return TaskPointType::Weld;
    default: return TaskPointType::Generic;
    }
}

// toTaskPoint:把需求任务转换为评估器使用的 TaskPoint。
// 关键映射:Must 级任务权重 1.0,其余 0.5;仅编译状态为 Included 的任务
// 标记为 enabled,供上层 UI 区分"被排除"与"待评估"的任务。
TaskPoint toTaskPoint(const RequirementExecutionTask& task)
{
    TaskPoint point;
    point.id = task.id;
    point.name = task.name;
    point.type = toTaskPointType (task.processType);
    point.refFrame = task.refFrame;
    point.tcpFrame = task.tcpFrame;
    point.position = task.position;
    point.rpyDeg = task.rpyDeg;
    point.tolerance.positionMeters = task.positionToleranceMeters;
    point.tolerance.orientationDeg = task.orientationToleranceDeg;
    point.tolerance.allowToolRollFree = task.allowToolRollFree;
    point.weight = task.level == RequirementExecutionLevel::Must ? 1.0 : 0.5;
    point.enabled = task.compileState == RequirementExecutionCompileState::Included;
    return point;
}

// qualityRank:把质量等级映射为可比较的数值,用于聚合"最差质量"。
// 约定 Critical > Degraded > Good > Unknown;数值越大表示问题越严重。
int qualityRank(Quality quality)
{
    switch (quality) {
    case Quality::Critical: return 3;
    case Quality::Degraded: return 2;
    case Quality::Good: return 1;
    case Quality::Unknown: return 0;
    }
    return 0;
}

// appendWarning:构造来源为 "KinematicBatchRunner" 的告警并加入任务结果。
void appendWarning(TargetEvaluation& result,
                   const char* code,
                   const std::string& message)
{
    AnalysisWarning warning;
    warning.code = code;
    warning.message = message;
    warning.source = "KinematicBatchRunner";
    warning.severity = AnalysisStatus::Warning;
    result.warnings.push_back (warning);
}

} // namespace

// 序列化缓存键为单行可读文本。用 "key=value" 连接且以 '|' 分隔,
// 使键名可读且能避免字段之间的歧义(例如 seed 一定带 "seed=" 前缀)。
std::string KinematicBatchCacheKey::toString () const
{
    std::ostringstream stream;
    stream << "model=" << modelFingerprint
           << "|environment=" << environmentFingerprint
           << "|requirements=" << requirementFingerprint
           << "|stage=" << rws::toString (analysisStage)
           << "|config=" << configHash
           << "|seed=" << seed;
    return stream.str ();
}

// 缓存键相等 = 全部六项字段逐一相等;任一字段不同即判定为不同批次。
bool KinematicBatchCacheKey::operator== (const KinematicBatchCacheKey& other) const
{
    return modelFingerprint == other.modelFingerprint &&
           environmentFingerprint == other.environmentFingerprint &&
           requirementFingerprint == other.requirementFingerprint &&
           analysisStage == other.analysisStage && configHash == other.configHash &&
           seed == other.seed;
}

// 构造缓存键:把各项指纹 / 等级 / 配置哈希 / 种子打包进结构体。
KinematicBatchCacheKey makeKinematicBatchCacheKey (
    const std::string& modelFingerprint,
    const std::string& environmentFingerprint,
    const std::string& requirementFingerprint,
    AnalysisEvidenceStage analysisStage,
    const std::string& configHash,
    unsigned int seed)
{
    KinematicBatchCacheKey key;
    key.modelFingerprint = modelFingerprint;
    key.environmentFingerprint = environmentFingerprint;
    key.requirementFingerprint = requirementFingerprint;
    key.analysisStage = analysisStage;
    key.configHash = configHash;
    key.seed = seed;
    return key;
}

// -----------------------------------------------------------------------------
// KinematicBatchRunner::validateRequirements —— 批量需求验证
// -----------------------------------------------------------------------------
//
// 逐任务评估并聚合。整体可行性只由 Must 级任务决定,理由:Should / Info
// 任务属于"建议 / 参考"性质,不应因它们不可达而把整个需求集判为失败。
RequirementValidationSummary KinematicBatchRunner::validateRequirements(
    const AnalysisContext& context,
    const RequirementExecutionSet& requirements,
    const BatchRunOptions& options,
    const CancellationToken& cancellation) const
{
    RequirementValidationSummary summary;
    summary.stage = options.evidenceStage;
    summary.provenance = requirements.provenance;
    summary.taskResults.reserve (requirements.tasks.size ());

    bool hasInfeasibleMust = false;
    bool hasDataInsufficientMust = false;
    Quality mustQuality = Quality::Unknown;
    TargetEvaluator evaluator;

    // ---- 逐任务评估主循环 --------------------------------------------------
    // 每处理 maxBatchSize 个任务在批次起点报告一次中间进度,
    // 使长批量在 UI 上表现为平滑推进而非长时间无反馈。
    const int batchSize = options.maxBatchSize > 0 ? options.maxBatchSize : 128;
    int completed = 0;
    for (std::size_t taskIndex = 0; taskIndex < requirements.tasks.size (); ++taskIndex) {
        if (taskIndex % static_cast< std::size_t > (batchSize) == 0 &&
            options.progressCallback != nullptr) {
            options.progressCallback (completed, static_cast< int > (requirements.tasks.size ()),
                                      options.progressUserData);
        }
        if (cancellation.cancellationRequested ()) {
            summary.feasibility = Feasibility::DataInsufficient;
            summary.quality = Quality::Critical;
            AnalysisWarning warning;
            warning.code = "KIN_BATCH_CANCELLED";
            warning.message = "Requirement validation was cancelled between batches.";
            warning.source = "KinematicBatchRunner";
            warning.severity = AnalysisStatus::Warning;
            summary.warnings.push_back (warning);
            return summary;
        }
        const RequirementExecutionTask& task = requirements.tasks[taskIndex];
        const TaskPoint target = toTaskPoint (task);
        TargetEvaluation result;
        // 未纳入执行集的任务不求解,直接以 NotEvaluated 占位并附说明告警;
        // 这保证 taskResults 与输入任务严格一一对应,便于 UI 对齐显示。
        // 已纳入的任务则叠加其碰撞要求后交给 TargetEvaluator 求解。
        if (task.compileState != RequirementExecutionCompileState::Included) {
            result.stage = options.evidenceStage;
            result.feasibility = Feasibility::NotEvaluated;
            result.quality = Quality::Unknown;
            result.target = target;
            appendWarning (result, "KIN_TASK_EXCLUDED",
                           "Requirement task is not included in this execution set.");
        }
        else {
            TargetEvaluationOptions targetOptions = options.targetOptions;
            targetOptions.evidenceStage = options.evidenceStage;
            targetOptions.requireCollisionFree = task.collisionFreeRequired;
            targetOptions.checkCollision = targetOptions.checkCollision || task.collisionFreeRequired;
            try {
                result = evaluator.evaluate (context, target, targetOptions);
            }
            catch (const std::exception& exception) {
                result.stage = options.evidenceStage;
                result.feasibility = Feasibility::DataInsufficient;
                result.quality = Quality::Critical;
                result.target = target;
                result.failureReasons.push_back (KinematicFailureReason::SolverError);
                appendWarning (result, "KIN_TASK_SOLVER_ERROR",
                               "Task " + task.id + " failed: " + exception.what ());
            }
        }
        // 把执行契约等级(task.level)回填到结果,与求得的评估结果一起保存:
        // 交互式直接评估时 TargetEvaluation 默认 Must,这里显式覆盖为任务真实等级。
        // Preserve the execution contract level alongside the evaluated result.
        // Direct interactive evaluations retain TargetEvaluation's default Must.
        result.level = task.level;
        result.provenance = requirements.provenance;
        result.itemProvenance = task.provenance;
        summary.taskResults.push_back (result);

        ++completed;
        if (options.progressCallback != nullptr)
            options.progressCallback (completed, static_cast< int > (requirements.tasks.size ()),
                                     options.progressUserData);

        // 仅统计"已纳入且 Must 级"的任务:Should / Info 任务不参与整体可行性,
        // 只作为明细保留在 taskResults 中供 UI 展示。
        if (task.compileState != RequirementExecutionCompileState::Included ||
            task.level != RequirementExecutionLevel::Must)
            continue;

        ++summary.mustTaskCount;
        if (result.feasibility == Feasibility::Feasible)
            ++summary.mustTaskFeasibleCount;
        else if (result.feasibility == Feasibility::DataInsufficient)
            hasDataInsufficientMust = true;
        else
            hasInfeasibleMust = true;
        if (qualityRank (result.quality) > qualityRank (mustQuality))
            mustQuality = result.quality;

    }

    // ---- 整体可行性聚合 ----------------------------------------------------
    // 优先级:存在 DataInsufficient 的 Must > 存在 Infeasible 的 Must >
    // 全部 Must 可行(此时质量为所有 Must 任务中的最差等级)。
    if (summary.mustTaskCount == 0) {
        summary.feasibility = Feasibility::NotEvaluated;
        summary.quality = Quality::Unknown;
    }
    else if (hasDataInsufficientMust) {
        summary.feasibility = Feasibility::DataInsufficient;
        summary.quality = Quality::Critical;
    }
    else if (hasInfeasibleMust) {
        summary.feasibility = Feasibility::Infeasible;
        summary.quality = Quality::Critical;
    }
    else {
        summary.feasibility = Feasibility::Feasible;
        summary.quality = mustQuality;
    }
    return summary;
}

} // namespace rws
