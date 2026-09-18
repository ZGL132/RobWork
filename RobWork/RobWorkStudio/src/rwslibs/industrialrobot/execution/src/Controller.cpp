/**
 * @file   Controller.cpp
 * @brief  取消与终止协议编排的实现（EX-T03）——§7.1 逐步协议的可执行体：
 *         命令优先段、协作窗（2 s 生效/10 s 收敛）、超时强杀序列、运行
 *         超时监视（EX-WKR-5）、无 worker 任务的取消直达。
 *
 * 设计依据：
 *   - units/execution.md §7.1（协作式取消四步协议）、§7.4（四类行为对照
 *     ——无 worker 任务直达、强杀显式区分）、§10.2（Ack 前置语义）、
 *     §10.3（worker 操作语义）、§5.3（T3/T4/T6/T12/T13 转移行）、§11
 *     （EX-SM-1/3/4、EX-WKR-4/5 的被测行为）
 *   - ARCHITECTURE.md §4.4（四条协议——2 s/10 s 为上游需求值，实现侧
 *     以 constexpr 冻结、无运行期修改点；P-EX-2 处置：评审变更增量同步）
 *   - 需求 NFR-PERF-02（协议时序）、UX-03（正常取消零错误诊断——本实现
 *     的取消路径对 record().diagnostics 零写入）
 *
 * 实现说明（读代码前先看）：
 *   - 状态推进一律经 TaskStateMachine::request（EX-T02 转移表）——本编排
 *     不直接写状态字段（单一事实源；矩阵约束由状态机强制，编排层只负责
 *     "在正确时机发正确触发"）；
 *   - 一切时序判定用注入时钟的**时长比较**（now - 起点 ≥ 窗长），不比较
 *     绝对时刻——ManualClock 从 steady 纪元起步即可工作，与真实墙钟无关
 *     （testkit §6.6 时钟适配）；
 *   - 强杀防线：terminateForce 在一个任务生命周期内恰发一次（forceKillIssued
 *     标记）——收敛超时与直接强杀竞争同一任务时不会双重终止（进程树
 *     终止本身幂等，但恰一次语义让测试断言可精确）。
 */

#include <sdurws/ird/execution/Controller.hpp>

#include <sdurws/ird/core/Evaluation.hpp>   // core::toToken(TaskState)（终态反馈文案）
#include <sdurws/ird/core/Errors.hpp>
#include <sdurws/ird/execution/Errors.hpp>
#include <sdurws/ird/execution/StateMachine.hpp>

#include <algorithm>
#include <string>
#include <utility>

namespace sdurws::ird::execution {
namespace {

using core::TaskState;

/// 结构化反馈诊断（CancelAck/StatusAck.feedback 承载——core 工厂强制
/// context/cause/recommendedAction 非空；feedback 不写入任务诊断累积，
/// UX-03 的任务级诊断空断言不受本函数影响）。
core::DiagnosticRecord makeAckFeedback(const std::string& code, const std::string& context,
                                       const std::string& cause, const std::string& action)
{
    return core::DiagnosticRecord::make(code, std::nullopt, std::nullopt, std::nullopt,
                                        context, cause, action);
}

/// 判定类诊断写入（任务级诊断累积——经 TaskStateMachine::appendDiagnostic）。
core::DiagnosticRecord makeTaskDiagnostic(const std::string& code, const std::string& context,
                                          const std::string& cause, const std::string& action)
{
    return core::DiagnosticRecord::make(code, std::nullopt, std::nullopt, std::nullopt,
                                        context, cause, action);
}

}  // namespace

// ---------------------------------------------------------------------
// 构造/析构与登记（仅调度线程的表操作）
// ---------------------------------------------------------------------

TaskController::TaskController(Config config, ClockFn clock)
    : m_config(config)
    , m_clock(std::move(clock))
{
}

TaskController::~TaskController() = default;

std::chrono::steady_clock::time_point TaskController::steadyClock() noexcept
{
    // steady 时钟：单调、不受系统时间调整影响——协议时长度量的正确基准
    // （system_clock 会被 NTP 校时打乱时长，绝不能用于超时判定）。
    return std::chrono::steady_clock::now();
}

std::chrono::steady_clock::time_point TaskController::now() const noexcept
{
    // 注入时钟为空时退回生产默认（构造参数缺省的装配场景）。
    return m_clock ? m_clock() : steadyClock();
}

void TaskController::attachTask(std::unique_ptr<TaskStateMachine> machine)
{
    if (!machine) {
        // 空实例登记＝调用方拼装错误，fail-fast（§10.8 调用方违约抛）。
        throw ExecutionError(ExecutionErrorCode::InvalidState,
                             "execution/controller: attachTask 收到空状态机实例");
    }
    const TaskId id = machine->record().taskId;
    if (m_tasks.find(id) != m_tasks.end()) {
        // TaskId 全局唯一（§4.2 主键行）——重复登记属调度器登记面违约。
        throw ExecutionError(ExecutionErrorCode::InvalidState,
                             "execution/controller: 任务重复登记 " + id.toCanonical());
    }
    ManagedTask entry;
    entry.machine = std::move(machine);
    // 登记即按当前状态补齐时点（attach 可能发生在任务已 Running/终态后
    // ——恢复重建/测试装配场景；不得从零起点重新计时）。
    refreshTracking(entry, now());
    m_tasks.emplace(id, std::move(entry));

    // 状态速览同步登记（任意线程的受理判定自此可查）。
    {
        std::lock_guard<std::mutex> lock(m_commandMutex);
        m_stateView.emplace(id, m_tasks.find(id)->second.machine->state());
    }
}

void TaskController::bindWorker(TaskId task, IWorkerHandle* worker)
{
    const auto it = m_tasks.find(task);
    if (it == m_tasks.end()) {
        throw ExecutionError(ExecutionErrorCode::InvalidState,
                             "execution/controller: bindWorker 未知任务 " + task.toCanonical());
    }
    it->second.worker = worker;   // nullptr＝解绑（暂停后 worker 回收——§7.2）
    // 换绑/解绑重置协作窗防线：新 worker（或恢复后）的协作窗从零开始，
    // 旧的"已发信号/已强杀"标记不得泄漏到新绑定。
    it->second.cooperativeCancelSent = false;
    it->second.forceKillIssued = false;
    it->second.cancelEnteredAt.reset();
}

void TaskController::setDispatchGate(IDispatchGate* gate) noexcept
{
    m_dispatchGate = gate;   // 非所有权；nullptr＝无消费者（合法装配）
}

// ---------------------------------------------------------------------
// 控制命令（任意线程——命令通道入队＋速览受理判定）
// ---------------------------------------------------------------------

CancelAck TaskController::requestCancel(TaskId task)
{
    // ---- 受理判定（锁内速览）----
    core::TaskState speedState;
    bool known = false;
    {
        std::lock_guard<std::mutex> lock(m_commandMutex);
        const auto view = m_stateView.find(task);
        known = view != m_stateView.end();
        if (known) {
            speedState = view->second;
        }
    }
    if (!known) {
        // 未知任务＝调用方契约违约（任务存在性是调用方前置知识——§10.2
        // 前置"任务存在"），fail-fast 不静默。
        throw ExecutionError(ExecutionErrorCode::InvalidState,
                             "execution/controller: requestCancel 未知任务 " + task.toCanonical());
    }
    if (isTerminalTaskState(speedState)) {
        // 终态任务无可取消之物（§10.2 前置注：accepted=false＋InvalidState
        // 反馈）——可预期条件走结构化 Ack，不抛；feedback 仅应答携带，
        // 不入任务诊断（终态记录不可变纪律）。
        return CancelAck{false,
                         makeAckFeedback("EX-INVALID-STATE",
                                         "取消请求于终态任务",
                                         "任务已处于终态 " + std::string(core::toToken(speedState))
                                             + "，无在途运行可取消（§10.2 前置）",
                                         "无需操作；重跑请提交新任务（新身份链——§4.3）")};
    }
    // Canceling 中的重复取消也照常入队：poll 命令段经状态机幂等 ack
    // （T3 守卫注"重复取消幂等"）消化，不产生第二次转移/事件。

    // ---- 入队（FIFO——同级命令到达序生效，§10.8 并发规则行）----
    {
        std::lock_guard<std::mutex> lock(m_commandMutex);
        m_commands.push_back(ControlCommand{task, now(), false});
    }
    return CancelAck{true, std::nullopt};   // 正常受理：feedback 空（UX-03）
}

StatusAck TaskController::requestForceTerminate(TaskId task)
{
    core::TaskState speedState;
    bool known = false;
    {
        std::lock_guard<std::mutex> lock(m_commandMutex);
        const auto view = m_stateView.find(task);
        known = view != m_stateView.end();
        if (known) {
            speedState = view->second;
        }
    }
    if (!known) {
        throw ExecutionError(ExecutionErrorCode::InvalidState,
                             "execution/controller: requestForceTerminate 未知任务 "
                                 + task.toCanonical());
    }
    if (isTerminalTaskState(speedState)) {
        // §10.2 后置注"终态任务→幂等 no-op"：不动作、报当前态、无反馈
        // （强杀对已终结任务是空操作，不是错误——区别于取消的 accepted=
        // false：强杀幂等成功语义使 UI 重复点击无害）。
        return StatusAck{true, speedState, std::nullopt};
    }
    {
        std::lock_guard<std::mutex> lock(m_commandMutex);
        m_commands.push_back(ControlCommand{task, now(), true});
    }
    return StatusAck{true, speedState, std::nullopt};
}

// ---------------------------------------------------------------------
// 查询（仅调度线程）
// ---------------------------------------------------------------------

std::optional<ProgressReport> TaskController::progress(TaskId task) const noexcept
{
    const auto it = m_tasks.find(task);
    if (it == m_tasks.end()) {
        return std::nullopt;
    }
    return it->second.machine->record().progress;
}

std::optional<core::TaskState> TaskController::tryState(TaskId task) const noexcept
{
    const auto it = m_tasks.find(task);
    if (it == m_tasks.end()) {
        return std::nullopt;
    }
    return it->second.machine->state();
}

std::size_t TaskController::taskCount() const noexcept
{
    return m_tasks.size();
}

void TaskController::forEachTask(
    const std::function<void(TaskId, TaskStateMachine&)>& fn)
{
    // 遍历面：仅调度线程调用（排空编排/测试）；遍历中修改状态允许、
    // 增删表项未定义（attachTask/releaseResources 不得在 fn 内调用）。
    for (auto& [id, entry] : m_tasks) {
        fn(id, *entry.machine);
    }
}

// ---------------------------------------------------------------------
// 协议驱动（poll——调度线程；处理序见类注释 a~d 段）
// ---------------------------------------------------------------------

void TaskController::poll()
{
    const std::chrono::steady_clock::time_point nowTp = now();

    // ---- a. 命令段：清空命令通道（取消命令插队于一切推进之前——§7.1
    // 步 1"取消命令插队于派发之前"的编排表达：本段是 poll 的第一动作）----
    std::vector<ControlCommand> batch;
    {
        std::lock_guard<std::mutex> lock(m_commandMutex);
        batch.swap(m_commands);
    }
    for (const ControlCommand& cmd : batch) {
        const auto it = m_tasks.find(cmd.task);
        if (it == m_tasks.end()) {
            continue;   // 命令入队后任务被 releaseResources 回收——终态资源的
                        // 释放使未决命令失效（无害丢弃；保留窗语义保证调用方
                        // 不会在活跃任务上做这件事）
        }
        if (isTerminalTaskState(it->second.machine->state())) {
            continue;   // 入队速览与执行时点之间任务已终态：取消/强杀对终态
                        // 任务均为 no-op，丢弃命令（requestCancel 注的竞态窗口）
        }
        if (cmd.force) {
            executeForceTerminate(it->second, cmd, nowTp);
        } else {
            executeCancel(it->second, cmd, nowTp);
        }
    }

    // ---- b/c/d 段：对每个受管任务推进窗口与超时（终态任务只需时点维护）----
    for (auto& [id, entry] : m_tasks) {
        TaskStateMachine& machine = *entry.machine;
        const TaskState state = machine.state();

        if (isTerminalTaskState(state)) {
            refreshTracking(entry, nowTp);   // 终态：仅补打终态时点（保留窗起点）
            continue;
        }

        // ---- b. 协作窗段（仅 Canceling 任务）----
        if (state == TaskState::Canceling) {
            const bool hasWorker = entry.worker != nullptr;
            const bool settled = hasWorker && entry.cooperativeCancelSent
                                && entry.worker->cancelSettled();
            if (settled) {
                // 收敛优先于超时（窗边界上"恰好收敛"按收敛处理——§7.1
                // 步 2"10 s 内自然结束"的含边界语义）：T4→Canceled。
                machine.request(TransitionTrigger::CancelSettled);
                refreshTracking(entry, nowTp);
                continue;
            }
            const bool windowExpired = entry.cancelEnteredAt.has_value()
                && nowTp - *entry.cancelEnteredAt >= kCancelCooperativeWindow;
            if (windowExpired) {
                // 收敛窗（10 s）耗尽→强杀兜底（§7.1 步 3）——取消协议
                // 无永久等待路径的兜底臂（acceptance 2 自审项）。
                forceKillSequence(entry, nowTp, /*runTimeout=*/false);
                refreshTracking(entry, nowTp);
                continue;
            }
            // 窗内未收敛：保持 Canceling，等待下一拍（不 busy-wait、不 sleep
            // ——调度循环的下一周期再查，虚拟时钟推进即测试推进）。
            refreshTracking(entry, nowTp);
            continue;
        }

        // ---- c. 运行超时段（仅 Running 且声明时限的任务——EX-WKR-5）----
        if (state == TaskState::Running) {
            const auto& declared = machine.record().capability.evaluationTimeout;
            const bool timeoutEnabled = declared.has_value() && declared->count() > 0;
            if (timeoutEnabled && entry.runStartedAt.has_value()
                && nowTp - *entry.runStartedAt >= *declared) {
                // 超过声明运行时限→走与卡死相同的强杀路径（§7.1"同卡死
                // 路径"；EX-WKR-5"诊断区分 timeout"由 forceKillSequence
                // 的 runTimeout 分支写入 EX-WORKER-HUNG＋运行超时标记）。
                // 进入强杀相：先经 RequestCancel 进入 Canceling（矩阵内
                // 合法转移，row=0 行），强杀序列必经 T13 写入点。
                machine.request(TransitionTrigger::RequestCancel);
                forceKillSequence(entry, nowTp, /*runTimeout=*/true);
                refreshTracking(entry, nowTp);
                continue;
            }
        }

        // ---- d. 时点维护 ----
        refreshTracking(entry, nowTp);
    }
}

// ---------------------------------------------------------------------
// 编排内部：取消/强杀/直达/进入取消相（全部仅调度线程）
// ---------------------------------------------------------------------

void TaskController::executeCancel(ManagedTask& entry, const ControlCommand& cmd,
                                   std::chrono::steady_clock::time_point nowTp)
{
    TaskStateMachine& machine = *entry.machine;
    const TaskState state = machine.state();

    // 已在 Canceling：状态机幂等 ack（T3 守卫注）——不转移不发事件；
    // 这里无事可做（协作窗已在推进中）。
    if (state == TaskState::Canceling) {
        return;
    }

    // 矩阵内的取消入口：Queued(T3)/Preparing(T6)/Paused(T12)/Running(row0)
    // ——状态机按 (触发, 源态) 查表；命令段已排除终态。
    machine.request(TransitionTrigger::RequestCancel);
    enterCanceling(entry, nowTp);   // 协作窗起点＝生效时刻（进入 Canceling），非请求时刻——2 s 生效延迟由协议承担

    // 无 worker 任务（Queued/Preparing/Paused——无在途批次）：同拍直达
    // 收敛（§5.3 各行"无在途批次→速达 Canceled"；EX-SM-1 观测点要求
    // 状态序 Queued→Canceling→Canceled 两条事件都在，故不是跳过 Canceling
    // 而是两步连发）。
    if (entry.worker == nullptr) {
        settleWithoutWorker(entry);
    }
}

void TaskController::enterCanceling(ManagedTask& entry,
                                    std::chrono::steady_clock::time_point nowTp)
{
    // 停止派发新批次（§7.1 步 1"进入 Canceling 并停止派发新批次"）——
    // 显式闸门回调（EX-T05 调度器移出派发就绪集；EX-SM-3 观测点）。
    if (m_dispatchGate != nullptr) {
        m_dispatchGate->stopDispatch(entry.machine->record().taskId);
    }

    // 向 worker 恰发一次协作取消信号（§7.1 步 2 通道 CancelRequest）。
    // 有 worker 才有协作窗（10 s 度量起点＝本时刻）；无 worker 任务不
    // 开窗（settleWithoutWorker 同拍收敛）。
    if (entry.worker != nullptr && !entry.cooperativeCancelSent) {
        entry.worker->requestCooperativeCancel();
        entry.cooperativeCancelSent = true;
    }
    entry.cancelEnteredAt = nowTp;
}

void TaskController::settleWithoutWorker(ManagedTask& entry)
{
    // 无在途批次的收敛：T4 CancelSettled→Canceled（RetainLatest 检查点
    // 保留语义随转移行生效——EX-SM-4 的 Paused 路径即此形态）。
    entry.machine->request(TransitionTrigger::CancelSettled);
}

void TaskController::executeForceTerminate(ManagedTask& entry, const ControlCommand& cmd,
                                           std::chrono::steady_clock::time_point nowTp)
{
    TaskStateMachine& machine = *entry.machine;
    const TaskState state = machine.state();

    if (state == TaskState::Canceling) {
        // 已在协作窗中：直接强杀（用户强杀优先于窗等待——§10.2"进程树
        // 终止"后置不经等待）。
        forceKillSequence(entry, nowTp, /*runTimeout=*/false);
        return;
    }

    if (entry.worker != nullptr) {
        // 有活动 worker（Running——Queued/Preparing/Paused 无 worker）：
        // 先进入 Canceling（矩阵内合法转移，row=0 行；强杀必经 T13 的
        // ForceTerminated 写入点——EX-WKR-4 事件序 Running→Canceling→
        // Failed），再立即执行强杀序列（不等待批次收敛——"不经协作点"
        // 是强杀与协作取消的本质区别，§7.4 信号传递行）。
        machine.request(TransitionTrigger::RequestCancel);
        enterCanceling(entry, nowTp);
        forceKillSequence(entry, nowTp, /*runTimeout=*/false);
        return;
    }

    // 无 worker 任务（Queued/Preparing/Paused）：无进程树可终止——强杀
    // 语义在矩阵内的等效表达＝出队/直达取消（§7.4"强杀归任务操作"；
    // 排队/组装/暂停任务无在途结果、无活动进程，取消即完全终结）。终态
    // 为 Canceled（termination=Canceled）而非 ForceTerminated：状态机仅在
    // T13 写 ForceTerminated，而排队任务不满足 T13 的 Canceling 前置下
    // 的"强杀进程"语义——强杀标记的观测面（诊断）也不应出现在从未运行
    // 的任务上。
    machine.request(TransitionTrigger::RequestCancel);
    enterCanceling(entry, nowTp);
    settleWithoutWorker(entry);
}

void TaskController::forceKillSequence(ManagedTask& entry,
                                       std::chrono::steady_clock::time_point nowTp,
                                       bool runTimeout)
{
    TaskStateMachine& machine = *entry.machine;

    // ---- 第 1 步：进程树终止（恰一次）----
    // KILL_ON_JOB_CLOSE 契约见 IWorkerHandle::terminateForce 注（实现归
    // EX-T06 JobScope；本编排保证"一次任务至多一次终止调用"——收敛超时
    // 与运行超时与直接强杀三方竞争同一任务时不会双重终止）。
    if (!entry.forceKillIssued) {
        if (entry.worker != nullptr) {
            entry.worker->terminateForce(TerminationCause::ForceTerminated);
        }
        entry.forceKillIssued = true;
    }

    // ---- 第 2 步：运行超时的判定诊断（先于 T13 终结标记——诊断序即
    // 因果序：先"判定原因 EX-WORKER-HUNG（运行超时）"，后"终结标记
    // EX-FORCE-TERMINATED"）。EX-WKR-5"诊断区分 timeout"的落点：cause
    // 携带 evaluation-timeout 标记，与心跳失联（EX-T06 写入的卡死判定）
    // 区分。码值 EX-WORKER-HUNG 为 diagnostics StableCodeRegistry 已收编
    // 稳定码（§3.4 清单）——execution 不私定码值（契约 note）。
    if (runTimeout) {
        machine.appendDiagnostic(makeTaskDiagnostic(
            "EX-WORKER-HUNG",
            "运行超时判定：任务 " + machine.record().taskId.toCanonical(),
            "评估运行时长超过能力声明时限（evaluationTimeout——§11 EX-WKR-5 能力声明字段）；"
            "走卡死强杀路径，诊断以运行超时标记与心跳失联区分（§7.1）",
            "任务将被强制终止并保留最近检查点；重跑可从检查点续（新任务、新身份链）"));
    }

    // ---- 第 3 步：T13 超时强杀转移（Canceling→Failed）----
    // 状态机在转移内写入 termination=ForceTerminated＋EX-FORCE-TERMINATED
    // 显式诊断（"不伪装为普通失败"——§5.3 T13；EX-WKR-4 观测点"诊断
    // 标记"）。EX-FORCE-TERMINATED 用户可见"已强制终止，检查点保留"
    // （§7.4 诊断行）；普通失败（EX-WKR-2 崩溃）的呈现面是 EX-WORKER-
    // CRASHED——两码永不并存，即"显式区别"的机器判读面。
    machine.request(TransitionTrigger::CancelTimeoutForceKill);

    // nowTp 仅供签名对称与未来扩展（当前强杀序列的时点由 refreshTracking
    // 统一维护）；显式引用防未用参数告警。
    (void)nowTp;
}

void TaskController::refreshTracking(ManagedTask& entry,
                                     std::chrono::steady_clock::time_point nowTp)
{
    TaskStateMachine& machine = *entry.machine;
    const TaskState state = machine.state();

    // Running 起点（运行超时度量起点——EX-WKR-5）：进入 Running 时打点。
    if (state == TaskState::Running && !entry.runStartedAt.has_value()) {
        entry.runStartedAt = nowTp;
    }
    // 离开 Running（暂停/继续后的新尝试）重置起点：evaluationTimeout 是
    // "单次尝试"的时限（字段注）——T11 新 Attempt 从零计时。
    if (state != TaskState::Running) {
        entry.runStartedAt.reset();
    }

    // 终态时点（保留窗起点——releaseResources 的过窗判定）：只打一次。
    if (isTerminalTaskState(state) && !entry.terminalAt.has_value()) {
        entry.terminalAt = nowTp;
    }

    // 状态速览刷新（锁内——任意线程受理判定读取面）。
    {
        std::lock_guard<std::mutex> lock(m_commandMutex);
        m_stateView[machine.record().taskId] = state;
    }
}

// ---------------------------------------------------------------------
// 终态资源回收（§10.2 releaseResources——仅调度线程）
// ---------------------------------------------------------------------

void TaskController::releaseResources(TaskId task)
{
    const auto it = m_tasks.find(task);
    if (it == m_tasks.end()) {
        throw ExecutionError(ExecutionErrorCode::InvalidState,
                             "execution/controller: releaseResources 未知任务 "
                                 + task.toCanonical());
    }
    const ManagedTask& entry = it->second;
    const TaskState state = entry.machine->state();
    if (!isTerminalTaskState(state)) {
        // 非终态任务的活动内存态不可释放（§10.2 前置"任务处于终态"）。
        throw ExecutionError(ExecutionErrorCode::InvalidState,
                             "execution/controller: 任务非终态，不可释放 "
                                 + task.toCanonical());
    }
    if (!entry.terminalAt.has_value()
        || now() - *entry.terminalAt < m_config.terminalRetention) {
        // 未过保留窗口（D-07 默认 30 min）：终态任务保留可查询是查询体验
        // 的实现承诺（§4.2 生命周期行）——提前释放属调用方违约。
        throw ExecutionError(ExecutionErrorCode::InvalidState,
                             "execution/controller: 终态任务未过保留窗口，不可释放 "
                                 + task.toCanonical());
    }

    // 状态速览同步移除（此后对该 taskId 的控制请求按"未知任务"违约）。
    {
        std::lock_guard<std::mutex> lock(m_commandMutex);
        m_stateView.erase(task);
    }
    m_tasks.erase(it);   // 状态机实例随之析构——内存态回收；磁盘归档不动
}

}  // namespace sdurws::ird::execution
