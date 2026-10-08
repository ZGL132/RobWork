/**
 * @file   CombinationCheckTest.cpp
 * @brief  组合校核核心用例组（SelCombinationCheck）——黄金三组合（可行/
 *         转速淘汰/转矩淘汰）、惯量比未判定不阻断（O-11）、参考阈值不
 *         产生淘汰（§11.3）、映射批缺失/失败/Partial 分轨、多工况资格
 *         矩阵、身份一致性、质量核算、调用方契约拒绝与确定性。
 *
 * 设计依据：
 *   - units/selection.md §9.3（校核清单全覆盖）、§9.4（资格矩阵）、
 *     §10.2（空集语义——映射失败≠候选淘汰）、§11.3（惯量比未裁决期
 *     保守行为）、§14.0（错误两分法）
 *   - 需求 SEL-05（经共享映射校核＋惯量比可配置规则）、DYN-04（消费）、
 *     EVI-02（必验工况全覆盖）、NFR-COR-01/02/03
 *   - 任务契约 tasks/foundation/WP-19-T05.json acceptance 1/2
 *
 * 黄金值口径：电机侧工作点为映射口径解析值（τ_m＝c·τ_joint、ω_m＝
 * ω_joint/c——黄金联动 drivetrain §6.2 口径）；每组合黄金 verdict 见
 * CombinationTestSupport.hpp 头注。核心经 CombinationCheckCoreInput 值
 * 面直调（模型测试直调——NFR-MNT-01；评估器适配面归 Evaluator 测试）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/selection/CatalogTypes.hpp>
#include <sdurws/ird/selection/Combination.hpp>
#include <sdurws/ird/selection/DiagCodes.hpp>   // kSelInputAxisOutOfScope——范围外缺口稳定码断言（WP-19-T08）
#include <sdurws/ird/selection/Screening.hpp>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO

#include "CombinationTestSupport.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace sdurws::ird::selection;
using namespace sdurws::ird::selection::testsupport;
using namespace sdurws::ird;

namespace {

/// 黄金三组合夹具构造（GoldenCombos 结构见支撑头——本展开口径产出
/// [K1(全A,G10), K2(A,G20), K3(B@J1,G10)]，组合序＝builder 字典序）。
inline GoldenCombos makeGoldenCombos()
{
    GoldenCombos g;
    g.axes = makeGoldenAxes();
    DeviceCombinationBuilder builder;
    const std::vector<AxisCandidateList> perAxis = {
        AxisCandidateList{g.axes.j1, {"M-A", "M-B"}, {"G-10", "G-20"}},
        AxisCandidateList{g.axes.j2, {"M-A"}, {"G-10", "G-20"}},
    };
    // 每批上限放大（本组测试不分批——批视图归 Builder 测试）。
    const CombinationSet set = builder.build(perAxis, goldenCompat(),
                                             BatchBudget{64}, goldenCatalog());
    g.combos = set.combinations;
    return g;
}

/// 黄金核心输入（快照＋轴事实＋映射批＋条件；惯量规则默认未配置）。
inline CombinationCheckCoreInput makeCoreInput(const CatalogPackageSnapshot& snapshot,
                                               const GoldenCombos& golden)
{
    CombinationCheckCoreInput in;
    in.snapshot = &snapshot;
    in.axisFacts = {
        makeJointFacts(golden.axes.j1, "case-A", true),
        makeJointFacts(golden.axes.j2, "case-A", false),
    };
    in.mappingBatch = goldenMappingBatch(golden.combos, golden.axes);
    in.criteria = goldenCriteria();
    return in;
}

/// 按组合序取产出（index 对位——黄金组合序＝builder 展开序）。
inline const CombinationCheckOutcome& at(
    const std::vector<CombinationCheckOutcome>& outcomes, std::size_t index)
{
    return outcomes.at(index);
}

}  // namespace

// ---------------------------------------------------------------------
// 黄金三组合（case-A 单工况）
// ---------------------------------------------------------------------

/// 黄金可行组合 K1：全部轴级维度通过＋惯量比未判定——Feasible 且
/// 未判定不阻断（§11.3：该维度不是表 4 必需项，Verified 不整体阻断）。
TEST(SelCombinationCheck, FeasibleCombinationWithUnsettledInertia)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-05"}, std::vector<std::string>{"AT-08"});  // R1——黄金可行组合——惯量比未判定显式标记且不整体阻断
    const GoldenCombos golden = makeGoldenCombos();
    CatalogPackageSnapshot snapshot = goldenSnapshot();
    const CombinationCheckCoreInput in = makeCoreInput(snapshot, golden);
    const std::vector<CombinationCheckOutcome> outcomes = checkCombinations(in, nullptr);
    ASSERT_EQ(outcomes.size(), golden.combos.size());
    const CombinationCheckOutcome& k1 = at(outcomes, 0);
    // 组合级记录形态（T05 落位细化：id＝组合键、deviceKind=Combination、
    // 无单候选/单轴、inputSliceId/mappingId＝直调全零诚实标记）。
    EXPECT_EQ(k1.record.id, golden.combos[0].id);
    EXPECT_EQ(k1.record.deviceKind, DeviceKind::Combination);
    EXPECT_EQ(k1.record.candidateModelId, ModelId{});
    EXPECT_EQ(k1.record.verdict, VerdictKind::Feasible);
    EXPECT_TRUE(k1.record.reasons.empty());
    EXPECT_TRUE(k1.record.gaps.empty());
    // 惯量比维度：规则未配置 ⇒ 显式未判定（不产生原因/缺口）。
    EXPECT_TRUE(k1.inertiaRatioUnsettled);
    // 资格矩阵：单工况一格 Pass＋note 携带词表未判定标注（呈现面）。
    ASSERT_EQ(k1.coverage.size(), std::size_t{1});
    EXPECT_EQ(k1.coverage[0].verdict, VerdictKind::Feasible);
    EXPECT_NE(k1.coverage[0].note.find("inertia-ratio-policy-unsettled"),
              std::string::npos);
    // 质量核算：Σ 各轴电机＋减速器质量＝(3+2)×2＝10.0 kg（黄金解析值）。
    EXPECT_DOUBLE_EQ(k1.totalMass, 10.0);
    // 组合键映射身份回填（直调＝映射批切片身份全零）。
    EXPECT_FALSE(k1.record.mappingId.isValid());
}

/// K2 组合（全 G-20 面，c＝0.02）：电机 ω_peak 500＞300 转速不足＋
/// 减速器输入转速超限——Rejected 且多原因并存（多原因不互相覆盖）。
TEST(SelCombinationCheck, RejectsOnSpeedDimensions)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-05"}, std::vector<std::string>{"AT-08"});  // R1——转速维度淘汰——电机 SpeedInsufficient＋减速器 InputSpeedExceeded
    const GoldenCombos golden = makeGoldenCombos();
    CatalogPackageSnapshot snapshot = goldenSnapshot();
    const CombinationCheckCoreInput in = makeCoreInput(snapshot, golden);
    const std::vector<CombinationCheckOutcome> outcomes = checkCombinations(in, nullptr);
    // 按轴面定位 K2（全 G-20——防字典序漂移的脆弱断言）。
    const DeviceCombination* k2Combo =
        findCombo(golden.combos, "M-A", "G-20", "M-A", "G-20");
    ASSERT_TRUE(k2Combo != nullptr);
    const CombinationCheckOutcome* k2 = findOutcome(outcomes, k2Combo->id);
    ASSERT_TRUE(k2 != nullptr);
    EXPECT_EQ(k2->record.verdict, VerdictKind::Rejected);
    // 逐因断言（token 存在＋比较字段齐备——ERR-01；峰值/额定两路任一
    // 命中即转速维度失败——T04 转速维 ω_peak>maxSpeed 或 ω_rms>ratedSpeed）。
    const bool hasSpeed = [&] {
        for (const RejectionReason& r : k2->record.reasons) {
            if (r.token == ReasonToken::SpeedInsufficient) {
                EXPECT_EQ(r.unit, "rad/s");
                EXPECT_EQ(r.caseId, "case-A");
                // J1 面映射口径：ω_m_peak＝10/0.02＝500（>maxSpeed 300）
                // 或 ω_m_rms＝200（>ratedSpeed 150）。
                EXPECT_TRUE((r.actual == 500.0 && r.required == 300.0)
                            || (r.actual == 200.0 && r.required == 150.0))
                    << "转速比较字段不符（actual=" << r.actual
                    << " required=" << r.required << "）";
                return true;
            }
        }
        return false;
    }();
    ASSERT_TRUE(hasSpeed) << "缺少电机转速不足原因";
    const bool hasInputSpeed = [&] {
        for (const RejectionReason& r : k2->record.reasons) {
            if (r.token == ReasonToken::InputSpeedExceeded) {
                EXPECT_NE(r.thresholdSource.find("max_input_speed"), std::string::npos);
                return true;
            }
        }
        return false;
    }();
    ASSERT_TRUE(hasInputSpeed) << "缺少减速器输入转速超限原因";
    // 格定位：Fail 格携带首条原因的工作点（§9.4 定位面）。
    ASSERT_EQ(k2->coverage.size(), std::size_t{1});
    EXPECT_EQ(k2->coverage[0].verdict, VerdictKind::Rejected);
    EXPECT_FALSE(k2->coverage[0].note.empty());
}

/// K3 组合（J1 面配 M-B）：连续转矩 4＞2 与峰值转矩 10＞8——Rejected
/// 且两原因稳定并存（同轴多维度独立判定——§10.2 不短路）。
TEST(SelCombinationCheck, RejectsOnMotorTorqueDimensions)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-05"}, std::vector<std::string>{"AT-08"});  // R1——转矩维度淘汰——TorqueContinuousInsufficient＋TorquePeakInsufficient
    const GoldenCombos golden = makeGoldenCombos();
    CatalogPackageSnapshot snapshot = goldenSnapshot();
    const CombinationCheckCoreInput in = makeCoreInput(snapshot, golden);
    const std::vector<CombinationCheckOutcome> outcomes = checkCombinations(in, nullptr);
    const DeviceCombination* k3Combo =
        findCombo(golden.combos, "M-B", "G-10", "M-A", "G-10");
    ASSERT_TRUE(k3Combo != nullptr);
    const CombinationCheckOutcome* k3 = findOutcome(outcomes, k3Combo->id);
    ASSERT_TRUE(k3 != nullptr);
    EXPECT_EQ(k3->record.verdict, VerdictKind::Rejected);
    bool hasContinuous = false;
    bool hasPeak = false;
    for (const RejectionReason& r : k3->record.reasons) {
        if (r.token == ReasonToken::TorqueContinuousInsufficient) {
            hasContinuous = true;
            EXPECT_DOUBLE_EQ(r.actual, 4.0);   // 映射口径 τ_m_rms＝0.1×40。
            EXPECT_DOUBLE_EQ(r.required, 2.0); // M-B 额定连续转矩。
            EXPECT_EQ(r.unit, "N*m");
        }
        if (r.token == ReasonToken::TorquePeakInsufficient) {
            hasPeak = true;
            EXPECT_DOUBLE_EQ(r.actual, 10.0);  // τ_m_peak＝0.1×100。
            EXPECT_DOUBLE_EQ(r.required, 8.0);
        }
    }
    EXPECT_TRUE(hasContinuous);
    EXPECT_TRUE(hasPeak);
    // 质量核算：K3＝(M-B 4 + G-10 2) + (M-A 3 + G-10 2)＝11.0 kg。
    EXPECT_DOUBLE_EQ(k3->totalMass, 11.0);
}

// ---------------------------------------------------------------------
// 惯量比维度（§11.3——可配置工程规则，未裁决期保守行为）
// ---------------------------------------------------------------------

/// 规则配置参考阈值：不超参考仍 Feasible；超参考【仍 Feasible】（参考
/// 语义不产生淘汰原因——词表无惯量比超限 token，P-SEL-4 未裁决），
/// 格 note 携带超参考标注。
TEST(SelCombinationCheck, ReferenceThresholdNeverRejects)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-05"}, std::vector<std::string>{"AT-08"});  // R1——惯量比参考阈值——可配置但不作为淘汰依据（P-SEL-4 未裁决期）
    const GoldenCombos golden = makeGoldenCombos();
    CatalogPackageSnapshot snapshot = goldenSnapshot();
    CombinationCheckCoreInput in = makeCoreInput(snapshot, golden);
    // 参考阈值 100（全部惯量比 3.0/75.0 之内）→ K2 仍因转速淘汰；
    // K1 仍可行（阈值不影响 verdict）。
    in.inertiaRule.referenceMaxRatio = 100.0;
    in.inertiaRule.source = "policy://engineering/inertia-ratio";
    const std::vector<CombinationCheckOutcome> withHigh =
        checkCombinations(in, nullptr);
    EXPECT_EQ(at(withHigh, 0).record.verdict, VerdictKind::Feasible);
    EXPECT_FALSE(at(withHigh, 0).inertiaRatioUnsettled);  // 已配置＝非未判定态。
    EXPECT_EQ(at(withHigh, 1).record.verdict, VerdictKind::Rejected);

    // 参考阈值 5.0：K2 的 J1 惯量比 75 超参考——格 note 标注出现，
    // 但组合 verdict 仍由轴级能力维度决定（Rejected——转速），不新增
    // 惯量比原因（词表封闭）。
    in.inertiaRule.referenceMaxRatio = 5.0;
    const std::vector<CombinationCheckOutcome> withLow =
        checkCombinations(in, nullptr);
    EXPECT_EQ(at(withLow, 1).record.verdict, VerdictKind::Rejected);
    bool hasInertiaToken = false;
    for (const RejectionReason& r : at(withLow, 1).record.reasons) {
        if (r.token == ReasonToken::InertiaRatioPolicyUnsettled) {
            hasInertiaToken = true;  // 未判定 token 不应作为淘汰原因出现。
        }
    }
    EXPECT_FALSE(hasInertiaToken);
    // K1 全轴惯量比 3.0 ≤ 5.0——仍可行且无超参考标注。
    EXPECT_EQ(at(withLow, 0).record.verdict, VerdictKind::Feasible);
}

/// 规则配置而映射事实缺失（组合×轴无惯量比事实）⇒ 数据缺口
/// （不默认通过——§7.2；组合转 DataInsufficient）。
TEST(SelCombinationCheck, ConfiguredRuleWithMissingFactYieldsGap)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-05"}, std::vector<std::string>{"AT-08"});  // R1——惯量比事实缺失——规则已配置即数据不足分轨
    const GoldenCombos golden = makeGoldenCombos();
    CatalogPackageSnapshot snapshot = goldenSnapshot();
    CombinationCheckCoreInput in = makeCoreInput(snapshot, golden);
    in.inertiaRule.referenceMaxRatio = 10.0;
    // 掏空 K1 的映射事实（组合×轴素材不全）。
    in.mappingBatch.axes.clear();
    const std::vector<CombinationCheckOutcome> outcomes = checkCombinations(in, nullptr);
    const CombinationCheckOutcome& k1 = at(outcomes, 0);
    EXPECT_EQ(k1.record.verdict, VerdictKind::DataInsufficient);
    bool hasInertiaGap = false;
    for (const DataGap& g : k1.record.gaps) {
        if (g.dimension == "inertia-ratio") {
            hasInertiaGap = true;
        }
    }
    EXPECT_TRUE(hasInertiaGap) << "规则已配置而事实缺失应产生 inertia-ratio 缺口";
}

// ---------------------------------------------------------------------
// 映射批分轨（§10.2 空集语义——映射失败≠候选淘汰）
// ---------------------------------------------------------------------

/// 映射批整体未供给（版本全零）⇒ drivetrain-mapping 缺口＋DataInsufficient
/// ——不伪装成候选淘汰（无任何 RejectionReason）。
TEST(SelCombinationCheck, MissingMappingBatchIsDataInsufficient)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-05"}, std::vector<std::string>{"AT-08"});  // R1——映射批未供给——上游缺口分轨（不伪装淘汰）
    const GoldenCombos golden = makeGoldenCombos();
    CatalogPackageSnapshot snapshot = goldenSnapshot();
    CombinationCheckCoreInput in = makeCoreInput(snapshot, golden);
    in.mappingBatch = MappingBatchFacts{};  // 全零版本＝未供给。
    const std::vector<CombinationCheckOutcome> outcomes = checkCombinations(in, nullptr);
    for (const CombinationCheckOutcome& o : outcomes) {
        EXPECT_EQ(o.record.verdict, VerdictKind::DataInsufficient);
        EXPECT_TRUE(o.record.reasons.empty()) << "上游缺口不得伪装成候选淘汰";
        bool hasMappingGap = false;
        for (const DataGap& g : o.record.gaps) {
            if (g.dimension == "drivetrain-mapping") {
                hasMappingGap = true;
            }
        }
        EXPECT_TRUE(hasMappingGap);
    }
}

/// 映射批整体失败（mappingFailed）⇒ 上游失败透传（缺口＋无淘汰原因
/// ——诊断码面经评估器通道透传，核心记缺口；上游失败＝电机侧事实
/// 不可用，故同时清空逐轴事实）。
TEST(SelCombinationCheck, MappingFailureIsTransparentNotRejection)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-05"}, std::vector<std::string>{"AT-08"});  // R1——映射批整体失败——上游诊断透传不伪装淘汰
    const GoldenCombos golden = makeGoldenCombos();
    CatalogPackageSnapshot snapshot = goldenSnapshot();
    CombinationCheckCoreInput in = makeCoreInput(snapshot, golden);
    // 单组合（K1）＋上游失败形态：无逐轴事实＋失败标志＋诊断码引用。
    const DeviceCombination* k1Combo =
        findCombo(golden.combos, "M-A", "G-10", "M-A", "G-10");
    ASSERT_TRUE(k1Combo != nullptr);
    in.mappingBatch = goldenMappingBatch({*k1Combo}, golden.axes);
    in.mappingBatch.mappingFailed = true;
    in.mappingBatch.diagnosticCodes = {"DT-INPUT-TIME-NONMONOTONIC"};
    in.mappingBatch.axes.clear();  // 上游失败＝电机侧事实不可用。
    const std::vector<CombinationCheckOutcome> outcomes = checkCombinations(in, nullptr);
    ASSERT_EQ(outcomes.size(), std::size_t{1});
    EXPECT_EQ(outcomes[0].record.verdict, VerdictKind::DataInsufficient);
    EXPECT_TRUE(outcomes[0].record.reasons.empty())
        << "上游失败不得伪装成候选淘汰";
}

/// 映射批 Partial ＋ 缺失清单 ⇒ 缺失项逐条转数据缺口（不伪造完整——
/// §9.3 行 12； Partial 纪律：缺失清单必非空）。
TEST(SelCombinationCheck, PartialMappingAddsGaps)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-05"}, std::vector<std::string>{"AT-08"});  // R1——映射批 Partial——缺失清单逐条转缺口
    const GoldenCombos golden = makeGoldenCombos();
    CatalogPackageSnapshot snapshot = goldenSnapshot();
    CombinationCheckCoreInput in = makeCoreInput(snapshot, golden);
    in.mappingBatch.completeness = CompletenessKind::Partial;
    in.mappingBatch.missingItems = {"efficiency[j=0]"};
    const std::vector<CombinationCheckOutcome> outcomes = checkCombinations(in, nullptr);
    const CombinationCheckOutcome& k1 = at(outcomes, 0);
    EXPECT_EQ(k1.record.verdict, VerdictKind::DataInsufficient);
    bool found = false;
    for (const DataGap& g : k1.record.gaps) {
        if (g.dimension == "drivetrain-mapping" && g.detail.find("efficiency[j=0]")
                != std::string::npos) {
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

/// Partial 而缺失清单为空＝调用方契约违约（完整性状态与素材矛盾）。
TEST(SelCombinationCheck, PartialWithoutMissingItemsRejected)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-05"}, std::vector<std::string>{"AT-08"});  // R1——Partial 纪律——缺失清单必非空
    const GoldenCombos golden = makeGoldenCombos();
    CatalogPackageSnapshot snapshot = goldenSnapshot();
    CombinationCheckCoreInput in = makeCoreInput(snapshot, golden);
    in.mappingBatch.completeness = CompletenessKind::Partial;
    EXPECT_THROW((void)checkCombinations(in, nullptr), std::invalid_argument);
}

// ---------------------------------------------------------------------
// 身份一致性（§9.3 行 10——目录版本；逐项定位不整批短路）
// ---------------------------------------------------------------------

/// 组合目录身份 ≠ 判定快照 ⇒ CatalogVersionIncompatible（逐组合原因；
/// 其余组合不受影响——逐项定位）。
TEST(SelCombinationCheck, CatalogVersionMismatchIsPerCombinationReason)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-05"}, std::vector<std::string>{"AT-08"});  // R1——目录版本一致性——组合所用目录≠判定快照逐项定位
    const GoldenCombos golden = makeGoldenCombos();
    CatalogPackageSnapshot snapshot = goldenSnapshot();
    CombinationCheckCoreInput in = makeCoreInput(snapshot, golden);
    // K1 的目录身份改为异版本（映射批组合表同步——身份错配面）。
    CatalogIdentity foreign = goldenCatalog();
    foreign.version = "v4";
    in.mappingBatch.combinations[0].catalog = foreign;
    const std::vector<CombinationCheckOutcome> outcomes = checkCombinations(in, nullptr);
    const CombinationCheckOutcome& k1 = at(outcomes, 0);
    EXPECT_EQ(k1.record.verdict, VerdictKind::Rejected);
    bool found = false;
    for (const RejectionReason& r : k1.record.reasons) {
        if (r.token == ReasonToken::CatalogVersionIncompatible) {
            found = true;
            EXPECT_NE(r.actualText.find("v4"), std::string::npos);
            EXPECT_NE(r.requiredText.find("v5"), std::string::npos);
        }
    }
    EXPECT_TRUE(found);
    // 其余组合不受影响（K2 仍因转速淘汰、非身份原因）。
    EXPECT_EQ(at(outcomes, 1).record.verdict, VerdictKind::Rejected);
    bool k2IdentityOnly = true;
    for (const RejectionReason& r : at(outcomes, 1).record.reasons) {
        if (r.token == ReasonToken::CatalogVersionIncompatible) {
            k2IdentityOnly = false;
        }
    }
    EXPECT_TRUE(k2IdentityOnly);
}

/// 组合漏轴（事实轴集 ⊄ 组合轴集）⇒ AxisMappingIncomplete。
TEST(SelCombinationCheck, AxisMappingIncompleteDetected)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-05"}, std::vector<std::string>{"AT-08"});  // R1——轴映射完整性——漏轴即原因
    const GoldenCombos golden = makeGoldenCombos();
    CatalogPackageSnapshot snapshot = goldenSnapshot();
    CombinationCheckCoreInput in = makeCoreInput(snapshot, golden);
    // K1 的轴表去掉 J2（漏轴面）。
    in.mappingBatch.combinations[0].axes.resize(1);
    const std::vector<CombinationCheckOutcome> outcomes = checkCombinations(in, nullptr);
    const CombinationCheckOutcome& k1 = at(outcomes, 0);
    EXPECT_EQ(k1.record.verdict, VerdictKind::Rejected);
    bool found = false;
    for (const RejectionReason& r : k1.record.reasons) {
        if (r.token == ReasonToken::AxisMappingIncomplete) {
            found = true;
            EXPECT_NE(r.actualText.find("缺轴 1 根"), std::string::npos);
        }
    }
    EXPECT_TRUE(found);
}

// ---------------------------------------------------------------------
// 多工况资格矩阵（§9.4——EVI-02 必验工况全覆盖）
// ---------------------------------------------------------------------

/// 双工况矩阵：case-A Pass＋case-E 急停峰值超限 → 组合 Rejected；
/// 覆盖矩阵两格分别 Pass/Fail，Fail 格定位携带轴/时刻/段（§9.4 定位面）。
TEST(SelCombinationCheck, CaseMatrixAggregatesAllRequiredCases)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-05"}, std::vector<std::string>{"EVI-02"});  // R1——多工况资格矩阵——全部必验工况通过方可行；不得漏验
    const GoldenCombos golden = makeGoldenCombos();
    CatalogPackageSnapshot snapshot = goldenSnapshot();
    CombinationCheckCoreInput in = makeCoreInput(snapshot, golden);
    // 追加 case-E（急停）：J1 关节峰值 500 N·m → K1 电机侧 τ_m_peak＝50
    // （c=0.1 解析）＞M-A 峰值 15 → PeakInsufficient。
    in.axisFacts.push_back(makeJointFacts(golden.axes.j1, "case-E", true));
    in.axisFacts.back().jointTorquePeak = 500.0;
    in.axisFacts.back().jointTorqueRms = 40.0;
    // K1 的 case-E 映射事实（τ_peak 50、ω 与 case-A 同——急停峰值面）。
    MappingAxisFact eStop = makeMappingFact(golden.combos[0].id, golden.axes.j1,
                                            "case-E", 0.1, true, 3.0, 1.0);
    eStop.motorTorquePeak = 50.0;   // N·m（急停黄金值——解析 0.1×500）
    eStop.motorTorqueRms = 4.0;
    in.mappingBatch.axes.push_back(eStop);
    // case-E 的组合表只挂 K1（其余组合该工况缺事实——覆盖缺口素材）。
    const std::vector<CombinationCheckOutcome> outcomes = checkCombinations(in, nullptr);
    const CombinationCheckOutcome& k1 = at(outcomes, 0);
    EXPECT_EQ(k1.record.verdict, VerdictKind::Rejected);
    // 覆盖矩阵：case-A Pass；case-E Fail（定位＝峰值原因的轴/时刻/段）。
    ASSERT_EQ(k1.coverage.size(), std::size_t{2});
    EXPECT_EQ(k1.coverage[0].caseId, "case-A");
    EXPECT_EQ(k1.coverage[0].verdict, VerdictKind::Feasible);
    EXPECT_EQ(k1.coverage[1].caseId, "case-E");
    EXPECT_EQ(k1.coverage[1].verdict, VerdictKind::Rejected);
    EXPECT_TRUE(k1.coverage[1].axisId == golden.axes.j1);
    EXPECT_DOUBLE_EQ(k1.coverage[1].atTime, 1.5);
    EXPECT_EQ(k1.coverage[1].segmentId, "seg-A");
    // 淘汰原因定位到急停工况（caseId=case-E 的 PeakInsufficient）。
    bool eStopPeak = false;
    for (const RejectionReason& r : k1.record.reasons) {
        if (r.token == ReasonToken::TorquePeakInsufficient && r.caseId == "case-E") {
            eStopPeak = true;
            EXPECT_DOUBLE_EQ(r.actual, 50.0);
        }
    }
    EXPECT_TRUE(eStopPeak);
    // 工况未执行（K2 无 case-E 事实）→ 覆盖缺口素材（workpoint-missing）。
    const CombinationCheckOutcome& k2 = at(outcomes, 1);
    bool hasCoverageGap = false;
    for (const DataGap& g : k2.record.gaps) {
        if (g.dimension == "workpoint-missing" && g.caseId == "case-E") {
            hasCoverageGap = true;
        }
    }
    EXPECT_TRUE(hasCoverageGap);
}

// ---------------------------------------------------------------------
// 效率降级维度（§9.3 行 7——不以 η＝1 静默替代）
// ---------------------------------------------------------------------

/// 映射事实效率不可用 ⇒ efficiency 缺口（DataInsufficient——不默认通过）。
TEST(SelCombinationCheck, EfficiencyDegradedYieldsGap)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-05"}, std::vector<std::string>{"AT-08"});  // R1——效率降级——映射侧 η 缺失分轨
    const GoldenCombos golden = makeGoldenCombos();
    CatalogPackageSnapshot snapshot = goldenSnapshot();
    CombinationCheckCoreInput in = makeCoreInput(snapshot, golden);
    // K1/J1 效率不可用（映射侧降级面）。
    for (MappingAxisFact& f : in.mappingBatch.axes) {
        if (f.combinationId == golden.combos[0].id && f.jointId == golden.axes.j1) {
            f.efficiencyApplied = false;
        }
    }
    const std::vector<CombinationCheckOutcome> outcomes = checkCombinations(in, nullptr);
    const CombinationCheckOutcome& k1 = at(outcomes, 0);
    EXPECT_EQ(k1.record.verdict, VerdictKind::DataInsufficient);
    bool found = false;
    for (const DataGap& g : k1.record.gaps) {
        if (g.dimension == "efficiency" && g.axisId == golden.axes.j1) {
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

// ---------------------------------------------------------------------
// 调用方契约拒绝（fail-fast）与确定性（NFR-COR-02/03）
// ---------------------------------------------------------------------

/// 空快照指针拒绝。
TEST(SelCombinationCheck, RejectsNullSnapshot)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-05"}, std::vector<std::string>{"AT-08"});  // R1——空快照指针——调用方契约违约
    CombinationCheckCoreInput in;
    in.snapshot = nullptr;
    EXPECT_THROW((void)checkCombinations(in, nullptr), std::invalid_argument);
}

/// 非有限筛选条件拒绝（安全系数 NaN——NFR-COR-03 校验边界快速拒绝）。
TEST(SelCombinationCheck, RejectsNonfiniteCriteria)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-03"}, std::vector<std::string>{"AT-08"});  // R1——非有限条件——校验边界拒绝
    const GoldenCombos golden = makeGoldenCombos();
    CatalogPackageSnapshot snapshot = goldenSnapshot();
    CombinationCheckCoreInput in = makeCoreInput(snapshot, golden);
    in.criteria.safetyFactor = std::numeric_limits<double>::quiet_NaN();
    EXPECT_THROW((void)checkCombinations(in, nullptr), std::invalid_argument);
}

/// 惯量比参考阈值 ≤0／非有限拒绝（供给即调用方契约）。
TEST(SelCombinationCheck, RejectsBadInertiaRule)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-05"}, std::vector<std::string>{"AT-08"});  // R1——惯量比阈值非法——不写死默认值前提下的供给校验
    const GoldenCombos golden = makeGoldenCombos();
    CatalogPackageSnapshot snapshot = goldenSnapshot();
    CombinationCheckCoreInput in = makeCoreInput(snapshot, golden);
    in.inertiaRule.referenceMaxRatio = -1.0;
    EXPECT_THROW((void)checkCombinations(in, nullptr), std::invalid_argument);
    in.inertiaRule.referenceMaxRatio =
        std::numeric_limits<double>::quiet_NaN();
    EXPECT_THROW((void)checkCombinations(in, nullptr), std::invalid_argument);
}

/// 映射批版本部分供给（契约版 0/算法版＞0）拒绝；全零＝未供给不抛。
TEST(SelCombinationCheck, MappingBatchVersionSemantics)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-05"}, std::vector<std::string>{"AT-08"});  // R1——映射批版本字段——0 未登记非法/全零＝未供给
    const GoldenCombos golden = makeGoldenCombos();
    CatalogPackageSnapshot snapshot = goldenSnapshot();
    CombinationCheckCoreInput in = makeCoreInput(snapshot, golden);
    in.mappingBatch.mappingContractVersion = 0;   // 部分供给——非法。
    in.mappingBatch.mappingAlgorithmVersion = 1;
    EXPECT_THROW((void)checkCombinations(in, nullptr), std::invalid_argument);
    in.mappingBatch.mappingAlgorithmVersion = 0;  // 全零＝未供给——不抛。
    in.mappingBatch.combinations.clear();         // 无组合可校核——空产出。
    const std::vector<CombinationCheckOutcome> outcomes = checkCombinations(in, nullptr);
    EXPECT_TRUE(outcomes.empty());
}

/// 同输入两次调用结果逐字段相等（纯函数确定性——NFR-COR-02）。
TEST(SelCombinationCheck, DeterministicForSameInput)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-02"}, std::vector<std::string>{"AT-08"});  // R1——确定性——同输入等价输出
    const GoldenCombos golden = makeGoldenCombos();
    CatalogPackageSnapshot snapshot = goldenSnapshot();
    const CombinationCheckCoreInput in = makeCoreInput(snapshot, golden);
    const std::vector<CombinationCheckOutcome> first = checkCombinations(in, nullptr);
    const std::vector<CombinationCheckOutcome> second = checkCombinations(in, nullptr);
    ASSERT_EQ(first.size(), second.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        EXPECT_EQ(first[i].record, second[i].record);
        EXPECT_EQ(first[i].coverage, second[i].coverage);
        EXPECT_EQ(first[i].totalMass, second[i].totalMass);
        EXPECT_EQ(first[i].inertiaRatioUnsettled, second[i].inertiaRatioUnsettled);
    }
}

// ---------------------------------------------------------------------
// 移动关节范围外阻断（WP-19-T08——SEL-09/卡 §2.2 R1 纪律/D-SEL-15）
// ---------------------------------------------------------------------

/// 含移动关节轴的组合被阻断（链级范围外）：J2 声明 Prismatic → 黄金
/// 可行组合 K1（全 M-A/G-10 面）转 DataInsufficient（非 Feasible——格
/// 不得判 Pass；非 Rejected——不升级整机不可行）、零 RejectionReason
/// （不静默套用旋转传动：J2 的映射事实即使供给也不消费）＋恰一条 J2
/// 范围外缺口（稳定码 SEL-INPUT-AXIS-OUT-OF-SCOPE、caseId 空）；含旋转
/// 超限轴的 K2/K3 保持既有 Rejected（范围外不覆盖淘汰原因），且其全部
/// 原因定位在旋转轴 J1（移动轴 J2 零旋转原因）。
TEST(SelCombinationCheck, PrismaticCombinationBlockedAtCombinationLevel)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-09", "MDL-12"}, std::vector<std::string>{"AT-08", "AT-17"});  // R1——含移动关节组合链级阻断——DataInsufficient＋SEL-INPUT-AXIS-OUT-OF-SCOPE
    const GoldenCombos golden = makeGoldenCombos();
    CatalogPackageSnapshot snapshot = goldenSnapshot();
    CombinationCheckCoreInput in = makeCoreInput(snapshot, golden);
    // J2 声明移动关节（黄金组合全含 J2——三组合全部携带范围外缺口）。
    for (AxisWorkpointFacts& f : in.axisFacts) {
        if (f.jointId == golden.axes.j2) {
            f.jointKind = JointKind::Prismatic;
        }
    }
    const std::vector<CombinationCheckOutcome> outcomes = checkCombinations(in, nullptr);
    ASSERT_EQ(outcomes.size(), golden.combos.size());

    // ---- K1（全 M-A/G-10——无旋转超限的基准面）：完整阻断形态。
    const DeviceCombination* k1Combo =
        findCombo(golden.combos, "M-A", "G-10", "M-A", "G-10");
    ASSERT_TRUE(k1Combo != nullptr);
    const CombinationCheckOutcome* k1 = findOutcome(outcomes, k1Combo->id);
    ASSERT_TRUE(k1 != nullptr);
    EXPECT_EQ(k1->record.verdict, VerdictKind::DataInsufficient)
        << "含移动关节轴的组合不得判可行（SEL-09 链级阻断）";
    // 零淘汰原因（旋转传动维度对 J2 未执行；J1 黄金可行也无原因）。
    EXPECT_TRUE(k1->record.reasons.empty())
        << "移动关节轴不得产生任何旋转传动淘汰原因（SEL-09）";
    // 恰一条 J2 范围外缺口（逐轴恰一条、与工况无关）。
    ASSERT_EQ(k1->record.gaps.size(), std::size_t{1});
    EXPECT_EQ(k1->record.gaps[0].dimension, "axis-out-of-scope");
    EXPECT_EQ(k1->record.gaps[0].diagCode, std::string(kSelInputAxisOutOfScope));
    EXPECT_EQ(k1->record.gaps[0].axisId, golden.axes.j2);
    EXPECT_TRUE(k1->record.gaps[0].caseId.empty());
    // 资格矩阵格：非 Pass＋范围外标注（格聚合对该轴无记录可依——兜底）。
    ASSERT_EQ(k1->coverage.size(), std::size_t{1});
    EXPECT_EQ(k1->coverage[0].verdict, VerdictKind::DataInsufficient);
    EXPECT_NE(k1->coverage[0].note.find("axis-out-of-scope"), std::string::npos);

    // ---- K2（J1 面 M-A/G-20——旋转超限组合）：保持既有 Rejected；
    // 全部原因定位 J1（移动轴零原因——不套用）；范围外缺口并存。
    {
        const DeviceCombination* k2Combo =
            findCombo(golden.combos, "M-A", "G-20", "M-A", "G-10");
        ASSERT_TRUE(k2Combo != nullptr);
        const CombinationCheckOutcome* k2 = findOutcome(outcomes, k2Combo->id);
        ASSERT_TRUE(k2 != nullptr);
        EXPECT_EQ(k2->record.verdict, VerdictKind::Rejected)
            << "旋转轴既有淘汰保持原样（范围外不覆盖判定）";
        ASSERT_FALSE(k2->record.reasons.empty());
        for (const RejectionReason& r : k2->record.reasons) {
            EXPECT_EQ(r.axisId, golden.axes.j1)
                << "全部淘汰原因定位在旋转轴 J1（移动轴 J2 零原因）";
        }
        bool hasOutOfScopeGap = false;
        for (const DataGap& g : k2->record.gaps) {
            if (g.dimension == "axis-out-of-scope") {
                hasOutOfScopeGap = true;
                EXPECT_EQ(g.axisId, golden.axes.j2);
            }
        }
        EXPECT_TRUE(hasOutOfScopeGap) << "范围外缺口并存（多事实不互相覆盖）";
    }
}

/// 混合链（J1 旋转超限＋J2 移动）：旋转轴的候选能力淘汰保持原样
/// （Rejected 优先——范围外是数据/边界类事实，不覆盖既有淘汰原因），
/// 组合级同时携带旋转轴淘汰原因与 J2 范围外缺口（多事实并存——§10.3
/// "原因不被单一状态字段覆盖"）。
TEST(SelCombinationCheck, MixedChainKeepsRotatingAxisRejection)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-09", "MDL-12"}, std::vector<std::string>{"AT-08"});  // R1——混合链：旋转轴淘汰与移动轴范围外并存——不互相覆盖
    const GoldenCombos golden = makeGoldenCombos();
    CatalogPackageSnapshot snapshot = goldenSnapshot();
    CombinationCheckCoreInput in = makeCoreInput(snapshot, golden);
    // J1 换用 M-B 面（连续/峰值转矩超限——黄金 K3 组合的淘汰面）：
    // 用映射批全量黄金事实（M-B 超限值在 goldenMappingBatch 内），此处
    // 只把 J2 声明为移动关节。
    for (AxisWorkpointFacts& f : in.axisFacts) {
        if (f.jointId == golden.axes.j2) {
            f.jointKind = JointKind::Prismatic;
        }
    }
    const std::vector<CombinationCheckOutcome> outcomes = checkCombinations(in, nullptr);
    const DeviceCombination* k3Combo =
        findCombo(golden.combos, "M-B", "G-10", "M-A", "G-10");
    ASSERT_TRUE(k3Combo != nullptr);
    const CombinationCheckOutcome* k3 = findOutcome(outcomes, k3Combo->id);
    ASSERT_TRUE(k3 != nullptr);
    // J1（旋转）超限 → 组合仍 Rejected（旋转轴淘汰优先——格 Rejected
    // 保持原样，范围外不覆盖判定）。
    EXPECT_EQ(k3->record.verdict, VerdictKind::Rejected);
    bool hasTorqueReason = false;
    bool hasOutOfScopeGap = false;
    for (const RejectionReason& r : k3->record.reasons) {
        if (r.token == ReasonToken::TorquePeakInsufficient
            || r.token == ReasonToken::TorqueContinuousInsufficient) {
            hasTorqueReason = true;
            EXPECT_EQ(r.axisId, golden.axes.j1);  // 原因定位在旋转轴 J1。
        }
    }
    for (const DataGap& g : k3->record.gaps) {
        if (g.dimension == "axis-out-of-scope") {
            hasOutOfScopeGap = true;
            EXPECT_EQ(g.axisId, golden.axes.j2);
            EXPECT_EQ(g.diagCode, std::string(kSelInputAxisOutOfScope));
        }
    }
    EXPECT_TRUE(hasTorqueReason) << "旋转轴淘汰原因保持原样";
    EXPECT_TRUE(hasOutOfScopeGap) << "移动轴范围外缺口并存";
}

/// 同轴 jointKind 矛盾在组合校核入口 fail-fast（物理矛盾——调用方契约
/// 违约；显式校验的原因：全移动链不触发 T04 筛选器，矛盾须在入口拦截）。
TEST(SelCombinationCheck, JointKindConflictFailsFastAtCore)
{
    IRD_TEST_INFO(std::vector<std::string>{"SEL-09"}, std::vector<std::string>{"AT-08"});  // R1——同轴关节类型矛盾＝组合校核入口契约违约
    const GoldenCombos golden = makeGoldenCombos();
    CatalogPackageSnapshot snapshot = goldenSnapshot();
    CombinationCheckCoreInput in = makeCoreInput(snapshot, golden);
    // 同轴（J2）两条工况事实声明不同关节类型——物理矛盾。
    AxisWorkpointFacts conflicting = makeJointFacts(golden.axes.j2, "case-B", false);
    conflicting.jointKind = JointKind::Prismatic;
    in.axisFacts.push_back(conflicting);
    EXPECT_THROW((void)checkCombinations(in, nullptr), std::invalid_argument);
}
