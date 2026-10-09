/**
 * @file   DynPanelCommandCatalog.hpp
 * @brief  dynamics 插件装配登记目录的构造函数面（零 Qt）——§9.5 五命令
 *         描述符清单与域注册键的现产函数（登记值类型与挂位词表常量
 *         的唯一书写点在 assembly/ 公共头——本头零复制零第二权威）。
 *
 * 设计依据：
 *   - units/dynamics.md §9.5（UI 协作表五命令 token 行）；§11.5（GUI
 *     手动点验流程消费本目录的登记记录）；
 *   - assembly/sdurws/ird/dynamics/DynamicsPluginAssembly.hpp（登记值
 *     类型 DynCommandDescriptor/DynPanelRegistration 与挂位/域键词表
 *     常量的唯一书写点——本目录只提供"现产函数"实现面）；
 *   - units/ui.md §3.5（键族 cmd.<id>.title——命令标题键派生规则见
 *     实现 TU 注）；
 *   - 先例：kinematics/plugin/KinPanelCommandCatalog.hpp（域命令清单/
 *     装配登记的零 Qt 目录形态——WP-15-T12）；
 *   - 任务契约 tasks/foundation/WP-17-T09.json acceptance 1/2。
 *
 * 线程模型：目录查询纯函数（无状态——任意线程可调用）。
 */

#ifndef IRD_DYNAMICS_PLUGIN_DYNPANELCOMMANDCATALOG_HPP
#define IRD_DYNAMICS_PLUGIN_DYNPANELCOMMANDCATALOG_HPP

#include <string>
#include <vector>

#include <sdurws/ird/dynamics/DynamicsPluginAssembly.hpp> // DynCommandDescriptor/
                                                          //   kDynStageToken/
                                                          //   kDynDomainKey（唯一
                                                          //   书写点——assembly
                                                          //   PUBLIC include 面）

namespace sdurws::ird::dynamics {

/**
 * @brief 域命令描述符清单（§9.5 表行序——稳定序；五条全量）。
 *
 * @return 五条描述符（token 与 Commands.hpp kCommandTokens 逐位一致
 *         ——契约测试对账用例钉住；表外命令零登记）
 */
std::vector<DynCommandDescriptor> dynDomainCommands();

/**
 * @brief 就绪投影域注册键（恒 "dynamics"——kDynDomainKey 的 std::string
 *        便利形态；与 DynReadinessRow.domainKey 对账）。
 *
 * @return 域注册键（词表值——改词即宿主汇聚断链）
 */
std::string dynReadinessDomainKey();

}  // namespace sdurws::ird::dynamics

#endif  // IRD_DYNAMICS_PLUGIN_DYNPANELCOMMANDCATALOG_HPP
