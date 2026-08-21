#ifndef RWS_STRUCTUREOPTIMIZATION_ELITESELECTOR_HPP
#define RWS_STRUCTUREOPTIMIZATION_ELITESELECTOR_HPP

#include "CandidateResult.hpp"
#include "QuickScreeningPolicy.hpp"

#include <cstddef>
#include <vector>

namespace rws {

/** A result together with the stable design-space coordinates used for diversity. */
struct EliteSelectionCandidate
{
    std::size_t stableIndex = 0;
    CandidateResult result;
    std::vector<double> normalizedDesign;
    QuickScreeningDecision screeningDecision = QuickScreeningDecision::Uncertain;
};

struct EliteSelectorConfig
{
    std::size_t eliteCount = 0;
    std::size_t uncertainQuota = 0;
    double diversityWeight = 0.0;
};

struct EliteSelectionResult
{
    std::vector<std::size_t> indices;
};

/**
 * @brief Deterministic, feasibility-first selection of the next elite pool.
 *
 * The selector is deliberately evidence-only: it does not mutate candidate
 * results or perform another evaluation.  Uncertain Quick results are capped
 * by an explicit quota and stable indices are the final tie breaker.
 */
class EliteSelector
{
  public:
    static EliteSelectionResult select(const std::vector<EliteSelectionCandidate>& candidates,
                                       const EliteSelectorConfig& config);

    static double score(const CandidateResult& result);
    static double diversityDistance(const EliteSelectionCandidate& left,
                                    const EliteSelectionCandidate& right);
};

} // namespace rws

#endif
