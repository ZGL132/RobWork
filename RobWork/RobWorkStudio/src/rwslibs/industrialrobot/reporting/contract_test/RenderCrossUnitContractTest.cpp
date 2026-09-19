/**
 * @file   RenderCrossUnitContractTest.cpp
 * @brief  RPT-T06 渲染链跨单元契约测试——io 写出设施注入 fake 协作
 *         （P-RPT-1 裁决前形态）＋值单源三格式毒化矩阵验证＋CSV/JSON
 *         格式契约字节面＋公共头零 io 类型纪律自证。
 *
 * 设计依据：
 *   - units/reporting.md §3.4（测试目标分工：与 io 写出设施〔P-RPT-1
 *     裁决前经注入 fake〕的协作随契约测试落位）、§9.5（IReportIoFactory
 *     注入契约——工厂零行为、CSV 方言行/转义/JSON canonical 行为全归
 *     io 适配实现）、§8.3（JSON/CSV 格式契约）、§8.4（值单源）、§8.5
 *     （AT-22 结构性前提）
 *   - 任务契约 tasks/foundation/RPT-T06.json acceptance 4/5/6
 *
 * fake 边界声明（P-RPT-1 处置留痕）：FakeCsvWriter/FakeJsonWriter 为 io
 *   canonical 行为的**最小确定性替身**（RFC4180 引号化＋CRLF＋方言行/
 *   2 空格缩进＋to_chars 数值＋LF）——真实行为归 L5 装配的 io 适配器；
 *   替身只用于驱动渲染器的注入缝（P-RPT-1 裁决补边后直连 io，签名零
 *   改动——替身与真实适配器同签名）。替身确定性即"同输入同字节"的
 *   测试面；io 真实 canonical 的正确性由 io 单元自己的验证矩阵承担
 *   （SA-12——reporting 不复验 io 行为）。
 */

#include <gtest/gtest.h>

#include <charconv>
#include <cstdio>
#include <string>
#include <type_traits>
#include <vector>

#include <sdurws/ird/diagnostics/Redaction.hpp>
#include <sdurws/ird/reporting/Render.hpp>
#include <sdurws/ird/reporting/Sections.hpp>

namespace {

using namespace sdurws::ird::reporting;
namespace co = sdurws::ird::core;         // 短别名（与 ReportModelTest 同款）
namespace core = sdurws::ird::core;       // 全名别名（夹具正文两种拼写并存的统一承接）
namespace ev = sdurws::ird::evidence;
namespace evidence = sdurws::ird::evidence;
namespace diagnostics = sdurws::ird::diagnostics;

// =====================================================================
// 确定性夹具（与 test/RenderTest.cpp 同脚本——测试夹具自持不共享）
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
    f.quantity = co::SourcedValue<double>::provided(
        si, co::ValueProvenance::make(provenance));
    f.unit = std::move(unit);
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

/// 标准 B 级报告（与 RenderTest 同脚本；单截面足量——三格式协作断言面）。
ReviewReportFields makeStandardFields()
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
    FieldValue text;
    text.key = "kin.note";
    text.quantity = co::SourcedValue<double>::notProvided();
    text.text = "拐点校验通过";
    entry.fields.push_back(text);
    FieldValue notProvided;
    notProvided.key = "kin.missing";
    notProvided.quantity = co::SourcedValue<double>::notProvided();
    entry.fields.push_back(notProvided);
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

ReviewReport makeStandardReport()
{
    return ReviewReport::make(makeStandardFields(), frozenReportLevelRule());
}

diagnostics::RedactionService& redactionService()
{
    static diagnostics::RedactionService service;
    return service;
}

std::string asText(const std::vector<std::uint8_t>& bytes)
{
    return std::string(bytes.begin(), bytes.end());
}

// =====================================================================
// io 注入 fake（P-RPT-1 裁决前替身——canonical 行为最小确定性形态）
// =====================================================================

/// CSV 写出替身：RFC4180 引号化＋CRLF 行尾＋方言标识行（io.md §5.1/§5.3
/// 的最小确定性形态）；调用序记账（open/header/rows/finish 节奏断言面）。
class FakeCsvWriter final : public IReportCsvWriter {
public:
    // 会话记账（共享给工厂的观测面——渲染器节奏断言）。
    bool markerRequested = false;          ///< emitDialectMarker 实参
    bool opened = false;
    bool headerWritten = false;
    int rowCalls = 0;
    bool finished = false;
    std::vector<std::string> header;       ///< 表头列名（列序冻结断言面）
    std::vector<std::vector<std::string>> rowTexts;  ///< 行单元格文本形（值列断言面）

    bool open(IReportOutputTarget&& target, bool emitDialectMarker) override
    {
        m_target = &target;
        opened = true;
        markerRequested = emitDialectMarker;
        if (emitDialectMarker) {
            // 方言行字面＝io.md §8.3 口径（真实字面权威归 io——替身同形）。
            return m_target->write(
                reinterpret_cast<const std::uint8_t*>(
                    "#rwcsv1 delimiter=, quote=\" eol=CRLF encoding=utf-8\r\n"),
                std::string("#rwcsv1 delimiter=, quote=\" eol=CRLF encoding=utf-8\r\n").size());
        }
        return true;
    }

    bool writeHeader(const std::vector<std::string>& columns) override
    {
        headerWritten = true;
        header = columns;
        return writeRowTexts({columns});
    }

    bool writeRow(const std::vector<ReportCsvCell>& cells) override
    {
        ++rowCalls;
        std::vector<std::string> texts;
        texts.reserve(cells.size());
        for (const ReportCsvCell& cell : cells) {
            switch (cell.kind) {
            case ReportCsvCell::Kind::Empty:   texts.emplace_back(); break;
            case ReportCsvCell::Kind::Text:    texts.push_back(cell.text); break;
            case ReportCsvCell::Kind::Integer: texts.push_back(std::to_string(cell.integer)); break;
            case ReportCsvCell::Kind::Number:
                texts.push_back(toChars(cell.number));
                break;
            }
        }
        rowTexts.push_back(texts);
        return writeRowTexts(texts);
    }

    bool finish() override
    {
        finished = true;
        return true;
    }

private:
    /// RFC4180 字段编码（含引号/逗号/换行字段的引号化＋引号加倍——最小
    /// 确定性形态）＋CRLF 行尾。
    bool writeRowTexts(const std::vector<std::string>& texts)
    {
        std::string line;
        for (std::size_t i = 0; i < texts.size(); ++i) {
            if (i > 0) {
                line += ',';
            }
            const bool needsQuote = texts[i].find_first_of(",\"\r\n") != std::string::npos;
            if (needsQuote) {
                line += '"';
                for (const char c : texts[i]) {
                    if (c == '"') {
                        line += "\"\"";
                    } else {
                        line += c;
                    }
                }
                line += '"';
            } else {
                line += texts[i];
            }
        }
        line += "\r\n";
        return m_target->write(reinterpret_cast<const std::uint8_t*>(line.data()), line.size());
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
/// to_chars 数值、行尾 LF——io.md §5.9.3 的最小确定性形态）。
class FakeJsonWriter final : public IReportJsonWriter {
public:
    bool opened = false;
    std::size_t topLevelMembers = 0;   ///< 顶层键数（DOM 接收断言面）

    bool write(IReportOutputTarget&& target, const ReportJsonDom& dom) override
    {
        m_target = &target;
        opened = true;
        topLevelMembers = dom.objectMembers().size();
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

    std::string serialize(const ReportJsonDom& dom, int depth) const
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

/// io 工厂替身：产出计数（渲染器只经工厂取得 writer——注入纪律的观测面；
/// 工厂零行为——本替身除产出/记账外零逻辑，与 §9.5 边界价值一致）。
/// 计数为 mutable 观测面（const 接口内记账——BuilderTest 替身同款形态）。
class FakeIoFactory final : public IReportIoFactory {
public:
    mutable int csvWriterCalls = 0;
    mutable int jsonWriterCalls = 0;
    mutable int atomicTargetCalls = 0;

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
    std::unique_ptr<IReportOutputTarget> makeAtomicTarget(const std::filesystem::path&,
                                                          ReplacePolicy) const override
    {
        ++atomicTargetCalls;
        return nullptr;   // 渲染器不消费原子目标（落盘归导出链 RPT-T09）
    }
};

// =====================================================================
// 值单源（acceptance 4——毒化矩阵三格式验证）
// =====================================================================

TEST(RenderValueSingleSourceTest, PoisonedMatrix_ThreeFormats_EmitCellValue_RPT06_ACC4)
{
    // D-07/AT-22 结构性前提（§8.4）：三渲染器对同一 fieldKey 的输出只能
    // 来自同一 FieldCell——毒化矩阵（矩阵值与报告可推导值不同）喂三渲染
    // 器，三格式输出均携带毒化标记（值不可能被渲染器重算）。
    const ReviewReport report = makeStandardReport();
    FieldMatrix matrix = extractFieldMatrix(report);
    const std::string poison = "POISON-值单源-0xDEADBEEF";
    for (FieldCell& cell : matrix) {
        if (cell.fieldKey == "kinematics-collision.a-reach.kin.reach") {
            cell.valueRepr = poison;   // 毒化：报告可推导 1500——矩阵说了算
        }
    }

    FakeIoFactory factory;
    HtmlReportRenderer htmlRenderer(redactionService());
    JsonReportRenderer jsonRenderer(factory, redactionService());
    CsvReportRenderer csvRenderer(factory, redactionService());

    const auto html = htmlRenderer.render(report, matrix, ReportRenderFormat::Html);
    const auto json = jsonRenderer.render(report, matrix, ReportRenderFormat::Json);
    const auto csv = csvRenderer.render(report, matrix, ReportRenderFormat::Csv);
    ASSERT_TRUE(html.artifact.has_value());
    ASSERT_TRUE(json.artifact.has_value());
    ASSERT_TRUE(csv.artifact.has_value());

    const std::string htmlText = asText(html.artifact->bytes);
    const std::string jsonText = asText(json.artifact->bytes);
    const std::string csvText = asText(csv.artifact->bytes);

    // 三格式输出均携带毒化值——且不携带报告可推导的 1500（未被重算）。
    EXPECT_NE(htmlText.find("data-field=\"kinematics-collision.a-reach.kin.reach\">" + poison),
              std::string::npos);
    EXPECT_NE(jsonText.find(poison), std::string::npos);
    EXPECT_NE(csvText.find(poison), std::string::npos);
    EXPECT_EQ(htmlText.find(">1500<"), std::string::npos);
    EXPECT_EQ(jsonText.find("\"value\": \"1500\""), std::string::npos);
    EXPECT_EQ(csvText.find(",1500,"), std::string::npos);
}

// =====================================================================
// JSON 格式契约（acceptance 5——schemaVersion/四态 token/canonical）
// =====================================================================

TEST(JsonRenderContractTest, SchemaVersion_FourStateTokens_CanonicalBytes_RPT06_ACC5)
{
    // ird-report-json/1：顶层 schemaVersion、SourcedValue 四态显式 token、
    // canonical 同 DOM 二次写出字节相同（确定性）。
    const ReviewReport report = makeStandardReport();
    const FieldMatrix matrix = extractFieldMatrix(report);
    FakeIoFactory factory;
    JsonReportRenderer renderer(factory, redactionService());

    const RenderOutcome first = renderer.render(report, matrix, ReportRenderFormat::Json);
    const RenderOutcome second = renderer.render(report, matrix, ReportRenderFormat::Json);
    ASSERT_TRUE(first.artifact.has_value());
    ASSERT_TRUE(second.artifact.has_value());
    EXPECT_EQ(first.artifact->bytes, second.artifact->bytes);   // 二次渲染字节相同

    const std::string json = asText(first.artifact->bytes);
    EXPECT_NE(json.find("\"schemaVersion\": \"ird-report-json/1\""), std::string::npos);
    // 四态显式 token（provided/not-provided——夹具值族）。
    EXPECT_NE(json.find("\"state\": \"provided\""), std::string::npos);
    EXPECT_NE(json.find("\"state\": \"not-provided\""), std::string::npos);
    // 限定语数组字段（§6.4 结构性输出——JSON 面）。
    EXPECT_NE(json.find("\"qualifier\""), std::string::npos);
    // 顶层 17 键冻结集（NFR-DEP-04 执行侧自检的产出面）。
    EXPECT_EQ(first.artifact->digest, second.artifact->digest);
}

TEST(JsonRenderContractTest, WriterViaFactory_SessionSequence_RPT06_ACC6)
{
    // 注入纪律（P-RPT-1）：JSON 渲染恰一次 makeJsonWriter、零 makeCsvWriter
    // ——工厂零行为、行为归 io 适配（替身记账面）。
    const ReviewReport report = makeStandardReport();
    const FieldMatrix matrix = extractFieldMatrix(report);
    FakeIoFactory factory;
    JsonReportRenderer renderer(factory, redactionService());
    const RenderOutcome outcome = renderer.render(report, matrix, ReportRenderFormat::Json);
    ASSERT_TRUE(outcome.artifact.has_value());
    EXPECT_EQ(factory.jsonWriterCalls, 1);
    EXPECT_EQ(factory.csvWriterCalls, 0);
    EXPECT_EQ(factory.atomicTargetCalls, 0);   // 渲染零落盘（§9.3 副作用行）
}

// =====================================================================
// CSV 格式契约（acceptance 5——主表 11 列/附表/方言行/空值区分）
// =====================================================================

TEST(CsvRenderContractTest, MainTableElevenColumns_FrozenOrder_ValueColumnRules_RPT06_ACC5)
{
    // 主表列序冻结（§8.3 原文 11 列）；值列＝四态文本规范形（"未提供"
    // 等）；空值＝空字段（与"不适用"文本区分）。
    const ReviewReport report = makeStandardReport();
    const FieldMatrix matrix = extractFieldMatrix(report);
    FakeIoFactory factory;
    CsvReportRenderer renderer(factory, redactionService());
    const RenderOutcome outcome = renderer.render(report, matrix, ReportRenderFormat::Csv);
    ASSERT_TRUE(outcome.artifact.has_value());
    EXPECT_EQ(factory.csvWriterCalls, 3);   // 主表＋诊断附表＋覆盖附表

    // 字节面：方言行三次在场（每份文档各一行——表边界定位依据）。
    const std::string csv = asText(outcome.artifact->bytes);
    const std::string marker = "#rwcsv1 delimiter=, quote=\" eol=CRLF encoding=utf-8";
    std::size_t markerCount = 0;
    for (std::size_t pos = csv.find(marker); pos != std::string::npos;
         pos = csv.find(marker, pos + 1)) {
        ++markerCount;
    }
    EXPECT_EQ(markerCount, 3u);

    // 逐表头断言（字节面）：主表 11 列冻结序＋两附表列序。
    EXPECT_NE(csv.find("field_key,section_id,item_label,value,unit,status,qualifier,"
                       "result_ref,evidence_ref,case_scope,currentness"),
              std::string::npos);
    EXPECT_NE(csv.find("code,subject,local_name,severity,category,comparison_actual,"
                       "comparison_actual_unit,comparison_expected,comparison_expected_unit,"
                       "occurrences,source_run"),
              std::string::npos);
    EXPECT_NE(csv.find("case_id,status,run_id"), std::string::npos);

    // 值列四态：mm 投影值 1500 在场；"未提供"呈现词在场（not-provided 字段）。
    EXPECT_NE(csv.find("kinematics-collision.a-reach.kin.reach,kinematics-collision,"
                       "a-reach,1500,mm,feasible"),
              std::string::npos);
    EXPECT_NE(csv.find(",未提供,,feasible,,"), std::string::npos);
    // 证据引用规范文本（itemId@digest 前 12 hex——引用而非复制）。
    EXPECT_NE(csv.find("kin.reach-per-task-point@"), std::string::npos);
}

TEST(CsvRenderContractTest, ByteDeterministic_SecondRenderIdentical_RPT06_ACC1)
{
    // 同报告同格式二次导出字节一致（§8.3 确定性——RP-CONS-3 CSV 面）。
    const ReviewReport report = makeStandardReport();
    const FieldMatrix matrix = extractFieldMatrix(report);
    FakeIoFactory factory;
    CsvReportRenderer renderer(factory, redactionService());
    const RenderOutcome first = renderer.render(report, matrix, ReportRenderFormat::Csv);
    const RenderOutcome second = renderer.render(report, matrix, ReportRenderFormat::Csv);
    ASSERT_TRUE(first.artifact.has_value());
    ASSERT_TRUE(second.artifact.has_value());
    EXPECT_EQ(first.artifact->bytes, second.artifact->bytes);
    EXPECT_EQ(first.artifact->digest, second.artifact->digest);
    EXPECT_EQ(first.artifact->templateVersion, kCsvTemplateVersion);
}

TEST(CsvRenderContractTest, DiagnosticsAppendix_SanitizedLocalName_RPT06_ACC3)
{
    // 诊断附表渲染过脱敏（§4.3.3 双保险——CSV 面）：local_name 列不携带
    // 未脱敏路径/地址。
    ReviewReportFields fields = makeStandardFields();
    DiagRefEntry diag;
    diag.code = "KIN-IK-NO-SOLUTION";
    diag.severity = diagnostics::DiagnosticSeverity::Warning;
    diag.category = diagnostics::DiagnosticCategory::DataInsufficient;
    diag.localName = "D:\\私有目录\\机密\\model.stl 0x7FFE00001234";
    diag.occurrences = 1;
    diag.sourceSection = kSectionKinematicsCollision;
    fields.diagRefs.push_back(diag);
    const ReviewReport report = ReviewReport::make(std::move(fields), frozenReportLevelRule());
    const FieldMatrix matrix = extractFieldMatrix(report);
    FakeIoFactory factory;
    CsvReportRenderer renderer(factory, redactionService());
    const RenderOutcome outcome = renderer.render(report, matrix, ReportRenderFormat::Csv);
    ASSERT_TRUE(outcome.artifact.has_value());
    const std::string csv = asText(outcome.artifact->bytes);
    EXPECT_EQ(csv.find("机密"), std::string::npos);
    EXPECT_EQ(csv.find("0x7FFE00001234"), std::string::npos);
    EXPECT_NE(csv.find("D:\\私有目录"), std::string::npos);
}

// =====================================================================
// 跨单元契约面（acceptance 6——类型恒等与头纪律的结构性自证）
// =====================================================================

/// 渲染产物摘要类型＝core::Digest256（SHA-256 经 core 唯一算法——§4.1；
/// 类型恒等 static_assert 常驻自证，P-RPT-9 基线）。
static_assert(std::is_same<decltype(std::declval<RenderArtifact>().digest), core::Digest256>::value,
              "RenderArtifact.digest 必须以 core::Digest256 承载（摘要唯一算法纪律）");

TEST(RenderHeaderDisciplineTest, PublicHeaderZeroIoIncludes_RPT06_ACC6)
{
    // P-RPT-1 公共头零 io 类型纪律：Render.hpp 不 include 任何 io 头
    // （io 注入契约族只出现 reporting/core 类型——§3.3/§9.5）。
    // 源码扫描（RP-GATE-1 单元内源码面同款机制——运行期留痕自证）。
    const std::string unitRoot = IRD_REPORTING_UNIT_ROOT;
    // 根＝industrialrobot 目录（CMake 注入形 "<dir>/.."，无尾随分隔符）
    // ——单元相对路径带 reporting/ 前缀（BuildRedLineTest 同款口径）。
    const std::string headerPath =
        unitRoot + "/reporting/include/sdurws/ird/reporting/Render.hpp";
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

}  // namespace
