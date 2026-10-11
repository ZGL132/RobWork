/**
 * @file   TrajPanelModule.hpp
 * @brief  trajectory 插件面板模块（装配缝持有者）——服务缝与会话态的
 *         归属容器＋主面板工厂落点（dynamics 先例 DynPanelModule 同款
 *         形态；WP-16-T12）。
 *
 * 设计依据：
 *   - units/trajectory.md §16.1（面板由装配层创建与持有——ui.md §10.9
 *     装配时序；插件面"零计算逻辑"红线）、§14.5.2（投影与会话态——
 *     面板呈现事实经会话态注入）；
 *   - 先例：dynamics/plugin/DynPanelModule.hpp（WP-17-T09 模块＝缝＋
 *     会话态＋面板工厂的装配面形态）；
 *   - 任务契约 tasks/foundation/WP-16-T12.json acceptance 1。
 *
 * 线程约束：仅 UI 线程访问（会话态与面板引用——卡 §3.4）。
 */

#ifndef IRD_TRAJECTORY_PLUGIN_TRAJPANELMODULE_HPP
#define IRD_TRAJECTORY_PLUGIN_TRAJPANELMODULE_HPP

#include "TrajPanelTypes.hpp"  // TrajPanelServices/TrajModuleSessionState

class QWidget;  // 前置声明：createPanel 返回类型（全局域——Widgets 面）

namespace sdurws::ird::trajectory {

class TrajectoryPanelWidget;  // 前置声明（面板引用——完整类型在其头）

/**
 * @brief 插件面板模块（装配门面的私有协作对象——缝/会话态/面板引用
 *        三者归属；宿主只见装配门面，不触本类——R-2 同单元私有面）。
 */
class TrajPanelModule {
public:
    /**
     * @brief 创建主面板并接线（每次调用新建；归调用方接管——宿主层
     *        持有；仅 UI 线程）。面板构造即做一次会话刷新。
     *
     * @return 面板 widget（TrajectoryPanelWidget——工作流页＋曲线视图
     *         入口合一 Tab）
     */
    QWidget* createPanel();

    /// @brief 会话刷新（会话事实注入后的面板同步——现取重投影；面板
    ///        未创建＝空操作）。
    void refreshFromSession();

    /// 服务缝（装配层 setServices 整体注入——面板创建前生效）。
    TrajPanelServices services;
    /// 会话权威态（装配层注入快照绑定/事实投影的唯一载体）。
    TrajModuleSessionState session;

private:
    TrajectoryPanelWidget* m_panel = nullptr;  ///< 主面板引用（非 owning——
                                               ///<   宿主持有 widget）
};

}  // namespace sdurws::ird::trajectory

#endif  // IRD_TRAJECTORY_PLUGIN_TRAJPANELMODULE_HPP
