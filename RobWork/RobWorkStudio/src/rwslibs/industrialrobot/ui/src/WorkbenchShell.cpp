/**
 * @file   WorkbenchShell.cpp
 * @brief  工作台壳实现：五区 Dock 组装、跨会话布局记忆（含损坏回退＋Dev
 *         诊断）、无项目首页（PM-10 三入口）、PM-11 状态栏格式与壳层命令
 *         可用性门控。
 *
 * 设计依据：
 *   - units/ui.md §4.1~§4.6（五区布局总图/区域交互边界/空项目首页/响应式
 *     尺寸与最小可用布局/布局状态归属/用户级设置项）、§7.1~§7.6（命令注册
 *     表登记协议/快捷键表/命令面板/谓词/只读条件）、§10.1（IWorkbenchShell
 *     契约表——前置/后置/错误类型/线程/所有权/副作用）、§10.3/§10.4（两
 *     注册表接口）、§3.4（线程模型与后台落盘线程）、§3.5（UI-LAYOUT-RESTORE-
 *     FAILED 出线口径）、§11.4（不虚构业务能力——占位说明口径）；
 *   - 需求 UX-09/UX-13/UX-14/PM-07/PM-10/PM-11/PM-14；任务契约 tasks/
 *     foundation/UI-T03.json acceptance 1~4、tasks/foundation/UI-T06.json
 *     acceptance 1~3（注册表/快捷键/面板的壳集成——SA-16 唯一入口）、
 *     tasks/foundation/UI-T07.json acceptance 1~3（右栏工程策略摘要只读
 *     卡——§6.7，UI-T07；数据经 ShellWiring.policySource 自有端口，分组
 *     异名 POL-ID-3）、tasks/foundation/UI-T10.json acceptance 1~2（帮助
 *     入口与关于对话框——§11.4，UI-T10：help.about/help.contents 壳自持
 *     处理器＋ShellWiring.aboutSource 端口消费；UI-PLG-2/O-31）；
 *   - §14.1 交接清单（新建/打开向导编排归 workflow 阶段 B——本壳登记入口
 *     并以占位说明触发，不虚构能力）。
 *
 * 实现期口径登记（随 ui.md §10.1/§16.7 v0.8 增量修订同步）：
 *   - 视图开关/顶栏按钮/菜单动作的默认快捷键一律不在此安装（§7.3 全局快捷
 *     键唯一注册点归 GlobalShortcutRegistry——本壳经其 registerDefault 登
 *     记 §7.1 默认表，QShortcut 对象由该注册表 attach 创建）；
 *   - 命令登记/可用性/提交统一归 CommandRegistry（§10.3——UI-T03 的壳层
 *     命令板已移除，菜单/顶栏/首页/快捷键/面板五入口同走 registry.submit）；
 *   - Dev 级码经 wiring.devLog 出线（§6.2"Dev 走日志"）；用户级诊断条目
 *     需工厂产条目（工厂注入随 UI-T13 呈现模型落地），本壳阶段工厂不注入
 *     ——拒绝语义由返回值与状态栏说明承载（§11.4 不虚构业务能力）。
 */

#include "WorkbenchShell_p.hpp"
#include "CommandPalette_p.hpp"

#include <QDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <string_view>

#include <sdurws/ird/ui/AboutDialog.hpp>  // 关于框装配/工厂/手册入口（UI-T10——§11.4）
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
    //   ①窗口树与出厂快照 → ②命令设施装配（§7.1 登记＋seal——必须先于
    //   任何 refreshCommandStates 触达点：布局恢复/出厂回退都会刷命令态）
    //   → ③用户级设置同步读（最近项目＋快捷键历史＋面板近期——写线程未
    //   启动，读写不重叠）→ ④布局记忆装载（损坏回退）→ ⑤初始上下文
    //   （无项目首页＝PM-10 启动态，快照同步推送注册表）→ ⑥写盘线程启动
    //   → ⑦快捷键历史回放＋QShortcut attach＋面板创建（持久化回调需要
    //   写线程就位，故在⑥后）。
    buildWindow();
    assembleCommandSystem();

    {
        // 读盘（UI 线程、写线程未启动——§3.4 读写不重叠）：最近项目与布局
        // 同源用户级设置，但分属不同键组（布局损坏整段丢弃不波及相邻组）。
        QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                           kSettingsOrg, kSettingsApp);
        m_recent.load(settings.value(QString(kRecentGroup) + '/' + kRecentKey)
                          .toStringList());
        m_pendingUserBindings = loadUserShortcutBindings(settings);
        m_pendingPaletteRecent = loadPaletteRecent(settings);
    }

    restorePersistedLayout();
    refreshRecentList();

    // 启动即无项目首页（PM-10：壳启动时无会话，首页三入口就位；快照同步
    // 推送注册表——命令可用性求值与状态栏/首页消费同一份）。
    presentProjectContext(ProjectContextProjection{});

    // 写线程在读盘完成后启动（§3.4 后台落盘线程；此后所有设置写入经队列）。
    m_settingsWriter.start();

    // 快捷键历史回放（PM-14：User 绑定覆盖默认集；冲突条目丢弃＋Dev 日志
    // ——不阻塞启动）→ QShortcut attach（唯一创建点，宿主＝主窗口）→
    // 命令面板创建（workbench.commandPalette 处理器的打开目标）→ 面板近期
    // 使用回放（§7.4 置顶分组的持久化半区）。
    applyShortcutAndPaletteState();

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

    // ①布局/快捷键/面板近期落盘：值序列化在 UI 线程（Widget 尚存活），写盘
    //   交给后台线程，随后有界收口（§10.1"有界拆除"——队列里全是微秒级写
    //   任务，join 有界）。快捷键 User 绑定与面板近期使用随关停补一次落盘
    //   （改绑/使用路径已有即时落盘——此处是关停位的兜底快照，PM-14）。
    persistLayoutAsync();
    persistShortcutsAsync();
    persistPaletteRecentAsync();
    m_lastTeardown.layoutPersisted = m_settingsWriter.drainAndStop();

    // ②窗口销毁（§10.1 后置：shutdown 后 mainWindow 不可再用——唯一出口
    //   返回 nullptr；窗口树子指针随之失效，全部置空防悬垂）。
    m_shortcuts->detach();  // QShortcut 唯一创建点的对称拆卸（物理键随窗口树销毁——此处清表）
    m_window->close();
    m_window.reset();
    m_palette = nullptr;    // 面板为窗口树子——随窗口销毁（指针置空防悬垂）
    m_commands.reset();     // 注册表/快捷键表在窗口销毁后停用（处理器只经壳触发）
    m_shortcuts.reset();
    m_centralStack = nullptr;
    m_statusText = nullptr;
    m_readonlyBadge = nullptr;
    m_homeRecentList = nullptr;
    m_docks.fill(nullptr);
    m_commandActions.clear();
    m_topButtons.clear();
    m_viewToggles.clear();
    // 策略摘要卡随右栏窗口树销毁（UI-T07）——刷新钩子捕获卡容器指针与
    // wiring 端口引用，窗口销毁后一并失效：此处清空钩子防悬垂调用。
    m_refreshPolicyCard = nullptr;

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

    // 帮助（UI-T10 起实条目——§7.1 壳层命令 help.contents/help.about 经
    // 统一提交路径；F1 默认键已随 §7.3 默认表登记，入口动作不再自带快捷
    // 键——§4.2 路由红线：菜单只路由命令板）。
    QMenu* help = m_window->menuBar()->addMenu(QString::fromUtf8(WorkbenchText::kMenuHelp));
    help->addAction(makeCommandAction(WorkbenchText::kCmdHelpContents, "help.contents"));
    help->addAction(makeCommandAction(WorkbenchText::kCmdHelpAbout, "help.about"));
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
    // 工程策略摘要只读卡（UI-T07——§6.7"诊断与设置区"内嵌策略摘要入口）：
    // 数据源＝ShellWiring.policySource（O-31 ui 自有端口，L5 适配
    // policy::IPolicyProvider——§6.7 摘要只读面语义不变；initialize 前置
    // 已校验非空，此处恒可解引用）；行装配与分组异名语义在
    // PolicySummaryCard 模型层（GUI 只渲染）。右侧既有的"策略摘要入口
    // 占位"标签由真实卡取代——策略未装载时卡内呈占位行（不虚构数值），
    // 仍是契约显式设计而非未完成实现。
    rightLayout->addWidget(createPolicySummaryCard(*m_wiring.policySource,
                                                   &m_refreshPolicyCard, rightContent));
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
    // 中央区＝页栈：页 0 无项目首页（PM-10）、页 1 三维视图占位面板（§4.1
    // "阶段 A：占位面板（含'本阶段将在后续版本提供'说明，不虚构业务能力）"
    // ——该占位为契约显式设计，非未完成实现）。UI-T05 阶段 A 起占位面板
    // 承载三维视图区域身份与 UX-11 交互清单＋KIN-06 会话姿态语义的契约
    // 登记（View3DContract.hpp——阶段 B 交互实现的承接面）；交互实现归
    // 阶段 B（WP-10-T05 阶段 B 交付）。中央区不可隐藏（§4.4）。
    m_centralStack = new QStackedWidget(m_window.get());
    m_centralStack->setObjectName("ird_central_stack");
    m_centralStack->addWidget(buildHomePage());
    m_centralStack->addWidget(createView3DPlaceholder(m_centralStack));
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
// 命令设施装配（UI-T06——§7.1 最小命令集＋§7.3 默认绑定＋§7.4 面板）
// =====================================================================

namespace {

/// §7.1 最小命令集的登记行（表行的编译期形态——行序＝§7.1 表行序，即
/// registrationOrder 的稳定排序锚，NFR-COR-02 界面延伸）。
struct ShellCommandRow {
    const char* id;              ///< 命令 id（点分小写——§7.1 语法）
    const char* titleKey;        ///< 标题文案键（cmd.<id>.title——§3.5）
    const char* keywordKeys[2];  ///< 关键字键（cmd.<id>.kw.<n>——UX-13；不足以 nullptr 填充）
    CommandCategory category;    ///< 分类（面板分组轴）
    ShellCommandScope scope;     ///< 作用域（§7.1 CommandScope）
    bool readOnlyAllowed;        ///< 只读会话是否可用（§7.6）
    const char* defaultShortcut; ///< 默认键（nullptr＝无默认——面板可达，UX-13 兜底）
    const char* menuPath;        ///< 菜单与面板分组（"文件/新建"）
};

/// §7.1 表逐行（readOnlyAllowed/默认键严格按表列；归属列只影响处理器
/// 语义——阶段 A 全部命令项由壳登记，未接线处理器以占位说明反馈）。
constexpr ShellCommandRow kMinimalCommandRows[] = {
    {"project.new",     "cmd.project.new.title",     {"cmd.project.new.kw.0", "cmd.project.new.kw.1"}, CommandCategory::Project,   ShellCommandScope::Session, true,  "Ctrl+N",        "文件/新建"},
    {"project.open",    "cmd.project.open.title",    {"cmd.project.open.kw.0", nullptr},               CommandCategory::Project,   ShellCommandScope::Session, true,  "Ctrl+O",        "文件/打开"},
    {"draft.save",      "cmd.draft.save.title",      {"cmd.draft.save.kw.0", nullptr},                 CommandCategory::Edit,      ShellCommandScope::Project, false, "Ctrl+S",        "文件/保存草稿"},
    {"draft.apply",     "cmd.draft.apply.title",     {"cmd.draft.apply.kw.0", nullptr},                CommandCategory::Edit,      ShellCommandScope::Project, false, "Ctrl+Return",   "文件/应用修改"},
    {"project.undo",    "cmd.project.undo.title",    {"cmd.project.undo.kw.0", nullptr},               CommandCategory::Edit,      ShellCommandScope::Project, false, "Ctrl+Z",        "编辑/撤销"},
    {"project.redo",    "cmd.project.redo.title",    {"cmd.project.redo.kw.0", nullptr},               CommandCategory::Edit,      ShellCommandScope::Project, false, "Ctrl+Y",        "编辑/重做"},
    {"scheme.switch",   "cmd.scheme.switch.title",   {"cmd.scheme.switch.kw.0", nullptr},              CommandCategory::Stage,     ShellCommandScope::Project, false, nullptr,         "阶段/切换方案"},
    {"project.saveAs",  "cmd.project.saveAs.title",  {"cmd.project.saveAs.kw.0", nullptr},             CommandCategory::Project,   ShellCommandScope::Project, false, nullptr,         "文件/项目另存为"},
    {"package.export",  "cmd.package.export.title",  {"cmd.package.export.kw.0", nullptr},             CommandCategory::Project,   ShellCommandScope::Project, false, nullptr,         "文件/导出评估包"},
    {"report.export",   "cmd.report.export.title",   {"cmd.report.export.kw.0", nullptr},              CommandCategory::Report,    ShellCommandScope::Project, true,  nullptr,         "文件/导出报告"},
    {"analysis.collisionCheck", "cmd.analysis.collisionCheck.title", {"cmd.analysis.collisionCheck.kw.0", "cmd.analysis.collisionCheck.kw.1"}, CommandCategory::Analysis, ShellCommandScope::Project, false, nullptr, "工具/碰撞检查"},
    {"view.displayMode", "cmd.view.displayMode.title", {"cmd.view.displayMode.kw.0", nullptr},         CommandCategory::View,      ShellCommandScope::View,    true,  nullptr,         "视图/显示模式"},
    {"view.resetHome",  "cmd.view.resetHome.title",  {"cmd.view.resetHome.kw.0", "cmd.view.resetHome.kw.1"}, CommandCategory::View, ShellCommandScope::View,    true,  nullptr,         "视图/复位到 home 位"},
    {"view.resetZero",  "cmd.view.resetZero.title",  {"cmd.view.resetZero.kw.0", "cmd.view.resetZero.kw.1"}, CommandCategory::View, ShellCommandScope::View,    true,  nullptr,         "视图/复位到零位"},
    {"workbench.commandPalette", "cmd.workbench.commandPalette.title", {"cmd.workbench.commandPalette.kw.0", "cmd.workbench.commandPalette.kw.1"}, CommandCategory::Workbench, ShellCommandScope::Session, true, "Ctrl+Shift+P", "工具/命令面板"},
    {"workbench.closeProject",   "cmd.workbench.closeProject.title",   {"cmd.workbench.closeProject.kw.0", nullptr}, CommandCategory::Workbench, ShellCommandScope::Project, true,  "Ctrl+W",        "文件/关闭项目"},
    {"view.resetLayout", "cmd.view.resetLayout.title", {"cmd.view.resetLayout.kw.0", nullptr},         CommandCategory::View,      ShellCommandScope::View,    true,  nullptr,         "视图/恢复默认布局"},
    {"help.about",      "cmd.help.about.title",      {"cmd.help.about.kw.0", nullptr},                 CommandCategory::Help,      ShellCommandScope::Session, true,  nullptr,         "帮助/关于"},
    {"help.contents",   "cmd.help.contents.title",   {"cmd.help.contents.kw.0", nullptr},              CommandCategory::Help,      ShellCommandScope::Session, true,  "F1",            "帮助/帮助手册"},
};

}  // namespace

void WorkbenchShellImpl::assembleCommandSystem()
{
    // 命令注册表（§10.3）：白名单＝壳层设施（"ui"——SA-01 静态白名单的
    // 命令侧；插件命令经 IPluginUiRegistrar 随装配任务增量接入）。诊断面
    // 按可空成员纪律接线：devLog 恒转接（Dev 码出线）；diagFactory/diagSink
    // 为 UI-T06 wiring 增量（L5 注入工厂时用户级码经 create 唯一入口入目
    // 录——§9.2；壳期 L5 未注入＝无目录场景，拒绝语义由返回值与状态栏反馈
    // 承载——登记 ui.md §10.1 v0.8）。
    CommandRegistryDeps commandDeps;
    commandDeps.diagFactory = m_wiring.diagFactory;
    commandDeps.diagSink = m_wiring.diagSink;
    commandDeps.devLog = m_wiring.devLog;
    commandDeps.ownerWhitelist = {"ui"};
    m_commands = createCommandRegistry(std::move(commandDeps));

    // §7.1 最小命令集登记（默认谓词＝作用域/只读规则——与壳门控快照同源，
    // §7.5/§7.6；默认谓词内零 IO）。处理器按归属接线：
    //   - 壳自持语义（视图复位/面板打开）＝完整实现；
    //   - 其余归属 workflow/project/io/reporting 的命令＝占位说明处理器
    //     （§11.4/§14.1 契约显式的阶段 A 形态——不虚构业务能力，接线随
    //     归属任务落地；非空实现：触发有状态栏反馈且进入近期使用）。
    for (const ShellCommandRow& row : kMinimalCommandRows) {
        CommandDescriptor desc;
        desc.id = row.id;
        desc.ownerUnit = "ui";
        desc.titleKey = row.titleKey;
        for (const char* keywordKey : row.keywordKeys) {
            if (keywordKey != nullptr) {
                desc.keywordKeys.emplace_back(keywordKey);  // 定长数组到投影行（nullptr 填充段跳过）
            }
        }
        desc.category = row.category;
        desc.scope = row.scope;
        desc.readOnlyAllowed = row.readOnlyAllowed;
        desc.bindable = true;
        if (row.defaultShortcut != nullptr) {
            desc.defaultShortcut = QKeySequence(QString::fromLatin1(row.defaultShortcut));
        }
        desc.menuPath = row.menuPath;

        ICommandRegistry::CommandHandler handler;
        if (std::string_view(row.id) == "view.resetLayout") {
            // 壳自持：恢复出厂位形（§4.5）＋即时反馈。
            handler = [this](const std::vector<CommandParameter>&) {
                resetLayout();
                m_window->statusBar()->showMessage(u8"已恢复默认布局", 4000);
                CommandOutcome out;
                out.accepted = true;
                return out;
            };
        } else if (std::string_view(row.id) == "workbench.commandPalette") {
            // 壳自持：打开命令面板（§7.4——UX-13 的键盘可达入口）。
            handler = [this](const std::vector<CommandParameter>&) {
                openCommandPalette();
                CommandOutcome out;
                out.accepted = true;
                return out;
            };
        } else if (std::string_view(row.id) == "help.about") {
            // 壳自持：打开关于对话框（UI-T10——§11.4 关于页＝白名单∩报告
            // ＋版本基线，数据经 IUiAboutDataSource 端口现取）。
            handler = [this](const std::vector<CommandParameter>&) {
                openAboutDialog();
                CommandOutcome out;
                out.accepted = true;
                return out;
            };
        } else if (std::string_view(row.id) == "help.contents") {
            // 壳自持：打开用户手册（UI-T10——§11.4 帮助入口链接用户手册；
            // 反馈见 openUserManualEntry——成功/缺失二态均用户可见）。
            handler = [this](const std::vector<CommandParameter>&) {
                openUserManualEntry();
                CommandOutcome out;
                out.accepted = true;
                return out;
            };
        } else {
            // 占位说明处理器（阶段 A 契约显式形态——accepted=true：提交
            // 链路真实走通，能力面以 §11.4 说明呈现）。
            handler = [this](const std::vector<CommandParameter>&) {
                if (m_window) {
                    m_window->statusBar()->showMessage(
                        QString::fromUtf8(WorkbenchText::kEntryDeferredNotice), 4000);
                }
                CommandOutcome out;
                out.accepted = true;
                out.messageKey = std::string{"notice.entry-deferred"};
                return out;
            };
        }
        const RegistrationResult result = m_commands->registerCommand(desc, handler);
        Q_ASSERT(result == RegistrationResult::Ok
                 && "§7.1 最小命令集登记被拒（表内冲突＝装配 bug）");
        (void)result;
    }

    // 装配收口（§7.2——运行期只读；此后 registerCommand 拒绝）。
    m_commands->seal();

    // 全局快捷键表（§10.4/§7.3）：默认集登记（§7.1 默认键列——Default 随
    // 产品版本冻结）＋用户改绑持久化回调（PM-14——写盘经后台线程）。
    GlobalShortcutRegistryDeps shortcutDeps;
    shortcutDeps.commands = m_commands.get();
    shortcutDeps.diagFactory = m_wiring.diagFactory;
    shortcutDeps.diagSink = m_wiring.diagSink;
    shortcutDeps.devLog = m_wiring.devLog;
    shortcutDeps.onUserBindingsChanged =
        [this](const std::vector<HotkeyBinding>&) { persistShortcutsAsync(); };
    m_shortcuts = createGlobalShortcutRegistry(std::move(shortcutDeps));
    for (const ShellCommandRow& row : kMinimalCommandRows) {
        if (row.defaultShortcut != nullptr) {
            const HotkeyResult r = m_shortcuts->registerDefault(
                row.id, QKeySequence(QString::fromLatin1(row.defaultShortcut)));
            Q_ASSERT(r.isOk() && "§7.1 默认键注册被拒（表内冲突＝装配 bug）");
            (void)r;
        }
    }
}

void WorkbenchShellImpl::applyShortcutAndPaletteState()
{
    // ①用户改绑回放（PM-14 持久化半区——冲突/失效条目由注册表丢弃＋Dev
    //   日志，不阻塞启动；§4.5 坏一条丢一条的快捷键侧纪律）。
    m_shortcuts->restoreUserBindings(m_pendingUserBindings);
    m_pendingUserBindings.clear();
    // ②QShortcut attach（§7.3 唯一创建点——宿主＝主窗口，WindowShortcut
    //   上下文；触发经 registry.submit 统一路径）。
    m_shortcuts->attach(m_window.get());
    // ③命令面板（§7.4——面板是注册表的只读投影；父子归主窗口 Qt 树）。
    m_palette = createCommandPalette(m_commands.get(), m_shortcuts.get(), m_window.get());
    // ④面板近期使用回放（§7.4 置顶分组的持久化半区——未注册 id 丢弃）。
    m_commands->restoreRecentUsed(m_pendingPaletteRecent);
    m_pendingPaletteRecent.clear();
}

void WorkbenchShellImpl::openCommandPalette()
{
    if (m_palette != nullptr) {
        m_palette->open();  // 打开即构建快照（§7.4"打开时构建快照"）
    }
}

// =====================================================================
// 帮助入口（UI-T10——§11.4 帮助入口与关于对话框）
// =====================================================================

void WorkbenchShellImpl::openAboutDialog()
{
    if (!m_window) {
        Q_ASSERT(false && "shutdown 后调用 openAboutDialog（§10.1 非法调用）");
        return;
    }
    // 第 1 步：现取端口数据（§11.4 数据＝白名单 ∩ 报告＋版本基线；端口
    // 为空＝无关于数据测试场景——wiring 显式声明语义，清单/版本双占位，
    // 零虚构数据）。报告与基线各取一次快照，对话框呈现该快照（打开期间
    // 不订阅不刷新——关于框是装配期事实的静态呈现面）。
    const std::vector<PluginAssemblyReport> reports =
        m_wiring.aboutSource != nullptr ? m_wiring.aboutSource->assemblyReports()
                                        : std::vector<PluginAssemblyReport>{};
    const AboutVersionBaseline baseline =
        m_wiring.aboutSource != nullptr ? m_wiring.aboutSource->versionBaseline()
                                        : AboutVersionBaseline{};

    // 第 2 步：模型装配＋对话框构建（同源纪律——清单/版本行全部出自
    // AboutDialog.hpp 的装配函数，本壳零过滤零加工逻辑）。
    QDialog* dialog = createAboutDialog(baseline, aboutPluginRows(reports),
                                        m_window.get());

    // 第 3 步：非阻塞打开（QDialog::open＝窗口模态呈现、不进嵌套事件
    // 循环——§7.7 提交时序不可重入；对话框生命周期交 Qt 父子树，随主
    // 窗口销毁或用户关闭即回收）。
    dialog->open();
}

void WorkbenchShellImpl::openUserManualEntry()
{
    if (!m_window) {
        Q_ASSERT(false && "shutdown 后调用 openUserManualEntry（§10.1 非法调用）");
        return;
    }
    // 打开动作（§11.4"链接用户手册"语义本体在 openUserManual——存在才
    // 启动系统打开器）；本方法只承载二态的用户可见反馈（入口点了没反应
    // 属可见性违例）。反馈走状态栏（即时、非阻断）；缺失的解析路径细节
    // 走 Dev 日志（排障面——§3.5 码表无此事件码，不臆造稳定码；呈现面
    // 零内部路径）。
    if (openUserManual()) {
        m_window->statusBar()->showMessage(
            QString::fromUtf8(WorkbenchText::kHelpManualOpenedNotice), 4000);
        return;
    }
    emitDev("用户手册入口文件缺失（share 帮助文件未部署）：" + userManualPath());
    m_window->statusBar()->showMessage(
        QString::fromUtf8(WorkbenchText::kHelpManualMissingNotice), 6000);
}

std::vector<HotkeyBinding>
WorkbenchShellImpl::loadUserShortcutBindings(QSettings& settings)
{
    // 载荷读取（UI 线程、写线程未启动——读写不重叠）：QStringList 每条
    // "commandId\\t键 PortableText"；格式不符的条目丢弃（损坏一条不炸整表
    // ——§4.5 同纪律；来源标记统一 User——载荷只承载 command+key 事实）。
    std::vector<HotkeyBinding> out;
    const QStringList stored =
        settings.value(QString(kShortcutGroup) + '/' + kShortcutKey).toStringList();
    for (const QString& line : stored) {
        const int sep = line.indexOf(QLatin1Char('\t'));
        if (sep <= 0) {
            continue;  // 缺命令段（损坏条目——丢弃）
        }
        HotkeyBinding binding;
        binding.command = line.left(sep).toStdString();
        binding.key = QKeySequence(line.mid(sep + 1));
        binding.origin = BindingOrigin::User;
        if (!binding.key.isEmpty()) {
            out.push_back(std::move(binding));  // 空键＝损坏条目（丢弃）
        }
    }
    return out;
}

std::vector<CommandId> WorkbenchShellImpl::loadPaletteRecent(QSettings& settings)
{
    // 近期使用读取（§7.4"用户级持久化最近 20 条"——QStringList 最新在前；
    // 未注册 id 由 restoreRecentUsed 收敛丢弃）。
    QStringList stored =
        settings.value(QString(kPaletteRecentGroup) + '/' + kPaletteRecentKey).toStringList();
    std::vector<CommandId> out;
    out.reserve(static_cast<std::size_t>(stored.size()));
    for (const QString& id : stored) {
        out.push_back(id.toStdString());
    }
    return out;
}

void WorkbenchShellImpl::persistShortcutsAsync()
{
    // 值序列化在 UI 线程（§3.4 纪律）：User 绑定集→"commandId\\t键" 行表。
    const std::vector<HotkeyBinding> bindings =
        m_shortcuts ? m_shortcuts->bindings() : std::vector<HotkeyBinding>{};
    QStringList lines;
    for (const HotkeyBinding& binding : bindings) {
        if (binding.origin == BindingOrigin::User) {
            // Default 集随产品版本冻结（§7.3）——持久化只落 User 集。
            lines << QString::fromStdString(binding.command) + QLatin1Char('\t')
                           + binding.key.toString(QKeySequence::PortableText);
        }
    }
    m_settingsWriter.enqueue([lines](QSettings& s) {
        s.setValue(QString(kShortcutGroup) + '/' + kShortcutKey, lines);
    });
}

void WorkbenchShellImpl::persistPaletteRecentAsync()
{
    const QStringList snapshot = [this] {
        QStringList list;
        if (m_commands) {
            for (const CommandId& id : m_commands->recentUsed()) {
                list << QString::fromStdString(id);
            }
        }
        return list;
    }();
    m_settingsWriter.enqueue([snapshot](QSettings& s) {
        s.setValue(QString(kPaletteRecentGroup) + '/' + kPaletteRecentKey, snapshot);
    });
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
    // 门控快照（§7.5 UiContextSnapshot）一次性刷新并推送注册表——命令可用
    // 性求值只看这份快照（谓词内零端口查询/IO，NFR-PERF-01）；状态栏/徽标
    // 消费同一份（单一口径）。注册表未装配（initialize 序中的首次调用先于
    // assembleCommandSystem 之前不存在——本壳装配序保证）防御性跳过推送。
    m_gate.hasActiveProject = context.project.has_value();
    m_gate.writable = context.project.has_value() && context.project->writable;
    m_gate.drafts = context.drafts;
    if (m_commands) {
        m_commands->presentContext(m_gate);
    }

    // 中央区切换：无项目→首页（PM-10）；有项目→阶段占位面板（§4.1 阶段 A）。
    m_centralStack->setCurrentIndex(m_gate.hasActiveProject ? 1 : 0);
    refreshStatusBar();
    refreshCommandStates();
    // 策略摘要卡随上下文注入重拉端口快照（UI-T07——§6.7；阶段 A 的刷新
    // 锚点＝本唯一上下文入口，事件驱动刷新随投影管线 §6.1/UI-T09 接入）。
    refreshPolicySummaryCard();
}

void WorkbenchShellImpl::refreshPolicySummaryCard()
{
    // 卡未装配（initialize 装配序之前的 presentProjectContext 首调——
    // 与 m_commands 的防御性跳过同口径）或已 shutdown（钩子置空）＝空操作。
    if (m_refreshPolicyCard) {
        m_refreshPolicyCard();
    }
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
        m_readonlyBadge->setVisible(m_gate.hasActiveProject && !m_gate.writable);
    }
}

void WorkbenchShellImpl::refreshCommandStates()
{
    if (!m_window || !m_commands) {
        return;  // 窗口未建/命令设施未装配（initialize 装配序保证二者先于本调用）
    }
    // 菜单/按钮使能态＝命令注册表求值结果（§7.6"三处一致禁用"——菜单/
    // 顶栏/面板同源单一口径，无第二套求值；UI-T06 起由注册表承载）。
    for (auto& [action, commandId] : m_commandActions) {
        const CommandAvailability a = m_commands->availability(commandId);
        action->setEnabled(a.enabled);
    }
    for (auto& [button, commandId] : m_topButtons) {
        const CommandAvailability a = m_commands->availability(commandId);
        button->setEnabled(a.enabled);
        // 禁用原因随按钮 tooltip 呈现（§7.4"禁用＋说明"——发现性保留）。
        button->setToolTip(a.enabled
                               ? QString()
                               : QString::fromUtf8(a.disableReasonKey == WorkbenchText::kReasonReadOnly
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
// 壳层命令提交路径（§4.2 路由红线；§7.7 统一提交路径）
// =====================================================================

void WorkbenchShellImpl::submitShellCommand(const std::string& commandId)
{
    if (!m_window) {
        Q_ASSERT(false && "shutdown 后调用 submitShellCommand（§10.1 非法调用）");
        return;
    }
    // 统一提交路径（§7.7/§10.4 副作用行）：菜单/顶栏/首页/快捷键/面板全部
    // 收口 registry.submit——前置校验（未知/不可执行/只读）与诊断出线在
    // 注册表内完成（UI-CMD-UNKNOWN/UI-CMD-NOT-EXECUTABLE），处理器在
    // registerCommand 时绑定。本路径只做拒绝的用户可见反馈（状态栏说明）。
    const CommandOutcome outcome = m_commands->submit(commandId);
    if (outcome.accepted) {
        return;  // 处理器自行反馈（会话命令的状态栏说明/面板打开等）
    }
    // 拒绝反馈（§7.4"禁用＋说明"）：未注册与不可执行分别呈现——诊断条目
    // （工厂注入时）已由注册表出线，此处是即时可见性补偿。
    const CommandAvailability a = m_commands->availability(commandId);
    if (!a.registered) {
        m_window->statusBar()->showMessage(
            QString::fromUtf8(u8"未知命令：") + QString::fromStdString(commandId), 4000);
        return;
    }
    m_window->statusBar()->showMessage(
        QString::fromUtf8(a.disableReasonKey == WorkbenchText::kReasonReadOnly
                              ? WorkbenchText::kReasonReadOnlyText
                              : WorkbenchText::kReasonNoProjectText),
        4000);
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
    // 门面映射：注册表可用性快照（§10.3）→ 壳层可用性词表（UI-T03 门面
    // 形态不变——观测方零迁移；registered 位与注册表同源）。
    const CommandAvailability a = m_commands->availability(commandId);
    ShellCommandAvailability out;
    out.registered = a.registered;
    out.visible = a.visible;
    out.enabled = a.enabled;
    out.reasonKey = a.disableReasonKey;
    return out;
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
