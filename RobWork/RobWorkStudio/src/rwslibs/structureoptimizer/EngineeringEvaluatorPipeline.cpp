#include "EngineeringEvaluatorPipeline.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace rws {

namespace {

bool hasArtifact(const std::vector<EngineeringArtifact>& artifacts,
                 const std::string& artifactId)
{
    return std::any_of(artifacts.begin(), artifacts.end(),
                       [&artifactId](const EngineeringArtifact& artifact) {
                           return artifact.artifactId == artifactId;
                       });
}

EngineeringEvaluationResult insufficientResult(
    const CandidateEvaluationContext& candidate, const std::string& artifactId)
{
    EngineeringEvaluationResult result;
    result.status = EngineeringEvaluationStatus::DataInsufficient;
    result.inputSnapshot = candidate.inputSnapshot;
    AnalysisWarning warning;
    warning.code = "EngineeringEvaluatorPipeline.MissingArtifact";
    warning.message = "Required artifact is unavailable: " + artifactId;
    warning.source = "EngineeringEvaluatorPipeline";
    warning.severity = AnalysisStatus::Fail;
    result.warnings.push_back(warning);
    return result;
}

} // namespace

void EngineeringEvaluatorPipeline::addEvaluator(IEngineeringEvaluator& evaluator)
{
    _evaluators.push_back(&evaluator);
}

EngineeringEvaluationResult EngineeringEvaluatorPipeline::evaluate(
    const CandidateEvaluationContext& candidate, const EvaluationRequest& request,
    const EvaluationCallbacks& callbacks) const
{
    EngineeringEvaluationResult aggregate;
    aggregate.status = EngineeringEvaluationStatus::Success;
    aggregate.inputSnapshot = candidate.inputSnapshot;
    std::vector<EngineeringArtifact> artifacts = request.inputArtifacts;
    std::vector<IEngineeringEvaluator*> pending = _evaluators;

    while (!pending.empty()) {
        bool progressed = false;
        for (std::vector<IEngineeringEvaluator*>::iterator it = pending.begin();
             it != pending.end();) {
            IEngineeringEvaluator* evaluator = *it;
            bool ready = true;
            for (const std::string& artifactId : evaluator->requiredArtifactIds()) {
                if (!hasArtifact(artifacts, artifactId)) {
                    ready = false;
                    break;
                }
            }
            if (!ready) {
                ++it;
                continue;
            }

            EvaluationRequest evaluatorRequest = request;
            evaluatorRequest.inputArtifacts = artifacts;
            EngineeringEvaluationResult result =
                evaluator->evaluate(candidate, evaluatorRequest, callbacks);
            aggregate.metrics.insert(aggregate.metrics.end(), result.metrics.begin(),
                                     result.metrics.end());
            aggregate.constraints.insert(aggregate.constraints.end(), result.constraints.begin(),
                                         result.constraints.end());
            aggregate.warnings.insert(aggregate.warnings.end(), result.warnings.begin(),
                                      result.warnings.end());
            aggregate.artifacts.insert(aggregate.artifacts.end(), result.artifacts.begin(),
                                       result.artifacts.end());
            artifacts.insert(artifacts.end(), result.artifacts.begin(), result.artifacts.end());
            if (result.status != EngineeringEvaluationStatus::Success) {
                aggregate.status = result.status;
                return aggregate;
            }
            it = pending.erase(it);
            progressed = true;
        }

        if (!progressed) {
            for (IEngineeringEvaluator* evaluator : pending) {
                for (const std::string& artifactId : evaluator->requiredArtifactIds()) {
                    if (!hasArtifact(artifacts, artifactId))
                        return insufficientResult(candidate, artifactId);
                }
            }
        }
    }

    return aggregate;
}

} // namespace rws
