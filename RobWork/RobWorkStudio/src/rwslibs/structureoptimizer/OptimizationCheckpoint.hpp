#ifndef RWS_STRUCTUREOPTIMIZATION_OPTIMIZATIONCHECKPOINT_HPP
#define RWS_STRUCTUREOPTIMIZATION_OPTIMIZATIONCHECKPOINT_HPP

#include "CandidateResult.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace rws {

/** 运行恢复所需的输入内容指纹集合。时间戳等显示信息不参与匹配。 */
struct OptimizationCheckpointFingerprints
{
    std::string model;
    std::string environment;
    std::string requirements;
    std::string designSpace;

    bool valid() const
    {
        return !model.empty() && !environment.empty() && !requirements.empty() &&
               !designSpace.empty();
    }
};

/**
 * @brief 可序列化前的内存检查点契约。
 *
 * checkpoint 只保存调度进度和不可变候选结果，不保存 WorkCell、State、QObject
 * 或求解器指针。活动候选如果没有完整证据，会在 create 时降级为
 * DataInsufficient，避免恢复后把半个评估伪装成完整结论。
 */
struct OptimizationCheckpoint
{
    int schemaVersion = 1;
    OptimizationCheckpointFingerprints fingerprints;
    unsigned int randomSeed = 0;
    std::size_t nextCandidateIndex = 0;
    std::vector<std::size_t> pendingStableIndices;
    std::vector<CandidateResult> completedResults;
    std::vector<CandidateResult> partialResults;

    bool valid() const
    {
        return schemaVersion == 1 && fingerprints.valid();
    }

    static OptimizationCheckpoint create(
        const OptimizationCheckpointFingerprints& fingerprints,
        unsigned int randomSeed,
        std::size_t nextCandidateIndex,
        const std::vector<std::size_t>& pendingStableIndices,
        const std::vector<CandidateResult>& completedResults,
        const std::vector<CandidateResult>& activeResults);
};

struct OptimizationCheckpointRestoreResult
{
    bool ok = false;
    std::string diagnostic;
    OptimizationCheckpoint checkpoint;
};

OptimizationCheckpointRestoreResult restoreCheckpoint(
    const OptimizationCheckpoint& checkpoint,
    const OptimizationCheckpointFingerprints& currentFingerprints);

} // namespace rws

#endif
