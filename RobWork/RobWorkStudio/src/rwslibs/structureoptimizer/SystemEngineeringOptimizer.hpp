#ifndef RWS_STRUCTUREOPTIMIZATION_SYSTEMENGINEERINGOPTIMIZER_HPP
#define RWS_STRUCTUREOPTIMIZATION_SYSTEMENGINEERINGOPTIMIZER_HPP

#include "EngineeringEvaluatorPipeline.hpp"
#include "StructureOptimizationStrategy.hpp"

namespace rws {

/**
 * @brief 系统工程优化器类。
 *
 * 该类作为系统工程评价管线（EngineeringEvaluatorPipeline）与结构优化策略（如 HybridStructureOptimizer）之间的适配器。
 * 负责将多评估器组合的系统工程评价 Pipeline 包装为优化算法所需的候选解评估接口（IStructureCandidateEvaluator），
 * 驱动主优化策略在复杂工程依赖管线下完成多目标寻优与硬约束判别。
 */
class SystemEngineeringOptimizer
{
  public:
    /**
     * @brief 执行系统工程框架下的结构优化计算。
     *
     * 内部构建基于 Pipeline 的候选解评估器（PipelineCandidateEvaluator），
     * 随后调用底层的混合结构优化策略（HybridStructureOptimizer）开展多阶段寻优（粗筛、精评、局部搜索与灵敏度分析），
     * 将工程评价管线产出的指标、约束违背及工件汇总映射回结构优化结果对象中。
     *
     * @param problem 结构优化问题的完整定义（包含上下文、变量、任务点、约束和权重等）
     * @param pipeline 已配置好的系统工程评价管线（包含各类已注册的工程评估器）
     * @param callbacks 用于取消检查、暂停等待及进度汇报的控制回调接口
     * @return StructureOptimizationResult 打包好的结构优化最终运行结果
     */
    StructureOptimizationResult optimize(const StructureOptimizationProblem& problem,
                                         EngineeringEvaluatorPipeline& pipeline,
                                         const StructureOptimizationCallbacks& callbacks) const;
};

} // namespace rws

#endif // RWS_STRUCTUREOPTIMIZATION_SYSTEMENGINEERINGOPTIMIZER_HPP