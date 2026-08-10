#ifndef RWS_STRUCTUREOPTIMIZATION_HYBRIDSTRUCTUREOPTIMIZER_HPP
#define RWS_STRUCTUREOPTIMIZATION_HYBRIDSTRUCTUREOPTIMIZER_HPP

#include "StructureOptimizationStrategy.hpp"
#include "StructureCandidateCache.hpp"

namespace rws {

/**
 * @brief 混合结构优化器类 (系统的默认主寻优算法引擎)。
 * 
 * 继承自 StructureOptimizationStrategy 策略接口，实现了基于“全局分层采样 -> 
 * Quick 阶段低精度粗筛 -> 精英解 Verified 阶段高精度复核 -> 精英邻域局部搜索 (Local Search) -> 
 * 终极复核与灵敏度分析”的多阶段混合算法闭环。
 * 
 * 相比传统的全网格穷举或纯随机寻优，该混合算法能够在极短时间内淘汰非可行解，
 * 并集中计算资源对优质解进行精确提纯与鲁棒性分析，大幅提升计算效率。
 */
class HybridStructureOptimizer : public StructureOptimizationStrategy {
  public:
    /**
     * @brief 执行混合结构优化算法的主入口函数。
     * 
     * 函数内部具体执行闭环：
     *  1. 检查并评估基线（Baseline）原始模型规格；
     *  2. 根据 problem.run.strategy 读取算法策略，调用采样器（如 Latin Hypercube）生成初始候选解池；
     *  3. 遍历候选解池，执行 Quick 阶段低精度快速评估，并实时调用 callbacks 汇报进度与响应取消；
     *  4. 选出得分较高的精英解（Elites），升级到 Verified 阶段执行高精度运动学与碰撞复核；
     *  5. 在精英解周围施加局部随机小扰动，进行多轮局部搜索（Local Search）进一步拉升得分；
     *  6. 选出前排领跑解进行最终 Verified 确认，并触发 StructureSensitivityAnalyzer 对最佳解计算参数灵敏度；
     *  7. 打包填充诊断数据（diagnostics，如耗时、缓存命中数等），返回最终的 StructureOptimizationResult。
     * 
     * @param problem 当前结构优化问题的完整定义（包含设计变量、任务点、约束、运行参数等）
     * @param evaluator 依赖注入的候选解评估器接口指针（如 KinematicEngineeringEvaluator）
     * @param callbacks 用于线程暂停、取消检查及 UI 进度汇报的回调接口组合
     * @return StructureOptimizationResult 包含所有候选解评估数据、最佳解索引及运行诊断报告的结果对象
     */
    StructureOptimizationResult optimize(
        const StructureOptimizationProblem& problem,
        IStructureCandidateEvaluator& evaluator,
        const StructureOptimizationCallbacks& callbacks) override;
};

} // namespace rws
#endif // RWS_STRUCTUREOPTIMIZATION_HYBRIDSTRUCTUREOPTIMIZER_HPP