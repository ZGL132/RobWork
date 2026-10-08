/**
 * @file   AxisOutOfScopeTest.cpp
 * @brief  移动关节范围外诊断用例组（SelAxisOutOfScope——WP-19-T08）——
 *         SEL-09 R1 范围纪律：目标链含移动关节（prismatic）时该轴选型
 *         输出明确"范围外"诊断（SEL-INPUT-AXIS-OUT-OF-SCOPE，
 *         DataInsufficient 语义），不静默套用旋转传动；含移动关节链阻断；
 *         与单元卡 §2.1 支持矩阵一致（MDL-12-S1/SEL-09-S1 启用前不放开
 *         ——阻断是本任务交付语义，不是缺陷）。
 *
 * 设计依据：
 *   - units/selection.md §2.2（R1 目标链含移动关节的纪律——不得套用
 *     旋转传动/不得静默转换/不得伪造电机工作点/输出"范围外"诊断/
 *     DataInsufficient 语义/不升级整机不可行）、D-SEL-15（范围外＝
 *     DataInsufficient＋独立诊断）、§7.1/§8.1（旋转传动维度清单——
 *     移动关节轴全部不适用的反面面）、§10.3（axis-out-of-scope 词表
 *     token）、§16 WP-19-T08 行（含移动关节链阻断测试）
 *   - 需求 SEL-09（REQUIREMENTS：目标链含移动关节时该轴选型输出明确
 *     "范围外"诊断〔DataInsufficient〕，不得静默套用旋转传动）、
 *     MDL-12（R1 拒绝混合链选型——链型判定权威在 modeling，本组用例
 *     钉 selection 侧对 Prismatic 轴事实的阻断响应）
 *   - 任务契约 tasks/foundation/WP-19-T08.json acceptance 1/2/3
 *
 * 黄金值口径（附录 D 精神）：使用"超限工作点"作最强反证——若筛选器
 * 静默套用了旋转传动，超限工作点必然产生 SEL-MOTOR- 与 SEL-GEARBOX-
 * 前缀的淘汰原因；阻断语义下这些原因必须零出现（证明维度判定根本
 * 未执行），且记录不得判 Feasible（证明没有把范围外当通过）。
 *
 * ★ 接口消费纪律：全部用例经公共接口消费（HardConstraintSelector 的
 *   IHardConstraintSelector 契约面）——不留只测内部函数的盲区。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/selection/CatalogTypes.hpp>
#include <sdurws/ird/selection/DiagCodes.hpp>   // kSelInputAxisOutOfScope＋词表映射断言
#include <sdurws/ird/selection/Screening.hpp>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯

#include <stdexcept>
#include <string>
#include <vector>

using namespace sdurws::ird::selection;
using namespace sdurws::ird;  // 使限定符 core:: 可见

namespace {

// =====================================================================
// 本组黄金数据（自持——与 T04 黄金表同值口径，验证互不依赖）
// =====================================================================

/// 黄金电机：全字段齐备（若套用旋转传动则给定工作点全维超限——反证面）。
MotorCatalogEntry makeScopeMotor()
{
    MotorCatalogEntry m;
    m.modelId = "M-SCOPE";
    m.vendor = "Golden";
    m.displayName = "范围外测试电机";
    m.catalog = CatalogIdentity{"cat-scope", "v1", core::ContentIdentity{}, "范围外测试目录"};
    m.ratedTorque = 4.0;    // 额定连续转矩，N·m
    m.peakTorque = 10.0;    // 峰值转矩，N·m
    m.ratedSpeed = 150.0;   // 额定转速，rad/s
    m.maxSpeed = 300.0;     // 最高转速，rad/s
    m.ratedPower = 1500.0;  // 额定功率，W
    m.dutyClass = "S1";
    m.rotorInertia = 0.01;  // 转子惯量，kg·m²
    m.mass = 6.0;           // 质量，kg
    m.mounting = MountSpec{"flangeA", "shaftB"};
    m.status = ValidationStatus::Valid;
    return m;
}

/// 黄金减速器：若套用旋转传动则给定关节侧工作点超限（反证面）。
GearboxCatalogEntry makeScopeGearbox()
{
    GearboxCatalogEntry g;
    g.modelId = "G-SCOPE";
    g.vendor = "Golden";
    g.displayName = "范围外测试减速器";
    g.catalog = CatalogIdentity{"cat-scope", "v1", core::ContentIdentity{}, "范围外测试目录"};
    g.ratedOutputTorque = 200.0;  // 额定输出转矩，N·m（输出轴系）
    g.peakOutputTorque = 400.0;   // 峰值输出转矩，N·m
    g.maxInputSpeed = 300.0;      // 允许输入转速，rad/s
    g.ratio = 10.0;               // 速比 n:1（无量纲）
    g.efficiency = 0.9;           // 效率（无量纲）
    g.mass = 2.0;                 // 质量，kg
    g.mounting = MountSpec{"flangeA", "shaftB"};
    g.status = ValidationStatus::Valid;
    return g;
}

/// 移动关节轴的超限工作点（反证面：τ/ω 全部超过黄金能力——若套用旋转
/// 传动必产生淘汰原因；直线轴的旋转工作点物理不存在，本构造模拟"调用
/// 方误供了数值"的最坏情形，断言筛选器不消费这些数值）。
AxisWorkpointFacts makePrismaticOverloadFacts(const core::ObjectId& axis,
                                              const CaseId& caseId)
{
    AxisWorkpointFacts f;
    f.jointId = axis;
    f.caseId = caseId;
    f.jointKind = JointKind::Prismatic;  // 范围外语义的触发面（WP-19-T08）
    // 关节侧超限值（直线轴不存在这些量——构造故意超限以验证不消费）。
    f.jointTorqueRms = 1000.0;   // N·m（＞额定输出转矩 200）
    f.jointTorquePeak = 2000.0;  // N·m（＞峰值输出转矩 400）
    f.jointSpeedPeak = 999.0;    // rad/s（＞允许输入转速换算上限）
    // 电机侧超限值（同上——直线轴无旋转映射口径）。
    f.motorTorqueRms = 500.0;    // N·m（＞额定连续转矩 4）
    f.motorTorquePeak = 900.0;   // N·m（＞峰值转矩 10）
    f.motorSpeedPeak = 888.0;    // rad/s（＞最高转速 300）
    f.motorSpeedRms = 777.0;     // rad/s（＞额定转速 150）
    f.motorPowerPeak = 666.0;    // W（＞额定功率 1500？否——低于额定；
                                 //   转矩/转速已超限，套用即有多原因）
    f.motorPowerRms = 555.0;     // W
    f.atTime = 2.5;              // s
    f.segmentId = "seg-P";
    return f;
}

/// 旋转轴的工作点（正常旋转传动事实——混合链用例的旋转面）。
AxisWorkpointFacts makeRevoluteFacts(const core::ObjectId& axis,
                                     const CaseId& caseId, bool overload)
{
    AxisWorkpointFacts f;
    f.jointId = axis;
    f.caseId = caseId;
    f.jointKind = JointKind::Revolute;  // 显式声明旋转（零回归对照面）
    if (overload) {
        // 超限：电机侧峰值转矩 20 ＞ 峰值能力 10；关节侧峰值转矩 500 ＞
        // 峰值输出转矩 400——套用旋转传动时必产生双端淘汰原因。
        f.motorTorquePeak = 20.0;   // N·m
        f.jointTorquePeak = 500.0;  // N·m
    } else {
        // 可行：电机侧峰值转矩 3.5 ≤ 额定连续转矩 4（不触发过载时间窗
        // 核查——触发式维度见单元卡 §19.3 T04 细化 ⑧；本组黄金电机未
        // 声明 overload，τ_peak＞额定连续会转数据缺口）。
        f.motorTorquePeak = 3.5;    // N·m
        f.jointTorquePeak = 50.0;   // N·m
    }
    f.motorTorqueRms = 3.0;   // N·m
    f.jointTorqueRms = 30.0;  // N·m
    f.motorSpeedPeak = 100.0;  // rad/s
    f.motorSpeedRms = 80.0;    // rad/s
    f.jointSpeedPeak = 10.0;   // rad/s
    f.motorPowerPeak = 500.0;  // W
    f.motorPowerRms = 240.0;   // W
    f.atTime = 1.0;            // s
    f.segmentId = "seg-R";
    return f;
}

/// 筛选条件（默认全不启用——工作点驱动维度）。
ScreeningCriteria defaultCriteria()
{
    ScreeningCriteria c;
    c.safetyFactor = 1.0;  // 不加严
    return c;
}

/// 单候选快照构造。
CatalogPackageSnapshot makeSnapshot()
{
    CatalogPackageSnapshot s;
    s.manifest.formatVersion = kCatalogFormatVersion;
    s.manifest.identity.catalogId = "cat-scope";
    s.manifest.identity.version = "v1";
    s.manifest.identity.source = "范围外测试目录";
    s.motors = {makeScopeMotor()};
    s.gearboxes = {makeScopeGearbox()};
    return s;
}

/// 缺口定位查找（按 dimension 找范围外缺口——逐条断言统一入口）。
std::optional<DataGap> findOutOfScopeGap(const FeasibilityRecord& rec)
{
    for (const DataGap& g : rec.gaps) {
        if (g.dimension == "axis-out-of-scope") {
            return g;
        }
    }
    return std::nullopt;
}

}  // namespace

// ---------------------------------------------------------------------
// 筛选器阻断（screenMotors / screenGearboxes——经接口引用消费）
// ---------------------------------------------------------------------

/// 电机筛选对移动关节轴输出"范围外"记录：DataInsufficient＋原因恒空＋
/// 恰一条范围外缺口（稳定码 SEL-INPUT-AXIS-OUT-OF-SCOPE）——超限工作点
/// 不产生任何电机淘汰原因（证明旋转传动维度根本未执行——不静默套用）。
TEST(SelAxisOutOfScope, MotorScreenBlocksPrismaticAxis)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-09"}, std::vector<std::string>{"AT-08"});  // R1——移动关节轴电机筛选阻断——DataInsufficient＋SEL-INPUT-AXIS-OUT-OF-SCOPE
    const CatalogPackageSnapshot snapshot = makeSnapshot();
    const core::ObjectId axis = core::ObjectId::generate();
    const IHardConstraintSelector& selector = HardConstraintSelector{};

    const std::vector<FeasibilityRecord> records = selector.screenMotors(
        snapshot, {makePrismaticOverloadFacts(axis, "case-P")},
        defaultCriteria(), nullptr);

    // 记录数不变量：1 候选 × 1 轴 ＝ 1 条（截断感知计数不受阻断影响）。
    ASSERT_EQ(records.size(), std::size_t{1});
    const FeasibilityRecord& rec = records[0];
    EXPECT_EQ(rec.id, std::string("M-SCOPE|") + axis.toCanonical());
    EXPECT_EQ(rec.deviceKind, DeviceKind::Motor);
    EXPECT_EQ(rec.candidateModelId, "M-SCOPE");
    EXPECT_EQ(rec.axisId, axis);
    // DataInsufficient 语义（SEL-09/D-SEL-15——不是 Rejected：范围外是
    // 数据/边界类事实，不是候选能力淘汰，不升级整机不可行）。
    EXPECT_EQ(rec.verdict, VerdictKind::DataInsufficient);
    // 零淘汰原因——超限工作点未被消费（不静默套用旋转传动的最强反证：
    // 若任一维度执行了，τ_peak=900＞10 等必产生 SEL-MOTOR-* 原因）。
    EXPECT_TRUE(rec.reasons.empty())
        << "移动关节轴不得产生任何旋转传动淘汰原因（SEL-09）";
    // 恰一条范围外缺口，稳定码/定位面/工况无关性逐字段断言。
    ASSERT_EQ(rec.gaps.size(), std::size_t{1});
    const std::optional<DataGap> gap = findOutOfScopeGap(rec);
    ASSERT_TRUE(gap.has_value());
    EXPECT_EQ(gap->diagCode, std::string(kSelInputAxisOutOfScope));
    EXPECT_EQ(gap->axisId, axis);
    EXPECT_TRUE(gap->caseId.empty());  // 轴级边界事实——与工况无关
}

/// 减速器筛选同构阻断：速比换算 ω_m＝ω_joint/c 亦不适用（§8.2 的唯一
/// 自算映射量对直线轴无语义——超限关节侧转矩也不产生减速器原因）。
TEST(SelAxisOutOfScope, GearboxScreenBlocksPrismaticAxis)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-09"}, std::vector<std::string>{"AT-08"});  // R1——移动关节轴减速器筛选阻断——速比换算不适用
    const CatalogPackageSnapshot snapshot = makeSnapshot();
    const core::ObjectId axis = core::ObjectId::generate();
    const IHardConstraintSelector& selector = HardConstraintSelector{};

    const std::vector<FeasibilityRecord> records = selector.screenGearboxes(
        snapshot, {makePrismaticOverloadFacts(axis, "case-P")},
        defaultCriteria(), nullptr);

    ASSERT_EQ(records.size(), std::size_t{1});
    const FeasibilityRecord& rec = records[0];
    EXPECT_EQ(rec.deviceKind, DeviceKind::Gearbox);
    EXPECT_EQ(rec.candidateModelId, "G-SCOPE");
    EXPECT_EQ(rec.verdict, VerdictKind::DataInsufficient);
    EXPECT_TRUE(rec.reasons.empty())
        << "移动关节轴不得产生任何减速器淘汰原因（含输入转速维——"
           "ω_m＝ω_joint/c 对直线轴不执行）";
    ASSERT_EQ(rec.gaps.size(), std::size_t{1});
    const std::optional<DataGap> gap = findOutOfScopeGap(rec);
    ASSERT_TRUE(gap.has_value());
    EXPECT_EQ(gap->diagCode, std::string(kSelInputAxisOutOfScope));
}

/// 范围外缺口与词表→稳定码映射同码：ReasonToken::AxisOutOfScope 经唯一
/// 映射点得到的稳定码与缺口 diagCode 一致（同一语义在"原因词表面"与
/// "数据缺口面"分轨使用同一稳定诊断——DiagCodes 登记表单源）。
TEST(SelAxisOutOfScope, GapStableCodeMatchesTokenMapping)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-09"}, std::vector<std::string>{});  // R1——范围外语义的两承载面同码（词表映射唯一实现点）
    const core::ObjectId axis = core::ObjectId::generate();
    const DataGap gap =
        makeAxisOutOfScopeGap(axis, std::string(kSelInputAxisOutOfScope));
    // 词表 token→稳定码唯一映射点（DiagCodes.hpp）给出同一码值。
    EXPECT_EQ(reasonTokenDiagCode(ReasonToken::AxisOutOfScope),
              kSelInputAxisOutOfScope);
    EXPECT_EQ(gap.diagCode, std::string(reasonTokenDiagCode(ReasonToken::AxisOutOfScope)));
    // 定位词与词表文本一致（呈现层可按同一定位词跨面检索）。
    EXPECT_EQ(gap.dimension, std::string(reasonTokenText(ReasonToken::AxisOutOfScope)));
}

/// 混合链逐轴独立：J1 旋转（超限→正常淘汰）＋J2 移动（范围外阻断）——
/// 两轴互不污染：旋转轴判定完整执行，移动轴零旋转原因。
TEST(SelAxisOutOfScope, MixedChainAxesIndependent)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-09", "MDL-12"}, std::vector<std::string>{"AT-08", "AT-17"});  // R1——混合链：旋转轴正常判定与移动轴范围外阻断互不污染
    const CatalogPackageSnapshot snapshot = makeSnapshot();
    const core::ObjectId j1 = core::ObjectId::generate();  // 旋转轴（超限）
    const core::ObjectId j2 = core::ObjectId::generate();  // 移动轴
    const IHardConstraintSelector& selector = HardConstraintSelector{};

    const std::vector<FeasibilityRecord> records = selector.screenMotors(
        snapshot,
        {makeRevoluteFacts(j1, "case-A", /*overload=*/true),
         makePrismaticOverloadFacts(j2, "case-A")},
        defaultCriteria(), nullptr);

    // 记录数不变量：1 候选 × 2 轴 ＝ 2 条。
    ASSERT_EQ(records.size(), std::size_t{2});
    std::optional<FeasibilityRecord> recJ1;
    std::optional<FeasibilityRecord> recJ2;
    for (const FeasibilityRecord& r : records) {
        if (r.axisId == j1) { recJ1 = r; }
        if (r.axisId == j2) { recJ2 = r; }
    }
    ASSERT_TRUE(recJ1.has_value());
    ASSERT_TRUE(recJ2.has_value());
    // J1 旋转轴：正常维度判定——超限峰值转矩产生淘汰（旋转面不受阻断影响）。
    EXPECT_EQ(recJ1->verdict, VerdictKind::Rejected);
    ASSERT_FALSE(recJ1->reasons.empty());
    bool hasPeakReason = false;
    for (const RejectionReason& r : recJ1->reasons) {
        if (r.token == ReasonToken::TorquePeakInsufficient) {
            hasPeakReason = true;
            EXPECT_EQ(r.axisId, j1);
            EXPECT_DOUBLE_EQ(r.actual, 20.0);   // N·m（工作点黄金值）
            EXPECT_DOUBLE_EQ(r.required, 10.0); // N·m（M-SCOPE 峰值能力）
        }
    }
    EXPECT_TRUE(hasPeakReason) << "旋转轴维度判定必须完整执行（阻断只作用于移动轴）";
    // J2 移动轴：范围外形态（零旋转原因——超限值未被消费）。
    EXPECT_EQ(recJ2->verdict, VerdictKind::DataInsufficient);
    EXPECT_TRUE(recJ2->reasons.empty());
    ASSERT_EQ(recJ2->gaps.size(), std::size_t{1});
    EXPECT_EQ(recJ2->gaps[0].diagCode, std::string(kSelInputAxisOutOfScope));
}

/// 多工况同轴只产生一条范围外缺口（轴级边界事实与工况无关——缺口按轴
/// 计数不按工况重复），且 caseId 为空串。
TEST(SelAxisOutOfScope, MultiCasePrismaticYieldsSingleAxisLevelGap)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-09"}, std::vector<std::string>{"AT-08"});  // R1——范围外缺口与工况无关——多工况恰一条
    const CatalogPackageSnapshot snapshot = makeSnapshot();
    const core::ObjectId axis = core::ObjectId::generate();
    const IHardConstraintSelector& selector = HardConstraintSelector{};

    const std::vector<FeasibilityRecord> records = selector.screenMotors(
        snapshot,
        {makePrismaticOverloadFacts(axis, "case-1"),
         makePrismaticOverloadFacts(axis, "case-2")},
        defaultCriteria(), nullptr);

    ASSERT_EQ(records.size(), std::size_t{1});
    const FeasibilityRecord& rec = records[0];
    EXPECT_EQ(rec.verdict, VerdictKind::DataInsufficient);
    ASSERT_EQ(rec.gaps.size(), std::size_t{1})
        << "范围外是轴级事实——多工况不得重复计入缺口";
    EXPECT_TRUE(rec.gaps[0].caseId.empty());
}

/// 记录数不变量与确定性：同输入两次调用同输出（NFR-COR-02）；批量
/// （2 候选 × 2 轴，其一移动）记录数＝4。
TEST(SelAxisOutOfScope, RecordCountInvariantAndDeterminism)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-09", "NFR-COR-02"}, std::vector<std::string>{"AT-08"});  // R1——记录数不变量＋确定性
    CatalogPackageSnapshot snapshot = makeSnapshot();
    // 第二候选（可行形态——混合断言面）。
    MotorCatalogEntry m2 = makeScopeMotor();
    m2.modelId = "M-SCOPE2";
    snapshot.motors.push_back(m2);
    const core::ObjectId j1 = core::ObjectId::generate();
    const core::ObjectId j2 = core::ObjectId::generate();
    const IHardConstraintSelector& selector = HardConstraintSelector{};

    const std::vector<AxisWorkpointFacts> facts = {
        makeRevoluteFacts(j1, "case-A", false),          // 旋转可行
        makePrismaticOverloadFacts(j2, "case-A"),        // 移动阻断
    };
    const std::vector<FeasibilityRecord> first =
        selector.screenMotors(snapshot, facts, defaultCriteria(), nullptr);
    const std::vector<FeasibilityRecord> second =
        selector.screenMotors(snapshot, facts, defaultCriteria(), nullptr);

    // 记录数不变量：2 候选 × 2 轴 ＝ 4（移动轴也逐候选产出记录）。
    ASSERT_EQ(first.size(), std::size_t{4});
    EXPECT_EQ(first, second) << "同输入恒同输出（NFR-COR-02）";
    // 每根移动轴的两条记录均为范围外形态。
    std::size_t outOfScopeCount = 0;
    for (const FeasibilityRecord& r : first) {
        if (r.axisId == j2) {
            EXPECT_EQ(r.verdict, VerdictKind::DataInsufficient);
            EXPECT_TRUE(r.reasons.empty());
            ++outOfScopeCount;
        }
    }
    EXPECT_EQ(outOfScopeCount, std::size_t{2});
}

/// 同轴 jointKind 矛盾 fail-fast：同一根轴既声明旋转又声明移动＝物理
/// 矛盾（轴类型是轴级属性）——校验边界整批拒绝（§10.2 调用方契约违约），
/// 两筛选器同语义。
TEST(SelAxisOutOfScope, JointKindConflictFailsFast)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-09"}, std::vector<std::string>{"AT-08"});  // R1——同轴关节类型矛盾＝调用方契约违约 fail-fast
    const CatalogPackageSnapshot snapshot = makeSnapshot();
    const core::ObjectId axis = core::ObjectId::generate();
    AxisWorkpointFacts rotating = makeRevoluteFacts(axis, "case-1", false);
    AxisWorkpointFacts prismatic = makePrismaticOverloadFacts(axis, "case-2");
    const IHardConstraintSelector& selector = HardConstraintSelector{};

    EXPECT_THROW((void)selector.screenMotors(snapshot, {rotating, prismatic},
                                             defaultCriteria(), nullptr),
                 std::invalid_argument);
    EXPECT_THROW((void)selector.screenGearboxes(snapshot, {rotating, prismatic},
                                                defaultCriteria(), nullptr),
                 std::invalid_argument);
}

/// 显式 Revolute 声明的零回归：黄金可行工作点＋显式旋转声明 → 判定
 /// Feasible（默认值路径之外的显式路径同语义——T04 黄金表行为不变）。
TEST(SelAxisOutOfScope, ExplicitRevoluteUnaffected)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-09"}, std::vector<std::string>{"AT-08"});  // R1——显式旋转声明零回归——旋转链行为不变
    const CatalogPackageSnapshot snapshot = makeSnapshot();
    const core::ObjectId axis = core::ObjectId::generate();
    const IHardConstraintSelector& selector = HardConstraintSelector{};

    const std::vector<FeasibilityRecord> records = selector.screenMotors(
        snapshot, {makeRevoluteFacts(axis, "case-A", /*overload=*/false)},
        defaultCriteria(), nullptr);

    ASSERT_EQ(records.size(), std::size_t{1});
    EXPECT_EQ(records[0].verdict, VerdictKind::Feasible);
    EXPECT_TRUE(records[0].reasons.empty());
    EXPECT_TRUE(records[0].gaps.empty());
}
