/**
 * @file   PreferenceFilterTest.cpp
 * @brief  企业偏好过滤用例组（SelPreferenceFilter）——SEL-07 分轨语义：
 *         三个偏好维度（优选品牌/供应状态/系列限制）的白名单过滤与标注、
 *         "不改变硬能力判定"核心验收、非硬可行候选跳过偏好维度、确定性
 *         输出、契约违约 fail-fast 与接口路径消费证明。
 *
 * 设计依据：
 *   - units/selection.md §10.2（空集语义"用户过滤后为空＝偏好过滤结果，
 *     与硬约束淘汰分开——非工程结论"）、§10.3（user-preference-filtered
 *     边界/偏好组 token）、§10.4（分轨纪律）、D-SEL-8、§14.0（错误两分法）
 *   - 需求 SEL-07（企业自定义优选品牌、供应状态和系列限制，不改变硬
 *     能力判定）、ERR-01（偏好原因比较型文本字段）、NFR-COR-02（确定性）
 *   - 任务契约 tasks/foundation/WP-19-T07.json acceptance 1（"企业自定义
 *     优选品牌、供应状态和系列限制——不改变硬能力判定用例通过"）
 *
 * 数值口径：硬筛选工作点为解析黄金值（基线目录 M-100 额定连续转矩
 * 4.5 N·m / M-200 9.0 N·m——CatalogTestSupport 基线数据）；可行工作点取
 * τ_rms=1.0 N·m（远小于 4.5）、超限工作点取 τ_rms=999 N·m（大于全部
 * 候选额定值——全候选连续转矩不足淘汰）。偏好过滤是文本判定，无数值
 * 容差面（词表值逐字符精确匹配）。
 *
 * 线程约束：纯函数单线程调用（PreferenceFilter 可重入——无共享状态）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/selection/CatalogProvider.hpp>
#include <sdurws/ird/selection/CatalogTypes.hpp>
#include <sdurws/ird/selection/DiagCodes.hpp>
#include <sdurws/ird/selection/FeasibleSet.hpp>
#include <sdurws/ird/selection/Preference.hpp>
#include <sdurws/ird/selection/Screening.hpp>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO（需求/AT 追溯字段）

#include "CatalogTestSupport.hpp"

#include <stdexcept>
#include <string>
#include <vector>

using namespace sdurws::ird::selection;
using namespace sdurws::ird::selection::testsupport;

namespace {

/// 基线快照（两电机 M-100/M-200 vendor="Sinotech"、两减速器 G-50/G-120
/// vendor="Nabtesco"——经 CatalogImporter::assemble 装配的合法快照，
/// 包内容身份已回填）。
CatalogPackageSnapshot makeBaselineSnapshot()
{
    const CatalogImporter importer;
    return importer.assemble(makeBaselineInput(), makeBaselineManifest());
}

/// 可行电机工作点（全部电机侧维度通过：τ_rms=1.0 N·m < 4.5 N·m〔M-100
/// 额定〕；τ_peak=2.0 N·m 不进过载区〔<额定〕→ 过载维度不触发——
/// Screening.cpp 维度 5 触发式语义；电压/工作制/温度/制动/保持维度条件
/// 未配置＝不适用——条件缺失≠数据缺失，T04 落位细化 ⑧）。
AxisWorkpointFacts makeFeasibleFacts()
{
    AxisWorkpointFacts f;
    f.jointId = sdurws::ird::core::ObjectId::fromCanonical("obj-00000000000000000000000000000001");
    f.caseId = "case-cruise";
    f.motorTorqueRms = 1.0;    // 单位 N·m
    f.motorTorquePeak = 2.0;   // 单位 N·m
    f.motorSpeedPeak = 10.0;   // 单位 rad/s（< 300 rad/s 基线 max_speed）
    f.motorSpeedRms = 5.0;     // 单位 rad/s（< 150 rad/s 基线 rated_speed）
    f.motorPowerPeak = 100.0;  // 单位 W（< 2000 W 基线额定功率）
    f.motorPowerRms = 50.0;    // 单位 W
    // peakDuration 不供给：可行点不进过载区，维度不触发（零缺口）。
    return f;
}

/// 超限电机工作点（τ_rms=999 N·m > 全部候选额定连续转矩——两电机均
/// 连续转矩不足淘汰，ReasonToken::TorqueContinuousInsufficient）。
AxisWorkpointFacts makeOverloadFacts()
{
    AxisWorkpointFacts f = makeFeasibleFacts();
    f.motorTorqueRms = 999.0;  // 单位 N·m——超限注入
    return f;
}

/// 候选记录（用例 5/6 的值语义直构——偏好过滤只读 candidateModelId/
/// deviceKind/verdict 三个字段，直构记录免跑全筛选管线，聚焦分轨语义）。
FeasibilityRecord makeRecord(DeviceKind kind, const std::string& modelId,
                             VerdictKind verdict)
{
    FeasibilityRecord rec;
    rec.id = modelId + "|obj-00000000000000000000000000000001";
    rec.deviceKind = kind;
    rec.candidateModelId = modelId;
    rec.verdict = verdict;
    rec.catalog = makeBaselineSnapshot().manifest.identity;
    return rec;
}

/// make 调用计数器（接口路径消费证明——偏好原因必须经
/// IRejectionReasonProvider::make 接口构造，WP-20-T03"接口路径零覆盖"
/// 教训的钉扎面：记录调用次数与 token 实参）。
class CountingReasonProvider final : public IRejectionReasonProvider {
public:
    RejectionReason make(ReasonToken token, const ReasonContext& ctx) const override
    {
        ++makeCalls;
        lastToken = token;
        lastThresholdSource = ctx.thresholdSource;
        return RejectionReasonProvider{}.make(token, ctx);  // 真实构造（字段完备）
    }
    mutable int makeCalls = 0;                     ///< make 累计调用次数
    mutable ReasonToken lastToken = ReasonToken::InputInvalid; ///< 最近一次 token 实参
    mutable std::string lastThresholdSource;       ///< 最近一次 thresholdSource 实参
};

/// 在记录集中统计某 token 出现次数（用例 3 的"偏好 token 不混入硬记录"
/// 扫描工具——分轨纪律的执行证明）。
int countToken(const std::vector<FeasibilityRecord>& records, ReasonToken token)
{
    int n = 0;
    for (const FeasibilityRecord& r : records) {
        for (const RejectionReason& reason : r.reasons) {
            if (reason.token == token) {
                ++n;
            }
        }
    }
    return n;
}

}  // namespace

// =====================================================================
// 维度判定（品牌/供应状态/系列三维度逐维黄金断言）
// =====================================================================

/**
 * 品牌维度（SEL-07 维度 A）：命中优选白名单＝标注命中＋偏好通过＋零
 * 原因；未命中＝零标注＋偏好原因（thresholdSource/actualText/requiredText
 * 黄金值）＋diagRef=SEL-USER-PREFERENCE-FILTERED。vendor 从快照主表
 * 读取（商业权威＝目录条目——卡 §2.1），企业标注不携品牌。
 */
TEST(SelPreferenceFilter, VendorDimensionFiltersAndMarks)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-07"},
                  std::vector<std::string>{"AT-08"});
    const CatalogPackageSnapshot snap = makeBaselineSnapshot();
    const RejectionReasonProvider provider;
    const PreferenceFilter filter;

    // 命中：优选白名单含基线品牌 Sinotech（M-100/M-200 的 vendor）。
    EnterprisePreference hit;
    hit.preferredVendors = {"Sinotech"};
    const std::vector<FeasibilityRecord> records{
        makeRecord(DeviceKind::Motor, "M-100", VerdictKind::Feasible)};
    const std::vector<PreferenceFilterOutcome> outHit =
        filter.apply(snap, records, {}, hit, provider);
    ASSERT_EQ(outHit.size(), 1u);
    EXPECT_TRUE(outHit[0].preferredVendorHit);   // 标注面：命中优选
    EXPECT_TRUE(outHit[0].passesPreference);     // 过滤面：通过
    EXPECT_TRUE(outHit[0].preferenceReasons.empty());

    // 未命中：白名单只有 OtherCo——原因黄金三字段＋稳定码引用。
    EnterprisePreference miss;
    miss.preferredVendors = {"OtherCo"};
    const std::vector<PreferenceFilterOutcome> outMiss =
        filter.apply(snap, records, {}, miss, provider);
    ASSERT_EQ(outMiss.size(), 1u);
    EXPECT_FALSE(outMiss[0].preferredVendorHit);
    EXPECT_FALSE(outMiss[0].passesPreference);
    ASSERT_EQ(outMiss[0].preferenceReasons.size(), 1u);
    const RejectionReason& r = outMiss[0].preferenceReasons[0];
    EXPECT_EQ(r.token, ReasonToken::UserPreferenceFiltered);        // 分轨 token
    ASSERT_TRUE(r.diagRef.has_value());
    EXPECT_EQ(*r.diagRef, std::string(kSelUserPreferenceFiltered)); // SEL-USER-PREFERENCE-FILTERED
    EXPECT_EQ(r.thresholdSource, kPrefDimVendor);                   // 维度词表
    EXPECT_EQ(r.actualText, "Sinotech");                            // 实际品牌（来自快照）
    EXPECT_EQ(r.requiredText, "OtherCo");                           // 期望白名单
    EXPECT_EQ(r.candidateModelId, "M-100");
    EXPECT_EQ(r.catalog, snap.manifest.identity);                   // 判定快照身份（追溯）
}

/**
 * 供应状态与系列维度（SEL-07 维度 B/C）：白名单命中通过；未命中携带
 * 实际标注；标注缺失以"(未标注)"哨兵承载且不默认通过（企业漏标保守
 * 放行的反面——呈现层可见原因）。
 */
TEST(SelPreferenceFilter, AvailabilityAndSeriesWhitelistWithMissingAnnotation)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-07"},
                  std::vector<std::string>{"AT-08"});
    const CatalogPackageSnapshot snap = makeBaselineSnapshot();
    const RejectionReasonProvider provider;
    const PreferenceFilter filter;

    // 企业标注：M-100 供应状态在产、系列未标注。
    std::vector<CandidateEnterpriseFacts> facts;
    CandidateEnterpriseFacts f100;
    f100.modelId = "M-100";
    f100.availability = "in-production";
    f100.series = "";
    facts.push_back(f100);
    const std::vector<FeasibilityRecord> records{
        makeRecord(DeviceKind::Motor, "M-100", VerdictKind::Feasible)};

    // 供应状态命中：通过、零原因。
    EnterprisePreference passAvail;
    passAvail.allowedAvailability = {"in-production"};
    const auto outPass = filter.apply(snap, records, facts, passAvail, provider);
    ASSERT_EQ(outPass.size(), 1u);
    EXPECT_TRUE(outPass[0].passesPreference);
    EXPECT_TRUE(outPass[0].preferenceReasons.empty());

    // 供应状态未命中：原因携带实际标注（actualText="in-production"）。
    EnterprisePreference missAvail;
    missAvail.allowedAvailability = {"discontinued"};
    const auto outMiss = filter.apply(snap, records, facts, missAvail, provider);
    ASSERT_EQ(outMiss.size(), 1u);
    ASSERT_EQ(outMiss[0].preferenceReasons.size(), 1u);
    EXPECT_EQ(outMiss[0].preferenceReasons[0].thresholdSource, kPrefDimAvailability);
    EXPECT_EQ(outMiss[0].preferenceReasons[0].actualText, "in-production");
    EXPECT_EQ(outMiss[0].preferenceReasons[0].requiredText, "discontinued");

    // 系列维度启用但候选未标注："(未标注)"哨兵——不默认通过（保守）。
    EnterprisePreference seriesOnly;
    seriesOnly.allowedSeries = {"RV"};
    const auto outSeries = filter.apply(snap, records, facts, seriesOnly, provider);
    ASSERT_EQ(outSeries.size(), 1u);
    EXPECT_FALSE(outSeries[0].passesPreference);
    ASSERT_EQ(outSeries[0].preferenceReasons.size(), 1u);
    EXPECT_EQ(outSeries[0].preferenceReasons[0].thresholdSource, kPrefDimSeries);
    EXPECT_EQ(outMiss[0].preferenceReasons[0].actualText, "in-production"); // 供应状态维度不受系列配置影响
    EXPECT_EQ(outSeries[0].preferenceReasons[0].actualText, kPrefAnnotationAbsent);
}

/**
 * 空偏好配置＝全维度不适用（零原因全通过）——"不启用不惩罚"的分界
 * 纪律（条件缺失≠数据缺失，T04 同款）。
 */
TEST(SelPreferenceFilter, EmptyPreferenceAppliesNothing)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-07"},
                  std::vector<std::string>{"AT-08"});
    const CatalogPackageSnapshot snap = makeBaselineSnapshot();
    const RejectionReasonProvider provider;
    const PreferenceFilter filter;
    const std::vector<FeasibilityRecord> records{
        makeRecord(DeviceKind::Motor, "M-100", VerdictKind::Feasible),
        makeRecord(DeviceKind::Motor, "M-200", VerdictKind::Feasible)};
    const std::vector<PreferenceFilterOutcome> out =
        filter.apply(snap, records, {}, EnterprisePreference{}, provider);
    ASSERT_EQ(out.size(), 2u);
    for (const PreferenceFilterOutcome& o : out) {
        EXPECT_TRUE(o.passesPreference);
        EXPECT_TRUE(o.preferenceReasons.empty());
        EXPECT_FALSE(o.preferredVendorHit);  // 白名单空＝维度不适用＝零标注
    }
}

// =====================================================================
// 分轨核心（acceptance 1"不改变硬能力判定"）
// =====================================================================

/**
 * ★ 核心验收（SEL-07"不改变硬能力判定"）：偏好过滤对硬筛选记录集零
 * 改写——apply 前后输入深等；偏好未过不改 verdict；偏好 token 绝不混入
 * FeasibilityRecord.reasons；"用户过滤后为空"时硬可行事实原样保留
 * （§10.2 空集语义表——偏好过滤结果与硬约束淘汰分开，非工程结论）。
 * 记录集经真实 HardConstraintSelector::screenMotors 产出（非直构）。
 */
TEST(SelPreferenceFilter, PreferenceNeverAltersHardVerdict)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-07"},
                  std::vector<std::string>{"AT-08"});
    const CatalogPackageSnapshot snap = makeBaselineSnapshot();
    const RejectionReasonProvider provider;
    const PreferenceFilter filter;
    const HardConstraintSelector selector;

    // 真实硬筛选：可行工作点 → 两电机全 Feasible（黄金判定）。
    const std::vector<AxisWorkpointFacts> facts{makeFeasibleFacts()};
    const ScreeningCriteria criteria;  // 默认条件（安全系数 1.0＝SF 维度不适用）
    std::vector<FeasibilityRecord> records =
        selector.screenMotors(snap, facts, criteria, nullptr);
    ASSERT_EQ(records.size(), 2u);     // 候选数×轴数＝2×1
    for (const FeasibilityRecord& r : records) {
        ASSERT_EQ(r.verdict, VerdictKind::Feasible);  // 前置：硬判定可行
    }

    // 全不命中的偏好配置（品牌/供应状态/系列三维度全启用且全不命中）
    // ——呈现层将看到"硬可行 2∧偏好保留 0"的偏好过滤空集。
    EnterprisePreference rejectAll;
    rejectAll.preferredVendors = {"OtherCo"};
    rejectAll.allowedAvailability = {"discontinued"};
    rejectAll.allowedSeries = {"RV"};

    const std::vector<FeasibilityRecord> before = records;  // 深拷贝基线
    const std::vector<PreferenceFilterOutcome> out =
        filter.apply(snap, records, {}, rejectAll, provider);

    // 断言 1：输入记录集零改写（深等——分轨纪律的执行面）。
    EXPECT_EQ(records, before);
    // 断言 2：硬判定原样——全记录仍 Feasible、无任何偏好 token 混入。
    for (const FeasibilityRecord& r : records) {
        EXPECT_EQ(r.verdict, VerdictKind::Feasible);
    }
    EXPECT_EQ(countToken(records, ReasonToken::UserPreferenceFiltered), 0);
    // 断言 3：偏好过滤结果分轨承载——全部候选偏好未过（"用户过滤后
    // 为空"），但这是偏好事实不是工程结论。
    ASSERT_EQ(out.size(), 2u);
    for (const PreferenceFilterOutcome& o : out) {
        EXPECT_FALSE(o.passesPreference);
        ASSERT_FALSE(o.preferenceReasons.empty());
        for (const RejectionReason& reason : o.preferenceReasons) {
            EXPECT_EQ(reason.token, ReasonToken::UserPreferenceFiltered);
        }
    }
}

/**
 * 非硬可行候选跳过偏好维度（§10.2 分层：硬判定已定——淘汰候选不再产
 * 生偏好原因；优选标注照算供呈现层排序，不区分硬判定）。
 */
TEST(SelPreferenceFilter, InfeasibleCandidatesSkipPreferenceReasons)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-07"},
                  std::vector<std::string>{"AT-08"});
    const CatalogPackageSnapshot snap = makeBaselineSnapshot();
    const RejectionReasonProvider provider;
    const PreferenceFilter filter;
    const HardConstraintSelector selector;

    // 超限工作点 → 两电机全 Rejected（连续转矩不足——硬淘汰）。
    const std::vector<AxisWorkpointFacts> facts{makeOverloadFacts()};
    const std::vector<FeasibilityRecord> records =
        selector.screenMotors(snap, facts, ScreeningCriteria{}, nullptr);
    ASSERT_EQ(records.size(), 2u);
    for (const FeasibilityRecord& r : records) {
        ASSERT_EQ(r.verdict, VerdictKind::Rejected);
        ASSERT_FALSE(r.reasons.empty());  // 硬淘汰原因齐备（黄金前提）
    }

    // 三维度全启用且全不命中——淘汰候选零偏好原因（偏好维度不适用），
    // 但优选标注照算（vendor=Sinotech ∈ {"Sinotech"}）。
    EnterprisePreference rejectAll;
    rejectAll.preferredVendors = {"Sinotech"};      // 命中（标注面照常）
    rejectAll.allowedAvailability = {"discontinued"};
    rejectAll.allowedSeries = {"RV"};
    const std::vector<PreferenceFilterOutcome> out =
        filter.apply(snap, records, {}, rejectAll, provider);
    ASSERT_EQ(out.size(), 2u);
    for (const PreferenceFilterOutcome& o : out) {
        EXPECT_TRUE(o.passesPreference);             // 偏好维度不适用＝通过
        EXPECT_TRUE(o.preferenceReasons.empty());    // 零偏好原因
        EXPECT_TRUE(o.preferredVendorHit);           // 标注面照算（呈现排序）
    }
}

// =====================================================================
// 确定性与契约违约
// =====================================================================

/**
 * 输出确定性（NFR-COR-02）：输出按 (deviceKind, modelId) 升序（电机在
 * 减速器前）；输入记录序无关——同输入乱序重排后两次调用结果全等。
 */
TEST(SelPreferenceFilter, DeterministicOrderingStableOutput)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-07"},
                  std::vector<std::string>{"NFR-COR-02"});
    const CatalogPackageSnapshot snap = makeBaselineSnapshot();
    const RejectionReasonProvider provider;
    const PreferenceFilter filter;

    EnterprisePreference pref;
    pref.preferredVendors = {"Nabtesco"};  // 减速器品牌命中、电机不命中

    // 输入 A：电机在前；输入 B：同记录逆序（跨类别乱序——减速器在先）。
    const std::vector<FeasibilityRecord> recordsA{
        makeRecord(DeviceKind::Motor, "M-100", VerdictKind::Feasible),
        makeRecord(DeviceKind::Motor, "M-200", VerdictKind::Feasible),
        makeRecord(DeviceKind::Gearbox, "G-50", VerdictKind::Feasible),
        makeRecord(DeviceKind::Gearbox, "G-120", VerdictKind::Feasible)};
    const std::vector<FeasibilityRecord> recordsB{recordsA.rbegin(), recordsA.rend()};

    const std::vector<PreferenceFilterOutcome> outA =
        filter.apply(snap, recordsA, {}, pref, provider);
    const std::vector<PreferenceFilterOutcome> outB =
        filter.apply(snap, recordsB, {}, pref, provider);

    // 断言 1：输入序无关——结果全等。
    EXPECT_EQ(outA, outB);
    // 断言 2：输出序＝(deviceKind, modelId) 升序（Motor<Gearbox 枚举序；
    // 同类别内 modelId 字节序——G-120 < G-50）。
    ASSERT_EQ(outA.size(), 4u);
    EXPECT_EQ(outA[0].modelId, "M-100");
    EXPECT_EQ(outA[0].deviceKind, DeviceKind::Motor);
    EXPECT_EQ(outA[1].modelId, "M-200");
    EXPECT_EQ(outA[1].deviceKind, DeviceKind::Motor);
    EXPECT_EQ(outA[2].modelId, "G-120");
    EXPECT_EQ(outA[2].deviceKind, DeviceKind::Gearbox);
    EXPECT_EQ(outA[3].modelId, "G-50");
    EXPECT_EQ(outA[3].deviceKind, DeviceKind::Gearbox);
    // 断言 3：品牌维度黄金面——电机零标注、减速器命中（Nabtesco）。
    EXPECT_FALSE(outA[0].preferredVendorHit);
    EXPECT_TRUE(outA[2].preferredVendorHit);
    EXPECT_TRUE(outA[3].preferredVendorHit);
}

/**
 * 契约违约 fail-fast（调用方错误——卡 §14.0 两分法）：①组合级记录混入；
 * ②记录候选不在快照；③企业标注 modelId 重复；④白名单含空串条目。
 */
TEST(SelPreferenceFilter, ContractViolationsFailFast)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-07"},
                  std::vector<std::string>{"NFR-COR-03"});
    const CatalogPackageSnapshot snap = makeBaselineSnapshot();
    const RejectionReasonProvider provider;
    const PreferenceFilter filter;

    // ①组合级记录（deviceKind==Combination）——偏好过滤是候选级。
    FeasibilityRecord combo = makeRecord(DeviceKind::Motor, "M-100", VerdictKind::Feasible);
    combo.deviceKind = DeviceKind::Combination;
    EXPECT_THROW(static_cast<void>(filter.apply(snap, {combo}, {},
                                                EnterprisePreference{}, provider)),
                 std::invalid_argument);

    // ②记录引用的候选不在快照（引用完整性——硬筛选产物必然来自快照）。
    const std::vector<FeasibilityRecord> ghost{
        makeRecord(DeviceKind::Motor, "M-XXX", VerdictKind::Feasible)};
    EXPECT_THROW(static_cast<void>(filter.apply(snap, ghost, {},
                                                EnterprisePreference{}, provider)),
                 std::invalid_argument);

    // ③企业标注 modelId 重复（同一候选两条矛盾标注）。
    CandidateEnterpriseFacts f1;
    f1.modelId = "M-100";
    f1.availability = "in-production";
    CandidateEnterpriseFacts f2 = f1;  // 同 modelId 第二条——矛盾
    f2.availability = "discontinued";
    const std::vector<FeasibilityRecord> ok{makeRecord(DeviceKind::Motor, "M-100", VerdictKind::Feasible)};
    EXPECT_THROW(static_cast<void>(filter.apply(snap, ok, {f1, f2},
                                                EnterprisePreference{}, provider)),
                 std::invalid_argument);

    // ④白名单含空串条目（空串＝未标注哨兵——语义歧义，配置边界拒绝）。
    EnterprisePreference badWhitelist;
    badWhitelist.allowedAvailability = {"in-production", ""};
    EXPECT_THROW(static_cast<void>(filter.apply(snap, ok, {}, badWhitelist, provider)),
                 std::invalid_argument);
}

/**
 * 接口路径消费证明（WP-20-T03"接口路径零覆盖"教训的钉扎面）：偏好
 * 原因必须经 IRejectionReasonProvider::make 接口构造——注入计数
 * Provider，断言调用次数＝未命中维度数、token 实参恒为分轨 token。
 */
TEST(SelPreferenceFilter, ReasonsBuiltViaProviderInterface)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-07"},
                  std::vector<std::string>{"ERR-01"});
    const CatalogPackageSnapshot snap = makeBaselineSnapshot();
    CountingReasonProvider counting;   // 计数注入——接口路径可观测
    const PreferenceFilter filter;

    // 单候选两维度未命中（品牌 OtherCo 不含 Sinotech＋供应状态未标注）
    // ——make 恰被调用 2 次，token 实参恒 UserPreferenceFiltered。
    EnterprisePreference pref;
    pref.preferredVendors = {"OtherCo"};
    pref.allowedAvailability = {"in-production"};
    const std::vector<FeasibilityRecord> records{
        makeRecord(DeviceKind::Motor, "M-100", VerdictKind::Feasible)};
    const std::vector<PreferenceFilterOutcome> out =
        filter.apply(snap, records, {}, pref, counting);

    EXPECT_EQ(counting.makeCalls, 2);  // 维度 A＋维度 B 各一次（系列未启用）
    EXPECT_EQ(counting.lastToken, ReasonToken::UserPreferenceFiltered);
    ASSERT_EQ(out.size(), 1u);
    ASSERT_EQ(out[0].preferenceReasons.size(), 2u);  // 两维度两原因（全量不短路）
}
