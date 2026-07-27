#ifndef RWS_STRUCTUREOPTIMIZATION_KINEMATICENGINEERINGEVALUATOR_HPP
#define RWS_STRUCTUREOPTIMIZATION_KINEMATICENGINEERINGEVALUATOR_HPP

#include "StructureCandidateCache.hpp"
#include "StructureOptimizationStrategy.hpp"

#include <rwslibs/robotanalysiscore/IEngineeringEvaluator.hpp>

namespace rws {

class KinematicEngineeringEvaluator : public IEngineeringEvaluator
{
  public:
    explicit KinematicEngineeringEvaluator(const StructureOptimizationProblem& problem);

    std::string id() const override;
    std::string version() const override;
    std::vector<std::string> providedArtifactIds() const override;
    EngineeringEvaluationResult evaluate(const CandidateEvaluationContext& candidate,
                                         const EvaluationRequest& request,
                                         const EvaluationCallbacks& callbacks) override;

    void evaluateLegacy(StructureCandidateResult& candidate,
                        StructureEvaluationStage stage,
                        const StructureOptimizationCallbacks& callbacks,
                        StructureCandidateCache* cache = nullptr);

  private:
    const StructureOptimizationProblem& _problem;
};

} // namespace rws

#endif
