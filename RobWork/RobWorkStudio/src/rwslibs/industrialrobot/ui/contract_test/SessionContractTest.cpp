/**
 * @file   SessionContractTest.cpp
 * @brief  UI-T11 契约层用例（QCoreApplication 级 headless——§12.1 第二层
 *         分工"跨单元契约：对接 project/execution 公共头与桩实现"的会话
 *         生命周期面）：会话端口协议（requestClose 计数/closed 时序/
 *         subscribeClose 回调恰一次）、任务端口逐任务寻址（协作取消/常规
 *         路径零强杀）、切换后旧存储上下文保活直到归档完成（SA-17/§5.7）、
 *         退出就绪位与 scheduler.shutdown(DrainPolicy) 的 L5 对接点
 *         （§9.5）、T_force 装配期可配的触发面（P-UI-8）与 O-31 结构自证
 *         （会话端口全部为 ui 自有接口的虚派发——产品面零对端类型）。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T11.json verify 行（"cmake --build …
 *     --target sdurws_ird_ui_contract_test"——本目标即该验证的承载者）与
 *     acceptance 1~3（§5 生命周期与 project/execution 对接属 §12.1 契约
 *     测试面——契约 note 行 DOC-T13 编译回填原文）；
 *   - units/ui.md §5.1（SA-17/A7 分离原则）、§5.2（Draining 协议）、
 *     §5.4（S1/S2 时序）、§5.6（防线阈值）、§9.4（关闭对话框不提供强杀
 *     选项）、§9.5（关闭对话框任务区与 DrainPolicy 对接）、§3.1（O-31：
 *     产品面零对端链接/include——本套件以替身虚派发结构自证"会话面全部
 *     为 ui 自有端口"）。
 *
 * 与模型测试（SessionControllerModelTest）的分工：模型层验证对话框机制
 * 与状态机语义（行为矩阵），本层验证**端口协议与跨单元对接形状**（计数/
 * 时序/寻址/保活/配置面——L5 适配器将要实现的契约半区）。
 */

#include <gtest/gtest.h>

#include <QCoreApplication>

#include <sdurws/ird/ui/UiProjections.hpp>
#include <sdurws/ird/ui/UiSessionController.hpp>
#include <sdurws/ird/ui/UiTypes.hpp>
#include <sdurws/ird/ui/IDraftController.hpp>       // UI-T84 ACC1——真实 DraftController（生产三控制器组装面）
#include <sdurws/ird/ui/RuntimePublishBridge.hpp>   // UI-T84 ACC1——生产桥分派（attachOrSwitchHostSession）

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace sdurws::ird;
using ui::CloseDecision;
using ui::CloseDialogResolution;
using ui::DraftDisposition;
using ui::SessionPortBundle;
using ui::TaskDisposition;
using ui::TaskRowProjection;
using ui::UiCloseIntent;
using ui::UiOpenMode;
using ui::UiSessionController;
using ui::UiSessionControllerDeps;
using ui::UiSessionState;

// =====================================================================
// 极简替身（协议观测面——比模型层替身更窄：只记录协议事件序列）
// =====================================================================

/// 存储上下文端口替身（关闭协议事件记录——恰一次回调/计数语义的观测面）。
class ProtocolStorePort final : public ui::IUiProjectStorePort
{
public:
    core::ProjectId project;
    std::uint32_t inFlight = 1;
    bool closeRequested = false;
    bool closedFlag = false;
    int requestCloseCalls = 0;
    int closedCallbacks = 0;             ///< onStoreClosed 投递次数（恰一次观测）
    ui::IUiStoreCloseObserver* observer = nullptr;

    std::uint32_t requestClose() override
    {
        ++requestCloseCalls;
        closeRequested = true;
        return inFlight;
    }
    bool isClosed() const override { return closedFlag; }
    std::unique_ptr<core::IEventSubscription>
    subscribeClose(ui::IUiStoreCloseObserver& obs) override
    {
        observer = &obs;
        return std::make_unique<Sub>(*this);
    }
    void release()  ///< 对端归档收口（恰一次回调投递——closed 语义先置位）。
    {
        closedFlag = true;
        if (observer != nullptr) {
            ++closedCallbacks;
            observer->onStoreClosed(project);
        }
    }

private:
    class Sub final : public core::IEventSubscription
    {
    public:
        explicit Sub(ProtocolStorePort& owner)
            : m_owner(&owner)
        {}
        void unsubscribe() override { m_owner->observer = nullptr; }

    private:
        ProtocolStorePort* m_owner;
    };
};

/// 任务端口替身（逐任务寻址记录）。
class ProtocolTaskPort final : public ui::IUiSessionTaskPort
{
public:
    std::vector<TaskRowProjection> rows;
    std::vector<core::TaskIdentity> cancels;
    std::vector<core::TaskIdentity> forces;

    std::vector<TaskRowProjection> nonTerminalTasks(const core::ProjectId&) const override
    {
        return rows;
    }
    bool requestCancel(const core::TaskIdentity& t) override
    {
        cancels.push_back(t);
        return true;
    }
    bool requestForceTerminate(const core::TaskIdentity& t) override
    {
        forces.push_back(t);
        return true;
    }
};

/// 打开工厂替身（固定成功产出——协议面不需要失败形态，模型层已覆盖）。
class ProtocolFactoryPort final : public ui::IUiStoreFactoryPort
{
public:
    std::string displayName = "协议项目";
    bool writable = true;
    std::vector<TaskRowProjection> nextTaskRows;  ///< 下一次 open 的任务行集
    std::shared_ptr<ProtocolStorePort> lastStore;
    std::shared_ptr<ProtocolTaskPort> lastTasks;

    ui::OpenStoreOutcome open(const std::string& path,
                              UiOpenMode mode,
                              SessionPortBundle& out) override
    {
        ui::OpenStoreOutcome outcome;
        outcome.ok = true;
        outcome.opened.metadata.projectId = core::ProjectId::generate();
        outcome.opened.metadata.projectDisplayName = displayName;
        outcome.opened.metadata.writable = writable;
        auto store = std::make_shared<ProtocolStorePort>();
        store->project = outcome.opened.metadata.projectId;
        auto tasks = std::make_shared<ProtocolTaskPort>();
        tasks->rows = nextTaskRows;
        lastStore = store;
        lastTasks = tasks;
        out.store = store;
        out.tasks = tasks;
        return outcome;
    }
};

/// 决议便捷值：确认＋放弃＋等待（§5.4 S1 的静默默认分支）。
static CloseDecision confirmedWait()
{
    CloseDecision decision;
    decision.confirmed = true;
    decision.draft = DraftDisposition::Discard;
    decision.task = TaskDisposition::Wait;
    return decision;
}

/// 任务行工厂（五元组全字段生成——寻址断言的键）。
static TaskRowProjection makeRow()
{
    TaskRowProjection row;
    row.identity.project = core::ProjectId::generate();
    row.identity.branch = core::BranchId::generate();
    row.identity.revision = core::RevisionId::generate();
    row.identity.run = core::RunId::generate();
    row.identity.attempt = core::AttemptId{1};
    row.state = core::TaskState::Running;
    row.labelKey = ui::taskStateLabelKey(core::TaskState::Running);
    return row;
}

/// 切换步（A→B 的决议序列——供保活用例复用，先于使用点定义）。
static void resolveSwitchToB(UiSessionController& controller,
                             const std::string& candidatePath)
{
    (void)controller.beginSwitch(candidatePath, UiOpenMode::Writable);
    (void)controller.resolveCloseDialog(confirmedWait());
}

/// 会话环境（无诊断线——协议面不依赖目录；单调时钟缺省 steady_clock，
/// 阈值触发用例自带可控时钟注入）。
struct ProtocolHarness
{
    std::unique_ptr<ProtocolFactoryPort> factory = std::make_unique<ProtocolFactoryPort>();
    std::unique_ptr<UiSessionController> controller;
    int abandonCalls = 0;

    UiSessionControllerDeps deps()
    {
        UiSessionControllerDeps d;
        d.storeFactory.reset(factory.get(), [](ui::IUiStoreFactoryPort*) {});
        d.forceAbandonAll = [this]() { ++abandonCalls; };
        // diagSink/diagFactory/devLog/presentContext 缺省为空＝显式声明的
        // 无目录/无日志/无壳场景（§10.1 前置条件行同款纪律）；steadyClock
        // 缺省＝steady_clock::now。
        return d;
    }

    void build(UiSessionControllerDeps d = UiSessionControllerDeps{})
    {
        if (!d.storeFactory) {
            d = deps();  // 未定制时用标准装配（协议替身＋abandon 计数）
        }
        controller = std::make_unique<UiSessionController>(d);
    }

    /// 打开一个成功会话（协议面入口——全部用例的前置步）。
    void openDefault(std::vector<TaskRowProjection> rows = {})
    {
        factory->nextTaskRows = std::move(rows);
        (void)controller->openProject("D:/prj/proto", UiOpenMode::Writable);
    }
};

// =====================================================================
// C-3 关闭协议契约（requestClose 计数/closed 时序/subscribeClose 恰一次）
// =====================================================================

/// 关闭协议时序：beginClose/resolve 前不触碰 store；确认恰好发一次
/// requestClose（重复轮询不重发——Draining 面只读）；回调恰一次投递且
/// 驱动一次关闭完成（Draining→NoProject）；重复投递被幂等防御吸收。
TEST(SessionContract, StoreCloseProtocolTimingAndCounts)
{
    ProtocolHarness h;
    h.build();
    h.openDefault();
    auto* store = h.factory->lastStore.get();
    ASSERT_EQ(h.controller->state(), UiSessionState::OpenWritable);
    EXPECT_EQ(store->requestCloseCalls, 0);  // 对话框呈现前零触碰

    (void)h.controller->beginClose(UiCloseIntent::CloseProject);
    EXPECT_EQ(store->requestCloseCalls, 0);  // 呈现阶段仍零触碰（§5.2 Open* 行）

    ASSERT_EQ(h.controller->resolveCloseDialog(confirmedWait()).status,
              CloseDialogResolution::Status::Confirmed);
    EXPECT_EQ(store->requestCloseCalls, 1);  // 确认→恰一次关闭信号
    EXPECT_EQ(h.controller->state(), UiSessionState::Draining);

    // 重复轮询不重发关闭信号（Draining 面只读——轮询≠再次 requestClose）。
    (void)h.controller->pollDrain();
    (void)h.controller->pollDrain();
    EXPECT_EQ(store->requestCloseCalls, 1);

    // 归档收口：回调恰一次→关闭恰一次完成（Draining→NoProject）。
    store->release();
    EXPECT_EQ(store->closedCallbacks, 1);
    EXPECT_EQ(h.controller->state(), UiSessionState::NoProject);
    // 迟到重复投递：幂等防御（不重复递减/不失稳——回调纪律的防御半区）。
    store->release();
    EXPECT_EQ(store->closedCallbacks, 2);
    EXPECT_EQ(h.controller->state(), UiSessionState::NoProject);
}

// =====================================================================
// C-8 任务端口契约（协作取消逐任务寻址＋常规路径零强杀）
// =====================================================================

/// 任务端口寻址契约：协作取消对重查集逐任务恰一次请求（TaskIdentity
/// 五元组精确——execution RunRegistry 核对面，ui 透传身份不加工）；
/// 常规关闭路径零强杀请求（§9.4"关闭对话框不提供强杀选项"原文——强杀
/// 只经 §5.6 T_force 确认路径）。
TEST(SessionContract, TaskPortPerTaskAddressing)
{
    ProtocolHarness h;
    h.build();
    const TaskRowProjection t1 = makeRow();
    const TaskRowProjection t2 = makeRow();
    h.openDefault({t1, t2});
    auto* tasks = h.factory->lastTasks.get();

    (void)h.controller->beginClose(UiCloseIntent::CloseProject);
    CloseDecision cancel = confirmedWait();
    cancel.task = TaskDisposition::CooperativeCancel;
    ASSERT_EQ(h.controller->resolveCloseDialog(cancel).status,
              CloseDialogResolution::Status::Confirmed);
    ASSERT_EQ(tasks->cancels.size(), std::size_t{2});
    EXPECT_TRUE(tasks->cancels[0] == t1.identity);
    EXPECT_TRUE(tasks->cancels[1] == t2.identity);
    EXPECT_TRUE(tasks->forces.empty());  // 常规路径零强杀（§9.4 红线）

    // 协作取消重查语义（契约面）：决议后自然终态的任务不收请求——
    // 行集收缩后再次关闭只对存活行请求（协议观察：计数不增）。
    EXPECT_TRUE(tasks->forces.empty());
}

// =====================================================================
// SA-17/§5.7 契约：切换后旧存储上下文保活直到归档完成
// =====================================================================

/// 切换 A→B：A 的存储上下文引用由控制器后台持有点保活（shared 持有——
/// weak 可锁定＝shared 链存活），直到 closed 后释放；B 绑定即时完成
/// （"UI 会话立即绑定 B，不等 A 排空"——§5.4 S2 原文）。
TEST(SessionContract, SwitchKeepsOldStoreContextUntilArchiveDone)
{
    ProtocolHarness h;
    h.build();
    h.openDefault();
    auto* storeA = h.factory->lastStore.get();
    const std::weak_ptr<ProtocolStorePort> weakA = h.factory->lastStore;

    resolveSwitchToB(*h.controller, "D:/prj/b");
    // B 已绑定（上下文切换完成）。
    EXPECT_EQ(h.controller->state(), UiSessionState::OpenWritable);
    EXPECT_EQ(h.controller->drainingHoldCount(), std::size_t{1});
    // 保活半区：A 的替身仍被持有点引用——"保持 shared 持有直到
    // subscribeClose 回调"（§5.1 分离原则）的代码面实证。
    EXPECT_FALSE(weakA.expired());
    EXPECT_TRUE(storeA->closeRequested);  // 关闭信号已发（Draining 语义）

    // A 归档收口→持有点释放→shared 链断（释放点＝回调，§5.1 原文）。
    storeA->release();
    EXPECT_EQ(h.controller->drainingHoldCount(), std::size_t{0});
    EXPECT_TRUE(weakA.expired());  // 控制器不再保活旧存储上下文
    // 当前会话不受旧项目排空影响（迟到面不污染——TASK-03 呈现侧前提）。
    EXPECT_EQ(h.controller->state(), UiSessionState::OpenWritable);
}

// =====================================================================
// §9.5 契约：退出就绪位与 scheduler.shutdown(DrainPolicy) 的 L5 对接点
// =====================================================================

/// 退出路径：ExitApplication 关闭完成→takeExitPending 恰一次为 true——
/// L5/workflow 据此调用 scheduler.shutdown(CancelQueuedAndWait)（P-UI-3
/// 已销账的对接语义）＋应用退出；CloseProject 完成后置位不得出现。
TEST(SessionContract, ExitPendingOnlyForExitIntent)
{
    ProtocolHarness h;
    h.build();
    h.openDefault();
    (void)h.controller->beginClose(UiCloseIntent::CloseProject);
    (void)h.controller->resolveCloseDialog(confirmedWait());
    h.factory->lastStore->release();
    ASSERT_EQ(h.controller->state(), UiSessionState::NoProject);
    EXPECT_FALSE(h.controller->takeExitPending());  // 关项目≠退出

    // 退出意图：完成后果置位恰一次（幂等消费——重复 take 得 false）。
    h.openDefault();
    (void)h.controller->beginClose(UiCloseIntent::ExitApplication);
    (void)h.controller->resolveCloseDialog(confirmedWait());
    h.factory->lastStore->release();
    EXPECT_TRUE(h.controller->takeExitPending());
    EXPECT_FALSE(h.controller->takeExitPending());
}

// =====================================================================
// P-UI-8 契约：T_force/T_force2 装配期可配（触发面实测）
// =====================================================================

/// 阈值装配面：T_force 配置为 30 s 时 35 s 触发防线 3（默认 120 s 不触发
/// ——装配期可配的可观测证明；默认值 120 s/300 s 的冻结断言在模型层
/// PUi8DefaultsConservativeAndConfigurable）。
TEST(SessionContract, TForceThresholdsAssemblyConfigurable)
{
    ProtocolHarness h;
    UiSessionControllerDeps d;
    d.storeFactory.reset(h.factory.get(), [](ui::IUiStoreFactoryPort*) {});
    d.forceWaitThreshold = std::chrono::milliseconds{30 * 1000};  // 装配期可配
    // 注入单调时钟替身（阈值触发需要可控推进——§12.3 不用 sleep 判据）。
    auto clock = std::make_shared<std::chrono::steady_clock::time_point>(
        std::chrono::steady_clock::now());
    d.steadyClock = [clock]() { return *clock; };
    d.forceAbandonAll = []() {};
    h.controller = std::make_unique<UiSessionController>(d);

    h.openDefault({makeRow()});
    (void)h.controller->beginClose(UiCloseIntent::CloseProject);
    (void)h.controller->resolveCloseDialog(confirmedWait());

    *clock += std::chrono::milliseconds{35 * 1000};  // ≥30 s（<默认 120 s）
    EXPECT_EQ(h.controller->pollDrain().status,
              ui::DrainPollReport::Status::ForceConfirmDue);
}

// =====================================================================
// O-31 结构自证：会话面全部为 ui 自有接口的虚派发
// =====================================================================

/// O-31（契约 acceptance 3）：控制器对会话端口的一切交互都经 ui 自有抽象
/// 接口（IUiStoreFactoryPort/IUiProjectStorePort/IUiDraftQueryPort/
/// IUiSessionTaskPort）虚派发完成——本用例以独立替身实现再次注入并走通
/// 打开→对话框→关闭→回调全程，结构上证明 ui 侧不依赖任何对端具体类型
/// （产品面零对端链接/include 由 LinkageContractTest 与
/// NoCrossUnitInclude_O31_UI_BUILD 守卫常驻自证；本用例是行为面的互证）。
TEST(SessionContract, O31SessionFacesAreUiOwnedInterfaces)
{
    ProtocolHarness h;
    h.build();
    h.openDefault({makeRow()});
    ASSERT_EQ(h.controller->state(), UiSessionState::OpenWritable);
    (void)h.controller->beginClose(UiCloseIntent::CloseProject);
    ASSERT_EQ(h.controller->resolveCloseDialog(confirmedWait()).status,
              CloseDialogResolution::Status::Confirmed);
    ASSERT_EQ(h.controller->state(), UiSessionState::Draining);
    h.factory->lastStore->release();
    EXPECT_EQ(h.controller->state(), UiSessionState::NoProject);
}

// =====================================================================
// F-910 关闭→同进程重开分派契约（UI-T84 acceptance 1——返工修订语义）
// =====================================================================

/// 草稿查询端口替身（真实 DraftController 的对端装配半区）：清单恒空。
/// 本测试对象是关闭→重开的**分派与状态机**面；磁盘保真已由
/// HostDraftChainContractTest 真实存储面覆盖，此处零重复。
class Acc1DraftQueryPort final : public ui::IUiDraftQueryPort {
public:
    std::vector<ui::DraftRowProjection> listDrafts() const override { return {}; }
};

/// 草稿写半区端口替身：落盘/放弃恒成功，读取恒 Missing（缺省安全值）。
class Acc1DraftStorePort final : public ui::IUiDraftStorePort {
public:
    ui::DraftSaveOutcome save(const ui::DraftDocumentProjection&) override
    {
        ui::DraftSaveOutcome out;
        out.ok = true;
        return out;
    }
    ui::DraftLoadOutcome tryLoad(const std::string&) override { return {}; }
    ui::DraftDiscardOutcome discard(const std::string&) override
    {
        ui::DraftDiscardOutcome out;
        out.ok = true;
        return out;
    }
};

/// 模块草稿源替身（IModuleDraftSource 端口合规最小实现——载荷固定文本，
/// 使 saveAll 真实组装出一份文档走通脏关闭链）。
class Acc1ModuleDraftSource final : public ui::IModuleDraftSource {
public:
    Acc1ModuleDraftSource(std::string moduleId, std::string displayName)
        : m_moduleId(std::move(moduleId)), m_displayName(std::move(displayName))
    {
    }
    std::string displayName() const override { return m_displayName; }
    ui::DraftDocumentProjection buildDraftDocument() const override
    {
        ui::DraftDocumentProjection doc;
        doc.moduleId = m_moduleId;
        doc.schemaVersion = 1;
        doc.payload = "acc1-dirty-close";
        return doc;
    }
    void adoptRestoredDocument(const ui::DraftDocumentProjection&) override {}
    void rebuildOnRevision(const std::string&) override {}

private:
    std::string m_moduleId;
    std::string m_displayName;
};

/// 呈现构造端口替身：恒无视图（本测试只用桥的绑定态分派面——不消费
/// 呈现事件，fetch 不被触达即为本用例的声明边界）。
class Acc1PresentationSource final : public ui::IUiPresentationSource {
public:
    std::optional<ui::PresentationViewProjection> fetchPresentation(
        const ui::PresentationEventFacts& /*facts*/) override
    {
        return std::nullopt;
    }
};

/// 宿主刷新端口替身：应用恒成功（桥构造必填依赖——装配纪律面）。
class Acc1PresentationOutlet final : public ui::IUiPresentationOutlet {
public:
    ui::PresentationApplyReport applyPresentation(
        const ui::PresentationViewProjection& /*view*/) override
    {
        ui::PresentationApplyReport report;
        report.ok = true;
        return report;
    }
    void releasePresentation() override {}
};

/** 关闭→同进程重开，生产三控制器组装（UI-T84 acceptance 1 返工修订口径）：
 *  真实 UiSessionController＋RuntimePublishBridge＋DraftController 按生产
 *  接线形态组装——presentContext＝生产分派形态 attachOrSwitchHostSession
 *  （UiPlugin 同构，F-910 载重修复面）；脏事实经 DraftController
 *  onSessionDirtyChanged→session.reportSessionDirty 生产者接线；
 *  [保存] 决议经 saveAllDraftsManual→真实 saveAll(CloseDialog)。
 *  钉三件事：①脏会话关闭全程零逃逸（beginClose/resolve/release 无异常
 *  穿出）；②关闭完成控制器归 NoProject；③桥旧绑在时重开不炸——
 *  attachOrSwitch 按绑定态走 switch（绑定前移到新项目，boundProject 可
 *  观测）。修复前该形态＝attach 双挂 fail-fast 整站退出（红面已由验收
 *  attempt 1 N2 变异实证），此处钉修复后的绿面。 */
TEST(SessionContract, CloseThenReopenDispatchesAttachOrSwitchWithoutEscape_UI_T84_ACC1)
{
    // ---- 装配半区（端口替身零 owning shared_ptr——栈对象生命周期覆盖全程）----
    auto bridgeSource = std::make_shared<Acc1PresentationSource>();
    auto bridgeOutlet = std::make_shared<Acc1PresentationOutlet>();
    ui::RuntimePublishBridge::Deps bridgeDeps;
    bridgeDeps.source = bridgeSource;
    bridgeDeps.outlet = bridgeOutlet;
    ui::RuntimePublishBridge bridge{bridgeDeps};  // 桥无默认构造（装配 fail-fast 面）
    Acc1DraftQueryPort draftQuery;
    Acc1DraftStorePort draftStore;
    Acc1ModuleDraftSource source("acc1-module", "acc1 模块");

    // 会话控制器先行（presentContext 与 saveAllDraftsManual 都要触达
    // DraftController——后者滞后创建，经 unique_ptr 间接持有）。
    ProtocolHarness h;
    std::unique_ptr<ui::IDraftController> draftCtl;
    UiSessionControllerDeps d = h.deps();
    d.presentContext = [&](const ui::ProjectContextProjection& projection) {
        if (!projection.project.has_value()) {
            return;  // 首页态投影（无项目）不触发分派——生产分派同构
        }
        // 生产分派形态：attachOrSwitch 按绑定态分派（未绑定 attach／已
        // 绑定 switch）——壳层拆绑缺失/迟到的容错正是 F-910 修复语义。
        // 注意 presentContext 会被重呈现多次触发（reportSessionDirty/
        // 状态翻转均重放——presentContextLocked 契约），故本 lambda 只做
        // 幂等分派，不承担一次性装配（DraftController 绑定归下方壳层
        // attach 镜像——生产 attachPresentationSession 同位）。
        bridge.attachOrSwitchHostSession(projection.project->projectId);
    };
    // [保存] 决议委托（生产接线——UiPlugin 同构：CloseDialog 触发真实落盘）。
    d.saveAllDraftsManual = [&]() {
        return draftCtl->saveAll(ui::SaveTrigger::CloseDialog).failedCount == 0;
    };
    h.build(std::move(d));

    // DraftController 脏翻转→会话脏标记（生产者接线——reportSessionDirty
    // 唯一注入面；UI-T12 承诺的 L5 接线形态）。
    ui::DraftControllerDeps dd;
    dd.postToDiskThread = [](std::function<void()> task) -> bool {
        task();  // 内联执行（testkit §6.7——单线程测试 Marshal 语义等价）
        return true;
    };
    dd.postToUiThread = [](std::function<void()> task) -> bool {
        task();
        return true;
    };
    dd.onSessionDirtyChanged = [session = h.controller.get()](bool dirty) {
        session->reportSessionDirty(dirty);  // 生产者接线（h.build 已建控制器——非空）
    };
    draftCtl = ui::createDraftController(std::move(dd));

    // ---- 会话①：打开→壳层 attach 镜像→编辑置脏（真实脏链）----
    h.openDefault();
    ASSERT_EQ(h.controller->state(), UiSessionState::OpenWritable);
    ASSERT_TRUE(bridge.hasBoundSession());
    const core::ProjectId firstId = h.factory->lastStore->project;
    // DraftController 绑定（壳层 attachPresentationSession 镜像——§10.5
    // 归属三元组取现场存储事实；与 presentContext 重呈现解耦）。
    ui::DraftSessionBinding binding;
    binding.projectId = firstId;
    binding.writable = true;
    binding.drafts.reset(&draftQuery, [](ui::IUiDraftQueryPort*) {});
    binding.store.reset(&draftStore, [](ui::IUiDraftStorePort*) {});
    draftCtl->bindSession(binding);
    draftCtl->attachModule("acc1-module", source);
    draftCtl->notifySessionDirty("acc1-module");  // 脏链直达会话标记（伴随重呈现）
    (void)h.controller->beginClose(UiCloseIntent::CloseProject);

    // ---- 关闭：[保存] 决议走真实 saveAll(CloseDialog)——零逃逸面----
    CloseDecision saveDecision;
    saveDecision.confirmed = true;
    saveDecision.draft = DraftDisposition::Save;
    saveDecision.task = TaskDisposition::Wait;
    ASSERT_EQ(h.controller->resolveCloseDialog(saveDecision).status,
              CloseDialogResolution::Status::Confirmed)
        << "脏会话[保存]关闭必须零逃逸（F-910 修复面——异常不再穿出协议面）";
    h.factory->lastStore->release();  // 归档收口→NoProject
    ASSERT_EQ(h.controller->state(), UiSessionState::NoProject);

    // 壳层收口镜像（拆绑归 UI 壳层——模型层重开前显式拆绑，生产同序）。
    draftCtl->unbindSession();

    // ---- 同进程重开：attachOrSwitch 分派（switch 半区——F-922 消账：
    // 上步 unbindSession 只拆草稿控制器，桥仍绑旧项目；openDefault 触发
    // 的分派走 switch 换绑而非首次挂接，覆盖本身完整——变异证红在案）----
    h.openDefault();
    ASSERT_EQ(h.controller->state(), UiSessionState::OpenWritable)
        << "重开必须零逃逸且回到 Open*";
    const core::ProjectId secondId = h.factory->lastStore->project;
    EXPECT_FALSE(secondId == firstId) << "协议替身每次打开生成新项目身份";
    ASSERT_TRUE(bridge.hasBoundSession());
    ASSERT_TRUE(bridge.boundProject().has_value());
    EXPECT_EQ(*bridge.boundProject(), secondId)
        << "重开分派后桥绑定＝新项目（switch 换绑可观测终点）";

    // ---- 分派容错面（switch 半区——壳层拆绑缺失/迟到也不炸）----
    // 直接再触发一次分派（旧绑还在、身份不同）＝重开期重复 presentContext
    // 的防御形态：attachOrSwitch 走 switch 换绑而非 attach 双挂（F-910
    // 修复前此形态 logic_error 整站退出）。
    bridge.attachOrSwitchHostSession(secondId);
    ASSERT_TRUE(bridge.boundProject().has_value());
    EXPECT_EQ(*bridge.boundProject(), secondId)
        << "分派后绑定必须前移到新项目（switch 语义可观测终点）";
}

}  // namespace
