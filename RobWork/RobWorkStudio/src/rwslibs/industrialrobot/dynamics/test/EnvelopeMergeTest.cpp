/**
 * @file   EnvelopeMergeTest.cpp
 * @brief  WP-17-T07 用例组——多工况与包络合并（DynEnvelopeMerge）：多负载
 *         工况/急停-保持类设计工况经统一 RNEA 评估后的包络合并黄金算例、
 *         跨工况逐量纲 max 合并与来源工况归因（含并列峰值的 conditionId
 *         序确定性）、入参顺序无关性、RMS 跨工况 max 与 NaN 不参与、
 *         envelopeComplete 完整性传播、coversAllMandatory 覆盖分母口径
 *         （EVI-02：分母＝快照冻结 RequiredCaseSet；包络合并不替代必验
 *         工况覆盖规则）、Empty 语义与 fail-fast 装配契约（任务契约
 *         WP-17-T07 acceptance 逐条的执行证明面，对应 V-24～V-27）。
 *
 * 设计依据：
 *   - units/dynamics.md §7.4（多工况包络——合并规则/来源工况/完整性传播/
 *     Empty 不产出）、§8.1（工况消费与覆盖矩阵——EVI-02 五态、包络不替代
 *     矩阵）、§8.3（多工况覆盖和包络统计图）、§7.3（RMS 口径）、§7.6
 *     （统计完整性）、§10.4（mergeEnvelope 契约表）、§4.4（DynamicsEnvelope
 *     类型）
 *   - 需求 DYN-07（多负载工况、急停/保持等设计工况和结果包络合并；包络
 *     合并不替代必验工况覆盖规则）、EVI-02（正式计算与判定必须覆盖全部
 *     启用必验工况）、AT-07（多工况不漏验）；P-DYN-4（急停/保持模板结构
 *     未冻结——按统一 RNEA 评估给定工况，不自行定义构造语义）
 *   - 决策 D-DYN-9（RMS 时间加权）、NFR-COR-02（确定性/稳定排序）、
 *     NFR-COR-03（非有限/空集不伪装）
 *
 * 数值对照口径（附录 D C7）：手工黄金序列的期望值逐用例按公式手算（梯形
 *   展开/常数序列解析），断言容差为相对 1e-12（expectNearRel）；RNEA 端到
 *   端算例用 1e-9（T03 解析算例同口径）；无任何容差进产品代码。
 */

#include <sdurws/ird/dynamics/DynTypes.hpp>
#include <sdurws/ird/dynamics/Envelope.hpp>
#include <sdurws/ird/dynamics/Errors.hpp>
#include <sdurws/ird/dynamics/InverseDynamics.hpp>
#include <sdurws/ird/dynamics/PowerEnergy.hpp>
#include <sdurws/ird/dynamics/SeriesBuilder.hpp>
#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/evidence/Snapshot.hpp>           // evidence::RequiredCaseSet
                                                     //   （覆盖分母口径）
#include <sdurws/ird/runtime/CanonicalModel.hpp>     // RNEA 端到端用例模型面
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp> // IRD_TEST_INFO——需求/AT 追溯

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "TwoLinkFixture.hpp"

using namespace sdurws::ird::dynamics::testfixture;

using sdurws::ird::core::ContentDigester;
using sdurws::ird::core::Digest256;
using sdurws::ird::core::ObjectId;
using sdurws::ird::dynamics::DynamicsEnvelope;
using sdurws::ird::dynamics::DynamicsEnvelopeCalculator;
using sdurws::ird::dynamics::DynamicsError;
using sdurws::ird::dynamics::DynamicsSample;
using sdurws::ird::dynamics::DynamicsSeries;
using sdurws::ird::dynamics::DynamicsSeriesBuilder;
using sdurws::ird::dynamics::DynamicsValidity;
using sdurws::ird::dynamics::DynJointType;
using sdurws::ird::dynamics::InverseDynOutcome;
using sdurws::ird::dynamics::InverseDynRequest;
using sdurws::ird::dynamics::InverseDynSampleInput;
using sdurws::ird::dynamics::InverseDynamicsEvaluator;
using sdurws::ird::dynamics::OperatingConditionResult;
using sdurws::ird::dynamics::PeakRecord;
using sdurws::ird::dynamics::PowerEnergyCalculator;
using sdurws::ird::dynamics::SampleNumericState;
using sdurws::ird::dynamics::SeriesIdentity;
using sdurws::ird::evidence::CaseEntry;
using sdurws::ird::evidence::RequiredCaseSet;

namespace {

// =====================================================================
// 本地助手（确定性 id/摘要直接复用夹具 testfixture::digestOf/idFrom——
// using-directive 已引入；不另写第二份派生规则，防两套种子漂移）。
// =====================================================================

/// 端到端用例的取消/进度宿主面（evidence 卡口径本地替身——同 RneaTest
/// 形态：替身只承载对话形状，不伪造评估逻辑）。
class FakeContextLike final : public sdurws::ird::evidence::IEvaluationContext {
public:
    bool cancellationRequested() const override { return false; }
    void reportProgress(std::uint8_t, std::string_view) override {}
    std::optional<std::vector<std::uint8_t>> tryObjectBytes(
        sdurws::ird::core::ObjectId, sdurws::ird::core::ContentVersion) const override
    {
        return std::nullopt;
    }
};

/// 黄金对照相对容差（本文件声明——测试对照口径，附录 D C7 分离）。
constexpr double kGoldenRelTol = 1e-12;

/// 相对容差断言（期望为 0 时退化为绝对 1e-15——避免零除）。
void expectNearRel(double actual, double expected, const char* what)
{
    const double tol = std::max(1e-15, kGoldenRelTol * std::abs(expected));
    EXPECT_NEAR(actual, expected, tol) << what << "（实得 " << actual << "，期望 " << expected
                                       << "）";
}

/// 黄金样本行构造助手（分项字段按"统计器只消费 τ_total/功率/速度/加速度"
/// 的事实填充自洽值；工况 id 逐序列给定——包络合并的归因键）。
DynamicsSample rowAt(const ObjectId& cond, double t, std::uint32_t segment, double qd,
                     double qdd, double tauTotal)
{
    DynamicsSample r;
    r.t = t;                                        // s
    r.segmentIndex = segment;                       // 所在轨迹段（0 基）
    r.conditionId = cond;                           // 工况对象 ID（EVI-02 关联键）
    r.jointIndex = 0;                               // 单关节黄金序列
    r.jointObjectId = idFrom<ObjectId>("mrg-joint");// 关节稳定对象 ID
    r.jointType = DynJointType::Revolute;           // 转动——力矩量纲 N·m
    r.q = 0.0;                                      // rad（黄金序列不消费位置）
    r.qd = qd;                                      // rad/s
    r.qdd = qdd;                                    // rad/s²
    r.tauGravity = 0.0;                             // 黄金行不做分项拆解（五分项
    r.tauInertia = 0.0;                             //   恒等式由 0 值平凡成立——与
    r.tauCoriolisCentrifugal = 0.0;                 //   构建器/统计器"不校验恒等
    r.tauFriction = 0.0;                            //   式"的分层一致）
    r.tauExternal = 0.0;                            // R1 恒 0（P-DYN-3）
    r.tauTotal = tauTotal;                          // N·m
    r.mechanicalPower = tauTotal * qd;              // W（=τ·q̇——构造方保证）
    r.energyIntegralJ = 0.0;                        // J（包络合并不消费）
    r.payloadVariantIndex = 0;                      // 基线变体
    r.toolObjectId = ObjectId{};                    // 无工具模型＝空 id
    r.numericState = SampleNumericState::Ok;
    return r;
}

/// 序列身份块构造助手（确定性 id/摘要；planned 由调用方按场景给；
/// conditionId 逐工况给定——与样本行 conditionId 恒同）。
SeriesIdentity makeIdentity(const ObjectId& cond, const std::string& tag, std::size_t planned)
{
    SeriesIdentity id;
    id.snapshotId.bytes = digestOf("mrg-snap");     // 同一评估上下文（同快照）
    id.sliceId.bytes = digestOf("mrg-slice");
    id.trajectoryPayloadId.bytes = digestOf("mrg-trj");
    id.conditionId = cond;
    id.toolObjectId = ObjectId{};                   // 无工具模型（合法空 id）
    id.dynConfigDigest = "sha256-mrg-config";       // config.dyn 摘要（测试值）
    id.task.project = idFrom<sdurws::ird::core::ProjectId>("mrg-prj");
    id.task.branch = idFrom<sdurws::ird::core::BranchId>("mrg-brn");
    id.task.revision = idFrom<sdurws::ird::core::RevisionId>("mrg-rev");
    id.task.run = idFrom<sdurws::ird::core::RunId>("mrg-run");
    id.task.attempt.value = 1u;                     // AttemptId＝u64 尝试序号
    id.plannedSampleCount = planned;
    return id;
}

/// 冻结单关节黄金序列（行数据由调用方给齐——行数＝planned）。
DynamicsSeries makeSeries(const ObjectId& cond, const std::string& tag,
                          const std::vector<DynamicsSample>& rows)
{
    DynamicsSeriesBuilder b;
    for (const DynamicsSample& r : rows) {
        b.addSample(r);
    }
    return b.finalize(makeIdentity(cond, tag, rows.size()));
}

/// 组装 OperatingConditionResult（§4.4 聚合形态——峰值/功率能量/有效性
/// 三面均产自公共计算器，即 mergeEnvelope 契约要求的生产路径）。
OperatingConditionResult makeResult(const DynamicsSeries& series)
{
    const DynamicsEnvelopeCalculator calc;
    OperatingConditionResult res;
    res.conditionId = series.conditionId;
    res.caseScope = {series.conditionId};           // 单工况自身（EVI-02 关联）
    res.series = series;
    res.peaks = calc.computePeaks(series);
    res.powerEnergy = PowerEnergyCalculator{}.compute(series);
    res.validity = series.validity;
    return res;
}

/// 两行常数序列的快捷单关节结果（Empty/覆盖用例的"有样本产出"对照面）。
OperatingConditionResult oneRowHelper(const ObjectId& cond)
{
    return makeResult(makeSeries(cond, "emp-ok", {rowAt(cond, 0.0, 0u, 0.0, 0.0, 1.0),
                                                  rowAt(cond, 1.0, 0u, 0.0, 0.0, 1.0)}));
}

/// 冻结必验工况集（evidence §4.1.3 测试面组装——caseId/label/enabled/
/// mandatory 逐条给定；requiredCaseSetId 取合法非零摘要——合并器不消费
/// 凭据本体，只消费条目）。
RequiredCaseSet makeCaseSet(const std::vector<CaseEntry>& entries)
{
    RequiredCaseSet cs;
    cs.entries = entries;
    cs.requiredCaseSetId.bytes = digestOf("mrg-case-set");
    return cs;
}

/// CaseEntry 快捷构造（enabled/mandatory 逐条显式——覆盖矩阵口径面）。
CaseEntry caseOf(const ObjectId& id, bool enabled, bool mandatory)
{
    CaseEntry e;
    e.caseId = id;
    e.label = "golden-case";                        // 展示文案（非身份载体）
    e.enabled = enabled;
    e.mandatory = mandatory;
    return e;
}

}  // namespace

// =====================================================================
// 用例 1：三工况手工黄金合并——逐量纲跨工况 max、来源工况归因、并列峰值
//   的 conditionId 序确定性、RMS 跨工况 max、入参顺序无关（DYN-07 主对照
//   的机制面；V-24 的覆盖呈现侧在此一并覆盖）。
//
// 三个工况（单关节转动，量纲 N·m/rad/s/rad/s²/W）：
//   C_A：t=0..1.0 步 0.1（前 6 点段 0、后 5 点段 1）
//        τ  = [1,−2, 5, 5, 5,−4,−6, 2, 1,0,0]  q̇=[0,1,2,2,2,1,.5,.2,0,0,0]
//        q̈ = [0, 1, 3, 3, 3, 1, 0, 0,0,0,0]    P=τ·q̇
//        峰：τ⁺=5@[0.2,0.4]、τ⁻=6@0.6、|q̇|=2、|q̈|=3、P⁺=10、P⁻=4@0.5
//        RMS²=½(1+4)·.1+½(4+25)·.1+½(25+25)·.1+½(25+25)·.1+½(25+16)·.1
//             +½(16+36)·.1+½(36+4)·.1+½(4+1)·.1+½(1+0)·.1+0 = 13.65（T=1）
//   C_B：τ=[3,3,−6,−6,0,…]、q̇=[0,0,0,0,1.5,1.5,1.5,0,…]、q̈=[0,0,4,4,0,…]
//        峰：τ⁺=3、τ⁻=6@0.2〔与 C_A 并列——胜者=conditionId 字典序较小者〕、
//        |q̇|=1.5、|q̈|=4@0.2、P⁺=P⁻=0（P≡0）；RMS²=0.9+2.25+3.6+1.8=8.55
//   C_C：保持型工况（3 点 t=0/0.5/1，τ=7 恒、q̇=q̈=0——"设计工况"以给定
//        样本呈现，合并器不解释其语义）
//        峰：τ⁺=7@[0,1]（全跨度平顶）、τ⁻=−7（全正循环的带符号形态）、
//        |q̇|=0、|q̈|=0、P=0；RMS=7（常数序列解析）
// 合并期望：τ⁺=7(C_C)、τ⁻=6(并列→字典序小者)、|q̇|=2(C_A)、|q̈|=4(C_B)、
//   P幅值=10(C_A)、RMS=7(C_C)；来源集={C_A,C_B,C_C}（升序）；Complete。
// =====================================================================

TEST(DynEnvelopeMerge, ThreeConditionGoldenMaxMergeAndAttribution)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-07", "EVI-02"}, std::vector<std::string>{"AT-07"});
    // 本用例验证：跨工况逐量纲 max 合并的值/发生时间/段/窗随胜者整体归因、
    // 并列峰值按 conditionId 字典序取胜（确定性——与入参顺序无关）、RMS
    // 跨工况 max、来源工况集升序、envelopeComplete 全 Complete 为真。
    const ObjectId condA = idFrom<ObjectId>("mrg-cond-a");
    const ObjectId condB = idFrom<ObjectId>("mrg-cond-b");
    const ObjectId condC = idFrom<ObjectId>("mrg-cond-c");

    // —— C_A：11 点黄金网格（段边界在 k=5/6 之间）——
    const double tauA[11] = {1, -2, 5, 5, 5, -4, -6, 2, 1, 0, 0};
    const double qdA[11] = {0, 1, 2, 2, 2, 1, 0.5, 0.2, 0, 0, 0};
    const double qddA[11] = {0, 1, 3, 3, 3, 1, 0, 0, 0, 0, 0};
    std::vector<DynamicsSample> rowsA;
    for (int k = 0; k < 11; ++k) {
        rowsA.push_back(rowAt(condA, 0.1 * k, (k <= 5) ? 0u : 1u, qdA[k], qddA[k], tauA[k]));
    }

    // —— C_B：τ⁻ 并列 6、q̈ 峰 4、P≡0 ——
    const double tauB[11] = {3, 3, -6, -6, 0, 0, 0, 0, 0, 0, 0};
    const double qdB[11] = {0, 0, 0, 0, 1.5, 1.5, 1.5, 0, 0, 0, 0};
    const double qddB[11] = {0, 0, 4, 4, 0, 0, 0, 0, 0, 0, 0};
    std::vector<DynamicsSample> rowsB;
    for (int k = 0; k < 11; ++k) {
        rowsB.push_back(rowAt(condB, 0.1 * k, 0u, qdB[k], qddB[k], tauB[k]));
    }

    // —— C_C：保持型（给定静态样本——统一评估，无特殊构造语义）——
    std::vector<DynamicsSample> rowsC;
    for (const double t : {0.0, 0.5, 1.0}) {
        rowsC.push_back(rowAt(condC, t, 2u, 0.0, 0.0, 7.0));
    }

    const DynamicsSeries sA = makeSeries(condA, "mrg-a", rowsA);
    const DynamicsSeries sB = makeSeries(condB, "mrg-b", rowsB);
    const DynamicsSeries sC = makeSeries(condC, "mrg-c", rowsC);
    const std::vector<OperatingConditionResult> results = {makeResult(sA), makeResult(sB),
                                                           makeResult(sC)};

    // 覆盖分母：三工况全 Must 启用（V-24 覆盖呈现侧——全在结果集中）。
    const RequiredCaseSet coverage =
        makeCaseSet({caseOf(condA, true, true), caseOf(condB, true, true),
                     caseOf(condC, true, true)});

    const DynamicsEnvelopeCalculator calc;
    const DynamicsEnvelope env = calc.mergeEnvelope(results, coverage);

    // 包络面：单关节行、来源集升序含三工况、计数与覆盖呈现。
    ASSERT_EQ(env.joints.size(), 1u);
    const DynamicsEnvelope::JointEnvelope& j = env.joints[0];
    EXPECT_EQ(j.jointIndex, 0u);
    EXPECT_TRUE(j.jointObjectId == idFrom<ObjectId>("mrg-joint"));
    ASSERT_EQ(j.contributingConditions.size(), 3u);
    EXPECT_TRUE(std::is_sorted(j.contributingConditions.begin(),
                               j.contributingConditions.end()))
        << "来源工况集必须按 ObjectId 升序（稳定排序——NFR-COR-02）";
    EXPECT_TRUE(std::find(j.contributingConditions.begin(), j.contributingConditions.end(),
                          condA)
                != j.contributingConditions.end());
    EXPECT_TRUE(std::find(j.contributingConditions.begin(), j.contributingConditions.end(),
                          condB)
                != j.contributingConditions.end());
    EXPECT_TRUE(std::find(j.contributingConditions.begin(), j.contributingConditions.end(),
                          condC)
                != j.contributingConditions.end());
    EXPECT_EQ(env.conditionCount, 3u);
    EXPECT_TRUE(env.coversAllMandatory) << "三 Must 工况全在结果集——覆盖呈现为真（V-24）";
    EXPECT_TRUE(j.envelopeComplete) << "全部来源 Complete——包络完整";
    EXPECT_TRUE(env.contentIdentity.isValid());

    // τ⁺：C_C 的 7（全跨度平顶窗 [0,1]、段 2）。
    expectNearRel(j.tauMaxPositive.value, 7.0, "τ⁺ 包络=C_C 保持矩");
    EXPECT_TRUE(j.tauMaxPositive.conditionId == condC);
    EXPECT_DOUBLE_EQ(j.tauMaxPositive.tPeakS, 0.0);
    EXPECT_DOUBLE_EQ(j.tauMaxPositive.windowStartS, 0.0);
    EXPECT_DOUBLE_EQ(j.tauMaxPositive.windowEndS, 1.0);
    EXPECT_EQ(j.tauMaxPositive.segmentIndex, 2u);

    // τ⁻：C_A/C_B 并列 6——胜者=conditionId 字典序较小者（值/时间/段/窗
    // 随胜者整体归因）。
    expectNearRel(j.tauMaxNegative.value, 6.0, "τ⁻ 包络=并列 6");
    const bool aFirst = condA < condB;  // ObjectId 字节字典序（运行时判定——
                                        //   摘要派生 id 的序与种子拼写无关）
    EXPECT_TRUE(j.tauMaxNegative.conditionId == (aFirst ? condA : condB))
        << "并列峰值必须取 conditionId 字典序较小工况（合并确定性）";
    EXPECT_DOUBLE_EQ(j.tauMaxNegative.tPeakS, aFirst ? 0.6 : 0.2);
    EXPECT_EQ(j.tauMaxNegative.segmentIndex, aFirst ? 1u : 0u);

    // |q̇|：C_A 的 2（窗 [0.2,0.4]）。
    expectNearRel(j.velocityPeak.value, 2.0, "速度包络=C_A");
    EXPECT_TRUE(j.velocityPeak.conditionId == condA);

    // |q̈|：C_B 的 4（@0.2、窗 [0.2,0.3]）。
    expectNearRel(j.accelerationPeak.value, 4.0, "加速度包络=C_B");
    EXPECT_TRUE(j.accelerationPeak.conditionId == condB);
    EXPECT_DOUBLE_EQ(j.accelerationPeak.tPeakS, 0.2);
    EXPECT_DOUBLE_EQ(j.accelerationPeak.windowStartS, 0.2);
    EXPECT_DOUBLE_EQ(j.accelerationPeak.windowEndS, 0.3);

    // 功率幅值：C_A 的 10（max(P)=10>max(−P)=4——幅值合并口径）。
    expectNearRel(j.powerPeak.value, 10.0, "功率幅值包络=C_A");
    EXPECT_TRUE(j.powerPeak.conditionId == condA);
    EXPECT_DOUBLE_EQ(j.powerPeak.windowStartS, 0.2);
    EXPECT_DOUBLE_EQ(j.powerPeak.windowEndS, 0.4);

    // RMS：跨工况 max=7（C_C 常数序列解析；C_A √13.65≈3.696、C_B √8.55≈2.924）。
    expectNearRel(j.rmsTau, 7.0, "RMS 包络=跨工况 max");
    expectNearRel(j.rmsTau * j.rmsTau, 49.0, "RMS²=49（黄金）");

    // 入参顺序无关：逆序重合并——包络逐位同（含内容身份，NFR-COR-02）。
    std::vector<OperatingConditionResult> reversed = {results[2], results[1], results[0]};
    const DynamicsEnvelope env2 = calc.mergeEnvelope(reversed, coverage);
    EXPECT_TRUE(env2.contentIdentity == env.contentIdentity)
        << "入参顺序不得影响合并产物（conditionId 序处理）";
}

// =====================================================================
// 用例 2：多负载工况＋保持型设计工况统一 RNEA 评估→包络合并端到端
//   （acceptance 1/3——DYN-07"多负载工况、急停/保持等设计工况"与 P-DYN-4
//   "模板结构未冻结期间按统一 RNEA 评估给定工况，不自行定义构造语义"
//   的执行证明：三个工况走同一 InverseDynamicsEvaluator 同一代码路径，
//   "保持"工况只是给定静态样本的普通工况，合并器不解释工况语义）。
//
// 解析（二连杆地面安装、q=[0,0]、静态 q̇=q̈=0；g=9.81 m/s²）：
//   轻载（无工具）：τ₁=−g(m₁c₁+m₂(L₁+c₂))=−10.3005；τ₂=−g·m₂c₂=−1.4715
//   重载（工具 2.0 kg @TCP 系 (0,0,0)、法兰→TCP 平移 0.25 m）：
//     τ₁=−g(1.05+2.0·(L₁+0.25))=−g(1.05+1.5)=−25.0155（工具对关节 1 的
//       力臂＝L₁+tcpOffset——T03 用例 3 同式）；τ₂=−g(0.15+2.0·0.25)=−6.3765
//   保持（轻载模型、独立工况 id、更长驻留时标）：同轻载解析值
// 包络期望：τ⁺=轻载值（负力矩中更接近 0 者）、τ⁻=重载幅值、RMS=重载幅值、
//   速度/功率=真实零（不是伪装零——qd=0 全循环）；来源集=三工况。
// =====================================================================

TEST(DynEnvelopeMerge, MultiPayloadAndHoldConditionsUnifiedRneaEndToEnd)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-07"}, std::vector<std::string>{"AT-07"});
    // 本用例验证：多负载（不同工具质量的两个模型）＋保持型设计工况（独立
    // 工况 id 的静态样本）全部经统一 RNEA 评估器评估后包络合并——轻/重载
    // 峰值与 RMS 的解析归因、保持工况参与来源集、覆盖呈现与完整性。
    constexpr double kG = 9.81;   // 世界系重力幅值，m/s²（WorldPlacement 默认）

    const CanonicalModel lightModel = makeTwoLinkModel(groundWorld(), FrictionSpec{}, std::nullopt);
    const CanonicalModel heavyModel =
        makeTwoLinkModel(groundWorld(), FrictionSpec{}, ToolSpec{2.0, true, 0.0, 0.25, false});

    // 三个工况 id（互相独立——保持工况用轻载模型但工况 id 不同：包络按
    // 工况归因而非按模型）。
    const ObjectId condLight = idFrom<ObjectId>("e2e-cond-light");
    const ObjectId condHeavy = idFrom<ObjectId>("e2e-cond-heavy");
    const ObjectId condHold = idFrom<ObjectId>("e2e-cond-hold");

    // 统一评估入口：同一 InverseDynamicsEvaluator 实例、同一静态激励形态
    // （保持工况 t=0/1/2——更长驻留时标；轻/重载 t=0/0.5/1）。P-DYN-4：
    // 不定义"急停/保持构造器"——设计工况以给定样本进入统一 RNEA。
    auto evaluateStatic = [](const CanonicalModel& model, const ObjectId& cond,
                             const double t0, const double step) {
        InverseDynRequest req;
        req.conditionId = cond;
        req.model = &model;
        for (int k = 0; k < 3; ++k) {
            InverseDynSampleInput s;
            s.t = t0 + step * k;                    // s（上游轨迹时间轴）
            s.segmentIndex = 0u;
            s.q = {0.0, 0.0};                       // rad
            s.qd = {0.0, 0.0};                      // rad/s（静态）
            s.qdd = {0.0, 0.0};                     // rad/s²
            req.samples.push_back(s);
        }
        FakeContextLike ctx;
        return InverseDynamicsEvaluator().evaluate(req, ctx);
    };

    const InverseDynOutcome outLight = evaluateStatic(lightModel, condLight, 0.0, 0.5);
    const InverseDynOutcome outHeavy = evaluateStatic(heavyModel, condHeavy, 0.0, 0.5);
    const InverseDynOutcome outHold = evaluateStatic(lightModel, condHold, 0.0, 1.0);
    ASSERT_EQ(outLight.validity.completeness, DynamicsValidity::Completeness::Complete);
    ASSERT_EQ(outHeavy.validity.completeness, DynamicsValidity::Completeness::Complete);
    ASSERT_EQ(outHold.validity.completeness, DynamicsValidity::Completeness::Complete);

    // 评估器样本行→构建器冻结（validitySeed 透传评估器事实）→聚合装配。
    auto toResult = [](const InverseDynOutcome& outcome, const ObjectId& cond,
                       const std::string& tag) {
        DynamicsSeriesBuilder b;
        for (const DynamicsSample& r : outcome.samples) {
            b.addSample(r);
        }
        SeriesIdentity id = makeIdentity(cond, tag, 3);
        id.validitySeed = outcome.validity;
        return makeResult(b.finalize(id));
    };
    const std::vector<OperatingConditionResult> results = {toResult(outLight, condLight, "lt"),
                                                           toResult(outHeavy, condHeavy, "hv"),
                                                           toResult(outHold, condHold, "hd")};

    // 覆盖分母：轻/重载 Must、保持 Should（设计工况可为可选必验面）。
    const RequiredCaseSet coverage =
        makeCaseSet({caseOf(condLight, true, true), caseOf(condHeavy, true, true),
                     caseOf(condHold, true, false)});

    const DynamicsEnvelopeCalculator calc;
    const DynamicsEnvelope env = calc.mergeEnvelope(results, coverage);
    ASSERT_EQ(env.joints.size(), 2u) << "两关节各一行包络";
    EXPECT_EQ(env.conditionCount, 3u);
    EXPECT_TRUE(env.coversAllMandatory);
    EXPECT_TRUE(env.joints[0].envelopeComplete);
    EXPECT_TRUE(env.joints[1].envelopeComplete);
    for (const DynamicsEnvelope::JointEnvelope& j : env.joints) {
        ASSERT_EQ(j.contributingConditions.size(), 3u) << "三工况全参与来源集";
    }

    // 解析黄金（T03 用例 1/3 同式；单位 N·m——重载关节 1 的工具力臂含 L₁）。
    const double tau1Light = -kG * (kM1 * kC1 + kM2 * (kL1 + kC2));            // −10.3005
    const double tau2Light = -kG * kM2 * kC2;                                  // −1.4715
    const double tau1Heavy =
        -kG * (kM1 * kC1 + kM2 * (kL1 + kC2) + 2.0 * (kL1 + 0.25));            // −25.0155
    const double tau2Heavy = -kG * (kM2 * kC2 + 2.0 * 0.25);                   // −6.3765

    // 关节 0：τ⁺=轻载值（负力矩循环中更大者，来源=轻载/保持——二者解析值
    // 恒同，胜者=conditionId 字典序较小者）；τ⁻=重载幅值（来源=重载）；
    // 速度/功率=真实零；RMS=重载幅值（常数序列 RMS=幅值的 max）。
    const DynamicsEnvelope::JointEnvelope& j0 = env.joints[0];
    EXPECT_NEAR(j0.tauMaxPositive.value, tau1Light, 1e-9) << "τ⁺=轻载重力矩";
    EXPECT_NEAR(j0.tauMaxNegative.value, -tau1Heavy, 1e-9) << "τ⁻=重载幅值";
    EXPECT_TRUE(j0.tauMaxNegative.conditionId == condHeavy) << "τ⁻ 来源=重载工况";
    EXPECT_DOUBLE_EQ(j0.velocityPeak.value, 0.0) << "静态循环速度峰=真实零";
    EXPECT_DOUBLE_EQ(j0.powerPeak.value, 0.0) << "静态循环功率幅值=真实零";
    EXPECT_NEAR(j0.rmsTau, -tau1Heavy, 1e-9) << "RMS 包络=重载保持矩幅值";
    // τ⁺ 来源：轻载与保持解析恒同（并列）——胜者=conditionId 字典序较小者。
    const bool lightFirst = condLight < condHold;
    EXPECT_TRUE(j0.tauMaxPositive.conditionId == (lightFirst ? condLight : condHold));

    // 关节 1：同款归因（重载 τ₂⁻ 幅值、轻载 τ₂⁺）。
    const DynamicsEnvelope::JointEnvelope& j1 = env.joints[1];
    EXPECT_NEAR(j1.tauMaxPositive.value, tau2Light, 1e-9);
    EXPECT_NEAR(j1.tauMaxNegative.value, -tau2Heavy, 1e-9);
    EXPECT_TRUE(j1.tauMaxNegative.conditionId == condHeavy);
    EXPECT_NEAR(j1.rmsTau, -tau2Heavy, 1e-9);

    // 关节对象 ID 传播：包络行的 jointObjectId 与样本行一致（关节 0/1 的
    // 稳定 id——夹具 "dyn-j1"/"dyn-j2" 派生）。
    EXPECT_TRUE(j0.jointObjectId == idFrom<ObjectId>("dyn-j1"));
    EXPECT_TRUE(j1.jointObjectId == idFrom<ObjectId>("dyn-j2"));
}

// =====================================================================
// 用例 3：Partial 来源→envelopeComplete=false＋来源清单仍在（V-26"包络
//   标注不完整"），且与覆盖呈现互相独立（覆盖矩阵缺项不影响本布尔、
//   反之亦然——"两者独立呈现"的机器判读面）。
// =====================================================================

TEST(DynEnvelopeMerge, PartialSourceMarksEnvelopeIncompleteIndependently)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-07", "EVI-02"}, std::vector<std::string>{"AT-07"});
    // 本用例验证：来源工况 Partial（计划 3 实际 2）→envelopeComplete=false
    // 但贡献工况仍在来源清单（不静默剔除——§7.4"＋来源清单"）；覆盖呈现
    // （coversAllMandatory）按工况在场独立计算为 true——两布尔互不替代。
    const ObjectId condA = idFrom<ObjectId>("prt-cond-a");
    const ObjectId condB = idFrom<ObjectId>("prt-cond-b");

    std::vector<DynamicsSample> rowsA;
    for (const double t : {0.0, 0.5, 1.0}) {
        rowsA.push_back(rowAt(condA, t, 0u, 0.0, 0.0, 3.0));
    }
    // C_B：计划 3 只产 2 行（缺口——§4.6 Partial 语义，统计可继续）。
    std::vector<DynamicsSample> rowsB;
    for (const double t : {0.0, 0.5}) {
        rowsB.push_back(rowAt(condB, t, 0u, 1.0, 0.0, 4.0));
    }
    // makeSeries 按 rows.size() 冻结 planned——缺口须 planned=3/actual=2：
    // 直接以构建器冻结并显式 planned=3。
    DynamicsSeries sB;
    {
        DynamicsSeriesBuilder b;
        for (const DynamicsSample& r : rowsB) {
            b.addSample(r);
        }
        sB = b.finalize(makeIdentity(condB, "prt-b", 3));  // 计划 3 时刻
    }
    ASSERT_EQ(sB.validity.completeness, DynamicsValidity::Completeness::Partial);
    ASSERT_EQ(sB.validity.plannedSampleCount, 3u);
    ASSERT_EQ(sB.validity.actualSampleCount, 2u);

    const std::vector<OperatingConditionResult> results = {makeResult(makeSeries(condA, "prt-a",
                                                                                     rowsA)),
                                                           makeResult(sB)};
    const RequiredCaseSet coverage =
        makeCaseSet({caseOf(condA, true, true), caseOf(condB, true, true)});

    const DynamicsEnvelopeCalculator calc;
    const DynamicsEnvelope env = calc.mergeEnvelope(results, coverage);
    ASSERT_EQ(env.joints.size(), 1u);
    const DynamicsEnvelope::JointEnvelope& j = env.joints[0];

    EXPECT_FALSE(j.envelopeComplete) << "任一来源 Partial→包络不完整（§7.4）";
    ASSERT_EQ(j.contributingConditions.size(), 2u) << "Partial 来源仍在来源清单";
    EXPECT_TRUE(std::find(j.contributingConditions.begin(), j.contributingConditions.end(),
                          condB)
                != j.contributingConditions.end());
    EXPECT_TRUE(env.coversAllMandatory)
        << "覆盖呈现按工况在场（有样本产出）独立计算——与包络完整性互不替代";
    // 完整来源 C_A 的 3 与 Partial 来源 C_B 的 4 跨工况 max=4。
    EXPECT_DOUBLE_EQ(j.tauMaxPositive.value, 4.0);
    // RMS：C_A=3（常数）；C_B τ=[4,4]、T=0.5 → RMS=4；max=4（Partial 来源
    // 的已产出区间统计照常参与——§4.6"Partial 不阻断该工况其余统计"）。
    EXPECT_DOUBLE_EQ(j.rmsTau, 4.0);
}

// =====================================================================
// 用例 4：覆盖分母口径（EVI-02/acceptance 2——覆盖矩阵分母按快照冻结
//   RequiredCaseSet；V-25 漏验呈现＋disabled 条目不参与＋空分母平凡完备
//   〔P-EV-7〕＋"漂亮包络不等于覆盖"钉扎）。
// =====================================================================

TEST(DynEnvelopeMerge, CoverageDenominatorIsFrozenRequiredCaseSet)
{
    IRD_TEST_INFO(std::vector<std::string>{"EVI-02", "DYN-07"}, std::vector<std::string>{"AT-07"});
    // 本用例验证：coversAllMandatory 的分母＝coverage 的 enabled∧mandatory
    // 条目（冻结 RequiredCaseSet），与合并进来的结果数量/质量无关——
    // 漏一个 Must 工况即 false（V-25）；disabled Must（NotApplicable→
    // 不参与，§8.1 矩阵 C4 行）不构成漏验；空分母平凡完备（P-EV-7，
    // 保守处置归 evidence）；纯 Should 的完整包络不得声称覆盖（包络合并
    // 不替代必验工况覆盖规则——EVI-02 钉扎）。
    const ObjectId condM1 = idFrom<ObjectId>("cov-cond-m1");
    const ObjectId condM2 = idFrom<ObjectId>("cov-cond-m2");
    const ObjectId condM3 = idFrom<ObjectId>("cov-cond-m3");
    const ObjectId condS = idFrom<ObjectId>("cov-cond-s");

    auto oneRow = [](const ObjectId& cond) {
        return makeResult(makeSeries(cond, "cov", {rowAt(cond, 0.0, 0u, 0.0, 0.0, 2.0),
                                                   rowAt(cond, 1.0, 0u, 0.0, 0.0, 2.0)}));
    };

    const DynamicsEnvelopeCalculator calc;

    // ① 漏验：三 Must 只合并两个 → false（V-25）。
    {
        const std::vector<OperatingConditionResult> partial = {oneRow(condM1), oneRow(condM2)};
        const RequiredCaseSet coverage = makeCaseSet({caseOf(condM1, true, true),
                                                      caseOf(condM2, true, true),
                                                      caseOf(condM3, true, true)});
        const DynamicsEnvelope env = calc.mergeEnvelope(partial, coverage);
        EXPECT_FALSE(env.coversAllMandatory) << "漏一个启用必验工况必须呈现缺项";
        EXPECT_TRUE(env.joints[0].envelopeComplete)
            << "合并来源自身完整——包络完整性与覆盖缺项独立";
    }
    // ② disabled Must：不在覆盖义务内（NotApplicable→不参与）。
    {
        const std::vector<OperatingConditionResult> partial = {oneRow(condM1)};
        const RequiredCaseSet coverage = makeCaseSet({caseOf(condM1, true, true),
                                                      caseOf(condM3, false, true)});
        const DynamicsEnvelope env = calc.mergeEnvelope(partial, coverage);
        EXPECT_TRUE(env.coversAllMandatory)
            << "disabled 必验工况（NotApplicable）不在覆盖分母的有效子集内";
    }
    // ③ 空分母：平凡完备（P-EV-7——保守处置归 evidence 汇总判定层）。
    {
        const std::vector<OperatingConditionResult> one = {oneRow(condS)};
        const RequiredCaseSet empty = makeCaseSet({});
        const DynamicsEnvelope env = calc.mergeEnvelope(one, empty);
        EXPECT_TRUE(env.coversAllMandatory) << "空必验集→平凡完备（呈现层）";
    }
    // ④ 漂亮包络≠覆盖：纯 Should 工况的完整包络（envelopeComplete=true）
    //    在分母含未合并 Must 时必须 false——包络合并不替代覆盖规则。
    {
        const std::vector<OperatingConditionResult> optionalOnly = {oneRow(condS)};
        const RequiredCaseSet coverage = makeCaseSet({caseOf(condS, true, false),
                                                      caseOf(condM1, true, true)});
        const DynamicsEnvelope env = calc.mergeEnvelope(optionalOnly, coverage);
        EXPECT_TRUE(env.joints[0].envelopeComplete) << "Should 来源 Complete→包络完整";
        EXPECT_FALSE(env.coversAllMandatory)
            << "完整包络不得声称覆盖未合并的必验工况（EVI-02）";
    }
}

// =====================================================================
// 用例 5：Empty 语义（§7.4/§7.6——无已完成工况→包络不产出，绝不 0 值
//   伪装）＋Empty 完整度来源的覆盖保守不计。
// =====================================================================

TEST(DynEnvelopeMerge, EmptySemanticsAndEmptySourceCoverage)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-07", "NFR-COR-03"}, std::vector<std::string>{});
    // 本用例验证：①空结果列表→joints 空＋计数 0＋身份块完整（Empty 可
    // 寻址）；②全部来源无 Ok 行→同样不产出；③Empty 完整度的必验工况
    // 虽在结果集中（执行了但未产出样本）→覆盖保守不计为 false。
    const DynamicsEnvelopeCalculator calc;
    const ObjectId condA = idFrom<ObjectId>("emp-cond-a");
    const ObjectId condB = idFrom<ObjectId>("emp-cond-b");

    // ① 空列表。
    {
        const RequiredCaseSet coverage = makeCaseSet({caseOf(condA, true, true)});
        const DynamicsEnvelope env = calc.mergeEnvelope({}, coverage);
        EXPECT_TRUE(env.joints.empty()) << "空集→包络不产出（Empty 语义）";
        EXPECT_EQ(env.conditionCount, 0u);
        EXPECT_FALSE(env.coversAllMandatory) << "分母含未合并 Must→false（如实）";
        EXPECT_TRUE(env.contentIdentity.isValid()) << "空包络仍可寻址（身份块进摘要）";
    }
    // ② 全部来源无 Ok 行（单行标 NonFiniteInput——序列无统计行）。
    {
        DynamicsSample bad = rowAt(condA, 0.0, 0u, 0.0, 0.0, 2.0);
        bad.numericState = SampleNumericState::NonFiniteInput;
        bad.tauTotal = std::numeric_limits<double>::quiet_NaN();
        bad.mechanicalPower = std::numeric_limits<double>::quiet_NaN();
        const OperatingConditionResult res = makeResult(makeSeries(condA, "emp", {bad}));
        const RequiredCaseSet coverage = makeCaseSet({});
        const DynamicsEnvelope env = calc.mergeEnvelope({res}, coverage);
        EXPECT_TRUE(env.joints.empty()) << "无 Ok 行→无统计行→包络不产出";
        EXPECT_EQ(env.conditionCount, 1u) << "参与合并的工况数如实计数";
        EXPECT_TRUE(env.contentIdentity.isValid());
    }
    // ③ Empty 完整度来源的覆盖保守不计。
    {
        DynamicsSeriesBuilder b;  // 空序列（planned=0——Empty 冻结）
        const DynamicsSeries emptySeries = b.finalize(makeIdentity(condA, "emp-empty", 0));
        ASSERT_EQ(emptySeries.validity.completeness, DynamicsValidity::Completeness::Empty);
        const std::vector<OperatingConditionResult> results = {makeResult(emptySeries),
                                                               oneRowHelper(condB)};
        const RequiredCaseSet coverage =
            makeCaseSet({caseOf(condA, true, true), caseOf(condB, true, true)});
        const DynamicsEnvelope env = calc.mergeEnvelope(results, coverage);
        EXPECT_FALSE(env.coversAllMandatory)
            << "必验工况执行但 Empty（未产出样本）→覆盖保守不计（不虚报）";
        EXPECT_EQ(env.conditionCount, 2u);
        EXPECT_TRUE(env.joints[0].envelopeComplete)
            << "Empty 来源无关节行→不毒化任何关节包络（其缺项经覆盖呈现）";
    }
}

// =====================================================================
// 用例 6：RMS 跨工况合并的无效传播（§7.3——无效显式 NaN 不伪装：单样本
//   Complete 序列峰值有效但无时间区间→RMS NaN 不参与 max；全无效→NaN）。
// =====================================================================

TEST(DynEnvelopeMerge, RmsNaNExcludedFromMaxAndAllInvalidNan)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-07", "DYN-03"}, std::vector<std::string>{});
    // 本用例验证：①正常来源 RMS=2 与单样本来源（RMS NaN）合并→rmsTau=2
    // （NaN 不参与、不清零）；②仅单样本来源→rmsTau=NaN（显式无效）。
    const ObjectId condA = idFrom<ObjectId>("rms-cond-a");
    const ObjectId condB = idFrom<ObjectId>("rms-cond-b");
    const DynamicsEnvelopeCalculator calc;

    // C_A：两点常数 τ=2（T=1 → RMS=2）。
    const OperatingConditionResult resA =
        makeResult(makeSeries(condA, "rms-a", {rowAt(condA, 0.0, 0u, 0.0, 0.0, 2.0),
                                               rowAt(condA, 1.0, 0u, 0.0, 0.0, 2.0)}));
    // C_B：单样本（planned=1→Complete——峰值有效、RMS 无时间区间=NaN）。
    const OperatingConditionResult resB =
        makeResult(makeSeries(condB, "rms-b", {rowAt(condB, 0.0, 0u, 0.0, 0.0, 9.0)}));
    ASSERT_FALSE(resB.peaks.empty());
    ASSERT_TRUE(std::isnan(calc.computeRms(resB.series, 0)));

    {
        const RequiredCaseSet coverage = makeCaseSet({});
        const DynamicsEnvelope env = calc.mergeEnvelope({resA, resB}, coverage);
        ASSERT_EQ(env.joints.size(), 1u);
        EXPECT_DOUBLE_EQ(env.joints[0].rmsTau, 2.0) << "NaN 不参与 max（不毒化有效值）";
        // 峰值跨工况 max 不受影响：单样本 τ=9 > 2。
        EXPECT_DOUBLE_EQ(env.joints[0].tauMaxPositive.value, 9.0);
    }
    {
        const RequiredCaseSet coverage = makeCaseSet({});
        const DynamicsEnvelope env = calc.mergeEnvelope({resB}, coverage);
        ASSERT_EQ(env.joints.size(), 1u);
        EXPECT_TRUE(std::isnan(env.joints[0].rmsTau))
            << "全部来源 RMS 无效→NaN（显式无效——不伪造 0）";
    }
}

// =====================================================================
// 用例 7：fail-fast 装配契约（§10.0 调用方错误轨——身份恒同/透传恒同/
//   重复工况/峰值装配一致性/覆盖分母条目校验；DynamicsError 不发稳定码）。
// =====================================================================

TEST(DynEnvelopeMerge, AssemblyContractFailsFast)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-07"}, std::vector<std::string>{});
    // 本用例验证：入参契约逐项拒绝——①conditionId 与序列不恒同；②validity
    // 与序列不恒同；③工况重复入合并；④peaks 与 computePeaks(series) 不
    // 一致（装配不同源）；⑤序列含 Ok 行而 peaks 为空（装配缺失——④的
    // 特例，独立断言定位）；⑥覆盖分母条目 caseId 空；⑦覆盖分母条目重复。
    const ObjectId condA = idFrom<ObjectId>("ff-cond-a");
    const ObjectId condB = idFrom<ObjectId>("ff-cond-b");
    const DynamicsEnvelopeCalculator calc;
    const RequiredCaseSet coverage = makeCaseSet({caseOf(condA, true, true)});

    const OperatingConditionResult okResult =
        makeResult(makeSeries(condA, "ff-a", {rowAt(condA, 0.0, 0u, 0.0, 0.0, 2.0),
                                              rowAt(condA, 1.0, 0u, 0.0, 0.0, 2.0)}));

    auto expectRejected = [&](const std::vector<OperatingConditionResult>& results,
                              const RequiredCaseSet& cs, const char* what) {
        try {
            static_cast<void>(calc.mergeEnvelope(results, cs));
            FAIL() << what << "：必须 fail-fast";
        } catch (const DynamicsError&) {
            // 预期路径（调用方错误轨——不发稳定码，token 面见实现）。
        }
    };

    // ① conditionId 与 series.conditionId 不恒同。
    {
        OperatingConditionResult bad = okResult;
        bad.conditionId = condB;  // 篡改聚合层工况 id
        expectRejected({bad}, coverage, "conditionId 与序列不恒同");
    }
    // ② validity 与 series.validity 不恒同（透传契约）。
    {
        OperatingConditionResult bad = okResult;
        bad.validity.frictionMissing = true;  // 篡改聚合层有效性（与序列不一致）
        expectRejected({bad}, coverage, "validity 与序列不恒同");
    }
    // ③ 同一工况两次入合并。
    expectRejected({okResult, okResult}, coverage, "工况重复入合并");
    // ④ peaks 装配不同源（篡改一行峰值值）。
    {
        OperatingConditionResult bad = okResult;
        bad.peaks[0].value += 1.0;
        expectRejected({bad}, coverage, "peaks 与复算不一致");
    }
    // ⑤ 装配缺失：序列有 Ok 行而 peaks 为空。
    {
        OperatingConditionResult bad = okResult;
        bad.peaks.clear();
        expectRejected({bad}, coverage, "peaks 缺装配");
    }
    // ⑥ 覆盖分母条目 caseId 为空。
    {
        const RequiredCaseSet bad = makeCaseSet({caseOf(ObjectId{}, true, true)});
        expectRejected({okResult}, bad, "分母条目 caseId 空");
    }
    // ⑦ 覆盖分母条目重复。
    {
        const RequiredCaseSet bad =
            makeCaseSet({caseOf(condA, true, true), caseOf(condA, true, false)});
        expectRejected({okResult}, bad, "分母条目 caseId 重复");
    }
}
