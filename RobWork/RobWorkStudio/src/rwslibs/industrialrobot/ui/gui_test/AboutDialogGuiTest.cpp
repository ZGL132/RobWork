/**
 * @file   AboutDialogGuiTest.cpp
 * @brief  UI-T10 GUI 层用例（QApplication＋真实 Widget 树——§12.1 第三层）：
 *         帮助入口与关于对话框的呈现行为——帮助命令路径（菜单动作→统一
 *         提交→处理器）、关于框同源渲染（版本区/插件清单与模型输出逐字
 *         一致）、无数据源占位形态（零虚构）、帮助手册缺失的用户可见反馈
 *         （UI-PLG-2 登记用例的 GUI 半区——ui.md §12.3）。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T10.json acceptance 1（UI-PLG-2：清单
 *     与白名单∩报告一致、版本与注入基线一致、帮助入口链接用户手册）/
 *     acceptance 2（O-31：版本数据 L5 注入——测试以 IUiAboutDataSource
 *     可控替身承载，替身注入值即"注入基线"的对照物）；
 *   - units/ui.md §11.4（关于框数据构成＋帮助入口）、§12.3 UI-PLG-2 行
 *     （前置/操作/预期/观测点——"对话框数据源断言"）、§12.2 执行纪律、
 *     §3.1（"ui 测试以可控替身承载"）；
 *   - 同构先例：WorkbenchShellGuiTest.cpp（夹具形态/替身清单/事件泵）、
 *     PolicySummaryCardGuiTest.cpp（模型同源互证口径）。
 *
 * 模型/GUI 分工：清单一致性/占位/占位文案在模型层（AboutDialogModelTest）
 * 逐行断言；本文件断言真实控件树的呈现与命令路径，并以"版本行/清单格与
 * 模型输出逐字同源"互证（UI-POL-1 同款口径）。
 */

#include <gtest/gtest.h>

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDesktopServices>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QSettings>
#include <QStatusBar>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QUrl>
#include <QWidget>

#include <sdurws/ird/diagnostics/Catalog.hpp>
#include <sdurws/ird/diagnostics/Redaction.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include <sdurws/ird/ui/AboutDialog.hpp>
#include <sdurws/ird/ui/IWorkbenchShell.hpp>
#include <sdurws/ird/ui/UiPorts.hpp>
#include <sdurws/ird/ui/UiProjections.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

/**
 * @brief UX-14：记录操作系统打开URL的边界；不替代手册文件或菜单命令。
 *
 * 用例在UI线程栈上持有，析构撤销全局处理器，防止影响后续用例和真实帮助。
 */
class ManualUrlRecorder final : public QObject {
    Q_OBJECT
public:
    /** @brief 在本用例期间接管本地文件打开请求；无文件写入或浏览器进程。 */
    ManualUrlRecorder() { QDesktopServices::setUrlHandler("file", this, "record"); }
    /** @brief 恢复系统默认打开行为；调用方不再持有本对象。 */
    ~ManualUrlRecorder() override { QDesktopServices::unsetUrlHandler("file"); }
    QUrl opened; ///< 最近打开的本地URL；仅UI线程读写，空值表示未调用系统服务。
public Q_SLOTS:
    /** @brief 记录菜单帮助请求；url [in] 必须由产品帮助入口给出。 */
    void record(const QUrl& url) { opened = url; }
};

namespace {

using namespace sdurws::ird;
using sdurws::ird::ui::AboutPluginRow;
using sdurws::ird::ui::AboutVersionBaseline;
using sdurws::ird::ui::AboutVersionRow;
using sdurws::ird::ui::IWorkbenchShell;
using sdurws::ird::ui::PluginAssemblyReport;
using sdurws::ird::ui::ShellWiring;
using sdurws::ird::ui::WorkbenchRegion;
using sdurws::ird::ui::aboutPluginRows;
using sdurws::ird::ui::aboutVersionRows;

// =====================================================================
// 测试替身（ui.md §3.1"ui 测试以可控替身承载"；与 WorkbenchShellGuiTest
// 同款清单——本文件独立持有，测试目标内不跨文件共享夹具）
// =====================================================================

/// 诊断 sink 替身：本套件无用户级诊断产出预期，append 记数暴露意外行为。
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

/// 脱敏服务替身：直通原文（仅满足 wiring 非空前置）。
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

/// 开发日志记录器：帮助手册缺失路径的 Dev 观测点（§6.2"Dev 走日志"）。
class DevLogRecorder final : public diagnostics::IDevLogSink {
public:
    void logDev(std::string_view channel, std::string message) override
    {
        m_entries.emplace_back(std::string(channel), std::move(message));
    }
    /// 是否记录了含给定片段的条目（手册缺失消息不含稳定码——按文本匹配）。
    bool seen(const std::string& fragment) const
    {
        return std::any_of(m_entries.begin(), m_entries.end(),
                           [&fragment](const auto& e) {
                               return e.second.find(fragment) != std::string::npos;
                           });
    }

private:
    std::vector<std::pair<std::string, std::string>> m_entries;
};

/// 策略摘要端口替身：默认投影（壳不消费策略面——仅满足非空前置）。
class FixedPolicySource final : public ui::IPolicySummarySource {
public:
    ui::PolicySummaryProjection summary() const override { return {}; }
};

/// 名称解析端口替身：恒不可解析（本套件不消费名称解析）。
class NullNameResolver final : public ui::IUiNameResolver {
public:
    std::optional<std::string> resolveObjectId(core::ObjectId) const override
    {
        return std::nullopt;
    }
};

/**
 * 关于框数据源替身（O-31 注入面的消费证据）：构造时给定报告集与基线，
 * 记录调用次数——help.about 处理器"打开时现取"的端口消费证据。
 */
class FakeAboutSource final : public ui::IUiAboutDataSource {
public:
    FakeAboutSource(std::vector<PluginAssemblyReport> reports, AboutVersionBaseline baseline)
        : m_reports(std::move(reports)), m_baseline(std::move(baseline))
    {
    }

    std::vector<PluginAssemblyReport> assemblyReports() const override
    {
        ++m_reportQueries;
        return m_reports;
    }
    AboutVersionBaseline versionBaseline() const override
    {
        ++m_baselineQueries;
        return m_baseline;
    }

    int reportQueries() const { return m_reportQueries; }
    int baselineQueries() const { return m_baselineQueries; }

private:
    std::vector<PluginAssemblyReport> m_reports;
    AboutVersionBaseline m_baseline;
    mutable int m_reportQueries = 0;
    mutable int m_baselineQueries = 0;
};

// =====================================================================
// 夹具：每用例独立用户级设置目录＋完整壳装配（帮助命令路径经真实壳）
// =====================================================================

class AboutDialogGuiTest : public ::testing::Test {
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

    /// 组装 wiring（§10.1 前置：除 eventBus 外非空；aboutSource 缺省为空
    /// ＝无关于数据场景，注入态用例先行 setAboutSource）。
    ShellWiring makeWiring()
    {
        ShellWiring wiring;
        wiring.eventBus = nullptr;  // 允许为空＝无事件测试场景（显式声明）
        wiring.diagSink = m_diagSink;
        wiring.redaction = m_redaction;
        wiring.devLog = m_devLog;
        wiring.policySource = m_policySource;
        wiring.nameResolver = m_nameResolver;
        wiring.aboutSource = m_aboutSource;  // 可空成员（UI-T10——缺省 nullptr）
        return wiring;
    }

    /// 创建并显示壳（initialize→mainWindow→show——帮助入口在真实菜单栏）。
    IWorkbenchShell* createShell()
    {
        m_shell = ui::createWorkbenchShell();
        EXPECT_TRUE(m_shell->initialize(makeWiring()));
        QWidget* window = m_shell->mainWindow();
        EXPECT_NE(window, nullptr);
        window->resize(1280, 800);
        window->show();
        pump();
        return m_shell.get();
    }

    /// 从菜单栏按动作文本定位 QAction（帮助入口的菜单形态——§4.1 六菜单；
    /// 菜单栏是 QMainWindow 成员——壳主窗口即 QMainWindow 子类）。
    static QAction* findMenuAction(QWidget& window, const QString& text)
    {
        auto* mainWindow = qobject_cast<QMainWindow*>(&window);
        if (mainWindow == nullptr) {
            return nullptr;
        }
        const QList<QAction*> actions = mainWindow->menuBar()->actions();
        for (QAction* menuAction : actions) {
            QMenu* menu = menuAction->menu();
            if (menu == nullptr) {
                continue;
            }
            const QList<QAction*> entries = menu->actions();
            for (QAction* entry : entries) {
                if (entry->text() == text) {
                    return entry;
                }
            }
        }
        return nullptr;
    }

    /// 主窗口树内定位关于框（objectName 契约锚）。
    static QDialog* findAboutDialog(IWorkbenchShell& shell)
    {
        return shell.mainWindow()->findChild<QDialog*>(
            QString::fromLatin1(ui::kAboutDialogObjectName));
    }

    /// 用例事件泵（显示/模态/动作派发落定——不用固定 sleep）。
    static void pump() { QApplication::processEvents(); }

    std::unique_ptr<QTemporaryDir> m_settingsDir;
    std::unique_ptr<IWorkbenchShell> m_shell;
    std::shared_ptr<RecordingDiagnosticSink> m_diagSink = std::make_shared<RecordingDiagnosticSink>();
    std::shared_ptr<PassthroughRedaction> m_redaction = std::make_shared<PassthroughRedaction>();
    std::shared_ptr<DevLogRecorder> m_devLog = std::make_shared<DevLogRecorder>();
    std::shared_ptr<FixedPolicySource> m_policySource = std::make_shared<FixedPolicySource>();
    std::shared_ptr<NullNameResolver> m_nameResolver = std::make_shared<NullNameResolver>();
    std::shared_ptr<FakeAboutSource> m_aboutSource;  ///< 缺省空＝无关于数据场景
};

// =====================================================================
// UI-PLG-2（注入态）：帮助命令路径＋关于框同源渲染
// =====================================================================

/**
 * 前置（白名单＋报告＋注入基线）→ 操作（菜单"关于"触发 help.about 统一
 * 提交路径）→ 预期（关于框呈现：版本区与注入基线一致、插件清单＝白名单
 * ∩报告）→ 观测点（对话框数据源断言——版本行/清单格与模型输出逐字同源；
 * 端口消费计数为注入承载证据）。
 */
TEST_F(AboutDialogGuiTest, AboutDialogShowsBaselineAndListSameSource_UI_PLG_2_UI_T10_ACC1)
{
    IRD_TEST_INFO("UX-14", {}, std::nullopt);

    // ---- 前置：注入基线（NFR-DEP-05 值形态替身）＋装配报告 ----
    AboutVersionBaseline baseline;
    baseline.available = true;
    baseline.productVersion = "2026.9-stageA";
    baseline.components = {{"设计框架", "5.0.0"}, {"界面框架", "6.11.1"}};
    PluginAssemblyReport okRow;
    okRow.pluginId = "kinematics";
    okRow.ok = true;
    okRow.panelsLoaded = 2;
    okRow.commandsRegistered = 3;
    m_aboutSource = std::make_shared<FakeAboutSource>(
        std::vector<PluginAssemblyReport>{okRow}, baseline);

    IWorkbenchShell* shell = createShell();
    ASSERT_NE(shell, nullptr);
    QWidget* window = shell->mainWindow();

    // ---- 操作：菜单"关于"触发（菜单动作→submitShellCommand→registry.
    //         submit→help.about 处理器——SA-16 统一提交路径的真实走通）。
    QAction* aboutAction = findMenuAction(*window, QString::fromUtf8(u8"关于"));
    ASSERT_NE(aboutAction, nullptr) << "帮助菜单缺少关于条目";
    aboutAction->trigger();
    pump();

    // ---- 预期：对话框打开（QDialog::open 非阻塞呈现——窗口模态语义）。
    QDialog* dialog = findAboutDialog(*shell);
    ASSERT_NE(dialog, nullptr) << "help.about 未打开关于框";
    EXPECT_TRUE(dialog->isVisible());

    // 端口消费证据：处理器打开时现取两半区数据（O-31 注入面的真实消费）。
    EXPECT_EQ(m_aboutSource->reportQueries(), 1);
    EXPECT_EQ(m_aboutSource->baselineQueries(), 1);

    // 版本区：行集与模型输出逐字同源（"标签：值"形态——GUI 只渲染）。
    const std::vector<AboutVersionRow> versionRows = aboutVersionRows(baseline);
    for (const AboutVersionRow& row : versionRows) {
        auto* label = window->findChild<QLabel*>(
            QString::fromLatin1(ui::kAboutVersionRowObjectNamePrefix)
            + QString::fromStdString(row.key));
        ASSERT_NE(label, nullptr) << "版本行缺失: " << row.key;
        const QString expected = QString::fromUtf8(row.label.c_str()) + u8"："
                                 + QString::fromUtf8(row.valueText.c_str());
        EXPECT_EQ(label->text(), expected) << "版本行与模型输出不同源: " << row.key;
    }
    // 注入一致性（UI-PLG-2"版本与注入基线一致"的呈现半区抽查）。
    auto* product = window->findChild<QLabel*>(
        QString::fromLatin1(ui::kAboutVersionRowObjectNamePrefix) + "product");
    ASSERT_NE(product, nullptr);
    EXPECT_TRUE(product->text().contains(QString::fromUtf8("2026.9-stageA")))
        << "产品版本行未呈现注入基线值: " << product->text().toStdString();

    // 插件清单：行集/单元格与模型输出逐字同源（行序＝白名单序）。
    auto* table = window->findChild<QTableWidget*>(
        QString::fromLatin1(ui::kAboutPluginTableObjectName));
    ASSERT_NE(table, nullptr) << "插件清单表缺失";
    const std::vector<AboutPluginRow> pluginRows =
        aboutPluginRows(m_aboutSource->assemblyReports());
    ASSERT_EQ(table->rowCount(), static_cast<int>(pluginRows.size()));
    for (int r = 0; r < table->rowCount(); ++r) {
        const AboutPluginRow& row = pluginRows[static_cast<std::size_t>(r)];
        ASSERT_NE(table->item(r, 0), nullptr);
        EXPECT_EQ(table->item(r, 0)->text(), QString::fromUtf8(row.titleText.c_str()))
            << "标题列不同源: " << row.pluginId;
        EXPECT_EQ(table->item(r, 1)->text(), QString::fromUtf8(row.versionText.c_str()));
        EXPECT_EQ(table->item(r, 2)->text().toStdString(), std::to_string(row.panelCount));
        EXPECT_EQ(table->item(r, 3)->text().toStdString(), std::to_string(row.commandCount));
        EXPECT_EQ(table->item(r, 4)->text(), QString::fromUtf8(row.statusText.c_str()));
    }
    // 清单与白名单一致（行数＝白名单大小；命中行充实——UI-PLG-2 字面）。
    EXPECT_EQ(table->rowCount(), 8);
    const int kinematicsRow = [&] {
        for (int r = 0; r < table->rowCount(); ++r) {
            if (table->item(r, 0)->text()
                == QString::fromUtf8(u8"运动学")) {
                return r;
            }
        }
        return -1;
    }();
    ASSERT_GE(kinematicsRow, 0) << "命中报告的插件行缺失";
    EXPECT_EQ(table->item(kinematicsRow, 4)->text(), QString::fromUtf8(u8"已装配"));

    // ---- 收尾：关闭并回收对话框（不留模态窗口干扰后续用例）。
    dialog->close();
    pump();
}

// =====================================================================
// UI-PLG-2（无数据源占位态）：零虚构
// =====================================================================

/**
 * wiring.aboutSource 为空（显式声明的无关于数据场景）：关于框照常打开，
 * 版本区单行「未装载」占位、清单八行白名单占位——零虚构数据（§11.4 不
 * 虚构业务能力在关于框的呈现形态；与模型层占位用例互证）。
 */
TEST_F(AboutDialogGuiTest, AboutDialogWithoutSourceShowsPlaceholders_UI_PLG_2_UI_T10)
{
    IRD_TEST_INFO("UX-14", {}, std::nullopt);
    // m_aboutSource 缺省为空——makeWiring 的 aboutSource=nullptr 即声明。

    IWorkbenchShell* shell = createShell();
    ASSERT_NE(shell, nullptr);
    QWidget* window = shell->mainWindow();

    QAction* aboutAction = findMenuAction(*window, QString::fromUtf8(u8"关于"));
    ASSERT_NE(aboutAction, nullptr);
    aboutAction->trigger();
    pump();

    QDialog* dialog = findAboutDialog(*shell);
    ASSERT_NE(dialog, nullptr) << "无数据源时关于框仍必须可用";
    EXPECT_TRUE(dialog->isVisible());

    // 版本区：仅"版本基线：未装载"单行（占位——不伪造版本号）。
    auto* baselineRow = window->findChild<QLabel*>(
        QString::fromLatin1(ui::kAboutVersionRowObjectNamePrefix) + "baseline");
    ASSERT_NE(baselineRow, nullptr) << "未装载占位行缺失";
    EXPECT_EQ(baselineRow->text(), QString::fromUtf8(u8"版本基线：未装载"));

    // 清单区：八行白名单占位（全"未装配"、版本列「不适用」）。
    auto* table = window->findChild<QTableWidget*>(
        QString::fromLatin1(ui::kAboutPluginTableObjectName));
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->rowCount(), 8);
    for (int r = 0; r < table->rowCount(); ++r) {
        EXPECT_EQ(table->item(r, 4)->text(), QString::fromUtf8(u8"未装配"))
            << "第 " << r << " 行装配状态非占位值";
        EXPECT_EQ(table->item(r, 1)->text(), QString::fromUtf8(u8"不适用"));
        EXPECT_EQ(table->item(r, 2)->text().toStdString(), "0");
        EXPECT_EQ(table->item(r, 3)->text().toStdString(), "0");
    }

    dialog->close();
    pump();
}

// =====================================================================
// 帮助手册入口（§11.4——缺失时的用户可见反馈）
// =====================================================================

/**
 * UX-14/U22：集成宿主须部署真实手册并将菜单请求发送到相同本地URL；
 * 独立模式没有产品部署目标，继续验证缺失时的用户反馈。两种模式分支
 * 均真实执行，不能把文件缺失当成集成交付合格。
 */
TEST_F(AboutDialogGuiTest, HelpContentsDeploymentAndFeedback_UX14_WP14T12)
{
    IRD_TEST_INFO("UX-14", {}, std::nullopt);

    const QString manual = QString::fromStdString(ui::userManualPath());
#ifdef IRD_UI_HAS_PRODUCT_HOST
    ASSERT_TRUE(QFileInfo::exists(manual)) << "正式宿主构建没有部署帮助文件："
                                         << manual.toStdString();
#endif
    const bool deployed = QFileInfo::exists(manual);
    ManualUrlRecorder recorder;

    IWorkbenchShell* shell = createShell();
    ASSERT_NE(shell, nullptr);
    QWidget* window = shell->mainWindow();

    QAction* contentsAction = findMenuAction(*window, QString::fromUtf8(u8"帮助手册"));
    ASSERT_NE(contentsAction, nullptr) << "帮助菜单缺少帮助手册条目";
    contentsAction->trigger();
    pump();

    // 用户可见反馈：状态栏呈现缺失说明（非空当前消息——即时可见性）。
    auto* statusBar = window->findChild<QStatusBar*>();
    ASSERT_NE(statusBar, nullptr);
    const QString message = statusBar->currentMessage();
    EXPECT_FALSE(message.isEmpty()) << "手册缺失无用户可见反馈";
    EXPECT_TRUE(message.contains(QString::fromUtf8(u8"用户手册")))
        << "缺失反馈未点明手册语义: " << message.toStdString();

    if (deployed) {
        // 文件内容与真实菜单出站地址共同核对，防止仅有空文件或错误帮助目标。
        QFile file(manual);
        ASSERT_TRUE(file.open(QIODevice::ReadOnly));
        const QByteArray html = file.readAll();
        for (const char* anchor : {"stations", "regions", "conditions", "drafts",
                                   "import-export", "templates", "view3d", "validation"}) {
            EXPECT_TRUE(html.contains(QByteArray("id=\"") + anchor + '"')) << anchor;
        }
        EXPECT_EQ(QFileInfo(recorder.opened.toLocalFile()).absoluteFilePath(),
                  QFileInfo(manual).absoluteFilePath());
        EXPECT_FALSE(m_devLog->seen("用户手册入口文件缺失"));
    } else {
        // 独立模式保留环境缺供的真实反馈，零系统打开请求。
        EXPECT_TRUE(recorder.opened.isEmpty());
        EXPECT_TRUE(m_devLog->seen("用户手册入口文件缺失"))
            << "手册缺失未留 Dev 排障明文";
    }

    // 诊断目录零产出：手册缺失不是 §3.5 登记事件（无稳定码——不虚构
    // 诊断条目；Dev 日志是唯一排障出线）。
    EXPECT_TRUE(m_diagSink->appended().empty());
}

}  // namespace

#include "AboutDialogGuiTest.moc"
