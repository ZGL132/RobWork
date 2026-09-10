/**
 * @file   Compare.cpp
 * @brief  容差比较实现——C4 公式（含非有限拒绝/零参考退化）、逐元素重载、C7 转写。
 *
 * 设计依据：
 *   - units/core.md §4.5（契约表逐行）/§5.5（签名）/D-06（C7 单点转写）
 *   - 任务契约 tasks/foundation/CORE-T05.json（≙WP-03-T05）
 */

#include <sdurws/ird/core/Compare.hpp>

#include <cmath>
#include <stdexcept>
#include <string>

namespace sdurws::ird::core {

bool closeWithin(double value, double reference, Tolerance t) noexcept
{
    // 非有限输入 → false（不抛、不静默通过——NFR-COR-03；noexcept 契约）。
    const bool inputsFinite = value == value && reference == reference
                           && value - value == 0 && reference - reference == 0;
    if (!inputsFinite) {
        return false;
    }
    // C4 公式：|value−reference| ≤ relative·|reference| + absolute。
    // 零参考：|reference|=0 时相对项退化为 0——公式自然退化为纯 absolute（§4.5
    // "不要求精确零"——无需分支特判）。
    const double diff = value - reference;
    const double limit = t.relative * (reference < 0 ? -reference : reference)
                       + t.absolute;
    return (diff <= limit) && (-diff <= limit);
}

bool allCloseWithin(const std::vector<double>& a, const std::vector<double>& b,
                    Tolerance t)
{
    // 长度不等＝契约违约显性化（CoreError，core/compare/length 前缀）——
    // 静默 false 会把形状错误伪装成数值不匹配。
    if (a.size() != b.size()) {
        throw CoreError("core/compare/length: 长度不等（" + std::to_string(a.size())
                        + " vs " + std::to_string(b.size()) + "）");
    }
    // 逐元素应用 C4（防正负抵消——不以差值总和替代，§4.5 明文）。
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (!closeWithin(a[i], b[i], t)) {
            return false;
        }
    }
    return true;
}

#ifdef IRD_CORE_HAS_RW_Q
bool allCloseWithin(const rw::math::Q& a, const rw::math::Q& b, Tolerance t)
{
    if (a.size() != b.size()) {
        throw CoreError("core/compare/length: Q 长度不等（" + std::to_string(a.size())
                        + " vs " + std::to_string(b.size()) + "）");
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (!closeWithin(a[i], b[i], t)) {
            return false;
        }
    }
    return true;
}
#endif

std::optional<double> runtimeAbsoluteTolerance(QuantityKind k) noexcept
{
    // 附录 D C7 机械转写（D-06：core 是唯一转写点；修改只能随需求变更）。
    switch (k) {
    case QuantityKind::Angle:                 return 1e-12;
    case QuantityKind::Length:                return 1e-12;
    case QuantityKind::LinearVelocity:        return 1e-9;
    case QuantityKind::AngularVelocity:       return 1e-9;
    case QuantityKind::LinearAcceleration:    return 1e-9;
    case QuantityKind::AngularAcceleration:   return 1e-9;
    case QuantityKind::Torque:                return 1e-9;
    case QuantityKind::Dimensionless:         return 1e-12;
    // 附录 D 未声明默认的量纲：nullopt（"无默认且无配置来源的量不得引入产品侧
    // 相对校验"——C7 原文语义；mass/time/force/inertia/power/voltage）。
    default:                                  return std::nullopt;
    }
}

}  // namespace sdurws::ird::core
