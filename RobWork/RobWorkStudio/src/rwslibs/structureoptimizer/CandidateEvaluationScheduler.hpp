#ifndef RWS_STRUCTUREOPTIMIZATION_CANDIDATEEVALUATIONSCHEDULER_HPP
#define RWS_STRUCTUREOPTIMIZATION_CANDIDATEEVALUATIONSCHEDULER_HPP

#include "CandidateResult.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace rws {

/**
 * @brief 一个候选级评估任务。
 *
 * 调度器不读取 WorkCell、State 或 Qt 对象。调用方必须在闭包中捕获候选所需的
 * 不可变输入，并根据 workerIndex 选择独立的 State/碰撞上下文；这样并行边界
 * 永远位于候选之间，不会把同一个可变运动学状态暴露给多个 worker。
 */
struct CandidateEvaluationTask
{
    std::size_t stableIndex = 0;
    std::string candidateId;
    std::function<CandidateResult(std::size_t workerIndex)> evaluate;
};

struct CandidateEvaluationSchedulerConfig
{
    /** 并行 worker 数；1 表示完全串行的可复现基线。 */
    std::size_t parallelism = 1;
};

struct CandidateEvaluationSchedulerCallbacks
{
    /**
     * @brief 协作式取消查询。
     *
     * 调度器只在 worker 领取新任务前查询该回调，不替调用方中断已经运行的
     * evaluator；因此 evaluator 自身仍可通过它的既有取消协议处理中途退出。
     */
    std::function<bool()> isCancellationRequested;
};

/** 单个候选的稳定索引结果槽。 */
struct CandidateEvaluationItem
{
    std::size_t stableIndex = 0;
    std::string candidateId;
    std::size_t workerIndex = 0;
    CandidateResult result;
    bool exceptionCaught = false;
    std::string diagnostic;
};

/**
 * @brief 候选级并行评估的聚合结果。
 *
 * results 只包含已经启动并完成（包括异常转为失败）的任务，并始终按
 * stableIndex 升序排列；调用方无需依赖线程完成顺序即可得到确定性结果。
 */
struct CandidateEvaluationBatchResult
{
    std::vector<CandidateEvaluationItem> results;
    std::size_t requestedCount = 0;
    std::size_t startedCount = 0;
    std::size_t completedCount = 0;
    std::size_t failedCount = 0;
    bool canceled = false;
    std::string diagnostic;
};

/**
 * @brief 只负责候选级调度、异常隔离、取消和稳定合并的纯核心调度器。
 *
 * 该类不创建或共享 WorkCell/State/CollisionDetector，也不发射 Qt signal。
 * UI 进度必须由调用方在 run 返回后，或通过上层已有的 queued signal 机制，
 * 在拥有 QObject 的线程中更新。
 */
class CandidateEvaluationScheduler
{
  public:
    static CandidateEvaluationBatchResult run(
        const std::vector<CandidateEvaluationTask>& tasks,
        const CandidateEvaluationSchedulerConfig& config = {},
        const CandidateEvaluationSchedulerCallbacks& callbacks = {});
};

} // namespace rws

#endif
