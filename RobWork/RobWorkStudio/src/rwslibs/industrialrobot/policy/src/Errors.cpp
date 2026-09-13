/**
 * @file   Errors.cpp
 * @brief  policy 错误码稳定 token 表与建议注册码表的唯一定义点（POL-T02）。
 *
 * 设计依据：
 *   - units/policy.md §3.1（Errors.hpp/.cpp 为 POL-T02 产物）、§5.2（诊断码
 *     建议值逐行——token 派生源）、§9.6（POLICY-* 建议码清单）
 *   - runtime/evidence 同款实现纪律：编译期固定 switch 全枚举、无 default
 *     （新增枚举值未登记表项时编译器告警暴露遗漏）；测试侧全表用例逐值钉住
 *
 * 确定性（NFR-COR-02）：switch 全枚举返回静态存储期字面量——同码同串、
 * 跨平台跨进程逐字节一致、无 locale 依赖。线程安全：纯函数（可重入）。
 */

#include <sdurws/ird/policy/Errors.hpp>

namespace sdurws::ird::policy {

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
    }
    // 不可达路径：全枚举已覆盖。返回空串仅为满足编译器（无 default 时
    // 控制流分析仍要求出口感）；测试全表用例保证该路径永不在运行期出现。
    return {};
}

std::string_view registryCode(PolicyErrorCode code) noexcept
{
    // 建议注册码全表（§9.6 建议码清单逐项＋表尾追加值的补登建议）。
    // 码值权威＝diagnostics StableCodeRegistry——本表仅"建议"（P-PR-6 同
    // 模式，PA-1 不越权注册）；正式收编由 diagnostics 所有者裁决。
    switch (code) {
    case PolicyErrorCode::SchemaUnknownField:       return "POLICY-SCHEMA-UNKNOWN-FIELD";
    case PolicyErrorCode::SchemaVersionFuture:      return "POLICY-SCHEMA-VERSION-FUTURE";
    case PolicyErrorCode::SchemaVersionUnknown:     return "POLICY-SCHEMA-VERSION-UNKNOWN";
    case PolicyErrorCode::ThresholdNonFinite:       return "POLICY-THRESHOLD-NON-FINITE";
    case PolicyErrorCode::ThresholdNonPositive:     return "POLICY-THRESHOLD-NON-POSITIVE";
    case PolicyErrorCode::ThresholdOutOfRange:      return "POLICY-THRESHOLD-OUT-OF-RANGE";
    case PolicyErrorCode::ThresholdRequiredMissing: return "POLICY-THRESHOLD-REQUIRED-MISSING";
    case PolicyErrorCode::UnitMismatch:             return "POLICY-UNIT-MISMATCH";
    case PolicyErrorCode::RuleDuplicate:            return "POLICY-RULE-DUPLICATE";
    case PolicyErrorCode::RuleConflict:             return "POLICY-RULE-CONFLICT";
    case PolicyErrorCode::RuleCycle:                return "POLICY-RULE-CYCLE";
    case PolicyErrorCode::ScopeObjectMissing:       return "POLICY-SCOPE-OBJECT-MISSING";
    case PolicyErrorCode::ApplicabilityInvalid:     return "POLICY-APPLICABILITY-INVALID";
    case PolicyErrorCode::PolicyObjectInvalid:      return "POLICY-POLICY-OBJECT-INVALID";
    case PolicyErrorCode::EncodingInvalid:          return "POLICY-ENCODING-INVALID";
    }
    // 不可达路径：同 token() 说明。
    return {};
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
