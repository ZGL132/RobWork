/**
 * @file   Units.cpp
 * @brief  单位注册表（R1 冻结 17 token）＋UnitToken/Quantity 实现＋唯一换算入口。
 *
 * 设计依据：
 *   - units/core.md §4.4（R1 单位表：SI 因子表逐项）/§5.4（签名）/§8 UT-UNIT
 *   - SA-12：全产品唯一换算实现点；任务契约 tasks/foundation/CORE-T04.json
 *
 * 注册表实现：编译期常量数组（symbol/kind/factor 三列并行）——冻结表只读，
 * 无运行期注册（R2 扩展＝追加数组行＋重新编译，向后兼容）。
 */

#include <sdurws/ird/core/Units.hpp>

#include <cmath>
#include <stdexcept>
#include <string>

namespace sdurws::ird::core {

namespace {

/// 注册表行（symbol/kind/SI 因子——§4.4 R1 表逐行；token 冻结不改名）。
struct UnitRow {
    std::string_view symbol;
    QuantityKind kind;
    double factor;
};

/// R1 全量 17 条（覆盖 14 量纲；PI 精度足够 rad/deg 换算的 1e-12 相对容差）。
constexpr UnitRow kUnitTable[] = {
    // length
    {"m",       QuantityKind::Length,       1.0},
    {"cm",      QuantityKind::Length,       0.01},
    {"mm",      QuantityKind::Length,       0.001},
    // angle
    {"rad",     QuantityKind::Angle,        1.0},
    {"deg",     QuantityKind::Angle,        3.14159265358979323846 / 180.0},
    // mass / time
    {"kg",      QuantityKind::Mass,         1.0},
    {"s",       QuantityKind::Time,         1.0},
    // force / torque / inertia / power
    {"N",       QuantityKind::Force,        1.0},
    {"N*m",     QuantityKind::Torque,       1.0},
    {"kg*m^2",  QuantityKind::Inertia,      1.0},
    {"W",       QuantityKind::Power,        1.0},
    // velocities / accelerations
    {"m/s",     QuantityKind::LinearVelocity,        1.0},
    {"rad/s",   QuantityKind::AngularVelocity,       1.0},
    {"m/s^2",   QuantityKind::LinearAcceleration,    1.0},
    {"rad/s^2", QuantityKind::AngularAcceleration,   1.0},
    // voltage / dimensionless
    {"V",       QuantityKind::Voltage,      1.0},
    {"1",       QuantityKind::Dimensionless, 1.0},
};

constexpr std::uint16_t kInvalidIndex = 0xFFFF;

/// 统一错误出口（§4.4 错误口径表的三段前缀）。
[[noreturn]] void fail(const char* prefix, const std::string& detail)
{
    throw CoreError(std::string{prefix} + detail);
}

}  // namespace

// ---- detail 注册表访问（Units.hpp detail 声明的实现） ----
namespace detail {
std::string_view unitSymbolAt(std::uint16_t index) noexcept
{
    return kUnitTable[index].symbol;
}
QuantityKind unitKindAt(std::uint16_t index) noexcept
{
    return kUnitTable[index].kind;
}
double unitFactorAt(std::uint16_t index) noexcept
{
    return kUnitTable[index].factor;
}
}  // namespace detail

// ---- UnitToken 成员 ----
std::optional<UnitToken> UnitToken::find(std::string_view symbol) noexcept
{
    // 线性扫描（17 条常量表；编译期冻结——无哈希表必要）。
    for (std::uint16_t i = 0; i < sizeof(kUnitTable) / sizeof(kUnitTable[0]); ++i) {
        if (kUnitTable[i].symbol == symbol) {
            UnitToken u;
            u.tableIndex_ = i;
            return u;
        }
    }
    return std::nullopt;   // 未知符号（try 语义——调用方决定违约处理）
}

std::string_view UnitToken::symbol() const noexcept { return detail::unitSymbolAt(tableIndex_); }
QuantityKind UnitToken::kind() const noexcept { return detail::unitKindAt(tableIndex_); }
double UnitToken::siFactor() const noexcept { return detail::unitFactorAt(tableIndex_); }

// ---- 自由函数形态（§4.4） ----
QuantityKind kindOf(UnitToken u) noexcept { return u.kind(); }
double siFactor(UnitToken u) noexcept { return u.siFactor(); }

// ---- 换算（唯一入口，SA-12） ----
std::optional<double> tryConvert(double v, UnitToken from, UnitToken to) noexcept
{
    // 无效句柄＝未注册（"仅来自未走 find 的调用方违约"）。
    if (!from.isValid() || !to.isValid()) {
        return std::nullopt;
    }
    // 量纲不匹配（如 N*m→N）。
    if (from.kind() != to.kind()) {
        return std::nullopt;
    }
    // 输入非有限。
    if (std::isnan(v) || std::isinf(v)) {
        return std::nullopt;
    }
    // 各一次乘除（§4.4 数值行为）。
    const double out = (v * from.siFactor()) / to.siFactor();
    // 结果溢出为非有限（如 DBL_MAX mm→m）。
    if (std::isnan(out) || std::isinf(out)) {
        return std::nullopt;
    }
    return out;
}

double convert(double v, UnitToken from, UnitToken to)
{
    if (!from.isValid() || !to.isValid()) {
        fail("core/units/unregistered: ", "单位未注册（应先经 UnitToken::find）");
    }
    if (from.kind() != to.kind()) {
        fail("core/units/dimension: ",
             std::string{"量纲不匹配（"} + std::string{from.symbol()} + " → "
                 + std::string{to.symbol()} + "）");
    }
    if (std::isnan(v) || std::isinf(v)) {
        fail("core/units/convert: ", "输入非有限（NaN/±Inf 拒绝——NFR-COR-03）");
    }
    const double out = (v * from.siFactor()) / to.siFactor();
    if (std::isnan(out) || std::isinf(out)) {
        fail("core/units/convert: ", "结果溢出为非有限");
    }
    return out;
}

// ---- Quantity<K> 私有静态：K 对应的 SI 单位（显示投影的换算基准） ----
template <QuantityKind K>
UnitToken Quantity<K>::siUnit() noexcept
{
    // 每个 K 的 SI 单位在 R1 表中有且仅有一个（factor=1 行）。
    switch (K) {
    case QuantityKind::Length:               return *UnitToken::find("m");
    case QuantityKind::Angle:                return *UnitToken::find("rad");
    case QuantityKind::Mass:                 return *UnitToken::find("kg");
    case QuantityKind::Time:                 return *UnitToken::find("s");
    case QuantityKind::Force:                return *UnitToken::find("N");
    case QuantityKind::Torque:               return *UnitToken::find("N*m");
    case QuantityKind::Inertia:              return *UnitToken::find("kg*m^2");
    case QuantityKind::Power:                return *UnitToken::find("W");
    case QuantityKind::LinearVelocity:       return *UnitToken::find("m/s");
    case QuantityKind::AngularVelocity:      return *UnitToken::find("rad/s");
    case QuantityKind::LinearAcceleration:   return *UnitToken::find("m/s^2");
    case QuantityKind::AngularAcceleration:  return *UnitToken::find("rad/s^2");
    case QuantityKind::Voltage:              return *UnitToken::find("V");
    case QuantityKind::Dimensionless:        return *UnitToken::find("1");
    }
    return *UnitToken::find("1");   // 不可达；保返回避免警告
}

// 显式实例化常用量纲（TU 内生成 siUnit——测试/消费方无需看到实现）。
template class Quantity<QuantityKind::Length>;
template class Quantity<QuantityKind::Angle>;
template class Quantity<QuantityKind::Mass>;
template class Quantity<QuantityKind::Time>;
template class Quantity<QuantityKind::Force>;
template class Quantity<QuantityKind::Torque>;
template class Quantity<QuantityKind::Inertia>;
template class Quantity<QuantityKind::Power>;
template class Quantity<QuantityKind::LinearVelocity>;
template class Quantity<QuantityKind::AngularVelocity>;
template class Quantity<QuantityKind::LinearAcceleration>;
template class Quantity<QuantityKind::AngularAcceleration>;
template class Quantity<QuantityKind::Voltage>;
template class Quantity<QuantityKind::Dimensionless>;

}  // namespace sdurws::ird::core
