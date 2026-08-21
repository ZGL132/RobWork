#ifndef RWS_STRUCTUREOPTIMIZATION_QUICKSCREENINGPOLICY_HPP
#define RWS_STRUCTUREOPTIMIZATION_QUICKSCREENINGPOLICY_HPP

#include "CandidateResult.hpp"
#include "CompiledCandidate.hpp"

#include <string>

namespace rws {

//! @brief Quick 阶段对候选方案采取的保守筛选结论。
enum class QuickScreeningDecision
{
    DefinitelyReject, //!< 已有确定性硬证据，可直接淘汰
    Uncertain,        //!< 证据不足，必须保留到后续验证
    Promote           //!< Quick 通过，允许进入后续晋级队列
};

/**
 * @brief Quick 筛选所需的输入事实。
 *
 * 这些字段是上游编译器/评估器已经得出的事实，策略只读取它们并生成
 * 筛选意见，不修改 CandidateResult，也不重新执行 IK、FK 或碰撞计算。
 */
struct QuickScreeningPolicyInput
{
    const CandidateResult* candidate = nullptr;
    CandidateCompileStatus compileStatus = CandidateCompileStatus::Compiled;
    bool invalidModel = false;
    bool deterministicHardGeometryViolation = false;
    bool confirmedCollision = false;
    bool lowSampleNoIkSolution = false;
    bool partial = false;
    bool canceled = false;
    bool clearFeasibleEvidence = false;
    int uncertainPromotionCount = 0;
};

/**
 * @brief Quick 阶段的保守判定结果。
 *
 * reasonCode 用于机器审计，reason 用于日志/UI；两者都由策略统一产生，
 * 避免调用方各自拼接原因而导致同一结论出现不同文本。Quick-only 结果
 * 即使被 Promote，也不会具备 final best 资格，必须经过 Verified。
 */
struct QuickScreeningResult
{
    QuickScreeningDecision decision = QuickScreeningDecision::Uncertain;
    std::string reasonCode;
    std::string reason;
    int uncertainPromotionCount = 0;
    bool eligibleForFinalBest = false;
};

class QuickScreeningPolicy
{
  public:
    QuickScreeningResult evaluate(const QuickScreeningPolicyInput& input) const;
};

const char* toString(QuickScreeningDecision decision);

} // namespace rws

#endif
