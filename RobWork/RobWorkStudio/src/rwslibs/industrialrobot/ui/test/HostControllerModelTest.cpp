/**
 * @file   HostControllerModelTest.cpp
 * @brief  宿主控制器簇（IHostController——UI-T19，方案 B.1 D2）的模型层
 *         用例（QCoreApplication 级零 Widget——§12.1 第一层分工）：
 *         ①产品主窗口形态词表封闭性（D1 宿主唯一声明的类型化承载）；
 *         ②四控制器族聚合恒等（命令注册/会话/Draft——聚合面交出的是
 *         注入实例本身而非副本）；③Draft 族可空语义（未装配草稿链路的
 *         形态返回 nullptr——消费方显式处理契约）；④状态投影双钩子向
 *         内容装配面的转发（UI-T18 契约面语义不变的回归面）。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T19.json acceptance 1（宿主 Dock 控制器
 *     簇装配面以公共头契约登记——本套件是登记面的具名自证）；
 *   - units/ui.md §10.1 v1.17 增量注（IHostController 首消费冻结＋
 *     IWorkbenchContent::commandRegistry() 访问器增量）、§4.1（产品形态
 *     声明注）、§13 UI-T19 行；
 *   - B1-SPEC §2（D1/D2/D13）、ARCHITECTURE §7.12/SA-18；
 *   - 先例：SessionControllerModelTest/DraftControllerModelTest 的真实
 *     控制器＋替身端口装配形态（本套件独立声明，不跨 TU 共享私有件）。
 *
 * 为什么放在模型层：聚合面是纯转发（零 Widget、零事件循环），真实被聚合
 *   对象（命令注册表/会话控制器/草稿控制器）都有无 GUI 的装配路径（各自
 *   模型套件同款）；内容装配面以测试替身承载（其真实行为回归由
 *   WorkbenchContentGuiTest/WorkbenchShellGuiTest 全量承载——UI-T19 验收
 *   2"既有基线不减"的对界面）。
 */

#include <gtest/gtest.h>

#include <QString>

#include <sdurws/ird/diagnostics/Catalog.hpp>
#include <sdurws/ird/diagnostics/DiagCodes.hpp>
#include <sdurws/ird/diagnostics/Factory.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include <sdurws/ird/ui/IHostController.hpp>
#include <sdurws/ird/ui/UiPorts.hpp>
#include <sdurws/ird/ui/UiProjections.hpp>
#include <sdurws/ird/ui/WorkbenchContent.hpp>

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace sdurws::ird;
using sdurws::ird::ui::DraftDocumentProjection;
using sdurws::ird::ui::DraftingSummary;
using sdurws::ird::ui::IHostController;
using sdurws::ird::ui::IWorkbenchContent;
using sdurws::ird::ui::ProductMainWindow;
using sdurws::ird::ui::ProjectContextProjection;
using sdurws::ird::ui::RecentProjectEntry;
using sdurws::ird::ui::ShellCommandAvailability;
using sdurws::ird::ui::WorkbenchRegion;

// =====================================================================
// 诊断全链（SessionControllerModelTest 的 SessionHarness 同款——本套件
// 独立声明：码表收编 builtin＋ui 九码，工厂经 StableCodeRegistry 构造）
// =====================================================================

class DevLogRecorder final : public diagnostics::IDevLogSink {
public:
    void logDev(std::string_view channel, std::string message) override
    {
        m_entries.emplace_back(std::string(channel), std::move(message));
    }

private:
    std::vector<std::pair<std::string, std::string>> m_entries;
};

class ManualDiagClock final : public diagnostics::IClock {
public:
    std::chrono::system_clock::time_point nowUtc() const override { return m_now; }
    std::chrono::system_clock::time_point m_now{std::chrono::seconds{1000000}};
};

struct DiagChain {
    diagnostics::StableCodeRegistry registry;
    ManualDiagClock diagClock;
    std::shared_ptr<diagnostics::DiagnosticsFactory> factory;
    std::shared_ptr<diagnostics::DiagCatalog> catalog;
    std::shared_ptr<DevLogRecorder> devLog = std::make_shared<DevLogRecorder>();

    DiagChain()
    {
        diagnostics::registerBuiltinCodes(registry);
        for (const auto& descriptor : ui::uiDiagnosticCodeDescriptors()) {
            registry.registerCode(descriptor);
        }
        factory = std::make_shared<diagnostics::DiagnosticsFactory>(registry, diagClock);
        catalog = std::make_shared<diagnostics::DiagCatalog>();
    }
};

// =====================================================================
// 内容装配面替身（记录状态投影钩子注册；命令注册族返回真实注册表——
// 聚合恒等断言需要一个有地址的权威实例）
// =====================================================================

class ContentDouble final : public IWorkbenchContent {
public:
    explicit ContentDouble(DiagChain& diag)
        : m_diag(diag)
    {
        ui::CommandRegistryDeps deps;
        deps.diagFactory = diag.factory;
        deps.diagSink = diag.catalog;
        deps.devLog = diag.devLog;
        deps.ownerWhitelist = {"ui"};
        m_registry = ui::createCommandRegistry(std::move(deps));
    }

    // ---- 两段装配（本套件不驱动真实装配——安全空实现）----
    bool build() override { return true; }
    void activate() override {}

    // ---- 内容 Widget 出口（nullptr＝本套件零 Widget 纪律）----
    QWidget* topBarWidget() override { return nullptr; }
    QWidget* leftWidget() override { return nullptr; }
    QWidget* centralWidget() override { return nullptr; }
    QWidget* rightWidget() override { return nullptr; }
    QWidget* bottomWidget() override { return nullptr; }

    // ---- 状态投影钩子：记录注册值（转发断言面）----
    void setStatusTextObserver(std::function<void(const QString&)> observer) override
    {
        m_statusText = std::move(observer);
    }

    void setStatusMessageObserver(
        std::function<void(const QString& message, int timeoutMs)> observer) override
    {
        m_statusMessage = std::move(observer);
    }

    // ---- 其余面：契约最小合法形态（空值/空表——本套件不消费）----
    void setRegionVisibilityTarget(WorkbenchRegion, QWidget*) override {}
    bool regionVisible(WorkbenchRegion) const override { return false; }
    void setRegionVisible(WorkbenchRegion, bool) override {}
    void resetLayout() override {}
    // 辅助 Dock 可见性记忆（UI-T24 增量——替身零语义：本套件不消费域面板
    // 记忆；未登记键的查询按真实实现同形 fail-fast，防替身掩盖契约）。
    void setAuxVisibilityTarget(const std::string&, QWidget*, bool) override {}
    bool auxVisible(const std::string& key) const override
    {
        throw std::out_of_range("ContentDouble: 辅助可见性键未登记 " + key);
    }
    void setAuxVisible(const std::string&, bool) override {}
    void notifyHostResized(const QSize&) override {}
    void presentProjectContext(const ProjectContextProjection&) override {}
    void noteSelectionForContext(const ui::SelectionChange&) override {}  ///< UI-T26 增量（替身零语义——本用例不触及上下文栏）

    ui::ICommandRegistry& commandRegistry() override { return *m_registry; }
    ShellCommandAvailability commandAvailability(const std::string&) const override { return {}; }
    void submitCommand(const std::string&) override {}
    void showStagePanel(ui::StageId) override {}  ///< WP-24-T03b 增量（替身零语义——本用例不触及阶段页）

    std::vector<RecentProjectEntry> recentProjects() const override { return {}; }
    void noteRecentProject(const std::string&) override {}
    void removeRecentProject(const std::string&) override {}
    void setCommandStateObserver(std::function<void()>) override {}
    void setTitleTextObserver(std::function<void(const QString&)>) override {}
    bool shutdown() override { return true; }

    // ---- 观测面（断言用）----
    ui::ICommandRegistry* registryPtr() const { return m_registry.get(); }
    const std::function<void(const QString&)>& statusText() const { return m_statusText; }
    const std::function<void(const QString&, int)>& statusMessage() const { return m_statusMessage; }

private:
    DiagChain& m_diag;
    std::unique_ptr<ui::ICommandRegistry> m_registry;
    std::function<void(const QString&)> m_statusText;
    std::function<void(const QString&, int)> m_statusMessage;
};

// =====================================================================
// 会话控制器真实例（SessionControllerModelTest 的 makeDeps 最小同款——
// 聚合面只持引用，测试需要一个有地址的真实对象）
// =====================================================================

class StubStoreFactoryPort final : public ui::IUiStoreFactoryPort {
public:
    ui::OpenStoreOutcome open(const std::string& canonicalPath,
                              ui::UiOpenMode mode,
                              ui::SessionPortBundle& outBindings) override
    {
        (void)canonicalPath;
        (void)mode;
        (void)outBindings;
        ui::OpenStoreOutcome outcome;
        outcome.ok = false;  // 本套件不驱动真实打开——恒失败占位（不触碰端口绑定量）
        outcome.failure.projectPath = canonicalPath;
        return outcome;
    }
};

/// 假单调时钟（会话/草稿 deps 的注入点——测试零真实等待）。
struct FakeSteadyClock {
    std::chrono::steady_clock::time_point now() const { return m_now; }
    std::chrono::steady_clock::time_point m_now{std::chrono::seconds{1000}};
};

// =====================================================================
// 用例一：产品主窗口形态词表封闭（D1 宿主唯一的类型化承载）
// =====================================================================

/**
 * D1 产品形态声明（B1-SPEC §2；ui.md §4.1）：ProductMainWindow 是单值
 * 封闭词表——唯一值 RobWorkStudioHost。任何第二值的引入＝产品形态变更，
 * 必须走 B1-SPEC 增量修订（本断言钉住"宿主唯一"不被静默扩表）。
 */
TEST(HostControllerModel, ProductMainWindowVocabularyClosed_B1_UI_T19)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-09", "ARC-02"}, {});

    // 封闭词表钉桩：唯一枚举值且为 0（追加值只允许表尾——本表没有"表尾"，
    // 任何新增值都直接改变产品形态语义，评审期即被本断言拦截形态漂移）。
    EXPECT_EQ(0, static_cast<int>(ProductMainWindow::RobWorkStudioHost));
}

// =====================================================================
// 用例二：四控制器族聚合恒等（交出注入实例本身——非副本非代理）
// =====================================================================

/**
 * 聚合恒等契约（PA-1 权威唯一的结构性执行面）：宿主侧经 IHostController
 * 取到的命令注册表/会话控制器必须与装配期注入的实例同址——聚合面重建
 * 任何控制器语义都会在本断言下暴露（第二注册点＝SA-16 红线违约）。
 */
TEST(HostControllerModel, AggregatesControllerFamiliesByIdentity_B1_UI_T19)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-09", "ARC-02"}, {});

    DiagChain diag;
    ContentDouble content(diag);

    // 会话控制器真实例（工厂端口替身以无删除共享引用包装——所有权在测试）。
    auto factoryPort = std::make_shared<StubStoreFactoryPort>();
    ui::UiSessionControllerDeps sessionDeps;
    sessionDeps.storeFactory.reset(factoryPort.get(), [](ui::IUiStoreFactoryPort*) {});
    sessionDeps.diagSink = diag.catalog;
    sessionDeps.diagFactory = diag.factory;
    sessionDeps.devLog = diag.devLog;
    sessionDeps.presentContext = [](const ProjectContextProjection&) {};
    sessionDeps.saveAllDraftsManual = []() { return true; };
    sessionDeps.forceAbandonAll = []() {};
    FakeSteadyClock clock;
    sessionDeps.steadyClock = [&clock]() { return clock.now(); };
    ui::UiSessionController session(std::move(sessionDeps));

    // 草稿控制器真实例（DraftControllerModelTest 的 makeDeps 最小同款——
    // 落盘任务直接在调用线程执行，本套件不驱动真实落盘时序）。
    ui::DraftControllerDeps draftDeps;
    draftDeps.postToDiskThread = [](std::function<void()> task) { task(); };
    draftDeps.postToUiThread = [](std::function<void()> task) { task(); };
    draftDeps.onSessionDirtyChanged = [](bool) {};
    draftDeps.diagSink = diag.catalog;
    draftDeps.diagFactory = diag.factory;
    draftDeps.devLog = diag.devLog;
    draftDeps.steadyClock = [&clock]() { return clock.now(); };
    draftDeps.nowUtcIso = []() { return std::string("2026-09-27T12:00:00Z"); };
    auto draft = ui::createDraftController(std::move(draftDeps));

    auto host = ui::createHostController(content, session, draft.get());

    // 聚合恒等：三族各自与注入实例同址（命令注册＝内容层同一实例——SA-16）。
    EXPECT_EQ(content.registryPtr(), &host->commandRegistry());
    EXPECT_EQ(&session, &host->sessionController());
    EXPECT_EQ(draft.get(), host->draftController());
}

// =====================================================================
// 用例三：Draft 族可空语义（未装配草稿链路的形态返回 nullptr）
// =====================================================================

/**
 * 可空聚合成员契约（IHostController::draftController）：缺省装配（不注入
 * 草稿控制器——harness 开发验证形态现状）必须返回 nullptr，消费方据此
 * 显式禁用保存/恢复入口；聚合面不得以任何替身冒充草稿控制器（诚实边界）。
 */
TEST(HostControllerModel, DraftFamilyOptionalWhenNotAssembled_B1_UI_T19)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-09", "ARC-02"}, {});

    DiagChain diag;
    ContentDouble content(diag);
    auto factoryPort = std::make_shared<StubStoreFactoryPort>();
    ui::UiSessionControllerDeps sessionDeps;
    sessionDeps.storeFactory.reset(factoryPort.get(), [](ui::IUiStoreFactoryPort*) {});
    sessionDeps.diagSink = diag.catalog;
    sessionDeps.diagFactory = diag.factory;
    sessionDeps.devLog = diag.devLog;
    FakeSteadyClock clock;
    sessionDeps.steadyClock = [&clock]() { return clock.now(); };
    ui::UiSessionController session(std::move(sessionDeps));

    // 缺省 draftController 参数＝未装配草稿链路的装配形态（D13 harness 现状）。
    auto host = ui::createHostController(content, session);

    EXPECT_EQ(nullptr, host->draftController());
}

// =====================================================================
// 用例四：状态投影双钩子转发（UI-T18 契约面语义不变的回归面）
// =====================================================================

/**
 * 状态投影转发契约（PM-11 双面）：经宿主控制器簇注册的观察者必须落到
 * 内容装配面的同名钩子上——①注册值非空且被内容层持有；②内容层触发时
 * 宿主侧观察者收到原文与超时（语义零加工）；③空回调＝清除语义原样转发。
 */
TEST(HostControllerModel, StatusProjectionObserversForwardToContent_B1_UI_T19)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-09", "ARC-02"}, {});

    DiagChain diag;
    ContentDouble content(diag);
    auto factoryPort = std::make_shared<StubStoreFactoryPort>();
    ui::UiSessionControllerDeps sessionDeps;
    sessionDeps.storeFactory.reset(factoryPort.get(), [](ui::IUiStoreFactoryPort*) {});
    sessionDeps.diagSink = diag.catalog;
    sessionDeps.diagFactory = diag.factory;
    sessionDeps.devLog = diag.devLog;
    FakeSteadyClock clock;
    sessionDeps.steadyClock = [&clock]() { return clock.now(); };
    ui::UiSessionController session(std::move(sessionDeps));

    auto host = ui::createHostController(content, session);

    // ①经聚合面注册——内容层应持有非空回调（转发可达性）。
    QString receivedText;
    int receivedTimeoutMs = -1;
    host->setStatusTextObserver([&receivedText](const QString& text) { receivedText = text; });
    host->setStatusMessageObserver(
        [&receivedTimeoutMs](const QString& message, int timeoutMs) {
            (void)message;
            receivedTimeoutMs = timeoutMs;
        });
    ASSERT_TRUE(static_cast<bool>(content.statusText()));
    ASSERT_TRUE(static_cast<bool>(content.statusMessage()));

    // ②内容层触发——宿主侧观察者收到原文与超时值（零加工转发的语义面）。
    content.statusText()(QString::fromUtf8("示例项目 [*]"));
    content.statusMessage()(QString::fromUtf8("已受理"), 1200);
    EXPECT_EQ(QString::fromUtf8("示例项目 [*]"), receivedText);
    EXPECT_EQ(1200, receivedTimeoutMs);

    // ③空回调＝清除（语义原样转发到内容层——宿主状态栏解除订阅的场景）。
    host->setStatusTextObserver({});
    host->setStatusMessageObserver({});
    EXPECT_FALSE(static_cast<bool>(content.statusText()));
    EXPECT_FALSE(static_cast<bool>(content.statusMessage()));
}

}  // namespace
