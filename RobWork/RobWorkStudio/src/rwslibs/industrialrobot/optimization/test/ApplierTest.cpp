/**
 * @file   ApplierTest.cpp
 * @brief  候选应用组装面模型测试（OptApplier 组）——§10.3 六项前置校验、
 *         两步命令语义组装、通道化（P-OPT-3）、差异预览警告（P-OPT-8）、
 *         纯函数性（预览候选不修改基线）——任务契约 WP-20-T07 acceptance
 *         1/2/3 的模型面（经①端口的执行面归 contract_test/ApplierContractTest）。
 *
 * 设计依据：
 *   - units/optimization.md §10.1（流程 V1~V3/ASM）、§10.2（命令组合与
 *     原子性）、§10.3（六项前置校验清单——逐项失败语义）、§10.4（差异
 *     预览＝Model Diff 消费；P-OPT-8 范围外项给警告不虚构）、§12.2
 *     （buildApplyPlan 契约：不执行提交；kOptApplyPlanInvalid 抛出面）
 *   - 需求 OPT-08（候选归属/预览不改基线/设为当前方案三件套）、AT-12
 *     （基线保护——纯函数性用例承载模型面）
 *   - 已知风险 P-OPT-3（物化缝未注入 ⇒ allowApply=false——通道化用例）、
 *     P-OPT-8（传动比差异范围外警告用例）、P-PR-9/DOPT-6（组装面零
 *     命令 token 注册——词表常量只引用）
 *
 * 测试形态：模型测试＝直调计算库（NFR-MNT-01）——两条注入缝以脚本化
 * 替身供给（P-OPT-2/P-OPT-3 裁决前接缝形态，与 T04 探针/T05 指标投影/
 * T06 ICandidateProjector 同款）；接口路径钉扎（全部经
 * IOptimizationCandidateApplier 虚接口消费——WP-20-T03 B-1 教训）。
 */

#include <sdurws/ird/optimization/Applier.hpp>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/core/Units.hpp>
#include <sdurws/ird/optimization/DiagCodes.hpp>
#include <sdurws/ird/optimization/Run.hpp>
#include <sdurws/ird/optimization/Types.hpp>
#include <sdurws/ird/optimization/Variable.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

using namespace sdurws::ird;
using optimization::ApplyCandidateRequest;
using optimization::CandidateApplyPlan;
using optimization::CandidateId;
using optimization::CandidatePatch;
using optimization::IOptimizationCandidateApplier;
using optimization::OptimizationCandidateApplier;
using optimization::OptimizationRunId;
using optimization::OptimizationRunResult;
using optimization::PatchItem;
using optimization::RunPhase;
using optimization::TwoStageRunRecord;
using optimization::TwoStageRunResult;
using optimization::VariableBinding;
using optimization::VariableKind;

namespace {

// =====================================================================
// 注入缝替身（脚本化——同 T04/T05/T06 接缝形态）
// =====================================================================

/// 脚本化物化缝：返回固定字节并记录调用次数（纯函数面——同输入同输出）。
class ScriptedMaterializer final : public optimization::ICandidateDesignMaterializer {
public:
    explicit ScriptedMaterializer(std::vector<std::uint8_t> payload) noexcept
        : m_payload(std::move(payload)) {}

    std::vector<std::uint8_t> materialize(
        const CandidatePatch& /*patch*/,
        const std::vector<VariableBinding>& /*bindings*/) const override
    {
        ++m_calls;  // mutable 计数（投影纯函数契约——T06 ICandidateProjector 同款）
        return m_payload;
    }

    int calls() const noexcept { return m_calls; }

private:
    std::vector<std::uint8_t> m_payload;
    mutable int m_calls = 0;
};

/// 脚本化预览缝：返回预置条目/警告（modeling diff 的投影替身）。
class ScriptedDiffSource final : public optimization::ICandidateDiffSource {
public:
    optimization::CandidateDiffPreview diff(
        const std::vector<std::uint8_t>& baseline,
        const std::vector<std::uint8_t>& candidate) const override
    {
        // 纯函数契约断言：同输入同输出（NFR-COR-02——缝实现侧的确定性）。
        ++m_calls;
        optimization::CandidateDiffPreview out = m_preview;
        // 把两侧字节长度织入首条目摘要——验证缝收到的就是组装面的两侧
        // 字节（基线＝请求携带、候选＝物化产出）。
        if (!out.entries.empty()) {
            out.entries[0].baselineText =
                "baseline:" + std::to_string(baseline.size());
            out.entries[0].candidateText =
                "candidate:" + std::to_string(candidate.size());
        }
        return out;
    }

    void setPreview(optimization::CandidateDiffPreview preview)
    {
        m_preview = std::move(preview);
    }

    int calls() const noexcept { return m_calls; }

private:
    optimization::CandidateDiffPreview m_preview;
    mutable int m_calls = 0;
};

// =====================================================================
// 运行聚合与请求构造（RunTest 同款字面量身份——确定性）
// =====================================================================

constexpr const char* kCvHex =
    "00000000000000000000000000000000000000000000000000000000000000b7";
constexpr const char* kCidHex =
    "0000000000000000000000000000000000000000000000000000000000000c1d";

/// 已授权传动比绑定（词表 mdl.drivetrain.ratio[j] 实例化——P-OPT-8 用例主角）。
VariableBinding makeRatioBinding()
{
    VariableBinding b;
    b.bindingId = "mdl.drivetrain.ratio[1]";
    b.kind = VariableKind::Continuous;
    const auto u = core::UnitToken::find("1");  // 传动比 c＝Δq_joint/Δθ_motor，无量纲
    if (u.has_value()) {
        b.unit = *u;
    }
    b.lowerBound = 40.0;
    b.upperBound = 160.0;
    b.authorityFieldPath = "robot-drivetrain/ratioPerJoint[j]";
    b.authorized = true;
    b.locked = false;
    return b;
}

/// 已授权 DH 长度绑定（对照组——非传动比变量的 diff 路径）。
VariableBinding makeDhBinding()
{
    VariableBinding b;
    b.bindingId = "mdl.joint[2].dh.a";
    b.kind = VariableKind::Continuous;
    const auto u = core::UnitToken::find("m");  // DH 长度，单位 m
    if (u.has_value()) {
        b.unit = *u;
    }
    b.lowerBound = 0.05;
    b.upperBound = 0.40;
    b.authorityFieldPath = "robot-design/joints[i]/dh/a";
    b.authorized = true;
    b.locked = false;
    return b;
}

/// 可采用候选记录（Verified-Feasible-资格齐备；补丁由调用方给定）。
TwoStageRunRecord makeApplicableRecord(const CandidatePatch& patch,
                                       const core::ObjectId& root,
                                       const core::ContentVersion& cv)
{
    TwoStageRunRecord r;
    r.candidateId = optimization::candidateIdOf(root, cv, patch);
    r.patch = patch;
    r.isBaseline = patch.items.empty();
    r.mode = core::EvaluationMode::Verified;
    r.screeningOnly = false;
    r.status = optimization::CandidateStatus::Feasible;
    r.formalPassEligible = true;
    return r;
}

/// 组装完成的可采用运行聚合（Completed＋单一 Verified-Feasible 候选）。
struct RunFixture {
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
    OptimizationRunResult run;

    explicit RunFixture(const CandidatePatch& patch)
    {
        TwoStageRunResult done;
        done.runPhase = RunPhase::Completed;
        done.runCompleted = true;
        done.verifiedRecords = {makeApplicableRecord(patch, baselineRoot, baselineCv)};
        run = optimization::assembleRunResult(
            OptimizationRunId::generate(), project, branch, revision, snapshot,
            baselineRoot, baselineCv, optimization::OptimizationConfiguration{},
            done);
        // 变量绑定集补入（物化/预览的消费输入——运行冻结配置的一部分）。
        run.config.variables = {makeRatioBinding(), makeDhBinding()};
    }
};

/// 组装请求（基线状态默认"tip＝运行修订、可写"——正面形态）。
ApplyCandidateRequest makeRequest(const RunFixture& f,
                                  const std::vector<std::uint8_t>& baselineBytes)
{
    ApplyCandidateRequest req;
    req.run = f.run;
    req.candidateId = f.run.candidates.at(0).candidateId;
    req.baseline.branch = f.branch;
    req.baseline.tip = f.revision;
    req.baseline.writable = true;
    req.baselineDesignCanonical = baselineBytes;
    return req;
}

}  // namespace

// =====================================================================
// 正面组装（acceptance 2 的模型面——两步命令语义三件套）
// =====================================================================

TEST(OptApplier, BuildPlanHappyPathTwoStepSemantics_WP20T07_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-08"}, std::vector<std::string>{"AT-12"});

    RunFixture f{CandidatePatch{}};  // 基线候选（空补丁合法形态）
    const std::vector<std::uint8_t> baselineBytes{0x52, 0x44, 0x31};  // 任意基线字节
    ScriptedMaterializer materializer({0x43, 0x41, 0x4E, 0x44});      // 候选字节
    ScriptedDiffSource diffSource;
    diffSource.setPreview({{{"parameters", "modified", "obj-00000000000000000000000000000001",
                             "joints[2].dh.a", "dh.a", true, false, "0.25", "0.30"}},
                           {}});

    OptimizationCandidateApplier applier({&materializer, &diffSource});
    const CandidateApplyPlan plan
        = applier.buildApplyPlan(makeRequest(f, baselineBytes));

    // 通道化全绿：可应用＋零阻断＋零过期标记。
    EXPECT_TRUE(plan.allowApply);
    EXPECT_TRUE(plan.blockedReasons.empty());
    EXPECT_FALSE(plan.expectedStaleBaseline);

    // step1 建支语义：基修订＝运行输入修订（分支表 baseRevisionId 记录、
    // 不复制对象——OPT-VER-145 的组装面语义）；默认命名非空。
    EXPECT_EQ(plan.createBranch.baseRevisionId, f.revision);
    EXPECT_FALSE(plan.createBranch.label.empty());

    // step2 应用语义：token＝modeling 既有注册面（只引用不注册——DOPT-6；
    // 常量词表钉扎）；payload＝物化字节；expectedRevision＝运行输入修订
    // （新分支 tip＝建支基——project §4.5.1）；双编译恒声明（MDL-06）。
    ASSERT_TRUE(plan.applyDesign.has_value());
    EXPECT_EQ(plan.applyDesign->commandType,
              std::string(optimization::kApplyRobotDesignCommandToken));
    EXPECT_EQ(optimization::kApplyRobotDesignCommandToken, "apply-robot-design");
    EXPECT_EQ(plan.applyDesign->payloadCanonical,
              std::vector<std::uint8_t>({0x43, 0x41, 0x4E, 0x44}));
    EXPECT_EQ(plan.applyDesign->expectedRevision, f.revision);
    EXPECT_TRUE(plan.applyDesign->requiresDualCompile);

    // 采用三件套之三：完整复算提示恒在（§10.1 RECALC——复核完成前不
    // 沿用原通过结论）。
    EXPECT_TRUE(plan.recalcRequired);

    // 差异预览：条目＝缝产出原样（不重解释）；两侧字节通道正确（基线＝
    // 请求字节、候选＝物化字节——织入摘要断言）。
    ASSERT_EQ(plan.diffPreview.entries.size(), 1U);
    EXPECT_EQ(plan.diffPreview.entries[0].baselineText, "baseline:3");
    EXPECT_EQ(plan.diffPreview.entries[0].candidateText, "candidate:4");
    EXPECT_TRUE(plan.diffPreview.warnings.empty())
        << "非传动比候选且缝产出含对应条目——零警告";
}

TEST(OptApplier, BuildPlanDefaultLabelDerivedFromCandidateId_WP20T07)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-08", "PM-12"}, std::vector<std::string>{"AT-12"});

    // 默认命名规则（实现口径）："方案 "＋候选 id 前 12 位——可辨识且
    // 不参与身份（P-PR-8：label 一次写入无改名入口）。
    RunFixture f{CandidatePatch{}};
    ScriptedMaterializer materializer({0x01});
    OptimizationCandidateApplier applier({&materializer, nullptr});
    ApplyCandidateRequest req = makeRequest(f, {0x01});
    const CandidateApplyPlan plan = applier.buildApplyPlan(req);

    const std::string idText = req.candidateId.toCanonical();  // "cnd-<64hex>"
    EXPECT_EQ(plan.createBranch.label, "方案 " + idText.substr(4, 12));

    // 显式分支名优先（用户意图——组装面不覆盖）。
    req.branchLabel = "高速版";
    const CandidateApplyPlan named = applier.buildApplyPlan(req);
    EXPECT_EQ(named.createBranch.label, "高速版");
}

// =====================================================================
// §10.3 前置校验逐项（调用方错误 fail-fast——kOptApplyPlanInvalid）
// =====================================================================

TEST(OptApplier, RejectsNonCompletedRun_WP20T07)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-08", "TASK-02"}, std::vector<std::string>{});

    // §10.3 第 1 项：取消/失败/中断结果不可应用（取消态聚合——RunTest
    // 同款构造；RunPhase 越集由聚合工厂拦截，此处验证 Canceled 终态）。
    RunFixture f{CandidatePatch{}};
    f.run.runPhase = RunPhase::Canceled;   // 直接改投影（聚合为纯值——测试
                                            // 便利构造；生产路径由工厂保证）
    f.run.allowFormalExport = false;
    ScriptedMaterializer materializer({0x01});
    OptimizationCandidateApplier applier({&materializer, nullptr});

    bool thrown = false;
    try {
        applier.buildApplyPlan(makeRequest(f, {0x01}));
    } catch (const optimization::OptimizationError& e) {
        thrown = true;
        EXPECT_EQ(e.stableCode(),
                  std::string(optimization::kOptApplyPlanInvalid));
        EXPECT_NE(std::string(e.what()).find("Completed"), std::string::npos)
            << "消息必须携带实际状态与要求（ERR-01 定位）";
    }
    EXPECT_TRUE(thrown) << "非 Completed 运行必须 fail-fast（TASK-02）";
}

TEST(OptApplier, RejectsQuickScreeningOnlyRecord_WP20T07)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-08", "EVI-01"}, std::vector<std::string>{});

    // §10.3 第 2 项（效力半区）：Quick 筛选记录绝不支撑采用——
    // "Quick 不得单独支撑正式通过"（EVI-01/P-EV-8）的采用面闸门。
    RunFixture f{CandidatePatch{}};
    f.run.candidates[0].screeningOnly = true;
    f.run.candidates[0].formalPassEligible = false;  // Quick 记录恒无资格
    ScriptedMaterializer materializer({0x01});
    OptimizationCandidateApplier applier({&materializer, nullptr});

    bool thrown = false;
    try {
        applier.buildApplyPlan(makeRequest(f, {0x01}));
    } catch (const optimization::OptimizationError& e) {
        thrown = true;
        EXPECT_EQ(e.stableCode(),
                  std::string(optimization::kOptApplyPlanInvalid));
    }
    EXPECT_TRUE(thrown);
}

TEST(OptApplier, RejectsInfeasibleAndIneligibleCandidates_WP20T07)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-08", "RPT-05"}, std::vector<std::string>{"AT-09"});

    // §10.3 第 2 项（状态/资格半区）：非可行状态（Infeasible）与无正式
    // 资格（RPT-05 五条件不齐）都不可采用——状态词不同，拦截同一面。
    ScriptedMaterializer materializer({0x01});
    OptimizationCandidateApplier applier({&materializer, nullptr});

    {
        RunFixture f{CandidatePatch{}};
        f.run.candidates[0].status = optimization::CandidateStatus::Infeasible;
        bool thrown = false;
        try {
            applier.buildApplyPlan(makeRequest(f, {0x01}));
        } catch (const optimization::OptimizationError&) {
            thrown = true;
        }
        EXPECT_TRUE(thrown) << "Infeasible 候选不可应用（不进可行集——AT-09）";
    }
    {
        RunFixture f{CandidatePatch{}};
        f.run.candidates[0].formalPassEligible = false;  // 证据不齐
        bool thrown = false;
        try {
            applier.buildApplyPlan(makeRequest(f, {0x01}));
        } catch (const optimization::OptimizationError& e) {
            thrown = true;
            EXPECT_EQ(e.stableCode(),
                      std::string(optimization::kOptApplyPlanInvalid));
        }
        EXPECT_TRUE(thrown) << "FormalPassEligibility 未过不可应用（RPT-05）";
    }
}

TEST(OptApplier, RejectsForeignOrMissingCandidate_WP20T07)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-08"}, std::vector<std::string>{});

    // §10.3 第 3 项：归属核对（内容寻址重算失配/候选不在运行中——防
    // "把别的运行的结果应用到此基线"的静默错位）。
    ScriptedMaterializer materializer({0x01});
    OptimizationCandidateApplier applier({&materializer, nullptr});

    {
        // 候选不在运行中（异运行身份）。
        RunFixture f{CandidatePatch{}};
        ApplyCandidateRequest req = makeRequest(f, {0x01});
        req.candidateId = optimization::candidateIdOf(
            core::ObjectId::fromCanonical("obj-00000000000000000000000000000009"),
            core::ContentVersion::fromCanonical(std::string("cv-") + kCvHex),
            CandidatePatch{});
        bool thrown = false;
        try {
            applier.buildApplyPlan(req);
        } catch (const optimization::OptimizationError& e) {
            thrown = true;
            EXPECT_EQ(e.stableCode(),
                      std::string(optimization::kOptApplyPlanInvalid));
        }
        EXPECT_TRUE(thrown);
    }
    {
        // 记录在运行中但基线锚失配（归属重算不一致——记录被篡改/组装
        // 错位的防御面）。
        RunFixture f{CandidatePatch{}};
        f.run.baselineRoot = core::ObjectId::fromCanonical(
            "obj-00000000000000000000000000000009");  // 换基线锚——身份公式变
        bool thrown = false;
        try {
            applier.buildApplyPlan(makeRequest(f, {0x01}));
        } catch (const optimization::OptimizationError&) {
            thrown = true;
        }
        EXPECT_TRUE(thrown) << "归属重算失配必须拦截（§10.3 第 3 项）";
    }
}

TEST(OptApplier, RejectsMalformedRequestStructure_WP20T07)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-08"}, std::vector<std::string>{});

    // 结构违约（std::invalid_argument——与域异常分轨：请求连合法形态都
    // 不构成）。
    RunFixture f{CandidatePatch{}};
    ScriptedMaterializer materializer({0x01});
    OptimizationCandidateApplier applier({&materializer, nullptr});

    {
        ApplyCandidateRequest req = makeRequest(f, {0x01});
        req.candidateId = CandidateId{};  // 全零候选身份
        EXPECT_THROW(applier.buildApplyPlan(req), std::invalid_argument);
    }
    {
        ApplyCandidateRequest req = makeRequest(f, {0x01});
        req.baseline.tip = core::RevisionId{};  // 全零 tip
        EXPECT_THROW(applier.buildApplyPlan(req), std::invalid_argument);
    }
}

// =====================================================================
// §10.3 第 4 项与通道化（第 5/6 项——P-OPT-3/PM-07）
// =====================================================================

TEST(OptApplier, StaleBaselineFlaggedNotBlocking_WP20T07)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-08", "PM-04"}, std::vector<std::string>{"AT-12"});

    // §10.3 第 4 项：tip ≠ 运行输入修订 ⇒ 预期拒绝标记（**不阻塞组装**——
    // 原文；最终裁决在提交期 project S2——OPT-VER-146 的组装面半区）。
    RunFixture f{CandidatePatch{}};
    ScriptedMaterializer materializer({0x01});
    OptimizationCandidateApplier applier({&materializer, nullptr});

    ApplyCandidateRequest req = makeRequest(f, {0x01});
    req.baseline.tip = core::RevisionId::fromCanonical(
        "rev-00000000000000000000000000000099");  // HEAD 已前进
    const CandidateApplyPlan plan = applier.buildApplyPlan(req);

    EXPECT_TRUE(plan.expectedStaleBaseline);
    EXPECT_TRUE(plan.allowApply) << "过期标记不阻塞组装（§10.3 原文）";
    EXPECT_TRUE(plan.blockedReasons.empty());
    ASSERT_TRUE(plan.applyDesign.has_value());
    EXPECT_EQ(plan.applyDesign->expectedRevision, f.revision)
        << "expectedRevision 保持运行输入修订——提交期失配即 "
           "PRJ-STALE-REVISION-REJECTED";
}

TEST(OptApplier, ChannelizesMissingMaterializer_WP20T07)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-08"}, std::vector<std::string>{});

    // P-OPT-3 通道化：物化缝未注入（裁决前生产常态）⇒ 组装成功返回但
    // allowApply=false＋阻断原因——应用功能不启用，候选只导出（不违反
    // OPT-08 的 R1 验收前状态）；step2 未组装（nullopt——不可消费）。
    RunFixture f{CandidatePatch{}};
    OptimizationCandidateApplier applier({nullptr, nullptr});
    const CandidateApplyPlan plan = applier.buildApplyPlan(makeRequest(f, {}));

    EXPECT_FALSE(plan.allowApply);
    ASSERT_EQ(plan.blockedReasons.size(), 1U);
    EXPECT_EQ(plan.blockedReasons[0],
              std::string(optimization::kApplyBlockMaterializationUnavailable));
    EXPECT_FALSE(plan.applyDesign.has_value())
        << "无物化 ⇒ 无 step2 载荷（组装面零字节构造逻辑——N1）";
    // step1 语义仍完整（能力面与语义面分离——装配后即可执行）。
    EXPECT_EQ(plan.createBranch.baseRevisionId, f.revision);
}

TEST(OptApplier, ChannelizesReadOnlyProject_WP20T07)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-08", "PM-07"}, std::vector<std::string>{});

    // §10.3 第 6 项/§10.2 红线：只读项目应用入口禁用（通道化——PM-07）。
    RunFixture f{CandidatePatch{}};
    ScriptedMaterializer materializer({0x01});
    OptimizationCandidateApplier applier({&materializer, nullptr});

    ApplyCandidateRequest req = makeRequest(f, {0x01});
    req.baseline.writable = false;
    const CandidateApplyPlan plan = applier.buildApplyPlan(req);

    EXPECT_FALSE(plan.allowApply);
    ASSERT_EQ(plan.blockedReasons.size(), 1U);
    EXPECT_EQ(plan.blockedReasons[0],
              std::string(optimization::kApplyBlockProjectReadOnly));

    // 双重阻断（无物化＋只读）：原因按校验序追加（5 在前 6 在后——确定性）。
    OptimizationCandidateApplier noMat({nullptr, nullptr});
    req.baseline.writable = false;
    const CandidateApplyPlan both = noMat.buildApplyPlan(req);
    ASSERT_EQ(both.blockedReasons.size(), 2U);
    EXPECT_EQ(both.blockedReasons[0],
              std::string(optimization::kApplyBlockMaterializationUnavailable));
    EXPECT_EQ(both.blockedReasons[1],
              std::string(optimization::kApplyBlockProjectReadOnly));
}

// =====================================================================
// 差异预览（P-OPT-8——范围外项给警告不虚构）
// =====================================================================

TEST(OptApplier, RatioPatchWithoutDiffEntryWarnsNotFabricates_WP20T07_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-08", "MDL-08"}, std::vector<std::string>{"AT-12"});

    // P-OPT-8：候选补丁触及传动比、而 modeling diff（根对象范围）无
    // ratioPerJoint 条目 ⇒ 追加范围外警告；条目表保持 diff 实际产出
    // （**不虚构差异**——警告与条目两轴分离）。
    // 绑定表先行构造（makeCandidatePatch 需要词表匹配的绑定集——与
    // RunFixture 构造尾部补入的集合同构）。
    const std::vector<VariableBinding> vars = {makeRatioBinding(), makeDhBinding()};
    const CandidatePatch ratioPatch = optimization::makeCandidatePatch(
        vars, optimization::OptimizationStage::StageB,
        {{"mdl.drivetrain.ratio[1]", 120.0, 0, {}}}, "ratio 候选");
    RunFixture f{ratioPatch};
    const std::vector<std::uint8_t> baselineBytes{0x42};
    ScriptedMaterializer materializer({0x43, 0x44});
    ScriptedDiffSource diffSource;
    // 根对象范围 diff：只有 DH 条目、无 ratio 条目（现状建模 diff 面）。
    diffSource.setPreview({{{"parameters", "modified", "", "joints[2].dh.a",
                             "dh.a", true, false, "0.25", "0.30"}},
                           {}});
    OptimizationCandidateApplier applier({&materializer, &diffSource});
    const CandidateApplyPlan plan = applier.buildApplyPlan(makeRequest(f, baselineBytes));

    ASSERT_TRUE(plan.allowApply);
    ASSERT_EQ(plan.diffPreview.entries.size(), 1U)
        << "条目＝diff 实际产出（不虚构 ratio 条目——P-OPT-8 原文）";
    EXPECT_EQ(plan.diffPreview.entries[0].field, "dh.a");
    ASSERT_EQ(plan.diffPreview.warnings.size(), 1U);
    EXPECT_EQ(plan.diffPreview.warnings[0],
              std::string(optimization::kDiffWarningRatioOutOfScope));
}

TEST(OptApplier, RatioPatchWithDiffEntryNoWarning_WP20T07)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-08"}, std::vector<std::string>{});

    // 对照面：diff 产出含 ratioPerJoint 条目（P-OPT-8 裁决后 modeling
    // diff 范围扩展的将来形态）⇒ 零范围外警告。
    const std::vector<VariableBinding> vars = {makeRatioBinding(), makeDhBinding()};
    const CandidatePatch ratioPatch = optimization::makeCandidatePatch(
        vars, optimization::OptimizationStage::StageB,
        {{"mdl.drivetrain.ratio[1]", 120.0, 0, {}}}, "ratio 候选");
    RunFixture f{ratioPatch};
    ScriptedMaterializer materializer({0x43, 0x44});
    ScriptedDiffSource diffSource;
    diffSource.setPreview({{{"parameters", "modified", "",
                             "robot-drivetrain/ratioPerJoint[1]",
                             "ratioPerJoint", true, false, "100.0", "120.0"}},
                           {}});
    OptimizationCandidateApplier applier({&materializer, &diffSource});
    const CandidateApplyPlan plan = applier.buildApplyPlan(makeRequest(f, {0x42}));

    EXPECT_TRUE(plan.diffPreview.warnings.empty())
        << "条目在册 ⇒ 无范围外警告（警告只在缺失时追加）";
}

TEST(OptApplier, PreviewDegradedWhenSourceOrInputMissing_WP20T07)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-08", "MDL-08"}, std::vector<std::string>{"AT-12"});

    // 预览是前置呈现而非前提：缝未装配/基线字节缺失 ⇒ 降级警告（如实
    // 告知"差异可能不全"），不阻断组装、不虚构空差异冒充"无差异"。
    RunFixture f{CandidatePatch{}};
    ScriptedMaterializer materializer({0x01});
    ScriptedDiffSource diffSource;
    OptimizationCandidateApplier applier({&materializer, &diffSource});

    {
        // 基线字节未提供（空）——预览输入不足。
        ApplyCandidateRequest req = makeRequest(f, {});
        const CandidateApplyPlan plan = applier.buildApplyPlan(req);
        EXPECT_TRUE(plan.allowApply) << "预览降级不阻断应用";
        ASSERT_EQ(plan.diffPreview.warnings.size(), 1U);
        EXPECT_EQ(plan.diffPreview.warnings[0],
                  std::string(optimization::kDiffWarningSourceUnavailable));
        EXPECT_TRUE(plan.diffPreview.entries.empty()) << "零条目＝未预览，非无差异";
        EXPECT_EQ(diffSource.calls(), 0) << "缝未被调用（输入不足短路）";
    }
    {
        // 缝未装配。
        OptimizationCandidateApplier noDiff({&materializer, nullptr});
        const CandidateApplyPlan plan = noDiff.buildApplyPlan(makeRequest(f, {0x42}));
        EXPECT_TRUE(plan.allowApply);
        ASSERT_EQ(plan.diffPreview.warnings.size(), 1U);
        EXPECT_EQ(plan.diffPreview.warnings[0],
                  std::string(optimization::kDiffWarningSourceUnavailable));
    }
}

// =====================================================================
// 纯函数性（acceptance 1 模型面——预览候选不得修改基线）
// =====================================================================

TEST(OptApplier, BuildPlanPureFunctionInputUntouched_WP20T07_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-08"}, std::vector<std::string>{"AT-12"});

    // 组装是 const 纯函数：同请求两次调用产出等价计划（NFR-COR-02），
    // 且请求输入（运行聚合——候选归属容器）逐字节不被触碰——"预览候选
    // 不得修改基线"（OPT-08）的类型面承载（经①端口的执行面保证归
    // contract_test 的真实存储用例）。
    RunFixture f{CandidatePatch{}};
    ScriptedMaterializer materializer({0x43, 0x44});
    ScriptedDiffSource diffSource;
    diffSource.setPreview({{{"properties", "modified", "", "links[1].mass",
                             "mass", true, false, "12.0", "11.5"}},
                           {}});
    const OptimizationCandidateApplier applier({&materializer, &diffSource});

    const ApplyCandidateRequest req = makeRequest(f, {0x42, 0x43});
    const optimization::OptimizationRunResult runBefore = req.run;

    const CandidateApplyPlan a = applier.buildApplyPlan(req);
    const CandidateApplyPlan b = applier.buildApplyPlan(req);

    // 两次组装等价（值相等——CandidateApplyPlan 无自定义 ==，逐字段核对
    // 关键面：通道化/标记/两步语义/复算位/预览条目数）。
    EXPECT_EQ(a.allowApply, b.allowApply);
    EXPECT_EQ(a.blockedReasons, b.blockedReasons);
    EXPECT_EQ(a.expectedStaleBaseline, b.expectedStaleBaseline);
    EXPECT_EQ(a.createBranch.baseRevisionId, b.createBranch.baseRevisionId);
    EXPECT_EQ(a.createBranch.label, b.createBranch.label);
    ASSERT_TRUE(a.applyDesign.has_value());
    ASSERT_TRUE(b.applyDesign.has_value());
    EXPECT_EQ(a.applyDesign->commandType, b.applyDesign->commandType);
    EXPECT_EQ(a.applyDesign->payloadCanonical, b.applyDesign->payloadCanonical);
    EXPECT_EQ(a.applyDesign->expectedRevision, b.applyDesign->expectedRevision);
    EXPECT_EQ(a.recalcRequired, b.recalcRequired);
    EXPECT_EQ(a.diffPreview.entries.size(), b.diffPreview.entries.size());

    // 输入运行聚合不被触碰（候选归属容器完整性——OPT-08"候选归属于
    // OptimizationRunResult"的不可变纪律面）。
    EXPECT_EQ(req.run.candidates.size(), runBefore.candidates.size());
    EXPECT_EQ(req.run.candidates[0].candidateId,
              runBefore.candidates[0].candidateId);
    EXPECT_EQ(req.run.runPhase, runBefore.runPhase);
    EXPECT_EQ(req.run.revision, runBefore.revision);
    // 物化缝被调用（每组装一次——payload 即时产出，无隐藏缓存）。
    EXPECT_EQ(materializer.calls(), 2);
}
