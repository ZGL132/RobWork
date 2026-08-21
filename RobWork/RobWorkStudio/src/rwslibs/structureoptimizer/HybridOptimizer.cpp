#include "HybridOptimizer.hpp"

#include <algorithm>

namespace rws {
namespace {

bool canceled(const HybridOptimizerCallbacks& callbacks)
{
    return callbacks.isCancellationRequested && callbacks.isCancellationRequested();
}

bool budgetExhausted(std::size_t evaluations, const HybridOptimizerConfig& config)
{
    return config.maxEvaluationCount != 0 && evaluations >= config.maxEvaluationCount;
}

const HybridCandidateState* findState(const std::vector<HybridCandidateState>& states,
                                      std::size_t stableIndex)
{
    for (const HybridCandidateState& state : states)
        if (state.seed.stableIndex == stableIndex)
            return &state;
    return nullptr;
}

HybridCandidateState* findState(std::vector<HybridCandidateState>& states,
                                std::size_t stableIndex)
{
    for (HybridCandidateState& state : states)
        if (state.seed.stableIndex == stableIndex)
            return &state;
    return nullptr;
}

} // namespace

HybridOptimizationResult HybridOptimizer::run(
    const std::vector<HybridCandidateSeed>& seeds,
    const HybridOptimizerConfig& config,
    const HybridEvaluationCallback& evaluate,
    const HybridOptimizerCallbacks& callbacks)
{
    HybridOptimizationResult output;
    if (!evaluate || seeds.empty())
        return output;

    // Batch 1: every seed gets at most one Quick evaluation, in stable order.
    for (const HybridCandidateSeed& seed : seeds) {
        if (canceled(callbacks)) {
            output.canceled = true;
            break;
        }
        if (budgetExhausted(output.evaluatedCount, config))
            break;

        HybridCandidateState state;
        state.seed = seed;
        state.result = evaluate(seed, AnalysisEvidenceStage::Quick);
        ++output.evaluatedCount;

        QuickScreeningPolicyInput facts = seed.quickFacts;
        facts.candidate = &state.result;
        state.screening = QuickScreeningPolicy().evaluate(facts);
        output.candidates.push_back(std::move(state));
    }

    // Do not start a second batch after cancellation or when no complete pool
    // can be promoted under the candidate budget.
    if (output.canceled || canceled(callbacks)) {
        output.canceled = true;
        return output;
    }
    if (output.candidates.empty() || output.evaluatedCount < output.candidates.size())
        return output;

    std::vector<EliteSelectionCandidate> selectionCandidates;
    selectionCandidates.reserve(output.candidates.size());
    for (const HybridCandidateState& state : output.candidates) {
        EliteSelectionCandidate candidate;
        candidate.stableIndex = state.seed.stableIndex;
        candidate.result = state.result;
        candidate.normalizedDesign = state.seed.normalizedDesign;
        candidate.screeningDecision = state.screening.decision;
        selectionCandidates.push_back(std::move(candidate));
    }
    EliteSelectorConfig selectorConfig;
    selectorConfig.eliteCount = config.eliteCount;
    selectorConfig.uncertainQuota = config.uncertainQuota;
    selectorConfig.diversityWeight = config.diversityWeight;
    output.eliteIndices = EliteSelector::select(selectionCandidates, selectorConfig).indices;

    // Batch 2: promote only the selected stable indices to Verified.
    for (std::size_t stableIndex : output.eliteIndices) {
        if (canceled(callbacks)) {
            output.canceled = true;
            break;
        }
        if (budgetExhausted(output.evaluatedCount, config))
            break;
        HybridCandidateState* state = findState(output.candidates, stableIndex);
        if (state == nullptr)
            continue;
        state->result = evaluate(state->seed, AnalysisEvidenceStage::Verified);
        ++output.evaluatedCount;
    }

    // Final best is intentionally narrow: only Feasible + Verified qualifies.
    for (const HybridCandidateState& state : output.candidates) {
        if (state.result.feasibility != Feasibility::Feasible ||
            state.result.evidenceStage != AnalysisEvidenceStage::Verified)
            continue;
        if (!output.bestCandidateIndex.has_value()) {
            output.bestCandidateIndex = state.seed.stableIndex;
            continue;
        }
        const HybridCandidateState* current =
            findState(output.candidates, output.bestCandidateIndex.value());
        if (current == nullptr)
            continue;
        EliteSelectionCandidate left;
        left.stableIndex = state.seed.stableIndex;
        left.result = state.result;
        EliteSelectionCandidate right;
        right.stableIndex = current->seed.stableIndex;
        right.result = current->result;
        if (EliteSelector::score(left.result) > EliteSelector::score(right.result) ||
            (EliteSelector::score(left.result) == EliteSelector::score(right.result) &&
             left.stableIndex < right.stableIndex))
            output.bestCandidateIndex = state.seed.stableIndex;
    }
    return output;
}

} // namespace rws
