/**
 * @file   SolutionSetViewTest.cpp
 * @brief  解集视图消费面用例组（KinSolutionSetView；WP-15-T09）——
 *         规范筛选谓词（可用解/含碰撞诊断解）的比较集中行为、排序面
 *         复用与已排序集合幂等（V-04）、并发只读安全（§6.2/§9.4）。
 *
 * 设计依据：
 *   - units/kinematics.md §6.2（IKinematicSolutionSet 视图契约——筛选/
 *     排序/统计唯一实现点；"不可变操作返回新视图"）、§6.3（四键稳定
 *     排序——浮点全序＋稳定键兜底；已排序集合上幂等）、§9.4（插件会话/
 *     归档读取期持有、UI 线程只读消费——并发只读安全的契约出处）
 *   - REQUIREMENTS NFR-MNT-04（UI/报告不各自实现比较）、KIN-05（碰撞
 *     证据缺失绝不解读为无碰撞——可用解谓词的保守面依据）
 *   - 任务契约 tasks/foundation/WP-15-T09.json acceptance 1（具名对应见
 *     各用例 IRD_TEST_INFO 与用例名 _ACC1 段）
 */

#include <sdurws/ird/kinematics/SolutionSet.hpp>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记

#include <gtest/gtest.h>

#include <cstdint>
#include <thread>
#include <vector>

using namespace sdurws::ird::kinematics;

namespace {

// =====================================================================
// 测试夹具：手排解值（零求解依赖——视图面行为与本源解耦）
// =====================================================================

/// 手工构造一个解（参数与求解无关——仅承载排序键/碰撞标记字段）。
KinematicSolution handSolution(std::vector<double> q, double minMargin,
                               double manipulability, std::uint32_t initIndex)
{
    KinematicSolution s;
    s.q = std::move(q);
    s.minimumJointMargin = minMargin;
    s.manipulability = manipulability;
    s.sourceInitIndex = initIndex;
    return s;
}

/// 两解序列的逐字段全等（q 逐位＋init 序——排序面复用断言的比较器）。
bool sameSolution(const KinematicSolution& a, const KinematicSolution& b)
{
    return a.q == b.q && a.sourceInitIndex == b.sourceInitIndex
        && a.minimumJointMargin == b.minimumJointMargin;
}

/// 混合碰撞标记的四解集（本组共用夹具）：
///   init0＝已评价＋无碰撞（正面可用凭据）；
///   init1＝碰撞未评价（策略未启用——证据缺失，KIN-05）；
///   init2＝已评价＋碰撞标记（合并诊断呈现形态——生产端会把它移入
///          filteredRecords，此处手工承载以验证谓词比较本身）；
///   init3＝已评价＋无碰撞（第二可用解）。
IkSolutionSet mixedCollisionSet()
{
    IkSolutionSet set;
    set.requestIdentity.referenceQ = {0.0, 0.0};
    set.solutions.push_back(handSolution({0.1, 0.1}, 0.9, 1.0, 0U));
    set.solutions[0].collisionStatus.evaluated = true;
    set.solutions[0].collisionStatus.inCollision = false;
    set.solutions.push_back(handSolution({0.2, 0.2}, 0.8, 1.0, 1U));
    set.solutions[1].collisionStatus.evaluated = false;  // 证据缺失
    set.solutions[1].collisionStatus.inCollision = false;
    set.solutions.push_back(handSolution({0.3, 0.3}, 0.7, 1.0, 2U));
    set.solutions[2].collisionStatus.evaluated = true;
    set.solutions[2].collisionStatus.inCollision = true;  // 碰撞诊断标记
    set.solutions.push_back(handSolution({0.4, 0.4}, 0.6, 1.0, 3U));
    set.solutions[3].collisionStatus.evaluated = true;
    set.solutions[3].collisionStatus.inCollision = false;
    return set;
}

}  // namespace

// ---------------------------------------------------------------------
// ACC1-1：可用解谓词——只选"碰撞证据完备且无碰撞"的解（KIN-05 保守面）
// ---------------------------------------------------------------------

TEST(KinSolutionSetView, UsablePredicateSelectsConfirmedCollisionFree_WP15T09_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-08", "NFR-MNT-04", "KIN-05"},
                  std::vector<std::string>{});

    const IkSolutionSet set = mixedCollisionSet();
    const KinematicSolutionSet view(set);

    // 规范谓词取自唯一实现点（NFR-MNT-04——UI/报告不各自书写比较式）。
    const auto usable = view.filtered(usableSolutionPredicate());
    ASSERT_EQ(usable.size(), 2U)
        << "四解中仅两解有正面无碰撞判定：init1 证据缺失不入可用集"
           "（KIN-05：不得当作可行凭据），init2 携带碰撞标记";
    EXPECT_EQ(usable[0].sourceInitIndex, 0U);
    EXPECT_EQ(usable[1].sourceInitIndex, 3U);

    // 筛选保持 sorted() 序的子序列（init0 < init3——四键排序后相对次序
    // 不变；全序列 {0,1,2,3} 的子序列 {0,3}）。
    const auto& sorted = view.sorted();
    std::vector<std::size_t> positions;
    for (const KinematicSolution& u : usable) {
        for (std::size_t i = 0; i < sorted.size(); ++i) {
            if (sorted[i].sourceInitIndex == u.sourceInitIndex) {
                positions.push_back(i);
                break;
            }
        }
    }
    ASSERT_EQ(positions.size(), 2U);
    EXPECT_LT(positions[0], positions[1]) << "筛选结果须为稳定序子序列";
}

// ---------------------------------------------------------------------
// ACC1-2：碰撞诊断谓词——只选携带碰撞标记的解；生产端形态上恒空集
// ---------------------------------------------------------------------

TEST(KinSolutionSetView, CollisionDiagnosticPredicateSelectsMarked_WP15T09_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-08", "NFR-MNT-04"},
                  std::vector<std::string>{});

    const IkSolutionSet set = mixedCollisionSet();
    const KinematicSolutionSet view(set);

    // 混合诊断呈现形态（init2 携带 inCollision 标记）：谓词恰好选中它。
    const auto diagnostic = view.filtered(collisionDiagnosticPredicate());
    ASSERT_EQ(diagnostic.size(), 1U);
    EXPECT_EQ(diagnostic[0].sourceInitIndex, 2U);
    EXPECT_TRUE(diagnostic[0].collisionStatus.inCollision);

    // 生产端不变式（§6.1——碰撞解移入 filteredRecords）：求解器直接产出
    // 的解集上（全部解无碰撞标记）谓词恒空集——比较实现集中在此，供合并
    // 呈现与语义演进复用（NFR-MNT-04：禁止第二份 inCollision 比较式）。
    IkSolutionSet solverShaped = mixedCollisionSet();
    for (KinematicSolution& s : solverShaped.solutions) {
        s.collisionStatus.inCollision = false;  // 模拟"碰撞解已移出"的生产形态
    }
    const KinematicSolutionSet solverView(solverShaped);
    EXPECT_TRUE(solverView.filtered(collisionDiagnosticPredicate()).empty())
        << "生产端形态（碰撞解已入 filteredRecords）上诊断谓词为空集";
}

// ---------------------------------------------------------------------
// ACC1-3：排序面复用＋已排序集合幂等（V-04——视图排序与唯一实现点
// sortSolutions 同语义；重复排序逐位一致）
// ---------------------------------------------------------------------

TEST(KinSolutionSetView, SortedIdempotentAndReusesSortSurface_WP15T09_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-02", "NFR-COR-02", "KIN-08"},
                  std::vector<std::string>{});

    // 构造序故意打乱（margin/manip/距离三键交错——四键裁决见 T04 黄金
    // 用例，此处关注"视图与唯一实现点同序"而非键序本身）。
    IkSolutionSet set;
    set.requestIdentity.referenceQ = {0.0, 0.0};
    set.solutions.push_back(handSolution({1.0, 1.0}, 0.3, 0.9, 3U));
    set.solutions.push_back(handSolution({0.2, 0.2}, 0.3, 0.9, 8U));
    set.solutions.push_back(handSolution({0.5, 0.5}, 0.5, 0.1, 1U));
    set.solutions.push_back(handSolution({0.4, 0.4}, 0.3, 0.1, 2U));
    set.solutions.push_back(handSolution({0.2, 0.2}, 0.3, 0.9, 5U));

    // 面 A：视图构造排序（构造时完成一次稳定排序——§9.2 @pre）。
    const KinematicSolutionSet view(set);
    const auto& viaView = view.sorted();

    // 面 B：同一输入经唯一实现点 sortSolutions（视图与求解管线共享的
    // 排序语义——V-04 排序面复用断言：两面逐位一致）。
    IkSolutionSet copy = set;
    sortSolutions(copy.solutions, copy.requestIdentity.referenceQ);
    ASSERT_EQ(viaView.size(), copy.solutions.size());
    for (std::size_t i = 0; i < viaView.size(); ++i) {
        EXPECT_TRUE(sameSolution(viaView[i], copy.solutions[i]))
            << "视图排序须与唯一实现点逐位一致（i=" << i << "）";
    }

    // 幂等（已排序集合上）：对排序产物再次排序（唯一实现点）逐位不变；
    // 以排序产物再构造视图，sorted() 亦逐位不变（构造路径的幂等面）。
    sortSolutions(copy.solutions, copy.requestIdentity.referenceQ);
    for (std::size_t i = 0; i < viaView.size(); ++i) {
        EXPECT_TRUE(sameSolution(viaView[i], copy.solutions[i]))
            << "已排序集合上重复排序逐位一致（V-04 幂等前提，i=" << i << "）";
    }
    IkSolutionSet resorted = set;
    resorted.solutions = viaView;  // 已排序输入
    const KinematicSolutionSet viewOfSorted(resorted);
    ASSERT_EQ(viewOfSorted.sorted().size(), viaView.size());
    for (std::size_t i = 0; i < viaView.size(); ++i) {
        EXPECT_TRUE(sameSolution(viewOfSorted.sorted()[i], viaView[i]))
            << "已排序输入构造视图不改变次序（幂等——i=" << i << "）";
    }
}

// ---------------------------------------------------------------------
// ACC1-4：筛选幂等与空谓词契约（filtered 输出再筛＝原筛；空谓词＝全保留）
// ---------------------------------------------------------------------

TEST(KinSolutionSetView, FilteredIdempotentWithNullPredicateContract_WP15T09_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-08", "NFR-MNT-04"},
                  std::vector<std::string>{});

    const IkSolutionSet set = mixedCollisionSet();
    const KinematicSolutionSet view(set);

    const auto first = view.filtered(usableSolutionPredicate());
    const KinematicSolutionSet firstView([&] {
        IkSolutionSet s;
        s.requestIdentity = set.requestIdentity;
        s.solutions = first;
        return s;
    }());
    const auto second = firstView.filtered(usableSolutionPredicate());
    ASSERT_EQ(first.size(), second.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        EXPECT_TRUE(sameSolution(first[i], second[i]))
            << "已筛选集合上重复筛选幂等（i=" << i << "）";
    }

    // 空谓词＝全保留（§6.2 @param 契约——filtered 的缺省语义）。
    const SolutionPredicate nullPredicate;
    const auto all = view.filtered(nullPredicate);
    ASSERT_EQ(all.size(), view.sorted().size());
    for (std::size_t i = 0; i < all.size(); ++i) {
        EXPECT_TRUE(sameSolution(all[i], view.sorted()[i]));
    }
}

// ---------------------------------------------------------------------
// ACC1-5：并发只读安全（§9.4——构造后不可变；多线程重复读取结果全等）
// ---------------------------------------------------------------------

TEST(KinSolutionSetView, ConcurrentReadOnlyAccessAgrees_WP15T09_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-08", "KIN-05"},
                  std::vector<std::string>{});

    const IkSolutionSet set = mixedCollisionSet();
    const KinematicSolutionSet view(set);

    // 基准结果（单线程参照——并发读取的期望值）。
    const std::size_t baseSortedSize = view.sorted().size();
    const auto baseWorst = view.worstBy(WorstMetric::MinimumJointMargin);
    const auto baseUsable = view.filtered(usableSolutionPredicate());

    // 4 线程 × 200 轮并发只读（sorted/filtered/worstBy/statistics 混合
    // 调用）——任何数据竞争都会以尺寸/下标错位显形。
    constexpr int kThreads = 4;
    constexpr int kRounds = 200;
    std::vector<std::thread> workers;
    std::vector<bool> agreed(kThreads, true);
    for (int t = 0; t < kThreads; ++t) {
        // 捕获面：view/agreed/base* 以引用共享只读；kRounds 以值捕获
        // （MSVC 对 lambda 内 odr 使用的 constexpr 局部量要求显式捕获）。
        workers.emplace_back(
            [&view, &agreed, t, baseSortedSize, baseWorst, &baseUsable, kRounds]() {
            for (int r = 0; r < kRounds; ++r) {
                if (view.sorted().size() != baseSortedSize
                    || view.statistics().dedupedCount != 0U
                    || view.filtered(usableSolutionPredicate()).size()
                           != baseUsable.size()) {
                    agreed[static_cast<std::size_t>(t)] = false;
                    return;
                }
                const auto w = view.worstBy(WorstMetric::MinimumJointMargin);
                if (w.has_value() != baseWorst.has_value()
                    || (w.has_value()
                        && w->solutionIndex != baseWorst->solutionIndex)) {
                    agreed[static_cast<std::size_t>(t)] = false;
                    return;
                }
            }
        });
    }
    for (std::thread& w : workers) {
        w.join();
    }
    for (const bool ok : agreed) {
        EXPECT_TRUE(ok) << "并发只读观察与单线程基准不一致（§9.4 契约）";
    }

    // 并发后单线程复验：视图内容未被并发访问扰动（不可变视图）。
    ASSERT_EQ(view.sorted().size(), baseSortedSize);
    const auto worstAfter = view.worstBy(WorstMetric::MinimumJointMargin);
    ASSERT_TRUE(worstAfter.has_value());
    EXPECT_EQ(worstAfter->solutionIndex, baseWorst->solutionIndex);
}
