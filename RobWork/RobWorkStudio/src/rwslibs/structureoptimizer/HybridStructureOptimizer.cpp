#include "HybridStructureOptimizer.hpp"
#include "StructureCandidateGenerator.hpp"
#include "StructureObjectiveScorer.hpp"
#include "StructureCandidateEvaluator.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
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
    int count)
{
    std::vector<std::vector<double>> result;
    result.reserve(static_cast<std::size_t>(count));

    for (int i = 0; i < count; ++i)
    {
        std::vector<double> vals;
        vals.reserve(variables.size());

        for (std::size_t j = 0; j < variables.size(); ++j)
        {
            double range = variables[j].maximum - variables[j].minimum;
            double local = range * 0.15;   // 15 % neighbourhood
            double offset = (static_cast<double>(std::rand()) / RAND_MAX * 2.0 - 1.0) * local;
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

        // Collect elite indices (deduplicated, within original candidate range)
        std::vector<int> eliteIndices;
        int eliteCount = std::min(problem.run.eliteCount,
                                  static_cast<int>(result.candidates.size()));
        for (int i = 0; i < eliteCount; ++i)
            eliteIndices.push_back(result.candidates[i].index);

        // 5b.  Verified-evaluate elites
        {
            int completed = 0;
            for (int ei : eliteIndices)
            {
                if (callbacks.isCancellationRequested())
                {
                    result.canceled = true;
                    break;
                }
                if (callbacks.waitIfPaused)
                    callbacks.waitIfPaused();

                // Find the candidate by index
                StructureCandidateResult* elitePtr = nullptr;
                for (auto& c : result.candidates)
                {
                    if (c.index == ei)
                    {
                        elitePtr = &c;
                        break;
                    }
                }
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

        // 5c.  Local search around elites
        if (!result.canceled && !eliteIndices.empty())
        {
            int localPerElite = std::max(1,
                problem.run.maxLocalSweeps / eliteCount);

            std::srand(problem.run.randomSeed + 9999);

            std::vector<std::vector<double>> localPool;
            localPool.reserve(static_cast<std::size_t>(
                localPerElite * eliteCount));

            for (int ei : eliteIndices)
            {
                const StructureCandidateResult* elitePtr = nullptr;
                for (const auto& c : result.candidates)
                {
                    if (c.index == ei)
                    {
                        elitePtr = &c;
                        break;
                    }
                }
                if (!elitePtr)
                    continue;

                auto perturbed = generateLocalPerturbations(
                    problem.variables, elitePtr->values, localPerElite);
                localPool.insert(localPool.end(),
                                 std::make_move_iterator(perturbed.begin()),
                                 std::make_move_iterator(perturbed.end()));
            }

            diag.generatedCandidates += static_cast<int>(localPool.size());

            int completed = 0;
            for (std::size_t i = 0; i < localPool.size(); ++i)
            {
                if (callbacks.isCancellationRequested())
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

    // ── 6.  Sort and find best ──────────────────────────────────────────
    StructureObjectiveScorer::sortForDecision(result.candidates);

    for (const auto& c : result.candidates)
    {
        if (c.feasible)
        {
            result.bestCandidateIndex = c.index;
            break;
        }
    }

    // ── 7.  Diagnostics ─────────────────────────────────────────────────
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
