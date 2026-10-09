/**
 * @file   SelPanelCommandCatalog.hpp
 * @brief  selection 插件装配登记目录的构造函数面（零 Qt）——域命令
 *         描述符清单与域注册键的现产函数（登记值类型与挂位词表常量
 *         的唯一书写点在 assembly/ 公共头——本头零复制零第二权威）。
 *
 * 设计依据：
 *   - units/selection.md §16 WP-19-T10 行（回填入口的登记面）；§12.3
 *     （命令 token 语法——无点形态冻结常量的域面承载）；§3.4（零
 *     计算红线——本目录是纯值构造）；
 *   - assembly/sdurws/ird/selection/SelectionPluginAssembly.hpp（登记
 *     值类型 SelCommandDescriptor/SelPanelRegistration 与挂位/域键
 *     词表常量的唯一书写点——本目录只提供"现产函数"实现面）；
 *   - units/ui.md §3.5（键族 cmd.<id>.title——命令标题键派生规则见
 *     实现 TU 注）、§11.1（白名单 token "selection"）；
 *   - 先例：dynamics/plugin/DynPanelCommandCatalog.hpp（域命令清单/
 *     装配登记的零 Qt 目录形态——WP-17-T09）；
 *   - 任务契约 tasks/foundation/WP-19-T10.json acceptance 1/2。
 *
 * ★ 命令 token 自持口径（诚实登记，DTB §5.4——单元卡 §1.2 同步）：
 *   域命令 token 的权威常量在计算库回填公共头（无点冻结词表——
 *   T09 落位），但该头传递携带 project 公共头（处理器接口的传递
 *   依赖），而插件面零 project 包含（契约测试直接访问词表钉住）——
 *   故本头以**自持常量**承载同一词面（值逐字相等），逐字对账由契约
 *   测试消费计算库常量钉住（契约测试不在插件面扫描域——dynamics
 *   白名单对账用例同款精神）。token 改名属跨版本契约变更（走单元卡
 *   增量修订），两处常量同步改。
 *
 * 线程模型：目录查询纯函数（无状态——任意线程可调用）。
 */

#ifndef IRD_SELECTION_PLUGIN_SELPANELCOMMANDCATALOG_HPP
#define IRD_SELECTION_PLUGIN_SELPANELCOMMANDCATALOG_HPP

#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/selection/SelectionPluginAssembly.hpp> // SelCommandDescriptor/
                                                            //   kSelStageToken/
                                                            //   kSelDomainKey（唯一
                                                            //   书写点——assembly
                                                            //   PUBLIC include 面）

namespace sdurws::ird::selection {

/// 域命令 token（回填提交——无点形态；与计算库回填公共头的冻结常量
/// 逐字相等，契约测试对账钉住；插件面自持的唯一理由见文件头注）。
inline constexpr std::string_view kSelBackfillCommandToken =
    "apply-device-backfill";

/**
 * @brief 域命令描述符清单（§16 T10 行——回填入口一条；表外命令零登记）。
 *
 * @return 一条描述符（token 与 kSelBackfillCommandToken 逐字一致；
 *         titleKey 按 §3.5 键族派生——见实现 TU 注）
 */
std::vector<SelCommandDescriptor> selDomainCommands();

/**
 * @brief 就绪投影域注册键（恒 "selection"——kSelDomainKey 的
 *        std::string 便利形态；与 SelReadinessRow.domainKey 对账）。
 *
 * @return 域注册键（词表值——改词即宿主汇聚断链）
 */
std::string selReadinessDomainKey();

}  // namespace sdurws::ird::selection

#endif  // IRD_SELECTION_PLUGIN_SELPANELCOMMANDCATALOG_HPP
