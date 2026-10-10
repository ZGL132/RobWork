/**
 * @file   HostAdapterTest.cpp
 * @brief  宿主装配面模型测试（ASM-WF 宿主收口批次——装配核查 F-536、
 *         九端口适配器、ILifecycleFlowController 宿主实现、L5 关闭监督器、
 *         P-SEL-9 转译编排核的模型半区）。
 *
 * 设计依据：
 *   - units/workflow.md §10.2 v0.5~v1.2 端口接缝登记（"实现归 L5 装配层"
 *     ——本测试钉扎适配器实现与装配核查）、§7.3（A7 兜底——project §9.7
 *     L5 关闭控制器义务）、selection.md §19.1 P-SEL-9（转译编排）
 *   - 测试纪律（AGENTS §2.7/§4.2）：接口消费路径钉扎——适配器一律经
 *     接口引用虚派发消费（不留只测具体类的盲区——WP-20-T03 教训）；
 *     用例名带需求/登记追溯；fail-fast 反例面全覆盖（装配核查逐端口）
 *   - 模型面口径：编排核/适配器逻辑用脚本缝替身直调；真实 store 半区
 *     用真实落盘临时目录（SettingsTest 先例）——契约半区（端到端兜底
 *     链/P-SEL-9 token 路由）在 HostAssemblyContractTest
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求追溯登记

#include <sdurws/ird/project/CommandService.hpp> // CommandEnvelope/ICommandHandler（P-SEL-9 替身处理器）
#include <sdurws/ird/diagnostics/Catalog.hpp>    // DiagCatalog（恢复事实桥对端——真实目录）
#include <sdurws/ird/workflow/BackfillTranslate.hpp>
#include <sdurws/ird/workflow/CloseSupervisor.hpp>
#include <sdurws/ird/workflow/Lifecycle.hpp>

#include "plugin/WorkflowHostAdapters.hpp"  // 被测面（plugin/ 装配面——同单元 PRIVATE include，R-2 不外溢）

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

using namespace sdurws::ird;
using namespace sdurws::ird::workflow;

// =====================================================================
// 通用替身：空实现端口（装配核查反例面的填充物——只保虚析构合法性）
// =====================================================================

/// 全空端口集的填充替身（装配核查用——指针合法但功能不消费）。
struct StubPorts
{
    struct Init final : IDomainInitSubmitter
    {
        DomainInitResult submitInitialization(project::ProjectStore&,
                                              const DomainInitRequest&) override
        {
            return {};
        }
    };
    struct Decisions final : ICloseDecisionPort
    {
        DraftDisposition collectDraftDisposition(const CloseDialogData&) override
        {
            return DraftDisposition::Cancel;
        }
        RunningTaskDecision collectRunningTaskDecision(const CloseDialogData&) override
        {
            return RunningTaskDecision::CancelFlow;
        }
    };
    struct Drafts final : ICloseDraftPort
    {
        std::vector<std::string> unappliedDraftModules() override { return {}; }
        bool saveDrafts() override { return true; }
        bool discardDrafts() override { return true; }
    };
    struct Drain final : ICloseDrainPort
    {
        bool hasActiveTask(core::ProjectId) override { return false; }
        std::vector<core::TaskState> taskStates(core::ProjectId) override { return {}; }
        bool waitDrain() override { return true; }
        bool cooperativeCancel() override { return true; }
    };
    struct RelinkDecision final : IRelinkDecisionPort
    {
        RelinkDisposition confirmRelink(const ExternalSourceStatus&) override
        {
            return RelinkDisposition::Cancel;
        }
    };
    struct Relink final : IExternalRelinkPort
    {
        ExternalSourceStatus probe(const RelinkRequest&) override { return {}; }
        RelinkExecution relink(const RelinkRequest&, const ExternalSourceStatus&) override
        {
            return {};
        }
    };
    struct Title final : ITitleFactPort
    {
        TitleFacts collectFacts() const override { return {}; }
    };
    struct Recovery final : IRecoveryFactPort
    {
        RecoveryFacts collectFacts() override { return {}; }
    };
    struct Metric final : ISchemeMetricPort
    {
        std::vector<SchemeMetricFacts> collect(
            const std::vector<core::BranchId>&) const override
        {
            return {};
        }
    };
    struct Diff final : IComparisonDiffPort
    {
        SchemeDiffFacts diff(const core::BranchId&, const core::BranchId&) const override
        {
            return {};
        }
    };
    struct SaveAs final : ISaveAsPort
    {
        Execution executeCopy(project::ProjectStore&, const SaveAsRequest&,
                              IFlowCancelToken*, const FlowProgressCallback&) override
        {
            return {};
        }
    };
    struct Export final : IPackageExportPort
    {
        Execution exportPackage(project::ProjectStore&, const PackageExportRequest&,
                                IFlowCancelToken*, const FlowProgressCallback&) override
        {
            return {};
        }
    };
    struct Import final : IPackageImportPort
    {
        PackageImportExecution importPackage(const PackageImportRequest&,
                                             IFlowCancelToken*,
                                             const FlowProgressCallback&) override
        {
            return {};
        }
    };
};

/// 全端口就绪的集合（装配核查正面与控制器测试的底座——成员序与
/// WorkflowHostPorts 字段序一致）。
struct ReadyHost
{
    StubPorts::Init init;
    StubPorts::Decisions decisions;
    StubPorts::Drafts drafts;
    StubPorts::Drain drain;
    StubPorts::RelinkDecision relinkDecision;
    StubPorts::Relink relink;
    StubPorts::Title title;
    StubPorts::Recovery recovery;
    StubPorts::Metric metric;
    StubPorts::Diff diff;
    StubPorts::SaveAs saveAs;
    StubPorts::Export packageExport;
    StubPorts::Import packageImport;

    /// 端口集装配（全非空——正面底座）。
    WorkflowHostPorts ports()
    {
        WorkflowHostPorts p;
        p.domainInit = &init;
        p.closeDecisions = &decisions;
        p.closeDrafts = &drafts;
        p.closeDrain = &drain;
        p.relinkDecisions = &relinkDecision;
        p.externalRelink = &relink;
        p.titleFacts = &title;
        p.recoveryFacts = &recovery;
        p.schemeMetrics = &metric;
        p.comparisonDiff = &diff;
        p.saveAs = &saveAs;
        p.packageExport = &packageExport;
        p.packageImport = &packageImport;
        return p;
    }
};

/// 必填缝就绪的宿主桥（脚本录制面——测试断言经捕获字段）。
struct ReadyBridges
{
    std::optional<fs::path> openPath;         ///< selectOpenPath 脚本值
    bool collectNewOk = true;                 ///< collectNewProjectInputs 脚本值
    bool collectSaveAsOk = true;
    bool collectExportOk = true;
    bool collectImportOk = true;
    std::optional<fs::path> candidatePath;    ///< 切换候选脚本值
    std::optional<fs::path> relinkPath;       ///< 重定向脚本值（nullopt=取消）
    project::ProjectStore* store = nullptr;   ///< currentStore 脚本值（可空）
    core::ProjectId projectId{};              ///< currentProjectId 脚本值

    int failureCount = 0;                     ///< presentFailure 捕获计数
    std::string lastFailureContext;           ///< 最近失败上下文（UX-03 半区一）
    int noticeCount = 0;                      ///< presentNotice 捕获计数
    std::string lastNotice;

    // ---- 包导入收集缝观测（ASM-UI 修复③钉扎——收集恰一次＋预填透传）----
    int collectImportCalls = 0;               ///< collectPackageImport 调用计数
    std::vector<fs::path> collectedImportPacks;///< 每次调用收到的请求预填值
                                               ///<   （序＝调用序；预填丢失
                                               ///<   即空路径——断言面）

    WorkflowHostBridges bridges()
    {
        WorkflowHostBridges b;
        b.currentStore = [this] { return store; };
        b.currentProjectId = [this] { return projectId; };
        b.activateStore = [this](std::unique_ptr<project::ProjectStore> s,
                                 core::ProjectId id) {
            activatedStores.push_back(std::move(s));
            activatedIds.push_back(id);
            store = activatedStores.back().get();  // 模拟宿主会话接管
            projectId = id;
        };
        b.collectNewProjectInputs = [this](NewProjectInputs& inputs) {
            inputs.displayName = "适配器测试项目";
            inputs.directory = workDir / "adapter-proj.rwdesign";
            return collectNewOk;
        };
        b.selectOpenPath = [this] { return openPath; };
        b.selectCandidateProjectPath = [this] { return candidatePath; };
        b.collectSaveAsRequest = [this](SaveAsRequest& r) {
            r.targetDir = workDir / "save-as-target.rwdesign";
            return collectSaveAsOk;
        };
        b.collectPackageExport = [this](PackageExportRequest& r) {
            r.targetFile = workDir / "out.rwpack";
            return collectExportOk;
        };
        b.collectPackageImport = [this](PackageImportRequest& r) {
            ++collectImportCalls;                 // 观测：调用次数（修复③——恰一次）
            collectedImportPacks.push_back(r.packFile);  // 观测：预填透传
            if (r.packFile.empty()) {
                r.packFile = workDir / "in.rwpack";
            }
            r.targetDir = workDir / "import-target.rwdesign";
            return collectImportOk;
        };
        b.selectRelinkPath = [this] { return relinkPath; };
        b.presentFailure = [this](const std::string& context, const std::string&,
                                  const std::string&) {
            ++failureCount;
            lastFailureContext = context;
        };
        b.presentNotice = [this](const std::string& summary) {
            ++noticeCount;
            lastNotice = summary;
        };
        return b;
    }

    std::vector<std::unique_ptr<project::ProjectStore>> activatedStores;///< 激活捕获
    std::vector<core::ProjectId> activatedIds;                          ///< 激活身份
    fs::path workDir;  ///< 用例工作目录（夹具注入）
};

// =====================================================================
// 夹具：临时目录＋真实 blank 项目（store 半区真实桥的底座）
// =====================================================================

class HostAdapter : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        std::error_code ec;
        s_base = fs::temp_directory_path(ec) / "ird_wf_host_adapter_test";
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
        workDir = s_base / ("case" + std::to_string(++s_case));
        std::error_code ec;
        fs::create_directories(workDir, ec);
        ASSERT_FALSE(ec);
    }

    /// 真实 blank 项目落盘（空白来源——零领域提交，store 即 r0 骨架）。
    std::unique_ptr<project::ProjectStore> makeBlankStore(const std::string& name)
    {
        NewProjectInputs inputs;
        inputs.displayName = name;
        inputs.directory = workDir / (name + ".rwdesign");
        NewProjectOutcome outcome =
            NewProjectWizardFlow::commit(inputs, nullptr);
        EXPECT_TRUE(outcome.created) << "blank 建盘失败（夹具前置）: " << name;
        return std::move(outcome.store);
    }

    static fs::path s_base;
    static int s_case;
    fs::path workDir;
};

fs::path HostAdapter::s_base;
int HostAdapter::s_case = 0;

}  // namespace

// =====================================================================
// 装配核查（F-536 同族——端口/必填缝缺失即 fail-fast 且点名）
// =====================================================================

/**
 * 装配核查正面：13 端口＋11 必填缝全部就绪 → 控制器就绪（经
 * ILifecycleFlowController& 接口持有——基线虚类的消费形态即本测试形态）。
 */
TEST_F(HostAdapter, AssembleReadyHostReturnsController)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-04"}, std::vector<std::string>{});

    ReadyHost host;
    ReadyBridges bridgeState;
    bridgeState.workDir = workDir;
    WorkflowLifecycleController controller =
        assembleWorkflowLifecycleController(host.ports(), bridgeState.bridges());
    ILifecycleFlowController& flow = controller;  // 接口消费形态（虚派发）
    (void)flow;
    SUCCEED();
}

/**
 * 装配核查反例：任一端口缺失即 WorkflowError 且消息点名端口名——
 * 逐端口参数化断言（F-536 同族：端口无实现不允许静默空转）。
 */
TEST_F(HostAdapter, AssembleFailsFastNamingEachMissingPort)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-04"}, std::vector<std::string>{});

    // 13 端口的清空器与点名子串（错误消息必须含端口名——修复装配而非
    // 捕获后继续的可定位性）。
    const std::vector<std::pair<std::string, std::function<void(WorkflowHostPorts&)>>>
        clearing = {
            {"IDomainInitSubmitter", [](WorkflowHostPorts& p) { p.domainInit = nullptr; }},
            {"ICloseDecisionPort", [](WorkflowHostPorts& p) { p.closeDecisions = nullptr; }},
            {"ICloseDraftPort", [](WorkflowHostPorts& p) { p.closeDrafts = nullptr; }},
            {"ICloseDrainPort", [](WorkflowHostPorts& p) { p.closeDrain = nullptr; }},
            {"IRelinkDecisionPort", [](WorkflowHostPorts& p) { p.relinkDecisions = nullptr; }},
            {"IExternalRelinkPort", [](WorkflowHostPorts& p) { p.externalRelink = nullptr; }},
            {"ITitleFactPort", [](WorkflowHostPorts& p) { p.titleFacts = nullptr; }},
            {"IRecoveryFactPort", [](WorkflowHostPorts& p) { p.recoveryFacts = nullptr; }},
            {"ISchemeMetricPort", [](WorkflowHostPorts& p) { p.schemeMetrics = nullptr; }},
            {"IComparisonDiffPort", [](WorkflowHostPorts& p) { p.comparisonDiff = nullptr; }},
            {"ISaveAsPort", [](WorkflowHostPorts& p) { p.saveAs = nullptr; }},
            {"IPackageExportPort", [](WorkflowHostPorts& p) { p.packageExport = nullptr; }},
            {"IPackageImportPort", [](WorkflowHostPorts& p) { p.packageImport = nullptr; }},
        };

    for (const auto& [name, clear] : clearing) {
        ReadyHost host;
        ReadyBridges bridgeState;
        bridgeState.workDir = workDir;
        WorkflowHostPorts ports = host.ports();
        clear(ports);  // 清空当前端口（其余保持就绪——单变量隔离）
        try {
            (void)assembleWorkflowLifecycleController(ports, bridgeState.bridges());
            FAIL() << "缺失端口 " << name << " 未被装配核查拒绝";
        } catch (const WorkflowError& e) {
            EXPECT_NE(std::string(e.what()).find(name), std::string::npos)
                << "装配核查消息未点名缺失端口 " << name << ": " << e.what();
        }
    }
}

/**
 * 装配核查反例：11 必填会话桥逐一缺失即 WorkflowError 且消息点名桥名
 * （ASM-UI 收口——asm-wf 验收建议级②"抽样式两代表桥"补全为逐一参数化，
 * 与端口侧 13/13 逐一形态对齐）。逐桥语义：
 *   - currentStore/currentProjectId/activateStore＝会话状态桥（无项目
 *     会话的入口拒绝面／激活身份面）；
 *   - collect* 五缝＋select* 两缝＝收集缝（缺失＝入口不可用空转）；
 *   - presentFailure＝呈现缝（缺失＝失败静默伪造成功——F-565/566 失败
 *     可见面强制，装配核查最关键项）。
 * 可空缝 presentNotice 不在清单（缺省合法——类注）。
 */
TEST_F(HostAdapter, AssembleFailsFastOnMissingRequiredBridges)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-04", "PM-03"},
                  std::vector<std::string>{});

    // 11 必填桥的清空器与点名子串（错误消息必须含桥名——F-536 修复装配
    // 而非捕获后继续的可定位性；单变量隔离：每次只清空当前桥，其余保持
    // 就绪——与 13 端口参数化用例同型）。
    const std::vector<std::pair<std::string,
                                std::function<void(WorkflowHostBridges&)>>>
        clearing = {
            {"currentStore",
             [](WorkflowHostBridges& b) { b.currentStore = nullptr; }},
            {"currentProjectId",
             [](WorkflowHostBridges& b) { b.currentProjectId = nullptr; }},
            {"activateStore",
             [](WorkflowHostBridges& b) { b.activateStore = nullptr; }},
            {"collectNewProjectInputs",
             [](WorkflowHostBridges& b) { b.collectNewProjectInputs = nullptr; }},
            {"selectOpenPath",
             [](WorkflowHostBridges& b) { b.selectOpenPath = nullptr; }},
            {"selectCandidateProjectPath",
             [](WorkflowHostBridges& b) {
                 b.selectCandidateProjectPath = nullptr;
             }},
            {"collectSaveAsRequest",
             [](WorkflowHostBridges& b) { b.collectSaveAsRequest = nullptr; }},
            {"collectPackageExport",
             [](WorkflowHostBridges& b) { b.collectPackageExport = nullptr; }},
            {"collectPackageImport",
             [](WorkflowHostBridges& b) { b.collectPackageImport = nullptr; }},
            {"selectRelinkPath",
             [](WorkflowHostBridges& b) { b.selectRelinkPath = nullptr; }},
            {"presentFailure",
             [](WorkflowHostBridges& b) { b.presentFailure = nullptr; }},
        };
    ASSERT_EQ(clearing.size(), std::size_t{11})
        << "必填桥清单与装配核查清单（11 项）失配——同步义务";

    for (const auto& [name, clear] : clearing) {
        ReadyHost host;
        ReadyBridges bridgeState;
        bridgeState.workDir = workDir;
        WorkflowHostBridges bridges = bridgeState.bridges();
        clear(bridges);  // 清空当前桥（其余保持就绪——单变量隔离）
        try {
            (void)assembleWorkflowLifecycleController(host.ports(), bridges);
            FAIL() << "缺失必填桥 " << name << " 未被装配核查拒绝";
        } catch (const WorkflowError& e) {
            EXPECT_NE(std::string(e.what()).find(name), std::string::npos)
                << "装配核查消息未点名缺失桥 " << name << ": " << e.what();
        }
    }
}

// =====================================================================
// 适配器：缝拒绝面与委托透传（经接口引用虚派发消费）
// =====================================================================

/**
 * DomainInitSubmitterAdapter：空缝 fail-fast／组装 nullopt＝committed=false
 * ／组装异常＝失败半区——经 IDomainInitSubmitter& 接口消费。
 */
TEST_F(HostAdapter, DomainInitSubmitterAdapterSeamContracts)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-01"}, std::vector<std::string>{"AT-20"});

    // 空缝＝装配缺陷 fail-fast（F-536——静默失败会让向导永远失败无解释）。
    {
        DomainInitSubmitterAdapter adapter(nullptr);
        IDomainInitSubmitter& port = adapter;
        auto store = makeBlankStore("di-init-empty");
        ASSERT_TRUE(store != nullptr);
        EXPECT_THROW((void)port.submitInitialization(*store, DomainInitRequest{}),
                     WorkflowError);
    }
    // 组装缝返回 nullopt＝数据侧拒绝——committed=false＋UX-03 半区（零吞错）。
    {
        DomainInitSubmitterAdapter adapter(
            [](project::ProjectStore&, const DomainInitRequest&)
                -> std::optional<project::CommandEnvelope> { return std::nullopt; });
        IDomainInitSubmitter& port = adapter;
        auto store = makeBlankStore("di-init-null");
        ASSERT_TRUE(store != nullptr);
        const DomainInitResult result =
            port.submitInitialization(*store, DomainInitRequest{});
        EXPECT_FALSE(result.committed);
        EXPECT_FALSE(result.baselineRevision.has_value());
        EXPECT_FALSE(result.causeText.empty());
    }
}

/**
 * DialogCloseDecisionAdapter：双决策透传＋空缝 fail-fast——经
 * ICloseDecisionPort& 接口消费（决策词表值中转零改写）。
 */
TEST_F(HostAdapter, DialogCloseDecisionAdapterDelegatesAndFailsFast)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-03"}, std::vector<std::string>{"AT-20"});

    DialogCloseDecisionAdapter adapter(
        [](const CloseDialogData&) { return DraftDisposition::Discard; },
        [](const CloseDialogData&) { return RunningTaskDecision::Wait; });
    ICloseDecisionPort& port = adapter;

    CloseDialogData data;
    data.kind = CloseKind::Close;
    data.scenarioKey = closeScenarioKey(CloseKind::Close);
    EXPECT_EQ(port.collectDraftDisposition(data), DraftDisposition::Discard);
    EXPECT_EQ(port.collectRunningTaskDecision(data), RunningTaskDecision::Wait);

    // 空缝＝装配缺陷 fail-fast（决策点没有"默认继续"——绕过统一确认）。
    DialogCloseDecisionAdapter empty(nullptr, nullptr);
    ICloseDecisionPort& emptyPort = empty;
    EXPECT_THROW((void)emptyPort.collectDraftDisposition(data), WorkflowError);
    EXPECT_THROW((void)emptyPort.collectRunningTaskDecision(data), WorkflowError);
}

/**
 * IoExternalRelinkAdapter／DelegatingSchemeMetricPort／三执行端口委托
 * 适配器：空缝 fail-fast 统一面——经各自接口引用消费。
 */
TEST_F(HostAdapter, DelegatingPortsFailFastOnMissingSeam)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-09"}, std::vector<std::string>{});

    // 重关联双缝（v0.9——检测/提交）。
    IoExternalRelinkAdapter relinkEmpty(nullptr, nullptr);
    IExternalRelinkPort& relinkPort = relinkEmpty;
    RelinkRequest relinkRequest;
    relinkRequest.resource = core::ObjectId::generate();
    EXPECT_THROW((void)relinkPort.probe(relinkRequest), WorkflowError);
    EXPECT_THROW((void)relinkPort.relink(relinkRequest, ExternalSourceStatus{}),
                 WorkflowError);

    // 指标投影缝（v1.2——§8.1 零指标口径红线的宿主装配位）。
    DelegatingSchemeMetricPort metricEmpty(nullptr);
    ISchemeMetricPort& metricPort = metricEmpty;
    EXPECT_THROW((void)metricPort.collect({core::BranchId::generate(),
                                           core::BranchId::generate()}),
                 WorkflowError);

    // 三执行端口缝（v0.7/v0.8 另存/包面）。
    DelegatingSaveAsPort saveAsEmpty(nullptr);
    ISaveAsPort& saveAsPort = saveAsEmpty;
    auto store = makeBlankStore("seam-store");
    ASSERT_TRUE(store != nullptr);
    EXPECT_THROW((void)saveAsPort.executeCopy(*store, SaveAsRequest{}, nullptr, {}),
                 WorkflowError);

    DelegatingPackageExportPort exportEmpty(nullptr);
    IPackageExportPort& exportPort = exportEmpty;
    EXPECT_THROW((void)exportPort.exportPackage(*store, PackageExportRequest{},
                                                nullptr, {}),
                 WorkflowError);

    DelegatingPackageImportPort importEmpty(nullptr);
    IPackageImportPort& importPort = importEmpty;
    EXPECT_THROW((void)importPort.importPackage(PackageImportRequest{}, nullptr, {}),
                 WorkflowError);
}

/**
 * UnavailableComparisonDiffPort：缺省恒 available=false＋entries 空
 * （端口契约的诚实降级——P-OPT-8 不虚构）；注入后委托透传——经
 * IComparisonDiffPort& 接口消费。
 */
TEST_F(HostAdapter, UnavailableDiffPortDegradesHonestlyThenDelegates)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-13"}, std::vector<std::string>{"AT-12"});

    // 缺省形态：通道未装配＝诚实降级（available=false——编排核补降级警告）。
    UnavailableComparisonDiffPort degraded;
    IComparisonDiffPort& degradedPort = degraded;
    const core::BranchId base = core::BranchId::generate();
    const core::BranchId cand = core::BranchId::generate();
    const SchemeDiffFacts facts = degradedPort.diff(base, cand);
    EXPECT_FALSE(facts.available);
    EXPECT_TRUE(facts.entries.empty());

    // 注入形态：委托透传（宿主装配后翻转——消费方式不变）。
    int calls = 0;
    UnavailableComparisonDiffPort delegating(
        [&](const core::BranchId&, const core::BranchId&) {
            ++calls;
            SchemeDiffFacts out;
            out.available = true;
            return out;
        });
    IComparisonDiffPort& delegatingPort = delegating;
    EXPECT_TRUE(delegatingPort.diff(base, cand).available);
    EXPECT_EQ(calls, 1);
}

// =====================================================================
// 适配器：store 半区真实桥（真实落盘 store——drafts/title 桥）
// =====================================================================

/**
 * StoreCloseDraftAdapter：磁盘半源真实桥（store.query().listDrafts）∪
 * 会话半区合并去重；空会话缝 fail-fast——经 ICloseDraftPort& 接口消费。
 */
TEST_F(HostAdapter, StoreCloseDraftAdapterMergesDiskAndSessionHalves)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-03"}, std::vector<std::string>{"AT-20"});

    auto store = makeBlankStore("draft-merge");
    ASSERT_TRUE(store != nullptr);

    StoreCloseDraftAdapter::SessionHalf session;
    session.unapplied = [] { return std::vector<std::string>{"requirements"}; };
    session.save = [] { return true; };
    session.discard = [] { return true; };
    StoreCloseDraftAdapter adapter(*store, session);
    ICloseDraftPort& port = adapter;

    // 合并面：会话半源（requirements）＋磁盘半源（无落盘草稿＝空）——
    // 无重复模块；动作缝直通。
    const std::vector<std::string> modules = port.unappliedDraftModules();
    EXPECT_EQ(modules.size(), 1u);
    EXPECT_EQ(modules.at(0), "requirements");
    EXPECT_TRUE(port.saveDrafts());
    EXPECT_TRUE(port.discardDrafts());

    // 空会话缝＝装配缺陷（静默视为"无草稿"会丢失用户编辑——F-536）。
    StoreCloseDraftAdapter empty(*store, StoreCloseDraftAdapter::SessionHalf{});
    ICloseDraftPort& emptyPort = empty;
    EXPECT_THROW((void)emptyPort.unappliedDraftModules(), WorkflowError);
    EXPECT_THROW((void)emptyPort.saveDrafts(), WorkflowError);
}

/**
 * StoreTitleFactAdapter：store 半区真实取数（displayName/writable/label
 * 兜底/anyDirty 磁盘半源）＋会话半区缺省降级（端口契约安全缺省）——经
 * ITitleFactPort& 接口消费。
 */
TEST_F(HostAdapter, StoreTitleFactAdapterRealStoreHalfAndGracefulDegradation)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-11"}, std::vector<std::string>{"AT-21"});

    auto store = makeBlankStore("title-bridge");
    ASSERT_TRUE(store != nullptr);

    // 会话半区全部缺省（nullopt 函数）＝降级形态：schemeLabel 退
    // branchTips 兜底、anyDirty 只算磁盘半源、当前性不可判定。
    StoreTitleFactAdapter adapter(*store, StoreTitleFactAdapter::SessionHalf{});
    ITitleFactPort& port = adapter;
    const TitleFacts facts = port.collectFacts();
    EXPECT_EQ(facts.displayName, "title-bridge");  // project.json 权威元数据
    EXPECT_TRUE(facts.writable);                 // 创建者写锁持有
    EXPECT_FALSE(facts.schemeLabel.empty());     // branchTips 首行兜底
    EXPECT_FALSE(facts.anyDirty);                // 无落盘草稿
    EXPECT_FALSE(facts.resultsCurrentness.status.has_value());  // 不可判定缺省

    // 会话半区注入＝合并形态（sessionDirty=true 覆盖磁盘半源；活动分支
    // label 会话权威优先；当前性搬运）。
    StoreTitleFactAdapter::SessionHalf session;
    session.sessionDirty = [] { return std::optional<bool>(true); };
    session.activeBranchLabel = [] { return std::optional<std::string>("方案 B"); };
    session.currentness = [] {
        ui::CurrentnessProjection projection;
        return std::optional<ui::CurrentnessProjection>(std::move(projection));
    };
    StoreTitleFactAdapter merged(*store, std::move(session));
    ITitleFactPort& mergedPort = merged;
    const TitleFacts mergedFacts = mergedPort.collectFacts();
    EXPECT_TRUE(mergedFacts.anyDirty);
    EXPECT_EQ(mergedFacts.schemeLabel, "方案 B");  // 会话权威非兜底
}

/**
 * StoreRecoveryFactAdapter：统一诊断目录真实桥的折叠纪律——目录为
 * **存在性权威**（空目录＝零场景，即便计数半区有清单也不虚构场景——
 * PM-15"经统一诊断目录集成"的结构面）；写权限/计数半区透传——经
 * IRecoveryFactPort& 接口消费。（三场景真实注入的端到端在
 * HostAssemblyContractTest——目录注入的上下文校验链路在契约半区。）
 */
TEST_F(HostAdapter, StoreRecoveryFactAdapterCatalogIsExistenceAuthority)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-15"}, std::vector<std::string>{"AT-21"});

    diagnostics::DiagCatalog catalog;  // 默认护栏配置（P-DIAG-7 工程默认）——空目录

    // 计数半区声称有场景清单——但目录无场景码：不虚构场景（空清单）。
    StoreRecoveryFactAdapter::ReportHalf reportHalf;
    reportHalf.report = [] {
        project::RecoveryReport report;
        report.ignoredStagingTxs.push_back("tx-left-open");
        report.orphanDraftFiles.push_back("drafts/main/mod.draft.json.new");
        return std::optional<project::RecoveryReport>(std::move(report));
    };
    StoreRecoveryFactAdapter adapter(catalog,
                                     [] { return std::optional<bool>(false); },
                                     reportHalf);
    IRecoveryFactPort& port = adapter;
    const RecoveryFacts facts = port.collectFacts();
    EXPECT_TRUE(facts.scenarios.empty());       // 目录权威——无场景码不入清单
    EXPECT_FALSE(facts.writable);               // 写权限缝透传（false——放弃禁用输入）

    // 计数半区缺省＝折叠空清单（open 产物未持有＝环境缺省降级）。
    StoreRecoveryFactAdapter degraded(catalog, nullptr,
                                      StoreRecoveryFactAdapter::ReportHalf{});
    IRecoveryFactPort& degradedPort = degraded;
    EXPECT_TRUE(degradedPort.collectFacts().scenarios.empty());
}

// =====================================================================
// L5 关闭监督器（StoreCloseSupervisor——模型半区；兜底链路在契约测试）
// =====================================================================

/**
 * 构造参数违约 fail-fast（零/负阈值无监督语义——调用方装配违约）；
 * 未启动 stop＝no-op 安全；已闭合 store 立即观测（真实 store——
 * requestClose 后 Closed 态即闭合事实）。
 */
TEST_F(HostAdapter, CloseSupervisorConfigValidationAndImmediateClosed)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-03"}, std::vector<std::string>{"AT-21"});

    auto store = makeBlankStore("supervisor-light");
    ASSERT_TRUE(store != nullptr);

    // 调度器轻量装配（监督器构造需要引用——无任务场景零推进需求）。
    execution::TaskController controller(execution::TaskController::Config{});
    execution::DrainCoordinator drain(controller, execution::DrainCoordinator::Config{});
    execution::TaskScheduler::Collaboration collab;
    execution::TaskScheduler scheduler(controller, drain, collab);

    // 参数违约 fail-fast（WorkflowError——§10.3 调用方错误语义行）。
    StoreCloseSupervisor::Config bad;
    bad.abandonThreshold = std::chrono::milliseconds::zero();
    EXPECT_THROW(StoreCloseSupervisor(*store, scheduler, bad), WorkflowError);
    StoreCloseSupervisor::Config badGrace;
    badGrace.giveUpGrace = std::chrono::milliseconds{-1};
    EXPECT_THROW(StoreCloseSupervisor(*store, scheduler, badGrace), WorkflowError);

    // 未启动 stop＝no-op（幂等——析构路径重复收敛安全）。
    {
        StoreCloseSupervisor idle(*store, scheduler);
        idle.stop();
        EXPECT_FALSE(idle.running());
    }

    // 已闭合观测：无在途 requestClose→同步 Closed→监督器立即记录闭合退出。
    EXPECT_EQ(store->requestClose(), 0u);  // 无在途引用——同步收尾
    ASSERT_TRUE(store->closed());
    {
        StoreCloseSupervisor::Config config;
        config.abandonThreshold = std::chrono::seconds{30};
        config.giveUpGrace = std::chrono::seconds{30};
        StoreCloseSupervisor supervisor(*store, scheduler, config);
        supervisor.start();
        supervisor.stop();  // 阻塞收敛——闭合观测在循环首拍达成
        EXPECT_TRUE(supervisor.storeClosedObserved());
        EXPECT_FALSE(supervisor.abandonDiagObserved());
        EXPECT_FALSE(supervisor.gaveUp());
    }
}

/**
 * F-638——start 的 join 序（运行态重复 start 不阻塞）：修复前 start 先
 * join joinable 线程再查 running 位——监督进行中的重复 start 会阻塞在
 * 运行中线程上（直至循环退出，最长阈值＋宽限），违背幂等 no-op 契约。
 * 修复后 running 位先判（运行态直接返回），join 收割移入非运行分支。
 *
 * 用例三面（写法从简、避免 flaky——计时断言只设"行为面"宽上限）：
 * ①运行态重复 start 立即返回（计时上限 2 s——修复后为原子读＋返回的
 *   微秒级；缺陷态在本配置下阻塞至 120 s 阈值面，相差 3 个数量级以上）；
 * ②stop 收敛后可重启（收割 joinable 线程对象——join 序的非运行分支）；
 * ③监督中 store 自然闭合（①路径线程自清 running、对象保持 joinable），
 *   随后 start 先收割再重启——join 只发生在非运行态（不 std::terminate）。
 */
TEST_F(HostAdapter, CloseSupervisorRepeatedStartWhileRunningIsNonBlocking_F638)
{
    auto store = makeBlankStore("supervisor-restart");
    ASSERT_TRUE(store != nullptr);

    // 调度器轻量装配（监督器构造需要引用——无任务场景零推进需求）。
    execution::TaskController controller(execution::TaskController::Config{});
    execution::DrainCoordinator drain(controller, execution::DrainCoordinator::Config{});
    execution::TaskScheduler::Collaboration collab;
    execution::TaskScheduler scheduler(controller, drain, collab);

    // 长阈值：监督循环在用例窗口内既不闭合也不放弃——运行态稳定成立
    // （store 未 requestClose，closed() 恒 false；阈值面 120 s 远超断言窗，
    // 缺陷态若复现即表现为用例在此挂起至阈值而非误报通过）。
    StoreCloseSupervisor::Config config;
    config.abandonThreshold = std::chrono::seconds{60};
    config.giveUpGrace = std::chrono::seconds{60};
    StoreCloseSupervisor supervisor(*store, scheduler, config);

    // ---- ①运行态重复 start：立即返回且监督不被扰动。
    supervisor.start();
    ASSERT_TRUE(supervisor.running());
    const auto t0 = std::chrono::steady_clock::now();
    supervisor.start();
    supervisor.start();  // 重复触发同面（关闭入口重入形态）
    const auto elapsed = std::chrono::steady_clock::now() - t0;
    EXPECT_LT(elapsed, std::chrono::seconds{2})
        << "运行态重复 start 阻塞——join 序回归（F-638）";
    EXPECT_TRUE(supervisor.running()) << "重复 start 不得终止进行中的监督";

    // ---- ②stop 收敛后重启：非运行分支的 join 收割＋重拉。
    supervisor.stop();
    EXPECT_FALSE(supervisor.running());
    supervisor.start();
    EXPECT_TRUE(supervisor.running());

    // ---- ③自然退出收割：监督中同步闭合 store（①路径——线程自清
    //      running、线程对象保持 joinable），随后 start 先收割再重启。
    EXPECT_EQ(store->requestClose(), 0u);  // 无在途引用——同步收尾
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (supervisor.running()
           && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    ASSERT_FALSE(supervisor.running()) << "闭合观测后线程应自然退出";
    EXPECT_TRUE(supervisor.storeClosedObserved());
    supervisor.start();  // 收割 joinable 对象并重启（缺陷态此处 terminate/阻塞）
    EXPECT_TRUE(supervisor.running());
    supervisor.stop();
    EXPECT_FALSE(supervisor.running());
}

// =====================================================================
// P-SEL-9 转译编排核（SelBackfillTranslateFlow——模型半区全分支）
// =====================================================================

namespace {

/// P-SEL-9 替身处理器：模拟权威写入命令（token=apply-drivetrain-design
/// 路由键）——写一个 sel-device-backfill 记录对象（模拟 selection 记录面
/// 先行落盘——R-1 测试等价物：本目标不链 selection，记录字节以测试材料
/// 承载，对端解码权威契约由缝替身承载）。
class StubDrivetrainHandler final : public project::ICommandHandler {
public:
    [[nodiscard]] std::string commandType() const override
    {
        return std::string(kApplyDrivetrainDesignCommandToken);
    }
    [[nodiscard]] std::uint32_t currentPayloadVersion() const override { return 1; }

    project::PrepareOutcome prepare(project::HandlerContext& ctx,
                                    const project::CommandEnvelope& envelope,
                                    const project::RevisionView&,
                                    project::CommandPlan& out,
                                    std::vector<core::DiagnosticRecord>&) override
    {
        // 拒绝腿注入：payload 首字节 0xFF＝模拟对端域校验拒绝（hard-assert）。
        if (!envelope.payloadCanonical.empty()
            && envelope.payloadCanonical.front() == 0xFF) {
            out = project::CommandPlan{};
            return project::PrepareOutcome::RejectedHardAssert;
        }
        project::ObjectWrite write;
        write.objectId = ctx.objectId();
        write.objectTypeToken = std::string(kSelBackfillRecordObjectToken);
        write.payloadCanonical = envelope.payloadCanonical;  // 载荷透传登记
        out.objectWrites.push_back(std::move(write));
        out.summary = "P-SEL-9 契约测试：权威写入替身修订";
        return project::PrepareOutcome::Planned;
    }
};

/// 解码缝替身：按字节首标记分轨——ok 轨（中立 facts 一轴）／拒绝轨。
class StubRecordPort final : public ISelBackfillRecordPort {
public:
    bool failDecode = false;  ///< 拒绝注入开关
    int calls = 0;

    Decode decode(const std::vector<std::uint8_t>&) override
    {
        ++calls;
        Decode out;
        if (failDecode) {
            out.ok = false;
            out.detail = "记录解码失败（替身注入——selection 解码门拒绝）";
            return out;
        }
        out.ok = true;
        out.facts.referenceFrameToken = std::string(kSelBackfillLinkFrameToken);
        BackfillAxisRecord axis;
        axis.jointIdCanonical = core::ObjectId::generate().toCanonical();
        axis.appliedRatio = 120.0;  // 传动比（无量纲——测试值）
        axis.synthesisMassKg = 12.5;  // 合成质量（kg）
        axis.rotorInertiaKgM2 = 0.02;  // 转子惯量（kg·m²——独立登记面）
        axis.catalogVersion = "2026.1";
        axis.motorModelId = "motor-x";
        axis.gearboxModelId = "gearbox-y";
        axis.mountKind = "ground";
        out.facts.axes.push_back(axis);
        return out;
    }
};

/// 转译缝替身：ok 轨（draft——版本 1＋标记字节）／拒绝轨。
class StubTranslatePort final : public ISelBackfillTranslatePort {
public:
    bool failTranslate = false;
    int calls = 0;

    Translation translate(const SelBackfillRecordFacts& facts) override
    {
        ++calls;
        Translation out;
        if (failTranslate) {
            out.ok = false;
            out.cause = "转译被对端拒绝（替身注入）";
            out.action = "检查权威模型传动面";
            return out;
        }
        out.ok = true;
        out.draft.payloadFormatVersion = 1;
        out.draft.payloadCanonical = {0x01, static_cast<std::uint8_t>(facts.axes.size())};
        out.draft.summary = "替身转译产物";
        return out;
    }
};

}  // namespace

/**
 * P-SEL-9 前置 fail-fast（分支无效＝调用方契约违约）与 NoRecord 早退
 * （基线闭包无记录——selection 记录面不动，零提交）。
 */
TEST_F(HostAdapter, BackfillTranslateFlowPreconditionsAndNoRecord)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-10"}, std::vector<std::string>{"AT-30"});

    auto store = makeBlankStore("p-sel-9-norecord");
    ASSERT_TRUE(store != nullptr);
    StubRecordPort recordPort;
    StubTranslatePort translatePort;

    // 前置：无效分支 fail-fast。
    SelBackfillTranslateRequest invalid;
    EXPECT_THROW((void)SelBackfillTranslateFlow::run(*store, invalid, recordPort,
                                                     translatePort),
                 WorkflowError);

    // NoRecord：真实 blank 闭包无 sel-device-backfill 记录——零提交早退
    // （revisionId 空、两缝零触达——"selection 侧记录对象不动"）。
    SelBackfillTranslateRequest request;
    request.branch = store->query().branchTips().at(0).id;
    const SelBackfillTranslateOutcome outcome =
        SelBackfillTranslateFlow::run(*store, request, recordPort, translatePort);
    EXPECT_EQ(outcome.result, SelBackfillTranslateOutcome::Result::NoRecord);
    EXPECT_FALSE(outcome.revisionId.has_value());
    EXPECT_EQ(recordPort.calls, 0);
    EXPECT_EQ(translatePort.calls, 0);
}

/**
 * P-SEL-9 主线：闭包有记录（替身处理器先行落盘记录对象）→解码→转译→
 * ①端口提交（token 路由键命中替身处理器）→Committed 新修订。
 */
TEST_F(HostAdapter, BackfillTranslateFlowSubmitsThroughCommandPort)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-10"}, std::vector<std::string>{"AT-30"});

    auto store = makeBlankStore("p-sel-9-main");
    ASSERT_TRUE(store != nullptr);
    // 替身处理器注册（L5 装配期通道——store.handlerRegistry() 公共访问器；
    // token=apply-drivetrain-design＝modeling 既有命令路由键的字面引用）。
    store->handlerRegistry().registerHandler(std::make_unique<StubDrivetrainHandler>());
    const core::BranchId branch = store->query().branchTips().at(0).id;

    // 先行落盘记录对象（直提交替身处理器——模拟 selection 回填命令先行的
    // 记录面事实；替身 plan 写 sel-device-backfill 对象＝闭包记录事实。
    // 此时闭包尚无记录，转译编排核本身应 NoRecord——直提交绕开编排核，
    // 与契约测试 HostAssemblyContract 的落盘手法一致）。
    project::CommandEnvelope seedEnvelope;
    seedEnvelope.branch = branch;
    seedEnvelope.commandType = std::string(kApplyDrivetrainDesignCommandToken);
    seedEnvelope.payloadFormatVersion = 1;
    seedEnvelope.payloadCanonical = {0x01, 0x01};
    const project::CommandResult seeded =
        store->commands().submit(seedEnvelope);
    ASSERT_TRUE(seeded.committed()) << "记录对象落盘失败（替身链前置）";
    ASSERT_TRUE(seeded.newRevision.has_value());

    // 主线：闭包恰一记录 → 解码 ok → 转译 ok → 提交 Committed 新修订。
    StubRecordPort recordPort;
    StubTranslatePort translatePort;
    SelBackfillTranslateRequest request;
    request.branch = branch;
    const SelBackfillTranslateOutcome outcome =
        SelBackfillTranslateFlow::run(*store, request, recordPort, translatePort);
    EXPECT_EQ(outcome.result, SelBackfillTranslateOutcome::Result::Submitted);
    ASSERT_TRUE(outcome.revisionId.has_value());
    EXPECT_TRUE(outcome.revisionId->isValid());
    EXPECT_NE(*outcome.revisionId, *seeded.newRevision);  // 恰一新修订（PA-2 只增）
    EXPECT_EQ(recordPort.calls, 1);
    EXPECT_EQ(translatePort.calls, 1);
    EXPECT_FALSE(outcome.summary.empty());
    EXPECT_FALSE(outcome.failure.has_value());
}

/**
 * P-SEL-9 失败轨：解码拒绝／转译拒绝／提交 stale-revision——全部转
 * Failed 呈现且 revisionId 空（失败不带病报成功；对端事实透传）。
 */
TEST_F(HostAdapter, BackfillTranslateFlowFailureTracksPreserveHonesty)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-10"}, std::vector<std::string>{"AT-30"});

    auto store = makeBlankStore("p-sel-9-fail");
    ASSERT_TRUE(store != nullptr);
    store->handlerRegistry().registerHandler(std::make_unique<StubDrivetrainHandler>());
    const core::BranchId branch = store->query().branchTips().at(0).id;

    // 先行落盘记录（直提交替身处理器——同主线手法；闭包有记录才有失败
    // 轨的解码/转译）。
    project::CommandEnvelope seedEnvelope;
    seedEnvelope.branch = branch;
    seedEnvelope.commandType = std::string(kApplyDrivetrainDesignCommandToken);
    seedEnvelope.payloadFormatVersion = 1;
    seedEnvelope.payloadCanonical = {0x01, 0x01};
    const project::CommandResult seeded = store->commands().submit(seedEnvelope);
    ASSERT_TRUE(seeded.committed());

    // 解码拒绝 → Failed（detail 透传入 cause；转译缝零触达）。
    {
        StubRecordPort failedDecode;
        failedDecode.failDecode = true;
        StubTranslatePort untouched;
        SelBackfillTranslateRequest request;
        request.branch = branch;
        const SelBackfillTranslateOutcome outcome = SelBackfillTranslateFlow::run(
            *store, request, failedDecode, untouched);
        EXPECT_EQ(outcome.result, SelBackfillTranslateOutcome::Result::Failed);
        ASSERT_TRUE(outcome.failure.has_value());
        EXPECT_NE(outcome.failure->cause.find("替身注入"), std::string::npos);
        EXPECT_EQ(untouched.calls, 0);
    }
    // 转译拒绝 → Failed（cause/action 透传；零提交）。
    {
        StubRecordPort stubRecordForFail;  // ok 轨（失败轨只注入转译缝）
        StubTranslatePort failedTranslate;
        failedTranslate.failTranslate = true;
        SelBackfillTranslateRequest request;
        request.branch = branch;
        const SelBackfillTranslateOutcome outcome = SelBackfillTranslateFlow::run(
            *store, request, stubRecordForFail, failedTranslate);
        EXPECT_EQ(outcome.result, SelBackfillTranslateOutcome::Result::Failed);
        ASSERT_TRUE(outcome.failure.has_value());
        EXPECT_NE(outcome.failure->cause.find("转译被对端拒绝"), std::string::npos);
        EXPECT_FALSE(outcome.revisionId.has_value());
    }
    // 过期基线 → Rejected(stale-revision) 透传 Failed（并发校验兜底——
    // expectedRevision 锚定旧修订，tip 已前进）。stale 前先直提交一次
    // 推进 tip（seeded 后 tip==seeded.newRevision——直接锚定不构成过期）。
    {
        project::CommandEnvelope advance;
        advance.branch = branch;
        advance.commandType = std::string(kApplyDrivetrainDesignCommandToken);
        advance.payloadFormatVersion = 1;
        advance.payloadCanonical = {0x01, 0x01};
        const project::CommandResult advanced = store->commands().submit(advance);
        ASSERT_TRUE(advanced.committed()) << "tip 推进失败（替身链前置）";
    }
    {
        SelBackfillTranslateRequest stale;
        stale.branch = branch;
        stale.expectedRevision = *seeded.newRevision;  // 旧 tip——已被推进腿越过
        StubRecordPort recordPort;
        StubTranslatePort translatePort;
        const SelBackfillTranslateOutcome outcome =
            SelBackfillTranslateFlow::run(*store, stale, recordPort, translatePort);
        EXPECT_EQ(outcome.result, SelBackfillTranslateOutcome::Result::Failed);
        EXPECT_FALSE(outcome.revisionId.has_value());
    }
}

/**
 * ExecutionCloseDrainAdapter：空调度器轻面（hasActiveTask=false／
 * taskStates 空／waitDrain 幂等立即 true——无任务 shutdown 后 drained
 * 即达成）——经 ICloseDrainPort& 接口消费（重排空链路在契约测试）。
 */
TEST_F(HostAdapter, ExecutionCloseDrainAdapterEmptySchedulerLightFace)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-03"}, std::vector<std::string>{"AT-21"});

    execution::TaskController controller(execution::TaskController::Config{});
    execution::DrainCoordinator drain(controller, execution::DrainCoordinator::Config{});
    execution::TaskScheduler::Collaboration collab;  // 空协作面——无任务可提交
    execution::TaskScheduler scheduler(controller, drain, collab);

    ExecutionCloseDrainAdapter adapter(scheduler, controller,
                                       std::chrono::milliseconds{200});
    ICloseDrainPort& port = adapter;

    const core::ProjectId projectId = core::ProjectId::generate();
    EXPECT_FALSE(port.hasActiveTask(projectId));   // 空调度器无在途
    EXPECT_TRUE(port.taskStates(projectId).empty());
    EXPECT_TRUE(port.waitDrain());                 // 无任务排空立即达成
    EXPECT_TRUE(port.cooperativeCancel());         // 协作取消空集同样立即达成
}

// =====================================================================
// WorkflowLifecycleController（六方法缝编排——经基线虚类接口消费）
// =====================================================================

/**
 * openProject：Dialog 来源取消（selectOpenPath=nullopt）→零副作用退出
 * （不激活不呈现）；空路径命令行来源同取消路径——经
 * ILifecycleFlowController& 虚派发消费。
 */
TEST_F(HostAdapter, ControllerOpenProjectCancelLeavesSessionUntouched)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-02"}, std::vector<std::string>{"AT-20"});

    ReadyHost host;
    ReadyBridges bridgeState;
    bridgeState.workDir = workDir;
    bridgeState.openPath = std::nullopt;  // 用户取消脚本
    WorkflowLifecycleController controller =
        assembleWorkflowLifecycleController(host.ports(), bridgeState.bridges());
    ILifecycleFlowController& flow = controller;

    flow.openProject(OpenSource::Dialog, "");  // 空路径→选择缝→取消
    EXPECT_TRUE(bridgeState.activatedStores.empty());
    EXPECT_EQ(bridgeState.failureCount, 0);
}

/**
 * requestClose：无项目会话调用＝宿主装配缺陷 fail-fast（入口可用性
 * 裁剪归宿主——PM-10 无项目禁用语义的编排侧拒绝面）。
 */
TEST_F(HostAdapter, ControllerRequestCloseWithoutProjectFailsFast)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-03"}, std::vector<std::string>{"AT-20"});

    ReadyHost host;
    ReadyBridges bridgeState;
    bridgeState.workDir = workDir;
    bridgeState.store = nullptr;  // 无项目会话
    WorkflowLifecycleController controller =
        assembleWorkflowLifecycleController(host.ports(), bridgeState.bridges());
    ILifecycleFlowController& flow = controller;

    EXPECT_THROW((void)flow.requestClose(CloseKind::Close), WorkflowError);
}

/**
 * startNewProjectWizard：取消（collectNewProjectInputs=false）→零副作用
 * （不激活不呈现——"取消不留半成品"的宿主缝半区）。
 */
TEST_F(HostAdapter, ControllerNewProjectWizardCancelLeavesNoTrace)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-01"}, std::vector<std::string>{"AT-20"});

    ReadyHost host;
    ReadyBridges bridgeState;
    bridgeState.workDir = workDir;
    bridgeState.collectNewOk = false;  // 用户在向导内取消
    WorkflowLifecycleController controller =
        assembleWorkflowLifecycleController(host.ports(), bridgeState.bridges());
    ILifecycleFlowController& flow = controller;

    flow.startNewProjectWizard();
    EXPECT_TRUE(bridgeState.activatedStores.empty());
    EXPECT_EQ(bridgeState.failureCount, 0);
}

/**
 * startPackageWizard：Import 完成→按打开协议进入目标目录（复用
 * openProject 编排核——PM-05"按打开协议进入"同源语义；本用例以不存在的
 * 目标目录驱动打开失败呈现轨——失败可见面经 presentFailure 捕获）。
 */
TEST_F(HostAdapter, ControllerPackageImportEntersViaOpenProtocol)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-05"}, std::vector<std::string>{"AT-20"});

    ReadyHost host;
    ReadyBridges bridgeState;
    bridgeState.workDir = workDir;
    WorkflowLifecycleController controller =
        assembleWorkflowLifecycleController(host.ports(), bridgeState.bridges());
    ILifecycleFlowController& flow = controller;

    // 收集缝产出的导入请求指向不存在目标/包文件——导入执行端口替身
    // 返回失败（数据侧）→Failed→presentFailure 捕获（诚实呈现不吞）。
    bridgeState.collectImportOk = true;
    flow.startPackageWizard(PackageFlowKind::Import);
    EXPECT_GE(bridgeState.failureCount, 1);  // 失败可见（F-565/566 同族纪律）
}

/**
 * ASM-UI 修复③钉扎（asm-wf 验收建议级①）：openProject 的 .rwpack 分流
 * 后导入收集缝**恰调用一次**且 packFile 预填透传（此前形态＝分流段与
 * 向导段各收集一次〔==2〕且第二次预填丢失——用户面对两次输入收集）。
 * 分流词面（classifyOpenTarget 扩展名分流）真实触发——空 .rwpack 文件
 * 即可分流（编排①段不读内容）。经 ILifecycleFlowController& 虚派发消费。
 */
TEST_F(HostAdapter, ControllerOpenProjectPackRoute_CollectsImportOnce_WithPrefill)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-02", "PM-05"},
                  std::vector<std::string>{"AT-20"});

    // 分流目标：.rwpack 词面文件（空内容——分流只看扩展名，io 侧校验归
    // 导入执行端口）。
    const fs::path packFile = workDir / "route-target.rwpack";
    {
        std::ofstream out(packFile, std::ios::binary);
        ASSERT_TRUE(out.good()) << "包词面文件创建失败（夹具前置）";
    }

    ReadyHost host;
    ReadyBridges bridgeState;
    bridgeState.workDir = workDir;
    bridgeState.openPath = packFile;  // Dialog 来源路径收集脚本
    WorkflowLifecycleController controller =
        assembleWorkflowLifecycleController(host.ports(), bridgeState.bridges());
    ILifecycleFlowController& flow = controller;

    flow.openProject(OpenSource::Dialog, "");  // 空路径→选择缝→包路径分流

    // 收集恰一次（修复前＝2：分流段＋向导段各一次）。
    EXPECT_EQ(bridgeState.collectImportCalls, 1)
        << ".rwpack 分流后导入收集缝必须恰调用一次（asm-wf 建议级①）";
    // 预填透传：收集请求携带分流目标包路径（修复前第二次调用请求为空
    // ——预填丢失）。
    ASSERT_EQ(bridgeState.collectedImportPacks.size(), std::size_t{1});
    EXPECT_EQ(bridgeState.collectedImportPacks.front(), packFile);
    // 分流零副作用：普通存储激活零发生（包路径不走打开③步）。
    EXPECT_TRUE(bridgeState.activatedStores.empty());
    // 收集成功→导入编排（替身执行端口失败轨）→失败可见（不吞错）。
    EXPECT_GE(bridgeState.failureCount, 1);
}

/**
 * ASM-UI 修复④钉扎（asm-wf 验收建议级④）：requestClose(Switch) Proceed
 * 后激活的**候选项目身份**以候选 store 自身 projectId() 取值（此前形态
 * ＝复用切换前的会话桥 currentProjectId——旧项目身份登记给新 store 的
 * 错配）。真实落盘双项目：候选先建后关（释放写锁——编排内候选验证重新
 * 打开），当前项目经 CloseFlow 全链（无草稿无任务→Proceed）。
 */
TEST_F(HostAdapter, ControllerSwitchClose_ActivatesCandidateStoreIdentity)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-03"}, std::vector<std::string>{"AT-20"});

    // 候选项目：真实建盘拿身份与目录，随后关闭释放锁（候选验证＝编排内
    // 重新 open——同一目录的第二实例）。
    fs::path candidateDir;
    core::ProjectId candidateId{};
    {
        std::unique_ptr<project::ProjectStore> candidate =
            makeBlankStore("switch-candidate");
        ASSERT_NE(candidate, nullptr);
        candidateId = candidate->projectId();
        candidateDir = workDir / "switch-candidate.rwdesign";
        // 显式关闭（编排外释放写锁——否则候选验证 open 与本实例锁冲突）。
        candidate->requestClose();
    }

    // 当前项目：真实 store（bridgeState.store 注入——会话桥脚本面）。
    std::unique_ptr<project::ProjectStore> current = makeBlankStore("switch-current");
    ASSERT_NE(current, nullptr);

    ReadyHost host;
    ReadyBridges bridgeState;
    bridgeState.workDir = workDir;
    bridgeState.store = current.get();
    bridgeState.projectId = current->projectId();
    bridgeState.candidatePath = candidateDir;  // 切换候选脚本
    WorkflowLifecycleController controller =
        assembleWorkflowLifecycleController(host.ports(), bridgeState.bridges());
    ILifecycleFlowController& flow = controller;

    const CloseFlowResult outcome = flow.requestClose(CloseKind::Switch);
    EXPECT_EQ(outcome.result, CloseFlowOutcome::Result::Proceed)
        << "双 blank 项目全链（无草稿无任务）应 Proceed";

    // 候选激活：恰一次，且身份＝候选 store 自身身份（修复前＝旧项目身份
    // ——错配钉扎）。
    ASSERT_EQ(bridgeState.activatedStores.size(), std::size_t{1});
    ASSERT_EQ(bridgeState.activatedIds.size(), std::size_t{1});
    EXPECT_EQ(bridgeState.activatedIds.back(), candidateId)
        << "激活身份必须＝候选项目身份（asm-wf 建议级④——store 身份权威）";
    ASSERT_NE(bridgeState.activatedStores.back(), nullptr);
    // 激活面契约不变量：store 与身份同一项目（取值同源的结构性复核）。
    EXPECT_EQ(bridgeState.activatedStores.back()->projectId(),
              bridgeState.activatedIds.back());
}
