#ifndef RWS_ROBOTANALYSISCORE_IENGINEERINGEVALUATOR_HPP
#define RWS_ROBOTANALYSISCORE_IENGINEERINGEVALUATOR_HPP

#include "EngineeringEvaluationTypes.hpp"

namespace rws {

class IEngineeringEvaluator
{
public:
    virtual ~IEngineeringEvaluator() = default;
    virtual std::string id() const = 0;
    virtual std::string version() const = 0;
    virtual EngineeringEvaluationResult evaluate(
        const CandidateEvaluationContext& candidate,
        const EvaluationRequest& request,
        const EvaluationCallbacks& callbacks) = 0;
};

} // namespace rws

#endif // RWS_ROBOTANALYSISCORE_IENGINEERINGEVALUATOR_HPP
