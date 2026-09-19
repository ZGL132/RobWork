/**
 * @file   RenderTest.cpp
 * @brief  RPT-T06 渲染链单元测试——FieldMatrix 提取（确定性/值投影/限定语/
 *         单位闸/曲线引用）＋HTML 渲染器（HTML5 静态契约/确定性/data-field
 *         锚/脱敏双保险/资格声明门/失败无外泄）。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/RPT-T06.json acceptance 1~5 的单元内面对
 *     （acceptance 6 的 io 注入 fake 协作面随
 *     contract_test/RenderCrossUnitContractTest.cpp——§3.4 测试目标分工：
 *     与 io 写出设施的协作属跨单元契约面）；
 *   - units/reporting.md §8.1~§8.4（渲染总则/格式契约/字段矩阵）、§4.3.3
 *     （脱敏双保险）、§6.4（限定语词表）、§8.2①（两类声明前置断言）。
 *
 * 替身边界声明（RP-STATE-4 同款纪律）：报告对象一律经 ReviewReport::make
 *   构造（合法实例唯一生产者）；HTML 渲染器零 io 依赖（§9.3 副作用行），
 *   本文件无 io 替身——JSON/CSV 的 canonical 写出协作经注入 fake，随契约
 *   测试落位。脱敏双保险用例直接消费 diagnostics::RedactionService 实现
 *   （C-3 登记边的真实服务——不用替身，双保险语义才有真实凭证）。
 *
 * 确定性夹具（与 ReportModelTest 同款纪律）：身份/数据源字段逐位固定
 *   （禁随机 generate——确定性断言的前提）；ReportId 用确定性铺位值而非
 *   generate()（同脚本两次构建逐位一致——二次渲染字节比对的基础）。
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

#include <sdurws/ird/diagnostics/Redaction.hpp>   // 真实脱敏服务（双保险用例凭证）
#include <sdurws/ird/reporting/Render.hpp>
#include <sdurws/ird/reporting/Sections.hpp>      // kSectionKinematicsCollision 等词表常量

namespace {

using namespace sdurws::ird::reporting;
namespace co = sdurws::ird::core;         // 短别名（与 ReportModelTest 同款）
namespace core = sdurws::ird::core;       // 全名别名（夹具正文两种拼写并存的统一承接）
namespace ev = sdurws::ird::evidence;
namespace evidence = sdurws::ird::evidence;
namespace diagnostics = sdurws::ird::diagnostics;

// =====================================================================
// 确定性夹具身份（逐位铺位——与 ReportModelTest 同款，自持不共享）
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

/// Superseded 快照（原因非空——presence 纪律）。
CurrentnessSnapshot supersededSnapshot(co::RevisionId head, std::int64_t at)
{
    CurrentnessSnapshot c;
    c.status = ev::CurrentnessStatus::Superseded;
    c.evaluatedAgainst.headRevision = head;
    c.evaluatedAgainst.contextSummary = "夹具上下文（依赖已变化）";
    c.computedAtUtc = fixedTime(at);
    evidence::InvalidationReason reason;
    reason.dependencyKey = "object.model";
    reason.kind = evidence::InvalidationKind::ObjectContentChanged;
    reason.detail = "对象内容版本已前进";
    c.reasons = {reason};
    return c;
}

/// 数值字段快捷构造（SI 真值＋来源；单位可选）。
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

/// 合法结果引用（r1＝Verified/Completed/Feasible/Current/formalPass 成立）。
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

/// 合法结果引用（r2＝Quick/Completed/Feasible/Superseded/formalPass 不成立
/// ——screening-only＋historical-superseded 限定语的数据源）。
ResultRefSnapshot quickSupersededResult(co::RevisionId revision, co::ObjectId case2)
{
    ResultRefSnapshot r;
    r.runId = fixtureId<co::RunId>(0x24);
    r.task.project = fixtureId<co::ProjectId>(0x11);
    r.task.branch = fixtureId<co::BranchId>(0x12);
    r.task.revision = revision;
    r.task.run = r.runId;
    r.task.attempt.value = 2;
    r.evaluationKey = "kin-batch-ik";
    r.evaluatorContractVersion = 1;
    r.mode = co::EvaluationMode::Quick;
    r.outcome = co::TaskOutcome::Completed;
    r.engineeringStatus = co::EngineeringStatus::Feasible;
    r.snapshotId = cid(0x20);
    r.sliceId = cid(0x40);
    r.inputBaselineId = cid(0x60);
    r.caseScope = {case2};
    r.eligibility.formalPass = false;
    r.currentness = supersededSnapshot(revision, 1700000001);
    r.producer.productVersion = "0.1.0";
    return r;
}

// =====================================================================
// 标准 B 级报告夹具（ acceptance 1~5 的公共底座——各用例单点变异）
// =====================================================================

/// 标准夹具章节集：
///   order1 project-scheme  Populated（case.count＝3 无量纲）
///   order2 input-summary   NoFormalResult（缺项 1 条）
///   order3 kinematics-collision Populated（两条目：值族全形态＋Quick 绑定）
ReviewReportFields makeStandardFields()
{
    const co::RevisionId revision = fixtureId<co::RevisionId>(0x13);
    const co::ObjectId case1 = fixtureObject(0x01);
    const co::ObjectId case2 = fixtureObject(0x02);

    ReviewReportFields f;
    f.reportId = fixtureId<ReportId>(0x70);
    f.level = ReportLevel::B;
    f.project = fixtureId<co::ProjectId>(0x11);
    f.branch = fixtureId<co::BranchId>(0x12);
    f.revision = revision;
    f.revisionSeq = 7;

    f.resultRefs.push_back(feasibleResult(revision, case1));
    f.resultRefs.push_back(quickSupersededResult(revision, case2));

    // 章节 1：project-scheme（Populated——无量纲计数）。
    ReviewReportSection scheme;
    scheme.sectionId = kSectionProjectScheme;
    scheme.sectionVersion = 1;
    scheme.selected = true;
    scheme.status = SectionStatus::Populated;
    SectionEntryView schemeEntry;
    schemeEntry.entryKey = "scheme-summary";
    schemeEntry.fields.push_back(
        numericField("case.count", 3.0, core::ProvenanceKind::UserProvided));
    schemeEntry.result.runId = f.resultRefs[0].runId;
    schemeEntry.result.fieldPath = "payload.caseCount";
    schemeEntry.caseScope = {case1};
    scheme.entries.push_back(schemeEntry);
    scheme.sourceResults = {f.resultRefs[0].runId};
    scheme.renderHint = RenderHint::Table;
    scheme.order = 1;
    f.sections.push_back(scheme);

    // 章节 2：input-summary（NoFormalResult——缺正式结果默认不选＋缺项）。
    ReviewReportSection input;
    input.sectionId = kSectionInputSummary;
    input.sectionVersion = 1;
    input.selected = false;
    input.status = SectionStatus::NoFormalResult;
    input.missingItems.push_back(MissingItemView{"common.snapshot-identity", "该域无已完成结果"});
    input.renderHint = RenderHint::MetadataBlock;
    input.order = 2;
    f.sections.push_back(input);

    // 章节 3：kinematics-collision（Populated——值族全形态＋限定语族）。
    ReviewReportSection kin;
    kin.sectionId = kSectionKinematicsCollision;
    kin.sectionVersion = 1;
    kin.selected = true;
    kin.status = SectionStatus::Populated;
    kin.sourceResults = {f.resultRefs[0].runId, f.resultRefs[1].runId};
    // 引用 Superseded 结果的章节必填 currentness（§6.3）。
    kin.currentness = supersededSnapshot(revision, 1700000002);

    // 条目 a-reach（绑定 r1——值投影五形态）。
    SectionEntryView reach;
    reach.entryKey = "a-reach";
    reach.fields.push_back(numericField("kin.reach", 1.5, core::ProvenanceKind::UserProvided,
                                        *co::UnitToken::find("mm")));          // mm 投影
    FieldValue text;
    text.key = "kin.note";
    text.quantity = co::SourcedValue<double>::notProvided();
    text.text = "拐点校验通过";
    reach.fields.push_back(text);                                            // 文本字段
    FieldValue notApplicable;
    notApplicable.key = "kin.na-ratio";
    notApplicable.quantity = co::SourcedValue<double>::notApplicable();
    reach.fields.push_back(notApplicable);                                   // 不适用态
    reach.fields.push_back(
        numericField("kin.estimate", 2.5, core::ProvenanceKind::GeometricEstimate)); // 估算
    FieldValue invalid;
    invalid.key = "kin.bad-raw";
    invalid.quantity = co::SourcedValue<double>::invalid("abc");             // 非法保留原文
    reach.fields.push_back(invalid);
    reach.result.runId = f.resultRefs[0].runId;
    reach.result.fieldPath = "payload.reach";
    EvidenceBinding okBinding;
    okBinding.itemId = "kin.reach-per-task-point";
    okBinding.status = evidence::EvidenceItemStatus::Satisfied;
    okBinding.digest = cid(0x10).bytes;
    okBinding.caseScope = {case1};
    reach.evidence = {okBinding};
    reach.caseScope = {case1};
    kin.entries.push_back(reach);

    // 条目 b-quick（绑定 r2——Quick＋Superseded＋证据 Missing 限定语族）。
    SectionEntryView quick;
    quick.entryKey = "b-quick";
    quick.fields.push_back(numericField("kin.quick-count", 2.0, core::ProvenanceKind::UserProvided));
    quick.result.runId = f.resultRefs[1].runId;
    quick.result.fieldPath = "payload.quick";
    EvidenceBinding missingBinding;
    missingBinding.itemId = "kin.mode-evidence";
    missingBinding.status = evidence::EvidenceItemStatus::Missing;
    missingBinding.caseScope = {case2};
    quick.evidence = {missingBinding};
    quick.caseScope = {case2};
    kin.entries.push_back(quick);

    kin.renderHint = RenderHint::Table;
    kin.order = 3;
    f.sections.push_back(kin);

    // 报告级证据引用（去重并集——Satisfied/NotApplicable/Missing 三形态）。
    EvidenceRefEntry okRef;
    okRef.itemId = "kin.reach-per-task-point";
    okRef.itemClass = evidence::EvidenceItemClass::Required;
    okRef.status = evidence::EvidenceItemStatus::Satisfied;
    okRef.artifactDigest = cid(0x10).bytes;
    okRef.caseScope = {case1};
    okRef.sourceSection = kSectionKinematicsCollision;
    f.evidenceRefs.push_back(okRef);
    EvidenceRefEntry naRef;
    naRef.itemId = "kin.region-coverage";
    naRef.itemClass = evidence::EvidenceItemClass::Required;
    naRef.status = evidence::EvidenceItemStatus::NotApplicable;
    naRef.notApplicableReason = "纯关节路径不涉及区域覆盖";
    naRef.sourceSection = kSectionKinematicsCollision;
    f.evidenceRefs.push_back(naRef);
    EvidenceRefEntry missingRef;
    missingRef.itemId = "kin.mode-evidence";
    missingRef.itemClass = evidence::EvidenceItemClass::Required;
    missingRef.status = evidence::EvidenceItemStatus::Missing;
    missingRef.sourceSection = kSectionKinematicsCollision;
    f.evidenceRefs.push_back(missingRef);

    // 报告级诊断引用（含路径＋0x 地址的定位名——脱敏双保险用例数据源）。
    DiagRefEntry diag;
    diag.code = "KIN-IK-NO-SOLUTION";
    diag.severity = diagnostics::DiagnosticSeverity::Warning;
    diag.category = diagnostics::DiagnosticCategory::DataInsufficient;
    diag.localName = "D:\\私有目录\\机密\\model.stl 0x7FFE00001234";
    diag.occurrences = 2;
    diag.sourceSection = kSectionKinematicsCollision;
    diag.sourceRun = f.resultRefs[0].runId;
    f.diagRefs.push_back(diag);

    // 当前性汇总/复现要素/评审元数据（B 级最小合法形态）。
    f.currentnessSummary.perResult.push_back(
        ResultCurrentnessEntry{f.resultRefs[0].runId, currentSnapshot(revision, 1700000000)});
    f.currentnessSummary.perResult.push_back(
        ResultCurrentnessEntry{f.resultRefs[1].runId, supersededSnapshot(revision, 1700000001)});
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

/// 冻结标准报告（make 唯一生产者——确定性身份）。
ReviewReport makeStandardReport() { return ReviewReport::make(makeStandardFields(), frozenReportLevelRule()); }

/// 脱敏服务（真实实现——双保险用例凭证；默认策略 RootOnly）。
diagnostics::RedactionService& redactionService()
{
    static diagnostics::RedactionService service;   // 测试进程级单例（无 failureSink——静默降级形态）
    return service;
}

/// 按 fieldKey 查矩阵单元格（测试可读性辅助）。
const FieldCell* findCell(const FieldMatrix& matrix, const std::string& fieldKey)
{
    for (const FieldCell& cell : matrix) {
        if (cell.fieldKey == fieldKey) {
            return &cell;
        }
    }
    return nullptr;
}

// =====================================================================
// 字段矩阵（acceptance 4——单次投影＋确定性）
// =====================================================================

TEST(FieldMatrixTest, SameReportSameMatrix_TraversalOrderBySectionEntry_RPT06_ACC4)
{
    // 同报告两次提取逐单元格全等（§8.4"同报告同矩阵"——确定性单测锁定）。
    const ReviewReport report = makeStandardReport();
    const FieldMatrix first = extractFieldMatrix(report);
    const FieldMatrix second = extractFieldMatrix(report);
    ASSERT_EQ(first.size(), second.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        ASSERT_EQ(first[i], second[i]) << "下标 " << i << " 单元格不一致";
    }

    // 遍历序＝章节 order 序＋条目 entryKey 序（§8.4 冻结序）——
    // project-scheme(1) → kinematics-collision(3)；input-summary 无条目。
    // 单元格总数＝1（scheme）＋5（a-reach）＋1（b-quick）＝7。
    ASSERT_EQ(first.size(), 7u);
    EXPECT_EQ(first[0].sectionId, kSectionProjectScheme);
    EXPECT_EQ(first[0].fieldKey, "project-scheme.scheme-summary.case.count");
    // kin 章节条目序：a-reach（5 字段）先于 b-quick（entryKey 字典序）。
    EXPECT_EQ(first[1].sectionId, kSectionKinematicsCollision);
    EXPECT_EQ(first[1].entryKey, "a-reach");
    EXPECT_EQ(first[5].fieldKey, "kinematics-collision.a-reach.kin.bad-raw");
    EXPECT_EQ(first[6].entryKey, "b-quick");
    EXPECT_EQ(first[6].fieldKey, "kinematics-collision.b-quick.kin.quick-count");

    // input-summary（NoFormalResult）无条目——不产生单元格（矩阵只承载
    // 字段值；缺项清单为章节结构，由渲染器直接消费报告结构呈现）。
    for (const FieldCell& cell : first) {
        EXPECT_NE(cell.sectionId, kSectionInputSummary);
    }
}

TEST(FieldMatrixTest, ValueProjection_FourStateWords_ToChars_AndUnitConversion_RPT06_ACC2)
{
    // 值文本规范形（§8.3 值列规则＋§8.4 valueRepr）：
    //   数值→显示单位换算后 to_chars；文本→原样；四态→呈现词/原文。
    const ReviewReport report = makeStandardReport();
    const FieldMatrix matrix = extractFieldMatrix(report);

    // 无量纲计数：3.0 SI → "3"（to_chars 最短往返）。
    const FieldCell* count = findCell(matrix, "project-scheme.scheme-summary.case.count");
    ASSERT_NE(count, nullptr);
    EXPECT_EQ(count->valueRepr, "3");
    EXPECT_FALSE(count->unit.has_value());

    // 长度字段：1.5 m(SI) → mm 冻结集投影 = 1500（core tryConvert 唯一入口）。
    const FieldCell* reach = findCell(matrix, "kinematics-collision.a-reach.kin.reach");
    ASSERT_NE(reach, nullptr);
    EXPECT_EQ(reach->valueRepr, "1500");
    ASSERT_TRUE(reach->unit.has_value());
    EXPECT_EQ(*reach->unit, "mm");

    // 文本字段原样；不适用→"不适用"；估算→to_chars；非法→保留原文（NFR-COR-03）。
    const FieldCell* note = findCell(matrix, "kinematics-collision.a-reach.kin.note");
    ASSERT_NE(note, nullptr);
    EXPECT_EQ(note->valueRepr, "拐点校验通过");
    const FieldCell* na = findCell(matrix, "kinematics-collision.a-reach.kin.na-ratio");
    ASSERT_NE(na, nullptr);
    EXPECT_EQ(na->valueRepr, "不适用");
    const FieldCell* estimate = findCell(matrix, "kinematics-collision.a-reach.kin.estimate");
    ASSERT_NE(estimate, nullptr);
    EXPECT_EQ(estimate->valueRepr, "2.5");
    const FieldCell* badRaw = findCell(matrix, "kinematics-collision.a-reach.kin.bad-raw");
    ASSERT_NE(badRaw, nullptr);
    EXPECT_EQ(badRaw->valueRepr, "abc");

    // status 维度＝绑定结果工程判定 token（§6.6 结论列——AT-22 status 维度）。
    EXPECT_EQ(count->status, "feasible");
}

TEST(FieldMatrixTest, UnitPreferenceConflict_Rejected_DataInvalid_RPT06_ACC2)
{
    // 字段显示单位与报告级 UnitPreference 冻结集同量纲条目不一致 →
    // DataInvalid（unitPreference 冻结入身份的执行侧一致性闸——KIN-12）。
    ReviewReportFields fields = makeStandardFields();
    // 字段带 mm，报告冻结集改为 cm（同量纲不同 token——冲突面）。
    fields.unitPreference.displayUnits[co::QuantityKind::Length] = *co::UnitToken::find("cm");
    const ReviewReport report = ReviewReport::make(std::move(fields), frozenReportLevelRule());
    EXPECT_THROW(extractFieldMatrix(report), ReportError);
    try {
        extractFieldMatrix(report);
    } catch (const ReportError& e) {
        EXPECT_EQ(e.code(), ReportErrorCode::DataInvalid);
    }
}

TEST(FieldMatrixTest, QualifierDerivation_Estimated_Screening_Superseded_DataInsufficient_RPT06_ACC5)
{
    // §6.4 限定语结构性推导（固定序去重——提取期一次性）。
    const ReviewReport report = makeStandardReport();
    const FieldMatrix matrix = extractFieldMatrix(report);

    // 估算字段：estimated（ValueProvenance GeometricEstimate——§6.4 行 1）。
    const FieldCell* estimate = findCell(matrix, "kinematics-collision.a-reach.kin.estimate");
    ASSERT_NE(estimate, nullptr);
    ASSERT_EQ(estimate->qualifier.size(), 1u);
    EXPECT_EQ(estimate->qualifier[0], QualifierToken::Estimated);

    // 不适用字段：not-applicable（四态显式标记——§6.4 行 7）。
    const FieldCell* na = findCell(matrix, "kinematics-collision.a-reach.kin.na-ratio");
    ASSERT_NE(na, nullptr);
    ASSERT_EQ(na->qualifier.size(), 1u);
    EXPECT_EQ(na->qualifier[0], QualifierToken::NotApplicable);

    // Quick＋Superseded＋证据 Missing 条目：screening-only＋historical-
    // superseded＋data-insufficient 三限定语齐备（§6.4 行 2/4/6）。
    const FieldCell* quick = findCell(matrix, "kinematics-collision.b-quick.kin.quick-count");
    ASSERT_NE(quick, nullptr);
    EXPECT_TRUE(std::find(quick->qualifier.begin(), quick->qualifier.end(),
                          QualifierToken::ScreeningOnly)
                != quick->qualifier.end());
    EXPECT_TRUE(std::find(quick->qualifier.begin(), quick->qualifier.end(),
                          QualifierToken::HistoricalSuperseded)
                != quick->qualifier.end());
    EXPECT_TRUE(std::find(quick->qualifier.begin(), quick->qualifier.end(),
                          QualifierToken::DataInsufficient)
                != quick->qualifier.end());
    // 当前性呈现 token（superseded——矩阵 currentness 维度）。
    ASSERT_TRUE(quick->currentness.has_value());
    EXPECT_EQ(*quick->currentness, "superseded");

    // 结果/证据引用规范文本（引用而非复制——§8.3）。
    EXPECT_EQ(quick->resultRef, fixtureId<co::RunId>(0x24).toCanonical());
    ASSERT_EQ(quick->evidenceRef.size(), 1u);
    EXPECT_EQ(quick->evidenceRef[0], "kin.mode-evidence");   // 无摘要绑定＝裸 itemId
    const FieldCell* reach = findCell(matrix, "kinematics-collision.a-reach.kin.reach");
    ASSERT_NE(reach, nullptr);
    // Satisfied 绑定＝itemId@digest 前 12 hex（§8.3 引用规范文本）。
    ASSERT_EQ(reach->evidenceRef.size(), 1u);
    EXPECT_EQ(reach->evidenceRef[0],
              "kin.reach-per-task-point@" + [&] {
                  static const char* kHex = "0123456789abcdef";
                  std::string hex;
                  for (std::size_t i = 0; i < 6; ++i) {
                      const std::uint8_t b = cid(0x10).bytes[i];
                      hex += kHex[(b >> 4) & 0x0F];
                      hex += kHex[b & 0x0F];
                  }
                  return hex;
              }());
}

TEST(FieldMatrixTest, CurveRefSection_ValueIsReferenceText_NotCopy_RPT06_ACC2)
{
    // 曲线类复杂结构以引用呈现（§8.1/RP-CONS-4）：CurveRef 提示章节中
    // 无内联值的字段 → "run-<hex>#<fieldPath>" 引用文本；不复制大数组
    // （模型层不携带曲线数组——结构性不复制，渲染层引用呈现）。
    ReviewReportFields fields = makeStandardFields();
    ReviewReportSection curve;
    curve.sectionId = kSectionOptimizationCandidates;
    curve.sectionVersion = 1;
    curve.selected = true;
    curve.status = SectionStatus::Populated;
    SectionEntryView entry;
    entry.entryKey = "torque-curve";
    // 曲线字段：无内联值（NotProvided）——引用呈现形态。
    FieldValue curveField;
    curveField.key = "opt.torque-series";
    curveField.quantity = co::SourcedValue<double>::notProvided();
    entry.fields.push_back(curveField);
    entry.result.runId = fields.resultRefs[0].runId;
    entry.result.fieldPath = "payload.curves[0].torque";
    entry.caseScope = {fixtureObject(0x01)};
    curve.entries.push_back(entry);
    curve.sourceResults = {fields.resultRefs[0].runId};
    curve.renderHint = RenderHint::CurveRef;
    curve.order = 6;
    fields.sections.push_back(curve);
    const ReviewReport report = ReviewReport::make(std::move(fields), frozenReportLevelRule());

    const FieldMatrix matrix = extractFieldMatrix(report);
    const FieldCell* cell = findCell(matrix, "optimization-candidates.torque-curve.opt.torque-series");
    ASSERT_NE(cell, nullptr);
    EXPECT_EQ(cell->valueRepr, fixtureId<co::RunId>(0x14).toCanonical() + "#payload.curves[0].torque");
    EXPECT_EQ(cell->unit, std::nullopt);
}

TEST(FieldMatrixTest, BindingOutOfRange_Rejected_DataInvalid_RPT06_ACC4)
{
    // 条目结果绑定越界（resultRefs 无该运行）→ 拒绝——绑定可解析性是
    // §4.2 字段校验原语的一部分（ReviewReport::make 即拒绝，错误码
    // EvidenceRefInvalid）；extractFieldMatrix 内的同名检查为防御面
    // （make 产出的报告不可达该态——测试以 make 拒绝语义钉住闸门）。
    ReviewReportFields fields = makeStandardFields();
    fields.sections[2].entries[0].result.runId = fixtureId<co::RunId>(0x99);
    bool rejected = false;
    try {
        ReviewReport::make(std::move(fields), frozenReportLevelRule());
    } catch (const ReportError& e) {
        rejected = true;
        EXPECT_EQ(e.code(), ReportErrorCode::EvidenceRefInvalid);
    }
    EXPECT_TRUE(rejected);
}

// =====================================================================
// HTML 渲染器（acceptance 1/3/5）
// =====================================================================

TEST(HtmlRenderTest, ByteDeterministic_SecondRenderIdentical_RPT06_ACC1)
{
    // 同报告同格式二次渲染字节相同（§8.1 确定性总则——RP-CONS-3）；
    // 摘要随字节一致（core SHA-256 唯一算法）。
    const ReviewReport report = makeStandardReport();
    const FieldMatrix matrix = extractFieldMatrix(report);
    HtmlReportRenderer renderer(redactionService());

    const RenderOutcome first = renderer.render(report, matrix, ReportRenderFormat::Html);
    const RenderOutcome second = renderer.render(report, matrix, ReportRenderFormat::Html);
    ASSERT_TRUE(first.artifact.has_value());
    ASSERT_TRUE(second.artifact.has_value());
    ASSERT_EQ(first.artifact->bytes.size(), second.artifact->bytes.size());
    EXPECT_EQ(first.artifact->bytes, second.artifact->bytes);
    EXPECT_EQ(first.artifact->digest, second.artifact->digest);
    EXPECT_EQ(first.artifact->rendererVersion, kReportRendererVersion);
    EXPECT_EQ(first.artifact->templateVersion, kHtmlTemplateVersion);
    EXPECT_TRUE(first.artifact->sourceReportIdentity == report.contentIdentity());
}

TEST(HtmlRenderTest, Html5StaticContract_ZeroExternalZeroScript_LfNoBom_RPT06_ACC5)
{
    // HTML5 静态契约（§8.1）：零外部依赖零脚本、样式内联、UTF-8 无 BOM、
    // 行尾 LF（无 CR——HTML/JSON 行尾纪律）。
    const ReviewReport report = makeStandardReport();
    const FieldMatrix matrix = extractFieldMatrix(report);
    HtmlReportRenderer renderer(redactionService());
    const RenderOutcome outcome = renderer.render(report, matrix, ReportRenderFormat::Html);
    ASSERT_TRUE(outcome.artifact.has_value());
    const std::string html(outcome.artifact->bytes.begin(), outcome.artifact->bytes.end());

    // 无 BOM（首三字节不得为 EF BB BF）。
    ASSERT_GE(html.size(), 3u);
    EXPECT_FALSE(static_cast<unsigned char>(html[0]) == 0xEF
                 && static_cast<unsigned char>(html[1]) == 0xBB
                 && static_cast<unsigned char>(html[2]) == 0xBF);
    // HTML5 起手＋charset 声明。
    EXPECT_EQ(html.substr(0, 15), "<!DOCTYPE html>");
    EXPECT_NE(html.find("<meta charset=\"utf-8\">"), std::string::npos);
    // 零脚本/零外链/零外部资源引用（离线打开一致——§8.1）。
    EXPECT_EQ(html.find("<script"), std::string::npos);
    EXPECT_EQ(html.find("http://"), std::string::npos);
    EXPECT_EQ(html.find("https://"), std::string::npos);
    EXPECT_EQ(html.find("<link"), std::string::npos);
    EXPECT_EQ(html.find("src="), std::string::npos);
    EXPECT_EQ(html.find("@import"), std::string::npos);
    // 样式内联（<style> 在 head 内）。
    EXPECT_NE(html.find("<style>"), std::string::npos);
    // 行尾 LF（无 CR 字节——CSV 的 CRLF 归 io 方言，与 HTML 无涉）。
    EXPECT_EQ(html.find('\r'), std::string::npos);
}

TEST(HtmlRenderTest, DataFieldAnchors_AndQualifierMarks_MachineExtractable_RPT06_ACC5)
{
    // 机器可提取锚（§8.1/D-08）：每数据单元格 data-field="<fieldKey>"；
    // 限定语逐 token data-qualifier 标记（§6.4 结构性输出）。
    const ReviewReport report = makeStandardReport();
    const FieldMatrix matrix = extractFieldMatrix(report);
    HtmlReportRenderer renderer(redactionService());
    const RenderOutcome outcome = renderer.render(report, matrix, ReportRenderFormat::Html);
    ASSERT_TRUE(outcome.artifact.has_value());
    const std::string html(outcome.artifact->bytes.begin(), outcome.artifact->bytes.end());

    // 每个矩阵单元格恰有一个 data-field 锚（值单源的 HTML 面）。
    for (const FieldCell& cell : matrix) {
        const std::string anchor = "data-field=\"" + cell.fieldKey + "\"";
        EXPECT_NE(html.find(anchor), std::string::npos) << "缺锚：" << anchor;
    }
    // 限定语标记与 §6.4 文案（估算/输入已变化/数据不足）。
    EXPECT_NE(html.find("data-qualifier=\"estimated\""), std::string::npos);
    EXPECT_NE(html.find("估算"), std::string::npos);
    EXPECT_NE(html.find("data-qualifier=\"historical-superseded\""), std::string::npos);
    EXPECT_NE(html.find("输入已变化"), std::string::npos);
    EXPECT_NE(html.find("data-qualifier=\"data-insufficient\""), std::string::npos);
    // 值文本规范形出现在锚单元格内（与矩阵同源）。
    EXPECT_NE(html.find("1500"), std::string::npos);      // mm 投影值
    EXPECT_NE(html.find("不适用"), std::string::npos);     // 四态呈现词
    // 章节锚点与状态徽标（§8.1 结构）。
    EXPECT_NE(html.find("id=\"sec-kinematics-collision\""), std::string::npos);
    EXPECT_NE(html.find("未选择（缺正式结果）"), std::string::npos);
    // 目录锚（§5 order）。
    EXPECT_NE(html.find("id=\"toc\""), std::string::npos);
}

TEST(HtmlRenderTest, SanitizationDoubleInsurance_NoRawPathNoAddress_RPT06_ACC3)
{
    // 诊断脱敏双保险（§4.3.3/NFR-SEC-07）：渲染侧再次过脱敏——用户级
    // 报告不含未脱敏本机路径、内存地址；真实 RedactionService 凭证。
    const ReviewReport report = makeStandardReport();
    const FieldMatrix matrix = extractFieldMatrix(report);
    HtmlReportRenderer renderer(redactionService());
    const RenderOutcome outcome = renderer.render(report, matrix, ReportRenderFormat::Html);
    ASSERT_TRUE(outcome.artifact.has_value());
    const std::string html(outcome.artifact->bytes.begin(), outcome.artifact->bytes.end());

    // 原始整路径不得出现（RootOnly 策略——盘符＋一级目录＋…＋文件名）；
    // 二级目录"机密"与 0x 地址必须被替换。
    EXPECT_EQ(html.find("机密"), std::string::npos);
    EXPECT_EQ(html.find("0x7FFE00001234"), std::string::npos);
    // 一级目录与文件名保留（RootOnly 的可读性折中——§7.7）。
    EXPECT_NE(html.find("D:\\私有目录"), std::string::npos);
    EXPECT_NE(html.find("model.stl"), std::string::npos);
}

TEST(HtmlRenderTest, IdentityCanonicalText_ConfinedToTraceBlocks_RPT06_ACC3)
{
    // UX-02：身份规范文本（哈希/Schema）仅出现在追溯区块/附录——正文
    // 表格区不得携带完整规范身份（缩略形定位）。
    const ReviewReport report = makeStandardReport();
    const FieldMatrix matrix = extractFieldMatrix(report);
    HtmlReportRenderer renderer(redactionService());
    const RenderOutcome outcome = renderer.render(report, matrix, ReportRenderFormat::Html);
    ASSERT_TRUE(outcome.artifact.has_value());
    const std::string html(outcome.artifact->bytes.begin(), outcome.artifact->bytes.end());

    const std::string fullIdentity = report.contentIdentity().toCanonical();
    // 追溯区块携带完整规范文本（允许域）。
    EXPECT_NE(html.find(fullIdentity), std::string::npos);
    // data-field 锚单元格（正文数据区）不携带规范身份——锚值恒为 fieldKey。
    const std::string anchor =
        "data-field=\"kinematics-collision.a-reach.kin.reach\">1500";
    EXPECT_NE(html.find(anchor), std::string::npos);
    EXPECT_EQ(html.find("data-field=\"" + fullIdentity), std::string::npos);
    // 正文数据源行用缩略形（前 8 位 hex＋…），非完整 64 hex。
    const std::string abbreviated = report.revision().toCanonical().substr(0, 12) + "…";
    EXPECT_NE(html.find(abbreviated), std::string::npos);
}

TEST(HtmlRenderTest, FormalPassWordingGate_RPT06_ACC3)
{
    // §8.2①②：资格成立才可渲染"正式通过结论"；资格不成立渲染"未达正式
    // 通过"——两声明独立、措辞不互换（渲染断言面；RP-STATE-1~3 具名用例
    // 归 RPT-T08，本用例钉住门的语义）。
    ReviewReportFields fields = makeStandardFields();
    // project-scheme 章节资格：formalPass 不成立（render "未达正式通过"）。
    fields.sections[0].eligibilityNote = EligibilityNote{false, false, "证据未验证"};
    // kin 章节资格：formalPass 成立＋评审记录成立（双声明独立呈现）。
    fields.sections[2].eligibilityNote = EligibilityNote{true, true, ""};
    const ReviewReport report = ReviewReport::make(std::move(fields), frozenReportLevelRule());
    const FieldMatrix matrix = extractFieldMatrix(report);
    HtmlReportRenderer renderer(redactionService());
    const RenderOutcome outcome = renderer.render(report, matrix, ReportRenderFormat::Html);
    ASSERT_TRUE(outcome.artifact.has_value());
    const std::string html(outcome.artifact->bytes.begin(), outcome.artifact->bytes.end());

    // 资格不成立章节：无"正式通过结论（资格成立）"声明字样（project-scheme
    // 块内——注意"筛选级……不得单独支撑正式通过结论"限定语文案含子串，
    // 断言须用完整声明短语区分）。
    const auto schemePos = html.find("data-eligibility=\"project-scheme\"");
    ASSERT_NE(schemePos, std::string::npos);
    const auto kinPos = html.find("data-eligibility=\"kinematics-collision\"");
    ASSERT_NE(kinPos, std::string::npos);
    const std::string schemeBlock = html.substr(schemePos, kinPos - schemePos);
    EXPECT_EQ(schemeBlock.find("正式通过结论（资格成立）"), std::string::npos);
    EXPECT_NE(schemeBlock.find("未达正式通过（资格不成立）"), std::string::npos);
    // 资格成立章节：字样在场＋评审记录独立声明在场。
    const std::string kinBlock = html.substr(kinPos);
    EXPECT_NE(kinBlock.find("正式通过结论（资格成立）"), std::string::npos);
    EXPECT_NE(kinBlock.find("经验证的不可行结论——正式评审记录（资格成立）"), std::string::npos);
}

TEST(HtmlRenderTest, MatrixReportMismatch_Rejected_NoPartialArtifact_RPT06_ACC1)
{
    // 矩阵与报告不同源（缺单元格/多单元格）→ DataInvalid 且无部分产物
    // 外泄（§9.3 后置行——失败＝error 在场＋artifact 缺席）。
    const ReviewReport report = makeStandardReport();
    FieldMatrix matrix = extractFieldMatrix(report);
    HtmlReportRenderer renderer(redactionService());

    // 缺单元格：报告字段查不到矩阵值。
    FieldMatrix missing = matrix;
    missing.erase(missing.begin());
    {
        const RenderOutcome outcome = renderer.render(report, missing, ReportRenderFormat::Html);
        EXPECT_FALSE(outcome.artifact.has_value());
        ASSERT_TRUE(outcome.error.has_value());
        EXPECT_EQ(outcome.error->code(), ReportErrorCode::DataInvalid);
    }
    // 多单元格：矩阵存在报告未引用的剩余值。
    FieldMatrix extra = matrix;
    FieldCell stray;
    stray.fieldKey = "kinematics-collision.stray.ghost";
    extra.push_back(stray);
    {
        const RenderOutcome outcome = renderer.render(report, extra, ReportRenderFormat::Html);
        EXPECT_FALSE(outcome.artifact.has_value());
        ASSERT_TRUE(outcome.error.has_value());
        EXPECT_EQ(outcome.error->code(), ReportErrorCode::DataInvalid);
    }
}

TEST(HtmlRenderTest, WrongFormatRequest_UsageFailFast_RPT06_ACC6)
{
    // 实现类只服务自己的格式词位（§9.3"新格式＝新实现"）——他格式请求
    // ＝调用方违约 Usage。
    const ReviewReport report = makeStandardReport();
    const FieldMatrix matrix = extractFieldMatrix(report);
    HtmlReportRenderer renderer(redactionService());
    const RenderOutcome outcome = renderer.render(report, matrix, ReportRenderFormat::Csv);
    EXPECT_FALSE(outcome.artifact.has_value());
    ASSERT_TRUE(outcome.error.has_value());
    EXPECT_EQ(outcome.error->code(), ReportErrorCode::Usage);
}

// =====================================================================
// 格式 token（§8.5 提取器/工件清单格式列——NFR-COR-02 同码同串）
// =====================================================================

TEST(RenderFormatTokenTest, TokensStable_RPT06_ACC1)
{
    EXPECT_EQ(token(ReportRenderFormat::Html), "html");
    EXPECT_EQ(token(ReportRenderFormat::Json), "json");
    EXPECT_EQ(token(ReportRenderFormat::Csv), "csv");
}

TEST(HtmlRenderTest, ReviewSignoffBlock_LevelC_RPT06_ACC5)
{
    // §8.1 C 级评审签署块（signoff/comments）＋B 级不渲染该块的级别纪律。
    ReviewReportFields fields = makeStandardFields();
    fields.level = ReportLevel::C;
    // C 级零 C 章节选中＝ScopeInsufficient（§4.6 行 4）——补一个选中的
    // C 专属章节（review-signoff，Populated 形态）。
    ReviewReportSection signoff;
    signoff.sectionId = kSectionReviewSignoff;
    signoff.sectionVersion = 1;
    signoff.selected = true;
    signoff.status = SectionStatus::Populated;
    SectionEntryView entry;
    entry.entryKey = "signoff-summary";
    entry.fields.push_back(numericField("sign.revision-count", 1.0, core::ProvenanceKind::UserProvided));
    entry.result.runId = fields.resultRefs[0].runId;
    entry.result.fieldPath = "payload.signoff";
    entry.caseScope = {fixtureObject(0x01)};
    signoff.entries.push_back(entry);
    signoff.sourceResults = {fields.resultRefs[0].runId};
    signoff.renderHint = RenderHint::MetadataBlock;
    signoff.order = 13;
    fields.sections.push_back(signoff);
    // 评审元数据（C 级条件字段＋签署＋意见）。
    fields.review.reviewer = "评审员甲";
    fields.review.reviewedAtUtc = fixedTime(1700000200);
    fields.review.signOff = SignOffState::signedState("签署人乙", fixedTime(1700000300), cid(0x80).bytes);
    ReviewComment comment;
    comment.author = "评审员甲";
    comment.atUtc = fixedTime(1700000250);
    comment.text = "结论与证据链一致";
    comment.sectionId = std::string(kSectionKinematicsCollision);
    fields.review.comments = {comment};
    const ReviewReport report = ReviewReport::make(std::move(fields), frozenReportLevelRule());
    const FieldMatrix matrix = extractFieldMatrix(report);
    HtmlReportRenderer renderer(redactionService());
    const RenderOutcome outcome = renderer.render(report, matrix, ReportRenderFormat::Html);
    ASSERT_TRUE(outcome.artifact.has_value());
    const std::string html(outcome.artifact->bytes.begin(), outcome.artifact->bytes.end());

    EXPECT_NE(html.find("id=\"report-review\""), std::string::npos);
    EXPECT_NE(html.find("评审员甲"), std::string::npos);
    EXPECT_NE(html.find("已签署（"), std::string::npos);
    EXPECT_NE(html.find("结论与证据链一致"), std::string::npos);

    // B 级报告不渲染评审签署块（级别×章节合法组合的呈现面）。
    const ReviewReport bReport = makeStandardReport();
    const RenderOutcome bOutcome = renderer.render(
        bReport, extractFieldMatrix(bReport), ReportRenderFormat::Html);
    ASSERT_TRUE(bOutcome.artifact.has_value());
    const std::string bHtml(bOutcome.artifact->bytes.begin(), bOutcome.artifact->bytes.end());
    EXPECT_EQ(bHtml.find("id=\"report-review\""), std::string::npos);
}

}  // namespace
