/**
 * @file   Units.hpp
 * @brief  数量、单位与数值基础契约——QuantityKind/UnitToken 注册表/唯一换算入口/
 *         Quantity<K> 强类型量。
 *
 * 设计依据：
 *   - units/core.md §4.4（量纲枚举、R1 单位表、换算接口与错误口径、Quantity<K>）、
 *     §5.4（签名）、§8 UT-UNIT（往返/量纲/溢出/非有限/表完整性）
 *   - 需求 NFR-COR-03（不静默转 0——非有限/溢出显式拒绝）、KIN-12（显示单位仅投影，
 *     切换不触发重算）、DYN-03（类型化广义力——Torque/Force 类型层区分）、SEL-02
 *   - SA-12：本头是单位换算的**唯一入口**（rw::math 聚合类型不包装，其数值一律 SI
 *     真值——显示层换算由调用方对本函数逐元素施加，不另设第二个实现点）
 *   - 任务契约 tasks/foundation/CORE-T04.json（≙WP-03-T04，UT-UNIT 载体）
 *
 * R1 单位表（冻结；R2 扩展如 inch/grad/turn 为向后兼容追加，随 KIN-12-S1 登记）：
 *   token 全 ASCII、区分大小写、冻结后不改名（持久化契约）。
 *
 * 确定性：换算＝各一次乘除（to_si=v*f(from)；out=to_si/f(to)）——不承诺位往返
 * 精确（0.01 无二进制精确表示），测试按相对容差断言（UT-UNIT）。
 * 线程安全：注册表编译期冻结只读；UnitToken/Quantity 纯值。
 */

#ifndef SDURWS_IRD_CORE_UNITS_HPP
#define SDURWS_IRD_CORE_UNITS_HPP

#include <cstdint>
#include <optional>
#include <string_view>

#include <sdurws/ird/core/Errors.hpp>

namespace sdurws::ird::core {

/// 量纲类别（§4.4 冻结 14 类——不设温度/电流/货币：R1/R2 无消费者）。
enum class QuantityKind {
    Length,               ///< 长度（m）
    Angle,                ///< 角度（rad）
    Mass,                 ///< 质量（kg）
    Time,                 ///< 时间（s）
    Force,                ///< 力（N）
    Torque,               ///< 力矩（N·m）
    Inertia,              ///< 转动惯量（kg·m²）
    Power,                ///< 功率（W）
    LinearVelocity,       ///< 线速度（m/s）
    AngularVelocity,      ///< 角速度（rad/s）
    LinearAcceleration,   ///< 线加速度（m/s²）
    AngularAcceleration,  ///< 角加速度（rad/s²）
    Voltage,              ///< 电压（V）
    Dimensionless,        ///< 无量纲（1）
};

/**
 * @brief 已注册单位的值句柄（§5.4：tableIndex 未注册＝0xFFFF 无效）。
 *
 * 只能经 find() 获得——保证运行期持有的句柄必指向注册表有效项（前置"已注册"
 * 由构造路径保证，成员函数无需重复判空）。
 * 线程安全：纯值。
 */
class UnitToken {
public:
    /// 按冻结 token 查注册表；未知符号返回 nullopt（io 目录单位标签校验入口）。
    static std::optional<UnitToken> find(std::string_view symbol) noexcept;

    /// 冻结 token 原文（持久化契约——不改名）。
    std::string_view symbol() const noexcept;
    /// 所属量纲。
    QuantityKind kind() const noexcept;
    /// SI 因子：value_si = value × factor。
    double siFactor() const noexcept;
    bool operator==(UnitToken o) const noexcept { return tableIndex_ == o.tableIndex_; }
    bool operator!=(UnitToken o) const noexcept { return !(*this == o); }
    /// 句柄有效（已注册）——默认构造/越权构造的句柄为 false；convert 前置的自检面。
    bool isValid() const noexcept { return tableIndex_ != 0xFFFF; }

private:
    friend class UnitRegistryAccess;
    std::uint16_t tableIndex_ = 0xFFFF;   ///< 注册表下标；0xFFFF＝无效（未注册）
};

// ---- 注册表只读访问（detail：UnitToken 成员与自由函数共用，非公共契约） ----
namespace detail {
std::string_view unitSymbolAt(std::uint16_t index) noexcept;
QuantityKind unitKindAt(std::uint16_t index) noexcept;
double unitFactorAt(std::uint16_t index) noexcept;
}  // namespace detail

/// §4.4 自由函数形态：kindOf(u)／siFactor(u)（前置 u 已注册——经 find 取得即满足）。
QuantityKind kindOf(UnitToken u) noexcept;
double siFactor(UnitToken u) noexcept;

/**
 * @brief 换算（抛出轨迹）：out = v×f(from)/f(to)，各一次乘除。
 *
 * @throws CoreError 四种情形（§4.4 错误口径表）：from/to 未注册
 *         （"core/units/unregistered:"）、量纲不匹配（"core/units/dimension:"）、
 *         v 非有限、结果溢出非有限（后两者 "core/units/convert:"）
 */
double convert(double v, UnitToken from, UnitToken to);

/// 换算 try 轨：任一错误情形返回 nullopt，不抛。
std::optional<double> tryConvert(double v, UnitToken from, UnitToken to) noexcept;

/**
 * @brief 强类型量：SI 真值持有＋类型层量纲隔离（DYN-03——Torque 与 Force 不可互赋）。
 *
 * @tparam K 编译期量纲（类型层消除单位不匹配）
 * 显示单位只是投影：displayValueIn() 不改变 SI 真值（KIN-12——切换显示单位不触发
 * 重算、不产生修订）。operator== 为逐值精确相等（工程数值比较请用 Compare 系列）。
 */
template <QuantityKind K>
struct Quantity {
    /// 由 SI 真值构造；非有限拒绝（抛 "core/units/non-finite:"）。
    static Quantity fromSi(double v)
    {
        if (!isFinite(v)) {
            throw CoreError("core/units/non-finite: SI 值必须有限（NaN/±Inf 拒绝）");
        }
        Quantity q;
        q.si_ = v;
        return q;
    }

    /// fromSi 的 try 轨。
    static std::optional<Quantity> tryFromSi(double v) noexcept
    {
        if (!isFinite(v)) { return std::nullopt; }
        Quantity q;
        q.si_ = v;
        return q;
    }

    /**
     * @brief 由"数值＋单位"构造：unit 的量纲必须等于 K 且 v 有限才成功。
     *
     * 用例：目录导入 Torque::tryParse(4.5, *UnitToken::find("N*m"))；
     * kind 不符（如喂 "N" 给 Torque）＝类型误用在解析边界失败。
     */
    static std::optional<Quantity> tryParse(double v, UnitToken unit) noexcept
    {
        if (!isFinite(v)) { return std::nullopt; }
        if (unit.kind() != K) { return std::nullopt; }
        Quantity q;
        q.si_ = v * unit.siFactor();
        return q;
    }

    /// SI 真值（唯一权威数值；显示单位只是投影）。
    double siValue() const noexcept { return si_; }

    /**
     * @brief 显示投影：按单位 u 换算的显示数值（不改 SI 真值——KIN-12）。
     * @throws CoreError u 的量纲与 K 不匹配
     */
    double displayValueIn(UnitToken u) const
    {
        if (u.kind() != K) {
            throw CoreError("core/units/dimension: displayValueIn 单位量纲与本量不匹配");
        }
        return convert(si_, siUnit(), u);
    }

    /// 逐值精确相等（工程比较用 Compare 系列——附录 D 容差）。
    bool operator==(Quantity o) const noexcept {
        return si_ == o.si_;
    }
    bool operator!=(Quantity o) const noexcept { return !(*this == o); }

private:
    static bool isFinite(double v) noexcept { return v == v && v - v == 0; }   // NaN/±Inf 检测（无 std 依赖面）
    static UnitToken siUnit() noexcept;                                        // K 对应的 SI 单位（实现在 .cpp）
    double si_ = 0.0;                                                          ///< SI 真值
};

// ---- 十四个强类型别名（§4.4 别名清单——Torque/Force 等互不可赋值） ----
using Length = Quantity<QuantityKind::Length>;
using Angle = Quantity<QuantityKind::Angle>;
using Mass = Quantity<QuantityKind::Mass>;
using Time = Quantity<QuantityKind::Time>;
using Force = Quantity<QuantityKind::Force>;
using Torque = Quantity<QuantityKind::Torque>;
using Inertia = Quantity<QuantityKind::Inertia>;
using Power = Quantity<QuantityKind::Power>;
using LinearVelocity = Quantity<QuantityKind::LinearVelocity>;
using AngularVelocity = Quantity<QuantityKind::AngularVelocity>;
using LinearAcceleration = Quantity<QuantityKind::LinearAcceleration>;
using AngularAcceleration = Quantity<QuantityKind::AngularAcceleration>;
using Voltage = Quantity<QuantityKind::Voltage>;
using Scalar = Quantity<QuantityKind::Dimensionless>;

}  // namespace sdurws::ird::core

#endif  // SDURWS_IRD_CORE_UNITS_HPP
