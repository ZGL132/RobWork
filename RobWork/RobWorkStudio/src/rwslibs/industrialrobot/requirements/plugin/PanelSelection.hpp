/**
 * @file   PanelSelection.hpp
 * @brief  requirements 面板选中联动（零 Qt）——L-R1 数据流的会话态半区
 *         （卡 §9.8 界面逻辑表 L-R1 行）。
 *
 * 设计依据：
 *   - units/requirements.md §9.8 界面逻辑表 L-R1（"选中联动：树/表选中→
 *     SelectionModel→投影刷新；诊断定位反向跳转"；消费契约＝ui §4.2、
 *     UX-06）、§9.8 面板表第 1 行（节点锚＝ObjectId——四方关联键）
 *   - units/ui.md §4.2（SelectionModel＝ui 拥有的会话选中状态）
 *   - 需求 UX-05/UX-06；任务契约 tasks/foundation/WP-14-T08.json
 *     acceptance 1
 *
 * ★ P-REQ-8 接缝说明（modeling P-MDL-8 同款）：ui 的 SelectionModel 尚未
 *   落位（ui.md §4.2 为契约文本；磁盘核对 ui/include 无对应头）。本类承载
 *   requirements 侧会话选中的**最小会话态**（选中锚＋定位事件流），语义
 *   逐条对齐 ui.md §4.2 文本基线；ui 侧 SelectionModel 落位后，本类退化为
 *   对该模型的薄适配（构造注入替换）——增量同步登记于单元卡 §14.6
 *   （R-REQ-1）。
 *
 * 零修订纪律（L-R1 的核心约束，KIN-06/AT-04 同源）：本类的任何操作只写
 * 会话态与发出定位/高亮事件，**不触工作集可变面、不提交命令**——零修订
 * 由结构保证（本头不 include 任何命令提交面；测试以工作集字节不变＋命令
 * 回调零调用双证据断言）。
 *
 * 线程约束：仅 UI 线程访问（§3.4——选中态是会话对象的一部分；非线程
 * 安全）。确定性：事件序即调用序（无重排）。
 */

#ifndef IRD_REQUIREMENTS_PLUGIN_PANELSELECTION_HPP
#define IRD_REQUIREMENTS_PLUGIN_PANELSELECTION_HPP

#include <functional>
#include <optional>

#include <sdurws/ird/core/Identity.hpp>  // core::ObjectId（选中锚/定位目标——L-R1 关联键）

namespace sdurws::ird::requirements {

/**
 * @brief 反向定位目标（L-R1"诊断定位反向跳转"的承载值）。
 *
 * scrollToNode＋highlightObject 同源（同一 ObjectId——树与三维高亮联动，
 * 名称端口取显示名归 widget 层）。无对象主体的定位（如分组节点/集合级
 * 警告）不产生本值（nullopt 语义）。
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
 * @brief requirements 面板会话选中态（L-R1 的 requirements 侧承载——零修订
 *        纪律的结构化实现）。
 *
 * 使用方式（widget 层接线约定）：
 *   - 树点击/表选中 → select()：仅更新会话选中锚；随后 widget 以
 *     selectionChanged 触发检查器投影刷新（投影数据从工作集现取——本类
 *     不持有任何模型数据副本，"面板不缓存权威数据"）。
 *   - 校验面板逐项行/必验清单行跳转 → locate()：产出 LocateTarget（树滚
 *     动＋高亮），widget 执行滚动与高亮；本类不直接操纵视图控件（模型/
 *     视图分离——面板不互读控件）。
 *
 * 生命周期：由面板 widget 持有（构造/析构随面板——UI 线程对象）。
 * 线程约束：仅 UI 线程访问（§3.4；非线程安全）。确定性：事件即调即发。
 */
class PanelSelectionState {
public:
    PanelSelectionState() = default;

    /// 不可拷贝/不可移动（会话态对象——选中锚与事件 sink 绑定生命周期）。
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
     * @brief 树点击/表选中入口（L-R1 正向半区：写会话态，零修订）。
     *
     * @param oid [in] 选中锚（ObjectId——树节点或表行条目锚；nullopt＝
     *              清除选中，如点击空白处）
     * @return true＝选中态变化（widget 据此刷新检查器投影）；false＝重复
     *         选中同一锚（幂等——不重复触发刷新）
     */
    bool select(const std::optional<core::ObjectId>& oid)
    {
        if (m_selected == oid) { return false; }  // 幂等：重复选中不抖动检查器
        m_selected = oid;
        return true;
    }

    /**
     * @brief 反向定位入口（L-R1 反向半区：树滚动＋三维高亮目标产出）。
     *
     * @param oid [in] 定位目标（校验面板行/必验清单行的 caseId 锚）
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

}  // namespace sdurws::ird::requirements

#endif  // IRD_REQUIREMENTS_PLUGIN_PANELSELECTION_HPP
