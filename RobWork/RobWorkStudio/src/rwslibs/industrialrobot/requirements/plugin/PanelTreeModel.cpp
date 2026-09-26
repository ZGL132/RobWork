/**
 * @file   PanelTreeModel.cpp
 * @brief  需求对象树投影实现——"值→视图行"的形状搬运与确定性文本化。
 *
 * 设计依据：units/requirements.md §9.8（面板表第 1 行——树结构与节点锚）、
 * §4.1/§4.2（五对象层级）、ui.md §6.6（UX-02）；契约 WP-14-T08
 * acceptance 1。实现纪律同 modeling/PanelModel.cpp 通则：零业务判定、
 * 无静态可变状态、确定性文本化（locale 无关）。
 */

#include "PanelTreeModel.hpp"

#include <sdurws/ird/ui/UiText.hpp>  // ui::ensureNoInternalIdentity——UX-02 哈希形态守卫（唯一出口复用，不私写第二实现）

namespace sdurws::ird::requirements {
namespace {

// 拼接"标签（N）"形态的分组标题（N＝组内条目数——折叠标题的计数面）。
std::string groupLabel(const char* base, std::size_t count)
{
    // std::to_string：整数转串无 locale 分组符差异（NFR-COR-02）。
    return std::string(base) + "（" + std::to_string(count) + "）";
}

}  // namespace

std::vector<RequirementNode> buildRequirementTree(const RequirementWorkingSet& ws)
{
    std::vector<RequirementNode> nodes;
    // 容量预算：1 根＋4 分组＋四集合条目总数（一次分配——纯投影无增量语义）。
    nodes.reserve(5 + ws.points.entries.size() + ws.regions.entries.size()
                  + ws.conditions.entries.size() + ws.plans.entries.size());

    // ---- 根节点（req-set——修订闭包锚；显示根 name）----
    RequirementNode root;
    root.kind = RequirementNodeKind::RequirementRoot;
    root.localName = ws.root.name;
    root.displayLabel = ws.root.name.empty() ? "需求集" : ws.root.name;
    ui::ensureNoInternalIdentity(root.displayLabel);  // UX-02：哈希形态名 fail-fast
    nodes.push_back(std::move(root));

    // ---- 四分组（卡面固定序：工位→区域→工况→计划；组内条目按工作集序
    //      ＝ObjectId 字典序，I-REQ-1——投影不重排）----
    auto pushGroup = [&nodes](RequirementNodeKind groupKind, const char* base,
                              std::size_t count) {
        RequirementNode g;
        g.kind = groupKind;
        g.displayLabel = groupLabel(base, count);  // 分组标题带计数——折叠呈现面
        g.entryCount = count;
        nodes.push_back(std::move(g));
    };

    // 工位分组＋条目（TaskPoint）。
    pushGroup(RequirementNodeKind::PointsGroup, "工位", ws.points.entries.size());
    for (const TaskPoint& p : ws.points.entries) {
        RequirementNode n;
        n.kind = RequirementNodeKind::Point;
        n.objectId = p.objectId;          // 节点锚＝条目 ObjectId（L-R1 关联键）
        n.localName = p.name;             // D-REQ-6：name 是工程用语语义字段
        n.displayLabel = p.name;
        ui::ensureNoInternalIdentity(n.displayLabel);
        nodes.push_back(std::move(n));
    }

    // 区域分组＋条目（WorkRegion）。
    pushGroup(RequirementNodeKind::RegionsGroup, "区域", ws.regions.entries.size());
    for (const WorkRegion& r : ws.regions.entries) {
        RequirementNode n;
        n.kind = RequirementNodeKind::Region;
        n.objectId = r.objectId;
        n.localName = r.name;
        n.displayLabel = r.name;
        ui::ensureNoInternalIdentity(n.displayLabel);
        nodes.push_back(std::move(n));
    }

    // 工况分组＋条目（OperatingCondition）。
    pushGroup(RequirementNodeKind::ConditionsGroup, "工况", ws.conditions.entries.size());
    for (const OperatingCondition& c : ws.conditions.entries) {
        RequirementNode n;
        n.kind = RequirementNodeKind::Condition;
        n.objectId = c.objectId;
        n.localName = c.name;
        n.displayLabel = c.name;
        ui::ensureNoInternalIdentity(n.displayLabel);
        nodes.push_back(std::move(n));
    }

    // 计划分组＋条目（SamplingPlan——无 name 字段，§5.2：标签用"计划→
    // 目标区域锚短名"形态；显示面＝objectId 规范文本会引入内部标识，
    // 与 UX-02 冲突，故用固定中文标签＋序号——序号＝工作集序下标，确定性）。
    pushGroup(RequirementNodeKind::PlansGroup, "计划", ws.plans.entries.size());
    for (std::size_t i = 0; i < ws.plans.entries.size(); ++i) {
        const SamplingPlan& pl = ws.plans.entries[i];
        RequirementNode n;
        n.kind = RequirementNodeKind::Plan;
        n.objectId = pl.objectId;
        // SamplingPlan 无语义名（§5.2 字段表只有 regionRef＋采样参数）——
        // 标签＝"计划 i→区域锚"，i 为工作集序（1 起呈现）；锚规范文本不进
        // 显示面（UX-02）。
        n.localName = "plan-" + std::to_string(i + 1);
        n.displayLabel = "计划 " + std::to_string(i + 1);
        nodes.push_back(std::move(n));
    }

    return nodes;
}

std::optional<std::size_t> treeRowIndexFor(const std::vector<RequirementNode>& nodes,
                                           const core::ObjectId& oid)
{
    // 全零保留值无节点（O-36：未分配身份不定位——不伪造跳转）。
    if (!oid.isValid()) {
        return std::nullopt;
    }
    // 线性扫描＝输入序（确定性——同输入同下标；树行规模有限，无需索引）。
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].objectId.has_value() && nodes[i].objectId.value() == oid) {
            return i;
        }
    }
    return std::nullopt;
}

}  // namespace sdurws::ird::requirements
