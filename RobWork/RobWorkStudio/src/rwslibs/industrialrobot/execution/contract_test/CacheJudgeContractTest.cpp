/**
 * @file   CacheJudgeContractTest.cpp
 * @brief  缓存/检查点判定的跨单元契约（EX-T08 acceptance 3~4）——
 *         execution 协调器的兼容性结论 ≡ evidence 判定函数直接调用的
 *         结论（单点复用零复制的结构性证明）；EX-CCH-1/EX-CKP-2 的
 *         reasons token 词表溯源；EX-CKP-3 恢复失败保留的接线面。
 *
 * 设计依据：
 *   - units/execution.md §3.4（`_contract_test`＝跨单元契约面）、§8.2
 *     （判定权威归 evidence judgeCacheHit/judgeCheckpointCompatibility；
 *     execution 只做存储/查找/淘汰/写入——§2.2 CON-04 行"不可越界"列）、
 *     §11（EX-CCH-1 行"判定来自 evidence——本用例验证调度侧消费与不
 *     派发伪装"；EX-CKP-2 行"judgeCheckpointCompatibility 调用记录——
 *     判定归 evidence 验证"）
 *   - ARCHITECTURE.md §3.5（execution→evidence 为登记边——真实函数直
 *     调；runtime 经 ICompileCacheJudge 注入——P-EX-3，其透传面在单元
 *     测试以脚本替身钉住，本套件不链接 runtime：不制造表外边观测面）
 *   - 任务契约 tasks/foundation/EX-T08.json acceptance 3~4（EX-CCH-1/
 *     EX-CKP-2、判定逻辑零复制）
 *
 * 替身边界声明：归档写面复用 §11"归档端口 fake"形态（写真实临时文件
 *   的最小替身——ArchiveSessionRef 构造归 project 内部，替身以默认空
 *   句柄自持）；判定面零替身（真实 evidence 纯函数）；登记表为真实
 *   RunRegistry。EX-T09 套件落地时本面并入全矩阵。
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

const char* kHex64A = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
const char* kHex64B = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
const char* kHex64C = "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";
const char* kHex64D = "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd";

core::ContentIdentity cid(const char* hex64)
{
    return core::ContentIdentity::fromCanonical(std::string{"cid-"} + hex64);
}

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

std::chrono::system_clock::time_point fixedNow()
{
    return std::chrono::system_clock::time_point{std::chrono::seconds{1000}};
}

/// 诊断收集替身（结构化通道断言面）。
class CollectingSink final : public IExecutionDiagnosticsSink {
public:
    std::vector<core::DiagnosticRecord> reports;

    void report(const core::DiagnosticRecord& record) override { reports.push_back(record); }
    void reportDev(const std::string&, const std::string&) override {}
    bool hasReportCode(const std::string& code) const
    {
        for (const auto& r : reports) {
            if (r.code == code) {
                return true;
            }
        }
        return false;
    }
};

/// 归档端口最小替身（写真实临时文件——批次/manifest 两动作；会话令牌
/// 为默认空句柄，见 CheckpointCacheTest 同款登记）。
class MiniArchiveDisk final : public pd::IResultArchivePort {
public:
    pd::ArchiveSessionRef begin(const pd::ArchiveRequest& request) override
    {
        m_dir = request.runDir;
        return pd::ArchiveSessionRef{};
    }
    pd::ArchiveStatus writeBatch(pd::ArchiveSessionRef session,
                                 const pd::ArchiveBatch& batch) override
    {
        (void)session;
        std::error_code ec;
        fs::create_directories(m_dir, ec);
        for (const pd::ArchiveItem& item : batch.items) {
            std::ofstream out(m_dir / item.relPath, std::ios::binary);
            out.write(reinterpret_cast<const char*>(item.bytes.data()),
                      static_cast<std::streamsize>(item.bytes.size()));
        }
        return {true, std::nullopt};
    }
    pd::ArchiveStatus finalize(pd::ArchiveSessionRef session,
                               const pd::RunManifest& manifest) override
    {
        (void)session;
        (void)manifest;
        std::ofstream out(m_dir / "manifest.json", std::ios::binary);
        out << "{\"fake\":true}";
        return {true, std::nullopt};
    }
    void abandon(pd::ArchiveSessionRef, pd::ArchiveEndReason) override {}

private:
    fs::path m_dir;
};

// ---- 登记底座（registerRun 资源上下文非空要求——最小桩）----

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

class NullGateway final : public IRunArchiveGateway {
public:
    class NullPort final : public pd::IResultArchivePort {
    public:
        pd::ArchiveSessionRef begin(const pd::ArchiveRequest&) override
        {
            throw std::runtime_error("null port 不应被调用");
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
    pd::IResultArchivePort& port() const override
    {
        static NullPort port;
        return port;
    }
    bool reacquireWriteAuthority() override { return false; }
};

// =====================================================================
// 夹具：协调器全接线＋临时目录
// =====================================================================

class CacheJudgeContractTest : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        std::error_code ec;
#ifdef _WIN32
        s_base = fs::temp_directory_path(ec) / "ird_ex_cachejudge_contract"
                 / std::to_string(::GetCurrentProcessId());
#else
        s_base = fs::temp_directory_path(ec) / "ird_ex_cachejudge_contract";
#endif
        ASSERT_FALSE(ec);
        fs::create_directories(s_base, ec);
        if (ec) {
            std::exit(2);
        }
    }

    static void TearDownTestSuite()
    {
        std::error_code ec;
        fs::remove_all(s_base, ec);
    }

    void SetUp() override
    {
        m_caseDir = s_base / ("case" + std::to_string(++s_caseCounter));
        fs::create_directories(m_caseDir);
        m_archive = std::make_unique<MiniArchiveDisk>();
        m_sink = std::make_unique<CollectingSink>();
        m_registry = std::make_unique<RunRegistry>(fixedNow);
        m_producers = std::make_shared<NoopProducers>();
        m_profiles = std::make_shared<NoopProfiles>();
        m_checkpoints = std::make_unique<CheckpointCoordinator>(*m_archive, *m_sink, fixedNow);
        m_cache = std::make_unique<ExecutionCacheCoordinator>(*m_registry, *m_sink, fixedNow);
        m_identity = makeIdentity();
    }

    /// 登记一份运行（Completed＋Archived 镜像可选项）。
    core::TaskIdentity registerRun(core::EvaluationMode mode,
                                   const core::ContentIdentity& sliceId,
                                   std::uint32_t contractVersion,
                                   const core::ContentIdentity& profileIdentity,
                                   bool completedAndArchived = true)
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
        if (completedAndArchived) {
            m_registry->noteAdmissionCompleted(identity.run);
            m_registry->noteArchivePhase(identity.run, ArchivePhase::Archived);
        }
        return identity;
    }

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

    /// 写一份标准检查点（契约版本 7、切片 C；seq/可续可调）。
    void writeStandardCheckpoint(std::uint64_t seq, bool resumable = true)
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
        req.record.threadCount = 2;
        req.record.completedUnits = 10;
        req.record.totalUnits = 20;
        req.record.resumable = resumable;
        CheckpointBatchFile f;
        f.relPath = "state.bin";
        f.bytes = {0xAA, 0xBB, 0xCC, 0xDD};
        req.files.push_back(std::move(f));
        req.archiveDir = m_caseDir / "checkpoints"
                         / m_identity.run.toCanonical() / std::to_string(seq);
        const CheckpointWriteResult result = m_checkpoints->writeCheckpoint(req);
        ASSERT_EQ(result.outcome, CheckpointWriteResult::Outcome::Published);
    }

    static fs::path s_base;
    static int s_caseCounter;
    fs::path m_caseDir;
    std::unique_ptr<MiniArchiveDisk> m_archive;
    std::unique_ptr<CollectingSink> m_sink;
    std::unique_ptr<RunRegistry> m_registry;
    std::shared_ptr<NoopProducers> m_producers;
    std::shared_ptr<NoopProfiles> m_profiles;
    std::unique_ptr<CheckpointCoordinator> m_checkpoints;
    std::unique_ptr<ExecutionCacheCoordinator> m_cache;
    core::TaskIdentity m_identity;
};

fs::path CacheJudgeContractTest::s_base;
int CacheJudgeContractTest::s_caseCounter = 0;

// =====================================================================
// EX-CKP-2：检查点判定的单点复用——协调器结论 ≡ evidence 直接判定
// =====================================================================

/**
 * 结构性证明（acceptance 4"判定逻辑零复制"）：对同一组输入，restore 的
 * verdict/reasons 与直接调用 evidence::judgeCheckpointCompatibility 的
 * 输出**逐字段相等**——协调器侧不存在第二套判定（若存在本地复判，两
 * 路输出在任何一处语义偏差上即分叉，用例即红）。
 */
TEST_F(CacheJudgeContractTest, EX_CKP_2_RestoreVerdictEqualsDirectEvidenceJudge)
{
    writeStandardCheckpoint(1);

    // --- 失配请求（契约版本 8）：协调器路径。
    const CheckpointRestoreResult viaCoordinator = m_checkpoints->restore(
        CheckpointRestoreRequest{CheckpointId{m_identity.run, 1}, cid(kHex64C), 8});
    ASSERT_EQ(viaCoordinator.outcome, CheckpointRestoreResult::Outcome::Incompatible);
    EXPECT_TRUE(m_sink->hasReportCode("EX-CHECKPOINT-INCOMPATIBLE"));

    // --- 同输入直接判 evidence（登记摘要五字段＝协调器组装口径）。
    ev::CacheHitQuery judgeRequest;
    judgeRequest.requestSliceId = cid(kHex64C);
    judgeRequest.requestContractVersion = 8;
    ev::CheckpointSummary summary;
    summary.evaluatorKey = "kin-batch-ik";
    summary.sliceId = cid(kHex64C);
    summary.evaluatorContractVersion = 7;
    summary.checkpointFormatVersion = 1;   // 写出戳（当前版本）
    summary.integrityVerified = true;
    const ev::CheckpointCompatibilityResult direct
        = ev::judgeCheckpointCompatibility(judgeRequest, summary);

    // 逐字段相等（verdict＋reasons 全清单）。
    EXPECT_EQ(viaCoordinator.reasons, direct.reasons);
    ASSERT_EQ(direct.verdict, ev::CheckpointCompatibilityResult::Incompatible);
    ASSERT_FALSE(direct.reasons.empty());
    EXPECT_EQ(direct.reasons[0], "contract-mismatch");

    // --- 兼容请求（契约版本 7）：两路同为 Resumeable。
    const CheckpointRestoreResult compatible = m_checkpoints->restore(
        CheckpointRestoreRequest{CheckpointId{m_identity.run, 1}, cid(kHex64C), 7});
    EXPECT_EQ(compatible.outcome, CheckpointRestoreResult::Outcome::Restored);
    judgeRequest.requestContractVersion = 7;
    EXPECT_EQ(ev::judgeCheckpointCompatibility(judgeRequest, summary).verdict,
              ev::CheckpointCompatibilityResult::Resumeable);
}

/// EX-CKP-3 接线面：resumable=false 的恢复拒绝经协调器发生且磁盘保留
/// （判定/申报两通道的分野——拒绝不来自 evidence 判定，来自域申报，
/// 磁盘断言为观测点）。
TEST_F(CacheJudgeContractTest, EX_CKP_3_NotResumableRefusalWiring)
{
    writeStandardCheckpoint(1, /*resumable=*/false);
    const fs::path dir = m_caseDir / "checkpoints" / m_identity.run.toCanonical() / "1";
    std::map<std::string, std::string> before;
    for (const auto& entry : fs::directory_iterator(dir)) {
        std::ifstream in(entry.path(), std::ios::binary);
        std::string bytes((std::istreambuf_iterator<char>(in)),
                          std::istreambuf_iterator<char>());
        before[entry.path().filename().string()] = bytes;
    }

    const CheckpointRestoreResult result = m_checkpoints->restore(
        CheckpointRestoreRequest{CheckpointId{m_identity.run, 1}, cid(kHex64C), 7});
    EXPECT_EQ(result.outcome, CheckpointRestoreResult::Outcome::NotResumable);

    // 磁盘断言：目录内容不变（原检查点完整保留）。
    std::map<std::string, std::string> after;
    for (const auto& entry : fs::directory_iterator(dir)) {
        std::ifstream in(entry.path(), std::ios::binary);
        std::string bytes((std::istreambuf_iterator<char>(in)),
                          std::istreambuf_iterator<char>());
        after[entry.path().filename().string()] = bytes;
    }
    EXPECT_EQ(after, before);
}

// =====================================================================
// EX-CCH-1：缓存判定的单点复用——lookup 结论 ≡ evidence 直接判定＋
// 调度边界（不派发伪装）
// =====================================================================

/**
 * 结构性证明（acceptance 3~4）：lookup 的 resultVerdict/guidance 与直接
 * 调用 evidence::judgeCacheHit 的输出逐字段相等；Quick↔Verified 双向
 * 失配（表 1 模式效力——不升降级，D-13）在调度边界全部落为正常派发。
 */
TEST_F(CacheJudgeContractTest, EX_CCH_1_LookupVerdictEqualsDirectEvidenceJudge)
{
    // Quick 运行完成＋归档＋入缓存（执行侧门槛的正例通道）。
    const core::TaskIdentity quickRun
        = registerRun(core::EvaluationMode::Quick, cid(kHex64C), 7, cid(kHex64A));
    m_cache->storeResult(quickRun.run);

    // 条目摘要（与 storeResult 的登记组装同构——判定输入同源）。
    ev::CachedResultSummary entrySummary;
    entrySummary.mode = core::EvaluationMode::Quick;
    entrySummary.outcome = core::TaskOutcome::Completed;
    entrySummary.manifestFinalized = true;
    entrySummary.sliceId = cid(kHex64C);
    entrySummary.evaluatorContractVersion = 7;
    entrySummary.profileContentIdentity = cid(kHex64A);
    entrySummary.inputBaselineId = cid(kHex64B);

    // --- 方向一：Quick 条目 × Verified 请求（高级别证据不被降级冒充）。
    const CacheLookup verified = m_cache->lookup(
        makeQuery(core::EvaluationMode::Verified, cid(kHex64C), 7, cid(kHex64A)));
    ev::CacheHitQuery verifiedQuery;
    verifiedQuery.requestedMode = core::EvaluationMode::Verified;
    verifiedQuery.requestSliceId = cid(kHex64C);
    verifiedQuery.requestContractVersion = 7;
    verifiedQuery.requestProfileIdentity = cid(kHex64A);
    const ev::CacheHitResult directVerified = ev::judgeCacheHit(verifiedQuery, entrySummary);

    // 逐字段相等（verdict＋reasons——mode-mismatch token 溯源 evidence）。
    EXPECT_EQ(verified.resultVerdict.verdict, directVerified.verdict);
    EXPECT_EQ(verified.resultVerdict.reasons.size(), directVerified.reasons.size());
    ASSERT_FALSE(verified.resultVerdict.reasons.empty());
    EXPECT_EQ(verified.resultVerdict.reasons[0], ev::CacheMissReason::ModeMismatch);
    EXPECT_EQ(directVerified.reasons[0], ev::CacheMissReason::ModeMismatch);
    // 调度边界：失配即正常派发，绝无短路径（不派发伪装的观测点）。
    EXPECT_EQ(verified.guidance, CacheLookup::Guidance::DispatchNormal);
    EXPECT_FALSE(verified.hitEntry.has_value());

    // --- 方向二：Quick 条目 × Quick 请求 → FullHit（两路同为 FullHit）。
    const CacheLookup quick = m_cache->lookup(
        makeQuery(core::EvaluationMode::Quick, cid(kHex64C), 7, cid(kHex64A)));
    ev::CacheHitQuery quickQuery = verifiedQuery;
    quickQuery.requestedMode = core::EvaluationMode::Quick;
    const ev::CacheHitResult directQuick = ev::judgeCacheHit(quickQuery, entrySummary);
    EXPECT_EQ(quick.resultVerdict.verdict, directQuick.verdict);
    EXPECT_EQ(directQuick.verdict, ev::CacheHitResult::FullHit);
    EXPECT_TRUE(quick.resultVerdict.reasons.empty());
    EXPECT_EQ(quick.guidance, CacheLookup::Guidance::ShortPath);
    ASSERT_TRUE(quick.hitEntry.has_value());
    EXPECT_EQ(quick.hitEntry->run, quickRun.run);

    // --- 方向三：Verified 条目 × Quick 请求（低级别证据不被升格冒充
    //     ——双向同判；用独立切片构造，排除方向一 Quick 条目的合法命中
    //     干扰——同切片同存时 Quick 请求命中 Quick 条目本就是正确行为）。
    const core::TaskIdentity verifiedRun
        = registerRun(core::EvaluationMode::Verified, cid(kHex64D), 7, cid(kHex64A));
    m_cache->storeResult(verifiedRun.run);
    const CacheLookup down = m_cache->lookup(
        makeQuery(core::EvaluationMode::Quick, cid(kHex64D), 7, cid(kHex64A)));
    EXPECT_EQ(down.resultVerdict.verdict, ev::CacheHitResult::Incompatible);
    ASSERT_FALSE(down.resultVerdict.reasons.empty());
    EXPECT_EQ(down.resultVerdict.reasons[0], ev::CacheMissReason::ModeMismatch);
    EXPECT_EQ(down.guidance, CacheLookup::Guidance::DispatchNormal);
    EXPECT_FALSE(down.hitEntry.has_value());
}

}  // namespace
