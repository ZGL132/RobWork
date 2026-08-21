#ifndef RWS_STRUCTUREOPTIMIZATION_VERIFIEDREGIONSTAGE_HPP
#define RWS_STRUCTUREOPTIMIZATION_VERIFIEDREGIONSTAGE_HPP

#include "EvaluationPlan.hpp"
#include "EvaluationStage.hpp"
#include <rwslibs/kinematicanalysis/KinematicAnalysisContext.hpp>
#include <rwslibs/kinematicanalysis/KinematicAnalysisTypes.hpp>

namespace rws {

struct VerifiedRegionResult {
    EvaluationStageResult stage;
    std::vector<RegionCoverageResult> regions;
};

class VerifiedRegionStage final {
  public:
    VerifiedRegionResult evaluate(
        const AnalysisContext& context,
        const EvaluationPlan& plan,
        const CancellationToken& cancellation = CancellationToken()) const;
};

} // namespace rws

#endif
