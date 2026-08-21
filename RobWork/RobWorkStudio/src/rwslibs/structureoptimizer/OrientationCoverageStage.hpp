#ifndef RWS_STRUCTUREOPTIMIZATION_ORIENTATIONCOVERAGESTAGE_HPP
#define RWS_STRUCTUREOPTIMIZATION_ORIENTATIONCOVERAGESTAGE_HPP

#include "EvaluationStage.hpp"
#include <rwslibs/kinematicanalysis/KinematicAnalysisContext.hpp>
#include <rwslibs/kinematicanalysis/KinematicAnalysisTypes.hpp>

namespace rws {

struct OrientationCoverageResult {
    EvaluationStageResult stage;
    std::vector<PoseReachabilitySample> samples;
    bool directionCoverageAvailable = true;
    bool fullOrientationCoverageAvailable = true;
};

class OrientationCoverageStage final {
  public:
    OrientationCoverageResult evaluate(
        const AnalysisContext& context,
        const std::vector<std::array<double, 3>>& positions,
        const PoseReachabilityConfig& config,
        const CancellationToken& cancellation = CancellationToken()) const;
};

} // namespace rws

#endif
