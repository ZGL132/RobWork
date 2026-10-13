/**
 * @file   ReproBenchmarkTest.cpp
 * @brief  selection 域确定性复现基准用例（WP-23-T09 抽样——B-08）：电机硬
 *         筛选（HardConstraintSelector::screenMotors——SEL-03）同配置复跑
 *         的等价集合＋稳定排序（候选序×轴序遍历＋原因词表稳定序——§10.4；
 *         NFR-COR-02）。
 *
 * 设计依据：
 *   - units/selection.md §7.1（电机硬筛选——逐候选×逐轴确定遍历序）＋
 *     §10.3/§10.4（原因封闭词表与稳定排序：token 词表序→工况 ID→时刻）
 *   - doc/industrial-robot-design/performance-baseline.md §2 B-08 行＋§4.6
 *     （不要求浮点逐字节相同——对照容差经 repro-reduction 档案）
 *   - 需求 NFR-COR-02；任务契约 tasks/foundation/WP-23-T09.json
 *
 * 抽样说明（各域抽样复现——本域取样点）：电机硬筛选是本域候选资格判定的
 * 核心通道（记录键＝候选×轴；原因含实际值/阈值 SI 数值——集合对照的数值
 * 面）。本域无并行分片与随机种子入口——线程等价腿不适用，如实只跑复跑腿。
 *
 * 数值面口径（repro.value 无量纲键）：原因记录的 actual/required 为 SI 数值
 * （转矩 N·m/转速 rad/s 等——量纲混合，B-08 对照关注"同输入同值"的确定性
 * 而非量纲换算，逐项以无量纲键入档案——量纲换算对照归各域黄金档案）。
 */

#include <sdurws/ird/selection/CatalogTypes.hpp>
#include <sdurws/ird/selection/Screening.hpp>
#include <sdurws/ird/testkit/ReproBenchmark.hpp>
#include <sdurws/ird/testkit/ToleranceProfile.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

namespace sel = sdurws::ird::selection;
namespace tk = sdurws::ird::testkit;
using namespace sdurws::ird;   // core::ObjectId 等限定符直用（ScreeningMotorTest 同款）

// =====================================================================
// 元素投影（FeasibilityRecord → SetMatchTraits 元素——§5.4.5 适配归域）。
// 元素与 traits 特化置于全局作用域（SetCheckTest 消费示范同款——特化须
// 在主模板的可达外围命名空间声明）。
// =====================================================================

/// 记录元素：身份＝记录键（"<modelId>|<axisId>"——T04 候选×轴资格键）；
/// 数值面＝逐条淘汰原因的 (actual, required, atTime) 三元组（词表稳定序
/// 展开——原因序变化即数值序列变化＝集合违例；verdict/gaps 为枚举/文本，
/// 以空数值面身份配对承载——verdict 差异经由 reasons 差异显影）。
struct ReproScreenItem {
    std::string id;                     ///< 记录键（批内唯一）
    std::vector<double> reasonNumbers;  ///< 逐原因 (actual, required, atTime) 展平
};

namespace sdurws::ird::testkit {
template <>
struct SetMatchTraits<ReproScreenItem> {
    static std::optional<std::string> identity(const ReproScreenItem& e)
    {
        return e.id;
    }
    static std::vector<NumericFieldView> numerics(const ReproScreenItem& e)
    {
        std::vector<NumericFieldView> out;
        out.reserve(e.reasonNumbers.size());
        for (const double v : e.reasonNumbers) {
            out.push_back({"repro.value", v});
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

/// 电机候选构造（能力域按序递变——覆盖可行/多项淘汰/边界三类判定面）。
sel::MotorCatalogEntry makeMotor(const std::string& id, double ratedTorque,
                                 double peakTorque, double ratedPower)
{
    sel::MotorCatalogEntry m;
    m.modelId = id;
    m.vendor = "Repro";
    m.displayName = id;
    m.catalog = sel::CatalogIdentity{"cat-repro", "v1", core::ContentIdentity{},
                                     "复现基准测试目录"};
    m.ratedTorque = ratedTorque;    // N·m
    m.peakTorque = peakTorque;      // N·m
    m.ratedSpeed = 150.0;           // rad/s
    m.maxSpeed = 300.0;             // rad/s
    m.ratedPower = ratedPower;      // W
    m.overload = sel::OverloadSpec{12.0, 8.0};       // 12 N·m 允许 8 s
    m.dutyClass = "S1";             // 工作制词表值
    m.ratedVoltage = 220.0;         // V
    m.thermal = sel::ThermalDerating{25.0, 0.9};     // 档位 25/系数 0.9
    m.brakeTorque = 12.0;           // N·m
    m.holdingTorque = 15.0;         // N·m
    m.rotorInertia = 0.01;          // kg·m²（筛选不消费）
    m.mass = 6.0;                   // kg（同上）
    m.mounting = sel::MountSpec{"flangeA", "shaftB"};
    m.status = sel::ValidationStatus::Valid;
    return m;
}

/// 工作点事实（三轴能力需求递变——轴 2 触发转矩/转速双维淘汰）。
sel::AxisWorkpointFacts makeFacts(const core::ObjectId& axis, double torqueRms,
                                  double torquePeak, double speedPeak)
{
    sel::AxisWorkpointFacts f;
    f.jointId = axis;
    f.caseId = "case-repro";
    f.motorTorqueRms = torqueRms;     // N·m
    f.motorTorquePeak = torquePeak;   // N·m
    f.motorSpeedPeak = speedPeak;     // rad/s
    f.motorSpeedRms = speedPeak * 0.6; // rad/s
    f.motorPowerPeak = torquePeak * speedPeak;     // W（P=τ·ω——工作点事实）
    f.motorPowerRms = torqueRms * speedPeak * 0.6; // W
    f.peakDuration = 5.0;             // s（＜过载窗口 8 s）
    f.atTime = 1.5;                   // s
    f.segmentId = "seg-repro";
    return f;
}

/// 筛选记录投影（records 序＝候选序×轴序——确定性遍历序；原因序＝§10.4
/// 稳定排序——两者都是被对照的"排序"面）。
std::vector<ReproScreenItem> projectRecords(
    const std::vector<sel::FeasibilityRecord>& recs)
{
    std::vector<ReproScreenItem> out;
    out.reserve(recs.size());
    for (const sel::FeasibilityRecord& r : recs) {
        ReproScreenItem e;
        e.id = r.id;                  // "<modelId>|<jointId>"——记录键
        for (const sel::RejectionReason& reason : r.reasons) {
            e.reasonNumbers.push_back(reason.actual);
            e.reasonNumbers.push_back(reason.required);
            e.reasonNumbers.push_back(reason.atTime);
        }
        out.push_back(std::move(e));
    }
    return out;
}

}  // namespace

/**
 * selection 域复现基准（B-08 抽样）：6 电机×3 轴＝18 记录（能力域梯度
 * 覆盖可行/单维淘汰/多维淘汰）——同输入复跑等价集合＋稳定排序。
 */
TEST(SelReproBenchmark, MotorScreeningRerunEquivalentSetAndOrder_WP23T09_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-02"},
                  std::vector<std::string>{"AT-08"});

    // ---- 确定性输入装配（值构造快照——ScreeningMotorTest 同款形态）----
    sel::CatalogPackageSnapshot snapshot;
    snapshot.manifest.formatVersion = sel::kCatalogFormatVersion;
    snapshot.manifest.identity.catalogId = "cat-repro";
    snapshot.manifest.identity.version = "v1";
    snapshot.manifest.identity.source = "复现基准测试目录";
    snapshot.motors = {
        makeMotor("M-REPRO-01", 4.0, 10.0, 1500.0),   // 全维余量——可行
        makeMotor("M-REPRO-02", 3.2, 8.0, 1200.0),    // 边界域
        makeMotor("M-REPRO-03", 2.5, 6.0, 900.0),     // 转矩维淘汰面
        makeMotor("M-REPRO-04", 1.8, 4.5, 600.0),     // 多维淘汰面
        makeMotor("M-REPRO-05", 5.0, 12.5, 2000.0),   // 大裕量
        makeMotor("M-REPRO-06", 2.0, 5.0, 750.0),     // 中度淘汰面
    };
    core::ObjectId axisA = core::ObjectId::fromCanonical(
        "obj-000000000000000000000000000000a1");
    core::ObjectId axisB = core::ObjectId::fromCanonical(
        "obj-000000000000000000000000000000a2");
    core::ObjectId axisC = core::ObjectId::fromCanonical(
        "obj-000000000000000000000000000000a3");
    std::vector<sel::AxisWorkpointFacts> facts = {
        makeFacts(axisA, 3.0, 7.0, 200.0),    // 温和
        makeFacts(axisB, 3.8, 9.5, 240.0),    // 中等
        makeFacts(axisC, 4.9, 11.0, 280.0),   // 苛刻（触发多维淘汰）
    };
    sel::ScreeningCriteria criteria;
    criteria.safetyFactor = 1.0;    // 不加严（安全系数维不启用——确定性输入面）

    // ---- 复现用例执行（两腿：reference/rerun——线程腿不适用）----
    tk::ReproCaseSpec spec;
    spec.caseId = "sel-motor-screening-rerun";
    spec.domain = "selection";
    spec.inputProvenance =
        "catalogValueFixture(6 motors x 3 axes, gradient capability domain, "
        "criteria SF=1.0, fixed object ids)";
    spec.repro.seed = 20260909;      // 无随机消费域——记录默认口径
    spec.repro.threadCount = 1;
    spec.parallelThreadCount = 0;    // 逐候选×逐轴独立判定——线程腿不适用

    const sel::HardConstraintSelector selector;
    const tk::ReproCaseReport report = tk::runReproCase<ReproScreenItem>(
        spec, reproProfile(), tk::SetMatchTraits<ReproScreenItem>{},
        [&](std::uint32_t) {
            return projectRecords(selector.screenMotors(snapshot, facts,
                                                        criteria, nullptr));
        });

    // 判定（NFR-COR-02：等价集合＋稳定排序——本域抽样面；记录数＝候选×轴
    // ＝18 不变量）。
    ASSERT_EQ(report.verdicts.size(), 1U);
    EXPECT_TRUE(report.verdicts.front().setEquivalent);
    EXPECT_TRUE(report.verdicts.front().sameOrder);
    EXPECT_TRUE(report.passed) << (report.violations.empty()
        ? "" : report.violations.front());
    EXPECT_EQ(report.legs.front().elementCount, 18U);

    // 报告拖挂（IRD_REPRO_REPORT_DIR 设置时落盘——基准执行留痕）。
    if (const auto dir = tk::reproReportDirFromEnv()) {
        EXPECT_TRUE(tk::writeReproReportJson(*dir, report));
    }
}
