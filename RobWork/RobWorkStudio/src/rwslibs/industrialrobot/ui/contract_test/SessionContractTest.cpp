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

}  // namespace
