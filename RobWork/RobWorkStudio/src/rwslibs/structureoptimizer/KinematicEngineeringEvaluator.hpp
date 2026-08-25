#ifndef RWS_STRUCTUREOPTIMIZATION_KINEMATICENGINEERINGEVALUATOR_HPP
#define RWS_STRUCTUREOPTIMIZATION_KINEMATICENGINEERINGEVALUATOR_HPP

#include "StructureCandidateCache.hpp"
#include "StructureOptimizationStrategy.hpp"

#include <rwslibs/robotanalysiscore/IEngineeringEvaluator.hpp>

namespace rws {

/**
 * @brief 运动学工程评估器类。
 * 
 * 继承自 IEngineeringEvaluator 接口，是结构优化插件中最核心的物理评估算子。
 * 负责对突变后的机器人 3D 构件执行全套运动学分析，包含：
 *  1. 逆运动学 (IK) 可求解性与可达性 (Task Reachability)；
 *  2. 关节角度超限与奇点避让；
 *  3. 基于雅可比矩阵 J(q) 的可操作度 (Manipulability) 评估；
 *  4. 基于 CollisionDetector 的自碰撞与环境干涉检查；
 *  5. 工作空间包围盒体素覆盖率 (StructureWorkspaceCoverage)。
 */
class KinematicEngineeringEvaluator : public IEngineeringEvaluator
{
  public:
    /**
     * @brief 构造函数，绑定当前优化问题的定义。
     * @param problem 结构优化问题引用（包含目标任务点、工作空间覆盖盒及评价权重等）
     */
    explicit KinematicEngineeringEvaluator(const StructureOptimizationProblem& problem);

    /**
     * @brief 获取评估器的唯一 ID 标识符。
     * @return std::string 固定返回 "KinematicEngineeringEvaluator"
     */
    std::string id() const override;

    /**
     * @brief 获取当前运动学算法的版本号。
     * @return std::string 返回算法版本号（如 "1.0.0"），用于参与哈希缓存 Key 生成
     */
    std::string version() const override;

    /**
     * @brief 获取评估完成后能够向管线产出的新工件 (Artifact) ID 列表。
     * @return std::vector<std::string> 包含 "KinematicEvaluationArtifact" 等运动学工件标识
     */
    std::vector<std::string> providedArtifactIds() const override;

    /**
     * @brief 系统工程管线标准评估入口。
     * 
     * 内部流程：
     *  1. 从 candidate 中提取候选解 3D 构件 (Device / CollisionDetector / State)；
     *  2. 结合 request 指定的精度阶段 (Quick / Verified) 设置采样网格密度；
     *  3. 检查 callbacks 中途取消标志；
     *  4. 求解 IK、碰撞、灵敏度与覆盖率，将指标打包填入 EngineeringEvaluationResult 返回。
     * 
     * @param candidate 候选解评估上下文（包含突变生成的 3D 构件）
     * @param request 评估请求（包含评估精度阶段 Quick/Verified 及输入工件）
     * @param callbacks 线程控制回调接口（用于检查取消与进度汇报）
     * @return EngineeringEvaluationResult 包含可达性、可操作度、碰撞率等指标的综合评估报告
     */
    EngineeringEvaluationResult evaluate(const CandidateEvaluationContext& candidate,
                                         const EvaluationRequest& request,
                                         const EvaluationCallbacks& callbacks) override;

    /**
     * @brief 统一的候选解评估入口。
     *
     * 直接对 StructureCandidateResult 结构体进行原地修改与赋值，支持传入 StructureCandidateCache 哈希缓存以提速。
     * 模型构建、IK、碰撞与指标汇总全部由本类完成；IEngineeringEvaluator::evaluate 只负责把结果映射为公共工程结果。
     *
     * @param candidate [in, out] 待评估与更新的候选解结果结构体
     * @param stage 当前评估阶段 (Quick 粗筛 / Verified 精复核)
     * @param callbacks 线程控制与进度回调接口
     * @param cache 可选的候选解哈希缓存指针，若命中直接复用历史指标
     */
    void evaluateCandidate(StructureCandidateResult& candidate,
                           StructureEvaluationStage stage,
                           const StructureOptimizationCallbacks& callbacks,
                           StructureCandidateCache* cache = nullptr);

    /**
     * @brief 兼容转发：旧入口名，等价于 evaluateCandidate。
     *
     * 仅保留给唯一的历史兼容性测试使用；生产与新测试代码一律调用 evaluateCandidate。
     */
    void evaluateLegacy(StructureCandidateResult& candidate,
                        StructureEvaluationStage stage,
                        const StructureOptimizationCallbacks& callbacks,
                        StructureCandidateCache* cache = nullptr);

  private:
    const StructureOptimizationProblem& _problem; //!< 优化的全局问题定义只读引用
};

} // namespace rws

#endif // RWS_STRUCTUREOPTIMIZATION_KINEMATICENGINEERINGEVALUATOR_HPP