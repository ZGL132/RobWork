/**
 * @file   Errors.hpp
 * @brief  runtime 错误契约——RuntimeErrorCode 全表＋RuntimeError＋Expected<T,E>
 *         ＋名称解析两错误值类型（RuntimeResolveError/RuntimeNameError）。
 *
 * 设计依据：
 *   - units/runtime.md §3.4（错误类型与 CMake 集成——枚举全表/异常类型/
 *     Expected 非抛出查询轨的原文契约）、§3.1（Errors.hpp 模块清单——
 *     RuntimeResolveError/RuntimeNameError 同置本头，形态见 §7.4）
 *   - units/runtime.md §10.11（与 diagnostics：枚举↔稳定码关系 v0.3 冻结——
 *     "12 个错误码 1:1＋2 个 fail-fast token＋2 个事件码"）、§5.2（十段编译
 *     逐步表——各错误码的产生段与归属）、§5.6（输入非法/能力缺失/编译失败
 *     正交关系——错误分类语义）、§8.4（RobWork 异常转译——robwork-error
 *     事件路径）
 *   - 需求 NFR-COR-03（非有限/非法单位/引用缺失不静默——两态不吞错的
 *     需求源头）；任务契约 tasks/foundation/RT-T02.json（acceptance 1/3）
 *
 * 背景说明（为什么 runtime 自带一套错误码，而不直接复用 core::CoreError）：
 *   core 的错误契约（CoreError）只覆盖 core 自身抛错路径（§3.2 消费清单：
 *   "core 抛错捕获与转发"）；runtime 十段编译链的错误面远大于此——输入校验、
 *   资源、WC/DWC 编译、名称、缓存、取消各有独立归属与恢复路径。故本头
 *   冻结 15 个枚举值（每值一个稳定 token），作为 CompileOutcome.diagnostics
 *   与异常轨（RuntimeError）共用的码面。码值权威＝diagnostics 单元的
 *   StableCodeRegistry（本单元只产出 token/建议码，不注册码值——PA-1）。
 *
 * 错误语义总纲（AGENTS.md 错误语义在本单元的落点）：
 *   - 调用方契约违约（如对已释放上下文的操作、Expected 前置违约）＝fail-fast
 *     （抛 RuntimeError/ std::logic_error），不走诊断收集；
 *   - 环境错误（资源缺失、RobWork 异常等）＝经 CompileOutcome.diagnostics
 *     稳定码登记上报（后续任务 RT-T11 落地），本头提供其码面与分类。
 *
 * 线程安全：本头全部实体为纯值/纯函数（无共享可变状态），并发只读安全。
 * 确定性：token/稳定码/分类为编译期固定表（switch 全枚举），同码同串、
 * 跨平台跨进程逐字节一致（NFR-COR-02 的码面子集）。
 */

#ifndef SDURWS_IRD_RUNTIME_ERRORS_HPP
#define SDURWS_IRD_RUNTIME_ERRORS_HPP

#include <sdurws/ird/core/Identity.hpp>

#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace sdurws::ird::runtime {

// =====================================================================
// RuntimeErrorCode——runtime 全量稳定错误码枚举（§3.4 原文契约，15 值）。
// 枚举顺序即 §3.4 声明顺序；token 注释列＝每值的稳定诊断码字符串
// （经 token() 取得）。顺序与数值一经交付不得改动/插入——持久化于诊断与
// 报告的 token 虽为字符串，但枚举数值进入二进制契约面，稳定第一。
// =====================================================================

/**
 * @brief runtime 稳定错误码全表（§3.4）。
 *
 * token 稳定（持久化于诊断/报告）；建议码值归 diagnostics StableCodeRegistry
 * （§10.11 已收编 14 项——映射见 registryCode()）。
 * 每值的产生段与归属见注释（段号＝§5.2 十段编译逐步表）。
 */
enum class RuntimeErrorCode {
    InputInvalid,          // runtime/input-invalid          输入非法（结构/数值/引用，就地定位；S2 reader 链/S3 校验/S5 构造不变量/S6 T_world_base 非法旋转）
    UnitMismatch,          // runtime/unit-mismatch          单位/量纲非法（S3 单位校验）
    StructureInvalid,      // runtime/structure-invalid      链结构非法（环/断链/重复身份；S3；S2 闭包防混入 CM-0——RT-CONT-1）
    ResourceMissing,       // runtime/resource-missing       资源缺失（io 已诊断，此处定位 resourceId；S4）
    ResourceChanged,       // runtime/resource-changed       编译期间资源内容变化（§8.6 复查失败；S4/S10 前复查）
    ResourceBudget,        // runtime/resource-budget        资源预算超限（S4；std::bad_alloc 转译同码——§5.5）
    WorkCellCompileFailed, // runtime/wc-compile-failed      WorkCell 编译硬失败（含 RobWork 异常转译；S6——RT-AD-1）
    DwcCompileFailed,      // runtime/dwc-compile-failed     DynamicWorkCell 编译硬失败（物性已提供但构造失败；S7——注意与能力缺失降级严格区分，§5.6）
    NameConflict,          // runtime/name-conflict          名称冲突无法消歧/非法字符无法合法化（S8，含 WC 对象名交叉校验不一致）
    BaseWorldInconsistent, // runtime/base-world-inconsistent 基座—世界一致性检查失败（S9——实现缺陷类失败，不得放行）
    CacheIncompatible,     // runtime/cache-incompatible     缓存兼容判定拒绝（供调用方转译；§9.4 判定输出）
    Cancelled,             // runtime/cancelled              协作取消（非错误路径，UX-03/D-11——取消诊断非 error 级）
    UnknownObject,         // runtime/unknown-object         解析目标不在本快照（S1 修订不存在/§7.4 名称查询未命中）
    ContextReleased,       // runtime/context-released       快照/编译上下文已释放（S1 上下文已关闭/§9.3 迟到请求）
    RobWorkError,          // runtime/robwork-error          RobWork 基线异常/空句柄/无效层级（§8.4 转译路径的事件码面）
};

/**
 * @brief 取错误码的稳定 token（§3.4 注释列）。
 *
 * @param code [in] 错误码（全枚举 15 值均有 token——全函数，永不返回空）
 * @return 稳定 token 字符串（"runtime/..." 形态；静态存储期，调用方无需释放）
 *
 * 确定性：编译期固定 switch 全枚举表（新增枚举值未登记时编译期不可达值
 * 返回空串并由测试全表钉住——见 ErrorsTest 全表用例）；同码同串、无 locale
 * 依赖（NFR-COR-02）。
 */
std::string_view token(RuntimeErrorCode code) noexcept;

/**
 * @brief 取错误码对应的 diagnostics 稳定注册码（§10.11 v0.3 冻结关系）。
 *
 * 冻结关系（§10.11 原文——"12 个错误码 1:1＋2 个 fail-fast token＋2 个事件码"）：
 *   - 12 个错误码 1:1 映射 RT-* 注册码（如 InputInvalid→"RT-INPUT-INVALID"）；
 *   - UnknownObject/ContextReleased 属调用方契约违约（RuntimeError fail-fast，
 *     token 仅供日志，不发稳定码）→ 返回空；
 *   - RobWorkError 的 "RT-ROBWORK-ERROR" 为诊断事件码（编译诊断/RobWork 异常
 *     转译路径——§8.4 translateRobWorkError，非 RuntimeError 异常携带码）→
 *     返回空（事件码的产出与登记归 RT-T07/T08/T11 转译点）。
 *
 * @param code [in] 错误码
 * @return 稳定注册码（"RT-*" 形态）或空串（上述三值——空即"本码不走
 *         StableCodeRegistry 注册路径"，不是未知码；码值权威＝diagnostics）
 */
std::string_view registryCode(RuntimeErrorCode code) noexcept;

/**
 * @brief 错误分类轴（§5.2 产生段归属＋§5.6 正交关系的码面投影）。
 *
 * 背景说明：§5.6 把编译失败面切成正交三轴（输入非法/能力缺失/编译失败），
 * 其中"能力缺失"不是错误（警告级降级，无枚举值——RT-CAPABILITY-MISSING
 * 为事件码）；本分类只覆盖错误/取消码面，按产生段归类以支撑"错误归属
 * 可定位"（acceptance 2）与诊断聚合。
 */
enum class RuntimeErrorCategory {
    Input,       ///< 输入非法类（S2/S3/S5/S6 局部）：InputInvalid/UnitMismatch/StructureInvalid——建模侧修复后重编译可恢复
    Resource,    ///< 资源类（S4）：ResourceMissing/ResourceChanged/ResourceBudget——重关联或固化后可恢复（PM-09/CON-03）
    Compile,     ///< 编译硬失败类（S6/S7）：WorkCellCompileFailed/DwcCompileFailed——同输入重试结果一致（确定性）
    Name,        ///< 名称类（S8）：NameConflict——消歧规则无法收敛或交叉校验不一致
    Consistency, ///< 一致性类（S9）：BaseWorldInconsistent——实现缺陷类，不可恢复，须修复编译器不得放行
    Cache,       ///< 缓存判定类：CacheIncompatible——非编译失败，供调用方转译（§9.4）
    Context,     ///< 上下文/解析类：UnknownObject/ContextReleased——调用方契约违约（fail-fast 轨，不发稳定码）
    Baseline,    ///< 基线适配类：RobWorkError——RobWork 基线异常/空句柄/无效层级（§8.4 事件路径）
    Cancelled    ///< 取消（非错误路径）：Cancelled——UX-03 正常取消不产错误诊断（D-11）
};

/**
 * @brief 取错误码的分类（§5.2/§5.6 归属表）。
 * @param code [in] 错误码（全枚举全覆盖）
 * @return 分类值；逐值映射见 Errors.cpp 的固定表（测试全表钉住）
 */
RuntimeErrorCategory category(RuntimeErrorCode code) noexcept;

/**
 * @brief 是否取消码（Cancelled 判别——取消是正常控制流，非错误路径）。
 *
 * 用途：诊断分级（取消诊断非 error 级，§5.6 组合表）与 CompileOutcome
 * 取消分支判别（RT-T11）。除 Cancelled 外恒 false。
 */
bool isCancellation(RuntimeErrorCode code) noexcept;

/**
 * @brief 是否调用方契约违约码（fail-fast 轨判别）。
 *
 * 冻结清单（§10.11 v0.3）：仅 UnknownObject/ContextReleased——二者属调用方
 * 契约违约（对不存在对象的查询、对已释放上下文的操作），处理方式是
 * fail-fast（异常/断言）而非诊断收集，token 仅供日志、不发稳定码。
 * 其余 13 值（含 Cancelled——取消是正常路径不是违约）恒 false。
 */
bool isContractViolation(RuntimeErrorCode code) noexcept;

// =====================================================================
// RuntimeError——runtime 唯一异常类型（§3.4：携稳定 token、消息前缀
// "runtime/..." 面向开发诊断）。
// =====================================================================

/**
 * @brief runtime 异常轨载体（std::runtime_error 子类，code() 访问器）。
 *
 * 值语义：按值抛出/捕获（what() 消息在基类内持有，安全）。
 * 消息前缀约定（§3.4）：what() ＝ "<token>[: <detail>]"——token 前缀由
 * 构造函数强制拼装（无裸消息构造路径），面向开发诊断；用户可见文案与
 * 码值登记归 diagnostics 单元（NFR-REL-05），本类不越权。
 *
 * 使用场景：构造期硬失败（如 buildRuntimeNameMap 消歧失败→NameConflict，
 * §7.4 接口属性表）与调用方契约违约 fail-fast（UnknownObject/ContextReleased
 * 的异常轨——§10.11）；可恢复查询路径一律走 Expected 非抛出轨（见下）。
 *
 * 线程安全：不可变（构造后仅只读访问）；what() 与 std::runtime_error 同规。
 */
class RuntimeError : public std::runtime_error {
public:
    /**
     * @brief 以错误码＋细节构造；what() ＝ "<token>: <detail>"。
     * @param code   [in] 稳定错误码（全表 15 值之一）
     * @param detail [in] 开发诊断细节（就地定位信息：对象/字段/名称等）；
     *               空串合法——此时 what() 恰为 token（无尾随冒号空格）
     */
    RuntimeError(RuntimeErrorCode code, std::string detail);

    /**
     * @brief 以错误码构造（无细节）；what() ＝ token。
     * @param code [in] 稳定错误码
     */
    explicit RuntimeError(RuntimeErrorCode code)
        : RuntimeError(code, std::string{})
    {
    }

    /// 稳定错误码访问器（构造后不变）。
    RuntimeErrorCode code() const noexcept { return m_code; }

private:
    RuntimeErrorCode m_code;   ///< 本异常携带的稳定错误码（token 经 token(m_code) 取得）
};

// =====================================================================
// Expected<T,E>——非抛出查询轨的值语义结果（§3.4：C++17 无 std::expected，
// 故本单元自持最小实现；查询路径"一律提供 Expected 变体"）。
// =====================================================================

/**
 * @brief 成功/失败两态的值语义结果（§3.4 原文契约）。
 *
 * 语义（acceptance 3——成功/失败两态不静默吞错，NFR-COR-03）：
 *   - 恰持有一侧：ok 态持 T、err 态持 E（std::variant 承载，构造后不变）；
 *   - ok()/error() 是带前置的访问器：对错误侧调 get()（或对成功侧调
 *     error()）属调用方契约违约——抛 std::logic_error fail-fast，
 *     绝不返回默认 T/E 静默吞错（默认值会伪装成合法结果，正是
 *     NFR-COR-03 禁止的"静默转换为 0 或默认通过"）；
 *   - 工厂 ok()/err() 为唯一构造入口（variant 成员公开仅为契约原文形态，
 *     调用方应经工厂构造以保证两态判别可靠）。
 *
 * @tparam T 成功值类型（值语义；支持移动-only 类型）
 * @tparam E 错误值类型（如 RuntimeResolveError——§7.4 查询接口签名）
 *
 * 线程安全：纯值类型（无共享状态）；const 访问器并发只读安全。
 */
template <class T, class E>
struct Expected {
    /// 两态载体：index 0＝成功（T）、index 1＝错误（E）。
    std::variant<T, E> value;

    /// 是否成功态（noexcept 纯判别；true ⇒ get() 可用，error() 不可用）。
    bool ok() const noexcept { return value.index() == 0; }

    /**
     * @brief 取成功值（前置 ok()）。
     * @return 成功值的 const 引用（生命周期随本对象）
     * @throws std::logic_error 前置违约（对错误态取值＝调用方契约违约，
     *          fail-fast 而非返回默认值——见类注释）
     */
    const T& get() const
    {
        if (!ok()) {
            // 前置违约 fail-fast：消息用稳定前缀（runtime/expected/...），
            // 与 RuntimeError 消息前缀约定同风格，便于日志检索。
            throw std::logic_error("runtime/expected/get-on-error: 对错误态调用 get()");
        }
        return std::get<0>(value);
    }

    /**
     * @brief 取错误值（前置 !ok()）。
     * @return 错误值的 const 引用（生命周期随本对象）
     * @throws std::logic_error 前置违约（对成功态取错误＝调用方契约违约）
     */
    const E& error() const
    {
        if (ok()) {
            throw std::logic_error("runtime/expected/error-on-ok: 对成功态调用 error()");
        }
        return std::get<1>(value);
    }

    /// 成功态工厂（移动入构造——支持 move-only 的 T）。
    static Expected ok(T v)
    {
        Expected r;
        r.value.template emplace<0>(std::move(v));
        return r;
    }

    /// 错误态工厂（移动入构造——支持 move-only 的 E）。
    static Expected err(E e)
    {
        Expected r;
        r.value.template emplace<1>(std::move(e));
        return r;
    }
};

// =====================================================================
// 名称解析两错误值类型（§7.4 原文形态；Errors.hpp 承载＝§3.1 模块清单——
// NameMap/IRuntimeNameResolver 的 Expected 错误侧在 RT-T05 消费）。
// =====================================================================

/**
 * @brief 名称→对象解析失败载荷（RuntimeNameMap::resolveRuntimeName 错误侧）。
 *
 * 值语义纯结构；code 取 UnknownObject（含空名/非法句法同码——§7.4 接口
 * 属性表）等 Context 类码；requestedName 回显原名（RT-NM-3：错误含回显、
 * 不抛、不默认命中）。线程安全：纯值。
 */
struct RuntimeResolveError {
    RuntimeErrorCode code;       ///< 错误码（unknown-object 等）
    std::string detail;          ///< 开发诊断细节（定位失败原因）
    std::string requestedName;   ///< 请求解析的完整名（原名回显——诊断可定位）
};

/**
 * @brief 对象→名称反解失败载荷（RuntimeNameMap::resolveObjectId 错误侧）。
 *
 * 与 RuntimeResolveError 同构但回显域不同：id 为强类型（防名称/身份混用
 * ——RT-NM-7 类型层纪律）。线程安全：纯值。
 */
struct RuntimeNameError {
    RuntimeErrorCode code;           ///< 错误码（unknown-object 等）
    std::string detail;              ///< 开发诊断细节
    core::ObjectId requestedId;      ///< 请求反解的对象身份（id 规范文本入 detail——§7.4）
};

}  // namespace sdurws::ird::runtime

#endif  // SDURWS_IRD_RUNTIME_ERRORS_HPP
