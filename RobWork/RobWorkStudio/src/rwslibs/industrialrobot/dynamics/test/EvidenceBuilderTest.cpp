/**
 * @file   EvidenceBuilderTest.cpp
 * @brief  WP-17-T06 dyn Profile 证据装配器用例组（DynEvidence）——任务契约
 *         acceptance 逐条的执行证明面：数据不足降级经 evidence 证据表达
 *         （V-09 摩擦缺失→必需项 Invalid＋DYN-FRICTION-MISSING；缺失项
 *         全量列入清单→evidence checkEvidenceCompleteness 判定④级素材）；
 *         不把估算结果包装成精确结论（V-10 估算限定语＋DYN-PROPERTY-
 *         DOWNGRADED）；限定语纪律、Profile 表 4 对账、fail-fast 契约面。
 *
 * 设计依据：
 *   - units/dynamics.md §5.5（三层降级）、§8.4（dyn Profile 六项/缺失全量）、
 *     §9.6（限定语词表）、§10.6（装配器契约）、§4.6（Empty/无时间参数不伪造）
 *   - units/evidence.md §6.1/§6.2/§6.4.1（Profile 注册/presence 纪律/决策表④
 *     ——必需项 Missing/Invalid/Unverified→DataInsufficient 的对账面）
 *   - 需求 DYN-06/MDL-16/MDL-05/CON-03/RPT-05/EVI-01
 *   - 数值对照口径：本组无数值断言（证据状态/词表/清单为离散面）——RNEA
 *     数值正确性由 DynRnea/DynStats 组承担，本组消费其真实产出作素材。
 */

#include <sdurws/ird/dynamics/EvidenceBuilder.hpp>

#include <sdurws/ird/dynamics/DiagCodes.hpp>
#include <sdurws/ird/dynamics/DynTypes.hpp>
#include <sdurws/ird/dynamics/Envelope.hpp>
#include <sdurws/ird/dynamics/Errors.hpp>
#include <sdurws/ird/dynamics/ForwardDynamics.hpp>
#include <sdurws/ird/dynamics/InverseDynamics.hpp>
#include <sdurws/ird/dynamics/PowerEnergy.hpp>
#include <sdurws/ird/dynamics/SeriesBuilder.hpp>
#include <sdurws/ird/evidence/Evidence.hpp>
#include <sdurws/ird/evidence/Evaluator.hpp>   // EvidenceProfileRegistry——真实注册面
#include <sdurws/ird/runtime/BaseWorldTransform.hpp>  // gravityToBase——重力投影规则单点
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "TwoLinkFixture.hpp"

using namespace sdurws::ird::dynamics::testfixture;

using sdurws::ird::core::DiagnosticRecord;
using sdurws::ird::core::ObjectId;
using sdurws::ird::dynamics::DynamicsEvidence;
using sdurws::ird::dynamics::DynamicsSample;
using sdurws::ird::dynamics::DynamicsSeries;
using sdurws::ird::dynamics::DynamicsSeriesBuilder;
using sdurws::ird::dynamics::DynamicsValidity;
using sdurws::ird::dynamics::DynamicsError;
using sdurws::ird::dynamics::EndEffectorPayload;
using sdurws::ird::dynamics::EvidenceGap;
using sdurws::ird::dynamics::ForwardCheckOutcome;
using sdurws::ird::dynamics::InverseDynOutcome;
using sdurws::ird::dynamics::InverseDynRequest;
using sdurws::ird::dynamics::InverseDynSampleInput;
using sdurws::ird::dynamics::InverseDynamicsEvaluator;
using sdurws::ird::dynamics::OperatingConditionResult;
using sdurws::ird::dynamics::PayloadEvent;
using sdurws::ird::dynamics::SeriesIdentity;
namespace evidence = sdurws::ird::evidence;

namespace {

// =====================================================================
// 宿主上下文本地替身（RneaTest 同款形状——只承载取消对话，不伪造评估）。
// =====================================================================

class FakeContext final : public sdurws::ird::evidence::IEvaluationContext {
public:
    bool cancellationRequested() const override { return false; }
    void reportProgress(std::uint8_t, std::string_view) override {}
    std::optional<std::vector<std::uint8_t>> tryObjectBytes(
        sdurws::ird::core::ObjectId, sdurws::ird::core::ContentVersion) const override
    {
        return std::nullopt;
    }
};

// =====================================================================
// 素材组装助手（真实评估链：RNEA→序列→统计→OperatingConditionResult——
// 证据装配的消费素材一律经产品公共链产出，不手搓伪造样本）。
// =====================================================================

/// 单样本激励（全关节同值列——简化算例；单位随关节类型）。
InverseDynSampleInput sampleAt(double t, const std::vector<double>& q,
                               const std::vector<double>& qd, const std::vector<double>& qdd)
{
    InverseDynSampleInput s;
    s.t = t;
    s.segmentIndex = 0;
    s.q = q;
    s.qd = qd;
    s.qdd = qdd;
    return s;
}

/// 序列身份块（确定性 id/摘要——夹具派生规则；planned＝激励时刻数）。
SeriesIdentity makeIdentity(const std::string& tag, std::size_t planned)
{
    SeriesIdentity id;
    id.snapshotId.bytes = digestOf(tag + "-snap");
    id.sliceId.bytes = digestOf(tag + "-slice");
    id.trajectoryPayloadId.bytes = digestOf(tag + "-trj");
    id.conditionId = idFrom<ObjectId>(tag + "-cond");
    id.toolObjectId = ObjectId{};
    id.dynConfigDigest = "sha256-" + tag;
    id.task.project = idFrom<sdurws::ird::core::ProjectId>(tag + "-prj");
    id.task.branch = idFrom<sdurws::ird::core::BranchId>(tag + "-brn");
    id.task.revision = idFrom<sdurws::ird::core::RevisionId>(tag + "-rev");
    id.task.run = idFrom<sdurws::ird::core::RunId>(tag + "-run");
    id.task.attempt.value = 1u;
    id.plannedSampleCount = planned;
    return id;
}

/// 真实评估链：模型＋激励→单工况结果（series/peaks/powerEnergy/validity
/// 同源装配——§4.4 OperatingConditionResult 的编排层形态）。
OperatingConditionResult evaluateCondition(const CanonicalModel& model, const std::string& tag,
                                           const std::vector<InverseDynSampleInput>& samples,
                                           const std::vector<EndEffectorPayload>& payloads = {},
                                           const std::vector<PayloadEvent>& events = {})
{
    InverseDynRequest req;
    req.conditionId = idFrom<ObjectId>(tag + "-cond");
    req.model = &model;
    const rw::math::Vector3D<double> gBase =
        sdurws::ird::runtime::gravityToBase(model.world().T_world_base.R(),
                                            model.world().gravityWorld);
    req.gravityBase[0] = gBase[0];
    req.gravityBase[1] = gBase[1];
    req.gravityBase[2] = gBase[2];
    req.samples = samples;
    req.payloads = payloads;
    req.events = events;

    FakeContext ctx;
    const InverseDynOutcome out = InverseDynamicsEvaluator().evaluate(req, ctx);

    // 序列冻结（validitySeed＝评估器来源事实透传——构建器只覆写派生字段
    // 〔completeness/计数〕，frictionMissing/估算计数等事实逐位保留）。
    SeriesIdentity id = makeIdentity(tag, samples.size());
    id.validitySeed = out.validity;
    DynamicsSeriesBuilder b;
    for (const DynamicsSample& row : out.samples) {
        b.addSample(row);
    }
    const DynamicsSeries series = b.finalize(id);

    // 统计装配（峰值/RMS＋功率能量——§10.4/§10.5 唯一实现点）。
    const sdurws::ird::dynamics::DynamicsEnvelopeCalculator env;
    const sdurws::ird::dynamics::PowerEnergyCalculator pe;
    OperatingConditionResult result;
    result.conditionId = series.conditionId;
    result.series = series;
    result.peaks = env.computePeaks(series);
    result.powerEnergy = pe.compute(series);
    result.validity = series.validity;
    return result;
}

/// 在证据行清单中按 itemId 取行（断言辅助——未命中返回 nullptr）。
const evidence::EvidenceItem* findItem(const std::vector<evidence::EvidenceItem>& items,
                                       std::string_view itemId)
{
    for (const evidence::EvidenceItem& item : items) {
        if (item.itemId == itemId) { return &item; }
    }
    return nullptr;
}

/// 在缺失清单中按 itemId 取条目（断言辅助——未命中返回 nullptr）。
const EvidenceGap* findGap(const std::vector<EvidenceGap>& gaps, std::string_view itemId)
{
    for (const EvidenceGap& g : gaps) {
        if (g.itemId == itemId) { return &g; }
    }
    return nullptr;
}

/// 限定语清单是否含给定 token。
bool hasQualifier(const std::vector<std::string_view>& qualifiers, std::string_view token)
{
    return std::find(qualifiers.begin(), qualifiers.end(), token) != qualifiers.end();
}

/// 证据行清单装配进 EvidenceManifest（对账 checkEvidenceCompleteness 用——
/// Profile 身份三元组以 dynProfile 的注册形态填充）。
evidence::EvidenceManifest makeManifest(const std::vector<evidence::EvidenceItem>& items,
                                        const DynamicsSeries& series)
{
    evidence::EvidenceManifest manifest;
    manifest.snapshotId = series.snapshotId;
    manifest.sliceId = series.sliceId;
    manifest.profileId = std::string{sdurws::ird::dynamics::kDynProfileId};
    manifest.profileVersion = std::string{sdurws::ird::dynamics::kDynProfileVersion};
    manifest.profileContentIdentity =
        evidence::computeProfileContentIdentity(sdurws::ird::dynamics::dynProfile());
    manifest.items = items;
    return manifest;
}

}  // namespace

// =====================================================================
// 用例 1：dynProfile 与 §8.4 表逐字段对账（EVI-01——Profile 明细归需求
// 表 4，域实例化不增删改写；注册期校验＋真实注册表＋内容身份确定性）。
// =====================================================================

TEST(DynEvidence, ProfileMatchesDynTableAndRegisters_WP17T06)
{
    IRD_TEST_INFO(std::vector<std::string>{"EVI-01", "DYN-06"}, std::vector<std::string>{});
    // 本用例验证：dynProfile() 恰含 §8.4 六项（required 四＋suggested 两）、
    // itemId 逐字一致、替代标志符合 §8.4 行语义（成功产物类＝true；来源
    // 标记/负载标识不可替代）；通过 evidence 注册期校验、可注册进真实
    // EvidenceProfileRegistry（§8.4"先于评估器注册"的装配面可用性）、
    // 内容身份对同内容确定（NFR-COR-02）。
    const evidence::RequiredEvidenceProfile profile =
        sdurws::ird::dynamics::dynProfile();

    EXPECT_EQ(profile.profileId, "dyn");
    EXPECT_EQ(profile.version, "1");
    ASSERT_EQ(profile.required.size(), 4u);
    ASSERT_EQ(profile.suggested.size(), 2u);

    // 行序＝§8.4 表行序（确定性——后续增项只允许表尾追加）。
    EXPECT_EQ(profile.required[0].itemId,
              std::string{sdurws::ird::dynamics::kDynItemJointSeries});
    EXPECT_EQ(profile.required[1].itemId,
              std::string{sdurws::ird::dynamics::kDynItemPeakRms});
    EXPECT_EQ(profile.required[2].itemId,
              std::string{sdurws::ird::dynamics::kDynItemProvenance});
    EXPECT_EQ(profile.required[3].itemId,
              std::string{sdurws::ird::dynamics::kDynItemLoadCondition});
    EXPECT_EQ(profile.suggested[0].itemId,
              std::string{sdurws::ird::dynamics::kDynItemPowerEnergy});
    EXPECT_EQ(profile.suggested[1].itemId,
              std::string{sdurws::ird::dynamics::kDynItemForwardCheck});

    // 替代标志（C2/C6）：①②⑤成功产物类可被不可行证明替代；③来源标记
    // （降级清单行）/④负载工况标识（可追溯性行）不豁免。
    EXPECT_TRUE(profile.required[0].substitutableByInfeasibility);
    EXPECT_TRUE(profile.required[1].substitutableByInfeasibility);
    EXPECT_FALSE(profile.required[2].substitutableByInfeasibility);
    EXPECT_FALSE(profile.required[3].substitutableByInfeasibility);
    // 说明非空（注册期校验强制——登记契约的一部分）。
    for (const evidence::EvidenceProfileItem& item : profile.required) {
        EXPECT_FALSE(item.description.empty()) << item.itemId;
    }

    // 注册期校验零问题＋内容身份确定性（同内容恒同摘要）。
    EXPECT_TRUE(evidence::validateEvidenceProfile(profile).empty());
    const sdurws::ird::core::ContentIdentity cid1 =
        evidence::computeProfileContentIdentity(profile);
    const sdurws::ird::core::ContentIdentity cid2 =
        evidence::computeProfileContentIdentity(sdurws::ird::dynamics::dynProfile());
    EXPECT_TRUE(cid1 == cid2);

    // 真实注册表注册（重复注册拒绝的注册表语义由 evidence 单元用例承担，
    // 此处验证 dyn Profile 实例化值可被注册面接受——装配链路可达）。
    evidence::EvidenceProfileRegistry registry;
    registry.registerProfile(sdurws::ird::dynamics::dynProfile());
    const evidence::RequiredEvidenceProfile* found =
        registry.findProfile("dyn", "1");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->required.size(), 4u);
    // 注册后 contentIdentity 被 evidence 覆写为计算值（域不可申报）。
    EXPECT_TRUE(found->contentIdentity == cid1);
}

// =====================================================================
// 用例 2：干净链全绿——四必需 Satisfied＋两建议 Satisfied、presence 纪律
 //   校验零问题、无限定语、无缺失（V-48 的对照面：精确结论只在全干净时）。
// =====================================================================

TEST(DynEvidence, CleanChainAllSatisfiedNoQualifier_WP17T06)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-06"}, std::vector<std::string>{});
    // 本用例验证：全部物性 UserProvided、摩擦三元组齐全的干净链端到端
    // （RNEA→序列→统计→装配）——四必需项 Satisfied、FD Passed→建议项
    // Satisfied、evidence presence 校验零 issue、限定语清单为空（无限定语
    // ＝可作精确结论呈现，不弱化也不添加）、缺失清单为空。
    const FrictionSpec fric{true, 0.5, 2.0, 0.1};  // fv/fc/bias 全 Provided
    const CanonicalModel model = makeTwoLinkModel(groundWorld(), fric, std::nullopt);
    const OperatingConditionResult result = evaluateCondition(
        model, "ev-clean",
        {sampleAt(0.0, {0.0, 0.0}, {0.0, 0.0}, {0.0, 0.0}),
         sampleAt(0.1, {0.0, 0.0}, {1.0, -2.0}, {0.0, 0.0}),
         sampleAt(0.2, {0.0, 0.0}, {1.0, -2.0}, {0.5, 0.5})});
    ASSERT_EQ(result.series.validity.completeness, DynamicsValidity::Completeness::Complete);
    EXPECT_FALSE(result.series.validity.frictionMissing);
    EXPECT_EQ(result.series.validity.estimatedLinkCount, 0u);

    sdurws::ird::dynamics::DynamicsEvidenceBuilder builder;
    builder.addSeries(result.series);
    builder.addConditionResult(result);
    ForwardCheckOutcome fd;
    fd.state = DynamicsValidity::ForwardCheckState::Passed;
    fd.comparedSamples = 2;
    builder.addForwardCheck(fd);
    const DynamicsEvidence ev = builder.build();

    const std::vector<evidence::EvidenceItem> items = builder.evidenceItems();
    ASSERT_EQ(items.size(), 6u);  // 六项状态行恒齐备（§10.6 后置）
    for (const evidence::EvidenceItem& item : items) {
        EXPECT_EQ(item.status, evidence::EvidenceItemStatus::Satisfied) << item.itemId;
        EXPECT_TRUE(item.artifactDigest.has_value()) << item.itemId;
    }
    // presence 纪律官方校验（Satisfied⇒摘要非零）零问题。
    EXPECT_TRUE(evidence::validateEvidenceItems(items).empty());
    // 对账 Profile：必需四项＋建议两项全满足——gaps 双清零。
    const evidence::EvidenceCompletenessResult completeness =
        evidence::checkEvidenceCompleteness(sdurws::ird::dynamics::dynProfile(),
                                            makeManifest(items, result.series));
    EXPECT_TRUE(completeness.requiredGaps.empty());
    EXPECT_TRUE(completeness.suggestedGaps.empty());
    // 限定语空＝精确结论（无降级事实）；缺失清单空。
    EXPECT_TRUE(builder.trustQualifiersCached().empty());
    EXPECT_TRUE(builder.missingList().empty());
    // 组织形态：四 refs 组非空、两建议可用位 true。
    EXPECT_EQ(ev.seriesRefs.size(), 1u);
    EXPECT_EQ(ev.statsRefs.size(), 1u);
    EXPECT_EQ(ev.provenanceRefs.size(), 1u);
    EXPECT_EQ(ev.loadConditionRefs.size(), 1u);
    EXPECT_TRUE(ev.powerEnergyAvailable);
    EXPECT_TRUE(ev.forwardCheckAvailable);
}

// =====================================================================
// 用例 3（V-09）：摩擦参数缺失→provenance 项 Invalid＋DYN-FRICTION-MISSING
//   →对账 Profile 必需项不满足（④级 DataInsufficient 素材）＋限定语
//   data-insufficient——降级经 evidence 证据表达（acceptance 1）。
// =====================================================================

TEST(DynEvidence, FrictionMissingDowngradesProvenanceItem_WP17T06)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-06", "MDL-16"}, std::vector<std::string>{"V-09"});
    // 本用例验证：摩擦三元组全缺模型（MDL-16 未填写）评估后——validity.
    // frictionMissing=true → dyn.property-friction-provenance 项 Invalid 且
    // invalidReason 稳定码＝DYN-FRICTION-MISSING（V-09 行
    // "property-friction-provenance=Invalid/降级标记"）；对账 Profile 得
    // requiredGaps 恰含该项（其余必需项不误伤——全量列出、不短路）；
    // 限定语含 data-insufficient；数值侧不受影响（样本行齐全）。
    const CanonicalModel model = makeTwoLinkModel(groundWorld(), FrictionSpec{}, std::nullopt);
    const OperatingConditionResult result = evaluateCondition(
        model, "ev-fric-missing",
        {sampleAt(0.0, {0.1, -0.2}, {0.3, 0.4}, {0.0, 0.0}),
         sampleAt(0.1, {0.1, -0.2}, {0.3, 0.4}, {1.0, -1.0})});
    ASSERT_TRUE(result.series.validity.frictionMissing);

    sdurws::ird::dynamics::DynamicsEvidenceBuilder builder;
    builder.addSeries(result.series);
    builder.addConditionResult(result);
    builder.build();

    const std::vector<evidence::EvidenceItem> items = builder.evidenceItems();
    // presence 纪律校验：Invalid⇒invalidReason 必填——官方校验零问题。
    EXPECT_TRUE(evidence::validateEvidenceItems(items).empty());

    const evidence::EvidenceItem* prov =
        findItem(items, sdurws::ird::dynamics::kDynItemProvenance);
    ASSERT_NE(prov, nullptr);
    EXPECT_EQ(prov->status, evidence::EvidenceItemStatus::Invalid);
    ASSERT_TRUE(prov->invalidReason.has_value());
    EXPECT_EQ(prov->invalidReason->code,
              std::string{sdurws::ird::dynamics::kDynFrictionMissing});
    // 摘要位与原因位互斥（Invalid 无产物摘要——presence 纪律）。
    EXPECT_FALSE(prov->artifactDigest.has_value());

    // 其余必需项不受摩擦缺失影响（缺失清单不误伤——①②④ Satisfied）。
    for (const std::string_view other :
         {sdurws::ird::dynamics::kDynItemJointSeries,
          sdurws::ird::dynamics::kDynItemPeakRms,
          sdurws::ird::dynamics::kDynItemLoadCondition}) {
        const evidence::EvidenceItem* item = findItem(items, other);
        ASSERT_NE(item, nullptr) << other;
        EXPECT_EQ(item->status, evidence::EvidenceItemStatus::Satisfied) << other;
    }

    // 对账 Profile（evidence 决策表④的执行面）：requiredGaps 恰含
    // provenance 项且状态＝Invalid——汇总层据此判 DataInsufficient。
    const evidence::EvidenceCompletenessResult completeness =
        evidence::checkEvidenceCompleteness(sdurws::ird::dynamics::dynProfile(),
                                            makeManifest(items, result.series));
    ASSERT_EQ(completeness.requiredGaps.size(), 1u);
    EXPECT_EQ(completeness.requiredGaps[0].itemId,
              std::string{sdurws::ird::dynamics::kDynItemProvenance});
    EXPECT_EQ(completeness.requiredGaps[0].status, evidence::EvidenceItemStatus::Invalid);

    // 限定语（RPT-05 冻结——数据不足限定语在场）＋缺失清单同步。
    EXPECT_TRUE(hasQualifier(builder.trustQualifiersCached(),
                             sdurws::ird::dynamics::kQualifierDataInsufficient));
    const std::vector<EvidenceGap> gaps3 = builder.missingList();
    const EvidenceGap* gap = findGap(gaps3, sdurws::ird::dynamics::kDynItemProvenance);
    ASSERT_NE(gap, nullptr);
    EXPECT_EQ(gap->status, evidence::EvidenceItemStatus::Invalid);
}

// =====================================================================
// 用例 4（V-10）：估算物性→estimated 限定语＋DYN-PROPERTY-DOWNGRADED——
//   估算结果不包装成精确结论（限定语纪律，acceptance 2）。
// =====================================================================

TEST(DynEvidence, EstimatedPropertyKeepsQualifier_WP17T06)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-06", "MDL-05"}, std::vector<std::string>{"V-10"});
    // 本用例验证：连杆物性来源＝GeometricEstimate（MDL-05）＋负载 com 缺失
    // （D-DYN-6 保守估算）——estimated 计数>0 → provenance 项 Invalid＋
    // invalidReason 稳定码＝DYN-PROPERTY-DOWNGRADED；限定语恰含 estimated
    // （不含 data-insufficient——三层互斥呈现：本例无摩擦缺失）；估算结果
    // 未被升级为精确（qualifiers 非空＋Invalid 态）。
    // 工具物性来源标 GeometricEstimate（夹具参数化注入位——MDL-05 估算
    // 来源的模型面）。
    const CanonicalModel estModel = makeTwoLinkModel(
        groundWorld(), FrictionSpec{true, 0.2, 1.0, 0.05}, ToolSpec{0.5, true, 0.0, 0.25, true});

    // 负载 com 缺失（SourcedValue 默认 NotProvided）→保守估算路径。
    EndEffectorPayload payload;
    payload.objectId = idFrom<ObjectId>("payload-est");
    payload.mass = val(0.8);  // kg（必填——Provided 且＞0）
    // com/inertia 缺省＝NotProvided（D-DYN-6：com→安装点、惯量→点质量）。

    const OperatingConditionResult result = evaluateCondition(
        estModel, "ev-estimated",
        {sampleAt(0.0, {0.0, 0.0}, {0.0, 0.0}, {0.0, 0.0}),
         sampleAt(0.1, {0.5, 0.5}, {1.0, 1.0}, {0.0, 0.0})},
        {payload});
    // 来源事实：工具物性 GeometricEstimate 计数（连杆估算 0——estModel 只
    // 标了工具；末端件估算 1——负载 com 缺失保守估算）。
    EXPECT_EQ(result.series.validity.estimatedLinkCount, 0u);
    EXPECT_EQ(result.series.validity.estimatedPayloadCount, 2u);  // 工具＋负载
    EXPECT_FALSE(result.series.validity.frictionMissing);

    sdurws::ird::dynamics::DynamicsEvidenceBuilder builder;
    builder.addSeries(result.series);
    builder.addConditionResult(result);
    builder.build();

    const std::vector<evidence::EvidenceItem> items4 = builder.evidenceItems();
    const evidence::EvidenceItem* prov =
        findItem(items4, sdurws::ird::dynamics::kDynItemProvenance);
    ASSERT_NE(prov, nullptr);
    EXPECT_EQ(prov->status, evidence::EvidenceItemStatus::Invalid);
    ASSERT_TRUE(prov->invalidReason.has_value());
    EXPECT_EQ(prov->invalidReason->code,
              std::string{sdurws::ird::dynamics::kDynPropertyDowngraded});

    // 限定语纪律：恰含 estimated、不含 data-insufficient（无摩擦缺失——
    // 三层互斥呈现不混用）；估算结果未被升级为精确。
    const std::vector<std::string_view> qualifiers = builder.trustQualifiersCached();
    EXPECT_TRUE(hasQualifier(qualifiers, sdurws::ird::dynamics::kQualifierEstimated));
    EXPECT_FALSE(hasQualifier(qualifiers,
                              sdurws::ird::dynamics::kQualifierDataInsufficient));
    EXPECT_FALSE(qualifiers.empty());
    // 缺失清单含 provenance 项（估算层 Invalid——④级素材）。
    const std::vector<EvidenceGap> gaps4 = builder.missingList();
    EXPECT_NE(findGap(gaps4, sdurws::ird::dynamics::kDynItemProvenance), nullptr);
}

// =====================================================================
// 用例 5：缺失清单全量列出、不短路（表 2 ④）——摩擦缺失＋无时间参数＋
//   检查未执行三面缺失同时在场，gaps 三条并存且互不挤占。
// =====================================================================

TEST(DynEvidence, MissingListExhaustiveNotShortCircuit_WP17T06)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-06"}, std::vector<std::string>{});
    // 本用例验证：三处不满足同时在场（③摩擦缺失 Invalid／⑤单样本→
    // timeParamAvailable=false→建议项 Missing／⑥检查未装配→Missing）——
    // missingList() 全量含三条（不因首个缺失短路），其余必需项 ①②④ 不
    // 入清单；限定语含 data-insufficient（③层）。
    const CanonicalModel model = makeTwoLinkModel(groundWorld(), FrictionSpec{}, std::nullopt);
    // 单样本序列：峰值可产出（单行取极值）、能量不可积分（有效行<2→
    // timeParamAvailable=false——§4.6 无时间参数语义）。
    const OperatingConditionResult result = evaluateCondition(
        model, "ev-multi-gap",
        {sampleAt(0.0, {0.2, -0.1}, {0.5, 0.5}, {0.0, 0.0})});
    ASSERT_TRUE(result.series.validity.frictionMissing);
    ASSERT_FALSE(result.powerEnergy.timeParamAvailable);
    ASSERT_FALSE(result.peaks.empty());  // 峰值在场（必需项②素材齐备）

    sdurws::ird::dynamics::DynamicsEvidenceBuilder builder;
    builder.addSeries(result.series);
    builder.addConditionResult(result);
    // 不调用 addForwardCheck——建议项⑥保持未装配（Missing）。
    builder.build();

    const std::vector<EvidenceGap> gaps = builder.missingList();
    ASSERT_EQ(gaps.size(), 3u);
    EXPECT_EQ(gaps[0].itemId, std::string{sdurws::ird::dynamics::kDynItemProvenance});
    EXPECT_EQ(gaps[0].status, evidence::EvidenceItemStatus::Invalid);
    EXPECT_EQ(gaps[1].itemId, std::string{sdurws::ird::dynamics::kDynItemPowerEnergy});
    EXPECT_EQ(gaps[1].status, evidence::EvidenceItemStatus::Missing);
    EXPECT_EQ(gaps[2].itemId, std::string{sdurws::ird::dynamics::kDynItemForwardCheck});
    EXPECT_EQ(gaps[2].status, evidence::EvidenceItemStatus::Missing);

    // 必需项①②④不受影响（行序稳定——§8.4 表行序）。
    EXPECT_TRUE(hasQualifier(builder.trustQualifiersCached(),
                             sdurws::ird::dynamics::kQualifierDataInsufficient));
}

// =====================================================================
// 用例 6：正动力学检查四态→证据状态映射（Passed→Satisfied／Failed→
//   Invalid＋DYN-FD-* 原因／NotApplicable→显式不计缺失／NotRun→Missing）。
// =====================================================================

TEST(DynEvidence, ForwardCheckStatusMapping_WP17T06)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-05", "DYN-06"}, std::vector<std::string>{});
    // 本用例验证：ForwardCheckOutcome 四态到证据项五态的映射契约——
    // Passed→Satisfied；Failed→Invalid（invalidReason＝检查器附带诊断首条
    // ——失败不能判定模型无效 §6.3.5）；NotApplicable（Skip）→NotApplicable
    // ＋原因必填且不入缺失清单（C2/EV-VER-7 不计缺失）；NotRun→Missing
    // （V-14 建议项缺失不阻断）。
    const CanonicalModel model = makeTwoLinkModel(
        groundWorld(), FrictionSpec{true, 0.1, 0.5, 0.02}, std::nullopt);
    const OperatingConditionResult result = evaluateCondition(
        model, "ev-fd-map",
        {sampleAt(0.0, {0.0, 0.0}, {0.0, 0.0}, {0.0, 0.0}),
         sampleAt(0.1, {0.1, 0.1}, {1.0, 1.0}, {0.0, 0.0})});

    // —— Passed → Satisfied ——
    {
        sdurws::ird::dynamics::DynamicsEvidenceBuilder b;
        b.addSeries(result.series);
        b.addConditionResult(result);
        ForwardCheckOutcome fd;
        fd.state = DynamicsValidity::ForwardCheckState::Passed;
        b.addForwardCheck(fd);
        b.build();
        const std::vector<evidence::EvidenceItem> items = b.evidenceItems();
        const evidence::EvidenceItem* item =
            findItem(items, sdurws::ird::dynamics::kDynItemForwardCheck);
        ASSERT_NE(item, nullptr);
        EXPECT_EQ(item->status, evidence::EvidenceItemStatus::Satisfied);
        EXPECT_TRUE(b.missingList().empty());
    }
    // —— Failed → Invalid＋首条诊断 ——
    {
        sdurws::ird::dynamics::DynamicsEvidenceBuilder b;
        b.addSeries(result.series);
        b.addConditionResult(result);
        ForwardCheckOutcome fd;
        fd.state = DynamicsValidity::ForwardCheckState::Failed;
        fd.diagnostics.push_back(DiagnosticRecord::make(
            std::string{sdurws::ird::dynamics::kDynFdConsistencyFailed}, result.conditionId,
            std::optional<std::string>{}, std::optional<std::string>{},
            std::string{"dynamics 正动力学一致性检查"}, std::string{"max|e_q| 超阈值（比较型素材）"},
            std::string{"扩大容差或排查模型/积分器配置"}));
        b.addForwardCheck(fd);
        b.build();
        const std::vector<evidence::EvidenceItem> items = b.evidenceItems();
        const evidence::EvidenceItem* item =
            findItem(items, sdurws::ird::dynamics::kDynItemForwardCheck);
        ASSERT_NE(item, nullptr);
        EXPECT_EQ(item->status, evidence::EvidenceItemStatus::Invalid);
        ASSERT_TRUE(item->invalidReason.has_value());
        EXPECT_EQ(item->invalidReason->code,
                  std::string{sdurws::ird::dynamics::kDynFdConsistencyFailed});
    }
    // —— NotApplicable（Skip）→ 显式不计缺失 ——
    {
        sdurws::ird::dynamics::DynamicsEvidenceBuilder b;
        b.addSeries(result.series);
        b.addConditionResult(result);
        ForwardCheckOutcome fd;
        fd.state = DynamicsValidity::ForwardCheckState::NotApplicable;
        b.addForwardCheck(fd);
        b.build();
        const std::vector<evidence::EvidenceItem> items = b.evidenceItems();
        const evidence::EvidenceItem* item =
            findItem(items, sdurws::ird::dynamics::kDynItemForwardCheck);
        ASSERT_NE(item, nullptr);
        EXPECT_EQ(item->status, evidence::EvidenceItemStatus::NotApplicable);
        ASSERT_TRUE(item->notApplicableReason.has_value());
        EXPECT_FALSE(item->notApplicableReason->empty());
        // 不计缺失：gaps 不含⑥（EV-VER-7 判定面）。
        const std::vector<EvidenceGap> gaps = b.missingList();
        EXPECT_EQ(findGap(gaps, sdurws::ird::dynamics::kDynItemForwardCheck), nullptr);
    }
    // —— NotRun → Missing（不阻断）——
    {
        sdurws::ird::dynamics::DynamicsEvidenceBuilder b;
        b.addSeries(result.series);
        b.addConditionResult(result);
        ForwardCheckOutcome fd;
        fd.state = DynamicsValidity::ForwardCheckState::NotRun;
        b.addForwardCheck(fd);
        b.build();
        const std::vector<evidence::EvidenceItem> items = b.evidenceItems();
        const evidence::EvidenceItem* item =
            findItem(items, sdurws::ird::dynamics::kDynItemForwardCheck);
        ASSERT_NE(item, nullptr);
        EXPECT_EQ(item->status, evidence::EvidenceItemStatus::Missing);
        const std::vector<EvidenceGap> gaps = b.missingList();
        EXPECT_NE(findGap(gaps, sdurws::ird::dynamics::kDynItemForwardCheck), nullptr);
    }
}

// =====================================================================
// 用例 7：fail-fast 契约面（重复注入/缺锚/工况不一致/身份块不完整）——
//       调用方错误轨（§10.0），不产出半成品证据。
// =====================================================================

TEST(DynEvidence, BuilderFailsFastContractViolations_WP17T06)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-06"}, std::vector<std::string>{});
    // 本用例验证：装配器调用方契约违约全部 DynamicsError fail-fast——
    // 重复 addSeries／未注入序列先注结果／conditionId 不一致／身份块缺失。
    const CanonicalModel model = makeTwoLinkModel(
        groundWorld(), FrictionSpec{true, 0.1, 0.5, 0.02}, std::nullopt);
    const OperatingConditionResult result = evaluateCondition(
        model, "ev-failfast",
        {sampleAt(0.0, {0.0, 0.0}, {0.0, 0.0}, {0.0, 0.0}),
         sampleAt(0.1, {0.1, 0.1}, {1.0, 1.0}, {0.0, 0.0})});

    // 重复注入序列。
    {
        sdurws::ird::dynamics::DynamicsEvidenceBuilder b;
        b.addSeries(result.series);
        EXPECT_THROW(b.addSeries(result.series), DynamicsError);
    }
    // 未注入序列先注工况结果（缺绑定锚）。
    {
        sdurws::ird::dynamics::DynamicsEvidenceBuilder b;
        EXPECT_THROW(b.addConditionResult(result), DynamicsError);
    }
    // 工况结果与序列的 conditionId 不一致（错误工况引用）。
    {
        OperatingConditionResult other = result;
        other.conditionId = idFrom<ObjectId>("ev-other-cond");
        sdurws::ird::dynamics::DynamicsEvidenceBuilder b;
        b.addSeries(result.series);
        EXPECT_THROW(b.addConditionResult(other), DynamicsError);
    }
    // 身份块不完整（dynConfigDigest 空串——缺身份拒绝装配）。注意：
    // finalize 的身份冻结校验在前（空摘要冻结即抛——构建器契约），故用
    // 合法身份冻结后手工破坏分量，专测装配面 addSeries 的防御校验
    // （§10.0"缺身份拒绝"对手工构造/跨版本序列的防线）。
    {
        SeriesIdentity id = makeIdentity("ev-bad-identity", 1);
        DynamicsSeriesBuilder sb;
        sb.addSample(result.series.samples[0]);
        DynamicsSeries broken = sb.finalize(id);
        broken.dynConfigDigest.clear();  // 测试注入：破坏身份分量（非正常产物形态）
        sdurws::ird::dynamics::DynamicsEvidenceBuilder b;
        EXPECT_THROW(b.addSeries(broken), DynamicsError);
    }
    // 重复注入正动力学检查产出。
    {
        sdurws::ird::dynamics::DynamicsEvidenceBuilder b;
        ForwardCheckOutcome fd;
        b.addForwardCheck(fd);
        EXPECT_THROW(b.addForwardCheck(fd), DynamicsError);
    }
}

// =====================================================================
// 用例 8：空序列与未 build 访问器（Empty 不做 0 值伪装＋无半成品逸出）。
// =====================================================================

TEST(DynEvidence, EmptySeriesMissingAndUnbuiltAccessors_WP17T06)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-06"}, std::vector<std::string>{});
    // 本用例验证：①空样本序列（上游取消/无激励产出）→必需项① Missing
    // （§4.6 Empty 绝不做 0 值伪装——不伪造无值序列的"证据"）；②build 前
    // evidenceItems()/missingList() 为空（不预生成半成品清单）；③未注入
    // 序列时限定语缓存为空。
    const CanonicalModel model = makeTwoLinkModel(
        groundWorld(), FrictionSpec{true, 0.1, 0.5, 0.02}, std::nullopt);
    // 空序列：直接经 SeriesBuilder 冻结零行（身份块齐全——合法 Empty 形态）。
    SeriesIdentity id = makeIdentity("ev-empty", 0);
    DynamicsSeriesBuilder sb;
    const DynamicsSeries empty = sb.finalize(id);
    ASSERT_TRUE(empty.samples.empty());

    sdurws::ird::dynamics::DynamicsEvidenceBuilder b;
    // build 前访问器为空（不预生成）。
    EXPECT_TRUE(b.evidenceItems().empty());
    EXPECT_TRUE(b.missingList().empty());
    EXPECT_TRUE(b.trustQualifiersCached().empty());

    b.addSeries(empty);
    b.build();
    const std::vector<evidence::EvidenceItem> items = b.evidenceItems();
    const evidence::EvidenceItem* seriesItem =
        findItem(items, sdurws::ird::dynamics::kDynItemJointSeries);
    ASSERT_NE(seriesItem, nullptr);
    EXPECT_EQ(seriesItem->status, evidence::EvidenceItemStatus::Missing);
    const std::vector<EvidenceGap> gaps = b.missingList();
    EXPECT_NE(findGap(gaps, sdurws::ird::dynamics::kDynItemJointSeries), nullptr);
}
