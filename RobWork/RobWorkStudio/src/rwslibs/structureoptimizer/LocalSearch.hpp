#ifndef RWS_STRUCTUREOPTIMIZATION_LOCALSEARCH_HPP
#define RWS_STRUCTUREOPTIMIZATION_LOCALSEARCH_HPP

#include "CandidateResult.hpp"
#include "DesignVariable.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace rws {

/**
 * @brief 局部搜索遇到归一化边界时的处理方式。
 *
 * 局部搜索的内部坐标统一为 [0, 1]。这里的策略只作用于扰动后的归一化
 * 坐标，绝不直接对工程量 x 做截断，从而避免把米、弧度、整数步长混在一
 * 起比较。Clamp 会把越界点压回边界，Reflect 会镜像回合法区间，Skip
 * 则直接丢弃该邻居。
 */
enum class LocalSearchBoundaryHandling
{
    Clamp,
    Reflect,
    Skip
};

/**
 * @brief 局部搜索需要的独立变量投影。
 *
 * LocalSearch 不读取完整 CompiledDesignSpace，而只接收本轮已经筛选好的
 * 独立变量。Derived 变量即使误传入，也会被明确跳过，保证局部算法不会
 * 直接扰动派生量并破坏设计空间依赖关系。minimum/maximum/step 仍保存为
 * 工程单位，用于把整数步长正确换算成 normalized 步长。
 */
struct LocalSearchVariable
{
    std::string id;
    VariableRole role = VariableRole::Independent;
    VariableDomain domain = VariableDomain::Continuous;
    double minimum = 0.0;
    double maximum = 1.0;
    double step = 0.0;
    std::size_t discreteOptionCount = 0;
    bool enabled = true;
    std::string groupId;
};

/** 局部精修的冻结配置，配置内容应参与上层运行指纹。 */
struct LocalSearchConfig
{
    std::size_t maxSweeps = 0;       ///< 最大扫掠轮数；0 表示不执行局部搜索
    std::size_t maxEvaluations = 0;  ///< 最大邻域评估次数；0 表示不设上限
    double improvementTolerance = 0.0; ///< 必须超过该容差才接受新中心
    LocalSearchBoundaryHandling boundary = LocalSearchBoundaryHandling::Clamp;
    /** 每个变量的 normalized 半径；缺省时连续变量使用其工程 step 换算值。 */
    std::vector<double> radii;
    /** 每个分组是一组变量下标；组扰动会同时沿同一方向移动这些变量。 */
    std::vector<std::vector<std::size_t>> groups;
};

/** 已通过 Verified 的局部搜索中心。 */
struct LocalSearchCenter
{
    std::size_t stableIndex = 0;
    std::vector<double> normalizedDesign;
    CandidateResult result;
};

/** 一次实际评估过的局部邻居，包含其生成轮次以便审计。 */
struct LocalSearchEvaluation
{
    std::vector<double> normalizedDesign;
    CandidateResult result;
    std::size_t sweep = 0;
};

struct LocalSearchCallbacks
{
    std::function<bool()> isCancellationRequested;
};

struct LocalSearchResult
{
    LocalSearchCenter best;
    std::vector<LocalSearchEvaluation> evaluations;
    std::size_t evaluatedCount = 0;
    std::size_t sweeps = 0;
    bool canceled = false;
};

using LocalSearchEvaluationCallback =
    std::function<CandidateResult(const std::vector<double>&, AnalysisEvidenceStage)>;

/**
 * @brief 对一个 Verified 可行中心执行确定性的邻域爬山搜索。
 *
 * 每轮按“单变量正向、单变量负向、变量组正向、变量组负向”的稳定顺序
 * 生成邻居。所有邻居都必须经过回调重新执行 Verified 评估；局部搜索只
 * 负责候选生成和中心接受，不复制 CandidateCompiler 或评估器逻辑。
 */
class LocalSearch
{
  public:
    static LocalSearchResult run(const std::vector<LocalSearchVariable>& variables,
                                 const LocalSearchCenter& center,
                                 const LocalSearchConfig& config,
                                 const LocalSearchEvaluationCallback& evaluate,
                                 const LocalSearchCallbacks& callbacks = {});
};

} // namespace rws

#endif
