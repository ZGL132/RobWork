/**
 * @file   UnitsTest.cpp
 * @brief  单位与量用例组——UT-UNIT（units/core.md §8）：往返/量纲不匹配/非有限/
 *         溢出/未知符号/kind 误配/token 表完整性（§4.4/§5.4，CORE-T04）。
 *
 * 设计依据：
 *   - units/core.md §4.4（R1 表/错误口径/Quantity<K>）、§5.4（签名）、§8 UT-UNIT 行
 *   - 需求 NFR-COR-03（不静默转 0）、KIN-12（显示投影）、SEL-02、DYN-03
 *   - 任务契约 tasks/foundation/CORE-T04.json
 *
 * 数值断言口径：换算各一次乘除——往返不承诺位精确，按相对容差 1e-12 断言
 * （UT-UNIT 明文"按附录 D 容差断言往返一致"的精神，用于无附录 D 条目的纯换算）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/Errors.hpp>
#include <sdurws/ird/core/Units.hpp>

#include <cmath>
#include <limits>
#include <string>

namespace {
using namespace sdurws::ird::core;

constexpr double kTol = 1e-12;   // 纯换算相对容差（0.01 因子的二进制舍入量级远小于此）

/// 相对容差近似相等（scale 为参照量级，防 0 附近除零）。
void expectNearRel(double got, double want, double scale)
{
    EXPECT_NEAR(got, want, kTol * scale);
}

/** 已知换算：mm→m＝1.234；deg→rad＝π/2（§8 UT-UNIT 明文两条）。 */
TEST(UnitsConvert, KnownConversions_UT_UNIT)
{
    const auto mm = UnitToken::find("mm");
    const auto m = UnitToken::find("m");
    const auto deg = UnitToken::find("deg");
    const auto rad = UnitToken::find("rad");
    ASSERT_TRUE(mm.has_value() && m.has_value() && deg.has_value() && rad.has_value());

    EXPECT_DOUBLE_EQ(convert(1234.0, *mm, *m), 1.234);            // §5.4 示例（整因子位精确）
    expectNearRel(convert(90.0, *deg, *rad), 1.5707963267948966, 1.5707963267948966);

    // 往返一致（容差内）：deg→rad→deg。
    const double back = convert(convert(37.5, *deg, *rad), *rad, *deg);
    expectNearRel(back, 37.5, 37.5);

    // try 轨与抛出轨迹同值。
    EXPECT_DOUBLE_EQ(*tryConvert(1234.0, *mm, *m), convert(1234.0, *mm, *m));
}

/** 量纲不匹配：N*m→N 抛/空（§8 明文）；反向同。 */
TEST(UnitsConvert, DimensionMismatch_UT_UNIT)
{
    const auto nm = UnitToken::find("N*m");
    const auto n = UnitToken::find("N");
    ASSERT_TRUE(nm.has_value() && n.has_value());
    EXPECT_THROW(convert(1.0, *nm, *n), CoreError);
    EXPECT_FALSE(tryConvert(1.0, *nm, *n).has_value());
    // 错误前缀 core/units/dimension（§4.10 稳定前缀）。
    try {
        convert(1.0, *nm, *n);
        FAIL() << "必须抛出";
    } catch (const CoreError& e) {
        EXPECT_EQ(std::string(e.what()).find("core/units/dimension: "), 0u);
    }
}

/** 非有限与溢出：NaN/±Inf 拒绝；DBL_MAX mm→m 溢出拒绝（§8 明文）。 */
TEST(UnitsConvert, NonFiniteAndOverflow_UT_UNIT)
{
    const auto mm = UnitToken::find("mm");
    const auto m = UnitToken::find("m");
    const auto quietNaN = std::numeric_limits<double>::quiet_NaN();
    const auto inf = std::numeric_limits<double>::infinity();
    const auto dbmax = std::numeric_limits<double>::max();

    EXPECT_FALSE(tryConvert(quietNaN, *mm, *m).has_value());
    EXPECT_FALSE(tryConvert(inf, *mm, *m).has_value());
    EXPECT_THROW(convert(quietNaN, *mm, *m), CoreError);
    // DBL_MAX mm(0.001)→m 不溢出——反向 DBL_MAX m→mm（×1000）才溢出：§8 的方向
    // 语义按"溢出拒绝"验证：放大因子的方向必须拒绝。
    EXPECT_FALSE(tryConvert(dbmax, *m, *mm).has_value());
    EXPECT_THROW(convert(dbmax, *m, *mm), CoreError);
}

/** 未知符号：find 返回空（§8 明文）；无效句柄走 convert 抛"unregistered"。 */
TEST(UnitsFind, UnknownSymbol_UT_UNIT)
{
    EXPECT_EQ(UnitToken::find("parsec"), std::nullopt);
    EXPECT_EQ(UnitToken::find(""), std::nullopt);
    EXPECT_EQ(UnitToken::find("MM"), std::nullopt);   // 区分大小写（token 冻结）
    // 无效句柄（默认构造绕过 find——调用方违约形态）的 try 轨/抛出轨迹。
    UnitToken invalid;
    EXPECT_FALSE(tryConvert(1.0, invalid, *UnitToken::find("m")).has_value());
    EXPECT_THROW(convert(1.0, invalid, *UnitToken::find("m")), CoreError);
}

/** token 表完整性：R1 清单 17 条逐项注册且 kind/因子正确（§8 明文）。 */
TEST(UnitsRegistry, R1TableComplete_UT_UNIT)
{
    struct Row { const char* sym; QuantityKind kind; double factor; };
    const Row r1[] = {
        {"m", QuantityKind::Length, 1.0}, {"cm", QuantityKind::Length, 0.01},
        {"mm", QuantityKind::Length, 0.001}, {"rad", QuantityKind::Angle, 1.0},
        {"deg", QuantityKind::Angle, 3.14159265358979323846 / 180.0},
        {"kg", QuantityKind::Mass, 1.0}, {"s", QuantityKind::Time, 1.0},
        {"N", QuantityKind::Force, 1.0}, {"N*m", QuantityKind::Torque, 1.0},
        {"kg*m^2", QuantityKind::Inertia, 1.0}, {"W", QuantityKind::Power, 1.0},
        {"m/s", QuantityKind::LinearVelocity, 1.0},
        {"rad/s", QuantityKind::AngularVelocity, 1.0},
        {"m/s^2", QuantityKind::LinearAcceleration, 1.0},
        {"rad/s^2", QuantityKind::AngularAcceleration, 1.0},
        {"V", QuantityKind::Voltage, 1.0}, {"1", QuantityKind::Dimensionless, 1.0},
    };
    for (const auto& row : r1) {
        const auto u = UnitToken::find(row.sym);
        ASSERT_TRUE(u.has_value()) << "R1 token 缺注册: " << row.sym;
        EXPECT_EQ(u->kind(), row.kind) << row.sym;
        EXPECT_DOUBLE_EQ(u->siFactor(), row.factor) << row.sym;
        EXPECT_EQ(u->symbol(), std::string_view(row.sym));
    }
}

/** Quantity 强类型：kind 误配 tryParse 失败（§8 明文 Torque 喂 "N"）；显示投影不改真值。 */
TEST(QuantityTyped, TryParseKindMismatchAndDisplay_UT_UNIT)
{
    const auto nm = UnitToken::find("N*m");
    const auto n = UnitToken::find("N");
    ASSERT_TRUE(nm.has_value() && n.has_value());

    // 合法：Torque::tryParse(4.5, N*m)。
    const auto t = Torque::tryParse(4.5, *nm);
    ASSERT_TRUE(t.has_value());
    EXPECT_DOUBLE_EQ(t->siValue(), 4.5);

    // kind 误配（§8 明文）：喂 "N"（Force）给 Torque —— 类型误用解析边界失败。
    EXPECT_FALSE(Torque::tryParse(4.5, *n).has_value());
    // Force 侧同型成功——两类型天然隔离（DYN-03）。
    EXPECT_TRUE(Force::tryParse(4.5, *n).has_value());

    // 非有限拒绝。
    EXPECT_FALSE(Torque::tryParse(std::numeric_limits<double>::quiet_NaN(), *nm).has_value());

    // 显示投影：displayValueIn 只换算数值、不改 SI 真值（KIN-12）。
    const auto len = Length::fromSi(1.234);
    expectNearRel(len.displayValueIn(*UnitToken::find("mm")), 1234.0, 1234.0);
    EXPECT_DOUBLE_EQ(len.siValue(), 1.234);
    // kind 不匹配抛错。
    EXPECT_THROW(len.displayValueIn(*UnitToken::find("rad")), CoreError);

    // 精确相等语义（工程比较归 Compare）。
    EXPECT_TRUE(len == Length::fromSi(1.234));
    EXPECT_FALSE(len == Length::fromSi(1.235));

    // fromSi 非有限拒绝。
    EXPECT_THROW(Length::fromSi(std::numeric_limits<double>::infinity()), CoreError);
    EXPECT_FALSE(Length::tryFromSi(std::numeric_limits<double>::quiet_NaN()).has_value());
}
}  // namespace
