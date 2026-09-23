/**
 * @file   UiPlugin.hpp
 * @brief  工作台宿主插件（sdurws_ird_ui_plugin）——框架 RobWorkStudio 的
 *         集成入口＋开发期动态加载验证通道（rws::RobWorkStudioPlugin 派生
 *         薄插件，零计算逻辑；任务 UI-T16，O-38 裁决承接）。
 *
 * 设计依据：
 *   - units/ui.md §13 UI-T16 行＋UI-T16 立项登记注（v1.9）：①形态＝
 *     rws::RobWorkStudioPlugin 派生薄插件（框架基类本体即 QDockWidget 子类，
 *     虚函数 initialize/open/close/setupMenu/setupToolBar＋getRobWorkStudio/
 *     getState 注入面；IID "dk.sdu.mip.Robwork.RobWorkStudioPlugin/0.1"），
 *     插件零计算逻辑；②壳拆分＝harness 与插件共用同一内容装配面
 *     （WorkbenchContent.hpp——五区交互/布局记忆/命令门控语义不变）；
 *     **禁止把顶层 QMainWindow 嵌套进宿主 Dock**（双菜单/双状态栏/双 Dock
 *     管理反模式——本插件把内容装配面安放进自身 QDockWidget 体栅格，不建
 *     任何顶层窗口）；③三维视图＝共存最小接入（宿主中央视图不丢失）；
 *     ④通道边界＝动态加载仅限开发期验证（SA-01 静态白名单不变，正式
 *     装配归 WP-24-T03——落位时本插件按同一契约转正式装配或被替换）；
 *   - 框架 rws::RobWorkStudioPlugin 机制（src/rws/RobWorkStudioPlugin.hpp，
 *     零框架修改——SA-02）：宿主经 Plugins→Load plugin 动态装载本 DLL，
 *     addPlugin 流程回调 setRobWorkStudio→setupMenu→initialize，并把插件
 *     本体 addDockWidget 进主窗口（插件即 Dock 面板）；
 *   - O-31 装配层特权（DTB §4.5 登记模式）：打开协议端口适配复用 harness
 *     PortAdapters 形态（app/PortAdapters.*——ui 自有端口→project 对端）。
 *
 * 背景说明（插件里有什么、没有什么）：
 *   有——装配序列（诊断栈/端口适配/内容装配面/会话控制器）、会话入口编排
 *   （新建/打开项目的文件对话框→UiSessionController 打开协议，§11.5"触发
 *   时机编排归装配层"）、宿主框架菜单动作、让位观测日志；没有什么——
 *   任何业务计算、任何领域对象持有（插件零计算逻辑，登记 ui.md §13）。
 *
 * 线程模型：全部回调（initialize/open/close/setupMenu 与命令处理器）由
 *   宿主在 UI 线程调用（框架 addPlugin 流程与 Qt 信号同步触发）；ui 内容
 *   装配面的线程纪律（§3.4 M-1）因此自然成立。
 */

#ifndef SDURWS_IRD_UI_PLUGIN_UIPLUGIN_HPP
#define SDURWS_IRD_UI_PLUGIN_UIPLUGIN_HPP

#include <QPointer>
#include <QString>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <rws/RobWorkStudioPlugin.hpp>   // 框架插件基类（QDockWidget 派生＋宿主注入面）

#include <sdurws/ird/project/ProjectStore.hpp>    // project::OpenStoreResult（创建协议产物）
#include <sdurws/ird/ui/IWorkbenchShell.hpp>      // ui::ShellWiring（注入包）
#include <sdurws/ird/ui/UiSessionController.hpp>  // ui::UiSessionController（§5 会话状态机）
#include <sdurws/ird/ui/WorkbenchContent.hpp>     // ui::IWorkbenchContent（内容装配面）

#include "app/DiagnosticsAssembly.hpp"           // 诊断栈装配（与 harness 共用序列）
#include "app/PortAdapters.hpp"                  // 端口适配器集（O-31 装配层特权边——复用形态）

class QAction;    // 框架菜单动作（setupMenu 注入面——完整定义在 Qt 头）
class QMenu;      // 宿主 Plugins 菜单（setupMenu 回调参数）
class QWidget;    // Dock 体控件（前置声明）

namespace sdurws {
namespace ird {
namespace ui {

/**
 * @brief 工作台宿主插件（ui 单元唯一 _plugin 目标的根类型）。
 *
 * 生命周期：QPluginLoader 在宿主装载时创建本实例（宿主持有到进程退出——
 *   框架不删除已装载插件实例）；框架 addPlugin 流程依次回调 setupMenu/
 *   setupToolBar/initialize，插件在 initialize 完成全部装配（一次守卫）。
 *   Qt 父子树纪律：内容 Widget/菜单动作全部以插件 Dock 体或框架菜单为父
 *   对象——随宿主窗口销毁即回收。
 *
 * 让位形态（O-38 裁决③）：插件不承载三维视图本体——宿主中央区
 *   RWStudioView3D 与本 Dock 共存；open(workcell) 回调仅做只读观测留痕
 *   （ getView()/getWorkCellScene() 注入面的最小接入——记录宿主三维能力
 *   在位，零视图操作），完整三维交互归 WP-10-T05 阶段 B。
 */
class IrdWorkbenchHostPlugin final : public rws::RobWorkStudioPlugin {
    Q_OBJECT
    Q_INTERFACES (rws::RobWorkStudioPlugin)
    Q_PLUGIN_METADATA (IID "dk.sdu.mip.Robwork.RobWorkStudioPlugin/0.1" FILE "plugin.json")

public:
    /// @brief 构造（宿主 QPluginLoader 创建——此处不触碰 studio，装配在
    ///        initialize：框架此时尚未注入 RobWorkStudio 句柄）。
    IrdWorkbenchHostPlugin();

    /// @brief 析构：有界拆卸内容装配面（幂等——正常退出已随 aboutToQuit 收口）。
    ~IrdWorkbenchHostPlugin() override;

    // ---- 框架插件回调（rws::RobWorkStudioPlugin 虚函数）----

    /// @brief 宿主注入完成后的一次性装配（框架 addPlugin 流程调用）：
    ///        Dock 体栅格＋内容装配面＋会话控制器。
    void initialize() override;

    /// @brief 宿主 Plugins 菜单注入（框架 addPlugin 流程在 initialize 前
    ///        回调）：基类显示/隐藏开关先行，本插件追加命令入口动作。
    void setupMenu (QMenu* menu) override;

    /// @brief 宿主装载工作单元回调：共存观测留痕（宿主中央视图在位——
    ///        本插件让位，零视图操作——O-38 裁决③最小接入）。
    void open (rw::models::WorkCell* workcell) override;

    /// @brief 宿主关闭工作单元回调：观测留痕（零操作本体）。
    void close() override;

private:
    // ---- 装配段（initialize 内部步骤——各函数单一职责）----
    bool buildDockBody();              ///< Dock 体栅格＋内容装配面 build/activate
    void refreshHostMenuActions();     ///< 菜单动作使能同步（内容装配层命令状态观察回调）
    void connectAppQuitDrain();        ///< 宿主退出时的有界落盘收口挂接（aboutToQuit）

    // ---- 会话入口编排（§11.5——触发时机编排归装配层；经命令处理器覆写
    //      注入内容装配面，五处壳入口同路由）----
    /// 打开编排：目录选择对话框→打开五步协议（PM-07 降级只读是成功形态）。
    CommandOutcome orchestrateOpenProject(const std::vector<CommandParameter>& params);
    /// 新建编排：目录＋显示名采集→创建协议（零半成品）→经标准打开协议进入。
    CommandOutcome orchestrateNewProject(const std::vector<CommandParameter>& params);
    /// 打开协议公共段（新建流在此汇合——同一协议路径，锁干净）。
    /// @return 打开是否成功（失败已就地呈现错误，不抛）。
    bool openViaSessionController(const std::string& canonicalPath);

    // ---- 装配产物（所有权：诊断栈/适配器经 shared_ptr 供壳与控制器共享
    //      引用；控制器/内容装配面为本插件独占成员——析构序＝声明逆序，
    //      控制器与内容装配面先于端口适配器消亡）----
    app::DiagnosticsStack m_diag;                                 ///< 诊断栈（与 harness 同装配序列）
    std::shared_ptr<app::UnloadedPolicySource> m_policySource;    ///< 策略未装载占位（C-10）
    std::shared_ptr<app::NullUiNameResolver> m_nameResolver;      ///< 名称解析占位（C-11）
    std::shared_ptr<app::HarnessAboutSource> m_aboutSource;       ///< 关于框数据占位（§11.4）
    std::shared_ptr<app::ProjectDiagnosticsBridge> m_bridge;      ///< project 诊断桥（P-PR-6）
    std::shared_ptr<app::StoreFactoryPortAdapter> m_storeFactory; ///< 打开五步协议适配器（C-3）
    /// §5 会话状态机（打开编排的推进面）——延迟构造：依赖的适配器在
    /// initialize 装配（控制器构造时一次性注入依赖包，不可后换；类型不可
    /// 拷贝/移动，经 unique_ptr 在装配点就地构造）。
    std::unique_ptr<UiSessionController> m_controller;
    std::unique_ptr<IWorkbenchContent> m_content;             ///< 内容装配面（与 harness 同源）
    QPointer<QWidget> m_dockBody;                             ///< Dock 体控件（Qt 父子树托管）
    std::vector<std::pair<QAction*, std::string>> m_hostMenuCommandIds; ///< 框架菜单动作→命令 id（使能同步）
    std::vector<std::pair<WorkbenchRegion, QAction*>> m_hostRegionToggles; ///< 框架菜单五区开关（勾选态回写）
    bool m_assembled = false;                                 ///< initialize 已完成（一次守卫）
};

}  // namespace ui
}  // namespace ird
}  // namespace sdurws

#endif  // SDURWS_IRD_UI_PLUGIN_UIPLUGIN_HPP
