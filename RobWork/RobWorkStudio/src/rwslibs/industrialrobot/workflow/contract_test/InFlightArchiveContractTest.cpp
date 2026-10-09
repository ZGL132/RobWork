/**
 * @file   InFlightArchiveContractTest.cpp
 * @brief  等待在途归档（A7）的端到端契约测试（WF-VER-210 执行半区——
 *         units/workflow.md §11.2"210 等待在途归档"＋WP-22-T06 落位注记
 *         "端到端在途任务归 WP-22-T13 主线"的划归承载）。
 *
 * 设计依据：
 *   - units/workflow.md §7.3（关闭编排——等待选项＝等待存储上下文排空）、
 *     §5.4（与 execution 的关闭协作——shutdown(DrainPolicy)/drained 消费面）、
 *     §11.2（WF-VER-210：在途运行→关闭-等待→存储上下文保持至归档完成；
 *     迟到结果归档原修订——观测点＝归档完成事件）
 *   - REQUIREMENTS.md §17 PM-03（关闭统一确认——等待选项覆盖在途归档）、
 *     AT-21；ARCHITECTURE.md §6.8 A7（界面会话与存储上下文分离——存储
 *     上下文保持到在途归档＋草稿落盘完成）
 *   - execution.md §7.5（关闭排空——CancelQueuedAndWait：取消排队、在途
 *     运行执行至归档完成；drained＝无在途运行）、§10.1（调度器契约——
 *     submit/shutdown/drained/tasksByProject）
 *
 * 契约测试形态（§11.0——"契约测试＝跨单元联合"）：与 project 真实存储＋
 * **真实 execution 全链**（TaskController＋DrainCoordinator＋TaskScheduler＋
 * DomainEventBusImpl——EX 单元生产代码，非桩）联合：
 *   - 任务快照锚定真实黄金项目（projectId/branch/tip 修订——真实 store 取数），
 *     经真实调度器提交→调度线程 tick 推进→内联执行体阻塞（"在途运行"事实）；
 *   - workflow::ICloseDrainPort 的**真实桥**（L5 装配桥的测试等价物）桥接
 *     调度器契约：hasActiveTask/taskStates＝tasksByProject 投影；waitDrain＝
 *     shutdown(CancelQueuedAndWait)＋有界轮询 drained——编排面对 execution
 *     的消费契约在真实执行链上兑现（CloseFlowContractTest 的脚本化端口桩
 *     钉编排面形状，本文件钉端到端语义——WP-22-T06 注记的分工）；
 *   - 用户决策桩选"等待"→CloseFlow::run 在后台编排线程等待排空→测试线程
 *     释放执行体→任务真实到达 Completed 终态（归档完成事件）→drained→
 *     存储上下文 requestClose→closed。
 *
 * 确定性纪律（testkit §6.5/§6.6）：不 sleep——线程会合全部经条件变量/
 * 状态轮询的确定性条件（在途状态到达/排空完成），无定时猜测。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记

#include <sdurws/ird/core/Events.hpp>            // DomainEvent/TaskStatusChangedPayload
#include <sdurws/ird/evidence/Evaluator.hpp>     // IProducerRegistryView（协作面替身接口）
#include <sdurws/ird/evidence/Snapshot.hpp>      // AnalysisSnapshot/SnapshotBuilder（冻结快照）
#include <sdurws/ird/execution/Controller.hpp>   // TaskController（取消/状态查询）
#include <sdurws/ird/execution/Errors.hpp>
#include <sdurws/ird/execution/EventBus.hpp>     // DomainEventBusImpl（真实事件总线）
#include <sdurws/ird/execution/Scheduler.hpp>    // TaskScheduler/DrainCoordinator（真实排空）
#include <sdurws/ird/project/ProjectStore.hpp>   // ProjectStoreFactory（真实存储）
#include <sdurws/ird/project/QueryPort.hpp>      // branchTips（快照锚定取数）
#include <sdurws/ird/workflow/Lifecycle.hpp>     // CloseFlow/ICloseDrainPort（被测编排核）

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

using namespace sdurws::ird;
namespace exe = sdurws::ird::execution;
namespace ev = sdurws::ird::evidence;
namespace pd = sdurws::ird::project;
using workflow::CloseFlowOutcome;
using workflow::RunningTaskDecision;

// =====================================================================
// 协作面替身（SchedulerEventBusTest 同款形态——校验面消费契约的验证载体）
// =====================================================================

/// 评估器注册表替身：键→契约版本（V1 校验消费面）。
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

/// 修订闭包替身：全部接纳（V2 包含性——夹具快照对象在锚定修订内）。
class AcceptAllClosureSource final : public ev::IRevisionClosureSource {
public:
    bool objectInRevision(core::RevisionId, core::ObjectId, core::ContentVersion) const override
    {
        return true;
    }
};

/// 阻塞内联执行体（"在途运行"的事实施载体——run 阻塞到 release，模拟
/// 可预测轻任务的计算本体；release 后以 Completed 终结＝任务完成归档）。
class BlockingExecutor final : public exe::IInlineRunExecutor {
public:
    exe::InlineRunOutcome run(const exe::TaskRecord&) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this] { return m_release; });
        exe::InlineRunOutcome out;
        out.cause = exe::TerminationCause::Completed;
        return out;
    }
    /// 放行计算本体（任务到达终态——"归档完成"的执行侧事实）。
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

/// 事件收集 sink（mutex 保护：总线投递线程回调＋主线程断言）。
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

private:
    mutable std::mutex m_mutex;
    std::vector<core::DomainEvent> m_events;
};

// =====================================================================
// 决策/草稿端口桩（用户选"等待"；本契约场景无未应用草稿——三选跳过）
// =====================================================================

class WaitDecisions final : public workflow::ICloseDecisionPort {
public:
    RunningTaskDecision taskAnswer = RunningTaskDecision::Wait;
    workflow::DraftDisposition collectDraftDisposition(
        const workflow::CloseDialogData&) override
    {
        return workflow::DraftDisposition::Cancel;  // 不到达（无草稿——兜底）
    }
    RunningTaskDecision collectRunningTaskDecision(
        const workflow::CloseDialogData& data) override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_prompts.push_back(data);
        return taskAnswer;
    }
    std::vector<workflow::CloseDialogData> prompts()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_prompts;
    }

private:
    std::mutex m_mutex;
    std::vector<workflow::CloseDialogData> m_prompts;
};

/// 空草稿端口桩（清单空＝三选决策点跳过——编排核 D1"否"边）。
class NoDraftsPort final : public workflow::ICloseDraftPort {
public:
    std::vector<std::string> unappliedDraftModules() override { return {}; }
    bool saveDrafts() override { return true; }
    bool discardDrafts() override { return true; }
};

// =====================================================================
// 排空端口的真实桥（L5 装配桥的测试等价物——桥接真实 execution 契约）
// =====================================================================

/**
 * @brief ICloseDrainPort 的真实 execution 桥——编排面对执行侧的消费契约在
 *        真实调度器上兑现（CloseFlowContractTest 的 ScriptedDrain 桩钉形状
 *        与调用序，本桥钉语义）：
 *          - hasActiveTask/taskStates＝TaskScheduler::tasksByProject 投影
 *            （提交快照锚定项目过滤——execution §10.1 原文签名；非终态
 *            ＝Queued/Preparing/Running/Paused/Canceling）；
 *          - waitDrain＝shutdown(CancelQueuedAndWait)（排队取消＋在途执行
 *            至归档完成——execution §7.5）＋有界轮询 drained（无永久等待
 *            ——drained 判定语义）；
 *          - cooperativeCancel＝逐任务 requestCancel 后同轮询（本契约场景
 *            不消费——等待分支；实现齐备供编排核任意分支可达）。
 *
 * 进入等待即通知测试线程（waitEntered cv）——释放执行体的确定性会合点
 * （先确认编排已真实进入等待排空，再放行任务完成）。
 */
class ExecutionDrainBridge final : public workflow::ICloseDrainPort {
public:
    ExecutionDrainBridge(exe::TaskScheduler& scheduler, exe::TaskController& controller)
        : m_scheduler(scheduler)
        , m_controller(controller)
    {
    }

    bool hasActiveTask(core::ProjectId project) override
    {
        return anyNonTerminal(m_scheduler.tasksByProject(project));
    }

    std::vector<core::TaskState> taskStates(core::ProjectId project) override
    {
        std::vector<core::TaskState> states;
        for (const exe::TaskSnapshot& snap : m_scheduler.tasksByProject(project)) {
            states.push_back(snap.state);
        }
        return states;
    }

    bool waitDrain() override
    {
        // 会合通知：编排已真实进入等待排空（测试线程此后才释放执行体）。
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_waitEntered = true;
        }
        m_waitEnteredCv.notify_all();

        // §7.5 等待分支：停止派发＋取消排队＋在途执行至归档完成。
        m_scheduler.shutdown(exe::DrainPolicy::CancelQueuedAndWait);
        // 有界轮询 drained（无永久等待——drained＝无 Preparing/Running/
        // Canceling；在途任务由测试线程 release 后经调度线程收尾到达终态）。
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{30};
        while (!m_scheduler.drained()) {
            if (std::chrono::steady_clock::now() > deadline) {
                return false;  // 有界等待超限——编排核转 Failed（不永久阻塞）
            }
            std::this_thread::yield();
        }
        return true;
    }

    bool cooperativeCancel() override
    {
        // 协作取消：对项目任务清单逐任务 requestCancel（任意线程——命令
        // 通道），再走同一有界排空轮询（取消协议的临时区清理/检查点保留
        // 语义由 execution 承载——N4 分工）。
        for (const exe::TaskSnapshot& snap :
             m_scheduler.tasksByProject(m_project)) {
            (void)m_controller.requestCancel(snap.taskId);
        }
        m_scheduler.shutdown(exe::DrainPolicy::CancelQueuedAndWait);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{30};
        while (!m_scheduler.drained()) {
            if (std::chrono::steady_clock::now() > deadline) {
                return false;
            }
            std::this_thread::yield();
        }
        return true;
    }

    /// 项目身份注入（提交后回填——tasksByProject 过滤键）。
    void setProject(core::ProjectId project) { m_project = project; }

    /// 等待编排进入 waitDrain（确定性会合——不含 sleep：条件变量等待）。
    bool waitForWaitEntered(std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_waitEnteredCv.wait_for(lock, timeout, [this] { return m_waitEntered; });
    }

private:
    static bool anyNonTerminal(const std::vector<exe::TaskSnapshot>& tasks)
    {
        for (const exe::TaskSnapshot& snap : tasks) {
            switch (snap.state) {
            case core::TaskState::Queued:
            case core::TaskState::Preparing:
            case core::TaskState::Running:
            case core::TaskState::Paused:
            case core::TaskState::Canceling:
                return true;
            default:
                break;  // 终态（Completed/Canceled/Failed/Interrupted）不算在途
            }
        }
        return false;
    }

    exe::TaskScheduler& m_scheduler;   ///< 真实调度器（非 owning——夹具持有）
    exe::TaskController& m_controller; ///< 真实控制器（协作取消通道）
    core::ProjectId m_project;         ///< 过滤键（提交后回填）
    std::mutex m_mutex;
    std::condition_variable m_waitEnteredCv;
    bool m_waitEntered = false;
};

// =====================================================================
// 夹具：真实黄金项目＋真实 execution 全链（调度器三件套＋总线）
// =====================================================================

class InFlightArchiveContract : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        std::error_code ec;
        s_base = fs::temp_directory_path(ec) / "ird_wf_inflight_contract_test";
        ASSERT_FALSE(ec);
        fs::remove_all(s_base, ec);  // 前次运行残留防御（总根重建）
        fs::create_directories(s_base, ec);
        ASSERT_FALSE(ec) << "无法创建测试根目录: " << s_base.string();
    }

    static void TearDownTestSuite()
    {
        std::error_code ec;
        fs::remove_all(s_base, ec);
    }

    void SetUp() override
    {
        // 真实黄金项目（临时目录——ComparisonContract 同款序列化）。
        static std::atomic<unsigned long long> seq{0};
        m_dir = s_base / ("case" + std::to_string(seq.fetch_add(1)) + "-"
                          + std::to_string(
                              std::chrono::steady_clock::now().time_since_epoch().count()));
        m_opened = std::make_unique<pd::OpenStoreResult>(
            pd::ProjectStoreFactory::createNew(m_dir, "wf 在途归档契约", nullptr, nullptr));
        ASSERT_TRUE(m_opened->store != nullptr);
        ASSERT_TRUE(m_opened->store->writable());

        // 真实 execution 全链（SchedulerEventBusTest 同款装配——协作面替身
        // +真实 store/控制器/排空编排/调度器/事件总线）。
        m_clock = [] { return std::chrono::steady_clock::now(); };
        m_registry = std::make_unique<ScriptedProducerRegistry>(
            ScriptedProducerRegistry({{"kin-batch-ik", 7}}));
        m_closure = std::make_unique<AcceptAllClosureSource>();
        m_executor = std::make_unique<BlockingExecutor>();
        m_controller = std::make_unique<exe::TaskController>(exe::TaskController::Config{}, m_clock);
        m_drain = std::make_unique<exe::DrainCoordinator>(
            *m_controller, exe::DrainCoordinator::Config{}, m_clock);
        // 内联资格谓词（可切换——等待腿 true＝内联走链；协作取消腿 false＝
        // 普通排队〔可取消形态——submitAndWaitInFlight 注〕）。
        m_config.inlineEligible = [this](const exe::TaskSubmission&) {
            return m_inlineEnabled.load();
        };
        m_collab.evaluators = m_registry.get();
        m_collab.closure = m_closure.get();
        m_collab.store = m_opened->store.get();  // V3 写权限真实面（PM-07）
        m_collab.guard = nullptr;                // 无注入半区（跳过——合法装配）
        m_collab.capabilities = nullptr;         // 最小能力（本契约不消费暂停）
        m_scheduler = std::make_unique<exe::TaskScheduler>(
            *m_controller, *m_drain, m_collab, m_config, m_clock);
        m_scheduler->setEventBus(&m_bus);
        m_scheduler->setInlineExecutor(m_executor.get());
        m_subscription = m_bus.subscribe(m_eventSink);
    }

    void TearDown() override
    {
        m_subscription.reset();  // 句柄先于总线析构退订（RAII 契约）
        if (m_tickThread.joinable()) {
            m_tickThread.join();
        }
        m_scheduler.reset();
        m_drain.reset();
        m_controller.reset();
        m_opened.reset();  // 释放写锁（先于目录清理）
        std::error_code ec;
        fs::remove_all(m_dir, ec);
    }

    /// 构建锚定真实项目/分支/tip 修订的冻结快照（"迟到结果归档原修订"的
    /// 锚定三元组——真实 store 取数，非生成值）。
    ev::AnalysisSnapshot anchoredSnapshot()
    {
        const auto tips = m_opened->store->query().branchTips();
        EXPECT_FALSE(tips.empty());
        m_anchorBranch = tips.empty() ? core::BranchId{} : tips[0].id;
        m_anchorRevision = tips.empty() ? core::RevisionId{} : tips[0].tip;

        ev::SnapshotBuilder b;
        b.setIdentity(m_opened->store->projectId(), m_anchorBranch, m_anchorRevision, 5);
        b.setPolicyRef(ev::PolicyRef{core::ContentIdentity::fromCanonical(
            std::string{"cid-"} + std::string(64, 'a'))});
        b.setNameMapRef(ev::NameMapRef{core::ContentIdentity::fromCanonical(
            std::string{"cid-"} + std::string(64, 'b'))});
        ev::ReproductionBlock r;
        r.productVersion = "industrialrobot-designer 0.1.0";
        r.evidenceContractVersion = "evidence-contract/1";
        r.codecVersions = {"snapshot-codec/1", "slice-codec/1"};
        b.setReproduction(r);
        ev::ObjectRefEntry obj;
        obj.objectId = core::ObjectId::fromCanonical(
            std::string{"obj-"} + std::string(32, 'a'));
        obj.contentVersion = core::ContentVersion::fromCanonical(
            std::string{"cv-"} + std::string(64, 'a'));
        obj.objectTypeToken = "robot-design";
        obj.digest = obj.contentVersion.bytes;
        b.addObjectRef(obj);
        b.addCase(ev::CaseEntry{obj.objectId, "case-a", true, true});
        return b.build(*m_closure);
    }

    /// 提交在途任务并等待到达在途态（确定性条件轮询——不含 sleep）。
    ///
    /// @param inlineEligible [in] 内联资格：true＝内联走链（阻塞执行体令
    ///        任务停在 Preparing——计算本体运行中，等待归档腿的"在途运行"
    ///        形态；T5 在执行体返回后——Scheduler.hpp §6.3）；false＝普通
    ///        排队（T2 出队后停在 Preparing 等普通派发——协作取消腿形态：
    ///        单线程调度域中内联计算本体占用调度线程会使取消命令〔poll
    ///        消费〕不可达——可取消的"在途"＝排队/准备中，EX 单元
    ///        ShutdownCancelQueuedAndWaitDrains 同款形态）。
    exe::TaskId submitAndWaitInFlight(bool inlineEligible)
    {
        m_inlineEnabled.store(inlineEligible);
        exe::TaskSubmission submission;
        submission.snapshot = anchoredSnapshot();
        submission.evaluatorKey = "kin-batch-ik";
        submission.contractVersion = 7;
        submission.mode = core::EvaluationMode::Preview;  // 内联门槛（§6.3）
        submission.priority = exe::TaskPriority::Background;
        const exe::SubmitResult submitted = m_scheduler->submit(std::move(submission));
        EXPECT_TRUE(submitted.accepted) << "在途任务提交被拒（夹具前置）";
        EXPECT_TRUE(submitted.task.has_value());
        if (!submitted.accepted || !submitted.task) {
            return exe::TaskId{};
        }
        // 调度线程：tick 循环推进（命令通道消费〔poll〕与出队/排空推进
        // 都在 tick 拍内——轮询 drained 的有界等待依赖本线程持续推进）。
        m_tickThread = std::thread([this] {
            while (!m_stopTick.load() || !m_scheduler->drained()) {
                m_scheduler->tick();
                std::this_thread::yield();
            }
        });
        // 确定性条件等待：状态到达在途态（非终态即 D2"运行中"事实——
        // 超时即夹具失败；非 void 返回函数内禁 ASSERT——以 ADD_FAILURE＋
        // 哨兵返回承载）。
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{30};
        while (true) {
            const std::optional<core::TaskState> state =
                m_controller->tryState(*submitted.task);
            if (state == core::TaskState::Preparing || state == core::TaskState::Running) {
                break;
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                ADD_FAILURE() << "在途任务未在时限内到达在途态（夹具前置失败）";
                return exe::TaskId{};
            }
            std::this_thread::yield();
        }
        return *submitted.task;
    }

    /// 收尾：放行任务完成＋停调度线程＋关存储上下文（用例自选断言后调用）。
    void finishDrainAndClose()
    {
        m_executor->release();  // 任务到达 Completed（归档完成事实）
        m_stopTick.store(true);
        if (m_tickThread.joinable()) {
            m_tickThread.join();
        }
    }

    static fs::path s_base;
    fs::path m_dir;
    std::unique_ptr<pd::OpenStoreResult> m_opened;
    exe::TaskScheduler::Collaboration m_collab;
    exe::TaskScheduler::Config m_config;
    std::function<std::chrono::steady_clock::time_point()> m_clock;
    std::unique_ptr<ScriptedProducerRegistry> m_registry;
    std::unique_ptr<AcceptAllClosureSource> m_closure;
    std::unique_ptr<BlockingExecutor> m_executor;
    std::unique_ptr<exe::TaskController> m_controller;
    std::unique_ptr<exe::DrainCoordinator> m_drain;
    std::unique_ptr<exe::TaskScheduler> m_scheduler;
    CollectingEventSink m_eventSink;
    exe::DomainEventBusImpl m_bus;
    std::unique_ptr<core::IEventSubscription> m_subscription;
    std::thread m_tickThread;
    std::atomic<bool> m_stopTick{false};
    std::atomic<bool> m_inlineEnabled{true};  // 内联资格开关（两腿形态切换）
    core::BranchId m_anchorBranch;
    core::RevisionId m_anchorRevision;
};

fs::path InFlightArchiveContract::s_base;

// =====================================================================
// WF-VER-210 端到端：在途运行→关闭-等待→归档完成→存储上下文关闭
// =====================================================================

/**
 * 端到端主线（真实 execution 全链——WP-22-T06 划归本任务的执行半区）：
 *   ① 任务在途（真实调度器 Running＋阻塞执行体）；
 *   ② 关闭编排（后台线程）→二选"等待"→排空桥 waitDrain 真实进入
 *      shutdown(CancelQueuedAndWait)；
 *   ③ A7 观测：等待期间存储上下文保持（未关闭——主线程在会合点断言）；
 *   ④ 释放执行体→任务真实到达 Completed（归档完成事件：TaskStatusChanged
 *      载荷锚定三元组＝提交快照原修订——"迟到结果归档原修订"）；
 *   ⑤ drained→waitDrain 返回→存储上下文 requestClose→closed（编排 Proceed
 *      ＝存储上下文已排空关闭——CloseFlowOutcome 不变量）。
 */
TEST_F(InFlightArchiveContract, WF_VER_210_EndToEndInFlightArchiveWaitedBeforeClose)
{
    IRD_TEST_INFO((std::vector<std::string>{"PM-03", "TASK-03"}),
                  (std::vector<std::string>{"AT-21", "WF-VER-210"}));

    // ---- ① 在途运行（真实提交→Running）。
    const exe::TaskId taskId = submitAndWaitInFlight(true);
    ASSERT_TRUE(taskId.isValid());
    const core::ProjectId projectId = m_opened->store->projectId();  // 关闭前取定
    // （closed 后的元数据读取面不进入断言——身份对照一律用本局部量。）

    // ---- ② 关闭编排（后台线程——用户选"等待"）。
    WaitDecisions decisions;
    NoDraftsPort noDrafts;
    ExecutionDrainBridge drainBridge(*m_scheduler, *m_controller);
    drainBridge.setProject(projectId);

    workflow::CloseFlowRequest request;
    request.projectId = projectId;
    request.decisions = &decisions;
    request.drafts = &noDrafts;
    request.drain = &drainBridge;

    auto outcomeFuture = std::async(std::launch::async, [this, &request] {
        return workflow::CloseFlow::run(workflow::CloseKind::Close,
                                        *m_opened->store, request);
    });

    // ---- ③ 确定性会合：编排已真实进入等待排空；此刻任务仍在途、存储
    //      上下文保持未关闭（A7 前半——"存储上下文保持至归档完成"）。
    ASSERT_TRUE(drainBridge.waitForWaitEntered(std::chrono::seconds{30}))
        << "编排未在时限内进入等待排空（桥会合失败）";
    EXPECT_FALSE(m_opened->store->closed())
        << "等待排空期间存储上下文必须保持（A7——界面会话与存储上下文分离）";
    EXPECT_TRUE(m_opened->store->writable()) << "排空完成前写权限未释放";

    // 二选决策点数据面：九态清单透传（真实任务投影——Running）。
    const std::vector<workflow::CloseDialogData> prompts = decisions.prompts();
    ASSERT_EQ(prompts.size(), 1u) << "二选决策点恰好一次";
    ASSERT_FALSE(prompts[0].taskStates.empty());
    EXPECT_EQ(prompts[0].taskStates.front(), core::TaskState::Preparing)
        << "任务清单来自真实调度投影（在途态）";

    // ---- ④ 释放执行体→任务真实到达 Completed（归档完成）→drained。
    finishDrainAndClose();

    // ---- ⑤ 编排收口：Proceed＋waitedForArchiveDrain＋存储上下文已关闭。
    const CloseFlowOutcome outcome = outcomeFuture.get();
    EXPECT_EQ(outcome.result, CloseFlowOutcome::Result::Proceed);
    EXPECT_TRUE(outcome.waitedForArchiveDrain) << "等待排空分支观测位（A7 承接证据）";
    EXPECT_FALSE(outcome.cooperativeCancelled);
    EXPECT_FALSE(outcome.failure.has_value());
    EXPECT_TRUE(m_opened->store->closed())
        << "排空完成后存储上下文已关闭（Proceed ⇔ closed——不变量）";
    EXPECT_FALSE(m_opened->store->writable()) << "写权限随上下文释放消失（A7/SA-17）";

    // 归档完成事件：TaskStatusChanged(Completed) 已发布（总线 idle 后快照）
    // ——payload 锚定三元组＝提交快照原项目/原分支/原修订（"迟到结果归档
    // 原修订"的观测面）。
    m_bus.waitForIdle();
    bool completedSeen = false;
    for (const core::DomainEvent& e : m_eventSink.snapshot()) {
        if (e.kind == core::DomainEventKind::TaskStatusChanged) {
            const core::TaskStatusChangedPayload payload = e.asTaskStatusChanged();
            if (payload.newState == core::TaskState::Completed) {
                completedSeen = true;
                EXPECT_EQ(payload.task.project.toCanonical(), projectId.toCanonical());
                EXPECT_EQ(payload.task.branch, m_anchorBranch)
                    << "归档锚定分支＝提交快照分支（原修订语义）";
                EXPECT_EQ(payload.task.revision, m_anchorRevision)
                    << "归档锚定修订＝提交快照 tip（原修订——不是关闭后的新修订）";
            }
        }
    }
    EXPECT_TRUE(completedSeen) << "归档完成事件未发布（任务未真实到达 Completed）";
}

// =====================================================================
// 端到端对照腿：协作取消分支（同一真实执行链——WF-VER-211 执行半区补充）
// =====================================================================

/**
 * 协作取消端到端（对照腿——真实取消协议）：任务在途→二选"协作取消"→
 * 桥逐任务 requestCancel→真实取消协议收敛（Canceled 终态）→drained→
 * 存储上下文关闭。观测：cooperativeCancelled 登记位＋零错误诊断（UX-03
 * ——关闭触发的取消不是错误）＋事件流含 Canceled 终态。
 */
TEST_F(InFlightArchiveContract, WF_VER_211_EndToEndCooperativeCancelReachesClosed)
{
    IRD_TEST_INFO((std::vector<std::string>{"PM-03"}),
                  (std::vector<std::string>{"AT-21", "WF-VER-211"}));

    const exe::TaskId taskId = submitAndWaitInFlight(false);
    ASSERT_TRUE(taskId.isValid());

    WaitDecisions decisions;
    decisions.taskAnswer = RunningTaskDecision::CooperativeCancel;
    NoDraftsPort noDrafts;
    ExecutionDrainBridge drainBridge(*m_scheduler, *m_controller);
    drainBridge.setProject(m_opened->store->projectId());

    workflow::CloseFlowRequest request;
    request.projectId = m_opened->store->projectId();
    request.decisions = &decisions;
    request.drafts = &noDrafts;
    request.drain = &drainBridge;

    auto outcomeFuture = std::async(std::launch::async, [this, &request] {
        return workflow::CloseFlow::run(workflow::CloseKind::Close,
                                        *m_opened->store, request);
    });

    // 会合：进入协作取消（桥 waitDrain 仅等待分支调用——协作取消走
    // cooperativeCancel；会合点改用任务状态到达 Canceled 的确定性条件）。
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{30};
    while (m_controller->tryState(taskId) != core::TaskState::Canceled) {
        ASSERT_LT(std::chrono::steady_clock::now(), deadline)
            << "协作取消未在时限内收敛（取消协议链断裂；drained="
            << (m_scheduler->drained() ? "true" : "false")
            << " 状态="
            << [&] {
                   const auto s = m_controller->tryState(taskId);
                   return s.has_value() ? std::to_string(static_cast<int>(*s))
                                        : std::string{"none"};
               }()
            << " 任务数=" << m_controller->taskCount();
        std::this_thread::yield();
    }

    m_stopTick.store(true);
    if (m_tickThread.joinable()) {
        m_tickThread.join();
    }

    const CloseFlowOutcome outcome = outcomeFuture.get();
    EXPECT_EQ(outcome.result, CloseFlowOutcome::Result::Proceed);
    EXPECT_TRUE(outcome.cooperativeCancelled) << "协作取消观测位（AT-21 承接证据）";
    EXPECT_FALSE(outcome.failure.has_value()) << "取消不是错误（UX-03——零失败呈现）";
    EXPECT_TRUE(m_opened->store->closed()) << "取消路径同样走完存储上下文排空（A7）";

    // 事件流：任务真实到达 Canceled 终态（取消协议的状态机侧事实）。
    m_bus.waitForIdle();
    bool canceledSeen = false;
    for (const core::DomainEvent& e : m_eventSink.snapshot()) {
        if (e.kind == core::DomainEventKind::TaskStatusChanged
            && e.asTaskStatusChanged().newState == core::TaskState::Canceled) {
            canceledSeen = true;
        }
    }
    EXPECT_TRUE(canceledSeen) << "取消终态事件未发布";
}

}  // namespace
