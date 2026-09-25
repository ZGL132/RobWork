/**
 * @file   PanelSelection.hpp
 * @brief  建模面板选中联动（零 Qt）——L-1 数据流的会话态半区（卡 §9.7.2）。
 *
 * 设计依据：
 *   - units/modeling.md §9.7.2 L-1（树点击/View3D ray-cast 拾取→ui
 *     SelectionModel（写会话态，零修订）→属性区只读投影刷新；反向"定位"：
 *     属性区/诊断条跳转→树滚动＋三维高亮目标）
 *   - units/ui.md §4.2（SelectionModel＝ui 拥有的会话选中状态）
 *   - 需求 MDL-07、KIN-06（会话操作零修订——AT-04）
 *   - 任务契约 tasks/foundation/WP-13-T15.json acceptance 3
 *
 * ★ P-MDL-8 接缝说明：ui 的 SelectionModel 尚未落位（ui.md §4.2 为契约
 * 文本；磁盘核对 ui/include 无对应头）。本类承载建模侧会话选中的**最小
 * 会话态**（选中锚＋定位事件流），语义逐条对齐 ui.md §4.2 文本基线；ui
 * 侧 SelectionModel 落位后，本类退化为对该模型的薄适配（构造注入替换）
 * ——增量同步登记于单元卡 §14.6（R-MDL-1）。
 *
 * 零修订纪律（L-1 的核心约束，KIN-06/AT-04）：本类的任何操作只写会话态
 * 与发出定位/高亮事件，**不触工作集可变面、不提交命令**——零修订由结构
 * 保证（本头不 include 任何命令提交面；测试以工作集字节不变＋命令回调
 * 零调用双证据断言）。
 *
 * 线程约束：仅 UI 线程访问（§3.4——选中态是会话对象的一部分；非线程
 * 安全）。确定性：事件序即调用序（无重排）。
 */

#ifndef IRD_MODELING_PLUGIN_PANELSELECTION_HPP
#define IRD_MODELING_PLUGIN_PANELSELECTION_HPP

#include <functional>
#include <optional>

#include <sdurws/ird/core/Identity.hpp>  // core::ObjectId（选中锚/定位目标——L-1 关联键）

namespace sdurws::ird::modeling {

/**
 * @brief 反向定位目标（L-1"反向定位：属性区/诊断条跳转→树滚动＋三维
 *        高亮目标"的承载值）。
 *
 * scrollToNode＋highlightObject 同源（同一 ObjectId——树与三维高亮联动，
 * 名称端口取显示名归 widget 层）。无对象主体的定位（如分组节点）不产生
 * 本值（nullopt 语义）。
 */
struct LocateTarget {
    core::ObjectId scrollToNode{};    ///< 树滚动目标（节点锚＝ObjectId）
    core::ObjectId highlightObject{}; ///< 三维高亮目标（View3D 高亮入参——同一锚）

    bool operator==(const LocateTarget& o) const noexcept
    {
        return scrollToNode == o.scrollToNode && highlightObject == o.highlightObject;
    }
    bool operator!=(const LocateTarget& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 建模面板会话选中态（L-1 的建模侧承载——零修订纪律的结构化实现）。
 *
 * 使用方式（widget 层接线约定）：
 *   - 树点击/View3D 拾取 → select()：仅更新会话选中锚；随后 widget 以
 *     selectionChanged 触发属性区只读投影刷新（投影数据从工作集现取——
 *     本类不持有任何模型数据副本，ACC5"防第二真值"）。
 *   - 属性区/诊断条跳转 → locate()：产出 LocateTarget（树滚动＋高亮），
 *     widget 执行滚动与高亮；本类不直接操纵视图控件（模型/视图分离）。
 *
 * 生命周期：由面板 widget 持有（构造/析构随面板——UI 线程对象）。
 * 线程约束：仅 UI 线程访问（§3.4；非线程安全）。确定性：事件即调即发。
 */
class PanelSelectionState {
public:
    PanelSelectionState() = default;

    /// 不可拷贝/不可移动（会话态对象——选中锚与事件sink 绑定生命周期）。
    PanelSelectionState(const PanelSelectionState&) = delete;
    PanelSelectionState& operator=(const PanelSelectionState&) = delete;

    /**
     * @brief 注册定位事件出口（widget 层的树滚动/高亮执行器——非 owning，
     *        调用方保证存活期覆盖本对象）。
     *
     * @param sink [in] 定位执行回调（UI 线程调用；可为空＝静默丢弃定位——
     *              仅用于"无三维视图"装配形态，KIN-06 会话语义不变）
     */
    void setLocateSink(std::function<void(const LocateTarget&)> sink)
    {
        m_locateSink = std::move(sink);
    }

    /**
     * @brief 树点击/View3D 拾取入口（L-1 正向半区：写会话态，零修订）。
     *
     * @param oid [in] 选中锚（ObjectId——树节点或拾取命中对象；nullopt＝
     *              清除选中，如点击空白处）
     * @return true＝选中态变化（widget 据此刷新属性区投影）；false＝重复
     *         选中同一锚（幂等——不重复触发刷新）
     */
    bool select(const std::optional<core::ObjectId>& oid)
    {
        if (m_selected == oid) { return false; }  // 幂等：重复选中不抖动属性区
        m_selected = oid;
        return true;
    }

    /**
     * @brief 反向定位入口（L-1 反向半区：树滚动＋三维高亮目标产出）。
     *
     * @param oid [in] 定位目标（属性区行/诊断条跳转的 subject 锚）
     * @return 有目标＝LocateTarget（并已投递 locateSink）；无效目标＝
     *         nullopt（闭包外身份——不定位不高亮，呈现保持原状）
     */
    std::optional<LocateTarget> locate(const core::ObjectId& oid)
    {
        if (!oid.isValid()) { return std::nullopt; }  // 全零保留值无节点——不伪造定位
        LocateTarget t;
        t.scrollToNode = oid;
        t.highlightObject = oid;
        if (m_locateSink) {
            m_locateSink(t);  // 事件即调即发（UI 线程内同步——无队列无重排）
        }
        return t;
    }

    /// 当前选中锚（会话态只读投影；nullopt＝无选中）。
    const std::optional<core::ObjectId>& selected() const noexcept { return m_selected; }

private:
    std::optional<core::ObjectId> m_selected;  ///< 会话选中锚（仅 UI 线程可变——§3.4）
    std::function<void(const LocateTarget&)> m_locateSink;  ///< 定位出口（非 owning——widget 层注入）
};

}  // namespace sdurws::ird::modeling

#endif  // IRD_MODELING_PLUGIN_PANELSELECTION_HPP
