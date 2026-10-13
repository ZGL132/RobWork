/**
 * @file   CancelTimingBenchmarkTest.cpp
 * @brief  取消时序基准（performance-baseline §2 B-02／需求 NFR-PERF-02）——
 *         真实独立 worker 进程上的 2 s/10 s 协议时序测量（WP-23-T03）：
 *         取消请求 2 s 内进入 Canceling 并停止派发新批次；普通批次 10 s
 *         内收敛；超时后终止独立工作进程并保留最近检查点。
 *
 * 设计依据：
 *   - doc/industrial-robot-design/performance-baseline.md §2 B-02 行（达标
 *     口径照抄需求原文——2 s/10 s 零私改）、§4.1（硬件/软件版本登记）、
 *     §4.8（报告留痕——原始样本序列全量）、§5.2（本卡挂点）；
 *   - 需求 NFR-PERF-02（REQUIREMENTS §19.3 原文——阈值类判定，B-02 不涉
 *     分位数统计〔performance-baseline §2 统计口径区分〕，判定统计量＝
 *     有效样本最大值：全样本须达标）；
 *   - 任务契约 tasks/foundation/WP-23-T03.json（≙DTB §2.24 行）；
 *   - units/execution.md §7（协作式取消逐步协议——EX-T03 语义本体，被测
 *     对象：TaskController 协议编排＋WorkerSupervisor 真进程操作面）。
 *
 * 与功能用例（CancelProtocolTest）的分工——为什么本基准不用虚拟时钟：
 *   EX-T03 功能用例以 ManualClock 虚拟推进断言协议"正确性"（testkit §6.5
 *   纪律——正确性判据禁止 sleep 后断言）；本基准测量同一协议在**真实墙钟**
 *   下的"时序达标"（NFR-PERF-02 是性能需求——数字必须真实产生，任何虚拟
 *   时钟都不构成时序证据）。两者互补不互替：功能面证"协议对"，本基准证
 *   "协议快"。
 *
 * 测量口径（datasetId ird-perf-b02-cancel@1——manifest 冻结于
 * testdata/performance/b02-cancel-manifest.json，本处常量与 manifest 逐字
 * 一致，报告输出参数供漂移核对）：
 *   - 驱动编排＝本 TU 的 CancelTimingRig：真实 TaskController（缺省
 *     steady_clock——真实墙钟）＋真实 WorkerSupervisor＋真实 worker 进程
 *     （sdurws_ird_execution_worker 阶段 A 脚本替身评估器），驱动线程以
 *     10 ms 周期 poll（"调度线程"的基准形态——生产 poll 周期上限 ≤2 s 由
 *     调度器配置保证，本基准取远小于协议窗的 10 ms 使测量分辨率比被测
 *     阈值高两个数量级以上）；
 *   - 时点采集＝驱动线程内事件到达时刻（steady_clock::now()）——状态机
 *     事件在 poll 内同步发布，观测滞后 ≤1 个 poll 周期（保守方向：实测
 *     值 ≥ 真实转移时刻，不虚低）；
 *   - 判定统计量＝有效样本最大值（阈值类全样本判定），P95/最小值一并
 *     报告作分布参考（不作判据——B-02 非 P95 类基准，禁以分位数表述
 *     达标）；
 *   - 样本量：正常收敛腿 1 次预热（剔除）＋15 有效样本；超时强杀腿 3 次
 *     有效场景（阈值类无统计预处理——每次都须满足，不剔除任何样本）。
 *
 * 替身边界声明（§11 同源纪律——替身不构成业务证明）：
 *   - worker 评估器＝阶段 A 内置脚本替身（execution.md §3.4 登记形态）——
 *     它证明的是"取消协议对真实进程/真实通道的时序"，不是业务评估算法的
 *     时序（业务评估器的批次边界协作形态与脚本替身同构：协作点轮询
 *     ICancelSignal——§7.1 信号传递链）；
 *   - 归档端口＝本 TU 的 DiskArchivePort（真实写盘的测试面替身）——检查
 *     点文件真实落盘、强杀后真实留存；project 存储白名单校验语义归
 *     project 契约测试面（EX-T04/PRJ-T14 覆盖），不在 B-02 测量面。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/execution/ChannelProtocol.hpp>
#include <sdurws/ird/execution/Checkpoint.hpp>
#include <sdurws/ird/execution/Controller.hpp>
#include <sdurws/ird/execution/Ports.hpp>
#include <sdurws/ird/execution/StateMachine.hpp>
#include <sdurws/ird/execution/TaskTypes.hpp>
#include <sdurws/ird/execution/WorkerSupervisor.hpp>
#include <sdurws/ird/project/ArchivePort.hpp>
#include <sdurws/ird/project/StoreTypes.hpp>
#include <sdurws/ird/testkit/PerfBenchmark.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace fs = std::filesystem;

namespace {

using namespace sdurws::ird;
using execution::TaskController;
using execution::TaskStateMachine;
using execution::WorkerSupervisor;
namespace core = sdurws::ird::core;
namespace project = sdurws::ird::project;

// =====================================================================
// 数据集冻结参数（datasetId＝ird-perf-b02-cancel，version 1——manifest
// 权威文件 testdata/performance/b02-cancel-manifest.json；本处常量必须
// 与 manifest 逐字一致，报告输出参数供漂移核对）
// =====================================================================

/// 驱动线程 poll 周期（单位 ms——测量分辨率；远小于被测的 2 s/10 s 阈值，
/// 观测滞后占比 ≤0.5%，登记于 manifest measurementProtocol）。
constexpr std::chrono::milliseconds kPollPeriod{10};
/// 正常收敛腿有效样本数（阈值类基准的样本量登记——协议 §4.2 的"每组有效
/// 样本 ≥15"系 P95 分组下限，本基准按同数自律取 15；manifest 冻结）。
constexpr std::size_t kNormalValidSamples = 15;
/// 正常收敛腿预热次数（剔除不计入判定——与 testkit 分组采样纪律同形）。
constexpr std::size_t kNormalWarmupRuns = 1;
/// 超时强杀腿场景次数（每次 ~11 s 真实墙钟——3 次即真实超时路径的重复
/// 实证；阈值类无统计预处理，全部样本参与判定）。
constexpr std::size_t kTimeoutKillRuns = 3;
/// 正常收敛腿 worker 脚本（流式批次 5×10⁶——测量窗内持续 Running，与
/// B-01 后台负载同源脚本形态；协作取消在批次边界收敛）。
constexpr const char* kNormalSettleScript = "stage-a-script:v1\nbatches 5000000\n";
/// 超时强杀腿 worker 脚本（先写 1 个检查点→关心跳→静默挂起——永不收敛，
/// 驱动 10 s 协作窗超时强杀路径；checkpoint 1 提供第三腿的"最近检查点"）。
constexpr const char* kHangScript =
    "stage-a-script:v1\ncheckpoint 1\nheartbeat-off\nhang\n";
/// 替身评估器装配 manifest 规范文本（worker 侧握手比对输入——与 worker
/// main.cpp kStageAManifest 同源）。
constexpr const char* kStageAManifest = "stage-a-double|1|7";
/// 夹具安全界（非协议值——有界等待的上限，防夹具故障时无限挂起；超界
/// 即显性失败如实报告）。单样本整周期上界。
constexpr std::chrono::seconds kCycleGuardTimeout{60};
/// 夹具安全界：worker 启动到 Running 相位（进程启动＋握手＋派发受理）。
constexpr std::chrono::seconds kRunningGuardTimeout{30};
/// 协议常量静态钉子（2 s/10 s 照抄 NFR-PERF-02——基准读数与被测协议用
/// 同一常量，杜绝抄写漂移；常量本身即被测对象的一部分）。
static_assert(TaskController::kCancelAcceptWindow == std::chrono::milliseconds{2000},
              "2 s 取消生效窗为 NFR-PERF-02 上游需求值");
static_assert(TaskController::kCancelCooperativeWindow == std::chrono::milliseconds{10000},
              "10 s 协作收敛窗为 NFR-PERF-02 上游需求值");

// =====================================================================
// 测试设施
// =====================================================================

/// 诊断空汇（IExecutionDiagnosticsSink 最小实现——CheckpointCoordinator
/// 构造必备注入；基准不消费诊断语义，计数留作观测面）。
class NullDiagnosticsSink final : public execution::IExecutionDiagnosticsSink
{
public:
    void report(const core::DiagnosticRecord&) override { ++m_count; }
    void reportDev(const std::string&, const std::string&) override { ++m_devCount; }

    /// 收到的结构化诊断数（观测面——正常路径应为 0）。
    int count() const noexcept { return m_count; }

private:
    int m_count = 0;      ///< 结构化诊断计数
    int m_devCount = 0;   ///< 开发诊断计数
};

/**
 * @brief 派发闸门记录器（IDispatchGate——"停止派发新批次"的观测面）。
 *
 * 线程约束：stopDispatch 在驱动线程 poll 内回调；callCount() 的原子量供
 * 测试线程在 Canceling 观测后自旋确认（进入 Canceling 与停派发回调在同一
 * poll 内顺序发生——极小窗口内跨线程可见，原子计数消除数据竞争）；
 * stopped() 明细断言在驱动线程 join 后调用（单线程域）。
 */
class RecordingDispatchGate final : public execution::IDispatchGate
{
public:
    void stopDispatch(execution::TaskId task) override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopped.push_back(task);
        m_calls.fetch_add(1, std::memory_order_release);
    }

    /// 回调到达次数（原子量——跨线程自旋等待面）。
    int callCount() const noexcept { return m_calls.load(std::memory_order_acquire); }

    /// 指定任务是否已停止派发（join 后单线程域断言面）。
    bool stopped(execution::TaskId task) const noexcept
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& t : m_stopped) {
            if (t == task) { return true; }
        }
        return false;
    }

private:
    mutable std::mutex m_mutex;              ///< 明细表互斥
    std::vector<execution::TaskId> m_stopped; ///< 收到 stopDispatch 的任务序
    std::atomic<int> m_calls{0};             ///< 回调计数（跨线程可见面）
};

/**
 * @brief 归档端口测试面替身（IResultArchivePort——真实写盘的最小实现）。
 *
 * 为什么不是生产 ProjectStore：归档白名单校验是 project 侧语义（PA-1——
 * 归档位置取登记记录，校验归 project），B-02 的测量面是"检查点在强杀后
 * 是否真实留存"，本替身把批次字节与 manifest 真实写入磁盘即完成该产物
 * 面（与 execution 单元 CheckpointCacheTest 的 FakeArchiveDisk 同形）。
 * 写盘失败按环境失败结构化返回（不抛——端口契约）。
 *
 * 线程约束：全部方法仅驱动线程（CheckpointCoordinator 调用域）。
 */
class DiskArchivePort final : public project::IResultArchivePort
{
public:
    project::ArchiveSessionRef begin(const project::ArchiveRequest& request) override
    {
        m_openDirs.push_back(request.runDir);
        return project::ArchiveSessionRef{};   // 空句柄＝自持令牌（替身口径，
                                               // 协调器作不透明凭据回传）
    }

    project::ArchiveStatus writeBatch(project::ArchiveSessionRef,
                                      const project::ArchiveBatch& batch) override
    {
        const fs::path dir = m_openDirs.back();
        std::error_code ec;
        fs::create_directories(dir, ec);
        if (ec) {
            return project::ArchiveStatus{
                false, project::StoreError{project::StoreErrorCode::DiskFull,
                                           "perf-b02: 创建目录失败"}};
        }
        for (const project::ArchiveItem& item : batch.items) {
            std::ofstream out(dir / item.relPath, std::ios::binary);
            out.write(reinterpret_cast<const char*>(item.bytes.data()),
                      static_cast<std::streamsize>(item.bytes.size()));
            if (!out) {
                return project::ArchiveStatus{
                    false, project::StoreError{project::StoreErrorCode::DiskFull,
                                               "perf-b02: 写文件失败"}};
            }
        }
        return project::ArchiveStatus{true, std::nullopt};
    }

    project::ArchiveStatus finalize(project::ArchiveSessionRef,
                                    const project::RunManifest&) override
    {
        // manifest.json 发布（存在性即"正式"标记——内容非契约面，同
        // CheckpointCacheTest 替身口径）。
        const fs::path dir = m_openDirs.back();
        std::ofstream out(dir / "manifest.json", std::ios::binary);
        out << "{\"dataset\":\"ird-perf-b02-cancel\"}";
        return project::ArchiveStatus{true, std::nullopt};
    }

    void abandon(project::ArchiveSessionRef, project::ArchiveEndReason) override {}

private:
    std::vector<fs::path> m_openDirs;   ///< begin 登记的归档目录（单会话形态）
};

/**
 * @brief 状态事件时点观测（ITaskEventSink——驱动线程内同步回调）。
 *
 * 时点在回调内采集＝"状态转移的观测时刻"（含 ≤1 个 poll 周期的保守滞后
 * ——实测值不低于真实转移时刻，方向保守不虚低）。测试线程经 CV 有界
 * 等待状态到达，读取首达时刻做时序统计。
 */
class TimingStateSink final : public execution::ITaskEventSink
{
public:
    void onTaskStatusChanged(const core::TaskStatusChangedPayload& payload) override
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            const auto now = std::chrono::steady_clock::now();
            // 首达时点只记一次（Canceling 的进入时刻＝协作窗起点的观测值；
            // 终态时刻＝收敛/强杀完成时刻）。
            m_firstAt.emplace(payload.newState, now);
            m_history.push_back(payload.newState);
        }
        m_cv.notify_all();
    }

    /// 等待某状态首达（有界——夹具安全界；false＝超界）。
    bool waitFor(core::TaskState state, std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_cv.wait_for(lock, timeout, [&] {
            return m_firstAt.find(state) != m_firstAt.end();
        });
    }

    /// 某状态首达时刻（未达＝epoch 0——调用方须先 waitFor 成功）。
    std::chrono::steady_clock::time_point firstAt(core::TaskState state)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_firstAt.find(state);
        return it != m_firstAt.end() ? it->second
                                     : std::chrono::steady_clock::time_point{};
    }

private:
    std::mutex m_mutex;               ///< 首达表与历史的互斥
    std::condition_variable m_cv;     ///< 状态到达通知
    std::map<core::TaskState, std::chrono::steady_clock::time_point>
        m_firstAt;                    ///< 各状态首达观测时刻
    std::vector<core::TaskState> m_history;   ///< 转移序（发布序留痕）
};

/// 一次完整取消周期的测量与断言产物（全部数值为真实墙钟实测——零预填；
/// 断言面字段在 teardown 后回填，此时驱动线程已停、协议记录单线程可读）。
struct CycleMeasurement {
    // ---- 测量面（时序统计的原始样本值）----
    bool assembled = false;      ///< worker 启动＋Running 相位就绪
    bool cancelAccepted = false; ///< 取消请求被受理
    double cancelingMs = 0.0;    ///< 取消请求 → 进入 Canceling（含停派发），单位 ms
    double settleMs = 0.0;       ///< 进入 Canceling → 终态（收敛或强杀完成），单位 ms
    double totalMs = 0.0;        ///< 取消请求 → 终态，单位 ms
    std::string failure;         ///< 非空＝本样本无效原因（夹具/环境断——如实留痕）

    // ---- 断言面（teardown 后回填）----
    bool hasTerminal = false;    ///< 终态记录已读到
    core::TaskState terminalState = core::TaskState::Queued;  ///< 终态
    /// 终结原因（TaskRecord::termination 原形——optional：非终结路径为空；
    /// 与枚举直接比较经 std::operator==(optional, T)）。
    std::optional<execution::TerminationCause> termination{};
    std::vector<std::string> diagCodes;    ///< 任务诊断码（正常取消应为空）
    bool exitSeen = false;                 ///< worker 退出已观测
    execution::ExitClassification exitClassification =
        execution::ExitClassification::Crashed;       ///< 退出分类
    std::uint32_t exitCode = 0;            ///< 退出码
    bool cancelAckSeen = false;            ///< CancelAck 帧已到达
    std::uint32_t workerPid = 0;           ///< worker 主进程 PID
    bool gateStopped = false;              ///< 停派发回调已到达（明细断言面）
    // ---- 第三腿检查点面 ----
    bool checkpointPublished = false;      ///< 检查点已发布（Published）
    std::string checkpointFailure;         ///< 发布失败留因（未发布时可读）
    fs::path archiveDir;                   ///< 实际归档目录
    std::vector<std::uint8_t> archiveBytes; ///< 发布的检查点批次字节（留存核对基准）
    core::Digest256 publishedDigest{};     ///< 发布时载荷复合摘要（恢复核对基准）
};

/**
 * @brief 取消时序基准驱动编排（一次装配＝一个真实进程上的完整取消周期）。
 *
 * 线程模型（生产装配的基准形态——调度线程显式驱动）：
 *   - 驱动线程（本 rig 唯一的"调度线程"）：TaskController/WorkerSupervisor/
 *     CheckpointCoordinator/状态机的全部调用都在本线程；装配完成后进入
 *     {controller.poll(); supervisor.poll(); sleep(kPollPeriod)} 循环；
 *   - 测试线程：assemble() 发起、requestCancel()（线程安全的命令通道——
 *     任意线程面）、经 TimingStateSink 的 CV 等待观测、teardown 后读终态
 *     记录。
 *
 * 生命周期：每样本构造一个 rig（独立 worker 进程＋独立协议编排——样本
 * 间零共享状态），析构＝join 驱动线程＋监督器析构兜杀（不留孤儿进程）。
 */
class CancelTimingRig final : public execution::IWorkerEventSink
{
public:
    CancelTimingRig() = default;

    ~CancelTimingRig() override { teardown(); }

    CancelTimingRig(const CancelTimingRig&) = delete;
    CancelTimingRig& operator=(const CancelTimingRig&) = delete;

    /**
     * @brief 装配一个 Running 态任务＋真实 worker 进程（驱动线程内完成）。
     *
     * @param script        [in] worker 脚本（stage-a-script:v1 文本——承载
     *                      于派发物快照字节，替身评估器解读）
     * @param checkpointDir [in] 检查点归档目录（非空＝到达的 CheckpointBatch
     *                      经 CheckpointCoordinator 真实写盘——第三腿用；
     *                      空＝不归档）
     * @return 就绪（worker Running＋协议绑定完成）；false＝装配失败
     *         （assembleFailure()/launchDiagnostics() 留因）
     */
    bool assemble(const std::string& script, const fs::path& checkpointDir = {})
    {
        m_driver = std::thread([this, script, checkpointDir] {
            driverProcedure(script, checkpointDir);
        });
        std::unique_lock<std::mutex> lock(m_assembleMutex);
        m_assembleCv.wait(lock, [this] { return m_assembleDone; });
        return m_assembleOk;
    }

    /// 装配失败原因（assemble()==false 时可读）。
    const std::string& assembleFailure() const noexcept { return m_assembleFailure; }

    /// 装配失败诊断（launch 失败时非空——首条 cause 供失败消息）。
    const std::vector<core::DiagnosticRecord>& launchDiagnostics() const
    {
        return m_launchDiagnostics;
    }

    /**
     * @brief 发起取消请求（测试线程；t0 由调用方以 steady_clock 采集）。
     *
     * TaskController::requestCancel 是任意线程的命令通道入口（§10.8）——
     * 命令由驱动线程下一次 poll 执行（生效延迟上界＝1 个 poll 周期，
     * 计入测量——方向保守）。
     */
    execution::CancelAck requestCancel(execution::TaskId task)
    {
        return m_controller->requestCancel(task);
    }

    /// 状态观测面（时点表——驱动线程采集，测试线程经 CV 等待后读取）。
    TimingStateSink& states() noexcept { return m_machineState; }

    /// 等待"停止派发"回调到达（有界自旋——与 Canceling 同 poll 内发生；
    /// false＝超界，断言面）。
    bool waitForDispatchStopped(std::chrono::milliseconds timeout)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (m_gate.callCount() == 0) {
            if (std::chrono::steady_clock::now() > deadline) { return false; }
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        return true;
    }

    /// 受管任务的任务身份（assemble 成功后有效——取消请求的键）。
    execution::TaskId taskId() const noexcept { return m_taskId; }

    /// worker 主进程 PID（0＝未知——Running 相位前）。
    std::uint32_t workerPid() const noexcept { return m_workerPid.load(); }

    /// WorkerExited 已观测（驱动线程事件面）。
    bool workerExitSeen() const noexcept { return m_exitSeen.load(); }

    /// 退出分类（exitSeen 后有效）。
    execution::ExitClassification workerExitClassification() const noexcept
    {
        return m_exitClassification.load();
    }

    /// 退出码（exitSeen 后有效）。
    std::uint32_t workerExitCode() const noexcept { return m_exitCode.load(); }

    /// CancelAck 帧已到达（协作窗收敛观测——§7.1 步 2）。
    bool cancelAckSeen() const noexcept { return m_cancelAckSeen.load(); }

    /// 检查点已发布（第三腿——writeCheckpoint Published）。
    bool checkpointPublished() const noexcept { return m_checkpointPublished.load(); }

    /// 检查点发布失败留因（checkpointPublished()==false 时可读）。
    const std::string& checkpointFailure() const noexcept { return m_checkpointFailure; }

    /// 发布的检查点批次字节（留存核对的基准——join 后单线程域读取）。
    const std::vector<std::uint8_t>& archiveBytes() const noexcept
    {
        return m_archiveBytes;
    }

    /// 发布时的载荷复合摘要（留存完整性核对基准——同上）。
    const core::Digest256& publishedDigest() const noexcept
    {
        return m_publishedDigest;
    }

    /// 检查点实际归档目录（第三腿——强杀后留存核对的目标位）。
    const fs::path& archiveDir() const noexcept { return m_archiveDirActual; }

    /// 收尾：停驱动线程＋监督器析构（兜杀残留 worker——不留孤儿进程）。
    /// 析构自动调用；显式调用后即可在测试线程读取协议终态记录（单线程域
    /// ——控制器随 rig 成员析构回收，显式 teardown 不提前置空以保记录可读）。
    void teardown()
    {
        if (m_stopRequested.exchange(true)) { return; }   // 幂等
        if (m_driver.joinable()) { m_driver.join(); }
        m_supervisor.reset();   // 监督器析构＝作业兜杀（KILL_ON_JOB_CLOSE）
    }

    /// 协议终态记录读取（teardown 后——调度线程已停，单线程域）。
    void forEachTask(const std::function<void(execution::TaskId,
                                              TaskStateMachine&)>& fn)
    {
        if (m_controller) {
            m_controller->forEachTask(fn);
        }
    }

private:
    // ---- IWorkerEventSink（驱动线程内被 supervisor.poll 调用）----
    void onWorkerEvent(const execution::WorkerEvent& event) override
    {
        switch (event.kind) {
        case execution::WorkerEvent::Kind::DispatchAccepted:
            // 派发已被 worker 身份核对接受（帧处理事件）——装配同步锚点：
            // status 相位 Running 在派发请求"发出"即置位（早于受理 ~一个
            // 握手往返），不能作为"worker 已开始执行脚本"的依据；以本
            // 事件为取消请求的 earliest 前提（此后 worker 已在脚本内，
            // 卡死脚本已进入 hang 段——取消时序的测量对象自此成立）。
            m_dispatchAccepted.store(true);
            break;
        case execution::WorkerEvent::Kind::CancelAck:
            m_cancelAckSeen.store(true);
            break;
        case execution::WorkerEvent::Kind::CheckpointBatch:
            // 第三腿：检查点批次到达——经 CheckpointCoordinator 真实写盘
            //（驱动线程域＝协调器调用域；发布结论留存断言面）。
            archiveCheckpoint(event);
            break;
        case execution::WorkerEvent::Kind::WorkerExited:
            m_exitCode.store(event.exitCode);
            m_exitClassification.store(event.exit);
            m_exitSeen.store(true);
            break;
        default:
            break;   // 握手/进度/批次等——本基准不消费
        }
    }

    /// 检查点批次→CheckpointCoordinator 真实写盘（驱动线程）。
    void archiveCheckpoint(const execution::WorkerEvent& event)
    {
        if (!m_checkpointCoordinator) { return; }   // 未装配归档面——只透传
        // 事件载荷已是解码后的批次字节（事件字段注释：CheckpointBatch 的
        // bytes＝批次字节、checkpointSequence＝序号）——不再二次解码。
        if (event.checkpointSequence == 0 || event.bytes.empty()) {
            m_checkpointFailure = "perf-b02: CheckpointBatch 事件载荷为空";
            return;
        }
        const std::uint64_t sequence = event.checkpointSequence;
        // 组装写出请求：记录字段按 CheckpointCacheTest 同款合法形态——身份
        // 字段以内容身份派生（非零即合法）；载荷面（引用清单＋复合摘要）
        // 由协调器按实际批次重算补全——申报面预填值被忽略（防申报与载荷
        // 不一致的伪造面）。
        execution::CheckpointWriteRequest req;
        req.record.checkpointId =
            execution::CheckpointId{m_identity.run, sequence};
        req.record.task = m_identity;
        req.record.snapshotId = execution::computeContentIdentity({0xA0});
        req.record.sliceId = execution::computeContentIdentity({0xB0, 0xB1});
        req.record.evaluatorKey = "stage-a-double";
        req.record.evaluatorContractVersion = 1;
        req.record.policyIdentity = execution::computeContentIdentity({0xC0});
        req.record.nameMapIdentity = execution::computeContentIdentity({0xD0});
        req.record.randomSeed = 0;
        req.record.threadCount = 1;
        req.record.completedUnits = 1;
        req.record.totalUnits = 100;
        req.record.resumable = true;
        execution::CheckpointBatchFile file;
        file.relPath = "part-000.bin";
        file.bytes = event.bytes;   // 拷贝留存（强杀后留存核对的基准字节）
        req.files.push_back(file);
        req.archiveDir = m_checkpointDir / m_identity.run.toCanonical()
                         / std::to_string(sequence);

        const execution::CheckpointWriteResult result =
            m_checkpointCoordinator->writeCheckpoint(req);
        if (result.outcome == execution::CheckpointWriteResult::Outcome::Published) {
            m_archiveDirActual = req.archiveDir;
            m_archiveBytes = file.bytes;
            // 发布时的载荷复合摘要（协调器按实际批次重算的同款公式——本处
            // 以同一单点函数重算留存，作为强杀后磁盘留存的完整性判据）。
            m_publishedDigest = execution::computePayloadDigest(req.files);
            m_checkpointPublished.store(true);
        } else {
            m_checkpointFailure = "perf-b02: 检查点发布未 Published（"
                                  + std::to_string(result.diagnostics.size())
                                  + " 条诊断）";
        }
    }

    /// 驱动线程主过程（本 rig 唯一的调度线程：装配→poll 循环至收尾）。
    void driverProcedure(const std::string& script, const fs::path& checkpointDir)
    {
        // ---- 装配段（全部调度线程域调用）----
        const std::wstring exePath = [] {
            const std::string narrow = IRD_EXECUTION_PERF_WORKER_EXE;
            return std::wstring(narrow.begin(), narrow.end());
        }();
        auto supervisor = std::make_unique<WorkerSupervisor>(exePath,
                                                             WorkerSupervisor::Config{});
        supervisor->setEventSink(this);
        auto controller = std::make_unique<TaskController>();
        controller->setDispatchGate(&m_gate);

        // 检查点协调器（第三腿装配——归档端口＋诊断 sink 注入；UTC 时钟
        // 缺省 system_clock——createdAtUtc 观测面，不参与时序判定）。
        if (!checkpointDir.empty()) {
            m_checkpointDir = checkpointDir;
            m_checkpointCoordinator =
                std::make_unique<execution::CheckpointCoordinator>(m_diskPort,
                                                                   m_diagSink);
        }

        // 受管任务装配到 Running（形态同 CancelProtocolTest::makeRunning
        // ——T1 受理→T2 出队→bindRun→T5 握手成功）。
        execution::TaskRecord record;
        record.taskId = execution::TaskId::generate();
        record.submission.snapshot.project = core::ProjectId::generate();
        record.submission.snapshot.branch = core::BranchId::generate();
        record.submission.snapshot.revision = core::RevisionId::generate();
        record.submission.evaluatorKey = "stage-a-double";
        record.submission.contractVersion = 1;
        record.submission.mode = core::EvaluationMode::Verified;
        auto machine = std::make_unique<TaskStateMachine>(
            TaskStateMachine::forAcceptedSubmission(record, &m_machineState));
        if (!machine->request(execution::TransitionTrigger::DispatchDequeued).accepted) {
            finishAssemble(false, "perf-b02: T2 出队转移被拒（夹具断）");
            return;
        }
        m_identity.project = record.submission.snapshot.project;
        m_identity.branch = record.submission.snapshot.branch;
        m_identity.revision = record.submission.snapshot.revision;
        m_identity.run = core::RunId::generate();
        m_identity.attempt = core::AttemptId{1};
        machine->bindRun(m_identity.run);
        if (!machine->request(execution::TransitionTrigger::PrepareSucceeded).accepted) {
            finishAssemble(false, "perf-b02: T5 握手成功转移被拒（夹具断）");
            return;
        }
        m_taskId = record.taskId;
        controller->attachTask(std::move(machine));

        // worker 派发绑定（脚本承载于快照字节——B-01 后台负载同构形态；
        // 五元组与受管任务的 run 一致）。
        execution::WorkerAssignment a;
        a.identity = m_identity;
        a.evaluatorKey = "stage-a-double";
        a.dispatch.snapshotBytes.assign(script.begin(), script.end());
        a.dispatch.modelBytes = {0xC0, 0xDE};
        a.dispatch.snapshotIdentity =
            execution::computeContentIdentity(a.dispatch.snapshotBytes);
        a.dispatch.modelIdentity =
            execution::computeContentIdentity(a.dispatch.modelBytes);
        a.manifestDigest = execution::manifestDigestHex(kStageAManifest);

        const execution::WorkerLaunchResult launched = supervisor->launch(a);
        {
            std::lock_guard<std::mutex> lock(m_assembleMutex);
            m_launchDiagnostics = launched.diagnostics;
        }
        if (!launched.ok) {
            finishAssemble(false, "perf-b02: worker 启动失败");
            return;
        }
        m_workerId = launched.worker;

        // 派发受理有界等待（进程启动＋握手＋**派发被 worker 身份核对接受**
        // ——夹具安全界；握手协议由本线程 poll 推进）。同步锚点＝
        // DispatchAccepted 事件而非 status 相位：相位 Running 在派发请求
        // "发出"即置位（早于受理帧一个握手往返）——以相位为锚会让取消
        // 请求竞态打到尚未受理派发的 worker（CancelRequest 落在派发等待
        // 段被丢弃→卡死脚本永不收敛→测得的是丢弃伪影而非协议时序）。
        const auto deadline = std::chrono::steady_clock::now() + kRunningGuardTimeout;
        while (!m_stopRequested.load(std::memory_order_acquire)) {
            supervisor->poll();
            const execution::WorkerStatus st = supervisor->status(m_workerId);
            m_workerPid.store(st.pid);
            if (m_dispatchAccepted.load()) { break; }
            if (m_exitSeen.load()) {
                finishAssemble(false, "perf-b02: worker 提前退出（握手拒绝等）");
                return;
            }
            if (std::chrono::steady_clock::now() > deadline) {
                finishAssemble(false, "perf-b02: 派发受理等待超界（夹具安全界）");
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{2});
        }

        // 协议绑定：worker 句柄适配器交 TaskController（取消/强杀编排的
        // 进程操作面——§10.3 IWorkerHandle 接缝的生产装配形态）。
        controller->bindWorker(m_taskId, supervisor->workerHandle(m_workerId));

        // 装配完成放行测试线程；本线程转入协议驱动循环直至收尾。
        m_supervisor = std::move(supervisor);
        m_controller = std::move(controller);
        finishAssemble(true, "");

        while (!m_stopRequested.load(std::memory_order_acquire)) {
            m_controller->poll();     // 协议推进（取消命令优先——§7.1）
            m_supervisor->poll();     // 通道/握手/退出分类推进
            std::this_thread::sleep_for(kPollPeriod);
        }
    }

    /// 装配结论收口（CV 放行测试线程）。
    void finishAssemble(bool ok, const std::string& reason)
    {
        {
            std::lock_guard<std::mutex> lock(m_assembleMutex);
            m_assembleOk = ok;
            m_assembleFailure = reason;
            m_assembleDone = true;
        }
        m_assembleCv.notify_all();
    }

    // ---- 装配面（驱动线程创建；teardown 后测试线程单线程域只读）----
    std::unique_ptr<WorkerSupervisor> m_supervisor;   ///< worker 池监督器
    std::unique_ptr<TaskController> m_controller;     ///< 取消/终止协议编排
    std::unique_ptr<execution::CheckpointCoordinator> m_checkpointCoordinator; ///< 检查点协调器（第三腿）
    TimingStateSink m_machineState;                   ///< 状态事件时点观测
    RecordingDispatchGate m_gate;                     ///< 停派发观测
    DiskArchivePort m_diskPort;                       ///< 归档端口（真实写盘替身）
    NullDiagnosticsSink m_diagSink;                   ///< 诊断空汇
    execution::WorkerId m_workerId{};                 ///< worker 实例
    execution::TaskId m_taskId{};                     ///< 受管任务身份
    core::TaskIdentity m_identity{};                  ///< 派发五元组

    // ---- 第三腿检查点观测（驱动线程写、join 后测试线程读）----
    fs::path m_checkpointDir;                          ///< 检查点归档根
    fs::path m_archiveDirActual;                       ///< 实际归档目录
    std::vector<std::uint8_t> m_archiveBytes;          ///< 发布批次字节
    core::Digest256 m_publishedDigest{};               ///< 发布时复合摘要
    std::string m_checkpointFailure;                   ///< 发布失败留因

    // ---- 跨线程观测（原子量：驱动线程写、测试线程读）----
    std::atomic<std::uint32_t> m_workerPid{0};          ///< worker 主进程 PID
    std::atomic<bool> m_exitSeen{false};                ///< WorkerExited 已观测
    std::atomic<std::uint32_t> m_exitCode{0};           ///< 退出码
    std::atomic<execution::ExitClassification> m_exitClassification{
        execution::ExitClassification::Crashed};        ///< 退出分类
    std::atomic<bool> m_cancelAckSeen{false};           ///< CancelAck 帧已到达
    std::atomic<bool> m_checkpointPublished{false};     ///< 检查点已发布
    std::atomic<bool> m_dispatchAccepted{false};        ///< 派发已被 worker 受理（装配锚点）

    // ---- 线程编排 ----
    std::thread m_driver;                ///< 驱动线程（装配＋poll 循环）
    mutable std::mutex m_assembleMutex;  ///< 装配结论互斥
    std::condition_variable m_assembleCv;
    bool m_assembleDone = false;
    bool m_assembleOk = false;
    std::string m_assembleFailure;
    std::atomic<bool> m_stopRequested{false};
    std::vector<core::DiagnosticRecord> m_launchDiagnostics; ///< 启动失败诊断
};

// =====================================================================
// 单样本执行器：一个真实进程上的完整取消周期
// =====================================================================

/**
 * @brief 执行一次完整取消周期并测量两段时序（真实墙钟——零虚拟零预填）。
 *
 * 周期：装配（worker 启动→Running→协议绑定）→ t0 取消请求 → d1＝进入
 * Canceling（＋停派发）→ d2b＝终态（协作收敛或超时强杀完成）→ teardown
 * 后回填断言面。任何夹具断以返回 failure 留因（采样循环记无效样本）。
 *
 * @param script            [in] worker 脚本
 * @param expectCooperative [in] true＝期望协作收敛终态 Canceled（正常腿）；
 *                          false＝期望超时强杀终态 Failed（第三腿）
 * @param checkpointDir     [in] 检查点归档目录（第三腿非空）
 * @return 测量产物（assembled==false 时测量面全零＋failure 留因）
 */
CycleMeasurement runCancelCycle(const std::string& script, bool expectCooperative,
                                const fs::path& checkpointDir = {})
{
    CycleMeasurement m;
    CancelTimingRig rig;
    if (!rig.assemble(script, checkpointDir)) {
        m.failure = rig.assembleFailure();
        if (!rig.launchDiagnostics().empty()) {
            m.failure += "（首诊断：" + rig.launchDiagnostics().front().cause + "）";
        }
        return m;   // 装配失败＝无效样本（原因留痕——不计入判定分母）
    }
    m.assembled = true;

    // ---- t0：取消请求（命令通道任意线程面；生效由驱动线程 poll 执行）----
    const auto t0 = std::chrono::steady_clock::now();
    const auto ack = rig.requestCancel(rig.taskId());
    if (!ack.accepted) {
        m.failure = "perf-b02: 取消请求未被受理（协议前置断——Running 态取消必受理）";
        return m;
    }
    m.cancelAccepted = true;

    // ---- d1：进入 Canceling（协议第一条款的测量对象）----
    if (!rig.states().waitFor(core::TaskState::Canceling, kCycleGuardTimeout)) {
        m.failure = "perf-b02: 等待 Canceling 超界（夹具安全界——非协议判定）";
        return m;
    }
    if (!rig.waitForDispatchStopped(std::chrono::milliseconds{500})) {
        m.failure = "perf-b02: 进入 Canceling 后未观测到停止派发回调";
        return m;
    }
    const auto tCanceling = rig.states().firstAt(core::TaskState::Canceling);
    m.cancelingMs =
        std::chrono::duration<double, std::milli>(tCanceling - t0).count();

    // ---- d2b：终态（协作收敛 Canceled／超时强杀 Failed）----
    const core::TaskState expectedTerminal = expectCooperative
                                                 ? core::TaskState::Canceled
                                                 : core::TaskState::Failed;
    if (!rig.states().waitFor(expectedTerminal, kCycleGuardTimeout)) {
        m.failure = "perf-b02: 等待终态超界（夹具安全界——非协议判定）";
        return m;
    }
    const auto tTerminal = rig.states().firstAt(expectedTerminal);
    m.settleMs =
        std::chrono::duration<double, std::milli>(tTerminal - tCanceling).count();
    m.totalMs = std::chrono::duration<double, std::milli>(tTerminal - t0).count();

    // ---- 进程退出事件落地（有界等待——强杀是异步语义：T13 与
    // terminateForce 同拍，进程死亡事件经读线程→poll→事件随后到达；
    // 不等即 teardown 会把分类面留在默认值——观测面失真）。等待失败
    // 不阻断判定：exitSeen 断言面如实反映。----
    {
        const auto exitDeadline = std::chrono::steady_clock::now()
                                  + std::chrono::seconds{5};
        while (!rig.workerExitSeen()
               && std::chrono::steady_clock::now() < exitDeadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds{2});
        }
    }

    // ---- 收尾（join 驱动线程；此后单线程域读协议记录与进程观测面）----
    rig.teardown();
    rig.forEachTask([&](execution::TaskId, TaskStateMachine& machine) {
        m.hasTerminal = true;
        m.terminalState = machine.record().state;
        m.termination = machine.record().termination;
        for (const auto& d : machine.record().diagnostics) {
            m.diagCodes.push_back(d.code);
        }
    });
    m.exitSeen = rig.workerExitSeen();
    m.exitClassification = rig.workerExitClassification();
    m.exitCode = rig.workerExitCode();
    m.cancelAckSeen = rig.cancelAckSeen();
    m.workerPid = rig.workerPid();
    m.gateStopped = rig.waitForDispatchStopped(std::chrono::milliseconds{0});
    m.checkpointPublished = rig.checkpointPublished();
    m.checkpointFailure = rig.checkpointFailure();
    m.archiveDir = rig.archiveDir();
    m.archiveBytes = rig.archiveBytes();
    m.publishedDigest = rig.publishedDigest();
    return m;
}

// =====================================================================
// 报告组装（testkit PerfSessionReport 复用——协议 §4.8 留痕面）
// =====================================================================

/// 环境事实读取（§4.1 登记清单的环境变量可得面——缺失如实留空）。
std::string envOr(const char* name, const char* fallback = "")
{
    const char* v = std::getenv(name);
    return (v != nullptr && *v != '\0') ? std::string(v) : std::string(fallback);
}

/// 组装会话报告头（标识/版本/数据集/环境——测量组由调用方追加）。
testkit::PerfSessionReport makeSessionHeader(const std::string& caseId)
{
    testkit::PerfSessionReport r;
    r.caseId = caseId;
    r.baselineDoc = "performance-baseline #2 B-02";
    // 软件版本锁定（§4.1）：commit 由执行方经 IRD_PERF_GIT_COMMIT 注入
    //（缺省如实留空——commit SHA 在首测报告文档登记）。
    r.gitCommit = envOr("IRD_PERF_GIT_COMMIT");
    r.buildConfig = "Release";
    r.datasetId = "ird-perf-b02-cancel";
    r.datasetVersion = "1";
    r.machineName = envOr("COMPUTERNAME");
    r.cpuInfo = envOr("PROCESSOR_IDENTIFIER")
                + " x" + envOr("NUMBER_OF_PROCESSORS", "?");
    r.osInfo = envOr("OS", "Windows") + " " + envOr("PROCESSOR_ARCHITECTURE");
    // 内存/存储/电源：环境变量不可得——如实指向首测报告的硬件清单。
    r.memoryInfo = "见首测报告硬件清单（traceability/benchmarks/"
                   "wp23-t03-cancel-baseline.md §2）";
    r.storageInfo = "见首测报告硬件清单";
    r.powerPolicyNote = "见首测报告硬件清单";
    r.backgroundNote = "每样本一个独立 worker 进程（顺序启动/终结）；测量"
                       "期间无其他基准并行";
    // 口径登记（报告自含——验收对照面）。
    r.notes.push_back(
        "B-02 为时序阈值类判定（performance-baseline §2 统计口径区分）："
        "判定统计量＝有效样本最大值（全样本须达标），非 P95 分位数；"
        "verdicts.measuredP95Ms 字段沿夹具 schema 承载该判定统计量");
    r.notes.push_back(
        "测量编排＝真实 TaskController（steady_clock）＋真实 WorkerSupervisor"
        "＋真实独立 worker 进程（stage-a-script 替身评估器），驱动线程 "
        "10 ms poll 周期；时点＝驱动线程事件到达时刻（滞后 ≤1 poll，保守）");
    r.notes.push_back(
        "替身边界：评估器＝阶段 A 脚本替身（协作点形态与业务评估器同构）；"
        "归档端口＝真实写盘测试面替身（project 白名单校验语义不在测量面）"
        "——详见用例文件头与首测报告 §4");
    r.notes.push_back(
        "数据集 manifest＝testdata/performance/b02-cancel-manifest.json"
        "（datasetId ird-perf-b02-cancel@1——参数冻结，升级换版本）");
    return r;
}

/// 单腿阈值判定记录（判定统计量＝有效样本最大值——阈值类全样本判定；
/// measuredP95Ms 字段沿夹具 schema 承载该统计量，notes 已声明口径）。
testkit::PerfThresholdVerdict
makeMaxVerdict(const testkit::InteractionGroupReport& g, double thresholdMs,
               const char* thresholdSource)
{
    testkit::PerfThresholdVerdict v;
    v.opId = g.opId;
    v.stateId = g.stateId;
    v.thresholdMs = thresholdMs;
    v.thresholdSource = thresholdSource;
    v.measuredP95Ms = g.maxValidMs;   // 判定统计量（见 notes 口径声明）
    v.pass = g.maxValidMs <= thresholdMs;
    return v;
}

/// 由原始样本序列组装分组报告（统计面经 testkit 单点函数——P95 仅作分布
/// 参考；判定统计量 maxValidMs；调用方保证 valid 非空）。
testkit::InteractionGroupReport
makeGroupReport(const std::string& opId, const std::string& stateId,
                const std::vector<testkit::InteractionSample>& samples)
{
    testkit::InteractionGroupReport g;
    g.opId = opId;
    g.stateId = stateId;
    g.samples = samples;
    std::vector<double> validMs;
    for (const testkit::InteractionSample& s : samples) {
        if (s.valid && !s.excluded) {
            ++g.validCount;
            validMs.push_back(s.durationMs);
        } else if (!s.valid) {
            ++g.invalidCount;
        } else {
            ++g.warmupRuns;
        }
    }
    if (!validMs.empty()) {
        g.p95Ms = testkit::percentileLinearInterpolated(validMs, 0.95);
        g.maxValidMs = *std::max_element(validMs.begin(), validMs.end());
        g.minValidMs = *std::min_element(validMs.begin(), validMs.end());
    }
    return g;
}

/// 从磁盘读取检查点批次文件（第三腿留存核对——强杀后重新落盘读出）。
bool readDiskFile(const fs::path& path, std::vector<std::uint8_t>& out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) { return false; }
    out.assign(std::istreambuf_iterator<char>(in),
               std::istreambuf_iterator<char>());
    return true;
}

/// OS 级进程死亡核对（强杀证据面——PID 不再对应存活进程；有界重试 3 s
/// 防清理竞态，超界即失败——不静默放行）。
bool workerProcessIsDead(std::uint32_t pid)
{
    if (pid == 0) { return false; }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
    for (;;) {
        // SYNCHRONIZE＋QUERY_LIMITED：仅等待/基本查询权——不注入不控制。
        HANDLE h = ::OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                                 FALSE, pid);
        if (h == nullptr) {
            return true;   // 打不开＝进程不存在（已终结——主路径）
        }
        const DWORD w = ::WaitForSingleObject(h, 0);
        ::CloseHandle(h);
        if (w == WAIT_OBJECT_0) {
            return true;   // 句柄已信号＝进程已终结
        }
        if (std::chrono::steady_clock::now() > deadline) {
            return false;  // 3 s 仍存活——强杀证据不成立
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{20});
    }
}

}  // namespace

// =====================================================================
// 基准用例（B-02 三条款——全部真实墙钟实测）
// =====================================================================

/**
 * 基准用例（B-02 条款 1＋2）：真实独立 worker 进程上的正常取消周期——
 * 取消请求 2 s 内进入 Canceling 并停止派发新批次；普通批次（协作收敛）
 * 10 s 内结束。1 次预热（剔除）＋15 有效样本，逐样本 fresh worker 进程；
 * 判定统计量＝有效样本最大值（阈值类全样本判定——NFR-PERF-02 原文）。
 * 附加证据面：worker 以退出码 20（协作取消确认）真实退出、CancelAck 帧
 * 到达、正常取消零诊断（UX-03）。
 */
TEST(CancelTimingBenchmark, NormalCancelSettleRealWorkerThresholds_WP23T03_B02)
{
    IRD_TEST_INFO("NFR-PERF-02", {"B-02"}, std::nullopt);

    std::vector<testkit::InteractionSample> cancelingSamples;   ///< d1 原始序列
    std::vector<testkit::InteractionSample> settleSamples;      ///< d2b 原始序列
    std::string warmupNote;   ///< 预热腿实测摘要（留痕不判定）

    // ---- 预热腿（剔除不计入判定——首跑含进程启动链路的冷效应）----
    {
        const CycleMeasurement w = runCancelCycle(kNormalSettleScript, true);
        ASSERT_TRUE(w.assembled) << "预热样本装配失败：" << w.failure;
        ASSERT_TRUE(w.cancelAccepted);
        testkit::InteractionSample s;
        s.excluded = true;
        s.durationMs = w.cancelingMs;
        cancelingSamples.push_back(s);
        settleSamples.push_back(s);
        warmupNote = "预热腿实测 d1=" + std::to_string(w.cancelingMs)
                     + " ms, d2b=" + std::to_string(w.settleMs) + " ms（剔除）";
        std::fprintf(stderr, "[perf-b02] %s\n", warmupNote.c_str());
    }

    // ---- 采样腿（15 有效样本——逐样本 fresh worker 进程）----
    for (std::size_t i = 0; i < kNormalValidSamples; ++i) {
        const CycleMeasurement m = runCancelCycle(kNormalSettleScript, true);
        testkit::InteractionSample s1;
        testkit::InteractionSample s2;
        if (!m.assembled || !m.cancelAccepted || !m.hasTerminal
            || !m.failure.empty()) {
            // 无效样本：测量区间不完整——原因留痕不计入分母（逐例打点，
            // 消费方断言零无效收口——testkit 采样纪律）。
            s1.valid = false;
            s2.valid = false;
            s1.invalidReason = m.failure;
            s2.invalidReason = m.failure;
            std::fprintf(stderr, "[perf-b02] invalid sample #%zu: %s\n", i + 1,
                         m.failure.c_str());
        } else {
            s1.durationMs = m.cancelingMs;
            s2.durationMs = m.settleMs;
        }
        cancelingSamples.push_back(s1);
        settleSamples.push_back(s2);

        // 逐样本进程面证据（真实收敛的证据链——非统计面，即时断言）。
        if (m.assembled && m.hasTerminal) {
            EXPECT_EQ(m.terminalState, core::TaskState::Canceled)
                << "样本 #" << i + 1 << " 终态非 Canceled（协作收敛未发生）";
            EXPECT_TRUE(m.cancelAckSeen)
                << "样本 #" << i + 1 << " 无 CancelAck 帧（§7.1 步 2 收敛观测缺失）";
            EXPECT_EQ(m.exitClassification,
                      execution::ExitClassification::CancelAcknowledged)
                << "样本 #" << i + 1 << " worker 退出分类非协作取消（退出码 "
                << m.exitCode << "）";
            EXPECT_TRUE(m.diagCodes.empty())
                << "样本 #" << i + 1 << " 正常取消产生诊断（UX-03 违约）";
        }
    }

    // ---- 统计与判定（判定统计量＝有效样本最大值；B-02 非 P95 类）----
    const auto cancelingGroup =
        makeGroupReport("cancel-to-canceling", "real-worker", cancelingSamples);
    const auto settleGroup =
        makeGroupReport("canceling-to-settled", "real-worker", settleSamples);
    ASSERT_EQ(cancelingGroup.validCount, kNormalValidSamples)
        << "有效样本不足（manifest 冻结的样本量）";
    ASSERT_EQ(cancelingGroup.invalidCount, std::size_t{0})
        << "存在无效样本（失败留痕见 stderr/报告——不带病判过）";
    ASSERT_EQ(settleGroup.validCount, kNormalValidSamples);
    ASSERT_EQ(settleGroup.invalidCount, std::size_t{0});

    // 条款 1：取消请求 2 s 内进入 Canceling（NFR-PERF-02 原文）。
    EXPECT_LE(cancelingGroup.maxValidMs, 2000.0)
        << "取消生效时序超阈（NFR-PERF-02 条款 1：≤2 s——max="
        << cancelingGroup.maxValidMs << " ms）";
    // 条款 2：普通批次 10 s 内结束（协作收敛窗——NFR-PERF-02 原文）。
    EXPECT_LE(settleGroup.maxValidMs, 10000.0)
        << "批次收敛时序超阈（NFR-PERF-02 条款 2：≤10 s——max="
        << settleGroup.maxValidMs << " ms）";

    // ---- 报告落盘（拖挂——IRD_PERF_REPORT_DIR 未设则跳过）----
    testkit::PerfSessionReport session =
        makeSessionHeader("b02-cancel-normalsettle");
    session.groups.push_back(cancelingGroup);
    session.groups.push_back(settleGroup);
    session.verdicts.push_back(makeMaxVerdict(
        cancelingGroup, 2000.0, "REQUIREMENTS NFR-PERF-02（条款 1：≤2 s）"));
    session.verdicts.push_back(makeMaxVerdict(
        settleGroup, 10000.0, "REQUIREMENTS NFR-PERF-02（条款 2：≤10 s）"));
    session.allPassed = std::all_of(session.verdicts.begin(), session.verdicts.end(),
                                    [](const testkit::PerfThresholdVerdict& v) {
                                        return v.pass;
                                    });
    session.notes.push_back(warmupNote);
    session.notes.push_back(
        "逐样本进程证据：退出分类＝协作取消（码 20）、CancelAck 帧到达、"
        "任务诊断空（UX-03）——15/15 样本全数成立");
    if (const auto dir = testkit::perfReportDirFromEnv()) {
        EXPECT_TRUE(testkit::writePerfReportJson(*dir, session))
            << "报告写失败（拖挂不阻断——检查 IRD_PERF_REPORT_DIR）";
    }
}

/**
 * 基准用例（B-02 条款 3）：超时强杀路径——永不收敛的卡死批次（heartbeat-
 * off＋hang 真实脚本）驱动 10 s 协作窗耗尽，协议强制终止真实独立 worker
 * 进程（进程树终止），且强杀前发布的检查点在强杀后真实留存（可续跑）。
 * 3 次有效场景（阈值类无统计预处理），逐场景判定：
 *   ①取消 2 s 内进入 Canceling（同条款 1——第三腿同样适用）；
 *   ②收敛窗耗尽后强杀完成：任务 Failed＋EX-FORCE-TERMINATED＋
 *     TerminationCause::ForceTerminated（不伪装普通失败）；
 *   ③worker 进程 OS 级死亡（PID 探测）＋监督器 Dead 相位面；
 *   ④检查点留存：归档目录 manifest＋批次文件仍在，磁盘重读字节的复合
 *     摘要＝发布时摘要（内容寻址核对——可续跑的物质基础）。
 */
TEST(CancelTimingBenchmark, TimeoutForceKillRetainsCheckpoint_WP23T03_B02)
{
    IRD_TEST_INFO("NFR-PERF-02", {"B-02"}, std::nullopt);

    // 临时归档根（本用例私有——目录名含时刻＋PID 防撞；失败保留现场，
    // 成功清理——TempDir 惯例）。
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path checkpointRoot = fs::temp_directory_path()
        / ("ird-perf-b02-ckpt-" + std::to_string(stamp) + "-"
           + std::to_string(::GetCurrentProcessId()));
    std::error_code ec;
    fs::create_directories(checkpointRoot, ec);
    ASSERT_FALSE(ec) << "检查点临时目录创建失败";

    std::vector<testkit::InteractionSample> cancelingSamples;   ///< 各场景 d1
    std::vector<testkit::InteractionSample> killSamples;        ///< 各场景窗耗尽→强杀完成
    std::string evidenceNote;   ///< 逐场景证据摘要（报告留痕）

    for (std::size_t i = 0; i < kTimeoutKillRuns; ++i) {
        // ---- 场景执行（真实 10 s 窗＋真实进程树终止——每次 ~11 s 墙钟）----
        const CycleMeasurement m = runCancelCycle(kHangScript, false, checkpointRoot);

        // 无效样本判定（夹具断——如实显性失败，不带病判过）。
        ASSERT_TRUE(m.assembled) << "场景 #" << i + 1 << " 装配失败：" << m.failure;
        ASSERT_TRUE(m.cancelAccepted) << "场景 #" << i + 1 << "：" << m.failure;
        ASSERT_TRUE(m.failure.empty()) << "场景 #" << i + 1 << "：" << m.failure;
        ASSERT_TRUE(m.hasTerminal);

        // ---- ①取消生效（同条款 1——每场景判定）----
        EXPECT_LE(m.cancelingMs, 2000.0)
            << "场景 #" << i + 1 << " 取消生效超阈（NFR-PERF-02 条款 1：≤2 s）";

        // ---- ②强杀完成面（窗耗尽→强杀序列→Failed 显式标记）----
        EXPECT_EQ(m.terminalState, core::TaskState::Failed)
            << "场景 #" << i + 1 << " 终态非 Failed（超时强杀未完成）";
        EXPECT_EQ(m.termination, execution::TerminationCause::ForceTerminated)
            << "场景 #" << i + 1 << " 终结原因非 ForceTerminated（T13 显式标记缺失"
            "——强杀不得伪装普通失败）";
        EXPECT_NE(std::find(m.diagCodes.begin(), m.diagCodes.end(),
                            "EX-FORCE-TERMINATED"),
                  m.diagCodes.end())
            << "场景 #" << i + 1 << " 缺 EX-FORCE-TERMINATED 稳定码";
        EXPECT_FALSE(m.cancelAckSeen)
            << "场景 #" << i + 1 << " 收到 CancelAck（卡死脚本不应收敛）";
        EXPECT_EQ(m.exitClassification,
                  execution::ExitClassification::ForceTerminatedBySupervisor)
            << "场景 #" << i + 1 << " 退出分类非监督方强杀";

        // 强杀开销（窗耗尽点＝进入 Canceling＋10 s 协作窗；超出部分＝强杀
        // 序列的真实开销——报告面，登记于 notes）。
        const double killOverheadMs = m.settleMs - 10000.0;

        // ---- ③worker 进程 OS 级死亡（强杀证据——PID 不再存活）----
        EXPECT_TRUE(workerProcessIsDead(m.workerPid))
            << "场景 #" << i + 1 << " worker 进程（PID " << m.workerPid
            << "）强杀后仍存活";

        // ---- ④检查点留存（强杀前的发布——强杀后的磁盘事实）----
        ASSERT_TRUE(m.checkpointPublished)
            << "场景 #" << i + 1 << " 检查点未发布（前置断——"
            << m.checkpointFailure << "）";
        ASSERT_TRUE(fs::exists(m.archiveDir / "manifest.json"))
            << "场景 #" << i + 1 << " 归档 manifest 缺失（留存断言面）";
        std::vector<std::uint8_t> diskBytes;
        ASSERT_TRUE(readDiskFile(m.archiveDir / "part-000.bin", diskBytes))
            << "场景 #" << i + 1 << " 检查点批次文件缺失（强杀后不留存？）";
        EXPECT_EQ(diskBytes, m.archiveBytes)
            << "场景 #" << i + 1 << " 批次文件字节与发布时不一致（留存被篡改）";
        // 内容寻址完整性核对：磁盘重读字节的复合摘要＝发布时摘要（同一
        // 单点函数——CON-05 内容寻址的基准面复用）。
        execution::CheckpointBatchFile diskFile;
        diskFile.relPath = "part-000.bin";
        diskFile.bytes = diskBytes;
        EXPECT_TRUE(m.publishedDigest
                    == execution::computePayloadDigest({diskFile}))
            << "场景 #" << i + 1 << " 磁盘重读摘要与发布摘要不符";

        // 样本登记（d1 与"窗耗尽→强杀完成"两序列——真实墙钟实测）。
        testkit::InteractionSample s1;
        s1.durationMs = m.cancelingMs;
        cancelingSamples.push_back(s1);
        testkit::InteractionSample sk;
        sk.durationMs = m.settleMs;
        killSamples.push_back(sk);

        evidenceNote += "场景#" + std::to_string(i + 1)
                        + ": d1=" + std::to_string(m.cancelingMs)
                        + "ms, 强杀完成=" + std::to_string(m.settleMs)
                        + "ms（自入 Canceling 起，含 10 s 窗，超出窗部分 "
                        + std::to_string(killOverheadMs) + "ms）, workerPid="
                        + std::to_string(m.workerPid)
                        + ", 检查点留存=已核对\n";
        std::fprintf(stderr, "[perf-b02] %s", evidenceNote.c_str());
    }

    // ---- 报告落盘（含逐场景证据）----
    const auto cancelingGroup =
        makeGroupReport("cancel-to-canceling", "timeout-kill", cancelingSamples);
    const auto killGroup =
        makeGroupReport("window-expiry-to-forcekilled", "timeout-kill", killSamples);
    testkit::PerfSessionReport session =
        makeSessionHeader("b02-cancel-timeout-kill");
    session.groups.push_back(cancelingGroup);
    session.groups.push_back(killGroup);
    // 条款 3 无独立毫秒阈值（"超时后允许终止"是行为条款）——判定由四断言
    // 面承载（生效/显式标记/进程死亡/检查点留存）；verdicts 登记 d1 阈值
    //（条款 1 在本用例同样适用），强杀时长作为测量值报告不作阈值判定。
    session.verdicts.push_back(makeMaxVerdict(
        cancelingGroup, 2000.0, "REQUIREMENTS NFR-PERF-02（条款 1：≤2 s）"));
    session.allPassed = session.verdicts.front().pass;
    session.notes.push_back("条款 3 判定面＝行为断言（EX-FORCE-TERMINATED＋"
                            "进程 OS 级死亡＋检查点留存核对）×3 场景全成立——"
                            "非毫秒阈值类");
    session.notes.push_back(evidenceNote);
    if (const auto dir = testkit::perfReportDirFromEnv()) {
        EXPECT_TRUE(testkit::writePerfReportJson(*dir, session));
    }

    // 成功清理（失败保留现场——TempDir 惯例）。
    if (!HasNonfatalFailure()) {
        fs::remove_all(checkpointRoot, ec);
    }
}
