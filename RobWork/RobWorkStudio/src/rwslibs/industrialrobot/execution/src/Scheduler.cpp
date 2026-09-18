/**
 * @file   Scheduler.cpp
 * @brief  两段实现——TaskScheduler 调度本体（EX-T05：提交验证 V1~V4/
 *         双优先级队列/预算闸/内联门槛/进度节流）与 DrainPolicy 排空
 *         编排（EX-T03：§7.5 关闭二选的执行侧）。
 *
 * 设计依据：见 Scheduler.hpp 文件头（§4.3/§6.1~§6.3/§7.4~§7.5/§10.1/
 * §10.8、ARCH §4.1/§4.4、acceptance 1~5——此处不重复）。
 *
 * 实现说明（本文件的关键结构决策，评审重点）：
 *   - **主锁即调度串行域**（阶段 A 显式驱动形态——头注"调度线程模型"）：
 *     submit 受理/tick 推进/查询投影全部在 m_mutex 临界区内互斥，状态机
 *     的唯一写者纪律由"临界区互斥＋tick 单线程"共同承担；Controller 的
 *     "仅调度线程"方法只在主锁内（或 tick 单线程域）被调用；
 *   - **唯一锁外段是内联执行**（§6.3 内联门槛的计算本体）：锁只保护
 *     簿记不覆盖计算——锁外段机器无并发写者（唯一写者＝tick 线程自身，
 *     控制命令只入队、由同一线程稍后的 poll 消费），const 访问安全；
 *   - DrainCoordinator 编排是 TaskController 之上的策略层——所有任务级
 *     动作（取消/强杀/协议推进）都经 TaskController 的公共面执行，本
 *     文件不触碰 worker 句柄（职责分层：协议时序归 Controller，关闭
 *     策略归 DrainCoordinator，受理/排队/派发/验证归 TaskScheduler）。
 */

#include <sdurws/ird/execution/Scheduler.hpp>

#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/evidence/Evaluator.hpp>   // IProducerRegistryView（V1 评估器注册查询——登记边）
#include <sdurws/ird/evidence/Snapshot.hpp>    // AnalysisSnapshot/IRevisionClosureSource（V1/V2）
#include <sdurws/ird/execution/StateMachine.hpp>
#include <sdurws/ird/project/ProjectStore.hpp> // ProjectStore::writable（V3——PM-07，登记边）

#include <algorithm>
#include <stdexcept>
#include <thread>
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

/// 结构化拒绝诊断构造（ERR-01 字段完整的最小承载——context 定域、cause
/// 携带开发明细、recommendedAction 给用户可读动作；subject 等呈现字段
/// 非提交拒绝所需，恒空——不伪造）。稳定码为 diagnostics StableCodeRegistry
/// 已收编的 EX-* 码（码值权威归 diagnostics，execution 只引用 token）。
core::DiagnosticRecord rejectionDiag(std::string code, std::string cause)
{
    return core::DiagnosticRecord::make(std::move(code), std::nullopt, std::nullopt,
                                        std::nullopt, "execution/scheduler",
                                        std::move(cause),
                                        "检查提交内容或装配后重新提交");
}

/// 内联资格的 Preview 门槛（IInlineRunExecutor 归置边界——§6.3 括注的
/// 登记段依赖派发装配面，随 EX-T06 统一放开；Preview 不登记不归档，
/// 内联走链零登记语义自洽——表 1）。
bool inlineEligibleSubmission(const TaskScheduler::Config& config,
                              const TaskSubmission& submission)
{
    return static_cast<bool>(config.inlineEligible)
        && config.inlineEligible(submission)
        && submission.mode == core::EvaluationMode::Preview;
}

}  // namespace

// =====================================================================
// TaskScheduler——构造 / 装配面
// =====================================================================

TaskScheduler::TaskScheduler(TaskController& controller, DrainCoordinator& drain,
                             Collaboration collab, Config config, ClockFn clock)
    : m_controller(controller)
    , m_drain(drain)
    , m_collab(std::move(collab))
    , m_config(std::move(config))
    , m_clock(std::move(clock))
{
}

TaskScheduler::~TaskScheduler() = default;

void TaskScheduler::setEventBus(core::IDomainEventBus* bus) noexcept
{
    // 装配面（头注：先于首个 submit——受理段按此值决定状态机的 sink）。
    // 不取主锁：装配期单线程约定（L5 装配序），与 submit/tick 无并发窗。
    m_eventBus = bus;
}

void TaskScheduler::setInlineExecutor(IInlineRunExecutor* executor) noexcept
{
    // 同 setEventBus——装配期一次；空＝无内联能力（全部排队）。
    m_inlineExecutor = executor;
}

std::chrono::steady_clock::time_point TaskScheduler::steadyClock() noexcept
{
    return TaskController::steadyClock();
}

// =====================================================================
// 提交受理（§6.3 V1~V4＋§4.3 受理段——acceptance 1 的主载体）
// =====================================================================

SubmitResult TaskScheduler::submit(TaskSubmission&& submission)
{
    // 主锁内完成"验证→分配→入队"整段（§4.3"提交（调度线程串行）"——
    // 受理段的互斥即串行；并发提交各得唯一 TaskId＝互斥段内 generate()
    // 各一次＋Id128 随机唯一，EX-SUB-1 的结构保证）。
    std::lock_guard<std::mutex> lock(m_mutex);

    // 关闭态拒新（§10.1 合法调用行"shutdown 后 submit→ExecutionError
    // (ContextClosed)"——调用方违约走异常，非结构化拒绝：关闭后提交是
    // 调用时序错误，不是提交内容问题）。m_drain.closed() 为幂等标志。
    if (m_drain.closed()) {
        throw ExecutionError(ExecutionErrorCode::ContextClosed,
                             "execution/scheduler: submit after shutdown");
    }

    // V1~V3（形式/内容/权限——清单行序，首错即停）。结构化拒绝经
    // diagnostics 不抛（§10.1 注）。
    if (std::optional<core::DiagnosticRecord> diag = validateSubmission(submission)) {
        return SubmitResult{false, std::nullopt, {*std::move(diag)}};
    }

    // V4 预算（等待队列容量——§6.1"提交期预算检查仅拒绝对列容量溢出"）。
    if (!queueHasCapacity()) {
        return SubmitResult{false, std::nullopt,
                            {rejectionDiag("EX-TASK-REJECTED",
                                           "等待队列已满（容量 "
                                               + std::to_string(m_budget.queueCapacity)
                                               + "）——提交被容量预算拒绝")}};
    }

    // ---- 受理段（§4.3 分配协议第一段：TaskId 分配在提交受理时完成） ----

    // TaskId 全局唯一随机（thread_local 引擎——并发安全）；幂等性不按
    // 提交内容承诺：同输入重复提交＝两个独立任务（§4.2 taskId 行）。
    const TaskId taskId = TaskId::generate();

    // 任务主记录（§4.2 字段默认值——state=Queued 由默认构造保证）。
    // capability 在受理时即按注册表推导（§5.5"派发时查表推导"的归置：
    // 注册表注册期＝L5 装配，先于一切提交，受理与派发间值不可能变化——
    // 提前写入使 capability 的"此后不可变"纪律更早成立，登记 §15.4）。
    TaskRecord record;
    record.taskId = taskId;
    record.submission = std::move(submission);
    record.capability = m_collab.capabilities != nullptr
        ? m_collab.capabilities->lookupOrMinimal(record.submission.evaluatorKey)
        : TaskCapability{};

    // T1：受理即 Queued（无 Created 态，P-EX-4）并同步发布
    // TaskStatusChanged(Queued)——事件经 ITaskEventSink 适配（this）到
    // 总线；总线未装配时以空 sink 构造（转移不发事件——合法装配）。
    // "同任务事件 FIFO"的序基点：本事件是任务事件流的第一个元素
    // （acceptance 1——同发布者 FIFO 的状态机侧保证点）。
    auto machine = std::make_unique<TaskStateMachine>(
        TaskStateMachine::forAcceptedSubmission(std::move(record),
                                                m_eventBus != nullptr ? this : nullptr));
    TaskStateMachine* machinePtr = machine.get();

    // 登记进协议引擎（取消协议的受管面；attachTask 的"仅调度线程"约束
    // 由主锁域＝调度串行域满足——头注模型）。TaskId 唯一性使重复登记
    // 不可达（attachTask 的防御抛错＝受理段内部错误的显式暴露）。
    m_controller.attachTask(std::move(machine));

    // 入簿记与等待队列：双优先级（§6.1 队列排序行）——Interactive 全序
    // 先于 Background，同级按提交序号 FIFO（单调递增排序键，D-04）。
    const TaskPriority priority = machinePtr->record().submission.priority;
    ScheduledTask entry;
    entry.machine = machinePtr;
    entry.priority = priority;
    entry.submitSeq = m_nextSubmitSeq++;
    m_tasks.emplace(taskId, entry);
    (priority == TaskPriority::Interactive ? m_interactive : m_background)
        .push_back(taskId);

    return SubmitResult{true, taskId, {}};
}

// ---------------------------------------------------------------------
// 提交验证 V1~V3（V4 见 queueHasCapacity——清单行序的拆分）
// ---------------------------------------------------------------------

std::optional<core::DiagnosticRecord> TaskScheduler::validateSubmission(
    const TaskSubmission& submission) const
{
    // ---- V1 形式校验（§6.3 清单行 1） ----

    // [V1-a] 上下文注入半区："project/branch/revision 属当前存储上下文"
    // 是存储侧知识（PA-1——execution 不私判），经 ISubmissionGuard 由 L5
    // 注入；guard 未装配＝无注入半区（可选增强，跳过不拒绝）。
    if (m_collab.guard != nullptr) {
        if (std::optional<core::DiagnosticRecord> diag = m_collab.guard->checkSubmission(submission)) {
            return diag;   // 注入侧诊断原样拒绝（契约——ISubmissionGuard 注）
        }
    }

    // [V1-b] 快照身份三元组合法（五元组前缀的执行侧最小核对——快照
    // 锚定三元组进事件五元组前缀，§4.1）。
    const evidence::AnalysisSnapshot& snapshot = submission.snapshot;
    if (!snapshot.project.isValid() || !snapshot.branch.isValid()
        || !snapshot.revision.isValid()) {
        return rejectionDiag("EX-TASK-REJECTED",
                             "快照锚定身份三元组含保留值（project/branch/revision）");
    }

    // [V1-c] 快照已冻结（evidence builder 产物——snapshotId 非保留值；
    // builder 中间态提交＝CON-01 违约）。
    if (!snapshot.snapshotId.isValid()) {
        return rejectionDiag("EX-TASK-REJECTED",
                             "快照未冻结（snapshotId 为保留值——非 evidence builder 产物）");
    }

    // [V1-d] 策略与名称映射内容身份非空（CON-06——运行绑定扩展字段的
    // 前置；判定面在 evidence builder，此处只核对非空）。
    if (!snapshot.policyRef.policyContentIdentity.isValid()
        || !snapshot.nameMapRef.nameMapContentIdentity.isValid()) {
        return rejectionDiag("EX-TASK-REJECTED",
                             "策略或名称映射内容身份为空（CON-06 绑定不完整）");
    }

    // [V1-e] mode 合法（值域防御——枚举只认三值，越界值按调用方违约
    // 结构化拒绝；非法值不进入状态机/登记任何路径）。
    if (submission.mode != core::EvaluationMode::Preview
        && submission.mode != core::EvaluationMode::Quick
        && submission.mode != core::EvaluationMode::Verified) {
        return rejectionDiag("EX-TASK-REJECTED", "评估模式值域非法");
    }

    // [V1-f] 评估器已注册且契约版本相符（evidence 注册查询面——注册表
    // 缺失＝校验面缺失，fail-closed 拒绝：缺校验面的受理等于绕过门禁，
    // 见类注释"校验面缺失语义"）。
    if (m_collab.evaluators == nullptr) {
        return rejectionDiag("EX-TASK-REJECTED",
                             "评估器注册查询面未装配（Collaboration::evaluators 为空）");
    }
    if (!m_collab.evaluators->isRegistered(submission.evaluatorKey)) {
        return rejectionDiag("EX-TASK-REJECTED",
                             "评估器未注册：" + submission.evaluatorKey);
    }
    if (!m_collab.evaluators->contractVersionMatches(submission.evaluatorKey,
                                                     submission.contractVersion)) {
        return rejectionDiag("EX-TASK-REJECTED",
                             "评估器契约版本不符（key=" + submission.evaluatorKey + "）");
    }

    // ---- V2 内容校验（§6.3 清单行 2——修订闭包含性，CON-01 派发前
    // 快照完整性；经 evidence IRevisionClosureSource 同源形态注入） ----

    if (m_collab.closure == nullptr) {
        return rejectionDiag("EX-SNAPSHOT-STALE",
                             "修订闭包查询面未装配（Collaboration::closure 为空）");
    }
    // objectClosure 每条 (oid,cv) 必须属于锚定修订（防混入/防过期——
    // 修订被移出闭包或对象缺失即 EX-SNAPSHOT-STALE 拒绝；EX-SUB-2 载体）。
    for (const evidence::ObjectRefEntry& ref : snapshot.objectClosure) {
        if (!m_collab.closure->objectInRevision(snapshot.revision, ref.objectId,
                                                ref.contentVersion)) {
            return rejectionDiag("EX-SNAPSHOT-STALE",
                                 "快照对象不在锚定修订闭包内（过期快照——objectId="
                                     + ref.objectId.toCanonical() + "）");
        }
    }

    // ---- V3 权限校验（§6.3 清单行 3——Quick/Verified 消费
    // store.writable()；Preview 不写 results/ 故跳过；写权限判定归
    // project——execution 不私判，PM-07 只读拒绝启动） ----

    if (submission.mode != core::EvaluationMode::Preview) {
        if (m_collab.store == nullptr) {
            return rejectionDiag("EX-STORE-READ-ONLY",
                                 "存储写权限查询面未装配（Collaboration::store 为空）");
        }
        if (!m_collab.store->writable()) {
            return rejectionDiag("EX-STORE-READ-ONLY",
                                 "项目上下文只读（PM-07）——需写 results/ 的正式评估拒绝启动");
        }
    }

    return std::nullopt;   // V1~V3 全绿（V4 在 submit 主体内）
}

bool TaskScheduler::queueHasCapacity() const
{
    // 预置：主锁已持有。容量判据＝两等待队列长度之和（V4 的"队列容量"
    // 语义——排队位占用；在途任务不占队列容量）。
    return m_interactive.size() + m_background.size() < m_budget.queueCapacity;
}

// =====================================================================
// 查询投影（任意线程——主锁内深拷贝，§4.2 通用约定）
// =====================================================================

std::optional<TaskSnapshot> TaskScheduler::tryTask(TaskId task) const noexcept
{
    // noexcept 契约（§10.1 原文）：锁内查表＋投影拷贝无失败路径（极端
    // OOM 场景按 noexcept 语义终止——查询面不为内存异常设计降级）。
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_tasks.find(task);
    if (it == m_tasks.end()) {
        return std::nullopt;   // 未登记（含被拒绝的提交——EX-SUB-2"状态查询为空"观测点）
    }
    const TaskRecord& record = it->second.machine->record();
    TaskSnapshot snap;
    snap.taskId = record.taskId;
    snap.state = record.state;
    snap.run = record.run;
    snap.attempt = record.attempt;
    snap.progress = record.progress;
    snap.termination = record.termination;
    snap.archivePhase = record.archivePhase;
    return snap;
}

std::vector<TaskSnapshot> TaskScheduler::tasksByProject(core::ProjectId project) const
{
    // PM-03 任务清单数据（§10.1 原文签名）：按提交快照锚定项目过滤。
    // 迭代序＝哈希表序（无业务排序语义——呈现排序归 ui，§13 交接行）。
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<TaskSnapshot> result;
    for (const auto& [taskId, entry] : m_tasks) {
        const TaskRecord& record = entry.machine->record();
        if (record.submission.snapshot.project == project) {
            TaskSnapshot snap;
            snap.taskId = record.taskId;
            snap.state = record.state;
            snap.run = record.run;
            snap.attempt = record.attempt;
            snap.progress = record.progress;
            snap.termination = record.termination;
            snap.archivePhase = record.archivePhase;
            result.push_back(std::move(snap));
        }
    }
    return result;
}

void TaskScheduler::setResourceBudget(const ResourceBudget& budget)
{
    // 运行期可调（§6.1"验证不改变任务身份：预算变化只影响排队与派发
    // 时机"）——仅换值，无任务面动作。
    std::lock_guard<std::mutex> lock(m_mutex);
    m_budget = budget;
}

// =====================================================================
// 关闭排空（§7.5——经 DrainCoordinator 组合实现，§10.1 注）
// =====================================================================

void TaskScheduler::shutdown(DrainPolicy policy)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    // 幂等（§10.1"shutdown 幂等"注）由 DrainCoordinator 内部标志保证
    // （二次调用不重复取消排队/不重置兜底阈值计时）。CancelQueuedAndWait
    // 的排队取消在其内部经 controller 命令通道执行——UX-03 零错误诊断
    // 由取消协议保证；KeepQueuedTerminate 排队保留（P-EX-6）。
    // 关闭后出队停止由 tick 的 closed 判定承载（§7.5 停止派发）。
    m_drain.shutdown(policy);
}

bool TaskScheduler::drained() const noexcept
{
    // 排空完成查询（无永久等待——轮询面）：未关闭恒 false；关闭后＝
    // 无在途运行（DrainCoordinator 判定语义——排队不阻塞，头注）。
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_drain.drained();
}

// =====================================================================
// 进度接收与节流（§6.1"进度报告与节流"——D-07 实现参数）
// =====================================================================

void TaskScheduler::reportProgress(TaskId task, const ProgressReport& report)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // 未知任务＝调用方违约（通道帧携带的任务必经 submit 受理）——fail-fast。
    const auto it = m_tasks.find(task);
    if (it == m_tasks.end()) {
        throw ExecutionError(ExecutionErrorCode::InvalidState,
                             "execution/scheduler: progress for unknown task");
    }
    // 终态任务的进度帧＝同步违约（终态后无流式更新——§4.2 progress 行）。
    if (isTerminalTaskState(it->second.machine->state())) {
        throw ExecutionError(ExecutionErrorCode::InvalidState,
                             "execution/scheduler: progress after terminal state");
    }

    // 节流判定（同任务对外发布 ≤10 Hz＝最小间隔 100 ms，实现参数 D-07——
    // 非上游需求值）：窗内帧直接丢弃（worker 进度高频连续，最新值由后续
    // 窗外帧携带）；窗口判定基准＝每任务最近放行帧的时钟戳。间隔置 0＝
    // 不节流（逐帧透传——测试对照面）。
    const std::chrono::steady_clock::time_point now = m_clock ? m_clock() : steadyClock();
    if (m_config.progressMinInterval.count() > 0) {
        const auto last = m_lastProgressAt.find(task);
        if (last != m_lastProgressAt.end()
            && now - last->second < m_config.progressMinInterval) {
            return;   // 窗内帧丢弃（不发布不排队——节流的"对外"语义）
        }
    }
    m_lastProgressAt[task] = now;
    m_pendingProgress.emplace_back(task, report);
    // 放行帧经 tick 段 a 应用（updateProgress 在调度域执行——任意线程
    // 的本方法只做簿记，不触碰状态机写面）。
}

// =====================================================================
// 调度推进（tick——仅调度域单线程，不可重入）
// =====================================================================

void TaskScheduler::tick()
{
    // ---- 段 a：应用节流放行的进度帧（主锁内——状态机写面在调度域） ----
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        for (auto& [task, report] : m_pendingProgress) {
            const auto it = m_tasks.find(task);
            if (it == m_tasks.end()) {
                continue;   // 任务已释放（releaseResources）——陈旧帧丢弃
            }
            if (isTerminalTaskState(it->second.machine->state())) {
                continue;   // 入队后到达终态（如排队取消）——延迟帧丢弃
                            // （入队时刻的上报已合法应答；应用时刻过期不
                            // 追溯违约，与取消竞态的边缘序列无害）
            }
            // 单锁域内无并发转移（检查与应用同临界区）——updateProgress
            // 的终态防御在此不可达（双保险保留）。
            it->second.machine->updateProgress(std::move(report));
        }
        m_pendingProgress.clear();
    }

    // ---- 段 b/c：关闭期走排空复合拍；否则协议推进＋出队派发 ----
    {
        // 主锁全程持有（除 dispatchOne 的内联执行段）——协议动作与簿记
        // 同域互斥（Controller"仅调度线程"方法的调用域）。
        std::unique_lock<std::mutex> lock(m_mutex);
        if (m_drain.closed()) {
            // 关闭期：排空编排复合拍（controller.poll 协议推进＋超阈值
            // abandonAll 兜底——§7.5"等待排空"的推进面），不再出队新任务
            // （停止派发）。
            m_drain.poll();
            return;
        }
        // 协议优先（§7.1 步 1"取消命令插队于派发之前"——poll 命令段在
        // 出队之前执行，取消响应延迟上界＝tick 周期，Controller 注）。
        m_controller.poll();
        // 出队派发（额度/优先级闸＋T2＋内联走链——每拍至多一个任务：
        // 出队节奏由调度周期承载，单拍单出使"取消命令下一拍即见"的
        // 响应上界不被长派发段吞没）。
        dispatchOne(lock);
    }
}

// ---------------------------------------------------------------------
// 出队与派发（前置：主锁已持有；内联段临时释放——见函数内注）
// ---------------------------------------------------------------------

void TaskScheduler::dispatchOne(std::unique_lock<std::mutex>& lock)
{
    // ---- 额度闸 1：并发上限（§6.1 maxConcurrentTasks，0＝自动
    // min(4, 逻辑核/2) 下限 1——逻辑核查询失败〔返回 0〕按 1 核处理） ----
    std::uint32_t maxConcurrent = m_budget.maxConcurrentTasks;
    if (maxConcurrent == 0) {
        const unsigned cores = std::thread::hardware_concurrency();
        maxConcurrent = static_cast<std::uint32_t>(std::max(1u, cores / 2u));
        maxConcurrent = std::min(maxConcurrent, 4u);
    }
    if (inFlightCount() >= maxConcurrent) {
        return;   // 额度满——不抢占（D-04）：在途任务跑完自然腾位
    }

    // ---- 额度闸 2：优先级选择（§6.1——Interactive 全序先于 Background，
    // 同级 FIFO〔提交序号单调〕；双 deque 的队头即序首） ----
    std::deque<TaskId>& queue =
        !m_interactive.empty() ? m_interactive : m_background;
    if (queue.empty()) {
        return;   // 无等待任务
    }
    const TaskId head = queue.front();
    const auto entryIt = m_tasks.find(head);
    if (entryIt == m_tasks.end()) {
        // 队列与簿记失步（stopDispatch 已移除簿记的路径不存在——防御
        // 分支）：剔除坏队头并放弃本拍（正常路径不可达）。
        queue.pop_front();
        return;
    }
    ScheduledTask& entry = entryIt->second;
    const TaskRecord& record = entry.machine->record();

    // ---- 额度闸 3：同项目正式任务上限（§6.1 防单项目独占；Preview 不
    // 计入——不写 results/ 的轻任务不占正式额度）。队头阻塞语义：受限
    // 任务挡住整个队列（严格 FIFO/优先级序可预测性优先于吞吐——D-04
    // 排序键精神，登记 §15.4）。
    if (record.submission.mode != core::EvaluationMode::Preview
        && inFlightFormalFor(record.submission.snapshot.project)
            >= m_budget.maxTasksPerProject) {
        return;
    }

    // ---- T2：出队（Queued→Preparing——§4.3 分配协议第二段的起点；
    // 事件随转移同步发布）。出队即离队（等待队列容量因此释放）。
    queue.pop_front();
    entry.machine->request(TransitionTrigger::DispatchDequeued);

    // ---- 内联门槛（§6.3：可预测 <1 s 的轻任务调度线程内联——ARCH
    // §4.1）：谓词注入＋执行体装配＋Preview 三者齐备才内联；否则停留
    // Preparing 等待 EX-T06 派发链（worker 启动/登记段——登记完整性
    // 优先，见 IInlineRunExecutor 归置边界）。
    if (!inlineEligibleSubmission(m_config, record.submission)
        || m_inlineExecutor == nullptr) {
        return;   // 非 Preview 内联资格／无执行体＝普通派发形态（停留）
    }

    // ---- 内联执行段（锁外——锁只保护簿记不覆盖计算，NFR-PERF-01 的
    // 结构表达：调用方〔含 UI 线程的 submit/查询〕在执行体运行期间照常
    // 前进）。锁外安全性：状态机唯一写者＝tick 线程（本线程），控制命令
    // 只入 controller 队列、由本线程稍后的 poll 消费——record 在本段无
    // 并发写者，const 访问安全（头注"调度线程模型"）。
    const TaskRecord& inlineRecord = entry.machine->record();
    lock.unlock();
    InlineRunOutcome outcome;
    try {
        outcome = m_inlineExecutor->run(inlineRecord);
    } catch (...) {
        // 执行体抛出＝装配违约 escaping 契约面（InlineRunOutcome 已是
        // 结构化应答面）——重锁后按 Failed 走链兜底，不让异常穿透 tick
        // 打断调度循环；诊断以开发说明补记（原始异常信息不可跨契约
        // 携带，终结原因承载 Failed）。
        lock.lock();
        entry.machine->request(TransitionTrigger::PrepareSucceeded);
        entry.machine->appendDiagnostic(
            rejectionDiag("EX-TASK-REJECTED", "内联执行体异常逸出（装配违约）"));
        entry.machine->request(TransitionTrigger::RunFailed);
        return;
    }
    lock.lock();

    // ---- 内联走链收尾（重锁——簿记域）：T5（Preparing→Running，无
    // worker 派发段——§6.3"同一转移表"）→执行期诊断追加→T8/T9。
    // cause 校验先行：非法值＝执行体装配违约，fail-fast（不带病终结——
    // 任务停在 Running 由装配方处置，行为与中止的 tick 一致）。
    if (outcome.cause != TerminationCause::Completed
        && outcome.cause != TerminationCause::Failed) {
        throw ExecutionError(ExecutionErrorCode::InvalidState,
                             "execution/scheduler: inline executor returned illegal cause");
    }
    entry.machine->request(TransitionTrigger::PrepareSucceeded);
    for (core::DiagnosticRecord& diag : outcome.diagnostics) {
        entry.machine->appendDiagnostic(std::move(diag));
    }
    // 终态出口：Completed→T8；Failed→T9（执行失败不是调用方违约——
    // 走状态机正常转移，诊断已随 outcome 追加）。
    entry.machine->request(outcome.cause == TerminationCause::Completed
                               ? TransitionTrigger::RunCompleted
                               : TransitionTrigger::RunFailed);
}

// ---------------------------------------------------------------------
// 额度判据（前置：主锁已持有）
// ---------------------------------------------------------------------

std::size_t TaskScheduler::inFlightCount() const
{
    // 在途＝Preparing/Running/Canceling（§6.1 并发上限的计量口径）。
    // O(任务总数) 遍历：阶段 A 任务量为会话级个位～十位数量级，线性
    // 扫描足够；计数器优化随规模化验收（WP-23）再评估。
    std::size_t count = 0;
    for (const auto& [taskId, entry] : m_tasks) {
        if (isInFlight(entry.machine->state())) {
            ++count;
        }
    }
    return count;
}

std::size_t TaskScheduler::inFlightFormalFor(core::ProjectId project) const
{
    // 同项目在途正式任务（Quick/Verified——Preview 不占正式额度）。
    std::size_t count = 0;
    for (const auto& [taskId, entry] : m_tasks) {
        const TaskRecord& record = entry.machine->record();
        if (record.submission.mode != core::EvaluationMode::Preview
            && record.submission.snapshot.project == project
            && isInFlight(record.state)) {
            ++count;
        }
    }
    return count;
}

// =====================================================================
// 派发闸门与事件接缝（Controller 回调 / 状态机适配）
// =====================================================================

void TaskScheduler::stopDispatch(TaskId task)
{
    // 前置（头注）：仅 Controller::poll() 在主锁内回调——同域同线程，
    // 不取主锁（重入死锁）。语义：取消的任务移出等待队列（§7.1 步 1
    // "停止派发"的排队半区——T3 直达取消不再出队）；簿记保留（终态
    // 投影仍可查）。任务不在等待队列（已出队/终态）＝无害 no-op。
    removeFromQueue(m_interactive, task);
    removeFromQueue(m_background, task);
}

void TaskScheduler::onTaskStatusChanged(const core::TaskStatusChangedPayload& payload)
{
    // 前置：总线已装配（受理段判空——装配缺省时状态机拿空 sink，本
    // 回调不可达）。适配为 core DomainEvent 发布（值拷贝入总线队列）——
    // publish 线程安全即返，回调链（状态机转移→本方法）不被总线拖慢；
    // 同发布者 FIFO＝转移序（§10.4 的状态机侧保证点）。
    m_eventBus->publish(core::DomainEvent::make(payload));
}

void TaskScheduler::removeFromQueue(std::deque<TaskId>& queue, TaskId task)
{
    for (auto it = queue.begin(); it != queue.end(); ++it) {
        if (*it == task) {
            queue.erase(it);
            return;
        }
    }
}

// ---------------------------------------------------------------------
// DrainPolicy 排空编排的实现（EX-T03）
// ---------------------------------------------------------------------

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
