/**
 * @file   DiagData.cpp
 * @brief  诊断契约实现——C-3 码句法/必填串校验、C-1 比较型强制、C-2 凭据一致性。
 *
 * 设计依据：
 *   - units/core.md §4.8（不变量 C-1~C-3）/§5.7
 *   - 任务契约 tasks/foundation/CORE-T07.json（UT-DIAG 载体）
 */

#include <sdurws/ird/core/DiagData.hpp>

namespace sdurws::ird::core {
namespace {

/// 码句法校验：^[A-Z0-9]+(-[A-Z0-9]+)*$ 且 ≤64（§4.8 DiagCode 行）。
bool codeSyntaxOk(const std::string& code) noexcept
{
    if (code.empty() || code.size() > 64) { return false; }
    auto alnum = [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    };
    if (!alnum(code.front())) { return false; }
    bool prevDash = false;
    for (const char c : code) {
        if (alnum(c)) {
            prevDash = false;
        } else if (c == '-') {
            if (prevDash) { return false; }   // 连续连字符拒绝（[--] 形态）
            prevDash = true;
        } else {
            return false;                     // 小写/下划线/空白等一律拒绝
        }
    }
    return !prevDash;                          // 尾连字符拒绝
}

}  // namespace

DiagnosticRecord DiagnosticRecord::make(DiagCode code, std::optional<ObjectId> subject,
                                        std::optional<std::string> localName,
                                        std::optional<std::string> runtimeName,
                                        std::string context, std::string cause,
                                        std::string recommendedAction,
                                        std::optional<ComparativeFields> comparison)
{
    // C-3①：code 句法（§4.8 DiagCode 行——前缀 core/diag/code）。
    if (!codeSyntaxOk(code)) {
        throw CoreError("core/diag/code: 码句法非法（^[A-Z0-9]+(-[A-Z0-9]+)*$ 且 ≤64）: "
                        + code);
    }
    // C-3②：必填串非空（context/cause/recommendedAction 三字段——ERR-01 承载）。
    if (context.empty() || cause.empty() || recommendedAction.empty()) {
        throw CoreError("core/diag/required: context/cause/recommendedAction 均为必填非空");
    }
    DiagnosticRecord r;
    r.code = std::move(code);
    r.subject = std::move(subject);
    r.localName = std::move(localName);
    r.runtimeName = std::move(runtimeName);
    r.context = std::move(context);
    r.cause = std::move(cause);
    r.recommendedAction = std::move(recommendedAction);
    r.comparison = std::move(comparison);
    return r;
}

bool DiagnosticRecord::operator==(const DiagnosticRecord& o) const noexcept
{
    return code == o.code && subject == o.subject && localName == o.localName
        && runtimeName == o.runtimeName && context == o.context
        && cause == o.cause && recommendedAction == o.recommendedAction
        && comparison == o.comparison;
}

ConfirmableFinding ConfirmableFinding::make(DiagnosticRecord record)
{
    // C-1（SA-15）：可确认诊断必为比较型——无三要素则不可确认（直接走普通记录）。
    if (!record.comparison.has_value()) {
        throw CoreError("core/diag/c1: 可确认诊断必为比较型（comparison 缺失）");
    }
    ConfirmableFinding f;
    f.record = std::move(record);
    f.state = ConfirmationState::Pending;
    return f;
}

void ConfirmableFinding::confirm(ConfirmationCredential credential)
{
    // 状态机前置：仅 Pending 可确认（Confirmed/Rejected 的推进归 project §7.1）。
    if (state != ConfirmationState::Pending) {
        throw CoreError("core/diag/state: 仅 Pending 态可确认");
    }
    state = ConfirmationState::Confirmed;
    this->credential = std::move(credential);   // C-2：Confirmed ⇔ 凭据在场
                                                //（this-> 消歧——参数名与成员同名）
}

void ConfirmableFinding::reject()
{
    if (state != ConfirmationState::Pending) {
        throw CoreError("core/diag/state: 仅 Pending 态可否决");
    }
    state = ConfirmationState::Rejected;
    // C-2：Rejected 不得携带凭据——保证字段为空（如有历史残留则清）。
    credential.reset();
}

bool ConfirmableFinding::operator==(const ConfirmableFinding& o) const noexcept
{
    return record == o.record && state == o.state && credential == o.credential;
}

}  // namespace sdurws::ird::core
