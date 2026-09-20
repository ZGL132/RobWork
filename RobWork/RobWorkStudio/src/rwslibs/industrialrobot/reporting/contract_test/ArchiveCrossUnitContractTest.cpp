/**
 * @file   ArchiveCrossUnitContractTest.cpp
 * @brief  RPT-T09 跨单元契约测试——与 project 工件汇集座（fake
 *         IReportArtifactSink——§3.4 测试目标分工的"与 project 工件汇集座
 *         协作"面）和 io 写出设施（注入 fake——P-RPT-1）的协作契约。
 *
 * 设计依据：
 *   - units/reporting.md §3.4（`_contract_test` 目标分工：跨单元契约面）、
 *     §7.3（D-13 manifest 原子发布＝唯一完整标志）、§7.4（D-14 幂等判定）、
 *     §9.6（sink 四方法契约——project 侧实现的语义投影）、§12.2（向 project
 *     交接：IReportArtifactSink 契约＋manifest＋sources.json 结构——本测试
 *     即该契约的常驻自证）
 *   - 任务契约 tasks/foundation/RPT-T09.json acceptance 5（sink 四方语义与
 *     §9.6 逐字一致；幂等/冲突判定承载；P-RPT-1 公共头零 io 类型）
 *
 * 替身边界声明：FakeArtifactSink（test/FakeArtifactSink.hpp——边界声明在
 *   其文件头）在此作为 project 侧实现的**契约投影替身**——验证的是
 *   reporting 侧对 sink 契约的编排与消费语义，不构成 project 持久化实现的
 *   正确性证明（D-13/D-14 的磁盘事实归 project 验证矩阵）。
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/reporting/Archive.hpp>
#include <sdurws/ird/reporting/Errors.hpp>
#include <sdurws/ird/reporting/Export.hpp>
#include <sdurws/ird/reporting/Identity.hpp>

#include "../test/FakeArtifactSink.hpp"

// 测试源码树位置（构建定义注入——公共头零 io 扫描用）。
#ifndef IRD_REPORTING_UNIT_ROOT
#define IRD_REPORTING_UNIT_ROOT "."
#endif

namespace {

using namespace sdurws::ird::reporting;
namespace co = sdurws::ird::core;
using test_fakes::FakeArtifactSink;

/// 确定性 ReportId（逐字节铺位——避免手写 32 位 hex 的拼写脆弱性）。
ReportId fixtureReportId(std::uint8_t seed)
{
    ReportId id;
    for (std::size_t i = 0; i < id.bytes.size(); ++i) {
        id.bytes[i] = static_cast<std::uint8_t>(seed + i);
    }
    return id;
}

/// 提取全部 #include 行（P-RPT-1 扫描口径——只审预处理包含行，注释文本
/// 中的词面不构成编译边）。
std::vector<std::string> includeLines(const std::string& text)
{
    std::vector<std::string> lines;
    std::size_t pos = 0;
    while ((pos = text.find("#include", pos)) != std::string::npos) {
        const std::size_t eol = text.find('\n', pos);
        lines.push_back(text.substr(pos, eol == std::string::npos ? eol : eol - pos));
        pos = eol == std::string::npos ? text.size() : eol;
    }
    return lines;
}

// =====================================================================
// 契约钉：值类型形状与 §9.6/§7.3 逐字面（编译期自证——上游冻结 diff 的
// 增量同步触发器：任一断言失败即上public面漂移，按 DTB §5.4 登记）
// =====================================================================

/// ArtifactStatus 六值序＝§9.6 原文声明序（数值入二进制契约面——稳定第一，
/// Errors.hpp 同款纪律）。
TEST(ArchiveContractTypes, ArtifactStatusEnumOrderPinned)
{
    static_assert(static_cast<std::uint8_t>(ArtifactStatus::Ok) == 0);
    static_assert(static_cast<std::uint8_t>(ArtifactStatus::IdempotentHit) == 1);
    static_assert(static_cast<std::uint8_t>(ArtifactStatus::Conflict) == 2);
    static_assert(static_cast<std::uint8_t>(ArtifactStatus::DiskFull) == 3);
    static_assert(static_cast<std::uint8_t>(ArtifactStatus::WriteRejected) == 4);
    static_assert(static_cast<std::uint8_t>(ArtifactStatus::ContextClosed) == 5);
    SUCCEED();
}

/// ReportEndReason 三值＝§9.6 abandon 注释原文（Completed/Canceled/Failed）。
TEST(ArchiveContractTypes, ReportEndReasonOrderPinned)
{
    static_assert(static_cast<std::uint8_t>(ReportEndReason::Completed) == 0);
    static_assert(static_cast<std::uint8_t>(ReportEndReason::Canceled) == 1);
    static_assert(static_cast<std::uint8_t>(ReportEndReason::Failed) == 2);
    EXPECT_STREQ(std::string(token(ReportEndReason::Canceled)).c_str(), "canceled");
}

/// 会话引用＝值句柄（跨单元值边界——非指针；0＝无效保留值）。
TEST(ArchiveContractTypes, SessionRefIsValueType)
{
    static_assert(std::is_trivially_copyable_v<ArtifactSessionRef>);
    ArtifactSessionRef ref;
    EXPECT_FALSE(ref.isValid());   // 默认＝无效（保留值纪律）
    ref.value = 7;
    EXPECT_TRUE(ref.isValid());
}

/// manifest 持久化相对路径＝§7.3 原文（report.json——唯一完整标志 D-13 的
/// 文件名）；工件相对命名决策（§14.4 v0.12 登记面）。
TEST(ArchiveContractTypes, ManifestAndArtifactRelPathsPinned)
{
    EXPECT_EQ(std::string(kManifestRelPath), "report.json");
    EXPECT_EQ(std::string(artifactRelPath(ReportRenderFormat::Html)), "report.html");
    EXPECT_EQ(std::string(artifactRelPath(ReportRenderFormat::Json)), "report-data.json");
    EXPECT_EQ(std::string(artifactRelPath(ReportRenderFormat::Csv)), "report.csv");
}

/// manifest 摘要类型＝core::ContentIdentity（P-RPT-9 基线——core 唯一摘要
/// 值类型，字节等值比较；跨单元类型恒等由 static_assert 常驻自证）。
TEST(ArchiveContractTypes, ManifestDigestIsCoreContentIdentity)
{
    static_assert(std::is_same_v<decltype(computeManifestDigest(
                                     std::declval<const ReportArtifactManifest&>())),
                                 co::ContentIdentity>);
    SUCCEED();
}

// =====================================================================
// P-RPT-1：公共头零 io 类型（源码扫描——Archive.hpp/Export.hpp 的 include
// 面不得出现 io/runtime/execution/业务域单元）
// =====================================================================

TEST(ArchiveContractHeaders, PublicHeadersZeroForbiddenIncludes)
{
    const std::filesystem::path root = std::filesystem::path(IRD_REPORTING_UNIT_ROOT)
                                       / "reporting" / "include" / "sdurws" / "ird"
                                       / "reporting";
    const std::vector<std::string> headers = {"Archive.hpp", "Export.hpp"};
    for (const std::string& name : headers) {
        std::ifstream file(root / name, std::ios::binary);
        ASSERT_TRUE(file.good()) << name;
        const std::string text((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
        // 表外编译边单元（ARCH §3.5 未登记——P-RPT-1/P-RPT-2/R-1）：include
        // 行命中即契约违约（L5 装配注入是唯一消费通道）。
        for (const std::string& unit :
             {"ird/io/", "ird/runtime/", "ird/execution/", "ird/modeling/",
              "ird/requirements/", "ird/kinematics/", "ird/trajectory/",
              "ird/dynamics/", "ird/selection/", "ird/optimization/", "ird/ui/"}) {
            for (const std::string& line : includeLines(text)) {
                EXPECT_TRUE(line.find(unit) == std::string::npos)
                    << name << " 引用了表外单元头: " << line;
            }
        }
        // 零 Qt（D-01 含 Core——比 L3 上限更严；只审 include 行——注释中的
        // "Qt" 词面不构成编译边）。
        for (const std::string& line : includeLines(text)) {
            EXPECT_TRUE(line.find("Qt") == std::string::npos) << name << ": " << line;
        }
    }
}

// =====================================================================
// sink 契约语义（fake 承载的 project 侧投影——acceptance 5"四方语义"面）
// =====================================================================

class SinkContractTest : public ::testing::Test {
protected:
    FakeArtifactSink sink_;

    /// 确定性项目身份（tryPublished/listPublished 与 begin/manifest 同键
    /// ——编址键含 projectId，全零与铺位值是不同键）。
    static co::ProjectId makeProject()
    {
        co::ProjectId p;
        for (std::size_t i = 0; i < p.bytes.size(); ++i) {
            p.bytes[i] = static_cast<std::uint8_t>(0x21 + i);
        }
        return p;
    }

    static ReportArtifactBeginRequest makeRequest(ReportId id)
    {
        ReportArtifactBeginRequest r;
        r.reportId = id;
        r.project = makeProject();
        r.contentIdentity = co::ContentIdentity{};
        for (std::size_t i = 0; i < r.contentIdentity.bytes.size(); ++i) {
            r.contentIdentity.bytes[i] = static_cast<std::uint8_t>(0x31 + i);
        }
        return r;
    }

    static ArtifactWrite makeWrite(const std::string& rel, const std::string& bytes)
    {
        ArtifactWrite w;
        w.relPath = rel;
        w.bytes.assign(bytes.begin(), bytes.end());
        return w;
    }

    static ReportArtifactManifest makeManifest(ReportId id)
    {
        ReportArtifactManifest m;
        m.reportId = id;
        m.project = makeProject();
        m.contentIdentity = co::ContentIdentity{};
        for (std::size_t i = 0; i < m.project.bytes.size(); ++i) {
            m.project.bytes[i] = static_cast<std::uint8_t>(0x21 + i);
        }
        m.contentIdentity = co::ContentIdentity{};
        for (std::size_t i = 0; i < m.contentIdentity.bytes.size(); ++i) {
            m.contentIdentity.bytes[i] = static_cast<std::uint8_t>(0x31 + i);
        }
        ArtifactManifestEntry e;
        e.relPath = "report.html";
        e.format = ReportRenderFormat::Html;
        e.sha256 = co::Digest256{};
        for (std::size_t i = 0; i < e.sha256.size(); ++i) {
            e.sha256[i] = static_cast<std::uint8_t>(0x41 + i);
        }
        e.sizeBytes = 5;
        e.rendererVersion = 1;
        e.templateVersion = 1;
        m.artifacts.push_back(e);
        return m;
    }
};

/// begin→Staged（目录已建）；finalize→Ok 提交（manifest 原子发布＝唯一
/// 完整标志——之后才出现在只读清单）；只读清单恰一条。
TEST_F(SinkContractTest, BeginStagedFinalizePublishesOnce)
{
    const ReportId id = fixtureReportId(0xB1);
    const ArtifactSessionRef session = sink_.begin(makeRequest(id));
    ASSERT_TRUE(session.isValid());
    EXPECT_EQ(sink_.activeSessions(), 1u);
    EXPECT_TRUE(sink_.tryPublished(makeProject(), id) == std::nullopt);   // 未 Finalized

    ReportArtifactManifest m = makeManifest(id);
    m.finalizedAtUtc = std::chrono::system_clock::now();
    m.manifestDigest = computeManifestDigest(m);
    ASSERT_EQ(sink_.writeArtifact(session, makeWrite("report.html", "abcde")),
              ArtifactStatus::Ok);
    EXPECT_EQ(sink_.finalize(session, m), ArtifactStatus::Ok);

    // Finalized 后：清单恰一条（"文件存在≠已发布"的正向面——发布事实入清单）。
    const std::optional<PublishedReportRecord> record = sink_.tryPublished(makeProject(), id);
    ASSERT_TRUE(record.has_value());
    EXPECT_TRUE(record->manifestDigest == m.manifestDigest);
    EXPECT_EQ(sink_.activeSessions(), 0u);
}

/// finalize 幂等（D-14 同构）：同摘要重投递＝IdempotentHit 且已发布字节
/// 零改写（替身以"字节表不变"承载零重写观测）。
TEST_F(SinkContractTest, FinalizeIdempotentHitKeepsPublishedBytes)
{
    const ReportId id = fixtureReportId(0xB2);

    ReportArtifactManifest m = makeManifest(id);
    m.finalizedAtUtc = std::chrono::system_clock::now();
    m.manifestDigest = computeManifestDigest(m);
    {
        const ArtifactSessionRef s = sink_.begin(makeRequest(id));
        ASSERT_EQ(sink_.writeArtifact(s, makeWrite("report.html", "原字节")), ArtifactStatus::Ok);
        ASSERT_EQ(sink_.finalize(s, m), ArtifactStatus::Ok);
    }
    const auto& rowBefore = sink_.published.begin()->second;

    // 第二次发布（同摘要——幂等重投递）。
    ReportArtifactManifest m2 = makeManifest(id);
    m2.manifestDigest = m.manifestDigest;   // 同内容同摘要（时间戳不入判定键）
    {
        const ArtifactSessionRef s = sink_.begin(makeRequest(id));
        ASSERT_EQ(sink_.writeArtifact(s, makeWrite("report.html", "原字节")),
                  ArtifactStatus::Ok);
        EXPECT_EQ(sink_.finalize(s, m2), ArtifactStatus::IdempotentHit);
    }
    const auto& rowAfter = sink_.published.begin()->second;
    // 零重写：已发布字节与发布时刻保持原值。
    EXPECT_TRUE(rowAfter.artifacts.at("report.html")
                == rowBefore.artifacts.at("report.html"));
    EXPECT_TRUE(rowAfter.record.publishedAtUtc == rowBefore.record.publishedAtUtc);
}

/// finalize 冲突（RPT-ARCHIVE-CONFLICT 承载）：同 reportId 不同摘要＝
/// Conflict 且既有内容不覆盖（§7.4 行 2）。
TEST_F(SinkContractTest, FinalizeConflictKeepsPublishedIntact)
{
    const ReportId id = fixtureReportId(0xB3);

    ReportArtifactManifest m = makeManifest(id);
    m.finalizedAtUtc = std::chrono::system_clock::now();
    m.manifestDigest = computeManifestDigest(m);
    {
        const ArtifactSessionRef s = sink_.begin(makeRequest(id));
        ASSERT_EQ(sink_.writeArtifact(s, makeWrite("report.html", "已发布内容")),
                  ArtifactStatus::Ok);
        ASSERT_EQ(sink_.finalize(s, m), ArtifactStatus::Ok);
    }

    // 不同内容（不同摘要）的发布请求——冲突拒绝。
    ReportArtifactManifest other = makeManifest(id);
    other.contentIdentity = co::ContentIdentity{};
    for (std::size_t i = 0; i < other.contentIdentity.bytes.size(); ++i) {
        other.contentIdentity.bytes[i] = static_cast<std::uint8_t>(0x61 + i);
    }
    other.manifestDigest = computeManifestDigest(other);
    {
        const ArtifactSessionRef s = sink_.begin(makeRequest(id));
        ASSERT_EQ(sink_.writeArtifact(s, makeWrite("report.html", "冲突内容")),
                  ArtifactStatus::Ok);
        EXPECT_EQ(sink_.finalize(s, other), ArtifactStatus::Conflict);
    }
    // 既有内容零触碰（字节逐位等于首次发布的原字节——"已发布内容" UTF-8
    // 共 15 字节，以 string 构造字节向量比对）。
    const std::string original = "已发布内容";
    const std::vector<std::uint8_t> originalBytes(original.begin(), original.end());
    EXPECT_TRUE(sink_.published.begin()->second.artifacts.at("report.html") == originalBytes);
}

/// write 只增幂等（§9.6 合法调用行"重试续写"）：同 relPath 同字节＝
/// IdempotentHit（跳过）；同 relPath 异字节＝Conflict（只增违约拒绝）。
TEST_F(SinkContractTest, WriteOnlyIncreaseSemantics)
{
    const ReportId id = fixtureReportId(0xB4);
    const ArtifactSessionRef s = sink_.begin(makeRequest(id));
    EXPECT_EQ(sink_.writeArtifact(s, makeWrite("report.html", "字节A")), ArtifactStatus::Ok);
    // 续写同内容：跳过（幂等）。
    EXPECT_EQ(sink_.writeArtifact(s, makeWrite("report.html", "字节A")),
              ArtifactStatus::IdempotentHit);
    // 同路径异字节：只增违约——冲突拒绝（不覆盖批次文件）。
    EXPECT_EQ(sink_.writeArtifact(s, makeWrite("report.html", "字节B")),
              ArtifactStatus::Conflict);
    sink_.abandon(s, ReportEndReason::Failed);
    EXPECT_EQ(sink_.activeSessions(), 0u);
}

/// abandon 责任终结（PM-08/§7.4 行 9）：未完成工件不入清单；清理失败注入
/// 不抛出、已发布报告零影响（残留仅计数——§10"可注入清理失败"）。
TEST_F(SinkContractTest, AbandonTerminatesWithoutTouchingPublished)
{
    const ReportId published = fixtureReportId(0xB5);
    ReportArtifactManifest m = makeManifest(published);
    m.finalizedAtUtc = std::chrono::system_clock::now();
    m.manifestDigest = computeManifestDigest(m);
    {
        const ArtifactSessionRef s = sink_.begin(makeRequest(published));
        ASSERT_EQ(sink_.writeArtifact(s, makeWrite("report.html", "x")), ArtifactStatus::Ok);
        ASSERT_EQ(sink_.finalize(s, m), ArtifactStatus::Ok);
    }

    // 清理失败注入下的 abandon（取消轨）。
    const ReportId unfinished = fixtureReportId(0xB6);
    sink_.cleanupFailure = true;
    const ArtifactSessionRef s = sink_.begin(makeRequest(unfinished));
    ASSERT_EQ(sink_.writeArtifact(s, makeWrite("report.html", "半截")), ArtifactStatus::Ok);
    sink_.abandon(s, ReportEndReason::Canceled);   // 不抛出——责任终结
    EXPECT_EQ(sink_.cleanupFailedCount, 1);        // 残留登记（诊断通道归 project 实现）

    // 未完成工件不入清单；已发布报告零影响。
    EXPECT_FALSE(sink_.tryPublished(makeProject(), unfinished).has_value());
    EXPECT_TRUE(sink_.tryPublished(makeProject(), published).has_value());
    EXPECT_EQ(sink_.listPublished(makeProject()).size(), 1u);
}

/// 只读存储上下文（PM-07——writable 检查归 project §6.8）：begin 抛
/// ReadOnlyStore（异常轨——§9.9"StoreError 透传→ReportError"投影）。
TEST_F(SinkContractTest, ReadOnlyContextThrowsReadOnlyStore)
{
    sink_.readOnly = true;
    const ReportId id = fixtureReportId(0xB7);
    EXPECT_THROW(static_cast<void>(sink_.begin(makeRequest(id))), ReportError);
    try {
        static_cast<void>(sink_.begin(makeRequest(id)));
    } catch (const ReportError& e) {
        EXPECT_EQ(e.code(), ReportErrorCode::ReadOnlyStore);
    }
}

// =====================================================================
// 协调器编排契约（reporting 侧对 sink 的消费序——acceptance 5"编排顺序
// begin→write×N→finalize、任一非 Ok→abandon"）
// =====================================================================
// 协调器编排契约（reporting 侧对 sink 的消费序——acceptance 5"编排顺序
// begin→write×N→finalize、任一非 Ok→abandon"；编排序的行为断言在单元
// 测试 CoordinatorTest 以调用日志承载，此处钉跨单元稳定的码面）
// =====================================================================

/// 状态↔错误码映射契约（§9.5 错误行——协调器/服务把 sink 状态转译为
/// ReportErrorCode 的稳定面）：DiskFull→DiskFull、Conflict→ArchiveConflict、
/// WriteRejected/ContextClosed→ExportFailed（与 Archive.cpp 实现一致）。
TEST(CoordinatorContract, StatusToErrorCodeMappingStable)
{
    // 该映射的消费面在服务层结果（ReportExportResult.error.code()）——
    // 契约以 token 稳定性钉住（token 表与 §3.5 清单一字不差由
    // ErrorsIdentityTest 全表用例钉住；此处钉导出链涉及的四码存在性与序）。
    EXPECT_EQ(std::string(token(ReportErrorCode::DiskFull)), "reporting/disk-full");
    EXPECT_EQ(std::string(token(ReportErrorCode::ArchiveConflict)),
              "reporting/archive-conflict");
    EXPECT_EQ(std::string(token(ReportErrorCode::ExportFailed)), "reporting/export-failed");
    EXPECT_EQ(std::string(token(ReportErrorCode::ReadOnlyStore)), "reporting/read-only-store");
}

}  // namespace
