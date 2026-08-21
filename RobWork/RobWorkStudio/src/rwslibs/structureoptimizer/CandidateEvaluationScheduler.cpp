#include "CandidateEvaluationScheduler.hpp"

#include <algorithm>
#include <atomic>
#include <exception>
#include <mutex>
#include <thread>
#include <unordered_set>

namespace rws {
namespace {

bool cancellationRequested(const CandidateEvaluationSchedulerCallbacks& callbacks)
{
    return callbacks.isCancellationRequested && callbacks.isCancellationRequested();
}

CandidateResult failedResult(const CandidateEvaluationTask& task,
                             const std::string& diagnostic)
{
    CandidateResult result;
    result.candidateId = task.candidateId;
    result.lifecycle = CandidateLifecycle::Failed;
    result.feasibility = Feasibility::DataInsufficient;
    result.evidenceStage = AnalysisEvidenceStage::Quick;
    result.evaluationDiagnostic = diagnostic;
    return result;
}

} // namespace

CandidateEvaluationBatchResult CandidateEvaluationScheduler::run(
    const std::vector<CandidateEvaluationTask>& tasks,
    const CandidateEvaluationSchedulerConfig& config,
    const CandidateEvaluationSchedulerCallbacks& callbacks)
{
    CandidateEvaluationBatchResult output;
    output.requestedCount = tasks.size();

    if (config.parallelism == 0) {
        output.diagnostic = "Candidate evaluation parallelism must be greater than zero.";
        return output;
    }

    std::unordered_set<std::size_t> stableIndices;
    for (const CandidateEvaluationTask& task : tasks) {
        if (!stableIndices.insert(task.stableIndex).second) {
            output.diagnostic = "Candidate evaluation stable indices must be unique.";
            return output;
        }
    }

    if (tasks.empty())
        return output;

    // 每个任务只写自己的槽位，主线程 join 后再读取；无需让 worker 争用结果锁。
    std::vector<CandidateEvaluationItem> slots(tasks.size());
    std::vector<std::atomic_bool> completed(tasks.size());
    for (std::atomic_bool& flag : completed)
        flag.store(false);

    std::atomic_size_t nextTask{0};
    std::atomic_size_t startedCount{0};
    std::atomic_size_t completedCount{0};
    std::atomic_size_t failedCount{0};
    const std::size_t workerCount = std::min(config.parallelism, tasks.size());
    std::vector<std::thread> workers;
    workers.reserve(workerCount);

    for (std::size_t workerIndex = 0; workerIndex < workerCount; ++workerIndex) {
        workers.emplace_back([&, workerIndex]() {
            while (true) {
                // 取消只阻止领取新任务，不强行终止当前 evaluator，保证资源可析构。
                if (cancellationRequested(callbacks))
                    break;

                const std::size_t taskIndex = nextTask.fetch_add(1);
                if (taskIndex >= tasks.size())
                    break;
                if (cancellationRequested(callbacks))
                    break;

                const CandidateEvaluationTask& task = tasks[taskIndex];
                CandidateEvaluationItem item;
                item.stableIndex = task.stableIndex;
                item.candidateId = task.candidateId;
                item.workerIndex = workerIndex;
                startedCount.fetch_add(1);

                try {
                    if (!task.evaluate) {
                        item.diagnostic = "Candidate evaluation callback is not provided.";
                        item.result = failedResult(task, item.diagnostic);
                        ++failedCount;
                    }
                    else {
                        item.result = task.evaluate(workerIndex);
                        if (item.result.lifecycle == CandidateLifecycle::Failed) {
                            item.diagnostic = item.result.evaluationDiagnostic;
                            ++failedCount;
                        }
                    }
                }
                catch (const std::exception& error) {
                    item.exceptionCaught = true;
                    item.diagnostic = error.what();
                    item.result = failedResult(task, item.diagnostic);
                    ++failedCount;
                }
                catch (...) {
                    item.exceptionCaught = true;
                    item.diagnostic = "Unknown exception from candidate evaluator.";
                    item.result = failedResult(task, item.diagnostic);
                    ++failedCount;
                }

                slots[taskIndex] = std::move(item);
                completed[taskIndex].store(true, std::memory_order_release);
                completedCount.fetch_add(1);
            }
        });
    }

    for (std::thread& worker : workers)
        worker.join();

    output.startedCount = startedCount.load();
    output.completedCount = completedCount.load();
    output.failedCount = failedCount.load();
    // 只有确实有任务未启动时才把取消请求反映到批次状态；所有任务已经完成
    // 后才到达的迟到取消，不应把一个完整、可审计的批次改写成部分结果。
    output.canceled = output.startedCount < output.requestedCount;
    if (output.canceled)
        output.diagnostic = "Candidate evaluation canceled before all tasks started.";

    output.results.reserve(output.completedCount);
    for (std::size_t index = 0; index < slots.size(); ++index) {
        if (completed[index].load(std::memory_order_acquire))
            output.results.push_back(std::move(slots[index]));
    }
    std::sort(output.results.begin(), output.results.end(),
              [](const CandidateEvaluationItem& left, const CandidateEvaluationItem& right) {
                  return left.stableIndex < right.stableIndex;
              });
    return output;
}

} // namespace rws
