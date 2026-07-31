#include "KinematicEngineeringEvaluator.hpp"
#include "StructureCandidateEvaluator.hpp"

#include <exception>
#include <sstream>

namespace rws {

namespace {

EngineeringEvaluationStatus statusFor(const StructureCandidateResult& candidate)
{
    switch (candidate.status) {
    case StructureCandidateStatus::Feasible:
        return EngineeringEvaluationStatus::Success;
    case StructureCandidateStatus::Infeasible:
        return EngineeringEvaluationStatus::Infeasible;
    case StructureCandidateStatus::Canceled:
        return EngineeringEvaluationStatus::Cancelled;
    case StructureCandidateStatus::Failed:
    case StructureCandidateStatus::Pending:
    default:
        return EngineeringEvaluationStatus::Failed;
    }
}

void addMetric(EngineeringEvaluationResult& result, const std::string& id,
               double value, const std::string& unit, const std::string& provider)
{
    EngineeringMetric metric;
    metric.metricId = id;
    metric.value = value;
    metric.unit = unit;
    metric.providerId = provider;
    result.metrics.push_back(metric);
}

std::string ikSummary(const StructureRawMetrics& raw)
{
    std::ostringstream stream;
    stream << "{\"tasks\":[";
    for (std::size_t i = 0; i < raw.taskMetrics.size(); ++i) {
        const StructureTaskMetric& task = raw.taskMetrics[i];
        if (i != 0)
            stream << ',';
        stream << "{\"id\":\"" << task.taskId
               << "\",\"usableSolutionCount\":" << task.usableSolutionCount
               << ",\"reachable\":" << (task.reachable ? "true" : "false")
               << "}";
    }
    stream << "]}";
    return stream.str();
}

std::string workspaceSummary(const StructureRawMetrics& raw)
{
    std::ostringstream stream;
    stream << "{\"coverage\":" << raw.workspaceCoverage
           << ",\"occupiedCellCount\":" << raw.workspaceOccupiedCellCount
           << ",\"totalCellCount\":" << raw.workspaceTotalCellCount
           << "}";
    return stream.str();
}

} // namespace

KinematicEngineeringEvaluator::KinematicEngineeringEvaluator(
    const StructureOptimizationProblem& problem) : _problem(problem)
{}

std::string KinematicEngineeringEvaluator::id() const
{
    return _problem.evaluation.evaluatorId.empty()
        ? "structure.kinematics" : _problem.evaluation.evaluatorId;
}

std::string KinematicEngineeringEvaluator::version() const
{
    return _problem.evaluation.evaluatorVersion.empty()
        ? "1" : _problem.evaluation.evaluatorVersion;
}

std::vector<std::string> KinematicEngineeringEvaluator::providedArtifactIds() const
{
    return {"kinematics.ik-solutions", "kinematics.workspace.coverage-summary",
            "structure.raw-metrics"};
}

EngineeringEvaluationResult KinematicEngineeringEvaluator::evaluate(
    const CandidateEvaluationContext& context, const EvaluationRequest& request,
    const EvaluationCallbacks& callbacks)
{
    StructureCandidateResult candidate;
    candidate.values = context.variableValues;
    StructureOptimizationCallbacks legacyCallbacks;
    legacyCallbacks.isCancellationRequested = callbacks.isCancellationRequested;
    EngineeringEvaluationResult result;
    result.providerId = id();
    result.providerVersion = version();
    result.inputSnapshot = context.inputSnapshot;
    try {
        evaluateLegacy(candidate,
                       request.stage == EngineeringEvaluationStage::Verified
                           ? StructureEvaluationStage::Verified
                           : StructureEvaluationStage::Quick,
                       legacyCallbacks, nullptr);
    } catch (const std::exception& error) {
        result.status = EngineeringEvaluationStatus::Failed;
        AnalysisWarning warning;
        warning.code = "KinematicEngineeringEvaluator.Exception";
        warning.message = error.what();
        warning.source = id();
        warning.severity = AnalysisStatus::Fail;
        result.warnings.push_back(warning);
        return result;
    } catch (...) {
        result.status = EngineeringEvaluationStatus::Failed;
        AnalysisWarning warning;
        warning.code = "KinematicEngineeringEvaluator.UnknownException";
        warning.message = "Unknown kinematic evaluation failure.";
        warning.source = id();
        warning.severity = AnalysisStatus::Fail;
        result.warnings.push_back(warning);
        return result;
    }
    result.status = statusFor(candidate);
    const StructureRawMetrics& raw = candidate.raw;
    if (raw.workspaceCoverageDataInsufficient) {
        result.status = EngineeringEvaluationStatus::DataInsufficient;
    }
    if (result.status == EngineeringEvaluationStatus::Success &&
        raw.requiredReachableCount < raw.requiredTaskCount) {
        result.status = EngineeringEvaluationStatus::Infeasible;
    }
    addMetric(result, "kinematics.reachability.weighted", raw.weightedReachability,
              "ratio", id());
    addMetric(result, "kinematics.manipulability.p10", raw.manipulabilityP10,
              "ratio", id());
    addMetric(result, "kinematics.joint_margin.p10", raw.jointMarginP10,
              "ratio", id());
    addMetric(result, "kinematics.joint_margin.minimum", raw.minimumJointMargin,
              "ratio", id());
    if ((!_problem.evaluation.coverageBoxes.empty() || _problem.evaluation.coverageBox.enabled) &&
        !raw.workspaceCoverageDataInsufficient) {
        addMetric(result, "kinematics.workspace.coverage", raw.workspaceCoverage,
                  "ratio", id());
    }
    addMetric(result, "kinematics.task.required.count", raw.requiredTaskCount,
              "count", id());
    addMetric(result, "kinematics.task.required.reachable_count",
              raw.requiredReachableCount, "count", id());
    addMetric(result, "kinematics.task.optional.count", raw.optionalTaskCount,
              "count", id());
    addMetric(result, "kinematics.task.optional.reachable_count",
              raw.optionalReachableCount, "count", id());
    addMetric(result, "collision.free_rate", raw.collisionFreeRate, "ratio", id());
    addMetric(result, "geometry.compactness", candidate.scores.compactness,
              "ratio", id());
    addMetric(result, "geometry.kinematic_length", raw.totalKinematicLength,
              "m", id());
    addMetric(result, "geometry.base_height", raw.baseHeight, "m", id());
    addMetric(result, "geometry.cross_section.maximum", raw.maxCrossSection,
              "m2", id());
    addMetric(result, "geometry.link_slenderness.maximum", raw.maxLinkSlenderness,
              "ratio", id());
    addMetric(result, "structure.preference", raw.engineeringPreference,
              "ratio", id());
    addMetric(result, "evaluation.model_build_seconds", raw.modelBuildSeconds,
              "s", id());
    addMetric(result, "evaluation.kinematic_seconds",
              raw.kinematicEvaluationSeconds, "s", id());
    addMetric(result, "evaluation.workspace_seconds",
              raw.workspaceEvaluationSeconds, "s", id());
    result.artifacts.push_back({"kinematics.ik-solutions", "application/json", ikSummary(raw)});
    if ((!_problem.evaluation.coverageBoxes.empty() || _problem.evaluation.coverageBox.enabled) &&
        !raw.workspaceCoverageDataInsufficient) {
        result.artifacts.push_back({"kinematics.workspace.coverage-summary",
                                    "application/json", workspaceSummary(raw)});
    }
    result.artifacts.push_back({"structure.raw-metrics", "application/json", "{}"});

    for (const std::string& violation : candidate.violatedConstraints) {
        EngineeringConstraintResult constraint;
        constraint.metricId = violation;
        constraint.hard = true;
        constraint.satisfied = false;
        constraint.failureReason = "Structure constraint was not satisfied.";
        result.constraints.push_back(constraint);
    }
    for (const std::string& message : candidate.warnings) {
        AnalysisWarning warning;
        warning.code = "KinematicEngineeringEvaluator.Warning";
        warning.message = message;
        warning.source = id();
        warning.severity = AnalysisStatus::Warning;
        result.warnings.push_back(warning);
    }
    return result;
}

void StructureCandidateEvaluator::evaluate(
    const StructureOptimizationProblem& problem, StructureCandidateResult& candidate,
    StructureEvaluationStage stage, const StructureOptimizationCallbacks& callbacks,
    StructureCandidateCache* cache)
{
    KinematicEngineeringEvaluator evaluator(problem);
    evaluator.evaluateLegacy(candidate, stage, callbacks, cache);
}

} // namespace rws
