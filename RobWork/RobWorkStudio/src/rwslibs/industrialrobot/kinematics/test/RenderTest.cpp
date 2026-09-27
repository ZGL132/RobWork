/**
 * @file   RenderTest.cpp
 * @brief  失败点/薄弱区三维渲染数据用例组（KIN-07，T11 批）——四类数据
 *         结构组装、统一状态词投影、会话回写与选中联动数据的逐条自证。
 *
 * 设计依据：
 *   - units/kinematics.md §9.8（KIN-07 数据本单元产）、§6.2（worstBy
 *     规范来源）、§7.1（per-item 状态）、§4.5（回写身份外）、§7.3
 *     （KIN-09 R2 边界——本组不含任何点云/投影/PNG 用例，边界以
 *     "无占位实现＋零用例需求"呈现）
 *   - policy.md §4.4/P-POL-2（阈值显式不适用）、D-08（边界含于合规侧）
 *   - 任务契约 tasks/foundation/WP-15-T11.json acceptance 1~4（用例名
 *     _ACCn 段逐条对应）
 *
 * 确定性：全部组装函数同输入同输出——重复组装逐字段比对（NFR-COR-01）。
 */

#include "KinFkFixture.hpp"

#include <sdurws/ird/kinematics/Render.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求追溯登记

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace core = sdurws::ird::core;
namespace policy = sdurws::ird::policy;
namespace kin = sdurws::ird::kinematics;
using sdurws::ird::kinematics::testfixture::idFrom;

namespace {

using kin::BatchComputation;
using kin::BatchItemStatus;
using kin::BatchTaskPoint;
using kin::BatchWorkItemRecord;
using kin::KinematicSolution;
using kin::KinematicSolutionSet;
using kin::SolutionRef;
using kin::WeakZoneKind;

// =====================================================================
// 小工具（脚手架——非断言面）
// =====================================================================

/// 组装一个最小可行解（仅渲染面消费的字段——其余字段用求解器缺省值）。
KinematicSolution makeSolution(const std::string& sig, std::vector<double> margins,
                               double conditionNumber, std::uint32_t initIndex,
                               bool evaluated = true, bool inCollision = false,
                               std::vector<core::ObjectId> collisionPairs = {})
{
    KinematicSolution s;
    s.q = margins;  // 测试值——q 与 margins 同维即可（渲染面只读维度）
    double minMargin = 0.0;
    if (!margins.empty()) {
        minMargin = margins[0];
        for (const double m : margins) {
            minMargin = std::min(minMargin, m);
        }
    }
    s.jointMargins = std::move(margins);
    s.minimumJointMargin = minMargin;  // 求解器不变式：min(jointMargins)
    s.conditionNumber = conditionNumber;
    s.sourceInitIndex = initIndex;
    s.signature = sig;
    s.collisionStatus.evaluated = evaluated;
    s.collisionStatus.inCollision = inCollision;
    s.collisionStatus.objectIdPairs = std::move(collisionPairs);
    return s;
}

/// 组装一个解集值（视图构造时完成一次稳定排序——T04/T09 设施）。
kin::IkSolutionSet makeSolutionSet(std::vector<KinematicSolution> solutions,
                                   std::vector<kin::FilteredSolutionRecord> filtered = {})
{
    kin::IkSolutionSet set;
    set.targetRef.pointOid = idFrom<core::ObjectId>("kin-point-main");
    set.requestIdentity.referenceQ = {};  // 测试用空参考构型——距离键全 0
    set.solutions = std::move(solutions);
    set.filteredRecords = std::move(filtered);
    set.statistics.dedupedCount = set.solutions.size();
    set.statistics.filteredCount = set.filteredRecords.size();
    return set;
}

/// 组装一条硬过滤记录（诊断价值承载——渲染面消费其签名/原因/对象对）。
kin::FilteredSolutionRecord makeFilteredRecord(const std::string& sig,
                                               kin::SolutionFilterReason reason,
                                               std::vector<core::ObjectId> pairs = {})
{
    kin::FilteredSolutionRecord r;
    r.signature = sig;
    r.reason = reason;
    r.objectIdPairs = std::move(pairs);
    r.q = {0.1, 0.2};
    return r;
}

/// 组装一个批量工作项记录（状态＋可选最佳解/过滤记录/原因）。
BatchWorkItemRecord makeItem(const core::ObjectId& point, const core::ObjectId& condition,
                             BatchItemStatus status, std::optional<KinematicSolution> best = {},
                             std::vector<kin::FilteredSolutionRecord> filtered = {},
                             std::string reason = {})
{
    BatchWorkItemRecord item;
    item.pointOid = point;
    item.conditionId = condition;
    item.status = status;
    item.bestSolution = std::move(best);
    item.filteredRecords = std::move(filtered);
    item.reason = std::move(reason);
    if (status == BatchItemStatus::NoConvergence || status == BatchItemStatus::AllFiltered) {
        item.searchRecord = kin::IkSearchRecord{};  // T05 不变式：结局 2/3 必附
    }
    return item;
}

/// 组装批量计算值（items＋守恒统计——T05 不变式形状）。
BatchComputation makeComputation(std::vector<BatchWorkItemRecord> items)
{
    BatchComputation c;
    c.items = std::move(items);
    c.totalWorkItems = c.items.size();
    c.processedItemCount = c.totalWorkItems;
    return c;
}

/// 组装宿主解析的任务点投影（位置 x 平移区分点位——基座系 {B}）。
BatchTaskPoint makePoint(const core::ObjectId& oid, double x)
{
    BatchTaskPoint p;
    p.pointOid = oid;
    // 单位旋转用 9 元素内联构造（Rotation3D::identity() 为库导出符号，
    // 冒烟模式不可链——TaskPointsBatchTest 同款先例）。
    const rw::math::Rotation3D<double> identity(1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0);
    p.targetInBase = rw::math::Transform3D<double>(rw::math::Vector3D<double>(x, 0.0, 0.0),
                                                   identity);
    return p;
}

/// 两阈值全在场的策略阈值投影（0.3 近限位比／4.0 条件数——测试锚值）。
kin::WeakZoneThresholds testThresholds()
{
    kin::WeakZoneThresholds t;
    t.nearLimitRatio = 0.3;
    t.conditionNumberWarning = 4.0;
    return t;
}

}  // namespace

// =====================================================================
// acceptance 1——四类数据结构与组装（worstBy 来源／ObjectId 标注）
// =====================================================================

/// 批量失败点组装：四失败态入集、NotApplicable/NotRun 排除并计数、
/// 守恒不变式成立（KIN-07＋§7.1——acceptance 1/3 交界的主用例）。
TEST(KinRender, BatchFailurePointsCoverFourKindsAndConserveCounts_WP15T11_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-07"}, std::vector<std::string>{});

    const auto c1 = idFrom<core::ObjectId>("kin-cond-1");
    const auto pA = idFrom<core::ObjectId>("kin-pA");
    const auto pB = idFrom<core::ObjectId>("kin-pB");
    const auto pC = idFrom<core::ObjectId>("kin-pC");
    const auto pD = idFrom<core::ObjectId>("kin-pD");
    const auto pE = idFrom<core::ObjectId>("kin-pE");
    const auto pF = idFrom<core::ObjectId>("kin-pF");
    const auto pG = idFrom<core::ObjectId>("kin-pG");

    BatchComputation computation = makeComputation({
        makeItem(pA, c1, BatchItemStatus::CandidateFound,
                 makeSolution("sig-best", {0.2}, 1.5, 0)),                         // 有解
        makeItem(pB, c1, BatchItemStatus::NoConvergence),                          // 失败 1
        makeItem(pC, c1, BatchItemStatus::BoundExceeded),                          // 失败 2
        makeItem(pD, c1, BatchItemStatus::InputInvalid, {}, {}, {"点引用悬空"}),   // 失败 3
        makeItem(pE, c1, BatchItemStatus::AllFiltered),                            // 失败 4
        makeItem(pF, c1, BatchItemStatus::NotApplicable, {}, {}, {"点停用"}),      // 排除
        makeItem(pG, c1, BatchItemStatus::NotRun, {}, {}, {"批取消"}),             // 排除
    });

    // 链序列表：单关节（与有解项的最佳解 margins 同维——调用方契约）。
    const std::vector<core::ObjectId> chain = {idFrom<core::ObjectId>("kin-j0")};
    const kin::BatchRenderData data = kin::assembleBatchRenderData(
        computation, {}, chain, testThresholds(), core::TaskState::Completed);

    // 失败点＝恰四条（四失败态），序随 items 全序；种类逐条如实。
    ASSERT_EQ(data.failurePoints.points.size(), 4u);
    EXPECT_EQ(data.failurePoints.points[0].failureKind, BatchItemStatus::NoConvergence);
    EXPECT_EQ(data.failurePoints.points[1].failureKind, BatchItemStatus::BoundExceeded);
    EXPECT_EQ(data.failurePoints.points[2].failureKind, BatchItemStatus::InputInvalid);
    EXPECT_EQ(data.failurePoints.points[3].failureKind, BatchItemStatus::AllFiltered);
    // 排除计数分列（acceptance 3 如实区分——不合并）。
    EXPECT_EQ(data.failurePoints.candidateFoundCount, 1u);
    EXPECT_EQ(data.failurePoints.notApplicableItemCount, 1u);
    EXPECT_EQ(data.failurePoints.notRunItemCount, 1u);
    // 守恒不变式：4+1+1+1 == 7（每工作项恰归一类——ERR-01 零静默丢弃）。
    EXPECT_EQ(data.failurePoints.points.size() + data.failurePoints.candidateFoundCount
                  + data.failurePoints.notApplicableItemCount + data.failurePoints.notRunItemCount,
              computation.items.size());
    // 失败点的工作项下标可回投（检查器跳转键——L-K1）。
    EXPECT_EQ(data.failurePoints.points[0].workItemIndex, 1u);
    EXPECT_EQ(data.failurePoints.points[3].workItemIndex, 4u);
}

/// 失败点位置解析：已解析点取宿主投影位姿；悬空点如实缺位（不虚构坐标
/// ——ARC-04；位置为基座系 {B}）。
TEST(KinRender, FailurePointPositionLookupAndDanglingFallback_WP15T11_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-07"}, std::vector<std::string>{});

    const auto c1 = idFrom<core::ObjectId>("kin-cond-1");
    const auto pResolved = idFrom<core::ObjectId>("kin-pr");
    const auto pDangling = idFrom<core::ObjectId>("kin-pd");

    BatchComputation computation = makeComputation({
        makeItem(pResolved, c1, BatchItemStatus::NoConvergence),
        makeItem(pDangling, c1, BatchItemStatus::InputInvalid, {}, {}, {"悬空"}),
    });
    const std::vector<BatchTaskPoint> resolved = {makePoint(pResolved, 1.25)};

    const kin::BatchRenderData data = kin::assembleBatchRenderData(
        computation, resolved, {}, testThresholds(), core::TaskState::Completed);

    ASSERT_EQ(data.failurePoints.points.size(), 2u);
    ASSERT_TRUE(data.failurePoints.points[0].targetInBase.has_value());
    // 已解析点：位姿平移＝宿主投影原值（{B} 系，m）。
    EXPECT_DOUBLE_EQ(data.failurePoints.points[0].targetInBase->P()[0], 1.25);
    // 悬空点：nullopt——无可渲染位置，不虚构。
    EXPECT_FALSE(data.failurePoints.points[1].targetInBase.has_value());
}

/// 薄弱区·近限位：阈值读 policy（经 weakZoneThresholdsOf 投影）；严格
/// 小于判定（r＝阈值不警告——D-08 边界含于合规侧）；命中关节以链序
/// ObjectId 标注（R-4）。
TEST(KinRender, WeakZonesNearLimitReadsPolicyStrictBoundary_WP15T11_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-07"}, std::vector<std::string>{});

    // 策略阈值子模型：nearLimitRatio=0.3（Explicit 来源）——唯一阈值来源。
    const policy::JointThresholds jt = policy::JointThresholds::make(
        0.3, policy::PolicyValueOrigin::Explicit, std::nullopt,
        policy::PolicyValueOrigin::Explicit, 4.0, policy::PolicyValueOrigin::Explicit);
    const kin::WeakZoneThresholds thresholds = kin::weakZoneThresholdsOf(jt);
    ASSERT_TRUE(thresholds.nearLimitRatio.has_value());
    EXPECT_DOUBLE_EQ(*thresholds.nearLimitRatio, 0.3);

    const auto joint0 = idFrom<core::ObjectId>("kin-j0");
    const auto joint1 = idFrom<core::ObjectId>("kin-j1");
    const std::vector<core::ObjectId> chain = {joint0, joint1};

    // 解 1：margin=[0.29, 0.9]——关节 0 命中（0.29 < 0.3）。
    // 解 2：margin=[0.3, 0.9]——边界值不命中（严格小于）。
    KinematicSolutionSet view(makeSolutionSet({
        makeSolution("sig-inside", {0.29, 0.9}, 1.5, 0),
        makeSolution("sig-boundary", {0.3, 0.9}, 1.5, 1),
    }));

    const kin::SolutionSetRenderData data =
        kin::assembleSolutionSetRenderData(view, idFrom<core::ObjectId>("kin-pt"),
                                           core::ObjectId{}, chain, thresholds);
    EXPECT_TRUE(data.weakZones.nearLimitApplicable);
    ASSERT_EQ(data.weakZones.zones.size(), 1u);
    const kin::WeakZoneRenderItem& zone = data.weakZones.zones[0];
    EXPECT_EQ(zone.kind, WeakZoneKind::NearJointLimit);
    // R-4：关节标注＝注入链序 ObjectId（显示名归 ui nameResolver）。
    EXPECT_EQ(zone.jointObject, joint0);
    EXPECT_EQ(zone.jointIndex, 0u);
    EXPECT_DOUBLE_EQ(zone.value, 0.29);
    EXPECT_DOUBLE_EQ(zone.threshold, 0.3);
    // 解级定位＝sorted() 序下标：sorted 序（minMargin 降序）＝
    // [sig-boundary(0.3), sig-inside(0.29)]——命中解在序位 1。
    ASSERT_TRUE(zone.solutionIndex.has_value());
    EXPECT_EQ(*zone.solutionIndex, 1u);
    EXPECT_FALSE(zone.workItemIndex.has_value());
}

/// 薄弱区·近奇异：条件数严格大于阈值判定；模型级事实无单关节归属
/// （jointObject 全零保留值）；+∞（奇异）恒命中。
TEST(KinRender, WeakZonesNearSingularReadsPolicyStrictBoundary_WP15T11_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-07"}, std::vector<std::string>{});

    const policy::JointThresholds jt = policy::JointThresholds::make(
        std::nullopt, policy::PolicyValueOrigin::Explicit, 4.0,
        policy::PolicyValueOrigin::Explicit, 4.0, policy::PolicyValueOrigin::Explicit);
    const kin::WeakZoneThresholds thresholds = kin::weakZoneThresholdsOf(jt);

    const auto joint0 = idFrom<core::ObjectId>("kin-j0");
    const std::vector<core::ObjectId> chain = {joint0};

    // 解 1：条件数 4.1 > 4.0——命中；解 2：条件数 4.0——边界不命中；
    // 解 3：条件数 +∞（奇异）——恒命中。链序按 minimumJointMargin 降序：
    // 1.0 / 0.9 / 0.8（相等次键→init 序兜底，序＝解 1/2/3）。
    KinematicSolutionSet view(makeSolutionSet({
        makeSolution("sig-below", {1.0}, 4.1, 0),
        makeSolution("sig-boundary", {0.9}, 4.0, 1),
        makeSolution("sig-singular", {0.8}, std::numeric_limits<double>::infinity(), 2),
    }));

    const kin::SolutionSetRenderData data = kin::assembleSolutionSetRenderData(
        view, idFrom<core::ObjectId>("kin-pt"), core::ObjectId{}, chain, thresholds);
    EXPECT_TRUE(data.weakZones.nearSingularApplicable);
    // 命中两条：解 1（sorted 序 0）与解 3（sorted 序 2）；序＝逐解遍历。
    ASSERT_EQ(data.weakZones.zones.size(), 2u);
    EXPECT_EQ(data.weakZones.zones[0].kind, WeakZoneKind::NearSingular);
    ASSERT_TRUE(data.weakZones.zones[0].solutionIndex.has_value());
    EXPECT_EQ(*data.weakZones.zones[0].solutionIndex, 0u);
    EXPECT_EQ(data.weakZones.zones[1].kind, WeakZoneKind::NearSingular);
    EXPECT_EQ(*data.weakZones.zones[1].solutionIndex, 2u);
    // 模型级事实：jointObject 全零保留值（"未绑定"——结构注口径）。
    EXPECT_EQ(data.weakZones.zones[0].jointObject, core::ObjectId{});
    EXPECT_DOUBLE_EQ(data.weakZones.zones[0].value, 4.1);
    EXPECT_DOUBLE_EQ(data.weakZones.zones[0].threshold, 4.0);
}

/// P-POL-2 显式不适用：策略未设置阈值→对应种类条目恒空＋在场标记如实
/// false（不伪造"无薄弱"，也不发明默认阈值）。
TEST(KinRender, WeakZonesPolicyUnsetIsExplicitNotApplicable_WP15T11_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-07"}, std::vector<std::string>{});

    // 两阈值槽位全部 nullopt（P-POL-2——无冻结默认）。
    const policy::JointThresholds jt = policy::JointThresholds::make(
        std::nullopt, policy::PolicyValueOrigin::Explicit, std::nullopt,
        policy::PolicyValueOrigin::Explicit, 4.0, policy::PolicyValueOrigin::Explicit);
    const kin::WeakZoneThresholds thresholds = kin::weakZoneThresholdsOf(jt);
    EXPECT_FALSE(thresholds.nearLimitRatio.has_value());
    EXPECT_FALSE(thresholds.conditionNumberWarning.has_value());

    const auto joint0 = idFrom<core::ObjectId>("kin-j0");
    KinematicSolutionSet view(makeSolutionSet({
        makeSolution("sig-would-hit", {0.01}, 100.0, 0),  // 若伪造阈值必命中
    }));

    const kin::SolutionSetRenderData data = kin::assembleSolutionSetRenderData(
        view, idFrom<core::ObjectId>("kin-pt"), core::ObjectId{}, {joint0}, thresholds);
    EXPECT_TRUE(data.weakZones.zones.empty());
    EXPECT_FALSE(data.weakZones.nearLimitApplicable);
    EXPECT_FALSE(data.weakZones.nearSingularApplicable);
}

/// 碰撞对象对：解内碰撞对＋过滤碰撞诊断对，两来源同组装（policy 规范序
/// A<B 保序）；非碰撞原因记录不产对；定位三键按来源恰一。
TEST(KinRender, CollisionPairsFromBestSolutionsAndFilteredRecords_WP15T11_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-07"}, std::vector<std::string>{});

    const auto c1 = idFrom<core::ObjectId>("kin-cond-1");
    const auto pA = idFrom<core::ObjectId>("kin-pA");
    const auto obj1 = idFrom<core::ObjectId>("kin-obj1");
    const auto obj2 = idFrom<core::ObjectId>("kin-obj2");
    const auto obj3 = idFrom<core::ObjectId>("kin-obj3");
    const auto obj4 = idFrom<core::ObjectId>("kin-obj4");

    // 最佳解：evaluated∧inCollision，对象对 (obj1,obj2)；
    // 过滤记录：一条 Collision（(obj3,obj4)）＋一条 JointLimit（无对）。
    BatchComputation computation = makeComputation({
        makeItem(pA, c1, BatchItemStatus::CandidateFound,
                 makeSolution("sig-collide", {0.5}, 1.2, 0, true, true, {obj1, obj2}),
                 {makeFilteredRecord("sig-filtered", kin::SolutionFilterReason::Collision,
                                     {obj3, obj4}),
                  makeFilteredRecord("sig-limit", kin::SolutionFilterReason::JointLimit)}),
    });

    const kin::BatchRenderData data = kin::assembleBatchRenderData(
        computation, {}, {idFrom<core::ObjectId>("kin-j0")}, testThresholds(),
        core::TaskState::Completed);

    ASSERT_EQ(data.collisionPairs.size(), 2u);
    // 对 1：最佳解（解内形态）。
    EXPECT_EQ(data.collisionPairs[0].objectA, obj1);
    EXPECT_EQ(data.collisionPairs[0].objectB, obj2);
    EXPECT_FALSE(data.collisionPairs[0].fromFilteredRecord);
    ASSERT_TRUE(data.collisionPairs[0].workItemIndex.has_value());
    EXPECT_EQ(*data.collisionPairs[0].workItemIndex, 0u);
    EXPECT_FALSE(data.collisionPairs[0].filteredRecordIndex.has_value());
    // 对 2：过滤碰撞记录（诊断形态——filteredRecordIndex 定位）。
    EXPECT_EQ(data.collisionPairs[1].objectA, obj3);
    EXPECT_EQ(data.collisionPairs[1].objectB, obj4);
    EXPECT_TRUE(data.collisionPairs[1].fromFilteredRecord);
    ASSERT_TRUE(data.collisionPairs[1].filteredRecordIndex.has_value());
    EXPECT_EQ(*data.collisionPairs[1].filteredRecordIndex, 0u);
    EXPECT_EQ(data.collisionPairs[1].configurationSignature, "sig-filtered");
    // JointLimit 记录无对象对——不产条目（已由总数 2 断言）。
}

/// 最差关节裕量：来源＝视图 worstBy(MinimumJointMargin)（T09 设施复用
/// ——不另设排序）；arg-min 关节链序 ObjectId；空集→nullopt。
TEST(KinRender, WorstMarginSourcedFromViewWorstByNotOwnSort_WP15T11_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-07"}, std::vector<std::string>{});

    const auto joint0 = idFrom<core::ObjectId>("kin-j0");
    // 链序列表与解 margins 同维（单关节解——调用方契约）。
    const std::vector<core::ObjectId> chain = {joint0};

    // 三解裕量 0.9/0.1/0.5——sorted 序（裕量降序）＝[0.9, 0.5, 0.1]，
    // 最差（0.1）在 sorted 序下标 2。若组装另设排序，此定位必漂移。
    KinematicSolutionSet view(makeSolutionSet({
        makeSolution("sig-max", {0.9}, 2.0, 0),
        makeSolution("sig-worst", {0.1}, 5.0, 1),
        makeSolution("sig-mid", {0.5}, 3.0, 2),
    }));

    // 规范来源对照：视图自身 worstBy 的产出（独立取得，不在组装内）。
    const std::optional<SolutionRef> worstRef = view.worstBy(kin::WorstMetric::MinimumJointMargin);
    ASSERT_TRUE(worstRef.has_value());
    EXPECT_EQ(worstRef->solutionIndex, 2u);

    const kin::SolutionSetRenderData data = kin::assembleSolutionSetRenderData(
        view, idFrom<core::ObjectId>("kin-pt"), core::ObjectId{}, chain, testThresholds());
    ASSERT_TRUE(data.worstJointMargin.has_value());
    EXPECT_EQ(data.worstJointMargin->solutionIndex, worstRef->solutionIndex);
    EXPECT_DOUBLE_EQ(data.worstJointMargin->minimumJointMargin, 0.1);
    // arg-min 关节＝链序命中关节（ObjectId 标注——R-4；单关节解＝下标 0）。
    EXPECT_EQ(data.worstJointMargin->jointObject, joint0);
    EXPECT_EQ(data.worstJointMargin->jointIndex, 0u);
    EXPECT_EQ(data.worstJointMargin->configurationSignature, "sig-worst");

    // 空解集：worstBy 为 nullopt→渲染数据 nullopt（无解不虚构）。
    KinematicSolutionSet emptyView(makeSolutionSet({}));
    const kin::SolutionSetRenderData emptyData = kin::assembleSolutionSetRenderData(
        emptyView, idFrom<core::ObjectId>("kin-pt"), core::ObjectId{}, chain,
        testThresholds());
    EXPECT_FALSE(emptyData.worstJointMargin.has_value());
}

/// 解集视图 filteredRecords() 访问器（T11 表尾追加）——构造值持有原序
/// 副本，只读消费面（碰撞诊断明细的视图来源）。
TEST(KinRender, SolutionSetViewExposesFilteredRecordsOrder_WP15T11_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-07"}, std::vector<std::string>{});

    const auto obj1 = idFrom<core::ObjectId>("kin-obj1");
    const auto obj2 = idFrom<core::ObjectId>("kin-obj2");
    KinematicSolutionSet view(makeSolutionSet(
        {makeSolution("sig-ok", {0.5}, 1.5, 0)},
        {makeFilteredRecord("sig-f1", kin::SolutionFilterReason::Collision, {obj1, obj2}),
         makeFilteredRecord("sig-f2", kin::SolutionFilterReason::ResidualRecheck)}));

    const std::vector<kin::FilteredSolutionRecord>& records = view.filteredRecords();
    ASSERT_EQ(records.size(), 2u);
    EXPECT_EQ(records[0].signature, "sig-f1");  // 原序保持
    EXPECT_EQ(records[1].signature, "sig-f2");
    // 只读面：与 sorted()/worstBy 共存（不可变视图定性不破——表尾追加）。
    EXPECT_EQ(view.sorted().size(), 1u);
    EXPECT_TRUE(view.worstBy(kin::WorstMetric::MinimumJointMargin).has_value());
}

/// 组装确定性与对象标注：同输入两次组装逐字段相等（NFR-COR-01）；
/// 全部对象字段为 ObjectId 直传值（R-4 数据路径自证）。
TEST(KinRender, AssemblyIsDeterministicAndObjectAnnotated_WP15T11_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-07"}, std::vector<std::string>{"NFR-COR-01"});

    const auto c1 = idFrom<core::ObjectId>("kin-cond-1");
    const auto pA = idFrom<core::ObjectId>("kin-pA");
    const auto joint0 = idFrom<core::ObjectId>("kin-j0");

    BatchComputation computation = makeComputation({
        makeItem(pA, c1, BatchItemStatus::CandidateFound,
                 makeSolution("sig-d", {0.2}, 5.0, 0)),
        makeItem(idFrom<core::ObjectId>("kin-pB"), c1, BatchItemStatus::NoConvergence),
    });
    const std::vector<BatchTaskPoint> resolved = {makePoint(pA, 0.5)};

    const kin::BatchRenderData first = kin::assembleBatchRenderData(
        computation, resolved, {joint0}, testThresholds(), core::TaskState::Completed);
    const kin::BatchRenderData second = kin::assembleBatchRenderData(
        computation, resolved, {joint0}, testThresholds(), core::TaskState::Completed);
    EXPECT_EQ(first, second);  // operator== 逐字段（含位姿/对象/定位键）

    // 对象标注直传：薄弱区关节＝注入 ObjectId；点绑定＝工作项 ObjectId。
    // 最佳解（margin 0.2 < 0.3 且条件数 5.0 > 4.0）双命中——近限位在前
    // （逐解先近限位后近奇异的固定产出序）。
    ASSERT_EQ(first.weakZones.zones.size(), 2u);
    EXPECT_EQ(first.weakZones.zones[0].kind, WeakZoneKind::NearJointLimit);
    EXPECT_EQ(first.weakZones.zones[0].jointObject, joint0);
    EXPECT_EQ(first.weakZones.zones[0].pointOid, pA);
    EXPECT_EQ(first.weakZones.zones[0].conditionId, c1);
    EXPECT_EQ(first.weakZones.zones[1].kind, WeakZoneKind::NearSingular);
}

/// 调用方契约违约 fail-fast：维度不符／NaN 指标／对象对奇数长度／下标
/// 越界（§9.1 调用方错误轨——不静默截断不猜测）。
TEST(KinRender, ContractViolationsFailFast_WP15T11_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-07"}, std::vector<std::string>{});

    const auto joint0 = idFrom<core::ObjectId>("kin-j0");
    const auto pt = idFrom<core::ObjectId>("kin-pt");

    // 维度不符：链序列 1 个 vs 解 margins 2 个。
    KinematicSolutionSet mismatched(makeSolutionSet({makeSolution("sig-m", {0.1, 0.2}, 1.0, 0)}));
    EXPECT_THROW(kin::assembleSolutionSetRenderData(mismatched, pt, core::ObjectId{}, {joint0},
                                                    testThresholds()),
                 std::invalid_argument);

    // NaN 指标：条件数 NaN（求解器契约排除值——NFR-COR-03）。
    KinematicSolutionSet nanCond(makeSolutionSet(
        {makeSolution("sig-nan", {0.5}, std::numeric_limits<double>::quiet_NaN(), 0)}));
    EXPECT_THROW(kin::assembleSolutionSetRenderData(nanCond, pt, core::ObjectId{}, {joint0},
                                                    testThresholds()),
                 std::invalid_argument);

    // 对象对奇数长度（成对展平契约违约）。
    BatchComputation oddPairs = makeComputation({
        makeItem(idFrom<core::ObjectId>("kin-pA"), idFrom<core::ObjectId>("kin-c1"),
                 BatchItemStatus::CandidateFound,
                 makeSolution("sig-odd", {0.5}, 1.0, 0, true, true,
                              {idFrom<core::ObjectId>("kin-only-a")})),
    });
    EXPECT_THROW(kin::assembleBatchRenderData(oddPairs, {}, {joint0}, testThresholds(),
                                              core::TaskState::Completed),
                 std::invalid_argument);

    // 选中联动下标越界。
    BatchComputation tiny = makeComputation({
        makeItem(idFrom<core::ObjectId>("kin-pA"), idFrom<core::ObjectId>("kin-c1"),
                 BatchItemStatus::NoConvergence),
    });
    EXPECT_THROW(kin::failurePointSelectionLink(tiny, 5u, {}, core::TaskState::Completed),
                 std::invalid_argument);
    KinematicSolutionSet one(makeSolutionSet({makeSolution("sig-one", {0.5}, 1.0, 0)}));
    EXPECT_THROW(kin::solutionSelectionLink(one, SolutionRef{9u}), std::invalid_argument);
}

// =====================================================================
// acceptance 2——渲染状态与统一状态词一致
// =====================================================================

/// 失败点状态投影＝core 词表双轴（taskState 随来源运行通道、taskOutcome
/// 恒 Failed）；token 直读 core 冻结表（不自造状态词——UX-10 消费同一
/// 词表）。
TEST(KinRender, StateProjectionUsesCoreVocabularyTokens_WP15T11_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-07"}, std::vector<std::string>{"UX-10"});

    const auto c1 = idFrom<core::ObjectId>("kin-cond-1");
    BatchComputation computation = makeComputation({
        makeItem(idFrom<core::ObjectId>("kin-pA"), c1, BatchItemStatus::NoConvergence),
    });

    const kin::BatchRenderData data = kin::assembleBatchRenderData(
        computation, {}, {}, testThresholds(), core::TaskState::Interrupted);
    ASSERT_EQ(data.failurePoints.points.size(), 1u);
    const kin::FailurePointRenderItem& point = data.failurePoints.points[0];
    // 状态机轴＝来源任务态（调用方供给——L-K12 通道）；结果轴＝Failed。
    EXPECT_EQ(point.state.taskState, core::TaskState::Interrupted);
    EXPECT_EQ(point.state.taskOutcome, core::TaskOutcome::Failed);
    // token 形态＝core::toToken 直读（零新词——"interrupted"/"failed"）。
    EXPECT_EQ(kin::renderTaskStateToken(point.state),
              core::toToken(core::TaskState::Interrupted));
    EXPECT_EQ(kin::renderTaskOutcomeToken(point.state),
              core::toToken(core::TaskOutcome::Failed));
    EXPECT_EQ(kin::renderTaskOutcomeToken(point.state), "failed");
}

/// per-item 状态→core 结果轴映射全表：四失败态→Failed、CandidateFound→
/// Completed、NotApplicable/NotRun→nullopt（排除信号——acceptance 3）。
TEST(KinRender, BatchItemOutcomeProjectionTable_WP15T11_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-07"}, std::vector<std::string>{});

    EXPECT_EQ(kin::projectBatchItemOutcome(BatchItemStatus::CandidateFound),
              std::optional{core::TaskOutcome::Completed});
    EXPECT_EQ(kin::projectBatchItemOutcome(BatchItemStatus::NoConvergence),
              std::optional{core::TaskOutcome::Failed});
    EXPECT_EQ(kin::projectBatchItemOutcome(BatchItemStatus::AllFiltered),
              std::optional{core::TaskOutcome::Failed});
    EXPECT_EQ(kin::projectBatchItemOutcome(BatchItemStatus::BoundExceeded),
              std::optional{core::TaskOutcome::Failed});
    EXPECT_EQ(kin::projectBatchItemOutcome(BatchItemStatus::InputInvalid),
              std::optional{core::TaskOutcome::Failed});
    EXPECT_EQ(kin::projectBatchItemOutcome(BatchItemStatus::NotApplicable), std::nullopt);
    EXPECT_EQ(kin::projectBatchItemOutcome(BatchItemStatus::NotRun), std::nullopt);
}

// =====================================================================
// acceptance 3——回写仅会话姿态（KIN-06/V-18）
// =====================================================================

/// 回写只进会话容器：writebackOf 值仅 {q, 签名}（结构化绑定自证两成员
/// ——零端口/零身份字段）；写入口唯一落 KinSessionPose（T08 纯值容器
/// ——零端口结构性保证即 V-18"零修订/零失效/不入缓存身份"的观测根基）。
TEST(KinRender, WritebackTargetsSessionPoseOnly_WP15T11_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-06"}, std::vector<std::string>{"AT-04"});

    KinematicSolution solution = makeSolution("sig-wb", {0.11, 0.22}, 1.0, 3);
    solution.q = {0.11, 0.22};

    const kin::RenderPointWriteback wb = kin::writebackOf(solution);
    // 结构面：恰两成员（q＋记录键签名）——结构化绑定编译期自证。
    auto [wbQ, wbSig] = wb;
    EXPECT_EQ(wbQ, solution.q);
    EXPECT_EQ(wbSig, "sig-wb");

    // 行为面：唯一消费口＝KinSessionPose 写入口；写后仅会话值变化。
    kin::KinSessionPose session;
    EXPECT_FALSE(session.isSet());
    kin::writebackToSessionPose(session, wb);
    EXPECT_TRUE(session.isSet());
    EXPECT_EQ(session.jointConfiguration(), solution.q);

    // 过滤诊断解同样可回写（T09 合并诊断呈现的会话面延伸）。
    const kin::FilteredSolutionRecord record =
        makeFilteredRecord("sig-filtered-wb", kin::SolutionFilterReason::Collision);
    const kin::RenderPointWriteback wbFiltered = kin::writebackOf(record);
    kin::KinSessionPose session2;
    kin::writebackToSessionPose(session2, wbFiltered);
    EXPECT_EQ(session2.jointConfiguration(), record.q);

    // 非有限 q 拒绝（写入口既定契约——NFR-COR-03 会话面）。
    kin::RenderPointWriteback bad = wb;
    bad.q = {std::numeric_limits<double>::quiet_NaN()};
    kin::KinSessionPose session3;
    EXPECT_THROW(kin::writebackToSessionPose(session3, bad), std::invalid_argument);
}

/// NotApplicable/NotRun 不渲染为失败：不产失败点条目、排除计数分列；
/// 选中联动对预终结项无结果轴投影（nullopt——ui 中性呈现不落失败态）。
TEST(KinRender, NotApplicableAndNotRunNotRenderedAsFailure_WP15T11_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-07"}, std::vector<std::string>{});

    const auto c1 = idFrom<core::ObjectId>("kin-cond-1");
    const auto pNa = idFrom<core::ObjectId>("kin-pNa");
    const auto pNr = idFrom<core::ObjectId>("kin-pNr");
    BatchComputation computation = makeComputation({
        makeItem(pNa, c1, BatchItemStatus::NotApplicable, {}, {}, {"工况不适用"}),
        makeItem(pNr, c1, BatchItemStatus::NotRun, {}, {}, {"分批取消"}),
    });

    const kin::BatchRenderData data = kin::assembleBatchRenderData(
        computation, {}, {}, testThresholds(), core::TaskState::Canceled);
    // 零失败点（如实——伪造即 ERR-01 违例）；排除计数分列。
    EXPECT_TRUE(data.failurePoints.points.empty());
    EXPECT_EQ(data.failurePoints.notApplicableItemCount, 1u);
    EXPECT_EQ(data.failurePoints.notRunItemCount, 1u);
    EXPECT_EQ(data.failurePoints.candidateFoundCount, 0u);
    // 无解无薄弱无碰撞对（解级评估只覆盖 CandidateFound 项）。
    EXPECT_TRUE(data.weakZones.zones.empty());
    EXPECT_TRUE(data.collisionPairs.empty());

    // 选中面：预终结项 state=nullopt（中性呈现），种类/原因如实携带。
    const kin::FailurePointSelectionLink link =
        kin::failurePointSelectionLink(computation, 0u, {}, core::TaskState::Canceled);
    EXPECT_EQ(link.failureKind, BatchItemStatus::NotApplicable);
    EXPECT_EQ(link.reason, "工况不适用");
    EXPECT_FALSE(link.state.has_value());
}

// =====================================================================
// acceptance 4——入口与选中联动数据（L-K1）
// =====================================================================

/// 失败点选中联动：身份/种类/原因/位姿/工作项下标齐备；(pointOid,
/// conditionId) 反查工作项下标往返一致；无命中如实 nullopt。
TEST(KinRender, FailurePointSelectionLinkage_WP15T11_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-07"}, std::vector<std::string>{});

    const auto c1 = idFrom<core::ObjectId>("kin-cond-1");
    const auto c2 = idFrom<core::ObjectId>("kin-cond-2");
    const auto pA = idFrom<core::ObjectId>("kin-pA");
    const auto pB = idFrom<core::ObjectId>("kin-pB");

    BatchComputation computation = makeComputation({
        makeItem(pA, c1, BatchItemStatus::BoundExceeded),
        makeItem(pA, c2, BatchItemStatus::NoConvergence),
        makeItem(pB, c1, BatchItemStatus::AllFiltered, {}, {}, {"全部过滤"}),
    });
    const std::vector<BatchTaskPoint> resolved = {makePoint(pA, 2.0), makePoint(pB, 3.0)};

    // 正查（渲染条目→联动）：失败点条目的 workItemIndex 可直接回投。
    const kin::FailurePointSelectionLink link =
        kin::failurePointSelectionLink(computation, 1u, resolved, core::TaskState::Completed);
    EXPECT_EQ(link.pointOid, pA);
    EXPECT_EQ(link.conditionId, c2);
    EXPECT_EQ(link.failureKind, BatchItemStatus::NoConvergence);
    EXPECT_EQ(link.state, std::optional(kin::RenderStateProjection{
                              core::TaskState::Completed, core::TaskOutcome::Failed}));
    ASSERT_TRUE(link.targetInBase.has_value());
    EXPECT_DOUBLE_EQ(link.targetInBase->P()[0], 2.0);
    EXPECT_EQ(link.workItemIndex, 1u);

    // 反查（对象树/三维拾取→工作项）：按 (pointOid, conditionId) 定位。
    const std::optional<std::uint64_t> idx = kin::findWorkItemIndex(computation, pB, c1);
    ASSERT_TRUE(idx.has_value());
    EXPECT_EQ(*idx, 2u);
    // 无命中如实 nullopt（不猜测——ARC-04）。
    EXPECT_FALSE(kin::findWorkItemIndex(computation, pB, c2).has_value());
}

/// 解条目选中联动（L-K1 候选选中）：SolutionRef→检查器定位＋回写值；
/// 回写值写会话姿态（KIN-06 双击候选场景闭环）。
TEST(KinRender, SolutionSelectionLinkageAndSessionWriteback_WP15T11_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-07"}, std::vector<std::string>{"KIN-06"});

    KinematicSolutionSet view(makeSolutionSet({
        makeSolution("sig-first", {0.9}, 2.0, 0),
        makeSolution("sig-second", {0.5}, 3.0, 1),
    }));

    const kin::SolutionSelectionLink link =
        kin::solutionSelectionLink(view, SolutionRef{1u});
    EXPECT_EQ(link.solutionIndex, 1u);
    EXPECT_EQ(link.writeback.q, view.sorted()[1].q);
    EXPECT_EQ(link.writeback.configurationSignature, "sig-second");

    // 选中→回写→会话（只写会话态——零修订零失效随容器结构性保证）。
    kin::KinSessionPose session;
    kin::writebackToSessionPose(session, link.writeback);
    EXPECT_EQ(session.jointConfiguration(), view.sorted()[1].q);
}
