/**
 * @file   ReproBenchmarkTest.cpp
 * @brief  kinematics 域确定性复现基准用例（WP-23-T09 抽样——B-08）：
 *         批量任务点验证（runBatchComputation）同配置复跑＋跨线程数
 *         （1→8 并行分片）等价集合＋稳定排序＋归约容差（1×10⁻¹²）。
 *
 * 设计依据：
 *   - units/kinematics.md §8.4（批量执行确定性——同输入同线程数逐位一致；
 *     异线程数等价集合＋稳定排序一致：分片＝全序工作项连续区间、结果按
 *     全序槽位写回）＋§7.1（批量执行图）
 *   - doc/industrial-robot-design/performance-baseline.md §2 B-08 行＋§4.6
 *     （并行归约与确定性容差——附录 D 第 8 项；不要求浮点逐字节相同）
 *   - 需求 NFR-COR-02；任务契约 tasks/foundation/WP-23-T09.json
 *
 * 抽样说明（各域抽样复现——本域取样点）：批量任务点验证是本域唯一带
 * threadCount 执行参数的计算通道（§8.4 并行分片）——B-08 的"线程数"维度
 * 在本域由它承载；种子经 BatchQuery.seed（SeededRandom 初值序列源）注入，
 * 确定性上下文经 testkit ReproRecord 登记。
 *
 * 确定性口径：异线程数等价集合按 NFR-COR-02 判定（不要求逐字节）——
 * 实际实现按全序槽位写回，观测偏差如实记录于报告（预期 0）。
 */

#include "KinFkFixture.hpp"   // TestView/NoopContext/twoLinkModel/referenceFk/idFrom

#include <sdurws/ird/evidence/Evaluator.hpp>
#include <sdurws/ird/kinematics/Evaluators.hpp>
#include <sdurws/ird/kinematics/KinTypes.hpp>
#include <sdurws/ird/testkit/ReproBenchmark.hpp>
#include <sdurws/ird/testkit/ToleranceProfile.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

using namespace sdurws::ird::kinematics::testfixture;
namespace ev = sdurws::ird::evidence;
namespace kin = sdurws::ird::kinematics;
namespace tk = sdurws::ird::testkit;
using kin::BatchCondition;
using kin::BatchQuery;
using kin::BatchTaskPoint;
using kin::InitialValueStrategy;

// =====================================================================
// 元素投影（BatchWorkItemRecord → SetMatchTraits 元素——§5.4.5 适配归域）。
// 元素与 traits 特化置于全局作用域（SetCheckTest 消费示范同款——特化须
// 在主模板的可达外围命名空间；匿名命名空间内的特化声明跨命名空间歧义）。
// =====================================================================

/// 批量工作项元素：身份＝(pointOid, conditionId) 规范文本；数值面随状态
/// 分档——CandidateFound 项携带最佳解 q（rad）/位置残差（m）/最小裕量
/// （无量纲），预终结项（NoConvergence 等）无数值（身份配对语义）。
struct ReproBatchItem {
    std::string id;                  ///< "<pointOid>|<conditionId>"（批内唯一）
    std::vector<double> q;           ///< 最佳解关节向量，单位 rad（链序）
    double positionResidual = 0.0;   ///< 位置残差，单位 m
    double minJointMargin = 0.0;     ///< 最小归一化关节裕量，无量纲
};

/// referenceFk 的 4×4 齐次矩阵 → rw 位姿（TaskPointsBatchTest 同款换形）。
rw::math::Transform3D<double> transformOf(const Mat4& m)
{
    const rw::math::Rotation3D<double> r(m[0][0], m[0][1], m[0][2], m[1][0],
                                         m[1][1], m[1][2], m[2][0], m[2][1],
                                         m[2][2]);
    const rw::math::Vector3D<double> p(m[0][3], m[1][3], m[2][3]);
    return rw::math::Transform3D<double>(p, r);
}

/// 逐项投影（计算面→断言面；items 已按 (pointOid, conditionId) 全序——§8.4）。
std::vector<ReproBatchItem> projectItems(const kin::BatchComputation& comp)
{
    std::vector<ReproBatchItem> out;
    out.reserve(comp.items.size());
    for (const kin::BatchWorkItemRecord& it : comp.items) {
        ReproBatchItem e;
        e.id = it.pointOid.toCanonical() + "|" + it.conditionId.toCanonical();
        if (it.bestSolution.has_value()) {
            e.q = it.bestSolution->q;
            e.positionResidual = it.bestSolution->positionResidual;
            e.minJointMargin = it.bestSolution->minimumJointMargin;
        }
        out.push_back(std::move(e));
    }
    return out;
}

/// traits 特化（域侧适配——档案键按量纲取：q→repro.angle、残差→repro.length、
/// 裕量→repro.value；字面量键＝静态存储期，string_view 生命周期安全）。
/// 特化在主模板所在命名空间内显式展开声明（OptGoldenDatasetTest 域侧特化
/// 同款形态——命名空间别名不用于特化声明，规避 MSVC 别名作用域解析歧义）。
namespace sdurws::ird::testkit {
template <>
struct SetMatchTraits<ReproBatchItem> {
    static std::optional<std::string> identity(const ReproBatchItem& e)
    {
        return e.id;
    }
    static std::vector<NumericFieldView> numerics(const ReproBatchItem& e)
    {
        std::vector<NumericFieldView> out;
        out.reserve(e.q.size() + 2);
        for (const double v : e.q) { out.push_back({"repro.angle", v}); }
        if (e.q.size() > 0) {
            out.push_back({"repro.length", e.positionResidual});
            out.push_back({"repro.value", e.minJointMargin});
        }
        return out;
    }
};
}  // namespace sdurws::ird::testkit

namespace {

/// repro-reduction 档案（B-08 数值唯一来源——与 testkit 元测试同一文件）。
const tk::ToleranceProfile& reproProfile()
{
    static const tk::ToleranceProfile profile = tk::ToleranceProfile::load(
        tk::toleranceProfileDir("repro-reduction") / "v1.0.0.json");
    return profile;
}

}  // namespace

/**
 * kinematics 域复现基准（B-08 抽样）：12 环形任务点×2 工况＝24 工作项，
 * SeededRandom 多初值（种子 20260909——ReproRecord 默认口径同源）、
 * maxBatchSize=8（强制多批）、参考腿 1 线程、线程等价腿 8 线程。
 */
TEST(KinReproBenchmark, Batch1vs8ThreadEquivalentSetAndOrder_WP23T09_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-02"},
                  std::vector<std::string>{"AT-27"});

    // ---- 确定性输入装配（平面二连杆黄金几何——夹具自持字面量）----
    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);

    // 环形任务点：半径 0.75 m ∈ (|L1−L2|=0.4, L1+L2=1.8) 可达域内——12 点
    // 全部期望 CandidateFound（多初值降局部极小；可达面求解路径稳定）。
    constexpr std::size_t kPointCount = 12;
    std::vector<BatchTaskPoint> points;
    points.reserve(kPointCount);
    for (std::size_t i = 0; i < kPointCount; ++i) {
        const double angle = static_cast<double>(i) * 0.5;   // rad——环向展开
        BatchTaskPoint p;
        p.pointOid = idFrom<core::ObjectId>("kin-rp-" + std::to_string(i));
        p.level = kin::BatchRequirementLevel::Must;
        p.enabled = true;
        p.targetInBase = transformOf(
            referenceFk(model, {angle, -0.5}).tcp);          // 独立参考 FK 产目标
        p.positionTolerance = 1e-6;                          // m
        p.orientationTolerance = 1e-6;                       // rad
        points.push_back(p);
    }
    std::vector<kin::BatchCondition> conditions;
    for (int c = 0; c < 2; ++c) {
        BatchCondition cond;
        cond.conditionId = idFrom<core::ObjectId>("kin-rc-" + std::to_string(c));
        cond.level = kin::BatchRequirementLevel::Must;
        cond.enabled = true;
        cond.appliesTo = kin::BatchAppliesToScope::AllStations;
        conditions.push_back(cond);
    }

    // ---- 基线查询（执行参数 threadCount 由复现夹具逐腿注入）----
    BatchQuery base;
    base.points = points;
    base.conditions = conditions;
    base.defaultTcp.toolObject = idFrom<core::ObjectId>("kin-tool");
    base.referenceQ = {0.0, 0.0};                            // rad——排序参考构型
    base.initialStrategy = InitialValueStrategy::SeededRandom;
    base.initialValuesCount = 8U;                            // 种子派生确定性序列
    base.iterationLimit = 200U;
    base.dedupThresholdPerAxis = 1e-6;                       // rad 逐轴
    base.seed = 20260909U;                                   // 非 0——I-KIN-4
    base.maxBatchSize = 8U;                                  // 强制多批（24 项→3 批）
    base.threadCount = 1U;                                   // 夹具逐腿改写

    ev::EvaluationRequest req;
    req.mode = core::EvaluationMode::Verified;
    req.snapshot.snapshotId.bytes = digestOf("kin-repro-snap");
    req.slice.sliceId.bytes = digestOf("kin-repro-slice");
    req.task.attempt.value = 1U;
    for (const BatchCondition& c : conditions) { req.caseSubset.push_back(c.conditionId); }

    // ---- 复现用例执行（三腿：reference/re-rerun/threads-8）----
    tk::ReproCaseSpec spec;
    spec.caseId = "kin-batch-thread-equivalence";
    spec.domain = "kinematics";
    spec.inputProvenance =
        "twoLinkFixture(12 ring points x 2 conditions, SeededRandom n=8, "
        "seed=20260909, maxBatchSize=8, iterationLimit=200)";
    spec.repro.seed = 20260909;
    spec.repro.threadCount = 1;
    spec.parallelThreadCount = 8U;                           // 并行归约对照腿

    NoopContext ctx;
    const kin::BatchComputation firstPass = kin::runBatchComputation(
        view, base, req, ctx);   // 可达性前置探测（期望面自检——不进判定）
    ASSERT_EQ(firstPass.items.size(), points.size() * conditions.size());
    ASSERT_EQ(firstPass.incomplete, false);

    const tk::ReproCaseReport report = tk::runReproCase<ReproBatchItem>(
        spec, reproProfile(), tk::SetMatchTraits<ReproBatchItem>{},
        [&](std::uint32_t threads) {
            BatchQuery q = base;       // 值拷贝——仅执行参数随腿变化
            q.threadCount = threads;
            NoopContext legCtx;        // 每腿独立上下文（无跨腿状态）
            return projectItems(kin::runBatchComputation(view, q, req, legCtx));
        });

    // ---- 判定（NFR-COR-02：等价集合＋稳定排序；归约容差经档案 1e-12）----
    ASSERT_EQ(report.verdicts.size(), 2U);
    for (const tk::ReproLegVerdict& v : report.verdicts) {
        EXPECT_TRUE(v.setEquivalent) << v.legId;
        EXPECT_TRUE(v.sameOrder) << v.legId;
        EXPECT_EQ(v.missingExpected, 0U) << v.legId;
        EXPECT_EQ(v.extraActual, 0U) << v.legId;
    }
    EXPECT_TRUE(report.passed) << (report.violations.empty()
        ? "" : report.violations.front());
    // 元素面：24 项全部在场（抽样规模如实留痕）。
    EXPECT_EQ(report.legs.front().elementCount, points.size() * conditions.size());

    // ---- 报告拖挂（IRD_REPRO_REPORT_DIR 设置时落盘——基准执行留痕）----
    if (const auto dir = tk::reproReportDirFromEnv()) {
        EXPECT_TRUE(tk::writeReproReportJson(*dir, report));
    }
}
