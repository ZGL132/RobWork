/**
 * @file   OptimizationPluginAssembly.cpp
 * @brief  optimization 插件装配门面的实现翻译单元——描述符现产的唯一装配
 *         点（WP-20-T02 最小可注册形态）。
 *
 * 设计依据：
 *   - units/optimization.md §3.1/§3.2（目标布局与二分结构——插件界面面
 *     随 WP-20-T10；"只消费端口与只读投影，不持算法/判定真值"红线——
 *     本 TU 零优化域计算类符号〔词表见契约测试；其扫描为全文扫描，本
 *     注释不书写词表符号字样〕）
 *   - units/ui.md §11.1（白名单 token "optimization"——AboutDialog.cpp
 *     在册）、§3.5（键族 plugin.<id>.title）
 *   - 先例：trajectory/plugin/TrajectoryPluginAssembly.cpp（WP-16-T03
 *     同款"描述符现产"装配形态——业务域单元落位批次零 Q_OBJECT 类；
 *     dynamics/selection/workflow 更早先例）
 *
 * 线程模型：纯值工厂（无共享状态、无 Qt 调用）——任意线程可调用。
 *
 * Qt 说明（诚实登记）：本 TU 零 Qt 类消费（同 dynamics/selection/
 * trajectory/workflow 落位批次"链接面先行建立"取舍）——Qt6 链接在
 * sdurws_ird_optimization_plugin 目标层面建立（卡 §3.2 二分结构
 * "Qt Widgets 界面"例外载体面），WP-20-T10 面板批次直接增列源文件即可，
 * 目标链接面零改动。零 Q_OBJECT 类故目标不开 AUTOMOC（首个 Q_OBJECT 类
 * 落位任务按 modeling/requirements 先例置 AUTOMOC ON——DTB §5.1 v0.17
 * 行口径）。
 */

#include <sdurws/ird/optimization/OptimizationPluginAssembly.hpp>

namespace sdurws::ird::optimization {

OptimizationPluginDescriptor createOptimizationPluginAssembly()
{
    // 描述符现产（字段值与门面头类型注逐条对应；两值均有 ui.md 登记出处
    // ——§11.1 白名单 token 与 §3.5 键族，零私造词表）。
    OptimizationPluginDescriptor descriptor;
    descriptor.pluginId = "optimization";  // ui.md §11.1 白名单 token（AboutDialog.cpp 在册）
    descriptor.titleKey =
        "plugin.optimization.title";  // ui.md §3.5 键族（值归 ui 文案资源）

    // 面板/命令/投影不在 T02 批次（卡 §14.1——变量表/约束页/运行控制/
    // 候选表与对比随 WP-20-T10 以真实 ui 类型落位；本处不预建占位接口
    // ——NFR-MNT-04）。
    return descriptor;
}

}  // namespace sdurws::ird::optimization

