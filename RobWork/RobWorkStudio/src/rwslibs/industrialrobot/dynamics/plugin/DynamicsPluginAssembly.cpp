/**
 * @file   DynamicsPluginAssembly.cpp
 * @brief  dynamics 插件装配门面的实现翻译单元——描述符现产的唯一装配点
 *         （WP-17-T02 最小可注册形态）。
 *
 * 设计依据：
 *   - units/dynamics.md §9.5（UI 协作——插件界面面随 WP-17-T09；"UI
 *     线程不得执行动力学计算"红线——本 TU 零动力学计算类符号〔词表
 *     见契约测试；其扫描为全文扫描，本注释不书写词表符号字样〕）、
 *     §3.2（目标布局——_plugin 目标随 WP-17-T02 最小可注册实现）
 *   - units/ui.md §11.1（白名单 token "dynamics"）、§3.5（键族
 *     plugin.<id>.title）
 *   - 先例：selection/plugin/SelectionPluginAssembly.cpp（WP-19-T02
 *     同款"描述符现产"装配形态——业务域单元 T02 批次零 Q_OBJECT 类）
 *
 * 线程模型：纯值工厂（无共享状态、无 Qt 调用）——任意线程可调用。
 *
 * Qt 说明（诚实登记）：本 TU 零 Qt 类消费（同 selection/workflow T02
 * "链接面先行建立"取舍）——Qt6 链接在 sdurws_ird_dynamics_plugin 目标
 * 层面建立（卡 §3.2 二分结构"Qt Widgets 插件"例外载体面），WP-17-T09
 * 面板批次直接增列源文件即可，目标链接面零改动。零 Q_OBJECT 类故目标
 * 不开 AUTOMOC（首个 Q_OBJECT 类落位任务按 modeling/requirements 先例
 * 置 AUTOMOC ON——DTB §5.1 v0.17 行口径）。
 */

#include <sdurws/ird/dynamics/DynamicsPluginAssembly.hpp>

namespace sdurws::ird::dynamics {

DynamicsPluginDescriptor createDynamicsPluginAssembly()
{
    // 描述符现产（字段值与门面头类型注逐条对应；两值均有 ui.md 登记出处
    // ——§11.1 白名单 token 与 §3.5 键族，零私造词表）。
    DynamicsPluginDescriptor descriptor;
    descriptor.pluginId = "dynamics";  // ui.md §11.1 白名单 token（AboutDialog.cpp 在册）
    descriptor.titleKey =
        "plugin.dynamics.title";  // ui.md §3.5 键族（值归 ui 文案资源）

    // 面板/命令/投影不在 T02 批次（卡 §9.5——dynamics.analyze/show-curves/
    // locate-peak/replay-at/export-curve-data 命令族与曲线联动/峰值定位/
    // 时刻回放随 WP-17-T09 以真实 ui 类型落位；本处不预建占位接口——
    // NFR-MNT-04）。
    return descriptor;
}

}  // namespace sdurws::ird::dynamics
