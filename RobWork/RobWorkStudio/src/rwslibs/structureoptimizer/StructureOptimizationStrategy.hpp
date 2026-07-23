#ifndef RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONSTRATEGY_HPP
#define RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONSTRATEGY_HPP

#include "StructureOptimizationTypes.hpp"
#include <functional>

namespace rws {

class StructureCandidateCache;

struct StructureOptimizationCallbacks {
    std::function<bool()> isCancellationRequested;
    std::function<void()> waitIfPaused;
    std::function<void(const StructureProgress&)> onProgress;
};

//! @brief 候选解评估器抽象接口。
//!
//! 实现类负责根据给定的设计变量值构建机器人模型、执行运动学 / 碰撞评估，
//! 并将结果写入 candidate (raw / scores / feasible / violatedConstraints 等)。
class IStructureCandidateEvaluator {
public:
    virtual ~IStructureCandidateEvaluator() = default;

    //! @brief 评估指定设计变量值组合的候选解。
    //! @param problem  优化问题定义 (只读)。
    //! @param candidate  待评估的候选解，其 values 字段已填充；评估后 raw / scores / feasible 被写入。
    //! @param stage  评估阶段 (Quick / Verified)。
    //! @param callbacks  取消检查与进度报告回调。
    //! @param cache  可选缓存，评估前查找 / 评估后存入。
    virtual void evaluate(
        const StructureOptimizationProblem& problem,
        StructureCandidateResult& candidate,
        StructureEvaluationStage stage,
        const StructureOptimizationCallbacks& callbacks,
        StructureCandidateCache* cache = nullptr) = 0;
};

class StructureOptimizationStrategy {
  public:
    virtual ~StructureOptimizationStrategy() {}
    virtual StructureOptimizationResult optimize(
        const StructureOptimizationProblem& problem,
        IStructureCandidateEvaluator& evaluator,
        const StructureOptimizationCallbacks& callbacks) = 0;
};

} // namespace rws
#endif
