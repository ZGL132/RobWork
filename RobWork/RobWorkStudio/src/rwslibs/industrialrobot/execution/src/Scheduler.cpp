/**
 * @file   Scheduler.cpp
 * @brief  DrainPolicy 排空编排的实现（EX-T03）——§7.5 关闭二选的执行侧：
 *         排队处置两策略、在途等待、协作取消批量入口、超阈值 abandonAll
 *         兜底（无永久等待）。
 *
 * 设计依据：见 Scheduler.hpp 文件头（§7.4/§7.5/§10.1、ARCH §4.4、
 * acceptance 3——此处不重复）。
 *
 * 实现说明：本编排是 TaskController 之上的**策略层**——所有任务级动作
 * （取消/强杀/协议推进）都经 TaskController 的公共面（requestCancel/
 * requestForceTerminate/poll/forEachTask/tryState）执行，本文件不触碰
 * 状态机与 worker 句柄（职责分层：协议时序归 Controller，关闭策略归
 * Scheduler——EX-T05 调度器届时组合两者并补齐 submit/shutdown 的
 * ContextClosed 拒绝面）。
 */

#include <sdurws/ird/execution/Scheduler.hpp>

#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/execution/StateMachine.hpp>

#include <utility>

namespace sdurws::ird::execution {
namespace {

using core::TaskState;

/// 在途状态判定（排空等待面——drained/兜底处置的公共判据）：
/// Preparing（派发组装中）/Running（在途运行）/Canceling（取消收敛窗中）。
/// Queued 不属在途（§7.5 两策略对其分道处置）；四终态更不属。
bool isInFlight(TaskState state) noexcept
{
    return state == TaskState::Preparing || state == TaskState::Running
        || state == TaskState::Canceling;
}

}  // namespace

// ---------------------------------------------------------------------
// 构造与时钟
// ---------------------------------------------------------------------

DrainCoordinator::DrainCoordinator(TaskController& controller, Config config, ClockFn clock)
    : m_controller(controller)
    , m_config(config)
    , m_clock(std::move(clock))
{
}

std::chrono::steady_clock::time_point DrainCoordinator::now() const noexcept
{
    // 缺省回退与 TaskController 一致（steady 单调时钟）——两编排用不同
    // 时钟源会造成兜底阈值与协作窗度量漂移（头注纪律），装配侧注入时
    // 应传同一 ManualClock/时钟函数。
    return m_clock ? m_clock() : TaskController::steadyClock();
}

std::chrono::steady_clock::time_point DrainCoordinator::steadyClock() noexcept
{
    return TaskController::steadyClock();
}

// ---------------------------------------------------------------------
// 关闭控制（§7.5 两路径）
// ---------------------------------------------------------------------

void DrainCoordinator::shutdown(DrainPolicy policy)
{
    if (m_closed) {
        return;   // 幂等（§10.1"shutdown 幂等"注）：重复关闭不重复取消
                  // 排队（第二次可能把 KeepQueued 的保留任务误取消）、
                  // 不重置兜底阈值计时（"超阈值"自首次关闭起量）
    }
    m_closed = true;
    m_policy = policy;
    m_shutdownAt = now();

    if (policy == DrainPolicy::CancelQueuedAndWait) {
        // "等待"分支的排队处置：排队任务逐个取消（无 worker→同拍直达
        // Canceled——§7.5"取消排队任务"；UX-03：关闭触发的正常取消零
        // 错误诊断，诊断空断言由取消协议本身保证——CancelAck.feedback
        // 为空且不写任务诊断）。
        m_controller.forEachTask([this](TaskId id, TaskStateMachine& machine) {
            if (machine.state() == TaskState::Queued) {
                (void)m_controller.requestCancel(id);
            }
        });
    }
    // KeepQueuedTerminate：排队任务保留不动（随会话终结消失——P-EX-6）；
    // 停止派发由 closed() 条件承载（EX-T05 调度器在派发前查 closed，
    // 关闭态不再派发任何新批次/新任务）。
}

bool DrainCoordinator::closed() const noexcept
{
    return m_closed;
}

bool DrainCoordinator::drained() const noexcept
{
    if (!m_closed) {
        return false;   // 未关闭谈不上"排空完成"（drained 是 shutdown 的
                        // 完成查询——§10.1 注）
    }
    // 排空完成＝无在途运行（Preparing/Running/Canceling）。排队任务不
    // 阻塞 drained（KeepQueued 保留是设计决定非未决工作——头注）；
    // 本查询无阻塞（快照判定——"无永久等待"的查询面：等待方轮询而非
    // 阻塞等）。
    bool busy = false;
    m_controller.forEachTask([&busy](TaskId, TaskStateMachine& machine) {
        if (isInFlight(machine.state())) {
            busy = true;
        }
    });
    return !busy;
}

std::optional<DrainPolicy> DrainCoordinator::policy() const noexcept
{
    return m_policy;
}

std::size_t DrainCoordinator::requestCancelAll()
{
    // 协作取消路径（§7.4 PM-03"协作取消"分支：对任务清单逐/批量
    // requestCancel 后走取消协议）。全部非终态任务逐一请求；终态任务的
    // 拒绝（accepted=false）不计入受理数。后续收敛由 TaskController::
    // poll 推进（在途任务 2 s 生效＋10 s 收敛＋超时强杀兜底；排队任务
    // 直达）。
    std::size_t accepted = 0;
    m_controller.forEachTask([this, &accepted](TaskId id, TaskStateMachine& machine) {
        if (!isTerminalTaskState(machine.state())) {
            if (m_controller.requestCancel(id).accepted) {
                ++accepted;
            }
        }
    });
    return accepted;
}

std::size_t DrainCoordinator::abandonAllForced()
{
    // 强制兜底（§7.5"超阈值由 L5 关闭控制器强制 abandonAll(ForceTerminated)
    // ——project §9.7 同口径"）。处置面仅**在途**任务：排队任务保留语义
    // 不被兜底破坏（KeepQueued；CancelQueuedAndWait 下排队已清空，此处
    // 天然无排队可处置）。
    std::size_t handled = 0;
    m_controller.forEachTask([this, &handled](TaskId id, TaskStateMachine& machine) {
        if (isInFlight(machine.state())) {
            if (m_controller.requestForceTerminate(id).accepted) {
                ++handled;
            }
        }
    });
    return handled;
}

void DrainCoordinator::poll()
{
    // ---- 第 1 段：协议推进（任务级时序归 TaskController——取消协作窗/
    // 运行超时/强杀序列）----
    m_controller.poll();

    // ---- 第 2 段：排空监视（仅关闭态；未关闭无兜底语义——在途运行的
    // 长时间运行是正常业务，不是"排空超时"）----
    if (!m_closed || m_config.abandonThreshold.count() <= 0 || m_autoAbandonDone) {
        return;
    }
    if (drained()) {
        return;   // 已排空：无兜底必要（阈值只在"关闭后仍有在途"时计时意义）
    }
    const bool thresholdExceeded = m_shutdownAt.has_value()
        && now() - *m_shutdownAt >= m_config.abandonThreshold;
    if (thresholdExceeded) {
        // 超阈值自动兜底（恰一次——m_autoAbandonDone 防线）：关闭流程
        // 的无永久等待保证（§7.5"执行侧保证关闭流程无永久等待：排空
        // 有界……超阈值由 L5 关闭控制器强制 abandonAll(ForceTerminated)"
        // ——本触发是 L5 兜底的执行侧承载；残留（若有）记开发诊断的
        // 呈现面归 project §9.7 对端口径）。
        (void)abandonAllForced();
        m_autoAbandonDone = true;
    }
}

}  // namespace sdurws::ird::execution
