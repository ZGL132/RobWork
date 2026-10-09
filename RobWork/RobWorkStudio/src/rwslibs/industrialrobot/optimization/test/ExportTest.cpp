/**
 * @file   ExportTest.cpp
 * @brief  优化证据一站式导出模型测试（OptExport 组）——六工件数据面组装、
 *         契约三元组校验与正式导出阻断（OPT-EXPORT-CONTRACT-STALE）、资格
 *         终判与限定语强制、当前性投影消费、io canonical 写出编排——任务
 *         契约 WP-20-T09 acceptance 1/2/3（OPT-12；AT-34/OPT-VER-141/142/
 *         148；knownPitfalls P-OPT-5/P-RPT-1）。
 *
 * 设计依据：
 *   - units/optimization.md §11.4（六工件契约表＋导出绑定与阻断段）、
 *     §11.2（三态正交——取消/失败/中断/数据不足不冒充正式；当前性是
 *     evidence 投影）、§12.2（buildExportBundle @throws 语义）、§12.3
 *     （错误语义——调用方错误 fail-fast／环境错误结构化返回）、P-OPT-5/
 *     P-OPT-7（口径未冻结按"—"导出）、P-OPT-6（研究定义 JSON 副本承载）
 *   - 需求 OPT-12（一站式导出＋契约过期阻断）、NFR-COR-04（报告可追溯）、
 *     TASK-02（取消结果不入正式可行集）
 *   - 用例与接口路径（WP-20-T03 B-1 教训）：全部经公共接口消费——组装走
 *     IOptimizationExportProvider 虚接口、写出走 writeExportBundle 自由
 *     函数（Export.hpp 公共面），不留只测内部函数的盲区。
 *
 * 测试形态：模型测试＝直调计算库；文件层写出到 TempDir 真实文件并读回
 * 断言（io 设施真工厂——"文件层经 io canonical 写出"的执行证明；候选包
 * 缝以可控替身承载——P-OPT-2 裁决前形态，诚实登记）。
 */

#include <sdurws/ird/optimization/Export.hpp>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/evidence/Currentness.hpp>
#include <sdurws/ird/evidence/Envelope.hpp>
#include <sdurws/ird/io/Csv.hpp>
#include <sdurws/ird/io/Json.hpp>
#include <sdurws/ird/optimization/CandidatePatch.hpp>
#include <sdurws/ird/optimization/DiagCodes.hpp>
#include <sdurws/ird/optimization/Objective.hpp>
#include <sdurws/ird/optimization/Run.hpp>
#include <sdurws/ird/optimization/Types.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

using namespace sdurws::ird;
namespace fs = std::filesystem;

// =====================================================================
// 构造辅助（模型测试自持——固定字面量身份，确定性）
// =====================================================================

namespace {

/// 合法的 64 位十六进制字面量（cv-/cid- 前缀类型的填充体——固定值保证
/// 用例可复现；尾位非零避开保留值）。
constexpr const char* kCvHex =
    "00000000000000000000000000000000000000000000000000000000000000b7";
constexpr const char* kCidHex =
    "0000000000000000000000000000000000000000000000000000000000000c1d";

/// 输入身份四元组＋基线锚（固定字面量——与 RunTest 同源口径）。
struct IdentityInputs {
    core::ProjectId project = core::ProjectId::fromCanonical(
        "prj-00000000000000000000000000000001");
    core::BranchId branch = core::BranchId::fromCanonical(
        "brn-0000000000000000000000000000000a");
    core::RevisionId revision = core::RevisionId::fromCanonical(
        "rev-0000000000000000000000000000002b");
    core::ContentIdentity snapshot = core::ContentIdentity::fromCanonical(
        std::string("cid-") + kCidHex);
    core::ObjectId baselineRoot = core::ObjectId::fromCanonical(
        "obj-00000000000000000000000000000001");
    core::ContentVersion baselineCv = core::ContentVersion::fromCanonical(
        std::string("cv-") + kCvHex);
};

/// 已校验最小配置（默认构造即合法——T06 validateConfiguration 全默认值面；
/// 目标集取 StageB 默认三项静态）。
optimization::OptimizationConfiguration makeConfig()
{
    optimization::OptimizationConfiguration c;
    c.objectives = optimization::defaultObjectives(optimization::OptimizationStage::StageB);
    optimization::validateConfiguration(c);
    return c;
}

/// 运行描述（"opt" Profile 引用——三元组记录面；身份四元组＝固定字面量）。
optimization::OptimizationRunSpec makeSpec()
{
    optimization::OptimizationRunSpec spec;
    const IdentityInputs id;
    spec.project = id.project;
    spec.branch = id.branch;
    spec.revision = id.revision;
    spec.snapshotId = id.snapshot;
    spec.config = makeConfig();
    spec.profile.profileId = "opt";
    spec.profile.version = "1.0";
    spec.profile.contentIdentity = core::ContentIdentity::fromCanonical(
        std::string("cid-") + kCidHex);
    spec.createdBy = "ut-export";
    spec.createdAtUtc = std::chrono::system_clock::time_point{
        std::chrono::milliseconds{1728000000000LL}};  // 固定纪元毫秒（确定性）
    optimization::validateRunSpec(spec);
    return spec;
}

/// 八项指标全量（三实值＋五 StageB"—"——computeStaticMetrics 的 StageB
/// 产出形态；八槽契约由 StaticMetricResult 承载）。
optimization::StaticMetricResult makeMetrics(double envelope, double mass,
                                             double margin)
{
    optimization::StaticMetricResult r;
    r.metrics.resize(optimization::kMetricCount);
    r.metrics[static_cast<std::size_t>(optimization::MetricId::Envelope)] = {
        optimization::MetricId::Envelope, envelope,
        optimization::kMetricGapStage, "ut"};
    r.metrics[static_cast<std::size_t>(optimization::MetricId::StructuralMass)] = {
        optimization::MetricId::StructuralMass, mass,
        optimization::kMetricGapStage, "ut"};
    r.metrics[static_cast<std::size_t>(optimization::MetricId::MinJointMargin)] = {
        optimization::MetricId::MinJointMargin, margin,
        optimization::kMetricGapStage, "ut"};
    for (int i = 3; i < 8; ++i) {
        r.metrics[static_cast<std::size_t>(i)] = {
            static_cast<optimization::MetricId>(i), std::optional<double>{},
            optimization::kMetricGapStage, "StageB 恒—"};
    }
    return r;
}

/// Verified-Feasible 候选记录（含指标/约束事实——导出候选面的真实形态）。
optimization::TwoStageRunRecord makeFeasibleRecord(bool baseline, bool quick,
                                                   const char* cvTail)
{
    optimization::TwoStageRunRecord r;
    const IdentityInputs id;
    r.candidateId = optimization::candidateIdOf(
        id.baselineRoot, id.baselineCv, optimization::CandidatePatch{});
    r.patch = optimization::CandidatePatch{};
    r.isBaseline = baseline;
    r.mode = quick ? core::EvaluationMode::Quick : core::EvaluationMode::Verified;
    r.screeningOnly = quick;
    r.status = optimization::CandidateStatus::Feasible;
    r.formalPassEligible = !quick;
    r.metrics = makeMetrics(baseline ? 3.0 : 2.5, 10.0, 0.4);
    optimization::ConstraintFact fact;
    fact.constraintId = optimization::ConstraintId::JointLimit;
    fact.verdict = optimization::ConstraintVerdict::Satisfied;
    fact.evaluationKey = std::string(optimization::kOptStaticScreenKey);
    fact.subject = "obj-probe-joint";
    fact.actualText = "0.31";
    fact.requiredText = ">= 0.05";
    fact.unitSymbol = "1";
    fact.detail = "关节限位执行完成无 Must 违例";
    r.evaluation.candidateId = r.candidateId;
    r.evaluation.constraintFacts = {fact};
    return r;
}

/// Completed 终态编排产出（Quick 2＋Verified 2＋非支配集——导出结论面素材）。
optimization::TwoStageRunResult makeCompletedOrchestrated()
{
    optimization::TwoStageRunResult r;
    r.runPhase = optimization::RunPhase::Completed;
    r.runCompleted = true;
    optimization::TwoStageRunRecord q1
        = makeFeasibleRecord(true, true, "00000000000000000000000000000000000000000000000000000000000000c1");
    optimization::TwoStageRunRecord q2
        = makeFeasibleRecord(false, true, "00000000000000000000000000000000000000000000000000000000000000c2");
    r.quickRecords = {q1, q2};
    r.verifiedRecords = {makeFeasibleRecord(true, false, kCvHex),
                         makeFeasibleRecord(false, false, kCvHex)};
    // 非支配集（两成员全为非支配——秩 0；去重 1 的对账素材）。
    for (const auto& rec : r.verifiedRecords) {
        optimization::ParetoFrontEntry e;
        e.candidateId = rec.candidateId;
        e.isBaseline = rec.isBaseline;
        e.nondominationRank = 0;
        e.paretoNondominated = true;
        r.pareto.entries.push_back(e);
        r.pareto.nondominatedIds.push_back(rec.candidateId);
        r.pareto.feasibleIds.push_back(rec.candidateId);
    }
    r.pareto.duplicatesDropped = 1;
    // 审计计数（Quick 全评估 2＋复核 2；无缓存会话——查找/命中为零）。
    r.audit.candidatesGenerated = 2;
    r.audit.quickEvaluated = 2;
    r.audit.quickScreenedOut = 0;
    r.audit.verifiedEvaluated = 2;
    return r;
}

/// 归档的 Completed 运行结果（assembleRunResult 工厂产出——归档面已登记；
/// runId 本域生成——身份只需有效，不跨用例固定）。
optimization::OptimizationRunResult makeArchivedCompletedRun()
{
    const IdentityInputs id;
    return optimization::assembleRunResult(
        optimization::OptimizationRunId::generate(),
        id.project, id.branch, id.revision, id.snapshot, id.baselineRoot,
        id.baselineCv, makeConfig(), makeCompletedOrchestrated(), {},
        optimization::ArchivePhase::Archived);
}

/// 归档的 Canceled 运行结果（部分批保留——TASK-02 资格位恒 false）。
optimization::OptimizationRunResult makeArchivedCanceledRun()
{
    optimization::TwoStageRunResult partial;
    partial.runPhase = optimization::RunPhase::Canceled;
    partial.runCompleted = false;
    partial.quickRecords = {makeFeasibleRecord(true, true, kCvHex)};
    partial.audit.candidatesGenerated = 1;
    partial.audit.quickEvaluated = 1;
    const IdentityInputs id;
    return optimization::assembleRunResult(
        optimization::OptimizationRunId::generate(),
        id.project, id.branch, id.revision, id.snapshot, id.baselineRoot,
        id.baselineCv, makeConfig(), partial, {},
        optimization::ArchivePhase::Archived);
}

/// 组装好的导出请求（三元组全部当前——formal 用例的基线请求面）。
optimization::ExportRequest makeRequest(const optimization::OptimizationRunResult& run,
                                        bool formal)
{
    optimization::ExportRequest req;
    req.run = run;
    req.spec = makeSpec();
    req.currentProfile = req.spec.profile;  // 当前面＝记录面（三元组①当前）
    req.evaluatorContractVersionAtRun
        = optimization::kOptStaticScreenContractVersion;  // ②当前
    req.exportContractVersionAtRun = optimization::kOptExportContractVersion;  // ③当前
    req.evaluatorSetId = core::ContentIdentity::fromCanonical(
        std::string("cid-") + kCidHex);
    req.formal = formal;
    return req;
}

/// JSON 对象导航（测试侧键读取——canonical DOM 的 const 查找面）。
const io::JsonValue* jFind(const io::JsonValue& v, const char* key)
{
    return v.findMember(key);
}

/// 临时目录守卫（TempDir 隔离——io 测试同款模式；失败如实失败不伪造）。
struct TempDirGuard {
    fs::path dir;
    TempDirGuard(const char* tag)
        : dir(fs::temp_directory_path()
              / (std::string("ird_wp20_t09_export_") + tag))
    {
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
    }
    ~TempDirGuard()
    {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

/// 候选包写出替身（ICandidatePackageWriter 缝的可控承载——P-OPT-2 裁决前
/// 形态：条目集按"路径长度＋路径＋字节长度＋字节"框架串接成单文件，真实
/// 落盘以驱动编排路径与条目内容断言；产品适配器桥 io IPackageExporter 的
/// 完整 ZIP 语义随 P-OPT-2/MDL-20 联调——诚实登记）。
class PackageWriterFake final : public optimization::ICandidatePackageWriter {
public:
    mutable int calls = 0;                 ///< 写出调用计数（编排驱动观测）
    mutable std::vector<std::string> paths;  ///< 收到的条目路径序（确定性序）

    io::IoResult<void> writePackage(
        const std::vector<optimization::ExportPackageEntry>& entries,
        const fs::path& targetFile) const override
    {
        ++calls;
        std::ofstream out(targetFile, std::ios::binary);
        if (!out) {
            io::IoResult<void> r;
            r.error.code = io::IoErrorCode::ResAccessDenied;
            return r;
        }
        for (const auto& e : entries) {
            paths.push_back(e.packPath);
            const std::uint32_t pathLen = static_cast<std::uint32_t>(e.packPath.size());
            out.write(reinterpret_cast<const char*>(&pathLen), sizeof(pathLen));
            out.write(e.packPath.data(), static_cast<std::streamsize>(e.packPath.size()));
            const std::uint32_t byteLen = static_cast<std::uint32_t>(e.bytes.size());
            out.write(reinterpret_cast<const char*>(&byteLen), sizeof(byteLen));
            out.write(reinterpret_cast<const char*>(e.bytes.data()),
                      static_cast<std::streamsize>(e.bytes.size()));
        }
        io::IoResult<void> r;
        r.error.code = io::IoErrorCode::Ok;
        return r;
    }
};

/// 文本文件读取（读回断言用——测试侧 IO，不触产品面）。
std::string readFileText(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
}

}  // namespace

// =====================================================================
// acceptance 1：六工件全套导出＋审计计数（AT-34/OPT-VER-141）
// =====================================================================

TEST(OptExport, FormalSixArtifactsEndToEnd_WP20T09_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-12", "NFR-COR-04"},
                  std::vector<std::string>{"AT-34"});

    // OPT-VER-141：Completed＋Archived＋三元组当前 ⇒ formal 全套导出，
    // 六工件逐一面齐全且字段符合契约；文件层经真 io 设施落盘（TempDir）。
    const optimization::OptimizationRunResult run = makeArchivedCompletedRun();
    optimization::ExportRequest req = makeRequest(run, /*formal=*/true);

    optimization::OptimizationExportProvider provider;
    const optimization::ExportBundleData bundle
        = provider.buildExportBundle(req);  // 经 IOptimizationExportProvider
                                            //  虚接口消费（接口路径纪律）
    // 状态面：Formal＋三元组全当前（status 字段强制）。限定语无条件推导：
    // 本请求未携带当前性投影 ⇒ 携带 currentness-not-provided（事实自述类
    // 限定语在 Formal 工件上同样在场——绝不默认 Current，EV-CUR-2）。
    EXPECT_EQ(bundle.status, optimization::ExportStatus::Formal);
    ASSERT_EQ(bundle.qualifierTokens.size(), 1U);
    EXPECT_EQ(bundle.qualifierTokens.front(),
              std::string(optimization::kExportQualifierCurrentnessNotProvided));
    EXPECT_TRUE(bundle.contract.current());

    // 工件 1（JSON）：研究定义副本在场（P-OPT-6）＋运行/身份/审计块齐全。
    const io::JsonValue* research = jFind(bundle.researchJson.root, "research");
    ASSERT_NE(research, nullptr);
    const io::JsonValue* config = jFind(*research, "config");
    ASSERT_NE(config, nullptr);
    const io::JsonValue* seed = jFind(*config, "seed");
    ASSERT_NE(seed, nullptr);
    EXPECT_EQ(seed->integerValue, 1) << "默认种子 1（config 副本——P-OPT-6）";
    const io::JsonValue* exportBlock = jFind(bundle.researchJson.root, "export");
    ASSERT_NE(exportBlock, nullptr);
    const io::JsonValue* statusV = jFind(*exportBlock, "status");
    ASSERT_NE(statusV, nullptr);
    EXPECT_EQ(statusV->stringValue, "formal");
    const io::JsonValue* candidates = jFind(bundle.researchJson.root, "candidates");
    ASSERT_NE(candidates, nullptr);
    EXPECT_EQ(candidates->items.size(), run.candidates.size());
    const io::JsonValue* audit = jFind(bundle.researchJson.root, "audit");
    ASSERT_NE(audit, nullptr);
    const io::JsonValue* quickEval = jFind(*audit, "quickEvaluated");
    ASSERT_NE(quickEval, nullptr);
    EXPECT_EQ(quickEval->integerValue, 2);

    // 工件 2~4（CSV）：行数/行宽与运行记录对账。
    EXPECT_EQ(bundle.candidatesCsvRows.size(), run.candidates.size());
    ASSERT_FALSE(bundle.candidatesCsvRows.empty());
    EXPECT_EQ(bundle.candidatesCsvRows.front().size(),
              bundle.candidatesCsvHeader.size()) << "行宽＝表头列数";
    EXPECT_EQ(bundle.auditCsvRows.size(), 15U)
        << "审计行集＝R1 十一行＋R2 三行＋两统计列（P-OPT-5/7 按—）";
    EXPECT_TRUE(bundle.tasksCsvRows.empty()) << "R1 进程内编排＝空任务映射（如实）";

    // 工件 5（MD）：状态/限定语/基线声明呈现。
    EXPECT_NE(bundle.evidenceReportMarkdown.find("formal（正式完整导出）"),
              std::string::npos);
    EXPECT_NE(bundle.evidenceReportMarkdown.find("比较基准声明"), std::string::npos);

    // 工件 6（包）：条目集＝逐候选补丁 canonical＋包清单（来源标识）。
    ASSERT_EQ(bundle.candidatePackageEntries.size(), run.candidates.size() + 1U);
    bool hasManifest = false;
    for (const auto& e : bundle.candidatePackageEntries) {
        if (e.packPath == "candidate-package/manifest.json") {
            hasManifest = true;
        }
    }
    EXPECT_TRUE(hasManifest) << "包内来源标识清单在场";

    // ---- 文件层：真 io 端口＋包缝替身 → 六文件落盘并逐一读回断言。
    TempDirGuard guard("formal");
    PackageWriterFake packageFake;
    optimization::ExportIoPorts ports = optimization::makeDefaultExportIoPorts();
    ports.packageWriter = &packageFake;
    const optimization::ExportWriteResult written
        = optimization::writeExportBundle(bundle, ports, guard.dir);
    ASSERT_TRUE(written.allOk()) << "六工件全部原子就位";
    EXPECT_EQ(packageFake.calls, 1);

    const std::string jsonText = readFileText(guard.dir / "opt-research.json");
    EXPECT_NE(jsonText.find("\"research\""), std::string::npos)
        << "canonical JSON 已落盘（io 单点 canonical——字典序键序）";
    const std::string candidatesCsv = readFileText(guard.dir / "opt-candidates.csv");
    EXPECT_NE(candidatesCsv.find("#rwcsv1"), std::string::npos)
        << "CSV 方言标识行（io §5.1 v1）";
    EXPECT_NE(candidatesCsv.find("candidate_id"), std::string::npos);
    EXPECT_NE(readFileText(guard.dir / "opt-tasks.csv").find("task_project"),
              std::string::npos)
        << "任务明细 CSV 表头在场";
    const std::string auditCsv = readFileText(guard.dir / "opt-audit.csv");
    EXPECT_NE(auditCsv.find("quick_evaluated"), std::string::npos);
    EXPECT_NE(auditCsv.find("ci95_upper_bound"), std::string::npos)
        << "R2 统计列以—在场（P-OPT-7）";
    const std::string md = readFileText(guard.dir / "opt-evidence-report.md");
    EXPECT_NE(md.find("Pareto 非支配集"), std::string::npos);
    const std::string packBytes = readFileText(guard.dir / "opt-candidate-package.zip");
    EXPECT_NE(packBytes.find("candidate-package/manifest.json"), std::string::npos)
        << "包条目经缝落盘（P-OPT-2 裁决前替身承载）";
}

TEST(OptExport, AuditCsvMatchesRunRecord_WP20T09_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-12"}, std::vector<std::string>{"AT-34"});

    // AT-34 导出面半区：审计 CSV 逐行值＝运行记录审计字段＋Pareto 去重数
    //（重放一致性的记录面承载；重放统计对账归契约测试 OptExportContract）。
    const optimization::OptimizationRunResult run = makeArchivedCompletedRun();
    optimization::ExportRequest req = makeRequest(run, /*formal=*/false);
    optimization::OptimizationExportProvider provider;
    const optimization::ExportBundleData bundle = provider.buildExportBundle(req);

    // key-value 行检索（count_name → value 文本）。
    std::vector<std::pair<std::string, std::string>> rows;
    for (const auto& r : bundle.auditCsvRows) {
        ASSERT_EQ(r.size(), 3U);
        std::string value;
        if (r[1].kind == io::CsvCell::Kind::Int) {
            value = std::to_string(r[1].integer);
        } else {
            value = r[1].text;  // "—"（R2 未冻结口径）
        }
        rows.emplace_back(r[0].text, value);
    }
    const auto find = [&rows](const std::string& name) {
        for (const auto& kv : rows) {
            if (kv.first == name) {
                return kv.second;
            }
        }
        return std::string("<missing>");
    };
    EXPECT_EQ(find("candidates_generated"), "2");
    EXPECT_EQ(find("quick_evaluated"), "2");
    EXPECT_EQ(find("quick_screened_out"), "0");
    EXPECT_EQ(find("verified_evaluated"), "2");
    EXPECT_EQ(find("cache_full_hits"), "0");
    EXPECT_EQ(find("duplicates_dropped"), "1") << "去重数＝Pareto dedupCount（T05 审计入口）";
    EXPECT_EQ(find("sensitivity_samples"), "\xE2\x80\x94") << "R2 按—（P-OPT-5）";
    EXPECT_EQ(find("false_rejection_rate"), "\xE2\x80\x94") << "P-OPT-7 未冻结";
    EXPECT_EQ(find("ci95_upper_bound"), "\xE2\x80\x94") << "P-OPT-7 未冻结";
}

// =====================================================================
// acceptance 2：契约过期阻断＋不冒充完整正式导出（OPT-VER-142）
// =====================================================================

TEST(OptExport, ContractStaleBlocksFormalExport_WP20T09_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-12"}, std::vector<std::string>{"AT-34"});

    // OPT-VER-142：Profile/评估器/导出契约三元组任一漂移 ⇒ formal 请求抛
    // OPT-EXPORT-CONTRACT-STALE（阻断；不降级不冒充）；限定语请求放行但
    // 强制携带 export-contract-stale。
    const optimization::OptimizationRunResult run = makeArchivedCompletedRun();

    // 漂移①：Profile 内容身份（evidence 注册表权威值变化——RP-CUR-2 同源）。
    {
        optimization::ExportRequest req = makeRequest(run, /*formal=*/true);
        req.currentProfile.contentIdentity = core::ContentIdentity::fromCanonical(
            std::string("cid-") + std::string(62, '0') + "ff");
        optimization::OptimizationExportProvider provider;
        bool thrown = false;
        try {
            (void)provider.buildExportBundle(req);
        } catch (const optimization::OptimizationError& e) {
            thrown = true;
            EXPECT_EQ(e.stableCode(),
                      std::string(optimization::kOptExportContractStale));
            EXPECT_NE(e.what(), nullptr);
        }
        EXPECT_TRUE(thrown) << "Profile 漂移阻断正式导出";
    }
    // 漂移②：评估器契约版本（记录面 1 vs 现值 2——评估器升级场景）。
    {
        optimization::ExportRequest req = makeRequest(run, /*formal=*/true);
        req.evaluatorContractVersionAtRun = 2;
        optimization::OptimizationExportProvider provider;
        bool thrown = false;
        try {
            (void)provider.buildExportBundle(req);
        } catch (const optimization::OptimizationError& e) {
            thrown = true;
            EXPECT_EQ(e.stableCode(),
                      std::string(optimization::kOptExportContractStale));
        }
        EXPECT_TRUE(thrown) << "评估器契约漂移阻断正式导出";
    }
    // 漂移③：导出契约版本（工件字段契约升级场景）。
    {
        optimization::ExportRequest req = makeRequest(run, /*formal=*/true);
        req.exportContractVersionAtRun
            = optimization::kOptExportContractVersion + 1U;
        optimization::OptimizationExportProvider provider;
        bool thrown = false;
        try {
            (void)provider.buildExportBundle(req);
        } catch (const optimization::OptimizationError& e) {
            thrown = true;
            EXPECT_EQ(e.stableCode(),
                      std::string(optimization::kOptExportContractStale));
        }
        EXPECT_TRUE(thrown) << "导出契约漂移阻断正式导出";
    }
    // 限定语请求＋漂移 ⇒ Qualified＋限定语强制（不阻断但不冒充正式）。
    {
        optimization::ExportRequest req = makeRequest(run, /*formal=*/false);
        req.currentProfile.contentIdentity = core::ContentIdentity::fromCanonical(
            std::string("cid-") + std::string(62, '0') + "ff");
        optimization::OptimizationExportProvider provider;
        const optimization::ExportBundleData bundle = provider.buildExportBundle(req);
        EXPECT_EQ(bundle.status, optimization::ExportStatus::Qualified);
        ASSERT_FALSE(bundle.qualifierTokens.empty());
        bool found = false;
        for (const auto& q : bundle.qualifierTokens) {
            if (q == std::string(optimization::kExportQualifierContractStale)) {
                found = true;
            }
        }
        EXPECT_TRUE(found) << "限定语导出强制携带 export-contract-stale";
    }
}

TEST(OptExport, CanceledInsufficientNeverFormal_WP20T09_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-12", "TASK-02"},
                  std::vector<std::string>{"AT-34"});

    // §12.2 调用示例非法 2：Canceled 运行＋formal=true ⇒ OPT-EXPORT-
    // CONTRACT-STALE 语义拒绝（取消结果不得冒充正式导出）；限定语导出
    // 强制携带 run-not-completed。
    const optimization::OptimizationRunResult canceled = makeArchivedCanceledRun();
    EXPECT_FALSE(canceled.allowFormalExport);
    optimization::OptimizationExportProvider provider;
    {
        optimization::ExportRequest req = makeRequest(canceled, /*formal=*/true);
        bool thrown = false;
        try {
            (void)provider.buildExportBundle(req);
        } catch (const optimization::OptimizationError& e) {
            thrown = true;
            EXPECT_EQ(e.stableCode(),
                      std::string(optimization::kOptExportContractStale));
        }
        EXPECT_TRUE(thrown);
    }
    {
        optimization::ExportRequest req = makeRequest(canceled, /*formal=*/false);
        const optimization::ExportBundleData bundle = provider.buildExportBundle(req);
        EXPECT_EQ(bundle.status, optimization::ExportStatus::Qualified);
        bool found = false;
        for (const auto& q : bundle.qualifierTokens) {
            if (q == std::string(optimization::kExportQualifierRunNotCompleted)) {
                found = true;
            }
        }
        EXPECT_TRUE(found) << "取消结果限定语强制";
    }

    // 数据不足候选（Verified 批 DataInsufficient）⇒ verified-data-insufficient
    // 限定语（"数据不足不得冒充完整正式导出"）。
    optimization::OptimizationRunResult insufficient = makeArchivedCompletedRun();
    insufficient.candidates.back().status
        = optimization::CandidateStatus::DataInsufficient;
    {
        optimization::ExportRequest req = makeRequest(insufficient, /*formal=*/false);
        const optimization::ExportBundleData bundle = provider.buildExportBundle(req);
        bool found = false;
        for (const auto& q : bundle.qualifierTokens) {
            if (q
                == std::string(optimization::kExportQualifierVerifiedDataInsufficient)) {
                found = true;
            }
        }
        EXPECT_TRUE(found);
    }

    // 搜索空（Completed＋可行集空——OPT-VER-120 数据不足语义）⇒ search-empty
    // 限定语（正式导出亦如实携带——空集事实自我声明）。
    optimization::OptimizationRunResult searchEmpty = makeArchivedCompletedRun();
    searchEmpty.pareto.feasibleIds.clear();
    searchEmpty.pareto.nondominatedIds.clear();
    {
        optimization::ExportRequest req = makeRequest(searchEmpty, /*formal=*/true);
        const optimization::ExportBundleData bundle = provider.buildExportBundle(req);
        EXPECT_EQ(bundle.status, optimization::ExportStatus::Formal)
            << "搜索空＝Completed 正常完成（formal 资格不受影响——OPT-VER-120）";
        bool found = false;
        for (const auto& q : bundle.qualifierTokens) {
            if (q == std::string(optimization::kExportQualifierSearchEmpty)) {
                found = true;
            }
        }
        EXPECT_TRUE(found) << "空集事实在限定语面如实声明";
    }
}

TEST(OptExport, CurrentnessProjectionConsumed_WP20T09)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-12", "CON-02"},
                  std::vector<std::string>{"AT-34"});

    // OPT-VER-148（T07 注⑤划归本面）：当前性＝evidence computeCurrentness
    // 投影的消费呈现——Superseded ⇒ superseded 限定语＋CSV 列＋JSON 块；
    // 未携带 ⇒ currentness-not-provided（绝不默认 Current——EV-CUR-2）。
    optimization::OptimizationRunResult run = makeArchivedCompletedRun();
    optimization::OptimizationExportProvider provider;
    {
        // 投影：Superseded（判定本体归 evidence——测试以投影值承载消费面）。
        optimization::ExportRequest req = makeRequest(run, /*formal=*/false);
        evidence::CurrentnessResult cur;
        cur.status = evidence::CurrentnessStatus::Superseded;
        evidence::InvalidationReason reason;
        reason.dependencyKey = "opt.config";
        reason.kind = evidence::InvalidationKind::ConfigurationChanged;
        reason.detail = "config.opt 已变化（种子 1→2）";
        cur.reasons = {reason};
        req.currentness = cur;
        const optimization::ExportBundleData bundle = provider.buildExportBundle(req);
        bool found = false;
        for (const auto& q : bundle.qualifierTokens) {
            if (q == std::string(optimization::kExportQualifierSuperseded)) {
                found = true;
            }
        }
        EXPECT_TRUE(found) << "历史结果过期限定语（CON-02）";
        EXPECT_EQ(bundle.candidatesCsvRows.front()
                      .at(19)  // currentness 列（表头序 20 列中的第 20 位，0 基 19）
                      .text,
                  "superseded");
        const io::JsonValue* curBlock = jFind(bundle.researchJson.root, "currentness");
        ASSERT_NE(curBlock, nullptr);
        const io::JsonValue* st = jFind(*curBlock, "status");
        ASSERT_NE(st, nullptr);
        EXPECT_EQ(st->stringValue, "superseded");
    }
    {
        // 未携带投影 ⇒ CSV 当前列 not-provided（"—"呈现面；不默认 Current）。
        // formal 资格三条件不含当前性投影（当前性是消费呈现面非资格面），
        // Formal 工件的限定语集恒空与"未提供"事实并存——由 CSV 列诚实承载。
        optimization::ExportRequest req = makeRequest(run, /*formal=*/true);
        const optimization::ExportBundleData bundle = provider.buildExportBundle(req);
        EXPECT_EQ(bundle.status, optimization::ExportStatus::Formal);
        EXPECT_EQ(bundle.candidatesCsvRows.front().at(19).text, "not-provided");
    }
}

// =====================================================================
// 请求校验（调用方违约 fail-fast——kOptInputInvalid）
// =====================================================================

TEST(OptExport, RequestValidationFailFast_WP20T09)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-12"}, std::vector<std::string>{});

    optimization::OptimizationExportProvider provider;
    // ①归档资格：会话内结果（pending）不在导出范围——T09 @pre。
    {
        optimization::OptimizationRunResult unarchived = makeArchivedCompletedRun();
        unarchived.archivePhase = optimization::ArchivePhase::Pending;
        optimization::ExportRequest req = makeRequest(unarchived, /*formal=*/false);
        bool thrown = false;
        try {
            (void)provider.buildExportBundle(req);
        } catch (const optimization::OptimizationError& e) {
            thrown = true;
            EXPECT_EQ(e.stableCode(), std::string(optimization::kOptInputInvalid));
        }
        EXPECT_TRUE(thrown) << "未归档运行不可导出（CON-02/PA-2）";
    }
    // ②绑定一致性：spec 与 run 身份错绑即组装违约（§11.4 导出绑定面）。
    {
        optimization::ExportRequest req = makeRequest(makeArchivedCompletedRun(),
                                                      /*formal=*/false);
        req.spec.revision = core::RevisionId::fromCanonical(
            "rev-0000000000000000000000000000003c");
        bool thrown = false;
        try {
            (void)provider.buildExportBundle(req);
        } catch (const optimization::OptimizationError& e) {
            thrown = true;
            EXPECT_EQ(e.stableCode(), std::string(optimization::kOptInputInvalid));
        }
        EXPECT_TRUE(thrown);
    }
    // ③记录面版本未登记（0）——无法对账即组装违约。
    {
        optimization::ExportRequest req = makeRequest(makeArchivedCompletedRun(),
                                                      /*formal=*/false);
        req.exportContractVersionAtRun = 0;
        bool thrown = false;
        try {
            (void)provider.buildExportBundle(req);
        } catch (const optimization::OptimizationError& e) {
            thrown = true;
            EXPECT_EQ(e.stableCode(), std::string(optimization::kOptInputInvalid));
        }
        EXPECT_TRUE(thrown);
    }
}

// =====================================================================
// 确定性与写出编排（acceptance 3——NFR-COR-02＋P-RPT-1）
// =====================================================================

TEST(OptExport, BundleByteDeterministic_WP20T09)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-12", "NFR-COR-02"},
                  std::vector<std::string>{});

    // 同请求双跑 ⇒ 六工件数据面逐字节相同（JSON canonical 字节经 io 单点
    // 比对——确定性书写无时钟/无随机源）。
    const optimization::OptimizationRunResult run = makeArchivedCompletedRun();
    optimization::OptimizationExportProvider provider;
    const optimization::ExportBundleData a
        = provider.buildExportBundle(makeRequest(run, /*formal=*/true));
    const optimization::ExportBundleData b
        = provider.buildExportBundle(makeRequest(run, /*formal=*/true));

    const io::JsonWriteOptions canonical{};
    const io::IoResult<io::IoString> bytesA = io::canonicalizeJson(a.researchJson, canonical);
    const io::IoResult<io::IoString> bytesB = io::canonicalizeJson(b.researchJson, canonical);
    ASSERT_TRUE(bytesA);
    ASSERT_TRUE(bytesB);
    EXPECT_EQ(bytesA.value, bytesB.value) << "研究结果 JSON canonical 字节确定";
    EXPECT_EQ(a.evidenceReportMarkdown, b.evidenceReportMarkdown);
    ASSERT_EQ(a.candidatesCsvRows.size(), b.candidatesCsvRows.size());
    for (std::size_t i = 0; i < a.candidatesCsvRows.size(); ++i) {
        ASSERT_EQ(a.candidatesCsvRows[i].size(), b.candidatesCsvRows[i].size());
        for (std::size_t c = 0; c < a.candidatesCsvRows[i].size(); ++c) {
            EXPECT_EQ(a.candidatesCsvRows[i][c].text, b.candidatesCsvRows[i][c].text);
            EXPECT_EQ(a.candidatesCsvRows[i][c].real, b.candidatesCsvRows[i][c].real);
        }
    }
    EXPECT_EQ(a.candidatePackageEntries.size(), b.candidatePackageEntries.size());
}

TEST(OptExport, WritePortAssemblyViolationFailsFast_WP20T09_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-12"}, std::vector<std::string>{});

    // 写出编排的装配校验：端口缺一即 fail-fast（六工件要么全套要么明确
    // 失败——不出"三件套冒充全套"）。
    const optimization::OptimizationRunResult run = makeArchivedCompletedRun();
    optimization::OptimizationExportProvider provider;
    const optimization::ExportBundleData bundle
        = provider.buildExportBundle(makeRequest(run, /*formal=*/false));
    TempDirGuard guard("ports");

    {
        optimization::ExportIoPorts empty;  // 全空
        bool thrown = false;
        try {
            (void)optimization::writeExportBundle(bundle, empty, guard.dir);
        } catch (const optimization::OptimizationError& e) {
            thrown = true;
            EXPECT_EQ(e.stableCode(), std::string(optimization::kOptInputInvalid));
        }
        EXPECT_TRUE(thrown);
    }
    {
        optimization::ExportIoPorts noPackage = optimization::makeDefaultExportIoPorts();
        noPackage.packageWriter = nullptr;  // 包缝未装配
        bool thrown = false;
        try {
            (void)optimization::writeExportBundle(bundle, noPackage, guard.dir);
        } catch (const optimization::OptimizationError& e) {
            thrown = true;
            EXPECT_EQ(e.stableCode(), std::string(optimization::kOptInputInvalid));
        }
        EXPECT_TRUE(thrown);
    }
}

TEST(OptExport, WriteFailureSurfacesPerArtifact_WP20T09_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-12"}, std::vector<std::string>{});

    // §12.3 环境错误语义：写出失败不抛——逐工件结构化给账（io 稳定码），
    // 其余工件照常就位；allOk()==false 即"部分失败不冒充完整导出"。
    const optimization::OptimizationRunResult run = makeArchivedCompletedRun();
    optimization::OptimizationExportProvider provider;
    const optimization::ExportBundleData bundle
        = provider.buildExportBundle(makeRequest(run, /*formal=*/false));
    TempDirGuard guard("fail");

    // 注入：候选包缝失败（真缝语义——任一工件失败目标不受影响、稳定码
    // 透传、其余工件照常就位）。
    struct FailingPackageWriter final : optimization::ICandidatePackageWriter {
        io::IoResult<void> writePackage(
            const std::vector<optimization::ExportPackageEntry>&,
            const fs::path&) const override
        {
            io::IoResult<void> r;
            r.error.code = io::IoErrorCode::ResAccessDenied;
            return r;
        }
    } failingPackage;
    optimization::ExportIoPorts ports = optimization::makeDefaultExportIoPorts();
    ports.packageWriter = &failingPackage;

    const optimization::ExportWriteResult written
        = optimization::writeExportBundle(bundle, ports, guard.dir);
    EXPECT_FALSE(written.allOk()) << "包失败 ⇒ 非完整导出（不冒充）";
    EXPECT_FALSE(written.candidatePackage.ok);
    EXPECT_FALSE(written.candidatePackage.errorCode.empty())
        << "环境错误带 io 稳定码（结构化返回——§12.3）";
    EXPECT_TRUE(written.researchJson.ok) << "其余工件照常就位";
    EXPECT_TRUE(written.auditCsv.ok);
}
