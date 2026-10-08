/**
 * @file   EnvelopeBoundaryContractTest.cpp
 * @brief  WP-17-T07 契约测试——多工况包络面的契约钉扎：mergeEnvelope 公共
 *         契约形态编译期核对（签名＝§10.4 设计基线〔覆盖分母形参按 DTB
 *         §5.4 以 evidence::RequiredCaseSet 承载——单元卡 §1.2 T07 登记〕
 *         ＋包络摘要 magic 词面冻结）＋EVI-02 覆盖分母口径行为契约
 *         （coversAllMandatory 只由冻结 RequiredCaseSet 的 enabled∧mandatory
 *         条目与结果在场决定；与 envelopeComplete 互相独立——"包络合并
 *         不替代必验工况覆盖规则"的机器判读面）。
 *
 * 设计依据：
 *   - units/dynamics.md §10.4（mergeEnvelope 契约表——签名/前置/合法与
 *     非法示例）、§7.4（多工况包络——不替代必验工况）、§8.1（覆盖矩阵
 *     分母＝快照冻结 caseSet；EVI-02 五态）、§8.3（包络与矩阵独立呈现）
 *   - 需求 DYN-07（包络合并不替代必验工况覆盖规则）、EVI-02（正式计算与
 *     判定必须覆盖全部启用必验工况）
 *   - 先例：RneaBoundaryContractTest/EvidenceBoundaryContractTest（静态
 *     钉扎＋契约形态核对形态）
 */

#include <gtest/gtest.h>

#include <sdurws/ird/dynamics/DynTypes.hpp>
#include <sdurws/ird/dynamics/Envelope.hpp>
#include <sdurws/ird/dynamics/SeriesBuilder.hpp>
#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/evidence/Snapshot.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯

#include <algorithm>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

using namespace sdurws::ird::dynamics;
namespace evidence = sdurws::ird::evidence;
namespace core = sdurws::ird::core;

namespace {

/// 固定种子的 SHA-256 摘要（测试专用确定性来源——非产品路径）。
core::Digest256 digestOf(const std::string& seed)
{
    core::ContentDigester d;
    d.update(seed.data(), seed.size());
    return d.finalize();
}

/// 从固定种子派生 16 字节强类型 id（前 16 字节；测试值）。
template <typename Id>
Id idFrom(const std::string& seed)
{
    const core::Digest256 d = digestOf(seed);
    Id id;
    std::copy(d.begin(), d.begin() + 16, id.bytes.begin());
    return id;
}

/// 两点常数力矩黄金序列（单关节转动——覆盖契约的最小结果构造面）。
DynamicsSeries twoPointSeries(const core::ObjectId& cond, double tau)
{
    DynamicsSeriesBuilder b;
    for (const double t : {0.0, 1.0}) {
        DynamicsSample r;
        r.t = t;                                    // s
        r.segmentIndex = 0u;
        r.conditionId = cond;
        r.jointIndex = 0u;
        r.jointObjectId = idFrom<core::ObjectId>("eb-joint");
        r.jointType = DynJointType::Revolute;       // 转动——N·m
        r.q = 0.0;                                  // rad
        r.qd = 0.0;                                 // rad/s（静态）
        r.qdd = 0.0;                                // rad/s²
        r.tauTotal = tau;                           // N·m
        r.mechanicalPower = 0.0;                    // W（τ·q̇）
        r.numericState = SampleNumericState::Ok;
        b.addSample(r);
    }
    SeriesIdentity id;
    id.snapshotId.bytes = digestOf("eb-snap");
    id.sliceId.bytes = digestOf("eb-slice");
    id.trajectoryPayloadId.bytes = digestOf("eb-trj");
    id.conditionId = cond;
    id.dynConfigDigest = "sha256-eb-config";
    id.task.project = idFrom<core::ProjectId>("eb-prj");
    id.task.branch = idFrom<core::BranchId>("eb-brn");
    id.task.revision = idFrom<core::RevisionId>("eb-rev");
    id.task.run = idFrom<core::RunId>("eb-run");
    id.task.attempt.value = 1u;
    id.plannedSampleCount = 2;
    return b.finalize(id);
}

/// 聚合结果装配（峰值产自统计口径唯一实现点——mergeEnvelope 生产路径）。
OperatingConditionResult assemble(const DynamicsSeries& series)
{
    OperatingConditionResult res;
    res.conditionId = series.conditionId;
    res.caseScope = {series.conditionId};
    res.series = series;
    res.peaks = DynamicsEnvelopeCalculator{}.computePeaks(series);
    res.validity = series.validity;
    return res;
}

/// 必验工况条目快捷构造。
evidence::CaseEntry caseOf(const core::ObjectId& id, bool enabled, bool mandatory)
{
    evidence::CaseEntry e;
    e.caseId = id;
    e.enabled = enabled;
    e.mandatory = mandatory;
    return e;
}

/// 冻结必验工况集（凭据取合法非零摘要——合并器只消费条目）。
evidence::RequiredCaseSet caseSetOf(const std::vector<evidence::CaseEntry>& entries)
{
    evidence::RequiredCaseSet cs;
    cs.entries = entries;
    cs.requiredCaseSetId.bytes = digestOf("eb-case-set");
    return cs;
}

}  // namespace

// =====================================================================
// 用例 1：mergeEnvelope 公共契约形态编译期核对（§10.4 设计基线——签名/
//   返回类型/const 性静态钉扎；包络摘要 magic 词面冻结——改动即跨版本
//   摘要断链；逐关节六 token 常量与包络五记录槽结构核对）。
// =====================================================================

TEST(DynEnvelopeBoundary, MergeContractShapeAndMagicFrozen_WP17T07)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-07", "EVI-02"}, std::vector<std::string>{});
    // 本用例验证：①mergeEnvelope 签名与 §10.4 设计基线同形（入参＝结果集
    //   ＋覆盖分母、返回 DynamicsEnvelope、const 成员——覆盖分母形参类型
    //   为 evidence::RequiredCaseSet，§10.4 草案名 CaseCoverageSnapshot 的
    //   登记落地面，单元卡 §1.2 T07 注）；②包络摘要 magic 词面冻结
    //   （IRDDYVE1——与序列 IRDDYNS1/证据 IRDDYEV1 域分隔）；③峰值行结构
    //   常量冻结（每关节 6 行——token 表），包络五记录槽（token 4/5 并入
    //   powerPeak）与 §4.4 类型面一致。
    using MemberPtr = DynamicsEnvelope (DynamicsEnvelopeCalculator::*)(
        const std::vector<OperatingConditionResult>&, const evidence::RequiredCaseSet&) const;
    static_assert(std::is_same_v<decltype(&DynamicsEnvelopeCalculator::mergeEnvelope),
                                 MemberPtr>,
                  "mergeEnvelope 签名漂移（§10.4 设计基线＋DTB §5.4 分母承载登记）");

    // 包络摘要 magic 词面冻结（8 字节 ASCII——编译期逐字对照）。
    static_assert(kEnvelopeContentMagic[0] == 'I' && kEnvelopeContentMagic[1] == 'R'
                      && kEnvelopeContentMagic[2] == 'D' && kEnvelopeContentMagic[3] == 'D'
                      && kEnvelopeContentMagic[4] == 'Y' && kEnvelopeContentMagic[5] == 'V'
                      && kEnvelopeContentMagic[6] == 'E' && kEnvelopeContentMagic[7] == '1',
                  "包络 canonical 摘要 magic 冻结值漂移（IRDDYVE1）");

    // 峰值行结构常量冻结（token 表长度——包络合并按 (jointIndex, token)
    // 对位消费；改动即跨任务行语义断链）。
    static_assert(kPeakTokenCount == 6, "峰值 token 数冻结值漂移（§7.2 枚举序）");
    static_assert(kPeaksPerJoint == 6, "每关节峰值行数冻结值漂移");

    // 运行期复核（非仅编译期）：magic 以字符串形态可读（诊断/留痕用）。
    EXPECT_EQ(std::string(kEnvelopeContentMagic, kEnvelopeContentMagic + 8), "IRDDYVE1");
}

// =====================================================================
// 用例 2：EVI-02 覆盖分母口径行为契约（覆盖矩阵分母按快照冻结
//   RequiredCaseSet；coversAllMandatory 与 envelopeComplete 互相独立——
//   "包络合并不替代必验工况覆盖规则"的机器判读面）。
// =====================================================================

TEST(DynEnvelopeBoundary, CoverageDenominatorAndIndependenceContract_WP17T07)
{
    IRD_TEST_INFO(std::vector<std::string>{"EVI-02", "DYN-07"}, std::vector<std::string>{});
    // 本用例验证：①同一结果集对不同冻结分母合并——coversAllMandatory 只
    //   随分母（enabled∧mandatory 条目）翻转，包络内容身份随之改变（覆盖
    //   答案是内容的一部分——内容寻址不掩盖覆盖口径）；②Partial 来源翻转
    //   envelopeComplete 但不动 coversAllMandatory（两布尔独立——V-26
    //   "两者独立呈现"）；③空结果集→joints 空＋身份有效（Empty 语义，
    //   覆盖答案仍按分母如实呈现）。
    const core::ObjectId condM = idFrom<core::ObjectId>("eb-cond-m");  // 必验
    const core::ObjectId condS = idFrom<core::ObjectId>("eb-cond-s");  // 可选
    const DynamicsEnvelopeCalculator calc;

    // ① 分母翻转：同一结果集（M 在场、Complete）——
    //    分母 {M Must} → true；分母 {S Must（M 不在）} → false。
    const std::vector<OperatingConditionResult> results = {assemble(twoPointSeries(condM, 2.0))};
    const DynamicsEnvelope envCovered = calc.mergeEnvelope(results, caseSetOf({caseOf(condM, true, true)}));
    const DynamicsEnvelope envMissing = calc.mergeEnvelope(results, caseSetOf({caseOf(condS, true, true)}));
    EXPECT_TRUE(envCovered.coversAllMandatory);
    EXPECT_FALSE(envMissing.coversAllMandatory) << "分母含未在场 Must→缺项呈现";
    EXPECT_TRUE(envCovered.contentIdentity.isValid());
    // 覆盖答案（coversAllMandatory）进包络内容身份——同一结果集在两个分母
    // 下答案不同（true/false），包络内容随之不同，摘要必须不同（内容寻址
    // 不掩盖覆盖口径：同摘要⇒同内容，反之内容异则摘要异）。
    EXPECT_FALSE(envCovered.contentIdentity == envMissing.contentIdentity)
        << "coversAllMandatory 翻转必须改变内容身份";
    EXPECT_TRUE(envCovered.joints[0].envelopeComplete);
    EXPECT_TRUE(envMissing.joints[0].envelopeComplete)
        << "覆盖缺项不改变来源完整性呈现（独立面①）";

    // ② 完整性独立：Partial 来源翻转 envelopeComplete、不动覆盖答案。
    {
        // 构造 Partial：计划 3 实际 2（缺口）。
        DynamicsSeriesBuilder b;
        for (const double t : {0.0, 1.0}) {
            DynamicsSample r;
            r.t = t;
            r.segmentIndex = 0u;
            r.conditionId = condS;
            r.jointIndex = 0u;
            r.jointObjectId = idFrom<core::ObjectId>("eb-joint");
            r.jointType = DynJointType::Revolute;
            r.q = 0.0;
            r.qd = 0.0;
            r.qdd = 0.0;
            r.tauTotal = 3.0;
            r.mechanicalPower = 0.0;
            r.numericState = SampleNumericState::Ok;
            b.addSample(r);
        }
        SeriesIdentity id;
        id.snapshotId.bytes = digestOf("eb-snap");
        id.sliceId.bytes = digestOf("eb-slice");
        id.trajectoryPayloadId.bytes = digestOf("eb-trj");
        id.conditionId = condS;
        id.dynConfigDigest = "sha256-eb-config";
        id.task.project = idFrom<core::ProjectId>("eb-prj");
        id.task.branch = idFrom<core::BranchId>("eb-brn");
        id.task.revision = idFrom<core::RevisionId>("eb-rev");
        id.task.run = idFrom<core::RunId>("eb-run");
        id.task.attempt.value = 1u;
        id.plannedSampleCount = 3;  // 计划 3 → 实际 2＝Partial
        const OperatingConditionResult partial = assemble(b.finalize(id));
        ASSERT_EQ(partial.validity.completeness, DynamicsValidity::Completeness::Partial);

        const evidence::RequiredCaseSet coverage = caseSetOf({caseOf(condS, true, true)});
        const DynamicsEnvelope env = calc.mergeEnvelope({partial}, coverage);
        EXPECT_FALSE(env.joints[0].envelopeComplete) << "Partial 来源→包络不完整（§7.4）";
        EXPECT_TRUE(env.coversAllMandatory)
            << "在场且有样本产出的必验工况照常计入覆盖（独立面②——完整性"
            "传播不得改写覆盖答案）";
    }

    // ③ 空结果集：Empty 语义＋覆盖答案按分母如实。
    {
        const DynamicsEnvelope env =
            calc.mergeEnvelope({}, caseSetOf({caseOf(condM, true, true)}));
        EXPECT_TRUE(env.joints.empty()) << "无已完成工况→包络不产出（Empty）";
        EXPECT_FALSE(env.coversAllMandatory) << "分母 Must 未在场→如实 false";
        EXPECT_TRUE(env.contentIdentity.isValid()) << "空包络仍可寻址";
    }
}
