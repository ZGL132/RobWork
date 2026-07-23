#include "StructureCandidateCache.hpp"

#include <cmath>
#include <cstddef>
#include <functional>
#include <map>
#include <vector>

namespace rws {

namespace {

// Hash combiner (boost::hash_combine style)
std::size_t hashCombine(std::size_t seed, std::size_t value)
{
    return seed ^ (value + 0x9e3779b9 + (seed << 6) + (seed >> 2));
}

} // anonymous namespace

// ============================================================================
//  Key::operator<
// ============================================================================
bool StructureCandidateCache::Key::operator<(const Key& rhs) const
{
    if (quantizedValues != rhs.quantizedValues)
        return quantizedValues < rhs.quantizedValues;
    if (evaluationHash != rhs.evaluationHash)
        return evaluationHash < rhs.evaluationHash;
    return stage < rhs.stage;
}

// ============================================================================
//  makeKey
// ============================================================================
StructureCandidateCache::Key StructureCandidateCache::makeKey(
    const StructureOptimizationProblem& problem,
    const std::vector<double>& values,
    StructureEvaluationStage stage) const
{
    Key key;
    key.stage = stage;

    const auto& variables = problem.variables;
    const auto& ev        = problem.evaluation;

    // Quantized values for enabled variables (in variable order)
    for (std::size_t i = 0; i < variables.size(); ++i)
    {
        if (variables[i].enabled)
        {
            double diff   = values[i] - variables[i].minimum;
            long long qv  = static_cast<long long>(std::llround(diff / variables[i].step));
            key.quantizedValues.push_back(qv);
        }
    }

    // Evaluation hash: combine all config fields that affect evaluation outcome
    std::size_t h = 0;

    // -- thresholds --
    const auto& th = ev.thresholds;
    h = hashCombine(h, std::hash<double>{}(th.nearJointLimitRatio));
    h = hashCombine(h, std::hash<double>{}(th.singularValueWarning));
    h = hashCombine(h, std::hash<double>{}(th.conditionWarning));
    h = hashCombine(h, std::hash<double>{}(th.conditionFail));
    h = hashCombine(h, std::hash<double>{}(th.manipulabilityWarning));
    h = hashCombine(h, std::hash<double>{}(th.positionToleranceMeters));
    h = hashCombine(h, std::hash<double>{}(th.orientationToleranceDeg));
    h = hashCombine(h, std::hash<double>{}(th.ikDuplicateQThreshold));

    // -- quick workspace config --
    const auto& qw = ev.quickWorkspace;
    h = hashCombine(h, static_cast<std::size_t>(qw.mode));
    h = hashCombine(h, static_cast<std::size_t>(qw.sampleCount));
    h = hashCombine(h, static_cast<std::size_t>(qw.gridStepsPerJoint));
    h = hashCombine(h, static_cast<std::size_t>(qw.checkCollision));
    h = hashCombine(h, static_cast<std::size_t>(qw.randomSeed));

    // -- verified workspace config --
    const auto& vw = ev.verifiedWorkspace;
    h = hashCombine(h, static_cast<std::size_t>(vw.mode));
    h = hashCombine(h, static_cast<std::size_t>(vw.sampleCount));
    h = hashCombine(h, static_cast<std::size_t>(vw.gridStepsPerJoint));
    h = hashCombine(h, static_cast<std::size_t>(vw.checkCollision));
    h = hashCombine(h, static_cast<std::size_t>(vw.randomSeed));

    // -- coverage box --
    const auto& cb = ev.coverageBox;
    h = hashCombine(h, static_cast<std::size_t>(cb.enabled));
    for (int i = 0; i < 3; ++i)
    {
        h = hashCombine(h, std::hash<double>{}(cb.minimum[i]));
        h = hashCombine(h, std::hash<double>{}(cb.maximum[i]));
        h = hashCombine(h, static_cast<std::size_t>(cb.cells[i]));
    }

    // -- checkCollision --
    h = hashCombine(h, static_cast<std::size_t>(ev.checkCollision));

    key.evaluationHash = h;
    return key;
}

// ============================================================================
//  put / find / clear
// ============================================================================
void StructureCandidateCache::put(
    const StructureOptimizationProblem& problem,
    const std::vector<double>& values,
    StructureEvaluationStage stage,
    const StructureCandidateResult& result)
{
    Key key = makeKey(problem, values, stage);
    _cache[key] = result;
}

bool StructureCandidateCache::find(
    const StructureOptimizationProblem& problem,
    const std::vector<double>& values,
    StructureEvaluationStage stage,
    StructureCandidateResult& result) const
{
    Key key = makeKey(problem, values, stage);
    auto it = _cache.find(key);
    if (it != _cache.end())
    {
        result = it->second;
        ++_hits;
        return true;
    }
    return false;
}

void StructureCandidateCache::clear()
{
    _cache.clear();
    _hits = 0;
}

} // namespace rws
