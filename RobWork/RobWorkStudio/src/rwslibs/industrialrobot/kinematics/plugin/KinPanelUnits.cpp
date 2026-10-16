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

/// 域外无穷的显式标记文本（F-902——按行白名单收紧的拒绝半区呈现词）。
/// 语义：该行的量域契约不含无穷（位置/残差恒有限、旋转矩阵元 ∈[-1,1]、
/// 奇异值/可操作度有限——Fk.hpp PoseMetrics 字段规则/KinTypes.hpp §6.1），
/// siValue=+∞ 即上游双重违约（FK 契约要求有限值却产出无穷）。呈现"非法
/// 值(+∞)"而非静默"∞"：读者一眼可分辨"契约内合法无穷（奇异/∞、无界）"
/// 与"契约外违约值"；不带单位后缀（无穷与量纲投影无意义——合法无穷行
/// 同则）。行集层的标记与制式无关（reprojectMetricRows 对一切 +∞ 行
/// 原样保留 displayText——标记词亦跨制式稳定）。
constexpr const char* kOutOfRangeInfinityText = "非法值(+∞)";

std::string formatKinQuantityText(double siValue, const std::string& unitToken,
                                  const std::optional<DisplayUnitProjection>& displayUnits,
                                  std::optional<std::string_view> infinityText)
{
    // 非有限值三分类（KIN-P3/审查 A03 三分类保留；★ F-902 按行收紧——
    // 修复前对一切 +∞ 返回 infinityText（缺省泛化"∞"），域外位置行
    // siValue=+∞（上游深度违约）被静默放行）：
    //   1. 合法 +∞（行白名单在场）：原样呈现 infinityText 语义词
    //      （"奇异/∞"/"无界"）——禁止截断成有限数字（截断即伪造数据，
    //      比抛异常更失实）；
    //   2. 域外 +∞（白名单缺省）：该量域契约不含无穷——显式"非法值(+∞)"
    //      标记轨（不再静默"∞"）；呈现而非抛异常的原因：显示层异常会
    //      穿越 Qt 槽（A03 教训），且违约的呈现事实比炸掉整表更可诊断；
    //   3. NaN/−∞：任何行的值域都不含该两类 → 上游计算违约，fail-fast
    //      不渲染伪数据（A03 拒绝半区不松动）。
    if (std::isinf(siValue) && siValue > 0.0) {
        // 域内/域外的唯一判定面＝行白名单参数（调用方按 Fk.hpp:108-112/
        // KinTypes.hpp:272-273 的域契约逐行供给——本函数零量表复制，
        // 单一事实源在调用方的行语义）。
        return std::string(infinityText.value_or(kOutOfRangeInfinityText));
    }
    if (!std::isfinite(siValue)) {
        throw std::invalid_argument(
            "formatKinQuantityText: 非有限 SI 值不可渲染（NaN/−∞——上游计算违约）");
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
    // +∞ 行（KIN-P3/审查 A03；F-902 起含两类）的重投影语义：+∞ 的呈现
    // 文本（行白名单语义词"奇异/∞"/"无界"，或域外标记"非法值(+∞)"）是
    // 行构建期按行语义选定的、与单位制式无关——重投影无法从
    // (siValue, unitToken) 再推导它，故原样保留首建 displayText（修复
    // 中途形态曾把 +∞ 行退化成缺省 "∞"——L-K8 单位切换的幂等性要求
    // 语义词跨制式稳定；F-902 的域外标记同则——域外 +∞ 行切制式后仍
    // 显式标记，不退化回数值或泛化"∞"）。NaN/−∞ 行仍走 format 的
    // fail-fast 拒绝面。
    std::vector<KinNamedValueRow> out;
    out.reserve(rows.size());
    for (const KinNamedValueRow& r : rows) {
        KinNamedValueRow rebuilt;      // 逐字段显式重建（防未来扩字段时漏搬运）
        rebuilt.key = r.key;
        rebuilt.label = r.label;
        rebuilt.siValue = r.siValue;   // SI 真值不变
        rebuilt.unitToken = r.unitToken;
        const bool legitInfinity = std::isinf(r.siValue) && r.siValue > 0.0;
        rebuilt.displayText =
            legitInfinity
                ? r.displayText  // +∞ 行：语义词与制式无关——原样保留
                : formatKinQuantityText(r.siValue, r.unitToken, displayUnits);
        out.push_back(std::move(rebuilt));
    }
    return out;
}

double projectJointAxisToDisplay(double siValue, runtime::JointType jointType,
                                 const std::optional<DisplayUnitProjection>& displayUnits)
{
    // 关节向量分量按契约恒有限（求解器/FK 产出——§5.1 q_authoritative）；
    // 非有限即上游违约，fail-fast 不渲染（与 formatKinQuantityText 同纪律）。
    if (!std::isfinite(siValue)) {
        throw std::invalid_argument(
            "projectJointAxisToDisplay: 关节分量非有限（上游计算违约）");
    }
    // Fixed 关节不消耗 q（装配层 dof 提取应已滤除——出现在类型表即调用
    // 方装配错误，fail-fast 而非猜一个量纲继续）。
    if (jointType == runtime::JointType::Fixed) {
        throw std::invalid_argument(
            "projectJointAxisToDisplay: Fixed 关节不消耗 q（逐轴类型表装配错误）");
    }
    // 逐轴天然量纲分派（A05 修复核心）：移动轴走长度投影（m→mm/cm）、
    // 转动/连续轴走角度投影（rad→deg）；nullopt＝SI 直显恒等。
    if (jointType == runtime::JointType::Prismatic) {
        return displayUnits.has_value() ? displayUnits->projectLength(siValue)
                                        : siValue;
    }
    return displayUnits.has_value() ? displayUnits->projectAngle(siValue)
                                    : siValue;
}

std::string formatJointAxisText(double siValue, runtime::JointType jointType,
                                const std::optional<DisplayUnitProjection>& displayUnits)
{
    // 数值半区复用唯一投影判定点（零第二实现——projectJointAxisToDisplay
    // 的量纲分派即本函数的换算语义）。
    const double shown =
        projectJointAxisToDisplay(siValue, jointType, displayUnits);
    // 单位后缀取当前制式对应量纲的 token 原文（移动轴→长度单位、转动/
    // 连续轴→角度单位；nullopt＝SI 直显，用 SI token "m"/"rad"）。
    const bool prismatic = jointType == runtime::JointType::Prismatic;
    const std::string symbol =
        displayUnits.has_value()
            ? std::string(prismatic ? displayUnits->lengthUnit().symbol()
                                    : displayUnits->angleUnit().symbol())
            : std::string(prismatic ? "m" : "rad");
    return generalText(shown) + " " + symbol;
}

std::pair<std::string, std::string> reprojectSolutionColumns(
    const std::vector<double>& qSi, const std::vector<runtime::JointType>& dofJointTypes,
    double residualSiM, const std::optional<DisplayUnitProjection>& displayUnits)
{
    // 逐轴类型表与 q 向量必须同维同链序（KIN-P3/审查 A05 契约面——调用
    // 方从模型关节表经 dofJointTypes 提取；维度失配＝装配错误 fail-fast，
    // 绝不回退"全轴按角度猜"的旧语义——那正是 A05 根因）。
    if (qSi.size() != dofJointTypes.size()) {
        throw std::invalid_argument(
            "reprojectSolutionColumns: 关节向量维度 " + std::to_string(qSi.size())
            + " != 逐轴类型表维度 " + std::to_string(dofJointTypes.size())
            + "（调用方装配错误——类型表须与 q 同链序同维度）");
    }
    // 关节向量：逐分量按其天然量纲投影（转动/连续 rad→角度、移动 m→长度
    // ——projectJointAxisToDisplay 唯一判定点；分量文本为裸数值逗号串，
    // 单位语义由列头与关节表的逐轴"数值 单位"文本承载）。
    std::string qText;
    for (std::size_t i = 0; i < qSi.size(); ++i) {
        const double v =
            projectJointAxisToDisplay(qSi[i], dofJointTypes[i], displayUnits);
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

double resolveKinDisplayToSi(double displayValue,
                             const std::optional<DisplayUnitProjection>& displayUnits)
{
    // 显示→SI 逆换算＝SI 因子直乘（value_si＝value×factor——core Units
    // §4.4 因子表语义，mm→0.001/cm→0.01/m→1；与 Quantity::make 同一
    // 算术，非第二换算实现）。nullopt＝SI 直显制式，恒等返回。
    // 审查 A07 修复：修复前 widget 用 projectLength（SI→显示正向）处理
    // 显示值——方向反转（mm 输入 0.1 → 100 m）；提交面的正确方向是
    // 因子直乘（mm 输入 0.1 → 1e-4 m）。目标输入恒为长度轴（位置 XYZ
    // ——单位随长度制式；角度制式不参与位置分量，§5.3 目标＝基座系 m）。
    return displayUnits.has_value()
               ? displayValue * displayUnits->lengthUnit().siFactor()
               : displayValue;
}

double resolveKinAngleDisplayToSi(double displayValue,
                                  const std::optional<DisplayUnitProjection>& displayUnits)
{
    // 角度量纲的显示→SI 逆换算（★ KIN-R2 任务4——目标姿态 RPY 编辑的
    // 提交面；与长度版 resolveKinDisplayToSi 同一算术、另一量纲——
    // deg→π/180、rad→1；角度制式不参与位置分量，本函数只服务姿态分量）。
    // 产出恒为 SI rad（不是度）——与 IkRequest 姿态容差/姿态残差同口径。
    return displayUnits.has_value()
               ? displayValue * displayUnits->angleUnit().siFactor()
               : displayValue;
}

}  // namespace kinematics
}  // namespace ird
}  // namespace sdurws
