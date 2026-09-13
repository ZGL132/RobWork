/**
 * @file   PolicySetTest.cpp
 * @brief  policy 字段约束与不可变性用例组（POL-T02）——阈值域校验
 *         （POL-PARSE-2 前置）、四态承载（P-POL-2/O-10 保守口径）、
 *         O-11/O-18 字段不存在钉住、发布门与错误码面。
 *
 * 设计依据：
 *   - units/policy.md §4（数据模型契约）、§4.4/§5.2（阈值域与错误分类——
 *     POL-PARSE-2 的前置字段校验）、§11（POL-PARSE-2 用例行：NaN/±Inf/
 *     负值/越域逐项拒绝）、§12 POL-T02 行（验证方式＝POL-PARSE-2 前置
 *     （字段校验）单测；完成条件＝字段约束与不可变性用例通过）
 *   - 任务契约 tasks/foundation/POL-T02.json acceptance 1～4：
 *     ①字段约束与不可变性用例；②O-10/P-POL-2 保守口径（nullopt＝显式
 *     不适用）；③O-11 不预填惯量比字段；④O-18 本 schema 不并入耦合阈值
 *
 * 用例追溯命名（DTB §5.5）：用例名尾部带需求/用例组编号（POL-PARSE-2/
 * POL-ID/AT-01 等），正文断言处注明验证的条款。
 *
 * 范围声明：本套件只覆盖 POL-T02 落地的类型层契约（字段约束/不可变性/
 * 码面表）；解析管线语义（重复/冲突/循环/对象存在性——POL-T04）与
 * codec 往返（POL-ID-1~5——POL-T03）不在本套件，其前置事实（码面、
 * 阈值域谓词、nullopt 语义）在此钉住。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/policy/Errors.hpp>
#include <sdurws/ird/policy/PolicySet.hpp>

#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>

using namespace sdurws::ird::policy;

/// core 契约类型的短别名（测试可读性；core 类型实际由 policy 公共头传递引入）。
namespace core = sdurws::ird::core;

namespace {

/// double π 字面量（与 PolicySet.hpp 同源——测试期望值不另引入第二常量源）。
constexpr double kPi = 3.141592653589793;

/// 生成一个有效的策略对象身份（core 保留值纪律：generate 保证非零）。
core::ObjectId validPolicyObjectId()
{
    return core::ObjectId::generate();
}

/// 构造一个非全零的内容身份（测试载荷——发布门仅核对 isValid（非零），
/// 语义身份的正确性由 POL-T03 codec/解析管线负责，此处不模拟真实摘要）。
core::ContentIdentity nonZeroContentIdentity(std::uint8_t tag)
{
    core::ContentIdentity cid;
    cid.bytes[0] = tag;   // 首字节打标（保证非全零→isValid()==true）
    return cid;
}

/// 有效碰撞规则子模型（§4.6 合法实例口径：启用 {Self,Environment,Tool}、
/// 安全间距 0.02 m（Explicit）、相邻默认过滤、无显式对）。
CollisionRules validCollisionRules()
{
    return CollisionRules::make(
        true,
        {CollisionDomain::Self, CollisionDomain::Environment, CollisionDomain::Tool},
        PolicyThreshold::make(0.02, PolicyValueOrigin::Explicit,
                              PolicyThresholdDomain::SafetyClearance),
        true, {}, {});
}

/// 有效阈值子模型（§4.6 合法实例口径：行程上限 4π（DefaultAppendixD）、
/// 近限位比 0.05（Explicit）、条件数未设置＝显式不适用（P-POL-2））。
JointThresholds validJointThresholds()
{
    return JointThresholds::make(
        0.05, PolicyValueOrigin::Explicit,
        std::nullopt, PolicyValueOrigin::Explicit,
        kDefaultFiniteRotationTravelLimit, PolicyValueOrigin::DefaultAppendixD);
}

/// 有效适用范围（全空集＝全部适用——§4.2.1 空集语义）。
PolicyApplicability validApplicability()
{
    return PolicyApplicability{};
}

/// 有效发布实例（其余审计/身份字段取合法占位——语义身份真实计算归 POL-T03/T04）。
EngineeringPolicySet validReleasedPolicySet()
{
    return EngineeringPolicySet::make(
        validPolicyObjectId(),
        kPolicySchemaVersionCurrent,
        nonZeroContentIdentity(0xAB),
        validCollisionRules(),
        validJointThresholds(),
        validApplicability(),
        PolicyOrigin{PolicyOriginKind::Template, std::nullopt, std::nullopt},
        PolicyValidationState::Valid);
}

// ---- O-11/O-18 字段不存在检测（SFINAE——C++17 无 requires 表达式） ----

/// 逐名字探测器（以成员访问表达式的合法性作 SFINAE 判据——编译期布尔）。
/// 判读：成员存在→偏特化命中 true_type；不存在→替换失败回落主模板 false_type。
template <typename T, typename = void>
struct HasInertiaRatio : std::false_type {
};
template <typename T>
struct HasInertiaRatio<T, std::void_t<decltype(std::declval<T&>().inertiaRatio)>>
    : std::true_type {
};

template <typename T, typename = void>
struct HasCouplingMatrixThreshold : std::false_type {
};
template <typename T>
struct HasCouplingMatrixThreshold<T,
                                  std::void_t<decltype(std::declval<T&>().couplingMatrixThreshold)>>
    : std::true_type {
};

template <typename T, typename = void>
struct HasCouplingConditionLimit : std::false_type {
};
template <typename T>
struct HasCouplingConditionLimit<T,
                                 std::void_t<decltype(std::declval<T&>().couplingConditionLimit)>>
    : std::true_type {
};

template <typename T, typename = void>
struct HasIllConditionedCouplingThreshold : std::false_type {
};
template <typename T>
struct HasIllConditionedCouplingThreshold<
    T, std::void_t<decltype(std::declval<T&>().illConditionedCouplingThreshold)>>
    : std::true_type {
};

}  // namespace

// =====================================================================
// 错误码面（acceptance 1 的码面基础——POL-T04 解析诊断与异常轨共用）。
// =====================================================================

/** 锚定：token 全表逐枚举钉住（§3.1"稳定 token"——同码同串，NFR-COR-02）。 */
TEST(PolicyErrors, TokenTableFullEnumeration_NFR_COR_02)
{
    // 全表 16 值逐项核对（顺序＝枚举声明顺序；字符串＝§5.2 建议码 kebab 派生；
    // 表尾 3 值为补登：PolicyObjectInvalid（POL-T02）、EncodingInvalid（POL-T03
    // ——对象字节编码契约违约，§9.2"字节损坏在解码期报错"）、
    // PortAssemblyIncomplete（POL-T05——④端口装配契约违约，§9.1 前置行）。
    const std::pair<PolicyErrorCode, const char*> table[] = {
        {PolicyErrorCode::SchemaUnknownField, "policy/schema-unknown-field"},
        {PolicyErrorCode::SchemaVersionFuture, "policy/schema-version-future"},
        {PolicyErrorCode::SchemaVersionUnknown, "policy/schema-version-unknown"},
        {PolicyErrorCode::ThresholdNonFinite, "policy/threshold-non-finite"},
        {PolicyErrorCode::ThresholdNonPositive, "policy/threshold-non-positive"},
        {PolicyErrorCode::ThresholdOutOfRange, "policy/threshold-out-of-range"},
        {PolicyErrorCode::ThresholdRequiredMissing, "policy/threshold-required-missing"},
        {PolicyErrorCode::UnitMismatch, "policy/unit-mismatch"},
        {PolicyErrorCode::RuleDuplicate, "policy/rule-duplicate"},
        {PolicyErrorCode::RuleConflict, "policy/rule-conflict"},
        {PolicyErrorCode::RuleCycle, "policy/rule-cycle"},
        {PolicyErrorCode::ScopeObjectMissing, "policy/scope-object-missing"},
        {PolicyErrorCode::ApplicabilityInvalid, "policy/applicability-invalid"},
        {PolicyErrorCode::PolicyObjectInvalid, "policy/policy-object-invalid"},
        {PolicyErrorCode::EncodingInvalid, "policy/encoding-invalid"},
        {PolicyErrorCode::PortAssemblyIncomplete, "policy/port-assembly-incomplete"},
    };
    for (const auto& [code, expected] : table) {
        EXPECT_EQ(token(code), std::string_view{expected}) << "枚举值 token 漂移";
        EXPECT_FALSE(token(code).empty()) << "全函数承诺：任一枚举值 token 非空";
    }
}

/** 锚定：建议注册码全表（§9.6 建议码清单——码值权威归 diagnostics，PA-1）。 */
TEST(PolicyErrors, RegistryCodeTableFullEnumeration_NFR_COR_02)
{
    // 全表 16 值逐项核对（§9.6 清单 13 值＋表尾补登 3 值：POLICY-POLICY-
    // OBJECT-INVALID（POL-T02）、POLICY-ENCODING-INVALID（POL-T03）、
    // POLICY-PORT-ASSEMBLY-INCOMPLETE（POL-T05）——
    // P-PR-6 同模式，码值权威归 diagnostics，PA-1）。
    const std::pair<PolicyErrorCode, const char*> table[] = {
        {PolicyErrorCode::SchemaUnknownField, "POLICY-SCHEMA-UNKNOWN-FIELD"},
        {PolicyErrorCode::SchemaVersionFuture, "POLICY-SCHEMA-VERSION-FUTURE"},
        {PolicyErrorCode::SchemaVersionUnknown, "POLICY-SCHEMA-VERSION-UNKNOWN"},
        {PolicyErrorCode::ThresholdNonFinite, "POLICY-THRESHOLD-NON-FINITE"},
        {PolicyErrorCode::ThresholdNonPositive, "POLICY-THRESHOLD-NON-POSITIVE"},
        {PolicyErrorCode::ThresholdOutOfRange, "POLICY-THRESHOLD-OUT-OF-RANGE"},
        {PolicyErrorCode::ThresholdRequiredMissing, "POLICY-THRESHOLD-REQUIRED-MISSING"},
        {PolicyErrorCode::UnitMismatch, "POLICY-UNIT-MISMATCH"},
        {PolicyErrorCode::RuleDuplicate, "POLICY-RULE-DUPLICATE"},
        {PolicyErrorCode::RuleConflict, "POLICY-RULE-CONFLICT"},
        {PolicyErrorCode::RuleCycle, "POLICY-RULE-CYCLE"},
        {PolicyErrorCode::ScopeObjectMissing, "POLICY-SCOPE-OBJECT-MISSING"},
        {PolicyErrorCode::ApplicabilityInvalid, "POLICY-APPLICABILITY-INVALID"},
        {PolicyErrorCode::PolicyObjectInvalid, "POLICY-POLICY-OBJECT-INVALID"},
        {PolicyErrorCode::EncodingInvalid, "POLICY-ENCODING-INVALID"},
        {PolicyErrorCode::PortAssemblyIncomplete, "POLICY-PORT-ASSEMBLY-INCOMPLETE"},
    };
    for (const auto& [code, expected] : table) {
        EXPECT_EQ(registryCode(code), std::string_view{expected})
            << "建议注册码与 §9.6 清单漂移";
    }
}

/** 锚定：PolicyError 消息前缀约定（what()＝"<token>[: <detail>]"，构造点强制拼装）。 */
TEST(PolicyErrors, ErrorMessagePrefixConvention_AGENTS_ErrorSemantics)
{
    // 带细节：前缀＝token，后随 ": "＋细节（无裸消息构造路径）。
    const PolicyError withDetail(PolicyErrorCode::ThresholdNonFinite, "细节 x=NaN");
    EXPECT_EQ(std::string(withDetail.what()),
              std::string{"policy/threshold-non-finite"} + ": " + "细节 x=NaN");
    EXPECT_EQ(withDetail.code(), PolicyErrorCode::ThresholdNonFinite);

    // 无细节：what() 恰为 token（无尾随冒号空格）。
    const PolicyError noDetail(PolicyErrorCode::RuleDuplicate);
    EXPECT_EQ(std::string(noDetail.what()), "policy/rule-duplicate");
    EXPECT_EQ(noDetail.code(), PolicyErrorCode::RuleDuplicate);

    // 按值抛/捕获（值语义——runtime/evidence 同款）。
    try {
        throw PolicyError(PolicyErrorCode::UnitMismatch, "N 不是长度量纲");
    }
    catch (const PolicyError& e) {
        EXPECT_EQ(e.code(), PolicyErrorCode::UnitMismatch);
        SUCCEED() << "按值捕获成功（D-15：仅进程内抛传）";
    }
}

// =====================================================================
// PolicyThreshold 域校验（POL-PARSE-2 前置——acceptance 1/2 核心）。
// =====================================================================

/** 锚定：安全间距域 [0,+∞) m——0 合法（§4.3"退化为仅碰撞检测"＋D-08 边界归属）。 */
TEST(PolicyThresholdField, SafetyClearanceZeroIsLegal_D08_Boundary)
{
    const auto zero = PolicyThreshold::make(0.0, PolicyValueOrigin::Explicit,
                                            PolicyThresholdDomain::SafetyClearance);
    EXPECT_DOUBLE_EQ(zero.siValue(), 0.0);   // 0 m 合法（D-08：d=m 属安全侧）
    EXPECT_EQ(zero.origin(), PolicyValueOrigin::Explicit);
    EXPECT_EQ(zero.domain(), PolicyThresholdDomain::SafetyClearance);
}

/** 锚定：安全间距负值拒绝（POL-PARSE-2"负值"项——§5.2 行 4 左码）。 */
TEST(PolicyThresholdField, SafetyClearanceNegativeRejected_POL_PARSE_2)
{
    try {
        (void)PolicyThreshold::make(-0.01, PolicyValueOrigin::Explicit,
                                    PolicyThresholdDomain::SafetyClearance);
        FAIL() << "safetyClearance=-0.01（§4.6 非法反例）应被拒绝";
    }
    catch (const PolicyError& e) {
        EXPECT_EQ(e.code(), PolicyErrorCode::ThresholdNonPositive);
        SUCCEED() << "负值→NON-POSITIVE（原文保留归 RawPolicyInput，POL-T03/T04）";
    }
}

/** 锚定：近限位比 1.5 越窗拒绝（POL-PARSE-2 原文例——§5.2 行 4 右码）。 */
TEST(PolicyThresholdField, NearLimitRatio1_5OutOfRange_POL_PARSE_2)
{
    try {
        (void)PolicyThreshold::make(1.5, PolicyValueOrigin::Explicit,
                                    PolicyThresholdDomain::NearLimitRatio);
        FAIL() << "nearLimitRatio=1.5∉(0,1]（POL-PARSE-2 原文例）应被拒绝";
    }
    catch (const PolicyError& e) {
        EXPECT_EQ(e.code(), PolicyErrorCode::ThresholdOutOfRange);
        SUCCEED() << "越窗→OUT-OF-RANGE（实际值进入诊断细节，可定位）";
    }
}

/** 锚定：近限位比边界 1.0 合法、0/负值拒绝（(0,1] 域窗＋D-08 边界归属）。 */
TEST(PolicyThresholdField, NearLimitRatioBoundaries_D08_POL_PARSE_2)
{
    // 上边界 1.0 合法（域窗含 1）。
    const auto one = PolicyThreshold::make(1.0, PolicyValueOrigin::Explicit,
                                           PolicyThresholdDomain::NearLimitRatio);
    EXPECT_DOUBLE_EQ(one.siValue(), 1.0);

    // 0 拒绝（要求为正→NON-POSITIVE；§4.4 域 (0,1]）。
    try {
        (void)PolicyThreshold::make(0.0, PolicyValueOrigin::Explicit,
                                    PolicyThresholdDomain::NearLimitRatio);
        FAIL() << "nearLimitRatio=0 应被拒绝（域窗不含 0）";
    }
    catch (const PolicyError& e) {
        EXPECT_EQ(e.code(), PolicyErrorCode::ThresholdNonPositive);
    }
    // 负值同码（§5.2 行 4"负值/越域"两码分列：非正优先）。
    try {
        (void)PolicyThreshold::make(-0.1, PolicyValueOrigin::Explicit,
                                    PolicyThresholdDomain::NearLimitRatio);
        FAIL() << "nearLimitRatio=-0.1 应被拒绝";
    }
    catch (const PolicyError& e) {
        EXPECT_EQ(e.code(), PolicyErrorCode::ThresholdNonPositive);
    }
}

/** 锚定：条件数警告域 [1,+∞)——1 合法、(0,1) 越窗、非正拒绝（§4.4 域窗）。 */
TEST(PolicyThresholdField, ConditionNumberDomainWindows_POL_PARSE_2)
{
    // 下边界 1.0 合法（条件数恒≥1，域窗含 1）。
    const auto one = PolicyThreshold::make(1.0, PolicyValueOrigin::Explicit,
                                           PolicyThresholdDomain::ConditionNumberWarning);
    EXPECT_DOUBLE_EQ(one.siValue(), 1.0);

    // 正值但低于下窗→OUT-OF-RANGE。
    try {
        (void)PolicyThreshold::make(0.5, PolicyValueOrigin::Explicit,
                                    PolicyThresholdDomain::ConditionNumberWarning);
        FAIL() << "条件数阈值 0.5<1 应被拒绝（越窗）";
    }
    catch (const PolicyError& e) {
        EXPECT_EQ(e.code(), PolicyErrorCode::ThresholdOutOfRange);
    }
    // 非正→NON-POSITIVE（固定校验序：非正先于越窗）。
    try {
        (void)PolicyThreshold::make(-2.0, PolicyValueOrigin::Explicit,
                                    PolicyThresholdDomain::ConditionNumberWarning);
        FAIL() << "条件数阈值 -2 应被拒绝（非正）";
    }
    catch (const PolicyError& e) {
        EXPECT_EQ(e.code(), PolicyErrorCode::ThresholdNonPositive);
    }
}

/** 锚定：行程上限 (0,+∞) rad——4π 合法（DefaultAppendixD）、6π 合法
 *  （AT-01 反例素材"±1080° 多圈关节调阈值后正常放行"）、0/负拒绝。 */
TEST(PolicyThresholdField, TravelLimitDomain_AT01_MultiTurn)
{
    // 4π（唯一冻结默认）——6π（Explicit）同为合法值（§4.6 合法实例第 2 例）。
    const auto sixPi = PolicyThreshold::make(
        6.0 * kPi, PolicyValueOrigin::Explicit,
        PolicyThresholdDomain::FiniteRotationTravelLimit);
    EXPECT_DOUBLE_EQ(sixPi.siValue(), 6.0 * kPi);   // SI rad（不是度）
    EXPECT_EQ(sixPi.origin(), PolicyValueOrigin::Explicit);

    // 0 拒绝（§4.6 反例"finiteRotationTravelLimit=0"）。
    try {
        (void)PolicyThreshold::make(0.0, PolicyValueOrigin::DefaultAppendixD,
                                    PolicyThresholdDomain::FiniteRotationTravelLimit);
        FAIL() << "行程上限=0 应被拒绝";
    }
    catch (const PolicyError& e) {
        EXPECT_EQ(e.code(), PolicyErrorCode::ThresholdNonPositive);
    }
    // 负值拒绝（同 §4.6 反例"负"）。
    try {
        (void)PolicyThreshold::make(-1.0, PolicyValueOrigin::Explicit,
                                    PolicyThresholdDomain::FiniteRotationTravelLimit);
        FAIL() << "行程上限=-1 应被拒绝";
    }
    catch (const PolicyError& e) {
        EXPECT_EQ(e.code(), PolicyErrorCode::ThresholdNonPositive);
    }
}

/** 锚定：NaN/±Inf 全域拒绝（POL-PARSE-2"NaN/±Inf"项——§5.2 行 3）。 */
TEST(PolicyThresholdField, NonFiniteRejectedForAllDomains_POL_PARSE_2)
{
    const double nonFinite[] = {
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
    };
    const PolicyThresholdDomain domains[] = {
        PolicyThresholdDomain::SafetyClearance,
        PolicyThresholdDomain::NearLimitRatio,
        PolicyThresholdDomain::ConditionNumberWarning,
        PolicyThresholdDomain::FiniteRotationTravelLimit,
    };
    // 逐域×逐非有限值：一律 ThresholdNonFinite（NFR-COR-03：不静默转 0/默认）。
    for (const auto d : domains) {
        for (const double v : nonFinite) {
            try {
                (void)PolicyThreshold::make(v, PolicyValueOrigin::Explicit, d);
                FAIL() << "非有限值应被拒绝（域下标 "
                       << static_cast<int>(d) << "）";
            }
            catch (const PolicyError& e) {
                EXPECT_EQ(e.code(), PolicyErrorCode::ThresholdNonFinite);
            }
        }
    }
}

/** 锚定：阈值不可变封装（§4.4"构造后不可变"——类型层静态特征钉住）。 */
TEST(PolicyThresholdField, ThresholdImmutability_StaticTraits)
{
    // 可拷贝/可整体赋值（值语义＋子模型 optional 槽位装配所需）但不可默认
    // 构造——唯一构造入口 make()（§4.4"构造时校验有限性与符号"的类型层
    // 强制）；成员私有无 setter——字段级修改在类型层不可表达（CanonicalModel
    // 同款封装式不可变；整体赋值源只能是经校验实例，无绕过校验的改值途径）。
    static_assert(std::is_copy_constructible<PolicyThreshold>::value,
                  "阈值须可拷贝（值语义，§4.2）");
    static_assert(std::is_copy_assignable<PolicyThreshold>::value,
                  "整体赋值合法——槽位装配需要（赋值源仅限 make() 产出的已校验实例）");
    static_assert(!std::is_default_constructible<PolicyThreshold>::value,
                  "阈值不可默认构造（唯一构造入口 make()——校验纪律）");
    SUCCEED() << "类型特征静态断言通过（拷贝/整体赋值可、默认构造删、成员私有）";

    // 拷贝语义可用且相等（域元数据一并拷贝）。
    const auto t = PolicyThreshold::make(0.02, PolicyValueOrigin::Explicit,
                                         PolicyThresholdDomain::SafetyClearance);
    const auto copy = t;
    EXPECT_TRUE(copy == t);
    EXPECT_EQ(copy.domain(), PolicyThresholdDomain::SafetyClearance);
}

/** 锚定：同值异域不相等（单位域是工程语义的一部分——m 与 rad 不同物量）。 */
TEST(PolicyThresholdField, SameValueDifferentDomainNotEqual)
{
    const auto clearance = PolicyThreshold::make(1.0, PolicyValueOrigin::Explicit,
                                                 PolicyThresholdDomain::SafetyClearance);
    const auto ratio = PolicyThreshold::make(1.0, PolicyValueOrigin::Explicit,
                                             PolicyThresholdDomain::NearLimitRatio);
    EXPECT_FALSE(clearance == ratio) << "同数值不同域＝不同工程语义（m 对无量纲）";
    EXPECT_TRUE(clearance != ratio);
}

// =====================================================================
// JointThresholds（四态承载＋O-11 钉住）。
// =====================================================================

/**
 * 锚定：O-10/P-POL-2 保守口径（acceptance 2）——默认构造下近限位比与
 * 条件数阈值为 nullopt（该检查显式不适用），行程上限默认 4π（DefaultAppendixD，
 * 唯一冻结默认）；不发明任何数值。
 */
TEST(PolicyJointThresholds, ConservativeDefaultsNoInventedValues_P_POL_2_O10)
{
    const JointThresholds defaults;
    // 无冻结默认项：nullopt＝显式不适用（§4.4 字段注释原文；评估侧输出
    // NotApplicable 标记由 POL-T08 承载）。
    EXPECT_FALSE(defaults.nearLimitRatio.has_value())
        << "近限位比无冻结默认——不得预填数值（P-POL-2）";
    EXPECT_FALSE(defaults.conditionNumberWarning.has_value())
        << "条件数阈值无冻结默认——不得预填数值（P-POL-2）";
    // 唯一冻结默认：4π rad（附录 D 第 11 项），来源 DefaultAppendixD。
    ASSERT_TRUE(defaults.finiteRotationTravelLimit.origin()
                == PolicyValueOrigin::DefaultAppendixD)
        << "行程上限默认来源须为 DefaultAppendixD（唯一上游冻结默认）";
    EXPECT_DOUBLE_EQ(defaults.finiteRotationTravelLimit.siValue(),
                     4.0 * kPi) << "行程上限默认＝4π rad";
    EXPECT_EQ(defaults.finiteRotationTravelLimit.domain(),
              PolicyThresholdDomain::FiniteRotationTravelLimit);
    EXPECT_TRUE(defaults.travelLimitCheckEnabled)
        << "行程校验开关默认 true（MDL-06④ 策略校验默认执行）";
}

/** 锚定：make() 组装——值与来源逐槽位承载，nullopt 语义保持（acceptance 1）。 */
TEST(PolicyJointThresholds, MakeCarriesValuesAndOrigins)
{
    const auto jt = JointThresholds::make(
        0.05, PolicyValueOrigin::Explicit,
        10.0, PolicyValueOrigin::Inherited,
        6.0 * kPi, PolicyValueOrigin::Explicit,
        false);
    ASSERT_TRUE(jt.nearLimitRatio.has_value());
    EXPECT_DOUBLE_EQ(jt.nearLimitRatio->siValue(), 0.05);
    EXPECT_EQ(jt.nearLimitRatio->origin(), PolicyValueOrigin::Explicit);
    EXPECT_EQ(jt.nearLimitRatio->domain(), PolicyThresholdDomain::NearLimitRatio);

    ASSERT_TRUE(jt.conditionNumberWarning.has_value());
    EXPECT_DOUBLE_EQ(jt.conditionNumberWarning->siValue(), 10.0);
    EXPECT_EQ(jt.conditionNumberWarning->origin(), PolicyValueOrigin::Inherited);
    EXPECT_EQ(jt.conditionNumberWarning->domain(),
              PolicyThresholdDomain::ConditionNumberWarning);

    EXPECT_DOUBLE_EQ(jt.finiteRotationTravelLimit.siValue(), 6.0 * kPi);
    EXPECT_EQ(jt.finiteRotationTravelLimit.origin(), PolicyValueOrigin::Explicit);

    EXPECT_FALSE(jt.travelLimitCheckEnabled) << "开关显式关闭被承载";

    // nullopt 透传（未设置语义——P-POL-2）。
    const auto unset = JointThresholds::make(
        std::nullopt, PolicyValueOrigin::Explicit,
        std::nullopt, PolicyValueOrigin::Explicit,
        4.0 * kPi, PolicyValueOrigin::DefaultAppendixD);
    EXPECT_FALSE(unset.nearLimitRatio.has_value());
    EXPECT_FALSE(unset.conditionNumberWarning.has_value());
}

/**
 * 锚定：O-11（acceptance 3）——JointThresholds 不预填惯量比字段
 * （SEL-05 归属裁决在 selection 卡 WP-19-T01，P-POL-3）；schema 当前代＝1
 * 是次版本追加通道的前提（裁决归策略时经通道追加，不在本代预填）。
 */
TEST(PolicyJointThresholds, NoInertiaRatioField_O11_P_POL_3)
{
    // SFINAE 编译期探测：inertiaRatio 成员不存在（预填即违约——归属未裁决）。
    static_assert(!HasInertiaRatio<JointThresholds>::value,
                  "O-11：惯量比字段不得预填（SEL-05 归属裁决在 selection 卡，"
                  "P-POL-3；如裁决归 policy 走 schema 次版本追加通道）");
    // 次版本追加通道版本锚：当前代＝1（§5.3 版本策略——追加可选字段向后兼容）。
    EXPECT_EQ(kPolicySchemaVersionCurrent, 1u);
    SUCCEED() << "O-11 静态断言通过：无惯量比字段；追加通道锚＝schema 代 1";
}

/**
 * 锚定：O-18（acceptance 4）——本 schema 不并入耦合矩阵病态阈值
 * （runtime 侧维持其设计默认，P-RT-7；如裁决并入走次版本追加）。
 */
TEST(PolicyJointThresholds, NoCouplingThresholdField_O18_P_RT_7)
{
    // SFINAE 编译期探测：候选命名（couplingMatrixThreshold/couplingConditionLimit/
    // illConditionedCouplingThreshold）均不存在——并入裁决前不得预填。
    static_assert(!HasCouplingMatrixThreshold<JointThresholds>::value,
                  "O-18：耦合矩阵阈值不得并入本 schema（P-RT-7 待裁决）");
    static_assert(!HasCouplingConditionLimit<JointThresholds>::value,
                  "O-18：耦合条件数阈值不得并入本 schema（P-RT-7 待裁决）");
    static_assert(!HasIllConditionedCouplingThreshold<JointThresholds>::value,
                  "O-18：病态耦合阈值不得并入本 schema（P-RT-7 待裁决）");
    SUCCEED() << "O-18 静态断言通过：无耦合矩阵病态阈值字段（runtime 维持设计默认）";
}

// =====================================================================
// CollisionRules（P-POL-2 必填阈值缺省语义）。
// =====================================================================

/** 锚定：字段默认（§4.3 伪码字面契约——enabled/excludeAdjacent 默认 true）。 */
TEST(PolicyCollisionRules, FieldDefaultsPerCard)
{
    const CollisionRules defaults;
    EXPECT_TRUE(defaults.enabled) << "碰撞域总开关默认启用";
    EXPECT_TRUE(defaults.excludeAdjacentLinksByDefault) << "相邻连杆默认过滤";
    EXPECT_FALSE(defaults.safetyClearance.has_value())
        << "安全间距无 C++ 默认（P-POL-2：无冻结默认——nullopt＝显式不适用）";
    EXPECT_TRUE(defaults.enabledDomains.empty())
        << "启用域无 C++ 默认（默认解析 {Self,Environment,Tool} 归解析管线③——"
           "避免第二默认源）";
}

/**
 * 锚定：必填阈值缺省拒绝发布（§5.2 行 5——enabled==true 而 safetyClearance
 * 未设置；P-POL-2 保守口径"缺失即非法"的直接执行点）。
 */
TEST(PolicyCollisionRules, EnabledWithoutClearanceRejected_POLICY_THRESHOLD_REQUIRED_MISSING)
{
    try {
        (void)CollisionRules::make(
            true, {CollisionDomain::Self, CollisionDomain::Environment}, std::nullopt,
            true, {}, {});
        FAIL() << "enabled==true 且 safetyClearance 未设置应被拒绝发布";
    }
    catch (const PolicyError& e) {
        EXPECT_EQ(e.code(), PolicyErrorCode::ThresholdRequiredMissing);
        SUCCEED() << "无冻结默认项不得静默省略（不发明数值——P-POL-2）";
    }
}

/** 锚定：停用态允许无间距值（§4.3——enabled=false 可整体停用后恢复）。 */
TEST(PolicyCollisionRules, DisabledWithoutClearanceAllowed)
{
    const auto rules = CollisionRules::make(
        false, {CollisionDomain::Self, CollisionDomain::Environment}, std::nullopt,
        true, {}, {});
    EXPECT_FALSE(rules.enabled);
    EXPECT_FALSE(rules.safetyClearance.has_value())
        << "停用态下间距未设置合法（必填约束仅对 enabled==true——§5.2 行 5 条件）";
}

/** 锚定：空域启用拒绝（§5.1④"空 enabledDomains 且 enabled=true→非法：无域可检"）。 */
TEST(PolicyCollisionRules, EnabledEmptyDomainsRejected_APPLICABILITY_INVALID)
{
    try {
        // enabled==true＋间距有值，仅启用域为空——"无域可检"独立成立。
        (void)CollisionRules::make(
            true, {},
            PolicyThreshold::make(0.02, PolicyValueOrigin::Explicit,
                                  PolicyThresholdDomain::SafetyClearance),
            true, {}, {});
        FAIL() << "空启用域且 enabled==true 应被拒绝（无域可检）";
    }
    catch (const PolicyError& e) {
        EXPECT_EQ(e.code(), PolicyErrorCode::ApplicabilityInvalid);
    }
}

/** 锚定：阈值域错位拒绝（"单位由字段位置固定"——rad 阈值误放间距槽位）。 */
TEST(PolicyCollisionRules, WrongDomainThresholdRejected_PolicyObjectInvalid)
{
    try {
        (void)CollisionRules::make(
            true, {CollisionDomain::Self},
            // 行程上限域阈值误放安全间距槽位（4π rad ≠ m——域元数据暴露错位）。
            JointThresholds::defaultFiniteRotationTravelLimit(),
            true, {}, {});
        FAIL() << "非安全间距域的阈值放入间距槽位应被拒绝";
    }
    catch (const PolicyError& e) {
        EXPECT_EQ(e.code(), PolicyErrorCode::PolicyObjectInvalid);
    }
}

/** 锚定：规则对结构校验（§4.3 reason 非空必填＋ScopeTarget kind 一致性）。 */
TEST(PolicyCollisionRules, PairRuleStructureValidation)
{
    // 空理由拒绝（可追溯性必填）。
    try {
        (void)PairRule::make(ScopeTarget::makeRole("RobotLink"),
                             ScopeTarget::makeRole("Tool"),
                             PolicyRuleLevel::Must, "");
        FAIL() << "空理由的规则对应被拒绝（§4.3 非空必填）";
    }
    catch (const PolicyError& e) {
        EXPECT_EQ(e.code(), PolicyErrorCode::PolicyObjectInvalid);
    }
    // 无效对象身份的 Object 目标拒绝（保留值纪律）。
    try {
        (void)ScopeTarget::makeObject(core::ObjectId{});
        FAIL() << "全零对象身份的目标应被拒绝";
    }
    catch (const PolicyError& e) {
        EXPECT_EQ(e.code(), PolicyErrorCode::PolicyObjectInvalid);
    }
    // 合法规则对（理由非空、目标结构合法）可构造且等值可用。
    const auto rule = PairRule::make(
        ScopeTarget::makeObject(core::ObjectId::generate()),
        ScopeTarget::makeRole("EnvironmentObject"),
        PolicyRuleLevel::Should, "邻近夹具非结构碰撞面，允许过滤并留痕");
    EXPECT_EQ(rule.level, PolicyRuleLevel::Should);
    EXPECT_FALSE(rule.reason.empty());
}

// =====================================================================
// EngineeringPolicySet 发布门与不可变性（acceptance 1）。
// =====================================================================

/** 锚定：有效发布实例——字段往返＋全字段等值（POL-ID-1 观测点的类型层承载）。 */
TEST(PolicySetGate, ValidInstanceRoundTripAndEquality)
{
    const auto policy = validReleasedPolicySet();
    EXPECT_TRUE(policy.policyObject.isValid());
    EXPECT_EQ(policy.schemaVersion, kPolicySchemaVersionCurrent) << "当前代＝1";
    EXPECT_TRUE(policy.contentIdentity.isValid()) << "发布对象必有语义内容身份";
    EXPECT_EQ(policy.validationState, PolicyValidationState::Valid)
        << "发布对象只存在 Valid 态（§4.5）";
    EXPECT_EQ(policy.numericContractAnchor, "appendixD@v1.16")
        << "数值契约锚默认（§4.2 默认列）";
    // 子模型字段往返（§4.6 合法实例第 1 例口径）。
    EXPECT_TRUE(policy.collision.enabled);
    ASSERT_TRUE(policy.collision.safetyClearance.has_value());
    EXPECT_DOUBLE_EQ(policy.collision.safetyClearance->siValue(), 0.02);
    EXPECT_EQ(policy.collision.safetyClearance->origin(), PolicyValueOrigin::Explicit);
    EXPECT_FALSE(policy.jointThresholds.conditionNumberWarning.has_value())
        << "validJointThresholds 夹具未设条件数——nullopt 保持（P-POL-2）";
    ASSERT_TRUE(policy.jointThresholds.nearLimitRatio.has_value())
        << "夹具显式给出近限位比 0.05（Explicit）——有值被承载";
    EXPECT_DOUBLE_EQ(policy.jointThresholds.nearLimitRatio->siValue(), 0.05);
    EXPECT_DOUBLE_EQ(policy.jointThresholds.finiteRotationTravelLimit.siValue(),
                     4.0 * kPi);
    EXPECT_TRUE(policy.applicability.modes.empty())
        << "适用范围全空＝全部适用（§4.2.1）";
    // 等值语义：同一实例的拷贝逐字段相等；两次独立 make（各自 generate 新
    // 对象身份）≠ 相等——对象身份参与等值（§4.1：对象身份跨修订稳定且
    // 与内容无关，不同身份即不同对象；内容身份相等性是另一判定轴，归
    // POL-T03/T04 的 POL-ID 用例）。
    const auto copy = policy;
    EXPECT_TRUE(copy == policy) << "拷贝＝全字段相等（值语义）";
    const auto another = validReleasedPolicySet();
    EXPECT_FALSE(policy == another) << "不同对象身份的策略对象不相等";
    EXPECT_TRUE(policy != another);
}

/** 锚定：发布门拒绝无效对象身份/内容身份/校验状态/空锚（§4.5 机械执行）。 */
TEST(PolicySetGate, GateRejectsInvalidIdentityStateAnchor_PolicyObjectInvalid)
{
    const auto id = validPolicyObjectId();
    const auto cid = nonZeroContentIdentity(0x01);
    const auto collision = validCollisionRules();
    const auto joint = validJointThresholds();
    const auto applicability = validApplicability();

    // 全零对象身份（保留值纪律）。
    try {
        (void)EngineeringPolicySet::make(
            core::ObjectId{}, kPolicySchemaVersionCurrent, cid, collision, joint,
            applicability, PolicyOrigin{}, PolicyValidationState::Valid);
        FAIL() << "无效 policyObject 应被拒绝";
    }
    catch (const PolicyError& e) {
        EXPECT_EQ(e.code(), PolicyErrorCode::PolicyObjectInvalid);
    }
    // 全零内容身份（发布对象必有语义身份——CON-05/06）。
    try {
        (void)EngineeringPolicySet::make(
            id, kPolicySchemaVersionCurrent, core::ContentIdentity{}, collision, joint,
            applicability, PolicyOrigin{}, PolicyValidationState::Valid);
        FAIL() << "无效 contentIdentity 应被拒绝";
    }
    catch (const PolicyError& e) {
        EXPECT_EQ(e.code(), PolicyErrorCode::PolicyObjectInvalid);
    }
    // 非 Valid 校验状态（NotValidated 草稿不对外、Invalid 不产生可消费对象）。
    for (const auto state : {PolicyValidationState::NotValidated,
                             PolicyValidationState::Invalid}) {
        try {
            (void)EngineeringPolicySet::make(
                id, kPolicySchemaVersionCurrent, cid, collision, joint, applicability,
                PolicyOrigin{}, state);
            FAIL() << "非 Valid 校验状态应被拒绝（工厂只产出 Valid 实例）";
        }
        catch (const PolicyError& e) {
            EXPECT_EQ(e.code(), PolicyErrorCode::PolicyObjectInvalid);
        }
    }
    // 空数值契约锚（§4.2 必填列）。
    try {
        (void)EngineeringPolicySet::make(
            id, kPolicySchemaVersionCurrent, cid, collision, joint, applicability,
            PolicyOrigin{}, PolicyValidationState::Valid, {}, std::nullopt, "");
        FAIL() << "空 numericContractAnchor 应被拒绝";
    }
    catch (const PolicyError& e) {
        EXPECT_EQ(e.code(), PolicyErrorCode::PolicyObjectInvalid);
    }
}

/**
 * 锚定：发布门 schema 代核对（POL-PARSE-1 的前置码面——未来/未知代在
 * 当前程序中不可发布；解析期对输入的同码面拒绝归 POL-T04）。
 */
TEST(PolicySetGate, GateRejectsFutureAndUnknownSchemaVersions_PM06)
{
    const auto id = validPolicyObjectId();
    const auto cid = nonZeroContentIdentity(0x02);

    // 未来代（>当前）→ SchemaVersionFuture（未来版本不前向猜测——PM-06）。
    try {
        (void)EngineeringPolicySet::make(
            id, kPolicySchemaVersionCurrent + 1u, cid, validCollisionRules(),
            validJointThresholds(), validApplicability(), PolicyOrigin{},
            PolicyValidationState::Valid);
        FAIL() << "未来 schema 代应被拒绝";
    }
    catch (const PolicyError& e) {
        EXPECT_EQ(e.code(), PolicyErrorCode::SchemaVersionFuture);
    }
    // 未知旧代（<当前）→ SchemaVersionUnknown。
    try {
        (void)EngineeringPolicySet::make(
            id, kPolicySchemaVersionCurrent - 1u, cid, validCollisionRules(),
            validJointThresholds(), validApplicability(), PolicyOrigin{},
            PolicyValidationState::Valid);
        FAIL() << "未知旧 schema 代应被拒绝";
    }
    catch (const PolicyError& e) {
        EXPECT_EQ(e.code(), PolicyErrorCode::SchemaVersionUnknown);
    }
}

/** 锚定：发布对象不可变性（§4.2/§4.6——全字段 const，赋值编译期删除）。 */
TEST(PolicySetGate, ReleasedInstanceImmutability_StaticTraits)
{
    // 可拷贝（值语义）但不可赋值（构造后无任何修改途径——类型层保证）。
    static_assert(std::is_copy_constructible<EngineeringPolicySet>::value,
                  "发布对象须可拷贝（值语义，§4.6）");
    static_assert(!std::is_copy_assignable<EngineeringPolicySet>::value,
                  "发布对象构造后不可变（全字段 const——赋值删除，§4.2/§4.6）");
    static_assert(!std::is_default_constructible<EngineeringPolicySet>::value,
                  "发布对象不可默认构造（唯一入口 make() 发布门）");
    SUCCEED() << "类型特征静态断言通过（拷贝可/赋值删/默认构造删）";

    // const 成员只读访问可用（经 const 引用读取子模型）。
    const auto policy = validReleasedPolicySet();
    const CollisionRules& rules = policy.collision;
    EXPECT_TRUE(rules.enabled);
    EXPECT_DOUBLE_EQ(policy.jointThresholds.finiteRotationTravelLimit.siValue(),
                     4.0 * kPi);
}
