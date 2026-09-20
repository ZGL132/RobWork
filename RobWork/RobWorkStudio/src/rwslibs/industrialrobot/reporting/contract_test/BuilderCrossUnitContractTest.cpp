/**
 * @file   BuilderCrossUnitContractTest.cpp
 * @brief  构建器跨单元契约测试（RPT-T05）——注入面类型恒等（evidence 包络/
 *         当前性投影/project 查询端口）＋RPT-* 呈现码注册表一致性＋跨单元
 *         协作装配（acceptance 6：EV-T08/PRJ-T09 就绪声明与 IReportResultSource
 *         注入隔离的常驻自证）。
 *
 * 设计依据：
 *   - units/reporting.md §3.2（C-2 evidence 资格纯检查与当前性投影消费、
 *     C-4 project 查询端口消费——P-RPT-9 基线＝各卡 v0.1 Draft）、§3.3
 *     （结果信封读取经 IReportResultSource 注入隔离——envelope 归档解码
 *     入口缺位〔R-3〕的处置面）、§9.1（注入源签名——实现建议的落位增量
 *     见单元卡 §14.4 v0.8）、§3.5（RPT-* 码清单——码值权威＝diagnostics
 *     StableCodeRegistry）
 *   - 任务契约 tasks/foundation/RPT-T05.json acceptance 6（跨单元依赖就绪
 *     声明：EV-T08 公共头可用性、PRJ-T09 公共头可用性、本任务不自建解码
 *     器、不私建编译边——四条登记链接边之外的零边由 LinkageContractTest
 *     常驻自证，本文件补注入面的类型恒等面）
 *
 * 契约声明（acceptance 6 逐项）：
 *   ①EV-T08 就绪：evidence::CurrentnessResult（Currentness.hpp——EV-T08
 *     交付契约）即 IReportResultSource::currentnessOf 的返回类型——类型
 *     恒等由 static_assert 钉死，evidence 冻结出 diff 时本文件编译失败
 *     即增量同步触发器（P-RPT-9 处置口径）；
 *   ②PRJ-T09 就绪：project::IProjectQueryPort（QueryPort.hpp——PRJ-T09
 *     落位）即构建器锚定/运行清单消费面——构建器构造签名持其 const 引用
 *     （只读消费——零项目写的类型级表达）；
 *   ③解码隔离：IReportResultSource 为唯一 envelope 来源——本文件用最小
 *     脚本源证明构建器经注入面取得包络（不自建解码器）；
 *   ④不私建编译边：本目标链接面与产品目标同源（sdurws_ird_reporting 的
 *     四条登记边传染）——LinkageContractTest 既有用例常驻复核。
 */

#include <sdurws/ird/reporting/Builder.hpp>

#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/diagnostics/DiagCodes.hpp>
#include <sdurws/ird/evidence/Currentness.hpp>
#include <sdurws/ird/evidence/Envelope.hpp>
#include <sdurws/ird/evidence/Snapshot.hpp>
#include <sdurws/ird/evidence/Verdict.hpp>
#include <sdurws/ird/project/QueryPort.hpp>
#include <sdurws/ird/reporting/Errors.hpp>
#include <sdurws/ird/reporting/ReportModel.hpp>
#include <sdurws/ird/reporting/SectionProvider.hpp>
#include <sdurws/ird/reporting/Sections.hpp>

#include "../test/ScriptedResultSource.hpp"   // 具名替身（RPT-T11 收敛——测试目录头，测试目标内消费）

#include <gtest/gtest.h>

#include <map>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using namespace sdurws::ird::reporting;
namespace core = sdurws::ird::core;
namespace evidence = sdurws::ird::evidence;
namespace project = sdurws::ird::project;
namespace diagnostics = sdurws::ird::diagnostics;
namespace test_fakes = sdurws::ird::reporting::test_fakes;

// =====================================================================
// 类型恒等断言（acceptance 6①②——跨单元类型零复制/零重定义，O-13/O-24）
// =====================================================================

// ①EV-T08 交付面：currentnessOf 的返回类型恰为 evidence 当前性投影结果
//（含不可判定计算形态 status=optional——P-EV-4 的类型级承载）。
using CurrentnessOfFn = evidence::CurrentnessResult (IReportResultSource::*)(
    const evidence::ResultEnvelope&, core::RevisionId) const;
static_assert(std::is_same<decltype(&IReportResultSource::currentnessOf), CurrentnessOfFn>::value,
              "currentnessOf 必须返回 evidence::CurrentnessResult（EV-T08 交付契约——零重定义）");

// envelope 只读值注入面：tryEnvelope 返回 evidence::ResultEnvelope 的
// optional（只读值语义——§9.1 原文签名）。
using TryEnvelopeFn = std::optional<evidence::ResultEnvelope> (IReportResultSource::*)(
    core::RunId) const;
static_assert(std::is_same<decltype(&IReportResultSource::tryEnvelope), TryEnvelopeFn>::value,
              "tryEnvelope 必须返回 evidence::ResultEnvelope 只读值（§9.1——零重定义）");

// ②PRJ-T09 交付面：构建器构造持查询端口 const 引用（只读消费——零项目写
// 的类型级表达；不持有任何写端口引用）。
// 签名钉死双面断言（成员指针类型携带所属类——ReviewReportBuilder 为 final，
// 其 &build 的类型是"派生类成员指针"，与接口成员指针做 is_same 恒为假，
// 故拆为两面：接口面钉 §9.1 契约形状；实现面钉实现形状——两者的关联由
// Builder.hpp 中 override 关键字在编译期强制，任一面参数漂移即编译失败）。
using InterfaceBuildFn = ReportBuildOutcome (IReviewReportBuilder::*)(
    const ReportBuildRequest&, const ReportCancelToken*, IReportProgressSink*);
static_assert(std::is_same<decltype(&IReviewReportBuilder::build), InterfaceBuildFn>::value,
              "IReviewReportBuilder::build 接口签名与 §9.1 契约逐参一致");
using ImplBuildFn = ReportBuildOutcome (ReviewReportBuilder::*)(
    const ReportBuildRequest&, const ReportCancelToken*, IReportProgressSink*);
static_assert(std::is_same<decltype(&ReviewReportBuilder::build), ImplBuildFn>::value,
              "build 实现签名与 §9.1 契约逐参一致（override 关联接口面）");

// 资格纯检查结果对：直接消费 evidence::EligibilityCheck（§7.2 共用结构——
// unmetConditions 词表归 evidence，reporting 零复制）。
static_assert(std::is_same<decltype(&ReportEligibilityChecks::formalPass),
                           evidence::EligibilityCheck ReportEligibilityChecks::*>::value,
              "资格对成员必须恰为 evidence::EligibilityCheck（§7.2 纯检查结果）");

// =====================================================================
// 最小跨单元替身（本文件自持——与单元测试替身职责不同：这里验证的是
// "构建器经注入面协作"，非业务内容）
// =====================================================================

/// 查询端口最小实现（project 接口——head/tryRevision/listRuns 三只读方法
/// 脚本化；其余方法抛异常＝越权调用判红）。
class MinimalQueryPort final : public project::IProjectQueryPort {
public:
    project::RevisionView view{};
    std::vector<project::RunInfo> runs{};

    project::RevisionView head() const override { return view; }
    std::optional<project::RevisionView> tryRevision(core::RevisionId id) const override
    {
        if (!(id == view.id)) {
            return std::nullopt;
        }
        return view;
    }
    std::vector<project::RunInfo> listRuns(core::RevisionId) const override { return runs; }
    project::RevisionView revision(core::RevisionId) const override
    {
        throw std::logic_error("contract: 未预期调用");
    }
    project::ProjectMetadataView currentMetadata() const override
    {
        throw std::logic_error("contract: 未预期调用");
    }
    std::optional<project::ProjectMetadataView> metadataAt(core::RevisionId) const override
    {
        throw std::logic_error("contract: 未预期调用");
    }
    std::vector<project::BranchTip> branchTips() const override
    {
        throw std::logic_error("contract: 未预期调用");
    }
    std::vector<project::RevisionView> branchHistory(core::BranchId, std::uint32_t) const override
    {
        throw std::logic_error("contract: 未预期调用");
    }
    std::optional<std::vector<std::uint8_t>> tryObject(core::ObjectId,
                                                       core::ContentVersion) const noexcept override
    {
        return std::nullopt;
    }
    std::vector<std::uint8_t> object(core::ObjectId, core::ContentVersion) const override
    {
        throw std::logic_error("contract: 未预期调用");
    }
    std::vector<project::DraftInfo> listDrafts(core::BranchId) const override
    {
        throw std::logic_error("contract: 未预期调用");
    }
    std::filesystem::path runDir(core::RunId) const override
    {
        throw std::logic_error("contract: 未预期调用");
    }
};

/// 结果源（唯一 envelope 注入面——解码隔离的自证形态）。RPT-T11 收敛：
/// 局部 MinimalResultSource 拆除，消费具名正本 ScriptedResultSource
/// （test/ScriptedResultSource.hpp——envelope 脚本表；缺条目＝解码失败，
/// 与原局部类"envelope 缺席＝解码失败"语义一致；缺省行为＝Current/
/// 资格成立/复现块可解析，与原最小实现同向）。
using MinimalResultSource = test_fakes::ScriptedResultSource;

// =====================================================================
// 用例
// =====================================================================

TEST(ReportingBuilderContract, InjectedSourcesCarryEvidenceAndProjectTypes_RPT05_ACC6)
{
    // 跨单元协作装配：project 端口（PRJ-T09 接口）＋evidence 包络（经其
    // 构造边界 make 产出）经注入面进入构建器——类型恒等（上面 static_assert）
    // 的运行期面：构建成功且报告结果引用与注入包络身份一致。
    core::TaskIdentity task;
    task.project = core::ProjectId::fromCanonical(
        std::string{"prj-"} + "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    task.branch = core::BranchId::fromCanonical(
        std::string{"brn-"} + "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    task.revision = core::RevisionId::fromCanonical(
        std::string{"rev-"} + "cccccccccccccccccccccccccccccccc");
    task.run = core::RunId::fromCanonical(
        std::string{"run-"} + "dddddddddddddddddddddddddddddddd");
    task.attempt = core::AttemptId{1};

    evidence::ResultEnvelopeDraft draft;
    draft.task = task;
    draft.evaluationKey = "kin-batch-ik";
    draft.evaluatorContractVersion = 7;
    draft.mode = core::EvaluationMode::Verified;
    draft.snapshotId = core::ContentIdentity::fromCanonical(
        std::string{"cid-"} + std::string(64, 'a'));
    draft.sliceId = core::ContentIdentity::fromCanonical(
        std::string{"cid-"} + std::string(64, 'c'));
    draft.inputBaselineId = core::ContentIdentity::fromCanonical(
        std::string{"cid-"} + std::string(64, 'b'));
    draft.caseScope.caseIds = {core::ObjectId::fromCanonical(
        std::string{"obj-"} + "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")};
    draft.profile.profileId = "kin";
    draft.profile.version = "1.0.0";
    draft.profile.contentIdentity = draft.snapshotId;
    draft.outcome = core::TaskOutcome::Completed;
    draft.engineeringStatus = core::EngineeringStatus::Feasible;
    evidence::EvidenceManifest manifest;
    manifest.snapshotId = draft.snapshotId;
    manifest.sliceId = draft.sliceId;
    manifest.profileId = "kin";
    manifest.profileVersion = "1.0.0";
    manifest.profileContentIdentity = draft.profile.contentIdentity;
    evidence::EvidenceItem item;
    item.itemId = "kin.reach-per-task-point";
    item.status = evidence::EvidenceItemStatus::Satisfied;
    item.artifactDigest = draft.snapshotId.bytes;
    manifest.items = {item};
    draft.evidence = manifest;
    draft.payload = evidence::DomainPayloadDraft{"kin.batch-ik.v1", {0x01}};
    draft.producer.productVersion = "contract";

    MinimalQueryPort port;
    port.view.id = task.revision;
    port.view.seq = 1;
    port.view.branch = task.branch;
    project::RunInfo info;
    info.runId = task.run;
    info.task = task;
    info.runKind = "evaluation";
    info.evaluationKey = draft.evaluationKey;
    info.finalizedAtUtc = "2026-09-19T00:00:00Z";
    port.runs = {info};

    MinimalResultSource source;
    source.envelopes.emplace(task.run.toCanonical(),
                             evidence::ResultEnvelope::make(std::move(draft)));

    SectionRegistry registry;   // 空注册表——全部域章节缺项（B 级无范围拒绝）
    ReviewReportBuilder builder(port, source, registry);

    ReportBuildRequest request;
    request.project = task.project;
    request.branch = task.branch;
    request.revision = task.revision;
    request.level = ReportLevel::B;
    request.resultRuns = {task.run};
    const ReportBuildOutcome outcome = builder.build(request);

    ASSERT_NE(outcome.report, nullptr);
    ASSERT_EQ(outcome.report->resultRefs().size(), 1u);
    EXPECT_TRUE(outcome.report->resultRefs()[0].snapshotId
                == source.envelopes.begin()->second.snapshotId);
    EXPECT_TRUE(outcome.report->resultRefs()[0].eligibility.formalPass);
}

TEST(ReportingBuilderContract, RptPresentationCodesRegisteredInStableCodeRegistry_RPT05_ACC6)
{
    // RPT-* 呈现码（构建器上报面：RPT-CURRENTNESS-UNEVALUABLE/RPT-SOURCE-
    // MISSING/RPT-SCOPE-INSUFFICIENT）与 diagnostics StableCodeRegistry 收编
    // 表一致（码值权威——PA-1；与 RPT-T02 契约测试同款常驻自证形态）。
    diagnostics::StableCodeRegistry registry;
    diagnostics::registerBuiltinCodes(registry);
    registry.seal();

    const diagnostics::CodeDescriptor* unevaluable =
        registry.find(std::string{diagcodes::kCurrentnessUnevaluable});
    ASSERT_NE(unevaluable, nullptr);
    EXPECT_EQ(unevaluable->severity, diagnostics::DiagnosticSeverity::Warning);
    EXPECT_TRUE(unevaluable->reportable);   // P-EV-4 呈现义务——码必须可进报告

    const diagnostics::CodeDescriptor* sourceMissing =
        registry.find(std::string{diagcodes::kSourceMissing});
    ASSERT_NE(sourceMissing, nullptr);
    EXPECT_TRUE(sourceMissing->reportable);

    const diagnostics::CodeDescriptor* scopeInsufficient =
        registry.find(std::string{diagcodes::kScopeInsufficient});
    ASSERT_NE(scopeInsufficient, nullptr);
    EXPECT_EQ(scopeInsufficient->severity, diagnostics::DiagnosticSeverity::Warning);
}

TEST(ReportingBuilderContract, DecodeIsolatedViaResultSource_NoOwnDecoder_RPT05_ACC6)
{
    // 解码隔离（§3.3/R-3）：结果源不可解析时构建器以 SourceMissing 拒绝——
    // reporting 侧不存在第二解码路径（注入面缺席即失败，不静默降级）。
    MinimalQueryPort port;
    core::RevisionId revision = core::RevisionId::fromCanonical(
        std::string{"rev-"} + "cccccccccccccccccccccccccccccccc");
    port.view.id = revision;
    port.view.seq = 1;
    MinimalResultSource source;   // envelope 缺席＝解码失败（空脚本表——缺条目即 nullopt）

    SectionRegistry registry;
    ReviewReportBuilder builder(port, source, registry);

    ReportBuildRequest request;
    request.project = core::ProjectId::fromCanonical(
        std::string{"prj-"} + "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    request.branch = core::BranchId::fromCanonical(
        std::string{"brn-"} + "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    request.revision = revision;
    request.level = ReportLevel::B;
    request.resultRuns = {core::RunId::fromCanonical(
        std::string{"run-"} + "dddddddddddddddddddddddddddddddd")};

    // 运行不在 finalize 清单——步② SourceMissing（先于包络解码）。
    const ReportBuildOutcome outcome = builder.build(request);
    ASSERT_EQ(outcome.report, nullptr);
    ASSERT_TRUE(outcome.error.has_value());
    EXPECT_EQ(outcome.error->code(), ReportErrorCode::SourceMissing);
}

}  // namespace
