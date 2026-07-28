#include "HybridStructureOptimizer.hpp"
#include "StructureCandidateGenerator.hpp"
#include "StructureObjectiveScorer.hpp"
#include "StructureCandidateEvaluator.hpp"
#include "StructureSensitivityAnalyzer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <random>
#include <string>
#include <vector>

namespace rws {

// =============================================================================
//  Anonymous helpers
// =============================================================================
namespace {

// ---------------------------------------------------------------------------
//  currentTimestamp — ISO 8601 string
// ---------------------------------------------------------------------------
std::string currentTimestamp()
{
    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", std::localtime(&t));
    return std::string(buf);
}

// ---------------------------------------------------------------------------
//  generateLocalPerturbations — small random offsets around a centre
// ---------------------------------------------------------------------------
std::vector<std::vector<double>> generateLocalPerturbations(
    const std::vector<StructureDesignVariable>& variables,
    const std::vector<double>& centre,
    int count,
    std::mt19937& rng)
{
    std::vector<std::vector<double>> result;
    result.reserve(static_cast<std::size_t>(count));

    std::uniform_real_distribution<double> unitOffset(-1.0, 1.0);
    for (int i = 0; i < count; ++i)
    {
        std::vector<double> vals;
        vals.reserve(variables.size());

        for (std::size_t j = 0; j < variables.size(); ++j)
        {
            double range = variables[j].maximum - variables[j].minimum;
            double local = range * 0.15;   // 15 % neighbourhood
            double offset = unitOffset(rng) * local;
            double val = centre[j] + offset;
            val = std::max(variables[j].minimum, std::min(variables[j].maximum, val));
            vals.push_back(val);
        }
        result.push_back(vals);
    }
    return result;
}

// ---------------------------------------------------------------------------
//  gatherCurrentValues from problem.variables[].currentValue
// ---------------------------------------------------------------------------
std::vector<double> gatherCurrentValues(
    const std::vector<StructureDesignVariable>& variables)
{
    std::vector<double> vals;
    vals.reserve(variables.size());
    for (const auto& v : variables)
        vals.push_back(v.currentValue);
    return vals;
}

double normalizedDistance(const std::vector<StructureDesignVariable>& variables,
                          const std::vector<double>& first,
                          const std::vector<double>& second)
{
    double squaredDistance = 0.0;
    for (std::size_t i = 0; i < variables.size() && i < first.size() &&
                            i < second.size(); ++i) {
        const double range = variables[i].maximum - variables[i].minimum;
        if (range <= 0.0)
            continue;
        const double delta = (first[i] - second[i]) / range;
        squaredDistance += delta * delta;
    }
    return std::sqrt(squaredDistance);
}

std::vector<int> selectDiverseEliteIndices(
    const std::vector<StructureCandidateResult>& candidates,
    const std::vector<StructureDesignVariable>& variables, int count)
{
    std::vector<int> selected;
    if (count <= 0)
        return selected;

    for (const StructureCandidateResult& candidate : candidates) {
        if (static_cast<int>(selected.size()) >= count)
            break;
        if (selected.empty()) {
            selected.push_back(candidate.index);
            continue;
        }

        const StructureCandidateResult* best = nullptr;
        double bestPriority = -1.0;
        for (const StructureCandidateResult& option : candidates) {
            if (std::find(selected.begin(), selected.end(), option.index) != selected.end())
                continue;
            double minDistance = std::numeric_limits<double>::max();
            for (int selectedIndex : selected) {
                const auto selectedIt = std::find_if(
                    candidates.begin(), candidates.end(),
                    [selectedIndex](const StructureCandidateResult& value) {
                        return value.index == selectedIndex;
                    });
                minDistance = std::min(minDistance, normalizedDistance(
                    variables, option.values, selectedIt->values));
            }
            const double feasibilityPriority = option.feasible ? 1000.0 : 0.0;
            const double priority = feasibilityPriority + option.totalScore + minDistance * 5.0;
            if (best == nullptr || priority > bestPriority) {
                best = &option;
                bestPriority = priority;
            }
        }
        if (best != nullptr)
            selected.push_back(best->index);
    }
    return selected;
}

StructureCandidateResult* findCandidateByIndex(
    std::vector<StructureCandidateResult>& candidates, int index)
{
    for (StructureCandidateResult& candidate : candidates) {
        if (candidate.index == index)
            return &candidate;
    }
    return nullptr;
}

const StructureCandidateResult* findCandidateByIndex(
    const std::vector<StructureCandidateResult>& candidates, int index)
{
    for (const StructureCandidateResult& candidate : candidates) {
        if (candidate.index == index)
            return &candidate;
    }
    return nullptr;
}

} // anonymous namespace

// ===========================================================================
//  optimize()
// ===========================================================================
StructureOptimizationResult HybridStructureOptimizer::optimize(
    const StructureOptimizationProblem& problem,
    IStructureCandidateEvaluator& evaluator,
    const StructureOptimizationCallbacks& callbacks)
{
    auto tStart = std::chrono::steady_clock::now();

    // ── 1.  Result & diagnostics ────────────────────────────────────────
    StructureOptimizationResult result;
    result.startedAt = currentTimestamp();

    StructureCandidateCache   cache;
    StructureRunDiagnostics   diag{};

    // ── 2.  Evaluate baseline (index 0) ─────────────────────────────────
    std::vector<double> baseValues = gatherCurrentValues(problem.variables);

    {
        StructureCandidateResult baseline;
        baseline.index  = 0;
        baseline.values = baseValues;
        evaluator.evaluate(problem, baseline,
                           StructureEvaluationStage::Verified,
                           callbacks, &cache);
        ++diag.evaluatedCandidates;
        result.candidates.push_back(std::move(baseline));
    }
    result.baselineCandidateIndex = 0;

    // ── 3.  Generate candidates ─────────────────────────────────────────
    std::vector<std::vector<double>> candidatePool;

    switch (problem.run.strategy)
    {
    case StructureStrategyKind::Random:
        candidatePool = StructureCandidateGenerator::randomUniform(
            problem.variables, problem.run.candidateCount,
            problem.run.randomSeed);
        break;

    case StructureStrategyKind::Grid:
        candidatePool = StructureCandidateGenerator::grid(
            problem.variables, problem.run.gridSteps,
            problem.run.candidateCount);
        break;

    case StructureStrategyKind::Hybrid:
    default:
        candidatePool = StructureCandidateGenerator::latinHypercube(
            problem.variables, problem.run.candidateCount,
            problem.run.randomSeed);
        break;
    }

    diag.generatedCandidates = 1 + static_cast<int>(candidatePool.size());

    // ── 4.  Quick-evaluate all candidates ───────────────────────────────
    {
        int completed = 0;
        for (std::size_t i = 0; i < candidatePool.size(); ++i)
        {
            if (callbacks.isCancellationRequested &&
                callbacks.isCancellationRequested())
            {
                result.canceled = true;
                break;
            }
            if (callbacks.waitIfPaused)
                callbacks.waitIfPaused();

            StructureCandidateResult cr;
            cr.index  = static_cast<int>(result.candidates.size());
            cr.values = candidatePool[i];
            evaluator.evaluate(problem, cr, StructureEvaluationStage::Quick,
                               callbacks, &cache);
            ++diag.evaluatedCandidates;
            result.candidates.push_back(std::move(cr));
            ++completed;

            if (callbacks.onProgress)
            {
                StructureProgress p;
                p.stage     = "Quick";
                p.completed = completed;
                p.planned   = static_cast<int>(candidatePool.size());
                for (const auto& c : result.candidates)
                    if (c.totalScore > p.bestScore)
                        p.bestScore = c.totalScore;
                callbacks.onProgress(p);
            }
        }
    }

    // ── 5.  Hybrid-specific: Verified elite + local search ──────────────
    if (problem.run.strategy == StructureStrategyKind::Hybrid &&
        !result.canceled)
    {
        // 5a.  Sort and select elites
        StructureObjectiveScorer::sortForDecision(result.candidates);

        // Preserve feasibility and proxy score while spreading elites in design space.
        int eliteCount = std::min(problem.run.eliteCount,
                                  static_cast<int>(result.candidates.size()));
        std::vector<int> eliteIndices = selectDiverseEliteIndices(
            result.candidates, problem.variables, eliteCount);

        // 5b.  Verified-evaluate elites
        {
            int completed = 0;
            for (int ei : eliteIndices)
            {
                if (callbacks.isCancellationRequested &&
                    callbacks.isCancellationRequested())
                {
                    result.canceled = true;
                    break;
                }
                if (callbacks.waitIfPaused)
                    callbacks.waitIfPaused();

                StructureCandidateResult* elitePtr =
                    findCandidateByIndex(result.candidates, ei);
                if (!elitePtr)
                    continue;

                // Re-evaluate at Verified stage
                evaluator.evaluate(problem, *elitePtr,
                                   StructureEvaluationStage::Verified,
                                   callbacks, &cache);
                // (cache is updated inside the evaluator)
                ++completed;

                if (callbacks.onProgress)
                {
                    StructureProgress p;
                    p.stage     = "Verified";
                    p.completed = completed;
                    p.planned   = eliteCount;
                    for (const auto& c : result.candidates)
                        if (c.totalScore > p.bestScore)
                            p.bestScore = c.totalScore;
                    callbacks.onProgress(p);
                }
            }
        }

        std::vector<StructureCandidateResult> verifiedElites;
        for (int eliteIndex : eliteIndices) {
            const StructureCandidateResult* candidate =
                findCandidateByIndex(result.candidates, eliteIndex);
            if (candidate != nullptr && candidate->feasible &&
                candidate->stage == StructureEvaluationStage::Verified) {
                verifiedElites.push_back(*candidate);
            }
        }
        const int localEliteCount = std::min(
            problem.run.localEliteCount,
            static_cast<int>(verifiedElites.size()));
        const std::vector<int> localEliteIndices = selectDiverseEliteIndices(
            verifiedElites, problem.variables, localEliteCount);

        // 5c.  Local search around the configured number of verified elites.
        if (!result.canceled && !localEliteIndices.empty())
        {
            int localPerElite = std::max(1,
                problem.run.maxLocalSweeps / localEliteCount);

            std::mt19937 localRng(problem.run.randomSeed + 9999u);

            std::vector<std::vector<double>> localPool;
            localPool.reserve(static_cast<std::size_t>(
                localPerElite * localEliteCount));

            for (int ei : localEliteIndices)
            {
                const StructureCandidateResult* elitePtr =
                    findCandidateByIndex(result.candidates, ei);
                if (!elitePtr)
                    continue;

                auto perturbed = generateLocalPerturbations(
                    problem.variables, elitePtr->values, localPerElite, localRng);
                localPool.insert(localPool.end(),
                                 std::make_move_iterator(perturbed.begin()),
                                 std::make_move_iterator(perturbed.end()));
            }

            diag.generatedCandidates += static_cast<int>(localPool.size());

            int completed = 0;
            for (std::size_t i = 0; i < localPool.size(); ++i)
            {
                if (callbacks.isCancellationRequested &&
                    callbacks.isCancellationRequested())
                {
                    result.canceled = true;
                    break;
                }
                if (callbacks.waitIfPaused)
                    callbacks.waitIfPaused();

                StructureCandidateResult cr;
                cr.index  = static_cast<int>(result.candidates.size());
                cr.values = localPool[i];
                evaluator.evaluate(problem, cr, StructureEvaluationStage::Quick,
                                   callbacks, &cache);
                ++diag.evaluatedCandidates;
                result.candidates.push_back(std::move(cr));
                ++completed;

                if (callbacks.onProgress)
                {
                    StructureProgress p;
                    p.stage     = "Local";
                    p.completed = completed;
                    p.planned   = static_cast<int>(localPool.size());
                    for (const auto& c : result.candidates)
                        if (c.totalScore > p.bestScore)
                            p.bestScore = c.totalScore;
                    callbacks.onProgress(p);
                }
            }
        }
    }

    // ── 6.  Final verified review of leading candidates ─────────────────
    StructureObjectiveScorer::sortForDecision(result.candidates);
    if (!result.canceled) {
        const int finalVerificationCount = std::min(
            problem.run.finalVerificationCount,
            static_cast<int>(result.candidates.size()));
        int completed = 0;
        for (int i = 0; i < finalVerificationCount; ++i) {
            if (callbacks.isCancellationRequested &&
                callbacks.isCancellationRequested()) {
                result.canceled = true;
                break;
            }
            if (callbacks.waitIfPaused)
                callbacks.waitIfPaused();

            evaluator.evaluate(problem, result.candidates[static_cast<std::size_t>(i)],
                               StructureEvaluationStage::Verified,
                               callbacks, &cache);
            ++diag.evaluatedCandidates;
            ++completed;
            if (callbacks.onProgress) {
                StructureProgress p;
                p.stage = "FinalVerified";
                p.completed = completed;
                p.planned = finalVerificationCount;
                callbacks.onProgress(p);
            }
        }
    }

    // ── 7.  Sort, find a verified best candidate, and analyze sensitivity ─
    StructureObjectiveScorer::sortForDecision(result.candidates);
    for (const auto& c : result.candidates)
    {
        if (c.feasible && c.stage == StructureEvaluationStage::Verified)
        {
            result.bestCandidateIndex = c.index;
            break;
        }
    }

    if (!result.canceled && result.bestCandidateIndex >= 0) {
        const StructureCandidateResult* bestCandidate =
            findCandidateByIndex(result.candidates, result.bestCandidateIndex);
        if (bestCandidate != nullptr) {
            result.sensitivity = StructureSensitivityAnalyzer().analyze(
                problem, *bestCandidate, evaluator, callbacks, &cache);
            if (callbacks.isCancellationRequested &&
                callbacks.isCancellationRequested()) {
                result.canceled = true;
            }
        }
    } else if (result.bestCandidateIndex < 0) {
        AnalysisWarning warning;
        warning.code = "StructureOptimization.NoFeasibleCandidate";
        warning.message = "No feasible verified candidate is available for sensitivity analysis.";
        warning.source = "HybridStructureOptimizer";
        warning.severity = AnalysisStatus::Warning;
        result.warnings.push_back(warning);
    }

    // ── 8.  Diagnostics ─────────────────────────────────────────────────
    auto tEnd = std::chrono::steady_clock::now();
    diag.totalSeconds = std::chrono::duration<double>(tEnd - tStart).count();

    for (const auto& c : result.candidates)
    {
        diag.modelBuildSeconds          += c.raw.modelBuildSeconds;
        diag.kinematicEvaluationSeconds += c.raw.kinematicEvaluationSeconds;
        diag.workspaceEvaluationSeconds += c.raw.workspaceEvaluationSeconds;
    }

    // Count cache hits
    diag.cacheHits = static_cast<int>(cache.hitCount());

    result.diagnostics = diag;
    result.completedAt = currentTimestamp();

    return result;
}

} // namespace rws
