#ifndef RWS_STRUCTUREOPTIMIZATION_ESTIMATEDWORKSPACESTAGE_HPP
#define RWS_STRUCTUREOPTIMIZATION_ESTIMATEDWORKSPACESTAGE_HPP

#include "EvaluationStage.hpp"
#include <rwslibs/kinematicanalysis/KinematicAnalysisContext.hpp>
#include <rwslibs/kinematicanalysis/KinematicAnalysisTypes.hpp>

namespace rws {

struct EstimatedWorkspaceResult {
    EvaluationStageResult stage;
    std::vector<WorkspaceSample> samples;
    bool collisionChecked = false;
};

class EstimatedWorkspaceStage final {
  public:
    EstimatedWorkspaceResult evaluate(
        const AnalysisContext& context,
        const WorkspaceSamplingConfig& config,
        const CancellationToken& cancellation = CancellationToken()) const;
};

} // namespace rws

#endif
