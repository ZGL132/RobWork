/**
 * @file   CommandPaletteGuiTest.cpp
 * @brief  UI-T06 GUI 层用例（QApplication＋真实 Widget 树——§12.1 第三层）：
 *         命令面板键盘导航与可达性（UX-13/§7.4）、全局快捷键默认键触发
 *         （§7.3/§10.4——QShortcut 唯一创建点的端到端半区）、用户改绑的
 *         用户级设置跨会话生效（PM-14/§7.3）。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T06.json acceptance 1（快捷键冲突拒绝的
 *     GUI 半区＝模型层 ShortcutRegistryModelTest；此处钉物理键行为）、
 *     acceptance 2（模糊搜索与键盘导航；未绑定命令经面板可达；rebind/
 *     解绑为用户级设置持久化）；
 *   - units/ui.md §12.2 执行纪律：VS x64 环境＋QT_QPA_PLATFORM=windows
 *     （环境由运行方设置——main 不覆盖）＋绝对路径单实例；§12.3 行观测点；
 *   - §3.1"ui 测试以可控替身承载"（wiring 替身与 WorkbenchShellGuiTest
 *     同款纪律）。
 */

#include <gtest/gtest.h>

#include <QApplication>
#include <QFileInfo>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

#include <sdurws/ird/diagnostics/Catalog.hpp>
#include <sdurws/ird/diagnostics/Redaction.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include <sdurws/ird/ui/IWorkbenchShell.hpp>
#include <sdurws/ird/ui/UiPorts.hpp>
#include <sdurws/ird/ui/UiProjections.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace sdurws::ird;
using sdurws::ird::ui::IWorkbenchShell;
using sdurws::ird::ui::ShellWiring;

// =====================================================================
// 测试替身（WorkbenchShellGuiTest 同款纪律——§10.1 wiring 各指针非空）
// =====================================================================

class RecordingDiagnosticSink final : public diagnostics::IDiagnosticSink {
public:
    void append(diagnostics::DiagnosticEntry entry) override { m_appended.push_back(entry); }
    std::vector<diagnostics::DiagProjectionItem> snapshot(diagnostics::DiagQuery) const override
    {
        return {};
    }
    std::unique_ptr<diagnostics::ISubscription> subscribe(diagnostics::IDiagObserver&) override
    {
        return nullptr;
    }
    std::string exportSafeSummary(diagnostics::DiagQuery, std::size_t) const override
    {
        return {};
    }
    const std::vector<diagnostics::DiagnosticEntry>& appended() const { return m_appended; }

private:
    std::vector<diagnostics::DiagnosticEntry> m_appended;
};

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

class DevLogRecorder final : public diagnostics::IDevLogSink {
public:
    void logDev(std::string_view channel, std::string message) override
    {
        m_entries.emplace_back(std::string(channel), std::move(message));
    }
    bool seen(const std::string& code) const
    {
        return std::any_of(m_entries.begin(), m_entries.end(),
                           [&code](const auto& e) {
                               return e.second.find(code) != std::string::npos;
                           });
    }

private:
    std::vector<std::pair<std::string, std::string>> m_entries;
};

class FixedPolicySource final : public ui::IPolicySummarySource {
public:
    ui::PolicySummaryProjection summary() const override { return {}; }
};

class NullNameResolver final : public ui::IUiNameResolver {
public:
    std::optional<std::string> resolveObjectId(core::ObjectId) const override
    {
        return std::nullopt;
    }
};

// =====================================================================
// 夹具：每用例独立用户级设置目录（PM-14 测试隔离；ird_gui 串行单实例）
// =====================================================================

class CommandPaletteGuiTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_settingsDir = std::make_unique<QTemporaryDir>();
        ASSERT_TRUE(m_settingsDir->isValid());
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           m_settingsDir->path());
    }

    ShellWiring makeWiring()
    {
        ShellWiring wiring;
        wiring.eventBus = nullptr;  // 允许为空＝无事件测试场景（显式声明）
        wiring.diagSink = m_diagSink;
        wiring.redaction = m_redaction;
        wiring.devLog = m_devLog;
        wiring.diagFactory = nullptr;  // 允许为空＝无目录测试场景（显式声明）
        wiring.policySource = m_policySource;
        wiring.nameResolver = m_nameResolver;
        return wiring;
    }

    QString settingsIniPath() const
    {
        return m_settingsDir->filePath(QString::fromLatin1("sdurws/ird-workbench.ini"));
    }

    static void pump() { QApplication::processEvents(); }

    /// 组装并显示工作台（§12.2 真实窗口——快捷键 WindowShortcut 上下文需要
    /// 活动窗口：显式激活＋置活动窗口后泵事件；UI-WB 同款 show＋pump 形态
    /// 外加快捷键所需的活动窗口语义）。
    std::unique_ptr<IWorkbenchShell> makeShell()
    {
        auto shell = ui::createWorkbenchShell();
        EXPECT_TRUE(shell->initialize(makeWiring()));
        auto* window = shell->mainWindow();
        EXPECT_NE(window, nullptr);
        window->show();
        window->activateWindow();
        QApplication::setActiveWindow(window);
        pump();
        QTest::qWaitForWindowActive(window);
        return shell;
    }

    /// 命令面板探针（主窗口树内定位——objectName 契约）。
    static QWidget* findPalette(IWorkbenchShell& shell)
    {
        return shell.mainWindow()->findChild<QWidget*>(
            QString::fromLatin1("ird_command_palette"));
    }

    std::unique_ptr<QTemporaryDir> m_settingsDir;
    std::shared_ptr<RecordingDiagnosticSink> m_diagSink = std::make_shared<RecordingDiagnosticSink>();
    std::shared_ptr<PassthroughRedaction> m_redaction = std::make_shared<PassthroughRedaction>();
    std::shared_ptr<DevLogRecorder> m_devLog = std::make_shared<DevLogRecorder>();
    std::shared_ptr<FixedPolicySource> m_policySource = std::make_shared<FixedPolicySource>();
    std::shared_ptr<NullNameResolver> m_nameResolver = std::make_shared<NullNameResolver>();
};

// =====================================================================
// UI-PALETTE-1：默认键触发面板＋Esc 关闭（§7.3/§7.4——UX-13 键盘可达）
// =====================================================================

TEST_F(CommandPaletteGuiTest, PaletteOpensViaDefaultShortcut_UI_PALETTE_1)
{
    IRD_TEST_INFO("UX-13", {}, std::nullopt);
    auto shell = makeShell();
    auto* window = shell->mainWindow();
    QWidget* palette = findPalette(*shell);
    ASSERT_NE(palette, nullptr) << "命令面板应随壳装配创建";
    EXPECT_FALSE(palette->isVisible());

    // 默认键 Ctrl+Shift+P 触发（§7.1 默认表——QShortcut 唯一创建点的
    // 端到端半区：物理键 → registry.submit → 面板处理器）。
    QTest::keyClick(window->windowHandle(), Qt::Key_P, Qt::ControlModifier | Qt::ShiftModifier);
    pump();
    EXPECT_TRUE(palette->isVisible()) << "默认快捷键必须打开命令面板（workbench.commandPalette）";

    // Esc 关闭（§7.4 键盘导航表）。
    QTest::keyClick(palette->findChild<QLineEdit*>(
                        QString::fromLatin1("ird_command_palette_filter")),
                    Qt::Key_Escape);
    pump();
    EXPECT_FALSE(palette->isVisible());
}

// =====================================================================
// UI-PALETTE-2：模糊过滤＋键盘导航＋Enter 执行（§7.4 全键盘流）
// =====================================================================

TEST_F(CommandPaletteGuiTest, PaletteFilterNavigateExecute_UI_PALETTE_2)
{
    IRD_TEST_INFO("UX-13", {}, std::nullopt);
    auto shell = makeShell();
    auto* window = shell->mainWindow();
    QWidget* palette = findPalette(*shell);
    ASSERT_NE(palette, nullptr);
    auto* filter = palette->findChild<QLineEdit*>(QString::fromLatin1("ird_command_palette_filter"));
    auto* list = palette->findChild<QListWidget*>(QString::fromLatin1("ird_command_palette_list"));
    ASSERT_NE(filter, nullptr);
    ASSERT_NE(list, nullptr);

    // 打开面板（默认键）→ 全量快照非空（含未绑定命令——scheme.switch 无
    // 默认键，§7.3"无默认绑定经命令面板可达"）。
    QTest::keyClick(window->windowHandle(), Qt::Key_P, Qt::ControlModifier | Qt::ShiftModifier);
    pump();
    ASSERT_TRUE(palette->isVisible());
    EXPECT_GT(list->count(), 0);
    const QStringList allTexts = [list] {
        QStringList texts;
        for (int i = 0; i < list->count(); ++i) {
            texts << list->item(i)->text();
        }
        return texts;
    }();
    const bool unboundReachable = std::any_of(allTexts.cbegin(), allTexts.cend(),
                                              [](const QString& t) {
                                                  return t.contains(QString::fromUtf8("切换方案"));
                                              });
    EXPECT_TRUE(unboundReachable) << "未绑定命令必须经面板可达（acceptance 2；UX-13 兜底）";

    // 无项目态的写命令行＝"禁用＋说明"呈现（PM-10/§7.4 发现性保留）。
    const bool disabledWithReason = std::any_of(allTexts.cbegin(), allTexts.cend(),
                                                [](const QString& t) {
                                                    return t.contains(QString::fromUtf8("应用修改"))
                                                        && t.contains(QString::fromUtf8("不可用"));
                                                });
    EXPECT_TRUE(disabledWithReason) << "项目写命令在无项目态须以「禁用＋说明」呈现";

    // 模糊过滤："恢复" → 命中 view.resetLayout（标题过渡文案）。中文词无单键
    // 序列（QTest 键入仅覆盖 ASCII 键）——以 setText 走同一 textChanged
    // 触发链（被测面是过滤管线，非按键硬件路径）。
    filter->setText(QString::fromUtf8("恢复"));
    pump();
    ASSERT_GT(list->count(), 0);
    EXPECT_TRUE(list->item(0)->text().contains(QString::fromUtf8("恢复默认布局")))
        << "标题过渡文案命中置顶（§7.4 加权规则）";

    // Enter 执行当前选中（view.resetLayout 处理器＝壳自持语义——先把左栏
    // 藏掉，执行后面板收起且左栏恢复＝出厂位形，可从门面观测）。
    shell->setRegionVisible(sdurws::ird::ui::WorkbenchRegion::Left, false);
    EXPECT_FALSE(shell->regionVisible(sdurws::ird::ui::WorkbenchRegion::Left));
    QTest::keyClick(filter, Qt::Key_Return);
    pump();
    EXPECT_FALSE(palette->isVisible()) << "执行后面板收起";
    EXPECT_TRUE(shell->regionVisible(sdurws::ird::ui::WorkbenchRegion::Left))
        << "Enter 必须经 registry.submit 派发 view.resetLayout（统一提交路径）";
}

// =====================================================================
// UI-PALETTE-3：用户改绑跨会话生效（PM-14/§7.3——持久化装载＋默认覆盖）
// =====================================================================

TEST_F(CommandPaletteGuiTest, UserRebindPersistsAcrossSessions_UI_PALETTE_3)
{
    IRD_TEST_INFO("PM-14", {}, std::nullopt);
    // 会话 A 前：注入上一会话的用户改绑（载荷格式＝persistShortcutsAsync
    // 写出的 "commandId\\t键 PortableText"——restoreUserBindings 应用为
    // User 来源并覆盖默认键）。
    {
        QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                           QString::fromLatin1("sdurws"), QString::fromLatin1("ird-workbench"));
        settings.setValue(QString::fromLatin1("shortcuts/userBindings"),
                          QStringList{QString::fromUtf8("workbench.commandPalette\tCtrl+J")});
        settings.sync();
        ASSERT_TRUE(QFileInfo::exists(settingsIniPath()));
    }

    // 会话 A：改绑生效——Ctrl+J 打开、原默认 Ctrl+Shift+P 不再打开
    // （rebind 语义＝User 覆盖 Default，§7.3"一键一命令"双向唯一）。
    {
        auto shell = makeShell();
        auto* window = shell->mainWindow();
        QWidget* palette = findPalette(*shell);
        ASSERT_NE(palette, nullptr);
        QTest::keyClick(window->windowHandle(), Qt::Key_J, Qt::ControlModifier);
        pump();
        EXPECT_TRUE(palette->isVisible()) << "用户改绑键必须生效（PM-14 装载半区）";
        QTest::keyClick(palette->findChild<QLineEdit*>(QString::fromLatin1("ird_command_palette_filter")),
                        Qt::Key_Escape);
        pump();
        QTest::keyClick(window->windowHandle(), Qt::Key_P, Qt::ControlModifier | Qt::ShiftModifier);
        pump();
        EXPECT_FALSE(palette->isVisible())
            << "被覆盖的默认键不再触发（改绑＝替换而非并存，§7.3）";
        shell->shutdown();  // 关停兜底落盘（真实写路径回放当前 User 集）
    }

    // 会话 B：写路径往返——上一会话关停持久化的 User 集再次装载生效。
    {
        auto shell = makeShell();
        auto* window = shell->mainWindow();
        QWidget* palette = findPalette(*shell);
        ASSERT_NE(palette, nullptr);
        QTest::keyClick(window->windowHandle(), Qt::Key_J, Qt::ControlModifier);
        pump();
        EXPECT_TRUE(palette->isVisible()) << "跨会话往返后用户改绑仍生效（持久化闭环）";
    }
}

}  // namespace

