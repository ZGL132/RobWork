/**
 * @file   CheckpointCacheTest.cpp
 * @brief  检查点与缓存存储治理用例组（EX-T08）——EX-CKP-1~3（损坏拒绝
 *         且原文件保留/版本不兼容＋reasons/恢复失败原检查点完整保留）、
 *         写出三段与失败路径、EX-CCH-1/2 的执行侧门槛与调度边界、D-10
 *         同键在途合并、LRU 分账淘汰（§8.1/§8.2/§10.6/§10.7）。
 *
 * 设计依据：
 *   - units/execution.md §8.1（检查点契约字段表＋损坏/不兼容/恢复失败
 *     保留规则）、§8.2（四类区分＋治理规则表）、§10.6/§10.7（接口契约）、
 *     §11（EX-CKP-1/2/3、EX-CCH-1/2 用例行——观测点：诊断/目录内容不变/
 *     磁盘断言/CacheLookupResult.reasons/缓存清单断言）、§15.1（D-10 登记的
 *     实现落点）
 *   - 需求 CON-04（部分/失败结果不作正式命中；命中≠当前——CacheLookup
 *     无当前性字段的结构性表达在用例注释中逐处钉住）、CON-05（缓存键
 *     内容身份驱动）、OPT-06（缓存键绑定 sliceId/inputBaselineId）
 *   - 任务契约 tasks/foundation/EX-T08.json acceptance 1~5
 *
 * 替身边界声明（§11 同源纪律）：
 *   - 归档写面为**写真实临时文件的端口替身**（FakeArchiveDisk——§11
 *     "project 归档端口 fake"明文形态）：ArchiveSessionRef 只能由 project
 *     内部实现构造（值语义凭据防伪——EX-T04 套件同款登记），替身以默认
 *     空会话句柄作自持令牌；文件落盘为真实磁盘断言（EX-CKP-1/3 的
 *     "目录内容不变"观测面）。替身不证明 project 端口行为本身——那归
 *     EX-T04/PRJ-T14 套件；
 *   - 判定面零替身：restore/lookup 内的兼容性结论全部来自真实
 *     evidence::judgeCheckpointCompatibility/judgeCacheHit（已登记边的
 *     真实产品路径——EV-REG-3 模式）；ICompileCacheJudge 为脚本替身
 *     （判定器归 L5 注入——其真实适配器行为归 runtime/EV 侧套件）；
 *   - 登记表为真实 RunRegistry（storeResult 双门槛的镜像数据源）。
 *
 * 时钟纪律（testkit §6.5）：时刻/LRU 断言经注入固定时钟，不 sleep。
 */

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/evidence/Compatibility.hpp>
#include <sdurws/ird/execution/CacheCoordinator.hpp>
#include <sdurws/ird/execution/Checkpoint.hpp>
#include <sdurws/ird/execution/Errors.hpp>
#include <sdurws/ird/execution/Ports.hpp>
#include <sdurws/ird/execution/RunRegistry.hpp>
#include <sdurws/ird/project/ArchivePort.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {

using namespace sdurws::ird::execution;
namespace core = sdurws::ird::core;
namespace ev = sdurws::ird::evidence;
namespace pd = sdurws::ird::project;
namespace fs = std::filesystem;

// =====================================================================
// 身份辅助（RunRegistryAdmissionTest 同款确定性固定值——自持不共享）
// =====================================================================

const char* kHex64A = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
const char* kHex64B = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
const char* kHex64C = "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";
const char* kHex64D = "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd";

core::ContentIdentity cid(const char* hex64)
{
    return core::ContentIdentity::fromCanonical(std::string{"cid-"} + hex64);
}

/// 固定五元组（分支/修订生成值——检查点/缓存面不校验修订闭包）。
core::TaskIdentity makeIdentity()
{
    core::TaskIdentity id;
    id.project = core::ProjectId::generate();
    id.branch = core::BranchId::generate();
    id.revision = core::RevisionId::generate();
    id.run = core::RunId::generate();
    id.attempt = core::AttemptId{1};
    return id;
}

/// 固定时刻（2026-01-01T00:00:00Z 的 epoch 秒——LRU 断言确定性）。
std::chrono::system_clock::time_point fixedNow()
{
    return std::chrono::system_clock::time_point{std::chrono::seconds{1000}};
}

/// 诊断收集替身（稳定码＋开发双通道——EX-CKP 观测点"诊断"的断言面）。
class CollectingSink final : public IExecutionDiagnosticsSink {
public:
    std::vector<core::DiagnosticRecord> reports;
    std::vector<std::pair<std::string, std::string>> devs;

    void report(const core::DiagnosticRecord& record) override { reports.push_back(record); }
    void reportDev(const std::string& channel, const std::string& message) override
    {
        devs.emplace_back(channel, message);
    }
    bool hasReportCode(const std::string& code) const
    {
        for (const auto& r : reports) {
            if (r.code == code) {
                return true;
            }
        }
        return false;
    }
    bool hasDevChannel(const std::string& channel) const
    {
        for (const auto& d : devs) {
            if (d.first == channel) {
                return true;
            }
        }
        return false;
    }
};

// =====================================================================
// 第一部分：检查点协调器（EX-CKP-1~3）
// =====================================================================

/**
 * @brief 归档端口替身：begin 记录请求、writeBatch 把批次字节写进真实
 *        磁盘 runDir、finalize 写 manifest.json 标记"正式"。故障注入旗标
 *        支撑 ArchiveFailed 路径（见文件头替身边界声明）。
 */
class FakeArchiveDisk final : public pd::IResultArchivePort {
public:
    // ---- 故障注入旗标 ----
    bool failBeginWithStoreError = false; ///< begin 抛 StoreError（ContextClosed）
    bool failWriteBatch = false;          ///< writeBatch 返回 disk-full
    bool failFinalize = false;            ///< finalize 返回 disk-full

    // ---- 观测面 ----
    int beginCalls = 0;
    std::vector<pd::ArchiveEndReason> abandons;
    pd::RunManifest lastManifest;

    pd::ArchiveSessionRef begin(const pd::ArchiveRequest& request) override
    {
        ++beginCalls;
        if (failBeginWithStoreError) {
            throw pd::StoreError{pd::StoreErrorCode::ContextClosed,
                                 "fake: 注入上下文关闭"};
        }
        m_openDirs.push_back(request.runDir);
        // 默认空句柄＝本替身的自持令牌（ArchiveSessionRef 构造归 project
        // 内部——替身不铸造真会话，协调器将其作不透明凭据原样回传）。
        return pd::ArchiveSessionRef{};
    }

    pd::ArchiveStatus writeBatch(pd::ArchiveSessionRef session,
                                 const pd::ArchiveBatch& batch) override
    {
        (void)session;
        if (failWriteBatch) {
            return pd::ArchiveStatus{false, pd::StoreError{pd::StoreErrorCode::DiskFull,
                                                           "fake: 注入 disk-full"}};
        }
        // 写入最近一次 begin 的目录（单会话用例形态——协调器一次只持一
        // 个在途会话）。
        const fs::path dir = m_openDirs.back();
        std::error_code ec;
        fs::create_directories(dir, ec);
        if (ec) {
            return pd::ArchiveStatus{false, pd::StoreError{pd::StoreErrorCode::DiskFull,
                                                           "fake: 创建目录失败"}};
        }
        for (const pd::ArchiveItem& item : batch.items) {
            std::ofstream out(dir / item.relPath, std::ios::binary);
            out.write(reinterpret_cast<const char*>(item.bytes.data()),
                      static_cast<std::streamsize>(item.bytes.size()));
            if (!out) {
                return pd::ArchiveStatus{false,
                                         pd::StoreError{pd::StoreErrorCode::DiskFull,
                                                        "fake: 写文件失败"}};
            }
        }
        return pd::ArchiveStatus{true, std::nullopt};
    }

    pd::ArchiveStatus finalize(pd::ArchiveSessionRef session,
                               const pd::RunManifest& manifest) override
    {
        (void)session;
        if (failFinalize) {
            return pd::ArchiveStatus{false, pd::StoreError{pd::StoreErrorCode::DiskFull,
                                                           "fake: 注入 disk-full"}};
        }
        // manifest.json 原子发布的最小替身（内容非契约面——存在性即
        // "正式"标记；EX-CKP 用例只篡改批次文件，不动 manifest）。
        const fs::path dir = m_openDirs.back();
        std::ofstream out(dir / "manifest.json", std::ios::binary);
        out << "{\"fake\":true}";
        lastManifest = manifest;
        return pd::ArchiveStatus{true, std::nullopt};
    }

    void abandon(pd::ArchiveSessionRef session, pd::ArchiveEndReason reason) override
    {
        (void)session;
        abandons.push_back(reason);
    }

private:
    std::vector<fs::path> m_openDirs;
};

class CheckpointCoordinatorTest : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        std::error_code ec;
#ifdef _WIN32
        s_base = fs::temp_directory_path(ec) / "ird_ex_checkpoint_test"
                 / std::to_string(::GetCurrentProcessId());
#else
        s_base = fs::temp_directory_path(ec) / "ird_ex_checkpoint_test";
#endif
        ASSERT_FALSE(ec);
        fs::create_directories(s_base, ec);
        if (ec) {
            std::exit(2);
        }
    }

    static void TearDownTestSuite()
    {
        // 失败保留现场（TempDir 失败保留惯例）；成功清理。
        std::error_code ec;
        fs::remove_all(s_base, ec);
    }

    void SetUp() override
    {
        m_caseDir = s_base / ("case" + std::to_string(++s_caseCounter));
        fs::create_directories(m_caseDir);
        m_port = std::make_unique<FakeArchiveDisk>();
        m_sink = std::make_unique<CollectingSink>();
        m_identity = makeIdentity();
        m_coordinator = std::make_unique<CheckpointCoordinator>(*m_port, *m_sink,
                                                                fixedNow);
    }

    /// 构造一份合法写出请求（两批次文件；身份/绑定/统计齐备）。
    CheckpointWriteRequest makeWriteRequest(std::uint64_t seq = 1, bool resumable = true)
    {
        CheckpointWriteRequest req;
        req.record.checkpointId = CheckpointId{m_identity.run, seq};
        req.record.task = m_identity;
        req.record.snapshotId = cid(kHex64A);
        req.record.sliceId = cid(kHex64C);
        req.record.evaluatorKey = "kin-batch-ik";
        req.record.evaluatorContractVersion = 7;
        req.record.policyIdentity = cid(kHex64B);
        req.record.nameMapIdentity = cid(kHex64D);
        req.record.randomSeed = 42;
        req.record.threadCount = 4;
        req.record.completedUnits = 30;
        req.record.totalUnits = 100;
        req.record.resumable = resumable;
        CheckpointBatchFile f1;
        f1.relPath = "part-000.bin";
        f1.bytes = {0x01, 0x02, 0x03, 0x04, 0x05};
        CheckpointBatchFile f2;
        f2.relPath = "part-001.bin";
        f2.bytes = {0xF0, 0xF1, 0xF2};
        req.files.push_back(std::move(f1));
        req.files.push_back(std::move(f2));
        req.archiveDir = m_caseDir / "checkpoints" / m_identity.run.toCanonical()
                         / std::to_string(seq);
        return req;
    }

    /// 目录快照（relPath→字节——EX-CKP-1/3"目录内容不变"的比对底座）。
    std::map<std::string, std::string> snapshotDir(const fs::path& dir) const
    {
        std::map<std::string, std::string> out;
        std::error_code ec;
        for (const auto& entry : fs::recursive_directory_iterator(dir, ec)) {
            if (entry.is_regular_file(ec)) {
                const auto rel = fs::relative(entry.path(), dir, ec);
                std::ifstream in(entry.path(), std::ios::binary);
                std::string bytes((std::istreambuf_iterator<char>(in)),
                                  std::istreambuf_iterator<char>());
                out[rel.generic_string()] = std::move(bytes);
            }
        }
        return out;
    }

    static fs::path s_base;
    static int s_caseCounter;
    fs::path m_caseDir;
    std::unique_ptr<FakeArchiveDisk> m_port;
    std::unique_ptr<CollectingSink> m_sink;
    core::TaskIdentity m_identity;
    std::unique_ptr<CheckpointCoordinator> m_coordinator;
};

fs::path CheckpointCoordinatorTest::s_base;
int CheckpointCoordinatorTest::s_caseCounter = 0;

// ---------------------------------------------------------------------
// 写出路径：发布→记录补全→manifest 面正确
// ---------------------------------------------------------------------

/// 正常写出：Published＋记录载荷面以实际批次重算＋manifest 携带批次清单。
TEST_F(CheckpointCoordinatorTest, WritePublishesRecordAndManifest)
{
    auto request = makeWriteRequest();
    // 预填伪造载荷面——写出必须以实际批次重算覆盖（防伪造面）。
    request.record.intermediateStateRef.push_back(CheckpointFileRef{"fake.bin", {}, 1});
    request.record.payloadDigest = cid(kHex64A).bytes;

    const CheckpointWriteResult result = m_coordinator->writeCheckpoint(request);

    ASSERT_EQ(result.outcome, CheckpointWriteResult::Outcome::Published);
    // manifest 完整性清单＝两批次文件（relPath/sizeBytes 与请求一致，
    // sha256 为小写 64 hex——project §4.4.7 token 面）。
    ASSERT_EQ(m_port->lastManifest.items.size(), 2u);
    EXPECT_EQ(m_port->lastManifest.items[0].relPath, "part-000.bin");
    EXPECT_EQ(m_port->lastManifest.items[0].sizeBytes, 5u);
    EXPECT_EQ(m_port->lastManifest.items[0].sha256.size(), 64u);
    EXPECT_EQ(m_port->lastManifest.taskIdentity, m_identity);
    EXPECT_EQ(m_port->lastManifest.runKind, "checkpoint");
    EXPECT_EQ(m_port->lastManifest.evaluationKey, "kin-batch-ik");
    // 索引就位（恢复调度的查找基准）＋磁盘三文件（2 批次＋manifest）。
    EXPECT_EQ(m_coordinator->indexedCount(), 1u);
    EXPECT_TRUE(fs::exists(request.archiveDir / "manifest.json"));
    EXPECT_TRUE(fs::exists(request.archiveDir / "part-000.bin"));
}

/// 复合摘要确定性：同批次同摘要、与文件交序无关（NFR-COR-02——写/读两
/// 侧同一函数的确定性前提）。
TEST_F(CheckpointCoordinatorTest, PayloadDigestDeterministicAcrossOrder)
{
    CheckpointBatchFile a;
    a.relPath = "a.bin";
    a.bytes = {0x10, 0x20};
    CheckpointBatchFile b;
    b.relPath = "b.bin";
    b.bytes = {0x30};
    std::vector<CheckpointBatchFile> files;
    files.push_back(a);
    files.push_back(b);
    const core::Digest256 d1 = computePayloadDigest(files);
    std::vector<CheckpointBatchFile> reversed;
    reversed.push_back(b);
    reversed.push_back(a);
    EXPECT_TRUE(computePayloadDigest(reversed) == d1);
    // 不同内容必得不同摘要（损坏检测灵敏度的最小反例）。
    CheckpointBatchFile c = a;
    c.bytes = {0x10, 0x21};
    std::vector<CheckpointBatchFile> files2;
    files2.push_back(c);
    files2.push_back(b);
    EXPECT_FALSE(computePayloadDigest(files2) == d1);
}

/// 信封版本戳：请求携带任意版本值都被当前版本覆盖（§8.1 所有权列
/// "execution（信封）"——P-EX-9"无迁移"的写出面：本实现写出的永远是
/// 当前版本；旧版本的判定拒绝归 evidence 判定单点，acceptance 2）。
TEST_F(CheckpointCoordinatorTest, WriteStampsCurrentEnvelopeVersion)
{
    auto request = makeWriteRequest();
    request.record.checkpointFormatVersion = 99;
    request.record.recordVersion = 99;
    const CheckpointWriteResult result = m_coordinator->writeCheckpoint(request);
    ASSERT_EQ(result.outcome, CheckpointWriteResult::Outcome::Published);
    // 版本随会话索引记录，经 listCompatible 的判定摘要可观测（＝当前
    // 版本 1，落在 evidence 支持区间内——区间外拒绝由 evidence 判定面
    // 承担，本单元无迁移代码路径【结构面】）。
    ResumeQuery query;
    query.requestSliceId = cid(kHex64C);
    query.requestContractVersion = 7;
    const auto candidates = m_coordinator->listCompatible(query);
    ASSERT_EQ(candidates.size(), 1u);
    EXPECT_EQ(candidates[0].summary.checkpointFormatVersion, 1u);
}

/// 调用方契约违约 fail-fast：无效记录/空批次/空目录/重发布同主键。
TEST_F(CheckpointCoordinatorTest, WriteContractViolationsFailFast)
{
    // 无效记录（sliceId 为保留值）。
    auto bad = makeWriteRequest();
    bad.record.sliceId = core::ContentIdentity{};
    EXPECT_THROW((void)m_coordinator->writeCheckpoint(bad), ExecutionError);
    // 空批次。
    auto empty = makeWriteRequest();
    empty.files.clear();
    EXPECT_THROW((void)m_coordinator->writeCheckpoint(empty), ExecutionError);
    // 空归档位置。
    auto nodir = makeWriteRequest();
    nodir.archiveDir.clear();
    EXPECT_THROW((void)m_coordinator->writeCheckpoint(nodir), ExecutionError);
    // 重发布同 (run,seq)＝编排协议错误（正式检查点不可重写）。
    auto first = makeWriteRequest();
    ASSERT_EQ(m_coordinator->writeCheckpoint(first).outcome,
              CheckpointWriteResult::Outcome::Published);
    EXPECT_THROW((void)m_coordinator->writeCheckpoint(first), ExecutionError);
}

/// 环境类失败（begin StoreError/写失败/发布失败）→结构化 ArchiveFailed
/// ＋EX-ARCHIVE-FAILED＋abandon 结束责任（不抛——AGENTS §3 可预期侧）。
TEST_F(CheckpointCoordinatorTest, WriteEnvironmentFailuresAreStructured)
{
    // begin 失败（上下文关闭注入）。
    m_port->failBeginWithStoreError = true;
    const auto r1 = m_coordinator->writeCheckpoint(makeWriteRequest());
    EXPECT_EQ(r1.outcome, CheckpointWriteResult::Outcome::ArchiveFailed);
    EXPECT_TRUE(m_sink->hasReportCode("EX-ARCHIVE-FAILED"));
    m_port->failBeginWithStoreError = false;

    // 批次写入失败→abandon（Failed）＋无 manifest 残留（CON-04 未完成）。
    m_port->failWriteBatch = true;
    const auto r2 = m_coordinator->writeCheckpoint(makeWriteRequest(2));
    EXPECT_EQ(r2.outcome, CheckpointWriteResult::Outcome::ArchiveFailed);
    ASSERT_EQ(m_port->abandons.size(), 1u);
    EXPECT_EQ(m_port->abandons[0], pd::ArchiveEndReason::Failed);
    EXPECT_FALSE(fs::exists(m_caseDir / "checkpoints" / m_identity.run.toCanonical()
                            / "2" / "manifest.json"));
    m_port->failWriteBatch = false;

    // finalize 失败→abandon＋失败不落索引。
    m_port->failFinalize = true;
    const auto r3 = m_coordinator->writeCheckpoint(makeWriteRequest(3));
    EXPECT_EQ(r3.outcome, CheckpointWriteResult::Outcome::ArchiveFailed);
    EXPECT_EQ(m_coordinator->indexedCount(), 0u);
    m_port->failFinalize = false;

    // 三次失败后同主键可重写（未发布＝无索引——临时/在途窗口语义）。
    const auto r4 = m_coordinator->writeCheckpoint(makeWriteRequest(3));
    EXPECT_EQ(r4.outcome, CheckpointWriteResult::Outcome::Published);
}

// ---------------------------------------------------------------------
// EX-CKP-1：篡改一字节→EX-CHECKPOINT-CORRUPT 拒绝恢复且原文件保留
// ---------------------------------------------------------------------

TEST_F(CheckpointCoordinatorTest, EX_CKP_1_TamperedByteRejectedAndFilePreserved)
{
    auto request = makeWriteRequest();
    ASSERT_EQ(m_coordinator->writeCheckpoint(request).outcome,
              CheckpointWriteResult::Outcome::Published);

    // 篡改批次文件一字节（翻转最低位——真实的单字节损坏注入）。
    const fs::path victim = request.archiveDir / "part-000.bin";
    {
        std::fstream file(victim, std::ios::binary | std::ios::in | std::ios::out);
        ASSERT_TRUE(file.is_open());
        char byte = 0;
        file.seekg(1);
        file.read(&byte, 1);
        file.seekp(1);
        file.put(static_cast<char>(byte ^ 0x01));
    }

    // 篡改后目录快照（"原文件保留"的比对基准——含被篡改文件本身）。
    const auto before = snapshotDir(request.archiveDir);

    const CheckpointRestoreResult result = m_coordinator->restore(
        CheckpointRestoreRequest{CheckpointId{m_identity.run, 1}, cid(kHex64C), 7});

    // 拒绝恢复＋稳定码诊断（观测点：诊断）。
    EXPECT_EQ(result.outcome, CheckpointRestoreResult::Outcome::Corrupt);
    EXPECT_TRUE(m_sink->hasReportCode("EX-CHECKPOINT-CORRUPT"));
    EXPECT_FALSE(result.diagnostics.empty());
    // 目录内容逐字节不变（观测点：目录内容不变——不删除、不修复、
    // 无废弃落盘；废弃标记只在会话内存）。
    EXPECT_EQ(snapshotDir(request.archiveDir), before);
    // 损坏检查点废弃（会话内标记）——重复恢复走 Discarded，不再装载。
    const CheckpointRestoreResult again = m_coordinator->restore(
        CheckpointRestoreRequest{CheckpointId{m_identity.run, 1}, cid(kHex64C), 7});
    EXPECT_EQ(again.outcome, CheckpointRestoreResult::Outcome::Discarded);
    EXPECT_EQ(snapshotDir(request.archiveDir), before);
    // 废弃项不出兼容候选。
    ResumeQuery query;
    query.requestSliceId = cid(kHex64C);
    query.requestContractVersion = 7;
    EXPECT_TRUE(m_coordinator->listCompatible(query).empty());
}

/// 装载失败（批次文件被删）同走 Corrupt——"目录内容不变"结构性成立。
TEST_F(CheckpointCoordinatorTest, MissingBatchFileTreatedAsCorrupt)
{
    auto request = makeWriteRequest();
    ASSERT_EQ(m_coordinator->writeCheckpoint(request).outcome,
              CheckpointWriteResult::Outcome::Published);
    std::error_code ec;
    fs::remove(request.archiveDir / "part-001.bin", ec);
    const auto before = snapshotDir(request.archiveDir);

    const CheckpointRestoreResult result = m_coordinator->restore(
        CheckpointRestoreRequest{CheckpointId{m_identity.run, 1}, cid(kHex64C), 7});
    EXPECT_EQ(result.outcome, CheckpointRestoreResult::Outcome::Corrupt);
    EXPECT_TRUE(m_sink->hasReportCode("EX-CHECKPOINT-CORRUPT"));
    EXPECT_EQ(snapshotDir(request.archiveDir), before);
}

// ---------------------------------------------------------------------
// EX-CKP-2：契约版本失配→EX-CHECKPOINT-INCOMPATIBLE＋reasons（判定来自
// evidence——token 词表可溯源；P-EX-9 不迁移的拒绝面）
// ---------------------------------------------------------------------

TEST_F(CheckpointCoordinatorTest, EX_CKP_2_VersionMismatchIncompatibleWithReasons)
{
    auto request = makeWriteRequest();   // 记录契约版本 7
    ASSERT_EQ(m_coordinator->writeCheckpoint(request).outcome,
              CheckpointWriteResult::Outcome::Published);
    const auto before = snapshotDir(request.archiveDir);

    // 请求契约版本 8——judgeCheckpointCompatibility 的第三条件失配。
    const CheckpointRestoreResult result = m_coordinator->restore(
        CheckpointRestoreRequest{CheckpointId{m_identity.run, 1}, cid(kHex64C), 8});

    EXPECT_EQ(result.outcome, CheckpointRestoreResult::Outcome::Incompatible);
    // reasons＝evidence 稳定 token 词表（"contract-mismatch"——token 只能
    // 产生自 evidence 判定单点，本单元零复制的溯源面）。
    ASSERT_EQ(result.reasons.size(), 1u);
    EXPECT_EQ(result.reasons[0], "contract-mismatch");
    EXPECT_TRUE(m_sink->hasReportCode("EX-CHECKPOINT-INCOMPATIBLE"));
    // 不迁移：原检查点完整保留＋索引仍在（可再试或换请求面）。
    EXPECT_EQ(snapshotDir(request.archiveDir), before);
    EXPECT_EQ(m_coordinator->indexedCount(), 1u);

    // 同版本重试恢复成功（对照——拒绝面只对失配请求生效）。
    const CheckpointRestoreResult ok = m_coordinator->restore(
        CheckpointRestoreRequest{CheckpointId{m_identity.run, 1}, cid(kHex64C), 7});
    EXPECT_EQ(ok.outcome, CheckpointRestoreResult::Outcome::Restored);
}

/// 切片身份失配同走 Incompatible（"slice-mismatch" token——输入变了就
/// 得从头跑，续跑旧输入毫无意义）。
TEST_F(CheckpointCoordinatorTest, SliceMismatchIncompatible)
{
    auto request = makeWriteRequest();
    ASSERT_EQ(m_coordinator->writeCheckpoint(request).outcome,
              CheckpointWriteResult::Outcome::Published);

    const CheckpointRestoreResult result = m_coordinator->restore(
        CheckpointRestoreRequest{CheckpointId{m_identity.run, 1}, cid(kHex64B), 7});
    EXPECT_EQ(result.outcome, CheckpointRestoreResult::Outcome::Incompatible);
    ASSERT_EQ(result.reasons.size(), 1u);
    EXPECT_EQ(result.reasons[0], "slice-mismatch");
    EXPECT_TRUE(m_sink->hasReportCode("EX-CHECKPOINT-INCOMPATIBLE"));
}

// ---------------------------------------------------------------------
// EX-CKP-3：恢复失败（域申报不可续）→原检查点完整保留（磁盘断言）
// ---------------------------------------------------------------------

TEST_F(CheckpointCoordinatorTest, EX_CKP_3_NotResumableRefusedAndPreserved)
{
    auto request = makeWriteRequest(1, /*resumable=*/false);
    ASSERT_EQ(m_coordinator->writeCheckpoint(request).outcome,
              CheckpointWriteResult::Outcome::Published);
    const auto before = snapshotDir(request.archiveDir);

    // 判定面全绿（同切片同契约版本）但域申报不可续——仅供诊断。
    const CheckpointRestoreResult result = m_coordinator->restore(
        CheckpointRestoreRequest{CheckpointId{m_identity.run, 1}, cid(kHex64C), 7});
    EXPECT_EQ(result.outcome, CheckpointRestoreResult::Outcome::NotResumable);
    // 原检查点完整保留（磁盘断言——不删除、不降级；任务可再试或全量重跑）。
    EXPECT_EQ(snapshotDir(request.archiveDir), before);
    EXPECT_EQ(m_coordinator->indexedCount(), 1u);
    // 未标废弃：候选仍在（与损坏废弃的区别面——不可续不剥夺可诊断性）。
    ResumeQuery query;
    query.requestSliceId = cid(kHex64C);
    query.requestContractVersion = 7;
    EXPECT_EQ(m_coordinator->listCompatible(query).size(), 1u);
}

// ---------------------------------------------------------------------
// listCompatible / discard / 未知主键
// ---------------------------------------------------------------------

TEST_F(CheckpointCoordinatorTest, ListCompatibleFiltersAndOrders)
{
    auto r1 = makeWriteRequest(1);
    ASSERT_EQ(m_coordinator->writeCheckpoint(r1).outcome,
              CheckpointWriteResult::Outcome::Published);
    // 第二个运行的同键检查点（跨运行查询面）。
    const core::TaskIdentity other = makeIdentity();
    CheckpointWriteRequest r2 = makeWriteRequest(1);
    r2.record.checkpointId = CheckpointId{other.run, 1};
    r2.record.task = other;
    r2.archiveDir = m_caseDir / "checkpoints" / other.run.toCanonical() / "1";
    ASSERT_EQ(m_coordinator->writeCheckpoint(r2).outcome,
              CheckpointWriteResult::Outcome::Published);

    // 跨运行查询：两候选，主键字典序确定性排列（NFR-COR-02）。
    ResumeQuery all;
    all.requestSliceId = cid(kHex64C);
    all.requestContractVersion = 7;
    const auto candidates = m_coordinator->listCompatible(all);
    ASSERT_EQ(candidates.size(), 2u);
    EXPECT_TRUE(candidates[0].id < candidates[1].id);

    // 运行过滤：只回本运行候选。
    ResumeQuery scoped;
    scoped.run = m_identity.run;
    scoped.requestSliceId = cid(kHex64C);
    scoped.requestContractVersion = 7;
    ASSERT_EQ(m_coordinator->listCompatible(scoped).size(), 1u);
    EXPECT_EQ(m_coordinator->listCompatible(scoped)[0].id.run, m_identity.run);

    // 判定面失配（契约版本）→零候选（清单面与 restore 同一 evidence 判定）。
    ResumeQuery stale;
    stale.requestSliceId = cid(kHex64C);
    stale.requestContractVersion = 8;
    EXPECT_TRUE(m_coordinator->listCompatible(stale).empty());

    // 显式废弃：出候选＋恢复拒绝（磁盘不动——阶段 A 不做 GC，§10.6）。
    m_coordinator->discard(CheckpointId{m_identity.run, 1});
    EXPECT_EQ(m_coordinator->listCompatible(scoped).size(), 0u);
    const auto before = snapshotDir(r1.archiveDir);
    const CheckpointRestoreResult refused = m_coordinator->restore(
        CheckpointRestoreRequest{CheckpointId{m_identity.run, 1}, cid(kHex64C), 7});
    EXPECT_EQ(refused.outcome, CheckpointRestoreResult::Outcome::Discarded);
    EXPECT_EQ(snapshotDir(r1.archiveDir), before);
}

TEST_F(CheckpointCoordinatorTest, UnknownCheckpointAndContractViolations)
{
    // 未知主键＝结构化 UnknownCheckpoint（跨会话重建归恢复扫描，§5.4）。
    const CheckpointRestoreResult unknown = m_coordinator->restore(
        CheckpointRestoreRequest{CheckpointId{m_identity.run, 9}, cid(kHex64C), 7});
    EXPECT_EQ(unknown.outcome, CheckpointRestoreResult::Outcome::UnknownCheckpoint);
    // 主键无效＝调用方契约违约 fail-fast。
    CheckpointRestoreRequest invalid;
    invalid.checkpointId = CheckpointId{core::RunId{}, 0};
    EXPECT_THROW((void)m_coordinator->restore(invalid), ExecutionError);
    EXPECT_THROW(m_coordinator->discard(CheckpointId{core::RunId{}, 0}), ExecutionError);
}

// =====================================================================
// 第二部分：执行缓存协调器（EX-CCH-1/2、D-10、淘汰）
// =====================================================================

// ---- 登记底座替身（registerRun 的资源上下文须非空——最小桩）----

class NoopProducers final : public ev::IProducerRegistryView {
public:
    bool isRegistered(std::string_view) const override { return false; }
    bool contractVersionMatches(std::string_view, std::uint32_t) const override
    {
        return false;
    }
};

class NoopProfiles final : public ev::IProfileRegistryView {
public:
    const ev::RequiredEvidenceProfile* findProfile(std::string_view,
                                                   std::string_view) const override
    {
        return nullptr;
    }
};

/// 网关桩（storeResult 路径不触碰归档端口——登记面要求非空而已）。
class NullGateway final : public IRunArchiveGateway {
public:
    pd::IResultArchivePort& port() const override
    {
        static NullArchivePort port;
        return port;
    }
    bool reacquireWriteAuthority() override { return false; }

private:
    class NullArchivePort final : public pd::IResultArchivePort {
    public:
        pd::ArchiveSessionRef begin(const pd::ArchiveRequest&) override
        {
            throw std::runtime_error("null gateway port 不应被调用");
        }
        pd::ArchiveStatus writeBatch(pd::ArchiveSessionRef, const pd::ArchiveBatch&) override
        {
            return {false, std::nullopt};
        }
        pd::ArchiveStatus finalize(pd::ArchiveSessionRef, const pd::RunManifest&) override
        {
            return {false, std::nullopt};
        }
        void abandon(pd::ArchiveSessionRef, pd::ArchiveEndReason) override {}
    };
};

/// 编译缓存判定脚本替身（记录判定入参——注入边界的透传断言面；判定
/// 规则归 runtime，其真实行为验证归 RT 侧套件；本替身只钉住"协调器把
/// 请求键与登记键原样交判定器、verdict 原样回"的透传契约）。
class ScriptedCompileJudge final : public ICompileCacheJudge {
public:
    struct Call {
        CompileCacheKeyView requested;
        CompileCacheKeyView cached;
    };
    mutable std::vector<Call> calls;
    mutable std::vector<CompileCacheVerdict> script;

    CompileCacheVerdict judge(const CompileCacheKeyView& requested,
                              const CompileCacheKeyView& cached) const override
    {
        calls.push_back(Call{requested, cached});
        if (script.empty()) {
            return CompileCacheVerdict::Incompatible;
        }
        const CompileCacheVerdict answer = script.front();
        script.erase(script.begin());
        return answer;
    }
};

class CacheCoordinatorTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_sink = std::make_unique<CollectingSink>();
        m_registry = std::make_unique<RunRegistry>(fixedNow);
        m_producers = std::make_shared<NoopProducers>();
        m_profiles = std::make_shared<NoopProfiles>();
        m_coordinator = std::make_unique<ExecutionCacheCoordinator>(*m_registry, *m_sink,
                                                                    fixedNow);
    }

    /// 登记一份运行（可指定模式/切片/契约/Profile/基准），返回五元组。
    core::TaskIdentity registerRun(core::EvaluationMode mode,
                                   const core::ContentIdentity& sliceId,
                                   std::uint32_t contractVersion,
                                   const core::ContentIdentity& profileIdentity)
    {
        const core::TaskIdentity identity = makeIdentity();
        RegistrationInput in;
        in.identity = identity;
        in.evaluatorKey = "kin-batch-ik";
        in.contractVersion = contractVersion;
        in.snapshotId = cid(kHex64A);
        in.sliceId = sliceId;
        in.inputBaselineId = cid(kHex64B);
        in.policyIdentity = cid(kHex64D);
        in.nameMapIdentity = cid(kHex64D);
        in.mode = mode;
        in.runDir = fs::path{"results"} / identity.run.toCanonical();
        in.runKind = "kin-batch-eval";
        in.allowedResultKinds = {ResultKind::FinalEnvelope};
        in.currentState = core::TaskState::Running;

        auto materials = std::make_shared<RunEvaluationMaterials>();
        materials->snapshot = std::make_shared<const ev::AnalysisSnapshot>();
        materials->producers = m_producers;
        materials->profiles = m_profiles;
        materials->manifestProfile = ev::EvidenceProfileRef{"kin", "1.0.0", profileIdentity};
        in.resources.evaluation = std::move(materials);
        in.resources.archive = std::make_shared<NullGateway>();
        m_registry->registerRun(TaskId::generate(), in);
        return identity;
    }

    /// 推进到"Completed＋归档完成"镜像（storeResult 的门槛正例）。
    void makeCompletedAndArchived(core::RunId run)
    {
        m_registry->noteAdmissionCompleted(run);
        m_registry->noteArchivePhase(run, ArchivePhase::Archived);
    }

    /// 标准查询面（Verified/S/7/P 组合的变异底座）。
    CacheLookupQuery makeQuery(core::EvaluationMode mode,
                               const core::ContentIdentity& sliceId,
                               std::uint32_t contractVersion,
                               const core::ContentIdentity& profileIdentity) const
    {
        CacheLookupQuery q;
        q.requestedMode = mode;
        q.requestSliceId = sliceId;
        q.requestContractVersion = contractVersion;
        q.requestProfileIdentity = profileIdentity;
        return q;
    }

    std::unique_ptr<CollectingSink> m_sink;
    std::unique_ptr<RunRegistry> m_registry;
    std::shared_ptr<NoopProducers> m_producers;
    std::shared_ptr<NoopProfiles> m_profiles;
    std::unique_ptr<ExecutionCacheCoordinator> m_coordinator;
};

/// 标准查询面常量（Verified/SliceC/7/ProfileA——各用例单点变异）。
const core::EvaluationMode kMode = core::EvaluationMode::Verified;
const core::ContentIdentity kSlice = cid(kHex64C);
const std::uint32_t kContract = 7;
const core::ContentIdentity kProfile = cid(kHex64A);

// ---------------------------------------------------------------------
// EX-CCH-2：失败/取消结果不入正式缓存（仅 finalize 产物入缓存）
// ---------------------------------------------------------------------

TEST_F(CacheCoordinatorTest, EX_CCH_2_StoreGateOnlyFinalizedCompleted)
{
    // ①Completed＋Archived → 入缓存（清单断言：resultEntries==1）。
    const core::TaskIdentity ok = registerRun(kMode, kSlice, kContract, kProfile);
    makeCompletedAndArchived(ok.run);
    m_coordinator->storeResult(ok.run);
    EXPECT_EQ(m_coordinator->stats().resultEntries, 1u);

    // ②Completed 但归档未 finalize（Archiving）→ 不登记（§8.2 写入门槛
    //   双条件之二）。
    const core::TaskIdentity archiving = registerRun(kMode, kSlice, kContract, kProfile);
    m_registry->noteAdmissionCompleted(archiving.run);
    m_registry->noteArchivePhase(archiving.run, ArchivePhase::Archiving);
    m_coordinator->storeResult(archiving.run);
    EXPECT_EQ(m_coordinator->stats().resultEntries, 1u);
    EXPECT_TRUE(m_sink->hasDevChannel("execution/cache"));

    // ③运行中未终结 → 不登记。
    const core::TaskIdentity running = registerRun(kMode, kSlice, kContract, kProfile);
    m_coordinator->storeResult(running.run);
    EXPECT_EQ(m_coordinator->stats().resultEntries, 1u);

    // ④Canceled/Failed 终结 → 不登记（CON-04/TASK-02 的执行侧落点——
    //   失败/取消结果禁止进入正式成功缓存）。
    const core::TaskIdentity canceled = registerRun(kMode, kSlice, kContract, kProfile);
    m_registry->markTerminated(canceled.run, TerminationCause::Canceled);
    m_coordinator->storeResult(canceled.run);
    const core::TaskIdentity failed = registerRun(kMode, kSlice, kContract, kProfile);
    m_registry->markTerminated(failed.run, TerminationCause::Failed);
    m_coordinator->storeResult(failed.run);
    EXPECT_EQ(m_coordinator->stats().resultEntries, 1u);

    // ⑤未登记运行 → 调用方契约违约 fail-fast（登记表是唯一事实镜像）。
    EXPECT_THROW(m_coordinator->storeResult(core::RunId::generate()), ExecutionError);

    // 入账条目的命中（对照闭环：只有①可命中）。
    const CacheLookup hit = m_coordinator->lookup(makeQuery(kMode, kSlice, kContract, kProfile));
    EXPECT_EQ(hit.guidance, CacheLookup::Guidance::ShortPath);
    ASSERT_TRUE(hit.hitEntry.has_value());
    EXPECT_EQ(hit.hitEntry->run, ok.run);
}

/// 部分批次仅入诊断账户：查找给 DiagnosticOnly＋正常派发（永不 ShortPath
/// ——EX-CCH-2 的查找面"部分批次仅 DiagnosticOnly"）。
TEST_F(CacheCoordinatorTest, PartialBatchesStayDiagnosticOnly)
{
    // 诊断条目（取消运行的未 finalize 批次——调用方如实填报摘要面）。
    CacheEntrySummary partial;
    partial.run = core::RunId::generate();
    partial.summary.mode = kMode;
    partial.summary.outcome = core::TaskOutcome::Canceled;
    partial.summary.manifestFinalized = false;
    partial.summary.sliceId = kSlice;
    partial.summary.evaluatorContractVersion = kContract;
    partial.summary.profileContentIdentity = kProfile;
    partial.payloadBytes = 128;
    m_coordinator->storePartialDiagnostic(std::move(partial));

    // 查找：诊断性可读（verdict 来自 evidence 判定），但 guidance 恒
    // 正常派发——部分结果永不替代派发，"不派发伪装"的边界。
    const CacheLookup result = m_coordinator->lookup(makeQuery(kMode, kSlice, kContract, kProfile));
    EXPECT_EQ(result.guidance, CacheLookup::Guidance::DispatchNormal);
    EXPECT_EQ(result.resultVerdict.verdict, ev::CacheHitResult::DiagnosticOnly);
    EXPECT_FALSE(result.hitEntry.has_value());
    // 诊断账户与正式账户分账（四类区分——诊断不入 resultEntries）。
    EXPECT_EQ(m_coordinator->stats().diagnosticEntries, 1u);
    EXPECT_EQ(m_coordinator->stats().resultEntries, 0u);
}

// ---------------------------------------------------------------------
// EX-CCH-1：Quick 缓存不替代 Verified（mode-mismatch→Incompatible）＋
// 命中≠当前的结构面＋OPT-06 键绑定
// ---------------------------------------------------------------------

TEST_F(CacheCoordinatorTest, EX_CCH_1_ModeMismatchNeverShortPath)
{
    // Quick 运行完成并归档（正式 Quick 条目入缓存）。
    const core::TaskIdentity quickRun
        = registerRun(core::EvaluationMode::Quick, kSlice, kContract, kProfile);
    makeCompletedAndArchived(quickRun.run);
    m_coordinator->storeResult(quickRun.run);

    // Verified 请求同键面（slice/契约/Profile 同、模式不同）→ Incompatible
    // （判定来自真实 evidence judgeCacheHit——reasons[0]＝ModeMismatch，
    //  mode-mismatch 的 token 溯源面）＋正常派发（不派发伪装：调度侧
    //   消费 guidance，Quick 产物绝不冒充 Verified 答案——表 1 不升降级）。
    const CacheLookup mismatch = m_coordinator->lookup(
        makeQuery(core::EvaluationMode::Verified, kSlice, kContract, kProfile));
    EXPECT_EQ(mismatch.guidance, CacheLookup::Guidance::DispatchNormal);
    EXPECT_EQ(mismatch.resultVerdict.verdict, ev::CacheHitResult::Incompatible);
    ASSERT_FALSE(mismatch.resultVerdict.reasons.empty());
    EXPECT_EQ(mismatch.resultVerdict.reasons[0], ev::CacheMissReason::ModeMismatch);
    EXPECT_FALSE(mismatch.hitEntry.has_value());

    // 反向对照：同模式请求 FullHit 短路径＋OPT-06 绑定可见
    // （inputBaselineId 随条目携带——记录面核对；命中规格带产生运行引用
    //  ——缓存命中也是一个可追溯运行）。
    const CacheLookup hit = m_coordinator->lookup(
        makeQuery(core::EvaluationMode::Quick, kSlice, kContract, kProfile));
    EXPECT_EQ(hit.guidance, CacheLookup::Guidance::ShortPath);
    EXPECT_EQ(hit.resultVerdict.verdict, ev::CacheHitResult::FullHit);
    EXPECT_TRUE(hit.resultVerdict.reasons.empty());
    ASSERT_TRUE(hit.hitEntry.has_value());
    EXPECT_EQ(hit.hitEntry->run, quickRun.run);
    ASSERT_TRUE(hit.hitEntry->summary.inputBaselineId.has_value());
    EXPECT_EQ(hit.hitEntry->summary.inputBaselineId.value(), cid(kHex64B));

    // 命中≠当前结果（CON-04）的结构面：FullHit 结论中不存在任何当前性
    // 字段——当前性归 evidence computeCurrentness 另判（Superseded 的
    // 历史缓存照样可命中同键请求）。此断言钉住 CacheLookup 的字段面。
    EXPECT_EQ(hit.hitEntry->summary.outcome, core::TaskOutcome::Completed);
}

// ---------------------------------------------------------------------
// D-10：同键并发缓存请求合并为等待同一运行
// ---------------------------------------------------------------------

TEST_F(CacheCoordinatorTest, D10_ConcurrentSameKeyMergedToInFlightRun)
{
    const CacheLookupQuery q = makeQuery(kMode, kSlice, kContract, kProfile);

    // 首个派发登记在途 → 后续同键查找合并为等待该运行（不重复派发）。
    const core::TaskIdentity first = registerRun(kMode, kSlice, kContract, kProfile);
    m_coordinator->noteDispatchStarted(q, first.run);
    const CacheLookup waiting = m_coordinator->lookup(q);
    EXPECT_EQ(waiting.guidance, CacheLookup::Guidance::WaitForInFlightRun);
    ASSERT_TRUE(waiting.inFlightRun.has_value());
    EXPECT_EQ(waiting.inFlightRun.value(), first.run);
    EXPECT_EQ(m_coordinator->stats().inFlightRuns, 1u);

    // 同键第二个运行派发登记 → 保留首个（后续记为等待——合并语义）。
    const core::TaskIdentity second = registerRun(kMode, kSlice, kContract, kProfile);
    m_coordinator->noteDispatchStarted(q, second.run);
    EXPECT_EQ(m_coordinator->stats().inFlightRuns, 1u);
    const CacheLookup stillFirst = m_coordinator->lookup(q);
    EXPECT_EQ(stillFirst.inFlightRun.value(), first.run);

    // 首个运行完成＋归档 → storeResult 解除在途＋条目可命中（共享完成
    // 事件闭环：等待者下一次查找即 FullHit）。
    makeCompletedAndArchived(first.run);
    m_coordinator->storeResult(first.run);
    EXPECT_EQ(m_coordinator->stats().inFlightRuns, 0u);
    const CacheLookup hit = m_coordinator->lookup(q);
    EXPECT_EQ(hit.guidance, CacheLookup::Guidance::ShortPath);
    EXPECT_EQ(hit.hitEntry->run, first.run);

    // 失败路径：在途运行终结但无缓存登记 → 后续查找回到正常派发
    // （等待者不悬挂——合并不引入永久等待）。
    const CacheLookupQuery q2 = makeQuery(kMode, kSlice, kContract + 1, kProfile);
    m_coordinator->noteDispatchStarted(q2, second.run);
    m_coordinator->noteRunFinished(second.run);
    const CacheLookup afterFail = m_coordinator->lookup(q2);
    EXPECT_EQ(afterFail.guidance, CacheLookup::Guidance::DispatchNormal);

    // 异键查找不受在途影响（键面隔离——合并只作用于同键）。
    const CacheLookupQuery otherKey = makeQuery(kMode, kSlice, kContract + 2, kProfile);
    const CacheLookup other = m_coordinator->lookup(otherKey);
    EXPECT_EQ(other.guidance, CacheLookup::Guidance::DispatchNormal);
}

// ---------------------------------------------------------------------
// 模型缓存联合查询（ICompileCacheJudge 注入消费——判定零复制）
// ---------------------------------------------------------------------

TEST_F(CacheCoordinatorTest, ModelCacheJudgeInjectionPassthrough)
{
    ScriptedCompileJudge judge;
    m_coordinator->setCompileCacheJudge(&judge);
    const CompileCacheKeyView key{cid(kHex64A), std::nullopt};

    // 无登记条目 → Miss（需编译——查找面缺失，不调用判定器）。
    CacheLookupQuery q = makeQuery(kMode, kSlice, kContract, kProfile);
    q.requestedModelKey = key;
    const CacheLookup miss = m_coordinator->lookup(q);
    EXPECT_EQ(miss.modelVerdict, CacheLookup::ModelVerdict::Miss);
    EXPECT_TRUE(judge.calls.empty());

    // 登记模型缓存 → 判定透传（FullReuse；调用入参＝请求键与登记键——
    // 注入边界两侧同值）。
    m_coordinator->storeModelCache(key, {0x01, 0x02, 0x03});
    judge.script.push_back(CompileCacheVerdict::FullReuse);
    const CacheLookup full = m_coordinator->lookup(q);
    EXPECT_EQ(full.modelVerdict, CacheLookup::ModelVerdict::FullReuse);
    ASSERT_EQ(judge.calls.size(), 1u);
    EXPECT_TRUE(judge.calls[0].requested == key);
    EXPECT_TRUE(judge.calls[0].cached == key);

    // WorkCellOnlyReuse 透传（不得作完整命中上报的处置在 Preparing 编排
    // ——本面只如实报告三态）。
    judge.script.push_back(CompileCacheVerdict::WorkCellOnlyReuse);
    const CacheLookup wcOnly = m_coordinator->lookup(q);
    EXPECT_EQ(wcOnly.modelVerdict, CacheLookup::ModelVerdict::WorkCellOnlyReuse);

    // Incompatible 透传（旧编译器/基线拒绝面——判定规则在注入侧）。
    judge.script.push_back(CompileCacheVerdict::Incompatible);
    const CacheLookup incompat = m_coordinator->lookup(q);
    EXPECT_EQ(incompat.modelVerdict, CacheLookup::ModelVerdict::Incompatible);

    // 不携带模型键 → NotQueried（联合查询的可选半区）。
    const CacheLookup notQueried
        = m_coordinator->lookup(makeQuery(kMode, kSlice, kContract, kProfile));
    EXPECT_EQ(notQueried.modelVerdict, CacheLookup::ModelVerdict::NotQueried);
}

/// 判定器缺席 fail-closed：模型面按 Incompatible 保守拒绝＋一次性留痕
/// （不静默放行复用——EX-T05 校验面缺失同口径）。
TEST_F(CacheCoordinatorTest, ModelCacheWithoutJudgeFailsClosed)
{
    const CompileCacheKeyView key{cid(kHex64A), std::nullopt};
    m_coordinator->storeModelCache(key, {0x01});
    CacheLookupQuery q = makeQuery(kMode, kSlice, kContract, kProfile);
    q.requestedModelKey = key;
    const CacheLookup result = m_coordinator->lookup(q);
    EXPECT_EQ(result.modelVerdict, CacheLookup::ModelVerdict::Incompatible);
    ASSERT_FALSE(result.modelReasons.empty());
    EXPECT_EQ(result.modelReasons[0], "judge-unavailable");
    EXPECT_TRUE(m_sink->hasDevChannel("execution/cache"));
    // 一次性留痕：再次查找不重复告警。
    (void)m_coordinator->lookup(q);
    EXPECT_EQ(m_sink->devs.size(), 1u);
}

// ---------------------------------------------------------------------
// LRU 分账淘汰与统计
// ---------------------------------------------------------------------

TEST_F(CacheCoordinatorTest, EvictionByBudgetAndCapWithStats)
{
    // 模型账户：字节预算 10 B——6 B×2 入账后只留 1 条（LRU 逐出）。
    EvictionPolicy tiny;
    tiny.maxModelCacheBytes = 10;
    tiny.maxDiagnosticEntries = 1;
    m_coordinator->setEvictionPolicy(tiny);

    const CompileCacheKeyView k1{cid(kHex64A), std::nullopt};
    const CompileCacheKeyView k2{cid(kHex64B), std::nullopt};
    m_coordinator->storeModelCache(k1, std::vector<std::uint8_t>(6, 0x11));
    m_coordinator->storeModelCache(k2, std::vector<std::uint8_t>(6, 0x22));
    const CacheStats afterModel = m_coordinator->stats();
    EXPECT_EQ(afterModel.modelEntries, 1u);
    EXPECT_EQ(afterModel.modelBytes, 6u);
    EXPECT_EQ(afterModel.evictedEntries, 1u);
    // 固定时钟下全部时刻相同——LRU 平局由键字典序裁决：victim 取最小键
    // k1（先入的 aaaa 逐出；三级字典序的确定性平局面，NFR-COR-02）。
    // 留存者必为 k2（bbbb）——经命中面间接验证（k1 已不在：再存同键不
    // 触发冲突诊断，k2 再存同长不同内容触发冲突诊断）。
    m_coordinator->storeModelCache(k2, std::vector<std::uint8_t>(9, 0x22));
    EXPECT_TRUE(m_sink->hasDevChannel("execution/cache"));   // k2 冲突→既有在账

    // 诊断账户：条目数上限 1——第二条入账逐出第一条（短周期保留）。
    CacheEntrySummary d1;
    d1.summary.mode = kMode;
    d1.summary.outcome = core::TaskOutcome::Interrupted;
    d1.summary.manifestFinalized = false;
    d1.summary.sliceId = kSlice;
    d1.summary.evaluatorContractVersion = kContract;
    d1.summary.profileContentIdentity = kProfile;
    m_coordinator->storePartialDiagnostic(d1);
    CacheEntrySummary d2 = d1;
    d2.summary.evaluatorContractVersion = kContract + 1;
    m_coordinator->storePartialDiagnostic(d2);
    EXPECT_EQ(m_coordinator->stats().diagnosticEntries, 1u);

    // 结果账户记账面（payloadBytes=0——阶段 A 载荷驻留归档目录）。
    const core::TaskIdentity run = registerRun(kMode, kSlice, kContract, kProfile);
    makeCompletedAndArchived(run.run);
    m_coordinator->storeResult(run.run);
    const CacheStats s = m_coordinator->stats();
    EXPECT_EQ(s.resultEntries, 1u);
    EXPECT_EQ(s.resultBytes, 0u);
}

/// 同键异载荷冲突：保留既有＋开发诊断（不覆盖——§8.2 内容冲突行）。
TEST_F(CacheCoordinatorTest, ModelCacheConflictKeepsExisting)
{
    const CompileCacheKeyView key{cid(kHex64A), std::nullopt};
    m_coordinator->storeModelCache(key, std::vector<std::uint8_t>(4, 0x11));
    m_coordinator->storeModelCache(key, std::vector<std::uint8_t>(9, 0x22));
    EXPECT_EQ(m_coordinator->stats().modelBytes, 4u);   // 既有保留
    EXPECT_TRUE(m_sink->hasDevChannel("execution/cache"));
}

/// 查找/派发登记的调用方契约违约 fail-fast（保留值键）。
TEST_F(CacheCoordinatorTest, LookupContractViolationsFailFast)
{
    CacheLookupQuery bad = makeQuery(kMode, core::ContentIdentity{}, kContract, kProfile);
    EXPECT_THROW((void)m_coordinator->lookup(bad), ExecutionError);
    EXPECT_THROW(m_coordinator->noteDispatchStarted(bad, core::RunId::generate()),
                 ExecutionError);
}

}  // namespace
