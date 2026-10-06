/**
 * @file   TrajectoryPluginAssembly.hpp
 * @brief  trajectory 插件装配门面——宿主装配层消费 trajectory 插件的
 *         **唯一公共入口**（WP-16-T03 最小可注册形态）。
 *
 * 设计依据：
 *   - units/trajectory.md §16.1（插件目标与形态——sdurws_ird_trajectory_
 *     plugin 为 Qt Widgets 界面载体，"零计算逻辑：规划、碰撞、时间参数
 *     化、复检全部在 worker/计算库；UI 线程只做投影、插值查表（动画）、
 *     命令提交"；界面面落位随 WP-16-T12——本头不预建占位接口）、§14.5.1
 *     （领域命令族——trajectory.plan-sequence 等会话命令，全部零修订）、
 *     §16.2（零修订与零计算红线——机器可断言）
 *   - units/ui.md §11.1（白名单 token 词表——"trajectory" 为在册 token，
 *     ui/src/AboutDialog.cpp pluginUiWhitelist 实测含 "trajectory" 行）、
 *     §3.5（文案键族 plugin.<id>.title——id 词表＝§11.1 白名单 token，
 *     值归 ui 文案资源；UX-02：token 本身不进用户文本）
 *   - 先例：dynamics/assembly/DynamicsPluginAssembly.hpp（WP-17-T02
 *     同款"自持描述符装配门面"最小可注册形态；selection WP-19-T02/
 *     workflow WP-22-T02 更早先例）——trajectory 与 dynamics/selection
 *     同属业务域单元、插件界面面均随后续任务（WP-16-T12/WP-17-T09/
 *     WP-19-T10），T03 批次零 Q_OBJECT 类、零 ui 编译边（本头零 ui 头
 *     包含）
 *
 * ★ 落位形态说明（诚实登记，非遗漏）：本门面以**自持描述符**承载可注册
 *   面——pluginId/titleKey 两字段均有 ui.md 登记出处（§11.1 token＋§3.5
 *   键族），零私造词表；面板/命令/协议供给（轨迹工作流页、曲线视图入口、
 *   动画衔接的域侧数据面与 §14.5.1 命令族注册）随 WP-16-T12 插件界面
 *   任务落位——本头不预建占位接口（NFR-MNT-04）。零业务计算逻辑（卡
 *   §16.1 红线）：本结构是纯值聚合，轨迹域计算类符号零出现——契约测试
 *   TrajectoryPluginAssemblyContractTest 以词表扫描钉住（卡 §16.2"UI
 *   线程不得执行规划/碰撞/时间参数化"红线的 T03 侧执行面）。
 *
 * 落位形态说明（文件位置）：本头位于 trajectory 单元的 `assembly/` 目录
 * （plugin 目标的 PUBLIC include 面——插件界面目标的装配契约头，非产品
 * include/ 扫描域；与 plugin/ 同理在零 Qt 红线的文件域之外——ird_gates
 * 第 4 步产品面扫描域为 include/**＋src/**）。实现
 * （plugin/TrajectoryPluginAssembly.cpp）编入 sdurws_ird_trajectory_
 * plugin 目标。
 *
 * 线程模型：createTrajectoryPluginAssembly 为纯值工厂（无共享状态、
 * 无 Qt 调用）——任意线程可调用；T03 阶段装配时序未涉及（面板随
 * WP-16-T12 落位时按其任务卡的 UI 线程约束执行）。
 */

#ifndef IRD_TRAJECTORY_ASSEMBLY_TRAJECTORYPLUGINASSEMBLY_HPP
#define IRD_TRAJECTORY_ASSEMBLY_TRAJECTORYPLUGINASSEMBLY_HPP

#include <string>

namespace sdurws::ird::trajectory {

/**
 * @brief trajectory 插件装配描述符（WP-16-T03 最小可注册形态的装配产物）。
 *
 * 字段取舍口径（逐字段有据，零私造词表）：
 *   - pluginId：ui.md §11.1 白名单 token "trajectory"（编译期/装配期
 *     常量词表，L5 应用壳固定；ui/src/AboutDialog.cpp pluginUiWhitelist
 *     实测在册）——插件身份的唯一书写点；
 *   - titleKey：ui.md §3.5 键族 plugin.<id>.title 的 "plugin.trajectory.
 *     title"（id 词表＝§11.1 白名单 token；UX-02：token 本身不进用户
 *     文本——用户见中文标题，值归 ui 文案资源，本域不携带值）。
 *
 * 面板/命令/能力声明（轨迹工作流页、曲线视图入口、动画播放/进度、
 * §14.5.1 六条会话命令与 DomainReadinessItem 七态投影供给——卡 §16）
 * **不在本结构**：随 WP-16-T12 以真实 ui 类型落位（本头零 ui 头包含）。
 * 零业务计算逻辑（卡 §16.1/§16.2 红线）：本结构是纯值聚合，轨迹域计算
 * 类符号零出现——契约测试词表扫描钉住（该扫描为全文扫描、不剥注释
 * ——本头注释亦不书写词表符号字样）。
 */
struct TrajectoryPluginDescriptor {
    /// 插件身份（ui.md §11.1 白名单 token——"trajectory"；显示名永不
    /// 替代身份，ARC-04 纪律）。
    std::string pluginId;
    /// 标题文案键（ui.md §3.5 键族 plugin.<id>.title——值归 ui 文案
    /// 资源文件，本域零文案值）。
    std::string titleKey;
};

/**
 * @brief 创建 trajectory 插件装配描述符（每次调用全新实例——无缓存）。
 *
 * T03 批次装配规则（逐字段见类型注）：pluginId＝"trajectory"、
 * titleKey＝"plugin.trajectory.title"。
 *
 * @return 装配描述符（纯值——零 Qt、零业务计算调用）
 */
TrajectoryPluginDescriptor createTrajectoryPluginAssembly();

}  // namespace sdurws::ird::trajectory

#endif  // IRD_TRAJECTORY_ASSEMBLY_TRAJECTORYPLUGINASSEMBLY_HPP
