/**
 * @file   WorkerSupervisor.cpp
 * @brief  worker 池监督器实现（契约见 WorkerSupervisor.hpp；§6.4 生命
 *         周期/退出码约定集、§6.5 通道协议消费、§7.4 失联强杀路径的
 *         可执行体）。
 *
 * 实现要点（对照头注释逐段）：
 *   - 读线程只做"字节→帧→收件箱"（含 FrameAssembler——其状态属字节流
 *     域，随读线程自持）；seq 序控/重组/心跳时戳/分类判定全部在调度线程
 *     poll 内（状态机纪律：任务语义不进读线程）。
 *   - 退出码分类是纯函数 classifyWorkerExitCode（§6.4 约定集表的数据化）。
 *   - 池纪律："崩溃的 worker 永不回池"由结构保证——只有正常分类走 Idle；
 *     Dead 记录保留为墓碑（观测面），复用查找只扫 Idle。
 */

#include <sdurws/ird/execution/WorkerSupervisor.hpp>

#include <algorithm>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

#include "win32/ChannelPair.hpp"
#include "win32/JobScope.hpp"
#include "win32/MemProbe.hpp"
#include "win32/ProcessLauncher.hpp"

namespace sdurws::ird::execution {

using win32::ChannelPair;
using win32::JobScope;
using Clock = std::chrono::steady_clock;

namespace {

/// 注入时钟读取（空＝steady now——与 TaskController 同款约定）。
Clock::time_point clockNow(const WorkerSupervisor::ClockFn& clock)
{
    return clock ? clock() : Clock::now();
}

/// 启动失败稳定码诊断（EX-WORKER-LAUNCH-FAILED——§3.4 稳定码清单）。
core::DiagnosticRecord launchFailedDiag(std::string detail)
{
    return core::DiagnosticRecord::make(
        "EX-WORKER-LAUNCH-FAILED", std::nullopt, std::nullopt, std::nullopt,
        "execution/worker-launch", std::move(detail),
        "检查 worker 可执行部署位置（bin 同目录，NFR-DEP-02）与评估器装配清单");
}

}  // namespace

// =====================================================================
// classifyWorkerExitCode——§6.4 退出码约定集表（纯函数）
// =====================================================================

ExitClassification classifyWorkerExitCode(std::uint32_t exitCode,
                                          bool terminatedBySupervisor) noexcept
{
    // 监督方强杀优先于退出码表：TerminateJobObject 的退出码参数是监督方
    // 选择的值，不代表 worker 自身行为（§7.4 强杀路径的分类面）。
    if (terminatedBySupervisor) {
        return ExitClassification::ForceTerminatedBySupervisor;
    }
    // §6.4 约定集逐行（值序＝表行序）；约定集之外的任何值——含 Windows
    // 异常码族 0xC0000005 等——一律判 WorkerCrashed（NFR-REL-02）。
    switch (exitCode) {
    case 0:
        return ExitClassification::NormalCompletion;   // 正常终结（按通道最后消息走 T8/T4/T10）
    case 10:
        return ExitClassification::LaunchFailed;       // 启动失败（依赖/装配错误）
    case 11:
        return ExitClassification::HostInternalError;  // 宿主内部错误（协议/装载错）
    case 12:
        return ExitClassification::EvaluatorFailed;    // 评估器失败（ErrorReport 已先行）
    case 20:
        return ExitClassification::CancelAcknowledged; // 协作取消确认——T4
    case 21:
        return ExitClassification::PauseAcknowledged;  // 暂停确认——T10
    default:
        return ExitClassification::Crashed;
    }
}

// =====================================================================
// WorkerRecord（内部记录——定义在本文件，公共头只持前向声明）
// =====================================================================

struct WorkerSupervisor::WorkerRecord {
    /// 收件箱条目（读线程→调度线程；Frames 之外的项目＝流终结）。
    struct InboxItem {
        enum class Kind { Frames, Exited, ChannelError };
        Kind kind = Kind::Frames;
        std::vector<ChannelFrame> frames;  ///< Frames：读线程已凑齐的帧（到达序）
        std::uint32_t exitCode = 0;        ///< Exited：GetExitCodeProcess 观测值
        std::string detail;                ///< ChannelError：错误明细（开发诊断）
        DWORD win32Error = 0;              ///< ChannelError：系统错误码
    };

    WorkerRecord(WorkerId workerId, WorkerSupervisor::ClockFn clock,
                 FrameSequencer::Config seqConfig)
        : id(workerId)
        , sequencer(seqConfig, std::move(clock))
    {
    }

    WorkerId id;                       ///< 实例标识
    std::uint32_t pid = 0;             ///< 主进程 PID（观测面/临时目录名成分）
    core::TaskIdentity identity;       ///< 当前绑定五元组（帧身份块核对值）
    WorkerStatus::Phase phase = WorkerStatus::Phase::Booting;  ///< 相位（调度线程域）
    WorkerAssignment assignment;       ///< 当前派发绑定
    JobScope job;                      ///< 作业对象 RAII（KILL_ON_JOB_CLOSE——§6.6）
    HANDLE processHandle = nullptr;    ///< 主进程句柄（调用方所有——本记录 RAII 关闭）
    std::unique_ptr<ChannelPair> channel;  ///< 管道对（服务端在主进程）
    std::thread reader;                ///< 通道读线程（§6.2"通道读线程"）
    std::unique_ptr<HandleAdapter> handle;  ///< TaskController 绑定面（随记录存活）

    std::mutex inboxMutex;             ///< 收件箱互斥（读线程推 ↔ 调度线程弹）
    std::deque<InboxItem> inbox;       ///< 收件箱（FIFO——同 worker 帧序保证）

    // ---- 协议状态（仅调度线程——poll 域） ----
    FrameSequencer sequencer;          ///< seq 重排/去重/断裂（EX-CHN-1 判定本体）
    MessageReassembler reassembler;    ///< 分帧续传重组（DispatchRequest/FinalOutput）
    std::uint64_t outSeq = 1;          ///< 命令流发送序号（单调）
    bool handshakeDone = false;        ///< 握手完成（Hello/HelloAck 已过）
    bool protocolFailed = false;       ///< 协议错误闩（fatal 触发后本通道废弃）
    bool hungJudged = false;           ///< 已出 WorkerHung 判定（恰一次）
    bool forceKillIssued = false;      ///< 已强杀（恰一次——§10.3 terminateForce）
    bool exitProcessed = false;        ///< 退出已分类（恰一次）
    bool cancelSettledFlag = false;    ///< 收到 CancelAck（IWorkerHandle::cancelSettled）
    bool pendingDestroy = false;       ///< 记录待整体销毁（回收路径出清）
    std::optional<Clock::time_point> lastHeartbeat;   ///< 最近心跳（注入时钟域）
    std::optional<Clock::time_point> lastActivity;    ///< 最近任意帧（"管道无数据"半区）
    Clock::time_point idleSince{};     ///< Idle 起点（空闲回收计时）
    std::uint32_t lastExitCode = 0;    ///< 退出码（墓碑观测面）
    ExitClassification lastClassification = ExitClassification::Crashed;  ///< 分类（墓碑观测面）
    std::uint64_t tasksServed = 0;     ///< 累计服务任务数（池化复用计数）

    /// 关闭进程资源（Dead/销毁路径共用；读线程须先 join）。
    void closeProcessResources()
    {
        if (processHandle != nullptr) {
            ::CloseHandle(processHandle);
            processHandle = nullptr;
        }
        channel.reset();  // 服务端句柄关闭——管道断裂是 worker 侧正常归宿
    }
};

// =====================================================================
// HandleAdapter——IWorkerHandle 适配（EX-T03 TaskController 绑定面）
// =====================================================================

/**
 * @brief 把监督器的 worker 操作面适配为取消协议的 IWorkerHandle（§10.3
 *        单 worker 视口 ↔ §10.2 编排面的桥；EX-T03 Controller.hpp 注：
 *        "EX-T06 WorkerSupervisor/JobScope 适配实现"）。
 *
 * 线程约束：四个方法全部仅调度线程调用（Controller poll 域＝supervisor
 *   poll 域——同一调度线程，转发无须加锁）。
 */
class WorkerSupervisor::HandleAdapter final : public IWorkerHandle {
public:
    HandleAdapter(WorkerSupervisor* owner, WorkerId worker)
        : m_owner(owner)
        , m_worker(worker)
    {
    }

    void requestCooperativeCancel() override { m_owner->requestCooperativeCancel(m_worker); }

    bool cancelSettled() const override { return m_owner->workerCancelSettled(m_worker); }

    void terminateForce(TerminationCause cause) override
    {
        m_owner->terminateForce(m_worker, cause);
    }

    bool isAlive() const override { return m_owner->workerAlive(m_worker); }

private:
    WorkerSupervisor* m_owner;  ///< 非所有权——监督器持有适配器（存活期覆盖）
    WorkerId m_worker;          ///< 目标实例
};

// =====================================================================
// 构造 / 析构
// =====================================================================

WorkerSupervisor::WorkerSupervisor(std::wstring executablePath, Config config, ClockFn clock)
    : m_executablePath(std::move(executablePath))
    , m_config(config)
    , m_clock(std::move(clock))
{
}

WorkerSupervisor::~WorkerSupervisor()
{
    // 析构＝全池终结：强杀每棵进程树（terminate 幂等——已死的无副作用）
    // →join 读线程（进程退出使其自然收尾）→关闭句柄。KILL_ON_JOB_CLOSE
    // 在 JobScope 析构时兜杀残留（不留孤儿 worker，§6.6）。
    for (auto& [id, record] : m_workers) {
        (void)id;
        if (!record->exitProcessed) {
            record->job.terminate(win32::kForceTerminateExitCode);
        }
        if (record->reader.joinable()) {
            record->reader.join();
        }
        record->closeProcessResources();
    }
}

void WorkerSupervisor::setEventSink(IWorkerEventSink* sink) noexcept
{
    m_sink = sink;
}

// =====================================================================
// launch（§10.3——池取或新建；调度线程）
// =====================================================================

WorkerLaunchResult WorkerSupervisor::launch(const WorkerAssignment& assignment)
{
    // 前置自查（§10.3 前置注的执行面）：五元组/派发身份必须有效——
    // 调用方拼装违约 fail-fast。
    if (!assignment.identity.isValid() || !assignment.dispatch.snapshotIdentity.isValid()
        || !assignment.dispatch.modelIdentity.isValid()) {
        throw ExecutionError(ExecutionErrorCode::InvalidState,
                             "execution/worker-launch: assignment identity invalid");
    }

    // 池查找：Idle 且握手完成且 manifest 一致（worker 进程的装配在启动时
    // 固定——manifest 不同的派发不能复用，必须新进程，§6.4 池化边界）。
    for (auto& [id, record] : m_workers) {
        (void)id;
        if (record->phase == WorkerStatus::Phase::Idle && record->handshakeDone
            && !record->exitProcessed && !record->protocolFailed
            && record->assignment.manifestDigest == assignment.manifestDigest) {
            return reuseIdleWorker(*record, assignment);
        }
    }
    return spawnNewWorker(assignment);
}

WorkerLaunchResult WorkerSupervisor::spawnNewWorker(const WorkerAssignment& assignment)
{
    // WorkerId 分配（单调自增；复用的进程沿用原 id——id 跟随记录不跟随任务）。
    const WorkerId workerId{m_nextWorkerId.value++};

    auto record = std::make_unique<WorkerRecord>(
        workerId, m_clock,
        m_config.channelSequencer);  // 序控参数经 Config 透传（窗口/缺口超时——实现参数）
    record->identity = assignment.identity;
    record->assignment = assignment;
    record->tasksServed = 1;

    // 管道名基底：主进程 pid＋worker id 保证全局唯一（跨监督器/跨会话
    // 不冲突——命名管道是内核全局命名空间）。
    const std::wstring baseName =
        L"ird-exec-" + std::to_wstring(::GetCurrentProcessId()) + L"-"
        + std::to_wstring(workerId.value);

    // 绑定五元组命令行值（五个规范文本 '|' 连接——握手帧身份块的来源；
    // 规范文本只含 [a-z0-9-]，'|' 作分隔符无歧义）。各段先落命名局部量
    // ——迭代器对必须取自同一字符串实例，跨临时量取迭代器是未定义行为。
    const std::string projCanon = assignment.identity.project.toCanonical();
    const std::string brCanon = assignment.identity.branch.toCanonical();
    const std::string revCanon = assignment.identity.revision.toCanonical();
    const std::string runCanon = assignment.identity.run.toCanonical();
    const std::string attCanon = assignment.identity.attempt.toCanonical();
    const std::wstring identityArg =
        std::wstring(projCanon.begin(), projCanon.end()) + L"|"
        + std::wstring(brCanon.begin(), brCanon.end()) + L"|"
        + std::wstring(revCanon.begin(), revCanon.end()) + L"|"
        + std::wstring(runCanon.begin(), runCanon.end()) + L"|"
        + std::wstring(attCanon.begin(), attCanon.end());

    // 启动时序（见 ProcessLauncher.hpp：挂起→作业→Resume→关副本）。
    win32::RealProcessOps ops;
    win32::LaunchResult launched =
        win32::launchWorkerProcess(m_executablePath, baseName, identityArg, ops, record->job);
    if (!launched.ok || launched.channel == nullptr) {
        // 启动失败：EX-WORKER-LAUNCH-FAILED（§6.4 退出码表 10 行的主进程
        // 侧同源诊断——进程没起来，编排方 Preparing→Failed）。
        WorkerLaunchResult failure;
        failure.ok = false;
        failure.diagnostics.push_back(launchFailedDiag(
            "CreateProcessW/channel setup failed, win32 error " + std::to_string(launched.lastError)
            + ", pipe base " + std::string(baseName.begin(), baseName.end())));
        return failure;
    }
    record->pid = launched.pid;
    record->processHandle = launched.processHandle;
    record->channel = std::move(launched.channel);
    record->handle = std::make_unique<HandleAdapter>(this, workerId);

    // 心跳/活动时戳初始化为启动时刻（Booting 相不做失联判定——握手前
    // worker 可能尚在装载运行时；失联判定只在 Running 相，EX-WKR-3 口径）。
    const auto now = clockNow(m_clock);
    record->lastHeartbeat = now;
    record->lastActivity = now;

    // 读线程启动（§6.2"通道读线程"——字节→帧→收件箱）。
    WorkerRecord* raw = record.get();
    raw->reader = std::thread([this, raw]() { readerLoop(*raw); });

    // 状态投影（并发只读面——锁内镜像）。
    {
        std::lock_guard<std::mutex> lock(m_statusMutex);
        WorkerStatus& view = m_statusView[workerId.value];
        view.id = workerId;
        view.pid = record->pid;
        view.currentRun = assignment.identity.run;
        view.phase = WorkerStatus::Phase::Booting;
        view.lastHeartbeatUtc = std::chrono::system_clock::now();
        view.tasksServed = record->tasksServed;
    }

    m_workers.emplace(workerId.value, std::move(record));

    WorkerLaunchResult result;
    result.ok = true;
    result.worker = workerId;
    return result;
}

WorkerLaunchResult WorkerSupervisor::reuseIdleWorker(WorkerRecord& record,
                                                     const WorkerAssignment& a)
{
    // 池化复用（§6.4"进程保活，跨任务复用"）：握手已完成（manifest 同）
    // ——跳过 Hello/HelloAck，直接换绑定＋派发。序控/重组状态**延续**
    // （worker 的数据流 seq 不重置——接收侧基线持续有效）。
    record.identity = a.identity;
    record.assignment = a;
    record.phase = WorkerStatus::Phase::Running;
    record.tasksServed += 1;
    record.hungJudged = false;
    record.forceKillIssued = false;
    record.exitProcessed = false;
    record.cancelSettledFlag = false;
    record.pendingDestroy = false;
    const auto now = clockNow(m_clock);
    record.lastHeartbeat = now;
    record.lastActivity = now;

    const bool dispatched = sendDispatchRequest(record);
    {
        std::lock_guard<std::mutex> lock(m_statusMutex);
        WorkerStatus& view = m_statusView[record.id.value];
        view.currentRun = a.identity.run;
        view.phase = WorkerStatus::Phase::Running;
        view.tasksServed = record.tasksServed;
        view.lastHeartbeatUtc = std::chrono::system_clock::now();
    }
    if (!dispatched) {
        // 派发写失败＝通道已断（空闲期 worker 悄悄死掉的边缘序列）——
        // 按启动失败处置（编排方 Preparing→Failed，重试拿新进程）。
        WorkerLaunchResult failure;
        failure.ok = false;
        failure.worker = record.id;
        failure.diagnostics.push_back(launchFailedDiag(
            "idle worker channel broken on redispatch (pid " + std::to_string(record.pid)
            + ")"));
        return failure;
    }
    WorkerLaunchResult result;
    result.ok = true;
    result.worker = record.id;
    return result;
}

// =====================================================================
// 命令发送（调度线程——写命令通道）
// =====================================================================

bool WorkerSupervisor::sendFrame(WorkerRecord& record, const ChannelFrame& frame) const
{
    if (record.channel == nullptr) {
        return false;
    }
    const std::vector<std::uint8_t> bytes = encodeFrame(frame);
    // 有界等待写（Config.commandTimeoutMs——worker 卡死时调度线程不被
    // 拖死；§6.6"不无限阻塞"对命令方向同样成立）。
    return record.channel->writeCommand(bytes.data(), bytes.size(),
                                         static_cast<DWORD>(m_config.commandTimeoutMs));
}

bool WorkerSupervisor::sendDispatchRequest(WorkerRecord& record)
{
    // 派发物消息体（§3.3 MaterializedDispatch 的线路形态——execution 只
    // 透传字节，P-EX-3）。
    DispatchRequestPayload payload;
    payload.snapshotIdentity = record.assignment.dispatch.snapshotIdentity;
    payload.modelIdentity = record.assignment.dispatch.modelIdentity;
    payload.snapshotBytes = record.assignment.dispatch.snapshotBytes;
    payload.modelBytes = record.assignment.dispatch.modelBytes;
    const std::vector<std::uint8_t> message = encodeDispatchRequest(payload);

    // 大载荷分帧续传（§6.5"NFR-PERF-03 不整体装载"——Config.
    // dispatchFragmentBytes 分片；seq 为帧级连续）。
    std::vector<ChannelFrame> frames;
    if (!fragmentMessage(ChannelMsgType::DispatchRequest, record.identity, record.outSeq,
                         message, m_config.dispatchFragmentBytes, frames)) {
        record.protocolFailed = true;
        emit(WorkerEvent{WorkerEvent::Kind::ProtocolViolation, record.id, record.identity,
                         std::nullopt, 0, 0, {}, "EX-CHANNEL-PROTOCOL-ERROR",
                         "dispatch request fragmentation rejected", true, 0,
                         ExitClassification::Crashed});
        return false;
    }
    for (const ChannelFrame& frame : frames) {
        if (!sendFrame(record, frame)) {
            return false;  // 写失败＝通道断（worker 已死/卡死——按终结路径走）
        }
    }
    return true;
}

void WorkerSupervisor::sendShutdown(WorkerRecord& record)
{
    // 空闲回收的协作退出请求（§6.4"空闲超时回收"——worker 宿主收到后
    // 以退出码 0 自然退出；写失败忽略：worker 多半已死，读线程会出
    // Exited 记录）。
    ChannelFrame frame;
    frame.type = ChannelMsgType::Shutdown;
    frame.seq = record.outSeq++;
    frame.identity = record.identity;
    (void)sendFrame(record, frame);
}

// =====================================================================
// IWorkerSupervisor 其余操作（§10.3；调度线程）
// =====================================================================

void WorkerSupervisor::requestCooperativeCancel(WorkerId worker)
{
    const auto it = m_workers.find(worker.value);
    if (it == m_workers.end()) {
        // 未知 worker＝调用方登记面违约（§10.8——fail-fast）。
        throw ExecutionError(ExecutionErrorCode::InvalidState,
                             "execution/worker-cancel: unknown worker id");
    }
    WorkerRecord& record = *it->second;
    if (record.exitProcessed) {
        return;  // 已退出——幂等 no-op（取消对已终结 worker 无物可操作）
    }
    ChannelFrame frame;
    frame.type = ChannelMsgType::CancelRequest;
    frame.seq = record.outSeq++;
    frame.identity = record.identity;
    (void)sendFrame(record, frame);  // 写失败＝通道断——协作取消退化为强杀路径（上层协议窗超时兜底）
}

void WorkerSupervisor::terminateForce(WorkerId worker, TerminationCause /*cause*/)
{
    const auto it = m_workers.find(worker.value);
    if (it == m_workers.end()) {
        throw ExecutionError(ExecutionErrorCode::InvalidState,
                             "execution/worker-terminate: unknown worker id");
    }
    WorkerRecord& record = *it->second;
    if (record.forceKillIssued || record.exitProcessed) {
        return;  // 恰一次防线（§10.3"terminateForce 恰一次"；已退出无树可杀）
    }
    record.forceKillIssued = true;
    // TerminateJobObject 整树强杀（§6.6 强制终止行——异步语义：退出确认
    // 以读线程的 Exited 记录为准，本调用不等待）。
    record.job.terminate(win32::kForceTerminateExitCode);
}

WorkerStatus WorkerSupervisor::status(WorkerId worker) const
{
    std::lock_guard<std::mutex> lock(m_statusMutex);
    const auto it = m_statusView.find(worker.value);
    if (it == m_statusView.end()) {
        WorkerStatus unknown;
        unknown.phase = WorkerStatus::Phase::Dead;  // 未知 id＝不存在→Dead 空投影（头注口径）
        return unknown;
    }
    return it->second;  // 深拷贝快照（§10.3 并发只读）
}

std::vector<WorkerStatus> WorkerSupervisor::list() const
{
    std::lock_guard<std::mutex> lock(m_statusMutex);
    std::vector<WorkerStatus> out;
    out.reserve(m_statusView.size());
    for (const auto& [id, view] : m_statusView) {
        out.push_back(view);
    }
    // 稳定序（id 升序——观测面确定性，NFR-COR-02 同源精神）。
    std::sort(out.begin(), out.end(),
              [](const WorkerStatus& l, const WorkerStatus& r) { return l.id.value < r.id.value; });
    return out;
}

std::size_t WorkerSupervisor::workerCount() const noexcept
{
    return m_workers.size();  // 仅调度线程（头注——任意线程观测走 list()）
}

std::uint64_t WorkerSupervisor::aggregateJobMemoryBytes() const
{
    // EX-T07（§6.6 内存采样行 JobMemory 半区）：全部活 worker 的作业提交
    // 内存峰值求和——ResourceController 生产采样源（SupervisorMemorySampler）
    // 的 worker 半区消费口。仅调度线程（遍历 m_workers 记录域——头注）。
    std::uint64_t total = 0;
    for (const auto& [id, record] : m_workers) {
        // 参与聚合的资格＝作业对象有效且主进程未终结（processHandle 非空
        // ——Dead/已出清记录的作业句柄已随 RAII 关闭，job.valid() 为假或
        // 查询必失败）。查询失败的条目计 0：尽力求和不毒化整体读数——
        // "半读数"的防混淆语义由采样器层的整体失败通道承担（其注）。
        if (record->job.valid() && record->processHandle != nullptr) {
            std::uint64_t jobBytes = 0;
            if (win32::queryJobPeakCommittedBytes(record->job.handle(), jobBytes)) {
                total += jobBytes;
            }
        }
    }
    return total;
}

std::size_t WorkerSupervisor::reclaimIdleWorkers()
{
    // EX-T07（§6.1 内存预算行"降低并行度（回收空闲 worker）"的池侧执行
    // 面）：对全部 Idle 相位记录发起回收，按进程存亡分两支——仅调度线程
    // （记录域遍历——头注）。
    //
    // 支 1（阶段 A worker 模型——worker 宿主每任务终结即退出，约定码 0）：
    //   Idle 记录的进程已不存在（读线程已收 Exited→processExit→回池），
    //   "回收"＝直接出清记录（与 poll 尾部销毁完全同序：join 读线程→关
    //   进程资源→双表 erase）。对死进程走 Shutdown 协议是死路：写静默
    //   失败且永远等不到第二次 Exited，记录会滞留——因此按存亡分流。
    // 支 2（§6.4"进程保活"模型——Idle 记录进程仍在）：协作退出协议，
    //   与 checkIdleRecycle 完全同路径（置 Draining＋状态镜像＋Shutdown
    //   请求→自然退出→poll 收割）。
    //
    // 处置面仅 Idle（在途 worker 是"并行度"本身——其新增已被调度侧资源
    // 闸停派发承载，两侧合起来才是 §6.1"停派发＋降并行"的完整执行面）。
    std::vector<std::uint64_t> reapedIds;   // 支 1 的出清清单（循环后统一 erase——遍历中擦除会失效迭代器）
    std::size_t reclaimed = 0;
    for (auto& [id, record] : m_workers) {
        if (record->phase != WorkerStatus::Phase::Idle) {
            continue;
        }
        if (record->exitProcessed) {
            // 支 1：进程已终结的池槽——直接出清（join→关资源→erase）。
            if (record->reader.joinable()) {
                record->reader.join();
            }
            record->closeProcessResources();
            reapedIds.push_back(id);
        } else {
            // 支 2：进程仍存活的空闲 worker——协作退出（同 checkIdleRecycle；
            // worker 以 0 退出→processExit（Draining 分支）→pendingDestroy→
            // poll 尾部出清）。
            record->phase = WorkerStatus::Phase::Draining;
            {
                std::lock_guard<std::mutex> lock(m_statusMutex);
                m_statusView[record->id.value].phase = WorkerStatus::Phase::Draining;
            }
            sendShutdown(*record);
        }
        ++reclaimed;
    }
    if (!reapedIds.empty()) {
        for (const std::uint64_t reapedId : reapedIds) {
            m_workers.erase(reapedId);
        }
        std::lock_guard<std::mutex> lock(m_statusMutex);
        for (const std::uint64_t reapedId : reapedIds) {
            m_statusView.erase(reapedId);
        }
    }
    return reclaimed;
}

bool WorkerSupervisor::hasOpenChildHandleDuplicates(WorkerId worker) const
{
    // R-3 观测面（头注）：通道副本在 ProcessLauncher 第 6 步即关闭——
    // 启动完成后本查询恒 false；记录已释放（channel 空）同样视为无泄漏。
    const auto it = m_workers.find(worker.value);
    if (it == m_workers.end() || it->second->channel == nullptr) {
        return false;
    }
    return it->second->channel->hasOpenChildEndDuplicates();
}

IWorkerHandle* WorkerSupervisor::workerHandle(WorkerId worker)
{
    const auto it = m_workers.find(worker.value);
    if (it == m_workers.end()) {
        throw ExecutionError(ExecutionErrorCode::InvalidState,
                             "execution/worker-handle: unknown worker id");
    }
    return it->second->handle.get();
}

// 监督器内部方法（HandleAdapter 转发目标——调度线程域）。
bool WorkerSupervisor::workerCancelSettled(WorkerId worker) const
{
    const auto it = m_workers.find(worker.value);
    return it != m_workers.end() && it->second->cancelSettledFlag;
}

bool WorkerSupervisor::workerAlive(WorkerId worker) const
{
    const auto it = m_workers.find(worker.value);
    if (it == m_workers.end() || it->second->exitProcessed
        || it->second->processHandle == nullptr) {
        return false;
    }
    // 存活探测（EX-WKR-4 观测点"进程存活探测"——句柄零超时等待）。
    return ::WaitForSingleObject(it->second->processHandle, 0) == WAIT_TIMEOUT;
}

// =====================================================================
// poll——调度线程显式驱动（收件箱→失联→回收→销毁清账）
// =====================================================================

void WorkerSupervisor::poll()
{
    const auto now = clockNow(m_clock);
    std::vector<std::uint64_t> destroyed;
    for (auto& [id, record] : m_workers) {
        (void)id;
        drainInbox(*record);
        // 缺口超时探测独立于帧到达（缺口后 worker 停发帧的场景也要能到
        // 点判定——EX-CHN-1 断裂面的时钟半区）。
        if (!record->protocolFailed && record->sequencer.gapTimedOut()) {
            record->protocolFailed = true;
            record->forceKillIssued = true;
            record->job.terminate(win32::kForceTerminateExitCode);
            emit(WorkerEvent{WorkerEvent::Kind::ProtocolViolation, record->id,
                             record->identity, std::nullopt, 0, 0, {},
                             "EX-CHANNEL-PROTOCOL-ERROR", "sequence gap timed out", true, 0,
                             ExitClassification::Crashed});
        }
        checkHeartbeatLoss(*record, now);
        checkIdleRecycle(*record, now);
        if (record->pendingDestroy) {
            destroyed.push_back(record->id.value);
        }
    }
    // 销毁清账（迭代后移除——poll 遍历稳定性）。
    for (const std::uint64_t id : destroyed) {
        auto it = m_workers.find(id);
        if (it == m_workers.end()) {
            continue;
        }
        if (it->second->reader.joinable()) {
            it->second->reader.join();
        }
        it->second->closeProcessResources();
        m_workers.erase(it);
        std::lock_guard<std::mutex> lock(m_statusMutex);
        m_statusView.erase(id);
    }
}

void WorkerSupervisor::drainInbox(WorkerRecord& record)
{
    for (;;) {
        std::unique_ptr<WorkerRecord::InboxItem> item;
        {
            std::lock_guard<std::mutex> lock(record.inboxMutex);
            if (record.inbox.empty()) {
                return;
            }
            item = std::make_unique<WorkerRecord::InboxItem>(std::move(record.inbox.front()));
            record.inbox.pop_front();
        }
        switch (item->kind) {
        case WorkerRecord::InboxItem::Kind::Frames:
            deliverSequenced(record, std::move(item->frames));
            break;
        case WorkerRecord::InboxItem::Kind::Exited:
            processExit(record, item->exitCode);
            break;
        case WorkerRecord::InboxItem::Kind::ChannelError:
            // 读线程级系统错误/帧装配失败＝通道不可信——fatal 协议错误
            // （EX-CHN-1 断裂面的另一来源；编排方使尝试 Failed）。
            record.protocolFailed = true;
            emit(WorkerEvent{WorkerEvent::Kind::ProtocolViolation, record.id,
                             record.identity, std::nullopt, 0, 0, {},
                             "EX-CHANNEL-PROTOCOL-ERROR",
                             "channel reader error: " + item->detail, true, 0,
                             ExitClassification::Crashed});
            break;
        }
        if (record.protocolFailed && !record.forceKillIssued && !record.exitProcessed) {
            // fatal 协议错误后 worker 通道已不可信——监督器就地强杀
            // （进程已无继续服务的可能；§6.5"断裂→通道错误→尝试 Failed"
            // 的执行侧闭环：任务失败由编排方按事件驱动，进程由监督器清）。
            record.forceKillIssued = true;
            record.job.terminate(win32::kForceTerminateExitCode);
        }
    }
}

void WorkerSupervisor::deliverSequenced(WorkerRecord& record,
                                        std::vector<ChannelFrame>&& frames)
{
    for (ChannelFrame& frame : frames) {
        if (record.protocolFailed) {
            return;  // 协议错误闩——余帧全部静默丢弃（错误已上报）
        }
        FrameSequencer::Outcome outcome;
        std::vector<ChannelFrame> delivered = record.sequencer.accept(std::move(frame), &outcome);
        switch (outcome) {
        case FrameSequencer::Outcome::DuplicateDropped:
            // 重复/回退：丢弃＋开发诊断（非致命——§6.5"重复/回退→丢弃
            // ＋开发诊断"，通道继续）。
            emit(WorkerEvent{WorkerEvent::Kind::ProtocolViolation, record.id,
                             record.identity, std::nullopt, 0, 0, {},
                             "EX-CHANNEL-PROTOCOL-ERROR",
                             "duplicate/out-of-window seq dropped", false, 0,
                             ExitClassification::Crashed});
            break;
        case FrameSequencer::Outcome::WindowOverflow:
            // 窗口溢出/保留值 seq：断裂面（致命——EX-CHN-1）。
            record.protocolFailed = true;
            emit(WorkerEvent{WorkerEvent::Kind::ProtocolViolation, record.id,
                             record.identity, std::nullopt, 0, 0, {},
                             "EX-CHANNEL-PROTOCOL-ERROR",
                             "sequencer window overflow", true, 0,
                             ExitClassification::Crashed});
            return;
        case FrameSequencer::Outcome::Delivered:
        case FrameSequencer::Outcome::Buffered:
            // Delivered：交付本拍连成的连续段；Buffered：窗口内暂存。
            for (ChannelFrame& inOrder : delivered) {
                if (record.protocolFailed) {
                    return;
                }
                handleCompleteFrame(record, std::move(inOrder));
            }
            break;
        }
    }
    // 缺口超时探测（断裂的时钟面——配置 gapTimeout 到点未补齐）。
    if (!record.protocolFailed && record.sequencer.gapTimedOut()) {
        record.protocolFailed = true;
        emit(WorkerEvent{WorkerEvent::Kind::ProtocolViolation, record.id, record.identity,
                         std::nullopt, 0, 0, {}, "EX-CHANNEL-PROTOCOL-ERROR",
                         "sequence gap timed out", true, 0, ExitClassification::Crashed});
    }
}

void WorkerSupervisor::handleCompleteFrame(WorkerRecord& record, ChannelFrame&& frame)
{
    // 帧身份块与当前绑定核对（通道串扰/错绑在此暴露——ARCH §4.1 五元组
    // 随行承诺的执行侧核对；worker 只会为本绑定发帧）。
    if (!(frame.identity == record.identity)) {
        record.protocolFailed = true;
        emit(WorkerEvent{WorkerEvent::Kind::ProtocolViolation, record.id, record.identity,
                         std::nullopt, 0, 0, {}, "EX-CHANNEL-PROTOCOL-ERROR",
                         "frame identity does not match worker binding", true, 0,
                         ExitClassification::Crashed});
        return;
    }
    bool broken = false;
    std::vector<ChannelFrame> messages = record.reassembler.accept(std::move(frame), &broken);
    if (broken) {
        record.protocolFailed = true;
        emit(WorkerEvent{WorkerEvent::Kind::ProtocolViolation, record.id, record.identity,
                         std::nullopt, 0, 0, {}, "EX-CHANNEL-PROTOCOL-ERROR",
                         "fragment reassembly failed", true, 0, ExitClassification::Crashed});
        return;
    }
    for (ChannelFrame& message : messages) {
        handleFrame(record, std::move(message));
        if (record.protocolFailed) {
            return;
        }
    }
}

void WorkerSupervisor::handleFrame(WorkerRecord& record, ChannelFrame&& frame)
{
    record.lastActivity = clockNow(m_clock);  // "管道有数据"半区刷新（§6.5 失联判定）
    switch (frame.type) {
    case ChannelMsgType::Heartbeat: {
        record.lastHeartbeat = clockNow(m_clock);
        std::lock_guard<std::mutex> lock(m_statusMutex);
        m_statusView[record.id.value].lastHeartbeatUtc = std::chrono::system_clock::now();
        break;
    }
    case ChannelMsgType::Hello:
        if (record.handshakeDone) {
            // 重复 Hello＝宿主异常（开发诊断，非致命）。
            emit(WorkerEvent{WorkerEvent::Kind::ProtocolViolation, record.id,
                             record.identity, std::nullopt, 0, 0, {},
                             "EX-CHANNEL-PROTOCOL-ERROR", "repeated Hello after handshake",
                             false, 0, ExitClassification::Crashed});
        } else {
            handleHandshake(record, std::move(frame));
        }
        break;
    case ChannelMsgType::DispatchAccept:
        emit(WorkerEvent{WorkerEvent::Kind::DispatchAccepted, record.id, record.identity,
                         std::nullopt, 0, 0, {}, "", "", false, 0,
                         ExitClassification::Crashed});
        break;
    case ChannelMsgType::Progress: {
        const std::optional<ProgressReport> report = decodeProgress(frame.payload);
        if (!report.has_value()) {
            // 帧级解码校验失败（值域违约）＝协议错误（致命——§6.5"帧级
            // 靠长度＋解码校验"）。
            record.protocolFailed = true;
            emit(WorkerEvent{WorkerEvent::Kind::ProtocolViolation, record.id,
                             record.identity, std::nullopt, 0, 0, {},
                             "EX-CHANNEL-PROTOCOL-ERROR", "progress payload invalid", true,
                             0, ExitClassification::Crashed});
            break;
        }
        WorkerEvent event;
        event.kind = WorkerEvent::Kind::Progress;
        event.worker = record.id;
        event.identity = record.identity;
        event.progress = report;
        emit(std::move(event));
        break;
    }
    case ChannelMsgType::ResultBatch: {
        const std::optional<ResultBatchPayload> batch = decodeResultBatch(frame.payload);
        if (!batch.has_value()) {
            record.protocolFailed = true;
            emit(WorkerEvent{WorkerEvent::Kind::ProtocolViolation, record.id,
                             record.identity, std::nullopt, 0, 0, {},
                             "EX-CHANNEL-PROTOCOL-ERROR", "result batch payload invalid",
                             true, 0, ExitClassification::Crashed});
            break;
        }
        WorkerEvent event;
        event.kind = WorkerEvent::Kind::ResultBatch;
        event.worker = record.id;
        event.identity = record.identity;
        event.batchIndex = batch->batchIndex;
        event.bytes = std::move(batch->bytes);
        emit(std::move(event));
        break;
    }
    case ChannelMsgType::CheckpointBatch: {
        const std::optional<CheckpointBatchPayload> batch =
            decodeCheckpointBatch(frame.payload);
        if (!batch.has_value() || batch->sequence == 0) {
            record.protocolFailed = true;
            emit(WorkerEvent{WorkerEvent::Kind::ProtocolViolation, record.id,
                             record.identity, std::nullopt, 0, 0, {},
                             "EX-CHANNEL-PROTOCOL-ERROR",
                             "checkpoint batch payload invalid", true, 0,
                             ExitClassification::Crashed});
            break;
        }
        WorkerEvent event;
        event.kind = WorkerEvent::Kind::CheckpointBatch;
        event.worker = record.id;
        event.identity = record.identity;
        event.checkpointSequence = batch->sequence;
        event.bytes = std::move(batch->bytes);
        emit(std::move(event));
        break;
    }
    case ChannelMsgType::FinalOutput: {
        const std::optional<FinalOutputPayload> output = decodeFinalOutput(frame.payload);
        if (!output.has_value()) {
            record.protocolFailed = true;
            emit(WorkerEvent{WorkerEvent::Kind::ProtocolViolation, record.id,
                             record.identity, std::nullopt, 0, 0, {},
                             "EX-CHANNEL-PROTOCOL-ERROR", "final output payload invalid",
                             true, 0, ExitClassification::Crashed});
            break;
        }
        // 通道级绑定核对：自报身份须与派发声明一致（worker 自报不可信
        // ——真正的接纳判定以登记五元组为准；此处只拦通道错乱）。
        if (!(output->snapshotIdentity == record.assignment.dispatch.snapshotIdentity)
            || !(output->modelIdentity == record.assignment.dispatch.modelIdentity)) {
            record.protocolFailed = true;
            emit(WorkerEvent{WorkerEvent::Kind::ProtocolViolation, record.id,
                             record.identity, std::nullopt, 0, 0, {},
                             "EX-CHANNEL-PROTOCOL-ERROR",
                             "final output self-reported identity mismatch", true, 0,
                             ExitClassification::Crashed});
            break;
        }
        WorkerEvent event;
        event.kind = WorkerEvent::Kind::FinalOutput;
        event.worker = record.id;
        event.identity = record.identity;
        event.bytes = std::move(output->outputCanon);
        emit(std::move(event));
        break;
    }
    case ChannelMsgType::ErrorReport: {
        const std::optional<ErrorReportPayload> report = decodeErrorReport(frame.payload);
        if (!report.has_value()) {
            record.protocolFailed = true;
            emit(WorkerEvent{WorkerEvent::Kind::ProtocolViolation, record.id,
                             record.identity, std::nullopt, 0, 0, {},
                             "EX-CHANNEL-PROTOCOL-ERROR", "error report payload invalid",
                             true, 0, ExitClassification::Crashed});
            break;
        }
        // dev=true（开发通道）→ stableCode 置空、detail 携带通道名＋文本
        // ——编排方按"空稳定码＝reportDev"分流（§6.4 诊断经通道回传）。
        WorkerEvent event;
        event.kind = WorkerEvent::Kind::ErrorReport;
        event.worker = record.id;
        event.identity = record.identity;
        if (report->dev) {
            event.stableCode = "";
            event.detail = report->codeOrChannel + ": " + report->message;
        } else {
            event.stableCode = report->codeOrChannel;
            event.detail = report->message;
        }
        emit(std::move(event));
        break;
    }
    case ChannelMsgType::CancelAck:
        record.cancelSettledFlag = true;  // 协作窗收敛观测（IWorkerHandle::cancelSettled）
        emit(WorkerEvent{WorkerEvent::Kind::CancelAck, record.id, record.identity,
                         std::nullopt, 0, 0, {}, "", "", false, 0,
                         ExitClassification::Crashed});
        break;
    case ChannelMsgType::PauseAck:
        emit(WorkerEvent{WorkerEvent::Kind::PauseAck, record.id, record.identity,
                         std::nullopt, 0, 0, {}, "", "", false, 0,
                         ExitClassification::Crashed});
        break;
    default:
        // 主→worker 方向的消息出现在数据流＝方向错乱（开发诊断，非致命
        // ——单帧异常，宿主可能仍在正常服务）。
        emit(WorkerEvent{WorkerEvent::Kind::ProtocolViolation, record.id, record.identity,
                         std::nullopt, 0, 0, {}, "EX-CHANNEL-PROTOCOL-ERROR",
                         "unexpected message type on data channel", false, 0,
                         ExitClassification::Crashed});
        break;
    }
}

void WorkerSupervisor::handleHandshake(WorkerRecord& record, ChannelFrame&& frame)
{
    const std::optional<HelloPayload> hello = decodeHello(frame.payload);
    std::string rejectReason;
    if (!hello.has_value()) {
        rejectReason = "Hello payload undecodable";
    } else if (hello->protoVersion != kChannelProtocolVersion) {
        // 主版本失配→握手拒绝（§6.5"消息版本"行——开发诊断面）。
        rejectReason = "protocol version mismatch: worker "
                       + std::to_string(hello->protoVersion) + " vs main "
                       + std::to_string(kChannelProtocolVersion);
    } else if (hello->manifestDigestHex != record.assignment.manifestDigest) {
        // manifest 摘要比对（§6.4 握手——"不一致→拒绝派发→Preparing→
        // Failed"，CON-06/AT-19 装配侧纪律）。
        rejectReason = "evaluator manifest digest mismatch";
    }

    if (!rejectReason.empty()) {
        // 拒绝派发：事件→编排方使任务 Failed；进程就地终结（不可服务的
        // worker 不留）。
        emit(WorkerEvent{WorkerEvent::Kind::HandshakeRejected, record.id, record.identity,
                         std::nullopt, 0, 0, {}, "EX-WORKER-LAUNCH-FAILED", rejectReason,
                         false, 0, ExitClassification::Crashed});
        record.forceKillIssued = true;  // 分类面：监督方终结（区别于 worker 自身异常）
        record.job.terminate(win32::kForceTerminateExitCode);
        return;
    }

    // 握手通过：回 HelloAck（携带心跳间隔——两侧同源 HeartbeatPolicy，
    // D-06），随即派发（§6.4 时序：握手→DispatchRequest）。
    HelloAckPayload ack;
    ack.accepted = true;
    ack.heartbeatIntervalMs =
        static_cast<std::uint32_t>(record.assignment.heartbeat.interval.count());
    ChannelFrame ackFrame;
    ackFrame.type = ChannelMsgType::HelloAck;
    ackFrame.seq = record.outSeq++;
    ackFrame.identity = record.identity;
    ackFrame.payload = encodeHelloAck(ack);
    if (!sendFrame(record, ackFrame) || !sendDispatchRequest(record)) {
        // 握手后的首笔写失败＝通道已断——读线程的 Exited/ChannelError
        // 会跟进；此处只留开发诊断。
        emit(WorkerEvent{WorkerEvent::Kind::ProtocolViolation, record.id, record.identity,
                         std::nullopt, 0, 0, {}, "EX-CHANNEL-PROTOCOL-ERROR",
                         "command channel broken right after handshake", true, 0,
                         ExitClassification::Crashed});
        return;
    }
    record.handshakeDone = true;
    record.phase = WorkerStatus::Phase::Running;
    {
        std::lock_guard<std::mutex> lock(m_statusMutex);
        m_statusView[record.id.value].phase = WorkerStatus::Phase::Running;
    }
}

void WorkerSupervisor::processExit(WorkerRecord& record, std::uint32_t exitCode)
{
    if (record.exitProcessed) {
        return;  // 恰一次（Broken+Exited 双报等边缘序列）
    }
    record.exitProcessed = true;
    record.lastExitCode = exitCode;
    record.lastClassification = classifyWorkerExitCode(exitCode, record.forceKillIssued);

    // 读线程收尾（该线程在推入 Exited 后即退出——join 快速返回）。
    if (record.reader.joinable()) {
        record.reader.join();
    }

    const bool abnormal =
        record.lastClassification == ExitClassification::Crashed
        || record.lastClassification == ExitClassification::ForceTerminatedBySupervisor
        || record.lastClassification == ExitClassification::LaunchFailed
        || record.lastClassification == ExitClassification::HostInternalError
        || record.lastClassification == ExitClassification::EvaluatorFailed;

    if (abnormal) {
        // 异常终结：phase Dead——**永不回池**（§6.4"崩溃的 worker 永不回
        // 池"的结构保证：复用查找只扫 Idle）。进程资源关闭，记录留作墓碑
        // （退出码/分类观测面）。
        record.phase = WorkerStatus::Phase::Dead;
        record.closeProcessResources();
        {
            std::lock_guard<std::mutex> lock(m_statusMutex);
            m_statusView[record.id.value].phase = WorkerStatus::Phase::Dead;
        }
    } else if (record.phase == WorkerStatus::Phase::Draining) {
        // 回收路径的自然退出：整体销毁（pendingDestroy——poll 尾清账）。
        record.pendingDestroy = true;
    } else {
        // 正常终结：回池判定（Idle——可跨任务复用，§6.4 池化）；池满则
        // 销毁（容量治理——实现参数 poolCapacity）。池容量检查在状态锁
        // 内完成（m_statusView 与 list()/status() 并发读的纪律）。
        bool poolHasRoom = false;
        {
            std::lock_guard<std::mutex> lock(m_statusMutex);
            poolHasRoom = m_statusView.size() <= m_config.poolCapacity;
        }
        if (poolHasRoom) {
            record.phase = WorkerStatus::Phase::Idle;
            record.idleSince = clockNow(m_clock);
            std::lock_guard<std::mutex> lock(m_statusMutex);
            m_statusView[record.id.value].phase = WorkerStatus::Phase::Idle;
        } else {
            record.pendingDestroy = true;
        }
    }

    WorkerEvent event;
    event.kind = WorkerEvent::Kind::WorkerExited;
    event.worker = record.id;
    event.identity = record.identity;
    event.exitCode = exitCode;
    event.exit = record.lastClassification;
    emit(std::move(event));
}

void WorkerSupervisor::checkHeartbeatLoss(WorkerRecord& record, Clock::time_point now)
{
    // 失联判定只在 Running 相（握手完成、派发在途、未退出——EX-WKR-3 的
    // 场景边界；Boot/Idle 相无在途评估，卡死无从谈起）。
    if (record.phase != WorkerStatus::Phase::Running || !record.handshakeDone
        || record.exitProcessed || record.hungJudged || record.forceKillIssued
        || record.protocolFailed) {
        return;
    }
    if (!record.lastHeartbeat.has_value() || !record.lastActivity.has_value()) {
        return;
    }
    // 连续 lossThresholdIntervals 个间隔无心跳 **且** 管道无数据→WorkerHung
    // （§6.5"连续 3 个间隔无心跳且管道无数据"；阈值＝HeartbeatPolicy.hung
    // Window——D-06 实现参数非需求值，acceptance 1 明文口径）。
    const auto window = record.assignment.heartbeat.hungWindow();
    if (now - *record.lastHeartbeat < window || now - *record.lastActivity < window) {
        return;
    }
    record.hungJudged = true;
    // 判定事件（EX-WORKER-HUNG——与运行超时的区分在 cause 标记：此处
    // 携带 heartbeat-loss 标记；EX-WKR-5 的运行超时由 Controller 在其
    // 判定诊断中携带 evaluationTimeout 标记——诊断码区分，acceptance 1）。
    emit(WorkerEvent{WorkerEvent::Kind::WorkerHung, record.id, record.identity,
                     std::nullopt, 0, 0, {}, "EX-WORKER-HUNG",
                     "heartbeat loss for " + std::to_string(window.count())
                         + " ms (heartbeat-loss path)",
                     false, 0, ExitClassification::Crashed});
    // 判定即强杀（§7.4"WorkerHung 判定→强杀路径→Failed＋EX-FORCE-
    // TERMINATED"；恰一次防线在 terminateForce 与本标记的双守卫）。
    record.forceKillIssued = true;
    record.job.terminate(win32::kForceTerminateExitCode);
}

void WorkerSupervisor::checkIdleRecycle(WorkerRecord& record, Clock::time_point now)
{
    // 空闲超时回收（§6.4"空闲超时回收——默认 120 s，实现参数"；0＝禁用）。
    if (m_config.idleRecycleAfter.count() <= 0 || record.phase != WorkerStatus::Phase::Idle
        || record.exitProcessed) {
        return;
    }
    if (now - record.idleSince < m_config.idleRecycleAfter) {
        return;
    }
    record.phase = WorkerStatus::Phase::Draining;
    {
        std::lock_guard<std::mutex> lock(m_statusMutex);
        m_statusView[record.id.value].phase = WorkerStatus::Phase::Draining;
    }
    sendShutdown(record);  // 协作退出（worker 以 0 退出→processExit→pendingDestroy）
}

// =====================================================================
// 读线程（§6.2"通道读线程"——字节→帧→收件箱）
// =====================================================================

void WorkerSupervisor::readerLoop(WorkerRecord& record)
{
    // 读线程自持的字节流装配器（帧装配状态属字节流域——不与调度线程的
    // seq 序控/重组混淆，§6.2 分层）。
    FrameAssembler assembler;
    std::vector<ChannelFrame> frames;
    std::vector<std::uint8_t> chunk;

    auto pushFrames = [&record, &frames]() {
        if (frames.empty()) {
            return;
        }
        std::lock_guard<std::mutex> lock(record.inboxMutex);
        record.inbox.push_back(WorkerRecord::InboxItem{WorkerRecord::InboxItem::Kind::Frames, std::move(frames), 0, "",
                                         0});
        frames.clear();
    };
    auto pushExit = [&record](std::uint32_t exitCode) {
        std::lock_guard<std::mutex> lock(record.inboxMutex);
        record.inbox.push_back(WorkerRecord::InboxItem{WorkerRecord::InboxItem::Kind::Exited, {}, exitCode, "", 0});
    };
    auto pushChannelError = [&record](std::string detail, DWORD err) {
        std::lock_guard<std::mutex> lock(record.inboxMutex);
        record.inbox.push_back(
            WorkerRecord::InboxItem{WorkerRecord::InboxItem::Kind::ChannelError, {}, 0, std::move(detail), err});
    };

    if (!record.channel->beginRead()) {
        pushChannelError("initial read post failed", record.channel->lastError());
        return;
    }
    for (;;) {
        // 有界等待片（Config.readerSliceMs）：数据/退出双源唤醒；超时
        // 继续轮询——读线程不无限阻塞（§6.6 管道行）。
        const ChannelPair::WaitResult r =
            record.channel->waitFor(record.processHandle, m_config.readerSliceMs);
        switch (r) {
        case ChannelPair::WaitResult::DataReady:
            chunk.clear();
            record.channel->takeReadBytes(&chunk);
            switch (assembler.feed(chunk.data(), chunk.size(), frames)) {
            case FrameAssembler::Status::Ok:
                pushFrames();
                break;
            case FrameAssembler::Status::ProtocolError:
                pushFrames();  // 先交出错误前的完整帧（事件序完整）
                pushChannelError("frame assembler protocol error", 0);
                return;
            }
            if (!record.channel->repostRead()) {
                pushChannelError("read repost failed", record.channel->lastError());
                return;
            }
            break;
        case ChannelPair::WaitResult::ProcessExited: {
            // 进程退出：收割残留字节→采集退出码→终结读循环。
            const std::vector<std::uint8_t> drained = record.channel->drainAfterExit();
            if (!drained.empty()
                && assembler.feed(drained.data(), drained.size(), frames)
                       == FrameAssembler::Status::ProtocolError) {
                pushFrames();
                pushChannelError("frame assembler protocol error during drain", 0);
                return;
            }
            pushFrames();
            DWORD exitCode = 0;
            if (record.processHandle != nullptr
                && ::GetExitCodeProcess(record.processHandle, &exitCode)) {
                pushExit(static_cast<std::uint32_t>(exitCode));
            } else {
                // 取不到退出码（句柄异常——理论不可达）：按约定集外值
                // 处理（分类 Crashed——保守方向，NFR-REL-02）。
                pushExit(0xFFFFFFFFu);
            }
            return;
        }
        case ChannelPair::WaitResult::Broken: {
            // 数据管道先断而进程句柄未 signaled（worker 自关写端的边缘
            // 形态）：以退出码现状终结（STILL_ACTIVE→约定集外→Crashed，
            // 保守方向）。
            DWORD exitCode = 0xFFFFFFFFu;
            if (record.processHandle != nullptr) {
                ::GetExitCodeProcess(record.processHandle, &exitCode);
            }
            pushExit(static_cast<std::uint32_t>(exitCode));
            return;
        }
        case ChannelPair::WaitResult::Error:
            pushChannelError("wait/read system error", record.channel->lastError());
            return;
        case ChannelPair::WaitResult::Timeout:
            break;  // 继续轮询
        }
    }
}

// =====================================================================
// 事件出口
// =====================================================================

void WorkerSupervisor::emit(WorkerEvent event)
{
    if (m_sink != nullptr) {
        m_sink->onWorkerEvent(event);  // 回调发生在调度线程（头注——编排方无须加锁）
    }
}

}  // namespace sdurws::ird::execution
