/**
 * @file   StateMachine.hpp
 * @brief  任务状态机——九态逐转移表（§5.3 T1~T14）＋守卫＋事件发布接缝，
 *         公共可测（§5.2/§5.3、§3.1 StateMachine.hpp 组成行）。
 *
 * 设计依据：
 *   - units/execution.md §5.1（九态定义——词表归 core，本文零新增，
 *     P-EX-4）、§5.2（状态机图与转换矩阵——矩阵外禁止）、§5.3（逐转移
 *     表：触发/守卫/事件/清理）、§5.5（能力门控——暂停请求显式反馈）、
 *     §10.8（接口共性：调用方违约抛 ExecutionError；可预期失败经 Ack/
 *     诊断结构化）、§10.4（事件发布：TaskStatusChanged 每次转移）
 *   - 需求 TASK-01（状态机＋能力声明）、NFR-PERF-02（取消状态机侧——
 *     协作窗时序归 EX-T03）、NFR-REL-03（Interrupted 恢复期指派）、
 *     ARCH §11.2-6（取消矩阵覆盖 Queued/Preparing/Running/Paused 四态
 *     入口与暂停中取消的检查点保留）
 *   - 任务契约 tasks/foundation/EX-T02.json acceptance 1~5
 *
 * 背景说明（三条冻结规则，写代码前先读）：
 *   1. **九态零新增**（P-EX-4）：状态词表＝core::TaskState（core 词表，
 *      本单元零重定义）；不存在 Created/CancelRequested/Rejected 三态——
 *      提交受理前的拒绝发生在**状态机之外**（提交边界验证失败→结构化
 *      拒绝，不产生 TaskRecord、不占状态，§5.1 注）；"取消请求已受理"
 *      相由 Canceling 承载。
 *   2. **终态无出边**（§5.2）：Canceled/Completed/Failed/Interrupted 的
 *      任何出边请求均矩阵外非法（重跑＝新 TaskId，新身份链——§4.3）。
 *   3. **唯一写者**（§5.3 并发守卫总规则）：状态机由调度线程串行驱动，
 *      一切控制请求经命令队列串化；本类**非线程安全**（AGENTS §2.5
 *      线程约束标注），并发防线在调用方（EX-T05 调度线程）。
 *
 * 与后续任务的分工（本头只做"状态机本体"）：协作窗 2 s/10 s 时序、
 * 强杀进程树编排、归档端口调用（abandon/finalize）、worker 生命周期
 * 等清理动作的**执行**归 EX-T03/T04/T06——本状态机在 TransitionSpec
 * 中声明各行 §5.3 清理列的检查点保留语义（声明面），供编排层消费与
 * 测试逐行断言。
 *
 * 线程安全：除标注外非线程安全——仅任务所属调度线程访问（§5.3 并发
 * 守卫总规则；TASK-02 同源约束）。
 */

#ifndef SDURWS_IRD_EXECUTION_STATEMACHINE_HPP
#define SDURWS_IRD_EXECUTION_STATEMACHINE_HPP

#include <optional>
#include <vector>

#include <sdurws/ird/core/Evaluation.hpp>   // TaskState（九态词表归 core——零新增）
#include <sdurws/ird/core/Events.hpp>       // TaskStatusChangedPayload（⑤端口事件载荷）
#include <sdurws/ird/core/Identity.hpp>     // TaskIdentity（五元组值类型归 core）
#include <sdurws/ird/execution/Errors.hpp>
#include <sdurws/ird/execution/TaskTypes.hpp>

namespace sdurws::ird::execution {

// =====================================================================
// 转移触发与转移表（§5.2 矩阵＋§5.3 逐转移行的数据化表达）
// =====================================================================

/**
 * @brief 转移触发（§5.3 触发列的枚举化——每个运行期转移由恰好一个
 *        (触发， 源态) 二元组唯一确定目标态）。
 *
 * 各值注释＝对应 §5.3 行号；SubmitAccepted/RecoveryScanFound 是两条
 * "记录诞生"路径（T1 受理即 Queued——无 Created 态，P-EX-4；T14 恢复期
 * 重建指派），只经 forAcceptedSubmission/forRecoveryInterrupted 工厂
 * 走，不出现在 request() 运行期查表中（T14"不与运行期转移并发"——
 * §5.3 守卫列原文）。
 */
enum class TransitionTrigger {
    SubmitAccepted,         ///< T1  →Queued（submit 受理；工厂路径）
    DispatchDequeued,       ///< T2  Queued→Preparing（调度器出队）
    RequestCancel,          ///< T3/T6/Running→Canceling/T12（源态决定目标——四态取消入口）
    CancelSettled,          ///< T4  Canceling→Canceled（在途批次收敛或无在途）
    PrepareSucceeded,       ///< T5  Preparing→Running（预留＋握手成功）
    PrepareFailed,          ///< T7  Preparing→Failed（准备失败）
    PauseConfirmed,         ///< T10 Running→Paused（检查点写出并确认——暂停确认边界）
    ResumeConfirmed,        ///< T11 Paused→Running（兼容判定通过；新 AttemptId）
    RunCompleted,           ///< T8  Running→Completed（FinalOutput 接纳通过）
    RunFailed,              ///< T9  Running→Failed（崩溃/卡死/通道错误/评估器失败）
    CancelTimeoutForceKill, ///< T13 Canceling→Failed（超时强杀——EX-FORCE-TERMINATED）
    RecoveryScanFound,      ///< T14 →Interrupted（恢复期指派；工厂路径）
};

/// 检查点保留语义（§5.3 清理列中"最近检查点保留"的声明面——本状态机
/// 只声明语义，磁盘动作由编排层〔EX-T03 取消协议/EX-T04 归档协作〕执行；
/// EX-SM-4 断言暂停中取消路径携带 RetainLatest——ARCH §11.2-6 后半句）。
enum class CheckpointRetentionPolicy {
    NoCheckpointAction,      ///< 本转移无检查点动作（§5.3 清理列为"—"或非检查点项）
    RetainLatestCheckpoint,  ///< 保留最近检查点（可续跑——ARCH §4.3 A5/§4.4）
};

/**
 * @brief 逐转移表行（§5.3 表一行＝一个 TransitionSpec——表数据公开，
 *        测试逐行断言触发/守卫/事件/清理，§11 EX-SM 矩阵用例载体）。
 *
 * 值语义；表内容编译期固定（runtimeTable() 返回单例只读引用）。
 */
struct TransitionSpec {
    int row = 0;                 ///< §5.3 行号（T1=1…T14=14）；0＝无行号的转移（Running→Canceling——§5.3 表下散文"Running 取消传递"）
    TransitionTrigger trigger{}; ///< 触发（§5.3 触发列）
    /// 源态（§5.3 转移列箭头左端）；nullopt＝记录诞生路径（T1/T14——
    /// 无运行期源态，只经工厂进入，不出现在 request() 查表）
    std::optional<core::TaskState> source;
    core::TaskState target{};    ///< 目标态（§5.3 转移列箭头右端＝事件 newState）
    bool requiresSupportsPause = false;  ///< 能力守卫：仅 T10——不支持暂停→显式反馈 EX-CAPABILITY-UNSUPPORTED（EX-SM-7）
    CheckpointRetentionPolicy checkpointPolicy = CheckpointRetentionPolicy::NoCheckpointAction;  ///< 清理列检查点保留语义
    const char* cleanupNote = "";        ///< §5.3 清理列原文（开发可读性；磁盘/进程动作的执行归编排层任务）
};

/// 事件发布接缝（§12 EX-T02 行"事件发布接缝"；§10.4 的状态机侧半区）。
///
/// 每次成功转移（含工厂诞生）后由状态机**同步**调用 onTaskStatusChanged
/// ——单写者纪律下同任务事件序＝转移序（§4.3"同任务事件 FIFO"的保证
/// 点；EX-SUB-1 断言载体）。总线实现（core IDomainEventBus 的 execution
/// 侧参考实现 DomainEventBusImpl）归 EX-T05（§10.4），届时由适配器实现
/// 本接缝；EX-T02 测试用测试替身 sink 收集断言。实现方须自行保证线程
/// 安全约定（ sink 回调发生在调度线程——§10.8 线程约束行）。
class ITaskEventSink {
public:
    virtual ~ITaskEventSink() = default;
    /// 一次状态变更（§10.4：TaskStatusChanged{task:五元组, newState}；
    /// 进度不入领域事件——core.md D-09）。
    virtual void onTaskStatusChanged(const core::TaskStatusChangedPayload& payload) = 0;
};

/// 结构化转移应答（§10.2 StatusAck 同形：可预期拒绝经 Ack 反馈不抛——
/// §10.8 错误行；EX-SM-7 的观测点 StatusAck.feedback 即本结构 feedback）。
struct StateMachineAck {
    bool accepted = false;                           ///< true＝转移已生效（状态已变）；false＝可预期拒绝（状态不变）
    core::TaskState currentState = core::TaskState::Queued;  ///< 应答时状态（拒绝时＝原态——EX-SM-6/7"状态不变"断言面）
    std::optional<core::DiagnosticRecord> feedback;  ///< 显式反馈（EX-CAPABILITY-UNSUPPORTED 等；正常接受为空——UX-03 正常取消无错误诊断）
};

// =====================================================================
// TaskStateMachine——单任务状态机本体
// =====================================================================

/**
 * @brief 单任务九态状态机（转移表＋守卫＋事件接缝；§5.2/§5.3 的可执行体）。
 *
 * 实例生命周期：由两条工厂路径诞生（受理＝T1 / 恢复重建＝T14），此后
 * 经 request() 推进直至终态；终态后一切运行期触发均为矩阵外非法。
 * 一实例对应一 TaskRecord（记录随转移推进：state/attempt/termination/
 * diagnostics），record() 提供只读访问；并发只读投影经 TaskSnapshot
 * （调用方拷贝——§4.2 通用约定）。
 *
 * 错误语义（§10.8 两分法——本类最重要契约）：
 *   - 矩阵外非法转换（含终态出边、源态不匹配的触发）＝**调用方契约
 *     违约**→抛 ExecutionError(InvalidState)（§3.4 v0.2 对齐说明：
 *     InvalidState 是 fail-fast token，不发稳定码；EX-SM-6 观测点
 *     "ExecutionError.code"）；
 *   - 能力不支持（T10 守卫）＝**可预期条件**→返回 accepted=false 的
 *     StateMachineAck＋EX-CAPABILITY-UNSUPPORTED 显式反馈，状态不变
 *     （EX-SM-7；ARCH §4.3"不支持暂停的任务收到暂停请求→显式状态
 *     反馈（不静默忽略）"原文）；
 *   - 重复取消（当前已 Canceling 再收 RequestCancel）＝**幂等 ack**，
 *     不转移不发事件（§5.3 T3 守卫注"重复取消幂等：已在 Canceling→ack"；
 *     状态机层的幂等语义，EX-T03 控制器直接透传）。
 */
class TaskStateMachine {
public:
    // ---- 转移表（静态、编译期固定、公共可测——§3.1"转移表＋守卫，公共可测"） ----

    /// §5.3 逐转移表全集（T1~T14 共 14 行＋Running→Canceling 1 行＝15 行；
    /// T1/T14 的 source 为空＝工厂路径）。单例只读引用——内容永不修改。
    static const std::vector<TransitionSpec>& runtimeTable();

    /// §5.2 矩阵纯查询：运行期 (源态→目标态) 是否合法（13 对合法边；
    /// T14 恢复期指派不在运行期矩阵——§5.2 Interrupted 列全"·"行注
    /// "运行期不指派；恢复期指派"）。矩阵外请求在 request() 中抛
    /// InvalidState——本函数供表驱动断言与调用方前置探测。
    static bool isLegalRuntimeTransition(core::TaskState from, core::TaskState to) noexcept;

    // ---- 记录诞生工厂（T1/T14——两条"进状态机"的唯一入口） ----

    /**
     * @brief T1：提交受理→Queued（受理即 Queued——无 Created 态，P-EX-4）。
     *
     * 调用前提（§5.3 T1 守卫列"提交验证全通过"）：提交边界验证
     * （§6.3 V1~V4——EX-T05 调度器职责）已全部通过；**验证失败的提交
     * 永远不会到达本工厂**（状态机之外的结构化拒绝，不产生 TaskRecord
     * ——契约 acceptance 1 括注；EX-T02 以本工厂签名固化该边界：无
     * "Rejected 态记录"的构造路径）。
     *
     * @param record [in] 受理记录——taskId 必须 isValid、state 必须为
     *                     Queued、termination 必须为空（违约抛
     *                     ExecutionError(InvalidState)——调用方拼装错误）
     * @param sink   [in] 事件接缝（可为 null——无消费者时不投递；
     *                     不接管所有权，调用方保证存活期覆盖本机实例）
     * @return 已发布 TaskStatusChanged(Queued) 事件的状态机实例
     *
     * @throws ExecutionError(InvalidState) record 前置违约
     */
    static TaskStateMachine forAcceptedSubmission(TaskRecord record, ITaskEventSink* sink);

    /**
     * @brief T14：恢复期重建→Interrupted（§5.1 Interrupted 行"恢复期
     *        重建"；§7.5 恢复扫描发现归档预留未终结→重建为"已中断"）。
     *
     * 恢复期语义（NFR-REL-03）：主进程崩溃后新会话启动期，恢复扫描
     * （project ②端口）发现无 manifest 的归档预留→execution 重建任务
     * 条目、直接指派 Interrupted 终态——**不伪装为完整结果**（不产生
     * outcome=Completed 的任何路径；EX-TASK-INTERRUPTED 状态标注诊断
     * 随记录累积）。Queued 任务无磁盘痕迹、崩溃即消失，故**不存在**
     * "排队任务被重建为已中断"的路径（P-EX-6——本工厂只承接
     * Preparing 起可观测的运行，§5.4）。
     *
     * @param record [in] 重建记录——taskId 必须 isValid、state 必须为
     *                     Interrupted（恢复扫描的指派结论）；termination
     *                     为空时写入 Interrupted（终态一次写入纪律）
     * @param sink   [in] 事件接缝（同 forAcceptedSubmission）
     * @return 已发布 TaskStatusChanged(Interrupted) 事件的状态机实例
     *
     * @throws ExecutionError(InvalidState) record 前置违约
     */
    static TaskStateMachine forRecoveryInterrupted(TaskRecord record, ITaskEventSink* sink);

    // ---- 运行期转移（T2~T13——唯一状态变更入口） ----

    /**
     * @brief 提交一个运行期转移触发（§5.3：查表→守卫→状态推进→事件）。
     *
     * 处理序（每步语义见类注释"错误语义"）：
     *  1. 幂等特判：RequestCancel 且当前已 Canceling→accepted=true 的
     *     ack，不转移不发事件（§5.3 T3 守卫注）；
     *  2. 查表：(trigger, 当前态) 命中恰一行——未命中＝矩阵外请求→抛
     *     ExecutionError(InvalidState)（EX-SM-6；状态保证不变）；
     *  3. 能力守卫：行声明 requiresSupportsPause 且记录能力不支持→
     *     返回拒绝 ack＋EX-CAPABILITY-UNSUPPORTED 反馈（EX-SM-7；
     *     状态不变、不发事件）；
     *  4. 推进：state←target；终态到达时一次写入 termination（T4=
     *     Canceled、T7/T9=Failed、T8=Completed、T13=ForceTerminated——
     *     §4.2 termination 行"终态时一次写入"）；T11 追加 attempt+1
     *     （同 RunId 新 AttemptId——§4.3；登记追加/旧 attempt 入
     *     supersededAttempts 归 RunRegistry，EX-T04）；
     *  5. 事件：sink 非空则同步发布 TaskStatusChanged(目标态)——五元组
     *     取当前绑定（project/branch/revision 取提交快照锚定三元组；
     *     run/attempt Queued 期为保留空值——§4.2 run 行"Queued 期可空"，
     *     派发登记后恒完整，§4.3）。
     *
     * @param trigger [in] 运行期触发（SubmitAccepted/RecoveryScanFound
     *                     是工厂触发——经本方法提交即矩阵外违约，抛错）
     * @return 应答（接受＝目标态；能力拒绝＝原态＋反馈）
     *
     * @throws ExecutionError(InvalidState) 矩阵外请求（源态不匹配/终态
     *         出边/工厂触发误用）；termination 二次写入（防御分支——
     *         正常路径被"终态无出边"拦截，不可达）
     */
    StateMachineAck request(TransitionTrigger trigger);

    /**
     * @brief 派发登记绑定（§4.3 分配协议第二段——运行/尝试身份的分配点）。
     *
     * 协议位置：调度器出队进入 Preparing 后，RunRegistry.registerRun
     * （登记完整五元组＋runDir——EX-T04）完成即调用本方法把运行身份写
     * 入任务记录：run←登记值、attempt←1（首次尝试）。自此任务携带完整
     * 五元组（事件/通道消息的完整性承诺自此成立——§4.3）。
     *
     * @param run [in] 登记分配的运行身份（core::RunId::generate 产物；
     *                 保留零值/重复绑定/非 Preparing 态调用均属调用方
     *                 违约，抛 ExecutionError(InvalidState)）
     *
     * @throws ExecutionError(InvalidState) run 非法、任务已绑定、状态
     *         非 Preparing（§4.3：绑定只发生在派发登记点）
     */
    void bindRun(core::RunId run);

    /// 只读访问任务记录（调度线程内使用——不跨线程共享可变实例）。
    const TaskRecord& record() const noexcept { return m_record; }

    /**
     * @brief 更新任务进度（§4.2 progress 行"流式更新"的受控写入口——EX-T05
     *        落位；归置增量登记单元卡 §15.4）。
     *
     * 背景：进度由通道读取线程（§6.2）经调度器节流（同任务对外发布
     *   ≤10 Hz，实现参数 D-07）后到达本方法——节流在调度器侧完成，本方法
     *   只承载"节流后的合法帧"。进度**不入**领域事件（core.md D-09），本
     *   方法不发事件、只写记录；对外可见面＝TaskSnapshot/查询投影。
     *
     * @param report [in] 已按 ProgressReport::make 校验的进度帧
     *                     （percent≤100、phaseToken 非空——phaseToken 是
     *                     worker 域的进度阶段 token，原样透传不解读：
     *                     UX-10 七态状态词与映射归 ui，execution 零新增
     *                     状态词——§2.2/§13 ui 行）
     *
     * @throws ExecutionError(InvalidState) 任务已终态（终态后无流式更新——
     *         进度帧晚到属通道与调度失步，调用方契约违约 fail-fast）
     *
     * 线程约束：仅调度线程（状态机唯一写者纪律——§4.2 通用约定）。
     */
    void updateProgress(ProgressReport report);

    /**
     * @brief 追加一条任务级诊断（§4.2 diagnostics 行可变性"追加"的入口）。
     *
     * 背景（EX-T03 编排层需要）：转移类诊断（如 T13 的 EX-FORCE-TERMINATED）
     * 由状态机在转移时写入；而**判定类**诊断（如运行超时判定的
     * EX-WORKER-HUNG——EX-WKR-5"诊断区分 timeout"）发生在两次转移之间，
     * 由协议编排层（TaskController，EX-T03）在强杀动作前写入，随后才触发
     * 转移——诊断序（判定原因在前、终结标记在后）由此保证。提交验证拒绝
     * 等状态机之外的场景**不经**本方法（不产生 TaskRecord——§5.1 注）。
     *
     * @param record [in] 已按 core::DiagnosticRecord::make 校验的记录
     *                     （ERR-01 字段完整；码值须为 diagnostics
     *                     StableCodeRegistry 已收编的稳定码——码值分配
     *                     权威归 diagnostics，execution 不私定）
     *
     * 线程约束：仅调度线程（状态机唯一写者纪律——§4.2 通用约定）。
     */
    void appendDiagnostic(core::DiagnosticRecord record);

    /// 当前状态（record().state 的便捷读）。
    core::TaskState state() const noexcept { return m_record.state; }

    /// 当前五元组绑定（§4.1 混用防线 1：TaskId 不进五元组；project/
    /// branch/revision 取提交快照锚定三元组，run/attempt 取当前绑定——
    /// Queued 期为保留空值，事件与通道消息的"完整五元组"承诺自派发
    /// 登记起成立，§4.3）。
    core::TaskIdentity currentIdentity() const;

private:
    TaskStateMachine() = default;   // 仅工厂可构造（记录诞生路径受控）

    /// 同步发布状态变更事件（sink 为空时静默跳过——无消费者是合法装配）。
    void publishStatusChanged();

    TaskRecord m_record;        ///< 任务主记录（§4.2——随转移推进，本实例私有）
    ITaskEventSink* m_sink = nullptr;   ///< 事件接缝（不接管所有权；可为 null）
};

}  // namespace sdurws::ird::execution

#endif  // SDURWS_IRD_EXECUTION_STATEMACHINE_HPP
