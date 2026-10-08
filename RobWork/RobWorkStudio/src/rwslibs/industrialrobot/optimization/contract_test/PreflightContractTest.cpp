/**
 * @file   PreflightContractTest.cpp
 * @brief  优化预检 Preflight 跨单元契约测试（WP-20-T08）——真 L5 装配链
 *         （evidence 双注册表＋opt-static-screen 注册）下的阻塞可定位契约
 *         （OPT-VER-112/135 契约半区）与 P-OPT-6 持久化通道边界（会话态
 *         ＋导出副本承载、不入 .rwdesign 的源码面承载）。
 *
 * 追溯：OPT-11、units/optimization.md §6.4（检查项全表/五元组）、§16.3
 * P-OPT-6（"R1 会话态＋研究结果 JSON 副本承载研究定义〔§11.4 工件 1〕；
 * 不入 .rwdesign"——裁决前允许范围＝会话态＋导出副本）；测试矩阵
 * OPT-VER-112（输入前置阻断——阻塞项逐项定位＋allowStart=false）/
 * OPT-VER-135（人为缺评估器/策略——阻塞计数＋逐项定位＋basis＋suggestion）。
 *
 * 契约面口径：模型测试（PreflightTest.cpp）以局部 fixture 覆盖 20 项逐项
 * 语义；本文件以**真 evidence 注册表装配**（Profile 先于评估器注册——
 * §6.7 顺序约束）证明同一服务在真实装配链下的可定位性——两层互补
 * （T04/T06 契约测试同款分工）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/evidence/Evaluator.hpp>
#include <sdurws/ird/evidence/Snapshot.hpp>
#include <sdurws/ird/optimization/Objective.hpp>
#include <sdurws/ird/optimization/Preflight.hpp>
#include <sdurws/ird/optimization/Variable.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace optimization = sdurws::ird::optimization;
namespace evidence = sdurws::ird::evidence;
namespace core = sdurws::ird::core;

namespace {

/// optimization 单元树根（IRD_OPTIMIZATION_UNIT_ROOT 注入——BuildGraph
/// 契约测试同款）。
const fs::path& unitRoot()
{
    static const fs::path dir = fs::path{IRD_OPTIMIZATION_UNIT_ROOT};
    return dir;
}

/// 读取文件全文；不可读显性失败（不留假阳性通道）。
std::string readFile(const fs::path& file)
{
    EXPECT_TRUE(fs::exists(file)) << file.string() << " 不存在";
    std::ifstream in(file, std::ios::binary);
    EXPECT_TRUE(static_cast<bool>(in)) << "无法读取 " << file.string();
    return std::string{std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>()};
}

/// 修订闭包真值源（全部 (oid,cv) 声明在册——真快照组装入口）。
class AllPresentClosure final : public evidence::IRevisionClosureSource {
public:
    bool objectInRevision(core::RevisionId, core::ObjectId,
                          core::ContentVersion) const override
    {
        return true;
    }
};

/// 非零内容身份。
core::ContentIdentity makeContentIdentity(unsigned char seed)
{
    core::ContentIdentity id;
    id.bytes.fill(seed);
    return id;
}

/// 闭包条目（digest=cv 字节）。
evidence::ObjectRefEntry makeClosureEntry(std::string typeToken)
{
    evidence::ObjectRefEntry entry;
    entry.objectId = core::ObjectId::generate();
    entry.contentVersion = core::ContentVersion::fromCanonical(
        "cv-0000000000000000000000000000000000000000000000000000000000000001");
    entry.objectTypeToken = std::move(typeToken);
    entry.digest = entry.contentVersion.bytes;
    return entry;
}

/// 真 L5 装配夹具：evidence 双注册表（Profile "opt" 先行——§6.7 顺序约束）
/// ＋opt-static-screen 契约版本 1 注册。
struct RealAssemblyFixture {
    evidence::EvidenceProfileRegistry profiles;
    evidence::EvaluatorRegistry evaluators{profiles};
    core::ContentIdentity optProfileIdentity{};

    RealAssemblyFixture()
    {
        evidence::RequiredEvidenceProfile profile;
        profile.profileId = std::string(optimization::kOptProfileId);
        profile.version = "1.0";
        profiles.registerProfile(profile);
        optProfileIdentity
            = profiles.findProfile(std::string(optimization::kOptProfileId), "1.0")
                  ->contentIdentity;
        evidence::EvaluatorDescriptor d;
        d.key = std::string(optimization::kOptStaticScreenKey);
        d.contractVersion = optimization::kOptStaticScreenContractVersion;
        d.profile.profileId = std::string(optimization::kOptProfileId);
        d.profile.version = "1.0";
        d.supportedModes = {core::EvaluationMode::Quick,
                            core::EvaluationMode::Verified};
        d.stateless = true;
        d.threadSafety = evidence::ThreadSafety::FullyThreadSafe;
        evaluators.registerEvaluator(
            std::make_unique<ScriptedFactory>(d), {});
    }

    /// 空 evaluate 替身工厂（Preflight 只消费注册清单 descriptor——评估
    /// 行为本面不涉；工厂类型在夹具内自持）。
    class ScriptedEvaluator final : public evidence::IEngineeringEvaluator {
    public:
        explicit ScriptedEvaluator(evidence::EvaluatorDescriptor descriptor)
            : m_descriptor(std::move(descriptor))
        {
        }
        const evidence::EvaluatorDescriptor& descriptor() const override
        {
            return m_descriptor;
        }
        evidence::EvaluationOutput evaluate(const evidence::EvaluationRequest&,
                                            evidence::IEvaluationContext&) override
        {
            return {};
        }

    private:
        evidence::EvaluatorDescriptor m_descriptor;
    };

    class ScriptedFactory final : public evidence::IEvaluatorFactory {
    public:
        explicit ScriptedFactory(evidence::EvaluatorDescriptor descriptor)
            : m_descriptor(std::move(descriptor))
        {
        }
        const evidence::EvaluatorDescriptor& descriptor() const override
        {
            return m_descriptor;
        }
        std::unique_ptr<evidence::IEngineeringEvaluator> create() const override
        {
            return std::make_unique<ScriptedEvaluator>(m_descriptor);
        }

    private:
        evidence::EvaluatorDescriptor m_descriptor;
    };

    /// 工件齐全的快照（modeling 两工件＋requirements 五对象＋策略＋名称
    /// 映射＋两必验工况——真 builder 组装，身份为计算值）。
    evidence::AnalysisSnapshot completeSnapshot() const
    {
        evidence::SnapshotBuilder builder;
        builder.setIdentity(core::ProjectId::generate(), core::BranchId::generate(),
                            core::RevisionId::generate(), 1U);
        for (const std::string_view token :
             {optimization::kObjectTypeRobotDesign,
              optimization::kObjectTypeRobotDrivetrain,
              optimization::kObjectTypeReqRootSet,
              optimization::kObjectTypeReqPointSet,
              optimization::kObjectTypeReqRegionSet,
              optimization::kObjectTypeReqConditionSet,
              optimization::kObjectTypeReqPlanSet}) {
            builder.addObjectRef(makeClosureEntry(std::string(token)));
        }
        evidence::PolicyRef policy;
        policy.policyContentIdentity = makeContentIdentity(4);
        builder.setPolicyRef(policy);
        evidence::NameMapRef nameMap;
        nameMap.nameMapContentIdentity = makeContentIdentity(5);
        builder.setNameMapRef(nameMap);
        builder.addCase({core::ObjectId::generate(), "额定工况", true, true});
        builder.addCase({core::ObjectId::generate(), "极限工况", true, true});
        evidence::ReproductionBlock repro;
        repro.productVersion = "test";
        repro.evidenceContractVersion = "1";
        builder.setReproduction(repro);
        return builder.build(AllPresentClosure{});
    }

    /// 合法检查输入（基线投影与快照一致）。
    optimization::PreflightInputs inputs(const evidence::AnalysisSnapshot& s) const
    {
        optimization::PreflightInputs in;
        in.snapshot = s;
        in.baseline.branch = s.branch;
        in.baseline.tip = s.revision;
        in.baseline.writable = true;
        in.evaluatorRegistry = &evaluators;
        in.profileRegistry = &profiles;
        return in;
    }

    /// 合法运行描述（身份与快照一致；单连续绑定＋StageB 默认目标）。
    optimization::OptimizationRunSpec spec(const evidence::AnalysisSnapshot& s) const
    {
        optimization::OptimizationRunSpec sp;
        sp.project = s.project;
        sp.branch = s.branch;
        sp.revision = s.revision;
        sp.snapshotId = s.snapshotId;
        sp.config.stage = optimization::OptimizationStage::StageB;
        sp.config.seed = 7;
        sp.config.objectives
            = optimization::defaultObjectives(optimization::OptimizationStage::StageB);
        optimization::VariableBinding binding;
        binding.bindingId = "mdl.joint[2].dh.a";
        binding.kind = optimization::VariableKind::Continuous;
        binding.unit = core::UnitToken::find("m").value();
        binding.lowerBound = 0.2;   // m
        binding.upperBound = 0.8;   // m
        binding.defaultValue = 0.5; // m
        binding.authorityFieldPath = "robot-design/joints[2]/dh/a";
        // diagSubject 指向闭包内 robot-design 对象（T03 绑定校验的闭包核对
        // ——随机身份会产生检查 3 悬空定位阻塞，污染真装配正例）。
        for (const auto& entry : s.objectClosure) {
            if (entry.objectTypeToken == optimization::kObjectTypeRobotDesign) {
                binding.diagSubject = entry.objectId.toCanonical();
                break;
            }
        }
        sp.config.variables = {binding};
        sp.profile.profileId = std::string(optimization::kOptProfileId);
        sp.profile.version = "1.0";
        sp.profile.contentIdentity = optProfileIdentity;
        sp.createdBy = "preflight-contract-test";
        return sp;
    }
};

/// 五元组齐备断言（§6.4 输出契约——subject/basis/suggestion 全非空，
/// basis 与 suggestion 面向"不看文档的工程师"自足可读）。
void expectFindingWellFormed(const optimization::PreflightFinding& finding)
{
    EXPECT_FALSE(finding.subject.empty())
        << "checkId " << static_cast<int>(finding.checkId) << " 的 subject 不得为空";
    EXPECT_FALSE(finding.basis.empty())
        << "checkId " << static_cast<int>(finding.checkId) << " 的 basis 不得为空";
    EXPECT_FALSE(finding.suggestion.empty())
        << "checkId " << static_cast<int>(finding.checkId) << " 的 suggestion 不得为空";
    EXPECT_GE(finding.checkId, 1U);
    EXPECT_LE(finding.checkId, 20U);
}

}  // namespace

// =====================================================================
// OPT-VER-112/135 契约半区：真装配下阻塞项逐项可定位
// =====================================================================

TEST(OptPreflightContract, BlocksLocatableOnRealAssembly_OPTVER112_135)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-11"}, std::vector<std::string>{"AT-09"});

    RealAssemblyFixture f;
    const auto snapshot = f.completeSnapshot();

    // 场景一（OPT-VER-135"人为缺评估器"）：注册表整体缺失 → 检查 7 阻塞，
    // 五元组齐备、定位到评估键 opt-static-screen。
    auto inputsNoRegistry = f.inputs(snapshot);
    inputsNoRegistry.evaluatorRegistry = nullptr;
    const optimization::OptimizationPreflightService noRegistry(inputsNoRegistry);
    const auto reportNoRegistry = noRegistry.preflight(f.spec(snapshot));
    ASSERT_GT(reportNoRegistry.blockerCount, 0U);
    const optimization::PreflightFinding* evaluatorFinding = nullptr;
    for (const auto& finding : reportNoRegistry.findings) {
        expectFindingWellFormed(finding);
        if (finding.checkId == optimization::kPreflightCheckEvaluatorMissing) {
            evaluatorFinding = &finding;
        }
    }
    ASSERT_NE(evaluatorFinding, nullptr) << "缺评估器必须由检查 7 定位";
    EXPECT_EQ(evaluatorFinding->subject, "opt-static-screen");
    EXPECT_EQ(evaluatorFinding->basis, "§6.1、EvaluatorSetId");
    EXPECT_FALSE(reportNoRegistry.allowances.allowStart)
        << "OPT-VER-112：阻塞存在时 allowStart=false";

    // 场景二（OPT-VER-112"缺 req-* 工件"）：快照闭包剥离 requirements
    // 五对象 → 检查 5 逐条阻塞（缺几条报几条——逐项定位非聚合一条）。
    evidence::SnapshotBuilder builder;
    builder.setIdentity(core::ProjectId::generate(), core::BranchId::generate(),
                        core::RevisionId::generate(), 1U);
    for (const std::string_view token :
         {optimization::kObjectTypeRobotDesign,
          optimization::kObjectTypeRobotDrivetrain}) {
        builder.addObjectRef(makeClosureEntry(std::string(token)));
    }
    evidence::PolicyRef policy;
    policy.policyContentIdentity = makeContentIdentity(4);
    builder.setPolicyRef(policy);
    evidence::NameMapRef nameMap;
    nameMap.nameMapContentIdentity = makeContentIdentity(5);
    builder.setNameMapRef(nameMap);
    builder.addCase({core::ObjectId::generate(), "额定工况", true, true});
    evidence::ReproductionBlock repro;
    repro.productVersion = "test";
    repro.evidenceContractVersion = "1";
    builder.setReproduction(repro);
    const auto snapshotNoReq = builder.build(AllPresentClosure{});

    const optimization::OptimizationPreflightService svc(f.inputs(snapshotNoReq));
    const auto report = svc.preflight(f.spec(snapshotNoReq));
    std::size_t reqFindings = 0;
    for (const auto& finding : report.findings) {
        expectFindingWellFormed(finding);
        if (finding.checkId == optimization::kPreflightCheckRequirementArtifacts) {
            ++reqFindings;
            EXPECT_EQ(finding.basis, "requirements.md §8.2");
        }
    }
    EXPECT_EQ(reqFindings, 5U)
        << "五对象全缺必须逐条定位（req-set＋四集合各一条——逐项非聚合）";
    EXPECT_FALSE(report.allowances.allowQuick);
}

// =====================================================================
// 真装配正例： allowances 五元组全放开（allowFormalExport 契约面）
// =====================================================================

TEST(OptPreflightContract, CompleteAssemblyAllowsFormalExport_WP20T08)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-11"}, std::vector<std::string>{"AT-09"});

    RealAssemblyFixture f;
    const auto snapshot = f.completeSnapshot();
    const optimization::OptimizationPreflightService svc(f.inputs(snapshot));
    const auto report = svc.preflight(f.spec(snapshot));

    // 真装配全检通过：零阻塞零警告；allowances 五元组全放开（Preview
    // 除外——R1 opt-static-screen 声明 {Quick,Verified} 不含 Preview）。
    EXPECT_EQ(report.blockerCount, 0U);
    EXPECT_EQ(report.warningCount, 0U);
    EXPECT_TRUE(report.allowances.allowStart);
    EXPECT_TRUE(report.allowances.allowQuick);
    EXPECT_TRUE(report.allowances.allowVerified);
    EXPECT_TRUE(report.allowances.allowFormalExport);
    EXPECT_FALSE(report.allowances.allowPreview);
    // 评估器集摘要（EvaluatorSetId 承载）＝真注册表 manifest 摘要。
    EXPECT_TRUE(report.evaluatorSetDigest
                == f.evaluators.manifest().digest);
}

// =====================================================================
// P-OPT-6 契约面：研究配置持久化通道边界（会话态＋导出副本、不入
// .rwdesign——源码面承载）
// =====================================================================

TEST(OptPreflightContract, SessionStateNoPersistenceChannel_WP20T08_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-11"}, std::vector<std::string>{});

    // P-OPT-6（§16.3）：优化研究配置（OptimizationRunSpec/config.opt）的
    // 持久化通道裁决前——R1 会话态＋研究结果 JSON 副本（§11.4 工件 1，
    // WP-20-T09 导出面）承载，**不入 .rwdesign**。本单元 Preflight 交付面
    // 的源码边界：零 io 单元 include（导出写盘归 T09）、零持久化写调用、
    // 零 .rwdesign 字样（项目文件零研究配置通道）。
    const auto header = readFile(unitRoot() / "optimization" / "include"
                                 / "sdurws" / "ird" / "optimization"
                                 / "Preflight.hpp");
    const auto impl = readFile(unitRoot() / "optimization" / "src"
                               / "Preflight.cpp");

    for (const auto* text : {&header, &impl}) {
        EXPECT_EQ(text->find("ird/io/"), std::string::npos)
            << "Preflight 交付面不得 include io 单元（P-OPT-6：导出副本归 T09）";
        EXPECT_EQ(text->find("ofstream"), std::string::npos)
            << "Preflight 交付面零文件写出调用（只读检查面——§6.4）";
        EXPECT_EQ(text->find("fwrite"), std::string::npos)
            << "Preflight 交付面零字节写出调用（只读检查面——§6.4）";
        // 扫描字符串字面量形态（"\".rwdesign\""）——代码级持久化通道的
        // 出现形态；注释中的登记性提及（不入 .rwdesign 的口径说明）不命中。
        EXPECT_EQ(text->find("\".rwdesign\""), std::string::npos)
            << "研究配置不入 .rwdesign（P-OPT-6 裁决前口径）";
    }
}
