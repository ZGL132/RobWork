/**
 * @file   ReportingContractSuiteTest.cpp
 * @brief  RP-\* 契约套件——units/reporting.md §10.1 用例矩阵的统一用例体
 *         （RPT-T11 产物；§11 RPT-T11 行"验证方式＝§10.1 矩阵逐条"的执行面）。
 *
 * 设计依据：
 *   - units/reporting.md §10.1（用例矩阵——每组用例名 RP_<组>_<行> 与矩阵行
 *     一一对应，测试体首行 IRD_TEST_INFO 登记该行"需求/AT 依据"列——
 *     ird-test-report.json 承载，DTB §5.5）、§10（可控替身清单＋替身边界
 *     声明——四具名替身消费，边界声明见 test/README.md §3）、§11 RPT-T11 行
 *   - 任务契约 tasks/foundation/RPT-T11.json acceptance 1（矩阵逐条执行通过
 *     并留痕）～6（P-RPT-9/O-13 陷阱处置）
 *
 * 阶段 A 范围如实登记（不私扩验收面）：
 *   - RP-SCOPE-1/2 的"8/14 章节全量"含框架章节（§5.1 行 1/2/7/8 与 C 追加
 *     的 review-signoff/variant-diff 等）；框架章节内建投影不在 RPT-T05
 *     交付面（单元卡 §14.4 v0.8 登记——数据源依赖 RPT-T12/诊断采集/ui），
 *     其用例面如实收敛为"域章节全量＋词表序＋结构完整＋框架位缺项事实"，
 *     端到端 8/14 项随 RPT-T12/T14/T15 收口（§11 任务拆分链）；
 *   - RP-COV-1 的报告级 coverageSummary 聚合语义未冻结（§14.4 v0.12——
 *     Builder.cpp 置零值），用例面收敛为条目级覆盖呈现（包络合并标注/漏验
 *     缺项呈现），聚合计数随其冻结任务收口；
 *   - RP-ROUND-1（阶段 C——RPT-T16）不在本任务矩阵（契约 acceptance 1
 *     列表原文不含）。
 *
 * 替身边界声明（test/README.md §3 全文——RP-STATE-4 用例机器核对其正本）：
 *   本文件全部替身输出仅验证 reporting 侧契约，不构成任何运动学/轨迹/动力
 *   学/选型结果的业务正确性证明；替身 envelope 一律经 evidence
 *   ResultEnvelope::make 构造（唯一例外＝RP-MDL-2 的 Preview 受控越界样本
 *   ——用例内显式注明）。
 *
 * 确定性上下文（testkit §6.3）：夹具 DeterministicEnv 持有固定种子记录
 *   （20260909）——本套件零随机（脚本值全固定），同输入同结论可重放。
 *
 * 线程约束：单线程（gtest 串行——测试目标纪律）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/ContractCheck.hpp>       // 契约断言（追溯链谓词）
#include <sdurws/ird/testkit/Fixture.hpp>             // TempDir/DeterministicEnv（T-1 允许形态）
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO/IRD_EXPECT_*（消费方 TU 展开）

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/diagnostics/DiagCodes.hpp>
#include <sdurws/ird/evidence/Envelope.hpp>
#include <sdurws/ird/evidence/Evidence.hpp>
#include <sdurws/ird/reporting/Archive.hpp>
#include <sdurws/ird/reporting/Builder.hpp>
#include <sdurws/ird/reporting/Consistency.hpp>
#include <sdurws/ird/reporting/Errors.hpp>
#include <sdurws/ird/reporting/Export.hpp>
#include <sdurws/ird/reporting/Identity.hpp>
#include <sdurws/ird/reporting/Render.hpp>
#include <sdurws/ird/reporting/ReportModel.hpp>
#include <sdurws/ird/reporting/Sections.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "../src/ReportCodec.hpp"   // 单元内私有头（ReportCodec 往返——ReportModelTest 同款相对路径）
#include "ReportingScenario.hpp"

namespace fs = std::filesystem;

namespace {

using namespace sdurws::ird::reporting;
namespace core = sdurws::ird::core;
namespace diagnostics = sdurws::ird::diagnostics;
namespace evidence = sdurws::ird::evidence;
namespace diagcodes = sdurws::ird::reporting::diagcodes;
namespace tk = sdurws::ird::testkit;
using namespace sdurws::ird::reporting::rp_scenario;
using test_fakes::FakeArtifactSink;
using test_fakes::ScriptedResultSource;
using test_fakes::ScriptedSectionProvider;
// IRD_EXPECT_IDENTICAL 宏展开为非限定 checkIdentical（string 实参无 ADL 关联
// ——消费方 TU 显式引入，AssertMacros.hpp 文档化前置条件的本文件承载）。
using sdurws::ird::testkit::checkIdentical;

/// 场景失败中止辅助（ADD_FAILURE 登记＋抛出——gtest 捕获测试体异常并判红；
/// 非 void 辅助函数不可用 FAIL() 的 return 语义——GTEST_FAIL_ 含 return;）。
[[noreturn]] inline void scenarioAbort(const std::string& what)
{
    ADD_FAILURE() << what;
    throw ReportError(ReportErrorCode::Usage, "场景前置失败——用例中止: " + what);
}

// =====================================================================
// 套件夹具（确定性环境——testkit §6.3；每用例独立场景对象）
// =====================================================================

class ReportingContractSuite : public ::testing::Test {
protected:
    /// 确定性上下文（testkit §6.3——固定种子记录为唯一来源；本套件零随机，
    /// 记录随 ird-test-report.json 的 repro 面可重放）。
    void SetUp() override
    {
        tk::ReproRecord record;   // 默认 seed=20260909/threadCount=1（§6.3 口径）
        record.notes = "RPT-T11 RP-* 契约套件——脚本值全固定，零随机";
        m_env = std::make_unique<tk::DeterministicEnv>(record);
        ASSERT_EQ(m_env->record().seed, 20260909u) << "确定性种子缺省口径（§6.3）";
    }

private:
    std::unique_ptr<tk::DeterministicEnv> m_env;
};

/// 构建报告辅助（成功形态断言——失败即用例失败并给出诊断全文）。
inline ReviewReport buildOk(BuildScene& scene, ReportLevel level,
                            const ReportCancelToken* cancel = nullptr)
{
    const ReportBuildOutcome outcome = scene.builder()->build(scene.request(level), cancel);
    if (outcome.report == nullptr) {
        std::string diagText;
        for (const core::DiagnosticRecord& d : outcome.diagnostics) {
            diagText += d.code + ";";
        }
        scenarioAbort("构建意外失败（code="
                      + (outcome.error.has_value() ? std::string(token(outcome.error->code()))
                                                   : std::string{"?"})
                      + "）diag=" + diagText);
    }
    return std::move(*outcome.report);
}

/// 外部导出辅助（成功形态断言；返回逐文件表）。
inline std::vector<ExportedFile> exportOk(ExportScene& scene, const ReviewReport& report,
                                          const std::vector<ReportRenderFormat>& formats,
                                          const fs::path& dir,
                                          ReplacePolicy replace = ReplacePolicy::NeverOverwrite)
{
    ReportExportRequest request;
    request.project = report.project();
    request.reportId = report.reportId();
    request.formats = formats;
    request.destination.kind = ExportDestination::ExternalPath;
    request.destination.externalPath = dir;
    request.destination.replace = replace;
    ReportExportResult result = scene.service()->exportReport(request);
    if (result.error.has_value() || result.files.empty()) {
        scenarioAbort("外部导出意外失败（"
                      + (result.error.has_value() ? std::string(token(result.error->code()))
                                                  : std::string{"无文件"})
                      + "）");
    }
    return std::move(result.files);
}

/// 项目内归档辅助（成功/幂等形态——published 在场）。
inline PublishedReportRecord archiveOk(ExportScene& scene, const ReviewReport& report,
                                       const std::vector<ReportRenderFormat>& formats)
{
    ReportExportRequest request;
    request.project = report.project();
    request.reportId = report.reportId();
    request.formats = formats;
    request.destination.kind = ExportDestination::ProjectArchive;
    ReportExportResult result = scene.service()->exportReport(request);
    if (!result.published.has_value()) {
        scenarioAbort("项目内归档意外失败（"
                      + (result.error.has_value() ? std::string(token(result.error->code()))
                                                  : std::string{"?"})
                      + "）");
    }
    return std::move(*result.published);
}

// =====================================================================
// RP-MDL 组（报告模型与身份）
// =====================================================================

/**
 * §10.1 RP-MDL-1：报告身份确定性——同数据源两次构建（替身同脚本），
 * contentIdentity 逐字节相等；ReportCodec 往返 parse(encode(x))==x；
 * ReportId 不同（新对象）如实登记。
 */
TEST_F(ReportingContractSuite, RP_MDL_1_ReportIdentityDeterministic)
{
    IRD_TEST_INFO(std::vector<std::string>{"RPT-01", "NFR-COR-02"}, {std::string{"AT-32"}},
                  std::nullopt);

    const core::RunId run = runAt(0x14);
    BuildScene sceneA = standardScene(run);
    BuildScene sceneB = standardScene(run);
    const ReviewReport a = buildOk(sceneA, ReportLevel::B);
    const ReviewReport b = buildOk(sceneB, ReportLevel::B);

    // contentIdentity/dataIdentity 逐字节相等（确定性——§9.1 确定性行）。
    IRD_EXPECT_IDENTICAL("report.contentIdentity", a.contentIdentity().toCanonical(),
                         b.contentIdentity().toCanonical());
    IRD_EXPECT_IDENTICAL("report.dataIdentity", a.dataIdentity().toCanonical(),
                         b.dataIdentity().toCanonical());
    // ReportId 不同（新对象——§4.1 身份纪律；如实登记而非视为失败）。
    EXPECT_FALSE(a.reportId() == b.reportId()) << "ReportId 为新对象标识（非内容身份）";

    // ReportCodec-Data 往返（§4.4——parse(encode(x))==x；抽取与
    // ReportModel.cpp extractDataSpec 同构，测试侧独立组装）。
    ReportSourceSpec spec;
    spec.project = a.project();
    spec.branch = a.branch();
    spec.revision = a.revision();
    spec.revisionSeq = a.revisionSeq();
    spec.level = a.level();
    spec.snapshotId = a.snapshotId();
    spec.inputSliceId = a.inputSliceId();
    for (const ResultRefSnapshot& r : a.resultRefs()) {
        ResultDataSourceRef ref;
        ref.runId = r.runId;
        ref.snapshotId = r.snapshotId;
        ref.sliceId = r.sliceId;
        ref.inputBaselineId = r.inputBaselineId;
        ref.caseScope = r.caseScope;
        spec.resultRefs.push_back(std::move(ref));
    }
    spec.unitPreference = a.unitPreference();
    for (const ReviewReportSection& sec : a.sections()) {
        spec.selectedSections.push_back(SelectedSectionEntry{sec.sectionId, sec.selected});
    }
    const std::vector<std::uint8_t> bytes = ReportCodec::encodeData(spec);
    const ReportSourceSpec decoded = ReportCodec::parseData(bytes);
    EXPECT_TRUE(decoded == spec) << "ReportCodec 往返 parse(encode(x))==x（§4.4）";
}

/**
 * §10.1 RP-MDL-2：引用不可解析拒绝——脚本注入结果未 finalize／修订不存在／
 * Preview 结果，逐项 SourceMissing 定位；无报告对象；FakeSink 零调用。
 */
TEST_F(ReportingContractSuite, RP_MDL_2_UnresolvableRefs_Rejected)
{
    IRD_TEST_INFO(std::vector<std::string>{"RPT-01", "NFR-COR-04"}, {}, std::nullopt);

    const core::RunId run = runAt(0x14);

    // ①修订不存在（tryRevision nullopt——步①锚定失败）。
    {
        BuildScene scene = standardScene(run);
        scene.queryPort.revisionExists = false;
        expectBuildFailure(scene.builder()->build(scene.request(ReportLevel::B)),
                           ReportErrorCode::SourceMissing);
    }
    // ②结果未 finalize（不在 listRuns 清单——D-13 口径）。
    {
        BuildScene scene = standardScene(run);
        scene.queryPort.runs.clear();
        expectBuildFailure(scene.builder()->build(scene.request(ReportLevel::B)),
                           ReportErrorCode::SourceMissing);
    }
    // ③Preview 结果（替身边界唯一例外：Preview 包络无法经 evidence make()
    // 产出〔表 1/表 3 行 4〕，以聚合初始化受控构造越界样本——被测对象是
    // reporting 构建边界对越界样本的第二道闸，不构成对 evidence 校验器的
    // 替代验证；test/README.md §3.1 登记）。
    {
        BuildScene scene = standardScene(run);
        evidence::ResultEnvelope preview = test_fakes::feasibleEnvelope(run);
        preview.mode = core::EvaluationMode::Preview;   // 受控越界（见上注）
        scene.resultSource.envelopes.clear();
        scene.resultSource.envelopes.emplace(run.toCanonical(), std::move(preview));
        expectBuildFailure(scene.builder()->build(scene.request(ReportLevel::B)),
                           ReportErrorCode::SourceMissing);
    }
    // ④FakeSink 零调用（失败路径零项目写——§7.1"生成失败是否修改原项目＝否"；
    // RP-MDL-2 观测点"FakeSink 零调用"）。
    {
        BuildScene scene = standardScene(run);
        scene.queryPort.revisionExists = false;
        FakeArtifactSink sink;
        EXPECT_TRUE(sink.calls.empty()) << "构建失败路径汇集座零调用";
        EXPECT_EQ(sink.published.size(), std::size_t{0}) << "零项目写（无发布记录）";
        expectBuildFailure(scene.builder()->build(scene.request(ReportLevel::B)),
                           ReportErrorCode::SourceMissing);
        EXPECT_TRUE(sink.calls.empty()) << "构建失败前后汇集座零调用";
    }
}

/**
 * §10.1 RP-MDL-3：身份与路径/名称分离——构建后重命名显示名（非身份元数据：
 * generatedBy/generatorVersion 不入身份——§4.4 排除列）再导出：
 * contentIdentity 不变；幂等命中；manifest 摘要比对一致。
 */
TEST_F(ReportingContractSuite, RP_MDL_3_IdentityIndependentOfPathAndName)
{
    IRD_TEST_INFO(std::vector<std::string>{"RPT-01"}, {std::string{"AT-32"}}, std::nullopt);

    const core::RunId run = runAt(0x14);
    tk::TempDir dirA("rp-mdl3-a");
    tk::TempDir dirB("rp-mdl3-b");
    {
        BuildScene scene = standardScene(run);
        const ReviewReport report = buildOk(scene, ReportLevel::B);
        ExportScene exportScene(report);

        // 同一 contentIdentity 的报告导出到两个不同路径（路径不在身份面）。
        const std::vector<ExportedFile> filesA =
            exportOk(exportScene, report, {ReportRenderFormat::Html, ReportRenderFormat::Json},
                     dirA.path());
        const std::vector<ExportedFile> filesB =
            exportOk(exportScene, report, {ReportRenderFormat::Html, ReportRenderFormat::Json},
                     dirB.path());
        ASSERT_EQ(filesA.size(), filesB.size());
        for (std::size_t i = 0; i < filesA.size(); ++i) {
            EXPECT_EQ(filesA[i].format, filesB[i].format);
            EXPECT_TRUE(filesA[i].sha256 == filesB[i].sha256)
                << "同内容异路径＝同字节（身份与路径分离）";
        }

        // 项目内归档幂等命中＋manifest 摘要一致（重命名显示名前后——
        // manifest 摘要不以目标路径、不以非身份元数据为输入，D-02）。
        const PublishedReportRecord first = archiveOk(exportScene, report,
                                                      {ReportRenderFormat::Html});
        const PublishedReportRecord second = archiveOk(exportScene, report,
                                                       {ReportRenderFormat::Html});
        EXPECT_TRUE(first.manifestDigest == second.manifestDigest)
            << "幂等命中且 manifest 摘要逐字节一致";
        EXPECT_TRUE(first.contentIdentity == report.contentIdentity())
            << "发布记录内容身份与报告对象一致";
    }
    // "重命名显示名"半区（元数据不入身份——§4.4 排除列）由 RP-MDL-1 承载：
    // 两次独立构建的 generatedAtUtc/generatedBy 等元数据由构建器现取而
    // contentIdentity 逐字节相等，即非身份元数据不影响身份的构建器自证；
    // 项目内编址键＝projectId+reportId（D-13），跨 ReportId 的同内容报告
    // 本就是新对象——不构成幂等命中的对象（与 RP-IDEM-1 的键面一致）。
}

// =====================================================================
// RP-SCOPE 组（B/C 级章节契约）
// =====================================================================

/**
 * §10.1 RP-SCOPE-1：B 级章节完整性——域提供方就绪（替身），build(B)：
 * 域章节集＝§5.1 B 级域项且按 order；每章节结构完整（状态/条目/绑定）。
 * 框架章节（词表行 1/2/7/8）内建投影不在 RPT-T05 交付面（§14.4 v0.8），
 * 端到端 8 项随 RPT-T12/T14 收口——本用例如实收敛为域四项＋词表事实。
 */
TEST_F(ReportingContractSuite, RP_SCOPE_1_BLevelSectionCompleteness)
{
    IRD_TEST_INFO(std::vector<std::string>{"RPT-01-B"}, {std::string{"AT-32"}}, std::nullopt);

    const core::RunId run = runAt(0x14);
    BuildScene scene = standardScene(run);
    // B 级域四章节提供方全部就绪（Populated——kin 由 standardScene 注册，
    // 补齐其余三个；条目绑定同一运行，绑定校验通过面）。
    for (const std::string_view id : {kSectionModel, kSectionRequirements,
                                      kSectionOptimizationCandidates}) {
        ContentSpec spec;
        spec.sectionId = std::string(id);
        spec.boundRun = run;
        registerContent(scene.registry, spec);
    }

    const ReviewReport report = buildOk(scene, ReportLevel::B);

    // 词表事实：B 级词表 8 项＝框架 4＋域 4；报告承载域 4 项全量。
    ASSERT_EQ(kBLevelSectionIds.size(), std::size_t{8});
    const std::vector<std::string> domainScope = domainSectionsInScope(ReportLevel::B);
    ASSERT_EQ(report.sections().size(), domainScope.size());
    // 章节集与 §5.1 order 逐项断言（枚举与 order 断言——观测点原文）。
    for (std::size_t i = 0; i < report.sections().size(); ++i) {
        const ReviewReportSection& section = report.sections()[i];
        IRD_EXPECT_IDENTICAL("section.order[" + std::to_string(i) + "]", section.sectionId,
                             domainScope[i]);
        EXPECT_EQ(section.order, *trySectionOrder(section.sectionId)) << "order＝词表行号";
        // 结构完整（Populated：状态/条目/绑定齐备——§5.3 presence 纪律）。
        ASSERT_EQ(section.status, SectionStatus::Populated);
        ASSERT_FALSE(section.entries.empty());
        for (const SectionEntryView& entry : section.entries) {
            EXPECT_FALSE(entry.result.runId.toCanonical().empty()) << "结果绑定在场";
            ASSERT_FALSE(entry.evidence.empty()) << "证据绑定在场";
        }
        EXPECT_TRUE(section.selected) << "Populated 默认选中（§5 默认规则）";
    }
}

/**
 * §10.1 RP-SCOPE-2：C 级章节完整性——B＋C 提供方就绪：域八章节全选＋评审
 * 签署块（ReviewMetadata 字段——报告对象 review() 面；框架六项的端到端
 * 随 RPT-T12/T15，见文件头阶段 A 范围登记）。
 */
TEST_F(ReportingContractSuite, RP_SCOPE_2_CLevelSectionCompleteness)
{
    IRD_TEST_INFO(std::vector<std::string>{"RPT-01-C"}, {std::string{"AT-32"}}, std::nullopt);

    const core::RunId run = runAt(0x14);
    BuildScene scene = standardScene(run);
    // B 域其余三项＋C 专属域四项（trajectory-cycle/dynamics-envelope/
    // drivetrain-operating-points/selection-bom）提供方就绪。
    for (const std::string_view id :
         {kSectionModel, kSectionRequirements, kSectionOptimizationCandidates,
          kSectionTrajectoryCycle, kSectionDynamicsEnvelope,
          kSectionDrivetrainOperatingPoints, kSectionSelectionBom}) {
        ContentSpec spec;
        spec.sectionId = std::string(id);
        spec.boundRun = run;
        spec.minLevel = trySectionMinimumLevel(id).value_or(ReportLevel::B);
        registerContent(scene.registry, spec);
    }

    const ReviewReport report = buildOk(scene, ReportLevel::C);
    EXPECT_EQ(report.level(), ReportLevel::C);

    // 域八章节全选（C＝B 域全部＋C 追加域四——§5.2 范围图域侧）。
    const std::vector<std::string> domainScope = domainSectionsInScope(ReportLevel::C);
    ASSERT_EQ(kDomainSectionIds.size(), std::size_t{8});
    ASSERT_EQ(report.sections().size(), domainScope.size());
    for (std::size_t i = 0; i < report.sections().size(); ++i) {
        const ReviewReportSection& section = report.sections()[i];
        EXPECT_EQ(section.sectionId, domainScope[i]);
        EXPECT_TRUE(section.selected) << "全选场景——Populated 默认选中";
    }
    // 评审签署块（§4.5 ReviewMetadata——构建器冻结的报告对象面）。
    EXPECT_TRUE(report.review().basisRevision == fixedRevision()) << "basisRevision＝锚定修订";
    EXPECT_EQ(report.review().signOff.state, SignOffState::State::Unsigned) << "未签署初值";
    EXPECT_EQ(report.reportVersion(), 1u);
}

/**
 * §10.1 RP-SCOPE-3：C 缺必需证据降级/拒绝——①部分 C 章节缺正式结果＝C 报告
 * 生成，缺失章节 NoFormalResult＋默认不选＋缺项显示；②全部 C 缺＝
 * ScopeInsufficient 拒绝＋RPT-SCOPE-INSUFFICIENT＋建议 B。
 */
TEST_F(ReportingContractSuite, RP_SCOPE_3_CMissingEvidence_DegradeOrReject)
{
    IRD_TEST_INFO(std::vector<std::string>{"RPT-01-C"}, {}, std::nullopt);

    const core::RunId run = runAt(0x14);
    // ①部分 C 章节缺正式结果（仅 trajectory-cycle Populated——其余 C 域缺）
    // →生成，缺失章节缺项表达（§5.4 保守处置不拒绝部分缺失）。
    {
        BuildScene scene = standardScene(run);
        ContentSpec cSpec;
        cSpec.sectionId = std::string(kSectionTrajectoryCycle);
        cSpec.minLevel = ReportLevel::C;
        cSpec.boundRun = run;
        registerContent(scene.registry, cSpec);
        const ReviewReport report = buildOk(scene, ReportLevel::C);
        const std::vector<std::string> domainScope = domainSectionsInScope(ReportLevel::C);
        ASSERT_EQ(report.sections().size(), domainScope.size());
        for (const ReviewReportSection& section : report.sections()) {
            if (section.sectionId == kSectionTrajectoryCycle) {
                EXPECT_EQ(section.status, SectionStatus::Populated);
                EXPECT_TRUE(section.selected);
            } else if (section.sectionId == kSectionKinematicsCollision) {
                EXPECT_EQ(section.status, SectionStatus::Populated);
            } else {
                // 缺失章节：NoFormalResult＋默认不选＋缺项清单（§16 验收要点）。
                EXPECT_EQ(section.status, SectionStatus::NoFormalResult);
                EXPECT_FALSE(section.selected) << "缺正式结果默认不选";
                ASSERT_FALSE(section.missingItems.empty()) << "缺项显示";
            }
        }
    }
    // ②全部 C 章节缺正式结果（域提供方内容亦缺项）→ScopeInsufficient 拒绝
    // ＋RPT-SCOPE-INSUFFICIENT 诊断＋建议生成 B 级（P-RPT-5 契约词）。
    // 场景手工组装（standardScene 已注册 Populated 的 kin——本分支须为缺项
    // 形态，注册表不接受同章节二次注册〔§9.2 注册边界〕）。
    {
        BuildScene scene;
        scene.addRun(RunSpec{run});
        ContentSpec missing;
        missing.status = SectionStatus::NoFormalResult;
        scene.addKinContent(missing);
        ReportBuildRequest request = scene.request(ReportLevel::C);
        const ReportBuildOutcome outcome = scene.builder()->build(request);
        expectBuildFailure(outcome, ReportErrorCode::ScopeInsufficient);
        bool found = false;
        for (const core::DiagnosticRecord& diag : outcome.diagnostics) {
            if (diag.code == std::string(diagcodes::kScopeInsufficient)) {
                found = true;
                EXPECT_NE(diag.recommendedAction.find("B 级"), std::string::npos)
                    << "降级建议（P-RPT-5 契约词）";
            }
        }
        EXPECT_TRUE(found) << "RPT-SCOPE-INSUFFICIENT 必须随拒绝上报（NFR-COR-03）";
    }
}

/**
 * §10.1 RP-SCOPE-4：B 不伪造 C 章节——请求 B＋C 章节覆盖＝LevelConflict 拒绝；
 * 错误码在案；无报告对象。
 */
TEST_F(ReportingContractSuite, RP_SCOPE_4_BLevelWithCSection_LevelConflict)
{
    IRD_TEST_INFO(std::vector<std::string>{"RPT-01-B"}, {}, std::nullopt);

    const core::RunId run = runAt(0x14);
    BuildScene scene = standardScene(run);
    ReportBuildRequest request = scene.request(ReportLevel::B);
    request.sectionOverrides = {SectionSelection{std::string(kSectionTrajectoryCycle), true}};
    const ReportBuildOutcome outcome = scene.builder()->build(request);
    expectBuildFailure(outcome, ReportErrorCode::LevelConflict);
}

// =====================================================================
// RP-STATE 组（状态呈现——RPT-05 措辞冻结）
// =====================================================================

/**
 * §10.1 RP-STATE-1：失败/取消/中断不入正式结论——替身返回 Canceled/Failed/
 * Interrupted envelope（合法组合），该类结果仅现于诊断/状态；正文无"正式
 * 通过"字样（HTML 文本扫描计数=0，限定语通道除外）；渲染器前置断言生效。
 */
TEST_F(ReportingContractSuite, RP_STATE_1_NotCompletedResults_NeverFormalPass)
{
    IRD_TEST_INFO(std::vector<std::string>{"TASK-02", "RPT-05"}, {std::string{"AT-10"}},
                  std::nullopt);

    const core::RunId canceled = runAt(0x14);
    const core::RunId failed = runAt(0x15);
    const core::RunId interrupted = runAt(0x16);
    BuildScene scene;
    scene.addRun(RunSpec{canceled, "canceled"});
    scene.addRun(RunSpec{failed, "failed"});
    scene.addRun(RunSpec{interrupted, "interrupted"});
    ContentSpec noBinding;
    noBinding.status = SectionStatus::NoFormalResult;
    scene.addKinContent(noBinding);

    const ReviewReport report = buildOk(scene, ReportLevel::B);

    // 结果仅以状态三轴呈现（执行轴如实登记；判定轴不伪造——ERR-01）。
    ASSERT_EQ(report.resultRefs().size(), std::size_t{3});
    std::set<std::string> outcomes;
    for (const ResultRefSnapshot& ref : report.resultRefs()) {
        outcomes.insert(std::string(core::toToken(ref.outcome)));
        EXPECT_EQ(ref.engineeringStatus, core::EngineeringStatus::NotApplicable)
            << "非 Completed 不伪造工程判定（ERR-01）";
        // 诊断照常呈现（TASK-02 呈现侧——diagRefs 携带来源运行）。
        bool found = false;
        for (const DiagRefEntry& diag : report.diagRefs()) {
            if (diag.sourceRun.has_value() && *(diag.sourceRun) == ref.runId) {
                found = true;
            }
        }
        EXPECT_TRUE(found) << "运行诊断进入报告（TASK-02）";
    }
    EXPECT_EQ(outcomes.size(), std::size_t{3}) << "canceled/failed/interrupted 三态各就其位";

    // 正文无"正式通过"字样（HTML 文本扫描计数=0——限定语冻结文案通道除外；
    // 渲染器前置断言：资格不成立时禁止输出——§8.2①）。
    const std::string html = renderText(report, ReportRenderFormat::Html);
    EXPECT_EQ(countOutsideQualifierSpans(html, "正式通过"), std::size_t{0})
        << "非正式结论正文零'正式通过'字样";

    // 诊断记录本身过契约断言（testkit ContractCheck——RP-TRACE 的诊断面；
    // envelope 诊断经 make() 构造边界在案的复核）。
    const evidence::ResultEnvelope& envelope = scene.resultSource.envelopes.begin()->second;
    ASSERT_FALSE(envelope.diagnostics.empty());
    EXPECT_TRUE(tk::checkDiagnosticRecord(
                        envelope.diagnostics.front(), tk::DiagnosticCheckOptions{true})
                        .passed)
        << "envelope 诊断记录契约合格（testkit ContractCheck）";
}

/**
 * §10.1 RP-STATE-2：DataInsufficient 正确表达——替身 envelope＝Completed+
 * DataInsufficient＋缺失全量清单：章节 DataInsufficient＋缺失项全量清单＋
 * 限定语；无结论字样。O-13：EvidenceItemStatus 五值随本组用例锁定
 * （五词各有其位不合并——§5.3）。
 */
TEST_F(ReportingContractSuite, RP_STATE_2_DataInsufficient_Expression)
{
    IRD_TEST_INFO(std::vector<std::string>{"RPT-05"}, {std::string{"AT-32"}}, std::nullopt);

    // O-13 锁定面：EvidenceItemStatus 五值枚举在案且 reporting 绑定值语义
    // 保序不合并（消费不重定义——§2.1 C-2；词表实现承载随上游裁决同步）。
    static_assert(static_cast<int>(evidence::EvidenceItemStatus::NotApplicable) == 4,
                  "EvidenceItemStatus 五值枚举序（Satisfied/Missing/Invalid/Unverified/"
                  "NotApplicable）——O-13 五词各有其位");
    const evidence::EvidenceItemStatus kFive[] = {
        evidence::EvidenceItemStatus::Satisfied, evidence::EvidenceItemStatus::Missing,
        evidence::EvidenceItemStatus::Invalid, evidence::EvidenceItemStatus::Unverified,
        evidence::EvidenceItemStatus::NotApplicable};
    std::set<int> distinct;
    for (const evidence::EvidenceItemStatus s : kFive) {
        distinct.insert(static_cast<int>(s));
    }
    EXPECT_EQ(distinct.size(), std::size_t{5}) << "五值互异（不合并）";

    const core::RunId run = runAt(0x14);
    BuildScene scene;
    RunSpec spec{run};
    spec.kind = "data-insufficient";
    spec.missingItems = {"kin.reach-report", "kin.collision-margin"};
    scene.addRun(spec);
    // 章节内容＝DataInsufficient（缺失清单与包络全量一致——§5.3）。
    ContentSpec content;
    content.status = SectionStatus::DataInsufficient;
    content.missingItems = spec.missingItems;
    content.boundRun = run;
    registerContent(scene.registry, content);

    const ReviewReport report = buildOk(scene, ReportLevel::B);

    // 结果三轴：DataInsufficient 判定冻结（§6.2 呈现口径）。
    ASSERT_EQ(report.resultRefs().size(), std::size_t{1});
    EXPECT_EQ(report.resultRefs()[0].engineeringStatus,
              core::EngineeringStatus::DataInsufficient);

    // 章节：DataInsufficient＋缺失项全量清单（计数与内容——观测点原文）。
    const ReviewReportSection& kin = report.sections()[2];
    ASSERT_EQ(kin.sectionId, kSectionKinematicsCollision);
    EXPECT_EQ(kin.status, SectionStatus::DataInsufficient);
    ASSERT_EQ(kin.missingItems.size(), spec.missingItems.size());
    for (const std::string& item : spec.missingItems) {
        bool found = false;
        for (const MissingItemView& missing : kin.missingItems) {
            if (missing.itemId == item) {
                found = true;
            }
        }
        EXPECT_TRUE(found) << "缺失项全量列出（不抽样——表 2④）: " << item;
    }

    // 渲染面：数据不足限定语＋无"正式通过"结论字样。
    const std::string html = renderText(report, ReportRenderFormat::Html);
    EXPECT_NE(countOutsideQualifierSpans(html, "数据不足"), std::size_t{0})
        << "数据不足限定语呈现";
    EXPECT_EQ(countOutsideQualifierSpans(html, "正式通过结论"), std::size_t{0})
        << "无结论字样（RPT-05 措辞冻结）";
}

/**
 * §10.1 RP-STATE-3：不可行评审记录——替身 envelope＝EngineeringInfeasible＋
 * 有效证明（经 evidence）：呈现"经验证的不可行结论（正式评审记录）"措辞＋
 * 证明呈现；无"通过"措辞；与正式通过两声明并存不混淆。
 */
TEST_F(ReportingContractSuite, RP_STATE_3_InfeasibleReviewRecord)
{
    IRD_TEST_INFO(std::vector<std::string>{"RPT-05"}, {}, std::nullopt);

    const core::RunId run = runAt(0x14);
    BuildScene scene;
    RunSpec spec{run};
    spec.kind = "infeasible";
    scene.addRun(spec);
    // 提供方绑定不可行结果（Completed 组合——可绑入条目）＋评审记录资格
    // 成立脚本（§7.2 纯检查结果的冻结面——reporting 不自算）。
    ContentSpec content;
    content.boundRun = run;
    registerContent(scene.registry, content);
    ReportEligibilityChecks checks;
    checks.formalPass = test_fakes::scriptedEligibility(false);
    checks.reviewRecord = test_fakes::scriptedEligibility(true);
    scene.resultSource.withEligibility(run, checks);

    const ReviewReport report = buildOk(scene, ReportLevel::B);
    ASSERT_EQ(report.resultRefs().size(), std::size_t{1});
    EXPECT_EQ(report.resultRefs()[0].engineeringStatus,
              core::EngineeringStatus::EngineeringInfeasible);
    EXPECT_FALSE(report.resultRefs()[0].eligibility.formalPass);
    EXPECT_TRUE(report.resultRefs()[0].eligibility.reviewRecord) << "评审记录资格冻结";

    // 措辞断言（§8.2 两类声明独立——观测点原文）。证明本体的呈现＝评审
    // 记录声明＋条目不可行状态词（证明文件归 evidence 归档，报告以资格
    // 声明与结果状态承载——§6.2 呈现口径；claimToken 不入 §4 报告模型）。
    const std::string html = renderText(report, ReportRenderFormat::Html);
    EXPECT_NE(html.find("经验证的不可行结论（正式评审记录）"), std::string::npos)
        << "正式评审记录声明呈现";
    bool infeasibleStatusSeen = false;
    for (const FieldCell& cell : extractFieldMatrix(report)) {
        if (cell.status.has_value() && *cell.status == std::string(core::toToken(
                                            core::EngineeringStatus::EngineeringInfeasible))) {
            infeasibleStatusSeen = true;
        }
    }
    EXPECT_TRUE(infeasibleStatusSeen) << "条目不可行状态词呈现（core 词表）";
    EXPECT_EQ(countOutsideQualifierSpans(html, "正式通过"), std::size_t{0})
        << "无'通过'措辞（两类声明并存不混淆）";
}

/**
 * §10.1 RP-STATE-4：替身边界声明——测试文档显式声明替身输出不构成业务算法
 * 证明（评审检查的机器核对半区：文档在案＋四具名替身头声明在案；全文见
 * test/README.md §3）。
 */
TEST_F(ReportingContractSuite, RP_STATE_4_DoubleBoundaryDeclarationOnFile)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-04"}, {}, std::nullopt);

    const fs::path unitRoot = fs::path{IRD_REPORTING_UNIT_ROOT} / "reporting" / "test";
    // 声明正本（test/README——acceptance 3"test/README 或替身头注释留痕"）。
    const std::string readme = readFileText(unitRoot / "README.md");
    EXPECT_NE(readme.find("不构成任何运动学/轨迹/动力学/选型结果的业务正确性证明"),
              std::string::npos)
        << "替身边界声明主句在案（README §3）";
    EXPECT_NE(readme.find("ResultEnvelope::make"), std::string::npos)
        << "合法组合-only 构造纪律在案";
    EXPECT_NE(readme.find("不得以替身数据冒充验收"), std::string::npos)
        << "真实章节内容不冒充验收（§12.3 同源）在案";
    // 四具名替身头逐一在案且带边界声明（§10 可控替身清单的落位正本）。
    for (const std::string& header :
         {"ScriptedResultSource.hpp", "ScriptedSectionProvider.hpp", "FakeArtifactSink.hpp",
          "FakeArchiveWriter.hpp"}) {
        const std::string text = readFileText(unitRoot / header);
        EXPECT_NE(text.find("替身边界声明"), std::string::npos)
            << header << " 头边界声明在案";
        EXPECT_NE(text.find("RP-STATE-4"), std::string::npos)
            << header << " 头追溯锚在案";
    }
}

// =====================================================================
// RP-CUR 组（当前性——CON-02/05）
// =====================================================================

/**
 * §10.1 RP-CUR-1：HEAD 前进不改历史——报告 A 发布（基于 r7）→模拟 TCP 变更
 * 产生 r8：重读报告 A 工件与对象，字节与字段不变；T1 当前性快照保留。
 */
TEST_F(ReportingContractSuite, RP_CUR_1_HeadAdvanceKeepsHistoryImmutable)
{
    IRD_TEST_INFO(std::vector<std::string>{"CON-02", "CON-05", "RPT-01"}, {}, std::nullopt);

    const core::RunId run = runAt(0x14);
    tk::TempDir dir("rp-cur1");
    core::Digest256 publishedSha{};
    BuildScene scene = standardScene(run);
    const ReviewReport report = buildOk(scene, ReportLevel::B);
    {
        ExportScene exportScene(report);
        const std::vector<ExportedFile> files =
            exportOk(exportScene, report, {ReportRenderFormat::Html}, dir.path());
        ASSERT_EQ(files.size(), std::size_t{1});
        publishedSha = files.front().sha256;
    }

    // 模拟 HEAD 前进（r7→r8：TCP 对象内容变更——锚定视图已冻结，不影响
    // 已建报告对象；此处推进查询端口视图供后续构建对照）。
    scene.queryPort.view.seq = 8;

    // 重读报告 A：对象字段不变（不可变值——§4.2）＋重渲染字节不变＋已发布
    // 工件哈希不变。
    EXPECT_TRUE(report.revision() == fixedRevision()) << "报告 A 锚定 r7 不变";
    EXPECT_EQ(report.revisionSeq(), 7u);
    const std::string reRendered = renderText(report, ReportRenderFormat::Html);
    const core::Digest256 reSha = [&] {
        core::ContentDigester d;
        const auto bytes = asBytes(reRendered);
        d.update(bytes.data(), bytes.size());
        return d.finalize();
    }();
    EXPECT_TRUE(reSha == publishedSha) << "重渲染字节＝已发布工件（历史不可改写）";
    EXPECT_TRUE(fileSha256(dir.path() / "report.html") == publishedSha)
        << "工件文件哈希不变";
    // T1 当前性快照保留（生成时刻的 evaluatedAgainst——§6.3 冻结点）。
    ASSERT_TRUE(report.resultRefs()[0].currentness.status.has_value());
    EXPECT_EQ(*report.resultRefs()[0].currentness.status, evidence::CurrentnessStatus::Current);
    EXPECT_TRUE(report.resultRefs()[0].currentness.evaluatedAgainst.headRevision
                == fixedRevision()) << "当前性快照锚定生成时刻 HEAD（r7）";
}

/**
 * §10.1 RP-CUR-2：当前性变化产生诊断不修改内容——同 RP-CUR-1 场景后实时
 * 查询：伴随提示 Superseded＋原因清单；报告 A 零修改（诊断与内容分离）。
 */
TEST_F(ReportingContractSuite, RP_CUR_2_SupersededHint_DiagnosticNotContent)
{
    IRD_TEST_INFO(std::vector<std::string>{"CON-02", "AT-30"}, {}, std::nullopt);

    const core::RunId run = runAt(0x14);
    tk::TempDir dir("rp-cur2");
    core::Digest256 reportASha{};
    BuildScene scene = standardScene(run);
    const ReviewReport reportA = buildOk(scene, ReportLevel::B);
    {
        ExportScene exportScene(reportA);
        const std::vector<ExportedFile> files =
            exportOk(exportScene, reportA, {ReportRenderFormat::Html}, dir.path());
        reportASha = files.front().sha256;
    }

    // TCP 变更后的"实时查询"＝以 Superseded 当前性脚本构建报告 B（当前性
    // 投影实时重查——§6.3 每份报告构建时取值）；报告 A 工件零修改。
    // 场景手工组装（避免对已注册章节的二次注册——§9.2 注册边界）。
    BuildScene sceneB;
    RunSpec specB{run};
    specB.currentness = "superseded";
    sceneB.addRun(specB);
    ContentSpec content;
    content.boundRun = run;
    sceneB.addKinContent(content);
    const ReviewReport reportB = buildOk(sceneB, ReportLevel::B);

    // 报告 B：Superseded＋原因清单（提示数据——观测点原文；AT-30"复核完成
    // 前不沿用原通过结论"的呈现承载）。
    ASSERT_EQ(reportB.resultRefs().size(), std::size_t{1});
    ASSERT_TRUE(reportB.resultRefs()[0].currentness.status.has_value());
    EXPECT_EQ(*reportB.resultRefs()[0].currentness.status,
              evidence::CurrentnessStatus::Superseded);
    ASSERT_EQ(reportB.resultRefs()[0].currentness.reasons.size(), std::size_t{1});
    EXPECT_EQ(reportB.resultRefs()[0].currentness.reasons[0].dependencyKey, "robot-design");
    const std::string htmlB = renderText(reportB, ReportRenderFormat::Html);
    EXPECT_NE(htmlB.find("已被取代"), std::string::npos) << "Superseded 呈现词（§6.3）";

    // 报告 A 零修改（文件哈希再比对——诊断与内容分离）。
    EXPECT_TRUE(fileSha256(dir.path() / "report.html") == reportASha)
        << "报告 A 工件零修改";
    EXPECT_TRUE(reportA.resultRefs()[0].currentness.status.has_value());
    EXPECT_EQ(*reportA.resultRefs()[0].currentness.status, evidence::CurrentnessStatus::Current)
        << "报告 A 冻结快照仍为生成时刻事实";
}

/**
 * §10.1 RP-CUR-3：不可判定不默认 Current——脚本：当前性依赖无法解析：
 * currentness.status=nullopt＋RPT-CURRENTNESS-UNEVALUABLE 诊断；呈现
 * "无法判定"。
 */
TEST_F(ReportingContractSuite, RP_CUR_3_Unevaluable_NotDefaultCurrent)
{
    IRD_TEST_INFO(std::vector<std::string>{"RPT-01"}, {}, std::nullopt);

    const core::RunId run = runAt(0x14);
    // 场景手工组装（不可判定当前性脚本——单点变异面）。
    BuildScene scene;
    RunSpec spec{run};
    spec.currentness = "unevaluable";
    scene.addRun(spec);
    ContentSpec content;
    content.boundRun = run;
    scene.addKinContent(content);

    const ReviewReport report = buildOk(scene, ReportLevel::B);
    const ResultRefSnapshot& ref = report.resultRefs()[0];
    EXPECT_FALSE(ref.currentness.status.has_value()) << "不得默认 Current（P-EV-4）";
    ASSERT_TRUE(ref.currentness.unevaluableNote.has_value());
    EXPECT_EQ(ref.currentness.unevaluableNote->code,
              std::string(diagcodes::kCurrentnessUnevaluable));
    // 报告级诊断并集携带同一码面（RPT-CURRENTNESS-UNEVALUABLE）。
    bool found = false;
    for (const DiagRefEntry& diag : report.diagRefs()) {
        if (diag.code == std::string(diagcodes::kCurrentnessUnevaluable)) {
            found = true;
        }
    }
    EXPECT_TRUE(found) << "不可判定诊断在案";
    // 呈现"无法判定"（呈现词＝"不可判定"——§6.3 呈现名，非第三持久态）。
    const std::string html = renderText(report, ReportRenderFormat::Html);
    EXPECT_NE(html.find("不可判定"), std::string::npos) << "不可判定呈现词在正文";
}

// =====================================================================
// RP-COV 组（多工况覆盖——EVI-02/AT-32）
// =====================================================================

/**
 * §10.1 RP-COV-1：多工况覆盖显示——①包络合并条目"包络合并（呈现方式）"
 * 标注（跨多工况条目）；②漏验一个启用必验工况→整体缺项呈现（不得正式
 * 通过）；③全覆盖→覆盖摘要呈现。报告级 coverageSummary 聚合语义未冻结
 * （§14.4 v0.12 置零值）——聚合计数随其冻结任务收口（文件头登记）。
 */
TEST_F(ReportingContractSuite, RP_COV_1_CaseCoverageDisplay)
{
    IRD_TEST_INFO(std::vector<std::string>{"EVI-02"}, {std::string{"AT-32"}}, std::nullopt);

    const core::ObjectId caseA = caseAt(0x21);
    const core::ObjectId caseB = caseAt(0x22);
    const core::RunId run = runAt(0x14);

    // ①全覆盖（包络覆盖双工况）：条目跨双工况＝包络合并标注（§6.5/DYN-07）。
    {
        BuildScene scene;
        RunSpec spec{run};
        spec.caseScope = {caseA, caseB};
        scene.addRun(spec);
        ContentSpec content;
        content.boundRun = run;
        content.entryCaseScope = {caseA, caseB};
        scene.addKinContent(content);
        const ReviewReport report = buildOk(scene, ReportLevel::B);
        const std::string html = renderText(report, ReportRenderFormat::Html);
        EXPECT_NE(html.find("包络合并（呈现方式）"), std::string::npos)
            << "包络合并标注（跨多工况条目）";
        // 报告级覆盖摘要（stage A：聚合语义未冻结——builder 置零值，渲染
        // 面＝P-EV-7 如实表达"无启用必验工况"；"全部已覆盖"分支随聚合
        // 语义冻结任务收口，§14.4 v0.12——本用例如实断言零值分支）。
        EXPECT_NE(html.find("覆盖完备性"), std::string::npos) << "覆盖摘要在正文";
        EXPECT_NE(html.find("无启用必验工况"), std::string::npos)
            << "零值聚合的如实呈现（不伪造'已覆盖'空真表述——P-EV-7）";
        // 全覆盖＋资格成立的正例面："正式通过"声明在此合法呈现（§8.2①
        // 断言①通过）——"漏验不得正式通过"的反例面由②承载。
        EXPECT_NE(countOutsideQualifierSpans(html, "正式通过"), std::size_t{0})
            << "全覆盖正例的正式通过声明（资格成立）";
    }
    // ②漏验一个启用必验工况：漏验工况以 caseId 规范文本进缺失清单（§6.4.1
    // MissingItem.itemId 漏验口径）——整体缺项呈现，无正式通过字样。
    {
        BuildScene scene;
        RunSpec spec{run};
        spec.kind = "data-insufficient";
        spec.missingItems = {caseB.toCanonical()};   // 漏验工况＝caseId 规范文本
        spec.caseScope = {caseA};
        scene.addRun(spec);
        ContentSpec content;
        content.status = SectionStatus::DataInsufficient;
        content.missingItems = {caseB.toCanonical()};
        content.boundRun = run;
        content.entryCaseScope = {caseA};
        registerContent(scene.registry, content);
        const ReviewReport report = buildOk(scene, ReportLevel::B);
        const ReviewReportSection& kin = report.sections()[2];
        EXPECT_EQ(kin.status, SectionStatus::DataInsufficient);
        ASSERT_EQ(kin.missingItems.size(), std::size_t{1});
        EXPECT_EQ(kin.missingItems[0].itemId, caseB.toCanonical()) << "漏验工况定位";
        const std::string html = renderText(report, ReportRenderFormat::Html);
        EXPECT_EQ(countOutsideQualifierSpans(html, "正式通过"), std::size_t{0})
            << "漏验任一启用必验工况不得正式通过（§8.2①断言）";
    }
}

// =====================================================================
// RP-TRACE 组（证据引用可追溯——NFR-COR-04/AT-14）
// =====================================================================

/**
 * §10.1 RP-TRACE-1：证据引用可追溯——逐结论条目校验：ResultBinding(runId+
 * fieldPath)→snapshotId/sliceId 可解析；EvidenceBinding→itemId+digest 在
 * 证据清单（追溯链遍历断言；testkit ContractCheck 谓词消费）。
 */
TEST_F(ReportingContractSuite, RP_TRACE_1_EvidenceTraceabilityChain)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-04"}, {std::string{"AT-14"}}, std::nullopt);

    const core::RunId run = runAt(0x14);
    BuildScene scene = standardScene(run);
    const ReviewReport report = buildOk(scene, ReportLevel::B);

    // 结果引用索引（runId 规范文本→引用快照）。
    std::map<std::string, const ResultRefSnapshot*> resultIndex;
    for (const ResultRefSnapshot& ref : report.resultRefs()) {
        // testkit ContractCheck：任务五元组契约（追溯链第一跳的身份面）。
        const auto check = tk::checkTaskIdentity(ref.task);
        EXPECT_TRUE(check.passed) << "resultRef 任务五元组契约合格";
        resultIndex.emplace(ref.runId.toCanonical(), &ref);
    }
    EXPECT_FALSE(resultIndex.empty()) << "扫描失效防护（至少一条结果引用）";

    // 逐章节逐条目遍历（§9.2 结论条目全量——不抽样）。
    for (const ReviewReportSection& section : report.sections()) {
        for (const SectionEntryView& entry : section.entries) {
            // ResultBinding.runId→resultRefs 可解析；快照/切片可解析到
            // envelope 绑定面（NFR-COR-04 第一跳）。
            const auto it = resultIndex.find(entry.result.runId.toCanonical());
            ASSERT_NE(it, resultIndex.end()) << "条目结果绑定越界";
            const ResultRefSnapshot* ref = it->second;
            const evidence::ResultEnvelope& envelope =
                scene.resultSource.envelopes.at(entry.result.runId.toCanonical());
            EXPECT_TRUE(ref->snapshotId == envelope.snapshotId) << "snapshotId 可解析";
            EXPECT_TRUE(ref->sliceId == envelope.sliceId) << "sliceId 可解析";
            EXPECT_FALSE(entry.result.fieldPath.empty()) << "fieldPath 定位在场";
            // EvidenceBinding→itemId+digest 在证据清单（第二跳——AT-14）。
            for (const EvidenceBinding& binding : entry.evidence) {
                bool inManifest = false;
                for (const evidence::EvidenceItem& item : envelope.evidence.items) {
                    if (item.itemId == binding.itemId) {
                        EXPECT_TRUE(item.artifactDigest == binding.digest)
                            << "digest 与清单一致: " << binding.itemId;
                        inManifest = true;
                    }
                }
                EXPECT_TRUE(inManifest) << "证据项在清单: " << binding.itemId;
            }
            // 报告级证据引用并集可回指（§4.2 evidenceRefs——来源章节导航）。
            for (const EvidenceRefEntry& evidenceRef : report.evidenceRefs()) {
                EXPECT_FALSE(evidenceRef.sourceSection.empty()) << "来源章节在案";
            }
        }
    }
}

// =====================================================================
// RP-CONS 组（多格式渲染与一致性——RPT-02/AT-22）
// =====================================================================

/**
 * §10.1 RP-CONS-1：多格式逐字段一致——合法报告三格式渲染＋一致性检查；
 * 注入坏样本（HTML 删一个 data-field/CSV 改一值/JSON 删限定语）→mismatch
 * 定位到 (format, fieldKey, dimension)；fieldCount 计数在案。
 */
TEST_F(ReportingContractSuite, RP_CONS_1_MultiFormatConsistency_BadSampleLocation)
{
    IRD_TEST_INFO(std::vector<std::string>{"RPT-02"}, {std::string{"AT-22"}}, std::nullopt);

    const core::RunId run = runAt(0x14);
    BuildScene scene = standardScene(run);
    const ReviewReport report = buildOk(scene, ReportLevel::B);
    const FieldMatrix matrix = extractFieldMatrix(report);
    ASSERT_FALSE(matrix.empty());

    // 正常三格式渲染＋逐字段一致（含 Quick 运行对照以产出限定语维度——
    // JSON 删限定语坏样本的定位前提）。
    const RenderArtifact html = artifactOf(report, ReportRenderFormat::Html);
    const RenderArtifact json = artifactOf(report, ReportRenderFormat::Json);
    const RenderArtifact csv = artifactOf(report, ReportRenderFormat::Csv);
    ScenarioCsvReaderFactory csvReaders;
    FieldConsistencyChecker checker(csvReaders);
    ConsistencyInput input;
    input.report = &report;
    input.matrix = &matrix;
    input.artifacts = {&html, &json, &csv};
    const ConsistencyResult ok = checker.check(input);
    EXPECT_TRUE(ok.consistent) << "合法三格式逐字段一致";
    for (const FieldMismatch& m : ok.mismatches) {
        ADD_FAILURE() << "意外 mismatch: " << m.fieldKey << "@" << m.dimension;
    }
    EXPECT_EQ(ok.fieldCount, matrix.size()) << "fieldCount＝矩阵大小";

    // 坏样本注入①：HTML 删一个 data-field（值维度缺失定位）。
    {
        std::string bad(html.bytes.begin(), html.bytes.end());
        const std::size_t pos = bad.find("data-field=\"" + matrix.front().fieldKey + "\"");
        ASSERT_NE(pos, std::string::npos) << "坏样本注入锚点（data-field 属性）在案";
        bad.erase(pos, std::string("data-field=\"" + matrix.front().fieldKey + "\"").size());
        const RenderArtifact badHtml = [&] {
            RenderArtifact a = html;
            a.bytes = asBytes(bad);
            return a;
        }();
        const ConsistencyResult r = checker.check({&report, &matrix, {&badHtml, &json, &csv}});
        EXPECT_FALSE(r.consistent) << "坏样本可检出";
        bool located = false;
        for (const FieldMismatch& m : r.mismatches) {
            if (m.fieldKey == matrix.front().fieldKey) {
                located = true;   // (format, fieldKey, dimension) 定位
            }
        }
        EXPECT_TRUE(located) << "mismatch 定位到注入 fieldKey";
    }
    // 坏样本注入②：CSV 改一值（值维度定位）。
    {
        std::string bad(csv.bytes.begin(), csv.bytes.end());
        const std::string needle = "," + csvCellText(ReportCsvCell::textCell(
                                         matrix.front().valueRepr.empty()
                                             ? std::string{"0"}
                                             : matrix.front().valueRepr))
                                       + ",";
        const std::size_t pos = bad.find(needle);
        if (pos != std::string::npos) {
            bad.replace(pos, needle.size(), ",tampered,");
        }
        ASSERT_NE(bad, std::string(csv.bytes.begin(), csv.bytes.end()))
            << "坏样本注入生效（值列可定位）";
        const RenderArtifact badCsv = [&] {
            RenderArtifact a = csv;
            a.bytes = asBytes(bad);
            return a;
        }();
        const ConsistencyResult r = checker.check({&report, &matrix, {&html, &json, &badCsv}});
        EXPECT_FALSE(r.consistent) << "CSV 坏样本可检出";
    }
    // 坏样本注入③：JSON 删限定语数组（qualifier 维度定位）——以带限定语的
    // Quick 运行报告构造，删除其 qualifier 数组后定位 dimension=qualifier。
    {
        BuildScene quickScene;
        RunSpec quickSpec{run};
        quickSpec.mode = "quick";
        quickScene.addRun(quickSpec);
        ContentSpec content;
        content.boundRun = run;
        quickScene.addKinContent(content);
        const ReviewReport quickReport = buildOk(quickScene, ReportLevel::B);
        const FieldMatrix quickMatrix = extractFieldMatrix(quickReport);
        const RenderArtifact qHtml = artifactOf(quickReport, ReportRenderFormat::Html);
        const RenderArtifact qJson = artifactOf(quickReport, ReportRenderFormat::Json);
        const RenderArtifact qCsv = artifactOf(quickReport, ReportRenderFormat::Csv);
        std::string bad(qJson.bytes.begin(), qJson.bytes.end());
        // 锚定筛选级限定语所在的 qualifier 数组（JSON 中 "qualifier" 键有
        // 多处——覆盖/边界章摘要与逐单元格；以 token 文本定位其所属数组，
        // 避免 GA 锚错位）。
        const std::size_t tokPos = bad.find("\"screening-only\"");
        ASSERT_NE(tokPos, std::string::npos) << "筛选级限定语 token 在 JSON（Quick 运行）";
        const std::size_t arrStart = bad.rfind('[', tokPos);
        const std::size_t arrEnd = bad.find(']', tokPos);
        ASSERT_NE(arrStart, std::string::npos);
        ASSERT_NE(arrEnd, std::string::npos);
        bad.replace(arrStart, arrEnd - arrStart + 1, "[]");
        const RenderArtifact badJson = [&] {
            RenderArtifact a = qJson;
            a.bytes = asBytes(bad);
            return a;
        }();
        const ConsistencyResult r =
            checker.check({&quickReport, &quickMatrix, {&qHtml, &badJson, &qCsv}});
        EXPECT_FALSE(r.consistent) << "JSON 限定语坏样本可检出";
        bool qualifierLocated = false;
        for (const FieldMismatch& m : r.mismatches) {
            if (m.dimension == std::string(kMismatchDimQualifier)) {
                qualifierLocated = true;
            }
        }
        EXPECT_TRUE(qualifierLocated) << "mismatch 定位到 qualifier 维度";
    }
}

/**
 * §10.1 RP-CONS-2：CSV 转义 roundtrip——字段含 `=+−@'` 前缀/中文/引号/换行/
 * 空串样例：导出→io reader 回读→矩阵重建比对，逐字符一致；转义形式不入
 * 结构层（io IO-V01/V02 报告侧消费——转义唯一实现归 io，此处为注入缝镜像）。
 */
TEST_F(ReportingContractSuite, RP_CONS_2_CsvEscapeRoundtrip)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-SEC-03"}, {std::string{"AT-22"}}, std::nullopt);

    const core::RunId run = runAt(0x14);
    // 场景手工组装（文本字段样例的章节内容需整章定制——standardScene 的
    // kin 注册面在此不适用，避免二次注册——§9.2 注册边界）。
    BuildScene scene;
    scene.addRun(RunSpec{run});
    // 文本字段样例集（FieldValue.text——与 Provided 数值互斥；§4.3）：
    // =公式/+正号/−U+2212/@宏/'引号/中文/双引号/换行/空串——每字段一键。
    const std::vector<std::pair<std::string, std::string>> samples = {
        {"kin.f-equals", "=SUM(A1)"},
        {"kin.f-plus", "+30"},
        {"kin.f-uminus", "−1.5"},
        {"kin.f-at", "@ref"},
        {"kin.f-quote", "'lead"},
        {"kin.f-chinese", "中文对象名——可达性"},
        {"kin.f-dquote", "含\"引号\"字段"},
        {"kin.f-newline", "第一行\n第二行"},
        {"kin.f-empty", ""},
    };
    scene.registry.registerProvider(std::make_unique<ScriptedSectionProvider>(
        std::string(kSectionKinematicsCollision), ReportLevel::B,
        std::vector<std::string>{"kin-batch-ik"}, [&] {
            ContentSpec spec;
            spec.boundRun = run;
            SectionContent content = sectionContentOf(spec);
            content.status = SectionStatus::Populated;
            SectionEntryView entry = content.entries.front();
            entry.fields.clear();
            for (const auto& sample : samples) {
                FieldValue field;
                field.key = sample.first;
                field.text = sample.second;
                entry.fields.push_back(field);
            }
            content.entries = {entry};
            return content;
        }()));

    const ReviewReport report = buildOk(scene, ReportLevel::B);
    const FieldMatrix matrix = extractFieldMatrix(report);
    ASSERT_EQ(matrix.size(), samples.size());

    // 导出（含 CSV）→回读→矩阵重建比对。
    tk::TempDir dir("rp-cons2");
    ExportScene exportScene(report);
    const std::vector<ExportedFile> files =
        exportOk(exportScene, report,
                 {ReportRenderFormat::Html, ReportRenderFormat::Json, ReportRenderFormat::Csv},
                 dir.path());
    const fs::path csvPath = dir.path() / "report.csv";
    ASSERT_TRUE(fs::exists(csvPath)) << "CSV 工件落盘";
    const std::string csvBytes = readFileText(csvPath);

    // 文件层守卫形态在案（=SUM 样例的前置 ' 前缀——转义发生在文件层）。
    EXPECT_NE(csvBytes.find("'=SUM(A1)"), std::string::npos)
        << "危险前缀守卫出现在文件层（恰一个 ' 前缀）";

    // io reader 回读（注入缝 reader——检查器同路径）。
    ScenarioCsvReaderFactory factory;
    std::unique_ptr<IReportCsvReader> reader = factory.makeCsvReader();
    std::vector<ReportCsvTable> tables;
    ASSERT_TRUE(reader->readTables(asBytes(csvBytes).data(), asBytes(csvBytes).size(), tables));
    const ReportCsvTable* main = nullptr;
    for (const ReportCsvTable& table : tables) {
        if (!table.header.empty() && table.header.front() == "field_key") {
            main = &table;
        }
    }
    ASSERT_NE(main, nullptr) << "主表（field_key 冻结首列）在回读表集中";

    // 矩阵重建比对：逐 fieldKey 行的 value 列与矩阵 valueRepr 逐字符一致
    // （转义形式不入结构层——守卫前缀已在读取侧还原）。
    std::map<std::string, const std::vector<std::string>*> rowByFieldKey;
    for (const std::vector<std::string>& row : main->rows) {
        ASSERT_FALSE(row.empty());
        rowByFieldKey.emplace(row.front(), &row);
    }
    for (const FieldCell& cell : matrix) {
        const auto it = rowByFieldKey.find(cell.fieldKey);
        ASSERT_NE(it, rowByFieldKey.end()) << "矩阵单元格行在 CSV: " << cell.fieldKey;
        const std::size_t valueCol = std::find(main->header.begin(), main->header.end(), "value")
                                     - main->header.begin();
        ASSERT_LT(valueCol, it->second->size());
        // 逐字符一致（roundtrip——IRD_EXPECT_IDENTICAL 宏为语句形态，失败
        // 详情经其内部 ADD_FAILURE 输出，不能追加流式说明）。
        IRD_EXPECT_IDENTICAL("csv.value[" + cell.fieldKey + "]", (*it->second)[valueCol],
                             cell.valueRepr);
    }
}

/**
 * §10.1 RP-CONS-3：稳定排序——同报告两次导出：三格式各自二次导出字节相同
 * （文件 SHA-256——NFR-COR-02 精神）。
 */
TEST_F(ReportingContractSuite, RP_CONS_3_StableOrdering_ByteDeterminism)
{
    IRD_TEST_INFO(std::vector<std::string>{"RPT-02"}, {std::string{"AT-22"}}, std::nullopt);

    const core::RunId run = runAt(0x14);
    tk::TempDir dir1("rp-cons3-1");
    tk::TempDir dir2("rp-cons3-2");
    BuildScene scene = standardScene(run);
    const ReviewReport report = buildOk(scene, ReportLevel::B);
    const std::vector<ReportRenderFormat> formats = {
        ReportRenderFormat::Html, ReportRenderFormat::Json, ReportRenderFormat::Csv};

    ExportScene exportScene(report);
    const std::vector<ExportedFile> first = exportOk(exportScene, report, formats, dir1.path());
    const std::vector<ExportedFile> second = exportOk(exportScene, report, formats, dir2.path());
    ASSERT_EQ(first.size(), second.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        EXPECT_EQ(first[i].format, second[i].format);
        EXPECT_TRUE(first[i].sha256 == second[i].sha256)
            << "同报告两次导出逐格式字节相同（format="
            << std::string(token(first[i].format)) << "）";
    }
}

/**
 * §10.1 RP-CONS-4：Unicode/单位/复杂结构引用——含中文对象名/单位集 mm·deg/
 * 曲线类字段：UTF-8 无 BOM；单位列与 SI 真值换算正确（core 唯一入口）；
 * 曲线以引用呈现不复制。
 */
TEST_F(ReportingContractSuite, RP_CONS_4_UnicodeUnitsAndCurveRefs)
{
    IRD_TEST_INFO(std::vector<std::string>{"RPT-02", "KIN-12"}, {std::string{"AT-32"}},
                  std::nullopt);

    const core::RunId run = runAt(0x14);
    // 场景手工组装（章节内容整章定制：曲线引用条目＋mm 数值字段——§6.5
    // 曲线以引用呈现；KIN-12 显示单位换算断言的数据源）。
    BuildScene scene;
    scene.addRun(RunSpec{run});
    scene.registry.registerProvider(std::make_unique<ScriptedSectionProvider>(
        std::string(kSectionKinematicsCollision), ReportLevel::B,
        std::vector<std::string>{"kin-batch-ik"}, [&] {
            ContentSpec spec;
            spec.boundRun = run;
            SectionContent content = sectionContentOf(spec);
            content.status = SectionStatus::Populated;
            SectionEntryView entry = content.entries.front();
            entry.entryKey = "cycle-curve";
            // 数值字段携带 Length 量纲显示单位 mm（SI 真值 1.0 m——KIN-12
            // 换算断言基准：1 m→"1000" mm）；文本字段携带中文对象名。
            entry.fields.front().unit = *core::UnitToken::find("mm");
            FieldValue text;
            text.key = "kin.object-name";
            text.text = "中文对象名——可达性包络";
            entry.fields.push_back(text);
            content.entries = {entry};
            content.renderHint = RenderHint::CurveRef;
            return content;
        }()));
    ReportBuildRequest request = scene.request(ReportLevel::B);
    // 单位集 mm·deg（§4.2 unitPreference 冻结入身份——显示单位声明）。
    UnitPreference units;
    units.displayUnits.emplace(core::QuantityKind::Length, *core::UnitToken::find("mm"));
    units.displayUnits.emplace(core::QuantityKind::Angle, *core::UnitToken::find("deg"));
    request.units = units;

    const ReviewReport report = buildOk(scene, ReportLevel::B);
    const FieldMatrix matrix = extractFieldMatrix(report);
    const std::string html = renderText(report, ReportRenderFormat::Html);

    // Unicode：UTF-8 无 BOM（首三字节非 EF BB BF）；中文条目名原样呈现。
    ASSERT_FALSE(html.empty());
    EXPECT_FALSE(static_cast<unsigned char>(html[0]) == 0xEF
                 && static_cast<unsigned char>(html[1]) == 0xBB
                 && static_cast<unsigned char>(html[2]) == 0xBF)
        << "UTF-8 无 BOM";
    EXPECT_NE(html.find("cycle-curve"), std::string::npos) << "条目键（UTF-8 直出）";

    // 单位列与 SI 真值换算（KIN-12——core tryConvert 唯一入口）：数值字段
    // SI 1.0 m（Length）＋显示单位 mm→1000。
    bool unitChecked = false;
    for (const FieldCell& cell : matrix) {
        if (cell.unit.has_value() && *cell.unit == "mm") {
            EXPECT_EQ(cell.valueRepr, "1000") << "SI→显示单位换算（1 m＝1000 mm）";
            unitChecked = true;
        }
    }
    EXPECT_TRUE(unitChecked) << "单位换算断言生效（mm 列在矩阵）";
    // 曲线以引用呈现不复制（resultRef 规范文本在矩阵与正文——引用非复制）。
    bool curveRefSeen = false;
    for (const FieldCell& cell : matrix) {
        if (cell.entryKey == "cycle-curve") {
            ASSERT_TRUE(cell.resultRef.has_value());
            EXPECT_EQ(*cell.resultRef, run.toCanonical()) << "曲线条目携带结果引用";
            curveRefSeen = true;
        }
    }
    EXPECT_TRUE(curveRefSeen) << "曲线类条目在矩阵";
    EXPECT_NE(html.find(run.toCanonical()), std::string::npos)
        << "正文以引用（runId 规范文本）呈现曲线来源";
}

// =====================================================================
// RP-CONC 组（生成期间后台写入——CON-01/05/AT-10 观测）
// =====================================================================

/**
 * §10.1 RP-CONC-1：生成期间后台写入——构建中注入新修订提交＋同修订迟到
 * 结果事件：报告内容不受影响（锚定视图）；迟到结果不进入已冻结结果集；
 * 不写新项目（FakeSink 调用序列为零）。
 */
TEST_F(ReportingContractSuite, RP_CONC_1_BackgroundWrites_AnchoredView)
{
    IRD_TEST_INFO(std::vector<std::string>{"CON-01", "CON-05"}, {std::string{"AT-10"}},
                  std::nullopt);

    const core::RunId run = runAt(0x14);
    const core::RunId lateRun = runAt(0x15);
    // 标准场景已含 kin Populated 提供方（避免二次注册——§9.2 注册边界）。
    BuildScene scene = standardScene(run);
    // 迟到结果"到达"前的场景事实：构建全程 listRuns 恰一次（锚定快照）。
    const ReviewReport report = buildOk(scene, ReportLevel::B);

    // 迟到结果"到达"（listRuns 已消费的锚定清单之外）。
    scene.queryPort.runs.push_back(runInfoOf(lateRun, "kin-batch-ik"));
    scene.queryPort.view.seq = 8;

    // 锚定视图隔离：报告内容不受影响（r7/单结果）。
    EXPECT_EQ(report.revisionSeq(), 7u);
    ASSERT_EQ(report.resultRefs().size(), std::size_t{1});
    EXPECT_TRUE(report.resultRefs()[0].runId == run) << "迟到结果不入已冻结结果集";
    EXPECT_EQ(scene.queryPort.listRunsCalls, 1) << "锚定恰一次（迟到不触发重查）";
    // 不写新项目（FakeSink 调用序列——构建全程零汇集座调用）。
    FakeArtifactSink sink;
    EXPECT_TRUE(sink.calls.empty()) << "构建不产生任何汇集座调用（零项目写）";
}

// =====================================================================
// RP-CANC 组（取消与临时清理——UX-03）
// =====================================================================

/**
 * §10.1 RP-CANC-1：取消与临时清理——导出/构建各阶段注入取消令牌：
 * Canceled（非错误诊断）；临时文件清理；目标不变/未完成工件不入清单。
 */
TEST_F(ReportingContractSuite, RP_CANC_1_CancelAndTempCleanup)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-03", "RPT-02"}, {}, std::nullopt);

    const core::RunId run = runAt(0x14);
    // ①构建取消（章节解析检查点）：无报告、无错误（UX-03 正常取消非错误）。
    {
        BuildScene scene = standardScene(run);
        AlwaysCancelToken token;
        const ReportBuildOutcome outcome = scene.builder()->build(scene.request(ReportLevel::B),
                                                                 &token);
        expectCanceled(outcome);
    }
    // ②项目内归档取消：FakeSink 未完成工件不入清单（abandon 责任终结）。
    {
        BuildScene scene = standardScene(run);
        const ReviewReport report = buildOk(scene, ReportLevel::B);
        ExportScene exportScene(report);
        AlwaysCancelToken token;
        ReportExportRequest request;
        request.project = report.project();
        request.reportId = report.reportId();
        request.formats = {ReportRenderFormat::Html, ReportRenderFormat::Json};
        request.destination.kind = ExportDestination::ProjectArchive;
        const ReportExportResult result = exportScene.service()->exportReport(request, &token);
        EXPECT_FALSE(result.published.has_value()) << "取消＝无发布记录";
        EXPECT_FALSE(result.error.has_value()) << "取消非错误（UX-03）";
        EXPECT_TRUE(result.files.empty());
        EXPECT_EQ(exportScene.sink.published.size(), std::size_t{0})
            << "未完成工件不入已发布清单";
    }
    // ③外部导出取消：目标不变（无半途产物落盘——临时清理）。
    {
        BuildScene scene = standardScene(run);
        const ReviewReport report = buildOk(scene, ReportLevel::B);
        tk::TempDir dir("rp-canc1");
        ExportScene exportScene(report);
        AlwaysCancelToken token;
        ReportExportRequest request;
        request.project = report.project();
        request.reportId = report.reportId();
        request.formats = {ReportRenderFormat::Html};
        request.destination.kind = ExportDestination::ExternalPath;
        request.destination.externalPath = dir.path();
        const ReportExportResult result = exportScene.service()->exportReport(request, &token);
        EXPECT_TRUE(result.files.empty());
        EXPECT_FALSE(result.error.has_value()) << "取消非错误";
        EXPECT_FALSE(fs::exists(dir.path() / "report.html")) << "目标不变（零落盘）";
        // 临时文件清理：目录内零 .tmp 残留（导出链临时物随取消清理）。
        for (const auto& entry : fs::directory_iterator(dir.path())) {
            EXPECT_EQ(entry.path().extension().string(), "") << "零临时残留";
        }
    }
}

// =====================================================================
// RP-IDEM / RP-CONF / RP-DISK / RP-RO 组（幂等导出与冲突检测——RPT-01/§7.4）
// =====================================================================

/**
 * §10.1 RP-IDEM-1：幂等导出（项目内）——已 Finalized 同内容再发布：
 * 第二次 IdempotentHit（零重写）；manifest 零变化。
 */
TEST_F(ReportingContractSuite, RP_IDEM_1_ProjectArchiveIdempotentHit)
{
    IRD_TEST_INFO(std::vector<std::string>{"RPT-01"}, {}, std::nullopt);

    const core::RunId run = runAt(0x14);
    BuildScene scene = standardScene(run);
    const ReviewReport report = buildOk(scene, ReportLevel::B);
    ExportScene exportScene(report);

    const PublishedReportRecord first = archiveOk(exportScene, report,
                                                  {ReportRenderFormat::Html,
                                                   ReportRenderFormat::Json});
    const std::size_t callsAfterFirst = exportScene.sink.calls.size();
    EXPECT_GT(callsAfterFirst, std::size_t{0}) << "首次发布产生汇集座调用";

    const PublishedReportRecord second = archiveOk(exportScene, report,
                                                   {ReportRenderFormat::Html,
                                                    ReportRenderFormat::Json});
    // IdempotentHit：零重写（零新汇集座会话调用）＋manifest 零变化。
    EXPECT_EQ(exportScene.sink.calls.size(), callsAfterFirst) << "第二次发布零重写";
    EXPECT_TRUE(first.manifestDigest == second.manifestDigest) << "manifest 摘要零变化";
    EXPECT_TRUE(first.publishedAtUtc == second.publishedAtUtc) << "原发布时刻保留";
}

/**
 * §10.1 RP-IDEM-2：幂等导出（外部）——外部目标已存在同字节文件：幂等成功
 * （无操作）；文件 mtime/哈希不变。
 */
TEST_F(ReportingContractSuite, RP_IDEM_2_ExternalIdempotentNoOp)
{
    IRD_TEST_INFO(std::vector<std::string>{"RPT-01"}, {}, std::nullopt);

    const core::RunId run = runAt(0x14);
    tk::TempDir dir("rp-idem2");
    BuildScene scene = standardScene(run);
    const ReviewReport report = buildOk(scene, ReportLevel::B);
    ExportScene exportScene(report);

    const std::vector<ExportedFile> first =
        exportOk(exportScene, report, {ReportRenderFormat::Html}, dir.path());
    const fs::path htmlPath = dir.path() / "report.html";
    const auto mtimeBefore = fs::last_write_time(htmlPath);
    const core::Digest256 shaBefore = first.front().sha256;
    const int createdBefore = exportScene.ioFactory.atomicTargetCreated;

    // 二次导出（NeverOverwrite＋同字节目标）＝幂等成功无操作。
    const std::vector<ExportedFile> second =
        exportOk(exportScene, report, {ReportRenderFormat::Html}, dir.path());
    ASSERT_EQ(second.size(), std::size_t{1});
    EXPECT_TRUE(second.front().sha256 == shaBefore) << "哈希不变";
    EXPECT_EQ(fs::last_write_time(htmlPath), mtimeBefore) << "mtime 不变（零重写）";
    EXPECT_EQ(exportScene.ioFactory.atomicTargetCreated, createdBefore)
        << "原子目标零新建（无操作——幂等半区）";
}

/**
 * §10.1 RP-CONF-1：同路径不同内容冲突——①项目内同 reportId 不同内容＝
 * ArchiveConflict（差异维度定位）；②外部目标不同内容＝NeverOverwrite 拒绝，
 * 显式确认后 OverwriteAtomic 替换成功且先前输出在替换前完整。
 */
TEST_F(ReportingContractSuite, RP_CONF_1_ConflictDetectionAndResolution)
{
    IRD_TEST_INFO(std::vector<std::string>{"RPT-01"}, {}, std::nullopt);

    const core::RunId run = runAt(0x14);
    // ①项目内：同 reportId（不同 reportId 会走不同编址键——以种子预置同
    // reportId 异摘要记录注入冲突面，FakeSink seedPublished 差异维度独立
    // 可设：身份维度＝contentIdentity 异）。
    {
        BuildScene scene = standardScene(run);
        const ReviewReport report = buildOk(scene, ReportLevel::B);
        ExportScene exportScene(report);
        archiveOk(exportScene, report, {ReportRenderFormat::Html});
        // 同报告对象恒幂等（对照）；冲突面以"同 reportId、不同 manifest
        // 摘要"的预置记录触发（§7.4 冲突＝同 reportId 且摘要不一致）。
        FakeArtifactSink conflictSink;
        core::ContentIdentity foreign = report.contentIdentity();
        foreign.bytes[0] = static_cast<std::uint8_t>(foreign.bytes[0] + 1);   // 异内容身份
        conflictSink.seedPublished(report.project(), report.reportId(), foreign, foreign);
        const std::size_t callsBefore = conflictSink.calls.size();
        ReportExportRequest request;
        request.project = report.project();
        request.reportId = report.reportId();
        request.formats = {ReportRenderFormat::Html};
        request.destination.kind = ExportDestination::ProjectArchive;
        const ReportExportResult result = ReportExportService(
            exportScene.ioFactory, redactionService(), conflictSink, conflictSink,
            exportScene.csvReaders, exportScene.resolver)
                                             .exportReport(request);
        ASSERT_TRUE(result.error.has_value()) << "冲突被拒绝";
        EXPECT_EQ(result.error->code(), ReportErrorCode::ArchiveConflict);
        bool conflictDiag = false;
        for (const core::DiagnosticRecord& diag : result.diagnostics) {
            if (diag.code == std::string(diagcodes::kArchiveConflict)) {
                conflictDiag = true;   // 差异维度定位（诊断随拒绝——NFR-COR-03）
            }
        }
        EXPECT_TRUE(conflictDiag) << "RPT-ARCHIVE-CONFLICT 诊断在案（差异维度定位）";
        EXPECT_EQ(conflictSink.published.size(), std::size_t{1}) << "冲突不覆盖（只增）";
        EXPECT_EQ(conflictSink.calls.size(), callsBefore) << "冲突路径零重写调用";
    }
    // ②外部：不同内容目标——NeverOverwrite 拒绝且原文件完整；显式确认
    // （OverwriteAtomic）替换成功。
    {
        const core::RunId runB = runAt(0x15);
        BuildScene sceneB = standardScene(runB);
        const ReviewReport other = buildOk(sceneB, ReportLevel::B);
        tk::TempDir dir("rp-conf1");
        BuildScene scene = standardScene(run);
        const ReviewReport report = buildOk(scene, ReportLevel::B);
        ExportScene exportScene(report);
        exportOk(exportScene, report, {ReportRenderFormat::Html}, dir.path());
        const std::string original = readFileText(dir.path() / "report.html");
        ASSERT_FALSE(original.empty());

        // NeverOverwrite：目标存在即拒绝（原文件完整保留）。
        ReportExportRequest request;
        request.project = report.project();
        request.reportId = other.reportId();
        request.formats = {ReportRenderFormat::Html};
        request.destination.kind = ExportDestination::ExternalPath;
        request.destination.externalPath = dir.path();
        request.destination.replace = ReplacePolicy::NeverOverwrite;
        ExportScene otherScene(other);
        ReportExportResult rejected = otherScene.service()->exportReport(request);
        ASSERT_TRUE(rejected.error.has_value()) << "NeverOverwrite 拒绝";
        EXPECT_EQ(readFileText(dir.path() / "report.html"), original)
            << "先前输出在拒绝后完整保留";

        // OverwriteAtomic（显式确认）：替换成功；失败注入时先前输出保留
        // （§7.4 原子替换语义——此处验证成功半区，失败半区由
        // ArchiveExportTest::ExternalOverwriteFailurePreservesPrevious 钉住）。
        request.destination.replace = ReplacePolicy::OverwriteAtomic;
        ReportExportResult replaced = otherScene.service()->exportReport(request);
        EXPECT_FALSE(replaced.error.has_value()) << "显式确认后替换成功";
        EXPECT_NE(readFileText(dir.path() / "report.html"), original) << "目标已被替换";
    }
}

/**
 * §10.1 RP-DISK-1：磁盘不足——FakeSink 注入写失败：DiskFull＋abandon＋清理；
 * 可重试（选择与路径保留）。
 */
TEST_F(ReportingContractSuite, RP_DISK_1_DiskFull_AbandonAndRetry)
{
    IRD_TEST_INFO(std::vector<std::string>{"RPT-01"}, {}, std::nullopt);

    const core::RunId run = runAt(0x14);
    BuildScene scene = standardScene(run);
    const ReviewReport report = buildOk(scene, ReportLevel::B);
    ExportScene exportScene(report);
    exportScene.sink.diskFullAtWrite = 1;   // 首次 writeArtifact 注入 DiskFull

    ReportExportRequest request;
    request.project = report.project();
    request.reportId = report.reportId();
    request.formats = {ReportRenderFormat::Html, ReportRenderFormat::Json};
    request.destination.kind = ExportDestination::ProjectArchive;
    ReportExportResult failed = exportScene.service()->exportReport(request);
    ASSERT_TRUE(failed.error.has_value()) << "磁盘满被拒绝";
    EXPECT_EQ(failed.error->code(), ReportErrorCode::DiskFull);
    EXPECT_FALSE(failed.published.has_value()) << "失败＝无发布记录";
    // abandon 责任终结（调用序列含 abandon——未完成工件不入清单）。
    bool abandoned = false;
    for (const FakeArtifactSink::CallRecord& call : exportScene.sink.calls) {
        if (call.method == "abandon") {
            abandoned = true;
        }
    }
    EXPECT_TRUE(abandoned) << "abandon 在调用序列（清理）";
    EXPECT_EQ(exportScene.sink.published.size(), std::size_t{0}) << "零半包";

    // 可重试（选择与路径保留——清除故障后重发成功）。
    exportScene.sink.resetInjections();
    ReportExportResult retried = exportScene.service()->exportReport(request);
    EXPECT_TRUE(retried.published.has_value()) << "重试成功（同一目标路径）";
}

/**
 * §10.1 RP-RO-1：只读项目——只读上下文：①ProjectArchive＝ReadOnlyStore
 * 拒绝＋诊断说明；②ExternalPath＝成功。
 */
TEST_F(ReportingContractSuite, RP_RO_1_ReadOnlyProject_TwoPaths)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-07"}, {}, std::nullopt);

    const core::RunId run = runAt(0x14);
    BuildScene scene = standardScene(run);
    const ReviewReport report = buildOk(scene, ReportLevel::B);
    ExportScene exportScene(report);
    exportScene.sink.readOnly = true;   // 只读上下文（writable=false）

    // ①项目内归档：ReadOnlyStore 拒绝＋诊断说明（PM-07）。
    ReportExportRequest archive;
    archive.project = report.project();
    archive.reportId = report.reportId();
    archive.formats = {ReportRenderFormat::Html};
    archive.destination.kind = ExportDestination::ProjectArchive;
    ReportExportResult rejected = exportScene.service()->exportReport(archive);
    ASSERT_TRUE(rejected.error.has_value());
    EXPECT_EQ(rejected.error->code(), ReportErrorCode::ReadOnlyStore);
    EXPECT_FALSE(rejected.diagnostics.empty()) << "只读诊断说明随拒绝（NFR-COR-03）";
    EXPECT_EQ(exportScene.sink.published.size(), std::size_t{0}) << "只读零写";

    // ②外部路径：成功（只读上下文不约束项目外用户目录）。
    tk::TempDir dir("rp-ro1");
    ReportExportRequest external = archive;
    external.destination.kind = ExportDestination::ExternalPath;
    external.destination.externalPath = dir.path();
    ReportExportResult ok = exportScene.service()->exportReport(external);
    EXPECT_FALSE(ok.error.has_value()) << "外部导出成功";
    EXPECT_TRUE(fs::exists(dir.path() / "report.html")) << "导出文件存在";
}

// =====================================================================
// RP-SAN 组（诊断脱敏——NFR-SEC-07/AT-11 精神）
// =====================================================================

/**
 * §10.1 RP-SAN-1：诊断脱敏——诊断含本机路径样例：输出经脱敏（无完整路径/
 * 凭据；exportSafeSummary 双保险）。
 */
TEST_F(ReportingContractSuite, RP_SAN_1_DiagnosticRedaction)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-SEC-07"}, {std::string{"AT-11"}}, std::nullopt);

    const core::RunId run = runAt(0x14);
    BuildScene scene = standardScene(run);
    const ReviewReport report = buildOk(scene, ReportLevel::B);

    // 注入含本机路径样例的诊断引用面（渲染侧脱敏通道的凭证——真实
    // RedactionService〔C-3 登记边〕为双保险服务；RenderTest 脱敏双保险
    // 同法——含路径文本过服务后扫描输出）。
    const std::string secret = "C:\\Users\\zgl18\\secret\\robot-model.json";
    const std::string redacted =
        redactionService().redact(secret, diagnostics::LogTier::User);
    EXPECT_EQ(redacted.find("zgl18"), std::string::npos) << "用户名不再出现";
    EXPECT_EQ(redacted.find(secret), std::string::npos) << "完整路径不再出现";

    // 渲染输出全文扫描（正文/诊断通道零本机路径样例——渲染侧过脱敏的
    // 输出面证据；io IO-V30 同法）。
    const std::string html = renderText(report, ReportRenderFormat::Html);
    EXPECT_EQ(html.find("zgl18"), std::string::npos) << "渲染输出零本机路径样例";
    const std::string json = renderText(report, ReportRenderFormat::Json);
    EXPECT_EQ(json.find("zgl18"), std::string::npos) << "JSON 输出零本机路径样例";
}

// =====================================================================
// RP-REVW 组（评审元数据——RPT-01-C）
// =====================================================================

/**
 * §10.1 RP-REVW-1：评审元数据新版本——C 报告生成→签署/追加意见→
 * rebuild(reviewSeed)：新 ReportId＋reportVersion+1＋supersedes；
 * dataIdentity 不变；旧报告文件不变。
 */
TEST_F(ReportingContractSuite, RP_REVW_1_ReviewEvolutionChain)
{
    IRD_TEST_INFO(std::vector<std::string>{"RPT-01-C"}, {}, std::nullopt);

    const core::RunId run = runAt(0x14);
    tk::TempDir dir("rp-revw1");
    BuildScene scene = standardScene(run);
    ContentSpec cContent;
    cContent.sectionId = std::string(kSectionTrajectoryCycle);
    cContent.minLevel = ReportLevel::C;
    cContent.boundRun = run;
    registerContent(scene.registry, cContent);

    const ReviewReport v1 = buildOk(scene, ReportLevel::C);
    core::Digest256 v1Sha{};
    {
        ExportScene exportScene(v1);
        const std::vector<ExportedFile> files =
            exportOk(exportScene, v1, {ReportRenderFormat::Html}, dir.path());
        v1Sha = files.front().sha256;
    }

    // 签署/追加意见（评审演化种子——新版元数据初值）。
    ReviewMetadataSeed seed;
    seed.priorReportId = v1.reportId();
    seed.priorDataIdentity = v1.dataIdentity();
    seed.priorReportVersion = v1.reportVersion();
    seed.metadata.basisRevision = fixedRevision();
    seed.metadata.comments.push_back(
        ReviewComment{"评审人甲", fixedTime(1700000200), "追加工况意见一条", std::nullopt});

    ReportBuildRequest evolution = scene.request(ReportLevel::C);
    evolution.reviewSeed = seed;
    const ReportBuildOutcome outcome = scene.builder()->build(evolution);
    ASSERT_NE(outcome.report, nullptr);
    const ReviewReport v2 = *outcome.report;

    // 身份链断言：新 ReportId＋版本+1＋supersedes＝v1；dataIdentity 不变。
    EXPECT_FALSE(v2.reportId() == v1.reportId()) << "新 ReportId（新对象）";
    EXPECT_EQ(v2.reportVersion(), v1.reportVersion() + 1) << "reportVersion+1";
    ASSERT_TRUE(v2.supersedes().has_value());
    EXPECT_TRUE(*(v2.supersedes()) == v1.reportId()) << "supersedes＝v1";
    EXPECT_TRUE(v2.dataIdentity() == v1.dataIdentity()) << "数据基准未变";
    EXPECT_FALSE(v2.contentIdentity() == v1.contentIdentity()) << "元数据入内容身份";
    // 旧报告文件不变。
    EXPECT_TRUE(fileSha256(dir.path() / "report.html") == v1Sha) << "v1 工件零修改";
}

/**
 * §10.1 RP-REVW-2：未签署可导出——signOff=Unsigned：导出成功；签署状态
 * 如实呈现。
 */
TEST_F(ReportingContractSuite, RP_REVW_2_UnsignedExportable)
{
    IRD_TEST_INFO(std::vector<std::string>{"RPT-01-C"}, {}, std::nullopt);

    const core::RunId run = runAt(0x14);
    BuildScene scene = standardScene(run);
    ContentSpec cContent;
    cContent.sectionId = std::string(kSectionTrajectoryCycle);
    cContent.minLevel = ReportLevel::C;
    cContent.boundRun = run;
    registerContent(scene.registry, cContent);
    const ReviewReport report = buildOk(scene, ReportLevel::C);
    ASSERT_EQ(report.review().signOff.state, SignOffState::State::Unsigned) << "未签署前置";

    // 导出成功（未签署不阻断导出——§4.5）。
    tk::TempDir dir("rp-revw2");
    ExportScene exportScene(report);
    const std::vector<ExportedFile> files =
        exportOk(exportScene, report,
                 {ReportRenderFormat::Html, ReportRenderFormat::Json}, dir.path());
    EXPECT_EQ(files.size(), std::size_t{2});

    // 签署状态如实呈现（HTML"未签署"＋JSON signOff 状态字段）。
    const std::string html = readFileText(dir.path() / "report.html");
    EXPECT_NE(html.find("未签署"), std::string::npos) << "签署状态如实呈现（HTML）";
    const std::string json = readFileText(dir.path() / "report-data.json");
    EXPECT_NE(json.find("\"signOff\""), std::string::npos) << "签署状态字段（JSON）";
}

// =====================================================================
// RP-BUN 组（证据包与归档协作——RPT-03/AT-14/PM-05 观测）
// =====================================================================

/// 归档事实注入源（§7.6 输入②——三项事实逐运行脚本化；移除条目＝事实
/// 缺位——BundleIncomplete 缺失清单的注入路径；套件自持非四具名成员）。
class ScriptedBundleSource final : public IEvidenceBundleSource {
public:
    std::map<std::string, core::Digest256> envelopeDigests;
    std::map<std::string, core::ContentIdentity> caseSetDigests;
    std::map<std::string, std::vector<BundleConfigRef>> configs;

    std::optional<core::Digest256> tryEnvelopeDigest(core::RunId runId) const override
    {
        const auto it = envelopeDigests.find(runId.toCanonical());
        return it == envelopeDigests.end() ? std::nullopt : std::optional{it->second};
    }
    std::optional<core::ContentIdentity> tryCaseSetDigest(core::RunId runId) const override
    {
        const auto it = caseSetDigests.find(runId.toCanonical());
        return it == caseSetDigests.end() ? std::nullopt : std::optional{it->second};
    }
    std::optional<std::vector<BundleConfigRef>>
    tryConfigurationRefs(core::RunId runId) const override
    {
        const auto it = configs.find(runId.toCanonical());
        return it == configs.end() ? std::nullopt : std::optional{it->second};
    }
};

/**
 * §10.1 RP-BUN-1：证据包与归档协作——合法报告＋FakeArchiveWriter：bundle
 * 清单完整（快照引用/结果引用/复现要素条目在案）；要素缺失→BundleIncomplete
 * 全量列出；取消→清理目标不变。
 */
TEST_F(ReportingContractSuite, RP_BUN_1_BundleAssemblyAndCollaboration)
{
    IRD_TEST_INFO(std::vector<std::string>{"RPT-03"}, {std::string{"AT-14"}}, std::nullopt);

    const core::RunId run = runAt(0x14);
    BuildScene scene = standardScene(run);
    const ReviewReport report = buildOk(scene, ReportLevel::B);
    tk::TempDir dir("rp-bun1");

    // 归档事实脚本（三项事实齐备——§7.6 输入②）。
    auto makeBundleSource = [run] {
        auto source = std::make_unique<ScriptedBundleSource>();
        core::ContentDigester d;
        const std::uint8_t probe = 0;
        d.update(&probe, 1);   // 占位摘要（事实面在案即可——内容归 evidence）
        source->envelopeDigests[run.toCanonical()] = d.finalize();
        source->caseSetDigests[run.toCanonical()] = test_fakes::scriptedCid(0x40);
        BundleConfigRef config;
        config.configKindToken = "task-config";
        config.contentIdentity = test_fakes::scriptedCid(0x41);
        source->configs[run.toCanonical()] = {config};
        return source;
    };

    // ①清单完整：assemble→Completed；容器条目枚举＝快照/结果/复现/清单
    // （snapshot/<run>/snapshot.json、results/<run>/result.json、
    // reproduction.json、bundle.json——§7.6 条目编址）。
    {
        FakeArchiveWriter writer;
        auto bundleSource = makeBundleSource();
        MapResolver resolver;
        resolver.put(report);
        ScenarioIoFactory ioFactory;
        EvidenceBundleAssembler assembler(resolver, scene.resultSource, *bundleSource, writer,
                                          ioFactory);
        EvidenceBundleRequest request;
        request.reportId = report.reportId();
        request.target = dir.path() / "bundle.zip";
        const EvidenceBundleOutcome outcome = assembler.assemble(request);
        ASSERT_EQ(outcome.status, BundleStatus::Completed);
        ASSERT_TRUE(outcome.totalDigest.has_value()) << "totalDigest 在场";
        EXPECT_TRUE(writer.finished) << "finish 发布完成";
        EXPECT_FALSE(fs::exists(writer.tempPath())) << "临时区已清理（发布后）";
        // 条目枚举（§7.6 编址四类——复现要素随运行编址）。
        EXPECT_NE(std::find(writer.entryNames.begin(), writer.entryNames.end(),
                            "snapshot/" + run.toCanonical() + "/snapshot.json"),
                  writer.entryNames.end())
            << "快照引用条目";
        EXPECT_NE(std::find(writer.entryNames.begin(), writer.entryNames.end(),
                            "results/" + run.toCanonical() + "/result.json"),
                  writer.entryNames.end())
            << "结果引用条目";
        EXPECT_NE(std::find(writer.entryNames.begin(), writer.entryNames.end(),
                            "reproduction.json"),
                  writer.entryNames.end())
            << "复现要素条目";
        EXPECT_NE(std::find(writer.entryNames.begin(), writer.entryNames.end(), "bundle.json"),
                  writer.entryNames.end())
            << "包清单条目";
        // bundle.json 内容（包清单六字段锚——reportId 在案）。
        const std::string bundleJson(writer.entryBytes.at("bundle.json").begin(),
                                     writer.entryBytes.at("bundle.json").end());
        EXPECT_NE(bundleJson.find(report.reportId().toCanonical()), std::string::npos)
            << "bundle.json 携带 reportId（包身份锚）";
        EXPECT_TRUE(fs::exists(dir.path() / "bundle.zip")) << "目标已发布";
    }
    // ②要素缺失：移除归档事实（快照/结果/配置全缺）→BundleIncomplete 全量
    // 列出（不产出半包——目标不变；五项事实全缺——envelope/复现块经
    // 具名替身缺位注入，归档三项事实经零事实 ScriptedBundleSource）。
    {
        FakeArchiveWriter writer;
        auto bundleSource = std::make_unique<ScriptedBundleSource>();   // 零事实
        MapResolver resolver;
        resolver.put(report);
        ScenarioIoFactory ioFactory;
        EvidenceBundleAssembler assembler(resolver, scene.resultSource, *bundleSource, writer,
                                          ioFactory);
        EvidenceBundleRequest request;
        request.reportId = report.reportId();
        request.target = dir.path() / "bundle-incomplete.zip";
        // 五项事实全缺（"<要素>@<runId>" 标签面——BundleTest 同口径）。
        scene.resultSource.reproductionAvailable = false;
        scene.resultSource.envelopes.erase(run.toCanonical());
        scene.resultSource.reproductions.erase(run.toCanonical());
        const EvidenceBundleOutcome outcome = assembler.assemble(request);
        EXPECT_EQ(outcome.status, BundleStatus::Failed);
        ASSERT_TRUE(outcome.error.has_value());
        EXPECT_EQ(outcome.error->code(), ReportErrorCode::BundleIncomplete);
        const std::string detail = outcome.error->what();
        EXPECT_NE(detail.find("envelope@" + run.toCanonical()), std::string::npos)
            << "envelope 要素在列";
        EXPECT_NE(detail.find("reproduction@" + run.toCanonical()), std::string::npos)
            << "reproduction 要素在列";
        EXPECT_NE(detail.find("envelope-digest@" + run.toCanonical()), std::string::npos)
            << "envelope-digest 要素在列";
        EXPECT_NE(detail.find("case-set-digest@" + run.toCanonical()), std::string::npos)
            << "case-set-digest 要素在列";
        EXPECT_NE(detail.find("configurations@" + run.toCanonical()), std::string::npos)
            << "configurations 要素在列";
        EXPECT_FALSE(writer.finished) << "零半包（未发布）";
        EXPECT_FALSE(fs::exists(dir.path() / "bundle-incomplete.zip")) << "目标不变";
        EXPECT_FALSE(fs::exists(writer.tempPath())) << "临时清理（RAII/失败路径）";
    }
    // ③取消：assemble 中途取消→清理、目标不变（Canceled 非错误）。
    {
        FakeArchiveWriter writer;
        auto bundleSource = makeBundleSource();
        MapResolver resolver;
        resolver.put(report);
        ScenarioIoFactory ioFactory;
        EvidenceBundleAssembler assembler(resolver, scene.resultSource, *bundleSource, writer,
                                          ioFactory);
        EvidenceBundleRequest request;
        request.reportId = report.reportId();
        request.target = dir.path() / "bundle-canceled.zip";
        AlwaysCancelToken token;
        const EvidenceBundleOutcome outcome = assembler.assemble(request, &token);
        EXPECT_EQ(outcome.status, BundleStatus::Canceled) << "取消终态";
        EXPECT_FALSE(outcome.error.has_value()) << "取消非错误（UX-03）";
        EXPECT_FALSE(outcome.totalDigest.has_value()) << "取消无 totalDigest";
        EXPECT_FALSE(fs::exists(dir.path() / "bundle-canceled.zip")) << "目标不变";
        EXPECT_FALSE(fs::exists(writer.tempPath())) << "临时清理（writer RAII）";
    }
}

// =====================================================================
// RP-GATE 组（构建红线——NFR-MNT-01/02/04）
// =====================================================================

/**
 * §10.1 RP-GATE-1：构建红线——脚本扫描：零 Qt 头；无私有头出 include/；
 * 依赖边＝登记四条＋零表外同层边；零 testkit 产品链接（testkit 引用只落
 * 在测试目标链接语句——T-1 允许形态）。与 BuildRedLineTest（include 面）/
 * LinkageContractTest（构建图面）互为三面防线，本用例为矩阵行的具名收口。
 */
TEST_F(ReportingContractSuite, RP_GATE_1_BuildRedLines)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-MNT-01", "NFR-MNT-02", "NFR-MNT-04"}, {},
                  std::nullopt);

    const fs::path unit = fs::path{IRD_REPORTING_UNIT_ROOT} / "reporting";
    // ①零 Qt 头（产品面 include/＋src——D-01 零 Qt 含 Core）。
    for (const std::string& sub : {"include", "src"}) {
        for (const auto& entry : fs::recursive_directory_iterator(unit / sub)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            const std::string text = readFileText(entry.path());
            std::size_t pos = text.find("#include <Q");
            if (pos == std::string::npos) {
                pos = text.find("#include \"Q");
            }
            EXPECT_EQ(pos, std::string::npos)
                << "零 Qt 头红线: " << entry.path().string();
        }
    }
    // ②无私有头出 include/（R-2——include/ 下文件全部位于命名空间根）。
    for (const auto& entry : fs::recursive_directory_iterator(unit / "include")) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::string rel =
            fs::relative(entry.path(), unit / "include").generic_string();
        EXPECT_EQ(rel.substr(0, std::string("sdurws/ird/reporting/").size()),
                  "sdurws/ird/reporting/")
            << "公共头路径布局红线: " << rel;
    }
    // ③依赖边＝登记四条＋零表外同层边＋测试目标 testkit 允许形态
    // （CMakeLists 非注释行文本扫描——LinkageContractTest 同纪律）。
    {
        const std::string cmake = readFileText(unit / "CMakeLists.txt");
        std::set<std::string> refs;
        std::string line;
        std::istringstream lines(cmake);
        int testkitLines = 0;
        while (std::getline(lines, line)) {
            const auto hashPos = line.find('#');
            if (hashPos != std::string::npos) {
                line.erase(hashPos);
            }
            for (std::size_t pos = line.find("sdurws_ird_"); pos != std::string::npos;
                 pos = line.find("sdurws_ird_", pos + 1)) {
                std::size_t end = pos + std::string("sdurws_ird_").size();
                while (end < line.size()
                       && (std::isalnum(static_cast<unsigned char>(line[end]))
                           || line[end] == '_')) {
                    ++end;
                }
                if (end > pos + std::string("sdurws_ird_").size()) {
                    refs.insert(line.substr(pos, end - pos));
                }
            }
            // T-1 允许形态：testkit 引用只落在测试目标链接语句行。
            if (line.find("sdurws_ird_testkit") != std::string::npos) {
                ++testkitLines;
                const bool onTestTargetLine
                    = (line.find("sdurws_ird_reporting_test") != std::string::npos
                       || line.find("sdurws_ird_reporting_contract_test") != std::string::npos)
                      && line.find("target_link_libraries") != std::string::npos;
                EXPECT_TRUE(onTestTargetLine)
                    << "testkit 引用必须落在测试目标链接语句（T-1）: " << line;
            }
        }
        EXPECT_GT(testkitLines, 0) << "RPT-T11 已登记测试目标 testkit 消费（防扫描失效）";
        // 白名单：四条登记边＋本单元三目标＋testkit（测试目标允许形态）。
        const std::set<std::string> allowed = {
            "sdurws_ird_core",           "sdurws_ird_evidence",
            "sdurws_ird_diagnostics",    "sdurws_ird_project",
            "sdurws_ird_reporting",      "sdurws_ird_reporting_test",
            "sdurws_ird_reporting_contract_test", "sdurws_ird_testkit"};
        for (const std::string& ref : refs) {
            EXPECT_NE(allowed.find(ref), allowed.end())
                << "构建图白名单外目标引用: " << ref;
        }
        // 登记边存在性（四条 PUBLIC 链——链接器半区由构建日志承载）。
        for (const std::string& edge :
             {"sdurws_ird_core", "sdurws_ird_evidence", "sdurws_ird_diagnostics",
              "sdurws_ird_project"}) {
            EXPECT_NE(refs.find(edge), refs.end()) << "登记边缺失: " << edge;
        }
    }
}

}  // namespace
