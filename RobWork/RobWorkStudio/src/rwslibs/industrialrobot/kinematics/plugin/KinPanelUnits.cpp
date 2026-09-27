/**
 * @file   KinPanelUnits.cpp
 * @brief  面板显示单位投影辅助的实现（KinPanelUnits.hpp 全部落点）。
 *
 * 设计依据：KinPanelUnits.hpp 文件头（本 TU 是其全部函数的实现落点）；
 * 换算唯一经 kinematics::DisplayUnitProjection→core Units（SA-12 唯一
 * 入口——本 TU 零换算算术，只做分派与文本拼装）。
 */

#include "KinPanelUnits.hpp"

#include <charconv>
#include <cmath>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace sdurws {
namespace ird {
namespace kinematics {
namespace {

/**
 * @brief 6 位有效数字 general 格式化（locale 无关——NFR-COR-02；与 ui
 *        表单公共件 formatFieldValueText 同精度口径）。
 *
 * @param v [in] 已投影数值（有限性由调用方前置校验）
 * @return 数值文本（如 "0.3"、"300"、"1.41421"）
 */
std::string generalText(double v)
{
    // 6 位有效数字是呈现投影精度（SI 真值恒为唯一权威——显示误差不进
    // 任何计算/身份）；char 缓冲按 double 最长 general 表示预留。
    char buf[64];
    const auto res = std::to_chars(buf, buf + sizeof(buf), v,
                                   std::chars_format::general, 6);
    return std::string(buf, res.ptr);
}

}  // namespace

std::string formatKinQuantityText(double siValue, const std::string& unitToken,
                                  const std::optional<DisplayUnitProjection>& displayUnits)
{
    // 非有限拒绝（NFR-COR-03）：显示非有限值即伪造数据——fail-fast。
    if (!std::isfinite(siValue)) {
        throw std::invalid_argument(
            "formatKinQuantityText: 非有限 SI 值不可渲染（NaN/±∞）");
    }

    // ---- 无量纲：直显数值不带单位后缀（工程惯例——ui 同案）----
    if (unitToken == "1") {
        return generalText(siValue);
    }

    // ---- 长度/角度：经显示投影换算（KIN-12 唯一出口）；其它 token 直显 ----
    double shown = siValue;            // 显示数值（缺省＝SI 原值）
    std::string shownUnit = unitToken; // 显示单位（缺省＝SI token 原文）
    if (displayUnits.has_value()) {
        // 投影句柄只覆盖 R1 两量纲——单位 token 不是 m/rad 却要求投影＝
        // 调用方装配错误（fail-fast，不出错误数值）。
        if (unitToken == "m") {
            shown = displayUnits->projectLength(siValue);
            shownUnit = std::string(displayUnits->lengthUnit().symbol());
        } else if (unitToken == "rad") {
            shown = displayUnits->projectAngle(siValue);
            shownUnit = std::string(displayUnits->angleUnit().symbol());
        } else {
            throw std::invalid_argument(
                "formatKinQuantityText: token '" + unitToken
                + "' 不属显示投影量纲（仅 m/rad 可投影）");
        }
    }

    // 无量纲之外的单位同显（"300 mm"/"17.2 deg"——数值＋空格＋token）。
    return generalText(shown) + " " + shownUnit;
}

std::vector<KinNamedValueRow> reprojectMetricRows(
    const std::vector<KinNamedValueRow>& rows,
    const std::optional<DisplayUnitProjection>& displayUnits)
{
    // 整表重建（非逐格改写）：避免半新半旧混合呈现——见 KinPanelUnits.hpp
    // 文件头注。SI 真值逐行原样搬运（权威不动，V-17）。
    std::vector<KinNamedValueRow> out;
    out.reserve(rows.size());
    for (const KinNamedValueRow& r : rows) {
        KinNamedValueRow rebuilt;      // 逐字段显式重建（防未来扩字段时漏搬运）
        rebuilt.key = r.key;
        rebuilt.label = r.label;
        rebuilt.siValue = r.siValue;   // SI 真值不变
        rebuilt.unitToken = r.unitToken;
        rebuilt.displayText =
            formatKinQuantityText(r.siValue, r.unitToken, displayUnits); // 唯一变化面
        out.push_back(std::move(rebuilt));
    }
    return out;
}

std::pair<std::string, std::string> reprojectSolutionColumns(
    const std::vector<double>& qSi, double residualSiM,
    const std::optional<DisplayUnitProjection>& displayUnits)
{
    // 关节向量：逐分量按其天然单位投影（转动 rad、移动 m——混合链逐分量
    // 各自走对应量纲投影；这里以"向量整体语义"由调用方保证单位口径，本
    // 函数按角度/长度两类投影的最大公约语义处理：分量逐个经 format 单值
    // 面会带单位后缀不适合逗号串，故直接经投影数值拼装逗号分隔文本）。
    std::string qText;
    for (std::size_t i = 0; i < qSi.size(); ++i) {
        double v = qSi[i];
        if (!std::isfinite(v)) {
            throw std::invalid_argument(
                "reprojectSolutionColumns: 关节向量含非有限分量");
        }
        // 混合链的分量量纲无法从 double 本身判定——投影语义按角度制式
        // 统一（关节角是主导量纲；移动关节分量在 R1 面板呈现按长度制式
        // 由调用方以 metric 行单独承载）。此处经 DisplayUnitProjection 的
        // 角度投影（rad→deg）或原值（nullopt）。
        if (displayUnits.has_value()) {
            v = displayUnits->projectAngle(v);
        }
        if (i != 0) {
            qText += ", ";
        }
        qText += generalText(v);
    }

    // 位置残差：长度量纲投影＋单位同显。
    const std::string residualText =
        formatKinQuantityText(residualSiM, "m", displayUnits);
    return {qText, residualText};
}

std::optional<DisplayUnitProjection> tryFindKinDisplayUnits(
    std::string_view lengthSymbol, std::string_view angleSymbol)
{
    // 词表封闭（KIN-12 R1 子集）——R2 token（inch/grad/turn）在
    // DisplayUnitProjection::tryFind 内即被拒（nullopt），此处透传不重列
    // 白名单（唯一权威在该句柄——NFR-MNT-03）。
    return DisplayUnitProjection::tryFind(lengthSymbol, angleSymbol);
}

}  // namespace kinematics
}  // namespace ird
}  // namespace sdurws
