/**
 * @file   ScreeningGearboxTest.cpp
 * @brief  减速器硬筛选黄金表用例组（SelScreeningGearbox）——SEL-04 八维
 *         ＋安装方向（额定/峰值输出转矩、允许输入转速、速比、效率、回程
 *         间隙、寿命、安装方向、允许外载荷）＋ω_m 换算口径＋外载荷力臂
 *         核算黄金核算＋多工况合并与确定性。
 *
 * 设计依据：
 *   - units/selection.md §8.1/§8.2（减速器硬筛选流程与纪律——安装方向/
 *     外载荷独立淘汰原因；ω_m＝ω_j/c 是唯一允许的自算映射量；映射事实
 *     优先）、§10.1/§10.3/§10.4（记录/词表/稳定排序）、§14.4（契约）、
 *     §15.2 V3 组（减速器硬筛选 14 项故障注入——各维边界、接口不兼容、
 *     缺曲线、数据不足、原因完整）
 *   - 需求 SEL-04（减速器筛选全维）、SEL-06/ERR-01（每淘汰项含实际值
 *     和阈值）、AT-08、NFR-COR-01/02（黄金对照/确定性）
 *   - 任务契约 tasks/foundation/WP-19-T04.json acceptance 1/2
 *
 * 黄金值口径：与 ScreeningMotorTest 同（字面黄金值＋core C7 容差吸收
 * 浮点噪声；力臂核算 F_allow＝F_rated×L_rated/L_actual 用解析可整除
 * 黄金值——1200×0.08/0.16＝600 N 精确）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/selection/CatalogProvider.hpp>
#include <sdurws/ird/selection/CatalogTypes.hpp>
#include <sdurws/ird/selection/Screening.hpp>

#include "CatalogTestSupport.hpp"  // testsupport 基线包构造（全链路用例——T03 交接面）

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace sdurws::ird::selection;
using namespace sdurws::ird;  // 使限定符 core:: 可见（ObjectId/ContentIdentity 直用）

namespace {

// =====================================================================
// 黄金数据（黄金表——集中定义）
// =====================================================================

/// 黄金减速器 G-GOLD：全字段齐备的基准型号（黄金值使默认工作点全维通过）。
GearboxCatalogEntry makeGoldenGearbox()
{
    GearboxCatalogEntry g;
    g.modelId = "G-GOLD";
    g.vendor = "Golden";
    g.displayName = "黄金基准减速器";
    g.catalog = CatalogIdentity{"cat-gold", "v1", core::ContentIdentity{}, "黄金表测试目录"};
    g.ratedOutputTorque = 50.0;    // 额定输出转矩，N·m（输出轴系）
    g.peakOutputTorque = 100.0;    // 峰值输出转矩，N·m
    g.maxInputSpeed = 300.0;       // 允许输入转速，rad/s
    g.ratio = 100.0;               // 速比（无量纲）
    g.efficiency = 0.95;           // 效率（无量纲 (0,1]）
    g.backlash = 0.0005;           // 回程间隙，rad（v1 冻结 SI 口径）
    g.ratedLife = 6000.0;          // 额定寿命（循环数——v1 口径）
    g.mountingOrientation = "any"; // 安装方向词表值
    g.extLoad = ExternalLoadSpec{1200.0, 600.0, 0.08};  // 径向 1200 N @ 0.08 m／轴向 600 N
    g.mass = 3.2;                  // 质量，kg（筛选不消费——字段完备性）
    g.mounting = MountSpec{"flangeA", "shaftB"};
    g.status = ValidationStatus::Valid;
    return g;
}

/// 黄金工作点（关节侧全供给；ω_m=ω_j/c=2.0/100=0.02 rad/s——黄金换算）。
AxisWorkpointFacts makeGoldenFacts(const core::ObjectId& axis)
{
    AxisWorkpointFacts f;
    f.jointId = axis;
    f.caseId = "case-1";
    // 关节侧（dynamics 上游黄金值）。
    f.jointTorqueRms = 30.0;    // N·m
    f.jointTorquePeak = 70.0;   // N·m
    f.jointSpeedPeak = 2.0;     // rad/s
    f.atTime = 2.5;             // s
    f.segmentId = "seg-B";
    return f;
}

/// 黄金筛选条件（全条件缺省＝条件驱动维度全部不适用）。
ScreeningCriteria makeGoldenCriteria()
{
    ScreeningCriteria c;
    c.safetyFactor = 1.0;
    return c;
}

CatalogPackageSnapshot makeSnapshot(std::vector<GearboxCatalogEntry> gearboxes)
{
    CatalogPackageSnapshot s;
    s.manifest.formatVersion = kCatalogFormatVersion;
    s.manifest.identity.catalogId = "cat-gold";
    s.manifest.identity.version = "v1";
    s.manifest.identity.source = "黄金表测试目录";
    s.gearboxes = std::move(gearboxes);
    return s;
}

/// 按词表 token 查找原因（首条）。
std::optional<RejectionReason> findReason(const FeasibilityRecord& rec, ReasonToken token)
{
    for (const RejectionReason& r : rec.reasons) {
        if (r.token == token) {
            return r;
        }
    }
    return std::nullopt;
}

/// 记录级黄金断言（verdict＋token 序＋缺口数）。
void expectRecordShape(const FeasibilityRecord& rec, VerdictKind verdict,
                       const std::vector<ReasonToken>& expectTokens,
                       std::size_t expectGaps, const std::string& modelId)
{
    EXPECT_EQ(rec.candidateModelId, modelId);
    EXPECT_EQ(rec.deviceKind, DeviceKind::Gearbox);
    EXPECT_EQ(rec.verdict, verdict);
    ASSERT_EQ(rec.reasons.size(), expectTokens.size())
        << "原因条数不符（modelId=" << modelId << "）";
    for (std::size_t i = 0; i < expectTokens.size(); ++i) {
        EXPECT_EQ(rec.reasons[i].token, expectTokens[i]);
    }
    EXPECT_EQ(rec.gaps.size(), expectGaps);
}

struct Fixture {
    CatalogPackageSnapshot snapshot;
    AxisWorkpointFacts facts;
    ScreeningCriteria criteria;
    core::ObjectId axis = core::ObjectId::generate();
};

Fixture makeFixture()
{
    Fixture fx;
    fx.snapshot = makeSnapshot({makeGoldenGearbox()});
    fx.facts = makeGoldenFacts(fx.axis);
    fx.criteria = makeGoldenCriteria();
    return fx;
}

}  // namespace

// =====================================================================
// 黄金行 0：可行基准
// =====================================================================

/** 黄金基准：黄金工作点全维位于 G-GOLD 能力域内——Feasible 零原因零缺口。 */
TEST(SelScreeningGearbox, GoldenFeasibleBaseline)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-04"}, std::vector<std::string>{"AT-08"});

    const Fixture fx = makeFixture();
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenGearboxes(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    expectRecordShape(recs[0], VerdictKind::Feasible, {}, 0U, "G-GOLD");
    EXPECT_EQ(recs[0].id, "G-GOLD|" + fx.axis.toCanonical());
    EXPECT_FALSE(recs[0].inputSliceId.isValid());  // 直调路径全零（诚实标记）
    EXPECT_FALSE(recs[0].mappingId.isValid());
}

// =====================================================================
// 黄金行 1：额定／峰值输出转矩（§8.1 ④——关节侧事实直判）
// =====================================================================

/** 额定输出转矩不足：关节 τ_rms=55 ＞ 50——黄金对照 actual/required。 */
TEST(SelScreeningGearbox, RatedTorqueInsufficientGoldenRow)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-04", "SEL-06"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    fx.facts.jointTorqueRms = 55.0;  // N·m
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenGearboxes(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    expectRecordShape(recs[0], VerdictKind::Rejected,
                      {ReasonToken::GearboxRatedTorqueInsufficient}, 0U, "G-GOLD");
    const std::optional<RejectionReason> r
        = findReason(recs[0], ReasonToken::GearboxRatedTorqueInsufficient);
    ASSERT_TRUE(r.has_value());
    EXPECT_DOUBLE_EQ(r->actual, 55.0);
    EXPECT_DOUBLE_EQ(r->required, 50.0);
    EXPECT_EQ(r->unit, "N*m");
    EXPECT_NE(r->thresholdSource.find("rated_output_torque_nm"), std::string::npos);
    EXPECT_EQ(r->caseId, "case-1");
    EXPECT_DOUBLE_EQ(r->atTime, 2.5);
}

/** 峰值输出转矩不足：τ_peak=105 ＞ 100——gearbox-peak-torque-insufficient。 */
TEST(SelScreeningGearbox, PeakTorqueInsufficientGoldenRow)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-04"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    fx.facts.jointTorquePeak = 105.0;  // N·m
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenGearboxes(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    expectRecordShape(recs[0], VerdictKind::Rejected,
                      {ReasonToken::GearboxPeakTorqueInsufficient}, 0U, "G-GOLD");
    const std::optional<RejectionReason> r
        = findReason(recs[0], ReasonToken::GearboxPeakTorqueInsufficient);
    ASSERT_TRUE(r.has_value());
    EXPECT_DOUBLE_EQ(r->actual, 105.0);
    EXPECT_DOUBLE_EQ(r->required, 100.0);
    EXPECT_EQ(r->unit, "N*m");
}

// =====================================================================
// 黄金行 2：允许输入转速（§8.2——映射事实优先，否则 ω_m＝ω_j/c 换算）
// =====================================================================

/**
 * 换算口径超限：ω_joint_peak=40000 rad/s → ω_m＝40000/100＝400 ＞
 * 允许 300——actual＝换算黄金值 400，来源标注候选传动参数换算。
 */
TEST(SelScreeningGearbox, InputSpeedExceededViaRatioConversion)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-04", "SEL-06"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    fx.facts.jointSpeedPeak = 40000.0;  // rad/s（关节侧）→ ω_m=400
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenGearboxes(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    expectRecordShape(recs[0], VerdictKind::Rejected, {ReasonToken::InputSpeedExceeded}, 0U,
                      "G-GOLD");
    const std::optional<RejectionReason> r = findReason(recs[0], ReasonToken::InputSpeedExceeded);
    ASSERT_TRUE(r.has_value());
    EXPECT_DOUBLE_EQ(r->actual, 400.0);   // 40000/100（黄金解析值）
    EXPECT_DOUBLE_EQ(r->required, 300.0);
    EXPECT_EQ(r->unit, "rad/s");
    EXPECT_NE(r->thresholdSource.find("max_input_speed"), std::string::npos);
}

/**
 * 映射事实优先：motorSpeedPeak=350（映射工作点）供给时不自算换算——
 * actual＝映射值 350（§8.2"ω_m 来自映射工作点"），来源标注映射事实。
 */
TEST(SelScreeningGearbox, InputSpeedPrefersMappingFactOverConversion)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-04"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    fx.facts.jointSpeedPeak = 2.0;     // rad/s（若误用换算得 0.02——不超限）
    fx.facts.motorSpeedPeak = 350.0;   // rad/s（映射事实——超限真值）
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenGearboxes(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    expectRecordShape(recs[0], VerdictKind::Rejected, {ReasonToken::InputSpeedExceeded}, 0U,
                      "G-GOLD");
    const std::optional<RejectionReason> r = findReason(recs[0], ReasonToken::InputSpeedExceeded);
    ASSERT_TRUE(r.has_value());
    EXPECT_DOUBLE_EQ(r->actual, 350.0);  // 映射事实值（非 0.02 换算值）
    EXPECT_NE(r->thresholdSource.find("motorSpeedPeak"), std::string::npos);
}

/** 映射事实与关节侧量均缺失——数据缺口（不猜测 ω_m）。 */
TEST(SelScreeningGearbox, InputSpeedMissingFactsIsDataGap)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-04"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    fx.facts.jointSpeedPeak = std::nullopt;
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenGearboxes(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    expectRecordShape(recs[0], VerdictKind::DataInsufficient, {}, 1U, "G-GOLD");
    EXPECT_EQ(recs[0].gaps[0].dimension, "gb-input-speed");
}

// =====================================================================
// 黄金行 3：速比（§8.1 ③——该轴允许传动比范围，闭区间）
// =====================================================================

/** 范围下界外：ratio=40 ＜ [50,150]——required＝下界 50。 */
TEST(SelScreeningGearbox, RatioBelowRangeGoldenRow)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-04"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    fx.snapshot.gearboxes[0].ratio = 40.0;
    fx.criteria.ratioRange = RatioRange{50.0, 150.0};
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenGearboxes(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    expectRecordShape(recs[0], VerdictKind::Rejected, {ReasonToken::RatioMismatch}, 0U,
                      "G-GOLD");
    const std::optional<RejectionReason> r = findReason(recs[0], ReasonToken::RatioMismatch);
    ASSERT_TRUE(r.has_value());
    EXPECT_DOUBLE_EQ(r->actual, 40.0);
    EXPECT_DOUBLE_EQ(r->required, 50.0);  // 越界侧边界（下界）
    EXPECT_EQ(r->unit, "1");              // 无量纲
    EXPECT_NE(r->thresholdSource.find("ratio_range"), std::string::npos);
}

/** 范围上界外：ratio=200 ＞ 150——required＝上界 150。 */
TEST(SelScreeningGearbox, RatioAboveRangeGoldenRow)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-04"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    fx.snapshot.gearboxes[0].ratio = 200.0;
    fx.criteria.ratioRange = RatioRange{50.0, 150.0};
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenGearboxes(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    expectRecordShape(recs[0], VerdictKind::Rejected, {ReasonToken::RatioMismatch}, 0U,
                      "G-GOLD");
    EXPECT_DOUBLE_EQ(findReason(recs[0], ReasonToken::RatioMismatch)->required, 150.0);
}

/** 边界内（含端点）：ratio=50 恰为下界——闭区间含端点，通过。 */
TEST(SelScreeningGearbox, RatioAtRangeBoundaryPasses)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-04"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    fx.snapshot.gearboxes[0].ratio = 50.0;  // ＝下界（闭区间）
    fx.criteria.ratioRange = RatioRange{50.0, 150.0};
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenGearboxes(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    expectRecordShape(recs[0], VerdictKind::Feasible, {}, 0U, "G-GOLD");
}

/** 条件未配置速比范围——维度不适用（无原因无缺口；落位细化 ⑧分界）。 */
TEST(SelScreeningGearbox, RatioDimensionNotApplicableWithoutCriterion)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-04"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    fx.snapshot.gearboxes[0].ratio = 40.0;  // 荒谬速比——但条件未配置不判
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenGearboxes(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    expectRecordShape(recs[0], VerdictKind::Feasible, {}, 0U, "G-GOLD");
}

// =====================================================================
// 黄金行 4：效率／回程间隙／寿命（条件驱动＋目录缺失缺口）
// =====================================================================

/** 效率不足：要求 0.96 ＞ 目录 0.95——efficiency-insufficient（无量纲 "1"）。 */
TEST(SelScreeningGearbox, EfficiencyInsufficientGoldenRow)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-04"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    fx.criteria.minEfficiency = 0.96;
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenGearboxes(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    expectRecordShape(recs[0], VerdictKind::Rejected, {ReasonToken::EfficiencyInsufficient},
                      0U, "G-GOLD");
    const std::optional<RejectionReason> r
        = findReason(recs[0], ReasonToken::EfficiencyInsufficient);
    ASSERT_TRUE(r.has_value());
    EXPECT_DOUBLE_EQ(r->actual, 0.95);
    EXPECT_DOUBLE_EQ(r->required, 0.96);
    EXPECT_EQ(r->unit, "1");
    EXPECT_NE(r->thresholdSource.find("min_efficiency"), std::string::npos);
}

/** 回程间隙超限：上限 0.0004 ＜ 目录 0.0005 rad——backlash-exceeded。 */
TEST(SelScreeningGearbox, BacklashExceededGoldenRow)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-04"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    fx.criteria.maxBacklash = 0.0004;  // rad（SI 域比较——v1 冻结口径）
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenGearboxes(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    expectRecordShape(recs[0], VerdictKind::Rejected, {ReasonToken::BacklashExceeded}, 0U,
                      "G-GOLD");
    const std::optional<RejectionReason> r = findReason(recs[0], ReasonToken::BacklashExceeded);
    ASSERT_TRUE(r.has_value());
    EXPECT_DOUBLE_EQ(r->actual, 0.0005);
    EXPECT_DOUBLE_EQ(r->required, 0.0004);
    EXPECT_EQ(r->unit, "rad");
    EXPECT_NE(r->thresholdSource.find("backlash"), std::string::npos);
}

/** 条件要回隙而目录未声明——数据缺口（不默认满足）。 */
TEST(SelScreeningGearbox, BacklashMissingInCatalogIsDataGap)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-04"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    fx.snapshot.gearboxes[0].backlash = std::nullopt;
    fx.criteria.maxBacklash = 0.001;  // rad
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenGearboxes(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    expectRecordShape(recs[0], VerdictKind::DataInsufficient, {}, 1U, "G-GOLD");
    EXPECT_EQ(recs[0].gaps[0].dimension, "backlash");
}

/** 寿命不足：要求 8000 ＞ 目录 6000——life-insufficient（循环数口径）。 */
TEST(SelScreeningGearbox, LifeInsufficientGoldenRow)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-04"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    fx.criteria.requiredLife = 8000.0;
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenGearboxes(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    expectRecordShape(recs[0], VerdictKind::Rejected, {ReasonToken::LifeInsufficient}, 0U,
                      "G-GOLD");
    const std::optional<RejectionReason> r = findReason(recs[0], ReasonToken::LifeInsufficient);
    ASSERT_TRUE(r.has_value());
    EXPECT_DOUBLE_EQ(r->actual, 6000.0);
    EXPECT_DOUBLE_EQ(r->required, 8000.0);
    EXPECT_EQ(r->unit, "1");
    EXPECT_NE(r->thresholdSource.find("rated_life"), std::string::npos);
}

/** 条件要寿命而目录未声明——数据缺口。 */
TEST(SelScreeningGearbox, LifeMissingInCatalogIsDataGap)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-04"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    fx.snapshot.gearboxes[0].ratedLife = std::nullopt;
    fx.criteria.requiredLife = 1000.0;
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenGearboxes(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    expectRecordShape(recs[0], VerdictKind::DataInsufficient, {}, 1U, "G-GOLD");
    EXPECT_EQ(recs[0].gaps[0].dimension, "life");
}

// =====================================================================
// 黄金行 5：安装接口／安装方向（§8.1 ②——减速器特有 orientation 子项）
// =====================================================================

/** 安装方向不匹配：要求 "floor" vs 目录 "any"——mounting-incompatible。 */
TEST(SelScreeningGearbox, OrientationMismatchGoldenRow)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-04"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    JointMountRequirement req;
    req.orientation = "floor";
    fx.facts.mountRequirement = req;
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenGearboxes(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    expectRecordShape(recs[0], VerdictKind::Rejected, {ReasonToken::MountingIncompatible},
                      0U, "G-GOLD");
    const std::optional<RejectionReason> r
        = findReason(recs[0], ReasonToken::MountingIncompatible);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->actualText, "any");
    EXPECT_EQ(r->requiredText, "floor");
    EXPECT_EQ(r->unit, "");
    EXPECT_NE(r->thresholdSource.find("mounting_orientation"), std::string::npos);
}

/** 法兰＋方向双失败——两条同 token 原因并存（子项独立记录，不短路）。 */
TEST(SelScreeningGearbox, FlangeAndOrientationBothRecorded)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-04"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    JointMountRequirement req;
    req.flangeKind = "flangeX";
    req.orientation = "floor";
    fx.facts.mountRequirement = req;
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenGearboxes(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    expectRecordShape(recs[0], VerdictKind::Rejected,
                      {ReasonToken::MountingIncompatible, ReasonToken::MountingIncompatible},
                      0U, "G-GOLD");
    // 同 token 双条：法兰条在前（执行序），方向条在后。
    EXPECT_EQ(recs[0].reasons[0].actualText, "flangeA");
    EXPECT_EQ(recs[0].reasons[1].actualText, "any");
}

// =====================================================================
// 黄金行 6：允许外载荷（轴向直比＋径向力臂核算——落位细化 ⑥）
// =====================================================================

/** 轴向超限：实际 700 N ＞ 允许 600 N——external-load-exceeded。 */
TEST(SelScreeningGearbox, AxialLoadExceededGoldenRow)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-04", "SEL-06"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    ExternalLoadFacts load;
    load.radial = 500.0;    // N（≤1200——径向维通过）
    load.axial = 700.0;     // N（＞600）
    load.distance = 0.05;   // m（不更远——径向不折减）
    fx.facts.externalLoad = load;
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenGearboxes(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    expectRecordShape(recs[0], VerdictKind::Rejected, {ReasonToken::ExternalLoadExceeded},
                      0U, "G-GOLD");
    const std::optional<RejectionReason> r
        = findReason(recs[0], ReasonToken::ExternalLoadExceeded);
    ASSERT_TRUE(r.has_value());
    EXPECT_DOUBLE_EQ(r->actual, 700.0);
    EXPECT_DOUBLE_EQ(r->required, 600.0);
    EXPECT_EQ(r->unit, "N");
    EXPECT_NE(r->thresholdSource.find("external_load_axial_n"), std::string::npos);
}

/**
 * 径向力臂核算（折减后超限）：实际 1300 N @ 0.16 m，目录 1200 N @ 0.08 m
 * ——作用点更远（0.16＞0.08），F_allow＝1200×0.08/0.16＝600 N（黄金
 * 解析值）——1300 ＞ 600 淘汰，required＝核算后 600。
 */
TEST(SelScreeningGearbox, RadialLoadLeverArmReductionExceeded)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-04", "SEL-06"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    ExternalLoadFacts load;
    load.radial = 1300.0;    // N
    load.axial = 100.0;      // N（≤600）
    load.distance = 0.16;    // m（2 倍力臂——允许力折半）
    fx.facts.externalLoad = load;
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenGearboxes(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    expectRecordShape(recs[0], VerdictKind::Rejected, {ReasonToken::ExternalLoadExceeded},
                      0U, "G-GOLD");
    const std::optional<RejectionReason> r
        = findReason(recs[0], ReasonToken::ExternalLoadExceeded);
    ASSERT_TRUE(r.has_value());
    EXPECT_DOUBLE_EQ(r->actual, 1300.0);
    EXPECT_DOUBLE_EQ(r->required, 600.0);  // 1200×0.08/0.16（黄金解析值）
    EXPECT_NE(r->thresholdSource.find("L_actual"), std::string::npos);
}

/**
 * 径向力臂核算（折减后通过）：实际 500 N @ 0.16 m——核算后允许 600 N
 * 仍满足（证明核算确实生效：若未折减 1200 也会通过，本行黄金值钉住
 * required 语义由核算给出——通过行无数值断言面，改用"折减后恰超"行
 * （上一用例）钉住公式；本行验证折减后可行路径）。
 */
TEST(SelScreeningGearbox, RadialLoadWithinReducedLimitPasses)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-04"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    ExternalLoadFacts load;
    load.radial = 500.0;    // N ＜ 核算后 600
    load.axial = 100.0;     // N
    load.distance = 0.16;   // m
    fx.facts.externalLoad = load;
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenGearboxes(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    expectRecordShape(recs[0], VerdictKind::Feasible, {}, 0U, "G-GOLD");
}

/** 作用点不更远（0.05 ≤ 0.08）——不折减：1100 N ＜ 1200 N 原值通过。 */
TEST(SelScreeningGearbox, RadialLoadCloserThanRatedNoReduction)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-04"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    ExternalLoadFacts load;
    load.radial = 1100.0;   // N（＞600 折减值、＜1200 原值——钉住"不折减"）
    load.axial = 100.0;
    load.distance = 0.05;   // m ≤ 0.08
    fx.facts.externalLoad = load;
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenGearboxes(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    expectRecordShape(recs[0], VerdictKind::Feasible, {}, 0U, "G-GOLD");
}

/** 轴侧声明外载荷而目录未声明允许值——数据缺口。 */
TEST(SelScreeningGearbox, ExternalLoadMissingInCatalogIsDataGap)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-04"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    fx.snapshot.gearboxes[0].extLoad = std::nullopt;
    ExternalLoadFacts load;
    load.radial = 100.0;
    load.axial = 50.0;
    load.distance = 0.05;
    fx.facts.externalLoad = load;
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenGearboxes(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    expectRecordShape(recs[0], VerdictKind::DataInsufficient, {}, 1U, "G-GOLD");
    EXPECT_EQ(recs[0].gaps[0].dimension, "external-load");
}

/** 双方均无外载荷声明——维度不适用（轴未声明≠零载荷，不互造缺口）。 */
TEST(SelScreeningGearbox, ExternalLoadNotApplicableWhenUndeclaredBothSides)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-04"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    fx.snapshot.gearboxes[0].extLoad = std::nullopt;  // fx.facts.externalLoad 本就 nullopt
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenGearboxes(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    expectRecordShape(recs[0], VerdictKind::Feasible, {}, 0U, "G-GOLD");
}

// =====================================================================
// 多维／多工况／确定性与全链路（§10.4/契约面）
// =====================================================================

/** 多维失败不短路（§10.2——候选能力筛选不短路）：转矩＋回隙＋寿命三因。 */
TEST(SelScreeningGearbox, MultiDimensionFailuresAllRecorded)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-04", "SEL-06"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    fx.facts.jointTorqueRms = 60.0;        // N·m ＞ 50（额定转矩维失败）
    fx.criteria.maxBacklash = 0.0001;      // rad ＜ 0.0005（回隙维失败）
    fx.criteria.requiredLife = 9000.0;     // ＞ 6000（寿命维失败）
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenGearboxes(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    // 词表序（枚举序＝§10.3 组内行序）：rated-torque(11) → backlash(16)
    // → life(17)。
    expectRecordShape(recs[0], VerdictKind::Rejected,
                      {ReasonToken::GearboxRatedTorqueInsufficient, ReasonToken::BacklashExceeded,
                       ReasonToken::LifeInsufficient},
                      0U, "G-GOLD");
}

/** 同轴多工况合并：case-1 转矩失败＋case-2 输入转速失败——单记录双因。 */
TEST(SelScreeningGearbox, MultiCaseMergedIntoSingleRecord)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-04"}, std::vector<std::string>{"AT-08"});

    Fixture fx = makeFixture();
    AxisWorkpointFacts f1 = fx.facts;
    f1.caseId = "case-rated";
    f1.jointTorqueRms = 60.0;      // N·m
    AxisWorkpointFacts f2 = fx.facts;
    f2.caseId = "case-speed";
    f2.jointSpeedPeak = 40000.0;   // rad/s → ω_m=400 ＞ 300
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenGearboxes(fx.snapshot, {f1, f2}, fx.criteria, nullptr);
    ASSERT_EQ(recs.size(), 1U);
    expectRecordShape(recs[0], VerdictKind::Rejected,
                      {ReasonToken::GearboxRatedTorqueInsufficient, ReasonToken::InputSpeedExceeded},
                      0U, "G-GOLD");
    EXPECT_EQ(recs[0].reasons[0].caseId, "case-rated");
    EXPECT_EQ(recs[0].reasons[1].caseId, "case-speed");
}

/** 确定性（NFR-COR-02）：同输入两次调用逐字段全等。 */
TEST(SelScreeningGearbox, DeterministicSameInputSameOutput)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-04"}, std::vector<std::string>{"NFR-COR-02"});

    Fixture fx = makeFixture();
    fx.criteria.minEfficiency = 0.96;
    fx.criteria.maxBacklash = 0.0001;
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> a
        = selector.screenGearboxes(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    const std::vector<FeasibilityRecord> b
        = selector.screenGearboxes(fx.snapshot, {fx.facts}, fx.criteria, nullptr);
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        EXPECT_TRUE(a[i] == b[i]);
    }
}

/** 条件全缺省＋可选字段全缺的条目——全维不适用/通过（基线 G-50 形态）。 */
TEST(SelScreeningGearbox, AllConditionsOffWithSparseEntryFeasible)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-04"}, std::vector<std::string>{"AT-08"});

    GearboxCatalogEntry sparse = makeGoldenGearbox();
    sparse.modelId = "G-SPARSE";
    sparse.backlash = std::nullopt;
    sparse.ratedLife = std::nullopt;
    sparse.extLoad = std::nullopt;
    CatalogPackageSnapshot snapshot = makeSnapshot({sparse});
    core::ObjectId axis = core::ObjectId::generate();
    AxisWorkpointFacts f = makeGoldenFacts(axis);
    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenGearboxes(snapshot, {f}, makeGoldenCriteria(), nullptr);
    ASSERT_EQ(recs.size(), 1U);
    expectRecordShape(recs[0], VerdictKind::Feasible, {}, 0U, "G-SPARSE");
}

/** 全链路（导入→装配→筛选）：基线包 G-50/G-120 黄金对照（T03 交接面）。 */
TEST(SelScreeningGearbox, EndToEndFromCatalogImportToScreening)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-04"}, std::vector<std::string>{"AT-08"});

    const CatalogImporter importer;
    const CatalogValidationReport rep
        = importer.validate(testsupport::makeBaselineInput(), testsupport::makeBaselineManifest());
    ASSERT_TRUE(rep.ok());
    const CatalogPackageSnapshot snapshot
        = importer.assemble(testsupport::makeBaselineInput(), testsupport::makeBaselineManifest());
    ASSERT_EQ(snapshot.gearboxes.size(), 2U);  // G-50 ＋ G-120

    // 黄金工作点：关节 τ_rms=60 N·m（G-50 额定 50 不足、G-120 额定 120 足）；
    // 条件：回隙上限 0.5 rad（G-120 backlash=0.8 rad 超限、G-50 未声明→缺口）。
    core::ObjectId axis = core::ObjectId::generate();
    AxisWorkpointFacts f;
    f.jointId = axis;
    f.caseId = "case-load";
    f.jointTorqueRms = 60.0;    // N·m
    f.jointTorquePeak = 90.0;   // N·m（G-50 峰值 100 内、G-120 峰值 240 内）
    f.jointSpeedPeak = 250.0;   // rad/s → G-50 ω_m=250/50=5 ≤ 300 ✓；G-120 ω_m=2.5 ≤ 300 ✓
    ScreeningCriteria criteria;
    criteria.safetyFactor = 1.0;
    criteria.maxBacklash = 0.5;  // rad（黄金上限）

    HardConstraintSelector selector;
    const std::vector<FeasibilityRecord> recs
        = selector.screenGearboxes(snapshot, {f}, criteria, nullptr);
    ASSERT_EQ(recs.size(), 2U);
    const FeasibilityRecord* g50 = nullptr;
    const FeasibilityRecord* g120 = nullptr;
    for (const FeasibilityRecord& r : recs) {
        if (r.candidateModelId == "G-50") { g50 = &r; }
        if (r.candidateModelId == "G-120") { g120 = &r; }
    }
    ASSERT_NE(g50, nullptr);
    ASSERT_NE(g120, nullptr);
    // G-50：额定转矩 60＞50 淘汰＋回隙未声明缺口——双轨并存（淘汰优先定级）。
    EXPECT_EQ(g50->verdict, VerdictKind::Rejected);
    ASSERT_GE(g50->reasons.size(), 1U);
    EXPECT_EQ(g50->reasons[0].token, ReasonToken::GearboxRatedTorqueInsufficient);
    EXPECT_DOUBLE_EQ(g50->reasons[0].actual, 60.0);
    EXPECT_DOUBLE_EQ(g50->reasons[0].required, 50.0);
    ASSERT_EQ(g50->gaps.size(), 1U);
    EXPECT_EQ(g50->gaps[0].dimension, "backlash");
    // G-120：转矩通过、回隙 0.8＞0.5 淘汰（黄金值对照）。
    EXPECT_EQ(g120->verdict, VerdictKind::Rejected);
    ASSERT_EQ(g120->reasons.size(), 1U);
    EXPECT_EQ(g120->reasons[0].token, ReasonToken::BacklashExceeded);
    EXPECT_DOUBLE_EQ(g120->reasons[0].actual, 0.8);
    EXPECT_DOUBLE_EQ(g120->reasons[0].required, 0.5);
}

/** 调用方契约违约：速比范围上下倒置——fail-fast（§10.2 校验边界）。 */
TEST(SelScreeningGearbox, InvertedRatioRangeFailsFast)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-04"}, std::vector<std::string>{"NFR-COR-03"});

    Fixture fx = makeFixture();
    fx.criteria.ratioRange = RatioRange{150.0, 50.0};  // 倒置
    HardConstraintSelector selector;
    EXPECT_THROW((void)selector.screenGearboxes(fx.snapshot, {fx.facts}, fx.criteria, nullptr),
                 std::invalid_argument);
}

/** 关节侧工作点非有限（±Inf）——fail-fast（NFR-COR-03）。 */
TEST(SelScreeningGearbox, NonFiniteJointWorkpointFailsFast)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-04"}, std::vector<std::string>{"NFR-COR-03"});

    Fixture fx = makeFixture();
    fx.facts.jointTorqueRms = std::numeric_limits<double>::infinity();
    HardConstraintSelector selector;
    EXPECT_THROW((void)selector.screenGearboxes(fx.snapshot, {fx.facts}, fx.criteria, nullptr),
                 std::invalid_argument);
}
