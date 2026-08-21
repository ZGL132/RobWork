#include "IndependentFinalVerifier.hpp"

#include <unordered_set>

namespace rws {
namespace {

bool canceled(const IndependentFinalCallbacks& callbacks)
{
    return callbacks.isCancellationRequested && callbacks.isCancellationRequested();
}

bool validFinalSample(const CandidateResult& result)
{
    return result.feasibility == Feasibility::Feasible &&
           result.evidenceStage == AnalysisEvidenceStage::Verified;
}

} // namespace

const char* toString(FinalValidationStatus status)
{
    switch (status) {
    case FinalValidationStatus::Incomplete: return "Incomplete";
    case FinalValidationStatus::Complete: return "Complete";
    case FinalValidationStatus::Failed: return "Failed";
    }
    return "Incomplete";
}

IndependentFinalResult IndependentFinalVerifier::verify(
    const FinalValidationPlan& plan,
    const CandidateResult& searchResult,
    const std::vector<std::string>& priorEvidenceKeys,
    const IndependentFinalEvaluationCallback& evaluate,
    const IndependentFinalCallbacks& callbacks)
{
    IndependentFinalResult output;
    output.plan = plan;
    output.searchResult = searchResult;
    output.finalResult = searchResult;
    output.requestedCount = plan.verificationSeeds.size();

    if (!plan.valid()) {
        output.diagnostic = "Final validation plan is invalid.";
        output.finalResult.feasibility = Feasibility::DataInsufficient;
        return output;
    }
    if (!evaluate) {
        output.diagnostic = "Final validation evaluator is not provided.";
        output.finalResult.feasibility = Feasibility::DataInsufficient;
        return output;
    }

    std::unordered_set<std::string> seenEvidence(priorEvidenceKeys.begin(),
                                                 priorEvidenceKeys.end());
    bool anyFailure = false;
    for (const std::string& seed : plan.verificationSeeds) {
        if (canceled(callbacks)) {
            output.canceled = true;
            output.diagnostic = "Final validation canceled between verification seeds.";
            break;
        }

        FinalEvaluationSample sample = evaluate(seed, AnalysisEvidenceStage::Verified);
        ++output.completedCount;
        if (sample.evidenceKey.empty()) {
            anyFailure = true;
            output.diagnostic = "Final validation sample has an empty evidence key.";
        }
        else if (!seenEvidence.insert(sample.evidenceKey).second) {
            ++output.duplicateEvidenceCount;
        }
        else {
            ++output.newEvidenceCount;
        }
        if (!validFinalSample(sample.result))
            anyFailure = true;
        output.samples.push_back(std::move(sample));
    }

    if (anyFailure) {
        output.status = FinalValidationStatus::Failed;
        // 失败样本优先对外呈现，明确阻止 Feasible 假设继续传播。
        for (const FinalEvaluationSample& sample : output.samples) {
            if (!validFinalSample(sample.result)) {
                output.finalResult = sample.result;
                break;
            }
        }
        if (output.finalResult.feasibility == Feasibility::Feasible)
            output.finalResult.feasibility = Feasibility::Infeasible;
        return output;
    }

    if (output.canceled || output.completedCount != output.requestedCount ||
        output.duplicateEvidenceCount != 0) {
        output.status = FinalValidationStatus::Incomplete;
        output.finalResult.feasibility = Feasibility::DataInsufficient;
        output.finalResult.evidenceStage = AnalysisEvidenceStage::Verified;
        return output;
    }

    output.status = FinalValidationStatus::Complete;
    output.finalResult = output.samples.back().result;
    output.finalResult.evidenceStage = AnalysisEvidenceStage::Verified;
    return output;
}

} // namespace rws
