/**
 * @file   ParetoTest.cpp
 * @brief  Pareto 非支配模型测试（OptPareto 组）——支配判定（严格序/容差
 *         支配/max 方向）、非支配分层、稳定排序三键、CandidateId 去重与
 *         输入校验（"—"候选防线）——任务契约 WP-20-T05 acceptance 1/3
 *         （OPT-VER-131/132/133 观测点；AT-09 容差支配；NFR-COR-02 确定性；
 *         OPT-04 禁加权总分语义见契约测试 OptNoWeightedScoreContract）。
 *
 * 设计依据：
 *   - units/optimization.md §7.4（支配定义两式/相同指标向量互不支配/重复
 *     CandidateId 去重保留首次/稳定排序三键）、§12.2（buildFront @pre——
 *     "—""候选已在管线侧排除）、DOPT-5（默认零容差——不发明阈值，P-OPT-5
 *     安全默认）、DOPT-8（输出序三键全序且与线程无关）、§13.1（OPT-VER-131
 *     稳定排序/132 容差支配/133 重复候选去重）、§8.4（确定性承诺——
 *     NFR-COR-02）
 *   - 需求 OPT-04（Pareto 非支配；不以单一加权总分代替工程取舍）、AT-09
 *     （集合/排序满足容差支配）
 *   - 用例名与断言注释带需求/AT 追溯（AGENTS §2.7）；数值断言给解析期望
 *
 * 测试形态（模型测试＝直调计算库，NFR-MNT-01）：支配/分层/排序为纯函数；
 * buildFront 经 IParetoFrontBuilder 接口指针消费（虚函数路径天然钉住——
 * WP-20-T03 B-1 教训：不留只测自由函数的盲区）。
 */

#include <sdurws/ird/optimization/Pareto.hpp>

#include <sdurws/ird/core/Compare.hpp>
#include <sdurws/ird/optimization/DiagCodes.hpp>
#include <sdurws/ird/optimization/Objective.hpp>
#include <sdurws/ird/optimization/Types.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <string>
#include <vector>

using namespace sdurws::ird;
using optimization::CandidateId;
using optimization::defaultObjectives;
using optimization::dominates;
using optimization::FeasibleCandidate;
using optimization::IParetoFrontBuilder;
using optimization::makeObjectiveSet;
using optimization::MetricId;
using optimization::ObjectiveEntry;
using optimization::ObjectiveSet;
using optimization::ParetoFrontBuilder;
using optimization::ParetoFrontEntry;
using optimization::ParetoFrontResult;
using optimization::kMetricCount;
using optimization::kOptInputInvalid;
using optimization::OptimizationError;
using optimization::OptimizationStage;

namespace {

// =====================================================================
// 构造辅助
// =====================================================================

/// 指定种子的非零候选身份（内容寻址格式的测试构造——字节填种子，避免
/// 保留值全零；种子即身份在断言中的可读标记）。
CandidateId makeCid(unsigned char seed)
{
    CandidateId id;
    id.bytes.fill(seed);
    return id;
}

/// 两目标（包络 min + 裕量 max）可行候选构造——本组测试的标准场景：
/// 指标向量按 {Envelope, MinJointMargin} 两槽填值，其余槽全 nullopt
/// （非激活槽自由——§7.4"八槽全量展示"语义的输入形态）。
FeasibleCandidate makeCandidate(unsigned char seed, double envelope,
                                double margin, bool isBaseline = false)
{
    FeasibleCandidate c;
    c.candidateId = makeCid(seed);
    c.isBaseline = isBaseline;
    c.metricValues.assign(kMetricCount, std::nullopt);
    c.metricValues[static_cast<std::size_t>(MetricId::Envelope)] = envelope;
    c.metricValues[static_cast<std::size_t>(MetricId::MinJointMargin)] = margin;
    return c;
}

/// 两目标（min 包络 / max 裕量）零容差目标集——标准场景 objectives。
ObjectiveSet makeTwoObjectives()
{
    return makeObjectiveSet({{MetricId::Envelope, {}},
                             {MetricId::MinJointMargin, {}}});
}

/// 按 entries 序取身份（断言辅助）。
std::vector<CandidateId> idsOf(const ParetoFrontResult& r)
{
    std::vector<CandidateId> ids;
    ids.reserve(r.entries.size());
    for (const auto& e : r.entries) {
        ids.push_back(e.candidateId);
    }
    return ids;
}

}  // namespace

// =====================================================================
// 支配判定（§7.4 定义式——严格序/容差/max 方向）
// =====================================================================

/// §7.4 严格序（零容差默认——DOPT-5/P-OPT-5 安全默认）：经典 min-min 场景
/// 的支配/被支配/互不支配/相同向量互不支配四象限（解析期望）。
TEST(OptPareto, DominatesStrictZeroTolerance)
{
    IRD_TEST_INFO("OPT-04", {"AT-09"}, std::nullopt);
    const ObjectiveSet objs = makeTwoObjectives();
    // 场景（包络↓min / 裕量↑max）：
    //   A(1.0, 0.9)：B 全劣于 A（包络 2>1 劣、裕量 0.5<0.9 劣）→ A 支配 B；
    //   C(2.0, 0.95)：A、C 各优一维 → 互不支配；
    //   D(1.0, 0.9)：与 A 逐元素全等 → 互不支配（"相同指标向量……互不
    //   支配"，§7.4）。
    const FeasibleCandidate a = makeCandidate(0x01, 1.0, 0.9);
    const FeasibleCandidate b = makeCandidate(0x02, 2.0, 0.5);
    const FeasibleCandidate c = makeCandidate(0x03, 2.0, 0.95);
    const FeasibleCandidate d = makeCandidate(0x04, 1.0, 0.9);

    EXPECT_TRUE(dominates(a, b, objs));
    EXPECT_FALSE(dominates(b, a, objs));
    EXPECT_FALSE(dominates(a, c, objs));
    EXPECT_FALSE(dominates(c, a, objs));
    EXPECT_FALSE(dominates(a, d, objs)) << "全等互不支配（§7.4）";
    EXPECT_FALSE(dominates(d, a, objs));
}

/// AT-09/§7.4 容差支配：显式容差下近似相等（比较容差内）互不支配；同
/// 输入零容差下严格支配——容差只经显式配置生效（不发明默认阈值）。
TEST(OptPareto, DominatesToleranceExplicitConfig)
{
    IRD_TEST_INFO("OPT-04", {"AT-09"}, std::nullopt);
    // 近似相等对：包络 1.000 与 1.008（差 0.008 ≤ ε_abs 0.01）。
    const FeasibleCandidate near1 = makeCandidate(0x11, 1.000, 0.9);
    const FeasibleCandidate near2 = makeCandidate(0x12, 1.008, 0.9);

    // 零容差目标集：1.000 < 1.008 严格优 + 裕量相等 → near1 支配 near2。
    const ObjectiveSet strict = makeTwoObjectives();
    EXPECT_TRUE(dominates(near1, near2, strict));
    EXPECT_FALSE(dominates(near2, near1, strict));

    // 显式 ε_abs=0.01 目标集：包络差在容差内（closeWithin 退化 ε_abs——
    // 零参考不涉）⇒ 双向"不劣于"且无一维超容差严格优 ⇒ 互不支配。
    const ObjectiveSet tolerant = makeObjectiveSet(
        {{MetricId::Envelope, {0.0, 0.01}},
         {MetricId::MinJointMargin, {0.0, 0.01}}});
    EXPECT_FALSE(dominates(near1, near2, tolerant)) << "容差内近似相等互不支配";
    EXPECT_FALSE(dominates(near2, near1, tolerant));

    // 同一容差下超出容差的优仍是支配（容差不是"全部相等化"）：包络差
    // 0.5 > ε_abs 0.01 → 弱者被支配。
    const FeasibleCandidate far = makeCandidate(0x13, 1.500, 0.9);
    EXPECT_TRUE(dominates(near1, far, tolerant));
}

/// §7.4 定义式方向对称性（max 目标——裕量大者优）与容差参考元语义
/// （closeWithin(aᵢ, bᵢ, tᵢ) 字面：相对项锚定被比较方 b）。
TEST(OptPareto, DominatesMaxDirectionAndToleranceReference)
{
    IRD_TEST_INFO("OPT-04", {"AT-09"}, std::nullopt);
    const ObjectiveSet objs = makeTwoObjectives();
    // 裕量（max）0.9 vs 0.5：0.9 优；包络相等 → 高裕量者支配。
    const FeasibleCandidate highMargin = makeCandidate(0x21, 1.0, 0.9);
    const FeasibleCandidate lowMargin = makeCandidate(0x22, 1.0, 0.5);
    EXPECT_TRUE(dominates(highMargin, lowMargin, objs));
    EXPECT_FALSE(dominates(lowMargin, highMargin, objs));

    // 相对容差参考元（min 目标、ε_rel=0.05、ε_abs=0）：大值差异容差更大
    // ——closeWithin(1.04, 2.00, {0.05,0})＝|1.04−2|＝0.96 > 0.05×2＝0.1
    // → false；closeWithin(1.96, 2.00, {0.05,0})＝0.04 ≤ 0.1 → true。
    const ObjectiveSet rel = makeObjectiveSet(
        {{MetricId::Envelope, {0.05, 0.0}}, {MetricId::MinJointMargin, {}}});
    const FeasibleCandidate refBig = makeCandidate(0x23, 2.00, 0.9);
    const FeasibleCandidate closeSmall = makeCandidate(0x24, 1.96, 0.9);
    const FeasibleCandidate farSmall = makeCandidate(0x25, 1.04, 0.9);
    EXPECT_FALSE(dominates(refBig, closeSmall, rel))
        << "4% 相对差在 ε_rel 5% 内（参考元＝b＝2.00）→ 互不支配";
    // 超出容差的优仍是支配：farSmall(1.04) 对 refBig(2.00) 包络严格优
    // （差 0.96 > 0.05×2.00＝0.10）且裕量不劣 → farSmall 支配 refBig；
    // 反向 refBig 包络劣且差超容差 → 不劣不成立。
    EXPECT_TRUE(dominates(farSmall, refBig, rel))
        << "48% 相对差超出容差 → 优方支配成立";
    EXPECT_FALSE(dominates(refBig, farSmall, rel));
}

// =====================================================================
// buildFront（分层/去重/稳定排序/校验——经接口消费）
// =====================================================================

/// OPT-VER-131：稳定排序三键（rank 升序 → 激活目标声明序严格字典序 →
/// CandidateId 终键）＋与输入顺序无关（乱序输入/打乱重跑输出相同——
/// NFR-COR-02；"与线程数无关"由纯单线程纯函数实现承载，见 Pareto.hpp
/// 文件头注③）。
TEST(OptPareto, BuildFrontStableOrderInputOrderIndependent)
{
    IRD_TEST_INFO("NFR-COR-02", {"AT-09"}, std::nullopt);
    // 五候选场景（包络↓min / 裕量↑max，零容差）：
    //   P1(1.0, 0.9)——第一前沿（无支配者）；
    //   P2(2.0, 0.95)——第一前沿（与 P1 互不支配）；
    //   P3(1.5, 0.7)——仅被 P1 支配（1.5>1.0 且 0.7<0.9）→ 第二层；
    //   P4(1.5, 0.7) 同向量不同身份——与 P3 互不支配 → 同第二层；
    //   P5(2.0, 0.4)——被 P1 支配，且在第二层内被 P3 支配（1.5≤2.0 且
    //   0.7>0.4 严格优）→ 级联落第三层（逐层筛选语义的直接观测）。
    // 层内排序键：rank 1 组内 P3/P4 包络 1.5<2.0(P5) → P3/P4 先；
    // P3 vs P4 指标全等 → 键③身份字节 0x33<0x34 → P3 先。
    // 期望全序：P1(0x31), P2(0x32), P3(0x33), P4(0x34), P5(0x35)。
    const FeasibleCandidate p1 = makeCandidate(0x31, 1.0, 0.9);
    const FeasibleCandidate p2 = makeCandidate(0x32, 2.0, 0.95);
    const FeasibleCandidate p3 = makeCandidate(0x33, 1.5, 0.7);
    const FeasibleCandidate p4 = makeCandidate(0x34, 1.5, 0.7);
    const FeasibleCandidate p5 = makeCandidate(0x35, 2.0, 0.4);
    const ObjectiveSet objs = makeTwoObjectives();

    const ParetoFrontBuilder builder;
    const ParetoFrontResult r1 =
        builder.buildFront({p5, p3, p1, p4, p2}, objs);
    // 期望序提取为局部变量（gtest 宏参数内禁用花括号初始化列表）。
    const std::vector<CandidateId> expected = {makeCid(0x31), makeCid(0x32),
                                               makeCid(0x33), makeCid(0x34),
                                               makeCid(0x35)};
    EXPECT_EQ(idsOf(r1), expected) << "三键全序（rank→目标序→CandidateId）";
    // rank 断言（分层语义——P5 级联落第三层）。
    EXPECT_EQ(r1.entries[0].nondominationRank, 0U);
    EXPECT_EQ(r1.entries[1].nondominationRank, 0U);
    EXPECT_EQ(r1.entries[2].nondominationRank, 1U);
    EXPECT_EQ(r1.entries[3].nondominationRank, 1U);
    EXPECT_EQ(r1.entries[4].nondominationRank, 2U);
    // 非支配集＝rank 0 子集（双标记显式面）。
    ASSERT_EQ(r1.nondominatedIds.size(), 2U);
    EXPECT_EQ(r1.nondominatedIds[0], makeCid(0x31));
    EXPECT_EQ(r1.nondominatedIds[1], makeCid(0x32));
    ASSERT_EQ(r1.feasibleIds.size(), 5U) << "可行集＝去重后全体";
    for (const auto& e : r1.entries) {
        EXPECT_EQ(e.paretoNondominated, e.nondominationRank == 0U);
    }

    // 输入顺序打乱重跑 ⇒ 输出逐字段相同（确定性——同输入同输出；
    // "与线程数无关"由纯函数实现承载——无共享状态无并行归约）。
    const ParetoFrontResult r2 =
        builder.buildFront({p2, p4, p1, p5, p3}, objs);
    EXPECT_EQ(r2.entries.size(), r1.entries.size());
    for (std::size_t i = 0; i < r1.entries.size(); ++i) {
        EXPECT_EQ(r2.entries[i].candidateId, r1.entries[i].candidateId);
        EXPECT_EQ(r2.entries[i].nondominationRank, r1.entries[i].nondominationRank);
        EXPECT_EQ(r2.entries[i].paretoNondominated, r1.entries[i].paretoNondominated);
        EXPECT_EQ(r2.entries[i].isBaseline, r1.entries[i].isBaseline);
    }
}

/// OPT-VER-133：重复候选去重——同 CandidateId 双生成 → 单候选保留首次＋
/// duplicatesDropped=1（审计计数；保留"首次"＝输入序首见——用不同指标
/// 向量区分首见与重复）。
TEST(OptPareto, BuildFrontDedupKeepsFirstOccurrence)
{
    IRD_TEST_INFO("OPT-04", {"AT-09"}, std::nullopt);
    // 同身份（0x41）两个不同指标向量的条目——内容寻址身份下这属上游
    // 一致性缺陷形态，去重规约只按身份：保留输入序首见（0x41 首见为
    // "好"向量 1.0/0.9），重复（2.0/0.4）丢弃并计数。
    FeasibleCandidate first = makeCandidate(0x41, 1.0, 0.9);
    const FeasibleCandidate dup = makeCandidate(0x41, 2.0, 0.4);
    const FeasibleCandidate other = makeCandidate(0x42, 2.0, 0.95);

    const ParetoFrontBuilder builder;
    const ParetoFrontResult r = builder.buildFront({first, dup, other},
                                                   makeTwoObjectives());
    EXPECT_EQ(r.entries.size(), 2U) << "去重后单候选";
    EXPECT_EQ(r.duplicatesDropped, 1U) << "dedupCount 入审计（§7.4）";
    // 期望序提取为局部变量（gtest 宏参数内禁用花括号初始化列表——逗号
    // 会被宏当作参数分隔符）。
    const std::vector<CandidateId> expectedFirstKept{makeCid(0x41),
                                                     makeCid(0x42)};
    EXPECT_EQ(idsOf(r), expectedFirstKept) << "保留首见（好向量），重复被丢";
    EXPECT_EQ(r.entries[0].nondominationRank, 0U) << "首见向量参与分层";
}

/// 分层收敛形态：三层级联链（A 支配 B 支配 C——A 不支配 C 的构造）验证
/// rank 逐层递进（非支配 rank 升序键的分层语义锚）。
TEST(OptPareto, BuildFrontLayeredRanksCascade)
{
    IRD_TEST_INFO("OPT-04", {}, std::nullopt);
    // 级联（仅包络 min 一维目标最直观）：3.0 → 2.0 → 1.0 各层一员。
    const ObjectiveSet one = makeObjectiveSet({{MetricId::Envelope, {}}});
    const FeasibleCandidate worst = makeCandidate(0x51, 3.0, 0.0);
    const FeasibleCandidate mid = makeCandidate(0x52, 2.0, 0.0);
    const FeasibleCandidate best = makeCandidate(0x53, 1.0, 0.0);

    const ParetoFrontBuilder builder;
    const ParetoFrontResult r = builder.buildFront({worst, mid, best}, one);
    const std::vector<CandidateId> expectedCascade{makeCid(0x53), makeCid(0x52),
                                                   makeCid(0x51)};
    EXPECT_EQ(idsOf(r), expectedCascade);
    EXPECT_EQ(r.entries[0].nondominationRank, 0U);
    EXPECT_EQ(r.entries[1].nondominationRank, 1U);
    EXPECT_EQ(r.entries[2].nondominationRank, 2U);
    EXPECT_EQ(r.nondominatedIds.size(), 1U);
}

/// P-OPT-5/DOPT-5：默认零容差＝浮点全序（浮点近邻严格支配——近邻差不
/// 因默认容差被吞；"不发明阈值"）。经接口指针消费（虚函数路径钉住）。
TEST(OptPareto, DefaultZeroToleranceKeepsFloatTotalOrder)
{
    IRD_TEST_INFO("OPT-04", {"AT-09"}, std::nullopt);
    const double base = 1.0;
    const double nextAfter = std::nextafter(base, 10.0);  // base 的相邻浮点
    const FeasibleCandidate tiny = makeCandidate(0x61, base, 0.9);
    const FeasibleCandidate slightlyWorse = makeCandidate(0x62, nextAfter, 0.9);
    EXPECT_LT(base, nextAfter) << "近邻浮点严格递增";

    // 默认（零容差）目标集：近邻差也构成严格优 → tiny 支配。
    const IParetoFrontBuilder& builder = ParetoFrontBuilder{};
    const ParetoFrontResult r =
        builder.buildFront({slightlyWorse, tiny}, makeTwoObjectives());
    ASSERT_EQ(r.entries.size(), 2U);
    EXPECT_EQ(r.entries[0].candidateId, makeCid(0x61)) << "近邻严格序保持";
    EXPECT_EQ(r.entries[0].nondominationRank, 0U);
    EXPECT_EQ(r.entries[1].nondominationRank, 1U);
    EXPECT_EQ(r.duplicatesDropped, 0U);

    // 声明序影响排序键②（同 rank 时）：p3(1.5,0.7) 支配 p5(2.0,0.4)
    // （裕量 0.7>0.4 严格优＋包络 1.5≤2.0 不劣）→ p3 rank 0 在前、
    // p5 rank 1——分层与声明序无关，排序键②在同 rank 层内生效。
    const ObjectiveSet marginFirst = makeObjectiveSet(
        {{MetricId::MinJointMargin, {}}, {MetricId::Envelope, {}}});
    const FeasibleCandidate p3 = makeCandidate(0x63, 1.5, 0.7);
    const FeasibleCandidate p5 = makeCandidate(0x64, 2.0, 0.4);
    const ParetoFrontResult ra = ParetoFrontBuilder{}.buildFront({p3, p5},
                                                                 marginFirst);
    // 裕量声明在前：层内键②先比裕量（max 方向大者先）——p3 仍先；
    // rank 面与包络声明序时一致（支配关系不随声明序变化）。
    EXPECT_EQ(ra.entries[0].candidateId, makeCid(0x63));
    EXPECT_EQ(ra.entries[0].nondominationRank, 0U);
    EXPECT_EQ(ra.entries[1].nondominationRank, 1U);
}

/// buildFront 校验面（@pre 防线）：空目标集/激活指标缺失（"—"候选——
/// §7.4 缺失指标不参与支配比较的实现位置）/非有限值 → OPT-INPUT-INVALID。
TEST(OptPareto, BuildFrontRejectsContractViolations)
{
    IRD_TEST_INFO("OPT-07", {"AT-09"}, std::nullopt);
    const ParetoFrontBuilder builder;

    // 空目标集（无支配比较基础）。
    try {
        (void)builder.buildFront({makeCandidate(0x71, 1.0, 0.9)}, ObjectiveSet{});
        FAIL() << "空目标集应抛";
    } catch (const OptimizationError& e) {
        EXPECT_EQ(e.stableCode(), kOptInputInvalid);
    }

    // "—"候选未在管线侧排除：激活目标槽位 nullopt → 拒绝（§7.4 缺失
    // 指标不参与支配——管线侧 DataInsufficient 排除的防线，acceptance 2
    // "不参与 Pareto"的执行证明点）。
    FeasibleCandidate withGap = makeCandidate(0x72, 1.0, 0.9);
    withGap.metricValues[static_cast<std::size_t>(MetricId::Envelope)]
        = std::nullopt;
    try {
        (void)builder.buildFront({withGap}, makeTwoObjectives());
        FAIL() << "激活目标缺失候选应抛（'—'不参与 Pareto——@pre 防线）";
    } catch (const OptimizationError& e) {
        EXPECT_EQ(e.stableCode(), kOptInputInvalid);
    }

    // 非有限值（NaN）→ 拒绝（NFR-COR-03：不静默比较/不静默转 0）。
    const FeasibleCandidate nanVal =
        makeCandidate(0x73, std::nan(""), 0.9);
    try {
        (void)builder.buildFront({nanVal}, makeTwoObjectives());
        FAIL() << "非有限指标应抛";
    } catch (const OptimizationError& e) {
        EXPECT_EQ(e.stableCode(), kOptInputInvalid);
    }

    // 槽数非法（≠8）→ 拒绝（形状契约）。
    FeasibleCandidate badShape = makeCandidate(0x74, 1.0, 0.9);
    badShape.metricValues.pop_back();
    try {
        (void)builder.buildFront({badShape}, makeTwoObjectives());
        FAIL() << "槽数非八应抛";
    } catch (const OptimizationError& e) {
        EXPECT_EQ(e.stableCode(), kOptInputInvalid);
    }
}

/// NFR-COR-02：同输入重复调用输出逐字段相等（确定性——重复运行一致；
/// OPT-VER-130 同种子确定性的 Pareto 面观测）＋基线标记透传（§8.2 基线
/// 参与 Pareto）。
TEST(OptPareto, BuildFrontDeterministicAndBaselineFlag)
{
    IRD_TEST_INFO("NFR-COR-02", {"AT-09"}, std::nullopt);
    const FeasibleCandidate baseline = makeCandidate(0x81, 1.2, 0.8, true);
    const FeasibleCandidate better = makeCandidate(0x82, 1.0, 0.9, false);
    const ObjectiveSet objs = makeTwoObjectives();

    const ParetoFrontBuilder builder;
    const ParetoFrontResult r1 = builder.buildFront({baseline, better}, objs);
    const ParetoFrontResult r2 = builder.buildFront({baseline, better}, objs);
    ASSERT_EQ(r1.entries.size(), r2.entries.size());
    for (std::size_t i = 0; i < r1.entries.size(); ++i) {
        EXPECT_EQ(r1.entries[i].candidateId, r2.entries[i].candidateId);
        EXPECT_EQ(r1.entries[i].isBaseline, r2.entries[i].isBaseline);
        EXPECT_EQ(r1.entries[i].nondominationRank, r2.entries[i].nondominationRank);
        EXPECT_EQ(r1.entries[i].paretoNondominated, r2.entries[i].paretoNondominated);
    }
    EXPECT_EQ(r1.duplicatesDropped, r2.duplicatesDropped);
    // 基线标记透传（基线候选作为可行方案之一参与 Pareto——§8.2）。
    EXPECT_TRUE(r1.entries[1].isBaseline);
    EXPECT_EQ(r1.feasibleIds.size(), 2U);
}
