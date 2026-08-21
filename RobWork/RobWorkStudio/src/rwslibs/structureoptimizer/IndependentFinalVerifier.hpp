#ifndef RWS_STRUCTUREOPTIMIZATION_INDEPENDENTFINALVERIFIER_HPP
#define RWS_STRUCTUREOPTIMIZATION_INDEPENDENTFINALVERIFIER_HPP

#include "CandidateResult.hpp"
#include "FinalValidationPlan.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace rws {

enum class FinalValidationStatus
{
    Incomplete,
    Complete,
    Failed
};

/** 一个独立 seed 的 Final 评估结果及其可去重证据身份。 */
struct FinalEvaluationSample
{
    std::string evidenceKey;
    CandidateResult result;
};

struct IndependentFinalCallbacks
{
    std::function<bool()> isCancellationRequested;
};

struct IndependentFinalResult
{
    FinalValidationStatus status = FinalValidationStatus::Incomplete;
    FinalValidationPlan plan;
    CandidateResult searchResult;
    CandidateResult finalResult;
    std::vector<FinalEvaluationSample> samples;
    std::size_t requestedCount = 0;
    std::size_t completedCount = 0;
    std::size_t newEvidenceCount = 0;
    std::size_t duplicateEvidenceCount = 0;
    bool canceled = false;
    std::string diagnostic;

    /**
     * @brief Final best 资格门控。
     *
     * 只有独立计划全部完成、没有失败样本、所有证据均为新证据，且最终
     * 聚合结果满足 Feasible + Verified 时才允许对外发布。
     */
    bool eligibleForFinalBest() const
    {
        return status == FinalValidationStatus::Complete && !canceled &&
               duplicateEvidenceCount == 0 &&
               finalResult.feasibility == Feasibility::Feasible &&
               finalResult.evidenceStage == AnalysisEvidenceStage::Verified;
    }
};

using IndependentFinalEvaluationCallback =
    std::function<FinalEvaluationSample(const std::string&, AnalysisEvidenceStage)>;

/**
 * @brief 执行与搜索阶段独立的 Final Verification。
 *
 * 该类只负责编排固定 seed、重复证据检查和状态聚合；真实的编译、FK、IK、
 * 碰撞与指标计算仍由回调完成，避免 Final 阶段复制 TargetEvaluator 算法。
 */
class IndependentFinalVerifier
{
  public:
    static IndependentFinalResult verify(
        const FinalValidationPlan& plan,
        const CandidateResult& searchResult,
        const std::vector<std::string>& priorEvidenceKeys,
        const IndependentFinalEvaluationCallback& evaluate,
        const IndependentFinalCallbacks& callbacks = {});
};

const char* toString(FinalValidationStatus status);

} // namespace rws

#endif
