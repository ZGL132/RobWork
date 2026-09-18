/**
 * @file   Scheduler.hpp
 * @brief  DrainPolicy 排空语义与关闭编排（EX-T03）——§7.5"UI 关闭二选"
 *         的执行侧：等待排空（CancelQueuedAndWait/KeepQueuedTerminate）
 *         与协作取消（requestCancelAll）两路径，含无永久等待的超阈值
 *         abandonAll 兜底。
 *
 * 设计依据：
 *   - units/execution.md §3.1（Scheduler.hpp 组成行含 DrainPolicy）、
 *     §7.5（UI 关闭与应用崩溃——关闭二选、排空有界、超阈值强制
 *     abandonAll(ForceTerminated) 兜底）、§7.4（UI 关闭行："等待"分支＝
 *     排空；"协作取消"分支＝逐/批量 requestCancel；关闭确认对话框不提供
 *     强杀选项——强杀仅作超阈值兜底而非用户选项）、§10.1（DrainPolicy
 *     枚举值域冻结＋shutdown 幂等＋drained"无永久等待"注）
 *   - ARCHITECTURE.md §4.4（协议锚点；P-EX-2 处置同 Controller.hpp）
 *   - 需求 UX-03（关闭触发的排队取消同样零错误诊断）、PM-03（关闭确认
 *     对话框——workflow/ui 侧消费本头的排空语义）
 *   - 任务契约 tasks/foundation/EX-T03.json acceptance 3（DrainPolicy
 *     排空语义实现就位：等待排空与协作取消两路径；其行为用例 EX-ARC-4
 *     "无永久等待＋超阈值 abandon 兜底"归 §11 契约测试套件 EX-T09 承载
 *     ——本头交付语义本体与基础自证用例）
 *
 * 背景说明（两路径与策略值的对应——§7.5 原文展开）：
 *   - **等待排空**＝workflow 调 shutdown(DrainPolicy::CancelQueuedAndWait)：
 *     停止派发新任务＋取消排队任务＋在途运行执行至自然终态（不干预），
 *     drained() 供关闭方查询；KeepQueuedTerminate 是同一"等待"分支的
 *     保留变体：排队任务**不**取消（随会话终结消失——P-EX-6：Queued
 *     任务纯内存，主进程退出即无痕）、在途运行同样等待自然终态；
 *   - **协作取消**＝PM-03 对话框另一选项：对任务清单逐/批量 requestCancel
 *     （本头 requestCancelAll）——在途任务走 §7.1 完整取消协议（2 s 生效
 *     ＋10 s 收敛＋超时强杀兜底），排队任务直达 Canceled；
 *   - **无永久等待**（§7.5"排空有界"）：关闭后超阈值（实现参数）仍有
 *     在途运行→强制 abandonAll(ForceTerminated) 兜底（L5 关闭控制器同
 *     口径——project §9.7；本头提供执行侧承载：自动兜底经 poll 触发，
 *     手动兜底经 abandonAllForced 暴露给 L5）。兜底对排队任务不生效
 *     （KeepQueued 保留语义不被破坏；CancelQueuedAndWait 的排队已在
 *     shutdown 时清空）。
 *
 * ITaskScheduler 归属说明：§10.1 的调度器完整接口（submit/tryTask/
 * setResourceBudget/shutdown/drained）随 EX-T05 调度线程落位——本头先
 * 落 DrainPolicy 枚举（§3.1 组成行登记于本头）与排空编排（EX-T03 卡行
 * 产物"DrainPolicy"），EX-T05 的 ITaskScheduler::shutdown 以本编排器
 * 组合实现（归置登记于单元卡 §15.4）。
 *
 * 线程约束：DrainCoordinator 全部方法**仅调度线程**（状态机唯一写者域；
 * closed()/drained() 的跨线程查询面随 EX-T05 调度器的任意线程投影——
 * §10.8；阶段 A 装配中关闭编排由 workflow 在调度线程外调用的形态经
 * EX-T05 的命令管线转串）。
 */

#ifndef SDURWS_IRD_EXECUTION_SCHEDULER_HPP
#define SDURWS_IRD_EXECUTION_SCHEDULER_HPP

#include <chrono>
#include <cstddef>
#include <optional>

#include <sdurws/ird/execution/Controller.hpp>
#include <sdurws/ird/execution/TaskTypes.hpp>

namespace sdurws::ird::execution {

// =====================================================================
// DrainPolicy（§10.1 枚举值域冻结——两值，不新增）
// =====================================================================

/**
 * @brief 调度器关闭时的排空策略（§10.1 原文枚举——值域冻结）。
 *
 * 两值只编码"排队任务"的处置差异（取消 vs 保留）；在途运行的处置两值
 * 相同：等待自然终态＋超阈值兜底（§7.5——关闭流程不提供用户强杀选项，
 * 强杀仅是兜底机制）。
 */
enum class DrainPolicy {
    CancelQueuedAndWait,   ///< 取消排队任务并等待在途运行终结（"等待"分支——§7.5）
    KeepQueuedTerminate,   ///< 保留排队任务（随会话终结消失——P-EX-6）、终止派发、等待在途运行终结
};

// =====================================================================
// DrainCoordinator——关闭排空编排（执行侧语义本体）
// =====================================================================

/**
 * @brief UI 关闭/会话终结的排空编排器（§7.5 执行侧）。
 *
 * 生命周期：与 TaskController 同域装配（组合引用——本编排不拥有任务，
 * 只按策略驱动 TaskController）；shutdown 幂等（§10.1"shutdown 幂等"注
 * ——重复调用不重复取消/重置阈值计时）。
 *
 * drained() 判定语义：**无在途运行**（无 Preparing/Running/Canceling）。
 * 排队任务不阻塞 drained：KeepQueuedTerminate 下排队保留是设计决定而非
 * 未决工作（随会话终结消失——P-EX-6）；CancelQueuedAndWait 下排队已在
 * shutdown 时清空。该定义使"关闭方等待 drained"在两策略下都有界——
 * 无永久等待的判定面（acceptance 3 自审项）。
 */
class DrainCoordinator {
public:
    /// 注入时钟（与 TaskController::ClockFn 同形——两编排须用同一时钟
    /// 源，否则兜底阈值与协作窗的度量基准漂移）。
    using ClockFn = TaskController::ClockFn;

    /// 实现参数（非上游阈值——登记于单元卡 §15.4 EX-T03 行）。
    struct Config {
        /**
         * 关闭后排空等待的超阈值（自 shutdown 时刻起量）：超过即触发
         * abandonAllForced 自动兜底。默认 15 s＝取消协议收敛窗 10 s（上游
         * 值）＋5 s 余量（实现参数——在途运行若无取消请求则可能长于
         * 协议窗，兜底阈值给足"等待排空"的合理耐心后强制终结）。置 0
         * ＝禁用自动兜底（仅经 abandonAllForced 手动触发——L5 关闭控制
         * 器自管阈值的装配形态）。
         */
        std::chrono::milliseconds abandonThreshold{15000};
    };

    /**
     * @brief 构造（组合 TaskController——非所有权引用，调用方保证存活期
     *        覆盖本编排器）。
     */
    DrainCoordinator(TaskController& controller, Config config = {}, ClockFn clock = nullptr);

    // ---- 关闭控制（仅调度线程） ----

    /**
     * @brief 启动关闭排空（幂等——二次调用不重复动作不重置阈值计时）。
     *
     * CancelQueuedAndWait：全部 Queued 任务逐个 requestCancel（经
     * TaskController 协议：无 worker 直达 Canceled，零诊断——UX-03）；
     * KeepQueuedTerminate：排队任务保留不动。两策略下在途运行（Preparing/
     * Running/Canceling）均不立即干预——等待自然终态或超阈值兜底。
     * 本方法只置位与处置排队；在途收敛由 poll() 推进（调用方在关闭期
     * 持续 poll——等待排空的"等待"即周期 poll，无阻塞等待原语）。
     */
    void shutdown(DrainPolicy policy);

    /// 已进入关闭态（shutdown 曾被调用；调度器据此拒绝新提交——
    /// ContextClosed 的执行侧条件，EX-T05 消费）。
    bool closed() const noexcept;

    /// 排空完成查询：无在途运行（§10.1 drained"shutdown 完成查询（无
    /// 永久等待）"——本实现为无阻塞快照查询，等待方轮询本方法而非阻塞）。
    bool drained() const noexcept;

    /// 关闭策略（未关闭→nullopt）。
    std::optional<DrainPolicy> policy() const noexcept;

    /**
     * @brief 协作取消路径：对全部非终态任务逐个 requestCancel（§7.4
     *        PM-03"协作取消"分支的批量入口）。
     *
     * 在途任务进入 §7.1 完整取消协议（后续由 TaskController::poll 推进）；
     * 排队任务直达 Canceled。返回受理数（终态任务的拒绝不计——UX-03：
     * 关闭触发的取消不产生任何错误诊断）。
     */
    std::size_t requestCancelAll();

    /**
     * @brief 强制 abandonAll（ForceTerminated）兜底（§7.5"超阈值由 L5
     *        关闭控制器强制 abandonAll(ForceTerminated)"的执行侧承载）。
     *
     * 处置面＝全部**在途**任务（Preparing/Running/Canceling）：经
     * TaskController::requestForceTerminate 强杀序列（有 worker→进程树
     * 终止＋T13；无 worker→取消直达）。排队任务不在处置面（KeepQueued
     * 保留语义不被兜底破坏；CancelQueuedAndWait 的排队已清空）。返回
     * 处置数（幂等 no-op 不计）。
     */
    std::size_t abandonAllForced();

    /**
     * @brief 关闭期复合驱动：先推进 TaskController 协议一拍，再做排空
     *        监视（超阈值自动兜底）。调度循环在关闭期以本方法替代裸
     *        controller.poll()；未关闭时等价于 controller.poll()。
     */
    void poll();

    /// 生产默认时钟（透传 TaskController::steadyClock——同一基准）。
    static std::chrono::steady_clock::time_point steadyClock() noexcept;

private:
    /// 时钟读取（与 TaskController 同款缺省回退）。
    std::chrono::steady_clock::time_point now() const noexcept;

    TaskController& m_controller;   ///< 协议引擎（非所有权组合）
    Config m_config;                ///< 实现参数（兜底阈值）
    ClockFn m_clock;                ///< 注入时钟（空＝steady_clock）
    bool m_closed = false;          ///< 关闭态（shutdown 幂等标记）
    std::optional<DrainPolicy> m_policy;  ///< 关闭策略（未关闭为空）
    std::optional<std::chrono::steady_clock::time_point> m_shutdownAt;  ///< 关闭时刻（兜底阈值起点）
    bool m_autoAbandonDone = false; ///< 自动兜底已触发（恰一次——重复 poll 不重复强杀）
};

}  // namespace sdurws::ird::execution

#endif  // SDURWS_IRD_EXECUTION_SCHEDULER_HPP