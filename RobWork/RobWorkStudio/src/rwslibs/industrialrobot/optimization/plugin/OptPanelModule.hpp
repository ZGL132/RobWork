/**
 * @file   OptPanelModule.hpp
 * @brief  optimization 插件面板模块（装配缝持有者）——服务缝与会话态的
 *         归属容器＋主面板工厂落点（dynamics 先例 DynPanelModule 的同款
 *         形态；WP-20-T10）。
 *
 * 设计依据：
 *   - units/optimization.md §9.1（运行控制面板的缝/会话态纪律——UI 线程
 *     约束、候选评估绝不在 UI 线程执行）；§3.1（plugin/ 目录承载界面面）
 *   - 先例：dynamics/plugin/DynPanelModule.hpp（模块＝缝＋会话态＋面板
 *     工厂的装配面形态——WP-17-T09；kinematics 同源更早先例）
 *   - 任务契约 tasks/foundation/WP-20-T10.json acceptance 1
 *
 * 线程约束：仅 UI 线程访问（会话态与面板引用——卡 §9.1）。
 */

#ifndef IRD_OPTIMIZATION_PLUGIN_OPTPANELMODULE_HPP
#define IRD_OPTIMIZATION_PLUGIN_OPTPANELMODULE_HPP

#include "OptPanelTypes.hpp"  // OptPanelServices/OptModuleSessionState

class QWidget;  // 前置声明：createPanel 返回类型（全局域——Widgets 面）

namespace sdurws::ird::optimization {

class OptimizationPanelWidget;  // 前置声明（面板引用——完整类型在其头）

/**
 * @brief 插件面板模块（装配门面的私有协作对象——缝/会话态/面板引用
 *        三者归属；宿主只见装配门面，不触本类——R-2 同单元私有面）。
 */
class OptPanelModule {
public:
    /**
     * @brief 创建主面板并接线（每次调用新建；归调用方接管——宿主层
     *        持有；仅 UI 线程）。面板构造即做一次会话刷新。
     *
     * @return 面板 widget（OptimizationPanelWidget——四页合一 Tab：
     *         变量表/约束页/运行控制〔取消/进度漏斗〕/候选表与对比）
     */
    QWidget* createPanel();

    /// @brief 会话刷新（restoreOnOpen 等会话事件后的面板同步——现取
    ///        重投影；面板未创建＝空操作）。
    void refreshFromSession();

    /// 服务缝（装配层 setServices 整体注入——面板创建前生效）。
    OptPanelServices services;
    /// 会话权威态（装配层注入快照绑定/事实投影的唯一载体）。
    OptModuleSessionState session;

private:
    OptimizationPanelWidget* m_panel =
        nullptr;  ///< 主面板引用（非 owning——宿主持有 widget）
};

}  // namespace sdurws::ird::optimization

#endif  // IRD_OPTIMIZATION_PLUGIN_OPTPANELMODULE_HPP
