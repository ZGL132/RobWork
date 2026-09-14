/**
 * @file   ParamSchema.hpp
 * @brief  私有实现头：paramSchema 受限 JSON 数形的单一解析点。
 *
 * 设计依据：
 *   - units/diagnostics.md §4.5（CodeDescriptor.paramSchema："受限 JSON 形
 *     schema token 列表"——注册期验证"paramSchema 非空且参数名合法"；§9.2
 *     工厂"实例的 context/cause/comparison 须按模式填充（工厂校验占位一致）"
 *     ——DiagContext.params 键集与 schema 参数名集的一致性校验）
 *   - 任务契约 tasks/foundation/DIAG-T04.json（≙WP-09-T04）：工厂侧占位
 *     一致性校验需要按名提取 schema 参数清单——DIAG-T03 的注册期验证
 *     （DiagCodes.cpp validateParamSchema）与本任务的工厂校验共用同一文法
 *     判定，避免同文法两实现漂移（NFR-MNT-04 精神；单元内私有共享，
 *     不跨单元暴露——R-2：本头位于 src/，不入公共 include）
 *
 * 文法（与 DIAG-T03 登记一致，冻结）：接受且仅接受 `[]`（无参数的显式声明）
 * 或 `["name","name",...]`；括号/逗号外侧允许空白；字符串内不允许转义/空白
 * （参数名词形本身不含这些字符，出现即非法）；参数名重复非法。
 */

#ifndef SDURWS_IRD_DIAGNOSTICS_SRC_PARAMSCHEMA_HPP
#define SDURWS_IRD_DIAGNOSTICS_SRC_PARAMSCHEMA_HPP

#include <string>
#include <string_view>
#include <vector>

namespace sdurws::ird::diagnostics::detail {

/**
 * @brief 解析 paramSchema 文本为有序参数名清单（解析单点——注册期验证与
 *        工厂占位一致性校验共用）。
 *
 * @param schema [in] 模式文本（注册期已验证/待验证均可——本函数即判定原语）
 * @param names  [out] 成功时的参数名清单（声明序——占位校验只比较集合，
 *               序保留供诊断 detail 使用；失败时被清空）
 * @param error  [out] 失败原因文本（detail 用，指明首个违约点；成功时清空）
 * @return true＝文法合法（names/error 分别置为清单/空）；false＝非法
 *         （names 置空、error 指明字段与原因）
 *
 * 纯函数；确定性（NFR-COR-02）；不抛异常（调用方处于验证链——异常轨留给
 * 上层的 DiagnosticsError 组装）。
 */
bool tryParseParamSchema(const std::string& schema, std::vector<std::string>* names,
                         std::string* error);

}  // namespace sdurws::ird::diagnostics::detail

#endif  // SDURWS_IRD_DIAGNOSTICS_SRC_PARAMSCHEMA_HPP
