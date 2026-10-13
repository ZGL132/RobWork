/**
 * @file   InteractionResponseBenchmarkTest.cpp
 * @brief  交互响应基准（performance-baseline §2 B-01／需求 NFR-PERF-01）——
 *         黄金规模标准数据（1,000 对象/100 修订）下导航/选择/筛选/编辑/
 *         视图切换五类交互的无头模型层测量，空闲态/后台态两态负载分组
 *         采样，P95 ≤200 ms 达标判定＋无 >2 s 无响应窗口＋>1 s 转后台
 *         用例（WP-23-T02）。
 *
 * 设计依据：
 *   - doc/industrial-robot-design/performance-baseline.md §2 B-01 行（达标
 *     口径照抄需求原文）、§3（标准规模数据锚定黄金项目 1,000 对象/100 修订
 *     ——本夹具为 ui 侧投影数据集，黄金项目文件数据集归 WP-23-T05）、
 *     §4.2（样本量与 P95 线性插值）、§4.3（交互响应测量口径——两态后台
 *     负载、端到端响应时长、无响应窗口、>1 s 转后台、禁 3 次中位数）、
 *     §4.8（报告留痕）、§5.1（本卡挂点动作）；
 *   - 需求 NFR-PERF-01（REQUIREMENTS §19.3 原文——200 ms/2 s/1 s 三值
 *     均照抄，零私改）；
 *   - 任务契约 tasks/foundation/WP-23-T02.json（≙DTB §2.24 行）；
 *   - units/ui.md §3.4（UI 线程纪律 M-1）、§12.3 UI-PERF-1（UI 线程零长
 *     计算——先例用例 UiThreadHeartbeatUnderSlowPeerQuery_UI_PERF_1）。
 *
 * 无头等价口径（验收记录同步说明，报告 notes 逐条登记）：
 *   测量在 ui 模型层（QCoreApplication 级，testkit.md §6.7 模型测试豁免）
 *   驱动**产品代码本体**（StageNavigationModel/SelectionService/
 *   ProjectTreeModel/ParamEditModel——与 Widget 呈现层消费的是同一模型
 *   API），五类交互的"端到端响应时长"以模型入口调用为起点、模型状态更新
 *   与观察者广播完成为终点。与真实界面的等价性论证：
 *   ①200 ms 预算的规模敏感部分（投影合成/选择索引/名称解析过滤/编辑解析
 *     /显示投影重算）全部在模型层承载，Widget 重绘在本口径外（两侧同担
 *     且与数据规模近似无关）；
 *   ②"筛选"在产品中是呈现层规则（IndustrialProjectTree.cpp applyFilter
 *     ——搜索子串＋分组可见性，无模型层 API），无头驱动按其规则语义在
 *     模型侧等价施加（1,000 节点名称端口解析＋大小写不敏感子串匹配＋
 *     分组可见性计数），规则本体与面板实现逐条对齐；
 *   ③"视图切换"取"仅改变会话显示"的显示投影切换承载（ParamEditModel
 *     setDisplayUnitForKind——KIN-12 单位制式批量切换触发全字段显示投影
 *     重算；命令面板 view.displayMode 命令的模型半区归 workflow，不在本
 *     单元测量面内）。
 *   上述三点是本基准的登记口径（等价性边界如实申报，不冒充真实界面测量
 *   ——performance-baseline §4.8"未执行/差距如实登记"纪律）。
 *
 * 后台态负载注入形态（§4.3 委托本任务登记、冻结进数据集 manifest）：
 *   经 execution::WorkerSupervisor 启动**真实独立 worker 进程**
 *   （sdurws_ird_execution_worker，阶段 A 内置脚本替身评估器——execution.md
 *   §3.4 口径），派发"stage-a-script:v1／batches 5,000,000"流式评估批次；
 *   主进程侧独立泵线程消化 worker 事件流（调度线程纪律），批次事件按
 *   1/16 抽样经 Qt 排队 Marshal 投递 UI 线程消费（M-1 纪律的测试面形态
 *   ——呈现桥生产路径同型）。批次构成：单评估批次、流式 ResultBatch 帧、
 *   协作取消收尾（CancelAck→退出码 20——§6.4 约定集）。
 *
 * 测试目标链接面（O-31 测试面直链先例＝ui_contract_test/UI-T14）：
 *   本测试目标直链 sdurws_ird_execution（公共头 WorkerSupervisor/
 *   ChannelProtocol）——产品库 sdurws_ird_ui 本体仍仅 core/diagnostics，
 *   零新增跨单元产品编译边；直链登记册 traceability/wp23-t02-gate-
 *   registrations.md（LinkageContractTest 目标清单同步扩登）。
 */

#include <gtest/gtest.h>

// QT_NO_KEYWORDS：本 TU 同时消费 Qt 与 execution 公共头——execution
// WorkerSupervisor 有名为 emit 的成员函数（execution 零 Qt，D-01），Qt 的
// emit 宏会破坏其声明解析。本 TU 不使用 Qt 关键字形态（信号槽以函数对象
// 接线），故屏蔽之。
#define QT_NO_KEYWORDS 1
#include <QCoreApplication>
#include <QObject>

#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/core/Units.hpp>
#include <sdurws/ird/execution/ChannelProtocol.hpp>
#include <sdurws/ird/execution/Ports.hpp>
#include <sdurws/ird/execution/TaskTypes.hpp>
#include <sdurws/ird/execution/WorkerSupervisor.hpp>
#include <sdurws/ird/testkit/PerfBenchmark.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include <sdurws/ird/ui/FormEditCommon.hpp>
#include <sdurws/ird/ui/IStageNavigationModel.hpp>
#include <sdurws/ird/ui/IndustrialProjectTree.hpp>
#include <sdurws/ird/ui/SelectionService.hpp>
#include <sdurws/ird/ui/UiPorts.hpp>
#include <sdurws/ird/ui/UiProjections.hpp>
#include <sdurws/ird/ui/UiTypes.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {

using namespace sdurws::ird;
using testkit::InteractionGroupReport;
using testkit::InteractionGroupSpec;
using testkit::PerfSessionReport;
using testkit::PerfThresholdVerdict;
using ui::StageId;
using ui::StageViewStatus;

// =====================================================================
// 数据集冻结参数（datasetId＝ird-perf-b01-interaction，version 1——
// manifest 权威文件 testdata/performance/b01-interaction-manifest.json；
// 本处常量必须与 manifest 逐字一致，报告输出参数供漂移核对）
// =====================================================================

/// 黄金规模（PM-16 原文）：对象数（树节点/选择全域/存在性集共用一份）。
constexpr std::size_t kGoldenObjectCount = 1000;
/// 黄金规模（PM-16 原文）：修订数（数据集修订语境条数——投影身份字段源）。
constexpr std::size_t kGoldenRevisionCount = 100;
/// 选择操作单次选中规模（50/1000——工业交互的批量选中形态）。
constexpr std::size_t kSelectionBatchSize = 50;
/// 编辑表单字段数（单对象参数表的黄金形态：16 长度＋8 角度）。
constexpr std::size_t kFormFieldCount = 24;
/// 筛选搜索词（"obj-007"——命中 11/1000 的窄子串，过滤语义真实发生）。
constexpr const char* kFilterNeedle = "obj-007";
/// 对象显示名前缀（筛选匹配目标——零内部名拼接纪律在此不适用：这是
/// 桩名称源的生成参数，非产品拼接路径）。
constexpr const char* kObjectNamePrefix = "obj-";
/// 后台评估批次的流式帧数（5×10⁶——保证批次在测量窗内持续 Running；
/// 协作取消收尾，不依赖自然完结）。
constexpr std::uint64_t kBackgroundBatchFrames = 5000000;
/// 后台批次事件经排队 Marshal 投递 UI 线程的抽样间隔（每 16 帧一投——
/// 呈现桥生产路径的抽样形态；全量投递会淹没事件队列，抽样保序保活）。
constexpr std::uint64_t kMarshalEveryNthBatch = 16;

/// 五分组节点分布（和恰为 kGoldenObjectCount——B1-SPEC §3.1 五分组封闭
/// 清单；分布为生成器参数，冻结于 manifest）。
constexpr std::size_t kGroupNodeCounts[5] = {10, 420, 380, 90, 100};

// =====================================================================
// 黄金规模数据集与桩端口族（ui 模型层的供数面——全部真实产品模型消费）
// =====================================================================

/// 显示名构造：obj-0000 … obj-0999（四位零填充——筛选子串的匹配面）。
std::string goldenObjectName(std::size_t index)
{
    std::string name = kObjectNamePrefix;
    const std::string digits = std::to_string(index);
    while (digits.size() + name.size() < std::string{kObjectNamePrefix}.size() + 4) {
        name.push_back('0');
    }
    return name + digits;
}

/**
 * @brief 黄金规模数据集（一次构建、全部用例共享只读——值面零互访）。
 *
 * 对象身份经 generate() 生成（基准不要求跨运行同 ID——测量对象是操作
 * 时延而非身份复现；确定性复现语义归 B-08/T09，两基准不混）。
 */
struct GoldenDataset {
    std::vector<core::ObjectId> objects;               ///< 1,000 业务对象身份
    std::map<core::ObjectId, std::string> nameById;    ///< 身份→显示名（筛选/名称端口共用）
    std::map<std::string, core::ObjectId> idByName;    ///< 运行时名→身份（反解桩用）

    GoldenDataset()
    {
        objects.reserve(kGoldenObjectCount);
        for (std::size_t i = 0; i < kGoldenObjectCount; ++i) {
            core::ObjectId id = core::ObjectId::generate();
            const std::string name = goldenObjectName(i);
            nameById.emplace(id, name);
            idByName.emplace(name, id);
            objects.push_back(std::move(id));
        }
    }
};

/// 名称解析端口桩（IUiNameResolver——C-11 冻结形状；O(1) 哈希查表，
/// 快速返回纪律与生产 NameMap 同型）。
class StubNameResolver final : public ui::IUiNameResolver
{
public:
    explicit StubNameResolver(const GoldenDataset& data) : m_data(data) {}

    std::optional<std::string> resolveObjectId(core::ObjectId id) const override
    {
        const auto it = m_data.nameById.find(id);
        if (it != m_data.nameById.end()) { return it->second; }
        return std::nullopt;
    }

private:
    const GoldenDataset& m_data;  ///< 数据集只读借用（调用方保证存活期覆盖）
};

/// 运行时名称映射端口桩（IUiRuntimeNameMapPort——SA-05 双射两向；黄金
/// 数据集视全部对象为"已应用"（正向恒命中）——选择 L2 高亮分支真实到达）。
class StubRuntimeNameMap final : public ui::IUiRuntimeNameMapPort
{
public:
    explicit StubRuntimeNameMap(const GoldenDataset& data) : m_data(data) {}

    std::optional<core::ObjectId>
    resolveObjectIdFromRuntimeName(const std::string& runtimeName) const override
    {
        const auto it = m_data.idByName.find(runtimeName);
        if (it != m_data.idByName.end()) { return it->second; }
        return std::nullopt;
    }

    std::optional<std::string>
    resolveRuntimeName(const core::ObjectId& id) const override
    {
        const auto it = m_data.nameById.find(id);
        if (it != m_data.nameById.end()) { return it->second; }
        return std::nullopt;
    }

private:
    const GoldenDataset& m_data;
};

/// 三维高亮出口桩（IUiHighlightOutlet——记录动作次数；零三维知识）。
class StubHighlightOutlet final : public ui::IUiHighlightOutlet
{
public:
    std::atomic<int> highlightCalls{0};  ///< highlight 动作计数（观测面）
    std::atomic<int> clearCalls{0};      ///< clear 动作计数

    void highlightRuntimeObject(const std::string&) override { ++highlightCalls; }
    void clearHighlight() override { ++clearCalls; }
};

/// 阶段门控桩（IUiStageGate——"未产出→桩"口径同 StageNavigationModelTest；
/// 本基准放行全部导航——测导航链路时延，非门控判定语义）。
class AllowAllStageGate final : public ui::IUiStageGate
{
public:
    ui::StageGateView presentStage(StageId) const override { return {}; }

    ui::StageGateDecision evaluate(const ui::StageReadinessSnapshot&) const override
    {
        ui::StageGateDecision d;
        d.allowed = true;  // 全放行——导航时延含门控评估调用的真实路径
        return d;
    }
};

/// 域就绪源桩（IUiDomainReadinessSource——每源一域；黄金规模下就绪项
/// 按阶段返回固定清单——汇聚路径真实执行，项数不随对象数缩放，如实
/// 登记：导航时延的规模敏感面是投影合成而非就绪项数）。
class StubDomainSource final : public ui::IUiDomainReadinessSource
{
public:
    explicit StubDomainSource(std::string key) : m_key(std::move(key)) {}

    /// 域注册键（非端口方法——本桩的自报观测面，供报告登记）。
    const std::string& domainKey() const noexcept { return m_key; }

    std::vector<ui::DomainReadinessItem> domainReadiness(StageId) const override
    {
        ui::DomainReadinessItem item;
        item.domainKey = m_key;
        item.verdict = core::EngineeringStatus::NotApplicable;
        item.inputComplete = true;
        return {item};
    }

private:
    std::string m_key;  ///< 域注册键（"modeling"/"requirements"）
};

// =====================================================================
// 后台评估批次驱动（真实独立 worker 进程——后台态负载注入体）
// =====================================================================

/**
 * @brief 后台负载驱动：经 WorkerSupervisor 启动真实 worker 进程跑流式
 *        评估批次；主进程侧独立泵线程（本驱动的"调度线程"）launch/poll/
 *        cancel；批次事件抽样经排队 Marshal 投递 UI 线程。
 *
 * 线程模型：全部监督器调用（launch/poll/requestCooperativeCancel/status/
 * 析构）只在泵线程执行（§10.3 调度线程纪律）；事件汇回调在泵线程内
 * （poll 消化路径），只触碰自身原子量＋Qt 排队投递（线程安全 API）。
 * 测试线程经条件变量等待 Running 相位、经原子量读观测、经停止标志收尾。
 */
class BackgroundEvaluationLoad final : public execution::IWorkerEventSink
{
public:
    /// 启动后台批次（泵线程内完成 launch＋握手等待）。返回是否进入
    /// Running 相位（false＝启动失败——诊断在 launchDiagnostics）。
    bool start()
    {
        std::unique_lock<std::mutex> lk(m_mutex);
        m_pump = std::thread([this] { pumpProcedure(); });
        m_startedCv.wait(lk, [this] { return m_startOutcomeReady; });
        return m_startOk;
    }

    /// worker 主进程 PID（0＝未知/未启动）。
    std::uint32_t workerPid() const { return m_workerPid.load(); }

    /// 是否已观测到 Running 相位。
    bool runningObserved() const { return m_runningSeen.load(); }

    /// 收到的 ResultBatch 事件数（流式批次真实到达的证据）。
    std::uint64_t batchEvents() const { return m_batchEvents.load(); }

    /// UI 线程已消费的 Marshal 投递数（排队落地证据）。
    std::uint64_t uiMarshalDelivered() const { return m_uiMarshalDelivered.load(); }

    /// 启动失败诊断（start()==false 时可读）。
    std::vector<core::DiagnosticRecord> launchDiagnostics() const
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        return m_launchDiagnostics;
    }

    /// worker 未按协作取消退出的观测面（收尾判定用）。
    bool exitSeenForTest() const { return m_exitSeen.load(); }

    /// 协作取消收尾并等待泵线程退出（幂等——析构亦兜底调用）。
    void stopAndJoin()
    {
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            if (!m_pump.joinable() || m_stopRequested) { return; }
            m_stopRequested = true;
        }
        m_pump.join();
    }

    ~BackgroundEvaluationLoad() override { stopAndJoin(); }

private:
    // ---- IWorkerEventSink（泵线程内被 poll 调用）----
    void onWorkerEvent(const execution::WorkerEvent& event) override
    {
        switch (event.kind) {
        case execution::WorkerEvent::Kind::DispatchAccepted:
            m_dispatchAccepted = true;
            break;
        case execution::WorkerEvent::Kind::ResultBatch:
            // 流式批次计数＋抽样排队投递（M-1 测试面形态——呈现桥生产
            // 路径同型：后台事件经队列回 UI 线程消费）。
            {
                const std::uint64_t n = m_batchEvents.fetch_add(1) + 1;
                if (n % kMarshalEveryNthBatch == 0) {
                    QMetaObject::invokeMethod(
                        QCoreApplication::instance(),
                        [this] { m_uiMarshalDelivered.fetch_add(1); },
                        Qt::QueuedConnection);
                }
            }
            break;
        case execution::WorkerEvent::Kind::WorkerExited:
            m_exitSeen = true;
            m_exitClassification = static_cast<int>(event.exit);
            break;
        default:
            break;  // 进度/检查点等事件——本基准不消费（计数面不展开）
        }
    }

    /// 泵线程主过程（本驱动唯一的监督器调用线程）。
    void pumpProcedure()
    {
        // worker 可执行路径（CMake 注入——与 execution 契约测试同款机制）。
        const std::wstring exePath = [] {
            const std::string narrow = IRD_UI_PERF_WORKER_EXE;
            return std::wstring(narrow.begin(), narrow.end());
        }();

        auto supervisor = std::make_unique<execution::WorkerSupervisor>(
            exePath, execution::WorkerSupervisor::Config{});
        supervisor->setEventSink(this);

        // 派发绑定：脚本承载于快照字节（阶段 A 替身评估器约定——执行
        // 单元 WorkerProcessContractTest 的 makeAssignment 同构最小面）。
        execution::WorkerAssignment a;
        a.identity.project = core::ProjectId::generate();
        a.identity.branch = core::BranchId::generate();
        a.identity.revision = core::RevisionId::generate();
        a.identity.run = core::RunId::generate();
        a.identity.attempt = core::AttemptId{1};
        a.evaluatorKey = "stage-a-double";
        const std::string script = "stage-a-script:v1\nbatches "
                                   + std::to_string(kBackgroundBatchFrames) + "\n";
        a.dispatch.snapshotBytes.assign(script.begin(), script.end());
        a.dispatch.modelBytes = {0xC0, 0xDE};
        a.dispatch.snapshotIdentity =
            execution::computeContentIdentity(a.dispatch.snapshotBytes);
        a.dispatch.modelIdentity =
            execution::computeContentIdentity(a.dispatch.modelBytes);
        a.manifestDigest = execution::manifestDigestHex("stage-a-double|1|7");

        const execution::WorkerLaunchResult launched = supervisor->launch(a);
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_launchDiagnostics = launched.diagnostics;
        }
        if (!launched.ok) {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_startOk = false;
            m_startOutcomeReady = true;
            m_startedCv.notify_all();
            return;  // 启动失败——测试线程经 start() 返回值感知（诊断已留存）
        }
        m_workerId = launched.worker;

        // 握手→派发→Running 等待循环（有界——30 s 超时按启动失败上报）。
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (!m_stopRequested.load()) {
            supervisor->poll();
            const execution::WorkerStatus st = supervisor->status(m_workerId);
            m_workerPid.store(st.pid);
            if (st.phase == execution::WorkerStatus::Phase::Running) {
                m_runningSeen = true;
                break;
            }
            if (m_exitSeen.load()) { break; }  // 提前退出（握手拒绝等）——按失败上报
            if (std::chrono::steady_clock::now() > deadline) { break; }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        m_startOk = m_runningSeen.load() && !m_exitSeen.load();
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_startOutcomeReady = true;
            m_startedCv.notify_all();
        }

        // 测量窗维持循环：持续泵事件直到停止标志（批次保持 Running——
        // 后台负载在窗内真实存在）。
        while (!m_stopRequested.load(std::memory_order_acquire) && !m_exitSeen.load()) {
            supervisor->poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }

        // 协作取消收尾（§7.1——CancelAck→退出码 20；未及退出则析构兜杀）。
        if (!m_exitSeen.load()) {
            supervisor->requestCooperativeCancel(m_workerId);
            const auto cancelDeadline =
                std::chrono::steady_clock::now() + std::chrono::seconds(30);
            while (!m_exitSeen.load()
                   && std::chrono::steady_clock::now() < cancelDeadline) {
                supervisor->poll();
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        }

        // 监督器在本线程析构（调度线程纪律——读线程收尾＋作业兜杀）。
        supervisor.reset();
    }

    // ---- 共享观测面（原子量：泵线程写、测试线程读）----
    std::atomic<std::uint32_t> m_workerPid{0};       ///< worker 主进程 PID
    std::atomic<bool> m_runningSeen{false};          ///< Running 相位已观测
    std::atomic<bool> m_dispatchAccepted{false};     ///< 派发已被 worker 接受
    std::atomic<std::uint64_t> m_batchEvents{0};     ///< ResultBatch 事件计数
    std::atomic<std::uint64_t> m_uiMarshalDelivered{0}; ///< UI 线程 Marshal 消费计数
    std::atomic<bool> m_exitSeen{false};             ///< WorkerExited 已观测
    std::atomic<int> m_exitClassification{0};        ///< 退出分类（ExitClassification）
    std::atomic<bool> m_stopRequested{false};        ///< 收尾标志

    execution::WorkerId m_workerId;                  ///< worker 实例（泵线程专用）
    mutable std::mutex m_mutex;                      ///< 启动握手互斥
    std::condition_variable m_startedCv;             ///< 启动结果通知
    bool m_startOutcomeReady = false;                ///< 启动结果已定
    bool m_startOk = false;                          ///< 启动成功（进入 Running）
    std::vector<core::DiagnosticRecord> m_launchDiagnostics; ///< 启动失败诊断
    std::thread m_pump;                              ///< 泵线程（监督器调用线程）
};

// =====================================================================
// 五类交互操作驱动（产品模型本体——测量口径见文件头等价性论证）
// =====================================================================

/**
 * @brief 五操作驱动器（一次装配、逐操作闭包——全部操作在 UI 线程执行，
 *        与 M-1 纪律一致）。
 *
 * 生命周期：构造即装配黄金规模模型面（树模型 rebuild 1,000 节点、选择
 * 服务、导航模型、24 字段编辑会话）；操作闭包由测试线程（QCoreApplication
 * 线程＝UI 线程测试形态）调用。
 */
class InteractionDrivers
{
public:
    explicit InteractionDrivers(const GoldenDataset& data) : m_data(data)
    {
        buildTreeModel();
        buildSelectionService();
        buildNavigationModel();
        buildEditModel();
    }

    // ---- 操作 1：导航（requestNavigate 七阶段轮转——模型全路径）----
    void navigateNext()
    {
        const auto stages = ui::stageIdSequence();
        const StageId target = stages[m_navCursor % stages.size()];
        ++m_navCursor;
        const ui::NavigateResult result = m_navigation->requestNavigate(target);
        if (!result.navigated()) {
            // 放行桩下导航必须成功——失败即夹具断（如实抛出，记无效样本）。
            throw std::runtime_error("benchmark: requestNavigate 未放行");
        }
    }

    // ---- 操作 2：选择（selectBusiness 50/1000 旋转子集＋观察者消费）----
    void selectNext()
    {
        std::vector<core::ObjectId> batch;
        batch.reserve(kSelectionBatchSize);
        for (std::size_t i = 0; i < kSelectionBatchSize; ++i) {
            batch.push_back(
                m_data.objects[(m_selectCursor + i) % m_data.objects.size()]);
        }
        m_selectCursor = (m_selectCursor + kSelectionBatchSize) % m_data.objects.size();
        // 唯一业务写入口：去重保序＋L2 正向存在性（桩 NameMap 全命中→
        // 高亮出线）＋SelectionChange 广播（观察者计数即消费证据）。
        m_selection->selectBusiness(std::move(batch),
                                    ui::SelectionSource::ProjectTree);
    }

    // ---- 操作 3：筛选（applyFilter 规则的模型侧等价施加——见文件头）----
    void filterPass()
    {
        // 大小写不敏感子串（ASCII——与面板 Qt::CaseInsensitive 语义对齐）。
        const std::string needle = kFilterNeedle;
        const auto lowerOf = [](std::string s) {
            for (char& c : s) {
                if (c >= 'A' && c <= 'Z') { c = static_cast<char>(c - 'A' + 'a'); }
            }
            return s;
        };
        const std::string needleLower = lowerOf(needle);
        std::size_t visibleTotal = 0;
        // 逐组取节点序（模型查询）→ 逐节点名称端口解析 → 子串匹配 →
        // 组内可见计数——规则三段与 IndustrialProjectTree::applyFilter
        // 逐条对齐（呈现层的 setHidden 半区在本口径外，见文件头论证②）。
        for (const int g : {0, 1, 2, 3, 4}) {
            const auto group = static_cast<ui::ProjectTreeGroup>(g);
            const std::vector<core::ObjectId> nodes = m_tree->nodesInGroup(group);
            std::size_t visible = 0;
            for (const core::ObjectId& id : nodes) {
                const std::optional<std::string> name = m_nameResolver->resolveObjectId(id);
                if (name.has_value()
                    && lowerOf(*name).find(needleLower) != std::string::npos) {
                    ++visible;
                }
            }
            visibleTotal += visible;
        }
        if (visibleTotal == 0) {
            // 筛选零命中即夹具断（needle 设计上必命中——防夹具漂移静默）。
            throw std::runtime_error("benchmark: 筛选零命中（夹具漂移）");
        }
    }

    // ---- 操作 4：编辑（参数表编辑会话全路径：暂存→确认→移交出口）----
    void editNext()
    {
        // 8 字段旋转编辑（显示单位制式下的合法数值文本），表单级确认应用
        // 移交编辑出口（SI 真值）。数值经调用序号单调推进——与基线恒不
        // 等值（同值再输入＝无效编辑不进修改集——pendingChanges 口径），
        // 每个样本都是一次真实的"变更→确认→移交"会话。
        ++m_editSequence;
        for (std::size_t k = 0; k < 8; ++k) {
            const std::size_t field = (m_editCursor + k) % kFormFieldCount;
            const double displayValue =
                12.0 + static_cast<double>(k)
                + static_cast<double>(m_editSequence) * 0.5;  // 显示单位数值
            const std::string text = std::to_string(displayValue);
            if (!m_editModel->setEditText(m_fieldKeys[field], text)) {
                throw std::runtime_error("benchmark: 编辑解析被拒（夹具漂移）");
            }
        }
        m_editCursor = (m_editCursor + 8) % kFormFieldCount;
        const ui::ConfirmApplyResult applied =
            m_editModel->confirmApply(*m_editOutlet);
        if (!applied.ok) {
            throw std::runtime_error("benchmark: 确认应用未移交（" + applied.reason
                                     + "）");
        }
    }

    // ---- 操作 5：视图切换（显示单位制式批量切换——KIN-12 仅投影）----
    void viewSwitchNext()
    {
        // 长度制式 mm↔cm 交替（会话显示设置——零脏零移交），随后全字段
        // 显示投影重读（displayValueText——KIN-12 唯一格式化出口的重算面）。
        const core::UnitToken nextUnit =
            m_viewSwitchToggle ? (*core::UnitToken::find("cm")) : (*core::UnitToken::find("mm"));
        m_viewSwitchToggle = !m_viewSwitchToggle;
        m_editModel->setDisplayUnitForKind(core::QuantityKind::Length, nextUnit);
        double checksum = 0.0;
        for (const std::string& key : m_fieldKeys) {
            checksum += static_cast<double>(m_editModel->displayValueText(key).size());
        }
        if (checksum <= 0.0) {
            throw std::runtime_error("benchmark: 显示投影空读（夹具漂移）");
        }
    }

    // 组间排队事件消化（后台 Marshal 落地消费——UI 线程纪律的会话半区）。
    void drainQueuedEvents()
    {
        if (QCoreApplication::instance() != nullptr) {
            QCoreApplication::processEvents();
        }
    }

    /// 选中消费观察计数（选择广播真实到达的证据——报告登记用）。
    int selectionObserverHits() const { return m_selectionObserver.hits; }

    /// 编辑出口移交计数（确认应用真实到达的证据）。
    int editOutletHits() const { return m_editOutlet->appliedCount.load(); }

private:
    // ---- 装配（构造序）----

    void buildTreeModel()
    {
        // 五分组 × 冻结分布的节点供给（B1-SPEC §5.1 Provider 协议——
        // 节点身份有效、全局唯一、子引用闭合、深度 0）。
        for (int g = 0; g < 5; ++g) {
            class GroupProvider final : public ui::IUiTreeNodesProvider
            {
            public:
                GroupProvider(std::string key, int group,
                              std::vector<ui::ProjectTreeNode> nodes)
                    : m_key(std::move(key)), m_group(group),
                      m_nodes(std::move(nodes))
                {
                }

                std::string domainKey() const override { return m_key; }

                std::vector<ui::ProjectTreeNode> treeNodes() const override
                {
                    return m_nodes;
                }

            private:
                std::string m_key;
                int m_group;
                std::vector<ui::ProjectTreeNode> m_nodes;
            };

            std::vector<ui::ProjectTreeNode> nodes;
            nodes.reserve(kGroupNodeCounts[g]);
            for (std::size_t k = 0; k < kGroupNodeCounts[g]; ++k) {
                ui::ProjectTreeNode node;
                node.objectId = m_data.objects[m_treeCursor];
                node.group = static_cast<ui::ProjectTreeGroup>(g);
                node.depth = 0;  // 扁平黄金树（分组承载规模——manifest 冻结）
                ++m_treeCursor;
                nodes.push_back(std::move(node));
            }
            m_tree->addProvider(std::make_shared<GroupProvider>(
                "perf-domain-" + std::to_string(g), g, std::move(nodes)));
        }
        const ui::TreeRebuildReport report = m_tree->rebuild();
        if (!report.ok || report.nodeCount != kGoldenObjectCount) {
            throw std::runtime_error("benchmark: 树重建未达黄金规模（"
                                     + std::to_string(report.nodeCount) + "）");
        }
    }

    void buildSelectionService()
    {
        ui::SelectionService::Deps deps;
        deps.nameMap = std::make_shared<StubRuntimeNameMap>(m_data);
        deps.treeLocator = [this](const core::ObjectId& id) {
            // L3 落点缝：真实树模型定位（locate 命中即定位成功）。
            return m_tree->locate(id).has_value();
        };
        deps.highlightOutlet = m_highlightOutlet;
        deps.devLog = nullptr;  // 显式声明：无日志场景（端口可空纪律）
        m_selection = std::make_unique<ui::SelectionService>(std::move(deps));
        m_selectionSub = m_selection->subscribe(m_selectionObserver);
    }

    void buildNavigationModel()
    {
        ui::StageNavigationModelDeps deps;
        deps.gate = &m_gate;
        deps.epochSource = [] { return std::uint64_t{1}; };
        deps.writableSource = [] { return true; };
        deps.sessionNavigableSource = [] { return true; };
        deps.sevenStateFacts =
            [](StageId) { return std::optional<ui::StatusFacts>{ui::StatusFacts{}}; };
        deps.domainSources.push_back(&m_domainA);
        deps.domainSources.push_back(&m_domainB);
        deps.initialStage = StageId::Modeling;
        m_navigation = ui::createStageNavigationModel(std::move(deps));
    }

    void buildEditModel()
    {
        // 24 字段黄金参数表：16 长度（SI m／显示 mm）＋8 角度（SI rad／
        // 显示 deg）——makeQuantityFieldSpec 装配期保证单位一致性。
        std::vector<ui::QuantityFieldSpec> specs;
        specs.reserve(kFormFieldCount);
        const auto mm = core::UnitToken::find("mm");
        const auto deg = core::UnitToken::find("deg");
        const auto m = core::UnitToken::find("m");
        const auto rad = core::UnitToken::find("rad");
        if (!mm || !deg || !m || !rad) {
            throw std::runtime_error("benchmark: 单位注册表缺 token（夹具漂移）");
        }
        for (std::size_t i = 0; i < kFormFieldCount; ++i) {
            const std::string key = "field-"
                                    + std::to_string(i);
            m_fieldKeys.push_back(key);
            if (i < 16) {
                specs.push_back(ui::makeQuantityFieldSpec(
                    key, "长度参数" + std::to_string(i), core::QuantityKind::Length,
                    *m, *mm));
            } else {
                specs.push_back(ui::makeQuantityFieldSpec(
                    key, "角度参数" + std::to_string(i), core::QuantityKind::Angle,
                    *rad, *deg));
            }
        }
        m_editModel = std::make_unique<ui::ParamEditModel>(std::move(specs));
        for (std::size_t i = 0; i < kFormFieldCount; ++i) {
            m_editModel->setBaseline(m_fieldKeys[i], baselineValue(i));
        }
    }

    /// 字段基线值（SI 真值——setBaseline 契约：长度 m／角度 rad；显示
    /// 制式的换算由 KIN-12 显示投影承担，本夹具不预换算）。
    double baselineValue(std::size_t field) const
    {
        return field < 16 ? 0.5 + 0.01 * static_cast<double>(field)      // 单位 m
                          : 0.2 + 0.05 * static_cast<double>(field - 16); // 单位 rad
    }

    /// 选择观察者（广播消费计数——重入禁令下只计数不回写）。
    class CountingSelectionObserver final : public ui::IUiSelectionObserver
    {
    public:
        void onSelectionChanged(const ui::SelectionChange&) override { ++hits; }
        int hits = 0;
    };

    /// 编辑出口桩（ParamEditSet 移交接收——SI 真值落点）。
    class CountingEditOutlet final : public ui::IFormEditOutlet
    {
    public:
        void applyEdits(const ui::ParamEditSet&) override { ++appliedCount; }
        std::atomic<int> appliedCount{0};
    };

    const GoldenDataset& m_data;                        ///< 黄金数据集借用
    std::shared_ptr<StubNameResolver> m_nameResolver =  ///< 名称解析桩
        std::make_shared<StubNameResolver>(m_data);
    std::unique_ptr<ui::ProjectTreeModel> m_tree =      ///< 树模型（1,000 节点）
        std::make_unique<ui::ProjectTreeModel>();
    std::shared_ptr<StubHighlightOutlet> m_highlightOutlet = ///< 高亮出口桩
        std::make_shared<StubHighlightOutlet>();
    std::unique_ptr<ui::SelectionService> m_selection;  ///< 选择服务
    CountingSelectionObserver m_selectionObserver;      ///< 选择观察者
    std::unique_ptr<core::IEventSubscription> m_selectionSub; ///< 订阅句柄
    AllowAllStageGate m_gate;                           ///< 全放行门控桩
    StubDomainSource m_domainA{"modeling"};             ///< 域就绪源 A
    StubDomainSource m_domainB{"requirements"};         ///< 域就绪源 B
    std::unique_ptr<ui::IStageNavigationModel> m_navigation; ///< 导航模型
    std::unique_ptr<ui::ParamEditModel> m_editModel;    ///< 编辑会话模型
    std::shared_ptr<CountingEditOutlet> m_editOutlet =  ///< 编辑出口桩
        std::make_shared<CountingEditOutlet>();
    std::vector<std::string> m_fieldKeys;               ///< 字段键（注册序）
    std::size_t m_navCursor = 1;                        ///< 导航轮转游标（首步→Requirements）
    std::size_t m_selectCursor = 0;                     ///< 选中批旋转游标
    std::size_t m_editCursor = 0;                       ///< 编辑字段旋转游标
    std::size_t m_editSequence = 0;                     ///< 编辑调用序号（数值单调推进——防同值无效编辑）
    bool m_viewSwitchToggle = false;                    ///< 制式切换交替位
    std::size_t m_treeCursor = 0;                       ///< 树节点分配游标（装配期）
};

// =====================================================================
// 测量会话组装（PerfSessionReport 的公共半区）
// =====================================================================

/// 环境事实读取（§4.1 登记清单的环境变量可得面——缺失如实留空）。
std::string envOr(const char* name, const char* fallback = "")
{
    const char* v = std::getenv(name);
    return (v != nullptr && *v != '\0') ? std::string(v) : std::string(fallback);
}

/// 组装会话报告头（标识/版本/数据集/环境——测量组由调用方追加）。
PerfSessionReport makeSessionHeader(const std::string& caseId)
{
    PerfSessionReport r;
    r.caseId = caseId;
    r.baselineDoc = "performance-baseline #2 B-01";
    // 软件版本锁定（§4.1）：commit 由执行方经 IRD_PERF_GIT_COMMIT 注入
    // （缺省如实留空——commit SHA 在首测报告文档登记）。
    r.gitCommit = envOr("IRD_PERF_GIT_COMMIT");
    r.buildConfig = "Release";
    r.datasetId = "ird-perf-b01-interaction";
    r.datasetVersion = "1";
    r.machineName = envOr("COMPUTERNAME");
    r.cpuInfo = envOr("PROCESSOR_IDENTIFIER")
                + " x" + envOr("NUMBER_OF_PROCESSORS", "?");
    r.osInfo = envOr("OS", "Windows") + " " + envOr("PROCESSOR_ARCHITECTURE");
    // 内存/存储/电源：环境变量不可得——如实指向首测报告的硬件清单
    // （traceability/benchmarks/wp23-t02-interaction-baseline.md §2）。
    r.memoryInfo = "见首测报告硬件清单（manifest 同源）";
    r.storageInfo = "见首测报告硬件清单";
    r.powerPolicyNote = "见首测报告硬件清单";
    r.backgroundNote = "空闲态＝无后台评估；后台态＝独立 worker 进程流式"
                       "评估批次（manifest 冻结形态）＋主进程泵线程";
    // 等价性口径与操作实现登记（报告自含——验收对照面）。
    r.notes.push_back(
        "无头模型层等价口径：五操作驱动产品模型本体（StageNavigationModel/"
        "SelectionService/ProjectTreeModel/ParamEditModel），Widget 重绘在"
        "口径外——论证见用例文件头与首测报告 §4");
    r.notes.push_back(
        "筛选＝applyFilter 规则模型侧等价施加；视图切换＝显示单位制式批量"
        "切换（KIN-12 仅投影）——边界如实申报");
    r.notes.push_back(
        "数据集 manifest＝testdata/performance/b01-interaction-manifest.json"
        "（datasetId ird-perf-b01-interaction@1——参数冻结，升级换版本）");
    return r;
}

/// 五组 P95 阈值判定组装（阈值 200 ms 照抄 REQUIREMENTS NFR-PERF-01 原文
/// ——出处随判定逐条登记，夹具零私设）。
std::vector<PerfThresholdVerdict>
makeVerdicts(const std::vector<InteractionGroupReport>& groups)
{
    std::vector<PerfThresholdVerdict> verdicts;
    verdicts.reserve(groups.size());
    for (const InteractionGroupReport& g : groups) {
        PerfThresholdVerdict v;
        v.opId = g.opId;
        v.stateId = g.stateId;
        v.thresholdMs = 200.0;  // NFR-PERF-01 原文：P95 响应 ≤200 ms
        v.thresholdSource = "REQUIREMENTS NFR-PERF-01";
        v.measuredP95Ms = g.p95Ms;
        v.pass = g.p95Ms <= v.thresholdMs;
        verdicts.push_back(v);
    }
    return verdicts;
}

// =====================================================================
// 基准用例
// =====================================================================

/**
 * 基准用例（B-01 空闲态）：黄金规模标准数据＋无后台负载下五类交互各采
 * 集 1 次预热＋≥15 有效样本，判定三面——
 *   ①P95 ≤200 ms（NFR-PERF-01 原文——夹具线性插值，禁中位数替代）；
 *   ②单次交互无 >2 s 无响应窗口（每样本 <2 s——协议 §4.3 无响应窗口）；
 *   ③零无效样本（操作闭包异常即夹具断——无效留痕不判过）。
 */
TEST(InteractionResponseBenchmark, FiveOpsIdleStateGoldenScaleP95_WP23T02_B01)
{
    IRD_TEST_INFO("NFR-PERF-01", {"B-01"}, std::nullopt);

    const GoldenDataset dataset;  // 黄金规模数据集（1,000 对象/100 修订语境）
    InteractionDrivers drivers(dataset);

    // 100 修订语境登记（§4.3 标准规模锚定的修订面——投影语境条数；
    // 五操作时延的规模敏感面为对象数，修订语境如实登记不虚构权重）。
    ASSERT_GE(kGoldenRevisionCount, std::size_t{100});

    const char* opIds[5] = {"nav", "select", "filter", "edit", "viewswitch"};
    std::vector<InteractionGroupReport> groups;
    groups.reserve(10);

    for (const char* opId : opIds) {
        InteractionGroupSpec spec;
        spec.opId = opId;
        spec.stateId = "idle";
        spec.warmupRuns = 1;        // 协议 §4.2：剔除各组首次预热
        spec.minValidSamples = 15;  // 协议 §4.2：每组有效样本 ≥15

        InteractionGroupReport report = testkit::runInteractionGroup(
            spec, [&drivers, opId] {
                if (opId[0] == 'n') { drivers.navigateNext(); }
                else if (opId[0] == 's') { drivers.selectNext(); }
                else if (opId[0] == 'f') { drivers.filterPass(); }
                else if (opId[0] == 'e') { drivers.editNext(); }
                else { drivers.viewSwitchNext(); }
            });
        groups.push_back(std::move(report));
        drivers.drainQueuedEvents();
    }

    // 判定三面（组×组核对——判据数值全部来自需求原文）。
    PerfSessionReport session = makeSessionHeader("b01-interaction-idle");
    for (const InteractionGroupReport& g : groups) {
        ASSERT_EQ(g.validCount, std::size_t{15})
            << g.opId << " 有效样本不足（协议 §4.2 下限）";
        ASSERT_EQ(g.invalidCount, std::size_t{0})
            << g.opId << " 存在无效样本（操作异常——详见报告留痕）";
        EXPECT_LT(g.maxValidMs, 2000.0)
            << g.opId << " 单次交互出现 >2 s 无响应窗口（NFR-PERF-01）";
        EXPECT_LE(g.p95Ms, 200.0)
            << g.opId << " P95 超预算（NFR-PERF-01：≤200 ms）";
    }
    session.groups = groups;
    session.verdicts = makeVerdicts(groups);
    session.allPassed = std::all_of(session.verdicts.begin(), session.verdicts.end(),
                                    [](const PerfThresholdVerdict& v) { return v.pass; });
    session.notes.push_back("选择广播消费 " + std::to_string(drivers.selectionObserverHits())
                            + " 次；编辑出口移交 "
                            + std::to_string(drivers.editOutletHits()) + " 次（操作真实到达证据）");

    // 报告落盘（拖挂——IRD_PERF_REPORT_DIR 未设则跳过；写失败不阻断判定）。
    if (const auto dir = testkit::perfReportDirFromEnv()) {
        EXPECT_TRUE(testkit::writePerfReportJson(*dir, session))
            << "报告写失败（拖挂不阻断——检查 IRD_PERF_REPORT_DIR）";
    }
}

/**
 * 基准用例（B-01 后台态）：真实独立 worker 进程流式评估批次运行中，五类
 * 交互同口径采样与判定（两态各自满足 P95 ≤200 ms——协议 §4.3）；同时
 * 验证后台负载的进程面证据（worker PID ≠ 主进程 PID、批次事件真实到达、
 * Marshal 经排队落 UI 线程消费）。
 */
TEST(InteractionResponseBenchmark, FiveOpsBackgroundStateGoldenScaleP95_WP23T02_B01)
{
    IRD_TEST_INFO("NFR-PERF-01", {"B-01"}, std::nullopt);

    const GoldenDataset dataset;
    InteractionDrivers drivers(dataset);

    // ---- 后台负载注入（真实独立 worker 进程——manifest 冻结形态）----
    BackgroundEvaluationLoad load;
    ASSERT_TRUE(load.start())
        << "后台 worker 启动失败（诊断："
        << (load.launchDiagnostics().empty()
                ? std::string("无诊断记录")
                : load.launchDiagnostics().front().cause)
        << "）";
    ASSERT_TRUE(load.runningObserved());
    EXPECT_NE(load.workerPid(), std::uint32_t{0});
    EXPECT_NE(load.workerPid(), static_cast<std::uint32_t>(QCoreApplication::applicationPid()))
        << "后台批次必须运行于独立进程（NFR-PERF-01 后台负载语义）";

    // ---- 等待批次流真实到达（Running＝派发受理；ResultBatch 流到达＝
    // 评估批次正在执行——后台态语义的证据面；握手→派发→评估有启动时延，
    // 有界等待 10 s 防夹具竞态误判）----
    {
        const auto streamDeadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (load.batchEvents() == 0
               && std::chrono::steady_clock::now() < streamDeadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        ASSERT_GT(load.batchEvents(), std::uint64_t{0})
            << "后台批次事件流未到达（批次未真正执行——后台态不成立）";
    }

    // ---- 五操作 × 后台态采样（同空闲态口径）----
    const char* opIds[5] = {"nav", "select", "filter", "edit", "viewswitch"};
    std::vector<InteractionGroupReport> groups;
    groups.reserve(10);
    for (const char* opId : opIds) {
        InteractionGroupSpec spec;
        spec.opId = opId;
        spec.stateId = "background";
        spec.warmupRuns = 1;
        spec.minValidSamples = 15;
        InteractionGroupReport report = testkit::runInteractionGroup(
            spec, [&drivers, opId] {
                if (opId[0] == 'n') { drivers.navigateNext(); }
                else if (opId[0] == 's') { drivers.selectNext(); }
                else if (opId[0] == 'f') { drivers.filterPass(); }
                else if (opId[0] == 'e') { drivers.editNext(); }
                else { drivers.viewSwitchNext(); }
            });
        groups.push_back(std::move(report));
        drivers.drainQueuedEvents();  // 排队 Marshal 落地消费（组间消化）
    }

    // ---- 判定三面（同空闲态）＋后台面证据 ----
    PerfSessionReport session = makeSessionHeader("b01-interaction-background");
    for (const InteractionGroupReport& g : groups) {
        ASSERT_EQ(g.validCount, std::size_t{15});
        ASSERT_EQ(g.invalidCount, std::size_t{0});
        EXPECT_LT(g.maxValidMs, 2000.0)
            << g.opId << " 单次交互出现 >2 s 无响应窗口（后台态）";
        EXPECT_LE(g.p95Ms, 200.0)
            << g.opId << " P95 超预算（后台态——协议 §4.3 两态各自判定）";
    }
    EXPECT_GT(load.batchEvents(), std::uint64_t{0})
        << "后台批次事件未到达（负载注入失效——测量不构成后台态）";
    EXPECT_GT(load.uiMarshalDelivered(), std::uint64_t{0})
        << "排队 Marshal 未在 UI 线程消费（M-1 落地面缺失）";

    session.groups = groups;
    session.verdicts = makeVerdicts(groups);
    session.allPassed = std::all_of(session.verdicts.begin(), session.verdicts.end(),
                                    [](const PerfThresholdVerdict& v) { return v.pass; });
    session.notes.push_back(
        "后台负载＝独立 worker 进程（PID " + std::to_string(load.workerPid())
        + "）流式评估批次，ResultBatch 事件 "
        + std::to_string(load.batchEvents()) + " 帧，UI 线程 Marshal 消费 "
        + std::to_string(load.uiMarshalDelivered()) + " 次");

    if (const auto dir = testkit::perfReportDirFromEnv()) {
        EXPECT_TRUE(testkit::writePerfReportJson(*dir, session));
    }

    // ---- 协作取消收尾（CancelAck→退出码 20——§6.4 约定集；不留孤儿进程）----
    load.stopAndJoin();
    EXPECT_TRUE(load.exitSeenForTest()) << "worker 未按协作取消退出";
}

/**
 * 基准用例（B-01 转后台）：>1 s 的工作转后台执行且期间 UI 线程保持可交互
 * （协议 §4.3 无响应窗口/转后台两条的用例面）——后台批次真实运行超过
 * 1 s（实测墙钟），期间交互采样无 >2 s 无响应窗口、批次结果事件经排队
 * Marshal 落地 UI 线程。
 */
TEST(InteractionResponseBenchmark, LongWorkRunsOffUiThreadAndMarshalsBack_WP23T02_B01)
{
    IRD_TEST_INFO("NFR-PERF-01", {"B-01"}, std::nullopt);

    const GoldenDataset dataset;
    InteractionDrivers drivers(dataset);

    BackgroundEvaluationLoad load;
    ASSERT_TRUE(load.start());

    // ---- 等待批次流到达（同 BackgroundState 用例——转后台语义的证据面
    // 先成立，再开观察窗；握手→派发→评估的启动时延不进 >1 s 窗）----
    {
        const auto streamDeadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (load.batchEvents() == 0
               && std::chrono::steady_clock::now() < streamDeadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        ASSERT_GT(load.batchEvents(), std::uint64_t{0})
            << "后台批次事件流未到达（批次未真正执行）";
    }

    // ---- >1 s 观察窗：后台批次运行期间 UI 线程持续做交互＋消化排队事件 ----
    const auto windowStart = std::chrono::steady_clock::now();
    const auto windowEnd = windowStart + std::chrono::milliseconds(1200);
    double maxInteractionMs = 0.0;   // 产品面无响应窗（交互操作时长）
    double maxDrainMs = 0.0;         // 事件消化窗（排队 Marshal 消化耗时）
    int interactions = 0;
    while (std::chrono::steady_clock::now() < windowEnd) {
        // 一次真实交互（产品模型全路径）——时长即该次交互的 UI 线程
        // 不可交互窗口（协议 §4.3 无响应窗口语义）。
        const auto t0 = std::chrono::steady_clock::now();
        drivers.selectNext();
        const auto t1 = std::chrono::steady_clock::now();
        const double ms =
            std::chrono::duration<double, std::milli>(t1 - t0).count();
        if (ms > maxInteractionMs) { maxInteractionMs = ms; }
        ++interactions;

        // 排队事件消化（后台 Marshal 落地——转后台的结果回到 UI 消费）。
        const auto d0 = std::chrono::steady_clock::now();
        drivers.drainQueuedEvents();
        const auto d1 = std::chrono::steady_clock::now();
        const double drainMs =
            std::chrono::duration<double, std::milli>(d1 - d0).count();
        if (drainMs > maxDrainMs) { maxDrainMs = drainMs; }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const double windowMs = std::chrono::duration<double, std::milli>(
                                std::chrono::steady_clock::now() - windowStart)
                                .count();

    // ---- 判定：>1 s 工作确实在后台运行（未完结→仍在执行＝时长超窗）----
    // 批次自 start() 返回（Running 相位）至今仍未自然完结——>1 s 的工作
    // 运行在独立 worker 进程（下方 PID 断言），UI 线程零承载（交互窗证据）。
    EXPECT_GE(windowMs, 1000.0) << "观察窗未达 1 s（用例前提失真）";
    EXPECT_TRUE(load.runningObserved()) << "批次未处于运行态";
    EXPECT_GT(load.batchEvents(), std::uint64_t{0}) << "批次事件未流式到达";
    EXPECT_GT(load.uiMarshalDelivered(), std::uint64_t{0})
        << "批次事件未经排队 Marshal 落地 UI 线程（转后台闭环缺失）";
    EXPECT_LT(maxInteractionMs, 2000.0)
        << "交互出现 >2 s 无响应窗口（长工作侵入了 UI 线程）";
    EXPECT_LT(maxDrainMs, 2000.0) << "事件消化窗超限（UI 线程被后台结果淹没）";

    // 判定结论聚合（报告 allPassed 与断言同源——失败如实落报告）。
    const bool casePassed = windowMs >= 1000.0 && load.runningObserved()
                            && load.batchEvents() > 0
                            && load.uiMarshalDelivered() > 0
                            && maxInteractionMs < 2000.0 && maxDrainMs < 2000.0;

    PerfSessionReport session = makeSessionHeader("b01-longwork-background");
    session.allPassed = casePassed;
    session.notes.push_back(
        "转后台用例：批次独立进程（PID " + std::to_string(load.workerPid())
        + "）运行 ≥" + std::to_string(windowMs) + " ms 期间，UI 线程交互 "
        + std::to_string(interactions) + " 次（最大不可交互窗 "
        + std::to_string(maxInteractionMs) + " ms）、Marshal 消费 "
        + std::to_string(load.uiMarshalDelivered()) + " 次");
    if (const auto dir = testkit::perfReportDirFromEnv()) {
        EXPECT_TRUE(testkit::writePerfReportJson(*dir, session));
    }

    load.stopAndJoin();
}

}  // namespace
