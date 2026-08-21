#include "TaskEvaluationStage.hpp"

#include <rwslibs/kinematicanalysis/TargetEvaluator.hpp>

#include <algorithm>
#include <exception>

namespace rws {
namespace {

TaskPointType taskType(RequirementExecutionProcessType type)
{
    switch (type) {
    case RequirementExecutionProcessType::Pick: return TaskPointType::Pick;
    case RequirementExecutionProcessType::Place:
    case RequirementExecutionProcessType::MachineLoad:
    case RequirementExecutionProcessType::MachineUnload: return TaskPointType::Place;
    case RequirementExecutionProcessType::Inspect: return TaskPointType::Inspect;
    case RequirementExecutionProcessType::WeldStart:
    case RequirementExecutionProcessType::WeldEnd: return TaskPointType::Weld;
    default: return TaskPointType::Generic;
    }
}

TaskPoint toTaskPoint(const RequirementExecutionTask& source)
{
    TaskPoint point;
    point.id = source.id;
    point.name = source.name;
    point.type = taskType(source.processType);
    point.refFrame = source.refFrame;
    point.tcpFrame = source.tcpFrame;
    point.position = source.position;
    point.rpyDeg = source.rpyDeg;
    point.tolerance.positionMeters = source.positionToleranceMeters;
    point.tolerance.orientationDeg = source.orientationToleranceDeg;
    point.tolerance.allowToolRollFree = source.allowToolRollFree;
    point.weight = source.level == RequirementExecutionLevel::Must ? 1.0 : 0.5;
    point.enabled = source.compileState == RequirementExecutionCompileState::Included;
    return point;
}

AnalysisEvidenceStage evidenceStage(const EvaluationPlanTask& task)
{
    return task.evidenceRequired || task.hardConstraint ? AnalysisEvidenceStage::Verified
                                                          : AnalysisEvidenceStage::Quick;
}

int qualityRank(Quality quality)
{
    switch (quality) {
    case Quality::Critical: return 3;
    case Quality::Degraded: return 2;
    case Quality::Good: return 1;
    case Quality::Unknown: return 0;
    }
    return 0;
}

Quality worstQuality(Quality left, Quality right)
{
    return qualityRank(left) >= qualityRank(right) ? left : right;
}

void addFailureCodes(std::vector<std::string>& output,
                     const std::vector<KinematicFailureReason>& reasons)
{
    for (const KinematicFailureReason reason : reasons) {
        const std::string code = toString(reason);
        if (std::find(output.begin(), output.end(), code) == output.end())
            output.push_back(code);
    }
}

TaskEvidence makeEvidence(const RequirementExecutionTask& source,
                          const TargetEvaluation& evaluation)
{
    TaskEvidence evidence;
    evidence.taskId = source.id;
    evidence.level = source.level;
    evidence.feasibility = evaluation.feasibility;
    evidence.quality = evaluation.quality;
    evidence.solutionCount = evaluation.candidates.size();
    addFailureCodes(evidence.failureCodes, evaluation.failureReasons);
    if (!evaluation.candidates.empty()) {
        const TargetCandidate& candidate = evaluation.candidates.front();
        evidence.positionResidualMeters = candidate.positionErrorMeters;
        evidence.orientationResidualDeg = candidate.orientationErrorDeg;
        evidence.collisionChecked = candidate.configuration.collisionChecked;
        evidence.inCollision = candidate.configuration.inCollision;
        evidence.representativeQ.reserve(candidate.configuration.q.size());
        for (std::size_t i = 0; i < candidate.configuration.q.size(); ++i)
            evidence.representativeQ.push_back(candidate.configuration.q[i]);
        addFailureCodes(evidence.failureCodes, candidate.configuration.failureReasons);
    }
    for (const AnalysisWarning& warning : evaluation.warnings) {
        if (std::find(evidence.failureCodes.begin(), evidence.failureCodes.end(), warning.code) ==
            evidence.failureCodes.end())
            evidence.failureCodes.push_back(warning.code);
    }
    return evidence;
}

} // namespace

TaskEvaluationResult TaskEvaluationStage::evaluate(
    const AnalysisContext& context,
    const EvaluationPlan& plan,
    const CancellationToken& cancellation) const
{
    TaskEvaluationResult output;
    output.stage.stageId = "task_evaluation";
    output.stage.version = "1";
    output.stage.requestedCount = plan.tasks.size();
    output.tasks.reserve(plan.tasks.size());

    bool hasMust = false;
    bool mustDataInsufficient = false;
    bool mustInfeasible = false;
    TargetEvaluator evaluator;
    for (const EvaluationPlanTask& task : plan.tasks) {
        if (cancellation.cancellationRequested()) {
            output.stage.status = EvaluationStageStatus::Canceled;
            output.mustFeasibility = hasMust
                                          ? (mustDataInsufficient ? Feasibility::DataInsufficient
                                                                   : mustInfeasible ? Feasibility::Infeasible
                                                                                    : Feasibility::Feasible)
                                          : Feasibility::NotEvaluated;
            return output;
        }

        TargetEvaluation evaluation;
        evaluation.target = toTaskPoint(task.source);
        evaluation.level = task.source.level;
        try {
            TargetEvaluationOptions options;
            options.evidenceStage = evidenceStage(task);
            options.checkCollision = task.source.collisionFreeRequired;
            options.requireCollisionFree = task.source.collisionFreeRequired;
            options.positionToleranceMeters = task.source.positionToleranceMeters;
            options.orientationToleranceDeg = task.source.orientationToleranceDeg;
            evaluation = evaluator.evaluate(context, evaluation.target, options);
            evaluation.level = task.source.level;
        }
        catch (const std::exception& exception) {
            evaluation.feasibility = Feasibility::DataInsufficient;
            evaluation.quality = Quality::Critical;
            evaluation.failureReasons.push_back(KinematicFailureReason::SolverError);
            AnalysisWarning warning;
            warning.code = "TASK_EVALUATOR_EXCEPTION";
            warning.message = exception.what();
            warning.source = "TaskEvaluationStage";
            warning.severity = AnalysisStatus::Warning;
            evaluation.warnings.push_back(warning);
        }

        output.tasks.push_back(makeEvidence(task.source, evaluation));
        ++output.stage.completedCount;
        if (task.source.level == RequirementExecutionLevel::Must) {
            hasMust = true;
            mustDataInsufficient = mustDataInsufficient ||
                                   evaluation.feasibility == Feasibility::DataInsufficient;
            mustInfeasible = mustInfeasible ||
                             evaluation.feasibility == Feasibility::Infeasible;
            output.mustQuality = worstQuality(output.mustQuality, evaluation.quality);
        }
    }

    if (!hasMust)
        output.mustFeasibility = Feasibility::NotEvaluated;
    else if (mustDataInsufficient)
        output.mustFeasibility = Feasibility::DataInsufficient;
    else if (mustInfeasible)
        output.mustFeasibility = Feasibility::Infeasible;
    else
        output.mustFeasibility = Feasibility::Feasible;

    output.stage.status = output.stage.completedCount < output.stage.requestedCount
                              ? EvaluationStageStatus::DataInsufficient
                              : output.stage.requestedCount == 0
                                    ? EvaluationStageStatus::NotEvaluated
                                    : output.mustFeasibility == Feasibility::DataInsufficient
                                          ? EvaluationStageStatus::DataInsufficient
                                          : output.mustFeasibility == Feasibility::Infeasible
                                                ? EvaluationStageStatus::Failed
                                                : EvaluationStageStatus::Passed;
    return output;
}

} // namespace rws
