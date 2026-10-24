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
#include <boost/multiprecision/cpp_bin_float.hpp>
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
    // REUSE-T21 / GAP-CORE-C4-01 R3：只有两侧同时溢出才会丢失原比较的排序信息。
    // 先保留普通路径的既定舍入；聚合构造的非法tolerance也继续走原行为，
    // 不借数值修复收窄工厂以外的公开接口。有限值前置已由上方检查保证。
    if (std::isinf(diff) && std::isinf(limit) && limit > 0.0
        && t.relative >= 0.0 && t.absolute >= 0.0
        && std::isfinite(t.relative) && std::isfinite(t.absolute)) {
        using Product = boost::multiprecision::number<
            boost::multiprecision::cpp_bin_float<128,
                boost::multiprecision::digit_base_2, void>,
            boost::multiprecision::et_off>;
        using Wide = boost::multiprecision::number<
            boost::multiprecision::cpp_bin_float<4224,
                boost::multiprecision::digit_base_2, void>,
            boost::multiprecision::et_off>;
        // binary64两数乘积最多106位；先在128位库类型相乘，再无损扩大。
        // 直接做4224位乘法会进入库的Karatsuba临时分配路径；这里不改库或写新乘法。
        const Product product = Product(t.relative) * Product(std::fabs(reference));
        // 有限binary64为2^-1074的整数倍；乘加量子2^-2148且绝对值<2^2049，
        // 最多4197位。4224位足够精确表达原公式，包含次正规绝对项，无需除二。
        // 固定Allocator=void的有界转换/加减不申请堆空间；本分支的指数范围
        // 也远在库类型范围内，不引入分配失败、指数溢出或吞异常的替代行为。
        const Wide wideDiff = boost::multiprecision::abs(Wide(value) - Wide(reference));
        const Wide wideLimit = Wide(product) + Wide(t.absolute);
        return wideDiff <= wideLimit;
    }
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
