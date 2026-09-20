/**
 * @file   WorkbenchShell.cpp
 * @brief  工作台壳实现：五区 Dock 组装、跨会话布局记忆（含损坏回退＋Dev
 *         诊断）、无项目首页（PM-10 三入口）、PM-11 状态栏格式与壳层命令
 *         可用性门控。
 *
 * 设计依据：
 *   - units/ui.md §4.1~§4.6（五区布局总图/区域交互边界/空项目首页/响应式
 *     尺寸与最小可用布局/布局状态归属/用户级设置项）、§7.1/§7.4/§7.5/§7.6
 *     （壳层命令子集/NoProject 禁用口径/可用性谓词/只读条件）、§10.1
 *     （IWorkbenchShell 契约表——前置/后置/错误类型/线程/所有权/副作用）、
 *     §3.4（线程模型与后台落盘线程）、§3.5（UI-LAYOUT-RESTORE-FAILED 出线
 *     口径）、§11.4（不虚构业务能力——占位说明口径）；
 *   - 需求 UX-09/PM-10/PM-11/PM-14；任务契约 tasks/foundation/UI-T03.json
 *     acceptance 1~4（UI-WB-1/2/3、UI-SES-1；状态栏格式；最小可用布局＋
 *     门面契约；O-31/P-UI-5 处置）；
 *   - §14.1 交接清单（新建/打开向导编排归 workflow 阶段 B——本壳登记入口
 *     并以占位说明触发，不虚构能力）。
 *
 * 实现期口径登记（随 ui.md §10.1 v0.5 增量修订同步）：
 *   - 视图开关/顶栏按钮/菜单动作的默认快捷键一律不在此安装（§7.3 全局快捷
 *     键唯一注册点归 GlobalShortcutRegistry，UI-T06）——本壳只做可见/使能；
 *   - Dev 级码经 wiring.devLog 出线（§6.2"Dev 走日志"）；用户级诊断经
 *     IDiagnosticSink 需工厂产条目（工厂注入随 UI-T13 呈现模型落地），本壳
 *     当前不产用户级诊断条目——入口触发未接线时以状态栏说明反馈（§11.4
 *     不虚构业务能力）。
 */

#include "WorkbenchShell_p.hpp"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <sdurws/ird/ui/UiProjections.hpp>

namespace sdurws {
namespace ird {
namespace ui {
namespace detail {

// =====================================================================
// 主窗口：resize 钩子宿主（§4.4 尺寸折叠的入口）
// =====================================================================

void WorkbenchShellImpl::WorkbenchMainWindow::resizeEvent(QResizeEvent* event)
{
    // 先让 QMainWindow 完成自身布局，再把最终尺寸交给壳的折叠判别
    // （折叠读取的是本次事件后的 width/height）。
    QMainWindow::resizeEvent(event);
    if (resizeHook) {
        resizeHook(event);
    }
}

// =====================================================================
// 生命周期：initialize / shutdown（§10.1 契约表）
// =====================================================================

WorkbenchShellImpl::~WorkbenchShellImpl()
{
    // 兜底有界拆卸：L5 侧未显式 shutdown 即析构时保证布局落盘尝试与窗口
    // 销毁仍发生（正常路径 shutdown 已幂等收口——此处为空操作）。
    if (m_initialized) {
        shutdown();
    }
}

bool WorkbenchShellImpl::initialize(const ShellWiring& wiring)
{
    // 恰好一次（§10.1 前置条件）：二次调用＝调用方契约违约，走返回值轨
    // fail-fast（§10.1 错误类型行"返回 bool/report，不抛"）。
    if (m_initialized) {
        return false;
    }
    // 必填指针校验（§10.1 前置条件行：eventBus 之外一律非空；devLog 为
    // UI-T03 增量的可空成员——与 eventBus 同款"允许为空＝无日志测试场景，
    // 须显式声明"）。
    if (!wiring.diagSink || !wiring.redaction || !wiring.policySource
        || !wiring.nameResolver) {
        return false;
    }
    m_wiring = wiring;  // 共享引用持有（所有权仍在 L5——§10.1 所有权行）

    // 装配序（每步产物是下一步的输入）：
    //   ①窗口树与出厂快照 → ②最近项目装载 → ③布局记忆装载（损坏回退）
    //   → ④初始上下文（无项目首页＝PM-10 启动态）→ ⑤写盘线程启动。
    buildWindow();

    {
        // 读盘（UI 线程、写线程未启动——读写不重叠）：最近项目与布局同源
        // 用户级设置，但分属两个键组（布局损坏整段丢弃不波及最近项目）。
        QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                           kSettingsOrg, kSettingsApp);
        m_recent.load(settings.value(QString(kRecentGroup) + '/' + kRecentKey)
                          .toStringList());
    }

    restorePersistedLayout();
    refreshRecentList();

    // 启动即无项目首页（PM-10：壳启动时无会话，首页三入口就位）。
    presentProjectContext(ProjectContextProjection{});

    // 写线程在读盘完成后启动（§3.4 后台落盘线程；此后所有设置写入经队列）。
    m_settingsWriter.start();

    m_initialized = true;
    return true;
}

ShellTeardownReport WorkbenchShellImpl::shutdown()
{
    // 幂等（§10.1：shutdown 幂等——重复调用返回末次报告）。
    if (m_lastTeardown.completed) {
        return m_lastTeardown;
    }
    m_lastTeardown = ShellTeardownReport{};
    m_lastTeardown.wasInitialized = m_initialized;
    if (!m_initialized) {
        // 未初始化即拆卸：无布局可落盘、无窗口可销毁——按"完整走完的空
        // 拆卸"报告（不谎报 layoutPersisted）。
        m_lastTeardown.completed = true;
        return m_lastTeardown;
    }

    // ①布局落盘：值序列化在 UI 线程（Widget 尚存活），写盘交给后台线程，
    //   随后有界收口（§10.1"有界拆除"——队列里全是微秒级写任务，join 有界）。
    persistLayoutAsync();
    m_lastTeardown.layoutPersisted = m_settingsWriter.drainAndStop();

    // ②窗口销毁（§10.1 后置：shutdown 后 mainWindow 不可再用——唯一出口
    //   返回 nullptr；窗口树子指针随之失效，全部置空防悬垂）。
    m_window->close();
    m_window.reset();
    m_centralStack = nullptr;
    m_statusText = nullptr;
    m_readonlyBadge = nullptr;
    m_homeRecentList = nullptr;
    m_docks.fill(nullptr);
    m_commandActions.clear();
    m_topButtons.clear();
    m_viewToggles.clear();

    m_initialized = false;
    m_lastTeardown.completed = true;
    return m_lastTeardown;
}

// =====================================================================
// 装配段：窗口树组装（§4.1 布局总图）
// =====================================================================

void WorkbenchShellImpl::buildWindow()
{
    m_window = std::make_unique<WorkbenchMainWindow>();
    m_window->setObjectName("ird_workbench_main");
    m_window->resize(kFactoryWindowWidth, kFactoryWindowHeight);
    // §4.4 折叠判别挂在真实 resize 事件上（"8 px 内响应"——事件粒度即响应）。
    m_window->resizeHook = [this](QResizeEvent*) { updateCollapseBySize(); };

    buildMenus();
    buildTopBar();
    buildSideDocks();
    buildBottomDock();
    buildCentralArea();

    // 五区入位（枚举序＝addDockWidget 调用序；中央区＝主视图区——§4.1）。
    m_window->addDockWidget(Qt::TopDockWidgetArea, m_docks[static_cast<std::size_t>(WorkbenchRegion::Top)]);
    m_window->addDockWidget(Qt::LeftDockWidgetArea, m_docks[static_cast<std::size_t>(WorkbenchRegion::Left)]);
    m_window->addDockWidget(Qt::RightDockWidgetArea, m_docks[static_cast<std::size_t>(WorkbenchRegion::Right)]);
    m_window->addDockWidget(Qt::BottomDockWidgetArea, m_docks[static_cast<std::size_t>(WorkbenchRegion::Bottom)]);
    m_window->setCentralWidget(m_centralStack);

    // 状态栏（最小可用布局三要素之一——§4.4"状态行恒在"）：永久标签承载
    // PM-11 文本（不用 showMessage——那是临时消息位，会被超时清空）。
    m_statusText = new QLabel(m_window.get());
    m_statusText->setObjectName("ird_status_project_text");
    m_window->statusBar()->addWidget(m_statusText, /*stretch=*/1);

    // 出厂快照：此后 view.resetLayout / 损坏回退都以这两份字节为基准
    // （§4.4"恢复出厂位形"、§4.5"回退出厂默认布局"）。
    m_factoryGeometry = m_window->saveGeometry();
    m_factoryState = m_window->saveState();
}

void WorkbenchShellImpl::buildMenus()
{
    // 命令动作辅助：动作触发统一路由壳层提交路径（§4.2 红线——菜单/按钮
    // 不直接调用任何领域服务；UI-T03 阶段未接线的命令以状态栏说明反馈）。
    auto makeCommandAction = [this](const char* title, const char* commandId) {
        auto* action = new QAction(QString::fromUtf8(title), m_window.get());
        QObject::connect(action, &QAction::triggered, m_window.get(),
                [this, commandId] { submitShellCommand(commandId); });
        m_commandActions.emplace_back(action, commandId);
        return action;
    };

    // 文件（项目入口菜单——PM-10"无项目仅留项目菜单"的活动面）。
    QMenu* file = m_window->menuBar()->addMenu(QString::fromUtf8(WorkbenchText::kMenuFile));
    file->addAction(makeCommandAction(WorkbenchText::kCmdNewProject, "project.new"));
    file->addAction(makeCommandAction(WorkbenchText::kCmdOpenProject, "project.open"));
    file->addAction(makeCommandAction(WorkbenchText::kCmdCloseProject, "workbench.closeProject"));
    file->addSeparator();
    // 退出＝关闭主窗口（壳不拥有进程生命周期——进程退出归 L5 应用壳，
    // PM-17；窗口 close 会触发 L5 侧的关闭编排）。
    QAction* exitAction = file->addAction(QString::fromUtf8(WorkbenchText::kCmdExit));
    QObject::connect(exitAction, &QAction::triggered, m_window.get(), [this] {
        if (m_window) {
            m_window->close();
        }
    });

    // 编辑（撤销/重做——PM-18 入口；命令本体归 project 命令服务）。
    QMenu* edit = m_window->menuBar()->addMenu(QString::fromUtf8(WorkbenchText::kMenuEdit));
    edit->addAction(makeCommandAction(WorkbenchText::kCmdUndo, "project.undo"));
    edit->addAction(makeCommandAction(WorkbenchText::kCmdRedo, "project.redo"));

    // 视图（五区显示开关＋恢复默认布局；开关状态随壳状态回写——见
    // refreshCommandStates，triggered/toggled 分离避免回环）。
    QMenu* view = m_window->menuBar()->addMenu(QString::fromUtf8(WorkbenchText::kMenuView));
    const std::pair<WorkbenchRegion, const char*> toggles[] = {
        {WorkbenchRegion::Left, WorkbenchText::kToggleLeft},
        {WorkbenchRegion::Right, WorkbenchText::kToggleRight},
        {WorkbenchRegion::Bottom, WorkbenchText::kToggleBottom},
    };
    for (const auto& [region, title] : toggles) {
        auto* action = view->addAction(QString::fromUtf8(title));
        action->setCheckable(true);
        QObject::connect(action, &QAction::triggered, m_window.get(),
                [this, region] { setRegionVisible(region, !regionVisible(region)); });
        m_viewToggles.emplace_back(region, action);
    }
    view->addSeparator();
    view->addAction(makeCommandAction(WorkbenchText::kCmdResetLayout, "view.resetLayout"));

    // 阶段（导航归 §6.4/UI-T09——阶段 A 仅菜单位，条目禁用占位不虚构能力）。
    QMenu* stage = m_window->menuBar()->addMenu(QString::fromUtf8(WorkbenchText::kMenuStage));
    QAction* stagePlaceholder = stage->addAction(QString::fromUtf8(WorkbenchText::kStagePlaceholder));
    stagePlaceholder->setEnabled(false);

    // 工具（命令面板入口——面板本体随 UI-T06；入口命令恒可用＝PM-10
    // "命令面板仍可达"的门控面）。
    QMenu* tools = m_window->menuBar()->addMenu(QString::fromUtf8(WorkbenchText::kMenuTools));
    tools->addAction(makeCommandAction(WorkbenchText::kCmdPalette, "workbench.commandPalette"));

    // 帮助（关于/内容随 UI-T10——阶段 A 仅菜单位）。
    QMenu* help = m_window->menuBar()->addMenu(QString::fromUtf8(WorkbenchText::kMenuHelp));
    QAction* helpPlaceholder = help->addAction(QString::fromUtf8(WorkbenchText::kStagePlaceholder));
    helpPlaceholder->setEnabled(false);
}

void WorkbenchShellImpl::buildTopBar()
{
    // 顶栏 Dock（§4.1 顶栏行）：恒在三区之一——不可关闭/浮动（§4.4"顶栏
    // 恒在"），仅可移动；标题栏隐藏（工具条形态，非面板形态）。
    auto* topDock = new QDockWidget(QString::fromUtf8(WorkbenchText::kDockTop), m_window.get());
    topDock->setObjectName("ird_dock_top");
    topDock->setFeatures(QDockWidget::DockWidgetMovable);
    topDock->setTitleBarWidget(new QWidget());  // 空 titular＝无标题栏

    auto* bar = new QWidget(topDock);
    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(4, 2, 4, 2);

    // 项目入口▾（下拉：新建/打开/关闭——与文件菜单同命令，§4.2 路由红线
    // 同样适用：只经壳层提交路径）。
    auto* projectEntry = new QToolButton(bar);
    projectEntry->setText(QString::fromUtf8(WorkbenchText::kMenuFile) + QString::fromUtf8(u8"入口"));
    projectEntry->setPopupMode(QToolButton::InstantPopup);
    QMenu* entryMenu = new QMenu(projectEntry);
    entryMenu->addAction(QString::fromUtf8(WorkbenchText::kCmdNewProject), projectEntry,
                         [this] { submitShellCommand("project.new"); });
    entryMenu->addAction(QString::fromUtf8(WorkbenchText::kCmdOpenProject), projectEntry,
                         [this] { submitShellCommand("project.open"); });
    entryMenu->addAction(QString::fromUtf8(WorkbenchText::kCmdCloseProject), projectEntry,
                         [this] { submitShellCommand("workbench.closeProject"); });
    projectEntry->setMenu(entryMenu);
    layout->addWidget(projectEntry);

    // 阶段导航条/当前方案工况指示/任务状态指示：§4.1 顶栏行的阶段 A 占位
    // （阶段导航归 UI-T09、方案工况随项目流程任务、任务状态随 UI-T13——
    // 占位说明不虚构能力，§11.4）。三处共用同一占位说明文本。
    const QString topPlaceholder =
        QString::fromUtf8(u8"阶段导航（本阶段将在后续版本提供）");
    QLabel* stageNav = new QLabel(topPlaceholder, bar);
    layout->addWidget(stageNav);
    QLabel* scheme = new QLabel(topPlaceholder, bar);
    layout->addWidget(scheme);

    // 写命令按钮组（§4.2 顶栏行：保存草稿/应用修改/撤销/重做——全部路由
    // 命令板，可用性随上下文刷新）。
    const std::pair<const char*, const char*> buttons[] = {
        {WorkbenchText::kCmdSaveDraft, "draft.save"},
        {WorkbenchText::kCmdApplyDraft, "draft.apply"},
        {WorkbenchText::kCmdUndo, "project.undo"},
        {WorkbenchText::kCmdRedo, "project.redo"},
    };
    for (const auto& [title, commandId] : buttons) {
        auto* button = new QPushButton(QString::fromUtf8(title), bar);
        QObject::connect(button, &QPushButton::clicked, bar,
                [this, commandId] { submitShellCommand(commandId); });
        m_topButtons.emplace_back(button, commandId);
        layout->addWidget(button);
    }

    QLabel* taskState = new QLabel(topPlaceholder, bar);
    layout->addWidget(taskState);

    // 只读徽标（PM-07/§4.2 顶栏行）：可见性随上下文（refreshCommandStates）。
    m_readonlyBadge = new QLabel(QString::fromUtf8(u8"只读"), bar);
    m_readonlyBadge->setObjectName("ird_readonly_badge");
    m_readonlyBadge->setVisible(false);
    layout->addWidget(m_readonlyBadge);

    layout->addStretch(1);  // 其余控件靠左，徽标后弹性收尾
    bar->setLayout(layout);
    topDock->setWidget(bar);
    m_docks[static_cast<std::size_t>(WorkbenchRegion::Top)] = topDock;
}

void WorkbenchShellImpl::buildSideDocks()
{
    // 左栏（§4.2 左栏行：项目对象树＋阶段任务列表——对象树需查询端口＋
    // 名称解析消费（阶段 B）、任务列表由阶段就绪投影驱动（UI-T09）；阶段 A
    // 占位说明，§11.4 不虚构能力）。最小内容宽 240 px（§4.4 原文数值）。
    auto* leftDock = new QDockWidget(QString::fromUtf8(WorkbenchText::kDockLeft), m_window.get());
    leftDock->setObjectName("ird_dock_left");
    leftDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable
                          | QDockWidget::DockWidgetClosable);
    auto* leftContent = new QWidget(leftDock);
    leftContent->setMinimumWidth(kLeftDockMinWidth);
    auto* leftLayout = new QVBoxLayout(leftContent);
    leftLayout->addWidget(new QLabel(u8"项目对象树（本阶段将在后续版本提供）", leftContent));
    leftLayout->addWidget(new QLabel(u8"阶段任务列表（本阶段将在后续版本提供）", leftContent));
    leftLayout->addStretch(1);
    leftDock->setWidget(leftContent);
    m_docks[static_cast<std::size_t>(WorkbenchRegion::Left)] = leftDock;

    // 右栏（§4.2 右栏行：属性编辑区＋诊断与设置区——属性编辑阶段 B 域
    // 编辑器、诊断摘要 UI-T13、策略摘要入口 UI-T07）。最小内容宽 280 px。
    auto* rightDock = new QDockWidget(QString::fromUtf8(WorkbenchText::kDockRight), m_window.get());
    rightDock->setObjectName("ird_dock_right");
    rightDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable
                           | QDockWidget::DockWidgetClosable);
    auto* rightContent = new QWidget(rightDock);
    rightContent->setMinimumWidth(kRightDockMinWidth);
    auto* rightLayout = new QVBoxLayout(rightContent);
    rightLayout->addWidget(new QLabel(u8"属性编辑区（本阶段将在后续版本提供）", rightContent));
    rightLayout->addWidget(new QLabel(u8"诊断与设置区（本阶段将在后续版本提供）", rightContent));
    rightLayout->addWidget(new QLabel(u8"工程策略摘要入口（本阶段将在后续版本提供）", rightContent));
    rightLayout->addStretch(1);
    rightDock->setWidget(rightContent);
    m_docks[static_cast<std::size_t>(WorkbenchRegion::Right)] = rightDock;
}

void WorkbenchShellImpl::buildBottomDock()
{
    // 底部任务和状态区（§4.2 底部行：五页签——结果/任务进度/诊断/日志/
    // 下一步建议。呈现模型随 UI-T13、状态词随 UI-T04、下一步建议由
    // workflow 提供（阶段 A 占位——§4.2 底部行明示）。最小内容高 160 px。
    auto* bottomDock = new QDockWidget(QString::fromUtf8(WorkbenchText::kDockBottom), m_window.get());
    bottomDock->setObjectName("ird_dock_bottom");
    bottomDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable
                            | QDockWidget::DockWidgetClosable);
    auto* tabs = new QTabWidget(bottomDock);
    tabs->setMinimumHeight(kBottomDockMinHeight);
    const char* tabTitles[] = {
        WorkbenchText::kTabResults, WorkbenchText::kTabTasks, WorkbenchText::kTabDiagnostics,
        WorkbenchText::kTabLog, WorkbenchText::kTabAdvice,
    };
    for (const char* title : tabTitles) {
        auto* page = new QLabel(u8"本阶段将在后续版本提供", tabs);
        page->setAlignment(Qt::AlignCenter);
        tabs->addTab(page, QString::fromUtf8(title));
    }
    bottomDock->setWidget(tabs);
    m_docks[static_cast<std::size_t>(WorkbenchRegion::Bottom)] = bottomDock;
}

void WorkbenchShellImpl::buildCentralArea()
{
    // 中央区＝页栈：页 0 无项目首页（PM-10）、页 1 阶段占位面板（§4.1
    // "阶段 A：占位面板（含'本阶段将在后续版本提供'说明，不虚构业务能力）"
    // ——该占位为契约显式设计，非未完成实现）。中央区不可隐藏（§4.4）。
    m_centralStack = new QStackedWidget(m_window.get());
    m_centralStack->setObjectName("ird_central_stack");
    m_centralStack->addWidget(buildHomePage());
    auto* stagePage = new QLabel(u8"本阶段将在后续版本提供", m_centralStack);
    stagePage->setObjectName("ird_stage_placeholder");
    stagePage->setAlignment(Qt::AlignCenter);
    m_centralStack->addWidget(stagePage);
}

QWidget* WorkbenchShellImpl::buildHomePage()
{
    // 无项目首页（PM-10：新建/打开/最近项目三入口＋项目状态摘要）。
    auto* home = new QWidget(m_centralStack);
    home->setObjectName("ird_homepage");
    auto* layout = new QVBoxLayout(home);
    layout->setContentsMargins(24, 24, 24, 24);

    auto* title = new QLabel(u8"工程工作台", home);
    title->setAlignment(Qt::AlignHCenter);
    layout->addWidget(title);
    auto* subtitle = new QLabel(u8"未打开项目——从下方入口开始", home);
    subtitle->setAlignment(Qt::AlignHCenter);
    layout->addWidget(subtitle);

    // 入口一/二：新建、打开（点击经壳层提交路径路由 project.new/open；
    // 编排归 workflow 阶段 B——§14.1，本壳触发占位说明）。
    auto* entryRow = new QHBoxLayout();
    auto* newButton = new QPushButton(u8"新建项目", home);
    newButton->setObjectName("ird_home_new");
    QObject::connect(newButton, &QPushButton::clicked, home,
            [this] { submitShellCommand("project.new"); });
    auto* openButton = new QPushButton(u8"打开项目", home);
    openButton->setObjectName("ird_home_open");
    QObject::connect(openButton, &QPushButton::clicked, home,
            [this] { submitShellCommand("project.open"); });
    entryRow->addStretch(1);
    entryRow->addWidget(newButton);
    entryRow->addWidget(openButton);
    entryRow->addStretch(1);
    layout->addLayout(entryRow);

    // 入口三：最近项目（PM-10：上限/去重/失效保留＋"项目位置不可用"提示
    // ＋重新选择＋移除）。数据源＝RecentProjectsModel（用户级设置承载）。
    auto* recentGroup = new QWidget(home);
    recentGroup->setObjectName("ird_home_recent");
    auto* recentLayout = new QVBoxLayout(recentGroup);
    recentLayout->addWidget(new QLabel(u8"最近项目", recentGroup));
    m_homeRecentList = new QListWidget(recentGroup);
    m_homeRecentList->setObjectName("ird_home_recent_list");
    // 可用项点击＝打开入口（重新打开该项目）；失效项仅右键操作（重新选择
    // ／移除）——双击不触发（PM-10 失效项提示语义）。
    QObject::connect(m_homeRecentList, &QListWidget::itemClicked, m_homeRecentList,
            [this](QListWidgetItem* item) {
                if (!item->data(Qt::UserRole).toBool()) {
                    submitShellCommand("project.open");  // 可用项：打开该项目的入口
                }
            });
    // 右键菜单：重新选择／移除（对失效项与可用项均可移除——PM-10 原文）。
    m_homeRecentList->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(m_homeRecentList, &QWidget::customContextMenuRequested, m_homeRecentList,
            [this](const QPoint& pos) {
                QListWidgetItem* item = m_homeRecentList->itemAt(pos);
                if (!item) {
                    return;  // 空白处无上下文菜单
                }
                QMenu menu(m_homeRecentList);
                menu.addAction(u8"重新选择", m_homeRecentList, [this] {
                    submitShellCommand("project.open");  // 重新选择＝打开流（重选位置）
                });
                menu.addAction(u8"移除", m_homeRecentList, [this, item] {
                    removeRecentProject(item->toolTip().toStdString());
                });
                menu.exec(m_homeRecentList->mapToGlobal(pos));
            });
    recentLayout->addWidget(m_homeRecentList);
    layout->addWidget(recentGroup);

    // 项目状态摘要（PM-10 首页要素之一）：无项目态的摘要说明（打开项目后
    // 由后续任务接入真实摘要投影——阶段 A 不虚构数据）。
    auto* summary = new QLabel(u8"项目状态摘要：打开项目后在此显示", home);
    summary->setAlignment(Qt::AlignHCenter);
    layout->addWidget(summary);

    layout->addStretch(1);
    return home;
}

// =====================================================================
// 五区可见性与尺寸折叠（§4.1/§4.4）
// =====================================================================

QDockWidget* WorkbenchShellImpl::dockFor(WorkbenchRegion region) const
{
    return m_docks.at(static_cast<std::size_t>(region));
}

bool* WorkbenchShellImpl::userVisibilityFlag(WorkbenchRegion region)
{
    switch (region) {
    case WorkbenchRegion::Left: return &m_visibleLeft;
    case WorkbenchRegion::Right: return &m_visibleRight;
    case WorkbenchRegion::Bottom: return &m_visibleBottom;
    default: return nullptr;  // Top/Central 恒在——无用户可见性存储位
    }
}

bool WorkbenchShellImpl::regionVisible(WorkbenchRegion region) const
{
    // Top/Central 恒在（§4.4 最小可用布局三要素之二——第三要素为状态行，
    // 状态栏无隐藏入口）。
    if (region == WorkbenchRegion::Top || region == WorkbenchRegion::Central) {
        return true;
    }
    // 折叠态返回实际呈现（§4.4 折叠＝用户当前看不到该区）；非折叠返回
    // 用户意愿位。用模型位而非 Widget isVisible：窗口未显示阶段语义仍确定。
    const bool* flag = const_cast<WorkbenchShellImpl*>(this)->userVisibilityFlag(region);
    if (m_collapsedBySize) {
        return false;
    }
    return flag != nullptr && *flag;
}

void WorkbenchShellImpl::setRegionVisible(WorkbenchRegion region, bool visible)
{
    // shutdown 后调用＝§10.1"非法"行（安全轨：安全返回不崩溃，DT 断言面）。
    if (!m_window) {
        Q_ASSERT(false && "shutdown 后调用 setRegionVisible（§10.1 非法调用）");
        return;
    }
    bool* flag = userVisibilityFlag(region);
    if (!flag) {
        return;  // Top/Central 恒在：不接受隐藏（§4.4）——无操作
    }
    *flag = visible;
    // 折叠态不立即显示（展开时按用户意愿恢复——折叠不覆盖用户意愿）。
    dockFor(region)->setVisible(visible && !m_collapsedBySize);
    refreshCommandStates();  // 视图开关勾选态同步
}

void WorkbenchShellImpl::resetLayout()
{
    if (!m_window) {
        Q_ASSERT(false && "shutdown 后调用 resetLayout（§10.1 非法调用）");
        return;
    }
    // 出厂位形恢复（§4.4"恢复出厂位形"）：几何＋停靠位形取构建期快照；
    // restoreState 会把浮动窗口重新停靠＝"清除该会话的浮动窗口记忆"（§4.4）。
    m_window->restoreGeometry(m_factoryGeometry);
    m_window->restoreState(m_factoryState);
    m_visibleLeft = true;
    m_visibleRight = true;
    m_visibleBottom = true;
    m_collapsedBySize = false;
    updateCollapseBySize();       // 按当前尺寸重判折叠（出厂恢复≠免折叠）
    refreshCommandStates();
    persistLayoutAsync();         // 用户级设置同步（PM-14——复位即持久化）
}

void WorkbenchShellImpl::updateCollapseBySize()
{
    if (!m_window) {
        return;
    }
    // §4.4：低于最小窗口（1280×720）时左/右栏自动折叠、底栏折叠为单行
    // （顶栏＋中央区＋状态行恒在——最小可用布局定义）。
    const bool below =
        m_window->width() < kCollapseWidth || m_window->height() < kCollapseHeight;
    if (below == m_collapsedBySize) {
        return;  // 状态无变化（resize 拖动过程中的重复事件——无操作）
    }
    m_collapsedBySize = below;
    for (const WorkbenchRegion region :
         {WorkbenchRegion::Left, WorkbenchRegion::Right, WorkbenchRegion::Bottom}) {
        const bool* flag = userVisibilityFlag(region);
        dockFor(region)->setVisible(flag != nullptr && *flag && !below);
    }
}

// =====================================================================
// 上下文投影注入与呈现刷新（PM-10/PM-11/PM-07/§7.5）
// =====================================================================

void WorkbenchShellImpl::presentProjectContext(const ProjectContextProjection& context)
{
    if (!m_window) {
        Q_ASSERT(false && "shutdown 后调用 presentProjectContext（§10.1 非法调用）");
        return;
    }
    m_context = context;
    // 门控事实（§7.5 快照的 UI-T03 子集）一次性刷新——命令可用性求值只看
    // 这份快照（谓词内零端口查询/IO，NFR-PERF-01）。
    m_gate.hasProject = context.project.has_value();
    m_gate.writable = context.project.has_value() && context.project->writable;
    m_gate.drafts = context.drafts;

    // 中央区切换：无项目→首页（PM-10）；有项目→阶段占位面板（§4.1 阶段 A）。
    m_centralStack->setCurrentIndex(m_gate.hasProject ? 1 : 0);
    refreshStatusBar();
    refreshCommandStates();
}

void WorkbenchShellImpl::refreshStatusBar()
{
    // PM-11 唯一权威＝formatProjectStatusText（UiProjections.hpp）——标题栏
    // 与状态栏同格式（PM-11"标题栏与状态栏"双面）。
    const QString text = QString::fromStdString(formatProjectStatusText(m_context));
    if (m_statusText) {
        m_statusText->setText(text);
    }
    if (m_window) {
        m_window->setWindowTitle(text);
    }
    // 只读徽标（PM-07）：仅项目态且不可写时呈现。
    if (m_readonlyBadge) {
        m_readonlyBadge->setVisible(m_gate.hasProject && !m_gate.writable);
    }
}

void WorkbenchShellImpl::refreshCommandStates()
{
    if (!m_window) {
        return;
    }
    // 菜单/按钮使能态＝命令板求值结果（§7.6"三处一致禁用"——菜单/顶栏/
    // 面板同源，无第二口径）。
    for (auto& [action, commandId] : m_commandActions) {
        const ShellCommandAvailability a = m_board.availability(commandId, m_gate);
        action->setEnabled(a.enabled);
    }
    for (auto& [button, commandId] : m_topButtons) {
        const ShellCommandAvailability a = m_board.availability(commandId, m_gate);
        button->setEnabled(a.enabled);
        // 禁用原因随按钮 tooltip 呈现（§7.4"禁用＋说明"——发现性保留）。
        button->setToolTip(a.enabled
                               ? QString()
                               : QString::fromUtf8(a.reasonKey == WorkbenchText::kReasonReadOnly
                                                       ? WorkbenchText::kReasonReadOnlyText
                                                       : WorkbenchText::kReasonNoProjectText));
    }
    // 视图开关勾选态＝当前有效可见性（复位/记忆恢复后同步——不回环：
    // 本处只 setChecked，不触发 triggered）。
    for (auto& [region, action] : m_viewToggles) {
        action->setChecked(regionVisible(region));
    }
}

// =====================================================================
// 壳层命令提交路径（§4.2 路由红线；§14.1 阶段 A 边界）
// =====================================================================

void WorkbenchShellImpl::submitShellCommand(const std::string& commandId)
{
    if (!m_window) {
        Q_ASSERT(false && "shutdown 后调用 submitShellCommand（§10.1 非法调用）");
        return;
    }
    const ShellCommandAvailability a = m_board.availability(commandId, m_gate);
    if (!a.registered) {
        // 壳面只路由已登记命令（未登记提交路径＝UI-T06 注册表的
        // UI-CMD-UNKNOWN 拒绝面——本壳不产生该诊断）。
        return;
    }
    if (!a.enabled) {
        // 禁用态触发（可用性快照与刷新间隙的竞态等）：状态栏说明反馈
        // （§7.4"禁用＋说明"），不派发。
        m_window->statusBar()->showMessage(
            QString::fromUtf8(a.reasonKey == WorkbenchText::kReasonReadOnly
                                  ? WorkbenchText::kReasonReadOnlyText
                                  : WorkbenchText::kReasonNoProjectText),
            4000);
        return;
    }

    // 壳自持行为：恢复默认布局（本壳唯一在阶段 A 有完整语义的命令）。
    if (commandId == "view.resetLayout") {
        resetLayout();
        m_window->statusBar()->showMessage(u8"已恢复默认布局", 4000);
        return;
    }

    // 其余已登记命令（project.new/open、workbench.commandPalette、
    // draft.*、project.undo/redo、workbench.closeProject）：命令处理器分别
    // 随 workflow 向导编排（§14.1 阶段 B）、CommandRegistry/面板（UI-T06）、
    // UiSessionController（UI-T11）、DraftController（UI-T12）落地——本壳
    // 已完成登记＋门控＋路由收口，触发时以占位说明反馈（§11.4 不虚构业务
    // 能力；此为契约显式的阶段 A 形态，非空实现）。
    m_window->statusBar()->showMessage(
        QString::fromUtf8(WorkbenchText::kEntryDeferredNotice), 4000);
}

// =====================================================================
// 最近项目（PM-10/PM-14）
// =====================================================================

std::vector<RecentProjectEntry> WorkbenchShellImpl::recentProjects() const
{
    std::vector<RecentProjectEntry> out;
    out.reserve(m_recent.entries().size());
    for (const auto& path : m_recent.entries()) {
        RecentProjectEntry entry;
        entry.canonicalPath = path;
        // 可用性按当前文件系统事实计算（失效≠删除——PM-10 失效保留）。
        entry.available = QFileInfo::exists(QString::fromStdString(path));
        out.push_back(std::move(entry));
    }
    return out;
}

void WorkbenchShellImpl::noteRecentProject(const std::string& canonicalPath)
{
    // 入口即规范化：非规范输入收敛到去重键口径（PM-10"按规范路径去重"）。
    m_recent.note(RecentProjectsModel::canonicalize(canonicalPath));
    persistRecentAsync();
    refreshRecentList();
}

void WorkbenchShellImpl::removeRecentProject(const std::string& canonicalPath)
{
    m_recent.remove(RecentProjectsModel::canonicalize(canonicalPath));
    persistRecentAsync();
    refreshRecentList();
}

void WorkbenchShellImpl::refreshRecentList()
{
    if (!m_homeRecentList) {
        return;  // shutdown 后/构建前——无列表可刷
    }
    m_homeRecentList->clear();
    for (const RecentProjectEntry& entry : recentProjects()) {
        const QString path = QString::fromStdString(entry.canonicalPath);
        auto* item = new QListWidgetItem(m_homeRecentList);
        if (entry.available) {
            item->setText(path);
        } else {
            // 失效项保留＋"项目位置不可用"提示（PM-10 原文措辞）。
            item->setText(path + QString::fromUtf8(WorkbenchText::kRecentUnavailableSuffix));
            item->setForeground(Qt::gray);
        }
        item->setToolTip(path);  // 完整路径承载于 tooltip（移除操作的键）
        item->setData(Qt::UserRole, entry.available);  // 点击判别的可用位
        m_homeRecentList->addItem(item);
    }
}

// =====================================================================
// 布局记忆装载与持久化（§4.5/PM-14；UI-WB-1/2/3 的机制面）
// =====================================================================

bool WorkbenchShellImpl::restorePersistedLayout()
{
    // 读盘在 UI 线程（写线程未启动——§3.4 读写不重叠）。
    QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                       kSettingsOrg, kSettingsApp);
    const LayoutMemory::LoadResult loaded = LayoutMemory::load(settings);

    if (loaded.kind == LayoutMemory::LoadResult::Kind::Corrupt) {
        // §4.5 原文三件事：①回退出厂默认布局；②UI-LAYOUT-RESTORE-FAILED
        // （Dev 诊断——经开发日志通道出线，不阻塞启动）；③损坏段整段丢弃。
        LayoutMemory::discard(settings);
        applyFactoryLayout();
        emitDev(std::string(WorkbenchText::kLayoutRestoreFailedCode)
                + ": 布局记忆损坏或版本不识别——已回退出厂默认布局，"
                  "损坏的用户设置段已整段丢弃（不阻塞启动）");
        return false;
    }
    if (loaded.kind == LayoutMemory::LoadResult::Kind::Absent) {
        applyFactoryLayout();  // 出厂首次：静默取出厂位形（不制造告警）
        return true;
    }

    // 正常恢复：先几何/停靠位形（restoreState 按 objectName 匹配 Dock——
    // buildWindow 已设置），再叠加用户可见性键。
    const bool geometryOk = m_window->restoreGeometry(loaded.geometry);
    const bool stateOk = m_window->restoreState(loaded.state);
    if (!geometryOk || !stateOk) {
        // 字节在但 Qt 判不可恢复＝损坏同路径（§4.5"设置损坏"）。
        LayoutMemory::discard(settings);
        applyFactoryLayout();
        emitDev(std::string(WorkbenchText::kLayoutRestoreFailedCode)
                + ": 布局记忆解析失败——已回退出厂默认布局，损坏的用户设置段"
                  "已整段丢弃（不阻塞启动）");
        return false;
    }
    m_visibleLeft = loaded.visibleLeft;
    m_visibleRight = loaded.visibleRight;
    m_visibleBottom = loaded.visibleBottom;
    for (const WorkbenchRegion region :
         {WorkbenchRegion::Left, WorkbenchRegion::Right, WorkbenchRegion::Bottom}) {
        dockFor(region)->setVisible(*userVisibilityFlag(region));
    }
    return true;
}

void WorkbenchShellImpl::applyFactoryLayout()
{
    // 出厂位形（构建期快照）：几何＋停靠位形＋三区全可见。
    m_window->restoreGeometry(m_factoryGeometry);
    m_window->restoreState(m_factoryState);
    m_visibleLeft = true;
    m_visibleRight = true;
    m_visibleBottom = true;
    m_collapsedBySize = false;
    updateCollapseBySize();
    refreshCommandStates();
}

void WorkbenchShellImpl::persistLayoutAsync()
{
    if (!m_window) {
        return;
    }
    // 值序列化在 UI 线程完成（§3.4 纪律：落盘线程不触碰 Widget——任务体
    // 只拿已拷贝的字节与布尔位）。
    const QByteArray geometry = m_window->saveGeometry();
    const QByteArray state = m_window->saveState();
    const bool visibleLeft = m_visibleLeft;
    const bool visibleRight = m_visibleRight;
    const bool visibleBottom = m_visibleBottom;
    const bool accepted = m_settingsWriter.enqueue([geometry, state, visibleLeft,
                                                    visibleRight, visibleBottom](QSettings& s) {
        LayoutMemory::store(s, geometry, state, visibleLeft, visibleRight, visibleBottom);
    });
    if (!accepted) {
        // 写线程未运行（shutdown 后）——如实留痕不静默（layoutPersisted
        // 将为 false，下次启动回到上次成功落盘位形）。
        emitDev("布局落盘任务被拒（写线程未运行）——本次布局变更未持久化");
    }
}

void WorkbenchShellImpl::persistRecentAsync()
{
    const QStringList snapshot = m_recent.stored();  // 值快照（禁触 Widget）
    m_settingsWriter.enqueue([snapshot](QSettings& s) {
        s.setValue(QString(kRecentGroup) + '/' + kRecentKey, snapshot);
    });
}

// =====================================================================
// 门面出口与辅助
// =====================================================================

QWidget* WorkbenchShellImpl::mainWindow()
{
    // 唯一 Widget 出口（§10.1）；shutdown 后返回 nullptr（后置条件原文
    // "shutdown 后 mainWindow 不可再用"）。
    return m_window.get();
}

ShellCommandAvailability WorkbenchShellImpl::commandAvailability(
    const std::string& commandId) const
{
    return m_board.availability(commandId, m_gate);
}

void WorkbenchShellImpl::emitDev(const std::string& message)
{
    // Dev 码唯一出线＝开发日志通道（diagnostics §6.2"Dev 走日志"）；为空
    // ＝wiring 显式声明的无日志场景——恢复行为不依赖日志可用性（§4.5）。
    if (m_wiring.devLog) {
        m_wiring.devLog->logDev(kUiDevChannel, message);
    }
}

// =====================================================================
// 装配入口（IWorkbenchShell.hpp 工厂声明的实现）
// =====================================================================

}  // namespace detail

std::unique_ptr<IWorkbenchShell> createWorkbenchShell()
{
    return std::make_unique<detail::WorkbenchShellImpl>();
}

}  // namespace ui
}  // namespace ird
}  // namespace sdurws
