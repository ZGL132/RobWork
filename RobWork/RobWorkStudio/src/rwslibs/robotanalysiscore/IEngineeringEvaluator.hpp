#ifndef RWS_ROBOTANALYSISCORE_IENGINEERINGEVALUATOR_HPP
#define RWS_ROBOTANALYSISCORE_IENGINEERINGEVALUATOR_HPP

#include "EngineeringEvaluationTypes.hpp"

#include <vector>

namespace rws {

/**
 * @brief 单项工程评估器抽象接口（专业质检工位契约）。
 * 
 * 采用策略模式（Strategy Pattern）与插件化设计。
 * 所有具体的工程评估算子（如运动学可操作度评估器、碰撞检测评估器、结构刚度评估器等）
 * 都必须继承并实现该接口，才能被注册进 EngineeringEvaluatorPipeline 流水线中进行统一调度。
 */
class IEngineeringEvaluator
{
public:
    /**
     * @brief 虚析构函数，确保派生类对象能被安全析构。
     */
    virtual ~IEngineeringEvaluator() = default;

    /**
     * @brief 获取当前评估器的唯一标识符字符串（工位 ID）。
     * 
     * @return std::string 评估器唯一名称（如 "KinematicEngineeringEvaluator"、"CollisionEvaluator"）
     */
    virtual std::string id() const = 0;

    /**
     * @brief 获取当前评估器算法的版本号字符串。
     * 
     * 版本号将参与生成哈希缓存 Key（StructureCandidateCache）。
     * 当评估器内部算法升级版本改变时，会自动使旧版本的评估结果缓存失效。
     * 
     * @return std::string 版本号字符串（如 "1.0.0"）
     */
    virtual std::string version() const = 0;

    /**
     * @brief 获取当前评估器正常运行所依赖的前置工件（Artifact）ID 列表。
     * 
     * 流水线（EngineeringEvaluatorPipeline）在调度时，会根据此列表检查上游工位
     * 是否已产出了所有必需的中间结果。若未就绪则暂不触发当前评估器。
     * 默认返回空列表（表示无前置依赖，可直接作为源头工位执行）。
     * 
     * @return std::vector<std::string> 依赖的工件 ID 字符串集合
     */
    virtual std::vector<std::string> requiredArtifactIds() const { return {}; }

    /**
     * @brief 获取当前评估器计算完成后能够产出的新工件（Artifact）ID 列表。
     * 
     * 告知流水线当前评估器运行完毕后，可以向全局工件库补充哪些中间计算产物，
     * 以便解锁下游依赖这些工件的其他评估器。
     * 默认返回空列表（表示不产出供下游使用的中间工件）。
     * 
     * @return std::vector<std::string> 能够产出的工件 ID 字符串集合
     */
    virtual std::vector<std::string> providedArtifactIds() const { return {}; }

    /**
     * @brief 评估器核心计算入口：对给定的候选解执行单项物理/工程评估。
     * 
     * 派生类重写此函数实现具体的算法逻辑：
     *  1. 从 candidate 中提取机器人 3D 构件 (WorkCell / Device / State)；
     *  2. 从 request.inputArtifacts 中提取上游工位传递过来的中间工件；
     *  3. 定期调用 callbacks.isCancellationRequested() 支持快速响应中途取消；
     *  4. 执行运动学、碰撞或物理计算；
     *  5. 将计算出的原始物理指标 (Raw Metrics)、违背的约束及新产出的工件打包填入 EngineeringEvaluationResult 返回。
     * 
     * @param candidate 当前候选解的上下文环境（包含突变后的机器人 3D 模型构件）
     * @param request 评估请求参数（包含当前的评估精度阶段 Quick/Verified 及已就绪的工件库）
     * @param callbacks 控制回调接口（包含取消检查、暂停等待及进度汇报功能）
     * @return EngineeringEvaluationResult 包含了该评估器单项评估指标与产出工件的结果结构体
     */
    virtual EngineeringEvaluationResult evaluate(
        const CandidateEvaluationContext& candidate,
        const EvaluationRequest& request,
        const EvaluationCallbacks& callbacks) = 0;
};

} // namespace rws

#endif // RWS_ROBOTANALYSISCORE_IENGINEERINGEVALUATOR_HPP