/**
 * @file   Errors.hpp
 * @brief  reporting 错误契约——ReportErrorCode 全表（16 个稳定 token）＋
 *         ReportError 异常＋RPT-* 稳定诊断码引用登记清单。
 *
 * 设计依据：
 *   - units/reporting.md §3.5（错误类型与稳定诊断码：ReportErrorCode 16 值
 *     token 稳定、ReportError : std::runtime_error、detail 前缀
 *     "reporting/<域>:"口径；RPT-* 码清单——码值权威＝diagnostics
 *     StableCodeRegistry，已收编全量 8 项〔diagnostics.md §4.5/§4.6，
 *     2026-09-10〕）、§3.1 组成表（`Errors.hpp`｜ReportErrorCode／ReportError／
 *     RPT-* 码建议清单）、§1.4 异常行（可恢复查询路径提供 try* 非抛出变体）
 *   - 需求 RPT-01（报告平台服务错误语义承载）、ERR-01（稳定诊断码族支撑——
 *     §2.1 O-15 上游依据）、NFR-COR-02（同码同串确定性）、NFR-COR-03（不静默）
 *   - 任务契约 tasks/foundation/RPT-T02.json acceptance 2（16 token 逐值与
 *     §3.5 清单一致；detail 前缀口径；try* 约定声明）与 acceptance 3
 *     （RPT-* 码清单按收编表引用登记——P-RPT-8）
 *
 * 背景说明（为什么 reporting 自有一套错误码，而不复用 core::CoreError）：
 *   与 evidence::EvidenceError 同案（EV-T02 先例）——core 的 CoreError 只覆盖
 *   core 自身抛错路径；reporting 的错误面横跨来源解析（§7.1/§9.1）、级别与
 *   章节合法性（§4.6/§5）、渲染与一致性（§8）、导出与归档（§7.4/§9.5/§9.6）、
 *   证据包（§7.6）与往返复算（§7.7），各产生位置有独立的恢复路径与稳定码面。
 *   异常轨（本头）与后续任务的判定/投影共用同一码面；码值权威＝diagnostics
 *   StableCodeRegistry（PA-1 权威唯一）——本单元只登记 token 与建议码的
 *   **引用**，不承担码值注册职责（acceptance 3"引用登记非二次定义"）。
 *
 * 错误语义总纲（AGENTS.md 错误语义在本单元的落点）：
 *   - 调用方契约违约（如对不可变 ReviewReport 的写请求、非法注册——随
 *     RPT-T04/T05 落地）＝fail-fast（抛 ReportError(Usage)），不走诊断收集；
 *   - 环境/数据错误（来源不可解析、磁盘不足、归档冲突等）以 ReportError
 *     表达但携带可定位 detail＋对应 RPT-* 稳定诊断码（经诊断通道上报——
 *     NFR-COR-03：任何失败都不得静默转换为默认通过）；
 *   - 可恢复查询路径一律另有 try* 非抛出变体（§1.4 异常行）——本头即按该
 *     约定提供 ReportId::tryFromCanonical / tryLevelFromToken（Identity.hpp）
 *     与后续任务的 try* 查询接口；抛出轨迹（fromCanonical 等）仅用于
 *     解码边界上的 fail-fast。
 *
 * 线程安全：本头全部实体为纯值/纯函数（无共享可变状态），并发只读安全。
 * 确定性：token/建议码为编译期固定表（switch 全枚举），同码同串、跨平台
 * 跨进程逐字节一致（NFR-COR-02 的码面子集）。
 */

#ifndef SDURWS_IRD_REPORTING_ERRORS_HPP
#define SDURWS_IRD_REPORTING_ERRORS_HPP

#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace sdurws::ird::reporting {

// =====================================================================
// ReportErrorCode——reporting 全量稳定错误码枚举（§3.5 原文 16 值，表序
// ＝§3.5 清单序）。顺序与数值一经交付不得改动/插入——token 虽为字符串，
// 但枚举数值进入二进制契约面，稳定第一（evidence/runtime 同款纪律）；
// 后续任务需要新码时只能**追加表尾**并在单元卡增量修订留痕（DTB §5.4）。
// =====================================================================

/**
 * @brief reporting 稳定错误码全表（§3.5 清单原文 16 值）。
 *
 * token 稳定（持久化于诊断与导出物）；诊断码面见 diagcodes 登记清单与
 * suggestedDiagCode()。每值的产生位置（单元卡章节锚点）随对应接口任务
 * （RPT-T03~T10）落地登记于其抛错点；本表只冻结码面本身。
 */
enum class ReportErrorCode : std::uint8_t {
    /// reporting/source-missing——引用的修订/快照/结果不存在或未 finalize
    /// （§9.1 构建请求锚定失败；建议码 RPT-SOURCE-MISSING）。
    SourceMissing,
    /// reporting/source-ambiguous——来源指定不明确（占位"当前"值——§4.2
    /// 身份三元组禁止"当前/tip"占位，明确 ID 纪律的拒绝码面）。
    SourceAmbiguous,
    /// reporting/scope-insufficient——C 级请求但全部 C 章节缺正式结果
    /// （§5.4 保守处置：宁可拒绝不可虚级；建议码 RPT-SCOPE-INSUFFICIENT——
    /// 诊断面为降级建议 Warning）。
    ScopeInsufficient,
    /// reporting/level-conflict——B 级携带 C 章节／章节级别与报告级别冲突
    /// （§4.6 合法组合矩阵的构建期拒绝）。
    LevelConflict,
    /// reporting/evidence-ref-invalid——章节证据引用未通过绑定校验
    /// （§4.3.2 EvidenceBinding：引用与所属结果 envelope 不一致等）。
    EvidenceRefInvalid,
    /// reporting/consistency-mismatch——多格式逐字段不一致（§8.5，AT-22
    /// 反例；建议码 RPT-CONSISTENCY-MISMATCH）。
    ConsistencyMismatch,
    /// reporting/render-failed——渲染错误（模板/编码层——§8.1；与
    /// DataInvalid 严格区分：数据本身合法而呈现层失败）。
    RenderFailed,
    /// reporting/data-invalid——数据错误（报告字段非法——§4.2 非法实例
    /// 矩阵：NaN/非有限、字段违约；与 RenderFailed 严格区分）。
    DataInvalid,
    /// reporting/archive-conflict——同目标不同内容（§7.4 幂等判定失败：
    /// 同 reportId 路径不同内容身份；建议码 RPT-ARCHIVE-CONFLICT）。
    ArchiveConflict,
    /// reporting/export-failed——外部导出失败（保留选择与路径可重试——
    /// §7.4；建议码 RPT-EXPORT-FAILED，附可重试动作）。
    ExportFailed,
    /// reporting/disk-full——磁盘不足（工件写出失败——§7.4 RP-DISK-1；
    /// abandon 后可重试）。
    DiskFull,
    /// reporting/read-only-store——只读项目拒绝项目内发布（§2.2 PM-07 行：
    /// 项目内 reports/ 发布拒绝＋导出到项目外允许）。
    ReadOnlyStore,
    /// reporting/roundtrip-mismatch——往返复算不一致（§7.7 RPT-06——
    /// 报告不可用于正式结论；建议码 RPT-ROUNDTRIP-MISMATCH）。
    RoundtripMismatch,
    /// reporting/bundle-incomplete——证据包要素缺失（§7.6 组装校验：
    /// 快照引用/结果引用/复现要素清单化拒绝）。
    BundleIncomplete,
    /// reporting/template-version——模板/章节模型版本不兼容（§4.1
    /// sectionModelVersion 与渲染模板版本的兼容性拒绝；PM-06 执行侧口径）。
    TemplateVersion,
    /// reporting/usage——调用方违约（fail-fast 码面：非法参数/违反头文件
    /// 契约前置条件——AGENTS.md 错误语义"调用方错误 fail-fast"）。
    Usage,
};

/**
 * @brief 取错误码的稳定 token（§3.5 注释列原文）。
 *
 * @param code [in] 错误码（全表 16 值均有 token——全函数，永不返回空）
 * @return 稳定 token 字符串（"reporting/..." 形态；静态存储期，调用方无需释放）
 *
 * 确定性：编译期固定 switch 全枚举表（无 default——新增枚举值未登记表项时
 * 编译器全枚举告警暴露遗漏；测试侧另有全表用例逐值钉住）；同码同串、无
 * locale 依赖（NFR-COR-02）。token 命名规则："reporting/" 前缀＋§3.5 注释列
 * 原文 kebab 串，逐值与单元卡 §3.5 清单一字不差（acceptance 2）。
 */
std::string_view token(ReportErrorCode code) noexcept;

// =====================================================================
// ReportError——reporting 唯一异常类型（§3.5 原文契约；消息前缀
// "reporting/<域>:" 面向开发诊断——evidence/runtime 同款约定）。
// =====================================================================

/**
 * @brief reporting 异常轨载体（std::runtime_error 子类，code() 访问器）。
 *
 * 值语义：按值抛出/捕获（what() 消息在基类内持有，安全）。
 * 消息前缀约定（§3.5 "detail 前缀 'reporting/<域>:'口径"）：what() ＝
 * "<token>[: <detail>]"——token（"reporting/<域>"）前缀由构造函数强制拼装
 * （无裸消息构造路径），面向开发诊断；用户可见文案与码值登记归
 * diagnostics 单元（NFR-REL-05 同源），本类不越权。
 *
 * 使用场景：构建/渲染/导出/归档各抛错点的环境与数据错误，以及调用方
 * 契约违约 fail-fast（Usage）；可恢复查询路径一律另有 try* 非抛出变体
 * （§1.4 异常行约定——本头文件"错误语义总纲"节与各接口注释共同声明）。
 *
 * 生命周期约束：仅进程内抛传，不跨进程边界（evidence D-15 同源——
 * worker 通道由 execution 错误码化；捕获后不得重抛入其他单元的异常契约）。
 *
 * 线程安全：不可变（构造后仅只读访问）；what() 与 std::runtime_error 同规。
 */
class ReportError : public std::runtime_error {
public:
    /**
     * @brief 以错误码＋细节构造；what() ＝ "<token>: <detail>"。
     * @param code   [in] 稳定错误码（全表 16 值之一）
     * @param detail [in] 开发诊断细节（就地定位信息：字段/键/路径/期望值
     *               等）；默认空串——此时 what() 恰为 token（无尾随冒号
     *               空格，避免噪音字符）。缺省参数保持 §3.5 原文双参签名
     *               的同时提供单参构造形态（evidence EV-T02 同款口径）。
     */
    ReportError(ReportErrorCode code, std::string detail = {});

    /// 稳定错误码访问器（构造后不变；建议诊断码经 suggestedDiagCode(code) 取）。
    ReportErrorCode code() const noexcept { return m_code; }

private:
    ReportErrorCode m_code;   ///< 本异常携带的稳定错误码（token 经 token(m_code) 取得）
};

// =====================================================================
// RPT-* 稳定诊断码引用登记清单（§3.5 清单——acceptance 3"码清单登记"）。
//
// 权威声明（PA-1/NFR-MNT-03——本清单为 reporting 侧**引用登记非二次定义**）：
// 码值权威＝diagnostics 单元 StableCodeRegistry，全量 8 项已于 2026-09-10
// 收编（diagnostics.md §4.5 补登 RPT 前缀、§4.6 收编清单；实现侧见
// diagnostics/src/DiagCodes.cpp 内置码表 RPT 段 8 行——P-RPT-8 码值部分
// 已消账）。本清单的字面量与 diagnostics 内置码表逐字一致，一致性由
// 契约测试（contract_test）对 StableCodeRegistry 的逐码 find 断言常驻
// 自证；若收编表后续修订，本清单按 DTB §5.4 增量同步（与收编表不一致＝
// 实现偏差，登记修订留痕而非静默跟随）。
//
// 分类/严重级别**不以本单元登记值为权威**（以 diagnostics 收编登记值为
// 准——§3.5 原文），下述注释仅转载收编表当前值供阅读定位。
// =====================================================================

namespace diagcodes {

/// RPT-SOURCE-MISSING——章节缺正式结果/缺项（收编严重级别 Error）。
inline constexpr std::string_view kSourceMissing = "RPT-SOURCE-MISSING";
/// RPT-SCOPE-INSUFFICIENT——C 级降级建议（收编严重级别 Warning）。
inline constexpr std::string_view kScopeInsufficient = "RPT-SCOPE-INSUFFICIENT";
/// RPT-CONSISTENCY-MISMATCH——多格式逐字段不一致（收编严重级别 Error）。
inline constexpr std::string_view kConsistencyMismatch = "RPT-CONSISTENCY-MISMATCH";
/// RPT-ARCHIVE-CONFLICT——同目标不同内容、幂等判定失败（收编严重级别 Error）。
inline constexpr std::string_view kArchiveConflict = "RPT-ARCHIVE-CONFLICT";
/// RPT-EXPORT-FAILED——外部导出失败，附可重试动作（收编严重级别 Error）。
inline constexpr std::string_view kExportFailed = "RPT-EXPORT-FAILED";
/// RPT-ROUNDTRIP-MISMATCH——往返复算不一致（收编严重级别 Error）。
inline constexpr std::string_view kRoundtripMismatch = "RPT-ROUNDTRIP-MISMATCH";
/// RPT-CURRENTNESS-UNEVALUABLE——当前性不可判定呈现（收编严重级别
/// Warning；P-EV-4 关联——"无法判定"不默认 Current，RP-CUR-3）。
inline constexpr std::string_view kCurrentnessUnevaluable = "RPT-CURRENTNESS-UNEVALUABLE";
/// RPT-SECTION-NOT-APPLICABLE——不适用章节显式标记（收编严重级别 Info）。
inline constexpr std::string_view kSectionNotApplicable = "RPT-SECTION-NOT-APPLICABLE";

/// 登记清单全量（8 项；登记序＝diagnostics.md §4.6 收编清单序＝§3.5 清单序）。
/// inline constexpr 数组：编译期固定、跨翻译单元同一实体（ODR 安全）。
inline constexpr std::array<std::string_view, 8> kStableCodes = {
    kSourceMissing, kScopeInsufficient, kConsistencyMismatch, kArchiveConflict,
    kExportFailed, kRoundtripMismatch, kCurrentnessUnevaluable, kSectionNotApplicable,
};

}  // namespace diagcodes

/**
 * @brief 取错误码对应的 RPT-* 建议诊断码（§3.5 清单映射——引用登记）。
 *
 * 映射面（6/16）：SourceMissing/ScopeInsufficient/ConsistencyMismatch/
 * ArchiveConflict/ExportFailed/RoundtripMismatch 六值与 RPT-* 码一一对应
 * （§3.5 清单逐值注释）。其余 10 值无对应 RPT 诊断码——它们是构建/渲染/
 * 导出过程中的调用方或过程性错误（SourceAmbiguous/LevelConflict/
 * EvidenceRefInvalid/RenderFailed/DataInvalid/DiskFull/ReadOnlyStore/
 * BundleIncomplete/TemplateVersion/Usage），不进入报告诊断主轴（正式
 * 上报形态随各抛错点任务落地登记）。
 *
 * 另两枚登记码（diagcodes::kCurrentnessUnevaluable/kSectionNotApplicable）
 * 是**呈现类诊断**而非异常码面：当前性不可判定与不适用章节由构建器/
 * 渲染器以诊断条目上报（RP-CUR-3/§5.3），不抛 ReportError——故不入本映射。
 *
 * @param code [in] 错误码
 * @return 对应 RPT-* 建议码（"RPT-*" 形态的静态存储期字面量）；无对应→
 *         nullopt。正式码值以 diagnostics StableCodeRegistry 注册为准，
 *         本函数不承担注册职责（PA-1）。
 */
std::optional<std::string_view> suggestedDiagCode(ReportErrorCode code) noexcept;

}  // namespace sdurws::ird::reporting

#endif  // SDURWS_IRD_REPORTING_ERRORS_HPP
