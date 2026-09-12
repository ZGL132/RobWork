/**
 * @file   ErrorsTest.cpp
 * @brief  错误码全表用例组（EV-ERR）——EvidenceErrorCode 全枚举逐 token：
 *         稳定 token/建议注册码逐枚举＋EvidenceError 异常轨＋§13 建议码
 *         清单全覆盖核对。
 *
 * 设计依据：
 *   - units/evidence.md §3.1 组成表（EvidenceErrorCode（稳定 token）、
 *     EvidenceError）、§13 diagnostics 行（7 个 EVI-* 建议码原文）、
 *     各抛错点原文（§4.1.5④b "snapshot-integrity"、§9.4
 *     "duplicate-evaluator"——token 期望值逐项抄录自这些原文）
 *   - 需求 NFR-COR-03（不静默）；任务契约 tasks/foundation/EV-T02.json
 *     （acceptance 2：EvidenceErrorCode 全表 token 用例通过——稳定 token
 *     逐枚举）
 *
 * 组名说明：§11 反例矩阵未设 EV-T02 专项组（该矩阵自 EV-T03 起的消费面
 * 用例为主）——本文件组 ID（EV-ERR）为实现侧组名，对照 §12 EV-T02 行
 * 验证方式"单元测试（token/语法/闭包校验器）"的 token 部分；
 * EV-T01 的 EV-BUILD 同例（实现侧组名先例）。
 */

#include <sdurws/ird/evidence/Errors.hpp>

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

using namespace sdurws::ird::evidence;

// =====================================================================
// 期望全表：逐值抄录自 units/evidence.md 原文（token 出处见注释列——
// §13 建议码清单＋§4.1.5④b/§9.4 抛错点原文）。手写期望而非由被测函数
// 反推——防"实现即期望"的恒真用例（验收协议：锚定先行）。
// =====================================================================

/// 单行期望：码值＋稳定 token＋建议注册码。
struct CodeExpectation {
    EvidenceErrorCode code;   ///< 被测枚举值
    const char* tokenText;    ///< 稳定 token（"evidence/..."，§3.1/抛错点原文派生）
    const char* registry;     ///< §13 建议码（"EVI-..."；后两值为补登建议 F-056）
};

/// 全 9 值期望表（顺序＝枚举声明顺序——数值稳定性一并钉住）。
const std::vector<CodeExpectation>& expectations()
{
    static const std::vector<CodeExpectation> table = {
        // ---------------- §13 建议码清单 7 值（逐字对应） ----------------
        {EvidenceErrorCode::SnapshotIncomplete,         "evidence/snapshot-incomplete",         "EVI-SNAPSHOT-INCOMPLETE"},
        {EvidenceErrorCode::EvidenceMissing,            "evidence/evidence-missing",            "EVI-EVIDENCE-MISSING"},
        {EvidenceErrorCode::ProofInvalid,               "evidence/proof-invalid",               "EVI-PROOF-INVALID"},
        {EvidenceErrorCode::CaseCoverageMissing,        "evidence/case-coverage-missing",       "EVI-CASE-COVERAGE-MISSING"},
        {EvidenceErrorCode::EnvelopeIllegalCombination, "evidence/envelope-illegal-combination", "EVI-ENVELOPE-ILLEGAL-COMBINATION"},
        {EvidenceErrorCode::CacheIncompatible,          "evidence/cache-incompatible",          "EVI-CACHE-INCOMPATIBLE"},
        {EvidenceErrorCode::EvaluatorDuplicate,         "evidence/duplicate-evaluator",         "EVI-EVALUATOR-DUPLICATE"},
        // ---------------- 抛错点明文 token（§4.1.5④b 原文） ----------------
        {EvidenceErrorCode::SnapshotIntegrity,          "evidence/snapshot-integrity",          "EVI-SNAPSHOT-INTEGRITY"},  // 建议码补登（F-056）
        // ---------------- 注册期闭包拒绝码（§4.2.3① 原文语义） ----------------
        {EvidenceErrorCode::DeclarationInvalid,         "evidence/declaration-invalid",         "EVI-DECLARATION-INVALID"}, // 建议码补登（F-056）
    };
    return table;
}

}  // namespace

/** EV-ERR-1 全表逐 token：9 值逐一核对 token/建议码（acceptance 2——稳定 token 逐枚举）。 */
TEST(EvidenceErrorsFullTable, AllCodesPerToken_EV_ERR_NFR_COR_02)
{
    // 前置锚定：枚举值个数与顺序钉死——9 值、EvaluatorDuplicate 序号 8。
    // 防止后续任务静默插入/重排枚举值（数值进入二进制契约面；追加只能
    // 在表尾并在单元卡留痕——头文件纪律注释）。
    ASSERT_EQ(static_cast<int>(EvidenceErrorCode::EvaluatorDuplicate), 8)
        << "枚举值个数/顺序漂移——稳定 token 契约与持久化诊断的码面被破坏";
    ASSERT_EQ(expectations().size(), static_cast<std::size_t>(9));

    for (const auto& e : expectations()) {
        SCOPED_TRACE(std::string{"码值: "} + e.tokenText);
        // token：非空、统一前缀、与冻结原文逐字一致（全函数契约）。
        EXPECT_FALSE(token(e.code).empty()) << "token 不得为空（全函数契约）";
        EXPECT_EQ(token(e.code).substr(0, 9), "evidence/") << "token 前缀约定";
        EXPECT_EQ(token(e.code), std::string_view{e.tokenText});
        // 建议注册码：EVI- 前缀、与 §13 原文（或补登建议）逐字一致；
        // 码值权威归 diagnostics StableCodeRegistry（PA-1）——本表只承诺
        // 建议码面（§13 P-PR-6 同模式）。
        EXPECT_FALSE(registryCode(e.code).empty()) << "9 值均发建议码（F-056 补登口径）";
        EXPECT_EQ(registryCode(e.code).substr(0, 4), "EVI-") << "建议码前缀（§13 清单形态）";
        EXPECT_EQ(registryCode(e.code), std::string_view{e.registry});
    }
}

/** EV-ERR-2 §13 清单全覆盖：§13 建议码清单的 7 值在码面中各出现恰一次（不重不漏）。 */
TEST(EvidenceErrorsFullTable, Section13SuggestionsAllCovered_EV_ERR)
{
    // §13 原文清单（diagnostics 交接行——建议值，码值权威归 StableCodeRegistry）。
    // 逐项在期望表中检索：既验证"清单全收编"（不漏），也验证"无重复码"
    // （一码一建议——诊断聚合不歧义）。
    const char* const section13[] = {
        "EVI-SNAPSHOT-INCOMPLETE",       "EVI-CASE-COVERAGE-MISSING", "EVI-EVIDENCE-MISSING",
        "EVI-PROOF-INVALID",             "EVI-ENVELOPE-ILLEGAL-COMBINATION",
        "EVI-CACHE-INCOMPATIBLE",        "EVI-EVALUATOR-DUPLICATE",
    };
    for (const char* expected : section13) {
        SCOPED_TRACE(std::string{"§13 建议码: "} + expected);
        int hits = 0;
        for (const auto& e : expectations()) {
            if (registryCode(e.code) == std::string_view{expected}) { ++hits; }
        }
        EXPECT_EQ(hits, 1) << "§13 建议码应被恰一个枚举值承载";
    }
    // 收编规模核对：9 值码面 = §13 清单 7 值＋补登建议 2 值（F-056）。
    EXPECT_EQ(expectations().size(), static_cast<std::size_t>(9));
}

/** EV-ERR-3 异常轨：消息前缀约定＋code() 访问器＋按基类捕获（§2.1 异常行/D-15）。 */
TEST(EvidenceErrorsThrow, MessagePrefixAndCodeAccessor_EV_ERR)
{
    // 有细节：what() ＝ "<token>: <detail>"——前缀由构造强制拼装，
    // 调用方无法构造裸消息异常（检索面稳定）。
    const EvidenceError withDetail{EvidenceErrorCode::SnapshotIntegrity,
                                   "obj-3f.. 内容摘要与 contentVersion 不符"};
    EXPECT_STREQ(withDetail.what(),
                 "evidence/snapshot-integrity: obj-3f.. 内容摘要与 contentVersion 不符");
    EXPECT_EQ(withDetail.code(), EvidenceErrorCode::SnapshotIntegrity);

    // 无细节：what() 恰为 token（无尾随冒号空格）。
    const EvidenceError noDetail{EvidenceErrorCode::EvaluatorDuplicate};
    EXPECT_STREQ(noDetail.what(), "evidence/duplicate-evaluator");
    EXPECT_EQ(noDetail.code(), EvidenceErrorCode::EvaluatorDuplicate);

    // 异常轨按基类捕获（std::runtime_error/std::exception 双层兼容——
    // 调用方可用既有异常体系接住 evidence 失败；D-15：仅进程内抛传）。
    try {
        throw EvidenceError{EvidenceErrorCode::EnvelopeIllegalCombination, "Canceled×Feasible"};
    } catch (const std::runtime_error& ex) {
        EXPECT_STREQ(ex.what(), "evidence/envelope-illegal-combination: Canceled×Feasible");
    }
}
