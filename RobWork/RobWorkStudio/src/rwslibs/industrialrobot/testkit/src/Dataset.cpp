/**
 * @file   Dataset.cpp
 * @brief  Dataset 装载实现——JsonLite 解析→schema 校验→完整性（ContentDigester）→交叉校验。
 *
 * 设计依据：
 *   - units/testkit.md §4.2.2（字段表逐行约束）/§4.5（size＋SHA-256）/§5.1（流程）
 *   - 错误统一 TestKitError(DatasetInvalid)，消息含字段路径（TK-MAN/TK-INT 口径）
 */

#include <sdurws/ird/testkit/Dataset.hpp>

#include <sdurws/ird/testkit/JsonLite.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace sdurws::ird::testkit {
namespace {

using core::ContentDigester;

/// 字段路径错误出口（全部校验失败统一走此——DatasetInvalid＋路径＋原因）。
[[noreturn]] void failField(const std::string& fieldPath, const std::string& reason)
{
    throw TestKitError(TestKitErrorKind::DatasetInvalid,
                       "dataset-invalid: " + fieldPath + ": " + reason);
}

/// 必填字符串字段。
std::string requireString(const JsonValue& obj, const std::string& key,
                          const std::string& pathPrefix)
{
    const JsonValue* v = obj.find(key);
    if (v == nullptr || !v->isString()) {
        failField(pathPrefix + key, "必填字段缺失或非字符串");
    }
    return v->text;
}

std::string requireString(const JsonValue& obj, const std::string& key)
{
    return requireString(obj, key, "");
}

/// 字符串数组字段（minCount=0 表示可缺省）。
std::vector<std::string> requireStringArray(const JsonValue& obj, const std::string& key,
                                            std::size_t minCount)
{
    const JsonValue* v = obj.find(key);
    if (v == nullptr || !v->isArray()) {
        if (minCount == 0) { return {}; }
        failField(key, "必填数组字段缺失或非数组");
    }
    if (v->items.size() < minCount) {
        failField(key, "至少 " + std::to_string(minCount) + " 个元素");
    }
    std::vector<std::string> out;
    for (const auto& item : v->items) {
        if (!item.isString()) { failField(key, "数组元素须为字符串"); }
        out.push_back(item.text);
    }
    return out;
}

/// 布尔字段。
bool requireBool(const JsonValue& obj, const std::string& key)
{
    const JsonValue* v = obj.find(key);
    if (v == nullptr || !v->isBool()) {
        failField(key, "必填布尔字段缺失或非布尔");
    }
    return v->boolean;
}

/// 简单模式校验（全字符集匹配，无重复实现正则的必要——语法白名单逐字符）。
bool allCharsIn(std::string_view s, const std::string& allowed)
{
    return std::all_of(s.begin(), s.end(), [&](char c) {
        return allowed.find(c) != std::string::npos;
    });
}

/// 读取文件字节（读失败抛 EnvUnavailable——目录/权限属环境问题）。
std::string readFileBytes(const std::filesystem::path& p)
{
    std::ifstream in(p, std::ios::binary);
    if (!in) {
        throw TestKitError(TestKitErrorKind::EnvUnavailable,
                           "env-unavailable: 无法读取文件 " + p.string());
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

/// SHA-256 十六进制（core ContentDigester——TK-T01 依赖链）。
std::string sha256HexOf(const std::string& bytes)
{
    ContentDigester d;
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

// ---- token 映射（§4.2.1 五类） ----
const char* toToken(DatasetKind k) noexcept
{
    switch (k) {
    case DatasetKind::AnalyticCase:        return "analytic-case";
    case DatasetKind::ReferenceImpl:       return "reference-impl";
    case DatasetKind::ContractFixture:     return "contract-fixture";
    case DatasetKind::Regression:          return "regression";
    case DatasetKind::PerformanceBaseline: return "performance-baseline";
    }
    return "unknown";
}

std::optional<DatasetKind> datasetKindFromToken(std::string_view token) noexcept
{
    if (token == "analytic-case")        { return DatasetKind::AnalyticCase; }
    if (token == "reference-impl")       { return DatasetKind::ReferenceImpl; }
    if (token == "contract-fixture")     { return DatasetKind::ContractFixture; }
    if (token == "regression")           { return DatasetKind::Regression; }
    if (token == "performance-baseline") { return DatasetKind::PerformanceBaseline; }
    return std::nullopt;
}

void parseSemanticVersion(std::string_view v, std::string_view fieldPath,
                          int* major, int* minor, int* patch)
{
    int vals[3] = {0, 0, 0};
    std::size_t idx = 0;
    std::size_t consumed = 0;
    for (int part = 0; part < 3; ++part) {
        std::size_t start = consumed;
        while (consumed < v.size() && v[consumed] >= '0' && v[consumed] <= '9') { ++consumed; }
        if (consumed == start) {
            throw TestKitError(TestKitErrorKind::DatasetInvalid,
                               std::string("dataset-invalid: ") + std::string(fieldPath)
                                   + ": 语义化版本段缺失数字（期望 M.m.p）: " + std::string{v});
        }
        vals[part] = std::stoi(std::string{v.substr(start, consumed - start)});
        if (part < 2) {
            if (consumed >= v.size() || v[consumed] != '.') {
                throw TestKitError(TestKitErrorKind::DatasetInvalid,
                                   std::string("dataset-invalid: ") + std::string(fieldPath)
                                       + ": 版本分隔符缺失（期望 M.m.p）: " + std::string{v});
            }
            ++consumed;
        }
    }
    if (consumed != v.size()) {
        throw TestKitError(TestKitErrorKind::DatasetInvalid,
                           std::string("dataset-invalid: ") + std::string(fieldPath)
                               + ": 版本含尾随内容: " + std::string{v});
    }
    *major = vals[0];
    *minor = vals[1];
    *patch = vals[2];
    (void)idx;
}

bool DatasetManifest::operator==(const DatasetManifest& o) const
{
    // 逐字段全等（向量/成对容器深比较）——测试一致性断言用。
    return schemaVersion == o.schemaVersion && datasetId == o.datasetId
        && version == o.version && kind == o.kind
        && scenarioCategory == o.scenarioCategory
        && coveredRequirements == o.coveredRequirements && coveredAt == o.coveredAt
        && toleranceProfileId == o.toleranceProfileId
        && toleranceProfileVersion == o.toleranceProfileVersion
        && inputs == o.inputs && expected == o.expected
        && parametersJson == o.parametersJson && units == o.units && frames == o.frames
        && referenceSourcePresent == o.referenceSourcePresent
        && referenceSource == o.referenceSource
        && producerPresent == o.producerPresent && producer == o.producer
        && edgeCasesPresent == o.edgeCasesPresent && edgeCases.sampleRefs == o.edgeCases.sampleRefs
        && edgeCases.zeroValue == o.edgeCases.zeroValue
        && edgeCases.nearZero == o.edgeCases.nearZero
        && edgeCases.signCancellation == o.edgeCases.signCancellation
        && integrity == o.integrity && generatorPresent == o.generatorPresent
        && generatorScript == o.generatorScript
        && generatorInvocation == o.generatorInvocation
        && generatorCommitted == o.generatorCommitted && history == o.history;
}

GoldenDataset GoldenDataset::load(const DatasetRef& ref)
{
    // 引用合法性（datasetId 句法——与目录名一致的前提）。
    if (ref.datasetId.size() < 3 || ref.datasetId.size() > 64
        || !allCharsIn(ref.datasetId, "abcdefghijklmnopqrstuvwxyz0123456789-")) {
        failField("datasetId", "引用句法非法（[a-z0-9-]{3,64}）: " + ref.datasetId);
    }
    const std::filesystem::path versionDir =
        goldenDataRoot() / "golden" / ref.datasetId / ref.version;
    const auto manifestPath = versionDir / "manifest.json";
    if (!std::filesystem::exists(manifestPath)) {
        throw TestKitError(TestKitErrorKind::DatasetInvalid,
                           "dataset-invalid: manifest 不存在: " + manifestPath.string());
    }

    // 第一步：解析（TK-T02 JsonLite——行列错误已含定位）。
    const auto text = readFileBytes(manifestPath);
    const JsonValue root = parseJson(text);
    if (!root.isObject()) {
        failField("$", "manifest 顶层须为对象");
    }

    GoldenDataset out;
    out.versionDir_ = versionDir;
    DatasetManifest& m = out.manifest_;

    // ---- 顶层必填串字段（§4.2.2 表逐行） ----
    m.schemaVersion = requireString(root, "schemaVersion");
    static constexpr char kSchemaPrefix[] = "ird-golden-manifest/";
    if (m.schemaVersion.rfind(kSchemaPrefix, 0) != 0) {
        failField("schemaVersion", "前缀须为 ird-golden-manifest/: " + m.schemaVersion);
    }
    {
        // schema 后缀仅主版本段（"ird-golden-manifest/1"）——非 M.m.p 形态，单段整数校验。
        const std::string majorStr = m.schemaVersion.substr(sizeof(kSchemaPrefix) - 1);
        if (majorStr.empty() || !std::all_of(majorStr.begin(), majorStr.end(),
            [](char c) { return c >= '0' && c <= '9'; })) {
            failField("schemaVersion", "主版本段须为整数: " + m.schemaVersion);
        }
        if (std::stoi(majorStr) != 1) {
            failField("schemaVersion", "未知主版本（当前支持 1）: " + m.schemaVersion);
        }
    }
    m.datasetId = requireString(root, "datasetId");
    if (m.datasetId.size() < 3 || m.datasetId.size() > 64
        || !allCharsIn(m.datasetId, "abcdefghijklmnopqrstuvwxyz0123456789-")) {
        failField("datasetId", "句法非法（[a-z0-9-]{3,64}）: " + m.datasetId);
    }
    // datasetId 与目录名一致（§4.2.2 + lint 口径）。
    if (m.datasetId != ref.datasetId) {
        failField("datasetId", "与目录名不一致: " + m.datasetId + " != " + ref.datasetId);
    }
    {
        int maj = 0, mi = 0, pa = 0;
        m.version = requireString(root, "version");
        parseSemanticVersion(m.version, "version", &maj, &mi, &pa);
        if (ref.version != m.version) {
            failField("version", "与版本目录名不一致: " + m.version + " != " + ref.version);
        }
    }

    // kind（五 token 白名单）。
    const auto kindToken = requireString(root, "kind");
    const auto kind = datasetKindFromToken(kindToken);
    if (!kind.has_value()) {
        failField("kind", "未知数据集类别: " + kindToken);
    }
    m.kind = *kind;

    m.scenarioCategory = requireString(root, "scenarioCategory");
    if (m.scenarioCategory.empty() || m.scenarioCategory.size() > 64
        || !allCharsIn(m.scenarioCategory, "abcdefghijklmnopqrstuvwxyz0123456789-/")) {
        failField("scenarioCategory", "句法非法（[a-z0-9-]{1,64}，域前缀建议带 /）: "
                                       + m.scenarioCategory);
    }

    // coveredRequirements（≥1；lint 对字典逐项校验在 lint 工具——装载层仅句法）。
    m.coveredRequirements = requireStringArray(root, "coveredRequirements", 1);
    m.coveredAt = requireStringArray(root, "coveredAt", 0);

    // toleranceProfile（{id,version}）。
    const JsonValue* tp = root.find("toleranceProfile");
    if (tp == nullptr || !tp->isObject()) {
        failField("toleranceProfile", "必填对象字段缺失");
    }
    m.toleranceProfileId = requireString(*tp, "id", "toleranceProfile.");
    m.toleranceProfileVersion = requireString(*tp, "version", "toleranceProfile.");

    m.inputs = requireStringArray(root, "inputs", 1);
    m.expected = requireStringArray(root, "expected", 1);

    // parameters（对象，域自有 schema——装载层存 JSON 原文）。
    if (const JsonValue* p = root.find("parameters"); p != nullptr && p->isObject()) {
        m.parametersJson = dumpJson(*p, false);
    } else {
        failField("parameters", "必填对象字段缺失");
    }

    // units：field→UnitToken（token 须在 core 单位表注册——装载时校验）。
    if (const JsonValue* u = root.find("units"); u != nullptr && u->isObject()) {
        for (const auto& kv : u->members) {
            const auto tok = core::UnitToken::find(kv.second.text);
            if (!u->members.empty() && !kv.second.isString()) {
                failField("units." + kv.first, "单位须为注册 token 字符串");
            }
            if (!tok.has_value()) {
                failField("units." + kv.first, "单位未注册（core 单位表无此 token）: "
                                               + kv.second.text);
            }
            m.units.emplace_back(kv.first, kv.second.text);
        }
    } else {
        failField("units", "必填对象字段缺失");
    }

    // frames：至少 base（涉及 world/tcp 时必含——交叉校验）。
    if (const JsonValue* fr = root.find("frames"); fr != nullptr && fr->isObject()) {
        bool hasBase = false;
        for (const auto& kv : fr->members) {
            if (!kv.second.isString()) { failField("frames." + kv.first, "须为字符串"); }
            if (kv.first == "base") { hasBase = true; }
            m.frames.emplace_back(kv.first, kv.second.text);
        }
        if (!hasBase) { failField("frames", "至少含 base（§4.2.2）"); }
    } else {
        failField("frames", "必填对象字段缺失");
    }

    // referenceSource：analytic-case/reference-impl 必填且独立=true；regression 必填 =false。
    if (const JsonValue* rs = root.find("referenceSource"); rs != nullptr && rs->isObject()) {
        m.referenceSourcePresent = true;
        m.referenceSource.method = requireString(*rs, "method", "referenceSource.");
        static const char* kMethods[] = {"closed-form", "textbook", "independent-program",
                                         "production-snapshot", "hand-built", "measured"};
        bool methodOk = false;
        for (const auto* mm : kMethods) {
            if (m.referenceSource.method == mm) { methodOk = true; }
        }
        if (!methodOk) {
            failField("referenceSource.method", "未知方法: " + m.referenceSource.method);
        }
        m.referenceSource.scope = requireString(*rs, "scope", "referenceSource.");
        m.referenceSource.independentOfProductionImpl =
            requireBool(*rs, "independentOfProductionImpl");
        m.referenceSource.description = requireString(*rs, "description", "referenceSource.");

        const bool needIndependent =
            (m.kind == DatasetKind::AnalyticCase || m.kind == DatasetKind::ReferenceImpl);
        if (needIndependent && !m.referenceSource.independentOfProductionImpl) {
            failField("referenceSource.independentOfProductionImpl",
                      "analytic-case/reference-impl 必须独立于生产实现");
        }
        if (m.kind == DatasetKind::Regression && m.referenceSource.independentOfProductionImpl) {
            failField("referenceSource.independentOfProductionImpl",
                      "regression 的来源是生产实现快照，必须 =false");
        }
    } else if (m.kind == DatasetKind::AnalyticCase
               || m.kind == DatasetKind::ReferenceImpl
               || m.kind == DatasetKind::Regression) {
        failField("referenceSource", "该 kind 下必填");
    }

    // producer（必填）。
    if (const JsonValue* pr = root.find("producer"); pr != nullptr && pr->isObject()) {
        m.producerPresent = true;
        m.producer.softwareVersion = requireString(*pr, "softwareVersion", "producer.");
        m.producer.algorithmId = requireString(*pr, "algorithmId", "producer.");
        if (const JsonValue* sc = pr->find("solverConfig"); sc != nullptr && sc->isObject()) {
            m.producer.solverConfigJson = dumpJson(*sc, false);
        }
        if (const JsonValue* sd = pr->find("seed"); sd != nullptr && !sd->isNull()) {
            if (!sd->isNumber() || sd->number < 0) {
                failField("producer.seed", "seed 须为非负整数或 null");
            }
            m.producer.seedPresent = true;
            m.producer.seed = static_cast<std::uint64_t>(sd->number);
        }
        if (const JsonValue* tc = pr->find("threadCount"); tc != nullptr && tc->isNumber()) {
            m.producer.threadCount = static_cast<int>(tc->number);
            if (m.producer.threadCount < 1) { failField("producer.threadCount", "须 ≥1"); }
        }
        m.producer.generatedAtUtc = requireString(*pr, "generatedAtUtc", "producer.");
    } else {
        failField("producer", "必填对象字段缺失");
    }

    // edgeCases：analytic-case/reference-impl 必填且三布尔全 true（附录 D C4 lint 强制）。
    if (const JsonValue* ec = root.find("edgeCases"); ec != nullptr && ec->isObject()) {
        m.edgeCasesPresent = true;
        m.edgeCases.zeroValue = requireBool(*ec, "zeroValue");
        m.edgeCases.nearZero = requireBool(*ec, "nearZero");
        m.edgeCases.signCancellation = requireBool(*ec, "signCancellation");
        m.edgeCases.sampleRefs = requireStringArray(*ec, "sampleRefs", 0);
        if (m.kind == DatasetKind::AnalyticCase || m.kind == DatasetKind::ReferenceImpl) {
            if (!m.edgeCases.zeroValue || !m.edgeCases.nearZero
                || !m.edgeCases.signCancellation) {
                failField("edgeCases", "analytic-case/reference-impl 三布尔必须全 true"
                                       "（附录 D C4 黄金数据集必含零值/近零值/正负抵消样例）");
            }
        }
    } else if (m.kind == DatasetKind::AnalyticCase || m.kind == DatasetKind::ReferenceImpl) {
        failField("edgeCases", "analytic-case/reference-impl 必填");
    }

    // integrity（≥1；覆盖 inputs+expected+generate 全部文件——装载期先校验结构，
    // 全量文件校验在下方 integrity 段执行）。
    if (const JsonValue* ig = root.find("integrity"); ig != nullptr && ig->isArray()) {
        if (ig->items.empty()) { failField("integrity", "至少 1 条"); }
        for (const auto& item : ig->items) {
            if (!item.isObject()) { failField("integrity", "条目须为对象"); }
            IntegrityEntry e;
            e.path = requireString(item, "path", "integrity.");
            e.sha256Hex = requireString(item, "sha256", "integrity.");
            const JsonValue* sz = item.find("sizeBytes");
            if (sz == nullptr || !sz->isNumber() || sz->number < 0) {
                failField("integrity.sizeBytes", "须为非负整数");
            }
            e.sizeBytes = static_cast<std::uint64_t>(sz->number);
            if (e.sha256Hex.size() != 64
                || !allCharsIn(e.sha256Hex, "0123456789abcdef")) {
                failField("integrity.sha256", "须为 64 位小写十六进制: " + e.sha256Hex);
            }
            m.integrity.push_back(std::move(e));
        }
    } else {
        failField("integrity", "必填数组字段缺失");
    }

    // generator（必填）。
    if (const JsonValue* gn = root.find("generator"); gn != nullptr && gn->isObject()) {
        m.generatorPresent = true;
        m.generatorScript = requireString(*gn, "script", "generator.");
        m.generatorInvocation = requireString(*gn, "invocation", "generator.");
        m.generatorCommitted = requireBool(*gn, "committed");
    } else {
        failField("generator", "必填对象字段缺失");
    }

    // history（≥1；首版也登记）。
    if (const JsonValue* hi = root.find("history"); hi != nullptr && hi->isArray()
                                                   && !hi->items.empty()) {
        for (const auto& item : hi->items) {
            if (!item.isObject()) { failField("history", "条目须为对象"); }
            HistoryEntry h;
            h.version = requireString(item, "version", "history.");
            h.date = requireString(item, "date", "history.");
            h.change = requireString(item, "change", "history.");
            h.reReviewedBy = requireString(item, "reReviewedBy", "history.");
            if (const JsonValue* sb = item.find("supersededBy");
                sb != nullptr && sb->isString()) {
                h.supersededBy = sb->text;
            }
            m.history.push_back(std::move(h));
        }
    } else {
        failField("history", "必填数组字段缺失或为空（首版也须登记）");
    }

    // ---- 完整性校验（§4.5：size＋SHA-256 逐文件；任何不符→DatasetInvalid） ----
    // 完整性清单须覆盖 inputs+expected（generate 声明 committed 时亦覆盖）。
    for (const auto& rel : m.inputs) {
        const bool covered = std::any_of(
            m.integrity.begin(), m.integrity.end(),
            [&](const IntegrityEntry& e) { return e.path == rel; });
        if (!covered) { failField("integrity", "inputs 文件未登记完整性: " + rel); }
    }
    for (const auto& rel : m.expected) {
        const bool covered = std::any_of(
            m.integrity.begin(), m.integrity.end(),
            [&](const IntegrityEntry& e) { return e.path == rel; });
        if (!covered) { failField("integrity", "expected 文件未登记完整性: " + rel); }
    }
    for (const auto& e : m.integrity) {
        const auto filePath = versionDir / e.path;
        const std::string bytes = readFileBytes(filePath);      // 缺失→EnvUnavailable
        if (bytes.size() != e.sizeBytes) {
            failField("integrity." + e.path,
                      "size 不符（期望 " + std::to_string(e.sizeBytes) + "，实际 "
                          + std::to_string(bytes.size()) + "）");
        }
        const std::string actualHex = sha256HexOf(bytes);
        if (actualHex != e.sha256Hex) {
            failField("integrity." + e.path,
                      "SHA-256 不符（期望 " + e.sha256Hex + "，实际 " + actualHex + "）——"
                      "文件被篡改或损坏");
        }
    }

    return out;
}

std::filesystem::path GoldenDataset::resolveInput(std::string_view relPath) const
{
    const bool listed = std::any_of(manifest_.inputs.begin(), manifest_.inputs.end(),
                                    [&](const std::string& s) { return s == relPath; });
    if (!listed) {
        failField("inputs", "相对路径不在清单 inputs 内: " + std::string{relPath});
    }
    return versionDir_ / std::filesystem::path{relPath};
}

std::filesystem::path GoldenDataset::resolveExpected(std::string_view relPath) const
{
    const bool listed = std::any_of(manifest_.expected.begin(), manifest_.expected.end(),
                                    [&](const std::string& s) { return s == relPath; });
    if (!listed) {
        failField("expected", "相对路径不在清单 expected 内: " + std::string{relPath});
    }
    return versionDir_ / std::filesystem::path{relPath};
}

}  // namespace sdurws::ird::testkit
