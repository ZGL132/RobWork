/**
 * @file   SetCheckTest.cpp
 * @brief  集合与顺序断言用例组——TK-SET/TK-ORD（units/testkit.md §8）：一一配对/
 *         歧义报告/重复检测/排序误判反例/身份 vs 数值两形态（TK-T06）。
 *
 * 设计依据：
 *   - units/testkit.md §5.4.1（容差匹配非等价关系——TK-SET-3 反例）/§5.4.2（两阶段
 *     规则）/§5.4.3（集合/顺序不可互替）/§5.4.4（歧义处理）/§8 TK-SET·TK-ORD
 *   - 需求 NFR-COR-02；任务契约 tasks/foundation/TK-T06.json acceptance
 *     （排序误判/重复匹配反例——元测试）
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/Errors.hpp>
#include <sdurws/ird/testkit/SetCheck.hpp>
#include <sdurws/ird/testkit/ToleranceProfile.hpp>

#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace {
using namespace sdurws::ird::testkit;

/// 演示元素：身份键（可空）＋两个数值字段（position.x/y 语义）。
struct DemoElem {
    std::string id;        ///< 身份键（空串＝无身份）
    double x = 0.0;        ///< SI 值 1
    double y = 0.0;        ///< SI 值 2
};

}

/// traits 特化（§5.4.5：消费方以 traits 适配自身类型——本测试即消费示范）。
template <>
struct SetMatchTraits<DemoElem> {
    static std::optional<std::string> identity(const DemoElem& e)
    {
        if (e.id.empty()) { return std::nullopt; }
        return e.id;
    }
    static std::vector<NumericFieldView> numerics(const DemoElem& e)
    {
        return {{"fk[*].tcp.position.x", e.x}, {"fk[*].tcp.position.y", e.y}};
    }
};

/// 仓库内示例档案（tolerance/kin-fk——绝对容差 1e-12 条目）。
const ToleranceProfile& sampleProfile()
{
    static const ToleranceProfile profile = ToleranceProfile::load(
        fs::path{IRD_TESTDATA_ROOT} / "tolerance" / "kin-fk" / "v1.0.0.json");
    return profile;
}

DemoElem elem(const std::string& id, double x, double y)
{
    return DemoElem{id, x, y};
}

/** TK-SET：同集合（不同顺序）等价通过＋matched 覆盖（一一配对）。 */
TEST(SetEquivalent, OrderIndependentEquivalent_UT_SET)
{
    const std::vector<DemoElem> expected{elem("a", 1.0, 2.0), elem("b", 3.0, 4.0)};
    const std::vector<DemoElem> actual{elem("b", 3.0, 4.0), elem("a", 1.0, 2.0)};
    const auto r = checkSetEquivalent(expected, actual, sampleProfile(),
                                      SetMatchTraits<DemoElem>{});
    EXPECT_TRUE(r.equivalent);
    EXPECT_EQ(r.matched.size(), 2u);
    EXPECT_TRUE(r.missingExpected.empty());
    EXPECT_TRUE(r.extraActual.empty());
    EXPECT_FALSE(r.ambiguousMatch);
}

/** TK-SET 反例：值超容差（同身份）→ identity-value-mismatch 详情定位到字段。 */
TEST(SetEquivalent, IdentityValueMismatchLocated_UT_SET)
{
    const std::vector<DemoElem> expected{elem("a", 1.0, 2.0)};
    const std::vector<DemoElem> actual{elem("a", 1.5, 2.0)};   // x 超 1e-12 容差
    const auto r = checkSetEquivalent(expected, actual, sampleProfile(),
                                      SetMatchTraits<DemoElem>{});
    EXPECT_FALSE(r.equivalent);
    ASSERT_FALSE(r.mismatchedPairs.empty());
    EXPECT_EQ(r.mismatchedPairs[0].fieldPath, "fk[*].tcp.position.x");
}

/** 阶段 0 反例：actual 身份键重复 → duplicate-identity 逐键列出。 */
TEST(SetDuplicates, ActualDuplicateIdentity_UT_SET)
{
    const std::vector<DemoElem> expected{elem("a", 1.0, 2.0)};
    const std::vector<DemoElem> actual{elem("a", 1.0, 2.0), elem("a", 1.0, 2.0)};
    const auto r = checkSetEquivalent(expected, actual, sampleProfile(),
                                      SetMatchTraits<DemoElem>{});
    EXPECT_FALSE(r.equivalent);
    ASSERT_FALSE(r.duplicates.empty());
    EXPECT_EQ(r.duplicates[0], "a");
}

/** 阶段 0 反例：expected 身份键重复 → DatasetInvalid（参考集自相矛盾）。 */
TEST(SetDuplicates, ExpectedDuplicateThrows_UT_SET)
{
    const std::vector<DemoElem> expected{elem("a", 1.0, 2.0), elem("a", 1.0, 2.0)};
    const std::vector<DemoElem> actual{elem("a", 1.0, 2.0)};
    EXPECT_THROW(checkSetEquivalent(expected, actual, sampleProfile(),
                                    SetMatchTraits<DemoElem>{}),
                 TestKitError);
}

/** 数值匹配（无身份）：纯数值形态一一配对；缺/多元素进 missing/extra。 */
TEST(SetNumeric, NumericOnlyMatching_UT_SET)
{
    const std::vector<DemoElem> expected{elem("", 1.0, 2.0), elem("", 5.0, 6.0)};
    const std::vector<DemoElem> actual{elem("", 5.0, 6.0), elem("", 1.0, 2.0)};
    const auto r = checkSetEquivalent(expected, actual, sampleProfile(),
                                      SetMatchTraits<DemoElem>{});
    EXPECT_TRUE(r.equivalent);
    // 缺失/多余：少一个实际元素。
    const std::vector<DemoElem> short1{elem("", 1.0, 2.0)};
    const auto r2 = checkSetEquivalent(expected, short1, sampleProfile(),
                                       SetMatchTraits<DemoElem>{});
    EXPECT_FALSE(r2.equivalent);
    ASSERT_EQ(r2.missingExpected.size(), 1u);
    EXPECT_EQ(r2.extraActual.size(), 0u);
}

/** TK-SET-3 反例（元测试）：排序后逐项比较会误判的"两两错位"形态——
 *  一一配对语义必须通过（钉住"容差匹配非等价关系"的集合语义正确性）。 */
TEST(SetNumeric, PairwiseSlipperyCaseStillMatches_UT_SET)
{
    // 期望 {1.0, 1.0000000005e0}——两元素互相在对方容差内（近邻对）。
    // 排序后逐项比较（按数值排序）会因错位配对误判；一一配对可交换配对成功。
    const std::vector<DemoElem> expected{elem("", 1.0, 50.0), elem("", 1.0000000000005, 50.0)};
    const std::vector<DemoElem> actual{elem("", 1.0000000000005, 50.0), elem("", 1.0, 50.0)};
    const auto r = checkSetEquivalent(expected, actual, sampleProfile(),
                                      SetMatchTraits<DemoElem>{});
    EXPECT_TRUE(r.equivalent);
}

/** TK-ORD：稳定排序一致（同序）＋首错位索引报出（元素对但序错——§5.4.3）。 */
TEST(StableOrder, SameOrderAndFirstDivergence_UT_ORD)
{
    const std::vector<DemoElem> expected{elem("a", 1.0, 0.0), elem("b", 2.0, 0.0)};
    const std::vector<DemoElem> actual{elem("a", 1.0, 0.0), elem("b", 2.0, 0.0)};
    const auto same = checkStableOrder(expected, actual, SetMatchTraits<DemoElem>{},
                                       sampleProfile());
    EXPECT_TRUE(same.sameOrder);

    // 元素对但序错：集合等价通过而顺序失败（§5.4.3——不可互替的行为面钉住）。
    const std::vector<DemoElem> flipped{elem("b", 2.0, 0.0), elem("a", 1.0, 0.0)};
    const auto r = checkStableOrder(expected, flipped, SetMatchTraits<DemoElem>{},
                                    sampleProfile());
    EXPECT_FALSE(r.sameOrder);
    EXPECT_EQ(r.firstDivergence, 0u);
    // 同集合断言仍通过（两断言独立——互替性反证）。
    const auto setR = checkSetEquivalent(expected, flipped, sampleProfile(),
                                         SetMatchTraits<DemoElem>{});
    EXPECT_TRUE(setR.equivalent);
}

/** 规模护栏：>100,000 元素抛 Usage（§5.4.2）。 */
TEST(SetGuard, ScaleLimitThrows_UT_SET)
{
    const std::vector<DemoElem> big(100001);
    const std::vector<DemoElem> small(1);
    EXPECT_THROW(checkSetEquivalent(big, small, sampleProfile(),
                                    SetMatchTraits<DemoElem>{}),
                 TestKitError);
}
