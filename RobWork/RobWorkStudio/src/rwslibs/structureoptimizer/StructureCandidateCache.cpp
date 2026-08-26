#include "StructureCandidateCache.hpp"

#include "StructureOptimizationJson.hpp"

#include <rwslibs/robotanalysiscore/RobotAnalysisJson.hpp>
#include <rwslibs/robotmodelbuilder/RobotModelSpecJson.hpp>

#include <cmath>
#include <cstddef>
#include <cstring>
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

// 缓存键按位置量化启用变量，要求 values 与 variables 一一对应。数量不一致的
// 调用（Controller 可被直接调用）不可缓存：查找视为未命中、写入直接跳过，
// 绝不越界读取 values。
bool valuesAlignedWithVariables(const StructureOptimizationProblem& problem,
                                const std::vector<double>& values)
{
    return values.size() == problem.variables.size();
}

} // anonymous namespace

// ============================================================================
//  Key::operator<
// ============================================================================
bool StructureCandidateCache::Key::operator<(const Key& rhs) const
{
    if (quantizedValues != rhs.quantizedValues)
        return quantizedValues < rhs.quantizedValues;
    if (modelHash != rhs.modelHash)
        return modelHash < rhs.modelHash;
    if (taskEnvironmentHash != rhs.taskEnvironmentHash)
        return taskEnvironmentHash < rhs.taskEnvironmentHash;
    if (evaluatorHash != rhs.evaluatorHash)
        return evaluatorHash < rhs.evaluatorHash;
    if (configurationHash != rhs.configurationHash)
        return configurationHash < rhs.configurationHash;
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
            long long qv = 0;
            if (variables[i].step > 0.0)
            {
                const double diff = values[i] - variables[i].minimum;
                qv = static_cast<long long>(std::llround(diff / variables[i].step));
            }
            else
            {
                // 纵深防御：step 非法时不得除零（llround(Inf) 为 UB，且会让
                // 不同取值塌缩到同一缓存键）。退化为原始 double 位型键，
                // 保证不同数值不会碰撞；上层校验会另行拦截该配置。
                static_assert(sizeof(long long) == sizeof(double),
                              "bit-cast requires equal width");
                long long bits = 0;
                std::memcpy(&bits, &values[i], sizeof(bits));
                qv = bits;
            }
            key.quantizedValues.push_back(qv);
        }
    }

    key.modelHash = std::hash<std::string>{}(
        RobotModelSpecJson::toJson(problem.context.modelSpec));
    key.taskEnvironmentHash = std::hash<std::string>{}(
        RobotAnalysisJson::toJson(problem.context));
    key.evaluatorHash = std::hash<std::string>{}(
        problem.evaluation.evaluatorId + "@" + problem.evaluation.evaluatorVersion);

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

    h = hashCombine(h, std::hash<std::string>{}(
        StructureOptimizationJson::problemToJson(problem)));
    key.configurationHash = h;
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
    if (!valuesAlignedWithVariables(problem, values))
        return;
    Key key = makeKey(problem, values, stage);
    _cache[key] = result;
}

bool StructureCandidateCache::find(
    const StructureOptimizationProblem& problem,
    const std::vector<double>& values,
    StructureEvaluationStage stage,
    StructureCandidateResult& result) const
{
    if (!valuesAlignedWithVariables(problem, values))
        return false;
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
