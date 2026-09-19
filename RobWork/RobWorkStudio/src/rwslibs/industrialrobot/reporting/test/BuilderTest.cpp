/**
 * @file   BuilderTest.cpp
 * @brief  报告构建器单元测试（RPT-T05）——会话状态机/反例矩阵/引用不可解析
 *         拒绝/资格与当前性快照/零项目写/取消与冻结（契约 acceptance 1~5
 *         的构建器层用例；局部夹具自持——具名 RP-* 用例体随 RPT-T11 契约
 *         套件收口，任务卡 RPT-T05 acceptance 3 原文）。
 *
 * 设计依据：
 *   - units/reporting.md §7.1/§7.2（状态机与冻结流程）、§9.1（构建器契约）、
 *     §4.6（级别×章节合法组合矩阵——反例矩阵的语义源）、§6.2/§6.3（资格与
 *     当前性快照纪律）、§5.4（C 级保守处置——P-RPT-5）、§10.1（RP-MDL-2/
 *     RP-SCOPE-1~4/RP-CUR-3/RP-STATE-1~3 的构建器层映射——本文件用例名
 *     逐条标注对应关系）
 *   - 任务契约 tasks/foundation/RPT-T05.json acceptance 1~6
 *
 * 替身边界声明（§10.1 RP-STATE-4/任务约束§八——本文件全部用例共用）：
 *   本文件的 ScriptedResultSource/ScriptedSectionProvider/FakeQueryPort
 *   输出仅验证 reporting 构建器的契约（绑定校验/快照冻结/拒绝矩阵/零项目
 *   写/状态机），不构成任何运动学/轨迹/动力学/选型结果的业务正确性证明；
 *   合法组合的替身包络一律经 evidence ResultEnvelope::make 构造（非法组合
 *   在构造边界即被 evidence 拒绝——reporting 不自造非法样本）。唯一例外：
 *   Preview 包络无法经 make() 产出（§8.1 表 1/表 3 行 4——构造边界本就
 *   拒绝），而 Preview 拒绝（D-16/acceptance 1）的被测对象恰是 reporting
 *   构建边界对"越界样本到达注入面"的第二道闸——该用例以聚合初始化构造
 *   Preview 包络并在用例内显式注明，不构成对 evidence 校验器的替代验证。
 *
 * 线程约束：全部用例单线程（构建会话单线程——§9.1；测试环境即属主线程）。
 */

#include <sdurws/ird/reporting/Builder.hpp>

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/core/Provenance.hpp>
#include <sdurws/ird/diagnostics/DiagCodes.hpp>
#include <sdurws/ird/evidence/Envelope.hpp>
#include <sdurws/ird/evidence/Errors.hpp>
#include <sdurws/ird/evidence/Verdict.hpp>
#include <sdurws/ird/project/QueryPort.hpp>
#include <sdurws/ird/reporting/Errors.hpp>
#include <sdurws/ird/reporting/ReportModel.hpp>
#include <sdurws/ird/reporting/SectionProvider.hpp>
#include <sdurws/ird/reporting/Sections.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace sdurws::ird::reporting;
namespace core = sdurws::ird::core;
namespace evidence = sdurws::ird::evidence;
namespace project = sdurws::ird::project;
namespace diagnostics = sdurws::ird::diagnostics;

// =====================================================================
// 身份/取值辅助（确定性固定值——与 evidence EnvelopeTest 同款风格，自持
// 不共享：测试夹具不跨单元复用，避免隐式耦合）
// =====================================================================

const char* kHex32A = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
const char* kHex32B = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
const char* kHex32C = "cccccccccccccccccccccccccccccccc";
const char* kHex32D = "dddddddddddddddddddddddddddddddd";
const char* kHex32E = "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee";
const char* kHex64A = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
const char* kHex64B = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
const char* kHex64C = "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";

core::ContentIdentity cid(const char* hex64)
{
    return core::ContentIdentity::fromCanonical(std::string{"cid-"} + hex64);
}

core::ObjectId oid(const char* hex32)
{
    return core::ObjectId::fromCanonical(std::string{"obj-"} + hex32);
}

core::RunId runId(const char* hex32)
{
    return core::RunId::fromCanonical(std::string{"run-"} + hex32);
}

core::ProjectId fixedProject()
{
    return core::ProjectId::fromCanonical(std::string{"prj-"} + kHex32A);
}

core::BranchId fixedBranch()
{
    return core::BranchId::fromCanonical(std::string{"brn-"} + kHex32B);
}

core::RevisionId fixedRevision()
{
    return core::RevisionId::fromCanonical(std::string{"rev-"} + kHex32C);
}

/// 合法任务五元组（execution 分配形态；runId 由参数区分多运行场景）。
core::TaskIdentity taskOf(const core::RunId& run)
{
    core::TaskIdentity t;
    t.project = fixedProject();
    t.branch = fixedBranch();
    t.revision = fixedRevision();
    t.run = run;
    t.attempt = core::AttemptId{1};
    return t;
}

/// 锚定修订视图（seq=7——revisionSeq 断言的数据源；字段满足 §4.4.2 形态）。
project::RevisionView anchoredView()
{
    project::RevisionView view;
    view.id = fixedRevision();
    view.seq = 7;
    view.branch = fixedBranch();
    view.commandSummary = "测试锚定修订";
    view.hasUnresolvedPayload = false;
    return view;
}

// =====================================================================
// 合法替身包络（经 evidence ResultEnvelope::make 构造——§10.1 替身边界）
// =====================================================================

/// 结构有效的证据清单（三元组与包络同名绑定一致＋一条 Satisfied 项——
/// presence 纪律合格；itemId/digest 供章节绑定的"一致"比对基准）。
evidence::EvidenceManifest legalManifest(const core::ContentIdentity& snapshotId,
                                         const core::ContentIdentity& sliceId)
{
    evidence::EvidenceManifest m;
    m.snapshotId = snapshotId;
    m.sliceId = sliceId;
    m.profileId = "kin";
    m.profileVersion = "1.0.0";
    m.profileContentIdentity = cid(kHex64B);
    evidence::EvidenceItem ok;
    ok.itemId = "kin.reach-per-task-point";
    ok.status = evidence::EvidenceItemStatus::Satisfied;
    ok.artifactDigest = cid(kHex64A).bytes;   // Digest256＝ContentIdentity 字节面（同一摘要值）
    m.items = {ok};
    return m;
}

/// 表 3 行 1 合法底座：Completed×Feasible（证据清单＋工况标识齐备）。
evidence::ResultEnvelope legalFeasibleEnvelope(const core::RunId& run)
{
    evidence::ResultEnvelopeDraft d;
    d.task = taskOf(run);
    d.evaluationKey = "kin-batch-ik";
    d.evaluatorContractVersion = 7;
    d.mode = core::EvaluationMode::Verified;
    d.snapshotId = cid(kHex64A);
    d.sliceId = cid(kHex64C);
    d.inputBaselineId = cid(kHex64B);
    d.caseScope.caseIds = {oid(kHex32A)};
    d.profile.profileId = "kin";
    d.profile.version = "1.0.0";
    d.profile.contentIdentity = cid(kHex64B);
    d.outcome = core::TaskOutcome::Completed;
    d.engineeringStatus = core::EngineeringStatus::Feasible;
    d.evidence = legalManifest(d.snapshotId, d.sliceId);
    d.payload = evidence::DomainPayloadDraft{"kin.batch-ik.v1", {0x01, 0x02, 0x03}};
    d.producer.productVersion = "industrialrobot-designer 0.1.0";
    return evidence::ResultEnvelope::make(std::move(d));
}

/// 表 3 行 3 合法底座：Canceled×NotApplicable（无正式结论字段、保留诊断
/// ——TASK-02 呈现面用例的被测形态）。
evidence::ResultEnvelope legalCanceledEnvelope(const core::RunId& run)
{
    evidence::ResultEnvelopeDraft d;
    d.task = taskOf(run);
    d.evaluationKey = "kin-batch-ik";
    d.evaluatorContractVersion = 7;
    d.mode = core::EvaluationMode::Verified;
    d.snapshotId = cid(kHex64A);
    d.sliceId = cid(kHex64C);
    d.inputBaselineId = cid(kHex64B);
    d.outcome = core::TaskOutcome::Canceled;
    d.engineeringStatus = core::EngineeringStatus::NotApplicable;
    d.partialData = evidence::PartialDataRef{"runs/run-x/partial", false};
    d.diagnostics = {core::DiagnosticRecord::make(
        std::string{evidence::kDiagOutcomeNotCompleted}, std::nullopt, std::nullopt,
        std::nullopt, "verdict-aggregate", "运行被用户取消，无工程判定",
        "查看运行日志确认取消原因")};
    d.producer.productVersion = "industrialrobot-designer 0.1.0";
    return evidence::ResultEnvelope::make(std::move(d));
}

/// 合法复现块（§4.1.2——版本族齐备的最小形态）。
evidence::ReproductionBlock legalReproduction()
{
    evidence::ReproductionBlock r;
    r.productVersion = "industrialrobot-designer 0.1.0";
    r.evidenceContractVersion = "evidence-contract/1";
    r.codecVersions = {"snapshot-codec/1", "slice-codec/1"};
    return r;
}

/// 合法资格检查（五条件成立/未成立的脚本形态——unmetConditions 词表面）。
evidence::EligibilityCheck eligibleCheck(bool eligible)
{
    evidence::EligibilityCheck c;
    c.eligible = eligible;
    if (!eligible) {
        c.unmetConditions = {"status-not-feasible"};
    }
    return c;
}

/// 当前性投影脚本值（Current/Superseded/不可判定三形态——§6.3）。
evidence::CurrentnessResult currentnessScript(
    std::optional<evidence::CurrentnessStatus> status,
    std::vector<evidence::InvalidationReason> reasons = {})
{
    evidence::CurrentnessResult r;
    r.status = status;
    if (!status.has_value()) {
        r.unevaluableCause = evidence::UnevaluableCause::UnresolvedDependency;
    }
    r.reasons = std::move(reasons);
    return r;
}

// =====================================================================
// 替身一：project 查询端口（记录全部调用——零项目写断言与锚定计数的事实面）
// =====================================================================

class FakeQueryPort final : public project::IProjectQueryPort {
public:
    // 脚本数据（默认＝锚定视图在闭包内＋run1 已 finalize）。
    bool revisionExists = true;
    std::vector<project::RunInfo> runs{};
    project::RevisionView view = anchoredView();

    // 调用记录（acceptance 2/4 的零项目写＋恰一次断言）——const 方法内
    // 记账故为 mutable（测试桩观测面，产品代码无此形态）。
    mutable std::vector<std::string> callLog;   ///< 方法名调用序（head/tryRevision/listRuns/…）
    mutable int headCalls = 0;
    mutable int tryRevisionCalls = 0;
    mutable int listRunsCalls = 0;

    project::RevisionView head() const override
    {
        record("head");
        return view;
    }

    std::optional<project::RevisionView> tryRevision(core::RevisionId id) const override
    {
        record("tryRevision");
        if (!revisionExists || !(id == view.id)) {
            return std::nullopt;   // 闭包外/不存在——步① SourceMissing 触发面
        }
        return view;
    }

    std::vector<project::RunInfo> listRuns(core::RevisionId) const override
    {
        record("listRuns");
        return runs;   // 仅 finalize 口径的脚本清单（D-13——未 finalize 不列入）
    }

    // —— 以下方法构建器绝不可调用：调用即留痕判红（零项目写/零越权读的
    //    反向断言面——acceptance 2"查询端口桩零调用"；noexcept 的 tryObject
    //    以留痕＋空返回表达，避免 noexcept 内抛出导致 terminate）。 ——
    project::RevisionView revision(core::RevisionId) const override
    {
        unexpected("revision");
    }
    project::ProjectMetadataView currentMetadata() const override
    {
        unexpected("currentMetadata");
    }
    std::optional<project::ProjectMetadataView> metadataAt(core::RevisionId) const override
    {
        unexpected("metadataAt");
    }
    std::vector<project::BranchTip> branchTips() const override { unexpected("branchTips"); }
    std::vector<project::RevisionView> branchHistory(core::BranchId, std::uint32_t) const override
    {
        unexpected("branchHistory");
    }
    std::optional<std::vector<std::uint8_t>> tryObject(core::ObjectId,
                                                       core::ContentVersion) const noexcept override
    {
        record("tryObject");   // noexcept 契约下不可抛——留痕判红＋空返回
        return std::nullopt;
    }
    std::vector<std::uint8_t> object(core::ObjectId, core::ContentVersion) const override
    {
        unexpected("object");
    }
    std::vector<project::DraftInfo> listDrafts(core::BranchId) const override
    {
        unexpected("listDrafts");
    }
    std::filesystem::path runDir(core::RunId) const override { unexpected("runDir"); }

private:
    /// 调用留痕（计数与序——零项目写/锚定恰一次的正向核对面）。
    void record(const char* method) const
    {
        callLog.emplace_back(method);
        if (std::string(method) == "head") {
            ++headCalls;
        } else if (std::string(method) == "tryRevision") {
            ++tryRevisionCalls;
        } else if (std::string(method) == "listRuns") {
            ++listRunsCalls;
        }
    }

    /// 意外调用＝立即失败（以异常表达——gtest 不可用于返回值构造上下文，
    /// 测试进程捕获后判红）。
    [[noreturn]] static void unexpected(const char* method)
    {
        throw std::logic_error(std::string{"FakeQueryPort: 构建器调用了意外方法 "} + method);
    }
};

// =====================================================================
// 替身二：归档结果注入源（脚本化 envelope/资格/当前性/复现块——调用计数
// 供"不缓存跨报告复用"断言）
// =====================================================================

class ScriptedResultSource final : public IReportResultSource {
public:
    std::map<std::string, evidence::ResultEnvelope> envelopes;      ///< runId 规范文本→包络
    std::map<std::string, evidence::CurrentnessResult> currentness; ///< 同上→当前性脚本
    std::map<std::string, ReportEligibilityChecks> eligibility;     ///< 同上→资格脚本
    bool reproductionAvailable = true;                              ///< 复现块可解析开关

    // 调用计数（每 runId——"每份报告构建时重查、不缓存跨报告复用"§6.2；
    // const 接口内记账故为 mutable——测试桩观测面）。
    mutable std::map<std::string, int> envelopeCalls;
    mutable std::map<std::string, int> currentnessCalls;
    mutable std::map<std::string, int> eligibilityCalls;

    std::optional<evidence::ResultEnvelope> tryEnvelope(core::RunId run) const override
    {
        ++envelopeCalls[run.toCanonical()];
        const auto it = envelopes.find(run.toCanonical());
        if (it == envelopes.end()) {
            return std::nullopt;   // 解码失败面（§7.2 步③ SourceMissing）
        }
        return it->second;
    }

    evidence::CurrentnessResult currentnessOf(const evidence::ResultEnvelope& envelope,
                                              core::RevisionId) const override
    {
        ++currentnessCalls[envelope.task.run.toCanonical()];
        const auto it = currentness.find(envelope.task.run.toCanonical());
        return it != currentness.end() ? it->second : currentnessScript(std::nullopt);
    }

    ReportEligibilityChecks eligibilityOf(const evidence::ResultEnvelope& envelope) const override
    {
        ++eligibilityCalls[envelope.task.run.toCanonical()];
        const auto it = eligibility.find(envelope.task.run.toCanonical());
        if (it != eligibility.end()) {
            return it->second;
        }
        ReportEligibilityChecks def;
        def.formalPass = eligibleCheck(true);
        def.reviewRecord = eligibleCheck(false);
        return def;
    }

    std::optional<evidence::ReproductionBlock> tryReproduction(core::RunId) const override
    {
        return reproductionAvailable ? std::optional{legalReproduction()} : std::nullopt;
    }
};

// =====================================================================
// 替身三：章节提供方（脚本化 SectionContent——记录请求供过滤断言）
// =====================================================================

class ScriptedSectionProvider final : public IReportSectionProvider {
public:
    ScriptedSectionProvider(std::string id, ReportLevel minLevel,
                            std::vector<std::string> keys, SectionContent scripted)
        : m_id(std::move(id))
        , m_minLevel(minLevel)
        , m_keys(std::move(keys))
        , m_scripted(std::move(scripted))
    {
    }

    std::string sectionId() const override { return m_id; }
    ReportLevel minimumLevel() const override { return m_minLevel; }
    std::vector<std::string> requiredEvaluationKeys() const override { return m_keys; }

    SectionContent project(const SectionRequest& request) override
    {
        lastRequest = request;   // 记录最近请求（结果子集过滤的断言面）
        ++projectCalls;
        return m_scripted;
    }

    mutable SectionRequest lastRequest;   ///< 最近一次投影请求（过滤断言）
    mutable int projectCalls = 0;         ///< 投影调用计数

private:
    std::string m_id;
    ReportLevel m_minLevel;
    std::vector<std::string> m_keys;
    SectionContent m_scripted;
};

// =====================================================================
// 取消令牌替身（手工置位——检查点行为断言）
// =====================================================================

class ManualCancelToken final : public ReportCancelToken {
public:
    bool cancelled = false;
    bool isCancelled() const override { return cancelled; }
};

// =====================================================================
// 场景组装辅助（单一事实源——各用例单点变异）
// =====================================================================

/// 域章节（B/C 两级各一）的"缺正式结果"脚本内容（提供方声明缺项形态——
/// §5.3 提供方以 status＋missingItems 表达）。
SectionContent noFormalResultContent()
{
    SectionContent content;
    content.status = SectionStatus::NoFormalResult;
    MissingItemView missing;
    missing.itemId = "kin.required-evidence";
    missing.reason = "该域无已完成的评估运行";
    content.missingItems = {missing};
    return content;
}

/// Populated 章节脚本：一个结论条目（字段/结果绑定/证据绑定/工况/跳转——
/// §9.2 SectionEntry 全形状；绑定与 legalFeasibleEnvelope 清单一致）。
SectionContent populatedKinContent(const core::RunId& run)
{
    SectionContent content;
    content.providerContractVersion = 3;
    content.status = SectionStatus::Populated;

    SectionEntryView entry;
    entry.entryKey = "ik-converged";
    FieldValue field;
    field.key = "kin.solved-count";
    field.quantity = core::SourcedValue<double>::provided(
        1.0, core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
    entry.fields = {field};
    entry.result.runId = run;
    entry.result.fieldPath = "payload.task-points[0].converged";
    EvidenceBinding binding;
    binding.itemId = "kin.reach-per-task-point";
    binding.status = evidence::EvidenceItemStatus::Satisfied;
    binding.digest = cid(kHex64A).bytes;      // 与包络清单一致（绑定校验通过面）
    binding.caseScope = {oid(kHex32A)};
    entry.evidence = {binding};
    entry.caseScope = {oid(kHex32A)};
    entry.jump.runId = run;
    entry.jump.caseId = oid(kHex32A);
    content.entries = {entry};
    return content;
}

/// Populated 章节脚本（条目绑定指定包络清单外的证据项——EvidenceRefInvalid
/// 触发面，acceptance 1 行 7）。
SectionContent contentBindingUnknownEvidence(const core::RunId& run)
{
    SectionContent content = populatedKinContent(run);
    content.entries[0].evidence[0].itemId = "kin.not-in-manifest";
    return content;
}

/// Populated 章节脚本（证据摘要与包络清单不一致——EvidenceRefInvalid 面）。
SectionContent contentBindingWrongDigest(const core::RunId& run)
{
    SectionContent content = populatedKinContent(run);
    content.entries[0].evidence[0].digest = cid(kHex64C).bytes;   // 清单内为 kHex64A
    return content;
}

/// Populated 章节脚本（证据工况范围越界——§4.7/§4.6 行 7 面）。
SectionContent contentBindingOutOfScopeEvidence(const core::RunId& run)
{
    SectionContent content = populatedKinContent(run);
    content.entries[0].evidence[0].caseScope = {oid(kHex32E)};   // 包络 caseScope＝{kHex32A}
    return content;
}

/// Populated 章节脚本（条目绑定指定运行——供"绑定非 Completed 结果"
/// 用例把取消运行绑进结论条目）。
SectionContent contentBindingRun(const core::RunId& run)
{
    SectionContent content = populatedKinContent(run);
    content.entries[0].result.runId = run;
    content.entries[0].evidence.clear();   // 取消包络无证据清单——绑定仅结果
    return content;
}

/// 标准构建请求（B 级＋单 kin 运行——各用例单点变异）。
ReportBuildRequest standardRequest(const core::RunId& run)
{
    ReportBuildRequest request;
    request.project = fixedProject();
    request.branch = fixedBranch();
    request.revision = fixedRevision();
    request.level = ReportLevel::B;
    request.resultRuns = {run};
    return request;
}

/// 标准场景（查询端口＋结果源＋注册表——构建器注入三元组）。
struct StandardScene {
    FakeQueryPort queryPort;
    ScriptedResultSource resultSource;
    SectionRegistry registry;

    /// 默认组装：run 已 finalize＋合法 Feasible 包络＋Current 当前性。
    static StandardScene withRun(const core::RunId& run)
    {
        StandardScene scene;
        project::RunInfo info;
        info.runId = run;
        info.task = taskOf(run);
        info.runKind = "evaluation";
        info.evaluationKey = "kin-batch-ik";
        info.finalizedAtUtc = "2026-09-19T00:00:00Z";
        scene.queryPort.runs = {info};
        scene.resultSource.envelopes.emplace(run.toCanonical(), legalFeasibleEnvelope(run));
        scene.resultSource.currentness.emplace(run.toCanonical(),
                                               currentnessScript(evidence::CurrentnessStatus::Current));
        return scene;
    }

    /// 注册 kin 提供方（指定章节内容脚本）。
    void addKinProvider(SectionContent content)
    {
        registry.registerProvider(std::make_unique<ScriptedSectionProvider>(
            std::string(kSectionKinematicsCollision), ReportLevel::B,
            std::vector<std::string>{"kin-batch-ik"}, std::move(content)));
    }
};

/// 断言失败形态：report 为空＋error 码面正确（§9.1 后置"失败＝无报告对象"）。
void expectFailure(const ReportBuildOutcome& outcome, ReportErrorCode code)
{
    ASSERT_EQ(outcome.report, nullptr) << "失败路径不得产生报告对象（§4.2①）";
    ASSERT_TRUE(outcome.error.has_value());
    EXPECT_EQ(outcome.error->code(), code);
}

/// 断言取消形态：report 空＋error 缺席＋零诊断（UX-03——正常取消非错误）。
void expectCanceled(const ReportBuildOutcome& outcome)
{
    EXPECT_EQ(outcome.report, nullptr);
    EXPECT_FALSE(outcome.error.has_value());
    EXPECT_TRUE(outcome.diagnostics.empty());
}

// =====================================================================
// 会话状态机（acceptance 5——§7.1 七态＋终态不可变＋RAII 清理）
// =====================================================================

TEST(ReportBuildSessionTest, SevenStateHappyPath_AdvancesStrictly_RPT05_ACC5)
{
    // §7.1 主链逐段推进（Requested→…→Completed）＋token 稳定字面锁定。
    ReportBuildSession session;
    EXPECT_EQ(session.stage(), ReportBuildStage::Requested);
    EXPECT_FALSE(session.isTerminal());

    const std::vector<ReportBuildStage> chain = {
        ReportBuildStage::Resolving, ReportBuildStage::Building,
        ReportBuildStage::Rendering, ReportBuildStage::Verifying,
        ReportBuildStage::Publishing, ReportBuildStage::Completed,
    };
    for (const ReportBuildStage next : chain) {
        session.advanceTo(next);
    }
    EXPECT_EQ(session.stage(), ReportBuildStage::Completed);
    EXPECT_TRUE(session.isTerminal());
    // token 与状态一一对应（呈现/诊断定位面——NFR-COR-02 同码同串）。
    EXPECT_EQ(token(ReportBuildStage::Resolving), "resolving");
    EXPECT_EQ(token(ReportBuildStage::Canceled), "canceled");
}

TEST(ReportBuildSessionTest, IllegalJumpRejected_UsageFailFast_RPT05_ACC5)
{
    // 跳段（Requested→Building）与回退（Resolving→Requested）均拒绝
    //（§7.1 主链严格逐步——状态机语义单点）。
    ReportBuildSession session;
    EXPECT_THROW(session.advanceTo(ReportBuildStage::Building), ReportError);
    session.advanceTo(ReportBuildStage::Resolving);
    EXPECT_THROW(session.advanceTo(ReportBuildStage::Requested), ReportError);
    EXPECT_THROW(session.advanceTo(ReportBuildStage::Completed), ReportError);   // 跨三段
    try {
        session.advanceTo(ReportBuildStage::Requested);
    } catch (const ReportError& err) {
        EXPECT_EQ(err.code(), ReportErrorCode::Usage);   // 调用方违约 fail-fast
    }
}

TEST(ReportBuildSessionTest, TerminalStatesImmutable_RPT05_ACC5)
{
    // 终态后不可变（§7.1——Completed/Failed/Canceled 三终态上任何推进/
    // 失败/取消请求＝Usage）。
    for (const bool useFail : {true, false}) {
        ReportBuildSession session;
        session.advanceTo(ReportBuildStage::Resolving);
        if (useFail) {
            session.fail(ReportErrorCode::DataInvalid, "测试失败");
        } else {
            session.cancel();
        }
        ASSERT_TRUE(session.isTerminal());
        EXPECT_THROW(session.advanceTo(ReportBuildStage::Building), ReportError);
        EXPECT_THROW(session.fail(ReportErrorCode::Usage, "二次失败"), ReportError);
        EXPECT_THROW(session.cancel(), ReportError);
    }
    // Failed 会话的失败记录可读（终态后定位面）。
    ReportBuildSession failed;
    failed.advanceTo(ReportBuildStage::Resolving);
    failed.fail(ReportErrorCode::SourceMissing, "锚定失败");
    ASSERT_TRUE(failed.failure().has_value());
    EXPECT_EQ(failed.failure()->code(), ReportErrorCode::SourceMissing);
    // Completed 会话无失败记录。
    ReportBuildSession done;
    done.advanceTo(ReportBuildStage::Resolving);
    done.advanceTo(ReportBuildStage::Building);
    done.advanceTo(ReportBuildStage::Rendering);
    done.advanceTo(ReportBuildStage::Verifying);
    done.advanceTo(ReportBuildStage::Publishing);
    done.advanceTo(ReportBuildStage::Completed);
    EXPECT_FALSE(done.failure().has_value());
}

TEST(ReportBuildSessionTest, FailFromAnyNonTerminalStage_RPT05_ACC5)
{
    // 任一非终态可转 Failed（§7.1"任一步失败"——逐阶段验证）。
    for (const ReportBuildStage stage :
         {ReportBuildStage::Requested, ReportBuildStage::Resolving,
          ReportBuildStage::Building, ReportBuildStage::Rendering,
          ReportBuildStage::Verifying, ReportBuildStage::Publishing}) {
        ReportBuildSession session;
        // 推进到目标非终态（逐段合法推进）。
        const std::vector<ReportBuildStage> chain = {
            ReportBuildStage::Resolving, ReportBuildStage::Building,
            ReportBuildStage::Rendering, ReportBuildStage::Verifying,
            ReportBuildStage::Publishing,
        };
        for (const ReportBuildStage next : chain) {
            if (session.stage() == stage) {
                break;
            }
            session.advanceTo(next);
        }
        ASSERT_EQ(session.stage(), stage);
        session.fail(ReportErrorCode::RenderFailed, "阶段失败");
        EXPECT_EQ(session.stage(), ReportBuildStage::Failed);
        ASSERT_TRUE(session.failure().has_value());
        EXPECT_EQ(session.failure()->code(), ReportErrorCode::RenderFailed);
    }
}

TEST(ReportBuildSessionTest, DestructorGuardSmoke_NonTerminalReleaseAndTerminalSafe_RPT05_ACC5)
{
    // RAII 清理守卫冒烟（§7.1"取消→中止→清理（RAII）→Canceled"）：非终态
    // 析构安全、release 后析构零动作、终态析构零动作。守卫的内部迁移
    // （非终态→Canceled）在析构后不可观测——其行为由 cancel() 语义测试与
    // 构建器取消路径（expectCanceled）共同锁定。
    {
        ReportBuildSession abandoned;   // 非终态析构——守卫迁移 Canceled（内部）
    }
    {
        ReportBuildSession handed;
        handed.advanceTo(ReportBuildStage::Resolving);
        handed.release();               // 构建阶段交接——析构零动作
    }
    {
        ReportBuildSession terminal;
        terminal.advanceTo(ReportBuildStage::Resolving);
        terminal.cancel();              // 已终态——析构零动作
    }
    SUCCEED();
}

// =====================================================================
// 构建器正例（章节组装/快照冻结/锚定纪律/内容幂等——acceptance 3/4/5）
// =====================================================================

TEST(ReviewReportBuilderTest, BuildsBLevelReport_WithSnapshotsAndAnchoring_RPT05_ACC3_ACC4)
{
    // 正例：B 级＋单 kin Completed 运行＋kin 提供方 Populated——章节组装、
    // 资格/当前性快照冻结、锚定恰一次、零越权调用（acceptance 3/4）。
    const core::RunId run = runId(kHex32D);
    StandardScene scene = StandardScene::withRun(run);
    scene.addKinProvider(populatedKinContent(run));
    ReviewReportBuilder builder(scene.queryPort, scene.resultSource, scene.registry);

    const ReportBuildOutcome outcome = builder.build(standardRequest(run));
    ASSERT_NE(outcome.report, nullptr);
    EXPECT_FALSE(outcome.error.has_value());
    const ReviewReport& report = *outcome.report;

    // 身份三元组与级别（§4.2——明确 ID；revisionSeq 取锚定视图 seq）。
    EXPECT_TRUE(report.project() == fixedProject());
    EXPECT_TRUE(report.branch() == fixedBranch());
    EXPECT_TRUE(report.revision() == fixedRevision());
    EXPECT_EQ(report.revisionSeq(), 7u);
    EXPECT_EQ(report.level(), ReportLevel::B);
    EXPECT_TRUE(report.dataIdentity().isValid());
    EXPECT_TRUE(report.contentIdentity().isValid());
    EXPECT_EQ(report.reportVersion(), 1u);
    EXPECT_FALSE(report.supersedes().has_value());

    // 结果引用快照（§4.3.1——三轴分列＋资格/当前性冻结）。
    ASSERT_EQ(report.resultRefs().size(), 1u);
    const ResultRefSnapshot& ref = report.resultRefs()[0];
    EXPECT_TRUE(ref.runId == run);
    EXPECT_EQ(ref.outcome, core::TaskOutcome::Completed);
    EXPECT_EQ(ref.engineeringStatus, core::EngineeringStatus::Feasible);
    EXPECT_TRUE(ref.eligibility.formalPass);        // 资格快照＝evidence 纯检查结果
    EXPECT_FALSE(ref.eligibility.reviewRecord);
    ASSERT_TRUE(ref.currentness.status.has_value());
    EXPECT_EQ(*ref.currentness.status, evidence::CurrentnessStatus::Current);
    EXPECT_TRUE(ref.currentness.evaluatedAgainst.headRevision == fixedRevision());

    // 章节组装：B 级域四章节按词表序（order 严格递增——§4.7）；kin 已注册
    // Populated，其余未注册＝缺项非空壳（§5.1 词表规则）。
    const std::vector<std::string> scope = domainSectionsInScope(ReportLevel::B);
    ASSERT_EQ(report.sections().size(), scope.size());
    for (std::size_t i = 0; i < report.sections().size(); ++i) {
        const ReviewReportSection& section = report.sections()[i];
        EXPECT_EQ(section.sectionId, scope[i]);
        EXPECT_EQ(section.order, *trySectionOrder(scope[i]));
    }
    const ReviewReportSection& kin = report.sections()[2];   // 词表行 5——kinematics-collision
    ASSERT_FALSE(kin.entries.empty());
    EXPECT_EQ(kin.status, SectionStatus::Populated);
    EXPECT_TRUE(kin.selected);                               // Populated 默认选中（§5 默认规则）
    EXPECT_EQ(kin.sectionVersion, 3u);                       // 提供方契约版本透传
    ASSERT_TRUE(kin.eligibilityNote.has_value());
    EXPECT_TRUE(kin.eligibilityNote->formalPassAllowed);     // 资格聚合（所引结果成立）
    EXPECT_FALSE(kin.eligibilityNote->reviewRecordAllowed);
    const ReviewReportSection& unregistered = report.sections()[0];   // model——未注册
    EXPECT_EQ(unregistered.status, SectionStatus::NoFormalResult);
    EXPECT_FALSE(unregistered.selected);                     // 缺正式结果默认不选（§16 验收要点）
    ASSERT_EQ(unregistered.missingItems.size(), 1u);
    EXPECT_EQ(unregistered.missingItems[0].itemId, "model");
    EXPECT_EQ(unregistered.missingItems[0].reason, "章节不可用（提供方未注册）");

    // 证据引用并集（§4.2——来源章节可反向导航；清单事实为权威值）。
    ASSERT_EQ(report.evidenceRefs().size(), 1u);
    EXPECT_EQ(report.evidenceRefs()[0].itemId, "kin.reach-per-task-point");
    EXPECT_EQ(report.evidenceRefs()[0].sourceSection, "kinematics-collision");
    EXPECT_EQ(report.evidenceRefs()[0].status, evidence::EvidenceItemStatus::Satisfied);

    // 锚定纪律（acceptance 4）：head/tryRevision/listRuns 各恰一次。
    EXPECT_EQ(scene.queryPort.headCalls, 1);
    EXPECT_EQ(scene.queryPort.tryRevisionCalls, 1);
    EXPECT_EQ(scene.queryPort.listRunsCalls, 1);
    // 零项目写（acceptance 2/4）：调用日志只含三个只读方法——本测试桩对
    // 其余方法一律抛异常（调用即测试失败），日志为正向核对面。
    for (const std::string& call : scene.queryPort.callLog) {
        EXPECT_TRUE(call == "head" || call == "tryRevision" || call == "listRuns") << call;
    }
}

TEST(ReviewReportBuilderTest, ContentIdentityStableAcrossRebuilds_ReportIdDiffers_RPT05_ACC5)
{
    // 内容幂等（§7.1 逐项明确表"同一输入重复生成是否幂等＝是（内容幂等）"
    // ＋§9.1 确定性行）：同数据源重建 contentIdentity 相等、ReportId 不同
    //（新对象——§4.1 身份纪律）。
    const core::RunId run = runId(kHex32D);
    StandardScene sceneA = StandardScene::withRun(run);
    sceneA.addKinProvider(populatedKinContent(run));
    ReviewReportBuilder builderA(sceneA.queryPort, sceneA.resultSource, sceneA.registry);
    const ReportBuildOutcome first = builderA.build(standardRequest(run));
    ASSERT_NE(first.report, nullptr);

    StandardScene sceneB = StandardScene::withRun(run);
    sceneB.addKinProvider(populatedKinContent(run));
    ReviewReportBuilder builderB(sceneB.queryPort, sceneB.resultSource, sceneB.registry);
    const ReportBuildOutcome second = builderB.build(standardRequest(run));
    ASSERT_NE(second.report, nullptr);

    EXPECT_TRUE(first.report->contentIdentity() == second.report->contentIdentity());
    EXPECT_TRUE(first.report->dataIdentity() == second.report->dataIdentity());
    EXPECT_FALSE(first.report->reportId() == second.report->reportId());
}

TEST(ReviewReportBuilderTest, ResultOrderNormalized_IdentityIndependentOfRequestOrder_RPT05_ACC4)
{
    // 集合语义规范化（§4.4 resultRefs 集）：同一结果集以不同请求顺序构建
    // ＝同 dataIdentity（确定性——§9.1）。
    const core::RunId run1 = runId(kHex32D);
    const core::RunId run2 = runId(kHex32E);

    StandardScene forward = StandardScene::withRun(run1);
    forward.resultSource.envelopes.emplace(run2.toCanonical(), legalFeasibleEnvelope(run2));
    forward.resultSource.currentness.emplace(run2.toCanonical(),
                                             currentnessScript(evidence::CurrentnessStatus::Current));
    project::RunInfo info2;
    info2.runId = run2;
    info2.task = taskOf(run2);
    info2.runKind = "evaluation";
    info2.evaluationKey = "kin-batch-ik";
    info2.finalizedAtUtc = "2026-09-19T00:00:01Z";
    forward.queryPort.runs.push_back(info2);
    forward.addKinProvider(populatedKinContent(run1));
    ReviewReportBuilder builderForward(forward.queryPort, forward.resultSource, forward.registry);

    StandardScene reversed = StandardScene::withRun(run1);
    reversed.resultSource.envelopes.emplace(run2.toCanonical(), legalFeasibleEnvelope(run2));
    reversed.resultSource.currentness.emplace(run2.toCanonical(),
                                              currentnessScript(evidence::CurrentnessStatus::Current));
    reversed.queryPort.runs = {info2, reversed.queryPort.runs[0]};
    reversed.addKinProvider(populatedKinContent(run1));
    ReviewReportBuilder builderReversed(reversed.queryPort, reversed.resultSource, reversed.registry);

    ReportBuildRequest request = standardRequest(run1);
    request.resultRuns = {run1, run2};
    const ReportBuildOutcome outcomeForward = builderForward.build(request);
    ASSERT_NE(outcomeForward.report, nullptr);
    ReportBuildRequest requestReversed = request;
    requestReversed.resultRuns = {run2, run1};   // 仅请求顺序翻转
    const ReportBuildOutcome outcomeReversed = builderReversed.build(requestReversed);
    ASSERT_NE(outcomeReversed.report, nullptr);

    EXPECT_TRUE(outcomeForward.report->dataIdentity() == outcomeReversed.report->dataIdentity());
    EXPECT_TRUE(outcomeForward.report->contentIdentity()
                == outcomeReversed.report->contentIdentity());
    // resultRefs 存储序＝runId 规范字典序（渲染/CSV 输出序的确定性前提）。
    ASSERT_EQ(outcomeForward.report->resultRefs().size(), 2u);
    EXPECT_TRUE(outcomeForward.report->resultRefs()[0].runId.toCanonical()
                < outcomeForward.report->resultRefs()[1].runId.toCanonical());
}

// =====================================================================
// 反例矩阵（acceptance 1——§4.6 合法组合矩阵逐行的构建期拒绝）
// =====================================================================

TEST(ReviewReportBuilderTest, BLevelWithCExclusiveOverride_RejectedLevelConflict_RPT05_ACC1)
{
    // §4.6 行 2：B 级携带 C 专属章节＝LevelConflict（不能伪造 C 级章节）。
    const core::RunId run = runId(kHex32D);
    StandardScene scene = StandardScene::withRun(run);
    scene.addKinProvider(populatedKinContent(run));
    ReviewReportBuilder builder(scene.queryPort, scene.resultSource, scene.registry);

    ReportBuildRequest request = standardRequest(run);
    request.sectionOverrides = {SectionSelection{std::string(kSectionTrajectoryCycle), true}};
    const ReportBuildOutcome outcome = builder.build(request);
    expectFailure(outcome, ReportErrorCode::LevelConflict);
}

TEST(ReviewReportBuilderTest, CLevelWithNoCSectionContent_RejectedScopeInsufficient_RPT05_ACC1)
{
    // §4.6 行 4/§5.4（P-RPT-5 保守处置）：C 级且全部 C 章节缺正式结果＝
    // ScopeInsufficient 拒绝＋RPT-SCOPE-INSUFFICIENT 诊断＋建议生成 B 级
    //（非静默降级——诊断 recommendedAction 固定契约词）。
    const core::RunId run = runId(kHex32D);
    StandardScene scene = StandardScene::withRun(run);
    scene.addKinProvider(noFormalResultContent());   // 仅 B 域提供方，内容缺项
    ReviewReportBuilder builder(scene.queryPort, scene.resultSource, scene.registry);

    ReportBuildRequest request = standardRequest(run);
    request.level = ReportLevel::C;
    const ReportBuildOutcome outcome = builder.build(request);
    expectFailure(outcome, ReportErrorCode::ScopeInsufficient);
    // 降级建议诊断在案（NFR-COR-03 不静默——拒绝同时携带诊断）。
    ASSERT_FALSE(outcome.diagnostics.empty());
    bool found = false;
    for (const core::DiagnosticRecord& diag : outcome.diagnostics) {
        if (diag.code == std::string(diagcodes::kScopeInsufficient)) {
            found = true;
            // P-RPT-5 契约词：显式确认＋B 级重建（RPT-T04 冻结文案）。
            EXPECT_NE(diag.recommendedAction.find("B 级"), std::string::npos);
            EXPECT_NE(diag.recommendedAction.find("显式确认"), std::string::npos);
        }
    }
    EXPECT_TRUE(found) << "RPT-SCOPE-INSUFFICIENT 诊断必须随拒绝上报";
}

TEST(ReviewReportBuilderTest, CLevelWithPopulatedCSection_Builds_RPT05_ACC1)
{
    // §4.6 行 3 正例：C 级＋B/C 章节合法选择（C 专属章节有内容）→生成。
    const core::RunId run = runId(kHex32D);
    StandardScene scene = StandardScene::withRun(run);
    scene.addKinProvider(populatedKinContent(run));
    // C 专属域章节（trajectory-cycle——词表级别 C）：Populated。
    SectionContent trajectory = populatedKinContent(run);
    trajectory.entries[0].entryKey = "cycle-time";
    scene.registry.registerProvider(std::make_unique<ScriptedSectionProvider>(
        std::string(kSectionTrajectoryCycle), ReportLevel::C,
        std::vector<std::string>{"kin-batch-ik"}, std::move(trajectory)));
    ReviewReportBuilder builder(scene.queryPort, scene.resultSource, scene.registry);

    ReportBuildRequest request = standardRequest(run);
    request.level = ReportLevel::C;
    const ReportBuildOutcome outcome = builder.build(request);
    ASSERT_NE(outcome.report, nullptr);
    EXPECT_EQ(outcome.report->level(), ReportLevel::C);
    // C 级域范围＝8 章节（§5.2——B 全部＋C 追加的域八项）。
    EXPECT_EQ(outcome.report->sections().size(),
              domainSectionsInScope(ReportLevel::C).size());
}

TEST(ReviewReportBuilderTest, CLevelUserDeselectAllCSections_RejectedScopeInsufficient_RPT05_ACC1)
{
    // §4.6 行 4 选择路面：C 级零 C 章节选中（用户覆盖全部取消）＝拒绝
    //（与 make() 的 ScopeInsufficient 校验同语义——构建器前置拦截以携带
    // 降级建议诊断）。
    const core::RunId run = runId(kHex32D);
    StandardScene scene = StandardScene::withRun(run);
    scene.addKinProvider(populatedKinContent(run));
    SectionContent trajectory = populatedKinContent(run);
    trajectory.entries[0].entryKey = "cycle-time";
    scene.registry.registerProvider(std::make_unique<ScriptedSectionProvider>(
        std::string(kSectionTrajectoryCycle), ReportLevel::C,
        std::vector<std::string>{"kin-batch-ik"}, std::move(trajectory)));
    ReviewReportBuilder builder(scene.queryPort, scene.resultSource, scene.registry);

    ReportBuildRequest request = standardRequest(run);
    request.level = ReportLevel::C;
    request.sectionOverrides = {SectionSelection{std::string(kSectionTrajectoryCycle), false}};
    const ReportBuildOutcome outcome = builder.build(request);
    expectFailure(outcome, ReportErrorCode::ScopeInsufficient);
    EXPECT_FALSE(outcome.diagnostics.empty());   // 降级建议诊断随拒绝在案
}

TEST(ReviewReportBuilderTest, PreviewResultRejected_SourceMissing_RPT05_ACC1_ACC2)
{
    // §4.6 行 5/D-16：Preview 结果进入 resultRefs＝SourceMissing（构建边界
    // 显性化）。替身边界例外（本文件头注）：Preview 包络无法经 evidence
    // make() 产出（表 3 行 4）——本用例的被测对象恰是 reporting 构建边界
    // 对越界样本的第二道闸，以聚合初始化构造受控越界样本。
    const core::RunId run = runId(kHex32D);
    StandardScene scene = StandardScene::withRun(run);
    evidence::ResultEnvelope preview = legalFeasibleEnvelope(run);
    preview.mode = core::EvaluationMode::Preview;   // 受控越界（详见文件头替身边界声明）
    scene.resultSource.envelopes.clear();
    scene.resultSource.envelopes.emplace(run.toCanonical(), std::move(preview));
    ReviewReportBuilder builder(scene.queryPort, scene.resultSource, scene.registry);

    const ReportBuildOutcome outcome = builder.build(standardRequest(run));
    expectFailure(outcome, ReportErrorCode::SourceMissing);
    EXPECT_FALSE(outcome.diagnostics.empty());   // NFR-COR-03：拒绝携带诊断
}

TEST(ReviewReportBuilderTest, CanceledResultBoundIntoEntry_RejectedDataInvalid_RPT05_ACC1)
{
    // §4.6 行 6/TASK-02：Canceled 结果进入正式结论条目＝构建边界拒绝
    //（取消/失败/中断仅诊断/状态呈现）。
    const core::RunId canceled = runId(kHex32D);
    StandardScene scene = StandardScene::withRun(canceled);
    scene.resultSource.envelopes.clear();
    scene.resultSource.envelopes.emplace(canceled.toCanonical(),
                                         legalCanceledEnvelope(canceled));
    scene.addKinProvider(contentBindingRun(canceled));   // 提供方把取消运行绑进条目
    ReviewReportBuilder builder(scene.queryPort, scene.resultSource, scene.registry);

    const ReportBuildOutcome outcome = builder.build(standardRequest(canceled));
    expectFailure(outcome, ReportErrorCode::DataInvalid);
}

TEST(ReviewReportBuilderTest, CanceledResultUnbound_PresentAsStatusAndDiagnostics_RPT05_ACC3)
{
    // §6.2/RP-STATE-1 构建器层面：Canceled 结果未被条目绑定时构建成功——
    // 结果仅以状态（resultRefs 三轴）与诊断（diagRefs 携带 sourceRun）呈现。
    const core::RunId canceled = runId(kHex32D);
    StandardScene scene = StandardScene::withRun(canceled);
    scene.resultSource.envelopes.clear();
    scene.resultSource.envelopes.emplace(canceled.toCanonical(),
                                         legalCanceledEnvelope(canceled));
    scene.addKinProvider(noFormalResultContent());   // 无条目绑定取消运行
    ReviewReportBuilder builder(scene.queryPort, scene.resultSource, scene.registry);

    const ReportBuildOutcome outcome = builder.build(standardRequest(canceled));
    ASSERT_NE(outcome.report, nullptr);
    ASSERT_EQ(outcome.report->resultRefs().size(), 1u);
    EXPECT_EQ(outcome.report->resultRefs()[0].outcome, core::TaskOutcome::Canceled);
    EXPECT_EQ(outcome.report->resultRefs()[0].engineeringStatus,
              core::EngineeringStatus::NotApplicable);   // 不伪造判定（ERR-01）
    // 其诊断照常呈现（§6.2"失败可定位"——diagRefs 携带来源运行）。
    bool found = false;
    for (const DiagRefEntry& diag : outcome.report->diagRefs()) {
        if (diag.code == std::string{evidence::kDiagOutcomeNotCompleted}
            && diag.sourceRun.has_value() && *(diag.sourceRun) == canceled) {
            found = true;
        }
    }
    EXPECT_TRUE(found) << "取消结果的诊断必须进入报告（TASK-02 呈现侧）";
}

TEST(ReviewReportBuilderTest, EvidenceRefMismatchVariants_Rejected_RPT05_ACC1)
{
    // §4.6 行 7：章节证据引用与所属结果 envelope 绑定不符＝EvidenceRefInvalid
    //（防引用错位）——itemId 不在清单/摘要不一致/工况越界三变体。
    const core::RunId run = runId(kHex32D);
    const std::vector<SectionContent (*) (const core::RunId&)> variants = {
        contentBindingUnknownEvidence, contentBindingWrongDigest,
        contentBindingOutOfScopeEvidence,
    };
    for (const auto& makeContent : variants) {
        StandardScene scene = StandardScene::withRun(run);
        scene.addKinProvider(makeContent(run));
        ReviewReportBuilder builder(scene.queryPort, scene.resultSource, scene.registry);
        const ReportBuildOutcome outcome = builder.build(standardRequest(run));
        expectFailure(outcome, ReportErrorCode::EvidenceRefInvalid);
    }
}

TEST(ReviewReportBuilderTest, EntryCaseScopeOutsideResult_Rejected_RPT05_ACC1)
{
    // §4.7 关系约束：条目工况范围 ⊆ 所属结果 caseScope（引用错位族）。
    const core::RunId run = runId(kHex32D);
    StandardScene scene = StandardScene::withRun(run);
    SectionContent content = populatedKinContent(run);
    content.entries[0].caseScope = {oid(kHex32E)};   // 包络 caseScope＝{kHex32A}
    scene.addKinProvider(std::move(content));
    ReviewReportBuilder builder(scene.queryPort, scene.resultSource, scene.registry);
    const ReportBuildOutcome outcome = builder.build(standardRequest(run));
    expectFailure(outcome, ReportErrorCode::EvidenceRefInvalid);
}

TEST(ReviewReportBuilderTest, ProviderStructuredViolation_RejectedDataInvalid_RPT05_ACC1)
{
    // §9.2 结构违约由构建器拒绝：Populated 但 entries 空（firstSectionContent
    // Violation 消费面——RPT-T04 交付原语）。
    const core::RunId run = runId(kHex32D);
    StandardScene scene = StandardScene::withRun(run);
    SectionContent hollow;
    hollow.status = SectionStatus::Populated;   // entries 空——伪造"有内容"
    scene.addKinProvider(std::move(hollow));
    ReviewReportBuilder builder(scene.queryPort, scene.resultSource, scene.registry);
    const ReportBuildOutcome outcome = builder.build(standardRequest(run));
    expectFailure(outcome, ReportErrorCode::DataInvalid);
}

// =====================================================================
// 引用不可解析拒绝（acceptance 2——RP-MDL-2 构建器层映射）
// =====================================================================

TEST(ReviewReportBuilderTest, UnresolvableReferences_RejectedSourceMissing_RPT05_ACC2)
{
    // RP-MDL-2 三反例（§7.2 步①②③——listRuns 仅 finalize 口径）：修订不
    // 存在/结果未 finalize/包络解码失败 → SourceMissing＋无报告对象。
    const core::RunId run = runId(kHex32D);

    // ①修订不存在（tryRevision nullopt）。
    {
        StandardScene scene = StandardScene::withRun(run);
        scene.addKinProvider(populatedKinContent(run));
        scene.queryPort.revisionExists = false;
        ReviewReportBuilder builder(scene.queryPort, scene.resultSource, scene.registry);
        const ReportBuildOutcome outcome = builder.build(standardRequest(run));
        expectFailure(outcome, ReportErrorCode::SourceMissing);
        EXPECT_FALSE(outcome.diagnostics.empty());
    }
    // ②结果未 finalize（不在 listRuns 清单）。
    {
        StandardScene scene = StandardScene::withRun(run);
        scene.queryPort.runs.clear();   // 运行尚未 finalize——清单为空
        ReviewReportBuilder builder(scene.queryPort, scene.resultSource, scene.registry);
        const ReportBuildOutcome outcome = builder.build(standardRequest(run));
        expectFailure(outcome, ReportErrorCode::SourceMissing);
    }
    // ③包络解码失败（tryEnvelope nullopt）。
    {
        StandardScene scene = StandardScene::withRun(run);
        scene.resultSource.envelopes.clear();
        ReviewReportBuilder builder(scene.queryPort, scene.resultSource, scene.registry);
        const ReportBuildOutcome outcome = builder.build(standardRequest(run));
        expectFailure(outcome, ReportErrorCode::SourceMissing);
    }
}

TEST(ReviewReportBuilderTest, ZeroProjectWrites_OnFailureAndSuccess_RPT05_ACC2_ACC4)
{
    // 零项目写验证（§7.1 逐项明确表"生成失败是否修改原项目＝否"）：成功与
    // 失败两条路径的查询端口调用日志均只含只读方法（写路径在结构上不存在
    // ——构建器不持有任何写端口；本测试桩对其余方法调用即抛）。
    const core::RunId run = runId(kHex32D);

    StandardScene okScene = StandardScene::withRun(run);
    okScene.addKinProvider(populatedKinContent(run));
    ReviewReportBuilder okBuilder(okScene.queryPort, okScene.resultSource, okScene.registry);
    const ReportBuildOutcome okOutcome = okBuilder.build(standardRequest(run));
    ASSERT_NE(okOutcome.report, nullptr);
    EXPECT_EQ(okScene.queryPort.callLog.size(), 3u);   // head＋tryRevision＋listRuns 恰三次

    StandardScene failScene = StandardScene::withRun(run);
    failScene.queryPort.revisionExists = false;
    ReviewReportBuilder failBuilder(failScene.queryPort, failScene.resultSource, failScene.registry);
    const ReportBuildOutcome failOutcome = failBuilder.build(standardRequest(run));
    expectFailure(failOutcome, ReportErrorCode::SourceMissing);
    // 失败路径在锚定即中止——结果源零调用（"查询端口桩/FakeSink 零调用"
    // 的报告侧对应面：结果源未被消费）。
    EXPECT_TRUE(failScene.resultSource.envelopeCalls.empty());
    for (const std::string& call : failScene.queryPort.callLog) {
        EXPECT_TRUE(call == "head" || call == "tryRevision" || call == "listRuns") << call;
    }
}

TEST(ReviewReportBuilderTest, AnchoredViewIsolatedFromLateRuns_RPT05_ACC4)
{
    // 锚定视图纪律（§7.1"生成期间项目后台写入"/AT-10）：listRuns 恰一次
    // ——构建期间到达的迟到结果不入已冻结结果集（调用计数即证据：第二次
    // 查询根本不发生）。
    const core::RunId run = runId(kHex32D);
    StandardScene scene = StandardScene::withRun(run);
    scene.addKinProvider(populatedKinContent(run));
    ReviewReportBuilder builder(scene.queryPort, scene.resultSource, scene.registry);
    const ReportBuildOutcome outcome = builder.build(standardRequest(run));
    ASSERT_NE(outcome.report, nullptr);
    // 构建完成后模拟"迟到结果到达"（脚本清单追加）——已返回的报告不受
    // 影响，且不再发起任何 listRuns（快照纪律）。
    project::RunInfo lateRun;
    lateRun.runId = runId(kHex32E);
    lateRun.task = taskOf(runId(kHex32E));
    lateRun.runKind = "evaluation";
    lateRun.evaluationKey = "kin-batch-ik";
    lateRun.finalizedAtUtc = "2026-09-19T00:00:02Z";
    scene.queryPort.runs.push_back(lateRun);
    EXPECT_EQ(scene.queryPort.listRunsCalls, 1);
    ASSERT_EQ(outcome.report->resultRefs().size(), 1u);   // 迟到结果不入报告
}

// =====================================================================
// 资格与当前性快照（acceptance 3——RP-CUR-3/RP-STATE-1 构建器层映射）
// =====================================================================

TEST(ReviewReportBuilderTest, EligibilityFrozenFromPureCheck_QueriedPerBuild_RPT05_ACC3)
{
    // §6.2：资格经 evidence 纯检查取得并冻结进 ResultRefSnapshot.eligibility
    // ——不自算（构建器只冻结 eligible 位）、不缓存跨报告复用（每份报告
    // 构建时重查——两次构建各查一次）。
    const core::RunId run = runId(kHex32D);
    StandardScene scene = StandardScene::withRun(run);
    scene.resultSource.eligibility[run.toCanonical()] = [] {
        ReportEligibilityChecks checks;
        checks.formalPass = eligibleCheck(true);
        checks.reviewRecord = eligibleCheck(true);
        return checks;
    }();
    scene.addKinProvider(populatedKinContent(run));
    ReviewReportBuilder builder(scene.queryPort, scene.resultSource, scene.registry);

    const ReportBuildOutcome first = builder.build(standardRequest(run));
    ASSERT_NE(first.report, nullptr);
    EXPECT_EQ(scene.resultSource.eligibilityCalls[run.toCanonical()], 1);
    EXPECT_TRUE(first.report->resultRefs()[0].eligibility.formalPass);
    EXPECT_TRUE(first.report->resultRefs()[0].eligibility.reviewRecord);

    const ReportBuildOutcome second = builder.build(standardRequest(run));
    ASSERT_NE(second.report, nullptr);
    EXPECT_EQ(scene.resultSource.eligibilityCalls[run.toCanonical()], 2);   // 重查——不缓存
}

TEST(ReviewReportBuilderTest, UnevaluableCurrentness_NotDefaultCurrent_RPT05_ACC3)
{
    // RP-CUR-3/P-EV-4：当前性不可判定＝status=nullopt＋RPT-CURRENTNESS-
    // UNEVALUABLE 诊断——**不得默认 Current**。
    const core::RunId run = runId(kHex32D);
    StandardScene scene = StandardScene::withRun(run);
    scene.resultSource.currentness[run.toCanonical()] =
        currentnessScript(std::nullopt);   // 依赖无法解析——不可判定计算形态
    scene.addKinProvider(populatedKinContent(run));
    ReviewReportBuilder builder(scene.queryPort, scene.resultSource, scene.registry);

    const ReportBuildOutcome outcome = builder.build(standardRequest(run));
    ASSERT_NE(outcome.report, nullptr);
    const ResultRefSnapshot& ref = outcome.report->resultRefs()[0];
    EXPECT_FALSE(ref.currentness.status.has_value()) << "不可判定不得默认 Current（P-EV-4）";
    ASSERT_TRUE(ref.currentness.unevaluableNote.has_value());
    EXPECT_EQ(ref.currentness.unevaluableNote->code,
              std::string(diagcodes::kCurrentnessUnevaluable));
    // 报告级诊断并集携带同一码面（acceptance 3——RPT-CURRENTNESS-UNEVALUABLE）。
    bool found = false;
    for (const DiagRefEntry& diag : outcome.report->diagRefs()) {
        if (diag.code == std::string(diagcodes::kCurrentnessUnevaluable)) {
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST(ReviewReportBuilderTest, SupersededCurrentness_CarriesReasonsAndSectionMarker_RPT05_ACC3)
{
    // §6.3：Superseded 结果照常可被引用（历史证据有效），逐结果携带失效
    // 原因清单；引用它的章节必填 currentness（§4.3.4）。
    const core::RunId run = runId(kHex32D);
    StandardScene scene = StandardScene::withRun(run);
    evidence::InvalidationReason reason;
    reason.dependencyKey = "robot-design";
    reason.kind = evidence::InvalidationKind::ObjectContentChanged;
    reason.detail = "TCP 对象内容变化（旧→新）";
    scene.resultSource.currentness[run.toCanonical()] =
        currentnessScript(evidence::CurrentnessStatus::Superseded, {reason});
    scene.addKinProvider(populatedKinContent(run));
    ReviewReportBuilder builder(scene.queryPort, scene.resultSource, scene.registry);

    const ReportBuildOutcome outcome = builder.build(standardRequest(run));
    ASSERT_NE(outcome.report, nullptr);
    const ResultRefSnapshot& ref = outcome.report->resultRefs()[0];
    ASSERT_TRUE(ref.currentness.status.has_value());
    EXPECT_EQ(*ref.currentness.status, evidence::CurrentnessStatus::Superseded);
    ASSERT_EQ(ref.currentness.reasons.size(), 1u);
    EXPECT_EQ(ref.currentness.reasons[0].kind, evidence::InvalidationKind::ObjectContentChanged);
    // 章节引用 Superseded 结果——章节级当前性必填（构建器冻结面）。
    const ReviewReportSection& kin = outcome.report->sections()[2];
    ASSERT_TRUE(kin.currentness.has_value());
    ASSERT_TRUE(kin.currentness->status.has_value());
    EXPECT_EQ(*kin.currentness->status, evidence::CurrentnessStatus::Superseded);
}

// =====================================================================
// 取消（acceptance 5——检查点与 UX-03 正常取消非错误）
// =====================================================================

TEST(ReviewReportBuilderTest, CancelAtResultCheckpoint_NoReportNoError_RPT05_ACC5)
{
    // 取消检查点（结果解析粒度）：令牌置位后构建即中止——无报告、无错误
    //（UX-03 正常取消非错误）、结果源零调用（检查点先于逐结果解析）。
    const core::RunId run = runId(kHex32D);
    StandardScene scene = StandardScene::withRun(run);
    scene.addKinProvider(populatedKinContent(run));
    ReviewReportBuilder builder(scene.queryPort, scene.resultSource, scene.registry);

    ManualCancelToken token;
    token.cancelled = true;
    const ReportBuildOutcome outcome = builder.build(standardRequest(run), &token);
    expectCanceled(outcome);
    EXPECT_TRUE(scene.resultSource.envelopeCalls.empty());   // 结果解析未开始
}

TEST(ReviewReportBuilderTest, CancelBetweenSections_StopsWithoutError_RPT05_ACC5)
{
    // 取消检查点（章节解析粒度）：检查点位于逐章节解析开头（§7.1 取消行）
    // ——构建器按 §5.1 词表序推进（requirements 行 4 先于 kinematics-collision
    // 行 5），故以 requirements 投影完成后作为置位观察点；其后的章节
    //（kinematics-collision）解析应被取消检查点拦下、不再投影。
    const core::RunId run = runId(kHex32D);
    StandardScene scene = StandardScene::withRun(run);
    // 词表序在前的已注册章节（requirements——B 域行 4）：首投影后令牌置位。
    auto reqProvider = std::make_unique<ScriptedSectionProvider>(
        std::string(kSectionRequirements), ReportLevel::B,
        std::vector<std::string>{"kin-batch-ik"}, noFormalResultContent());
    ScriptedSectionProvider* reqPtr = reqProvider.get();
    scene.registry.registerProvider(std::move(reqProvider));
    // 词表序在后的已注册章节（kinematics-collision——B 域行 5）：取消后
    // 不应再被投影。
    auto kinProvider = std::make_unique<ScriptedSectionProvider>(
        std::string(kSectionKinematicsCollision), ReportLevel::B,
        std::vector<std::string>{"kin-batch-ik"}, populatedKinContent(run));
    ScriptedSectionProvider* kinPtr = kinProvider.get();
    scene.registry.registerProvider(std::move(kinProvider));

    // requirements 投影一次即取消（§7.1 检查点粒度＝逐章节——下一章节
    // 解析前中止）。
    class CancelAfterFirst : public ReportCancelToken {
    public:
        explicit CancelAfterFirst(ScriptedSectionProvider* watched)
            : m_watched(watched)
        {
        }
        bool isCancelled() const override
        {
            // requirements 提供方已被调用（首个已注册章节投影完成）→ 取消。
            return m_watched->projectCalls >= 1;
        }

    private:
        ScriptedSectionProvider* m_watched;
    };
    CancelAfterFirst token(reqPtr);
    ReviewReportBuilder builder(scene.queryPort, scene.resultSource, scene.registry);

    const ReportBuildOutcome outcome = builder.build(standardRequest(run), &token);
    expectCanceled(outcome);
    EXPECT_EQ(reqPtr->projectCalls, 1);   // 首章节已投影
    EXPECT_EQ(kinPtr->projectCalls, 0);   // 后续章节解析被取消检查点拦下
}

// =====================================================================
// 请求面校验与评审演化（§9.1 前置/维度表——Usage 面）
// =====================================================================

TEST(ReviewReportBuilderTest, MalformedRequests_RejectedUsage_RPT05_ACC1)
{
    const core::RunId run = runId(kHex32D);
    StandardScene scene = StandardScene::withRun(run);
    ReviewReportBuilder builder(scene.queryPort, scene.resultSource, scene.registry);

    // 空结果集（≥1——§9.1）。
    {
        ReportBuildRequest request = standardRequest(run);
        request.resultRuns.clear();
        expectFailure(builder.build(request), ReportErrorCode::Usage);
    }
    // 重复 runId（结果集语义歧义）。
    {
        ReportBuildRequest request = standardRequest(run);
        request.resultRuns = {run, run};
        expectFailure(builder.build(request), ReportErrorCode::Usage);
    }
    // 词表外章节覆盖（§5.1 词表冻结）。
    {
        ReportBuildRequest request = standardRequest(run);
        request.sectionOverrides = {SectionSelection{"not-a-section", true}};
        expectFailure(builder.build(request), ReportErrorCode::Usage);
    }
    // 空 revision 占位（SourceAmbiguous——明确 ID 纪律）。
    {
        ReportBuildRequest request = standardRequest(run);
        request.revision = core::RevisionId{};
        expectFailure(builder.build(request), ReportErrorCode::SourceAmbiguous);
    }
    // 报告级快照与逐结果不一致（§7.2 步⑤——SourceAmbiguous）。
    {
        ReportBuildRequest request = standardRequest(run);
        request.snapshotId = cid(kHex64C);   // 包络快照＝kHex64A
        expectFailure(builder.build(request), ReportErrorCode::SourceAmbiguous);
    }
}

TEST(ReviewReportBuilderTest, ReviewEvolution_SeedValidatedAndChainExtended_RPT05_ACC5)
{
    // §9.1 评审演化：种子 dataIdentity 与本次一致→supersedes/版本+1；
    // 不一致→Usage（数据已变，须按新报告处理）。
    const core::RunId run = runId(kHex32D);
    StandardScene scene = StandardScene::withRun(run);
    scene.addKinProvider(populatedKinContent(run));
    ReviewReportBuilder builder(scene.queryPort, scene.resultSource, scene.registry);

    // 首版（全新报告——版本 1、无 supersedes）。
    const ReportBuildOutcome first = builder.build(standardRequest(run));
    ASSERT_NE(first.report, nullptr);

    // 一致种子：演化链新版本（dataIdentity 不变——同数据基准的评审演化）。
    ReportBuildRequest evolution = standardRequest(run);
    ReviewMetadataSeed seed;
    seed.priorReportId = first.report->reportId();
    seed.priorDataIdentity = first.report->dataIdentity();
    seed.priorReportVersion = first.report->reportVersion();
    seed.metadata.basisRevision = fixedRevision();
    seed.metadata.comments.push_back(ReviewComment{"评审人", std::chrono::system_clock::now(),
                                                   "意见一条", std::nullopt});
    evolution.reviewSeed = seed;
    const ReportBuildOutcome second = builder.build(evolution);
    ASSERT_NE(second.report, nullptr);
    ASSERT_TRUE(second.report->supersedes().has_value());
    EXPECT_TRUE(*(second.report->supersedes()) == first.report->reportId());
    EXPECT_EQ(second.report->reportVersion(), 2u);
    EXPECT_TRUE(second.report->dataIdentity() == first.report->dataIdentity());   // 数据基准未变
    EXPECT_FALSE(second.report->contentIdentity() == first.report->contentIdentity());   // 元数据入身份

    // 不一致种子：dataIdentity 申报错误→Usage（§9.1 维度表）。
    ReportBuildRequest stale = standardRequest(run);
    ReviewMetadataSeed badSeed = seed;
    badSeed.priorDataIdentity = cid(kHex64C);   // 非本次数据源身份
    stale.reviewSeed = badSeed;
    expectFailure(builder.build(stale), ReportErrorCode::Usage);
}

}  // namespace
