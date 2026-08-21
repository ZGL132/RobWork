#ifndef RWS_STRUCTUREOPTIMIZATION_HYBRIDOPTIMIZER_HPP
#define RWS_STRUCTUREOPTIMIZATION_HYBRIDOPTIMIZER_HPP

#include "EliteSelector.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

namespace rws {

struct HybridCandidateSeed
{
    std::size_t stableIndex = 0;
    std::vector<double> normalizedDesign;
    QuickScreeningPolicyInput quickFacts;
};

struct HybridOptimizerConfig
{
    std::size_t eliteCount = 0;
    std::size_t uncertainQuota = 0;
    std::size_t maxEvaluationCount = 0; // zero means unlimited
    double diversityWeight = 0.0;
};

struct HybridOptimizerCallbacks
{
    std::function<bool()> isCancellationRequested;
};

struct HybridCandidateState
{
    HybridCandidateSeed seed;
    CandidateResult result;
    QuickScreeningResult screening;
};

struct HybridOptimizationResult
{
    std::vector<HybridCandidateState> candidates;
    std::vector<std::size_t> eliteIndices;
    std::optional<std::size_t> bestCandidateIndex;
    std::size_t evaluatedCount = 0;
    bool canceled = false;
};

using HybridEvaluationCallback =
    std::function<CandidateResult(const HybridCandidateSeed&, AnalysisEvidenceStage)>;

/** One deterministic Initial Pool -> Quick -> Elite -> Verified iteration. */
class HybridOptimizer
{
  public:
    static HybridOptimizationResult run(const std::vector<HybridCandidateSeed>& seeds,
                                         const HybridOptimizerConfig& config,
                                         const HybridEvaluationCallback& evaluate,
                                         const HybridOptimizerCallbacks& callbacks = {});
};

} // namespace rws

#endif
