/**
 * @file   ContractCheck.cpp
 * @brief  契约通用断言实现——诊断记录/比较型三要素/任务身份/身份唯一性/文件完整性。
 *
 * 设计依据：
 *   - units/testkit.md §5.5（签名）/§4.8（core 诊断语义）/§8 TK-CTR
 *   - 任务契约 tasks/foundation/TK-T07.json（≙WP-02-T07）
 */

#include <sdurws/ird/testkit/ContractCheck.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace sdurws::ird::testkit {
namespace {

/// 契约校验失败出口（fieldPath 列承载违规描述——CheckResult 复用口径）。
CheckResult failWith(std::string violation)
{
    CheckResult r;
    CompareDetail d;
    d.fieldPath = std::move(violation);
    r.failures.push_back(std::move(d));
    r.passed = false;
    return r;
}

/// 码句法校验（与 core DiagData 同规则——独立实现以校验其正确性，非复用）。
bool codeSyntaxOk(const std::string& code) noexcept
{
    if (code.empty() || code.size() > 64) { return false; }
    auto alnum = [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    };
    if (!alnum(code.front())) { return false; }
    bool prevDash = false;
    for (const char c : code) {
        if (alnum(c)) { prevDash = false; }
        else if (c == '-') {
            if (prevDash) { return false; }
            prevDash = true;
        } else { return false; }
    }
    return !prevDash;
}

/// SHA-256 十六进制（core ContentDigester——复用核心实现）。
std::string sha256HexOf(const std::string& bytes)
{
    core::ContentDigester d;
    d.update(bytes.data(), bytes.size());
    const auto digest = d.finalize();
    static constexpr char kHex[] = "0123456789abcdef";
    std::string s;
    s.reserve(64);
    for (const auto b : digest) {
        s.push_back(kHex[b >> 4]);
        s.push_back(kHex[b & 0x0F]);
    }
    return s;
}

}  // namespace

CheckResult checkDiagnosticRecord(const core::DiagnosticRecord& r,
                                  DiagnosticCheckOptions opt)
{
    CheckResult result;
    const auto add = [&result](std::string violation) {
        CompareDetail d;
        d.fieldPath = std::move(violation);
        result.failures.push_back(std::move(d));
        result.passed = false;
    };

    // code 句法（§5.5——与 core 同规则独立实现，校验器不得信任被校验方）。
    if (!codeSyntaxOk(r.code)) {
        add("code: 句法非法（^[A-Z0-9]+(-[A-Z0-9]+)*$ 且 ≤64）: " + r.code);
    }
    // 稳定诊断项 subject 必填（allowTransient 时可空——瞬时开发诊断）。
    if (!r.subject.has_value() && !opt.allowTransient) {
        add("subject: 稳定诊断项必须携带合法 ObjectId（allowTransient=false）");
    }
    if (r.subject.has_value() && !r.subject->isValid()) {
        add("subject: 保留值（全零）不是合法 ObjectId");
    }
    // 必填串非空。
    if (r.context.empty()) { add("context: 不得为空"); }
    if (r.cause.empty()) { add("cause: 不得为空"); }
    if (r.recommendedAction.empty()) { add("recommendedAction: 不得为空"); }
    // 比较型条目存在时三要素完整（unit 已注册）。
    if (r.comparison.has_value()) {
        for (const auto* side : {&r.comparison->actual, &r.comparison->expected}) {
            if (!core::UnitToken::find(side->unit.symbol()).has_value()) {
                add("comparison.unit: 单位未注册: " + std::string{side->unit.symbol()});
            }
            // NotApplicable 值不得伪造数值（SourcedValue 状态检查）：
            // NotApplicable 态 tryValue 必空——若实现退化返回数值即在此暴露。
            if (side->quantity.state() == core::FieldState::NotApplicable
                && side->quantity.tryValue().has_value()) {
                add("comparison.quantity: NotApplicable 态携带数值（伪造判定）");
            }
        }
    }
    return result;
}

CheckResult checkComparativeFields(const core::ComparativeFields& f,
                                   core::QuantityKind expectedKind)
{
    CheckResult result;
    const auto add = [&result](std::string violation) {
        CompareDetail d;
        d.fieldPath = std::move(violation);
        result.failures.push_back(std::move(d));
        result.passed = false;
    };
    // 量纲与 unit.token 的 kind 一致（expectedKind 由调用域给出——§5.5）。
    for (const auto* side : {&f.actual, &f.expected}) {
        if (side->unit.kind() != expectedKind) {
            add("unit.kind 与期望量纲不一致: " + std::string{side->unit.symbol()});
        }
    }
    return result;
}

CheckResult checkTaskIdentity(const core::TaskIdentity& id)
{
    CheckResult result;
    if (!id.isValid()) {
        CompareDetail d;
        d.fieldPath = "task: 五元组存在未设置字段（isValid=false）";
        result.failures.push_back(d);
        result.passed = false;
    }
    return result;
}

CheckResult checkStableIdsUnique(const std::vector<core::ObjectId>& ids)
{
    CheckResult result;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        for (std::size_t j = i + 1; j < ids.size(); ++j) {
            if (ids[i] == ids[j]) {
                CompareDetail d;
                d.fieldPath = "duplicate ObjectId: " + ids[i].toCanonical();
                result.failures.push_back(std::move(d));
                result.passed = false;
            }
        }
    }
    return result;
}

CheckResult checkFileIntegrity(const std::filesystem::path& file,
                               std::string_view sha256Expected)
{
    CheckResult result;
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        CompareDetail d;
        d.fieldPath = "file: 无法读取 " + file.string();
        result.failures.push_back(d);
        result.passed = false;
        return result;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string bytes = ss.str();
    const std::string actualHex = sha256HexOf(bytes);
    if (actualHex != sha256Expected) {
        CompareDetail d;
        d.fieldPath = "sha256: 文件被篡改或损坏（期望 " + std::string{sha256Expected}
                    + "，实际 " + actualHex + "）";
        result.failures.push_back(d);
        result.passed = false;
    }
    return result;
}

}  // namespace sdurws::ird::testkit
