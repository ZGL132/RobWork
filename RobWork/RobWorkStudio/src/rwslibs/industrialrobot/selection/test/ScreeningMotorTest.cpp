/**
 * @file   ScreeningMotorTest.cpp
 * @brief  电机硬筛选黄金表用例组（SelScreeningMotor）——SEL-03 九维
 *         （连续/峰值转矩、转速、功率、过载持续时间、工作制、电压、
 *         温度降额、制动/保持、安全系数）＋安装兼容＋多工况合并＋
 *         多轴遍历＋确定性＋取消截断的黄金数据对照。
 *
 * 设计依据：
 *   - units/selection.md §7.1/§7.2（电机硬筛选流程与纪律——逐维独立
 *     不短路；缺数据标数据不足）、§10.1/§10.3/§10.4（记录类型/原因
 *     词表/稳定排序）、§14.4（IHardConstraintSelector 契约）、§15.2 V2
 *     组（电机硬筛选故障注入矩阵——"刚好满足/不足"边界、缺失字段、
 *     曲线区间外、每项失败原因完整记录）
 *   - 需求 SEL-03（电机筛选九维）、SEL-06/ERR-01（每淘汰项含实际值和
 *     阈值）、AT-08（选型验证口径）、NFR-COR-01/02（解析黄金值对照/
 *     确定性）
 *   - 任务契约 tasks/foundation/WP-19-T04.json acceptance 1/2
 *
 * 黄金值口径（附录 D 精神）：全部黄金值为一位小数以内的解析算例——
 * 判定边界与期望值同为字面值，浮点噪声由 core C7 绝对容差（torque
 * 1e-9）吸收；原因比较字段（actual/required）按值精确断言。
 *
 * ★ 接口消费纪律：全部用例经公共接口消费（HardConstraintSelector 对象
 *   ＋IHardConstraintSelector 契约测试面）——不留只测内部函数的盲区
 *   （WP-20-T03 教训前移）；接口路径钉扎归
 *   contract_test/HardScreeningContractTest.cpp。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/selection/CatalogProvider.hpp>
#include <sdurws/ird/selection/CatalogTypes.hpp>
#include <sdurws/ird/selection/DiagCodes.hpp>   // kSelCurveExtrapolationDenied（曲线外推缺口断言）
#include <sdurws/ird/selection/Screening.hpp>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯

#include "CatalogTestSupport.hpp"  // testsupport 基线包构造（全链路用例——T03 交接面）

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace sdurws::ird::selection;
using namespace sdurws::ird;  // 使限定符 core:: 可见（ObjectId/ContentIdentity 直用）

namespace {

// =====================================================================
// 黄金数据（黄金表——集中定义，用例逐行消费）
// =====================================================================

/// 黄金电机 M-GOLD：全字段齐备的基准型号（各维黄金值的选择使默认工作点
/// 全维通过、单点变异只触发目标维度——黄金表"可行基准＋逐维不可行"结构）。
MotorCatalogEntry makeGoldenMotor()
{
    MotorCatalogEntry m;
    m.modelId = "M-GOLD";
    m.vendor = "Golden";
    m.displayName = "黄金基准电机";
    m.catalog = CatalogIdentity{"cat-gold", "v1", core::ContentIdentity{}, "黄金表测试目录"};
    m.ratedTorque = 4.0;    // 额定连续转矩，N·m
    m.peakTorque = 10.0;    // 峰值转矩，N·m
    m.ratedSpeed = 150.0;   // 额定转速，rad/s
    m.maxSpeed = 300.0;     // 最高转速，rad/s
    m.ratedPower = 1500.0;  // 额定功率，W
    m.overload = OverloadSpec{12.0, 8.0};  // 过载 12 N·m 允许 8 s
    m.dutyClass = "S1";     // 工作制词表值
    m.ratedVoltage = 220.0; // 额定电压，V
    m.thermal = ThermalDerating{25.0, 0.9};  // 参考档位 25（°C 语义），每档系数 0.9
    m.brakeTorque = 12.0;   // 制动能力，N·m
    m.holdingTorque = 15.0; // 保持能力，N·m
    m.rotorInertia = 0.01;  // 转子惯量，kg·m²（筛选不消费——字段完备性）
    m.mass = 6.0;           // 质量，kg（同上）
    m.mounting = MountSpec{"flangeA", "shaftB"};
    m.status = ValidationStatus::Valid;
    return m;
}

/// 黄金工作点（默认全供给——全部黄金值处于 M-GOLD 能力域内）：
/// τ_rms=3.0/4.0、τ_peak=7.0/10.0、ω_peak=200/300、ω_rms=120/150、
/// P_peak=1200/1500、P_rms=700/1500（比值含义：工作点/能力）。
AxisWorkpointFacts makeGoldenFacts(const core::ObjectId& axis)
{
    AxisWorkpointFacts f;
    f.jointId = axis;
    f.caseId = "case-1";
    // 电机侧工作点（映射事实的黄金值——组合校核段供给形态）。
    f.motorTorqueRms = 3.0;    // N·m
    f.motorTorquePeak = 7.0;   // N·m
    f.motorSpeedPeak = 200.0;  // rad/s
    f.motorSpeedRms = 120.0;   // rad/s
    f.motorPowerPeak = 1200.0; // W
    f.motorPowerRms = 700.0;   // W
    f.peakDuration = 5.0;      // s（峰值段 5 s ＜ 过载窗口 8 s——过载维通过）
    // 关节侧（电机筛选不消费——缺省 nullopt 即可）。
    // 保持/外载荷/安装需求：nullopt＝对应维度不适用（黄金默认无这些工况）。
    f.atTime = 1.5;       // s
    f.segmentId = "seg-A";
    return f;
}

/// 黄金筛选条件（默认全不适用——仅转矩/转速/功率/过载维度由工作点驱动）。
ScreeningCriteria makeGoldenCriteria()
{
    ScreeningCriteria c;
    c.safetyFactor = 1.0;  // 不加严（安全系数维度不启用）
    return c;
}

/// 单候选快照构造（黄金电机入快照——直接值构造，CatalogIdentity 回填
/// 由调用方按需调整）。
CatalogPackageSnapshot makeSnapshot(std::vector<MotorCatalogEntry> motors)
{
    CatalogPackageSnapshot s;
    s.manifest.formatVersion = kCatalogFormatVersion;
    s.manifest.identity.catalogId = "cat-gold";
    s.manifest.identity.version = "v1";
    s.manifest.identity.source = "黄金表测试目录";
    s.motors = std::move(motors);
    return s;
}

/// 按词表 token 查找原因（找到返回首条——逐因断言统一入口）。
std::optional<RejectionReason> findReason(const FeasibilityRecord& rec, ReasonToken token)
{
    for (const RejectionReason& r : rec.reasons) {
        if (r.token == token) {
            return r;
        }
    }
    return std::nullopt;
}

/// 记录级黄金断言（verdict＋原因 token 全表——黄金行的期望侧结构）。
/// @param expectTokens [in] 期望原因 token 集合（须与记录 reasons 的
///        token 序一致——稳定排序后的词表序）
void expectRecordShape(const FeasibilityRecord& rec, VerdictKind verdict,
                       const std::vector<ReasonToken>& expectTokens,
                       std::size_t expectGaps, const std::string& modelId)
{
    EXPECT_EQ(rec.candidateModelId, modelId);
    EXPECT_EQ(rec.deviceKind, DeviceKind::Motor);
    EXPECT_EQ(rec.verdict, verdict);
    ASSERT_EQ(rec.reasons.size(), expectTokens.size())
        << "原因条数不符（modelId=" << modelId << "）";
    for (std::size_t i = 0; i < expectTokens.size(); ++i) {
        EXPECT_EQ(rec.reasons[i].token, expectTokens[i])
            << "第 " << i << " 条原因 token 不符（词表序）";
    }
    EXPECT_EQ(rec.gaps.size(), expectGaps);
}

/// 复制夹具（每用例独立对象——防用例间通过引用互染）。
struct Fixture {
    CatalogPackageSnapshot snapshot;   ///< 单黄金候选快照
    AxisWorkpointFacts facts;          ///< 黄金工作点
    ScreeningCriteria criteria;        ///< 黄金条件
    core::ObjectId axis = core::ObjectId::generate();  ///< 轴对象 ID（每夹具一副本）
};

/// 构造标准夹具（黄金电机＋黄金工作点＋黄金条件）。
Fixture makeFixture()
{
    Fixture fx;
    fx.snapshot = makeSnapshot({makeGoldenMotor()});
    fx.facts = makeGoldenFacts(fx.axis);
    fx.criteria = makeGoldenCriteria();
    return fx;
}

}  // namespace

// =====================================================================
// 黄金行 0：可行基准（全维通过——黄金表的"可行型号"行，SEL-03）
// =====================================================================

/**
 * 黄金基准：默认工作点全部位于 M-GOLD 能力域内（比值见 makeGoldenFacts
 * 注），无任何条件维度启用——期望 Feasible、零原因零缺口。
 */
TEST(SelScreeningMotor, GoldenFeasibleBaseline)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-03"}, std::vector<std::string>{"AT-08"});

    const Fixture fx = makeFixture();
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenMotors(fx.snapshot, {fx.facts}, fx.criteria, nullptr);

    ASSERT_EQ(recs.size(), 1U);
    expectRecordShape(recs[0], VerdictKind::Feasible, {}, 0U, "M-GOLD");
    // 记录键＝"<modelId>|<jointId>"（T04 逐候选路径契约——登记 §19.3 T04 ①）。
    EXPECT_EQ(recs[0].id, "M-GOLD|" + fx.axis.toCanonical());
    // 直调路径的身份字段：切片/映射身份全零（诚实标记——无 evidence 切片）。
    EXPECT_FALSE(recs[0].inputSliceId.isValid());
    EXPECT_FALSE(recs[0].mappingId.isValid());
}

// =====================================================================
// 黄金行 1：连续转矩（"刚好满足/不足"边界——SEL-03 V2 组）
// =====================================================================

/** 边界：τ_rms＝额定连续转矩（恰好满足，≤ 含等）——不淘汰。 */
TEST(SelScreeningMotor, ContinuousTorqueExactBoundaryPasses)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-03"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    fx.facts.motorTorqueRms = 4.0;  // N·m＝额定（边界值）
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenMotors(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    expectRecordShape(recs[0], VerdictKind::Feasible, {}, 0U, "M-GOLD");
}

/** 不足：τ_rms=4.5 ＞ 额定 4.0——淘汰原因含实际值/阈值/单位/来源。 */
TEST(SelScreeningMotor, ContinuousTorqueInsufficientGoldenRow)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-03", "SEL-06"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    fx.facts.motorTorqueRms = 4.5;  // N·m（超额定 0.5）
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenMotors(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    expectRecordShape(recs[0], VerdictKind::Rejected, {ReasonToken::TorqueContinuousInsufficient},
                      0U, "M-GOLD");
    // 黄金断言（ERR-01 比较型四要素齐备）：actual=4.5、required=4.0、
    // 单位 N*m、阈值来源指向目录列 rated_torque_nm。
    const std::optional<RejectionReason> r
        = findReason(recs[0], ReasonToken::TorqueContinuousInsufficient);
    ASSERT_TRUE(r.has_value());
    EXPECT_DOUBLE_EQ(r->actual, 4.5);
    EXPECT_DOUBLE_EQ(r->required, 4.0);
    EXPECT_EQ(r->unit, "N*m");
    EXPECT_NE(r->thresholdSource.find("rated_torque_nm"), std::string::npos);
    EXPECT_EQ(r->caseId, "case-1");
    EXPECT_DOUBLE_EQ(r->atTime, 1.5);
    EXPECT_EQ(r->segmentId, "seg-A");
    EXPECT_FALSE(r->diagRef.has_value());  // SEL-* 稳定码映射随 WP-19-T06（T04 恒空）
}

// =====================================================================
// 黄金行 2：峰值转矩／转速（词表双 token 独立判定）
// =====================================================================

/** 峰值不足：τ_peak=10.5 ＞ 峰值 10.0——torque-peak-insufficient。 */
TEST(SelScreeningMotor, PeakTorqueInsufficientGoldenRow)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-03"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    fx.facts.motorTorquePeak = 10.5;  // N·m
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenMotors(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    expectRecordShape(recs[0], VerdictKind::Rejected, {ReasonToken::TorquePeakInsufficient},
                      0U, "M-GOLD");
    const std::optional<RejectionReason> r = findReason(recs[0], ReasonToken::TorquePeakInsufficient);
    ASSERT_TRUE(r.has_value());
    EXPECT_DOUBLE_EQ(r->actual, 10.5);
    EXPECT_DOUBLE_EQ(r->required, 10.0);
    EXPECT_EQ(r->unit, "N*m");
    EXPECT_NE(r->thresholdSource.find("peak_torque_nm"), std::string::npos);
}

/** 最高转速不足：ω_peak=310 ＞ 300——speed-insufficient（max_speed 侧）。 */
TEST(SelScreeningMotor, SpeedPeakInsufficientGoldenRow)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-03"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    fx.facts.motorSpeedPeak = 310.0;  // rad/s
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenMotors(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    expectRecordShape(recs[0], VerdictKind::Rejected, {ReasonToken::SpeedInsufficient}, 0U,
                      "M-GOLD");
    const std::optional<RejectionReason> r = findReason(recs[0], ReasonToken::SpeedInsufficient);
    ASSERT_TRUE(r.has_value());
    EXPECT_DOUBLE_EQ(r->actual, 310.0);
    EXPECT_DOUBLE_EQ(r->required, 300.0);
    EXPECT_EQ(r->unit, "rad/s");
    EXPECT_NE(r->thresholdSource.find("max_speed"), std::string::npos);
}

/** 额定转速不足：ω_rms=155 ＞ 150——speed-insufficient（rated_speed 侧）。 */
TEST(SelScreeningMotor, SpeedRmsInsufficientGoldenRow)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-03"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    fx.facts.motorSpeedRms = 155.0;  // rad/s
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenMotors(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    expectRecordShape(recs[0], VerdictKind::Rejected, {ReasonToken::SpeedInsufficient}, 0U,
                      "M-GOLD");
    const std::optional<RejectionReason> r = findReason(recs[0], ReasonToken::SpeedInsufficient);
    ASSERT_TRUE(r.has_value());
    EXPECT_DOUBLE_EQ(r->actual, 155.0);
    EXPECT_DOUBLE_EQ(r->required, 150.0);
    EXPECT_NE(r->thresholdSource.find("rated_speed"), std::string::npos);
}

// =====================================================================
// 黄金行 3：功率（固定额定值口径＋曲线口径＋区间外分轨——落位细化 ⑤）
// =====================================================================

/** 额定口径不足：P_peak=1600 ＞ 1500（无曲线——固定额定值口径）。 */
TEST(SelScreeningMotor, PowerInsufficientRatedFieldGoldenRow)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-03"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    fx.facts.motorPowerPeak = 1600.0;  // W（无功率曲线→额定 1500 兜底）
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenMotors(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    expectRecordShape(recs[0], VerdictKind::Rejected, {ReasonToken::PowerInsufficient}, 0U,
                      "M-GOLD");
    const std::optional<RejectionReason> r = findReason(recs[0], ReasonToken::PowerInsufficient);
    ASSERT_TRUE(r.has_value());
    EXPECT_DOUBLE_EQ(r->actual, 1600.0);
    EXPECT_DOUBLE_EQ(r->required, 1500.0);
    EXPECT_EQ(r->unit, "W");
    // 固定额定值口径的来源显式（§6.4——不得用额定值伪造曲线）。
    EXPECT_NE(r->thresholdSource.find("rated_power_w"), std::string::npos);
}

/**
 * 曲线口径（区间内）：条目声明 speed→power 曲线（黄金点 (100,2000)、
 * (300,1400)），P_peak=1500 @ ω_peak=200 → 曲线插值
 * 2000−(2000−1400)·(200−100)/(300−100)＝1700 W ＞ 1500——通过，
 * thresholdSource 携带曲线 ID（曲线口径优先于额定字段——落位细化 ⑤）。
 */
TEST(SelScreeningMotor, PowerCurveWithinPassesWithCurveSource)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-03"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    PerformanceCurve curve;
    curve.curveId = "curve-pw-gold";
    curve.xQuantity = kQuantitySpeed;
    curve.yQuantity = kQuantityPower;
    curve.xUnit = "rad/s";
    curve.yUnit = "W";
    curve.points = {{100.0, 2000.0}, {300.0, 1400.0}};
    curve.catalog = fx.snapshot.motors[0].catalog;
    curve.contentIdentity = computeCurveContentIdentity(curve);
    fx.snapshot.curves.push_back(curve);
    fx.snapshot.motors[0].curves.push_back(CurveRef{"curve-pw-gold", kQuantitySpeed,
                                                    kQuantityPower});
    fx.facts.motorPowerPeak = 1500.0;  // W ＜ 曲线能力 1700

    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenMotors(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    expectRecordShape(recs[0], VerdictKind::Feasible, {}, 0U, "M-GOLD");
}

/** 曲线口径不足：P_peak=1750 ＞ 曲线能力 1700——required＝曲线插值黄金值。 */
TEST(SelScreeningMotor, PowerCurveExceededGoldenRow)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-03", "SEL-06"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    PerformanceCurve curve;
    curve.curveId = "curve-pw-gold";
    curve.xQuantity = kQuantitySpeed;
    curve.yQuantity = kQuantityPower;
    curve.xUnit = "rad/s";
    curve.yUnit = "W";
    curve.points = {{100.0, 2000.0}, {300.0, 1400.0}};
    curve.catalog = fx.snapshot.motors[0].catalog;
    curve.contentIdentity = computeCurveContentIdentity(curve);
    fx.snapshot.curves.push_back(curve);
    fx.snapshot.motors[0].curves.push_back(CurveRef{"curve-pw-gold", kQuantitySpeed,
                                                    kQuantityPower});
    fx.facts.motorPowerPeak = 1750.0;  // W ＞ 1700

    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenMotors(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    expectRecordShape(recs[0], VerdictKind::Rejected, {ReasonToken::PowerInsufficient}, 0U,
                      "M-GOLD");
    const std::optional<RejectionReason> r = findReason(recs[0], ReasonToken::PowerInsufficient);
    ASSERT_TRUE(r.has_value());
    EXPECT_DOUBLE_EQ(r->actual, 1750.0);
    EXPECT_DOUBLE_EQ(r->required, 1700.0);  // 曲线插值黄金值（解析：见上行用例）
    EXPECT_NE(r->thresholdSource.find("curve-pw-gold"), std::string::npos);
}

/**
 * 曲线区间外（V2"曲线区间外"行）：ω_peak=350 落在 [100,300] 外——
 * 查询拒绝＝数据不足分轨（不判淘汰、不静默外推；缺口携带
 * SEL-CURVE-EXTRAPOLATION-DENIED 码），verdict＝DataInsufficient。
 */
TEST(SelScreeningMotor, PowerCurveExtrapolationBecomesDataGap)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-03"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    PerformanceCurve curve;
    curve.curveId = "curve-pw-gold";
    curve.xQuantity = kQuantitySpeed;
    curve.yQuantity = kQuantityPower;
    curve.xUnit = "rad/s";
    curve.yUnit = "W";
    curve.points = {{100.0, 2000.0}, {300.0, 1400.0}};
    curve.catalog = fx.snapshot.motors[0].catalog;
    curve.contentIdentity = computeCurveContentIdentity(curve);
    fx.snapshot.curves.push_back(curve);
    fx.snapshot.motors[0].curves.push_back(CurveRef{"curve-pw-gold", kQuantitySpeed,
                                                    kQuantityPower});
    fx.facts.motorSpeedPeak = 350.0;   // rad/s——区间外（黄金默认 200）
    fx.facts.motorPowerPeak = 1000.0;  // W（数值本身不超额定——拒绝来自外推禁令）

    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenMotors(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    // ω_peak=350 同时触发转速淘汰（310＞300 同理）——期望原因＋缺口并存。
    expectRecordShape(recs[0], VerdictKind::Rejected, {ReasonToken::SpeedInsufficient}, 1U,
                      "M-GOLD");
    ASSERT_EQ(recs[0].gaps.size(), 1U);
    EXPECT_EQ(recs[0].gaps[0].dimension, "power-curve");
    EXPECT_EQ(recs[0].gaps[0].diagCode, std::string(kSelCurveExtrapolationDenied));
}

// =====================================================================
// 黄金行 4：过载持续时间（触发条件式维度——τ_peak>额定段才核查）
// =====================================================================

/** 过载时间不足：τ_peak=8（＞额定 4——过载区），峰值段 10 s ＞ 窗口 8 s。 */
TEST(SelScreeningMotor, OverloadTimeInsufficientGoldenRow)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-03", "SEL-06"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    fx.facts.motorTorquePeak = 8.0;   // N·m（＞额定 4——进入过载区；≤峰值 10）
    fx.facts.peakDuration = 10.0;     // s ＞ 窗口 8
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenMotors(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    expectRecordShape(recs[0], VerdictKind::Rejected, {ReasonToken::OverloadTimeInsufficient},
                      0U, "M-GOLD");
    const std::optional<RejectionReason> r
        = findReason(recs[0], ReasonToken::OverloadTimeInsufficient);
    ASSERT_TRUE(r.has_value());
    EXPECT_DOUBLE_EQ(r->actual, 10.0);
    EXPECT_DOUBLE_EQ(r->required, 8.0);
    EXPECT_EQ(r->unit, "s");
    EXPECT_NE(r->thresholdSource.find("overload_duration_s"), std::string::npos);
}

/** 未过载不触发：τ_peak=3.5 ≤ 额定 4（额定区内）——峰值段时长不核查。 */
TEST(SelScreeningMotor, OverloadDimensionNotTriggeredInContinuousZone)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-03"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    fx.facts.motorTorquePeak = 3.5;   // N·m（额定区内）
    fx.facts.peakDuration = 999.0;    // s（荒谬值——不触发即不判，证明条件式）
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenMotors(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    expectRecordShape(recs[0], VerdictKind::Feasible, {}, 0U, "M-GOLD");
}

/** 过载区但目录未声明过载能力（overload 缺失）——数据缺口，不默认通过。 */
TEST(SelScreeningMotor, OverloadZoneWithoutCatalogSpecIsDataGap)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-03"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    fx.snapshot.motors[0].overload = std::nullopt;  // 目录未声明
    fx.facts.motorTorquePeak = 8.0;                 // N·m（过载区）
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenMotors(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    expectRecordShape(recs[0], VerdictKind::DataInsufficient, {}, 1U, "M-GOLD");
    EXPECT_EQ(recs[0].gaps[0].dimension, "overload-duration");
}

// =====================================================================
// 黄金行 5：工作制／电压（条件驱动维度——条件缺失＝不适用，落位细化 ⑧）
// =====================================================================

/** 工作制不匹配：需求 S2 vs 目录 S1——duty-mismatch（文本类原因）。 */
TEST(SelScreeningMotor, DutyMismatchGoldenRow)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-03"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    fx.criteria.requiredDutyClass = "S2";
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenMotors(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    expectRecordShape(recs[0], VerdictKind::Rejected, {ReasonToken::DutyMismatch}, 0U,
                      "M-GOLD");
    const std::optional<RejectionReason> r = findReason(recs[0], ReasonToken::DutyMismatch);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->actualText, "S1");
    EXPECT_EQ(r->requiredText, "S2");
    EXPECT_EQ(r->unit, "");           // 文本类比较无数值单位
    EXPECT_NE(r->thresholdSource.find("duty_class"), std::string::npos);
}

/** 电压不匹配：需求 230 V vs 额定 220 V（容差 0＝精确相等）——mismatch。 */
TEST(SelScreeningMotor, VoltageMismatchGoldenRow)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-03"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    fx.criteria.requiredVoltage = 230.0;  // V
    fx.criteria.voltageRelativeTolerance = 0.0;
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenMotors(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    expectRecordShape(recs[0], VerdictKind::Rejected, {ReasonToken::VoltageMismatch}, 0U,
                      "M-GOLD");
    const std::optional<RejectionReason> r = findReason(recs[0], ReasonToken::VoltageMismatch);
    ASSERT_TRUE(r.has_value());
    EXPECT_DOUBLE_EQ(r->actual, 220.0);
    EXPECT_DOUBLE_EQ(r->required, 230.0);
    EXPECT_EQ(r->unit, "V");
    EXPECT_NE(r->thresholdSource.find("rated_voltage_v"), std::string::npos);
}

/** 电压容差内匹配：相对容差 0.1 → |220−230|=10 ≤ 23——通过（附录 D C4）。 */
TEST(SelScreeningMotor, VoltageWithinTolerancePasses)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-03"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    fx.criteria.requiredVoltage = 230.0;             // V
    fx.criteria.voltageRelativeTolerance = 0.1;      // 0.1×230=23 V 界
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenMotors(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    expectRecordShape(recs[0], VerdictKind::Feasible, {}, 0U, "M-GOLD");
}

/** 条件要电压而目录未声明（ratedVoltage 缺失）——数据缺口，不默认匹配。 */
TEST(SelScreeningMotor, VoltageMissingInCatalogIsDataGap)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-03"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    fx.snapshot.motors[0].ratedVoltage = std::nullopt;
    fx.criteria.requiredVoltage = 220.0;  // V
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenMotors(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    expectRecordShape(recs[0], VerdictKind::DataInsufficient, {}, 1U, "M-GOLD");
    EXPECT_EQ(recs[0].gaps[0].dimension, "voltage");
}

// =====================================================================
// 黄金行 6：温度降额（落位细化 ④ 档位公式——f＝factorPerRef^n）
// =====================================================================

/**
 * 降额复判不足：环境 26（参考 25）→ n＝ceil(1)＝1 档、f＝0.9——
 * 折减后连续能力 4.0×0.9＝3.6 ＜ 工作点 3.8（原始判定 3.8 ≤ 4.0 通过，
 * 降额维独立失败——两维分轨）。
 */
TEST(SelScreeningMotor, ThermalDeratingInsufficientGoldenRow)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-03", "SEL-06"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    fx.facts.motorTorqueRms = 3.8;        // N·m（原始判定通过）
    fx.criteria.ambientTemp = 26.0;       // 档位值（°C 语义）——超出 1 档
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenMotors(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    // 期望：仅温度降额维失败（原始连续转矩维通过——3.8 ≤ 4.0）。
    expectRecordShape(recs[0], VerdictKind::Rejected, {ReasonToken::ThermalDeratingInsufficient},
                      0U, "M-GOLD");
    const std::optional<RejectionReason> r
        = findReason(recs[0], ReasonToken::ThermalDeratingInsufficient);
    ASSERT_TRUE(r.has_value());
    EXPECT_DOUBLE_EQ(r->actual, 3.8);
    EXPECT_DOUBLE_EQ(r->required, 3.6);  // 4.0×0.9（黄金解析值）
    EXPECT_EQ(r->unit, "N*m");
    EXPECT_NE(r->thresholdSource.find("thermal_factor_per_ref"), std::string::npos);
}

/** 半档保守取整：环境 26.5 → n＝ceil(1.5)＝2 档、f＝0.81——同 26.9 档位。 */
TEST(SelScreeningMotor, ThermalDeratingHalfStepRoundsUp)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-03"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    fx.facts.motorTorqueRms = 3.3;        // N·m（0.9^1 折减 3.6 可过、0.9^2 折减 3.24 不可过）
    fx.criteria.ambientTemp = 26.5;       // 半档——保守按 2 档
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenMotors(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    expectRecordShape(recs[0], VerdictKind::Rejected, {ReasonToken::ThermalDeratingInsufficient},
                      0U, "M-GOLD");
    EXPECT_DOUBLE_EQ(findReason(recs[0], ReasonToken::ThermalDeratingInsufficient)->required,
                     3.24);  // 4.0×0.9²（黄金解析值——半档取整证据）
}

/**
 * 极端环境温度饱和不反转（F-641）：ambientTemp=1e30（调用方契约只保证
 * 有限）→ 超出档数经 2e9 上界钳位饱和、f＝0.9^(2e9) 下溢为 0——折减后
 * 能力 0.0：维度照常判不足，能力系数不被反转放大。原实现
 * static_cast<int>(ceil(1e30)) 溢出是未定义行为（x64 饱和为 INT_MIN＝负
 * 档数 → pow 反转 ≫1 → 极端高温反而通过筛选——降额单调性被破坏）。
 */
TEST(SelScreeningMotor, ThermalDeratingExtremeAmbientSaturatesNoInversion_F641)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-03"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    fx.facts.motorTorqueRms = 3.8;   // N·m（工作点恒定——单点变异只动环境温度维）
    fx.criteria.ambientTemp = 1e30;  // 档位值（°C 语义——极端大值）
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenMotors(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    // 期望：降额维照常判不足（溢出不放行——饱和至极限降额而非反转）。
    // f＝0 使连续与峰值两个转矩维同时不足（逐维独立不短路——§7.2），
    // 同 token 两条原因（稳定序＝产生序）。
    expectRecordShape(recs[0], VerdictKind::Rejected,
                      {ReasonToken::ThermalDeratingInsufficient,
                       ReasonToken::ThermalDeratingInsufficient},
                      0U, "M-GOLD");
    const std::optional<RejectionReason> r
        = findReason(recs[0], ReasonToken::ThermalDeratingInsufficient);
    ASSERT_TRUE(r.has_value());
    EXPECT_DOUBLE_EQ(r->actual, 3.8);
    // 折减后能力饱和至 0（0.9^(2e9) 下溢——IEEE754 定义行为），恒不高于
    // 额定 4.0（单调性保险丝：档数增大只让能力不变或更低，绝不升高）。
    EXPECT_DOUBLE_EQ(r->required, 0.0);
    EXPECT_LE(r->required, 4.0);
    // 峰值转矩维同饱和（工作点 7.0 N·m vs 能力 10.0×0＝0——同码两条）。
    ASSERT_EQ(recs[0].reasons.size(), 2U);
    EXPECT_DOUBLE_EQ(recs[0].reasons[1].actual, 7.0);
    EXPECT_DOUBLE_EQ(recs[0].reasons[1].required, 0.0);
    EXPECT_LE(recs[0].reasons[1].required, 10.0);
}

/** 环境不高于参考档位（20 ≤ 25）——不折减（f=1）、无降额原因。 */
TEST(SelScreeningMotor, ThermalDeratingBelowReferenceNoDerating)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-03"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    fx.facts.motorTorqueRms = 3.9;    // N·m（接近额定）
    fx.criteria.ambientTemp = 20.0;   // 档位值 ≤ 参考 25
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenMotors(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    expectRecordShape(recs[0], VerdictKind::Feasible, {}, 0U, "M-GOLD");
}

/** 条件要降额而目录未声明 thermal——数据缺口（不默认不折减）。 */
TEST(SelScreeningMotor, ThermalMissingInCatalogIsDataGap)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-03"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    fx.snapshot.motors[0].thermal = std::nullopt;
    fx.criteria.ambientTemp = 40.0;
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenMotors(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    expectRecordShape(recs[0], VerdictKind::DataInsufficient, {}, 1U, "M-GOLD");
    EXPECT_EQ(recs[0].gaps[0].dimension, "thermal-derating");
}

// =====================================================================
// 黄金行 7：制动／保持（保持工况需求驱动——§7.1"无需求则不适用"）
// =====================================================================

/** 制动不足：保持需求 13 ＞ 制动 12——brake-insufficient（保持 15 仍够）。 */
TEST(SelScreeningMotor, BrakeInsufficientGoldenRow)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-03"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    fx.facts.requiredHoldingTorque = 13.0;  // N·m
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenMotors(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    expectRecordShape(recs[0], VerdictKind::Rejected, {ReasonToken::BrakeInsufficient}, 0U,
                      "M-GOLD");
    const std::optional<RejectionReason> r = findReason(recs[0], ReasonToken::BrakeInsufficient);
    ASSERT_TRUE(r.has_value());
    EXPECT_DOUBLE_EQ(r->actual, 13.0);
    EXPECT_DOUBLE_EQ(r->required, 12.0);
    EXPECT_EQ(r->unit, "N*m");
    EXPECT_NE(r->thresholdSource.find("brake_torque_nm"), std::string::npos);
}

/** 保持不足：保持需求 16 ＞ 保持 15（制动 12 也超——两原因并存，不短路）。 */
TEST(SelScreeningMotor, HoldingInsufficientWithBrakeBothRecorded)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-03"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    fx.facts.requiredHoldingTorque = 16.0;  // N·m——制动（12）与保持（15）双超
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenMotors(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    // 词表序：BrakeInsufficient ＜ HoldingInsufficient（枚举序 8＜9）。
    expectRecordShape(recs[0], VerdictKind::Rejected,
                      {ReasonToken::BrakeInsufficient, ReasonToken::HoldingInsufficient}, 0U,
                      "M-GOLD");
}

/** 无保持需求：制动/保持维度整体不适用（目录缺 brake/holding 也不记缺口）。 */
TEST(SelScreeningMotor, HoldingDimensionsNotApplicableWithoutDemand)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-03"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    fx.snapshot.motors[0].brakeTorque = std::nullopt;
    fx.snapshot.motors[0].holdingTorque = std::nullopt;
    // fx.facts.requiredHoldingTorque 保持 nullopt——维度不适用。
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenMotors(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    expectRecordShape(recs[0], VerdictKind::Feasible, {}, 0U, "M-GOLD");
}

/** 有保持需求而制动能力缺失——brake 维数据缺口（保持维正常判定）。 */
TEST(SelScreeningMotor, BrakeMissingInCatalogIsDataGap)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-03"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    fx.snapshot.motors[0].brakeTorque = std::nullopt;
    fx.facts.requiredHoldingTorque = 10.0;  // N·m（≤ 保持 15——保持维通过）
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenMotors(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    expectRecordShape(recs[0], VerdictKind::DataInsufficient, {}, 1U, "M-GOLD");
    EXPECT_EQ(recs[0].gaps[0].dimension, "brake");
}

// =====================================================================
// 黄金行 8：安全系数（工作点×SF 复判——独立 token，§7.1）
// =====================================================================

/**
 * 安全系数复判不足：SF=1.5——黄金核算（工作点×1.5 vs 能力）：
 * τ_rms 4.5＞4.0、τ_peak 10.5＞10.0、ω_rms 180＞150、P_peak 1800＞1500
 * 四子项超限（ω_peak 300≤300 与 P_rms 1050≤1500 容差内通过）——四条
 * safety-factor-insufficient 按子项执行序稳定输出（同 token 同工况），
 * 原始维零原因（分轨）。
 */
TEST(SelScreeningMotor, SafetyFactorInsufficientGoldenRow)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-03"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    fx.criteria.safetyFactor = 1.5;
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenMotors(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    expectRecordShape(recs[0], VerdictKind::Rejected,
                      {ReasonToken::SafetyFactorInsufficient, ReasonToken::SafetyFactorInsufficient,
                       ReasonToken::SafetyFactorInsufficient, ReasonToken::SafetyFactorInsufficient},
                      0U, "M-GOLD");
    // 子项一：连续转矩（3.0×1.5=4.5 ＞ 额定 4.0）。
    const RejectionReason& r1 = recs[0].reasons[0];
    EXPECT_DOUBLE_EQ(r1.actual, 4.5);
    EXPECT_DOUBLE_EQ(r1.required, 4.0);
    EXPECT_EQ(r1.unit, "N*m");
    EXPECT_NE(r1.thresholdSource.find("SF=1.500000"), std::string::npos);
    // 子项二：峰值转矩（7.0×1.5=10.5 ＞ 10.0）。
    EXPECT_DOUBLE_EQ(recs[0].reasons[1].actual, 10.5);
    EXPECT_DOUBLE_EQ(recs[0].reasons[1].required, 10.0);
    // 子项三：RMS 转速（120×1.5=180 ＞ 额定 150）。
    EXPECT_DOUBLE_EQ(recs[0].reasons[2].actual, 180.0);
    EXPECT_DOUBLE_EQ(recs[0].reasons[2].required, 150.0);
    EXPECT_EQ(recs[0].reasons[2].unit, "rad/s");
    // 子项四：峰值功率（1200×1.5=1800 ＞ 额定 1500——固定额定值口径）。
    EXPECT_DOUBLE_EQ(recs[0].reasons[3].actual, 1800.0);
    EXPECT_DOUBLE_EQ(recs[0].reasons[3].required, 1500.0);
    EXPECT_EQ(recs[0].reasons[3].unit, "W");
}

/** 安全系数全过：SF=1.1——各量 ×1.1 仍在能力域内，Feasible。 */
TEST(SelScreeningMotor, SafetyFactorWithinPasses)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-03"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    fx.criteria.safetyFactor = 1.1;
    // 黄金核算：3.3≤4、7.7≤10、220≤300、132≤150、1320≤1500、770≤1500。
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenMotors(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    expectRecordShape(recs[0], VerdictKind::Feasible, {}, 0U, "M-GOLD");
}

/** SF=1.0（默认）不启用安全系数维——边界工作点不因 SF 维重复记因。 */
TEST(SelScreeningMotor, SafetyFactorOneDoesNotAddDimension)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-03"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    fx.facts.motorTorqueRms = 4.0;  // N·m＝额定（边界——原始维容差内通过）
    fx.criteria.safetyFactor = 1.0;
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenMotors(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    expectRecordShape(recs[0], VerdictKind::Feasible, {}, 0U, "M-GOLD");
}

// =====================================================================
// 黄金行 9：安装兼容（§7.1 ②——兼容性判定非能力值；共用 token 落位细化 ③）
// =====================================================================

/** 法兰接口不匹配：要求 flangeA vs 条目 flangeA 基线——改要求为 flangeX。 */
TEST(SelScreeningMotor, MountingIncompatibleGoldenRow)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-03"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    JointMountRequirement req;
    req.flangeKind = "flangeX";
    fx.facts.mountRequirement = req;
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenMotors(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    expectRecordShape(recs[0], VerdictKind::Rejected, {ReasonToken::MountingIncompatible},
                      0U, "M-GOLD");
    const std::optional<RejectionReason> r
        = findReason(recs[0], ReasonToken::MountingIncompatible);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->actualText, "flangeA");
    EXPECT_EQ(r->requiredText, "flangeX");
    EXPECT_EQ(r->unit, "");
    EXPECT_NE(r->thresholdSource.find("mountRequirement"), std::string::npos);
}

/** 条目法兰缺失（空）而关节有要求——数据缺口（不默认兼容）。 */
TEST(SelScreeningMotor, MountingMissingInCatalogIsDataGap)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-03"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    fx.snapshot.motors[0].mounting.flangeKind = "";
    JointMountRequirement req;
    req.flangeKind = "flangeA";
    fx.facts.mountRequirement = req;
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenMotors(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    expectRecordShape(recs[0], VerdictKind::DataInsufficient, {}, 1U, "M-GOLD");
    EXPECT_EQ(recs[0].gaps[0].dimension, "mounting-flange");
}

/** 无安装要求（nullopt）——安装维不适用（条目接口值任填不判）。 */
TEST(SelScreeningMotor, MountingNotApplicableWithoutRequirement)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-03"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    fx.snapshot.motors[0].mounting = MountSpec{"", ""};  // 条目接口空——但无要求
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenMotors(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    expectRecordShape(recs[0], VerdictKind::Feasible, {}, 0U, "M-GOLD");
}

// =====================================================================
// 多原因／多工况／多轴／确定性与取消（§10.4/§14.4 契约面）
// =====================================================================

/**
 * 多维失败不短路（§7.2）：连续转矩＋转速＋工作制三维同时失败——
 * 三条原因并存且按词表序稳定排序（TorqueContinuous → Speed → Duty）。
 */
TEST(SelScreeningMotor, MultiDimensionFailuresAllRecordedInLexicographicOrder)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-03", "SEL-06"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    fx.facts.motorTorqueRms = 5.0;      // N·m ＞ 4.0（连续转矩维失败）
    fx.facts.motorSpeedPeak = 320.0;    // rad/s ＞ 300（转速维失败）
    fx.criteria.requiredDutyClass = "S3";  // ≠ S1（工作制维失败）
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenMotors(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    // 词表序（枚举序＝§10.3 组内行序）：torque-continuous(0) → speed(2)
    // → duty(5)。
    expectRecordShape(recs[0], VerdictKind::Rejected,
                      {ReasonToken::TorqueContinuousInsufficient, ReasonToken::SpeedInsufficient,
                       ReasonToken::DutyMismatch},
                      0U, "M-GOLD");
}

/**
 * 同轴多工况合并（EVI-02 精神——任一工况失败即不可用，原因按工况定位）：
 * case-1 连续转矩失败、case-2 峰值转速失败——单记录双原因，caseId 各归。
 */
TEST(SelScreeningMotor, MultiCaseMergedIntoSingleRecordWithCaseIds)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-03"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    AxisWorkpointFacts f1 = fx.facts;
    f1.caseId = "case-1";
    f1.motorTorqueRms = 5.0;   // N·m——case-1 连续转矩失败
    AxisWorkpointFacts f2 = fx.facts;
    f2.caseId = "case-2";
    f2.motorSpeedPeak = 320.0;  // rad/s——case-2 转速失败
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenMotors(fx.snapshot, {f1, f2}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);  // 同候选同轴单记录
    expectRecordShape(recs[0], VerdictKind::Rejected,
                      {ReasonToken::TorqueContinuousInsufficient, ReasonToken::SpeedInsufficient},
                      0U, "M-GOLD");
    EXPECT_EQ(recs[0].reasons[0].caseId, "case-1");
    EXPECT_EQ(recs[0].reasons[1].caseId, "case-2");
}

/** 多轴遍历：两轴 facts → 每候选两条记录（轴序＝facts 首现序）。 */
TEST(SelScreeningMotor, MultiAxisProducesRecordPerAxis)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-03"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    core::ObjectId axis2 = core::ObjectId::generate();
    AxisWorkpointFacts f1 = fx.facts;                       // 轴 1（黄金点——通过）
    AxisWorkpointFacts f2 = makeGoldenFacts(axis2);         // 轴 2
    f2.caseId = "case-j2";
    f2.motorTorqueRms = 5.0;                                // 轴 2 失败
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenMotors(fx.snapshot, {f1, f2}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 2U);
    EXPECT_EQ(recs[0].axisId, fx.axis);   // 首现序
    EXPECT_EQ(recs[0].verdict, VerdictKind::Feasible);
    EXPECT_EQ(recs[1].axisId, axis2);
    EXPECT_EQ(recs[1].verdict, VerdictKind::Rejected);
    EXPECT_EQ(recs[1].id, "M-GOLD|" + axis2.toCanonical());
}

/** 确定性（NFR-COR-02）：同输入两次调用结果逐字段全等。 */
TEST(SelScreeningMotor, DeterministicSameInputSameOutput)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-03"}, std::vector<std::string>{"NFR-COR-02"});

    Fixture fx = makeFixture();
    fx.facts.motorTorqueRms = 5.0;
    fx.criteria.requiredDutyClass = "S2";
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> a
        = selector.screenMotors(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    const std::vector<FeasibilityRecord> b
        = selector.screenMotors(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        EXPECT_TRUE(a[i] == b[i]) << "第 " << i << " 条记录两次调用不一致";
    }
}

/** 工作点全缺失：全部电机侧量 nullopt——全维数据缺口，DataInsufficient。 */
TEST(SelScreeningMotor, AllWorkpointsMissingBecomesDataInsufficient)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-03"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    AxisWorkpointFacts empty = fx.facts;
    empty.motorTorqueRms = std::nullopt;
    empty.motorTorquePeak = std::nullopt;
    empty.motorSpeedPeak = std::nullopt;
    empty.motorSpeedRms = std::nullopt;
    empty.motorPowerPeak = std::nullopt;
    empty.motorPowerRms = std::nullopt;
    empty.peakDuration = std::nullopt;
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenMotors(fx.snapshot, {empty}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    // 缺工作点数据＝数据不足（不把缺动力学证据当零负载——§7.2）：
    // 零淘汰原因＋六维缺口（连续/峰值转矩＋峰值/RMS 转速＋峰值/RMS 功率；
    // 过载维触发条件不满足不记）。
    expectRecordShape(recs[0], VerdictKind::DataInsufficient, {}, 6U, "M-GOLD");
}

/** 全链路（目录导入→装配→筛选——与 WP-19-T03 交接面）：基线电机 M-100
 *  （额定 4.5 N·m）在 τ_rms=5.2 工作点下淘汰，actual/required 黄金对照。 */
TEST(SelScreeningMotor, EndToEndFromCatalogImportToScreening)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-03"}, std::vector<std::string>{"AT-08"});

    // ①T03 导入链：合法基线包 → 校验通过 → 装配快照。
    const CatalogImporter importer;
    const CatalogValidationReport rep
        = importer.validate(testsupport::makeBaselineInput(), testsupport::makeBaselineManifest());
    ASSERT_TRUE(rep.ok());
    const CatalogPackageSnapshot snapshot
        = importer.assemble(testsupport::makeBaselineInput(), testsupport::makeBaselineManifest());
    ASSERT_EQ(snapshot.motors.size(), 2U);  // M-100 ＋ M-200

    // ②工作点：M-100 超额定（4.5）不超峰值（11）——黄金淘汰行。
    core::ObjectId axis = core::ObjectId::generate();
    AxisWorkpointFacts f;
    f.jointId = axis;
    f.caseId = "case-load";
    f.motorTorqueRms = 5.2;    // N·m
    f.motorTorquePeak = 9.0;   // N·m（≤11——峰值维通过）
    f.motorSpeedPeak = 280.0;  // rad/s（≤300）
    f.motorSpeedRms = 140.0;   // rad/s（≤150）
    f.motorPowerPeak = 1900.0; // W（≤2000）
    f.motorPowerRms = 950.0;   // W（≤2000）
    f.peakDuration = 4.0;      // s

    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenMotors(snapshot, {f}, makeGoldenCriteria(), nullptr);
    ASSERT_EQ(recs.size(), 2U);  // 两候选各一条
    // M-100：5.2 ＞ 4.5——淘汰（黄金值）。
    const FeasibilityRecord* m100 = nullptr;
    const FeasibilityRecord* m200 = nullptr;
    for (const FeasibilityRecord& r : recs) {
        if (r.candidateModelId == "M-100") { m100 = &r; }
        if (r.candidateModelId == "M-200") { m200 = &r; }
    }
    ASSERT_NE(m100, nullptr);
    ASSERT_NE(m200, nullptr);
    EXPECT_EQ(m100->verdict, VerdictKind::Rejected);
    ASSERT_EQ(m100->reasons.size(), 1U);
    EXPECT_EQ(m100->reasons[0].token, ReasonToken::TorqueContinuousInsufficient);
    EXPECT_DOUBLE_EQ(m100->reasons[0].actual, 5.2);
    EXPECT_DOUBLE_EQ(m100->reasons[0].required, 4.5);
    // M-200（额定 9 N·m）：黄金点全过——可行（M-200 无电压等缺失影响：
    // 缺失字段仅在对应条件启用时才产生缺口——本用例条件全关）。
    EXPECT_EQ(m200->verdict, VerdictKind::Feasible);
}

/** Invalid 条目不参选（§7.1 ①——防御路径：直接构造快照验证跳过语义）。 */
TEST(SelScreeningMotor, InvalidEntryIsSkipped)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-03"}, std::vector<std::string>{"AT-08"});

    MotorCatalogEntry bad = makeGoldenMotor();
    bad.modelId = "M-BAD";
    bad.status = ValidationStatus::Invalid;
    Fixture fx = makeFixture();  // 非 const——快照被本用例替换为双候选（含 Invalid）
    fx.snapshot = makeSnapshot({bad, makeGoldenMotor()});
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenMotors(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);  // 仅 M-GOLD 产出记录
    EXPECT_EQ(recs[0].candidateModelId, "M-GOLD");
}

// =====================================================================
// 调用方契约违约（fail-fast——§10.2 短路边界只允许致命输入错误）
// =====================================================================

/** 安全系数＜1：调用方契约违约——std::invalid_argument（不静默按 1 处理）。 */
TEST(SelScreeningMotor, SafetyFactorBelowOneFailsFast)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-03"}, std::vector<std::string>{"NFR-COR-03"});

    Fixture fx = makeFixture();
    fx.criteria.safetyFactor = 0.5;
    HardConstraintSelector selector;
    EXPECT_THROW((void)selector.screenMotors(fx.snapshot, {fx.facts}, fx.criteria, nullptr),
                 std::invalid_argument);
}

/** 工作点非有限（NaN）：fail-fast（NFR-COR-03——不静默通过）。 */
TEST(SelScreeningMotor, NonFiniteWorkpointFailsFast)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-03"}, std::vector<std::string>{"NFR-COR-03"});

    Fixture fx = makeFixture();
    fx.facts.motorTorqueRms = std::numeric_limits<double>::quiet_NaN();
    HardConstraintSelector selector;
    EXPECT_THROW((void)selector.screenMotors(fx.snapshot, {fx.facts}, fx.criteria, nullptr),
                 std::invalid_argument);
}

/** 最低效率要求域外（1.5 ∉ (0,1]）：fail-fast（调用方条件构造错误）。 */
TEST(SelScreeningMotor, MinEfficiencyOutOfRangeFailsFast)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-03"}, std::vector<std::string>{"NFR-COR-03"});

    Fixture fx = makeFixture();
    fx.criteria.minEfficiency = 1.5;
    HardConstraintSelector selector;
    EXPECT_THROW((void)selector.screenMotors(fx.snapshot, {fx.facts}, fx.criteria, nullptr),
                 std::invalid_argument);
}
