/**
 * @file   WorkerSupervisor.hpp
 * @brief  worker 监督器——进程池（launch/握手/心跳/崩溃检测/退出码分类/
 *         池纪律）与单 worker 操作面（§6.4 工作进程模型、§10.3
 *         IWorkerSupervisor、§3.1 组成行）。
 *
 * 设计依据：
 *   - units/execution.md §3.1（WorkerSupervisor.hpp 组成行：WorkerId、
 *     WorkerStatus、WorkerAssignment、IWorkerSupervisor、WorkerLaunchResult、
 *     HeartbeatPolicy）、§6.4（worker 生命周期/退出码约定集/池纪律——
 *     "崩溃的 worker 永不回池"、空闲超时回收、临时目录隔离）、§7.4
 *     （卡死判定→强杀路径）、§10.3（接口原文）、§10.8（launch/
 *     terminateForce 仅调度线程；status/list 并发只读）
 *   - ARCHITECTURE.md §4.1（worker 自报身份不可信——以登记为准）、§4.3
 *     （崩溃只失败当前任务——NFR-REL-02）
 *   - 需求 NFR-REL-02（worker 崩溃隔离——主进程存活、其他任务不受影响）、
 *     NFR-PERF-02（超时路径——EX-WKR-3）、NFR-PERF-03（分批流式回传）
 *   - 任务契约 tasks/foundation/EX-T06.json acceptance 1（EX-WKR-1~3）、
 *     3（分批流式/临时目录隔离）、4（R-3 不超诺：退出码分类真进程实证）
 *
 * 背景说明（监督器在编排中的位置——不越权声明）：
 *   监督器只负责"进程与通道"这一层（§6.4 时序图的 launch→握手→派发→
 *   接收→分类）：它不理解任务状态机（转移归调度编排——T5/T9 由上层驱动）、
 *   不接触 RunRegistry/接纳（登记与九步归 EX-T04 的接口）、不写盘（一切
 *   持久化经 project 端口——PA-1）。它向编排方发布 WorkerEvent 事件流，
 *   编排方（调度器/L5/契约测试）据事件驱动状态机与接纳。这一刀切在
 *   "通道消息→结构化事件"处：事件之上是任务语义，事件之下是进程语义。
 *
 * 线程模型（§6.2"通道读线程"的落位）：
 *   - launch/poll/requestCooperativeCancel/terminateForce/releaseAll：
 *     **仅调度线程**（§10.3/§10.8 原文）；
 *   - 每个 worker 一条读线程（构造时启动、退出确认后收尾）：有界等待读
 *     管道＋监视进程句柄，原始字节/退出记录入收件箱——poll 在调度线程
 *     消化（序控/重组/心跳时戳/分类判定全部在调度线程，状态机纪律不破）；
 *   - status/list：**并发只读**（互斥保护的快照拷贝——§10.3 原文注，
 *     ResourceController 汇总消费〔EX-T07〕可从任意线程调用）。
 *
 * 时钟：hung 判定与空闲回收的时长度量全部经注入 ClockFn（测试 ManualClock
 *   虚拟推进——EX-WKR-3 不 sleep；testkit §6.5 纪律）。
 */

#ifndef SDURWS_IRD_EXECUTION_WORKERSUPERVISOR_HPP
#define SDURWS_IRD_EXECUTION_WORKERSUPERVISOR_HPP

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/execution/ChannelProtocol.hpp>  // ChannelFrame/FrameSequencer/MessageReassembler（协议本体）
#include <sdurws/ird/execution/Controller.hpp>      // IWorkerHandle（TaskController 绑定面）
#include <sdurws/ird/execution/Ports.hpp>           // MaterializedDispatch（§3.3 值传递——P-EX-3）
#include <sdurws/ird/execution/Scheduler.hpp>       // ResourceBudget（workerShare 承载）
#include <sdurws/ird/execution/TaskTypes.hpp>       // CheckpointId/TerminationCause

namespace sdurws::ird::execution {

// =====================================================================
// WorkerId / HeartbeatPolicy（§3.1 组成行）
// =====================================================================

/**
 * @brief worker 实例标识（监督器内自增分配；0＝保留值）。
 *
 * 进程可复用而 WorkerId 恒新：池化复用的是"进程"，每次派发绑定仍是同一
 *   worker 记录——WorkerId 跟随 worker 记录（§6.4 池化语义：进程保活，
 *   跨任务复用）。崩溃销毁后新任务新进程新 WorkerId。
 */
struct WorkerId {
    std::uint64_t value = 0;  ///< ≥1 合法；0＝空（保留值）

    bool isValid() const noexcept { return value != 0; }
    bool operator==(const WorkerId& o) const noexcept { return value == o.value; }
    bool operator!=(const WorkerId& o) const noexcept { return value != o.value; }
};

/**
 * @brief 心跳策略（§6.5 心跳行——实现参数 D-06，**非上游需求值**；
 *        EX-WKR-3 契约明文要求"5 s×3 失联阈值为实现参数非需求值"）。
 *
 * hung 判定窗＝interval × lossThresholdIntervals：连续该数目个间隔无心跳
 *   且管道无数据→WorkerHung 判定（§7.4；诊断以 EX-WORKER-HUNG 出具，
 *   与运行超时〔EX-WKR-5 cause 标记〕区分——诊断码区分）。
 */
struct HeartbeatPolicy {
    /// 心跳间隔（D-06 默认 5 s——worker 宿主按 HelloAck 下发的同值发送）。
    std::chrono::milliseconds interval{5000};
    /// 失联阈值＝连续无心跳的间隔数（D-06 默认 3）。
    std::uint32_t lossThresholdIntervals = 3;

    /// hung 判定窗（两参数的乘积——调度侧判定基准）。
    std::chrono::milliseconds hungWindow() const noexcept
    {
        return interval * static_cast<std::int64_t>(lossThresholdIntervals);
    }
};

// =====================================================================
// WorkerStatus / WorkerAssignment / WorkerLaunchResult（§10.3 原文形态）
// =====================================================================

/**
 * @brief 单 worker 状态投影（§10.3 原文字段；并发只读——深拷贝发布）。
 */
struct WorkerStatus {
    /// 生命周期相位（§10.3 原文五值）。
    enum class Phase {
        Idle,      ///< 池中空闲（可复用——§6.4 池化）
        Booting,   ///< 已启动、握手中
        Running,   ///< 派发已受理、执行中
        Draining,  ///< 已请求退出（空闲回收/关闭——等待自然退出）
        Dead,      ///< 已终结（崩溃/强杀/拒绝——**永不回池**，§6.4）
    };

    WorkerId id;                                  ///< 实例标识
    std::uint32_t pid = 0;                        ///< 主进程 PID（观测面/临时目录名成分）
    core::RunId currentRun;                       ///< 当前绑定运行（Idle 期为上一任务残留值——仅 Running 相有义）
    Phase phase = Phase::Booting;                 ///< 相位
    std::chrono::system_clock::time_point lastHeartbeatUtc{};  ///< 最近心跳（UTC 墙钟——观测面，不参与判定；判定用注入时钟的 steady 值）
    std::uint64_t tasksServed = 0;                ///< 累计服务任务数（池化复用计数）
};

/**
 * @brief 一次 worker 派发绑定（§10.3 原文字段＋manifest 摘要增量）。
 *
 * 增量登记（单元卡 §15.4）：§10.3 原文字段无握手比对值，而 §6.4 握手
 *   要求"协议版本＋评估器 manifest 摘要比对，不一致→拒绝派发"——比对值
 *   （主进程侧期望摘要，装配清单派生）必须随绑定携带，故增 manifestDigest
 *   字段（口径同 EX-T03 给 TaskCapability 增 evaluationTimeout 的先例）。
 *
 * dispatch 为 §3.3 MaterializedDispatch 值传递（P-EX-3 处置：物化字节由
 *   IExecutionModelService 注入适配产出，execution 只透传不解析——
 *   acceptance 4）。
 */
struct WorkerAssignment {
    core::TaskIdentity identity;                       ///< 本派发五元组（帧头身份块来源）
    evidence::EvaluationKey evaluatorKey;              ///< 评估键（透传装配清单）
    MaterializedDispatch dispatch;                     ///< 派发物（物化字节＋两份内容身份）
    HeartbeatPolicy heartbeat;                         ///< 心跳策略（HelloAck 下发 worker 同值）
    std::optional<CheckpointId> resumeFrom;            ///< 续跑检查点（nullopt＝全新——EX-T08 消费）
    ResourceBudget workerShare;                        ///< 本 worker 资源份额（EX-T07 治理消费）
    std::string manifestDigest;                        ///< 期望的评估器装配 manifest 摘要（握手比对值）
};

/**
 * @brief 启动结果（§10.3 原文形态；ok=false 时 worker 无效——调度器据以
 *        Preparing→Failed，EX-WORKER-LAUNCH-FAILED）。
 */
struct WorkerLaunchResult {
    bool ok = false;                                   ///< 启动＋Job Scope＋握手链路是否发起成功
    WorkerId worker;                                   ///< 实例标识（ok=true 时有效）
    std::vector<core::DiagnosticRecord> diagnostics;   ///< 启动失败诊断（ok=false 时非空）
};

// =====================================================================
// WorkerEvent / IWorkerEventSink（监督器→编排方的事件流——进程层边界）
// =====================================================================

/// 进程退出分类（§6.4 退出码约定集的执行面——EX-WKR-2/3 的判定载体）。
enum class ExitClassification {
    NormalCompletion,            ///< 0 正常终结（FinalOutput/CancelAck/PauseAck 已发——按通道最后消息走 T8/T4/T10）
    LaunchFailed,                ///< 10 启动失败（依赖/装配错误）——Preparing→Failed
    HostInternalError,           ///< 11 宿主内部错误（协议/装载错）
    EvaluatorFailed,             ///< 12 评估器失败（ErrorReport 已先行——域诊断透传）
    CancelAcknowledged,          ///< 20 协作取消确认——T4（Canceled）
    PauseAcknowledged,           ///< 21 暂停确认——T10（Paused）
    ForceTerminatedBySupervisor, ///< 监督方强杀（§7.4 强杀路径——EX-FORCE-TERMINATED）
    Crashed,                     ///< 约定集之外的任何值（含 0xC0000005 异常码族）——EX-WORKER-CRASHED（NFR-REL-02）
};

/**
 * @brief 退出码分类（§6.4 退出码约定集表的纯函数化——真进程用例的判定
 *        本体，独立可测）。
 *
 * @param exitCode               [in] GetExitCodeProcess 观测的退出码
 * @param terminatedBySupervisor [in] 是否监督方强杀（terminateForce 已发——
 *                               分类优先于退出码表：强杀路径的退出码是
 *                               TerminateJobObject 的参数值，不代表
 *                               worker 自身行为）
 * @return 分类（§6.4 表逐行——约定集 {0,10,11,12,20,21}；其余一律
 *         Crashed，含 Windows 异常码族 0xC0000005 等）
 */
ExitClassification classifyWorkerExitCode(std::uint32_t exitCode,
                                          bool terminatedBySupervisor) noexcept;

/**
 * @brief 监督器事件（通道消息→结构化事件的唯一出口；编排方〔调度器/
 *        契约测试〕据此驱动状态机与接纳——见类注释"不越权声明"）。
 */
struct WorkerEvent {
    /// 事件类别（与通道消息一一对应＋监督器自身的判定类事件）。
    enum class Kind {
        HandshakeRejected,    ///< 握手拒绝（manifest/版本失配→拒绝派发——编排使 Preparing→Failed，§6.4）
        DispatchAccepted,     ///< worker 身份核对通过（DispatchAccept——观测面）
        Progress,             ///< 进度帧（经 §6.1 节流的编排面在调度器——此处原样上报）
        ResultBatch,          ///< 结果批次（流式逐批——NFR-PERF-03；接纳面＝登记核对面）
        CheckpointBatch,      ///< 检查点批次（透传——持久化编排归 EX-T08，PA-1 不伪造第二路径）
        FinalOutput,          ///< 最终产出（九步接纳的到达面——envelope 载荷）
        ErrorReport,          ///< worker 诊断回传（稳定码或开发通道）
        ProtocolViolation,    ///< 通道协议错误（EX-CHANNEL-PROTOCOL-ERROR；fatal=true→尝试 Failed，EX-CHN-1）
        WorkerHung,           ///< 心跳失联判定（EX-WORKER-HUNG——其后监督器自动走强杀路径，§7.4）
        CancelAck,            ///< 取消确认（协作窗收敛观测——§7.1 步 2）
        PauseAck,             ///< 暂停确认（§7.2）
        WorkerExited,         ///< 进程退出（分类＋退出码——编排方按 §6.4 表走 T8/T4/T10/T9）
    };

    Kind kind = Kind::WorkerExited;  ///< 类别
    WorkerId worker;                 ///< 实例
    core::TaskIdentity identity;     ///< 事件绑定五元组（帧头原值；Exited 时＝绑定值）
    /// 进度载荷（Progress）。
    std::optional<ProgressReport> progress;
    /// 批次号（ResultBatch——worker 内单调 ≥1）。
    std::uint64_t batchIndex = 0;
    /// 检查点序号（CheckpointBatch——≥1，CheckpointId.sequence 语义）。
    std::uint64_t checkpointSequence = 0;
    /// 载荷字节（ResultBatch/CheckpointBatch＝批次字节；FinalOutput＝
    /// EvaluationOutput canonical——绑定的自报身份已在通道层核对）。
    std::vector<std::uint8_t> bytes;
    /// 稳定码/开发通道名（ErrorReport/ProtocolViolation/WorkerHung——
    /// EX-\* 稳定码 token 或 dev 通道名）。
    std::string stableCode;
    /// 明细（开发诊断文本）。
    std::string detail;
    /// 协议错误是否致命（ProtocolViolation：false＝重复帧丢弃＋开发诊断
    /// ——继续；true＝断裂/非法帧——尝试 Failed，§6.5）。
    bool fatal = false;
    /// 退出码（WorkerExited——GetExitCodeProcess 观测值）。
    std::uint32_t exitCode = 0;
    /// 退出分类（WorkerExited）。
    ExitClassification exit = ExitClassification::Crashed;
};

/// 事件接缝（回调发生在调度线程 poll 内——编排方无须加锁；可实现方自行
/// 保证自身数据结构的线程纪律）。
class IWorkerEventSink {
public:
    virtual ~IWorkerEventSink() = default;
    virtual void onWorkerEvent(const WorkerEvent& event) = 0;
};

// =====================================================================
// IWorkerSupervisor（§10.3 接口原文）
// =====================================================================

/**
 * @brief worker 池监督接口（§10.3 原文——前置注：进程可执行可达〔与主
 *        进程同版本基线——部署同目录，NFR-DEP-02〕；预算允许。后置注：
 *        进程启动＋Job Scope 绑定＋握手完成〔manifest 摘要一致〕；失败
 *        →ok=false〔Preparing→Failed 由调度器处置〕）。
 *
 * 线程约束（§10.3/§10.8）：launch/requestCooperativeCancel/terminateForce
 *   仅调度线程；status/list 并发只读。
 */
class IWorkerSupervisor {
public:
    virtual ~IWorkerSupervisor() = default;

    /// 启动（池取或新建进程）并派发绑定（握手在 poll 内完成——本调用只
    /// 保证"进程已启动、通道已建立"；握手是异步协议，§6.4 时序图）。
    virtual WorkerLaunchResult launch(const WorkerAssignment& assignment) = 0;

    /// 发送通道 CancelRequest（§7.1 步 2——worker 宿主置取消标志）。
    virtual void requestCooperativeCancel(WorkerId worker) = 0;

    /// 强制终止进程树（TerminateJobObject；句柄关闭＋reap——§10.3 原文；
    /// cause 仅承载语义标记，分类面以 ForceTerminatedBySupervisor 出具）。
    virtual void terminateForce(WorkerId worker, TerminationCause cause) = 0;

    /// 单 worker 状态（未知 id→phase=Dead 的空投影——并发只读快照）。
    virtual WorkerStatus status(WorkerId worker) const = 0;

    /// 全体 worker 状态快照（ResourceController 汇总消费——EX-T07）。
    virtual std::vector<WorkerStatus> list() const = 0;
};

// =====================================================================
// WorkerSupervisor——实现本体
// =====================================================================

/**
 * @brief worker 池监督器实现（IWorkerSupervisor 本体——§6.4 生命周期的
 *        可执行体）。
 *
 * 生命周期：调度器/测试装配创建（随排空释放）；每个 launch 产生一条
 *   worker 记录（进程＋通道＋读线程），记录随池纪律演进：Running→
 *   （正常退出）Idle→复用或回收 / （异常）Dead（记录保留至本类析构——
 *   Dead 记录保留进程退出码等观测面；**绝不**回到 Idle——"崩溃的 worker
 *   永不回池"的结构保证）。
 *
 * poll 职责（调度线程显式驱动——与 TaskController 同模型，EX-T03 先例）：
 *   ①消化读线程收件箱（帧解码→seq 序控→重组→按类型出事件——EX-CHN-1
 *   的重排/去重/断裂判定在此）；②握手协议推进（Hello 比对→HelloAck→
 *   DispatchRequest 分帧派发）；③心跳失联判定（hungWindow——EX-WKR-3）
 *   与自动强杀；④退出记录分类（§6.4 约定集表）与池纪律落实；⑤空闲
 *   超时回收（Shutdown 请求）。
 *
 * 临时目录隔离（§6.4/§6.6——acceptance 3）：目录由 **worker 宿主** 创建
 *   （%TEMP%\ird-worker-<pid>-<run>-<attempt>，每尝试独立）——监督器不
 *   接触 worker 文件系统面（边界规则：worker 自身仅写自身临时目录；
 *   主进程不代管），监督器的义务是把五元组随帧携带（临时目录名的 run/
 *   attempt 成分由此而来）并在诊断通道回传清理失败（EX-ARC-3 开发诊断）。
 *
 * 线程约束：见文件头（调度线程写域＋每 worker 一条读线程）。
 */
class WorkerSupervisor final : public IWorkerSupervisor {
public:
    /// 时钟注入形态（steady 域——hung/空闲度量；空＝steady_clock::now）。
    using ClockFn = std::function<std::chrono::steady_clock::time_point()>;

    /// 实现参数（登记单元卡 §15.4——均非上游值）。
    struct Config {
        /// 池中空闲 worker 的保活上限（§6.4"空闲超时回收——默认 120 s，
        /// 实现参数"；0＝禁用回收）。到期发 Shutdown 请求→Draining→
        /// 自然退出后销毁记录。
        std::chrono::milliseconds idleRecycleAfter{120000};
        /// 池容量上限（Idle 相位记录的最大数；满则正常退出即销毁——
        /// 阶段 A 保守默认 2；内存预算治理归 EX-T07 ResourceController）。
        std::size_t poolCapacity = 2;
        /// 读线程有界等待片（单位 ms——poll 响应性与唤醒开销的折中；
        /// 25 ms 对任务级时序无观测影响）。
        std::uint32_t readerSliceMs = 25;
        /// 写命令有界等待（单位 ms——worker 卡死时 CancelRequest 必须能
        /// 超时返回，调度线程不可被拖死）。
        std::uint32_t commandTimeoutMs = 1000;
        /// 派发物分帧尺寸（单位字节——§6.5"大载荷分帧续传"；64 KiB 对
        /// 本地管道是吞吐/内存的常用折中，非上游值）。
        std::size_t dispatchFragmentBytes = 64 * 1024;
        /// 数据通道 seq 序控参数（窗口/缺口超时——§6.5"乱序与重复"；
        /// 缺省值见 FrameSequencer::Config——均实现参数非上游值；契约
        /// 测试以小缺口超时做断裂用例的有界实证）。
        FrameSequencer::Config channelSequencer{};
    };

    /**
     * @brief 构造。
     *
     * @param executablePath [in] worker 可执行完整路径（与主进程同目录
     *                       部署——NFR-DEP-02；契约测试经 CMake 注入路径）
     * @param config         [in] 实现参数（缺省＝上表默认）
     * @param clock          [in] 注入时钟（空＝steady_clock）
     */
    WorkerSupervisor(std::wstring executablePath, Config config = {}, ClockFn clock = nullptr);

    /// 析构：全部 worker 强杀（作业 RAII 兜杀）＋读线程收尾——不留孤儿
    /// 进程（KILL_ON_JOB_CLOSE 语义，§6.6）。
    ~WorkerSupervisor() override;

    WorkerSupervisor(const WorkerSupervisor&) = delete;
    WorkerSupervisor& operator=(const WorkerSupervisor&) = delete;

    // ---- 装配（仅调度线程，launch 前完成） ----

    /// 登记事件接缝（非所有权；nullptr＝无消费者——裸池场景）。
    void setEventSink(IWorkerEventSink* sink) noexcept;

    // ---- IWorkerSupervisor（§10.3） ----

    WorkerLaunchResult launch(const WorkerAssignment& assignment) override;
    void requestCooperativeCancel(WorkerId worker) override;
    void terminateForce(WorkerId worker, TerminationCause cause) override;
    WorkerStatus status(WorkerId worker) const override;
    std::vector<WorkerStatus> list() const override;

    // ---- 协议驱动与终结（仅调度线程） ----

    /**
     * @brief 推进一个节拍（幂等；TaskController::poll 同模型——调度循环
     *        周期调用）。处理序见类注释（收件箱→握手→失联判定→退出
     *        分类→空闲回收）。
     */
    void poll();

    /// 已登记 worker 数（含 Dead/Idle——仅调度线程〔测试断言在编排域〕；
    /// 任意线程的观测面走 list()——§10.3 线程约束行）。
    std::size_t workerCount() const noexcept;

    /**
     * @brief 全部活 worker 的作业提交内存峰值合计（EX-T07 增量——§6.6
     *        内存采样行 JobMemory 半区的监督器侧聚合；ResourceController
     *        生产采样源 SupervisorMemorySampler 的 worker 半区消费口）。
     *
     * 聚合口径：对每条**作业有效且进程未终结**的 worker 记录调
     *   MemProbe::queryJobPeakCommittedBytes（QueryInformationJobObject→
     *   PeakJobMemoryUsed——作业创建以来的提交内存峰值，单调不回落，
     *   见 MemProbe.hpp 不超诺声明）后求和；单条查询失败＝该条计 0
     *   （部分失败不毒化整体读数——"半读数比无读数危险"的关切由采样
     *   器层的整体失败通道承担，本聚合只做尽力求和）。Dead 记录（作业
     *   已随进程终结关闭）与空记录不参与。空池返回 0。
     *
     * 峰值口径与 §6.1"主进程＋全部 worker 合计峰值 ≤ 物理内存 70%"
     *   预算行对齐：池化复用的 worker 其峰值覆盖整个生命周期（不随任务
     *   结束回落）——治理侧按保守值判定，不会低估存量压力。
     *
     * 线程约束：**仅调度线程**（遍历 m_workers 记录域——与 launch/poll
     *   同域；ResourceController::evaluate 在 tick 主锁内调用本方法，
     *   域一致）。
     */
    std::uint64_t aggregateJobMemoryBytes() const;

    /**
     * @brief 立即回收全部空闲 worker（EX-T07 增量——§6.1 内存预算行
     *        "降低并行度（回收空闲 worker）"的池侧执行面；与既有空闲
     *        超时回收〔idleRecycleAfter——§6.4 实现参数〕同一触发语义，
     *        区别仅在触发源：内存压力即时触发 vs 超时触发）。
     *
     * 动作按 Idle 记录的进程存亡分流（阶段 A worker 宿主每任务终结即
     *   退出——约定码 0，回池记录的进程已不存在）：
     *   - 进程已终结（exitProcessed）的池槽：直接出清记录（与 poll 尾部
     *     销毁同序：join 读线程→关进程资源→双表 erase）——对死进程走
     *     Shutdown 协议永远等不到第二次退出确认，记录会滞留；
     *   - 进程仍存活的空闲 worker（§6.4"进程保活"模型）：置 Draining＋
     *     发 Shutdown（协作退出→自然退出→poll 收割——与 checkIdleRecycle
     *     同路径）。
     * 返回发起回收的条数。Boot/Running/Draining/Dead 记录不在处置面
     *   （在途 worker 是"并行度"本身，不由池侧回收——并行度的新增已被
     *   调度侧资源闸的停派发承载，两侧合起来才是 §6.1"停派发＋降并行"
     *   的完整执行面）。
     *
     * 线程约束：仅调度线程（记录域遍历＋状态锁内镜像更新——与 poll 同域；
     *   ResourceController 决策的消费方〔编排/L5/测试〕在调度域调用）。
     *
     * @return 本次发起回收的空闲 worker 数（0＝无可回收空闲）
     */
    std::size_t reclaimIdleWorkers();

    /**
     * @brief R-3 观测面：该 worker 的通道是否仍持有可继承句柄副本（§6.6
     *        进程创建行"句柄在主进程侧于启动后关闭继承副本（防泄漏）"
     *        的实证口——正常路径启动完成即恒 false；acceptance 4）。
     *
     * @param worker [in] 目标实例；未知/已释放→false（无副本即无泄漏）
     */
    bool hasOpenChildHandleDuplicates(WorkerId worker) const;

    /**
     * @brief 取 worker 的 TaskController 绑定面（IWorkerHandle 适配——
     *        EX-T03 取消协议的操作接缝；指针存活期＝本监督器存活期）。
     *
     * @param worker [in] 目标实例（须已登记——违约抛 ExecutionError(
     *               InvalidState)；未知 worker 是调用方登记面错误）
     */
    IWorkerHandle* workerHandle(WorkerId worker);

private:
    // ---- HandleAdapter 转发目标（调度线程域——记录表只读查询） ----
    bool workerCancelSettled(WorkerId worker) const;
    bool workerAlive(WorkerId worker) const;

    // ---- 内部记录（调度线程域——读线程只经收件箱与状态互斥交互；
    // WorkerRecord 及其收件箱条目的完整定义在实现文件——含 Win32 句柄
    // 类型，公共头不暴露平台面） ----

    /// 单 worker 记录（全部可变面仅调度线程访问；收件箱与状态快照除外）。
    struct WorkerRecord;

    // ---- launch 辅助（调度线程） ----
    WorkerLaunchResult spawnNewWorker(const WorkerAssignment& assignment);
    WorkerLaunchResult reuseIdleWorker(WorkerRecord& record, const WorkerAssignment& a);

    // ---- 派发/命令（调度线程——写命令通道） ----
    bool sendFrame(WorkerRecord& record, const ChannelFrame& frame) const;
    bool sendDispatchRequest(WorkerRecord& record);
    void sendShutdown(WorkerRecord& record);

    // ---- poll 段（调度线程） ----
    void drainInbox(WorkerRecord& record);
    void handleFrame(WorkerRecord& record, ChannelFrame&& frame);
    void deliverSequenced(WorkerRecord& record, std::vector<ChannelFrame>&& frames);
    void handleCompleteFrame(WorkerRecord& record, ChannelFrame&& frame);
    void handleHandshake(WorkerRecord& record, ChannelFrame&& frame);
    void processExit(WorkerRecord& record, std::uint32_t exitCode);
    void checkHeartbeatLoss(WorkerRecord& record,
                            std::chrono::steady_clock::time_point now);
    void checkIdleRecycle(WorkerRecord& record,
                          std::chrono::steady_clock::time_point now);
    void reapWorker(WorkerRecord& record);
    void emit(WorkerEvent event);

    /// 读线程主体（每 worker 一条——§6.2"通道读线程"）。
    void readerLoop(WorkerRecord& record);

    std::wstring m_executablePath;  ///< worker exe 路径（NFR-DEP-02 部署同目录）
    Config m_config;                ///< 实现参数
    ClockFn m_clock;                ///< 注入时钟（空＝steady）
    IWorkerEventSink* m_sink = nullptr;  ///< 事件接缝（非所有权；可空）

    WorkerId m_nextWorkerId{1};     ///< WorkerId 分配器（单调自增）

    /// 全体 worker 记录（调度线程唯一写者——launch/poll/析构同域；
    /// status/list 经状态互斥拷贝投影表，不触碰记录本体）。
    std::unordered_map<std::uint64_t, std::unique_ptr<WorkerRecord>> m_workers;
    /// 状态快照互斥（status/list 任意线程 ↔ poll 调度线程的交汇点）。
    mutable std::mutex m_statusMutex;
    /// 状态投影表（poll 在锁内刷新——任意线程 list()/status() 读同一锁
    /// 下的镜像快照；记录本体不出调度线程域）。键＝WorkerId.value。
    std::unordered_map<std::uint64_t, WorkerStatus> m_statusView;

    /// IWorkerHandle 适配器（每 worker 一份——TaskController 绑定面；
    /// 转发到本监督器对应记录的操作）。
    class HandleAdapter;
};

}  // namespace sdurws::ird::execution

#endif  // SDURWS_IRD_EXECUTION_WORKERSUPERVISOR_HPP
