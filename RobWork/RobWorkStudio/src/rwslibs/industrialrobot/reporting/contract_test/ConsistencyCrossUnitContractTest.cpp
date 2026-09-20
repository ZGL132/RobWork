/**
 * @file   ConsistencyCrossUnitContractTest.cpp
 * @brief  RPT-T07 一致性检查链跨单元契约测试——合法报告三格式渲染→一致
 *         （RP-CONS-1）＋CSV 转义 roundtrip（RP-CONS-2/NFR-SEC-03）＋
 *         CSV/JSON 维度坏样本定位＋CSV 回读注入缝协作（P-RPT-1）＋公共头
 *         零 io 类型纪律自证。
 *
 * 设计依据：
 *   - units/reporting.md §3.4（测试目标分工：与 io 写出/读回设施〔P-RPT-1
 *     裁决前经注入 fake〕的协作属跨单元契约面）、§8.5（AT-22：多格式逐字
 *     段一致＋CSV 转义 roundtrip——io IO-V01/V02 的报告侧消费）、§8.3
 *     （#rwcsv1 方言：值列规则/空值区分/前缀转义唯一实现归 io）、§9.4
 *     （七元组判定维度表——value/unit/status/qualifier/ref/missing）
 *   - 任务契约 tasks/foundation/RPT-T07.json acceptance 1~5
 *
 * fake 边界声明（P-RPT-1 处置留痕——acceptance 5，与 RPT-T06 同款口径）：
 *   FakeCsvWriter/FakeJsonWriter/FakeCsvReader 为 io canonical 行为的**最
 *   小确定性替身**（§5.3 前缀转义可逆对＋RFC4180 引号化＋CRLF＋方言标识
 *   行／2 空格缩进 canonical／记录边界标识行分帧＋前缀还原）——真实行为
 *   归 L5 装配的 io 适配器（转义唯一实现归 io，NFR-SEC-03/SA-12）；替身
 *   只用于驱动渲染器与检查器的注入缝，P-RPT-1/P-IO-1 合并裁决补边后直连
 *   io、签名零改动（替身与真实适配器同签名）。roundtrip 用例验证的是
 *   **报告链的注入缝与消费路径**（导出→reader 回读→逐字符还原），io 真
 *   实转义的正确性由 io 单元自己的验证矩阵承担（IO-V01/V02）。
 *
 * 替身边界声明（RP-STATE-4 同款）：报告对象一律经 ReviewReport::make 构造；
 *   夹具文本字段携带 RP-CONS-2 样例集（=、+、-、@、' 前缀/中文/引号/换行
 *   ——经 FieldValue.text 合法通道进入 valueRepr）；"空串"样例以**缺席列
 *   （空 CSV 字段）**承载——SourcedValue::invalid 拒绝空串（core 契约），
 *   空值列与"不适用"的区分正是 §8.3 空值规则与 roundtrip 的验证点。
 *
 * 确定性夹具（与 RenderTest/unit ConsistencyTest 同款纪律——自持不共享，
 * 身份逐位铺位禁随机）。
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <sdurws/ird/diagnostics/Redaction.hpp>
#include <sdurws/ird/reporting/Consistency.hpp>
#include <sdurws/ird/reporting/Render.hpp>
#include <sdurws/ird/reporting/Sections.hpp>

namespace {

using namespace sdurws::ird::reporting;
namespace co = sdurws::ird::core;
namespace core = sdurws::ird::core;
namespace ev = sdurws::ird::evidence;
namespace evidence = sdurws::ird::evidence;
namespace diagnostics = sdurws::ird::diagnostics;

// =====================================================================
// 确定性夹具（自持不共享——身份逐位铺位）
// =====================================================================

co::ContentIdentity cid(std::uint8_t seed)
{
    co::ContentIdentity id;
    for (std::size_t i = 0; i < id.bytes.size(); ++i) {
        id.bytes[i] = static_cast<std::uint8_t>(seed + i);
    }
    return id;
}

template <typename T> T fixtureId(std::uint8_t seed)
{
    T id;
    for (std::size_t i = 0; i < id.bytes.size(); ++i) {
        id.bytes[i] = static_cast<std::uint8_t>(seed + i);
    }
    return id;
}

co::ObjectId fixtureObject(std::uint8_t seed)
{
    co::ObjectId id;
    for (std::size_t i = 0; i < id.bytes.size(); ++i) {
        id.bytes[i] = static_cast<std::uint8_t>(seed * 4 + i);
    }
    return id;
}

std::chrono::system_clock::time_point fixedTime(std::int64_t unixSeconds)
{
    return std::chrono::system_clock::time_point(std::chrono::seconds(unixSeconds));
}

CurrentnessSnapshot currentSnapshot(co::RevisionId head, std::int64_t at)
{
    CurrentnessSnapshot c;
    c.status = ev::CurrentnessStatus::Current;
    c.evaluatedAgainst.headRevision = head;
    c.evaluatedAgainst.contextSummary = "夹具上下文（HEAD 一致）";
    c.computedAtUtc = fixedTime(at);
    return c;
}

FieldValue numericField(std::string key, double si, core::ProvenanceKind provenance,
                        std::optional<core::UnitToken> unit = std::nullopt)
{
    FieldValue f;
    f.key = std::move(key);
    f.quantity = co::SourcedValue<double>::provided(si, co::ValueProvenance::make(provenance));
    f.unit = std::move(unit);
    return f;
}

/// 文本字段快捷构造（FieldValue.text 合法通道——valueRepr＝原文逐字符）。
FieldValue textField(std::string key, std::string text)
{
    FieldValue f;
    f.key = std::move(key);
    f.quantity = co::SourcedValue<double>::notProvided();
    f.text = std::move(text);
    return f;
}

ResultRefSnapshot feasibleResult(co::RevisionId revision, co::ObjectId case1)
{
    ResultRefSnapshot r;
    r.runId = fixtureId<co::RunId>(0x14);
    r.task.project = fixtureId<co::ProjectId>(0x11);
    r.task.branch = fixtureId<co::BranchId>(0x12);
    r.task.revision = revision;
    r.task.run = r.runId;
    r.task.attempt.value = 1;
    r.evaluationKey = "kin-batch-ik";
    r.evaluatorContractVersion = 1;
    r.mode = co::EvaluationMode::Verified;
    r.outcome = co::TaskOutcome::Completed;
    r.engineeringStatus = co::EngineeringStatus::Feasible;
    r.snapshotId = cid(0x10);
    r.sliceId = cid(0x30);
    r.inputBaselineId = cid(0x50);
    r.caseScope = {case1};
    r.eligibility.formalPass = true;
    r.currentness = currentSnapshot(revision, 1700000000);
    r.producer.productVersion = "0.1.0";
    return r;
}

/// 精简合法报告＋RP-CONS-2 样例集（13 字段——七元组维度＋转义样本全集）。
ReviewReportFields makeFields()
{
    const co::RevisionId revision = fixtureId<co::RevisionId>(0x13);
    const co::ObjectId case1 = fixtureObject(0x01);

    ReviewReportFields f;
    f.reportId = fixtureId<ReportId>(0x70);
    f.level = ReportLevel::B;
    f.project = fixtureId<co::ProjectId>(0x11);
    f.branch = fixtureId<co::BranchId>(0x12);
    f.revision = revision;
    f.revisionSeq = 7;
    f.resultRefs.push_back(feasibleResult(revision, case1));

    ReviewReportSection kin;
    kin.sectionId = kSectionKinematicsCollision;
    kin.sectionVersion = 1;
    kin.selected = true;
    kin.status = SectionStatus::Populated;
    SectionEntryView entry;
    entry.entryKey = "a-reach";
    entry.fields.push_back(numericField("kin.reach", 1.5, core::ProvenanceKind::UserProvided,
                                        *co::UnitToken::find("mm")));
    entry.fields.push_back(textField("kin.note", "拐点校验通过"));
    FieldValue notApplicable;
    notApplicable.key = "kin.na-ratio";
    notApplicable.quantity = co::SourcedValue<double>::notApplicable();
    entry.fields.push_back(notApplicable);
    entry.fields.push_back(numericField("kin.estimate", 2.5, core::ProvenanceKind::GeometricEstimate));
    FieldValue invalid;
    invalid.key = "kin.bad-raw";
    invalid.quantity = co::SourcedValue<double>::invalid("abc");
    entry.fields.push_back(invalid);
    // ---- RP-CONS-2 样例集（=、+、-、@、' 前缀/中文/引号/换行）----
    entry.fields.push_back(textField("kin.text-eq", "=command;shutdown"));
    entry.fields.push_back(textField("kin.text-plus", "+增量"));
    entry.fields.push_back(textField("kin.text-minus", "-0.5"));
    entry.fields.push_back(textField("kin.text-at", "@地址"));
    entry.fields.push_back(textField("kin.text-quote-prefix", "'引号前缀"));
    entry.fields.push_back(textField("kin.text-cn", "中文扭矩「峰值」N·m"));
    entry.fields.push_back(textField("kin.text-quo", "他说：\"你好\""));
    entry.fields.push_back(textField("kin.text-nl", "line1\nline2"));
    entry.result.runId = f.resultRefs[0].runId;
    entry.result.fieldPath = "payload.reach";
    EvidenceBinding binding;
    binding.itemId = "kin.reach-per-task-point";
    binding.status = evidence::EvidenceItemStatus::Satisfied;
    binding.digest = cid(0x10).bytes;
    binding.caseScope = {case1};
    entry.evidence = {binding};
    entry.caseScope = {case1};
    kin.entries.push_back(entry);
    kin.sourceResults = {f.resultRefs[0].runId};
    kin.renderHint = RenderHint::Table;
    kin.order = 3;
    f.sections.push_back(kin);

    EvidenceRefEntry okRef;
    okRef.itemId = "kin.reach-per-task-point";
    okRef.itemClass = evidence::EvidenceItemClass::Required;
    okRef.status = evidence::EvidenceItemStatus::Satisfied;
    okRef.artifactDigest = cid(0x10).bytes;
    okRef.caseScope = {case1};
    okRef.sourceSection = kSectionKinematicsCollision;
    f.evidenceRefs.push_back(okRef);

    f.currentnessSummary.perResult.push_back(
        ResultCurrentnessEntry{f.resultRefs[0].runId, currentSnapshot(revision, 1700000000)});
    f.reproduction.productVersion = "0.1.0";
    f.reproduction.evidenceContractVersion = "ev-contract-1";
    f.review.basisRevision = revision;
    f.review.signOff = SignOffState::unsignedState();
    f.unitPreference.displayUnits.emplace(co::QuantityKind::Length, *co::UnitToken::find("mm"));
    f.unitPreference.displayUnits.emplace(co::QuantityKind::Angle, *co::UnitToken::find("rad"));
    f.generatedAtUtc = fixedTime(1700000100);
    f.generatedBy = "batch";
    f.generatorVersion = "rpt-0.1.0";
    f.reportVersion = 1;
    f.sectionModelVersion = std::string(kSectionModelVersion);
    return f;
}

ReviewReport makeStandardReport() { return ReviewReport::make(makeFields(), frozenReportLevelRule()); }

diagnostics::RedactionService& redactionService()
{
    static diagnostics::RedactionService service;
    return service;
}

std::string asText(const std::vector<std::uint8_t>& bytes)
{
    return std::string(bytes.begin(), bytes.end());
}

std::vector<std::uint8_t> asBytes(const std::string& text)
{
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

const FieldCell* findCell(const FieldMatrix& matrix, const std::string& fieldKey)
{
    for (const FieldCell& cell : matrix) {
        if (cell.fieldKey == fieldKey) {
            return &cell;
        }
    }
    return nullptr;
}

/// 定位包含 needle 的 CSV 数据行的 field_key 列（坏样本定位的 fieldKey
/// 断言辅助——行首至首个 ',' 即主表首列）。
std::string fieldKeyOfRowContaining(const std::string& csv, const std::string& needle)
{
    const std::size_t at = csv.find(needle);
    EXPECT_NE(at, std::string::npos) << "夹具字节应含：" << needle;
    const std::size_t rowStart = csv.rfind("\n", at) == std::string::npos ? 0 : csv.rfind("\n", at) + 1;
    const std::size_t comma = csv.find(',', rowStart);
    EXPECT_NE(comma, std::string::npos);
    return csv.substr(rowStart, comma - rowStart);
}

// =====================================================================
// io 注入替身族（P-RPT-1 裁决前形态——边界声明见文件头）
// =====================================================================

/// 方言标识行字面（io §5.1 canonical——真实字面权威归 io，替身同形）。
const std::string kDialectMarker = "#rwcsv1 delimiter=, quote=\" eol=CRLF encoding=utf-8";

/// CSV 写出替身：io §5.3 前缀转义（=+−@' 前缀文本恰加一个 '）＋RFC4180
/// 引号化＋CRLF＋方言标识行（RPT-T06 最小 fake 的 RP-CONS-2 增强形——
/// 前缀转义是 roundtrip 用例的被测语义，替身须同形承载）。
class FakeCsvWriter final : public IReportCsvWriter {
public:
    bool open(IReportOutputTarget&& target, bool emitDialectMarker) override
    {
        m_target = &target;
        if (emitDialectMarker) {
            return writeRaw(kDialectMarker + "\r\n");
        }
        return true;
    }

    bool writeHeader(const std::vector<std::string>& columns) override
    {
        return writeQuotedRow(columns);
    }

    bool writeRow(const std::vector<ReportCsvCell>& cells) override
    {
        std::vector<std::string> texts;
        texts.reserve(cells.size());
        for (const ReportCsvCell& cell : cells) {
            switch (cell.kind) {
            case ReportCsvCell::Kind::Empty:   texts.emplace_back(); break;
            case ReportCsvCell::Kind::Text:    texts.push_back(cell.text); break;
            case ReportCsvCell::Kind::Integer: texts.push_back(std::to_string(cell.integer)); break;
            case ReportCsvCell::Kind::Number:  texts.push_back(toChars(cell.number)); break;
            }
        }
        return writeQuotedRow(texts);
    }

    bool finish() override { return true; }

private:
    /// io §5.3 导出转义（最小确定性形态）：= + - @ ' 前缀文本恰加一个 '。
    static std::string escapeText(const std::string& raw)
    {
        if (!raw.empty()
            && (raw[0] == '=' || raw[0] == '+' || raw[0] == '-' || raw[0] == '@'
                || raw[0] == '\'')) {
            return "'" + raw;
        }
        return raw;
    }

    /// 先转义、再 RFC4180 引号化（含引号加倍）、CRLF 行尾。
    bool writeQuotedRow(const std::vector<std::string>& texts)
    {
        std::string line;
        for (std::size_t i = 0; i < texts.size(); ++i) {
            if (i > 0) {
                line += ',';
            }
            const std::string escaped = escapeText(texts[i]);
            if (escaped.find_first_of(",\"\r\n") != std::string::npos) {
                line += '"';
                for (const char c : escaped) {
                    line += c;
                    if (c == '"') {
                        line += '"';
                    }
                }
                line += '"';
            } else {
                line += escaped;
            }
        }
        return writeRaw(line + "\r\n");
    }

    bool writeRaw(const std::string& bytes)
    {
        return m_target->write(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
    }

    static std::string toChars(double value)
    {
        char buffer[64];
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
        return std::string(buffer, result.ptr);
    }

    IReportOutputTarget* m_target = nullptr;   ///< 会话期借用（open→finish）
};

/// JSON 写出替身：2 空格缩进 canonical（键按装配序、字符串最小转义、
/// to_chars 数值、LF——io §5.9.3 的最小确定性形态）。
class FakeJsonWriter final : public IReportJsonWriter {
public:
    bool write(IReportOutputTarget&& target, const ReportJsonDom& dom) override
    {
        m_target = &target;
        const std::string text = serialize(dom, 0);
        return m_target->write(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
    }

private:
    static std::string escape(const std::string& raw)
    {
        std::string out;
        for (const char c : raw) {
            switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;      break;
            }
        }
        return out;
    }

    static std::string number(double value)
    {
        char buffer[64];
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
        return std::string(buffer, result.ptr);
    }

    static std::string serialize(const ReportJsonDom& dom, int depth)
    {
        const std::string pad(static_cast<std::size_t>(depth) * 2, ' ');
        const std::string innerPad(static_cast<std::size_t>(depth + 1) * 2, ' ');
        switch (dom.type()) {
        case ReportJsonDom::Type::Null:   return "null";
        case ReportJsonDom::Type::Bool:   return dom.booleanValue() ? "true" : "false";
        case ReportJsonDom::Type::Number: return number(dom.numberValue());
        case ReportJsonDom::Type::String:
            return "\"" + escape(dom.stringValue()) + "\"";
        case ReportJsonDom::Type::Array: {
            if (dom.arrayItems().empty()) {
                return "[]";
            }
            std::string out = "[\n";
            for (std::size_t i = 0; i < dom.arrayItems().size(); ++i) {
                out += innerPad + serialize(dom.arrayItems()[i], depth + 1);
                if (i + 1 < dom.arrayItems().size()) {
                    out += ',';
                }
                out += '\n';
            }
            return out + pad + "]";
        }
        case ReportJsonDom::Type::Object: {
            if (dom.objectMembers().empty()) {
                return "{}";
            }
            std::string out = "{\n";
            for (std::size_t i = 0; i < dom.objectMembers().size(); ++i) {
                out += innerPad + "\"" + escape(dom.objectMembers()[i].first) + "\": "
                       + serialize(dom.objectMembers()[i].second, depth + 1);
                if (i + 1 < dom.objectMembers().size()) {
                    out += ',';
                }
                out += '\n';
            }
            return out + pad + "}";
        }
        }
        return "";
    }

    IReportOutputTarget* m_target = nullptr;   ///< 会话期借用
};

/// io 工厂替身（产出计数——注入纪律观测面；工厂零行为）。
class FakeIoFactory final : public IReportIoFactory {
public:
    mutable int csvWriterCalls = 0;
    mutable int jsonWriterCalls = 0;

    std::unique_ptr<IReportCsvWriter> makeCsvWriter() const override
    {
        ++csvWriterCalls;
        return std::make_unique<FakeCsvWriter>();
    }
    std::unique_ptr<IReportJsonWriter> makeJsonWriter() const override
    {
        ++jsonWriterCalls;
        return std::make_unique<FakeJsonWriter>();
    }
    std::unique_ptr<IReportOutputTarget>
    makeAtomicTarget(const std::filesystem::path&, ReplacePolicy) const override
    {
        return nullptr;   // 一致性链不落盘（内存工件）——占位不可达
    }
};

/**
 * CSV 读取器替身（io ICsvReader 投影的最小确定性形态——检查器注入缝的
 * 消费对象）：记录边界处方言标识行字节精确判定分帧（引号态安全——标识行
 * 判定先于记录解析，引号字段内的同形字节不是记录边界）＋RFC4180 去引号
 * ＋§5.3 导入还原（恰剥一个前导 '）。与 FakeCsvWriter 构成可逆对。
 */
class FakeCsvReader final : public IReportCsvReader {
public:
    bool readTables(const std::uint8_t* bytes, std::size_t size,
                    std::vector<ReportCsvTable>& out) override
    {
        if (bytes == nullptr && size != 0) {
            return false;
        }
        std::string_view text(reinterpret_cast<const char*>(bytes), size);
        std::size_t pos = 0;
        ReportCsvTable* current = nullptr;
        while (pos < text.size()) {
            // ① 记录边界处标识行字节精确判定（分帧——先于记录解析）。
            const std::size_t lineEnd = text.find('\n', pos);
            const std::size_t lineStop = lineEnd == std::string_view::npos ? text.size() : lineEnd;
            std::string_view line = text.substr(pos, lineStop - pos);
            if (!line.empty() && line.back() == '\r') {
                line.remove_suffix(1);
            }
            if (line == kDialectMarker) {
                pos = lineStop == text.size() ? text.size() : lineStop + 1;
                out.emplace_back();
                current = &out.back();
                continue;
            }
            // ② 数据记录（引号感知——单记录解析，不跨记录）。
            std::vector<std::string> fields;
            std::string field;
            bool inQuotes = false;
            bool closed = false;
            while (pos < text.size()) {
                const char c = text[pos];
                if (inQuotes) {
                    if (c == '"') {
                        if (pos + 1 < text.size() && text[pos + 1] == '"') {
                            field += '"';
                            pos += 2;
                            continue;
                        }
                        inQuotes = false;
                        closed = true;
                        ++pos;
                        continue;
                    }
                    field += c;
                    ++pos;
                    continue;
                }
                if (c == '"' && !closed) {
                    inQuotes = true;
                    ++pos;
                    continue;
                }
                if (c == ',') {
                    fields.push_back(field);
                    field.clear();
                    closed = false;
                    ++pos;
                    continue;
                }
                if (c == '\r' || c == '\n') {
                    if (c == '\r' && pos + 1 < text.size() && text[pos + 1] == '\n') {
                        pos += 2;
                    } else {
                        pos += 1;
                    }
                    break;
                }
                field += c;
                ++pos;
            }
            fields.push_back(field);   // 末字段（行尾/EOF 收口）
            if (pos == text.size() && inQuotes) {
                return false;   // 引号未闭合＝结构损坏（§9.3 -QUOTE 同判）
            }
            // 空行（文档分隔）跳过——单空字段记录。
            if (fields.size() == 1 && fields[0].empty()) {
                continue;
            }
            if (current == nullptr) {
                return false;   // 标识行前出现数据＝结构损坏
            }
            for (std::string& unescaped : fields) {
                unescaped = unescapeText(std::move(unescaped));
            }
            if (current->header.empty()) {
                current->header = std::move(fields);
            } else {
                current->rows.push_back(std::move(fields));
            }
        }
        return true;
    }

private:
    /// io §5.3 导入还原（最小确定性形态）：恰剥一个前导 '（带标识文件）。
    static std::string unescapeText(std::string field)
    {
        if (!field.empty() && field.front() == '\'') {
            field.erase(field.begin());
        }
        return field;
    }
};

/// 读取器工厂替身（产出计数——acceptance 5"经测试注入"的观测面）。
class FakeCsvReaderFactory final : public IReportCsvReaderFactory {
public:
    std::unique_ptr<IReportCsvReader> makeCsvReader() const override
    {
        ++csvReaderCalls;
        if (injectFailure) {
            return nullptr;   // 适配缺位注入（parse-failed 轨观测）
        }
        return std::make_unique<FakeCsvReader>();
    }

    mutable int csvReaderCalls = 0;   ///< makeCsvReader 调用计数（借用观测面）
    mutable bool injectFailure = false;
};

// =====================================================================
// 三格式公共夹具（真渲染器＋io 替身——导出链步①②③的测试形）
// =====================================================================

struct ThreeFormatFixture
{
    // ReviewReport 无默认构造（make() 唯一生产者）——以 optional 承载
    // （值语义可拷贝，夹具构造期就地制造）。
    std::optional<ReviewReport> report;
    FieldMatrix matrix;
    RenderArtifact html;
    RenderArtifact json;
    RenderArtifact csv;
    ConsistencyInput input;
};

void makeThreeFormatFixture(ThreeFormatFixture* fx)
{
    fx->report.emplace(makeStandardReport());
    fx->matrix = extractFieldMatrix(*fx->report);
    ASSERT_EQ(fx->matrix.size(), 13u);   // 5 基础形态＋8 转义样本

    FakeIoFactory ioFactory;
    HtmlReportRenderer htmlRenderer(redactionService());
    JsonReportRenderer jsonRenderer(ioFactory, redactionService());
    CsvReportRenderer csvRenderer(ioFactory, redactionService());

    const RenderOutcome htmlOut = htmlRenderer.render(*fx->report, fx->matrix, ReportRenderFormat::Html);
    const RenderOutcome jsonOut = jsonRenderer.render(*fx->report, fx->matrix, ReportRenderFormat::Json);
    const RenderOutcome csvOut = csvRenderer.render(*fx->report, fx->matrix, ReportRenderFormat::Csv);
    ASSERT_FALSE(htmlOut.error.has_value()) << "HTML 渲染失败（夹具非法）";
    ASSERT_FALSE(jsonOut.error.has_value()) << "JSON 渲染失败（夹具非法）";
    ASSERT_FALSE(csvOut.error.has_value()) << "CSV 渲染失败（夹具非法）";
    ASSERT_TRUE(htmlOut.artifact.has_value());
    ASSERT_TRUE(jsonOut.artifact.has_value());
    ASSERT_TRUE(csvOut.artifact.has_value());
    fx->html = *htmlOut.artifact;
    fx->json = *jsonOut.artifact;
    fx->csv = *csvOut.artifact;

    fx->input.report = &*fx->report;
    fx->input.matrix = &fx->matrix;
    fx->input.artifacts = {&fx->html, &fx->json, &fx->csv};
}

/// 单条 mismatch 定位断言（acceptance 1 四元组）。
void expectSingleMismatch(const ConsistencyResult& result, ReportRenderFormat format,
                          const std::string& fieldKey, std::string_view dimension,
                          const std::string& expected, const std::string& actual)
{
    ASSERT_EQ(result.mismatches.size(), 1u);
    const FieldMismatch& m = result.mismatches[0];
    EXPECT_EQ(m.format, format);
    EXPECT_EQ(m.fieldKey, fieldKey);
    EXPECT_EQ(m.dimension, dimension);
    EXPECT_EQ(m.expected, expected);
    EXPECT_EQ(m.actual, actual);
    EXPECT_FALSE(result.consistent);
}

}  // namespace

// =====================================================================
// RP-CONS-1：合法报告＋三格式渲染→consistent；fieldCount 断言
// =====================================================================

TEST(ConsistencyThreeFormatTest, LegalReport_Consistent_FieldCount_RPT07_ACC1)
{
    ThreeFormatFixture fx;
    makeThreeFormatFixture(&fx);

    FakeCsvReaderFactory readerFactory;
    FieldConsistencyChecker checker(readerFactory);
    const ConsistencyResult result = checker.check(fx.input);
    EXPECT_TRUE(result.consistent) << "合法报告三格式应逐字段一致";
    EXPECT_TRUE(result.mismatches.empty());
    EXPECT_EQ(result.fieldCount, fx.matrix.size());
    EXPECT_TRUE(result.notes.empty());
    // 注入缝消费观测：每个 CSV 工件恰经工厂取一个独立 reader（acceptance 5）。
    EXPECT_EQ(readerFactory.csvReaderCalls, 1);
}

// =====================================================================
// RP-CONS-1 坏样本：CSV 改一值 / JSON 删限定语 → (format, fieldKey,
// dimension) 定位；mismatches 全量列出（不短路）
// =====================================================================

TEST(ConsistencyBadSampleTest, CsvChangeOneValue_ValueLocated_RPT07_ACC1)
{
    ThreeFormatFixture fx;
    makeThreeFormatFixture(&fx);
    std::string csv = asText(fx.csv.bytes);
    static const std::string kNeedle = "拐点校验通过";
    const std::size_t at = csv.find(kNeedle);
    ASSERT_NE(at, std::string::npos);
    csv.replace(at, kNeedle.size(), "被篡改值");
    fx.csv.bytes = asBytes(csv);

    FakeCsvReaderFactory readerFactory;
    FieldConsistencyChecker checker(readerFactory);
    const ConsistencyResult result = checker.check(fx.input);
    expectSingleMismatch(result, ReportRenderFormat::Csv,
                         "kinematics-collision.a-reach.kin.note", kMismatchDimValue,
                         "拐点校验通过", "被篡改值");
}

TEST(ConsistencyBadSampleTest, JsonDeleteQualifier_QualifierLocated_RPT07_ACC1)
{
    // JSON 删限定语（canonical 字节文本手术——仅去除 token 字面，残留
    // 空白数组由读取侧容忍）：限定语丢失＝mismatch（§6.4 硬约束）。
    ThreeFormatFixture fx;
    makeThreeFormatFixture(&fx);
    std::string json = asText(fx.json.bytes);
    static const std::string kFieldAnchor = "\"fieldKey\": \"kinematics-collision.a-reach.kin.estimate\"";
    const std::size_t fieldAt = json.find(kFieldAnchor);
    ASSERT_NE(fieldAt, std::string::npos) << "canonical JSON 应含字段键";
    static const std::string kToken = "\"estimated\"";
    const std::size_t tokenAt = json.find(kToken, fieldAt);
    ASSERT_NE(tokenAt, std::string::npos) << "估算字段的 qualifier 数组应含 token";
    json.erase(tokenAt, kToken.size());
    fx.json.bytes = asBytes(json);

    FakeCsvReaderFactory readerFactory;
    FieldConsistencyChecker checker(readerFactory);
    const ConsistencyResult result = checker.check(fx.input);
    expectSingleMismatch(result, ReportRenderFormat::Json,
                         "kinematics-collision.a-reach.kin.estimate", kMismatchDimQualifier,
                         "estimated", "");
}

TEST(ConsistencyBadSampleTest, MismatchesListedExhaustively_NotShortCircuit_RPT07_ACC1)
{
    // 不短路（§9.4 后置行）：两个格式各注入一处坏样本→两条 mismatch
    // 全量列出（一条 Csv value＋一条 Json qualifier）。
    ThreeFormatFixture fx;
    makeThreeFormatFixture(&fx);
    std::string csv = asText(fx.csv.bytes);
    const std::size_t csvAt = csv.find("拐点校验通过");
    ASSERT_NE(csvAt, std::string::npos);
    csv.replace(csvAt, std::string("拐点校验通过").size(), "被篡改值");
    fx.csv.bytes = asBytes(csv);
    std::string json = asText(fx.json.bytes);
    const std::size_t fieldAt = json.find("\"fieldKey\": \"kinematics-collision.a-reach.kin.estimate\"");
    ASSERT_NE(fieldAt, std::string::npos);
    const std::size_t tokenAt = json.find("\"estimated\"", fieldAt);
    ASSERT_NE(tokenAt, std::string::npos);
    json.erase(tokenAt, std::string("\"estimated\"").size());
    fx.json.bytes = asBytes(json);

    FakeCsvReaderFactory readerFactory;
    FieldConsistencyChecker checker(readerFactory);
    const ConsistencyResult result = checker.check(fx.input);
    ASSERT_EQ(result.mismatches.size(), 2u);
    EXPECT_FALSE(result.consistent);
    const bool hasCsvValue
        = std::any_of(result.mismatches.begin(), result.mismatches.end(),
                      [](const FieldMismatch& m) {
                          return m.format == ReportRenderFormat::Csv
                                 && m.fieldKey == "kinematics-collision.a-reach.kin.note"
                                 && m.dimension == kMismatchDimValue;
                      });
    const bool hasJsonQualifier
        = std::any_of(result.mismatches.begin(), result.mismatches.end(),
                      [](const FieldMismatch& m) {
                          return m.format == ReportRenderFormat::Json
                                 && m.fieldKey == "kinematics-collision.a-reach.kin.estimate"
                                 && m.dimension == kMismatchDimQualifier;
                      });
    EXPECT_TRUE(hasCsvValue);
    EXPECT_TRUE(hasJsonQualifier);
}

// =====================================================================
// 七元组维度覆盖（acceptance 3）：status/ref 维度仅 CSV 字段对齐携带
// ——CSV 列坏样本定位；HTML status 维度随单元测试（显示名反查）。
// =====================================================================

TEST(ConsistencySevenTupleTest, CsvStatusColumnChange_StatusLocated_RPT07_ACC3)
{
    ThreeFormatFixture fx;
    makeThreeFormatFixture(&fx);
    // 先取 fieldKey（首个 ",feasible," 所在行——篡改后该 token 不在首行，
    // 定位辅助必须基于篡改前字节序）。
    const std::string csvBefore = asText(fx.csv.bytes);
    const std::string fieldKey = fieldKeyOfRowContaining(csvBefore, ",feasible,");
    EXPECT_EQ(fieldKey, "kinematics-collision.a-reach.kin.reach");   // 矩阵序首字段行

    std::string csv = csvBefore;
    static const std::string kStatusCell = ",feasible,";
    const std::size_t at = csv.find(kStatusCell);
    ASSERT_NE(at, std::string::npos);
    csv.replace(at, kStatusCell.size(), ",engineering-infeasible,");
    fx.csv.bytes = asBytes(csv);

    FakeCsvReaderFactory readerFactory;
    FieldConsistencyChecker checker(readerFactory);
    const ConsistencyResult result = checker.check(fx.input);
    expectSingleMismatch(result, ReportRenderFormat::Csv, fieldKey, kMismatchDimStatus,
                         "feasible", "engineering-infeasible");
}

TEST(ConsistencySevenTupleTest, CsvResultRefColumnChange_RefLocated_RPT07_ACC3)
{
    ThreeFormatFixture fx;
    makeThreeFormatFixture(&fx);
    const std::string runRef = fixtureId<co::RunId>(0x14).toCanonical();
    const std::string csvBefore = asText(fx.csv.bytes);
    const std::string fieldKey = fieldKeyOfRowContaining(csvBefore, runRef);
    EXPECT_EQ(fieldKey, "kinematics-collision.a-reach.kin.reach");

    std::string csv = csvBefore;
    const std::size_t at = csv.find(runRef);
    ASSERT_NE(at, std::string::npos);
    csv.replace(at, runRef.size(), "run-ffffffffffffffffffffffffffffffff");
    fx.csv.bytes = asBytes(csv);

    FakeCsvReaderFactory readerFactory;
    FieldConsistencyChecker checker(readerFactory);
    const ConsistencyResult result = checker.check(fx.input);
    expectSingleMismatch(result, ReportRenderFormat::Csv, fieldKey, kMismatchDimRef,
                         runRef, "run-ffffffffffffffffffffffffffffffff");
}

// =====================================================================
// RP-CONS-2：CSV 转义 roundtrip（NFR-SEC-03——io IO-V01/V02 的报告侧
// 消费）：样例集导出→reader 回读→逐字符还原一致；转义形式留在文件层、
// 不进入结构化层
// =====================================================================

TEST(ConsistencyCsvRoundtripTest, NastyValueSamples_RoundtripIdentical_NoEscapeIntoStructLayer_RPT07_ACC2)
{
    ThreeFormatFixture fx;
    makeThreeFormatFixture(&fx);

    // 文件层自证：样例集在 CSV 字节中以**转义形**存在（前缀转义/引号化
    // ——转义发生在 io 写出侧）。
    const std::string csvText = asText(fx.csv.bytes);
    EXPECT_NE(csvText.find("'=command;shutdown"), std::string::npos)
        << "= 前缀文本应带 ' 转义前缀落盘（NFR-SEC-03）";
    EXPECT_NE(csvText.find("''引号前缀"), std::string::npos)
        << "' 前缀文本应带加倍前缀落盘（escape 不是 unescape 的左逆——可逆性正确形态）";
    EXPECT_NE(csvText.find("\"line1\nline2\""), std::string::npos)
        << "含换行字段应引号化落盘（RFC4180）";
    // 结构化层自证：源矩阵值恒为原文（无转义形）。
    const FieldCell* eq = findCell(fx.matrix, "kinematics-collision.a-reach.kin.text-eq");
    ASSERT_NE(eq, nullptr);
    EXPECT_EQ(eq->valueRepr, "=command;shutdown");

    // roundtrip 主断言：reader 回读→逐字段比对→一致（逐字符还原）。
    FakeCsvReaderFactory readerFactory;
    FieldConsistencyChecker checker(readerFactory);
    const ConsistencyResult result = checker.check(fx.input);
    EXPECT_TRUE(result.consistent) << "roundtrip 后应与源矩阵逐字符一致";
    for (const FieldMismatch& m : result.mismatches) {
        ADD_FAILURE() << "意外 mismatch：" << m.fieldKey << " 维度 " << m.dimension
                      << " 期望[" << m.expected << "] 实际[" << m.actual << "]";
    }
    // 缺席列 roundtrip：文本字段的 unit/qualifier 列为空字段（空值≠"不适用"
    // ——§8.3），回读后仍为缺席——consistent 已蕴含；此处显式钉住源形态。
    const FieldCell* plus = findCell(fx.matrix, "kinematics-collision.a-reach.kin.text-plus");
    ASSERT_NE(plus, nullptr);
    EXPECT_FALSE(plus->unit.has_value());
    EXPECT_TRUE(plus->qualifier.empty());
}

// =====================================================================
// parse-failed 错误轨（acceptance 4——回读本身失败→ConsistencyMismatch）
// =====================================================================

TEST(ConsistencyParseFailedTest, JsonTruncated_ParseFailedThrown_RPT07_ACC4)
{
    ThreeFormatFixture fx;
    makeThreeFormatFixture(&fx);
    fx.json.bytes.resize(fx.json.bytes.size() / 2);   // 结构中段截断

    FakeCsvReaderFactory readerFactory;
    FieldConsistencyChecker checker(readerFactory);
    try {
        checker.check(fx.input);
        FAIL() << "JSON 结构截断应抛 ConsistencyMismatch";
    } catch (const ReportError& e) {
        EXPECT_EQ(e.code(), ReportErrorCode::ConsistencyMismatch);
        EXPECT_NE(std::string(e.what()).find(kMismatchDimParseFailed), std::string::npos);
        EXPECT_NE(std::string(e.what()).find("json"), std::string::npos);
    }
}

TEST(ConsistencyParseFailedTest, CsvGarbageWithoutMarker_ParseFailedThrown_RPT07_ACC4)
{
    ThreeFormatFixture fx;
    makeThreeFormatFixture(&fx);
    fx.csv.bytes = asBytes("not,a,valid,report,artifact\r\nwithout,dialect,marker\r\n");

    FakeCsvReaderFactory readerFactory;
    FieldConsistencyChecker checker(readerFactory);
    try {
        checker.check(fx.input);
        FAIL() << "无标识行的 CSV 字节应判格式损坏";
    } catch (const ReportError& e) {
        EXPECT_EQ(e.code(), ReportErrorCode::ConsistencyMismatch);
        EXPECT_NE(std::string(e.what()).find(kMismatchDimParseFailed), std::string::npos);
        EXPECT_NE(std::string(e.what()).find("csv"), std::string::npos);
    }
}

TEST(ConsistencyParseFailedTest, CsvUnclosedQuoteStructure_ParseFailedThrown_RPT07_ACC4)
{
    ThreeFormatFixture fx;
    makeThreeFormatFixture(&fx);
    std::string csv = asText(fx.csv.bytes);
    // 结构破坏：去掉含换行字段（"line1\nline2"）的**一对引号**——值内裸
    // LF 变成记录终止符，该行字段数与表头不符（11 列结构被截断）。
    const std::size_t quotedAt = csv.find("\"line1\nline2\"");
    ASSERT_NE(quotedAt, std::string::npos);
    csv.erase(quotedAt, 1);                                   // 去掉开引号
    const std::size_t closingAt = csv.find("\"", quotedAt);   // 原闭引号（下一裸 '"'）
    ASSERT_NE(closingAt, std::string::npos);
    csv.erase(closingAt, 1);                                  // 去掉闭引号
    fx.csv.bytes = asBytes(csv);

    FakeCsvReaderFactory readerFactory;
    FieldConsistencyChecker checker(readerFactory);
    try {
        checker.check(fx.input);
        FAIL() << "引号结构破坏（行截断）应判格式损坏";
    } catch (const ReportError& e) {
        EXPECT_EQ(e.code(), ReportErrorCode::ConsistencyMismatch);
        EXPECT_NE(std::string(e.what()).find(kMismatchDimParseFailed), std::string::npos);
    }
}

TEST(ConsistencyParseFailedTest, CsvReaderFactoryMissing_ParseFailedThrown_RPT07_ACC5)
{
    // 适配缺位（工厂产出空 reader）＝CSV 回读能力缺位——同 parse-failed
    // 轨（不静默当成一致——"以格式存在当内容一致"禁令的对偶面）。
    ThreeFormatFixture fx;
    makeThreeFormatFixture(&fx);

    FakeCsvReaderFactory readerFactory;
    readerFactory.injectFailure = true;
    FieldConsistencyChecker checker(readerFactory);
    try {
        checker.check(fx.input);
        FAIL() << "CSV 适配缺位应抛 ConsistencyMismatch";
    } catch (const ReportError& e) {
        EXPECT_EQ(e.code(), ReportErrorCode::ConsistencyMismatch);
        EXPECT_NE(std::string(e.what()).find(kMismatchDimParseFailed), std::string::npos);
    }
}

// =====================================================================
// 纯函数纪律（acceptance 4——全路径同输入同结论）
// =====================================================================

TEST(ConsistencyPurityContractTest, SameInputSameResult_ThreeFormats_RPT07_ACC4)
{
    ThreeFormatFixture fx;
    makeThreeFormatFixture(&fx);
    // 注入一处坏样本（有区分度的输入）。
    std::string csv = asText(fx.csv.bytes);
    const std::size_t at = csv.find("拐点校验通过");
    ASSERT_NE(at, std::string::npos);
    csv.replace(at, std::string("拐点校验通过").size(), "被篡改值");
    fx.csv.bytes = asBytes(csv);

    const std::vector<std::uint8_t> csvBefore = fx.csv.bytes;
    const std::vector<std::uint8_t> jsonBefore = fx.json.bytes;

    FakeCsvReaderFactory readerFactory;
    FieldConsistencyChecker checker(readerFactory);
    const ConsistencyResult first = checker.check(fx.input);
    const ConsistencyResult second = checker.check(fx.input);

    ASSERT_EQ(first.mismatches.size(), 1u);
    EXPECT_EQ(first.mismatches.size(), second.mismatches.size());
    EXPECT_EQ(first.mismatches[0].format, second.mismatches[0].format);
    EXPECT_EQ(first.mismatches[0].fieldKey, second.mismatches[0].fieldKey);
    EXPECT_EQ(first.mismatches[0].dimension, second.mismatches[0].dimension);
    EXPECT_EQ(first.mismatches[0].expected, second.mismatches[0].expected);
    EXPECT_EQ(first.mismatches[0].actual, second.mismatches[0].actual);
    EXPECT_EQ(first.consistent, second.consistent);
    // 不修改任何输入（工件字节逐位不变——check 全程 const 只读）。
    EXPECT_EQ(csvBefore, fx.csv.bytes);
    EXPECT_EQ(jsonBefore, fx.json.bytes);
}

// =====================================================================
// P-RPT-1 公共头零 io 类型纪律（acceptance 5——§3.3 注入边界自证）
// =====================================================================

TEST(ConsistencyHeaderDisciplineTest, PublicHeaderZeroIoIncludes_RPT07_ACC5)
{
    // Consistency.hpp 不 include 任何 io 头（CSV 回读缝只出现 reporting
    // 自有类型——io ICsvReader 的投影而非引用；裁决补边后直连、签名零
    // 改动）。源码扫描（RP-GATE-1 单元内源码面同款机制）。
    const std::string unitRoot = IRD_REPORTING_UNIT_ROOT;
    const std::string headerPath =
        unitRoot + "/reporting/include/sdurws/ird/reporting/Consistency.hpp";
    FILE* file = std::fopen(headerPath.c_str(), "rb");
    ASSERT_NE(file, nullptr) << "公共头缺失：" << headerPath;
    std::string content;
    char buffer[4096];
    std::size_t n = 0;
    while ((n = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
        content.append(buffer, n);
    }
    std::fclose(file);

    EXPECT_EQ(content.find("sdurws/ird/io/"), std::string::npos)
        << "公共头出现 io include——P-RPT-1 纪律违约";
    EXPECT_EQ(content.find("#include <Qt"), std::string::npos)
        << "公共头出现 Qt include——D-01 零 Qt 纪律违约";
}
