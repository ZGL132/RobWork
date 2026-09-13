/**
 * @file   CompatibilityTest.cpp
 * @brief  策略兼容判定用例组（POL-T09）——POL-COMPAT-1/2：schema/后端/
 *         数值锚失配的全量 reason 用例、比较型三要素（schema 数值臂）、
 *         内容身份变化的素材供给面、七臂全量收集不短路、确定性与注入
 *         接口委托、O-20/P-POL-5 保守口径（后端版本串取构建版本）。
 *
 * 设计依据：
 *   - units/policy.md §11（POL-COMPAT-1 用例行：schema/后端/数值锚失配的
 *     请求 → Incompatible＋对应 reason＋POLICY-VERSION-INCOMPATIBLE（比较型）
 *     ——观测点"诊断三要素完整"；POL-COMPAT-2 用例行：策略阈值变化后旧
 *     切片请求——本侧供身份＋原因素材，切片不命中判定归 evidence
 *     judgeCacheHit〔跨单元联动归其契约测试〕）、§8.2（变化分类各行→原因
 *     词表；"不抛异常（判定是查询非违约）"；CON-02 历史保留）、§9.5
 *     （契约表：纯函数/无状态；注入形态接口）、§12 POL-T09 行
 *   - 需求 CON-04（按契约判断兼容）、CON-05（内容身份进缓存键——素材
 *     断言）、NFR-DEP-05（后端版本基线——P-POL-5 暂取口径）、ERR-01/UX-03
 *     （比较型＝数值判定；三要素）、CON-02（历史保留——无删除通道）
 *   - 任务契约 tasks/foundation/POL-T09.json acceptance 1/2
 *
 * 用例追溯命名（DTB §5.5）：用例名尾部带需求/用例组编号（POL_COMPAT_1/
 * POL_COMPAT_2 等），正文断言处注明验证的条款。
 *
 * 范围声明（评审对照点）：
 *   - 本套件为**集成模式专属**：backend-mismatch 臂的正例需要"当前注册
 *     后端"的真值——经唯一构造入口 makeRobWorkCollisionEvaluator().backend()
 *     取得（与 CollisionSessionTest 同款取值通道；框架库仅集成模式可链）。
 *     与 src/Compatibility.cpp 同因同 gating（其 RW_VERSION 注入亦需框架
 *     配置头）。
 *   - policy-invalid 为防御臂（Compatibility.hpp 实现口径 C-3）：发布门
 *     （EngineeringPolicySet::make）只产 Valid 实例——公共 API 无构造非法
 *     对象的通道，该臂**类型层不可达**，无运行期用例；其 token 由词表
 *     全表用例钉住（ReasonTokenTableExactSeven）。此为设计事实登记而非
 *     覆盖缺口。
 *   - 本套件不链接 testkit（POL-T11 落位）——诊断契约三要素以字段断言
 *     直接核对（码/subject/context/cause/action＋comparison 数值侧），语义
 *     与 testkit checkDiagnosticRecord 等价项手工覆盖。
 *   - 替身边界（POL-TD-1 同款声明）：本套件零碰撞替身——判定消费的
 *     ICollisionEvaluator 仅作后端真值来源（唯一装配线），其输出不构成
 *     任何碰撞算法正确性证明。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/policy/CollisionEvaluator.hpp>
#include <sdurws/ird/policy/Compatibility.hpp>
#include <sdurws/ird/policy/PolicyPort.hpp>
#include <sdurws/ird/policy/PolicySet.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace sdurws::ird::policy;

/// core 契约类型的短别名（测试可读性——policy 套件同款）。
namespace core = sdurws::ird::core;

namespace {

// =====================================================================
// 测试夹具（PolicySetTest/JointLimitsTest 同款结构）。
// =====================================================================

/// 手工构造的有效关节对象身份（首字节打标——字节字典序可控）。
core::ObjectId taggedObjectId(std::uint8_t tag)
{
    core::ObjectId id;
    id.bytes[0] = tag;
    return id;
}

/// 手工构造的非全零内容身份（发布门仅核对 isValid——身份与阈值的绑定
/// 关系归 POL-T03 POL-ID-4，本套件只消费"身份不同"这一素材事实）。
core::ContentIdentity nonZeroContentIdentity(std::uint8_t tag)
{
    core::ContentIdentity id;
    id.bytes[0] = tag;
    return id;
}

/// 碰撞已启用的规则集（§4.3 合法实例口径：三域＋安全间距 0.02 m 显式）。
CollisionRules enabledCollisionRules(double clearanceM = 0.02)
{
    return CollisionRules::make(
        true,
        {CollisionDomain::Self, CollisionDomain::Environment, CollisionDomain::Tool},
        PolicyThreshold::make(clearanceM, PolicyValueOrigin::Explicit,
                              PolicyThresholdDomain::SafetyClearance),
        true, {}, {});
}

/// 碰撞已禁用的规则集（enabled=false：域/间距均可空——发布门放行形态，
/// §4.3"禁用被误调用的评估期应答"由评估侧负责，本套件只消费开关）。
CollisionRules disabledCollisionRules()
{
    return CollisionRules::make(false, {}, std::nullopt, true, {}, {});
}

/// 默认阈值子模型（§4.6 合法实例口径——JointLimitsTest 同款）。
JointThresholds defaultJointThresholds()
{
    return JointThresholds::make(
        0.05, PolicyValueOrigin::Explicit,
        std::nullopt, PolicyValueOrigin::Explicit,
        kDefaultFiniteRotationTravelLimit, PolicyValueOrigin::DefaultAppendixD);
}

/**
 * @brief 有效已发布策略（其余审计字段取合法占位——发布门装配）。
 *
 * @param rules        [in] 碰撞规则子模型（启用/禁用两形态）
 * @param applicability [in] 适用范围（缺省空集＝全部模式适用——§4.2.1）
 * @param numericContractAnchor [in] 数值契约锚（缺省 kNumericContractAnchorDefault
 *                     ＝"appendixD@v1.16"）
 * @param identityTag  [in] 内容身份打标字节（区分不同策略实例——素材面）
 */
EngineeringPolicySet makePolicy(const CollisionRules& rules,
                                PolicyApplicability applicability = {},
                                std::string numericContractAnchor
                                    = std::string{kNumericContractAnchorDefault},
                                std::uint8_t identityTag = 0xAB)
{
    return EngineeringPolicySet::make(
        taggedObjectId(0x01),                       // 策略对象身份（非全零）
        kPolicySchemaVersionCurrent,                // schema 代＝当前代（发布门核对）
        nonZeroContentIdentity(identityTag),        // 内容身份（isValid 即可——素材面）
        rules,
        defaultJointThresholds(),
        std::move(applicability),
        PolicyOrigin{PolicyOriginKind::Template, std::nullopt, std::nullopt},
        PolicyValidationState::Valid,
        {},                                         // validationDiagnostics（告知性，可空）
        std::nullopt,                               // compatibilityNotes
        std::move(numericContractAnchor));
}

/// 缺省策略（碰撞启用、全模式适用、默认锚）——判定正例的基准对象。
EngineeringPolicySet basePolicy()
{
    return makePolicy(enabledCollisionRules());
}

/// 当前进程唯一注册后端的真值（§6.5 唯一装配线——与判定实现同源取值通道；
/// 本套件零"第二取值源"，O-20/P-POL-5 用例的同源断言基础）。
CollisionBackendDescriptor currentBackend()
{
    const std::unique_ptr<ICollisionEvaluator> evaluator = makeRobWorkCollisionEvaluator();
    return evaluator->backend();
}

/// 带局部名的诊断检索（断言辅助——按下标取已对齐诊断的可读封装）。
const core::DiagnosticRecord& diagAt(const PolicyCompatibility& r, std::size_t i)
{
    return r.diagnostics.at(i);
}

/**
 * @brief 诊断记录的 ERR-01 基础三件断言（码/主体/上下文＋原因＋建议动作
 *        非空——与 testkit checkDiagnosticRecord 的等价项手工核对；testkit
 *        链接归 POL-T11）。
 */
void expectErr01Baseline(const core::DiagnosticRecord& d, const core::ObjectId& subject)
{
    EXPECT_EQ(d.code, "POLICY-VERSION-INCOMPATIBLE") << "码面恒为建议码原文";
    EXPECT_TRUE(d.subject.has_value()) << "稳定诊断项 subject 必填（ERR-01）";
    if (d.subject.has_value()) {
        EXPECT_EQ(*d.subject, subject) << "主体＝被核对的策略对象";
    }
    EXPECT_FALSE(d.context.empty()) << "上下文非空（ERR-01）";
    EXPECT_FALSE(d.cause.empty()) << "原因非空（ERR-01）";
    EXPECT_FALSE(d.recommendedAction.empty()) << "建议动作非空（ERR-01）";
}

/**
 * @brief schema 数值臂的比较型三要素断言（UX-03：实际值/期望值/单位；
 *        无量纲单位 "1"——版本/代类比较的 SI 占位，PolicyParsing 同款）。
 * @param actualV   [in] 期望的实际代（如策略代 1）
 * @param expectedV [in] 期望的要求代（消费方边界）
 */
void expectSchemaTriple(const core::DiagnosticRecord& d, double actualV, double expectedV)
{
    ASSERT_TRUE(d.comparison.has_value())
        << "schema 臂为数值判定——比较型三要素必须在场（POL-COMPAT-1 观测点）";
    EXPECT_EQ(d.comparison->actual.quantity.state(), core::FieldState::Provided);
    EXPECT_EQ(d.comparison->expected.quantity.state(), core::FieldState::Provided);
    ASSERT_TRUE(d.comparison->actual.quantity.tryValue().has_value());
    ASSERT_TRUE(d.comparison->expected.quantity.tryValue().has_value());
    EXPECT_DOUBLE_EQ(*d.comparison->actual.quantity.tryValue(), actualV) << "实际代";
    EXPECT_DOUBLE_EQ(*d.comparison->expected.quantity.tryValue(), expectedV) << "要求代";
    EXPECT_EQ(d.comparison->actual.unit.symbol(), "1") << "无量纲单位（已注册 token）";
    EXPECT_EQ(d.comparison->expected.unit.symbol(), "1");
}

/// 判定结果含指定原因（断言辅助——读取可读性）。
bool hasReason(const PolicyCompatibility& r, PolicyIncompatibilityReason reason)
{
    for (const PolicyIncompatibilityReason x : r.reasons) {
        if (x == reason) {
            return true;
        }
    }
    return false;
}

}  // namespace

// =====================================================================
// 原因词表全表（词表完整性——含防御臂 token 钉住）。
// =====================================================================

/** §8.1 reasons 注释行七值 token 逐一精确（kebab 词表原文——持久化于失效
 *  原因清单，不改名）；PolicyInvalid 为防御臂 token（其臂类型层不可达，
 *  见文件头范围声明——登记而非覆盖缺口）。 */
TEST(CompatibilityReasonTokens, ReasonTokenTableExactSeven)
{
    // IRD_TEST_INFO 登记归 POL-T11 套件整合——本套件以用例名承载追溯
    // （§12 POL-T09 行：POL-COMPAT-1/2；词表完整性为其前置面）。
    EXPECT_EQ(reasonToken(PolicyIncompatibilityReason::SchemaFuture), "schema-future");
    EXPECT_EQ(reasonToken(PolicyIncompatibilityReason::SchemaObsolete), "schema-obsolete");
    EXPECT_EQ(reasonToken(PolicyIncompatibilityReason::CollisionRequiredDisabled),
              "collision-required-disabled");
    EXPECT_EQ(reasonToken(PolicyIncompatibilityReason::BackendMismatch), "backend-mismatch");
    EXPECT_EQ(reasonToken(PolicyIncompatibilityReason::NumericContractMismatch),
              "numeric-contract-mismatch");
    EXPECT_EQ(reasonToken(PolicyIncompatibilityReason::ModeNotApplicable),
              "mode-not-applicable");
    EXPECT_EQ(reasonToken(PolicyIncompatibilityReason::PolicyInvalid), "policy-invalid");
}

// =====================================================================
// POL-COMPAT-1——版本不兼容（schema/后端/锚失配全量 reason 用例）。
// =====================================================================

/** §8.1/§9.5 正例：缺省要求（当前代/不要碰撞/任意后端/不核对锚/不限
 *  模式）× 合法已发布策略 → Compatible——零误报（缓存接纳前的常态）。 */
TEST(PolCompat1, DefaultRequirementsAcceptPublishedPolicy)
{
    const PolicyCompatibility r = checkPolicyCompatibility(basePolicy(),
                                                           PolicyConsumerRequirements{});
    EXPECT_EQ(r.verdict, PolicyCompatibility::Compatible);
    EXPECT_TRUE(r.reasons.empty()) << "无失配即无原因（全量收集的反面——零命中）";
    EXPECT_TRUE(r.diagnostics.empty()) << "原因与诊断严格同构——零原因为零诊断";
}

/** §8.2"schema 主版本变化"行左（POL-COMPAT-1 版本臂）：消费方只支持
 *  更低代（min=max=0）→ schema-future；比较型三要素（实际 1/期望 0/
 *  单位 "1"）——POL-COMPAT-1 观测点"诊断三要素完整"。 */
TEST(PolCompat1, SchemaFutureOnUpperBoundExceeded)
{
    PolicyConsumerRequirements req;
    req.minSchemaVersion = 0;
    req.maxSchemaVersion = 0;   // 闭区间 [0,0]：策略代 1＞0 → 未来代
    const EngineeringPolicySet policy = basePolicy();
    const PolicyCompatibility r = checkPolicyCompatibility(policy, req);
    EXPECT_EQ(r.verdict, PolicyCompatibility::Incompatible);
    ASSERT_EQ(r.reasons.size(), std::size_t{1}) << "单臂失配恰一原因";
    EXPECT_EQ(r.reasons[0], PolicyIncompatibilityReason::SchemaFuture);
    ASSERT_EQ(r.diagnostics.size(), std::size_t{1}) << "每因一诊（不变式）";
    expectErr01Baseline(diagAt(r, 0), policy.policyObject);
    EXPECT_EQ(diagAt(r, 0).localName, std::optional<std::string>("schemaVersion"));
    expectSchemaTriple(diagAt(r, 0), 1.0, 0.0);
}

/** §8.2"schema 主版本变化"行右（POL-COMPAT-1 版本臂）：消费方要求更高代
 *  （min=max=2）→ schema-obsolete；三要素（实际 1/期望 2）。 */
TEST(PolCompat1, SchemaObsoleteOnLowerBoundUnderrun)
{
    PolicyConsumerRequirements req;
    req.minSchemaVersion = 2;
    req.maxSchemaVersion = 2;   // 闭区间 [2,2]：策略代 1＜2 → 旧代
    const EngineeringPolicySet policy = basePolicy();
    const PolicyCompatibility r = checkPolicyCompatibility(policy, req);
    EXPECT_EQ(r.verdict, PolicyCompatibility::Incompatible);
    ASSERT_EQ(r.reasons.size(), std::size_t{1});
    EXPECT_EQ(r.reasons[0], PolicyIncompatibilityReason::SchemaObsolete);
    ASSERT_EQ(r.diagnostics.size(), std::size_t{1});
    expectErr01Baseline(diagAt(r, 0), policy.policyObject);
    expectSchemaTriple(diagAt(r, 0), 1.0, 2.0);
}

/** §8.1 闭区间语义：策略代恰在 [min,max] 任一端点即兼容（无隐藏容差/
 *  不设开区间）——缺省 [1,1] 与扩窗 [0,2] 均零命中。 */
TEST(PolCompat1, SchemaBoundariesClosedIntervalAccept)
{
    const EngineeringPolicySet policy = basePolicy();
    PolicyConsumerRequirements atBothBounds;
    atBothBounds.minSchemaVersion = 1;
    atBothBounds.maxSchemaVersion = 1;
    EXPECT_EQ(checkPolicyCompatibility(policy, atBothBounds).verdict,
              PolicyCompatibility::Compatible)
        << "策略代等于边界＝兼容（闭区间）";
    PolicyConsumerRequirements widened;
    widened.minSchemaVersion = 0;
    widened.maxSchemaVersion = 2;
    EXPECT_EQ(checkPolicyCompatibility(policy, widened).verdict,
              PolicyCompatibility::Compatible) << "窗口覆盖策略代即兼容";
}

/** §8.1 注释行原文（POL-COMPAT-1 碰撞能力臂）：消费方需要碰撞而策略
 *  enabled=false → collision-required-disabled；正例两态（不需要碰撞×
 *  禁用策略、需要碰撞×启用策略）均零命中。 */
TEST(PolCompat1, CollisionRequiredButDisabledRejected)
{
    const EngineeringPolicySet disabled = makePolicy(disabledCollisionRules());
    PolicyConsumerRequirements req;
    req.requiresCollision = true;
    const PolicyCompatibility r = checkPolicyCompatibility(disabled, req);
    EXPECT_EQ(r.verdict, PolicyCompatibility::Incompatible);
    ASSERT_EQ(r.reasons.size(), std::size_t{1});
    EXPECT_EQ(r.reasons[0], PolicyIncompatibilityReason::CollisionRequiredDisabled);
    ASSERT_EQ(r.diagnostics.size(), std::size_t{1});
    expectErr01Baseline(diagAt(r, 0), disabled.policyObject);
    EXPECT_EQ(diagAt(r, 0).localName, std::optional<std::string>("collision.enabled"));
    EXPECT_FALSE(diagAt(r, 0).comparison.has_value())
        << "布尔开关非数值判定——比较型三要素不在场（UX-03 数值判定口径）";

    // 正例 1：不需要碰撞的消费方 × 禁用策略 → 兼容（开关不参与判定）。
    PolicyConsumerRequirements indifferent;
    EXPECT_EQ(checkPolicyCompatibility(disabled, indifferent).verdict,
              PolicyCompatibility::Compatible);
    // 正例 2：需要碰撞的消费方 × 启用策略 → 兼容。
    EXPECT_EQ(checkPolicyCompatibility(basePolicy(), req).verdict,
              PolicyCompatibility::Compatible);
}

/** §8.2"碰撞后端版本变化"行（POL-COMPAT-1 后端臂）：要求后端与当前注册
 *  后端任一字段不同 → backend-mismatch；三串逐字段精确（版本/标识/容差
 *  模型分别变异各产一因）；诊断 cause 逐字携带双侧原文（ERR-01 原因要素
 *  ——字符串臂无比较型三要素，头文件实现口径 C-2）。 */
TEST(PolCompat1, BackendMismatchOnAnyFieldDifference)
{
    const CollisionBackendDescriptor current = currentBackend();
    const EngineeringPolicySet policy = basePolicy();

    // 变异一：版本串不同（升版/降版同因——复现要素任一差异即失配）。
    CollisionBackendDescriptor otherVersion = current;
    otherVersion.backendVersion += "-variant";
    PolicyConsumerRequirements reqVersion;
    reqVersion.requiresBackend = otherVersion;
    const PolicyCompatibility rVersion = checkPolicyCompatibility(policy, reqVersion);
    EXPECT_EQ(rVersion.verdict, PolicyCompatibility::Incompatible);
    ASSERT_EQ(rVersion.reasons.size(), std::size_t{1});
    EXPECT_EQ(rVersion.reasons[0], PolicyIncompatibilityReason::BackendMismatch);
    ASSERT_EQ(rVersion.diagnostics.size(), std::size_t{1});
    expectErr01Baseline(diagAt(rVersion, 0), policy.policyObject);
    EXPECT_EQ(diagAt(rVersion, 0).localName, std::optional<std::string>("collisionBackend"));
    EXPECT_FALSE(diagAt(rVersion, 0).comparison.has_value()) << "字符串臂无比较型三要素";
    EXPECT_NE(diagAt(rVersion, 0).cause.find(otherVersion.backendVersion), std::string::npos)
        << "要求侧版本串逐字在 cause";
    EXPECT_NE(diagAt(rVersion, 0).cause.find(current.backendVersion), std::string::npos)
        << "当前侧版本串逐字在 cause";

    // 变异二：后端标识不同。
    CollisionBackendDescriptor otherId = current;
    otherId.backendId = "rw.proximity.some-other-backend";
    PolicyConsumerRequirements reqId;
    reqId.requiresBackend = otherId;
    const PolicyCompatibility rId = checkPolicyCompatibility(policy, reqId);
    ASSERT_EQ(rId.reasons.size(), std::size_t{1});
    EXPECT_EQ(rId.reasons[0], PolicyIncompatibilityReason::BackendMismatch);

    // 变异三：容差模型登记不同。
    CollisionBackendDescriptor otherTolerance = current;
    otherTolerance.toleranceModel += "-variant";
    PolicyConsumerRequirements reqTolerance;
    reqTolerance.requiresBackend = otherTolerance;
    const PolicyCompatibility rTolerance = checkPolicyCompatibility(policy, reqTolerance);
    ASSERT_EQ(rTolerance.reasons.size(), std::size_t{1});
    EXPECT_EQ(rTolerance.reasons[0], PolicyIncompatibilityReason::BackendMismatch);
}

/** §8.1"缺省=任意已注册后端"（POL-COMPAT-1 后端臂正例）：要求＝当前唯一
 *  注册后端真值（唯一装配线取得——与判定实现同源）→ 兼容；nullopt 缺省
 *  → 不核对后端（任意注册后端均可）。 */
TEST(PolCompat1, BackendExactDescriptorOrAbsentAccepted)
{
    const CollisionBackendDescriptor current = currentBackend();
    const EngineeringPolicySet policy = basePolicy();
    PolicyConsumerRequirements exact;
    exact.requiresBackend = current;
    EXPECT_EQ(checkPolicyCompatibility(policy, exact).verdict,
              PolicyCompatibility::Compatible)
        << "精确相等的复现要素零命中（逐字段==）";
    EXPECT_TRUE(checkPolicyCompatibility(policy, exact).reasons.empty());
}

/** §8.2"数值契约锚变化"行（POL-COMPAT-1 锚臂）：要求锚≠策略锚 →
 *  numeric-contract-mismatch（附录 D 修订走需求变更）；正例＝默认锚
 *  "appendixD@v1.16" 精确命中零误报；cause 逐字携带双侧锚。 */
TEST(PolCompat1, AnchorMismatchOnAppendixDRevision)
{
    const EngineeringPolicySet policy = basePolicy();
    PolicyConsumerRequirements req;
    req.requiresNumericContract = "appendixD@v1.17";
    const PolicyCompatibility r = checkPolicyCompatibility(policy, req);
    EXPECT_EQ(r.verdict, PolicyCompatibility::Incompatible);
    ASSERT_EQ(r.reasons.size(), std::size_t{1});
    EXPECT_EQ(r.reasons[0], PolicyIncompatibilityReason::NumericContractMismatch);
    ASSERT_EQ(r.diagnostics.size(), std::size_t{1});
    expectErr01Baseline(diagAt(r, 0), policy.policyObject);
    EXPECT_EQ(diagAt(r, 0).localName,
              std::optional<std::string>("numericContractAnchor"));
    EXPECT_FALSE(diagAt(r, 0).comparison.has_value()) << "锚为字符串——无比较型三要素";
    EXPECT_NE(diagAt(r, 0).cause.find("appendixD@v1.17"), std::string::npos)
        << "要求锚逐字在 cause";
    EXPECT_NE(diagAt(r, 0).cause.find(std::string{kNumericContractAnchorDefault}),
              std::string::npos)
        << "策略锚逐字在 cause";

    // 正例：锚精确命中（策略默认锚）→ 兼容。
    PolicyConsumerRequirements same;
    same.requiresNumericContract = std::string{kNumericContractAnchorDefault};
    EXPECT_EQ(checkPolicyCompatibility(policy, same).verdict,
              PolicyCompatibility::Compatible);
}

/** §8.1 评估模式行"适用性核对用"（POL-COMPAT-1 模式臂）：策略 modes 非
 *  空且不含目标模式 → mode-not-applicable；正例两态（目标在清单内、策略
 *  modes 空集＝全部适用）零命中；cause 携带目标 token 与适用清单。 */
TEST(PolCompat1, ModeNotApplicableOutsideScope)
{
    // 适用范围仅 {Preview, Quick}（§4.2.1 非空清单语义）。
    PolicyApplicability scoped;
    scoped.modes = {core::EvaluationMode::Preview, core::EvaluationMode::Quick};
    const EngineeringPolicySet policy = makePolicy(enabledCollisionRules(), scoped);

    PolicyConsumerRequirements req;
    req.mode = core::EvaluationMode::Verified;
    const PolicyCompatibility r = checkPolicyCompatibility(policy, req);
    EXPECT_EQ(r.verdict, PolicyCompatibility::Incompatible);
    ASSERT_EQ(r.reasons.size(), std::size_t{1});
    EXPECT_EQ(r.reasons[0], PolicyIncompatibilityReason::ModeNotApplicable);
    ASSERT_EQ(r.diagnostics.size(), std::size_t{1});
    expectErr01Baseline(diagAt(r, 0), policy.policyObject);
    EXPECT_EQ(diagAt(r, 0).localName, std::optional<std::string>("applicability.modes"));
    EXPECT_NE(diagAt(r, 0).cause.find("verified"), std::string::npos)
        << "目标模式 token 在 cause（core toToken 稳定词表）";
    EXPECT_NE(diagAt(r, 0).cause.find("preview"), std::string::npos)
        << "适用清单在 cause（素材面）";

    // 正例 1：目标模式在清单内 → 兼容。
    PolicyConsumerRequirements inScope;
    inScope.mode = core::EvaluationMode::Quick;
    EXPECT_EQ(checkPolicyCompatibility(policy, inScope).verdict,
              PolicyCompatibility::Compatible);
    // 正例 2：策略 modes 空集＝全部模式适用（§4.2.1）→ Verified 亦兼容。
    PolicyConsumerRequirements verified;
    verified.mode = core::EvaluationMode::Verified;
    EXPECT_EQ(checkPolicyCompatibility(basePolicy(), verified).verdict,
              PolicyCompatibility::Compatible);
}

// =====================================================================
// POL-COMPAT-2——内容身份变化拒复用（本侧供身份＋原因素材）。
// =====================================================================

/** §11 POL-COMPAT-2 行本侧观测点（CON-05/06）：阈值变化的策略＝新内容
 *  身份（切片键素材变化——上游事实）；兼容判定本身不按身份拒绝（CON-05
 *  "失效按切片内容，不按修订号"——拒复用由 evidence judgeCacheHit 在切片
 *  层执行，本判定供身份＋原因素材，越权即 PA-1 违约）。 */
TEST(PolCompat2, ContentIdentityChangeSuppliesSliceMaterial)
{
    // 同语义结构、仅安全间距不同（0.02 m → 0.03 m）的两份已发布策略；
    // 内容身份取不同打标（身份=阈值的绑定归 POL-T03 POL-ID-4——此处只
    // 消费"身份不同"这一素材事实）。
    const EngineeringPolicySet oldPolicy = makePolicy(enabledCollisionRules(0.02), {},
                                                      std::string{kNumericContractAnchorDefault},
                                                      0xAB);
    const EngineeringPolicySet newPolicy = makePolicy(enabledCollisionRules(0.03), {},
                                                      std::string{kNumericContractAnchorDefault},
                                                      0xAC);
    EXPECT_FALSE(oldPolicy.contentIdentity == newPolicy.contentIdentity)
        << "阈值变化 → 内容身份不同（切片键素材——CON-05）";

    // 身份变化本身不构成兼容失配：缺省要求对两份策略均 Compatible——
    // 判定只核对版本要素，不核对身份（越权核对＝替 evidence 行使缓存
    // 判定权，PA-1）。
    const PolicyCompatibility rOld = checkPolicyCompatibility(oldPolicy,
                                                              PolicyConsumerRequirements{});
    const PolicyCompatibility rNew = checkPolicyCompatibility(newPolicy,
                                                              PolicyConsumerRequirements{});
    EXPECT_EQ(rOld.verdict, PolicyCompatibility::Compatible);
    EXPECT_EQ(rNew.verdict, PolicyCompatibility::Compatible);
    EXPECT_TRUE(rNew.reasons.empty()) << "无版本要素失配即无原因素材";

    // 原因素材面：对同一策略提出失配要求时，原因清单全量且带诊断（消费方
    // 组装失效原因清单的供给侧——§8.2"失效原因清单"行）。
    PolicyConsumerRequirements stale;
    stale.requiresNumericContract = "appendixD@v1.17";
    const PolicyCompatibility rStale = checkPolicyCompatibility(newPolicy, stale);
    ASSERT_EQ(rStale.reasons.size(), std::size_t{1});
    EXPECT_EQ(rStale.reasons[0], PolicyIncompatibilityReason::NumericContractMismatch);
    EXPECT_FALSE(rStale.diagnostics.empty());
}

// =====================================================================
// 全量收集／确定性／注入接口——§9.5 契约表与实现口径 C-1。
// =====================================================================

/** 实现口径 C-1（NFR-COR-02 同款纪律）：五臂同时失配 → reasons 按枚举
 *  声明序（检查序）全量收集、不首错短路；诊断与原因严格同构（数目、
 *  码面、主体、定位字段按下标对齐）。 */
TEST(PolCompatAll, AllMismatchedReasonsCollectedInEnumOrder)
{
    // 策略形态：碰撞禁用＋适用范围仅 {Preview, Quick}（schema 代 1、默认锚）。
    PolicyApplicability scoped;
    scoped.modes = {core::EvaluationMode::Preview, core::EvaluationMode::Quick};
    const EngineeringPolicySet policy = makePolicy(disabledCollisionRules(), scoped);

    // 要求形态：同时触发五臂（schema 窗 [0,0]→future；要碰撞→disabled；
    // 后端版本变异→mismatch；锚变异→mismatch；Verified→not-applicable）。
    PolicyConsumerRequirements req;
    req.minSchemaVersion = 0;
    req.maxSchemaVersion = 0;
    req.requiresCollision = true;
    CollisionBackendDescriptor other = currentBackend();
    other.backendVersion += "-variant";
    req.requiresBackend = other;
    req.requiresNumericContract = "appendixD@v1.17";
    req.mode = core::EvaluationMode::Verified;

    const PolicyCompatibility r = checkPolicyCompatibility(policy, req);
    EXPECT_EQ(r.verdict, PolicyCompatibility::Incompatible);
    ASSERT_EQ(r.reasons.size(), std::size_t{5}) << "五臂全量（无 policy-invalid——发布门只产 Valid）";
    EXPECT_EQ(r.reasons[0], PolicyIncompatibilityReason::SchemaFuture);
    EXPECT_EQ(r.reasons[1], PolicyIncompatibilityReason::CollisionRequiredDisabled);
    EXPECT_EQ(r.reasons[2], PolicyIncompatibilityReason::BackendMismatch);
    EXPECT_EQ(r.reasons[3], PolicyIncompatibilityReason::NumericContractMismatch);
    EXPECT_EQ(r.reasons[4], PolicyIncompatibilityReason::ModeNotApplicable);
    ASSERT_EQ(r.diagnostics.size(), r.reasons.size()) << "每因一诊";
    for (std::size_t i = 0; i < r.diagnostics.size(); ++i) {
        expectErr01Baseline(diagAt(r, i), policy.policyObject);
    }
    EXPECT_EQ(diagAt(r, 0).localName, std::optional<std::string>("schemaVersion"));
    EXPECT_EQ(diagAt(r, 1).localName, std::optional<std::string>("collision.enabled"));
    EXPECT_EQ(diagAt(r, 2).localName, std::optional<std::string>("collisionBackend"));
    EXPECT_EQ(diagAt(r, 3).localName, std::optional<std::string>("numericContractAnchor"));
    EXPECT_EQ(diagAt(r, 4).localName, std::optional<std::string>("applicability.modes"));
}

/** §9.5"确定性"行（NFR-COR-02）：同输入重复判定 → reasons 与 diagnostics
 *  逐字节相等（诊断记录含精确等值算子——字段面全可比）。 */
TEST(PolCompatAll, DeterminismSameInputSameResult)
{
    const EngineeringPolicySet policy = basePolicy();
    PolicyConsumerRequirements req;
    req.requiresCollision = true;
    req.requiresNumericContract = "appendixD@v1.17";
    req.mode = core::EvaluationMode::Verified;

    const PolicyCompatibility a = checkPolicyCompatibility(policy, req);
    const PolicyCompatibility b = checkPolicyCompatibility(policy, req);
    EXPECT_EQ(a.reasons, b.reasons) << "原因清单逐项相等（含顺序）";
    ASSERT_EQ(a.diagnostics.size(), b.diagnostics.size());
    for (std::size_t i = 0; i < a.diagnostics.size(); ++i) {
        EXPECT_TRUE(a.diagnostics[i] == b.diagnostics[i]) << "诊断逐字节相等（含比较型三要素）";
    }
}

/** §8.1 注入形态（§9.5"无状态；接口供宿主装配"）：面向接口的消费经委托
 *  自由函数得到与直调逐字节相等的结果——接口不是第二判定权威（单一判定
 *  权威，PA-1；宿主适配器归 L5——本适配器为测试侧替身）。 */
TEST(PolCompatAll, CheckerInterfaceDelegatesToFreeFunction)
{
    // 测试侧装配适配器（L5 宿主同构形态——委托即唯一合法实现方式）。
    class DelegatingChecker final : public IPolicyCompatibilityChecker {
    public:
        PolicyCompatibility check(const EngineeringPolicySet& policy,
                                  const PolicyConsumerRequirements& requirements) const override
        {
            return checkPolicyCompatibility(policy, requirements);
        }
    };
    const DelegatingChecker checker;

    // 判据形态：需要碰撞的消费方 × 禁用碰撞的策略——实际触发失配（Incompatible），
    // 使接口面与函数面的相等断言落在非平凡结果上（空结果相等无区分度）。
    const EngineeringPolicySet policy = makePolicy(disabledCollisionRules());
    PolicyConsumerRequirements req;
    req.requiresCollision = true;
    req.mode = core::EvaluationMode::Verified;

    const PolicyCompatibility viaInterface = checker.check(policy, req);
    const PolicyCompatibility viaFreeFunction = checkPolicyCompatibility(policy, req);
    EXPECT_EQ(viaInterface.reasons, viaFreeFunction.reasons);
    ASSERT_EQ(viaInterface.diagnostics.size(), viaFreeFunction.diagnostics.size());
    for (std::size_t i = 0; i < viaInterface.diagnostics.size(); ++i) {
        EXPECT_TRUE(viaInterface.diagnostics[i] == viaFreeFunction.diagnostics[i]);
    }
    EXPECT_EQ(viaInterface.verdict, PolicyCompatibility::Incompatible);
    EXPECT_EQ(hasReason(viaInterface, PolicyIncompatibilityReason::CollisionRequiredDisabled),
              true);
}

// =====================================================================
// O-20/P-POL-5——后端版本串保守口径（acceptance 2）。
// =====================================================================

/** acceptance 2（O-20/P-POL-5）：判定所核对的后端复现要素与唯一装配线
 *  （makeRobWorkCollisionEvaluator→backend()）同源同值——backendVersion
 *  取构建版本（RW_VERSION 注入， Compatibility.cpp 与装配 TU 同宏同字面量
 *  单点），本单元**不私定最终取值**（WP-24-T01 冻结 NFR-DEP-05 基线后
 *  锁定回填，判定逻辑零变化）。机械证明：要求侧携带装配线真值 → 兼容；
 *  携带任意其他版本串 → 失配（核对对象=构建期事实，非硬编码常量）。 */
TEST(O20Ppol5, BackendVersionConservativeStance)
{
    const CollisionBackendDescriptor assembled = currentBackend();
    const EngineeringPolicySet policy = basePolicy();

    // 同源正例：要求＝装配线真值 → 零命中（判定内部推导值与装配值逐字段
    // 相等——同宏同单点的机械结果，"无第二取值源"）。
    PolicyConsumerRequirements exact;
    exact.requiresBackend = assembled;
    const PolicyCompatibility rExact = checkPolicyCompatibility(policy, exact);
    EXPECT_EQ(rExact.verdict, PolicyCompatibility::Compatible);
    EXPECT_TRUE(rExact.reasons.empty());

    // 保守口径面：backendId 冻结（§6.5 唯一注册后端标识）、backendVersion
    // 非空且来自构建（装配线口径——P-POL-5"暂取构建版本"）。
    EXPECT_EQ(assembled.backendId, "rw.proximity.builtin-rw");
    EXPECT_FALSE(assembled.backendVersion.empty());

    // 失配反例：仅版本串不同的要求 → backend-mismatch（版本串变化即拒绝
    // ——§8.2/NFR-DEP-05；回填锁定后本断言形态不变）。
    CollisionBackendDescriptor other = assembled;
    other.backendVersion += "-other";
    PolicyConsumerRequirements stale;
    stale.requiresBackend = other;
    const PolicyCompatibility rStale = checkPolicyCompatibility(policy, stale);
    EXPECT_EQ(rStale.verdict, PolicyCompatibility::Incompatible);
    ASSERT_EQ(rStale.reasons.size(), std::size_t{1});
    EXPECT_EQ(rStale.reasons[0], PolicyIncompatibilityReason::BackendMismatch);
}
