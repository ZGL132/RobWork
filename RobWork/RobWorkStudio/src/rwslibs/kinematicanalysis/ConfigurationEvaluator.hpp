// =============================================================================
//  ConfigurationEvaluator.hpp —— 关节配置评估器(声明)
// =============================================================================
//
// 本组件回答一个核心问题:"给定一个关节配置 q,该配置是否满足当前分析任务?"
// 评估维度包括:
//   - TCP 正运动学位姿(FK 结果);
//   - 各关节相对限位的归一化裕度(是否接近/超出关节限位);
//   - TCP 雅可比的奇异值 / 条件数 / 可操作度(是否奇异或退化);
//   - (可选)碰撞状态。
//
// 设计约束:
//   - 评估器为无状态类,只依赖入参 AnalysisContext,不持有任何成员数据,
//     因此可被上层组件(TargetEvaluator、KinematicBatchRunner)安全地反复复用;
//   - 所有判定阈值来自 AnalysisContext::thresholds,由调用方统一注入,
//     保证"近限位 / 近奇异"的判定口径在整个插件内保持一致;
//   - 单个指标缺失不阻止评估:评估器始终返回结构完整的 ConfigurationEvaluation,
//     通过 feasibility / quality / failureReasons / warnings 共同表达"数据不足"。
#ifndef RWS_KINEMATICANALYSIS_CONFIGURATIONEVALUATOR_HPP
#define RWS_KINEMATICANALYSIS_CONFIGURATIONEVALUATOR_HPP

#include "KinematicAnalysisContext.hpp"

namespace rws {

// =============================================================================
//  ConfigurationEvaluationOptions —— 单次配置评估的选项
// =============================================================================
//
// 该选项仅影响"本次 evaluate"的行为,不会改写 AnalysisContext 中的全局配置。
// 其中 requireCollisionFree 与 context.collisionRequired 是"或"关系:
// 任一为真都会强制要求碰撞检测器可用,否则评估以 DataInsufficient 拒绝。
struct ConfigurationEvaluationOptions
{
    // 证据等级(Estimated / Quick / Verified),写入评估结果的 stage 字段。
    AnalysisEvidenceStage evidenceStage = AnalysisEvidenceStage::Quick;
    // 是否执行碰撞检测;需要 AnalysisContext.collisionDetector 可用。
    bool checkCollision = true;
    // 是否要求"必须无碰撞";为真时即使 checkCollision=false 也会强制检测。
    bool requireCollisionFree = false;
};

// =============================================================================
//  ConfigurationEvaluator —— 关节配置评估器(无状态,线程安全)
// =============================================================================
//
// 唯一入口 evaluate() 输入"上下文 + 关节配置 q + 选项",返回 ConfigurationEvaluation。
// 该类不保存任何跨调用状态,所有输入经参数传入,因此可被并发/嵌套调用。
class ConfigurationEvaluator
{
  public:
    // 评估关节配置 q,返回结构完整的 ConfigurationEvaluation。
    // 结果的可行性 / 质量按统一规则聚合:
    //   - DataInsufficient:关键依赖(Device / TCP / 碰撞检测器)缺失或底层求解异常;
    //   - Infeasible:存在硬性失败原因(超出限位 / 奇异 / 碰撞等);
    //   - Feasible:无硬性失败,quality 进一步区分 Good / Degraded。
    // 调用方无需再理解内部聚合规则,直接消费返回的字段即可。
    ConfigurationEvaluation evaluate(const AnalysisContext& context,
                                     const rw::math::Q& q,
                                     const ConfigurationEvaluationOptions& options =
                                         ConfigurationEvaluationOptions ()) const;
};

} // namespace rws

#endif // RWS_KINEMATICANALYSIS_CONFIGURATIONEVALUATOR_HPP
