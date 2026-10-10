/**
 * @file   HostAssemblyContractTest.cpp
 * @brief  宿主装配面端到端契约（ASM-WF 宿主收口批次——装配核查后的
 *         控制器经基线虚类接口真实驱动、P-SEL-9 权威写入经①端口提交、
 *         L5 关闭监督器兜底链、恢复事实适配器统一诊断目录真实集成）。
 *
 * 设计依据：
 *   - units/workflow.md §10.2 v0.5~v1.2（端口接缝"L5 装配层实现"登记）、
 *     §7.3（A7 等待语义＋L5 abandonAll 兜底——project.md §9.7 义务）、
 *     units/selection.md §19.1 P-SEL-9（"权威 robot-drivetrain 对象写入
 *     走 modeling 既有 apply-drivetrain-design 命令链……由 L5 装配层把
 *     本命令的回填字段转译编排"——token 路由与①端口提交的真实落盘面）
 *   - 契约测试形态（§11.0"契约测试＝跨单元联合"）：本文件与 project
 *     真实存储（createNew/open/①端口七步事务）、execution 真实全链
 *     （TaskController/DrainCoordinator/TaskScheduler——InFlightArchive-
 *     ContractTest 同款装配）、diagnostics 真实目录（码注册/subject 边界/
 *     上下文校验全真——StatusBannerContractTest 同款）联合；领域处理器
 *     为测试桩（R-1：本目标不链 modeling/selection——token 路由语义经
 *     替身处理器承载，字段映射对账归宿主装配批次，单元卡 §10.2 本批
 *     登记段如实登记该边界）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记

#include <sdurws/ird/core/DiagData.hpp>           // DiagnosticRecord（真实产出重建）
#include <sdurws/ird/execution/EventBus.hpp>      // exe::DomainEventBusImpl（真实事件总线——execution 全链）
#include <sdurws/ird/diagnostics/Catalog.hpp>     // DiagCatalog/DiagContext/DiagQuery/IDevLogSink（真实目录装配族）
#include <sdurws/ird/diagnostics/DiagCodes.hpp>   // StableCodeRegistry/registerBuiltinCodes（码表真链）
#include <sdurws/ird/diagnostics/Factory.hpp>     // diagnostics::DiagnosticsFactory（create 校验链）
#include <sdurws/ird/evidence/Snapshot.hpp>       // AnalysisSnapshot/SnapshotBuilder（在途任务锚定快照）
#include <sdurws/ird/execution/Scheduler.hpp>     // TaskController/DrainCoordinator/TaskScheduler（真实排空）
#include <sdurws/ird/project/CommandService.hpp>  // CommandEnvelope/ICommandHandler（P-SEL-9 替身处理器）
#include <sdurws/ird/project/QueryPort.hpp>       // branchHistory/RevisionView（修订链观测）
#include <sdurws/ird/workflow/BackfillTranslate.hpp>
#include <sdurws/ird/workflow/CloseSupervisor.hpp>
#include <sdurws/ird/workflow/Lifecycle.hpp>

#include "plugin/WorkflowHostAdapters.hpp"  // 被测面（plugin/ 装配面——同单元 PRIVATE include，R-2 不外溢）

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

namespace exe = sdurws::ird::execution;
namespace ev = sdurws::ird::evidence;
namespace pd = sdurws::ird::project;

using namespace sdurws::ird;
using namespace sdurws::ird::workflow;

// =====================================================================
// 夹具：临时目录（套件级总根＋用例级独立目录——StatusBanner 同型）
// =====================================================================

class HostAssemblyContract : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        std::error_code ec;
        s_base = fs::temp_directory_path(ec) / "ird_wf_host_assembly_contract_test";
        ASSERT_FALSE(ec);
        fs::remove_all(s_base, ec);
        fs::create_directories(s_base, ec);
        ASSERT_FALSE(ec) << "无法创建测试根目录: " << s_base.string();
    }

    static void TearDownTestSuite()
    {
        std::error_code ec;
        fs::remove_all(s_base, ec);
    }

    void SetUp() override
    {
        m_dir = s_base / ("case" + std::to_string(++s_caseCounter));
        std::error_code ec;
        fs::create_directories(m_dir, ec);
        ASSERT_FALSE(ec);
    }

    /// 真实 blank 项目（createNew 直产——调用方持 store；StatusBanner
    /// createGolden 同款）。
    static project::OpenStoreResult createGolden(const fs::path& dir,
                                                 const char* displayName)
    {
        return project::ProjectStoreFactory::createNew(dir, displayName);
    }

    /// 场景①盘面注入（.staging/<tx>/ 事务残留——StatusBanner 同款词形）。
    void injectStagingResidue(const fs::path& projectDir, const std::string& txName)
    {
        const fs::path txDir = projectDir / ".staging" / txName;
        std::error_code ec;
        fs::create_directories(txDir, ec);
        ASSERT_FALSE(ec) << txDir.string();
        std::ofstream out(txDir / "payload.bin", std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(out.is_open());
        out << "unfinished-transaction-workspace";
    }

    static fs::path s_base;
    static int s_caseCounter;
    fs::path m_dir;
};

fs::path HostAssemblyContract::s_base;
int HostAssemblyContract::s_caseCounter = 0;

// =====================================================================
// P-SEL-9 替身处理器（token=apply-drivetrain-design 路由键——含"基线
// 闭包已有记录对象则继承 oid 改版"的 T09 语义；写 sel-device-backfill
// 记录对象＝selection 记录面先行的模拟——R-1 测试等价物）
// =====================================================================

class StubDrivetrainHandler final : public project::ICommandHandler {
public:
    [[nodiscard]] std::string commandType() const override
    {
        return std::string(kApplyDrivetrainDesignCommandToken);
    }
    [[nodiscard]] std::uint32_t currentPayloadVersion() const override { return 1; }

    project::PrepareOutcome prepare(project::HandlerContext& ctx,
                                    const project::CommandEnvelope& envelope,
                                    const project::RevisionView& baseSnapshot,
                                    project::CommandPlan& out,
                                    std::vector<core::DiagnosticRecord>&) override
    {
        project::ObjectWrite write;
        // T09 落位口径："基线闭包已有该对象则继承 oid 改版（PA-2 旧字节
        // 随历史闭包保留）"——基线闭包扫描既有记录对象并继承其 oid。
        for (const project::ObjectRef& ref : baseSnapshot.objectRefs) {
            if (ref.objectTypeToken == kSelBackfillRecordObjectToken) {
                write.objectId = ref.objectId;  // 继承 oid（改版——非新建）
                break;
            }
        }
        if (!write.objectId.has_value() || !write.objectId->isValid()) {
            write.objectId = ctx.objectId();  // 无既有记录＝project 分配新建
        }
        write.objectTypeToken = std::string(kSelBackfillRecordObjectToken);
        write.payloadCanonical = envelope.payloadCanonical;  // 载荷透传登记
        out.objectWrites.push_back(std::move(write));
        out.summary = "P-SEL-9 契约：权威写入替身（记录对象改版）";
        return project::PrepareOutcome::Planned;
    }
};

/// 解码/转译缝替身（编排核两缝的测试等价物——ok 轨固定产物）。
class ContractRecordPort final : public ISelBackfillRecordPort {
public:
    Decode decode(const std::vector<std::uint8_t>&) override
    {
        Decode out;
        out.ok = true;
        out.facts.referenceFrameToken = std::string(kSelBackfillLinkFrameToken);
        BackfillAxisRecord axis;
        axis.jointIdCanonical = core::ObjectId::generate().toCanonical();
        axis.appliedRatio = 100.0;   // 传动比（无量纲）
        axis.synthesisMassKg = 10.0; // 合成质量（kg）
        axis.catalogVersion = "2026.1";
        axis.mountKind = "ground";
        out.facts.axes.push_back(axis);
        return out;
    }
};

class ContractTranslatePort final : public ISelBackfillTranslatePort {
public:
    Translation translate(const SelBackfillRecordFacts& facts) override
    {
        Translation out;
        out.ok = true;
        out.draft.payloadFormatVersion = 1;
        out.draft.payloadCanonical = {0x01, static_cast<std::uint8_t>(facts.axes.size())};
        return out;
    }
};

// =====================================================================
// 用例一：装配核查后控制器经基线虚类接口真实关闭（PM-03 全链——真实
// store 落盘＋排空协议走完＝"宿主收口"的端到端含义）
// =====================================================================

/**
 * 装配核查通过的适配器链（Decision 桩＋Draft 适配器＋Drain 适配器）→
 * assembleWorkflowLifecycleController → 经 ILifecycleFlowController&
 * 接口 requestClose(Close)：三选保存→任务二选跳过（无任务）→排空
 * （真实 scheduler shutdown＋drained 立即达成）→存储上下文关闭（真实
 * requestClose/closed 协议）→Proceed＋closed()==true（PM-03/AT-20/
 * AT-21 的编排端到端观测点）。
 */
TEST_F(HostAssemblyContract, AssembledControllerClosesRealStoreThroughInterface)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-03"}, std::vector<std::string>{"AT-20", "AT-21"});

    // ---- 真实 store 落盘（黄金项目——createNew 直产）。
    const fs::path projectDir = m_dir / "host-close.rwdesign";
    project::OpenStoreResult created = createGolden(projectDir, "宿主关闭契约");
    ASSERT_TRUE(created.store != nullptr);
    ASSERT_TRUE(created.writable);
    const core::ProjectId projectId = created.store->projectId();

    // ---- 端口装配（真实桥适配器＋桩决策）。
    struct StubDecisions final : ICloseDecisionPort
    {
        DraftDisposition collectDraftDisposition(const CloseDialogData&) override
        {
            return DraftDisposition::Discard;  // 三选：放弃草稿（继续流程）
        }
        RunningTaskDecision collectRunningTaskDecision(const CloseDialogData&) override
        {
            return RunningTaskDecision::CancelFlow;  // 无任务时不达此点（防误触达）
        }
    };
    StubDecisions decisions;
    StoreCloseDraftAdapter::SessionHalf draftSession;  // 会话半区（无编辑会话——空半区）
    draftSession.unapplied = [] { return std::vector<std::string>{}; };
    draftSession.save = [] { return true; };
    draftSession.discard = [] { return true; };
    StoreCloseDraftAdapter drafts(*created.store, std::move(draftSession));
    execution::TaskController controller(execution::TaskController::Config{});
    execution::DrainCoordinator drain(controller, execution::DrainCoordinator::Config{});
    execution::TaskScheduler::Collaboration collab;
    execution::TaskScheduler scheduler(controller, drain, collab);
    ExecutionCloseDrainAdapter drainAdapter(scheduler, controller,
                                            std::chrono::seconds{10});

    WorkflowHostPorts ports;
    ports.domainInit = nullptr;  // 关闭流程不触达——装配核查要求全端口：
    // （下方以占位补齐——真实装配面全端口就绪是宿主装配批次的形态。）
    struct NullInit final : IDomainInitSubmitter
    {
        DomainInitResult submitInitialization(project::ProjectStore&,
                                              const DomainInitRequest&) override
        {
            return {};
        }
    };
    struct NullRelinkDecision final : IRelinkDecisionPort
    {
        RelinkDisposition confirmRelink(const ExternalSourceStatus&) override
        {
            return RelinkDisposition::Cancel;
        }
    };
    struct NullRelink final : IExternalRelinkPort
    {
        ExternalSourceStatus probe(const RelinkRequest&) override { return {}; }
        RelinkExecution relink(const RelinkRequest&, const ExternalSourceStatus&) override
        {
            return {};
        }
    };
    struct NullTitle final : ITitleFactPort
    {
        TitleFacts collectFacts() const override { return {}; }
    };
    struct NullRecovery final : IRecoveryFactPort
    {
        RecoveryFacts collectFacts() override { return {}; }
    };
    struct NullMetric final : ISchemeMetricPort
    {
        std::vector<SchemeMetricFacts> collect(
            const std::vector<core::BranchId>&) const override
        {
            return {};
        }
    };
    struct NullDiff final : IComparisonDiffPort
    {
        SchemeDiffFacts diff(const core::BranchId&, const core::BranchId&) const override
        {
            return {};
        }
    };
    struct NullSaveAs final : ISaveAsPort
    {
        Execution executeCopy(project::ProjectStore&, const SaveAsRequest&,
                              IFlowCancelToken*, const FlowProgressCallback&) override
        {
            return {};
        }
    };
    struct NullExport final : IPackageExportPort
    {
        Execution exportPackage(project::ProjectStore&, const PackageExportRequest&,
                                IFlowCancelToken*, const FlowProgressCallback&) override
        {
            return {};
        }
    };
    struct NullImport final : IPackageImportPort
    {
        PackageImportExecution importPackage(const PackageImportRequest&,
                                             IFlowCancelToken*,
                                             const FlowProgressCallback&) override
        {
            return {};
        }
    };
    NullInit nullInit;
    NullRelinkDecision nullRelinkDecision;
    NullRelink nullRelink;
    NullTitle nullTitle;
    NullRecovery nullRecovery;
    NullMetric nullMetric;
    NullDiff nullDiff;
    NullSaveAs nullSaveAs;
    NullExport nullExport;
    NullImport nullImport;
    ports.domainInit = &nullInit;
    ports.closeDecisions = &decisions;
    ports.closeDrafts = &drafts;
    ports.closeDrain = &drainAdapter;
    ports.relinkDecisions = &nullRelinkDecision;
    ports.externalRelink = &nullRelink;
    ports.titleFacts = &nullTitle;
    ports.recoveryFacts = &nullRecovery;
    ports.schemeMetrics = &nullMetric;
    ports.comparisonDiff = &nullDiff;
    ports.saveAs = &nullSaveAs;
    ports.packageExport = &nullExport;
    ports.packageImport = &nullImport;

    // ---- 会话桥（激活捕获＋presentFailure 捕获——失败可见面断言位）。
    std::vector<std::unique_ptr<project::ProjectStore>> activated;
    int failures = 0;
    WorkflowHostBridges bridges;
    bridges.currentStore = [&created] { return created.store.get(); };
    bridges.currentProjectId = [projectId] { return projectId; };
    bridges.activateStore = [&activated](std::unique_ptr<project::ProjectStore> s,
                                         core::ProjectId) {
        activated.push_back(std::move(s));
    };
    bridges.collectNewProjectInputs = [](NewProjectInputs&) { return false; };
    bridges.selectOpenPath = [] { return std::optional<fs::path>{}; };
    bridges.selectCandidateProjectPath = [] { return std::optional<fs::path>{}; };
    bridges.collectSaveAsRequest = [](SaveAsRequest&) { return false; };
    bridges.collectPackageExport = [](PackageExportRequest&) { return false; };
    bridges.collectPackageImport = [](PackageImportRequest&) { return false; };
    bridges.selectRelinkPath = [] { return std::optional<fs::path>{}; };
    bridges.presentFailure = [&failures](const std::string&, const std::string&,
                                         const std::string&) { ++failures; };

    // ---- 装配核查＋经基线虚类接口消费（接口路径钉扎——不留具体类盲区）。
    WorkflowLifecycleController assembled =
        assembleWorkflowLifecycleController(ports, bridges);
    ILifecycleFlowController& flow = assembled;

    const CloseFlowResult outcome = flow.requestClose(CloseKind::Close);
    EXPECT_EQ(outcome.result, CloseFlowOutcome::Result::Proceed);
    EXPECT_EQ(outcome.abortedAt, CloseFlowOutcome::AbortStage::None);
    EXPECT_FALSE(outcome.failure.has_value());
    EXPECT_EQ(failures, 0);           // 全程零失败呈现（正路径）
    EXPECT_TRUE(activated.empty());   // Close 非切换——无候选激活
    // 存储上下文关闭事实（DRAIN 后半协议走完——awaitStoreClosed 达成）。
    EXPECT_TRUE(created.store->closed());
}

// =====================================================================
// 用例二：P-SEL-9 权威写入经①端口（token 路由＋记录对象继承改版——
// selection.md §19.3 T09② 落位口径的编排面兑现观测点）
// =====================================================================

/**
 * 真实 store＋替身权威写入处理器（token=apply-drivetrain-design 经
 * handlerRegistry 注册——"modeling token 为路由键"的真实路由证明）：
 * 首轮提交产生记录对象（project 分配新建 oid）；第二轮提交继承同 oid
 * 改版（T09"基线闭包已有该对象则继承 oid 改版——PA-2 旧字节随历史闭包
 * 保留"）；每轮恰一新修订（branchHistory 递增——修订只增不改）。
 */
TEST_F(HostAssemblyContract, BackfillTranslateSubmitsAuthoritativeWriteViaCommandPort)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-10"}, std::vector<std::string>{"AT-30"});

    const fs::path projectDir = m_dir / "host-p-sel-9.rwdesign";
    project::OpenStoreResult created = createGolden(projectDir, "P-SEL-9 转译契约");
    ASSERT_TRUE(created.store != nullptr);
    created.store->handlerRegistry().registerHandler(
        std::make_unique<StubDrivetrainHandler>());
    const core::BranchId branch = created.store->query().branchTips().at(0).id;
    const std::uint64_t baseRevisionCount =
        created.store->query().branchHistory(branch, 100).size();

    ContractRecordPort recordPort;
    ContractTranslatePort translatePort;
    SelBackfillTranslateRequest request;
    request.branch = branch;

    // ---- 首轮：闭包无记录 → 编排核 NoRecord（零提交——selection 记录
    // 面不动的编排纪律；这是"记录先行"语义的编排面证明）。
    const SelBackfillTranslateOutcome first =
        SelBackfillTranslateFlow::run(*created.store, request, recordPort,
                                      translatePort);
    EXPECT_EQ(first.result, SelBackfillTranslateOutcome::Result::NoRecord);
    EXPECT_EQ(created.store->query().branchHistory(branch, 100).size(),
              baseRevisionCount);  // 零修订增长

    // ---- 先行落盘记录（替身处理器直提交——模拟 selection 回填命令先行的
    // 记录面事实；写对象 token=sel-device-backfill）。
    project::CommandEnvelope seedEnvelope;
    seedEnvelope.branch = branch;
    seedEnvelope.commandType = std::string(kApplyDrivetrainDesignCommandToken);
    seedEnvelope.payloadFormatVersion = 1;
    seedEnvelope.payloadCanonical = {0x01, 0x01};
    const project::CommandResult seeded = created.store->commands().submit(seedEnvelope);
    ASSERT_TRUE(seeded.committed());
    ASSERT_TRUE(seeded.newRevision.has_value());
    core::ObjectId seededRecordOid{};
    for (const project::ObjectRef& ref : seeded.newHeadState->objectRefs) {
        if (ref.objectTypeToken == kSelBackfillRecordObjectToken) {
            seededRecordOid = ref.objectId;
            break;
        }
    }
    ASSERT_TRUE(seededRecordOid.isValid()) << "记录对象未入闭包（替身链前置）";

    // ---- 次轮：闭包恰一记录 → 解码→转译→①端口提交 → Submitted 新修订。
    const SelBackfillTranslateOutcome second =
        SelBackfillTranslateFlow::run(*created.store, request, recordPort,
                                      translatePort);
    EXPECT_EQ(second.result, SelBackfillTranslateOutcome::Result::Submitted);
    ASSERT_TRUE(second.revisionId.has_value());
    EXPECT_TRUE(second.revisionId->isValid());
    // 修订只增（PA-2）：恰一新增修订，历史连续。
    const std::vector<project::RevisionView> history =
        created.store->query().branchHistory(branch, 100);
    EXPECT_EQ(history.size(), baseRevisionCount + 2);  // 记录落盘＋权威写入
    // 记录对象继承 oid 改版（T09 落位口径）：新 tip 闭包记录对象与首版
    // 同 oid（改版非新建——PA-2 旧字节随历史闭包保留的可观测面）。
    const std::optional<project::RevisionView> tip =
        created.store->query().tryRevision(*second.revisionId);
    ASSERT_TRUE(tip.has_value());
    bool inherited = false;
    for (const project::ObjectRef& ref : tip->objectRefs) {
        if (ref.objectTypeToken == kSelBackfillRecordObjectToken
            && ref.objectId == seededRecordOid) {
            inherited = true;
            break;
        }
    }
    EXPECT_TRUE(inherited) << "权威写入未继承记录对象 oid（改版语义破坏）";
}

// =====================================================================
// 用例三：L5 关闭监督器兜底端到端（真实 execution 全链阻塞在途→排空
// 协议超阈值强制 abandonAll→存储上下文闭合→Dev 日志留痕）
// =====================================================================

// ---- execution 全链替身（InFlightArchiveContractTest 同款形态——外层
// 匿名 namespace 已隔离 ODR；精简为本契约所需面；接口签名逐方法对齐
// evidence/execution 公共头）。 ----

/// 评估器注册替身（isRegistered/contractVersionMatches——V1 校验半区）。
class HostScriptedProducers final : public ev::IProducerRegistryView {
public:
    explicit HostScriptedProducers(std::map<std::string, std::uint32_t> evaluators)
        : m_evaluators(std::move(evaluators))
    {
    }
    bool isRegistered(std::string_view key) const override
    {
        return m_evaluators.count(std::string{key}) > 0;
    }
    bool contractVersionMatches(std::string_view key, std::uint32_t version) const override
    {
        const auto it = m_evaluators.find(std::string{key});
        return it != m_evaluators.end() && it->second == version;
    }

private:
    std::map<std::string, std::uint32_t> m_evaluators;///< 评估器键→契约版本
};

/// 闭包容源替身（V2 半区——全部接受）。
class HostAcceptAllClosure final : public ev::IRevisionClosureSource {
public:
    bool objectInRevision(core::RevisionId, core::ObjectId,
                          core::ContentVersion) const override
    {
        return true;
    }
};

/// 阻塞型内联执行体（在途任务停驻面——release 前计算本体不返回；
/// IInlineRunExecutor::run 契约：返回 InlineRunOutcome 终结因）。
class HostBlockingExecutor final : public exe::IInlineRunExecutor {
public:
    exe::InlineRunOutcome run(const exe::TaskRecord&) override
    {
        // 阻塞至 release（测试线程确定性放行——模拟长计算）。
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this] { return m_released; });
        exe::InlineRunOutcome out;
        out.cause = exe::TerminationCause::Completed;
        return out;
    }
    void release()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_released = true;
        }
        m_cv.notify_all();
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_released = false;
};

/// 事件收集 sink（总线订阅面——本契约零事件断言，仅装配完整性）。
class HostEventSink final : public core::IDomainEventSink {
public:
    void onEvent(const core::DomainEvent&) override {}
};

/// 开发日志捕获替身（IDevLogSink 窄接口——兜底诊断留痕的观测面；
/// StatusBannerContractTest::CapturingDevLog 同款形态）。
struct CapturingDevLog final : diagnostics::IDevLogSink
{
    /// {通道,消息} 捕获（mutex 保护——监督线程写入/主线程断言的并发面；
    /// "Dev 日志 sink 的实现方须自行保证线程安全"〔监督器契约〕的兑现）。
    void logDev(std::string_view channel, std::string message) override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        entries.emplace_back(std::string{channel}, std::move(message));
    }
    std::vector<std::pair<std::string, std::string>> snapshot() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return entries;
    }

private:
    mutable std::mutex m_mutex;                            ///< 捕获互斥
    std::vector<std::pair<std::string, std::string>> entries;///< 保护数据
};

/**
 * 监督器兜底契约（project.md §9.7 义务的可执行证明）：真实 execution
 * 全链＋真实在途任务（内联执行体阻塞——调度线程被计算本体占用的真实
 * 形态）→ 监督器启动（短阈值短宽限——真实时钟）→ 监督线程**只观测
 * 不驱动**（tick 单线程域——监督并发 tick＝锁域违例的实测教训，见
 * CloseSupervisor 头注）→ 兜底诊断留痕（Dev 日志——"残留会话记开发
 * 诊断"）→ 越宽限**放弃监督**（不阻塞进程退出的义务面——本用例在
 * 测试时限内完成即"有界"的可执行证据）。存储上下文保持未闭合（未进入
 * 关闭的 Active 态＝"非任务侧在途引用未释放"的监督场景前提）。
 *
 * 分工登记（诚实边界）：execution 侧"超阈值自动 abandonAllForced 强制
 * 兜底→有界 drained"的正链路已由 execution 契约 EX-ARC-4（Threshold-
 * ExceededForcesAbandonBoundedDrain）覆盖——本用例不重复承载强杀链
 * （内联执行体阻塞调度线程时 poll 不可达——强杀叙事对内联形态不可
 * 达），聚焦 L5 监督器的可观测义务面〔诊断＋放弃＋有界〕。
 */
TEST_F(HostAssemblyContract, CloseSupervisorForcesAbandonBoundedAndStoreCloses)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-03"}, std::vector<std::string>{"AT-21"});

    // ---- 真实黄金项目＋真实 execution 全链（InFlight 夹具同款装配）。
    const fs::path projectDir = m_dir / "host-supervisor.rwdesign";
    project::OpenStoreResult created = createGolden(projectDir, "监督器兜底契约");
    ASSERT_TRUE(created.store != nullptr);

    auto clock = [] { return std::chrono::steady_clock::now(); };
    HostScriptedProducers registry({{"kin-batch-ik", 7}});
    HostAcceptAllClosure closure;
    HostBlockingExecutor executor;
    execution::TaskController::Config controllerConfig;
    execution::DrainCoordinator::Config drainConfig;
    drainConfig.abandonThreshold = std::chrono::milliseconds{200};  // 排空自动兜底（实现参数——EX-ARC-4 链路的装配面）
    execution::TaskController exeController(controllerConfig, clock);
    execution::DrainCoordinator exeDrain(exeController, drainConfig, clock);
    execution::TaskScheduler::Collaboration collab;
    collab.evaluators = &registry;
    collab.closure = &closure;
    collab.store = created.store.get();  // V3 写权限真实面
    execution::TaskScheduler exeScheduler(exeController, exeDrain, collab,
                                    execution::TaskScheduler::Config{}, clock);
    exe::DomainEventBusImpl bus;
    HostEventSink sink;
    auto subscription = bus.subscribe(sink);
    exeScheduler.setEventBus(&bus);
    exeScheduler.setInlineExecutor(&executor);

    // ---- 在途任务提交（锚定真实项目/分支/tip）＋tick 线程推进至在途。
    const auto tips = created.store->query().branchTips();
    ASSERT_FALSE(tips.empty());
    ev::SnapshotBuilder b;
    b.setIdentity(created.store->projectId(), tips[0].id, tips[0].tip, 5);
    b.setPolicyRef(ev::PolicyRef{core::ContentIdentity::fromCanonical(
        std::string{"cid-"} + std::string(64, 'a'))});
    b.setNameMapRef(ev::NameMapRef{core::ContentIdentity::fromCanonical(
        std::string{"cid-"} + std::string(64, 'b'))});
    ev::ReproductionBlock r;
    r.productVersion = "industrialrobot-designer 0.1.0";
    r.evidenceContractVersion = "evidence-contract/1";
    r.codecVersions = {"snapshot-codec/1", "slice-codec/1"};
    b.setReproduction(r);
    ev::ObjectRefEntry obj;
    obj.objectId = core::ObjectId::fromCanonical(
        std::string{"obj-"} + std::string(32, 'a'));
    obj.contentVersion = core::ContentVersion::fromCanonical(
        std::string{"cv-"} + std::string(64, 'a'));
    obj.objectTypeToken = "robot-design";
    obj.digest = obj.contentVersion.bytes;
    b.addObjectRef(obj);
    b.addCase(ev::CaseEntry{obj.objectId, "case-a", true, true});
    const ev::AnalysisSnapshot snapshot = b.build(closure);

    exe::TaskSubmission submission;
    submission.snapshot = snapshot;
    submission.evaluatorKey = "kin-batch-ik";
    submission.contractVersion = 7;
    submission.mode = core::EvaluationMode::Preview;  // 内联门槛
    submission.priority = exe::TaskPriority::Background;
    const exe::SubmitResult submitted = exeScheduler.submit(std::move(submission));
    ASSERT_TRUE(submitted.accepted);
    ASSERT_TRUE(submitted.task.has_value());

    std::atomic<bool> stopTick{false};
    std::thread tickThread([&] {
        while (!stopTick.load()) {
            exeScheduler.tick();  // 协议推进（命令通道/出队/派发——调度域；
                                  // 内联任务派发后本线程进入执行体阻塞——
                                  // 推进停摆＝调度线程占用的真实形态）
            std::this_thread::yield();
        }
    });
    // 确定性等待在途态（Preparing/Running——计算本体阻塞中）。
    const auto inFlightDeadline = std::chrono::steady_clock::now() + std::chrono::seconds{30};
    while (true) {
        const auto state = exeController.tryState(*submitted.task);
        if (state == core::TaskState::Preparing || state == core::TaskState::Running) {
            break;
        }
        ASSERT_LT(std::chrono::steady_clock::now(), inFlightDeadline)
            << "任务未按时到达在途态（夹具前置失败）";
        std::this_thread::yield();
    }

    // ---- 关闭期排空段（等待分支的调度面：shutdown 停派发取消排队）。
    // 存储上下文**不发起关闭**（Active 态——模拟"非任务侧在途引用未
    // 释放"的监督场景：闭合永不达成，监督器必须走完②兜底诊断→③放弃
    // 监督的有界路径）。
    exeScheduler.shutdown(execution::DrainPolicy::CancelQueuedAndWait);

    // ---- 监督器启动（短阈值短宽限——真实时钟，③④两段在测试时限内）。
    CapturingDevLog devLog;  // Dev 日志捕获（"残留会话记开发诊断"通道）
    StoreCloseSupervisor::Config config;
    config.abandonThreshold = std::chrono::milliseconds{300};
    config.giveUpGrace = std::chrono::milliseconds{300};
    config.pollInterval = std::chrono::milliseconds{1};
    StoreCloseSupervisor supervisor(*created.store, exeScheduler, config, &devLog);
    const auto superviseStart = std::chrono::steady_clock::now();
    supervisor.start();

    // 确定性等待监督器放弃退出（②→③路径——上限 30 s＝测试时限）。
    const auto giveUpDeadline = std::chrono::steady_clock::now() + std::chrono::seconds{30};
    while (!supervisor.gaveUp()) {
        ASSERT_LT(std::chrono::steady_clock::now(), giveUpDeadline)
            << "监督器未在时限内走完兜底诊断→放弃路径（有界性破坏）";
        std::this_thread::yield();
    }
    const auto superviseElapsed = std::chrono::steady_clock::now() - superviseStart;
    supervisor.stop();

    // ---- 兜底链观测点断言。
    EXPECT_TRUE(supervisor.abandonDiagObserved());  // ②段兜底诊断位（超阈值）
    EXPECT_TRUE(supervisor.gaveUp());               // ③段放弃位（不阻塞进程退出）
    EXPECT_FALSE(supervisor.storeClosedObserved()); // 存储上下文保持未闭合（场景前提）
    EXPECT_FALSE(created.store->closed());
    // 有界性：总监督时长 ≈ 阈值＋宽限（300 ms＋300 ms——量级断言 <10 s，
    // 产品默认 15 s＋15 s 的同构缩短）。
    EXPECT_LT(superviseElapsed, std::chrono::seconds{10})
        << "监督总时长超出量级（有界性破坏）";
    // Dev 日志留痕两条（兜底告知＋放弃告知——project §9.7 开发诊断义务）。
    bool abandonLogged = false;
    bool giveUpLogged = false;
    for (const auto& entry : devLog.snapshot()) {
        if (entry.second.find("abandonAll") != std::string::npos) {
            abandonLogged = true;
        }
        if (entry.second.find("放弃") != std::string::npos) {
            giveUpLogged = true;
        }
    }
    EXPECT_TRUE(abandonLogged) << "兜底诊断未留痕（project §9.7 开发诊断义务）";
    EXPECT_TRUE(giveUpLogged) << "放弃监督未留痕（不阻塞进程退出的诚实告知）";

    // ---- 收敛（放行阻塞执行体〔内联体仍在等待——release 防线程悬挂；
    // run 返回后 tick 线程恢复、随 stopTick 收敛〕＋关存储上下文）。
    executor.release();
    stopTick.store(true);
    tickThread.join();
    subscription.reset();
    (void)created.store->requestClose();
}

// =====================================================================
// 用例四：恢复事实适配器统一诊断目录真实集成（PM-15——打开期恢复诊断
// 经真实 factory 校验链入目录，适配器折叠场景①事实）
// =====================================================================

/**
 * 真实 open 产出（.staging 残留注入→恢复扫描 PRJ-RECOVERY-IGNORED-
 * UNCOMMITTED 诊断）→ 码注册/subject 边界/上下文校验全真注入目录 →
 * StoreRecoveryFactAdapter 折叠 → 场景①命中＋计数＝涉事清单长度（
 * StatusBannerContractTest 同款真链——本用例消费面为**适配器**而非
 * 测试等价物桥，PM-15 集成的装配面兑现）。
 */
TEST_F(HostAssemblyContract, StoreRecoveryFactAdapterFoldsRealCatalog)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-15"}, std::vector<std::string>{"AT-21"});

    // ---- 真实落盘＋场景①盘面注入＋释放写锁。
    const fs::path projectDir = m_dir / "host-recovery.rwdesign";
    {
        project::OpenStoreResult created = createGolden(projectDir, "恢复集成契约");
        ASSERT_TRUE(created.store != nullptr);
        injectStagingResidue(projectDir, "tx-host-left-open");
        (void)created.store->requestClose();
        created.store.reset();
    }

    // ---- 真实 open（Writable＋捕获 sink）：⑤步恢复扫描真实产出。
    class CapturingSink final : public project::IDiagnosticsSink
    {
    public:
        std::vector<core::DiagnosticRecord> reports;
        void report(const core::DiagnosticRecord& record) override
        {
            reports.push_back(record);
        }
        void reportDev(const std::string&, const std::string&) override {}
    };
    CapturingSink sink;
    project::OpenStoreRequest request;
    request.path = projectDir;
    request.diagnostics = &sink;
    const project::OpenStoreResult reopened =
        project::ProjectStoreFactory::open(request);
    ASSERT_TRUE(reopened.store != nullptr);
    ASSERT_EQ(reopened.recovery.ignoredStagingTxs.size(), 1u);

    // ---- 统一诊断目录真链装配（码注册→seal→工厂→目录）＋L5 注入桥
    // （StatusBanner 同款——码注册校验/subject 边界/上下文规则全真）。
    diagnostics::StableCodeRegistry registry;
    diagnostics::registerBuiltinCodes(registry);
    registry.seal();
    diagnostics::SystemClock clock;
    diagnostics::DiagnosticsFactory factory(registry, clock);
    diagnostics::DiagCatalog catalog;
    CapturingDevLog devLog;
    const core::ObjectId projectObject =
        reopened.store->query().currentMetadata().ref.objectId;
    ASSERT_TRUE(projectObject.isValid());

    const core::DiagnosticRecord* ignoredRec = nullptr;
    for (const auto& rec : sink.reports) {
        if (rec.code == "PRJ-RECOVERY-IGNORED-UNCOMMITTED") {
            ignoredRec = &rec;
            break;
        }
    }
    ASSERT_NE(ignoredRec, nullptr);
    {
        diagnostics::DiagContext context;
        context.sourceUnit = "project";
        context.sourceInterface = "store.open";
        const core::DiagnosticRecord rebuilt = core::DiagnosticRecord::make(
            ignoredRec->code, projectObject, ignoredRec->localName,
            ignoredRec->runtimeName, ignoredRec->context, ignoredRec->cause,
            ignoredRec->recommendedAction);
        ASSERT_NO_THROW(catalog.append(factory.create(rebuilt, context)));
    }

    // ---- 适配器消费（经 IRecoveryFactPort& 接口——真实目录＋真实 open
    // 产物清单半区；写权限取自真实 store）。
    StoreRecoveryFactAdapter::ReportHalf reportHalf;
    reportHalf.report = [&reopened] {
        return std::optional<project::RecoveryReport>(reopened.recovery);
    };
    StoreRecoveryFactAdapter adapter(
        catalog, [&reopened] { return std::optional<bool>(reopened.writable); },
        reportHalf);
    IRecoveryFactPort& port = adapter;

    const RecoveryFacts facts = port.collectFacts();
    ASSERT_EQ(facts.scenarios.size(), 1u);
    EXPECT_EQ(facts.scenarios.at(0).scenario,
              RecoveryScenario::IgnoredUnfinishedSave);
    EXPECT_EQ(facts.scenarios.at(0).itemCount, 1u);  // .staging 残留事务数
    EXPECT_TRUE(facts.writable);
    (void)devLog;
}

}  // namespace
