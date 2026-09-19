/**
 * @file   ArchiveFaultContractTest.cpp
 * @brief  归档故障注入契约用例组（EX-T09 §11 矩阵补齐）——EX-ARC-1 磁盘
 *         不足、EX-ARC-2 写入拒绝、EX-ARC-4 UI 已关闭但归档仍在途。
 *
 * 设计依据：
 *   - units/execution.md §11（EX-ARC-1：归档端口 fake 注入 disk-full→
 *     有界重试→abandon＋EX-ARCHIVE-FAILED、任务保持 Completed、**缓存跳过
 *     登记**；EX-ARC-2：write-rejected 同路径、StoreError 透传；EX-ARC-4：
 *     shutdown＋在途归档→上下文存活至 finalize、drained() 后 Closed、
 *     超阈值强制 abandon 兜底亦验证——PM-03/A7）、§9.3（迟到处置：有界
 *     自动重试 D-11、abandon 后任务终态不变）、§9.5（归档协作/A7）、
 *     §7.5（UI 关闭——无永久等待）、§3.4（`_contract_test` 分工＝跨单元
 *     契约面：与 project 归档端口——本组用真实 ProjectStore＋端口 fake）
 *   - units/testkit.md §6.4（FaultInterceptor 接缝原语——本组即其
 *     execution/archive/* 接缝的接线消费）、§6.6（ManualClock 虚拟推进
 *     ——EX-ARC-4 兜底阈值不 sleep）、§7.3（IRD_TEST_INFO 追溯登记）
 *   - 需求 NFR-REL-01（归档失败有界责任终结）、PM-03（关闭二选——等待
 *     分支的执行侧保证）、AT-10/AT-13（场景载体——契约 acceptance 5）
 *   - 任务契约 tasks/foundation/EX-T09.json acceptance 1（§11 矩阵逐项）、
 *     2（FaultInterceptor 接缝在案）、4（断言稳定码只在收编清单）、
 *     5（AT 载体＋ManualClock 纪律）
 *
 * 与既有用例的关系（零重复声明）：单元套件 RunRegistryAdmissionTest 的
 *   ArchiveRetriesBoundedThenSucceeds／EX_ARC_5 两用例已钉住有界重试的
 *   编排语义（退避值/abandon 原因/诊断码）；本组按 §11 目标分工落到
 *   契约面——①以 testkit FaultInterceptor 驱动归档端口 fake（§11"归档
 *   端口 fake"原文形态，故障点标识登记见套件设施头）；②补 EX-ARC-1 行
 *   特有的"缓存跳过登记"联动断言；③EX-ARC-4 的行为用例（EX-T03 期登记
 *   "行为用例归 EX-T09"）。
 *
 * 替身边界声明（§11——完整声明见 ContractSuiteFacilities.hpp 文件头）：
 *   归档写面为**真实** ProjectStore＋FaultInterceptor 包裹的端口 fake
 *   （注入只落在 ArchiveStatus 返回值上——ArchiveSessionRef 防伪，替身
 *   不铸造会话）；解码/反解为接口替身；评估产出为契约形态数据。替身
 *   输出不构成业务算法正确性证明（R-7/A-9）。
 */

#include "ContractSuiteFacilities.hpp"

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/evidence/Evidence.hpp>
#include <sdurws/ird/evidence/Snapshot.hpp>
#include <sdurws/ird/execution/CacheCoordinator.hpp>
#include <sdurws/ird/execution/Controller.hpp>
#include <sdurws/ird/execution/Ports.hpp>
#include <sdurws/ird/execution/RunRegistry.hpp>
#include <sdurws/ird/execution/Scheduler.hpp>
#include <sdurws/ird/execution/StateMachine.hpp>
#include <sdurws/ird/execution/TaskTypes.hpp>
#include <sdurws/ird/project/ArchivePort.hpp>
#include <sdurws/ird/project/ProjectStore.hpp>
#include <sdurws/ird/testkit/Fault.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
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
namespace tk = sdurws::ird::testkit;
namespace fs = std::filesystem;
using exsuite::kFaultArchiveWriteBatch;

// =====================================================================
// 身份/取值辅助（契约套件同款确定性固定值——自持不共享）
// =====================================================================

const char* kHex32A = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
const char* kHex32B = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
const char* kHex64A = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
const char* kHex64B = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
const char* kHex64C = "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";

core::ContentIdentity cid(const char* hex64)
{
    return core::ContentIdentity::fromCanonical(std::string{"cid-"} + hex64);
}

core::ContentVersion cv(const char* hex64)
{
    return core::ContentVersion::fromCanonical(std::string{"cv-"} + hex64);
}

core::ObjectId oid(const char* hex32)
{
    return core::ObjectId::fromCanonical(std::string{"obj-"} + hex32);
}

/// 受理记录的最小合法形态（TaskStateMachineTest 同款——EX-ARC-4 编排用）。
TaskRecord makeQueuedRecord()
{
    TaskRecord r;
    r.taskId = TaskId::generate();
    r.submission.snapshot.project = core::ProjectId::generate();
    r.submission.snapshot.branch = core::BranchId::generate();
    r.submission.snapshot.revision = core::RevisionId::generate();
    r.submission.evaluatorKey = "kin-batch-ik";
    r.submission.contractVersion = 1;
    r.submission.mode = core::EvaluationMode::Verified;
    return r;
}

// =====================================================================
// 快照/Profile/判定素材（契约形态数据——ArchiveCollaborationContractTest
// 同构最小实例；替身边界见文件头）
// =====================================================================

class AcceptAllClosureSource : public ev::IRevisionClosureSource {
public:
    bool objectInRevision(core::RevisionId, core::ObjectId, core::ContentVersion) const override
    {
        return true;
    }
};

ev::AnalysisSnapshot buildSnapshotFor(core::ProjectId project, core::BranchId branch,
                                      core::RevisionId revision)
{
    ev::SnapshotBuilder b;
    b.setIdentity(project, branch, revision, 1);
    b.setPolicyRef(ev::PolicyRef{cid(kHex64A)});
    b.setNameMapRef(ev::NameMapRef{cid(kHex64B)});
    ev::ReproductionBlock r;
    r.productVersion = "industrialrobot-designer 0.1.0";
    r.evidenceContractVersion = "evidence-contract/1";
    r.codecVersions = {"snapshot-codec/1", "slice-codec/1"};
    b.setReproduction(r);
    ev::ObjectRefEntry obj;
    obj.objectId = oid(kHex32A);
    obj.contentVersion = cv(kHex64A);
    obj.objectTypeToken = "robot-design";
    obj.digest = obj.contentVersion.bytes;
    b.addObjectRef(obj);
    for (const ev::CaseEntry& c : std::vector<ev::CaseEntry>{
             {oid(kHex32A), "case-a", true, true},
             {oid(kHex32B), "case-b", true, true}}) {
        b.addCase(c);
    }
    AcceptAllClosureSource source;
    return b.build(source);
}

ev::RequiredEvidenceProfile standardKinProfile()
{
    ev::RequiredEvidenceProfile p;
    p.profileId = "kin";
    p.version = "1.0.0";
    ev::EvidenceProfileItem reach;
    reach.itemId = "kin.reach-per-task-point";
    reach.itemClass = ev::EvidenceItemClass::Required;
    reach.description = "任务点可达性证据（最小承载实例）";
    ev::EvidenceProfileItem convergence;
    convergence.itemId = "kin.ik-convergence-per-point";
    convergence.itemClass = ev::EvidenceItemClass::Required;
    convergence.description = "逐任务点 IK 收敛证据（最小承载实例）";
    p.required = {reach, convergence};
    p.contentIdentity = ev::computeProfileContentIdentity(p);
    return p;
}

class ScriptedProducerRegistry final : public ev::IProducerRegistryView {
public:
    bool isRegistered(std::string_view key) const override
    {
        return key == "kin-batch-ik";
    }
    bool contractVersionMatches(std::string_view key, std::uint32_t version) const override
    {
        return key == "kin-batch-ik" && version == 7;
    }
};

class ScriptedProfileRegistry final : public ev::IProfileRegistryView {
public:
    explicit ScriptedProfileRegistry(ev::RequiredEvidenceProfile profile)
        : m_profile(std::move(profile))
    {
    }
    const ev::RequiredEvidenceProfile* findProfile(std::string_view profileId,
                                                   std::string_view version) const override
    {
        if (profileId == m_profile.profileId && version == m_profile.version) {
            return &m_profile;
        }
        return nullptr;
    }

private:
    ev::RequiredEvidenceProfile m_profile;
};

ev::CaseCoverageMatrix fullCoverage(const ev::AnalysisSnapshot& snapshot)
{
    ev::CaseCoverageMatrix matrix;
    matrix.requiredCaseSetId = snapshot.caseSet.requiredCaseSetId;
    for (const ev::CaseEntry& entry : snapshot.caseSet.entries) {
        ev::CaseCoverageEntry item;
        item.caseId = entry.caseId;
        item.status = ev::CaseExecutionStatus::Executed;
        matrix.entries.push_back(item);
    }
    return matrix;
}

ev::EvidenceItem satisfiedItem(std::string itemId)
{
    ev::EvidenceItem item;
    item.itemId = std::move(itemId);
    item.status = ev::EvidenceItemStatus::Satisfied;
    item.artifactDigest = cv(kHex64A).bytes;
    return item;
}

/// 标准合法产出（两项必需 Satisfied＋域载荷——接纳步 9 的组装素材）。
ev::EvaluationOutput standardOutput()
{
    ev::EvaluationOutput out;
    out.evidence = {satisfiedItem("kin.reach-per-task-point"),
                    satisfiedItem("kin.ik-convergence-per-point")};
    ev::DomainPayload payload;
    payload.kindToken = "kin.batch-ik.v1";
    payload.canonicalBytes = {0x01, 0x02, 0x03, 0x04};
    core::ContentDigester d;
    d.update(payload.canonicalBytes.data(), payload.canonicalBytes.size());
    payload.digest.bytes = d.finalize();
    out.payload = std::move(payload);
    return out;
}

// =====================================================================
// 注入替身（观测面——断言稳定码全部过 isRegisteredExStableCode 自检）
// =====================================================================

/// 诊断收集 sink（观测替身——非装配占位；装配占位见设施头 NullExecutionDiagnosticsSink）。
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
        // 断言自检闸（acceptance 4）：稳定码必须在本套件镜像的收编清单内
        // ——清单外码值出现在断言里即测试自身失败（防私定新码值）。
        EXPECT_TRUE(exsuite::isRegisteredExStableCode(code))
            << "断言使用了收编清单外的稳定码（P-EX-8 不私定）: " << code;
        for (const auto& r : reports) {
            if (r.code == code) {
                return true;
            }
        }
        return false;
    }
};

class MapNameResolver final : public INameResolverAdapter {
public:
    std::optional<core::ObjectId> tryResolve(core::ContentIdentity,
                                             std::string_view) const override
    {
        return oid(kHex32A);  // 名称面已由单元套件钉住——本组恒成功
    }
};

// =====================================================================
// 归档端口 fake：testkit FaultInterceptor 接缝的接线消费（§11 原文形态）
// =====================================================================

/**
 * @brief 故障注入归档端口——包装真实端口，按 FaultPlan 在登记的故障点
 *        第 N 次命中时注入 StoreError，其余调用全透传。
 *
 * 会话句柄防伪的承接（RunRegistryAdmissionTest::FaultInjectingPort 同款
 * 口径）：ArchiveSessionRef 只能由 project 内部构造——begin 恒透传真端口
 * 取得会话，注入只落在 writeBatch/finalize 的 ArchiveStatus 返回值上。
 * 与单元套件手写计数器版本的差异：本件以 testkit FaultInterceptor 驱动
 * （§11"FaultInterceptor 接缝"的 in-situ 消费——故障点标识、命中计划与
 * 命中日志全部走登记原语，命中日志可断言＝注入行为的观测面）。
 */
class FaultArchivePort final : public pd::IResultArchivePort {
public:
    /// 绑定真实端口与故障计划（real 非所有权——null 删除器 shared_ptr
    /// 仅为满足 FaultInterceptor 的 shared 所有权形参面；寿命由测试夹具
    /// 保证覆盖本对象）。
    FaultArchivePort(pd::IResultArchivePort& real, tk::FaultPlan plan,
                     pd::StoreErrorCode injectedCode)
        : m_fault(std::shared_ptr<pd::IResultArchivePort>(&real, [](pd::IResultArchivePort*) {}),
                  std::move(plan))
        , m_code(injectedCode)
    {
    }

    pd::ArchiveSessionRef begin(const pd::ArchiveRequest& request) override
    {
        ++m_beginCalls;
        return m_fault.real()->begin(request);  // 预留恒成功（EX-ARC-1/2 注入位＝写面）
    }

    pd::ArchiveStatus writeBatch(pd::ArchiveSessionRef session,
                                 const pd::ArchiveBatch& batch) override
    {
        ++m_writeCalls;
        if (m_fault.shouldFire(kFaultArchiveWriteBatch, m_writeCalls)) {
            // 注入：按构造指定的稳定错误码返回（disk-full＝EX-ARC-1，
            // write-rejected＝EX-ARC-2——错误语义与 §7.6 矩阵行对齐）。
            return pd::ArchiveStatus{false, pd::StoreError{m_code, "ex-t09 fake: fault plan"}};
        }
        return m_fault.real()->writeBatch(std::move(session), batch);
    }

    pd::ArchiveStatus finalize(pd::ArchiveSessionRef session,
                               const pd::RunManifest& manifest) override
    {
        ++m_finalizeCalls;
        return m_fault.real()->finalize(std::move(session), manifest);
    }

    void abandon(pd::ArchiveSessionRef session, pd::ArchiveEndReason reason) override
    {
        m_abandons.push_back(reason);  // 转发前记录——责任终结的观测面
        m_fault.real()->abandon(std::move(session), reason);
    }

    /// 故障点命中日志（FaultInterceptor 观测面——断言"注入按计划发生"）。
    tk::FaultLog faultLog() const { return m_fault.log(); }

    int writeCalls() const noexcept { return m_writeCalls; }
    const std::vector<pd::ArchiveEndReason>& abandons() const noexcept { return m_abandons; }

private:
    tk::FaultInterceptor<pd::IResultArchivePort> m_fault;  ///< §6.4 拦截器（计划＋日志）
    pd::StoreErrorCode m_code;                             ///< 注入的稳定错误码
    int m_beginCalls = 0;                                  ///< begin 透传计数
    int m_writeCalls = 0;                                  ///< writeBatch 命中计数（自 1 计）
    int m_finalizeCalls = 0;                               ///< finalize 透传计数
    std::vector<pd::ArchiveEndReason> m_abandons;          ///< abandon 观测
};

/// 归档网关（登记记录 resources.archive 的消费面——指向故障端口）。
class FaultGateway final : public IRunArchiveGateway {
public:
    explicit FaultGateway(FaultArchivePort& port)
        : m_port(&port)
    {
    }
    pd::IResultArchivePort& port() const override { return *m_port; }
    bool reacquireWriteAuthority() override { return false; }  // 本组不覆盖 A7 重开

private:
    FaultArchivePort* m_port;
};

// =====================================================================
// 夹具：真实存储＋登记表＋故障端口＋接纳编排全接线
// =====================================================================

class ArchiveFaultContractTest : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        std::error_code ec;
#ifdef _WIN32
        s_base = fs::temp_directory_path(ec) / "ird_ex_archive_fault"
                 / std::to_string(::GetCurrentProcessId());
#else
        s_base = fs::temp_directory_path(ec) / "ird_ex_archive_fault";
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
        fs::remove_all(s_base, ec);  // 失败保留现场惯例
    }

    void SetUp() override
    {
        // 确定性上下文（testkit §6.3）：repro 随本套件每条 TestRecord 携带
        // （RecordListener 聚合进 ird-test-report.json——acceptance 1）。
        tk::report::bindFixtureContext(m_repro, nullptr, nullptr);

        const int n = ++s_caseCounter;
        m_storeDir = s_base / ("case" + std::to_string(n)) / "p.rwdesign";
        m_store = pd::ProjectStoreFactory::createNew(m_storeDir, "项目P", nullptr, nullptr).store;
        m_branch = core::BranchId::generate();
        m_revision = core::RevisionId::generate();
        m_snapshot = std::make_shared<const ev::AnalysisSnapshot>(
            buildSnapshotFor(m_store->projectId(), m_branch, m_revision));
        m_profile = standardKinProfile();
        m_producers = std::make_shared<ScriptedProducerRegistry>();
        m_profiles = std::make_shared<ScriptedProfileRegistry>(m_profile);
        m_sink = std::make_unique<CollectingSink>();
        m_decoder = std::make_unique<exsuite::ScriptedOutputDecoder>();
        m_resolver = std::make_unique<MapNameResolver>();
        // 默认装配全透传端口（空计划＝零注入）；用例在 registerRun 之前
        // rebuild 换计划（登记捕获的是网关——先建端口再登记，避免悬挂）。
        rebuildFaultPort(pd::StoreErrorCode::DiskFull, {});
    }

    /// 重建故障端口（新计划＋新注入码）并回填网关（须先于 registerRun）。
    FaultArchivePort& rebuildFaultPort(pd::StoreErrorCode code, tk::FaultPlan plan)
    {
        m_faultPort = std::make_unique<FaultArchivePort>(m_store->archive(), std::move(plan), code);
        m_gateway = std::make_shared<FaultGateway>(*m_faultPort);
        return *m_faultPort;
    }

    /// 登记一次正式运行（接纳判定的事实面——ArchiveCollaboration 套件同构）。
    /// 登记的 TaskId 一并记入 m_lastTaskId（控制器状态查询的键——§4.2
    /// 主键；RunId 是运行身份，TaskId 是任务身份，两者不同轴）。
    core::TaskIdentity registerRun()
    {
        core::TaskIdentity id;
        id.project = m_store->projectId();
        id.branch = m_branch;
        id.revision = m_revision;
        id.run = core::RunId::generate();
        id.attempt = core::AttemptId{1};

        RegistrationInput in;
        in.identity = id;
        in.evaluatorKey = "kin-batch-ik";
        in.contractVersion = 7;
        in.snapshotId = m_snapshot->snapshotId;
        in.sliceId = cid(kHex64C);
        in.inputBaselineId = cid(kHex64B);
        in.policyIdentity = cid(kHex64A);
        in.nameMapIdentity = cid(kHex64B);
        in.mode = core::EvaluationMode::Verified;
        in.runDir = m_store->canonicalPath() / "results" / id.run.toCanonical();
        in.runKind = "kin-batch-eval";
        in.allowedResultKinds = {ResultKind::FinalEnvelope};
        in.currentState = core::TaskState::Running;

        auto materials = std::make_shared<RunEvaluationMaterials>();
        materials->snapshot = m_snapshot;
        materials->producers = m_producers;
        materials->profiles = m_profiles;
        materials->producedIn = ev::ProducerProcess::Worker;
        materials->readiness.valid = true;
        materials->snapshotGate.complete = true;
        materials->coverage = fullCoverage(*m_snapshot);
        materials->manifestProfile.profileId = m_profile.profileId;
        materials->manifestProfile.version = m_profile.version;
        materials->manifestProfile.contentIdentity = m_profile.contentIdentity;
        in.resources.evaluation = std::move(materials);
        in.resources.archive = m_gateway;

        m_lastTaskId = TaskId::generate();
        m_registry.registerRun(m_lastTaskId, in);
        return id;
    }

    /// 接纳编排（退避经记录器虚拟推进——不 sleep，testkit §6.5）。
    ResultAdmission makeAdmission(std::vector<std::chrono::milliseconds>* delays = nullptr)
    {
        ResultAdmission::Config config;  // D-11 缺省：2 次、1 s→4 s（断言值）
        auto delay = [delays](std::chrono::milliseconds d) {
            if (delays != nullptr) {
                delays->push_back(d);
            }
        };
        ResultAdmission admission(m_registry, *m_sink, nullptr, config, delay);
        admission.setOutputDecoder(m_decoder.get());
        admission.setNameResolver(m_resolver.get());
        return admission;
    }

    /// 合法 FinalEnvelope 到达（登记绑定值——九步 1~7 的核对面对端）。
    ArrivalEnvelope makeArrival(const core::TaskIdentity& id)
    {
        ArrivalEnvelope a;
        a.identity = id;
        a.snapshotId = m_snapshot->snapshotId;
        a.sliceId = cid(kHex64C);
        a.evaluatorKey = "kin-batch-ik";
        a.contractVersion = 7;
        a.policyIdentity = cid(kHex64A);
        a.nameMapIdentity = cid(kHex64B);
        a.kind = ResultKind::FinalEnvelope;
        a.payloadCanon = {0x01, 0x02, 0x03, 0x04};
        return a;
    }

    /// N 次命中计划（写面恒失败需覆盖重试预算：初次＋2 重试＝3 次调用）。
    static tk::FaultPlan writeFailPlan(const std::vector<std::uint64_t>& occurrences)
    {
        tk::FaultPlan plan;
        for (const std::uint64_t n : occurrences) {
            tk::FaultTrigger t;
            t.faultPointId = kFaultArchiveWriteBatch;
            t.occurrence = n;
            plan.triggers.push_back(t);
        }
        return plan;
    }

    static fs::path s_base;
    static int s_caseCounter;

    tk::ReproRecord m_repro;  ///< 确定性上下文（默认 seed=20260909——§6.3）
    fs::path m_storeDir;
    std::unique_ptr<pd::ProjectStore> m_store;
    core::BranchId m_branch;
    core::RevisionId m_revision;
    std::shared_ptr<const ev::AnalysisSnapshot> m_snapshot;
    ev::RequiredEvidenceProfile m_profile;
    std::shared_ptr<ScriptedProducerRegistry> m_producers;
    std::shared_ptr<ScriptedProfileRegistry> m_profiles;
    std::unique_ptr<CollectingSink> m_sink;
    std::unique_ptr<exsuite::ScriptedOutputDecoder> m_decoder;
    std::unique_ptr<MapNameResolver> m_resolver;
    std::unique_ptr<FaultArchivePort> m_faultPort;
    std::shared_ptr<FaultGateway> m_gateway;
    TaskId m_lastTaskId;  ///< 最近一次 registerRun 的任务身份（控制器查询键）
    RunRegistry m_registry;
};

fs::path ArchiveFaultContractTest::s_base;
int ArchiveFaultContractTest::s_caseCounter = 0;

// =====================================================================
// EX-ARC-1：磁盘不足——有界重试→abandon＋EX-ARCHIVE-FAILED；任务保持
// Completed；缓存跳过登记（§11 行预期结果全项）
// =====================================================================

TEST_F(ArchiveFaultContractTest, DiskFullBoundedRetryThenAbandonKeepsCompletedAndSkipsCache_EX_ARC_1)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-REL-01"}, std::vector<std::string>{"AT-10", "AT-13"});

    // 计划：写面第 1/2/3 次命中全部注入 disk-full——覆盖重试预算
    // （初次＋2 次，D-11）＝恒失败面。命中日志按注入序观测。
    (void)rebuildFaultPort(pd::StoreErrorCode::DiskFull, writeFailPlan({1, 2, 3}));
    const core::TaskIdentity id = registerRun();
    std::vector<std::chrono::milliseconds> delays;
    ResultAdmission admission = makeAdmission(&delays);
    m_decoder->enqueue(standardOutput());

    const AdmissionOutcome out = admission.admitResult(makeArrival(id));

    // 任务轴与归档轴正交（§5.6）：接纳成立、任务 Completed；归档责任以
    // abandon 终结、阶段 ArchiveFailed（"完成≠归档完成"）。
    EXPECT_EQ(out.decision.verdict, AdmissionDecision::Verdict::Admitted);
    EXPECT_EQ(out.taskStateAfter, core::TaskState::Completed);
    EXPECT_EQ(out.archivePhaseAfter, ArchivePhase::ArchiveFailed);
    EXPECT_EQ(admission.archivePhase(id.run), ArchivePhase::ArchiveFailed);

    // 有界性：恰 3 次写调用（初次＋2 重试）、2 次退避（1 s→4 s——D-11 值，
    // 经记录器虚拟推进，无 sleep）；abandon(Failed) 恰一次。
    EXPECT_EQ(m_faultPort->writeCalls(), 3);
    ASSERT_EQ(delays.size(), 2u);
    EXPECT_EQ(delays[0], std::chrono::milliseconds{1000});
    EXPECT_EQ(delays[1], std::chrono::milliseconds{4000});
    ASSERT_EQ(m_faultPort->abandons().size(), 1u);
    EXPECT_EQ(m_faultPort->abandons().front(), pd::ArchiveEndReason::Failed);

    // 注入按计划发生（FaultLog 观测：3 次命中、故障点＝登记 ID）。
    const tk::FaultLog log = m_faultPort->faultLog();
    ASSERT_EQ(log.hits.size(), 3u);
    EXPECT_EQ(log.hits.front().first, std::string{kFaultArchiveWriteBatch});

    // 稳定码断言（收编清单内——CollectingSink 内置自检闸）。
    EXPECT_TRUE(m_sink->hasReportCode("EX-ARCHIVE-FAILED"));

    // 磁盘事实：无 manifest＝未完成归档（D-13——残留目录不伪装完整）。
    EXPECT_FALSE(fs::exists(m_store->canonicalPath() / "results" / id.run.toCanonical()
                            / "manifest.json"));

    // 缓存跳过登记（§11 EX-ARC-1 行预期结果）：ArchiveFailed 不过 storeResult
    // 双门槛（Completed∧Archived）。对照闭环：正常归档的运行照常入账——
    // 证明门槛在工作（而非缓存整体失效）。
    ExecutionCacheCoordinator cache(m_registry, *m_sink);
    cache.storeResult(id.run);
    EXPECT_EQ(cache.stats().resultEntries, 0u)
        << "归档失败的运行不得进入正式结果缓存（CON-04——§11 EX-ARC-1 行）";

    (void)rebuildFaultPort(pd::StoreErrorCode::DiskFull, {});  // 对照运行：全透传
    m_decoder->enqueue(standardOutput());
    const core::TaskIdentity healthy = registerRun();
    ResultAdmission healthyAdmission = makeAdmission();
    const AdmissionOutcome okOut = healthyAdmission.admitResult(makeArrival(healthy));
    ASSERT_EQ(okOut.archivePhaseAfter, ArchivePhase::Archived);
    ExecutionCacheCoordinator healthyCache(m_registry, *m_sink);
    healthyCache.storeResult(healthy.run);
    EXPECT_EQ(healthyCache.stats().resultEntries, 1u) << "正常归档运行照常入账（对照）";
}

// =====================================================================
// EX-ARC-2：写入拒绝——StoreError 透传（瞬态失败重试成功／持续失败
// abandon 两形态同路径）
// =====================================================================

TEST_F(ArchiveFaultContractTest, WriteRejectedStoreErrorPassthroughBoundedPath_EX_ARC_2)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-REL-01"}, std::vector<std::string>{"AT-10", "AT-13"});

    // 形态 A（瞬态）：仅第 1 次写注入 write-rejected——错误从端口透传到
    // 重试决策（第 2 次真实写发生＝StoreError 被消费而非吞掉），退避
    // 1 s 后成功 finalize。§7.6"归档批次写失败"行的恢复动作面。
    (void)rebuildFaultPort(pd::StoreErrorCode::WriteRejected, writeFailPlan({1}));
    const core::TaskIdentity transient = registerRun();
    std::vector<std::chrono::milliseconds> delays;
    ResultAdmission admissionA = makeAdmission(&delays);
    m_decoder->enqueue(standardOutput());
    const AdmissionOutcome outA = admissionA.admitResult(makeArrival(transient));
    EXPECT_EQ(outA.decision.verdict, AdmissionDecision::Verdict::Admitted);
    EXPECT_EQ(outA.taskStateAfter, core::TaskState::Completed);
    EXPECT_EQ(outA.archivePhaseAfter, ArchivePhase::Archived);
    ASSERT_EQ(delays.size(), 1u);
    EXPECT_EQ(delays[0], std::chrono::milliseconds{1000});
    EXPECT_TRUE(fs::exists(m_store->canonicalPath() / "results"
                           / transient.run.toCanonical() / "manifest.json"))
        << "瞬态写拒绝经有界重试后归档完成";

    // 形态 B（持续）：预算内全失败——同路径收敛到 abandon＋EX-ARCHIVE-
    // FAILED，任务保持 Completed（与 EX-ARC-1 的 disk-full 同一有界责任
    // 终结路径——错误码不同、处置同构，§7.6 表"同上路径"）。
    (void)rebuildFaultPort(pd::StoreErrorCode::WriteRejected, writeFailPlan({1, 2, 3}));
    const core::TaskIdentity persistent = registerRun();
    ResultAdmission admissionB = makeAdmission();
    m_decoder->enqueue(standardOutput());
    const AdmissionOutcome outB = admissionB.admitResult(makeArrival(persistent));
    EXPECT_EQ(outB.decision.verdict, AdmissionDecision::Verdict::Admitted);
    EXPECT_EQ(outB.taskStateAfter, core::TaskState::Completed);
    EXPECT_EQ(outB.archivePhaseAfter, ArchivePhase::ArchiveFailed);
    ASSERT_EQ(m_faultPort->abandons().size(), 1u);
    EXPECT_EQ(m_faultPort->abandons().front(), pd::ArchiveEndReason::Failed);
    EXPECT_TRUE(m_sink->hasReportCode("EX-ARCHIVE-FAILED"));
    EXPECT_FALSE(fs::exists(m_store->canonicalPath() / "results"
                            / persistent.run.toCanonical() / "manifest.json"));
}

// =====================================================================
// EX-ARC-4（形态一）：UI 已关闭但归档仍在途——关闭后迟到完成仍归档至
// 原 runDir；drained() 之后存储上下文才可关闭（存活至 finalize；closed()
// 时序断言；全程无永久等待）
// =====================================================================

TEST_F(ArchiveFaultContractTest, LateCompletionArchivesAfterShutdownThenContextCloses_EX_ARC_4)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-03"}, std::vector<std::string>{"AT-10"});

    // 编排装配：TaskController（协议引擎）＋DrainCoordinator（排空编排）
    // ——生产形态的关闭方消费面；受管任务 Running 在途。（受管记录的
    // TaskId 与登记表的 TaskId 是两条独立任务记录——控制器查询键取
    // 受管记录自身的主键。）
    const core::TaskIdentity id = registerRun();
    TaskController controller;
    auto machine = std::make_unique<TaskStateMachine>(
        TaskStateMachine::forAcceptedSubmission(makeQueuedRecord(), nullptr));
    const TaskId controllerTaskId = machine->record().taskId;
    TaskStateMachine& task = *machine;
    controller.attachTask(std::move(machine));
    ASSERT_TRUE(task.request(TransitionTrigger::DispatchDequeued).accepted);  // T2
    task.bindRun(id.run);
    ASSERT_TRUE(task.request(TransitionTrigger::PrepareSucceeded).accepted);  // T5
    controller.poll();
    ASSERT_TRUE(controller.tryState(controllerTaskId).has_value());
    EXPECT_EQ(*controller.tryState(controllerTaskId), core::TaskState::Running);

    // UI 关闭（"等待"分支）：shutdown 置位——在途运行不立即干预，存储
    // 上下文不被关闭（关闭由执行侧终结路径驱动——§7.5）。
    DrainCoordinator drain(controller);
    drain.shutdown(DrainPolicy::CancelQueuedAndWait);
    ASSERT_TRUE(drain.closed());
    EXPECT_EQ(drain.policy(), DrainPolicy::CancelQueuedAndWait);
    EXPECT_FALSE(m_store->closed()) << "关闭期存储上下文必须存活（A7——finalize 前）";
    EXPECT_FALSE(drain.drained()) << "Running 在途＝未排空";

    // 完成事件迟到（UI 已关后到达）：接纳照常——归档至原 runDir（登记
    // 位置，S7/PM-13 同源），上下文存活兑现。
    ResultAdmission admission = makeAdmission();
    m_decoder->enqueue(standardOutput());
    const AdmissionOutcome out = admission.admitResult(makeArrival(id));
    ASSERT_EQ(out.decision.verdict, AdmissionDecision::Verdict::Admitted);
    EXPECT_EQ(out.archivePhaseAfter, ArchivePhase::Archived);
    EXPECT_TRUE(fs::exists(m_store->canonicalPath() / "results" / id.run.toCanonical()
                           / "manifest.json"));
    EXPECT_TRUE(task.request(TransitionTrigger::RunCompleted).accepted);  // T8
    controller.poll();
    EXPECT_TRUE(drain.drained()) << "在途归档 finalize＋任务终态后＝排空完成";
    EXPECT_FALSE(m_store->closed());

    // drained() 之后关闭上下文：requestClose 排空等待无在途票据——同步
    // 完成，无永久等待（PM-03/project §9.7 协作；closed() 时序＝finalize
    // 先于 Closed——EX-ARC-4 观测点）。
    m_store->requestClose();
    EXPECT_TRUE(m_store->closed());
}

// =====================================================================
// EX-ARC-4（形态二）：超阈值强制 abandon 兜底——虚拟时钟推进越阈即
// 自动兜底、任务显式终结、drained() 有界达成（无永久等待）
// =====================================================================

TEST_F(ArchiveFaultContractTest, ThresholdExceededForcesAbandonBoundedDrain_EX_ARC_4)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-03", "NFR-PERF-02"},
                  std::vector<std::string>{"AT-10", "AT-34"});

    // 虚拟时钟（ManualClock——§6.6；兜底阈值判定不得依赖真实时延）＋
    // 收窄兜底阈值（5 s＝实现参数，非上游值——仅取用例观测窗）。
    exsuite::ManualClock clock;
    TaskController controller(TaskController::Config{},
                              [&clock] { return clock.now(); });
    auto machine = std::make_unique<TaskStateMachine>(
        TaskStateMachine::forAcceptedSubmission(makeQueuedRecord(), nullptr));
    const TaskId taskId = machine->record().taskId;
    controller.attachTask(std::move(machine));
    TaskStateMachine* attached = nullptr;
    controller.forEachTask([&attached](TaskId, TaskStateMachine& m) { attached = &m; });
    ASSERT_NE(attached, nullptr);
    ASSERT_TRUE(attached->request(TransitionTrigger::DispatchDequeued).accepted);  // T2
    attached->bindRun(core::RunId::generate());
    ASSERT_TRUE(attached->request(TransitionTrigger::PrepareSucceeded).accepted);  // T5

    DrainCoordinator::Config drainConfig;
    drainConfig.abandonThreshold = std::chrono::milliseconds{5000};
    DrainCoordinator drain(controller, drainConfig, [&clock] { return clock.now(); });
    drain.shutdown(DrainPolicy::CancelQueuedAndWait);
    controller.poll();
    ASSERT_TRUE(drain.closed());
    ASSERT_FALSE(drain.drained()) << "Running 在途＝未排空（兜底未触发前）";

    // 虚拟推进越阈（shutdown 时刻起 5 s）→ poll 自动 abandonAllForced
    // （§7.5"L5 关闭控制器强制 abandonAll"的执行侧承载；自动兜底恰一次
    // ——重复 poll 不重复处置）。
    clock.advance(std::chrono::milliseconds{6000});
    drain.poll();
    // 强制命令经命令通道于下一拍命令段生效（TaskController 编排时序）——
    // 再驱动一拍后断言（均为显式驱动，无 sleep/阻塞）。
    drain.poll();

    // 任务显式终结（无 worker 强杀＝取消直达——§10.2 编排语义）＋排空
    // 达成：判定面为状态快照，全程虚拟时钟推进（无永久等待的判定面）。
    const std::optional<core::TaskState> state = controller.tryState(taskId);
    ASSERT_TRUE(state.has_value());
    EXPECT_TRUE(*state == core::TaskState::Canceled || *state == core::TaskState::Failed)
        << "兜底终结必须落到显式终态（不悬挂在 Canceling）";
    EXPECT_TRUE(drain.drained()) << "兜底后在途清零——排空有界达成";
}

}  // namespace
