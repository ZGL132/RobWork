/**
 * @file   StateMachine.cpp
 * @brief  任务状态机的实现——§5.3 逐转移表数据、§5.2 矩阵查询、守卫与
 *         事件发布（EX-T02 核心交付物）。
 *
 * 设计依据：
 *   - units/execution.md §5.2（转换矩阵与禁止转换示例）、§5.3（逐转移
 *     表 T1~T14＋并发守卫总规则）、§5.5（能力门控——EX-CAPABILITY-
 *     UNSUPPORTED 显式反馈）、§4.3（身份分配协议——T11 新 AttemptId）、
 *     §9.1（terminationCause 取值与接纳语义的耦合）
 *   - 需求 TASK-01（EX-SM-1~7 的被测行为）、NFR-REL-03（T14 已中断）、
 *     ARCH §11.2-6（四态取消入口＋暂停中取消保留检查点）
 *
 * 实现说明：转移表是**唯一事实源**——isLegalRuntimeTransition 与
 * request() 查表都从 runtimeTable() 推导，杜绝"表与判定两处维护漂移"
 * （§5.2 矩阵与 §5.3 逐转移表在卡内本就同源：矩阵 ✔ 格即逐转移行）。
 * 每行 cleanupNote 逐字摘录 §5.3 清理列，供断言与人工 review 对照。
 */

#include <sdurws/ird/execution/StateMachine.hpp>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace sdurws::ird::execution {
namespace {

using core::TaskState;

/// 抛矩阵外非法转换（EX-SM-6 契约形态：ExecutionError.code==InvalidState；
/// detail 前缀 "execution/statemachine:"——开发诊断定位用）。
void throwInvalidState(const std::string& detail)
{
    throw ExecutionError(ExecutionErrorCode::InvalidState,
                         "execution/statemachine: " + detail);
}

/// 触发枚举的诊断名（detail 文案用——TransitionTrigger 不设公共 token，
/// 其稳定面是 TransitionSpec.trigger 枚举本身，错误文案仅供开发定位）。
const char* triggerToken(TransitionTrigger t) noexcept
{
    switch (t) {
    case TransitionTrigger::SubmitAccepted:         return "SubmitAccepted";
    case TransitionTrigger::DispatchDequeued:       return "DispatchDequeued";
    case TransitionTrigger::RequestCancel:          return "RequestCancel";
    case TransitionTrigger::CancelSettled:          return "CancelSettled";
    case TransitionTrigger::PrepareSucceeded:       return "PrepareSucceeded";
    case TransitionTrigger::PrepareFailed:          return "PrepareFailed";
    case TransitionTrigger::PauseConfirmed:         return "PauseConfirmed";
    case TransitionTrigger::ResumeConfirmed:        return "ResumeConfirmed";
    case TransitionTrigger::RunCompleted:           return "RunCompleted";
    case TransitionTrigger::RunFailed:              return "RunFailed";
    case TransitionTrigger::CancelTimeoutForceKill: return "CancelTimeoutForceKill";
    case TransitionTrigger::RecoveryScanFound:      return "RecoveryScanFound";
    }
    return "?";   // 防御分支（枚举封闭，正常路径不可达）
}

/// 构造显式反馈诊断（§10.2 StatusAck.feedback 承载——core::DiagnosticRecord
/// 工厂强制 context/cause/recommendedAction 非空，故三段文案必须齐备）。
core::DiagnosticRecord makeFeedback(const std::string& code, const std::string& context,
                                    const std::string& cause, const std::string& action)
{
    return core::DiagnosticRecord::make(code, std::nullopt, std::nullopt, std::nullopt,
                                        context, cause, action);
}

}  // namespace

// ---------------------------------------------------------------------
// 逐转移表（§5.3 T1~T14＋Running→Canceling——15 行，次序即卡内行序）
// ---------------------------------------------------------------------

const std::vector<TransitionSpec>& TaskStateMachine::runtimeTable()
{
    // 表内容与 units/execution.md §5.3 逐行对照（review 锚点）：
    //   行号/触发/源→目标/守卫/事件（target 即）/清理（cleanupNote 原文）。
    // T1/T14 的 source=nullopt＝记录诞生工厂路径（请求路径不可达）。
    // Running→Canceling 无独立行号（row=0）——§5.3 表下散文"Running 取消
    // 传递＝通道 CancelRequest→…批次边界收敛"＋§5.2 矩阵 Running 行 ✔。
    static const std::vector<TransitionSpec> kTable = {
        {1,  TransitionTrigger::SubmitAccepted, std::nullopt,       TaskState::Queued, false,
         CheckpointRetentionPolicy::NoCheckpointAction, "—"},
        {2,  TransitionTrigger::DispatchDequeued, TaskState::Queued, TaskState::Preparing, false,
         CheckpointRetentionPolicy::NoCheckpointAction,
         "缓存查找已执行（命中→短路径不经本转移）；资源预算允许否则留队＋诊断"},
        {3,  TransitionTrigger::RequestCancel, TaskState::Queued, TaskState::Canceling, false,
         CheckpointRetentionPolicy::NoCheckpointAction,
         "直接出队，无在途批次（不适用 2 s 协作窗——ARCH §4.3）"},
        {4,  TransitionTrigger::CancelSettled, TaskState::Canceling, TaskState::Canceled, false,
         CheckpointRetentionPolicy::RetainLatestCheckpoint,
         "worker 退出回收；归档 abandon(Canceled)；保留最近检查点"},
        {5,  TransitionTrigger::PrepareSucceeded, TaskState::Preparing, TaskState::Running, false,
         CheckpointRetentionPolicy::NoCheckpointAction,
         "AttemptRecord 建立（归档预留成功＋worker 握手成功为守卫前置）"},
        {6,  TransitionTrigger::RequestCancel, TaskState::Preparing, TaskState::Canceling, false,
         CheckpointRetentionPolicy::NoCheckpointAction,
         "组装中止：丢弃物化派发物、终结归档预留；无在途批次→速达 Canceled"},
        {7,  TransitionTrigger::PrepareFailed, TaskState::Preparing, TaskState::Failed, false,
         CheckpointRetentionPolicy::NoCheckpointAction,
         "归档预留 abandon(Failed)；诊断必附（终结原因记录为守卫列前置）"},
        {8,  TransitionTrigger::RunCompleted, TaskState::Running, TaskState::Completed, false,
         CheckpointRetentionPolicy::NoCheckpointAction,
         "worker 退出回收；缓存登记（仅 Completed 产物，§8.2）；归档 finalize 后追加 ResultArchived"},
        {9,  TransitionTrigger::RunFailed, TaskState::Running, TaskState::Failed, false,
         CheckpointRetentionPolicy::RetainLatestCheckpoint,
         "归档 abandon(Failed)；最近检查点保留可续（ARCH §4.4）"},
        {10, TransitionTrigger::PauseConfirmed, TaskState::Running, TaskState::Paused, true,
         CheckpointRetentionPolicy::NoCheckpointAction,
         "worker 退出回收（释放预算；检查点在盘）——暂停确认边界＝检查点写出并确认〔AT-35〕"},
        {11, TransitionTrigger::ResumeConfirmed, TaskState::Paused, TaskState::Running, false,
         CheckpointRetentionPolicy::NoCheckpointAction,
         "新 AttemptId（登记追加，旧 attempt 标 Superseded）；新 worker 加载检查点启动；继续不重复统计〔AT-35〕"},
        {12, TransitionTrigger::RequestCancel, TaskState::Paused, TaskState::Canceling, false,
         CheckpointRetentionPolicy::RetainLatestCheckpoint,
         "暂停态无在途批次，直达；保留最近检查点（可续跑——ARCH §4.3 A5）"},
        {0,  TransitionTrigger::RequestCancel, TaskState::Running, TaskState::Canceling, false,
         CheckpointRetentionPolicy::NoCheckpointAction,
         "通道 CancelRequest→worker 协作收敛（§5.3 表下散文；批次 ≤10 s 收敛——EX-T03 时序）"},
        {13, TransitionTrigger::CancelTimeoutForceKill, TaskState::Canceling, TaskState::Failed, false,
         CheckpointRetentionPolicy::RetainLatestCheckpoint,
         "abandon(ForceTerminated)；检查点保留；不伪装为普通失败（诊断显式区分 EX-FORCE-TERMINATED）"},
        {14, TransitionTrigger::RecoveryScanFound, std::nullopt, TaskState::Interrupted, false,
         CheckpointRetentionPolicy::RetainLatestCheckpoint,
         "归档预留 abandon 由恢复流程执行（§9.5 强杀时机③前的恢复期指派）"},
    };
    return kTable;
}

bool TaskStateMachine::isLegalRuntimeTransition(core::TaskState from, core::TaskState to) noexcept
{
    // 从表推导（单一事实源）：凡 source 命中 from 且 target==to 的行即
    // 合法边（13 对）。T1/T14（source 空）不构成运行期边——Interrupted
    // 列矩阵全"·"（§5.2 行注"运行期不指派；恢复期指派"）由此保证。
    for (const auto& row : runtimeTable()) {
        if (row.source.has_value() && *row.source == from && row.target == to) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------
// 记录诞生工厂（T1/T14）
// ---------------------------------------------------------------------

TaskStateMachine TaskStateMachine::forAcceptedSubmission(TaskRecord record, ITaskEventSink* sink)
{
    // T1 守卫列"提交验证全通过"的记录面自查：调用方（EX-T05 调度器）
    // 必须在验证通过后才构造记录——三前置违约即调用方拼装错误，fail-fast。
    // 注意"验证失败的提交不产生 TaskRecord"（acceptance 1 括注）由调用
    // 顺序保证：本工厂是受理路径唯一入口，签名上不存在"带拒绝态的记录"。
    if (!record.taskId.isValid()) {
        throwInvalidState("T1 受理记录 taskId 非法（保留零值——TaskId::generate 未调用）");
    }
    if (record.state != TaskState::Queued) {
        throwInvalidState("T1 受理记录 state 必须为 Queued（受理即 Queued——P-EX-4 无 Created 态）");
    }
    if (record.termination.has_value()) {
        throwInvalidState("T1 受理记录不得携带终结原因（仅终态非空——§4.2 termination 行）");
    }
    if (record.run.has_value() || record.attempt.isValid()) {
        // §4.2：Queued 期 run 可空、attempt=0（未派发）——受理时提前绑定
        // 运行身份即绕过了派发登记协议（§4.3 分配协议的次序纪律）。
        throwInvalidState("T1 受理记录不得预绑定运行/尝试（派发时才分配 RunId/AttemptId=1——§4.3）");
    }

    TaskStateMachine machine;
    machine.m_record = std::move(record);
    machine.m_sink = sink;
    machine.publishStatusChanged();   // T1 事件：TaskStatusChanged(Queued)
    return machine;
}

TaskStateMachine TaskStateMachine::forRecoveryInterrupted(TaskRecord record, ITaskEventSink* sink)
{
    // T14 守卫列"重启恢复扫描发现归档预留未终结"的记录面自查：重建条目
    // 由恢复扫描指派 Interrupted（§5.1 行"恢复期重建"）；Queued 任务无
    // 磁盘痕迹（§5.4——崩溃即消失，P-EX-6），故恢复路径不存在"排队任务"。
    if (!record.taskId.isValid()) {
        throwInvalidState("T14 重建记录 taskId 非法");
    }
    if (record.state != TaskState::Interrupted) {
        throwInvalidState("T14 重建记录 state 必须为 Interrupted（恢复期直接指派终态——§5.1）");
    }
    if (!record.run.has_value() || !record.attempt.isValid()) {
        // 被重建的运行必有归档预留（Preparing 起登记）——无运行绑定说明
        // 调用方把内存态任务误传入了恢复路径。
        throwInvalidState("T14 重建记录必须绑定运行与尝试（归档预留未终结的运行——§7.5）");
    }

    TaskStateMachine machine;
    machine.m_record = std::move(record);

    // 终态一次写入（§4.2）：恢复扫描未写终结原因时补写 Interrupted。
    if (!machine.m_record.termination.has_value()) {
        machine.m_record.termination = TerminationCause::Interrupted;
    }

    // 状态标注诊断（§3.4 枚举对齐说明：EX-TASK-INTERRUPTED 为恢复期
    // Interrupted 终态的状态标注码，非 API 错误——随任务诊断累积，
    // 供恢复呈现"已中断可重跑"（PM-08/PM-15/NFR-REL-03）。
    machine.m_record.diagnostics.push_back(makeFeedback(
        "EX-TASK-INTERRUPTED",
        "崩溃恢复：重建中断任务条目",
        "主进程上次会话中断，归档预留未终结（无 manifest）——恢复扫描指派 Interrupted（NFR-REL-03）",
        "可从保留检查点续跑（重跑＝新任务、新身份链）；结果未完成，不得作为完整结果消费"));
    machine.m_sink = sink;
    machine.publishStatusChanged();   // T14 事件：TaskStatusChanged(Interrupted)
    return machine;
}

// ---------------------------------------------------------------------
// 运行期转移（T2~T13：幂等特判→查表→守卫→推进→事件）
// ---------------------------------------------------------------------

void TaskStateMachine::bindRun(core::RunId run)
{
    // §4.3 分配协议次序纪律：运行身份只在派发登记点（Preparing 段，
    // RunRegistry.registerRun 之后）分配一次——Queued 期提前绑定、
    // Running 起补绑定、重复绑定都是协议违约（fail-fast，不静默覆盖）。
    if (m_record.state != TaskState::Preparing) {
        throwInvalidState("运行绑定只发生在 Preparing 派发登记段（§4.3）——当前状态违约");
    }
    if (m_record.run.has_value()) {
        throwInvalidState("运行身份重复绑定（一次运行一个 RunId——登记互斥，EX-RES-1 同源）");
    }
    if (!run.isValid()) {
        throwInvalidState("运行绑定收到保留零值 RunId（core::RunId::generate 未调用）");
    }
    m_record.run = run;
    m_record.attempt = core::AttemptId{1};   // 首次尝试（§4.3"AttemptId=1"）
}

StateMachineAck TaskStateMachine::request(TransitionTrigger trigger)
{
    // ---- 第 1 步：幂等特判（§5.3 T3 守卫注）----
    // 已在 Canceling 再收取消＝重复请求：ack 但不转移不发事件（事件流
    // 只承载状态变化，重复发布会造成消费方状态序误判）。其余触发在
    // Canceling 态（含对 Canceling 的 CancelSettled 以外的任何重复）按
    // 查表结果处理——Canceling→Canceling 无行，自然落入矩阵外抛错。
    if (trigger == TransitionTrigger::RequestCancel && m_record.state == TaskState::Canceling) {
        return StateMachineAck{true, m_record.state, std::nullopt};
    }

    // ---- 第 2 步：查表（(触发, 当前态) 必须恰命中一行）----
    const TransitionSpec* row = nullptr;
    for (const auto& candidate : runtimeTable()) {
        if (candidate.trigger == trigger && candidate.source.has_value()
            && *candidate.source == m_record.state) {
            row = &candidate;
            break;
        }
    }
    if (row == nullptr) {
        // 矩阵外非法转换（含：终态出边、源态不匹配、工厂触发 SubmitAccepted/
        // RecoveryScanFound 误用、队列跳跃如 Queued→Running）＝调用方契约
        // 违约→ExecutionError(InvalidState) fail-fast（§3.4 v0.2 对齐说明；
        // EX-SM-6 断言状态不变由"先查表后推进"次序保证——异常路径无写入）。
        throwInvalidState(std::string{"矩阵外转换请求（触发 "} + triggerToken(trigger)
                          + " 于状态 " + core::toToken(m_record.state) + " 无对应合法转移——§5.2/§5.3）");
    }

    // ---- 第 3 步：能力守卫（仅 T10 声明 requiresSupportsPause）----
    // §5.5/ARCH §4.3：不支持暂停的任务收到暂停请求→**显式反馈非静默**
    // （卡行禁止项；EX-SM-7）。可预期条件走结构化 Ack（§10.8），状态
    // 不变、不发事件；稳定码 EX-CAPABILITY-UNSUPPORTED（diagnostics
    // StableCodeRegistry 已收编——§3.4 清单）。
    if (row->requiresSupportsPause && !m_record.capability.supportsPause) {
        return StateMachineAck{
            false, m_record.state,
            makeFeedback("EX-CAPABILITY-UNSUPPORTED",
                         "任务能力校验：暂停请求于 Running 态",
                         "评估器未声明暂停能力（supportsPause=false——§5.5 注册期声明；最小能力缺省亦为不支持）",
                         "改用支持暂停的评估器，或取消任务（不支持暂停不可静默忽略——ARCH §4.3）")};
    }

    // ---- 第 4 步：推进（状态→终结原因→尝试递增）----
    // 先做防御性终结检查再写状态：终态一次写入（§4.2）不可被二次触发
    // 破坏（正常路径被"终态无出边"第 2 步拦截，此分支为防御纵深）。
    if (isTerminalTaskState(row->target)) {
        if (m_record.termination.has_value()) {
            throwInvalidState("终结原因二次写入（终态一次写入纪律被破坏——防御分支，正常不可达）");
        }
        // 终结原因与目标终态的绑定（§4.2"四类"＋T13 显式标记）：
        //   T4→Canceled；T7/T9→Failed；T8→Completed；T13→ForceTerminated
        //   （状态轴仍为 Failed——强杀不伪装普通失败，§5.3 T13 事件列
        //   "TaskStatusChanged(Failed)＋EX-FORCE-TERMINATED"）。
        switch (row->target) {
        case TaskState::Canceled:    m_record.termination = TerminationCause::Canceled; break;
        case TaskState::Completed:   m_record.termination = TerminationCause::Completed; break;
        case TaskState::Failed:
            m_record.termination = (row->row == 13) ? TerminationCause::ForceTerminated
                                                    : TerminationCause::Failed;
            break;
        case TaskState::Interrupted: m_record.termination = TerminationCause::Interrupted; break;
        default: break;   // 非终态目标不会进入本分支（外层 isTerminalTaskState 已闸）
        }
    }

    m_record.state = row->target;

    // T11（继续）：同 RunId 新 AttemptId（§4.3"暂停后继续/检查点恢复→
    // 同 RunId，AttemptId+1"）。旧 attempt 移入 supersededAttempts 的
    // 登记追加归 RunRegistry（EX-T04，§9.1 字段）——本记录只推进当前
    // 尝试号。无符号回绕防护：attempt 上限即 2^64（工程上不可达）。
    if (row->row == 11) {
        m_record.attempt = core::AttemptId{m_record.attempt.value + 1};
    }

    // T13（超时强杀）：显式区分诊断（"不伪装为普通失败"——T13 事件列；
    // 状态标注面：诊断码 EX-FORCE-TERMINATED，用户可见"已强制终止，
    // 检查点保留"——§7.4 诊断行）。
    if (row->row == 13) {
        m_record.diagnostics.push_back(makeFeedback(
            "EX-FORCE-TERMINATED",
            "任务强制终止：取消协议超时路径",
            "2 s 内未进入 Canceling 或在途批次 10 s 未收敛——强制终止独立工作进程树（TerminateJobObject，§7.1 步 3）",
            "任务标记失败并保留最近检查点，可重跑续算（新任务、新身份链）；强杀与普通失败在呈现层区分"));
    }

    // ---- 第 5 步：事件（§10.4——每次转移发布；同任务 FIFO 由单写者
    // 串行调用本方法保证，§4.3）----
    publishStatusChanged();

    return StateMachineAck{true, m_record.state, std::nullopt};
}

// ---------------------------------------------------------------------
// 诊断追加（§4.2 diagnostics 行"追加"可变性的入口——EX-T03 编排层写入
// 判定类诊断，如运行超时 EX-WORKER-HUNG；转移类诊断仍在 request() 内写）
// ---------------------------------------------------------------------

void TaskStateMachine::appendDiagnostic(core::DiagnosticRecord record)
{
    // 追加语义（§4.2 diagnostics 行）：诊断是任务级累积面，只增不改；
    // 记录的合法性（ERR-01 字段完整＋稳定码）由 core::DiagnosticRecord::
    // make 工厂前置校验——本方法信任已构造记录，不重复校验（单一校验点）。
    // 线程约束：仅调度线程调用（唯一写者纪律）——由类注释与调用方契约
    // 保证，不设锁（与 request() 同域）。
    m_record.diagnostics.push_back(std::move(record));
}

// ---------------------------------------------------------------------
// 进度更新（§4.2 progress 行"流式更新"——EX-T05 调度器节流后的写面）
// ---------------------------------------------------------------------

void TaskStateMachine::updateProgress(ProgressReport report)
{
    // 终态防御（头注错误语义）：进度帧晚到（通道与调度失步的边缘序列）
    // 不允许改写终态记录——调用方契约违约 fail-fast。非终态（含 Canceling
    // ——在途批次收敛中仍可能报出最后进度）一律接受，不按状态加限制：
    // §4.2 progress 行只约定流式更新，没有"仅 Running"的词法依据。
    if (isTerminalTaskState(m_record.state)) {
        throw ExecutionError(ExecutionErrorCode::InvalidState,
                             "execution/scheduler: progress frame arrived after terminal state");
    }
    // 透传纪律（UX-10 边界）：phaseToken 是 worker 域进度阶段 token，本层
    // 不解读、不改写、不映射七态状态词（映射归 ui——§2.2/§13 ui 行）；
    // 字段合法性已由 ProgressReport::make 前置校验（单一校验点）。
    m_record.progress = std::move(report);
}

core::TaskIdentity TaskStateMachine::currentIdentity() const
{
    // 五元组组装（§4.1 混用防线 1：TaskId 不进五元组）。project/branch/
    // revision 取提交快照锚定三元组（快照冻结修订＝归档目标修订，与
    // 当前 HEAD 无关——§9.1 runDir 行同源纪律）；run/attempt Queued 期
    // 为保留空值（§4.2 run 行"Queued 期可空"）——事件五元组的完整性
    // 承诺自派发登记起成立（§4.3"一律携带当前 (run, attempt)"）。
    core::TaskIdentity identity;
    identity.project = m_record.submission.snapshot.project;
    identity.branch = m_record.submission.snapshot.branch;
    identity.revision = m_record.submission.snapshot.revision;
    identity.run = m_record.run.value_or(core::RunId{});
    identity.attempt = m_record.attempt;
    return identity;
}

void TaskStateMachine::publishStatusChanged()
{
    if (m_sink == nullptr) {
        return;   // 无消费者是合法装配（测试/裸装配场景）——非静默错误
    }
    core::TaskStatusChangedPayload payload;
    payload.task = currentIdentity();
    payload.newState = m_record.state;
    m_sink->onTaskStatusChanged(payload);
}

}  // namespace sdurws::ird::execution
