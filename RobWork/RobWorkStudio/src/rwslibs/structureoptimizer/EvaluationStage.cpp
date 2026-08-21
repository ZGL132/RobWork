#include "EvaluationStage.hpp"

#include <algorithm>

namespace rws {

void EvaluationPipeline::addStage(std::shared_ptr<const EvaluationStage> stage)
{
    if (!stage) return;
    _stages.push_back(std::move(stage));
}

std::vector<EvaluationStageResult> EvaluationPipeline::run(const EvaluationPlan& plan,
                                                           const std::string& candidateFingerprint,
                                                           std::atomic_bool* cancel) const
{
    std::vector<EvaluationStageResult> results;
    if (!plan.valid()) {
        EvaluationStageResult result;
        result.stageId = "pipeline";
        result.status = EvaluationStageStatus::Failed;
        result.diagnostics = plan.diagnostics;
        results.push_back(std::move(result));
        return results;
    }
    const EvaluationStageContext context{plan, candidateFingerprint};
    for (const auto& stage : _stages) {
        EvaluationStageResult result;
        result.stageId = stage->id();
        result.version = stage->version();
        if (cancel && cancel->load()) {
            result.status = EvaluationStageStatus::Canceled;
            results.push_back(std::move(result));
            break;
        }
        bool missing = false;
        for (const auto& capability : stage->requiredCapabilities()) {
            if (plan.capabilities.count(capability) == 0) {
                result.diagnostics.push_back({"CAPABILITY_MISSING", capability,
                                              "Stage capability is not available.", true});
                missing = true;
            }
        }
        if (missing) {
            result.status = EvaluationStageStatus::DataInsufficient;
            results.push_back(std::move(result));
            continue;
        }
        result = stage->run(context, cancel);
        if (result.stageId.empty()) result.stageId = stage->id();
        if (result.version.empty()) result.version = stage->version();
        results.push_back(std::move(result));
    }
    return results;
}

const char* toString(EvaluationStageStatus status)
{
    switch (status) {
    case EvaluationStageStatus::NotEvaluated: return "NotEvaluated";
    case EvaluationStageStatus::Passed: return "Passed";
    case EvaluationStageStatus::Failed: return "Failed";
    case EvaluationStageStatus::DataInsufficient: return "DataInsufficient";
    case EvaluationStageStatus::Canceled: return "Canceled";
    }
    return "NotEvaluated";
}

} // namespace rws
