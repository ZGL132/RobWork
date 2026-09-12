/**
 * @file   Errors.hpp
 * @brief  evidence 错误契约——EvidenceErrorCode 全表（稳定 token）＋EvidenceError。
 *
 * 设计依据：
 *   - units/evidence.md §3.1 组成表（`Errors.hpp`｜EvidenceErrorCode（稳定
 *     token）、EvidenceError）；§2.1 异常行（evidence 用自有异常类型
 *     EvidenceError；可恢复查询路径提供 try* 非抛出变体）
 *   - 注意（F-020 登记在案）：§12 EV-T02 行引用的"§3.5"在当前设计文本中无
 *     独立章节——错误语义承载于 §3.1 组成表＋各抛错点原文（§4.1.5④b、
 *     §9.4 等）＋§13 diagnostics 交接行的建议码清单。本头逐项锚定这些
 *     原文位置，未新增/收窄任何错误语义；§3.5 是否增设由 evidence 详设
 *     所有者裁决（findings F-020，open）
 *   - §13 diagnostics 行：EvidenceError 的稳定 token 与建议诊断码
 *     （EVI-SNAPSHOT-INCOMPLETE/EVI-CASE-COVERAGE-MISSING/EVI-EVIDENCE-MISSING/
 *     EVI-PROOF-INVALID/EVI-ENVELOPE-ILLEGAL-COMBINATION/EVI-CACHE-INCOMPATIBLE/
 *     EVI-EVALUATOR-DUPLICATE 等——**建议值，码值权威归 StableCodeRegistry**，
 *     P-PR-6 同模式）；需求 ERR-01（诊断记录字段）、NFR-COR-03（不静默吞错）
 *   - 任务契约 tasks/foundation/EV-T02.json（≙WP-05-T02）acceptance 2：
 *     EvidenceErrorCode 全表 token 用例通过（稳定 token 逐枚举）
 *
 * 背景说明（为什么 evidence 自有一套错误码，而不直接复用 core::CoreError）：
 *   core 的错误契约（CoreError）只覆盖 core 自身抛错路径（§3.2 消费清单：
 *   "core 抛错捕获与转发"——evidence 捕获 CoreError 后按上下文转译为本表
 *   码面，不向上游泄漏 core 类型）；evidence 的错误面横跨快照组装（§4.1.5）、
 *   依赖声明（§4.2.3）、证据/证明/覆盖（§6）、结果包络（§7）、缓存/检查点
 *   兼容（§8）与评估器注册（§9.4），各有独立归属与恢复路径——与 runtime
 *   错误契约（其 §3.4）同一模式：枚举全表＋稳定 token，异常轨与后续任务
 *   的判定/投影共用同一码面。码值权威＝diagnostics 单元的 StableCodeRegistry
 *   （尚未落地——diagnostics 现为骨架），本单元只产出 token/建议码，不注册
 *   码值（PA-1 权威唯一）。
 *
 * 错误语义总纲（AGENTS.md 错误语义在本单元的落点）：
 *   - 调用方契约违约（如对已冻结切片的修改请求、非法声明注册）＝fail-fast
 *     （抛 EvidenceError），不走诊断收集；
 *   - 环境错误（载荷摘要不符等）同样以 EvidenceError 表达但携带可定位的
 *     detail——NFR-COR-03：任何失败都不得静默转换为默认通过；
 *   - D-15：EvidenceError 不跨进程边界（通道层由 execution 错误码化）。
 *
 * 线程安全：本头全部实体为纯值/纯函数（无共享可变状态），并发只读安全。
 * 确定性：token/建议码为编译期固定表（switch 全枚举），同码同串、跨平台
 * 跨进程逐字节一致（NFR-COR-02 的码面子集）。
 */

#ifndef SDURWS_IRD_EVIDENCE_ERRORS_HPP
#define SDURWS_IRD_EVIDENCE_ERRORS_HPP

#include <stdexcept>
#include <string>
#include <string_view>

namespace sdurws::ird::evidence {

// =====================================================================
// EvidenceErrorCode——evidence 全量稳定错误码枚举（10 值；v0.5 表尾追加
// SliceIncomplete——EV-T04 冻结拒绝码面）。
// 枚举顺序＝单元章节流（§4 快照 → §4.2 声明 → §6 证据/证明/覆盖 →
// §7 包络 → §8 缓存 → §9 注册表）。顺序与数值一经交付不得改动/插入——
// 持久化于诊断与报告的 token 虽为字符串，但枚举数值进入二进制契约面，
// 稳定第一（runtime 同款纪律）；后续任务需要新码时只能**追加表尾**并在
// 单元卡增量修订留痕（DTB §5.4）。
// =====================================================================

/**
 * @brief evidence 稳定错误码全表（§3.1 组成表＋各抛错点原文）。
 *
 * token 稳定（持久化于诊断/报告）；建议码值归 diagnostics StableCodeRegistry
 * （§13——P-PR-6 同模式，映射见 registryCode()）。每值的产生位置见注释
 * （章节号＝units/evidence.md 原文锚点）。
 */
enum class EvidenceErrorCode : std::uint8_t {
    /// evidence/snapshot-incomplete——快照非法实例/builder 即时验证失败
    /// （§4.1.5④a：对象引用不在修订闭包内（防混入）、policyRef/nameMapRef
    /// 内容身份为空（CON-06）、工况重复、采样参数非负、外部资源状态机非法、
    /// Verified 前置校验存在 state==Recorded 的被消费外部资源（CON-03））。
    SnapshotIncomplete,
    /// evidence/snapshot-integrity——载荷级校验失败：物化对象字节重算
    /// SHA-256 与 contentVersion 不符（§4.1.5④b——检测传输/磁盘损坏，
    /// NFR-COR-03 不静默通过）。
    SnapshotIntegrity,
    /// evidence/declaration-invalid——依赖声明语法/结构/闭包校验拒绝
    /// （§4.2.3① 注册期："声明语法、kind 载荷合法、Conditional 条目的
    /// referencedKeys ⊆ 同一 descriptor 已声明键 ∪ 快照事实键——闭包规则，
    /// 防漏声明"；D-10：条件输入未入切片的声明在注册期即被拒绝）。
    /// 校验器本体见 Dependency.hpp（validateDependencyDeclarations）。
    DeclarationInvalid,
    /// evidence/evidence-missing——必需证据项缺失（§6.2 证据清单对
    /// RequiredEvidenceProfile 的完备性核对；§13 建议码 EVI-EVIDENCE-MISSING）。
    EvidenceMissing,
    /// evidence/proof-invalid——确定性不可行证明字段级校验失败
    /// （§6.3 证明契约；EV-VER-6 反例：缺 mandatoryState 的证明→Invalid）。
    ProofInvalid,
    /// evidence/case-coverage-missing——必验工况覆盖缺失（§6.6 覆盖矩阵；
    /// EVI-02/EV-COV-1：漏验工况拦截，包络不替代覆盖）。
    CaseCoverageMissing,
    /// evidence/envelope-illegal-combination——ResultEnvelope 非法组合
    /// （§7.1 构造边界/SA-13/D-08：唯一入口 make()，非法组合抛本码；
    /// EV-ENV-1"错误码逐条对应表 3"的逐条区分在 EV-T07 落地）。
    EnvelopeIllegalCombination,
    /// evidence/cache-incompatible——缓存/检查点兼容判定拒绝（§8.2；
    /// EV-CPA-1 Quick≠Verified、EV-CPA-2 契约版本——判定输出供调用方转译）。
    CacheIncompatible,
    /// evidence/duplicate-evaluator——同 evaluationKey 重复注册
    /// （§9.4：注册边界拒绝，**不覆盖、不静默**；token 取 §9.4 原文
    /// "duplicate-evaluator"）。
    EvaluatorDuplicate,
    /// evidence/slice-incomplete——切片非法实例/SliceBuilder 冻结拒绝
    /// （§4.2.3② 冻结期：条目语法/载荷非法、(kind,key) 重复、Object 条目
    /// 不在来源快照闭包内（子集校验，防漏声明错配）、CR-05 保留 token
    /// 值形态非法或必填缺失、来源快照未冻结、评估键语法非法——EV-T04
    /// 任务卡登记的冻结拒绝码面；SliceCodec 编解码的结构性非法同码面）。
    /// 表尾追加（v0.5，EV-T04）——建议码为补登值（P-PR-6 同模式）。
    SliceIncomplete,
};

/**
 * @brief 取错误码的稳定 token（注释列原文）。
 *
 * @param code [in] 错误码（全枚举 10 值均有 token——全函数，永不返回空）
 * @return 稳定 token 字符串（"evidence/..." 形态；静态存储期，调用方无需释放）
 *
 * 确定性：编译期固定 switch 全枚举表（无 default——新增枚举值未登记表项时
 * 编译器告警暴露遗漏；测试侧另有全表用例逐值钉住）；同码同串、无 locale
 * 依赖（NFR-COR-02）。token 命名规则："evidence/" 前缀＋设计原文称名
 * （§4.1.5④b 原文 "snapshot-integrity"、§9.4 原文 "duplicate-evaluator"；
 * 其余值按同一 kebab 规则自 §13 建议码派生——派生关系登记于单元卡 v0.3
 * 变更记录，F-056 转 owners 确认）。
 */
std::string_view token(EvidenceErrorCode code) noexcept;

/**
 * @brief 取错误码对应的 diagnostics 建议注册码（§13 建议码清单）。
 *
 * 冻结关系（§13 原文）：清单列出 7 个 EVI-* 建议码并以"等"收尾——本表
 * 10 值中 7 值与 §13 逐字对应；另 3 值（SnapshotIntegrity/DeclarationInvalid/
 * SliceIncomplete）的错误语义在设计文本中有明文抛错点（§4.1.5④b/§4.2.3①/
 * §4.2.3② 冻结期）但 §13 未给出建议码，本头按 P-PR-6 同模式补登建议值
 * EVI-SNAPSHOT-INTEGRITY/EVI-DECLARATION-INVALID/EVI-SLICE-INCOMPLETE
 * （**建议值**——码值权威归 StableCodeRegistry，
 * 是否收编由 diagnostics 所有者与 evidence 详设所有者裁决，登记 F-056）。
 *
 * @param code [in] 错误码
 * @return 建议注册码（"EVI-*" 形态——全部 10 值均发建议码；正式码值以
 *         diagnostics 注册为准，本函数不承担注册职责——PA-1）
 */
std::string_view registryCode(EvidenceErrorCode code) noexcept;

// =====================================================================
// EvidenceError——evidence 唯一异常类型（§2.1 异常行：自有异常类型；
// 消息前缀 "evidence/..." 面向开发诊断——runtime 同款约定）。
// =====================================================================

/**
 * @brief evidence 异常轨载体（std::runtime_error 子类，code() 访问器）。
 *
 * 值语义：按值抛出/捕获（what() 消息在基类内持有，安全）。
 * 消息前缀约定：what() ＝ "<token>[: <detail>]"——token 前缀由构造函数
 * 强制拼装（无裸消息构造路径），面向开发诊断；用户可见文案与码值登记归
 * diagnostics 单元（NFR-REL-05 同源），本类不越权。
 *
 * 使用场景：构造期硬失败（快照 builder 拒绝、切片冻结拒绝、envelope
 * make() 非法组合）与调用方契约违约 fail-fast（重复注册、声明闭包拒绝）；
 * 可恢复查询路径一律另有 try* 非抛出变体或判定函数（§2.1 异常行——如
 * §8.2 判定契约返回结构化结果而非抛错的同款分轨）。
 *
 * 生命周期约束（D-15）：仅进程内抛传，不跨进程边界——worker 通道由
 * execution 错误码化；捕获后不得重抛入其他单元的异常契约。
 *
 * 线程安全：不可变（构造后仅只读访问）；what() 与 std::runtime_error 同规。
 */
class EvidenceError : public std::runtime_error {
public:
    /**
     * @brief 以错误码＋细节构造；what() ＝ "<token>: <detail>"。
     * @param code   [in] 稳定错误码（全表 10 值之一）
     * @param detail [in] 开发诊断细节（就地定位信息：字段/键/条目下标等）；
     *               空串合法——此时 what() 恰为 token（无尾随冒号空格）
     */
    EvidenceError(EvidenceErrorCode code, std::string detail);

    /**
     * @brief 以错误码构造（无细节）；what() ＝ token。
     * @param code [in] 稳定错误码
     */
    explicit EvidenceError(EvidenceErrorCode code)
        : EvidenceError(code, std::string{})
    {
    }

    /// 稳定错误码访问器（构造后不变；建议码经 registryCode(code) 取）。
    EvidenceErrorCode code() const noexcept { return m_code; }

private:
    EvidenceErrorCode m_code;   ///< 本异常携带的稳定错误码（token 经 token(m_code) 取得）
};

}  // namespace sdurws::ird::evidence

#endif  // SDURWS_IRD_EVIDENCE_ERRORS_HPP
