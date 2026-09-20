/**
 * @file   RenderText.hpp
 * @brief  reporting 单元内部共享的"工程判定 token ↔ 中文显示名"映射表
 *         （私有实现头——不入 include/，R-2 纪律）。
 *
 * 设计依据：
 *   - units/reporting.md §6.6（"工程判定→结论列"的呈现列——token 形态见
 *     core::toToken，AT-22 比对维度用 token，正文呈现用本表）、§8.5
 *     （AT-22 逐字段一致性——status 维度的比对载体）
 *   - 任务契约 tasks/foundation/RPT-T07.json acceptance 3（七元组逐字段
 *     比对——status 维度）
 *
 * 背景说明（为什么抽成本头）：
 *   RPT-T06 渲染侧（Render.cpp）把矩阵单元格的 status token 映射为中文
 *   工程用语写入 HTML 状态列；RPT-T07 一致性检查器回读 HTML 时需要把该
 *   显示名**反查**回 token 才能与矩阵单元格逐字符比对。两张方向必须出自
 *   同一张表（值单源——§8.4 同纪律在呈现映射上的延伸）：若检查器自抄
 *   一份反向表，措辞冻结任务（RPT-T08）调整呈现词时两表会失步，一致性
 *   检查将产生"假不一致"。
 *
 *   本头是 Render.cpp（正向）与 Consistency.cpp（反向）的唯一共享点；
 *   表内容冻结于本文件，变更走单元卡 §14.4 登记。
 *
 * 线程约束：全部为纯函数（无共享可变状态）——并发安全。
 */
#ifndef SDURWS_IRD_REPORTING_RENDERTEXT_HPP
#define SDURWS_IRD_REPORTING_RENDERTEXT_HPP

#include <string>
#include <string_view>

namespace sdurws::ird::reporting::detail {

/// token↔显示名对照行（表驱动——正向/反向共用同一数据，杜绝双表失步）。
struct StatusDisplayEntry {
    std::string_view statusToken;   ///< core::toToken(EngineeringStatus) 词面
    std::string_view displayName;   ///< §6.6"报告内呈现"列的中文工程用语
};

/// 对照表（§6.6 呈现列逐项——四值全枚举；词面与 core token 冻结一致）。
inline constexpr StatusDisplayEntry kStatusDisplayTable[] = {
    {"feasible",               "可行"},
    {"engineering-infeasible", "工程不可行"},
    {"data-insufficient",      "数据不足"},
    {"not-applicable",         "不适用"},
};

/**
 * @brief 工程判定 token → 中文显示名（HTML 状态列渲染用——RPT-T06 正向）。
 *
 * 未知 token 回退原样呈现（不伪造中文文案——与 RPT-T06 落位行为逐字符
 * 一致，本头抽取属等价重构）。
 *
 * @param statusToken [in] 矩阵单元格 status 词面（core::toToken 产物）
 * @return 显示名（UTF-8）；未知 token 原样返回
 *
 * 复杂度：O(表长)（4 项线性扫描——量级常数）。
 */
inline std::string statusTokenToDisplay(std::string_view statusToken)
{
    for (const StatusDisplayEntry& entry : kStatusDisplayTable) {
        if (entry.statusToken == statusToken) {
            return std::string(entry.displayName);
        }
    }
    return std::string(statusToken);
}

/**
 * @brief 中文显示名 → 工程判定 token（HTML 状态列回读反查——RPT-T07 反向）。
 *
 * 与 statusTokenToDisplay 互为逆映射（同一张表——见文件头"为什么"）：
 * 对任意 token t，displayToToken(tokenToDisplay(t)) == t（未知 token 的
 * 恒等回退两侧对称，逆性仍成立）。
 *
 * @param display [in] HTML 状态列回读文本（已反转义）
 * @return status token 词面；非表内显示名原样返回（与正向恒等回退对称）
 *
 * 复杂度：O(表长)。
 */
inline std::string statusDisplayToToken(std::string_view display)
{
    for (const StatusDisplayEntry& entry : kStatusDisplayTable) {
        if (entry.displayName == display) {
            return std::string(entry.statusToken);
        }
    }
    return std::string(display);
}

}  // namespace sdurws::ird::reporting::detail

#endif  // SDURWS_IRD_REPORTING_RENDERTEXT_HPP
