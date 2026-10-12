/**
 * @file   LevelCEndToEndTest.cpp
 * @brief  C 级扩展与变体报告端到端集成测试（RPT-T15）——AT-32（C 级完整
 *         工程案例逐章追溯）＋RP-REVW-1/2（评审元数据演化/未签署可导出）＋
 *         RP-COV-1（多工况覆盖显示）＋EVI-02（变体基准一致——不一致即拒绝）。
 *
 * 设计依据：
 *   - units/reporting.md §5.1 词表行 9~14（C 专属六章节——域四项走注册、
 *     框架两项 review-signoff/variant-diff 内建投影）、§4.5（ReviewMetadata
 *     ——签署/意见/改型差异/演化链；"评审元数据的任何修改＝构建新报告对象，
 *     supersedes 链，dataIdentity 不变"）、§6.5（多工况覆盖显示——EVI-02
 *     呈现口径：漏验即整体缺项呈现，不得输出正式通过；覆盖完备性结论）、
 *     §5.4（C 级缺必需证据时的降级与拒绝——P-RPT-5 保守处置）
 *   - 需求 RPT-01-C（C 级追加章节＋评审/签署元数据＋改型差异及取舍理由）、
 *     RPT-04（方案/设计变体报告：引用一致的需求与工况基准）、EVI-02（方案
 *     比较只能使用一致的需求与工况基准）、AT-32（完整工程案例逐章追溯到
 *     结果、工况与快照；章节完整性含必验工况覆盖核对）
 *   - 任务契约 tasks/foundation/RPT-T15.json（acceptance 1/2；验证命令
 *     verify＝sdurws_ird_reporting_test 构建与运行）
 *
 * 真实链路口径（与 LevelBEndToEndTest 同源，RP-STATE-4 边界的执行面）：
 *   - "真实"指**数据流真实**：C 域章节条目由提供方适配器消费
 *     SectionRequest.results 的真实字段（runId/工况集/证据清单）投影，
 *     构建器做完整绑定校验后冻结；envelope 一律经 evidence
 *     ResultEnvelope::make 构造边界产出（合法组合-only）；覆盖矩阵为
 *     evidence 汇总层的呈现投影源（§6.6），脚本值只驱动 reporting 的注入
 *     消费与报告级聚合契约——**不构成**覆盖判定语义的业务正确性证明
 *     （判定权威在 evidence 汇总门禁②级，§2.1 C-2）；
 *   - 框架章节投影器（BuiltinFrameworkSections 的 review-signoff/
 *     variant-diff 两投影）为产品代码——本文件直接消费其真实输出。
 *
 * 线程约束：单线程（构建会话单线程——§9.1；测试环境即属主线程）。
 */

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/core/Provenance.hpp>
#include <sdurws/ird/evidence/Envelope.hpp>
#include <sdurws/ird/evidence/Evidence.hpp>
#include <sdurws/ird/evidence/Snapshot.hpp>
#include <sdurws/ird/evidence/Verdict.hpp>
#include <sdurws/ird/project/QueryPort.hpp>
#include <sdurws/ird/reporting/Builder.hpp>
#include <sdurws/ird/reporting/Errors.hpp>
#include <sdurws/ird/reporting/Export.hpp>
#include <sdurws/ird/reporting/FrameworkSections.hpp>
#include <sdurws/ird/reporting/ModelSummary.hpp>
#include <sdurws/ird/reporting/Render.hpp>
#include <sdurws/ird/reporting/ReportModel.hpp>
#include <sdurws/ird/reporting/SectionProvider.hpp>
#include <sdurws/ird/reporting/Sections.hpp>

#include "ReportingScenario.hpp"      // rp_scenario 场景设施（ExportScene/TempDir 等）
#include "ScriptedResultSource.hpp"   // 具名替身正本（IReportResultSource 脚本面＋覆盖矩阵脚本）

namespace {

using namespace sdurws::ird::reporting;
namespace core = sdurws::ird::core;
namespace evidence = sdurws::ird::evidence;
namespace project = sdurws::ird::project;
using test_fakes::ScriptedResultSource;

// =====================================================================
// 确定性身份铺位（禁随机——同输入同 contentIdentity 的断言前提）
// =====================================================================

core::ProjectId cProject()
{
    return test_fakes::scriptedId<core::ProjectId>(0x11);
}
core::BranchId cBranch()
{
    return test_fakes::scriptedId<core::BranchId>(0x12);
}
core::RevisionId cRevision()
{
    return test_fakes::scriptedId<core::RevisionId>(0x13);
}
core::RunId cRun(std::uint8_t seed)
{
    return test_fakes::scriptedId<core::RunId>(seed);
}
core::ObjectId cCase(std::uint8_t seed)
{
    return test_fakes::scriptedId<core::ObjectId>(seed);
}

/// 标准双必验工况（矩阵/包络 caseScope 的对齐铺位——漏验/全覆盖变异的
/// 共同分母）。
std::vector<core::ObjectId> standardCases()
{
    return {cCase(0x21), cCase(0x22)};
}

// =====================================================================
// 查询端口测试桩（锚定视图＋锚定元数据——project 契约类型的真实组装）
// =====================================================================

class CLevelQueryPort final : public project::IProjectQueryPort {
public:
    project::RevisionView view{};
    project::ProjectMetadataView metadata;

    CLevelQueryPort()
    {
        view.id = cRevision();
        view.seq = 11;
        view.branch = cBranch();
        view.commandSummary = "C 级端到端案例锚定修订（模型＋需求＋C 域评估）";
        view.hasUnresolvedPayload = false;

        project::BranchRecord branch;
        branch.branchId = cBranch();
        branch.label = "main";
        branch.tipRevisionId = view.id;
        metadata.record.projectDisplayName = "六轴搬运工作站方案 A（C 级评审）";
        metadata.record.primaryBranchId = cBranch();
        metadata.record.branches = {branch};
        metadata.record.schemeLabels = {{"scheme", "plan-a"}};
    }

    project::RevisionView head() const override { return view; }

    std::optional<project::RevisionView> tryRevision(core::RevisionId id) const override
    {
        if (!(id == view.id)) {
            return std::nullopt;
        }
        return view;
    }

    std::vector<project::RunInfo> listRuns(core::RevisionId) const override { return runs; }

    std::optional<project::ProjectMetadataView> metadataAt(core::RevisionId id) const override
    {
        if (!(id == view.id)) {
            return std::nullopt;
        }
        return metadata;
    }

    std::vector<project::RunInfo> runs;   ///< finalize 运行清单（listRuns 口径）

    // —— 构建器绝不调用的方法（零项目写/零越权读反向断言面——调用即失败）。
    project::RevisionView revision(core::RevisionId) const override { unexpected("revision"); }
    project::ProjectMetadataView currentMetadata() const override
    {
        unexpected("currentMetadata");
    }
    std::vector<project::BranchTip> branchTips() const override { unexpected("branchTips"); }
    std::vector<project::RevisionView> branchHistory(core::BranchId, std::uint32_t) const override
    {
        unexpected("branchHistory");
    }
    std::optional<std::vector<std::uint8_t>> tryObject(core::ObjectId,
                                                       core::ContentVersion) const noexcept override
    {
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

    [[noreturn]] static void unexpected(const char* method)
    {
        throw std::logic_error(std::string{"CLevelQueryPort: 意外方法 "} + method);
    }
};

/// finalize 运行清单行（listRuns 口径——与 envelope 同源）。
project::RunInfo runInfoOf(const evidence::ResultEnvelope& envelope)
{
    project::RunInfo info;
    info.runId = envelope.task.run;
    info.task = envelope.task;
    info.runKind = "evaluation";
    info.evaluationKey = envelope.evaluationKey;
    info.finalizedAtUtc = "2026-10-12T00:00:00Z";
    return info;
}

// =====================================================================
// C 域提供方适配器（真实数据投影——消费 SectionRequest.results 的真实字段）
// =====================================================================

/**
 * @brief C 域章节提供方适配器（与 LevelBEndToEndTest 的 DomainSectionAdapter
 *        同构——条目从注入 envelope 的真实字段投影，绑定与证据引用取自包络
 *        本体；minimumLevel 可为 C——C 专属域章节的注册面验证）。
 *
 * 可选包络条目标注（envelopeNote）：追加一节文本字段承载条目级呈现注记
 * （RP-COV-1 的"包络条目标注'包络合并（呈现方式）'"呈现面——§6.5；标注
 * 由提供方投影承载，reporting 渲染层透传条目文本）。
 */
class CDomainSectionAdapter final : public IReportSectionProvider {
public:
    CDomainSectionAdapter(std::string sectionId, ReportLevel minLevel,
                          std::string evaluationKey, std::string entryKey,
                          std::string fieldPath, std::string envelopeNote = {})
        : m_sectionId(std::move(sectionId)), m_minLevel(minLevel),
          m_evaluationKey(std::move(evaluationKey)), m_entryKey(std::move(entryKey)),
          m_fieldPath(std::move(fieldPath)), m_envelopeNote(std::move(envelopeNote))
    {
    }

    std::string sectionId() const override { return m_sectionId; }
    ReportLevel minimumLevel() const override { return m_minLevel; }
    std::vector<std::string> requiredEvaluationKeys() const override { return {m_evaluationKey}; }

    SectionContent project(const SectionRequest& request) override
    {
        // 前置（§9.2"request.results 非空——构建器已过滤"）：空集＝编排
        // 违约（构建器在空集时不调用投影——测试自证该前置）。
        if (request.results.empty()) {
            throw std::logic_error("CDomainSectionAdapter: 结果子集为空（§9.2 前置违约）");
        }
        const evidence::ResultEnvelope& envelope = request.results.front();

        SectionContent content;
        content.providerContractVersion = 1;
        content.renderHint = RenderHint::Table;

        // 条目字段＝包络真实值的投影（评估键/判定 token/工况计数——同请求
        // 同输出；不引入包络外的任何数据——§9.2"传入结果即全部输入"）。
        SectionEntryView entry;
        entry.entryKey = m_entryKey;
        entry.fields.push_back(textField("evaluation-key", envelope.evaluationKey));
        entry.fields.push_back(textField("engineering-status",
                                         std::string(core::toToken(envelope.engineeringStatus))));
        entry.fields.push_back(countField("case-count", envelope.caseScope.caseIds.size()));
        if (!m_envelopeNote.empty()) {
            // 条目级呈现注记（如"包络合并（呈现方式）"——§6.5 的标注由提供
            // 方侧承载，渲染层透传；不参与任何判定语义）。
            entry.fields.push_back(textField("presentation-note", m_envelopeNote));
        }
        entry.result.runId = envelope.task.run;
        entry.result.fieldPath = m_fieldPath;
        if (!envelope.evidence.items.empty()) {
            EvidenceBinding binding;
            const evidence::EvidenceItem& item = envelope.evidence.items.front();
            binding.itemId = item.itemId;
            binding.status = item.status;
            if (item.artifactDigest.has_value()) {
                binding.digest = item.artifactDigest;   // Satisfied 绑定摘要一致面
            }
            binding.caseScope = envelope.caseScope.caseIds;
            entry.evidence = {binding};
        }
        entry.caseScope = envelope.caseScope.caseIds;
        entry.jump.runId = envelope.task.run;
        if (!entry.caseScope.empty()) {
            entry.jump.caseId = entry.caseScope.front();
        }
        content.entries = {entry};
        content.status = SectionStatus::Populated;
        return content;
    }

private:
    /// 文本字段工厂（纯文本侧——与数值侧互斥，§9.2 字段形状）。
    static FieldValue textField(std::string key, std::string value)
    {
        FieldValue field;
        field.key = std::move(key);
        field.text = std::move(value);
        return field;
    }

    /// 无量纲计数字段工厂（Provided×UserProvided——域事实的来源就是该域
    /// 评估记录本身）。
    static FieldValue countField(std::string key, std::size_t count)
    {
        FieldValue field;
        field.key = std::move(key);
        field.quantity = core::SourcedValue<double>::provided(
            static_cast<double>(count),
            core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
        return field;
    }

    std::string m_sectionId;
    ReportLevel m_minLevel;
    std::string m_evaluationKey;
    std::string m_entryKey;
    std::string m_fieldPath;
    std::string m_envelopeNote;
};

/// §5.1 行 9——轨迹与节拍（C 专属域；trajectory 域提供方注册面）。
std::unique_ptr<IReportSectionProvider> trajectorySectionAdapter(const std::string& note = {})
{
    return std::make_unique<CDomainSectionAdapter>(std::string(kSectionTrajectoryCycle),
                                                   ReportLevel::C, "trj-cycle-time",
                                                   "cycle-time", "payload-cycle-time", note);
}
/// §5.1 行 10——动力学曲线、峰值和 RMS（C 专属域；dynamics 域）。
std::unique_ptr<IReportSectionProvider> dynamicsSectionAdapter(const std::string& note = {})
{
    return std::make_unique<CDomainSectionAdapter>(std::string(kSectionDynamicsEnvelope),
                                                   ReportLevel::C, "dyn-envelope-force",
                                                   "dyn-peaks", "payload-dyn-peaks", note);
}
/// §5.1 行 11——传动工作点（C 专属域；drivetrain 投影消费 dyn 域映射产物
/// ——评估键仍为 dyn 域键〔§5.1 行 11 主要数据源＝DYN-04 映射产物〕）。
std::unique_ptr<IReportSectionProvider> drivetrainSectionAdapter(const std::string& note = {})
{
    return std::make_unique<CDomainSectionAdapter>(
        std::string(kSectionDrivetrainOperatingPoints), ReportLevel::C, "dyn-op-points",
        "op-points", "payload-op-points", note);
}
/// §5.1 行 12——选型/BOM 与淘汰依据（C 专属域；selection 域）。
std::unique_ptr<IReportSectionProvider> selectionSectionAdapter(const std::string& note = {})
{
    return std::make_unique<CDomainSectionAdapter>(std::string(kSectionSelectionBom),
                                                   ReportLevel::C, "sel-bom-compare",
                                                   "bom-table", "payload-bom", note);
}

// =====================================================================
// C 域真实包络工厂（evidence make 构造边界——合法组合-only）
// =====================================================================

/**
 * @brief 域评估包络（Completed×Feasible×Verified——正式结论形态；证据清单
 *        带 Satisfied 项；profileId 限五域词表 kin|trj|dyn|sel|opt〔N-5〕）。
 */
evidence::ResultEnvelope cDomainEnvelope(const core::RunId& run,
                                         const std::string& evaluationKey,
                                         const std::string& profileId,
                                         const std::string& payloadId,
                                         std::vector<core::ObjectId> caseIds,
                                         const std::string& evidenceItemId)
{
    if (caseIds.empty()) {
        caseIds = standardCases();
    }
    evidence::ResultEnvelopeDraft d;
    d.task = test_fakes::scriptedTask(run);
    d.evaluationKey = evaluationKey;
    d.evaluatorContractVersion = 7;
    d.mode = core::EvaluationMode::Verified;   // 正式模式（报告级别≠证据等级）
    d.snapshotId = test_fakes::scriptedCid(0x10);
    d.sliceId = test_fakes::scriptedCid(0x30);
    d.inputBaselineId = test_fakes::scriptedCid(0x50);
    d.caseScope.caseIds = std::move(caseIds);
    d.profile.profileId = profileId;
    d.profile.version = "1.0.0";
    d.profile.contentIdentity = test_fakes::scriptedCid(0x60);
    d.outcome = core::TaskOutcome::Completed;
    d.engineeringStatus = core::EngineeringStatus::Feasible;
    evidence::EvidenceManifest manifest;
    manifest.snapshotId = d.snapshotId;
    manifest.sliceId = d.sliceId;
    manifest.profileId = profileId;
    manifest.profileVersion = "1.0.0";
    manifest.profileContentIdentity = test_fakes::scriptedCid(0x60);
    evidence::EvidenceItem ok;
    ok.itemId = evidenceItemId;
    ok.status = evidence::EvidenceItemStatus::Satisfied;
    ok.artifactDigest = test_fakes::scriptedCid(0x10).bytes;
    manifest.items = {ok};
    d.evidence = std::move(manifest);
    d.payload = evidence::DomainPayloadDraft{payloadId, {0x01, 0x02}};
    d.producer.productVersion = "industrialrobot-designer 0.1.0";
    return evidence::ResultEnvelope::make(std::move(d));
}

// =====================================================================
// C 级端到端场景（查询端口/结果源/注册表/摘要提供方四注入）
// =====================================================================

struct CLevelScene {
    CLevelQueryPort queryPort;
    ScriptedResultSource resultSource;
    SectionRegistry registry;
    std::unique_ptr<rp_scenario::RpModelSummaryProvider> summaryProvider =
        std::make_unique<rp_scenario::RpModelSummaryProvider>(rp_scenario::standardModelSummary());

    /// 登记一个域评估运行（真实包络＋finalize 清单同源；缺省覆盖矩阵＝
    /// 按 caseScope 全 Executed——替身缺省行为）。
    void addRun(const core::RunId& run, const std::string& evaluationKey,
                const std::string& profileId, const std::string& payloadId,
                const std::string& evidenceItemId,
                std::vector<core::ObjectId> caseIds = {})
    {
        evidence::ResultEnvelope envelope =
            cDomainEnvelope(run, evaluationKey, profileId, payloadId, std::move(caseIds),
                            evidenceItemId);
        queryPort.runs.push_back(runInfoOf(envelope));
        resultSource.withEnvelope(run, std::move(envelope));
    }

    /// 标准六域全注册（B 级 kin/opt＋C 专属 trj/dyn/drivetrain/sel——
    /// 14 章节全 Populated 的注册面；modeling/requirements 未注册→其章节
    /// 以"提供方未注册"缺项呈现〔§5.1 词表规则——非评估域不伪造〕）。
    void registerAllDomainAdapters()
    {
        registry.registerProvider(
            std::make_unique<CDomainSectionAdapter>(std::string(kSectionKinematicsCollision),
                                                    ReportLevel::B, "kin-batch-ik",
                                                    "ik-converged", "payload-task-points-0"));
        registry.registerProvider(
            std::make_unique<CDomainSectionAdapter>(std::string(kSectionOptimizationCandidates),
                                                    ReportLevel::B, "opt-static-screen",
                                                    "pareto-front", "payload-candidates-0"));
        registry.registerProvider(trajectorySectionAdapter());
        registry.registerProvider(dynamicsSectionAdapter());
        registry.registerProvider(drivetrainSectionAdapter());
        registry.registerProvider(selectionSectionAdapter());
    }

    ReportBuildRequest request(ReportLevel level) const
    {
        ReportBuildRequest request;
        request.project = cProject();
        request.branch = cBranch();
        request.revision = cRevision();
        request.level = level;
        for (const project::RunInfo& info : queryPort.runs) {
            request.resultRuns.push_back(info.runId);
        }
        return request;
    }

    /// 构建器实例（摘要提供方借用生命周期随本对象）。
    std::unique_ptr<ReviewReportBuilder> builder()
    {
        return std::make_unique<ReviewReportBuilder>(queryPort, resultSource, registry, nullptr,
                                                     summaryProvider.get());
    }
};

/// 标准四 C 域运行场景（双必验工况全 Executed 缺省矩阵——14 章节全量正例
/// 的铺位；用例单点变异：漏验矩阵/种子等显式覆写）。
CLevelScene standardCScene()
{
    CLevelScene scene;
    scene.addRun(cRun(0xA3), "kin-batch-ik", "kin", "kin-batch-ik-v1",
                 "kin.reach-per-task-point");
    scene.addRun(cRun(0xA4), "opt-static-screen", "opt", "opt-static-screen-v1",
                 "opt.hard-constraint-record");
    scene.addRun(cRun(0xA5), "trj-cycle-time", "trj", "trj-cycle-time-v1", "trj.cycle-time");
    scene.addRun(cRun(0xA6), "dyn-envelope-force", "dyn", "dyn-envelope-v1", "dyn.envelope-force");
    scene.addRun(cRun(0xA7), "dyn-op-points", "dyn", "dyn-op-points-v1", "dyn.op-points");
    scene.addRun(cRun(0xA8), "sel-bom-compare", "sel", "sel-bom-v1", "sel.bom-record");
    scene.registerAllDomainAdapters();
    return scene;
}

/// 按章节 token 取报告章节（断言辅助——未命中即失败）。
const ReviewReportSection& sectionOf(const ReviewReport& report, std::string_view sectionId,
                                     const char* where)
{
    for (const ReviewReportSection& section : report.sections()) {
        if (section.sectionId == sectionId) {
            return section;
        }
    }
    ADD_FAILURE() << where << ": 报告缺少章节 " << sectionId;
    static const ReviewReportSection kEmpty{};
    return kEmpty;
}

/// 失败形态断言（report 空＋error 码面——§9.1 后置"失败＝无报告对象"；
/// note 为用例语义说明——随首断言输出便于失败定位）。
void expectFailure(const ReportBuildOutcome& outcome, ReportErrorCode code,
                   const char* note = "失败路径不得产生报告对象（§4.2①）")
{
    ASSERT_EQ(outcome.report, nullptr) << note;
    ASSERT_TRUE(outcome.error.has_value());
    EXPECT_EQ(outcome.error->code(), code);
}

/// 诊断码面是否在场（失败路径诊断核对辅助——NFR-COR-03 断言面）。
bool hasDiagnostic(const std::vector<core::DiagnosticRecord>& diagnostics,
                   std::string_view code)
{
    for (const core::DiagnosticRecord& record : diagnostics) {
        if (record.code == code) {
            return true;
        }
    }
    return false;
}

/// HTML 文本中子串出现次数（RP-STATE-1"文本扫描"口径的复用面）。
std::size_t countOccurrences(const std::string& text, std::string_view needle)
{
    std::size_t count = 0;
    for (std::size_t pos = text.find(needle); pos != std::string::npos;
         pos = text.find(needle, pos + needle.size())) {
        ++count;
    }
    return count;
}

// =====================================================================
// 用例 1——AT-32 主正例：C 级 14 章节全量、逐章追溯（结果/工况/快照）＋
// 签署块在列＋覆盖摘要全覆盖
// =====================================================================

/**
 * AT-32（acceptance 1）：完整工程案例（B＋C 六域运行）→ C 级报告 14 章节
 * 按词表序全量在列；C 域章节逐章追溯到结果（runId→resultRefs）/工况
 * （caseScope ⊆ 结果范围）/快照（snapshotId/sliceId 追溯链非零）；评审/
 * 签署元数据章节 Populated（§4.2"C 级必含签署块"——Unsigned 如实呈现）；
 * 变体章节缺正式差异内容＝NoFormalResult＋缺项（默认不选，不伪造）；
 * 覆盖摘要＝双必验工况全 Executed。
 */
TEST(LevelCEndToEnd, FourteenSectionsChapterTrace_At32_RptT15)
{
    CLevelScene scene = standardCScene();
    const std::unique_ptr<ReviewReportBuilder> builder = scene.builder();

    const ReportBuildOutcome outcome = builder->build(scene.request(ReportLevel::C));
    ASSERT_NE(outcome.report, nullptr) << "C 级端到端正例必须构建成功";
    ASSERT_FALSE(outcome.error.has_value());
    const ReviewReport& report = *outcome.report;

    // ---- 报告骨架：C 级＋14 章节按词表序（RP-SCOPE-2"14 章节全选场景"
    // 的真实数据版——组装序＝词表行号序）。
    EXPECT_EQ(report.level(), ReportLevel::C);
    const std::vector<std::string> cScope = sectionsInScope(ReportLevel::C);
    ASSERT_EQ(cScope.size(), std::size_t{14}) << "C 级范围＝词表全 14 项（RPT-T15 扩量）";
    ASSERT_EQ(report.sections().size(), cScope.size());
    for (std::size_t i = 0; i < report.sections().size(); ++i) {
        EXPECT_EQ(report.sections()[i].sectionId, cScope[i]) << "组装序＝词表行号序";
        EXPECT_EQ(report.sections()[i].order, *trySectionOrder(cScope[i])) << "order＝词表行号";
    }

    // ---- 结果引用快照：六个评估域运行（Verified×Completed×Feasible）；
    // 追溯链三锚非零（NFR-COR-04——AT-32"追溯到快照"的报告侧锚点）。
    ASSERT_EQ(report.resultRefs().size(), std::size_t{6});
    std::map<std::string, const ResultRefSnapshot*> refIndex;
    for (const ResultRefSnapshot& ref : report.resultRefs()) {
        refIndex.emplace(ref.runId.toCanonical(), &ref);
        EXPECT_EQ(ref.mode, core::EvaluationMode::Verified);
        EXPECT_EQ(ref.outcome, core::TaskOutcome::Completed);
        EXPECT_EQ(ref.engineeringStatus, core::EngineeringStatus::Feasible);
        EXPECT_TRUE(ref.snapshotId.isValid());
        EXPECT_TRUE(ref.sliceId.isValid());
        EXPECT_TRUE(ref.inputBaselineId.isValid());
    }

    // ---- 四个 C 域章节逐章追溯（AT-32"逐章追溯到结果、工况与快照"）：
    // 条目结果绑定可解析＋工况范围 ⊆ 结果范围＋证据绑定入报告级并集＋
    // formalPass 资格聚合成立（Verified 正式结果）。
    for (const std::string_view domainId :
         {kSectionTrajectoryCycle, kSectionDynamicsEnvelope,
          kSectionDrivetrainOperatingPoints, kSectionSelectionBom}) {
        const ReviewReportSection& section = sectionOf(report, domainId, "C 域章节");
        ASSERT_EQ(section.status, SectionStatus::Populated);
        EXPECT_TRUE(section.selected) << "Populated 默认选中（§5 默认规则）";
        ASSERT_FALSE(section.entries.empty());
        for (const SectionEntryView& entry : section.entries) {
            // 追溯链第一跳：结果绑定 runId ∈ resultRefs（RP-TRACE-1 同口径）。
            const auto refIt = refIndex.find(entry.result.runId.toCanonical());
            ASSERT_TRUE(refIt != refIndex.end())
                << section.sectionId << ": 结果绑定不可解析——"
                << entry.result.runId.toCanonical();
            // 追溯链第二跳：绑定结果的身份锚可解析（快照/切片/基准非零
            // ——AT-32"追溯到快照"的逐章核对面）。
            EXPECT_TRUE(refIt->second->snapshotId.isValid());
            EXPECT_TRUE(refIt->second->sliceId.isValid());
            // 工况范围 ⊆ 所属结果工况范围（§4.7 关系约束——"追溯到工况"）。
            for (const core::ObjectId& caseId : entry.caseScope) {
                bool contained = false;
                for (const core::ObjectId& scope : refIt->second->caseScope) {
                    if (scope == caseId) {
                        contained = true;
                    }
                }
                EXPECT_TRUE(contained) << section.sectionId << ": 条目工况越界（§4.7）";
            }
            // 证据绑定进入报告级并集（§4.2 evidenceRefs——"追溯到结果"的
            // 证据面）。
            ASSERT_FALSE(entry.evidence.empty());
            for (const EvidenceBinding& binding : entry.evidence) {
                bool found = false;
                for (const EvidenceRefEntry& ref : report.evidenceRefs()) {
                    if (ref.itemId == binding.itemId
                        && ref.sourceSection == section.sectionId) {
                        found = true;
                    }
                }
                EXPECT_TRUE(found) << section.sectionId << ": 证据引用未入报告级并集";
            }
        }
        // 章节级资格聚合（Verified 正式结果——formalPass 成立）。
        ASSERT_TRUE(section.eligibilityNote.has_value());
        EXPECT_TRUE(section.eligibilityNote->formalPassAllowed);
    }

    // ---- 评审/签署元数据章节（§5.1 行 13——C 级必含签署块；全新报告＝
    // Unsigned 如实呈现、元数据块恒 Populated）。
    const ReviewReportSection& signoff = sectionOf(report, kSectionReviewSignoff, "签署章节");
    EXPECT_EQ(signoff.status, SectionStatus::Populated);
    EXPECT_TRUE(signoff.selected);
    EXPECT_EQ(signoff.renderHint, RenderHint::MetadataBlock);
    // 报告级元数据复核：basisRevision＝请求修订、未签署（§4.5 默认形态）。
    EXPECT_TRUE(report.review().basisRevision == cRevision());
    EXPECT_EQ(report.review().signOff.state, SignOffState::State::Unsigned);

    // ---- 变体章节（§5.1 行 14）：全新报告无 variantDiff——缺正式差异内容
    // ＝NoFormalResult＋缺项＋默认不选（§16 验收要点；不伪造差异内容）。
    const ReviewReportSection& variant = sectionOf(report, kSectionVariantDiff, "变体章节");
    EXPECT_EQ(variant.status, SectionStatus::NoFormalResult);
    EXPECT_FALSE(variant.selected) << "缺正式结果章节默认不选（§16 验收要点）";
    ASSERT_FALSE(variant.missingItems.empty());
    EXPECT_EQ(variant.missingItems.front().itemId, kSectionVariantDiff);

    // ---- 覆盖摘要（RP-COV-1 全覆盖路）：六结果缺省矩阵（双工况全 Executed
    // 同基准）→ 必验总数 2、已执行 2、零漏验。
    EXPECT_EQ(report.coverageSummary().totalRequired, std::uint64_t{2});
    EXPECT_EQ(report.coverageSummary().executed, std::uint64_t{2});
    EXPECT_EQ(report.coverageSummary().notExecuted, std::uint64_t{0});
    EXPECT_EQ(report.coverageSummary().invalid, std::uint64_t{0});
    EXPECT_EQ(report.coverageSummary().failed, std::uint64_t{0});

    // ---- HTML 渲染消费同源报告（AT-32 逐章追溯的呈现面冒烟：签署块与
    // 覆盖完备性结论如实入文）。
    const std::string html = rp_scenario::renderText(report, ReportRenderFormat::Html);
    EXPECT_NE(countOccurrences(html, "sign-off"), std::size_t{0}) << "签署块入渲染文本";
    EXPECT_NE(countOccurrences(html, "全部启用必验工况已覆盖"), std::size_t{0})
        << "覆盖完备性结论（§6.5）";
}

// =====================================================================
// 用例 2——RP-REVW-1：评审元数据新版本（签署＋追加意见→演化链重建）
// =====================================================================

/**
 * RP-REVW-1（acceptance 1）：C 报告 v1 生成→签署（Signed）＋追加评审意见
 * ＋演化链摘要经 reviewSeed 重建 v2→新 ReportId、reportVersion+1、
 * supersedes＝v1、dataIdentity 不变（同数据基准的评审演化——§4.5）、
 * v1 报告对象逐字段不变（不可变历史——PA-2/D-11）；v2 的 review-signoff
 * 章节呈现签署与意见（元数据入 contentIdentity——§4.4）。
 *
 * 场景边界说明（dataIdentity 对账语义）：本用例的演化种子**不含**
 * variantDiff——改型差异块的加入会使 variant-diff 章节从缺项（默认不选）
 * 转为 Populated（默认选中），选中章节集（§4.4 dataIdentity 进入编码的
 * "(sectionId, selected) 集"）随之变化＝数据源规格变化，种子对账按 §9.1
 * 整体 dataIdentity 比对将正确拒绝（那不是"同数据基准"的演化）。携带
 * variantDiff 的演化工作流（先以 sectionOverrides 预选变体章节保持选中集
 * 稳定）由独立用例 VariantReportConsistentBaseline_Rpt04 承载。
 */
TEST(LevelCEndToEnd, ReviewMetadataNewVersion_RpRevw1_RptT15)
{
    CLevelScene scene = standardCScene();
    const std::unique_ptr<ReviewReportBuilder> builder = scene.builder();

    // ---- v1：全新 C 级报告（未签署基线）。
    const ReportBuildOutcome v1Outcome = builder->build(scene.request(ReportLevel::C));
    ASSERT_NE(v1Outcome.report, nullptr);
    const ReviewReport v1 = *v1Outcome.report;   // 值拷贝——v1 冻结基线
    EXPECT_EQ(v1.reportVersion(), std::uint32_t{1});
    EXPECT_FALSE(v1.supersedes().has_value());

    // ---- v2 种子：签署＋追加意见＋演化链摘要（不改章节选择集——同数据
    // 基准演化的纯净形态）。
    ReviewMetadataSeed seed;
    seed.priorReportId = v1.reportId();
    seed.priorDataIdentity = v1.dataIdentity();
    seed.priorReportVersion = v1.reportVersion();
    seed.metadata.basisRevision = cRevision();
    seed.metadata.basisSnapshot = v1.snapshotId();
    seed.metadata.reviewer = "评审人-甲";
    seed.metadata.reviewedAtUtc = std::chrono::system_clock::now();
    ReviewComment comment;
    comment.author = "评审人-甲";
    comment.atUtc = std::chrono::system_clock::now();
    comment.text = "轨迹节拍满足节拍预算，动力学峰值在减速器许用窗内（评审意见留痕）";
    comment.sectionId = std::string(kSectionTrajectoryCycle);
    seed.metadata.comments = {comment};
    seed.metadata.signOff = SignOffState::signedState(
        "评审人-甲", std::chrono::system_clock::now(), test_fakes::scriptedCid(0x90).bytes);
    ReportVersionEntry change;
    change.reportId = v1.reportId();
    change.reportVersion = 1;
    change.atUtc = std::chrono::system_clock::now();
    change.actor = "评审人-甲";
    change.reason = "签署评审结论并追加评审意见";
    seed.metadata.changeLog = {change};

    ReportBuildRequest v2Request = scene.request(ReportLevel::C);
    v2Request.reviewSeed = seed;
    const ReportBuildOutcome v2Outcome = builder->build(v2Request);
    ASSERT_NE(v2Outcome.report, nullptr) << "演化重建必须成功（同数据基准）";
    ASSERT_FALSE(v2Outcome.error.has_value());
    const ReviewReport& v2 = *v2Outcome.report;

    // ---- 演化链断言（RP-REVW-1 观测点：身份链）。
    EXPECT_FALSE(v2.reportId() == v1.reportId()) << "新 ReportId（§4.5——新报告对象）";
    EXPECT_EQ(v2.reportVersion(), v1.reportVersion() + 1) << "reportVersion+1";
    ASSERT_TRUE(v2.supersedes().has_value());
    EXPECT_TRUE(*v2.supersedes() == v1.reportId()) << "supersedes＝旧 ReportId";
    EXPECT_TRUE(v2.dataIdentity() == v1.dataIdentity())
        << "dataIdentity 不变（同数据基准的评审演化——§4.5）";
    EXPECT_FALSE(v2.contentIdentity() == v1.contentIdentity())
        << "contentIdentity 变化（元数据入内容身份——§4.4）";

    // ---- v1 报告对象逐字段不变（不可变历史——PA-2/D-11：旧报告零改写；
    // 值拷贝基线与重建后的对象全等比较）。
    const ReviewReport v1Again = v1;   // 拷贝再比较——对象不可变的直接核对
    EXPECT_TRUE(v1Again == v1);

    // ---- v2 元数据呈现：签署块 Signed＋意见条目（§4.5 元数据入签署章节）。
    EXPECT_EQ(v2.review().signOff.state, SignOffState::State::Signed);
    ASSERT_EQ(v2.review().comments.size(), std::size_t{1});

    const ReviewReportSection& signoff = sectionOf(v2, kSectionReviewSignoff, "v2 签署章节");
    EXPECT_EQ(signoff.status, SectionStatus::Populated);
    bool hasCommentEntry = false;
    bool hasSignedEntry = false;
    for (const SectionEntryView& entry : signoff.entries) {
        if (entry.entryKey == "comment-1") {
            hasCommentEntry = true;
        }
        if (entry.entryKey == "sign-off") {
            for (const FieldValue& field : entry.fields) {
                if (field.key == "sign-off-state" && field.text.has_value()
                    && *field.text == "signed") {
                    hasSignedEntry = true;
                }
            }
        }
    }
    EXPECT_TRUE(hasCommentEntry) << "评审意见条目入签署章节（§4.5 comments）";
    EXPECT_TRUE(hasSignedEntry) << "签署状态条目（§4.5 signOff）";
}

/**
 * RPT-04 变体报告正例（EVI-02 一致半边——acceptance 2）：变体章节先以
 * sectionOverrides 显式预选（v1 的选中章节集已含 variant-diff），评审演化
 * 种子携带**一致基准**的 variantDiff 重建 v2→构建成功（选中集不变＝同数据
 * 基准，dataIdentity 对账通过）；variant-diff 章节转 Populated（DiffTable
 * 版式：修订对/模型差异引用/基准检查/取舍理由条目齐备）。
 */
TEST(LevelCEndToEnd, VariantReportConsistentBaseline_Rpt04_RptT15)
{
    CLevelScene scene = standardCScene();
    const std::unique_ptr<ReviewReportBuilder> builder = scene.builder();

    // v1：显式预选 variant-diff 章节（NoFormalResult＋选中——用户预告变体
    // 章节占位，§9.1 sectionOverrides 逐章节覆写语义）。
    ReportBuildRequest v1Request = scene.request(ReportLevel::C);
    v1Request.sectionOverrides = {SectionSelection{std::string(kSectionVariantDiff), true}};
    const ReportBuildOutcome v1Outcome = builder->build(v1Request);
    ASSERT_NE(v1Outcome.report, nullptr);
    const ReviewReport v1 = *v1Outcome.report;

    // v2：同预选（选中集不变）＋一致基准 variantDiff。
    ReviewMetadataSeed seed;
    seed.priorReportId = v1.reportId();
    seed.priorDataIdentity = v1.dataIdentity();
    seed.priorReportVersion = v1.reportVersion();
    seed.metadata.basisRevision = cRevision();
    seed.metadata.signOff = SignOffState::unsignedState();
    VariantDiffBlock diff;
    diff.baselineRevision = test_fakes::scriptedId<core::RevisionId>(0x41);
    diff.candidateRevision = cRevision();
    diff.modelDiffRef = test_fakes::scriptedId<core::ObjectId>(0x42);
    // 一致基准（evidence checkComparisonBaselinesConsistent 纯检查结果值——
    // consistent=true、零差异维度；reporting 消费不重算，§4.5）。
    diff.baselineCheckResult = evidence::BaselineConsistencyResult{};
    TradeOff trade;
    trade.topic = "减速器传动比";
    trade.rationale = "选取 i=121 传动比以在节拍与峰值扭矩间取平衡（取舍理由留痕）";
    trade.sectionId = std::string(kSectionDrivetrainOperatingPoints);
    diff.tradeOffs = {trade};
    seed.metadata.variantDiff = diff;

    ReportBuildRequest v2Request = scene.request(ReportLevel::C);
    v2Request.sectionOverrides = v1Request.sectionOverrides;
    v2Request.reviewSeed = seed;
    const ReportBuildOutcome v2Outcome = builder->build(v2Request);
    ASSERT_NE(v2Outcome.report, nullptr) << "一致基准的变体演化必须成功";
    ASSERT_FALSE(v2Outcome.error.has_value());
    const ReviewReport& v2 = *v2Outcome.report;
    EXPECT_TRUE(v2.dataIdentity() == v1.dataIdentity()) << "同数据基准（选中集不变）";
    ASSERT_TRUE(v2.review().variantDiff.has_value());

    // variant-diff 章节：Populated＋DiffTable＋四类条目齐备（§5.1 行 14）。
    const ReviewReportSection& variant = sectionOf(v2, kSectionVariantDiff, "v2 变体章节");
    EXPECT_EQ(variant.status, SectionStatus::Populated);
    EXPECT_EQ(variant.renderHint, RenderHint::DiffTable);
    EXPECT_TRUE(variant.selected);
    bool hasRevisions = false;
    bool hasModelDiff = false;
    bool hasBaselineCheck = false;
    bool hasTradeOff = false;
    for (const SectionEntryView& entry : variant.entries) {
        hasRevisions = hasRevisions || entry.entryKey == "revisions";
        hasModelDiff = hasModelDiff || entry.entryKey == "model-diff";
        hasBaselineCheck = hasBaselineCheck || entry.entryKey == "baseline-check";
        hasTradeOff = hasTradeOff || entry.entryKey == "trade-off-1";
    }
    EXPECT_TRUE(hasRevisions) << "基线/候选修订对条目";
    EXPECT_TRUE(hasModelDiff) << "模型差异实体引用条目（MDL-08 引用非副本）";
    EXPECT_TRUE(hasBaselineCheck) << "基准一致性检查结果条目（EVI-02 呈现面）";
    EXPECT_TRUE(hasTradeOff) << "取舍理由条目（RPT-04 呈现义务）";
}

// =====================================================================
// 用例 3——EVI-02 负例：变体基准不一致即拒绝
// =====================================================================

/**
 * EVI-02/RPT-04（acceptance 2 负例半边）：reviewSeed 携带的 variantDiff 其
 * baselineCheckResult.consistent==false（差异维度含模型/需求/工况集基准与
 * 必验工况集）→ 构建拒绝（无报告对象）＋RPT-CONSISTENCY-MISMATCH 诊断
 * （NFR-COR-03 不静默——"基准不一致即拒绝"）。
 */
TEST(LevelCEndToEnd, VariantInconsistentBaselineRejected_Evi02_RptT15)
{
    CLevelScene scene = standardCScene();
    const std::unique_ptr<ReviewReportBuilder> builder = scene.builder();

    // 先出 v1（数据基准）——种子的一致性字段需要合法基线。
    const ReportBuildOutcome v1Outcome = builder->build(scene.request(ReportLevel::C));
    ASSERT_NE(v1Outcome.report, nullptr);

    ReviewMetadataSeed seed;
    seed.priorReportId = v1Outcome.report->reportId();
    seed.priorDataIdentity = v1Outcome.report->dataIdentity();
    seed.priorReportVersion = 1;
    seed.metadata.basisRevision = cRevision();
    VariantDiffBlock diff;
    diff.baselineRevision = test_fakes::scriptedId<core::RevisionId>(0x41);
    diff.candidateRevision = cRevision();
    diff.modelDiffRef = test_fakes::scriptedId<core::ObjectId>(0x42);
    // 基准不一致（evidence 纯检查结果的不一致形态——差异维度：输入基准＋
    // 必验工况集；reporting 消费该结果做拒绝判定，不重算检查）。
    diff.baselineCheckResult.consistent = false;
    diff.baselineCheckResult.differingDimensions = {
        evidence::BaselineDifferenceDimension::InputBaseline,
        evidence::BaselineDifferenceDimension::RequiredCaseSet,
    };
    seed.metadata.variantDiff = diff;

    ReportBuildRequest badRequest = scene.request(ReportLevel::C);
    badRequest.reviewSeed = seed;
    const ReportBuildOutcome outcome = builder->build(badRequest);
    expectFailure(outcome, ReportErrorCode::Usage, "基准不一致＝种子组装违约拒绝（EVI-02）");
    EXPECT_TRUE(hasDiagnostic(outcome.diagnostics, std::string{diagcodes::kConsistencyMismatch}))
        << "RPT-CONSISTENCY-MISMATCH 诊断随交付（NFR-COR-03）";
}

// =====================================================================
// 用例 4——RP-COV-1：多工况覆盖显示（漏验/全覆盖/归并不掩盖三路）
// =====================================================================

/**
 * RP-COV-1（acceptance 1）漏验路：覆盖矩阵含 NotExecuted 工况→报告级
 * coverageSummary 忠实计数（executed=1/notExecuted=1）；HTML 呈现"漏验"
 * 与"整体数据不足"（§6.5 呈现口径）；漏验结果的 formalPass 资格不成立
 * （evidence 门禁的呈现侧自洽——渲染无"正式通过结论"字样，断言⑤防线）；
 * 包络条目的"包络合并"呈现注记透传入文（§6.5——包络合并不替代逐工况）。
 */
TEST(LevelCEndToEnd, CoverageMissingCaseOverallInsufficient_RpCov1_RptT15)
{
    CLevelScene scene;
    // 单 C 域运行（trj——漏验矩阵载体；domainIds 其余未注册——章节缺项，
    // 不影响覆盖呈现）；其资格脚本＝formalPass 不成立（漏验的 evidence
    // 门禁②级失败——呈现侧一致性：资格与覆盖计数不矛盾）。
    scene.addRun(cRun(0xA5), "trj-cycle-time", "trj", "trj-cycle-time-v1", "trj.cycle-time");
    ReportEligibilityChecks checks;
    checks.formalPass = test_fakes::scriptedEligibility(false);
    checks.reviewRecord = test_fakes::scriptedEligibility(false);
    scene.resultSource.withEligibility(cRun(0xA5), checks);
    // 漏验矩阵：case21 已执行、case22 未执行（§6.6 五态脚本——结构合法）。
    scene.resultSource.withCoverageMatrix(
        cRun(0xA5),
        test_fakes::scriptedCoverageMatrix(standardCases(),
                                           {evidence::CaseExecutionStatus::Executed,
                                            evidence::CaseExecutionStatus::NotExecuted}));
    scene.registry.registerProvider(trajectorySectionAdapter("包络合并（呈现方式）"));
    const std::unique_ptr<ReviewReportBuilder> builder = scene.builder();

    const ReportBuildOutcome outcome = builder->build(scene.request(ReportLevel::C));
    ASSERT_NE(outcome.report, nullptr) << "漏验呈现路径可生成（缺项呈现非构建拒绝）";
    const ReviewReport& report = *outcome.report;

    // ---- 报告级覆盖计数（§4.2 coverageSummary——漏验忠实计数）。
    EXPECT_EQ(report.coverageSummary().totalRequired, std::uint64_t{2});
    EXPECT_EQ(report.coverageSummary().executed, std::uint64_t{1});
    EXPECT_EQ(report.coverageSummary().notExecuted, std::uint64_t{1});

    // ---- 资格与覆盖自洽：漏验结果 formalPass=false（快照）。
    ASSERT_EQ(report.resultRefs().size(), std::size_t{1});
    EXPECT_FALSE(report.resultRefs().front().eligibility.formalPass)
        << "漏验结果的正式通过资格不成立（EVI-02）";

    // ---- HTML 呈现（§6.5 呈现口径——漏验清单规模＋整体缺项表述）。
    const std::string html = rp_scenario::renderText(report, ReportRenderFormat::Html);
    EXPECT_NE(countOccurrences(html, "漏验 1 项"), std::size_t{0}) << "漏验计数入文";
    EXPECT_NE(countOccurrences(html, "整体数据不足"), std::size_t{0})
        << "整体 DataInsufficient 缺项呈现（§6.5）";
    EXPECT_EQ(countOccurrences(html, "正式通过结论"), std::size_t{0})
        << "漏验时不得输出正式通过（EVI-02/渲染断言⑤）";
    EXPECT_NE(countOccurrences(html, "包络合并（呈现方式）"), std::size_t{0})
        << "包络条目标注透传（§6.5——包络合并仅为呈现方式）";
}

/**
 * RP-COV-1 全覆盖路＋NotApplicable 不计缺失（§5.3/§6.6——显式不适用不是
 * 漏验）：矩阵 [Executed, NotApplicable] → notApplicable=1、零漏验；HTML
 * 呈现"全部启用必验工况已覆盖"。
 */
TEST(LevelCEndToEnd, CoverageNotApplicableNotMissed_RpCov1_RptT15)
{
    CLevelScene scene;
    scene.addRun(cRun(0xA5), "trj-cycle-time", "trj", "trj-cycle-time-v1", "trj.cycle-time");
    scene.resultSource.withCoverageMatrix(
        cRun(0xA5),
        test_fakes::scriptedCoverageMatrix(standardCases(),
                                           {evidence::CaseExecutionStatus::Executed,
                                            evidence::CaseExecutionStatus::NotApplicable}));
    scene.registry.registerProvider(trajectorySectionAdapter());
    const std::unique_ptr<ReviewReportBuilder> builder = scene.builder();

    const ReportBuildOutcome outcome = builder->build(scene.request(ReportLevel::C));
    ASSERT_NE(outcome.report, nullptr);
    const ReviewReport& report = *outcome.report;

    // 计数：总数 2＝已执行 1＋不适用 1（NotApplicable 不计缺失——§6.6
    // "覆盖完备 ⇔ 全部 enabled∧mandatory 工况 Executed"的呈现侧投影）。
    EXPECT_EQ(report.coverageSummary().totalRequired, std::uint64_t{2});
    EXPECT_EQ(report.coverageSummary().executed, std::uint64_t{1});
    EXPECT_EQ(report.coverageSummary().notApplicable, std::uint64_t{1});
    EXPECT_EQ(report.coverageSummary().notExecuted, std::uint64_t{0});

    const std::string html = rp_scenario::renderText(report, ReportRenderFormat::Html);
    EXPECT_NE(countOccurrences(html, "全部启用必验工况已覆盖"), std::size_t{0})
        << "全覆盖结论（含不适用不计缺失——§6.5）";
}

/**
 * RP-COV-1 多结果归并路（§14.4 v0.18 归并规则的保守性钉扎）：同基准两
 * 结果，其一矩阵全覆盖、另一矩阵漏验同一工况→报告级取最不利（漏验不被
 * 另一结果的覆盖掩盖——EVI-02"漏验任一启用必验工况"的聚合侧表达）。
 */
TEST(LevelCEndToEnd, CoverageMergeNotMaskedMultiResult_RpCov1_RptT15)
{
    CLevelScene scene;
    scene.addRun(cRun(0xA3), "kin-batch-ik", "kin", "kin-batch-ik-v1",
                 "kin.reach-per-task-point");   // 全覆盖（缺省矩阵）
    scene.addRun(cRun(0xA5), "trj-cycle-time", "trj", "trj-cycle-time-v1",
                 "trj.cycle-time");             // 漏验 case22
    scene.resultSource.withCoverageMatrix(
        cRun(0xA5),
        test_fakes::scriptedCoverageMatrix(standardCases(),
                                           {evidence::CaseExecutionStatus::Executed,
                                            evidence::CaseExecutionStatus::NotExecuted}));
    // kin 矩阵显式全覆盖（同基准 requiredCaseSetId——缺省一致）。
    scene.resultSource.withCoverageMatrix(
        cRun(0xA3),
        test_fakes::scriptedCoverageMatrix(
            standardCases(),
            {evidence::CaseExecutionStatus::Executed, evidence::CaseExecutionStatus::Executed}));
    scene.registry.registerProvider(
        std::make_unique<CDomainSectionAdapter>(std::string(kSectionKinematicsCollision),
                                                ReportLevel::B, "kin-batch-ik",
                                                "ik-converged", "payload-task-points-0"));
    scene.registry.registerProvider(trajectorySectionAdapter());
    const std::unique_ptr<ReviewReportBuilder> builder = scene.builder();

    const ReportBuildOutcome outcome = builder->build(scene.request(ReportLevel::C));
    ASSERT_NE(outcome.report, nullptr);
    const ReviewReport& report = *outcome.report;

    // 归并计数：case22 在 trj 矩阵漏验→报告级 notExecuted=1（保守取态——
    // kin 的 Executed 不掩盖 trj 的 NotExecuted）。
    EXPECT_EQ(report.coverageSummary().totalRequired, std::uint64_t{2});
    EXPECT_EQ(report.coverageSummary().executed, std::uint64_t{1});
    EXPECT_EQ(report.coverageSummary().notExecuted, std::uint64_t{1});
}

// =====================================================================
// 用例 5——覆盖数据源缺位与跨基准对账拒绝（RPT-T15 注入增量）
// =====================================================================

/**
 * 覆盖矩阵缺数据源（tryCoverageMatrix 注入投影 nullopt——归档不含可解析
 * 矩阵）→ SourceMissing 拒绝＋诊断（报告级覆盖呈现缺数据源即失真——
 * EVI-02 呈现面宁可拒绝不可伪造）。
 */
TEST(LevelCEndToEnd, CoverageMatrixMissingSourceRejected_RptT15)
{
    CLevelScene scene;
    scene.addRun(cRun(0xA5), "trj-cycle-time", "trj", "trj-cycle-time-v1", "trj.cycle-time");
    scene.resultSource.withCoverageMatrix(cRun(0xA5), std::nullopt);   // 矩阵缺失注入
    scene.registry.registerProvider(trajectorySectionAdapter());
    const std::unique_ptr<ReviewReportBuilder> builder = scene.builder();

    const ReportBuildOutcome outcome = builder->build(scene.request(ReportLevel::C));
    expectFailure(outcome, ReportErrorCode::SourceMissing);
    EXPECT_TRUE(hasDiagnostic(outcome.diagnostics, std::string{diagcodes::kSourceMissing}));
}

/**
 * 跨基准对账拒绝（两结果覆盖矩阵 requiredCaseSetId 互异——报告绑定了不同
 * 必验工况集的结果，覆盖计数无法对账）→ SourceAmbiguous＋诊断（§6.6
 * "本矩阵对账的必验工况集身份"的报告级对账面——EVI-02 一致基准语义）。
 */
TEST(LevelCEndToEnd, CoverageBaselineMismatchRejected_RptT15)
{
    CLevelScene scene;
    scene.addRun(cRun(0xA3), "kin-batch-ik", "kin", "kin-batch-ik-v1",
                 "kin.reach-per-task-point");
    scene.addRun(cRun(0xA5), "trj-cycle-time", "trj", "trj-cycle-time-v1", "trj.cycle-time");
    // 同结构两矩阵、必验工况集身份互异——基准对账失败面。
    scene.resultSource.withCoverageMatrix(
        cRun(0xA3), test_fakes::scriptedCoverageMatrix(
                        standardCases(),
                        {evidence::CaseExecutionStatus::Executed,
                         evidence::CaseExecutionStatus::Executed},
                        test_fakes::scriptedCid(0x81)));
    scene.resultSource.withCoverageMatrix(
        cRun(0xA5), test_fakes::scriptedCoverageMatrix(
                        standardCases(),
                        {evidence::CaseExecutionStatus::Executed,
                         evidence::CaseExecutionStatus::Executed},
                        test_fakes::scriptedCid(0x82)));
    scene.registry.registerProvider(
        std::make_unique<CDomainSectionAdapter>(std::string(kSectionKinematicsCollision),
                                                ReportLevel::B, "kin-batch-ik",
                                                "ik-converged", "payload-task-points-0"));
    scene.registry.registerProvider(trajectorySectionAdapter());
    const std::unique_ptr<ReviewReportBuilder> builder = scene.builder();

    const ReportBuildOutcome outcome = builder->build(scene.request(ReportLevel::C));
    expectFailure(outcome, ReportErrorCode::SourceAmbiguous);
    EXPECT_TRUE(
        hasDiagnostic(outcome.diagnostics, std::string{diagcodes::kConsistencyMismatch}));
}

// =====================================================================
// 用例 6——RP-REVW-2：未签署报告可导出（签署状态如实呈现）
// =====================================================================

/**
 * RP-REVW-2（acceptance 1）：signOff=Unsigned 的 C 级报告经导出服务外部
 * 导出成功（签署状态是呈现事实，非导出门禁——D-12/§4.5"上游无'未签署禁
 * 导出'条款"）；导出 HTML 工件如实呈现未签署状态（unsigned 词面在文）。
 */
TEST(LevelCEndToEnd, UnsignedReportExportable_RpRevw2_RptT15)
{
    CLevelScene scene = standardCScene();
    const std::unique_ptr<ReviewReportBuilder> builder = scene.builder();
    const ReportBuildOutcome outcome = builder->build(scene.request(ReportLevel::C));
    ASSERT_NE(outcome.report, nullptr);
    const ReviewReport& report = *outcome.report;
    ASSERT_EQ(report.review().signOff.state, SignOffState::State::Unsigned)
        << "前置：被测报告为未签署形态";

    // ---- 外部导出（rp_scenario 导出场景——五注入借用随场景对象；临时
    // 目录为 testkit RAII 设施——资源隔离＋失败保留）。
    sdurws::ird::testkit::TempDir exportDir("rp-revw2-export");
    rp_scenario::ExportScene exportScene(report);
    ReportExportRequest request;
    request.project = report.project();
    request.reportId = report.reportId();
    request.formats = {ReportRenderFormat::Html};
    request.destination.kind = ExportDestination::ExternalPath;
    request.destination.externalPath = exportDir.path();
    const ReportExportResult result = exportScene.service()->exportReport(request);
    ASSERT_FALSE(result.error.has_value()) << "未签署报告导出成功（D-12——签署非门禁）";
    ASSERT_FALSE(result.files.empty());

    // ---- 签署状态如实呈现（导出 HTML 工件含 unsigned 词面）。
    bool foundUnsigned = false;
    for (const ExportedFile& file : result.files) {
        std::string bytes(std::filesystem::file_size(file.path), '\0');
        {
            std::FILE* fp = nullptr;
#ifdef _WIN32
            _wfopen_s(&fp, file.path.c_str(), L"rb");
#else
            fp = std::fopen(file.path.string().c_str(), "rb");
#endif
            ASSERT_NE(fp, nullptr) << "导出工件可读";
            const std::size_t got = std::fread(bytes.data(), 1, bytes.size(), fp);
            std::fclose(fp);
            ASSERT_EQ(got, bytes.size());
        }
        if (bytes.find("unsigned") != std::string::npos) {
            foundUnsigned = true;
        }
    }
    EXPECT_TRUE(foundUnsigned) << "导出工件如实呈现未签署状态（§4.5——呈现事实）";
}

// =====================================================================
// 用例 7——C 级保守拒绝回归钉（P-RPT-5 在 C 框架投影落位后不失效）
// =====================================================================

/**
 * T15 回归钉：C 专属框架章节（review-signoff 恒 Populated）落位后，C 域
 * 章节全缺正式结果时仍以 ScopeInsufficient 拒绝（P-RPT-5 保守处置不因
 * 元数据块恒有内容而失效——§5.4 行 2 的判定对象为承载评估结果的 C 域章节；
 * Builder 范围判定投影排除 C 框架章节的口径保持既有已验收行为）。
 */
TEST(LevelCEndToEnd, CScopeInsufficientStillRejected_Rpt15Regression)
{
    CLevelScene scene;
    // 仅 B 级两域运行与注册（四个 C 域章节未注册——全缺正式结果）。
    scene.addRun(cRun(0xA3), "kin-batch-ik", "kin", "kin-batch-ik-v1",
                 "kin.reach-per-task-point");
    scene.addRun(cRun(0xA4), "opt-static-screen", "opt", "opt-static-screen-v1",
                 "opt.hard-constraint-record");
    scene.registry.registerProvider(
        std::make_unique<CDomainSectionAdapter>(std::string(kSectionKinematicsCollision),
                                                ReportLevel::B, "kin-batch-ik",
                                                "ik-converged", "payload-task-points-0"));
    scene.registry.registerProvider(
        std::make_unique<CDomainSectionAdapter>(std::string(kSectionOptimizationCandidates),
                                                ReportLevel::B, "opt-static-screen",
                                                "pareto-front", "payload-candidates-0"));
    const std::unique_ptr<ReviewReportBuilder> builder = scene.builder();

    const ReportBuildOutcome outcome = builder->build(scene.request(ReportLevel::C));
    expectFailure(outcome, ReportErrorCode::ScopeInsufficient,
                  "C 域全缺＝ScopeInsufficient（P-RPT-5——不因签署块恒在而放行虚级）");
    EXPECT_TRUE(
        hasDiagnostic(outcome.diagnostics, std::string{diagcodes::kScopeInsufficient}))
        << "RPT-SCOPE-INSUFFICIENT 降级建议诊断随交付（§5.4 行 2）";
}

}  // namespace
