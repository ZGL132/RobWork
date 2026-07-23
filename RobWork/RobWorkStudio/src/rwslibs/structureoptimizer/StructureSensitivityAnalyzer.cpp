#include "StructureSensitivityAnalyzer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace rws {

StructureSensitivityResult StructureSensitivityAnalyzer::analyze(
    const StructureOptimizationProblem& problem,
    const StructureCandidateResult& best,
    IStructureCandidateEvaluator& evaluator,
    const StructureOptimizationCallbacks& callbacks,
    StructureCandidateCache* cache)
{
    StructureSensitivityResult result;
    double totalDrop = 0.0;
    int countDrops = 0;

    for (std::size_t i = 0; i < problem.variables.size(); ++i)
    {
        const auto& var = problem.variables[i];
        if (!var.enabled)
            continue;

        const double bestVal = best.values[i];
        const double step = var.step;

        // 收集可用的扰动方向: 如果在最小值只尝试 +step, 如果在最大值只尝试 -step
        std::vector<double> testValues;
        if (bestVal - step >= var.minimum - 1e-12)
            testValues.push_back(bestVal - step);
        if (bestVal + step <= var.maximum + 1e-12)
            testValues.push_back(bestVal + step);

        if (testValues.empty())
            continue;

        double variableWorstDrop = 0.0;
        bool variableEverInfeasible = false;

        for (double testVal : testValues)
        {
            StructureSensitivityEntry entry;
            entry.variableId    = var.id;
            entry.delta         = testVal - bestVal;
            entry.perturbedValue = testVal;

            // 复制最佳候选解并修改当前变量值
            StructureCandidateResult perturbed = best;
            perturbed.values[i] = testVal;
            perturbed.index = -1; // 临时候选解无索引

            // 在 Verified 阶段重新评估
            evaluator.evaluate(problem, perturbed,
                               StructureEvaluationStage::Verified,
                               callbacks, cache);

            if (!perturbed.feasible)
            {
                entry.scoreDrop = 100.0;
                entry.feasible = false;
                entry.violatedConstraints = perturbed.violatedConstraints;
                variableEverInfeasible = true;
            }
            else
            {
                entry.scoreDrop = best.totalScore - perturbed.totalScore;
                if (entry.scoreDrop < 0.0)
                    entry.scoreDrop = 0.0; // 不可能出现负下降
                entry.feasible = true;
            }

            result.entries.push_back(entry);

            if (entry.scoreDrop > variableWorstDrop)
                variableWorstDrop = entry.scoreDrop;
        }

        totalDrop += variableWorstDrop;
        ++countDrops;

        // 判定是否为关键变量: scoreDrop > 10 或扰动后不可行
        if (variableWorstDrop > 10.0 || variableEverInfeasible)
            result.criticalVariableIds.push_back(var.id);
    }

    // ── 统计量 ──────────────────────────────────────────────────────────────
    result.maximumScoreDrop = 0.0;
    result.meanScoreDrop = 0.0;

    if (!result.entries.empty())
    {
        result.maximumScoreDrop = -std::numeric_limits<double>::infinity();
        for (const auto& e : result.entries)
        {
            if (e.scoreDrop > result.maximumScoreDrop)
                result.maximumScoreDrop = e.scoreDrop;
        }
    }

    if (countDrops > 0)
        result.meanScoreDrop = totalDrop / static_cast<double>(countDrops);

    // ── 鲁棒性等级 ──────────────────────────────────────────────────────────
    if (result.maximumScoreDrop <= 2.0)
        result.robustnessGrade = "A";
    else if (result.maximumScoreDrop <= 5.0)
        result.robustnessGrade = "B";
    else if (result.maximumScoreDrop <= 10.0)
        result.robustnessGrade = "C";
    else
        result.robustnessGrade = "D";

    return result;
}

} // namespace rws
