/**
 * @file   DiagDataTest.cpp
 * @brief  诊断契约用例组——UT-DIAG（units/core.md §8）：C-1～C-3 不变量/
 *         码句法/凭据一致性/比较型三要素（CORE-T07）。
 *
 * 设计依据：
 *   - units/core.md §4.8（字段表与 C-1~C-3 不变量）、§5.7、§8 UT-DIAG 行
 *   - 需求 ERR-01/UX-03/SA-15/MDL-06；任务契约 tasks/foundation/CORE-T07.json
 *     acceptance 两条
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/DiagData.hpp>

#include <string>

namespace {
using namespace sdurws::ird::core;

/// 比较型三要素样例（实际 12.5 N·m vs 期望 10.0 N·m）。
ComparativeFields sampleComparison()
{
    ComparativeValue actual;
    actual.quantity = SourcedValue<double>::provided(
        12.5, ValueProvenance::make(ProvenanceKind::GeometricEstimate));
    actual.unit = *UnitToken::find("N*m");
    ComparativeValue expected;
    expected.quantity = SourcedValue<double>::provided(10.0, ValueProvenance::make(ProvenanceKind::UserProvided));
    expected.unit = *UnitToken::find("N*m");
    ComparativeFields f;
    f.actual = actual;
    f.expected = expected;
    return f;
}

/// 合法记录样例（比较型——供 ConfirmableFinding 用）。
DiagnosticRecord sampleRecord()
{
    return DiagnosticRecord::make(
        "RT-TORQUE-LIMIT", ObjectId::generate(), std::string("base_link"),
        std::string("robot/base_link"), "扭矩超限校验", "第 3 关节力矩超限",
        "减小末端负载或放慢轨迹",
        sampleComparison());
}

/** C-3①：码句法——合法形态（单段/多段/数字）全过。 */
TEST(DiagCodeSyntax, ValidForms_UT_DIAG)
{
    EXPECT_NO_THROW((void)DiagnosticRecord::make(
        "RT-INPUT-INVALID", std::nullopt, std::nullopt, std::nullopt,
        "ctx", "cause", "action"));
    EXPECT_NO_THROW((void)DiagnosticRecord::make(
        "E1", std::nullopt, std::nullopt, std::nullopt, "ctx", "cause", "action"));   // 单字符段
    EXPECT_NO_THROW((void)DiagnosticRecord::make(
        "RT-WC-COMPILE-FAILED-2", std::nullopt, std::nullopt, std::nullopt,
        "ctx", "cause", "action"));                                                    // 数字段
}

/** C-3①：码句法反例——小写/下划线/空/超 64/首尾连字符/连续连字符全部拒绝。 */
TEST(DiagCodeSyntax, InvalidForms_UT_DIAG)
{
    EXPECT_THROW((void)DiagnosticRecord::make(
        "rt-invalid", std::nullopt, std::nullopt, std::nullopt, "c", "c", "a"), CoreError);      // 小写
    EXPECT_THROW((void)DiagnosticRecord::make(
        "RT_INVALID", std::nullopt, std::nullopt, std::nullopt, "c", "c", "a"), CoreError);      // 下划线
    EXPECT_THROW((void)DiagnosticRecord::make(
        "", std::nullopt, std::nullopt, std::nullopt, "c", "c", "a"), CoreError);                // 空
    EXPECT_THROW((void)DiagnosticRecord::make(
        std::string(65, 'A'), std::nullopt, std::nullopt, std::nullopt, "c", "c", "a"), CoreError); // >64
    EXPECT_THROW((void)DiagnosticRecord::make(
        "-RT-X", std::nullopt, std::nullopt, std::nullopt, "c", "c", "a"), CoreError);           // 首连字符
    EXPECT_THROW((void)DiagnosticRecord::make(
        "RT-X-", std::nullopt, std::nullopt, std::nullopt, "c", "c", "a"), CoreError);           // 尾连字符
    EXPECT_THROW((void)DiagnosticRecord::make(
        "RT--X", std::nullopt, std::nullopt, std::nullopt, "c", "c", "a"), CoreError);           // 连续连字符
    // 前缀 core/diag/code。
    try {
        (void)DiagnosticRecord::make("bad", std::nullopt, std::nullopt, std::nullopt,
                                     "c", "c", "a");
        FAIL() << "必须抛出";
    } catch (const CoreError& e) {
        EXPECT_EQ(std::string(e.what()).find("core/diag/code: "), 0u);
    }
}

/** C-3②：必填串非空——context/cause/recommendedAction 任一空串拒绝。 */
TEST(DiagRecordRequired, RequiredStringsNonEmpty_UT_DIAG)
{
    EXPECT_THROW((void)DiagnosticRecord::make(
        "RT-X", std::nullopt, std::nullopt, std::nullopt, "", "c", "a"), CoreError);
    EXPECT_THROW((void)DiagnosticRecord::make(
        "RT-X", std::nullopt, std::nullopt, std::nullopt, "c", "", "a"), CoreError);
    EXPECT_THROW((void)DiagnosticRecord::make(
        "RT-X", std::nullopt, std::nullopt, std::nullopt, "c", "c", ""), CoreError);
    // 名称字段允许缺失（显式 nullopt——不伪造，§4.8 localName/runtimeName 行）。
    EXPECT_NO_THROW((void)DiagnosticRecord::make(
        "RT-X", std::nullopt, std::nullopt, std::nullopt, "c", "c", "a"));
}

/** C-1：可确认诊断必为比较型——无 comparison 的记录 make 抛（前缀 core/diag/c1）。 */
TEST(ConfirmableFindingC1, ComparisonRequired_UT_DIAG)
{
    const auto plain = DiagnosticRecord::make(
        "RT-X", std::nullopt, std::nullopt, std::nullopt, "c", "c", "a");   // 非比较型
    EXPECT_THROW(ConfirmableFinding::make(plain), CoreError);
    try {
        (void)ConfirmableFinding::make(plain);
        FAIL() << "必须抛出";
    } catch (const CoreError& e) {
        EXPECT_EQ(std::string(e.what()).find("core/diag/c1: "), 0u);
    }
    // 比较型可 make 且初始 Pending。
    const auto finding = ConfirmableFinding::make(sampleRecord());
    EXPECT_EQ(finding.state, ConfirmationState::Pending);
    EXPECT_FALSE(finding.credential.has_value());
}

/** C-2：Confirmed ⇔ 凭据在场；Pending/Rejected 不得携带凭据。 */
TEST(ConfirmableFindingC2, CredentialConsistency_UT_DIAG)
{
    auto finding = ConfirmableFinding::make(sampleRecord());
    // Pending 态不携带凭据。
    EXPECT_FALSE(finding.credential.has_value());

    // 确认：state→Confirmed 且凭据在场（C-2 正向）。
    ConfirmationCredential cred;
    cred.principal = "operator-a";
    cred.confirmedAtUtc = std::chrono::system_clock::now();
    finding.confirm(cred);
    EXPECT_EQ(finding.state, ConfirmationState::Confirmed);
    ASSERT_TRUE(finding.credential.has_value());
    EXPECT_EQ(finding.credential->principal, "operator-a");

    // 二次确认/否决：非 Pending 态拒绝推进（状态机前置）。
    EXPECT_THROW(finding.confirm(cred), CoreError);
    EXPECT_THROW(finding.reject(), CoreError);

    // 否决路径：Pending→Rejected 且凭据清空（C-2 反向）。
    auto r = ConfirmableFinding::make(sampleRecord());
    r.reject();
    EXPECT_EQ(r.state, ConfirmationState::Rejected);
    EXPECT_FALSE(r.credential.has_value());
}

/** 比较型三要素：SourcedValue 承载不适用/非法侧（UX-03＋MDL-06 复用语义）。 */
TEST(ComparativeFields, SourcedValueSides_UT_DIAG)
{
    ComparativeFields f;
    f.actual.quantity = SourcedValue<double>::provided(
        12.5, ValueProvenance::make(ProvenanceKind::GeometricEstimate));
    f.actual.unit = *UnitToken::find("N*m");
    // 期望侧"不适用"显式标记（ERR-01——不伪造数值）。
    f.expected.quantity = SourcedValue<double>::notApplicable();
    f.expected.unit = *UnitToken::find("N*m");
    EXPECT_TRUE(f.actual.quantity.tryValue().has_value());
    EXPECT_FALSE(f.expected.quantity.tryValue().has_value());   // 不适用无值可取
    // 非法侧保留原串（MDL-06/NFR-COR-03 复用 SourcedValue 语义——不另设第二套）。
    f.expected.quantity = SourcedValue<double>::invalid("1e999");
    EXPECT_EQ(f.expected.quantity.invalidRawInput(), "1e999");
}

/** 记录相等：全字段比较（含 comparison 有无）。 */
TEST(DiagRecordEquality, FullFieldCompare_UT_DIAG)
{
    const auto a = sampleRecord();
    const auto b = sampleRecord();   // ObjectId 随机——subject 不等！
    // sampleRecord 每次生成新 ObjectId：两实例 subject 不同 → 不等（字段级语义）。
    EXPECT_FALSE(a == b);
    // 同 subject 的副本相等。
    auto c = a;
    EXPECT_TRUE(a == c);
}
}  // namespace
