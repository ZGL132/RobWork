#ifndef RWS_STRUCTUREOPTIMIZATION_ENGINEERINGEVALUATORPIPELINE_HPP
#define RWS_STRUCTUREOPTIMIZATION_ENGINEERINGEVALUATORPIPELINE_HPP

#include <rwslibs/robotanalysiscore/IEngineeringEvaluator.hpp>

#include <vector>

namespace rws {

/**
 * @brief 工程评估器流水线类 (系统的综合质检流水线)。
 * 
 * 采用责任链 / 组合模式设计，负责管理并组合多个独立的工程评估器 (IEngineeringEvaluator)。
 * 它将不同维度 (如运动学、碰撞、结构刚度等) 的单项评估算子串联在一起，
 * 为优化算法提供统一的、组合式的全套工程评估接口。
 */
class EngineeringEvaluatorPipeline
{
  public:
    /**
     * @brief 向评估流水线中追加注册一个独立的工程评估器。
     * 
     * 评估器的执行顺序严格取决于它们被添加到流水线中的先后顺序。
     * 
     * @param evaluator 实现 IEngineeringEvaluator 接口的评估器实例引用 (流水线以指针形式引用，不拥有其生命周期)
     */
    void addEvaluator(IEngineeringEvaluator& evaluator);

    /**
     * @brief 驱动流水线对给定的候选解进行全方位的综合工程评估。
     * 
     * 内部流程：
     *  1. 依次遍历 _evaluators 容器中的每个评估器；
     *  2. 针对每个评估器调用其 evaluate(...) 方法；
     *  3. 在每一步检测 callbacks.isCancellationRequested()，支持中途快速响应取消；
     *  4. 自动将各单项评估器产出的物理指标、评分组件、警告列表及过程工件 (Artifacts) 归并汇总；
     *  5. 返回合并后的终态工程评估结果结构体。
     * 
     * @param candidate 待评估候选解的上下文对象 (包含变异后的机器人 3D 构件与参数)
     * @param request 评估请求参数 (如当前处于 Quick 快速阶段还是 Verified 精确阶段)
     * @param callbacks 控制回调接口 (包含取消检查、暂停等待及进度汇报功能)
     * @return EngineeringEvaluationResult 打包好的综合工程评估报告
     */
    EngineeringEvaluationResult evaluate(const CandidateEvaluationContext& candidate,
                                         const EvaluationRequest& request,
                                         const EvaluationCallbacks& callbacks) const;

  private:
    std::vector<IEngineeringEvaluator*> _evaluators; //!< 按注册顺序保存的所有工程评估器接口指针列表
};

} // namespace rws

#endif // RWS_STRUCTUREOPTIMIZATION_ENGINEERINGEVALUATORPIPELINE_HPP
