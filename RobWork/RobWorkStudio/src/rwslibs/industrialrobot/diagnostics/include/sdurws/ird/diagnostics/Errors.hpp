/**
 * @file   Errors.hpp
 * @brief  diagnostics 错误契约——DiagnosticsErrorCode 全表（稳定 token）＋DiagnosticsError。
 *
 * 设计依据：
 *   - units/diagnostics.md §9.0（错误类型：枚举全表 11 值＋`DiagnosticsError :
 *     std::runtime_error`，逐值 token 注释原文；"不抛诊断、不吞异常"总纲）
 *   - 需求 ERR-01（诊断字段）、NFR-MNT-03（码/文案单一权威——重复定义注册
 *     边界拒绝的异常载体）
 *   - 任务契约 tasks/foundation/DIAG-T03.json（≙WP-09-T03）outputs 首项
 *     （`Errors.*`——§11 DIAG-T03 行产物定义）
 *
 * 背景说明（为什么 diagnostics 自有一套错误码，而不复用 core::CoreError）：
 *   core 的 CoreError 只覆盖 core 自身抛错路径；diagnostics 的错误面横跨
 *   稳定码注册表（§4.5/§9.1）、目录与工厂（§9.2/§9.7，随 DIAG-T04 消费）、
 *   确认服务状态机（§5.4/§9.3，随 DIAG-T05 消费）与绑定复核（§5.3）——
 *   与 evidence::EvidenceError 同一模式（单元自有异常类型＋枚举全表＋稳定
 *   token，异常轨与后续任务的判定/转译共用同一码面）。稳定诊断码（PRJ-* 等）
 *   的**码值**权威＝本单元 StableCodeRegistry（§4.6），与本表的**单元错误码**
 *   （diagnostics/*——设施自身的失败语义）是两个层面：后者面向开发诊断，
 *   不作为诊断码对外呈现（§9.0 枚举注释原文）。
 *
 * 错误语义总纲（AGENTS.md 错误语义在本单元的落点，§9.0 尾段原文）：
 *   - 调用方契约违约（重复注册、未注册码构造、非法状态转移、空参数等）
 *     ＝ fail-fast（抛 DiagnosticsError），不走诊断收集；
 *   - "不抛诊断、不吞异常"：本单元设施不向调用方抛出"作为诊断语义"的
 *     异常（诊断经返回值/sink 传递）；调用方异常穿越本单元设施时不捕获
 *     不包装（透传——错误转换是显式调用的行为，不是隐式 catch）。
 *
 * 线程安全：本头全部实体为纯值/纯函数（无共享可变状态），并发只读安全。
 * 确定性：token 为编译期固定表（switch 全枚举），同码同串、跨平台跨进程
 * 逐字节一致（NFR-COR-02 的码面子集）。
 */

#ifndef SDURWS_IRD_DIAGNOSTICS_ERRORS_HPP
#define SDURWS_IRD_DIAGNOSTICS_ERRORS_HPP

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace sdurws::ird::diagnostics {

// =====================================================================
// DiagnosticsErrorCode——diagnostics 设施全量稳定错误码枚举（§9.0 原文
// 11 值，不增不删）。枚举顺序＝§9.0 声明序（持久化契约面纪律：数值进入
// 二进制契约，只允许表尾追加并走单元卡增量修订——evidence::EvidenceError
// 同款，DTB §5.4）。每值的产生位置见注释（章节号＝units/diagnostics.md）。
// =====================================================================

/**
 * @brief diagnostics 设施稳定错误码全表（§9.0 原文 11 值）。
 *
 * token 稳定（"内部错误语义，不对外呈现为诊断码之外的文本"——§9.0 枚举
 * 注释原文；token 清单见 token() 每值注释）。注意与稳定诊断码（§4.6 的
 * DIAG-REGISTRY-DUPLICATE 等）的分工：本表是**设施异常轨**的码面，诊断码
 * 是**诊断内容**的码值——设施失败先抛本表异常，是否落为 DIAG-* 诊断条目
 * 由捕获方按 §8 各转换点显式决定，不在异常类型内隐式生成诊断。
 */
enum class DiagnosticsErrorCode : std::uint8_t {
    /// diagnostics/duplicate-code——重复注册/重复转换规则（§4.5 注册边界：
    /// 同 code 再次注册拒绝，不覆盖、不静默——NFR-MNT-03；§9.2 同错误类型
    /// 重复登记转换规则同码面——随 DIAG-T04 消费）。
    DuplicateCode,
    /// diagnostics/code-unknown——未注册码（§4.5"工厂以未注册码构造 → 拒绝"；
    /// §9.1 注册期"前缀冲突"同码面：首段前缀与 ownerUnit 声明域不符＝
    /// 该码不在其声称的所有权域内——跨前缀注册拒绝）。
    CodeUnknown,
    /// diagnostics/code-deprecated——废弃码构造（§4.5.1：tombstone 保留
    /// 只读映射，但工厂拒绝以 deprecated 码构造新实例——"废弃码只读映射
    /// 不改写历史"（PA-2）的构造侧闸门，随 DIAG-T04 工厂消费）。
    CodeDeprecated,
    /// diagnostics/subject-missing——用户级码缺 subject（§9.2 工厂前置：
    /// 用户级码 subject 合法；Dev 码可空——§4.2 信封字段边界，随 DIAG-T04）。
    SubjectMissing,
    /// diagnostics/comparison-missing——比较型码缺三要素（ERR-01/UX-03/
    /// core C-1 强化：requiresComparison 的码实例 comparison 必须完整，
    /// 随 DIAG-T04）。
    ComparisonMissing,
    /// diagnostics/category-mismatch——非法分类/严重注入路径（§4.3 传播
    /// 规则：severity 归属码表不随实例变化——绕过码表注入分类＝违约）。
    CategoryMismatch,
    /// diagnostics/param-schema-mismatch——参数模式不符（§4.5 注册期验证：
    /// paramSchema 非法/参数名非法；§9.2 实例 context/cause/comparison
    /// 占位与模式不一致同码面）。
    ParamSchemaMismatch,
    /// diagnostics/context-missing——来源码必填上下文缺失（§9.2 工厂前置：
    /// execution→task；评估路径→snapshot/slice；命令路径→command/revision，
    /// 随 DIAG-T04）。
    ContextMissing,
    /// diagnostics/invalid-state——finding 状态机非法转移（§5.4 五态服务
    /// 状态机的转移闸门，随 DIAG-T05 消费）。
    InvalidState,
    /// diagnostics/binding-mismatch——绑定四元组复核失败（§5.3：确认提交
    /// 与编译前复核，四者任一不符即失效，随 DIAG-T05 消费）。
    BindingMismatch,
    /// diagnostics/usage——调用方违约（空参数等；§9.1"运行期 registerCode"
    /// 等时机违约同码面——装配期契约在运行期被打破）。
    Usage,
};

/**
 * @brief 取错误码的稳定 token（§9.0 逐值注释原文）。
 *
 * @param code [in] 错误码（全表 11 值均有 token——switch 全枚举，无 default，
 *              新增枚举值未登记表项时编译器告警暴露遗漏）
 * @return 稳定 token（"diagnostics/..." 形态；静态存储期，调用方无需释放）
 *
 * 确定性：编译期固定表，同码同串、无 locale 依赖（NFR-COR-02）。token 即
 * §9.0 注释列的 "diagnostics/<域>"——DiagnosticsError::what() 的前缀。
 */
std::string_view token(DiagnosticsErrorCode code) noexcept;

// =====================================================================
// DiagnosticsError——diagnostics 唯一异常类型（§9.0 原文契约）。
// =====================================================================

/**
 * @brief diagnostics 异常轨载体（std::runtime_error 子类，code() 访问器）。
 *
 * 值语义：按值抛出/捕获（what() 消息在基类内持有，安全）。
 * 消息前缀约定（§9.0 code() 注释原文"detail 前缀 diagnostics/<域>:，面向
 * 开发诊断"）：what() ＝ "<token>: <detail>"——token 由构造函数强制拼装
 * （无裸消息构造路径），用户可见文案与诊断码登记分属码表/文案资源
 * （P-DIAG-9 键值分离），本类不越权生成文案。
 *
 * 使用场景：调用方契约违约 fail-fast（重复注册 NFR-MNT-03、未注册/废弃码
 * 构造、参数模式不符、状态机非法转移）与装配期验证拒绝（§4.5 注册期验证
 * 表——任一失败即拒绝并指明字段，detail 携带字段名与原因）。
 *
 * 生命周期约束：仅进程内抛传（evidence D-15 同款纪律）；捕获后不得重抛入
 * 其他单元的异常契约（CR-08：跨单元错误只经已登记类型映射转译——§8.1）。
 *
 * 线程安全：不可变（构造后仅只读访问）；what() 与 std::runtime_error 同规。
 */
class DiagnosticsError : public std::runtime_error {
public:
    /**
     * @brief 以错误码＋细节构造（§9.0 原签名）；what() ＝ "<token>: <detail>"。
     * @param code   [in] 稳定错误码（全表 11 值之一）
     * @param detail [in] 开发诊断细节（就地定位信息：字段名/码值/条目键等；
     *               空串合法——此时 what() 恰为 token，无尾随冒号空格）
     */
    DiagnosticsError(DiagnosticsErrorCode code, std::string detail);

    /// 稳定错误码访问器（构造后不变）。
    DiagnosticsErrorCode code() const noexcept { return m_code; }

private:
    DiagnosticsErrorCode m_code;   ///< 本异常携带的稳定错误码（token 经 token(m_code) 取得）
};

}  // namespace sdurws::ird::diagnostics

#endif  // SDURWS_IRD_DIAGNOSTICS_ERRORS_HPP
