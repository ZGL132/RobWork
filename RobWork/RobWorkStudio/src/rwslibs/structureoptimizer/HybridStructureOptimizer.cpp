#include "HybridStructureOptimizer.hpp"
#include "StructureCandidateGenerator.hpp"
#include "StructureObjectiveScorer.hpp"
#include "StructureOptimizationStrategy.hpp"
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
//  匿名命名空间内部辅助函数
// =============================================================================
namespace {

// ---------------------------------------------------------------------------
//  生成当前的 ISO 8601 时间戳字符串 (如 "2026-08-09T23:45:00")
// ---------------------------------------------------------------------------
std::string currentTimestamp()
{
    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    char buf[32];
    std::tm utcTime{};
#ifdef _WIN32
    gmtime_s(&utcTime, &t);
#else
    gmtime_r(&t, &utcTime);
#endif
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &utcTime);
    return std::string(buf);
}

// ---------------------------------------------------------------------------
//  在给定的中心解 (centre) 周围施加微小随机扰动，生成局部搜索样本池
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
            // 在变量上下界的 15% 极值邻域范围内施加随机偏移
            double range = variables[j].maximum - variables[j].minimum;
            double local = range * 0.15;   // 15 % 邻域半径
            double offset = unitOffset(rng) * local;
            double val = centre[j] + offset;
            // 严格截断钳位在 [minimum, maximum] 范围内
            val = std::max(variables[j].minimum, std::min(variables[j].maximum, val));
            vals.push_back(val);
        }
        result.push_back(vals);
    }
    return result;
}

// ---------------------------------------------------------------------------
//  从问题变量定义中提取当前的基线初始数值向量
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

// ---------------------------------------------------------------------------
//  计算两个解在多维参数空间中的归一化欧氏距离
// ---------------------------------------------------------------------------
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
        // 将各轴偏差按变量取值范围归一化
        const double delta = (first[i] - second[i]) / range;
        squaredDistance += delta * delta;
    }
    return std::sqrt(squaredDistance);
}

// ---------------------------------------------------------------------------
//  兼顾“可行性”、“得分”与“设计空间分散性”的精英解贪心选择算法
// ---------------------------------------------------------------------------
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
        // 第一个精英解直接选取当前综合排序最高的方案
        if (selected.empty()) {
            selected.push_back(candidate.index);
            continue;
        }

        const StructureCandidateResult* best = nullptr;
        double bestPriority = -1.0;
        
        // 贪心搜索：找出一个与已选精英解距离最远且得分最高的新精英解
        for (const StructureCandidateResult& option : candidates) {
            if (std::find(selected.begin(), selected.end(), option.index) != selected.end())
                continue;
            
            // 计算当前备选解到所有已选精英解的最短归一化距离
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
            
            // 综合优先级评分 = 可行性权重 (1000) + 综合得分 + 空间距离加权 (minDistance * 5)
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

// ---------------------------------------------------------------------------
//  辅助函数：按候选解索引在结果列表中查找指针
// ---------------------------------------------------------------------------
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

double bestFeasibleScore(const std::vector<StructureCandidateResult>& candidates)
{
    double best = 0.0;
    for (const StructureCandidateResult& candidate : candidates) {
        if (candidate.feasible && std::isfinite(candidate.totalScore))
            best = std::max(best, candidate.totalScore);
    }
    return best;
}

} // 匿名命名空间

// ===========================================================================
//  HybridStructureOptimizer 优化主算法实现
// ===========================================================================
StructureOptimizationResult HybridStructureOptimizer::optimize(
    const StructureOptimizationProblem& problem,
    IStructureCandidateEvaluator& evaluator,
    const StructureOptimizationCallbacks& callbacks)
{
    auto tStart = std::chrono::steady_clock::now(); // 启动计时

    // ── 步骤 1. 初始化结果与性能诊断统计结构 ─────────────────────────────
    StructureOptimizationResult result;
    result.startedAt = currentTimestamp();

    StructureCandidateCache   cache; // 实例化局部哈希缓存
    StructureRunDiagnostics   diag{};

    // ── 步骤 2. 评估基线 Baseline 原始模型 (索引 0，强制 Verified 精评) ───────
    std::vector<double> baseValues = gatherCurrentValues(problem.variables);

    {
        StructureCandidateResult baseline;
        baseline.index  = 0;
        baseline.values = baseValues;
        // 评估基线模型在当前约束下的实际性能，作为后续评分的对比基准
        evaluator.evaluate(problem, baseline,
                           StructureEvaluationStage::Verified,
                           callbacks, &cache);
        ++diag.evaluatedCandidates;
        result.candidates.push_back(std::move(baseline));
    }
    result.baselineCandidateIndex = 0;

    // ── 步骤 3. 根据设定的策略生成初始候选解参数池 ─────────────────────────
    std::vector<std::vector<double>> candidatePool;

    switch (problem.run.strategy)
    {
    case StructureStrategyKind::Random:
        // 纯均匀随机采样
        candidatePool = StructureCandidateGenerator::randomUniform(
            problem.variables, problem.run.candidateCount,
            problem.run.randomSeed);
        break;

    case StructureStrategyKind::Grid:
        // 全网格遍历采样
        candidatePool = StructureCandidateGenerator::grid(
            problem.variables, problem.run.gridSteps,
            problem.run.candidateCount);
        break;

    case StructureStrategyKind::Hybrid:
    default:
        // 拉丁超立方采样 (LHS, 高维空间均匀分层抽样)
        candidatePool = StructureCandidateGenerator::latinHypercube(
            problem.variables, problem.run.candidateCount,
            problem.run.randomSeed);
        break;
    }

    diag.generatedCandidates = 1 + static_cast<int>(candidatePool.size());

    // ── 步骤 4. 对所有采样候选解执行 Quick 阶段低精度快速粗筛 ───────────────
    {
        int completed = 0;
        for (std::size_t i = 0; i < candidatePool.size(); ++i)
        {
            // 响应 UI 线程的取消或暂停请求
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
            
            // 执行 Quick 阶段粗评 (低网格密度、快速 IK/碰撞筛选)
            evaluator.evaluate(problem, cr, StructureEvaluationStage::Quick,
                               callbacks, &cache);
            ++diag.evaluatedCandidates;
            ++diag.quickEvaluatedCandidates;
            result.candidates.push_back(std::move(cr));
            ++completed;

            // 定期向 UI 抛出进度和当前最高得分
            if (callbacks.onProgress)
            {
                StructureProgress p;
                p.stage     = "Quick";
                p.completed = completed;
                p.planned   = static_cast<int>(candidatePool.size());
                p.bestScore = bestFeasibleScore(result.candidates);
                callbacks.onProgress(p);
            }
        }
    }

    // ── 步骤 5. 混合策略专属：精英解 Verified 精评 + 局部搜索 ─────────────────
    if (problem.run.strategy == StructureStrategyKind::Hybrid &&
        !result.canceled)
    {
        // 5a. 对 Quick 粗评结果进行决策排序
        StructureObjectiveScorer::sortForDecision(result.candidates);

        // 挑出兼顾可行性、得分与空间分布分散性的精英解
        int eliteCount = std::min(problem.run.eliteCount,
                                  static_cast<int>(result.candidates.size()));
        std::vector<int> eliteIndices = selectDiverseEliteIndices(
            result.candidates, problem.variables, eliteCount);

        // 5b. 对选出的精英解升级到 Verified 阶段执行高精度全项复核
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

                // 重新以 Verified 阶段评估 (开启高密度工作空间采样及全网格碰撞检测)
                evaluator.evaluate(problem, *elitePtr,
                                   StructureEvaluationStage::Verified,
                                   callbacks, &cache);
                ++diag.evaluatedCandidates;
                ++diag.verifiedEliteCandidates;
                ++completed;

                if (callbacks.onProgress)
                {
                    StructureProgress p;
                    p.stage     = "Verified";
                    p.completed = completed;
                    p.planned   = eliteCount;
                    p.bestScore = bestFeasibleScore(result.candidates);
                    callbacks.onProgress(p);
                }
            }
        }

        // 筛选出高精度验证后依然可行的精英解，作为局部搜索的中心点
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

        // 5c. 在可行精英解周围开展局部爬山搜索 (Local Sweeps)
        if (!result.canceled && problem.run.maxLocalSweeps > 0 &&
            !localEliteIndices.empty())
        {
            int localPerElite = std::max(1,
                problem.run.maxLocalSweeps / localEliteCount);

            std::mt19937 localRng(problem.run.randomSeed + 9999u);

            std::vector<std::vector<double>> localPool;
            localPool.reserve(static_cast<std::size_t>(
                localPerElite * localEliteCount));

            // 对每个精英解生成局部微调采样点
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

            // 对局部搜索生成的微调解进行 Quick 阶段快速评估
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
                ++diag.quickEvaluatedCandidates;
                result.candidates.push_back(std::move(cr));
                ++completed;

                if (callbacks.onProgress)
                {
                    StructureProgress p;
                    p.stage     = "Local";
                    p.completed = completed;
                    p.planned   = static_cast<int>(localPool.size());
                    p.bestScore = bestFeasibleScore(result.candidates);
                    callbacks.onProgress(p);
                }
            }
        }
    }

    // ── 步骤 6. 最终复核：对领跑候选解执行最终 Verified 阶段确认 ─────────────
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

            // 确保前几名领跑解均经过最高精度阶段的重新确认
            evaluator.evaluate(problem, result.candidates[static_cast<std::size_t>(i)],
                               StructureEvaluationStage::Verified,
                               callbacks, &cache);
            ++diag.evaluatedCandidates;
            ++diag.finalVerifiedCandidates;
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

    // ── 步骤 7. 决策排序、选拔最佳方案并计算参数灵敏度 ──────────────────────
    StructureObjectiveScorer::sortForDecision(result.candidates);
    
    // 找出同时满足“通过所有硬约束 (feasible==true)”且“处于 Verified 验证阶段”的最佳冠军方案
    for (const auto& c : result.candidates)
    {
        if (c.feasible && c.stage == StructureEvaluationStage::Verified)
        {
            result.bestCandidateIndex = c.index;
            break;
        }
    }

    // 对选出的冠军方案触发灵敏度分析 (微调变量评估得分降幅与鲁棒等级)
    if (!result.canceled && result.bestCandidateIndex >= 0) {
        const StructureCandidateResult* bestCandidate =
            findCandidateByIndex(result.candidates, result.bestCandidateIndex);
        if (bestCandidate != nullptr) {
            result.sensitivity = StructureSensitivityAnalyzer().analyze(
                problem, *bestCandidate, evaluator, callbacks, &cache);
            diag.sensitivityEvaluations =
                static_cast<int>(result.sensitivity.entries.size());
            diag.evaluatedCandidates += diag.sensitivityEvaluations;
            if (callbacks.isCancellationRequested &&
                callbacks.isCancellationRequested()) {
                result.canceled = true;
            }
        }
    } else if (result.bestCandidateIndex < 0) {
        // 若没有找到可行解，抛出 NoFeasibleCandidate 警告信息
        AnalysisWarning warning;
        warning.code = "StructureOptimization.NoFeasibleCandidate";
        warning.message = "No feasible verified candidate is available for sensitivity analysis.";
        warning.source = "HybridStructureOptimizer";
        warning.severity = AnalysisStatus::Warning;
        result.warnings.push_back(warning);
    }

    // ── 步骤 8. 性能诊断数据汇总与收尾 ──────────────────────────────────
    auto tEnd = std::chrono::steady_clock::now();
    diag.totalSeconds = std::chrono::duration<double>(tEnd - tStart).count();

    // 累加所有解的编译、运动学及工作空间评估耗时
    for (const auto& c : result.candidates)
    {
        diag.modelBuildSeconds          += c.raw.modelBuildSeconds;
        diag.kinematicEvaluationSeconds += c.raw.kinematicEvaluationSeconds;
        diag.workspaceEvaluationSeconds += c.raw.workspaceEvaluationSeconds;
    }

    // 统计全局哈希缓存击中次数
    diag.cacheHits = static_cast<int>(cache.hitCount());

    result.diagnostics = diag;
    result.completedAt = currentTimestamp();

    return result; // 返回完整的结构优化结果对象
}

} // namespace rws
