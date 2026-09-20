/**
 * @file   WorkbenchShellGuiTest.cpp
 * @brief  UI-T03 GUI 层用例（QApplication＋真实 Widget 树——§12.1 第三层）：
 *         UI-WB-1（五区创建与跨会话布局记忆）、UI-WB-2（恢复默认布局）、
 *         UI-WB-3（布局记忆损坏回退＋Dev 诊断＋不阻塞启动）、UI-SES-1
 *         （无项目首页三入口与项目作用域命令禁用、命令面板可达）；另含
 *         最近项目模型（PM-10/PM-14）与状态栏 PM-11 的控件端到端断言。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T03.json acceptance 1~3（§4.1~§4.5 用例
 *     通过并经 ctest 留痕；状态栏格式；最小可用布局）；units/ui.md §12.3
 *     （UI-WB-1/2/3、UI-SES-1 用例行——前置/操作/预期/观测点逐条对应）；
 *   - §12.2 执行纪律：VS x64 环境＋QT_QPA_PLATFORM=windows（环境由运行方
 *     设置——main 不覆盖运行方选择）＋绝对路径单实例＋不用 offscreen；
 *   - ui.md §3.1（"ui 测试以可控替身承载"——IDiagnosticSink/IRedaction
 *     Service/端口以测试替身注入）、§4.5（损坏回退三件事：出厂默认＋
 *     UI-LAYOUT-RESTORE-FAILED（Dev）＋损坏段整段丢弃）、§4.6/PM-14
 *     （用户级设置——绝不写入 .rwdesign）；
 *   - UI-WB-1 的"重启"在进程内模拟：销毁壳实例后以同一用户级设置目录重建
 *     （跨会话记忆的存储语义在 QSettings 用户级文件——进程内重建等价验证
 *     恢复路径；真进程重启由 UI-T14 契约套件承载）。
 */

#include <gtest/gtest.h>

#include <QApplication>
#include <QDockWidget>
#include <QFileInfo>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTimer>

#include <sdurws/ird/diagnostics/Catalog.hpp>
#include <sdurws/ird/diagnostics/Logging.hpp>
#include <sdurws/ird/diagnostics/Redaction.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include <sdurws/ird/ui/IWorkbenchShell.hpp>
#include <sdurws/ird/ui/UiPorts.hpp>
#include <sdurws/ird/ui/UiProjections.hpp>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

using namespace sdurws::ird;
using sdurws::ird::ui::IWorkbenchShell;
using sdurws::ird::ui::ProjectContextProjection;
using sdurws::ird::ui::ProjectMetadataProjection;
using sdurws::ird::ui::ShellCommandAvailability;
using sdurws::ird::ui::ShellTeardownReport;
using sdurws::ird::ui::ShellWiring;
using sdurws::ird::ui::WorkbenchRegion;

// =====================================================================
// 测试替身（ui.md §3.1"ui 测试以可控替身承载"；§10.1 wiring 各指针非空）
// =====================================================================

/// 诊断 sink 替身：UI-T03 无用户级诊断产出（工厂注入随 UI-T13），append
/// 记数即可暴露"壳违规产条目"的意外行为。
class RecordingDiagnosticSink final : public diagnostics::IDiagnosticSink {
public:
    void append(diagnostics::DiagnosticEntry entry) override { m_appended.push_back(entry); }
    std::vector<diagnostics::DiagProjectionItem> snapshot(diagnostics::DiagQuery) const override
    {
        return {};
    }
    std::unique_ptr<diagnostics::ISubscription> subscribe(diagnostics::IDiagObserver&) override
    {
        return nullptr;  // 本套件不订阅（真实订阅链路随 UI-T13 替身细化）
    }
    std::string exportSafeSummary(diagnostics::DiagQuery, std::size_t) const override
    {
        return {};
    }
    const std::vector<diagnostics::DiagnosticEntry>& appended() const { return m_appended; }

private:
    std::vector<diagnostics::DiagnosticEntry> m_appended;
};

/// 脱敏服务替身：直通原文（本套件不产脱敏面内容；仅满足 wiring 非空前置）。
class PassthroughRedaction final : public diagnostics::IRedactionService {
public:
    std::string redact(std::string_view raw, diagnostics::LogTier) const noexcept override
    {
        return std::string(raw);
    }
    std::string redactPath(std::string_view rawPath) const noexcept override
    {
        return std::string(rawPath);
    }
    std::string safeSummary(std::string_view raw, std::size_t maxBytes) const noexcept override
    {
        std::string out(raw);
        if (maxBytes > 0 && out.size() > maxBytes) {
            out.resize(maxBytes);
        }
        return out;
    }
    void setPolicy(const diagnostics::RedactionPolicy&) override {}
};

/// 开发日志记录器：UI-WB-3 的诊断观测点——断言 UI-LAYOUT-RESTORE-FAILED
/// 经 Dev 通道出线（§6.2"Dev 走日志"；目录不可达 Dev——IDiagnosticSink
/// 替身同时保证 append 零命中）。
class DevLogRecorder final : public diagnostics::IDevLogSink {
public:
    void logDev(std::string_view channel, std::string message) override
    {
        m_entries.emplace_back(std::string(channel), std::move(message));
    }
    /// 是否记录了含给定码的条目（码在消息首段——emitDev 拼接格式）。
    bool seen(const std::string& code) const
    {
        return std::any_of(m_entries.begin(), m_entries.end(),
                           [&code](const auto& e) { return e.second.find(code) != std::string::npos; });
    }

private:
    std::vector<std::pair<std::string, std::string>> m_entries;
};

/// 策略摘要端口替身：默认投影（UI-T03 壳不消费策略面——仅满足非空前置）。
class FixedPolicySource final : public ui::IPolicySummarySource {
public:
    ui::PolicySummaryProjection summary() const override { return {}; }
};

/// 名称解析端口替身：恒不可解析（壳 UI-T03 不消费名称解析——对象树随
/// 后续任务；解析失败路径＝nullopt 即替身的真实行为）。
class NullNameResolver final : public ui::IUiNameResolver {
public:
    std::optional<std::string> resolveObjectId(core::ObjectId) const override
    {
        return std::nullopt;
    }
};

// =====================================================================
// 夹具：每用例独立用户级设置目录（PM-14 用户级存储的测试隔离面）
// =====================================================================

class WorkbenchShellGuiTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // 每用例独立临时目录并全局重定向 QSettings 用户级 Ini 路径——用例
        // 之间零设置串扰（ird_gui 串行单实例，进程级重定向安全；§12.2）。
        m_settingsDir = std::make_unique<QTemporaryDir>();
        ASSERT_TRUE(m_settingsDir->isValid());
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           m_settingsDir->path());
    }

    /// 组装 wiring（§10.1 前置：除 eventBus/devLog 外非空；二者为空＝显式
    /// 声明的无事件/无日志场景——本夹具恒注入 devLog 以支撑 UI-WB-3 观测）。
    ShellWiring makeWiring()
    {
        ShellWiring wiring;
        wiring.eventBus = nullptr;  // 允许为空＝无事件测试场景（显式声明）
        wiring.diagSink = m_diagSink;
        wiring.redaction = m_redaction;
        wiring.devLog = m_devLog;
        wiring.policySource = m_policySource;
        wiring.nameResolver = m_nameResolver;
        return wiring;
    }

    /// 当前用例的用户级设置文件全路径（QSettings setPath 语义：<dir>/<org>/<app>.ini）。
    QString settingsIniPath() const
    {
        return m_settingsDir->filePath(QString::fromLatin1("sdurws/ird-workbench.ini"));
    }

    /// 用例事件泵（布局/显示变更落定——不用固定 sleep，事件驱动判据）。
    static void pump() { QApplication::processEvents(); }

    std::unique_ptr<QTemporaryDir> m_settingsDir;
    std::shared_ptr<RecordingDiagnosticSink> m_diagSink = std::make_shared<RecordingDiagnosticSink>();
    std::shared_ptr<PassthroughRedaction> m_redaction = std::make_shared<PassthroughRedaction>();
    std::shared_ptr<DevLogRecorder> m_devLog = std::make_shared<DevLogRecorder>();
    std::shared_ptr<FixedPolicySource> m_policySource = std::make_shared<FixedPolicySource>();
    std::shared_ptr<NullNameResolver> m_nameResolver = std::make_shared<NullNameResolver>();
};

// =====================================================================
// UI-WB-1：五区布局创建与跨会话恢复（UX-09/PM-14；acceptance 1）
// =====================================================================

/**
 * §12.3 UI-WB-1 行：前置（干净用户设置）→ 操作（改形：隐藏两区＋浮动
 * 底区＋调整几何 → 拆卸重建模拟重启）→ 预期（五区齐备；位形还原；布局
 * 未写入任何项目目录）→ 观测点（Dock 可见性矩阵；用户设置文件；.rwdesign
 * 目录零触碰）。
 */
TEST_F(WorkbenchShellGuiTest, FiveZoneLayout_PersistsAcrossSessions_UI_WB_1)
{
    IRD_TEST_INFO("UX-09", {}, std::nullopt);
    // ---- 第一会话：五区齐备（出厂位形）＋用户改形 ----
    std::unique_ptr<IWorkbenchShell> shell = ui::createWorkbenchShell();
    ASSERT_TRUE(shell->initialize(makeWiring()));
    QWidget* window = shell->mainWindow();
    ASSERT_NE(window, nullptr);
    window->resize(1600, 900);
    window->show();
    pump();

    // 出厂可见性矩阵：五区全部在位（§4.1 布局总图；Top/Central 恒在）。
    EXPECT_TRUE(shell->regionVisible(WorkbenchRegion::Top));
    EXPECT_TRUE(shell->regionVisible(WorkbenchRegion::Left));
    EXPECT_TRUE(shell->regionVisible(WorkbenchRegion::Central));
    EXPECT_TRUE(shell->regionVisible(WorkbenchRegion::Right));
    EXPECT_TRUE(shell->regionVisible(WorkbenchRegion::Bottom));

    // 用户改形：隐藏左/右两区＋底区浮动＋几何调整（"拖动停靠/隐藏两区"）。
    shell->setRegionVisible(WorkbenchRegion::Left, false);
    shell->setRegionVisible(WorkbenchRegion::Right, false);
    QDockWidget* bottom = window->findChild<QDockWidget*>(QString::fromLatin1("ird_dock_bottom"));
    ASSERT_NE(bottom, nullptr);
    bottom->setFloating(true);
    window->resize(1520, 860);
    pump();
    const QSize geometryBefore = window->size();

    // 布局落盘拆窗（有界 shutdown——layoutPersisted 为用户级落盘证据）。
    const ShellTeardownReport teardown = shell->shutdown();
    ASSERT_TRUE(teardown.completed);
    EXPECT_TRUE(teardown.layoutPersisted);
    shell.reset();

    // ---- 第二会话（同用户设置目录＝跨会话）：位形还原 ----
    std::unique_ptr<IWorkbenchShell> restored = ui::createWorkbenchShell();
    ASSERT_TRUE(restored->initialize(makeWiring()));
    QWidget* window2 = restored->mainWindow();
    ASSERT_NE(window2, nullptr);
    window2->show();
    pump();

    // 可见性矩阵还原（用户隐藏的区保持隐藏——跨会话记忆的是用户意愿）。
    EXPECT_FALSE(restored->regionVisible(WorkbenchRegion::Left));
    EXPECT_FALSE(restored->regionVisible(WorkbenchRegion::Right));
    EXPECT_TRUE(restored->regionVisible(WorkbenchRegion::Top));
    EXPECT_TRUE(restored->regionVisible(WorkbenchRegion::Central));
    EXPECT_TRUE(restored->regionVisible(WorkbenchRegion::Bottom));

    // 浮动记忆还原（"清除该会话的浮动窗口记忆"只属于 resetLayout——
    // 跨会话恢复保留用户浮动位形，§4.4/§4.5 的语义边界）。
    QDockWidget* bottom2 = window2->findChild<QDockWidget*>(QString::fromLatin1("ird_dock_bottom"));
    ASSERT_NE(bottom2, nullptr);
    EXPECT_TRUE(bottom2->isFloating());

    // 几何还原（窗口管理器允许 ±8 px 微调——§4.4"8 px 内响应"同量级容差）。
    EXPECT_LE(std::abs(window2->size().width() - geometryBefore.width()), 8);
    EXPECT_LE(std::abs(window2->size().height() - geometryBefore.height()), 8);

    // 观测点①：布局写入用户级设置文件（PM-14——存在即用户级承载证据）。
    EXPECT_TRUE(QFileInfo::exists(settingsIniPath()));

    // 观测点②：.rwdesign 零写入（acceptance 2 红线——设置目录与工作目录
    // 均不得出现任何 .rwdesign 形态）。
    EXPECT_FALSE(QFileInfo::exists(m_settingsDir->filePath(QString::fromLatin1(".rwdesign"))));
    EXPECT_FALSE(QFileInfo::exists(
        QString::fromStdString(fs::current_path().string() + "/.rwdesign")));
    bool foundRwdesign = false;
    for (const auto& entry : fs::recursive_directory_iterator(
             m_settingsDir->path().toStdString())) {
        if (entry.path().filename().string().find(".rwdesign") != std::string::npos) {
            foundRwdesign = true;
        }
    }
    EXPECT_FALSE(foundRwdesign);

    restored->shutdown();
}

// =====================================================================
// UI-WB-2：恢复默认布局（UX-09；acceptance 1）
// =====================================================================

/**
 * §12.3 UI-WB-2 行：前置（UI-WB-1 形态的乱序布局）→ 操作（view.resetLayout
 * ＝壳层提交路径）→ 预期（出厂位形恢复；会话浮窗清除）→ 观测点（五区
 * 几何断言——可见性矩阵＋浮动标志）。
 */
TEST_F(WorkbenchShellGuiTest, ResetLayout_RestoresFactory_UI_WB_2)
{
    IRD_TEST_INFO("UX-09", {}, std::nullopt);
    std::unique_ptr<IWorkbenchShell> shell = ui::createWorkbenchShell();
    ASSERT_TRUE(shell->initialize(makeWiring()));
    QWidget* window = shell->mainWindow();
    ASSERT_NE(window, nullptr);
    window->resize(1600, 900);
    window->show();
    pump();

    // 乱序前置：隐藏右区＋底区浮动。
    shell->setRegionVisible(WorkbenchRegion::Right, false);
    QDockWidget* bottom = window->findChild<QDockWidget*>(QString::fromLatin1("ird_dock_bottom"));
    ASSERT_NE(bottom, nullptr);
    bottom->setFloating(true);
    pump();
    ASSERT_FALSE(shell->regionVisible(WorkbenchRegion::Right));
    ASSERT_TRUE(bottom->isFloating());

    // 操作：恢复默认布局（经壳层命令提交路径——与菜单/面板同一路由）。
    shell->resetLayout();
    pump();

    // 出厂位形：三可隐藏区恢复可见、浮动窗口清除（重新停靠）。
    EXPECT_TRUE(shell->regionVisible(WorkbenchRegion::Left));
    EXPECT_TRUE(shell->regionVisible(WorkbenchRegion::Right));
    EXPECT_TRUE(shell->regionVisible(WorkbenchRegion::Bottom));
    EXPECT_FALSE(bottom->isFloating());
    // 恒在三区不受影响（§4.4 最小可用布局）。
    EXPECT_TRUE(shell->regionVisible(WorkbenchRegion::Top));
    EXPECT_TRUE(shell->regionVisible(WorkbenchRegion::Central));

    shell->shutdown();
}

// =====================================================================
// UI-WB-3：布局记忆损坏回退＋Dev 诊断＋不阻塞启动（UX-09/§4.5）
// =====================================================================

/**
 * §12.3 UI-WB-3 行：前置（用户设置注入损坏段——版本不识别＋乱码载荷）→
 * 启动 → 预期（默认布局＋UI-LAYOUT-RESTORE-FAILED（Dev）；启动不阻塞）→
 * 观测点（Dev 日志含该码；损坏段整段丢弃；initialize 返回 true）。
 */
TEST_F(WorkbenchShellGuiTest, CorruptLayoutMemory_FallsBackWithDevDiagnostic_UI_WB_3)
{
    IRD_TEST_INFO("UI-LAYOUT-RESTORE-FAILED", {}, std::nullopt);
    // 前置：注入损坏段（版本不识别＝§4.5"版本不识别"触发；载荷乱码＝
    // "设置损坏"触发的同路径形态）。
    {
        QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                           QStringLiteral("sdurws"), QStringLiteral("ird-workbench"));
        settings.setValue(QStringLiteral("layout/version"), 999);
        settings.setValue(QStringLiteral("layout/geometry"), QByteArray("!!!corrupt!!!"));
        settings.setValue(QStringLiteral("layout/state"), QByteArray("@@@corrupt@@@"));
    }

    // 启动：必须成功返回（§4.5"不得阻塞启动"——initialize true 即未阻塞）。
    std::unique_ptr<IWorkbenchShell> shell = ui::createWorkbenchShell();
    ASSERT_TRUE(shell->initialize(makeWiring()));
    QWidget* window = shell->mainWindow();
    ASSERT_NE(window, nullptr);
    window->show();
    pump();

    // 默认布局：三可隐藏区全部恢复出厂可见。
    EXPECT_TRUE(shell->regionVisible(WorkbenchRegion::Left));
    EXPECT_TRUE(shell->regionVisible(WorkbenchRegion::Right));
    EXPECT_TRUE(shell->regionVisible(WorkbenchRegion::Bottom));

    // Dev 诊断出线（观测点）：码经开发日志通道（diag/ui）可见；同时用户
    // 目录（IDiagnosticSink 替身）零命中——Dev 码不入目录（§6.2 双面断言）。
    EXPECT_TRUE(m_devLog->seen("UI-LAYOUT-RESTORE-FAILED"));
    EXPECT_TRUE(m_diagSink->appended().empty());

    // 损坏段整段丢弃（§4.5 原文）：layout 组三键全部清除——下次保存从
    // 干净状态重建，不残留半损坏键。
    QSettings verify(QSettings::IniFormat, QSettings::UserScope,
                     QStringLiteral("sdurws"), QStringLiteral("ird-workbench"));
    EXPECT_FALSE(verify.contains(QStringLiteral("layout/version")));
    EXPECT_FALSE(verify.contains(QStringLiteral("layout/geometry")));
    EXPECT_FALSE(verify.contains(QStringLiteral("layout/state")));

    shell->shutdown();
}

// =====================================================================
// UI-SES-1：无项目首页与入口禁用（PM-10；acceptance 1）
// =====================================================================

/**
 * §12.3 UI-SES-1 行：前置（无项目）→ 启动 → 预期（三入口可见；项目作用域
 * 命令 availability.enabled=false；命令面板仍可搜索打开/新建）→ 观测点
 * （命令可用性查询；首页控件）。另含 PM-11 状态栏控件端到端（acceptance 2）
 * 与 §7.4"禁用＋说明"的发现性保留断言。
 */
TEST_F(WorkbenchShellGuiTest, NoProjectHomepage_CommandGating_UI_SES_1)
{
    IRD_TEST_INFO("PM-10", {}, std::nullopt);
    std::unique_ptr<IWorkbenchShell> shell = ui::createWorkbenchShell();
    ASSERT_TRUE(shell->initialize(makeWiring()));
    QWidget* window = shell->mainWindow();
    ASSERT_NE(window, nullptr);
    window->resize(1600, 900);
    window->show();
    pump();

    // 中央区切到首页页（无项目态——PM-10）。
    auto* central = window->findChild<QStackedWidget*>(QString::fromLatin1("ird_central_stack"));
    ASSERT_NE(central, nullptr);
    EXPECT_EQ(central->currentIndex(), 0);

    // 三入口可见且可点（新建/打开按钮＋最近项目入口组）。
    auto* newButton = window->findChild<QPushButton*>(QString::fromLatin1("ird_home_new"));
    auto* openButton = window->findChild<QPushButton*>(QString::fromLatin1("ird_home_open"));
    auto* recentGroup = window->findChild<QWidget*>(QString::fromLatin1("ird_home_recent"));
    ASSERT_NE(newButton, nullptr);
    ASSERT_NE(openButton, nullptr);
    ASSERT_NE(recentGroup, nullptr);
    EXPECT_TRUE(newButton->isVisibleTo(window));
    EXPECT_TRUE(openButton->isVisibleTo(window));
    EXPECT_TRUE(recentGroup->isVisibleTo(window));
    EXPECT_TRUE(newButton->isEnabled());
    EXPECT_TRUE(openButton->isEnabled());

    // 项目作用域命令：无项目 → enabled=false（界面使能态——§7.5；命令
    // 真正执行仍由目标服务校验，界面使能只是第一道闸）。
    const char* projectScoped[] = {"draft.save", "draft.apply", "project.undo", "project.redo"};
    for (const char* id : projectScoped) {
        const ShellCommandAvailability a = shell->commandAvailability(id);
        EXPECT_TRUE(a.registered) << id;
        EXPECT_FALSE(a.enabled) << id;
        EXPECT_FALSE(a.reasonKey.empty()) << id;  // "禁用＋说明"——§7.4
    }

    // 会话/视图作用域命令恒可达（PM-10"仅留项目菜单"的活动面）：命令面板
    // 可达（UI-T06 面板的可搜索门控面）＋新建/打开可触发（首页入口的命令侧）。
    const char* alwaysReachable[] = {"project.new", "project.open",
                                     "workbench.commandPalette", "view.resetLayout"};
    for (const char* id : alwaysReachable) {
        const ShellCommandAvailability a = shell->commandAvailability(id);
        EXPECT_TRUE(a.registered) << id;
        EXPECT_TRUE(a.visible) << id;
        EXPECT_TRUE(a.enabled) << id;
    }

    // 未登记 id：registered=false（壳板面不认识——不伪装可用性）。
    EXPECT_FALSE(shell->commandAvailability("no.such.cmd").registered);

    // ---- 打开（可写）项目：门控翻转＋状态栏 PM-11 ----
    ProjectContextProjection context;
    ProjectMetadataProjection metadata;
    metadata.projectId = core::ProjectId::generate();
    metadata.projectDisplayName = u8"演示项目";
    metadata.writable = true;
    context.project = metadata;
    shell->presentProjectContext(context);
    pump();

    EXPECT_EQ(central->currentIndex(), 1);  // 阶段占位面板（§4.1 阶段 A）
    EXPECT_TRUE(shell->commandAvailability("draft.save").enabled);

    auto* statusText = window->findChild<QLabel*>(QString::fromLatin1("ird_status_project_text"));
    ASSERT_NE(statusText, nullptr);
    EXPECT_EQ(statusText->text(), QString::fromUtf8(u8"演示项目"));
    // 标题栏同格式（PM-11"标题栏与状态栏"双面）。
    EXPECT_EQ(window->windowTitle(), QString::fromUtf8(u8"演示项目"));

    // ---- 只读项目：（只读）后缀＋徽标＋写命令再禁用（§7.6）----
    context.project->writable = false;
    shell->presentProjectContext(context);
    pump();
    EXPECT_EQ(statusText->text(), QString::fromUtf8(u8"演示项目（只读）"));
    EXPECT_FALSE(shell->commandAvailability("draft.save").enabled);
    auto* readonlyBadge = window->findChild<QLabel*>(QString::fromLatin1("ird_readonly_badge"));
    ASSERT_NE(readonlyBadge, nullptr);
    EXPECT_TRUE(readonlyBadge->isVisibleTo(window));

    // ---- 未应用修改：`*` 标记（会话脏标记输入）----
    context.drafts.sessionDirty = true;
    shell->presentProjectContext(context);
    pump();
    EXPECT_EQ(statusText->text(), QString::fromUtf8(u8"演示项目*（只读）"));

    shell->shutdown();
}

// =====================================================================
// 最近项目模型（PM-10/PM-14——acceptance 1/2 的承载面）
// =====================================================================

/**
 * PM-10 语义：上限 10、按规范路径去重（最近使用置顶）、失效项保留。
 * PM-14 语义：用户级持久化（跨壳实例存活——同设置目录重建即读回）。
 */
TEST_F(WorkbenchShellGuiTest, RecentProjects_CapDedupPersist_PM_10_PM_14)
{
    IRD_TEST_INFO("PM-10", {}, std::nullopt);
    std::unique_ptr<IWorkbenchShell> shell = ui::createWorkbenchShell();
    ASSERT_TRUE(shell->initialize(makeWiring()));

    // 12 个真实存在的候选目录（可用性按文件系统事实计算）。
    std::vector<std::string> paths;
    for (int i = 1; i <= 12; ++i) {
        const std::string dir =
            (m_settingsDir->filePath(QString::fromLatin1("proj%1").arg(i)).toStdString());
        fs::create_directories(fs::u8path(dir));
        paths.push_back(fs::weakly_canonical(fs::u8path(dir)).u8string());
    }
    for (const auto& path : paths) {
        shell->noteRecentProject(path);
    }

    // 上限 10：最早的 p01/p02 被裁掉（裁尾——最近使用序）。
    std::vector<ui::RecentProjectEntry> entries = shell->recentProjects();
    ASSERT_EQ(entries.size(), std::size_t{10});
    EXPECT_EQ(entries.front().canonicalPath, paths.back());          // 最新在前
    EXPECT_FALSE(std::any_of(entries.begin(), entries.end(), [&](const auto& e) {
        return e.canonicalPath == paths.front();
    }));

    // 去重＋置顶：重复 note 已在列路径 → 数量不变、位置提到最前。
    shell->noteRecentProject(paths[5]);
    entries = shell->recentProjects();
    ASSERT_EQ(entries.size(), std::size_t{10});
    EXPECT_EQ(entries.front().canonicalPath, paths[5]);

    // 规范化去重：同一目录的带冗余段形态与规范形同键（PM-10"按规范路径"）。
    const std::string redundant = paths[5] + "/./";
    shell->noteRecentProject(redundant);
    entries = shell->recentProjects();
    ASSERT_EQ(entries.size(), std::size_t{10});
    EXPECT_EQ(entries.front().canonicalPath, paths[5]);  // 折叠到同一规范条目

    // 失效项保留：磁盘删除后条目仍在、available 翻 false（PM-10 提示面）。
    fs::remove_all(fs::u8path(paths[3]));
    entries = shell->recentProjects();
    ASSERT_EQ(entries.size(), std::size_t{10});
    const auto stale = std::find_if(entries.begin(), entries.end(), [&](const auto& e) {
        return e.canonicalPath == paths[3];
    });
    ASSERT_NE(stale, entries.end());
    EXPECT_FALSE(stale->available);

    // 移除：条目消失（PM-10"移除"动作；幂等——重复移除无异常路径）。
    shell->removeRecentProject(paths[3]);
    entries = shell->recentProjects();
    ASSERT_EQ(entries.size(), std::size_t{9});
    EXPECT_TRUE(std::none_of(entries.begin(), entries.end(), [&](const auto& e) {
        return e.canonicalPath == paths[3];
    }));

    // 跨会话持久化（PM-14）：同设置目录重建壳 → 列表读回。
    shell->shutdown();
    shell = ui::createWorkbenchShell();
    ASSERT_TRUE(shell->initialize(makeWiring()));
    entries = shell->recentProjects();
    ASSERT_EQ(entries.size(), std::size_t{9});
    EXPECT_EQ(entries.front().canonicalPath, paths[5]);

    shell->shutdown();
}

}  // namespace
