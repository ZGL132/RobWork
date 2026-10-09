/**
 * @file   SelPanelModule.hpp
 * @brief  selection 插件面板模块（装配缝持有者）——服务缝与会话态的
 *         归属容器＋主面板工厂落点（dynamics 先例 DynPanelModule 的
 *         收缩形态；WP-19-T10）。
 *
 * 设计依据：
 *   - units/selection.md §3.1（插件组成——面板数据源缝注入纪律）、
 *     §3.4（插件零计算红线——模块零业务语义，纯装配缝容器）；
 *   - 先例：dynamics/plugin/DynPanelModule.hpp（模块＝缝＋会话态＋
 *     面板工厂的装配面形态——WP-17-T09；selection 同零 ui 接口继承
 *     〔P-SEL-10 缺口——宿主装配批次收口〕）；
 *   - 任务契约 tasks/foundation/WP-19-T10.json acceptance 1。
 *
 * 线程约束：仅 UI 线程访问（会话态与面板引用——卡 §3.4）。
 */

#ifndef IRD_SELECTION_PLUGIN_SELPANELMODULE_HPP
#define IRD_SELECTION_PLUGIN_SELPANELMODULE_HPP

#include "SelPanelTypes.hpp"  // SelPanelServices/SelModuleSessionState

class QWidget;  // 前置声明：createPanel 返回类型（全局域——Widgets 面）

namespace sdurws::ird::selection {

class SelCatalogPanelWidget;  // 前置声明（面板引用——完整类型在其头）

/**
 * @brief 插件面板模块（装配门面的私有协作对象——缝/会话态/面板引用
 *        三者归属；宿主只见装配门面，不触本类——R-2 同单元私有面）。
 */
class SelPanelModule {
public:
    /**
     * @brief 创建主面板并接线（每次调用新建；归调用方接管——宿主层
     *        持有；仅 UI 线程）。面板构造即做一次会话刷新。
     *
     * @return 面板 widget（SelCatalogPanelWidget）
     */
    QWidget* createPanel();

    /// @brief 会话刷新（会话事件后的面板同步——现取重投影；面板未
    ///        创建＝空操作）。
    void refreshFromSession();

    /// 服务缝（装配层 setServices 整体注入——面板创建前生效）。
    SelPanelServices services;
    /// 会话权威态（装配层注入快照绑定/事实投影的唯一载体）。
    SelModuleSessionState session;

private:
    SelCatalogPanelWidget* m_panel = nullptr;  ///< 主面板引用（非 owning——
                                               ///<   宿主持有 widget）
};

}  // namespace sdurws::ird::selection

#endif  // IRD_SELECTION_PLUGIN_SELPANELMODULE_HPP
