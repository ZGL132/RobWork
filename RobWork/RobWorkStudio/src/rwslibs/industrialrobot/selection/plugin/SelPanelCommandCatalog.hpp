/**
 * @file   SelPanelCommandCatalog.hpp
 * @brief  selection 插件装配登记目录的构造函数面（零 Qt）——域命令
 *         描述符清单与域注册键的现产函数（登记值类型与挂位词表常量
 *         的唯一书写点在 assembly/ 公共头——本头零复制零第二权威）。
 *
 * 设计依据：
 *   - units/selection.md §16 WP-19-T10 行（回填入口的登记面）；§12.3
 *     （命令双词形——project 命令 token 无点冻结＋ui 命令 id 点分）；
 *     §3.4（零计算红线——本目录是纯值构造）；
 *   - assembly/sdurws/ird/selection/SelectionPluginAssembly.hpp（登记
 *     值类型 SelCommandDescriptor/SelPanelRegistration 与挂位/域键
 *     词表常量的唯一书写点——本目录只提供"现产函数"实现面）；
 *   - units/ui.md §3.5（键族 cmd.<id>.title——命令标题键派生规则见
 *     实现 TU 注）、§7.1/§7.2（命令 id 点分句法——<域前缀>.<kebab>，
 *     段字符 [a-z0-9-]＋必含点）、§11.1（白名单 token "selection"）；
 *   - 先例：dynamics/plugin/DynPanelCommandCatalog.hpp（域命令清单/
 *     装配登记的零 Qt 目录形态——WP-17-T09）；modeling/plugin/
 *     PanelCommandCatalog.cpp（ui id 点分 modeling.new-from-template
 *     ＋project token 无点 kCmdApply* 双词形分离的同款先例）；
 *   - 任务契约 tasks/foundation/WP-19-T10.json acceptance 1/2。
 *
 * ★ 命令双词形拆分（RUL-TOK 裁决销案批 2026-10-10——P-SEL-3/P-PR-9）：
 *   回填命令存在两个**词形各异**的标识面，各自单一书写点（禁止互相
 *   推导或第三副本）：
 *   ①ui 命令 id（本头 kSelBackfillUiCommandId）＝点分
 *     "selection.apply-device-backfill"——服从 ui.md §7.1/§7.2 既冻
 *     句法（<域前缀>.<kebab>；modeling 十条域命令先例），承载面＝
 *     SelCommandDescriptor.token（宿主注册表翻译 id＝token 逐字）；
 *   ②project 命令 token（计算库 kBackfillCommandToken）＝无点
 *     "apply-device-backfill"——服从 project.md §4.4.4 冻结语法
 *     ^[a-z0-9-]{3,64}（O-35 裁决采纳无点形态；P-SEL-3 据此销案），
 *     权威书写点在 Backfill.hpp 冻结常量，本头零副本。
 *   两词形的对齐关系（ui id 前缀＝pluginId＋"."、尾段＝project
 *   token）由契约测试消费计算库常量钉住（契约测试不在插件面扫描
 *   域——可包含 Backfill.hpp；dynamics 白名单对账用例同款精神）。
 *   任一词形改名属跨版本契约变更（走单元卡增量修订），两书写点经
 *   该对账用例同步改。
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

/// 回填命令的 ui 命令 id（点分 <域前缀>.<kebab> 词形——ui.md §7.1/§7.2
/// 既冻句法，RUL-TOK 批由无点词形修订而来：裁决在域侧生效、宿主校验序
/// 零改动）。本常量是 ui id 词形的唯一书写点（承载面＝SelCommandDescriptor
/// .token——翻译 id＝token 逐字＋标题键族派生＋面板提交意图标识）；
/// project 命令 token（无点词形）的权威书写点在计算库 Backfill.hpp
/// kBackfillCommandToken，两词形的对齐关系由契约测试逐段对账钉住。
inline constexpr std::string_view kSelBackfillUiCommandId =
    "selection.apply-device-backfill";

/**
 * @brief 域命令描述符清单（§16 T10 行——回填入口一条；表外命令零登记）。
 *
 * @return 一条描述符（token 与 kSelBackfillUiCommandId 逐字一致；
 *         titleKey 按 §3.5 键族 cmd.<id>.title 派生——id 为点分 ui
 *         命令 id，先例 modeling 十域命令 cmd.modeling.*.title）
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
