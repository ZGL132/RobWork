/**
 * @file   TrajectoryPluginAssembly.cpp
 * @brief  trajectory 插件装配门面的实现翻译单元——描述符现产的唯一装配
 *         点（WP-16-T03 最小可注册形态）。
 *
 * 设计依据：
 *   - units/trajectory.md §16.1（插件界面面随 WP-16-T12；"零计算逻辑：
 *     规划、碰撞、时间参数化、复检全部在 worker/计算库"红线——本 TU 零
 *     轨迹域计算类符号〔词表见契约测试；其扫描为全文扫描，本注释不书写
 *     词表符号字样〕）、§16.2（零修订红线——会话命令族全部零修订）
 *   - units/ui.md §11.1（白名单 token "trajectory"）、§3.5（键族
 *     plugin.<id>.title）
 *   - 先例：dynamics/plugin/DynamicsPluginAssembly.cpp（WP-17-T02 同款
 *     "描述符现产"装配形态——业务域单元落位批次零 Q_OBJECT 类）
 *
 * 线程模型：纯值工厂（无共享状态、无 Qt 调用）——任意线程可调用。
 *
 * Qt 说明（诚实登记）：本 TU 零 Qt 类消费（同 dynamics/selection/workflow
 * 落位批次"链接面先行建立"取舍）——Qt6 链接在 sdurws_ird_trajectory_
 * plugin 目标层面建立（卡 §16.1 二分结构"Qt Widgets 插件"例外载体面），
 * WP-16-T12 面板批次直接增列源文件即可，目标链接面零改动。零 Q_OBJECT
 * 类故目标不开 AUTOMOC（首个 Q_OBJECT 类落位任务按 modeling/requirements
 * 先例置 AUTOMOC ON——DTB §5.1 v0.17 行口径）。
 */

#include <sdurws/ird/trajectory/TrajectoryPluginAssembly.hpp>

namespace sdurws::ird::trajectory {

TrajectoryPluginDescriptor createTrajectoryPluginAssembly()
{
    // 描述符现产（字段值与门面头类型注逐条对应；两值均有 ui.md 登记出处
    // ——§11.1 白名单 token 与 §3.5 键族，零私造词表）。
    TrajectoryPluginDescriptor descriptor;
    descriptor.pluginId = "trajectory";  // ui.md §11.1 白名单 token（AboutDialog.cpp 在册）
    descriptor.titleKey =
        "plugin.trajectory.title";  // ui.md §3.5 键族（值归 ui 文案资源）

    // 面板/命令/投影不在 T03 批次（卡 §16——轨迹工作流页/曲线视图入口/
    // 动画衔接与 §14.5.1 六条会话命令随 WP-16-T12 以真实 ui 类型落位；
    // 本处不预建占位接口——NFR-MNT-04）。
    return descriptor;
}

}  // namespace sdurws::ird::trajectory
