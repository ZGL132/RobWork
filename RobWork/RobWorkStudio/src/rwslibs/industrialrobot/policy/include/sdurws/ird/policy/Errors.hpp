/**
 * @file   Errors.hpp
 * @brief  policy 错误契约——PolicyErrorCode 全表（稳定 token）＋PolicyError。
 *
 * 设计依据：
 *   - units/policy.md §3.1 组成表（`Errors.hpp`｜PolicyErrorCode（稳定 token）、
 *     PolicyError，详见"§3.1 尾、§5.2"）；§5.2 校验规则与错误分类表（诊断码/
 *     严重级/可确认性——本表码面的权威来源）；§9.6（POLICY-* 建议码清单——
 *     registryCode() 的建议值来源）
 *   - §2.1 异常行（policy 用自有异常类型 PolicyError；评估实现必须捕获
 *     RobWork 异常并转为 Failed＋诊断，不吞、不崩、不跨进程抛出）；
 *     §9.1～§9.5 通用契约行（错误＝PolicyError（稳定 token）或非抛出
 *     try*／optional 双轨；D-15：PolicyError 不跨进程边界）
 *   - 需求 ARC-05（策略单一权威）、NFR-COR-03（不静默吞错）
 *   - 任务契约 tasks/foundation/POL-T02.json（≙WP-07-T02）acceptance 1/2：
 *     字段约束与不可变性用例通过——本头是其码面基础
 *
 * 背景说明（为什么 policy 自有一套错误码，而不直接复用 core::CoreError）：
 *   core 的错误契约（CoreError）只覆盖 core 自身抛错路径（§3.2 消费清单：
 *   "core 抛错捕获与转发"——policy 捕获 CoreError 后按语境转译为本表码面，
 *   不向上游泄漏 core 类型）。policy 的错误面横跨 schema 校验（§5.2）、
 *   阈值域校验（§4.4/§7.4）、规则集结构检查（§7.2）、适用范围验证（§4.2.1）
 *   与发布对象工厂（§4.5），各有独立归属与恢复路径——与 runtime（其 §3.4
 *   15 值全表）、evidence（其 §3.1＋13 值全表）同一模式：枚举全表＋稳定
 *   token，异常轨与后续任务的判定/投影共用同一码面。码值权威＝diagnostics
 *   单元的 StableCodeRegistry（本单元只产出 token/建议码，不注册码值——PA-1）。
 *
 * 错误语义总纲（AGENTS.md 错误语义在本单元的落点）：
 *   - 调用方契约违约（如把阈值放到错误单位域的字段上、发布非法实例）＝
 *     fail-fast（抛 PolicyError），不走诊断收集；
 *   - 输入非法（解析/校验类码）的"拒绝发布"主轨是 PolicyParseResult.diagnostics
 *     （POL-T04 落地），本码面同时供其异常轨/断言使用——双轨共用同码面
 *     （runtime CompileOutcome.diagnostics 与 RuntimeError 同款纪律）；
 *   - D-15：PolicyError 不跨进程边界（worker 通道错误码化归 execution）。
 *
 * 线程安全：本头全部实体为纯值/纯函数（无共享可变状态），并发只读安全。
 * 确定性：token/建议码为编译期固定表（switch 全枚举），同码同串、跨平台
 * 跨进程逐字节一致（NFR-COR-02 的码面子集）。
 */

#ifndef SDURWS_IRD_POLICY_ERRORS_HPP
#define SDURWS_IRD_POLICY_ERRORS_HPP

#include <stdexcept>
#include <string>
#include <string_view>

namespace sdurws::ird::policy {

// =====================================================================
// PolicyErrorCode——policy 全量稳定错误码枚举（POL-T02 落位 14 值；
// POL-T03 表尾追加 EncodingInvalid → 15 值；POL-T05 表尾追加
// PortAssemblyIncomplete → 16 值）。
// 来源＝§5.2 错误类诊断码逐行（13 值，顺序即表序）＋表尾追加 2 值：
// PolicyObjectInvalid（发布对象工厂即时校验码——evidence SnapshotIncomplete
// "快照非法实例/builder 即时验证失败"同模式，随单元卡 v0.3 增量登记）与
// EncodingInvalid（对象字节编码契约违约码——§9.2"字节损坏在解码期报错"
// 落点，随单元卡 v0.4 增量登记）。
// 枚举顺序与数值一经交付不得改动/插入——持久化于诊断与报告的 token 虽为
// 字符串，但枚举数值进入二进制契约面，稳定第一（runtime/evidence 同款
// 纪律）；后续任务需要新码（§9.6 的 CLL、JNT、VERSION-INCOMPATIBLE 等
// 家族）时只能**追加表尾**并在单元卡增量修订留痕（DTB §5.4）。
// =====================================================================

/**
 * @brief policy 稳定错误码全表（§5.2 错误类各行＋发布对象工厂即时校验）。
 *
 * token 稳定（持久化于诊断/报告）；建议码值归 diagnostics StableCodeRegistry
 * （§9.6 建议码清单——映射见 registryCode()）。每值的设计锚点见注释。
 */
enum class PolicyErrorCode {
    /// policy/schema-unknown-field——schema 未定义的键（§5.2 行 1：拒绝发布，
    /// 不静默忽略——防拼写错误降级为默认值；NFR-COR-03 同源）。
    SchemaUnknownField,
    /// policy/schema-version-future——schemaVersion 大于当前已知代（§5.2 行 2：
    /// 未来版本不前向猜测解析；PM-06 同源只读拒绝＋升级指引）。
    SchemaVersionFuture,
    /// policy/schema-version-unknown——schemaVersion 小于已知最低代（§5.2 行 2：
    /// 只读拒绝；与 -future 同行分列）。
    SchemaVersionUnknown,
    /// policy/threshold-non-finite——阈值 NaN/±Inf（§5.2 行 3：拒绝发布；
    /// 原文保留于 RawPolicyInput——POL-T03/T04 承载）。
    ThresholdNonFinite,
    /// policy/threshold-non-positive——阈值负值/零（§5.2 行 4 左：域要求为正
    /// 而输入非正；如 nearLimitRatio≤0、行程上限≤0）。
    ThresholdNonPositive,
    /// policy/threshold-out-of-range——阈值为正但越出域窗（§5.2 行 4 右：
    /// 如 nearLimitRatio=1.5∉(0,1]、条件数阈值∈(0,1)；POL-PARSE-2 原文例）。
    ThresholdOutOfRange,
    /// policy/threshold-required-missing——必填阈值缺省（§5.2 行 5：
    /// enabled==true 而 safetyClearance 未设置——无冻结默认项不得静默省略，
    /// P-POL-2 保守口径：缺失即拒绝发布，不发明数值）。
    ThresholdRequiredMissing,
    /// policy/unit-mismatch——单位不一致/未注册 token（§5.2 行 6：core
    /// convert 抛错转发——SA-12 换算唯一权威在 core）。
    UnitMismatch,
    /// policy/rule-duplicate——同对同类型重复登记（§5.2 行 7：列出重复条目）。
    RuleDuplicate,
    /// policy/rule-conflict——排除覆盖必检（§5.2 行 8：excludedPairs 展开后
    /// 与 mandatoryPairs 相交——结构上保证过滤不得隐藏必检，§7.2/D-06）。
    RuleConflict,
    /// policy/rule-cycle——组引用循环/引用未定义组（§5.2 行 9：组仅可引用
    /// 对象；循环在 schema 层不可能，检查为纵深防御）。
    RuleCycle,
    /// policy/scope-object-missing——规则适用对象不在修订闭包内（§5.2 行 10：
    /// 对象已删除/跨修订混入——定位到对象 ID，CON-01）。
    ScopeObjectMissing,
    /// policy/applicability-invalid——适用范围无效（§5.2 行 11：未知模式
    /// token/空域启用〔enabled==true 且 enabledDomains 为空＝无域可检，§5.1④〕）。
    ApplicabilityInvalid,
    /// policy/policy-object-invalid——策略发布对象非法实例/工厂即时校验失败
    /// （表尾追加，POL-T02——evidence SnapshotIncomplete 同模式：身份无效/
    /// 内容身份为空/校验状态非 Valid/数值契约锚为空/子模型字段级结构非法
    /// 〔含阈值单位域与字段位置不符〕；§4.5"发布对象只存在 Valid 态"的
    /// fail-fast 载体）。
    PolicyObjectInvalid,
    /// policy/encoding-invalid——对象字节编码契约违约（表尾追加，POL-T03——
    /// §9.2"字节损坏在解码期报错"的码面落点）：PolicyCodec::decode 遇
    /// magic/形态字节之外的契约损坏（载荷长度与实际不符/字符串或计数越界/
    /// 未知枚举值/非有限位模式/载荷未精确耗尽/语义闭包投影形态误入解码）。
    /// 与 PolicyObjectInvalid 的分工：本码＝**字节**对格式契约的违约
    /// （传输/存储损坏或未校验写入），后者＝解码产物**实例**的保留值违约
    /// （全零身份）。evidence SliceIncomplete 同模式，随单元卡 v0.4 登记。
    EncodingInvalid,
    /// policy/port-assembly-incomplete——④端口装配契约违约（表尾追加，
    /// POL-T05——§9.1 前置条件行"实例由 L5/worker 宿主装配注入"的 fail-fast
    /// 载体）：宿主在评估器半区（ICollisionEvaluator）尚未注入时调用
    /// IPolicyProvider::collisionEvaluator()/collisionBackend() 语义所需的
    /// 已装配能力。与 PolicyObjectInvalid 的分工：本码＝**端口实例**的装配
    /// 状态违约（宿主装配期遗漏/装配未完成即对外服务），后者＝**策略对象**
    /// 实例的保留值违约。建议码 POLICY-PORT-ASSEMBLY-INCOMPLETE 为补登建议值
    /// （P-PR-6 同模式，随单元卡 v0.6 增量登记）。
    PortAssemblyIncomplete,
};

/**
 * @brief 取错误码的稳定 token（注释列原文）。
 *
 * @param code [in] 错误码（全枚举 16 值均有 token——全函数，永不返回空）
 * @return 稳定 token 字符串（"policy/..." 形态；静态存储期，调用方无需释放）
 *
 * 确定性：编译期固定 switch 全枚举表（无 default——新增枚举值未登记表项时
 * 编译器告警暴露遗漏；测试侧另有全表用例逐值钉住）；同码同串、无 locale
 * 依赖（NFR-COR-02）。token 命名规则："policy/" 前缀＋kebab-case（§9.6
 * 建议码 POLICY-X-Y-Z 派生为 policy/x-y-z；PolicyObjectInvalid 为表尾追加
 * 值，建议码 POLICY-POLICY-OBJECT-INVALID 为补登建议值——单元卡 v0.3 登记，
 * P-PR-6 同模式）。
 */
std::string_view token(PolicyErrorCode code) noexcept;

/**
 * @brief 取错误码对应的 diagnostics 建议注册码（§9.6 建议码清单）。
 *
 * 关系说明：§9.6 建议码清单覆盖 POLICY-SCHEMA、POLICY-THRESHOLD、UNIT、RULE、
 * SCOPE/APPLICABILITY 家族——与本表 13 个 §5.2 系值逐一对应；表尾追加值
 * 发补登建议码（P-PR-6 同模式，是否收编由 diagnostics 所有者裁决）：
 * PolicyObjectInvalid（POL-T02）与 EncodingInvalid（POL-T03）。注意：§9.6
 * 其余家族（POLICY-CLL-*、POLICY-JNT-*、POLICY-VERSION-INCOMPATIBLE、
 * POLICY-CONTENT-IDENTITY-MISMATCH 等）不在本表——
 * 其码面归属后续任务的表尾追加（CollisionEvaluator/JointLimits/Compatibility
 * 各自落位时登记），本函数不预发。
 *
 * @param code [in] 错误码
 * @return 建议注册码（"POLICY-*" 形态——全部 16 值均发建议码；正式码值以
 *         diagnostics StableCodeRegistry 注册为准，本函数不承担注册职责——PA-1）
 */
std::string_view registryCode(PolicyErrorCode code) noexcept;

// =====================================================================
// PolicyError——policy 唯一异常类型（§2.1 异常行：自有异常类型；消息前缀
// "policy/..." 面向开发诊断——runtime/evidence 同款约定）。
// =====================================================================

/**
 * @brief policy 异常轨载体（std::runtime_error 子类，code() 访问器）。
 *
 * 值语义：按值抛出/捕获（what() 消息在基类内持有，安全）。
 * 消息前缀约定：what() ＝ "<token>[: <detail>]"——token 前缀由构造函数
 * 强制拼装（无裸消息构造路径），面向开发诊断；用户可见文案与码值登记归
 * diagnostics 单元（NFR-REL-05 同源），本类不越权。
 *
 * 使用场景：构造期硬失败（PolicyThreshold 域校验拒绝、EngineeringPolicySet
 * 工厂即时校验拒绝——POL-T02）与调用方契约违约 fail-fast；解析/校验类输入
 * 非法的主轨是 PolicyParseResult.diagnostics（POL-T04——可恢复、全量不短路），
 * 异常轨为断言/快失败场景共用同码面。可恢复查询路径一律另有 try*／optional
 * 非抛出变体（§9.1～§9.5 通用契约行）。
 *
 * 生命周期约束（D-15）：仅进程内抛传，不跨进程边界——worker 通道由
 * execution 错误码化；捕获后不得重抛入其他单元的异常契约。
 * 另注（§2.1 异常行）：评估实现必须捕获 RobWork 异常（rw::common::Exception）
 * 并转为 Failed＋诊断——RobWork 异常**不得**以本类型重抛（转译归评估侧，
 * POL-T07）。
 *
 * 线程安全：不可变（构造后仅只读访问）；what() 与 std::runtime_error 同规。
 */
class PolicyError : public std::runtime_error {
public:
    /**
     * @brief 以错误码＋细节构造；what() ＝ "<token>: <detail>"。
     * @param code   [in] 稳定错误码（全表 16 值之一）
     * @param detail [in] 开发诊断细节（就地定位信息：字段/对象/数值等）；
     *               空串合法——此时 what() 恰为 token（无尾随冒号空格）
     */
    PolicyError(PolicyErrorCode code, std::string detail);

    /**
     * @brief 以错误码构造（无细节）；what() ＝ token。
     * @param code [in] 稳定错误码
     */
    explicit PolicyError(PolicyErrorCode code)
        : PolicyError(code, std::string{})
    {
    }

    /// 稳定错误码访问器（构造后不变；建议码经 registryCode(code) 取）。
    PolicyErrorCode code() const noexcept { return m_code; }

private:
    PolicyErrorCode m_code;   ///< 本异常携带的稳定错误码（token 经 token(m_code) 取得）
};

}  // namespace sdurws::ird::policy

#endif  // SDURWS_IRD_POLICY_ERRORS_HPP
