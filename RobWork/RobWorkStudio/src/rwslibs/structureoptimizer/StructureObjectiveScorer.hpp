#ifndef RWS_STRUCTUREOPTIMIZATION_STRUCTUREOBJECTIVESCORER_HPP
#define RWS_STRUCTUREOPTIMIZATION_STRUCTUREOBJECTIVESCORER_HPP

#include "StructureOptimizationTypes.hpp"

namespace rws {

/**
 * @brief 结构优化多目标综合打分与决策排序器类。
 * 
 * 负责将候选解计算出的离散物理指标（可达性、可操作度 P10、关节裕度 P10、臂长、无碰撞率等）
 * 映射归一化，并结合用户定义的目标权重（objectives）与约束条件（constraints），
 * 计算出综合打分 (totalScore)，判定可行性 (feasible)，并提供统一的决策排序算子。
 */
class StructureObjectiveScorer {
  public:
    /**
     * @brief 对指定的候选解执行多目标加权打分与硬/软约束审查。
     * 
     * 内部流程：
     *  1. 提取 candidate.raw 中的物理指标（包含调用 percentile10 计算的保守分位数）；
     *  2. 遍历 problem.constraints 检查约束：
     *     - 若违反硬约束 (hard == true)，记录原因至 candidate.violatedConstraints，将 candidate.feasible 置为 false；
     *     - 若违反软约束 (hard == false)，计算罚函数从得分中扣除额外分数；
     *  3. 遍历 problem.objectives，按各自权重加权计算可达性得分、可操作度得分、紧凑度得分及工程偏好得分；
     *  4. 将综合得分 (totalScore) 写回 candidate，若不可行则同步将 status 更新为 Infeasible。
     * 
     * @param problem 优化问题定义（包含约束条件列表、多目标权重配置等）
     * @param candidate [in, out] 待打分的候选解结果对象（输入 raw 指标，写回 score、feasible 及 status）
     */
    void score(const StructureOptimizationProblem& problem,
               StructureCandidateResult& candidate) const;

    /**
     * @brief 计算数据集合的 10% 分位数 (10th Percentile)。
     * 
     * 专门用于可操作度 (Manipulability) 和关节限位裕度 (Joint Margin) 等物理量。
     * 相比平均值 (Mean)，10% 分位数取悲观/保守估计，能极好地暴露机械臂在极少数奇点或极限角度附近
     * 发生的性能退化问题，防止高平均分掩盖极端失控点。
     * 
     * @param values 待计算的数据向量 (如各个任务点处求出的可操作度数组)
     * @return double 10% 分位数数值 (若数组为空则返回 0.0)
     */
    static double percentile10(std::vector<double> values);

    /**
     * @brief 决策优先排序算法：对候选解列表进行原地排序。
     * 
     * 严格遵循双层决策优先级：
     *  1. 可行性优先级：完全满足硬约束的解 (feasible == true) 严格排在不可行解 (feasible == false) 前面；
     *  2. 得分优先级：在可行性相同时，按综合总得分 (totalScore) 从高到低降序排列。
     * 
     * @param candidates [in, out] 待排序的候选解结果向量
     */
    static void sortForDecision(std::vector<StructureCandidateResult>& candidates);
};

} // namespace rws
#endif // RWS_STRUCTUREOPTIMIZATION_STRUCTUREOBJECTIVESCORER_HPP