/**
 * @file   PolicyParsingTest.cpp
 * @brief  策略解析器与校验器用例组（POL-T04）——POL-PARSE-1~6 错误分类表
 *         全量反例（不短路）、O-10/P-POL-2 保守默认口径与管线级确定性。
 *
 * 设计依据：
 *   - units/policy.md §5.1（解析与发布管线——七段流程/注入接口）、§5.2（校验
 *     规则与错误分类表——本套件反例矩阵的逐行权威）、§11（POL-PARSE-1~6 用例
 *     行）、§12 POL-T04 行（完成条件＝错误分类表全量反例通过；诊断全量不短路）、
 *     §4.3/§4.4/§4.6（无序对/角色词表/域窗/非法实例清单）、§7.2（排除∩必检=∅）
 *   - 需求 ARC-05（策略单一权威）、ERR-01（比较型三要素——POL-PARSE-2/6）、
 *     NFR-COR-03（不静默）、PM-06（未来版本只读拒绝——POL-PARSE-1）
 *   - 任务契约 tasks/foundation/POL-T04.json acceptance 1～2：
 *     ①POL-PARSE-1~6 错误分类表全量反例通过（不短路）——用例名带 _POL_PARSE_N；
 *     ②O-10/P-POL-2 保守口径——默认解析仅附录 D 第 11 项 4π
 *     （origin=DefaultAppendixD），无冻结默认项保持 nullopt——用例名带
 *     _Acceptance2
 *
 * 用例追溯命名（DTB §5.5）：用例名尾部带需求/用例组编号，正文断言处注明
 * 验证的条款。替身说明（POL-TD-1 精神——本套件内的受控上下文替身只替代
 * "修订闭包查询"这一注入面，不冒充碰撞算法/场景语义；完整契约替身归
 * POL-T11 套件）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/core/Units.hpp>
#include <sdurws/ird/policy/Errors.hpp>
#include <sdurws/ird/policy/PolicyInput.hpp>
#include <sdurws/ird/policy/PolicyParsing.hpp>
#include <sdurws/ird/policy/PolicySet.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace sdurws::ird::policy;

/// core 契约类型的短别名（测试可读性）。
namespace core = sdurws::ird::core;

namespace {

/// double π 字面量（与 PolicySet.hpp 同源——期望值不另引入第二常量源）。
constexpr double kPi = 3.141592653589793;

/// 手工构造的有效对象身份（首字节打标——非零有效且字节字典序可控）。
core::ObjectId taggedObjectId(std::uint8_t tag)
{
    core::ObjectId id;
    id.bytes[0] = tag;
    return id;
}

/// 生成一个有效的策略对象身份（core 保留值纪律：generate 保证非零）。
core::ObjectId validObjectId()
{
    return core::ObjectId::generate();
}

// =====================================================================
// 受控上下文替身——IPolicyValidationContext 的测试实现（只替代"修订闭包
// 查询"注入面；应答由测试显式装配，无任何猜测逻辑）。
// =====================================================================

/**
 * @brief 可编程修订闭包替身：存在性/角色/组定义三张显式应答表。
 *
 * 线程安全：单线程测试用（无共享）。确定性：应答即查表——同装配同应答。
 */
class TestValidationContext final : public IPolicyValidationContext {
public:
    std::set<core::ObjectId> existingObjects;          ///< objectExists 应答表
    std::map<core::ObjectId, std::string> roles;       ///< objectRole 应答表
    std::set<std::string> definedGroups;               ///< groupDefined 应答表

    bool objectExists(core::ObjectId object) const override
    {
        return existingObjects.count(object) > 0;
    }

    std::optional<std::string> objectRole(core::ObjectId object) const override
    {
        const auto it = roles.find(object);
        return it == roles.end() ? std::nullopt : std::optional<std::string>(it->second);
    }

    bool groupDefined(std::string_view groupName) const override
    {
        return definedGroups.count(std::string{groupName}) > 0;
    }
};

/// 装配标准闭包：四个对象（两个普通＋工具＋工件）＋一个已定义组。
TestValidationContext standardContext()
{
    TestValidationContext ctx;
    ctx.existingObjects = {taggedObjectId(0x01), taggedObjectId(0x02),
                           taggedObjectId(0x03), taggedObjectId(0x04)};
    ctx.roles = {{taggedObjectId(0x03), "Tool"},          // 工具对象
                 {taggedObjectId(0x04), "Workpiece"}};    // 工件对象
    ctx.definedGroups = {"jigs"};
    return ctx;
}

/// 便捷访问：标准闭包中的对象（语义见 standardContext 注释）。
core::ObjectId objA() { return taggedObjectId(0x01); }
core::ObjectId objB() { return taggedObjectId(0x02); }
core::ObjectId toolObj() { return taggedObjectId(0x03); }
core::ObjectId workpieceObj() { return taggedObjectId(0x04); }
core::ObjectId missingObj() { return taggedObjectId(0x7F); }   ///< 不在闭包内的对象

/// 组装一个"全部校验通过"的基线输入（各反例用例在其上做单点变异——
/// 保证每条诊断可归因到被测异常，而非叠加噪声）。
RawPolicyInput baseInput()
{
    RawPolicyInput in;
    in.schemaVersion = PolicySchema::currentVersion;
    in.policyObject = validObjectId();

    // 碰撞规则：启用三域＋安全间距 0.02 m＋一对必检＋一对排除（互不覆盖）。
    in.collision.enabled = true;
    in.collision.enabledDomains = {CollisionDomain::Self, CollisionDomain::Environment,
                                   CollisionDomain::Tool};
    in.collision.safetyClearance = RawThresholdInput{0.02, "m"};
    in.collision.excludeAdjacentLinksByDefault = true;
    in.collision.mandatoryPairs = {PairRule::make(ScopeTarget::makeObject(objA()),
                                                  ScopeTarget::makeObject(objB()),
                                                  PolicyRuleLevel::Must, "结构干涉必检")};
    in.collision.excludedPairs = {PairRule::make(ScopeTarget::makeObject(toolObj()),
                                                 ScopeTarget::makeObject(workpieceObj()),
                                                 PolicyRuleLevel::Should, "夹持接触豁免")};

    // 阈值：全部显式提供（默认口径用例另行移除槽位）。
    in.jointThresholds.nearLimitRatio = RawThresholdInput{0.05, ""};
    in.jointThresholds.conditionNumberWarning = RawThresholdInput{10.0, ""};
    in.jointThresholds.finiteRotationTravelLimit = RawThresholdInput{4.0 * kPi, "rad"};
    in.jointThresholds.travelLimitCheckEnabled = true;

    // 适用范围：限定两模式＋一个存在的模型对象。
    in.applicability.modes = {core::EvaluationMode::Quick, core::EvaluationMode::Verified};
    in.applicability.modelObjects = {objA()};

    in.origin.kind = PolicyOriginKind::UserEdited;
    return in;
}

/// 解析基线输入的便捷入口（标准闭包）。
PolicyParseResult parseBase()
{
    const TestValidationContext ctx = standardContext();
    return resolvePolicy(baseInput(), ctx);
}

/// 取诊断码串（首个匹配码的指针；断言可读性辅助）。
bool hasCode(const std::vector<core::DiagnosticRecord>& diags, const std::string& code)
{
    for (const auto& d : diags) {
        if (d.code == code) {
            return true;
        }
    }
    return false;
}

/// 统计某码出现次数（不短路断言用——多实例异常逐条计数）。
std::size_t countCode(const std::vector<core::DiagnosticRecord>& diags, const std::string& code)
{
    std::size_t n = 0;
    for (const auto& d : diags) {
        if (d.code == code) {
            ++n;
        }
    }
    return n;
}

/// §9.6 建议码常量（断言可读性——与 Errors.cpp registryCode 同源串面）。
const std::string kCodeUnknownField = "POLICY-SCHEMA-UNKNOWN-FIELD";
const std::string kCodeVersionFuture = "POLICY-SCHEMA-VERSION-FUTURE";
const std::string kCodeVersionUnknown = "POLICY-SCHEMA-VERSION-UNKNOWN";
const std::string kCodeNonFinite = "POLICY-THRESHOLD-NON-FINITE";
const std::string kCodeNonPositive = "POLICY-THRESHOLD-NON-POSITIVE";
const std::string kCodeOutOfRange = "POLICY-THRESHOLD-OUT-OF-RANGE";
const std::string kCodeRequiredMissing = "POLICY-THRESHOLD-REQUIRED-MISSING";
const std::string kCodeUnitMismatch = "POLICY-UNIT-MISMATCH";
const std::string kCodeRuleDuplicate = "POLICY-RULE-DUPLICATE";
const std::string kCodeRuleConflict = "POLICY-RULE-CONFLICT";
const std::string kCodeRuleCycle = "POLICY-RULE-CYCLE";
const std::string kCodeScopeMissing = "POLICY-SCOPE-OBJECT-MISSING";
const std::string kCodeApplicability = "POLICY-APPLICABILITY-INVALID";
const std::string kInfoDefaultApplied = "POLICY-INFO-DEFAULT-APPLIED";

}  // namespace

// =====================================================================
// POL-PARSE-1：未来版本拒绝（§5.2 行 2/PM-06；比较型诊断含实际/期望代）。
// =====================================================================

TEST(PolicyParseSchema, FutureVersionRejectedWithComparativeAndUpgradeGuide_POL_PARSE_1)
{
    RawPolicyInput in = baseInput();
    in.schemaVersion = PolicySchema::currentVersion + 1;   // 未来代（当前 1 → 2）

    const TestValidationContext ctx = standardContext();
    const PolicyParseResult r = resolvePolicy(in, ctx);

    // 行 2 处置：空 policy＋POLICY-SCHEMA-VERSION-FUTURE＋升级指引。
    EXPECT_FALSE(r.policy.has_value());
    ASSERT_EQ(r.diagnostics.size(), 1u);
    EXPECT_EQ(r.diagnostics[0].code, kCodeVersionFuture);
    // 比较型三要素（§11 POL-PARSE-1 行）：实际/期望版本，代数无量纲（单位 "1"）。
    ASSERT_TRUE(r.diagnostics[0].comparison.has_value());
    EXPECT_EQ(r.diagnostics[0].comparison->actual.quantity.tryValue(),
              std::optional<double>(2.0));
    EXPECT_EQ(r.diagnostics[0].comparison->expected.quantity.tryValue(),
              std::optional<double>(1.0));
    EXPECT_EQ(r.diagnostics[0].comparison->actual.unit.symbol(), "1");
    // 升级指引在建议动作（PM-06 同源——只读拒绝、不自动升级）。
    EXPECT_NE(r.diagnostics[0].recommendedAction.find("升级"), std::string::npos);
}

TEST(PolicyParseSchema, UnknownOlderVersionRejected)
{
    RawPolicyInput in = baseInput();
    in.schemaVersion = 0;   // 低于已知最低代 1

    const TestValidationContext ctx = standardContext();
    const PolicyParseResult r = resolvePolicy(in, ctx);

    EXPECT_FALSE(r.policy.has_value());
    ASSERT_EQ(r.diagnostics.size(), 1u);
    EXPECT_EQ(r.diagnostics[0].code, kCodeVersionUnknown);
}

TEST(PolicyParseSchema, FutureVersionDoesNotInterpretFields)
{
    // 行 2"未来版本不前向猜测解析"：即使载荷同时含非法阈值，也只发版本
    // 诊断——未知布局的字段解释无意义（版本门即返回，见实现注释）。
    RawPolicyInput in = baseInput();
    in.schemaVersion = PolicySchema::currentVersion + 1;
    in.collision.safetyClearance = RawThresholdInput{
        std::numeric_limits<double>::quiet_NaN(), "m"};

    const TestValidationContext ctx = standardContext();
    const PolicyParseResult r = resolvePolicy(in, ctx);

    EXPECT_FALSE(r.policy.has_value());
    ASSERT_EQ(r.diagnostics.size(), 1u);
    EXPECT_EQ(r.diagnostics[0].code, kCodeVersionFuture);
}

TEST(PolicyParseSchema, RoleVocabularyTypoRejectedAsUnknownField)
{
    // §5.1①/§5.2 行 1 的类型层活性检测面：角色 token 词表外的拼写错误
    // → POLICY-SCHEMA-UNKNOWN-FIELD（不静默忽略——防拼写错误静默收窄必检集）。
    RawPolicyInput in = baseInput();
    PairRule typo = PairRule::make(ScopeTarget::makeRole("RobtLink"),   // 拼写错误
                                   ScopeTarget::makeRole("Tool"),
                                   PolicyRuleLevel::Must, "拼写错误反例");
    in.collision.mandatoryPairs.push_back(typo);

    const TestValidationContext ctx = standardContext();
    const PolicyParseResult r = resolvePolicy(in, ctx);

    EXPECT_FALSE(r.policy.has_value());
    EXPECT_EQ(countCode(r.diagnostics, kCodeUnknownField), 1u);
    EXPECT_NE(r.diagnostics[0].cause.find("RobtLink"), std::string::npos);
    // 定位字段指向第 1 条规则的 first 端（输入承载序）。
    ASSERT_TRUE(r.diagnostics[0].localName.has_value());
    EXPECT_EQ(r.diagnostics[0].localName, "collision.mandatoryPairs[1].first");
}

TEST(PolicyParseSchema, SceneObjectRoleTokensFrozenVocabulary)
{
    // §4.3/§6.1 词表冻结：五值、顺序＝SceneObjectRole 枚举声明序（持久化契约）。
    const std::vector<std::string_view> tokens = sceneObjectRoleTokens();
    ASSERT_EQ(tokens.size(), 5u);
    EXPECT_EQ(tokens[0], "RobotLink");
    EXPECT_EQ(tokens[1], "Tool");
    EXPECT_EQ(tokens[2], "Payload");
    EXPECT_EQ(tokens[3], "EnvironmentObject");
    EXPECT_EQ(tokens[4], "Workpiece");
}

// =====================================================================
// POL-PARSE-2：非法阈值（§5.2 行 3/4；逐项拒绝＋原文保留＋比较型三要素）。
// =====================================================================

TEST(PolicyParseThresholds, NonFiniteSafetyClearanceRejectedKeepsRaw_POL_PARSE_2)
{
    RawPolicyInput in = baseInput();
    in.collision.safetyClearance = RawThresholdInput{
        std::numeric_limits<double>::quiet_NaN(), "m"};

    const TestValidationContext ctx = standardContext();
    const PolicyParseResult r = resolvePolicy(in, ctx);

    EXPECT_FALSE(r.policy.has_value());
    ASSERT_EQ(r.diagnostics.size(), 1u);
    EXPECT_EQ(r.diagnostics[0].code, kCodeNonFinite);
    // 原文保留（行 3"原文保留于 RawPolicyInput"——诊断 cause 同时引用原文；
    // %.17g 对 NaN 的稳定文本为 "nan"）。
    EXPECT_NE(r.diagnostics[0].cause.find("nan"), std::string::npos);
    // 非有限无数值期望界——比较型三要素不填（ERR-01 语义：不可比较不伪造）。
    EXPECT_FALSE(r.diagnostics[0].comparison.has_value());
}

TEST(PolicyParseThresholds, InfiniteTravelLimitRejected)
{
    RawPolicyInput in = baseInput();
    in.jointThresholds.finiteRotationTravelLimit = RawThresholdInput{
        std::numeric_limits<double>::infinity(), "rad"};

    const TestValidationContext ctx = standardContext();
    const PolicyParseResult r = resolvePolicy(in, ctx);

    EXPECT_FALSE(r.policy.has_value());
    ASSERT_EQ(r.diagnostics.size(), 1u);
    EXPECT_EQ(r.diagnostics[0].code, kCodeNonFinite);
    EXPECT_NE(r.diagnostics[0].cause.find("inf"), std::string::npos);
}

TEST(PolicyParseThresholds, NegativeSafetyClearanceCarriesComparative)
{
    // §4.6 反例 safetyClearance=−0.01：行 4 左码＋比较型（实际/期望域界/单位）。
    RawPolicyInput in = baseInput();
    in.collision.safetyClearance = RawThresholdInput{-0.01, "m"};

    const TestValidationContext ctx = standardContext();
    const PolicyParseResult r = resolvePolicy(in, ctx);

    EXPECT_FALSE(r.policy.has_value());
    ASSERT_EQ(r.diagnostics.size(), 1u);
    EXPECT_EQ(r.diagnostics[0].code, kCodeNonPositive);
    ASSERT_TRUE(r.diagnostics[0].comparison.has_value());
    EXPECT_EQ(r.diagnostics[0].comparison->actual.quantity.tryValue(), std::optional<double>(-0.01));
    EXPECT_EQ(r.diagnostics[0].comparison->expected.quantity.tryValue(), std::optional<double>(0.0));
    // 比较一律对 SI（§7.4）——实际/期望单位均为字段 SI 单位 m。
    EXPECT_EQ(r.diagnostics[0].comparison->actual.unit.symbol(), "m");
    EXPECT_EQ(r.diagnostics[0].comparison->expected.unit.symbol(), "m");
}

TEST(PolicyParseThresholds, NearLimitRatioOutOfRangeComparative_POL_PARSE_2)
{
    // §11 POL-PARSE-2 原文例 nearLimitRatio=1.5：OUT-OF-RANGE＋比较型三要素。
    RawPolicyInput in = baseInput();
    in.jointThresholds.nearLimitRatio = RawThresholdInput{1.5, ""};

    const TestValidationContext ctx = standardContext();
    const PolicyParseResult r = resolvePolicy(in, ctx);

    EXPECT_FALSE(r.policy.has_value());
    ASSERT_EQ(r.diagnostics.size(), 1u);
    EXPECT_EQ(r.diagnostics[0].code, kCodeOutOfRange);
    ASSERT_TRUE(r.diagnostics[0].comparison.has_value());
    EXPECT_EQ(r.diagnostics[0].comparison->actual.quantity.tryValue(), std::optional<double>(1.5));
    EXPECT_EQ(r.diagnostics[0].comparison->expected.quantity.tryValue(), std::optional<double>(1.0));
    EXPECT_EQ(r.diagnostics[0].comparison->actual.unit.symbol(), "1");
}

TEST(PolicyParseThresholds, ZeroTravelLimitRejected)
{
    // §4.6 反例 finiteRotationTravelLimit=0：(0,+∞) 域窗非正拒绝。
    RawPolicyInput in = baseInput();
    in.jointThresholds.finiteRotationTravelLimit = RawThresholdInput{0.0, "rad"};

    const TestValidationContext ctx = standardContext();
    const PolicyParseResult r = resolvePolicy(in, ctx);

    EXPECT_FALSE(r.policy.has_value());
    ASSERT_EQ(r.diagnostics.size(), 1u);
    EXPECT_EQ(r.diagnostics[0].code, kCodeNonPositive);
    EXPECT_NE(r.diagnostics[0].localName.value(), "");
    EXPECT_EQ(r.diagnostics[0].localName, "jointThresholds.finiteRotationTravelLimit");
}

TEST(PolicyParseThresholds, ConditionNumberBelowOneRejected)
{
    // [1,+∞) 域窗：正值但低于下窗 → OUT-OF-RANGE（两码分列——行 4）。
    RawPolicyInput in = baseInput();
    in.jointThresholds.conditionNumberWarning = RawThresholdInput{0.5, ""};

    const TestValidationContext ctx = standardContext();
    const PolicyParseResult r = resolvePolicy(in, ctx);

    EXPECT_FALSE(r.policy.has_value());
    ASSERT_EQ(r.diagnostics.size(), 1u);
    EXPECT_EQ(r.diagnostics[0].code, kCodeOutOfRange);
}

TEST(PolicyParseThresholds, MultipleThresholdErrorsCollectedWithoutShortCircuit)
{
    // 不短路（acceptance 1）：四个阈值槽位同时非法 → 四条诊断全量、按字段
    // struct 序发射（clearance→nearLimitRatio→conditionNumber→travelLimit）。
    RawPolicyInput in = baseInput();
    in.collision.safetyClearance = RawThresholdInput{
        std::numeric_limits<double>::quiet_NaN(), "m"};
    in.jointThresholds.nearLimitRatio = RawThresholdInput{1.5, ""};
    in.jointThresholds.conditionNumberWarning = RawThresholdInput{0.5, ""};
    in.jointThresholds.finiteRotationTravelLimit = RawThresholdInput{-1.0, "rad"};

    const TestValidationContext ctx = standardContext();
    const PolicyParseResult r = resolvePolicy(in, ctx);

    EXPECT_FALSE(r.policy.has_value());
    ASSERT_EQ(r.diagnostics.size(), 4u);
    EXPECT_EQ(r.diagnostics[0].code, kCodeNonFinite);
    EXPECT_EQ(r.diagnostics[0].localName, "collision.safetyClearance");
    EXPECT_EQ(r.diagnostics[1].code, kCodeOutOfRange);
    EXPECT_EQ(r.diagnostics[1].localName, "jointThresholds.nearLimitRatio");
    EXPECT_EQ(r.diagnostics[2].code, kCodeOutOfRange);
    EXPECT_EQ(r.diagnostics[2].localName, "jointThresholds.conditionNumberWarning");
    EXPECT_EQ(r.diagnostics[3].code, kCodeNonPositive);
    EXPECT_EQ(r.diagnostics[3].localName, "jointThresholds.finiteRotationTravelLimit");
}

TEST(PolicyParseThresholds, ValidThresholdsPublishedWithExplicitOrigin)
{
    // 通过路径：显式阈值以 Explicit 来源发布，SI 真值入模型（归一后）。
    const PolicyParseResult r = parseBase();

    ASSERT_TRUE(r.policy.has_value());
    ASSERT_TRUE(r.policy->collision.safetyClearance.has_value());
    EXPECT_EQ(r.policy->collision.safetyClearance->siValue(), 0.02);
    EXPECT_EQ(r.policy->collision.safetyClearance->origin(), PolicyValueOrigin::Explicit);
    ASSERT_TRUE(r.policy->jointThresholds.nearLimitRatio.has_value());
    EXPECT_EQ(r.policy->jointThresholds.nearLimitRatio->siValue(), 0.05);
    EXPECT_EQ(r.policy->jointThresholds.nearLimitRatio->origin(), PolicyValueOrigin::Explicit);
}

// =====================================================================
// POL-PARSE-3：单位不一致（§5.2 行 6——core convert 错误转发；POL-ID-3
// 管线级：显示单位归一 SI 不入身份）。
// =====================================================================

TEST(PolicyParseUnits, ForceUnitForLengthRejectedForwardsCoreError_POL_PARSE_3)
{
    // §11 POL-PARSE-3 原文例：安全间距携带力单位 "N"（异量纲）——
    // POLICY-UNIT-MISMATCH，cause 转发 core convert 的量纲错误原文。
    RawPolicyInput in = baseInput();
    in.collision.safetyClearance = RawThresholdInput{0.02, "N"};

    const TestValidationContext ctx = standardContext();
    const PolicyParseResult r = resolvePolicy(in, ctx);

    EXPECT_FALSE(r.policy.has_value());
    ASSERT_EQ(r.diagnostics.size(), 1u);
    EXPECT_EQ(r.diagnostics[0].code, kCodeUnitMismatch);
    EXPECT_NE(r.diagnostics[0].cause.find("core/units/dimension"), std::string::npos);
}

TEST(PolicyParseUnits, UnregisteredUnitTokenRejected)
{
    RawPolicyInput in = baseInput();
    in.collision.safetyClearance = RawThresholdInput{0.02, "parsec"};   // 未注册 token

    const TestValidationContext ctx = standardContext();
    const PolicyParseResult r = resolvePolicy(in, ctx);

    EXPECT_FALSE(r.policy.has_value());
    ASSERT_EQ(r.diagnostics.size(), 1u);
    EXPECT_EQ(r.diagnostics[0].code, kCodeUnitMismatch);
    EXPECT_NE(r.diagnostics[0].cause.find("parsec"), std::string::npos);
}

TEST(PolicyParseUnits, MillimeterNormalizedToSi_UnitsNotInIdentity)
{
    // POL-ID-3 管线级：20 mm 归一为 0.02 m（SA-12——换算唯一经 core convert；
    // 显示单位不入发布对象——发布形态无单位字段）。
    RawPolicyInput in = baseInput();
    in.collision.safetyClearance = RawThresholdInput{20.0, "mm"};

    const TestValidationContext ctx = standardContext();
    const PolicyParseResult r = resolvePolicy(in, ctx);

    ASSERT_TRUE(r.policy.has_value());
    ASSERT_TRUE(r.policy->collision.safetyClearance.has_value());
    // 20×0.001 与 0.02 位级一致（POL-T03 DisplayUnitNotInIdentity 已钉）。
    EXPECT_EQ(r.policy->collision.safetyClearance->siValue(), 0.02);
    EXPECT_EQ(r.policy->collision.safetyClearance->origin(), PolicyValueOrigin::Explicit);
}

TEST(PolicyParseUnits, DegreeTravelLimitNormalizedToRadian)
{
    // 360 deg → 2π rad（角度制式显式：解析期归一 SI rad——AGENTS §2.5）。
    RawPolicyInput in = baseInput();
    in.jointThresholds.finiteRotationTravelLimit = RawThresholdInput{360.0, "deg"};

    const TestValidationContext ctx = standardContext();
    const PolicyParseResult r = resolvePolicy(in, ctx);

    ASSERT_TRUE(r.policy.has_value());
    // deg 因子（π/180）非精确二进制——按 1e-12 相对容差断言（core UT-UNIT 口径）。
    const double si = r.policy->jointThresholds.finiteRotationTravelLimit.siValue();
    EXPECT_NEAR(si, 2.0 * kPi, 1e-9);
    EXPECT_EQ(r.policy->jointThresholds.finiteRotationTravelLimit.origin(),
              PolicyValueOrigin::Explicit);
}

TEST(PolicyParseUnits, EmptyUnitTokenMeansFieldSiUnit)
{
    // 空 token＝输入未提供单位——按字段域默认 SI 单位解释（factor 1）。
    RawPolicyInput in = baseInput();
    in.collision.safetyClearance = RawThresholdInput{0.02, ""};
    in.jointThresholds.finiteRotationTravelLimit = RawThresholdInput{4.0 * kPi, ""};

    const TestValidationContext ctx = standardContext();
    const PolicyParseResult r = resolvePolicy(in, ctx);

    ASSERT_TRUE(r.policy.has_value());
    EXPECT_EQ(r.policy->collision.safetyClearance->siValue(), 0.02);
    EXPECT_EQ(r.policy->jointThresholds.finiteRotationTravelLimit.siValue(), 4.0 * kPi);
}

// =====================================================================
// POL-PARSE-4：规则冲突（§5.2 行 8/§7.2——含经 Role/Group 展开的等价对）。
// =====================================================================

TEST(PolicyParseRules, DirectCrossListConflictRejected_POL_PARSE_4)
{
    // 同一对象对同时登记必检与排除——结构矛盾（D-06：排除∩必检=∅）。
    RawPolicyInput in = baseInput();
    in.collision.excludedPairs.push_back(PairRule::make(
        ScopeTarget::makeObject(objB()), ScopeTarget::makeObject(objA()),
        PolicyRuleLevel::Should, "反例：排除必检对"));

    const TestValidationContext ctx = standardContext();
    const PolicyParseResult r = resolvePolicy(in, ctx);

    EXPECT_FALSE(r.policy.has_value());
    ASSERT_EQ(r.diagnostics.size(), 1u);
    EXPECT_EQ(r.diagnostics[0].code, kCodeRuleConflict);
    // 定位冲突对：localName 同时指向两个清单条目（optional<string>——
    // 先解引用再查子串；构造保证有值）。
    ASSERT_TRUE(r.diagnostics[0].localName.has_value());
    EXPECT_NE(r.diagnostics[0].localName->find("mandatoryPairs"), std::string::npos);
    EXPECT_NE(r.diagnostics[0].localName->find("excludedPairs"), std::string::npos);
}

TEST(PolicyParseRules, RoleExpansionConflictRejected_POL_PARSE_4)
{
    // §7.2 原文场景："排除工具类不得暗中吞掉必检工具—工件对"——必检
    // {Role(Tool), 工件} 对排除 {t1, 工件}（t1 的建模角色为 Tool）：
    // 经 Role 展开后覆盖同一对象对 → CONFLICT。
    RawPolicyInput in = baseInput();
    in.collision.mandatoryPairs = {PairRule::make(ScopeTarget::makeRole("Tool"),
                                                  ScopeTarget::makeObject(workpieceObj()),
                                                  PolicyRuleLevel::Must, "工具—工件必检")};
    in.collision.excludedPairs = {PairRule::make(ScopeTarget::makeObject(toolObj()),
                                                 ScopeTarget::makeObject(workpieceObj()),
                                                 PolicyRuleLevel::Should, "反例：排除工具类")};

    const TestValidationContext ctx = standardContext();
    const PolicyParseResult r = resolvePolicy(in, ctx);

    EXPECT_FALSE(r.policy.has_value());
    ASSERT_EQ(r.diagnostics.size(), 1u);
    EXPECT_EQ(r.diagnostics[0].code, kCodeRuleConflict);
}

TEST(PolicyParseRules, GroupNameConflictRejected_POL_PARSE_4)
{
    // Group≡Group（同名展开集必交）——必检与排除为"同一对类"（同组名＋同
    // 另一端对象）时结构矛盾。注：{组,A} 对 {组,B} 不必然冲突（组成员级
    // 交集解析期不可见，归会话构建复核——§7.2 两层防御）。
    RawPolicyInput in = baseInput();
    in.collision.mandatoryPairs = {PairRule::make(ScopeTarget::makeGroup("jigs"),
                                                  ScopeTarget::makeObject(objA()),
                                                  PolicyRuleLevel::Must, "工装组必检")};
    in.collision.excludedPairs = {PairRule::make(ScopeTarget::makeObject(objA()),
                                                 ScopeTarget::makeGroup("jigs"),
                                                 PolicyRuleLevel::Should, "反例：排除同组")};

    const TestValidationContext ctx = standardContext();
    const PolicyParseResult r = resolvePolicy(in, ctx);

    EXPECT_FALSE(r.policy.has_value());
    ASSERT_EQ(r.diagnostics.size(), 1u);
    EXPECT_EQ(r.diagnostics[0].code, kCodeRuleConflict);
}

TEST(PolicyParseRules, RoleExpansionDuplicateWithinListRejected)
{
    // 行 7"同对同类型重复"的展开面：{Role(Tool), 工件} 与 {t1, 工件}
    // （t1 角色为 Tool）展开后为同一必检对——重复登记拒绝。
    // （排除清单改用与必检无关的对象对，隔离行 8 冲突面——单异常归因。）
    RawPolicyInput in = baseInput();
    in.collision.excludedPairs = {PairRule::make(ScopeTarget::makeObject(objA()),
                                                 ScopeTarget::makeObject(objB()),
                                                 PolicyRuleLevel::Should, "无关排除")};
    in.collision.mandatoryPairs = {PairRule::make(ScopeTarget::makeRole("Tool"),
                                                  ScopeTarget::makeObject(workpieceObj()),
                                                  PolicyRuleLevel::Must, "类级登记"),
                                   PairRule::make(ScopeTarget::makeObject(toolObj()),
                                                  ScopeTarget::makeObject(workpieceObj()),
                                                  PolicyRuleLevel::Must, "对象级登记")};

    const TestValidationContext ctx = standardContext();
    const PolicyParseResult r = resolvePolicy(in, ctx);

    EXPECT_FALSE(r.policy.has_value());
    ASSERT_EQ(r.diagnostics.size(), 1u);
    EXPECT_EQ(r.diagnostics[0].code, kCodeRuleDuplicate);
}

TEST(PolicyParseRules, ExactDuplicateListsBothEntries_POL_PARSE_5)
{
    // 行 7 字面反例：同对同类型重复登记（理由不同仍为重复——对＋级别是
    // 规则语义单元）；诊断列出重复双方条目。
    RawPolicyInput in = baseInput();
    in.collision.mandatoryPairs = {PairRule::make(ScopeTarget::makeObject(objA()),
                                                  ScopeTarget::makeObject(objB()),
                                                  PolicyRuleLevel::Must, "理由一"),
                                   PairRule::make(ScopeTarget::makeObject(objA()),
                                                  ScopeTarget::makeObject(objB()),
                                                  PolicyRuleLevel::Must, "理由二")};

    const TestValidationContext ctx = standardContext();
    const PolicyParseResult r = resolvePolicy(in, ctx);

    EXPECT_FALSE(r.policy.has_value());
    ASSERT_EQ(r.diagnostics.size(), 1u);
    EXPECT_EQ(r.diagnostics[0].code, kCodeRuleDuplicate);
    EXPECT_NE(r.diagnostics[0].cause.find("理由一"), std::string::npos);
    EXPECT_NE(r.diagnostics[0].cause.find("理由二"), std::string::npos);
}

TEST(PolicyParseRules, PairEndsNormalizedToLexicographicOrderInOutput)
{
    // §5.1②"成对规则规范化为字典序（消除顺序歧义）"：输入端序 {B,A} →
    // 发布产物承载 {A,B}（ScopeTarget 规范键——Object 按 Id128 字节序）。
    RawPolicyInput in = baseInput();
    in.collision.mandatoryPairs = {PairRule::make(ScopeTarget::makeObject(objB()),
                                                  ScopeTarget::makeObject(objA()),
                                                  PolicyRuleLevel::Must, "端序反例")};

    const TestValidationContext ctx = standardContext();
    const PolicyParseResult r = resolvePolicy(in, ctx);

    ASSERT_TRUE(r.policy.has_value());
    ASSERT_EQ(r.policy->collision.mandatoryPairs.size(), 1u);
    EXPECT_EQ(r.policy->collision.mandatoryPairs[0].first.object, objA());
    EXPECT_EQ(r.policy->collision.mandatoryPairs[0].second.object, objB());
}

// =====================================================================
// POL-PARSE-5：组引用（§5.2 行 9——未定义组；subject/定位）＋诊断全量。
// =====================================================================

TEST(PolicyParseRules, UndefinedGroupReferenceRejected_POL_PARSE_5)
{
    RawPolicyInput in = baseInput();
    in.collision.mandatoryPairs.push_back(PairRule::make(
        ScopeTarget::makeGroup("ghost"), ScopeTarget::makeObject(objA()),
        PolicyRuleLevel::Must, "引用未定义组反例"));

    const TestValidationContext ctx = standardContext();   // "ghost" 未定义
    const PolicyParseResult r = resolvePolicy(in, ctx);

    EXPECT_FALSE(r.policy.has_value());
    ASSERT_EQ(r.diagnostics.size(), 1u);
    EXPECT_EQ(r.diagnostics[0].code, kCodeRuleCycle);
    EXPECT_NE(r.diagnostics[0].cause.find("ghost"), std::string::npos);
    // 规范化承载序：{组(ghost), A} 的 Object 端较小在前 → 组端为 second。
    EXPECT_EQ(r.diagnostics[0].localName, "collision.mandatoryPairs[1].second");
}

TEST(PolicyParseRules, DefinedGroupReferencePublishes)
{
    // 通过路径：组名已定义（context 应答 true）→ 不发 RULE-CYCLE。
    RawPolicyInput in = baseInput();
    in.collision.mandatoryPairs.push_back(PairRule::make(
        ScopeTarget::makeGroup("jigs"), ScopeTarget::makeObject(objA()),
        PolicyRuleLevel::Must, "工装组必检"));

    const TestValidationContext ctx = standardContext();
    const PolicyParseResult r = resolvePolicy(in, ctx);

    ASSERT_TRUE(r.policy.has_value());
    EXPECT_TRUE(r.diagnostics.empty());   // 无错误无告知——诊断为空
}

// =====================================================================
// POL-PARSE-6：对象不存在（§5.2 行 10/CON-01——subject 绑定对象 ID）。
// =====================================================================

TEST(PolicyParseScope, MissingRuleObjectRejectedBindsSubject_POL_PARSE_6)
{
    RawPolicyInput in = baseInput();
    in.collision.mandatoryPairs = {PairRule::make(ScopeTarget::makeObject(objA()),
                                                  ScopeTarget::makeObject(missingObj()),
                                                  PolicyRuleLevel::Must, "引用闭包外对象")};

    const TestValidationContext ctx = standardContext();
    const PolicyParseResult r = resolvePolicy(in, ctx);

    EXPECT_FALSE(r.policy.has_value());
    ASSERT_EQ(r.diagnostics.size(), 1u);
    EXPECT_EQ(r.diagnostics[0].code, kCodeScopeMissing);
    // §11 POL-PARSE-6 行：subject 绑定对象 ID（定位到缺失对象本身）。
    ASSERT_TRUE(r.diagnostics[0].subject.has_value());
    EXPECT_EQ(*r.diagnostics[0].subject, missingObj());
    EXPECT_EQ(r.diagnostics[0].localName, "collision.mandatoryPairs[0].second");
}

TEST(PolicyParseScope, MissingApplicabilityObjectRejected)
{
    // 适用范围对象集同样经闭包核对（§5.1⑤"引用对象存在性"）。
    RawPolicyInput in = baseInput();
    in.applicability.modelObjects = {objA(), missingObj()};

    const TestValidationContext ctx = standardContext();
    const PolicyParseResult r = resolvePolicy(in, ctx);

    EXPECT_FALSE(r.policy.has_value());
    ASSERT_EQ(r.diagnostics.size(), 1u);
    EXPECT_EQ(r.diagnostics[0].code, kCodeScopeMissing);
    ASSERT_TRUE(r.diagnostics[0].subject.has_value());
    EXPECT_EQ(*r.diagnostics[0].subject, missingObj());
    EXPECT_EQ(r.diagnostics[0].localName, "applicability.modelObjects[1]");
}

TEST(PolicyParseScope, MultipleMissingObjectsAllReported)
{
    // 不短路：两个缺失对象（规则端＋适用范围）各自成条——全量收集。
    RawPolicyInput in = baseInput();
    in.collision.excludedPairs = {PairRule::make(ScopeTarget::makeObject(missingObj()),
                                                 ScopeTarget::makeObject(toolObj()),
                                                 PolicyRuleLevel::Should, "缺失对象一")};
    in.applicability.taskObjects = {missingObj()};   // 同一缺失身份→规范化后仍各表各点

    const TestValidationContext ctx = standardContext();
    const PolicyParseResult r = resolvePolicy(in, ctx);

    EXPECT_FALSE(r.policy.has_value());
    ASSERT_EQ(r.diagnostics.size(), 2u);
    EXPECT_EQ(r.diagnostics[0].code, kCodeScopeMissing);
    // 规范化承载序：{missing, tool} 按字节序重排为 {tool(0x03), missing(0x7F)}
    // → 缺失对象端为 second。
    EXPECT_EQ(r.diagnostics[0].localName, "collision.excludedPairs[0].second");
    EXPECT_EQ(r.diagnostics[1].code, kCodeScopeMissing);
    EXPECT_EQ(r.diagnostics[1].localName, "applicability.taskObjects[0]");
}

// =====================================================================
// 适用范围（§5.2 行 11——空域启用；空集语义与集合规范化）。
// =====================================================================

TEST(PolicyParseApplicability, EmptyDomainsWithEnabledRejected)
{
    // §5.1④：空 enabledDomains 且 enabled=true → 非法（无域可检）。
    // 不填 {Self,Environment,Tool} 默认（空集与显式清空不可区分——§5.1④
    // 权威语义为拒绝；模板默认归装配侧——单元卡 v0.5 增量登记）。
    RawPolicyInput in = baseInput();
    in.collision.enabledDomains.clear();

    const TestValidationContext ctx = standardContext();
    const PolicyParseResult r = resolvePolicy(in, ctx);

    EXPECT_FALSE(r.policy.has_value());
    ASSERT_EQ(r.diagnostics.size(), 1u);
    EXPECT_EQ(r.diagnostics[0].code, kCodeApplicability);
    EXPECT_EQ(r.diagnostics[0].localName, "collision.enabledDomains");
}

TEST(PolicyParseApplicability, EmptyDomainsWithDisabledPublishes)
{
    // §4.3"可整体停用后恢复"：enabled=false 时空域合法（碰撞整体停用）。
    RawPolicyInput in = baseInput();
    in.collision.enabled = false;
    in.collision.enabledDomains.clear();
    in.collision.safetyClearance = std::nullopt;   // 间距检查随总开关停用——可缺省

    const TestValidationContext ctx = standardContext();
    const PolicyParseResult r = resolvePolicy(in, ctx);

    ASSERT_TRUE(r.policy.has_value());
    EXPECT_FALSE(r.policy->collision.enabled);
    EXPECT_TRUE(r.diagnostics.empty());
}

TEST(PolicyParseApplicability, DuplicateDomainsNormalizedInOutput)
{
    // 集合语义（§5.3"集合按稳定序"）：重复与乱序不改变语义——发布产物
    // 升序去重（枚举值序：Self<Environment<Tool<Scene）。
    RawPolicyInput in = baseInput();
    in.collision.enabledDomains = {CollisionDomain::Tool, CollisionDomain::Self,
                                   CollisionDomain::Tool, CollisionDomain::Environment};

    const TestValidationContext ctx = standardContext();
    const PolicyParseResult r = resolvePolicy(in, ctx);

    ASSERT_TRUE(r.policy.has_value());
    const std::vector<CollisionDomain>& domains = r.policy->collision.enabledDomains;
    ASSERT_EQ(domains.size(), 3u);
    EXPECT_EQ(domains[0], CollisionDomain::Self);
    EXPECT_EQ(domains[1], CollisionDomain::Environment);
    EXPECT_EQ(domains[2], CollisionDomain::Tool);
}

// =====================================================================
// O-10/P-POL-2 保守口径（契约 acceptance 2——唯一默认＝4π DefaultAppendixD；
// 无冻结默认项保持 nullopt＝显式不适用；不发明数值、不设第二默认）。
// =====================================================================

TEST(PolicyParseDefaults, TravelLimitDefaultFilledFromAppendixD_Acceptance2)
{
    // 附录 D 第 11 项：行程上限未提供 → 填 4π（origin=DefaultAppendixD，
    // 唯一冻结默认）＋告知性诊断 POLICY-INFO-DEFAULT-APPLIED。
    RawPolicyInput in = baseInput();
    in.jointThresholds.finiteRotationTravelLimit = std::nullopt;

    const TestValidationContext ctx = standardContext();
    const PolicyParseResult r = resolvePolicy(in, ctx);

    ASSERT_TRUE(r.policy.has_value());   // 默认填充不是错误——策略照常发布
    const PolicyThreshold& limit = r.policy->jointThresholds.finiteRotationTravelLimit;
    EXPECT_EQ(limit.siValue(), 4.0 * kPi);   // 位级等于 4π（乘 2 的幂无舍入）
    EXPECT_EQ(limit.origin(), PolicyValueOrigin::DefaultAppendixD);
    ASSERT_EQ(r.diagnostics.size(), 1u);
    EXPECT_EQ(r.diagnostics[0].code, kInfoDefaultApplied);
    EXPECT_EQ(r.diagnostics[0].localName, "jointThresholds.finiteRotationTravelLimit");
    // 随发布对象附带（validationDiagnostics——§5.2 行 12"随发布对象附带"）。
    ASSERT_EQ(r.policy->validationDiagnostics.size(), 1u);
    EXPECT_EQ(r.policy->validationDiagnostics[0].code, kInfoDefaultApplied);
}

TEST(PolicyParseDefaults, UnfrozenDefaultsStayAbsent_Acceptance2)
{
    // P-POL-2/O-10：近限位比与条件数警告无冻结默认——未提供保持 nullopt
    // ＝该警告检查显式不适用；不发明数值、不发默认告知（无默认可告知）。
    RawPolicyInput in = baseInput();
    in.jointThresholds.nearLimitRatio = std::nullopt;
    in.jointThresholds.conditionNumberWarning = std::nullopt;

    const TestValidationContext ctx = standardContext();
    const PolicyParseResult r = resolvePolicy(in, ctx);

    ASSERT_TRUE(r.policy.has_value());
    EXPECT_FALSE(r.policy->jointThresholds.nearLimitRatio.has_value());
    EXPECT_FALSE(r.policy->jointThresholds.conditionNumberWarning.has_value());
    // 行程上限显式提供（4π）——无任何默认告知，诊断为空。
    EXPECT_TRUE(r.diagnostics.empty());
}

TEST(PolicyParseDefaults, ExplicitTravelLimitKeepsExplicitOriginWithoutInfo)
{
    // 显式提供 4π：来源 Explicit、无默认告知——默认告知只随"未提供→填入"。
    RawPolicyInput in = baseInput();   // travel=4π 显式

    const TestValidationContext ctx = standardContext();
    const PolicyParseResult r = resolvePolicy(in, ctx);

    ASSERT_TRUE(r.policy.has_value());
    EXPECT_EQ(r.policy->jointThresholds.finiteRotationTravelLimit.origin(),
              PolicyValueOrigin::Explicit);
    EXPECT_TRUE(r.diagnostics.empty());
}

TEST(PolicyParseDefaults, DefaultAppliedAndExplicitValueShareIdentity)
{
    // 来源标注不入身份（§4.1"来源标注不影响"）：默认填入的 4π 与显式 4π
    // 语义闭包相同 → 内容身份相同（诊断集不同——告知性条目不入身份）。
    RawPolicyInput withoutDefault = baseInput();
    withoutDefault.jointThresholds.finiteRotationTravelLimit = std::nullopt;

    const TestValidationContext ctx = standardContext();
    const PolicyParseResult a = resolvePolicy(withoutDefault, ctx);
    const PolicyParseResult b = resolvePolicy(baseInput(), ctx);

    ASSERT_TRUE(a.policy.has_value());
    ASSERT_TRUE(b.policy.has_value());
    EXPECT_EQ(a.policy->contentIdentity, b.policy->contentIdentity);
    EXPECT_NE(a.diagnostics.size(), b.diagnostics.size());   // 告知性差异可见
}

TEST(PolicyParseDefaults, RequiredMissingWhenEnabledWithoutClearance_Acceptance2)
{
    // §5.2 行 5/P-POL-2 拒绝面：enabled==true 而安全间距未设置——缺失即
    // 拒绝发布，不发明数值。
    RawPolicyInput in = baseInput();
    in.collision.safetyClearance = std::nullopt;

    const TestValidationContext ctx = standardContext();
    const PolicyParseResult r = resolvePolicy(in, ctx);

    EXPECT_FALSE(r.policy.has_value());
    ASSERT_EQ(r.diagnostics.size(), 1u);
    EXPECT_EQ(r.diagnostics[0].code, kCodeRequiredMissing);
    EXPECT_EQ(r.diagnostics[0].localName, "collision.safetyClearance");
}

// =====================================================================
// 管线级契约（POL-ID-1 确定性；codec 协同；validate/resolve 一致性；
// 调用方契约违约 fail-fast；全段不短路总反例）。
// =====================================================================

TEST(PolicyParsePipeline, DeterministicParseTwiceSameResult_POL_ID_1)
{
    // §5.1"解析器为纯函数——同 (输入, 上下文) → 同结果（POL-ID-1）"。
    const TestValidationContext ctx = standardContext();
    const RawPolicyInput in = baseInput();

    const PolicyParseResult a = resolvePolicy(in, ctx);
    const PolicyParseResult b = resolvePolicy(in, ctx);

    ASSERT_TRUE(a.policy.has_value());
    ASSERT_TRUE(b.policy.has_value());
    EXPECT_EQ(*a.policy, *b.policy);              // 对象逐字段相等（含诊断）
    EXPECT_EQ(a.diagnostics, b.diagnostics);
}

TEST(PolicyParsePipeline, PublishedIdentityMatchesCodecRecompute)
{
    // ⑥ 身份＝PolicyCodec::contentIdentity 对发布语义闭包的重算（CR-02：
    // 摘要单点；调用方不可申报身份——发布对象携带的即管线计算的）。
    const PolicyParseResult r = parseBase();

    ASSERT_TRUE(r.policy.has_value());
    const core::ContentIdentity expected = PolicyCodec::contentIdentity(
        r.policy->schemaVersion, r.policy->collision, r.policy->jointThresholds,
        r.policy->applicability, r.policy->numericContractAnchor);
    EXPECT_TRUE(r.policy->contentIdentity.isValid());
    EXPECT_EQ(r.policy->contentIdentity, expected);
}

TEST(PolicyParsePipeline, ParseOfDecodedBytesYieldsSamePolicy)
{
    // 管线与 codec 协同：encode→decode 往返后的输入解析出同一发布对象
    // （承载序规范化的一致性——实现纪律 5；含诊断逐字一致）。
    const TestValidationContext ctx = standardContext();
    const RawPolicyInput in = baseInput();

    const RawPolicyInput decoded = PolicyCodec::decode(PolicyCodec::encode(in));
    const PolicyParseResult a = resolvePolicy(in, ctx);
    const PolicyParseResult b = resolvePolicy(decoded, ctx);

    ASSERT_TRUE(a.policy.has_value());
    ASSERT_TRUE(b.policy.has_value());
    EXPECT_EQ(*a.policy, *b.policy);
    EXPECT_EQ(a.diagnostics, b.diagnostics);
}

TEST(PolicyParsePipeline, ValidatorMatchesResolveDiagnostics)
{
    // §9.2：validate 与 resolvePolicy 共享校验管线——诊断逐字一致；validate
    // 返回类型无 policy（只校验不发布——无发布副作用）。
    const TestValidationContext ctx = standardContext();
    const PolicyValidator validator;

    const RawPolicyInput valid = baseInput();
    EXPECT_EQ(validator.validate(valid, ctx), resolvePolicy(valid, ctx).diagnostics);

    // 非法输入同样一致（重复规则反例）。
    RawPolicyInput invalid = baseInput();
    invalid.collision.mandatoryPairs.push_back(PairRule::make(
        ScopeTarget::makeObject(objA()), ScopeTarget::makeObject(objB()),
        PolicyRuleLevel::Must, "重复条目"));
    EXPECT_EQ(validator.validate(invalid, ctx),
              resolvePolicy(invalid, ctx).diagnostics);
}

TEST(PolicyParsePipeline, CallerContractViolationsFailFast)
{
    // 调用方契约违约 → fail-fast（不走诊断轨——AGENTS 错误总纲；合法解码
    // 产物不可能触发，静默转诊断会掩盖装配侧 bug）。
    const TestValidationContext ctx = standardContext();

    // ① 全零 policyObject。
    RawPolicyInput zeroObject = baseInput();
    zeroObject.policyObject = core::ObjectId{};
    EXPECT_THROW(resolvePolicy(zeroObject, ctx), PolicyError);

    // ② 空 numericContractAnchor（§4.2 必填列）。
    RawPolicyInput emptyAnchor = baseInput();
    emptyAnchor.numericContractAnchor = "";
    EXPECT_THROW(resolvePolicy(emptyAnchor, ctx), PolicyError);

    // ③ 结构非法的 PairRule（聚合构造绕过 PairRule::make——防御性复检拦截）。
    RawPolicyInput malformed = baseInput();
    PairRule bad;
    bad.first = ScopeTarget::makeObject(objA());
    bad.second = ScopeTarget::makeObject(objB());
    bad.level = PolicyRuleLevel::Must;
    bad.reason = "";   // 非空必填违约（§4.3）
    malformed.collision.mandatoryPairs.push_back(bad);
    try {
        static_cast<void>(resolvePolicy(malformed, ctx));
        FAIL() << "预期结构非法输入 fail-fast 抛 PolicyError";
    } catch (const PolicyError& e) {
        EXPECT_EQ(e.code(), PolicyErrorCode::PolicyObjectInvalid);
    }
}

TEST(PolicyParsePipeline, AllStageErrorsCollectedInDeterministicOrder)
{
    // acceptance 1 总反例：跨五个检查面的异常一次性全量收集（不短路），
    // 发射顺序＝管线段序（①token → ②阈值 → ④重复 → ④组 → ⑤对象 → ⑤空域）。
    RawPolicyInput in = baseInput();
    // ① 角色拼写错误（mandatory[0].first）＋⑤ 闭包外对象（mandatory[0].second）。
    in.collision.mandatoryPairs = {PairRule::make(ScopeTarget::makeRole("RobtLink"),
                                                  ScopeTarget::makeObject(missingObj()),
                                                  PolicyRuleLevel::Must, "跨段反例")};
    // ④ 同对同类型重复（两条新增条目互为重复）。
    in.collision.mandatoryPairs.push_back(PairRule::make(
        ScopeTarget::makeObject(objA()), ScopeTarget::makeObject(objB()),
        PolicyRuleLevel::Must, "重复一"));
    in.collision.mandatoryPairs.push_back(PairRule::make(
        ScopeTarget::makeObject(objA()), ScopeTarget::makeObject(objB()),
        PolicyRuleLevel::Must, "重复二"));
    // ④ 引用未定义组（excluded）。
    in.collision.excludedPairs = {PairRule::make(ScopeTarget::makeGroup("ghost"),
                                                 ScopeTarget::makeObject(objB()),
                                                 PolicyRuleLevel::Should, "未定义组")};
    // ② 阈值双非法。
    in.collision.safetyClearance = RawThresholdInput{
        std::numeric_limits<double>::quiet_NaN(), "m"};
    in.jointThresholds.nearLimitRatio = RawThresholdInput{1.5, ""};
    // ⑤ 空域启用。
    in.collision.enabledDomains.clear();

    const TestValidationContext ctx = standardContext();
    const PolicyParseResult r = resolvePolicy(in, ctx);

    EXPECT_FALSE(r.policy.has_value());
    // 七条诊断全量：①角色拼写 → ②间距 NaN → ②近限位比越窗 → ④重复登记
    // → ④未定义组 → ⑤闭包外对象 → ⑤空域启用（发射序＝管线段序）。
    ASSERT_EQ(r.diagnostics.size(), 7u);
    EXPECT_EQ(r.diagnostics[0].code, kCodeUnknownField);
    EXPECT_EQ(r.diagnostics[0].localName, "collision.mandatoryPairs[0].first");
    EXPECT_EQ(r.diagnostics[1].code, kCodeNonFinite);
    EXPECT_EQ(r.diagnostics[2].code, kCodeOutOfRange);
    EXPECT_EQ(r.diagnostics[3].code, kCodeRuleDuplicate);
    EXPECT_EQ(r.diagnostics[4].code, kCodeRuleCycle);
    EXPECT_EQ(r.diagnostics[5].code, kCodeScopeMissing);
    EXPECT_EQ(r.diagnostics[5].localName, "collision.mandatoryPairs[2].first");
    EXPECT_EQ(r.diagnostics[6].code, kCodeApplicability);
}

TEST(PolicyParsePipeline, PublishedPolicyCarriesPolicyObjectAndAuditFields)
{
    // ⑦ 发布对象承载审计面：policyObject/origin/兼容注记透传（不入身份，
    // 但入对象——CR-02 full 形态字段）；validationState 恒 Valid（发布门）。
    RawPolicyInput in = baseInput();
    in.origin.kind = PolicyOriginKind::Imported;
    in.origin.note = "自模板导入";
    in.compatibilityNotes = "v0→v1 迁移说明";

    const TestValidationContext ctx = standardContext();
    const PolicyParseResult r = resolvePolicy(in, ctx);

    ASSERT_TRUE(r.policy.has_value());
    EXPECT_EQ(r.policy->policyObject, in.policyObject);
    EXPECT_EQ(r.policy->origin.kind, PolicyOriginKind::Imported);
    ASSERT_TRUE(r.policy->origin.note.has_value());
    EXPECT_EQ(*r.policy->origin.note, "自模板导入");
    ASSERT_TRUE(r.policy->compatibilityNotes.has_value());
    EXPECT_EQ(*r.policy->compatibilityNotes, "v0→v1 迁移说明");
    EXPECT_EQ(r.policy->validationState, PolicyValidationState::Valid);
    // sourceVersion 由 Provider（POL-T05）填充——解析器恒 nullopt（PA-1）。
    EXPECT_FALSE(r.sourceVersion.has_value());
}
