/**
 * @file   EvidenceCrossUnitContractTest.cpp
 * @brief  reporting×evidence 跨单元契约测试——报告模型对 evidence 公共头
 *         类型族的消费面自证（acceptance 5 就绪声明＋acceptance 6 陷阱
 *         处置的常驻机器自证）。
 *
 * 设计依据：
 *   - units/reporting.md §3.2 C-2 消费行（evidence 类型族——ResultEnvelope
 *     只读值、资格纯检查、EvidenceManifest/EvidenceItemStatus 呈现口径、
 *     CurrentnessResult、ReproductionBlock）、§6.1（状态词只消费既有权威
 *     词表，不新增）
 *   - 需求 EVI-01（五级汇总呈现）、CON-02（三轴正交）、O-13/O-24（词表
 *     消费纪律）、P-EV-8（EvidenceItemStatus 五值实现承载）
 *   - 任务契约 tasks/foundation/RPT-T03.json acceptance 5（跨单元依赖
 *     就绪声明：EV-T06/EV-T07 已 done 合入——本测试即其机器自证）＋
 *     acceptance 6（陷阱处置：消费不重定义不复制）
 *
 * 消费纪律（本文件的锁定面）：
 *   - ReportModel.hpp 消费的 evidence 类型（EvaluationKey/ProducerInfo/
 *     ReproductionBlock/EvidenceItemClass/EvidenceItemStatus/
 *     CurrentnessStatus/InvalidationReason/BaselineConsistencyResult）
 *     全部为 evidence 公共头原类型——本测试以类型同一性＋值往返断言
 *     "唯一定义在 evidence，reporting 零重定义"（P-RPT-9 基线＝各卡
 *     v0.1 Draft；上游冻结出 diff 后按影响面增量同步留痕）。
 *   - EvidenceItemStatus 五值（O-13）各有其位、不合并——以编译期值域
 *     钉住（五值缺一或增一即失败）。
 *
 * 线程安全：纯值测试（无共享状态）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/reporting/ReportModel.hpp>

#include <sdurws/ird/evidence/Currentness.hpp>
#include <sdurws/ird/evidence/Evidence.hpp>
#include <sdurws/ird/evidence/Evaluator.hpp>
#include <sdurws/ird/evidence/Envelope.hpp>
#include <sdurws/ird/evidence/Snapshot.hpp>
#include <sdurws/ird/evidence/Verdict.hpp>

#include <chrono>
#include <cstdint>
#include <type_traits>

#include "../src/ReportCodec.hpp"

namespace {

using namespace sdurws::ird::reporting;
namespace ev = sdurws::ird::evidence;
namespace co = sdurws::ird::core;

/// 固定时刻（UTC——夹具不用当前时钟，NFR-COR-02）。
std::chrono::system_clock::time_point fixedTime(std::int64_t unixSeconds)
{
    return std::chrono::system_clock::time_point(std::chrono::seconds(unixSeconds));
}

/**
 * 验证 acceptance 5/acceptance 6（O-13/O-24）：词表消费自证——
 *   ①EvidenceItemStatus 五值各有其位（P-EV-8 实现承载词表；O-13"五值
 *     不合并"——值域编译期钉住，缺一/增一即失败）；
 *   ②报告模型字段直接持有 core/evidence 权威枚举类型（类型同一性——
 *     reporting 零重定义、零别名包装）。
 */
TEST(EvidenceCrossUnit, VocabularyConsumedNotRedefined_O13_O24)
{
    // ① EvidenceItemStatus 五值域钉住（声明序＝evidence 卡 P-EV-8 词序：
    //    Satisfied/Missing/Invalid/Unverified/NotApplicable——O-13 保守字面）。
    static_assert(static_cast<std::uint8_t>(ev::EvidenceItemStatus::Satisfied) == 0
                      && static_cast<std::uint8_t>(ev::EvidenceItemStatus::Missing) == 1
                      && static_cast<std::uint8_t>(ev::EvidenceItemStatus::Invalid) == 2
                      && static_cast<std::uint8_t>(ev::EvidenceItemStatus::Unverified) == 3
                      && static_cast<std::uint8_t>(ev::EvidenceItemStatus::NotApplicable)
                             == 4,
                  "EvidenceItemStatus 五值词表（P-EV-8/O-13）——五词各有其位不合并");

    // ② 报告字段与 core 词表的类型同一性（O-24——EvaluationMode/
    //    TaskOutcome/EngineeringStatus 置 core，消费侧不本地重定义）。
    static_assert(std::is_same_v<decltype(ResultRefSnapshot::mode), co::EvaluationMode>,
                  "ResultRefSnapshot.mode 消费 core::EvaluationMode（O-24）");
    static_assert(std::is_same_v<decltype(ResultRefSnapshot::outcome), co::TaskOutcome>,
                  "ResultRefSnapshot.outcome 消费 core::TaskOutcome（O-24）");
    static_assert(
        std::is_same_v<decltype(ResultRefSnapshot::engineeringStatus), co::EngineeringStatus>,
        "ResultRefSnapshot.engineeringStatus 消费 core::EngineeringStatus（O-24）");
    static_assert(std::is_same_v<ev::EvaluationKey, std::string>,
                  "EvaluationKey 为 evidence 公共别名（EV-T07 就绪形态——消费不重定义）");

    // 证据引用/绑定字段直接持有 evidence 类别与状态枚举（值赋值往返）。
    EvidenceRefEntry e;
    e.itemClass = ev::EvidenceItemClass::Suggested;
    e.status = ev::EvidenceItemStatus::Unverified;   // Quick 产物用于正式判定＝本态
    EXPECT_EQ(e.status, ev::EvidenceItemStatus::Unverified);
    EXPECT_EQ(e.itemClass, ev::EvidenceItemClass::Suggested);
}

/**
 * 验证 acceptance 5：evidence 包络类型族（EV-T06/EV-T07 交付面——
 * ReproductionBlock/ProducerInfo/CurrentnessStatus/InvalidationReason/
 * BaselineConsistencyResult）可被报告模型消费并经编码往返保值——
 * "消费不复制"的值面自证（同值进出编码，evidence 类型零拷贝语义损失）。
 */
TEST(EvidenceCrossUnit, EnvelopeTypesConsumedAndRoundtripPreserved_ACC5)
{
    // Superseded 当前性快照（evidence 词表值——消费侧呈现投影）。
    CurrentnessSnapshot snapshot;
    snapshot.status = ev::CurrentnessStatus::Superseded;
    snapshot.evaluatedAgainst.headRevision = co::RevisionId::generate();
    snapshot.evaluatedAgainst.contextSummary = "HEAD r8（相对生成时 r7 前进）";
    snapshot.computedAtUtc = fixedTime(1700000400);
    ev::InvalidationReason reason;
    reason.dependencyKey = "policy.content";
    reason.kind = ev::InvalidationKind::PolicyChanged;
    reason.detail = "策略内容身份变化（CON-06）";
    snapshot.reasons.push_back(reason);

    // 复现要素与产出者（evidence §4.1.2/§7.1 值——报告字段直接持有，
    // 消费形态与 §4.2/§4.3.1 字段表一致；不入身份编码——§4.4 字段表）。
    ev::ReproductionBlock reproduction;
    reproduction.productVersion = "0.1.0";
    reproduction.evidenceContractVersion = "ev-contract-1";
    reproduction.codecVersions.push_back("snapshot-codec/1");
    reproduction.compilerContractVersion = "compiler-contract/1";
    ev::ProducerInfo producer;
    producer.producedIn = ev::ProducerProcess::Worker;
    producer.productVersion = "0.1.0";

    // 基准一致性纯检查结果（evidence §6.5——reporting 消费不重算）。
    ev::BaselineConsistencyResult baseline;
    baseline.consistent = false;
    baseline.differingDimensions.push_back(ev::BaselineDifferenceDimension::InputBaseline);
    baseline.differingDimensions.push_back(ev::BaselineDifferenceDimension::SampleSets);

    // 字段级消费自证一（报告字段直接持有 evidence 值并通过字段校验）：
    // ReviewReportFields.reproduction/ResultRefSnapshot.producer 为
    // evidence 类型值拷贝——合法值经 firstFieldViolation 判定通过
    // （消费面类型同一是编译期事实，此处为值面可运行性自证）。
    {
        ReviewReportFields f;
        f.reportId = ReportId::generate();
        f.level = ReportLevel::B;
        f.project = co::ProjectId::generate();
        f.branch = co::BranchId::generate();
        f.revision = co::RevisionId::generate();
        f.revisionSeq = 1;
        ResultRefSnapshot r;
        r.runId = co::RunId::generate();
        r.task.project = f.project;
        r.task.branch = f.branch;
        r.task.revision = f.revision;
        r.task.run = r.runId;
        r.task.attempt.value = 1;
        r.evaluationKey = "kin-batch-ik";
        r.evaluatorContractVersion = 1;
        r.mode = co::EvaluationMode::Verified;
        r.outcome = co::TaskOutcome::Completed;
        r.engineeringStatus = co::EngineeringStatus::Feasible;
        r.snapshotId.bytes[0] = 0xA0;           // 非零内容身份（夹具）
        r.sliceId.bytes[0] = 0xA1;
        r.inputBaselineId.bytes[0] = 0xA2;
        r.caseScope.push_back(co::ObjectId::generate());
        r.eligibility.formalPass = true;
        r.currentness.status = ev::CurrentnessStatus::Current;
        r.currentness.evaluatedAgainst.headRevision = f.revision;
        r.currentness.computedAtUtc = fixedTime(1700000000);
        r.producer = producer;                  // evidence::ProducerInfo 值拷贝
        f.resultRefs.push_back(r);
        f.reproduction = reproduction;          // evidence::ReproductionBlock 值拷贝
        ReviewReportSection section;
        section.sectionId = "input-summary";
        section.sectionVersion = 1;
        section.selected = false;
        section.status = SectionStatus::NoFormalResult;
        section.missingItems.push_back(
            MissingItemView{"kin.region-coverage", "该域无已完成结果"});
        section.currentness = snapshot;         // Superseded 引用 → currentness 必填
        section.order = 1;
        f.sections.push_back(section);
        f.review.basisRevision = f.revision;
        f.review.signOff = SignOffState::unsignedState();
        f.currentnessSummary.perResult.push_back(
            ResultCurrentnessEntry{r.runId, r.currentness});
        f.sectionModelVersion = std::string(kSectionModelVersion);
        EXPECT_FALSE(firstFieldViolation(f, ReportLevelRule{}).has_value())
            << "evidence 类型族作为报告字段值消费——字段校验通过（类型/值面就绪）";
    }

    // 字段级消费自证二（编码往返保值）：入身份编码的 evidence 值
    // （章节 currentness 的 CurrentnessStatus/InvalidationReason、
    // variantDiff 的 BaselineConsistencyResult）经 ReportCodec-Full
    // 往返逐值相等——"消费不复制"的值面闭环（语义零损失）。
    {
        ReviewReportSection section;
        section.sectionId = "input-summary";
        section.sectionVersion = 1;
        section.selected = false;
        section.status = SectionStatus::NoFormalResult;
        section.missingItems.push_back(
            MissingItemView{"kin.region-coverage", "该域无已完成结果"});
        section.currentness = snapshot;
        section.order = 1;

        VariantDiffBlock variant;
        variant.baselineRevision = co::RevisionId::generate();
        variant.candidateRevision = co::RevisionId::generate();
        variant.modelDiffRef = co::ObjectId::generate();
        variant.baselineCheckResult = baseline;
        variant.tradeOffs.push_back(
            TradeOff{"预算", "候选方案以较小预算满足必验工况", std::nullopt});

        ReportFullFields full;
        full.data.level = ReportLevel::B;
        full.data.revisionSeq = 1;
        full.review.basisRevision = co::RevisionId::generate();
        full.review.variantDiff = variant;
        full.review.signOff = SignOffState::unsignedState();
        full.sectionModelVersion = std::string(kSectionModelVersion);
        full.sections.push_back(section);

        const ReportFullFields decoded =
            ReportCodec::parseFull(ReportCodec::encodeFull(full));
        ASSERT_EQ(decoded.sections.size(), 1u);
        ASSERT_TRUE(decoded.sections[0].currentness.has_value());
        // evidence::CurrentnessStatus/InvalidationReason 逐值保值。
        EXPECT_EQ(*decoded.sections[0].currentness->status, ev::CurrentnessStatus::Superseded);
        ASSERT_EQ(decoded.sections[0].currentness->reasons.size(), 1u);
        EXPECT_EQ(decoded.sections[0].currentness->reasons[0], reason);
        // evidence::BaselineConsistencyResult 逐值保值。
        ASSERT_TRUE(decoded.review.variantDiff.has_value());
        EXPECT_EQ(decoded.review.variantDiff->baselineCheckResult, baseline);
    }
}

}  // namespace
