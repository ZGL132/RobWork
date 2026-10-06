/**
 * @file   SelectionPluginAssembly.cpp
 * @brief  selection 插件装配门面的实现翻译单元——描述符现产的唯一装配点
 *         （WP-19-T02 最小可注册形态）。
 *
 * 设计依据：
 *   - units/selection.md §3.1（插件组成——界面面随 WP-19-T10）、§3.2
 *     （插件依赖仅本计算库＋Qt Widgets）、§3.4（插件零计算红线——本
 *     TU 无筛选/插值/校核/排序符号，契约测试词表扫描钉住）
 *   - units/ui.md §11.1（白名单 token "selection"）、§3.5（键族
 *     plugin.<id>.title）
 *   - 先例：workflow/plugin/WorkflowPluginAssembly.cpp（WP-22-T02 同款
 *     "描述符现产"装配形态——selection 因卡 §3.2 无 ui 编译边，仅承载
 *     自持描述符半区，差异论证见 assembly 门面头文件注）
 *
 * 线程模型：纯值工厂（无共享状态、无 Qt 调用）——任意线程可调用。
 *
 * Qt 说明（诚实登记）：本 TU 零 Qt 类消费（同 workflow T02"链接面先行
 * 建立"取舍）——Qt6 链接在 sdurws_ird_selection_plugin 目标层面建立
 * （卡 §3.2"插件目标→Qt Widgets"例外载体面），WP-19-T10 面板批次直接
 * 增列源文件即可，目标链接面零改动。零 Q_OBJECT 类故目标不开 AUTOMOC
 * （首个 Q_OBJECT 类落位任务按 modeling/requirements 先例置 AUTOMOC ON
 * ——DTB §5.1 v0.17 行口径）。
 */

#include <sdurws/ird/selection/SelectionPluginAssembly.hpp>

namespace sdurws::ird::selection {

SelectionPluginDescriptor createSelectionPluginAssembly()
{
    // 描述符现产（字段值与门面头类型注逐条对应；两值均有 ui.md 登记出处
    // ——§11.1 白名单 token 与 §3.5 键族，零私造词表）。
    SelectionPluginDescriptor descriptor;
    descriptor.pluginId = "selection";  // ui.md §11.1 白名单第 6 token
    descriptor.titleKey =
        "plugin.selection.title";  // ui.md §3.5 键族（值归 ui 文案资源）

    // 面板/命令/协议供给不在 T02 批次（卡 §3.1——目录管理页/筛选条件/
    // 候选表/淘汰原因视图/回填入口与 IUiTreeNodesProvider/IUiProperty-
    // PagesProvider 域供给随 WP-19-T10 以真实 ui 类型落位；本处不预建
    // 占位接口——NFR-MNT-04）。
    return descriptor;
}

}  // namespace sdurws::ird::selection
