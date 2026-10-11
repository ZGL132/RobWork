/**
 * @file   FrameworkSections.cpp
 * @brief  B 级框架章节内建投影器实现（FrameworkSections.hpp 契约的编排体
 *         ——RPT-T14；四章节条目设计依据见头文件注与 §5.1 词表行 1/2/7/8
 *         的主要数据源列）。
 *
 * 实现口径：
 *   - 全部条目为**元数据条目**（非结论条目）：不带 ResultBinding/
 *     EvidenceBinding（§9.2"结论条目必填"的分化面——数据源为平台冻结
 *     事实，不来自评估运行；伪造绑定违反 NFR-COR-04），字段以文本侧或
 *     无量纲计数呈现；
 *   - 数值字段一律 UserProvided 来源（元数据的来源就是"平台记录本身"
 *     ——不冒充几何估算/导入来源，ProvenanceKind 词表消费纪律）；
 *   - 确定性：条目序固定（分派内硬编码序）、无时钟/locale 依赖——同
 *     输入同 SectionContent（NFR-COR-02；构建器绑定校验与渲染字段矩阵
 *     的稳定性前提）；
 *   - 错误语义：分派到非框架 token＝调用方违约（Usage fail-fast）；数据
 *     源缺位＝缺项表达（NoFormalResult＋缺项清单——ERR-01 不伪造），
 *     不抛业务异常（§9.2 错误行"提供方以 SectionContent 表达缺项"的
 *     内建侧对偶）。
 */

#include <sdurws/ird/reporting/FrameworkSections.hpp>

#include <sdurws/ird/reporting/ModelSummary.hpp>  // ResourceStateKind toToken（资源状态词面）

#include <string>
#include <utility>

namespace sdurws::ird::reporting {
namespace {

// ---------------------------------------------------------------------
// 投影辅助（文件局部——零公共契约面）
// ---------------------------------------------------------------------

/// 文本字段工厂（纯文本侧——与数值侧互斥，§9.2 字段形状）。
FieldValue textField(std::string key, std::string value)
{
    FieldValue field;
    field.key = std::move(key);
    field.text = std::move(value);
    return field;
}

/// 无量纲计数字段工厂（Provided×UserProvided——元数据计数的来源就是平台
/// 记录本身，不冒充估算/导入来源）。
FieldValue countField(std::string key, std::uint64_t count)
{
    FieldValue field;
    field.key = std::move(key);
    field.quantity = core::SourcedValue<double>::provided(
        static_cast<double>(count), core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
    return field;
}

/// 身份规范文本（全零保留值＝"未提供"→"—"；非全零→规范文本——UX-02
/// 允许域：规范身份文本不脱敏）。
std::string cidOrDash(const core::ContentIdentity& cid)
{
    return cid.isValid() ? cid.toCanonical() : std::string("-");
}

/// 摘要字节前 12 hex 的呈现形（§8.3"itemId@digest 前 12 hex"同口径的
/// 短摘要呈现——全零＝未提供→"—"；Digest256＝std::array<uint8_t,32> 别名，
/// 无公共文本化接口，此处为呈现层最小 hex 编码，非第二摘要算法）。
std::string digestShortOrDash(const core::Digest256& digest)
{
    static const char* kHex = "0123456789abcdef";
    bool zero = true;
    for (const std::uint8_t b : digest) {
        if (b != 0) {
            zero = false;
            break;
        }
    }
    if (zero) {
        return "-";
    }
    std::string out;
    out.reserve(12);
    for (std::size_t i = 0; i < 12 / 2; ++i) {
        out.push_back(kHex[digest[i] >> 4]);
        out.push_back(kHex[digest[i] & 0x0F]);
    }
    return out;
}

/// 布尔的稳定呈现词（true/false——中立词面，不新增状态词表；§6.1 状态词
/// 来源纪律：呈现非判定，工程词表不经框架条目）。
std::string boolWord(bool v)
{
    return v ? "true" : "false";
}

/// 空章节内容底座（词表版本/缺项形态共用——状态由调用方覆写）。
SectionContent baseContent()
{
    SectionContent content;
    content.providerContractVersion = 1;   // 框架投影契约版本（演进随单元卡变更记录）
    content.renderHint = RenderHint::Table;
    return content;
}

/// 缺项条目工厂（itemId＋原因——ERR-01：缺什么写什么）。
MissingItemView missingItem(std::string itemId, std::string reason)
{
    MissingItemView item;
    item.itemId = std::move(itemId);
    item.reason = std::move(reason);
    return item;
}

}  // namespace

// =====================================================================
// 构造与分派
// =====================================================================

BuiltinFrameworkSections::BuiltinFrameworkSections(
    const project::IProjectQueryPort& queryPort,
    const IModelSummaryProvider* modelSummary) noexcept
    : m_queryPort(&queryPort), m_modelSummary(modelSummary)
{
}

SectionContent BuiltinFrameworkSections::project(std::string_view sectionId,
                                                 const FrameworkSectionInput& input) const
{
    // 分派表＝B 级框架词表四项（§5.1 行 1/2/7/8）。C 专属框架两项
    // （review-signoff/variant-diff）的内建投影归 RPT-T15——传入即调用方
    // 违约（阶段边界在分派面显性化，不静默产出空章节）。
    if (sectionId == kSectionProjectScheme) {
        return projectProjectScheme(input);
    }
    if (sectionId == kSectionInputSummary) {
        return projectInputSummary(input);
    }
    if (sectionId == kSectionDiagnostics) {
        return projectDiagnostics(input);
    }
    if (sectionId == kSectionExternalValidationBoundary) {
        return projectExternalValidationBoundary(input);
    }
    // 非框架 token（域章节/C 专属框架/词表外）＝调用方契约违约 fail-fast
    //（AGENTS.md 错误语义：调用方错误走异常，不走诊断收集）。
    throw ReportError(ReportErrorCode::Usage,
                      "BuiltinFrameworkSections: '" + std::string(sectionId)
                          + "' 不是 B 级框架章节 token（§5.1 行 1/2/7/8——内建投影分派表外）");
}

std::vector<ExternalResourceStateEntry> BuiltinFrameworkSections::externalResourceEntries(
    core::RevisionId revision) const
{
    std::vector<ExternalResourceStateEntry> out;
    // 提供方未装配＝空集（§4.2"空＝未获知"——不伪造"确认无外部资源"）。
    if (m_modelSummary == nullptr) {
        return out;
    }
    const std::optional<ModelSummary> summary = m_modelSummary->trySummary(revision);
    if (!summary.has_value()) {
        return out;
    }
    out.reserve(summary->resources.size());
    for (const ResourceSummary& resource : summary->resources) {
        ExternalResourceStateEntry entry;
        entry.resourceId = resource.resourceId;
        // 词面映射：ResourceStateKind（ModelSummary 投影词表）→
        // ExternalResourceState（§4.2 报告词表）——同名一一（recorded/
        // solidified），穷举 switch 无 default 防漏。
        switch (resource.state) {
        case ResourceStateKind::Recorded:
            entry.state = ExternalResourceState::Recorded;
            break;
        case ResourceStateKind::Solidified:
            entry.state = ExternalResourceState::Solidified;
            break;
        }
        // 来源列＝投影来源说明（呈现义务"来源"——CON-03 协作面；框架侧
        // 资源状态的唯一来源是 runtime 摘要投影）。
        entry.source = "runtime 摘要投影";
        out.push_back(std::move(entry));
    }
    return out;
}

// =====================================================================
// §5.1 行 1——project-scheme（项目与方案）
// =====================================================================

SectionContent BuiltinFrameworkSections::projectProjectScheme(
    const FrameworkSectionInput& input) const
{
    SectionContent content = baseContent();

    // 数据源：锚定修订的元数据版本（锚定纪律口径——文件头注；§5.1 字面
    // "currentMetadata"的修订锚定版，DTB §5.4 登记）。nullopt＝修订在闭包
    // 但元数据版本不可解析（清单违约的防御面）——缺项表达，不伪造元数据。
    const std::optional<project::ProjectMetadataView> metadata =
        m_queryPort->metadataAt(input.revision);
    if (!metadata.has_value()) {
        content.status = SectionStatus::NoFormalResult;
        content.missingItems.push_back(
            missingItem("project-metadata",
                        "项目元数据不可解析（修订在锚定闭包但元数据版本缺失）"));
        return content;
    }
    const project::ProjectMetadataRecord& record = metadata->record;

    // 条目 1"project"：显示名＋主分支（§5.1 行 1"显示名"——PM-11 标题栏
    // 数据源；主分支＝初始分支 brn- 规范文本）。
    {
        SectionEntryView entry;
        entry.entryKey = "project";
        entry.fields.push_back(textField("display-name", record.projectDisplayName));
        entry.fields.push_back(
            textField("primary-branch", record.primaryBranchId.toCanonical()));
        content.entries.push_back(std::move(entry));
    }

    // 条目 2"branches"：分支计数（无量纲）＋分支表文本（§5.1 行 1"分支表"
    // ——每行"id | 显示名 | tip"；行序＝元数据记录序【INV-M1 唯一约束的
    // 权威序】，投影不重排——确定性）。空表不可达（INV-M1 ≥1），防御分支
    // 以"—"承载。
    {
        SectionEntryView entry;
        entry.entryKey = "branches";
        entry.fields.push_back(countField("branch-count", record.branches.size()));
        std::string table;
        for (const project::BranchRecord& branch : record.branches) {
            if (!table.empty()) {
                table += "; ";
            }
            table += branch.branchId.toCanonical() + " | " + branch.label + " | "
                     + branch.tipRevisionId.toCanonical();
        }
        entry.fields.push_back(textField("branch-table", table.empty() ? "-" : table));
        content.entries.push_back(std::move(entry));
    }

    // 条目 3"scheme"：方案清单附加标签（§5.1 行 1"方案清单"——键序＝
    // std::map 字典序【元数据 canonical 同源序】，投影不重排）。空 map＝
    // 合法状态（可选字段冻结位）→"—"（非缺失——不伪造缺项）。
    {
        SectionEntryView entry;
        entry.entryKey = "scheme";
        std::string labels;
        for (const auto& [key, value] : record.schemeLabels) {
            if (!labels.empty()) {
                labels += "; ";
            }
            labels += key + "=" + value;
        }
        entry.fields.push_back(textField("scheme-labels", labels.empty() ? "-" : labels));
        content.entries.push_back(std::move(entry));
    }

    // 条目 4"revision"：修订摘要（§5.1 行 1"＋修订摘要"——锚定视图的
    // commandSummary 与展示序号；两者都是构建器步①已锚定的会话事实）。
    {
        SectionEntryView entry;
        entry.entryKey = "revision";
        entry.fields.push_back(countField("revision-seq", input.revisionSeq));
        entry.fields.push_back(textField("revision-summary", input.revisionSummary));
        content.entries.push_back(std::move(entry));
    }

    content.status = SectionStatus::Populated;
    return content;
}

// =====================================================================
// §5.1 行 2——input-summary（输入摘要）
// =====================================================================

SectionContent BuiltinFrameworkSections::projectInputSummary(
    const FrameworkSectionInput& input) const
{
    SectionContent content = baseContent();

    // 数据源缺位两支（诚实表达——ERR-01）：
    //   ①提供方未注入（L5 装配缺席——P-RPT-2 注入形态的装配面状态）；
    //   ②trySummary 返回 nullopt（修订不在权威闭包——构建器步①已拦截，
    //     本支为防御面）。
    if (m_modelSummary == nullptr) {
        content.status = SectionStatus::NoFormalResult;
        content.missingItems.push_back(missingItem(
            "model-summary",
            "模型摘要提供方未装配（IModelSummaryProvider 注入缺席——L5 装配面）"));
        return content;
    }
    const std::optional<ModelSummary> summary = m_modelSummary->trySummary(input.revision);
    if (!summary.has_value()) {
        content.status = SectionStatus::NoFormalResult;
        content.missingItems.push_back(
            missingItem("model-summary", "模型摘要不可解析（修订不在权威闭包）"));
        return content;
    }

    // 条目 1"snapshot-identity"：快照身份块（§9.7 snapshotIdentity 恒在案
    // ＋模型身份/名称映射身份/策略内容身份——快照已释放时后者可空，全零
    // 呈现"—"，配 provenance-note 条目——不伪造）。
    {
        SectionEntryView entry;
        entry.entryKey = "snapshot-identity";
        entry.fields.push_back(textField("snapshot", summary->snapshotIdentity.toCanonical()));
        entry.fields.push_back(textField("model-identity", cidOrDash(summary->modelIdentity)));
        entry.fields.push_back(
            textField("name-map-identity", cidOrDash(summary->nameMapIdentity)));
        entry.fields.push_back(
            textField("policy-identity", cidOrDash(summary->policyContentIdentity)));
        content.entries.push_back(std::move(entry));
    }

    // 条目 2"model-shape"：链型摘要（§9.7 轴数【无量纲计数】＋全旋转标记
    // ＋安装预设呈现 token——呈现字段，禁止参与任何判定【§9.7 非法调用
    // 行】；本投影只呈现不判定）。
    {
        SectionEntryView entry;
        entry.entryKey = "model-shape";
        entry.fields.push_back(countField("joint-count", summary->jointCount));
        entry.fields.push_back(textField("all-revolute", boolWord(summary->allRevolute)));
        entry.fields.push_back(
            textField("install-preset", summary->installPresetToken.empty()
                                            ? "-"
                                            : summary->installPresetToken));
        content.entries.push_back(std::move(entry));
    }

    // 条目 3"capabilities"：能力声明八布尔（§9.7 Capabilities——MDL-06/
    // DYN-06 能力正交语义的呈现面；false＝缺失【降级非阻断】，如实呈现
    // true/false，不判定不合并）。
    {
        SectionEntryView entry;
        entry.entryKey = "capabilities";
        entry.fields.push_back(textField("has-dynamic-work-cell",
                                         boolWord(summary->capabilities.hasDynamicWorkCell)));
        entry.fields.push_back(textField("has-full-mass-inertia",
                                         boolWord(summary->capabilities.hasFullMassInertia)));
        entry.fields.push_back(
            textField("has-joint-velocity-limits",
                      boolWord(summary->capabilities.hasJointVelocityLimits)));
        entry.fields.push_back(textField("has-collision-geometry",
                                         boolWord(summary->capabilities.hasCollisionGeometry)));
        entry.fields.push_back(textField("has-tools", boolWord(summary->capabilities.hasTools)));
        entry.fields.push_back(textField("has-scene", boolWord(summary->capabilities.hasScene)));
        entry.fields.push_back(
            textField("has-friction-model", boolWord(summary->capabilities.hasFrictionModel)));
        entry.fields.push_back(
            textField("has-coupling-matrix", boolWord(summary->capabilities.hasCouplingMatrix)));
        content.entries.push_back(std::move(entry));
    }

    // 条目 4~"resource-N"：资源清单（CON-03——逐条资源 id/状态词面/内容
    // 摘要短形；序＝runtime 提供序，投影不重排——同修订同投影前提）。
    for (std::size_t i = 0; i < summary->resources.size(); ++i) {
        const ResourceSummary& resource = summary->resources[i];
        SectionEntryView entry;
        entry.entryKey = "resource-" + std::to_string(i + 1);
        entry.fields.push_back(textField("resource-id", resource.resourceId));
        entry.fields.push_back(textField("state", toToken(resource.state)));
        entry.fields.push_back(textField("digest", digestShortOrDash(resource.digest)));
        content.entries.push_back(std::move(entry));
    }

    // 条目"configuration-N"：配置引用（§9.7 configurations——Analysis-
    // Configuration 引用【configKindToken＋内容身份】；种子等具体值在配置
    // canonical 内由引用承载，本投影不解包——N-5/N-9）。
    for (std::size_t i = 0; i < summary->configurations.size(); ++i) {
        const ConfigSummary& config = summary->configurations[i];
        SectionEntryView entry;
        entry.entryKey = "configuration-" + std::to_string(i + 1);
        entry.fields.push_back(textField("config-kind", config.configKindToken));
        entry.fields.push_back(
            textField("content-identity", cidOrDash(config.contentIdentity)));
        content.entries.push_back(std::move(entry));
    }

    // 条目"provenance-note"：呈现注记（非空时——快照已释放等场景的"不
    // 伪造"说明；§14.4 v0.15 增补的 schema 承载位）。
    if (!summary->provenanceNote.empty()) {
        SectionEntryView entry;
        entry.entryKey = "provenance-note";
        entry.fields.push_back(textField("note", summary->provenanceNote));
        content.entries.push_back(std::move(entry));
    }

    content.status = SectionStatus::Populated;
    return content;
}

// =====================================================================
// §5.1 行 7——diagnostics（诊断）
// =====================================================================

SectionContent BuiltinFrameworkSections::projectDiagnostics(
    const FrameworkSectionInput& input) const
{
    SectionContent content = baseContent();

    // 数据源：报告级诊断引用并集（§4.2 diagRefs——构建器 DiagRefMerger 的
    // 当前快照；借用指针为空＝构建器编排违约——fail-fast，不静默当空集）。
    if (input.diagRefs == nullptr) {
        throw ReportError(ReportErrorCode::Usage,
                          "BuiltinFrameworkSections: diagnostics 投影输入缺 diagRefs"
                          "（构建器编排违约）");
    }

    // 空集＝无诊断记录——显式不适用（§5.3 NotApplicable"条件不满足，不计
    // 缺失"；"无诊断"是正常状态而非数据缺失，不伪造缺项清单）。
    if (input.diagRefs->empty()) {
        content.status = SectionStatus::NotApplicable;
        content.notApplicableReason = "本报告无诊断记录（各域结果与构建过程均未产生可呈现诊断）";
        return content;
    }

    // 非空＝逐诊断条目（序＝并集序【构建器 DiagRefMerger 去重聚合序】，
    // 投影不重排；字段全部为词表 token/规范文本/计数——UX-02 允许域，
    // 不引入自由文本面，渲染侧脱敏双保险的第二道照常生效）。
    for (std::size_t i = 0; i < input.diagRefs->size(); ++i) {
        const DiagRefEntry& ref = (*input.diagRefs)[i];
        SectionEntryView entry;
        entry.entryKey = "diag-" + std::to_string(i + 1);
        entry.fields.push_back(textField("code", ref.code));
        entry.fields.push_back(
            textField("severity", std::string(diagnostics::severityToken(ref.severity))));
        entry.fields.push_back(
            textField("category", std::string(diagnostics::categoryToken(ref.category))));
        entry.fields.push_back(countField("occurrences", ref.occurrences));
        entry.fields.push_back(textField("source-section", ref.sourceSection));
        if (ref.sourceRun.has_value()) {
            entry.fields.push_back(textField("source-run", ref.sourceRun->toCanonical()));
        }
        content.entries.push_back(std::move(entry));
    }

    content.status = SectionStatus::Populated;
    return content;
}

// =====================================================================
// §5.1 行 8——external-validation-boundary（外部验证边界）
// =====================================================================

SectionContent BuiltinFrameworkSections::projectExternalValidationBoundary(
    const FrameworkSectionInput& input) const
{
    SectionContent content = baseContent();

    // 条目 1"reproduction"：复现块摘要（§4.2 reproduction 必填——构建器
    // 步③已保证在场；借用指针为空＝编排违约 fail-fast）。字段＝版本族
    // 逐项（evidence §4.1.2——productVersion/evidenceContractVersion 必填
    // ＋codec 族＋编译器/碰撞后端可选版本；可选版本缺席＝"—"）。
    if (input.reproduction == nullptr) {
        throw ReportError(ReportErrorCode::Usage,
                          "BuiltinFrameworkSections: external-validation-boundary 投影输入缺"
                          " reproduction（构建器编排违约——复现块为报告必填事实）");
    }
    {
        SectionEntryView entry;
        entry.entryKey = "reproduction";
        entry.fields.push_back(textField("product-version", input.reproduction->productVersion));
        entry.fields.push_back(
            textField("evidence-contract-version", input.reproduction->evidenceContractVersion));
        // codec 版本族拼接（保序承载——evidence 不解释族序，本投影同序拼接）。
        std::string codecVersions;
        for (const std::string& version : input.reproduction->codecVersions) {
            if (!codecVersions.empty()) {
                codecVersions += "; ";
            }
            codecVersions += version;
        }
        entry.fields.push_back(
            textField("codec-versions", codecVersions.empty() ? "-" : codecVersions));
        entry.fields.push_back(textField(
            "compiler-contract-version",
            input.reproduction->compilerContractVersion.value_or("-")));
        entry.fields.push_back(textField(
            "collision-backend-version",
            input.reproduction->collisionBackendVersion.value_or("-")));
        content.entries.push_back(std::move(entry));
    }

    // 条目"resource-N"：外部资源固化状态（CON-03——数据源＝ModelSummary
    // .resources；Recorded 态即"外部验证未完成"限定语的数据源【§6.4 行 3】，
    // 报告级限定语由渲染器对 externalResourceSummary 的既有承接激活——
    // v0.9 登记"数据面随框架章节数源任务收口"的兑现面）。提供方未装配/
    // 摘要不可解析＝无资源条目（复现块条目独立在场——章节不因此缺项：
    // 资源清单缺位已在 input-summary 缺项表达，不重复不伪造）。
    if (m_modelSummary != nullptr) {
        const std::optional<ModelSummary> summary = m_modelSummary->trySummary(input.revision);
        if (summary.has_value()) {
            for (std::size_t i = 0; i < summary->resources.size(); ++i) {
                const ResourceSummary& resource = summary->resources[i];
                SectionEntryView entry;
                entry.entryKey = "resource-" + std::to_string(i + 1);
                entry.fields.push_back(textField("resource-id", resource.resourceId));
                entry.fields.push_back(textField("state", toToken(resource.state)));
                entry.fields.push_back(textField("digest", digestShortOrDash(resource.digest)));
                content.entries.push_back(std::move(entry));
            }
        }
    }

    content.status = SectionStatus::Populated;
    return content;
}

}  // namespace sdurws::ird::reporting