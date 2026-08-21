#include "VerifiedRegionStage.hpp"

#include <rwslibs/kinematicanalysis/RegionCoverageEvaluator.hpp>

namespace rws {

VerifiedRegionResult VerifiedRegionStage::evaluate(
    const AnalysisContext& context,
    const EvaluationPlan& plan,
    const CancellationToken& cancellation) const
{
    VerifiedRegionResult result;
    result.stage.stageId = "verified_region";
    result.stage.version = "1";
    result.stage.requestedCount = plan.regions.size();
    if (!plan.valid()) {
        result.stage.status = EvaluationStageStatus::DataInsufficient;
        result.stage.diagnostics.push_back(
            {"PLAN_INVALID", "plan", "Verified region evaluation requires a valid plan.", true});
        return result;
    }
    if (plan.regions.empty()) {
        result.stage.status = EvaluationStageStatus::NotEvaluated;
        return result;
    }
    RegionCoverageEvaluator evaluator;
    bool dataInsufficient = false;
    bool infeasible = false;
    for (const EvaluationPlanRegion& region : plan.regions) {
        if (cancellation.cancellationRequested()) {
            result.stage.status = EvaluationStageStatus::Canceled;
            return result;
        }
        RegionCoverageResult evaluated =
            evaluator.evaluate(context, region.source, cancellation);
        dataInsufficient = dataInsufficient ||
                           evaluated.feasibility == Feasibility::DataInsufficient;
        infeasible = infeasible || evaluated.feasibility == Feasibility::Infeasible;
        result.regions.push_back(std::move(evaluated));
        ++result.stage.completedCount;
        if (cancellation.cancellationRequested()) {
            result.stage.status = EvaluationStageStatus::Canceled;
            return result;
        }
    }
    result.stage.status = dataInsufficient ? EvaluationStageStatus::DataInsufficient
                                           : infeasible ? EvaluationStageStatus::Failed
                                                         : EvaluationStageStatus::Passed;
    return result;
}

} // namespace rws
