/**
 * @file   CompareTest.cpp
 * @brief  容差比较用例组（CORE-T05）——C4 公式/正负抵消反例/零参考退化/
 *         非有限拒绝/C7 转写逐量纲。
 *
 * 设计依据：
 *   - units/core.md §4.5（契约表）/§5.5（签名）/附录 D C4·C7
 *   - 需求 NFR-COR-02/03；任务契约 tasks/foundation/CORE-T05.json acceptance
 *     （C4 与 C7 用例，含正负抵消反例与零参考退化；CR-06 API diff 前置）
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/Compare.hpp>
#include <sdurws/ird/core/Errors.hpp>

#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
using namespace sdurws::ird::core;

constexpr auto inf = std::numeric_limits<double>::infinity();
constexpr auto qnan = std::numeric_limits<double>::quiet_NaN();

/** C4 公式基本面：相对项、绝对项、混合。 */
TEST(CompareFormula, C4Basic_UT_UNIT)
{
    const auto t = Tolerance::make(0.01, 0.001);
    // 相对项：value 偏离 reference 1%（≤ 相对容差 1%）。
    EXPECT_TRUE(closeWithin(1.01, 1.0, t));
    // 超出相对项。
    EXPECT_FALSE(closeWithin(1.02, 1.0, t));
    // 绝对项兜底：小值时相对项失效、绝对项 0.001 覆盖。
    EXPECT_TRUE(closeWithin(0.0009, 0.0, t));
}

/** 零参考退化：reference=0 时公式退化为纯 absolute（不要求精确零）。 */
TEST(CompareFormula, ZeroReferenceDegradesToAbsolute_UT_UNIT)
{
    const auto t = Tolerance::make(0.5, 0.001);   // 相对项大也无用（参考为 0）
    EXPECT_TRUE(closeWithin(0.0, 0.0, t));        // 精确零
    EXPECT_TRUE(closeWithin(0.0009, 0.0, t));     // |0.0009| ≤ abs(0.001)
    EXPECT_FALSE(closeWithin(0.002, 0.0, t));     // 超 absolute
    // 负方向的偏差同样只受 absolute 约束。
    EXPECT_TRUE(closeWithin(-0.0009, 0.0, t));
    EXPECT_FALSE(closeWithin(-0.002, 0.0, t));
}

/** 非有限输入：任一输入 NaN/±Inf → false（不抛、不静默通过——NFR-COR-03）。 */
TEST(CompareFormula, NonFiniteInputsFalse_UT_UNIT)
{
    const auto t = Tolerance::make(0.1, 0.1);
    EXPECT_FALSE(closeWithin(qnan, 1.0, t));
    EXPECT_FALSE(closeWithin(1.0, qnan, t));
    EXPECT_FALSE(closeWithin(inf, 1.0, t));
    EXPECT_FALSE(closeWithin(1.0, -inf, t));
    EXPECT_FALSE(closeWithin(qnan, qnan, t));     // NaN 对 NaN 也是 false（非有限）
}

/** 正负抵消反例：逐元素判定不被差值总和替代（C4 明文——UT 核心反例）。 */
TEST(AllCloseWithin, ElementWiseNotSummed_UT_UNIT)
{
    const auto t = Tolerance::make(0.0, 0.001);   // 纯绝对小容差，结果只看逐元素
    // 元素偏差 +0.01 与 -0.01：总和为 0（"总和判据"会误判通过），逐元素判定必失败。
    const std::vector<double> ref{1.0, 1.0};
    const std::vector<double> bad{1.01, 0.99};
    EXPECT_FALSE(allCloseWithin(bad, ref, t));
    // 逐元素都满足时才通过。
    const std::vector<double> good{1.0005, 0.9998};
    EXPECT_TRUE(allCloseWithin(good, ref, t));
}

/** 长度不等：抛 CoreError（core/compare/length 前缀——违约显性化）。 */
TEST(AllCloseWithin, LengthMismatchThrows_UT_UNIT)
{
    const auto t = Tolerance::make(0.1, 0.1);
    const std::vector<double> a{1.0, 2.0};
    const std::vector<double> b{1.0, 2.0, 3.0};
    EXPECT_THROW(allCloseWithin(a, b, t), CoreError);
    try {
        (void)allCloseWithin(a, b, t);
        FAIL() << "必须抛出";
    } catch (const CoreError& e) {
        EXPECT_EQ(std::string(e.what()).find("core/compare/length: "), 0u);
    }
}

/** Tolerance 工厂：负值/非有限拒绝（两分量均须 ≥0 且有限）。 */
TEST(ToleranceMake, Validation_UT_UNIT)
{
    EXPECT_NO_THROW((void)Tolerance::make(0.0, 0.0));                 // 零合法
    EXPECT_THROW((void)Tolerance::make(-0.1, 0.0), CoreError);        // 负相对
    EXPECT_THROW((void)Tolerance::make(0.0, -1.0), CoreError);        // 负绝对
    EXPECT_THROW((void)Tolerance::make(qnan, 0.0), CoreError);        // NaN
    EXPECT_THROW((void)Tolerance::make(0.0, inf), CoreError);         // Inf
    try {
        (void)Tolerance::make(-0.1, 0.0);
        FAIL() << "必须抛出";
    } catch (const CoreError& e) {
        EXPECT_EQ(std::string(e.what()).find("core/compare/tolerance: "), 0u);
    }
}

/** C7 转写：已声明量纲逐项＋未声明量纲 nullopt（D-06 机械转写）。 */
TEST(RuntimeAbsoluteTolerance, C7Transcription_UT_UNIT)
{
    // 已声明（附录 D C7 默认值逐项）。
    ASSERT_TRUE(runtimeAbsoluteTolerance(QuantityKind::Angle).has_value());
    EXPECT_DOUBLE_EQ(*runtimeAbsoluteTolerance(QuantityKind::Angle), 1e-12);
    ASSERT_TRUE(runtimeAbsoluteTolerance(QuantityKind::Length).has_value());
    EXPECT_DOUBLE_EQ(*runtimeAbsoluteTolerance(QuantityKind::Length), 1e-12);
    ASSERT_TRUE(runtimeAbsoluteTolerance(QuantityKind::Torque).has_value());
    EXPECT_DOUBLE_EQ(*runtimeAbsoluteTolerance(QuantityKind::Torque), 1e-9);
    ASSERT_TRUE(runtimeAbsoluteTolerance(QuantityKind::LinearVelocity).has_value());
    EXPECT_DOUBLE_EQ(*runtimeAbsoluteTolerance(QuantityKind::LinearVelocity), 1e-9);
    ASSERT_TRUE(runtimeAbsoluteTolerance(QuantityKind::AngularVelocity).has_value());
    EXPECT_DOUBLE_EQ(*runtimeAbsoluteTolerance(QuantityKind::AngularVelocity), 1e-9);
    ASSERT_TRUE(runtimeAbsoluteTolerance(QuantityKind::LinearAcceleration).has_value());
    EXPECT_DOUBLE_EQ(*runtimeAbsoluteTolerance(QuantityKind::LinearAcceleration), 1e-9);
    ASSERT_TRUE(runtimeAbsoluteTolerance(QuantityKind::AngularAcceleration).has_value());
    EXPECT_DOUBLE_EQ(*runtimeAbsoluteTolerance(QuantityKind::AngularAcceleration), 1e-9);
    ASSERT_TRUE(runtimeAbsoluteTolerance(QuantityKind::Dimensionless).has_value());
    EXPECT_DOUBLE_EQ(*runtimeAbsoluteTolerance(QuantityKind::Dimensionless), 1e-12);

    // 未声明量纲 → nullopt（"不得引入产品侧相对校验"——C7）。
    EXPECT_FALSE(runtimeAbsoluteTolerance(QuantityKind::Mass).has_value());
    EXPECT_FALSE(runtimeAbsoluteTolerance(QuantityKind::Time).has_value());
    EXPECT_FALSE(runtimeAbsoluteTolerance(QuantityKind::Force).has_value());
    EXPECT_FALSE(runtimeAbsoluteTolerance(QuantityKind::Inertia).has_value());
    EXPECT_FALSE(runtimeAbsoluteTolerance(QuantityKind::Power).has_value());
    EXPECT_FALSE(runtimeAbsoluteTolerance(QuantityKind::Voltage).has_value());
}

#ifdef IRD_CORE_HAS_RW_Q
/** Q 重载：逐元素语义同 vector 版（长度不等抛＋逐元素判定）。 */
TEST(AllCloseWithinQ, QOverloadSemantics_UT_UNIT)
{
    const auto t = Tolerance::make(0.0, 0.001);
    const rw::math::Q ref(2, 1.0, 1.0);
    const rw::math::Q good(2, 1.0005, 0.9998);
    const rw::math::Q bad(2, 1.01, 0.99);
    EXPECT_TRUE(allCloseWithin(good, ref, t));
    EXPECT_FALSE(allCloseWithin(bad, ref, t));    // 正负抵消反例（Q 形态）
    const rw::math::Q len3(3, 1.0, 1.0, 1.0);
    EXPECT_THROW(allCloseWithin(len3, ref, t), CoreError);
}
#endif
}  // namespace
