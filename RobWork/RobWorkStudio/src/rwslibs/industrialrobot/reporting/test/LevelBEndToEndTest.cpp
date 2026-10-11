/**
 * @file   LevelBEndToEndTest.cpp
 * @brief  B 级真实章节接入端到端集成测试（RPT-T14）——AT-32（B 部分）＋
 *         RP-SCOPE-1（真实数据）：真实链路（真实 envelope＋真实 ModelSummary
 *         ＋真实锚定元数据）经四域提供方注册联调产出 8 章节 B 级报告，逐章
 *         追溯到结果/工况/快照；C 级报告的结构性拒绝（阶段 B 交付的软件不
 *         允许生成 C 级报告——构建期结构承载非运行期开关）。
 *
 * 设计依据：
 *   - units/reporting.md §5.1（章节词表 14 项——B 级前 8 项的级别/所有者/
 *     数据源列；框架章节内建、域章节注册制）、§5.2（B/C 范围图——"B 级词
 *     表 ∩ C 专属词表＝∅；C 级零 C 章节选中＝ScopeInsufficient；报告级别
 *     ≠ 证据等级"）、§12.3（阶段 B/C 接入顺序约束——"真实章节内容不得以
 *     替身数据冒充验收（RP-STATE-4 边界——AT-32 验收必须真实链路）"）、
 *     §9.2（IReportSectionProvider 纯投影契约——域提供方注册联调的接口面）、
 *     §9.7（ModelSummary schema——input-summary 数据源）、§6.4 行 3（外部
 *     验证未完成限定语的章节载体）
 *   - 需求 RPT-01-B（B 级章节覆盖：项目与方案/输入摘要/模型/需求/运动学与
 *     碰撞/静态优化候选/诊断/外部验证边界）、EVI-02（覆盖一致基准的呈现
 *     面）、AT-32（完整工程报告逐章追溯到结果/工况/快照——B 部分口径）
 *   - 任务契约 tasks/foundation/RPT-T14.json（acceptance 1/2）
 *
 * 真实链路口径（本文件与替身的边界——RP-STATE-4 的执行面）：
 *   - "真实"指**数据流真实**：章节内容全部从注入数据投影而来——envelope
 *     经 evidence ResultEnvelope::make 构造边界产出（合法组合-only）、
 *     ModelSummary 经 RPT-T12 冻结 schema 组装、项目元数据经 project 契约
 *     类型组装；域章节条目由提供方适配器**消费 SectionRequest.results 的
 *     真实字段**（runId/工况集/证据清单/工程判定）投影，构建器做完整绑定
 *     校验（结果绑定越界/证据引用四面核对）后冻结——非 ScriptedSection
 *     Provider 的固定脚本内容；
 *   - 四域提供方适配器是**测试内的投影载体**：各域产品级 IReportSection-
 *     Provider 实现归各域单元（reporting.md §12.2 交接表——"各域章节提供
 *     方实现（阶段 B/C）"），本单元零业务域编译边（R-1/链接红线守卫），
 *     适配器仅以 evidence/core/project 登记边类型驱动报告链——其对端交付
 *     面由注册联调用例证明契约可用；
 *   - 框架章节投影器（BuiltinFrameworkSections）为产品代码——本文件直接
 *     消费其真实输出（不经替身）。
 *
 * 线程约束：单线程（构建会话单线程——§9.1；测试环境即属主线程）。
 */

#include <gtest/gtest.h>

#include <charconv>
#include <cstdint>
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
#include <sdurws/ird/diagnostics/DiagCodes.hpp>
#include <sdurws/ird/diagnostics/Redaction.hpp>
#include <sdurws/ird/evidence/Envelope.hpp>
#include <sdurws/ird/evidence/Snapshot.hpp>
#include <sdurws/ird/evidence/Verdict.hpp>
#include <sdurws/ird/project/PersistenceFormat.hpp>
#include <sdurws/ird/project/QueryPort.hpp>
#include <sdurws/ird/reporting/Builder.hpp>
#include <sdurws/ird/reporting/Consistency.hpp>
#include <sdurws/ird/reporting/Errors.hpp>
#include <sdurws/ird/reporting/FrameworkSections.hpp>
#include <sdurws/ird/reporting/ModelSummary.hpp>
#include <sdurws/ird/reporting/Render.hpp>
#include <sdurws/ird/reporting/ReportModel.hpp>
#include <sdurws/ird/reporting/SectionProvider.hpp>
#include <sdurws/ird/reporting/Sections.hpp>

#include "ScriptedResultSource.hpp"   // 具名替身正本（IReportResultSource 脚本面＋scripted* 工厂）

namespace {

using namespace sdurws::ird::reporting;
namespace core = sdurws::ird::core;
namespace evidence = sdurws::ird::evidence;
namespace project = sdurws::ird::project;
namespace diagnostics = sdurws::ird::diagnostics;
using test_fakes::ScriptedResultSource;

// =====================================================================
// 确定性身份铺位（禁随机——同输入同 contentIdentity 的断言前提）
// =====================================================================

core::ProjectId e2eProject()
{
    return test_fakes::scriptedId<core::ProjectId>(0x11);
}
core::BranchId e2eBranch()
{
    return test_fakes::scriptedId<core::BranchId>(0x12);
}
core::RevisionId e2eRevision()
{
    return test_fakes::scriptedId<core::RevisionId>(0x13);
}
core::RunId e2eRun(std::uint8_t seed)
{
    return test_fakes::scriptedId<core::RunId>(seed);
}
core::ObjectId e2eCase(std::uint8_t seed)
{
    return test_fakes::scriptedId<core::ObjectId>(seed);
}

// =====================================================================
// 查询端口测试桩（锚定视图＋锚定元数据——project 契约类型的真实组装）
// =====================================================================

class EndToEndQueryPort final : public project::IProjectQueryPort {
public:
    project::RevisionView view{};
    project::ProjectMetadataView metadata;
    bool metadataExists = true;

    EndToEndQueryPort()
    {
        // 锚定视图（§4.4.2 形态——seq/分支/命令摘要为确定性铺位值）。
        view.id = e2eRevision();
        view.seq = 9;
        view.branch = e2eBranch();
        view.commandSummary = "端到端案例锚定修订（模型＋需求＋评估三命令）";
        view.hasUnresolvedPayload = false;

        // 锚定元数据（project §4.4.3 冻结字段——分支表/显示名/主分支/方案
        // 清单；project-scheme 框架章节的数据源，锚定修订版）。
        project::BranchRecord branch;
        branch.branchId = e2eBranch();
        branch.label = "main";
        branch.tipRevisionId = view.id;
        metadata.record.projectDisplayName = "六轴搬运工作站方案 A";
        metadata.record.primaryBranchId = e2eBranch();
        metadata.record.branches = {branch};
        metadata.record.schemeLabels = {{"scheme", "plan-a"}, {"station", "palletizing"}};
    }

    // 调用记账（锚定恰一次＋零越权读的反向断言面）。
    mutable int headCalls = 0;
    mutable int tryRevisionCalls = 0;
    mutable int listRunsCalls = 0;
    mutable int metadataAtCalls = 0;

    project::RevisionView head() const override
    {
        ++headCalls;
        return view;
    }

    std::optional<project::RevisionView> tryRevision(core::RevisionId id) const override
    {
        ++tryRevisionCalls;
        if (!(id == view.id)) {
            return std::nullopt;
        }
        return view;
    }

    std::vector<project::RunInfo> listRuns(core::RevisionId) const override
    {
        ++listRunsCalls;
        return runs;
    }

    std::optional<project::ProjectMetadataView> metadataAt(core::RevisionId id) const override
    {
        ++metadataAtCalls;
        if (!metadataExists || !(id == view.id)) {
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
        throw std::logic_error(std::string{"EndToEndQueryPort: 意外方法 "} + method);
    }
};

/// finalize 运行清单行（listRuns 口径——评估键与任务五元组从 envelope 同源）。
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
// 模型摘要提供方（RPT-T12 冻结 schema 的真实值组装——input-summary 数据源）
// =====================================================================

/**
 * @brief 场景模型摘要提供方（IModelSummaryProvider 最小实现——持一份经
 *        RPT-T12 schema 公共构造面组装的 ModelSummary 值，同修订同投影）。
 *
 * 替身边界：验证 reporting 对注入面的消费，不构成 runtime 摘要行为证明
 * （runtime 侧适配归 L5——P-RPT-2）；摘要值为冻结 schema 的合法全字段
 * 形态（保留值纪律＋资源 Recorded/Solidified 两态并存——外部验证边界的
 * 限定语触发面）。
 */
class ScenarioModelSummaryProvider final : public IModelSummaryProvider {
public:
    explicit ScenarioModelSummaryProvider(ModelSummary summary) : m_summary(std::move(summary))
    {
    }

    std::optional<ModelSummary> trySummary(core::RevisionId revision) const override
    {
        if (!(revision == m_summary.revision)) {
            return std::nullopt;   // 闭包外修订——try 轨 nullopt（不伪造）
        }
        return m_summary;
    }

private:
    ModelSummary m_summary;
};

/// 端到端案例模型摘要（六轴链型＋能力六具备二缺失＋资源两态＋一份配置引用
/// ——确定性铺位值，全字段合法形态）。
ModelSummary e2eModelSummary()
{
    ModelSummary summary;
    summary.revision = e2eRevision();
    summary.snapshotIdentity = test_fakes::scriptedCid(0x70);
    summary.modelIdentity = test_fakes::scriptedCid(0x71);
    summary.nameMapIdentity = test_fakes::scriptedCid(0x72);
    summary.policyContentIdentity = test_fakes::scriptedCid(0x73);
    summary.jointCount = 6;       // 轴数（无量纲计数——六轴关节型机械臂）
    summary.allRevolute = true;   // 全旋转关节链
    summary.installPresetToken = "Ground";   // 呈现 token（禁止参与判定——§9.7）
    summary.capabilities.hasDynamicWorkCell = true;
    summary.capabilities.hasFullMassInertia = true;
    summary.capabilities.hasJointVelocityLimits = true;
    summary.capabilities.hasCollisionGeometry = true;
    summary.capabilities.hasTools = true;
    summary.capabilities.hasScene = true;
    summary.capabilities.hasFrictionModel = false;    // 能力缺失位（降级非阻断——如实）
    summary.capabilities.hasCouplingMatrix = false;
    ResourceSummary recorded;    // Recorded 态——external-validation-incomplete 限定语数据源
    recorded.resourceId = "mesh/conveyor-guard.step";
    recorded.state = ResourceStateKind::Recorded;
    recorded.digest = test_fakes::scriptedCid(0x74).bytes;
    ResourceSummary solidified;
    solidified.resourceId = "model/palletizer-canonical";
    solidified.state = ResourceStateKind::Solidified;
    solidified.digest = test_fakes::scriptedCid(0x75).bytes;
    summary.resources = {recorded, solidified};
    ConfigSummary config;
    config.configKindToken = "kin-analysis";
    config.contentIdentity = test_fakes::scriptedCid(0x76);
    summary.configurations = {config};
    return summary;
}

// =====================================================================
// 四域提供方适配器（真实数据投影——消费 SectionRequest.results 的真实字段）
// =====================================================================

/**
 * @brief 域章节提供方适配器基座（IReportSectionProvider 纯投影的最小实现
 *        框架——条目从注入 envelope 的真实字段投影，绑定与证据引用取自
 *        包络本体——"同请求同输出"的确定性形态）。
 *
 * 投影语义（各域同构）：恰一条目（entryKey 由子域给定）——
 *   - 结果绑定＝首条 envelope 的 runId＋fieldPath（payload 内定位路径——
 *     真实追溯锚，构建器核对可解析性）；
 *   - 证据绑定＝包络证据清单首条的 itemId/status/digest/caseScope（真实
 *     清单值——构建器做 itemId/状态/摘要/工况范围四面对包）；
 *   - 字段集＝评估键/工程判定 token/工况计数（真实值——文本侧与无量纲
 *     计数；工程判定以 core::toToken 规范词面呈现，不新增状态词）。
 */
class DomainSectionAdapter : public IReportSectionProvider {
public:
    DomainSectionAdapter(std::string sectionId, ReportLevel minLevel,
                         std::string evaluationKey, std::string entryKey,
                         std::string fieldPath)
        : m_sectionId(std::move(sectionId)), m_minLevel(minLevel),
          m_evaluationKey(std::move(evaluationKey)), m_entryKey(std::move(entryKey)),
          m_fieldPath(std::move(fieldPath))
    {
    }

    std::string sectionId() const override { return m_sectionId; }
    ReportLevel minimumLevel() const override { return m_minLevel; }
    std::vector<std::string> requiredEvaluationKeys() const override
    {
        return {m_evaluationKey};   // 声明消费的评估域——构建器据此过滤结果子集
    }

    SectionContent project(const SectionRequest& request) override
    {
        // 前置（§9.2"request.results 非空——构建器已过滤"）：空集＝编排
        // 违约（构建器在空集时不调用投影——测试自证该前置）。
        if (request.results.empty()) {
            throw std::logic_error("DomainSectionAdapter: 结果子集为空（§9.2 前置违约）");
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

protected:
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

private:
    std::string m_sectionId;
    ReportLevel m_minLevel;
    std::string m_evaluationKey;
    std::string m_entryKey;
    std::string m_fieldPath;
};

/// 四域适配器工厂（§5.1 词表行 3/4/5/6）。评估键词形权威归 evidence
/// isValidEvaluationKey（[a-z][a-z0-9-]{1,63}——不含点）；envelope 的
/// profileId 限五域词表 kin|trj|dyn|sel|opt——modeling/requirements 非
/// 评估域不产评估包络（N-5），其章节在本场景以"提供方注册在册＋该域无
/// 评估运行→构建器判定 NoFormalResult"的诚实形态呈现（不伪造评估事实）。
std::unique_ptr<IReportSectionProvider> modelSectionAdapter()
{
    // §5.1 行 3——模型：modeling 域提供方（消费声明＝其域键；阶段 B 场景
    // 无该域评估运行——构建器空集判定 NoFormalResult，不调投影）。
    return std::make_unique<DomainSectionAdapter>(std::string(kSectionModel), ReportLevel::B,
                                                  "mdl-model-projection", "model-summary",
                                                  "payload-model-summary");
}
std::unique_ptr<IReportSectionProvider> requirementsSectionAdapter()
{
    // §5.1 行 4——需求：requirements 域提供方（同上——无评估运行场景）。
    return std::make_unique<DomainSectionAdapter>(std::string(kSectionRequirements),
                                                  ReportLevel::B, "req-readiness-summary",
                                                  "readiness", "payload-readiness");
}
std::unique_ptr<IReportSectionProvider> kinematicsSectionAdapter()
{
    // §5.1 行 5——运动学与碰撞：kinematics 域（KIN-01~05 结果投影；消费
    // kin 域批量 IK 评估运行——真实对应）。
    return std::make_unique<DomainSectionAdapter>(std::string(kSectionKinematicsCollision),
                                                  ReportLevel::B, "kin-batch-ik",
                                                  "ik-converged", "payload-task-points-0");
}
std::unique_ptr<IReportSectionProvider> optimizationSectionAdapter()
{
    // §5.1 行 6——静态优化候选：optimization 域（OPT-B 候选投影；消费
    // opt 域静态筛选评估运行——真实对应）。
    return std::make_unique<DomainSectionAdapter>(std::string(kSectionOptimizationCandidates),
                                                  ReportLevel::B, "opt-static-screen",
                                                  "pareto-front", "payload-candidates-0");
}

// =====================================================================
// 真实包络工厂（evidence make 构造边界——各域评估键/证据清单各就各位）
// =====================================================================

/**
 * @brief 域评估包络（Completed×Feasible×Verified——正式结论形态；证据清单
 *        带 Satisfied 项；profile/评估键按域分派——四域结果的区分面）。
 *
 * 构造纪律：与具名替身同款——一律经 evidence ResultEnvelope::make（合法
 * 组合-only），非法组合在构造边界即被拒绝；评估键词形经 envelope 构造
 * 校验（evidence isValidEvaluationKey 词形权威的执行点）。
 */
evidence::ResultEnvelope domainEnvelope(const core::RunId& run, const std::string& evaluationKey,
                                        const std::string& profileId,
                                        const std::string& payloadId,
                                        std::vector<core::ObjectId> caseIds,
                                        const std::string& evidenceItemId)
{
    if (caseIds.empty()) {
        caseIds = {e2eCase(0x21)};
    }
    evidence::ResultEnvelopeDraft d;
    d.task = test_fakes::scriptedTask(run);
    d.evaluationKey = evaluationKey;
    d.evaluatorContractVersion = 7;
    d.mode = core::EvaluationMode::Verified;   // 正式模式（报告级别≠证据等级——B 级报告
                                               // 呈现 Verified 结果的载体）
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
// 端到端场景（真实链路注入三元组——查询端口/结果源/注册表/摘要提供方）
// =====================================================================

struct EndToEndScene {
    EndToEndQueryPort queryPort;
    ScriptedResultSource resultSource;
    SectionRegistry registry;
    std::optional<ModelSummary> summary = e2eModelSummary();   // 有值＝注入（缺项形态用例置 nullopt）
    std::unique_ptr<ScenarioModelSummaryProvider> summaryProvider;   // builder() 注入时创建

    /// 登记评估域运行（真实包络＋finalize 清单同源登记；modeling/requirements
    /// 非评估域——envelope profileId 五域词表 kin|trj|dyn|sel|opt 限制〔N-5〕
    /// ——不产评估运行，其章节以"注册在册＋缺正式结果"形态呈现）。
    void addDomainRuns()
    {
        struct RunDef {
            std::uint8_t seed;
            const char* evaluationKey;
            const char* profileId;
            const char* payloadId;
            const char* evidenceItemId;
        };
        static const RunDef kRuns[] = {
            {0xA3, "kin-batch-ik", "kin", "kin-batch-ik-v1", "kin.reach-per-task-point"},
            {0xA4, "opt-static-screen", "opt", "opt-static-screen-v1",
             "opt.hard-constraint-record"},
        };
        for (const RunDef& def : kRuns) {
            const core::RunId run = e2eRun(def.seed);
            evidence::ResultEnvelope envelope =
                domainEnvelope(run, def.evaluationKey, def.profileId, def.payloadId,
                               {e2eCase(0x21), e2eCase(0x22)}, def.evidenceItemId);
            queryPort.runs.push_back(runInfoOf(envelope));
            resultSource.withEnvelope(run, std::move(envelope));
        }
    }

    /// 注册四域提供方（装配期注册联调——SectionRegistry 的正常路径）。
    void registerDomainAdapters()
    {
        registry.registerProvider(modelSectionAdapter());
        registry.registerProvider(requirementsSectionAdapter());
        registry.registerProvider(kinematicsSectionAdapter());
        registry.registerProvider(optimizationSectionAdapter());
    }

    ReportBuildRequest request(ReportLevel level) const
    {
        ReportBuildRequest request;
        request.project = e2eProject();
        request.branch = e2eBranch();
        request.revision = e2eRevision();
        request.level = level;
        for (const project::RunInfo& info : queryPort.runs) {
            request.resultRuns.push_back(info.runId);
        }
        return request;
    }

    /// 构建器实例（摘要值在场即注入提供方——借用生命周期随本对象）。
    std::unique_ptr<ReviewReportBuilder> builder()
    {
        if (summary.has_value()) {
            summaryProvider = std::make_unique<ScenarioModelSummaryProvider>(*summary);
            summary.reset();
        }
        return std::make_unique<ReviewReportBuilder>(queryPort, resultSource, registry, nullptr,
                                                     summaryProvider.get());
    }
};

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

/// 失败形态断言（report 空＋error 码面——§9.1 后置"失败＝无报告对象"）。
void expectFailure(const ReportBuildOutcome& outcome, ReportErrorCode code)
{
    ASSERT_EQ(outcome.report, nullptr) << "失败路径不得产生报告对象（§4.2①）";
    ASSERT_TRUE(outcome.error.has_value());
    EXPECT_EQ(outcome.error->code(), code);
}

// =====================================================================
// 用例 1——AT-32（B 部分）主正例：8 章节 B 级报告逐章追溯（真实链路）
// =====================================================================

/**
 * AT-32（B 部分）＋RP-SCOPE-1（真实数据）主正例：四域真实包络＋真实模型
 * 摘要＋真实锚定元数据 → 注册联调 → B 级报告 8 章节按词表序全量在列；
 * 域章节逐章追溯到结果（runId→resultRefs）/工况（caseScope ⊆ 结果范围）/
 * 快照（snapshotId 追溯链），框架章节呈现真实平台事实（元数据穿透断言）。
 */
TEST(LevelBEndToEnd, EightSectionsRealChain_At32B_RPT14)
{
    EndToEndScene scene;
    scene.addDomainRuns();
    scene.registerDomainAdapters();
    const std::unique_ptr<ReviewReportBuilder> builder = scene.builder();

    const ReportBuildOutcome outcome = builder->build(scene.request(ReportLevel::B));
    ASSERT_NE(outcome.report, nullptr) << "端到端正例必须构建成功";
    EXPECT_FALSE(outcome.error.has_value());
    const ReviewReport& report = *outcome.report;

    // ---- 报告骨架：B 级＋8 章节按词表序（RP-SCOPE-1"章节集＝§5.1 前 8 项
    // 且按 order"——真实数据版）。
    EXPECT_EQ(report.level(), ReportLevel::B);
    const std::vector<std::string> bScope = sectionsInScope(ReportLevel::B);
    ASSERT_EQ(bScope.size(), std::size_t{8});
    ASSERT_EQ(report.sections().size(), bScope.size());
    for (std::size_t i = 0; i < report.sections().size(); ++i) {
        EXPECT_EQ(report.sections()[i].sectionId, bScope[i]) << "组装序＝词表行号序";
        EXPECT_EQ(report.sections()[i].order, *trySectionOrder(bScope[i])) << "order＝词表行号";
    }

    // ---- 结果引用快照：两个评估域运行（三轴分列——Verified×Completed×
    // Feasible；modeling/requirements 非评估域无运行——不伪造评估事实）。
    ASSERT_EQ(report.resultRefs().size(), std::size_t{2});
    std::map<std::string, const ResultRefSnapshot*> refIndex;
    for (const ResultRefSnapshot& ref : report.resultRefs()) {
        refIndex.emplace(ref.runId.toCanonical(), &ref);
        EXPECT_EQ(ref.mode, core::EvaluationMode::Verified);
        EXPECT_EQ(ref.outcome, core::TaskOutcome::Completed);
        EXPECT_EQ(ref.engineeringStatus, core::EngineeringStatus::Feasible);
        // 追溯链（NFR-COR-04）：snapshotId/sliceId/inputBaselineId 三锚非零。
        EXPECT_TRUE(ref.snapshotId.isValid());
        EXPECT_TRUE(ref.sliceId.isValid());
        EXPECT_TRUE(ref.inputBaselineId.isValid());
    }

    // ---- 评估域章节逐章追溯（AT-32 B 部分"逐章追溯到结果/工况/快照"——
    // kinematics-collision＋optimization-candidates 两章有评估结果）。
    for (const std::string_view domainId :
         {kSectionKinematicsCollision, kSectionOptimizationCandidates}) {
        const ReviewReportSection& section = sectionOf(report, domainId, "评估域章节");
        ASSERT_EQ(section.status, SectionStatus::Populated);
        EXPECT_TRUE(section.selected) << "Populated 默认选中（§5 默认规则）";
        ASSERT_FALSE(section.entries.empty());
        for (const SectionEntryView& entry : section.entries) {
            // 结果绑定可解析（追溯链第一跳——RP-TRACE-1 同口径）。
            const auto refIt = refIndex.find(entry.result.runId.toCanonical());
            ASSERT_TRUE(refIt != refIndex.end())
                << section.sectionId << ": 结果绑定不可解析——" << entry.result.runId.toCanonical();
            // 工况范围 ⊆ 所属结果工况范围（§4.7 关系约束）。
            const ResultRefSnapshot& bound = *refIt->second;
            ASSERT_FALSE(bound.caseScope.empty());
            for (const core::ObjectId& caseId : entry.caseScope) {
                bool contained = false;
                for (const core::ObjectId& scope : bound.caseScope) {
                    if (scope == caseId) {
                        contained = true;
                    }
                }
                EXPECT_TRUE(contained) << "条目工况越界（§4.7）";
            }
            // 证据绑定与包络清单一致（itemId 在场＋Satisfied 摘要一致）。
            ASSERT_FALSE(entry.evidence.empty());
            for (const EvidenceBinding& binding : entry.evidence) {
                bool found = false;
                for (const EvidenceRefEntry& ref : report.evidenceRefs()) {
                    if (ref.itemId == binding.itemId
                        && ref.sourceSection == section.sectionId) {
                        found = true;
                    }
                }
                EXPECT_TRUE(found) << "证据引用进入报告级并集（§4.2）";
            }
            // 章节级资格聚合（B 级报告呈现 Verified 结果——报告级别≠证据
            // 等级：正式通过资格在 B 级报告照常成立——§5.2 范围图注记行）。
            ASSERT_TRUE(section.eligibilityNote.has_value());
            EXPECT_TRUE(section.eligibilityNote->formalPassAllowed)
                << section.sectionId << ": Verified 正式结果的 formalPass 资格聚合成立";
        }
    }

    // ---- 非评估域两章节（model/requirements）：提供方注册在册但该域无评估
    // 运行——构建器过滤空集判定 NoFormalResult（§5.3 进入条件"提供方声明或
    // 构建器判定"的构建器半边；不调投影——§9.2 前置），缺项表达非空壳。
    for (const std::string_view domainId : {kSectionModel, kSectionRequirements}) {
        const ReviewReportSection& section = sectionOf(report, domainId, "非评估域章节");
        EXPECT_EQ(section.status, SectionStatus::NoFormalResult);
        EXPECT_FALSE(section.selected) << "缺正式结果默认不选（§16 验收要点）";
        ASSERT_FALSE(section.missingItems.empty()) << "缺项清单在场（不伪造评估事实）";
        EXPECT_TRUE(section.entries.empty());
    }

    // ---- 框架四章节的真实数据穿透断言（内建投影器输出＝注入事实）。
    // 行 1 project-scheme：显示名/方案标签来自锚定元数据（§5.1 行 1 数据源）。
    const ReviewReportSection& scheme = sectionOf(report, kSectionProjectScheme, "行 1");
    EXPECT_EQ(scheme.status, SectionStatus::Populated);
    {
        bool displayName = false;
        bool schemeLabel = false;
        bool revisionSummary = false;
        for (const SectionEntryView& entry : scheme.entries) {
            for (const FieldValue& field : entry.fields) {
                if (field.key == "display-name" && field.text.has_value()
                    && *field.text == "六轴搬运工作站方案 A") {
                    displayName = true;
                }
                if (field.key == "scheme-labels" && field.text.has_value()
                    && field.text->find("scheme=plan-a") != std::string::npos) {
                    schemeLabel = true;
                }
                if (field.key == "revision-summary" && field.text.has_value()
                    && !field.text->empty()) {
                    revisionSummary = true;
                }
            }
        }
        EXPECT_TRUE(displayName) << "项目显示名＝锚定元数据值（真实穿透）";
        EXPECT_TRUE(schemeLabel) << "方案清单＝锚定元数据值";
        EXPECT_TRUE(revisionSummary) << "修订摘要＝锚定视图值";
    }

    // 行 2 input-summary：快照身份/链型摘要/资源清单/配置引用来自 ModelSummary。
    const ReviewReportSection& inputSummary = sectionOf(report, kSectionInputSummary, "行 2");
    EXPECT_EQ(inputSummary.status, SectionStatus::Populated);
    {
        bool snapshot = false;
        bool jointCount = false;
        std::size_t resourceEntries = 0;
        std::size_t configEntries = 0;
        const std::string expectedSnapshot = e2eModelSummary().snapshotIdentity.toCanonical();
        for (const SectionEntryView& entry : inputSummary.entries) {
            if (entry.entryKey.rfind("resource-", 0) == 0) {
                ++resourceEntries;
            }
            if (entry.entryKey.rfind("configuration-", 0) == 0) {
                ++configEntries;
            }
            for (const FieldValue& field : entry.fields) {
                if (field.key == "snapshot" && field.text.has_value()
                    && *field.text == expectedSnapshot) {
                    snapshot = true;
                }
                if (field.key == "joint-count"
                    && field.quantity.state() == core::FieldState::Provided
                    && field.quantity.tryValue().value_or(0.0) == 6.0) {
                    jointCount = true;   // 轴数＝ModelSummary.jointCount（无量纲计数）
                }
            }
        }
        EXPECT_TRUE(snapshot) << "快照身份＝ModelSummary 值（真实穿透）";
        EXPECT_TRUE(jointCount) << "轴数＝ModelSummary 值";
        EXPECT_EQ(resourceEntries, std::size_t{2}) << "资源清单两条（Recorded＋Solidified）";
        EXPECT_EQ(configEntries, std::size_t{1}) << "配置引用一条";
    }

    // 行 7 diagnostics：无诊断场景＝显式不适用（四域包络无诊断——NotApplicable
    // 不计缺失，§5.3；不适用原因在提供方产出面承载——SectionContent 契约，
    // 报告侧以状态词呈现）。
    const ReviewReportSection& diag = sectionOf(report, kSectionDiagnostics, "行 7");
    EXPECT_EQ(diag.status, SectionStatus::NotApplicable);
    EXPECT_TRUE(diag.selected) << "NotApplicable 默认选中（破折号＋原因——§5 默认规则）";
    EXPECT_TRUE(diag.entries.empty()) << "不适用章节无条目（破折号呈现）";

    // 行 8 external-validation-boundary：复现块条目＋资源两态（Recorded 态即
    // 外部验证未完成限定语的数据源——§6.4 行 3）。
    const ReviewReportSection& external = sectionOf(report, kSectionExternalValidationBoundary,
                                                    "行 8");
    EXPECT_EQ(external.status, SectionStatus::Populated);
    {
        bool reproduction = false;
        std::size_t resourceEntries = 0;
        for (const SectionEntryView& entry : external.entries) {
            if (entry.entryKey == "reproduction") {
                reproduction = true;
            }
            if (entry.entryKey.rfind("resource-", 0) == 0) {
                ++resourceEntries;
            }
        }
        EXPECT_TRUE(reproduction) << "复现块条目在场（§4.2 reproduction 必填的呈现面）";
        EXPECT_EQ(resourceEntries, std::size_t{2}) << "资源固化状态两条";
    }
    // 报告级外部资源状态（external-validation-incomplete 限定语的数据面——
    // v0.9 登记"随框架章节数源任务收口"的兑现断言）。
    ASSERT_EQ(report.externalResourceSummary().size(), std::size_t{2});
    EXPECT_EQ(report.externalResourceSummary()[0].state, ExternalResourceState::Recorded);
    EXPECT_EQ(report.externalResourceSummary()[1].state, ExternalResourceState::Solidified);

    // ---- 锚定纪律：head/tryRevision/listRuns/metadataAt 各恰一次。
    EXPECT_EQ(scene.queryPort.headCalls, 1);
    EXPECT_EQ(scene.queryPort.tryRevisionCalls, 1);
    EXPECT_EQ(scene.queryPort.listRunsCalls, 1);
    EXPECT_EQ(scene.queryPort.metadataAtCalls, 1);
}

// =====================================================================
// io 注入替身（JSON/CSV 渲染用——最小确定性形态；夹具自持不共享——
// RenderTest 同款纪律：io canonical 行为的最小镜像，真实行为归 io 验证
// 矩阵 SA-12，替身只驱动渲染器注入缝）
// =====================================================================

class E2eCsvWriter final : public IReportCsvWriter {
public:
    bool open(IReportOutputTarget&& target, bool emitDialectMarker) override
    {
        m_target = &target;
        if (emitDialectMarker) {
            static const std::string kMarker =
                "#rwcsv1 delimiter=, quote=\" eol=CRLF encoding=utf-8\r\n";
            return m_target->write(reinterpret_cast<const std::uint8_t*>(kMarker.data()),
                                   kMarker.size());
        }
        return true;
    }
    bool writeHeader(const std::vector<std::string>& columns) override
    {
        return writeLine(columns);
    }
    bool writeRow(const std::vector<ReportCsvCell>& cells) override
    {
        std::vector<std::string> texts;
        texts.reserve(cells.size());
        for (const ReportCsvCell& cell : cells) {
            switch (cell.kind) {
            case ReportCsvCell::Kind::Empty:   texts.emplace_back(); break;
            case ReportCsvCell::Kind::Text:    texts.push_back(cell.text); break;
            case ReportCsvCell::Kind::Integer: texts.push_back(std::to_string(cell.integer)); break;
            case ReportCsvCell::Kind::Number:  texts.push_back(toChars(cell.number)); break;
            }
        }
        return writeLine(texts);
    }
    bool finish() override { return true; }

private:
    static std::string toChars(double value)
    {
        char buffer[64];
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
        return std::string(buffer, result.ptr);
    }
    bool writeLine(const std::vector<std::string>& texts)
    {
        std::string line;
        for (std::size_t i = 0; i < texts.size(); ++i) {
            if (i > 0) {
                line += ',';
            }
            if (texts[i].find_first_of(",\"\r\n") != std::string::npos) {
                line += '"';
                for (const char c : texts[i]) {
                    line += (c == '"') ? "\"\"" : std::string(1, c);
                }
                line += '"';
            } else {
                line += texts[i];
            }
        }
        line += "\r\n";
        return m_target->write(reinterpret_cast<const std::uint8_t*>(line.data()), line.size());
    }
    IReportOutputTarget* m_target = nullptr;   ///< 会话期借用（open→finish）
};

class E2eJsonWriter final : public IReportJsonWriter {
public:
    bool write(IReportOutputTarget&& target, const ReportJsonDom& dom) override
    {
        m_target = &target;
        const std::string text = serialize(dom, 0);
        return m_target->write(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
    }

private:
    static std::string escape(const std::string& raw)
    {
        std::string out;
        for (const char c : raw) {
            switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;      break;
            }
        }
        return out;
    }
    static std::string number(double value)
    {
        char buffer[64];
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
        return std::string(buffer, result.ptr);
    }
    std::string serialize(const ReportJsonDom& dom, int depth) const
    {
        const std::string pad(static_cast<std::size_t>(depth) * 2, ' ');
        const std::string innerPad(static_cast<std::size_t>(depth + 1) * 2, ' ');
        switch (dom.type()) {
        case ReportJsonDom::Type::Null:   return "null";
        case ReportJsonDom::Type::Bool:   return dom.booleanValue() ? "true" : "false";
        case ReportJsonDom::Type::Number: return number(dom.numberValue());
        case ReportJsonDom::Type::String: return "\"" + escape(dom.stringValue()) + "\"";
        case ReportJsonDom::Type::Array: {
            if (dom.arrayItems().empty()) {
                return "[]";
            }
            std::string out = "[\n";
            for (std::size_t i = 0; i < dom.arrayItems().size(); ++i) {
                out += innerPad + serialize(dom.arrayItems()[i], depth + 1);
                if (i + 1 < dom.arrayItems().size()) {
                    out += ',';
                }
                out += '\n';
            }
            return out + pad + "]";
        }
        case ReportJsonDom::Type::Object: {
            if (dom.objectMembers().empty()) {
                return "{}";
            }
            std::string out = "{\n";
            for (std::size_t i = 0; i < dom.objectMembers().size(); ++i) {
                out += innerPad + "\"" + escape(dom.objectMembers()[i].first) + "\": "
                       + serialize(dom.objectMembers()[i].second, depth + 1);
                if (i + 1 < dom.objectMembers().size()) {
                    out += ',';
                }
                out += '\n';
            }
            return out + pad + "}";
        }
        }
        return "";
    }
    IReportOutputTarget* m_target = nullptr;   ///< 会话期借用
};

/// io 工厂替身（工厂零行为——产出独立 writer，§9.5；无状态并发安全）。
class E2eIoFactory final : public IReportIoFactory {
public:
    std::unique_ptr<IReportCsvWriter> makeCsvWriter() const override
    {
        return std::make_unique<E2eCsvWriter>();
    }
    std::unique_ptr<IReportJsonWriter> makeJsonWriter() const override
    {
        return std::make_unique<E2eJsonWriter>();
    }
    std::unique_ptr<IReportOutputTarget> makeAtomicTarget(const std::filesystem::path&,
                                                          ReplacePolicy) const override
    {
        return nullptr;   // 渲染器不消费原子目标（落盘归导出链 RPT-T09）
    }
};

/// CSV 回读器（一致性检查 CSV 维度载体——最小确定性解析：方言行跳过/
/// 空行分表/头行判别/引号字段还原；与 E2eCsvWriter 的写出形态互为镜像，
/// 真实转义正确性归 io 验证矩阵——NFR-SEC-03/SA-12）。
class E2eCsvReader final : public IReportCsvReader {
public:
    bool readTables(const std::uint8_t* bytes, std::size_t size,
                    std::vector<ReportCsvTable>& out) override
    {
        if (bytes == nullptr && size != 0) {
            return false;
        }
        const std::string text(bytes == nullptr ? "" : reinterpret_cast<const char*>(bytes), size);
        ReportCsvTable current;
        bool inTable = false;
        auto flush = [&current, &inTable, &out] {
            if (inTable && !current.header.empty()) {
                out.push_back(current);
            }
            current = ReportCsvTable{};
            inTable = false;
        };
        std::string line;
        for (std::size_t i = 0; i <= text.size(); ++i) {
            if (i < text.size() && text[i] != '\n') {
                line.push_back(text[i]);
                continue;
            }
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line.rfind("#rwcsv1", 0) == 0) {
                line.clear();   // 方言标识行（§8.3——不入表结构）
                continue;
            }
            if (line.empty()) {
                flush();   // 文档间分隔（三份 CSV 文档 LF 空行拼接）
                line.clear();
                continue;
            }
            if (line.rfind("field_key", 0) == 0 || line.rfind("code,", 0) == 0
                || line.rfind("case_id,", 0) == 0) {
                flush();   // 新文档头行（三份冻结列序的首列字面）
                inTable = true;
                current.header = parseRow(line);
                line.clear();
                continue;
            }
            if (inTable) {
                current.rows.push_back(parseRow(line));
            }
            line.clear();
        }
        flush();
        return !out.empty();
    }

private:
    /// 单行 RFC4180 解析（引号字段/双引号转义——写出形态的最小镜像）。
    static std::vector<std::string> parseRow(const std::string& line)
    {
        std::vector<std::string> row;
        std::string field;
        bool inQuotes = false;
        for (std::size_t i = 0; i < line.size(); ++i) {
            const char c = line[i];
            if (inQuotes) {
                if (c == '"') {
                    if (i + 1 < line.size() && line[i + 1] == '"') {
                        field.push_back('"');
                        ++i;
                    } else {
                        inQuotes = false;
                    }
                } else {
                    field.push_back(c);
                }
            } else if (c == '"' && field.empty()) {
                inQuotes = true;
            } else if (c == ',') {
                row.push_back(field);
                field.clear();
            } else {
                field.push_back(c);
            }
        }
        row.push_back(field);
        return row;
    }
};

/// CSV 回读工厂（每次产出独立 reader——§9.4 纯函数纪律的注入面）。
class E2eCsvReaderFactory final : public IReportCsvReaderFactory {
public:
    std::unique_ptr<IReportCsvReader> makeCsvReader() const override
    {
        return std::make_unique<E2eCsvReader>();
    }
};

// =====================================================================
// 用例 2——三格式渲染与一致性端到端（8 章节全链——AT-22 呈现面）
// =====================================================================

/**
 * 8 章节 B 级报告 → 字段矩阵单次提取 → HTML/JSON/CSV 三格式渲染 →
 * 逐字段一致性校验零 mismatch（框架条目的 status/resultRef/currentness
 * 缺席面在三格式一致承载——可选维度的缺席语义）；HTML 含框架章节的
 * 机器锚（逐章呈现面）。
 */
TEST(LevelBEndToEnd, RenderAndConsistencyAcrossThreeFormats_RPT14)
{
    EndToEndScene scene;
    scene.addDomainRuns();
    scene.registerDomainAdapters();
    const std::unique_ptr<ReviewReportBuilder> builder = scene.builder();
    const ReportBuildOutcome outcome = builder->build(scene.request(ReportLevel::B));
    ASSERT_NE(outcome.report, nullptr);
    const ReviewReport& report = *outcome.report;

    // 字段矩阵单次提取（§8.4——三格式同源的 D-07 结构前提）。
    const FieldMatrix matrix = extractFieldMatrix(report);
    EXPECT_FALSE(matrix.empty());
    // 框架条目的缺席维度（矩阵事实面）：域条目 status 在场、框架条目缺席
    // ——绑定语义分化在渲染层的投影面。
    std::size_t frameworkCells = 0;
    std::size_t frameworkCellsWithStatus = 0;
    std::size_t domainCells = 0;
    std::size_t domainCellsWithoutStatus = 0;
    for (const FieldCell& cell : matrix) {
        if (isFrameworkSectionId(cell.sectionId)) {
            ++frameworkCells;
            if (cell.status.has_value()) {
                ++frameworkCellsWithStatus;
            }
        } else {
            ++domainCells;
            if (!cell.status.has_value()) {
                ++domainCellsWithoutStatus;
            }
        }
    }
    EXPECT_GT(frameworkCells, std::size_t{0}) << "框架章节进入字段矩阵（三格式呈现面）";
    EXPECT_EQ(frameworkCellsWithStatus, std::size_t{0})
        << "框架条目无工程判定（元数据条目——无绑定语义）";
    EXPECT_GT(domainCells, std::size_t{0});
    EXPECT_EQ(domainCellsWithoutStatus, std::size_t{0}) << "域条目 status 维度全在场";

    // 三格式渲染（脱敏＝C-3 登记边真实 RedactionService；JSON/CSV 经注入
    // io 工厂——P-RPT-1 注入形态）。
    static diagnostics::RedactionService redaction;
    E2eIoFactory ioFactory;
    const RenderOutcome htmlOutcome =
        HtmlReportRenderer(redaction).render(report, matrix, ReportRenderFormat::Html);
    const RenderOutcome jsonOutcome =
        JsonReportRenderer(ioFactory, redaction).render(report, matrix, ReportRenderFormat::Json);
    const RenderOutcome csvOutcome =
        CsvReportRenderer(ioFactory, redaction).render(report, matrix, ReportRenderFormat::Csv);
    ASSERT_TRUE(htmlOutcome.artifact.has_value());
    ASSERT_TRUE(jsonOutcome.artifact.has_value());
    ASSERT_TRUE(csvOutcome.artifact.has_value());
    const RenderArtifact& html = *htmlOutcome.artifact;
    const RenderArtifact& json = *jsonOutcome.artifact;
    const RenderArtifact& csv = *csvOutcome.artifact;

    // 框架章节在 HTML 在场（逐章呈现面——章节 token 可检索）。
    const std::string htmlText(html.bytes.begin(), html.bytes.end());
    for (const std::string_view frameworkId :
         {kSectionProjectScheme, kSectionInputSummary, kSectionDiagnostics,
          kSectionExternalValidationBoundary}) {
        EXPECT_NE(htmlText.find(std::string(frameworkId)), std::string::npos)
            << "框架章节在 HTML 在场: " << frameworkId;
    }

    // 三格式逐字段一致性（§8.5——AT-22 的执行面；框架缺席维度三格式归一；
    // CSV 维度经注入回读工厂——P-RPT-1 注入形态）。
    std::vector<const RenderArtifact*> artifacts = {&html, &json, &csv};
    E2eCsvReaderFactory csvReaders;
    FieldConsistencyChecker checker(csvReaders);
    ConsistencyInput input;
    input.report = &report;
    input.matrix = &matrix;
    input.artifacts = artifacts;
    const ConsistencyResult consistency = checker.check(input);
    EXPECT_TRUE(consistency.mismatches.empty()) << "三格式逐字段一致（含框架条目缺席面）";
    for (const FieldMismatch& mismatch : consistency.mismatches) {
        ADD_FAILURE() << std::string(token(mismatch.format)) << " " << mismatch.fieldKey
                      << " expected=[" << mismatch.expected << "] actual=[" << mismatch.actual
                      << "]";
    }
}

// =====================================================================
// 用例 3——C 级结构性拒绝（阶段 B 交付的软件不允许生成 C 级报告）
// =====================================================================

/**
 * 结构性防伪造口径（非运行期开关）：阶段 B 软件中 C 章节提供方（trajectory/
 * dynamics/drivetrain/selection）与 C 专属框架投影均不存在——C 级请求组装
 * 出的 C 章节全部缺正式结果 → ScopeInsufficient 拒绝＋RPT-SCOPE-INSUFFICIENT
 * 降级建议诊断（P-RPT-5 契约词）。同场景 B 级请求照常成功——拒绝是级别
 * 范围的结构事实，不是全局开关。
 */
TEST(LevelBEndToEnd, CLevelRequestStructurallyRejected_ScopeInsufficient_RPT14)
{
    EndToEndScene scene;
    scene.addDomainRuns();
    scene.registerDomainAdapters();
    const std::unique_ptr<ReviewReportBuilder> builder = scene.builder();

    // C 级请求：C 章节全部缺正式结果（提供方未注册＝缺项非空壳）→拒绝。
    const ReportBuildOutcome cOutcome = builder->build(scene.request(ReportLevel::C));
    expectFailure(cOutcome, ReportErrorCode::ScopeInsufficient);
    bool foundAdvice = false;
    for (const core::DiagnosticRecord& diag : cOutcome.diagnostics) {
        if (diag.code == std::string(diagcodes::kScopeInsufficient)) {
            foundAdvice = true;
            // P-RPT-5 保守处置契约词：显式确认＋B 级重建＋非静默降级。
            EXPECT_NE(diag.recommendedAction.find("B 级"), std::string::npos);
            EXPECT_NE(diag.recommendedAction.find("显式确认"), std::string::npos);
        }
    }
    EXPECT_TRUE(foundAdvice) << "RPT-SCOPE-INSUFFICIENT 诊断随拒绝上报（NFR-COR-03）";

    // 同场景 B 级请求照常成功（结构性拒绝的作用域＝C 级别范围——非全局
    // 开关；同输入同级别可复现）。
    const std::unique_ptr<ReviewReportBuilder> builderB = scene.builder();
    const ReportBuildOutcome bOutcome = builderB->build(scene.request(ReportLevel::B));
    ASSERT_NE(bOutcome.report, nullptr) << "B 级请求不受 C 级拒绝影响";
    EXPECT_EQ(bOutcome.report->sections().size(), std::size_t{8});
}

/**
 * LevelConflict 面（§4.6 行 2——"B 级词表 ∩ C 专属词表＝∅，不能伪造 C 级
 * 章节"）：B 级请求携带 C 专属章节覆盖＝构建边界拒绝。
 */
TEST(LevelBEndToEnd, BLevelRequestWithCSectionOverride_Rejected_RPT14)
{
    EndToEndScene scene;
    scene.addDomainRuns();
    scene.registerDomainAdapters();
    const std::unique_ptr<ReviewReportBuilder> builder = scene.builder();

    ReportBuildRequest request = scene.request(ReportLevel::B);
    request.sectionOverrides = {
        SectionSelection{std::string(kSectionTrajectoryCycle), true}};
    const ReportBuildOutcome outcome = builder->build(request);
    expectFailure(outcome, ReportErrorCode::LevelConflict);
}

// =====================================================================
// 用例 4——框架注册拒绝面与元数据缺项形态（联调防护与诚实表达）
// =====================================================================

/**
 * 框架章节"不注册、不可覆盖"（§5.1 词表规则）：把框架 token 注册进
 * SectionRegistry＝装配错误（SectionRegistry 第③条注册边界）——联调时
 * 域适配器误注册框架章节的防护面（Usage fail-fast）。
 */
TEST(LevelBEndToEnd, FrameworkSectionRegistrationRejected_RPT14)
{
    SectionRegistry registry;
    // 试图把 input-summary 注册为域提供方——框架章节不可注册。
    EXPECT_THROW(registry.registerProvider(std::make_unique<DomainSectionAdapter>(
                     std::string(kSectionInputSummary), ReportLevel::B, "req.readiness-summary",
                     "readiness", "payload.readiness")),
                 ReportError);
    EXPECT_TRUE(registry.registeredSections().empty()) << "拒绝注册不留残余";
}

/**
 * 数据源缺位的诚实表达（ERR-01 不伪造）：模型摘要提供方未注入——
 * input-summary＝NoFormalResult＋缺项清单（报告仍生成）；项目元数据不可
 * 解析——project-scheme＝NoFormalResult＋缺项清单。
 */
TEST(LevelBEndToEnd, MissingDataSourcesExpressedAsMissingItems_RPT14)
{
    EndToEndScene scene;
    scene.addDomainRuns();
    scene.registerDomainAdapters();
    scene.summary.reset();   // 不注入模型摘要（L5 装配缺席形态）
    scene.queryPort.metadataExists = false;   // 锚定元数据不可解析
    const std::unique_ptr<ReviewReportBuilder> builder = scene.builder();

    const ReportBuildOutcome outcome = builder->build(scene.request(ReportLevel::B));
    ASSERT_NE(outcome.report, nullptr) << "数据源缺位＝章节缺项表达，报告不因此失败";
    const ReviewReport& report = *outcome.report;

    // input-summary：NoFormalResult＋缺项（提供方未装配——不伪造空摘要）。
    const ReviewReportSection& inputSummary = sectionOf(report, kSectionInputSummary, "行 2");
    EXPECT_EQ(inputSummary.status, SectionStatus::NoFormalResult);
    EXPECT_FALSE(inputSummary.selected) << "缺正式结果默认不选（§16 验收要点）";
    ASSERT_FALSE(inputSummary.missingItems.empty());
    EXPECT_EQ(inputSummary.missingItems[0].itemId, "model-summary");

    // project-scheme：NoFormalResult＋缺项（元数据不可解析——不伪造元数据）。
    const ReviewReportSection& scheme = sectionOf(report, kSectionProjectScheme, "行 1");
    EXPECT_EQ(scheme.status, SectionStatus::NoFormalResult);
    ASSERT_FALSE(scheme.missingItems.empty());
    EXPECT_EQ(scheme.missingItems[0].itemId, "project-metadata");

    // external-validation-boundary 仍 Populated（复现块条目独立于模型摘要——
    // 框架投影的诚实分解：缺什么表达什么，其余照常呈现）。
    const ReviewReportSection& external =
        sectionOf(report, kSectionExternalValidationBoundary, "行 8");
    EXPECT_EQ(external.status, SectionStatus::Populated);
    // 报告级外部资源状态＝空集（未获知——非"确认无外部资源"，§4.2）。
    EXPECT_TRUE(report.externalResourceSummary().empty());
}

}  // namespace