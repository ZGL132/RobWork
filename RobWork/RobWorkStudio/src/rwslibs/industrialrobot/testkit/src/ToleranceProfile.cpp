/**
 * @file   ToleranceProfile.cpp
 * @brief  容差档案实现——JsonLite 解析→schema 校验→逐条目规则→fieldPath 模板匹配。
 *
 * 设计依据：
 *   - units/testkit.md §4.3.1/§4.3.2/§4.3.3/§5.2
 *   - 任务契约 tasks/foundation/TK-T04.json（TK-TOL 载体）
 */

#include <sdurws/ird/testkit/ToleranceProfile.hpp>

#include <sdurws/ird/testkit/Dataset.hpp>    // parseSemanticVersion（语义化版本复用）
#include <sdurws/ird/testkit/JsonLite.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace sdurws::ird::testkit {
namespace {

[[noreturn]] void failField(const std::string& fieldPath, const std::string& reason)
{
    throw TestKitError(TestKitErrorKind::DatasetInvalid,
                       "dataset-invalid: " + fieldPath + ": " + reason);
}

std::string requireString(const JsonValue& obj, const std::string& key,
                          const std::string& pathPrefix)
{
    const JsonValue* v = obj.find(key);
    if (v == nullptr || !v->isString()) {
        failField(pathPrefix + key, "必填字段缺失或非字符串");
    }
    return v->text;
}

std::string requireString2(const JsonValue& obj, const std::string& key)
{
    const JsonValue* v = obj.find(key);
    if (v == nullptr || !v->isString()) {
        failField(key, "必填字段缺失或非字符串");
    }
    return v->text;
}

/// 全字符白名单匹配。
bool allCharsIn(std::string_view s, const std::string& allowed)
{
    return std::all_of(s.begin(), s.end(), [&](char c) {
        return allowed.find(c) != std::string::npos;
    });
}

core::Tolerance parseTolerance(const JsonValue& obj, const std::string& pathPrefix)
{
    const JsonValue* rel = obj.find("relative");
    const JsonValue* abs = obj.find("absolute");
    if (rel == nullptr || !rel->isNumber() || abs == nullptr || !abs->isNumber()) {
        failField(pathPrefix, "tolerance 须含 relative/absolute 数值");
    }
    try {
        return core::Tolerance::make(rel->number, abs->number);
    } catch (const core::CoreError& e) {
        failField(pathPrefix, std::string{"tolerance 分量非法: "} + e.what());
    }
}

std::optional<core::Tolerance> parseOptionalTolerance(const JsonValue& obj,
                                                      const std::string& key,
                                                      const std::string& pathPrefix)
{
    const JsonValue* v = obj.find(key);
    if (v == nullptr || v->isNull()) { return std::nullopt; }
    if (!v->isObject()) { failField(pathPrefix + key, "须为对象或 null"); }
    return parseTolerance(*v, pathPrefix + key + ".");
}

/// 是否两分量满足 a ≤ b（allowedMax 语义："只准更严"——逐分量）。
bool toleranceLeq(const core::Tolerance& a, const core::Tolerance& b) noexcept
{
    return a.relative <= b.relative && a.absolute <= b.absolute;
}

/// QuantityKind → token（§4.4 量纲 token——与 core 单位表量纲同源）。
const char* kindTokenOf(core::QuantityKind k) noexcept
{
    switch (k) {
    case core::QuantityKind::Length:               return "length";
    case core::QuantityKind::Angle:                return "angle";
    case core::QuantityKind::Mass:                 return "mass";
    case core::QuantityKind::Time:                 return "time";
    case core::QuantityKind::Force:                return "force";
    case core::QuantityKind::Torque:               return "torque";
    case core::QuantityKind::Inertia:              return "inertia";
    case core::QuantityKind::Power:                return "power";
    case core::QuantityKind::LinearVelocity:       return "linear-velocity";
    case core::QuantityKind::AngularVelocity:      return "angular-velocity";
    case core::QuantityKind::LinearAcceleration:   return "linear-acceleration";
    case core::QuantityKind::AngularAcceleration:  return "angular-acceleration";
    case core::QuantityKind::Voltage:              return "voltage";
    case core::QuantityKind::Dimensionless:        return "dimensionless";
    }
    return "unknown";
}

/// token → QuantityKind（try 轨；未知 nullopt）。
std::optional<core::QuantityKind> kindFromToken(std::string_view token) noexcept
{
    if (token == "length")               { return core::QuantityKind::Length; }
    if (token == "angle")                { return core::QuantityKind::Angle; }
    if (token == "mass")                 { return core::QuantityKind::Mass; }
    if (token == "time")                 { return core::QuantityKind::Time; }
    if (token == "force")                { return core::QuantityKind::Force; }
    if (token == "torque")               { return core::QuantityKind::Torque; }
    if (token == "inertia")              { return core::QuantityKind::Inertia; }
    if (token == "power")                { return core::QuantityKind::Power; }
    if (token == "linear-velocity")      { return core::QuantityKind::LinearVelocity; }
    if (token == "angular-velocity")     { return core::QuantityKind::AngularVelocity; }
    if (token == "linear-acceleration")  { return core::QuantityKind::LinearAcceleration; }
    if (token == "angular-acceleration") { return core::QuantityKind::AngularAcceleration; }
    if (token == "voltage")              { return core::QuantityKind::Voltage; }
    if (token == "dimensionless")        { return core::QuantityKind::Dimensionless; }
    return std::nullopt;
}

}  // namespace

const char* toToken(ToleranceSource s) noexcept
{
    switch (s) {
    case ToleranceSource::AppendixDFixed:           return "appendixD-fixed";
    case ToleranceSource::AnalysisConfigDefault:    return "analysis-config-default";
    case ToleranceSource::EngineeringPolicyDefault: return "engineering-policy-default";
    case ToleranceSource::DatasetDeclared:        return "dataset-declared";
    }
    return "unknown";
}

std::optional<ToleranceSource> toleranceSourceFromToken(std::string_view token) noexcept
{
    if (token == "appendixD-fixed")           { return ToleranceSource::AppendixDFixed; }
    if (token == "analysis-config-default")   { return ToleranceSource::AnalysisConfigDefault; }
    if (token == "engineering-policy-default") { return ToleranceSource::EngineeringPolicyDefault; }
    if (token == "dataset-declared")          { return ToleranceSource::DatasetDeclared; }
    return std::nullopt;
}

ToleranceProfile ToleranceProfile::load(const std::filesystem::path& profileVersionJson)
{
    if (!std::filesystem::exists(profileVersionJson)) {
        throw TestKitError(TestKitErrorKind::DatasetInvalid,
                           "dataset-invalid: 档案文件不存在: "
                               + profileVersionJson.string());
    }
    std::ifstream in(profileVersionJson, std::ios::binary);
    if (!in) {
        throw TestKitError(TestKitErrorKind::EnvUnavailable,
                           "env-unavailable: 档案无法读取: " + profileVersionJson.string());
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    const JsonValue root = parseJson(ss.str());
    if (!root.isObject()) { failField("$", "档案顶层须为对象"); }

    ToleranceProfile p;
    p.profileId = requireString2(root, "profileId");
    if (p.profileId.size() < 3 || p.profileId.size() > 64
        || !allCharsIn(p.profileId, "abcdefghijklmnopqrstuvwxyz0123456789-")) {
        failField("profileId", "句法非法（[a-z0-9-]{3,64}）: " + p.profileId);
    }
    // profileId 与祖父目录名一致（json 父目录是版本目录——§4.3.1 布局）。
    const auto profileDir = profileVersionJson.parent_path().filename().string();
    if (p.profileId != profileDir) {
        failField("profileId", "与目录名不一致: " + p.profileId + " != " + profileDir);
    }

    p.version = requireString2(root, "version");
    int maj = 0, mi = 0, pa = 0;
    parseSemanticVersion(p.version, "version", &maj, &mi, &pa);
    // 文件名与 version 一致（v<version>.json——§4.3.1）。
    const auto fileName = profileVersionJson.filename().string();
    if (fileName != "v" + p.version + ".json") {
        failField("version", "与文件名不一致（期望 v" + p.version + ".json）: " + fileName);
    }

    p.basis = requireString2(root, "basis");
    if (p.basis.empty()) { failField("basis", "依据声明不得为空"); }

    const JsonValue* entries = root.find("entries");
    if (entries == nullptr || !entries->isArray() || entries->items.empty()) {
        failField("entries", "必填数组字段缺失或为空（≥1）");
    }

    std::size_t index = 0;
    for (const auto& item : entries->items) {
        if (!item.isObject()) { failField("entries", "条目须为对象"); }
        const std::string prefix = "entries[" + std::to_string(index) + "].";
        ToleranceEntry e;
        e.fieldPath = requireString(item, "fieldPath", prefix);
        if (e.fieldPath.empty()) { failField(prefix + "fieldPath", "不得为空"); }

        // quantityKind：以 core 词表 token 校验——存在某注册单位其量纲 token 等于
        // 该值即合法（量纲 token 集＝十四类，无第二词表）。
        const auto kindToken = requireString(item, "quantityKind", prefix);
        static const char* kProbeTokens[] = {"m", "rad", "kg", "s", "N", "N*m",
            "kg*m^2", "W", "m/s", "rad/s", "m/s^2", "rad/s^2", "V", "1"};
        bool kindOk = false;
        for (const auto* probe : kProbeTokens) {
            const auto probeUnit = core::UnitToken::find(probe);
            if (probeUnit.has_value()
                && std::string_view{kindTokenOf(probeUnit->kind())} == kindToken) {
                kindOk = true;
                break;
            }
        }
        if (!kindOk) { failField(prefix + "quantityKind", "未知量纲 token: " + kindToken); }

        // unit：须已注册（§4.3.1——经 core 单位表产生标签）。
        const auto unitTokenStr = requireString(item, "unit", prefix);
        const auto unit = core::UnitToken::find(unitTokenStr);
        if (!unit.has_value()) {
            failField(prefix + "unit", "单位未注册: " + unitTokenStr);
        }
        e.unit = *unit;

        // quantityKind 与 unit 的量纲必须一致（§4.3.3：不符→unit-mismatch，
        // 数据集非法级——在装载期即拒绝，而非比较期）。
        if (unit->kind() != kindFromToken(kindToken)) {
            failField(prefix + "quantityKind", "quantityKind 与 unit 量纲不符（"
                      + kindToken + " vs " + unitTokenStr + "）");
        }

        // tolerance 与 allowedMax。
        const JsonValue* tol = item.find("tolerance");
        if (tol == nullptr || !tol->isObject()) {
            failField(prefix + "tolerance", "必填对象字段缺失");
        }
        e.tolerance = parseTolerance(*tol, prefix + "tolerance.");
        e.allowedMax = parseOptionalTolerance(item, "allowedMax", prefix);

        // source 白名单。
        const auto sourceToken = requireString(item, "source", prefix);
        const auto source = toleranceSourceFromToken(sourceToken);
        if (!source.has_value()) {
            failField(prefix + "source", "未知来源类别: " + sourceToken);
        }
        e.source = *source;

        // allowedMax 条件规则（§4.3.1/§4.3.2）：
        //   appendixD-fixed：必填（＝附录 D 对应值，只准更严）；
        //   dataset-declared：必填（附依据）；
        //   其余两类可选（默认值归属被测单元配置/策略对象——档案只登记）。
        if (e.source == ToleranceSource::AppendixDFixed
            || e.source == ToleranceSource::DatasetDeclared) {
            if (!e.allowedMax.has_value()) {
                failField(prefix + "allowedMax",
                          std::string{"该 source 类别下必填（"}
                              + toToken(e.source) + "）");
            }
        }
        // tolerance ≤ allowedMax（只准更严——放宽即档案非法，§4.3.2）。
        if (e.allowedMax.has_value() && !toleranceLeq(e.tolerance, *e.allowedMax)) {
            failField(prefix + "tolerance", "超出 allowedMax（数据集只准更严、不准放宽）");
        }

        p.entries.push_back(std::move(e));
        ++index;
    }
    return p;
}

const ToleranceEntry& ToleranceProfile::resolve(std::string_view concreteFieldPath) const
{
    // 模板匹配：段数相同；'*' 段匹配"前缀[任意索引]"；其余段逐字符相等。
    const auto split = [](std::string_view path) {
        std::vector<std::string_view> parts;
        std::size_t start = 0;
        while (true) {
            const auto dot = path.find('.', start);
            parts.push_back(path.substr(start, dot - start));
            if (dot == std::string_view::npos) { break; }
            start = dot + 1;
        }
        return parts;
    };

    for (const auto& entry : entries) {
        const auto entryParts = split(entry.fieldPath);
        const auto concreteParts = split(concreteFieldPath);
        if (entryParts.size() != concreteParts.size()) { continue; }
        bool all = true;
        for (std::size_t i = 0; i < entryParts.size() && all; ++i) {
            const auto& tp = entryParts[i];
            const auto& cp = concreteParts[i];
            if (tp == cp) { continue; }
            const auto star = tp.find('*');
            if (star == std::string_view::npos) { all = false; continue; }
            // '*' 段：模板去掉 '*' 后为前缀（如 fk[*]→fk[），具体段须以该前缀开始
            // 且以 ']' 结束（fk[3] ✓；fk.x ✗）。
            const auto prefix = tp.substr(0, star);
            if (cp.size() <= prefix.size() || cp.substr(0, prefix.size()) != prefix
                || cp.back() != ']') {
                all = false;
            }
        }
        if (all) { return entry; }
    }
    // 未命中：不默认、不通过（附录 D C4——tolerance-undefined，数据集非法级）。
    throw TestKitError(TestKitErrorKind::ToleranceUndefined,
                       std::string{"tolerance-undefined: fieldPath 未命中容差条目: "}
                           + std::string{concreteFieldPath});
}

}  // namespace sdurws::ird::testkit
