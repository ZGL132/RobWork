#include "EstimatedWorkspaceStage.hpp"

#include <rwslibs/kinematicanalysis/KinematicAnalyzer.hpp>

namespace rws {
namespace {
bool canceled(void* data)
{
    return data != nullptr && static_cast<CancellationToken*>(data)->cancellationRequested();
}
}

EstimatedWorkspaceResult EstimatedWorkspaceStage::evaluate(
    const AnalysisContext& context,
    const WorkspaceSamplingConfig& config,
    const CancellationToken& cancellation) const
{
    EstimatedWorkspaceResult result;
    result.stage.stageId = "estimated_workspace";
    result.stage.version = "1";
    result.stage.requestedCount = config.sampleCount > 0
                                      ? static_cast<std::size_t>(config.sampleCount)
                                      : 0;
    if (cancellation.cancellationRequested()) {
        result.stage.status = EvaluationStageStatus::Canceled;
        return result;
    }
    if (config.sampleCount <= 0) {
        result.stage.status = EvaluationStageStatus::NotEvaluated;
        return result;
    }

    WorkspaceSamplingRunCallbacks callbacks;
    callbacks.isCancellationRequested = &canceled;
    callbacks.userData = const_cast<CancellationToken*>(&cancellation);
    KinematicAnalyzer analyzer;
    result.samples = analyzer.sampleWorkspace(
        context.device, context.tcpFrame, context.baseState, config,
        config.checkCollision ? context.collisionDetector : nullptr, callbacks);
    result.stage.completedCount = result.samples.size();
    for (const WorkspaceSample& sample : result.samples)
        result.collisionChecked = result.collisionChecked || sample.collisionChecked;
    if (cancellation.cancellationRequested())
        result.stage.status = EvaluationStageStatus::Canceled;
    else if (context.device == nullptr || context.tcpFrame == nullptr)
        result.stage.status = EvaluationStageStatus::DataInsufficient;
    else if (result.samples.empty())
        result.stage.status = EvaluationStageStatus::DataInsufficient;
    else
        result.stage.status = EvaluationStageStatus::Passed;
    return result;
}

} // namespace rws
