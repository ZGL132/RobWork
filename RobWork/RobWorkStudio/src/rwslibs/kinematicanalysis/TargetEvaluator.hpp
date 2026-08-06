// =============================================================================
//  TargetEvaluator.hpp —— 任务点评估器(声明)
// =============================================================================
//
// 本组件回答一个核心问题:"给定一个任务点(TaskPoint 目标位姿),机械臂是否能
// 以满足容差的方式到达?"。
//
// 评估流程概要:
//   1. 解析任务点的参考系:优先使用 WorkCell + TaskPointResolver 做完整解析
//      (支持任意参考帧 / TCP 帧),无 WorkCell 时回退到"仅设备 base 帧"的旧式解析;
//   2. 从当前关节值出发,结合关节边界生成多组 IK 种子,用 JacobianIKSolver
//      求出候选解(限制最大数量,并按 IK 去重阈值剔除近似重复解);
//   3. 对每个候选解复用 ConfigurationEvaluator 做完整配置评估(限位/奇异/碰撞),
//      并计算 FK 残差(位置误差、姿态误差)、相对当前 Q 的距离与综合评分;
//   4. 汇总全部候选,判定任务点整体 feasibility / quality。
//
// 输出类型 TargetEvaluation(见 KinematicAnalysisTypes.hpp)包含全部候选解,
// 上层(批量运行器 / 报告 / UI)可自由挑选用于展示的最优解。
#ifndef RWS_KINEMATICANALYSIS_TARGETEVALUATOR_HPP
#define RWS_KINEMATICANALYSIS_TARGETEVALUATOR_HPP

#include "KinematicAnalysisContext.hpp"

namespace rws {

// =============================================================================
//  TargetEvaluationOptions —— 单次任务点评估的选项
// =============================================================================
//
// 控制 TargetEvaluator::evaluate 的求解与判定行为。容差字段是"兜底值":
// 若 TaskPoint 自身未指定容差(tolerance 字段 <= 0),则回退到这里的默认值。
struct TargetEvaluationOptions
{
    // 证据等级,写入评估结果的 stage 字段。
    AnalysisEvidenceStage evidenceStage = AnalysisEvidenceStage::Quick;
    // 是否执行碰撞检测(透传给内部的 ConfigurationEvaluator)。
    bool checkCollision = true;
    // 是否要求"必须无碰撞"(同样透传,见 ConfigurationEvaluationOptions)。
    bool requireCollisionFree = false;
    // 候选解数量上限:超过后停止继续求解,避免高冗余 IK 场景下候选爆炸。
    int maxSolutions = 64;
    // IK 种子数:从不同初始点出发提高收敛到不同解分支的概率。
    int seedCount = 8;
    // 位置容差兜底(米),用于判定目标残差是否可接受。
    double positionToleranceMeters = 0.001;
    // 姿态容差兜底(度),用于判定目标残差是否可接受。
    double orientationToleranceDeg = 1.0;
};

// =============================================================================
//  TargetEvaluator —— 任务点评估器(无状态,可安全复用)
// =============================================================================
//
// 唯一入口 evaluate() 输入"上下文 + 任务点 + 选项",返回完整的 TargetEvaluation。
// 内部持有 ConfigurationEvaluator 与 IK 求解器,但全部按调用栈局部创建,
// 类本身不保存任何状态,因此可被并发 / 嵌套调用。
class TargetEvaluator
{
  public:
    // 评估任务点 target 的可达性与质量。
    // 返回结果中:
    //   - candidates 已按 sortTargetCandidatesForDisplay 排序(最优解在前),
    //     每个候选都附有完整的 ConfigurationEvaluation;
    //   - feasibility / quality 由全部候选聚合得出:
    //     * 任一候选 DataInsufficient => 整体 DataInsufficient(证据不足);
    //     * 存在"可行且残差满足容差"的候选 => Feasible;
    //     * 否则 => Infeasible(记录 TargetResidual 或 IkNoSolution)。
    TargetEvaluation evaluate(const AnalysisContext& context,
                               const TaskPoint& target,
                               const TargetEvaluationOptions& options =
                                   TargetEvaluationOptions ()) const;
};

// 将候选解按"适合展示"的优先级排序(就地修改):
// 无碰撞优先,其次位置残差小、姿态残差小、关节裕度大、可操作度大、
// 离当前 Q 近,最后退化为按关节值字典序,保证排序结果稳定可复现。
void sortTargetCandidatesForDisplay(std::vector< TargetCandidate >& candidates);

} // namespace rws

#endif // RWS_KINEMATICANALYSIS_TARGETEVALUATOR_HPP
