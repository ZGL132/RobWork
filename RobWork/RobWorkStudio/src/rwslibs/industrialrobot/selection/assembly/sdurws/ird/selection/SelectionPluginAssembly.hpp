/**
 * @file   SelectionPluginAssembly.hpp
 * @brief  selection 插件装配门面——宿主装配层消费 selection 插件的
 *         **唯一公共入口**（WP-19-T02 最小可注册形态）。
 *
 * 设计依据：
 *   - units/selection.md §3.1（插件组成——sdurws_ird_selection_plugin
 *     ＝Qt Widgets 插件界面：目录管理页/筛选条件表单/候选表/淘汰原因
 *     视图/回填入口/进度与取消，随 WP-19-T10 落位）、§3.2（插件依赖
 *     仅两条：本计算库〔单元内〕＋Qt Widgets——**无 ui 编译边**，树/
 *     属性页协议列在"运行时注入/端口"列）、§3.4（插件零计算红线——
 *     机器可断言）、§3.5（目标布局——_plugin 目标随 WP-19-T02）
 *   - units/ui.md §11.1（白名单八 token 词表——"selection" 为第 6 个
 *     占位 token，先行登记）、§3.5（文案键族 plugin.<id>.title——id
 *     词表＝§11.1 白名单 token，值归 ui 文案资源）、UI-T21/T22 登记注
 *     （IUiTreeNodesProvider/IUiPropertyPagesProvider 域供给协议——
 *     selection.md §3.1 点名的插件域供给形态）
 *   - 先例：workflow/assembly/WorkflowPluginAssembly.hpp（WP-22-T02
 *     插件随落位任务同批创建的最小可注册形态）——差异见下方"落位形态
 *     说明"（诚实登记，非遗漏）
 *
 * ★ 落位形态说明（与 modeling/requirements/kinematics/workflow 插件门面
 * 的差异——为什么不实现 IPluginUiModule）：四先例的插件门面携带
 * ui::PluginUiDescriptor/IPluginUiModule（要求 sdurws_ird_ui 编译边，
 * 各卡 §3.2"插件目标→本计算库＋sdurws_ird_ui"明文）。selection.md
 * §3.2 的插件依赖仅"本计算库＋Qt Widgets"两行——**零 ui 编译边**（树/
 * 属性页等 ui 协作全部列为运行时注入/端口形态）；且卡 §3.1 点名的插件
 * 域供给协议是 UI-T21/T22 冻结的 IUiTreeNodesProvider/IUiPropertyPages-
 * Provider（非 IPluginUiModule——UI-T21/T22 登记注：三域迁移消费后
 * 域侧实现随各域任务）。因此 T02 门面以**自持描述符**承载可注册面：
 * pluginId/titleKey 两字段均有 ui.md 登记出处（§11.1 token＋§3.5 键族），
 * 零私造词表；宿主注册（真实 ui 类型、面板/命令/协议供给）随 WP-19-T10
 * 插件界面任务落位——本头不预建占位接口（NFR-MNT-04）。
 *
 * 落位形态说明（文件位置）：本头位于 selection 单元的 `assembly/` 目录
 * （plugin 目标的 PUBLIC include 面——插件界面目标的装配契约头，非产品
 * include/ 扫描域；与 plugin/ 同理在零 Qt 红线的文件域之外——ird_gates
 * 第 4 步产品面扫描域为 include/**＋src/**）。实现
 * （plugin/SelectionPluginAssembly.cpp）编入 sdurws_ird_selection_plugin
 * 目标。
 *
 * 线程模型：createSelectionPluginAssembly 为纯值工厂（无共享状态、
 * 无 Qt 调用）——任意线程可调用；T02 阶段装配时序未涉及（面板随
 * WP-19-T10 落位时按其任务卡的 UI 线程约束执行）。
 */

#ifndef IRD_SELECTION_ASSEMBLY_SELECTIONPLUGINASSEMBLY_HPP
#define IRD_SELECTION_ASSEMBLY_SELECTIONPLUGINASSEMBLY_HPP

#include <string>

namespace sdurws::ird::selection {

/**
 * @brief selection 插件装配描述符（WP-19-T02 最小可注册形态的装配产物）。
 *
 * 字段取舍口径（逐字段有据，零私造词表）：
 *   - pluginId：ui.md §11.1 白名单第 6 token "selection"（编译期/装配期
 *     常量词表，L5 应用壳固定）——插件身份的唯一书写点；
 *   - titleKey：ui.md §3.5 键族 plugin.<id>.title 的 "plugin.selection.
 *     title"（id 词表＝§11.1 白名单 token；UX-02：token 本身不进用户
 *     文本——用户见中文标题，值归 ui 文案资源，本域不携带值）。
 *
 * 面板/命令/能力声明与协议供给（IUiTreeNodesProvider/IUiPropertyPages-
 * Provider 域供给——卡 §3.1）**不在本结构**：随 WP-19-T10 以真实 ui
 * 类型落位（本头零 ui 头包含——卡 §3.2 无 ui 编译边的结构承载）。
 * 零业务计算逻辑（卡 §3.4）：本结构是纯值聚合，筛选/插值/校核/排序
 * 符号零出现——契约测试 SelectionPluginAssemblyContractTest 以词表
 * 扫描钉住。
 */
struct SelectionPluginDescriptor {
    /// 插件身份（ui.md §11.1 白名单 token——"selection"；显示名永不
    /// 替代身份，ARC-04 纪律）。
    std::string pluginId;
    /// 标题文案键（ui.md §3.5 键族 plugin.<id>.title——值归 ui 文案
    /// 资源文件，本域零文案值）。
    std::string titleKey;
};

/**
 * @brief 创建 selection 插件装配描述符（每次调用全新实例——无缓存）。
 *
 * T02 批次装配规则（逐字段见类型注）：pluginId＝"selection"、
 * titleKey＝"plugin.selection.title"。
 *
 * @return 装配描述符（纯值——零 Qt、零业务计算调用）
 */
SelectionPluginDescriptor createSelectionPluginAssembly();

}  // namespace sdurws::ird::selection

#endif  // IRD_SELECTION_ASSEMBLY_SELECTIONPLUGINASSEMBLY_HPP
