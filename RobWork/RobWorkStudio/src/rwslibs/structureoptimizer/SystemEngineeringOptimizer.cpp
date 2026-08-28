#include "SystemEngineeringOptimizer.hpp"

#include "HybridStructureOptimizer.hpp"
#include "StructureCandidateCache.hpp"
#include "StructureObjectiveScorer.hpp"

#include <rwslibs/robotmodelbuilder/RobotModelFingerprint.hpp>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cmath>
#include <limits>

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

bool finiteNumber(const QJsonObject& object, const QString& key, double& value)
{
    const QJsonValue jsonValue = object.value(key);
    if (!jsonValue.isDouble())
        return false;
    value = jsonValue.toDouble();
    return std::isfinite(value);
}

bool cellCount(const QJsonObject& object, const QString& key, std::size_t& value)
{
    double number = 0.0;
    if (!finiteNumber(object, key, number) || number < 0.0 ||
        std::floor(number) != number ||
        number >= static_cast<double>(std::numeric_limits<std::size_t>::max()))
        return false;
    value = static_cast<std::size_t>(number);
    return true;
}

std::vector<StructureWorkspaceRegionMetric> workspaceRegionMetrics(
    const EngineeringEvaluationResult& result)
{
    for (const EngineeringArtifact& artifact : result.artifacts) {
        if (artifact.artifactId != "kinematics.workspace.coverage-summary")
            continue;

        QJsonParseError parseError;
        const QJsonDocument summary = QJsonDocument::fromJson(
            QByteArray::fromStdString(artifact.payload), &parseError);
        if (parseError.error != QJsonParseError::NoError || !summary.isObject())
            return {};
        const QJsonValue regionsValue =
            summary.object().value(QStringLiteral("regions"));
        if (!regionsValue.isArray())
            return {};

        const QJsonArray regions = regionsValue.toArray();
        std::vector<StructureWorkspaceRegionMetric> metrics;
        metrics.reserve(static_cast<std::size_t>(regions.size()));
        for (const QJsonValue& regionValue : regions) {
            if (!regionValue.isObject())
                return {};
            const QJsonObject region = regionValue.toObject();
            const QString id = region.value(QStringLiteral("id")).toString();
            if (id.isEmpty())
                return {};

            StructureWorkspaceRegionMetric metric;
            metric.id = id.toStdString();
            metric.referenceFrame = region.value(QStringLiteral("referenceFrame"))
                                        .toString(QStringLiteral("WORLD")).toStdString();
            if (!finiteNumber(region, QStringLiteral("coverage"), metric.coverage) ||
                !finiteNumber(region, QStringLiteral("orientationCoverage"),
                              metric.orientationCoverage) ||
                !cellCount(region, QStringLiteral("occupiedCellCount"),
                           metric.occupiedCellCount) ||
                !cellCount(region, QStringLiteral("totalCellCount"),
                           metric.totalCellCount))
                return {};
            metrics.push_back(std::move(metric));
        }
        return metrics;
    }
    return {};
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
            const int identityIndex = candidate.index;
            const std::vector<double> identityValues = candidate.values;
            StructureCandidateResult cached;
            if (cache->find(problem, candidate.values, stage, cached)) {
                candidate = cached;
                candidate.index = identityIndex;
                candidate.values = identityValues;
                return;
            }
        }

        CandidateEvaluationContext context;
        context.designContext = _problem.context;
        context.variableValues = candidate.values;
        context.inputSnapshot.modelHash =
            RobotModelFingerprint::canonicalSha256(_problem.context.modelSpec);
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
        candidate.raw.workspaceRegionMetrics = workspaceRegionMetrics(result);
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
        candidate.raw.workspaceEvaluationSeconds = metricOr(
            result, "evaluation.workspace_seconds");
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
