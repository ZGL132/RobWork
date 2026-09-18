/**
 * @file   WorkerProcessContractTest.cpp
 * @brief  worker 真进程契约用例组——EX-WKR-1 正常完成走九步接纳（含握手
 *         manifest 比对）、EX-WKR-2 崩溃只失败当前任务、EX-WKR-3 心跳失联
 *         强杀路径、EX-CHN-1 真进程 seq 异常、分批流式回传（NFR-PERF-03）、
 *         worker 临时目录隔离（§6.4/§6.6）、R-3 句柄纪律实证（§6.6）。
 *
 * 设计依据：
 *   - units/execution.md §6.4（worker 生命周期/退出码约定集/池纪律/临时
 *     目录隔离/边界规则）、§6.5（IRDCHN/1 通道协议）、§6.6（Windows 平台
 *     保证——进程创建/作业/管道/退出码逐行口径）、§11（EX-WKR-1~3、
 *     EX-CHN-1 用例行；`_contract_test` 分工＝"真实 worker 进程场景"）
 *   - 需求 NFR-REL-02（worker 崩溃只失败当前任务——主进程存活、其他任务
 *     不受影响、崩溃 worker 永不回池）、NFR-PERF-02（超时路径）、
 *     NFR-PERF-03（分批流式、不整体装载）
 *   - 任务契约 tasks/foundation/EX-T06.json acceptance 1（EX-WKR-1~3 全绿）、
 *     2（EX-CHN-1 真进程半区）、3（分批流式＋临时目录隔离）、4（R-3 处置：
 *     句柄继承/强杀时序等逐项对照 Microsoft Learn 口径，真进程实证不超诺；
 *     P-EX-3 处置：派发物经 IExecutionModelService 值传递〔本套件的
 *     WorkerAssignment.dispatch 直接构造——注入接口的消费者形态〕）
 *
 * 替身边界声明（§11 同源纪律）：
 *   - worker 进程为**真实进程**（sdurws_ird_execution_worker exe，经
 *     ProcessLauncher 真实启动）——进程/管道/作业/退出码行为全部真实；
 *   - worker 内评估器为阶段 A 脚本替身（worker exe 内置——§3.4"阶段 A
 *     仅测试替身"），替身输出不构成业务算法正确性证明（R-7）；
 *   - 接纳侧解码器/名称反解器为接口替身（EX-T04 套件同款——接纳编排
 *     本体与 project 存储为真实件）；检查点的磁盘持久化编排归 EX-T08
 *     （PA-1 不伪造第二持久化路径——本套件验证通道面与崩溃隔离，
 *     EX-WKR-2"最近检查点保留"断言到"崩溃前收到的检查点字节完好可用"）。
 *
 * EX-T09 增量（任务契约 tasks/foundation/EX-T09.json——§11 矩阵补齐）：
 *   TempDirCleanupFailureKeepsTerminalStateAndReportsDev_EX_ARC_3（worker
 *   临时目录清理失败注入——真进程 OS 句柄锁注入，§6.6 EX-ARC-3 行）与
 *   DoubleDispatchSameTaskRejectedSingleActiveWorker_EX_RES_1（同任务双
 *   派发注入——登记互斥与单活动 worker 观测）。两用例沿用本文件夹具，
 *   设施与故障点标识登记见 ../test/ContractSuiteFacilities.hpp。
 *
 * 时钟纪律（testkit §6.5）：EX-WKR-3 的失联判定经注入 ManualClock 虚拟
 *   推进（不 sleep 断言）；泵循环的真实 Sleep 只用于 I/O 到达，不是时序
 *   判据。EX-CHN-1 的缺口超时以 400 ms 实现参数注入（有界实证，非需求值）。
 */

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/evidence/Evidence.hpp>
#include <sdurws/ird/evidence/Snapshot.hpp>
#include <sdurws/ird/execution/ChannelProtocol.hpp>
#include <sdurws/ird/execution/Errors.hpp>
#include <sdurws/ird/execution/Ports.hpp>
#include <sdurws/ird/execution/RunRegistry.hpp>
#include <sdurws/ird/execution/StateMachine.hpp>
#include <sdurws/ird/execution/TaskTypes.hpp>
#include <sdurws/ird/execution/WorkerSupervisor.hpp>
#include <sdurws/ird/project/ProjectStore.hpp>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
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
// 身份/取值辅助（EX-T04 套件同款确定性固定值——自持不共享）
// =====================================================================

const char* kHex32A = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
const char* kHex32B = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
const char* kHex32C = "cccccccccccccccccccccccccccccccc";
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

/// worker exe 路径（CMake 注入——真进程场景的启动目标）。
std::wstring workerExePath()
{
    static const std::wstring path = [] {
        const std::string narrow = IRD_EXECUTION_WORKER_EXE;
        return std::wstring(narrow.begin(), narrow.end());
    }();
    return path;
}

/// 阶段 A 装配清单文本（与 worker exe 内置值同源——握手比对基准）。
constexpr const char* kStageAManifest = "stage-a-double|1|7";

// =====================================================================
// 快照/Profile/判定素材（EX-T04 套件同构最小实例——接纳编排的真实面）
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
// 注入替身（EX-T04 套件同款——接纳侧解码/反解/诊断收集）
// =====================================================================

class CollectingSink final : public IExecutionDiagnosticsSink {
public:
    std::vector<core::DiagnosticRecord> reports;
    std::vector<std::pair<std::string, std::string>> devs;
    void report(const core::DiagnosticRecord& record) override { reports.push_back(record); }
    void reportDev(const std::string& channel, const std::string& message) override
    {
        devs.emplace_back(channel, message);
    }
};

class PresetDecoder final : public IEvaluationOutputDecoder {
public:
    std::optional<ev::EvaluationOutput> tryDecode(
        const std::vector<std::uint8_t>&) const override
    {
        return standardOutput();  // 替身：canonical 字节→固定合法产出（§11 边界声明）
    }
};

class MapNameResolver final : public INameResolverAdapter {
public:
    std::optional<core::ObjectId> tryResolve(core::ContentIdentity,
                                             std::string_view) const override
    {
        return oid(kHex32A);
    }
};

class StoreGateway final : public IRunArchiveGateway {
public:
    explicit StoreGateway(pd::ProjectStore& store)
        : m_store(&store)
    {
    }
    pd::IResultArchivePort& port() const override { return m_store->archive(); }
    bool reacquireWriteAuthority() override { return false; }

private:
    pd::ProjectStore* m_store;
};

// =====================================================================
// 监督器事件收集（poll 在测试线程调用——回调同线程，无并发）
// =====================================================================

class EventCollector final : public IWorkerEventSink {
public:
    std::vector<WorkerEvent> events;

    void onWorkerEvent(const WorkerEvent& event) override { events.push_back(event); }

    std::size_t countOf(WorkerEvent::Kind kind) const
    {
        return static_cast<std::size_t>(std::count_if(
            events.begin(), events.end(),
            [kind](const WorkerEvent& e) { return e.kind == kind; }));
    }

    const WorkerEvent* firstOf(WorkerEvent::Kind kind) const
    {
        for (const WorkerEvent& e : events) {
            if (e.kind == kind) {
                return &e;
            }
        }
        return nullptr;
    }
};

// =====================================================================
// 夹具：真实存储＋登记表＋监督器（每用例独立）
// =====================================================================

/// 状态机受理记录（EX-T02 单元套件同款——最小合法 Queued 记录）。
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

class WorkerProcessContractTest : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        std::error_code ec;
#ifdef _WIN32
        s_base = fs::temp_directory_path(ec) / "ird_ex_worker_contract"
                 / std::to_string(::GetCurrentProcessId());
#else
        s_base = fs::temp_directory_path(ec) / "ird_ex_worker_contract";
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
        fs::remove_all(s_base, ec);  // 失败保留现场
    }

    void SetUp() override
    {
        const int n = ++s_caseCounter;
        m_storeDir = s_base / ("case" + std::to_string(n)) / "p.rwdesign";
        m_store = pd::ProjectStoreFactory::createNew(m_storeDir, "项目P", nullptr, nullptr).store;
        m_branch = core::BranchId::generate();
        m_revision = core::RevisionId::generate();
        m_snapshot = std::make_shared<const ev::AnalysisSnapshot>(
            buildSnapshotFor(m_store->projectId(), m_branch, m_revision));
        m_producers = std::make_shared<ScriptedProducerRegistry>();
        m_profiles = std::make_shared<ScriptedProfileRegistry>(standardKinProfile());
        m_devSink = std::make_unique<CollectingSink>();
    }

    /// 登记一次正式运行（EX-T04 套件同款——接纳判定的登记事实面）。
    core::TaskIdentity registerRun(core::EvaluationMode mode = core::EvaluationMode::Verified)
    {
        core::TaskIdentity id;
        id.project = m_store->projectId();
        id.branch = m_branch;
        id.revision = m_revision;
        id.run = core::RunId::generate();
        id.attempt = core::AttemptId{1};

        m_registry.registerRun(TaskId::generate(), makeRegistrationInput(id, mode));
        return id;
    }

    /// 构造一份登记输入（registerRun 的内容面；EX-RES-1 复刻同 run 输入
    /// 作重复登记的注入载体）。
    RegistrationInput makeRegistrationInput(const core::TaskIdentity& id,
                                            core::EvaluationMode mode = core::EvaluationMode::Verified) const
    {
        RegistrationInput in;
        in.identity = id;
        in.evaluatorKey = "kin-batch-ik";
        in.contractVersion = 7;
        in.snapshotId = m_snapshot->snapshotId;
        in.sliceId = cid(kHex64C);
        in.inputBaselineId = cid(kHex64B);
        in.policyIdentity = cid(kHex64A);
        in.nameMapIdentity = cid(kHex64B);
        in.mode = mode;
        in.runDir = m_store->canonicalPath() / "results" / id.run.toCanonical();
        in.runKind = "kin-batch-eval";
        in.allowedResultKinds = {ResultKind::FinalEnvelope, ResultKind::ResultBatch,
                                 ResultKind::CheckpointBatch, ResultKind::Progress};
        in.currentState = core::TaskState::Running;

        auto materials = std::make_shared<RunEvaluationMaterials>();
        materials->snapshot = m_snapshot;
        materials->producers = m_producers;
        materials->profiles = m_profiles;
        materials->producedIn = ev::ProducerProcess::Worker;
        materials->readiness.valid = true;
        materials->snapshotGate.complete = true;
        materials->coverage = fullCoverage(*m_snapshot);
        materials->manifestProfile.profileId = "kin";
        materials->manifestProfile.version = "1.0.0";
        materials->manifestProfile.contentIdentity = standardKinProfile().contentIdentity;
        in.resources.evaluation = std::move(materials);
        in.resources.archive = std::make_shared<StoreGateway>(*m_store);
        return in;
    }

    /// 构造一次派发绑定（脚本承载于"快照字节"——阶段 A 替身评估器约定）。
    WorkerAssignment makeAssignment(const core::TaskIdentity& id, const std::string& script,
                                    HeartbeatPolicy heartbeat = HeartbeatPolicy{})
    {
        WorkerAssignment a;
        a.identity = id;
        a.evaluatorKey = "stage-a-double";
        a.dispatch.snapshotBytes.assign(script.begin(), script.end());
        a.dispatch.modelBytes = {0xC0, 0xDE};
        a.dispatch.snapshotIdentity = computeContentIdentity(a.dispatch.snapshotBytes);
        a.dispatch.modelIdentity = computeContentIdentity(a.dispatch.modelBytes);
        a.heartbeat = heartbeat;
        a.manifestDigest = manifestDigestHex(kStageAManifest);
        return a;
    }

    /// 造监督器（worker exe 路径＋可选配置/时钟——每用例独立生命周期）。
    std::unique_ptr<WorkerSupervisor> makeSupervisor(
        WorkerSupervisor::Config config = {}, WorkerSupervisor::ClockFn clock = nullptr)
    {
        auto supervisor =
            std::make_unique<WorkerSupervisor>(workerExePath(), config, std::move(clock));
        supervisor->setEventSink(&m_events);
        return supervisor;
    }

    /// 泵循环（真实 Sleep 只为 I/O 到达——时序判据全部来自事件与注入时钟）。
    template <typename Pred>
    bool pumpUntil(WorkerSupervisor& supervisor, Pred done, int timeoutMs = 60000)
    {
        const auto deadline = std::chrono::steady_clock::now()
                              + std::chrono::milliseconds(timeoutMs);
        for (;;) {
            supervisor.poll();
            if (done()) {
                return true;
            }
            if (std::chrono::steady_clock::now() > deadline) {
                m_lastPumpEvents.clear();  // 失败现场转储（断言信息用）
                for (const WorkerEvent& e : m_events.events) {
                    m_lastPumpEvents.push_back(e.kind);
                }
                return false;
            }
            ::Sleep(10);
        }
    }

    /// 最近一次泵失败时已收到的事件类别（诊断转储——断言消息拼装用）。
    std::vector<WorkerEvent::Kind> m_lastPumpEvents;

    /// 事件类别的可读名（诊断转储）。
    static const char* eventName(WorkerEvent::Kind kind)
    {
        switch (kind) {
        case WorkerEvent::Kind::HandshakeRejected: return "HandshakeRejected";
        case WorkerEvent::Kind::DispatchAccepted: return "DispatchAccepted";
        case WorkerEvent::Kind::Progress: return "Progress";
        case WorkerEvent::Kind::ResultBatch: return "ResultBatch";
        case WorkerEvent::Kind::CheckpointBatch: return "CheckpointBatch";
        case WorkerEvent::Kind::FinalOutput: return "FinalOutput";
        case WorkerEvent::Kind::ErrorReport: return "ErrorReport";
        case WorkerEvent::Kind::ProtocolViolation: return "ProtocolViolation";
        case WorkerEvent::Kind::WorkerHung: return "WorkerHung";
        case WorkerEvent::Kind::CancelAck: return "CancelAck";
        case WorkerEvent::Kind::PauseAck: return "PauseAck";
        case WorkerEvent::Kind::WorkerExited: return "WorkerExited";
        }
        return "?";
    }

    /// 到达信封构造（登记绑定值——接纳步 5 的第二道核对面对端）。
    ArrivalEnvelope makeArrival(const core::TaskIdentity& id, ResultKind kind,
                                std::vector<std::uint8_t> payload)
    {
        ArrivalEnvelope a;
        a.identity = id;
        a.snapshotId = m_snapshot->snapshotId;
        a.sliceId = cid(kHex64C);
        a.evaluatorKey = "kin-batch-ik";
        a.contractVersion = 7;
        a.policyIdentity = cid(kHex64A);
        a.nameMapIdentity = cid(kHex64B);
        a.kind = kind;
        a.payloadCanon = std::move(payload);
        return a;
    }

    ResultAdmission makeAdmission()
    {
        ResultAdmission admission(m_registry, *m_devSink);
        admission.setOutputDecoder(&m_decoder);
        admission.setNameResolver(&m_resolver);
        return admission;
    }

    static fs::path s_base;
    static int s_caseCounter;

    fs::path m_storeDir;
    std::unique_ptr<pd::ProjectStore> m_store;
    core::BranchId m_branch;
    core::RevisionId m_revision;
    std::shared_ptr<const ev::AnalysisSnapshot> m_snapshot;
    std::shared_ptr<ScriptedProducerRegistry> m_producers;
    std::shared_ptr<ScriptedProfileRegistry> m_profiles;
    std::unique_ptr<CollectingSink> m_devSink;
    RunRegistry m_registry;
    PresetDecoder m_decoder;
    MapNameResolver m_resolver;
    EventCollector m_events;
};

fs::path WorkerProcessContractTest::s_base;
int WorkerProcessContractTest::s_caseCounter = 0;

// =====================================================================
// EX-WKR-1：worker 正常完成——握手→派发→流式回传→九步接纳→Completed
// =====================================================================

TEST_F(WorkerProcessContractTest, WorkerCompletesViaHandshakeAndNineStepAdmission_EX_WKR_1)
{
    // 排布（§6.4 时序图）：登记→launch（握手 manifest 比对基准＝阶段 A
    // 装配清单摘要）→脚本编排 progress/检查点/批次/final→FinalOutput 到达
    // 后走真实九步接纳（真实 ProjectStore 归档）。
    const core::TaskIdentity id = registerRun();
    TaskStateMachine machine =
        TaskStateMachine::forAcceptedSubmission(makeQueuedRecord(), nullptr);
    ASSERT_TRUE(machine.request(TransitionTrigger::DispatchDequeued).accepted);  // T2
    machine.bindRun(id.run);  // §4.3 派发登记绑定

    WorkerSupervisor::Config config;
    auto supervisor = makeSupervisor(config);
    const WorkerAssignment assignment =
        makeAssignment(id, "stage-a-script:v1\n"
                           "progress 40 kin.batch 1 3\n"
                           "checkpoint 1\n"
                           "batches 1\n"
                           "final\n");
    const WorkerLaunchResult launched = supervisor->launch(assignment);
    ASSERT_TRUE(launched.ok) << "worker 进程启动＋Job Scope 绑定应成功";
    ASSERT_TRUE(launched.worker.isValid());

    // 握手成功面：DispatchAccept 到达（HelloAck 通过＝manifest 摘要一致——
    // §6.4 握手比对的对端实证）。
    ASSERT_TRUE(pumpUntil(*supervisor,
                          [this] { return m_events.firstOf(WorkerEvent::Kind::DispatchAccepted)
                                          != nullptr; }))
        << "握手应在有界时间内完成；已收到事件＝"
        << [&] {
               std::string names;
               for (const WorkerEvent::Kind k : m_lastPumpEvents) {
                   names += eventName(k);
                   names += " ";
               }
               if (names.empty()) {
                   names = "(none)";
               }
               return names;
           }();
    ASSERT_EQ(m_events.countOf(WorkerEvent::Kind::HandshakeRejected), 0u);

    const WorkerStatus running = supervisor->status(launched.worker);
    EXPECT_EQ(running.phase, WorkerStatus::Phase::Running);
    EXPECT_NE(running.pid, 0u);

    // 编排侧时点（§6.4 时序图：派发受理→Preparing→Running——调度器在
    // 握手完成、派发送出后驱动 T5；本套件以测试线程充当编排）。
    EXPECT_TRUE(machine.request(TransitionTrigger::PrepareSucceeded).accepted);
    EXPECT_EQ(machine.state(), core::TaskState::Running);

    // 等待最终产出（分帧续传的 FinalOutput——canonical 分片重组后到达）。
    ASSERT_TRUE(pumpUntil(*supervisor, [this] {
        return m_events.firstOf(WorkerEvent::Kind::FinalOutput) != nullptr;
    })) << "FinalOutput 应在有界时间内到达";

    // 流式回传次序：进度→检查点→批次在前、最终产出在后（§6.5 分批流式）。
    ASSERT_NE(m_events.firstOf(WorkerEvent::Kind::Progress), nullptr);
    ASSERT_NE(m_events.firstOf(WorkerEvent::Kind::CheckpointBatch), nullptr);
    EXPECT_EQ(m_events.firstOf(WorkerEvent::Kind::CheckpointBatch)->checkpointSequence, 1u);
    ASSERT_EQ(m_events.countOf(WorkerEvent::Kind::ResultBatch), 1u);
    EXPECT_FALSE(m_events.firstOf(WorkerEvent::Kind::FinalOutput)->bytes.empty());

    // R-3 观测面：启动完成后主进程侧无可继承句柄副本残留（§6.6 防泄漏）。
    EXPECT_FALSE(supervisor->hasOpenChildHandleDuplicates(launched.worker));

    // 最近检查点在主进程侧完好（EX-T06 通道面——磁盘持久化编排归 EX-T08）。
    const WorkerEvent* checkpoint = m_events.firstOf(WorkerEvent::Kind::CheckpointBatch);
    EXPECT_EQ(checkpoint->bytes, (std::vector<std::uint8_t>{'B', 'A', 'T', 'C', 'H', ':', '1'}));

    // ---- 九步接纳（真实 ResultAdmission＋真实 ProjectStore——EX-T04 本体）----
    ResultAdmission admission = makeAdmission();
    for (const WorkerEvent& e : m_events.events) {
        if (e.kind == WorkerEvent::Kind::ResultBatch) {
            const AdmissionOutcome out = admission.admitResult(
                makeArrival(id, ResultKind::ResultBatch, e.bytes));
            EXPECT_EQ(out.decision.verdict, AdmissionDecision::Verdict::Admitted);
        }
    }
    const AdmissionOutcome finalOut = admission.admitResult(
        makeArrival(id, ResultKind::FinalEnvelope,
                    m_events.firstOf(WorkerEvent::Kind::FinalOutput)->bytes));
    EXPECT_EQ(finalOut.decision.verdict, AdmissionDecision::Verdict::Admitted);
    EXPECT_EQ(finalOut.taskStateAfter, core::TaskState::Completed);
    EXPECT_EQ(finalOut.archivePhaseAfter, ArchivePhase::Archived);

    // 归档 finalize 落盘（manifest 发布——project D-13 可见性）。
    EXPECT_TRUE(fs::exists(m_store->canonicalPath() / "results" / id.run.toCanonical()
                           / "manifest.json"));
    // 任务状态机 T8（编排侧驱动——admission 投影与状态机一致）。
    EXPECT_TRUE(machine.request(TransitionTrigger::RunCompleted).accepted);
    EXPECT_EQ(machine.state(), core::TaskState::Completed);

    // worker 正常退出（0）→回收池（Idle——§6.4 池化，与"崩溃不回池"对照）。
    ASSERT_TRUE(pumpUntil(*supervisor, [this] {
        const WorkerEvent* exited = m_events.firstOf(WorkerEvent::Kind::WorkerExited);
        return exited != nullptr;
    }));
    const WorkerEvent* exited = m_events.firstOf(WorkerEvent::Kind::WorkerExited);
    EXPECT_EQ(exited->exit, ExitClassification::NormalCompletion);
    EXPECT_EQ(exited->exitCode, 0u);
    EXPECT_EQ(supervisor->status(launched.worker).phase, WorkerStatus::Phase::Idle);
}

// =====================================================================
// 握手 manifest 比对（EX-WKR-1 括注——不一致→拒绝派发→Preparing→Failed）
// =====================================================================

TEST_F(WorkerProcessContractTest, HandshakeManifestMismatchRejectsDispatch)
{
    const core::TaskIdentity id = registerRun();
    TaskStateMachine machine =
        TaskStateMachine::forAcceptedSubmission(makeQueuedRecord(), nullptr);
    ASSERT_TRUE(machine.request(TransitionTrigger::DispatchDequeued).accepted);
    machine.bindRun(id.run);

    auto supervisor = makeSupervisor();
    WorkerAssignment assignment = makeAssignment(id, "stage-a-script:v1\nfinal\n");
    assignment.manifestDigest = std::string(64, '0');  // 装配漂移面——必不匹配
    ASSERT_TRUE(supervisor->launch(assignment).ok);

    ASSERT_TRUE(pumpUntil(*supervisor, [this] {
        return m_events.firstOf(WorkerEvent::Kind::HandshakeRejected) != nullptr;
    })) << "manifest 失配应被握手拒绝";
    // 拒绝派发：无 DispatchAccept、无任何结果帧。
    EXPECT_EQ(m_events.countOf(WorkerEvent::Kind::DispatchAccepted), 0u);
    EXPECT_EQ(m_events.countOf(WorkerEvent::Kind::FinalOutput), 0u);

    // 编排侧（调度器职责）：Preparing→Failed（T7）。
    EXPECT_TRUE(machine.request(TransitionTrigger::PrepareFailed).accepted);
    EXPECT_EQ(machine.state(), core::TaskState::Failed);

    // 拒绝后 worker 被监督器终结（不可服务的进程不留）。
    ASSERT_TRUE(pumpUntil(*supervisor, [this] {
        return m_events.firstOf(WorkerEvent::Kind::WorkerExited) != nullptr;
    }));
    EXPECT_EQ(supervisor->status(
                  m_events.firstOf(WorkerEvent::Kind::WorkerExited)->worker)
                  .phase,
              WorkerStatus::Phase::Dead);
    // 主进程存活面：status/list 仍可服务。
    EXPECT_FALSE(supervisor->list().empty());
}

// =====================================================================
// EX-WKR-2：worker 崩溃只失败当前任务（NFR-REL-02——隔离的结构实证）
// =====================================================================

TEST_F(WorkerProcessContractTest, WorkerCrashFailsOnlyItsOwnTask_EX_WKR_2)
{
    // 排布：任务 A（检查点×2 后真崩溃——0xC0000005）与任务 B（正常完成）
    // 各自独立 worker；A 崩溃后 B 仍完成、排队任务 C 不受影响、主进程存活。
    const core::TaskIdentity idA = registerRun();
    const core::TaskIdentity idB = registerRun();
    TaskStateMachine machineA =
        TaskStateMachine::forAcceptedSubmission(makeQueuedRecord(), nullptr);
    TaskStateMachine machineB =
        TaskStateMachine::forAcceptedSubmission(makeQueuedRecord(), nullptr);
    for (TaskStateMachine* m : {&machineA, &machineB}) {
        ASSERT_TRUE(m->request(TransitionTrigger::DispatchDequeued).accepted);
    }
    machineA.bindRun(idA.run);
    machineB.bindRun(idB.run);

    auto supervisor = makeSupervisor();
    const WorkerLaunchResult launchA = supervisor->launch(
        makeAssignment(idA, "stage-a-script:v1\ncheckpoint 1\ncheckpoint 2\ncrash\n"));
    ASSERT_TRUE(launchA.ok);

    // A 的检查点先于崩溃到达并完好捕获（"最近检查点保留"的通道面——
    // 磁盘持久化编排归 EX-T08，PA-1 不伪造第二持久化路径）。
    ASSERT_TRUE(pumpUntil(*supervisor, [this] {
        return m_events.countOf(WorkerEvent::Kind::CheckpointBatch) >= 2;
    }));
    // 编排侧时点：派发受理即 T5（Preparing→Running——调度器在握手后驱动；
    // T9 的合法性依赖 Running 相）。
    for (TaskStateMachine* m : {&machineA, &machineB}) {
        EXPECT_TRUE(m->request(TransitionTrigger::PrepareSucceeded).accepted);
    }
    std::vector<std::vector<std::uint8_t>> checkpointsBeforeCrash;
    for (const WorkerEvent& e : m_events.events) {
        if (e.kind == WorkerEvent::Kind::CheckpointBatch) {
            checkpointsBeforeCrash.push_back(e.bytes);
        }
    }
    ASSERT_EQ(checkpointsBeforeCrash.size(), 2u);

    // B 在 A 崩溃之前启动（同池并存）。
    const WorkerLaunchResult launchB =
        supervisor->launch(makeAssignment(idB, "stage-a-script:v1\nbatches 1\nfinal\n"));
    ASSERT_TRUE(launchB.ok);

    // A 真崩溃（真实访问违例族退出码——约定集外→Crashed，§6.4 表）。
    ASSERT_TRUE(pumpUntil(*supervisor, [this, &launchA] {
        for (const WorkerEvent& e : m_events.events) {
            if (e.kind == WorkerEvent::Kind::WorkerExited && e.worker == launchA.worker) {
                return true;
            }
        }
        return false;
    }));
    const WorkerEvent* crashEvent = nullptr;
    for (const WorkerEvent& e : m_events.events) {
        if (e.kind == WorkerEvent::Kind::WorkerExited && e.worker == launchA.worker) {
            crashEvent = &e;
        }
    }
    ASSERT_NE(crashEvent, nullptr);
    EXPECT_EQ(crashEvent->exit, ExitClassification::Crashed);
    EXPECT_EQ(crashEvent->exitCode, 0xC0000005u);  // 真进程异常码（脚本 crash 注入）

    // 仅当前任务失败：A 走 T9＋EX-WORKER-CRASHED 判定诊断（不伪装普通
    // 失败的显式标记——NFR-REL-02/D-02）。
    EXPECT_TRUE(machineA.request(TransitionTrigger::RunFailed).accepted);
    core::DiagnosticRecord crashDiag = core::DiagnosticRecord::make(
        "EX-WORKER-CRASHED", std::nullopt, std::nullopt, std::nullopt,
        "execution/worker", "worker crashed with exit code 0xC0000005",
        "任务可从最近检查点重跑");
    machineA.appendDiagnostic(crashDiag);
    EXPECT_EQ(machineA.state(), core::TaskState::Failed);
    ASSERT_TRUE(machineA.record().termination.has_value());
    EXPECT_EQ(*machineA.record().termination, TerminationCause::Failed);
    bool hasCrashCode = false;
    for (const core::DiagnosticRecord& d : machineA.record().diagnostics) {
        if (d.code == "EX-WORKER-CRASHED") {
            hasCrashCode = true;
        }
    }
    EXPECT_TRUE(hasCrashCode);

    // 检查点字节在崩溃后完好可用（保留语义的通道面断言）。
    std::vector<std::vector<std::uint8_t>> checkpointsAfterCrash;
    for (const WorkerEvent& e : m_events.events) {
        if (e.kind == WorkerEvent::Kind::CheckpointBatch) {
            checkpointsAfterCrash.push_back(e.bytes);
        }
    }
    EXPECT_EQ(checkpointsAfterCrash, checkpointsBeforeCrash);

    // 其他任务不受影响：B 仍在崩溃后完成并接纳（T8 Completed）。
    ASSERT_TRUE(pumpUntil(*supervisor, [this] {
        return m_events.firstOf(WorkerEvent::Kind::FinalOutput) != nullptr;
    }));
    ResultAdmission admission = makeAdmission();
    for (const WorkerEvent& e : m_events.events) {
        if (e.kind == WorkerEvent::Kind::ResultBatch) {
            (void)admission.admitResult(makeArrival(idB, ResultKind::ResultBatch, e.bytes));
        }
    }
    const AdmissionOutcome outB = admission.admitResult(
        makeArrival(idB, ResultKind::FinalEnvelope,
                    m_events.firstOf(WorkerEvent::Kind::FinalOutput)->bytes));
    EXPECT_EQ(outB.decision.verdict, AdmissionDecision::Verdict::Admitted);
    EXPECT_EQ(outB.taskStateAfter, core::TaskState::Completed);
    EXPECT_TRUE(machineB.request(TransitionTrigger::RunCompleted).accepted);

    // 崩溃的 worker 永不回池：A 记录 Dead；后续新任务拿到的进程绝不是
    // A 的 pid（§6.4 池纪律——新任务新进程）。
    EXPECT_EQ(supervisor->status(launchA.worker).phase, WorkerStatus::Phase::Dead);
    const core::TaskIdentity idC = registerRun();
    const WorkerLaunchResult launchC =
        supervisor->launch(makeAssignment(idC, "stage-a-script:v1\nfinal\n"));
    ASSERT_TRUE(launchC.ok);
    EXPECT_NE(supervisor->status(launchC.worker).pid,
              supervisor->status(launchA.worker).pid)
        << "崩溃 worker 的进程不得被复用";
    EXPECT_EQ(supervisor->status(launchA.worker).phase, WorkerStatus::Phase::Dead);

    // 主进程存活面：监督器仍可列出全部 worker（A=Dead、B=Idle/复用、C=Running）。
    const std::vector<WorkerStatus> all = supervisor->list();
    EXPECT_GE(all.size(), 2u);

    // 收尾：C 完成释放（避免析构强杀挂起进程的等待）。
    (void)pumpUntil(*supervisor, [this] {
        return m_events.countOf(WorkerEvent::Kind::WorkerExited) >= 2;
    }, 30000);
}

// =====================================================================
// EX-WKR-3：worker 卡死经心跳失联判定走强杀路径（§7.4；D-06 实现参数）
// =====================================================================

TEST_F(WorkerProcessContractTest, HeartbeatLossTriggersHungAndForceKill_EX_WKR_3)
{
    const core::TaskIdentity id = registerRun();
    TaskStateMachine machine =
        TaskStateMachine::forAcceptedSubmission(makeQueuedRecord(), nullptr);
    ASSERT_TRUE(machine.request(TransitionTrigger::DispatchDequeued).accepted);
    machine.bindRun(id.run);

    // 注入时钟（ManualClock——失联判定的虚拟推进，不 sleep 断言）＋缩小
    // 心跳策略（间隔 100 ms×3＝300 ms 判定窗——参数可配，D-06）。
    struct ManualClock {
        std::chrono::steady_clock::time_point now{std::chrono::steady_clock::now()
                                                  + std::chrono::hours(1)};
        std::chrono::steady_clock::time_point operator()() { return now; }
    } clockTick;
    WorkerSupervisor::Config config;
    auto supervisor = makeSupervisor(
        config, [&clockTick]() { return clockTick.now; });

    HeartbeatPolicy policy;
    policy.interval = std::chrono::milliseconds{100};
    policy.lossThresholdIntervals = 3;
    const WorkerLaunchResult launched = supervisor->launch(
        makeAssignment(id, "stage-a-script:v1\nheartbeat-off\nhang\n", policy));
    ASSERT_TRUE(launched.ok);

    // 派发受理（真进程握手完成）——判定窗计时起点。编排侧 T5（Running
    // 相——失联判定与 T9 的合法性前提）。
    ASSERT_TRUE(pumpUntil(*supervisor, [this] {
        return m_events.firstOf(WorkerEvent::Kind::DispatchAccepted) != nullptr;
    }));
    EXPECT_TRUE(machine.request(TransitionTrigger::PrepareSucceeded).accepted);
    const IWorkerHandle* handle = supervisor->workerHandle(launched.worker);
    EXPECT_TRUE(handle->isAlive()) << "卡死注入下 worker 进程真实存活（强杀对象为活进程）";

    // 虚拟推进越过失联判定窗（3×100 ms）→WorkerHung 判定＋自动强杀。
    clockTick.now += std::chrono::milliseconds{350};
    ASSERT_TRUE(pumpUntil(*supervisor, [this] {
        return m_events.firstOf(WorkerEvent::Kind::WorkerHung) != nullptr;
    }));
    const WorkerEvent* hung = m_events.firstOf(WorkerEvent::Kind::WorkerHung);
    EXPECT_EQ(hung->stableCode, "EX-WORKER-HUNG");
    // 诊断码区分（acceptance 1）：心跳失联路径携带 heartbeat-loss 标记，
    // 与运行超时路径的 evaluationTimeout 标记互斥（EX-WKR-5 归 Controller）。
    EXPECT_NE(hung->detail.find("heartbeat-loss"), std::string::npos);
    EXPECT_EQ(hung->detail.find("evaluationTimeout"), std::string::npos);

    // 强杀路径终结（真实 TerminateJobObject 作用于真实挂起进程）。
    ASSERT_TRUE(pumpUntil(*supervisor, [this, &launched] {
        for (const WorkerEvent& e : m_events.events) {
            if (e.kind == WorkerEvent::Kind::WorkerExited && e.worker == launched.worker) {
                return true;
            }
        }
        return false;
    }));
    const WorkerEvent* exited = nullptr;
    for (const WorkerEvent& e : m_events.events) {
        if (e.kind == WorkerEvent::Kind::WorkerExited && e.worker == launched.worker) {
            exited = &e;
        }
    }
    ASSERT_NE(exited, nullptr);
    EXPECT_EQ(exited->exit, ExitClassification::ForceTerminatedBySupervisor);
    EXPECT_FALSE(supervisor->workerHandle(launched.worker)->isAlive())
        << "强杀后进程存活探测必须为 false（EX-WKR-4 观测点）";

    // 编排侧：T9→Failed＋EX-FORCE-TERMINATED（不伪装普通失败——判定诊断
    // EX-WORKER-HUNG 先于终结标记，因果序与 Controller 同构）。
    core::DiagnosticRecord hungDiag = core::DiagnosticRecord::make(
        "EX-WORKER-HUNG", std::nullopt, std::nullopt, std::nullopt, "execution/worker",
        "heartbeat loss beyond 3x100 ms (heartbeat-loss path)", "检查评估器是否长阻塞");
    machine.appendDiagnostic(hungDiag);
    core::DiagnosticRecord forceDiag = core::DiagnosticRecord::make(
        "EX-FORCE-TERMINATED", std::nullopt, std::nullopt, std::nullopt, "execution/worker",
        "worker process tree terminated by supervisor", "任务可重跑");
    machine.appendDiagnostic(forceDiag);
    EXPECT_TRUE(machine.request(TransitionTrigger::RunFailed).accepted);
    EXPECT_EQ(*machine.record().termination, TerminationCause::Failed);

    // 永不回池：Dead 记录（复用查找不扫 Dead）。
    EXPECT_EQ(supervisor->status(launched.worker).phase, WorkerStatus::Phase::Dead);
}

// =====================================================================
// EX-CHN-1（真进程半区）：重复帧去重＋缺口断裂→协议错误→任务失败
// =====================================================================

TEST_F(WorkerProcessContractTest, SequenceGapBreaksChannelAndFailsTask_EX_CHN_1)
{
    const core::TaskIdentity id = registerRun();
    TaskStateMachine machine =
        TaskStateMachine::forAcceptedSubmission(makeQueuedRecord(), nullptr);
    ASSERT_TRUE(machine.request(TransitionTrigger::DispatchDequeued).accepted);
    machine.bindRun(id.run);

    // 缺口超时以 400 ms 实现参数注入（有界实证——FrameSequencer 缺省
    // 10 s 是长任务口径，此处为用例观测窗口）。
    WorkerSupervisor::Config config;
    config.channelSequencer.gapTimeout = std::chrono::milliseconds{400};
    auto supervisor = makeSupervisor(config);
    const WorkerLaunchResult launched = supervisor->launch(makeAssignment(
        id,
        "stage-a-script:v1\n"
        "progress 10 p 1 2\n"
        "duplicate\n"   // 重复 seq——去重＋开发诊断（非致命，通道继续）
        "progress 20 p 1 2\n"
        "gap\n"         // 缺 3 个 seq——缓冲＋缺口超时→断裂（致命）
        "hang\n"));
    ASSERT_TRUE(launched.ok);

    // 编排侧时点：派发即 T5（Running 相——T9 的合法性前提；断裂判定在
    // Running 相进行，失联判定同源边界）。
    EXPECT_TRUE(machine.request(TransitionTrigger::PrepareSucceeded).accepted);

    ASSERT_TRUE(pumpUntil(*supervisor, [this] {
        // 重复帧的开发诊断先到（fatal=false），随后缺口断裂（fatal=true）。
        for (const WorkerEvent& e : m_events.events) {
            if (e.kind == WorkerEvent::Kind::ProtocolViolation && e.fatal) {
                return true;
            }
        }
        return false;
    }, 30000));

    // 重复帧处置：丢弃＋开发诊断（EX-CHANNEL-PROTOCOL-ERROR，非致命）。
    bool sawDuplicateDev = false;
    for (const WorkerEvent& e : m_events.events) {
        if (e.kind == WorkerEvent::Kind::ProtocolViolation && !e.fatal) {
            sawDuplicateDev = true;
            EXPECT_EQ(e.stableCode, "EX-CHANNEL-PROTOCOL-ERROR");
        }
    }
    EXPECT_TRUE(sawDuplicateDev) << "重复 seq 应产生非致命开发诊断";
    // 缺口断裂：fatal 协议错误（断裂→协议错误→尝试 Failed——§6.5）。
    const WorkerEvent* fatal = nullptr;
    for (const WorkerEvent& e : m_events.events) {
        if (e.kind == WorkerEvent::Kind::ProtocolViolation && e.fatal) {
            fatal = &e;
        }
    }
    ASSERT_NE(fatal, nullptr);
    EXPECT_EQ(fatal->stableCode, "EX-CHANNEL-PROTOCOL-ERROR");
    EXPECT_NE(fatal->detail.find("gap"), std::string::npos);

    // 编排侧：任务 Failed＋EX-CHANNEL-PROTOCOL-ERROR 诊断。
    core::DiagnosticRecord protoDiag = core::DiagnosticRecord::make(
        "EX-CHANNEL-PROTOCOL-ERROR", std::nullopt, std::nullopt, std::nullopt,
        "execution/channel", "sequence gap timed out", "核对通道对端");
    machine.appendDiagnostic(protoDiag);
    EXPECT_TRUE(machine.request(TransitionTrigger::RunFailed).accepted);
    EXPECT_EQ(machine.state(), core::TaskState::Failed);

    // 监督器就地终结不可信通道的 worker（fatal→强杀）。
    ASSERT_TRUE(pumpUntil(*supervisor, [this, &launched] {
        for (const WorkerEvent& e : m_events.events) {
            if (e.kind == WorkerEvent::Kind::WorkerExited && e.worker == launched.worker) {
                return true;
            }
        }
        return false;
    }, 30000));
}

// =====================================================================
// 分批流式回传（acceptance 3——NFR-PERF-03 分批/流式、不整体装载）
// =====================================================================

TEST_F(WorkerProcessContractTest, ResultBatchesStreamInOrderBeforeFinal)
{
    const core::TaskIdentity id = registerRun();
    auto supervisor = makeSupervisor();
    ASSERT_TRUE(supervisor->launch(
        makeAssignment(id, "stage-a-script:v1\nbatches 3\nfinal\n"))
                    .ok);

    ASSERT_TRUE(pumpUntil(*supervisor, [this] {
        return m_events.firstOf(WorkerEvent::Kind::FinalOutput) != nullptr;
    }));

    // 三批独立到达（逐帧逐批——不是单一大载荷整体），索引 1,2,3 按序，
    // 全部先于 FinalOutput（§6.5"结果分片"行）。
    std::vector<std::uint64_t> batchOrder;
    std::size_t finalPos = 0;
    for (std::size_t i = 0; i < m_events.events.size(); ++i) {
        const WorkerEvent& e = m_events.events[i];
        if (e.kind == WorkerEvent::Kind::ResultBatch) {
            batchOrder.push_back(e.batchIndex);
        } else if (e.kind == WorkerEvent::Kind::FinalOutput && finalPos == 0) {
            finalPos = i + 1;
        }
    }
    ASSERT_EQ(batchOrder.size(), 3u);
    EXPECT_EQ(batchOrder, (std::vector<std::uint64_t>{1, 2, 3}));
    // 每批载荷独立完整（BATCH:<i> 形态——通道不合并批次）。
    for (const WorkerEvent& e : m_events.events) {
        if (e.kind == WorkerEvent::Kind::ResultBatch) {
            const std::string expected = "BATCH:" + std::to_string(e.batchIndex);
            const std::string actual(e.bytes.begin(), e.bytes.end());
            EXPECT_EQ(actual, expected);
        }
    }
    (void)finalPos;
}

// =====================================================================
// 临时目录隔离（acceptance 3——§6.4 边界规则：worker 只写自身临时目录）
// =====================================================================

TEST_F(WorkerProcessContractTest, WorkerTempDirIsolatedAndCleanedUp)
{
    const core::TaskIdentity id = registerRun();
    auto supervisor = makeSupervisor();
    const WorkerLaunchResult launched = supervisor->launch(
        makeAssignment(id, "stage-a-script:v1\nwrite-temp artifact.bin\nbatches 1\nfinal\n"));
    ASSERT_TRUE(launched.ok);

    ASSERT_TRUE(pumpUntil(*supervisor, [this] {
        return m_events.firstOf(WorkerEvent::Kind::FinalOutput) != nullptr;
    }));

    // 运行中产物只落在 worker 自身临时目录（§6.6 命名：
    // %TEMP%\ird-worker-<pid>-<run>-<attempt>）。
    const WorkerStatus status = supervisor->status(launched.worker);
    std::vector<wchar_t> tempRoot(MAX_PATH + 1);
    const DWORD n = ::GetTempPathW(MAX_PATH + 1, tempRoot.data());
    ASSERT_GT(n, 0u);
    // （各段先落命名局部量——迭代器对必须取自同一字符串实例，跨临时量
    // 取迭代器是未定义行为）。
    const std::string runCanonNarrow = id.run.toCanonical();
    const std::string attCanonNarrow = id.attempt.toCanonical();
    const std::wstring runCanon(runCanonNarrow.begin(), runCanonNarrow.end());
    const std::wstring attCanon(attCanonNarrow.begin(), attCanonNarrow.end());
    const fs::path expectedTemp = fs::path(std::wstring(tempRoot.data()))
                                  / (L"ird-worker-" + std::to_wstring(status.pid) + L"-"
                                     + runCanon + L"-" + attCanon);
    ASSERT_TRUE(fs::exists(expectedTemp / "artifact.bin"))
        << "worker 产物应只出现在其自身临时目录: "
        << (expectedTemp / "artifact.bin").string();

    // 项目正式目录零 worker 产物（results/ 树下不得出现 artifact.bin——
    // 一切正式落点经主进程登记核对，§6.4 边界规则）。
    bool leakedIntoProject = false;
    const fs::path resultsRoot = m_store->canonicalPath() / "results";
    std::error_code walkEc;
    for (const fs::directory_entry& entry :
         fs::recursive_directory_iterator(resultsRoot, walkEc)) {
        if (entry.path().filename() == "artifact.bin") {
            leakedIntoProject = true;
        }
    }
    EXPECT_FALSE(leakedIntoProject);

    // 尝试终结后临时目录 best-effort 清理（§6.6——正常退出即清理）。
    ASSERT_TRUE(pumpUntil(*supervisor, [this, &launched] {
        for (const WorkerEvent& e : m_events.events) {
            if (e.kind == WorkerEvent::Kind::WorkerExited && e.worker == launched.worker) {
                return true;
            }
        }
        return false;
    }));
    std::error_code cleanupEc;
    EXPECT_FALSE(fs::exists(expectedTemp, cleanupEc)) << "正常退出的临时目录应被清理";
}

// =====================================================================
// R-3 实证：句柄继承副本防泄漏（§6.6 进程创建行——启动后关闭继承副本）
// =====================================================================

TEST_F(WorkerProcessContractTest, LauncherClosesInheritedHandleDuplicates_R3)
{
    // 全链实证（真进程）：launch 完成即无任何可继承副本残留在主进程侧
    // （§6.6"句柄在主进程侧于启动后关闭继承副本（防泄漏）"——CreateProcessW
    // 抓取继承面的窗口在 launchWorkerProcess 内闭合，监督器观测面为证）。
    const core::TaskIdentity id = registerRun();
    auto supervisor = makeSupervisor();
    const WorkerLaunchResult launched = supervisor->launch(
        makeAssignment(id, "stage-a-script:v1\nheartbeat-off\nhang\n"));
    ASSERT_TRUE(launched.ok);
    // 握手已走通＝管道面全通，此时查询副本状态（进程仍存活——观测有效）。
    ASSERT_TRUE(pumpUntil(*supervisor, [this] {
        return m_events.firstOf(WorkerEvent::Kind::DispatchAccepted) != nullptr;
    }));
    EXPECT_FALSE(supervisor->hasOpenChildHandleDuplicates(launched.worker))
        << "启动完成后不得残留可继承句柄副本（防泄漏——§6.6）";

    // 收尾强杀（hang 脚本的 worker 不自然退出——不留挂起进程过用例边界）。
    supervisor->terminateForce(launched.worker, TerminationCause::Failed);
    (void)pumpUntil(*supervisor, [this, &launched] {
        for (const WorkerEvent& e : m_events.events) {
            if (e.kind == WorkerEvent::Kind::WorkerExited && e.worker == launched.worker) {
                return true;
            }
        }
        return false;
    }, 15000);
    EXPECT_EQ(supervisor->status(launched.worker).phase, WorkerStatus::Phase::Dead);
}

// =====================================================================
// EX-T07：作业内存聚合覆盖真 worker＋空闲回收即时缩池（§6.6 JobMemory
// 半区＋§6.1"降低并行度（回收空闲 worker）"的真进程实证面）
// =====================================================================

TEST_F(WorkerProcessContractTest, AggregateJobMemoryCoversRealWorkerAndReclaimShrinksIdlePool)
{
    // 排布：launch 一个真 worker 跑完 final 脚本（正常退出→回池 Idle）→
    // aggregateJobMemoryBytes 读到非零作业内存（该 worker 的作业里确实
    // 跑过进程——§6.6 QueryInformationJobObject 峰值口径的真值面）→
    // reclaimIdleWorkers 即时回收空闲 worker（Draining→自然退出→记录
    // 出清），聚合读数归零。登记面不参与本用例（监督器不查登记——
    // 进程层与接纳层的边界，EX-T06 头注"不越权声明"）。
    core::TaskIdentity id;
    id.project = m_store->projectId();
    id.branch = core::BranchId::generate();
    id.revision = core::RevisionId::generate();
    id.run = core::RunId::generate();
    id.attempt = core::AttemptId{1};

    auto supervisor = makeSupervisor();
    const WorkerAssignment assignment =
        makeAssignment(id, "stage-a-script:v1\nprogress 10 kin.batch 1 1\nfinal\n");
    const WorkerLaunchResult launched = supervisor->launch(assignment);
    ASSERT_TRUE(launched.ok) << "worker 进程启动＋Job Scope 绑定应成功";

    // 空池基线：launch 前聚合为 0（未启动任何 worker——对照真读数）。
    // 正常完成：FinalOutput 已收＋进程退出码 0→回池 Idle（§6.4 池化）。
    ASSERT_TRUE(pumpUntil(*supervisor, [this, &supervisor, &launched] {
        return supervisor->status(launched.worker).phase == WorkerStatus::Phase::Idle
            && m_events.countOf(WorkerEvent::Kind::WorkerExited) >= 1;
    })) << "worker 应正常退出并回池（Idle）";

    // 真值面：worker 的作业提交内存峰值合计 >0（任何进程都提交内存——
    // 峰值单调不回落，进程退出后仍可读，§6.6 JobMemory 口径）。
    const std::uint64_t aggregate = supervisor->aggregateJobMemoryBytes();
    EXPECT_GT(aggregate, 0u)
        << "§6.6 内存汇总的 worker 半区：真 worker 作业内存峰值应被覆盖（EX-T07 acceptance 2）";

    // 即时缩池（§6.1"降低并行度（回收空闲 worker）"——EX-T07 增量方法）：
    // 唯一 Idle worker 的进程已随任务终结退出（阶段 A worker 模型）——
    // 回收直接出清该池槽（join→关资源→双表 erase），聚合读数归零。
    const std::size_t reclaimed = supervisor->reclaimIdleWorkers();
    EXPECT_EQ(reclaimed, 1u);
    ASSERT_TRUE(pumpUntil(*supervisor, [&supervisor, &launched] {
        return supervisor->workerCount() == 0
            && supervisor->status(launched.worker).id.value == 0;
    })) << "回收后池槽立即出清（记录域与状态投影双清）";
    EXPECT_EQ(supervisor->aggregateJobMemoryBytes(), 0u)
        << "出清后聚合归零（无活 worker＝无 worker 内存占用）";
    // 第二次回收：无可回收空闲（幂等空转——返回 0 不报错）。
    EXPECT_EQ(supervisor->reclaimIdleWorkers(), 0u);
}

// =====================================================================
// EX-T09 EX-ARC-3：worker 临时目录清理失败——任务终态不受影响＋开发诊断
// ＋目录残留记录（真进程 OS 句柄锁注入——§6.6"清理失败→开发诊断"行）
// =====================================================================

TEST_F(WorkerProcessContractTest, TempDirCleanupFailureKeepsTerminalStateAndReportsDev_EX_ARC_3)
{
    // 注入原理：父进程对 worker 临时目录打开一个不带 FILE_SHARE_DELETE
    // 的目录句柄——Windows 上目录存在无共享删除位的打开句柄时
    // RemoveDirectory 必然失败（真 OS 注入，非脚本叙述）。脚本在 write-temp
    // 之后排 20000 批次帧＝给父进程留出"看到产物→锁目录"的有界窗口
    // （毫秒级）；锁死后 worker 走 final→清理失败→deverror→退出 0。
    IRD_TEST_INFO(std::vector<std::string>{"NFR-REL-01"}, std::vector<std::string>{"AT-10"});

    const core::TaskIdentity id = registerRun();
    TaskStateMachine machine =
        TaskStateMachine::forAcceptedSubmission(makeQueuedRecord(), nullptr);
    ASSERT_TRUE(machine.request(TransitionTrigger::DispatchDequeued).accepted);  // T2
    machine.bindRun(id.run);

    auto supervisor = makeSupervisor();
    const WorkerLaunchResult launched = supervisor->launch(makeAssignment(
        id,
        "stage-a-script:v1\n"
        "checkpoint 1\n"        // 已提交检查点（清理失败前——"检查点不动"断言面）
        "write-temp gate.bin\n" // 锁定窗口的门控产物（EventWatch 同源判据：文件出现）
        "batches 20000\n"       // 锁定窗口的时宽（毫秒级，非时序判据——窗口余量）
        "final\n"));
    ASSERT_TRUE(launched.ok);

    // 握手完成（拿到 pid——临时目录名成分）＋编排侧 T5。
    ASSERT_TRUE(pumpUntil(*supervisor, [this] {
        return m_events.firstOf(WorkerEvent::Kind::DispatchAccepted) != nullptr;
    }));
    EXPECT_TRUE(machine.request(TransitionTrigger::PrepareSucceeded).accepted);
    const WorkerStatus running = supervisor->status(launched.worker);
    ASSERT_NE(running.pid, 0u);

    // 已提交检查点字节的锁定基准（清理失败前捕获——事后比对不变）。
    const WorkerEvent* checkpoint = m_events.firstOf(WorkerEvent::Kind::CheckpointBatch);
    ASSERT_NE(checkpoint, nullptr);
    const std::vector<std::uint8_t> checkpointBefore = checkpoint->bytes;

    // 构造临时目录路径（§6.6 命名：%TEMP%\ird-worker-<pid>-<run>-<attempt>）
    // 并等待门控产物出现（有界轮询——观测"清理窗口已开启"，非时序判据）。
    std::vector<wchar_t> tempRoot(MAX_PATH + 1);
    const DWORD n = ::GetTempPathW(MAX_PATH + 1, tempRoot.data());
    ASSERT_GT(n, 0u);
    const std::string runCanonNarrow = id.run.toCanonical();
    const std::string attCanonNarrow = id.attempt.toCanonical();
    const std::wstring runCanon(runCanonNarrow.begin(), runCanonNarrow.end());
    const std::wstring attCanon(attCanonNarrow.begin(), attCanonNarrow.end());
    const fs::path workerTemp = fs::path(std::wstring(tempRoot.data()))
                                / (L"ird-worker-" + std::to_wstring(running.pid) + L"-"
                                   + runCanon + L"-" + attCanon);
    const fs::path gate = workerTemp / "gate.bin";
    const auto gateDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{30000};
    while (!fs::exists(gate) && std::chrono::steady_clock::now() < gateDeadline) {
        ::Sleep(2);  // 有界轮询（EventWatch 同机制——观测窗口开启，非判据）
    }
    ASSERT_TRUE(fs::exists(gate)) << "门控产物应在有界时间内出现（锁定窗口前提）";

    // OS 注入：打开目录句柄（无 FILE_SHARE_DELETE）——阻塞 RemoveDirectory。
    // （ directory 语义访问需 FILE_FLAG_BACKUP_SEMANTICS；共享位不含 DELETE
    //   ＝删除必然撞共享违例——清理失败的注入面。）
    const HANDLE lock = ::CreateFileW(workerTemp.c_str(), GENERIC_READ,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                      OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    ASSERT_NE(lock, INVALID_HANDLE_VALUE)
        << "目录锁定句柄应成功（真进程注入的前提）";

    // worker 走完 final→清理（失败）→deverror→退出 0——全程真进程。
    ASSERT_TRUE(pumpUntil(*supervisor, [this, &launched] {
        for (const WorkerEvent& e : m_events.events) {
            if (e.kind == WorkerEvent::Kind::WorkerExited && e.worker == launched.worker) {
                return true;
            }
        }
        return false;
    }, 60000));

    // 开发诊断：清理失败经通道回传（dev=true→stableCode 空、detail 携带
    // 通道名——§6.4 诊断边界规则；通道名 execution/worker-temp＝worker 宿主
    // 实现的登记口径）。目录残留信息随 detail 记录（"目录残留记录"观测点）。
    bool sawCleanupDevReport = false;
    for (const WorkerEvent& e : m_events.events) {
        if (e.kind == WorkerEvent::Kind::ErrorReport && e.stableCode.empty()
            && e.detail.rfind("execution/worker-temp", 0) == 0) {
            sawCleanupDevReport = true;
            EXPECT_NE(e.detail.find("cleanup failed"), std::string::npos);
        }
    }
    EXPECT_TRUE(sawCleanupDevReport) << "清理失败应产生开发诊断（EX-ARC-3 行）";

    // 任务终态不受影响：退出码 0（NormalCompletion）→T8 Completed——
    // "临时目录清理失败不波及任务终态"（§7.6 矩阵行）。
    const WorkerEvent* exited = nullptr;
    for (const WorkerEvent& e : m_events.events) {
        if (e.kind == WorkerEvent::Kind::WorkerExited && e.worker == launched.worker) {
            exited = &e;
        }
    }
    ASSERT_NE(exited, nullptr);
    EXPECT_EQ(exited->exit, ExitClassification::NormalCompletion);
    EXPECT_EQ(exited->exitCode, 0u);
    EXPECT_TRUE(machine.request(TransitionTrigger::RunCompleted).accepted);  // T8
    EXPECT_EQ(machine.state(), core::TaskState::Completed);

    // 已提交检查点不动：清理失败前后字节逐位一致（§7.6"已提交检查点不动"）。
    const WorkerEvent* checkpointAfter = m_events.firstOf(WorkerEvent::Kind::CheckpointBatch);
    ASSERT_NE(checkpointAfter, nullptr);
    EXPECT_EQ(checkpointAfter->bytes, checkpointBefore);

    // 目录残留记录：删除失败＝目录仍在盘（best-effort 语义——残留可观测，
    // 清理责任在环境恢复流程，worker 不重试不阻塞）。
    EXPECT_TRUE(fs::exists(workerTemp)) << "清理失败后临时目录应残留（EX-ARC-3 观测点）";

    // 测试现场恢复：释放锁定句柄并清残留（环境卫生——不影响断言）。
    ::CloseHandle(lock);
    std::error_code cleanupEc;
    fs::remove_all(workerTemp, cleanupEc);
}

// =====================================================================
// EX-T09 EX-RES-1：双 worker 竞争同一任务——登记互斥（状态机 T2 守卫＋
// 登记表 run 唯一）下仅一个活动 worker，第二次派发被拒绝
// =====================================================================

TEST_F(WorkerProcessContractTest, DoubleDispatchSameTaskRejectedSingleActiveWorker_EX_RES_1)
{
    // 排布：合法派发一次（真 worker 运行中）→注入"同任务第二次派发"→
    // 两道登记互斥守卫拒绝（状态机矩阵外／登记表 run 重复）→监督器观测
    // 面＝该 run 恰一个 Running worker、无第二个进程绑定同一 run。
    IRD_TEST_INFO(std::vector<std::string>{"TASK-03"}, std::vector<std::string>{"AT-10"});

    const core::TaskIdentity id = registerRun();
    TaskStateMachine machine =
        TaskStateMachine::forAcceptedSubmission(makeQueuedRecord(), nullptr);
    ASSERT_TRUE(machine.request(TransitionTrigger::DispatchDequeued).accepted);  // T2（唯一合法派发）
    machine.bindRun(id.run);

    auto supervisor = makeSupervisor();
    const WorkerLaunchResult launched = supervisor->launch(
        makeAssignment(id, "stage-a-script:v1\nheartbeat-off\nhang\n"));
    ASSERT_TRUE(launched.ok);
    ASSERT_TRUE(pumpUntil(*supervisor, [this] {
        return m_events.firstOf(WorkerEvent::Kind::DispatchAccepted) != nullptr;
    }));
    EXPECT_TRUE(machine.request(TransitionTrigger::PrepareSucceeded).accepted);  // T5

    // 注入①：同任务再次进入派发（调度互斥的状态机半区）——Preparing 态
    // 无 T2 出边＝矩阵外请求，fail-fast 拒绝且状态不变。
    EXPECT_THROW(machine.request(TransitionTrigger::DispatchDequeued), ExecutionError);
    EXPECT_EQ(machine.state(), core::TaskState::Running);

    // 注入②：同一 run 重复登记（调度互斥的登记表半区）——run 全局唯一
    // （registerRun 契约：run 重复登记抛 ExecutionError）。
    EXPECT_THROW(m_registry.registerRun(TaskId::generate(), makeRegistrationInput(id)),
                 ExecutionError);

    // 观测点（§11 行"WorkerSupervisor 状态"）：该 run 恰一个活动 worker
    // （Running 相、绑定 id.run）；池中不存在第二个绑定同一 run 的记录。
    std::size_t activeForRun = 0;
    std::size_t total = 0;
    for (const WorkerStatus& status : supervisor->list()) {
        ++total;
        if (status.currentRun == id.run && status.phase == WorkerStatus::Phase::Running) {
            ++activeForRun;
        }
    }
    EXPECT_EQ(activeForRun, 1u) << "同任务仅允许一个活动 worker（登记互斥）";
    EXPECT_EQ(total, 1u) << "竞争注入不得产生第二个 worker 记录";

    // 收尾：挂起 worker 强杀（不留孤儿进程过用例边界——R-3 纪律）。
    supervisor->terminateForce(launched.worker, TerminationCause::Failed);
    (void)pumpUntil(*supervisor, [this, &launched] {
        for (const WorkerEvent& e : m_events.events) {
            if (e.kind == WorkerEvent::Kind::WorkerExited && e.worker == launched.worker) {
                return true;
            }
        }
        return false;
    }, 15000);
    EXPECT_EQ(supervisor->status(launched.worker).phase, WorkerStatus::Phase::Dead);
}

}  // namespace
