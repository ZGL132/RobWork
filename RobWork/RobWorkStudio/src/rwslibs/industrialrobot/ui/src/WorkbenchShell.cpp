/**
 * @file   WorkbenchShell.cpp
 * @brief  工作台壳实现（UI-T16 起＝顶层窗口宿主层）：顶层 QMainWindow 的
 *         窗口 chrome——菜单栏、五区 Dock 包裹、中央区/状态行安放、出厂
 *         几何/位形快照与 resize 折叠转发；一切界面语义（五区内容/命令
 *         设施/布局记忆编排/上下文投影/最近项目）在内容装配层
 *         WorkbenchContentImpl（harness 与宿主插件共用的同一装配面——
 *         ui.md §10.1 v1.10 增量，O-38 裁决②）。
 *
 * 设计依据：
 *   - units/ui.md §10.1（IWorkbenchShell 契约表——前置/后置/错误类型/线程/
 *     所有权/副作用；本类仍是该门面的唯一实现）、§4.1（五区布局总图的
 *     窗口骨架半区——菜单与 Dock 容器）、§7.1~§7.6 语义经内容装配层承接
 *     （本类零命令判定逻辑——菜单动作只转发统一提交路径）；
 *   - O-31 裁决（DTB §4，2026-09-19）：ShellWiring 持有 ui 自有端口——
 *     产品面零对 project/evidence/execution/policy/runtime 的链接或 include；
 *   - O-38 裁决（DTB §4.2，2026-09-23）：壳拆两层，harness 行为回归零变化
 *     为硬验收项——本文件的窗口装配序/出厂快照时机/拆卸次序与 UI-T03
 *     以来形态逐行对应，语义主体原样在 WorkbenchContent.cpp。
 *
 * 实现期口径登记（承袭 UI-T03/UI-T06 登记，随本拆分复核不变）：
 *   - 视图开关/菜单动作的默认快捷键不在此安装（§7.3 唯一注册点归
 *     GlobalShortcutRegistry——内容装配层经其登记与 attach）；
 *   - 命令登记/可用性/提交统一归 CommandRegistry（§10.3——菜单动作触发
 *     转发内容装配层的统一提交路径，SA-16 唯一入口）；
 *   - Dev 级码经 wiring.devLog 出线（§6.2"Dev 走日志"——内容装配层承载）。
 */

#include "WorkbenchShell_p.hpp"
#include "WorkbenchContent_p.hpp"

#include <QAction>
#include <QDockWidget>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>

#include <sdurws/ird/ui/UiProjections.hpp>

namespace sdurws {
namespace ird {
namespace ui {
namespace detail {

// =====================================================================
// 主窗口：resize 转发钩子宿主（§4.4 尺寸折叠的入口——转发内容装配层）
// =====================================================================

void WorkbenchShellImpl::WorkbenchMainWindow::resizeEvent(QResizeEvent* event)
{
    // 先让 QMainWindow 完成自身布局，再把最终尺寸交给内容装配层的折叠判别
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
    m_wiring = wiring;  // 共享引用持有（所有权仍在 L5——§10.1 所有权行）

    // 装配序（§10.1 v1.10 两段装配的宿主层编排——每步产物是下一步的输入；
    // 时序与原单一壳实现逐行对应，harness 行为零变化的承载面）：
    //   ①窗口骨架（QMainWindow＋resize 转发钩子）→ ②内容装配层 build
    //   （同一装配面——五区内容＋命令设施；必填校验在 build 内）→ ③chrome
    //   安放（菜单/五区 Dock/中央区/状态栏/出厂快照/可见性目标登记）→
    //   ④观察钩子注册（菜单使能同步＋PM-11 标题转发）→ ⑤内容装配层
    //   activate（布局记忆装载＋无项目首页＋写线程＋快捷键/面板）。
    m_window = std::make_unique<WorkbenchMainWindow>();
    m_window->setObjectName("ird_workbench_main");
    m_window->resize(kFactoryWindowWidth, kFactoryWindowHeight);
    // §4.4 折叠判别挂在真实 resize 事件上（"8 px 内响应"——事件粒度即响应）。
    m_window->resizeHook = [this](QResizeEvent*) {
        if (m_content) {
            m_content->notifyHostResized(m_window->size());
        }
    };

    // ---- 内容装配层（两种宿主层共用的同一装配面——O-38 裁决②）----
    // 顶层窗口宿主提供几何/位形承载钩子（布局记忆的全量半区——窗口事实
    // 归宿主层，编排与损坏处置在内容装配层）；会话入口不覆写＝§7.1 阶段 A
    // 占位形态（harness 打开协议走 CLI 编排，UI-T15 行为零变化）。
    WorkbenchContentDeps contentDeps;
    contentDeps.wiring = m_wiring;
    contentDeps.hostKind = WorkbenchHostKind::TopLevelWindow;
    contentDeps.hostWidget = m_window.get();
    contentDeps.saveWindowGeometry = [this]() -> QByteArray {
        return m_window ? m_window->saveGeometry() : QByteArray();
    };
    contentDeps.saveWindowState = [this]() -> QByteArray {
        return m_window ? m_window->saveState() : QByteArray();
    };
    contentDeps.restoreWindowState = [this](const QByteArray& geometry,
                                            const QByteArray& state) -> bool {
        // 两段都成功才算恢复成功（任一失败＝损坏路径的判别输入——§4.5）。
        return m_window && m_window->restoreGeometry(geometry)
               && m_window->restoreState(state);
    };
    contentDeps.restoreFactoryWindowState = [this]() {
        if (m_window) {
            m_window->restoreGeometry(m_factoryGeometry);
            m_window->restoreState(m_factoryState);
        }
    };
    m_content = createWorkbenchContent(std::move(contentDeps));
    if (!m_content->build()) {
        // 必填校验被拒（wiring 必填指针为空/宿主控件为空）＝调用方错误——
        // fail-fast 返回值轨；半装配状态不留存（窗口与内容一并丢弃）。
        m_content.reset();
        m_window.reset();
        return false;
    }

    // ---- chrome 安放（§4.1 布局总图的窗口骨架半区）----
    buildMenus();
    buildDocks();

    // ---- 观察钩子（宿主层 chrome 与内容装配层的同步面）----
    // 命令状态：内容装配层每次刷新使能态后回调——菜单动作/视图开关据此
    // 同步（§7.6"三处一致禁用"的 chrome 半区；求值结果全在注册表——本类
    // 零判定逻辑）。
    m_content->setCommandStateObserver([this] { refreshChromeCommandStates(); });
    // PM-11 标题面：状态文本每次刷新回调——顶层宿主转发窗口标题
    // （"标题栏与状态栏"双面；嵌入式宿主可不注册）。
    m_content->setTitleTextObserver([this](const QString& text) {
        if (m_window) {
            m_window->setWindowTitle(text);
        }
    });
    refreshChromeCommandStates();  // 初始菜单使能态（无项目口径——与原实现一致）

    // ---- 内容装配层 activate（布局记忆装载＋初始上下文＋写线程＋快捷键）----
    m_content->activate();

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

    // ①布局/快捷键/面板近期落盘与设施收口（内容装配层：值序列化在 UI 线程
    //   ——Widget 尚存活，写盘交后台线程并有界收口 §10.1"有界拆除"）。
    //   落盘结论即 ShellTeardownReport.layoutPersisted（§10.1 返回值语义）。
    m_lastTeardown.layoutPersisted = m_content->shutdown();

    // ②窗口销毁（§10.1 后置：shutdown 后 mainWindow 不可再用——唯一出口
    //   返回 nullptr；窗口树子指针随销毁失效，chrome 引用一并清空）。
    m_window->close();
    m_window.reset();
    m_content.reset();      // 内容装配层已收口（shutdown 幂等）——实例随壳终结
    m_docks.fill(nullptr);
    m_commandActions.clear();
    m_viewToggles.clear();

    m_initialized = false;
    m_lastTeardown.completed = true;
    return m_lastTeardown;
}

// =====================================================================
// chrome 安放：菜单与五区 Dock（§4.1 布局总图的窗口骨架半区）
// =====================================================================

void WorkbenchShellImpl::buildMenus()
{
    // 命令动作辅助：动作触发统一转发内容装配层的提交路径（§4.2 路由红线
    // ——菜单只路由命令板；本类不触碰任何领域设施）。
    auto makeCommandAction = [this](const char* title, const char* commandId) {
        auto* action = new QAction(QString::fromUtf8(title), m_window.get());
        QObject::connect(action, &QAction::triggered, m_window.get(),
                [this, commandId] {
                    if (m_content) {
                        m_content->submitCommand(commandId);
                    }
                });
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
    // refreshChromeCommandStates，triggered/toggled 分离避免回环）。
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
                [this, region] {
                    if (m_content) {
                        m_content->setRegionVisible(region, !m_content->regionVisible(region));
                    }
                });
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

void WorkbenchShellImpl::buildDocks()
{
    // 五区入位（枚举序＝addDockWidget 调用序；中央区＝主视图区——§4.1）：
    // Dock 容器属窗口骨架（objectName 保留——布局记忆 restoreState 按
    // objectName 匹配 Dock），内容 Widget 全部来自内容装配层（同一装配面）。
    const struct {
        WorkbenchRegion region;
        Qt::DockWidgetArea area;
        const char* objectName;
        const char* title;
        QDockWidget::DockWidgetFeatures features;
    } dockSpecs[] = {
        // 顶栏：恒在三区之一——不可关闭/浮动（§4.4"顶栏恒在"），仅可移动；
        // 标题栏隐藏（工具条形态，非面板形态）。
        {WorkbenchRegion::Top, Qt::TopDockWidgetArea, "ird_dock_top", WorkbenchText::kDockTop,
         QDockWidget::DockWidgetMovable},
        {WorkbenchRegion::Left, Qt::LeftDockWidgetArea, "ird_dock_left", WorkbenchText::kDockLeft,
         QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable
             | QDockWidget::DockWidgetClosable},
        {WorkbenchRegion::Right, Qt::RightDockWidgetArea, "ird_dock_right", WorkbenchText::kDockRight,
         QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable
             | QDockWidget::DockWidgetClosable},
        {WorkbenchRegion::Bottom, Qt::BottomDockWidgetArea, "ird_dock_bottom", WorkbenchText::kDockBottom,
         QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable
             | QDockWidget::DockWidgetClosable},
    };
    for (const auto& spec : dockSpecs) {
        auto* dock = new QDockWidget(QString::fromUtf8(spec.title), m_window.get());
        dock->setObjectName(spec.objectName);
        dock->setFeatures(spec.features);
        if (spec.region == WorkbenchRegion::Top) {
            dock->setTitleBarWidget(new QWidget());  // 空 titular＝无标题栏
        }
        QWidget* content = nullptr;
        switch (spec.region) {
        case WorkbenchRegion::Top: content = m_content->topBarWidget(); break;
        case WorkbenchRegion::Left: content = m_content->leftWidget(); break;
        case WorkbenchRegion::Right: content = m_content->rightWidget(); break;
        case WorkbenchRegion::Bottom: content = m_content->bottomWidget(); break;
        default: break;  // Central 不入 Dock（主视图区——setCentralWidget）
        }
        dock->setWidget(content);  // 内容 Widget 重挂父子进 Dock（Qt 对象树）
        m_window->addDockWidget(spec.area, dock);
        const std::size_t index = static_cast<std::size_t>(spec.region);
        m_docks[index] = dock;
        // 可见性目标登记：三区隐藏语义作用于 Dock（隐藏 Dock＝UI-T03 以来
        // 行为——内容装配层的缺省目标是内容 Widget，此处覆盖）。
        m_content->setRegionVisibilityTarget(spec.region, dock);
    }

    // 中央区（内容页栈：首页/三维视图区域——宿主形态在内容装配层内定页）
    // 与状态栏（UI-T18 契约面变更——O-43 ③：内容装配层不再持有 QStatusBar，
    // 顶层宿主自有状态栏承载 PM-11 永久位＋瞬态消息，双观测钩子接线；
    // 呈现行为与原"内容装配层持有同一 QStatusBar"逐项等价：永久位同位
    // stretch、瞬态同 showMessage 语义）。
    m_window->setCentralWidget(m_content->centralWidget());
    QStatusBar* statusBar = m_window->statusBar();
    m_pm11Label = new QLabel(statusBar);
    m_pm11Label->setObjectName("ird_status_project_text");
    statusBar->addWidget(m_pm11Label, /*stretch=*/1);
    m_content->setStatusTextObserver([this](const QString& text) {
        if (m_pm11Label != nullptr) {
            m_pm11Label->setText(text);  // PM-11 永久位（不用 showMessage——瞬态位会超时清空）
        }
    });
    m_content->setStatusMessageObserver([statusBar](const QString& message, int timeoutMs) {
        statusBar->showMessage(message, timeoutMs);
    });

    // 出厂快照：此后 view.resetLayout / 损坏回退都以这两份字节为基准
    // （§4.4"恢复出厂位形"、§4.5"回退出厂默认布局"——快照在 Dock 全部
    // 入位后取，与原 buildWindow 时机一致；内容装配层经几何钩子回取）。
    m_factoryGeometry = m_window->saveGeometry();
    m_factoryState = m_window->saveState();
}

void WorkbenchShellImpl::refreshChromeCommandStates()
{
    if (!m_window || !m_content) {
        return;  // 窗口未建/内容未装配（装配序保证二者先于本调用）
    }
    // 菜单动作使能态＝内容装配层（命令注册表）求值结果（§7.6"三处一致
    // 禁用"——本类零判定，只消费快照）。
    for (auto& [action, commandId] : m_commandActions) {
        const ShellCommandAvailability a = m_content->commandAvailability(commandId);
        action->setEnabled(a.enabled);
    }
    // 视图开关勾选态＝当前有效可见性（复位/记忆恢复后同步——不回环：
    // 本处只 setChecked，不触发 triggered）。
    for (auto& [region, action] : m_viewToggles) {
        action->setChecked(m_content->regionVisible(region));
    }
}

// =====================================================================
// 门面转发（语义权威在内容装配层——§10.1 契约面不变，观测方零迁移）
// =====================================================================

bool WorkbenchShellImpl::regionVisible(WorkbenchRegion region) const
{
    return m_content ? m_content->regionVisible(region) : false;
}

void WorkbenchShellImpl::setRegionVisible(WorkbenchRegion region, bool visible)
{
    if (m_content) {
        m_content->setRegionVisible(region, visible);
    }
}

void WorkbenchShellImpl::resetLayout()
{
    if (m_content) {
        m_content->resetLayout();
    }
}

void WorkbenchShellImpl::presentProjectContext(const ProjectContextProjection& context)
{
    if (m_content) {
        m_content->presentProjectContext(context);
    }
}

ShellCommandAvailability WorkbenchShellImpl::commandAvailability(
    const std::string& commandId) const
{
    return m_content ? m_content->commandAvailability(commandId) : ShellCommandAvailability{};
}

std::vector<RecentProjectEntry> WorkbenchShellImpl::recentProjects() const
{
    return m_content ? m_content->recentProjects() : std::vector<RecentProjectEntry>{};
}

void WorkbenchShellImpl::noteRecentProject(const std::string& canonicalPath)
{
    if (m_content) {
        m_content->noteRecentProject(canonicalPath);
    }
}

void WorkbenchShellImpl::removeRecentProject(const std::string& canonicalPath)
{
    if (m_content) {
        m_content->removeRecentProject(canonicalPath);
    }
}

QWidget* WorkbenchShellImpl::mainWindow()
{
    // 唯一 Widget 出口（§10.1）；shutdown 后返回 nullptr（后置条件原文
    // "shutdown 后 mainWindow 不可再用"）。
    return m_window.get();
}

}  // namespace detail

std::unique_ptr<IWorkbenchShell> createWorkbenchShell()
{
    return std::make_unique<detail::WorkbenchShellImpl>();
}

}  // namespace ui
}  // namespace ird
}  // namespace sdurws
