/**
 * @file   ContractCheckTest.cpp
 * @brief  契约通用断言用例组——TK-CTR（units/testkit.md §8）：诊断记录校验/
 *         比较型三要素量纲/任务身份/身份唯一性/文件完整性/包络组合泛型谓词
 *         （TK-T07）。
 *
 * 设计依据：
 *   - units/testkit.md §5.5（签名逐项）/§4.8（core 语义）/§8 TK-CTR 行
 *   - 需求 ERR-01/UX-03/TASK-03；任务契约 tasks/foundation/TK-T07.json
 *     acceptance（正反例通过＋evidence 谓词模板以 core 值类型演练）
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/testkit/ContractCheck.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace {
using namespace sdurws::ird::core;
namespace core = sdurws::ird::core;   // 别名：core:: 限定名（using namespace 不引入名字本身）
using namespace sdurws::ird::testkit;

/// 合法比较型记录样例。
core::DiagnosticRecord validRecord(bool withComparison = true)
{
    core::ComparativeFields f;
    if (withComparison) {
        f.actual.quantity = core::SourcedValue<double>::provided(
            12.5, core::ValueProvenance::make(core::ProvenanceKind::GeometricEstimate));
        f.actual.unit = *core::UnitToken::find("N*m");
        f.expected.quantity = core::SourcedValue<double>::provided(
            10.0, core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
        f.expected.unit = *core::UnitToken::find("N*m");
    }
    return core::DiagnosticRecord::make(
        "RT-TORQUE-LIMIT", core::ObjectId::generate(), std::string("base"),
        std::string("robot/base"), "扭矩校验", "第 3 关节超限", "减载",
        withComparison ? std::optional<core::ComparativeFields>(f)
                       : std::optional<core::ComparativeFields>{});
}

/** 诊断记录：合法通过（比较型＋稳定 subject）。 */
TEST(ContractDiagRecord, ValidComparativeRecordPasses_UT_CTR)
{
    const auto r = validRecord(true);
    const auto result = checkDiagnosticRecord(r, DiagnosticCheckOptions{});
    EXPECT_TRUE(result.passed);
}

/** 诊断记录反例：码句法/subject 缺失/必填串空——各含违规描述。 */
TEST(ContractDiagRecord, RejectionsWithViolations_UT_CTR)
{
    auto badCode = validRecord(true);
    badCode.code = "rt-bad";
    const auto r1 = checkDiagnosticRecord(badCode, DiagnosticCheckOptions{});
    EXPECT_FALSE(r1.passed);

    auto noSubject = validRecord(false);
    noSubject.subject = std::nullopt;
    const auto r2 = checkDiagnosticRecord(noSubject, DiagnosticCheckOptions{});
    EXPECT_FALSE(r2.passed);   // allowTransient=false 时 subject 必填

    // allowTransient=true 时 subject 缺失合法（瞬时开发诊断）。
    const auto r3 = checkDiagnosticRecord(noSubject,
                                          DiagnosticCheckOptions{true, });
    EXPECT_TRUE(r3.passed);
}

/** 比较型三要素量纲：unit.kind 与 expectedKind 一致（§5.5——不一致即失败）。 */
TEST(ContractComparative, KindMismatchFails_UT_CTR)
{
    const auto f = [] {
        core::ComparativeFields x;
        x.actual.quantity = core::SourcedValue<double>::provided(
            1.0, core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
        x.actual.unit = *core::UnitToken::find("N*m");
        x.expected.quantity = core::SourcedValue<double>::provided(
            2.0, core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
        x.expected.unit = *core::UnitToken::find("N*m");
        return x;
    }();
    EXPECT_TRUE(checkComparativeFields(f, core::QuantityKind::Torque).passed);
    EXPECT_FALSE(checkComparativeFields(f, core::QuantityKind::Force).passed);
}

/** 任务身份：五元组全 isValid 通过；缺任一失败（TASK-03）。 */
TEST(ContractTaskIdentity, FiveFieldValidation_UT_CTR)
{
    core::TaskIdentity t;
    t.project = core::ProjectId::generate();
    t.branch = core::BranchId::generate();
    t.revision = core::RevisionId::generate();
    t.run = core::RunId::generate();
    t.attempt = core::AttemptId::fromCanonical("att-1");
    EXPECT_TRUE(checkTaskIdentity(t).passed);
    auto broken = t;
    broken.run = core::RunId{};
    EXPECT_FALSE(checkTaskIdentity(broken).passed);
}

/** 身份唯一性：重复项列入 failures（fieldPath＝规范文本）。 */
TEST(ContractIds, StableIdsUnique_UT_CTR)
{
    const std::vector<core::ObjectId> ids{core::ObjectId::generate(),
                                          core::ObjectId::generate(),
                                          core::ObjectId::generate()};
    EXPECT_TRUE(checkStableIdsUnique(ids).passed);
    const std::vector<core::ObjectId> dup{ids[0], ids[1], ids[0]};
    const auto r = checkStableIdsUnique(dup);
    EXPECT_FALSE(r.passed);
    ASSERT_GE(r.failures.size(), 1u);
    EXPECT_NE(r.failures[0].fieldPath.find("duplicate ObjectId"), std::string::npos);
}

/** 文件完整性：SHA 对照通过/篡改拒绝（core::ContentDigester 复用）。 */
TEST(ContractFileIntegrity, ShaCompare_UT_CTR)
{
    const auto path = fs::temp_directory_path() / "ird-tk-ctr-file.txt";
    const std::string content = "contract-file-integrity";
    { std::ofstream out(path, std::ios::binary); out << content; }

    // 期望哈希实算（同源）。
    core::ContentDigester d;
    d.update(content.data(), content.size());
    const auto digest = d.finalize();
    static constexpr char kHex[] = "0123456789abcdef";
    std::string hex;
    for (const auto b : digest) {
        hex.push_back(kHex[b >> 4]);
        hex.push_back(kHex[b & 0x0F]);
    }
    EXPECT_TRUE(checkFileIntegrity(path, hex).passed);

    std::string badHex = hex;
    badHex[0] = badHex[0] == '0' ? '1' : '0';
    EXPECT_FALSE(checkFileIntegrity(path, badHex).passed);
    EXPECT_FALSE(checkFileIntegrity(path / "no-such", hex).passed);
    std::error_code ec;
    fs::remove(path, ec);
}

/** 包络组合泛型谓词（§8.1 表 3）：evidence 值类型演练——模板以访问器消费。 */
TEST(ContractEnvelope, CombinationRules_UT_CTR)
{
    // 演练包络（evidence 侧真实类型的形状代理——表 3 字段子集）。
    struct Envelope {
        core::TaskOutcome outcome;
        core::EngineeringStatus status;
        bool hasEvidenceList = true;
        bool hasMissingItemsList = false;
        bool hasFormalConclusion = true;
    };
    struct Accessors {
        static core::TaskOutcome outcome(const Envelope& e) { return e.outcome; }
        static core::EngineeringStatus engineeringStatus(const Envelope& e) { return e.status; }
        static bool hasEvidenceList(const Envelope& e) { return e.hasEvidenceList; }
        static bool hasMissingItemsList(const Envelope& e) { return e.hasMissingItemsList; }
        static bool hasFormalConclusion(const Envelope& e) { return e.hasFormalConclusion; }
    };

    // 合法形态：Failed ⇒ NotApplicable 且无正式结论。
    Envelope ok{core::TaskOutcome::Failed, core::EngineeringStatus::NotApplicable,
                true, true, false};
    EXPECT_TRUE(checkEnvelopeCombination(ok, Accessors{}).passed);

    // 违例①：Failed 但 engineeringStatus=Feasible（伪造判定——ERR-01）。
    Envelope badStatus = ok;
    badStatus.status = core::EngineeringStatus::Feasible;
    EXPECT_FALSE(checkEnvelopeCombination(badStatus, Accessors{}).passed);

    // 违例②：Completed 但缺证据清单（表 3）。
    Envelope badCompleted{core::TaskOutcome::Completed,
                          core::EngineeringStatus::Feasible, false, true, true};
    EXPECT_FALSE(checkEnvelopeCombination(badCompleted, Accessors{}).passed);

    // 违例③：DataInsufficient 但缺失项清单缺位。
    Envelope badMissing{core::TaskOutcome::Completed,
                        core::EngineeringStatus::DataInsufficient, true, false, true};
    EXPECT_FALSE(checkEnvelopeCombination(badMissing, Accessors{}).passed);
}
}  // namespace
