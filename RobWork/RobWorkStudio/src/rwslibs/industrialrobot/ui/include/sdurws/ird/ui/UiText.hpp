/**
 * @file   UiText.hpp
 * @brief  工程用语文案体系（UiText）——界面文本经文案键解析的唯一出口
 *         （§3.5 文案键体系＋§6.6 工程用语与对象定位，UX-02）。
 *
 * 设计依据：
 *   - units/ui.md §3.5（"UiText::resolve(TextKey, params) 是唯一出口；界面
 *     禁止出现哈希、Schema 版本号、内部插件名"）、§6.6（"界面文本经
 *     UiText::resolve(TextKey, params)；参数中的数值一律带单位显示（core
 *     Quantity::displayValueIn——唯一换算入口）；'不适用'字段显示'不适用'
 *     占位，不伪造 0"）；
 *   - 需求 UX-02（工程用语：数值＋单位、对象定位用局部名、零内部标识）、
 *     ERR-01（"不适用"显式占位，不伪造 0/不缺省）、P-DIAG-9（键/值分离：
 *     键在设计与码表冻结，值归 ui 文案资源）；
 *   - 任务契约 tasks/foundation/UI-T09.json acceptance 2（"一切文本经
 *     UiText::resolve 文案键解析（§3.5/§6.6，数值带单位显示、对象定位经
 *     名称端口局部名）；界面零哈希/Schema/插件名（UX-02）"）。
 *
 * 背景说明（为什么"键→值"要收口到一个函数）：UX-02 的红线是"用户可见
 * 文本零哈希/零 Schema 版本/零内部插件名"。散落的字符串拼接无法审计这条
 * 红线，收口后才有唯一的执行点：数值经 formatQuantityText 强制带单位
 * （core 唯一换算入口）、动态参数过 ensureNoInternalIdentity 守卫（摘要
 * 十六进制形态拒绝进参数）、"不适用"统一占位文案。键/值分离（P-DIAG-9）
 * 让翻译资源替换不牵动代码：本实现的内建中文表是**过渡承载**（UI-T03
 * 壳内文案表同案——键是冻结契约，迁移到资源文件时只换值源，调用方零改动）。
 *
 * 对象定位（§6.6 第二分句）：对象身份到呈现名的换算不经本头——统一经
 * C-11 名称端口 IUiNameResolver::resolveObjectId（UiPorts.hpp，L5 适配
 * runtime），取得 localName 后才可作为参数进入 resolveText；R-4 禁止
 * 拼接/剥离名称前缀。本头的 ensureNoInternalIdentity 是最后一道网：
 * 万一上游把内容身份（SHA-256 十六进制形态）当参数传入，在呈现边界
 * fail-fast 而不是渲染给用户。
 *
 * 线程安全：全部纯/只读函数（内建表编译期固定），可重入。
 */

#ifndef SDURWS_IRD_UI_UITEXT_HPP
#define SDURWS_IRD_UI_UITEXT_HPP

#include <charconv>      // std::to_chars/chars_format（数值确定性文本化——locale 无关）
#include <stdexcept>     // std::invalid_argument（fail-fast 异常载体）
#include <string>
#include <system_error>  // std::errc（to_chars 错误码）
#include <vector>

#include <sdurws/ird/core/Units.hpp>   // core::Quantity/UnitToken（§6.6 数值带单位——core 唯一换算入口）
#include <sdurws/ird/ui/UiTypes.hpp>   // TextKey（§3.5 文案键——键半区）

namespace sdurws {
namespace ird {
namespace ui {

// =====================================================================
// 键解析（§3.5 唯一出口）
// =====================================================================

/**
 * @brief 解析文案键为用户可见文本（§3.5 "UiText::resolve(TextKey) 是唯一
 *        出口"的无参形态）。
 *
 * 键约定（§3.5 冻结族）："cmd.<id>.title"、"stage.<id>.title"、
 * "state.<token>.label"、"diag.<code-lower>.title/detail" 等。值取自内建
 * 中文过渡表（键冻结、值过渡承载——文件头注释；P-DIAG-9 交接资源文件后
 * 仅换值源）。
 *
 * @param key [in] 文案键（§3.5 冻结词形；必须已在内建表登记）
 * @return 中文文本（UTF-8；与键一一对应——NFR-COR-02 确定性，同键同文）
 *
 * @throws std::invalid_argument 键未登记（调用方拼写错误/漏登记＝调用方
 *         契约违约，fail-fast 而不是回显键名或返回空串——静默降级会把
 *         "键名/空白"渲染给用户，违反 UX-02 工程用语纪律且掩盖缺陷）
 *
 * @note UI 线程调用（§3.4 M-1）；查表为线性扫描（表规模＝几十行，预算内
 *       NFR-PERF-01）。
 */
std::string resolveText(const TextKey& key);

/**
 * @brief 解析文案键并做位置参数替换（§3.5/§6.6 "UiText::resolve(TextKey,
 *        params)" 的完整形态）。
 *
 * 值文本中的占位符形如 {0}、{1}（位置序号），替换为 args 对应位置的
 * 已渲染参数。参数必须**先渲染后传入**：数值参数经 formatQuantityText
 * （带单位）取得、对象名经 C-11 名称端口局部名取得——本函数不再做任何
 * 数值换算或名称解析（单一职责：替换与守卫）。
 *
 * 替换纪律（调用方契约，违约即 fail-fast）：
 *   - 值文本中的每个 {k} 必须满足 k < args.size()（引用越界＝文案表与
 *     调用方参数个数不齐）；
 *   - args 的每个位置 i 必须在值文本中出现 {i}（传了没用的参数＝调用方
 *     误传——同一文本的参数面必须与调用点一致，防止"改文案忘改参数"
 *     的静默漂移）。
 *
 * @param key  [in] 文案键（必须已登记，同 resolveText）
 * @param args [in] 已渲染参数（每个元素先过内部身份守卫——见
 *             ensureNoInternalIdentity；数值参数应来自 formatQuantityText）
 * @return 替换后的中文文本（UTF-8）
 *
 * @throws std::invalid_argument 键未登记、占位符越界、参数冗余、或任一
 *         参数含内部身份形态（哈希十六进制——UX-02 红线在呈现边界拦截）
 *
 * @note UI 线程调用；O(键文长＋参数总长)。
 */
std::string resolveText(const TextKey& key, const std::vector<std::string>& args);

// =====================================================================
// 数值＋单位显示（§6.6——core 唯一换算入口）
// =====================================================================

/**
 * @brief 量值的"数值＋单位"显示文本（§6.6 "参数中的数值一律带单位显示"
 *        的唯一格式化点）。
 *
 * 换算走 core::Quantity::displayValueIn——**唯一换算入口**（§6.6 原文；
 * KIN-12：显示单位只是投影，不改 SI 真值、不进任何身份计算）。数值文本
 * ＝6 位有效数字 general 格式（std::to_chars，与 locale 无关——与
 * FormEditCommon/PolicySummaryCard 同一确定性口径，NFR-COR-02），后接
 * 一个空格与显示单位 token（如 "25.4 mm"、"4.5 N*m"——token 为 core 冻结
 * 词表原文）。
 *
 * @tparam K   量纲种类（编译期隔离——Torque 与 Force 不可混用，DYN-03）
 * @param quantity [in] SI 真值量值（core 强类型量——不收裸 double，
 *                 避免"忘了带单位语义"的调用面）
 * @param displayUnit [in] 显示单位（core 注册表 token；量纲必须与 K 一致）
 * @return "数值 单位" 文本（如 "25.4 mm"；数值与单位之间一个空格）
 *
 * @throws core::CoreError displayUnit 量纲与 K 不匹配（调用方装配错误——
 *         fail-fast，与 core displayValueIn 的错误口径一致）
 *
 * @note UI 线程调用；无堆分配热点（单次字符串组装）。
 */
template <core::QuantityKind K>
std::string formatQuantityText(const core::Quantity<K>& quantity,
                               core::UnitToken displayUnit)
{
    // 第 1 步：显示投影换算（core 唯一换算入口——量纲违约在此上抛，
    // ui 不吞不改 core 错误语义）。
    const double display = quantity.displayValueIn(displayUnit);
    // 第 2 步：确定性数值文本化（6 位有效数字 general、locale 无关——
    // 与 FormEditCommon displayNumber 同口径，全产品一个精度纪律）。
    char buffer[40];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), display,
                                      std::chars_format::general, 6);
    // 可达性：double 全值域在 40 字节内恒可容纳（general 格式）——
    // 与 FormEditCommon 同款防御，失败即调用方环境违约，fail-fast。
    if (result.ec != std::errc()) {
        throw std::invalid_argument("ui/uitext/number-format: 数值文本化失败");
    }
    // 第 3 步：数值＋空格＋单位 token（§6.6 "一律带单位显示"——单位是
    // 数值的一部分，不是可省略后缀；token 为 core 冻结词表原文）。
    std::string text(buffer, static_cast<std::size_t>(result.ptr - buffer));
    text += ' ';
    text += displayUnit.symbol();
    return text;
}

/**
 * @brief "不适用"占位文本（§6.6/"ERR-01"：不适用字段显示"不适用"占位，
 *        不伪造 0）。
 *
 * 为什么是函数而不是调用方字面量：占位措辞是工程用语契约的一部分（与
 * "未设值不伪造 0"、显式不适用呈现同一纪律），收口后措辞调整只动一处，
 * 且测试可对全产品呈现层断言"零伪造 0"。
 *
 * @return "不适用"（UTF-8）
 */
std::string notApplicableText();

// =====================================================================
// UX-02 内部身份守卫（零哈希/Schema/插件名的呈现边界执行点）
// =====================================================================

/**
 * @brief 拒绝内部身份形态文本进入用户可见面（UX-02 "界面禁止出现哈希、
 *        Schema 版本号、内部插件名"的参数边界执行点）。
 *
 * 可机械执行的形态＝**摘要十六进制**：连续 64 个十六进制字符（SHA-256
 * 摘要的呈现形态——CON-05 内容寻址身份在本产品的唯一哈希形态）。命中即
 * 抛：内容身份属于证据链内部关联键（ARC-04/CON-05），绝无呈现语义；把
 * 摘要当参数传入resolveText 是装配/接线错误，fail-fast 比渲染给用户或
 * 静默吞掉都正确（AGENTS §3 错误语义：调用方错误 fail-fast）。
 *
 * Schema 版本号与内部插件名没有可机械判定的通用词形（"v3"是合法数值、
 * 域名段可任意命名），其纪律由两道既有面承担：①呈现文本只能出自内建
 * 文案表＋已解析局部名（键值分离让自由文本无处产生）；②单元测试对内建
 * 表做全表零内部名断言（StageNavigationModelTest 的具名用例——UI-STG-1
 * "零内部名断言"）。本函数覆盖其中可机械化的哈希形态。
 *
 * @param text [in] 待检文本（resolveText 参数路径自动调用；亦可独立用于
 *             其它呈现组装点的防御性自检）
 *
 * @throws std::invalid_argument 文本含 64 连续十六进制字符（摘要形态）
 */
void ensureNoInternalIdentity(const std::string& text);

/**
 * @brief 内建文案表的键清单（只读盘点面——测试零内部名断言与工具核对用）。
 *
 * @return 当前内建表登记的全部键（各族拼接序——七阶段标题、七态标签、
 *         九态标签、「无法判定」原因、门控不可用；同键多族登记只出现
 *         一次，如 state.failed.label）
 *
 * @note 值不在此暴露——键/值分离纪律（P-DIAG-9）下，值只能经 resolveText
 *       取得（唯一出口），盘点面只开键半区。
 */
std::vector<TextKey> registeredTextKeys();

}  // namespace ui
}  // namespace ird
}  // namespace sdurws

#endif  // SDURWS_IRD_UI_UITEXT_HPP
