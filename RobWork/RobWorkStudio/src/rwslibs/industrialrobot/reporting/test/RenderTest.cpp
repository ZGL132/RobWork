/**
 * @file   RenderTest.cpp
 * @brief  RPT-T06 渲染链单元测试——FieldMatrix 提取（确定性/值投影/限定语/
 *         单位闸/曲线引用）＋HTML 渲染器（HTML5 静态契约/确定性/data-field
 *         锚/脱敏双保险/资格声明门/失败无外泄）；RPT-T08 措辞冻结规则层
 *         测试——RP-STATE-1~3 具名用例（两类声明前置断言五路反例＋正文
 *         扫描计数=0）＋§6.4 限定语词表逐 token 三格式结构性输出＋RP-COV-1
 *         覆盖呈现（完备性结论/包络合并标注/P-EV-5 降级限定语）。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/RPT-T06.json acceptance 1~5 与
 *     tasks/foundation/RPT-T08.json acceptance 1~6 的单元内面对（io 注入
 *     fake 的工厂记账/公共头纪律协作面随 contract_test/
 *     RenderCrossUnitContractTest.cpp——§3.4 测试目标分工：与 io 写出设施
 *     的协作属跨单元契约面）；
 *   - units/reporting.md §8.1~§8.4（渲染总则/格式契约/字段矩阵）、§4.3.3
 *     （脱敏双保险）、§6.2（两类声明呈现规则）、§6.4（限定语词表）、
 *     §6.5（覆盖显示）、§8.2①~④（措辞冻结渲染规则）。
 *
 * 替身边界声明（RP-STATE-4 同款纪律）：报告对象一律经 ReviewReport::make
 *   构造（合法实例唯一生产者）；HTML 渲染器零 io 依赖（§9.3 副作用行）；
 *   RPT-T08 单元内 JSON/CSV 渲染用例经本文件自持的 io 写出最小替身
 *   （FakeCsvWriter/FakeJsonWriter/MinimalIoFactory——canonical 行为的
 *   最小确定性形态，真实行为归 L5 的 io 适配器，SA-12；与契约测试同款
 *   边界声明，夹具自持不共享）。脱敏双保险用例直接消费
 *   diagnostics::RedactionService 实现（C-3 登记边的真实服务——不用替身，
 *   双保险语义才有真实凭证）。
 *
 * 确定性夹具（与 ReportModelTest 同款纪律）：身份/数据源字段逐位固定
 *   （禁随机 generate——确定性断言的前提）；ReportId 用确定性铺位值而非
 *   generate()（同脚本两次构建逐位一致——二次渲染字节比对的基础）。
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <sdurws/ird/diagnostics/Redaction.hpp>   // 真实脱敏服务（双保险用例凭证）
#include <sdurws/ird/evidence/Verdict.hpp>       // kDiagRegionCoverageDowngraded（P-EV-5
                                                 // 降级诊断码常量——与渲染器同源消费）
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

/// 合法结果引用（RPT-T08 夹具——Verified/Completed/EngineeringInfeasible/
/// reviewRecord 成立/formalPass 不成立：§6.2 行 2"经验证的不可行结论——
/// 正式评审记录"声明的数据源；经 evidence 检查的诚实组合形态）。
ResultRefSnapshot infeasibleReviewRecordResult(co::RevisionId revision, co::ObjectId caseId,
                                               std::uint8_t runSeed)
{
    ResultRefSnapshot r;
    r.runId = fixtureId<co::RunId>(runSeed);
    r.task.project = fixtureId<co::ProjectId>(0x11);
    r.task.branch = fixtureId<co::BranchId>(0x12);
    r.task.revision = revision;
    r.task.run = r.runId;
    r.task.attempt.value = runSeed;
    r.evaluationKey = "kin-batch-ik";
    r.evaluatorContractVersion = 1;
    r.mode = co::EvaluationMode::Verified;
    r.outcome = co::TaskOutcome::Completed;
    r.engineeringStatus = co::EngineeringStatus::EngineeringInfeasible;
    r.snapshotId = cid(static_cast<std::uint8_t>(runSeed + 0x01));
    r.sliceId = cid(static_cast<std::uint8_t>(runSeed + 0x02));
    r.inputBaselineId = cid(0x50);
    r.caseScope = {caseId};
    r.eligibility.formalPass = false;
    r.eligibility.reviewRecord = true;
    r.currentness = currentSnapshot(revision, 1700000003);
    r.producer.productVersion = "0.1.0";
    return r;
}

/// 合法结果引用（RPT-T08 夹具——Verified/Completed/DataInsufficient：§8.1
/// 表 2④"数据不足"呈现的数据源；两类声明资格均不成立）。
ResultRefSnapshot dataInsufficientResult(co::RevisionId revision, co::ObjectId caseId,
                                         std::uint8_t runSeed)
{
    ResultRefSnapshot r = infeasibleReviewRecordResult(revision, caseId, runSeed);
    r.engineeringStatus = co::EngineeringStatus::DataInsufficient;
    r.eligibility.reviewRecord = false;
    r.currentness = currentSnapshot(revision, 1700000004);
    return r;
}

/// 非完成结果引用（RPT-T08 夹具——Canceled/Failed/Interrupted：§6.2 行 5
/// "不构成结论"状态事实呈现＋§8.2①断言①的反例数据源；TASK-02 表 3 该行
/// engineeringStatus==NotApplicable、两类资格恒不成立）。
ResultRefSnapshot nonCompletedResult(co::RevisionId revision, co::ObjectId caseId,
                                     co::TaskOutcome outcome, std::uint8_t runSeed)
{
    ResultRefSnapshot r = infeasibleReviewRecordResult(revision, caseId, runSeed);
    r.outcome = outcome;
    r.engineeringStatus = co::EngineeringStatus::NotApplicable;
    r.eligibility.reviewRecord = false;
    r.currentness = currentSnapshot(revision, 1700000005);
    return r;
}

/// 单条目 Populated 章节（RPT-T08 夹具——一个数值字段＋可选资格说明；
/// entryKey/字段键/工况按参注入，绑定指定运行）。
ReviewReportSection singleEntrySection(std::string_view sectionId, std::uint16_t order,
                                       const core::RunId& runId, const std::string& entryKey,
                                       const core::ObjectId& caseId,
                                       const EligibilityNote& note = EligibilityNote{},
                                       bool withNote = false)
{
    ReviewReportSection section;
    section.sectionId = std::string(sectionId);
    section.sectionVersion = 1;
    section.selected = true;
    section.status = SectionStatus::Populated;
    SectionEntryView entry;
    entry.entryKey = entryKey;
    entry.fields.push_back(numericField(entryKey + ".value", 1.0, core::ProvenanceKind::UserProvided));
    entry.result.runId = runId;
    entry.result.fieldPath = "payload." + entryKey;
    entry.caseScope = {caseId};
    section.entries.push_back(entry);
    section.sourceResults = {runId};
    if (withNote) {
        section.eligibilityNote = note;
    }
    section.renderHint = RenderHint::Table;
    section.order = order;
    return section;
}

/// 脱敏服务前置声明（定义在标准夹具之后——本段辅助函数先行使用）。
diagnostics::RedactionService& redactionService();

/// 正文扫描辅助（RP-STATE-1 机械验收面）：剥离限定语 span（screening-only
/// 等冻结文案本身含"正式通过"子串——§8.1 表 1 词面，属限定语通道而非
/// 声明正文），返回剩余文本中 needle 的出现次数。
std::size_t countOutsideQualifierSpans(const std::string& html, const std::string& needle)
{
    std::string stripped = html;
    static const std::string kOpen = "<span class=\"qualifier\"";
    static const std::string kClose = "</span>";
    for (auto pos = stripped.find(kOpen); pos != std::string::npos;
         pos = stripped.find(kOpen, pos)) {
        const auto end = stripped.find(kClose, pos);
        if (end == std::string::npos) {
            break;   // 截断面（不可达——渲染器输出良构 HTML）
        }
        stripped.erase(pos, end + kClose.size() - pos);
    }
    std::size_t count = 0;
    for (auto pos = stripped.find(needle); pos != std::string::npos;
         pos = stripped.find(needle, pos + needle.size())) {
        ++count;
    }
    return count;
}

/// 渲染 HTML 便捷（成功前置——失败时用例自身断言 error 的另行展开）。
std::string renderHtmlOk(const ReviewReport& report)
{
    HtmlReportRenderer renderer(redactionService());
    const RenderOutcome outcome =
        renderer.render(report, extractFieldMatrix(report), ReportRenderFormat::Html);
    EXPECT_TRUE(outcome.artifact.has_value());
    return outcome.artifact.has_value()
               ? std::string(outcome.artifact->bytes.begin(), outcome.artifact->bytes.end())
               : std::string();
}

// =====================================================================
// io 注入替身（RPT-T08 单元内 JSON/CSV 渲染用——最小确定性形态）
// =====================================================================
// fake 边界声明（与 contract_test/RenderCrossUnitContractTest.cpp 同款纪律，
// 夹具自持不共享——只保留 RPT-T08 用例所需的最小面）：
//   FakeCsvWriter/FakeJsonWriter 为 io canonical 行为的最小确定性替身
//   （RFC4180 引号化＋CRLF＋方言行／2 空格缩进＋to_chars 数值＋LF）——
//   真实行为归 L5 装配的 io 适配器（SA-12），替身只驱动渲染器注入缝；
//   无调用记账（RPT-T08 用例不验证工厂节奏——该面在契约测试）。

class FakeCsvWriter final : public IReportCsvWriter {
public:
    bool open(IReportOutputTarget&& target, bool emitDialectMarker) override
    {
        m_target = &target;
        if (emitDialectMarker) {
            static const std::string kMarker =
                "#rwcsv1 delimiter=, quote=\" eol=CRLF encoding=utf-8\r\n";
            return m_target->write(reinterpret_cast<const std::uint8_t*>(kMarker.data()),
                                   kMarker.size());
        }
        return true;
    }
    bool writeHeader(const std::vector<std::string>& columns) override
    {
        return writeLine(columns);
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
        return writeLine(texts);
    }
    bool finish() override { return true; }

private:
    static std::string toChars(double value)
    {
        char buffer[64];
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
        return std::string(buffer, result.ptr);
    }
    bool writeLine(const std::vector<std::string>& texts)
    {
        std::string line;
        for (std::size_t i = 0; i < texts.size(); ++i) {
            if (i > 0) {
                line += ',';
            }
            if (texts[i].find_first_of(",\"\r\n") != std::string::npos) {
                line += '"';
                for (const char c : texts[i]) {
                    line += (c == '"') ? "\"\"" : std::string(1, c);
                }
                line += '"';
            } else {
                line += texts[i];
            }
        }
        line += "\r\n";
        return m_target->write(reinterpret_cast<const std::uint8_t*>(line.data()), line.size());
    }
    IReportOutputTarget* m_target = nullptr;   ///< 会话期借用（open→finish）
};

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
    std::string serialize(const ReportJsonDom& dom, int depth) const
    {
        const std::string pad(static_cast<std::size_t>(depth) * 2, ' ');
        const std::string innerPad(static_cast<std::size_t>(depth + 1) * 2, ' ');
        switch (dom.type()) {
        case ReportJsonDom::Type::Null:   return "null";
        case ReportJsonDom::Type::Bool:   return dom.booleanValue() ? "true" : "false";
        case ReportJsonDom::Type::Number: return number(dom.numberValue());
        case ReportJsonDom::Type::String: return "\"" + escape(dom.stringValue()) + "\"";
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

/// io 工厂替身（工厂零行为——产出独立 writer，§9.5 边界价值；无状态并发安全）。
class MinimalIoFactory final : public IReportIoFactory {
public:
    std::unique_ptr<IReportCsvWriter> makeCsvWriter() const override
    {
        return std::make_unique<FakeCsvWriter>();
    }
    std::unique_ptr<IReportJsonWriter> makeJsonWriter() const override
    {
        return std::make_unique<FakeJsonWriter>();
    }
    std::unique_ptr<IReportOutputTarget> makeAtomicTarget(const std::filesystem::path&,
                                                          ReplacePolicy) const override
    {
        return nullptr;   // 渲染器不消费原子目标（落盘归导出链 RPT-T09）
    }
};

/// 工厂单例（无状态——引用语义注入）。
MinimalIoFactory& testIoFactory()
{
    static MinimalIoFactory factory;
    return factory;
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
    // §8.2①②：资格成立才可渲染"正式通过结论"；不可行结论独立措辞；资格
    // 不成立渲染"不构成正式结论"（RPT-T08 措辞收口后的 T06 门语义钉住）。
    // 夹具资格与所引结果一致（RPT-T08 断言④同款口径——AND 聚合）：
    //   project-scheme：formalPass/评审记录均不成立（否定声明面）；
    //   kinematics-collision：引 r1（formalPass 成立）＋r2（Quick 不成立）
    //     ——AND 聚合后两声明均不成立；
    //   optimization-candidates：仅引 r1——formalPass 声明成立；
    //   requirements：仅引 r3（不可行＋评审记录成立）——评审记录声明成立。
    ReviewReportFields fields = makeStandardFields();
    const co::RevisionId revision = fixtureId<co::RevisionId>(0x13);
    fields.resultRefs.push_back(
        infeasibleReviewRecordResult(revision, fixtureObject(0x03), 0x34));
    fields.sections[0].eligibilityNote = EligibilityNote{false, false, "证据未验证"};
    fields.sections[2].eligibilityNote = EligibilityNote{false, false, ""};
    // order 严格递增（§4.7）：requirements(4) 先于 optimization(6) 入列。
    fields.sections.push_back(
        singleEntrySection(kSectionRequirements, 4, fields.resultRefs[2].runId,
                           "req-summary", fixtureObject(0x03),
                           EligibilityNote{false, true, ""}, true));
    fields.sections.push_back(
        singleEntrySection(kSectionOptimizationCandidates, 6, fields.resultRefs[0].runId,
                           "opt-summary", fixtureObject(0x01),
                           EligibilityNote{true, false, ""}, true));
    const ReviewReport report = ReviewReport::make(std::move(fields), frozenReportLevelRule());
    const FieldMatrix matrix = extractFieldMatrix(report);
    HtmlReportRenderer renderer(redactionService());
    const RenderOutcome outcome = renderer.render(report, matrix, ReportRenderFormat::Html);
    ASSERT_TRUE(outcome.artifact.has_value());
    const std::string html(outcome.artifact->bytes.begin(), outcome.artifact->bytes.end());

    // 资格不成立章节：无"正式通过结论（资格成立）"声明字样（project-scheme
    // 块内——注意"筛选级……不得单独支撑正式通过结论"限定语文案含子串，
    // 断言须用完整声明短语区分）；否定声明为"不构成正式结论（资格不成立）"
    //（RPT-T08 措辞——不含"正式通过"短语，RP-STATE-1 扫描口径的前提）。
    const auto schemePos = html.find("data-eligibility=\"project-scheme\"");
    ASSERT_NE(schemePos, std::string::npos);
    const auto kinPos = html.find("data-eligibility=\"kinematics-collision\"");
    ASSERT_NE(kinPos, std::string::npos);
    const auto reqPos = html.find("data-eligibility=\"requirements\"");
    ASSERT_NE(reqPos, std::string::npos);
    const auto optPos = html.find("data-eligibility=\"optimization-candidates\"");
    ASSERT_NE(optPos, std::string::npos);
    const std::string schemeBlock = html.substr(schemePos, kinPos - schemePos);
    EXPECT_EQ(schemeBlock.find("正式通过结论（资格成立）"), std::string::npos);
    EXPECT_NE(schemeBlock.find("不构成正式结论（资格不成立）"), std::string::npos);
    // AND 聚合章节（kin 引 r1＋r2）：两声明均不成立——否定声明面。
    const std::string kinBlock = html.substr(kinPos, reqPos - kinPos);
    EXPECT_EQ(kinBlock.find("正式通过结论（资格成立）"), std::string::npos);
    EXPECT_NE(kinBlock.find("不构成正式结论（资格不成立）"), std::string::npos);
    // formalPass 成立章节：字样在场。
    const std::string optBlock = html.substr(optPos);
    EXPECT_NE(optBlock.find("正式通过结论（资格成立）"), std::string::npos);
    // 评审记录成立章节：§8.2② 原文引号形"经验证的不可行结论（正式评审
    // 记录）"（RPT-T08 对齐收口），且措辞不含"通过"字样。
    const std::string reqBlock = html.substr(reqPos, optPos - reqPos);
    EXPECT_NE(reqBlock.find("经验证的不可行结论（正式评审记录）"), std::string::npos);
    EXPECT_EQ(reqBlock.find("通过"), std::string::npos);
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

// =====================================================================
// RPT-T08 措辞冻结规则层（acceptance 1~4——RP-STATE-1~3 具名用例＋限定语
// 词表逐 token 结构性输出）
// =====================================================================

TEST(WordingFreezeTest, NonCompletedOutcome_DeclarationFlags_Rejected_DataInvalid_RPT08_ACC1)
{
    // §8.2①断言①（RP-STATE-1）：取消/失败/中断的结果永不携带任何声明资格
    // ——违反即 DataInvalid 构造失败，不存在"渲染了再说"路径。三类 outcome
    // ×两类资格逐一反例（表 3：该行 engineeringStatus==NotApplicable）。
    const co::RevisionId revision = fixtureId<co::RevisionId>(0x13);
    const co::ObjectId case1 = fixtureObject(0x01);
    const core::TaskOutcome outcomes[] = {core::TaskOutcome::Canceled,
                                          core::TaskOutcome::Failed,
                                          core::TaskOutcome::Interrupted};
    for (const core::TaskOutcome outcome : outcomes) {
        for (const bool formalFlag : {true, false}) {
            ReviewReportFields fields;
            fields.reportId = fixtureId<ReportId>(0x70);
            fields.level = ReportLevel::B;
            fields.project = fixtureId<co::ProjectId>(0x11);
            fields.branch = fixtureId<co::BranchId>(0x12);
            fields.revision = revision;
            fields.revisionSeq = 7;
            ResultRefSnapshot r = nonCompletedResult(revision, case1, outcome, 0x54);
            r.eligibility.formalPass = formalFlag;
            r.eligibility.reviewRecord = !formalFlag;
            fields.resultRefs.push_back(r);
            fields.sections.push_back(
                singleEntrySection(kSectionKinematicsCollision, 3, r.runId, "kin-summary",
                                   case1, EligibilityNote{false, false, ""}, false));
            fields.review.basisRevision = revision;
            fields.sectionModelVersion = std::string(kSectionModelVersion);
            const ReviewReport report =
                ReviewReport::make(std::move(fields), frozenReportLevelRule());
            HtmlReportRenderer renderer(redactionService());
            const RenderOutcome outcomeRendered =
                renderer.render(report, extractFieldMatrix(report), ReportRenderFormat::Html);
            EXPECT_FALSE(outcomeRendered.artifact.has_value()) << "outcome 槽位 " << static_cast<int>(outcome);
            ASSERT_TRUE(outcomeRendered.error.has_value());
            EXPECT_EQ(outcomeRendered.error->code(), ReportErrorCode::DataInvalid);
            EXPECT_NE(std::string(outcomeRendered.error->what()).find("断言①"), std::string::npos);
        }
    }
}

TEST(WordingFreezeTest, NonCompletedOutcome_StatusPresentationOnly_BodyScanZero_RPT08_ACC1)
{
    // RP-STATE-1 主验证（卡行验证方式列）：失败/取消/中断结果入报告时
    // 仅现于诊断/状态呈现——①正文（剥离限定语 span）无"正式通过"字样
    // （扫描计数=0）；②附录状态列钉住"——不构成结论"措辞；③条目单元格
    // 限定语为 outcome 状态事实 token（§6.4 行 8——非结论）。
    const co::RevisionId revision = fixtureId<co::RevisionId>(0x13);
    const co::ObjectId case1 = fixtureObject(0x01);
    const std::pair<core::TaskOutcome, const char*> rows[] = {
        {core::TaskOutcome::Canceled, "已取消——不构成结论"},
        {core::TaskOutcome::Failed, "已失败——不构成结论"},
        {core::TaskOutcome::Interrupted, "已中断——不构成结论"},
    };
    std::uint8_t seed = 0x54;
    for (const auto& [outcome, expectedWording] : rows) {
        ReviewReportFields fields;
        fields.reportId = fixtureId<ReportId>(0x70);
        fields.level = ReportLevel::B;
        fields.project = fixtureId<co::ProjectId>(0x11);
        fields.branch = fixtureId<co::BranchId>(0x12);
        fields.revision = revision;
        fields.revisionSeq = 7;
        const ResultRefSnapshot r = nonCompletedResult(revision, case1, outcome, ++seed);
        fields.resultRefs.push_back(r);
        fields.currentnessSummary.perResult.push_back(ResultCurrentnessEntry{r.runId, r.currentness});
        fields.review.basisRevision = revision;
        // 章节条目绑定非完成结果（构建器会拒绝该绑定——此处手造模型合法
        // 形态，验证渲染器对"仅状态呈现"的处理：无声明＋状态事实限定语）。
        ReviewReportSection section =
            singleEntrySection(kSectionKinematicsCollision, 3, r.runId, "kin-summary", case1,
                               EligibilityNote{false, false, ""}, true);
        section.status = SectionStatus::NoFormalResult;
        section.entries.clear();
        section.missingItems.push_back(MissingItemView{"kin.reach-per-task-point", "结果未完成"});
        fields.sections.push_back(section);
        fields.sectionModelVersion = std::string(kSectionModelVersion);
        const ReviewReport report =
            ReviewReport::make(std::move(fields), frozenReportLevelRule());

        // 矩阵面：outcome 状态事实限定语（词表完备性承载——无条目则无单元格，
        // 本夹具走缺项呈现分支，token 面用第二份含条目报告核对）。
        ReviewReportFields entryFields;
        entryFields.reportId = fixtureId<ReportId>(0x71);
        entryFields.level = ReportLevel::B;
        entryFields.project = fixtureId<co::ProjectId>(0x11);
        entryFields.branch = fixtureId<co::BranchId>(0x12);
        entryFields.revision = revision;
        entryFields.revisionSeq = 7;
        entryFields.resultRefs.push_back(r);
        entryFields.sections.push_back(
            singleEntrySection(kSectionKinematicsCollision, 3, r.runId, "kin-summary", case1));
        entryFields.currentnessSummary.perResult.push_back(
            ResultCurrentnessEntry{r.runId, r.currentness});
        entryFields.review.basisRevision = revision;
        entryFields.sectionModelVersion = std::string(kSectionModelVersion);
        const ReviewReport entryReport =
            ReviewReport::make(std::move(entryFields), frozenReportLevelRule());
        const FieldMatrix matrix = extractFieldMatrix(entryReport);
        ASSERT_EQ(matrix.size(), 1u);
        EXPECT_TRUE(std::find(matrix[0].qualifier.begin(), matrix[0].qualifier.end(),
                              outcome == core::TaskOutcome::Canceled  ? QualifierToken::Canceled
                              : outcome == core::TaskOutcome::Failed ? QualifierToken::Failed
                                                                     : QualifierToken::Interrupted)
                    != matrix[0].qualifier.end());

        // HTML 面：扫描计数=0＋状态措辞＋否定声明。
        const std::string html = renderHtmlOk(report);
        EXPECT_EQ(countOutsideQualifierSpans(html, "正式通过"), 0u)
            << "outcome 槽位 " << static_cast<int>(outcome);
        EXPECT_NE(html.find(expectedWording), std::string::npos);
        EXPECT_NE(html.find("不构成正式结论（资格不成立）"), std::string::npos);
    }
}

TEST(WordingFreezeTest, DataInsufficient_MissingListFull_Qualifier_NoConclusion_RPT08_ACC2)
{
    // RP-STATE-2（§8.1 表 2④）：Completed∧DataInsufficient ⇒ "数据不足"
    // 章节/条目＋缺失项全量清单（不因首个缺失短路）＋限定语；无结论字样。
    const co::RevisionId revision = fixtureId<co::RevisionId>(0x13);
    const co::ObjectId case1 = fixtureObject(0x01);
    ReviewReportFields fields;
    fields.reportId = fixtureId<ReportId>(0x70);
    fields.level = ReportLevel::B;
    fields.project = fixtureId<co::ProjectId>(0x11);
    fields.branch = fixtureId<co::BranchId>(0x12);
    fields.revision = revision;
    fields.revisionSeq = 7;
    const ResultRefSnapshot r = dataInsufficientResult(revision, case1, 0x64);
    fields.resultRefs.push_back(r);
    fields.currentnessSummary.perResult.push_back(ResultCurrentnessEntry{r.runId, r.currentness});
    ReviewReportSection section =
        singleEntrySection(kSectionKinematicsCollision, 3, r.runId, "kin-summary", case1,
                           EligibilityNote{false, false, "缺失项未汇总齐备"}, true);
    // 缺失项全量清单（两条——不短路）＋数据不足章节状态。
    section.status = SectionStatus::DataInsufficient;
    section.missingItems.push_back(MissingItemView{"kin.reach-per-task-point", "评估未产出"});
    section.missingItems.push_back(MissingItemView{"kin.joint-limit-margin", "证据缺失"});
    fields.sections.push_back(section);
    fields.review.basisRevision = revision;
    fields.sectionModelVersion = std::string(kSectionModelVersion);
    const ReviewReport report = ReviewReport::make(std::move(fields), frozenReportLevelRule());
    const FieldMatrix matrix = extractFieldMatrix(report);
    ASSERT_EQ(matrix.size(), 1u);
    EXPECT_TRUE(std::find(matrix[0].qualifier.begin(), matrix[0].qualifier.end(),
                          QualifierToken::DataInsufficient)
                != matrix[0].qualifier.end());

    const std::string html = renderHtmlOk(report);
    // 缺失项全量（两条都在场——不因首个缺失短路）。
    EXPECT_NE(html.find("kin.reach-per-task-point"), std::string::npos);
    EXPECT_NE(html.find("kin.joint-limit-margin"), std::string::npos);
    EXPECT_NE(html.find("数据不足"), std::string::npos);
    EXPECT_NE(html.find("data-qualifier=\"data-insufficient\""), std::string::npos);
    // 无结论字样：声明为"不构成正式结论"，且正文无"正式通过"（扫描口径）。
    EXPECT_NE(html.find("不构成正式结论（资格不成立）"), std::string::npos);
    EXPECT_EQ(countOutsideQualifierSpans(html, "正式通过"), 0u);
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

TEST(WordingFreezeTest, InfeasibleReviewRecord_WordingAndProof_NeverPassWording_RPT08_ACC2)
{
    // RP-STATE-3（RPT-05/RV-07——§8.2②）：EngineeringInfeasible∧reviewRecord
    // ==true ⇒ 输出"经验证的不可行结论（正式评审记录）"措辞＋证明呈现；
    // 全文（剥离限定语 span）无"通过"字样；与 formalPass 声明并存不混淆
    // ——同报告内两声明各随其结果出现、措辞不互换。
    const co::RevisionId revision = fixtureId<co::RevisionId>(0x13);
    const co::ObjectId case1 = fixtureObject(0x01);
    const co::ObjectId case2 = fixtureObject(0x02);
    ReviewReportFields fields;
    fields.reportId = fixtureId<ReportId>(0x70);
    fields.level = ReportLevel::B;
    fields.project = fixtureId<co::ProjectId>(0x11);
    fields.branch = fixtureId<co::BranchId>(0x12);
    fields.revision = revision;
    fields.revisionSeq = 7;
    // ①不可行结果（评审记录资格成立）②可行结果（formalPass 成立）——
    // 两声明并存的最小形态（各随其结果、不同章节）。
    const ResultRefSnapshot infeasible =
        infeasibleReviewRecordResult(revision, case1, 0x34);
    const ResultRefSnapshot feasible = feasibleResult(revision, case2);
    fields.resultRefs.push_back(infeasible);
    fields.resultRefs.push_back(feasible);
    fields.currentnessSummary.perResult.push_back(
        ResultCurrentnessEntry{infeasible.runId, infeasible.currentness});
    fields.currentnessSummary.perResult.push_back(
        ResultCurrentnessEntry{feasible.runId, feasible.currentness});
    fields.sections.push_back(
        singleEntrySection(kSectionKinematicsCollision, 3, infeasible.runId, "kin-infeasible",
                           case1, EligibilityNote{false, true, "任务级不可行证明成立"}, true));
    fields.sections.push_back(
        singleEntrySection(kSectionOptimizationCandidates, 6, feasible.runId, "opt-summary",
                           case2, EligibilityNote{true, false, ""}, true));
    // 证明呈现（§6.2 行 2"附证明类别/作用对象/前提/覆盖范围——呈现投影"）：
    // 证明证据项 Satisfied＋被替代成功产物按"因不可行而不适用"呈现（任务
    // 约束§五.3/C2 口径的呈现侧）。
    EvidenceRefEntry proof;
    proof.itemId = "kin.infeasibility-proof";
    proof.itemClass = evidence::EvidenceItemClass::Required;
    proof.status = evidence::EvidenceItemStatus::Satisfied;
    proof.artifactDigest = cid(0x90).bytes;
    proof.caseScope = {case1};
    proof.sourceSection = kSectionKinematicsCollision;
    fields.evidenceRefs.push_back(proof);
    EvidenceRefEntry substituted;
    substituted.itemId = "kin.reach-per-task-point";
    substituted.itemClass = evidence::EvidenceItemClass::Required;
    substituted.status = evidence::EvidenceItemStatus::NotApplicable;
    substituted.notApplicableReason = "因不可行而不适用（成功产物被确定性证明替代）";
    substituted.sourceSection = kSectionKinematicsCollision;
    fields.evidenceRefs.push_back(substituted);
    fields.review.basisRevision = revision;
    fields.sectionModelVersion = std::string(kSectionModelVersion);
    const ReviewReport report = ReviewReport::make(std::move(fields), frozenReportLevelRule());
    const std::string html = renderHtmlOk(report);

    // 两声明并存：评审记录声明（§8.2② 原文引号形）＋formalPass 声明同报告
    // 在场、措辞不互换。
    EXPECT_NE(html.find("经验证的不可行结论（正式评审记录）"), std::string::npos);
    EXPECT_NE(html.find("正式通过结论（资格成立）"), std::string::npos);
    // 证明呈现：证明证据项与"因不可行而不适用"替代说明在场。
    EXPECT_NE(html.find("kin.infeasibility-proof"), std::string::npos);
    EXPECT_NE(html.find("因不可行而不适用"), std::string::npos);
    // 永不输出"通过"之外的混淆：不可行章节块内无"通过"字样（机械断言面）。
    const auto kinPos = html.find("data-eligibility=\"kinematics-collision\"");
    const auto optPos = html.find("data-eligibility=\"optimization-candidates\"");
    ASSERT_NE(kinPos, std::string::npos);
    ASSERT_NE(optPos, std::string::npos);
    const std::string kinBlock = html.substr(kinPos, optPos - kinPos);
    EXPECT_NE(kinBlock.find("经验证的不可行结论（正式评审记录）"), std::string::npos);
    EXPECT_EQ(kinBlock.find("通过"), std::string::npos);
}

TEST(WordingFreezeTest, SectionFlagBeyondCitedResults_Rejected_DataInvalid_RPT08_ACC2)
{
    // §8.2①断言④：章节资格声明 ⇔ 所引结果资格 AND 聚合——声明超出所引
    // 结果资格即 DataInvalid（两声明独立核对：formalPass 侧与 reviewRecord
    // 侧各一反例）。
    const co::RevisionId revision = fixtureId<co::RevisionId>(0x13);
    const co::ObjectId case1 = fixtureObject(0x01);
    // 反例一：可行结果（reviewRecord 不成立）＋章节声明评审记录。
    {
        ReviewReportFields fields;
        fields.reportId = fixtureId<ReportId>(0x70);
        fields.level = ReportLevel::B;
        fields.project = fixtureId<co::ProjectId>(0x11);
        fields.branch = fixtureId<co::BranchId>(0x12);
        fields.revision = revision;
        fields.revisionSeq = 7;
        const ResultRefSnapshot feasible = feasibleResult(revision, case1);
        fields.resultRefs.push_back(feasible);
        fields.currentnessSummary.perResult.push_back(
            ResultCurrentnessEntry{feasible.runId, feasible.currentness});
        fields.sections.push_back(
            singleEntrySection(kSectionKinematicsCollision, 3, feasible.runId, "kin-summary",
                               case1, EligibilityNote{false, true, ""}, true));
        fields.review.basisRevision = revision;
        fields.sectionModelVersion = std::string(kSectionModelVersion);
        const ReviewReport report =
            ReviewReport::make(std::move(fields), frozenReportLevelRule());
        HtmlReportRenderer renderer(redactionService());
        const RenderOutcome outcome =
            renderer.render(report, extractFieldMatrix(report), ReportRenderFormat::Html);
        EXPECT_FALSE(outcome.artifact.has_value());
        ASSERT_TRUE(outcome.error.has_value());
        EXPECT_EQ(outcome.error->code(), ReportErrorCode::DataInvalid);
        EXPECT_NE(std::string(outcome.error->what()).find("断言④"), std::string::npos);
    }
    // 反例二：不可行结果（formalPass 不成立）＋章节声明 formalPass。
    {
        ReviewReportFields fields;
        fields.reportId = fixtureId<ReportId>(0x70);
        fields.level = ReportLevel::B;
        fields.project = fixtureId<co::ProjectId>(0x11);
        fields.branch = fixtureId<co::BranchId>(0x12);
        fields.revision = revision;
        fields.revisionSeq = 7;
        const ResultRefSnapshot infeasible =
            infeasibleReviewRecordResult(revision, case1, 0x34);
        fields.resultRefs.push_back(infeasible);
        fields.currentnessSummary.perResult.push_back(
            ResultCurrentnessEntry{infeasible.runId, infeasible.currentness});
        fields.sections.push_back(
            singleEntrySection(kSectionKinematicsCollision, 3, infeasible.runId, "kin-summary",
                               case1, EligibilityNote{true, false, ""}, true));
        fields.review.basisRevision = revision;
        fields.sectionModelVersion = std::string(kSectionModelVersion);
        const ReviewReport report =
            ReviewReport::make(std::move(fields), frozenReportLevelRule());
        HtmlReportRenderer renderer(redactionService());
        const RenderOutcome outcome =
            renderer.render(report, extractFieldMatrix(report), ReportRenderFormat::Html);
        EXPECT_FALSE(outcome.artifact.has_value());
        ASSERT_TRUE(outcome.error.has_value());
        EXPECT_EQ(outcome.error->code(), ReportErrorCode::DataInvalid);
        EXPECT_NE(std::string(outcome.error->what()).find("断言④"), std::string::npos);
    }
}

TEST(WordingFreezeTest, QuickResult_FormalPassFlag_Rejected_ScreeningOnlyMandatory_RPT08_ACC4)
{
    // acceptance 4（表 1 模式效力呈现侧）：mode==Quick 结果可进入报告但
    // 永不满足 FormalPass——①资格快照与模式矛盾＝DataInvalid（断言②）；
    // ②合法 Quick 结果的"筛选级——不得单独支撑正式通过结论"限定语在
    // 三格式结构性强制呈现（§6.4 行 5——不弱化不省略）。
    const co::RevisionId revision = fixtureId<co::RevisionId>(0x13);
    const co::ObjectId case2 = fixtureObject(0x02);
    // 反例：Quick 结果伪造 formalPass 资格——渲染边界拒绝。
    {
        ReviewReportFields fields;
        fields.reportId = fixtureId<ReportId>(0x70);
        fields.level = ReportLevel::B;
        fields.project = fixtureId<co::ProjectId>(0x11);
        fields.branch = fixtureId<co::BranchId>(0x12);
        fields.revision = revision;
        fields.revisionSeq = 7;
        ResultRefSnapshot quick = quickSupersededResult(revision, case2);
        quick.eligibility.formalPass = true;   // 伪造——Quick 永不满足 FormalPass
        fields.resultRefs.push_back(quick);
        fields.currentnessSummary.perResult.push_back(
            ResultCurrentnessEntry{quick.runId, quick.currentness});
        fields.sections.push_back(
            singleEntrySection(kSectionKinematicsCollision, 3, quick.runId, "kin-summary",
                               case2));
        // 引用 Superseded 结果的章节必填当前性（§4.3.4/§6.3——模型校验面）。
        fields.sections[0].currentness = quick.currentness;
        fields.review.basisRevision = revision;
        fields.sectionModelVersion = std::string(kSectionModelVersion);
        const ReviewReport report =
            ReviewReport::make(std::move(fields), frozenReportLevelRule());
        HtmlReportRenderer renderer(redactionService());
        const RenderOutcome outcome =
            renderer.render(report, extractFieldMatrix(report), ReportRenderFormat::Html);
        EXPECT_FALSE(outcome.artifact.has_value());
        ASSERT_TRUE(outcome.error.has_value());
        EXPECT_EQ(outcome.error->code(), ReportErrorCode::DataInvalid);
        EXPECT_NE(std::string(outcome.error->what()).find("断言②"), std::string::npos);
    }
    // 正例：合法 Quick 结果——screening-only 限定语三格式在场（标准夹具
    // 的 b-quick 条目即 Quick 数据源）。
    const ReviewReport report = makeStandardReport();
    const FieldMatrix matrix = extractFieldMatrix(report);
    HtmlReportRenderer htmlRenderer(redactionService());
    const RenderOutcome html =
        htmlRenderer.render(report, matrix, ReportRenderFormat::Html);
    ASSERT_TRUE(html.artifact.has_value());
    const std::string htmlText(html.artifact->bytes.begin(), html.artifact->bytes.end());
    EXPECT_NE(htmlText.find("data-qualifier=\"screening-only\""), std::string::npos);
    EXPECT_NE(htmlText.find("筛选级——不得单独支撑正式通过结论"), std::string::npos);
}

TEST(CoveragePresentationTest, FullCoverage_CompletenessConclusion_RPT08_ACC5)
{
    // RP-COV-1（EVI-02 呈现侧）：全覆盖 ⇒ coverageSummary 呈现"全部启用
    // 必验工况已覆盖"（§6.5 原文短语）。
    ReviewReportFields fields = makeStandardFields();
    fields.coverageSummary.totalRequired = 3;
    fields.coverageSummary.executed = 3;
    const ReviewReport report = ReviewReport::make(std::move(fields), frozenReportLevelRule());
    const std::string html = renderHtmlOk(report);
    EXPECT_NE(html.find("覆盖完备性：全部启用必验工况已覆盖"), std::string::npos);
}

TEST(CoveragePresentationTest, MissedCases_OverallDataInsufficient_PresentationAndGate_RPT08_ACC5)
{
    // RP-COV-1（§8.1 表 2② 呈现口径）：漏验任一启用必验工况 ⇒ 整体数据
    // 不足缺项呈现（漏验清单规模＋"整体数据不足"），且——断言⑤——任何
    // 章节不得声明 formalPass（并存量＝DataInvalid 渲染拒绝）。
    const co::RevisionId revision = fixtureId<co::RevisionId>(0x13);
    // ①呈现面：漏验 1（未执行 1）——覆盖完备性结论走"漏验清单"分支。
    {
        ReviewReportFields fields = makeStandardFields();
        fields.coverageSummary.totalRequired = 3;
        fields.coverageSummary.executed = 2;
        fields.coverageSummary.notExecuted = 1;
        const ReviewReport report =
            ReviewReport::make(std::move(fields), frozenReportLevelRule());
        const std::string html = renderHtmlOk(report);
        EXPECT_NE(html.find("漏验 1 项启用必验工况"), std::string::npos);
        EXPECT_NE(html.find("整体数据不足（缺项呈现）"), std::string::npos);
        // 漏验报告正文同样无"正式通过"（无 formalPass 声明的报告——扫描口径）。
        EXPECT_EQ(countOutsideQualifierSpans(html, "正式通过"), 0u);
    }
    // ②断言⑤：漏验＋章节 formalPass 声明并存——渲染边界 DataInvalid。
    {
        ReviewReportFields fields = makeStandardFields();
        fields.coverageSummary.totalRequired = 3;
        fields.coverageSummary.executed = 2;
        fields.coverageSummary.notExecuted = 1;
        fields.sections[0].eligibilityNote = EligibilityNote{true, false, ""};
        fields.sections[0].sourceResults = {fields.resultRefs[0].runId};
        const ReviewReport report =
            ReviewReport::make(std::move(fields), frozenReportLevelRule());
        HtmlReportRenderer renderer(redactionService());
        const RenderOutcome outcome =
            renderer.render(report, extractFieldMatrix(report), ReportRenderFormat::Html);
        EXPECT_FALSE(outcome.artifact.has_value());
        ASSERT_TRUE(outcome.error.has_value());
        EXPECT_EQ(outcome.error->code(), ReportErrorCode::DataInvalid);
        EXPECT_NE(std::string(outcome.error->what()).find("断言⑤"), std::string::npos);
    }
}

TEST(CoveragePresentationTest, EnvelopeMergeAnnotation_NotReplacingPerCaseEntries_RPT08_ACC5)
{
    // RP-COV-1（§6.5/DYN-07 呈现侧）：包络合并条目单独呈现并标注"包络合并
    // （呈现方式）"、不替代逐工况条目——标注触发数据源＝条目 caseScope
    // 跨多工况（数据驱动）；单工况条目不标注。
    ReviewReportFields fields = makeStandardFields();
    // 结果覆盖双工况（r1 原单工况——扩为双工况以容纳包络条目）。
    fields.resultRefs[0].caseScope = {fixtureObject(0x01), fixtureObject(0x02)};
    ReviewReportSection& kin = fields.sections[2];
    // 逐工况条目（caseScope 单工况——不标注）。
    kin.entries[0].caseScope = {fixtureObject(0x01)};
    // 包络合并条目（caseScope 双工况——标注一次）。
    SectionEntryView envelope;
    envelope.entryKey = "c-envelope";
    envelope.fields.push_back(
        numericField("kin.envelope-peak", 9.0, core::ProvenanceKind::UserProvided));
    envelope.result.runId = fields.resultRefs[0].runId;
    envelope.result.fieldPath = "payload.envelope";
    envelope.caseScope = {fixtureObject(0x01), fixtureObject(0x02)};
    kin.entries.push_back(envelope);
    const ReviewReport report = ReviewReport::make(std::move(fields), frozenReportLevelRule());
    const std::string html = renderHtmlOk(report);

    // 标注恰一次（包络条目首字段行）——逐工况条目零标注（"不替代"的
    // 呈现面）。
    const std::string kMark = "包络合并（呈现方式）";
    std::size_t count = 0;
    for (auto pos = html.find(kMark); pos != std::string::npos;
         pos = html.find(kMark, pos + kMark.size())) {
        ++count;
    }
    EXPECT_EQ(count, 1u);
    EXPECT_NE(html.find("data-case-merge=\"envelope\""), std::string::npos);
}

TEST(CoveragePresentationTest, DowngradedReferenceValue_MandatoryPresentation_RPT08_ACC6)
{
    // P-EV-5 处置（acceptance 6）：覆盖率参考值的降级限定语为消费侧强制
    // 呈现——不得弱化为普通标注（data-qualifier 结构性标记＋§6.4 冻结
    // 文案全串）、不得省略（无降级事实时零输出——不全局伪标注）。
    // 数据源＝diagRefs 携带 EVI-REGION-COVERAGE-DOWNGRADED（evidence 降级
    // 裁定的报告内投影——kDiagRegionCoverageDowngraded 常量同源）。
    // ①携带降级诊断：HTML 覆盖块限定语在场＋JSON coverageSummary.qualifier
    //   数组镜像（结构性输出——§6.4）。
    {
        ReviewReportFields fields = makeStandardFields();
        DiagRefEntry downgraded;
        downgraded.code = std::string(evidence::kDiagRegionCoverageDowngraded);
        downgraded.severity = diagnostics::DiagnosticSeverity::Warning;
        downgraded.category = diagnostics::DiagnosticCategory::DataInsufficient;
        downgraded.occurrences = 1;
        downgraded.sourceSection = kSectionKinematicsCollision;
        fields.diagRefs.push_back(downgraded);
        const ReviewReport report =
            ReviewReport::make(std::move(fields), frozenReportLevelRule());
        const std::string html = renderHtmlOk(report);
        EXPECT_NE(html.find("data-qualifier=\"downgraded-reference-value\""), std::string::npos);
        // 冻结文案全串在场（不得弱化——§6.4 行 4 呈现义务列原文）。
        EXPECT_NE(html.find("降级中——不可作为正式覆盖率结论"), std::string::npos);

        diagnostics::RedactionService redaction;
        JsonReportRenderer jsonRenderer(testIoFactory(), redaction);
        const RenderOutcome json =
            jsonRenderer.render(report, extractFieldMatrix(report), ReportRenderFormat::Json);
        ASSERT_TRUE(json.artifact.has_value());
        const std::string jsonText(json.artifact->bytes.begin(), json.artifact->bytes.end());
        // coverageSummary.qualifier 数组镜像降级限定语（块内定位——不依赖
        // 缩进细节）。
        const auto coveragePos = jsonText.find("\"coverageSummary\"");
        ASSERT_NE(coveragePos, std::string::npos);
        const std::string coverageBlock = jsonText.substr(coveragePos, 512);
        EXPECT_NE(coverageBlock.find("downgraded-reference-value"), std::string::npos);
    }
    // ②无降级事实：零输出（不伪造）。
    {
        const ReviewReport report = makeStandardReport();
        const std::string html = renderHtmlOk(report);
        EXPECT_EQ(html.find("data-qualifier=\"downgraded-reference-value\""), std::string::npos);
        EXPECT_EQ(html.find("降级中——不可作为正式覆盖率结论"), std::string::npos);
    }
}

TEST(WordingFreezeTest, QualifierWordList_PerTokenStructuralOutput_AllFormats_RPT08_ACC3)
{
    // acceptance 3（§6.4 词表 8 项——逐 token 触发数据源与呈现义务锁定）：
    // 单元格级 8 token（estimated/data-insufficient/screening-only/
    // historical-superseded/not-applicable/interrupted/canceled/failed）
    // 在三格式全部结构性在场（HTML data-qualifier 标记/CSV qualifier 列/
    // JSON qualifier 数组）；报告级 2 token（external-validation-incomplete
    // ——外部边界章 Recorded 态、downgraded-reference-value——覆盖块降级
    // 诊断）在 HTML/JSON 结构性在场（CSV 面＝数据面：诊断附表码行，见
    // 单元卡 §14.4 登记）。
    const co::RevisionId revision = fixtureId<co::RevisionId>(0x13);
    const co::ObjectId case1 = fixtureObject(0x01);
    const co::ObjectId case2 = fixtureObject(0x02);
    const co::ObjectId case3 = fixtureObject(0x03);
    const co::ObjectId case4 = fixtureObject(0x04);

    ReviewReportFields fields;
    fields.reportId = fixtureId<ReportId>(0x70);
    fields.level = ReportLevel::B;
    fields.project = fixtureId<co::ProjectId>(0x11);
    fields.branch = fixtureId<co::BranchId>(0x12);
    fields.revision = revision;
    fields.revisionSeq = 7;

    // 触发数据源族：r1 估算/不适用/证据缺失；r2 Quick＋Superseded（筛选级
    // ＋输入已变化）；r3/r4/r5 取消/失败/中断（状态事实三 token）。
    const ResultRefSnapshot r1 = feasibleResult(revision, case1);
    const ResultRefSnapshot r2 = quickSupersededResult(revision, case2);
    const ResultRefSnapshot r3 =
        nonCompletedResult(revision, case3, co::TaskOutcome::Canceled, 0x53);
    const ResultRefSnapshot r4 =
        nonCompletedResult(revision, case4, co::TaskOutcome::Failed, 0x54);
    const ResultRefSnapshot r5 =
        nonCompletedResult(revision, case1, co::TaskOutcome::Interrupted, 0x55);
    fields.resultRefs = {r1, r2, r3, r4, r5};
    for (const ResultRefSnapshot* r : {&r1, &r2, &r3, &r4, &r5}) {
        fields.currentnessSummary.perResult.push_back(
            ResultCurrentnessEntry{r->runId, r->currentness});
    }
    ReviewReportSection kin;
    kin.sectionId = kSectionKinematicsCollision;
    kin.sectionVersion = 1;
    kin.selected = true;
    kin.status = SectionStatus::Populated;
    SectionEntryView estimated;
    estimated.entryKey = "a-estimated";
    estimated.fields.push_back(numericField("kin.estimate", 2.5,
                                            core::ProvenanceKind::GeometricEstimate));
    estimated.fields.push_back([] {
        FieldValue f;
        f.key = "kin.na";
        f.quantity = co::SourcedValue<double>::notApplicable();
        return f;
    }());
    EvidenceBinding missingBinding;
    missingBinding.itemId = "kin.mode-evidence";
    missingBinding.status = evidence::EvidenceItemStatus::Missing;
    missingBinding.caseScope = {case1};
    estimated.evidence = {missingBinding};
    estimated.result.runId = r1.runId;
    estimated.result.fieldPath = "payload.a";
    estimated.caseScope = {case1};
    kin.entries.push_back(estimated);
    SectionEntryView screening;
    screening.entryKey = "b-screening";
    screening.fields.push_back(
        numericField("kin.quick", 1.0, core::ProvenanceKind::UserProvided));
    screening.result.runId = r2.runId;
    screening.result.fieldPath = "payload.b";
    screening.caseScope = {case2};
    kin.entries.push_back(screening);
    for (const auto& [runRef, key] :
         {std::pair<const ResultRefSnapshot*, const char*>{&r3, "c-canceled"},
          std::pair<const ResultRefSnapshot*, const char*>{&r4, "d-failed"},
          std::pair<const ResultRefSnapshot*, const char*>{&r5, "e-interrupted"}}) {
        SectionEntryView entry;
        entry.entryKey = key;
        entry.fields.push_back(
            numericField(std::string(key) + ".value", 1.0, core::ProvenanceKind::UserProvided));
        entry.result.runId = runRef->runId;
        entry.result.fieldPath = std::string("payload.") + key;
        entry.caseScope = {runRef->caseScope[0]};
        kin.entries.push_back(entry);
    }
    kin.sourceResults = {r1.runId, r2.runId, r3.runId, r4.runId, r5.runId};
    kin.currentness = supersededSnapshot(revision, 1700000002);
    kin.renderHint = RenderHint::Table;
    kin.order = 3;
    fields.sections.push_back(kin);

    // 报告级触发源：外部资源 Recorded（未固化）＋覆盖降级诊断。
    fields.externalResourceSummary.push_back(
        ExternalResourceStateEntry{"vendor-catalog-2023", ExternalResourceState::Recorded,
                                   "供应商目录（未回执）"});
    DiagRefEntry downgraded;
    downgraded.code = std::string(evidence::kDiagRegionCoverageDowngraded);
    downgraded.severity = diagnostics::DiagnosticSeverity::Warning;
    downgraded.category = diagnostics::DiagnosticCategory::DataInsufficient;
    downgraded.occurrences = 1;
    downgraded.sourceSection = kSectionKinematicsCollision;
    fields.diagRefs.push_back(downgraded);

    fields.review.basisRevision = revision;
    fields.sectionModelVersion = std::string(kSectionModelVersion);
    const ReviewReport report = ReviewReport::make(std::move(fields), frozenReportLevelRule());
    const FieldMatrix matrix = extractFieldMatrix(report);

    // 单元格级 token 触发核对（矩阵面——触发数据源逐项锁定）。
    const auto hasQualifier = [&matrix](QualifierToken token) {
        for (const FieldCell& cell : matrix) {
            if (std::find(cell.qualifier.begin(), cell.qualifier.end(), token)
                != cell.qualifier.end()) {
                return true;
            }
        }
        return false;
    };
    EXPECT_TRUE(hasQualifier(QualifierToken::Estimated));
    EXPECT_TRUE(hasQualifier(QualifierToken::DataInsufficient));
    EXPECT_TRUE(hasQualifier(QualifierToken::ScreeningOnly));
    EXPECT_TRUE(hasQualifier(QualifierToken::HistoricalSuperseded));
    EXPECT_TRUE(hasQualifier(QualifierToken::NotApplicable));
    EXPECT_TRUE(hasQualifier(QualifierToken::Canceled));
    EXPECT_TRUE(hasQualifier(QualifierToken::Failed));
    EXPECT_TRUE(hasQualifier(QualifierToken::Interrupted));

    // 三格式渲染。
    diagnostics::RedactionService redaction;
    HtmlReportRenderer htmlRenderer(redaction);
    JsonReportRenderer jsonRenderer(testIoFactory(), redaction);
    CsvReportRenderer csvRenderer(testIoFactory(), redaction);
    const RenderOutcome htmlOut = htmlRenderer.render(report, matrix, ReportRenderFormat::Html);
    const RenderOutcome jsonOut = jsonRenderer.render(report, matrix, ReportRenderFormat::Json);
    const RenderOutcome csvOut = csvRenderer.render(report, matrix, ReportRenderFormat::Csv);
    ASSERT_TRUE(htmlOut.artifact.has_value());
    ASSERT_TRUE(jsonOut.artifact.has_value());
    ASSERT_TRUE(csvOut.artifact.has_value());
    const std::string htmlText(htmlOut.artifact->bytes.begin(), htmlOut.artifact->bytes.end());
    const std::string jsonText(jsonOut.artifact->bytes.begin(), jsonOut.artifact->bytes.end());
    const std::string csvText(csvOut.artifact->bytes.begin(), csvOut.artifact->bytes.end());

    // 单元格级 8 token：三格式结构性在场。
    const std::pair<QualifierToken, const char*> cellTokens[] = {
        {QualifierToken::Estimated, "estimated"},
        {QualifierToken::DataInsufficient, "data-insufficient"},
        {QualifierToken::ScreeningOnly, "screening-only"},
        {QualifierToken::HistoricalSuperseded, "historical-superseded"},
        {QualifierToken::NotApplicable, "not-applicable"},
        {QualifierToken::Canceled, "canceled"},
        {QualifierToken::Failed, "failed"},
        {QualifierToken::Interrupted, "interrupted"},
    };
    for (const auto& [tokenValue, tokenText] : cellTokens) {
        (void)tokenValue;
        const std::string htmlMark = "data-qualifier=\"" + std::string(tokenText) + "\"";
        EXPECT_NE(htmlText.find(htmlMark), std::string::npos) << "HTML 缺限定语：" << tokenText;
        EXPECT_NE(jsonText.find("\"" + std::string(tokenText) + "\""), std::string::npos)
            << "JSON 缺限定语：" << tokenText;
        EXPECT_NE(csvText.find(std::string(tokenText)), std::string::npos)
            << "CSV 缺限定语：" << tokenText;
    }
    // 报告级 token：HTML/JSON 结构性在场（CSV＝数据面——诊断附表携带降级
    // 码，见单元卡 §14.4 v0.11 登记）。
    EXPECT_NE(htmlText.find("data-qualifier=\"external-validation-incomplete\""),
              std::string::npos);
    EXPECT_NE(jsonText.find("\"external-validation-incomplete\""), std::string::npos);
    EXPECT_NE(htmlText.find("data-qualifier=\"downgraded-reference-value\""), std::string::npos);
    EXPECT_NE(jsonText.find("\"downgraded-reference-value\""), std::string::npos);
    EXPECT_NE(csvText.find(std::string(evidence::kDiagRegionCoverageDowngraded)),
              std::string::npos);
}

TEST(WordingFreezeTest, ExternalValidationIncomplete_RecordedState_MandatoryBoundary_RPT08_ACC3)
{
    // §6.4 行 3（RPT-T08 报告级承接）：外部资源 state==Recorded ⇒
    // external-validation-incomplete 限定语随外部验证边界章条目强制呈现；
    // Solidified（已固化）零输出；JSON 镜像 qualifier 数组同源。
    ReviewReportFields recorded = makeStandardFields();
    recorded.externalResourceSummary.push_back(
        ExternalResourceStateEntry{"vendor-catalog-2023", ExternalResourceState::Recorded,
                                   "供应商目录（未回执）"});
    recorded.externalResourceSummary.push_back(
        ExternalResourceStateEntry{"tool-cad-file", ExternalResourceState::Solidified, "已固化 CAD"});
    const ReviewReport recordedReport =
        ReviewReport::make(std::move(recorded), frozenReportLevelRule());
    const std::string html = renderHtmlOk(recordedReport);
    EXPECT_NE(html.find("data-qualifier=\"external-validation-incomplete\""), std::string::npos);
    EXPECT_NE(html.find("外部验证未完成"), std::string::npos);

    diagnostics::RedactionService redaction;
    JsonReportRenderer jsonRenderer(testIoFactory(), redaction);
    const RenderOutcome json =
        jsonRenderer.render(recordedReport, extractFieldMatrix(recordedReport),
                            ReportRenderFormat::Json);
    ASSERT_TRUE(json.artifact.has_value());
    const std::string jsonText(json.artifact->bytes.begin(), json.artifact->bytes.end());
    EXPECT_NE(jsonText.find("\"external-validation-incomplete\""), std::string::npos);

    // 全部固化：限定语零输出（不伪造）。
    const ReviewReport report = makeStandardReport();
    const std::string cleanHtml = renderHtmlOk(report);
    EXPECT_EQ(cleanHtml.find("data-qualifier=\"external-validation-incomplete\""),
              std::string::npos);
}

}  // namespace
