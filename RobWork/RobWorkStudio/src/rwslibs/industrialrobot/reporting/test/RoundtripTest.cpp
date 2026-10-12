/**
 * @file   RoundtripTest.cpp
 * @brief  报告往返复算单元测试（RPT-T16）——RP-ROUND-1 一致/不一致双路
 *         ＋复算不可达三态＋Usage 轨＋历史零修改＋多结果逐条定位
 *         （任务契约 acceptance 1~2；§10.1 RP-ROUND-1 行"复算一致/不一致
 *         双路用例过"的执行面）。
 *
 * 设计依据：
 *   - units/reporting.md §7.7（设计权威——roundtripCheck 比对入口/复算
 *     执行归注入/一致维持可用/不一致不可用于正式结论/不修改历史报告）、
 *     §10.1 RP-ROUND-1 行（需求/AT 依据＝RPT-06、AT-22；观测点＝比对
 *     结果＋标记字段）、§10 替身边界声明
 *   - 需求 RPT-06（往返复算——不一致时报告不可用于正式结论）、NFR-COR-04
 *     （复现锚期复验）、AT-22（roundtrip 复算一致）
 *   - 任务契约 tasks/foundation/RPT-T16.json acceptance 1（RP-ROUND-1
 *     一致/不一致双路用例过）、acceptance 2（不一致时报告不可用于正式
 *     结论）
 *
 * 替身边界声明（§10.1 RP-STATE-4／test/README.md §3 同源——本文件共用）：
 *   本文件的局部夹具 ScriptedRecomputeSource 输出仅验证 reporting 侧往返
 *   复算契约（比对/标记/三态语义），**不构成**任何 execution 评估通道、
 *   各域评估器或运动学/轨迹/动力学/选型结果的业务正确性证明——复算执行
 *   真实形态归 execution/各域适配（L5 装配，§7.7）；替身返回的复算
 *   envelope 一律经 evidence ResultEnvelope::make 构造（合法组合-only——
 *   非法组合在 evidence 构造边界即被拒绝，reporting 不自造非法样本），
 *   脚本数值为确定性固定值（禁随机——同脚本同结论）。
 *
 * 场景设施：复用 RP 契约套件场景头 ReportingScenario.hpp（BuildScene/
 * MapResolver——同一场景语言，报告构建链路与 RP-\* 用例完全同源）。
 *
 * 线程约束：全部用例单线程（roundtripCheck 并发安全面，测试无须并发）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/Fixture.hpp>             // DeterministicEnv（确定性上下文）
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO（ird-test-report.json 追溯）

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/evidence/Envelope.hpp>
#include <sdurws/ird/reporting/Builder.hpp>
#include <sdurws/ird/reporting/Errors.hpp>
#include <sdurws/ird/reporting/Export.hpp>      // IReportResolver（复算检查器注入缝——复用）
#include <sdurws/ird/reporting/Identity.hpp>
#include <sdurws/ird/reporting/ReportModel.hpp>
#include <sdurws/ird/reporting/Roundtrip.hpp>

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "ReportingScenario.hpp"

namespace {

using namespace sdurws::ird::reporting;
namespace core = sdurws::ird::core;
namespace evidence = sdurws::ird::evidence;
namespace tk = sdurws::ird::testkit;
using namespace sdurws::ird::reporting::rp_scenario;
using test_fakes::ScriptedResultSource;

// IRD_EXPECT_IDENTICAL 宏展开为非限定 checkIdentical（消费方 TU 显式引入
// ——AssertMacros.hpp 文档化前置条件，ReportingContractSuiteTest 同款）。
using sdurws::ird::testkit::checkIdentical;

// =====================================================================
// 局部夹具：脚本化复算源（§7.7 注入源 IRoundtripRecomputeSource 的替身）
// =====================================================================

/**
 * @brief 复算注入源替身（按请求键返回预置 envelope 或 nullopt）。
 *
 * 脚本面：按 runId 规范文本索引的 envelope 表——命中即返回该 envelope
 * （"复算可达"），未命中返回 nullopt（"复算不可达"注入面）。请求观测表
 * 记录收到的全部请求（reporting 抽取的复算请求字段断言面——§7.2 步③
 * 投影逆用的核对）。
 *
 * "一致路"脚本＝把 ScriptedResultSource 中的原 envelope 原样登记（同
 * 数据源重评估产出同 envelope——确定性评估的替身表达）；"不一致路"脚本
 * ＝登记经单点变异的 envelope（RP-ROUND-1 前置列"脚本改输入"的替身
 * 表达——改绑定面或改结论面两种变异形态分别成案）。
 */
class ScriptedRecomputeSource final : public IRoundtripRecomputeSource {
public:
    /// 复算脚本（键＝runId 规范文本；缺条目＝nullopt——复算不可达）。
    std::map<std::string, evidence::ResultEnvelope> envelopes;
    /// 请求观测表（逐请求记录——字段抽取断言面）。
    mutable std::vector<RoundtripRecomputeRequest> requests;

    std::optional<evidence::ResultEnvelope>
    tryRecompute(const RoundtripRecomputeRequest& request) const override
    {
        requests.push_back(request);
        const auto it = envelopes.find(request.runId.toCanonical());
        if (it == envelopes.end()) {
            return std::nullopt;   // 复算不可达（try 轨——不抛，§7.7 注入面）
        }
        return it->second;
    }
};

// =====================================================================
// 局部夹具：单运行场景组装（报告构建→复算脚本预置→检查器装配）
// =====================================================================

/// 场景壳（生命周期聚合——注入引用借用随本壳存活）。
struct RoundtripScene {
    BuildScene build;                     ///< 报告构建场景（查询端口＋结果源＋注册表）
    ScriptedRecomputeSource recompute;    ///< 复算注入源（本测试局部替身）
    MapResolver resolver;                 ///< 报告值解析（Export.hpp 注入缝——map 承载）

    /// 构建报告并预置解析脚本（resolver.put——检查器经注入缝取值）。
    ReviewReport buildAndScript()
    {
        ReviewReport report = buildOkReport();
        resolver.put(report);
        return report;
    }

    /// 构建辅助（失败即 ADD_FAILURE 并抛——用例中止；ReportingContractSuite
    /// buildOk 同款形态，本文件自持以避免跨 TU 共享非头设施）。
    ReviewReport buildOkReport()
    {
        const ReportBuildOutcome outcome = build.builder()->build(build.request(ReportLevel::B));
        if (outcome.report == nullptr) {
            ADD_FAILURE() << "场景前置失败：报告构建意外失败（error="
                          << (outcome.error.has_value() ? std::string(token(outcome.error->code()))
                                                        : std::string{"无"})
                          << "）";
            throw ReportError(ReportErrorCode::Usage, "场景前置失败——用例中止");
        }
        return std::move(*outcome.report);
    }

    /// 检查器实例（借用注入随本壳——经接口多态消费的装配点）。
    std::unique_ptr<IReportRoundtripChecker> checker()
    {
        return std::make_unique<ReportRoundtripChecker>(resolver, recompute);
    }
};

/// 标准单运行场景（B 级＋kin Populated——与 RP 套件 standardScene 同源）。
inline RoundtripScene singleRunScene(const core::RunId& run = runAt(0x14))
{
    RoundtripScene scene;
    scene.build = standardScene(run);
    return scene;
}

/// 诊断表中是否含指定码面＋级别的条目（伴随提示断言辅助）。
bool hasDiagnostic(const std::vector<core::DiagnosticRecord>& diagnostics,
                   const std::string& code, core::DiagnosticLevel level)
{
    for (const core::DiagnosticRecord& d : diagnostics) {
        if (d.code == code && d.level == level) {
            return true;
        }
    }
    return false;
}

// =====================================================================
// 套件夹具（确定性环境——testkit §6.3 同口径）
// =====================================================================

class RoundtripSuite : public ::testing::Test {
protected:
    void SetUp() override
    {
        tk::ReproRecord record;   // 默认 seed=20260909/threadCount=1（§6.3 口径）
        record.notes = "RPT-T16 roundtrip 测试——脚本值全固定，零随机";
        m_env = std::make_unique<tk::DeterministicEnv>(record);
        ASSERT_EQ(m_env->record().seed, 20260909u) << "确定性种子缺省口径（§6.3）";
    }

private:
    std::unique_ptr<tk::DeterministicEnv> m_env;
};

// =====================================================================
// RP-ROUND-1 双路＋三态＋轨面用例
// =====================================================================

/**
 * §10.1 RP-ROUND-1 路①（一致）：复算结果与报告一致（复算源返回与报告
 * 结果源同源的 envelope）→ 一致（报告维持可用）；零 mismatch、零伴随
 * 诊断；复算请求字段＝报告数据源抽取（§7.2 步③投影逆用）；复算载荷
 * 摘要回执在场；历史报告零修改（PA-2——检查前后报告值逐字段全等）。
 * 经 IReportRoundtripChecker 接口消费（公共接口交付面钉扎——RPT-T16
 * 契约 acceptance 1①）。
 */
TEST_F(RoundtripSuite, RP_ROUND_1_Consistent_KeepsReportUsable)
{
    IRD_TEST_INFO(std::vector<std::string>{"RPT-06", "NFR-COR-04"},
                  {std::string{"AT-22"}}, std::nullopt);

    const core::RunId run = runAt(0x14);
    RoundtripScene scene = singleRunScene(run);
    const ReviewReport report = scene.buildAndScript();

    // 一致路脚本：复算源返回与报告结果源**同一** envelope（同数据源重
    // 评估产出同结果——确定性评估的替身表达；envelope 自 resultSource
    // 脚本表取，两侧同源——§7.7"从报告数据源复算"）。
    scene.recompute.envelopes.emplace(run.toCanonical(),
                                      scene.build.resultSource.envelopes.at(run.toCanonical()));

    // 经接口多态消费（公共接口交付面——roundtripCheck(reportId) 原文
    // 方法名，§7.7）。
    const std::unique_ptr<IReportRoundtripChecker> checker = scene.checker();
    const RoundtripOutcome outcome = checker->roundtripCheck(report.reportId());

    // 总判定：一致→报告维持可用（§7.7 两态语义的"一致"半边）。
    EXPECT_TRUE(outcome.consistent) << "复算与报告一致→consistent";
    EXPECT_TRUE(outcome.usableForFormalConclusions)
        << "一致→报告维持可用于正式结论（§7.7/RPT-06）";
    EXPECT_EQ(outcome.reportId, report.reportId()) << "身份回执＝被检报告";
    EXPECT_EQ(outcome.reportContentIdentity, report.contentIdentity())
        << "内容身份回执＝检查基准锚";

    // 逐条目：复算可达＋一致＋零 mismatch（entries 与 resultRefs 同序同量）。
    ASSERT_EQ(outcome.entries.size(), report.resultRefs().size());
    EXPECT_TRUE(outcome.entries[0].recomputed) << "复算可达";
    EXPECT_TRUE(outcome.entries[0].consistent) << "条目一致";
    EXPECT_TRUE(outcome.entries[0].mismatches.empty()) << "一致路零 mismatch";
    ASSERT_TRUE(outcome.entries[0].recomputedPayloadDigest.has_value())
        << "复算载荷摘要回执在场（Completed envelope 必有载荷）";

    // 伴随诊断：一致→空（零诊断零噪音）。
    EXPECT_TRUE(outcome.diagnostics.empty()) << "一致路无伴随诊断";

    // 复算请求字段＝报告数据源抽取（快照/切片/评估键——§7.7"报告绑定的
    // 快照/切片与评估键"；ResultRefSnapshot 的 envelope 决定字段族）。
    ASSERT_EQ(scene.recompute.requests.size(), 1u);
    const RoundtripRecomputeRequest& req = scene.recompute.requests[0];
    EXPECT_TRUE(req.runId == run) << "请求 runId＝报告引用运行";
    EXPECT_EQ(req.evaluationKey, report.resultRefs()[0].evaluationKey) << "请求评估键";
    EXPECT_TRUE(req.snapshotId == report.resultRefs()[0].snapshotId) << "请求快照绑定";
    EXPECT_TRUE(req.sliceId == report.resultRefs()[0].sliceId) << "请求切片绑定";
    EXPECT_TRUE(req.inputBaselineId == report.resultRefs()[0].inputBaselineId)
        << "请求输入基准绑定";
    EXPECT_EQ(req.mode, report.resultRefs()[0].mode) << "请求评估模式";

    // 历史报告零修改（§7.7"不修改历史报告"——检查后重读解析值逐字段
    // 全等；PA-2 不可变历史的检查侧体现）。
    const std::optional<ReviewReport> after = scene.resolver.tryResolve(report.reportId());
    ASSERT_TRUE(after.has_value());
    IRD_EXPECT_IDENTICAL("report.contentIdentity.before.after",
                         report.contentIdentity().toCanonical(),
                         after->contentIdentity().toCanonical());
    EXPECT_TRUE(report == *after) << "检查前后报告值逐字段全等（纯查询零写路径）";
}

/**
 * §10.1 RP-ROUND-1 路②（不一致——脚本改输入·绑定面变异）：复算源返回
 * 快照身份被改的 envelope（模拟"输入已变化"）→ RoundtripMismatch：整体
 * 不一致、报告**不可用于正式结论**（usableForFormalConclusions=false——
 * acceptance 2 的标记断言）；mismatch 定位到 snapshot-id 面（expected＝
 * 报告值/actual＝复算值）；伴随诊断含 RPT-ROUNDTRIP-MISMATCH（Error 级）；
 * 历史报告零修改。
 */
TEST_F(RoundtripSuite, RP_ROUND_1_Mismatch_BindingDrift_ForbidsFormalUse)
{
    IRD_TEST_INFO(std::vector<std::string>{"RPT-06", "NFR-COR-04"},
                  {std::string{"AT-22"}}, std::nullopt);

    const core::RunId run = runAt(0x14);
    RoundtripScene scene = singleRunScene(run);
    const ReviewReport report = scene.buildAndScript();

    // 不一致路脚本（脚本改输入）：以原 envelope 为底，单点变异快照身份
    // （envelope 不可变——经 Draft 重造；evidence make() 构造边界保证
    // 合法组合）。快照漂移＝报告数据源与当前可复算事实脱节的直接形态。
    // 注意：证据清单的绑定面（manifest.snapshotId）必须与包络同名绑定
    // 一致（CompletedManifestNotCoherent——evidence 构造边界核对），
    // 故"改输入"同步改写清单绑定——漂移的是完整数据源，不是半份包络。
    // 漂移 seed 取 0x7A：scriptedCid 的铺位 nibble 只取 seed 低 4 位
    // （seed+i 后 mod 16），低 4 位与原快照身份（seed 0x10）相同即产生
    // "逐字节相同的假漂移"（复算源与报告全等→误判一致）——0x7A 低 4 位
    // ＝10≠0，保证身份真不同。
    const evidence::ResultEnvelope original =
        scene.build.resultSource.envelopes.at(run.toCanonical());
    evidence::EvidenceManifest manifest = original.evidence;
    manifest.snapshotId = test_fakes::scriptedCid(0x7A);   // ← 同步漂移：清单绑定
    evidence::ResultEnvelopeDraft drifted;
    drifted.task = original.task;
    drifted.evaluationKey = original.evaluationKey;
    drifted.evaluatorContractVersion = original.evaluatorContractVersion;
    drifted.mode = original.mode;
    drifted.snapshotId = test_fakes::scriptedCid(0x7A);   // ← 单点变异：快照身份改写
    drifted.sliceId = original.sliceId;
    drifted.inputBaselineId = original.inputBaselineId;
    drifted.caseScope = original.caseScope;
    drifted.profile = original.profile;
    drifted.outcome = original.outcome;
    drifted.engineeringStatus = original.engineeringStatus;
    drifted.evidence = std::move(manifest);
    drifted.payload = evidence::DomainPayloadDraft{original.payload->kindToken,
                                                   original.payload->canonicalBytes};
    drifted.producer = original.producer;
    scene.recompute.envelopes.emplace(run.toCanonical(),
                                      evidence::ResultEnvelope::make(std::move(drifted)));

    const RoundtripOutcome outcome = scene.checker()->roundtripCheck(report.reportId());

    // 总判定：不一致→RoundtripMismatch——报告不可用于正式结论
    // （acceptance 2 的核心标记断言，§7.7"不一致→报告不可用于正式结论"）。
    EXPECT_FALSE(outcome.consistent) << "复算与报告不一致→不一致判定";
    EXPECT_FALSE(outcome.usableForFormalConclusions)
        << "不一致→报告不可用于正式结论（RPT-06——usable 标记位）";

    // 逐条目定位：mismatch 定位到 snapshot-id 面（expected＝报告值，
    // actual＝复算值——全量不短路下其余面零命中）。
    ASSERT_EQ(outcome.entries.size(), 1u);
    EXPECT_TRUE(outcome.entries[0].recomputed);
    EXPECT_FALSE(outcome.entries[0].consistent);
    ASSERT_FALSE(outcome.entries[0].mismatches.empty());
    bool snapshotMismatchFound = false;
    for (const RoundtripFieldMismatch& m : outcome.entries[0].mismatches) {
        if (m.fieldToken == std::string(kRoundtripFieldSnapshotId)) {
            snapshotMismatchFound = true;
            IRD_EXPECT_IDENTICAL("mismatch.expected(报告侧)",
                                 m.expected,
                                 report.resultRefs()[0].snapshotId.toCanonical());
            IRD_EXPECT_IDENTICAL("mismatch.actual(复算侧)", m.actual,
                                 test_fakes::scriptedCid(0x7A).toCanonical());
        }
    }
    EXPECT_TRUE(snapshotMismatchFound) << "快照漂移必须定位到 snapshot-id 比对面";

    // 伴随诊断：RPT-ROUNDTRIP-MISMATCH 码面（Error 级——收编严重级别）。
    EXPECT_TRUE(hasDiagnostic(outcome.diagnostics, std::string(diagcodes::kRoundtripMismatch),
                              core::DiagnosticLevel::Error))
        << "不一致必须伴随 RPT-ROUNDTRIP-MISMATCH（Error）";

    // 历史报告零修改（检查后重读——值逐字段全等；PA-2）。
    const std::optional<ReviewReport> after = scene.resolver.tryResolve(report.reportId());
    ASSERT_TRUE(after.has_value());
    EXPECT_TRUE(report == *after) << "不一致判定同样零写路径（§7.7 不修改历史报告）";
}

/**
 * §10.1 RP-ROUND-1 路②补充（不一致——结论面变异）：复算 envelope 工程
 * 判定从 Feasible 漂移为 DataInsufficient（证据退化——结论面不一致形态）
 * → mismatch 定位到 engineering-status 面；usable=false（不可用于正式
 * 结论——关键结论字段不一致同样触发 RPT-06 语义）。
 */
TEST_F(RoundtripSuite, RP_ROUND_1_Mismatch_EngineeringStatusDrift_ForbidsFormalUse)
{
    IRD_TEST_INFO(std::vector<std::string>{"RPT-06"}, {}, std::nullopt);

    const core::RunId run = runAt(0x14);
    RoundtripScene scene = singleRunScene(run);
    const ReviewReport report = scene.buildAndScript();

    // 结论面变异脚本：同数据源重评估，但工程判定漂移为 DataInsufficient
    // （合法组合：dataInsufficientEnvelope 工厂经 evidence make() 构造；
    // RP-STATE-2 同源形态）。注意 DataInsufficient envelope 的缺失清单为
    // 脚本铺位——仅验证比对判定，不构成业务证明（替身边界声明）。
    scene.recompute.envelopes.emplace(
        run.toCanonical(),
        test_fakes::dataInsufficientEnvelope(run, {"kin.required-evidence"},
                                             {"复算时证据未在正式条件下产出（脚本铺位）"}));

    const RoundtripOutcome outcome = scene.checker()->roundtripCheck(report.reportId());

    EXPECT_FALSE(outcome.usableForFormalConclusions)
        << "结论面不一致→报告不可用于正式结论（RPT-06）";
    EXPECT_TRUE(hasDiagnostic(outcome.diagnostics, std::string(diagcodes::kRoundtripMismatch),
                              core::DiagnosticLevel::Error));

    // mismatch 定位到 engineering-status 面（expected=feasible → actual=
    // data-insufficient——core 词表 token 文本）。
    bool statusMismatchFound = false;
    for (const RoundtripFieldMismatch& m : outcome.entries[0].mismatches) {
        if (m.fieldToken == std::string(kRoundtripFieldEngineeringStatus)) {
            statusMismatchFound = true;
            IRD_EXPECT_IDENTICAL("mismatch.expected(feasible)", m.expected,
                                 std::string(core::toToken(core::EngineeringStatus::Feasible)));
            IRD_EXPECT_IDENTICAL(
                "mismatch.actual(data-insufficient)", m.actual,
                std::string(core::toToken(core::EngineeringStatus::DataInsufficient)));
        }
    }
    EXPECT_TRUE(statusMismatchFound) << "结论漂移必须定位到 engineering-status 比对面";
}

/**
 * 三态补充（复算不可达——§7.7 未覆盖口径的落位增量②）：复算源 nullopt
 * → 条目 recomputed=false、保守不判一致、usable=false；**不出** Error 级
 * MISMATCH 诊断（没有复算事实，不伪造不一致结论——NFR-COR-03），仅附
 * Warning 级"复算不可达"注记（同码面＋级别/文案区分两类原因）。
 */
TEST_F(RoundtripSuite, RecomputeUnavailable_ConservativeNotUsable_NoMismatchVerdict)
{
    IRD_TEST_INFO(std::vector<std::string>{"RPT-06", "NFR-COR-03"}, {}, std::nullopt);

    const core::RunId run = runAt(0x14);
    RoundtripScene scene = singleRunScene(run);
    const ReviewReport report = scene.buildAndScript();
    // 复算脚本表留空——tryRecompute 恒 nullopt（复算源未装配/通道故障的
    // 环境形态注入）。

    const RoundtripOutcome outcome = scene.checker()->roundtripCheck(report.reportId());

    // 保守判定：无法证明一致→不可用于正式结论（宁可不可用不可虚可用）。
    EXPECT_FALSE(outcome.consistent);
    EXPECT_FALSE(outcome.usableForFormalConclusions)
        << "复算不可达→保守不判可用（落位增量②）";

    // 条目：recomputed=false、consistent=false、mismatches 恒空（没有复算
    // 事实可定位差异——伪造 mismatch 明细同样违约）。
    ASSERT_EQ(outcome.entries.size(), 1u);
    EXPECT_FALSE(outcome.entries[0].recomputed);
    EXPECT_FALSE(outcome.entries[0].consistent);
    EXPECT_TRUE(outcome.entries[0].mismatches.empty()) << "不可达不得伪造 mismatch 明细";
    EXPECT_FALSE(outcome.entries[0].recomputedPayloadDigest.has_value());

    // 诊断：无 Error 级 MISMATCH（不伪造不一致结论）；有 Warning 级注记
    // （NFR-COR-03 不静默——环境形态显式提示）。
    EXPECT_FALSE(hasDiagnostic(outcome.diagnostics, std::string(diagcodes::kRoundtripMismatch),
                               core::DiagnosticLevel::Error))
        << "复算不可达不得出具 Error 级 RoundtripMismatch";
    ASSERT_TRUE(hasDiagnostic(outcome.diagnostics, std::string(diagcodes::kRoundtripMismatch),
                              core::DiagnosticLevel::Warning))
        << "复算不可达必须显式注记（Warning）";
    // 文案区分：注记 cause 含"复算不可达"（两类原因的提示级区分）。
    for (const core::DiagnosticRecord& d : outcome.diagnostics) {
        EXPECT_NE(d.cause.find("复算不可达"), std::string::npos)
            << "注记文案须明示环境形态（非数据事实）";
    }
}

/**
 * 多结果逐条定位（§7.7 逐结果复算语义）：两运行报告，其一复算不一致、
 * 另一复算一致→整体不一致＋不一致条目精确定位到对应 runId（逐条目
 * entries 与 resultRefs 同序对齐；consistent 为全称量词——任一条目不
 * 一致即整体不一致）。
 */
TEST_F(RoundtripSuite, MultiResult_PartialMismatch_LocatesOffendingRun)
{
    IRD_TEST_INFO(std::vector<std::string>{"RPT-06", "NFR-COR-04"}, {}, std::nullopt);

    const core::RunId runA = runAt(0x14);
    const core::RunId runB = runAt(0x15);
    RoundtripScene scene;
    scene.build.addRun(RunSpec{runA});
    scene.build.addRun(RunSpec{runB});
    scene.build.addKinContent(ContentSpec{});   // 绑定首运行（Populated）
    const ReviewReport report = scene.buildAndScript();
    ASSERT_EQ(report.resultRefs().size(), 2u) << "场景前置：两结果引用";

    // 复算脚本：runA 一致（同源 envelope）；runB 改切片身份（绑定面变异
    // ——仅 runB 不一致；切片漂移 seed 0x7B 低 4 位＝11≠原切片 0x30 低
    // 4 位 0——scriptedCid 只取低 4 位，同低位即假漂移）。
    scene.recompute.envelopes.emplace(runA.toCanonical(),
                                      scene.build.resultSource.envelopes.at(runA.toCanonical()));
    const evidence::ResultEnvelope originalB =
        scene.build.resultSource.envelopes.at(runB.toCanonical());
    evidence::EvidenceManifest manifestB = originalB.evidence;
    manifestB.sliceId = test_fakes::scriptedCid(0x7B);   // ← 同步漂移：清单绑定
    evidence::ResultEnvelopeDraft driftedB;
    driftedB.task = originalB.task;
    driftedB.evaluationKey = originalB.evaluationKey;
    driftedB.evaluatorContractVersion = originalB.evaluatorContractVersion;
    driftedB.mode = originalB.mode;
    driftedB.snapshotId = originalB.snapshotId;
    driftedB.sliceId = test_fakes::scriptedCid(0x7B);   // ← 单点变异：切片身份改写
    driftedB.inputBaselineId = originalB.inputBaselineId;
    driftedB.caseScope = originalB.caseScope;
    driftedB.profile = originalB.profile;
    driftedB.outcome = originalB.outcome;
    driftedB.engineeringStatus = originalB.engineeringStatus;
    driftedB.evidence = std::move(manifestB);
    driftedB.payload = evidence::DomainPayloadDraft{originalB.payload->kindToken,
                                                    originalB.payload->canonicalBytes};
    driftedB.producer = originalB.producer;
    scene.recompute.envelopes.emplace(runB.toCanonical(),
                                      evidence::ResultEnvelope::make(std::move(driftedB)));

    const RoundtripOutcome outcome = scene.checker()->roundtripCheck(report.reportId());

    // 全称量词：任一条目不一致→整体不一致＋不可用于正式结论。
    EXPECT_FALSE(outcome.consistent);
    EXPECT_FALSE(outcome.usableForFormalConclusions);
    ASSERT_EQ(outcome.entries.size(), 2u);

    // 逐条目对齐（entries 与 resultRefs 同序——构建序＝请求 resultRuns 序）：
    // runA 一致、runB 不一致且 mismatch 定位到 slice-id 面。
    const RoundtripResultEntry& entryA = outcome.entries[0];
    const RoundtripResultEntry& entryB = outcome.entries[1];
    EXPECT_TRUE(entryA.runId == runA);
    EXPECT_TRUE(entryB.runId == runB);
    EXPECT_TRUE(entryA.consistent) << "runA 复算一致";
    EXPECT_TRUE(entryA.mismatches.empty());
    EXPECT_FALSE(entryB.consistent) << "runB 复算不一致";
    bool sliceMismatchFound = false;
    for (const RoundtripFieldMismatch& m : entryB.mismatches) {
        if (m.fieldToken == std::string(kRoundtripFieldSliceId)) {
            sliceMismatchFound = true;
        }
    }
    EXPECT_TRUE(sliceMismatchFound) << "runB 变异定位到 slice-id 比对面";
    // 精确一条 MISMATCH 诊断（只对不一致条目出具——runA 零诊断噪音）。
    std::size_t mismatchDiagCount = 0;
    for (const core::DiagnosticRecord& d : outcome.diagnostics) {
        if (d.code == std::string(diagcodes::kRoundtripMismatch)
            && d.level == core::DiagnosticLevel::Error) {
            ++mismatchDiagCount;
        }
    }
    EXPECT_EQ(mismatchDiagCount, 1u) << "仅不一致条目出具 MISMATCH 诊断";
}

/**
 * caseScope 集合语义（比对面⑪）：复算 envelope 的工况集与报告一致但
 * **顺序不同**→ 仍判一致（集合语义顺序不敏感——caseScope 是覆盖集合，
 * 锚定序不构成语义；排序比对见 Roundtrip.cpp caseScopeEqual）。
 */
TEST_F(RoundtripSuite, CaseScope_OrderInsensitive_SetSemantics)
{
    IRD_TEST_INFO(std::vector<std::string>{"RPT-06"}, {}, std::nullopt);

    const core::RunId run = runAt(0x14);
    const std::vector<core::ObjectId> cases = {caseAt(0x21), caseAt(0x22), caseAt(0x23)};
    RoundtripScene scene;
    scene.build.addRun(RunSpec{run, "feasible", "current", "", {}, cases});   // 三工况
    scene.build.addKinContent(ContentSpec{});
    const ReviewReport report = scene.buildAndScript();

    // 复算脚本：同结果但工况集逆序（集合相同、顺序不同）。
    const evidence::ResultEnvelope original =
        scene.build.resultSource.envelopes.at(run.toCanonical());
    evidence::ResultEnvelopeDraft reordered;
    reordered.task = original.task;
    reordered.evaluationKey = original.evaluationKey;
    reordered.evaluatorContractVersion = original.evaluatorContractVersion;
    reordered.mode = original.mode;
    reordered.snapshotId = original.snapshotId;
    reordered.sliceId = original.sliceId;
    reordered.inputBaselineId = original.inputBaselineId;
    reordered.caseScope.caseIds = {cases[2], cases[0], cases[1]};   // 逆序同集合
    reordered.profile = original.profile;
    reordered.outcome = original.outcome;
    reordered.engineeringStatus = original.engineeringStatus;
    reordered.evidence = original.evidence;
    reordered.payload = evidence::DomainPayloadDraft{original.payload->kindToken,
                                                     original.payload->canonicalBytes};
    reordered.producer = original.producer;
    scene.recompute.envelopes.emplace(run.toCanonical(),
                                      evidence::ResultEnvelope::make(std::move(reordered)));

    const RoundtripOutcome outcome = scene.checker()->roundtripCheck(report.reportId());

    EXPECT_TRUE(outcome.consistent) << "同集合异序→一致（集合语义）";
    EXPECT_TRUE(outcome.usableForFormalConclusions);
    EXPECT_TRUE(outcome.entries[0].mismatches.empty());
}

/**
 * Usage 轨（调用方契约违约 fail-fast）：①reportId 保留值（全零）→
 * ReportError(Usage)；②reportId 不可解析（resolver 未登记）→
 * ReportError(Usage)——IReportResolver 契约同口径（解析不到＝前置违约）。
 */
TEST_F(RoundtripSuite, UsageViolations_FailFast)
{
    IRD_TEST_INFO(std::vector<std::string>{"RPT-06"}, {}, std::nullopt);

    const core::RunId run = runAt(0x14);
    RoundtripScene scene = singleRunScene(run);
    const ReviewReport report = scene.buildAndScript();
    const std::unique_ptr<IReportRoundtripChecker> checker = scene.checker();

    // ①保留值 reportId（全零＝空身份——没有可检对象）。
    EXPECT_THROW(checker->roundtripCheck(ReportId{}), ReportError)
        << "保留值 reportId＝调用方违约（Usage fail-fast）";
    try {
        checker->roundtripCheck(ReportId{});
        ADD_FAILURE() << "不可达";
    } catch (const ReportError& e) {
        EXPECT_EQ(e.code(), ReportErrorCode::Usage) << "码面＝Usage";
    }

    // ②不可解析 reportId（合法身份但 resolver 未登记）。
    const ReportId unknown = ReportId::generate();
    EXPECT_THROW(checker->roundtripCheck(unknown), ReportError);
    try {
        checker->roundtripCheck(unknown);
        ADD_FAILURE() << "不可达";
    } catch (const ReportError& e) {
        EXPECT_EQ(e.code(), ReportErrorCode::Usage) << "解析不到＝前置违约（契约同口径）";
    }

    // 对照：合法路径同检查器正常工作（Usage 轨不影响正常面）。
    scene.recompute.envelopes.emplace(run.toCanonical(),
                                      scene.build.resultSource.envelopes.at(run.toCanonical()));
    const RoundtripOutcome outcome = checker->roundtripCheck(report.reportId());
    EXPECT_TRUE(outcome.consistent) << "合法路径不受 Usage 轨影响";
}

/**
 * 复算凭据回执（§7.7"envelope digest"可观测面）：复算 envelope 载荷摘要
 * （evidence make() 对 canonicalBytes 重算的 SHA-256）随条目回执输出，
 * 且与复算 envelope 自身载荷摘要逐字节一致（凭据自洽）；报告 ResultRef-
 * Snapshot 不承载该摘要（§4.3.1 字段表无该列——不虚增报告身份面），
 * 摘要不参与一致判定（一致路在回执在场时仍然零 mismatch）。
 */
TEST_F(RoundtripSuite, PayloadDigestReceipt_MatchesRecomputedEnvelope)
{
    IRD_TEST_INFO(std::vector<std::string>{"RPT-06", "NFR-COR-04"}, {}, std::nullopt);

    const core::RunId run = runAt(0x14);
    RoundtripScene scene = singleRunScene(run);
    const ReviewReport report = scene.buildAndScript();
    const evidence::ResultEnvelope original =
        scene.build.resultSource.envelopes.at(run.toCanonical());
    scene.recompute.envelopes.emplace(run.toCanonical(), original);

    const RoundtripOutcome outcome = scene.checker()->roundtripCheck(report.reportId());
    EXPECT_TRUE(outcome.consistent);

    // 回执＝复算 envelope 的载荷摘要（逐字节一致——凭据自洽）。
    ASSERT_TRUE(original.payload.has_value());
    ASSERT_TRUE(outcome.entries[0].recomputedPayloadDigest.has_value());
    IRD_EXPECT_IDENTICAL("receipt.digest", outcome.entries[0].recomputedPayloadDigest->toCanonical(),
                         original.payload->digest.toCanonical());
}

}  // namespace
