/**
 * @file   Errors.cpp
 * @brief  policy 错误码稳定 token 表的唯一定义点（POL-T02；POL-T03/T05/T06/
 *         T07 随表尾追加同步维护——追加留痕见枚举注释）。建议注册码
 *         （registryCode）自 POL-T10 起委托 Diagnostics.cpp 的建议码表：
 *         token 字面量仍唯一在本 TU，POLICY-* 建议码字面量唯一在
 *         Diagnostics.cpp（NFR-MNT-03 单一权威——两表面经映射函数衔接，
 *         字符串行为不变，PolicySetTest 两张全表锚定用例钉住）。
 *
 * 设计依据：
 *   - units/policy.md §3.1（Errors.hpp/.cpp 为 POL-T02 产物）、§5.2（诊断码
 *     建议值逐行——token 派生源）、§9.6（POLICY-* 建议码清单——建议码
 *     字面量的权威来源，POL-T10 落位 Diagnostics.hpp 码表后本 TU 不再
 *     持有第二份字面量）
 *   - runtime/evidence 同款实现纪律：编译期固定 switch 全枚举、无 default
 *     （新增枚举值未登记表项时编译器告警暴露遗漏）；测试侧全表用例逐值钉住
 *
 * 确定性（NFR-COR-02）：switch 全枚举返回静态存储期字面量——同码同串、
 * 跨平台跨进程逐字节一致、无 locale 依赖。线程安全：纯函数（可重入）。
 */

#include <sdurws/ird/policy/Errors.hpp>

#include <sdurws/ird/policy/Diagnostics.hpp>

namespace sdurws::ird::policy {

namespace {

/**
 * @brief 异常码 → 建议码枚举的映射（registryCode 的委托桥）。
 *
 * 全枚举 switch、无 default（新增 PolicyErrorCode 未登记映射时 MSVC C4062
 * 告警暴露——与 token() 同款纪律）。一一对应：每个异常码恰好发一个建议码
 * （§9.6 清单＋表尾补登——对应关系与 POL-T02~T07 的 registryCode 字面量
 * 表逐行一致，未新增/未改写任何映射）。
 */
PolicyDiagCode diagCodeOf(PolicyErrorCode code) noexcept
{
    switch (code) {
    case PolicyErrorCode::SchemaUnknownField:       return PolicyDiagCode::SchemaUnknownField;
    case PolicyErrorCode::SchemaVersionFuture:      return PolicyDiagCode::SchemaVersionFuture;
    case PolicyErrorCode::SchemaVersionUnknown:     return PolicyDiagCode::SchemaVersionUnknown;
    case PolicyErrorCode::ThresholdNonFinite:       return PolicyDiagCode::ThresholdNonFinite;
    case PolicyErrorCode::ThresholdNonPositive:     return PolicyDiagCode::ThresholdNonPositive;
    case PolicyErrorCode::ThresholdOutOfRange:      return PolicyDiagCode::ThresholdOutOfRange;
    case PolicyErrorCode::ThresholdRequiredMissing: return PolicyDiagCode::ThresholdRequiredMissing;
    case PolicyErrorCode::UnitMismatch:             return PolicyDiagCode::UnitMismatch;
    case PolicyErrorCode::RuleDuplicate:            return PolicyDiagCode::RuleDuplicate;
    case PolicyErrorCode::RuleConflict:             return PolicyDiagCode::RuleConflict;
    case PolicyErrorCode::RuleCycle:                return PolicyDiagCode::RuleCycle;
    case PolicyErrorCode::ScopeObjectMissing:       return PolicyDiagCode::ScopeObjectMissing;
    case PolicyErrorCode::ApplicabilityInvalid:     return PolicyDiagCode::ApplicabilityInvalid;
    case PolicyErrorCode::PolicyObjectInvalid:      return PolicyDiagCode::PolicyObjectInvalid;
    case PolicyErrorCode::EncodingInvalid:          return PolicyDiagCode::EncodingInvalid;
    case PolicyErrorCode::PortAssemblyIncomplete:   return PolicyDiagCode::PortAssemblyIncomplete;
    case PolicyErrorCode::SceneInvalid:             return PolicyDiagCode::CllSceneInvalid;
    case PolicyErrorCode::NameUnresolved:           return PolicyDiagCode::CllNameUnresolved;
    case PolicyErrorCode::QueryInvalid:             return PolicyDiagCode::QueryInvalid;
    }
    // 不可达路径：全枚举已覆盖（同 token() 出口说明——SchemaUnknownField
    // 仅为满足编译器；运行期永不可达，测试全表保证）。
    return PolicyDiagCode::SchemaUnknownField;
}

}  // namespace

std::string_view token(PolicyErrorCode code) noexcept
{
    // 稳定 token 全表（顺序＝枚举声明顺序；字符串＝"policy/"＋kebab-case）。
    // 无 default 分支：未来表尾追加新枚举值而忘记登记此表时，MSVC /W4
    // （C4062 全枚举 switch 缺项告警）在构建期即暴露——码面稳定性由编译器
    // 与测试双保险（runtime Errors.cpp 同款）。
    switch (code) {
    case PolicyErrorCode::SchemaUnknownField:       return "policy/schema-unknown-field";
    case PolicyErrorCode::SchemaVersionFuture:      return "policy/schema-version-future";
    case PolicyErrorCode::SchemaVersionUnknown:     return "policy/schema-version-unknown";
    case PolicyErrorCode::ThresholdNonFinite:       return "policy/threshold-non-finite";
    case PolicyErrorCode::ThresholdNonPositive:     return "policy/threshold-non-positive";
    case PolicyErrorCode::ThresholdOutOfRange:      return "policy/threshold-out-of-range";
    case PolicyErrorCode::ThresholdRequiredMissing: return "policy/threshold-required-missing";
    case PolicyErrorCode::UnitMismatch:             return "policy/unit-mismatch";
    case PolicyErrorCode::RuleDuplicate:            return "policy/rule-duplicate";
    case PolicyErrorCode::RuleConflict:             return "policy/rule-conflict";
    case PolicyErrorCode::RuleCycle:                return "policy/rule-cycle";
    case PolicyErrorCode::ScopeObjectMissing:       return "policy/scope-object-missing";
    case PolicyErrorCode::ApplicabilityInvalid:     return "policy/applicability-invalid";
    case PolicyErrorCode::PolicyObjectInvalid:      return "policy/policy-object-invalid";
    case PolicyErrorCode::EncodingInvalid:          return "policy/encoding-invalid";
    case PolicyErrorCode::PortAssemblyIncomplete:   return "policy/port-assembly-incomplete";
    // POL-T06 表尾追加（§9.6 CLL 家族落位——会话构建期场景/名称校验）。
    case PolicyErrorCode::SceneInvalid:             return "policy/cll-scene-invalid";
    case PolicyErrorCode::NameUnresolved:           return "policy/cll-name-unresolved";
    // POL-T07 表尾追加（评估期查询契约违约——§9.3 错误类型行评估半区载体）。
    case PolicyErrorCode::QueryInvalid:             return "policy/cll-query-invalid";
    }
    // 不可达路径：全枚举已覆盖。返回空串仅为满足编译器（无 default 时
    // 控制流分析仍要求出口感）；测试全表用例保证该路径永不在运行期出现。
    return {};
}

std::string_view registryCode(PolicyErrorCode code) noexcept
{
    // 建议注册码（POL-T10 起委托 Diagnostics.cpp 建议码表——NFR-MNT-03
    // 单一权威：POLICY-* 字面量全单元唯一在该表；本函数经 diagCodeOf 映射
    // 取码，字符串行为与 POL-T02~T07 的字面量表逐串一致——PolicySetTest
    // RegistryCodeTableFullEnumeration 全表锚定钉住）。码值权威＝diagnostics
    // StableCodeRegistry——本函数仅"建议"（P-PR-6 同模式，PA-1 不越权注册）；
    // 正式收编由 diagnostics 所有者裁决。
    return policyDiagCode(diagCodeOf(code));
}

// =====================================================================
// PolicyError——构造函数（消息前缀强制拼装的唯一位置）。
// =====================================================================

namespace {

/// 拼装 what() 消息："<token>[: <detail>]"（detail 空时恰为 token，无尾随
/// 冒号空格）。置于匿名命名空间的自由函数而非成员初始化式内联 lambda——
/// 规避 MSVC 对构造函数成员初始化列表中立即调用 lambda 的内部编译器错误
/// （实测触发；runtime/evidence 同款消息拼装逻辑的实现形态差异，语义一致）。
std::string buildWhatMessage(PolicyErrorCode code, const std::string& detail)
{
    // token 由本构造点强制拼装，调用方无法构造"裸消息"异常（前缀约定不变式）。
    const std::string_view t = token(code);
    if (detail.empty()) {
        return std::string{t};
    }
    std::string msg;
    msg.reserve(t.size() + 2 + detail.size());
    msg.append(t).append(": ").append(detail);
    return msg;
}

}  // namespace

PolicyError::PolicyError(PolicyErrorCode code, std::string detail)
    : std::runtime_error(buildWhatMessage(code, detail))
    , m_code(code)
{
}

}  // namespace sdurws::ird::policy
