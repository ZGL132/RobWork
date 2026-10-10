/**
 * @file   CloseSupervisor.hpp
 * @brief  L5 关闭控制器（StoreCloseSupervisor）——存储上下文关闭等待
 *         （awaitStoreClosed 无上限轮询）的超阈值兜底监督（project.md
 *         §9.7 登记的兜底义务；宿主收口批次 ASM-WF 落位）。
 *
 * 设计依据：
 *   - units/project.md §9.7（存储上下文引用计数协议——"**防永久等待**
 *     （自审项 A-4）：pending 的清零由 execution 的结束路径保证（成功/
 *     取消/失败/强杀均 abandon）；**L5 关闭控制器在超阈值（如取消协议
 *     10 s + 余量）后可强制 abandonAll(ForceTerminated) 收尾——残留会话
 *     记开发诊断，不阻塞进程退出**"）
 *   - units/execution.md §7.5（"排空有界（取消协议 10 s＋归档有界重试
 *     ＋abandon 兜底；超阈值由 L5 关闭控制器强制 abandonAll(Force-
 *     Terminated)——project §9.7 同口径，残留记开发诊断"）、§11 无永久
 *     等待行（"abandon 全路径覆盖＋L5 超阈值强制 abandonAll 兜底"）
 *   - units/workflow.md §7.3（A7 等待语义——"轮询无上限是 A7 语义本身，
 *     有界性由 execution 终态路径全部释放归档引用＋L5 关闭控制器
 *     abandonAll 兜底保证"；src/Lifecycle.cpp awaitStoreClosed 注）
 *   - execution Scheduler.hpp TaskScheduler::tick（调度推进——关闭期内部
 *     转 DrainCoordinator::poll 排空监视，超阈值自动兜底 abandonAllForced
 *     恰一次；DrainCoordinator::Config::abandonThreshold 默认 15 s＝
 *     10 s 协议窗＋5 s 余量）＋ F-578 线程约束（abandonAllForced 必须在
 *     调度主锁串行域内——本监督器只经 tick 驱动，不越锁域直调强制入口）
 *   - 需求 ARCH §6.8 A7（等待在途归档）；R1 零新增 WF- 稳定码（D-WF-7
 *     ——兜底告知走 Dev 级日志，不走用户级稳定码，Settings v1.1 诊断
 *     通道选择同案）
 *
 * 背景说明（第一读者须知——监督器兜的是什么底）：
 *   PM-03 关闭编排（CloseFlow::run 第 6 段）在编排线程上无上限轮询
 *   store.closed()（A7 语义——project §9.7"本契约不设超时参数"）。该
 *   轮询的有界性由两面保证：①execution 侧——全部任务终态路径释放归档
 *   引用＋排空自动兜底（超阈值 abandonAllForced 强杀在途——宿主调度
 *   循环持续 tick 驱动）；②本监督器（L5 侧）——宿主在发起关闭前启动
 *   它：监督线程周期**观测** store.closed()；超阈值仍未闭合时写 Dev
 *   级日志（"残留会话记开发诊断"——非任务侧在途引用〔在途命令/草稿
 *   保存〕不是 abandonAll 处置面，诊断是唯一可做的诚实告知）；再经
 *   一个宽限期仍未闭合即**停止监督**（不阻塞进程退出——project §9.7
 *   原文义务；残留事实已留痕，等待语义不演变为进程挂死）。
 *
 * 线程模型：start() 起一条监督线程（daemon 语义——析构/stop 必 join）；
 *   监督线程**只观测不驱动**——调度推进（TaskScheduler::tick——取消
 *   协议/排空监视/自动兜底的推进泵）归宿主调度循环线程（"tick 仅调度
 *   域单线程"——Scheduler.hpp；监督线程并发 tick＝锁域违例，实测死锁；
 *   这正是"编排线程只观察不推进"〔Lifecycle.cpp awaitStoreClosed 同款
 *   纪律〕的监督面表达）；store.closed() 为内部互斥快照查询（并发安全
 *   ——project §10.1 口径）；Dev 日志 sink 的实现方须自行保证线程安全
 *   （diagnostics 设施契约）。
 *
 * 错误语义：本监督器零新增稳定诊断码（R1/D-WF-7）——超阈值/放弃监督
 * 都走 IDevLogSink 开发级日志（可空＝静默；生产装配必须注入，Settings
 * v1.1 诊断通道选择同案）；观测位（storeClosedObserved/abandonDiag-
 * Observed/gaveUp）供宿主与测试复核。
 */

#ifndef SDURWS_IRD_WORKFLOW_CLOSESUPERVISOR_HPP
#define SDURWS_IRD_WORKFLOW_CLOSESUPERVISOR_HPP

#include <atomic>
#include <chrono>
#include <functional>
#include <thread>

#include <sdurws/ird/diagnostics/Catalog.hpp>   // diagnostics::IDevLogSink（开发级日志——兜底告知通道，R1 零新增码）
#include <sdurws/ird/execution/Scheduler.hpp>   // execution::TaskScheduler（tick 调度推进载体——白名单边）
#include <sdurws/ird/project/ProjectStore.hpp>  // project::ProjectStore（closed() 观测对象——白名单边）

namespace sdurws {
namespace ird {
namespace workflow {

/**
 * @brief L5 关闭控制器（存储上下文关闭等待的超阈值兜底监督器）。
 *
 * 生命周期与所有权：构造注入 store/scheduler/日志 sink 的引用（全部非
 * owning——调用方保证存活期覆盖监督期）；监督线程随 start() 创建、
 * stop()/析构收敛（一次性的 start/stop 配对，重复 start 幂等 no-op）。
 * 非线程共享对象的使用限制：start/stop/观测位可在宿主线程调用，监督
 * 循环在自有线程——共享态全部经原子量（见成员注）。
 */
class StoreCloseSupervisor final {
public:
    /// 注入时钟（steady 时刻源——与 execution ManualClock 同形可注入；
    /// 空＝steady_clock 生产形态。测试用受控时钟做阈值推进不 sleep）。
    using ClockFn = std::function<std::chrono::steady_clock::time_point()>;

    /**
     * @brief 监督参数（实现参数——非上游阈值，与 execution
     *        DrainCoordinator::Config::abandonThreshold 同口径登记）。
     */
    struct Config {
        /**
         * 兜底诊断阈值（自 start 起量）：超过仍未闭合即写 Dev 日志
         * （"残留会话"告知）。默认 15 s＝取消协议收敛窗 10 s（上游值）
         * ＋5 s 余量——与 execution abandonThreshold 默认同源同值（两
         * 阈值语义不同：execution 的驱动强制 abandonAll，本阈值驱动
         * 诊断与放弃计时——同默认值避免两套口径漂移）。
         */
        std::chrono::milliseconds abandonThreshold{15000};

        /**
         * 放弃监督宽限（自兜底诊断时刻起量）：再经此时长仍未闭合即
         * 停止监督（不阻塞进程退出——project §9.7 原文义务；store 侧
         * 非任务在途引用〔在途命令/草稿保存〕不是 abandon 处置面，监督
         * 不能替它清零——如实留痕后退出是唯一不演变为挂死的选择）。
         * 默认与阈值同值（15 s——总监督窗 30 s 上限量级）。
         */
        std::chrono::milliseconds giveUpGrace{15000};

        /// 轮询拍间隔（1 ms——awaitStoreClosed 同款拍频，避免忙等烧核）。
        std::chrono::milliseconds pollInterval{1};
    };

    /**
     * @brief 构造监督器（装配点）。
     *
     * @param store     [in] 目标存储上下文（非 owning——closed() 观测；
     *                  通常在宿主调用 requestClose 之后/同时处于 Draining）
     * @param scheduler [in] 执行调度器（非 owning——监督器不驱动调度：
     *                  tick 仅调度域单线程〔Scheduler.hpp〕，推进泵归宿主
     *                  调度循环〔F-578 锁域纪律〕；保留引用为装配面完整
     *                  性与监督场景的可定位性）
     * @param config    [in] 监督参数（见 Config 注）
     * @param devLog    [in] 开发级日志 sink（可空＝静默——生产装配必须
     *                  注入，"残留会话记开发诊断"义务的通道面）
     * @param clock     [in] 注入时钟（空＝steady_clock——测试受控推进）
     *
     * @throws WorkflowError abandonThreshold/giveUpGrace/pollInterval
     *         非正值（零/负阈值的监督无语义——调用方装配违约 fail-fast）
     */
    StoreCloseSupervisor(project::ProjectStore& store,
                         execution::TaskScheduler& scheduler,
                         Config config = {},
                         diagnostics::IDevLogSink* devLog = nullptr,
                         ClockFn clock = nullptr);

    /// 析构（自动 stop——监督线程不留后台悬挂）。
    ~StoreCloseSupervisor();

    StoreCloseSupervisor(const StoreCloseSupervisor&) = delete;
    StoreCloseSupervisor& operator=(const StoreCloseSupervisor&) = delete;

    /**
     * @brief 启动监督（幂等——已在监督中则 no-op）。
     *
     * 启动即记录 t0（阈值计时起点）；监督循环（先观测后查停止——首拍
     * 观测优先〔start 后立即 stop 的短会话形态下闭合事实不被吞〕）：
     * ①store 已闭合 → 记观测退出；②越兜底阈值仍未闭合 → 一次性
     * Dev 日志（残留会话告知）＋观测位；③越放弃宽限仍未闭合 → Dev
     * 日志（放弃监督）＋退出（不阻塞进程退出）；④stop() 请求 → 退出。
     */
    void start();

    /**
     * @brief 停止监督并收敛线程（幂等；未启动＝no-op）。阻塞至监督线程
     *        退出——调用后监督器回到可重新 start 的初始态（观测位保留
     *        ——历史事实不因重启清零）。
     */
    void stop() noexcept;

    /// 监督线程是否在运行（start 后、stop/自然退出前为 true）。
    bool running() const noexcept { return m_running.load(std::memory_order_acquire); }

    /// 监督期间是否观测到 store 闭合（closed()——关闭完成的复核面）。
    bool storeClosedObserved() const noexcept
    {
        return m_storeClosedObserved.load(std::memory_order_acquire);
    }

    /// 监督期间是否触发过兜底诊断（超阈值仍未闭合——残留会话告知位）。
    bool abandonDiagObserved() const noexcept
    {
        return m_abandonDiagObserved.load(std::memory_order_acquire);
    }

    /// 监督是否以"放弃"收场（越放弃宽限仍未闭合——不阻塞进程退出的
    /// 诚实退出位；true 时宿主应把残留事实升级人工处置）。
    bool gaveUp() const noexcept { return m_gaveUp.load(std::memory_order_acquire); }

private:
    /// 监督循环体（监督线程入口——见 start() 注的①~④序）。
    void superviseLoop();

    project::ProjectStore& m_store;       ///< 目标存储上下文（非 owning）
    execution::TaskScheduler& m_scheduler;      ///< 执行调度器（非 owning——tick 推进载体）
    Config m_config;                      ///< 监督参数（构造后不变）
    diagnostics::IDevLogSink* m_devLog;   ///< 开发级日志 sink（可空＝静默）
    ClockFn m_clock;                      ///< 注入时钟（空＝steady_clock）

    std::thread m_thread;                 ///< 监督线程（start 创建/stop 收敛）
    std::atomic<bool> m_stopRequested{false};   ///< 停止请求位（stop→线程）
    std::atomic<bool> m_running{false};         ///< 监督线程存活位（观测面）
    std::atomic<bool> m_storeClosedObserved{false};///< 闭合观测位（①段产物）
    std::atomic<bool> m_abandonDiagObserved{false};///< 兜底诊断位（③段产物）
    std::atomic<bool> m_gaveUp{false};          ///< 放弃监督位（④段产物）
};

}  // namespace workflow
}  // namespace ird
}  // namespace sdurws

#endif  // SDURWS_IRD_WORKFLOW_CLOSESUPERVISOR_HPP
