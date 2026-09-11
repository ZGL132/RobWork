/**
 * @file   CheckTest.cpp
 * @brief  数值断言用例组——TK-CMP（units/testkit.md §8）：CheckResult 机器可读/
 *         反例矩阵（NaN/长度/空/单位不匹配）/宏行为（TK-T05）。
 *
 * 设计依据：
 *   - units/testkit.md §4.3.3（失败行为总表/三级断言不可混用）、§5.3.1/§5.3.2、
 *     §8 TK-CMP 行
 *   - 需求 NFR-COR-01/03；任务契约 tasks/foundation/TK-T05.json acceptance
 *     （委托 core::closeWithin 用例＋CompareDetail 机器可读＋反例矩阵＋宏行为）
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/Errors.hpp>
#include <sdurws/ird/testkit/Check.hpp>
// 宏适配层（展开于消费方 TU——本测试即消费方；要求已链 gtest ✓）。
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <cmath>
#include <limits>
#include <stdexcept>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

namespace {
using namespace sdurws::ird::testkit;
namespace core = sdurws::ird::core;   // 别名：消费 core::Tolerance（C4 两分量）

constexpr auto qnan = std::numeric_limits<double>::quiet_NaN();
constexpr auto inf = std::numeric_limits<double>::infinity();

/** 委托 core 公式：closeWithin 单点包转（CR-06——公式唯一实现点在 core）。 */
TEST(CheckClose, DelegatesToCoreC4Formula_UT_CMP)
{
    const auto t = core::Tolerance::make(0.0, 0.001);
    // 通过：无失败详情。
    const auto ok = checkCloseWithin("fk[3].tcp.position.x", 0.0009, 0.0, t, "appendixD#4");
    EXPECT_TRUE(ok.passed);
    EXPECT_TRUE(ok.failures.empty());
    // 失败：恰一条详情（字段路径/actual/expected/diff 机器可读）。
    const auto bad = checkCloseWithin("fk[3].tcp.position.x", 0.002, 0.0, t, "appendixD#4");
    EXPECT_FALSE(bad.passed);
    ASSERT_EQ(bad.failures.size(), 1u);
    EXPECT_EQ(bad.failures[0].fieldPath, "fk[3].tcp.position.x");
    EXPECT_DOUBLE_EQ(bad.failures[0].actual, 0.002);
    EXPECT_DOUBLE_EQ(bad.failures[0].expected, 0.0);
    EXPECT_DOUBLE_EQ(bad.failures[0].diff, 0.002);
}

/** 非有限：NaN/±Inf → 断言失败（不静默通过——NFR-COR-03）。 */
TEST(CheckClose, NonFiniteFails_UT_CMP)
{
    const auto t = core::Tolerance::make(0.1, 0.1);
    EXPECT_FALSE(checkCloseWithin("fk.x", qnan, 1.0, t, "appendixD#4").passed);
    EXPECT_FALSE(checkCloseWithin("fk.x", 1.0, qnan, t, "appendixD#4").passed);
    EXPECT_FALSE(checkCloseWithin("fk.x", inf, 1.0, t, "appendixD#4").passed);
}

/** 正负抵消反例：逐元素判定不被总和替代（§4.3.3 C4 明文）。 */
TEST(AllCloseWithin, ElementWiseNotSummed_UT_CMP)
{
    const auto t = core::Tolerance::make(0.0, 0.001);
    const std::vector<double> ref{1.0, 1.0};
    const std::vector<double> bad{1.01, 0.99};     // 偏差 +0.01/−0.01——总和归零
    const auto result = checkAllCloseWithin("fk[*].tcp.position", bad, ref, t, "appendixD#4");
    EXPECT_FALSE(result.passed);
    ASSERT_EQ(result.failures.size(), 2u);          // 逐元素各自超差
    EXPECT_TRUE(result.failures[0].hasElement);
    EXPECT_EQ(result.failures[0].fieldPath, "fk[*].tcp.position[0]");
    EXPECT_EQ(result.failures[1].fieldPath, "fk[*].tcp.position[1]");
}

/** 长度不匹配：失败并登记超出索引（§4.3.3"列缺失/额外索引清单"）。 */
TEST(AllCloseWithin, LengthMismatchListed_UT_CMP)
{
    const auto t = core::Tolerance::make(0.0, 0.001);
    const std::vector<double> a{1.0, 2.0, 3.0};
    const std::vector<double> b{1.0, 2.0};
    const auto result = checkAllCloseWithin("fk[*].tcp.position", a, b, t, "appendixD#4");
    EXPECT_FALSE(result.passed);
    // 索引 2 仅在实际侧存在——缺失侧 NaN 占位（机器可读的缺失语义）。
    ASSERT_GE(result.failures.size(), 1u);
    EXPECT_TRUE(std::isnan(result.failures[0].expected));
}

/** 双方皆空：集合等价平凡通过（§4.3.3 表）。 */
TEST(AllCloseWithin, BothEmptyPasses_UT_CMP)
{
    const std::vector<double> empty;
    const auto result = checkAllCloseWithin("fk[*]", empty, empty, core::Tolerance::make(0, 0),
                                            "appendixD#4");
    EXPECT_TRUE(result.passed);
}

/** checkAtMost：≤阈值判定＋未注册单位抛 unit-mismatch（数据集非法级）。 */
TEST(CheckAtMost, BoundAndUnitValidation_UT_CMP)
{
    const auto ok = checkAtMost("dyn/residual", 0.5, 1.0, "N*m");
    EXPECT_TRUE(ok.passed);
    const auto bad = checkAtMost("dyn/residual", 1.5, 1.0, "N*m");
    EXPECT_FALSE(bad.passed);
    // 未注册单位 → TestKitError(UnitMismatch)（§4.3.3 表——数据集非法级）。
    EXPECT_THROW(checkAtMost("dyn/residual", 0.5, 1.0, "meter"), TestKitError);
}

/** checkIdentical：精确相等（无容差——附录 D 第 12 项）双重载。 */
TEST(CheckIdentical, ExactEqualityBothOverloads_UT_CMP)
{
    EXPECT_TRUE(checkIdentical("id", "obj-a", "obj-a").passed);
    EXPECT_FALSE(checkIdentical("id", "obj-a", "obj-b").passed);
    EXPECT_TRUE(checkIdentical("n", static_cast<std::int64_t>(7),
                               static_cast<std::int64_t>(7)).passed);
    EXPECT_FALSE(checkIdentical("n", static_cast<std::int64_t>(7),
                                static_cast<std::int64_t>(8)).passed);
}

/** 宏行为：IRD_EXPECT_CLOSE 失败时 ADD_FAILURE（gtest 集成形态）。 */
TEST(AssertMacrosBehavior, MacroExpansionWorks_UT_CMP)
{
    const auto profile = ToleranceProfile::load(
        fs::path{IRD_TESTDATA_ROOT} / "tolerance" / "kin-fk" / "v1.0.0.json");
    // 宏通过形态（委托 profile.resolve 取容差）。
    EXPECT_NO_THROW({ IRD_EXPECT_CLOSE("fk[3].tcp.position.x", 0.0, 0.0, profile, "appendixD#4"); });
    // IRD_EXPECT_FINITE 守卫。
    IRD_EXPECT_FINITE("x", 1.0);
    EXPECT_NO_THROW({ IRD_EXPECT_FINITE("x", 1.0); });
}
}  // namespace
