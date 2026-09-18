/**
 * @file   ResourceGovernanceTest.cpp
 * @brief  资源治理用例组（EX-T07）——EX-RES-2（内存采样 fake 推至 >70%：
 *         先节流〔停派发/降并行〕→仍超限→EX-RESOURCE-INSUFFICIENT 诊断；
 *         已排队/运行任务不失败；观测点＝派发暂停＋诊断）、三段阈值边界
 *         （65%/70%——D-05：65% 为实现参数非需求阈值）、耗尽边沿诊断
 *         恰一次、预算转发、采样间隔节流、采样失败保持、MemProbe 真实
 *         探针冒烟与生产采样件汇总（§6.6——主进程＋全部 worker 口径）。
 *
 * 设计依据：
 *   - units/execution.md §6.1（内存预算行：主进程＋全部 worker 合计峰值
 *     ≤ 物理内存 70%〔NFR-PERF-04 上游值〕；接近上限〔默认阈值 65%，
 *     实现参数 D-05〕先节流：暂停派发新任务＋降低并行度（回收空闲
 *     worker）→仍超限→EX-RESOURCE-INSUFFICIENT 诊断＋新任务保持排队
 *     〔不失败已排队/运行中任务〕）、§6.2（资源监控线程：周期内存采样
 *     ＋节流决策建议——决策仍由调度线程执行）、§6.6（内存采样行：
 *     GlobalMemoryStatusEx＋QueryInformationJobObject——MemProbe 汇总
 *     "主进程＋全部工作进程"）、§11（EX-RES-2 用例行）、§15.1（D-05
 *     三段治理登记）、§3.3（IExecutionDiagnosticsSink 注入——诊断出口）
 *   - ARCHITECTURE.md §4.6（资源治理：ResourceController 汇总＋先节流
 *     后诊断——P-EX-2：v0.11 Draft 待评审，评审变更按影响面增量同步）
 *   - 需求 NFR-PERF-04（70% 内存、先节流后诊断——规模化验收归
 *     WP-23-T06，本套件只覆盖执行侧基础形态，不越 §2.2 不可越界列）
 *   - 任务契约 tasks/foundation/EX-T07.json acceptance 1~3（acceptance 1
 *     ＝EX-RES-2 全绿＋三段治理就位；acceptance 2＝汇总口径＋比较型
 *     三要素诊断经 §3.3 sink；acceptance 3＝规模化不越界＋P-EX-1/P-EX-2
 *     处置——消费 core DiagnosticRecord/ComparativeFields/UnitToken/
 *     SourcedValue 公共头现状签名，冻结 diff 后按影响面增量同步）
 *
 * 替身边界声明（§11 同源纪律）：
 *   - 内存采样为脚本替身（ScriptedMemorySampler——EX-RES-2 用例行明文
 *     "内存采样 fake 推至 >70%"的承载；真实探针另设冒烟用例实证 API 面，
 *     两者合起来才构成采样口径的证据，替身本身不证明 Windows API 行为）；
 *   - worker 作业内存的真实聚合（SupervisorMemorySampler 的 worker 半区）
 *     归 contract_test 真进程用例（WorkerProcessContractTest 增量用例——
 *     作业句柄属监督器调度线程域，真进程才能产生非零作业内存）；
 *   - 评估器注册/修订闭包/存储写权限为接口替身或真实存储（EX-T05 套件
 *     同款底座——本套件不重复验证其语义，只作为提交受理的前置）。
 *
 * 时钟纪律（testkit §6.5）：全部时序断言（采样间隔）经注入 ManualClock
 *   虚拟推进，不 sleep。
 */

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/core/Events.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/core/Provenance.hpp>
#include <sdurws/ird/core/Units.hpp>
#include <sdurws/ird/evidence/Evaluator.hpp>
#include <sdurws/ird/evidence/Snapshot.hpp>
#include <sdurws/ird/execution/Controller.hpp>
#include <sdurws/ird/execution/Errors.hpp>
#include <sdurws/ird/execution/EventBus.hpp>
#include <sdurws/ird/execution/Ports.hpp>
#include <sdurws/ird/execution/Scheduler.hpp>
#include <sdurws/ird/project/ProjectStore.hpp>

#include "win32/MemProbe.hpp"   // MemProbe 真实探针冒烟（私有头——R-2 纪律下仅本单元测试可见）
#include "win32/JobScope.hpp"   // 空作业句柄构造（真实作业对象，零进程）

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {

using namespace sdurws::ird::execution;
namespace core = sdurws::ird::core;
namespace ev = sdurws::ird::evidence;
namespace pd = sdurws::ird::project;
namespace fs = std::filesystem;

// =====================================================================
// 身份/取值辅助（SchedulerEventBusTest 同款确定性固定值——自持不共享）
// =====================================================================

const char* kHex32A = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
const char* kHex64A = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
const char* kHex64B = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

core::ContentVersion cv(const char* hex64)
{
    return core::ContentVersion::fromCanonical(std::string{"cv-"} + hex64);
}

core::ObjectId oid(const char* hex32)
{
    return core::ObjectId::fromCanonical(std::string{"obj-"} + hex32);
}

core::ContentIdentity cid(const char* hex64)
{
    return core::ContentIdentity::fromCanonical(std::string{"cid-"} + hex64);
}

// =====================================================================
// ManualClock——虚拟时钟（时序断言经显式推进，不 sleep；四编排同源注入
// ——TaskController/DrainCoordinator/TaskScheduler/ResourceController）
// =====================================================================

class ManualClock {
public:
    std::chrono::steady_clock::time_point now() const { return m_now; }
    void advance(std::chrono::milliseconds d) { m_now += d; }

private:
    std::chrono::steady_clock::time_point m_now{std::chrono::milliseconds{1'000'000}};
};

// =====================================================================
// 脚本内存采样替身（EX-RES-2"内存采样 fake 推至 >70%"的承载）
// =====================================================================

class ScriptedMemorySampler final : public IMemorySampler {
public:
    /// 预置读数（单位字节——total=1'000'000'000 的十亿字节刻度，便于
    /// 百分比心算：main+worker 即百万分之一占比刻度……实际按比率断言，
    /// 刻度只求可读）。
    void set(std::uint64_t totalPhysical, std::uint64_t mainProcess, std::uint64_t workers)
    {
        m_reading.totalPhysicalBytes = totalPhysical;
        m_reading.mainProcessBytes = mainProcess;
        m_reading.workerBytes = workers;
    }

    /// 故障注入：置位后 sample() 返回 nullopt（采样失败路径）。
    void fail() { m_fail = true; }
    void recover() { m_fail = false; }

    std::optional<MemoryReading> sample() override
    {
        ++m_calls;
        if (m_fail) {
            return std::nullopt;
        }
        return m_reading;
    }

    int calls() const { return m_calls; }

private:
    MemoryReading m_reading;
    bool m_fail = false;
    int m_calls = 0;
};

// =====================================================================
// 诊断收集 sink（§3.3 IExecutionDiagnosticsSink 替身——结构化/开发双
// 通道记录；本套件验证的就是"诊断经注入 sink 出具"这一消费契约）
// =====================================================================

class RecordingSink final : public IExecutionDiagnosticsSink {
public:
    void report(const core::DiagnosticRecord& record) override { m_reports.push_back(record); }
    void reportDev(const std::string& channel, const std::string& message) override
    {
        m_dev.emplace_back(channel, message);
    }

    const std::vector<core::DiagnosticRecord>& reports() const { return m_reports; }
    const std::vector<std::pair<std::string, std::string>>& dev() const { return m_dev; }

private:
    std::vector<core::DiagnosticRecord> m_reports;
    std::vector<std::pair<std::string, std::string>> m_dev;
};

// =====================================================================
// 事件收集 sink（总线订阅面——调度器状态事件流的观测点）
// =====================================================================

class CollectingEventSink final : public core::IDomainEventSink {
public:
    void onEvent(const core::DomainEvent& event) override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_events.push_back(event);
    }
    std::vector<core::DomainEvent> snapshot() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_events;
    }

private:
    mutable std::mutex m_mutex;
    std::vector<core::DomainEvent> m_events;
};

// =====================================================================
// 提交受理前置替身（EX-T05 套件同款最小实现——只作为受理前置，本套件
// 不重复验证 V1~V3 语义）
// =====================================================================

class ScriptedProducerRegistry final : public ev::IProducerRegistryView {
public:
    explicit ScriptedProducerRegistry(std::map<std::string, std::uint32_t> table)
        : m_table(std::move(table))
    {
    }
    bool isRegistered(std::string_view key) const override
    {
        return m_table.count(std::string{key}) > 0;
    }
    bool contractVersionMatches(std::string_view key, std::uint32_t version) const override
    {
        const auto it = m_table.find(std::string{key});
        return it != m_table.end() && it->second == version;
    }

private:
    std::map<std::string, std::uint32_t> m_table;
};

class AcceptAllClosureSource final : public ev::IRevisionClosureSource {
public:
    bool objectInRevision(core::RevisionId, core::ObjectId, core::ContentVersion) const override
    {
        return true;
    }
};

// =====================================================================
// 冻结快照构建（EX-T05 套件同构最小实例——真实 builder 产物）
// =====================================================================

ev::AnalysisSnapshot buildSnapshotFor(core::ProjectId project, core::BranchId branch,
                                      core::RevisionId revision)
{
    ev::SnapshotBuilder b;
    b.setIdentity(project, branch, revision, 5);
    b.setPolicyRef(ev::PolicyRef{cid(kHex64A)});
    b.setNameMapRef(ev::NameMapRef{cid(kHex64A)});
    ev::ReproductionBlock r;
    r.productVersion = "industrialrobot-designer 0.1.0";
    r.evidenceContractVersion = "evidence-contract/1";
    r.codecVersions = {"snapshot-codec/1", "slice-codec/1"};
    b.setReproduction(r);
    ev::ObjectRefEntry obj;
    obj.objectId = oid(kHex32A);
    obj.contentVersion = cv(kHex64A);
    obj.objectTypeToken = "robot-design";
    obj.digest = obj.contentVersion.bytes;
    b.addObjectRef(obj);
    b.addCase({oid(kHex32A), "case-a", true, true});
    AcceptAllClosureSource source;
    return b.build(source);
}

// =====================================================================
// 控制器级用例的公共夹具（无调度器——纯三段判定/边沿/采样失败/间隔）
// =====================================================================

class ResourceControllerTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_clock = std::make_unique<ManualClock>();
        m_sampler = std::make_unique<ScriptedMemorySampler>();
        // 默认读数＝物理内存 50%（5 亿 / 10 亿字节刻度）——Normal 基线。
        m_sampler->set(kTotal, kTotal / 2, 0);
        ResourceController::Config config;   // 采样间隔默认 1000 ms（D 参数——非上游值）
        m_controller = std::make_unique<ResourceController>(*m_sampler, config,
                                                            [this] { return m_clock->now(); });
        m_controller->setDiagnosticsSink(&m_sink);
    }

    /// 推进一个采样间隔并评估一拍（时序断言全部经虚拟时钟——不 sleep）。
    ResourceController::Decision evaluateAfterInterval()
    {
        m_clock->advance(std::chrono::milliseconds{1000});
        return m_controller->evaluate();
    }

    static constexpr std::uint64_t kTotal = 1'000'000'000;  ///< 物理内存刻度（10 亿字节——占比＝占用/10⁹）

    std::unique_ptr<ManualClock> m_clock;
    std::unique_ptr<ScriptedMemorySampler> m_sampler;
    RecordingSink m_sink;
    std::unique_ptr<ResourceController> m_controller;
};

// =====================================================================
// 三段阈值边界（acceptance 1"三段治理就位"——D-05：65% 实现参数/
// 70% 上游值；边界取 ≥ 语义）
// =====================================================================

TEST_F(ResourceControllerTest, ThreeStageThresholdBoundaries_D05)
{
    // 预算取默认（0.65/0.70——ResourceBudget 字段默认值＝D-05 登记值；
    // 70% 是 NFR-PERF-04 上游值不改动）。读数刻度：kTotal 的 64.9%/
    // 65.0%/69.9%/70.0%。
    const struct {
        std::uint64_t aggregate;          // 主进程＋worker 合计（字节）
        ResourceController::Level expect; // 期望层级
    } rows[] = {
        {static_cast<std::uint64_t>(0.649 * kTotal), ResourceController::Level::Normal},
        {static_cast<std::uint64_t>(0.650 * kTotal), ResourceController::Level::Throttled},
        {static_cast<std::uint64_t>(0.699 * kTotal), ResourceController::Level::Throttled},
        {static_cast<std::uint64_t>(0.700 * kTotal), ResourceController::Level::Exhausted},
        {static_cast<std::uint64_t>(0.650 * kTotal), ResourceController::Level::Throttled},  // 回落：迟滞带外即时重判（恢复语义）
        {static_cast<std::uint64_t>(0.649 * kTotal), ResourceController::Level::Normal},
    };
    for (const auto& row : rows) {
        m_sampler->set(kTotal, row.aggregate, 0);   // 全部计入主进程半区（占比口径不区分半区）
        const ResourceController::Decision d = evaluateAfterInterval();
        EXPECT_EQ(d.level, row.expect) << "aggregate=" << row.aggregate;
        // 两标志与层级一致（停派发/降并行建议面——§6.1"先节流：暂停派发
        // 新任务＋降低并行度"；Normal 双 false）。
        EXPECT_EQ(d.pauseDispatch, row.expect != ResourceController::Level::Normal);
        EXPECT_EQ(d.reclaimIdleWorkers, row.expect != ResourceController::Level::Normal);
        EXPECT_EQ(m_controller->level(), row.expect);
    }
}

// =====================================================================
// 耗尽边沿诊断恰一次（acceptance 2——比较型三要素经 §3.3 sink）
// =====================================================================

TEST_F(ResourceControllerTest, ExhaustedEdgeDiagnosticOncePerEpisodeWithComparativeFields)
{
    // 第一拍：Normal——无诊断。
    {
        const ResourceController::Decision d = evaluateAfterInterval();
        EXPECT_EQ(d.level, ResourceController::Level::Normal);
        EXPECT_FALSE(d.diagnostic.has_value());
    }
    // 推至 71%：进入 Exhausted 的边沿——恰一次出具 EX-RESOURCE-INSUFFICIENT
    // （比较型：实际占比/上限 0.70/单位 "1"——Dimensionless 冻结 token）。
    m_sampler->set(kTotal, 710'000'000, 0);
    {
        const ResourceController::Decision d = evaluateAfterInterval();
        EXPECT_EQ(d.level, ResourceController::Level::Exhausted);
        ASSERT_TRUE(d.diagnostic.has_value());
        ASSERT_EQ(m_sink.reports().size(), 1u);   // 经 §3.3 sink 外报恰一次
        const core::DiagnosticRecord& rec = m_sink.reports().at(0);
        EXPECT_EQ(rec.code, "EX-RESOURCE-INSUFFICIENT");
        EXPECT_EQ(rec.context, "execution/resource");
        // 比较型三要素（core DiagnosticRecord::comparison 契约——UX-03）。
        ASSERT_TRUE(rec.comparison.has_value());
        ASSERT_TRUE(rec.comparison->actual.quantity.tryValue().has_value());
        EXPECT_DOUBLE_EQ(*rec.comparison->actual.quantity.tryValue(), 710'000'000.0 / kTotal);
        ASSERT_TRUE(rec.comparison->expected.quantity.tryValue().has_value());
        EXPECT_DOUBLE_EQ(*rec.comparison->expected.quantity.tryValue(), 0.70);
        EXPECT_TRUE(rec.comparison->actual.unit.isValid());
        EXPECT_EQ(rec.comparison->actual.unit.symbol(), "1");
        EXPECT_EQ(rec.comparison->expected.unit.symbol(), "1");
        // 来源标注：DerivedReadOnly＋memprobe（SourcedValue Provided 态必带
        // provenance——不伪造来源）。
        EXPECT_EQ(rec.comparison->actual.quantity.provenance().kind,
                  core::ProvenanceKind::DerivedReadOnly);
        // subject 恒空：资源压力不属于任何业务对象（不伪造 ObjectId）。
        EXPECT_FALSE(rec.subject.has_value());
        EXPECT_FALSE(rec.cause.empty());
        EXPECT_FALSE(rec.recommendedAction.empty());
    }
    // 持续超限（72%）：不重复刷诊断（§6.1"仍超限→诊断"是进入事件非
    // 持续广播）。
    m_sampler->set(kTotal, 720'000'000, 0);
    {
        const ResourceController::Decision d = evaluateAfterInterval();
        EXPECT_EQ(d.level, ResourceController::Level::Exhausted);
        EXPECT_FALSE(d.diagnostic.has_value());
        EXPECT_EQ(m_sink.reports().size(), 1u);
    }
    // 回落节流带（66%）：退出 Exhausted 无诊断（只有进入耗尽才出具）。
    m_sampler->set(kTotal, 660'000'000, 0);
    {
        const ResourceController::Decision d = evaluateAfterInterval();
        EXPECT_EQ(d.level, ResourceController::Level::Throttled);
        EXPECT_FALSE(d.diagnostic.has_value());
        EXPECT_EQ(m_sink.reports().size(), 1u);
    }
    // 再次进入耗尽：新一轮边沿——第二次诊断（"仍超限"的新 episode）。
    m_sampler->set(kTotal, 705'000'000, 0);
    {
        const ResourceController::Decision d = evaluateAfterInterval();
        EXPECT_EQ(d.level, ResourceController::Level::Exhausted);
        EXPECT_TRUE(d.diagnostic.has_value());
        EXPECT_EQ(m_sink.reports().size(), 2u);
    }
}

// =====================================================================
// 采样失败保持（不猜值——开发诊断经 reportDev 边沿一次）
// =====================================================================

TEST_F(ResourceControllerTest, ProbeFailureKeepsLastLevelAndReportsDevOnce)
{
    // 先建立 Throttled 基线（66%）。
    m_sampler->set(kTotal, 660'000'000, 0);
    EXPECT_EQ(evaluateAfterInterval().level, ResourceController::Level::Throttled);

    // 采样失败：保持 Throttled（停派发面不回退——fail-safe 方向），dev
    // 通道报告恰一次（连续失败去抖）。
    m_sampler->fail();
    {
        const ResourceController::Decision d = evaluateAfterInterval();
        EXPECT_EQ(d.level, ResourceController::Level::Throttled);
        EXPECT_TRUE(d.pauseDispatch);
        EXPECT_FALSE(d.diagnostic.has_value());   // 失败不构成层级迁移→无边沿诊断
    }
    {
        const ResourceController::Decision d = evaluateAfterInterval();
        EXPECT_EQ(d.level, ResourceController::Level::Throttled);
        EXPECT_EQ(m_sink.dev().size(), 1u);       // 第二次失败不再重复报
        EXPECT_EQ(m_sink.dev().at(0).first, "execution/resource");
    }
    // 恢复成功后再次失败：新的失败 episode——再报一次（边沿语义）。
    m_sampler->recover();
    evaluateAfterInterval();
    m_sampler->fail();
    evaluateAfterInterval();
    EXPECT_EQ(m_sink.dev().size(), 2u);
    EXPECT_EQ(m_sink.reports().size(), 0u);       // 全程无结构化诊断（未进入耗尽）
}

// =====================================================================
// 采样间隔节流（§6.2"周期内存采样"——间隔内复用最近决策）
// =====================================================================

TEST_F(ResourceControllerTest, SampleIntervalLimitsProbeCallsAndReplaysDecision)
{
    // 初始 71%（Exhausted 基线）。
    m_sampler->set(kTotal, 710'000'000, 0);
    {
        const ResourceController::Decision first = m_controller->evaluate();   // 首拍立即采样
        EXPECT_EQ(first.level, ResourceController::Level::Exhausted);
        EXPECT_EQ(m_sampler->calls(), 1);
    }
    // 间隔内（不推进时钟）：复用最近决策，不重复采样；重放拍 diagnostic
    // 恒空（边沿只在真实采样迁移层级的那一拍）。
    {
        const ResourceController::Decision replay = m_controller->evaluate();
        EXPECT_EQ(replay.level, ResourceController::Level::Exhausted);
        EXPECT_TRUE(replay.pauseDispatch);
        EXPECT_EQ(m_sampler->calls(), 1);
        EXPECT_FALSE(replay.diagnostic.has_value());
    }
    // 推进 999 ms：仍在间隔内（<1000 ms）。
    m_clock->advance(std::chrono::milliseconds{999});
    EXPECT_EQ(m_controller->evaluate().level, ResourceController::Level::Exhausted);
    EXPECT_EQ(m_sampler->calls(), 1);
    // 再推 1 ms（累计 1000 ms）：新采样拍。
    m_clock->advance(std::chrono::milliseconds{1});
    m_controller->evaluate();
    EXPECT_EQ(m_sampler->calls(), 2);
}

// =====================================================================
// EX-RES-2 主场景（acceptance 1——调度器集成：先节流〔派发暂停〕→
// 仍超限→诊断；已排队/运行任务不失败）
// =====================================================================

class ResourceGovernanceSchedulerTest : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        std::error_code ec;
#ifdef _WIN32
        s_base = fs::temp_directory_path(ec) / "ird_ex_resource_test"
                 / std::to_string(::GetCurrentProcessId());
#else
        s_base = fs::temp_directory_path(ec) / "ird_ex_resource_test";
#endif
        ASSERT_FALSE(ec);
        fs::create_directories(s_base, ec);
        if (ec) {
            std::exit(2);
        }
    }

    static void TearDownTestSuite()
    {
        std::error_code ec;
        fs::remove_all(s_base, ec);   // 失败保留现场的例外：统一清理（幂等重跑）
    }

    void SetUp() override
    {
        m_dir = s_base / ("case" + std::to_string(++s_caseCounter)) / "p.rwdesign";
        m_store = pd::ProjectStoreFactory::createNew(m_dir, "EX-T07", nullptr, nullptr).store;
        ASSERT_TRUE(m_store != nullptr);
        ASSERT_TRUE(m_store->writable());

        m_clock = std::make_unique<ManualClock>();
        m_registry = std::make_unique<ScriptedProducerRegistry>(
            ScriptedProducerRegistry({{"kin-batch-ik", 7}}));
        m_closure = std::make_unique<AcceptAllClosureSource>();
        m_sampler = std::make_unique<ScriptedMemorySampler>();
        m_sampler->set(kTotal, kTotal / 2, 0);   // 基线 50%——Normal

        m_snapshot = std::make_unique<const ev::AnalysisSnapshot>(
            buildSnapshotFor(m_store->projectId(), core::BranchId::generate(),
                             core::RevisionId::generate()));

        // 预算：并发上限 2（使"额度空位"可观测）、队列容量默认；其余
        // 走默认（65%/70%——D-05/上游值）。无内联执行体（普通派发停留
        // Preparing——在途任务持续存在，验证"运行任务不失败"）。
        m_budget.maxConcurrentTasks = 2;
        m_budget.queueCapacity = 256;
    }

    void TearDown() override
    {
        m_scheduler.reset();
        m_resourceController.reset();
        m_drain.reset();
        m_taskController.reset();
    }

    /// 组装：调度器＋资源控制器＋诊断 sink＋事件总线（同一 ManualClock 源）。
    void makeScheduler()
    {
        TaskController::Config controllerConfig;
        m_taskController = std::make_unique<TaskController>(
            controllerConfig, [this] { return m_clock->now(); });
        m_drain = std::make_unique<DrainCoordinator>(*m_taskController, DrainCoordinator::Config{},
                                                     [this] { return m_clock->now(); });

        ResourceController::Config rcConfig;   // 采样间隔默认 1000 ms——测试逐拍显式推进
        m_resourceController = std::make_unique<ResourceController>(
            *m_sampler, rcConfig, [this] { return m_clock->now(); });
        m_resourceController->setDiagnosticsSink(&m_sink);

        TaskScheduler::Collaboration collab;
        collab.evaluators = m_registry.get();
        collab.closure = m_closure.get();
        collab.store = m_store.get();
        collab.guard = nullptr;      // V1 注入半区可空（EX-T05——可选增强，跳过）
        collab.capabilities = nullptr;

        m_scheduler = std::make_unique<TaskScheduler>(*m_taskController, *m_drain, collab,
                                                      TaskScheduler::Config{},
                                                      [this] { return m_clock->now(); });
        m_scheduler->setEventBus(&m_bus);
        // 不注入内联执行体（无内联能力——全部任务排队后经出闸派发停留
        // Preparing，"运行任务不失败"的可观测载体）。
        m_scheduler->setResourceBudget(m_budget);
        m_scheduler->setResourceController(m_resourceController.get());
        m_sinkSubscription = m_bus.subscribe(m_eventSink);
    }

    /// 合法提交底座（Preview——不写 results/，V3 跳过；受理前置全绿）。
    TaskSubmission baseSubmission()
    {
        TaskSubmission s;
        s.snapshot = *m_snapshot;
        s.evaluatorKey = "kin-batch-ik";
        s.contractVersion = 7;
        s.mode = core::EvaluationMode::Preview;
        s.priority = TaskPriority::Background;
        return s;
    }

    /// 推进一个采样间隔并 tick 一拍（调度域显式驱动——不 sleep）。
    void tickAfterInterval()
    {
        m_clock->advance(std::chrono::milliseconds{1000});
        m_scheduler->tick();
    }

    /// 事件流中的全部任务状态（TaskStatusChanged 投影——失败断言素材）。
    std::vector<core::TaskState> statusStates() const
    {
        std::vector<core::TaskState> states;
        for (const core::DomainEvent& e : m_eventSink.snapshot()) {
            if (e.kind == core::DomainEventKind::TaskStatusChanged) {
                states.push_back(e.asTaskStatusChanged().newState);
            }
        }
        return states;
    }

    static constexpr std::uint64_t kTotal = 1'000'000'000;  ///< 物理内存刻度（与控制器级用例同刻度）

    static fs::path s_base;
    static int s_caseCounter;

    fs::path m_dir;
    std::unique_ptr<pd::ProjectStore> m_store;
    std::unique_ptr<ManualClock> m_clock;
    std::unique_ptr<ScriptedProducerRegistry> m_registry;
    std::unique_ptr<AcceptAllClosureSource> m_closure;
    std::unique_ptr<ScriptedMemorySampler> m_sampler;
    std::unique_ptr<const ev::AnalysisSnapshot> m_snapshot;
    ResourceBudget m_budget;
    RecordingSink m_sink;
    std::unique_ptr<TaskController> m_taskController;
    std::unique_ptr<DrainCoordinator> m_drain;
    std::unique_ptr<ResourceController> m_resourceController;
    std::unique_ptr<TaskScheduler> m_scheduler;
    CollectingEventSink m_eventSink;
    DomainEventBusImpl m_bus;
    /// 订阅句柄（声明在 m_bus 之后——析构逆序先退订，RAII 语义安全）。
    std::unique_ptr<core::IEventSubscription> m_sinkSubscription;
};

fs::path ResourceGovernanceSchedulerTest::s_base;
int ResourceGovernanceSchedulerTest::s_caseCounter = 0;

TEST_F(ResourceGovernanceSchedulerTest, ThrottleStopsDispatchThenExhaustDiagnostics_EX_RES_2)
{
    // 排布（§11 EX-RES-2 行：内存采样 fake 推至 >70%／持续提交／观测点
    // ＝派发暂停＋诊断）：三任务持续提交——50% 时两任务正常派发（并发
    // 额度 2 用满）→66% 先节流（第三任务保持排队＝派发暂停观测点）→
    // 71% 仍超限（EX-RESOURCE-INSUFFICIENT 诊断）→回落 50% 自动恢复派发。
    makeScheduler();

    // 持续提交三任务（§6.1"新任务保持排队"的排队面素材；TaskId 一律由
    // 受理段分配——§4.3，提交内容不携带身份）。
    const SubmitResult r1 = m_scheduler->submit(baseSubmission());
    const SubmitResult r2 = m_scheduler->submit(baseSubmission());
    const SubmitResult r3 = m_scheduler->submit(baseSubmission());
    ASSERT_TRUE(r1.accepted);
    ASSERT_TRUE(r2.accepted);
    ASSERT_TRUE(r3.accepted);
    const TaskId id1 = *r1.task;
    const TaskId id2 = *r2.task;
    const TaskId id3 = *r3.task;

    // 基线拍 ×2：50%——Normal，逐拍派发（并发额度 2 用满：t1/t2 入途，
    // 停留 Preparing——无内联执行体）。每拍推进恰一个采样间隔＝每拍都
    // 是新采样拍（1000 ms ≥ 间隔 1000 ms）。
    tickAfterInterval();   // 采样（50%→Normal）＋派发 t1
    ASSERT_EQ(m_scheduler->tryTask(id1)->state, core::TaskState::Preparing);
    tickAfterInterval();   // 采样（50%→Normal）＋派发 t2
    ASSERT_EQ(m_scheduler->tryTask(id2)->state, core::TaskState::Preparing);
    EXPECT_EQ(m_resourceController->level(), ResourceController::Level::Normal);
    EXPECT_TRUE(m_sink.reports().empty());

    // ---- 第一段：先节流（66% ≥ 65% 节流阈值〔D-05 实现参数〕）----
    m_sampler->set(kTotal, 660'000'000, 0);
    tickAfterInterval();   // 采样#2→Throttled——本拍不出队
    ASSERT_EQ(m_scheduler->tryTask(id3)->state, core::TaskState::Queued)
        << "EX-RES-2 观测点『派发暂停』：节流段排队任务保持 Queued（不出队不失败）";
    EXPECT_EQ(m_resourceController->level(), ResourceController::Level::Throttled);
    // 连续多拍仍节流：排队任务持续保持（不是一拍性抑制）。
    tickAfterInterval();
    ASSERT_EQ(m_scheduler->tryTask(id3)->state, core::TaskState::Queued);
    // 已在途任务不受影响（"运行任务不失败"的节流半区；D-04 不抢占）。
    EXPECT_EQ(m_scheduler->tryTask(id1)->state, core::TaskState::Preparing);
    EXPECT_EQ(m_scheduler->tryTask(id2)->state, core::TaskState::Preparing);
    EXPECT_TRUE(m_sink.reports().empty()) << "节流段尚无『资源不足』诊断（先节流后诊断）";

    // ---- 第二段：仍超限（71% ≥ 70% 上游值——诊断出具）----
    m_sampler->set(kTotal, 710'000'000, 0);
    tickAfterInterval();   // 采样#3→Exhausted 边沿——诊断恰一次
    EXPECT_EQ(m_resourceController->level(), ResourceController::Level::Exhausted);
    ASSERT_EQ(m_sink.reports().size(), 1u);
    EXPECT_EQ(m_sink.reports().at(0).code, "EX-RESOURCE-INSUFFICIENT");
    // 比较型三要素（acceptance 2）：实际占比 0.71／上限 0.70／单位 "1"。
    ASSERT_TRUE(m_sink.reports().at(0).comparison.has_value());
    EXPECT_DOUBLE_EQ(*m_sink.reports().at(0).comparison->actual.quantity.tryValue(),
                     710'000'000.0 / kTotal);
    EXPECT_DOUBLE_EQ(*m_sink.reports().at(0).comparison->expected.quantity.tryValue(), 0.70);
    EXPECT_EQ(m_sink.reports().at(0).comparison->actual.unit.symbol(), "1");
    // 排队任务在耗尽段同样不失败（§6.1"新任务保持排队（不失败已排队/
    // 运行中任务）"——诊断不改变任何任务状态）。
    EXPECT_EQ(m_scheduler->tryTask(id3)->state, core::TaskState::Queued);
    EXPECT_EQ(m_scheduler->tryTask(id1)->state, core::TaskState::Preparing);
    EXPECT_EQ(m_scheduler->tryTask(id2)->state, core::TaskState::Preparing);
    // 持续超限多拍：诊断不重复（边沿恰一次），排队任务持续保持。
    tickAfterInterval();
    tickAfterInterval();
    EXPECT_EQ(m_sink.reports().size(), 1u);
    EXPECT_EQ(m_scheduler->tryTask(id3)->state, core::TaskState::Queued);

    // ---- 恢复：读数回落 50%——层级回 Normal，资源闸放行 ----
    m_sampler->set(kTotal, kTotal / 2, 0);
    tickAfterInterval();
    EXPECT_EQ(m_resourceController->level(), ResourceController::Level::Normal);
    // t3 本拍仍不出队不是资源闸（层级已 Normal、闸已放行），而是并发额度
    // 闸：在途计数含停留 Preparing 的 t1/t2（阶段 A 无 worker 派发链），
    // 额度 2 已满。两个闸用同一观测面（出队与否）区分的对照面＝下一组拍：
    // 排队取消 t1（正常取消零诊断，UX-03；无 worker 任务取消两步连发直达
    // Canceled——§7.1），额度空位后 t3 立即出队——节流段同等条件不出队、
    // Normal 段出队，资源闸语义闭环。
    EXPECT_EQ(m_scheduler->tryTask(id3)->state, core::TaskState::Queued);
    ASSERT_TRUE(m_taskController->requestCancel(id1).accepted);
    tickAfterInterval();   // poll：t1 Preparing→Canceling→Canceled（无 worker 直达）＋派发 t3
    EXPECT_EQ(m_scheduler->tryTask(id3)->state, core::TaskState::Preparing)
        << "恢复后派发自动继续：同等额度空位下节流段不出队、Normal 段出队——资源闸语义闭环";

    // ---- 全程"已排队/运行任务不失败"的事件流复核 ----
    // 全部状态事件只含 Queued/Preparing/Canceling/Canceled（t1 的正常取消
    // 走廊）——无 Failed/Interrupted；且无任何任务级诊断写入（诊断 sink
    // 只有资源面的一条）。
    const std::vector<core::TaskState> states = statusStates();
    for (const core::TaskState s : states) {
        EXPECT_NE(s, core::TaskState::Failed) << "全程不得出现 Failed（不失败已排队/运行任务）";
        EXPECT_NE(s, core::TaskState::Interrupted) << "全程不得出现 Interrupted";
    }
    EXPECT_EQ(m_sink.reports().size(), 1u);   // 资源诊断恰一次，无追加
}

// =====================================================================
// 预算转发（EX-T05 登记"ResourceController 消费"的兑现——单一用户入口）
// =====================================================================

TEST_F(ResourceGovernanceSchedulerTest, BudgetUpdateForwardsToResourceController)
{
    makeScheduler();
    // 默认预算：55% 属 Normal（<65%）。
    m_sampler->set(kTotal, 550'000'000, 0);
    tickAfterInterval();
    EXPECT_EQ(m_resourceController->level(), ResourceController::Level::Normal);

    // 运行期改预算（节流阈值降至 50%）：经 ITaskScheduler::setResourceBudget
    // 单一入口转发后，同读数（55%）即入节流段——阈值消费来自转发的预算。
    ResourceBudget tightened;
    tightened.throttleRatio = 0.50;
    tightened.maxMemoryRatio = 0.60;
    tightened.maxConcurrentTasks = 2;
    m_scheduler->setResourceBudget(tightened);
    tickAfterInterval();
    EXPECT_EQ(m_resourceController->level(), ResourceController::Level::Throttled);

    // 上限也随预算收紧（60%）：61% 读数直接判耗尽（同上游 70% 语义、
    // 参数面可配——§6.1"可配"；诊断的 expected 侧携带收紧后的上限）。
    m_sampler->set(kTotal, 610'000'000, 0);
    tickAfterInterval();
    EXPECT_EQ(m_resourceController->level(), ResourceController::Level::Exhausted);
    ASSERT_EQ(m_sink.reports().size(), 1u);
    EXPECT_DOUBLE_EQ(*m_sink.reports().at(0).comparison->expected.quantity.tryValue(), 0.60);
}

// =====================================================================
// MemProbe 真实探针冒烟（§6.6 内存采样行 API 面——替身用例的对端实证）
// =====================================================================

TEST(MemProbeRealApiTest, SystemAndProcessProbesReturnPlausibleReadings)
{
    // 系统半区：总量 >0、可用 ≤ 总量（GlobalMemoryStatusEx 快照语义的
    // 最小自洽断言——不承诺具体数值）。
    win32::SystemMemoryInfo sys;
    ASSERT_TRUE(win32::querySystemMemory(sys));
    EXPECT_GT(sys.totalPhysicalBytes, 0u);
    EXPECT_LE(sys.availablePhysicalBytes, sys.totalPhysicalBytes);

    // 主进程半区：本测试进程的工作集 >0（GetProcessMemoryInfo——伪句柄
    // 自查路径）。
    std::uint64_t workingSet = 0;
    ASSERT_TRUE(win32::queryCurrentProcessWorkingSetBytes(workingSet));
    EXPECT_GT(workingSet, 0u);
}

TEST(MemProbeRealApiTest, JobMemoryProbeOnFreshEmptyJobIsZeroAndNullFails)
{
    // 真实作业对象（KILL_ON_JOB_CLOSE 同 EX-T06 生产形态）但未纳入任何
    // 进程：提交内存峰值恒 0（空作业——QueryInformationJobObject 路径
    // 的可达性实证；非零值面归 contract_test 真进程用例）。
    win32::JobScope job;
    ASSERT_TRUE(job.valid());
    std::uint64_t peak = 12345;
    ASSERT_TRUE(win32::queryJobPeakCommittedBytes(job.handle(), peak));
    EXPECT_EQ(peak, 0u);

    // 无效句柄：查询失败（false——调用方按采样失败处置）。
    std::uint64_t unused = 0;
    EXPECT_FALSE(win32::queryJobPeakCommittedBytes(nullptr, unused));
}

// =====================================================================
// 生产采样件（SupervisorMemorySampler——汇总口径的真实半区冒烟；worker
// 半区非零值面归 contract_test 真进程用例）
// =====================================================================

TEST(SupervisorSamplerTest, AggregatesSystemAndMainProcessWithoutWorkerPool)
{
    // 无 worker 池装配（监督器空指针——合法装配）：worker 半区恒 0，
    // 系统/主进程半区为真实读数且自洽（主进程工作集 ≤ 物理总量）。
    SupervisorMemorySampler sampler(nullptr);
    const std::optional<MemoryReading> reading = sampler.sample();
    ASSERT_TRUE(reading.has_value());
    EXPECT_GT(reading->totalPhysicalBytes, 0u);
    EXPECT_GT(reading->mainProcessBytes, 0u);
    EXPECT_EQ(reading->workerBytes, 0u);
    EXPECT_LE(reading->mainProcessBytes, reading->totalPhysicalBytes);
    // 合计与占比（MemoryReading 自洽——分母非零时占比在 (0,1] 内）。
    EXPECT_EQ(reading->aggregateBytes(), reading->mainProcessBytes + reading->workerBytes);
    const double ratio = reading->ratioOfPhysical();
    EXPECT_GT(ratio, 0.0);
    EXPECT_LE(ratio, 1.0);
}

}  // namespace
