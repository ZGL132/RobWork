/**
 * @file   EnvelopeContractTest.cpp
 * @brief  evidence 跨单元契约测试（sdurws_ird_evidence_contract_test）——
 *         CR-01 联合契约测试：evidence 结果包络的词表三轴仅消费 core
 *         Evaluation 词表（无本地枚举定义），token 字面量与 core::toToken
 *         输出逐一相等。
 *
 * 设计依据：
 *   - traceability/foundation-api-diff.md CR-01（core 词表 ↔ evidence
 *     ResultEnvelope 状态——已关闭）："EV-T07 的 _contract_test 增加断言：
 *     evidence 翻译单元仅经 core/Evaluation.hpp 取得词表（无本地枚举
 *     定义），测试内 token 字面量与 core::toToken 输出逐一相等"
 *   - units/evidence.md §11（测试目标 sdurws_ird_evidence_contract_test
 *     ＝跨单元契约面）、§12 EV-T07 行；任务契约 tasks/foundation/EV-T07.json
 *     acceptance 2
 *   - core.md §4.7（四词表 token 冻结表——小写连字符、持久化契约不改名；
 *     本测试钉住的就是这份冻结表对 evidence 的可见性）
 *
 * 背景说明（为什么词表要有跨单元契约测试）：
 *   core 是词表唯一权威（CR-01 裁决：core 负责 token 集，evidence 只
 *   消费——禁止各自定义枚举）。若 evidence 侧出现本地重定义或字面量
 *   漂移，诊断/报告/缓存键中的 token 将与 core 词表脱钩（跨单元数据
 *   不可解释）。本测试用三层防线钉住：①编译期——包络三轴字段的类型
 *   必须就是 core 词表类型（static_assert，本地重定义直接编译失败面）；
 *   ②源码面——evidence 产品源码（include/＋src/）禁止出现词表枚举的
 *   本地定义；③值面——本测试写死的 token 字面量与 core::toToken 输出
 *   逐一相等＋包络承载的枚举经 toToken 回得同样字面量。
 *
 * 替身边界声明（EV-REG-3 同源纪律）：本文件构造的包络为契约形态数据，
 * 仅验证词表消费契约，不构成任何业务算法正确性证明。
 */

#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/evidence/Envelope.hpp>
#include <sdurws/ird/evidence/Snapshot.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

using namespace sdurws::ird::evidence;
namespace core = sdurws::ird::core;

/// evidence 单元根（IRD_EVIDENCE_UNIT_ROOT 注入——与红线扫描同源）。
const fs::path& unitRoot()
{
    static const fs::path dir = fs::path{IRD_EVIDENCE_UNIT_ROOT};
    return dir;
}

/// 递归收集 C++ 源/头文件（相对路径、已排序——确定性失败信息）。
std::vector<fs::path> collectCppFiles(const fs::path& dir)
{
    std::vector<fs::path> files;
    std::error_code ec;
    if (!fs::exists(dir)) { return files; }
    for (auto it = fs::recursive_directory_iterator(dir, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec || !it->is_regular_file(ec)) continue;
        const auto ext = it->path().extension().string();
        if (ext == ".hpp" || ext == ".h" || ext == ".cpp") {
            files.push_back(fs::relative(it->path(), dir, ec));
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

/// 全文读取；读失败显性失败。
std::string readFile(const fs::path& file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        ADD_FAILURE() << "无法读取: " << file.string();
        return {};
    }
    return std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// =====================================================================
// 编译期防线：包络三轴字段的类型必须就是 core 词表类型（CR-01"无本地
// 枚举定义"的类型系统面——若 evidence 侧以本地枚举/别名替换，这里编译
// 失败）。词表取包络实际消费的三轴；TaskState（状态机轴）不进包络，
// 其映射归 execution（core.md §4.7 轴正交）。
// =====================================================================

static_assert(std::is_same_v<decltype(std::declval<ResultEnvelopeDraft>().mode),
                             core::EvaluationMode>,
              "CR-01: 包络 mode 轴必须就是 core::EvaluationMode（禁止本地枚举/别名）");
static_assert(std::is_same_v<decltype(std::declval<ResultEnvelopeDraft>().outcome),
                             core::TaskOutcome>,
              "CR-01: 包络 outcome 轴必须就是 core::TaskOutcome（禁止本地枚举/别名）");
static_assert(std::is_same_v<decltype(std::declval<ResultEnvelopeDraft>().engineeringStatus),
                             core::EngineeringStatus>,
              "CR-01: 包络 engineeringStatus 轴必须就是 core::EngineeringStatus"
              "（禁止本地枚举/别名）");
static_assert(std::is_same_v<decltype(std::declval<ResultEnvelope>().mode),
                             core::EvaluationMode>,
              "CR-01: 正式包络 mode 轴必须就是 core::EvaluationMode");
static_assert(std::is_same_v<decltype(std::declval<ResultEnvelope>().outcome),
                             core::TaskOutcome>,
              "CR-01: 正式包络 outcome 轴必须就是 core::TaskOutcome");
static_assert(std::is_same_v<decltype(std::declval<ResultEnvelope>().engineeringStatus),
                             core::EngineeringStatus>,
              "CR-01: 正式包络 engineeringStatus 轴必须就是 core::EngineeringStatus");

/// 定长 id 构造（tag＋显式重复的十六进制字符——避免手写字数漂移）。
std::string taggedHex(const char* tag, char fill, std::size_t count)
{
    return std::string{tag} + std::string(count, fill);
}

/// 工况 id（与 buildMinimalSnapshot 的 caseSet 条目同值——界内范围）。
core::ObjectId caseAId()
{
    return core::ObjectId::fromCanonical(taggedHex("obj-", 'a', 32));
}

/// 最小合法冻结快照（包络构造的绑定事实面——与 EnvelopeTest 同款风格，
/// 自持不共享；替身边界声明见文件头）。
AnalysisSnapshot buildMinimalSnapshot()
{
    SnapshotBuilder b;
    b.setIdentity(
        core::ProjectId::fromCanonical(taggedHex("prj-", 'a', 32)),
        core::BranchId::fromCanonical(taggedHex("brn-", 'b', 32)),
        core::RevisionId::fromCanonical(taggedHex("rev-", 'c', 32)),
        1);
    b.setPolicyRef(PolicyRef{core::ContentIdentity::fromCanonical(
        taggedHex("cid-", 'd', 64))});
    b.setNameMapRef(NameMapRef{core::ContentIdentity::fromCanonical(
        taggedHex("cid-", 'e', 64))});
    ReproductionBlock r;
    r.productVersion = "industrialrobot-designer 0.1.0";
    r.evidenceContractVersion = "evidence-contract/1";
    r.codecVersions = {"snapshot-codec/1", "slice-codec/1"};
    b.setReproduction(r);
    ObjectRefEntry obj;
    obj.objectId = caseAId();
    obj.contentVersion = core::ContentVersion::fromCanonical(
        taggedHex("cv-", 'a', 64));
    obj.objectTypeToken = "robot-design";
    obj.digest = obj.contentVersion.bytes;
    b.addObjectRef(obj);
    b.addCase({obj.objectId, "case-a", true, true});

    // 闭包事实来源替身（EV-REG-3）：对一切 (oid,cv) 回答 true——防混入
    // 校验不是本文件被测面。
    struct AcceptAll : IRevisionClosureSource {
        bool objectInRevision(core::RevisionId, core::ObjectId,
                              core::ContentVersion) const override
        {
            return true;
        }
    };
    AcceptAll source;
    return b.build(source);
}

/// 合法 Completed×Feasible 草稿（词表值面的最小承载——证据清单绑定面
/// 结构有效，构造边界应放行）。
ResultEnvelopeDraft legalFeasibleDraft(const AnalysisSnapshot& snapshot)
{
    ResultEnvelopeDraft d;
    core::TaskIdentity t;
    t.project = core::ProjectId::fromCanonical(taggedHex("prj-", 'a', 32));
    t.branch = core::BranchId::fromCanonical(taggedHex("brn-", 'b', 32));
    t.revision = core::RevisionId::fromCanonical(taggedHex("rev-", 'c', 32));
    t.run = core::RunId::fromCanonical(taggedHex("run-", 'd', 32));
    t.attempt = core::AttemptId{1};
    d.task = t;
    d.evaluationKey = "kin-batch-ik";
    d.evaluatorContractVersion = 7;
    d.mode = core::EvaluationMode::Verified;
    d.snapshotId = snapshot.snapshotId;
    d.sliceId = core::ContentIdentity::fromCanonical(taggedHex("cid-", '1', 64));
    d.inputBaselineId =
        core::ContentIdentity::fromCanonical(taggedHex("cid-", '2', 64));
    d.caseScope.caseIds = {caseAId()};
    d.profile.profileId = "kin";
    d.profile.version = "1.0.0";
    d.profile.contentIdentity =
        core::ContentIdentity::fromCanonical(taggedHex("cid-", '3', 64));
    d.outcome = core::TaskOutcome::Completed;
    d.engineeringStatus = core::EngineeringStatus::Feasible;
    EvidenceManifest m;
    m.snapshotId = d.snapshotId;
    m.sliceId = d.sliceId;
    m.profileId = "kin";
    m.profileVersion = "1.0.0";
    m.profileContentIdentity = d.profile.contentIdentity;
    EvidenceItem ok;
    ok.itemId = "kin.reach-per-task-point";
    ok.status = EvidenceItemStatus::Satisfied;
    ok.artifactDigest =
        core::ContentVersion::fromCanonical(taggedHex("cv-", 'a', 64)).bytes;
    m.items = {ok};
    d.evidence = m;
    d.producer.productVersion = "industrialrobot-designer 0.1.0";
    return d;
}

}  // namespace

// =====================================================================
// CR-01 断言 1：token 字面量与 core::toToken 输出逐一相等
// （foundation-api-diff.md CR-01 关闭记录的联合契约测试条款）
// =====================================================================

/** 三词表全部值的字面量逐一钉住（词表＝CR-01 记录原文：preview/quick/
 *  verified、completed/canceled/failed/interrupted、feasible/
 *  engineering-infeasible/data-insufficient/not-applicable）。 */
TEST(EvidenceVocabularyContract, TokenLiteralsEqualCoreToTokenOutputs_CR_01)
{
    // 评估模式（表 1 证据效力分层——三值）。
    EXPECT_STREQ("preview", core::toToken(core::EvaluationMode::Preview));
    EXPECT_STREQ("quick", core::toToken(core::EvaluationMode::Quick));
    EXPECT_STREQ("verified", core::toToken(core::EvaluationMode::Verified));

    // 任务结果（表 3 outcome 轴——四值）。
    EXPECT_STREQ("completed", core::toToken(core::TaskOutcome::Completed));
    EXPECT_STREQ("canceled", core::toToken(core::TaskOutcome::Canceled));
    EXPECT_STREQ("failed", core::toToken(core::TaskOutcome::Failed));
    EXPECT_STREQ("interrupted", core::toToken(core::TaskOutcome::Interrupted));

    // 工程判定（表 3 判定轴＋ERR-01——四值）。
    EXPECT_STREQ("feasible", core::toToken(core::EngineeringStatus::Feasible));
    EXPECT_STREQ("engineering-infeasible",
                 core::toToken(core::EngineeringStatus::EngineeringInfeasible));
    EXPECT_STREQ("data-insufficient",
                 core::toToken(core::EngineeringStatus::DataInsufficient));
    EXPECT_STREQ("not-applicable",
                 core::toToken(core::EngineeringStatus::NotApplicable));
}

/** token 往返（FromToken 轨）：字面量经 core 解析回同一枚举值——写入
 *  端（toToken）与读取端（*FromToken）共用同一词表（core.md §4.7）。 */
TEST(EvidenceVocabularyContract, TokenRoundTripThroughCoreParsers_CR_01)
{
    EXPECT_EQ(core::evaluationModeFromToken("preview"), core::EvaluationMode::Preview);
    EXPECT_EQ(core::evaluationModeFromToken("quick"), core::EvaluationMode::Quick);
    EXPECT_EQ(core::evaluationModeFromToken("verified"), core::EvaluationMode::Verified);

    EXPECT_EQ(core::taskOutcomeFromToken("completed"), core::TaskOutcome::Completed);
    EXPECT_EQ(core::taskOutcomeFromToken("canceled"), core::TaskOutcome::Canceled);
    EXPECT_EQ(core::taskOutcomeFromToken("failed"), core::TaskOutcome::Failed);
    EXPECT_EQ(core::taskOutcomeFromToken("interrupted"), core::TaskOutcome::Interrupted);

    EXPECT_EQ(core::engineeringStatusFromToken("feasible"),
              core::EngineeringStatus::Feasible);
    EXPECT_EQ(core::engineeringStatusFromToken("engineering-infeasible"),
              core::EngineeringStatus::EngineeringInfeasible);
    EXPECT_EQ(core::engineeringStatusFromToken("data-insufficient"),
              core::EngineeringStatus::DataInsufficient);
    EXPECT_EQ(core::engineeringStatusFromToken("not-applicable"),
              core::EngineeringStatus::NotApplicable);
}

/** 包络承载的枚举经 core::toToken 回出同一字面量（词表经包络流通不
 *  漂移——Completed/Feasible 与 Canceled/NotApplicable 两种合法形态）。 */
TEST(EvidenceVocabularyContract, EnvelopeTokensFlowThroughCoreVocabulary_CR_01)
{
    const AnalysisSnapshot snapshot = buildMinimalSnapshot();
    const ResultEnvelope feasible =
        ResultEnvelope::make(legalFeasibleDraft(snapshot));
    EXPECT_STREQ("verified", core::toToken(feasible.mode));
    EXPECT_STREQ("completed", core::toToken(feasible.outcome));
    EXPECT_STREQ("feasible", core::toToken(feasible.engineeringStatus));

    ResultEnvelopeDraft canceledDraft = legalFeasibleDraft(snapshot);
    canceledDraft.outcome = core::TaskOutcome::Canceled;
    canceledDraft.engineeringStatus = core::EngineeringStatus::NotApplicable;
    canceledDraft.evidence.items.clear();  // 行 3：无 Satisfied 判定声明
    canceledDraft.payload.reset();         // 行 3：payload 必须为空
    const ResultEnvelope canceled = ResultEnvelope::make(std::move(canceledDraft));
    EXPECT_STREQ("canceled", core::toToken(canceled.outcome));
    EXPECT_STREQ("not-applicable", core::toToken(canceled.engineeringStatus));
}

// =====================================================================
// CR-01 断言 2：evidence 翻译单元仅经 core/Evaluation.hpp 取得词表
// （无本地枚举定义——源码面扫描）
// =====================================================================

/** 产品源码面（include/＋src/）零词表本地定义：任何同时含词表枚举名与
 *  "enum class" 定义语法的行即失败（本地重定义在 code review 前被测试
 *  拦截——CR-01"禁止各自定义枚举"的机检面）。 */
TEST(EvidenceVocabularyContract, NoLocalVocabularyEnumDefinitionInProductSources_CR_01)
{
    const char* vocabulary[] = {"EvaluationMode", "TaskOutcome",
                                "EngineeringStatus", "TaskState"};
    for (const auto* sub : {"include", "src"}) {
        for (const auto& rel : collectCppFiles(unitRoot() / "evidence" / sub)) {
            const auto text = readFile(unitRoot() / "evidence" / sub / rel);
            std::istringstream lines(text);
            std::string line;
            int lineno = 0;
            while (std::getline(lines, line)) {
                ++lineno;
                if (line.find("enum class") == std::string::npos) { continue; }
                for (const auto* name : vocabulary) {
                    if (line.find(name) != std::string::npos) {
                        ADD_FAILURE()
                            << "CR-01：evidence 产品源码禁止本地定义 core 词表"
                            << "（仅经 core/Evaluation.hpp 消费）: " << rel.string()
                            << ":" << lineno;
                    }
                }
            }
        }
    }
    SUCCEED() << "词表本地定义扫描零命中";
}

/** 包络契约头确经 core/Evaluation.hpp 取得词表（"仅经该头消费"的正面
 *  断言——include 面存在性）。 */
TEST(EvidenceVocabularyContract, EnvelopeHeaderConsumesCoreEvaluationHeader_CR_01)
{
    const auto header = unitRoot() / "evidence" / "include" / "sdurws" / "ird"
                        / "evidence" / "Envelope.hpp";
    ASSERT_TRUE(fs::exists(header)) << "Envelope.hpp 不存在";
    const std::string text = readFile(header);
    EXPECT_NE(text.find("#include <sdurws/ird/core/Evaluation.hpp>"),
              std::string::npos)
        << "Envelope.hpp 必须 include core/Evaluation.hpp（CR-01 词表唯一来源）";
}
