/**
 * @file   Diagnostics.hpp
 * @brief  policy 诊断构造——建议码表（policyDiagCodes）＋IPolicyDiagnostics
 *         ＋makeComparative 辅助（§3.1 组成表三实体；§9.6 契约）。
 *
 * 设计依据：
 *   - units/policy.md §9.6（IPolicyDiagnostics 契约与建议码清单原文——本头的
 *     冻结来源）、§3.1 组成表（`Diagnostics.hpp`｜policyDiagCodes（建议码表）、
 *     IPolicyDiagnostics、makeComparative 辅助）、§12 POL-T10 行（本头＋
 *     src/Diagnostics.cpp 为任务产物）
 *   - 需求 ERR-01（诊断三轴正交：稳定码/对象定位/上下文＋比较型三要素）、
 *     UX-03（比较型校验 actual/expected/unit 必填）、NFR-MNT-03（单一权威
 *     定义——全单元建议码字面量自本头起唯一）、NFR-COR-02（确定性——码表
 *     编译期固定、同码同串）
 *   - 任务契约 tasks/foundation/POL-T10.json（≙WP-07-T10，分支 wp07-t10）
 *     acceptance 1：checkDiagnosticRecord 族（testkit）码表与构造不变量用例通过
 *
 * 背景说明（为什么要有这一层，而不是各处直接拼 core::DiagnosticRecord）：
 *   码值分配与文案的**权威**在 diagnostics 单元的 StableCodeRegistry（ARCH
 *   §7.8；PA-1 权威唯一），policy 只产出"建议码"——但建议码字符串若散落在
 *   各翻译单元的字面量里，注册表核对（registeredCodes）就没有可信的清单
 *   来源，且同名码在两处手写极易漂移（NFR-MNT-03 要禁止的第二事实源）。
 *   POL-T02~T09 期间各 TU 以"单点字面量＋迁码表注记"过渡承载（单元卡 v0.4~
 *   v0.9 逐次登记），本头即各注记指向的**收敛点**：全单元 29 个建议码在此
 *   唯一定义，五个消费 TU（PolicyParsing/PolicyPort/CollisionQuery/
 *   JointLimits/Compatibility）自本任务起只经 policyDiagCode(PolicyDiagCode)
 *   取码，不再手写字符串。
 *
 * 与 Errors.hpp 的关系（两码面、一个建议码权威）：
 *   - PolicyErrorCode（Errors.hpp）＝**异常轨**码面（PolicyError 载体，
 *     19 值）；其 registryCode() 给出每个异常码的建议注册码。
 *   - PolicyDiagCode（本头）＝**建议码全表**（29 值）——除异常轨 19 值外
 *     还含诊断轨码（评估输出 Failed＋DiagnosticRecord，非异常枚举：CLL 四
 *     评估期值、JNT 两值、VERSION-INCOMPATIBLE、INFO-DEFAULT-APPLIED）与
 *     §9.6 清单既有而尚无消费点的 POLICY-CONTENT-IDENTITY-MISMATCH。
 *   - 两表面 19 个交集值的字符串由测试逐串钉住一致（DiagnosticsTest 的
 *     RegistryCodeIntersection 钉住——防两处漂移）；registryCode() 的字面量
 *     已改为委托本表（Errors.cpp v0.10 起单一事实源在此）。
 *
 * 陷阱处置登记（任务契约 knownPitfalls，PIPE §0.3 处置约束）：
 *   - P-PR-6 与 P-EX-8（PRJ 系／EX 系稳定码与 sink 形态待 diagnostics 冻结
 *     ——与本单元同案的 sink 统一未裁决）：本头**只产出建议码**，不注册码
 *     值、不实现任何 sink、不裁决 sink 统一——注册行为归 diagnostics 单元
 *     （WP-09），消费其 StableCodeRegistry 冻结后由所有者收编或改写建议码
 *     （正式码值以注册为准，PA-1 不私裁）。
 *
 * 线程安全：码表编译期冻结只读；IPolicyDiagnostics 实现为无状态纯函数——
 * 并发只读安全（§9.6 通用契约行）。
 * 确定性：同 (code, subject, context, cause, action, comparison) 输入必得
 * 逐字段相等的 DiagnosticRecord（NFR-COR-02；无时钟、无随机源——诊断记录
 * 不含时间戳字段，core §4.8 字段表即全集）。
 */

#ifndef SDURWS_IRD_POLICY_DIAGNOSTICS_HPP
#define SDURWS_IRD_POLICY_DIAGNOSTICS_HPP

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Identity.hpp>

namespace sdurws::ird::policy {

// =====================================================================
// PolicyDiagCode——policy 建议码全表枚举（29 值）。
// =====================================================================

/**
 * @brief policy 全部稳定诊断建议码的枚举词表（§9.6 清单原文＋表尾补登）。
 *
 * 枚举顺序＝建议码表稳定序（policyDiagCodes() 输出序、registeredCodes()
 * 返回序）——一经交付不得改动/插入，新增值只能**追加表尾**并在单元卡增量
 * 修订留痕（DTB §5.4；Errors.hpp PolicyErrorCode 同款纪律：字符串是持久化
 * 面，枚举序进入二进制契约面，稳定第一）。
 *
 * 分组与来源（每值建议码原文见 policyDiagCode() 的 switch 表——唯一字面量点）：
 *  - schema/threshold/unit/rule/scope/applicability 家族：§5.2 错误分类表
 *    逐行（§9.6 清单 1~12 项）——异常轨与诊断轨共用码面（诊断构造入口
 *    make() 消费本表；异常轨 PolicyError 经 Errors.hpp registryCode 映射）；
 *  - PolicyObjectInvalid/EncodingInvalid/PortAssemblyIncomplete/
 *    ObjectMissing/QueryInvalid：表尾补登建议值（P-PR-6 同模式，单元卡
 *    v0.3/v0.4/v0.5/v0.6/v0.8 逐次登记——是否收编归 diagnostics 所有者）；
 *  - CLL 家族六值/JNT 家族两值：§9.6 清单——评估输出诊断轨（Failed＋
 *    DiagnosticRecord，**非异常枚举**；POL-T07/T08 期间以单点字面量过渡，
 *    本任务迁入本表）；
 *  - VersionIncompatible/ContentIdentityMismatch：§8.2 诊断行与 §5.3 身份
 *    核对（清单 13/14 项；后者为清单既有、尚无消费点——预留码面，不预发
 *    消费语义）；
 *  - InfoDefaultApplied：§5.2 行 12 唯一 INFO 级建议码（默认填充告知）。
 */
enum class PolicyDiagCode {
    // ---- schema 家族（§5.2 行 1~2）----
    SchemaUnknownField,        ///< POLICY-SCHEMA-UNKNOWN-FIELD（未定义键拒绝发布）
    SchemaVersionFuture,       ///< POLICY-SCHEMA-VERSION-FUTURE（未来代不前向猜测）
    SchemaVersionUnknown,      ///< POLICY-SCHEMA-VERSION-UNKNOWN（低于最低已知代）
    // ---- threshold 家族（§5.2 行 3~5）----
    ThresholdNonFinite,        ///< POLICY-THRESHOLD-NON-FINITE（NaN/±Inf 拒绝）
    ThresholdNonPositive,      ///< POLICY-THRESHOLD-NON-POSITIVE（要求为正而非正）
    ThresholdOutOfRange,       ///< POLICY-THRESHOLD-OUT-OF-RANGE（越出域窗）
    ThresholdRequiredMissing,  ///< POLICY-THRESHOLD-REQUIRED-MISSING（必填缺省——
                               ///< 补登：§5.2 行 5 系值，registryCode 既有而 §9.6
                               ///< 清单未列，随本任务入表登记，P-PR-6 同模式）
    // ---- 单位（§5.2 行 6）----
    UnitMismatch,              ///< POLICY-UNIT-MISMATCH（单位不一致/未注册 token）
    // ---- 规则集（§5.2 行 7~9）----
    RuleDuplicate,             ///< POLICY-RULE-DUPLICATE（同对同类型重复）
    RuleConflict,              ///< POLICY-RULE-CONFLICT（排除∩必检≠∅）
    RuleCycle,                 ///< POLICY-RULE-CYCLE（组引用未定义组/循环防御）
    // ---- 适用范围（§5.2 行 10~11）----
    ScopeObjectMissing,        ///< POLICY-SCOPE-OBJECT-MISSING（对象不在闭包内）
    ApplicabilityInvalid,      ///< POLICY-APPLICABILITY-INVALID（未知模式/空域启用）
    // ---- 对象/字节/端口/查询（表尾补登系——P-PR-6 同模式）----
    PolicyObjectInvalid,       ///< POLICY-POLICY-OBJECT-INVALID（发布对象工厂即时
                               ///< 校验——POL-T02 补登；注意建议码含两段 POLICY）
    EncodingInvalid,           ///< POLICY-ENCODING-INVALID（对象字节编码契约违约
                               ///< ——POL-T03 补登，§9.2 解码期报错）
    PortAssemblyIncomplete,    ///< POLICY-PORT-ASSEMBLY-INCOMPLETE（④端口装配
                               ///< 契约违约——POL-T05 补登，§9.1 前置行）
    ObjectMissing,             ///< POLICY-OBJECT-MISSING（④端口存储侧：对象缺失/
                               ///< 版本未指定不可编址取数——POL-T05 补登）
    QueryInvalid,              ///< POLICY-CLL-QUERY-INVALID（评估期查询契约违约
                               ///< ——POL-T07 补登，§9.3 错误类型行评估半区）
    // ---- CLL 家族（§9.6——评估输出诊断轨，非异常枚举）----
    CllSceneInvalid,           ///< POLICY-CLL-SCENE-INVALID（会话构建期场景校验）
    CllNameUnresolved,         ///< POLICY-CLL-NAME-UNRESOLVED（名称不可解析——ARC-04）
    CllContextExpired,         ///< POLICY-CLL-CONTEXT-EXPIRED（迟到调用拒绝——POL-LATE-1）
    CllDetectorUnavailable,    ///< POLICY-CLL-DETECTOR-UNAVAILABLE（检测器能力不可用
                               ///< ——KIN-05/P-POL-11）
    CllEvaluationFailed,       ///< POLICY-CLL-EVALUATION-FAILED（评估内部异常/非有限
                               ///< 实测值——§7.4/§7.5 不伪造）
    CllGeometryMissing,        ///< POLICY-CLL-GEOMETRY-MISSING（作用域对象几何缺口
                               ///< ——告知性，KIN-05）
    // ---- JNT 家族（§9.6——评估输出诊断轨）----
    JntTableInvalid,           ///< POLICY-JNT-TABLE-INVALID（关节表区间非法——D-13）
    JntEngineeringRangeInvalid, ///< POLICY-JNT-ENGINEERING-RANGE-INVALID（工程工作
                                ///< 范围非法——MDL-12）
    // ---- 版本/身份（§8.2/§5.3）----
    VersionIncompatible,       ///< POLICY-VERSION-INCOMPATIBLE（策略版本不兼容——
                               ///< 比较型，§8.2）
    ContentIdentityMismatch,   ///< POLICY-CONTENT-IDENTITY-MISMATCH（内容身份核对
                               ///< 失配——§5.3/§8.6；清单既有码面，消费点随后续
                               ///< 任务落位）
    // ---- INFO（§5.2 行 12）----
    InfoDefaultApplied,        ///< POLICY-INFO-DEFAULT-APPLIED（唯一 INFO 级：默认
                               ///< 填充告知——附录 D 第 11 项 4π）
};

/// 建议码全表值数（policyDiagCodes() 表长；新增枚举值时同步——测试钉住一致）。
inline constexpr std::size_t kPolicyDiagCodeCount = 29;

/// 建议码全表的稳定载体（std::array 直接复用——值语义、编译期定长）。
using PolicyDiagCodeTable = std::array<std::string_view, kPolicyDiagCodeCount>;

/**
 * @brief 取建议码原文（全单元 POLICY-* 字符串的**唯一字面量点**）。
 *
 * @param code [in] 建议码枚举值（全表 29 值均有映射——全函数）
 * @return 建议码原文（"POLICY-*" 形态；静态存储期，调用方无需释放）
 *
 * inline constexpr 定义于本头（而非 TU）：迁移 TU 的"码表引用别名"
 * （`constexpr std::string_view k = policyDiagCode(...)`）与 Errors.cpp 的
 * registryCode 委托都需要**编译期可见**的定义——字面量仍全单元仅此一处
 * switch 表（NFR-MNT-03 单一权威不因载体是 .hpp/.cpp 而变化）。
 *
 * 确定性：编译期固定 switch 全枚举、无 default（新增枚举值未登记表项时
 * MSVC /W4 C4062 告警在构建期暴露——Errors.cpp token() 同款双保险，测试侧
 * 另有全表用例逐值钉住）。码值权威＝diagnostics StableCodeRegistry——本表
 * 仅"建议"（P-PR-6 处置约束；PA-1 不越权注册）。
 */
constexpr std::string_view policyDiagCode(PolicyDiagCode code) noexcept
{
    // 稳定建议码全表（顺序＝枚举声明序；字符串＝§9.6 清单原文＋表尾补登）。
    // 无 default 分支：未来表尾追加新枚举值而忘记登记此表时，编译器告警
    // 在构建期即暴露（全表数组与测试用例为第二、第三重保险）。
    switch (code) {
    // ---- schema 家族（§5.2 行 1~2／§9.6 清单 1~3）----
    case PolicyDiagCode::SchemaUnknownField:         return "POLICY-SCHEMA-UNKNOWN-FIELD";
    case PolicyDiagCode::SchemaVersionFuture:        return "POLICY-SCHEMA-VERSION-FUTURE";
    case PolicyDiagCode::SchemaVersionUnknown:       return "POLICY-SCHEMA-VERSION-UNKNOWN";
    // ---- threshold 家族（§5.2 行 3~5／清单 4~6＋补登）----
    case PolicyDiagCode::ThresholdNonFinite:         return "POLICY-THRESHOLD-NON-FINITE";
    case PolicyDiagCode::ThresholdNonPositive:       return "POLICY-THRESHOLD-NON-POSITIVE";
    case PolicyDiagCode::ThresholdOutOfRange:        return "POLICY-THRESHOLD-OUT-OF-RANGE";
    // 补登值（§9.6 清单未列而 registryCode 既有——§5.2 行 5 系值，随本任务
    // 入表登记，P-PR-6 同模式；单元卡 v0.11 变更记录留痕）。
    case PolicyDiagCode::ThresholdRequiredMissing:   return "POLICY-THRESHOLD-REQUIRED-MISSING";
    // ---- 单位（§5.2 行 6／清单 7）----
    case PolicyDiagCode::UnitMismatch:               return "POLICY-UNIT-MISMATCH";
    // ---- 规则集（§5.2 行 7~9／清单 8~10）----
    case PolicyDiagCode::RuleDuplicate:              return "POLICY-RULE-DUPLICATE";
    case PolicyDiagCode::RuleConflict:               return "POLICY-RULE-CONFLICT";
    case PolicyDiagCode::RuleCycle:                  return "POLICY-RULE-CYCLE";
    // ---- 适用范围（§5.2 行 10~11／清单 11~12）----
    case PolicyDiagCode::ScopeObjectMissing:         return "POLICY-SCOPE-OBJECT-MISSING";
    case PolicyDiagCode::ApplicabilityInvalid:       return "POLICY-APPLICABILITY-INVALID";
    // ---- 对象/字节/端口/查询（表尾补登系——P-PR-6 同模式，单元卡 v0.3/
    //      v0.4/v0.5/v0.6/v0.8 逐次登记；是否收编归 diagnostics 所有者）----
    case PolicyDiagCode::PolicyObjectInvalid:        return "POLICY-POLICY-OBJECT-INVALID";
    case PolicyDiagCode::EncodingInvalid:            return "POLICY-ENCODING-INVALID";
    case PolicyDiagCode::PortAssemblyIncomplete:     return "POLICY-PORT-ASSEMBLY-INCOMPLETE";
    case PolicyDiagCode::ObjectMissing:              return "POLICY-OBJECT-MISSING";
    case PolicyDiagCode::QueryInvalid:               return "POLICY-CLL-QUERY-INVALID";
    // ---- CLL 家族（§9.6 清单 15~20——评估输出诊断轨；POL-T07/T08 期间
    //      的 CollisionQuery.cpp/JointLimits.cpp 单点字面量已迁入本表）----
    case PolicyDiagCode::CllSceneInvalid:            return "POLICY-CLL-SCENE-INVALID";
    case PolicyDiagCode::CllNameUnresolved:          return "POLICY-CLL-NAME-UNRESOLVED";
    case PolicyDiagCode::CllContextExpired:          return "POLICY-CLL-CONTEXT-EXPIRED";
    case PolicyDiagCode::CllDetectorUnavailable:     return "POLICY-CLL-DETECTOR-UNAVAILABLE";
    case PolicyDiagCode::CllEvaluationFailed:        return "POLICY-CLL-EVALUATION-FAILED";
    case PolicyDiagCode::CllGeometryMissing:         return "POLICY-CLL-GEOMETRY-MISSING";
    // ---- JNT 家族（§9.6 清单 21~22——同上迁移通道）----
    case PolicyDiagCode::JntTableInvalid:            return "POLICY-JNT-TABLE-INVALID";
    case PolicyDiagCode::JntEngineeringRangeInvalid: return "POLICY-JNT-ENGINEERING-RANGE-INVALID";
    // ---- 版本/身份（§8.2 诊断行／§5.3——清单 13~14）----
    case PolicyDiagCode::VersionIncompatible:        return "POLICY-VERSION-INCOMPATIBLE";
    case PolicyDiagCode::ContentIdentityMismatch:    return "POLICY-CONTENT-IDENTITY-MISMATCH";
    // ---- INFO（§5.2 行 12——唯一 INFO 级建议码）----
    case PolicyDiagCode::InfoDefaultApplied:         return "POLICY-INFO-DEFAULT-APPLIED";
    }
    // 不可达路径：全枚举已覆盖。返回空串仅为满足编译器（无 default 时控制
    // 流分析仍要求出口）；测试全表用例保证该路径永不在运行期出现。
    return {};
}

/**
 * @brief 建议码全表（稳定序＝枚举声明序；供注册表核对与测试全表锚定）。
 *
 * @return 全表只读引用（静态存储期——调用方持有引用即可，无所有权转移）
 *
 * 与 registeredCodes() 的分工：本函数返回 string_view 表（零拷贝，供静态
 * 断言/测试）；IPolicyDiagnostics::registeredCodes() 返回 string 拷贝
 * （§9.6 冻结签名——跨单元核对面的承载形态）。表元素经 policyDiagCode
 * 逐值生成（编译期求值）——表与映射函数同源，一致性另由 DiagnosticsTest
 * 全表用例钉住（防"改枚举忘改表"）。
 */
inline const PolicyDiagCodeTable& policyDiagCodes() noexcept
{
    static constexpr PolicyDiagCodeTable kTable = {
        policyDiagCode(PolicyDiagCode::SchemaUnknownField),
        policyDiagCode(PolicyDiagCode::SchemaVersionFuture),
        policyDiagCode(PolicyDiagCode::SchemaVersionUnknown),
        policyDiagCode(PolicyDiagCode::ThresholdNonFinite),
        policyDiagCode(PolicyDiagCode::ThresholdNonPositive),
        policyDiagCode(PolicyDiagCode::ThresholdOutOfRange),
        policyDiagCode(PolicyDiagCode::ThresholdRequiredMissing),
        policyDiagCode(PolicyDiagCode::UnitMismatch),
        policyDiagCode(PolicyDiagCode::RuleDuplicate),
        policyDiagCode(PolicyDiagCode::RuleConflict),
        policyDiagCode(PolicyDiagCode::RuleCycle),
        policyDiagCode(PolicyDiagCode::ScopeObjectMissing),
        policyDiagCode(PolicyDiagCode::ApplicabilityInvalid),
        policyDiagCode(PolicyDiagCode::PolicyObjectInvalid),
        policyDiagCode(PolicyDiagCode::EncodingInvalid),
        policyDiagCode(PolicyDiagCode::PortAssemblyIncomplete),
        policyDiagCode(PolicyDiagCode::ObjectMissing),
        policyDiagCode(PolicyDiagCode::QueryInvalid),
        policyDiagCode(PolicyDiagCode::CllSceneInvalid),
        policyDiagCode(PolicyDiagCode::CllNameUnresolved),
        policyDiagCode(PolicyDiagCode::CllContextExpired),
        policyDiagCode(PolicyDiagCode::CllDetectorUnavailable),
        policyDiagCode(PolicyDiagCode::CllEvaluationFailed),
        policyDiagCode(PolicyDiagCode::CllGeometryMissing),
        policyDiagCode(PolicyDiagCode::JntTableInvalid),
        policyDiagCode(PolicyDiagCode::JntEngineeringRangeInvalid),
        policyDiagCode(PolicyDiagCode::VersionIncompatible),
        policyDiagCode(PolicyDiagCode::ContentIdentityMismatch),
        policyDiagCode(PolicyDiagCode::InfoDefaultApplied),
    };
    return kTable;
}

/**
 * @brief 判定建议码原文是否在本表内（make() 的码表核对谓词；测试用）。
 *
 * @param code [in] 建议码原文（任意串——不要求来自本表）
 * @return true＝表内成员（句法由 core 工厂另行把关，本谓词只答"是否成员"）
 */
bool isRegisteredPolicyDiagCode(std::string_view code) noexcept;

// =====================================================================
// IPolicyDiagnostics——诊断构造接口（§9.6 签名逐字冻结）。
// =====================================================================

/**
 * @brief policy 诊断构造接口（§9.6）——本单元内部构造诊断的唯一正式入口。
 *
 * 职责边界：只负责"把建议码＋定位＋三段文案＋可选比较三要素组装成合法的
 * core::DiagnosticRecord"；码值注册、文案本地化、用户可见表述、日志 sink
 * 均归 diagnostics 单元（§10.6 交接——P-PR-6/P-EX-8 sink 统一未裁决，本
 * 接口不涉及任何 sink 形态）。
 *
 * 合法调用（§9.6 原文）：本单元内部构造＋测试断言（testkit
 * checkDiagnosticRecord）。非法调用：绕过本接口以裸字符串拼诊断码入正式
 * 结果（码值权威在注册表——静态检查拦截；实现侧的拦截面即本表唯一字面量
 * 点＋消费 TU 零手写码）。
 *
 * 生命周期/所有权：实现为无状态只读——调用方持有实例的期间即使用期；全部
 * 返回值为值语义（DiagnosticRecord 拷贝），无别名输出。
 */
class IPolicyDiagnostics {
public:
    virtual ~IPolicyDiagnostics() = default;

    /**
     * @brief 本单元建议码清单（§9.6——供 diagnostics 注册表核对）。
     *
     * @return 全部 29 个建议码原文的拷贝（稳定序＝枚举序；每次调用内容
     *         逐字节相同——NFR-COR-02）
     */
    virtual std::vector<std::string> registeredCodes() const = 0;

    /**
     * @brief 构造一条诊断记录（§9.6——core DiagData 不变量 C-1~C-3 的
     *        policy 侧前置核对＋委托 core 工厂）。
     *
     * @param code     [in] 建议码原文——**必须**是 policyDiagCodes() 表内
     *                 成员（表外码＝调用方契约违约，fail-fast 不静默；表内
     *                 码必然满足 core 码句法，C-3 的句法半场因此前置满足）
     * @param subject  [in] 定位对象（optional——接口不强制：ERR-01"稳定诊断
     *                 绑定对象"的完整性核对归消费侧 testkit
     *                 checkDiagnosticRecord（allowTransient 选项）与
     *                 diagnostics/reporting 边界（core DiagData.hpp 同口径）；
     *                 传入时必须是 isValid() 的合法 ObjectId）
     * @param context  [in] 上下文描述（非空——C-3；执行语境标注，用户可见
     *                 文案权威归 diagnostics/ui）
     * @param cause    [in] 原因（非空——C-3；定位细节/原文保留在此承载）
     * @param recommendedAction [in] 建议动作（非空——C-3）
     * @param comparison [in] 比较型三要素（缺省空＝非比较型；给出时 actual/
     *                 expected 两侧的 unit 必须为已注册的有效句柄——UX-03
     *                 "单位必填"；数值侧四态不限：Provided/Invalid（原文
     *                 保留）/NotApplicable（显式不适用，不伪造数值）均为
     *                 §9.6 认可的显式语义）
     * @return 组装完成的诊断记录（值语义拷贝；字段与入参逐一同引用——
     *         不添加、不改写、不排序）
     *
     * @throws PolicyError 表外 code（PolicyObjectInvalid 码面——"工厂即时
     *         校验拒绝非法实例"语义，radUnit() 内部不变量违约同款先例；
     *         what() 携带违规码原文便于定位）、context/cause/
     *         recommendedAction 空串、或 comparison 给出而任一侧 unit 无效
     *         （同为调用方契约违约 fail-fast——诊断构造是进程内纯函数，
     *         无环境错误形态）
     *
     * 线程安全：const 纯函数，可重入。
     */
    virtual core::DiagnosticRecord make(std::string_view code, std::optional<core::ObjectId> subject,
        std::string context, std::string cause, std::string recommendedAction,
        std::optional<core::ComparativeFields> comparison = std::nullopt) const = 0;
};

// =====================================================================
// PolicyDiagnostics——唯一产品实现（无状态）。
// =====================================================================

/**
 * @brief IPolicyDiagnostics 的唯一产品实现（§9.6——码表核对＋core 工厂委托）。
 *
 * 无状态：全部核对是查表与判空——实例可默认构造、可拷贝、可跨线程共享
 * 只读（§9.6"生命周期：无状态"通用口径）。测试外的消费方经具体对象使用
 * （依赖注入点在调用方装配——本单元不设工厂单例：无状态类型的实例身份
 * 无语义，多实例与单实例行为逐字节一致）。
 */
class PolicyDiagnostics final : public IPolicyDiagnostics {
public:
    /// @copydoc IPolicyDiagnostics::registeredCodes
    std::vector<std::string> registeredCodes() const override;

    /// @copydoc IPolicyDiagnostics::make
    core::DiagnosticRecord make(std::string_view code, std::optional<core::ObjectId> subject,
        std::string context, std::string cause, std::string recommendedAction,
        std::optional<core::ComparativeFields> comparison = std::nullopt) const override;
};

// =====================================================================
// makeComparative 辅助组（§3.1 组成表"makeComparative 辅助"）。
// =====================================================================

/**
 * @brief 比较型三要素的单侧值构造（有限数值→Provided；非有限→Invalid
 *        保留原文）。
 *
 * 设计依据：UX-03（比较型三要素）＋§7.5/NFR-COR-03（非有限值不静默转 0、
 * 以原文保留——JointLimits.cpp v0.9 ⑤ 同款口径收敛为本单点）＋DYN-06
 * （来源标注是事实记录，不构成可信等级）。
 *
 * @param value    [in] 实测/期望数值（物理值，单位由 @p unit 标注；NaN/±Inf
 *                 合法入参——按 §7.5 走 Invalid 原文保留轨，不伪造数值）
 * @param unit     [in] 已注册单位句柄（调用方经 UnitToken::find 取得——本
 *                 函数不校验有效性，无效句柄在 make() 的比较型核对处拦截）
 * @param methodTag [in] 溯源方法短标记（core ValueProvenance 语法
 *                 [a-z0-9./_-]，如 "policy/joint-limits"、"policy-compat"；
 *                 违反语法时 ValueProvenance::make 抛 CoreError——调用方
 *                 契约违约，本函数不吞）
 * @return 单侧值（有限→Provided＋DerivedReadOnly 溯源；非有限→Invalid＋
 *         %.17g 原文——round-trip 精确、"C" locale 数字格式无本地分岔）
 *
 * 线程安全：纯函数。
 */
core::ComparativeValue makeComparativeValue(double value, core::UnitToken unit,
                                            std::string_view methodTag);

/**
 * @brief 显式不适用侧值（P-POL-2：阈值未设置→NotApplicable 标记——不伪造
 *        数值；§9.6"NotApplicable 输出经 SourcedValue::notApplicable 语义"
 *        的构造单点）。
 *
 * @param unit [in] 已注册单位句柄（量纲仍须标注——UX-03"单位必填"对不适用
 *             侧同样成立：不适用的是数值不是量纲）
 * @return NotApplicable 态单侧值（tryValue 恒空——checkDiagnosticRecord 的
 *         "NotApplicable 不得携带数值"核对由此保证）
 */
core::ComparativeValue makeNotApplicableValue(core::UnitToken unit);

/**
 * @brief 组装比较型三要素（actual/expected 两侧行为由单侧构造器决定——
 *        本函数纯聚合，不加语义）。
 *
 * @param actual   [in] 实际值侧（makeComparativeValue/makeNotApplicableValue 产物）
 * @param expected [in] 期望值侧（同上）
 * @return 三要素聚合（供 IPolicyDiagnostics::make 的 comparison 入参）
 */
core::ComparativeFields makeComparative(core::ComparativeValue actual,
                                        core::ComparativeValue expected);

}  // namespace sdurws::ird::policy

#endif  // SDURWS_IRD_POLICY_DIAGNOSTICS_HPP
