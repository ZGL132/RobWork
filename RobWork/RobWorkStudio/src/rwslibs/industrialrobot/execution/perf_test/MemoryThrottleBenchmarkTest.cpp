/**
 * @file   MemoryThrottleBenchmarkTest.cpp
 * @brief  内存与节流规模化基准（performance-baseline §2 B-05／需求
 *         NFR-PERF-04）——真实独立 worker 进程的大任务管线（分批／流式／
 *         检查点）上测量"主进程＋全部工作进程"合计峰值内存占物理内存比例
 *         （≤70%，NFR-PERF-04 上游值），并以决策迹判定"接近上限先节流
 *         （65%，D-05 实现参数）再给资源不足诊断"的时序语义（WP-23-T06；
 *         performance-baseline §4.7 明文"时序语义由本卡用例承载"）。
 *
 * 设计依据：
 *   - doc/industrial-robot-design/performance-baseline.md §2 B-05 行（达标
 *     口径照抄需求原文——70% 零私改）、§4.1（硬件/软件版本登记）、§4.7
 *     （内存计量口径：主进程＋全部工作进程合计峰值，各进程独立计量后汇总，
 *     采样须覆盖工作进程峰值并存时刻）、§4.8（报告留痕——原始样本序列
 *     全量）、§5.5（本卡挂点）；
 *   - 需求 NFR-PERF-04（REQUIREMENTS §19.3 原文——"大任务使用分批、流式
 *     结果、缓存和检查点；主进程与全部工作进程合计峰值内存默认不超过物理
 *     内存 70%，接近上限先节流再给资源不足诊断"；B-05 为内存阈值类判定
 *     〔performance-baseline §2 统计口径区分行〕，判定统计量＝有效样本
 *     最大值：全样本须达标，非 P95）；
 *   - 任务契约 tasks/foundation/WP-23-T06.json（≙DTB §2.24 行——EX-T07
 *     越界声明的规模化验收承接位）；
 *   - units/execution.md §6.1 内存预算行／§6.6 内存采样行／§15.1 D-05
 *     （65% 节流阈值＝实现参数非需求阈值）、§15.4 v0.9（EX-T07 落位登记
 *     ——ResourceController/IMemorySampler/SupervisorMemorySampler/
 *     MemProbe 三探针即本基准的被测生产采样链）。
 *
 * 与功能用例（ResourceGovernanceTest）的分工——为什么两腿形态不同：
 *   EX-RES-2 功能用例以脚本采样 fake＋ManualClock 虚拟推进断言三段治理的
 *   "正确性"（testkit §6.5 纪律——正确性判据禁止 sleep 后断言）；本基准
 *   两腿补的是"规模化达标"证据：
 *   - 腿一（内存峰值基准）：真实 worker 进程真实内存占用，生产采样链
 *     （SupervisorMemorySampler＝MemProbe 三探针＋监督器作业内存合计）
 *     在运行全程高频采样——"协议对"之上的"峰值实测"；
 *   - 腿二（先节流后诊断时序语义）：判定统计量＝决策序列（节流层迁移
 *     必须先于诊断出具——§6.1"仍超限→诊断"的「仍」字语义），非时延
 *     测量——以真实 steady_clock 决策对象产出序列迹，配置
 *     sampleInterval=0 逐拍采样（Config 文档明载的测试对照面），
 *     调度器集成面（真实出队停拍）由 EX-RES-2 承载，两 face 互补不互替。
 *
 * 测量口径（datasetId ird-perf-b05-memory@1——manifest 冻结于
 * testdata/performance/b05-memory-manifest.json，本处常量与 manifest 逐字
 * 一致，报告输出参数供漂移核对）：
 *   - 驱动编排＝本 TU 的 MemoryThrottleRig：真实 WorkerSupervisor＋真实
 *     worker 进程（sdurws_ird_execution_worker 阶段 A 脚本替身评估器）＋
 *     真实 ResourceController/SupervisorMemorySampler（生产采样链——
 *     EX-T07 落位件），驱动线程 10 ms poll＋10 ms 采样间隔（Resource
 *     Controller Config——实现参数，manifest 冻结）；大任务脚本＝检查点
 *     ×2＋批次流 100,000＋FinalOutput（分批/流式/检查点三机制行为观测面）；
 *   - 合计峰值口径（§4.7）：主进程半区＝GetProcessMemoryInfo 工作集
 *     （MemProbe queryCurrentProcessWorkingSetBytes——当前值，峰值取采样
 *     序列最大）；worker 半区＝QueryInformationJobObject 的
 *     PeakJobMemoryUsed（作业创建以来提交内存峰值——**单调不回落**，
 *     天然覆盖"工作进程峰值与主进程并存时刻"，§4.7 口径的保守承载，
 *     EX-T07 §6.6 登记同源）；合计占比＝(主＋worker)/物理内存总量；
 *   - 判定统计量＝有效样本最大值（阈值类全样本判定——每有效样本的峰值
 *     占比都须 ≤70%），P95/最小值一并报告仅作分布参考（禁以分位数表述
 *     达标——B-05 非 P95 类基准）；
 *   - 样本量：1 次预热（剔除）＋15 有效样本（performance-baseline §4.2
 *     的"每组有效 ≥15"系 P95 分组下限，本基准按同数自律取 15——T02/T03
 *     先例）；逐样本 fresh worker 进程（独立 Job Scope——作业峰值不跨
 *     样本累积）。
 *
 * 替身边界声明（§11 同源纪律——替身不构成业务证明）：
 *   - worker 评估器＝阶段 A 内置脚本替身（execution.md §3.4 登记形态）——
 *     它证明的是"合计计量口径覆盖主进程＋全部工作进程并存时刻"＋"分批/
 *     流式/检查点机制在大任务管线真实发生"，不是业务评估算法的内存画像
 *     （业务评估器的大内存场景归集成模式扩展测量挂点——首测报告登记，
 *     集成构建由合入批执行）；
 *   - 归档端口＝本 TU 的 DiskArchivePort（真实写盘的测试面替身）——检查
 *     点文件真实落盘；project 存储白名单校验语义归 project 契约测试面
 *     （EX-T04/PRJ-T14 覆盖），不在 B-05 测量面；
 *   - NFR-PERF-04 原文的"缓存"机制面＝execution 缓存协调器（EX-T08
 *     EX-CCH-1/2 用例）与 optimization 消费面承载，随同套件 ctest 全绿
 *     佐证——DTB §2.24 本卡验收行明文为"大任务分批/流式/检查点＋先节流
 *     后诊断"，与腿一场景面一致。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/execution/ChannelProtocol.hpp>
#include <sdurws/ird/execution/Checkpoint.hpp>
#include <sdurws/ird/execution/Ports.hpp>
#include <sdurws/ird/execution/Scheduler.hpp>
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
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace fs = std::filesystem;

namespace {

using namespace sdurws::ird;
using execution::MemoryReading;
using execution::ResourceController;
using execution::SupervisorMemorySampler;
using execution::WorkerSupervisor;
namespace core = sdurws::ird::core;
namespace project = sdurws::ird::project;

// =====================================================================
// 数据集冻结参数（datasetId＝ird-perf-b05-memory，version 1——manifest
// 权威文件 testdata/performance/b05-memory-manifest.json；本处常量必须
// 与 manifest 逐字一致，报告输出参数供漂移核对）
// =====================================================================

/// 驱动线程 poll 周期（单位 ms——事件推进粒度；远小于样本运行时长，
/// 观测滞后占比可忽略，登记于 manifest measurementProtocol）。
constexpr std::chrono::milliseconds kPollPeriod{10};
/// ResourceController 真实采样最小间隔（单位 ms——Config::sampleInterval
/// 实现参数：10 ms 高频采样保证覆盖短任务的内存峰值段；manifest 冻结）。
constexpr std::chrono::milliseconds kSampleInterval{10};
/// 大任务腿有效样本数（阈值类基准的样本量登记——协议 §4.2 的"每组有效
/// 样本 ≥15"系 P95 分组下限，本基准按同数自律取 15；manifest 冻结）。
constexpr std::size_t kMemoryValidSamples = 15;
/// 大任务腿预热次数（剔除不计入判定——与 testkit 分组采样纪律同形）。
constexpr std::size_t kMemoryWarmupRuns = 1;
/// 大任务批次流条数（分批/流式机制的行为观测规模——批量为小确定值使
/// 多批流式回传必然发生；manifest 冻结）。
constexpr std::uint64_t kLargeTaskBatches = 100000;
/// 大任务腿 worker 脚本（检查点×2＋批次流＋FinalOutput——大任务管线
/// 三机制：检查点经 CheckpointCoordinator 真实落盘×2、ResultBatch 10 万条
/// 流式回传、FinalOutput 正常终结；manifest 冻结。批量取 10 万＝批次相位
/// 运行窗 ~0.3 s，对 10 ms 采样间隔即 ~30 个采样点——覆盖下限 5 的
/// ≥4× 余量）。
constexpr const char* kLargeTaskScript =
    "stage-a-script:v1\ncheckpoint 1\nbatches 100000\ncheckpoint 2\nfinal\n";
/// 替身评估器装配 manifest 规范文本（worker 侧握手比对输入——与 worker
/// main.cpp kStageAManifest 同源，T03 同款）。
constexpr const char* kStageAManifest = "stage-a-double|1|7";
/// 合计峰值内存上限（物理内存占比——NFR-PERF-04 上游值照抄，零私改；
/// 判定与 ResourceBudget 消费默认值同源双钉，见腿二阈值钉）。
constexpr double kMaxMemoryRatio = 0.70;
/// 节流阈值（物理内存占比——units/execution.md §15.1 D-05 实现参数，
/// 非需求阈值；腿二攀升剖面的节流步进位）。
constexpr double kThrottleRatio = 0.65;
/// 夹具安全界（非协议值——有界等待的上限，防夹具故障时无限挂起；超界
/// 即显性失败如实报告）。单样本整周期上界。
constexpr std::chrono::seconds kCycleGuardTimeout{60};
/// 夹具安全界：worker 启动到 Running 相位（进程启动＋握手＋派发受理）。
constexpr std::chrono::seconds kRunningGuardTimeout{30};
/// 采样覆盖下限（每有效样本的采样点数下限——低于此说明采样间隔未覆盖
/// 运行窗，峰值读数不可信，样本记无效；10 ms 间隔对 ≥150 ms 的样本运行
/// 即满足——manifest 冻结）。
constexpr std::size_t kMinReadingsPerSample = 5;

// =====================================================================
// 测试设施
// =====================================================================

/**
 * @brief 诊断收集汇（IExecutionDiagnosticsSink 最小实现——结构化/开发双
 *        通道记录）。
 *
 * 腿一用途：真实内存压力下治理层应保持 Normal——结构化诊断计数应为 0
 * （出现即说明本进程组合真实触达节流/耗尽，属如实显性失败面）；腿二
 * 用途：EX-RESOURCE-INSUFFICIENT 经 §3.3 注入 sink 外报恰一次每耗尽
 * 回合（EX-T07 消费契约的对端观测）。
 *
 * 线程约束：report/reportDev 仅在调度线程（驱动线程）被调用；记录读取
 * 在驱动线程 join 后（单线程域）。
 */
class RecordingDiagnosticsSink final : public execution::IExecutionDiagnosticsSink
{
public:
    void report(const core::DiagnosticRecord& record) override
    {
        m_reports.push_back(record);
    }
    void reportDev(const std::string& channel, const std::string& message) override
    {
        m_dev.emplace_back(channel, message);
    }

    /// 结构化诊断记录（join 后单线程域读取）。
    const std::vector<core::DiagnosticRecord>& reports() const noexcept
    {
        return m_reports;
    }

private:
    std::vector<core::DiagnosticRecord> m_reports;          ///< 结构化诊断序
    std::vector<std::pair<std::string, std::string>> m_dev; ///< 开发诊断序
};

/**
 * @brief 归档端口测试面替身（IResultArchivePort——真实写盘的最小实现）。
 *
 * 为什么不是生产 ProjectStore：归档白名单校验是 project 侧语义（PA-1），
 * B-05 的检查点面是"检查点批次经 CheckpointCoordinator 真实落盘"（大任务
 * 检查点机制的行为观测——§8.1 写出位置规则经归档端口三段协议），本替身
 * 把批次字节与 manifest 真实写入磁盘即完成该产物面（与 T03 DiskArchivePort
 * 同形）。写盘失败按环境失败结构化返回（不抛——端口契约）。
 *
 * 线程约束：全部方法仅驱动线程（CheckpointCoordinator 调用域）。
 */
class DiskArchivePort final : public project::IResultArchivePort
{
public:
    project::ArchiveSessionRef begin(const project::ArchiveRequest& request) override
    {
        m_openDirs.push_back(request.runDir);
        return project::ArchiveSessionRef{};   // 空句柄＝自持令牌（替身口径）
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
                                           "perf-b05: 创建目录失败"}};
        }
        for (const project::ArchiveItem& item : batch.items) {
            std::ofstream out(dir / item.relPath, std::ios::binary);
            out.write(reinterpret_cast<const char*>(item.bytes.data()),
                      static_cast<std::streamsize>(item.bytes.size()));
            if (!out) {
                return project::ArchiveStatus{
                    false, project::StoreError{project::StoreErrorCode::DiskFull,
                                               "perf-b05: 写文件失败"}};
            }
        }
        return project::ArchiveStatus{true, std::nullopt};
    }

    project::ArchiveStatus finalize(project::ArchiveSessionRef,
                                    const project::RunManifest&) override
    {
        // manifest.json 发布（存在性即"正式"标记——内容非契约面，同
        // T03/CheckpointCacheTest 替身口径）。
        const fs::path dir = m_openDirs.back();
        std::ofstream out(dir / "manifest.json", std::ios::binary);
        out << "{\"dataset\":\"ird-perf-b05-memory\"}";
        return project::ArchiveStatus{true, std::nullopt};
    }

    void abandon(project::ArchiveSessionRef, project::ArchiveEndReason) override {}

private:
    std::vector<fs::path> m_openDirs;   ///< begin 登记的归档目录（单会话形态）
};

/**
 * @brief 一次大任务样本的测量产物（全部数值为真实采样实测——零预填；
 *        断言面字段在 teardown 后回填，此时驱动线程已停、读数序列单线程
 *        可读）。
 */
struct LargeTaskMeasurement {
    // ---- 测量面（内存峰值的原始样本值）----
    bool assembled = false;               ///< worker 启动＋派发受理就绪
    std::uint64_t totalPhysicalBytes = 0; ///< 物理内存总量（占比分母；末次读数）
    std::uint64_t peakMainBytes = 0;      ///< 主进程工作集峰值（采样序列最大）
    std::uint64_t peakWorkerBytes = 0;    ///< 全部 worker 作业内存峰值（采样序列最大）
    std::uint64_t peakAggregateBytes = 0; ///< 合计峰值（逐采样点合计的最大值）
    double peakRatio = 0.0;               ///< 合计峰值占比（判定统计量输入）
    std::size_t readings = 0;             ///< 采样点数（覆盖度观测面）
    int rcDiagCount = 0;                  ///< 决策携带的结构化诊断数（健康机器应为 0）
    ResourceController::Level maxLevel =
        ResourceController::Level::Normal; ///< 采样期间最高治理层级（观测面）
    std::string failure;                  ///< 非空＝本样本无效原因（夹具/环境断）

    // ---- 断言面（teardown 后回填）----
    bool finalSeen = false;              ///< FinalOutput 已到达
    bool streamedBatches = false;        ///< 批次在 FinalOutput 之前流式到达
    std::uint64_t batchCount = 0;        ///< ResultBatch 到达条数
    int checkpointEvents = 0;            ///< CheckpointBatch 到达个数（脚本=2）
    int checkpointPublished = 0;         ///< 经协调器真实落盘发布个数
    std::vector<fs::path> archiveDirs;   ///< 检查点实际归档目录（留存断言面）
    std::uint32_t workerPid = 0;         ///< worker 主进程 PID
    bool exitSeen = false;               ///< worker 退出已观测
    execution::ExitClassification exitClassification =
        execution::ExitClassification::Crashed;       ///< 退出分类
    std::uint32_t exitCode = 0;            ///< 退出码
};

/**
 * @brief 大任务内存基准驱动编排（一次装配＝一个真实进程上的完整大任务
 *        周期＋生产采样链全程高频采样）。
 *
 * 线程模型（生产装配的基准形态——调度线程显式驱动，T03 CancelTimingRig
 *   同款结构；差异＝无取消协议编排，采样链在驱动线程逐拍 evaluate）：
 *   - 驱动线程（本 rig 唯一的"调度线程"）：WorkerSupervisor/
 *     CheckpointCoordinator/ResourceController/SupervisorMemorySampler 的
 *     全部调用都在本线程（aggregateJobMemoryBytes"仅调度线程"约束的
 *     满足位）；装配完成后进入
 *     {supervisor.poll(); rc.evaluate()→留存决策; sleep(kPollPeriod)}
 *     循环直至 WorkerExited 或夹具安全界；
 *   - 测试线程：assemble() 发起、有界等待完成、teardown 后读取测量与
 *     断言面。
 *
 * 生命周期：每样本构造一个 rig（独立 worker 进程＋独立 Job Scope——作业
 * 峰值不跨样本累积），析构＝join 驱动线程＋监督器析构兜杀（不留孤儿
 * 进程）。
 */
class MemoryThrottleRig final : public execution::IWorkerEventSink
{
public:
    MemoryThrottleRig() = default;

    ~MemoryThrottleRig() override { teardown(); }

    MemoryThrottleRig(const MemoryThrottleRig&) = delete;
    MemoryThrottleRig& operator=(const MemoryThrottleRig&) = delete;

    /**
     * @brief 装配一个大任务运行（驱动线程内完成）。
     *
     * @param script        [in] worker 脚本（stage-a-script:v1 文本——承载
     *                      于派发物快照字节，替身评估器解读）
     * @param checkpointDir [in] 检查点归档目录（非空＝到达的 CheckpointBatch
     *                      经 CheckpointCoordinator 真实写盘——大任务检查点
     *                      机制行为面；空＝只透传不归档）
     * @return 就绪（worker 派发受理＋协议绑定完成）；false＝装配失败
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

    /// worker 主进程 PID（0＝未知——Running 相位前）。
    std::uint32_t workerPid() const noexcept { return m_workerPid.load(); }

    /// WorkerExited 已观测（驱动线程事件面）。
    bool workerExitSeen() const noexcept { return m_exitSeen.load(); }

    /// 退出分类（exitSeen 后有效——原子量跨线程可读）。
    execution::ExitClassification workerExitClassification() const noexcept
    {
        return m_exitClassification.load();
    }

    /// 退出码（exitSeen 后有效）。
    std::uint32_t workerExitCode() const noexcept { return m_exitCode.load(); }

    /// 收尾：停驱动线程＋按依赖序释放调度域对象＋监督器析构（兜杀残留
    /// worker——不留孤儿进程）。
    ///
    /// 释放序说明：资源控制器/检查点协调器持有诊断 sink（本类成员）的
    /// 裸引用——成员逆序析构会先销毁 sink 再销毁持有者，若其析构路径
    /// 触碰 sink 即悬垂；故在本函数（析构体先于成员析构执行）内按
    /// "消费者先于被引用者"显式释放，保证 sink 生存期覆盖全部持有者。
    /// 析构自动调用；显式调用后即可在测试线程读取采样序列与事件观测面
    /// （单线程域——驱动线程已 join，零并发访问）。
    void teardown()
    {
        if (m_stopRequested.exchange(true)) { return; }   // 幂等
        if (m_driver.joinable()) { m_driver.join(); }
        m_rc.reset();                    // 消费 sampler 与 sink——先释放
        m_checkpointCoordinator.reset(); // 消费归档端口与 sink——次释放
        m_sampler.reset();               // 消费监督器（作业合计源）
        m_supervisor.reset();   // 监督器析构＝作业兜杀（KILL_ON_JOB_CLOSE）
    }

    // ---- teardown 后单线程域读取面（采样序列与事件观测）----

    /// 采样决策序列（teardown 后——驱动线程已停，单线程域）。
    const std::vector<ResourceController::Decision>& decisions() const noexcept
    {
        return m_decisions;
    }

    /// 决策携带的结构化诊断总数（观测面——健康机器应为 0）。
    int rcDiagnosticCount() const noexcept
    {
        int n = 0;
        for (const ResourceController::Decision& d : m_decisions) {
            if (d.diagnostic.has_value()) { ++n; }
        }
        return n;
    }

    /// 采样期间最高治理层级（观测面）。
    ResourceController::Level maxLevel() const noexcept
    {
        ResourceController::Level lvl = ResourceController::Level::Normal;
        for (const ResourceController::Decision& d : m_decisions) {
            if (d.level > lvl) { lvl = d.level; }
        }
        return lvl;
    }

    /// FinalOutput 已到达。
    bool finalSeen() const noexcept { return m_finalSeen.load(); }

    /// ResultBatch 到达条数（流式回传规模观测面）。
    std::uint64_t batchCount() const noexcept { return m_batchCount.load(); }

    /// 批次先于 FinalOutput 到达（流式机制到达序判据）。
    bool streamedBatches() const noexcept { return m_streamedBatches.load(); }

    /// CheckpointBatch 到达个数。
    int checkpointEvents() const noexcept { return m_checkpointEvents.load(); }

    /// 经协调器真实落盘发布的检查点个数。
    int checkpointPublished() const noexcept
    {
        return m_checkpointPublished.load();
    }

    /// 检查点实际归档目录（留存断言面——join 后单线程域读取）。
    const std::vector<fs::path>& archiveDirs() const noexcept { return m_archiveDirs; }

    /// 结构化诊断收集汇（join 后单线程域读取——腿二对端观测面）。
    const RecordingDiagnosticsSink& diagSink() const noexcept { return m_diagSink; }

private:
    // ---- IWorkerEventSink（驱动线程内被 supervisor.poll 调用）----
    void onWorkerEvent(const execution::WorkerEvent& event) override
    {
        switch (event.kind) {
        case execution::WorkerEvent::Kind::DispatchAccepted:
            // 派发已被 worker 身份核对接受（帧处理事件）——装配同步锚点
            // （T03 同款：status 相位 Running 在派发请求"发出"即置位，
            // 早于受理一个握手往返，不作锚点）。
            m_dispatchAccepted.store(true);
            break;
        case execution::WorkerEvent::Kind::ResultBatch:
            // 流式批次到达（驱动线程内同步事件——每条计数；到达序证据
            // 由 FinalOutput 处的"计数>0"固化为 streamedBatches）。
            m_batchCount.fetch_add(1);
            break;
        case execution::WorkerEvent::Kind::CheckpointBatch:
            // 检查点批次到达——经 CheckpointCoordinator 真实写盘（驱动
            // 线程域＝协调器调用域；发布结论留存断言面）。T03 同款。
            m_checkpointEvents.fetch_add(1);
            archiveCheckpoint(event);
            break;
        case execution::WorkerEvent::Kind::FinalOutput:
            m_finalSeen.store(true);
            // 流式判据固化：FinalOutput 到达时刻批次计数已 >0——批次是
            // 运行中逐条回传（脚本序：batches 在 final 之前），不是终局
            // 一次性投递（NFR-PERF-03 流式回传语义的到达序观测面）。
            m_streamedBatches.store(m_batchCount.load() > 0);
            break;
        case execution::WorkerEvent::Kind::WorkerExited:
            m_exitCode.store(event.exitCode);
            m_exitClassification.store(event.exit);
            m_exitSeen.store(true);
            break;
        default:
            break;   // 握手/进度等——本基准不消费
        }
    }

    /// 检查点批次→CheckpointCoordinator 真实写盘（驱动线程；T03 同款）。
    void archiveCheckpoint(const execution::WorkerEvent& event)
    {
        if (!m_checkpointCoordinator) { return; }   // 未装配归档面——只透传
        if (event.checkpointSequence == 0 || event.bytes.empty()) {
            return;   // 载荷异常——事件计数已如实++，发布面留空即断言失败
        }
        const std::uint64_t sequence = event.checkpointSequence;
        // 组装写出请求：记录字段按 CheckpointCacheTest/T03 同款合法形态
        // ——身份字段以内容身份派生（非零即合法）；载荷面（引用清单＋
        // 复合摘要）由协调器按实际批次重算补全（申报面预填值被忽略）。
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
        req.record.completedUnits = sequence;
        req.record.totalUnits = 100;
        req.record.resumable = true;
        execution::CheckpointBatchFile file;
        file.relPath = "part-000.bin";
        file.bytes = event.bytes;
        req.files.push_back(file);
        req.archiveDir = m_checkpointDir / m_identity.run.toCanonical()
                         / std::to_string(sequence);

        const execution::CheckpointWriteResult result =
            m_checkpointCoordinator->writeCheckpoint(req);
        if (result.outcome == execution::CheckpointWriteResult::Outcome::Published) {
            m_archiveDirs.push_back(req.archiveDir);
            m_checkpointPublished.fetch_add(1);
        }
    }

    /// 驱动线程主过程（本 rig 唯一的调度线程：装配→采样循环至收尾）。
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

        // 生产采样链（EX-T07 落位件的真实装配形态）：SupervisorMemory
        // Sampler＝MemProbe 三探针（系统/主进程工作集）＋监督器作业内存
        // 合计（全部活 worker 的 JobMemory 峰值求和）；ResourceController
        // 以 10 ms 采样间隔逐拍评估（决策仍由调度线程执行——§6.2）。
        // 诊断出口注入收集汇（健康机器上结构化诊断应为 0——观测面）。
        auto sampler = std::make_unique<SupervisorMemorySampler>(supervisor.get());
        ResourceController::Config rcConfig;
        rcConfig.sampleInterval = kSampleInterval;
        auto rc = std::make_unique<ResourceController>(*sampler, rcConfig);
        rc->setDiagnosticsSink(&m_diagSink);

        // 检查点协调器（大任务检查点机制行为面——归档端口＋诊断 sink）。
        if (!checkpointDir.empty()) {
            m_checkpointDir = checkpointDir;
            m_checkpointCoordinator =
                std::make_unique<execution::CheckpointCoordinator>(m_diskPort,
                                                                   m_diagSink);
        }

        // 受管任务身份装配（T03 同款——派发五元组；本基准无状态机编排，
        // 完成证据＝FinalOutput 事件＋worker 退出码 0，不经九步接纳面
        // ——接纳面归 EX-T04 契约测试，B-05 测量面不含）。
        m_identity.project = core::ProjectId::generate();
        m_identity.branch = core::BranchId::generate();
        m_identity.revision = core::RevisionId::generate();
        m_identity.run = core::RunId::generate();
        m_identity.attempt = core::AttemptId{1};

        // worker 派发绑定（脚本承载于快照字节——T03 同构形态）。
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
            finishAssemble(false, "perf-b05: worker 启动失败");
            return;
        }
        const execution::WorkerId workerId = launched.worker;

        // 派发受理有界等待（进程启动＋握手＋派发被 worker 身份核对接受
        // ——夹具安全界；握手协议由本线程 poll 推进。同步锚点＝
        // DispatchAccepted 事件，T03 同款）。
        const auto assembleDeadline =
            std::chrono::steady_clock::now() + kRunningGuardTimeout;
        while (!m_stopRequested.load(std::memory_order_acquire)) {
            supervisor->poll();
            if (m_dispatchAccepted.load()) { break; }
            if (m_exitSeen.load()) {
                finishAssemble(false, "perf-b05: worker 提前退出（握手拒绝等）");
                return;
            }
            if (std::chrono::steady_clock::now() > assembleDeadline) {
                finishAssemble(false, "perf-b05: 派发受理等待超界（夹具安全界）");
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{2});
        }
        m_workerPid.store(supervisor->status(workerId).pid);

        // 装配完成放行测试线程；本线程转入"推进＋采样"循环直至退出事件。
        const auto cycleDeadline =
            std::chrono::steady_clock::now() + kCycleGuardTimeout;
        m_supervisor = std::move(supervisor);
        m_sampler = std::move(sampler);
        m_rc = std::move(rc);
        finishAssemble(true, "");

        while (!m_stopRequested.load(std::memory_order_acquire)) {
            m_supervisor->poll();     // 通道/握手/退出分类推进（事件→本 sink）
            // 生产采样链逐拍评估（10 ms 间隔节流真实采样，间隔内复用最近
            // 决策——Config 语义）；决策序列全量留存（峰值统计与迹观测面）。
            m_decisions.push_back(m_rc->evaluate());
            if (m_exitSeen.load(std::memory_order_acquire)) { break; }
            if (std::chrono::steady_clock::now() > cycleDeadline) { break; }
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
    std::unique_ptr<WorkerSupervisor> m_supervisor;     ///< worker 池监督器
    std::unique_ptr<SupervisorMemorySampler> m_sampler; ///< 生产采样件（EX-T07）
    std::unique_ptr<ResourceController> m_rc;           ///< 三段治理控制器
    std::unique_ptr<execution::CheckpointCoordinator> m_checkpointCoordinator; ///< 检查点协调器
    RecordingDiagnosticsSink m_diagSink;                ///< 诊断收集汇
    DiskArchivePort m_diskPort;                         ///< 归档端口（真实写盘替身）
    core::TaskIdentity m_identity{};                    ///< 派发五元组
    std::vector<ResourceController::Decision> m_decisions; ///< 采样决策序列
    std::vector<fs::path> m_archiveDirs;                ///< 检查点实际归档目录
    fs::path m_checkpointDir;                           ///< 检查点归档根

    // ---- 跨线程观测（原子量：驱动线程写、测试线程读）----
    std::atomic<std::uint32_t> m_workerPid{0};          ///< worker 主进程 PID
    std::atomic<bool> m_exitSeen{false};                ///< WorkerExited 已观测
    std::atomic<std::uint32_t> m_exitCode{0};           ///< 退出码
    std::atomic<execution::ExitClassification> m_exitClassification{
        execution::ExitClassification::Crashed};        ///< 退出分类
    std::atomic<bool> m_finalSeen{false};               ///< FinalOutput 已到达
    std::atomic<std::uint64_t> m_batchCount{0};         ///< ResultBatch 到达条数
    std::atomic<int> m_checkpointEvents{0};             ///< CheckpointBatch 到达数
    std::atomic<int> m_checkpointPublished{0};          ///< 检查点发布数
    std::atomic<bool> m_dispatchAccepted{false};        ///< 派发已被 worker 受理（装配锚点）
    std::atomic<bool> m_streamedBatches{false};         ///< 批次先于 FinalOutput 到达

    // ---- 线程编排 ----
    std::thread m_driver;                ///< 驱动线程（装配＋采样循环）
    mutable std::mutex m_assembleMutex;  ///< 装配结论互斥
    std::condition_variable m_assembleCv;
    bool m_assembleDone = false;
    bool m_assembleOk = false;
    std::string m_assembleFailure;
    std::atomic<bool> m_stopRequested{false};
    std::vector<core::DiagnosticRecord> m_launchDiagnostics; ///< 启动失败诊断
};

// =====================================================================
// 单样本执行器：一个真实进程上的完整大任务周期＋内存峰值测量
// =====================================================================

/**
 * @brief 执行一次大任务周期并测量合计内存峰值（真实采样——零虚拟零预填）。
 *
 * 周期：装配（worker 启动→派发受理）→大任务管线（检查点×2 落盘→10 万条
 * 批次流式回传→FinalOutput→退出码 0）→全程 10 ms 采样→teardown 后统计
 * 峰值与回填断言面。任何夹具断以返回 failure 留因（采样循环记无效样本）。
 *
 * @param script        [in] worker 脚本
 * @param checkpointDir [in] 检查点归档目录（大任务检查点机制行为面）
 * @return 测量产物（assembled==false 时测量面全零＋failure 留因）
 */
LargeTaskMeasurement runLargeTaskSample(const std::string& script,
                                        const fs::path& checkpointDir = {})
{
    LargeTaskMeasurement m;
    MemoryThrottleRig rig;
    if (!rig.assemble(script, checkpointDir)) {
        m.failure = rig.assembleFailure();
        if (!rig.launchDiagnostics().empty()) {
            m.failure += "（首诊断：" + rig.launchDiagnostics().front().cause + "）";
        }
        return m;   // 装配失败＝无效样本（原因留痕——不计入判定分母）
    }
    m.assembled = true;

    // ---- 运行完成有界等待（WorkerExited＝夹具安全界；FinalOutput 与
    // 退出事件都在驱动线程事件面——等退出即等全周期收尾）----
    const auto deadline = std::chrono::steady_clock::now() + kCycleGuardTimeout;
    while (!rig.workerExitSeen()) {
        if (std::chrono::steady_clock::now() > deadline) {
            m.failure = "perf-b05: 等待 worker 退出超界（夹具安全界——非协议判定）";
            rig.teardown();
            return m;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }

    // ---- 收尾（join 驱动线程；此后单线程域读采样序列与事件观测面）----
    rig.teardown();

    // 峰值统计：逐采样点的"主进程＋全部 worker"合计取最大（§4.7 合计
    // 口径——各半区独立计量后逐点汇总；worker 半区为作业峰值单调量，
    // 天然覆盖并存时刻）；物理总量与两半区峰值一并留档。
    for (const ResourceController::Decision& d : rig.decisions()) {
        const MemoryReading& r = d.reading;
        m.totalPhysicalBytes = r.totalPhysicalBytes;   // 常量——末次即全序
        m.peakMainBytes = std::max(m.peakMainBytes, r.mainProcessBytes);
        m.peakWorkerBytes = std::max(m.peakWorkerBytes, r.workerBytes);
        m.peakAggregateBytes = std::max(m.peakAggregateBytes, r.aggregateBytes());
    }
    m.readings = rig.decisions().size();
    m.rcDiagCount = rig.rcDiagnosticCount();
    m.maxLevel = rig.maxLevel();
    if (m.totalPhysicalBytes > 0 && m.peakAggregateBytes > 0) {
        m.peakRatio = static_cast<double>(m.peakAggregateBytes)
                      / static_cast<double>(m.totalPhysicalBytes);
    }

    // 事件面回填。
    m.finalSeen = rig.finalSeen();
    m.streamedBatches = rig.streamedBatches();
    m.batchCount = rig.batchCount();
    m.checkpointEvents = rig.checkpointEvents();
    m.checkpointPublished = rig.checkpointPublished();
    m.archiveDirs = rig.archiveDirs();
    m.workerPid = rig.workerPid();
    m.exitSeen = rig.workerExitSeen();
    m.exitClassification = rig.workerExitClassification();
    m.exitCode = rig.workerExitCode();
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
    r.baselineDoc = "performance-baseline #2 B-05";
    // 软件版本锁定（§4.1）：commit 由执行方经 IRD_PERF_GIT_COMMIT 注入
    //（缺省如实留空——commit SHA 在首测报告文档登记）。
    r.gitCommit = envOr("IRD_PERF_GIT_COMMIT");
    r.buildConfig = "Release";
    r.datasetId = "ird-perf-b05-memory";
    r.datasetVersion = "1";
    r.machineName = envOr("COMPUTERNAME");
    r.cpuInfo = envOr("PROCESSOR_IDENTIFIER")
                + " x" + envOr("NUMBER_OF_PROCESSORS", "?");
    r.osInfo = envOr("OS", "Windows") + " " + envOr("PROCESSOR_ARCHITECTURE");
    // 内存/存储/电源：环境变量不可得——如实指向首测报告的硬件清单。
    r.memoryInfo = "见首测报告硬件清单（traceability/benchmarks/"
                   "wp23-t06-memory-baseline.md §2）";
    r.storageInfo = "见首测报告硬件清单";
    r.powerPolicyNote = "见首测报告硬件清单";
    r.backgroundNote = "每样本一个独立 worker 进程（顺序启动/终结）；测量"
                       "期间无其他基准并行（performance-baseline §4 测量"
                       "纪律——串行跑分）";
    // 口径登记（报告自含——验收对照面）。
    r.notes.push_back(
        "B-05 为内存阈值类判定（performance-baseline §2 统计口径区分）："
        "判定统计量＝有效样本最大值（全样本须达标），非 P95 分位数；"
        "verdicts.measuredP95Ms 字段沿夹具 schema 承载该判定统计量"
        "（无量纲占比，非毫秒——字段名沿 schema 的登记性复用，T03 同款"
        "纪律）");
    r.notes.push_back(
        "合计口径（performance-baseline §4.7）＝主进程工作集＋全部 worker"
        "作业内存峰值（QueryInformationJobObject PeakJobMemoryUsed——单调"
        "不回落，天然覆盖工作进程峰值与主进程并存时刻）；生产采样链＝"
        "SupervisorMemorySampler＋ResourceController（EX-T07 落位件），"
        "驱动线程 10 ms poll＋10 ms 采样间隔");
    r.notes.push_back(
        "替身边界：评估器＝阶段 A 脚本替身（§3.4 登记形态）——证明合计"
        "计量口径覆盖并存时刻＋分批/流式/检查点机制真实发生，非业务评估"
        "算法内存画像（归集成模式扩展测量挂点）；归档端口＝真实写盘测试"
        "面替身——详见用例文件头与首测报告 §4");
    r.notes.push_back(
        "数据集 manifest＝testdata/performance/b05-memory-manifest.json"
        "（datasetId ird-perf-b05-memory@1——参数冻结，升级换版本）");
    return r;
}

/// 单腿阈值判定记录（判定统计量＝有效样本最大值——阈值类全样本判定；
/// measuredP95Ms 字段沿夹具 schema 承载该统计量，notes 已声明口径）。
testkit::PerfThresholdVerdict
makeMaxVerdict(const testkit::InteractionGroupReport& g, double threshold,
               const char* thresholdSource)
{
    testkit::PerfThresholdVerdict v;
    v.opId = g.opId;
    v.stateId = g.stateId;
    v.thresholdMs = threshold;
    v.thresholdSource = thresholdSource;
    v.measuredP95Ms = g.maxValidMs;   // 判定统计量（见 notes 口径声明）
    v.pass = g.maxValidMs <= threshold;
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
    std::vector<double> validVals;
    for (const testkit::InteractionSample& s : samples) {
        if (s.valid && !s.excluded) {
            ++g.validCount;
            validVals.push_back(s.durationMs);
        } else if (!s.valid) {
            ++g.invalidCount;
        } else {
            ++g.warmupRuns;
        }
    }
    if (!validVals.empty()) {
        g.p95Ms = testkit::percentileLinearInterpolated(validVals, 0.95);
        g.maxValidMs = *std::max_element(validVals.begin(), validVals.end());
        g.minValidMs = *std::min_element(validVals.begin(), validVals.end());
    }
    return g;
}

/// 从磁盘读取文件字节（检查点留存核对——发布后重新落盘读出）。
bool readDiskFile(const fs::path& path, std::vector<std::uint8_t>& out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) { return false; }
    out.assign(std::istreambuf_iterator<char>(in),
               std::istreambuf_iterator<char>());
    return true;
}

// =====================================================================
// 腿二设施：脚本占比采样器（§11 登记的 IMemorySampler 注入面——
// EX-RES-2 同一接缝；分母来自 MemProbe 真实探针）
// =====================================================================

/**
 * @brief 脚本占比采样器（腿二"先节流后诊断"时序语义判定的驱动源）。
 *
 * 为什么不是真实内存攀升：把本进程组合真实推至物理内存 65%/70% 需要分配
 *   数 GB 量级——侵入宿主机、与串行测量纪律冲突且不可控；§11 用例行明文
 *   "内存采样 fake 推至 >70%"是本接缝的设计用途（EX-RES-2 功能面同源）。
 *   本基准的增量＝分母真实（MemProbe querySystemMemory 经
 *   SupervisorMemorySampler 空池半区采样——物理内存总量是本机真实值）＋
 *   决策对象真实（ResourceController 生产判定本体＋真实 DiagnosticRecord
 *   构造与 sink 出具）＋判定统计量＝决策序列迹（先节流后诊断的时序语义）。
 *
 * 字节定标（防浮点边界翻转）：步进值以整数运算从真实总量 T 定标——
 *   below 步取 floor(r×T)（重算占比 ≤r，严格低于边界）；at/above 步取
 *   ceil(r×T)（重算占比 ≥r，严格达到边界）——三段判定"≥ 语义"
 *   （throttleRatio ≤ 占比 < maxMemoryRatio）在任意真实 T 下确定可复现，
 *   不受 (r×T)/T 的 double 舍入方向影响（NFR-COR-02 同源精神）。
 *
 * 线程约束：sample 仅在调用域线程被调用（本腿单线程顺序驱动——无并发）。
 */
class ScriptedRatioSampler final : public execution::IMemorySampler
{
public:
    /// 以真实物理总量初始化（失败＝false——调用方显性失败，不猜值）。
    bool initFromRealSystem()
    {
        SupervisorMemorySampler probe(nullptr);   // 空池装配——系统/主进程半区照常
        const std::optional<MemoryReading> r = probe.sample();
        if (!r.has_value() || r->totalPhysicalBytes == 0) { return false; }
        m_total = r->totalPhysicalBytes;
        return true;
    }

    /// 物理内存总量（字节——initFromRealSystem 成功后有效）。
    std::uint64_t totalPhysicalBytes() const noexcept { return m_total; }

    /// 设定本拍合计占用（字节定标——见类注；main/worker 三七拆分仅为
    /// 读数形态完整，判定只消费合计占比）。
    void setAggregateBytes(std::uint64_t aggregate)
    {
        m_aggregate = aggregate;
        m_main = aggregate / 3;
        m_workers = aggregate - m_main;
    }

    std::optional<MemoryReading> sample() override
    {
        ++m_calls;
        MemoryReading r;
        r.totalPhysicalBytes = m_total;
        r.mainProcessBytes = m_main;
        r.workerBytes = m_workers;
        return r;
    }

    /// 采样调用次数（逐拍采样核对面——Config::sampleInterval=0 语义）。
    int calls() const noexcept { return m_calls; }

private:
    std::uint64_t m_total = 0;     ///< 物理内存总量（真实探针值；单位字节）
    std::uint64_t m_aggregate = 0; ///< 脚本合计占用（单位字节）
    std::uint64_t m_main = 0;      ///< 主进程半区（拆分形态；单位字节）
    std::uint64_t m_workers = 0;   ///< worker 半区（拆分形态；单位字节）
    int m_calls = 0;               ///< 采样调用计数
};

/// 整数 floor 定标：⌊ratio×T⌋（严格低于边界的步进值）。
std::uint64_t scaleFloor(double ratio, std::uint64_t total)
{
    return static_cast<std::uint64_t>(ratio * static_cast<double>(total));
}

/// 整数 ceil 定标：⌈ratio×T⌉（达到/越过边界的步进值）。
std::uint64_t scaleCeil(double ratio, std::uint64_t total)
{
    const double raw = ratio * static_cast<double>(total);
    const std::uint64_t floored = static_cast<std::uint64_t>(raw);
    return (static_cast<double>(floored) < raw) ? floored + 1 : floored;
}

/// 决策迹一行（报告留痕——tick 序＋占比＋层级＋两标志＋诊断边沿）。
std::string traceLine(int tick, double ratio, ResourceController::Level level,
                      bool pauseDispatch, bool reclaim, bool hasDiag)
{
    const char* levelName = level == ResourceController::Level::Normal  ? "Normal"
                            : level == ResourceController::Level::Throttled
                                ? "Throttled"
                                : "Exhausted";
    return "tick#" + std::to_string(tick)
         + " ratio=" + std::to_string(ratio)
         + " level=" + levelName
         + " pauseDispatch=" + (pauseDispatch ? "1" : "0")
         + " reclaimIdle=" + (reclaim ? "1" : "0")
         + " diagEdge=" + (hasDiag ? "1" : "0");
}

}  // namespace

// =====================================================================
// 基准用例（B-05 两腿——腿一真实内存峰值；腿二先节流后诊断时序语义）
// =====================================================================

/**
 * 基准用例（B-05 腿一：合计峰值内存基准——NFR-PERF-04 条款"主进程与全部
 * 工作进程合计峰值内存默认不超过物理内存 70%"）：
 * 真实独立 worker 进程执行大任务管线（检查点×2 经 CheckpointCoordinator
 * 真实落盘→100,000 条 ResultBatch 流式回传→FinalOutput→退出码 0），生产
 * 采样链（SupervisorMemorySampler＋ResourceController）全程 10 ms 采样；
 * 判定统计量＝有效样本最大值（阈值类全样本判定——每有效样本合计峰值占比
 * ≤70%）。附加证据面：分批/流式/检查点三机制行为观测（批次计数恰 100000、
 * 批次先于 FinalOutput 到达＝流式、检查点发布×2 且磁盘 manifest＋批次
 * 文件留存）、治理层全程 Normal（健康机器上本进程组合不触达节流——出现
 * 节流/诊断即如实失败）、采样覆盖度（每样本 ≥5 个采样点）。
 * 1 次预热（剔除）＋15 有效样本，逐样本 fresh worker 进程。
 */
TEST(LargeTaskMemoryBenchmark, LargeTaskBatchStreamCheckpointPeak_WP23T06_B05)
{
    IRD_TEST_INFO("NFR-PERF-04", {"B-05"}, std::nullopt);

    // 检查点临时归档根（本用例私有——目录名含时刻＋PID 防撞；失败保留
    // 现场，成功清理——TempDir 惯例）。
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path checkpointRoot = fs::temp_directory_path()
        / ("ird-perf-b05-ckpt-" + std::to_string(stamp) + "-"
           + std::to_string(::GetCurrentProcessId()));
    std::error_code ec;
    fs::create_directories(checkpointRoot, ec);
    ASSERT_FALSE(ec) << "检查点临时目录创建失败";

    std::vector<testkit::InteractionSample> peakSamples;  ///< 峰值占比原始序列
    std::string warmupNote;   ///< 预热腿实测摘要（留痕不判定）
    std::string perSampleNote; ///< 逐样本字节明细（报告留痕——§4.8 原始序列）

    // ---- 预热腿（剔除不计入判定——首跑含进程启动链路的冷效应）----
    {
        const LargeTaskMeasurement w = runLargeTaskSample(kLargeTaskScript,
                                                          checkpointRoot);
        ASSERT_TRUE(w.assembled) << "预热样本装配失败：" << w.failure;
        ASSERT_TRUE(w.failure.empty()) << "预热样本失败：" << w.failure;
        testkit::InteractionSample s;
        s.excluded = true;
        s.durationMs = w.peakRatio;
        peakSamples.push_back(s);
        warmupNote = "预热腿实测 peakRatio=" + std::to_string(w.peakRatio)
                     + "（剔除）";
        std::fprintf(stderr, "[perf-b05] %s\n", warmupNote.c_str());
    }

    // ---- 采样腿（15 有效样本——逐样本 fresh worker 进程＋独立 Job Scope）----
    for (std::size_t i = 0; i < kMemoryValidSamples; ++i) {
        const LargeTaskMeasurement m = runLargeTaskSample(kLargeTaskScript,
                                                          checkpointRoot);
        testkit::InteractionSample s;
        if (!m.assembled || !m.failure.empty()) {
            // 无效样本：测量区间不完整——原因留痕不计入分母（逐例打点，
            // 消费方断言零无效收口——testkit 采样纪律）。
            s.valid = false;
            s.invalidReason = m.failure;
            std::fprintf(stderr, "[perf-b05] invalid sample #%zu: %s\n", i + 1,
                         m.failure.c_str());
        } else {
            s.durationMs = m.peakRatio;
        }
        peakSamples.push_back(s);

        // 有效样本逐项断言（行为面＋判定面——即时断言，不积压到统计后）。
        if (m.assembled && m.failure.empty()) {
            // ---- 大任务三机制行为面（NFR-PERF-04"分批、流式结果……
            // 和检查点"的管线行为观测——DTB §2.24 本卡验收行口径）----
            ASSERT_TRUE(m.finalSeen)
                << "样本 #" << i + 1 << " 未收到 FinalOutput（大任务未正常完成）";
            EXPECT_EQ(m.batchCount, kLargeTaskBatches)
                << "样本 #" << i + 1 << " 批次流到达数非 " << kLargeTaskBatches
                << "（分批机制面不完整）";
            EXPECT_TRUE(m.streamedBatches)
                << "样本 #" << i + 1 << " 批次未先于 FinalOutput 到达"
                << "（流式回传机制面缺失）";
            ASSERT_EQ(m.checkpointEvents, 2)
                << "样本 #" << i + 1 << " 检查点事件数非 2（脚本=2——机制面缺失）";
            ASSERT_EQ(m.checkpointPublished, 2)
                << "样本 #" << i + 1 << " 检查点发布数非 2（协调器落盘失败）";
            for (const fs::path& dir : m.archiveDirs) {
                EXPECT_TRUE(fs::exists(dir / "manifest.json"))
                    << "样本 #" << i + 1 << " 检查点归档 manifest 缺失（"
                    << dir.string() << "）";
            }
            // ---- 进程面证据（真实运行完成的证据链）----
            EXPECT_EQ(m.exitClassification,
                      execution::ExitClassification::NormalCompletion)
                << "样本 #" << i + 1 << " worker 退出分类非正常终结（退出码 "
                << m.exitCode << "）";
            EXPECT_EQ(m.exitCode, 0u)
                << "样本 #" << i + 1 << " worker 退出码非 0";
            // ---- 采样覆盖度（峰值读数可信前提——manifest 冻结下限）----
            EXPECT_GE(m.readings, kMinReadingsPerSample)
                << "样本 #" << i + 1 << " 采样点数 "
                << m.readings << " 低于覆盖下限 " << kMinReadingsPerSample
                << "（采样间隔未覆盖运行窗——峰值不可信）";
            // ---- 治理层观测面（健康机器：本进程组合不触达节流/耗尽——
            // 出现即真实内存压力，如实显性失败不带病判过）----
            EXPECT_EQ(m.maxLevel, ResourceController::Level::Normal)
                << "样本 #" << i + 1 << " 采样期间治理层触达 "
                << static_cast<int>(m.maxLevel)
                << "（本进程组合真实内存压力——非预期，如实失败）";
            EXPECT_EQ(m.rcDiagCount, 0)
                << "样本 #" << i + 1 << " 出具 "
                << m.rcDiagCount << " 条 EX-RESOURCE-INSUFFICIENT 诊断"
                << "（真实资源不足——如实失败）";
            // ---- B-05 判定（NFR-PERF-04 原文：合计峰值 ≤ 物理内存 70%）----
            EXPECT_LE(m.peakRatio, kMaxMemoryRatio)
                << "样本 #" << i + 1 << " 合计峰值占比 " << m.peakRatio
                << " 超阈 " << kMaxMemoryRatio
                << "（NFR-PERF-04：主进程＋全部工作进程合计 ≤70% 物理内存）";
        }

        // 逐样本字节明细留痕（§4.8"不得只报统计值"的原始序列面）。
        char line[256];
        std::snprintf(line, sizeof(line),
                      "sample#%zu: main=%.1fMB worker=%.1fMB agg=%.1fMB"
                      " total=%.1fGB ratio=%.6f readings=%zu level=%d\n",
                      i + 1,
                      static_cast<double>(m.peakMainBytes) / 1048576.0,
                      static_cast<double>(m.peakWorkerBytes) / 1048576.0,
                      static_cast<double>(m.peakAggregateBytes) / 1048576.0,
                      static_cast<double>(m.totalPhysicalBytes) / 1073741824.0,
                      m.peakRatio, m.readings, static_cast<int>(m.maxLevel));
        perSampleNote += line;
        std::fprintf(stderr, "[perf-b05] %s", line);
    }

    // ---- 统计与判定（判定统计量＝有效样本最大值；B-05 非 P95 类）----
    const auto peakGroup = makeGroupReport("combined-peak-memory-ratio",
                                           "real-worker-large-task",
                                           peakSamples);
    ASSERT_EQ(peakGroup.validCount, kMemoryValidSamples)
        << "有效样本不足（manifest 冻结的样本量）";
    ASSERT_EQ(peakGroup.invalidCount, std::size_t{0})
        << "存在无效样本（失败留痕见 stderr/报告——不带病判过）";

    // 条款：主进程＋全部工作进程合计峰值 ≤ 物理内存 70%（NFR-PERF-04 原文
    // ——阈值类全样本判定，统计量＝有效样本最大值）。
    EXPECT_LE(peakGroup.maxValidMs, kMaxMemoryRatio)
        << "合计峰值占比超阈（NFR-PERF-04：≤70%——max="
        << peakGroup.maxValidMs << "）";

    // ---- 报告落盘（拖挂——IRD_PERF_REPORT_DIR 未设则跳过）----
    testkit::PerfSessionReport session =
        makeSessionHeader("b05-large-task-memory");
    session.groups.push_back(peakGroup);
    session.verdicts.push_back(makeMaxVerdict(
        peakGroup, kMaxMemoryRatio,
        "REQUIREMENTS NFR-PERF-04（主进程＋全部工作进程合计峰值内存默认"
        "不超过物理内存 70%——原文照抄零私改）"));
    session.allPassed = session.verdicts.front().pass;
    session.notes.push_back(warmupNote);
    session.notes.push_back(
        "大任务管线行为面（15/15 样本全数成立）：批次流恰 100000 条、批次"
        "先于 FinalOutput 到达（流式）、检查点×2 经 CheckpointCoordinator"
        "发布且磁盘留存——分批/流式/检查点三机制行为观测");
    session.notes.push_back(
        "治理层观测：全部样本全程 Normal、零 EX-RESOURCE-INSUFFICIENT"
        "（本进程组合未触达 65%/70%——健康机器预期面；先节流后诊断时序"
        "语义由腿二 b05-throttle-ordering 报告承载）");
    session.notes.push_back(perSampleNote);
    if (const auto dir = testkit::perfReportDirFromEnv()) {
        EXPECT_TRUE(testkit::writePerfReportJson(*dir, session))
            << "报告写失败（拖挂不阻断——检查 IRD_PERF_REPORT_DIR）";
    }

    // 成功清理（失败保留现场——TempDir 惯例）。
    if (!HasNonfatalFailure()) {
        fs::remove_all(checkpointRoot, ec);
    }
}

/**
 * 基准用例（B-05 腿二：先节流后诊断时序语义——performance-baseline §4.7
 * 明文"『接近上限先节流再给资源不足诊断』的时序语义由 WP-23-T06 用例
 * 承载"）：脚本占比采样器（§11 登记的 IMemorySampler 注入面——分母取
 * 自 MemProbe 真实探针）驱动确定性攀升剖面 60%→64.9%→65.0%→69.9%→
 * 70.0%→71%→69%→64%→70.5%（manifest 冻结），ResourceController 生产
 * 判定本体逐拍评估（sampleInterval=0——Config 文档明载的测试对照面），
 * 判定统计量＝决策序列迹：
 *   ①先节流后诊断：首次 Throttled（65.0% 步）严格先于首次诊断（70.0%
 *     步）——§6.1"接近上限先节流……仍超限→诊断"的时序不变量；
 *   ②三段 ≥ 语义边界（D-05）：64.9%→Normal／65.0%→Throttled／69.9%→
 *     Throttled／70.0%→Exhausted——阈值边界逐格钉扎；
 *   ③节流带内零诊断：69.9%（节流带）与持续 Exhausted（71%）均不出具
 *     诊断——诊断只在进入 Exhausted 的边沿恰一次（"仍"字语义）；
 *   ④恢复语义：回落 Normal 后 pauseDispatch 撤销；复耗尽（70.5%）＝
 *     新一轮边沿——第二次诊断；
 *   ⑤诊断经 §3.3 注入 sink 外报恰一次每回合，比较型三要素（实际占比/
 *     上限 0.70/单位 "1"——NFR-PERF-04 上游值）字段完整；
 *   ⑥阈值双钉：ResourceBudget 消费默认值 maxMemoryRatio==0.70（需求
 *     原文）/throttleRatio==0.65（D-05 实现参数）——判定与生产默认
 *     同源，杜绝抄写漂移。
 */
TEST(ResourceThrottleOrderingBenchmark, ThrottleBeforeDiagnosticSequence_WP23T06_B05)
{
    IRD_TEST_INFO("NFR-PERF-04", {"B-05"}, std::nullopt);

    // ---- 阈值双钉（判定与生产默认同源——抄写漂移的编译外防线）----
    {
        const execution::ResourceBudget defaults{};
        EXPECT_DOUBLE_EQ(defaults.maxMemoryRatio, kMaxMemoryRatio)
            << "ResourceBudget 默认上限与 NFR-PERF-04 原文 70% 漂移";
        EXPECT_DOUBLE_EQ(defaults.throttleRatio, kThrottleRatio)
            << "ResourceBudget 默认节流阈值与 D-05 实现参数 65% 漂移";
    }

    // ---- 脚本采样器初始化（分母＝MemProbe 真实探针；失败显性失败）----
    ScriptedRatioSampler sampler;
    ASSERT_TRUE(sampler.initFromRealSystem())
        << "真实系统内存探针失败（SupervisorMemorySampler 空池半区）——"
           "分母不可得，不猜值";
    ASSERT_GT(sampler.totalPhysicalBytes(), 0u);

    // ---- 控制器装配（生产判定本体；逐拍采样——时序语义判定非时延测量）----
    ResourceController::Config config;
    config.sampleInterval = std::chrono::milliseconds{0};   // 逐拍采样（Config 文档明载的测试对照面）
    ResourceController controller(sampler, config);
    RecordingDiagnosticsSink sink;
    controller.setDiagnosticsSink(&sink);

    // ---- 攀升剖面（manifest 冻结；below 步 floor 定标／at-above 步
    // ceil 定标——整数定标防浮点边界翻转，见 ScriptedRatioSampler 注）----
    struct Step {
        double ratio;    ///< 名义占比（报告迹显示值）
        bool atOrAbove;  ///< true＝达到/越过边界步（ceil 定标）；false＝严格低于（floor 定标）
        ResourceController::Level expected;  ///< 期望层级
        bool expectDiag; ///< 期望诊断边沿
    };
    const std::uint64_t total = sampler.totalPhysicalBytes();
    const std::vector<Step> profile = {
        {0.60,  false, ResourceController::Level::Normal,    false},
        {0.649, false, ResourceController::Level::Normal,    false},
        {kThrottleRatio,      true,  ResourceController::Level::Throttled, false},
        {0.699, false, ResourceController::Level::Throttled, false},
        {kMaxMemoryRatio,     true,  ResourceController::Level::Exhausted, true},
        {0.71,  true,  ResourceController::Level::Exhausted, false},
        {0.69,  false, ResourceController::Level::Throttled, false},
        {0.64,  false, ResourceController::Level::Normal,    false},
        {0.705, true,  ResourceController::Level::Exhausted, true},
    };

    // ---- 逐拍驱动＋决策迹留存（判定统计量＝序列迹——§4.8 原始序列）----
    std::vector<std::string> trace;
    std::vector<double> tickRatios;
    int firstThrottleTick = -1;   ///< 首次 Throttled 的拍序（时序不变量①的左端）
    int firstDiagTick = -1;       ///< 首次诊断的拍序（时序不变量①的右端）
    int decisionDiagCount = 0;    ///< Decision 携带的诊断边沿总数
    for (std::size_t i = 0; i < profile.size(); ++i) {
        const Step& step = profile[i];
        // 字节定标（below=floor／at-above=ceil——重算占比与边界关系确定）。
        const std::uint64_t aggregate = step.atOrAbove
            ? scaleCeil(step.ratio, total)
            : scaleFloor(step.ratio, total);
        sampler.setAggregateBytes(aggregate);

        const ResourceController::Decision d = controller.evaluate();
        tickRatios.push_back(d.reading.ratioOfPhysical());
        const int tick = static_cast<int>(i);
        trace.push_back(traceLine(tick, d.reading.ratioOfPhysical(), d.level,
                                  d.pauseDispatch, d.reclaimIdleWorkers,
                                  d.diagnostic.has_value()));

        // ---- 逐拍断言（期望层级与诊断边沿——序列判定的即时面）----
        EXPECT_EQ(d.level, step.expected)
            << "tick#" << tick << "（名义 " << step.ratio
            << "）层级非期望——三段 ≥ 语义或剖面漂移";
        EXPECT_EQ(d.diagnostic.has_value(), step.expectDiag)
            << "tick#" << tick << "（名义 " << step.ratio
            << "）诊断边沿非期望（「仍」字语义：诊断只在进入 Exhausted 的"
               "边沿恰一次）";
        // 节流两标志与层级一致（Throttled/Exhausted 停派发＋降并行建议；
        // Normal 不干预——§6.1 三段治理的编排消费面）。
        if (d.level == ResourceController::Level::Normal) {
            EXPECT_FALSE(d.pauseDispatch)
                << "tick#" << tick << " Normal 层不应停派发";
            EXPECT_FALSE(d.reclaimIdleWorkers)
                << "tick#" << tick << " Normal 层不应建议降并行";
        } else {
            EXPECT_TRUE(d.pauseDispatch)
                << "tick#" << tick << " 节流/耗尽层应停派发（§6.1 先节流）";
            EXPECT_TRUE(d.reclaimIdleWorkers)
                << "tick#" << tick << " 节流/耗尽层应建议降并行（§6.1）";
        }
        // ---- 时序不变量①：先节流后诊断（本基准的判定本体）----
        if (d.level == ResourceController::Level::Throttled
            && firstThrottleTick < 0) {
            firstThrottleTick = tick;
        }
        if (d.diagnostic.has_value()) {
            ++decisionDiagCount;
            if (firstDiagTick < 0) { firstDiagTick = tick; }
            EXPECT_TRUE(d.pauseDispatch)
                << "tick#" << tick << " 诊断出具拍必须同时处于停派发（先节流"
                   "后诊断——诊断是节流未能压回的升级面）";
        }
    }

    // ---- 时序不变量收口（腿二判定统计量）----
    ASSERT_GE(firstThrottleTick, 0) << "剖面未触达节流带（夹具断）";
    ASSERT_GE(firstDiagTick, 0) << "剖面未触达耗尽边沿（夹具断）";
    EXPECT_LT(firstThrottleTick, firstDiagTick)
        << "先节流后诊断时序不变量违约：首次节流 tick#" << firstThrottleTick
        << " 未先于首次诊断 tick#" << firstDiagTick;
    EXPECT_EQ(decisionDiagCount, 2)
        << "诊断边沿总数非 2（两个耗尽回合各恰一次——边沿语义）";
    EXPECT_EQ(sink.reports().size(), std::size_t{2})
        << "经 §3.3 注入 sink 外报的诊断数非 2（外报恰一次每回合）";
    EXPECT_EQ(sampler.calls(), static_cast<int>(profile.size()))
        << "真实采样次数非逐拍（sampleInterval=0 的逐拍语义）";

    // ---- 诊断记录字段面（比较型三要素——EX-T07 消费契约，UX-03）----
    for (const core::DiagnosticRecord& rec : sink.reports()) {
        EXPECT_EQ(rec.code, "EX-RESOURCE-INSUFFICIENT")
            << "诊断稳定码非 EX-RESOURCE-INSUFFICIENT";
        EXPECT_EQ(rec.context, "execution/resource");
        ASSERT_TRUE(rec.comparison.has_value())
            << "资源不足诊断缺比较型三要素";
        ASSERT_TRUE(rec.comparison->expected.quantity.tryValue().has_value());
        EXPECT_DOUBLE_EQ(*rec.comparison->expected.quantity.tryValue(),
                         kMaxMemoryRatio)
            << "比较型 expected 非 NFR-PERF-04 上限 0.70";
        EXPECT_TRUE(rec.comparison->actual.unit.isValid());
        EXPECT_EQ(rec.comparison->actual.unit.symbol(), "1");
        EXPECT_EQ(rec.comparison->expected.unit.symbol(), "1");
        EXPECT_FALSE(rec.cause.empty());
        EXPECT_FALSE(rec.recommendedAction.empty());
    }
    // 首诊的实际占比＝耗尽边沿拍的重算占比（同表达式——逐位一致）。
    ASSERT_TRUE(sink.reports().at(0).comparison.has_value());
    ASSERT_TRUE(sink.reports().at(0).comparison->actual.quantity.tryValue()
                    .has_value());
    EXPECT_DOUBLE_EQ(*sink.reports().at(0).comparison->actual.quantity.tryValue(),
                     tickRatios.at(static_cast<std::size_t>(firstDiagTick)))
        << "首诊实际占比与边沿拍读数占比不一致";

    // ---- 报告落盘（拖挂——决策迹全量在 notes）----
    testkit::PerfSessionReport session =
        makeSessionHeader("b05-throttle-ordering");
    // 腿二无毫秒统计组——判定以 verdicts 承载（字段沿 schema 的登记性
    // 复用：thresholdMs/measuredP95Ms 携带占比无量纲值，notes 声明口径）。
    {
        testkit::PerfThresholdVerdict v1;
        v1.opId = "throttle-before-diagnostic";
        v1.stateId = "scripted-climb";
        v1.thresholdMs = kThrottleRatio;
        v1.thresholdSource =
            "units/execution.md §15.1 D-05（65% 先节流——实现参数）＋§6.1"
            "「仍超限→诊断」时序语义；判定＝首次节流拍严格先于首次诊断拍";
        v1.measuredP95Ms = static_cast<double>(firstThrottleTick);
        v1.pass = firstThrottleTick < firstDiagTick;
        session.verdicts.push_back(v1);

        testkit::PerfThresholdVerdict v2;
        v2.opId = "diagnostic-entry-edge";
        v2.stateId = "scripted-climb";
        v2.thresholdMs = kMaxMemoryRatio;
        v2.thresholdSource =
            "REQUIREMENTS NFR-PERF-04（70%——NFR-PERF-04 上游值零私改）；"
            "判定＝诊断只在进入 Exhausted（占比 ≥0.70）的边沿出具，两回合"
            "恰 2 次、节流带与持续超限零重复";
        v2.measuredP95Ms = tickRatios.at(static_cast<std::size_t>(firstDiagTick));
        v2.pass = decisionDiagCount == 2
                  && sink.reports().size() == 2;
        session.verdicts.push_back(v2);
    }
    session.allPassed = std::all_of(session.verdicts.begin(),
                                    session.verdicts.end(),
                                    [](const testkit::PerfThresholdVerdict& v) {
                                        return v.pass;
                                    });
    session.notes.push_back(
        "判定统计量＝决策序列迹（先节流后诊断时序语义——非时延测量）："
        "首次节流 tick#" + std::to_string(firstThrottleTick)
        + " ＜ 首次诊断 tick#" + std::to_string(firstDiagTick)
        + "；三段 ≥ 边界逐格钉扎（0.649 Normal／0.650 Throttled／0.699 "
          "Throttled／0.700 Exhausted）");
    session.notes.push_back(
        "物理总量（真实探针 MemProbe querySystemMemory）＝"
        + std::to_string(total) + " 字节；步进字节定标 below=floor／"
        "at-above=ceil（整数定标防浮点边界翻转）");
    for (const std::string& line : trace) {
        session.notes.push_back(line);
    }
    if (const auto dir = testkit::perfReportDirFromEnv()) {
        EXPECT_TRUE(testkit::writePerfReportJson(*dir, session))
            << "报告写失败（拖挂不阻断——检查 IRD_PERF_REPORT_DIR）";
    }
}
