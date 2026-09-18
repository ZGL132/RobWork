/**
 * @file   Controller.hpp
 * @brief  任务取消与终止协议编排（EX-T03）——取消命令通道、协作窗
 *         （2 s 入 Canceling＋10 s 收敛，NFR-PERF-02/ARCH §4.4）、强制
 *         终止编排（进程树终止——KILL_ON_JOB_CLOSE）、运行超时监视
 *         （EX-WKR-5）、可注入时钟（ManualClock 载体，testkit §6.5/§6.6）。
 *
 * 设计依据：
 *   - units/execution.md §3.1（Controller.hpp 组成行：ITaskController、
 *     CancelAck/StatusAck、ICancelSignal）、§7.1（协作式取消逐步协议）、
 *     §7.4（四类行为对照——取消/强杀的诊断与检查点语义）、§10.2
 *     （ITaskController 契约与 CancelAck/StatusAck 形态）、§10.3
 *     （requestCooperativeCancel/terminateForce 的 worker 操作语义——
 *     本头 IWorkerHandle 接缝的语义锚点）、§11（EX-SM-1/3/4、EX-WKR-4/5
 *     用例与 ManualClock 设施）
 *   - ARCHITECTURE.md §4.4（取消与终止协议四条——上游需求值 2 s/10 s 的
 *     唯一锚点；P-EX-2 处置：v0.11 Draft 待评审，A9+ 变更按影响面增量
 *     同步，影响面集中本卡 §7）
 *   - 需求 NFR-PERF-02（2 s/10 s 协议时序——上游需求值不改动）、UX-03
 *     （正常用户取消不属于错误——取消路径零错误诊断）、TASK-01
 *   - 任务契约 tasks/foundation/EX-T03.json acceptance 1/2/4（≙WP-08-T04；
 *     acceptance 3 的 DrainPolicy 排空编排见 Scheduler.hpp——同任务交付）
 *
 * 背景说明（本头在阶段 A 的落位边界）：
 *   - 协议时序采用**显式驱动模型**：调度线程周期调用 TaskController::poll()
 *     推进协议（EX-T05 调度循环接入）；测试侧以 ManualClock 虚拟推进＋
 *     显式 poll 断言时序（testkit §6.5 同步纪律——正确性判据来自协议状态
 *     与虚拟时钟，禁止 sleep 后断言）。生产侧不内建真实线程/定时器：
 *     "2 s 内生效"由 poll 的命令优先序保证（取消命令在每次 poll 的第一段
 *     处理——§7.1 步 1"取消命令插队于派发之前"），poll 周期上限是调度器
 *     （EX-T05）的配置职责。
 *   - ITaskController 完整接口（§10.2 含 requestPause/requestResume）随
 *     EX-T05 与调度器组合落位：暂停确认＝检查点写出并确认（§7.2），依赖
 *     EX-T06 通道与 EX-T08 检查点协调器；本任务卡行产物是"取消与终止
 *     协议"（EX-T03 行），故本头落位 CancelAck/StatusAck 值类型＋
 *     ICancelSignal 最小接口＋TaskController 取消/终止编排——归置偏差
 *     随单元卡 §15.4 变更记录登记（TaskTypes.hpp ArchivePhase 先例）。
 *
 * 线程约束（§10.8 接口共性约束"提交/控制任意线程进入、内部转调度串行"
 * 在本类的阶段 A 形态）：
 *   - 控制命令 requestCancel/requestForceTerminate：**任意线程**可调——
 *     经命令通道（互斥锁保护）入队，受理判定基于命令入队时刻的状态速览；
 *     命令的实际协议动作由调度线程在 poll() 中执行（串行域）。
 *   - poll()/attachTask()/bindWorker()/setDispatchGate()/releaseResources()/
 *     forEachTask()/progress()：**仅调度线程**（状态机唯一写者纪律——
 *     §4.2/§5.3；对外只读投影的跨线程快照发布机制归 EX-T05 TaskSnapshot）。
 */

#ifndef SDURWS_IRD_EXECUTION_CONTROLLER_HPP
#define SDURWS_IRD_EXECUTION_CONTROLLER_HPP

#include <chrono>
#include <cstddef>
#include <functional>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>     // DiagnosticRecord（反馈/诊断承载）
#include <sdurws/ird/core/Evaluation.hpp>   // TaskState（九态词表归 core）
#include <sdurws/ird/execution/TaskTypes.hpp>  // TaskId/ProgressReport/TerminationCause

namespace sdurws::ird::execution {

class TaskStateMachine;   // 前置声明：受管任务本体（StateMachine.hpp——实现文件内使用）

// =====================================================================
// 结构化应答（§10.2 CancelAck/StatusAck——可预期拒绝经 Ack 反馈不抛，
// §10.8 错误两分法；本任务卡行"取消与终止协议"的应答面）
// =====================================================================

/**
 * @brief 取消请求应答（§10.2 原文形态）。
 *
 * 语义（§10.2 前置注）：任务存在且非终态→accepted=true（请求受理，协议
 * 异步推进）；终态任务→accepted=false＋feedback 携带 InvalidState 说明
 * （任务已终结，无可取消之物）；已在 Canceling→幂等 ack（accepted=true，
 * 不重复推进）。**正常取消的 accepted=true 应答 feedback 为空**——UX-03：
 * 取消不是失败，不产生错误级诊断（feedback 字段本身是应答携带的用户
 * 提示，不写入任务诊断累积，两回事）。
 */
struct CancelAck {
    bool accepted = false;                           ///< 请求是否被受理（非"取消是否完成"——后者经状态/事件观察）
    std::optional<core::DiagnosticRecord> feedback;  ///< 受理失败时的显式反馈；正常受理为空（UX-03）
};

/**
 * @brief 终止类请求应答（§10.2 StatusAck 同形——本任务用于
 *        requestForceTerminate；requestPause/requestResume 的同形应答随
 *        EX-T05 完整接口落位）。
 */
struct StatusAck {
    bool accepted = false;                           ///< 请求是否被受理/执行
    core::TaskState currentState = core::TaskState::Queued;  ///< 应答时的任务状态
    std::optional<core::DiagnosticRecord> feedback;  ///< 显式反馈（可预期拒绝时非空）
};

// =====================================================================
// ICancelSignal——取消信号最小接口（§3.1 Controller.hpp 组成行）
// =====================================================================

/**
 * @brief 协作点轮询的取消信号查询接口（§7.1 信号传递汇总的查询端）。
 *
 * 信号传递链（§7.1）：控制请求→调度线程→通道 CancelRequest→worker 宿主
 * 置取消标志→评估器/策略/编译**经 ICancelSignal 适配的各令牌**在协作点
 * （批次/检查点边界）观测。本接口是"各令牌"的最小公共形状：宿主实现
 * （worker 侧标志，EX-T06）与 L5 适配器（runtime/policy 令牌，ARCH §3.3
 * 注入边界）各自实现它；查询方（评估器循环）只依赖此接口。
 *
 * 实现要求：cancellationRequested() 一经置位**不得回退**（取消是单向
 * 决定——复位语义未在卡内定义，实现方自造复位会造成协作点漏判）；查询
 * 须无阻塞（协作点在批次边界高频调用）。
 */
class ICancelSignal {
public:
    virtual ~ICancelSignal() = default;

    /// 当前是否已请求取消（查询须无阻塞；置位后不回退——类注释）。
    virtual bool cancellationRequested() const = 0;
};

// =====================================================================
// IWorkerHandle——单 worker 生命周期操作接缝（§10.3 IWorkerSupervisor 的
// 单任务视口；EX-T06 WorkerSupervisor/JobScope 适配实现）
// =====================================================================

/**
 * @brief 协议编排层眼中的"一个在途 worker"（进程树的操作面）。
 *
 * 为什么是接缝而不是直接调用 Win32：取消协议（EX-T03）只关心 worker 的
 * **行为语义**（协作信号、批次收敛、进程树终止、存活探测），不关心进程
 * 与通道的实现（Job 对象/命名管道——EX-T06）；接缝同时是故障注入点
 * （§11 设施：测试替身 FakeWorker 实现本接口驱动协议时序断言）。
 *
 * 所有权：实现实例由 worker 池（EX-T06 supervisor）持有；TaskController
 * 只存裸指针、不接管所有权（bindWorker 前置：指针存活期覆盖任务在途期，
 * 解绑经 unbindWorker——AGENTS §2.5 所有权标注纪律）。
 *
 * 线程约束：本接口的调用点全部在调度线程 poll() 内（§10.3"调度线程
 * 调用"同源）；实现方无须再加锁。
 */
class IWorkerHandle {
public:
    virtual ~IWorkerHandle() = default;

    /// 发送通道 CancelRequest（§7.1 步 2——worker 宿主置取消标志；对同一
    /// 协作窗内重复调用无意义，编排层保证每窗恰发一次）。
    virtual void requestCooperativeCancel() = 0;

    /// 协作窗观测：worker 已对取消请求应答（CancelAck）或在途批次已收敛
    /// （§7.1 步 2"批次 10 s 内自然结束→CancelAck→Canceled"的观测端）。
    virtual bool cancelSettled() const = 0;

    /**
     * @brief 强制终止 worker 进程树（§10.3 terminateForce 原文语义）。
     *
     * 实现契约（EX-T06 JobScope 兑现，编排层据此注释锚定）：worker 进程
     * 绑定于带 JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE 标志的 Job 对象——
     * TerminateJobObject 终止进程树整体，随后关闭全部句柄（Job 句柄最后
     * 引用关闭时残留进程亦被 KILL_ON_JOB_CLOSE 兜杀，不留孤儿）；终止后
     * isAlive() 须返回 false。cause 仅承载语义标记（ForceTerminated）。
     */
    virtual void terminateForce(TerminationCause cause) = 0;

    /// 进程存活探测（EX-WKR-4 观测点"进程存活探测"；终止后恒 false）。
    virtual bool isAlive() const = 0;
};

// =====================================================================
// IDispatchGate——派发停止接缝（§7.1 步 1"停止派发新批次"的编排面）
// =====================================================================

/**
 * @brief 任务进入 Canceling 时回调的派发闸门（调度器侧实现）。
 *
 * 背景：§7.1 步 1 要求取消请求 2 s 内"进入 Canceling **并停止派发新
 * 批次**"。状态位 Canceling 本身就是派发层的禁行条件（调度器派发前查
 * 询状态——EX-T05），本接缝把"停止派发"做成显式回调而非隐式约定：
 * 调度器（EX-T05）实现本接口把任务移出派发就绪集，协议编排只负责在
 * 正确的时机回调——两侧职责都可在测试中断言。
 *
 * 线程约束：回调发生在调度线程 poll() 内。
 */
class IDispatchGate {
public:
    virtual ~IDispatchGate() = default;

    /// 任务进入 Canceling（协作窗开启）即回调；调度器应使该任务不再派发
    /// 任何新批次（§7.1 步 1；EX-SM-3 观测点"停止派发批次"）。
    virtual void stopDispatch(TaskId task) = 0;
};

// =====================================================================
// TaskController——取消与终止协议编排（本头核心交付）
// =====================================================================

/**
 * @brief 取消与终止协议的编排引擎（§7.1 逐步协议的可执行体）。
 *
 * 生命周期：由调度器（EX-T05）/测试装配创建；受管任务经 attachTask
 * 登记（状态机实例所有权移入本表——终态保留窗口内可查询，releaseResources
 * 回收）；poll() 由调度线程周期调用推进协议。
 *
 * 协议时序（全部经注入时钟度量——ManualClock 可虚拟推进，无真实等待）：
 *   1. **命令优先**：每次 poll 第一段清空命令通道并立即生效——取消请求
 *      的生效延迟上界＝poll 调用周期（§7.1 步 1 的 2 s 由调度器 poll 周期
 *      ≤2 s 保证；协议侧不做任何超过一个 poll 的拖延）；
 *   2. **协作窗**：Running 任务取消进入 Canceling 时，向 worker 恰发一次
 *      CancelRequest 并开启 10 s 收敛窗（自进入 Canceling 起度量——
 *      §7.1 步 2"普通批次 10 s 内自然结束"的窗起点；ARCH §4.4 条 1→2
 *      的次序即先入 Canceling 后等待收敛）；
 *   3. **收敛**：worker cancelSettled()→T4 CancelSettled→Canceled（无
 *      worker 的取消——Queued/Preparing/Paused——无在途批次，同 poll 内
 *      直达 Canceled：T3/T6/T12 后立即 T4，§5.3 各行"无在途批次"注）；
 *   4. **超时强杀**：收敛窗（10 s）耗尽仍未收敛→terminateForce 进程树
 *      终止→T13 CancelTimeoutForceKill→Failed＋EX-FORCE-TERMINATED
 *      （不伪装普通失败；检查点保留）——**取消协议无永久等待路径**的
 *      兜底臂（acceptance 2 自审项）；
 *   5. **运行超时**（EX-WKR-5）：声明了 evaluationTimeout 的任务在 Running
 *      相超过时限→走与卡死相同的强杀路径（terminateForce→T13），并在
 *      T13 的 EX-FORCE-TERMINATED 之前写入 EX-WORKER-HUNG 判定诊断、
 *      cause 携带运行超时标记与心跳失联区分（§7.1"同卡死路径"）；
 *   6. **直接强杀**（requestForceTerminate，EX-WKR-4）：对有活动 worker
 *      的任务＝立即进入上述强杀序列（事件序 Running→Canceling→Failed，
 *      矩阵内合法两步——强杀必经 T13 的 ForceTerminated 写入点）；
 *      对无 worker 任务（Queued/Preparing/Paused——无进程树可终止）
 *      ＝出队/直达取消（T3/T6/T12＋T4→Canceled；§7.4"强杀归任务操作"
 *      在矩阵内的等效表达：无进程之强杀即取消）。
 *
 * 错误语义（§10.8 两分法）：
 *   - 未知 TaskId 的控制请求＝调用方契约违约→ExecutionError(InvalidState)
 *     fail-fast（任务存在性是调用方的前置知识——EX-T05 调度器登记面）；
 *   - 终态任务的取消/强杀＝可预期条件→结构化 Ack（accepted=false＋
 *     反馈 / 幂等 no-op），不抛。
 *
 * 线程约束：见文件头注释（命令任意线程；poll/登记/查询仅调度线程）。
 */
class TaskController {
public:
    /// 注入时钟（testkit §6.6：消费者单元定义接口——生产默认
    /// steady_clock::now，测试注入 ManualClock::now；返回值仅在本类内部
    /// 作时长度量，无跨时钟比较）。
    using ClockFn = std::function<std::chrono::steady_clock::time_point()>;

    /// 协议常量（上游需求值——**不提供运行期修改点**，NFR-PERF-02/
    /// ARCH §4.4"协议值 2 s/10 s 为上游需求值不改动"）。
    static constexpr std::chrono::milliseconds kCancelAcceptWindow{2000};        ///< 取消请求 2 s 内生效（NFR-PERF-02 上游值）
    static constexpr std::chrono::milliseconds kCancelCooperativeWindow{10000};  ///< 在途批次 10 s 收敛窗（NFR-PERF-02 上游值）

    /// 实现参数（D-07——非上游阈值，登记于单元卡 §15.1）。
    struct Config {
        /// 终态任务资源保留窗口（D-07 默认 30 min；releaseResources 的
        /// 过窗判定基准）。
        std::chrono::milliseconds terminalRetention{std::chrono::minutes{30}};
    };

    /**
     * @brief 构造（时钟注入点）。
     *
     * @param config [in] 实现参数（缺省＝D-07 默认）
     * @param clock  [in] 时钟函数；空函数对象＝退回 steady_clock::now
     *               （缺省构造场景——生产装配通常不注入）
     */
    explicit TaskController(Config config = {}, ClockFn clock = nullptr);

    /// 析构：受管状态机实例随表回收（终态任务的内存态——磁盘归档不动，
    /// §10.2 releaseResources 注）。
    ~TaskController();

    TaskController(const TaskController&) = delete;             ///< 单写者语义不可拷贝
    TaskController& operator=(const TaskController&) = delete;  ///< 同上

    // ---- 受管任务登记（仅调度线程） ----

    /**
     * @brief 登记受管任务（状态机实例所有权移入本表）。
     *
     * @param machine [in] 状态机实例（T1 受理或恢复重建产物；调用方以
     *                unique_ptr 交接所有权）
     * @throws ExecutionError(InvalidState) 同一 taskId 重复登记（调度器
     *         登记面违约——TaskId 全局唯一，§4.2 主键行）
     */
    void attachTask(std::unique_ptr<TaskStateMachine> machine);

    /**
     * @brief 绑定任务的活动 worker（非所有权——实现实例归 worker 池）。
     *
     * 派发握手成功（T5）后由调度器调用；取消/强杀编排经此操作进程树。
     * 终态后调度器负责 unbindWorker（或直接复用 releaseResources 时点）。
     *
     * @param worker [in] worker 句柄；nullptr＝解绑（任务转入无 worker
     *               形态——如暂停后 worker 回收，§7.2）
     * @throws ExecutionError(InvalidState) 任务未登记
     */
    void bindWorker(TaskId task, IWorkerHandle* worker);

    /// 登记派发闸门（非所有权；可为 nullptr＝无消费者——裸装配场景）。
    void setDispatchGate(IDispatchGate* gate) noexcept;

    // ---- 控制命令（任意线程——命令通道；实际动作由 poll 执行） ----

    /**
     * @brief 请求取消任务（§10.2 requestCancel；任意线程）。
     *
     * 受理判定基于入队时刻的状态速览（竞态窗口说明：速览非终态而 poll
     * 执行时已终态的边缘序列下，命令被无害丢弃——取消对终态任务本为
     * no-op，协议结果不受影响）。正常受理不产生任何诊断（UX-03）。
     */
    CancelAck requestCancel(TaskId task);

    /**
     * @brief 强制终止任务（§10.2 requestForceTerminate；任意线程）。
     *
     * 编排语义见类注释第 6 条（有 worker＝强杀序列；无 worker＝取消
     * 直达；终态任务＝幂等 no-op，accepted=true 不重复动作）。
     */
    StatusAck requestForceTerminate(TaskId task);

    // ---- 查询（仅调度线程——状态机唯一写者域） ----

    /// 最近进度投影（未登记/无进度→nullopt；§10.2 progress）。
    std::optional<ProgressReport> progress(TaskId task) const noexcept;

    /// 当前状态查询（未登记→nullopt；排空编排与测试的状态观测面）。
    std::optional<core::TaskState> tryState(TaskId task) const noexcept;

    /// 受管任务数（含终态保留期——排空/测试断言用）。
    std::size_t taskCount() const noexcept;

    /**
     * @brief 遍历受管任务（仅调度线程；排空编排 requestCancelAll/
     *        abandonAllForced 的清单来源）。
     *
     * @param fn [in] 访问器（taskId, 状态机可变引用——编排可直接驱动
     *                转移与读记录；遍历中增删任务未定义）
     */
    void forEachTask(const std::function<void(TaskId, TaskStateMachine&)>& fn);

    // ---- 协议驱动（仅调度线程——EX-T05 调度循环周期调用） ----

    /**
     * @brief 推进协议一个节拍（幂等；可重入）。
     *
     * 处理序（§7.1 的编排展开）：
     *   a. 命令段：清空命令通道，逐条执行受理动作（取消→进入 Canceling
     *      ＋停派发回调＋协作信号；强杀→强杀序列）——取消命令优先于
     *      其他一切推进（"插队于派发之前"）；
     *   b. 协作窗段：Canceling 任务先查收敛（settled→T4），再查超时
     *      （窗耗尽→强杀序列→T13）——收敛优先于超时，窗边界上"恰好
     *      收敛"按收敛处理；
     *   c. 运行超时段：Running 且声明时限的任务超时→强杀序列（EX-WKR-5）；
     *   d. 时点维护：按转移后状态刷新 Canceling/Running/终态时点与状态
     *      速览（供任意线程的受理判定）。
     * 时钟取自注入 ClockFn——测试以 ManualClock::advance 虚拟推进后调
     * 用本方法断言时序（不 sleep，testkit §6.5）。
     */
    void poll();

    // ---- 终态资源回收（仅调度线程——§10.2 releaseResources） ----

    /**
     * @brief 释放终态任务的内存态（状态机实例出表——磁盘归档不动）。
     *
     * @throws ExecutionError(InvalidState) 任务未登记；任务非终态；
     *         终态但未过保留窗口（Config.terminalRetention）——三者均为
     *         调用方违约（§10.2 前置"任务处于终态且过保留窗口策略"）
     */
    void releaseResources(TaskId task);

    /// 生产默认时钟（ClockFn 缺省值的实际实现——steady 时钟，单调）。
    static std::chrono::steady_clock::time_point steadyClock() noexcept;

private:
    /// 受管任务的编排侧状态（时点与防线标记——全部仅调度线程访问）。
    struct ManagedTask {
        std::unique_ptr<TaskStateMachine> machine;   ///< 状态机本体（所有权在表）
        IWorkerHandle* worker = nullptr;             ///< 活动 worker（非所有权；可空＝无 worker 形态）
        std::optional<std::chrono::steady_clock::time_point> cancelEnteredAt;  ///< 进入 Canceling 时刻（10 s 收敛窗起点）
        std::optional<std::chrono::steady_clock::time_point> runStartedAt;     ///< 进入 Running 时刻（evaluationTimeout 起点）
        std::optional<std::chrono::steady_clock::time_point> terminalAt;       ///< 观测到终态的时刻（保留窗起点）
        bool cooperativeCancelSent = false;          ///< 本协作窗已发 CancelRequest（恰一次防线）
        bool forceKillIssued = false;                ///< 已执行 terminateForce（恰一次防线——重复强杀 no-op）
    };

    /// 命令通道条目（入队时刻随行——2 s 生效窗的度量依据）。
    struct ControlCommand {
        TaskId task;
        std::chrono::steady_clock::time_point requestedAt;
        bool force = false;   ///< false＝requestCancel；true＝requestForceTerminate
    };

    /// 时钟读取（注入空→steadyClock）。
    std::chrono::steady_clock::time_point now() const noexcept;

    /// 命令执行（poll 命令段——调度线程域；nowTp＝本拍生效时刻，协作窗
    /// 起点与强杀序列的时点基准）。
    void executeCancel(ManagedTask& entry, const ControlCommand& cmd,
                       std::chrono::steady_clock::time_point nowTp);
    void executeForceTerminate(ManagedTask& entry, const ControlCommand& cmd,
                               std::chrono::steady_clock::time_point nowTp);

    /// 强杀序列（收敛超时/运行超时/直接强杀共用——类注释第 4/5/6 条）：
    /// terminateForce 恰一次→（运行超时时先写 EX-WORKER-HUNG 判定诊断）→
    /// T13 转移→Failed(EX-FORCE-TERMINATED)。前置：entry 处于 Canceling。
    void forceKillSequence(ManagedTask& entry,
                           std::chrono::steady_clock::time_point now,
                           bool runTimeout);

    /// 无 worker 任务的取消收敛（T3/T6/T12 后同拍 T4——"无在途批次直达"）。
    void settleWithoutWorker(ManagedTask& entry);

    /// 进入 Canceling 的公共序（状态转移＋停派发回调＋协作信号＋时点）。
    void enterCanceling(ManagedTask& entry, std::chrono::steady_clock::time_point now);

    /// 刷新速览与时点（poll 尾段；终态时点只打一次）。
    void refreshTracking(ManagedTask& entry, std::chrono::steady_clock::time_point now);

    Config m_config;   ///< 实现参数（保留窗等）
    ClockFn m_clock;   ///< 注入时钟（空＝steady_clock）

    /// 命令通道＋状态速览的互斥（控制命令任意线程 ↔ poll 调度线程的
    /// 两个交汇点共用一把小粒度锁；状态机本体不在锁内——单写者域）。
    mutable std::mutex m_commandMutex;
    std::vector<ControlCommand> m_commands;  ///< 待处理控制命令（FIFO——同级到达序生效，§10.8）
    /// 状态速览（任意线程受理判定读；poll 尾段在锁内刷新——与真实状态
    /// 至多滞后一个 poll，竞态影响见 requestCancel 注）。
    std::unordered_map<TaskId, core::TaskState> m_stateView;

    /// 受管任务表（调度线程私有——唯一写者域，不加锁）。
    std::unordered_map<TaskId, ManagedTask> m_tasks;
    IDispatchGate* m_dispatchGate = nullptr;  ///< 派发闸门（非所有权；可空）
};

}  // namespace sdurws::ird::execution

#endif  // SDURWS_IRD_EXECUTION_CONTROLLER_HPP