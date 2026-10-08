/**
 * @file   RunTest.cpp
 * @brief  运行结果聚合面模型测试（OptRun 组）——OptimizationRunId 身份、
 *         归档阶段词表、assembleRunResult 聚合工厂（资格位推导/候选归属/
 *         终态校验）——任务契约 WP-20-T07 acceptance 1（"候选归属
 *         OptimizationRunResult，不产生项目修订"的模型面承载）。
 *
 * 设计依据：
 *   - units/optimization.md §4.2（运行身份 opt-run-<32hex>/tasks[] 映射）、
 *     §4.4 I-OPT-6（归档后不可修改）、§10.6/§11.2（OptimizationRunResult
 *     聚合：候选/指标/Pareto/审计；TASK-02 取消结果无正式资格）、§12.2
 *     （Export @pre archivePhase==Archived——词表面）
 *   - 需求 OPT-08（候选归属运行结果——本文件验证聚合的**归属容器**语义：
 *     纯值、零项目写面、拷贝独立）、TASK-02（取消/失败/中断不得进入正式
 *     可行集——allowFormalExport 位）、CON-02（历史不可变——值语义纪律面）
 *   - 用例与接口路径（WP-20-T03 B-1 教训）：身份生成/解析、聚合工厂、
 *     搜索空投影全部经公共接口消费（Run.hpp 公共面逐方法钉扎）。
 *
 * 测试形态：模型测试＝直调计算库（NFR-MNT-01）——聚合工厂为纯函数，
 * 无需注入缝（缝面测试归 ApplierTest）。
 */

#include <sdurws/ird/optimization/Run.hpp>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/optimization/CandidatePatch.hpp>
#include <sdurws/ird/optimization/DiagCodes.hpp>
#include <sdurws/ird/optimization/Types.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

using namespace sdurws::ird;
using optimization::ArchivePhase;
using optimization::OptimizationRunId;
using optimization::OptimizationConfiguration;
using optimization::RunPhase;
using optimization::TwoStageRunRecord;
using optimization::TwoStageRunResult;

namespace {

// =====================================================================
// 构造辅助（模型测试自持——固定字面量身份，确定性）
// =====================================================================

/// 合法的 64 位十六进制字面量（cv-/cid- 前缀类型的填充体——固定值保证
/// 用例可复现；全零保留值不可作身份，尾位非零）。
constexpr const char* kCvHex =
    "00000000000000000000000000000000000000000000000000000000000000b7";
constexpr const char* kCidHex =
    "0000000000000000000000000000000000000000000000000000000000000c1d";

/// 已校验的最小配置（T06 makeConfig 的精简版——本文件只透传不消费其
/// 内部语义；validateConfiguration 的独立覆盖归 EvaluatorPortsTest）。
OptimizationConfiguration makeConfig()
{
    OptimizationConfiguration c;
    c.seed = 7;  // ≥1（I-OPT-2）
    return c;
}

/// 一个最小的 Verified-Feasible 记录（空补丁＝基线候选——合法补丁形态，
/// §5.5 ③；screeningOnly=false/formalPassEligible=true 为采用可用形态）。
TwoStageRunRecord makeFeasibleRecord(bool baseline, const char* cvHexTail)
{
    TwoStageRunRecord r;
    // 候选身份：经 candidateIdOf 内容寻址计算（基线锚固定字面量——身份
    // 只需有效，不需跨用例固定值）。
    const core::ObjectId root = core::ObjectId::fromCanonical(
        "obj-00000000000000000000000000000001");
    const core::ContentVersion cv = core::ContentVersion::fromCanonical(
        std::string("cv-") + cvHexTail);
    r.candidateId = optimization::candidateIdOf(
        root, cv, optimization::CandidatePatch{});
    r.patch = optimization::CandidatePatch{};
    r.isBaseline = baseline;
    r.mode = core::EvaluationMode::Verified;
    r.screeningOnly = false;
    r.status = optimization::CandidateStatus::Feasible;
    r.formalPassEligible = true;
    return r;
}

/// Completed 终态的编排产出（Quick 2 条＋Verified 1 条——候选合并序的
/// 观测素材）。
TwoStageRunResult makeCompletedResult()
{
    TwoStageRunResult r;
    r.runPhase = RunPhase::Completed;
    r.runCompleted = true;
    // Quick 批两条（screening-only——不支撑采用，但属聚合的候选成员）。
    TwoStageRunRecord q1 = makeFeasibleRecord(true, kCvHex);
    q1.mode = core::EvaluationMode::Quick;
    q1.screeningOnly = true;
    q1.formalPassEligible = false;  // Quick 记录恒无正式资格（P-EV-8）
    TwoStageRunRecord q2 = makeFeasibleRecord(false, kCvHex);
    q2.mode = core::EvaluationMode::Quick;
    q2.screeningOnly = true;
    q2.formalPassEligible = false;
    r.quickRecords = {q1, q2};
    r.verifiedRecords = {makeFeasibleRecord(true, kCvHex)};
    return r;
}

/// 合法输入身份四元组＋基线锚（固定字面量——确定性）。
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

}  // namespace

// =====================================================================
// 运行身份（卡 §4.2——本域生成/解析/保留值）
// =====================================================================

TEST(OptRun, RunIdGenerateValidCanonicalRoundtrip_WP20T07)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-08"}, std::vector<std::string>{"AT-12"});

    // 生成非零（保留值纪律）＋格式钉扎："opt-run-<32 小写 hex>"（卡 §4.2
    // 身份表原文——token 前缀冻结，改名即日志/导出消费面漂移）。
    const OptimizationRunId id = OptimizationRunId::generate();
    EXPECT_TRUE(id.isValid());
    const std::string text = id.toCanonical();
    ASSERT_EQ(text.size(), std::string("opt-run-").size() + 32U);
    EXPECT_EQ(text.substr(0, 8), "opt-run-");
    for (std::size_t i = 8; i < text.size(); ++i) {
        const char c = text[i];
        ASSERT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))
            << "十六进制必须小写（NFR-COR-02 确定性书写）";
    }

    // parse(format(x))==x 往返（两轨一致：抛出轨与 try 轨）。
    EXPECT_EQ(OptimizationRunId::fromCanonical(text), id);
    ASSERT_TRUE(OptimizationRunId::tryFromCanonical(text).has_value());
    EXPECT_EQ(*OptimizationRunId::tryFromCanonical(text), id);

    // 唯一性抽查：两次生成不同（128 位随机空间——碰撞即身份面污染）。
    EXPECT_NE(OptimizationRunId::generate(), id);
}

TEST(OptRun, RunIdStrictParseAndReservedValue_WP20T07)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-08"}, std::vector<std::string>{});

    // 非法形态一概拒绝（try 轨 nullopt；抛出轨 kOptInputInvalid）——
    // 前缀不符/大写/长度/字符集/空白（core tryParseId128 同款严格性）。
    EXPECT_FALSE(OptimizationRunId::tryFromCanonical("run-00000000000000000000000000000001").has_value())
        << "core run- 前缀不接待优化运行身份（两层运行身份不混用——§4.2）";
    EXPECT_FALSE(OptimizationRunId::tryFromCanonical(
                     "opt-run-0000000000000000000000000000000A")
                     .has_value())
        << "大写 hex 拒绝";
    EXPECT_FALSE(OptimizationRunId::tryFromCanonical(
                     "opt-run-0000000000000000000000000000000")
                     .has_value())
        << "31 字符长度拒绝";
    EXPECT_FALSE(OptimizationRunId::tryFromCanonical(
                     "opt-run-0000000000000000000000000000000g")
                     .has_value())
        << "非 hex 字符拒绝";

    bool thrown = false;
    try {
        OptimizationRunId::fromCanonical("opt-run-zzzz");
    } catch (const optimization::OptimizationError& e) {
        thrown = true;
        EXPECT_EQ(e.stableCode(), std::string(optimization::kOptInputInvalid));
    }
    EXPECT_TRUE(thrown) << "抛出轨必须携带域稳定码（禁裸异常——AGENTS §2.5）";

    // 保留值：全零＝空（isValid 恒 false）——聚合工厂据此拦截。
    OptimizationRunId zero;
    EXPECT_FALSE(zero.isValid());
}

// =====================================================================
// 归档阶段词表（§12.2 Export @pre 的承载面）
// =====================================================================

TEST(OptRun, ArchivePhaseTokens_WP20T07)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-12"}, std::vector<std::string>{"AT-34"});

    // 两值 token 钉扎（T09 导出面按此消费 @pre——文本冻结）。
    EXPECT_EQ(optimization::toToken(ArchivePhase::Pending), "pending");
    EXPECT_EQ(optimization::toToken(ArchivePhase::Archived), "archived");
}

// =====================================================================
// 聚合工厂（acceptance 1 的模型面——候选归属容器语义）
// =====================================================================

TEST(OptRun, AssembleCompletedOwnsCandidatesAndFormalEligible_WP20T07_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-08"}, std::vector<std::string>{"AT-12"});

    // Completed 编排产出 → 聚合：资格位推导＋候选归属＋身份面投影。
    const IdentityInputs in;
    const OptimizationRunId runId = OptimizationRunId::generate();
    const TwoStageRunResult orchestrated = makeCompletedResult();

    const optimization::OptimizationRunResult result = optimization::assembleRunResult(
        runId, in.project, in.branch, in.revision, in.snapshot,
        in.baselineRoot, in.baselineCv, makeConfig(), orchestrated);

    // 身份面/基线锚逐字段落位（Applier 归属核对的立即可用输入——§10.3
    // 第 3 项的聚合侧前提）。
    EXPECT_EQ(result.runId, runId);
    EXPECT_EQ(result.project, in.project);
    EXPECT_EQ(result.branch, in.branch);
    EXPECT_EQ(result.revision, in.revision);
    EXPECT_EQ(result.snapshotId, in.snapshot);
    EXPECT_EQ(result.baselineRoot, in.baselineRoot);
    EXPECT_EQ(result.baselineCv, in.baselineCv);

    // 资格位：Completed ⇒ 正式导出资格（TASK-02 语义的正面）。
    EXPECT_TRUE(result.allowFormalExport);
    EXPECT_EQ(result.runPhase, RunPhase::Completed);
    EXPECT_EQ(result.archivePhase, ArchivePhase::Pending)
        << "组装时点未归档（归档登记随 T09/execution 面——I-OPT-6）";

    // 候选归属（OPT-08 数据面）：Quick 2 条在前＋Verified 1 条在后——
    // 保持编排序合并，全部为运行结果的成员值。
    ASSERT_EQ(result.candidates.size(), 3U);
    EXPECT_EQ(result.candidates[0].mode, core::EvaluationMode::Quick);
    EXPECT_TRUE(result.candidates[0].screeningOnly);
    EXPECT_EQ(result.candidates[2].mode, core::EvaluationMode::Verified);
    EXPECT_FALSE(result.candidates[2].screeningOnly);

    // 纯值拷贝独立（"不产生项目修订"的类型面承载：聚合零项目写面，
    // 拷贝即独立快照——历史不可变的值语义纪律，CON-02）。
    optimization::OptimizationRunResult copy = result;
    copy.runPhase = RunPhase::Canceled;
    copy.allowFormalExport = false;
    copy.candidates.clear();
    EXPECT_EQ(result.runPhase, RunPhase::Completed);
    EXPECT_TRUE(result.allowFormalExport);
    EXPECT_EQ(result.candidates.size(), 3U);
}

TEST(OptRun, AssembleCanceledNeverFormalEligible_WP20T07)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-08", "TASK-02"}, std::vector<std::string>{"AT-34"});

    // 取消结果：已回传批保留（部分候选在案）但**不得**具正式导出资格
    // （TASK-02/§10.6"已中断不伪装完整结果"同源纪律——NFR-REL-03）。
    const IdentityInputs in;
    TwoStageRunResult canceled;
    canceled.runPhase = RunPhase::Canceled;
    canceled.runCompleted = false;
    canceled.quickRecords = {makeFeasibleRecord(true, kCvHex)};

    const optimization::OptimizationRunResult result = optimization::assembleRunResult(
        OptimizationRunId::generate(), in.project, in.branch, in.revision,
        in.snapshot, in.baselineRoot, in.baselineCv, makeConfig(), canceled);

    EXPECT_EQ(result.runPhase, RunPhase::Canceled);
    EXPECT_FALSE(result.allowFormalExport)
        << "取消结果不得冒充完整正式结果（TASK-02）";
    EXPECT_EQ(result.candidates.size(), 1U) << "已回传批保留（协作取消语义）";
}

TEST(OptRun, AssembleRejectsNonTerminalPhase_WP20T07)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-08"}, std::vector<std::string>{});

    // 编排终态越集（QuickScreening 是中间态）＝调用方把非编排产物喂给
    // 聚合工厂——fail-fast，消息携带实际值 token（ERR-01 比较型定位）。
    const IdentityInputs in;
    TwoStageRunResult mid;
    mid.runPhase = RunPhase::QuickScreening;

    bool thrown = false;
    try {
        optimization::assembleRunResult(
            OptimizationRunId::generate(), in.project, in.branch, in.revision,
            in.snapshot, in.baselineRoot, in.baselineCv, makeConfig(), mid);
    } catch (const optimization::OptimizationError& e) {
        thrown = true;
        EXPECT_EQ(e.stableCode(), std::string(optimization::kOptInputInvalid));
        EXPECT_NE(std::string(e.what()).find("quick-screening"),
                  std::string::npos)
            << "消息必须携带实际终态 token（比较型定位）";
    }
    EXPECT_TRUE(thrown);
}

TEST(OptRun, AssembleRejectsReservedIdentities_WP20T07)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-08"}, std::vector<std::string>{});

    // 身份面保留值逐一拦截（runId/branch/baselineCv——core §4.1 U-1 的
    // 聚合侧强制；保留值进身份面＝证据链污染）。
    const IdentityInputs in;
    const TwoStageRunResult done = makeCompletedResult();

    bool thrown = false;
    try {
        optimization::assembleRunResult(
            OptimizationRunId{},  // 全零运行身份
            in.project, in.branch, in.revision, in.snapshot, in.baselineRoot,
            in.baselineCv, makeConfig(), done);
    } catch (const optimization::OptimizationError&) {
        thrown = true;
    }
    EXPECT_TRUE(thrown) << "全零 runId 拒绝";

    thrown = false;
    try {
        optimization::assembleRunResult(
            OptimizationRunId::generate(), in.project,
            core::BranchId{},  // 全零分支
            in.revision, in.snapshot, in.baselineRoot, in.baselineCv,
            makeConfig(), done);
    } catch (const optimization::OptimizationError&) {
        thrown = true;
    }
    EXPECT_TRUE(thrown) << "全零 branch 拒绝";

    thrown = false;
    try {
        optimization::assembleRunResult(
            OptimizationRunId::generate(), in.project, in.branch, in.revision,
            in.snapshot, in.baselineRoot,
            core::ContentVersion{},  // 全零基线内容版本
            makeConfig(), done);
    } catch (const optimization::OptimizationError&) {
        thrown = true;
    }
    EXPECT_TRUE(thrown) << "全零 baselineCv 拒绝（候选身份公式输入非法）";
}

// =====================================================================
// 搜索空投影（OPT-VER-120 观测点的聚合侧便捷面）
// =====================================================================

TEST(OptRun, IsSearchEmptyProjection_WP20T07)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-04"}, std::vector<std::string>{});

    // Completed＋可行集空 ⇒ 搜索空（OPT-SEARCH-EMPTY warning 语义——
    // 非任务不可行；呈现面据此出提示不出"不可行"）。
    optimization::OptimizationRunResult emptyRun;
    emptyRun.runPhase = RunPhase::Completed;
    emptyRun.pareto.feasibleIds = {};
    EXPECT_TRUE(optimization::isSearchEmpty(emptyRun));

    // 有可行集 ⇒ 非搜索空。
    emptyRun.pareto.feasibleIds = {makeFeasibleRecord(true, kCvHex).candidateId};
    EXPECT_FALSE(optimization::isSearchEmpty(emptyRun));

    // 取消 ⇒ 恒非搜索空（取消不是完成态——语义不同轴）。
    emptyRun.pareto.feasibleIds = {};
    emptyRun.runPhase = RunPhase::Canceled;
    EXPECT_FALSE(optimization::isSearchEmpty(emptyRun));
}
