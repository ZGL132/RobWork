/**
 * @file   ReportModelTest.cpp
 * @brief  reporting 报告模型与编码单测——身份确定性（RP-MDL-1）、身份与
 *         路径/名称分离（RP-MDL-3）、不可变与字段校验原语、双编码分离与
 *         确定性二进制规则（RPT-T03 acceptance 1/2/3/4 的单元内具名自证）。
 *
 * 设计依据：
 *   - units/reporting.md §10.1 RP-MDL-1/RP-MDL-3 行（验证方式）、§4 全节
 *     （§4.1 身份纪律、§4.2 非法实例矩阵、§4.4 编码规则）、§5.3/§6.4/§9.2
 *     （token 词表与字段形状）
 *   - 需求 RPT-01（身份/幂等）、NFR-COR-02（同输入同字节）、RPT-05
 *     （限定语词面）
 *   - 任务契约 tasks/foundation/RPT-T03.json acceptance 1~4
 *
 * 两阶段口径（契约 note 原文）：具名 RP-* 用例体随 RPT-T11 契约套件整备，
 * 本任务交付其**验证面用例（局部夹具）**——RP-MDL-1/3 的"构建"在本阶段
 * 以 ReviewReport::make（同夹具脚本两次构造）承载；导出/manifest 路径的
 * RP-MDL-3 全貌（重命名显示名再导出的端到端）随 RPT-T09/T11 收口，本文件
 * 以"身份纯函数性＋摘要幂等命中"锁定 §4.1『重命名/移动不改变任何身份』
 * 的类型级前提（显示名称/磁盘路径不是 ReviewReport 字段——身份编码面
 * 不含之，见 RP-MDL-3 组用例注释）。替身边界声明（RP-STATE-4 同源）：
 * 本文件夹具数据仅为报告结构契约验证，不构成任何业务算法正确性证明。
 *
 * 用例命名约定：`<主题>_<锚点>` 尾缀标注需求/acceptance 追溯字段
 * （AGENTS.md §2.7——每用例注释首行声明"验证哪条验收"）。
 */

#include <gtest/gtest.h>

#include "../src/ReportCodec.hpp"   // 单元内私有头（§3.1 src/ 行；runtime 先例）

#include <sdurws/ird/reporting/Errors.hpp>
#include <sdurws/ird/reporting/Identity.hpp>
#include <sdurws/ird/reporting/ReportModel.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace {

using namespace sdurws::ird::reporting;
namespace ev = sdurws::ird::evidence;
namespace co = sdurws::ird::core;

// =====================================================================
// 局部夹具（"同脚本"数据源——RP-MDL-1 的替身数据源对偶；RP-STATE-4：
// 夹具数据仅验证报告结构契约，不构成业务算法正确性证明）
// =====================================================================

/// 固定时刻（UTC——确定性：夹具不用当前时钟，NFR-COR-02）。
std::chrono::system_clock::time_point fixedTime(std::int64_t unixSeconds)
{
    return std::chrono::system_clock::time_point(std::chrono::seconds(unixSeconds));
}

/// 非零 32 字节内容身份（夹具用——core ContentIdentity 无 generate，
/// 手工铺位递增字节保证非零且互异）。
co::ContentIdentity fixtureIdentity(std::uint8_t seed)
{
    co::ContentIdentity id;
    for (std::size_t i = 0; i < id.bytes.size(); ++i) {
        id.bytes[i] = static_cast<std::uint8_t>(seed + i);
    }
    return id;
}

/// 非零 core::ObjectId（夹具用）。
co::ObjectId fixtureObject(std::uint8_t seed)
{
    co::ObjectId id;
    for (std::size_t i = 0; i < id.bytes.size(); ++i) {
        id.bytes[i] = static_cast<std::uint8_t>(seed * 4 + i);
    }
    return id;
}

/// 确定性 16 字节 Id128 夹具（core 身份类型族通用——铺位递增字节；同
/// seed 同值）。RP-MDL-1"同数据源两次构建"的确定性来源：数据源身份字段
/// （project/branch/revision/runId）必须逐位一致，**不得**用 generate()
/// （随机——每次调用产出不同数据源，身份相等断言将失去意义）；随机生成
/// 仅用于"必须互异"的场景（如 ReportId 新对象断言）。
template <typename T> T fixtureId(std::uint8_t seed)
{
    T id;
    for (std::size_t i = 0; i < id.bytes.size(); ++i) {
        id.bytes[i] = static_cast<std::uint8_t>(seed + i);
    }
    return id;
}

/// 当前性快照（Current 态——§6.3 presence 纪律的合法形态）。
CurrentnessSnapshot currentSnapshot(co::RevisionId head, std::int64_t at)
{
    CurrentnessSnapshot c;
    c.status = ev::CurrentnessStatus::Current;
    c.evaluatedAgainst.headRevision = head;
    c.evaluatedAgainst.contextSummary = "夹具上下文（HEAD 一致）";
    c.computedAtUtc = fixedTime(at);
    c.reasons = {};               // Current 恒空（§6.3）
    c.unevaluableNote = std::nullopt;
    return c;
}

/// 章节一：Populated（一条目——字段＋结果绑定，§4.3.4/§9.2 冻结视图）。
ReviewReportSection populatedSection(co::RunId runId, co::ObjectId case1)
{
    ReviewReportSection s;
    s.sectionId = "project-scheme";          // §5.1 词表 1 号（B 章节示例）
    s.sectionVersion = 1;
    s.selected = true;
    s.status = SectionStatus::Populated;

    SectionEntryView entry;
    entry.entryKey = "scheme-summary";       // §9.2 词形 [a-z0-9.-]{2,63}
    FieldValue field;
    field.key = "case.count";
    field.quantity = co::SourcedValue<double>::provided(
        3.0, co::ValueProvenance::make(co::ProvenanceKind::UserProvided));
    entry.fields.push_back(field);
    entry.result.runId = runId;              // 结论条目必带结果绑定（追溯锚）
    entry.result.fieldPath = "payload.caseCount";
    entry.caseScope.push_back(case1);
    s.entries.push_back(entry);

    s.sourceResults.push_back(runId);
    s.renderHint = RenderHint::Table;
    s.order = 1;
    return s;
}

/// 章节二：NoFormalResult（缺正式结果——默认不选＋缺项全量清单）。
ReviewReportSection missingSection()
{
    ReviewReportSection s;
    s.sectionId = "input-summary";           // §5.1 词表 2 号（框架章节示例）
    s.sectionVersion = 1;
    s.selected = false;                      // 缺正式结果默认不选（§16 验收要点）
    s.status = SectionStatus::NoFormalResult;
    s.missingItems.push_back(MissingItemView{"kin.region-coverage", "该域无已完成结果"});
    s.renderHint = RenderHint::MetadataBlock;
    s.order = 2;
    return s;
}

/// C 专属章节 ID 集（§5.1 词表 9~14 号——测试夹具副本；词表权威归
/// Sections.hpp/RPT-T04，本夹具仅注入组合判定原语，不构成第二权威）。
ReportLevelRule cExclusiveRule()
{
    ReportLevelRule rule;
    rule.cExclusiveSectionIds = {
        "trajectory-cycle", "dynamics-envelope", "drivetrain-operating-points",
        "selection-bom", "review-signoff", "variant-diff",
    };
    return rule;
}

/// 构造一份合法 B 级报告初值（同脚本——每次调用产出相同语义内容；
/// ReportId/generation 信息显式参数化供身份分离用例）。
ReviewReportFields makeValidFields(ReportId reportId,
                                   std::chrono::system_clock::time_point generatedAt,
                                   std::string generatedBy)
{
    ReviewReportFields f;
    f.reportId = reportId;
    f.level = ReportLevel::B;

    // 身份三元组（明确 ID——**确定性**夹具身份：同脚本两次调用逐位一致，
    // RP-MDL-1 身份相等断言的前提；随机 generate() 只用于 ReportId 参数）。
    f.project = fixtureId<co::ProjectId>(0x11);
    f.branch = fixtureId<co::BranchId>(0x12);
    f.revision = fixtureId<co::RevisionId>(0x13);
    f.revisionSeq = 7;

    // 结果引用（1 条 Verified/Completed/Feasible——§4.3.1 全字段）。
    ResultRefSnapshot r;
    r.runId = fixtureId<co::RunId>(0x14);
    r.task.project = f.project;
    r.task.branch = f.branch;
    r.task.revision = f.revision;
    r.task.run = r.runId;
    r.task.attempt.value = 1;
    r.evaluationKey = "kin-batch-ik";        // evidence EvaluationKey（string 承载）
    r.evaluatorContractVersion = 1;
    r.mode = co::EvaluationMode::Verified;
    r.outcome = co::TaskOutcome::Completed;
    r.engineeringStatus = co::EngineeringStatus::Feasible;
    r.snapshotId = fixtureIdentity(0x10);
    r.sliceId = fixtureIdentity(0x30);
    r.inputBaselineId = fixtureIdentity(0x50);
    r.caseScope.push_back(fixtureObject(0x01));
    r.eligibility.formalPass = true;
    r.eligibility.reviewRecord = false;
    r.currentness = currentSnapshot(f.revision, 1700000000);
    r.producer.producedIn = ev::ProducerProcess::MainProcess;
    r.producer.productVersion = "0.1.0";
    f.resultRefs.push_back(r);

    // 章节列表（order 严格递增——§4.7）。
    f.sections.push_back(populatedSection(r.runId, r.caseScope[0]));
    f.sections.push_back(missingSection());

    // 评审元数据（B 级最小形态——未签署）。
    f.review.basisRevision = f.revision;
    f.review.signOff = SignOffState::unsignedState();

    // 当前性汇总（与 resultRefs 对应）。
    f.currentnessSummary.perResult.push_back(
        ResultCurrentnessEntry{r.runId, currentSnapshot(f.revision, 1700000000)});

    // 复现要素（evidence ReproductionBlock 值拷贝——§4.3.1/§4.2）。
    f.reproduction.productVersion = "0.1.0";
    f.reproduction.evidenceContractVersion = "ev-contract-1";

    // 单位选择（冻结入身份——mm/rad 为 core R1 注册表 token）。
    f.unitPreference.displayUnits.emplace(co::QuantityKind::Length,
                                          *co::UnitToken::find("mm"));
    f.unitPreference.displayUnits.emplace(co::QuantityKind::Angle,
                                          *co::UnitToken::find("rad"));

    // 生成信息（不入身份——§4.4 排除列；参数化供 RP-MDL-3）。
    f.generatedAtUtc = generatedAt;
    f.generatedBy = std::move(generatedBy);
    f.generatorVersion = "rpt-0.1.0";
    f.reportVersion = 1;
    f.sectionModelVersion = std::string(kSectionModelVersion);
    return f;
}

/// 同脚本构造（RP-MDL-1"同数据源两次构建"的 make 对偶——ReportId 随机、
/// 其余字段逐位一致）。
ReviewReport makeSameSourceReport()
{
    return ReviewReport::make(
        makeValidFields(ReportId::generate(), fixedTime(1700000100), "batch"),
        cExclusiveRule());
}

/// 期望首个违例码（字段校验原语断言助手）。
void expectViolation(const ReviewReportFields& f, ReportErrorCode expected,
                     const char* what)
{
    const auto code = firstFieldViolation(f, cExclusiveRule());
    ASSERT_TRUE(code.has_value()) << what << "：期望违例 " << token(expected)
                                  << "，实际全部通过";
    EXPECT_EQ(*code, expected) << what;
}

// =====================================================================
// acceptance 1（RP-MDL-1）：报告身份确定性
// =====================================================================

/**
 * 验证 acceptance 1/RP-MDL-1：同数据源两次构建（替身同脚本——本阶段以
 * 同夹具两次 make 承载）contentIdentity 逐字节相等；同数据源重建＝新
 * ReportId（§4.1——ReportId 不承载内容信息，幂等判定不用它），如实断言。
 */
TEST(ReportModelIdentity, SameSourceTwoBuildsContentIdentityByteEqual_RP_MDL_1)
{
    const ReviewReport a = makeSameSourceReport();
    const ReviewReport b = makeSameSourceReport();

    // contentIdentity 逐字节相等（§10.1 RP-MDL-1 观测点"身份比较"）。
    EXPECT_TRUE(a.contentIdentity().isValid());
    EXPECT_EQ(a.contentIdentity().bytes, b.contentIdentity().bytes);
    // dataIdentity 同样确定（数据源身份——评审演化链上不变）。
    EXPECT_EQ(a.dataIdentity().bytes, b.dataIdentity().bytes);
    // ReportId 不同（新对象）——如实断言登记（§10.1 观测点原文）。
    EXPECT_NE(a.reportId(), b.reportId());
    // 两个身份互相独立（四概念互不替代——§4.1：报告对象身份≠内容身份）。
    EXPECT_NE(a.reportId().toCanonical(), a.contentIdentity().toCanonical());
}

/**
 * 验证 acceptance 1/§4.4 往返契约：ReportCodec-Data 往返
 * parse(encode(x))==x（编码器版本号入编码——头部由专测钉住）。
 */
TEST(ReportCodecRoundtrip, DataRoundtripParseEncodeEqual_RP_MDL_1)
{
    // 以合法报告抽取 §4.4 Data 字段集（同 make 内部口径——经 make 后的
    // 报告仅缺 spec 抽取出口，测试直接组装同形 spec 并断言往返）。
    ReviewReportFields f =
        makeValidFields(ReportId::generate(), fixedTime(1700000100), "batch");
    const ReportSourceSpec spec = [&f] {
        // 与 ReportModel.cpp extractDataSpec 同构的最小抽取（测试侧独立
        // 组装——往返断言的是 codec 自身，不依赖实现内部函数）。
        ReportSourceSpec s;
        s.project = f.project;
        s.branch = f.branch;
        s.revision = f.revision;
        s.revisionSeq = f.revisionSeq;
        s.level = f.level;
        s.snapshotId = f.snapshotId;
        s.inputSliceId = f.inputSliceId;
        for (const auto& r : f.resultRefs) {
            ResultDataSourceRef ref;
            ref.runId = r.runId;
            ref.snapshotId = r.snapshotId;
            ref.sliceId = r.sliceId;
            ref.inputBaselineId = r.inputBaselineId;
            ref.caseScope = r.caseScope;
            s.resultRefs.push_back(ref);
        }
        s.unitPreference = f.unitPreference;
        for (const auto& sec : f.sections) {
            s.selectedSections.push_back(SelectedSectionEntry{sec.sectionId, sec.selected});
        }
        return s;
    }();

    const std::vector<std::uint8_t> bytes = ReportCodec::encodeData(spec);
    const ReportSourceSpec decoded = ReportCodec::parseData(bytes);
    EXPECT_TRUE(decoded == spec) << "parse(encode(x))==x（§4.4 往返契约）";
}

/**
 * 验证 acceptance 1/§4.4 往返契约：ReportCodec-Full 往返
 * parse(encode(x))==x——含章节完整内容＋ReviewMetadata＋sectionModelVersion。
 */
TEST(ReportCodecRoundtrip, FullRoundtripParseEncodeEqual_RP_MDL_1)
{
    ReviewReportFields f =
        makeValidFields(ReportId::generate(), fixedTime(1700000100), "batch");
    // 追加评审演化要素（全类型覆盖：意见＋Superseded 当前性引用＋变体块
    // 不必全量——覆盖主要分支即可，全字段矩阵归 RPT-T11 契约套件）。
    ReviewComment comment;
    comment.author = "reviewer-a";
    comment.atUtc = fixedTime(1700000200);
    comment.text = "方案可行，注意行程上限";
    f.review.comments.push_back(comment);

    ReportFullFields full;
    full.data.project = f.project;
    full.data.branch = f.branch;
    full.data.revision = f.revision;
    full.data.revisionSeq = f.revisionSeq;
    full.data.level = f.level;
    full.data.snapshotId = f.snapshotId;
    full.data.inputSliceId = f.inputSliceId;
    for (const auto& r : f.resultRefs) {
        ResultDataSourceRef ref;
        ref.runId = r.runId;
        ref.snapshotId = r.snapshotId;
        ref.sliceId = r.sliceId;
        ref.inputBaselineId = r.inputBaselineId;
        ref.caseScope = r.caseScope;
        full.data.resultRefs.push_back(ref);
    }
    full.data.unitPreference = f.unitPreference;
    for (const auto& sec : f.sections) {
        full.data.selectedSections.push_back(
            SelectedSectionEntry{sec.sectionId, sec.selected});
    }
    full.sections = f.sections;
    full.review = f.review;
    full.sectionModelVersion = f.sectionModelVersion;

    const std::vector<std::uint8_t> bytes = ReportCodec::encodeFull(full);
    const ReportFullFields decoded = ReportCodec::parseFull(bytes);
    EXPECT_TRUE(decoded == full) << "parse(encode(x))==x（§4.4 往返契约——Full 面）";
}

// =====================================================================
// acceptance 2（RP-MDL-3）：身份与路径/名称分离
// =====================================================================

/**
 * 验证 acceptance 2/RP-MDL-3（§4.1『重命名/移动不改变任何身份』用例锁定）：
 * 显示名称与磁盘路径**不是 ReviewReport 字段**（§4.2 字段表穷举无此类
 * 字段——§1.3 目标 2"报告内容身份与文件路径、显示名称分离"的类型级事实），
 * 故"重命名/移动"对身份的唯一可达影响面为空。本用例锁定该前提的可达
 * 观测面：同语义内容在新对象（新 ReportId）与新生成信息（时间/者/版本）
 * 下重建，contentIdentity 逐字节不变。
 *
 * 全貌（构建后重命名显示名再导出——contentIdentity 不变、幂等命中
 * manifest 摘要比对）随 RPT-T09 导出归档任务收口；此处以内容摘要相等
 * 表达幂等命中的本地对偶（digestFull 两次相等＝同内容同摘要）。
 */
TEST(ReportModelIdentity, IdentitySeparatedFromIdAndGeneration_RP_MDL_3)
{
    // "构建"：同夹具脚本第一份。
    const ReviewReport a = ReviewReport::make(
        makeValidFields(ReportId::generate(), fixedTime(1700000100), "batch"),
        cExclusiveRule());
    // "重命名显示名＋移动＋重建"模拟：显示名/路径不在模型（无可改动
    // 字段）；可达变化仅新对象与新生成信息——逐项变化后重建。
    const ReviewReport b = ReviewReport::make(
        makeValidFields(ReportId::generate(), fixedTime(1800000200), "other-principal"),
        cExclusiveRule());

    EXPECT_EQ(a.contentIdentity().bytes, b.contentIdentity().bytes)
        << "重命名/移动/重建不改变任何身份（§4.1）";
    EXPECT_EQ(a.dataIdentity().bytes, b.dataIdentity().bytes);

    // 幂等命中（本地对偶）：同内容的 canonical 编码与摘要两次相等——
    // "manifest 摘要比对"的内容级等价物（文件侧幂等归 RPT-T09）。
    EXPECT_TRUE(a.contentIdentity() == b.contentIdentity());
}

// =====================================================================
// acceptance 3：不可变与字段校验原语
// =====================================================================

/**
 * 验证 acceptance 3/§4.2：ReviewReport 全部字段构造后不可变（冻结是类型
 * 系统事实——成员私有＋只读访问器＋无 setter，编译期强制）；值语义
 * （可拷贝，拷贝后与原件全等）；身份非零（合法实例纪律）。
 */
TEST(ReviewReportModel, ImmutableValueSemantics_RPT03_ACC3)
{
    const ReviewReport a = makeSameSourceReport();
    // 值语义：拷贝构造/赋值可得独立副本（§4.2"无 setter＋值语义"）。
    const ReviewReport copy = a;
    EXPECT_TRUE(copy == a);
    // 身份非零（§4.2 合法实例"contentIdentity 非零"）。
    EXPECT_TRUE(a.contentIdentity().isValid());
    EXPECT_TRUE(a.dataIdentity().isValid());
    // 冻结事实的运行期对偶：同对象两次访问同一身份字节（无任何写路径
    // 可使其漂移——类型系统事实的观测面；setter 不存在的证明＝编译期
    // 只能经 make 构造，注释为凭）。
    EXPECT_EQ(a.contentIdentity().bytes, a.contentIdentity().bytes);
    EXPECT_EQ(a.sectionModelVersion(), std::string(kSectionModelVersion));
}

/**
 * 验证 acceptance 3/§4.2 非法实例矩阵（逐项——字段校验原语 firstFieldViolation
 * 与 make() 抛出双轨断言；detail 携带字段定位）。
 */
TEST(ReviewReportValidation, IllegalInstanceMatrix_RPT03_ACC3)
{
    // ---- 基线合法初值 ----
    ReviewReportFields base =
        makeValidFields(ReportId::generate(), fixedTime(1700000100), "batch");
    ASSERT_FALSE(firstFieldViolation(base, cExclusiveRule()).has_value())
        << "夹具基线必须合法（用例前置自检）";

    // ---- revision 为空（§4.2"revision 为空/占位"——DataInvalid；文本占位
    //      "当前/tip"在请求解析边界拒绝〔SourceAmbiguous，RPT-T05〕）----
    {
        ReviewReportFields f = base;
        f.revision = co::RevisionId{};
        expectViolation(f, ReportErrorCode::DataInvalid, "revision 为空");
        EXPECT_THROW((void)ReviewReport::make(f, cExclusiveRule()), ReportError);
    }

    // ---- resultRefs 为空（§4.2 是(≥1)）----
    {
        ReviewReportFields f = base;
        f.resultRefs.clear();
        expectViolation(f, ReportErrorCode::DataInvalid, "resultRefs 为空");
    }

    // ---- Preview 结果进入 resultRefs（§4.6 行 5——SourceMissing）----
    {
        ReviewReportFields f = base;
        f.resultRefs[0].mode = co::EvaluationMode::Preview;
        expectViolation(f, ReportErrorCode::SourceMissing, "Preview 结果");
    }

    // ---- B 级携带 C 专属章节（§4.6 行 2——LevelConflict）----
    {
        ReviewReportFields f = base;
        ReviewReportSection cSec = missingSection();
        cSec.sectionId = "trajectory-cycle";   // §5.1 词表 9 号（C 专属）
        cSec.order = 3;
        f.sections.push_back(cSec);
        expectViolation(f, ReportErrorCode::LevelConflict, "B 级携 C 章节");
        EXPECT_THROW((void)ReviewReport::make(f, cExclusiveRule()), ReportError);
    }

    // ---- C 级零 C 章节选中（§4.6 行 4——ScopeInsufficient）----
    {
        ReviewReportFields f = base;
        f.level = ReportLevel::C;   // 章节集仍为纯 B 词表（无 C 章节选中）
        expectViolation(f, ReportErrorCode::ScopeInsufficient, "C 级零 C 章节");
    }

    // ---- 字段值 NaN（§4.2——DataInvalid；非有限在编码入口同样拒绝，
    //      见 ReportCodec 组）----
    {
        ReviewReportFields f = base;
        f.sections[0].entries[0].fields[0].quantity =
            co::SourcedValue<double>::provided(
                std::nan(""), co::ValueProvenance::make(co::ProvenanceKind::UserProvided));
        expectViolation(f, ReportErrorCode::DataInvalid, "字段值 NaN");
        EXPECT_THROW((void)ReviewReport::make(f, cExclusiveRule()), ReportError);
    }

    // ---- 章节 sourceResults ⊆ resultRefs 违约（§4.7——EvidenceRefInvalid）----
    {
        ReviewReportFields f = base;
        f.sections[0].sourceResults.push_back(co::RunId::generate());
        expectViolation(f, ReportErrorCode::EvidenceRefInvalid, "sourceResults 越界");
    }

    // ---- 证据绑定 caseScope ⊆ 所属结果 caseScope 违约（§4.7 行 7 族）----
    {
        ReviewReportFields f = base;
        EvidenceBinding bad;
        bad.itemId = "kin.ik-convergence-per-point";
        bad.status = ev::EvidenceItemStatus::Missing;
        bad.caseScope.push_back(fixtureObject(0xF0));   // 不在 run caseScope 内
        f.sections[0].entries[0].evidence.push_back(bad);
        expectViolation(f, ReportErrorCode::EvidenceRefInvalid, "绑定 caseScope 越界");
    }

    // ---- 当前性 presence：不可判定缺 unevaluableNote（§6.3——不得默认
    //      Current，EV-CUR-2）----
    {
        ReviewReportFields f = base;
        CurrentnessSnapshot c = currentSnapshot(f.revision, 1700000000);
        c.status = std::nullopt;
        c.unevaluableNote = std::nullopt;
        f.resultRefs[0].currentness = c;
        expectViolation(f, ReportErrorCode::DataInvalid, "不可判定缺诊断");
    }

    // ---- 当前性 presence：Current 携带 reasons（§6.3——Current 恒空）----
    {
        ReviewReportFields f = base;
        CurrentnessSnapshot c = currentSnapshot(f.revision, 1700000000);
        c.reasons.push_back(ev::InvalidationReason{"", ev::InvalidationKind::PolicyChanged,
                                                   "策略内容身份变化"});
        f.resultRefs[0].currentness = c;
        expectViolation(f, ReportErrorCode::DataInvalid, "Current 携带 reasons");
    }

    // ---- 章节引用 Superseded 结果但 currentness 缺席（§4.3.4 必填）----
    {
        ReviewReportFields f = base;
        f.resultRefs[0].currentness.status = ev::CurrentnessStatus::Superseded;
        f.resultRefs[0].currentness.reasons.push_back(
            ev::InvalidationReason{"slice.key", ev::InvalidationKind::SliceContentChanged,
                                   "切片内容变化"});
        f.resultRefs[0].currentness.unevaluableNote = std::nullopt;
        // 章节一引用 run1（现 Superseded）——currentness 必填。
        expectViolation(f, ReportErrorCode::DataInvalid, "Superseded 引用缺 currentness");
    }

    // ---- 证据引用 presence：Satisfied 无产物摘要（§4.3.2）----
    {
        ReviewReportFields f = base;
        EvidenceRefEntry e;
        e.itemId = "kin.ik-convergence-per-point";
        e.itemClass = ev::EvidenceItemClass::Required;
        e.status = ev::EvidenceItemStatus::Satisfied;
        e.sourceSection = "kinematics-collision";
        f.evidenceRefs.push_back(e);
        expectViolation(f, ReportErrorCode::DataInvalid, "Satisfied 无 digest");
    }

    // ---- 证据引用 presence：NotApplicable 无原因（C2/ERR-01）----
    {
        ReviewReportFields f = base;
        EvidenceRefEntry e;
        e.itemId = "trj.ik-continuity-per-cartesian-segment";
        e.itemClass = ev::EvidenceItemClass::Suggested;
        e.status = ev::EvidenceItemStatus::NotApplicable;
        e.sourceSection = "kinematics-collision";
        f.evidenceRefs.push_back(e);
        expectViolation(f, ReportErrorCode::DataInvalid, "NotApplicable 无原因");
    }

    // ---- 证据引用 presence：Invalid 无原因诊断码（§4.3.2/ERR-01）----
    {
        ReviewReportFields f = base;
        EvidenceRefEntry e;
        e.itemId = "kin.region-coverage";
        e.itemClass = ev::EvidenceItemClass::Required;
        e.status = ev::EvidenceItemStatus::Invalid;
        e.sourceSection = "kinematics-collision";
        f.evidenceRefs.push_back(e);
        expectViolation(f, ReportErrorCode::DataInvalid, "Invalid 无原因码");
    }

    // ---- 章节结构：order 未严格递增（§4.7）----
    {
        ReviewReportFields f = base;
        f.sections[1].order = f.sections[0].order;
        expectViolation(f, ReportErrorCode::DataInvalid, "order 相等");
    }

    // ---- 章节结构：sectionId 重复（§4.7 唯一性）----
    {
        ReviewReportFields f = base;
        f.sections[1].sectionId = f.sections[0].sectionId;
        expectViolation(f, ReportErrorCode::DataInvalid, "sectionId 重复");
    }

    // ---- 章节结构：Populated 无条目（§4.3.4 entries 必填）----
    {
        ReviewReportFields f = base;
        f.sections[0].entries.clear();
        expectViolation(f, ReportErrorCode::DataInvalid, "Populated 无条目");
    }

    // ---- 章节结构：NoFormalResult 无缺项清单（§4.3.4 missingItems 必填）----
    {
        ReviewReportFields f = base;
        f.sections[1].missingItems.clear();
        expectViolation(f, ReportErrorCode::DataInvalid, "缺项清单缺失");
    }

    // ---- 条目结构：entryKey 词形违约（§9.2 [a-z0-9.-]{2,63}）----
    {
        ReviewReportFields f = base;
        f.sections[0].entries[0].entryKey = "Bad_Key!";
        expectViolation(f, ReportErrorCode::DataInvalid, "entryKey 词形");
    }

    // ---- 评审元数据：意见正文超 4KiB（§4.5 text≤4KiB）----
    {
        ReviewReportFields f = base;
        ReviewComment c;
        c.author = "reviewer-a";
        c.atUtc = fixedTime(1700000200);
        c.text.assign(4097, 'x');
        f.review.comments.push_back(c);
        expectViolation(f, ReportErrorCode::DataInvalid, "意见超 4KiB");
    }

    // ---- 签署 presence：Unsigned 携带凭据（§4.5）----
    {
        ReviewReportFields f = base;
        f.review.signOff.signer = "ghost";
        expectViolation(f, ReportErrorCode::DataInvalid, "Unsigned 携带凭据");
    }

    // ---- 签署 presence：Signed 缺摘要（§4.5）----
    {
        ReviewReportFields f = base;
        f.review.signOff =
            SignOffState::signedState("reviewer-a", fixedTime(1700000300), co::Digest256{});
        expectViolation(f, ReportErrorCode::DataInvalid, "Signed 缺摘要");
    }

    // ---- 演化链：reportVersion < 1（§4.1 ≥1）----
    {
        ReviewReportFields f = base;
        f.reportVersion = 0;
        expectViolation(f, ReportErrorCode::DataInvalid, "reportVersion=0");
    }

    // ---- sectionModelVersion 非当前契约版本（§4.2 冻结常量）----
    {
        ReviewReportFields f = base;
        f.sectionModelVersion = "ird-report-section-model/2";
        expectViolation(f, ReportErrorCode::DataInvalid, "章节契约版本不符");
    }

    // ---- make() 抛出轨迹与 firstFieldViolation 码面一致（双轨同源）----
    {
        ReviewReportFields f = base;
        f.resultRefs.clear();
        try {
            (void)ReviewReport::make(f, cExclusiveRule());
            FAIL() << "make() 应抛出";
        } catch (const ReportError& e) {
            EXPECT_EQ(e.code(), ReportErrorCode::DataInvalid);
            EXPECT_NE(std::string(e.what()).find("resultRefs"), std::string::npos)
                << "detail 携带字段定位（what()=" << e.what() << "）";
        }
    }
}

// =====================================================================
// acceptance 4：双编码分离与确定性二进制规则
// =====================================================================

/**
 * 验证 acceptance 4/§4.4 排除列：ReportCodec-Data 排除评审元数据/entries
 * 内容/诊断/生成时间者——这些字段变化不改变 dataIdentity；进入编码字段
 * 变化必改变 dataIdentity（逐项锁定——进入/排除双向）。
 */
TEST(ReportCodecSeparation, DataIdentityIncludedExcludedFields_RPT03_ACC4)
{
    const ReviewReport a = makeSameSourceReport();
    const co::ContentIdentity baseData = a.dataIdentity();

    // —— 排除面：评审元数据变化（追加意见）→ dataIdentity 不变（§4.4
    //    排除列"评审元数据"；§4.5 评审演化链上 dataIdentity 不变）。----
    {
        ReviewReportFields f =
            makeValidFields(ReportId::generate(), fixedTime(1700000100), "batch");
        ReviewComment c;
        c.author = "reviewer-a";
        c.atUtc = fixedTime(1700000200);
        c.text = "追加意见";
        f.review.comments.push_back(c);
        const ReviewReport b = ReviewReport::make(std::move(f), cExclusiveRule());
        EXPECT_EQ(b.dataIdentity().bytes, baseData.bytes)
            << "评审元数据不入 dataIdentity（§4.4 排除列）";
        EXPECT_NE(b.contentIdentity().bytes, a.contentIdentity().bytes)
            << "但入 contentIdentity（§4.4——评审演化 contentIdentity 变化）";
    }

    // —— 排除面：entries 内容变化 → dataIdentity 不变。----
    {
        ReviewReportFields f =
            makeValidFields(ReportId::generate(), fixedTime(1700000100), "batch");
        f.sections[0].entries[0].fields[0].quantity =
            co::SourcedValue<double>::provided(
                99.0, co::ValueProvenance::make(co::ProvenanceKind::UserProvided));
        const ReviewReport b = ReviewReport::make(std::move(f), cExclusiveRule());
        EXPECT_EQ(b.dataIdentity().bytes, baseData.bytes)
            << "entries 内容不入 dataIdentity（§4.4 排除列）";
        EXPECT_NE(b.contentIdentity().bytes, a.contentIdentity().bytes);
    }

    // —— 排除面：诊断变化（章节诊断追加）→ dataIdentity 不变。----
    {
        ReviewReportFields f =
            makeValidFields(ReportId::generate(), fixedTime(1700000100), "batch");
        DiagRefEntry d;
        d.code = "RPT-SECTION-NOT-APPLICABLE";
        d.severity = sdurws::ird::diagnostics::DiagnosticSeverity::Info;
        d.category = sdurws::ird::diagnostics::DiagnosticCategory::InfeasibilityProof;
        d.sourceSection = "project-scheme";
        d.occurrences = 1;
        f.sections[0].diagnostics.push_back(d);
        const ReviewReport b = ReviewReport::make(std::move(f), cExclusiveRule());
        EXPECT_EQ(b.dataIdentity().bytes, baseData.bytes)
            << "诊断不入 dataIdentity（§4.4 排除列）";
    }

    // —— 进入面：身份三元组之修订变化 → dataIdentity 变化。----
    {
        ReviewReportFields f =
            makeValidFields(ReportId::generate(), fixedTime(1700000100), "batch");
        f.revision = co::RevisionId::generate();
        f.resultRefs[0].task.revision = f.revision;
        f.resultRefs[0].currentness.evaluatedAgainst.headRevision = f.revision;
        f.review.basisRevision = f.revision;
        f.currentnessSummary.perResult[0].currentness.evaluatedAgainst.headRevision =
            f.revision;
        const ReviewReport b = ReviewReport::make(std::move(f), cExclusiveRule());
        EXPECT_NE(b.dataIdentity().bytes, baseData.bytes)
            << "修订入 dataIdentity（§4.4 进入编码列）";
    }

    // —— 进入面：unitPreference 变化 → dataIdentity 变化（D-10 冻结）。----
    {
        ReviewReportFields f =
            makeValidFields(ReportId::generate(), fixedTime(1700000100), "batch");
        f.unitPreference.displayUnits.clear();   // 缺席量纲＝SI——与基线不同
        const ReviewReport b = ReviewReport::make(std::move(f), cExclusiveRule());
        EXPECT_NE(b.dataIdentity().bytes, baseData.bytes)
            << "单位选择入 dataIdentity（§4.4/D-10）";
    }

    // —— 进入面：选中章节集变化 → dataIdentity 变化。----
    {
        ReviewReportFields f =
            makeValidFields(ReportId::generate(), fixedTime(1700000100), "batch");
        f.sections[0].selected = false;
        const ReviewReport b = ReviewReport::make(std::move(f), cExclusiveRule());
        EXPECT_NE(b.dataIdentity().bytes, baseData.bytes)
            << "选中章节 (sectionId, selected) 集入 dataIdentity（§4.4）";
    }

    // —— 进入面：结果引用数据子集变化 → dataIdentity 变化。----
    {
        ReviewReportFields f =
            makeValidFields(ReportId::generate(), fixedTime(1700000100), "batch");
        f.resultRefs[0].snapshotId = fixtureIdentity(0x77);
        const ReviewReport b = ReviewReport::make(std::move(f), cExclusiveRule());
        EXPECT_NE(b.dataIdentity().bytes, baseData.bytes)
            << "resultRefs 数据子集入 dataIdentity（§4.4）";
    }
}

/**
 * 验证 acceptance 4/§4.4：ReportCodec-Full＝dataIdentity 全部字段＋章节
 * 完整内容＋ReviewMetadata＋sectionModelVersion——评审元数据与章节状态
 * 变化必改变 contentIdentity；生成信息变化不改变（排除列）。
 */
TEST(ReportCodecSeparation, FullIdentityCoversReviewAndSections_RPT03_ACC4)
{
    const ReviewReport a = makeSameSourceReport();

    // 章节状态变化 → contentIdentity 变化（章节完整内容入 Full）。
    {
        ReviewReportFields f =
            makeValidFields(ReportId::generate(), fixedTime(1700000100), "batch");
        f.sections[1].status = SectionStatus::NotApplicable;
        const ReviewReport b = ReviewReport::make(std::move(f), cExclusiveRule());
        EXPECT_NE(b.contentIdentity().bytes, a.contentIdentity().bytes);
        EXPECT_EQ(b.dataIdentity().bytes, a.dataIdentity().bytes);
    }

    // sectionModelVersion 变化 → contentIdentity 变化（§4.4 入 Full）。
    // 注：kSectionModelVersion 为冻结常量——版本演化走设计变更评审；
    // 此处以手工构造 Full 字段集验证编码面（不经 make 的常量校验）。
    {
        ReportFullFields full;
        full.data.level = ReportLevel::B;
        full.data.revisionSeq = 1;
        full.sections.push_back(missingSection());
        full.review.basisRevision = co::RevisionId::generate();
        full.sectionModelVersion = "ird-report-section-model/1";
        const co::ContentIdentity d1 = ReportCodec::digestFull(full);
        full.sectionModelVersion = "ird-report-section-model/2";
        const co::ContentIdentity d2 = ReportCodec::digestFull(full);
        EXPECT_NE(d1.bytes, d2.bytes) << "sectionModelVersion 入 contentIdentity（§4.4）";
    }
}

/**
 * 验证 acceptance 4/NFR-COR-02：纯函数同输入同字节——encode 两次字节
 * 相等、摘要相等（无时钟/随机/locale 依赖）。
 */
TEST(ReportCodecDeterminism, SameInputSameBytes_NFR_COR_02)
{
    const ReportSourceSpec spec = [] {
        ReviewReportFields f =
            makeValidFields(ReportId::generate(), fixedTime(1700000100), "batch");
        ReportSourceSpec s;
        s.project = f.project;
        s.branch = f.branch;
        s.revision = f.revision;
        s.revisionSeq = 0x0102030405060708ULL;   // 大端位序检查载荷
        s.level = ReportLevel::B;
        for (const auto& r : f.resultRefs) {
            ResultDataSourceRef ref;
            ref.runId = r.runId;
            ref.snapshotId = r.snapshotId;
            ref.sliceId = r.sliceId;
            ref.inputBaselineId = r.inputBaselineId;
            s.resultRefs.push_back(ref);
        }
        s.selectedSections.push_back(SelectedSectionEntry{"project-scheme", true});
        return s;
    }();

    const std::vector<std::uint8_t> b1 = ReportCodec::encodeData(spec);
    const std::vector<std::uint8_t> b2 = ReportCodec::encodeData(spec);
    EXPECT_EQ(b1, b2) << "同输入同字节（NFR-COR-02）";
    EXPECT_EQ(ReportCodec::digestData(spec), ReportCodec::digestData(spec));
}

/**
 * 验证 acceptance 4/§4.4 头部与字节布局：magic 字面（"IRDRPT1"/"IRDRPTD1"）、
 * 编码器版本号入编码（u32 大端＝1）、多字节整数大端、长度前缀、presence
 * 字节（逐字节钉住——确定性二进制的机器面）。
 */
TEST(ReportCodecBinary, MagicVersionBigEndianLayout_RPT03_ACC4)
{
    // 最小 Data 编码：branch 16B｜inputSliceId presence(0)｜level u8｜
    // project 16B｜resultRefs u32(0)｜revision 16B｜revisionSeq u64（大端
    // 载荷 0x0102030405060708）｜selectedSections u32(1)＋条目｜
    // snapshotId presence(0)｜unitPreference u32(0)——canonical 名序
    // （ReportCodec.cpp encodeData 注释表）。
    ReportSourceSpec spec;
    spec.branch = co::BranchId::generate();
    spec.level = ReportLevel::B;
    spec.project = co::ProjectId::generate();
    spec.revision = co::RevisionId::generate();
    spec.revisionSeq = 0x0102030405060708ULL;
    spec.selectedSections.push_back(SelectedSectionEntry{"ab", true});   // 长度前缀载荷

    const std::vector<std::uint8_t> bytes = ReportCodec::encodeData(spec);
    std::size_t off = 0;
    auto u8At = [&bytes, &off]() { return bytes[off++]; };
    auto u32At = [&bytes, &off]() {
        const std::uint32_t v = (static_cast<std::uint32_t>(bytes[off]) << 24)
                                | (static_cast<std::uint32_t>(bytes[off + 1]) << 16)
                                | (static_cast<std::uint32_t>(bytes[off + 2]) << 8)
                                | static_cast<std::uint32_t>(bytes[off + 3]);
        off += 4;
        return v;
    };
    auto u64At = [&bytes, &off]() {
        std::uint64_t v = 0;
        for (int i = 0; i < 8; ++i) {
            v = (v << 8) | bytes[off + i];
        }
        off += 8;
        return v;
    };

    // magic："IRDRPTD1"（8 字节 ASCII——§4.4 Data 字面）。
    const std::string magic(reinterpret_cast<const char*>(bytes.data()), 8);
    EXPECT_EQ(magic, "IRDRPTD1");
    off = 8;
    // 编码器版本号入编码（u32 大端＝1——§4.4 规则 8）。
    EXPECT_EQ(u32At(), 1u);
    // branch 16B。
    off += 16;
    // inputSliceId presence＝0（缺席——§4.4 规则 5）。
    EXPECT_EQ(u8At(), 0u);
    // level＝B（uint8 声明序 0）。
    EXPECT_EQ(u8At(), 0u);
    // project 16B。
    off += 16;
    // resultRefs 计数 u32＝0。
    EXPECT_EQ(u32At(), 0u);
    // revision 16B。
    off += 16;
    // revisionSeq u64 大端（0x0102030405060708——大端序逐字节核对）。
    EXPECT_EQ(u64At(), 0x0102030405060708ULL);
    // selectedSections 计数 u32＝1＋条目（sectionId 长度前缀 u32＝2＋"ab"
    // ＋selected u8＝1）。
    EXPECT_EQ(u32At(), 1u);
    EXPECT_EQ(u32At(), 2u);   // 长度前缀（§4.4 规则 4）
    EXPECT_EQ(bytes[off], 'a');
    EXPECT_EQ(bytes[off + 1], 'b');
    off += 2;
    EXPECT_EQ(u8At(), 1u);    // selected=true
    // snapshotId presence＝0。
    EXPECT_EQ(u8At(), 0u);
    // unitPreference 计数 u32＝0。
    EXPECT_EQ(u32At(), 0u);
    // 恰好消耗完（无尾随）。
    EXPECT_EQ(off, bytes.size());
}

/**
 * 验证 acceptance 4/§4.4：Full 编码头部 magic＝"IRDRPT1"（7 字节字面）、
 * 其后嵌入 Data 段头（复用编码——parseFull 消费之）。
 */
TEST(ReportCodecBinary, FullMagicAndEmbeddedDataHeader_RPT03_ACC4)
{
    ReportFullFields full;
    full.data.level = ReportLevel::B;
    full.data.revisionSeq = 1;
    full.review.basisRevision = co::RevisionId::generate();
    full.sectionModelVersion = std::string(kSectionModelVersion);

    const std::vector<std::uint8_t> bytes = ReportCodec::encodeFull(full);
    const std::string magic(reinterpret_cast<const char*>(bytes.data()), 7);
    EXPECT_EQ(magic, "IRDRPT1");          // §4.4 Full 字面（7 字节）
    // Full 头版本号（u32 大端＝1——§4.4 规则 8；位于 magic 之后、Data 段之前）。
    const std::uint32_t fullVersion =
        (static_cast<std::uint32_t>(bytes[7]) << 24) | (static_cast<std::uint32_t>(bytes[8]) << 16)
        | (static_cast<std::uint32_t>(bytes[9]) << 8) | static_cast<std::uint32_t>(bytes[10]);
    EXPECT_EQ(fullVersion, 1u);
    // 嵌入 Data 段头（offset 11 起——Full 头 7＋4 字节之后："IRDRPTD1"；
    // 布局与 parseFull 对称——先读 Full 头再读嵌入 Data 头）。
    const std::string inner(reinterpret_cast<const char*>(bytes.data() + 11), 8);
    EXPECT_EQ(inner, "IRDRPTD1");
    // 往返自洽（嵌头被 parseFull 消费——不残留）。
    const ReportFullFields decoded = ReportCodec::parseFull(bytes);
    EXPECT_TRUE(decoded == full);
}

/**
 * 验证 acceptance 4/§4.4：字符串 UTF-8 禁 NUL——编码入口拒绝（编码与
 * 解码两侧同规则；§4.4 规则 6）。
 */
TEST(ReportCodecBinary, NulStringRejectedAtEncode_RPT03_ACC4)
{
    ReportSourceSpec spec;
    spec.branch = co::BranchId::generate();
    spec.level = ReportLevel::B;
    spec.project = co::ProjectId::generate();
    spec.revision = co::RevisionId::generate();
    spec.selectedSections.push_back(
        // 含 NUL 的 sectionId：必须以（指针, 长度）构造——std::string(const
        // char*) 会在首个 '\0' 处截断（"bad"），NUL 就进不了编码入口，
        // 断言会失去对象。
        SelectedSectionEntry{std::string("bad\0id", 6), true});   // 含 NUL
    EXPECT_THROW((void)ReportCodec::encodeData(spec), ReportError);
    try {
        (void)ReportCodec::encodeData(spec);
        FAIL() << "应抛出";
    } catch (const ReportError& e) {
        EXPECT_EQ(e.code(), ReportErrorCode::DataInvalid);
    }
}

/**
 * 验证 acceptance 4/§4.4：浮点 IEEE754 位模式、NaN/±Inf 编码入口拒绝
 * （evidence D-06 同源——§4.4 规则 7；编码与解码两侧拒绝）。
 */
TEST(ReportCodecBinary, NonFiniteDoubleRejectedAndFiniteRoundtrips_RPT03_ACC4)
{
    // NaN 入编码 → DataInvalid（入口拒绝）。
    ReportFullFields nanFull;
    nanFull.data.level = ReportLevel::B;
    nanFull.data.revisionSeq = 1;
    nanFull.review.basisRevision = co::RevisionId::generate();
    nanFull.sectionModelVersion = std::string(kSectionModelVersion);
    ReviewReportSection s = missingSection();
    s.sectionId = "project-scheme";
    s.status = SectionStatus::Populated;
    SectionEntryView entry;
    entry.entryKey = "nan-field";
    entry.result.runId = co::RunId::generate();
    entry.result.fieldPath = "payload.x";
    FieldValue f;
    f.key = "value";
    f.quantity = co::SourcedValue<double>::provided(
        std::nan(""), co::ValueProvenance::make(co::ProvenanceKind::UserProvided));
    entry.fields.push_back(f);
    s.entries.push_back(entry);   // 条目必须挂入章节——否则 NaN 不在编码面内
    nanFull.sections.push_back(s);
    EXPECT_THROW((void)ReportCodec::encodeFull(nanFull), ReportError);

    // +Inf 同拒。
    ReportFullFields infFull = nanFull;
    infFull.sections[0].entries[0].fields[0].quantity =
        co::SourcedValue<double>::provided(
            HUGE_VAL, co::ValueProvenance::make(co::ProvenanceKind::UserProvided));
    EXPECT_THROW((void)ReportCodec::encodeFull(infFull), ReportError);

    // 有限值以 IEEE754 位模式往返（3.14 —— 位模式大端承载）。
    ReportFullFields okFull = nanFull;
    okFull.sections[0].entries[0].fields[0].quantity =
        co::SourcedValue<double>::provided(
            3.14, co::ValueProvenance::make(co::ProvenanceKind::UserProvided));
    const auto bytes = ReportCodec::encodeFull(okFull);
    const auto decoded = ReportCodec::parseFull(bytes);
    ASSERT_FALSE(decoded.sections.empty());
    ASSERT_FALSE(decoded.sections[0].entries.empty());
    ASSERT_TRUE(decoded.sections[0].entries[0].fields[0].quantity.tryValue().has_value());
    EXPECT_EQ(*decoded.sections[0].entries[0].fields[0].quantity.tryValue(), 3.14);
}

/**
 * 验证 acceptance 4/§4.4：章节按 order——未按 order 严格递增的输入在
 * 编码入口拒绝（§4.7 语义源＋编码二次把关；不做静默重排）。
 */
TEST(ReportCodecBinary, SectionsMustBeOrderIncreasing_RPT03_ACC4)
{
    ReportFullFields full;
    full.data.level = ReportLevel::B;
    full.data.revisionSeq = 1;
    full.review.basisRevision = co::RevisionId::generate();
    full.sectionModelVersion = std::string(kSectionModelVersion);
    ReviewReportSection s1 = missingSection();
    s1.sectionId = "input-summary";
    s1.order = 2;
    ReviewReportSection s2 = missingSection();
    s2.sectionId = "project-scheme";
    s2.order = 1;   // 逆序——违约
    full.sections.push_back(s1);
    full.sections.push_back(s2);
    EXPECT_THROW((void)ReportCodec::encodeFull(full), ReportError);
}

/**
 * 验证 acceptance 4/§4.4：解码边界 fail-fast——magic/版本不符、截断、
 * 尾随字节一律 DataInvalid（不做宽容归一化）。
 */
TEST(ReportCodecBinary, ParseBoundaryFailFast_RPT03_ACC4)
{
    const ReportSourceSpec spec = [] {
        ReportSourceSpec s;
        s.branch = co::BranchId::generate();
        s.level = ReportLevel::B;
        s.project = co::ProjectId::generate();
        s.revision = co::RevisionId::generate();
        s.selectedSections.push_back(SelectedSectionEntry{"ab", true});
        return s;
    }();
    const std::vector<std::uint8_t> bytes = ReportCodec::encodeData(spec);

    // magic 破坏。
    {
        std::vector<std::uint8_t> bad = bytes;
        bad[3] = 'X';
        EXPECT_THROW((void)ReportCodec::parseData(bad), ReportError);
    }
    // 版本不符（offset 8..11＝u32 版本——Data magic 8 字节后）。
    {
        std::vector<std::uint8_t> bad = bytes;
        bad[11] = 0x7F;
        EXPECT_THROW((void)ReportCodec::parseData(bad), ReportError);
    }
    // 截断。
    {
        std::vector<std::uint8_t> bad(bytes.begin(), bytes.end() - 3);
        EXPECT_THROW((void)ReportCodec::parseData(bad), ReportError);
    }
    // 尾随字节。
    {
        std::vector<std::uint8_t> bad = bytes;
        bad.push_back(0x00);
        EXPECT_THROW((void)ReportCodec::parseData(bad), ReportError);
    }
    // presence 字节非法（仅允许 0/1）。
    {
        std::vector<std::uint8_t> bad = bytes;
        bad[8 + 4 + 16] = 2;   // 头（8+4）后 branch（16）后的 presence 位
        EXPECT_THROW((void)ReportCodec::parseData(bad), ReportError);
    }
}

// =====================================================================
// token 词表（§5.3/§9.2/§6.4——报告侧新增词面的字面锁定）
// =====================================================================

/**
 * 验证 §5.3/§9.2/§6.4 token 词面与往返（RPT-T03 新增 token 函数的字面
 * 与单元卡原文一致；同码同串——NFR-COR-02）。
 */
TEST(ReportModelTokens, TokenTablesMatchDesign_RPT03)
{
    // §5.3 四态。
    EXPECT_EQ(token(SectionStatus::Populated), "populated");
    EXPECT_EQ(token(SectionStatus::NoFormalResult), "no-formal-result");
    EXPECT_EQ(token(SectionStatus::DataInsufficient), "data-insufficient");
    EXPECT_EQ(token(SectionStatus::NotApplicable), "not-applicable");
    for (const SectionStatus s :
         {SectionStatus::Populated, SectionStatus::NoFormalResult,
          SectionStatus::DataInsufficient, SectionStatus::NotApplicable}) {
        ASSERT_TRUE(trySectionStatusFromToken(token(s)).has_value());
        EXPECT_EQ(*trySectionStatusFromToken(token(s)), s);
        EXPECT_EQ(token(*trySectionStatusFromToken(token(s))), token(s));
    }
    // 非规范输入拒绝（try 轨 nullopt；抛出轨迹 DataInvalid）。
    EXPECT_FALSE(trySectionStatusFromToken("Populated").has_value());
    EXPECT_THROW((void)sectionStatusFromToken("no-such"), ReportError);

    // §9.2 渲染提示。
    EXPECT_EQ(token(RenderHint::Table), "table");
    EXPECT_EQ(token(RenderHint::CurveRef), "curve-ref");
    EXPECT_EQ(token(RenderHint::DiffTable), "diff-table");
    EXPECT_EQ(token(RenderHint::MetadataBlock), "metadata-block");

    // §6.4 限定语词表（10 词——RPT-05 措辞冻结的词面；全量逐值）。
    EXPECT_EQ(token(QualifierToken::Estimated), "estimated");
    EXPECT_EQ(token(QualifierToken::DataInsufficient), "data-insufficient");
    EXPECT_EQ(token(QualifierToken::ExternalValidationIncomplete),
              "external-validation-incomplete");
    EXPECT_EQ(token(QualifierToken::DowngradedReferenceValue),
              "downgraded-reference-value");
    EXPECT_EQ(token(QualifierToken::ScreeningOnly), "screening-only");
    EXPECT_EQ(token(QualifierToken::HistoricalSuperseded), "historical-superseded");
    EXPECT_EQ(token(QualifierToken::NotApplicable), "not-applicable");
    EXPECT_EQ(token(QualifierToken::Interrupted), "interrupted");
    EXPECT_EQ(token(QualifierToken::Canceled), "canceled");
    EXPECT_EQ(token(QualifierToken::Failed), "failed");
}

}  // namespace
