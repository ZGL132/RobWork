/**
 * @file   UiTypes.hpp
 * @brief  ui 单元公共值类型词表——命令 id／文案键／菜单路径等跨头共享的
 *         基础别名（§3.3 公共头布局登记的 UiTypes.hpp 落位）。
 *
 * 设计依据：
 *   - units/ui.md §3.3（公共头布局："UiTypes.hpp＝七态词表、StageId、
 *     CommandId/TextKey、UiSessionState 等本单元公共值类型"——本头为该行
 *     的首个落位（UI-T06），当前仅承载命令设施所需的词表别名；七态词表已
 *     随 UI-T04 以"首消费冻结"机制落位 UiProjections.hpp（§16.7 v0.6），
 *     其余条目随归属任务增量落位，不预建——NFR-MNT-04）；
 *   - §7.1（命令项模型：CommandId＝"点分小写，全局唯一"；TextKey＝§3.5
 *     文案键体系；MenuPath＝菜单与面板分组）；
 *   - NFR-MNT-03（词表唯一权威：本头是这些别名的唯一定义点，各公共头
 *     include 本头取用，禁止第二处重复定义）。
 *
 * 背景说明（为什么是别名而不是强类型）：命令 id／文案键在本单元内是
 * **呈现层标识**——它们的唯一性/词法约束由注册边界校验（§7.2 id 句法
 * 校验）与键命名约定（§3.5 "cmd.<id>.title" 族）承载，不参与内容寻址/
 * 身份计算（与 core 五类 Id128 强类型的场景不同）。std::string 别名把
 * 校验责任留给唯一的注册入口（CommandRegistry::registerCommand），
 * 避免"强类型＋宽松转换路径"的双权威。
 *
 * 线程安全：纯值别名（并发只读安全）。
 */

#ifndef SDURWS_IRD_UI_UITYPES_HPP
#define SDURWS_IRD_UI_UITYPES_HPP

#include <string>

namespace sdurws {
namespace ird {
namespace ui {

// =====================================================================
// 命令设施词表（§7.1 命令项模型——UI-T06 首消费落位）
// =====================================================================

/**
 * @brief 命令 id（§7.1 原文："点分小写，全局唯一"）。
 *
 * 词法（§7.2 注册协议第 1 步校验；词形权威＝§7.1 冻结表原文——表内
 * workbench.commandPalette／view.displayMode／view.resetHome 等 id 段含
 * 大写，"点分小写"按冻结词形落位为"点分段＋段字符 [A-Za-z0-9-]＋必含
 * 点"，措辞差登记 ui.md §16.7 v0.8）。全局唯一由注册边界拒绝重复
 * （§7.2 第 2 步——UI-CMD-DUPLICATE）保证，本别名不做词法自证。
 */
using CommandId = std::string;

/**
 * @brief 文案键（§3.5 文案键体系——UX-02 工程用语的键半区）。
 *
 * 键约定（§3.5 冻结）："cmd.<id>.title"、"cmd.<id>.kw.<n>"（模糊搜索
 * 关键字）、"stage.<id>.title"、"state.<token>.label"、"diag.<code-lower>.
 * title/detail" 等；**值（中英文资源）归 ui 文案资源文件**（P-DIAG-9
 * 交接），UiText::resolve（UI-T09）是唯一解析出口。UI-T09 落地前，
 * 面板/菜单以过渡文案表承载中文值（statusWordTransitionalLabel 同案——
 * 键不变，迁移时只换值源）。
 */
using TextKey = std::string;

/**
 * @brief 菜单与面板分组路径（§7.1："菜单与面板分组（"文件/新建"）"）。
 *
 * 形态："/" 分段的中文分组路径（如 "文件/新建"、"视图/工具"）——段词表
 * ＝§4.1 六菜单（文件/编辑/视图/阶段/工具/帮助）。命令面板按首段分组
 * 显示（§7.4"Tab 补全类别"的分组建模）。
 */
using MenuPath = std::string;

}  // namespace ui
}  // namespace ird
}  // namespace sdurws

#endif  // SDURWS_IRD_UI_UITYPES_HPP
