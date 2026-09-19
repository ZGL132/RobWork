/**
 * @file   Render.hpp
 * @brief  报告渲染契约——FieldMatrix 字段矩阵（单次投影）＋IReportRenderer
 *         格式无关渲染器＋HTML/JSON/CSV 三格式实现＋io 写出设施注入契约族
 *         （RPT-T06 产物，§8.1~§8.4/§9.3/§9.5 注入面）。
 *
 * 设计依据：
 *   - units/reporting.md §8.1（渲染总则与 HTML 结构：多格式共享同一
 *     ReviewReport＋同一 FieldMatrix、稳定排序、to_chars 数值、UTF-8 无
 *     BOM、行尾 LF、HTML5 静态零外部依赖零脚本、data-field 机器可提取锚、
 *     身份规范文本仅追溯区块/附录——UX-02）、§8.2（两类声明前置断言——
 *     RPT-T08 措辞冻结的渲染侧承载门）、§8.3（JSON ird-report-json/1
 *     canonical 与 CSV #rwcsv1 格式契约）、§8.4（FieldMatrix 单次投影——
 *     三渲染器唯一值来源）、§8.5（二次渲染字节比对＝确定性验证组成部分）、
 *     §9.3（IReportRenderer/RenderArtifact/RenderOutcome 契约表）、§9.5
 *     （IReportIoFactory/IReportCsvWriter/IReportJsonWriter/
 *     IReportOutputTarget 注入契约——§9.5 原文列于 Export.hpp，落位偏差
 *     见下方"io 注入契约族的落位说明"）、§6.4（限定语词表的结构性输出）、
 *     §4.3.3（诊断脱敏双保险——渲染侧再次过脱敏）
 *   - 需求 RPT-02（HTML 首版可附 JSON/CSV、多格式逐字段一致——AT-22）、
 *     NFR-SEC-07（诊断脱敏双保险的用户级报告侧承载）、NFR-DEP-04（schema
 *     版本自首版携带——JSON 顶层 schemaVersion＋未知字段拒绝执行侧）、
 *     KIN-12（显示单位纯投影——换算经 core 唯一入口）、UX-02（正文工程
 *     用语，身份规范文本限追溯区块）、D-08（HTML data-field 机器可提取锚）
 *   - 任务契约 tasks/foundation/RPT-T06.json acceptance 1~6
 *
 * 背景说明（渲染器在报告链中的位置）：
 *   构建器（RPT-T05）在 build() 返回时刻冻结 ReviewReport；渲染链消费冻结
 *   报告产出三格式字节。两条铁律在本头落地：
 *   ①值单源（§8.4——AT-22 的结构性前提）：三渲染器对条目字段值的输出
 *   一律取自 extractFieldMatrix() 产出的 FieldMatrix（结构来自报告、值来自
 *   矩阵）——任何格式对同一 fieldKey 的输出只能来自同一 FieldCell，"多格式
 *   共享同一 ReviewReport、不允许每种格式重新计算结果"由此机制化（ARCH
 *   §10.3"渲染单一数据源"）；
 *   ②确定性（§8.1/acceptance 1）：同 (report, matrix, format, 版本) → 字节
 *   相同——稳定排序（章节按 §5 order、条目按 entryKey、证据按 itemId、
 *   诊断按 orderKey）、数值 std::to_chars 最短往返、HTML/JSON 行尾 LF、
 *   UTF-8 无 BOM；二次渲染字节比对（§8.5 步③）由导出链（RPT-T09）在
 *   Verifying 步执行，渲染器侧的义务是"同输入同字节"的纯函数性。
 *
 * io 注入契约族的落位说明（DTB §5.4 偏差登记，单元卡 §14.4 v0.9）：
 *   §9.5 把 IReportIoFactory/IReportCsvWriter/IReportJsonWriter/
 *   IReportOutputTarget/ReplacePolicy/ReportCsvCell/ReportJsonDom 列于
 *   Export.hpp（RPT-T09 产物）；但 §11 RPT-T06 行明文"注入 io 工厂"——
 *   渲染器（RPT-T06）先于导出服务（RPT-T09）消费该族：CSV 转义/方言行、
 *   JSON canonical 序列化的**行为**全归 io 适配实现（SA-12——reporting
 *   不建第二套编码器），渲染器只构造 ReportCsvCell 行与 ReportJsonDom 并
 *   经注入工厂写出。故本头先承载该族（公共头零 io 类型纪律不变——接口
 *   只出现 reporting/core 类型，P-RPT-1），RPT-T09 的 Export.hpp 消费本头
 *   类型、不重复定义（SectionStatus 暂载 ReportModel.hpp 的 v0.6 同型
 *   先例）。IReportOutputTarget 的渲染侧内存实现归 src/Render.cpp 私有
 *   （零 I/O——§9.3 副作用行；落盘归导出服务经 io 原子协议）。
 *
 * P-RPT-6 处置（O-25 已裁决——acceptance 6）：不留 PDF 接口桩。
 *   IReportRenderer 是格式无关抽象（render() 以 ReportRenderFormat 选路、
 *   新格式＝新实现类，非接口变更）；本头不建 PDF 专属字段/配置/空页面
 *   （附录 B v1.3 裁决＋TRJ-08 同精神）；未来 PDF 走需求变更。
 *
 * 线程契约（§9.3 维度表）：IReportRenderer 三实现均并发安全——无共享可变
 *   状态（渲染缓冲局部）；注入引用（脱敏服务/io 工厂）由实现方承诺并发
 *   只读安全（diagnostics §9.5/§3.3 注入面同源）。
 * 确定性：同 (report, matrix, format, rendererVersion, templateVersion) →
 *   字节相同（§9.3 确定性行）；注入 io 适配器的 canonical 确定性（io.md
 *   §5.1/§5.9.3）是 CSV/JSON 字节确定性的上游前提——P-RPT-1 注入形态。
 */

#ifndef SDURWS_IRD_REPORTING_RENDER_HPP
#define SDURWS_IRD_REPORTING_RENDER_HPP

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <sdurws/ird/core/Digest.hpp>    // core::Digest256/ContentDigester（工件摘要——SHA-256 唯一算法）
#include <sdurws/ird/core/Units.hpp>     // core::QuantityKind（单位投影呈现 token 的对映面）

#include <sdurws/ird/diagnostics/Redaction.hpp>  // diagnostics::IRedactionService（脱敏双保险——C-3 登记边）

#include <sdurws/ird/reporting/ReportModel.hpp>  // ReviewReport/QualifierToken/UnitPreference（值单源输入面）

namespace sdurws::ird::reporting {

// =====================================================================
// 格式词表与版本常量（§9.3/§8.3——持久化于工件清单 §7.3）
// =====================================================================

/**
 * @brief 渲染格式三值（§9.3 原文 enum class ReportRenderFormat { Html,
 *        Json, Csv }）。
 *
 * RPT-02：HTML 为首版主格式（发布至少含 HTML——§9.5 非法调用行），JSON
 * （机器读取镜像）与 CSV（可附格式）为附加格式；P-RPT-6：枚举只含已立项
 * 格式——PDF 未立项，不预留词位（新格式＝新增枚举值＋新实现类，走单元卡
 * 增量修订）。
 */
enum class ReportRenderFormat : std::uint8_t {
    Html,  ///< html——HTML5 静态文档（首版主格式）
    Json,  ///< json——ird-report-json/1 canonical 机器镜像
    Csv,   ///< csv——#rwcsv1 方言主表＋附表
};

/// 取格式稳定 token（§8.5 提取器/工件清单格式列；静态存储期——NFR-COR-02）。
std::string_view token(ReportRenderFormat format) noexcept;

/// 渲染器版本（§9.3 RenderArtifact.rendererVersion——入工件清单；实现
/// 行为变更＝升版并登记单元卡变更记录，已发布工件的复验按清单内版本——
/// §14.2 R-6）。
inline constexpr std::uint32_t kReportRendererVersion = 1;

/// HTML 模板版本（§9.3 RenderArtifact.templateVersion；§8.2④"模板版本
/// 升级若删限定语＝版本不兼容拒绝"——模板演化走升版＋单元卡登记）。
inline constexpr std::uint32_t kHtmlTemplateVersion = 1;
/// JSON 模板版本（ird-report-json/1 的模板面版本——键集冻结见
/// kJsonTopLevelKeys）。
inline constexpr std::uint32_t kJsonTemplateVersion = 1;
/// CSV 模板版本（列序冻结主表 11 列＋附表的模板面版本）。
inline constexpr std::uint32_t kCsvTemplateVersion = 1;

/// JSON 顶层 schemaVersion（§8.3 原文——NFR-DEP-04"新格式从首版开始带
/// Schema 版本"；读取方按本值判别兼容性）。
inline constexpr std::string_view kJsonSchemaVersion = "ird-report-json/1";

/**
 * @brief JSON 顶层键冻结集（§8.3"ReviewReport 全字段的结构化序列"的
 *        顶层分组——身份块/章节/条目/证据/诊断/元数据/覆盖/当前性）。
 *
 * "未知字段拒绝"（NFR-DEP-04 执行侧——acceptance 5）的产出侧承载：
 *   渲染器装配完 DOM 后对顶层键集做自检（firstUnknownJsonTopLevelKey），
 *   冻结键集之外的键＝内部不变量违约（DataInvalid）——产出物结构上不可
 *   能携带未知字段；读取侧（RPT-T07 一致性检查回读解析）消费同一冻结集
 *   拒绝外来未知字段（前向兼容纪律：拒绝而非忽略——PM-06 执行侧口径）。
 */
inline constexpr std::array<std::string_view, 17> kJsonTopLevelKeys = {
    "schemaVersion", "report", "source", "identity", "unitPreference",
    "resultRefs", "evidenceRefs", "diagRefs", "currentnessSummary",
    "coverageSummary", "externalResourceSummary", "reproduction",
    "sections", "review", "generatedAtUtc", "generatedBy", "generatorVersion",
};

// =====================================================================
// 渲染选项（§9.3 原文——模板版本随渲染器，不可调用方选）
// =====================================================================

/**
 * @brief 渲染选项（§9.3 原文 struct RenderOptions { format }）。
 *
 * 契约载体：format 为唯一选项——模板版本随渲染器实现固定（§9.3 注释行
 * "模板版本随渲染器（不可调用方选）"），不存在"调用方选模板"的通道
 * （§8.2④ 措辞冻结纪律的类型面表达）。当前 render() 签名按 §9.3 原文
 * 以 ReportRenderFormat 为参，本结构作为契约形状的显式承载保留（未来
 * 选项扩展只增成员、不动签名——P-RPT-6：不设 PDF 等未立项格式的专属
 * 字段）。
 */
struct RenderOptions {
    ReportRenderFormat format = ReportRenderFormat::Html;   ///< 目标格式
};

// =====================================================================
// 字段矩阵（§8.4——单次投影；三渲染器唯一值来源）
// =====================================================================

/**
 * @brief 字段矩阵单元格（§8.4 原文形状——一个条目字段的三格式共同值源）。
 *
 * fieldKey 为层级稳定键＝"<sectionId>.<entryKey>.<field.key>"（段间以
 * '.' 连接；sectionId 全报告唯一、entryKey 节内唯一、field.key 条目内
 * 唯一——三层唯一性保证 fieldKey 全报告唯一，是 HTML data-field 锚、
 * CSV field_key 列与 JSON 字段键的统一对齐键，AT-22 逐字段比对的
 * join key）。
 *
 * valueRepr 为文本规范形（§8.4 原文）：数值＝显示单位换算后的
 * std::to_chars 最短往返文本；四态缺值＝呈现词（NotApplicable→"不适用"、
 * NotProvided→"未提供"，§8.3 CSV 值列规则）；Invalid＝保留原文
 * （NFR-COR-03 不静默转 0）；曲线类复杂结构＝引用文本（"run-<hex>#
 * <fieldPath>"——不复制大数组、不丢弃指向，§8.1 总则/RP-CONS-4）。
 *
 * status 为绑定结果的工程判定 token（core::toToken(EngineeringStatus)——
 * §6.6"工程判定→结论列"的呈现承载；AT-22 七元组的 status 维度）。
 *
 * entryKey 为条目稳定键（§8.4 形状的呈现承载增列——CSV item_label 列的
 * 数据源；条目无独立显示名，entryKey 即标签。DTB §5.4 登记：§8.4 原文
 * 未列出本成员，属实现补强，零语义增删）。
 *
 * qualifier 为限定语 token 集（§6.4 词表——提取期按固定推导序去重生成，
 * §6.4 映射的结构性输出；RPT-T08 措辞冻结任务在此词面集合上收口呈现
 * 措辞）。推导数据源当前面（§6.4 触发数据源列中已入 §4 报告模型者）：
 * estimated（字段来源 GeometricEstimate）/data-insufficient（工程判定/
 * 证据状态/章节状态）/screening-only（Quick 模式）/historical-superseded
 * （当前性 Superseded）/not-applicable（四态）/interrupted·canceled·
 * failed（outcome）；external-validation-incomplete 与
 * downgraded-reference-value 的条目级关联键未在 §4 报告模型冻结（外部
 * 资源与条目的关联结构、RegionCoverageEvidence.downgraded 标志随框架
 * 章节数据源任务落地）——不虚构关联（ERR-01），不全局伪标注。
 *
 * 值语义；线程安全：纯值。
 */
struct FieldCell {
    /// 层级稳定键（见类注——三格式统一对齐键；非空）。
    std::string fieldKey;
    /// 所属章节 ID（§5.1 词表 token）。
    std::string sectionId;
    /// 所属条目键（呈现承载增列——CSV item_label 数据源；见类注）。
    std::string entryKey;
    /// 值文本规范形（见类注——数值 to_chars/四态呈现词/Invalid 原文/引用文本）。
    std::string valueRepr;
    /// 显示单位符号（可选——无量纲/纯文本字段缺席；core 注册表 token）。
    std::optional<std::string> unit;
    /// 绑定结果工程判定 token（core::toToken(EngineeringStatus)——结论列）。
    std::optional<std::string> status;
    /// 限定语 token 集（§6.4——固定推导序去重；可为空集）。
    std::vector<QualifierToken> qualifier;
    /// 结果引用规范文本（"run-<32hex>"——ResultBinding.runId；引用非复制）。
    std::optional<std::string> resultRef;
    /// 证据引用规范文本集（"itemId@<digest 前 12 hex>"——无摘要绑定为
    /// "itemId"；按 itemId 排序去重）。
    std::vector<std::string> evidenceRef;
    /// 工况范围（条目 caseScope 的规范文本序——存储序承载）。
    std::vector<std::string> caseScope;
    /// 当前性呈现 token（"current"/"superseded"/"unevaluable"——末者为
    /// status==nullopt 的不可判定计算形态呈现词，非第三持久态，§6.3）。
    std::optional<std::string> currentness;

    /// 全字段精确相等（同报告同矩阵的判定面——确定性单测的比对载体）。
    bool operator==(const FieldCell& o) const
    {
        return fieldKey == o.fieldKey && sectionId == o.sectionId
               && entryKey == o.entryKey && valueRepr == o.valueRepr
               && unit == o.unit && status == o.status && qualifier == o.qualifier
               && resultRef == o.resultRef && evidenceRef == o.evidenceRef
               && caseScope == o.caseScope && currentness == o.currentness;
    }
    bool operator!=(const FieldCell& o) const { return !(*this == o); }
};

/// 字段矩阵（§8.4 原文——vector<FieldCell>；遍历序＝章节 order＋条目
/// entryKey 序＋字段存储序，同报告同矩阵）。
using FieldMatrix = std::vector<FieldCell>;

/**
 * @brief 字段矩阵提取（§8.4/§8.5 步①——对冻结报告的一次性遍历投影）。
 *
 * 前置：report 已冻结（ReviewReport::make 产出即保证——contentIdentity
 * 非零；违约＝调用方绕过 make 构造，Usage fail-fast）。字段值换算经 core
 * 唯一入口（tryConvert——KIN-12 显示单位纯投影）；字段显示单位与报告级
 * UnitPreference 冻结集冲突（同量纲条目 token 不一致）＝报告数据非法
 * （DataInvalid——unitPreference 冻结入身份的执行侧一致性闸）。
 *
 * 后置：矩阵确定性（同报告同矩阵——遍历序＝章节 order＋条目 entryKey 稳定
 * 序＋字段存储序；无时钟/随机/locale 依赖，NFR-COR-02）；单元格与报告
 * 字段一一对应（每条目字段恰一单元格）。
 *
 * @param report [in] 冻结报告（只读——本函数不修改任何输入）
 * @return 字段矩阵（值语义，独立副本）
 *
 * @throws ReportError Usage（报告身份非法——非 make() 产出）/
 *         DataInvalid（单位投影与冻结集冲突、SI 单位基线缺位、换算非有限
 *         溢出、结果引用越界——均为报告数据问题，与渲染层错误严格区分）
 *
 * 复杂度：O(R·S·E·F)（R＝结果数、S＝章节数、E＝条目数、F＝字段数——
 * 单次线性遍历＋每字段一次单位换算；提取一次、三格式共享——§8.4 单次
 * 投影纪律）。
 */
FieldMatrix extractFieldMatrix(const ReviewReport& report);

// =====================================================================
// 渲染产物与结果（§9.3）
// =====================================================================

/**
 * @brief 渲染产物（§9.3 原文 RenderArtifact——内存字节，零 I/O）。
 *
 * digest 为 bytes 的 SHA-256（core::ContentDigester 唯一算法——§4.1 摘要
 * 纪律）；rendererVersion/templateVersion 入工件清单（§73——§14.2 R-6
 * 复验按清单内版本）；sourceReportIdentity 为源报告内容身份（产物与报告
 * 的关联锚——导出链组装工件清单的事实面）。
 *
 * 值语义；线程安全：纯值。
 */
struct RenderArtifact {
    ReportRenderFormat format = ReportRenderFormat::Html;  ///< 产物格式
    std::vector<std::uint8_t> bytes;                       ///< 完整字节（canonical——同输入同字节）
    core::Digest256 digest{};                              ///< SHA-256(bytes)（core 唯一算法）
    std::uint32_t rendererVersion = kReportRendererVersion;  ///< 渲染器版本（入工件清单）
    std::uint32_t templateVersion = 1;                     ///< 模板版本（入工件清单——按格式取对应常量）
    core::ContentIdentity sourceReportIdentity;            ///< 源报告内容身份（非零——冻结报告）
};

/**
 * @brief 渲染结果（§9.3 render() 返回值——成功/失败两形态，无取消形态：
 *        §9.3 取消行"渲染本身不可取消（纯内存生成）"）。
 *
 * 成功＝artifact 唯一在场（完整字节；失败＝无部分产物外泄——§9.3 后置行，
 * 渲染缓冲全程局部）；失败＝error 在场（RenderFailed＝模板/编码层错误、
 * DataInvalid＝报告/矩阵数据非法、TemplateVersion＝章节模型版本不兼容、
 * Usage＝调用方前置违约——与数据错误严格区分，§8.1 总则/§9.3 错误行）。
 */
struct RenderOutcome {
    /// 渲染产物（成功时唯一非空）。
    std::optional<RenderArtifact> artifact;
    /// 失败错误（成功恒缺席）。
    std::optional<ReportError> error;
};

// =====================================================================
// IReportRenderer——格式无关渲染器接口（§9.3；P-RPT-6：新格式＝新实现）
// =====================================================================

/**
 * @brief 报告渲染器接口（§9.3——服务级、无状态；实现经注入使用 io 写出
 *        设施）。
 *
 * 契约（§9.3 签名注释行逐条）：
 *   - 前置：report 已冻结（contentIdentity 非零）；matrix 与 report 同源
 *     （调用方保证——导出链内部生成；本接口以"报告每条目字段恰有同
 *     fieldKey 单元格、矩阵每单元格恰有报告字段"的双向完整性检查执行同源
 *     验证，违约＝DataInvalid）；
 *   - 后置：成功＝完整字节（canonical：同输入同字节）；失败＝无部分产物
 *     外泄（内存缓冲，落盘归导出服务经 io 原子协议）；
 *   - 错误：RenderFailed（模板/编码层）/DataInvalid（报告字段或矩阵非法）
 *     /TemplateVersion（章节模型版本不兼容）/Usage（前置违约）；
 *   - 取消：无内部检查点（纯内存生成、毫秒级——落盘阶段在导出服务取消）。
 *
 * 线程：并发安全（无状态；缓冲局部）。副作用：零 I/O（字节返回内存）。
 * 合法调用：三格式任意子集（RPT-02——至少渲染 HTML 方可发布，发布门禁
 * 归导出链）。非法调用：从报告以外数据源取值（值单源纪律——字段值一律
 * 出自矩阵单元格）；输出未经资格断言的"正式通过"字样（§8.2 前置断言）。
 */
class IReportRenderer {
public:
    virtual ~IReportRenderer() = default;

    /**
     * @brief 渲染（§9.3 原文签名——实现按 format 选路；不支持的格式＝
     *        调用方违约 Usage fail-fast，实现类只服务自己的格式词位）。
     *
     * @param report [in] 冻结报告（只读；渲染期不可变——类型系统无写途径）
     * @param matrix [in] 字段矩阵（§8.4——值单源；与 report 同源）
     * @param format [in] 目标格式（三值词表——§9.3）
     *
     * @return 渲染结果（成功/失败两形态——RenderOutcome 注释）
     *
     * 复杂度：O(S·E·F＋M)（矩阵查找均摊 O(1)；M＝输出字节数——单遍装配）。
     */
    virtual RenderOutcome render(const ReviewReport& report, const FieldMatrix& matrix,
                                 ReportRenderFormat format) = 0;
};

// =====================================================================
// io 写出设施注入契约族（§9.5 原文——落位偏差见文件头"落位说明"；
// 公共头零 io 类型纪律〔P-RPT-1〕：接口只出现 reporting/core 类型，
// L5 装配适配 io，P-RPT-1 裁决补边后直连、签名零改动）
// =====================================================================

/**
 * @brief 替换策略（§9.5 ExportDestination/makeAtomicTarget 的策略词表——
 *        reporting 自有同形类型，L5 适配器映射到 io ReplacePolicy
 *        （AtomicFile.hpp 同名两值——ReportCancelToken/IoCancelToken 同形
 *        先例）；裁决补边后原位替换、签名零改动）。
 *
 * NeverOverwrite＝目标存在即拒绝（外部导出默认——不覆盖未经确认的既有
 * 文件）；OverwriteAtomic＝显式确认后的原子替换（失败保留先前输出——
 * §7.4）。渲染器不消费本枚举（内存目标无替换语义）——导出链（RPT-T09）
 * 消费；随 io 注入契约族先落位于本头（落位说明见文件头）。
 */
enum class ReplacePolicy : std::uint8_t {
    NeverOverwrite,   ///< never-overwrite——目标存在即拒绝（默认）
    OverwriteAtomic,  ///< overwrite-atomic——确认后原子替换（失败保留先前输出）
};

/**
 * @brief 原子输出目标抽象（§9.5 原文——io IAtomicFileWriter 的投影）。
 *
 * 方法契约（§9.5 原文 commit/abort 两行＋字节通道的形状补全，见下）：
 *   - write(bytes, n)：追加字节（append 语义——调用序即字节序；二进制
 *     透明，UTF-8 文本与原始字节均按原样写入）。§9.5 的投影只列
 *     commit/abort，未定义字节通道——io IAtomicFileWriter 的写入方法在
 *     io 侧，投影到 reporting 侧必须留出字节通道，否则 CSV/JSON writer
 *     无从落字节（§9.5"签名为实现建议"口径下的形状补全，登记 DTB §5.4
 *     ——单元卡 §14.4 v0.9）。
 *   - commit()：原子替换提交（失败/放弃→目标不变，返回 false）。
 *   - abort()：清理临时（幂等——放弃路径）。
 *
 * 渲染链的内存缓冲实现归本头 MemoryReportOutputTarget（渲染器零 I/O——
 * §9.3 副作用行）；文件目标实现归 L5 的 io 适配器（导出链消费——写入
 * 映射到 io IAtomicFileWriter）。
 *
 * 线程约束：单次渲染/导出会话内单线程使用（写入序即字节序——非线程安全）。
 */
class IReportOutputTarget {
public:
    virtual ~IReportOutputTarget() = default;

    /**
     * @brief 追加字节段（可为空段——空段无效果，不视为错误）。
     * @param bytes [in] 字节段首址（n==0 时可 nullptr）
     * @param n     [in] 字节数
     * @return 写入成功 true；目标已失效（abort 后/容量耗尽等）false
     */
    virtual bool write(const std::uint8_t* bytes, std::size_t n) = 0;

    /// 原子替换提交（成功 true；失败/放弃→目标不变——§9.5 原文注释）。
    virtual bool commit() = 0;
    /// 清理临时（幂等——放弃路径）。
    virtual void abort() = 0;
};

/**
 * @brief 渲染链的内存输出目标（render() 的零 I/O 字节汇——§9.3 副作用行
 *        "零 I/O（字节返回内存）"的承载）。
 *
 * 指向调用方提供的字节缓冲（不接管所有权——缓冲须覆盖 open→finish 全程）；
 * commit() 恒成功（内存态无原子替换语义——落盘归导出服务）；abort() 后
 * 拒绝再写（防半途产物混入）。渲染器在 finish 后从缓冲取字节组装
 * RenderArtifact——io 适配器（CSV 转义/JSON canonical）经 IReportOutputTarget
 * 接口写本目标，reporting 不绕过适配器自拼字节（SA-12——不建第二套编码器）。
 *
 * 线程约束：单线程（见 IReportOutputTarget）。
 */
class MemoryReportOutputTarget final : public IReportOutputTarget {
public:
    /**
     * @brief 绑定字节缓冲（借用——不接管所有权）。
     * @param buffer [in,out] 目标缓冲（调用方持有；须覆盖本目标存续期）
     */
    explicit MemoryReportOutputTarget(std::vector<std::uint8_t>* buffer) noexcept;

    // ---- IReportOutputTarget（§9.5 投影——内存态语义见类注）----
    bool write(const std::uint8_t* bytes, std::size_t n) override;
    bool commit() override;
    void abort() override;

private:
    std::vector<std::uint8_t>* m_buffer;   ///< 目标缓冲（借用；非空——构造前置）
    bool m_aborted = false;                ///< abort 后拒绝再写（防半途产物混入）
};

/**
 * @brief CSV 单元格（§9.5 IReportCsvWriter::writeRow 的行载荷——
 *        "string|int64|double|空"四形态）。
 *
 * Double 态数值由 io 适配器以 std::to_chars 最短往返写出（io canonical
 * 同口径——§8.1 总则）；Text 态的转义/引号/换行全归 io ICsvWriter（§8.3
 * ——前缀转义唯一实现，NFR-SEC-03）；Empty 态写出为空字段（与"不适用"
 * 文本区分——§8.3 空值规则）。工厂对 Double 态拒绝非有限值（DataInvalid
 * ——NaN/±Inf 不得进入 CSV 数值通道；四态缺值以 Text 态呈现词表达）。
 *
 * 值语义；线程安全：纯值。
 */
struct ReportCsvCell {
    /// 单元格形态（§9.5 注释列四形态）。
    enum class Kind : std::uint8_t { Empty, Text, Integer, Number };

    Kind kind = Kind::Empty;        ///< 当前形态（默认空字段）
    std::string text;               ///< Text 态载荷（UTF-8；转义归 io）
    std::int64_t integer = 0;       ///< Integer 态载荷
    double number = 0.0;            ///< Number 态载荷（io to_chars canonical）

    /// 空字段工厂。
    static ReportCsvCell empty() { return ReportCsvCell{}; }
    /// 文本工厂。
    static ReportCsvCell textCell(std::string value);
    /// 整数工厂。
    static ReportCsvCell integerCell(std::int64_t value);
    /// 数值工厂（非有限拒绝）。
    /// @throws ReportError(DataInvalid) value 为 NaN/±Inf
    static ReportCsvCell numberCell(double value);
};

/**
 * @brief 报告 JSON DOM 值（§9.5 IReportJsonWriter::write 的载荷——
 *        reporting 侧结构化树，canonical 序列化行为归 io）。
 *
 * 六型值（Null/Bool/Number/String/Array/Object）：Object 为**有序**键值对
 * （装配序即写出序——"键序固定"由 DOM 装配侧决定、io 逐对写出——§8.3
 * canonical 口径），重复键在工厂拒绝（DataInvalid——对象键唯一性纪律）；
 * Number 为 double（io 侧 to_chars 最短往返写出——§8.3；非有限拒绝，
 * 整型计数值以 double 承载精度安全上界 2^53——报告计数量级远低于此）。
 *
 * 字符串不在此处转义（canonical 转义归 io IJsonWriter——SA-12 同纪律）。
 *
 * 值语义（递归值树——深拷贝语义）；线程安全：纯值。
 */
class ReportJsonDom {
public:
    /// 值型六值。
    enum class Type : std::uint8_t { Null, Bool, Number, String, Array, Object };

    /// null 工厂。
    static ReportJsonDom nullValue();
    /// 布尔工厂。
    static ReportJsonDom boolean(bool value);
    /// 数值工厂（非有限拒绝）。
    /// @throws ReportError(DataInvalid) value 为 NaN/±Inf
    static ReportJsonDom number(double value);
    /// 字符串工厂。
    static ReportJsonDom string(std::string value);
    /// 数组工厂（保序）。
    static ReportJsonDom array(std::vector<ReportJsonDom> items);
    /// 对象工厂（装配序＝写出序；重复键拒绝）。
    /// @throws ReportError(DataInvalid) members 存在重复键
    static ReportJsonDom object(std::vector<std::pair<std::string, ReportJsonDom>> members);

    /// 值型（构造后不变）。
    Type type() const noexcept { return m_type; }
    /// 布尔载荷（前置 type()==Bool；违约 Usage fail-fast）。
    bool booleanValue() const;
    /// 数值载荷（前置 type()==Number）。
    double numberValue() const;
    /// 字符串载荷（前置 type()==String）。
    const std::string& stringValue() const;
    /// 数组载荷（前置 type()==Array；保序）。
    const std::vector<ReportJsonDom>& arrayItems() const;
    /// 对象载荷（前置 type()==Object；保序键值对）。
    const std::vector<std::pair<std::string, ReportJsonDom>>& objectMembers() const;

private:
    Type m_type = Type::Null;                                 ///< 值型
    bool m_bool = false;                                      ///< Bool 态载荷
    double m_number = 0.0;                                    ///< Number 态载荷
    std::string m_string;                                     ///< String 态载荷
    std::vector<ReportJsonDom> m_array;                       ///< Array 态载荷（保序）
    std::vector<std::pair<std::string, ReportJsonDom>> m_object;  ///< Object 态载荷（保序、键唯一）
};

/**
 * @brief CSV 写出器投影（§9.5 原文——io ICsvWriter 的投影；方言行/
 *        转义/RFC4180 行为全在 io，工厂零行为——P-RPT-1 边界价值）。
 *
 * 节奏契约：open（目标会话移交＋方言行开关）→ writeHeader（恰一次）→
 * writeRow（≥0 次）→ finish（恰一次；终结本表）。方法返回 false＝io 侧
 * 写出失败（目标失效/编码失败——渲染器据此报 RenderFailed，不含部分
 * 产物外泄）。emitDialectMarker=true 时 io 在首行写出方言标识行（#rwcsv1
 * ——§8.3；字面权威归 io，reporting 不复述方言文本）。
 *
 * 目标生命周期（&& 签名的会话语义——io ICsvWriter::open 的
 * "右值接收——暂存状态随本会话迁移"同口径）：target 须覆盖 open→finish
 * 全程存活（渲染器的内存缓冲同理——writer 内部以引用/指针消费目标，
 * 会话结束即失效）；同一目标不得同时供两个 writer 会话使用（字节序
 * 单线程纪律）。
 */
class IReportCsvWriter {
public:
    virtual ~IReportCsvWriter() = default;

    /// 打开表（§9.5 原文签名——目标会话移交；emitDialectMarker＝方言标识行开关）。
    virtual bool open(IReportOutputTarget&& target, bool emitDialectMarker = true) = 0;
    /// 写表头（恰一次；列名转义归 io）。
    virtual bool writeHeader(const std::vector<std::string>& columns) = 0;
    /// 写一行（列数须与表头一致——io 侧校验；单元格转义归 io）。
    virtual bool writeRow(const std::vector<ReportCsvCell>& cells) = 0;
    /// 终结本表（io 侧行尾/缓冲收口；终结后本 writer 不可再用）。
    virtual bool finish() = 0;
};

/**
 * @brief JSON 写出器投影（§9.5 原文——io IJsonWriter 的投影；canonical
 *        序列化〔键序固定、to_chars 数值、2 空格缩进、LF〕全在 io——
 *        §8.3；同 DOM 二次写出字节相同）。
 */
class IReportJsonWriter {
public:
    virtual ~IReportJsonWriter() = default;

    /// canonical 写出（§9.5 原文签名——目标会话移交，生命周期约定同
    /// IReportCsvWriter::open；单调用融合打开/写出/终结）。
    virtual bool write(IReportOutputTarget&& target, const ReportJsonDom& dom) = 0;
};

/**
 * @brief io 注入工厂（§9.5 原文——reporting 定义、L5 装配适配 io；裁决
 *        补边后可直连 io、签名零改动）。
 *
 * 边界价值（NFR-MNT-04 非转发包装器）：隔离 ARCH §3.5 未登记的
 * reporting→io 编译边（P-RPT-1）＋约束报告只能使用 io 的 canonical/原子
 * 写出路径（禁止报告自建第二套编码器——SA-12 的实施件）。工厂本身零
 * 行为（§3.3 原文）：CSV 方言行/转义/JSON canonical 全部行为归 io 适配
 * 实现。渲染器消费 makeCsvWriter/makeJsonWriter（内存目标由渲染器提供
 * ——零 I/O）；makeAtomicTarget 供导出链（RPT-T09）落盘。
 *
 * 线程：并发只读安全（工厂无状态——每次调用产出独立 writer）。
 */
class IReportIoFactory {
public:
    virtual ~IReportIoFactory() = default;

    /// CSV 写出器（io 适配实现——方言/转义行为载体）。
    virtual std::unique_ptr<IReportCsvWriter> makeCsvWriter() const = 0;
    /// JSON 写出器（io 适配实现——canonical 序列化行为载体）。
    virtual std::unique_ptr<IReportJsonWriter> makeJsonWriter() const = 0;
    /// 原子输出目标（io IAtomicFileWriter 投影——导出链落盘用）。
    virtual std::unique_ptr<IReportOutputTarget>
    makeAtomicTarget(const std::filesystem::path& path, ReplacePolicy policy) const = 0;
};

// =====================================================================
// 三格式渲染器实现（§9.3"三格式实现可独立替换/新增——新格式＝新实现"）
// =====================================================================

/**
 * @brief HTML 渲染器（§8.1——HTML5 静态文档：零外部依赖〔无外链
 *        CSS/JS/字体〕、零脚本、样式内联、meta charset=utf-8、行尾 LF、
 *        无 BOM）。
 *
 * 结构（§8.1 原文）：报告头块（含追溯区块——身份规范文本限定区）→目录
 * （章节锚点）→逐章节 section（id="sec-<sectionId>"；状态徽标/条目表
 * data-field 锚/折叠追溯区块〔证据表＋结果引用＋章节诊断＋资格声明〕/
 * 缺项清单/当前性标记/限定语标记 data-qualifier）→诊断汇总→外部验证
 * 边界→评审签署块（C 级）→追溯附录（完整身份规范文本域）。
 *
 * 脱敏双保险（NFR-SEC-07/§4.3.3）：全部人读自由文本（诊断定位名/缺项
 * 原因/资格说明/评审意见等——词表 token 与规范身份文本除外）渲染前经
 * 注入的 IRedactionService 以 User 档再过一遍脱敏；身份规范文本（哈希/
 * Schema/内部插件名）只出现在追溯区块与附录（UX-02——正文用缩略形＋
 * 工程用语）。
 *
 * 资格声明前置断言（§8.2①）：章节资格块对"正式通过"字样的输出以
 * formalPassAllowed==true 为硬门槛（violation→DataInvalid——不存在
 * "渲染了再说"路径）；"经验证的不可行结论（正式评审记录）"独立声明
 * （②——两声明绝不互换措辞）。
 *
 * 线程：并发安全（无共享可变状态；缓冲局部）。注入引用须覆盖全部
 * render() 调用期（借用——L5 装配持有）。
 */
class HtmlReportRenderer final : public IReportRenderer {
public:
    /**
     * @brief 注入构造（脱敏服务——C-3 登记边的只读消费）。
     * @param redaction [in] 脱敏服务（borrow；并发只读安全由实现方承诺）
     */
    explicit HtmlReportRenderer(const diagnostics::IRedactionService& redaction);

    /// 非拷贝（借用语义）。
    HtmlReportRenderer(const HtmlReportRenderer&) = delete;
    HtmlReportRenderer& operator=(const HtmlReportRenderer&) = delete;

    RenderOutcome render(const ReviewReport& report, const FieldMatrix& matrix,
                         ReportRenderFormat format) override;

private:
    const diagnostics::IRedactionService* m_redaction;  ///< 脱敏服务（借用；只读）
};

/**
 * @brief JSON 渲染器（§8.3——ird-report-json/1 机器读取镜像：顶层
 *        schemaVersion、SourcedValue 四态显式 token、限定语数组字段、
 *        canonical 写出经注入 io IJsonWriter〔键序固定、to_chars 数值、
 *        2 空格缩进、LF〕、未知字段拒绝〔NFR-DEP-04 执行侧——kJsonTopLevelKeys
 *        冻结集自检〕）。
 *
 * 值单源：条目字段的 value/unit/qualifier 一律取自矩阵单元格（§8.4）；
 * 四态 state token 来自报告字段结构（结构来自报告、值来自矩阵——§8.4
 * 原文分工）。数值以文本规范形（to_chars）进 JSON 字符串——三格式逐字符
 * 一致（AT-22"数值以文本规范形比对——避免二次舍入"）的承载。
 *
 * 报告 JSON 工件**不是**项目权威数据（§8.3——权威归 reports/ 的
 * report.json＋对象库；本工件是导出镜像）。
 *
 * 线程：并发安全。注入引用须覆盖 render() 调用期。
 */
class JsonReportRenderer final : public IReportRenderer {
public:
    /**
     * @brief 注入构造（io 工厂——canonical 写出设施；脱敏服务——自由文本
     *        双保险，与 HTML 同口径）。
     * @param ioFactory [in] io 注入工厂（borrow；工厂零行为——行为归适配）
     * @param redaction [in] 脱敏服务（borrow）
     */
    JsonReportRenderer(const IReportIoFactory& ioFactory,
                       const diagnostics::IRedactionService& redaction);

    /// 非拷贝（借用语义）。
    JsonReportRenderer(const JsonReportRenderer&) = delete;
    JsonReportRenderer& operator=(const JsonReportRenderer&) = delete;

    RenderOutcome render(const ReviewReport& report, const FieldMatrix& matrix,
                         ReportRenderFormat format) override;

private:
    const IReportIoFactory* m_ioFactory;                ///< io 工厂（借用；零行为）
    const diagnostics::IRedactionService* m_redaction;  ///< 脱敏服务（借用；只读）
};

/**
 * @brief CSV 渲染器（§8.3——io 方言 #rwcsv1：主表 report.csv 列序冻结
 *        11 列〔field_key,section_id,item_label,value,unit,status,
 *        qualifier,result_ref,evidence_ref,case_scope,currentness〕＋
 *        诊断附表 diagnostics.csv＋多工况明细附表 coverage.csv；转义/
 *        引号/换行/方言行全经注入 io ICsvWriter——NFR-SEC-03 前缀转义
 *        唯一实现）。
 *
 * 单一 CSV 工件的表组装（§8.5 单产物形状的承载，登记单元卡 §14.4 v0.9）：
 *   工件字节＝三份完整 CSV 文档顺序拼接（report→diagnostics→coverage），
 *   每份以自身方言标识行开头（emitDialectMarker=true——io 落行），文档间
 *   以一个空行（LF）分隔——读取方以方言行定位表边界、以表头行判别表身份
 *   （field_key/code/case_id 首列）。值列规则（§8.3）：NotApplicable→
 *   "不适用"、NotProvided→"未提供"、Invalid→保留原文；空值＝空字段
 *   （与"不适用"区分）。
 *
 * 覆盖附表的数据面登记：§4 报告模型的 CaseCoverageSummary 为五态计数
 * 聚合，不含逐工况行（caseId/status/runId 的数据源＝evidence 逐工况矩阵，
 * 未入 §4 冻结模型）——本任务交付列序冻结的附表面（表头＋方言行），
 * 数据行待逐工况覆盖数据入模型后填充（单元卡 §14.4 v0.9 登记；不伪造
 * 逐工况状态——ERR-01）。
 *
 * 线程：并发安全。注入引用须覆盖 render() 调用期。
 */
class CsvReportRenderer final : public IReportRenderer {
public:
    /**
     * @brief 注入构造（io 工厂——CSV 转义/方言行行为载体；脱敏服务——
     *        诊断附表自由文本双保险）。
     */
    CsvReportRenderer(const IReportIoFactory& ioFactory,
                      const diagnostics::IRedactionService& redaction);

    /// 非拷贝（借用语义）。
    CsvReportRenderer(const CsvReportRenderer&) = delete;
    CsvReportRenderer& operator=(const CsvReportRenderer&) = delete;

    RenderOutcome render(const ReviewReport& report, const FieldMatrix& matrix,
                         ReportRenderFormat format) override;

private:
    const IReportIoFactory* m_ioFactory;                ///< io 工厂（借用；零行为）
    const diagnostics::IRedactionService* m_redaction;  ///< 脱敏服务（借用；只读）
};

}  // namespace sdurws::ird::reporting

#endif  // SDURWS_IRD_REPORTING_RENDER_HPP
