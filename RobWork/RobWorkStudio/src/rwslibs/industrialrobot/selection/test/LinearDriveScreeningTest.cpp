/**
 * @file   LinearDriveScreeningTest.cpp
 * @brief  直线传动硬筛选用例组（SelLinearScreening）——直线轴类型化广义
 *         量工作点事实（drivetrain §16.2 扩展端口消费承载）经既有
 *         IHardConstraintSelector 接口面的筛选黄金对照、确定性回归与
 *         "不改变六/七轴全旋转链现有行为"回归钉扎（WP-19-T12——
 *         SEL-09-S1 选型层；AT-36 选型侧）。
 *
 * 设计依据：
 *   - units/selection.md §17.2（SEL-09-S1 选型层：直线工作点映射经
 *     drivetrain 扩展端口消费——selection 只消费不自实现；可行与不可行
 *     样例齐备）、§7.2（逐维独立执行不短路）、§10.3/§10.4（原因词表与
 *     稳定排序——直线组 token 复用同一套纪律）、§6.2（插值失败≠候选
 *     能力不足——外推拒绝分轨，禁外推不放宽）、§2.2（移动关节范围外
 *     R1 语义不因本任务放宽）、§18（SEL-09-S1 验证位置）
 *   - 需求 SEL-09-S1（可行/不可行样例——不可行项含实际值/阈值与原因；
 *     不改变六/七轴全旋转链现有行为——V12-03 收窄）、SEL-06（逐项淘汰
 *     原因含实际值与阈值——ERR-01）、NFR-COR-02（稳定排序）
 *   - 任务契约 tasks/foundation/WP-19-T12.json acceptance 1（可行与不可行
 *     样例齐备）＋acceptance 2（经既有评估器/组合校核接口面消费）＋
 *     acceptance 3（回归用例证明既有旋转链输出逐项不变）
 *
 * 测试策略（SelGoldenDatasetTest 同款形态）：期望值全部来自数据集
 * sel-linear-golden@1.0.0 的 expected/linear-screening-expected.json
 * （generate/ 脚本按 §17.2 维度集＋附录 D C7 容差＋§10.4 排序独立推导），
 * 本文件只做"装载→驱动产品入口→黄金对照"；驱动入口＝IHardConstraint
 * Selector 接口引用（screenLinearDrives——接口路径钉扎，不留只测实现的
 * 盲区）。
 *
 * 线程约束：gtest 用例天然串行；被测入口纯函数（卡 §14.10）。
 */

#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/selection/CatalogProvider.hpp>
#include <sdurws/ird/selection/CatalogTypes.hpp>
#include <sdurws/ird/selection/DiagCodes.hpp>
#include <sdurws/ird/selection/LinearDrive.hpp>
#include <sdurws/ird/selection/Screening.hpp>
#include <sdurws/ird/testkit/JsonLite.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include <sdurws/ird/testkit/gtest/GoldenFixture.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace sel = sdurws::ird::selection;
namespace core = sdurws::ird::core;
namespace tk = sdurws::ird::testkit;

// =====================================================================
// 黄金装载脚手架（筛选输入＝inputs/screening-inputs.json 业务模型直载；
// 期望＝expected/linear-screening-expected.json）
// =====================================================================

namespace {

std::string readDsText(const tk::GoldenDataset& ds, const char* rel, bool inputSide)
{
    const auto path = inputSide ? ds.resolveInput(rel) : ds.resolveExpected(rel);
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

tk::JsonValue loadDsJson(const tk::GoldenDataset& ds, const char* rel, bool inputSide)
{
    return tk::parseJson(readDsText(ds, rel, inputSide));
}

std::string str(const tk::JsonValue& o, const char* key)
{
    const tk::JsonValue* v = o.find(key);
    return (v != nullptr && v->isString()) ? v->text : std::string{};
}

double num(const tk::JsonValue& o, const char* key)
{
    const tk::JsonValue* v = o.find(key);
    return (v != nullptr && v->isNumber()) ? v->number : 0.0;
}

std::optional<double> optNum(const tk::JsonValue& o, const char* key)
{
    const tk::JsonValue* v = o.find(key);
    if (v == nullptr || v->isNull() || !v->isNumber()) {
        return std::nullopt;   // null＝该量未供给（事实侧缺口语义）
    }
    return v->number;
}

/// 黄金输入 → 直线器件目录快照（业务模型直载——与 valid 包同源；曲线经
/// 统一构造入口 tryMakePerformanceCurve 装配，保内容身份面）。
sel::CatalogPackageSnapshot makeLinearSnapshot(const tk::GoldenDataset& ds)
{
    const tk::JsonValue in = loadDsJson(ds, "inputs/screening-inputs.json", true);
    sel::CatalogPackageSnapshot snap;
    snap.manifest.formatVersion = sel::kCatalogFormatVersionV2;
    const tk::JsonValue& id = *in.find("identity");
    snap.manifest.identity.catalogId = str(id, "catalogId");
    snap.manifest.identity.version = str(id, "version");
    snap.manifest.identity.source = str(id, "source");
    const sel::CatalogIdentity catalog = snap.manifest.identity;

    // 曲线表（owner=linear-drive；v2 量纲词表——具名键装载，与 inputs 同源）。
    const tk::JsonValue& curves = *in.find("curves");
    for (const char* key : {"bs-force", "rp-load-power", "lm-force"}) {
        const tk::JsonValue* c = curves.find(key);
        if (c == nullptr) {
            continue;
        }
        std::vector<sel::CapabilityPoint> pts;
        for (const tk::JsonValue& p : c->find("points")->items) {
            pts.push_back({num(p, "x"), num(p, "y")});
        }
        sel::CatalogIssue reject;
        std::optional<sel::PerformanceCurve> curve = sel::tryMakePerformanceCurve(
            key, str(*c, "xQuantity"), str(*c, "yQuantity"),
            str(*c, "xUnit"), str(*c, "yUnit"), std::move(pts), catalog, reject);
        if (!curve.has_value()) {
            ADD_FAILURE() << "黄金曲线构造失败: " << key << " — " << reject.code;
            continue;
        }
        snap.curves.push_back(std::move(*curve));
    }

    // 直线器件条目（四类——黄金 inputs 直载）。
    for (const tk::JsonValue& e : in.find("drives")->items) {
        sel::LinearDriveCatalogEntry d;
        d.modelId = str(e, "modelId");
        d.catalog = catalog;
        const std::string kindText = str(e, "kind");
        for (int k = 0; k < sel::kLinearDriveKindCount; ++k) {
            if (sel::linearDriveKindText(static_cast<sel::LinearDriveKind>(k)) == kindText) {
                d.kind = static_cast<sel::LinearDriveKind>(k);
            }
        }
        d.ratedForce = num(e, "ratedForce");
        d.peakForce = num(e, "peakForce");
        d.maxLinearSpeed = num(e, "maxLinearSpeed");
        d.ratedPower = num(e, "ratedPower");
        d.efficiency = optNum(e, "efficiency");
        d.stroke = optNum(e, "stroke");
        d.mass = num(e, "mass");
        d.mounting.flangeKind = str(e, "flangeKind");
        d.mounting.shaftKind = str(e, "shaftKind");
        for (const tk::JsonValue& r : e.find("curveRefs")->items) {
            // 引用的量纲声明自曲线组反查（与装配入口同语义——引用携量纲）。
            std::string xq, yq;
            for (const sel::PerformanceCurve& c : snap.curves) {
                if (c.curveId == r.text) {
                    xq = c.xQuantity;
                    yq = c.yQuantity;
                }
            }
            d.curves.push_back({r.text, xq, yq});
        }
        d.status = sel::ValidationStatus::Valid;
        snap.linearDrives.push_back(std::move(d));
    }

    // 条目按 modelId 升序（装配入口排序契约——LD-BS/LD-LM/LD-RP/LD-TB）。
    std::sort(snap.linearDrives.begin(), snap.linearDrives.end(),
              [](const sel::LinearDriveCatalogEntry& a,
                 const sel::LinearDriveCatalogEntry& b) { return a.modelId < b.modelId; });

    // 电机条目（自持最小面——旋转链零回归钉扎的候选集；字段自持不依赖
    // 目录包黄金，固定值即"旋转链黄金输入"的能力侧）。
    sel::MotorCatalogEntry m;
    m.modelId = "M-CT-100";
    m.catalog = catalog;
    m.ratedTorque = 5.0;      ///< N·m（额定连续转矩）
    m.peakTorque = 12.0;      ///< N·m（峰值转矩）
    m.ratedSpeed = 150.0;     ///< rad/s（额定转速）
    m.maxSpeed = 250.0;       ///< rad/s（最高转速）
    m.ratedPower = 1500.0;    ///< W（额定功率）
    m.dutyClass = "S1";
    m.rotorInertia = 0.01;    ///< kg·m²（转子惯量）
    m.mass = 4.0;             ///< kg
    m.status = sel::ValidationStatus::Valid;
    snap.motors.push_back(std::move(m));

    snap.contentIdentity = sel::computePackageContentIdentity(snap);
    snap.manifest.identity.contentIdentity = snap.contentIdentity;
    return snap;
}

/// 黄金输入 → 直线轴工作点事实（类型化广义量——值传递；组装方语义）。
std::vector<sel::LinearAxisWorkpointFacts> makeLinearFacts(const tk::JsonValue& factsJson,
                                                           const tk::JsonValue& axes,
                                                           const char* axisKey)
{
    std::vector<sel::LinearAxisWorkpointFacts> facts;
    for (const tk::JsonValue& f : factsJson.items) {
        sel::LinearAxisWorkpointFacts fact;
        fact.jointId = core::ObjectId::fromCanonical(str(axes, axisKey));
        fact.caseId = str(f, "caseId");
        fact.forceRms = optNum(f, "forceRms");
        fact.forcePeak = optNum(f, "forcePeak");
        fact.linearSpeedPeak = optNum(f, "linearSpeedPeak");
        fact.powerPeak = optNum(f, "powerPeak");
        fact.powerRms = optNum(f, "powerRms");
        fact.displacementPeak = optNum(f, "displacementPeak");
        fact.accelerationPeak = optNum(f, "accelerationPeak");
        fact.atTime = num(f, "atTime");
        fact.segmentId = str(f, "segmentId");
        facts.push_back(std::move(fact));
    }
    return facts;
}

}  // namespace

// =====================================================================
// 黄金消费夹具
// =====================================================================

class SelLinearScreening : public tk::GoldenFixture {
protected:
    tk::DatasetRef datasetRef() const override { return {"sel-linear-golden", "1.0.0"}; }
};

// ---------------------------------------------------------------------
// acceptance 1：可行与不可行样例齐备（不可行项含实际值/阈值/原因——
// SEL-06 口径；经 IHardConstraintSelector 接口引用消费——acceptance 2
// 的接口面钉扎）
// ---------------------------------------------------------------------

/**
 * 直线筛选黄金对照（AT-36 选型侧）：逐算例经 IHardConstraintSelector::
 * screenLinearDrives 接口引用驱动（接口路径钉扎）→逐记录对照黄金
 * （verdict/reasons 全字段〔token/actual/required/unit/thresholdSource〕
 * /gaps〔dimension/diagCode〕）。插值锚点（独立双路防"两路同错"）：
 * bs-force 0.65 m/s→10500 N（0.5→0.8 段线性）、恰边界 10500≤10500 不误
 * 淘汰（附录 D C7 容差）；区间外 0.9 m/s 对 bs-force [0.2,0.8]＝外推拒绝
 * 数据缺口（SEL-02 禁外推不放宽——缺口与能力不足分轨，§6.2）。
 */
TEST_F(SelLinearScreening, LinearScreeningGolden_WP19T12_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-09-S1", "SEL-06", "NFR-COR-01"},
                  std::vector<std::string>{"AT-36"});

    const sel::CatalogPackageSnapshot snap = makeLinearSnapshot((*dataset));
    const tk::JsonValue in = loadDsJson((*dataset), "inputs/screening-inputs.json", true);
    const tk::JsonValue exp = loadDsJson((*dataset), "expected/linear-screening-expected.json", false);
    const tk::JsonValue& expRecords = *exp.find("records");

    // 经接口引用消费（IHardConstraintSelector——acceptance 2 接口面钉扎）。
    const sel::IHardConstraintSelector& selector = sel::HardConstraintSelector{};
    std::size_t expIdx = 0;
    for (const tk::JsonValue& c : in.find("cases")->items) {
        const std::string axisKey = str(c, "axis");
        const std::vector<sel::LinearAxisWorkpointFacts> facts =
            makeLinearFacts(*c.find("factsByAxis")->find(axisKey.c_str()),
                            *in.find("axes"), axisKey.c_str());
        sel::ScreeningCriteria criteria;
        criteria.safetyFactor = num(c, "safetyFactor") != 0.0 ? num(c, "safetyFactor") : 1.0;

        const std::vector<sel::FeasibilityRecord> records =
            selector.screenLinearDrives(snap, facts, criteria, nullptr);
        // 记录集＝快照全量直线器件×该轴（screenLinearDrives 遍历语义——
        // cases.drives 仅是黄金 inputs 的算例说明面）；对位靠黄金逐记录
        // id 断言（下方），数量由末尾"黄金记录全量消费"断言兜底。
        for (std::size_t i = 0; i < records.size(); ++i, ++expIdx) {
            const sel::FeasibilityRecord& rec = records[i];
            ASSERT_LT(expIdx, expRecords.items.size());
            const tk::JsonValue& e = expRecords.items[expIdx];

            // 记录身份（组合键＝候选×轴——与旋转通道同构）。
            EXPECT_EQ(rec.id, str(e, "id")) << "记录键";
            EXPECT_EQ(rec.candidateModelId, str(e, "candidateModelId"));
            EXPECT_EQ(rec.deviceKind, sel::DeviceKind::LinearDrive)
                << "直线通道记录类别（表尾追加枚举值）";

            // verdict 三态。
            const std::string expVerdict = str(e, "verdict");
            const std::string gotVerdict =
                rec.verdict == sel::VerdictKind::Feasible ? "Feasible"
                : rec.verdict == sel::VerdictKind::Rejected ? "Rejected"
                                                            : "DataInsufficient";
            EXPECT_EQ(gotVerdict, expVerdict) << rec.id << " 判定结论";

            // 逐原因全字段（SEL-06——实际值/阈值/单位/阈值来源齐备）。
            const tk::JsonValue& expReasons = *e.find("reasons");
            ASSERT_EQ(rec.reasons.size(), expReasons.items.size())
                << rec.id << " 原因数（§10.4 稳定排序后）";
            for (std::size_t k = 0; k < rec.reasons.size(); ++k) {
                const sel::RejectionReason& r = rec.reasons[k];
                const tk::JsonValue& er = expReasons.items[k];
                EXPECT_EQ(sel::reasonTokenText(r.token), str(er, "token"))
                    << rec.id << " 原因[" << k << "] token（词表序锚）";
                EXPECT_EQ(r.caseId, str(er, "caseId")) << rec.id << " 原因[" << k << "]";
                EXPECT_EQ(r.actual, num(er, "actual")) << rec.id << " 原因[" << k
                                                       << "] 实际值（SI——×SF 复判后）";
                EXPECT_EQ(r.required, num(er, "required")) << rec.id << " 原因[" << k
                                                           << "] 阈值（实际值/阈值——SEL-06）";
                EXPECT_EQ(r.unit, str(er, "unit")) << rec.id << " 原因[" << k << "] 单位";
                EXPECT_EQ(r.thresholdSource, str(er, "thresholdSource"))
                    << rec.id << " 原因[" << k << "] 阈值来源";
                EXPECT_FALSE(r.suggestion.empty())
                    << rec.id << " 原因[" << k << "] 建议动作（ERR-01）";
            }

            // 逐缺口（外推拒绝等数据不足分轨——与能力不足原因分轨）。
            const tk::JsonValue& expGaps = *e.find("gaps");
            ASSERT_EQ(rec.gaps.size(), expGaps.items.size())
                << rec.id << " 缺口数（插值失败≠候选能力不足——分轨）";
            for (std::size_t k = 0; k < rec.gaps.size(); ++k) {
                const sel::DataGap& g = rec.gaps[k];
                const tk::JsonValue& eg = expGaps.items[k];
                EXPECT_EQ(g.dimension, str(eg, "dimension"))
                    << rec.id << " 缺口[" << k << "] 维度";
                EXPECT_EQ(g.caseId, str(eg, "caseId"))
                    << rec.id << " 缺口[" << k << "] 工况";
                const std::string expDiag = str(eg, "diagCode");
                if (!expDiag.empty()) {
                    EXPECT_EQ(g.diagCode, expDiag)
                        << rec.id << " 缺口[" << k << "] 稳定码（外推拒绝面）";
                    EXPECT_EQ(g.diagCode, std::string{sel::kSelCurveExtrapolationDenied})
                        << "禁外推不放宽——外推拒绝码随缺口登记（§6.2）";
                }
            }
        }
    }
    ASSERT_EQ(expIdx, expRecords.items.size()) << "黄金记录全量消费";
}

// ---------------------------------------------------------------------
// acceptance 3：确定性（NFR-COR-02）与旋转链零回归（V12-03 收窄——
// 回归用例证明既有旋转链输出逐项不变；SEL-09 R1 语义不放宽）
// ---------------------------------------------------------------------

/**
 * 两次筛选逐记录全等（NFR-COR-02——同输入恒同输出；经接口二次调用）。
 */
TEST_F(SelLinearScreening, LinearScreeningDeterministic_WP19T12_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-09-S1", "NFR-COR-02"},
                  std::vector<std::string>{"AT-36"});

    const sel::CatalogPackageSnapshot snap = makeLinearSnapshot((*dataset));
    const tk::JsonValue in = loadDsJson((*dataset), "inputs/screening-inputs.json", true);
    const tk::JsonValue& c = in.find("cases")->items[0];
    const std::string axisKey = str(c, "axis");
    const std::vector<sel::LinearAxisWorkpointFacts> facts =
        makeLinearFacts(*c.find("factsByAxis")->find(axisKey.c_str()),
                        *in.find("axes"), axisKey.c_str());
    sel::ScreeningCriteria criteria;

    const sel::IHardConstraintSelector& selector = sel::HardConstraintSelector{};
    const std::vector<sel::FeasibilityRecord> first =
        selector.screenLinearDrives(snap, facts, criteria, nullptr);
    const std::vector<sel::FeasibilityRecord> second =
        selector.screenLinearDrives(snap, facts, criteria, nullptr);
    ASSERT_EQ(first.size(), second.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        EXPECT_EQ(first[i], second[i]) << "记录 " << i << " 两次筛选全等（NFR-COR-02）";
    }
}

/**
 * 旋转链零回归（V12-03 收窄——WP-19-T12 acceptance 3）：①同一 v2 快照
 * 下，直线器件条目的存在不改变旋转筛选输出——旋转黄金输入对"快照含
 * 直线条目"与"快照直线表清空"两种形态逐记录全等；②移动关节范围外
 * 诊断（SEL-09 R1 语义）不放宽——Prismatic 轴在 screenMotors/
 * screenGearboxes 仍产生范围外记录（DataInsufficient＋axis-out-of-scope
 * 缺口＋SEL-INPUT-AXIS-OUT-OF-SCOPE 稳定码，零旋转维度判定），且直线
 * 通道不产生 DeviceCombination（组合构造直线轴通道为卡 §17.2 预留——
 * 本任务不落位的结构证据）。
 */
TEST_F(SelLinearScreening, RotationChainUnchangedByLinearCatalog_WP19T12_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-09-S1", "SEL-09"},
                  std::vector<std::string>{"AT-36"});

    const sel::CatalogPackageSnapshot withLinear = makeLinearSnapshot((*dataset));
    ASSERT_FALSE(withLinear.linearDrives.empty());

    // 旋转链黄金输入（自持最小面——旋转工作点事实＋直线轴事实并列；
    // 轴 ID＝黄金 canonical 文本严格解析——"obj-<32 hex>"）。
    const core::ObjectId revAxis =
        core::ObjectId::fromCanonical("obj-00000000000000000000000000000401");
    const core::ObjectId linAxis =
        core::ObjectId::fromCanonical("obj-00000000000000000000000000000301");
    sel::AxisWorkpointFacts revFacts;
    revFacts.jointId = revAxis;
    revFacts.caseId = "case-nominal";
    revFacts.jointTorqueRms = 4.0;       // N·m（旋转链输入——黄金对照用固定值）
    revFacts.jointTorquePeak = 9.0;      // N·m
    revFacts.jointSpeedPeak = 200.0;     // rad/s
    revFacts.motorTorqueRms = 4.5;       // N·m
    revFacts.motorTorquePeak = 10.0;     // N·m
    revFacts.motorSpeedPeak = 210.0;     // rad/s
    revFacts.motorSpeedRms = 150.0;      // rad/s
    revFacts.motorPowerPeak = 1900.0;    // W
    revFacts.motorPowerRms = 1100.0;     // W
    // 移动关节轴的旋转工作点物理不存在——组装方全部保持 nullopt（不伪造
    // 工作点，卡 §2.2；AxisWorkpointFacts 契约）。
    sel::AxisWorkpointFacts linFactsAsRotary;
    linFactsAsRotary.jointId = linAxis;
    linFactsAsRotary.caseId = "case-nominal";
    linFactsAsRotary.jointKind = sel::JointKind::Prismatic;   // R1 范围外信号

    // ① 快照含/不含直线条目——旋转筛选输出逐记录全等（直线表零干扰）。
    sel::CatalogPackageSnapshot withoutLinear = withLinear;
    withoutLinear.linearDrives.clear();
    withoutLinear.contentIdentity =
        sel::computePackageContentIdentity(withoutLinear);
    const sel::IHardConstraintSelector& selector = sel::HardConstraintSelector{};
    const std::vector<sel::AxisWorkpointFacts> rotaryFacts{revFacts, linFactsAsRotary};
    const std::vector<sel::FeasibilityRecord> withRecords =
        selector.screenMotors(withLinear, rotaryFacts, sel::ScreeningCriteria{}, nullptr);
    const std::vector<sel::FeasibilityRecord> withoutRecords =
        selector.screenMotors(withoutLinear, rotaryFacts, sel::ScreeningCriteria{}, nullptr);
    ASSERT_EQ(withRecords.size(), withoutRecords.size());
    for (std::size_t i = 0; i < withRecords.size(); ++i) {
        EXPECT_EQ(withRecords[i], withoutRecords[i])
            << "旋转链记录 " << i << " 在直线器件存在与否两种快照下逐项全等"
            << "（V12-03——直线通道纯新增表，零旋转链干扰）";
    }
    const std::vector<sel::FeasibilityRecord> gwWith =
        selector.screenGearboxes(withLinear, rotaryFacts, sel::ScreeningCriteria{}, nullptr);
    const std::vector<sel::FeasibilityRecord> gwWithout =
        selector.screenGearboxes(withoutLinear, rotaryFacts, sel::ScreeningCriteria{}, nullptr);
    ASSERT_EQ(gwWith.size(), gwWithout.size());
    for (std::size_t i = 0; i < gwWith.size(); ++i) {
        EXPECT_EQ(gwWith[i], gwWithout[i]) << "减速器链记录 " << i << " 同上";
    }

    // ② SEL-09 R1 语义不放宽：Prismatic 轴在旋转筛选器仍范围外阻断。
    bool sawOutOfScope = false;
    for (const sel::FeasibilityRecord& rec : withRecords) {
        if (rec.axisId == linAxis) {
            sawOutOfScope = true;
            EXPECT_EQ(rec.verdict, sel::VerdictKind::DataInsufficient)
                << "移动关节轴＝DataInsufficient（不静默套用旋转传动）";
            EXPECT_TRUE(rec.reasons.empty())
                << "范围外轴零旋转维度原因（不执行 §7 判定）";
            ASSERT_EQ(rec.gaps.size(), std::size_t{1});
            EXPECT_EQ(rec.gaps.front().dimension, "axis-out-of-scope");
            EXPECT_EQ(rec.gaps.front().diagCode,
                      std::string{sel::kSelInputAxisOutOfScope})
                << "SEL-09 范围外稳定码（不因本任务放宽）";
        }
    }
    EXPECT_TRUE(sawOutOfScope) << "移动关节轴的范围外记录应存在";

    // ③ 直线通道零组合语义：screenLinearDrives 是资格事实面，本单元不因
    //    它产生任何 DeviceCombination（组合构造直线轴通道＝§17.2 预留）。
    //    结构证据：Combination.hpp 的组合构造入口（IDeviceCombinationBuilder）
    //    无直线通道调用点——此处以"直线记录 deviceKind 恒 LinearDrive 且
    //    无 Combination 类别产出"作行为面钉扎。
    const std::vector<sel::LinearAxisWorkpointFacts> linFacts{
        [&] {
            sel::LinearAxisWorkpointFacts f;
            f.jointId = linAxis;
            f.caseId = "case-nominal";
            f.forceRms = 6000.0;          // N
            f.forcePeak = 10500.0;        // N
            f.linearSpeedPeak = 0.65;     // m/s
            f.powerPeak = 3800.0;         // W
            f.powerRms = 2600.0;          // W
            return f;
        }()};
    const std::vector<sel::FeasibilityRecord> linRecords =
        selector.screenLinearDrives(withLinear, linFacts, sel::ScreeningCriteria{}, nullptr);
    for (const sel::FeasibilityRecord& rec : linRecords) {
        EXPECT_NE(rec.deviceKind, sel::DeviceKind::Combination)
            << "直线通道只产出器件级资格记录（零组合语义——分期边界）";
    }
}
