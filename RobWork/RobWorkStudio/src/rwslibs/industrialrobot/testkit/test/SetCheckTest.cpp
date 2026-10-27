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

#include <cstddef>
#include <cstdint>
#include <functional>
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

// =====================================================================
// F-594 回归（audit/unit-code-review-20261009）
// =====================================================================

/**
 * 回归（F-594，P1）：歧义探测移除配对后未屏蔽被释放的 ri——Kuhn 首扫
 * 按索引序原样配回并返回 true，任何阶段 2 数值配对下首次探测必误报
 * ambiguousMatch=true（唯一匹配与真歧义无法区分，审计字段失真）。
 * 唯一可行匹配必须报告 false。
 */
TEST(SetNumeric, AmbiguityProbeRequiresAlternativePartner_F594)
{
    const std::vector<DemoElem> expected{elem("", 1.0, 2.0), elem("", 5.0, 6.0)};
    const std::vector<DemoElem> actual{elem("", 1.0, 2.0), elem("", 5.0, 6.0)};
    const auto r = checkSetEquivalent(expected, actual, sampleProfile(),
                                      SetMatchTraits<DemoElem>{});
    ASSERT_TRUE(r.equivalent);
    EXPECT_FALSE(r.ambiguousMatch)
        << "每点仅一个可行伙伴——不得误报歧义（修复前恒 true）";
}

/**
 * 配对反面钉：真歧义（两期望点数值相同——任一点都有两个可行伙伴）必须
 * 仍被检出，防把探测修死（屏蔽 ri 后替代伙伴存在路径必须可达）。
 */
TEST(SetNumeric, TrueAmbiguityStillDetected_F594)
{
    const std::vector<DemoElem> expected{elem("", 1.0, 2.0), elem("", 1.0, 2.0)};
    const std::vector<DemoElem> actual{elem("", 1.0, 2.0), elem("", 1.0, 2.0)};
    const auto r = checkSetEquivalent(expected, actual, sampleProfile(),
                                      SetMatchTraits<DemoElem>{});
    ASSERT_TRUE(r.equivalent);
    EXPECT_TRUE(r.ambiguousMatch) << "存在替代配对＝真歧义（探测必须检出）";
}

// =====================================================================
// 最大匹配内核复用回归（Boost checked Edmonds 薄适配——§5.4.2 阶段 2；
// 审计 B17/probe-r17 候选的生产接入面。穷举 oracle 独立于被测实现：
// 精确最大基数由逐点枚举给出，不与内核共享任何代码路径）
// =====================================================================

namespace {

/// 穷举 oracle：精确最大基数匹配（左点逐个"跳过／配任一空闲右点"；n≤6
/// 精确无近似）。adj 为独立重算的相容邻接（不来自被测实现的 compatible）。
std::size_t bruteForceMaxMatching(std::size_t nL, std::size_t nR,
                                  const std::vector<std::vector<bool>>& adj)
{
    std::size_t best = 0;
    std::vector<bool> usedR(nR, false);
    const std::function<void(std::size_t, std::size_t)> walk =
        [&](std::size_t li, std::size_t cnt) {
            if (cnt + (nL - li) <= best) { return; }   // 剪枝：全配也追不上
            if (li == nL) {
                best = cnt;
                return;
            }
            walk(li + 1, cnt);   // li 不配对
            for (std::size_t ri = 0; ri < nR; ++ri) {
                if (!usedR[ri] && adj[li][ri]) {
                    usedR[ri] = true;
                    walk(li + 1, cnt + 1);
                    usedR[ri] = false;
                }
            }
        };
    walk(0, 0);
    return best;
}

/// 穷举 oracle：基数恰为 max 的匹配个数（>1＝多解——歧义 flag 的独立判据）。
std::size_t bruteForceCountMatchings(std::size_t nL, std::size_t nR,
                                     const std::vector<std::vector<bool>>& adj,
                                     std::size_t maxCardinality)
{
    std::size_t count = 0;
    std::vector<bool> usedR(nR, false);
    const std::function<void(std::size_t, std::size_t)> walk =
        [&](std::size_t li, std::size_t cnt) {
            if (li == nL) {
                if (cnt == maxCardinality) { ++count; }
                return;
            }
            walk(li + 1, cnt);   // li 不配对
            if (cnt == maxCardinality) { return; }   // 已达基数只能继续跳过
            for (std::size_t ri = 0; ri < nR; ++ri) {
                if (!usedR[ri] && adj[li][ri]) {
                    usedR[ri] = true;
                    walk(li + 1, cnt + 1);
                    usedR[ri] = false;
                }
            }
        };
    walk(0, 0);
    return count;
}

/// 确定性伪随机（LCG；编译期固定种子——无运行时随机源，同输入同用例集）。
struct DetRandom {
    std::uint32_t state;
    explicit DetRandom(std::uint32_t seed) : state(seed) {}
    std::uint32_t next()
    {
        state = state * 1664525u + 1013904223u;
        return (state >> 16) & 0x7FFFu;
    }
};

}  // namespace

/** 内核穷举 oracle（detail 接缝直测）：3×3 全部 512 种边集形态逐一对照
 *  精确最大基数；4×5 取 240 个确定性伪随机边集同检。验证面＝配对合法
 *  （都是给定边）、两两不同右点、基数与精确值一致。 */
TEST(SetMatchCore, ExhaustiveBruteForceAgreement_UT_SET)
{
    using sdurws::ird::testkit::detail::maximumBipartiteMatching;
    constexpr std::size_t kNil = static_cast<std::size_t>(-1);

    // 3×3 全枚举：位掩码 k 的第 (li*3+ri) 位＝边存在。
    for (std::uint32_t mask = 0; mask < 512u; ++mask) {
        std::vector<std::vector<bool>> adj(3, std::vector<bool>(3, false));
        std::vector<std::pair<std::size_t, std::size_t>> edges;
        for (std::size_t li = 0; li < 3; ++li) {
            for (std::size_t ri = 0; ri < 3; ++ri) {
                if ((mask >> (li * 3 + ri)) & 1u) {
                    adj[li][ri] = true;
                    edges.emplace_back(li, ri);
                }
            }
        }
        const auto matchL = maximumBipartiteMatching(3, 3, edges);
        ASSERT_EQ(matchL.size(), 3u);
        std::vector<bool> rightSeen(3, false);
        std::size_t count = 0;
        for (std::size_t li = 0; li < 3; ++li) {
            if (matchL[li] == kNil) { continue; }
            ++count;
            ASSERT_TRUE(adj[li][matchL[li]])
                << "mask=" << mask << "：配对必须是给定边";
            EXPECT_FALSE(rightSeen[matchL[li]])
                << "mask=" << mask << "：右点不得重复配对";
            rightSeen[matchL[li]] = true;
        }
        EXPECT_EQ(count, bruteForceMaxMatching(3, 3, adj))
            << "mask=" << mask << "：基数必须达到精确最大";
    }

    // 4×5 确定性伪随机抽样（无全枚举必要——密集/稀疏/无解形态混采）。
    DetRandom rng{20261023u};
    for (int caseIdx = 0; caseIdx < 240; ++caseIdx) {
        std::vector<std::vector<bool>> adj(4, std::vector<bool>(5, false));
        std::vector<std::pair<std::size_t, std::size_t>> edges;
        for (std::size_t li = 0; li < 4; ++li) {
            for (std::size_t ri = 0; ri < 5; ++ri) {
                if (rng.next() % 4u == 0u) {   // ≈25% 边密度
                    adj[li][ri] = true;
                    edges.emplace_back(li, ri);
                }
            }
        }
        const auto matchL = maximumBipartiteMatching(4, 5, edges);
        std::vector<bool> rightSeen(5, false);
        std::size_t count = 0;
        for (std::size_t li = 0; li < 4; ++li) {
            if (matchL[li] == kNil) { continue; }
            ++count;
            ASSERT_TRUE(adj[li][matchL[li]]) << "case=" << caseIdx;
            EXPECT_FALSE(rightSeen[matchL[li]]) << "case=" << caseIdx;
            rightSeen[matchL[li]] = true;
        }
        EXPECT_EQ(count, bruteForceMaxMatching(4, 5, adj)) << "case=" << caseIdx;
    }
}

/** 模板层网格族 oracle（生产入口 checkSetEquivalent 全链路）：1 维网格位置
 *  （δ=0.75e-12，容差 1e-12 ⇒ 边＝网格距≤1，非传递——含贪婪陷阱形态）。
 *  5^6=15625 组 3×3 位置逐一对照：等价结论/基数/缺失多余计数/歧义 flag/
 *  见证配对合法性（歧义＝最大匹配数>1，独立穷举判据）。 */
TEST(SetMatchCore, GridFamilyTemplateSemantics_UT_SET)
{
    constexpr double kGrid = 0.75e-12;   // 2×kGrid=1.5e-12>1e-12——距 2 无边
    const auto px = [kGrid](std::size_t grid) {
        return static_cast<double>(grid) * kGrid;
    };

    std::size_t ambiguousCases = 0;
    for (std::size_t pattern = 0; pattern < 5u * 5u * 5u * 5u * 5u * 5u; ++pattern) {
        std::size_t p = pattern;
        std::size_t lpos[3], apos[3];
        for (auto& v : lpos) { v = p % 5u; p /= 5u; }
        for (auto& v : apos) { v = p % 5u; p /= 5u; }
        std::vector<std::vector<bool>> adj(3, std::vector<bool>(3, false));
        for (std::size_t li = 0; li < 3; ++li) {
            for (std::size_t ri = 0; ri < 3; ++ri) {
                const double diff = px(lpos[li]) > px(apos[ri])
                                        ? px(lpos[li]) - px(apos[ri])
                                        : px(apos[ri]) - px(lpos[li]);
                adj[li][ri] = diff <= 1e-12;
            }
        }
        const std::size_t maxCard = bruteForceMaxMatching(3, 3, adj);
        const std::size_t maxCount = bruteForceCountMatchings(3, 3, adj, maxCard);

        const std::vector<DemoElem> expected{elem("", px(lpos[0]), 0.0),
                                             elem("", px(lpos[1]), 0.0),
                                             elem("", px(lpos[2]), 0.0)};
        const std::vector<DemoElem> actual{elem("", px(apos[0]), 0.0),
                                           elem("", px(apos[1]), 0.0),
                                           elem("", px(apos[2]), 0.0)};
        const auto r = checkSetEquivalent(expected, actual, sampleProfile(),
                                          SetMatchTraits<DemoElem>{});
        // 等价 ⇔ 精确最大匹配覆盖双方全部元素；缺失/多余计数由基数唯一决定。
        EXPECT_EQ(r.equivalent, maxCard == 3u) << "pattern=" << pattern;
        EXPECT_EQ(r.matched.size(), maxCard) << "pattern=" << pattern;
        EXPECT_EQ(r.missingExpected.size(), 3u - maxCard) << "pattern=" << pattern;
        EXPECT_EQ(r.extraActual.size(), 3u - maxCard) << "pattern=" << pattern;
        // 歧义（F-594 语义）：存在另一完美匹配 ⇔ 最大匹配个数>1；仅完美情形判。
        EXPECT_EQ(r.ambiguousMatch, maxCard == 3u && maxCount > 1)
            << "pattern=" << pattern;
        if (r.ambiguousMatch) { ++ambiguousCases; }
        // 见证配对必须都是相容边（oracle 独立重算），且左点互不相同。
        std::vector<bool> leftSeen(3, false);
        for (const auto& pair : r.matched) {
            ASSERT_LT(pair.expectedIndex, 3u);
            ASSERT_LT(pair.actualIndex, 3u);
            EXPECT_TRUE(adj[pair.expectedIndex][pair.actualIndex])
                << "pattern=" << pattern << "：见证必须是相容边";
            EXPECT_FALSE(leftSeen[pair.expectedIndex]) << "pattern=" << pattern;
            leftSeen[pair.expectedIndex] = true;
        }
    }
    EXPECT_GT(ambiguousCases, 0u) << "网格族必须覆盖到真歧义形态";
}

/** 贪婪陷阱（需增广才能全覆盖的确定性形态）：先到的期望点若被贪婪锁死在
 *  唯一被争用右点上，后续点只能经增广路径腾挪——最大匹配内核必须给出
 *  完美覆盖（TK-SET 一一配对语义的反贪婪验证）。 */
TEST(SetNumeric, GreedyTrapAugmented_UT_SET)
{
    // E: {δ, 0}，A: {0, 2δ}（δ=0.75e-12）——E0 邻 A0/A1，E1 只邻 A0：
    // 索引序贪婪先把 E0→A0 后会留下 E1；必须增广为 E0→A1、E1→A0。
    constexpr double kDelta = 0.75e-12;
    const std::vector<DemoElem> expected{elem("", kDelta, 0.0),
                                         elem("", 0.0, 0.0)};
    const std::vector<DemoElem> actual{elem("", 0.0, 0.0),
                                       elem("", 2.0 * kDelta, 0.0)};
    const auto r = checkSetEquivalent(expected, actual, sampleProfile(),
                                      SetMatchTraits<DemoElem>{});
    EXPECT_TRUE(r.equivalent) << "完美匹配存在（E0→A1、E1→A0）——贪婪"
                                 "陷阱形态必须经增广达成全覆盖";
    EXPECT_EQ(r.matched.size(), 2u);
    EXPECT_FALSE(r.ambiguousMatch);
}

/** 多解见证确定性（§5.4.2"同输入同报告"）：完全二部 2×2（4 边全通，2 个
 *  完美匹配）重复调用——见证配对逐次一致、歧义 flag 稳定居真。 */
TEST(SetNumeric, MultiSolutionWitnessDeterministic_UT_SET)
{
    const std::vector<DemoElem> expected{elem("", 0.0, 0.0),
                                         elem("", 0.75e-12, 0.0)};
    const std::vector<DemoElem> actual{elem("", 0.0, 0.0),
                                       elem("", 0.75e-12, 0.0)};
    const auto first = checkSetEquivalent(expected, actual, sampleProfile(),
                                          SetMatchTraits<DemoElem>{});
    const auto second = checkSetEquivalent(expected, actual, sampleProfile(),
                                           SetMatchTraits<DemoElem>{});
    ASSERT_TRUE(first.equivalent);
    EXPECT_TRUE(first.ambiguousMatch) << "4 边全通＝2 个完美匹配＝真歧义";
    EXPECT_EQ(first.matched, second.matched)
        << "多解情形的见证配对必须逐次稳定（确定性 tie-break 语义）";
    EXPECT_EQ(first.ambiguousMatch, second.ambiguousMatch);
}

/** REUSE-T20／TK-SET：解析缓存只属于本次断言，同字段在另一档案下必须重新解析。
 * 更严档案仍低于既有 allowedMax；不能让先前宽档案的解析值污染随后比较。 */
TEST(SetNumeric, ToleranceCacheIsAssertionLocal_UT_SET)
{
    auto strict = sampleProfile();
    for (auto& entry : strict.entries) { entry.tolerance.absolute *= 0.25; }
    const std::vector<DemoElem> expected{elem("", 0.0, 0.0), elem("", 4e-12, 0.0)};
    const std::vector<DemoElem> actual{elem("", 0.75e-12, 0.0), elem("", 4.75e-12, 0.0)};
    const auto compare = [&](const ToleranceProfile& profile) {
        return checkSetEquivalent(expected, actual, profile, SetMatchTraits<DemoElem>{});
    };
    EXPECT_TRUE(compare(sampleProfile()).equivalent);
    EXPECT_FALSE(compare(strict).equivalent) << "同一路径必须采用当前档案的更严容差";
    EXPECT_TRUE(compare(sampleProfile()).equivalent) << "缓存不能跨断言共享";
}

/** REUSE-T20／TK-SET：缓存不能预解析未消费字段，也不能吞掉真正使用的缺失容差。
 * 空侧无数值比较；相容谓词在首字段失配时短路；真正消费缺档字段仍报原错误。 */
TEST(SetNumeric, ToleranceResolutionRemainsLazy_UT_SET)
{
    auto xOnly = sampleProfile();
    xOnly.entries = {sampleProfile().resolve("fk[0].tcp.position.x")};
    const std::vector<DemoElem> expected{elem("", 0.0, 0.0)};
    const auto compare = [&](const std::vector<DemoElem>& actual) {
        return checkSetEquivalent(expected, actual, xOnly, SetMatchTraits<DemoElem>{});
    };
    EXPECT_NO_THROW(compare({}));
    // 只验证边谓词的短路；完整断言的失配详情会合法地消费全部字段，不能混为一谈。
    std::map<std::string, sdurws::ird::core::Tolerance, std::less<>> cache;
    EXPECT_FALSE(detail::compatible(expected.front(), elem("", 1.0, 0.0),
                                    xOnly, SetMatchTraits<DemoElem>{}, &cache));
    try {
        compare(expected);
        FAIL() << "实际比较到缺失的 y 容差时必须 fail-fast";
    } catch (const TestKitError& error) {
        EXPECT_EQ(error.kind(), TestKitErrorKind::ToleranceUndefined);
    }
}
