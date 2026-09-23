/**
 * @file   UiPlugin.cpp
 * @brief  工作台宿主插件实现（sdurws_ird_ui_plugin）——装配序列、会话入口
 *         编排、宿主框架菜单动作与共存观测留痕（零计算逻辑——全部界面
 *         语义经内容装配面 WorkbenchContent 与会话控制器承接）。
 *
 * 设计依据：
 *   - units/ui.md §13 UI-T16 行＋立项登记注（v1.9，O-38 裁决承接）、
 *     §11.5（触发时机编排归装配层——会话入口处理器覆写）、§5.2/§5.3
 *     （打开协议与 PM-07 显示差异——编排面与 HarnessMain 逐行同源）；
 *   - 框架 rws::RobWorkStudioPlugin 机制（零框架修改——SA-02）：本 DLL 由
 *     宿主 Plugins→Load plugin 动态装载（开发期验证通道——动态加载仅限
 *     开发期，SA-01 产品静态白名单不变）；本文件不 include 任何业务域
 *     计算面（插件零计算逻辑——ui.md §13 登记注）；
 *   - O-31 装配层特权（DTB §4.5）：适配器复用 harness 形态（app/
 *     PortAdapters.*），插件侧只做装配与编排，不代行 ui/对端任何语义。
 *
 * 留痕通道：装配/打开/共存观测事实全部经诊断栈 Dev 日志出线
 *   （diagnostics §6.2——插件不造第二套日志）；目录缺省为当前工作目录下
 *   ird-ui-plugin-logs（开发期通道形态，与 harness 的 ird-harness-logs
 *   同型；验收留痕经 traceability/builds/wp10-t16/ 固化）。
 */

#include "UiPlugin.hpp"

#include <QAction>
#include <QApplication>
#include <QFileDialog>
#include <QGridLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QStatusBar>
#include <QString>
#include <QTimer>
#include <QWidget>

#include <rws/RobWorkStudio.hpp>                 // 宿主注入面：getView()/getWorkCellScene()（共存最小接入）

#include <sdurws/ird/project/StoreTypes.hpp>     // project::StoreError（创建失败折叠）
#include <sdurws/ird/ui/ICommandRegistry.hpp>    // CommandOutcome/CommandParameter（会话入口覆写载体）

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace sdurws {
namespace ird {
namespace ui {

namespace {

/// 开发日志目录名（相对当前工作目录——开发期通道形态；与 harness 的
/// ird-harness-logs 同型不同名，两类通道的留痕互不混写）。
constexpr const char* kPluginLogDirName = "ird-ui-plugin-logs";

/// Dev 日志通道 token（diagnostics.md §7.2 LogChannel ≤48 字符；对齐
/// "diag/<单元>" 命名族——Dev 级事实的唯一出线通道 §6.2）。
constexpr const char* kPluginDevChannel = "diag/ui";

/// 控制台报告（宿主进程的控制台可用时可见；留痕主通道仍是 Dev 日志）。
void reportLine(const std::string& text)
{
    std::cout << "[ird-ui-plugin] " << text << std::endl;
}

/// 路径规范化（§9.3 口径——与 HarnessMain 打开编排同源；失败以原始路径
/// 继续＋Dev 留痕，不阻塞打开流程）。
std::string canonicalizePathOrKeep(const std::string& rawPath)
{
    try {
        return fs::weakly_canonical(fs::u8path(rawPath)).u8string();
    } catch (const fs::filesystem_error& error) {
        reportLine("路径规范化失败（以原始路径继续）：" + std::string(error.what()));
        return rawPath;
    }
}

}  // namespace

// =====================================================================
// 生命周期（框架插件协议）
// =====================================================================

IrdWorkbenchHostPlugin::IrdWorkbenchHostPlugin()
    // 插件名：Dock 标题/宿主 Plugins 菜单开关项/卸载对话框的显示名。
    // 图标留空（开发期通道不引入资源文件——零图标不是零功能）。
    : rws::RobWorkStudioPlugin(QString::fromUtf8("IRD 工作台"), QIcon())
{
}

void IrdWorkbenchHostPlugin::initialize()
{
    // 框架基类先行（宿主日志句柄注入——rws::RobWorkStudioPlugin::initialize 原语义）。
    RobWorkStudioPlugin::initialize();
    if (m_assembled) {
        return;  // 一次守卫（框架契约 initialize 恰好一次——防御性重复调用无害化）
    }

    // ---- 装配第一步：诊断栈（与 harness 同一装配序列——DiagnosticsAssembly）----
    const fs::path devlogDir = fs::current_path() / kPluginLogDirName;
    try {
        app::assembleDiagnostics(devlogDir, m_diag);
    } catch (const std::exception& error) {
        // 装配失败 fail-fast：框架捕获与否不由插件决定——留痕两条通道后
        // 原样上抛（禁止吞错；宿主 RW_THROW 机制会呈现装载失败）。
        reportLine(std::string("诊断栈装配失败：") + error.what());
        throw;
    }
    if (m_diag.pipeline) {
        m_diag.pipeline->logDev(kPluginDevChannel,
                                "插件诊断栈就绪（开发日志目录：" + devlogDir.u8string() + "）");
    }
    reportLine("诊断栈就绪（开发日志目录：" + devlogDir.u8string() + "）");

    // ---- 装配第二步：端口适配器（O-31 装配层特权边——复用 harness 形态）----
    // 会话生命周期端口占位形态与 harness 逐一同型：策略未装载（C-10）、
    // 名称不可解析（C-11）、关于框数据占位（§11.4）——零虚构语义。
    m_policySource = std::make_shared<app::UnloadedPolicySource>();
    m_nameResolver = std::make_shared<app::NullUiNameResolver>();
    m_aboutSource = std::make_shared<app::HarnessAboutSource>();
    m_bridge = std::make_shared<app::ProjectDiagnosticsBridge>(
        m_diag.catalog, m_diag.factory, m_diag.pipeline);
    m_storeFactory = std::make_shared<app::StoreFactoryPortAdapter>(*m_bridge);

    // ---- 会话控制器（§5 状态机——依赖就位后延迟构造，一次性注入依赖包；
    //      打开编排的推进面，与 HarnessMain 逐行同源）----
    ui::UiSessionControllerDeps sessionDeps;
    sessionDeps.storeFactory = m_storeFactory;
    sessionDeps.diagSink = m_diag.catalog;
    sessionDeps.diagFactory = m_diag.factory;
    sessionDeps.devLog = m_diag.pipeline;  // Dev 码唯一出线（diagnostics §6.2）
    // 上下文原子快照注入内容装配面（§10.1 v0.5 facets——状态行/首页/命令
    // 门控的单一数据源；控制器在打开成功/关闭完成时回调，UI 线程）。
    sessionDeps.presentContext = [this](const ui::ProjectContextProjection& context) {
        if (m_content) {
            m_content->presentProjectContext(context);
        }
    };
    m_controller = std::make_unique<UiSessionController>(std::move(sessionDeps));
    // saveAllDraftsManual/forceAbandonAll 不接线（与 harness 同口径——关闭
    // 链路的"保存"决议按控制器契约 fail-fast 而非静默降级）。

    // ---- 装配第三步：内容装配面（与 harness 共用的同一装配面——O-38 ②；
    //      嵌入式 Dock 宿主形态＋会话入口覆写＝本插件的两处宿主差异）----
    WorkbenchContentDeps contentDeps;
    contentDeps.wiring.eventBus = nullptr;  // 显式声明：无事件消费场景（同 harness v0.1 口径）
    contentDeps.wiring.diagSink = m_diag.catalog;
    contentDeps.wiring.redaction = m_diag.redaction;
    contentDeps.wiring.devLog = m_diag.pipeline;  // Dev 码唯一出线（diagnostics §6.2）
    contentDeps.wiring.diagFactory = m_diag.factory;
    contentDeps.wiring.policySource = m_policySource;
    contentDeps.wiring.nameResolver = m_nameResolver;
    contentDeps.wiring.aboutSource = m_aboutSource;
    contentDeps.hostKind = WorkbenchHostKind::EmbeddedDock;
    // 宿主控件＝插件本体（RobWorkStudioPlugin 即 QDockWidget 形态的
    // QWidget，随宿主主窗口安放）：QShortcut attach、命令面板与对话框
    // 的父窗口、内容 Widget 的初始父对象都以它为准——build() 必填校验
    // 之一（缺失＝内容无处安放，装配被拒）。初父在栅格安放时重挂为
    // Dock 体的区域栅格（buildDockBody）。
    // 宿主冒烟实证（2026-09-23）：漏注入本项时 build() 返回 false——
    // 插件装载被框架呈现为失败对话框、五区不出现；壳路径由
    // WorkbenchShell 注入自身窗口故未暴露。修复见同日提交。
    contentDeps.hostWidget = this;
    // 会话入口覆写（§11.5）：宿主插件的打开编排注入——首页/菜单/顶栏/
    // 面板/快捷键五处壳入口同走真实打开协议。
    contentDeps.openProjectHandler =
        [this](const std::vector<CommandParameter>& params) {
            return orchestrateOpenProject(params);
        };
    contentDeps.newProjectHandler =
        [this](const std::vector<CommandParameter>& params) {
            return orchestrateNewProject(params);
        };
    m_content = createWorkbenchContent(std::move(contentDeps));

    // ---- 装配第四步：Dock 体栅格＋内容装配面两段装配＋退出收口挂接 ----
    if (!buildDockBody()) {
        // 内容装配面校验被拒＝装配缺陷（build() 的必填校验覆盖三组：
        // wiring 非空项、宿主控件 hostWidget、几何/位形钩子成组——宿主
        // 冒烟曾实证 hostWidget 漏注入走到此处）：留痕后上抛，不留半
        // 装配插件（宿主呈现装载失败）。
        reportLine("内容装配面构建被拒（装配校验失败——wiring 非空项/"
                   "hostWidget/钩子成组之一不满足）");
        throw std::runtime_error("sdurws_ird_ui_plugin: 内容装配面构建被拒");
    }
    connectAppQuitDrain();

    m_assembled = true;
    // 装配完成与"呈现"分开表述（验收 attempt 1 的教训——B-1）：此刻 Dock
    // 尚不可见，框架 addPlugin 尾段（setVisible(PluginVisible_<名>)＋
    // restoreState(QtMainWindowState)）还没执行，它们会把本 Dock 置为
    // 隐藏——呈现结论只能由 reassertEmbeddedPresentation 在事件循环
    // 回归后给出（G1 装载门控的第二判据），此处不得提前声称"已嵌入"。
    reportLine("工作台装配完成（五区面板＋Ctrl+Shift+P 命令面板；"
               "装载呈现自证在事件循环稍后执行）");
    if (m_diag.pipeline) {
        m_diag.pipeline->logDev(kPluginDevChannel,
                                "内容装配面就位（嵌入式 Dock 宿主形态；会话入口覆写已注入）");
    }

    // ---- 装配第五步（时序关键）：装载呈现自证排队 ----
    // 零等待单发定时器：控制流回到事件循环的第一拍执行重申（此时 addPlugin
    // 已返回、其尾段 setVisible/restoreState 已完成——队列语义保证严格晚于
    // 二者，详见 reassertEmbeddedPresentation 内的根因链注释）。
    QTimer::singleShot(0, this, [this] { reassertEmbeddedPresentation(); });
}

IrdWorkbenchHostPlugin::~IrdWorkbenchHostPlugin()
{
    // 有界拆卸（幂等）：正常退出路径已随 aboutToQuit 收口——此处兜底
    // （插件实例由 QPluginLoader 持有到进程退出，析构可能不执行）。
    if (m_content) {
        m_content->shutdown();
        m_content.reset();
    }
}

// =====================================================================
// 宿主共存回调（O-38 裁决③——共存最小接入：只观测，零视图操作）
// =====================================================================

void IrdWorkbenchHostPlugin::open(rw::models::WorkCell* workcell)
{
    // 宿主装载工作单元＝宿主中央区 RWStudioView3D 将呈现三维场景；本插件
    // 让位（工作台面板与宿主中央视图并存，不承载、不复制三维能力）。此处
    // 只做注入面的只读观测留痕（getView()/getWorkCellScene()——验收操作
    // 序列"宿主三维共存"的可观测面），完整三维交互归 WP-10-T05 阶段 B。
    (void)workcell;  // 场景本体归宿主呈现——本插件零场景语义
    if (m_diag.pipeline) {
        const bool viewInPlace = getRobWorkStudio() != nullptr
                                 && getRobWorkStudio()->getView() != nullptr;
        m_diag.pipeline->logDev(
            kPluginDevChannel,
            std::string("宿主已装载工作单元——共存观测：RWStudioView3D ")
                + (viewInPlace ? "在位（宿主中央区承载三维视图，本插件让位）"
                               : "未就位（宿主中央视图尚未创建）"));
    }
}

void IrdWorkbenchHostPlugin::close()
{
    // 宿主关闭工作单元：观测留痕（零操作本体——工作台面板状态不随工作单元
    // 变化；ird 项目会话生命周期归 UiSessionController，与 rw WorkCell 正交）。
    if (m_diag.pipeline) {
        m_diag.pipeline->logDev(kPluginDevChannel, "宿主已关闭工作单元（工作台面板保持）");
    }
}

// =====================================================================
// 装配段实现
// =====================================================================

bool IrdWorkbenchHostPlugin::buildDockBody()
{
    // Dock 体：插件本体即框架主窗口的 QDockWidget（addPlugin 的
    // addDockWidget 目标）——体栅格安放内容装配面的五个区域＋状态行。
    // 红线：不建任何顶层 QMainWindow（O-38 裁决②——双菜单/双状态栏/
    // 双 Dock 管理反模式禁止；框架菜单/状态栏能力归宿主）。
    auto* body = new QWidget(this);
    body->setObjectName("ird_plugin_dock_body");
    auto* grid = new QGridLayout(body);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(2);

    if (!m_content->build()) {
        return false;  // wiring 校验被拒（装配缺陷——调用方上抛处理）
    }

    // 栅格安放（§4.1 五区在 Dock 体内的并置形态——自上而下：顶栏/三栏/
    // 底部/状态行；中央区吃伸展空间）：内容 Widget 从宿主控件重挂进栅格
    // （Qt 对象树托管生命周期）。
    grid->addWidget(m_content->topBarWidget(), 0, 0, 1, 3);
    grid->addWidget(m_content->leftWidget(), 1, 0);
    grid->addWidget(m_content->centralWidget(), 1, 1);
    grid->addWidget(m_content->rightWidget(), 1, 2);
    grid->addWidget(m_content->bottomWidget(), 2, 0, 1, 3);
    grid->addWidget(m_content->statusBarWidget(), 3, 0, 1, 3);
    // 中央区伸展（中央工作区吃剩余空间——§4.4）；三栏按内容最小尺寸呈现。
    grid->setColumnStretch(1, 1);
    grid->setRowStretch(1, 1);
    setWidget(body);
    m_dockBody = body;

    // 命令状态观察：内容装配层每次刷新使能态后同步框架菜单动作（§7.6
    // 三处一致禁用的宿主菜单半区——求值结果全在注册表，本插件零判定）。
    m_content->setCommandStateObserver([this] { refreshHostMenuActions(); });

    // 两段装配第二段：布局记忆（嵌入式＝三区可见性键）＋无项目首页＋
    // 写线程＋快捷键 attach＋命令面板（§10.1 v1.10 时序契约）。
    m_content->activate();
    return true;
}

void IrdWorkbenchHostPlugin::setupMenu (QMenu* menu)
{
    // 框架 Plugins 菜单注入（宿主 addPlugin 流程在 initialize 前回调——
    // 框架注入序保持）：基类先行（本插件面板的显示/隐藏开关——rws::
    // RobWorkStudioPlugin::setupMenu 原语义），本插件追加工作台命令入口。
    // 动作触发统一转发内容装配面的提交路径（§4.2 路由红线——菜单只路由
    // 命令板；使能态随命令可用性刷新——refreshHostMenuActions）。
    RobWorkStudioPlugin::setupMenu (menu);
    if (menu == nullptr) {
        return;  // 防御：框架以空菜单回调＝无注入面（基类动作已自我管理）
    }
    auto makeCommandAction = [this, menu](const char* title, const char* commandId) {
        auto* action = menu->addAction(QString::fromUtf8(title));
        // 动作父对象＝本插件（Qt 树托管——插件销毁随宿主进程收尾）。
        action->setParent(this);
        QObject::connect(action, &QAction::triggered, this, [this, commandId] {
            if (m_content) {
                m_content->submitCommand(commandId);
            }
        });
        m_hostMenuCommandIds.emplace_back(action, commandId);
    };
    menu->addSeparator();
    makeCommandAction("命令面板", "workbench.commandPalette");
    makeCommandAction("打开项目", "project.open");
    makeCommandAction("新建项目", "project.new");
    makeCommandAction("恢复默认布局", "view.resetLayout");
    // 五区开关（§4.1"支持隐藏"的宿主菜单承载——顶层壳视图菜单半区在嵌入
    // 式宿主的对位面；勾选态随内容装配层刷新同步，triggered/toggled 分离
    // 避免回环）。可隐藏三区（Top/Central 恒在——§4.4）。
    menu->addSeparator();
    const std::pair<WorkbenchRegion, const char*> regionToggles[] = {
        {WorkbenchRegion::Left, "左栏"},
        {WorkbenchRegion::Right, "右栏"},
        {WorkbenchRegion::Bottom, "底部任务和状态区"},
    };
    for (const auto& [region, title] : regionToggles) {
        auto* action = menu->addAction(QString::fromUtf8(title));
        action->setParent(this);
        action->setCheckable(true);
        QObject::connect(action, &QAction::triggered, this, [this, region] {
            if (m_content) {
                m_content->setRegionVisible(region, !m_content->regionVisible(region));
            }
        });
        m_hostRegionToggles.emplace_back(region, action);
    }
}

void IrdWorkbenchHostPlugin::refreshHostMenuActions()
{
    if (!m_content) {
        return;
    }
    // 框架菜单动作使能态＝内容装配层（命令注册表）求值结果（§7.6"三处
    // 一致禁用"——本插件零判定，只消费快照）。
    for (auto& [action, commandId] : m_hostMenuCommandIds) {
        const ShellCommandAvailability a = m_content->commandAvailability(commandId);
        action->setEnabled(a.enabled);
    }
    // 五区开关勾选态＝当前有效可见性（不回环：只 setChecked 不触发）。
    for (auto& [region, action] : m_hostRegionToggles) {
        action->setChecked(m_content->regionVisible(region));
    }
}

void IrdWorkbenchHostPlugin::connectAppQuitDrain()
{
    // 宿主退出时的有界落盘收口：插件不拥有应用生命周期（进程退出归宿主
    // ——PM-17 同口径），但布局旗标/最近项目/快捷键改绑的落盘队列需要
    // 一次 drainAndStop（§10.1"有界拆除"）。挂接 aboutToQuit（上下文＝
    // Dock 体——退出时仍在 Qt 树内，回调安全；content->shutdown 幂等）。
    if (QCoreApplication::instance() != nullptr && m_dockBody != nullptr) {
        QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit,
                         m_dockBody, [this] {
                             if (m_content) {
                                 m_content->shutdown();
                             }
                         });
    }
}

void IrdWorkbenchHostPlugin::reassertEmbeddedPresentation()
{
    // 防御守卫：本方法只应由 initialize 末尾排队的零等待定时器调用（正常
    // 时序下装配早已完成）；未装配即被调用＝装配缺陷，保持无操作不掩盖。
    if (!m_assembled) {
        return;
    }

    // 根因链（验收 attempt 1 阻断项 B-1 的实证修复，登记 ui.md §13 返工
    // 登记注）：本 initialize() 返回之后，框架 addPlugin 尾段还有两步会
    // 把本 Dock 置为不可见——
    //   ① plugin->setVisible(PluginVisible_<插件名> 的保存值，缺省取调用
    //      实参)：Plugins→Load plugin 对话框路径在框架里硬编码实参
    //      visible=false（RobWorkStudio.cpp loadPlugin() → setupPlugin(
    //      pathname, filename, 0, 1)）；
    //   ② restoreState(QtMainWindowState)：Qt 对主窗口状态 blob 里未登记
    //      的 Dock 一律按隐藏处理，而该 blob 是本插件装载之前保存的布局
    //      （宿主退出时 saveState 落盘 ini）——刚 addDockWidget 的本 Dock
    //      必然不在其中，恢复即被藏。
    // 两步都在框架侧（SA-02 零框架修改红线），且时序都在 initialize()
    // 之后——插件侧唯一可落点的位置是"控制流回到事件循环之后"：装载排
    // 队列的零等待单发定时器恰在 addPlugin 返回后的第一拍执行（事件循环
    // 语义保证严格晚于②），据此重申嵌入式呈现。
    //
    // 语义边界（为什么是"单发重申"而不是持续看护）：开发期验证通道取
    // "装载即呈现"口径——本方法只在装载后执行一次，不与用户后续的手动
    // 开关竞争（Plugins 菜单基类显示开关/Dock 关闭钮随时可再隐藏，插件
    // 不夺回）；跨会话的区域级可见性记忆仍归内容装配面（§4.5 用户级设置
    // ——PM-14），Dock 级呈现权在宿主装载语义下归本插件的装载自证。
    setFloating(false);  // 嵌入式形态钉死：非浮动（顶层漂浮窗口＝宿主形态违例）
    show();              // 恢复 Dock 呈现（仍在 addDockWidget 安放的停靠区内嵌于主窗口）
    reportLine("工作台 Dock 已呈现（宿主主窗口嵌入式——装载呈现自证完成）");
    if (m_diag.pipeline) {
        m_diag.pipeline->logDev(kPluginDevChannel,
                                "装载呈现自证完成（Dock 嵌入宿主主窗口可见）");
    }
}

// =====================================================================
// 会话入口编排（§11.5——触发时机编排归装配层；与 HarnessMain 逐行同源）
// =====================================================================

CommandOutcome IrdWorkbenchHostPlugin::orchestrateOpenProject(
    const std::vector<CommandParameter>& params)
{
    (void)params;  // 无参命令（§7.1 最小集——空 schema）
    CommandOutcome out;
    // 目录选择（宿主窗口为父——模态于宿主；取消＝用户撤单，accepted=false
    // 的静默形态：无项目状态不变，无错误可报）。
    QWidget* parent = m_dockBody.data();
    const QString dir = QFileDialog::getExistingDirectory(
        parent, QString::fromUtf8("打开项目"), QString(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (dir.isEmpty()) {
        return out;  // 用户取消——未发生打开请求
    }
    const bool ok = openViaSessionController(canonicalizePathOrKeep(dir.toStdString()));
    out.accepted = ok;
    return out;
}

CommandOutcome IrdWorkbenchHostPlugin::orchestrateNewProject(
    const std::vector<CommandParameter>& params)
{
    (void)params;  // 无参命令（§7.1 最小集——空 schema）
    CommandOutcome out;
    QWidget* parent = m_dockBody.data();

    // 步骤 1：目标目录（创建协议要求目录不存在或为空——零半成品纪律；
    // harness 预检给出可读提示而非异常中断，同款编排）。
    const QString dir = QFileDialog::getExistingDirectory(
        parent, QString::fromUtf8("新建项目——选择目标目录"), QString(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (dir.isEmpty()) {
        return out;  // 用户取消
    }
    std::error_code ec;
    const fs::path target = fs::u8path(dir.toStdString());
    if (fs::exists(target, ec) && !fs::is_empty(target, ec)) {
        QMessageBox::warning(parent, QString::fromUtf8("无法创建项目"),
                             QString::fromUtf8("目标目录已存在且非空：\n") + dir
                                 + QString::fromUtf8("\n（创建协议要求目录不存在或为空——零半成品纪律）"));
        return out;
    }

    // 步骤 2：显示名（PM-03 项目显示名——创建者即首个写权限持有者）。
    bool nameOk = false;
    const QString name = QInputDialog::getText(
        parent, QString::fromUtf8("新建项目"), QString::fromUtf8("项目显示名："),
        QLineEdit::Normal, QString::fromUtf8("新项目"), &nameOk);
    if (!nameOk || name.trimmed().isEmpty()) {
        // 取消或空名：不创建（创建协议 fail-fast 契约的前置自查——空名
        // 属调用方输入缺失，就地提示优于异常中断）。
        if (nameOk) {
            QMessageBox::warning(parent, QString::fromUtf8("无法创建项目"),
                                 QString::fromUtf8("项目显示名不能为空。"));
        }
        return out;
    }

    // 步骤 3：创建（组装区整体就位＋装载激活——结果 store 在作用域结束即
    // 析构＝隐式排空＋锁释放 §5.1 生命周期行；随后经标准打开协议进入会话
    // ——同一协议路径，锁干净，不走"进程内重复打开"的锁竞争分支）。
    const std::string displayName = name.trimmed().toStdString();
    try {
        project::OpenStoreResult created = project::ProjectStoreFactory::createNew(
            target, displayName, nullptr, m_bridge.get());
        (void)created;  // 创建产物随作用域析构（锁释放）——打开流在下方汇合
        reportLine("项目已创建（" + dir.toStdString() + "，显示名：" + displayName
                   + "）——释放创建锁后经打开协议进入");
    } catch (const project::StoreError& error) {
        // 创建失败的稳定诊断随 bridge 入目录；消息框呈现开发诊断 detail
        // （createNew 失败不留半成品的契约下，用户可据此清理后重试）。
        reportLine("项目创建失败：" + std::string(error.what()));
        QMessageBox::warning(parent, QString::fromUtf8("项目创建失败"),
                             QString::fromUtf8(error.what()));
        return out;
    } catch (const std::invalid_argument& error) {
        QMessageBox::warning(parent, QString::fromUtf8("项目创建失败"),
                             QString::fromUtf8(error.what()));
        return out;
    }

    // 步骤 4：经标准打开协议进入会话（与打开流同一协议路径）。
    const bool ok = openViaSessionController(canonicalizePathOrKeep(dir.toStdString()));
    out.accepted = ok;
    return out;
}

bool IrdWorkbenchHostPlugin::openViaSessionController(const std::string& canonicalPath)
{
    // 打开五步协议（§5.2 Opening 态）：状态机推进归 UiSessionController，
    // 本插件只做触发编排（§11.5）。可写打开（锁竞争/介质只读→PM-07 降级
    // 只读是成功形态——横幅与只读徽标由内容装配面呈现）。
    const SessionOpenReport report =
        m_controller ? m_controller->openProject(canonicalPath, UiOpenMode::Writable)
                     : SessionOpenReport{};  // 防御：未装配＝必失败报告（装配缺陷另行走 DEV 留痕）
    if (report.ok) {
        if (m_content) {
            m_content->noteRecentProject(canonicalPath);  // PM-10 最近项目（去重/上限壳内处理）
        }
        reportLine("项目已打开：" + canonicalPath + "（可写："
                   + (report.opened.metadata.writable ? "是" : "否（降级只读——见横幅）") + "）");
        return true;
    }
    // 失败：错误页数据（token＋detail＋路径）——开发期经消息框与控制台双
    // 通道呈现；壳保持无项目首页态（"当前项目不动"）。Dev 通道同步留痕。
    reportLine("打开失败（" + report.failure.errorCodeToken + "）："
               + report.failure.detail + " @ " + report.failure.projectPath);
    if (m_diag.pipeline) {
        m_diag.pipeline->logDev(
            kPluginDevChannel,
            "打开失败（" + report.failure.errorCodeToken + "）："
                + report.failure.detail + " @ " + report.failure.projectPath);
    }
    QMessageBox::warning(
        m_dockBody.data(), QString::fromUtf8("项目打开失败"),
        QString::fromUtf8("稳定码：") + QString::fromStdString(report.failure.errorCodeToken)
            + QString::fromUtf8("\n项目路径：")
            + QString::fromStdString(report.failure.projectPath)
            + QString::fromUtf8("\n详情：") + QString::fromStdString(report.failure.detail));
    return false;
}

}  // namespace ui
}  // namespace ird
}  // namespace sdurws
