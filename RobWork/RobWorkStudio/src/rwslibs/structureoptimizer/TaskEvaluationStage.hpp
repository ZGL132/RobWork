#ifndef RWS_STRUCTUREOPTIMIZATION_TASKEVALUATIONSTAGE_HPP
#define RWS_STRUCTUREOPTIMIZATION_TASKEVALUATIONSTAGE_HPP

#include "EvaluationPlan.hpp"
#include "EvaluationStage.hpp"

#include <rwslibs/kinematicanalysis/KinematicAnalysisContext.hpp>
#include <rwslibs/kinematicanalysis/KinematicAnalysisTypes.hpp>

#include <string>
#include <vector>

namespace rws {

struct TaskEvidence {
    std::string taskId;
    RequirementExecutionLevel level = RequirementExecutionLevel::Must;
    Feasibility feasibility = Feasibility::NotEvaluated;
    Quality quality = Quality::Unknown;
    double positionResidualMeters = 0.0;
    double orientationResidualDeg = 0.0;
    std::size_t solutionCount = 0;
    std::vector<double> representativeQ;
    bool collisionChecked = false;
    bool inCollision = false;
    std::vector<std::string> failureCodes;
};

struct TaskEvaluationResult {
    EvaluationStageResult stage;
    std::vector<TaskEvidence> tasks;
    Feasibility mustFeasibility = Feasibility::NotEvaluated;
    Quality mustQuality = Quality::Unknown;
};

/** Bridges frozen task requirements to the shared TargetEvaluator. */
class TaskEvaluationStage final {
  public:
    TaskEvaluationResult evaluate(
        const AnalysisContext& context,
        const EvaluationPlan& plan,
        const CancellationToken& cancellation = CancellationToken()) const;
};

} // namespace rws

#endif
