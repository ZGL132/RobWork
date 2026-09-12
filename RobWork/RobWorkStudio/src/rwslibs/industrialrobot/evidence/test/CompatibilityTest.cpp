/**
 * @file   CompatibilityTest.cpp
 * @brief  缓存/检查点兼容判定用例组——Quick≠Verified 双向拒绝（EV-CPA-1）、
 *         契约版本失配（EV-CPA-2）、检查点可恢复性与任务成败独立
 *         （EV-CPA-3）、部分/失败结果不得作正式缓存命中（EV-ENV-2 判定面
 *         ＋§8.2 纯判定反例）、FullHit 六条件、全量收集与检查序确定性、
 *         检查点五条件逐项与支持区间边界。
 *
 * 设计依据：
 *   - units/evidence.md §8.2（judgeCacheHit 三档判定式＋judgeCheckpoint
 *     Compatibility 五条件判定式）、§11 矩阵（EV-CPA-1/2/3、EV-ENV-2 行
 *     的输入/预期/观测点）、§12 EV-T09 行（验证方式＝EV-CPA-1~3；任务
 *     契约 tasks/foundation/EV-T09.json ≙WP-05-T09 acceptance 1～2）、
 *     §14 追踪矩阵 CON-04 行
 *   - 需求 CON-04（部分/失败结果不得作为正式缓存命中）、OPT-06（Quick
 *     筛选/Verified 复核/缓存/检查点）、EVI-01 表 1（模式效力——D-13
 *     不升降级）
 *
 * 用例与 acceptance 的对应（实现纪律"每条 acceptance 至少一个具名测试"）：
 *   - acceptance 1（EV-CPA-1~3）：ModeMismatchBothDirections_EV_CPA_1、
 *     ContractVersionMismatch_EV_CPA_2（两判定面）、
 *     CheckpointIndependentOfTaskOutcome_EV_CPA_3；
 *   - acceptance 2（部分/失败结果不得作正式缓存命中）：
 *     CanceledResultIsDiagnosticOnly_EV_ENV_2、
 *     UnfinalizedManifestIsDiagnosticOnly_EV_CPA_2 反例族、
 *     PartialOutcomeNeverFullHit 反例组。
 */

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/evidence/Compatibility.hpp>
#include <sdurws/ird/evidence/Slice.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace {

using namespace sdurws::ird::evidence;
namespace core = sdurws::ird::core;

// =====================================================================
// 身份/取值辅助（确定性固定值——与 CurrentnessTest 同款风格，自持不共享）
// =====================================================================

const char* kHex64A = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
const char* kHex64B = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
const char* kHex64C = "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";

core::ContentIdentity cid(const char* hex64)
{
    return core::ContentIdentity::fromCanonical(std::string{"cid-"} + hex64);
}

/// 合法评估键（isValidEvaluationKey 词形——小写字母开头 + [a-z0-9-]）。
const char* kValidKey = "kin-batch-ik";

// =====================================================================
// 场景组装辅助
// =====================================================================

/**
 * @brief 组装"六条件全对齐"的缓存命中查询与结果摘要对（各用例以此为
 *        底座、只改自关注的字段——正例即 FullHit，反例逐字段偏转）。
 *
 * 身份取值：sliceId＝A、契约版本＝3、Profile 身份＝B；模式 Quick。
 */
struct QueryAndSummary {
    CacheHitQuery query;
    CachedResultSummary summary;
};

QueryAndSummary alignedQuickPair()
{
    QueryAndSummary p;
    p.query.requestedMode = core::EvaluationMode::Quick;
    p.query.requestSliceId = cid(kHex64A);
    p.query.requestContractVersion = 3;
    p.query.requestProfileIdentity = cid(kHex64B);

    p.summary.mode = core::EvaluationMode::Quick;
    p.summary.outcome = core::TaskOutcome::Completed;
    p.summary.manifestFinalized = true;
    p.summary.sliceId = cid(kHex64A);
    p.summary.evaluatorContractVersion = 3;
    p.summary.profileContentIdentity = cid(kHex64B);
    p.summary.inputBaselineId = cid(kHex64C);   // 在场但不参与判定（D-04 分工）
    return p;
}

/// 组装与查询身份面对齐的检查点摘要（五条件底座——反例逐字段偏转）。
CheckpointSummary alignedCheckpoint()
{
    CheckpointSummary c;
    c.evaluatorKey = kValidKey;
    c.sliceId = cid(kHex64A);
    c.evaluatorContractVersion = 3;
    c.checkpointFormatVersion = 1;   // 支持闭区间 [1,1] 内（C-3）
    c.integrityVerified = true;
    return c;
}

/// 与检查点同源的请求（只消费 sliceId/契约版本两字段——C-4）。
CacheHitQuery checkpointRequest()
{
    CacheHitQuery q;
    q.requestedMode = core::EvaluationMode::Verified;   // 故意取非对齐值——断言模式不参与
    q.requestSliceId = cid(kHex64A);
    q.requestContractVersion = 3;
    q.requestProfileIdentity = cid(kHex64B);
    return q;
}

// =====================================================================
// EV-CPA-1：Quick≠Verified（模式不升降级，D-13 双向拒绝）
// =====================================================================

/** EV-CPA-1 主例（§11 行：Quick 结果缓存 vs Verified 请求〔sliceId 相同〕
 *  ｜judgeCacheHit｜Incompatible(mode-mismatch)｜reasons 精确）：Quick 证据
 *  不得当 Verified 证据（表 1——Quick 不得单独支撑正式通过，OPT-06 的
 *  "Quick 筛选→Verified 复核"链要求复核真实重算而非降级复用）。 */
TEST(CompatibilityCacheHit, QuickCacheVersusVerifiedRequest_EV_CPA_1)
{
    auto p = alignedQuickPair();
    p.query.requestedMode = core::EvaluationMode::Verified;   // 仅模式偏转——身份其余全对齐

    const CacheHitResult r = judgeCacheHit(p.query, p.summary);
    EXPECT_EQ(CacheHitResult::Incompatible, r.verdict);
    // reasons 精确（§11 观测点）：唯一失败项＝mode-mismatch。
    ASSERT_EQ(std::size_t{1}, r.reasons.size());
    EXPECT_EQ(CacheMissReason::ModeMismatch, r.reasons[0]);
}

/** EV-CPA-1 反方向（D-13 保守方向：Verified 命中供 Quick 用同为
 *  Incompatible——模式是请求属性，不做隐式升降级，防"高级别证据降级
 *  冒充"或"低级别证据升格冒充"两个方向的语义漂移）。 */
TEST(CompatibilityCacheHit, VerifiedCacheVersusQuickRequest_EV_CPA_1)
{
    auto p = alignedQuickPair();
    p.summary.mode = core::EvaluationMode::Verified;   // 缓存侧 Verified、请求侧 Quick

    const CacheHitResult r = judgeCacheHit(p.query, p.summary);
    EXPECT_EQ(CacheHitResult::Incompatible, r.verdict);
    ASSERT_EQ(std::size_t{1}, r.reasons.size());
    EXPECT_EQ(CacheMissReason::ModeMismatch, r.reasons[0]);
}

/** EV-CPA-1 正例半边：模式相等的同身份对必须 FullHit（证明拒绝只来自
 *  模式差——排除"任何 Quick 参与都拒绝"的过宽实现）。 */
TEST(CompatibilityCacheHit, SameModeAlignedPairIsFullHit_EV_CPA_1)
{
    const auto p = alignedQuickPair();
    const CacheHitResult r = judgeCacheHit(p.query, p.summary);
    EXPECT_EQ(CacheHitResult::FullHit, r.verdict);
    EXPECT_TRUE(r.reasons.empty());
}

// =====================================================================
// EV-CPA-2：契约版本失配（同 sliceId——模拟算法升级）
// =====================================================================

/** EV-CPA-2 缓存面（§11 行：同 sliceId、评估器 contractVersion 不同｜
 *  judgeCacheHit｜Incompatible(contract-mismatch)）：切片内容没变但算法
 *  契约升级——旧结果按旧契约算出，不得冒充新契约的输出。 */
TEST(CompatibilityCacheHit, ContractVersionMismatch_EV_CPA_2)
{
    auto p = alignedQuickPair();
    p.summary.evaluatorContractVersion = 2;   // 缓存条目是旧契约（请求侧为 3）

    const CacheHitResult r = judgeCacheHit(p.query, p.summary);
    EXPECT_EQ(CacheHitResult::Incompatible, r.verdict);
    ASSERT_EQ(std::size_t{1}, r.reasons.size());
    EXPECT_EQ(CacheMissReason::ContractMismatch, r.reasons[0]);
}

/** EV-CPA-2 检查点面（§11 行两判定同名观测）：契约升级后旧进度的求解
 *  状态不可信——reasons 以稳定 token"contract-mismatch"精确承载
 *  （§8.2 reasons 为 string 向量的跨进程透出面）。 */
TEST(CompatibilityCheckpoint, ContractVersionMismatch_EV_CPA_2)
{
    auto c = alignedCheckpoint();
    c.evaluatorContractVersion = 2;

    const CheckpointCompatibilityResult r =
        judgeCheckpointCompatibility(checkpointRequest(), c);
    EXPECT_EQ(CheckpointCompatibilityResult::Incompatible, r.verdict);
    ASSERT_EQ(std::size_t{1}, r.reasons.size());
    EXPECT_EQ(std::string{"contract-mismatch"}, r.reasons[0]);
}

// =====================================================================
// EV-CPA-3：检查点可恢复性与任务成败独立
// =====================================================================

/** EV-CPA-3 主例（§11 行：上次任务 Failed 的检查点〔integrityVerified=true〕
 *  ｜judgeCheckpointCompatibility｜Resumeable）：CheckpointSummary 根本没有
 *  outcome 字段（结构面断言）——可恢复性五条件不含任务成败维度；强制
 *  终止保留最近检查点正是为续跑而生（CON-04/TASK-01）。 */
TEST(CompatibilityCheckpoint, CheckpointIndependentOfTaskOutcome_EV_CPA_3)
{
    // 结构面：摘要类型不存在 outcome/模式/Profile 字段——任务成败维度在
    // 检查点判定中无处承载即"独立"的类型级保证（编译期事实，注释留痕）。
    CheckpointSummary failedTaskCheckpoint = alignedCheckpoint();
    // 上次任务 Failed（其检查点 integrityVerified=true——存储侧已校验本体
    // 完好）：判定只看五条件，全部对齐 → Resumeable。
    const CheckpointCompatibilityResult r =
        judgeCheckpointCompatibility(checkpointRequest(), failedTaskCheckpoint);
    EXPECT_EQ(CheckpointCompatibilityResult::Resumeable, r.verdict);
    EXPECT_TRUE(r.reasons.empty());

    // 独立性观测：同一检查点对 Quick 请求与 Verified 请求判定一致（C-4
    // ——requestedMode 不参与检查点判定，模式效力在结果面把关）。
    CacheHitQuery quickRequest = checkpointRequest();
    quickRequest.requestedMode = core::EvaluationMode::Quick;
    const CheckpointCompatibilityResult rQuick =
        judgeCheckpointCompatibility(quickRequest, failedTaskCheckpoint);
    EXPECT_EQ(r, rQuick);
}

/** EV-CPA-3 反面：完整性未通过的检查点一律拒绝（五条件第五条——
 *  NFR-COR-03：缺凭据视同损坏，不静默恢复；与上次任务是否成功无关的
 *  是"成败"，不是"完整性"——两维度不得混淆）。 */
TEST(CompatibilityCheckpoint, IntegrityNotVerifiedRejected_EV_CPA_3)
{
    auto c = alignedCheckpoint();
    c.integrityVerified = false;

    const CheckpointCompatibilityResult r =
        judgeCheckpointCompatibility(checkpointRequest(), c);
    EXPECT_EQ(CheckpointCompatibilityResult::Incompatible, r.verdict);
    ASSERT_EQ(std::size_t{1}, r.reasons.size());
    EXPECT_EQ(std::string{"integrity-not-verified"}, r.reasons[0]);
}

// =====================================================================
// acceptance 2：部分/失败结果不得作正式缓存命中（§8.2 纯判定反例）
// =====================================================================

/** EV-ENV-2 判定面（§11 行：Canceled 结果〔partialData〕作缓存查询｜
 *  judgeCacheHit｜DiagnosticOnly（可读诊断）非 FullHit｜reasons 含
 *  outcome-not-completed；EV-07 侧已保证 partialData.reusable 恒 false——
 *  本任务交付其判定面）：身份面对齐的未完成结果可读作诊断、绝不正式
 *  命中（CON-04 原文）。 */
TEST(CompatibilityCacheHit, CanceledResultIsDiagnosticOnly_EV_ENV_2)
{
    auto p = alignedQuickPair();
    p.summary.outcome = core::TaskOutcome::Canceled;   // 仅完成面偏转——身份面全对齐

    const CacheHitResult r = judgeCacheHit(p.query, p.summary);
    EXPECT_EQ(CacheHitResult::DiagnosticOnly, r.verdict);
    EXPECT_NE(CacheHitResult::FullHit, r.verdict);   // 不得正式命中
    ASSERT_EQ(std::size_t{1}, r.reasons.size());
    EXPECT_EQ(CacheMissReason::OutcomeNotCompleted, r.reasons[0]);
}

/** 失败/中断结果同轨（CON-04"部分/失败结果"的 Failed/Interrupted 两值
 *  ——四值 outcome 中除 Completed 外全部只可诊断性读取）。 */
TEST(CompatibilityCacheHit, FailedAndInterruptedResultsAreDiagnosticOnly_EV_ENV_2)
{
    for (const core::TaskOutcome outcome : {core::TaskOutcome::Failed,
                                            core::TaskOutcome::Interrupted}) {
        auto p = alignedQuickPair();
        p.summary.outcome = outcome;
        const CacheHitResult r = judgeCacheHit(p.query, p.summary);
        EXPECT_EQ(CacheHitResult::DiagnosticOnly, r.verdict) << "outcome 值：" << int(outcome);
        ASSERT_EQ(std::size_t{1}, r.reasons.size());
        EXPECT_EQ(CacheMissReason::OutcomeNotCompleted, r.reasons[0]);
    }
}

/** 清单未封账反例（Completed 但 manifestFinalized=false——运行未终结的
 *  中间态同样不得正式命中；§8.2 判定式"∨"第二支的独立承载：结果面两
 *  项各自成 reason，互不掩盖）。 */
TEST(CompatibilityCacheHit, UnfinalizedManifestIsDiagnosticOnly_EV_ENV_2)
{
    auto p = alignedQuickPair();
    p.summary.manifestFinalized = false;

    const CacheHitResult r = judgeCacheHit(p.query, p.summary);
    EXPECT_EQ(CacheHitResult::DiagnosticOnly, r.verdict);
    ASSERT_EQ(std::size_t{1}, r.reasons.size());
    EXPECT_EQ(CacheMissReason::ManifestIncomplete, r.reasons[0]);
}

/** 结果面双失配（未完成＋未封账并存）：两 reason 按检查序全量列出
 *  （C-1 不首错短路），verdict 仍为 DiagnosticOnly。 */
TEST(CompatibilityCacheHit, BothResultFaceFailuresListed_EV_ENV_2)
{
    auto p = alignedQuickPair();
    p.summary.outcome = core::TaskOutcome::Canceled;
    p.summary.manifestFinalized = false;

    const CacheHitResult r = judgeCacheHit(p.query, p.summary);
    EXPECT_EQ(CacheHitResult::DiagnosticOnly, r.verdict);
    ASSERT_EQ(std::size_t{2}, r.reasons.size());
    EXPECT_EQ(CacheMissReason::OutcomeNotCompleted, r.reasons[0]);   // 检查序：结果先于清单
    EXPECT_EQ(CacheMissReason::ManifestIncomplete, r.reasons[1]);
}

// =====================================================================
// 身份面其余两失配与全量收集（C-1 检查序确定性）
// =====================================================================

/** 切片身份失配（输入已变——结果过期；§8.2 FullHit 判定式第二条件。
 *  追溯：CON-04/§5.1 切片内容身份＝缓存键；acceptance 2 的 §8.2 纯判定
 *  反例族——身份面失配必须拒绝且不得降档为诊断性读取）。 */
TEST(CompatibilityCacheHit, SliceMismatchIsIncompatible)
{
    auto p = alignedQuickPair();
    p.summary.sliceId = cid(kHex64C);   // 缓存条目来自另一次输入冻结

    const CacheHitResult r = judgeCacheHit(p.query, p.summary);
    EXPECT_EQ(CacheHitResult::Incompatible, r.verdict);
    ASSERT_EQ(std::size_t{1}, r.reasons.size());
    EXPECT_EQ(CacheMissReason::SliceMismatch, r.reasons[0]);
}

/** Profile 身份失配（证据效力口径不同——跨标尺复用无语义；§8.2 FullHit
 *  判定式第四条件。追溯：CON-04 Profile 身份比对；acceptance 2 反例族）。 */
TEST(CompatibilityCacheHit, ProfileMismatchIsIncompatible)
{
    auto p = alignedQuickPair();
    p.summary.profileContentIdentity = cid(kHex64C);

    const CacheHitResult r = judgeCacheHit(p.query, p.summary);
    EXPECT_EQ(CacheHitResult::Incompatible, r.verdict);
    ASSERT_EQ(std::size_t{1}, r.reasons.size());
    EXPECT_EQ(CacheMissReason::ProfileMismatch, r.reasons[0]);
}

/** 全量收集（C-1）：六条件全部失配——reasons 恰为六项、顺序＝检查序
 *  （模式→切片→契约→Profile→结果→清单）；身份面失配在 presence 时
 *  verdict 恒 Incompatible（其余一切），即使结果面同时失配也不降档为
 *  DiagnosticOnly（三档判定式互斥）。 */
TEST(CompatibilityCacheHit, AllMismatchReasonsCollectedInCheckOrder_EV_CPA_2)
{
    auto p = alignedQuickPair();
    p.query.requestedMode = core::EvaluationMode::Verified;
    p.query.requestSliceId = cid(kHex64B);
    p.query.requestContractVersion = 9;
    p.query.requestProfileIdentity = cid(kHex64C);
    p.summary.outcome = core::TaskOutcome::Failed;
    p.summary.manifestFinalized = false;

    const CacheHitResult r = judgeCacheHit(p.query, p.summary);
    EXPECT_EQ(CacheHitResult::Incompatible, r.verdict);
    ASSERT_EQ(std::size_t{6}, r.reasons.size());
    EXPECT_EQ(CacheMissReason::ModeMismatch, r.reasons[0]);
    EXPECT_EQ(CacheMissReason::SliceMismatch, r.reasons[1]);
    EXPECT_EQ(CacheMissReason::ContractMismatch, r.reasons[2]);
    EXPECT_EQ(CacheMissReason::ProfileMismatch, r.reasons[3]);
    EXPECT_EQ(CacheMissReason::OutcomeNotCompleted, r.reasons[4]);
    EXPECT_EQ(CacheMissReason::ManifestIncomplete, r.reasons[5]);
}

/** 身份面＋结果面混合失配不降档：模式失配（身份面）与未完成（结果面）
 *  并存 → Incompatible 且两项全列（DiagnosticOnly 仅限身份面全对齐——
 *  §8.2"其余一切"的互斥面）。 */
TEST(CompatibilityCacheHit, IdentityFaceMismatchNeverDiagnosticOnly_EV_ENV_2)
{
    auto p = alignedQuickPair();
    p.query.requestedMode = core::EvaluationMode::Verified;   // 身份面失配
    p.summary.outcome = core::TaskOutcome::Canceled;          // 结果面失配

    const CacheHitResult r = judgeCacheHit(p.query, p.summary);
    EXPECT_EQ(CacheHitResult::Incompatible, r.verdict);
    ASSERT_EQ(std::size_t{2}, r.reasons.size());
    EXPECT_EQ(CacheMissReason::ModeMismatch, r.reasons[0]);
    EXPECT_EQ(CacheMissReason::OutcomeNotCompleted, r.reasons[1]);
}

/** 判定确定性（NFR-COR-02）：同输入两次判定结果全等（verdict＋reasons
 *  ——operator== 全字段比较）。 */
TEST(CompatibilityCacheHit, JudgmentIsDeterministic_EV_CPA_2)
{
    auto p = alignedQuickPair();
    p.summary.outcome = core::TaskOutcome::Canceled;
    const CacheHitResult first = judgeCacheHit(p.query, p.summary);
    const CacheHitResult second = judgeCacheHit(p.query, p.summary);
    EXPECT_EQ(first, second);
}

// =====================================================================
// 检查点五条件逐项与支持区间边界（C-2/C-3）
// =====================================================================

/** 检查点正例：五条件全对齐 → Resumeable（reasons 空；请求的 Quick 模式
 *  不影响——见 EV_CPA_3 独立性观测）。 */
TEST(CompatibilityCheckpoint, AlignedCheckpointIsResumeable_EV_CPA_3)
{
    const CheckpointCompatibilityResult r =
        judgeCheckpointCompatibility(checkpointRequest(), alignedCheckpoint());
    EXPECT_EQ(CheckpointCompatibilityResult::Resumeable, r.verdict);
    EXPECT_TRUE(r.reasons.empty());
}

/** 评估键词形非法（C-2：损坏/伪造的检查点头——正常写入路径产出的键必过
 *  isValidEvaluationKey 词形闸门；保守拒绝不带病恢复）。 */
TEST(CompatibilityCheckpoint, MalformedEvaluatorKeyRejected_EV_CPA_3)
{
    auto c = alignedCheckpoint();
    c.evaluatorKey = "Kin.Batch-IK";   // 大写开头＋含点——评估键词形（[a-z][a-z0-9-]{1,63}）双重违例

    const CheckpointCompatibilityResult r =
        judgeCheckpointCompatibility(checkpointRequest(), c);
    EXPECT_EQ(CheckpointCompatibilityResult::Incompatible, r.verdict);
    ASSERT_EQ(std::size_t{1}, r.reasons.size());
    EXPECT_EQ(std::string{"evaluator-key-invalid"}, r.reasons[0]);
}

/** 切片身份失配（输入变了就得从头跑——续跑旧输入毫无意义）。 */
TEST(CompatibilityCheckpoint, SliceMismatchRejected_EV_CPA_2)
{
    auto c = alignedCheckpoint();
    c.sliceId = cid(kHex64B);

    const CheckpointCompatibilityResult r =
        judgeCheckpointCompatibility(checkpointRequest(), c);
    EXPECT_EQ(CheckpointCompatibilityResult::Incompatible, r.verdict);
    ASSERT_EQ(std::size_t{1}, r.reasons.size());
    EXPECT_EQ(std::string{"slice-mismatch"}, r.reasons[0]);
}

/** 格式版本支持区间边界（C-3）：区间 [1,1] 内（下界 1）可恢复；区间外
 *  （0＝过老、2＝过新）一律拒绝——读不懂的格式不得恢复。 */
TEST(CompatibilityCheckpoint, FormatVersionRangeBoundary_EV_CPA_3)
{
    // 下界值＝当前唯一支持格式（1）：可恢复。
    {
        const CheckpointCompatibilityResult r =
            judgeCheckpointCompatibility(checkpointRequest(), alignedCheckpoint());
        EXPECT_EQ(CheckpointCompatibilityResult::Resumeable, r.verdict);
    }
    // 过老（0 < 下界）：拒绝。
    {
        auto c = alignedCheckpoint();
        c.checkpointFormatVersion = kCheckpointFormatVersionMin - 1;
        const CheckpointCompatibilityResult r =
            judgeCheckpointCompatibility(checkpointRequest(), c);
        EXPECT_EQ(CheckpointCompatibilityResult::Incompatible, r.verdict);
        ASSERT_EQ(std::size_t{1}, r.reasons.size());
        EXPECT_EQ(std::string{"checkpoint-format-incompatible"}, r.reasons[0]);
    }
    // 过新（2 > 上界）：拒绝（更高版本格式由更新的构建支持——当前构建
    // 读不懂即不恢复，保守方向）。
    {
        auto c = alignedCheckpoint();
        c.checkpointFormatVersion = kCheckpointFormatVersionMax + 1;
        const CheckpointCompatibilityResult r =
            judgeCheckpointCompatibility(checkpointRequest(), c);
        EXPECT_EQ(CheckpointCompatibilityResult::Incompatible, r.verdict);
        ASSERT_EQ(std::size_t{1}, r.reasons.size());
        EXPECT_EQ(std::string{"checkpoint-format-incompatible"}, r.reasons[0]);
    }
}

/** 多条件失配全量收集：切片＋契约＋格式＋完整性四项并拒——reasons 恰
 *  四项、顺序＝检查序（键→切片→契约→格式→完整性；本例键词形合法故
 *  首项为 slice-mismatch）。 */
TEST(CompatibilityCheckpoint, MultipleRejectionsCollectedInCheckOrder_EV_CPA_2)
{
    auto c = alignedCheckpoint();
    c.sliceId = cid(kHex64B);
    c.evaluatorContractVersion = 1;
    c.checkpointFormatVersion = 99;
    c.integrityVerified = false;

    const CheckpointCompatibilityResult r =
        judgeCheckpointCompatibility(checkpointRequest(), c);
    EXPECT_EQ(CheckpointCompatibilityResult::Incompatible, r.verdict);
    ASSERT_EQ(std::size_t{4}, r.reasons.size());
    EXPECT_EQ(std::string{"slice-mismatch"}, r.reasons[0]);
    EXPECT_EQ(std::string{"contract-mismatch"}, r.reasons[1]);
    EXPECT_EQ(std::string{"checkpoint-format-incompatible"}, r.reasons[2]);
    EXPECT_EQ(std::string{"integrity-not-verified"}, r.reasons[3]);
}

/** 判定确定性（NFR-COR-02）：同输入两次判定结果全等。 */
TEST(CompatibilityCheckpoint, JudgmentIsDeterministic_EV_CPA_3)
{
    auto c = alignedCheckpoint();
    c.integrityVerified = false;
    const CheckpointCompatibilityResult first =
        judgeCheckpointCompatibility(checkpointRequest(), c);
    const CheckpointCompatibilityResult second =
        judgeCheckpointCompatibility(checkpointRequest(), c);
    EXPECT_EQ(first, second);
}

}  // namespace
