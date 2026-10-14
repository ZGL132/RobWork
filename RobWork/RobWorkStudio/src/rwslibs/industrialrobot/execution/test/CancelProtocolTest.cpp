/**
 * @file   CancelProtocolTest.cpp
 * @brief  取消与终止协议编排用例组（EX-T03）——协作窗 2 s/10 s 时序
 *         （ManualClock 虚拟推进，不 sleep）、强杀编排（EX-FORCE-TERMINATED
 *         显式区分）、运行超时（同卡死路径＋诊断区分）、DrainPolicy 排空
 *         两路径与无永久等待兜底、正常取消零错误诊断（UX-03）。
 *
 * 设计依据（用例与契约 acceptance 对照——每条 acceptance 至少一个具名用例）：
 *   - units/execution.md §11 EX-SM-1/3/4（取消三形态）、EX-WKR-4/5（强杀
 *     与运行超时）、EX-ARC-4 的执行侧语义本体（行为用例归 EX-T09）、§7.1
 *     （协作式取消逐步协议）、§7.4（四类行为对照）、§7.5（关闭二选）、
 *     §10.2（Ack 语义）、§10.3（worker 操作语义）
 *   - 需求 NFR-PERF-02（2 s/10 s 上游协议值——用例以 ManualClock::advance
 *     推进到 1.9 s/9.9 s/10 s/窗满边界断言，不 sleep——testkit §6.5/§6.6）、
 *     UX-03（正常取消零错误诊断——EX-SM-1/3/4 观测点诊断空断言）
 *   - 任务契约 tasks/foundation/EX-T03.json acceptance 1~4 逐条
 *
 * 边界声明（替身不构成业务证明——§11 边界声明同源）：worker 为替身
 * （FakeWorkerHandle——进程树终止/存活探测是记录值，真进程 Job 语义归
 * EX-WKR-2/3 的真进程场景，EX-T06/T09 承载）；检查点磁盘断言验证的是
 * **取消编排零磁盘动作**（检查点写出协议归 EX-T08）；归档 abandon 调用
 * 归 EX-T04。2 s/10 s 协议值以 static_assert 钉住公开常量（上游需求值
 * 不改动——常量本身即被测对象）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/core/Errors.hpp>
#include <sdurws/ird/execution/Controller.hpp>
#include <sdurws/ird/execution/Errors.hpp>
#include <sdurws/ird/execution/Scheduler.hpp>
#include <sdurws/ird/execution/StateMachine.hpp>
#include <sdurws/ird/execution/TaskTypes.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

using namespace sdurws::ird::execution;
using sdurws::ird::core::TaskState;

// core:: 限定名不随 using-directive 到达（同 TaskStateMachineTest 的说明
// ——命名空间 enclosing 查找规则），显式建短名别名。
namespace core = sdurws::ird::core;

// =====================================================================
// 测试设施（AGENTS §2.7：fixture 与辅助函数按 §2.3 注释规范）
// =====================================================================

/// 手动时钟（testkit §6.6 形态：now()＋advance 虚拟推进——协议时序用例
/// 的唯一时间源，杜绝 sleep 后断言）。
class ManualClock {
public:
    using TimePoint = std::chrono::steady_clock::time_point;

    /// 当前虚拟时刻（steady 纪元起——协议只做时长比较，纪元起点无约束）。
    TimePoint now() const noexcept { return m_now; }

    /// 虚拟推进（超时/协作窗类场景——testkit §6.6 原文签名）。
    void advance(std::chrono::milliseconds dt) noexcept { m_now += dt; }

private:
    TimePoint m_now{};   ///< 虚拟当前时刻（steady 纪元起）
};

/// 事件收集替身（ITaskEventSink 最小实现——状态时序断言的观测面）。
class RecordingSink : public ITaskEventSink {
public:
    void onTaskStatusChanged(const core::TaskStatusChangedPayload& payload) override
    {
        m_states.push_back(payload.newState);
    }

    const std::vector<TaskState>& states() const noexcept { return m_states; }

    /// 清空已收集事件（分段观测——装配期转移（T1/T2/T5）验毕后清空，
    /// 只断言被测协议段的事件序）。
    void clear() noexcept { m_states.clear(); }

private:
    std::vector<TaskState> m_states;   ///< 按发布序收集的 newState 序列
};

/// 可编程 worker 替身（IWorkerHandle 实现——EX-T06 supervisor/JobScope 的
/// 接缝对位；内部以 ICancelSignal 形状承载取消标志，模拟 §7.1 信号传递
/// 链的 worker 宿主半区：通道 CancelRequest→宿主置标志→"评估器"在批次
/// 边界观测信号收敛）。
class FakeWorkerHandle : public IWorkerHandle {
public:
    /// 宿主侧取消标志（ICancelSignal 实现——协作点轮询查询端；置位后
    /// 不回退，符合接口契约"cancellationRequested 一经置位不得复位"）。
    class HostCancelSignal : public ICancelSignal {
    public:
        explicit HostCancelSignal(const FakeWorkerHandle* owner) noexcept
            : m_owner(owner)
        {
        }

        bool cancellationRequested() const override
        {
            return m_owner->m_cancelFlag;   // 读宿主标志位
        }

    private:
        const FakeWorkerHandle* m_owner;   ///< 宿主回指针（非所有权）
    };

    /**
     * @param clock       [in] 虚拟时钟（与被测编排同一时间源）
     * @param settleAfter [in] 自取消信号置位起经此时长后批次收敛；
     *                    nullopt＝永不收敛（取消超时/排空兜底场景）
     */
    FakeWorkerHandle(ManualClock& clock,
                     std::optional<std::chrono::milliseconds> settleAfter)
        : m_clock(clock)
        , m_settleAfter(settleAfter)
        , m_signal(this)
    {
    }

    // ---- IWorkerHandle（记录调用——编排行为的断言面） ----

    void requestCooperativeCancel() override
    {
        ++m_cancelRequests;
        m_cancelFlag = true;                  // 宿主置标志（§7.1 信号链）
        m_cancelRequestedAt = m_clock.now();  // 收敛计时起点
    }

    bool cancelSettled() const override
    {
        // 协议次序钉子：信号未置位（未发 CancelRequest）时永不收敛——
        // "未取消先收敛"是协议违例，替身拒绝伪造该形态。收敛＝信号
        // 已置位＋脚本时刻到达（批次边界）。
        if (!m_cancelFlag || !m_settleAfter.has_value()) {
            return false;
        }
        return m_clock.now() - *m_cancelRequestedAt >= *m_settleAfter;
    }

    /// 通道 PauseRequest（§7.2——WP-21-T07；宿主置暂停标志＋计时起点）。
    void requestPause() override
    {
        ++m_pauseRequests;
        m_pauseFlag = true;                  // 宿主置标志（§7.2 信号链）
        m_pauseRequestedAt = m_clock.now();  // 检查点边界写出计时起点
    }

    /// 暂停确认观测（§7.2 暂停确认边界＝检查点写出并确认）：协议次序钉
    /// 子同取消——未发 PauseRequest 永不确认（替身拒绝伪造"未请求先确认"
    /// 形态）；确认＝已置位＋脚本时刻到达。默认 nullopt＝永不确认（既有
    /// 取消用例零影响）；暂停用例经 setPauseSettleAfter 脚本化。
    bool pauseSettled() const override
    {
        if (!m_pauseFlag || !m_pauseSettleAfter.has_value()) {
            return false;
        }
        return m_clock.now() - *m_pauseRequestedAt >= *m_pauseSettleAfter;
    }

    void terminateForce(TerminationCause cause) override
    {
        ++m_terminateCalls;
        m_lastCause = cause;
        m_alive = false;        // 进程树终止（KILL_ON_JOB_CLOSE 兜杀——
                                // 实现契约见 IWorkerHandle::terminateForce 注）
        m_handleClosed = true;  // 句柄关闭＋reap（§10.3 terminateForce 注）
    }

    bool isAlive() const override { return m_alive; }

    // ---- 断言面（具名访问器——断言自解释） ----
    ICancelSignal& signal() noexcept { return m_signal; }             ///< 协作点查询端
    int cancelRequests() const noexcept { return m_cancelRequests; }  ///< CancelRequest 次数（恰一次）
    int terminateCalls() const noexcept { return m_terminateCalls; }  ///< 强杀次数（恰一次）
    bool handleClosed() const noexcept { return m_handleClosed; }     ///< 句柄关闭（EX-WKR-4 观测点）
    TerminationCause lastCause() const { return *m_lastCause; }       ///< 强杀 cause
    int pauseRequests() const noexcept { return m_pauseRequests; }    ///< PauseRequest 次数（WP-21-T07——恰一次）
    /// 暂停确认脚本（WP-21-T07——自 PauseRequest 置位起经此时长后确认）。
    void setPauseSettleAfter(std::chrono::milliseconds after) noexcept
    {
        m_pauseSettleAfter = after;
    }

private:
    ManualClock& m_clock;                        ///< 虚拟时钟（注入）
    std::optional<std::chrono::milliseconds> m_settleAfter;  ///< 收敛脚本
    HostCancelSignal m_signal;                   ///< 宿主信号（评估器观测端）
    bool m_cancelFlag = false;                   ///< 宿主取消标志（置位不回退）
    int m_cancelRequests = 0;                    ///< CancelRequest 计数
    std::optional<ManualClock::TimePoint> m_cancelRequestedAt;  ///< 信号置位时刻
    int m_terminateCalls = 0;                    ///< 强杀计数
    std::optional<TerminationCause> m_lastCause; ///< 最近强杀 cause
    bool m_alive = true;                         ///< 存活语义
    bool m_handleClosed = false;                 ///< 句柄关闭语义
    bool m_pauseFlag = false;                    ///< 宿主暂停标志（置位不回退——WP-21-T07）
    int m_pauseRequests = 0;                     ///< PauseRequest 计数（恰一次）
    std::optional<ManualClock::TimePoint> m_pauseRequestedAt;   ///< 暂停请求置位时刻
    std::optional<std::chrono::milliseconds> m_pauseSettleAfter; ///< 暂停确认脚本（默认永不确认）
};

/// 派发闸门替身（IDispatchGate 实现——"停止派发批次"的观测面，EX-SM-3）。
class FakeDispatchGate : public IDispatchGate {
public:
    void stopDispatch(TaskId task) override { m_stopped.push_back(task); }

    /// 指定任务是否已被停止派发。
    bool stopped(TaskId task) const noexcept
    {
        for (const auto& t : m_stopped) {
            if (t == task) {
                return true;
            }
        }
        return false;
    }

private:
    std::vector<TaskId> m_stopped;   ///< 收到 stopDispatch 的任务序
};

/// 临时目录（RAII——EX-SM-4 的检查点磁盘断言面；目录名含时刻＋进程内
/// 计数避免碰撞，析构递归清理；测试不写共享位置——testkit §6.2 同源）。
class TempDir {
public:
    TempDir()
    {
        static unsigned long long counter = 0;   ///< 进程内递增计数（同拍构造防撞）
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        m_path = std::filesystem::temp_directory_path()
            / ("ird-exsm4-" + std::to_string(stamp) + "-" + std::to_string(++counter));
        std::filesystem::create_directories(m_path);
    }

    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(m_path, ec);   // 清理失败静默（临时区）
    }

    const std::filesystem::path& path() const noexcept { return m_path; }

private:
    std::filesystem::path m_path;   ///< 本用例私有临时目录
};

/// 构造一条合法受理记录（§4.2 字段表默认值＋调用方给定能力；形态同
/// TaskStateMachineTest 的 makeQueuedRecord——评估键取合法词形）。
TaskRecord makeQueuedRecord(TaskCapability capability = {})
{
    TaskRecord r;
    r.taskId = TaskId::generate();
    r.submission.snapshot.project = core::ProjectId::generate();
    r.submission.snapshot.branch = core::BranchId::generate();
    r.submission.snapshot.revision = core::RevisionId::generate();
    r.submission.evaluatorKey = "kin-batch-ik";
    r.submission.contractVersion = 1;
    r.submission.mode = core::EvaluationMode::Verified;
    r.capability = capability;
    return r;
}

/// 装配一个已到 Running 的受管任务（T1→T2→bindRun→T5——形态同
/// TaskStateMachineTest::makeRunning；unique_ptr 供 attachTask 交接）。
std::unique_ptr<TaskStateMachine> makeRunning(TaskCapability capability,
                                              RecordingSink& sink)
{
    auto machine = std::make_unique<TaskStateMachine>(
        TaskStateMachine::forAcceptedSubmission(makeQueuedRecord(capability), &sink));
    EXPECT_EQ(machine->request(TransitionTrigger::DispatchDequeued).accepted, true);  // T2
    machine->bindRun(core::RunId::generate());                                        // §4.3
    EXPECT_EQ(machine->request(TransitionTrigger::PrepareSucceeded).accepted, true);  // T5
    return machine;
}

/// 装配一个已到 Paused 的受管任务（Running→T10；暂停确认后 worker 已
/// 回收——§7.2，故 Paused 任务不绑 worker）。T10 能力守卫要求
/// supportsPause=true——设施强制置位（被测对象是取消协议不是能力门控，
/// 后者已由 EX-T02 的 EX-SM-7 覆盖）。
std::unique_ptr<TaskStateMachine> makePaused(TaskCapability capability,
                                             RecordingSink& sink)
{
    capability.supportsPause = true;   // T10 守卫前置——见函数注
    auto machine = makeRunning(capability, sink);
    EXPECT_EQ(machine->request(TransitionTrigger::PauseConfirmed).accepted, true);  // T10
    return machine;
}

/// 装配一个 Queued 受管任务（T1 直达——取消直达路径的被测形态）。
std::unique_ptr<TaskStateMachine> makeQueued(RecordingSink& sink)
{
    return std::make_unique<TaskStateMachine>(
        TaskStateMachine::forAcceptedSubmission(makeQueuedRecord(), &sink));
}

/// 诊断码存在性检查（EX-FORCE-TERMINATED/EX-WORKER-HUNG 观测面）。
bool hasDiagCode(const TaskStateMachine& machine, const std::string& code)
{
    for (const auto& d : machine.record().diagnostics) {
        if (d.code == code) {
            return true;
        }
    }
    return false;
}

/// 任务诊断累积是否为空（UX-03"正常取消零错误诊断"的观测辅助——经
/// forEachTask 读取；严格断言"零诊断"强于"零错误级"，取消路径两种
/// 形态都不允许出现）。
bool noDiagnostics(TaskController& controller, TaskId id)
{
    bool empty = true;
    controller.forEachTask([&empty, id](TaskId tid, TaskStateMachine& machine) {
        if (tid == id && !machine.record().diagnostics.empty()) {
            empty = false;
        }
    });
    return empty;
}

/// 期望 ExecutionError(InvalidState)（EX-SM-6 同款复合断言——码＋detail
/// 稳定前缀；detail 前缀 "execution/controller:" 是开发诊断检索面）。
void expectInvalidState(const std::function<void()>& action)
{
    try {
        action();
        FAIL() << "期望 ExecutionError(InvalidState)，但未抛出";
    } catch (const ExecutionError& e) {
        EXPECT_EQ(e.code(), ExecutionErrorCode::InvalidState);
        EXPECT_NE(std::string(e.what()).find("execution/controller:"), std::string::npos)
            << "detail 须携带稳定前缀 execution/controller:（开发诊断）——实际: "
            << e.what();
    }
}

/// 协议常量钉子（NFR-PERF-02 上游值不改动——编译期断言，常量本身即被测对象）。
static_assert(TaskController::kCancelAcceptWindow == std::chrono::milliseconds{2000},
              "2 s 取消生效窗为 NFR-PERF-02 上游需求值，不得改动");
static_assert(TaskController::kCancelCooperativeWindow == std::chrono::milliseconds{10000},
              "10 s 协作收敛窗为 NFR-PERF-02 上游需求值，不得改动");

/// 把 ClockFn 绑定到 ManualClock（装配辅助——两编排必须同一时间源，
/// 否则兜底阈值与协作窗度量漂移——Scheduler.hpp 头注纪律）。
TaskController::ClockFn bindClock(ManualClock& clock)
{
    return [&clock]() { return clock.now(); };
}

// =====================================================================
// EX-SM-3（acceptance 1 前半＋acceptance 4）：Running 取消协作窗时序
// =====================================================================

/**
 * EX-SM-3 主臂：Running 取消 2 s 内入 Canceling＋停止派发批次；批次
 * 8 s（<10 s）收敛→Canceled；全程诊断空（UX-03）。时序全部由
 * ManualClock 虚拟推进（不 sleep——testkit §6.5）。
 */
TEST(CancelProtocol, RunningCancelEntersCancelingWithin2sAndSettlesWithin10s_EX_SM_3_NFR_PERF_02)
{
    ManualClock clock;
    RecordingSink sink;
    TaskController controller(TaskController::Config{}, bindClock(clock));
    FakeDispatchGate gate;
    controller.setDispatchGate(&gate);

    FakeWorkerHandle worker(clock, /*settleAfter=*/std::chrono::milliseconds{8000});
    auto machine = makeRunning(TaskCapability{}, sink);
    const TaskId id = machine->record().taskId;
    controller.attachTask(std::move(machine));
    controller.bindWorker(id, &worker);
    ASSERT_EQ(controller.tryState(id), TaskState::Running);
    sink.clear();   // 装配期事件（T1/T2/T5）已验毕——只观测取消协议段的事件序

    // ---- t0：取消请求受理（accepted 且无反馈——UX-03 的应答面）----
    const CancelAck ack = controller.requestCancel(id);
    EXPECT_TRUE(ack.accepted);
    EXPECT_FALSE(ack.feedback.has_value()) << "正常取消的应答不携带错误反馈（UX-03）";

    // ---- t0+1.9 s（<2 s 生效窗内）：一次 poll 后必须已 Canceling 并停派发。
    // 本协议的生效延迟上界＝一个 poll 周期（命令段是 poll 第一动作——
    // "取消命令插队于派发之前"），1.9 s 处的 poll 即断言 2 s 窗内生效。----
    clock.advance(std::chrono::milliseconds{1900});
    controller.poll();
    EXPECT_EQ(controller.tryState(id), TaskState::Canceling)
        << "取消请求须 2 s 内进入 Canceling（NFR-PERF-02/ARCH §4.4 条 1；虚拟时钟断言）";
    EXPECT_TRUE(gate.stopped(id))
        << "进入 Canceling 须停止派发新批次（§7.1 步 1；EX-SM-3 观测点）";
    EXPECT_EQ(worker.cancelRequests(), 1)
        << "通道 CancelRequest 须恰发一次（§7.1 步 2；重复发送会让 worker 重复置位）";

    // 中途窗内检查：t0+3.9 s（信号置位后 2 s、收敛脚本 8 s 未到、窗未满）
    // ——保持 Canceling，不得提前转移或强杀。
    clock.advance(std::chrono::milliseconds{2000});
    controller.poll();
    EXPECT_EQ(controller.tryState(id), TaskState::Canceling)
        << "批次未收敛前不得提前转移（协作窗语义）";

    // ---- t0+9.9 s：批次收敛（自取消信号置位 t0+1.9 s 起 8 s——<10 s 窗）
    // → CancelAck → Canceled（§7.1 步 2；EX-SM-3"批次 10 s 收敛"）。----
    clock.advance(std::chrono::milliseconds{6000});
    controller.poll();
    EXPECT_EQ(controller.tryState(id), TaskState::Canceled);

    // ---- 状态时序（EX-SM-3 观测点"状态时序（虚拟时钟断言）"）----
    const std::vector<TaskState> expected{TaskState::Canceling, TaskState::Canceled};
    EXPECT_EQ(sink.states(), expected) << "事件序＝转移序（§10.4 同任务 FIFO）";

    // ---- UX-03：正常取消全程诊断空（acceptance 4；EX-SM-3 观测点"诊断空"）----
    EXPECT_TRUE(noDiagnostics(controller, id)) << "正常取消不产生任何诊断（UX-03）";
}

/**
 * EX-SM-3 超时臂（acceptance 2"取消协议无永久等待路径"）：批次永不收敛
 * →10 s 收敛窗耗尽→强杀兜底→Failed＋EX-FORCE-TERMINATED；终态后推进
 * 10 小时状态稳定（不存在任何永久等待臂）。
 */
TEST(CancelProtocol, CancelWindowTimeoutForceKillsNoPermanentWait_EX_SM_3_acceptance2)
{
    ManualClock clock;
    RecordingSink sink;
    TaskController controller(TaskController::Config{}, bindClock(clock));

    FakeWorkerHandle worker(clock, /*settleAfter=*/std::nullopt);   // 永不收敛
    auto machine = makeRunning(TaskCapability{}, sink);
    const TaskId id = machine->record().taskId;
    controller.attachTask(std::move(machine));
    controller.bindWorker(id, &worker);

    // t0 请求取消，t0+2 s 生效入 Canceling（收敛窗自此起量）。
    ASSERT_TRUE(controller.requestCancel(id).accepted);
    clock.advance(std::chrono::milliseconds{2000});
    controller.poll();
    ASSERT_EQ(controller.tryState(id), TaskState::Canceling);

    // t=进入 Canceling+9.9 s（窗内 0.1 s 余量）：不得强杀（窗未满）。
    clock.advance(std::chrono::milliseconds{9900});
    controller.poll();
    EXPECT_EQ(controller.tryState(id), TaskState::Canceling)
        << "收敛窗（10 s）未满不得强杀——窗边界内的批次仍有机会自然收敛";
    EXPECT_EQ(worker.terminateCalls(), 0);

    // t=窗满（+10 s）：强杀兜底（§7.1 步 3）——T13→Failed(EX-FORCE-TERMINATED)。
    clock.advance(std::chrono::milliseconds{100});
    controller.poll();
    EXPECT_EQ(controller.tryState(id), TaskState::Failed);
    EXPECT_EQ(worker.terminateCalls(), 1) << "进程树终止恰一次（KILL_ON_JOB_CLOSE 编排面）";
    EXPECT_EQ(worker.lastCause(), TerminationCause::ForceTerminated);

    // 强杀标记与检查点保留（§7.4 诊断行："已强制终止，检查点保留"）。
    controller.forEachTask([&](TaskId, TaskStateMachine& m) {
        EXPECT_EQ(m.record().termination, TerminationCause::ForceTerminated)
            << "T13 显式标记：强杀不伪装普通失败（§5.3 T13）";
        EXPECT_TRUE(hasDiagCode(m, "EX-FORCE-TERMINATED"))
            << "取消超时强杀须携带 EX-FORCE-TERMINATED（EX-WKR-4 观测点'诊断标记'）";
    });

    // 无永久等待自证：终态后推进 10 小时并多次 poll——状态稳定、无进一步
    // 动作（acceptance 2"10 s 收敛＋强杀兜底"的兜底后不存在新等待臂）。
    clock.advance(std::chrono::hours{10});
    controller.poll();
    controller.poll();
    EXPECT_EQ(controller.tryState(id), TaskState::Failed);
    EXPECT_EQ(worker.terminateCalls(), 1) << "终态后不再重复强杀（恰一次防线）";
}

// =====================================================================
// EX-SM-4（acceptance 1 后半）：暂停中取消保检查点（磁盘断言）
// =====================================================================

/**
 * EX-SM-4：Paused 任务（检查点已确认、worker 已回收）取消→直达 Canceled
 * （不经协作窗——无在途批次）；检查点文件与 manifest 在取消前后逐字节
 * 一致（可续跑——磁盘断言；编排对检查点目录零动作）。
 */
TEST(CancelProtocol, PausedCancelRetainsCheckpointFilesOnDisk_EX_SM_4_ARCH_11_2_6)
{
    ManualClock clock;
    RecordingSink sink;
    TaskController controller(TaskController::Config{}, bindClock(clock));

    // 检查点已确认的在盘形态（EX-SM-4 前置"任务暂停中（检查点已确认）"
    // ——批次文件＋manifest；写出协议归 EX-T08，本用例只布置磁盘事实）。
    TempDir dir;
    const auto ckptDir = dir.path() / "checkpoints" / "run-under-test" / "1";
    std::filesystem::create_directories(ckptDir);
    const auto batchFile = ckptDir / "batch-1.bin";
    const auto manifestFile = ckptDir / "manifest.json";
    const std::string batchBytes = "checkpoint-payload-bytes-v1";
    const std::string manifestBytes = R"({"run":"run-under-test","seq":1,"complete":true})";
    {
        std::ofstream(batchFile, std::ios::binary).write(batchBytes.data(),
                                                         std::streamsize(batchBytes.size()));
        std::ofstream(manifestFile, std::ios::binary).write(manifestBytes.data(),
                                                            std::streamsize(manifestBytes.size()));
    }
    ASSERT_TRUE(std::filesystem::exists(batchFile));
    ASSERT_TRUE(std::filesystem::exists(manifestFile));

    auto machine = makePaused(TaskCapability{}, sink);   // Paused＋无 worker
    const TaskId id = machine->record().taskId;
    controller.attachTask(std::move(machine));
    ASSERT_EQ(controller.tryState(id), TaskState::Paused);
    sink.clear();   // 装配期事件已验毕——只观测取消段事件序

    // 暂停中取消：直达收敛（T12"暂停态无在途批次，直达"——不经 10 s 窗）。
    ASSERT_TRUE(controller.requestCancel(id).accepted);
    clock.advance(std::chrono::milliseconds{1});   // 下一拍即刻收敛——不推进窗
    controller.poll();
    EXPECT_EQ(controller.tryState(id), TaskState::Canceled)
        << "暂停中取消直达 Canceled（ARCH §4.3 A5/EX-SM-4：无在途批次不适用协作窗）";

    const std::vector<TaskState> expected{TaskState::Canceling, TaskState::Canceled};
    EXPECT_EQ(sink.states(), expected) << "状态序 Canceling→Canceled（T12+T4 两步——T12 已从"
                                          " Paused 进入）";

    // 磁盘断言：检查点文件/manifest 仍在且逐字节一致（EX-SM-4 观测点
    // "checkpoints 目录内容；磁盘断言"——可续跑的物质基础；T12 行
    // RetainLatestCheckpoint 语义的编排面＝零磁盘动作）。
    std::string readBatch(batchBytes.size(), '\0');
    std::string readManifest(manifestBytes.size(), '\0');
    {
        std::ifstream f(batchFile, std::ios::binary);
        f.read(readBatch.data(), std::streamsize(readBatch.size()));
        EXPECT_TRUE(f && f.gcount() == std::streamsize(batchBytes.size()))
            << "检查点批次文件须仍在（取消不删除检查点——EX-SM-4）";
        std::ifstream m(manifestFile, std::ios::binary);
        m.read(readManifest.data(), std::streamsize(readManifest.size()));
        EXPECT_TRUE(m && m.gcount() == std::streamsize(manifestBytes.size()))
            << "检查点 manifest 须仍在（可续跑判据——无 manifest 即中断）";
    }
    EXPECT_EQ(readBatch, batchBytes) << "批次文件内容须逐字节不变（编排零磁盘写）";
    EXPECT_EQ(readManifest, manifestBytes) << "manifest 内容须逐字节不变";

    // UX-03：暂停中取消同样零诊断（acceptance 4 覆盖 EX-SM-4 观测点）。
    EXPECT_TRUE(noDiagnostics(controller, id));
}

// =====================================================================
// EX-SM-1（acceptance 4）：Queued 取消直达＋重复取消幂等＋零诊断
// =====================================================================

TEST(CancelProtocol, QueuedCancelDirectIdempotentZeroDiagnostics_EX_SM_1_UX03)
{
    ManualClock clock;
    RecordingSink sink;
    TaskController controller(TaskController::Config{}, bindClock(clock));

    auto machine = makeQueued(sink);
    const TaskId id = machine->record().taskId;
    controller.attachTask(std::move(machine));
    ASSERT_EQ(controller.tryState(id), TaskState::Queued);
    sink.clear();   // 装配期 T1 事件已验毕——只观测取消段事件序

    // Queued 取消：直接出队（T3——不经协作窗，ARCH §4.3）；同拍直达
    // Canceled（T4——无在途批次）。
    const CancelAck first = controller.requestCancel(id);
    EXPECT_TRUE(first.accepted);
    EXPECT_FALSE(first.feedback.has_value());
    controller.poll();
    EXPECT_EQ(controller.tryState(id), TaskState::Canceled);

    const std::vector<TaskState> expected{TaskState::Canceling, TaskState::Canceled};
    EXPECT_EQ(sink.states(), expected) << "状态序 Canceling→Canceled（T3+T4 两步——T3 已从"
                                          " Queued 出队）";

    // 终态后的重复取消：accepted=false＋InvalidState 反馈（§10.2 前置注；
    // feedback 仅应答携带、不写任务诊断——终态记录不可变）。
    const CancelAck repeated = controller.requestCancel(id);
    EXPECT_FALSE(repeated.accepted);
    ASSERT_TRUE(repeated.feedback.has_value());
    EXPECT_EQ(repeated.feedback->code, "EX-INVALID-STATE")
        << "终态任务的取消拒绝反馈携带稳定码说明（结构化 Ack——§10.8）";
    EXPECT_EQ(controller.tryState(id), TaskState::Canceled) << "终态不变";

    // UX-03：排队取消零诊断（EX-SM-1 观测点"诊断空"）。
    EXPECT_TRUE(noDiagnostics(controller, id));
}

// =====================================================================
// EX-WKR-4（acceptance 2）：强制终止编排（KILL_ON_JOB_CLOSE 语义面）
// =====================================================================

TEST(CancelProtocol, RequestForceTerminateKillsProcessTreeAndMarksExplicitly_EX_WKR_4)
{
    ManualClock clock;
    RecordingSink sink;
    TaskController controller(TaskController::Config{}, bindClock(clock));
    FakeDispatchGate gate;
    controller.setDispatchGate(&gate);

    FakeWorkerHandle worker(clock, /*settleAfter=*/std::nullopt);
    auto machine = makeRunning(TaskCapability{}, sink);
    const TaskId id = machine->record().taskId;
    controller.attachTask(std::move(machine));
    controller.bindWorker(id, &worker);
    ASSERT_TRUE(worker.isAlive());
    sink.clear();   // 装配期事件已验毕——只观测强杀段事件序

    // 直接强杀（§10.2 requestForceTerminate——不经协作窗等待）。
    const StatusAck ack = controller.requestForceTerminate(id);
    EXPECT_TRUE(ack.accepted);
    EXPECT_EQ(ack.currentState, TaskState::Running) << "应答携带受理时刻状态";

    // 命令在 poll 中执行（任意线程入队→调度线程串行的协议面）。
    clock.advance(std::chrono::milliseconds{1});
    controller.poll();

    // 进程树终止语义面（EX-WKR-4 观测点"进程存活探测；诊断标记"）：
    // 终止恰一次、cause 显式、句柄关闭、存活翻转。
    EXPECT_EQ(worker.terminateCalls(), 1) << "进程树终止恰一次（KILL_ON_JOB_CLOSE——句柄关闭后兜杀）";
    EXPECT_EQ(worker.lastCause(), TerminationCause::ForceTerminated);
    EXPECT_TRUE(worker.handleClosed()) << "句柄关闭＋reap（§10.3 terminateForce 注）";
    EXPECT_FALSE(worker.isAlive()) << "终止后进程存活探测为假（EX-WKR-4 观测点）";
    EXPECT_TRUE(gate.stopped(id)) << "强杀路径同样停止派发（经 Canceling 进入 T13）";

    // 终态与显式标记（区别于普通失败——§5.3 T13"不伪装为普通失败"）。
    EXPECT_EQ(controller.tryState(id), TaskState::Failed);
    controller.forEachTask([&](TaskId, TaskStateMachine& m) {
        EXPECT_EQ(m.record().termination, TerminationCause::ForceTerminated);
        EXPECT_TRUE(hasDiagCode(m, "EX-FORCE-TERMINATED"));
        EXPECT_FALSE(hasDiagCode(m, "EX-WORKER-CRASHED"))
            << "强杀不得携带崩溃码（普通失败 EX-WORKER-CRASHED——两码互斥即'显式区别'）";
    });
    const std::vector<TaskState> expected{TaskState::Canceling, TaskState::Failed};
    EXPECT_EQ(sink.states(), expected) << "事件序 Canceling→Failed（强杀必经 T13 写入点）";

    // 终态后重复强杀：幂等 no-op（§10.2"终态任务→幂等 no-op"）——状态
    // 不变、无第二次终止调用。
    const StatusAck repeat = controller.requestForceTerminate(id);
    EXPECT_TRUE(repeat.accepted) << "幂等 no-op 以成功应答（UI 重复点击无害）";
    EXPECT_EQ(repeat.currentState, TaskState::Failed);
    clock.advance(std::chrono::milliseconds{1});
    controller.poll();
    EXPECT_EQ(worker.terminateCalls(), 1) << "不重复终止";
}

/**
 * EX-WKR-4 区分断言：同一协议下，强杀（EX-FORCE-TERMINATED＋
 * ForceTerminated）与普通失败（T9 RunFailed——无强杀码＋Failed 原因）
 * 的诊断/终结原因面互斥——"不伪装"的机器判读面。
 */
TEST(CancelProtocol, ForceKillDiagnosticsDifferFromOrdinaryFailure_EX_WKR_4)
{
    ManualClock clock;
    RecordingSink sink;
    TaskController controller(TaskController::Config{}, bindClock(clock));

    // 任务甲：强杀路径（收敛超时）。
    FakeWorkerHandle workerA(clock, std::nullopt);
    auto machineA = makeRunning(TaskCapability{}, sink);
    const TaskId idA = machineA->record().taskId;
    controller.attachTask(std::move(machineA));
    controller.bindWorker(idA, &workerA);
    ASSERT_TRUE(controller.requestCancel(idA).accepted);
    clock.advance(std::chrono::milliseconds{2000});   // 入 Canceling
    controller.poll();
    clock.advance(TaskController::kCancelCooperativeWindow);   // 窗满
    controller.poll();

    // 任务乙：普通失败（T9——评估器失败/崩溃类，编排外直接触发）。
    FakeWorkerHandle workerB(clock, std::nullopt);
    auto machineB = makeRunning(TaskCapability{}, sink);
    const TaskId idB = machineB->record().taskId;
    controller.attachTask(std::move(machineB));
    controller.bindWorker(idB, &workerB);
    controller.forEachTask([&](TaskId tid, TaskStateMachine& m) {
        if (tid == idB) {
            m.request(TransitionTrigger::RunFailed);   // T9：Running→Failed
        }
    });

    // 甲：ForceTerminated＋EX-FORCE-TERMINATED；乙：Failed＋无强杀码。
    controller.forEachTask([&](TaskId tid, TaskStateMachine& m) {
        if (tid == idA) {
            EXPECT_EQ(m.record().termination, TerminationCause::ForceTerminated);
            EXPECT_TRUE(hasDiagCode(m, "EX-FORCE-TERMINATED"));
        } else if (tid == idB) {
            EXPECT_EQ(m.record().termination, TerminationCause::Failed)
                << "普通失败的原因为 Failed（非 ForceTerminated）";
            EXPECT_FALSE(hasDiagCode(m, "EX-FORCE-TERMINATED"))
                << "普通失败不得携带强杀码（§7.4 诊断行两码互斥）";
        }
    });
}

// =====================================================================
// EX-WKR-5（acceptance 2）：运行超时走同卡死路径＋诊断区分 timeout
// =====================================================================

TEST(CancelProtocol, RunTimeoutTakesHungPathWithTimeoutDistinguishedDiag_EX_WKR_5)
{
    ManualClock clock;
    RecordingSink sink;
    TaskController controller(TaskController::Config{}, bindClock(clock));

    // 甲：声明 5 s 运行时限（能力声明字段——§11 EX-WKR-5 前置）。
    TaskCapability timed;
    timed.evaluationTimeout = std::chrono::milliseconds{5000};
    FakeWorkerHandle workerA(clock, std::nullopt);
    auto machineA = makeRunning(timed, sink);
    const TaskId idA = machineA->record().taskId;
    controller.attachTask(std::move(machineA));
    controller.bindWorker(idA, &workerA);
    sink.clear();   // 装配期事件已验毕——只观测超时强杀段事件序

    // 乙：未声明时限——不启用监视（对照臂：宁可不监视也不误杀）。
    FakeWorkerHandle workerB(clock, std::nullopt);
    auto machineB = makeRunning(TaskCapability{}, sink);
    const TaskId idB = machineB->record().taskId;
    controller.attachTask(std::move(machineB));
    controller.bindWorker(idB, &workerB);
    sink.clear();   // 乙的装配事件（T1/T2/T5）不在甲的事件序断言面内——清空

    // +4.9 s（时限内）：两任务都保持 Running。
    clock.advance(std::chrono::milliseconds{4900});
    controller.poll();
    EXPECT_EQ(controller.tryState(idA), TaskState::Running) << "时限内不得强杀";
    EXPECT_EQ(controller.tryState(idB), TaskState::Running);

    // +5 s（甲超时）：走卡死路径（§7.1——terminateForce→T13）。
    clock.advance(std::chrono::milliseconds{100});
    controller.poll();
    EXPECT_EQ(controller.tryState(idA), TaskState::Failed)
        << "运行超时→同卡死路径强杀（EX-WKR-5：同卡死路径）";
    EXPECT_EQ(workerA.terminateCalls(), 1);
    EXPECT_FALSE(workerA.isAlive());

    controller.forEachTask([&](TaskId tid, TaskStateMachine& m) {
        if (tid == idA) {
            EXPECT_EQ(m.record().termination, TerminationCause::ForceTerminated);
            // 诊断区分 timeout（EX-WKR-5 观测点"诊断区分 timeout"）：
            // 判定码 EX-WORKER-HUNG＋cause 携带运行超时标记（与心跳失联
            // 区分），随后 T13 的 EX-FORCE-TERMINATED 终结标记。
            EXPECT_TRUE(hasDiagCode(m, "EX-WORKER-HUNG")) << "运行超时须有判定诊断";
            EXPECT_TRUE(hasDiagCode(m, "EX-FORCE-TERMINATED")) << "强杀终结标记";
            for (const auto& d : m.record().diagnostics) {
                if (d.code == "EX-WORKER-HUNG") {
                    EXPECT_NE(d.cause.find("evaluationTimeout"), std::string::npos)
                        << "判定诊断 cause 须携带运行超时标记（区分心跳失联）";
                }
            }
            // 诊断因果序：判定原因（HUNG）先于终结标记（FORCE）。
            int hungIdx = -1;
            int forceIdx = -1;
            int idx = 0;
            for (const auto& d : m.record().diagnostics) {
                if (d.code == "EX-WORKER-HUNG") {
                    hungIdx = idx;
                }
                if (d.code == "EX-FORCE-TERMINATED") {
                    forceIdx = idx;
                }
                ++idx;
            }
            ASSERT_GE(hungIdx, 0);
            ASSERT_GE(forceIdx, 0);
            EXPECT_LT(hungIdx, forceIdx) << "判定原因先于终结标记（因果序）";
        }
        if (tid == idB) {
            EXPECT_EQ(m.record().state, TaskState::Running)
                << "未声明时限的任务不启用超时监视（nullopt＝不监视）";
        }
    });

    const std::vector<TaskState> expectedA{TaskState::Canceling, TaskState::Failed};
    EXPECT_EQ(sink.states(), expectedA)
        << "甲的事件序 Canceling→Failed（强杀序列的矩阵内路径）";
}

// =====================================================================
// DrainPolicy 排空（acceptance 3）：两路径＋协作取消＋超阈值兜底
// =====================================================================

/**
 * 等待排空路径（shutdown(CancelQueuedAndWait)）：排队任务即刻取消（直达
 * Canceled）；在途运行不被干预（强杀调用为零）执行至自然终态；随后
 * drained()==true（§7.5"在途运行执行至归档完成"的执行侧——归档面归
 * EX-T04，此处以任务终态为排空判据）。
 */
TEST(DrainPolicyTest, ShutdownCancelQueuedAndWaitDrainsInflightUntouched_acceptance3)
{
    ManualClock clock;
    RecordingSink sink;
    TaskController controller(TaskController::Config{}, bindClock(clock));
    DrainCoordinator drain(controller, DrainCoordinator::Config{}, bindClock(clock));

    auto queued = makeQueued(sink);
    const TaskId idQ = queued->record().taskId;
    controller.attachTask(std::move(queued));
    FakeWorkerHandle worker(clock, std::nullopt);
    auto running = makeRunning(TaskCapability{}, sink);
    const TaskId idR = running->record().taskId;
    controller.attachTask(std::move(running));
    controller.bindWorker(idR, &worker);
    ASSERT_FALSE(drain.closed());
    ASSERT_FALSE(drain.drained()) << "未关闭谈不上排空完成";

    drain.shutdown(DrainPolicy::CancelQueuedAndWait);
    EXPECT_TRUE(drain.closed());
    ASSERT_EQ(drain.policy(), DrainPolicy::CancelQueuedAndWait);

    // 排队任务在关闭时被取消：一次 poll 后直达 Canceled（UX-03 零诊断）。
    controller.poll();
    EXPECT_EQ(controller.tryState(idQ), TaskState::Canceled);
    EXPECT_TRUE(noDiagnostics(controller, idQ)) << "关闭触发的排队取消同样零错误诊断（UX-03）";

    // 在途运行不被干预（等待排空≠强杀——§7.5"在途运行执行至归档完成"）。
    EXPECT_EQ(controller.tryState(idR), TaskState::Running);
    EXPECT_EQ(worker.terminateCalls(), 0) << "等待排空不强制终止在途运行";
    EXPECT_FALSE(drain.drained()) << "在途运行未终结前不排空";

    // 在途运行自然完成（T8——结果接纳路径归 EX-T04，此处驱动状态机）。
    controller.forEachTask([&](TaskId tid, TaskStateMachine& m) {
        if (tid == idR) {
            m.request(TransitionTrigger::RunCompleted);
        }
    });
    controller.poll();
    EXPECT_TRUE(drain.drained()) << "无在途运行即排空完成（drained 判定语义）";
}

/**
 * 保留变体（shutdown(KeepQueuedTerminate)）：排队任务保留不取消（仍
 * Queued）；排队任务不阻塞 drained（保留是设计决定而非未决工作——
 * P-EX-6：Queued 纯内存随会话终结消失）；在途完成即排空。
 */
TEST(DrainPolicyTest, ShutdownKeepQueuedTerminateRetainsQueued_acceptance3)
{
    ManualClock clock;
    RecordingSink sink;
    TaskController controller(TaskController::Config{}, bindClock(clock));
    DrainCoordinator drain(controller, DrainCoordinator::Config{}, bindClock(clock));

    auto queued = makeQueued(sink);
    const TaskId idQ = queued->record().taskId;
    controller.attachTask(std::move(queued));
    FakeWorkerHandle worker(clock, std::nullopt);
    auto running = makeRunning(TaskCapability{}, sink);
    const TaskId idR = running->record().taskId;
    controller.attachTask(std::move(running));
    controller.bindWorker(idR, &worker);

    drain.shutdown(DrainPolicy::KeepQueuedTerminate);
    controller.poll();

    // 排队任务保留（不取消、不派发）。
    EXPECT_EQ(controller.tryState(idQ), TaskState::Queued)
        << "KeepQueuedTerminate：排队任务保留（随会话终结消失——P-EX-6）";

    // 在途运行自然完成→排空（排队不阻塞 drained）。
    controller.forEachTask([&](TaskId tid, TaskStateMachine& m) {
        if (tid == idR) {
            m.request(TransitionTrigger::RunCompleted);
        }
    });
    controller.poll();
    EXPECT_TRUE(drain.drained()) << "排队保留不阻塞排空判定（drained＝无在途）";
    EXPECT_EQ(controller.tryState(idQ), TaskState::Queued) << "排空后排队任务仍保留";
}

/**
 * 协作取消路径（§7.4 PM-03"协作取消"分支）：requestCancelAll 对排队任务
 * 直达取消、对在途任务走 §7.1 完整协作窗协议（2 s 生效＋8 s 脚本收敛）。
 */
TEST(DrainPolicyTest, RequestCancelAllCooperativeShutdownPath_acceptance3)
{
    ManualClock clock;
    RecordingSink sink;
    TaskController controller(TaskController::Config{}, bindClock(clock));
    DrainCoordinator drain(controller, DrainCoordinator::Config{}, bindClock(clock));

    auto queued = makeQueued(sink);
    const TaskId idQ = queued->record().taskId;
    controller.attachTask(std::move(queued));
    FakeWorkerHandle worker(clock, /*settleAfter=*/std::chrono::milliseconds{8000});
    auto running = makeRunning(TaskCapability{}, sink);
    const TaskId idR = running->record().taskId;
    controller.attachTask(std::move(running));
    controller.bindWorker(idR, &worker);

    // 协作取消分支（§7.4：用户选"协作取消"后关闭——先进入关闭态，再对
    // 任务清单批量 requestCancel；关闭态下排队任务由本路径显式处置，
    // drained() 自此可查询）。
    drain.shutdown(DrainPolicy::KeepQueuedTerminate);   // 排队保留——由协作取消显式处置
    EXPECT_EQ(drain.requestCancelAll(), std::size_t{2});

    // 第一拍：排队任务直达；在途任务入 Canceling（2 s 内生效）。
    clock.advance(std::chrono::milliseconds{1900});
    controller.poll();
    EXPECT_EQ(controller.tryState(idQ), TaskState::Canceled);
    EXPECT_EQ(controller.tryState(idR), TaskState::Canceling);

    // 在途任务经协作窗收敛（脚本 8 s < 10 s）。
    clock.advance(std::chrono::milliseconds{8000});
    controller.poll();
    EXPECT_EQ(controller.tryState(idR), TaskState::Canceled);
    EXPECT_TRUE(noDiagnostics(controller, idR)) << "协作取消零诊断（UX-03）";
    EXPECT_TRUE(drain.drained()) << "两任务终结即排空";
}

/**
 * 超阈值兜底（acceptance 2/3"无永久等待"）：关闭后在途运行永不收敛→
 * 推进至兜底阈值（实现参数 15 s）→自动 abandonAll(ForceTerminated)→
 * 在途任务 Failed(EX-FORCE-TERMINATED)→drained()==true；兜底恰一次。
 */
TEST(DrainPolicyTest, DrainAbandonThresholdForceAbandonsNoPermanentWait_acceptance2_3)
{
    ManualClock clock;
    RecordingSink sink;
    TaskController controller(TaskController::Config{}, bindClock(clock));
    DrainCoordinator drain(controller, DrainCoordinator::Config{}, bindClock(clock));

    // 在途任务卡住（无取消请求、永不完成、永不收敛）。
    FakeWorkerHandle worker(clock, std::nullopt);
    auto running = makeRunning(TaskCapability{}, sink);
    const TaskId idR = running->record().taskId;
    controller.attachTask(std::move(running));
    controller.bindWorker(idR, &worker);

    drain.shutdown(DrainPolicy::CancelQueuedAndWait);   // 排队为空；等待在途
    EXPECT_FALSE(drain.drained());

    // 阈值前（14.9 s）：不兜底（在途运行仍被等待）。
    clock.advance(std::chrono::milliseconds{14900});
    drain.poll();   // 关闭期复合驱动：controller.poll＋排空监视
    EXPECT_EQ(controller.tryState(idR), TaskState::Running)
        << "兜底阈值前不强制终结（等待排空先于兜底）";
    EXPECT_EQ(worker.terminateCalls(), 0);

    // 阈值到（15 s＝10 s 协议窗＋5 s 余量——实现参数）：自动 abandonAll。
    clock.advance(std::chrono::milliseconds{100});
    drain.poll();
    EXPECT_EQ(worker.terminateCalls(), 0)
        << "兜底在编排 poll 内入队（任意线程面→调度串行），本拍尚未执行强杀";

    // 下一拍：命令执行——强杀序列（进程树终止→T13）。
    clock.advance(std::chrono::milliseconds{1});
    drain.poll();
    EXPECT_EQ(controller.tryState(idR), TaskState::Failed);
    EXPECT_EQ(worker.terminateCalls(), 1) << "兜底强杀恰一次";
    controller.forEachTask([&](TaskId, TaskStateMachine& m) {
        EXPECT_EQ(m.record().termination, TerminationCause::ForceTerminated);
        EXPECT_TRUE(hasDiagCode(m, "EX-FORCE-TERMINATED"))
            << "兜底 abandonAll(ForceTerminated) 的显式标记（§7.5）";
    });
    EXPECT_TRUE(drain.drained()) << "兜底后无在途——关闭流程无永久等待（自审项）";

    // 兜底恰一次：再推进长时间不再有新动作。
    clock.advance(std::chrono::hours{1});
    drain.poll();
    EXPECT_EQ(worker.terminateCalls(), 1);
    EXPECT_EQ(controller.tryState(idR), TaskState::Failed);
}

/**
 * 手动兜底入口（abandonAllForced——L5 关闭控制器自管阈值的装配形态）：
 * 自动兜底禁用（阈值 0）时，关闭后仅在显式调用时强制终结在途任务。
 */
TEST(DrainPolicyTest, ManualAbandonAllForcedWhenAutoDisabled_acceptance3)
{
    ManualClock clock;
    RecordingSink sink;
    TaskController controller(TaskController::Config{}, bindClock(clock));
    DrainCoordinator::Config cfg;
    cfg.abandonThreshold = std::chrono::milliseconds{0};   // 禁用自动兜底
    DrainCoordinator drain(controller, cfg, bindClock(clock));

    FakeWorkerHandle worker(clock, std::nullopt);
    auto running = makeRunning(TaskCapability{}, sink);
    const TaskId idR = running->record().taskId;
    controller.attachTask(std::move(running));
    controller.bindWorker(idR, &worker);

    drain.shutdown(DrainPolicy::KeepQueuedTerminate);
    clock.advance(std::chrono::hours{1});
    drain.poll();
    EXPECT_EQ(controller.tryState(idR), TaskState::Running)
        << "自动兜底禁用：超阈值不触发（L5 自管阈值的装配形态）";

    EXPECT_EQ(drain.abandonAllForced(), std::size_t{1}) << "手动兜底处置在途任务";
    clock.advance(std::chrono::milliseconds{1});
    drain.poll();
    EXPECT_EQ(controller.tryState(idR), TaskState::Failed);
    EXPECT_TRUE(drain.drained());
}

// =====================================================================
// ICancelSignal 接缝（§3.1/§7.1——取消令牌最小接口的形状与消费链）
// =====================================================================

/**
 * 取消信号在协作点被观测（§7.1 信号传递链）：未请求时查询为假；请求后
 * 为真且不回退；"评估器批次边界"以信号＋边界时刻双条件收敛——信号缺失
 * 时不收敛（协议次序钉子）。
 */
TEST(CancelSignalTest, SignalObservedAtCooperationPoint_ICancelSignal)
{
    ManualClock clock;
    FakeWorkerHandle worker(clock, /*settleAfter=*/std::chrono::milliseconds{5000});

    // 未请求取消：信号为假、永不收敛（即使到达脚本边界）。
    EXPECT_FALSE(worker.signal().cancellationRequested());
    clock.advance(std::chrono::milliseconds{6000});
    EXPECT_FALSE(worker.cancelSettled()) << "无 CancelRequest 不许有 CancelAck（协议次序）";

    // 请求取消（通道 CancelRequest→宿主置标志）：信号置位且不回退。
    worker.requestCooperativeCancel();
    EXPECT_TRUE(worker.signal().cancellationRequested());
    clock.advance(std::chrono::milliseconds{1000});
    EXPECT_TRUE(worker.signal().cancellationRequested()) << "置位后不回退（接口契约）";
    EXPECT_FALSE(worker.cancelSettled()) << "信号置位但批次边界（5 s）未到——未收敛";

    // 边界到达：信号已置位＋边界时刻→收敛（CancelAck 语义）。
    clock.advance(std::chrono::milliseconds{4000});
    EXPECT_TRUE(worker.cancelSettled());
}

// =====================================================================
// 终态资源保留窗口（§10.2 releaseResources——D-07 保留窗语义）
// =====================================================================

TEST(TerminalRetentionTest, ReleaseResourcesGuardsRetentionWindow_S102_D07)
{
    ManualClock clock;
    RecordingSink sink;
    TaskController controller(TaskController::Config{}, bindClock(clock));

    auto machine = makeRunning(TaskCapability{}, sink);
    const TaskId id = machine->record().taskId;
    controller.attachTask(std::move(machine));
    ASSERT_TRUE(controller.requestCancel(id).accepted);
    clock.advance(std::chrono::milliseconds{1});
    controller.poll();
    ASSERT_EQ(controller.tryState(id), TaskState::Canceled);

    // 未过保留窗（D-07 默认 30 min）：释放＝调用方违约（§10.2 前置）。
    expectInvalidState([&] { controller.releaseResources(id); });
    EXPECT_EQ(controller.tryState(id), TaskState::Canceled) << "违约释放不改状态";

    // 非终态任务的释放同样违约。
    FakeWorkerHandle worker(clock, std::nullopt);
    auto active = makeRunning(TaskCapability{}, sink);
    const TaskId idActive = active->record().taskId;
    controller.attachTask(std::move(active));
    controller.bindWorker(idActive, &worker);
    expectInvalidState([&] { controller.releaseResources(idActive); });
    EXPECT_EQ(controller.tryState(idActive), TaskState::Running);

    // 过窗后释放：内存态回收（实例出表）——查询转为未知；磁盘归档不动
    // （磁盘面归 EX-T04，本单元无磁盘动作）。
    clock.advance(std::chrono::minutes{31});
    controller.releaseResources(id);
    EXPECT_EQ(controller.tryState(id), std::nullopt) << "释放后任务不可查询";

    // 未知任务的释放＝违约（fail-fast）。
    expectInvalidState([&] { controller.releaseResources(id); });
}

// =====================================================================
// 暂停/继续/暂停中取消（WP-21-T07——OPT-VER-229~232/AT-35/V12-06；
// units/execution.md §7.2/§7.3/§10.2 ＋ ARCH §4.3 A5）
// =====================================================================

/// 可编程继续兼容判定替身（IResumeGate——§7.3 判定结论注入；观测调用数）。
class FakeResumeGate : public IResumeGate {
public:
    ResumeJudgment judgment{};  ///< 预置判定结论（测试脚本面）
    int calls = 0;              ///< judgeResume 调用数（恰一次观测）

    ResumeJudgment judgeResume(TaskId) override
    {
        ++calls;
        return judgment;
    }
};

/// 暂停受理→异步确认→Paused（OPT-VER-229/230——§7.2 暂停确认边界＝检
/// 查点写出并确认）：受理 ack 时仍 Running（§10.2 注"异步——状态经事件
/// 观察"）；PauseRequest 恰发一次；确认后 T10→Paused 且 worker 解绑
/// （§7.2"worker 退出回收"的编排侧表达——解绑的可观测面＝其后取消一拍
/// 直达收敛，无协作窗等待）。
TEST(PauseResumeProtocol, PauseAsyncConfirmThenPaused_WP21T07_OPTVER229_230)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-06", "AT-35"},
                  std::vector<std::string>{"AT-35"});
    ManualClock clock;
    RecordingSink sink;
    TaskController controller(TaskController::Config{}, bindClock(clock));

    TaskCapability cap;
    cap.supportsPause = true;
    auto machine = makeRunning(cap, sink);
    const TaskId id = machine->record().taskId;
    controller.attachTask(std::move(machine));
    FakeWorkerHandle worker(clock, std::nullopt);
    controller.bindWorker(id, &worker);
    worker.setPauseSettleAfter(std::chrono::milliseconds{5});  // 检查点写出确认耗时脚本
    sink.clear();

    // 受理：accepted=true 但仍 Running（异步确认——§10.2 注"返回时可能
    // 仍 Running；状态经事件观察"）。
    const StatusAck ack = controller.requestPause(id);
    EXPECT_TRUE(ack.accepted);
    EXPECT_EQ(ack.currentState, TaskState::Running) << "受理≠已暂停（异步确认）";
    EXPECT_FALSE(ack.feedback.has_value()) << "正常受理无反馈";
    EXPECT_EQ(controller.tryState(id), TaskState::Running);

    // 第一拍：命令段置意图＋发 PauseRequest；确认未到（t=0<5ms）→保持
    // Running；重复 poll 不重发（恰一次防线）。
    controller.poll();
    EXPECT_EQ(worker.pauseRequests(), 1) << "PauseRequest 恰发一次（§7.2）";
    EXPECT_EQ(controller.tryState(id), TaskState::Running);
    controller.poll();
    EXPECT_EQ(worker.pauseRequests(), 1) << "未确认前不重发（恰一次防线）";

    // 第二拍（虚拟推进过检查点写出确认耗时）：pauseSettled→T10→Paused。
    clock.advance(std::chrono::milliseconds{5});
    controller.poll();
    EXPECT_EQ(controller.tryState(id), TaskState::Paused) << "暂停确认边界后方 T10（AT-35）";
    ASSERT_FALSE(sink.states().empty());
    EXPECT_EQ(sink.states().back(), TaskState::Paused) << "TaskStatusChanged(Paused) 事件观察";

    // worker 已解绑（§7.2 worker 退出回收的编排侧表达）：其后取消一拍直
    // 达收敛（无协作窗——Paused 无在途批次，ARCH §4.3 取消路径细则）。
    const CancelAck cancelAck = controller.requestCancel(id);
    EXPECT_TRUE(cancelAck.accepted);
    controller.poll();
    EXPECT_EQ(controller.tryState(id), TaskState::Canceled)
        << "暂停态取消直达收敛（T12→T4——解绑可观测面）";
}

/// 暂停中取消保留检查点（OPT-VER-231——T12 RetainLatest）：Paused 任务
/// 取消直达 Canceled（termination=Canceled），状态机 T12 行声明保留最近
/// 检查点、编排零磁盘动作（检查点在盘承载续跑状态——§7.4 检查点时机
/// 行；磁盘写出协议归 EX-T08，本断言面＝取消编排零废弃零删除调用）；
/// 正常取消零错误诊断（UX-03）。
TEST(PauseResumeProtocol, CancelWhilePausedRetainsCheckpoint_WP21T07_OPTVER231)
{
    IRD_TEST_INFO(std::vector<std::string>{"AT-35"},
                  std::vector<std::string>{"AT-35"});
    ManualClock clock;
    RecordingSink sink;
    TaskController controller(TaskController::Config{}, bindClock(clock));

    auto machine = makePaused(TaskCapability{}, sink);   // T10 已确认形态（worker 已回收）
    const TaskId id = machine->record().taskId;
    controller.attachTask(std::move(machine));
    sink.clear();

    // T12 行声明保留最近检查点（状态机转移表——清理列语义的声明面；
    // 编排层据此零废弃动作）。TransitionSpec 为命名空间级类型（§5.3 表
    // 行数据化——StateMachine.hpp 顶层声明）。
    const TransitionSpec* t12 = nullptr;
    for (const auto& row : TaskStateMachine::runtimeTable()) {
        if (row.row == 12) {
            t12 = &row;
        }
    }
    ASSERT_NE(t12, nullptr);
    EXPECT_EQ(t12->checkpointPolicy, CheckpointRetentionPolicy::RetainLatestCheckpoint)
        << "T12 暂停中取消必须声明保留最近检查点（ARCH §4.3 A5）";

    const CancelAck ack = controller.requestCancel(id);
    EXPECT_TRUE(ack.accepted);
    EXPECT_FALSE(ack.feedback.has_value()) << "正常取消零反馈（UX-03）";
    controller.poll();
    EXPECT_EQ(controller.tryState(id), TaskState::Canceled) << "T12→T4 一拍直达";

    // 终态与事件序核对：termination=Canceled；事件序 Canceling→Canceled
    // 两转移都在（EX-SM-1 同款观测点）。
    controller.forEachTask([&](TaskId tid, TaskStateMachine& m) {
        if (tid != id) {
            return;
        }
        ASSERT_TRUE(m.record().termination.has_value());
        EXPECT_EQ(m.record().termination.value(), TerminationCause::Canceled);
        EXPECT_TRUE(m.record().diagnostics.empty())
            << "正常取消零诊断（UX-03——非错误路径）";
    });
    ASSERT_GE(sink.states().size(), 2U);
    EXPECT_EQ(sink.states()[sink.states().size() - 2], TaskState::Canceling);
    EXPECT_EQ(sink.states().back(), TaskState::Canceled);
}

/// 能力不支持暂停时显式状态反馈（OPT-VER-232——EX-CAPABILITY-UNSUPPORTED
/// 不静默）：supportsPause=false 的 Running 任务收到暂停请求→accepted=
/// false＋反馈码 EX-CAPABILITY-UNSUPPORTED，状态保持 Running、零事件
/// （§7.2 前置/ARCH §4.3"不静默忽略"）；受理层同步拒绝先于状态机 T10
/// 守卫（两层同码一致）。
TEST(PauseResumeProtocol, CapabilityUnsupportedExplicitFeedback_WP21T07_OPTVER232)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-06"},
                  std::vector<std::string>{"AT-35"});
    ManualClock clock;
    RecordingSink sink;
    TaskController controller(TaskController::Config{}, bindClock(clock));

    // 缺省能力位＝supportsPause false（R1 任务缺省不支持——§5.5）。
    auto machine = makeRunning(TaskCapability{}, sink);
    const TaskId id = machine->record().taskId;
    controller.attachTask(std::move(machine));
    FakeWorkerHandle worker(clock, std::nullopt);
    controller.bindWorker(id, &worker);
    sink.clear();

    const StatusAck ack = controller.requestPause(id);
    EXPECT_FALSE(ack.accepted) << "能力拒绝必须 accepted=false";
    EXPECT_EQ(ack.currentState, TaskState::Running) << "状态不变（§7.2 前置拒绝）";
    ASSERT_TRUE(ack.feedback.has_value()) << "必须显式反馈（不静默）";
    EXPECT_EQ(ack.feedback->code, "EX-CAPABILITY-UNSUPPORTED")
        << "反馈码＝EX-CAPABILITY-UNSUPPORTED（EX-SM-7 同码——§7.2）";

    // 拒绝不产生转移与事件：poll 后仍 Running、零新事件。
    controller.poll();
    EXPECT_EQ(controller.tryState(id), TaskState::Running);
    EXPECT_TRUE(sink.states().empty()) << "能力拒绝零转移零事件";
    EXPECT_EQ(worker.pauseRequests(), 0) << "未向 worker 发暂停请求";
}

/// 继续：兼容判定通过→T11→Running 且 AttemptId+1、五元组绑定不变
/// （OPT-VER-229/230 继续半边＋"不改变输入快照"执行侧承载——I-OPT-4：
/// 恢复绑定原 snapshotId/sliceId/config，本编排零输入面写路径；五元组
/// 的 project/branch/revision/run 不变＋attempt+1 即其编排侧观测面）。
TEST(PauseResumeProtocol, ResumePassIncrementsAttemptKeepsIdentity_WP21T07_OPTVER230)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-06", "AT-35"},
                  std::vector<std::string>{"AT-35"});
    ManualClock clock;
    RecordingSink sink;
    TaskController controller(TaskController::Config{}, bindClock(clock));
    FakeResumeGate gate;
    gate.judgment.compatible = true;
    controller.setResumeGate(&gate);

    auto machine = makePaused(TaskCapability{}, sink);
    const TaskId id = machine->record().taskId;
    const auto identityBefore = machine->currentIdentity();
    const auto attemptBefore = machine->record().attempt;
    controller.attachTask(std::move(machine));
    sink.clear();

    const StatusAck ack = controller.requestResume(id);
    EXPECT_TRUE(ack.accepted);
    EXPECT_EQ(ack.currentState, TaskState::Paused) << "受理时仍 Paused（判定在 poll 域）";
    controller.poll();
    EXPECT_EQ(gate.calls, 1) << "兼容判定恰一次（T11 前置）";
    EXPECT_EQ(controller.tryState(id), TaskState::Running) << "T11→Running";
    ASSERT_FALSE(sink.states().empty());
    EXPECT_EQ(sink.states().back(), TaskState::Running);

    controller.forEachTask([&](TaskId tid, TaskStateMachine& m) {
        if (tid != id) {
            return;
        }
        // 新 AttemptId（同 RunId）＋输入绑定不变（不改变输入快照——I-OPT-4
        // 的编排侧观测面）。
        EXPECT_EQ(m.record().attempt.value, attemptBefore.value + 1)
            << "AttemptId+1（恢复语义 §7.3）";
        EXPECT_TRUE(m.currentIdentity().project == identityBefore.project);
        EXPECT_TRUE(m.currentIdentity().branch == identityBefore.branch);
        EXPECT_TRUE(m.currentIdentity().revision == identityBefore.revision)
            << "project/branch/revision 绑定不变（恢复不改输入）";
        EXPECT_TRUE(m.currentIdentity().run == identityBefore.run);
    });
}

/// 继续：兼容判定不通过→保持 Paused＋EX-CHECKPOINT-INCOMPATIBLE 诊断
/// （§7.3"不兼容→拒绝继续＋诊断"——原因 token 原样透传；用户可取消或
/// 等待配置回退）；判定面未装配→受理层 fail-closed 拒绝（EX-T05 校验面
/// 纪律同源）；非 Paused 态的继续请求→EX-INVALID-STATE 结构化反馈。
TEST(PauseResumeProtocol, ResumeRejectStaysPausedWithDiagnostic_WP21T07)
{
    IRD_TEST_INFO(std::vector<std::string>{"AT-35"},
                  std::vector<std::string>{"AT-35"});
    ManualClock clock;
    RecordingSink sink;
    TaskController controller(TaskController::Config{}, bindClock(clock));

    // ---- 分层面 1：判定面未装配——受理层 fail-closed 拒绝（不静默）。
    auto machine1 = makePaused(TaskCapability{}, sink);
    const TaskId id1 = machine1->record().taskId;
    controller.attachTask(std::move(machine1));
    {
        const StatusAck ack = controller.requestResume(id1);
        EXPECT_FALSE(ack.accepted);
        EXPECT_EQ(ack.currentState, TaskState::Paused);
        ASSERT_TRUE(ack.feedback.has_value());
        EXPECT_EQ(ack.feedback->code, "EX-INVALID-STATE")
            << "装配缺失＝调用前置不满足（fail-closed——不静默放行续跑）";
        EXPECT_EQ(controller.tryState(id1), TaskState::Paused);
    }

    // ---- 分层面 2：判定面在但不兼容——保持 Paused＋EX-CHECKPOINT-
    // INCOMPATIBLE 诊断（reasons 透传 cause）。
    FakeResumeGate gate;
    gate.judgment.compatible = false;
    gate.judgment.reasons = {"contract-version-mismatch"};
    controller.setResumeGate(&gate);
    {
        const StatusAck ack = controller.requestResume(id1);
        EXPECT_TRUE(ack.accepted) << "受理（判定在 poll 域执行）";
        controller.poll();
        EXPECT_EQ(controller.tryState(id1), TaskState::Paused)
            << "不兼容→保持 Paused（§7.3）";
        EXPECT_EQ(gate.calls, 1);
        bool found = false;
        controller.forEachTask([&](TaskId tid, TaskStateMachine& m) {
            if (tid != id1) {
                return;
            }
            for (const auto& d : m.record().diagnostics) {
                if (d.code == "EX-CHECKPOINT-INCOMPATIBLE") {
                    found = true;
                    EXPECT_NE(std::string(d.cause).find("contract-version-mismatch"),
                              std::string::npos)
                        << "原因 token 原样透传（词表归 evidence）";
                }
            }
        });
        EXPECT_TRUE(found) << "必须落 EX-CHECKPOINT-INCOMPATIBLE 诊断";
    }

    // ---- 分层面 3：非 Paused 态的继续请求——EX-INVALID-STATE 结构化反
    // 馈（把 id1 转 Canceling 形态后请求继续）。
    ASSERT_EQ(controller.requestCancel(id1).accepted, true);
    controller.poll();   // Paused 无在途批次——一拍直达 Canceled
    ASSERT_EQ(controller.tryState(id1), TaskState::Canceled);
    {
        const StatusAck ack = controller.requestResume(id1);
        EXPECT_FALSE(ack.accepted);
        ASSERT_TRUE(ack.feedback.has_value());
        EXPECT_EQ(ack.feedback->code, "EX-INVALID-STATE") << "终态任务无继续语义";
    }
}

/// 取消优先于暂停（§7.4 取消终态语义强于挂起）：暂停意图挂起（未确认）
/// 期间取消请求→意图消解、正常取消协议完成；其后 worker 暂停确认到位
/// 也不得触发 T10（源态守卫+意图清除双防线——无异常、终态不变）。
TEST(PauseResumeProtocol, CancelWinsOverPendingPause_WP21T07)
{
    IRD_TEST_INFO(std::vector<std::string>{"AT-35"},
                  std::vector<std::string>{"AT-35"});
    ManualClock clock;
    RecordingSink sink;
    TaskController controller(TaskController::Config{}, bindClock(clock));

    TaskCapability cap;
    cap.supportsPause = true;
    auto machine = makeRunning(cap, sink);
    const TaskId id = machine->record().taskId;
    controller.attachTask(std::move(machine));
    // 收敛脚本 0ms：取消胜出路径走有 worker 的协作窗（暂停未确认、worker
    // 仍绑定）——窗口内批次即刻收敛，一拍完成 Canceling→Canceled。
    FakeWorkerHandle worker(clock, std::chrono::milliseconds{0});
    controller.bindWorker(id, &worker);
    worker.setPauseSettleAfter(std::chrono::milliseconds{5});
    sink.clear();

    // 暂停受理＋一拍（意图置位、PauseRequest 已发、未确认）。
    ASSERT_TRUE(controller.requestPause(id).accepted);
    controller.poll();
    EXPECT_EQ(controller.tryState(id), TaskState::Running);

    // 取消（撤销暂停意图——取消优先）＋一拍收敛。
    ASSERT_TRUE(controller.requestCancel(id).accepted);
    controller.poll();
    EXPECT_EQ(controller.tryState(id), TaskState::Canceled)
        << "取消优先于挂起中的暂停（§7.4）";

    // 暂停确认后到也不得触发 T10：推进过确认点再 poll——终态保持、零异常。
    clock.advance(std::chrono::milliseconds{5});
    controller.poll();
    controller.poll();
    EXPECT_EQ(controller.tryState(id), TaskState::Canceled) << "终态无出边（意图已消解）";
    EXPECT_EQ(sink.states().back(), TaskState::Canceled);
}

}  // namespace
