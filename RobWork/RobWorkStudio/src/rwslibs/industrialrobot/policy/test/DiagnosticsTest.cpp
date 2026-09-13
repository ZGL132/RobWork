/**
 * @file   DiagnosticsTest.cpp
 * @brief  policy 诊断构造用例组（POL-T10）——建议码表全表锚定＋
 *         IPolicyDiagnostics 构造不变量（testkit checkDiagnosticRecord 族）
 *         ＋makeComparative 辅助语义。
 *
 * 设计依据：
 *   - units/policy.md §9.6（建议码清单与 IPolicyDiagnostics 契约——本套件
 *     逐条锚定的权威）、§11（"测试设施：testkit checkDiagnosticRecord/
 *     checkComparativeFields"原文——本套件为其在 POL-T10 的落位）、
 *     §12 POL-T10 行（完成条件"码表与构造不变量用例通过"）
 *   - 需求 ERR-01（稳定诊断绑定对象/三轴正交）、UX-03（比较型三要素）、
 *     NFR-COR-02（确定性）、NFR-MNT-03（单一权威定义）、ARC-04（不发明码）
 *   - 任务契约 tasks/foundation/POL-T10.json（≙WP-07-T10）acceptance 1：
 *     checkDiagnosticRecord 族（testkit）码表与构造不变量用例通过
 *
 * 替身/夹具边界声明（POL-TD-1 同款）：本套件零替身——全部被测对象是纯值
 * 构造与查表（无框架、无 I/O、无并发装置），断言不依赖任何外部数据集。
 *
 * 与 testkit 的链接关系（POL-T11 前置落位说明）：CompatibilityTest 等既有
 * 套件按其卡内口径以"手工等价断言"覆盖诊断契约；本套件是 policy 测试目标
 * **首次真实链接** sdurws_ird_testkit 并直接调用 checkDiagnosticRecord/
 * checkComparativeFields 的套件（acceptance 1 的"（testkit）"字面要求），
 * 测试目标的 testkit 链接随本任务在 CMakeLists 登记——产品库（include/src）
 * 仍零 testkit 依赖（BuildRedLineTest 扫描面不含 test/，链接面仅测试目标）。
 *
 * 线程安全：单线程执行（gtest 默认）——被测面本身并发只读安全。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/policy/Diagnostics.hpp>
#include <sdurws/ird/policy/Errors.hpp>
#include <sdurws/ird/testkit/Check.hpp>
#include <sdurws/ird/testkit/ContractCheck.hpp>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace {

using namespace sdurws::ird;   // NOLINT(google-build-using-namespace)——测试 TU 局部，缩短 core/testkit/policy 三前缀
using namespace sdurws::ird::policy;   // NOLINT——同上

/// 合法 subject（generate 保证非全零——Identity.hpp 契约；每用例独立生成，
/// 用例间无共享可变状态）。
core::ObjectId validSubject() { return core::ObjectId::generate(); }

/// 三段必填文案的固定样例（同码同串——断言逐字承载，NFR-COR-02）。
constexpr auto kContext = "诊断构造测试上下文（POL-T10）";
constexpr auto kCause = "测试原因（样例原文）";
constexpr auto kAction = "测试建议动作（样例原文）";

/// testkit checkDiagnosticRecord 的便捷包装（断言通过并返回结果供再断言）。
testkit::CheckResult expectRecordOk(const core::DiagnosticRecord& r,
                                    testkit::DiagnosticCheckOptions opt)
{
    // 本用例组的通过性判据＝testkit 契约校验器（§11 测试设施原文）——
    // 校验器不信任被测方（独立实现码句法/必填/三要素核对）。
    const testkit::CheckResult result = testkit::checkDiagnosticRecord(r, opt);
    EXPECT_TRUE(result.passed) << "checkDiagnosticRecord 失败: "
                               << (result.failures.empty()
                                       ? std::string{"<无明细>"}
                                       : result.failures.front().fieldPath);
    return result;
}

}  // namespace

// =====================================================================
// 建议码表（acceptance 1"码表"半场——全表锚定与两表面一致性）。
// =====================================================================

/** 锚定：registeredCodes() ＝ policyDiagCodes() 全表逐串（§9.6 registeredCodes
 *  语义"本单元建议码清单"——29 值、稳定序＝枚举序、零缺漏）。 */
TEST(PolicyDiagCodeTable, RegisteredCodesCoversWholeTable_SEC9_6)
{
    const PolicyDiagnostics diag;
    const std::vector<std::string> codes = diag.registeredCodes();

    // 表长钉住：kPolicyDiagCodeCount＝29（§9.6 清单 23＋表尾补登 3＋§5.2 系
    // 补登 3 的全集——新增值只能表尾追加并同步此数，DTB §5.4）。
    ASSERT_EQ(codes.size(), kPolicyDiagCodeCount) << "码表长度漂移（§9.6 全集）";
    const PolicyDiagCodeTable& table = policyDiagCodes();
    ASSERT_EQ(table.size(), kPolicyDiagCodeCount);

    // 逐串＋逐位一致（registeredCodes 为 string 拷贝面、policyDiagCodes 为
    // string_view 零拷贝面——两载体的同源性在此钉住）。
    for (std::size_t i = 0; i < table.size(); ++i) {
        EXPECT_EQ(codes[i], std::string{table[i]})
            << "registeredCodes()[" << i << "] 与码表第 " << i << " 项漂移";
    }
}

/** 锚定：registeredCodes() 两次调用（跨实例）逐字节相同（§9.6 通用契约行
 *  "确定性/NFR-COR-02"——查表产物是纯函数值，无隐藏状态）。 */
TEST(PolicyDiagCodeTable, RegisteredCodesDeterministicAcrossInstances_NFR_COR_02)
{
    const PolicyDiagnostics a;
    const PolicyDiagnostics b;
    const std::vector<std::string> first = a.registeredCodes();
    const std::vector<std::string> second = b.registeredCodes();
    ASSERT_EQ(first, second) << "registeredCodes 非确定性（实例间/调用间漂移）";
}

/** 锚定：码表零重复（registeredCodes 供注册表核对——重复项会使核对语义
 *  含混；ARC-04 不发明码的清单面推论）。 */
TEST(PolicyDiagCodeTable, NoDuplicateCodes_SEC9_6)
{
    const PolicyDiagnostics diag;
    const std::vector<std::string> codes = diag.registeredCodes();
    const std::set<std::string> unique(codes.cbegin(), codes.cend());
    EXPECT_EQ(unique.size(), codes.size()) << "码表存在重复建议码";
}

/** 锚定：码句法全表满足 ^[A-Z0-9]+(-[A-Z0-9]+)*$ 且 ≤64（core DiagData
 *  码句法行——表内码必然通过 core 工厂 C-3 句法半场的前提；本用例使该
 *  前提成为显式证据而非隐含假设）。 */
TEST(PolicyDiagCodeTable, AllCodesMatchCoreSyntax_CORE_C3)
{
    for (const std::string_view code : policyDiagCodes()) {
        ASSERT_FALSE(code.empty()) << "空码";
        ASSERT_LE(code.size(), std::size_t{64}) << "超长码: " << code;
        // 首字符必须为大写字母或数字（句法 ^[A-Z0-9]+…）。
        const auto alnum = [](char c) {
            return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        };
        ASSERT_TRUE(alnum(code.front())) << "首字符非法: " << code;
        bool prevDash = false;
        for (const char c : code) {
            if (alnum(c)) {
                prevDash = false;
                continue;
            }
            ASSERT_EQ(c, '-') << "非 [A-Z0-9-] 字符: " << code;
            ASSERT_FALSE(prevDash) << "连续连字符: " << code;
            prevDash = true;
        }
        ASSERT_FALSE(prevDash) << "尾随连字符: " << code;
    }
}

/** 锚定：异常轨 19 值的建议注册码（registryCode）逐串为码表成员（两表面
 *  一致性——POL-T10 起 registryCode 委托码表，本用例钉住委托后行为与
 *  POL-T02~T07 字面量表逐串一致＝PolicySetTest 全表锚定的成员面复证；
 *  NFR-MNT-03 单一权威的防漂移观测点）。 */
TEST(PolicyDiagCodeTable, RegistryCodeIntersectionConsistent_NFR_MNT_03)
{
    // 19 值全枚举（PolicyErrorCode 全表——顺序＝枚举序）。
    const PolicyErrorCode all[] = {
        PolicyErrorCode::SchemaUnknownField,       PolicyErrorCode::SchemaVersionFuture,
        PolicyErrorCode::SchemaVersionUnknown,     PolicyErrorCode::ThresholdNonFinite,
        PolicyErrorCode::ThresholdNonPositive,     PolicyErrorCode::ThresholdOutOfRange,
        PolicyErrorCode::ThresholdRequiredMissing, PolicyErrorCode::UnitMismatch,
        PolicyErrorCode::RuleDuplicate,            PolicyErrorCode::RuleConflict,
        PolicyErrorCode::RuleCycle,                PolicyErrorCode::ScopeObjectMissing,
        PolicyErrorCode::ApplicabilityInvalid,     PolicyErrorCode::PolicyObjectInvalid,
        PolicyErrorCode::EncodingInvalid,          PolicyErrorCode::PortAssemblyIncomplete,
        PolicyErrorCode::SceneInvalid,             PolicyErrorCode::NameUnresolved,
        PolicyErrorCode::QueryInvalid,
    };
    for (const PolicyErrorCode code : all) {
        const std::string_view registry = registryCode(code);
        EXPECT_FALSE(registry.empty()) << "异常码缺建议码（全函数承诺）";
        EXPECT_TRUE(isRegisteredPolicyDiagCode(registry))
            << "registryCode(" << registry << ") 不在 §9.6 码表——两表面漂移";
    }
}

// =====================================================================
// 诊断构造不变量（acceptance 1"构造不变量用例"半场——testkit
// checkDiagnosticRecord 族）。
// =====================================================================

/** 锚定：稳定诊断（带合法 subject）构造 → testkit checkDiagnosticRecord
 *  通过＋字段逐一承载（ERR-01 三轴：稳定码/对象定位/上下文＋建议动作；
 *  UX-03 非比较型 comparison 恒空）。 */
TEST(PolicyDiagnosticsMake, StableRecordPassesContractCheck_ERR_01)
{
    const PolicyDiagnostics diag;
    const core::ObjectId subject = validSubject();

    // 以表内码构造稳定诊断——make 只承载不加工：输出字段与入参逐一同引用。
    const core::DiagnosticRecord r = diag.make(
        policyDiagCode(PolicyDiagCode::JntTableInvalid), subject, kContext, kCause, kAction);

    expectRecordOk(r, testkit::DiagnosticCheckOptions{});
    EXPECT_EQ(r.code, std::string{policyDiagCode(PolicyDiagCode::JntTableInvalid)});
    ASSERT_TRUE(r.subject.has_value());
    EXPECT_EQ(*r.subject, subject) << "subject 逐一承载（ERR-01 对象定位轴）";
    EXPECT_EQ(r.context, kContext);
    EXPECT_EQ(r.cause, kCause);
    EXPECT_EQ(r.recommendedAction, kAction);
    EXPECT_FALSE(r.comparison.has_value()) << "未给 comparison 时不得发明比较型字段";
    EXPECT_FALSE(r.localName.has_value()) << "make 无 localName 入参——不得伪造";
    EXPECT_FALSE(r.runtimeName.has_value()) << "make 无 runtimeName 入参——不得伪造";
}

/** 锚定：subject 缺省的记录＝瞬时诊断形态——接口不强制（签名 optional），
 *  完整性由消费侧选项核对（allowTransient=false 判失败、true 判通过——
 *  core DiagData.hpp"完整性强制归 diagnostics/reporting 边界"的分工面）。 */
TEST(PolicyDiagnosticsMake, SubjectOptionalIntegrityCheckedByConsumer_ERR_01)
{
    const PolicyDiagnostics diag;
    const core::DiagnosticRecord r = diag.make(
        policyDiagCode(PolicyDiagCode::CllContextExpired), std::nullopt, kContext, kCause, kAction);

    // 稳定口径（allowTransient=false——默认）：subject 缺失记为违规。
    const testkit::CheckResult stable = testkit::checkDiagnosticRecord(r, testkit::DiagnosticCheckOptions{});
    EXPECT_FALSE(stable.passed) << "稳定诊断缺 subject 必须被判违规（ERR-01）";
    // 瞬时口径：迟到调用拒绝等执行语境诊断允许无对象（POL-LATE-1 形态）。
    expectRecordOk(r, testkit::DiagnosticCheckOptions{/*.allowTransient=*/true});
}

/** 锚定：表外码 fail-fast（§9.6"码值权威在注册表"的本侧拦截面；ARC-04
 *  不发明码——来历不明的码不得入正式记录；PolicyError＝本单元异常契约）。 */
TEST(PolicyDiagnosticsMake, RejectsUnknownCodeFailFast_SEC9_6)
{
    const PolicyDiagnostics diag;
    // 小写/表外形两形态都被拒——成员判定是全串精确匹配。
    for (const auto* foreign : {"POLICY-NOT-IN-TABLE", "policy-jnt-table-invalid", ""}) {
        EXPECT_THROW(diag.make(foreign, validSubject(), kContext, kCause, kAction),
                     PolicyError)
            << "表外码未拦截: " << foreign;
    }
    // 拦截面覆盖同串大小写漂移（表内码的大小写变体＝表外码）。
    try {
        diag.make("POLICY-JNT-TABLE-INVALID-X", validSubject(), kContext, kCause, kAction);
        ADD_FAILURE() << "近邻表外码未被拦截";
    }
    catch (const PolicyError& e) {
        EXPECT_NE(std::string{e.what()}.find("POLICY-JNT-TABLE-INVALID-X"),
                  std::string::npos)
            << "异常消息应携带违规码原文便于定位: " << e.what();
    }
}

/** 锚定：C-3 必填串空 → PolicyError（core 工厂的必填半场在 policy 侧前置
 *  核对并转译为本单元异常——core 异常类型不向 policy 调用方泄漏）。 */
TEST(PolicyDiagnosticsMake, RejectsBlankRequiredStrings_CORE_C3)
{
    const PolicyDiagnostics diag;
    const std::string_view code = policyDiagCode(PolicyDiagCode::UnitMismatch);
    EXPECT_THROW(diag.make(code, validSubject(), "", kCause, kAction), PolicyError)
        << "空 context 未拦截";
    EXPECT_THROW(diag.make(code, validSubject(), kContext, "", kAction), PolicyError)
        << "空 cause 未拦截";
    EXPECT_THROW(diag.make(code, validSubject(), kContext, kCause, ""), PolicyError)
        << "空 recommendedAction 未拦截";
}

/** 锚定：比较型诊断（UX-03 三要素齐备）→ testkit checkDiagnosticRecord 与
 *  checkComparativeFields 双通过＋逐字段断言（ERR-01 比较型三要素——
 *  POL-JNT-1 观测点的构造面基础）。 */
TEST(PolicyDiagnosticsMake, ComparativeRecordPassesContractAndComparativeChecks_UX_03)
{
    const PolicyDiagnostics diag;

    // 实际 6π rad（多圈行程）/期望 4π rad（附录 D 冻结默认）——比较型样例
    // 采用 POL-JNT-1 观测点的同语义素材（rad 为 core R1 冻结单位）。
    const core::UnitToken rad = core::UnitToken::find("rad").value();
    const core::ComparativeFields fields = makeComparative(
        makeComparativeValue(6.0 * 3.14159265358979323846, rad, "policy/joint-limits"),
        makeComparativeValue(4.0 * 3.14159265358979323846, rad, "policy/joint-limits"));

    const core::DiagnosticRecord r = diag.make(
        policyDiagCode(PolicyDiagCode::VersionIncompatible), validSubject(),
        kContext, "实际行程超出阈值", "调整行程阈值或改用多圈机型", fields);

    // testkit 双校验器：记录契约（含比较型三要素完整）＋量纲一致性。
    expectRecordOk(r, testkit::DiagnosticCheckOptions{});
    ASSERT_TRUE(r.comparison.has_value());
    const testkit::CheckResult comparative =
        testkit::checkComparativeFields(*r.comparison, core::QuantityKind::Angle);
    EXPECT_TRUE(comparative.passed) << "量纲核对失败（rad 应属 Angle）";

    // 数值侧逐一承载（Provided 态取值经 tryValue——四态访问纪律）。
    EXPECT_DOUBLE_EQ(r.comparison->actual.quantity.tryValue().value(),
                     6.0 * 3.14159265358979323846);
    EXPECT_DOUBLE_EQ(r.comparison->expected.quantity.tryValue().value(),
                     4.0 * 3.14159265358979323846);
    EXPECT_EQ(r.comparison->actual.unit, rad);
    EXPECT_EQ(r.comparison->expected.unit, rad);
}

/** 锚定：比较型带无效单位句柄 → fail-fast（UX-03"单位必填"的构造期拦截
 *  ——无效句柄不得入正式记录，下游注册表查找/校验器假定句柄有效）。 */
TEST(PolicyDiagnosticsMake, RejectsComparisonWithInvalidUnit_UX_03)
{
    const PolicyDiagnostics diag;
    core::ComparativeFields fields;
    // 默认构造的 ComparativeValue.unit＝无效句柄（tableIndex 0xFFFF——
    // Units.hpp 契约）；数值侧 Provided 不改变单位无效的判定。
    fields.actual.quantity = core::SourcedValue<double>::provided(
        1.0, core::ValueProvenance::make(core::ProvenanceKind::DerivedReadOnly));
    fields.expected.quantity = core::SourcedValue<double>::provided(
        2.0, core::ValueProvenance::make(core::ProvenanceKind::DerivedReadOnly));
    EXPECT_THROW(diag.make(policyDiagCode(PolicyDiagCode::VersionIncompatible),
                           validSubject(), kContext, kCause, kAction, fields),
                 PolicyError)
        << "无效单位句柄未拦截";
}

/** 锚定：NotApplicable 侧构造 → checkDiagnosticRecord 通过且取值恒空
 *  （P-POL-2/ERR-01：显式不适用标记——不伪造数值；§9.6"NotApplicable 输出
 *  经 SourcedValue::notApplicable 语义"原文）。 */
TEST(PolicyDiagnosticsMake, NotApplicableSidePassesCheckWithoutForgedValue_P_POL_2)
{
    const PolicyDiagnostics diag;
    const core::UnitToken rad = core::UnitToken::find("rad").value();

    // 实际行程已测、期望阈值未设置（nullopt＝无冻结默认）——近限位检查的
    // 显式不适用形态：数值侧 NotApplicable＋单位仍标注（不适用的是数值，
    // 不是量纲）。
    const core::ComparativeFields fields = makeComparative(
        makeComparativeValue(0.42, rad, "policy/joint-limits"),
        makeNotApplicableValue(rad));

    const core::DiagnosticRecord r = diag.make(
        policyDiagCode(PolicyDiagCode::JntEngineeringRangeInvalid), validSubject(),
        kContext, "阈值未设置——检查不适用", "如需检查请显式设置阈值", fields);

    expectRecordOk(r, testkit::DiagnosticCheckOptions{});
    ASSERT_TRUE(r.comparison.has_value());
    EXPECT_EQ(r.comparison->expected.quantity.state(), core::FieldState::NotApplicable);
    EXPECT_FALSE(r.comparison->expected.quantity.tryValue().has_value())
        << "NotApplicable 态取值必须为空（不伪造数值）";
}

/** 锚定：Invalid 侧原文保留 → checkDiagnosticRecord 通过（§7.5/NFR-COR-03：
 *  非有限值以原文保留不伪造——makeComparativeValue 的 Invalid 分派在完整
 *  构造链路中的承载证据）。 */
TEST(PolicyDiagnosticsMake, InvalidSidePreservesRawText_NFR_COR_03)
{
    const PolicyDiagnostics diag;
    const core::UnitToken rad = core::UnitToken::find("rad").value();

    // 构型位置 NaN（§7.5 评估期数值异常样例）——Invalid 态携带 "nan" 原文。
    const core::ComparativeFields fields = makeComparative(
        makeComparativeValue(std::numeric_limits<double>::quiet_NaN(), rad,
                             "policy/joint-limits"),
        makeComparativeValue(0.0, rad, "policy/joint-limits"));

    const core::DiagnosticRecord r = diag.make(
        policyDiagCode(PolicyDiagCode::CllEvaluationFailed), validSubject(),
        kContext, "实测位置非有限", "检查构型输入与运动学链", fields);

    expectRecordOk(r, testkit::DiagnosticCheckOptions{});
    ASSERT_TRUE(r.comparison.has_value());
    EXPECT_EQ(r.comparison->actual.quantity.state(), core::FieldState::Invalid);
    EXPECT_EQ(r.comparison->actual.quantity.invalidRawInput(), "nan")
        << "原文保留（%.17g 的 NaN 字面量形态）";
}

/** 锚定：确定性——同参数两次构造逐字段相等（NFR-COR-02；无时钟/随机源，
 *  core::DiagnosticRecord 的相等比较为逐字段——诊断不含时间戳字段）。 */
TEST(PolicyDiagnosticsMake, DeterministicConstruction_NFR_COR_02)
{
    const PolicyDiagnostics diag;
    const core::ObjectId subject = validSubject();
    const std::string_view code = policyDiagCode(PolicyDiagCode::ScopeObjectMissing);

    const core::DiagnosticRecord a = diag.make(code, subject, kContext, kCause, kAction);
    const core::DiagnosticRecord b = diag.make(code, subject, kContext, kCause, kAction);
    EXPECT_EQ(a, b) << "同输入两次构造结果不同——违反确定性（NFR-COR-02）";
}

// =====================================================================
// makeComparative 辅助（§3.1 组成表第三实体——单侧构造语义）。
// =====================================================================

/** 锚定：有限值 → Provided＋DerivedReadOnly＋methodTag 承载（DYN-06：来源
 *  标注是事实记录——溯源链路的构造面证据）。 */
TEST(MakeComparativeHelpers, FiniteValueProvidesWithProvenance_DYN_06)
{
    const core::UnitToken m = core::UnitToken::find("m").value();
    const core::ComparativeValue side = makeComparativeValue(0.02, m, "policy-compat");

    EXPECT_EQ(side.unit, m);
    ASSERT_EQ(side.quantity.state(), core::FieldState::Provided);
    EXPECT_DOUBLE_EQ(side.quantity.tryValue().value(), 0.02);
    // 溯源：类别与方法标记逐一承载（ValueProvenance 四字段为 public 值）。
    EXPECT_EQ(side.quantity.provenance().kind, core::ProvenanceKind::DerivedReadOnly);
    ASSERT_TRUE(side.quantity.provenance().methodTag.has_value());
    EXPECT_EQ(*side.quantity.provenance().methodTag, "policy-compat");
}

/** 锚定：非有限值 → Invalid＋原文保留（NaN/±Inf 的 %.17g 字面量——§7.5
 *  不静默转 0 的类型化承载；各非有限形态逐一）。 */
TEST(MakeComparativeHelpers, NonFiniteValueInvalidatesWithRawText_NFR_COR_03)
{
    const core::UnitToken rad = core::UnitToken::find("rad").value();

    const core::ComparativeValue nan =
        makeComparativeValue(std::numeric_limits<double>::quiet_NaN(), rad, "policy/joint-limits");
    EXPECT_EQ(nan.quantity.state(), core::FieldState::Invalid);
    EXPECT_EQ(nan.quantity.invalidRawInput(), "nan");

    const core::ComparativeValue inf =
        makeComparativeValue(std::numeric_limits<double>::infinity(), rad, "policy/joint-limits");
    EXPECT_EQ(inf.quantity.state(), core::FieldState::Invalid);
    EXPECT_EQ(inf.quantity.invalidRawInput(), "inf");

    const core::ComparativeValue negInf = makeComparativeValue(
        -std::numeric_limits<double>::infinity(), rad, "policy/joint-limits");
    EXPECT_EQ(negInf.quantity.state(), core::FieldState::Invalid);
    EXPECT_EQ(negInf.quantity.invalidRawInput(), "-inf");
}

/** 锚定：NotApplicable 侧取值恒空＋单位仍标注（量纲不随数值缺位消失——
 *  UX-03"单位必填"对不适用侧同样成立）。 */
TEST(MakeComparativeHelpers, NotApplicableSideKeepsUnit_P_POL_2)
{
    const core::UnitToken m = core::UnitToken::find("m").value();
    const core::ComparativeValue side = makeNotApplicableValue(m);
    EXPECT_EQ(side.unit, m);
    EXPECT_EQ(side.quantity.state(), core::FieldState::NotApplicable);
    EXPECT_FALSE(side.quantity.tryValue().has_value());
}

/** 锚定：makeComparative 纯聚合——两侧字段逐一（不加校验不改写；核对归
 *  IPolicyDiagnostics::make 第三步，分工面在此钉住）。 */
TEST(MakeComparativeHelpers, AggregatesBothSidesVerbatim_SEC9_6)
{
    const core::UnitToken rad = core::UnitToken::find("rad").value();
    const core::ComparativeValue actual = makeComparativeValue(1.5, rad, "policy/joint-limits");
    const core::ComparativeValue expected = makeNotApplicableValue(rad);

    const core::ComparativeFields fields = makeComparative(actual, expected);
    EXPECT_EQ(fields.actual, actual) << "实际侧被改写";
    EXPECT_EQ(fields.expected, expected) << "期望侧被改写";
}

// =====================================================================
// 接口多态面（§9.6 IPolicyDiagnostics 抽象契约——虚函数分派正确性）。
// =====================================================================

/** 锚定：经基类引用调用——registeredCodes/make 的分派落到产品实现（§9.6
 *  接口为跨单元核对面的冻结签名；多态消费形态在此验证）。 */
TEST(PolicyDiagnosticsInterface, PolymorphicUseThroughBaseReference_SEC9_6)
{
    const PolicyDiagnostics concrete;
    const IPolicyDiagnostics& diag = concrete;

    EXPECT_EQ(diag.registeredCodes().size(), kPolicyDiagCodeCount);
    const core::DiagnosticRecord r = diag.make(
        policyDiagCode(PolicyDiagCode::InfoDefaultApplied), std::nullopt,
        kContext, kCause, kAction);
    EXPECT_EQ(r.code, std::string{policyDiagCode(PolicyDiagCode::InfoDefaultApplied)});
}
