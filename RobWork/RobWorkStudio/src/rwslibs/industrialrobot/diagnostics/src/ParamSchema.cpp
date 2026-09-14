/**
 * @file   ParamSchema.cpp
 * @brief  paramSchema 受限 JSON 数形的单一解析点实现。
 *
 * 设计依据：
 *   - units/diagnostics.md §4.5（paramSchema 字段约束："非空且参数名合法"；
 *     §9.2 工厂占位一致性校验）、§4.5 示例 ["pid","host"]（参数名词形锚点）
 *   - 任务契约 tasks/foundation/DIAG-T04.json（≙WP-09-T04）：工厂校验占位
 *     一致性需要按名提取参数清单——解析单点自 DIAG-T03 的 DiagCodes.cpp
 *     内联实现迁入本文件（行为逐字保持，既有 DT-REG-5 用例为回归保护；
 *     单元内私有共享，不跨单元暴露——R-2）
 *
 * 文法（冻结，见 src/ParamSchema.hpp 头注）：`[]` 或 `["name","name",...]`；
 * 括号/逗号外侧允许空白；字符串内不允许转义/空白；参数名重复非法。
 *
 * 线程安全：纯函数（无共享状态）。
 */

#include "ParamSchema.hpp"

#include <cctype>

namespace sdurws::ird::diagnostics::detail {
namespace {

/// 参数名词形：^[a-z0-9]+(-[a-z0-9]+)*$（§4.5 示例 ["pid","host"]——小写
/// kebab，与诊断码同族词形的小写变体）。
bool isValidParamName(std::string_view name)
{
    if (name.empty() || !std::isalnum(static_cast<unsigned char>(name.front()))) {
        return false;
    }
    bool prevDash = false;
    for (const char ch : name) {
        if (ch == '-') {
            if (prevDash) {
                return false;   // 连续连字符非法
            }
            prevDash = true;
            continue;
        }
        if (!std::isalnum(static_cast<unsigned char>(ch))) {
            return false;       // 仅允许小写字母/数字（isalnum 对大写也为真——补查）
        }
        if (std::isupper(static_cast<unsigned char>(ch))) {
            return false;
        }
        prevDash = false;
    }
    return !prevDash;           // 尾连字符非法
}

}  // namespace

bool tryParseParamSchema(const std::string& schema, std::vector<std::string>* names,
                         std::string* error)
{
    // 输出槽先归位（失败/成功两条路径都得到确定状态——调用方无需预清理）。
    names->clear();
    error->clear();

    // ①空模式：paramSchema 为必填字段（"非空且参数名合法"——无参数也须显式
    // 声明 "[]"；完全缺省＝违约，因实例占位一致性校验需要明确的模式边界）。
    if (schema.empty()) {
        *error = "paramSchema 为空（必填字段——无参数也须显式声明 \"[]\"）";
        return false;
    }

    // ②受限 JSON 数形线性扫描（不引第三方 JSON 库——新增依赖须先登记；
    // 手写扫描确定性且零依赖，与 DIAG-T03 登记的判定逐字一致）。
    std::size_t i = 0;
    const auto skipWs = [&i, &schema] {
        while (i < schema.size() && (schema[i] == ' ' || schema[i] == '\t')) {
            ++i;
        }
    };
    skipWs();
    if (i >= schema.size() || schema[i] != '[') {
        *error = "paramSchema 须以 '[' 起始（受限 JSON 数组形）";
        return false;
    }
    ++i;
    skipWs();
    if (i < schema.size() && schema[i] == ']') {
        ++i;   // 空数组分支（显式"无参数"声明）
    } else {
        // 非空数组：逐项解析带引号参数名（词形/重复校验），项间以 ',' 分隔、
        // 以 ']' 收束——首处违约即失败并指明。
        while (true) {
            skipWs();
            if (i >= schema.size() || schema[i] != '"') {
                *error = "paramSchema 参数项须为带引号的参数名";
                return false;
            }
            ++i;
            const std::size_t start = i;
            while (i < schema.size() && schema[i] != '"') {
                ++i;
            }
            if (i >= schema.size()) {
                *error = "paramSchema 参数名字符串未闭合";
                return false;
            }
            const std::string_view name{schema.data() + start, i - start};
            if (!isValidParamName(name)) {
                *error = "paramSchema 参数名词形非法（须 ^[a-z0-9]+(-[a-z0-9]+)*$）: "
                         + std::string{name};
                return false;
            }
            for (const auto& prior : *names) {
                if (prior == name) {
                    *error = "paramSchema 参数名重复: " + std::string{name};
                    return false;
                }
            }
            names->emplace_back(name);
            ++i;   // 跳过闭引号
            skipWs();
            if (i < schema.size() && schema[i] == ',') {
                ++i;
                continue;   // 下一参数项
            }
            if (i < schema.size() && schema[i] == ']') {
                ++i;
                break;      // 数组结束
            }
            *error = "paramSchema 参数项后须为 ',' 或 ']'";
            return false;
        }
    }
    skipWs();
    if (i != schema.size()) {
        *error = "paramSchema 数组结束后存在多余字符";
        return false;
    }
    return true;   // 合法——names＝声明序参数名清单，error 空
}

}  // namespace sdurws::ird::diagnostics::detail
