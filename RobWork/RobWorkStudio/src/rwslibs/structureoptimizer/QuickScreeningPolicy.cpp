#include "QuickScreeningPolicy.hpp"

namespace rws {
namespace {

QuickScreeningResult reject(const char* code, const char* reason, int count)
{
    QuickScreeningResult result;
    result.decision = QuickScreeningDecision::DefinitelyReject;
    result.reasonCode = code;
    result.reason = reason;
    result.uncertainPromotionCount = count;
    return result;
}

QuickScreeningResult uncertain(const char* code, const char* reason, int count)
{
    QuickScreeningResult result;
    result.decision = QuickScreeningDecision::Uncertain;
    result.reasonCode = code;
    result.reason = reason;
    result.uncertainPromotionCount = count;
    return result;
}

} // namespace

QuickScreeningResult QuickScreeningPolicy::evaluate(const QuickScreeningPolicyInput& input) const
{
    const int count = input.uncertainPromotionCount;
    const bool candidatePartial = input.candidate && input.candidate->completion.partial();
    const bool candidateCanceled = input.candidate &&
                                   input.candidate->lifecycle == CandidateLifecycle::Canceled;

    // 这些条件具有确定性，且不会把“样本不足”误判成不可行。
    if (input.compileStatus == CandidateCompileStatus::CompileFailed)
        return reject("COMPILE_FAILED", "候选编译失败，不能继续评估。", count);
    if (input.invalidModel)
        return reject("INVALID_MODEL", "候选模型无效，违反模型硬约束。", count);
    if (input.deterministicHardGeometryViolation)
        return reject("HARD_GEOMETRY_VIOLATION", "确定性几何硬约束违反。", count);
    if (input.confirmedCollision)
        return reject("CONFIRMED_COLLISION", "已确认发生碰撞。", count);

    if (input.lowSampleNoIkSolution)
        return uncertain("LOW_SAMPLE_NO_IK_SOLUTION",
                         "低样本下未找到 IK 解，证据不足以直接淘汰。",
                         count);
    if (input.partial || candidatePartial)
        return uncertain("PARTIAL_EVALUATION", "评估仅部分完成，等待后续验证。", count);
    if (input.canceled || candidateCanceled)
        return uncertain("CANCELED_EVALUATION", "评估已取消，保留已有结果但不能下结论。", count);

    if (!input.candidate)
        return uncertain("MISSING_CANDIDATE_RESULT", "缺少候选结果，无法完成 Quick 筛选。", count);

    if (input.candidate->feasibility == Feasibility::Infeasible &&
        input.candidate->evidenceStage == AnalysisEvidenceStage::Verified)
        return reject("VERIFIED_INFEASIBLE", "Verified 证据已确认候选不可行。", count);

    if (!input.clearFeasibleEvidence || input.candidate->feasibility != Feasibility::Feasible)
        return uncertain("INSUFFICIENT_FEASIBLE_EVIDENCE",
                         "尚无足够的可行性证据，不能晋级或淘汰。",
                         count);

    QuickScreeningResult result;
    result.decision = QuickScreeningDecision::Promote;
    result.reasonCode = "CLEAR_FEASIBLE_EVIDENCE";
    result.reason = "已有明确可行性证据，允许进入后续验证队列。";
    result.uncertainPromotionCount = count;
    // Quick 只能晋级到 Verified 队列；最终最佳必须有 Verified 证据。
    result.eligibleForFinalBest =
        input.candidate->evidenceStage == AnalysisEvidenceStage::Verified;
    return result;
}

const char* toString(QuickScreeningDecision decision)
{
    switch (decision) {
    case QuickScreeningDecision::DefinitelyReject: return "DefinitelyReject";
    case QuickScreeningDecision::Uncertain: return "Uncertain";
    case QuickScreeningDecision::Promote: return "Promote";
    }
    return "Uncertain";
}

} // namespace rws
