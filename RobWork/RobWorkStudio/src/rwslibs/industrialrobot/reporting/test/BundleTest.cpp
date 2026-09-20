/**
 * @file   BundleTest.cpp
 * @brief  RPT-T10 证据包组装单元测试——RP-BUN-1 的单元内承载面：组装清单
 *         完整（bundle.json 六字段/snapshot、results、reproduction 三类条目）、
 *         要素缺失 BundleIncomplete 全量列出（不产出半包）、取消/失败清理
 *         （临时清理、目标不变）、缺省 runs 语义与 Usage 矩阵。
 *
 * 设计依据：
 *   - units/reporting.md §7.6（证据包导出流程原文＋要素边界——"要素缺失→
 *     BundleIncomplete 拒绝并列出〔不产出半包〕"、"取消/失败→清理临时、
 *     目标不变"、"默认＝报告 resultRefs"）、§9.6（IArchiveWriter 契约——
 *     finish 原子替换/失败清理；放弃路径 RAII）、§10.1（RP-BUN-1 行：
 *     观测点＝bundle.json 内容/条目枚举/终态）
 *   - 任务契约 tasks/foundation/RPT-T10.json acceptance 1~3（acceptance 4/5
 *     的格式与通道边界面随 contract_test/BundleCrossUnitContractTest.cpp）
 *
 * 替身边界声明：报告对象一律经 ReviewReport::make 构造（合法实例唯一生产
 *   者）；替身包络一律经 evidence ResultEnvelope::make 构造（合法组合-only
 *   ——reporting 不自造非法样本）；归档写出替身 FakeArchiveWriter
 *   （test/FakeArchiveWriter.hpp——本任务交付的最小确定性替身，真实临时
 *   文件承载生命周期，边界声明在该文件头）；JSON 写出替身为最小 canonical
 *   形态（io 真实 canonical 行为归 io 验证矩阵——SA-12）。
 *
 * 确定性夹具：身份/数据源字段逐位固定（禁随机——同输入同摘要是 totalDigest
 *   确定性断言的前提）；取消/写出故障经替身显式注入面，非时序依赖。
 */

#include <gtest/gtest.h>

#include <charconv>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/evidence/Envelope.hpp>
#include <sdurws/ird/evidence/Snapshot.hpp>
#include <sdurws/ird/reporting/Bundle.hpp>
#include <sdurws/ird/reporting/Export.hpp>
#include <sdurws/ird/reporting/Render.hpp>
#include <sdurws/ird/reporting/Sections.hpp>

#include "FakeArchiveWriter.hpp"

namespace {

using namespace sdurws::ird::reporting;
namespace co = sdurws::ird::core;
namespace core = sdurws::ird::core;
namespace ev = sdurws::ird::evidence;
namespace evidence = sdurws::ird::evidence;
using test_fakes::FakeArchiveWriter;

// =====================================================================
// 确定性夹具（与 ArchiveExportTest 同款纪律——自持不共享；身份逐位铺位）
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

/// 单运行引用快照（报告 resultRefs 元素——runId/snapshotId/sliceId/
/// inputBaselineId 与包络绑定面一致，NFR-COR-04 追溯链的夹具形态）。
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

/// 合法 B 级报告（单章节五字段——ReviewReport::make 已验证形态，与
/// ArchiveExportTest 夹具同构）。
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

ReviewReport makeStandardReport()
{
    return ReviewReport::make(makeFields(), frozenReportLevelRule());
}

/// 报告引用运行的包络绑定面（与报告 resultRefs 同源——追溯链一致）。
struct BindingIds {
    core::ContentIdentity snapshotId = cid(0x10);
    core::ContentIdentity sliceId = cid(0x30);
    core::ContentIdentity inputBaselineId = cid(0x50);
};

/// 结构有效的证据清单（与包络同名绑定一致＋Satisfied/Missing 两态项——
/// presence 纪律合格；覆盖 status 词面两形态）。
evidence::EvidenceManifest legalManifest(const BindingIds& ids)
{
    evidence::EvidenceManifest m;
    m.snapshotId = ids.snapshotId;
    m.sliceId = ids.sliceId;
    m.profileId = "kin";
    m.profileVersion = "1.0.0";
    m.profileContentIdentity = cid(0x60);
    evidence::EvidenceItem ok;
    ok.itemId = "kin.reach-per-task-point";
    ok.status = evidence::EvidenceItemStatus::Satisfied;
    ok.artifactDigest = cid(0x61).bytes;
    evidence::EvidenceItem absent;
    absent.itemId = "kin.optional-metric";
    absent.status = evidence::EvidenceItemStatus::Missing;
    m.items = {ok, absent};
    return m;
}

/// 表 3 行 1 合法底座：Completed×Feasible（经 evidence make 构造——替身
/// 边界声明；绑定面取报告引用快照）。
evidence::ResultEnvelope legalFeasibleEnvelope(const core::RunId& run, const BindingIds& ids)
{
    evidence::ResultEnvelopeDraft d;
    d.task.project = fixtureId<co::ProjectId>(0x11);
    d.task.branch = fixtureId<co::BranchId>(0x12);
    d.task.revision = fixtureId<co::RevisionId>(0x13);
    d.task.run = run;
    d.task.attempt.value = 1;
    d.evaluationKey = "kin-batch-ik";
    d.evaluatorContractVersion = 1;
    d.mode = core::EvaluationMode::Verified;
    d.snapshotId = ids.snapshotId;
    d.sliceId = ids.sliceId;
    d.inputBaselineId = ids.inputBaselineId;
    d.caseScope.caseIds = {fixtureObject(0x01)};
    d.profile.profileId = "kin";
    d.profile.version = "1.0.0";
    d.profile.contentIdentity = cid(0x60);
    d.outcome = core::TaskOutcome::Completed;
    d.engineeringStatus = core::EngineeringStatus::Feasible;
    d.evidence = legalManifest(ids);
    d.payload = evidence::DomainPayloadDraft{"kin.batch-ik.v1", {0x01, 0x02, 0x03}};
    d.producer.productVersion = "0.1.0";
    return evidence::ResultEnvelope::make(std::move(d));
}

/// 合法复现块（§4.1.2 最小形态——两必填齐备）。
evidence::ReproductionBlock legalReproduction()
{
    evidence::ReproductionBlock r;
    r.productVersion = "0.1.0";
    r.evidenceContractVersion = "ev-contract-1";
    r.codecVersions = {"snapshot-codec/1", "slice-codec/1"};
    r.compilerContractVersion = "compiler-contract/2";
    return r;
}

// =====================================================================
// 注入替身（本文件自持——装配器四注入面的最小脚本化）
// =====================================================================

/// 报告值解析器（IReportResolver 最小投影——存储一份报告值）。
class FakeResolver final : public IReportResolver {
public:
    std::optional<ReviewReport> report;   ///< 可解析的报告值（nullopt＝不可解析）

    std::optional<ReviewReport> tryResolve(ReportId reportId) const override
    {
        if (report.has_value() && report->reportId() == reportId) {
            return report;
        }
        return std::nullopt;
    }
};

/// 归档结果注入源（§7.6 输入①——tryEnvelope/tryReproduction 脚本化；
/// currentnessOf/eligibilityOf 非本任务消费面，越权调用判红）。
class FakeResultSource final : public IReportResultSource {
public:
    std::map<std::string, evidence::ResultEnvelope> envelopes;          ///< 键＝runId 规范文本
    std::map<std::string, evidence::ReproductionBlock> reproductions;   ///< 同上

    std::optional<evidence::ResultEnvelope> tryEnvelope(core::RunId runId) const override
    {
        const auto it = envelopes.find(runId.toCanonical());
        return it == envelopes.end() ? std::nullopt : std::optional{it->second};
    }

    evidence::CurrentnessResult currentnessOf(const evidence::ResultEnvelope&,
                                              core::RevisionId) const override
    {
        ADD_FAILURE() << "currentnessOf 不可达（证据包组装不消费当前性投影）";
        return {};
    }

    ReportEligibilityChecks eligibilityOf(const evidence::ResultEnvelope&) const override
    {
        ADD_FAILURE() << "eligibilityOf 不可达（证据包组装不消费资格纯检查）";
        return {};
    }

    std::optional<evidence::ReproductionBlock> tryReproduction(core::RunId runId) const override
    {
        const auto it = reproductions.find(runId.toCanonical());
        return it == reproductions.end() ? std::nullopt : std::optional{it->second};
    }
};

/// 归档事实注入源（§7.6 输入②——三项事实逐运行脚本化；从表中移除条目＝
/// 事实缺位（nullopt）——BundleIncomplete 缺失清单的注入路径）。
class FakeBundleSource final : public IEvidenceBundleSource {
public:
    std::map<std::string, core::Digest256> envelopeDigests;       ///< 键＝runId 规范文本
    std::map<std::string, core::ContentIdentity> caseSetDigests;  ///< 同上
    std::map<std::string, std::vector<BundleConfigRef>> configs;  ///< 同上

    std::optional<core::Digest256> tryEnvelopeDigest(core::RunId runId) const override
    {
        const auto it = envelopeDigests.find(runId.toCanonical());
        return it == envelopeDigests.end() ? std::nullopt : std::optional{it->second};
    }

    std::optional<core::ContentIdentity> tryCaseSetDigest(core::RunId runId) const override
    {
        const auto it = caseSetDigests.find(runId.toCanonical());
        return it == caseSetDigests.end() ? std::nullopt : std::optional{it->second};
    }

    std::optional<std::vector<BundleConfigRef>>
    tryConfigurationRefs(core::RunId runId) const override
    {
        const auto it = configs.find(runId.toCanonical());
        return it == configs.end() ? std::nullopt : std::optional{it->second};
    }
};

// =====================================================================
// io 替身：最小 canonical JSON writer（与 ArchiveExportTest 同款边界声明：
// io 真实 canonical 行为归 io 验证矩阵；本替身只须产出可解析合法 JSON）
// =====================================================================

std::string jsonEscape(const std::string& s)
{
    std::string out;
    for (const char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
                out += buf;
            } else {
                out += c;
            }
        }
    }
    return out;
}

std::string encodeJson(const ReportJsonDom& v, int depth)
{
    const std::string pad(depth * 2, ' ');
    const std::string padInner((depth + 1) * 2, ' ');
    switch (v.type()) {
    case ReportJsonDom::Type::Null: return "null";
    case ReportJsonDom::Type::Bool: return v.booleanValue() ? "true" : "false";
    case ReportJsonDom::Type::Number: {
        char buf[40];
        const auto res = std::to_chars(buf, buf + sizeof(buf), v.numberValue());
        return std::string(buf, res.ptr);
    }
    case ReportJsonDom::Type::String: return "\"" + jsonEscape(v.stringValue()) + "\"";
    case ReportJsonDom::Type::Array: {
        if (v.arrayItems().empty()) {
            return "[]";
        }
        std::string out = "[\n";
        for (std::size_t i = 0; i < v.arrayItems().size(); ++i) {
            if (i > 0) {
                out += ",\n";
            }
            out += padInner + encodeJson(v.arrayItems()[i], depth + 1);
        }
        out += "\n" + pad + "]";
        return out;
    }
    case ReportJsonDom::Type::Object: {
        if (v.objectMembers().empty()) {
            return "{}";
        }
        std::string out = "{\n";
        for (std::size_t i = 0; i < v.objectMembers().size(); ++i) {
            if (i > 0) {
                out += ",\n";
            }
            out += padInner + "\"" + jsonEscape(v.objectMembers()[i].first) + "\": "
                   + encodeJson(v.objectMembers()[i].second, depth + 1);
        }
        out += "\n" + pad + "}";
        return out;
    }
    }
    return "null";   // 不可达（全枚举）
}

class FakeJsonWriter final : public IReportJsonWriter {
public:
    bool write(IReportOutputTarget&& target, const ReportJsonDom& dom) override
    {
        const std::string text = encodeJson(dom, 0);
        IReportOutputTarget& t = target;
        return t.write(reinterpret_cast<const std::uint8_t*>(text.data()), text.size())
               && t.commit();
    }
};

/// CSV/原子目标占位（本测试面不请求 CSV 与落盘原子目标——不可达判红）。
class UnreachableCsvWriter final : public IReportCsvWriter {
public:
    bool open(IReportOutputTarget&&, bool) override
    {
        ADD_FAILURE() << "makeCsvWriter 不可达（证据包无 CSV 条目）";
        return false;
    }
    bool writeHeader(const std::vector<std::string>&) override { return false; }
    bool writeRow(const std::vector<ReportCsvCell>&) override { return false; }
    bool finish() override { return false; }
};

class UnreachableIoFactory final : public IReportIoFactory {
public:
    std::unique_ptr<IReportCsvWriter> makeCsvWriter() const override
    {
        return std::make_unique<UnreachableCsvWriter>();
    }
    std::unique_ptr<IReportJsonWriter> makeJsonWriter() const override
    {
        return std::make_unique<FakeJsonWriter>();
    }
    std::unique_ptr<IReportOutputTarget> makeAtomicTarget(const std::filesystem::path&,
                                                          ReplacePolicy) const override
    {
        ADD_FAILURE() << "makeAtomicTarget 不可达（证据包落盘经 IArchiveWriter）";
        return nullptr;
    }
};

// =====================================================================
// 装配环境（一套齐全注入——各用例按需旋钮注入故障/缺位）
// =====================================================================

struct BundleEnv {
    ReviewReport report = makeStandardReport();
    FakeResolver resolver;
    FakeResultSource resultSource;
    FakeBundleSource bundleSource;
    FakeArchiveWriter writer;
    UnreachableIoFactory ioFactory;
    EvidenceBundleAssembler assembler{resolver, resultSource, bundleSource, writer, ioFactory};

    BundleEnv()
    {
        resolver.report = report;
        const core::RunId run = report.resultRefs()[0].runId;
        const BindingIds ids;
        resultSource.envelopes[run.toCanonical()] = legalFeasibleEnvelope(run, ids);
        resultSource.reproductions[run.toCanonical()] = legalReproduction();
        seedFacts(run);
    }

    /// 齐备事实（缺位注入＝从表中移除对应键——各缺失用例自行移除）。
    void seedFacts(const core::RunId& run)
    {
        bundleSource.envelopeDigests[run.toCanonical()] = cid(0x80).bytes;
        bundleSource.caseSetDigests[run.toCanonical()] = cid(0x90);
        bundleSource.configs[run.toCanonical()] = {
            BundleConfigRef{"solver.seed", cid(0xA0)},
            BundleConfigRef{"solver.threads", cid(0xA1)},
        };
    }
};

/// 临时目标路径（每用例唯一——测试间零文件干扰）。
std::filesystem::path targetPath(const std::string& tag)
{
    return std::filesystem::temp_directory_path()
           / ("ird-bundle-test-" + tag + ".zip");
}

/// 在 JSON 文本中提取 "key": "value" 的字符串值（替身 canonical 输出的
/// 简单读取器——只服务断言，不构成 JSON 解析器实现〔SA-12 边界在产品侧，
/// 测试侧最小文本提取〕）。
std::string jsonStringField(const std::string& text, const std::string& key)
{
    const std::string needle = "\"" + key + "\": \"";
    const std::size_t at = text.find(needle);
    if (at == std::string::npos) {
        return {};
    }
    const std::size_t begin = at + needle.size();
    const std::size_t end = text.find('"', begin);
    return end == std::string::npos ? std::string{} : text.substr(begin, end - begin);
}

// =====================================================================
// acceptance 1（RP-BUN-1 组装清单完整）
// =====================================================================

/// RP-BUN-1：合法报告＋齐全事实→Completed；四类条目齐备；bundle.json 六
/// 字段逐项核对（schemaVersion/reportId/contentIdentity/sourceRuns/
/// generatedAtUtc/totalDigest）；snapshot/results/reproduction 三类条目
/// 内容逐列核对；目标文件存在。
TEST(BundleTest, AssemblesCompleteBundleWithAllEntries)
{
    BundleEnv env;
    const core::RunId run = env.report.resultRefs()[0].runId;
    const std::filesystem::path target = targetPath("complete");

    EvidenceBundleRequest request;
    request.reportId = env.report.reportId();
    request.runs = {run};
    request.target = target;

    const EvidenceBundleOutcome out = env.assembler.assemble(request);
    ASSERT_EQ(out.status, BundleStatus::Completed) << (out.error ? out.error->what() : "");
    ASSERT_TRUE(out.totalDigest.has_value());

    // 条目枚举（§10.1 RP-BUN-1 观测点"条目枚举"）：恰四条目。写出序＝
    // 字典序（其余条目按条目名字节序——NFR-COR-02）＋bundle.json 殿后
    // （其 totalDigest 依赖其余全部条目——装配顺序的结构约束）。
    ASSERT_EQ(env.writer.entryNames.size(), std::size_t{4});
    EXPECT_EQ(env.writer.entryNames[0], std::string(kBundleReproductionName));
    EXPECT_EQ(env.writer.entryNames[1], bundleResultEntryName(run));
    EXPECT_EQ(env.writer.entryNames[2], bundleSnapshotEntryName(run));
    EXPECT_EQ(env.writer.entryNames[3], std::string(kBundleManifestName));

    // 目标文件存在（finish 原子替换完成）。
    EXPECT_TRUE(std::filesystem::exists(target));

    // ---- bundle.json 六字段（acceptance 1 的逐字段核对面）----
    const auto bundleIt = env.writer.entryBytes.find(std::string(kBundleManifestName));
    ASSERT_NE(bundleIt, env.writer.entryBytes.end());
    const std::string bundleJson(bundleIt->second.begin(), bundleIt->second.end());
    EXPECT_EQ(jsonStringField(bundleJson, "schemaVersion"), std::string(kBundleSchemaVersion));
    EXPECT_EQ(jsonStringField(bundleJson, "reportId"), env.report.reportId().toCanonical());
    EXPECT_EQ(jsonStringField(bundleJson, "contentIdentity"),
              env.report.contentIdentity().toCanonical());
    EXPECT_NE(bundleJson.find("sourceRuns"), std::string::npos);
    EXPECT_NE(bundleJson.find(run.toCanonical()), std::string::npos);
    EXPECT_FALSE(jsonStringField(bundleJson, "generatedAtUtc").empty());
    // totalDigest 列与 outcome 返回值同值（同一摘要的两个交付面）。
    const std::string totalHex = jsonStringField(bundleJson, "totalDigest");
    EXPECT_EQ(totalHex.size(), std::size_t{64});
    core::Digest256 reported = {};
    for (std::size_t i = 0; i < 32; ++i) {
        const auto byte = std::stoul(totalHex.substr(i * 2, 2), nullptr, 16);
        reported[i] = static_cast<std::uint8_t>(byte);
    }
    EXPECT_EQ(reported, *out.totalDigest);

    // ---- snapshot/<run>/snapshot.json：refs-only 四列 ----
    const auto snapIt = env.writer.entryBytes.find(bundleSnapshotEntryName(run));
    ASSERT_NE(snapIt, env.writer.entryBytes.end());
    const std::string snapJson(snapIt->second.begin(), snapIt->second.end());
    const BindingIds ids;
    EXPECT_EQ(jsonStringField(snapJson, "snapshotId"), ids.snapshotId.toCanonical());
    EXPECT_EQ(jsonStringField(snapJson, "sliceId"), ids.sliceId.toCanonical());
    EXPECT_EQ(jsonStringField(snapJson, "inputBaselineId"), ids.inputBaselineId.toCanonical());
    EXPECT_EQ(jsonStringField(snapJson, "caseSetDigest"),
              env.bundleSource.caseSetDigests[run.toCanonical()].toCanonical());

    // ---- results/<run>/result.json：envelope 摘要＋证据清单＋复现要素 ----
    const auto resultIt = env.writer.entryBytes.find(bundleResultEntryName(run));
    ASSERT_NE(resultIt, env.writer.entryBytes.end());
    const std::string resultJson(resultIt->second.begin(), resultIt->second.end());
    EXPECT_EQ(jsonStringField(resultJson, "runId"), run.toCanonical());
    const core::Digest256 expectedDigest = env.bundleSource.envelopeDigests[run.toCanonical()];
    EXPECT_EQ(jsonStringField(resultJson, "envelopeDigest").size(), std::size_t{64});
    // 证据清单逐列（itemId/status/digest——Satisfied 携摘要、Missing 无摘要
    // 〔presence 纪律的序列化对偶〕）。
    EXPECT_NE(resultJson.find("\"itemId\": \"kin.reach-per-task-point\""), std::string::npos);
    EXPECT_NE(resultJson.find("\"status\": \"satisfied\""), std::string::npos);
    EXPECT_NE(resultJson.find("\"digest\""), std::string::npos);
    EXPECT_NE(resultJson.find("\"itemId\": \"kin.optional-metric\""), std::string::npos);
    EXPECT_NE(resultJson.find("\"status\": \"missing\""), std::string::npos);
    // 复现要素（evidence 值逐字段——版本列不在结果条目中自算）。
    EXPECT_NE(resultJson.find("\"productVersion\": \"0.1.0\""), std::string::npos);
    EXPECT_NE(resultJson.find("\"evidenceContractVersion\": \"ev-contract-1\""),
              std::string::npos);
    EXPECT_NE(resultJson.find("\"compilerContractVersion\": \"compiler-contract/2\""),
              std::string::npos);

    // ---- reproduction.json：逐运行 versions＋configurations ----
    const auto reproIt = env.writer.entryBytes.find(std::string(kBundleReproductionName));
    ASSERT_NE(reproIt, env.writer.entryBytes.end());
    const std::string reproJson(reproIt->second.begin(), reproIt->second.end());
    EXPECT_NE(reproJson.find("\"runId\": \"" + run.toCanonical() + "\""), std::string::npos);
    EXPECT_NE(reproJson.find("\"configKindToken\": \"solver.seed\""), std::string::npos);
    EXPECT_NE(reproJson.find("\"configKindToken\": \"solver.threads\""), std::string::npos);
    EXPECT_NE(reproJson.find("\"contentIdentity\": \"" + cid(0xA0).toCanonical() + "\""),
              std::string::npos);

    std::error_code ec;
    std::filesystem::remove(target, ec);
}

/// 确定性（NFR-COR-02/D-04）：同输入两次装配 totalDigest 逐字节相等——
/// generatedAtUtc 时刻变化不进摘要（D-04：生成时刻不入内容身份）。
TEST(BundleTest, TotalDigestIsDeterministicAcrossAssembles)
{
    BundleEnv env;
    const core::RunId run = env.report.resultRefs()[0].runId;

    EvidenceBundleRequest first;
    first.reportId = env.report.reportId();
    first.runs = {run};
    first.target = targetPath("det-1");
    const EvidenceBundleOutcome out1 = env.assembler.assemble(first);

    EvidenceBundleRequest second = first;
    second.target = targetPath("det-2");
    const EvidenceBundleOutcome out2 = env.assembler.assemble(second);

    ASSERT_EQ(out1.status, BundleStatus::Completed);
    ASSERT_EQ(out2.status, BundleStatus::Completed);
    ASSERT_TRUE(out1.totalDigest.has_value());
    ASSERT_TRUE(out2.totalDigest.has_value());
    EXPECT_EQ(*out1.totalDigest, *out2.totalDigest);

    std::error_code ec;
    std::filesystem::remove(first.target, ec);
    std::filesystem::remove(second.target, ec);
}

// =====================================================================
// acceptance 2（缺省 runs＝报告 resultRefs；要素缺失→BundleIncomplete 全量
// 列出、不产出半包）
// =====================================================================

/// 缺省语义：runs 空向量＝报告 resultRefs 全集——与显式全集同摘要
/// （同包内容，仅请求形态不同）。
TEST(BundleTest, EmptyRunsDefaultsToReportResultRefs)
{
    BundleEnv env;
    const core::RunId run = env.report.resultRefs()[0].runId;

    EvidenceBundleRequest defaulted;
    defaulted.reportId = env.report.reportId();
    defaulted.target = targetPath("default-runs");
    const EvidenceBundleOutcome outDefault = env.assembler.assemble(defaulted);

    EvidenceBundleRequest explicitRuns;
    explicitRuns.reportId = env.report.reportId();
    explicitRuns.runs = {run};
    explicitRuns.target = targetPath("explicit-runs");
    const EvidenceBundleOutcome outExplicit = env.assembler.assemble(explicitRuns);

    ASSERT_EQ(outDefault.status, BundleStatus::Completed);
    ASSERT_EQ(outExplicit.status, BundleStatus::Completed);
    ASSERT_TRUE(outDefault.totalDigest.has_value());
    ASSERT_TRUE(outExplicit.totalDigest.has_value());
    EXPECT_EQ(*outDefault.totalDigest, *outExplicit.totalDigest);

    std::error_code ec;
    std::filesystem::remove(defaulted.target, ec);
    std::filesystem::remove(explicitRuns.target, ec);
}

/// 要素缺失（envelope 缺位）→BundleIncomplete；writer 从未 open（不产出
/// 半包——零临时、零写出、目标不存在）；detail 逐项定位缺失要素。
TEST(BundleTest, MissingEnvelopeRejectsWithBundleIncompleteBeforeAnyWrite)
{
    BundleEnv env;
    const core::RunId run = env.report.resultRefs()[0].runId;
    env.resultSource.envelopes.erase(run.toCanonical());   // 注入 envelope 缺位

    EvidenceBundleRequest request;
    request.reportId = env.report.reportId();
    request.runs = {run};
    request.target = targetPath("missing-envelope");

    const EvidenceBundleOutcome out = env.assembler.assemble(request);
    ASSERT_EQ(out.status, BundleStatus::Failed);
    ASSERT_TRUE(out.error.has_value());
    EXPECT_EQ(out.error->code(), ReportErrorCode::BundleIncomplete);
    EXPECT_NE(out.error->what(), nullptr);
    EXPECT_NE(std::string(out.error->what()).find("envelope@" + run.toCanonical()),
              std::string::npos);
    // 不产出半包：零会话、零临时、零目标。
    EXPECT_FALSE(env.writer.opened);
    EXPECT_FALSE(std::filesystem::exists(request.target));
}

/// 全量列出：五项事实全缺时 detail 逐项列出全部缺失（不短路——一次修复
/// 全部缺失的观测面），且不产出半包。
TEST(BundleTest, MissingElementsAreListedExhaustively)
{
    BundleEnv env;
    const core::RunId run = env.report.resultRefs()[0].runId;
    // 注入五项事实全缺（envelope/reproduction/envelope-digest/case-set/
    // configurations 逐项移除）。
    env.resultSource.envelopes.erase(run.toCanonical());
    env.resultSource.reproductions.erase(run.toCanonical());
    env.bundleSource.envelopeDigests.erase(run.toCanonical());
    env.bundleSource.caseSetDigests.erase(run.toCanonical());
    env.bundleSource.configs.erase(run.toCanonical());

    EvidenceBundleRequest request;
    request.reportId = env.report.reportId();
    request.runs = {run};
    request.target = targetPath("missing-all");

    const EvidenceBundleOutcome out = env.assembler.assemble(request);
    ASSERT_EQ(out.status, BundleStatus::Failed);
    ASSERT_TRUE(out.error.has_value());
    EXPECT_EQ(out.error->code(), ReportErrorCode::BundleIncomplete);
    const std::string detail = out.error->what();
    // 五项逐项在列（"<要素>@<runId>" 形态——实现口径的标签面）。
    EXPECT_NE(detail.find("envelope@" + run.toCanonical()), std::string::npos);
    EXPECT_NE(detail.find("reproduction@" + run.toCanonical()), std::string::npos);
    EXPECT_NE(detail.find("envelope-digest@" + run.toCanonical()), std::string::npos);
    EXPECT_NE(detail.find("case-set-digest@" + run.toCanonical()), std::string::npos);
    EXPECT_NE(detail.find("configurations@" + run.toCanonical()), std::string::npos);
    // 复现块不完整与缺位同级处置（不产出半包）。
    EXPECT_FALSE(env.writer.opened);
    EXPECT_FALSE(std::filesystem::exists(request.target));
}

/// 复现块不完整（两必填字段其一为空）→BundleIncomplete 逐字段列出——
/// acceptance 2"复现块不完整等要素缺失"的直接承载。
TEST(BundleTest, IncompleteReproductionBlockIsRejectedFieldByField)
{
    BundleEnv env;
    const core::RunId run = env.report.resultRefs()[0].runId;
    evidence::ReproductionBlock broken = legalReproduction();
    broken.evidenceContractVersion = "";   // 必填字段置空（不完整形态）
    env.resultSource.reproductions[run.toCanonical()] = broken;

    EvidenceBundleRequest request;
    request.reportId = env.report.reportId();
    request.runs = {run};
    request.target = targetPath("incomplete-repro");

    const EvidenceBundleOutcome out = env.assembler.assemble(request);
    ASSERT_EQ(out.status, BundleStatus::Failed);
    ASSERT_TRUE(out.error.has_value());
    EXPECT_EQ(out.error->code(), ReportErrorCode::BundleIncomplete);
    const std::string detail = out.error->what();
    EXPECT_NE(detail.find("reproduction.evidenceContractVersion@" + run.toCanonical()),
              std::string::npos);
    // 完整字段（productVersion 非空）不在缺失清单——逐字段核对而非整块否决。
    EXPECT_EQ(detail.find("reproduction.productVersion@" + run.toCanonical()),
              std::string::npos);
    EXPECT_FALSE(env.writer.opened);
}

/// 配置引用集缺位（nullopt＝事实不可解析）计缺失；空集（快照合法无配置）
/// 不计缺失——nullopt 与空集语义分界（§5.2"缺失与空值不等价"的对偶）。
TEST(BundleTest, EmptyConfigurationListIsLegalButNulloptIsMissing)
{
    // 空集形态：合法完成。
    BundleEnv emptyOk;
    const core::RunId run = emptyOk.report.resultRefs()[0].runId;
    emptyOk.bundleSource.configs[run.toCanonical()] = {};   // 空集（非缺位）
    EvidenceBundleRequest okRequest;
    okRequest.reportId = emptyOk.report.reportId();
    okRequest.runs = {run};
    okRequest.target = targetPath("configs-empty");
    const EvidenceBundleOutcome okOut = emptyOk.assembler.assemble(okRequest);
    EXPECT_EQ(okOut.status, BundleStatus::Completed);

    // 缺位形态：BundleIncomplete。
    BundleEnv missing;
    missing.bundleSource.configs.erase(run.toCanonical());
    EvidenceBundleRequest missRequest;
    missRequest.reportId = missing.report.reportId();
    missRequest.runs = {run};
    missRequest.target = targetPath("configs-missing");
    const EvidenceBundleOutcome missOut = missing.assembler.assemble(missRequest);
    ASSERT_EQ(missOut.status, BundleStatus::Failed);
    ASSERT_TRUE(missOut.error.has_value());
    EXPECT_EQ(missOut.error->code(), ReportErrorCode::BundleIncomplete);
    EXPECT_NE(std::string(missOut.error->what()).find("configurations@" + run.toCanonical()),
              std::string::npos);

    std::error_code ec;
    std::filesystem::remove(okRequest.target, ec);
}

// =====================================================================
// acceptance 3（取消/失败清理——临时清理、目标不变）
// =====================================================================

/// 协作取消令牌（放行前 n 次查询，其后恒取消——ArchiveExportTest 同款）。
class PollsThenCancelToken final : public ReportCancelToken {
public:
    explicit PollsThenCancelToken(int freePolls) : freePolls_(freePolls) {}
    bool isCancelled() const override { return ++polls_ > freePolls_; }

private:
    int freePolls_;
    mutable int polls_ = 0;
};

/// 入轨取消（收集开始前）→Canceled 全空形态；零会话、目标不存在。
TEST(BundleTest, CancelBeforeCollectionReturnsCanceledWithNoSession)
{
    BundleEnv env;
    PollsThenCancelToken cancel(0);   // 第一次查询即取消（入轨检查点）

    EvidenceBundleRequest request;
    request.reportId = env.report.reportId();
    request.target = targetPath("cancel-entry");

    const EvidenceBundleOutcome out = env.assembler.assemble(request, &cancel);
    EXPECT_EQ(out.status, BundleStatus::Canceled);
    EXPECT_FALSE(out.totalDigest.has_value());
    EXPECT_FALSE(out.error.has_value());   // UX-03：正常取消非错误
    EXPECT_TRUE(out.diagnostics.empty());
    EXPECT_FALSE(env.writer.opened);
    EXPECT_FALSE(std::filesystem::exists(request.target));
}

/// 收集阶段取消（逐运行检查点）→Canceled；零会话（收集产物尚未落盘——
/// 全内存收集的"零临时"保证）。
TEST(BundleTest, CancelDuringCollectionReturnsCanceledBeforeOpen)
{
    BundleEnv env;
    PollsThenCancelToken cancel(1);   // 放行入轨检查点，逐运行检查点取消

    EvidenceBundleRequest request;
    request.reportId = env.report.reportId();
    request.target = targetPath("cancel-collect");

    const EvidenceBundleOutcome out = env.assembler.assemble(request, &cancel);
    EXPECT_EQ(out.status, BundleStatus::Canceled);
    EXPECT_FALSE(out.error.has_value());
    EXPECT_FALSE(env.writer.opened);
    EXPECT_FALSE(std::filesystem::exists(request.target));
}

/// 协作取消（写出阶段逐条目检查点）→Canceled；目标不变；writer 会话经其
/// 属主析构收尾（RAII 清理临时——Bundle.hpp 文件头"放弃路径"节的观测承载；
/// 本用例以独立存续期构造全套注入，writer 的析构点在断言中间）。
TEST(BundleTest, CancelDuringWriteCleansTempAndLeavesTargetUntouched)
{
    ReviewReport report = makeStandardReport();
    const core::RunId run = report.resultRefs()[0].runId;
    FakeResolver resolver;
    resolver.report = report;
    FakeResultSource resultSource;
    const BindingIds ids;
    resultSource.envelopes[run.toCanonical()] = legalFeasibleEnvelope(run, ids);
    resultSource.reproductions[run.toCanonical()] = legalReproduction();
    FakeBundleSource bundleSource;
    bundleSource.envelopeDigests[run.toCanonical()] = cid(0x80).bytes;
    bundleSource.caseSetDigests[run.toCanonical()] = cid(0x90);
    bundleSource.configs[run.toCanonical()] = {BundleConfigRef{"solver.seed", cid(0xA0)}};
    UnreachableIoFactory ioFactory;
    PollsThenCancelToken cancel(2);   // 放行入轨＋收集，首条目写前取消

    EvidenceBundleRequest request;
    request.reportId = report.reportId();
    request.target = targetPath("cancel-write");

    EvidenceBundleOutcome out;
    {
        // writer 以独立存续期持有（其析构＝放弃路径清理的触发点）。
        FakeArchiveWriter writer;
        EvidenceBundleAssembler assembler{resolver, resultSource, bundleSource, writer,
                                          ioFactory};
        out = assembler.assemble(request, &cancel);
        // 会话已开启但未发布：目标不存在；临时文件仍在（等待属主析构清理）。
        EXPECT_EQ(out.status, BundleStatus::Canceled);
        EXPECT_FALSE(out.error.has_value());
        EXPECT_TRUE(writer.opened);
        EXPECT_FALSE(writer.finished);
        EXPECT_FALSE(std::filesystem::exists(request.target));
        ASSERT_FALSE(writer.tempPath().empty());
        EXPECT_TRUE(std::filesystem::exists(writer.tempPath()));
    }   // writer 析构——放弃路径清理临时（RAII）。
    EXPECT_FALSE(std::filesystem::exists(std::filesystem::path{
        request.target.string() + ".ird-fake-partial"}));   // 临时已清理
    EXPECT_FALSE(std::filesystem::exists(request.target));   // 目标不变
}

/// IArchiveWriter 写出失败（addEntry 注入）→Failed（ExportFailed）；临时
/// 由失败路径/析构清理、目标不变（先前输出完整保留——§7.4 同精神）。
TEST(BundleTest, WriterFailureAtAddEntryFailsWithTargetUntouched)
{
    BundleEnv env;
    const core::RunId run = env.report.resultRefs()[0].runId;
    env.writer.failAtAddEntry = 2;   // 第 2 次 addEntry 抛出（snapshot 之后）

    EvidenceBundleRequest request;
    request.reportId = env.report.reportId();
    request.runs = {run};
    request.target = targetPath("fail-add");

    const EvidenceBundleOutcome out = env.assembler.assemble(request);
    EXPECT_EQ(out.status, BundleStatus::Failed);
    ASSERT_TRUE(out.error.has_value());
    EXPECT_EQ(out.error->code(), ReportErrorCode::ExportFailed);
    EXPECT_FALSE(env.writer.finished);
    EXPECT_FALSE(std::filesystem::exists(request.target));   // 目标不变（未发布）
}

/// IArchiveWriter finish 失败（发布失败注入）→Failed；临时已由失败路径
/// 清理（§9.6 finish 签名注释"异常/失败→清理临时、目标不变"）、目标不变。
TEST(BundleTest, WriterFailureAtFinishCleansTempAndKeepsTarget)
{
    BundleEnv env;
    const core::RunId run = env.report.resultRefs()[0].runId;
    env.writer.failAtFinish = true;

    EvidenceBundleRequest request;
    request.reportId = env.report.reportId();
    request.runs = {run};
    request.target = targetPath("fail-finish");

    const EvidenceBundleOutcome out = env.assembler.assemble(request);
    EXPECT_EQ(out.status, BundleStatus::Failed);
    ASSERT_TRUE(out.error.has_value());
    EXPECT_EQ(out.error->code(), ReportErrorCode::ExportFailed);
    EXPECT_FALSE(env.writer.finished);
    // 临时清理（失败路径即时清理——非仅析构兜底）＋目标不变。
    EXPECT_TRUE(env.writer.tempPath().empty());
    EXPECT_FALSE(std::filesystem::exists(request.target));
}

/// 失败可重试（§7.4"保留选择与路径可重试"同精神）：故障清除后同请求
/// 重装成功、目标完整。
TEST(BundleTest, RetryAfterWriterFailureSucceeds)
{
    BundleEnv env;
    const core::RunId run = env.report.resultRefs()[0].runId;
    env.writer.failAtAddEntry = 1;   // 先注故障

    EvidenceBundleRequest request;
    request.reportId = env.report.reportId();
    request.runs = {run};
    request.target = targetPath("retry");

    const EvidenceBundleOutcome failed = env.assembler.assemble(request);
    ASSERT_EQ(failed.status, BundleStatus::Failed);

    env.writer.failAtAddEntry = 0;   // 清除故障重试（选择与路径保留）
    const EvidenceBundleOutcome retried = env.assembler.assemble(request);
    EXPECT_EQ(retried.status, BundleStatus::Completed);
    ASSERT_TRUE(retried.totalDigest.has_value());
    EXPECT_TRUE(std::filesystem::exists(request.target));

    std::error_code ec;
    std::filesystem::remove(request.target, ec);
}

// =====================================================================
// Usage 矩阵（调用方契约违约 fail-fast——不产出任何目标）
// =====================================================================

/// Usage 矩阵：零 reportId／空 target／runs 越界／runs 重复／reportId 不可
/// 解析——五路全部 ReportError(Usage) 抛出，且零写出零临时。
TEST(BundleTest, UsageViolationsAreRejectedFast)
{
    BundleEnv env;
    const core::RunId run = env.report.resultRefs()[0].runId;

    EvidenceBundleRequest base;
    base.reportId = env.report.reportId();
    base.runs = {run};
    base.target = targetPath("usage");

    // 零 reportId（保留值）。
    {
        EvidenceBundleRequest r = base;
        r.reportId = ReportId{};
        EXPECT_THROW(env.assembler.assemble(r), ReportError);
    }
    // 空 target。
    {
        EvidenceBundleRequest r = base;
        r.target.clear();
        EXPECT_THROW(env.assembler.assemble(r), ReportError);
    }
    // runs 越界（报告未引用的运行）。
    {
        EvidenceBundleRequest r = base;
        r.runs = {fixtureId<co::RunId>(0xFE)};
        EXPECT_THROW(env.assembler.assemble(r), ReportError);
    }
    // runs 重复。
    {
        EvidenceBundleRequest r = base;
        r.runs = {run, run};
        EXPECT_THROW(env.assembler.assemble(r), ReportError);
    }
    // reportId 不可解析。
    {
        BundleEnv fresh;
        EvidenceBundleRequest r = base;
        r.reportId = fixtureId<ReportId>(0x7F);   // 未注册的报告身份
        EXPECT_THROW(fresh.assembler.assemble(r), ReportError);
    }
    // 全程零写出（含未产生目标文件）。
    EXPECT_FALSE(std::filesystem::exists(base.target));
    EXPECT_FALSE(env.writer.opened);
}

}  // namespace
