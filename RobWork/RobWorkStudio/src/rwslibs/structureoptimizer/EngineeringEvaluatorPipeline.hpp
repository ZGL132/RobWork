#ifndef RWS_STRUCTUREOPTIMIZATION_ENGINEERINGEVALUATORPIPELINE_HPP
#define RWS_STRUCTUREOPTIMIZATION_ENGINEERINGEVALUATORPIPELINE_HPP

#include <rwslibs/robotanalysiscore/IEngineeringEvaluator.hpp>

#include <vector>

namespace rws {

class EngineeringEvaluatorPipeline
{
  public:
    void addEvaluator(IEngineeringEvaluator& evaluator);
    EngineeringEvaluationResult evaluate(const CandidateEvaluationContext& candidate,
                                         const EvaluationRequest& request,
                                         const EvaluationCallbacks& callbacks) const;

  private:
    std::vector<IEngineeringEvaluator*> _evaluators;
};

} // namespace rws

#endif
