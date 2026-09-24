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
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QDockWidget>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenuBar>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QStatusBar>
#include <QString>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <rws/RobWorkStudio.hpp>                 // 宿主注入面：getView()/getWorkCellScene()/menuBar()（共存接入）

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

/// 会话标签常量（宿主菜单/对话框的装配层呈现文案——与 WorkbenchText 键面
/// 词汇保持一致；插件侧不 include src/ 私有头，按既有 orchestrate 对话框
/// 先例以字面量承载）。
constexpr const char* kProjectMenuTitle = "工业机器人项目";
constexpr const char* kRecentMenuTitle = "最近项目";
constexpr const char* kViewMenuTitle = "视图";
constexpr const char* kRecentUnavailableSuffix = "（项目位置不可用）";

/**
 * @brief 打开五步协议的捕获包装（UI-T17）：转发内层 StoreFactoryPortAdapter
 *        的 open，并保留每次成功打开的绑定集——草稿写半区端口（IUiDraft
 *        StorePort）由 StorePortAdapter 双面实现经 dynamic_pointer_cast 取
 *        得，供打开成功后的 DraftController bindSession（O-31 装配层特权：
 *        同时看见两边写包装，ui 冻结面 SessionPortBundle 零改动）。
 *
 * 生命周期：插件持有 shared_ptr；m_lastStore 与控制器绑定集共享同一端口
 * 实例（shared 引用），不产生第二份所有权语义。
 */
class BundleCapturingStoreFactory final : public IUiStoreFactoryPort {
public:
    /// @param inner [in] 内层工厂适配器（共享持有——存活期覆盖本包装）。
    explicit BundleCapturingStoreFactory(std::shared_ptr<IUiStoreFactoryPort> inner)
        : m_inner(std::move(inner))
    {
    }

    /// @brief 直转 open 并在成功时捕获绑定集（失败不写——"失败＝无绑定泄漏"）。
    OpenStoreOutcome open(const std::string& canonicalPath,
                          UiOpenMode mode,
                          SessionPortBundle& outBindings) override
    {
        const OpenStoreOutcome outcome = m_inner->open(canonicalPath, mode, outBindings);
        if (outcome.ok) {
            m_lastStore = outBindings.store;  // shared 拷贝＝观察同一实例
        }
        return outcome;
    }

    /// @brief 最近一次成功打开的草稿写半区端口（双面适配器_cast；未打开＝空）。
    std::shared_ptr<IUiDraftStorePort> lastDraftStore() const
    {
        return std::dynamic_pointer_cast<IUiDraftStorePort>(m_lastStore);
    }

private:
    /// 内层工厂（StoreFactoryPortAdapter——对端翻译面）。
    std::shared_ptr<IUiStoreFactoryPort> m_inner;
    /// 最近一次成功打开的 store 端口（双面适配器——DraftStore 强转源）。
    std::shared_ptr<IUiProjectStorePort> m_lastStore;
};

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
    // 打开工厂＝捕获包装（UI-T17）包住对端翻译适配器：包装只透传并捕获
    // 成功绑定集（草稿写半区端口来源），对端翻译语义零改动。
    m_storeFactory = std::make_shared<BundleCapturingStoreFactory>(
        std::make_shared<app::StoreFactoryPortAdapter>(*m_bridge));

    // ---- 会话控制器（§5 状态机——依赖就位后延迟构造，一次性注入依赖包；
    //      打开编排的推进面，与 HarnessMain 逐行同源）----
    ui::UiSessionControllerDeps sessionDeps;
    sessionDeps.storeFactory = m_storeFactory;
    sessionDeps.diagSink = m_diag.catalog;
    sessionDeps.diagFactory = m_diag.factory;
    sessionDeps.devLog = m_diag.pipeline;  // Dev 码唯一出线（diagnostics §6.2）
    // 上下文原子快照注入内容装配面（§10.1 v0.5 facets——状态行/首页/命令
    // 门控的单一数据源；控制器在打开成功/关闭完成时回调，UI 线程）。
    // UI-T17 增量：上下文清空（项目关闭完成）时同步解绑草稿控制器会话
    // （§8.6 表处置——模块表/局部栈清空，磁盘草稿零触碰）。
    sessionDeps.presentContext = [this](const ui::ProjectContextProjection& context) {
        if (!context.project.has_value() && m_draft && m_draft->hasSession()) {
            m_draft->unbindSession();
        }
        if (m_content) {
            m_content->presentProjectContext(context);
        }
    };
    // 关闭对话框"保存"决议的执行半区（§5.4/[保存]→saveAll(Manual)）：
    // UI-T17 起接线（此前与 harness 同口径不接线——关闭链路不可达；本任务
    // 装配 DraftController 后链路真实可达，未绑定/保存失败经返回值轨反馈）。
    sessionDeps.saveAllDraftsManual = [this]() -> bool { return saveDraftsNow(); };
    // T_force 强杀兜底（§11.5 分工：abandonAll 调用权在 L5）：开发期无
    // execution 引擎装配＝零后台任务可放弃（SessionTaskPortStub 恒空同源
    // 事实）——显式留痕不静默（resolveForceCloseDialog 要求已接线）。
    sessionDeps.forceAbandonAll = [this]() {
        if (m_diag.pipeline) {
            m_diag.pipeline->logDev(kPluginDevChannel,
                                    "forceAbandonAll：无执行引擎装配——零任务可放弃（T_force 确认后的空兜底）");
        }
    };
    m_controller = std::make_unique<UiSessionController>(std::move(sessionDeps));

    // ---- 草稿链装配（UI-T17——§8：执行器＋控制器；保存链路真实）----
    assembleDraftChain();

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
    // 会话入口覆写（§11.5）：宿主插件的打开/新建/保存/关闭编排注入——
    // 首页/菜单/顶栏/面板/快捷键五处壳入口同走真实协议（UI-T17 扩至四命令）。
    contentDeps.openProjectHandler =
        [this](const std::vector<CommandParameter>& params) {
            return orchestrateOpenProject(params);
        };
    contentDeps.newProjectHandler =
        [this](const std::vector<CommandParameter>& params) {
            return orchestrateNewProject(params);
        };
    contentDeps.saveProjectHandler =
        [this](const std::vector<CommandParameter>& params) {
            return orchestrateSaveProject(params);
        };
    contentDeps.closeProjectHandler =
        [this](const std::vector<CommandParameter>& params) {
            return orchestrateCloseProject(params);
        };
    m_content = createWorkbenchContent(std::move(contentDeps));

    // ---- 状态投影绑宿主状态栏（UI-T18——O-43 ③：宿主 chrome 唯一）----
    // PM-11 永久文本与瞬态消息经双观测钩子直投 getRobWorkStudio()->statusBar()
    // （宿主注入先于 initialize——setRobWorkStudio→setupMenu→initialize 序）；
    // v1.11"状态行钉底恒可见"的语义等价迁移＝宿主状态栏本身恒可见（强于
    // Dock 内钉底——任何 Dock 开关都不再影响状态投影）。永久位仅添加一次
    // （initialize 恰好一次——一次守卫保证）。
    if (getRobWorkStudio() != nullptr) {
        m_hostStatusBar = getRobWorkStudio()->statusBar();
    }
    if (m_hostStatusBar != nullptr) {
        auto* pm11 = new QLabel(m_hostStatusBar);
        pm11->setObjectName("ird_status_project_text");
        m_hostStatusBar->addWidget(pm11, /*stretch=*/1);
        m_content->setStatusTextObserver([pm11](const QString& text) {
            pm11->setText(text);  // PM-11 永久位（不用 showMessage——瞬态位会超时清空）
        });
        m_content->setStatusMessageObserver(
            [this](const QString& message, int timeoutMs) {
                if (m_hostStatusBar != nullptr) {
                    m_hostStatusBar->showMessage(message, timeoutMs);
                }
            });
    } else {
        reportLine("宿主状态栏不可得——状态投影无呈现面（降级形态，如实留痕）");
        if (m_diag.pipeline) {
            m_diag.pipeline->logDev(kPluginDevChannel,
                                    "宿主状态栏不可得——PM-11/瞬态消息无投影面（装配降级）");
        }
    }

    // ---- 装配第四步：多 Dock 拓扑＋内容装配面两段装配＋退出收口挂接 ----
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
    reportLine("工作台装配完成（多 Dock：主 Dock＋属性/任务同级 Dock＋宿主状态栏投影＋Ctrl+Shift+P 命令面板；"
               "装载呈现自证在事件循环稍后执行）");
    if (m_diag.pipeline) {
        m_diag.pipeline->logDev(kPluginDevChannel,
                                "内容装配面就位（多 Dock 嵌入形态；会话入口覆写已注入；状态投影绑宿主状态栏）");
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
    // 草稿链收口（UI-T17）：先停执行器（有界排空在途落盘任务）再释放
    // 控制器——m_draft 析构前其分派的落盘任务必须已执行完（IDraftController
    // 生命周期契约：L5 保证控制器存活至落盘执行器排空）。
    if (m_diskExecutor) {
        m_diskExecutor->stop();
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
    // 多 Dock 拓扑（UI-T18——O-43 裁决③：单一工作台 Dock 五区栅格拆分）：
    //   ①插件本体 Dock＝主 Dock（Left 停靠区）——命令条（顶栏）＋项目导航
    //     （左栏）纵排；
    //   ②IRD 属性与诊断 Dock（Right 停靠区）＝右栏内容（本插件新建、宿主
    //     addDockWidget 同级注册——addDockWidget 延后到装载呈现自证，彼时
    //     插件已入宿主主窗口）；
    //   ③IRD 任务和状态 Dock（Bottom 停靠区）＝底部内容（同上）。
    // 中央区不安放（宿主中央 RWStudioView3D 唯一所有三维——O-38 裁决③；
    // 内容装配面的中央让位页保持已构建不挂载，零呈现面）。状态行不进任何
    // Dock——PM-11 永久投影与瞬态消息经双观测钩子直投宿主状态栏（宿主
    // chrome 唯一，v1.11"状态行钉底"的语义等价迁移见落位登记注）。
    // 红线不变：不建任何顶层 QMainWindow（O-38 裁决②）。
    auto* body = new QWidget(this);
    body->setObjectName("ird_plugin_dock_body");
    auto* mainLayout = new QVBoxLayout(body);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(2);

    if (!m_content->build()) {
        return false;  // wiring 校验被拒（装配缺陷——调用方上抛处理）
    }

    // 主 Dock 体（内容 Widget 从宿主控件重挂进纵排——Qt 对象树托管）。
    mainLayout->addWidget(m_content->topBarWidget(), /*stretch=*/0);
    mainLayout->addWidget(m_content->leftWidget(), /*stretch=*/1);
    setWidget(body);
    m_dockBody = body;

    // 右/底 Dock 创建（父对象＝本插件；装载呈现自证时 addDockWidget 重挂
    // 进宿主主窗口——插件本体在 addPlugin 尾段才入主窗口，彼时宿主窗口
    // 才可寻址）。objectName 供宿主状态 blob 与排障日志定位。
    m_propsDock = new QDockWidget(QString::fromUtf8("IRD 属性与诊断"), this);
    m_propsDock->setObjectName("ird_props_dock");
    m_propsDock->setWidget(m_content->rightWidget());
    m_tasksDock = new QDockWidget(QString::fromUtf8("IRD 任务和状态"), this);
    m_tasksDock->setObjectName("ird_tasks_dock");
    m_tasksDock->setWidget(m_content->bottomWidget());

    // 可见性目标登记（chrome 安放在 activate 之前——两段装配时序契约；
    // 三区开关语义自此作用于 Dock 本体：左＝插件主 Dock、右/底＝同级 Dock。
    // Top/Central 按契约登记为无操作——顶栏随主 Dock、中央归宿主）。
    m_content->setRegionVisibilityTarget(WorkbenchRegion::Left, this);
    m_content->setRegionVisibilityTarget(WorkbenchRegion::Right, m_propsDock);
    m_content->setRegionVisibilityTarget(WorkbenchRegion::Bottom, m_tasksDock);

    // 命令状态观察：内容装配层每次刷新使能态后同步框架菜单动作（§7.6
    // 三处一致禁用的宿主菜单半区——求值结果全在注册表，本插件零判定）。
    m_content->setCommandStateObserver([this] { refreshHostMenuActions(); });

    // 两段装配第二段：布局记忆（嵌入式＝三区可见性键，此刻施加到三个
    // Dock）＋无项目首页＋写线程＋快捷键 attach＋命令面板（§10.1 时序）。
    m_content->activate();
    return true;
}

void IrdWorkbenchHostPlugin::setupMenu (QMenu* menu)
{
    // 框架 Plugins 菜单注入（宿主 addPlugin 流程在 initialize 前回调——
    // 框架注入序保持）：基类先行（本插件面板的显示/隐藏开关——rws::
    // RobWorkStudioPlugin::setupMenu 原语义）。
    // UI-T17（O-43 ②）宿主菜单融合：本菜单（Plugins）只保留基类显示/隐藏
    // 开关，不再承载工作台命令——项目命令迁宿主 File 菜单、命令面板迁
    // Tools、区域开关与布局复位迁自建"视图"菜单（registerHostMenus）。
    RobWorkStudioPlugin::setupMenu (menu);
    registerHostMenus();
}

void IrdWorkbenchHostPlugin::registerHostMenus()
{
    // 宿主菜单定位（SA-02 零框架修改——只经公开 menuBar() 读宿主菜单结构；
    // 找不到目标菜单＝宿主形态异常，留痕后保持 Plugins-only 降级形态）。
    auto* studio = getRobWorkStudio();
    QMenuBar* menuBar = studio != nullptr ? studio->menuBar() : nullptr;
    if (menuBar == nullptr) {
        reportLine("宿主菜单栏未就位——命令保持 Plugins 菜单承载（降级形态）");
        return;
    }
    const auto findHostMenu = [menuBar](const char* title) -> QMenu* {
        for (QAction* action : menuBar->actions()) {
            if (QMenu* m = action->menu(); m != nullptr && m->title() == QString::fromLatin1(title)) {
                return m;
            }
        }
        return nullptr;
    };
    QMenu* fileMenu = findHostMenu("&File");
    QMenu* toolsMenu = findHostMenu("&Tools");

    // ---- File：按位插入"工业机器人项目"子菜单 ----
    // 为什么按位插入而不是尾插：宿主 updateLastFiles 每次 open WorkCell 后
    // 移除重加"最近文件"条目（追加在菜单尾部）——尾插的项目子菜单会被
    // 后续重排顶到最近文件之下；锚定 Preferences 前的分隔符保持稳定分组。
    if (fileMenu != nullptr) {
        auto* projectMenu = new QMenu(QString::fromUtf8(kProjectMenuTitle), fileMenu);
        addCommandAction(projectMenu, "新建项目", "project.new");
        addCommandAction(projectMenu, "打开项目", "project.open");
        addCommandAction(projectMenu, "保存草稿", "draft.save");
        addCommandAction(projectMenu, "项目另存为", "project.saveAs");
        addCommandAction(projectMenu, "关闭项目", "workbench.closeProject");
        // 最近项目子菜单（PM-10）：内容装配时清空、aboutToShow 现取重建
        // （去重/上限/失效提示全在内容装配面——本插件只投影）。
        m_recentMenu = new QMenu(QString::fromUtf8(kRecentMenuTitle), projectMenu);
        connect(m_recentMenu, &QMenu::aboutToShow, this, [this] { rebuildRecentMenu(); });
        projectMenu->addSeparator();
        projectMenu->addMenu(m_recentMenu);

        // 插入锚：Preferences 动作之前最近的分隔符（无则退化为 Preferences
        // 动作本身、再无则追加尾部）。
        QAction* anchor = nullptr;
        const QList<QAction*> fileActions = fileMenu->actions();
        for (int i = 0; i < fileActions.size(); ++i) {
            if (fileActions[i]->text() == QString::fromLatin1("&Preferences")) {
                for (int j = i - 1; j >= 0; --j) {
                    if (fileActions[j]->isSeparator()) {
                        anchor = fileActions[j];
                        break;
                    }
                }
                if (anchor == nullptr) {
                    anchor = fileActions[i];
                }
                break;
            }
        }
        if (anchor != nullptr) {
            fileMenu->insertMenu(anchor, projectMenu);
        } else {
            fileMenu->addMenu(projectMenu);
        }
    } else {
        reportLine("宿主 File 菜单未定位——项目命令未上菜单（降级形态）");
    }

    // ---- Tools：命令面板入口（UX-13 键盘可达入口的菜单半区）----
    if (toolsMenu != nullptr) {
        addCommandAction(toolsMenu, "命令面板", "workbench.commandPalette");
    }

    // ---- 视图（自建——宿主无 View 菜单）：三区开关＋恢复默认布局 ----
    // 区域开关语义（§4.1"支持隐藏"的宿主菜单承载；勾选态随内容装配层刷新
    // 同步，triggered/toggled 分离避免回环）。可隐藏三区（Top/Central 恒在
    // ——§4.4）。插入位置：Plugins 菜单之前（File/Tools 之后）。
    auto* viewMenu = new QMenu(QString::fromUtf8(kViewMenuTitle), menuBar);
    const std::pair<WorkbenchRegion, const char*> regionToggles[] = {
        {WorkbenchRegion::Left, "左栏"},
        {WorkbenchRegion::Right, "右栏"},
        {WorkbenchRegion::Bottom, "底部任务和状态区"},
    };
    for (const auto& [region, title] : regionToggles) {
        auto* action = viewMenu->addAction(QString::fromUtf8(title));
        action->setParent(this);
        action->setCheckable(true);
        QObject::connect(action, &QAction::triggered, this, [this, region] {
            if (m_content) {
                m_content->setRegionVisible(region, !m_content->regionVisible(region));
            }
        });
        m_hostRegionToggles.emplace_back(region, action);
    }
    viewMenu->addSeparator();
    addCommandAction(viewMenu, "恢复默认布局", "view.resetLayout");
    QAction* beforeView = nullptr;
    for (QAction* action : menuBar->actions()) {
        if (QMenu* m = action->menu(); m != nullptr && m->title() == QString::fromLatin1("&Plugins")) {
            beforeView = action;
            break;
        }
    }
    if (beforeView != nullptr) {
        menuBar->insertMenu(beforeView, viewMenu);
    } else {
        menuBar->addMenu(viewMenu);
    }
}

QAction* IrdWorkbenchHostPlugin::addCommandAction(QMenu* target,
                                                  const char* title,
                                                  const char* commandId)
{
    // 菜单只路由命令板（§4.2 路由红线——触发统一转发内容装配面提交路径）；
    // 使能态随命令可用性快照刷新（refreshHostMenuActions——本插件零判定）。
    QAction* action = target->addAction(QString::fromUtf8(title));
    action->setParent(this);  // 动作父对象＝本插件（Qt 树托管——随宿主收尾）
    QObject::connect(action, &QAction::triggered, this, [this, commandId] {
        if (m_content) {
            m_content->submitCommand(commandId);
        }
    });
    m_hostMenuCommandIds.emplace_back(action, commandId);
    return action;
}

void IrdWorkbenchHostPlugin::rebuildRecentMenu()
{
    if (m_recentMenu == nullptr || !m_content) {
        return;
    }
    m_recentMenu->clear();
    const std::vector<RecentProjectEntry> entries = m_content->recentProjects();
    if (entries.empty()) {
        // 空清单＝占位行（不虚构条目——PM-10 首页语义的菜单对位）。
        QAction* empty = m_recentMenu->addAction(QString::fromUtf8("（无）"));
        empty->setEnabled(false);
        return;
    }
    for (const RecentProjectEntry& entry : entries) {
        QString label = QString::fromStdString(entry.canonicalPath);
        if (!entry.available) {
            // 失效项保留并提示（PM-10"失效≠删除"——禁用动作，不删除条目）。
            label += QString::fromUtf8(kRecentUnavailableSuffix);
        }
        QAction* action = m_recentMenu->addAction(label);
        action->setEnabled(entry.available);
        const std::string path = entry.canonicalPath;
        connect(action, &QAction::triggered, this,
                [this, path] { openRecentProject(path); });
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
                             // 草稿链退出收口（UI-T17）：落盘执行器有界排空
                             // （在途保存任务执行完再收线程——§5.7"在途草稿
                             // 落盘完成后上下文才释放"的宿主侧对位）。
                             if (m_diskExecutor) {
                                 m_diskExecutor->stop();
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

    // 多 Dock 拓扑收口（UI-T18——O-43 ③）：右/底两个同级 Dock 在本拍入宿主
    // 主窗口（此时插件已入主窗口、宿主窗口可寻址——addDockWidget 把 Dock
    // 从插件父子树重挂进主窗口），与主 Dock 同受宿主装载语义支配（框架尾段
    // restoreState 对状态 blob 未登记的 Dock 按隐藏处理——与主 Dock 同根因
    // 链，故一并重显）。单发重申语义与主 Dock 一致：只此一拍，不与用户后续
    // 手动开关竞争。
    auto* hostWindow = qobject_cast<QMainWindow*>(parentWidget());
    if (hostWindow != nullptr && m_propsDock != nullptr && m_tasksDock != nullptr) {
        hostWindow->addDockWidget(Qt::RightDockWidgetArea, m_propsDock);
        hostWindow->addDockWidget(Qt::BottomDockWidgetArea, m_tasksDock);
        m_propsDock->show();
        m_tasksDock->show();
        // 区域旗标重施（UI-T18——PM-14 跨会话记忆不被装载重显夺回）：三区
        // 可见性目标已改绑 Dock 本体，activate 期恢复的用户旗标若为"隐藏"，
        // 上面的重显 show() 会把它顶回可见——与跨会话记忆矛盾。此处按内容
        // 装配面的模型位（regionVisible 返回用户意愿位，非 Widget 实测态）
        // 重施一次：用户隐藏的区保持隐藏（可经宿主"视图"菜单重新开启），
        // 无隐藏记忆（缺省）时与重显结果一致。主 Dock（Left 目标）不在此
        // 重施——其装载呈现维持 v1.11 注册口径（G1 门控判据"装载即呈现"；
        // 主 Dock 承载命令条，整 Dock 隐藏将无处承载工作台入口）。
        m_propsDock->setVisible(m_content->regionVisible(WorkbenchRegion::Right));
        m_tasksDock->setVisible(m_content->regionVisible(WorkbenchRegion::Bottom));
    }

    // 共存形态收口（O-38 裁决③"三维共存最小接入"的形态保障）：装载序列中
    // addDockWidget 先按停靠区整幅宽给位、随后 setVisible(false) 隐藏——重显
    // （上一行 show()）会恢复该整幅宽，宿主中央 RWStudioView3D 被挤压为零
    // （attempt 2 首录截图实证：五区完整可见但三维视图不可见）。故在 show()
    // 布局落定后的下一拍用 resizeDocks 显式把 Dock 宽度收束到宿主主窗口客户
    // 宽的约 2/5——中央三维视图保有其余宽度，两能力同帧共存。连续两拍各发一
    // 次收束（show 布局与主窗口布局的落定拍序不由插件决定，第二拍兜底）；
    // 结果宽度如实留痕——若被内容最小宽度钳制（该形态下顶栏按钮行很宽），
    // 收束只能到达钳制宽度，此时宿主窗口越宽三维视图所得越多，如实呈现。
    // 右/底 Dock（UI-T18）同拍给一次合理初值（右＝宿主宽约 1/5 钳制到内容
    // 最小宽 280 px 以上；底＝宿主高约 1/4 钳制到内容最小高 160 px 以上——
    // §4.4 各区最小尺寸），此后尺寸归用户拖拽与宿主布局管理。
    auto issueDockWidthShrink = [this] {
        auto* hostWindow = qobject_cast<QMainWindow*>(parentWidget());
        if (hostWindow == nullptr) {
            return;  // 未嵌宿主主窗口＝异常装载形态（防御——不越权假设父型）
        }
        const int targetWidth = qBound(420, hostWindow->width() * 2 / 5, 1024);
        hostWindow->resizeDocks({this}, {targetWidth}, Qt::Horizontal);
        if (m_propsDock != nullptr) {
            const int propsWidth = qBound(300, hostWindow->width() / 5, 480);
            hostWindow->resizeDocks({m_propsDock}, {propsWidth}, Qt::Horizontal);
        }
        if (m_tasksDock != nullptr) {
            const int tasksHeight = qBound(190, hostWindow->height() / 4, 340);
            hostWindow->resizeDocks({m_tasksDock}, {tasksHeight}, Qt::Vertical);
        }
        reportLine("工作台 Dock 宽度收束：目标 " + std::to_string(targetWidth)
                   + " px，实际 " + std::to_string(width())
                   + " px（受内容最小宽度钳制时如实留痕）");
        // 装载几何事实一次性落 Dev 日志（排障面——多 Dock 拓扑下主/右/底
        // 三 Dock 的尺寸与可见性；Dev 通道 §6.2，不进控制台）。
        if (m_diag.pipeline != nullptr) {
            const QWidget* bodyW = m_dockBody.data();
            std::string facts = "[geometry] dock=" + std::to_string(width()) + "x"
                                + std::to_string(height());
            if (bodyW != nullptr) {
                const QSize bodyMin = bodyW->minimumSizeHint();
                facts += " body=" + std::to_string(bodyW->width()) + "x"
                         + std::to_string(bodyW->height())
                         + " bodyMin=" + std::to_string(bodyMin.width()) + "x"
                         + std::to_string(bodyMin.height());
            }
            if (m_propsDock != nullptr) {
                facts += " props=" + std::to_string(m_propsDock->width()) + "x"
                         + std::to_string(m_propsDock->height())
                         + " visible=" + (m_propsDock->isVisible() ? "1" : "0");
            }
            if (m_tasksDock != nullptr) {
                facts += " tasks=" + std::to_string(m_tasksDock->width()) + "x"
                         + std::to_string(m_tasksDock->height())
                         + " visible=" + (m_tasksDock->isVisible() ? "1" : "0");
            }
            if (m_hostStatusBar != nullptr) {
                facts += std::string(" hostStatusBar visible=")
                         + (m_hostStatusBar->isVisible() ? "1" : "0");
            }
            m_diag.pipeline->logDev(kPluginDevChannel, facts);
        }
    };
    QTimer::singleShot(0, this, issueDockWidthShrink);
    QTimer::singleShot(100, this, issueDockWidthShrink);

    reportLine("工作台多 Dock 已呈现（主 Dock＋属性/任务 Dock＋宿主状态栏投影——装载呈现自证完成）");
    if (m_diag.pipeline) {
        m_diag.pipeline->logDev(kPluginDevChannel,
                                "装载呈现自证完成（多 Dock 嵌入宿主主窗口可见；状态投影归宿主状态栏）");
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
        // 草稿会话绑定（UI-T17——§5.2"打开成功→草稿侧编排"）：捕获包装里
        // 取草稿写半区端口（StorePortAdapter 双面）；分支锚＝缺省值（诚实
        // 边界——立项登记注③：本阶段无挂接模块，锚不可达）。
        if (m_draft) {
            if (m_draft->hasSession()) {
                m_draft->unbindSession();  // 切换流表处置（§8.6——清空再绑）
            }
            auto* capturing = dynamic_cast<BundleCapturingStoreFactory*>(m_storeFactory.get());
            std::shared_ptr<IUiDraftStorePort> draftStore =
                capturing != nullptr ? capturing->lastDraftStore() : nullptr;
            if (report.opened.metadata.writable && draftStore == nullptr) {
                // 双面适配器缺失＝装配缺陷：留痕并保持未绑定（保存路径经
                // saveDraftsNow 的未绑定检查走返回值轨，不虚构"已绑定"）。
                if (m_diag.pipeline) {
                    m_diag.pipeline->logDev(kPluginDevChannel,
                                            "草稿写半区端口不可得（装配缺陷）——本会话保存链路未绑定");
                }
            } else {
                DraftSessionBinding binding;
                binding.projectId = report.opened.metadata.projectId;
                binding.branchId = core::BranchId{};  // 分支锚（诚实边界——见上）
                binding.writable = report.opened.metadata.writable;
                binding.drafts = std::make_shared<app::DraftQueryPortStub>();
                binding.store = std::move(draftStore);
                m_draft->bindSession(binding);
            }
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

// =====================================================================
// 草稿链装配与保存编排（UI-T17——§8；O-43 ②）
// =====================================================================

void IrdWorkbenchHostPlugin::assembleDraftChain()
{
    // 串行落盘执行器（§3.4"ui 后台落盘线程（1 条）——串行队列"的开发期
    // 宿主）：构造即启动，退出路径有界排空（connectAppQuitDrain/析构）。
    m_diskExecutor = std::make_unique<app::SerialTaskExecutor>();

    ui::DraftControllerDeps deps;
    // 磁盘段投递（§8.2 数据流"转投 ui 后台落盘线程"）——串行执行器承接。
    deps.postToDiskThread = [this](std::function<void()> task) {
        m_diskExecutor->post(std::move(task));
    };
    // 完成回执 Marshal 回 UI 线程（§3.4 M-1）：以 Dock 体为上下文对象——
    // 其销毁后排队的回执自动作废（Qt 上下文语义），不悬挂。
    deps.postToUiThread = [this](std::function<void()> task) {
        if (m_dockBody.data() != nullptr) {
            QMetaObject::invokeMethod(m_dockBody.data(), std::move(task),
                                      Qt::QueuedConnection);
        }
    };
    // anyDirty 翻转→会话脏生产者接线（§10.5/UI-T12——标题 `*` 判定位的
    // 会话半区数据源；UI-T11 登记的"生产者接线随 UI-T12 落地"承诺）。
    deps.onSessionDirtyChanged = [this](bool dirty) {
        if (m_controller) {
            m_controller->reportSessionDirty(dirty);
        }
    };
    deps.diagSink = m_diag.catalog;
    deps.diagFactory = m_diag.factory;
    deps.devLog = m_diag.pipeline;
    m_draft = createDraftController(std::move(deps));
    if (m_diag.pipeline) {
        m_diag.pipeline->logDev(kPluginDevChannel,
                                "草稿控制器就绪（保存链路真实；恢复/autosave/分支锚随完整草稿链路任务接续）");
    }
}

bool IrdWorkbenchHostPlugin::saveDraftsNow()
{
    // 未绑定会话＝无可保存对象（返回 false——调用方按 SaveFailed/保存反馈
    // 处置，不虚构"已保存"；只读会话 saveAll 按契约 fail-fast，门控由
    // 命令可用性快照承担——draft.save readOnlyAllowed=false）。
    if (!m_draft || !m_draft->hasSession()) {
        return false;
    }
    // 全量保存挂接的脏模块（§8.2/§8.4——保存/应用分离红线：零修订；当前
    // 无挂接模块＝零脏模块＝平凡成功，属诚实形态而非能力伪造）。
    const SaveOutcome outcome = m_draft->saveAll(SaveTrigger::Manual);
    QStatusBar* statusBar = m_hostStatusBar;  // 状态投影面＝宿主状态栏（UI-T18）
    if (outcome.failedCount == 0) {
        if (statusBar != nullptr) {
            statusBar->showMessage(QString::fromUtf8("草稿已保存（%1 个模块）")
                                       .arg(static_cast<int>(outcome.savedCount)),
                                   4000);
        }
        return true;
    }
    // 失败保留脏标记（§8.2 失败行）——首失败模块 token 入状态行（开发期
    // 反馈面；用户文案随草稿链路完整任务细化）。
    const QString failedModule =
        outcome.failedModules.empty()
            ? QString()
            : QString::fromStdString(outcome.failedModules.front());
    if (statusBar != nullptr) {
        statusBar->showMessage(QString::fromUtf8("草稿保存失败（%1 个模块；首失败：%2）")
                                   .arg(static_cast<int>(outcome.failedCount))
                                   .arg(failedModule),
                               8000);
    }
    return false;
}

CommandOutcome IrdWorkbenchHostPlugin::orchestrateSaveProject(
    const std::vector<CommandParameter>& params)
{
    (void)params;  // 无参命令（§7.1 最小集——空 schema）
    CommandOutcome out;
    // 前置守卫（可用性快照已禁用的兜底面）：无会话＝未发生保存请求。
    if (!m_controller || !m_controller->hasOpenSession()) {
        return out;  // accepted=false——命令未派发
    }
    out.accepted = true;
    saveDraftsNow();  // 结局反馈经状态行（保存/应用分离——失败不清脏）
    return out;
}

// =====================================================================
// 关闭编排（UI-T17——workbench.closeProject 覆写面；§5.4/§5.6）
// =====================================================================

CommandOutcome IrdWorkbenchHostPlugin::orchestrateCloseProject(
    const std::vector<CommandParameter>& params)
{
    (void)params;  // 无参命令
    CommandOutcome out;
    if (!m_controller || !m_controller->hasOpenSession()) {
        return out;  // 无项目态没有"关闭项目"语义（门控兜底）
    }
    out.accepted = true;
    try {
        // S1 首步（§5.4）：装配对话框数据（不改状态——取消可回原状态）。
        const CloseDialogData data = m_controller->beginClose(UiCloseIntent::CloseProject);
        // 呈现（装配层对话框）＋决议回交（机制归控制器——§5 注释分工）。
        const std::optional<CloseDecision> decision = presentCloseDialog(data);
        if (!decision.has_value()) {
            // [取消]→原状态（无处置执行——会话原状）。
            (void)m_controller->resolveCloseDialog(CloseDecision{});  // confirmed=false＝取消
            return out;
        }
        const CloseDialogResolution resolution = m_controller->resolveCloseDialog(*decision);
        switch (resolution.status) {
        case CloseDialogResolution::Status::Confirmed:
            // 处置执行完毕——进入 Draining（或同步直达 Closed）：启动防线
            // 轮询（§5.6 四级防线的 UI 线程驱动点）。
            startDrainWatch();
            break;
        case CloseDialogResolution::Status::SaveFailed:
            // 保存失败＝关闭中止（不虚构"已保存"——§5.4 失败侧保守出口）。
            QMessageBox::warning(m_dockBody.data(), QString::fromUtf8("关闭已中止"),
                                 QString::fromUtf8("草稿保存失败，项目保持打开。"));
            break;
        case CloseDialogResolution::Status::Cancelled:
        case CloseDialogResolution::Status::CandidateRejected:
            break;  // 非切换流不可达（防御——状态机原状，无额外动作）
        }
    } catch (const std::logic_error& error) {
        // 调用次序违约（重复 beginClose 等）＝编排缺陷：留痕＋就地提示，
        // 不吞错不崩溃（宿主进程内 fail-fast 的可观测形态）。
        reportLine(std::string("关闭编排状态违约：") + error.what());
        if (m_diag.pipeline) {
            m_diag.pipeline->logDev(kPluginDevChannel,
                                    std::string("关闭编排状态违约：") + error.what());
        }
        QMessageBox::warning(m_dockBody.data(), QString::fromUtf8("无法关闭项目"),
                             QString::fromUtf8("当前状态不接受关闭请求（状态机违约，已留痕）。"));
    }
    return out;
}

void IrdWorkbenchHostPlugin::openRecentProject(const std::string& canonicalPath)
{
    if (!m_controller) {
        return;
    }
    // 无会话：标准打开协议（与 project.open 同一路径）。
    if (!m_controller->hasOpenSession()) {
        openViaSessionController(canonicalizePathOrKeep(canonicalPath));
        return;
    }
    // 有会话：§5.4 S2 切换流（A 的统一确认对话框→决议确认后候选验证；
    // "候选验证成功才切"——失败 A 会话不变，PM-03）。
    try {
        const CloseDialogData data = m_controller->beginSwitch(canonicalPath, UiOpenMode::Writable);
        const std::optional<CloseDecision> decision = presentCloseDialog(data);
        if (!decision.has_value()) {
            (void)m_controller->resolveCloseDialog(CloseDecision{});  // 取消
            return;
        }
        const CloseDialogResolution resolution = m_controller->resolveCloseDialog(*decision);
        switch (resolution.status) {
        case CloseDialogResolution::Status::Confirmed:
            // A 转入 Draining 背景持有点＋B 已绑定（INV-SES-2/3）——A 排空
            // 归防线轮询观测；B 记入最近项目。
            if (m_content) {
                m_content->noteRecentProject(canonicalPath);
            }
            startDrainWatch();
            break;
        case CloseDialogResolution::Status::SaveFailed:
            QMessageBox::warning(m_dockBody.data(), QString::fromUtf8("切换已中止"),
                                 QString::fromUtf8("草稿保存失败，当前项目保持打开。"));
            break;
        case CloseDialogResolution::Status::CandidateRejected:
            QMessageBox::warning(m_dockBody.data(), QString::fromUtf8("无法切换项目"),
                                 QString::fromUtf8("候选项目验证失败，当前项目保持打开。"));
            break;
        case CloseDialogResolution::Status::Cancelled:
            break;
        }
    } catch (const std::logic_error& error) {
        reportLine(std::string("切换编排状态违约：") + error.what());
        QMessageBox::warning(m_dockBody.data(), QString::fromUtf8("无法切换项目"),
                             QString::fromUtf8("当前状态不接受切换请求（状态机违约，已留痕）。"));
    }
}

std::optional<CloseDecision> IrdWorkbenchHostPlugin::presentCloseDialog(
    const CloseDialogData& data)
{
    // §5.4 统一确认对话框的装配层呈现面：机制（数据装配/决议解析/状态迁移
    // /Draining 防线）全在控制器——本对话框只渲染 CloseDialogData 并把
    // 用户决议交回 resolveCloseDialog（草稿三选×任务二选的两轴呈现）。
    QDialog dialog(m_dockBody.data());
    dialog.setWindowTitle(QString::fromUtf8("关闭项目"));
    auto* layout = new QVBoxLayout(&dialog);

    // 标题区（UX-02 工程用语——显示名来自权威元数据投影）。
    layout->addWidget(new QLabel(QString::fromUtf8("项目「%1」即将关闭。")
                                     .arg(QString::fromStdString(data.projectDisplayName)),
                                 &dialog));

    // 草稿处置轴（§5.4 草稿区）：无行集且无会话脏＝呈现"无未应用修改"
    // （零虚构——不渲染不存在的选项）；可写会话才可选"保存"（§5.5）。
    QRadioButton* saveDrafts = nullptr;
    QRadioButton* discardDrafts = nullptr;
    QLabel* draftSummary = new QLabel(
        QString::fromUtf8("未应用的草稿修改：%1 项%2")
            .arg(static_cast<int>(data.draftRows.size()))
            .arg(data.sessionDirty ? QString::fromUtf8("（含未落盘的会话修改）") : QString()),
        &dialog);
    layout->addWidget(draftSummary);
    if (data.draftRows.empty() && !data.sessionDirty) {
        layout->addWidget(new QLabel(QString::fromUtf8("无未应用修改。"), &dialog));
    } else {
        saveDrafts = new QRadioButton(QString::fromUtf8("保存草稿并关闭"), &dialog);
        saveDrafts->setEnabled(data.saveDraftsAvailable);
        discardDrafts = new QRadioButton(QString::fromUtf8("放弃未应用修改并关闭"), &dialog);
        // 缺省决议保守化：可保存时缺省保存（数据安全侧）；不可保存时仅有
        // "放弃"可选（只读会话——磁盘草稿保留，会话脏数据丢弃）。
        (data.saveDraftsAvailable ? saveDrafts : discardDrafts)->setChecked(true);
        layout->addWidget(saveDrafts);
        layout->addWidget(discardDrafts);
    }

    // 任务处置轴（§5.4 任务区）：无在途任务＝呈现"无后台任务"（等待轴
    // 无呈现对象——noActiveTasks 时等待直接进入 Draining）。
    QRadioButton* waitTasks = nullptr;
    QRadioButton* cancelTasks = nullptr;
    if (!data.noActiveTasks) {
        layout->addWidget(new QLabel(
            QString::fromUtf8("仍有 %1 个后台任务未完成。")
                .arg(static_cast<int>(data.taskRows.size())),
            &dialog));
        waitTasks = new QRadioButton(QString::fromUtf8("等待后台任务结束后关闭"), &dialog);
        waitTasks->setChecked(true);
        cancelTasks = new QRadioButton(QString::fromUtf8("协作取消后台任务并关闭"), &dialog);
        layout->addWidget(waitTasks);
        layout->addWidget(cancelTasks);
    } else {
        layout->addWidget(new QLabel(QString::fromUtf8("无后台任务。"), &dialog));
    }

    // 按钮区（取消＝回原状态；确认＝执行两轴决议）。
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QString::fromUtf8("继续"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("取消"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;  // [取消]——会话原状不变
    }
    CloseDecision decision;
    decision.confirmed = true;
    decision.draft = (saveDrafts != nullptr && saveDrafts->isChecked())
                         ? DraftDisposition::Save
                         : DraftDisposition::Discard;
    decision.task = (cancelTasks != nullptr && cancelTasks->isChecked())
                        ? TaskDisposition::CooperativeCancel
                        : TaskDisposition::Wait;
    return decision;
}

// =====================================================================
// Draining 防线轮询驱动（§5.6 四级防线——UI 线程 QTimer 周期）
// =====================================================================

void IrdWorkbenchHostPlugin::startDrainWatch()
{
    if (m_drainTimer == nullptr) {
        m_drainTimer = new QTimer(this);
        m_drainTimer->setInterval(200);  // §9.4 轮询周期同源（UI 线程零阻塞）
        connect(m_drainTimer, &QTimer::timeout, this, [this] { pollDrainOnce(); });
    }
    QStatusBar* statusBar = m_hostStatusBar;  // 状态投影面＝宿主状态栏（UI-T18）
    if (statusBar != nullptr) {
        statusBar->showMessage(QString::fromUtf8("正在关闭项目……（等待后台任务与草稿落盘收口）"));
    }
    m_drainTimer->start();
}

void IrdWorkbenchHostPlugin::pollDrainOnce()
{
    if (!m_controller) {
        if (m_drainTimer != nullptr) {
            m_drainTimer->stop();
        }
        return;
    }
    DrainPollReport report;
    try {
        report = m_controller->pollDrain();
    } catch (const std::logic_error&) {
        // "不在 Draining 且无后台持有点"＝关闭已收口（同步完成/切换 B 绑定
        // 后台持有点已排空）——轮询对象消失，停止驱动（不是错误）。
        if (m_drainTimer != nullptr) {
            m_drainTimer->stop();
        }
        return;
    }
    QStatusBar* statusBar = m_hostStatusBar;  // 状态投影面＝宿主状态栏（UI-T18）
    switch (report.status) {
    case DrainPollReport::Status::Draining:
        break;  // 保持等待（防线 1 的有界反馈已在状态行）
    case DrainPollReport::Status::ForceConfirmDue: {
        // 防线 3（T_force 到点）：强制结束确认——呈现一次，决议交回控制器
        // （确认＝强杀序列＋abandon 兜底；拒绝＝继续等待，T_force2 仍兜底）。
        if (m_drainTimer != nullptr) {
            m_drainTimer->stop();
        }
        const ForceCloseDialogData forceData = m_controller->forceCloseDialogData();
        const QMessageBox::StandardButton choice = QMessageBox::question(
            m_dockBody.data(), QString::fromUtf8("强制结束后台任务？"),
            QString::fromUtf8("项目「%1」关闭等待已超过 %2，仍有 %3 个后台任务未结束。\n\n"
                              "强制结束＝任务记为失败（最近检查点保留可续）。是否强制结束并关闭？")
                .arg(QString::fromStdString(forceData.projectDisplayName),
                     QString::fromStdString(forceData.waitedText),
                     QString::number(static_cast<int>(forceData.activeTaskCount))),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        m_controller->resolveForceCloseDialog(choice == QMessageBox::Yes);
        if (m_drainTimer != nullptr) {
            m_drainTimer->start();  // 确认与否都回到轮询收敛（T_force2 兜底）
        }
        break;
    }
    case DrainPollReport::Status::GivenUp:
        // 防线 4（T_force2 到点）：放弃等待并完成关闭（绝不无限等待——
        // INV-SES-4；数据损失限于未归档结果，检查点保留）。
        if (m_drainTimer != nullptr) {
            m_drainTimer->stop();
        }
        if (statusBar != nullptr) {
            statusBar->showMessage(QString::fromUtf8("关闭等待超时，已强制完成关闭（未归档结果不保留）。"), 8000);
        }
        break;
    case DrainPollReport::Status::ClosedNow:
        if (m_drainTimer != nullptr) {
            m_drainTimer->stop();
        }
        if (statusBar != nullptr) {
            statusBar->showMessage(QString::fromUtf8("项目已关闭。"), 4000);
        }
        break;
    }
}

}  // namespace ui
}  // namespace ird
}  // namespace sdurws
