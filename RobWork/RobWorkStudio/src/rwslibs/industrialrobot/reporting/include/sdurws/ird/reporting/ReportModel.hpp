/**
 * @file   ReportModel.hpp
 * @brief  ReviewReport 数据模型——报告级身份/引用类型全族/章节冻结视图/
 *         评审元数据＋不可变报告对象与字段校验原语（RPT-T03 产物）。
 *
 * 设计依据：
 *   - units/reporting.md §4 全节（§4.1 身份纪律、§4.2 字段表、§4.3 引用类型
 *     字段表、§4.5 ReviewMetadata、§4.6 合法组合矩阵、§4.7 对象关系图）、
 *     §5.3（SectionStatus 四态）、§6.3（CurrentnessSnapshot）、§6.4
 *     （QualifierToken 词表）、§9.2（SectionEntry 冻结视图形状）、§3.1
 *     组成表（ReportModel.hpp 载荷清单）
 *   - 需求 RPT-01（报告对象只读、只引用明确 ID）、NFR-COR-04（结论定位到
 *     快照与证据——引用结构承载）、NFR-COR-02（确定性）、CON-02（三轴
 *     正交——状态词只消费权威词表）、RPT-05（限定语数据驱动）
 *   - 任务契约 tasks/foundation/RPT-T03.json acceptance 1~6
 *
 * 背景说明（本头在 reporting 中的位置）：
 *   ReviewReport 是报告生成/渲染/归档/复算全链的**单一数据源**（AT-22）：
 *   全部格式渲染、幂等导出、往返复算都从本对象的只读投影出发。报告对象
 *   是纯值快照——构建成功即冻结（§4.2"生成状态"属于构建会话、不属于本
 *   对象；"归档状态"属于工件清单），不存在"半份报告"。身份四概念
 *   （ReportId/reportVersion/dataIdentity/contentIdentity）的类型承载：
 *   ReportId 在 Identity.hpp（P-RPT-3 自建 tag 类型），两个内容身份为
 *   core::ContentIdentity——由 ReportCodec（src/ 私有实现，§3.1）经
 *   ReviewReport::make() 计算注入，调用方**不可申报**（§4.2）。
 *
 * 消费纪律（P-RPT-9 基线＝各卡 v0.1 Draft；acceptance 5/6）：
 *   - core 词表与身份类型全族消费既有权威定义，不本地重定义（O-24）：
 *     EvaluationMode/TaskOutcome/EngineeringStatus（core/Evaluation.hpp）、
 *     ObjectId/ProjectId/BranchId/RevisionId/RunId/TaskIdentity（core/
 *     Identity.hpp）、ContentIdentity/Digest256（core/Digest.hpp）、
 *     SourcedValue/ValueProvenance（core/Provenance.hpp）、UnitToken/
 *     QuantityKind（core/Units.hpp）、DiagnosticRecord/ComparativeFields/
 *     DiagCode（core/DiagData.hpp）。
 *   - evidence 词表与包络类型族消费既有权威定义，不本地重定义不复制
 *     （O-13——EvidenceItemStatus 五值各有其位、不合并；acceptance 5）：
 *     EvaluationKey（Evaluator.hpp）、ProducerInfo（Envelope.hpp）、
 *     ReproductionBlock（Snapshot.hpp）、EvidenceItemClass/EvidenceItemStatus
 *     （Evidence.hpp）、CurrentnessStatus/InvalidationReason（Currentness.hpp）、
 *     BaselineConsistencyResult（Verdict.hpp）。
 *   - 上游各卡冻结出 diff 后按影响面增量同步留痕（DTB §5.4），不私改上游。
 *
 * SectionStatus/RenderHint/§4.3.4 冻结视图类型的落位说明（DTB §5.4 偏差
 * 登记，单元卡 v0.6 变更行）：§3.1 组成表把 SectionStatus 列于 Sections.hpp
 * （RPT-T04）、把 SectionEntry/ResultBinding/EvidenceBinding/JumpTarget/
 * MissingItemView 列于 SectionProvider.hpp（RPT-T04）；但 §4.3.4
 * ReviewReportSection 以值字段引用这些类型——RPT-T03 先于 RPT-T04 落位
 * （§11 依赖列），故本头承载：SectionStatus/RenderHint 两枚举＋条目
 * 冻结视图族（SectionEntryView/FieldValue/ResultBinding/EvidenceBinding/
 * JumpTarget/MissingItemView/EligibilityNote）。RPT-T04 的 Sections.hpp/
 * SectionProvider.hpp 消费本头类型、不重复定义；类型语义与 §5.3/§9.2
 * 原文逐字一致，本偏差不改任何契约语义。
 *
 * 线程安全：全部类型为不可变值对象（构造后无 setter）——并发只读安全；
 * ReviewReport::make() 为可重入纯计算（无共享状态）。
 * 确定性：身份计算归 ReportCodec（§4.4 纯函数——同输入同字节，NFR-COR-02）。
 */

#ifndef SDURWS_IRD_REPORTING_REPORTMODEL_HPP
#define SDURWS_IRD_REPORTING_REPORTMODEL_HPP

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>       // core::DiagnosticRecord/ComparativeFields/DiagCode
#include <sdurws/ird/core/Evaluation.hpp>     // core::EvaluationMode/TaskOutcome/EngineeringStatus（O-24 消费）
#include <sdurws/ird/core/Identity.hpp>       // core::ObjectId/ProjectId/BranchId/RevisionId/RunId/TaskIdentity
#include <sdurws/ird/core/Provenance.hpp>     // core::SourcedValue（FieldValue 四态承载）
#include <sdurws/ird/core/Units.hpp>          // core::UnitToken/QuantityKind（单位呈现唯一入口）

#include <sdurws/ird/diagnostics/DiagCodes.hpp>  // diagnostics::DiagnosticSeverity/DiagnosticCategory

#include <sdurws/ird/evidence/Currentness.hpp>  // evidence::CurrentnessStatus/InvalidationReason
#include <sdurws/ird/evidence/Evidence.hpp>     // evidence::EvidenceItemClass/EvidenceItemStatus（O-13 消费）
#include <sdurws/ird/evidence/Evaluator.hpp>    // evidence::EvaluationKey
#include <sdurws/ird/evidence/Envelope.hpp>     // evidence::ProducerInfo
#include <sdurws/ird/evidence/Snapshot.hpp>     // evidence::ReproductionBlock
#include <sdurws/ird/evidence/Verdict.hpp>      // evidence::BaselineConsistencyResult

#include <sdurws/ird/reporting/Errors.hpp>    // ReportError/ReportErrorCode（make/解析边界）
#include <sdurws/ird/reporting/Identity.hpp>  // ReportId/ReportLevel（RPT-T02 产物——P-RPT-3 消费）

namespace sdurws::ird::reporting {

// =====================================================================
// 章节契约常量与枚举（§4.2/§5.3/§9.2）
// =====================================================================

/// 章节契约版本（§4.2 sectionModelVersion 字段原文——入 contentIdentity；
/// 词表升级＝sectionModelVersion 升版并走单元卡变更记录，§5.1 词表规则）。
inline constexpr std::string_view kSectionModelVersion = "ird-report-section-model/1";

/**
 * @brief 章节状态四态（§5.3 词表——token 冻结）。
 *
 * 五词各有其位的呈现纪律（§5.3"区分规则"）：Populated＝有正式内容；
 * NoFormalResult＝缺正式结果（默认不选＋缺项清单——§16 验收要点）；
 * DataInsufficient＝有结果但证据不足（缺失项**全量**清单）；NotApplicable＝
 * 显式不适用（不计缺失——C2/ERR-01）。reporting 不新增状态、不合并词位
 * （§6.1 状态词来源纪律）。
 */
enum class SectionStatus : std::uint8_t {
    Populated,        ///< populated——章节有正式内容（≥1 条目）
    NoFormalResult,   ///< no-formal-result——缺正式结果（默认不选＋缺项）
    DataInsufficient, ///< data-insufficient——有结果但证据不足（全量缺失清单）
    NotApplicable,    ///< not-applicable——显式不适用（"—"＋原因，不伪造）
};

/// 取章节状态稳定 token（§5.3 token 列原文；静态存储期——NFR-COR-02）。
std::string_view token(SectionStatus status) noexcept;

/// try 轨解析章节状态 token（恰接受四规范字面，其余 nullopt——§1.4 约定）。
std::optional<SectionStatus> trySectionStatusFromToken(std::string_view token) noexcept;

/// 抛出轨迹解析（与 try 同判据；解码边界 fail-fast）。
/// @throws ReportError(ReportErrorCode::DataInvalid) token 非规范字面
SectionStatus sectionStatusFromToken(std::string_view token);

/**
 * @brief 章节渲染提示（§9.2 SectionContent.renderHint——Table/CurveRef/
 *        DiffTable/MetadataBlock 四值）。
 *
 * 渲染器的版式选择输入（呈现方式，非内容事实）——入 contentIdentity
 * （§4.4"全部章节完整内容"）：同一章节以不同版式呈现属于不同报告内容。
 */
enum class RenderHint : std::uint8_t {
    Table,          ///< table——表格
    CurveRef,       ///< curve-ref——曲线引用（曲线以引用呈现不复制——RP-CONS-4）
    DiffTable,      ///< diff-table——差异表（RPT-04 变体章节）
    MetadataBlock,  ///< metadata-block——元数据块（评审/签署等框架章节）
};

/// 取渲染提示稳定 token（§9.2 注释列 kebab 形；静态存储期）。
std::string_view token(RenderHint hint) noexcept;

/**
 * @brief 限定语 token（§6.4 词表——**数据驱动**的呈现标记，非自由文本）。
 *
 * RPT-05 措辞冻结的实施承载：限定语不得弱化或省略——渲染器把数据源
 * （SourcedValue 态/评估模式/当前性/覆盖率降级标记等）**结构性**映射到本
 * 词表（HTML 附加标记/CSV qualifier 列/JSON qualifier 数组字段），一致性
 * 校验把限定语作为字段的一部分逐格式比对（§6.4/§8.5）。本枚举只冻结
 * 词面（§6.4 表 token 列原文），映射规则归渲染任务 RPT-T06/T08。
 */
enum class QualifierToken : std::uint8_t {
    Estimated,                     ///< estimated（估算值——ValueProvenance GeometricEstimate）
    DataInsufficient,              ///< data-insufficient（证据不足/缺失）
    ExternalValidationIncomplete,  ///< external-validation-incomplete（外部资源未固化）
    DowngradedReferenceValue,      ///< downgraded-reference-value（覆盖率参考值降级——P-EV-5）
    ScreeningOnly,                 ///< screening-only（Quick 模式——不得单独支撑正式通过）
    HistoricalSuperseded,          ///< historical-superseded（当前性 Superseded）
    NotApplicable,                 ///< not-applicable（"—"＋原因，不伪造数值）
    Interrupted,                   ///< interrupted（状态事实呈现，非结论）
    Canceled,                      ///< canceled（同上）
    Failed,                        ///< failed（同上）
};

/// 取限定语稳定 token（§6.4 token 列原文；静态存储期）。
std::string_view token(QualifierToken qualifier) noexcept;

// =====================================================================
// 条目冻结视图族（§9.2 SectionEntry 的报告侧冻结形态——§4.3.4）
// =====================================================================

/**
 * @brief 章节字段值（§9.2 SectionEntry.fields "{key, SourcedValue<double>|
 *        文本, UnitToken?}" 的冻结承载）。
 *
 * 数值/文本两侧互斥：text 在场时 quantity 不得为 Provided 态（字段校验
 * 原语执行——数值语义走 SourcedValue 四态、纯文本走 text）。单位是显示
 * 投影（KIN-12：SI 真值不变）——可选（无量纲字段缺席）。
 *
 * 值语义；线程安全：纯值。
 */
struct FieldValue {
    /// 字段键（字段矩阵列键——AT-22 逐字段一致性的对齐键；非空）。
    std::string key;
    /// 数值侧四态值（SI 真值——core §4.4 唯一换算入口的输入面；
    /// Provided 态必须有限——NaN/±Inf 在字段校验与编码入口双重拒绝）。
    core::SourcedValue<double> quantity;
    /// 文本侧（可选——纯文本字段；与 Provided 数值互斥）。
    std::optional<std::string> text;
    /// 显示单位（可选——已注册 UnitToken；数值字段的呈现单位）。
    std::optional<core::UnitToken> unit;

    /// 全字段精确相等（UnitToken 表内等值；SourcedValue 四态等值——core 契约）。
    bool operator==(const FieldValue& o) const
    {
        return key == o.key && quantity == o.quantity && text == o.text && unit == o.unit;
    }
    bool operator!=(const FieldValue& o) const { return !(*this == o); }
};

/**
 * @brief 结果绑定（§9.2 ResultBinding——{runId, fieldPath}）。
 *
 * 结论条目的追溯锚（RP-TRACE-1）：每个结论条目必带结果绑定——runId 解析
 * 到 report.resultRefs 的 ResultRefSnapshot，fieldPath 定位字段来源
 * （NFR-COR-04 结论定位到快照与证据的报告侧第一跳）。
 */
struct ResultBinding {
    /// 来源运行（必须存在于 report.resultRefs——字段校验原语执行）。
    core::RunId runId;
    /// 字段路径（envelope 载荷内的定位路径；非空）。
    std::string fieldPath;

    bool operator==(const ResultBinding& o) const
    {
        return runId == o.runId && fieldPath == o.fieldPath;
    }
    bool operator!=(const ResultBinding& o) const { return !(*this == o); }
};

/**
 * @brief 证据绑定（§9.2 EvidenceBinding——{itemId, status, digest?,
 *        caseScope}）。
 *
 * 章节条目对证据项的引用（报告级汇总去重后进入 EvidenceRefEntry，
 * §4.2 evidenceRefs 行）。status/digest 的语义与 evidence §6.2 一致——
 * reporting 只呈现不重判（§2.1 C-2）。
 */
struct EvidenceBinding {
    /// 证据项 id（"<域>.<项>" 词形——evidence isValidProfileItemId 同闸门）。
    std::string itemId;
    /// 证据状态（P-EV-8 五值词表——O-13：消费不重定义、五值不合并）。
    evidence::EvidenceItemStatus status = evidence::EvidenceItemStatus::Missing;
    /// 产物摘要（Satisfied 必填非零——绑定校验锚；字段校验原语执行）。
    std::optional<core::Digest256> digest;
    /// 工况范围（⊆ 所属结果 caseScope——§4.7 关系约束，字段校验原语执行）。
    std::vector<core::ObjectId> caseScope;

    bool operator==(const EvidenceBinding& o) const
    {
        return itemId == o.itemId && status == o.status && digest == o.digest
               && caseScope == o.caseScope;
    }
    bool operator!=(const EvidenceBinding& o) const { return !(*this == o); }
};

/**
 * @brief 预览跳转目标（§9.2 JumpTarget——{objectId?, caseId?, runId?}）。
 *
 * ui 预览宿主跳转锚（§9.8；caseId 语义＝evidence::CaseId——其 using 别名
 * 即 core::ObjectId，本处直接以 core::ObjectId 承载，evidence 消费口径
 * 不变）。三目标可任意组合缺席。
 */
struct JumpTarget {
    /// 对象跳转（项目对象——⑥名称端口反解归 ui）。
    std::optional<core::ObjectId> objectId;
    /// 工况跳转（evidence::CaseId 同物——core::ObjectId）。
    std::optional<core::ObjectId> caseId;
    /// 运行跳转。
    std::optional<core::RunId> runId;

    bool operator==(const JumpTarget& o) const
    {
        return objectId == o.objectId && caseId == o.caseId && runId == o.runId;
    }
    bool operator!=(const JumpTarget& o) const { return !(*this == o); }
};

/**
 * @brief 章节条目（§4.3.4 entries——"§9.2 SectionEntry 的冻结视图：字段/
 *        结果绑定/证据绑定/工况范围/跳转目标"）。
 *
 * 提供方（§9.2 IReportSectionProvider）产出 SectionEntry，构建器校验后
 * 冻结为本视图存入报告——字段集与 §9.2 原文一致，报告内不可变。
 */
struct SectionEntryView {
    /// 章节内稳定键（§9.2 语法 [a-z0-9.-]{2,63}；节内唯一——校验原语执行）。
    std::string entryKey;
    /// 字段集（§9.2 fields）。
    std::vector<FieldValue> fields;
    /// 结果绑定（结论条目必填——追溯锚）。
    ResultBinding result;
    /// 证据绑定集。
    std::vector<EvidenceBinding> evidence;
    /// 工况范围（⊆ 所属结果 caseScope——§4.7）。
    std::vector<core::ObjectId> caseScope;
    /// 预览跳转目标。
    JumpTarget jump;

    bool operator==(const SectionEntryView& o) const
    {
        return entryKey == o.entryKey && fields == o.fields && result == o.result
               && evidence == o.evidence && caseScope == o.caseScope && jump == o.jump;
    }
    bool operator!=(const SectionEntryView& o) const { return !(*this == o); }
};

/**
 * @brief 缺项视图（§4.3.4 missingItems——"itemId＋原因"）。
 *
 * 缺项**全量**清单的条目（不因首个缺失短路——§8.1 表 2④ 呈现口径）；
 * NoFormalResult/DataInsufficient 章节必填（字段校验原语执行）。
 */
struct MissingItemView {
    /// 缺失项 id（证据项/必需项——Profile itemId 同词形）。
    std::string itemId;
    /// 缺失原因（非空——ERR-01 不伪造）。
    std::string reason;

    bool operator==(const MissingItemView& o) const
    {
        return itemId == o.itemId && reason == o.reason;
    }
    bool operator!=(const MissingItemView& o) const { return !(*this == o); }
};

/**
 * @brief 章节级资格说明（§4.3.4 eligibilityNote——"该章节结论可否渲染
 *        '正式通过'/'评审记录'——由所引结果资格聚合"）。
 *
 * 两类声明独立（RPT-05/RV-07）——两个布尔各自成立、互不推导。聚合算法
 * 归构建器（RPT-T05 消费 evidence 资格纯检查）；本类型只承载聚合结果。
 * note 为人读聚合说明（渲染侧呈现；可为空——无附加说明）。
 */
struct EligibilityNote {
    /// 本章节结论可渲染"正式通过"（全部所引结果 formalPass 资格成立）。
    bool formalPassAllowed = false;
    /// 本章节结论可渲染"正式评审记录"（reviewRecord 资格成立）。
    bool reviewRecordAllowed = false;
    /// 聚合说明（可选载体；空串＝无附加说明）。
    std::string note;

    bool operator==(const EligibilityNote& o) const
    {
        return formalPassAllowed == o.formalPassAllowed
               && reviewRecordAllowed == o.reviewRecordAllowed && note == o.note;
    }
    bool operator!=(const EligibilityNote& o) const { return !(*this == o); }
};

// =====================================================================
// 结果引用快照与当前性（§4.3.1/§6.3）
// =====================================================================

/**
 * @brief 两类资格的检查结果快照（§4.3.1 eligibility——{formalPass: bool,
 *        reviewRecord: bool}）。
 *
 * 由 evidence 纯检查计算（FormalPassEligibility/ReviewRecordEligibility），
 * reporting 只记录与消费（§6.2——不自算资格、不缓存跨报告复用）。
 */
struct EligibilitySnapshot {
    /// FormalPassEligibility 五条件检查结果（EVI-01/§8.1）。
    bool formalPass = false;
    /// ReviewRecordEligibility 检查结果（RPT-05 两类声明之一）。
    bool reviewRecord = false;

    bool operator==(const EligibilitySnapshot& o) const noexcept
    {
        return formalPass == o.formalPass && reviewRecord == o.reviewRecord;
    }
    bool operator!=(const EligibilitySnapshot& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 诊断引用（§4.3.3——报告诊断章节与各章节诊断的引用条目）。
 *
 * 字段来自 diagnostics 的 DiagProjection/exportSafeSummary 值拷贝（双保险
 * 脱敏的第一道在 diagnostics；渲染输出前再次脱敏——§4.3.3"诊断脱敏双
 * 保险"）。reportable=false 的码不进报告（diagnostics §4.5——消费侧
 * 过滤归构建器/诊断投影源，本类型不重复判定）。
 */
struct DiagRefEntry {
    /// 稳定诊断码（core::DiagCode；码值权威＝diagnostics StableCodeRegistry）。
    core::DiagCode code;
    /// 严重级别（码表元数据——diagnostics 收编登记值）。
    diagnostics::DiagnosticSeverity severity{};
    /// 诊断类别（码表元数据）。
    diagnostics::DiagnosticCategory category{};
    /// 对象定位（ERR-01；可空）。
    std::optional<core::ObjectId> subject;
    /// 局部名（可空——缺失＝nullopt，不伪造）。
    std::optional<std::string> localName;
    /// 运行时名（⑥名称端口取得；可空）。
    std::optional<std::string> runtimeName;
    /// 比较型三要素（比较型诊断必填——UX-03；值拷贝自 DiagnosticRecord）。
    std::optional<core::ComparativeFields> comparison;
    /// 去重计数（diagnostics §6.4——聚合不吞缺失项）。
    std::size_t occurrences = 0;
    /// 来源章节（SectionId——引用关系可反向导航；非空）。
    std::string sourceSection;
    /// 来源运行（envelope 内诊断携带；可空）。
    std::optional<core::RunId> sourceRun;

    bool operator==(const DiagRefEntry& o) const;
    bool operator!=(const DiagRefEntry& o) const { return !(*this == o); }
};

/// 当前性判定所相对的上下文（§6.3 evaluatedAgainst——{headRevision,
/// contextSummary}）。
struct CurrentnessContext {
    /// 当时 HEAD 修订（非报告修订本身——两者一致的常见情形如实记录）。
    core::RevisionId headRevision;
    /// 上下文摘要（人读；渲染呈现用）。
    std::string contextSummary;

    bool operator==(const CurrentnessContext& o) const
    {
        return headRevision == o.headRevision && contextSummary == o.contextSummary;
    }
    bool operator!=(const CurrentnessContext& o) const { return !(*this == o); }
};

/**
 * @brief 当前性快照（§6.3——报告生成时刻冻结，之后不改写）。
 *
 * presence 纪律（§6.3 字段表，字段校验原语逐条执行）：
 *   - status 仅两持久态（Current/Superseded——evidence P-EV-4）；nullopt
 *     ＝不可判定**计算形态**（非第三持久态）；
 *   - status==nullopt ⇒ unevaluableNote 必填（"当前性无法判定"以诊断
 *     表达——**不得默认 Current**，任务约束§五.4/EV-CUR-2）；
 *   - reasons 仅在 status==Superseded 时非空（Current 与不可判定恒空）。
 * reasons 直接消费 evidence::InvalidationReason（呈现投影——单一定义，
 * 不复制 evidence 差异结构）。
 *
 * 值语义；不可变（报告冻结纪律）。
 */
struct CurrentnessSnapshot {
    /// 判定状态（nullopt＝不可判定——unevaluableNote 必填）。
    std::optional<evidence::CurrentnessStatus> status;
    /// 判定所相对的上下文。
    CurrentnessContext evaluatedAgainst;
    /// 计算时刻（UTC；快照属性——生成时冻结）。
    std::chrono::system_clock::time_point computedAtUtc{};
    /// 逐条目失效原因（Superseded 时非空；其余恒空）。
    std::vector<evidence::InvalidationReason> reasons;
    /// 不可判定诊断（status==nullopt 时必填——RPT-CURRENTNESS-UNEVALUABLE
    /// 呈现的数据源）。
    std::optional<DiagRefEntry> unevaluableNote;

    bool operator==(const CurrentnessSnapshot& o) const;
    bool operator!=(const CurrentnessSnapshot& o) const { return !(*this == o); }
};

/**
 * @brief 证据引用（§4.3.2——逐章节证据绑定的去重并集条目，含来源章节）。
 *
 * presence 纪律（§4.3.2 约束列，字段校验原语逐条执行——evidence §6.2
 * 口径的呈现侧）：
 *   - status==Satisfied ⇒ artifactDigest 必填非零（绑定校验通过方为
 *     Satisfied）；
 *   - status==NotApplicable ⇒ notApplicableReason 必填（C2 口径——"因
 *     不可行而不适用"等）；
 *   - status==Invalid ⇒ invalidReasonCode 必填（附诊断码引用——ERR-01）；
 *   - Missing/Unverified 无附加必填（无产物/未验证——没有可填的凭据）。
 */
struct EvidenceRefEntry {
    /// 证据项 id（"<域>.<项>" 词形——非空）。
    std::string itemId;
    /// 证据项类别（Common/Required/Suggested——§8.1 表 4 语义）。
    evidence::EvidenceItemClass itemClass = evidence::EvidenceItemClass::Required;
    /// 五值状态（P-EV-8 实现承载词表——O-13：消费不新增不合并）。
    evidence::EvidenceItemStatus status = evidence::EvidenceItemStatus::Missing;
    /// 产物摘要（Satisfied 必填非零）。
    std::optional<core::Digest256> artifactDigest;
    /// 工况范围（可选——多工况覆盖显示，§6.5）。
    std::optional<std::vector<core::ObjectId>> caseScope;
    /// 对象范围（可选——证据针对的对象）。
    std::optional<core::ObjectId> subject;
    /// 不适用原因（NotApplicable 必填非空）。
    std::optional<std::string> notApplicableReason;
    /// 无效原因诊断码引用（Invalid 必填非空）。
    std::optional<std::string> invalidReasonCode;
    /// 来源章节（SectionId——非空；可反向导航）。
    std::string sourceSection;

    bool operator==(const EvidenceRefEntry& o) const;
    bool operator!=(const EvidenceRefEntry& o) const { return !(*this == o); }
};

/**
 * @brief 结果引用快照（§4.3.1——报告绑定的明确结果的全字段引用）。
 *
 * resultRefs 的条目类型：全部为已 finalize 运行（listRuns 口径——构建器
 * 核对）；Preview 结果拒绝进入报告（§4.6——SourceMissing，字段校验原语
 * 执行）。三轴字段（outcome/engineeringStatus/currentness）分列——CON-02
 * 正交（不混排、不互相推导）。producer 承载 §4.3.1"producedIn /
 * productVersion"两列（evidence::ProducerInfo 摘要——直接消费其值）。
 *
 * 值语义；不可变（报告冻结纪律）。
 */
struct ResultRefSnapshot {
    /// 明确运行身份（非空保留值）。
    core::RunId runId;
    /// 任务五元组（TASK-03 呈现——evidence envelope.task 值拷贝）。
    core::TaskIdentity task;
    /// 评估键（evidence::EvaluationKey——非空；词形权威在 evidence）。
    evidence::EvaluationKey evaluationKey;
    /// 评估器契约版本（追溯——无量纲）。
    std::uint32_t evaluatorContractVersion = 0;
    /// 评估模式（Preview 拒绝入报告；Quick 携"筛选级"限定语——§8.1 表 1）。
    core::EvaluationMode mode = core::EvaluationMode::Verified;
    /// 结果完整性（core 词表——三轴之执行轴）。
    core::TaskOutcome outcome = core::TaskOutcome::Completed;
    /// 工程判定（core 词表——三轴之判定轴；当前性在 currentness，不混排）。
    core::EngineeringStatus engineeringStatus = core::EngineeringStatus::Feasible;
    /// envelope→snapshot 追溯链（NFR-COR-04 报告侧锚点——三者非零）。
    core::ContentIdentity snapshotId;
    /// 来源切片身份（非零——缓存键）。
    core::ContentIdentity sliceId;
    /// 输入基准身份（非零——比较基准）。
    core::ContentIdentity inputBaselineId;
    /// 覆盖工况集（⊆ 快照 caseSet——构建校验）。
    std::vector<core::ObjectId> caseScope;
    /// 资格检查结果快照（evidence 纯检查计算——reporting 只记录）。
    EligibilitySnapshot eligibility;
    /// 生成时刻当前性（冻结）。
    CurrentnessSnapshot currentness;
    /// 产生侧信息（§4.3.1 producedIn/productVersion 两列——evidence 值拷贝）。
    evidence::ProducerInfo producer;

    bool operator==(const ResultRefSnapshot& o) const;
    bool operator!=(const ResultRefSnapshot& o) const { return !(*this == o); }
};

// =====================================================================
// 报告级汇总与外部资源（§4.2 汇总字段族）
// =====================================================================

/// 外部资源状态二值词表（CON-03 呈现——Recorded（未固化）/Solidified）。
enum class ExternalResourceState : std::uint8_t {
    Recorded,    ///< recorded——已记录未固化（"外部验证未完成"限定语数据源）
    Solidified,  ///< solidified——已固化
};

/**
 * @brief 外部资源状态条目（§4.2 externalResourceSummary——"resourceId＋
 *        Recorded/Solidified＋来源"）。
 */
struct ExternalResourceStateEntry {
    /// 资源 id（非空——CON-03 协作面）。
    std::string resourceId;
    /// 固化状态。
    ExternalResourceState state = ExternalResourceState::Recorded;
    /// 来源（非空——呈现"来源"列）。
    std::string source;

    bool operator==(const ExternalResourceStateEntry& o) const
    {
        return resourceId == o.resourceId && state == o.state && source == o.source;
    }
    bool operator!=(const ExternalResourceStateEntry& o) const { return !(*this == o); }
};

/**
 * @brief 必验工况覆盖摘要（§4.2 coverageSummary——evidence 覆盖矩阵的
 *        呈现投影：五态计数＋必验总数）。
 *
 * 五态词表＝evidence CaseCoverageMatrix 的呈现投影（EVI-02）；计数语义
 * 归 evidence，本结构只承载呈现值（不重算——§2.1 C-2）。
 */
struct CaseCoverageSummary {
    std::uint64_t totalRequired = 0;   ///< 必验工况总数
    std::uint64_t executed = 0;        ///< Executed——已执行
    std::uint64_t notExecuted = 0;     ///< NotExecuted——未执行（漏验清单数据源）
    std::uint64_t invalid = 0;         ///< Invalid——执行无效
    std::uint64_t notApplicable = 0;   ///< NotApplicable——不适用（不计缺失）
    std::uint64_t failed = 0;          ///< Failed——执行失败

    bool operator==(const CaseCoverageSummary& o) const noexcept
    {
        return totalRequired == o.totalRequired && executed == o.executed
               && notExecuted == o.notExecuted && invalid == o.invalid
               && notApplicable == o.notApplicable && failed == o.failed;
    }
    bool operator!=(const CaseCoverageSummary& o) const noexcept { return !(*this == o); }
};

/// 逐结果当前性快照条目（§4.7 关系图"per-result 快照"的承载）。
struct ResultCurrentnessEntry {
    /// 结果运行身份。
    core::RunId runId;
    /// 该结果生成时刻的当前性快照。
    CurrentnessSnapshot currentness;

    bool operator==(const ResultCurrentnessEntry& o) const
    {
        return runId == o.runId && currentness == o.currentness;
    }
    bool operator!=(const ResultCurrentnessEntry& o) const { return !(*this == o); }
};

/**
 * @brief 报告级当前性快照汇总（§4.2 currentnessSummary——"报告生成时刻
 *        的当前性快照汇总"；§9.2 SectionRequest 载荷同名类型的报告侧
 *        对偶）。
 */
struct ReportCurrentnessSummary {
    /// 逐结果当前性快照（与 resultRefs 一一对应由构建器保证）。
    std::vector<ResultCurrentnessEntry> perResult;

    bool operator==(const ReportCurrentnessSummary& o) const
    {
        return perResult == o.perResult;
    }
    bool operator!=(const ReportCurrentnessSummary& o) const { return !(*this == o); }
};

/**
 * @brief 报告数值单位选择（§4.2 unitPreference——冻结入身份，KIN-12
 *        显示单位纯投影的报告侧冻结点，D-10）。
 *
 * displayUnits 按量纲声明显示单位；缺席量纲＝SI 显示。std::map 承载——
 * 迭代序＝QuantityKind 枚举序（确定性，NFR-COR-02：编码/渲染同序）。
 * 同报告重渲染字节一致的确定性前提（§4.2 字段表行）。
 */
struct UnitPreference {
    /// 量纲→显示单位（值必须为已注册 UnitToken——字段校验原语执行）。
    std::map<core::QuantityKind, core::UnitToken> displayUnits;

    bool operator==(const UnitPreference& o) const { return displayUnits == o.displayUnits; }
    bool operator!=(const UnitPreference& o) const { return !(*this == o); }
};

// =====================================================================
// 评审/签署元数据（§4.5——RPT-01-C/RPT-04）
// =====================================================================

/**
 * @brief 评审意见条目（§4.5 comments——{author, atUtc, text≤4KiB,
 *        sectionId?}；逐条不可变，追加产生新报告版本）。
 */
struct ReviewComment {
    /// 意见作者（principal——ui 采集，报告内受控字段不写日志）。
    std::string author;
    /// 意见时刻（UTC）。
    std::chrono::system_clock::time_point atUtc{};
    /// 意见正文（≤4KiB——字段校验原语执行）。
    std::string text;
    /// 关联章节（可选——逐章意见）。
    std::optional<std::string> sectionId;

    bool operator==(const ReviewComment& o) const
    {
        return author == o.author && atUtc == o.atUtc && text == o.text
               && sectionId == o.sectionId;
    }
    bool operator!=(const ReviewComment& o) const { return !(*this == o); }
};

/**
 * @brief 签署状态（§4.5 signOff——Unsigned（默认）/Signed{signer,
 *        signedAtUtc, statement digest}）。
 *
 * 签署**不改变历史结果**（仅元数据事实——§4.5；不赋予也不撤销任何结果
 * 的工程资格）。presence 纪律（字段校验原语执行）：Signed ⇒ signer 非空
 * ＋statementDigest 非零；Unsigned ⇒ signer 空＋digest 零（无凭据残留）。
 */
struct SignOffState {
    /// 签署二态。
    enum class State : std::uint8_t { Unsigned, Signed };

    State state = State::Unsigned;                       ///< 当前签署状态
    /// 签署人（principal；Signed 必填非空）。
    std::string signer;
    /// 签署时刻（UTC）。
    std::chrono::system_clock::time_point signedAtUtc{};
    /// 签署声明摘要（statement digest——SHA-256 原始字节；Signed 必填非零）。
    core::Digest256 statementDigest{};

    /// Unsigned 态工厂（显式表达"默认未签署"）。
    static SignOffState unsignedState() { return SignOffState{}; }
    /// Signed 态工厂（字段赋值；presence 校验归字段校验原语——保持纯值）。
    static SignOffState signedState(std::string signer,
                                    std::chrono::system_clock::time_point atUtc,
                                    core::Digest256 statementDigest)
    {
        SignOffState s;
        s.state = State::Signed;
        s.signer = std::move(signer);
        s.signedAtUtc = atUtc;
        s.statementDigest = statementDigest;
        return s;
    }

    bool operator==(const SignOffState& o) const
    {
        return state == o.state && signer == o.signer && signedAtUtc == o.signedAtUtc
               && statementDigest == o.statementDigest;
    }
    bool operator!=(const SignOffState& o) const { return !(*this == o); }
};

/// 改型差异取舍理由条目（§4.5 variantDiff.tradeOffs——{topic, rationale,
/// sectionId?}）。
struct TradeOff {
    /// 取舍主题（非空）。
    std::string topic;
    /// 取舍理由（非空——RPT-04"取舍理由"呈现义务）。
    std::string rationale;
    /// 关联章节（可选）。
    std::optional<std::string> sectionId;

    bool operator==(const TradeOff& o) const
    {
        return topic == o.topic && rationale == o.rationale && sectionId == o.sectionId;
    }
    bool operator!=(const TradeOff& o) const { return !(*this == o); }
};

/**
 * @brief 改型差异引用块（§4.5 variantDiff——RPT-04：{baselineRevision,
 *        candidateRevision, modelDiffRef(ObjectId), baselineCheckResult,
 *        tradeOffs[]}）。
 *
 * baselineCheckResult 直接消费 evidence::BaselineConsistencyResult
 * （checkComparisonBaselinesConsistent 的纯检查结果——reporting 消费不重
 * 算，基准不一致拒绝归构建器 RPT-T05）。
 */
struct VariantDiffBlock {
    /// 基线修订。
    core::RevisionId baselineRevision;
    /// 候选修订。
    core::RevisionId candidateRevision;
    /// 模型差异实体引用（modeling MDL-08 数据实体——引用非副本）。
    core::ObjectId modelDiffRef;
    /// 基准一致性纯检查结果（evidence §6.5——值拷贝）。
    evidence::BaselineConsistencyResult baselineCheckResult;
    /// 取舍理由集。
    std::vector<TradeOff> tradeOffs;

    bool operator==(const VariantDiffBlock& o) const
    {
        return baselineRevision == o.baselineRevision
               && candidateRevision == o.candidateRevision
               && modelDiffRef == o.modelDiffRef
               && baselineCheckResult == o.baselineCheckResult
               && tradeOffs == o.tradeOffs;
    }
    bool operator!=(const VariantDiffBlock& o) const { return !(*this == o); }
};

/// 评审演化链摘要条目（§4.5 changeLog——{reportId, reportVersion, atUtc,
/// actor, reason}）。
struct ReportVersionEntry {
    /// 链上报告身份。
    ReportId reportId;
    /// 该报告版本号（≥1——§4.1）。
    std::uint32_t reportVersion = 1;
    /// 变更时刻（UTC）。
    std::chrono::system_clock::time_point atUtc{};
    /// 操作者（principal/batch）。
    std::string actor;
    /// 变更原因。
    std::string reason;

    bool operator==(const ReportVersionEntry& o) const
    {
        return reportId == o.reportId && reportVersion == o.reportVersion
               && atUtc == o.atUtc && actor == o.actor && reason == o.reason;
    }
    bool operator!=(const ReportVersionEntry& o) const { return !(*this == o); }
};

/**
 * @brief 评审/签署元数据（§4.5 全字段表——RPT-01-C/RPT-04 的结构化承载，
 *        O-13）。
 *
 * 可修改性纪律（§4.5）：本结构随 ReviewReport 整体不可变——评审元数据的
 * 任何修改（追加意见/签署/改型差异）＝构建**新报告对象**（supersedes 链，
 * dataIdentity 不变、contentIdentity 变化）。
 */
struct ReviewMetadata {
    /// 依据修订（＝报告数据源身份的复核字段——渲染时校验与 dataIdentity
    /// 一致；非空保留值）。
    core::RevisionId basisRevision;
    /// 依据快照（可选）。
    std::optional<core::ContentIdentity> basisSnapshot;
    /// 评审人（C 级条件；principal——受控字段不写日志）。
    std::optional<std::string> reviewer;
    /// 评审时间（UTC；C 级条件）。
    std::optional<std::chrono::system_clock::time_point> reviewedAtUtc;
    /// 评审意见集（可空集；逐条不可变）。
    std::vector<ReviewComment> comments;
    /// 签署状态（默认 Unsigned——签署仅记录事实）。
    SignOffState signOff;
    /// 改型差异引用（C 级条件——RPT-04）。
    std::optional<VariantDiffBlock> variantDiff;
    /// 变更记录（supersedes 链摘要）。
    std::vector<ReportVersionEntry> changeLog;

    bool operator==(const ReviewMetadata& o) const;
    bool operator!=(const ReviewMetadata& o) const { return !(*this == o); }
};

// =====================================================================
// 章节（§4.3.4）与数据源规格（§4.4）
// =====================================================================

/**
 * @brief 评审报告章节（§4.3.4 字段表——报告章节骨架的冻结形态）。
 *
 * 关系约束（§4.7，字段校验原语执行）：sourceResults ⊆ report.resultRefs；
 * Populated ⇒ entries 非空；NoFormalResult/DataInsufficient ⇒ missingItems
 * 非空；引用 Superseded 结果的章节必填 currentness。report 级 sections
 * 按 order 严格递增且 sectionId 唯一。
 */
struct ReviewReportSection {
    /// 章节 ID（§5.1 词表稳定 token——持久化契约）。
    std::string sectionId;
    /// 章节契约版本（提供方注册时声明；入 contentIdentity）。
    std::uint32_t sectionVersion = 1;
    /// 是否选入本报告（缺正式结果默认 false——§16 验收要点）。
    bool selected = false;
    /// 章节状态（§5.3 四态）。
    SectionStatus status = SectionStatus::NoFormalResult;
    /// 来源对象（项目对象——追溯与跳转；可空集）。
    std::vector<core::ObjectId> sourceObjects;
    /// 来源结果（⊆ report.resultRefs；框架章节可空）。
    std::vector<core::RunId> sourceResults;
    /// 章节条目（Populated 必填——§9.2 SectionEntry 冻结视图）。
    std::vector<SectionEntryView> entries;
    /// 缺项全量清单（NoFormalResult/DataInsufficient 必填）。
    std::vector<MissingItemView> missingItems;
    /// 章节引用历史结果的当前性（引用 Superseded 结果时必填——§6.3）。
    std::optional<CurrentnessSnapshot> currentness;
    /// 章节诊断（可空集）。
    std::vector<DiagRefEntry> diagnostics;
    /// 章节级资格说明（可选——由所引结果资格聚合）。
    std::optional<EligibilityNote> eligibilityNote;
    /// 渲染提示（§9.2 RenderHint）。
    RenderHint renderHint = RenderHint::Table;
    /// 排序（§5 冻结顺序——渲染与 CSV/JSON 输出序唯一依据）。
    std::uint16_t order = 0;

    bool operator==(const ReviewReportSection& o) const;
    bool operator!=(const ReviewReportSection& o) const { return !(*this == o); }
};

/// §4.4 dataIdentity 进入编码的选中章节对（sectionId, selected）集的条目。
struct SelectedSectionEntry {
    /// 章节 ID。
    std::string sectionId;
    /// 是否选中。
    bool selected = false;

    bool operator==(const SelectedSectionEntry& o) const
    {
        return sectionId == o.sectionId && selected == o.selected;
    }
    bool operator!=(const SelectedSectionEntry& o) const { return !(*this == o); }
};

/// §4.4 dataIdentity 进入编码的结果引用子集——"(runId, snapshotId,
/// sliceId, inputBaselineId, caseScope) 集"的条目（§4.4 字段表原文）。
struct ResultDataSourceRef {
    /// 运行身份。
    core::RunId runId;
    /// 快照身份（非零）。
    core::ContentIdentity snapshotId;
    /// 切片身份（非零）。
    core::ContentIdentity sliceId;
    /// 输入基准身份（非零）。
    core::ContentIdentity inputBaselineId;
    /// 覆盖工况集。
    std::vector<core::ObjectId> caseScope;

    bool operator==(const ResultDataSourceRef& o) const
    {
        return runId == o.runId && snapshotId == o.snapshotId && sliceId == o.sliceId
               && inputBaselineId == o.inputBaselineId && caseScope == o.caseScope;
    }
    bool operator!=(const ResultDataSourceRef& o) const { return !(*this == o); }
};

/**
 * @brief 报告数据源规格（§3.1 ReportModel.hpp 载荷；§4.4 ReportCodec-Data
 *        的编码主体＝dataIdentity 进入编码字段集的显式形状）。
 *
 * 语义＝§4.1 dataIdentity 行"报告基于哪组冻结输入（修订/快照/结果引用集/
 * 级别/工况范围/单位选择）"——**不含**评审元数据、entries 内容、诊断、
 * 生成时间/者（§4.4 排除列原文；报告级汇总字段 currentnessSummary 等
 * 生成时刻事实亦不在其列——§4.4 进入编码列穷举）。构建器（RPT-T05）从
 * ReportBuildRequest 组装本规格；ReportCodec-Data 对其做规范字节编码与
 * 摘要；ReviewReport::make() 从 ReviewReportFields 抽取同形状子集。
 *
 * 值语义；线程安全：纯值。
 */
struct ReportSourceSpec {
    /// 身份三元组之项目（明确 ID）。
    core::ProjectId project;
    /// 身份三元组之分支。
    core::BranchId branch;
    /// 身份三元组之修订（报告对象解析锚）。
    core::RevisionId revision;
    /// 修订展示序号（不参与身份语义——§4.2；但按 §4.4 进入 Data 编码）。
    std::uint64_t revisionSeq = 0;
    /// 报告级别。
    ReportLevel level = ReportLevel::B;
    /// 报告级统一快照（可选）。
    std::optional<core::ContentIdentity> snapshotId;
    /// 报告级统一切片（可选）。
    std::optional<core::ContentIdentity> inputSliceId;
    /// 结果引用集（§4.4 数据子集形状——ResultDataSourceRef）。
    std::vector<ResultDataSourceRef> resultRefs;
    /// 单位选择（冻结入身份）。
    UnitPreference unitPreference;
    /// 选中章节（sectionId, selected）集。
    std::vector<SelectedSectionEntry> selectedSections;

    bool operator==(const ReportSourceSpec& o) const;
    bool operator!=(const ReportSourceSpec& o) const { return !(*this == o); }
};

// =====================================================================
// ReviewReport——不可变报告对象（§4.2 字段表；make() 唯一生产者）
// =====================================================================

/**
 * @brief 报告级别与章节合法组合的判定规则（§4.6 矩阵的注入面）。
 *
 * cExclusiveSectionIds＝C 专属章节 ID 集（§5.1 词表 9~14 号六个 token——
 * trajectory-cycle/dynamics-envelope/drivetrain-operating-points/
 * selection-bom/review-signoff/variant-diff）。词表权威归 Sections.hpp
 * （RPT-T04）——本任务以注入形态交付组合判定原语（acceptance 3：
 * "B 级携 C 章节"的 §4.2 非法实例判定），避免在 ReportModel 中复制 §5.1
 * 词表造成双权威；RPT-T05 构建器以 Sections.hpp 常量注入。
 */
struct ReportLevelRule {
    /// C 专属章节 ID 集（§5.1 词表——注入方保证与词表一致）。
    std::vector<std::string> cExclusiveSectionIds;
};

/**
 * @brief ReviewReport 构造初值（§4.2 字段表全字段——dataIdentity/
 *        contentIdentity 除外）。
 *
 * 两个内容身份由 make() 内部经 ReportCodec 计算（§4.2"dataIdentity /
 * contentIdentity 由构建器计算，调用方不可申报"——本初值不携带身份字段，
 * 类型层面切断申报通道）。字段顺序与 §4.2 字段表一致。
 */
struct ReviewReportFields {
    ReportId reportId;                     ///< 构建成功时分配（非零保留值）
    ReportLevel level = ReportLevel::B;    ///< 报告级别
    core::ProjectId project;               ///< 身份三元组（明确 ID——禁止占位）
    core::BranchId branch;                 ///< 身份三元组
    core::RevisionId revision;             ///< 身份三元组（报告对象解析锚）
    std::uint64_t revisionSeq = 0;         ///< 展示排序用（不参与身份语义）
    std::optional<core::ContentIdentity> snapshotId;   ///< 报告级统一快照（可选）
    std::optional<core::ContentIdentity> inputSliceId; ///< 报告级统一切片（可选）
    std::vector<ResultRefSnapshot> resultRefs;         ///< 明确结果集（≥1）
    std::vector<EvidenceRefEntry> evidenceRefs;        ///< 证据引用汇总（去重并集）
    std::vector<DiagRefEntry> diagRefs;                ///< 诊断引用汇总
    ReportCurrentnessSummary currentnessSummary;       ///< 当前性快照汇总
    CaseCoverageSummary coverageSummary;               ///< 必验工况覆盖摘要
    std::vector<ExternalResourceStateEntry> externalResourceSummary; ///< 外部资源状态
    evidence::ReproductionBlock reproduction;          ///< 复现要素（evidence 值拷贝）
    std::vector<ReviewReportSection> sections;         ///< 章节列表（≥1，order 有序）
    ReviewMetadata review;                             ///< 评审/签署元数据
    UnitPreference unitPreference;                     ///< 单位选择（冻结入身份）
    std::chrono::system_clock::time_point generatedAtUtc{}; ///< 生成时间（不入身份）
    std::string generatedBy;                           ///< 生成者（principal/batch；不入身份）
    std::string generatorVersion;                      ///< 生成器版本（不入身份）
    std::optional<ReportId> supersedes;                ///< 被本版取代的报告（演化链）
    std::uint32_t reportVersion = 1;                   ///< 评审演化版本（≥1——§4.1）
    std::string sectionModelVersion;                   ///< 章节契约版本（恰为 kSectionModelVersion）

    bool operator==(const ReviewReportFields& o) const;
    bool operator!=(const ReviewReportFields& o) const { return !(*this == o); }
};

/// §4.2 非法实例判定的首个违例码（字段校验原语——acceptance 3）。
///
/// 上下文无关字段校验的**非抛出**核心：按 §4.2 字段表序检查，返回首个
/// 违例的稳定错误码；nullopt＝全部字段校验通过。需要外部事实面的检查
/// （结果未 finalize/envelope 绑定不一致/C 级零 C 章节语义补全）随
/// RPT-T05 构建器收口（acceptance 3 原文）；本原语覆盖的可判定面：
///   - 身份保留值（reportId/project/branch/revision/supersedes/reportId 链）
///   - resultRefs 空/Preview 结果（§4.6 行 5——SourceMissing）
///   - 证据/诊断引用 presence 纪律（§4.3.2/§4.3.3）
///   - 当前性 presence 纪律（§6.3——不可判定必附诊断、reasons 仅
///     Superseded 非空）
///   - 章节结构（order 严格递增/sectionId 唯一/状态-条目-缺项对应/
///     sourceResults ⊆ resultRefs/绑定 caseScope ⊆ 所属结果/
///     Superseded 引用必填 currentness/字段值 NaN·非有限）
///   - 级别×章节组合（§4.6——B 级携 C 专属章节 LevelConflict；C 级零
///     C 章节选中 ScopeInsufficient；词表经 ReportLevelRule 注入）
///   - 评审元数据（comment ≤4KiB/签署 presence/演化链版本 ≥1）
///   - sectionModelVersion 恰为 kSectionModelVersion
///
/// @param fields [in] 待检初值（调用方持有）
/// @param rule   [in] 级别×章节组合规则（C 专属词表注入）
/// @return 首个违例码；nullopt＝通过（纯函数——同输入同结论，NFR-COR-02）
std::optional<ReportErrorCode> firstFieldViolation(const ReviewReportFields& fields,
                                                   const ReportLevelRule& rule);

/**
 * @brief 评审报告对象（§4.2——全部字段构造后不可变，无 setter）。
 *
 * 不可变是类型系统事实：成员私有＋只读访问器＋无任何写路径（§4.2
 * "冻结是类型系统事实：无 setter＋值语义"）；值语义（可拷贝——报告是
 * 值快照）。唯一生产者＝make()：先执行 §4.2 非法实例字段校验（任一违例
 * 抛 ReportError），再经 ReportCodec 计算 dataIdentity/contentIdentity
 * （调用方不可申报——§4.2）。端到端构建拒绝路径（来源解析/envelope 绑定
 * 等需要外部事实面的检查）随 RPT-T05 构建器收口——本类型只承载上下文
 * 无关字段校验（acceptance 3 交付口径）。
 *
 * 身份纪律（§4.1）：dataIdentity＝"报告基于哪组冻结输入"（评审演化链上
 * 不变）；contentIdentity＝"报告全部语义内容的字节摘要"（幂等导出与冲突
 * 判定的**唯一**依据）——与磁盘路径、显示名称、ReportId 分离（重命名/
 * 移动不改变任何身份，§4.1）。生成时间/者/生成器版本不入身份（§4.4——
 * 同内容重建身份一致，幂等判定基础）。
 *
 * 生命周期/所有权：make() 返回值归调用方（值语义，独立深拷贝）。
 * 线程安全：不可变值对象——并发只读安全；make() 可重入。
 */
class ReviewReport {
public:
    /**
     * @brief 唯一生产者：字段校验＋身份计算＋冻结构造。
     *
     * @param fields [in] 报告全字段初值（身份字段除外——见 ReviewReportFields）
     * @param rule   [in] 级别×章节组合规则（C 专属章节词表注入）
     * @return 冻结的报告对象（dataIdentity/contentIdentity 已计算且非零）
     *
     * @throws ReportError firstFieldViolation 返回的首个违例码（§4.2 非法
     *         实例——构建器拒绝语义的类型级承载）；detail 含字段定位
     */
    static ReviewReport make(ReviewReportFields fields, const ReportLevelRule& rule);

    // ---- 只读访问器（§4.2 字段表序——命名与字段表一致；无 setter） ----
    const ReportId& reportId() const noexcept { return m_reportId; }
    ReportLevel level() const noexcept { return m_level; }
    const core::ProjectId& project() const noexcept { return m_project; }
    const core::BranchId& branch() const noexcept { return m_branch; }
    const core::RevisionId& revision() const noexcept { return m_revision; }
    std::uint64_t revisionSeq() const noexcept { return m_revisionSeq; }
    const std::optional<core::ContentIdentity>& snapshotId() const noexcept { return m_snapshotId; }
    const std::optional<core::ContentIdentity>& inputSliceId() const noexcept { return m_inputSliceId; }
    const std::vector<ResultRefSnapshot>& resultRefs() const noexcept { return m_resultRefs; }
    const std::vector<EvidenceRefEntry>& evidenceRefs() const noexcept { return m_evidenceRefs; }
    const std::vector<DiagRefEntry>& diagRefs() const noexcept { return m_diagRefs; }
    const ReportCurrentnessSummary& currentnessSummary() const noexcept { return m_currentnessSummary; }
    const CaseCoverageSummary& coverageSummary() const noexcept { return m_coverageSummary; }
    const std::vector<ExternalResourceStateEntry>& externalResourceSummary() const noexcept
    {
        return m_externalResourceSummary;
    }
    const evidence::ReproductionBlock& reproduction() const noexcept { return m_reproduction; }
    const std::vector<ReviewReportSection>& sections() const noexcept { return m_sections; }
    const ReviewMetadata& review() const noexcept { return m_review; }
    const UnitPreference& unitPreference() const noexcept { return m_unitPreference; }
    const std::chrono::system_clock::time_point& generatedAtUtc() const noexcept
    {
        return m_generatedAtUtc;
    }
    const std::string& generatedBy() const noexcept { return m_generatedBy; }
    const std::string& generatorVersion() const noexcept { return m_generatorVersion; }
    const std::optional<ReportId>& supersedes() const noexcept { return m_supersedes; }
    std::uint32_t reportVersion() const noexcept { return m_reportVersion; }
    const core::ContentIdentity& dataIdentity() const noexcept { return m_dataIdentity; }
    const core::ContentIdentity& contentIdentity() const noexcept { return m_contentIdentity; }
    const std::string& sectionModelVersion() const noexcept { return m_sectionModelVersion; }

    /// 全字段精确相等（身份为字节等值——§4.1 比较纪律；生成信息参与比较
    /// ——报告对象全等强于身份相等：同内容不同生成时刻＝身份相等、对象
    /// 逐字段不等——两概念按 §4.1 区分使用）。
    bool operator==(const ReviewReport& o) const;
    bool operator!=(const ReviewReport& o) const { return !(*this == o); }

private:
    /// 私有构造（唯一调用点＝make()——切断直接构造/聚合初始化通道）。
    ReviewReport(ReviewReportFields&& fields, core::ContentIdentity dataIdentity,
                 core::ContentIdentity contentIdentity);

    ReportId m_reportId;                     ///< 报告对象身份（§4.1）
    ReportLevel m_level;                     ///< 报告级别
    core::ProjectId m_project;               ///< 身份三元组
    core::BranchId m_branch;
    core::RevisionId m_revision;
    std::uint64_t m_revisionSeq;             ///< 展示序（不入身份语义）
    std::optional<core::ContentIdentity> m_snapshotId;
    std::optional<core::ContentIdentity> m_inputSliceId;
    std::vector<ResultRefSnapshot> m_resultRefs;
    std::vector<EvidenceRefEntry> m_evidenceRefs;
    std::vector<DiagRefEntry> m_diagRefs;
    ReportCurrentnessSummary m_currentnessSummary;
    CaseCoverageSummary m_coverageSummary;
    std::vector<ExternalResourceStateEntry> m_externalResourceSummary;
    evidence::ReproductionBlock m_reproduction;
    std::vector<ReviewReportSection> m_sections;
    ReviewMetadata m_review;
    UnitPreference m_unitPreference;
    std::chrono::system_clock::time_point m_generatedAtUtc;
    std::string m_generatedBy;
    std::string m_generatorVersion;
    std::optional<ReportId> m_supersedes;
    std::uint32_t m_reportVersion;
    core::ContentIdentity m_dataIdentity;     ///< 数据源身份（make 经 ReportCodec-Data 计算）
    core::ContentIdentity m_contentIdentity;  ///< 内容身份（make 经 ReportCodec-Full 计算）
    std::string m_sectionModelVersion;
};

// =====================================================================
// 复合类型的相等定义（成员均为已具备 == 的值类型——逐成员全等）
// =====================================================================

inline bool CurrentnessSnapshot::operator==(const CurrentnessSnapshot& o) const
{
    return status == o.status && evaluatedAgainst == o.evaluatedAgainst
           && computedAtUtc == o.computedAtUtc && reasons == o.reasons
           && unevaluableNote == o.unevaluableNote;
}

inline bool DiagRefEntry::operator==(const DiagRefEntry& o) const
{
    return code == o.code && severity == o.severity && category == o.category
           && subject == o.subject && localName == o.localName
           && runtimeName == o.runtimeName && comparison == o.comparison
           && occurrences == o.occurrences && sourceSection == o.sourceSection
           && sourceRun == o.sourceRun;
}

inline bool EvidenceRefEntry::operator==(const EvidenceRefEntry& o) const
{
    return itemId == o.itemId && itemClass == o.itemClass && status == o.status
           && artifactDigest == o.artifactDigest && caseScope == o.caseScope
           && subject == o.subject && notApplicableReason == o.notApplicableReason
           && invalidReasonCode == o.invalidReasonCode && sourceSection == o.sourceSection;
}

inline bool ResultRefSnapshot::operator==(const ResultRefSnapshot& o) const
{
    return runId == o.runId && task == o.task && evaluationKey == o.evaluationKey
           && evaluatorContractVersion == o.evaluatorContractVersion && mode == o.mode
           && outcome == o.outcome && engineeringStatus == o.engineeringStatus
           && snapshotId == o.snapshotId && sliceId == o.sliceId
           && inputBaselineId == o.inputBaselineId && caseScope == o.caseScope
           && eligibility == o.eligibility && currentness == o.currentness
           && producer == o.producer;
}

inline bool ReviewMetadata::operator==(const ReviewMetadata& o) const
{
    return basisRevision == o.basisRevision && basisSnapshot == o.basisSnapshot
           && reviewer == o.reviewer && reviewedAtUtc == o.reviewedAtUtc
           && comments == o.comments && signOff == o.signOff
           && variantDiff == o.variantDiff && changeLog == o.changeLog;
}

inline bool ReviewReportSection::operator==(const ReviewReportSection& o) const
{
    return sectionId == o.sectionId && sectionVersion == o.sectionVersion
           && selected == o.selected && status == o.status
           && sourceObjects == o.sourceObjects && sourceResults == o.sourceResults
           && entries == o.entries && missingItems == o.missingItems
           && currentness == o.currentness && diagnostics == o.diagnostics
           && eligibilityNote == o.eligibilityNote && renderHint == o.renderHint
           && order == o.order;
}

inline bool ReportSourceSpec::operator==(const ReportSourceSpec& o) const
{
    return project == o.project && branch == o.branch && revision == o.revision
           && revisionSeq == o.revisionSeq && level == o.level
           && snapshotId == o.snapshotId && inputSliceId == o.inputSliceId
           && resultRefs == o.resultRefs && unitPreference == o.unitPreference
           && selectedSections == o.selectedSections;
}

inline bool ReviewReportFields::operator==(const ReviewReportFields& o) const
{
    return reportId == o.reportId && level == o.level && project == o.project
           && branch == o.branch && revision == o.revision
           && revisionSeq == o.revisionSeq && snapshotId == o.snapshotId
           && inputSliceId == o.inputSliceId && resultRefs == o.resultRefs
           && evidenceRefs == o.evidenceRefs && diagRefs == o.diagRefs
           && currentnessSummary == o.currentnessSummary
           && coverageSummary == o.coverageSummary
           && externalResourceSummary == o.externalResourceSummary
           && reproduction == o.reproduction && sections == o.sections
           && review == o.review && unitPreference == o.unitPreference
           && generatedAtUtc == o.generatedAtUtc && generatedBy == o.generatedBy
           && generatorVersion == o.generatorVersion && supersedes == o.supersedes
           && reportVersion == o.reportVersion
           && sectionModelVersion == o.sectionModelVersion;
}

}  // namespace sdurws::ird::reporting

#endif  // SDURWS_IRD_REPORTING_REPORTMODEL_HPP
