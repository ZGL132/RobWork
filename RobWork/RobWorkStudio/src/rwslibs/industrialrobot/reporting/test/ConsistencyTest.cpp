/**
 * @file   ConsistencyTest.cpp
 * @brief  RPT-T07 一致性检查器单元测试——§9.4 契约边（平凡通过＋注记/
 *         Usage fail-fast/纯函数性）＋HTML data-field 提取器的坏样本定位
 *         与 parse-failed 轨（RP-CONS-1 单元内面对）。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/RPT-T07.json acceptance 1/3/4 的单元内
 *     承载面（acceptance 2 的 CSV 转义 roundtrip 与 acceptance 5 的 io
 *     reader 注入协作随 contract_test/ConsistencyCrossUnitContractTest
 *     .cpp——§3.4 测试目标分工：与 io 写出/读回设施的协作属跨单元契约
 *     面；本文件无 io 替身，第二格式工件以**手写 JSON 镜像夹具**承载
 *     ——字节是测试数据不是 io 协作，schema 逐成员对齐 ird-report-json/1
 *     冻结面，兼作该 schema 的可读样本）；
 *   - units/reporting.md §8.5（AT-22 逐字段一致性）、§9.4（IReportConsistency
 *     Checker 契约：纯函数、artifacts<2 平凡通过＋注记、parse-failed
 *     错误轨、调用方违约 Usage）、§6.4（限定语丢失＝mismatch）。
 *
 * 替身边界声明（RP-STATE-4 同款纪律）：报告对象一律经 ReviewReport::make
 *   构造（合法实例唯一生产者）；HTML 渲染器零 io 依赖（§9.3 副作用行），
 *   脱敏经 diagnostics::RedactionService 真实服务；手写 JSON 夹具只携带
 *   比对消费面（fieldKey/value/unit/qualifier）的真值，结构成员值为
 *   夹具占位（检查器对非七元组成员只校验成员集与类型——NFR-DEP-04
 *   读取侧的严格面即此）。
 *
 * 确定性夹具（与 RenderTest 同款纪律）：身份/数据源字段逐位固定（禁
 *   随机——同脚本两次构建逐位一致是纯函数断言的前提）。
 */

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include <sdurws/ird/diagnostics/Redaction.hpp>   // 真实脱敏服务（HTML 渲染注入面）
#include <sdurws/ird/reporting/Consistency.hpp>
#include <sdurws/ird/reporting/Render.hpp>
#include <sdurws/ird/reporting/Sections.hpp>      // kSectionKinematicsCollision/frozenReportLevelRule

namespace {

using namespace sdurws::ird::reporting;
namespace co = sdurws::ird::core;
namespace core = sdurws::ird::core;
namespace ev = sdurws::ird::evidence;
namespace evidence = sdurws::ird::evidence;
namespace diagnostics = sdurws::ird::diagnostics;

// =====================================================================
// 确定性夹具（与 RenderTest 同款纪律——自持不共享；身份逐位铺位）
// =====================================================================

co::ContentIdentity cid(std::uint8_t seed)
{
    co::ContentIdentity id;
    for (std::size_t i = 0; i < id.bytes.size(); ++i) {
        id.bytes[i] = static_cast<std::uint8_t>(seed + i);
    }
    return id;
}

/// 确定性 16 字节 Id128（同 seed 同值——禁随机）。
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

/// 当前性快照（Current 态合法形态——§6.3 presence 纪律）。
CurrentnessSnapshot currentSnapshot(co::RevisionId head, std::int64_t at)
{
    CurrentnessSnapshot c;
    c.status = ev::CurrentnessStatus::Current;
    c.evaluatedAgainst.headRevision = head;
    c.evaluatedAgainst.contextSummary = "夹具上下文（HEAD 一致）";
    c.computedAtUtc = fixedTime(at);
    return c;
}

/// 数值字段快捷构造（SI 真值＋来源；单位可选）。
FieldValue numericField(std::string key, double si, core::ProvenanceKind provenance,
                        std::optional<core::UnitToken> unit = std::nullopt)
{
    FieldValue f;
    f.key = std::move(key);
    f.quantity = co::SourcedValue<double>::provided(si, co::ValueProvenance::make(provenance));
    f.unit = std::move(unit);
    return f;
}

/// 合法结果引用（Verified/Completed/Feasible/Current/formalPass 成立）。
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

/// 精简合法报告（单章节单条目五字段——七元组各维度的可预期覆盖面）：
///   kin.reach     数值 1.5 m(SI)→mm 投影 "1500"/mm（value＋unit 维度）
///   kin.note      文本"拐点校验通过"（value 维度文本形）
///   kin.na-ratio  不适用态→"不适用"＋not-applicable（四态＋qualifier 维度）
///   kin.estimate  估算 2.5→"2.5"＋estimated（qualifier 维度第二词）
///   kin.bad-raw   非法保留原文"abc"（NFR-COR-03）
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
    FieldValue text;
    text.key = "kin.note";
    text.quantity = co::SourcedValue<double>::notProvided();
    text.text = "拐点校验通过";
    entry.fields.push_back(text);
    FieldValue notApplicable;
    notApplicable.key = "kin.na-ratio";
    notApplicable.quantity = co::SourcedValue<double>::notApplicable();
    entry.fields.push_back(notApplicable);
    entry.fields.push_back(numericField("kin.estimate", 2.5, core::ProvenanceKind::GeometricEstimate));
    FieldValue invalid;
    invalid.key = "kin.bad-raw";
    invalid.quantity = co::SourcedValue<double>::invalid("abc");
    entry.fields.push_back(invalid);
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

/// 脱敏服务（真实实现——HTML 渲染注入面）。
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

/// 矩阵单元格计数（本夹具恒 5——fieldCount 断言基准）。
constexpr std::size_t kFixtureFieldCount = 5;

/// 注入 CSV 读取器工厂（平凡通过/Usage 面不触达 CSV 回读——最小占位
/// 实现；真实回读协作随契约测试的行/列映射替身）。
class NullCsvReader final : public IReportCsvReader {
public:
    bool readTables(const std::uint8_t*, std::size_t, std::vector<ReportCsvTable>&) override
    {
        return false;   // 本测试面不消费 CSV 工件——占位恒失败（不可达）
    }
};

class NullCsvReaderFactory final : public IReportCsvReaderFactory {
public:
    std::unique_ptr<IReportCsvReader> makeCsvReader() const override
    {
        return std::make_unique<NullCsvReader>();
    }
};

/// 手写 JSON 镜像夹具（ird-report-json/1——结构成员对齐 buildReportDom
/// 冻结成员集；比对消费面 fieldKey/value/unit/qualifier 与矩阵真值逐字
/// 一致，故与 HTML 渲染工件构成"合法一致"输入对。非比对成员（runId/
/// fieldPath 等）为夹具占位值——检查器只校验成员集与类型）。
const char* kConsistentJsonMirror = R"json({
  "schemaVersion": "ird-report-json/1",
  "sections": [
    {
      "sectionId": "kinematics-collision",
      "sectionVersion": 1,
      "selected": true,
      "status": "populated",
      "entries": [
        {
          "entryKey": "a-reach",
          "fields": [
            {"key": "kin.reach", "fieldKey": "kinematics-collision.a-reach.kin.reach",
             "state": "provided", "value": "1500", "unit": "mm", "methodTag": null,
             "qualifier": []},
            {"key": "kin.note", "fieldKey": "kinematics-collision.a-reach.kin.note",
             "state": "not-provided", "value": "拐点校验通过", "unit": null,
             "methodTag": null, "qualifier": []},
            {"key": "kin.na-ratio", "fieldKey": "kinematics-collision.a-reach.kin.na-ratio",
             "state": "not-applicable", "value": "不适用", "unit": null,
             "methodTag": null, "qualifier": ["not-applicable"]},
            {"key": "kin.estimate", "fieldKey": "kinematics-collision.a-reach.kin.estimate",
             "state": "provided", "value": "2.5", "unit": null, "methodTag": null,
             "qualifier": ["estimated"]},
            {"key": "kin.bad-raw", "fieldKey": "kinematics-collision.a-reach.kin.bad-raw",
             "state": "invalid", "value": "abc", "unit": null, "methodTag": null,
             "qualifier": []}
          ],
          "result": {"runId": "run-fixture", "fieldPath": "payload.reach"},
          "evidence": [],
          "caseScope": [],
          "jump": {"objectId": null, "caseId": null, "runId": null}
        }
      ],
      "missingItems": [],
      "currentness": null,
      "diagnostics": [],
      "eligibilityNote": null,
      "renderHint": "table",
      "order": 3
    }
  ]
})json";

/// 公共两格式输入（HTML 真渲染＋手写 JSON 镜像——全比对路径的最小
/// 合法输入；各坏样本用例在此基础上单点变异）。void 出参形态——夹具
/// 内 ASSERT 失败即终止本用例（gtest 断言语义要求 void 返回）。
struct TwoFormatFixture
{
    // ReviewReport 无默认构造（make() 唯一生产者）——以 optional 承载，
    // 夹具构造期就地拷贝制造（值语义可拷贝）。
    std::optional<ReviewReport> report;
    FieldMatrix matrix;
    RenderArtifact html;
    RenderArtifact jsonMirror;
    ConsistencyInput input;
};

void makeTwoFormatFixture(TwoFormatFixture* fx)
{
    fx->report.emplace(makeStandardReport());
    fx->matrix = extractFieldMatrix(*fx->report);
    ASSERT_EQ(fx->matrix.size(), kFixtureFieldCount);

    HtmlReportRenderer renderer(redactionService());
    const RenderOutcome outcome = renderer.render(*fx->report, fx->matrix, ReportRenderFormat::Html);
    ASSERT_FALSE(outcome.error.has_value()) << "HTML 渲染失败（夹具非法）";
    ASSERT_TRUE(outcome.artifact.has_value());
    fx->html = *outcome.artifact;

    fx->jsonMirror.format = ReportRenderFormat::Json;
    fx->jsonMirror.bytes = asBytes(kConsistentJsonMirror);
    fx->jsonMirror.templateVersion = kJsonTemplateVersion;
    fx->jsonMirror.sourceReportIdentity = fx->report->contentIdentity();

    fx->input.report = &*fx->report;
    fx->input.matrix = &fx->matrix;
    fx->input.artifacts = {&fx->html, &fx->jsonMirror};
}

/// 断言单条 mismatch 的定位四元组（format/fieldKey/dimension/expected/
/// actual——acceptance 1"定位到 (format, fieldKey, dimension)"）。
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
// 平凡通过＋注记（acceptance 4——§9.4 ConsistencyInput 注释行）
// =====================================================================

TEST(ConsistencyTrivialPassTest, ZeroArtifacts_TrivialPassWithNote_RPT07_ACC4)
{
    const ReviewReport report = makeStandardReport();
    const FieldMatrix matrix = extractFieldMatrix(report);
    ASSERT_EQ(matrix.size(), kFixtureFieldCount);

    NullCsvReaderFactory factory;
    FieldConsistencyChecker checker(factory);
    ConsistencyInput input;
    input.report = &report;
    input.matrix = &matrix;

    const ConsistencyResult result = checker.check(input);
    EXPECT_TRUE(result.consistent);
    EXPECT_TRUE(result.mismatches.empty());
    EXPECT_EQ(result.fieldCount, kFixtureFieldCount);   // 比对宇宙＝矩阵大小
    ASSERT_EQ(result.notes.size(), 1u);                 // 注记承载位（§9.4 偏差登记）
    EXPECT_NE(result.notes[0].find("artifacts=0"), std::string::npos);
}

TEST(ConsistencyTrivialPassTest, SingleArtifact_TrivialPassWithNote_RPT07_ACC4)
{
    TwoFormatFixture fx;
    makeTwoFormatFixture(&fx);
    fx.input.artifacts = {&fx.html};   // 单格式工件集

    NullCsvReaderFactory factory;
    FieldConsistencyChecker checker(factory);
    const ConsistencyResult result = checker.check(fx.input);
    EXPECT_TRUE(result.consistent);
    EXPECT_TRUE(result.mismatches.empty());
    EXPECT_EQ(result.fieldCount, kFixtureFieldCount);
    ASSERT_EQ(result.notes.size(), 1u);
    // 注记携带格式 token（html）——诊断可读性面。
    EXPECT_NE(result.notes[0].find("artifacts=1"), std::string::npos);
    EXPECT_NE(result.notes[0].find("html"), std::string::npos);
}

// =====================================================================
// Usage fail-fast（§9.4 前置行——调用方违约不产结果）
// =====================================================================

TEST(ConsistencyUsageTest, NullReportOrNullMatrix_Usage_RPT07_ACC4)
{
    const ReviewReport report = makeStandardReport();
    const FieldMatrix matrix = extractFieldMatrix(report);
    NullCsvReaderFactory factory;
    FieldConsistencyChecker checker(factory);

    ConsistencyInput nullReport;
    nullReport.report = nullptr;
    nullReport.matrix = &matrix;
    try {
        checker.check(nullReport);
        FAIL() << "空 report 指针应 Usage fail-fast";
    } catch (const ReportError& e) {
        EXPECT_EQ(e.code(), ReportErrorCode::Usage);
    }

    ConsistencyInput nullMatrix;
    nullMatrix.report = &report;
    nullMatrix.matrix = nullptr;
    try {
        checker.check(nullMatrix);
        FAIL() << "空 matrix 指针应 Usage fail-fast";
    } catch (const ReportError& e) {
        EXPECT_EQ(e.code(), ReportErrorCode::Usage);
    }
}

TEST(ConsistencyUsageTest, NullArtifactPointer_Usage_RPT07_ACC4)
{
    TwoFormatFixture fx;
    makeTwoFormatFixture(&fx);
    fx.input.artifacts = {&fx.html, nullptr};

    NullCsvReaderFactory factory;
    FieldConsistencyChecker checker(factory);
    try {
        checker.check(fx.input);
        FAIL() << "工件空指针应 Usage fail-fast";
    } catch (const ReportError& e) {
        EXPECT_EQ(e.code(), ReportErrorCode::Usage);
    }
}

TEST(ConsistencyUsageTest, DuplicateArtifactFormat_Usage_RPT07_ACC4)
{
    // 合法调用＝2~3 个**格式互异**工件（§9.4 合法调用行）——两个 HTML
    // 工件不构成格式集，前置拒绝（在字节解析前 fail-fast——字节为占位）。
    TwoFormatFixture fx;
    makeTwoFormatFixture(&fx);
    RenderArtifact secondHtml = fx.html;   // 同格式第二工件（占位字节同源）
    fx.input.artifacts = {&fx.html, &secondHtml};

    NullCsvReaderFactory factory;
    FieldConsistencyChecker checker(factory);
    try {
        checker.check(fx.input);
        FAIL() << "格式重复应 Usage fail-fast";
    } catch (const ReportError& e) {
        EXPECT_EQ(e.code(), ReportErrorCode::Usage);
    }
}

TEST(ConsistencyUsageTest, ArtifactSourceIdentityMismatch_Usage_RPT07_ACC4)
{
    // §9.4 前置行"artifacts 与 report/matrix 同源"的可验证面：工件
    // sourceReportIdentity 与报告 contentIdentity 不符＝装配违约。
    TwoFormatFixture fx;
    makeTwoFormatFixture(&fx);
    RenderArtifact foreign = fx.jsonMirror;
    foreign.sourceReportIdentity = cid(0x99);   // 他人身份——同源破坏
    fx.input.artifacts = {&fx.html, &foreign};

    NullCsvReaderFactory factory;
    FieldConsistencyChecker checker(factory);
    try {
        checker.check(fx.input);
        FAIL() << "工件与报告不同源应 Usage fail-fast";
    } catch (const ReportError& e) {
        EXPECT_EQ(e.code(), ReportErrorCode::Usage);
    }
}

// =====================================================================
// 全比对路径（HTML＋手写 JSON 镜像）——一致基线（acceptance 1 前半）
// =====================================================================

TEST(ConsistencyCheckTest, HtmlPlusJsonMirror_Consistent_FieldCount_RPT07_ACC1)
{
    TwoFormatFixture fx;
    makeTwoFormatFixture(&fx);

    NullCsvReaderFactory factory;
    FieldConsistencyChecker checker(factory);
    const ConsistencyResult result = checker.check(fx.input);
    EXPECT_TRUE(result.consistent);
    EXPECT_TRUE(result.mismatches.empty());
    // fieldCount 断言（acceptance 1）——恒等于矩阵单元格数。
    EXPECT_EQ(result.fieldCount, kFixtureFieldCount);
    EXPECT_TRUE(result.notes.empty());   // 正常比对无注记
}

// =====================================================================
// HTML 坏样本定位（acceptance 1——mismatch 定位到 (format, fieldKey,
// dimension)；acceptance 3——限定语丢失＝mismatch〔§6.4〕）
// =====================================================================

TEST(ConsistencyHtmlBadSampleTest, DeleteDataFieldAttribute_MissingLocated_RPT07_ACC1)
{
    // 坏样本形态一：删 data-field **属性**（单元格保留）——提取器以行内
    // 其余序位单元格重建 fieldKey，missing 定位仍落到具体键。
    TwoFormatFixture fx;
    makeTwoFormatFixture(&fx);
    std::string html = asText(fx.html.bytes);
    static const std::string kAttr = R"( data-field="kinematics-collision.a-reach.kin.note")";
    const std::size_t at = html.find(kAttr);
    ASSERT_NE(at, std::string::npos) << "夹具应含目标 data-field 属性";
    html.erase(at, kAttr.size());
    fx.html.bytes = asBytes(html);

    NullCsvReaderFactory factory;
    FieldConsistencyChecker checker(factory);
    const ConsistencyResult result = checker.check(fx.input);
    // expected＝源单元格值（missing 维度的定位诊断面）。
    expectSingleMismatch(result, ReportRenderFormat::Html,
                         "kinematics-collision.a-reach.kin.note", kMismatchDimMissing,
                         "拐点校验通过", std::string(kMismatchAbsentMark));
}

TEST(ConsistencyHtmlBadSampleTest, DeleteWholeValueCell_MissingLocated_RPT07_ACC1)
{
    // 坏样本形态二：删整个 data-field 单元格——行结构仍在（缺值列），
    // 同样落入 missing 轨（结构层完好——不是 parse-failed）。
    TwoFormatFixture fx;
    makeTwoFormatFixture(&fx);
    std::string html = asText(fx.html.bytes);
    static const std::string kCell =
        R"(<td data-field="kinematics-collision.a-reach.kin.estimate">2.5)"
        R"(<span class="qualifier" data-qualifier="estimated">估算</span></td>)";
    const std::size_t at = html.find(kCell);
    ASSERT_NE(at, std::string::npos) << "夹具应含目标值单元格";
    html.erase(at, kCell.size());
    fx.html.bytes = asBytes(html);

    NullCsvReaderFactory factory;
    FieldConsistencyChecker checker(factory);
    const ConsistencyResult result = checker.check(fx.input);
    expectSingleMismatch(result, ReportRenderFormat::Html,
                         "kinematics-collision.a-reach.kin.estimate", kMismatchDimMissing,
                         "2.5", std::string(kMismatchAbsentMark));
}

TEST(ConsistencyHtmlBadSampleTest, ChangeUnitCell_UnitLocated_RPT07_ACC3)
{
    // 单位维度坏样本：mm→cm（显示单位投影失真——value 恒真、unit 失配）。
    TwoFormatFixture fx;
    makeTwoFormatFixture(&fx);
    std::string html = asText(fx.html.bytes);
    static const std::string kUnitCell = R"(<td class="unit">mm</td>)";
    const std::size_t at = html.find(kUnitCell);
    ASSERT_NE(at, std::string::npos) << "夹具应含目标单位单元格";
    html.replace(at, kUnitCell.size(), R"(<td class="unit">cm</td>)");
    fx.html.bytes = asBytes(html);

    NullCsvReaderFactory factory;
    FieldConsistencyChecker checker(factory);
    const ConsistencyResult result = checker.check(fx.input);
    expectSingleMismatch(result, ReportRenderFormat::Html,
                         "kinematics-collision.a-reach.kin.reach", kMismatchDimUnit, "mm", "cm");
}

TEST(ConsistencyHtmlBadSampleTest, ChangeStatusDisplay_StatusLocated_RPT07_ACC3)
{
    // 状态维度坏样本（HTML 携带中文显示名——经共享映射表反查 token 后
    // 与矩阵 token 比对；"可行"→"工程不可行"＝token 失配）。
    TwoFormatFixture fx;
    makeTwoFormatFixture(&fx);
    std::string html = asText(fx.html.bytes);
    static const std::string kStatusCell = "<td>可行</td>";
    const std::size_t at = html.find(kStatusCell);   // 首个＝矩阵序首字段行（kin.reach）
    ASSERT_NE(at, std::string::npos) << "夹具应含目标状态单元格";
    html.replace(at, kStatusCell.size(), "<td>工程不可行</td>");
    fx.html.bytes = asBytes(html);

    NullCsvReaderFactory factory;
    FieldConsistencyChecker checker(factory);
    const ConsistencyResult result = checker.check(fx.input);
    expectSingleMismatch(result, ReportRenderFormat::Html,
                         "kinematics-collision.a-reach.kin.reach", kMismatchDimStatus,
                         "feasible", "engineering-infeasible");
}

TEST(ConsistencyHtmlBadSampleTest, DeleteQualifierSpan_QualifierLocated_RPT07_ACC3)
{
    // 限定语丢失坏样本（§6.4 硬约束——任何格式省略限定语＝mismatch；
    // §9.4 非法调用行"跳过限定语维度"的反面：限定语维度必须全格式比对）。
    TwoFormatFixture fx;
    makeTwoFormatFixture(&fx);
    std::string html = asText(fx.html.bytes);
    static const std::string kSpan =
        R"(<span class="qualifier" data-qualifier="estimated">估算</span>)";
    const std::size_t at = html.find(kSpan);
    ASSERT_NE(at, std::string::npos) << "夹具应含目标限定语标记";
    html.erase(at, kSpan.size());
    fx.html.bytes = asBytes(html);

    NullCsvReaderFactory factory;
    FieldConsistencyChecker checker(factory);
    const ConsistencyResult result = checker.check(fx.input);
    expectSingleMismatch(result, ReportRenderFormat::Html,
                         "kinematics-collision.a-reach.kin.estimate", kMismatchDimQualifier,
                         "estimated", "");
}

// =====================================================================
// parse-failed 错误轨（acceptance 4——回读本身失败〔格式损坏〕→
// ConsistencyMismatch 抛出，detail 含 dimension=parse-failed）
// =====================================================================

TEST(ConsistencyParseFailedTest, HtmlTruncatedInsideEntryRow_ParseFailedThrown_RPT07_ACC4)
{
    // 截断点取条目行内部（data-field 属性后 10 字节）——扫描器处于
    // 结构中段（行/单元格未闭合）→ parse-failed（不是 missing：结构
    // 层损坏时"哪个字段不一致"无从谈起）。
    TwoFormatFixture fx;
    makeTwoFormatFixture(&fx);
    std::string html = asText(fx.html.bytes);
    const std::size_t anchor = html.find("data-field=\"kinematics-collision.a-reach.kin.reach\"");
    ASSERT_NE(anchor, std::string::npos);
    fx.html.bytes = asBytes(html.substr(0, anchor + 40));

    NullCsvReaderFactory factory;
    FieldConsistencyChecker checker(factory);
    try {
        checker.check(fx.input);
        FAIL() << "HTML 结构截断应抛 ConsistencyMismatch";
    } catch (const ReportError& e) {
        EXPECT_EQ(e.code(), ReportErrorCode::ConsistencyMismatch);
        EXPECT_NE(std::string(e.what()).find(kMismatchDimParseFailed), std::string::npos);
        EXPECT_NE(std::string(e.what()).find("html"), std::string::npos);
    }
}

// =====================================================================
// 纯函数纪律（acceptance 4——同输入同结论、不修改任何输入）
// =====================================================================

TEST(ConsistencyPurityTest, SameInputSameResult_InputsUnmodified_RPT07_ACC4)
{
    TwoFormatFixture fx;
    makeTwoFormatFixture(&fx);

    // 注入坏样本（两条 mismatch——比"恒等返回/恒真"更有区分度的输入）。
    std::string html = asText(fx.html.bytes);
    const std::size_t at = html.find("<td class=\"unit\">mm</td>");
    ASSERT_NE(at, std::string::npos);
    html.replace(at, std::string("<td class=\"unit\">mm</td>").size(), "<td class=\"unit\">cm</td>");
    fx.html.bytes = asBytes(html);

    const std::vector<std::uint8_t> htmlBefore = fx.html.bytes;
    const std::vector<std::uint8_t> jsonBefore = fx.jsonMirror.bytes;

    NullCsvReaderFactory factory;
    FieldConsistencyChecker checker(factory);
    const ConsistencyResult first = checker.check(fx.input);
    const ConsistencyResult second = checker.check(fx.input);

    // 同输入同结论：consistent/fieldCount/mismatches 逐项相等（纯函数）。
    EXPECT_EQ(first.consistent, second.consistent);
    EXPECT_EQ(first.fieldCount, second.fieldCount);
    ASSERT_EQ(first.mismatches.size(), second.mismatches.size());
    for (std::size_t i = 0; i < first.mismatches.size(); ++i) {
        EXPECT_EQ(first.mismatches[i].format, second.mismatches[i].format);
        EXPECT_EQ(first.mismatches[i].fieldKey, second.mismatches[i].fieldKey);
        EXPECT_EQ(first.mismatches[i].dimension, second.mismatches[i].dimension);
        EXPECT_EQ(first.mismatches[i].expected, second.mismatches[i].expected);
        EXPECT_EQ(first.mismatches[i].actual, second.mismatches[i].actual);
    }
    // 不修改任何输入：工件字节逐位不变（check 全程 const 只读）。
    EXPECT_EQ(htmlBefore, fx.html.bytes);
    EXPECT_EQ(jsonBefore, fx.jsonMirror.bytes);
    ASSERT_FALSE(first.mismatches.empty());   // 夹具有效性自证（非平凡一致）
    EXPECT_EQ(first.mismatches[0].dimension, kMismatchDimUnit);
}
