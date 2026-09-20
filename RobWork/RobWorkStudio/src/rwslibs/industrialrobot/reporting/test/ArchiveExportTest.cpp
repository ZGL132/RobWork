/**
 * @file   ArchiveExportTest.cpp
 * @brief  RPT-T09 归档协调与导出服务单元测试——manifest 幂等判定键（D-02）、
 *         协调器编排四轨（幂等/冲突/磁盘/取消/只读）、导出服务外部路径
 *         幂等/冲突/覆盖与 Usage 矩阵（RP-IDEM-1/2、RP-CONF-1、RP-DISK-1、
 *         RP-RO-1、RP-CANC-1 的单元内承载面）。
 *
 * 设计依据：
 *   - units/reporting.md §7.3/§7.4（幂等判定键＝manifest 内容摘要、状态图
 *     abandon 纪律）、§8.5（导出编排）、§9.5/§9.6（契约维度表——非法调用/
 *     合法调用/取消/确定性各行）、§10.1（RP-IDEM/CONF/DISK/RO/CANC 组）
 *   - 任务契约 tasks/foundation/RPT-T09.json acceptance 1~4（acceptance 5
 *     的 sink 契约面随 contract_test/ArchiveCrossUnitContractTest.cpp）
 *
 * 替身边界声明：报告对象一律经 ReviewReport::make 构造（合法实例唯一生产
 *   者）；汇集座替身 FakeArtifactSink（test/FakeArtifactSink.hpp——本任务
 *   交付的最小确定性替身，边界声明在该文件头）；io 替身 FileBasedIoFactory
 *   以真实临时文件承载原子协议（mtime/哈希观测需要真实文件——替身的替换
 *   原子性为近似实现，io 真实原子协议归 io 单元验证矩阵）；脱敏经真实
 *   RedactionService。
 *
 * 确定性夹具：身份/数据源字段逐位固定（禁随机——同输入同摘要是幂等断言
 *   的前提）；磁盘满/取消注入经替身显式注入面，非时序依赖。
 */

#include <gtest/gtest.h>

#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/diagnostics/Redaction.hpp>
#include <sdurws/ird/reporting/Archive.hpp>
#include <sdurws/ird/reporting/Export.hpp>
#include <sdurws/ird/reporting/Render.hpp>
#include <sdurws/ird/reporting/Sections.hpp>

#include "FakeArtifactSink.hpp"

namespace {

using namespace sdurws::ird::reporting;
namespace co = sdurws::ird::core;
namespace core = sdurws::ird::core;
namespace ev = sdurws::ird::evidence;
namespace evidence = sdurws::ird::evidence;
namespace diagnostics = sdurws::ird::diagnostics;
using test_fakes::FakeArtifactSink;

// =====================================================================
// 确定性夹具（与 ConsistencyTest 同款纪律——自持不共享；身份逐位铺位）
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
/// ConsistencyTest 夹具同构）。
ReviewReportFields makeFields(double reachSi = 1.5)
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
    entry.fields.push_back(numericField("kin.reach", reachSi, core::ProvenanceKind::UserProvided,
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

ReviewReport makeStandardReport(double reachSi = 1.5)
{
    return ReviewReport::make(makeFields(reachSi), frozenReportLevelRule());
}

diagnostics::RedactionService& redactionService()
{
    static diagnostics::RedactionService service;
    return service;
}

core::Digest256 digestOf(const std::vector<std::uint8_t>& bytes)
{
    core::ContentDigester d;
    d.update(bytes.data(), bytes.size());
    return d.finalize();
}

/// 手工渲染产物（协调器测试用——RenderArtifact 是值类型，协调器不重渲染，
/// 只消费字节/摘要/版本列；digest 与 bytes 一致由本夹具保证）。
RenderArtifact handmadeArtifact(ReportRenderFormat format, const std::string& content,
                                const ReviewReport& report)
{
    RenderArtifact a;
    a.format = format;
    a.bytes.assign(content.begin(), content.end());
    a.digest = digestOf(a.bytes);
    a.rendererVersion = kReportRendererVersion;
    a.templateVersion = kHtmlTemplateVersion;
    a.sourceReportIdentity = report.contentIdentity();
    return a;
}

/// 按 §9.5 服务装配形态构造 manifest（与 Export.cpp 服务内装配同构——
/// 协调器测试的输入装配复用）。
ReportArtifactManifest makeManifest(const ReviewReport& report,
                                    const std::vector<RenderArtifact>& artifacts)
{
    ReportArtifactManifest m;
    m.reportId = report.reportId();
    m.reportVersion = report.reportVersion();
    m.supersedes = report.supersedes();
    m.level = report.level();
    m.project = report.project();
    m.branch = report.branch();
    m.revision = report.revision();
    m.dataIdentity = report.dataIdentity();
    m.contentIdentity = report.contentIdentity();
    m.generatedAtUtc = report.generatedAtUtc();
    m.generatedBy = report.generatedBy();
    m.generatorVersion = report.generatorVersion();
    for (const RenderArtifact& a : artifacts) {
        ArtifactManifestEntry e;
        e.relPath = std::string(artifactRelPath(a.format));
        e.format = a.format;
        e.sha256 = a.digest;
        e.sizeBytes = a.bytes.size();
        e.rendererVersion = a.rendererVersion;
        e.templateVersion = a.templateVersion;
        m.artifacts.push_back(std::move(e));
    }
    for (const ResultRefSnapshot& r : report.resultRefs()) {
        m.sourceRuns.push_back(r.runId);
    }
    return m;
}

// =====================================================================
// 取消令牌替身
// =====================================================================

/// 恒真取消（入轨即取消——检查点序位 1）。
class AlwaysCancelToken final : public ReportCancelToken {
public:
    bool isCancelled() const override { return true; }
};

/// 前 n 次查询放行、其后取消（定位中途检查点——查询计数即检查点序）。
class PollsThenCancelToken final : public ReportCancelToken {
public:
    explicit PollsThenCancelToken(int freePolls) : freePolls_(freePolls) {}
    bool isCancelled() const override
    {
        return ++polls_ > freePolls_;
    }
private:
    int freePolls_;                 ///< 放行的查询次数（其后恒取消）
    mutable int polls_ = 0;         ///< 已查询计数（检查点序观测）
};

// =====================================================================
// io 替身：真实文件原子目标＋内存 canonical writer（文件头替身边界声明）
// =====================================================================

/// 2 空格缩进 canonical JSON 序列化（最小确定性替身——T06 同款边界声明：
/// io 真实 canonical 行为归 io 验证矩阵；本替身只须产出可解析合法 JSON）。
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

class FailingCsvWriter final : public IReportCsvWriter {
public:
    bool open(IReportOutputTarget&&, bool) override { return false; }
    bool writeHeader(const std::vector<std::string>&) override { return false; }
    bool writeRow(const std::vector<ReportCsvCell>&) override { return false; }
    bool finish() override { return false; }
};

/// 原子目标替身（真实文件——<target>.<8hex>.tmp 协议近似；替换原子性为
/// 替身近似，io 真实协议归 io 验证矩阵）。
class FakeFileAtomicTarget final : public IReportOutputTarget {
public:
    FakeFileAtomicTarget(std::filesystem::path path, ReplacePolicy policy, int tmpSerial,
                         bool failCommit)
        : path_(std::move(path)), policy_(policy), failCommit_(failCommit), tmpSerial_(tmpSerial)
    {
    }

    bool write(const std::uint8_t* bytes, std::size_t n) override
    {
        if (finished_) {
            return false;
        }
        buffer_.insert(buffer_.end(), bytes, bytes + n);
        return true;
    }

    bool commit() override
    {
        if (finished_) {
            return false;
        }
        finished_ = true;
        const std::filesystem::path tmp = tmpPath();
        {
            std::ofstream file(tmp, std::ios::binary);
            file.write(reinterpret_cast<const char*>(buffer_.data()),
                       static_cast<std::streamsize>(buffer_.size()));
            if (!file) {
                std::filesystem::remove(tmp, fsError_);
                return false;
            }
        }
        if (failCommit_) {
            std::filesystem::remove(tmp, fsError_);
            return false;   // 注入提交失败——目标不变（先前输出完整保留）
        }
        std::error_code ec;
        if (policy_ == ReplacePolicy::NeverOverwrite && std::filesystem::exists(path_, ec)) {
            // TOCTOU 兜底（探测后目标出现——io NeverOverwrite 语义的替身承载）。
            std::filesystem::remove(tmp, ec);
            return false;
        }
        if (std::filesystem::exists(path_, ec)) {
            std::filesystem::remove(path_, ec);   // 替身近似（Windows rename 不覆盖）
            if (ec) {
                std::filesystem::remove(tmp, ec);
                return false;
            }
        }
        std::filesystem::rename(tmp, path_, ec);
        if (ec) {
            std::filesystem::remove(tmp, ec);
            return false;
        }
        return true;
    }

    void abort() override
    {
        finished_ = true;
        buffer_.clear();
        std::error_code ec;
        std::filesystem::remove(tmpPath(), ec);
    }

private:
    /// 临时路径：<target>.<8hex>.tmp（P-RPT-1 原子协议的替身形态）。
    std::filesystem::path tmpPath() const
    {
        char hex[9];
        std::snprintf(hex, sizeof(hex), "%08x", static_cast<unsigned>(tmpSerial_));
        return path_.parent_path()
               / (path_.filename().string() + "." + hex + ".tmp");
    }

    std::filesystem::path path_;
    ReplacePolicy policy_;
    bool failCommit_;
    int tmpSerial_;
    bool finished_ = false;
    std::vector<std::uint8_t> buffer_;
    std::error_code fsError_;
};

/// 文件 io 工厂替身（导出服务外部路径的真实文件承载——makeAtomicTarget
/// 调用计数供"幂等零写"断言）。
class FileBasedIoFactory final : public IReportIoFactory {
public:
    bool failCommit = false;                  ///< 注入 commit 失败（OverwriteAtomic 失败半区）
    mutable int atomicTargetCreated = 0;      ///< makeAtomicTarget 调用计数（幂等无操作＝不增）
    mutable int atomicTargetCommitted = 0;    ///< commit 成功计数

    std::unique_ptr<IReportCsvWriter> makeCsvWriter() const override
    {
        return std::make_unique<FailingCsvWriter>();   // 本测试面不请求 CSV（不可达）
    }
    std::unique_ptr<IReportJsonWriter> makeJsonWriter() const override
    {
        return std::make_unique<FakeJsonWriter>();
    }
    std::unique_ptr<IReportOutputTarget> makeAtomicTarget(const std::filesystem::path& path,
                                                          ReplacePolicy policy) const override
    {
        ++atomicTargetCreated;
        return std::make_unique<FakeFileAtomicTarget>(path, policy, serial_++,
                                                      failCommit);
    }

private:
    mutable int serial_ = 1;   ///< 临时文件序号（8hex 递增——进程内唯一）
};

/// CSV 回读工厂占位（本测试面一致性检查只用 HTML+JSON——不可达）。
class NullCsvReader final : public IReportCsvReader {
public:
    bool readTables(const std::uint8_t*, std::size_t, std::vector<ReportCsvTable>&) override
    {
        return false;
    }
};

class NullCsvReaderFactory final : public IReportCsvReaderFactory {
public:
    std::unique_ptr<IReportCsvReader> makeCsvReader() const override
    {
        return std::make_unique<NullCsvReader>();
    }
};

/// 报告值解析替身（IReportResolver 的 map 承载；ReviewReport 无默认构造
/// ——插入用 emplace 而非 operator[]，避免默认构造需求）。
class MapResolver final : public IReportResolver {
public:
    void put(const ReviewReport& report) { reports_.insert_or_assign(report.reportId().toCanonical(), report); }

    std::optional<ReviewReport> tryResolve(ReportId reportId) const override
    {
        auto it = reports_.find(reportId.toCanonical());
        if (it == reports_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

private:
    std::map<std::string, ReviewReport> reports_;
};

// =====================================================================
// 临时目录夹具（外部路径真实文件观测）
// =====================================================================

class TempDir {
public:
    explicit TempDir(const std::string& name)
    {
        dir_ = std::filesystem::temp_directory_path()
               / ("ird-rpt-t09-" + name + "-" + std::to_string(++serial()));
        std::filesystem::remove_all(dir_);
        std::filesystem::create_directories(dir_);
    }
    ~TempDir() { std::filesystem::remove_all(dir_); }

    const std::filesystem::path& path() const { return dir_; }

private:
    static int& serial()
    {
        static int s = 0;
        return s;
    }
    std::filesystem::path dir_;
};

std::string readFileText(const std::filesystem::path& p)
{
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

void writeFileText(const std::filesystem::path& p, const std::string& text)
{
    std::ofstream f(p, std::ios::binary);
    f << text;
}

std::vector<std::uint8_t> asBytes(const std::string& text)
{
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

bool containsText(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}

// =====================================================================
// manifest 幂等判定键（acceptance 1——D-02：不以 ReportId、不以目标路径）
// =====================================================================

class ManifestDigestTest : public ::testing::Test {
protected:
    ReviewReport report_ = makeStandardReport();
    std::vector<RenderArtifact> artifacts_ = {
        handmadeArtifact(ReportRenderFormat::Html, "<html>报告 A</html>", report_),
        handmadeArtifact(ReportRenderFormat::Json, "{\"json\":1}", report_)};
    ReportArtifactManifest base_ = makeManifest(report_, artifacts_);
};

/// 同输入两次计算同摘要（NFR-COR-02——纯函数确定性）。
TEST_F(ManifestDigestTest, DeterministicSameInput)
{
    const core::ContentIdentity a = computeManifestDigest(base_);
    const core::ContentIdentity b = computeManifestDigest(base_);
    EXPECT_TRUE(a == b);
    EXPECT_TRUE(a.isValid());
}

/// D-02 排除域：报告身份链（reportId/supersedes/reportVersion）与数据源锚
/// （project/branch/revision）变化不改判定键——幂等以内容判定，不以 Id。
TEST_F(ManifestDigestTest, ReportIdentityExcludedFromKey)
{
    ReportArtifactManifest changed = base_;
    changed.reportId = fixtureId<ReportId>(0x7F);          // 不同 ReportId
    changed.supersedes = fixtureId<ReportId>(0x7E);        // 不同演化链
    changed.reportVersion = 9;
    changed.project = fixtureId<co::ProjectId>(0x5A);
    changed.branch = fixtureId<co::BranchId>(0x5B);
    changed.revision = fixtureId<co::RevisionId>(0x5C);
    EXPECT_TRUE(computeManifestDigest(changed) == computeManifestDigest(base_));
}

/// D-02/D-04 排除域：生成信息与发布时刻不入判定键（同输入重建摘要一致是
/// 幂等前提——发布时刻是发布事实非内容事实）。
TEST_F(ManifestDigestTest, GenerationAndFinalizeTimeExcluded)
{
    ReportArtifactManifest changed = base_;
    changed.generatedAtUtc = fixedTime(1800000000);
    changed.generatedBy = "another-batch";
    changed.generatorVersion = "rpt-9.9.9";
    changed.finalizedAtUtc = fixedTime(1800000001);
    EXPECT_TRUE(computeManifestDigest(changed) == computeManifestDigest(base_));
}

/// D-02 判定键核心：contentIdentity 变化＝不同内容＝不同摘要（按 Id 判幂等
/// 会误放行内容变化——D-02 备选被否的依据场景）。
TEST_F(ManifestDigestTest, ContentIdentityChangesDigest)
{
    ReportArtifactManifest changed = base_;
    changed.contentIdentity = cid(0xE0);
    EXPECT_FALSE(computeManifestDigest(changed) == computeManifestDigest(base_));
}

/// 判定键因子：rendererVersion/templateVersion 变化＝不同摘要（§7.4 判定键
/// 五元组的版本维）。
TEST_F(ManifestDigestTest, VersionDimensionsChangeDigest)
{
    ReportArtifactManifest ver = base_;
    ver.artifacts[0].rendererVersion = 99;
    EXPECT_FALSE(computeManifestDigest(ver) == computeManifestDigest(base_));

    ReportArtifactManifest tpl = base_;
    tpl.artifacts[1].templateVersion = 42;
    EXPECT_FALSE(computeManifestDigest(tpl) == computeManifestDigest(base_));
}

/// 工件集顺序无关（判定键＝集合语义——relPath 字节序稳定排序后编码）。
TEST_F(ManifestDigestTest, ArtifactOrderInsensitive)
{
    ReportArtifactManifest reordered = base_;
    std::swap(reordered.artifacts[0], reordered.artifacts[1]);
    EXPECT_TRUE(computeManifestDigest(reordered) == computeManifestDigest(base_));
}

/// 调用方违约：空工件集/重复 relPath＝Usage fail-fast（清单装配违约）。
TEST_F(ManifestDigestTest, UsageOnEmptyAndDuplicate)
{
    ReportArtifactManifest empty = base_;
    empty.artifacts.clear();
    EXPECT_THROW(computeManifestDigest(empty), ReportError);

    ReportArtifactManifest dup = base_;
    dup.artifacts[1].relPath = dup.artifacts[0].relPath;
    EXPECT_THROW(computeManifestDigest(dup), ReportError);
}

// =====================================================================
// 归档协调器（acceptance 1/2/3/4/5——编排四轨＋幂等预检零调用）
// =====================================================================

class CoordinatorTest : public ::testing::Test {
protected:
    ReviewReport report_ = makeStandardReport();
    std::vector<RenderArtifact> artifacts_ = {
        handmadeArtifact(ReportRenderFormat::Html, "<html>报告 A</html>", report_),
        handmadeArtifact(ReportRenderFormat::Json, "{\"json\":1}", report_)};
    ReportArtifactManifest manifest_ = makeManifest(report_, artifacts_);
    FakeArtifactSink sink_;
    ReportArchiveCoordinator coordinator_{sink_, sink_};   // index 与 sink 同源（替身合一）
};

/// 正常发布：编排顺序 begin→writeArtifact×N→finalize（§9.6 编排行）；发布
/// 记录六字段与 manifest 一致；工件相对路径含 manifest 自身（report.json）。
TEST_F(CoordinatorTest, PublishesWithOrchestrationOrder)
{
    const ArchiveOutcome out = coordinator_.publish(report_, artifacts_, manifest_);
    ASSERT_TRUE(out.status == ArchiveStatus::Published);
    ASSERT_TRUE(out.record.has_value());
    EXPECT_TRUE(out.record->reportId == report_.reportId());
    EXPECT_TRUE(out.record->contentIdentity == report_.contentIdentity());
    EXPECT_TRUE(out.record->manifestDigest == computeManifestDigest(manifest_));
    ASSERT_EQ(out.record->artifactRelPaths.size(), manifest_.artifacts.size() + 1);
    EXPECT_EQ(out.record->artifactRelPaths.back(), kManifestRelPath);
    EXPECT_TRUE(out.error == std::nullopt);
    EXPECT_TRUE(out.diagnostics.empty());

    // 编排顺序断言（sink 调用记录——acceptance 1/5 的观测通道）。
    ASSERT_EQ(sink_.calls.size(), 4u);   // begin + write×2 + finalize
    EXPECT_EQ(sink_.calls[0].method, "begin");
    EXPECT_EQ(sink_.calls[1].method, "writeArtifact");
    EXPECT_EQ(sink_.calls[2].method, "writeArtifact");
    EXPECT_EQ(sink_.calls[3].method, "finalize");
    EXPECT_EQ(sink_.activeSessions(), 0u);

    // 发布清单恰一条（listPublished 仅 Finalized）。
    ASSERT_EQ(coordinator_.listPublished(report_.project()).size(), 1u);
}

/// RP-IDEM-1（幂等命中）：同 reportId 已 Finalized 且 manifest 摘要一致＝
/// IdempotentHit 零重写——第二次发布零汇集座调用、返回既有记录原文
/// （含原发布时刻——不伪造）。
TEST_F(CoordinatorTest, IdempotentHitZeroRewrite)
{
    const ArchiveOutcome first = coordinator_.publish(report_, artifacts_, manifest_);
    ASSERT_TRUE(first.status == ArchiveStatus::Published);

    const std::size_t callsAfterFirst = sink_.calls.size();
    const ArchiveOutcome second = coordinator_.publish(report_, artifacts_, manifest_);
    EXPECT_TRUE(second.status == ArchiveStatus::IdempotentHit);
    ASSERT_TRUE(second.record.has_value());
    EXPECT_TRUE(second.record->publishedAtUtc == first.record->publishedAtUtc);   // 既有记录原文
    EXPECT_TRUE(second.record->manifestDigest == first.record->manifestDigest);
    // 零重写：第二次发布零汇集座调用（不 begin 不 write 不 finalize——磁盘
    // 零触碰的最强形态）。
    EXPECT_EQ(sink_.calls.size(), callsAfterFirst);
}

/// RP-CONF-1①（身份维度冲突）：同 reportId 已 Finalized、contentIdentity 不
/// 同＝ArchiveConflict，诊断定位"身份维度"，不覆盖（零汇集座写调用）。
TEST_F(CoordinatorTest, ConflictIdentityDimension)
{
    sink_.seedPublished(manifest_.project, manifest_.reportId, cid(0xE1), cid(0xF1));

    const ArchiveOutcome out = coordinator_.publish(report_, artifacts_, manifest_);
    EXPECT_TRUE(out.status == ArchiveStatus::Failed);
    ASSERT_TRUE(out.error.has_value());
    EXPECT_TRUE(out.error->code() == ReportErrorCode::ArchiveConflict);
    ASSERT_FALSE(out.diagnostics.empty());
    EXPECT_EQ(out.diagnostics.front().code, std::string(diagcodes::kArchiveConflict));
    EXPECT_TRUE(containsText(out.diagnostics.front().cause, "身份维度"));
    // 不覆盖：零汇集座调用（冲突在预检拒绝——已 Finalized 内容零触碰）。
    EXPECT_TRUE(sink_.calls.empty());
}

/// RP-CONF-1①（版本维度冲突）：contentIdentity 相同而 manifest 摘要不同＝
/// 诊断定位"版本/工件集维度"（D-02 判定键其余因子的变化面）。
TEST_F(CoordinatorTest, ConflictVersionDimension)
{
    sink_.seedPublished(manifest_.project, manifest_.reportId, manifest_.contentIdentity,
                        cid(0xF2));

    const ArchiveOutcome out = coordinator_.publish(report_, artifacts_, manifest_);
    EXPECT_TRUE(out.status == ArchiveStatus::Failed);
    ASSERT_TRUE(out.error.has_value());
    EXPECT_TRUE(out.error->code() == ReportErrorCode::ArchiveConflict);
    ASSERT_FALSE(out.diagnostics.empty());
    EXPECT_TRUE(containsText(out.diagnostics.front().cause, "版本/工件集维度"));
}

/// RP-DISK-1（写失败注入）：DiskFull＝abandon(Failed)＋清理＋比较型诊断
/// （所需侧数值在位/可用侧未提供——§7.4 表）；清除注入后重试成功（失败
/// 保留选择与路径可重试）。
TEST_F(CoordinatorTest, DiskFullAbandonsThenRetrySucceeds)
{
    sink_.diskFullAtWrite = 1;   // 第 1 次 writeArtifact 命中磁盘满

    const ArchiveOutcome out = coordinator_.publish(report_, artifacts_, manifest_);
    EXPECT_TRUE(out.status == ArchiveStatus::Failed);
    ASSERT_TRUE(out.error.has_value());
    EXPECT_TRUE(out.error->code() == ReportErrorCode::DiskFull);
    ASSERT_FALSE(out.diagnostics.empty());
    // 比较型诊断：期望侧＝失败工件所需字节（数值在位）；实际侧＝可用空间
    // 未提供（NotProvided 四态——精确值存储侧通道随 project 实现对齐）。
    const core::DiagnosticRecord& diag = out.diagnostics.front();
    ASSERT_TRUE(diag.comparison.has_value());
    EXPECT_TRUE(diag.comparison->expected.quantity.state() == co::FieldState::Provided);
    EXPECT_DOUBLE_EQ(diag.comparison->expected.quantity.value(),
                     static_cast<double>(artifacts_[0].bytes.size()));
    EXPECT_TRUE(diag.comparison->actual.quantity.state() == co::FieldState::NotProvided);

    // abandon（责任终结）已调、会话归零、未完成工件不入清单（PM-08）。
    ASSERT_FALSE(sink_.calls.empty());
    EXPECT_EQ(sink_.calls.back().method, "abandon");
    EXPECT_EQ(sink_.calls.back().detail, "failed");
    EXPECT_EQ(sink_.activeSessions(), 0u);
    EXPECT_TRUE(coordinator_.listPublished(report_.project()).empty());

    // 可重试：清除注入后原样重试成功。
    sink_.resetInjections();
    const ArchiveOutcome retry = coordinator_.publish(report_, artifacts_, manifest_);
    EXPECT_TRUE(retry.status == ArchiveStatus::Published);
    ASSERT_TRUE(retry.record.has_value());
}

/// RP-CANC-1（取消）：入轨取消＝Canceled（UX-03 全空形态）＋零汇集座调用
/// （尚未 begin——无临时物可清）。
TEST_F(CoordinatorTest, CancelBeforeBegin)
{
    AlwaysCancelToken token;
    ReportArchiveCoordinator c(sink_, sink_, &token);
    const ArchiveOutcome out = c.publish(report_, artifacts_, manifest_);
    EXPECT_TRUE(out.status == ArchiveStatus::Canceled);
    EXPECT_TRUE(out.record == std::nullopt);
    EXPECT_TRUE(out.error == std::nullopt);
    EXPECT_TRUE(out.diagnostics.empty());
    EXPECT_TRUE(sink_.calls.empty());   // 未 begin——磁盘零触碰
}

/// RP-CANC-1（中途取消）：逐工件写前检查点取消＝abandon(Canceled)＋已写
/// 工件不入清单（"文件存在≠已发布"——任务约束§五.6）。
TEST_F(CoordinatorTest, CancelBetweenWritesAbandons)
{
    // 检查点序：协调器 1 次预检（pre-begin）→逐工件写前各 1 次。2 工件时
    // 取消注入在第 3 次查询（write2 前）触发。
    PollsThenCancelToken token(2);
    ReportArchiveCoordinator c(sink_, sink_, &token);
    const ArchiveOutcome out = c.publish(report_, artifacts_, manifest_);
    EXPECT_TRUE(out.status == ArchiveStatus::Canceled);
    EXPECT_TRUE(out.record == std::nullopt);
    EXPECT_TRUE(out.error == std::nullopt);   // UX-03：正常取消非错误

    // abandon(Canceled) 已调、无 finalize（未完成——不入清单）。
    ASSERT_GE(sink_.calls.size(), 3u);
    EXPECT_EQ(sink_.calls.back().method, "abandon");
    EXPECT_EQ(sink_.calls.back().detail, "canceled");
    EXPECT_EQ(std::count_if(sink_.calls.begin(), sink_.calls.end(),
                            [](const FakeArtifactSink::CallRecord& r) {
                                return r.method == "finalize";
                            }),
              0);
    EXPECT_TRUE(coordinator_.listPublished(report_.project()).empty());
}

/// RP-RO-1（只读项目）：begin 抛 ReadOnlyStore＝Failed＋两路径诊断说明
/// （PM-07：项目内拒绝≠导出被禁）。
TEST_F(CoordinatorTest, ReadOnlyStoreRejectedWithTwoPathDiag)
{
    sink_.readOnly = true;
    const ArchiveOutcome out = coordinator_.publish(report_, artifacts_, manifest_);
    EXPECT_TRUE(out.status == ArchiveStatus::Failed);
    ASSERT_TRUE(out.error.has_value());
    EXPECT_TRUE(out.error->code() == ReportErrorCode::ReadOnlyStore);
    ASSERT_FALSE(out.diagnostics.empty());
    const core::DiagnosticRecord& diag = out.diagnostics.front();
    EXPECT_EQ(diag.code, std::string("RPT-READONLY-STORE"));
    // 两条路径说明：解锁后重试 / 项目外导出允许。
    EXPECT_TRUE(containsText(diag.recommendedAction, "两条路径"));
    EXPECT_TRUE(containsText(diag.recommendedAction, "项目外"));
    EXPECT_TRUE(sink_.calls.empty());   // begin 未成功——无会话
}

/// PM-03（责任终结）：清理失败注入下 abandon 仍终结（不抛出）、已 Finalized
/// 的其他报告零影响（残留仅计数——§7.4 行 9）。
TEST_F(CoordinatorTest, CleanupFailureLeavesPublishedUntouched)
{
    // 预置另一份已发布报告（对照——"已 Finalized 的其他报告不受影响"）。
    const ReportId otherId = fixtureId<ReportId>(0x7A);
    sink_.seedPublished(manifest_.project, otherId, cid(0xE9), cid(0xEA));

    sink_.cleanupFailure = true;
    sink_.diskFullAtWrite = 1;
    const ArchiveOutcome out = coordinator_.publish(report_, artifacts_, manifest_);
    EXPECT_TRUE(out.status == ArchiveStatus::Failed);   // 主失败如实上报
    EXPECT_EQ(sink_.cleanupFailedCount, 1);             // 残留登记（仅诊断——不抛出）

    // 已发布对照报告零影响、清单稳定。
    const std::vector<ReportListingEntry> listing = coordinator_.listPublished(report_.project());
    ASSERT_EQ(listing.size(), 1u);
    EXPECT_TRUE(listing.front().reportId == otherId);
}

/// 前置违约（Usage fail-fast）：报告未冻结/工件不同源/manifest 身份不符/
/// 工件集与清单数量不一致。
TEST_F(CoordinatorTest, UsageOnPreconditionViolations)
{
    // 工件与报告不同源（sourceReportIdentity 不符）。
    std::vector<RenderArtifact> foreign = artifacts_;
    foreign[0].sourceReportIdentity = cid(0xEE);
    EXPECT_THROW(coordinator_.publish(report_, foreign, manifest_), ReportError);

    // manifest 身份列与报告不一致（contentIdentity 错链）。
    ReportArtifactManifest badManifest = manifest_;
    badManifest.contentIdentity = cid(0xED);
    EXPECT_THROW(coordinator_.publish(report_, artifacts_, badManifest), ReportError);

    // 清单与产物集数量不一致。
    ReportArtifactManifest shortManifest = manifest_;
    shortManifest.artifacts.pop_back();
    EXPECT_THROW(coordinator_.publish(report_, artifacts_, shortManifest), ReportError);

    // 空工件集。
    EXPECT_THROW(coordinator_.publish(report_, {}, manifest_), ReportError);

    // 上述违约均未产生任何汇集座调用（fail-fast 先于磁盘）。
    EXPECT_TRUE(sink_.calls.empty());
}

// =====================================================================
// 导出服务（acceptance 1/2/3/4——外部路径幂等/冲突/覆盖、只读两路径、
// Usage 矩阵、取消）
// =====================================================================

class ExportServiceTest : public ::testing::Test {
protected:
    ReviewReport report_ = makeStandardReport();
    FakeArtifactSink sink_;
    FileBasedIoFactory io_;
    NullCsvReaderFactory csvReaders_;
    MapResolver resolver_;
    diagnostics::RedactionService redaction_;
    ReportExportService service_{io_, redaction_, sink_, sink_, csvReaders_, resolver_};

    void SetUp() override { resolver_.put(report_); }

    ReportExportRequest externalRequest(std::filesystem::path dir)
    {
        ReportExportRequest req;
        req.project = report_.project();
        req.reportId = report_.reportId();
        req.formats = {ReportRenderFormat::Html, ReportRenderFormat::Json};
        req.destination.kind = ExportDestination::ExternalPath;
        req.destination.externalPath = std::move(dir);
        return req;
    }

    ReportExportRequest archiveRequest()
    {
        ReportExportRequest req;
        req.project = report_.project();
        req.reportId = report_.reportId();
        req.formats = {ReportRenderFormat::Html, ReportRenderFormat::Json};
        req.destination.kind = ExportDestination::ProjectArchive;
        return req;
    }
};

/// §9.5 非法调用矩阵（Usage 结果错误轨）：空 formats/不含 Html/重复格式/
/// 零 reportId/不可解析/外部路径为空/证据包标志（落位偏差④）。
TEST_F(ExportServiceTest, UsageMatrix)
{
    ReportExportRequest base = archiveRequest();

    {
        ReportExportRequest req = base;
        req.formats.clear();
        EXPECT_TRUE(service_.exportReport(req).error->code() == ReportErrorCode::Usage);
    }
    {
        ReportExportRequest req = base;
        req.formats = {ReportRenderFormat::Json};   // 不含 Html——RPT-02
        EXPECT_TRUE(service_.exportReport(req).error->code() == ReportErrorCode::Usage);
    }
    {
        ReportExportRequest req = base;
        req.formats = {ReportRenderFormat::Html, ReportRenderFormat::Html};
        EXPECT_TRUE(service_.exportReport(req).error->code() == ReportErrorCode::Usage);
    }
    {
        ReportExportRequest req = base;
        req.reportId = ReportId{};   // 零保留值
        EXPECT_TRUE(service_.exportReport(req).error->code() == ReportErrorCode::Usage);
    }
    {
        ReportExportRequest req = base;
        req.reportId = fixtureId<ReportId>(0x77);   // 未注册——解析不到
        EXPECT_TRUE(service_.exportReport(req).error->code() == ReportErrorCode::Usage);
    }
    {
        ReportExportRequest req = externalRequest(TempDir("usage-empty").path());
        req.destination.externalPath.clear();
        EXPECT_TRUE(service_.exportReport(req).error->code() == ReportErrorCode::Usage);
    }
    {
        ReportExportRequest req = base;
        req.withEvidenceBundle = true;   // RPT-T10 边界——显式拒绝非静默
        const ReportExportResult r = service_.exportReport(req);
        EXPECT_TRUE(r.error->code() == ReportErrorCode::Usage);
        EXPECT_TRUE(containsText(r.error->what(), "RPT-T10"));
    }
    // 全部 Usage 均未触碰磁盘（零汇集座调用、零原子目标创建）。
    EXPECT_TRUE(sink_.calls.empty());
    EXPECT_EQ(io_.atomicTargetCreated, 0);
}

/// 外部导出成功：两文件按工件相对名落盘（report.html/report-data.json）、
/// 摘要与渲染产物一致、返回 files（published 空）。
TEST_F(ExportServiceTest, ExternalExportWritesFiles)
{
    TempDir dir("ext-ok");
    const ReportExportResult r = service_.exportReport(externalRequest(dir.path()));
    EXPECT_FALSE(r.error.has_value());
    ASSERT_EQ(r.files.size(), 2u);
    EXPECT_FALSE(r.published.has_value());

    EXPECT_TRUE(containsText(r.files[0].path.string(), "report.html"));
    EXPECT_TRUE(containsText(r.files[1].path.string(), "report-data.json"));
    for (const ExportedFile& f : r.files) {
        const std::string text = readFileText(f.path);
        EXPECT_FALSE(text.empty());
        EXPECT_TRUE(f.sha256 == digestOf(asBytes(text)));   // 摘要与落盘字节一致
    }
    // 幂等预检的判定基础自证：两文件摘要互异且非零。
    EXPECT_FALSE(r.files[0].sha256 == r.files[1].sha256);
}

/// RP-IDEM-1②（外部幂等）：同字节重复导出＝幂等成功无操作——文件
/// mtime/哈希不变、零原子目标创建（acceptance 1 外部半区）。
TEST_F(ExportServiceTest, ExternalIdempotentNoOp)
{
    TempDir dir("ext-idem");
    const ReportExportResult first = service_.exportReport(externalRequest(dir.path()));
    ASSERT_FALSE(first.error.has_value());

    // 记录既有文件的 mtime（真实文件观测——替身零写调用时 mtime 必不变）。
    std::map<std::string, std::filesystem::file_time_type> mtimes;
    for (const ExportedFile& f : first.files) {
        mtimes[f.path.filename().string()] = std::filesystem::last_write_time(f.path);
    }

    const int createdBefore = io_.atomicTargetCreated;
    const ReportExportResult second = service_.exportReport(externalRequest(dir.path()));
    EXPECT_FALSE(second.error.has_value());
    ASSERT_EQ(second.files.size(), 2u);   // 幂等成功仍返回逐文件（既有摘要）

    for (const ExportedFile& f : second.files) {
        const auto it = mtimes.find(f.path.filename().string());
        ASSERT_NE(it, mtimes.end());
        EXPECT_EQ(std::filesystem::last_write_time(f.path), it->second);   // mtime 不变
        const core::Digest256 onDisk = digestOf(asBytes(readFileText(f.path)));
        EXPECT_TRUE(f.sha256 == onDisk);   // 哈希不变
    }
    EXPECT_EQ(io_.atomicTargetCreated, createdBefore);   // 零写调用（无操作）
}

/// RP-CONF-1②（外部冲突）：目标不同内容＋NeverOverwrite（默认）＝拒绝、
/// 既有文件字节不变、诊断含 io 族码引用与双摘要（可重试——保留路径）。
TEST_F(ExportServiceTest, ExternalConflictNeverOverwriteKeepsExisting)
{
    TempDir dir("ext-conflict");
    writeFileText(dir.path() / "report.html", "旧内容——先前输出");

    const ReportExportResult r = service_.exportReport(externalRequest(dir.path()));
    ASSERT_TRUE(r.error.has_value());
    EXPECT_TRUE(r.error->code() == ReportErrorCode::ExportFailed);
    ASSERT_FALSE(r.diagnostics.empty());
    EXPECT_EQ(r.diagnostics.front().code, std::string(diagcodes::kExportFailed));
    EXPECT_TRUE(containsText(r.diagnostics.front().cause, "IO-PACK-TARGET-EXISTS"));
    EXPECT_TRUE(containsText(r.diagnostics.front().recommendedAction, "OverwriteAtomic"));

    // 既有文件完整保留（不覆盖——零写调用）。
    EXPECT_EQ(readFileText(dir.path() / "report.html"), "旧内容——先前输出");
    EXPECT_TRUE(r.files.empty());
}

/// RP-CONF-1②（确认覆盖）：OverwriteAtomic＝原子替换成功、内容为新字节。
TEST_F(ExportServiceTest, ExternalOverwriteAtomicReplaces)
{
    TempDir dir("ext-overwrite");
    writeFileText(dir.path() / "report.html", "旧内容——先前输出");

    ReportExportRequest req = externalRequest(dir.path());
    req.destination.replace = ReplacePolicy::OverwriteAtomic;   // 用户显式确认
    const ReportExportResult r = service_.exportReport(req);
    EXPECT_FALSE(r.error.has_value());
    ASSERT_EQ(r.files.size(), 2u);

    // report.html 已替换为新渲染字节（与幂等导出结果一致——确定性）。
    TempDir reference("ext-overwrite-ref");
    const ReportExportResult ref = service_.exportReport(externalRequest(reference.path()));
    ASSERT_FALSE(ref.error.has_value());
    EXPECT_EQ(readFileText(dir.path() / "report.html"),
              readFileText(reference.path() / "report.html"));
}

/// RP-CONF-1②（替换前保留）：OverwriteAtomic 提交失败＝目标不变、先前输出
/// 完整保留、无临时残留（io 原子协议的失败半区——§7.4 行 6）。
TEST_F(ExportServiceTest, ExternalOverwriteFailurePreservesPrevious)
{
    TempDir dir("ext-overwrite-fail");
    writeFileText(dir.path() / "report.html", "旧内容——先前输出");

    io_.failCommit = true;
    ReportExportRequest req = externalRequest(dir.path());
    req.destination.replace = ReplacePolicy::OverwriteAtomic;
    const ReportExportResult r = service_.exportReport(req);
    ASSERT_TRUE(r.error.has_value());
    EXPECT_TRUE(r.error->code() == ReportErrorCode::ExportFailed);

    // 先前输出完整保留（替换前保留——commit 失败不触碰目标）。
    EXPECT_EQ(readFileText(dir.path() / "report.html"), "旧内容——先前输出");
    // 无临时残留（abort 清理——<target>.<8hex>.tmp 全清）。
    for (const auto& entry : std::filesystem::directory_iterator(dir.path())) {
        EXPECT_FALSE(containsText(entry.path().string(), ".tmp")) << entry.path();
    }
}

/// RP-DISK-1（服务级可重试）：项目内发布磁盘满＝DiskFull 错误；清除注入后
/// 重试成功（选择与路径保留——同一请求原样重放）。
TEST_F(ExportServiceTest, ProjectArchiveDiskFullRetrySucceeds)
{
    sink_.diskFullAtWrite = 1;
    const ReportExportResult failed = service_.exportReport(archiveRequest());
    ASSERT_TRUE(failed.error.has_value());
    EXPECT_TRUE(failed.error->code() == ReportErrorCode::DiskFull);
    EXPECT_TRUE(failed.published == std::nullopt);

    sink_.resetInjections();
    const ReportExportResult retry = service_.exportReport(archiveRequest());
    EXPECT_FALSE(retry.error.has_value());
    ASSERT_TRUE(retry.published.has_value());
    EXPECT_TRUE(retry.published->reportId == report_.reportId());
}

/// 项目内发布成功：published 记录在场、汇集座存储的 manifest 身份列与报告
/// 一致（§7.3 字段装配——服务编排的 manifest 装配面）。
TEST_F(ExportServiceTest, ProjectArchivePublishesWithManifest)
{
    const ReportExportResult r = service_.exportReport(archiveRequest());
    EXPECT_FALSE(r.error.has_value());
    ASSERT_TRUE(r.published.has_value());
    EXPECT_TRUE(r.published->contentIdentity == report_.contentIdentity());
    EXPECT_TRUE(r.published->archiveState == ReportArchiveState::Finalized);

    // 汇集座行的 manifest 身份列（发布事实与报告同源——§7.3）。
    const FakeArtifactSink::PublishedRow& row =
        sink_.published.begin()->second;
    EXPECT_TRUE(row.manifest.reportId == report_.reportId());
    EXPECT_TRUE(row.manifest.level == report_.level());
    EXPECT_TRUE(row.manifest.dataIdentity == report_.dataIdentity());
    EXPECT_TRUE(row.manifest.contentIdentity == report_.contentIdentity());
    ASSERT_EQ(row.manifest.sourceRuns.size(), report_.resultRefs().size());
    EXPECT_TRUE(row.manifest.sourceRuns.front() == report_.resultRefs().front().runId);
    for (const ArtifactManifestEntry& e : row.manifest.artifacts) {
        const auto it = row.artifacts.find(e.relPath);
        ASSERT_NE(it, row.artifacts.end());
        EXPECT_TRUE(digestOf(it->second) == e.sha256);   // 工件摘要与字节一致
    }
}

/// RP-IDEM-1①（服务级幂等）：同请求二次发布＝IdempotentHit 语义（published
/// ＝既有记录、publishedAtUtc 不变）；重复导出不产生第二份存储行。
TEST_F(ExportServiceTest, ProjectArchiveIdempotentRepeat)
{
    const ReportExportResult first = service_.exportReport(archiveRequest());
    ASSERT_TRUE(first.published.has_value());
    const std::size_t storeRows = sink_.published.size();

    const ReportExportResult second = service_.exportReport(archiveRequest());
    EXPECT_FALSE(second.error.has_value());
    ASSERT_TRUE(second.published.has_value());
    EXPECT_TRUE(second.published->publishedAtUtc == first.published->publishedAtUtc);
    EXPECT_TRUE(second.published->manifestDigest == first.published->manifestDigest);
    EXPECT_EQ(sink_.published.size(), storeRows);   // 零新增存储（零重写）
}

/// RP-RO-1（服务级两路径）：只读项目 ProjectArchive＝ReadOnlyStore＋两路径
/// 诊断；同请求改外部路径＝导出成功（PM-07——ui report.export readOnlyAllowed）。
TEST_F(ExportServiceTest, ReadOnlyProjectTwoPaths)
{
    sink_.readOnly = true;

    const ReportExportResult archived = service_.exportReport(archiveRequest());
    ASSERT_TRUE(archived.error.has_value());
    EXPECT_TRUE(archived.error->code() == ReportErrorCode::ReadOnlyStore);
    ASSERT_FALSE(archived.diagnostics.empty());
    EXPECT_TRUE(containsText(archived.diagnostics.front().recommendedAction, "两条路径"));

    // 项目外导出允许（只读项目不拦外部路径——§7.4 只读行）。
    TempDir dir("ext-readonly");
    const ReportExportResult external = service_.exportReport(externalRequest(dir.path()));
    EXPECT_FALSE(external.error.has_value());
    ASSERT_EQ(external.files.size(), 2u);
}

/// RP-CANC-1（服务级取消·项目内）：入轨取消＝五成员全空（UX-03 形态）＋
/// 零汇集座调用。
TEST_F(ExportServiceTest, CancelProjectArchiveAtEntry)
{
    AlwaysCancelToken token;
    const ReportExportResult r = service_.exportReport(archiveRequest(), &token);
    // 取消形态＝五成员全空（落位偏差③——构建器 outcome 同款 UX-03 形态）。
    EXPECT_FALSE(r.published.has_value());
    EXPECT_TRUE(r.files.empty());
    EXPECT_FALSE(r.bundleDigest.has_value());
    EXPECT_TRUE(r.diagnostics.empty());
    EXPECT_FALSE(r.error.has_value());
    EXPECT_TRUE(sink_.calls.empty());
}

/// RP-CANC-1（服务级取消·中途）：逐工件写前取消＝abandon(Canceled)、目标
/// 不变（无 finalize——未完成工件不入清单）。
TEST_F(ExportServiceTest, CancelProjectArchiveBetweenWrites)
{
    // 检查点序（{Html, Json} 两格式）：1 入轨、2/3 逐格式渲染后、4 协调器
    // 预检、5 写前#1、6 写前#2、7 finalize 前——注入在 6 触发（写前#2）。
    PollsThenCancelToken token(5);
    const ReportExportResult r = service_.exportReport(archiveRequest(), &token);
    EXPECT_FALSE(r.published.has_value());
    EXPECT_TRUE(r.files.empty());
    EXPECT_FALSE(r.error.has_value());   // 正常取消非错误

    const int abandons = static_cast<int>(std::count_if(
        sink_.calls.begin(), sink_.calls.end(),
        [](const FakeArtifactSink::CallRecord& c) { return c.method == "abandon"; }));
    const int finalizes = static_cast<int>(std::count_if(
        sink_.calls.begin(), sink_.calls.end(),
        [](const FakeArtifactSink::CallRecord& c) { return c.method == "finalize"; }));
    EXPECT_EQ(abandons, 1);
    EXPECT_EQ(sink_.calls.back().detail, "canceled");
    EXPECT_EQ(finalizes, 0);
    EXPECT_TRUE(sink_.published.empty());   // 未完成工件不入清单
}

/// RP-CANC-1（服务级取消·外部）：取消＝无文件写出、结果全空、无临时残留。
TEST_F(ExportServiceTest, CancelExternalExport)
{
    TempDir dir("ext-cancel");
    AlwaysCancelToken token;
    const ReportExportResult r = service_.exportReport(externalRequest(dir.path()), &token);
    EXPECT_TRUE(r.files.empty());
    EXPECT_FALSE(r.error.has_value());
    EXPECT_EQ(std::distance(std::filesystem::directory_iterator(dir.path()),
                            std::filesystem::directory_iterator()),
              0);   // 目录空——零写出
}

}  // namespace
