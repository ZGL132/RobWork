/**
 * @file   CapabilityCurveTest.cpp
 * @brief  能力曲线插值用例组（SelCapabilityCurve）——分段线性插值解析
 *         算例、端点闭区间、默认禁止外推（SEL-CURVE-EXTRAPOLATION-DENIED）、
 *         结果携带面与分轨语义（AT-08/SEL-02 acceptance 2 用例面）。
 *
 * 设计依据：
 *   - units/selection.md §6.2（插值规则四条：闭区间内分段线性/默认禁止
 *     外推——不自动使用最近点、不静默外推/边界点闭区间含端点/结果携带
 *     实际输入点＋插值区间＋曲线版本＋单位——NFR-COR-04；插值失败≠候选
 *     能力不足——数据不足类分轨，§10.3）、§14.3（接口契约——拒绝轨不
 *     抛异常；查询非有限＝调用方违约 fail-fast）
 *   - 需求 SEL-02（分段线性插值、默认禁止外推）、NFR-COR-01（解析算例
 *     对照）、NFR-COR-02（确定性）、NFR-COR-03（非有限拒绝）、NFR-COR-04
 *     （可定位）
 *   - 任务契约 tasks/foundation/WP-19-T03.json acceptance 2
 *
 * 容差口径（附录 D 精神——卡 §6.2 注"本卡不自定义与附录 D 冲突的容差"）：
 *   本组黄金值为两三位有效数字的解析算例，插值是纯代数运算（乘加），浮点
 *   噪声在 1e-12 量级以下——统一使用 1e-12 绝对容差（远紧于任何工程容差，
 *   仅吸收浮点表示误差；筛选层阈值比较的附录 D 容差归 WP-19-T04 消费面）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <sdurws/ird/selection/CatalogTypes.hpp>
#include <sdurws/ird/selection/Curve.hpp>
#include <sdurws/ird/selection/DiagCodes.hpp>

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace sdurws::ird::selection;

namespace {

/// 黄金曲线：curve-tq（电机转矩-转速——CatalogImportTest 基线同源数据）：
/// 点 (50,5.0) (150,4.5) (300,3.5)，x 单位 rad/s、y 单位 N·m（SI 域）。
PerformanceCurve makeGoldenTorqueCurve()
{
    PerformanceCurve c;
    c.curveId = "curve-tq";
    c.xQuantity = kQuantitySpeed;
    c.yQuantity = kQuantityTorque;
    c.xUnit = "rad/s";
    c.yUnit = "N*m";
    c.points = {{50.0, 5.0}, {150.0, 4.5}, {300.0, 3.5}};
    c.catalog = CatalogIdentity{"cat-demo", "v1", {}, "demo 企业器件库"};
    // 内容身份按真实计算路径回填（黄金曲线同样走构造入口纪律）。
    c.contentIdentity = computeCurveContentIdentity(c);
    return c;
}

}  // namespace

// =====================================================================
// 分段线性插值（SEL-02 冻结语义——解析期望值）
// =====================================================================

/**
 * 段内黄金插值（NFR-COR-01 解析算例）：x=100 落在段 [50,150]，
 * y = 5.0 + (4.5−5.0)·(100−50)/(150−50) = 5.0 − 0.25 = 4.75 N·m；
 * 结果携带插值区间 [50,150]、曲线版本与单位（NFR-COR-04）。
 */
TEST(SelCapabilityCurve, InterpolatesMidSegmentWithAnalyticValue)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-02", "NFR-COR-01", "NFR-COR-04"},
                  std::vector<std::string>{"AT-08"});

    const LinearCurveEvaluator ev;
    const PerformanceCurve curve = makeGoldenTorqueCurve();

    const CurveQueryResult r = ev.evaluate(curve, 100.0);   // rad/s
    ASSERT_TRUE(r.ok());
    EXPECT_NEAR(r.value, 4.75, 1e-12);                      // N·m（解析黄金值）
    EXPECT_DOUBLE_EQ(r.x, 100.0);
    EXPECT_DOUBLE_EQ(r.xLow, 50.0);                         // 插值区间下端
    EXPECT_DOUBLE_EQ(r.xHigh, 150.0);                       // 插值区间上端
    EXPECT_EQ(r.catalog.catalogId, "cat-demo");
    EXPECT_EQ(r.catalog.version, "v1");
    EXPECT_EQ(r.xUnit, "rad/s");
    EXPECT_EQ(r.yUnit, "N*m");
    EXPECT_EQ(r.rejectCode, "");                            // 值轨无拒绝码
}

/**
 * 第二段斜率黄金值：x=225 落在段 [150,300]，
 * y = 4.5 + (3.5−4.5)·(225−150)/(300−150) = 4.5 − 0.5 = 4.0 N·m。
 */
TEST(SelCapabilityCurve, InterpolatesSecondSegmentWithAnalyticValue)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-02", "NFR-COR-01"},
                  std::vector<std::string>{});

    const LinearCurveEvaluator ev;
    const CurveQueryResult r = ev.evaluate(makeGoldenTorqueCurve(), 225.0);
    ASSERT_TRUE(r.ok());
    EXPECT_NEAR(r.value, 4.0, 1e-12);                       // N·m
    EXPECT_DOUBLE_EQ(r.xLow, 150.0);
    EXPECT_DOUBLE_EQ(r.xHigh, 300.0);
}

/**
 * 边界点＝闭区间含端点（卡 §6.2）：x=x_min 与 x=x_max 返回端点值精确，
 * 插值区间退化到端点。
 */
TEST(SelCapabilityCurve, BoundaryQueriesReturnExactEndpointValues)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-02"},
                  std::vector<std::string>{"AT-08"});

    const LinearCurveEvaluator ev;
    const PerformanceCurve curve = makeGoldenTorqueCurve();

    const CurveQueryResult lo = ev.evaluate(curve, 50.0);   // x_min
    ASSERT_TRUE(lo.ok());
    EXPECT_NEAR(lo.value, 5.0, 1e-12);                      // N·m
    EXPECT_DOUBLE_EQ(lo.xLow, 50.0);
    EXPECT_DOUBLE_EQ(lo.xHigh, 150.0);                      // 首段承载（含端点）

    const CurveQueryResult hi = ev.evaluate(curve, 300.0);  // x_max
    ASSERT_TRUE(hi.ok());
    EXPECT_NEAR(hi.value, 3.5, 1e-12);                      // N·m
    EXPECT_DOUBLE_EQ(hi.xLow, 150.0);                       // 末段承载（含端点）
    EXPECT_DOUBLE_EQ(hi.xHigh, 300.0);
}

// =====================================================================
// 默认禁止外推（SEL-CURVE-EXTRAPOLATION-DENIED——acceptance 2 核心码）
// =====================================================================

/**
 * 区间外查询两侧拒绝（卡 §6.2——不自动使用最近点、不静默外推）：
 * x=49（下侧）与 x=300.5（上侧）均产出 SEL-CURVE-EXTRAPOLATION-DENIED
 * 拒绝轨，不产出任何插值。
 */
TEST(SelCapabilityCurve, ExtrapolationDeniedOnBothSides_AT08)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-02"},
                  std::vector<std::string>{"AT-08"});

    const LinearCurveEvaluator ev;
    const PerformanceCurve curve = makeGoldenTorqueCurve();

    for (const double x : {49.0, 300.5}) {                  // rad/s（区间 [50,300] 外）
        const CurveQueryResult r = ev.evaluate(curve, x);
        EXPECT_FALSE(r.ok()) << "区间外 x=" << x << " 不应产出插值";
        EXPECT_EQ(r.outcome, CurveQueryResult::Outcome::Rejected);
        EXPECT_EQ(r.rejectCode, std::string(kSelCurveExtrapolationDenied));
    }
}

/**
 * 拒绝结果携带有效区间与单位（ERR-01 比较型素材：实际输入点/有效区间/
 * 单位——NFR-COR-04 追溯面）。
 */
TEST(SelCapabilityCurve, RejectionCarriesValidRangeAndUnits)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-04", "ERR-01"},
                  std::vector<std::string>{});

    const LinearCurveEvaluator ev;
    const CurveQueryResult r = ev.evaluate(makeGoldenTorqueCurve(), 1000.0);

    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.rejectCode, std::string(kSelCurveExtrapolationDenied));
    EXPECT_NE(r.rejectMessage.find("rad/s"), std::string::npos);  // 单位进入消息
    EXPECT_DOUBLE_EQ(r.xLow, 50.0);                               // 有效区间携带
    EXPECT_DOUBLE_EQ(r.xHigh, 300.0);
    EXPECT_DOUBLE_EQ(r.x, 1000.0);                                // 实际输入点回填
    EXPECT_EQ(r.yUnit, "N*m");
    EXPECT_EQ(r.catalog.version, "v1");                           // 曲线版本携带
}

// =====================================================================
// 拒绝轨分轨语义（§6.2——插值失败≠候选能力不足）
// =====================================================================

/**
 * 数据不足分轨（卡 §6.2"两者分轨"）：拒绝轨只发数据不足类码、不产出
 * 能力值——"候选能力不足"（插值成功但值不达标）在筛选层〔WP-19-T04〕
 * 独立判定；本用例钉住拒绝轨不伪装成能力比较结果。
 */
TEST(SelCapabilityCurve, RejectionIsDataInsufficiencyNotCapabilityShortfall)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-02"},
                  std::vector<std::string>{});

    const LinearCurveEvaluator ev;
    const CurveQueryResult r = ev.evaluate(makeGoldenTorqueCurve(), 0.0);
    ASSERT_FALSE(r.ok());
    // 拒绝轨的三无：无值语义（outcome=Rejected）、数据不足类码、值字段
    // 不承载"不达标"判定（value 固定零填充——消费方只看 ok()）。
    EXPECT_EQ(r.outcome, CurveQueryResult::Outcome::Rejected);
    EXPECT_EQ(r.rejectCode, std::string(kSelCurveExtrapolationDenied));
    EXPECT_DOUBLE_EQ(r.value, 0.0);
}

/**
 * 空/单点曲线的运行期防御（构造入口应拦——防御分支按数据不足拒绝，
 * 不抛异常，卡 §14.3 注）。
 */
TEST(SelCapabilityCurve, EmptyOrSinglePointCurveIsDataInsufficient)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-02"},
                  std::vector<std::string>{});

    const LinearCurveEvaluator ev;
    const CatalogIdentity id{"cat-demo", "v1", {}, "demo"};

    PerformanceCurve empty;
    empty.curveId = "c-empty";
    empty.xUnit = "rad/s";
    empty.yUnit = "N*m";
    empty.catalog = id;

    const CurveQueryResult re = ev.evaluate(empty, 100.0);
    EXPECT_FALSE(re.ok());
    EXPECT_EQ(re.rejectCode, std::string(kSelCurveIntervalInvalid));

    PerformanceCurve single = empty;
    single.curveId = "c-single";
    single.points = {{100.0, 1.0}};   // 单点（应走固定额定值口径——卡 §6.4）
    const CurveQueryResult rs = ev.evaluate(single, 100.0);
    EXPECT_FALSE(rs.ok());
    EXPECT_EQ(rs.rejectCode, std::string(kSelCurveIntervalInvalid));
}

// =====================================================================
// 契约边界与确定性
// =====================================================================

/** 查询横坐标非有限：调用方违约 fail-fast（NFR-COR-03——卡 §14.3 @throws）。 */
TEST(SelCapabilityCurve, NonFiniteQueryFailsFast)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-03"},
                  std::vector<std::string>{});

    const LinearCurveEvaluator ev;
    const PerformanceCurve curve = makeGoldenTorqueCurve();
    const double nan = std::nan("");
    EXPECT_THROW(static_cast<void>(ev.evaluate(curve, nan)), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(ev.evaluate(curve, std::numeric_limits<double>::infinity())),
                 std::invalid_argument);
}

/** 评估器纯函数性（NFR-COR-01/02）：同曲线同查询恒同结果。 */
TEST(SelCapabilityCurve, EvaluatorIsPureAndDeterministic)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-01", "NFR-COR-02"},
                  std::vector<std::string>{});

    const LinearCurveEvaluator ev;
    const PerformanceCurve curve = makeGoldenTorqueCurve();
    const CurveQueryResult a = ev.evaluate(curve, 123.0);
    const CurveQueryResult b = ev.evaluate(curve, 123.0);
    EXPECT_EQ(a.outcome, b.outcome);
    EXPECT_EQ(a.value, b.value);
    EXPECT_EQ(a.xLow, b.xLow);
    EXPECT_EQ(a.xHigh, b.xHigh);
}

/**
 * 曲线内容身份（CON-05/卡 §6.1）：点集任一变更→内容身份变更（目录能力
 * 曲线变更→依赖切片失效的可定位判据，卡 §4.2/evidence §5.3）。
 */
TEST(SelCapabilityCurve, CurveContentIdentityTracksPointChanges)
{
    IRD_TEST_INFO(std::vector<std::string>{"CON-05"},
                  std::vector<std::string>{});

    PerformanceCurve a = makeGoldenTorqueCurve();
    PerformanceCurve b = a;
    b.points[1].y = 4.500001;   // 单点 y 微变
    // 内容身份由点集派生——点变更后重算 b 的身份（拷贝来的旧身份不自动
    // 跟随，这正是"变更任何字节→新内容身份"的判据语义，卡 §6.1）。
    b.contentIdentity = computeCurveContentIdentity(b);

    EXPECT_NE(a.contentIdentity, b.contentIdentity);
    EXPECT_EQ(a.contentIdentity, computeCurveContentIdentity(a));   // 稳定重算一致
}
