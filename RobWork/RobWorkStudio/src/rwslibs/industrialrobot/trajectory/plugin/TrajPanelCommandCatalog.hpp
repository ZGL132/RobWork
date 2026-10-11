/**
 * @file   TrajPanelCommandCatalog.hpp
 * @brief  trajectory 插件装配登记目录的构造函数面（零 Qt）——§14.5.1 七
 *         命令描述符清单与域注册键的现产函数（登记值类型与挂位词表常量
 *         的唯一书写点在 assembly/ 公共头——本头零复制零第二权威）。
 *
 * 设计依据：
 *   - units/trajectory.md §14.5.1（领域命令表七行 token——plan-sequence/
 *     preview-segment/local-replan/play/playback-seek/export/show-curve，
 *     修订列全部"无"）；§16.1（GUI 手动点验流程消费本目录的登记记录）；
 *   - assembly/sdurws/ird/trajectory/TrajectoryPluginAssembly.hpp（登记
 *     值类型 TrjCommandDescriptor/TrjPanelRegistration 与挂位/域键词表
 *     常量的唯一书写点——本目录只提供"现产函数"实现面）；
 *   - units/ui.md §3.5（键族 cmd.<id>.title——命令标题键派生规则见
 *     实现 TU 注）；
 *   - 先例：dynamics/plugin/DynPanelCommandCatalog.hpp（WP-17-T09 同款
 *     目录形态）；
 *   - 任务契约 tasks/foundation/WP-16-T12.json acceptance 1。
 *
 * 线程模型：目录查询纯函数（无状态——任意线程可调用）。
 */

#ifndef IRD_TRAJECTORY_PLUGIN_TRAJPANELCOMMANDCATALOG_HPP
#define IRD_TRAJECTORY_PLUGIN_TRAJPANELCOMMANDCATALOG_HPP

#include <string>
#include <vector>

#include <sdurws/ird/trajectory/TrajectoryPluginAssembly.hpp> // TrjCommandDescriptor/
                                                               //   kTrjStageToken/
                                                               //   kTrjDomainKey（唯一
                                                               //   书写点——assembly
                                                               //   PUBLIC include 面）

namespace sdurws::ird::trajectory {

/**
 * @brief 域命令描述符清单（§14.5.1 表行序——稳定序；七条全量）。
 *
 * @return 七条描述符（token 与 Commands.hpp kCommandTokens 逐位一致
 *         ——契约测试对账用例钉住；表外命令零登记）
 */
std::vector<TrjCommandDescriptor> trjDomainCommands();

/**
 * @brief 就绪投影域注册键（恒 "trajectory"——kTrjDomainKey 的
 *        std::string 便利形态；与 TrajModuleSessionState 投影行对账）。
 *
 * @return 域注册键（词表值——改词即宿主汇聚断链）
 */
std::string trjReadinessDomainKey();

}  // namespace sdurws::ird::trajectory

#endif  // IRD_TRAJECTORY_PLUGIN_TRAJPANELCOMMANDCATALOG_HPP
