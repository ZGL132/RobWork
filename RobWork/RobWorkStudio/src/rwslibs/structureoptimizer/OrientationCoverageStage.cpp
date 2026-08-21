#include "OrientationCoverageStage.hpp"

#include <rwslibs/kinematicanalysis/KinematicAnalysisPoseReachability.hpp>
#include <rwslibs/kinematicanalysis/KinematicAnalyzer.hpp>

namespace rws {
namespace {
bool canceled(void* data)
{
    return data != nullptr && static_cast<CancellationToken*>(data)->cancellationRequested();
}
}

OrientationCoverageResult OrientationCoverageStage::evaluate(
    const AnalysisContext& context,
    const std::vector<std::array<double, 3>>& positions,
    const PoseReachabilityConfig& config,
    const CancellationToken& cancellation) const
{
    OrientationCoverageResult result;
    result.stage.stageId = "orientation_coverage";
    result.stage.version = "1";
    result.stage.requestedCount = positions.size();
    if (cancellation.cancellationRequested()) {
        result.stage.status = EvaluationStageStatus::Canceled;
        return result;
    }
    if (positions.empty()) {
        result.stage.status = EvaluationStageStatus::NotEvaluated;
        return result;
    }

    PoseReachabilityRunCallbacks callbacks;
    callbacks.isCancellationRequested = &canceled;
    callbacks.userData = const_cast<CancellationToken*>(&cancellation);
    KinematicAnalyzer analyzer;
    result.samples = analyzer.analyzePoseReachability(
        context.device, context.tcpFrame, context.baseState, positions, config,
        config.checkCollision ? context.collisionDetector : nullptr, callbacks);
    result.stage.completedCount = result.samples.size();
    if (cancellation.cancellationRequested())
        result.stage.status = EvaluationStageStatus::Canceled;
    else if (context.device == nullptr || context.tcpFrame == nullptr)
        result.stage.status = EvaluationStageStatus::DataInsufficient;
    else
        result.stage.status = result.samples.size() < positions.size()
                                  ? EvaluationStageStatus::DataInsufficient
                                  : EvaluationStageStatus::Passed;
    return result;
}

} // namespace rws
