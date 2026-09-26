/**
 * @file   PanelTreeModel.hpp
 * @brief  需求对象树投影（零 Qt）——左栏"需求集→四分组→条目"的纯函数
 *         投影层（卡 §9.8 面板表第 1 行）。
 *
 * 设计依据：
 *   - units/requirements.md §9.8（面板组成表——"需求对象树（左栏，需求
 *     节点）：需求集→工位/区域/工况/计划四分组→条目；节点锚＝ObjectId，
 *     显示 name（工程用语）"；消费契约＝②查询端口＋ui SelectionModel）、
 *     §9.8 线程约束行（面板不缓存权威数据——防第二真值）、§4.1/§4.2
 *     （五对象与根引用表——树的层级结构依据）、§4.8（name 是语义字段
 *     ——D-REQ-6，树显示面直接用条目名）
 *   - units/ui.md §4.2/§6.6（UX-02 工程用语：节点只显示局部名＋工程用语
 *     标签，零哈希/Schema/插件名——守卫复用已落位的
 *     ui::UiText::ensureNoInternalIdentity，不私写第二实现）
 *   - 需求 UX-05（需求界面）、UX-02（零内部标识呈现）；任务契约
 *     tasks/foundation/WP-14-T08.json acceptance 1（需求对象树行）
 *
 * 背景说明：插件是 requirements 计算库的唯一交互前端，只消费计算库公共
 * 接口与 ui 平台契约，不持有业务判定（ARCH §3.3 二分结构）。本头把"树该
 * 显示什么"的全部信息架构决策实现为纯函数投影：输入＝编辑器工作集
 * （RequirementWorkingSet——②查询端口的编辑态承载），输出＝树节点行。
 * 投影无副作用、无缓存成员（"面板不缓存权威数据"的结构性保证——每次
 * 调用现场重算，同输入同输出，NFR-COR-02）。
 *
 * ★ P-REQ-8 处置（契约 knownPitfalls）：ui SelectionModel 尚未落位（ui.md
 *   §4.2 为契约文本；磁盘核对 ui/include 无对应头）。本头只产出树行数据，
 *   选中会话态的最小承载见 PanelSelection.hpp（P-MDL-8 同款接缝先例——
 *   ui 侧落位后按 R-REQ-1 增量同步，单元卡 §14.6 登记）。
 *
 * 线程约束：全部函数为纯函数（无共享可变状态），可重入；入参工作集本身
 * 仅 UI 线程可变（§3.4 总约定 2——调用方保证传参期间无并发写）。
 * 确定性：同输入→同输出（行序固定：根、四分组按卡面序、组内条目按工作
 * 集序〔＝ObjectId 字典序，I-REQ-1〕；无 locale/时钟/环境读取）。
 */

#ifndef IRD_REQUIREMENTS_PLUGIN_PANELTREEMODEL_HPP
#define IRD_REQUIREMENTS_PLUGIN_PANELTREEMODEL_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Identity.hpp>  // core::ObjectId（节点锚/定位跳转目标）
#include <sdurws/ird/requirements/Editor.hpp>  // RequirementWorkingSet（②查询端口工作集值）

namespace sdurws::ird::requirements {

// =====================================================================
// 树节点模型（卡 §9.8 面板表第 1 行的投影承载）
// =====================================================================

/**
 * @brief 树节点类别词表（"需求集→四分组→条目"的两级结构——投影判别用，
 *        非存储枚举）。
 *
 * 词表固定为卡 §9.8 行"需求集→工位/区域/工况/计划四分组→条目"的节点
 * 类别；分组节点无对象身份（呈现折叠用），条目节点锚定条目 ObjectId。
 */
enum class RequirementNodeKind : std::uint8_t {
    RequirementRoot,  ///< 需求集根（req-set——修订闭包锚，§4.2）
    PointsGroup,      ///< 工位分组（req-point-set——任务点条目的呈现折叠）
    RegionsGroup,     ///< 区域分组（req-region-set）
    ConditionsGroup,  ///< 工况分组（req-condition-set）
    PlansGroup,       ///< 计划分组（req-plan-set）
    Point,            ///< 任务点条目（TaskPoint——节点锚＝条目 ObjectId）
    Region,           ///< 区域条目（WorkRegion）
    Condition,        ///< 工况条目（OperatingCondition）
    Plan,             ///< 采样计划条目（SamplingPlan）
};

/**
 * @brief 需求对象树的一个节点行（纯值——widget 层 QTreeWidget 的行数据源）。
 *
 * ★ 节点锚＝ObjectId（卡 §9.8 行原文）：树/面板/校验条/三维拾取四方以
 * 同一 ObjectId 关联（L-R1 选中联动的关联键）。分组节点无对象身份
 * （objectId=nullopt——分组仅是呈现折叠，不是对象）。显示面＝局部名
 * （条目 name——D-REQ-6 语义字段）＋工程用语标签；条目节点构造即经
 * ui::ensureNoInternalIdentity 守卫，哈希/内部 token 形态的串不可能进入
 * displayLabel（守卫抛错即调用方违约 fail-fast——非法名不应到达面板）。
 *
 * 线程安全：纯值类型。
 */
struct RequirementNode {
    RequirementNodeKind kind = RequirementNodeKind::RequirementRoot;  ///< 节点类别
    std::optional<core::ObjectId> objectId;  ///< 节点锚（分组节点＝nullopt——仅呈现折叠）
    std::string localName;                   ///< 条目语义名（name——分组节点为空）
    std::string displayLabel;                ///< 工程用语标签（已过 UX-02 守卫——零哈希）
    std::size_t entryCount = 0;              ///< 分组子条目数（分组节点有效——折叠标题"工位（3）"）

    bool operator==(const RequirementNode& o) const
    {
        return kind == o.kind && objectId == o.objectId && localName == o.localName
            && displayLabel == o.displayLabel && entryCount == o.entryCount;
    }
    bool operator!=(const RequirementNode& o) const { return !(*this == o); }
};

/**
 * @brief 构建需求对象树投影（§9.8 面板表第 1 行——根→四分组→条目）。
 *
 * 结构与呈现规则（确定性来源——行序固定）：
 *   - 行序＝卡面固定序：根、工位分组、工位条目…、区域分组、区域条目…、
 *     工况分组、工况条目…、计划分组、计划条目…；组内条目按工作集既有序
 *     （ObjectId 字典序——I-REQ-1，投影不重排：单一排序权威纪律）。
 *   - 条目 displayLabel＝条目 name 原文（D-REQ-6：name 是工程用语语义
 *     字段，进报告/定位）；根节点 displayLabel＝根 name；分组节点标签由
 *     本函数按固定中文词表产出（"工位"/"区域"/"工况"/"计划"——呈现文案
 *     的确定性来源）。
 *   - UX-02 守卫：每个 displayLabel 过 ui::ensureNoInternalIdentity——
 *     name 若为哈希形态（64 位十六进制串等）即抛错（调用方错误：非法名
 *     不应到达面板；构造边界已拒的兜底守卫）。
 *
 * @param ws [in] 工作集（只读；仅 UI 线程可变——§3.4）
 * @return 树节点行序列（行序＝呈现序；纯投影——不缓存）
 *
 * 纯函数；确定性；不抛（UX-02 守卫抛 std::invalid_argument 除外——
 * ensureNoInternalIdentity 契约；那是非法输入的 fail-fast）。
 */
std::vector<RequirementNode> buildRequirementTree(const RequirementWorkingSet& ws);

/**
 * @brief 按节点锚反查树行下标（L-R1 反向定位的呈现半区——校验面板跳转
 *        时滚动到目标行）。
 *
 * @param nodes [in] buildRequirementTree 产出（只读）
 * @param oid   [in] 目标条目锚（诊断 subject 等；分组/根节点无锚不可定位）
 * @return 命中＝行下标（0 起）；未命中＝nullopt（闭包外身份——不定位，
 *         呈现保持原状，不伪造定位）
 *
 * 纯函数；确定性（线性扫描＝输入序）。
 */
std::optional<std::size_t> treeRowIndexFor(const std::vector<RequirementNode>& nodes,
                                           const core::ObjectId& oid);

}  // namespace sdurws::ird::requirements

#endif  // IRD_REQUIREMENTS_PLUGIN_PANELTREEMODEL_HPP
