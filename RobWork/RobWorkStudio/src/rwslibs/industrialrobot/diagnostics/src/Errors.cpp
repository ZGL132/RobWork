/**
 * @file   Errors.cpp
 * @brief  DiagnosticsError/token 的单点实现（头文件契约的实现侧）。
 *
 * 设计依据：
 *   - units/diagnostics.md §9.0（错误类型：token 逐值注释＋异常契约）；
 *   - 任务契约 tasks/foundation/DIAG-T03.json（≙WP-09-T03）；
 *   - 实现先例：evidence/src/Errors.cpp（token() switch 全枚举＋what() 拼装
 *     ——单元错误码面的同款纪律）。
 *
 * 确定性（NFR-COR-02）：token 表为编译期字面量，同码同串；what() 拼装规则
 * 固定（"<token>[: <detail>]"），同输入同消息。
 *
 * 线程安全：全部函数无共享可变状态；token 指向静态存储期字面量。
 */

#include <sdurws/ird/diagnostics/Errors.hpp>

namespace sdurws::ird::diagnostics {

namespace {

/**
 * @brief 拼装 what() 消息："<token>[: <detail>]"。
 *
 * 空细节不加尾随 ": "（消息恰为 token——EvidenceError 同款约定，避免
 * "token: " 这类悬空冒号污染日志与断言输出）。
 */
std::string makeWhat(std::string_view tokenText, const std::string& detail)
{
    if (detail.empty()) {
        return std::string{tokenText};
    }
    std::string what;
    what.reserve(tokenText.size() + 2 + detail.size());
    what.append(tokenText.data(), tokenText.size());
    what.append(": ");
    what.append(detail);
    return what;
}

}  // namespace

std::string_view token(DiagnosticsErrorCode code) noexcept
{
    // switch 全枚举（无 default）：新增枚举值而未登记 token 时，编译器以
    // "-Wswitch"（MSVC C4062 级告警）暴露遗漏——表驱动遗漏在编译期拦截，
    // 不靠运行期用例兜底（evidence::token 同款纪律）。
    switch (code) {
    case DiagnosticsErrorCode::DuplicateCode:
        // §9.0 注释原文：重复注册/重复转换规则（注册边界拒绝——NFR-MNT-03）。
        return "diagnostics/duplicate-code";
    case DiagnosticsErrorCode::CodeUnknown:
        // §9.0 注释原文：未注册码（工厂/查找）；注册期前缀冲突同码面（§9.1）。
        return "diagnostics/code-unknown";
    case DiagnosticsErrorCode::CodeDeprecated:
        // §9.0 注释原文：废弃码构造（tombstone 只读——§4.5.1，PA-2）。
        return "diagnostics/code-deprecated";
    case DiagnosticsErrorCode::SubjectMissing:
        // §9.0 注释原文：用户级码缺 subject（工厂前置——§9.2）。
        return "diagnostics/subject-missing";
    case DiagnosticsErrorCode::ComparisonMissing:
        // §9.0 注释原文：比较型码缺三要素（ERR-01/UX-03——core C-1 强化）。
        return "diagnostics/comparison-missing";
    case DiagnosticsErrorCode::CategoryMismatch:
        // §9.0 注释原文：非法分类/严重注入路径（severity 归属码表——§4.3）。
        return "diagnostics/category-mismatch";
    case DiagnosticsErrorCode::ParamSchemaMismatch:
        // §9.0 注释原文：参数模式不符（注册期验证＋实例占位一致性——§4.5/§9.2）。
        return "diagnostics/param-schema-mismatch";
    case DiagnosticsErrorCode::ContextMissing:
        // §9.0 注释原文：来源码必填上下文缺失（工厂前置——§9.2）。
        return "diagnostics/context-missing";
    case DiagnosticsErrorCode::InvalidState:
        // §9.0 注释原文：finding 状态机非法转移（§5.4——随 DIAG-T05 消费）。
        return "diagnostics/invalid-state";
    case DiagnosticsErrorCode::BindingMismatch:
        // §9.0 注释原文：绑定四元组复核失败（§5.3——随 DIAG-T05 消费）。
        return "diagnostics/binding-mismatch";
    case DiagnosticsErrorCode::Usage:
        // §9.0 注释原文：调用方违约（空参数等；运行期装配期调用——§9.1）。
        return "diagnostics/usage";
    }
    // 全枚举 switch 理论上不可达；MSVC 下 switch 穿出仍要求有返回值。
    // 走到此处＝枚举扩表而 token 未登记（编译器告警已拦截），运行期保守
    // 返回 Usage 的 token 而非 UB。
    return "diagnostics/usage";
}

DiagnosticsError::DiagnosticsError(DiagnosticsErrorCode code, std::string detail)
    : std::runtime_error(makeWhat(token(code), detail)), m_code(code)
{
}

}  // namespace sdurws::ird::diagnostics
