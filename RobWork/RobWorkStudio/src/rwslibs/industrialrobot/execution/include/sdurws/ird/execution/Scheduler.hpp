/**
 * @file   Scheduler.hpp
 * @brief  任务调度器（EX-T05：ITaskScheduler 契约面＋TaskScheduler 调度
 *         本体——队列/优先级/预算/提交验证 V1~V4/内联门槛/进度节流）、
 *         DrainPolicy 排空编排（EX-T03：§7.5"UI 关闭二选"的执行侧）与
 *         资源治理（EX-T07：IMemorySampler 采样接缝＋ResourceController
 *         三段节流决策＋资源不足诊断——§6.1 内存预算/§6.6 内存采样）。
 *
 * 设计依据：
 *   - units/execution.md §6.1（队列/优先级与并发治理：双优先级＋同级 FIFO
 *     不抢占〔D-04〕、并发上限、UI 线程不阻塞〔NFR-PERF-01〕、进度报告
 *     与节流 ≤10 Hz〔实现参数——§6.1 表"进度报告与节流"行〕；内存预算
 *     行：主进程＋全部 worker 合计峰值 ≤ 物理内存 70%，接近上限〔默认
 *     阈值 65%，实现参数〕先节流〔暂停派发＋降低并行度〕→仍超限→
 *     EX-RESOURCE-INSUFFICIENT 诊断，已排队/运行任务不失败〔NFR-PERF-04；
 *     D-05 三段治理：65% 节流阈值→停派发/降并行→70% 诊断〕）、§6.3
 *     （提交验证清单 V1~V4 与内联门槛——可预测 <1 s 调度线程内联，
 *     ARCH §4.1）、§10.1（ITaskScheduler/ResourceBudget/SubmitResult/
 *     DrainPolicy 接口原文）、§10.8（接口共性约束：提交/查询任意线程、
 *     状态写调度串行；调用方违约抛 ExecutionError）、§4.3（身份分配协议
 *     ——提交受理段）、§7.5（UI 关闭与应用崩溃——关闭二选、排空有界、
 *     超阈值强制 abandonAll(ForceTerminated) 兜底）、§7.4（UI 关闭行）、
 *     §6.2（资源监控线程：周期内存采样＋节流决策建议——决策仍由调度
 *     线程执行）、§6.6（内存采样行：GlobalMemoryStatusEx＋作业内存——
 *     MemProbe 汇总"主进程＋全部工作进程"）、§3.3（IExecutionDiagnosticsSink
 *     注入——资源不足诊断的出口）
 *   - ARCHITECTURE.md §4.1（可预测 <1 s 轻任务调度线程内联——内联门槛
 *     语义锚）、§4.4（协议锚点；P-EX-2 处置同 Controller.hpp）、§4.6
 *     （资源治理：ResourceController 汇总主进程＋全部工作进程内存，默认
 *     上限物理内存 70%；接近上限先节流〔降低并行度/暂停派发〕→仍超限
 *     给"资源不足"诊断——v0.11 Draft 待评审，评审 A9+ 变更按影响面
 *     增量同步，P-EX-2）
 *   - 需求 TASK-01（状态机受理入口）、TASK-03（五元组分配在提交受理
 *     路径——§4.3/EX-SUB-1）、UX-10（进度阶段投影边界——execution 零
 *     新增状态词）、NFR-PERF-01（>1 s 转后台——提交/控制非阻塞）、
 *     NFR-PERF-04（70% 内存、先节流后诊断——本头的 ResourceController
 *     承载其执行侧基础形态；规模化验收归 WP-23-T06，§2.2 不可越界列）、
 *     UX-03（关闭触发的排队取消同样零错误诊断）、PM-03（关闭确认对话
 *     框——workflow/ui 侧消费本头的排空语义）、PM-07（只读拒绝启动
 *     ——V3 消费 store.writable()，写权限判定归 project 不私判）
 *   - 任务契约 tasks/foundation/EX-T05.json acceptance 1~5（EX-SUB-1/2、
 *     事件 FIFO 与进度节流、UI 零计算结构断言、队列/优先级/预算/提交
 *     验证/PM-07、UX-10 边界＋P-EX-1 处置）；EX-T03 acceptance 3
 *     （DrainPolicy 排空编排）；EX-T07 acceptance 1~3（EX-RES-2 全绿＋
 *     三段治理就位、内存汇总覆盖主进程＋全部 worker＋比较型三要素诊断
 *     经 §3.3 sink、规模化不越界＋P-EX-1/P-EX-2 处置）
 *
 * 背景说明（两路径与策略值的对应——§7.5 原文展开）：
 *   - **等待排空**＝workflow 调 shutdown(DrainPolicy::CancelQueuedAndWait)：
 *     停止派发新任务＋取消排队任务＋在途运行执行至自然终态（不干预），
 *     drained() 供关闭方查询；KeepQueuedTerminate 是同一"等待"分支的
 *     保留变体：排队任务**不**取消（随会话终结消失——P-EX-6：Queued
 *     任务纯内存，主进程退出即无痕）、在途运行同样等待自然终态；
 *   - **协作取消**＝PM-03 对话框另一选项：对任务清单逐/批量 requestCancel
 *     （DrainCoordinator::requestCancelAll）——在途任务走 §7.1 完整取消
 *     协议（2 s 生效＋10 s 收敛＋超时强杀兜底），排队任务直达 Canceled；
 *   - **无永久等待**（§7.5"排空有界"）：关闭后超阈值（实现参数）仍有
 *     在途运行→强制 abandonAll(ForceTerminated) 兜底（L5 关闭控制器同
 *     口径——project §9.7；DrainCoordinator 提供执行侧承载：自动兜底经
 *     poll 触发，手动兜底经 abandonAllForced 暴露给 L5）。兜底对排队任务
 *     不生效（KeepQueued 保留语义不被破坏；CancelQueuedAndWait 的排队
 *     已在 shutdown 时清空）。
 *
 * 调度线程模型（阶段 A 形态，与 Controller/DrainCoordinator 同一显式
 * 驱动决策——归置登记单元卡 §15.4）：
 *   §6.2"调度线程（每调度器 1 条）"在阶段 A 以**显式驱动**表达：TaskScheduler
 *   不内建真实线程，tick() 即调度线程的推进原语（生产由 L5 装配以固定
 *   周期在其自有线程调用；测试显式调用逐拍断言）。"调度线程串行域"的
 *   实质＝主互斥临界区：submit（任意线程进入）/tryTask/tasksByProject/
 *   tick 在主锁内串行，状态机与队列的全部写动作因此互斥（§10.8"内部转
 *   调度串行"的实施形态）。例外只有内联执行段（§6.3 内联门槛）：执行体
 *   回调可能耗时至 <1 s 量级，在锁外执行——锁只保护簿记不覆盖计算，
 *   这正是"UI 线程零计算"的结构表达（NFR-PERF-01：计算发生在调度域，
 *   提交/查询调用方永不等计算）。
 *
 * 线程约束：submit/tryTask/tasksByProject/setResourceBudget/reportProgress/
 *   shutdown/drained 任意线程（内部经主锁转串行）；tick 仅调度域单线程
 *   （不可重入——内联段会暂时释放主锁，重入将撕裂调度序）；DrainCoordinator
 *   全部方法仅调度线程。TaskScheduler::stopDispatch 是 Controller 在
 *   poll()（主锁内）的回调——同域同线程，不取锁（见其注释）。
 *   ResourceController::evaluate 仅调度线程（tick 在主锁内调用——§6.2
 *   "决策仍由调度线程执行"；其注入的 IMemorySampler 实现随之承担同一
 *   线程约束，生产件 SupervisorMemorySampler 消费监督器的调度线程域聚合）。
 *   EX-T07 资源闸集成说明：tick 段 c 在出队前评估资源决策，pauseDispatch
 *   ＝true 时本拍不出队（§6.1 内存预算行"暂停派发新任务"——排队任务
 *   保持排队、在途任务不受影响〔D-04 不抢占〕，"已排队/运行任务不失败"
 *   由"闸只挡出队、不触碰状态机"的结构保证）。
 */

#ifndef SDURWS_IRD_EXECUTION_SCHEDULER_HPP
#define SDURWS_IRD_EXECUTION_SCHEDULER_HPP

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>     // DiagnosticRecord（提交拒绝诊断承载）
#include <sdurws/ird/core/Events.hpp>       // IDomainEventBus（TaskStatusChanged 发布面——指针注入）
#include <sdurws/ird/core/Identity.hpp>     // ProjectId（tasksByProject 过滤键）
#include <sdurws/ird/execution/Controller.hpp>
#include <sdurws/ird/execution/StateMachine.hpp>  // TaskStateMachine/ITaskEventSink（受理段构造＋事件接缝——本头 TaskScheduler 消费）
#include <sdurws/ird/execution/TaskTypes.hpp>

namespace sdurws::ird::evidence {
class IProducerRegistryView;    ///< V1 评估器注册查询面（isRegistered/contractVersionMatches——前向声明，实现在 .cpp include）
class IRevisionClosureSource;   ///< V2 修订闭包含性查询（evidence §3.3 同源形态）
}  // namespace sdurws::ird::evidence

namespace sdurws::ird::project {
class ProjectStore;             ///< V3 写权限查询面（writable()——前向声明，实现在 .cpp include）
}  // namespace sdurws::ird::project

namespace sdurws::ird::execution {

class IExecutionDiagnosticsSink;  ///< §3.3 诊断注入（Ports.hpp——ResourceController 诊断出口；指针成员，前向声明免拖入 evidence 头链）
class WorkerSupervisor;           ///< worker 池监督器（WorkerSupervisor.hpp——SupervisorMemorySampler 的作业内存合计源；指针成员）


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

// =====================================================================
// ITaskScheduler 契约面（§10.1——EX-T05 落位）
// =====================================================================

/**
 * @brief 资源预算（§10.1 原文结构——字段与默认值逐行对照）。
 *
 * 值语义；经 setResourceBudget 运行期可调且**不影响任务身份**（§6.1
 * "验证不改变任务身份：预算变化只影响排队与派发时机"）。各字段的消费
 * 归属（EX-T05 与 EX-T07 的分工——归置登记单元卡 §15.4）：
 *   - queueCapacity / maxConcurrentTasks / maxTasksPerProject：调度面
 *     消费（EX-T05——V4 提交容量与出队闸）；
 *   - maxMemoryRatio / throttleRatio：ResourceController 消费（EX-T07
 *     兑现——TaskScheduler::setResourceBudget 把本结构转发给已装配的
 *     ResourceController，控制器据两比值做三段节流判定；EX-T05 期
 *     "调度器保存值但不消费"的登记到本任务消账）；
 *   - maxWorkers：worker 池规模——§10.1 原文承载字段，阶段 A 仍无编译
 *     期消费者（池规模治理随编排装配 EX-T09/L5 落位），调度器保存值但
 *     不消费（不预建行为，登记单元卡 §15.4）。
 */
struct ResourceBudget {
    double maxMemoryRatio = 0.70;        ///< 物理内存占比上限（NFR-PERF-04 上游值——不改动；EX-T07 消费）
    double throttleRatio  = 0.65;        ///< 节流阈值（实现参数 D-05——非需求阈值，§15.1 登记；EX-T07 消费）
    std::uint32_t maxWorkers = 0;        ///< worker 池上限；0＝自动（逻辑核-1，下限 1；EX-T06/T07 消费）
    std::uint32_t maxConcurrentTasks = 0;///< 同时在途（Preparing/Running/Canceling）任务上限；
                                         ///  0＝自动 min(4, 逻辑核/2)（§6.1 并发上限行——EX-T05 出队闸）
    std::uint32_t maxTasksPerProject = 2;///< 同项目并发**正式**任务上限（§6.1 防单项目独占；Preview
                                         ///  不计入——不写 results/ 的轻任务不占正式额度；EX-T05 出队闸）
    std::size_t   queueCapacity = 256;   ///< 等待队列容量（§6.1/§6.3 V4：提交期预算检查仅拒绝对列容量溢出
                                         ///  ——溢出＝SubmissionRejected＋诊断，不失败已排队任务）
};

// =====================================================================
// 内存采样接缝与 ResourceController（§6.1 内存预算行/§6.6 内存采样行/
// ARCH §4.6——EX-T07 落位）
// =====================================================================

/**
 * @brief 一次内存采样读数（NFR-PERF-04 汇总口径——§6.1 内存预算行
 *        "主进程＋全部 worker 合计"的三元数据）。
 *
 * 值类型；字段的产生方式（而非数值本身）是契约：totalPhysicalBytes 来自
 *   系统查询（GlobalMemoryStatusEx——分母），mainProcessBytes 来自主进程
 *   工作集（§6.6"主进程经自身 Job 或工作集查询"），workerBytes 来自全部
 *   worker 作业内存合计（§6.6 QueryInformationJobObject——生产装配由
 *   SupervisorMemorySampler 从监督器聚合，见其注）。单位一律字节。
 */
struct MemoryReading {
    std::uint64_t totalPhysicalBytes = 0;  ///< 物理内存总量（占比分母；单位字节）
    std::uint64_t mainProcessBytes = 0;    ///< 主进程占用（工作集——§6.6 口径；单位字节）
    std::uint64_t workerBytes = 0;         ///< 全部 worker 作业内存合计（§6.6 JobMemory 口径；单位字节）

    /// 合计占用（分子）＝主进程＋全部 worker（§6.1 内存预算行的汇总口径）。
    std::uint64_t aggregateBytes() const noexcept { return mainProcessBytes + workerBytes; }

    /**
     * @brief 合计占用占物理内存的比例（ResourceController 判定基准）。
     *
     * @return aggregateBytes / totalPhysicalBytes；totalPhysicalBytes==0
     *         （未装配的读数）返回 0.0 防除零——零读数按"无压力"处置是
     *         保守方向的对称面：判定层对无效读数另有 nullopt 通道（见
     *         IMemorySampler 注），到达本函数的读数视为有效。
     */
    double ratioOfPhysical() const noexcept
    {
        return totalPhysicalBytes == 0
            ? 0.0
            : static_cast<double>(aggregateBytes())
                  / static_cast<double>(totalPhysicalBytes);
    }
};

/**
 * @brief 内存采样注入接缝（EX-T07——ResourceController 的数据源边界）。
 *
 * 为什么是接缝而非直调：§6.1 的治理判定必须可被 EX-RES-2 用"内存采样
 *   fake 推至 >70%"驱动（§11 用例行——观测点＝派发暂停＋诊断），且生产
 *   装配（L5）与测试装配注入不同实现；采样这一面被隔离成单方法接口后，
 *   判定本体（ResourceController）与采样来源（Windows 探针/测试脚本）
 *   彻底解耦——也是 §6.2"资源监控线程〔采样〕……决策仍由调度线程执行"
 *   中采样/决策可分线程的结构前提（阶段 A 同域采样，见 ResourceController 注）。
 *
 * 失败语义：返回 nullopt＝本次采样失败（系统查询不可用等环境错误——
 *   AGENTS §3 错误二分的"可预期失败"侧）；ResourceController 保持最近
 *   一次成功采样的治理状态并经 reportDev 出开发诊断，不猜值、不静默降级。
 *
 * 线程约束：sample 仅在调度线程被调用（evaluate 的调用域——§6.2 决策
 *   线程约束的传染）；实现方无须为并发付费。
 */
class IMemorySampler {
public:
    virtual ~IMemorySampler() = default;

    /**
     * @brief 执行一次内存采样（主进程＋全部 worker＋物理总量）。
     *
     * @return 读数；采样失败返回 nullopt（控制器保持原状态＋开发诊断）
     */
    virtual std::optional<MemoryReading> sample() = 0;
};

/**
 * @brief 生产内存采样器——MemProbe 真实探针＋监督器作业内存合计的装配
 *        件（EX-T07；L5 装配期注入 ResourceController）。
 *
 * 汇总口径（acceptance 2："内存汇总覆盖主进程＋全部工作进程"）：
 *   - totalPhysicalBytes ← win32 MemProbe::querySystemMemory
 *     （GlobalMemoryStatusEx——§6.6"系统"半区）；
 *   - mainProcessBytes ← win32 MemProbe::queryCurrentProcessWorkingSetBytes
 *     （主进程工作集——§6.6"主进程经自身 Job 或工作集查询"）；
 *   - workerBytes ← WorkerSupervisor::aggregateJobMemoryBytes（全部活
 *     worker 作业提交内存峰值求和——§6.6 JobMemory 半区；聚合本体在
 *     监督器，因其作业句柄属调度线程域私有记录）。
 *
 * 任一半区查询失败＝整体采样失败（返回 nullopt）——半读数比无读数更
 *   危险（会把"worker 超限"误判为"主进程无压力"），fail-to-no-data 而非
 *   fail-to-partial。
 */
class SupervisorMemorySampler final : public IMemorySampler {
public:
    /**
     * @brief 构造。
     *
     * @param supervisor [in] worker 池监督器（非所有权；可空＝无 worker
     *                   池装配——workerBytes 恒 0，主进程/系统半区照常采样）
     */
    explicit SupervisorMemorySampler(const WorkerSupervisor* supervisor) noexcept;

    /// 见 IMemorySampler 注（失败语义：任一半区失败→nullopt）。
    std::optional<MemoryReading> sample() override;

private:
    const WorkerSupervisor* m_supervisor;  ///< 作业内存合计源（非所有权；可空）
};

/**
 * @brief 资源治理控制器（ARCH §4.6 命名的 ResourceController——§6.1
 *        内存预算行的三段节流决策本体＋资源不足诊断出具，EX-T07）。
 *
 * 三段治理（D-05：65% 节流阈值→停派发/降并行→70% 诊断——**65% 为实现
 *   参数非需求阈值**（§15.1 D-05 登记），70% 为 NFR-PERF-04 上游值不改动）：
 *   - 合计占比 < throttleRatio（默认 0.65）→ Normal：不干预；
 *   - throttleRatio ≤ 占比 < maxMemoryRatio → Throttled：停派发＋降并行
 *     建议（Decision 两标志）——§6.1"先节流：暂停派发新任务＋降低并行度
 *     （回收空闲 worker）"；
 *   - 占比 ≥ maxMemoryRatio（默认 0.70）→ Exhausted：维持停派发/降并行，
 *     且**进入该层级的第一拍**出具 EX-RESOURCE-INSUFFICIENT 诊断（边沿
 *     触发恰一次——同一持续超限期间不重复刷诊断；退出后再次进入＝新一
 *     轮边沿）。§6.1"仍超限→『资源不足』诊断（EX-RESOURCE-INSUFFICIENT）"
 *     的"仍"字即两段递进的语义：诊断只在节流未能压回占比时出现。
 *
 * "已排队/运行任务不失败"（§6.1 括注/NFR-PERF-04）：本控制器只产出
 *   派发闸建议与诊断，不触碰任何任务状态——闸的执行面在
 *   TaskScheduler::tick（跳过出队段），排队任务保持 Queued、在途任务
 *   自然运行至终态（D-04 不抢占）。
 *
 * 判定函数（decide）是当前读数的纯函数＋一个边沿记忆（上一层级）；判定
 *   结果确定性（同读数同预算同上层级→同决策，NFR-COR-02 同源精神）。
 *   恢复语义＝层级随读数回落即时重判（≤65% 回 Normal，65%~70% 回
 *   Throttled）——无迟滞带（防抖依赖采样间隔的天然低通，实现参数
 *   Config::sampleInterval；规模化形态的迟滞/停留时间归 WP-23-T06，
 *   本任务不提前实现——acceptance 3 边界）。
 *
 * 阶段 A 驱动形态（§6.2 的显式驱动表达，登记单元卡 §15.4）：采样与
 *   决策同在调度域（TaskScheduler::tick 在主锁内调用 evaluate）；
 *   §6.2"资源监控线程"的生产形态（采样与决策分线程、建议面交接）由
 *   编排装配（EX-T09/L5）消费同一 IMemorySampler 接缝实现——判定本体
 *   不因驱动形态而变。真实采样按 Config::sampleInterval 节流（系统查询
 *   不必逐拍执行），间隔内的 evaluate 返回最近决策。
 *
 * 诊断出口：IExecutionDiagnosticsSink（§3.3 注入——P-EX-8：sink 名称/
 *   归属统一归 diagnostics 裁决，裁决前按本单元注入形状消费）；未装配
 *   ＝诊断不外报（EventBus"无消费者是合法装配"同款），Decision.diagnostic
 *   仍携带记录（观测/测试面）——治理动作（停派发）不依赖诊断上报。
 *   采样失败的开发诊断走 reportDev（通道约定 "execution/resource"——
 *   Ports.hpp 的通道约定行）。
 *
 * 线程约束：全部方法仅调度线程（tick 调用域——§6.2"决策仍由调度线程
 *   执行"；无内部锁）。
 */
class ResourceController {
public:
    /// 注入时钟（与 TaskController/DrainCoordinator/TaskScheduler 同形
    /// ——四者须同一 ManualClock 源，采样间隔度量基准才一致）。
    using ClockFn = TaskController::ClockFn;

    /// 实现参数（非上游值——登记单元卡 §15.4）。
    struct Config {
        /**
         * 真实采样最小间隔（§6.2"周期内存采样"的显式驱动参数化：evaluate
         * 逐拍被调用，系统查询按本间隔节流，间隔内复用最近决策——低通
         * 防抖＋省系统调用）。默认 1000 ms；置 0＝逐拍采样（测试对照面）。
         */
        std::chrono::milliseconds sampleInterval{1000};
    };

    /// 治理层级（三段——D-05；两阈值的消费见类注释）。
    enum class Level {
        Normal,     ///< 占比 < throttleRatio——不干预
        Throttled,  ///< throttleRatio ≤ 占比 < maxMemoryRatio——先节流（停派发＋降并行建议）
        Exhausted,  ///< 占比 ≥ maxMemoryRatio——仍超限（维持节流＋EX-RESOURCE-INSUFFICIENT 诊断）
    };

    /// 一次评估的决策（调度域消费的只读投影）。
    struct Decision {
        Level level = Level::Normal;             ///< 本次治理层级
        bool pauseDispatch = false;              ///< 停派发（Throttled/Exhausted 为 true——tick 据此跳过出队段）
        bool reclaimIdleWorkers = false;         ///< 降并行建议面（§6.1"回收空闲 worker"——池所有者据其调用
                                                 ///  WorkerSupervisor::reclaimIdleWorkers；本控制器不持池引用，
                                                 ///  决策与执行分离——§6.2"决策仍由调度线程执行"的编排半区）
        std::optional<core::DiagnosticRecord> diagnostic;  ///< EX-RESOURCE-INSUFFICIENT（进入 Exhausted 的边沿恰一次；
                                                           ///  比较型三要素＝实际占比/上限/单位"1"——acceptance 2）
        MemoryReading reading;                   ///< 决策所依据的读数（间隔内复用时＝最近一次成功采样；观测面）
    };

    /**
     * @brief 构造。
     *
     * @param sampler [in] 内存采样源（非所有权引用——空采样源＝治理无
     *                观测面，属装配错误，引用形参在类型层排除 nullptr）
     * @param config  [in] 实现参数（采样间隔）
     * @param clock   [in] 注入时钟（空＝steady_clock——与调度器同源注入）
     */
    explicit ResourceController(IMemorySampler& sampler, Config config = {},
                                ClockFn clock = nullptr);

    /// 注入诊断 sink（§3.3——装配期一次；可空＝诊断不外报，见类注释）。
    void setDiagnosticsSink(IExecutionDiagnosticsSink* sink) noexcept;

    /**
     * @brief 应用资源预算中的内存治理参数（TaskScheduler::setResourceBudget
     *        转发——单一用户入口不变，EX-T05 登记的"ResourceController 消费"
     *        归属就此兑现）。
     *
     * @param budget [in] 预算（只消费 maxMemoryRatio/throttleRatio 两字段；
     *               maxWorkers 的池规模消费仍归编排装配——见 ResourceBudget 注）
     */
    void setBudget(const ResourceBudget& budget) noexcept;

    /**
     * @brief 评估一拍：按采样间隔取读数→三段判定→边沿诊断（调度域调用）。
     *
     * 采样失败（sampler 返回 nullopt）：保持最近层级与读数，边沿记忆
     *   不变（失败不构成层级迁移——不猜值），并出一次 reportDev 开发
     *   诊断（连续失败只报首次——边沿去抖；恢复成功后清标志）。
     *
     * @return 本次决策（间隔内＝最近决策的重放；diagnostic 在重放拍为空
     *         ——边沿诊断只在真实采样迁移层级的那一拍出具恰一次）
     */
    Decision evaluate();

    /// 最近治理层级（观测面——未评估过＝Normal）。
    Level level() const noexcept { return m_level; }

    /// 生产默认时钟（透传 TaskController::steadyClock——同一基准）。
    static std::chrono::steady_clock::time_point steadyClock() noexcept;

private:
    /// 三段判定＋边沿诊断构造（读数的纯函数＋m_level 边沿记忆；前置：
    /// reading 有效）。Exhausted 进入边沿产出比较型诊断并经 sink 外报。
    Decision decide(const MemoryReading& reading);

    IMemorySampler& m_sampler;        ///< 采样源（非所有权——构造引用注入）
    IExecutionDiagnosticsSink* m_sink = nullptr;  ///< 诊断出口（可空——合法装配）
    Config m_config;                  ///< 实现参数（采样间隔）
    ClockFn m_clock;                  ///< 注入时钟（空＝steady_clock）
    ResourceBudget m_budget{};        ///< 内存治理参数来源（默认 0.65/0.70——D-05/上游值）
    Level m_level = Level::Normal;    ///< 最近治理层级（边沿判定记忆）
    MemoryReading m_lastReading{};    ///< 最近一次成功采样读数（间隔内重放与采样失败保持的依据）
    std::optional<std::chrono::steady_clock::time_point> m_lastSampleAt;  ///< 最近真实采样时刻（间隔节流基准）
    bool m_probeFailureReported = false;  ///< 采样失败开发诊断的边沿标志（恢复成功清零）
};

/**
 * @brief 提交结果（§10.1 原文结构——结构化拒绝的承载面）。
 *
 * 错误语义（§10.1 注）：结构化拒绝优先经 diagnostics（不抛）——V1~V4
 * 任一失败都返回 accepted=false＋携带稳定码诊断的 diagnostics；抛出仅限
 * 调用方违约（如关闭后 submit 的 ContextClosed）。拒绝**不产生 TaskRecord**
 * （§5.1 注——提交边界验证失败发生在状态机之外，不占用任何状态）。
 */
struct SubmitResult {
    bool accepted = false;                             ///< 受理＝true（task 非空）；拒绝＝false（task 空）
    std::optional<TaskId> task;                        ///< 受理时分配的任务身份（拒绝时空——§4.3 受理段）
    std::vector<core::DiagnosticRecord> diagnostics;   ///< 拒绝原因（稳定码 EX-TASK-REJECTED/EX-SNAPSHOT-STALE/
                                                       ///  EX-STORE-READ-ONLY——ERR-01 字段完整；受理时为空）
};

/**
 * @brief 提交前验证注入点（§3.1 Scheduler.hpp 组成行原文名 ISubmissionGuard）。
 *
 * 背景：§6.3 V1 形式校验含"五元组身份前缀合法（project/branch/revision
 *   **属当前存储上下文**）"——"属当前上下文"是存储侧知识（PA-1：项目
 *   身份/修订闭包的权威在 project，execution 不私判），经本注入点由 L5
 *   装配期提供（适配 store 的上下文查询），调度器只消费结论。
 *
 * 错误语义：返回空 optional＝通过；返回诊断＝拒绝（调度器把该诊断原样
 *   放入 SubmitResult.diagnostics，整体按 SubmissionRejected 处置——
 *   不抛异常，结构化拒绝面）。
 *
 * 线程约束：任意线程（submit 调用线程，主锁内）——实现方保证并发只读
 * 安全（典型实现转发 store 只读查询）。
 */
class ISubmissionGuard {
public:
    virtual ~ISubmissionGuard() = default;

    /**
     * @brief 对一份提交做上下文侧校验（V1 的注入半区）。
     *
     * @param submission [in] 待验提交（只读）
     * @return nullopt＝通过；非空＝拒绝诊断（稳定码＋ERR-01 完整字段——
     *         原样进入 SubmitResult.diagnostics）
     */
    virtual std::optional<core::DiagnosticRecord> checkSubmission(
        const TaskSubmission& submission) const = 0;
};

/**
 * @brief 内联运行结果（§6.3 内联门槛的执行体应答面）。
 *
 * cause 仅允许 Completed / Failed 两值（内联走链的终点只有 T8/T9 两个
 * 合法出口；其他值＝执行体违约，调度器 fail-fast——见 TaskScheduler 注）。
 */
struct InlineRunOutcome {
    TerminationCause cause;                            ///< 终结原因（Completed→T8；Failed→T9）
    std::vector<core::DiagnosticRecord> diagnostics;   ///< 执行期诊断（失败原因等——追加到任务记录）
};

/**
 * @brief 内联运行执行体接缝（§6.3 内联门槛的"计算本体"注入点）。
 *
 * 背景：内联＝可预测 <1 s 的轻任务在调度线程（串行域）内直接执行，
 *   不经 worker 派发（§6.3"无 worker 派发；同一转移表"）。**评估计算
 *   本体不属于 execution**（N-3：业务算法归各域单元）——阶段 A 的执行
 *   体由 L5 装配注入（正式）或测试替身（§11 ScriptedEvaluator 同精神）。
 *   未注入（nullptr）＝无内联能力：满足内联谓词的任务退化为普通排队
 *   （等待 EX-T06 worker 派发链），不报错——装配缺失的显式降级语义
 *   （同 setEventBus 空指针先例：ResultAdmission 侧"总线未装配"）。
 *
 * 归置边界（登记单元卡 §15.4）：内联门槛的**登记段**（registerRun＋归档
 *   预留——§6.3 括注"仍登记 RunRegistry"）依赖派发装配面（Materials/
 *   归档网关/runDir 布局），随 EX-T06 派发链就绪统一放开；本任务的
 *   内联走链完整支持 Preview 任务（表 1：Preview 不登记不归档——零登记
 *   语义自洽），非 Preview 任务即使满足谓词也不内联（排队等待普通派发
 *   ——登记完整性优先于派发开销，装配缺失不静默放行）。
 *
 * 线程约束：run 在调度域（tick 内联段）被调用——锁外单线程串行；实现方
 *   无须为并发付费，但回调必须有限时长（可预测 <1 s 是其存在前提）。
 */
class IInlineRunExecutor {
public:
    virtual ~IInlineRunExecutor() = default;

    /**
     * @brief 执行一次内联评估（调度线程，锁外——计算本体所在）。
     *
     * @param record [in] 任务记录只读快照（提交内容＋能力——执行体据此
     *                自行取用评估输入；execution 不解析快照内容）
     * @return 终结结论（cause∈{Completed, Failed}＋执行期诊断——调度器
     *         据此走 T8/T9 并把诊断追加到任务记录）
     */
    virtual InlineRunOutcome run(const TaskRecord& record) = 0;
};

/**
 * @brief 调度器契约（§10.1 原文接口——提交/查询/预算/排空）。
 *
 * 前置/后置/错误/副作用逐条见 TaskScheduler 实现类注释（契约权威在本
 * 头的注释与 §10.1/§6.3 原文）。
 */
class ITaskScheduler {
public:
    virtual ~ITaskScheduler() = default;

    /// 前置：快照已冻结（evidence builder 产物）；评估器已注册；调度器未
    /// shutdown。后置：受理→TaskId 分配→Queued＋TaskStatusChanged(Queued)
    /// 事件（§4.3 受理段）；拒绝→无 TaskRecord（§5.1 注）。
    /// 错误：结构化拒绝经 SubmitResult.diagnostics（不抛）；抛出仅限
    /// 调用方违约（关闭后提交→ExecutionError(ContextClosed)）。
    virtual SubmitResult submit(TaskSubmission&& submission) = 0;

    /// 并发只读快照（未登记→nullopt；深拷贝投影——§4.2 通用约定）。
    virtual std::optional<TaskSnapshot> tryTask(TaskId task) const noexcept = 0;

    /// PM-03 任务清单数据（按提交快照锚定项目过滤——§10.1 原文签名）。
    virtual std::vector<TaskSnapshot> tasksByProject(core::ProjectId project) const = 0;

    /// 运行期可调；不影响任务身份（§6.1——预算变化只影响排队与派发时机）。
    virtual void setResourceBudget(const ResourceBudget& budget) = 0;

    /// §7.5 排空；幂等。关闭后 submit→ExecutionError(ContextClosed)。
    virtual void shutdown(DrainPolicy policy) = 0;

    /// shutdown 完成查询（无永久等待——轮询面，无阻塞原语）。
    virtual bool drained() const noexcept = 0;
};

// =====================================================================
// TaskScheduler——调度器本体（§6.1~§6.3/§4.3 的可执行体）
// =====================================================================

/**
 * @brief 任务调度器（ITaskScheduler 实现——EX-T05 卡行产物：
 *         队列/优先级/预算/内联门槛/提交验证＋进度节流）。
 *
 * 组合结构：TaskController（EX-T03 取消/终止协议引擎）与 DrainCoordinator
 *   （EX-T03 排空编排）的非所有权组合——调度器拥有"受理/排队/派发"面，
 *   协议时序与关闭编排经既有公共面复用，零重实现（§10.1 注：shutdown
 *   以 DrainCoordinator 组合实现）。
 *
 * 提交受理序（§6.3 V1~V4＋§4.3 受理段——逐条对应，首错即停）：
 *   [V1] 形式校验：ISubmissionGuard 上下文校验（注入半区）→快照身份三元组
 *        isValid→快照冻结（snapshotId 非保留值）→策略/名称映射内容身份
 *        非空（CON-06）→mode 值域→评估器已注册且 contractVersion 相符
 *        （evidence 注册查询面）；失败→EX-TASK-REJECTED 结构化拒绝；
 *   [V2] 内容校验：修订闭包含性（evidence IRevisionClosureSource 形态
 *        注入——objectClosure 每条 (oid,cv) 均属锚定修订；任一失配→
 *        EX-SNAPSHOT-STALE——CON-01 派发前快照完整性；载荷摘要抽查在
 *        物化时全量校验，归物化链路非提交面）；
 *   [V3] 权限校验（Quick/Verified）：store.writable()（project 权威——
 *        execution 不私判写权限，PM-07 只读拒绝启动→EX-STORE-READ-ONLY；
 *        Preview 跳过——不写 results/）；
 *   [V4] 预算校验：等待队列容量（§6.1：提交期预算检查**仅**拒绝对列容量
 *        溢出→EX-TASK-REJECTED＋队列满诊断；资源余量不足不拒绝——排队）。
 *   全通过→TaskId::generate()→TaskRecord{Queued, capability 经注册表推导}
 *   →状态机 T1（发布 TaskStatusChanged(Queued)）→入等待队列（双优先级
 *   FIFO）。**TaskId 分配在提交受理时完成**（acceptance 1 括注——§4.3
 *   分配协议第一段的落点；RunId/Attempt 延迟到派发登记段，§4.3）。
 *
 * 校验面缺失语义（fail-closed，登记单元卡 §15.4）：V1/V2/V3 的协作指针
 *   （评估器注册查询/修订闭包/存储写权限）任一为空→对应校验按**失败**
 *   处置（拒绝提交＋开发诊断说明装配缺失），不静默放行——缺校验面的
 *   受理等于绕过 CON-01/PM-07 门禁。ISubmissionGuard 为空＝无注入半区
 *   （可选增强，跳过不拒绝）。
 *
 * 出队与派发（tick 段，§6.1）：关闭态不出队；资源闸（EX-T07——已装配
 *   ResourceController 时先评估：Throttled/Exhausted 本拍不出队，§6.1
 *   内存预算行"暂停派发新任务"，排队任务保持排队、在途任务不受影响）
 *   通过后，并发额度（在途计数 < maxConcurrentTasks，0=自动）与同项目
 *   正式任务上限（maxTasksPerProject）满足时按 Interactive→Background、
 *   同级 FIFO（提交序号单调）出队头任务走 T2（Queued→Preparing）。队头
 *   阻塞语义：队头任务受项目上限约束时整个队列等待（严格 FIFO/优先级
 *   序——可预测性优先于吞吐，D-04 排序键精神，登记 §15.4）。不抢占
 *   （D-04）：已 Running 任务不因更高优先级到达被打断——出队只发生在
 *   额度空位时。普通（非内联）任务停在 Preparing 等待 EX-T06 派发链
 *   （worker 启动/登记段）。
 *
 * 内联门槛（tick 段，§6.3）：出队任务若满足注入谓词（可预测 <1 s——
 *   ARCH §4.1）且为 Preview 且执行体已注入→锁外执行（计算本体）→
 *   T5（PrepareSucceeded——无 worker 派发段）→T8/T9（按执行体结论）。
 *   谓词未注入或执行体缺失→普通排队（不内联）。同任务事件序＝转移序
 *   （Queued→Preparing→Running→终态——同发布者 FIFO 的状态机侧保证，
 *   §4.3/§10.4）。
 *
 * 进度节流（§6.1"进度报告与节流"）：reportProgress（任意线程——§6.2
 *   通道读取线程入口）在主锁内按每任务节流窗（默认 100 ms＝≤10 Hz，实现
 *   参数 D-07——非上游需求值）过滤，超窗帧经 tick 应用到任务记录
 *   （StateMachine::updateProgress），窗内帧丢弃（worker 进度高频连续，
 *   最新值由后续帧携带）。UX-10 边界：phaseToken 原样透传，execution
 *   零新增状态词（七态状态词与映射归 ui——§2.2/§13 ui 行）。
 *
 * 错误语义（§10.8 两分法）：结构化拒绝（V1~V4）经 SubmitResult 不抛；
 *   调用方违约 fail-fast——关闭后 submit→ExecutionError(ContextClosed)、
 *   未知任务的进度上报→ExecutionError(InvalidState)、终态任务的进度帧→
 *   同左（StateMachine::updateProgress 防御）、内联执行体返回非法 cause→
 *   ExecutionError(InvalidState)（装配违约，不带病终结）。
 *
 * 线程约束：见文件头"调度线程模型"。主锁即调度串行域的阶段 A 表达；
 *   Controller 全部方法调用点都在主锁内（满足其"仅调度线程"约束的
 *   实质——与 poll 互斥的串行域）。
 */
class TaskScheduler final : public ITaskScheduler,
                            public IDispatchGate,
                            private ITaskEventSink {
public:
    /// 注入时钟（与 Controller/DrainCoordinator 同形——同一 ManualClock
    /// 源注入三者，节流窗与协议窗的度量基准才一致）。
    using ClockFn = TaskController::ClockFn;

    /// 实现参数（非上游值——登记单元卡 §15.1/§15.4）。
    struct Config {
        /**
         * 进度节流最小间隔（默认 100 ms＝同任务对外发布 ≤10 Hz 上限——
         * §6.1 表"进度报告与节流"行的实现参数化表达；非上游需求值）。
         * 置 0＝不节流（逐帧透传——测试对照面）。
         */
        std::chrono::milliseconds progressMinInterval{100};

        /**
         * 内联资格谓词（§6.3 内联门槛"请求标记 inline-eligible 且可预测
         * <1 s"的判定注入——"可预测"是评估器/装配侧知识，execution 不
         * 私猜时长）。空＝无任务内联（全部排队——缺省保守形态）。
         */
        std::function<bool(const TaskSubmission&)> inlineEligible;
    };

    /// 提交验证协作面（V1~V3 的查询指针——全部非所有权、装配期注入；
    /// 失败语义见类注释"校验面缺失语义"）。
    struct Collaboration {
        const evidence::IProducerRegistryView* evaluators = nullptr;  ///< V1 评估器注册查询（isRegistered/contractVersionMatches）
        const evidence::IRevisionClosureSource* closure = nullptr;    ///< V2 修订闭包含性（evidence §3.3 同源形态）
        project::ProjectStore* store = nullptr;                       ///< V3 写权限（writable()——PM-07，Quick/Verified 消费）
        ISubmissionGuard* guard = nullptr;                            ///< V1 上下文注入半区（可空＝跳过该半区）
        const EvaluatorRuntimeCapabilities* capabilities = nullptr;   ///< 能力声明注册表（§5.5——受理时推导 TaskCapability；空＝最小能力）
    };

    /**
     * @brief 构造（协议引擎与排空编排为非所有权组合——调用方保证存活期
     *        覆盖调度器；两者通常与调度器同域装配）。
     *
     * @param controller [in] 取消/终止协议引擎（EX-T03）
     * @param drain      [in] 排空编排（EX-T03——shutdown 组合实现，§10.1 注）
     * @param collab     [in] 提交验证协作面（指针集——可全空，见 fail-closed）
     * @param config     [in] 实现参数（节流窗/内联谓词）
     * @param clock      [in] 注入时钟（空＝steady_clock——三编排须同源）
     */
    TaskScheduler(TaskController& controller, DrainCoordinator& drain,
                  Collaboration collab, Config config = {}, ClockFn clock = nullptr);

    ~TaskScheduler() override;

    TaskScheduler(const TaskScheduler&) = delete;             ///< 主锁域不可拷贝
    TaskScheduler& operator=(const TaskScheduler&) = delete;  ///< 同上

    // ---- 事件接线（装配期一次；任意线程但须先于首个 submit） ----

    /**
     * @brief 注入领域事件总线（TaskStatusChanged 发布面——§10.4：状态机
     *        每次转移发布；ResultArchived 由接纳路径经同一总线发布，
     *        EX-T04 已有独立注入面）。空＝不发布（总线未装配——合法，
     *        事件不持久化 core D-09；此后受理的任务以空 sink 构造状态机，
     *        转移不发事件——StateMachine"无消费者是合法装配"先例）。
     */
    void setEventBus(core::IDomainEventBus* bus) noexcept;

    /// 注入内联执行体（可空＝无内联能力——全部排队，见 IInlineRunExecutor 注）。
    void setInlineExecutor(IInlineRunExecutor* executor) noexcept;

    /**
     * @brief 注入资源治理控制器（EX-T07——可空＝无资源闸〔无内存治理，
     *        合法装配：治理是可选增强，提交/派发语义不因缺装配而改变〕）。
     *
     * 装配后本调度器：①tick 出队段前评估决策（pauseDispatch→跳过出队
     *   ——§6.1"暂停派发新任务"）；②setResourceBudget 同步转发预算
     *   （maxMemoryRatio/throttleRatio 的单一用户入口仍是 ITaskScheduler，
     *   见 ResourceBudget 注）。非所有权；调用方保证存活期覆盖调度器；
     *   装配期一次（与 setEventBus 同纪律——先于首个 submit/tick）。
     */
    void setResourceController(ResourceController* controller) noexcept;

    // ---- ITaskScheduler（§10.1——任意线程，主锁转串行） ----

    SubmitResult submit(TaskSubmission&& submission) override;
    std::optional<TaskSnapshot> tryTask(TaskId task) const noexcept override;
    std::vector<TaskSnapshot> tasksByProject(core::ProjectId project) const override;
    void setResourceBudget(const ResourceBudget& budget) override;
    void shutdown(DrainPolicy policy) override;
    bool drained() const noexcept override;

    // ---- 进度接收（§6.2 通道读取线程入口——任意线程） ----

    /**
     * @brief 上报一帧进度（节流窗过滤后经 tick 应用——见类注释"进度节流"）。
     *
     * @param task    [in] 目标任务（未登记→ExecutionError(InvalidState)——
     *                调用方违约：通道帧携带的任务必经 submit 受理）
     * @param report  [in] 进度帧（ProgressReport::make 已校验字段）
     *
     * @throws ExecutionError(InvalidState) 任务未登记；任务已终态
     *         （updateProgress 防御——终态后无流式更新）
     */
    void reportProgress(TaskId task, const ProgressReport& report);

    // ---- 调度推进（仅调度域单线程——不可重入） ----

    /**
     * @brief 推进调度一拍（§6.2 调度线程职责的显式驱动形态）。
     *
     * 处理序（每段语义见类注释）：
     *   a. 应用待投递进度帧（节流窗放行的帧——updateProgress）；
     *   b. 关闭期：DrainCoordinator::poll()（controller.poll＋超时兜底）
     *      替代裸协议推进，且不再出队新任务（关闭即停止派发——§7.5）；
     *   c. 非关闭期：协议推进（controller.poll——取消命令优先，§7.1）
     *      ＋资源闸评估（EX-T07——已装配 ResourceController 时先决策：
     *      pauseDispatch＝true 本拍不出队，§6.1 内存预算行"暂停派发新
     *      任务"；排队任务保持排队、在途任务不受影响）＋出队派发
     *      （额度/优先级闸→T2→内联走链或停留 Preparing）。
     *
     * 内联执行段在主锁外运行（锁只保护簿记不覆盖计算——文件头调度
     * 线程模型）；其余段全程持锁。
     */
    void tick();

    /// 生产默认时钟（透传 TaskController::steadyClock——同一基准）。
    static std::chrono::steady_clock::time_point steadyClock() noexcept;

    // ---- IDispatchGate（Controller 在主锁内的回调——同域不取锁） ----

    /**
     * @brief 任务进入 Canceling 时把其移出等待队列（§7.1 步 1"停止派发"
     *        的排队半区；T3 直达取消不再出队）。
     *
     * 线程约束：**仅 Controller::poll() 在主锁内回调**（同域同线程）——
     * 不取主锁（重入死锁）；主锁在 tick 全程持有，回调链的数据竞争因此
     * 被 tick 的锁覆盖。非等待队列任务（在途/终态）到达此回调＝无害
     * no-op（出队面只对等待态有意义）。
     */
    void stopDispatch(TaskId task) override;

private:
    /// 单任务簿记（等待队列条目＋投影缓存——主锁域内私有）。
    struct ScheduledTask {
        TaskStateMachine* machine = nullptr;  ///< 状态机（非所有权——本体在 Controller 表）
        TaskPriority priority = TaskPriority::Background;  ///< 队列优先级（提交属性，不可变）
        std::uint64_t submitSeq = 0;          ///< 提交序号（同级 FIFO 排序键——单调递增，D-04）
    };

    // ---- ITaskEventSink（状态机事件接缝→总线——私有实现） ----

    /// 每次转移后由状态机同步回调（主锁域内）；适配为 core DomainEvent
    /// 发布（§10.4：TaskStatusChanged{task:五元组, newState}；进度不入
    /// 事件——core D-09）。仅当总线已装配时状态机才接到本接缝
    /// （受理段判空——见 setEventBus 注），故回调内总线必非空。
    void onTaskStatusChanged(const core::TaskStatusChangedPayload& payload) override;

    // ---- 提交验证（V1~V4——主锁内，受理线程执行） ----

    /// V1~V3 校验（形式/内容/权限）；通过返回空，失败返回稳定码拒绝诊断。
    /// 前置：主锁已持有（受理段调用）。
    std::optional<core::DiagnosticRecord> validateSubmission(const TaskSubmission& submission) const;
    /// V4 预算（等待队列容量）——独立成段以对应 §6.3 清单行序。前置：主锁。
    bool queueHasCapacity() const;

    /// 出队一段（额度/优先级闸＋T2＋内联走链——tick 段 c 的派发半区）。
    /// 前置：主锁已由 lock 持有；内联执行段会临时解锁并在返回前重锁
    /// （锁只保护簿记不覆盖计算——文件头"调度线程模型"）。
    void dispatchOne(std::unique_lock<std::mutex>& lock);

    /// 在途任务计数（Preparing/Running/Canceling——并发额度判据；主锁内）。
    std::size_t inFlightCount() const;

    /// 同项目在途正式任务计数（Quick/Verified——maxTasksPerProject 判据）。
    /// 前置：主锁已持有。
    std::size_t inFlightFormalFor(core::ProjectId project) const;

    /// 等待队列线性移除（stopDispatch 的两队列共用体；未命中＝no-op）。
    /// 前置：主锁已持有（stopDispatch 同域回调——不重复取锁）。
    static void removeFromQueue(std::deque<TaskId>& queue, TaskId task);

    TaskController& m_controller;      ///< 协议引擎（非所有权——EX-T03）
    DrainCoordinator& m_drain;         ///< 排空编排（非所有权——EX-T03；shutdown 组合实现）
    Collaboration m_collab;            ///< 提交验证协作面（指针集）
    Config m_config;                   ///< 实现参数（节流窗/内联谓词）
    ClockFn m_clock;                   ///< 注入时钟（空＝steady_clock）
    ResourceBudget m_budget;           ///< 资源预算（运行期可调——setResourceBudget）

    /// 调度主锁（调度串行域的阶段 A 表达——文件头"调度线程模型"）。
    /// mutable：const 查询面（tryTask/tasksByProject/drained）也在锁内。
    mutable std::mutex m_mutex;

    std::unordered_map<TaskId, ScheduledTask> m_tasks;  ///< 任务簿记（受理到查询终期；TaskId 哈希——TaskTypes 特化）
    std::deque<TaskId> m_interactive;   ///< Interactive 等待队列（同级 FIFO——push_back/pop_front）
    std::deque<TaskId> m_background;    ///< Background 等待队列（同上；Interactive 全序先于本队列）
    std::uint64_t m_nextSubmitSeq = 1;  ///< 提交序号发生器（同级 FIFO 排序键——单调）

    core::IDomainEventBus* m_eventBus = nullptr;    ///< 事件总线（可空——未装配时状态机直接以空 sink 构造，
                                                    ///  转移不发事件〔StateMachine"无消费者是合法装配"先例〕）
    IInlineRunExecutor* m_inlineExecutor = nullptr; ///< 内联执行体（可空——无内联能力）
    ResourceController* m_resourceController = nullptr;  ///< 资源治理控制器（EX-T07——可空＝无资源闸；tick 出队段前评估）

    /// 待应用进度帧（reportProgress 节流放行→tick 段 a 应用——任意线程
    /// 入队、调度域消费的交接队列；随主锁互斥）。
    std::deque<std::pair<TaskId, ProgressReport>> m_pendingProgress;
    /// 每任务最近一次放行帧的时钟戳（节流判定基准——主锁内读写）。
    std::unordered_map<TaskId, std::chrono::steady_clock::time_point> m_lastProgressAt;
};

}  // namespace sdurws::ird::execution

#endif  // SDURWS_IRD_EXECUTION_SCHEDULER_HPP