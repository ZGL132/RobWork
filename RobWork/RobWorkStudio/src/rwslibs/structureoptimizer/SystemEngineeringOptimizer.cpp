#include "SystemEngineeringOptimizer.hpp"

#include "HybridStructureOptimizer.hpp"
#include "StructureCandidateCache.hpp"
#include "StructureObjectiveScorer.hpp"

namespace rws {

namespace {

const EngineeringMetric* findMetric(const EngineeringEvaluationResult& result,
                                    const std::string& metricId)
{
    for (const EngineeringMetric& metric : result.metrics)
        if (metric.metricId == metricId)
            return &metric;
    return nullptr;
}

double metricOr(const EngineeringEvaluationResult& result, const std::string& metricId,
                double fallback = 0.0)
{
    const EngineeringMetric* metric = findMetric(result, metricId);
    return metric == nullptr ? fallback : metric->value;
}

class PipelineCandidateEvaluator : public IStructureCandidateEvaluator
{
  public:
    PipelineCandidateEvaluator(const StructureOptimizationProblem& problem,
                               EngineeringEvaluatorPipeline& pipeline)
        : _problem(problem), _pipeline(pipeline)
    {}

    void evaluate(const StructureOptimizationProblem& problem,
                  StructureCandidateResult& candidate,
                  StructureEvaluationStage stage,
                  const StructureOptimizationCallbacks& callbacks,
                  StructureCandidateCache* cache) override
    {
        if (cache != nullptr) {
            StructureCandidateResult cached;
            if (cache->find(problem, candidate.values, stage, cached)) {
                candidate = cached;
                return;
            }
        }

        CandidateEvaluationContext context;
        context.designContext = _problem.context;
        context.variableValues = candidate.values;
        context.inputSnapshot.modelHash = _problem.context.modelSpec.robotName;
        context.inputSnapshot.configurationHash =
            stage == StructureEvaluationStage::Verified ? "verified" : "quick";
        for (const StructureDesignVariable& variable : _problem.variables)
            context.variableIds.push_back(variable.id);

        EvaluationRequest request;
        request.stage = stage == StructureEvaluationStage::Verified
            ? EngineeringEvaluationStage::Verified : EngineeringEvaluationStage::Quick;
        request.configurationHash = context.inputSnapshot.configurationHash;
        EvaluationCallbacks engineeringCallbacks;
        engineeringCallbacks.isCancellationRequested = callbacks.isCancellationRequested;
        EngineeringEvaluationResult result =
            _pipeline.evaluate(context, request, engineeringCallbacks);

        candidate.stage = stage;
        candidate.raw.modelValid = result.status == EngineeringEvaluationStatus::Success ||
                                   result.status == EngineeringEvaluationStatus::Infeasible;
        candidate.raw.weightedReachability =
            metricOr(result, "kinematics.reachability.weighted");
        candidate.raw.manipulabilityP10 =
            metricOr(result, "kinematics.manipulability.p10");
        candidate.raw.jointMarginP10 =
            metricOr(result, "kinematics.joint_margin.p10");
        candidate.raw.minimumJointMargin =
            metricOr(result, "kinematics.joint_margin.minimum");
        candidate.raw.workspaceCoverage =
            metricOr(result, "kinematics.workspace.coverage");
        candidate.raw.collisionFreeRate = metricOr(result, "collision.free_rate");
        candidate.raw.totalKinematicLength =
            metricOr(result, "geometry.kinematic_length");
        candidate.raw.baseHeight = metricOr(result, "geometry.base_height");
        candidate.raw.maxCrossSection =
            metricOr(result, "geometry.cross_section.maximum");
        candidate.raw.maxLinkSlenderness =
            metricOr(result, "geometry.link_slenderness.maximum");
        candidate.raw.engineeringPreference =
            metricOr(result, "structure.preference");
        candidate.raw.requiredTaskCount = static_cast<int>(metricOr(
            result, "kinematics.task.required.count"));
        candidate.raw.requiredReachableCount = static_cast<int>(metricOr(
            result, "kinematics.task.required.reachable_count"));
        candidate.raw.optionalTaskCount = static_cast<int>(metricOr(
            result, "kinematics.task.optional.count"));
        candidate.raw.optionalReachableCount = static_cast<int>(metricOr(
            result, "kinematics.task.optional.reachable_count"));
        candidate.raw.modelBuildSeconds = metricOr(
            result, "evaluation.model_build_seconds");
        candidate.raw.kinematicEvaluationSeconds = metricOr(
            result, "evaluation.kinematic_seconds");
        for (const AnalysisWarning& warning : result.warnings)
            candidate.warnings.push_back(warning.code + ": " + warning.message);

        if (result.status == EngineeringEvaluationStatus::Cancelled) {
            candidate.status = StructureCandidateStatus::Canceled;
        } else if (result.status == EngineeringEvaluationStatus::Failed ||
                   result.status == EngineeringEvaluationStatus::DataInsufficient) {
            candidate.status = StructureCandidateStatus::Failed;
        } else {
            StructureObjectiveScorer scorer;
            scorer.score(_problem, candidate);
            for (const EngineeringConstraintResult& constraint : result.constraints) {
                if (constraint.hard && !constraint.satisfied) {
                    candidate.feasible = false;
                    candidate.status = StructureCandidateStatus::Infeasible;
                    candidate.violatedConstraints.push_back(constraint.metricId);
                }
            }
            if (result.status == EngineeringEvaluationStatus::Infeasible) {
                candidate.feasible = false;
                candidate.status = StructureCandidateStatus::Infeasible;
            }
        }

        if (cache != nullptr)
            cache->put(problem, candidate.values, stage, candidate);
    }

  private:
    const StructureOptimizationProblem& _problem;
    EngineeringEvaluatorPipeline& _pipeline;
};

} // namespace

StructureOptimizationResult SystemEngineeringOptimizer::optimize(
    const StructureOptimizationProblem& problem, EngineeringEvaluatorPipeline& pipeline,
    const StructureOptimizationCallbacks& callbacks) const
{
    PipelineCandidateEvaluator evaluator(problem, pipeline);
    HybridStructureOptimizer optimizer;
    return optimizer.optimize(problem, evaluator, callbacks);
}

} // namespace rws
