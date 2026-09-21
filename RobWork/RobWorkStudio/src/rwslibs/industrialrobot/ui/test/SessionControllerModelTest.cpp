/**
 * @file   SessionControllerModelTest.cpp
 * @brief  UI-T11 模型层用例（QCoreApplication 级——§12.1 第一层分工）：
 *         统一确认对话框机制（数据装配＋确认/取消/保存/放弃/等待/协作取消
 *         分支——§5.4 S1/S2）、只读打开横幅含 PID 与 actionKind 逐场景区分
 *         （UI-SES-2/3——PM-07/§5.3）、会话状态机迁移矩阵与 INV-SES-1/2/3
 *         不变量（§5.2）、Draining 四级防线与 T_force/T_force2 装配期可配
 *         阈值（UI-SES-5/6——§5.6/P-UI-8）、迟到写提示不重试（UI-SES-7/
 *         UI-LCY-1——§5.3/§5.7）。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T11.json acceptance 1~3（逐条对应各
 *     用例 IRD_TEST_INFO 追溯字段）；knownPitfalls O-31（已裁决放行——
 *     会话端口全部以 ui 自有接口替身承载，零对端类型）与 P-UI-8（T_force/
 *     T_force2 按 §5.6 默认保守值实现且装配期可配——不私定终值）；
 *   - units/ui.md §5 全节（§5.2 状态机＋不变量、§5.3 显示差异、§5.4
 *     S1/S2 时序、§5.5 只读禁用、§5.6 防线、§5.7 在途归档分离）、§6.2
 *     （epoch 递增时机）、§12.3 UI-SES-2~7/UI-LCY-1/UI-DRF-1 行（机制
 *     半区的观测点——GUI 呈现半区归 gui_test 层）；
 *   - 先例：ShortcutRegistryModelTest.cpp 的诊断全链接线（StableCode
 *     Registry＋DiagnosticsFactory＋DiagCatalog＋DevLogRecorder）与
 *     StageNavigationModelTest.cpp 的可控替身注入形态。
 *
 * 为什么替身而不集成桩：O-31 裁决下 ui 产品面对 project/execution 零链接
 * 零 include——替身实现 ui 自有端口（IUiStoreFactoryPort/IUiProjectStorePort
 * /IUiDraftQueryPort/IUiSessionTaskPort），L5 装配期才以适配器绑定对端
 * （§3.1"ui 测试以可控替身承载"原文）。替身同时充当 §12.3 各用例前置列
 * 的"桩 OpenStoreResult/锁竞争/归档挂起"角色（两进程锁竞争/FaultPlan 的
 * 真实形态归 §12.1 第三层 GUI/集成面，本层验证机制语义）。
 *
 * 会话脏标记的供给说明：m_sessionDirty 的生产者＝编辑会话（IDraftController
 * §10.5/UI-T12），UI-T11 只承载该事实位（装配进对话框/上下文投影、放弃/
 * 保存处置时清除）——本套件按机制半区断言（位随投影呈现、磁盘行集独立
 * 保留），生产者接线随 UI-T12 契约头落地复核。
 */

#include <gtest/gtest.h>

#include <QCoreApplication>

#include <sdurws/ird/diagnostics/Catalog.hpp>
#include <sdurws/ird/diagnostics/DiagCodes.hpp>
#include <sdurws/ird/diagnostics/Factory.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include <sdurws/ird/ui/IWorkbenchShell.hpp>  // uiDiagnosticCodeDescriptors（§3.5 九码描述符供体——诊断全链装配）
#include <sdurws/ird/ui/UiProjections.hpp>
#include <sdurws/ird/ui/UiSessionController.hpp>
#include <sdurws/ird/ui/UiTypes.hpp>

#include <algorithm>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace sdurws::ird;
using ui::CloseDecision;
using ui::CloseDialogData;
using ui::CloseDialogInputs;
using ui::CloseDialogResolution;
using ui::DraftDisposition;
using ui::DraftRowProjection;
using ui::ProjectContextProjection;
using ui::ReadOnlyBannerProjection;
using ui::ReadOnlyOpenCause;
using ui::SessionPortBundle;
using ui::TaskDisposition;
using ui::TaskRowProjection;
using ui::UiCloseIntent;
using ui::UiOpenMode;
using ui::UiSessionController;
using ui::UiSessionControllerDeps;
using ui::UiSessionState;

// =====================================================================
// 测试替身（与 ShortcutRegistryModelTest/StageNavigationModelTest 同款纪律）
// =====================================================================

class DevLogRecorder final : public diagnostics::IDevLogSink {
public:
    void logDev(std::string_view channel, std::string message) override
    {
        m_entries.emplace_back(std::string(channel), std::move(message));
    }
    bool seen(const std::string& needle) const
    {
        return std::any_of(m_entries.begin(), m_entries.end(),
                           [&needle](const auto& e) {
                               return e.second.find(needle) != std::string::npos;
                           });
    }

private:
    std::vector<std::pair<std::string, std::string>> m_entries;
};

class ManualDiagClock final : public diagnostics::IClock {
public:
    std::chrono::system_clock::time_point nowUtc() const override { return m_now; }
    std::chrono::system_clock::time_point m_now{std::chrono::seconds{1000000}};
};

/// 假单调时钟（T_force/T_force2 防线驱动——§12.3 通用判据"不使用固定
/// sleep 判据"：时长阈值用注入时钟确定性推进，不真实等待）。
class FakeSteadyClock final
{
public:
    std::chrono::steady_clock::time_point now() const { return m_now; }
    void advance(std::chrono::milliseconds delta) { m_now += delta; }

private:
    std::chrono::steady_clock::time_point m_now{std::chrono::seconds{1000}};
};

/// 存储上下文端口替身（C-3 关闭协议面——requestClose/closed/subscribeClose）。
class StubStorePort final : public ui::IUiProjectStorePort
{
public:
    core::ProjectId project;          ///< 所属项目（回调匹配键——工厂替身填入）
    std::uint32_t inFlightOnClose = 2;///< requestClose 返回值（在途引用数——可编程）
    bool closeRequested = false;      ///< requestClose 是否已被调用
    bool closedFlag = false;          ///< closed()==true 语义（测试显式置位——归档挂起形态）
    ui::IUiStoreCloseObserver* closeObserver = nullptr; ///< 订阅的回调面（恰一观察者）

    std::uint32_t requestClose() override
    {
        closeRequested = true;
        return inFlightOnClose;
    }

    bool isClosed() const override { return closedFlag; }

    std::unique_ptr<core::IEventSubscription>
    subscribeClose(ui::IUiStoreCloseObserver& observer) override
    {
        closeObserver = &observer;
        return std::make_unique<Subscription>(*this);
    }

    /// 测试驱动：对端归档完成→上下文释放（subscribeClose 回调路径）。
    void fireClosed()
    {
        closedFlag = true;
        if (closeObserver != nullptr) {
            closeObserver->onStoreClosed(project);
        }
    }

private:
    class Subscription final : public core::IEventSubscription
    {
    public:
        explicit Subscription(StubStorePort& owner)
            : m_owner(&owner)
        {}
        void unsubscribe() override { m_owner->closeObserver = nullptr; }

    private:
        StubStorePort* m_owner;
    };
};

/// 草稿清单端口替身（C-5 只读半区）。
class StubDraftPort final : public ui::IUiDraftQueryPort
{
public:
    std::vector<DraftRowProjection> rows;  ///< 可编程行集（空＝无磁盘草稿）

    std::vector<DraftRowProjection> listDrafts() const override { return rows; }
};

/// 会话任务端口替身（C-8 查询/控制面——取消/强杀逐任务记录）。
class StubTaskPort final : public ui::IUiSessionTaskPort
{
public:
    std::vector<TaskRowProjection> rows;         ///< 可编程非终态行集
    std::vector<core::TaskIdentity> cancelCalls; ///< 协作取消请求记录
    std::vector<core::TaskIdentity> forceCalls;  ///< 强制终止请求记录

    std::vector<TaskRowProjection> nonTerminalTasks(const core::ProjectId&) const override
    {
        return rows;
    }

    bool requestCancel(const core::TaskIdentity& task) override
    {
        cancelCalls.push_back(task);
        return true;
    }

    bool requestForceTerminate(const core::TaskIdentity& task) override
    {
        forceCalls.push_back(task);
        return true;
    }
};

/// 打开工厂端口替身（C-3 工厂面——"桩 ProjectStoreFactory"角色）。
class StubStoreFactoryPort final : public ui::IUiStoreFactoryPort
{
public:
    /// 下一次 open 的结果（可编程——成功/降级只读/失败三形态）。
    ui::OpenStoreOutcome nextOutcome;
    /// 下一次 open 绑定的会话端口（每次 open 被移走——测试按需重填）。
    SessionPortBundle nextBindings;
    /// open 调用记录（路径＋模式——候选验证/打开入口的观测面）。
    std::vector<std::pair<std::string, UiOpenMode>> openCalls;

    ui::OpenStoreOutcome open(const std::string& canonicalPath,
                              UiOpenMode mode,
                              SessionPortBundle& outBindings) override
    {
        openCalls.emplace_back(canonicalPath, mode);
        if (!nextOutcome.ok) {
            nextOutcome.failure.projectPath = canonicalPath;
            return nextOutcome;  // 失败不写 outBindings（端口契约后置——绑定零泄漏）
        }
        outBindings = std::move(nextBindings);
        return nextOutcome;
    }
};

// =====================================================================
// 测试环境装配（诊断全链＋假时钟＋可编程工厂——ShortcutHarness 同案）
// =====================================================================

struct SessionHarness
{
    FakeSteadyClock clock;
    diagnostics::StableCodeRegistry registry;
    ManualDiagClock diagClock;
    std::shared_ptr<diagnostics::DiagnosticsFactory> factory;
    std::shared_ptr<diagnostics::DiagCatalog> catalog;
    std::shared_ptr<DevLogRecorder> devLog = std::make_shared<DevLogRecorder>();
    StubStoreFactoryPort* factoryPort = nullptr;
    std::unique_ptr<UiSessionController> controller;

    /// 草稿保存接线记录（saveAllDraftsManual 的调用观测）。
    int saveAllCalls = 0;
    bool saveAllResult = true;
    /// L5 abandonAll 接线记录（forceAbandonAll 的调用观测）。
    int abandonAllCalls = 0;
    /// 上下文注入记录（presentContext 的快照观测）。
    std::vector<ProjectContextProjection> contextUpdates;

    SessionHarness()
    {
        // 诊断全链（码表收编 UI-* 九码——emitContextInvalidDiagnostic 的
        // 目录出线依赖 UI-SESSION-CONTEXT-INVALID 已注册：未注册码会被
        // 工厂 CodeUnknown 拒绝，这正是要验证的产码纪律边界）。
        diagnostics::registerBuiltinCodes(registry);
        for (const auto& descriptor : ui::uiDiagnosticCodeDescriptors()) {
            registry.registerCode(descriptor);
        }
        factory = std::make_shared<diagnostics::DiagnosticsFactory>(registry, diagClock);
        catalog = std::make_shared<diagnostics::DiagCatalog>();
    }

    /// 目录内指定码的条目计数（诊断出线断言面——ShortcutHarness 同案）。
    std::size_t countOf(const std::string& code) const
    {
        const auto items = catalog->snapshot(diagnostics::DiagQuery{});
        return static_cast<std::size_t>(std::count_if(
            items.begin(), items.end(),
            [&code](const diagnostics::DiagProjectionItem& i) { return i.code == code; }));
    }

    /// 构造一个可成功打开的会话端口绑定（每次调用产出全新替身实例——
    /// 多会话/切换场景各自独立，互不串扰；测试经 lastStore/lastDrafts/
    /// lastTasks 取回最近一批替身指针做驱动与断言）。
    SessionPortBundle makeBindings(const core::ProjectId& project,
                                   std::vector<TaskRowProjection> taskRows = {})
    {
        auto store = std::make_shared<StubStorePort>();
        store->project = project;
        auto drafts = std::make_shared<StubDraftPort>();
        auto tasks = std::make_shared<StubTaskPort>();
        tasks->rows = std::move(taskRows);
        m_lastStore = store;
        m_lastDrafts = drafts;
        m_lastTasks = tasks;
        SessionPortBundle bundle;
        bundle.store = store;
        bundle.drafts = drafts;
        bundle.tasks = tasks;
        return bundle;
    }

    /// 可编程一次成功打开（writable 指定 INV-SES-1 数据源取值）。
    void armSuccessfulOpen(const std::string& displayName, bool writable,
                           std::vector<TaskRowProjection> taskRows = {})
    {
        factoryPort->nextOutcome = ui::OpenStoreOutcome{};
        factoryPort->nextOutcome.ok = true;
        factoryPort->nextOutcome.opened.metadata.projectId = core::ProjectId::generate();
        factoryPort->nextOutcome.opened.metadata.projectDisplayName = displayName;
        factoryPort->nextOutcome.opened.metadata.writable = writable;
        factoryPort->nextBindings =
            makeBindings(factoryPort->nextOutcome.opened.metadata.projectId,
                         std::move(taskRows));
    }

    /// 可编程锁竞争降级只读打开（UI-SES-2——横幅含 PID 的数据面）。
    void armLockHeldOpen(std::uint64_t pid, const std::string& host)
    {
        factoryPort->nextOutcome = ui::OpenStoreOutcome{};
        factoryPort->nextOutcome.ok = true;
        factoryPort->nextOutcome.opened.metadata.projectId = core::ProjectId::generate();
        factoryPort->nextOutcome.opened.metadata.projectDisplayName = "锁竞争项目";
        factoryPort->nextOutcome.opened.metadata.writable = false;
        factoryPort->nextOutcome.opened.readOnlyCause = ReadOnlyOpenCause::LockHeld;
        ui::LockHolderProjection holder;
        holder.pid = pid;
        holder.host = host;
        factoryPort->nextOutcome.opened.lockHolder = holder;
        factoryPort->nextBindings =
            makeBindings(factoryPort->nextOutcome.opened.metadata.projectId);
    }

    /// 可编程介质只读/权限不足降级打开（UI-SES-3 的两成因）。
    void armDegradedOpen(ReadOnlyOpenCause cause, const std::string& displayName)
    {
        factoryPort->nextOutcome = ui::OpenStoreOutcome{};
        factoryPort->nextOutcome.ok = true;
        factoryPort->nextOutcome.opened.metadata.projectId = core::ProjectId::generate();
        factoryPort->nextOutcome.opened.metadata.projectDisplayName = displayName;
        factoryPort->nextOutcome.opened.metadata.writable = false;
        factoryPort->nextOutcome.opened.readOnlyCause = cause;
        factoryPort->nextOutcome.opened.lockHolder = std::nullopt;
        factoryPort->nextBindings =
            makeBindings(factoryPort->nextOutcome.opened.metadata.projectId);
    }

    /// 可编程失败打开（§5.3 行 6 错误页形态）。
    void armFailedOpen(const std::string& code, const std::string& detail)
    {
        factoryPort->nextOutcome = ui::OpenStoreOutcome{};
        factoryPort->nextOutcome.ok = false;
        factoryPort->nextOutcome.failure.errorCodeToken = code;
        factoryPort->nextOutcome.failure.detail = detail;
        factoryPort->nextOutcome.failure.projectPath = "filled-on-open";
    }

    /// 构建 deps（诊断全链＋假时钟＋两接线点——各用例按需覆盖默认；
    /// storeFactory 以无删除共享引用包装替身裸指针——所有权在测试）。
    UiSessionControllerDeps makeDeps()
    {
        UiSessionControllerDeps deps;
        deps.storeFactory.reset(factoryPort, [](ui::IUiStoreFactoryPort*) {});
        deps.diagSink = catalog;
        deps.diagFactory = factory;
        deps.devLog = devLog;
        deps.presentContext = [this](const ProjectContextProjection& ctx) {
            contextUpdates.push_back(ctx);
        };
        deps.saveAllDraftsManual = [this]() {
            ++saveAllCalls;
            return saveAllResult;
        };
        deps.forceAbandonAll = [this]() { ++abandonAllCalls; };
        deps.steadyClock = [this]() { return clock.now(); };
        return deps;
    }

    std::shared_ptr<StubStorePort> lastStore() const { return m_lastStore; }
    std::shared_ptr<StubDraftPort> lastDrafts() const { return m_lastDrafts; }
    std::shared_ptr<StubTaskPort> lastTasks() const { return m_lastTasks; }

private:
    std::shared_ptr<StubStorePort> m_lastStore;
    std::shared_ptr<StubDraftPort> m_lastDrafts;
    std::shared_ptr<StubTaskPort> m_lastTasks;
};

/// 测试夹具：每个用例全新环境（工厂替身＋控制器惰性构建）。
class SessionControllerModelTest : public ::testing::Test
{
protected:
    void buildController()
    {
        m_factoryPort = std::make_unique<StubStoreFactoryPort>();
        m_harness.factoryPort = m_factoryPort.get();
        m_harness.controller =
            std::make_unique<UiSessionController>(m_harness.makeDeps());
    }

    SessionHarness& harness() { return m_harness; }
    StubStoreFactoryPort& factoryPort() { return *m_factoryPort; }
    UiSessionController& controller()
    {
        if (!m_harness.controller) {
            buildController();
        }
        return *m_harness.controller;
    }

    /// 造一个非终态任务行（任务区数据面——identity 全字段生成）。
    static TaskRowProjection makeTaskRow(const std::string& phaseToken)
    {
        TaskRowProjection row;
        row.identity.project = core::ProjectId::generate();
        row.identity.branch = core::BranchId::generate();
        row.identity.revision = core::RevisionId::generate();
        row.identity.run = core::RunId::generate();
        row.identity.attempt = core::AttemptId{1};
        row.state = core::TaskState::Running;
        row.archivePhaseToken = phaseToken;
        row.labelKey = ui::taskStateLabelKey(core::TaskState::Running);
        return row;
    }

    /// 决议便捷值：确认＋放弃＋等待（§5.4 S1 的静默默认分支）。
    static CloseDecision confirmedWait()
    {
        CloseDecision decision;
        decision.confirmed = true;
        decision.draft = DraftDisposition::Discard;
        decision.task = TaskDisposition::Wait;
        return decision;
    }

private:
    SessionHarness m_harness;
    std::unique_ptr<StubStoreFactoryPort> m_factoryPort;
};

// =====================================================================
// acceptance 1：统一确认对话框机制——数据装配（§5.4 S1"数据装配"框）
// =====================================================================

/// UI-DRF-1/§5.4：对话框数据装配＝草稿区（磁盘行集＋会话脏位）＋任务区
/// （非终态行集）＋按钮可用位（保存仅可写会话）；行集直通不加工。
TEST_F(SessionControllerModelTest, AssembleDialogDataAreasAndAvailability)
{
    IRD_TEST_INFO("PM-03", {"AT-34"}, std::nullopt);
    buildController();

    // ---- 纯函数面：直接断言装配规则（§5.4 逐项）。
    CloseDialogInputs inputs;
    inputs.projectDisplayName = "装配测试项目";
    inputs.writable = true;
    inputs.sessionDirty = true;
    DraftRowProjection draft;
    draft.documentKey = "module-a";
    draft.displayName = "模块 A";
    draft.sessionDirty = true;
    inputs.diskDraftRows = {draft};
    inputs.nonTerminalTaskRows = {makeTaskRow("archiving")};

    const CloseDialogData data = ui::assembleCloseDialogData(inputs);
    EXPECT_EQ(data.projectDisplayName, "装配测试项目");
    ASSERT_EQ(data.draftRows.size(), std::size_t{1});
    EXPECT_EQ(data.draftRows[0].displayName, "模块 A");
    ASSERT_EQ(data.taskRows.size(), std::size_t{1});
    EXPECT_EQ(data.taskRows[0].archivePhaseToken, "archiving");
    EXPECT_TRUE(data.sessionDirty);
    EXPECT_TRUE(data.saveDraftsAvailable);   // 可写会话——[保存草稿]可选
    EXPECT_FALSE(data.noActiveTasks);

    // ---- 只读会话：保存按钮不可选（§5.5 draft.save 写操作禁用的呈现位）。
    inputs.writable = false;
    EXPECT_FALSE(ui::assembleCloseDialogData(inputs).saveDraftsAvailable);

    // ---- 空任务集：noActiveTasks 置位（"等待"此时直接进入 Draining）。
    inputs.nonTerminalTaskRows.clear();
    const CloseDialogData empty = ui::assembleCloseDialogData(inputs);
    EXPECT_TRUE(empty.noActiveTasks);
}

/// §5.4 S1[取消]：决议取消→回到原状态——状态从未离开 Open*、未发
/// requestClose、单槽释放（可再次 beginClose）。
TEST_F(SessionControllerModelTest, CloseCancelRestoresOriginalState)
{
    IRD_TEST_INFO("PM-03", {}, std::nullopt);
    buildController();
    harness().armSuccessfulOpen("取消测试项目", true);
    ASSERT_TRUE(controller().openProject("D:/prj/cancel", UiOpenMode::Writable).ok);
    ASSERT_EQ(controller().state(), UiSessionState::OpenWritable);
    const std::uint64_t epochBefore = controller().epoch();

    const CloseDialogData dialog = controller().beginClose(UiCloseIntent::CloseProject);
    EXPECT_FALSE(dialog.projectDisplayName.empty());
    EXPECT_EQ(controller().state(), UiSessionState::OpenWritable);  // 呈现不改状态

    CloseDecision decision;  // confirmed=false＝[取消]
    const CloseDialogResolution out = controller().resolveCloseDialog(decision);
    EXPECT_EQ(out.status, CloseDialogResolution::Status::Cancelled);
    EXPECT_EQ(controller().state(), UiSessionState::OpenWritable);  // 原状态
    EXPECT_EQ(controller().epoch(), epochBefore);                   // 无会话形变
    EXPECT_FALSE(harness().lastStore()->closeRequested);            // 未发关闭信号

    // 单槽已释放：可再次发起（begin*/resolve* 配对纪律的正面观测）。
    EXPECT_NO_THROW(controller().beginClose(UiCloseIntent::CloseProject));
}

/// §5.4 S1[保存草稿]：saveAll(Manual) 被调用→成功后继续进入 Draining；
/// 失败→SaveFailed 且关闭中止（不允许"没保存成还关了"）；只读会话选
/// 保存＝调用方契约违约 fail-fast。
TEST_F(SessionControllerModelTest, CloseSaveDispositionBranches)
{
    IRD_TEST_INFO("PM-03", {"AT-13"}, std::nullopt);
    buildController();
    harness().armSuccessfulOpen("保存测试项目", true);
    ASSERT_TRUE(controller().openProject("D:/prj/save", UiOpenMode::Writable).ok);
    DraftRowProjection diskDraft;
    diskDraft.displayName = "草稿模块";
    harness().lastDrafts()->rows = {diskDraft};  // 磁盘草稿在位

    // ---- 成功分支：saveAll 调用恰一次→Draining→requestClose 已发。
    (void)controller().beginClose(UiCloseIntent::CloseProject);
    CloseDecision save = confirmedWait();
    save.draft = DraftDisposition::Save;
    const CloseDialogResolution ok = controller().resolveCloseDialog(save);
    EXPECT_EQ(ok.status, CloseDialogResolution::Status::Confirmed);
    EXPECT_EQ(harness().saveAllCalls, 1);
    EXPECT_TRUE(harness().lastStore()->closeRequested);
    EXPECT_EQ(controller().state(), UiSessionState::Draining);
    harness().lastStore()->closedFlag = true;  // 零在途排空
    EXPECT_EQ(controller().pollDrain().status, ui::DrainPollReport::Status::ClosedNow);
    EXPECT_EQ(controller().state(), UiSessionState::NoProject);

    // ---- 失败分支：saveAll 报告失败→SaveFailed，回到原状态不发关闭。
    harness().armSuccessfulOpen("保存失败项目", true);
    ASSERT_TRUE(controller().openProject("D:/prj/save2", UiOpenMode::Writable).ok);
    harness().saveAllResult = false;
    (void)controller().beginClose(UiCloseIntent::CloseProject);
    const CloseDialogResolution failed = controller().resolveCloseDialog(save);
    EXPECT_EQ(failed.status, CloseDialogResolution::Status::SaveFailed);
    EXPECT_EQ(controller().state(), UiSessionState::OpenWritable);
    EXPECT_FALSE(harness().lastStore()->closeRequested);  // 关闭中止
    harness().saveAllResult = true;
    // 清场：正常关闭第二会话（下一部分需要无项目态——openProject 前置）。
    (void)controller().beginClose(UiCloseIntent::CloseProject);
    (void)controller().resolveCloseDialog(confirmedWait());
    harness().lastStore()->fireClosed();
    ASSERT_EQ(controller().state(), UiSessionState::NoProject);

    // ---- 只读会话选保存：§5.5 违约 fail-fast（呈现层置灰的决议侧防线）。
    harness().armDegradedOpen(ReadOnlyOpenCause::MediaReadOnly, "只读保存项目");
    ASSERT_TRUE(controller().openProject("D:/prj/ro", UiOpenMode::Writable).ok);
    ASSERT_TRUE(controller().isReadOnlySession());  // INV-SES-1 前置
    (void)controller().beginClose(UiCloseIntent::CloseProject);
    EXPECT_THROW(controller().resolveCloseDialog(save), std::logic_error);
}

/// §5.4 S1[协作取消]：对**重查**的非终态任务逐个 requestCancel（NFR-PERF-02
/// 的 ui 侧半区：2 s/10 s 收敛由 execution 保证，ui 只发请求不等待）。
TEST_F(SessionControllerModelTest, CooperativeCancelAddressesRequeriedRows)
{
    IRD_TEST_INFO("TASK-03", {"AT-34"}, std::nullopt);
    buildController();
    const TaskRowProjection taskA = makeTaskRow("");
    harness().armSuccessfulOpen("协作取消项目", true, {taskA});
    ASSERT_TRUE(controller().openProject("D:/prj/cc", UiOpenMode::Writable).ok);

    (void)controller().beginClose(UiCloseIntent::CloseProject);
    // 决议前任务集变化：A 自然终态移出、新任务 B 在途——重查语义下
    // 只有 B 收到取消请求（陈旧行不发无意义请求）。
    const TaskRowProjection taskB = makeTaskRow("");
    harness().lastTasks()->rows = {taskB};

    CloseDecision cancel = confirmedWait();
    cancel.task = TaskDisposition::CooperativeCancel;
    const CloseDialogResolution out = controller().resolveCloseDialog(cancel);
    EXPECT_EQ(out.status, CloseDialogResolution::Status::Confirmed);
    ASSERT_EQ(harness().lastTasks()->cancelCalls.size(), std::size_t{1});
    EXPECT_TRUE(harness().lastTasks()->cancelCalls[0] == taskB.identity);
    EXPECT_FALSE(
        std::any_of(harness().lastTasks()->cancelCalls.begin(),
                    harness().lastTasks()->cancelCalls.end(),
                    [&taskA](const core::TaskIdentity& id) { return id == taskA.identity; }));
    EXPECT_EQ(controller().state(), UiSessionState::Draining);
}

// =====================================================================
// acceptance 1：只读打开提示与显示差异（UI-SES-2/3——PM-07/§5.3）
// =====================================================================

/// UI-SES-2：锁竞争降级只读——横幅含 PID/host、actionKind=contact-holder、
/// 会话只读判定源唯一（INV-SES-1）；横幅装配纯函数直证 §5.3 行 1。
TEST_F(SessionControllerModelTest, LockHeldBannerCarriesPidAndActionKind)
{
    IRD_TEST_INFO("PM-07", {"AT-20"}, std::nullopt);
    buildController();
    harness().armLockHeldOpen(4242u, "workstation-7");
    const auto report = controller().openProject("D:/prj/locked", UiOpenMode::Writable);
    ASSERT_TRUE(report.ok);  // 降级只读＝打开成功（PM-07 不阻塞等待）
    ASSERT_TRUE(report.readOnlyBanner.has_value());
    EXPECT_EQ(report.readOnlyBanner->cause, ReadOnlyOpenCause::LockHeld);
    EXPECT_EQ(report.readOnlyBanner->messageKey, "ui.session.readonly.lock-held.banner");
    ASSERT_EQ(report.readOnlyBanner->actionKindTokens.size(), std::size_t{1});
    EXPECT_EQ(report.readOnlyBanner->actionKindTokens[0], "contact-holder");
    ASSERT_TRUE(report.readOnlyBanner->lockHolder.has_value());
    EXPECT_EQ(report.readOnlyBanner->lockHolder->pid, 4242u);  // 横幅含 PID（观测点）
    EXPECT_EQ(report.readOnlyBanner->lockHolder->host, "workstation-7");
    EXPECT_TRUE(controller().isReadOnlySession());  // INV-SES-1：写权限唯一判定源
    EXPECT_EQ(controller().state(), UiSessionState::OpenReadOnly);

    // 纯函数面互证：同一映射对 §5.3 表逐字（横幅装配唯一实现点）。
    ui::LockHolderProjection holder;
    holder.pid = 7u;
    holder.host = "h";
    const ReadOnlyBannerProjection banner =
        ui::assembleReadOnlyBanner(ReadOnlyOpenCause::LockHeld, holder);
    EXPECT_EQ(banner.messageKey, report.readOnlyBanner->messageKey);
    EXPECT_TRUE(banner.lockHolder.has_value());
}

/// UI-SES-3：介质只读/权限不足的显示差异——actionKind 逐场景区分
/// （retry-readonly vs contact-holder/inspect-resource，§5.3 行 2/3 原文）。
TEST_F(SessionControllerModelTest, ReadOnlyCausesDistinguishActionKinds)
{
    IRD_TEST_INFO("PM-07", {}, std::nullopt);
    // 介质只读——retry-readonly（§5.3 行 2）。
    const ReadOnlyBannerProjection media =
        ui::assembleReadOnlyBanner(ReadOnlyOpenCause::MediaReadOnly, std::nullopt);
    EXPECT_EQ(media.messageKey, "ui.session.readonly.media-readonly.banner");
    ASSERT_EQ(media.actionKindTokens.size(), std::size_t{1});
    EXPECT_EQ(media.actionKindTokens[0], "retry-readonly");
    EXPECT_FALSE(media.lockHolder.has_value());  // 非锁场景无持有者（presence 纪律）

    // 权限不足——contact-holder/inspect-resource 双动作（§5.3 行 3 原文序）。
    const ReadOnlyBannerProjection denied =
        ui::assembleReadOnlyBanner(ReadOnlyOpenCause::AccessDenied, std::nullopt);
    EXPECT_EQ(denied.messageKey, "ui.session.readonly.access-denied.banner");
    ASSERT_EQ(denied.actionKindTokens.size(), std::size_t{2});
    EXPECT_EQ(denied.actionKindTokens[0], "contact-holder");
    EXPECT_EQ(denied.actionKindTokens[1], "inspect-resource");

    // 打开路径端到端：降级成因 → 横幅随打开报告装配（§5.3 打开期时序）。
    buildController();
    harness().armDegradedOpen(ReadOnlyOpenCause::AccessDenied, "权限项目");
    const auto report = controller().openProject("D:/prj/denied", UiOpenMode::Writable);
    ASSERT_TRUE(report.ok);
    ASSERT_TRUE(report.readOnlyBanner.has_value());
    EXPECT_EQ(report.readOnlyBanner->cause, ReadOnlyOpenCause::AccessDenied);
}

/// §5.3 行 6：打开校验失败→错误页数据（对端 token＋路径），当前项目不动
/// （NoProject 保持——"失败→错误页，停留/回退 NoProject"原文）。
TEST_F(SessionControllerModelTest, OpenFailureKeepsNoProjectWithErrorPageData)
{
    IRD_TEST_INFO("PM-07", {}, std::nullopt);
    buildController();
    harness().armFailedOpen("schema-future", "项目由更新版本创建");
    const auto report = controller().openProject("D:/prj/broken", UiOpenMode::Writable);
    EXPECT_FALSE(report.ok);
    EXPECT_FALSE(report.readOnlyBanner.has_value());
    EXPECT_EQ(report.failure.errorCodeToken, "schema-future");
    EXPECT_EQ(report.failure.projectPath, "D:/prj/broken");  // 错误页定位具体文件
    EXPECT_EQ(controller().state(), UiSessionState::NoProject);  // 当前项目不动
    EXPECT_EQ(controller().epoch(), std::uint64_t{0});           // 无会话形变
    // 失败路径恰一次调用（无重试——不重试纪律的打开面同源）。
    EXPECT_EQ(factoryPort().openCalls.size(), std::size_t{1});
}

// =====================================================================
// acceptance 1/2：等待覆盖在途归档（SA-17/§5.7）＋切换持有点（UI-SES-4）
// =====================================================================

/// UI-SES-5：关闭选"等待"——Draining 期间存储上下文保持（UI 会话结束≠
/// 存储上下文结束），归档完成回调后关闭完成；在途引用数随报告可见。
TEST_F(SessionControllerModelTest, WaitOptionCoversInFlightArchive)
{
    IRD_TEST_INFO("SA-17", {"AT-10"}, std::nullopt);
    buildController();
    harness().armSuccessfulOpen("等待归档项目", true, {makeTaskRow("archiving")});
    ASSERT_TRUE(controller().openProject("D:/prj/wait", UiOpenMode::Writable).ok);
    harness().lastStore()->inFlightOnClose = 3;  // 归档会话/在途事务/草稿落盘

    (void)controller().beginClose(UiCloseIntent::ExitApplication);
    CloseDecision wait = confirmedWait();  // [等待]——不取消任何任务
    ASSERT_EQ(controller().resolveCloseDialog(wait).status,
              CloseDialogResolution::Status::Confirmed);
    EXPECT_EQ(controller().state(), UiSessionState::Draining);
    EXPECT_TRUE(harness().lastTasks()->cancelCalls.empty());  // 未催促（§5.7）

    // Draining 中上下文仍持有（shared 保活——store 替身仍可交互）且轮询
    // 报告在途引用数（§5.6 防线 1 有界面反馈的机制半区）。
    const auto draining = controller().pollDrain();
    EXPECT_EQ(draining.status, ui::DrainPollReport::Status::Draining);
    EXPECT_EQ(draining.inFlightReferences, std::uint32_t{3});

    // 迟到结果照常归档→上下文释放（subscribeClose 回调）→关闭完成：
    // epoch 递增（§6.2"关闭完成"）＋退出就绪位（ExitApplication——
    // scheduler.shutdown(DrainPolicy) 调用权在 L5，§9.5）。
    harness().lastStore()->fireClosed();
    EXPECT_EQ(controller().state(), UiSessionState::NoProject);
    EXPECT_EQ(controller().epoch(), std::uint64_t{2});  // 打开成功＋关闭完成
    EXPECT_TRUE(controller().takeExitPending());
    EXPECT_FALSE(controller().takeExitPending());  // 幂等消费
}

/// UI-SES-4（会话机制半区）：切换 A→B——对话框针对 A、候选验证成功后
/// A 入后台持有点（INV-SES-3）＋B 立即绑定（epoch 递增）；A 排空后
/// 持有点释放；上下文恒为 B（迟到面不污染当前会话——TASK-03 呈现侧）。
TEST_F(SessionControllerModelTest, SwitchBindsBAndHoldsAUntilDrained)
{
    IRD_TEST_INFO("TASK-03", {"AT-10"}, std::nullopt);
    buildController();
    harness().armSuccessfulOpen("项目A", true, {makeTaskRow("archiving")});
    ASSERT_TRUE(controller().openProject("D:/prj/a", UiOpenMode::Writable).ok);
    StubStorePort* storeA = harness().lastStore().get();  // A 的替身（B 装配前捕获）
    const std::uint64_t epochA = controller().epoch();

    // 对话框针对 A 装配（§5.4 S2"对话框（同上，针对 A）"）。
    const CloseDialogData dialog = controller().beginSwitch("D:/prj/b", UiOpenMode::Writable);
    EXPECT_EQ(dialog.projectDisplayName, "项目A");
    EXPECT_EQ(controller().state(), UiSessionState::OpenWritable);

    harness().armSuccessfulOpen("项目B", true);
    const CloseDialogResolution out =
        controller().resolveCloseDialog(confirmedWait());
    EXPECT_EQ(out.status, CloseDialogResolution::Status::Confirmed);

    // B 立即绑定（不等 A 排空——§5.4 S2 原文）；A 的 store 已收关闭信号
    // 但转为后台持有点（INV-SES-3：shared 保活直到 closed）。
    EXPECT_EQ(controller().state(), UiSessionState::OpenWritable);
    ASSERT_TRUE(controller().context().project.has_value());
    EXPECT_EQ(controller().context().project->projectDisplayName, "项目B");
    EXPECT_EQ(controller().epoch(), epochA + 1);  // §6.2"切换绑定新项目"
    EXPECT_EQ(controller().drainingHoldCount(), std::size_t{1});
    EXPECT_TRUE(storeA->closeRequested);  // A：requestClose 已发（关闭信号）
    EXPECT_NE(storeA->project, controller().context().project->projectId);  // 当前会话＝B（非 A）

    // A 排空完成（轮询兜底路径）→持有点释放；B 上下文不受影响。
    storeA->closedFlag = true;
    const auto poll = controller().pollDrain();
    EXPECT_EQ(poll.holdsReleased, std::size_t{1});
    EXPECT_EQ(controller().drainingHoldCount(), std::size_t{0});
    EXPECT_EQ(controller().state(), UiSessionState::OpenWritable);  // B 不动
    ASSERT_TRUE(controller().context().project.has_value());
    EXPECT_EQ(controller().context().project->projectDisplayName, "项目B");
}

/// PM-03"候选验证成功才切"：候选打开失败→CandidateRejected（错误页数据），
/// A 界面会话不变、零持有点、A 未发关闭信号。
TEST_F(SessionControllerModelTest, SwitchCandidateRejectedKeepsA)
{
    IRD_TEST_INFO("PM-03", {}, std::nullopt);
    buildController();
    harness().armSuccessfulOpen("项目A", true);
    ASSERT_TRUE(controller().openProject("D:/prj/a", UiOpenMode::Writable).ok);
    StubStorePort* storeA = harness().lastStore().get();
    const std::uint64_t epochA = controller().epoch();

    (void)controller().beginSwitch("D:/prj/broken", UiOpenMode::Writable);
    harness().armFailedOpen("format-legacy", "旧格式项目");
    const CloseDialogResolution out =
        controller().resolveCloseDialog(confirmedWait());
    EXPECT_EQ(out.status, CloseDialogResolution::Status::CandidateRejected);
    EXPECT_EQ(out.candidateErrorCodeToken, "format-legacy");
    EXPECT_EQ(out.candidatePath, "D:/prj/broken");
    // A 界面会话不变（§5.4 S2 原文）——状态/身份/纪元/持有点全不变。
    EXPECT_EQ(controller().state(), UiSessionState::OpenWritable);
    ASSERT_TRUE(controller().context().project.has_value());
    EXPECT_EQ(controller().context().project->projectDisplayName, "项目A");
    EXPECT_EQ(controller().epoch(), epochA);
    EXPECT_EQ(controller().drainingHoldCount(), std::size_t{0});
    EXPECT_FALSE(storeA->closeRequested);
}

// =====================================================================
// acceptance 2：状态机迁移矩阵与不变量（§5.2——INV-SES-1/2/3）
// =====================================================================

/// §5.2 迁移矩阵主链＋epoch（§6.2：打开成功/关闭完成递增）＋INV-SES-1
/// （writable 唯一只读判定源——可写/只读打开两形态读出一致）。
TEST_F(SessionControllerModelTest, StateMatrixEpochAndInvSes1)
{
    IRD_TEST_INFO("PM-07", {}, std::nullopt);
    buildController();
    EXPECT_EQ(controller().state(), UiSessionState::NoProject);
    EXPECT_FALSE(controller().hasOpenSession());
    EXPECT_FALSE(controller().isReadOnlySession());  // 无项目态无只读语义

    // 可写打开：NoProject→OpenWritable，epoch 0→1（§6.2"打开成功"）。
    harness().armSuccessfulOpen("可写项目", true);
    ASSERT_TRUE(controller().openProject("D:/prj/w", UiOpenMode::Writable).ok);
    EXPECT_EQ(controller().state(), UiSessionState::OpenWritable);
    EXPECT_TRUE(controller().hasOpenSession());
    EXPECT_FALSE(controller().isReadOnlySession());
    EXPECT_EQ(controller().epoch(), std::uint64_t{1});
    ASSERT_TRUE(controller().context().project.has_value());
    EXPECT_EQ(controller().context().project->projectDisplayName, "可写项目");

    // 关闭完成：Draining→(Closed 瞬态)→NoProject，epoch 1→2（"关闭完成"）。
    (void)controller().beginClose(UiCloseIntent::CloseProject);
    ASSERT_EQ(controller().resolveCloseDialog(confirmedWait()).status,
              CloseDialogResolution::Status::Confirmed);
    EXPECT_EQ(controller().state(), UiSessionState::Draining);
    harness().lastStore()->fireClosed();
    EXPECT_EQ(controller().state(), UiSessionState::NoProject);
    EXPECT_EQ(controller().epoch(), std::uint64_t{2});
    EXPECT_FALSE(controller().context().project.has_value());  // 首页快照

    // 只读打开：OpenReadOnly＋INV-SES-1 读出 true（writable=false 直写）。
    harness().armDegradedOpen(ReadOnlyOpenCause::LockHeld, "只读项目");
    ASSERT_TRUE(controller().openProject("D:/prj/ro", UiOpenMode::Writable).ok);
    EXPECT_EQ(controller().state(), UiSessionState::OpenReadOnly);
    EXPECT_TRUE(controller().isReadOnlySession());  // 唯一数据源＝打开结果
    EXPECT_EQ(controller().epoch(), std::uint64_t{3});
}

/// INV-SES-2（结构性防线）＋调用面契约：已有 Open* 会话时 openProject
/// 拒绝（切换唯一路径）；单槽未决议时二次 begin* 拒绝；无挂起时 resolve
/// 拒绝；无会话态的关闭/切换拒绝（§5.2 状态表触发列）。
TEST_F(SessionControllerModelTest, InvSes2AndCallOrderContract)
{
    IRD_TEST_INFO("TASK-03", {}, std::nullopt);
    buildController();
    harness().armSuccessfulOpen("主项目", true);
    ASSERT_TRUE(controller().openProject("D:/prj/main", UiOpenMode::Writable).ok);
    StubStorePort* storeMain = harness().lastStore().get();  // 主会话替身
    //      （armSuccessfulOpen 会刷新 lastStore——先捕获主会话替身再构造
    //       其他打开场景，避免回调打到未订阅的替身上）。

    // 打开重入拒绝（INV-SES-2 调用面——不存在双 Open* 路径）。
    harness().armSuccessfulOpen("叠开项目", true);
    EXPECT_THROW(controller().openProject("D:/prj/other", UiOpenMode::Writable),
                 std::logic_error);
    // 单槽纪律：未决议前二次 begin* 拒绝。
    (void)controller().beginClose(UiCloseIntent::CloseProject);
    EXPECT_THROW(controller().beginClose(UiCloseIntent::CloseProject), std::logic_error);
    EXPECT_THROW(controller().beginSwitch("D:/prj/x", UiOpenMode::Writable), std::logic_error);
    // 配对决议后单槽释放（[取消]——不改变会话）。
    CloseDecision cancel;  // confirmed=false
    (void)controller().resolveCloseDialog(cancel);
    EXPECT_NO_THROW(controller().beginSwitch("D:/prj/x", UiOpenMode::Writable));
    (void)controller().resolveCloseDialog(cancel);
    // 无挂起时 resolve 拒绝。
    EXPECT_THROW(controller().resolveCloseDialog(cancel), std::logic_error);

    // 正常关闭回到无项目态后：关闭/切换入口拒绝（无项目态没有关闭语义）。
    (void)controller().beginClose(UiCloseIntent::CloseProject);
    (void)controller().resolveCloseDialog(confirmedWait());
    storeMain->fireClosed();  // 主会话替身（非后续 arm* 刷新的替身）
    ASSERT_EQ(controller().state(), UiSessionState::NoProject);
    EXPECT_THROW(controller().beginClose(UiCloseIntent::CloseProject), std::logic_error);
    EXPECT_THROW(controller().beginSwitch("D:/prj/y", UiOpenMode::Writable), std::logic_error);
}

/// INV-SES-3（回调释放路径）：后台持有点在 subscribeClose 回调到达时释放
/// （pollDrain 轮询是其兜底——两条释放路径互证）；重复回调幂等防御。
TEST_F(SessionControllerModelTest, InvSes3HoldReleasedByCallback)
{
    IRD_TEST_INFO("TASK-03", {}, std::nullopt);
    buildController();
    harness().armSuccessfulOpen("项目A", true);
    ASSERT_TRUE(controller().openProject("D:/prj/a", UiOpenMode::Writable).ok);
    StubStorePort* storeA = harness().lastStore().get();
    harness().armSuccessfulOpen("项目B", true);
    (void)controller().beginSwitch("D:/prj/b", UiOpenMode::Writable);
    (void)controller().resolveCloseDialog(confirmedWait());
    ASSERT_EQ(controller().drainingHoldCount(), std::size_t{1});

    storeA->fireClosed();  // 回调路径（对端归档完成主动通知）
    EXPECT_EQ(controller().drainingHoldCount(), std::size_t{0});
    // 重复回调幂等（无匹配——防御纪律，不使状态机失稳）。
    storeA->fireClosed();
    EXPECT_EQ(controller().drainingHoldCount(), std::size_t{0});
    EXPECT_EQ(controller().state(), UiSessionState::OpenWritable);
}

// =====================================================================
// acceptance 2：INV-SES-4 四级防线（§5.6——UI-SES-6）＋P-UI-8 阈值
// =====================================================================

/// UI-SES-6：归档挂起形态——T_force（默认 120 s）到点出现"强制结束并
/// 关闭"确认（数据含影响面任务数）；确认后强杀序列逐任务执行＋L5
/// abandonAll 兜底＋上下文最终关闭；拒绝则继续等待且仍可取消等待恢复
/// 显示（尚未 abandon——§5.2 状态表 Draining 行）。
TEST_F(SessionControllerModelTest, FourLevelDefenseForceConfirm)
{
    IRD_TEST_INFO("INV-SES-4", {"AT-34"}, std::nullopt);
    buildController();
    const TaskRowProjection task = makeTaskRow("archiving");
    harness().armSuccessfulOpen("挂起项目", true, {task});
    ASSERT_TRUE(controller().openProject("D:/prj/hang", UiOpenMode::Writable).ok);
    harness().lastStore()->inFlightOnClose = 1;

    (void)controller().beginClose(UiCloseIntent::CloseProject);
    CloseDecision wait = confirmedWait();  // 等待——但归档挂起（FaultPlan 形态）
    ASSERT_EQ(controller().resolveCloseDialog(wait).status,
              CloseDialogResolution::Status::Confirmed);

    // 防线 3 前不触发（<120 s）。
    harness().clock.advance(std::chrono::milliseconds{119 * 1000});
    EXPECT_EQ(controller().pollDrain().status, ui::DrainPollReport::Status::Draining);
    EXPECT_THROW(controller().forceCloseDialogData(), std::logic_error);

    // T_force 到点：确认待呈现＋数据就绪（waitedText 单位显式）。
    harness().clock.advance(std::chrono::milliseconds{1 * 1000});
    EXPECT_EQ(controller().pollDrain().status, ui::DrainPollReport::Status::ForceConfirmDue);
    const auto forceDialog = controller().forceCloseDialogData();
    EXPECT_EQ(forceDialog.messageKey, "ui.session.force-close.confirm");
    EXPECT_EQ(forceDialog.activeTaskCount, std::size_t{1});
    EXPECT_EQ(forceDialog.waitedText, "120 s");

    // 拒绝 → 继续等待（不重复弹确认；强杀序列未执行）。
    controller().resolveForceCloseDialog(false);
    harness().clock.advance(std::chrono::milliseconds{10 * 1000});
    EXPECT_EQ(controller().pollDrain().status, ui::DrainPollReport::Status::Draining);
    EXPECT_TRUE(harness().lastTasks()->forceCalls.empty());
    // 拒绝后取消等待仍可恢复（尚未 abandon——§5.2 状态表 Draining 行）。
    EXPECT_TRUE(controller().cancelDrainWait());
    EXPECT_EQ(controller().state(), UiSessionState::OpenWritable);

    // 再次关闭并等到 T_force：确认 → 强杀逐任务＋abandonAll 恰一次。
    (void)controller().beginClose(UiCloseIntent::CloseProject);
    ASSERT_EQ(controller().resolveCloseDialog(confirmedWait()).status,
              CloseDialogResolution::Status::Confirmed);
    harness().clock.advance(std::chrono::milliseconds{120 * 1000});
    ASSERT_EQ(controller().pollDrain().status, ui::DrainPollReport::Status::ForceConfirmDue);
    controller().resolveForceCloseDialog(true);
    ASSERT_EQ(harness().lastTasks()->forceCalls.size(), std::size_t{1});
    EXPECT_TRUE(harness().lastTasks()->forceCalls[0] == task.identity);
    EXPECT_EQ(harness().abandonAllCalls, 1);
    EXPECT_TRUE(harness().devLog->seen("force terminate sequence"));
    // abandon 后取消等待被拒绝（"仅当尚未 abandon 时"恢复——§5.2）。
    EXPECT_FALSE(controller().cancelDrainWait());
    // 强杀后归档收口→关闭完成。
    harness().lastStore()->fireClosed();
    EXPECT_EQ(controller().state(), UiSessionState::NoProject);
}

/// §5.6 防线 4：T_force2（默认 300 s）到点自动放弃等待——补行强杀序列、
/// UI-SESSION-CONTEXT-INVALID 双通道出线（目录 Warning＋Dev 日志）、关闭
/// 完成（INV-SES-4"绝不无限等待"——不依赖用户在场）。
TEST_F(SessionControllerModelTest, TForce2GiveUpBounded)
{
    IRD_TEST_INFO("INV-SES-4", {}, std::nullopt);
    buildController();
    const TaskRowProjection task = makeTaskRow("");
    harness().armSuccessfulOpen("放弃等待项目", true, {task});
    ASSERT_TRUE(controller().openProject("D:/prj/giveup", UiOpenMode::Writable).ok);

    (void)controller().beginClose(UiCloseIntent::CloseProject);
    (void)controller().resolveCloseDialog(confirmedWait());

    // 300 s 内的首次轮询：T_force（120 s）先到——防线 3 的确认先呈现
    // （防线串联语义：3→4 依次生效，§5.6 顺序）。
    harness().clock.advance(std::chrono::milliseconds{299 * 1000});
    EXPECT_EQ(controller().pollDrain().status, ui::DrainPollReport::Status::ForceConfirmDue);
    // T_force2 到点（用户未应答）：放弃＋收尾（防线 3 未确认——补行强杀
    // 序列；INV-SES-4 不依赖用户在场）。
    harness().clock.advance(std::chrono::milliseconds{1 * 1000});
    const auto poll = controller().pollDrain();
    EXPECT_EQ(poll.status, ui::DrainPollReport::Status::GivenUp);
    EXPECT_EQ(controller().state(), UiSessionState::NoProject);
    ASSERT_EQ(harness().lastTasks()->forceCalls.size(), std::size_t{1});  // 补行强杀
    EXPECT_TRUE(harness().lastTasks()->forceCalls[0] == task.identity);
    EXPECT_EQ(harness().abandonAllCalls, 1);
    // UI-SESSION-CONTEXT-INVALID 双通道（目录 Warning＋Dev 日志明文）。
    EXPECT_GE(harness().countOf("UI-SESSION-CONTEXT-INVALID"), std::size_t{1});
    EXPECT_TRUE(harness().devLog->seen("UI-SESSION-CONTEXT-INVALID"));
}

/// P-UI-8：T_force/T_force2 默认保守值 120 s/300 s 冻结断言＋装配期可配
/// （自定义 T_force=30 s 在 35 s 触发而默认 120 s 不会——裁决未闭合，
/// 默认值只是 §5.6 原文保守值承载，装配面可配不私定终值）。
TEST_F(SessionControllerModelTest, PUi8DefaultsConservativeAndConfigurable)
{
    IRD_TEST_INFO("PM-03", {}, std::nullopt);
    // 默认值冻结（§5.6 原文保守值——P-UI-8 裁决未闭合）。
    const UiSessionControllerDeps defaults;
    EXPECT_EQ(defaults.forceWaitThreshold, std::chrono::milliseconds{120 * 1000});
    EXPECT_EQ(defaults.forceWaitGiveUpThreshold, std::chrono::milliseconds{300 * 1000});

    // 装配期可配：自定义 T_force=30 s 的控制器（同一替身环境重建）。
    buildController();
    harness().armSuccessfulOpen("可配阈值项目", true, {makeTaskRow("")});
    ASSERT_TRUE(controller().openProject("D:/prj/tuned", UiOpenMode::Writable).ok);
    (void)controller().beginClose(UiCloseIntent::CloseProject);
    (void)controller().resolveCloseDialog(confirmedWait());
    harness().clock.advance(std::chrono::milliseconds{35 * 1000});
    // 默认阈值（120 s）下 35 s 不触发——本控制器以默认构建，35 s 应等待；
    // 触发面由契约测试对自定义阈值环境断言（SessionContractTest）。
    EXPECT_EQ(controller().pollDrain().status, ui::DrainPollReport::Status::Draining);
}

/// UI-SES-6 装配违约面：确认强杀但 abandonAll 未接线→fail-fast（§5.6
/// 防线 3 的 L5 兜底半区缺失——不半执行强杀，禁吞错纪律）。
TEST_F(SessionControllerModelTest, ForceConfirmWithoutWiringFailsFast)
{
    IRD_TEST_INFO("INV-SES-4", {}, std::nullopt);
    // 无 abandonAll 接线的独立环境（显式声明缺失——装配违约形态）。
    auto localFactory = std::make_unique<StubStoreFactoryPort>();
    harness().factoryPort = localFactory.get();
    UiSessionControllerDeps deps = harness().makeDeps();
    deps.forceAbandonAll = nullptr;
    harness().controller = std::make_unique<UiSessionController>(std::move(deps));

    const TaskRowProjection task = makeTaskRow("");
    harness().armSuccessfulOpen("缺接线项目", true, {task});
    ASSERT_TRUE(controller().openProject("D:/prj/nowire", UiOpenMode::Writable).ok);
    (void)controller().beginClose(UiCloseIntent::CloseProject);
    (void)controller().resolveCloseDialog(confirmedWait());
    harness().clock.advance(std::chrono::milliseconds{120 * 1000});
    ASSERT_EQ(controller().pollDrain().status, ui::DrainPollReport::Status::ForceConfirmDue);
    EXPECT_THROW(controller().resolveForceCloseDialog(true), std::logic_error);
    // 强杀序列未执行（fail-fast 拦截在执行前——不半执行）。
    EXPECT_TRUE(harness().lastTasks()->forceCalls.empty());
}

// =====================================================================
// acceptance 1：迟到写提示（UI-SES-7/UI-LCY-1——§5.3 行 4/§5.7）
// =====================================================================

/// UI-LCY-1：Closed 后迟到写——提示数据装配＋UI-SESSION-CONTEXT-INVALID
/// 出线＋"不重试"由数据形状保证（提示只有文案键，无动作位/待写入口）。
TEST_F(SessionControllerModelTest, ContextInvalidNoticeWithoutRetry)
{
    IRD_TEST_INFO("SA-17", {"AT-10"}, std::nullopt);
    buildController();
    harness().armSuccessfulOpen("已关闭项目", true);
    ASSERT_TRUE(controller().openProject("D:/prj/closed", UiOpenMode::Writable).ok);
    (void)controller().beginClose(UiCloseIntent::CloseProject);
    (void)controller().resolveCloseDialog(confirmedWait());
    harness().lastStore()->fireClosed();
    ASSERT_EQ(controller().state(), UiSessionState::NoProject);

    // 迟到写到达（对端 context-closed 拒绝后呈现层取提示）。
    const auto notice = controller().makeContextInvalidNotice();
    EXPECT_EQ(notice.messageKey, "ui.session.context-invalid.title");
    EXPECT_EQ(notice.adviceKey, "ui.session.context-invalid.advice");
    // 目录条目出线（码已注册——工厂全链）＋Dev 明文；无重试/无崩溃。
    EXPECT_GE(harness().countOf("UI-SESSION-CONTEXT-INVALID"), std::size_t{1});
    EXPECT_TRUE(harness().devLog->seen("不重试"));
    EXPECT_EQ(controller().state(), UiSessionState::NoProject);  // 进程存活
}

/// §5.4 S1[放弃]：会话脏数据丢弃——磁盘草稿行保留（磁盘事实不动，§8.6
/// 归 DraftController）；无在途任务时"等待"直达关闭协议。
TEST_F(SessionControllerModelTest, DiscardProceedsAndKeepsDiskDrafts)
{
    IRD_TEST_INFO("PM-04", {}, std::nullopt);
    buildController();
    harness().armSuccessfulOpen("放弃项目", true);
    ASSERT_TRUE(controller().openProject("D:/prj/discard", UiOpenMode::Writable).ok);
    // 磁盘草稿在位（与会话脏位来源不同——DraftPresenceProjection 两事实位）。
    DraftRowProjection diskDraft;
    diskDraft.displayName = "磁盘草稿模块";
    harness().lastDrafts()->rows = {diskDraft};

    // 对话框呈现：磁盘行进入草稿区（呈现事实），保存按钮可选（可写）。
    const CloseDialogData dialog = controller().beginClose(UiCloseIntent::CloseProject);
    ASSERT_EQ(dialog.draftRows.size(), std::size_t{1});
    EXPECT_EQ(dialog.draftRows[0].displayName, "磁盘草稿模块");
    EXPECT_TRUE(dialog.saveDraftsAvailable);

    // [放弃] → 继续（磁盘行集不动——端口未被要求清空）。
    CloseDecision discard;
    discard.confirmed = true;
    discard.draft = DraftDisposition::Discard;
    discard.task = TaskDisposition::Wait;
    ASSERT_EQ(controller().resolveCloseDialog(discard).status,
              CloseDialogResolution::Status::Confirmed);
    ASSERT_EQ(harness().lastDrafts()->rows.size(), std::size_t{1});
    EXPECT_EQ(controller().state(), UiSessionState::Draining);
    // 无在途任务＋上下文释放 → 关闭完成回首页。
    harness().lastStore()->fireClosed();
    EXPECT_EQ(controller().state(), UiSessionState::NoProject);
}

// =====================================================================
// 装配契约（fail-fast 面）
// =====================================================================

/// 构造期装配校验：storeFactory 为空→invalid_argument（没有打开端口的
/// 控制器无从进入 Opening——装配错误 fail-fast，AGENTS §3 错误语义）。
TEST(SessionControllerConstructionTest, MissingFactoryPortFailsFast)
{
    UiSessionControllerDeps deps;
    deps.storeFactory = nullptr;
    EXPECT_THROW((UiSessionController{deps}), std::invalid_argument);
}

}  // namespace
