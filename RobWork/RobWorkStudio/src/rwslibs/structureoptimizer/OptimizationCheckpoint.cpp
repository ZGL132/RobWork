#include "OptimizationCheckpoint.hpp"

namespace rws {
namespace {

bool sameFingerprints(const OptimizationCheckpointFingerprints& left,
                      const OptimizationCheckpointFingerprints& right)
{
    return left.model == right.model && left.environment == right.environment &&
           left.requirements == right.requirements && left.designSpace == right.designSpace;
}

} // namespace

OptimizationCheckpoint OptimizationCheckpoint::create(
    const OptimizationCheckpointFingerprints& fingerprints,
    unsigned int randomSeed,
    std::size_t nextCandidateIndex,
    const std::vector<std::size_t>& pendingStableIndices,
    const std::vector<CandidateResult>& completedResults,
    const std::vector<CandidateResult>& activeResults)
{
    OptimizationCheckpoint checkpoint;
    checkpoint.fingerprints = fingerprints;
    checkpoint.randomSeed = randomSeed;
    checkpoint.nextCandidateIndex = nextCandidateIndex;
    checkpoint.pendingStableIndices = pendingStableIndices;
    checkpoint.completedResults = completedResults;

    for (const CandidateResult& active : activeResults) {
        CandidateResult partial = active;
        // 活动候选无论当前 feasibility 字段为何，都必须明确降级为数据不足。
        partial.feasibility = Feasibility::DataInsufficient;
        partial.lifecycle = CandidateLifecycle::Canceled;
        partial.completion.canceled = true;
        if (partial.completion.partialReason.empty())
            partial.completion.partialReason = "Checkpoint captured an active candidate.";
        checkpoint.partialResults.push_back(std::move(partial));
    }
    return checkpoint;
}

OptimizationCheckpointRestoreResult restoreCheckpoint(
    const OptimizationCheckpoint& checkpoint,
    const OptimizationCheckpointFingerprints& currentFingerprints)
{
    OptimizationCheckpointRestoreResult result;
    if (!checkpoint.valid()) {
        result.diagnostic = "CHECKPOINT_INVALID";
        return result;
    }
    if (!currentFingerprints.valid()) {
        result.diagnostic = "CHECKPOINT_CURRENT_FINGERPRINTS_INVALID";
        return result;
    }
    if (!sameFingerprints(checkpoint.fingerprints, currentFingerprints)) {
        result.diagnostic = "CHECKPOINT_FINGERPRINT_MISMATCH";
        return result;
    }
    result.ok = true;
    result.checkpoint = checkpoint;
    return result;
}

} // namespace rws
