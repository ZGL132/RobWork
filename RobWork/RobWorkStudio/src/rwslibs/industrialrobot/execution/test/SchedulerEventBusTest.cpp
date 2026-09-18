/**
 * @file   SchedulerEventBusTest.cpp
 * @brief  调度器与事件/进度分发用例组（EX-T05）——EX-SUB-1/2（并发提交
 *         唯一身份/同任务事件 FIFO/过期快照拒绝）、提交验证 V1~V4、
 *         PM-07 只读拒绝启动、双优先级 FIFO 不抢占（D-04）、同项目预算、
 *         进度节流 ≤10 Hz（D-07）、UX-10 阶段投影透传边界、UI 线程零
 *         计算结构断言（NFR-PERF-01）、关闭排空（§7.5）。
 *
 * 设计依据：
 *   - units/execution.md §6.1（队列/优先级/预算/进度节流）、§6.3（提交
 *     验证 V1~V4＋内联门槛）、§4.3（身份分配协议——受理段）、§7.5
 *     （关闭排空）、§10.1/§10.8（调度器契约/接口共性）、§11（EX-SUB-1/
 *     EX-SUB-2 用例行——验证方式与观测点）、§13（ui 行——UX-10 七态
 *     状态词与映射归 ui，execution 零新增状态词）
 *   - 需求 TASK-01（状态机受理入口）、TASK-03（五元组分配在提交受理
 *     路径）、CON-01（派发前快照完整性——V2）、PM-07（只读拒绝启动
 *     ——V3）、NFR-PERF-01（>1 s 转后台——提交/控制非阻塞）、UX-10
 *     （进度阶段投影）、UX-03（关闭触发的排队取消零错误诊断）
 *   - 任务契约 tasks/foundation/EX-T05.json acceptance 1/3/4/5（acceptance
 *     2 的事件投递线程契约面归 contract_test/EventBusDeliveryContractTest；
 *     本文件覆盖总线的发布接线行为）
 *
 * 替身边界声明（§11/EV-REG-3 同源纪律）：评估器注册查询/修订闭包为接口
 *   替身（只验证调度器对校验面的消费契约，不构成 evidence 注册表/快照
 *   校验器的实现正确性证明）；内联执行体为脚本替身（阻塞/即时两种——
 *   真实评估计算归业务域单元，EX-T06+ 派发链接管非内联登记段）；存储
 *   写权限面用**真实** ProjectStore（PRJ-T14——PM-07 验证的就是对真实
 *   writable() 的消费）。断言不 sleep：节流/非阻塞断言经注入 ManualClock
 *   虚拟推进与确定性条件等待（testkit §6.5/§6.6 同纪律）。
 */

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/core/Events.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/evidence/Evaluator.hpp>
#include <sdurws/ird/evidence/Snapshot.hpp>
#include <sdurws/ird/execution/Controller.hpp>
#include <sdurws/ird/execution/Errors.hpp>
#include <sdurws/ird/execution/EventBus.hpp>
#include <sdurws/ird/execution/Scheduler.hpp>
#include <sdurws/ird/project/ProjectStore.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
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
// 身份/取值辅助（确定性固定值——RunRegistryAdmissionTest 同款风格，自持）
// =====================================================================

const char* kHex32A = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
const char* kHex32D = "dddddddddddddddddddddddddddddddd";
const char* kHex64A = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
const char* kHex64B = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

core::ContentVersion cv(const char* hex64)
{
    return core::ContentVersion::fromCanonical(std::string{"cv-"} + hex64);
}

core::ObjectId oid(const char* hex32)
{
    return core::ObjectId::fromCanonical(std::string{"obj-"} + hex32);
}

core::ContentIdentity cid(const char* hex64)
{
    return core::ContentIdentity::fromCanonical(std::string{"cid-"} + hex64);
}

/// 测试侧拒绝诊断构造（注入半区/守卫替身用——ERR-01 最小合法承载）。
core::DiagnosticRecord rejectionOf(std::string code, std::string cause)
{
    return core::DiagnosticRecord::make(std::move(code), std::nullopt, std::nullopt,
                                        std::nullopt, "execution/scheduler-test",
                                        std::move(cause), "检查提交上下文");
}

// =====================================================================
// ManualClock——虚拟时钟（testkit §6.5/§6.6 纪律：时序断言经显式推进，
// 不 sleep；ClockFn 形态与 Controller/DrainCoordinator/TaskScheduler 同源）
// =====================================================================

class ManualClock {
public:
    std::chrono::steady_clock::time_point now() const { return m_now; }
    void advance(std::chrono::milliseconds d) { m_now += d; }

private:
    std::chrono::steady_clock::time_point m_now{std::chrono::milliseconds{1'000'000}};
};

// =====================================================================
// 提交验证协作面替身（校验面消费契约的验证载体——非 evidence 实现证明）
// =====================================================================

/// 评估器注册表替身：键→契约版本（IProducerRegistryView 最小实现）。
class ScriptedProducerRegistry final : public ev::IProducerRegistryView {
public:
    explicit ScriptedProducerRegistry(std::map<std::string, std::uint32_t> table)
        : m_table(std::move(table))
    {
    }
    bool isRegistered(std::string_view key) const override
    {
        return m_table.count(std::string{key}) > 0;
    }
    bool contractVersionMatches(std::string_view key, std::uint32_t version) const override
    {
        const auto it = m_table.find(std::string{key});
        return it != m_table.end() && it->second == version;
    }

private:
    std::map<std::string, std::uint32_t> m_table;
};

/// 修订闭包替身：脚本化逐对象应答（V2 包含性的正反两面）。
class ScriptedClosureSource final : public ev::IRevisionClosureSource {
public:
    bool objectInRevision(core::RevisionId, core::ObjectId objectId,
                          core::ContentVersion) const override
    {
        return m_rejectAll ? false : m_missing.count(objectId) == 0;
    }
    void rejectAll() { m_rejectAll = true; }
    void missObject(core::ObjectId id) { m_missing.insert(std::move(id)); }

private:
    bool m_rejectAll = false;
    std::set<core::ObjectId> m_missing;
};

/// 上下文守卫替身：脚本化返回（V1 注入半区——返回诊断即拒绝）。
class ScriptedGuard final : public ISubmissionGuard {
public:
    std::optional<core::DiagnosticRecord> checkSubmission(const TaskSubmission&) const override
    {
        return m_rejection;   // nullopt＝通过；非空＝拒绝
    }
    void rejectWith(core::DiagnosticRecord record) { m_rejection = std::move(record); }

private:
    std::optional<core::DiagnosticRecord> m_rejection;
};

// =====================================================================
// 内联执行体替身（IInlineRunExecutor——即时/阻塞两种脚本）
// =====================================================================

/// 即时执行体：记录调用序（优先级/预算用例的出队序观测点），返回 Completed。
class InstantExecutor final : public IInlineRunExecutor {
public:
    InlineRunOutcome run(const TaskRecord& record) override
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_runOrder.push_back(record.taskId);
        }
        InlineRunOutcome out;
        out.cause = TerminationCause::Completed;
        return out;
    }
    std::vector<TaskId> runOrder() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_runOrder;
    }

private:
    mutable std::mutex m_mutex;
    std::vector<TaskId> m_runOrder;
};

/// 阻塞执行体：run 阻塞到 release()（UI 零计算结构断言的"计算本体"）。
class BlockingExecutor final : public IInlineRunExecutor {
public:
    InlineRunOutcome run(const TaskRecord&) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this] { return m_release; });
        InlineRunOutcome out;
        out.cause = TerminationCause::Completed;
        return out;
    }
    void release()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_release = true;
        }
        m_cv.notify_all();
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_release = false;
};

// =====================================================================
// 事件收集 sink（总线订阅面——mutex 保护：投递线程回调＋主线程断言）
// =====================================================================

class CollectingEventSink final : public core::IDomainEventSink {
public:
    void onEvent(const core::DomainEvent& event) override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_events.push_back(event);
    }
    std::vector<core::DomainEvent> snapshot() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_events;
    }
    std::size_t size() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_events.size();
    }

private:
    mutable std::mutex m_mutex;
    std::vector<core::DomainEvent> m_events;
};

// =====================================================================
// 冻结快照构建（VerdictTest/RunRegistryAdmissionTest 同构最小实例——
// 契约形态数据；builder.build 即时验证＋计算 snapshotId＝真实冻结产物）
// =====================================================================

class AcceptAllClosureSource final : public ev::IRevisionClosureSource {
public:
    bool objectInRevision(core::RevisionId, core::ObjectId, core::ContentVersion) const override
    {
        return true;
    }
};

std::vector<ev::CaseEntry> standardCases()
{
    return {{oid(kHex32A), "case-a", true, true}};
}

ev::AnalysisSnapshot buildSnapshotFor(core::ProjectId project, core::BranchId branch,
                                      core::RevisionId revision)
{
    ev::SnapshotBuilder b;
    b.setIdentity(project, branch, revision, 5);
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
    for (const ev::CaseEntry& c : standardCases()) {
        b.addCase(c);
    }
    AcceptAllClosureSource source;
    return b.build(source);
}

/// 未冻结快照（builder 中间态——不经 build，snapshotId 保留值）。
ev::AnalysisSnapshot unfrozenSnapshotFor(core::ProjectId project, core::BranchId branch,
                                         core::RevisionId revision)
{
    ev::AnalysisSnapshot snap = buildSnapshotFor(project, branch, revision);
    snap.snapshotId = core::ContentIdentity{};   // 保留值＝未冻结
    return snap;
}

// =====================================================================
// 测试夹具：真实项目存储（Writable）＋全绿协作面＋事件总线接线
// =====================================================================

class SchedulerEventBusTest : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        std::error_code ec;
#ifdef _WIN32
        s_base = fs::temp_directory_path(ec) / "ird_ex_scheduler_test"
                 / std::to_string(::GetCurrentProcessId());
#else
        s_base = fs::temp_directory_path(ec) / "ird_ex_scheduler_test";
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
        fs::remove_all(s_base, ec);   // 失败保留现场的例外：本夹具用例多，
                                      // 统一清理；残留不影响幂等重跑
    }

    void SetUp() override
    {
        m_dir = s_base / ("case" + std::to_string(++s_caseCounter)) / "p.rwdesign";
        m_store = pd::ProjectStoreFactory::createNew(m_dir, "EX-T05", nullptr, nullptr).store;
        ASSERT_TRUE(m_store != nullptr);
        ASSERT_TRUE(m_store->writable());

        m_clock = std::make_unique<ManualClock>();
        m_registry = std::make_unique<ScriptedProducerRegistry>(
            ScriptedProducerRegistry({{"kin-batch-ik", 7}}));
        m_closure = std::make_unique<ScriptedClosureSource>();
        m_executor = std::make_unique<InstantExecutor>();

        m_snapshot = std::make_unique<const ev::AnalysisSnapshot>(
            buildSnapshotFor(m_store->projectId(), core::BranchId::generate(),
                             core::RevisionId::generate()));

        // 默认装配：节流窗 100 ms（D-07）；谓词恒 true（内联 Preview 走链
        // ——各用例按需覆盖 Config）。
        m_config.progressMinInterval = std::chrono::milliseconds{100};
        m_config.inlineEligible = [](const TaskSubmission&) { return true; };
    }

    void TearDown() override
    {
        m_scheduler.reset();
        m_drain.reset();
        m_controller.reset();
    }

    /// 组装调度器（协作面全绿底座——各用例按需改 store/priority 等）。
    void makeScheduler(ResourceBudget budget = {})
    {
        TaskScheduler::Collaboration collab;
        collab.evaluators = m_registry.get();
        collab.closure = m_closure.get();
        collab.store = m_store.get();
        collab.guard = &m_guard;
        collab.capabilities = nullptr;   // 能力面缺省＝最小能力（本组用例不消费）

        TaskController::Config controllerConfig;   // 默认保留窗
        m_controller = std::make_unique<TaskController>(
            controllerConfig, [this] { return m_clock->now(); });
        DrainCoordinator::Config drainConfig;   // 缺省兜底阈值（15 s——EX-T03 D 值）
        m_drain = std::make_unique<DrainCoordinator>(*m_controller, drainConfig,
                                                     [this] { return m_clock->now(); });
        m_scheduler = std::make_unique<TaskScheduler>(*m_controller, *m_drain, collab,
                                                      m_config, [this] { return m_clock->now(); });
        m_scheduler->setEventBus(&m_bus);
        m_scheduler->setInlineExecutor(m_executor.get());
        m_scheduler->setResourceBudget(budget);
        // 订阅句柄持久保存（临时句柄会在语句末析构＝立即退订——RAII 契约）。
        m_sinkSubscription = m_bus.subscribe(m_eventSink);
    }

    /// 合法提交底座（快照锚定真实 projectId；可单点变异模式/优先级）。
    TaskSubmission baseSubmission(core::EvaluationMode mode = core::EvaluationMode::Preview,
                                  TaskPriority priority = TaskPriority::Background)
    {
        TaskSubmission s;
        s.snapshot = *m_snapshot;
        s.evaluatorKey = "kin-batch-ik";
        s.contractVersion = 7;
        s.mode = mode;
        s.priority = priority;
        return s;
    }

    /// 按状态过滤事件（收集 sink 快照的任务状态流——FIFO 断言素材）。
    static std::vector<core::TaskState> statusStates(const std::vector<core::DomainEvent>& events)
    {
        std::vector<core::TaskState> states;
        for (const core::DomainEvent& e : events) {
            if (e.kind == core::DomainEventKind::TaskStatusChanged) {
                states.push_back(e.asTaskStatusChanged().newState);
            }
        }
        return states;
    }

    static fs::path s_base;
    static int s_caseCounter;

    fs::path m_dir;
    std::unique_ptr<pd::ProjectStore> m_store;
    std::unique_ptr<ManualClock> m_clock;
    std::unique_ptr<ScriptedProducerRegistry> m_registry;
    std::unique_ptr<ScriptedClosureSource> m_closure;
    ScriptedGuard m_guard;
    std::unique_ptr<InstantExecutor> m_executor;
    std::unique_ptr<const ev::AnalysisSnapshot> m_snapshot;
    TaskScheduler::Config m_config;
    std::unique_ptr<TaskController> m_controller;
    std::unique_ptr<DrainCoordinator> m_drain;
    std::unique_ptr<TaskScheduler> m_scheduler;
    CollectingEventSink m_eventSink;
    DomainEventBusImpl m_bus;
    /// 订阅句柄（声明在 m_bus 之后——析构逆序保证句柄先于总线析构时退订，
    /// RAII 语义安全；makeScheduler 赋值，避免临时句柄立即退订）。
    std::unique_ptr<core::IEventSubscription> m_sinkSubscription;
};

fs::path SchedulerEventBusTest::s_base;
int SchedulerEventBusTest::s_caseCounter = 0;

// =====================================================================
// EX-SUB-1：并发提交——各获唯一 TaskId、无重复、受理即 Queued 事件
// =====================================================================

TEST_F(SchedulerEventBusTest, ConcurrentSubmitAssignsUniqueTaskIds_EX_SUB_1)
{
    // TASK-03/§4.3：多线程并发提交 N 个任务→各获唯一 TaskId（互斥受理段
    // 内 generate 各一次）；受理即 Queued（TASK-01 状态机受理入口——§5.1
    // 注"受理即 Queued"，无 Created 态）；同输入重复提交＝两个独立任务。
    makeScheduler();
    constexpr int kThreads = 6;
    constexpr int kPerThread = 12;

    std::vector<std::vector<SubmitResult>> results(kThreads);
    std::vector<std::thread> workers;
    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([this, t, kPerThread, &results] {
            for (int i = 0; i < kPerThread; ++i) {
                // 同一快照重复提交（幂等性不按提交内容承诺——§4.2）。
                results[static_cast<std::size_t>(t)]
                    .push_back(m_scheduler->submit(baseSubmission()));
            }
        });
    }
    for (std::thread& w : workers) {
        w.join();
    }

    // 全部受理且 TaskId 两两不同（checkStableIdsUnique 同型断言——§11
    // EX-SUB-1 观测点"TaskId 唯一性"）。
    std::set<std::string> canonicalIds;
    std::size_t accepted = 0;
    for (const auto& perThread : results) {
        ASSERT_EQ(perThread.size(), static_cast<std::size_t>(kPerThread));
        for (const SubmitResult& r : perThread) {
            ASSERT_TRUE(r.accepted);
            ASSERT_TRUE(r.task.has_value());
            canonicalIds.insert(r.task->toCanonical());
            ++accepted;
        }
    }
    EXPECT_EQ(accepted, static_cast<std::size_t>(kThreads * kPerThread));
    EXPECT_EQ(canonicalIds.size(), accepted) << "TaskId 全局唯一——零重复";

    // 受理事件：每任务恰一个 Queued（§4.3 受理段；尚未 tick——无后续转移）。
    m_bus.waitForIdle();
    const std::vector<core::DomainEvent> events = m_eventSink.snapshot();
    ASSERT_EQ(events.size(), accepted);
    std::set<std::string> eventRunKeys;
    for (const core::DomainEvent& e : events) {
        ASSERT_EQ(e.kind, core::DomainEventKind::TaskStatusChanged);
        const core::TaskStatusChangedPayload& p = e.asTaskStatusChanged();
        EXPECT_EQ(p.newState, core::TaskState::Queued);
        // 五元组前缀＝提交快照锚定三元组（受理时绑定——§4.3）；run/attempt
        // Queued 期为保留空值（§4.2 run 行——"完整五元组"承诺自派发起）。
        EXPECT_EQ(p.task.project, m_snapshot->project);
        EXPECT_EQ(p.task.revision, m_snapshot->revision);
        eventRunKeys.insert(p.task.run.toCanonical());
    }
    EXPECT_EQ(eventRunKeys.size(), 1u) << "全部 Queued 事件携带同一保留空 run 值";
}

// =====================================================================
// EX-SUB-1（事件 FIFO 半区）：同任务事件序＝状态机转移序（内联走链）
// =====================================================================

TEST_F(SchedulerEventBusTest, SameTaskEventFifoFollowsTransitionOrder_EX_SUB_1)
{
    // §4.3"同任务事件 FIFO"/§10.4：调度线程发布的状态事件序＝状态机
    // 转移序。内联 Preview 任务走链 Queued→Preparing→Running→Completed
    // （§6.3 内联门槛——同一转移表），事件流按此序投递（acceptance 2
    // 的九态流之外、本任务可驱动到的四态子序——其余状态的事件序由
    // EX-T02 转移矩阵用例与 EX-T03/T09 契约套件覆盖）。
    makeScheduler();
    const SubmitResult r = m_scheduler->submit(baseSubmission());
    ASSERT_TRUE(r.accepted);

    m_scheduler->tick();   // 出队→内联走链至终态
    m_bus.waitForIdle();

    const std::vector<core::TaskState> states = statusStates(m_eventSink.snapshot());
    ASSERT_EQ(states.size(), 4u);
    EXPECT_EQ(states[0], core::TaskState::Queued);
    EXPECT_EQ(states[1], core::TaskState::Preparing);
    EXPECT_EQ(states[2], core::TaskState::Running);
    EXPECT_EQ(states[3], core::TaskState::Completed);

    // 终态投影：Completed＋Preview 归档阶段恒 NotApplicable（§4.2/表 1）。
    const std::optional<TaskSnapshot> snap = m_scheduler->tryTask(*r.task);
    ASSERT_TRUE(snap.has_value());
    EXPECT_EQ(snap->state, core::TaskState::Completed);
    EXPECT_EQ(snap->archivePhase, ArchivePhase::NotApplicable);
}

// =====================================================================
// EX-SUB-2：过期快照拒绝——SubmissionRejected＋EX-SNAPSHOT-STALE＋无记录
// =====================================================================

TEST_F(SchedulerEventBusTest, StaleSnapshotRejectedWithNoRecord_EX_SUB_2)
{
    // CON-01/§6.3 V2：快照锚定修订被移出闭包→EX-SNAPSHOT-STALE 结构化
    // 拒绝；不产生 TaskRecord（§5.1 注——拒绝发生在状态机之外）、状态
    // 查询为空、零事件（§11 EX-SUB-2 观测点）。
    makeScheduler();
    m_closure->rejectAll();

    const SubmitResult r = m_scheduler->submit(baseSubmission());
    EXPECT_FALSE(r.accepted);
    EXPECT_FALSE(r.task.has_value());
    ASSERT_FALSE(r.diagnostics.empty());
    EXPECT_EQ(r.diagnostics.front().code, "EX-SNAPSHOT-STALE");
    EXPECT_EQ(m_eventSink.size(), 0u) << "拒绝不产生任何状态事件";

    // "无 TaskRecord"的可观测面：查询为空（tryTask 未知任务→nullopt）。
    EXPECT_EQ(m_scheduler->tryTask(TaskId::generate()), std::nullopt);
}

/** V2 变体：闭包内单对象缺失（非整体过期）同样 EX-SNAPSHOT-STALE。 */
TEST_F(SchedulerEventBusTest, MissingClosureObjectRejected_EX_SUB_2)
{
    makeScheduler();
    m_closure->missObject(oid(kHex32A));

    const SubmitResult r = m_scheduler->submit(baseSubmission());
    EXPECT_FALSE(r.accepted);
    ASSERT_FALSE(r.diagnostics.empty());
    EXPECT_EQ(r.diagnostics.front().code, "EX-SNAPSHOT-STALE");
}

/** V1：未注册评估器／契约版本不符／未冻结快照——全部结构化拒绝。 */
TEST_F(SchedulerEventBusTest, FormalValidationRejectsInvalidSubmissions)
{
    makeScheduler();

    // 评估器未注册（§6.3 V1——evaluatorKey 在注册表 find 不命中）。
    TaskSubmission unknown = baseSubmission();
    unknown.evaluatorKey = "no-such-evaluator";
    SubmitResult r1 = m_scheduler->submit(std::move(unknown));
    EXPECT_FALSE(r1.accepted);
    ASSERT_FALSE(r1.diagnostics.empty());
    EXPECT_EQ(r1.diagnostics.front().code, "EX-TASK-REJECTED");

    // 契约版本不符（contractVersion 与注册值 7 不一致）。
    TaskSubmission versionMismatch = baseSubmission();
    versionMismatch.contractVersion = 8;
    SubmitResult r2 = m_scheduler->submit(std::move(versionMismatch));
    EXPECT_FALSE(r2.accepted);
    ASSERT_FALSE(r2.diagnostics.empty());
    EXPECT_EQ(r2.diagnostics.front().code, "EX-TASK-REJECTED");

    // 快照未冻结（builder 中间态——snapshotId 保留值）。
    TaskSubmission unfrozen = baseSubmission();
    unfrozen.snapshot = unfrozenSnapshotFor(m_snapshot->project, m_snapshot->branch,
                                            m_snapshot->revision);
    SubmitResult r3 = m_scheduler->submit(std::move(unfrozen));
    EXPECT_FALSE(r3.accepted);
    ASSERT_FALSE(r3.diagnostics.empty());
    EXPECT_EQ(r3.diagnostics.front().code, "EX-TASK-REJECTED");

    // 注入半区：上下文守卫的拒绝诊断原样透传（ISubmissionGuard 契约）。
    m_guard.rejectWith(rejectionOf("EX-TASK-REJECTED", "上下文校验失败（注入半区）"));
    SubmitResult r4 = m_scheduler->submit(baseSubmission());
    EXPECT_FALSE(r4.accepted);
    ASSERT_FALSE(r4.diagnostics.empty());
    EXPECT_EQ(r4.diagnostics.front().cause, "上下文校验失败（注入半区）");
}

// =====================================================================
// PM-07：只读拒绝启动——消费真实 store.writable()（V3，写权限归 project）
// =====================================================================

TEST_F(SchedulerEventBusTest, ReadOnlyStoreRejectsFormalSubmit_PM_07)
{
    // PM-07/§6.3 V3：显式只读打开同一项目（真实 ProjectStore——writable()
    // 的唯一权威＝是否持有 OS 排他句柄，SA-17；execution 只消费结论）。
    // 需写 results/ 的正式评估（Verified）拒绝启动＋EX-STORE-READ-ONLY；
    // Preview 不写 results/ 跳过 V3——照常受理。
    pd::OpenStoreRequest req;
    req.path = m_dir;
    req.mode = pd::OpenMode::ReadOnly;
    std::unique_ptr<pd::ProjectStore> readOnly =
        pd::ProjectStoreFactory::open(req).store;
    ASSERT_TRUE(readOnly != nullptr);
    ASSERT_FALSE(readOnly->writable()) << "前置：只读上下文（PM-07 的输入事实）";

    TaskScheduler::Collaboration collab;
    collab.evaluators = m_registry.get();
    collab.closure = m_closure.get();
    collab.store = readOnly.get();
    TaskController::Config controllerConfig;   // 缺省保留窗
    m_controller = std::make_unique<TaskController>(
        controllerConfig, [this] { return m_clock->now(); });
    DrainCoordinator::Config drainConfig;      // 缺省兜底阈值（EX-T03 D 值）
    m_drain = std::make_unique<DrainCoordinator>(*m_controller, drainConfig,
                                                 [this] { return m_clock->now(); });
    m_scheduler = std::make_unique<TaskScheduler>(*m_controller, *m_drain, collab,
                                                  m_config, [this] { return m_clock->now(); });
    m_scheduler->setEventBus(&m_bus);
    m_scheduler->setInlineExecutor(m_executor.get());
    m_sinkSubscription = m_bus.subscribe(m_eventSink);

    // Verified 提交：只读拒绝启动（不产生 TaskRecord）。
    const SubmitResult rejected = m_scheduler->submit(baseSubmission(core::EvaluationMode::Verified));
    EXPECT_FALSE(rejected.accepted);
    EXPECT_FALSE(rejected.task.has_value());
    ASSERT_FALSE(rejected.diagnostics.empty());
    EXPECT_EQ(rejected.diagnostics.front().code, "EX-STORE-READ-ONLY");

    // Quick 同拒（V3 对全部写 results/ 的模式生效）。
    const SubmitResult quick = m_scheduler->submit(baseSubmission(core::EvaluationMode::Quick));
    EXPECT_FALSE(quick.accepted);
    ASSERT_FALSE(quick.diagnostics.empty());
    EXPECT_EQ(quick.diagnostics.front().code, "EX-STORE-READ-ONLY");

    // Preview 跳过 V3（§6.3 V3"Preview 跳过（不写 results）"）——照常受理。
    const SubmitResult preview = m_scheduler->submit(baseSubmission(core::EvaluationMode::Preview));
    EXPECT_TRUE(preview.accepted);
}

// =====================================================================
// V4：队列容量——溢出＝SubmissionRejected＋诊断（不失败已排队任务）
// =====================================================================

TEST_F(SchedulerEventBusTest, QueueCapacityRejectsOverflow_V4)
{
    // §6.1/§6.3 V4：提交期预算检查仅拒绝对列容量溢出（容量 2 的预算）；
    // 已排队任务不受影响（排队等待，非失败）。
    m_config.inlineEligible = [](const TaskSubmission&) { return false; };   // 排队面观测
    ResourceBudget budget;
    budget.queueCapacity = 2;
    makeScheduler(budget);

    const SubmitResult first = m_scheduler->submit(baseSubmission());
    ASSERT_TRUE(first.accepted);
    ASSERT_TRUE(m_scheduler->submit(baseSubmission()).accepted);
    const SubmitResult overflow = m_scheduler->submit(baseSubmission());
    EXPECT_FALSE(overflow.accepted);
    ASSERT_FALSE(overflow.diagnostics.empty());
    EXPECT_EQ(overflow.diagnostics.front().code, "EX-TASK-REJECTED");
    EXPECT_NE(overflow.diagnostics.front().cause.find("队列已满"), std::string::npos);

    // 已排队任务不受溢出拒绝波及（仍 Queued 等待——§6.1"不失败已排队
    // 任务"；tick 后正常出队 Preparing——排队位语义完整）。
    m_scheduler->tick();
    EXPECT_EQ(m_controller->tryState(*first.task).value(), core::TaskState::Preparing);
}

// =====================================================================
// D-04：双优先级 FIFO 不抢占——Interactive 先于 Background、同级按序
// =====================================================================

TEST_F(SchedulerEventBusTest, PriorityAndFifoDispatchOrder_D04)
{
    // §6.1 队列排序行：双优先级＋同级 FIFO（提交序号单调）；不抢占
    // （已 Running 任务不被更高优先级打断——出队只发生在额度空位）。
    // 本组关闭内联（普通排队形态），经逐拍状态观测出队序。
    m_config.inlineEligible = [](const TaskSubmission&) { return false; };
    ResourceBudget budget;
    budget.maxConcurrentTasks = 8;
    makeScheduler(budget);

    // 入队序：B1 先；I1/I2 后（更高优先级）；B3 最后。
    const SubmitResult b1 = m_scheduler->submit(
        baseSubmission(core::EvaluationMode::Preview, TaskPriority::Background));
    const SubmitResult i1 = m_scheduler->submit(
        baseSubmission(core::EvaluationMode::Preview, TaskPriority::Interactive));
    const SubmitResult i2 = m_scheduler->submit(
        baseSubmission(core::EvaluationMode::Preview, TaskPriority::Interactive));
    const SubmitResult b3 = m_scheduler->submit(
        baseSubmission(core::EvaluationMode::Preview, TaskPriority::Background));
    ASSERT_TRUE(b1.accepted && i1.accepted && i2.accepted && b3.accepted);

    // 拍 1：Interactive 队头 I1 出队（先于先提交的 B1——双优先级语义），
    // B1/I2/B3 仍 Queued。
    m_scheduler->tick();
    EXPECT_EQ(m_controller->tryState(*i1.task).value(), core::TaskState::Preparing);
    EXPECT_EQ(m_controller->tryState(*b1.task).value(), core::TaskState::Queued);
    EXPECT_EQ(m_controller->tryState(*i2.task).value(), core::TaskState::Queued);

    // 拍 2：同级 FIFO——I2 先于 B1（Interactive 队列未空前 Background 不出）。
    m_scheduler->tick();
    EXPECT_EQ(m_controller->tryState(*i2.task).value(), core::TaskState::Preparing);
    EXPECT_EQ(m_controller->tryState(*b1.task).value(), core::TaskState::Queued);

    // 拍 3/4：Interactive 队列已空——Background 按提交序 B1→B3。
    m_scheduler->tick();
    EXPECT_EQ(m_controller->tryState(*b1.task).value(), core::TaskState::Preparing);
    EXPECT_EQ(m_controller->tryState(*b3.task).value(), core::TaskState::Queued);
    m_scheduler->tick();
    EXPECT_EQ(m_controller->tryState(*b3.task).value(), core::TaskState::Preparing);

    // 不抢占（D-04）：在途（Preparing/Running）不被新到更高优先级打断
    // ——出队只发生在额度空位；此处额度充足（8），已出队四任务保持
    // Preparing 原状（无任何回退/中断路径），新提交的 I3 进入正常排队。
    const SubmitResult i3 = m_scheduler->submit(
        baseSubmission(core::EvaluationMode::Preview, TaskPriority::Interactive));
    ASSERT_TRUE(i3.accepted);
    m_scheduler->tick();
    EXPECT_EQ(m_controller->tryState(*i3.task).value(), core::TaskState::Preparing);
    EXPECT_EQ(m_controller->tryState(*b1.task).value(), core::TaskState::Preparing)
        << "在途任务不受后续优先级到达影响（无抢占路径）";
}

/** 优先级出队序的行为半区：内联执行体记录的派发序＝I1→I2→B1→B3。 */
TEST_F(SchedulerEventBusTest, InlineDispatchFollowsPriorityThenFifo)
{
    // 内联走链（谓词恒 true——夹具默认）：出队即走链至终态、额度立即
    // 释放——执行体调用序即出队序（D-04 排序键的行为可观测面）。
    ResourceBudget budget;
    budget.maxConcurrentTasks = 1;   // 单额度：每拍恰派发一个
    makeScheduler(budget);

    const SubmitResult b1 = m_scheduler->submit(
        baseSubmission(core::EvaluationMode::Preview, TaskPriority::Background));
    const SubmitResult i1 = m_scheduler->submit(
        baseSubmission(core::EvaluationMode::Preview, TaskPriority::Interactive));
    const SubmitResult i2 = m_scheduler->submit(
        baseSubmission(core::EvaluationMode::Preview, TaskPriority::Interactive));
    const SubmitResult b3 = m_scheduler->submit(
        baseSubmission(core::EvaluationMode::Preview, TaskPriority::Background));
    ASSERT_TRUE(b1.accepted && i1.accepted && i2.accepted && b3.accepted);

    for (int beat = 0; beat < 4; ++beat) {
        m_scheduler->tick();
    }
    const std::vector<TaskId> order = m_executor->runOrder();
    ASSERT_EQ(order.size(), 4u);
    EXPECT_EQ(order[0], *i1.task) << "Interactive 先于先提交的 Background";
    EXPECT_EQ(order[1], *i2.task) << "同级按提交序（I1<I2）";
    EXPECT_EQ(order[2], *b1.task) << "Background 内按提交序（B1<B3）";
    EXPECT_EQ(order[3], *b3.task);
}

/** 同项目正式任务上限（§6.1 防单项目独占）——超出排队；Preview 不计。 */
TEST_F(SchedulerEventBusTest, PerProjectFormalCapQueuesThirdTask)
{
    m_config.inlineEligible = [](const TaskSubmission&) { return false; };
    ResourceBudget budget;
    budget.maxConcurrentTasks = 8;
    budget.maxTasksPerProject = 1;
    makeScheduler(budget);

    // 半区 A（正式上限）：同项目 Quick q1 出队（正式在途 1＝上限满）。
    const SubmitResult q1 = m_scheduler->submit(baseSubmission(core::EvaluationMode::Quick));
    ASSERT_TRUE(q1.accepted);
    m_scheduler->tick();
    EXPECT_EQ(m_controller->tryState(*q1.task).value(), core::TaskState::Preparing);

    // 半区 B（Preview 不计正式额度，Background 队头序 p1→p2）：若 Preview
    // 被错误计入正式额度，p1 会因"q1 已占满上限 1"被队头阻塞——两帧
    // Preview 先后出队即证明 Preview 不占正式额度（§6.1"正式任务"限定）。
    const SubmitResult p1 = m_scheduler->submit(baseSubmission(core::EvaluationMode::Preview));
    const SubmitResult p2 = m_scheduler->submit(baseSubmission(core::EvaluationMode::Preview));
    ASSERT_TRUE(p1.accepted && p2.accepted);
    m_scheduler->tick();
    EXPECT_EQ(m_controller->tryState(*p1.task).value(), core::TaskState::Preparing);
    m_scheduler->tick();
    EXPECT_EQ(m_controller->tryState(*p2.task).value(), core::TaskState::Preparing);

    // 半区 C（正式额度满→排队；队头阻塞，登记 §15.4）：q2 受上限约束
    // 排队，并挡住其后入队的 Background 任务 b——严格 FIFO 序可预测性
    // 优先于吞吐（D-04 排序键精神）。
    const SubmitResult q2 = m_scheduler->submit(baseSubmission(core::EvaluationMode::Quick));
    const SubmitResult b = m_scheduler->submit(
        baseSubmission(core::EvaluationMode::Preview, TaskPriority::Background));
    ASSERT_TRUE(q2.accepted && b.accepted);
    m_scheduler->tick();
    EXPECT_EQ(m_controller->tryState(*q2.task).value(), core::TaskState::Queued)
        << "同项目正式在途已满——q2 排队（防单项目独占）";
    EXPECT_EQ(m_controller->tryState(*b.task).value(), core::TaskState::Queued)
        << "队头 q2 受限——队头阻塞语义挡住后续 Background 任务";
}

// =====================================================================
// D-07：进度对外节流 ≤10 Hz（实现参数非需求值——§6.1）
// =====================================================================

TEST_F(SchedulerEventBusTest, ProgressThrottledToTenHz_D07)
{
    // §6.1"进度报告与节流"：同任务对外发布 ≤10 Hz（100 ms 窗，实现参数
    // D-07）。虚拟时钟推进断言：窗内帧丢弃、窗外帧放行——最新放行帧是
    // 对外唯一可见面。关闭内联：任务停留非终态（进度承载面）。
    m_config.inlineEligible = [](const TaskSubmission&) { return false; };
    makeScheduler();
    const SubmitResult r = m_scheduler->submit(baseSubmission());
    ASSERT_TRUE(r.accepted);
    const TaskId id = *r.task;

    auto report = [this, &id](const char* phase) {
        m_scheduler->reportProgress(id, ProgressReport::make(10, phase, 1, 10));
        m_scheduler->tick();   // 段 a 应用放行帧
    };

    report("t0");               // 首帧（无基准时刻）——立即放行
    m_clock->advance(std::chrono::milliseconds{50});
    report("t50-dropped");      // 窗内——丢弃
    m_clock->advance(std::chrono::milliseconds{50});
    report("t100");             // 恰出窗——放行
    m_clock->advance(std::chrono::milliseconds{50});
    report("t150-dropped");     // 窗内——丢弃
    m_clock->advance(std::chrono::milliseconds{50});
    report("t200");             // 出窗——放行

    const std::optional<TaskSnapshot> snap = m_scheduler->tryTask(id);
    ASSERT_TRUE(snap.has_value());
    ASSERT_TRUE(snap->progress.has_value());
    // 窗内帧已被节流丢弃：对外可见的最近帧是 t200（而非 t150/t50）。
    EXPECT_EQ(snap->progress->phaseToken, "t200");
    EXPECT_EQ(snap->progress->percent, 10);
}

/** 节流关闭（间隔 0）＝逐帧透传（实现参数的对照面——D-07 非强制常量）。 */
TEST_F(SchedulerEventBusTest, ProgressThrottleDisabledPassesAllFrames)
{
    m_config.progressMinInterval = std::chrono::milliseconds{0};
    m_config.inlineEligible = [](const TaskSubmission&) { return false; };
    makeScheduler();
    const SubmitResult r = m_scheduler->submit(baseSubmission());
    ASSERT_TRUE(r.accepted);

    for (int i = 0; i < 3; ++i) {
        m_scheduler->reportProgress(*r.task, ProgressReport::make(i + 1, "p", 1, 3));
        m_scheduler->tick();
    }
    const std::optional<TaskSnapshot> snap = m_scheduler->tryTask(*r.task);
    ASSERT_TRUE(snap.has_value());
    ASSERT_TRUE(snap->progress.has_value());
    EXPECT_EQ(snap->progress->percent, 3) << "不节流＝逐帧应用";
}

/** 进度帧违约面：未知任务/终态任务的帧＝调用方违约 fail-fast。 */
TEST_F(SchedulerEventBusTest, ProgressRejectsUnknownAndTerminalTasks)
{
    makeScheduler();
    // 未知任务——通道帧必经受理，携带未知任务＝调用方契约违约。
    EXPECT_THROW(m_scheduler->reportProgress(TaskId::generate(),
                                             ProgressReport::make(1, "p", 0, 1)),
                 ExecutionError);

    // 终态任务——流式更新无出边（§4.2 progress 行）。
    const SubmitResult r = m_scheduler->submit(baseSubmission());
    ASSERT_TRUE(r.accepted);
    m_scheduler->tick();   // 内联走链至 Completed
    EXPECT_THROW(m_scheduler->reportProgress(*r.task,
                                             ProgressReport::make(1, "p", 0, 1)),
                 ExecutionError);
}

// =====================================================================
// UX-10 边界：phaseToken 透传——execution 零新增状态词（映射归 ui）
// =====================================================================

TEST_F(SchedulerEventBusTest, PhaseTokenPassedThroughVerbatim_UX_10)
{
    // §2.2/§13 ui 行：进度阶段 token 是 worker 域词——execution 原样
    // 承载不解读；七态状态词与映射归 ui（execution 不新增状态词）。
    // 断言：任意 token（含非 ui 词形的域 token）透传后逐字符相等。
    m_config.inlineEligible = [](const TaskSubmission&) { return false; };
    makeScheduler();
    const SubmitResult r = m_scheduler->submit(baseSubmission());
    ASSERT_TRUE(r.accepted);

    const std::string token = "kin.batch.progress.phase-7";   // worker 域词形
    m_scheduler->reportProgress(*r.task, ProgressReport::make(40, token, 4, 10));
    m_scheduler->tick();

    const std::optional<TaskSnapshot> snap = m_scheduler->tryTask(*r.task);
    ASSERT_TRUE(snap.has_value());
    ASSERT_TRUE(snap->progress.has_value());
    EXPECT_EQ(snap->progress->phaseToken, token) << "逐字符透传——无映射无改写";

    // 进度不入领域事件（core D-09）——事件流只含状态变更，无进度帧。
    for (const core::DomainEvent& e : m_eventSink.snapshot()) {
        EXPECT_EQ(e.kind, core::DomainEventKind::TaskStatusChanged);
    }
}

// =====================================================================
// NFR-PERF-01：UI 线程零计算——提交/查询在计算本体阻塞期间照常前进
// =====================================================================

TEST_F(SchedulerEventBusTest, SubmitAndQueryNonBlockingDuringComputation_NFR_PERF_01)
{
    // §6.1"UI 线程不阻塞"/§6.3 内联门槛的结构断言（NFR-PERF-01：>1 s
    // 转后台）：计算本体（内联执行体）阻塞在调度域时，UI 线程的 submit/
    // tryTask 仍即时前进（锁只保护簿记不覆盖计算——文件头调度线程模型；
    // §11 EX-BLD-1/T05 完成条件的结构断言承载）。
    auto blocking = std::make_unique<BlockingExecutor>();
    BlockingExecutor* blockingPtr = blocking.get();
    makeScheduler();
    m_scheduler->setInlineExecutor(blockingPtr);   // 换装阻塞执行体

    const SubmitResult first = m_scheduler->submit(baseSubmission());
    ASSERT_TRUE(first.accepted);

    // 调度域线程：tick 进入内联执行段并阻塞（模拟 >1 s 的可预测轻任务
    // 正在调度线程上计算）。
    std::thread schedulerDomain([this] { m_scheduler->tick(); });

    // UI 线程半区（本测试线程）：提交/查询在计算期间必须即返——经
    // future 有界等待证明（若实现错误地让 submit 等待计算完成，等待
    // 超时即失败；不 sleep——确定性条件为执行体放行）。
    auto submitFuture = std::async(std::launch::async, [this] {
        return m_scheduler->submit(baseSubmission());
    });
    ASSERT_EQ(submitFuture.wait_for(std::chrono::seconds{2}), std::future_status::ready)
        << "提交在计算本体运行期间被阻塞——违反 NFR-PERF-01 非阻塞结构";
    const SubmitResult second = submitFuture.get();
    EXPECT_TRUE(second.accepted) << "计算期间提交照常受理（排队等待调度）";

    // 查询同样不阻塞：新任务 Queued（未受计算影响），首任务 Running。
    const std::optional<TaskSnapshot> secondSnap = m_scheduler->tryTask(*second.task);
    ASSERT_TRUE(secondSnap.has_value());
    EXPECT_EQ(secondSnap->state, core::TaskState::Queued);

    // 放行计算本体——调度域走链收尾并退出。
    blockingPtr->release();
    schedulerDomain.join();
    EXPECT_EQ(m_controller->tryState(*first.task).value_or(core::TaskState::Queued),
              core::TaskState::Completed);
}

// =====================================================================
// §7.5：关闭排空——排队取消（CancelQueuedAndWait）＋关闭后提交违约
// =====================================================================

TEST_F(SchedulerEventBusTest, ShutdownCancelQueuedAndWaitDrains)
{
    m_config.inlineEligible = [](const TaskSubmission&) { return false; };
    makeScheduler();
    const SubmitResult a = m_scheduler->submit(baseSubmission());
    const SubmitResult b = m_scheduler->submit(baseSubmission());
    ASSERT_TRUE(a.accepted && b.accepted);

    m_scheduler->shutdown(DrainPolicy::CancelQueuedAndWait);
    // 排队任务不阻塞 drained（§7.5/DrainCoordinator 语义：CancelQueuedAndWait
    // 的排队已在 shutdown 时清空——取消命令已受理；drained＝无在途运行，
    // 无永久等待的轮询面）。排队取消的状态转移由排空复合拍推进：
    m_scheduler->tick();
    EXPECT_EQ(m_controller->tryState(*a.task).value(), core::TaskState::Canceled);
    EXPECT_EQ(m_controller->tryState(*b.task).value(), core::TaskState::Canceled);
    EXPECT_TRUE(m_scheduler->drained()) << "无在途运行——排空完成";

    // 正常取消零错误诊断（UX-03）：两任务诊断累积为空。
    for (const std::optional<TaskSnapshot> snap :
         {m_scheduler->tryTask(*a.task), m_scheduler->tryTask(*b.task)}) {
        ASSERT_TRUE(snap.has_value());
        EXPECT_EQ(snap->termination, TerminationCause::Canceled);
    }

    // 关闭后提交＝调用方违约（§10.1 合法调用行）——ExecutionError(ContextClosed)。
    try {
        (void)m_scheduler->submit(baseSubmission());
        FAIL() << "关闭后提交必须抛 ContextClosed";
    } catch (const ExecutionError& e) {
        EXPECT_EQ(e.code(), ExecutionErrorCode::ContextClosed);
    }
}

/** shutdown 幂等（§10.1 注）：首次策略生效、二次关闭不重复动作。 */
TEST_F(SchedulerEventBusTest, ShutdownIsIdempotent)
{
    m_config.inlineEligible = [](const TaskSubmission&) { return false; };
    makeScheduler();
    const SubmitResult a = m_scheduler->submit(baseSubmission());
    ASSERT_TRUE(a.accepted);

    // 首次 KeepQueuedTerminate：排队保留（P-EX-6——随会话终结消失）。
    m_scheduler->shutdown(DrainPolicy::KeepQueuedTerminate);
    // 二次关闭（策略漂移的故意误用）：幂等 no-op——首次保留语义不被
    // 第二次的 CancelQueued 破坏（DrainCoordinator 内部标志保证）。
    m_scheduler->shutdown(DrainPolicy::CancelQueuedAndWait);
    m_scheduler->tick();
    EXPECT_EQ(m_controller->tryState(*a.task).value(), core::TaskState::Queued)
        << "KeepQueued 保留语义不被幂等二次关闭破坏";

    // 关闭后提交＝调用方违约（幂等关闭仍是关闭）。
    try {
        (void)m_scheduler->submit(baseSubmission());
        FAIL() << "关闭后提交必须抛 ContextClosed";
    } catch (const ExecutionError& e) {
        EXPECT_EQ(e.code(), ExecutionErrorCode::ContextClosed);
    }
}

// =====================================================================
// 查询投影面：tasksByProject 过滤（PM-03 任务清单数据源）
// =====================================================================

TEST_F(SchedulerEventBusTest, TasksByProjectFiltersByAnchoredProject)
{
    makeScheduler();
    ASSERT_TRUE(m_scheduler->submit(baseSubmission()).accepted);

    // 本项目快照锚定 store 项目——过滤命中；异项目查询为空。
    EXPECT_EQ(m_scheduler->tasksByProject(m_snapshot->project).size(), 1u);
    EXPECT_EQ(m_scheduler->tasksByProject(core::ProjectId::generate()).size(), 0u);
}

/** 预算运行期可调且不影响任务身份（§6.1——setResourceBudget 契约）。 */
TEST_F(SchedulerEventBusTest, ResourceBudgetAdjustableWithoutIdentityChange)
{
    ResourceBudget budget;
    budget.queueCapacity = 1;
    makeScheduler(budget);
    const SubmitResult first = m_scheduler->submit(baseSubmission());
    ASSERT_TRUE(first.accepted);
    EXPECT_FALSE(m_scheduler->submit(baseSubmission()).accepted) << "容量 1 已满";

    // 运行期放宽——已受理任务的 TaskId/状态不受预算变化影响（§6.1
    // "预算变化只影响排队与派发时机"）。
    ResourceBudget wider;
    wider.queueCapacity = 8;
    m_scheduler->setResourceBudget(wider);
    const SubmitResult second = m_scheduler->submit(baseSubmission());
    ASSERT_TRUE(second.accepted);
    EXPECT_NE(second.task->toCanonical(), first.task->toCanonical());
    EXPECT_EQ(m_scheduler->tryTask(*first.task)->state, core::TaskState::Queued);
}

}  // namespace
