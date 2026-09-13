/**
 * @file   PolicyInputTest.cpp
 * @brief  策略编码与内容身份用例组（POL-T03）——canonical 往返、位模式、
 *         排序无关性、近似相等禁入身份与 CR-02 摘要边界。
 *
 * 设计依据：
 *   - units/policy.md §5.3（canonical 编码与内容身份——编码规则权威）、
 *     §11（POL-ID-1~5 用例行）、§12 POL-T03 行（完成条件＝canonical 往返
 *     ＋位模式＋排序无关性用例通过）、§4.2（语义闭包集合）、§4.3（无序对
 *     语义/reason 入身份）
 *   - 需求 CON-05（POL-ID-2/4/5）、CON-06（POL-ID-1）、NFR-COR-02
 *     （确定性）、附录 D 第 12 项（身份精确等值——POL-ID-5）、UX-08/
 *     KIN-12（显示单位不入身份——POL-ID-3）
 *   - 任务契约 tasks/foundation/POL-T03.json acceptance 1～3：
 *     ①POL-ID-1~5 用例通过；②近似相等禁入身份；③CR-02——contentIdentity
 *     经 core ContentDigester 摘要（不复制摘要算法、不私设第二哈希路径）
 *
 * 用例追溯命名（DTB §5.5）：用例名尾部带需求/用例组编号（POL-ID/CR-02/
 * NFR-COR-02 等），正文断言处注明验证的条款。
 *
 * 范围声明：本套件覆盖 POL-T03 落地的 codec 层契约（往返/身份/位模式/
 * 排序无关/错误路径）。POL-ID-1/2/3 的**解析管线级**观测（resolvePolicy
 * 两次解析、m/mm 输入归一只经 core convert）归 POL-T04 套件——本套件
 * 在 codec 层钉住其全部机制前提（规范化字节、显示单位不入投影、往返）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/policy/Errors.hpp>
#include <sdurws/ird/policy/PolicyInput.hpp>
#include <sdurws/ird/policy/PolicySet.hpp>

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace sdurws::ird::policy;

/// core 契约类型的短别名（测试可读性）。
namespace core = sdurws::ird::core;

namespace {

/// double π 字面量（与 PolicySet.hpp 同源——测试期望值不另引入第二常量源）。
constexpr double kPi = 3.141592653589793;

/// 生成一个有效的策略对象身份（core 保留值纪律：generate 保证非零）。
core::ObjectId validObjectId()
{
    return core::ObjectId::generate();
}

/// 生成一个有效的来源对象身份（origin.sourceObject 载荷用）。
core::ObjectId anotherObjectId()
{
    return core::ObjectId::generate();
}

/// 手工构造的有效对象身份（首字节打标——非零有效且字典序可控）。
core::ObjectId taggedObjectId(std::uint8_t tag)
{
    core::ObjectId id;
    id.bytes[0] = tag;   // 首字节标记（非全零→isValid；字典序由 tag 决定）
    return id;
}

/// 全字段填充的原始策略输入（§4.6 合法实例口径的 Raw 对偶——往返/保真
/// 用例的公共样例；字段已按编码规范序排列，满足 decode(encode(x))==x）。
RawPolicyInput fullRawInput()
{
    RawPolicyInput in;
    in.schemaVersion = PolicySchema::currentVersion;
    in.policyObject = validObjectId();

    // 碰撞规则：启用三域、安全间距 0.02（显示单位 m）、一组必检/一组排除。
    in.collision.enabled = true;
    in.collision.enabledDomains = {CollisionDomain::Self, CollisionDomain::Environment,
                                   CollisionDomain::Tool};   // 已升序（枚举序）
    in.collision.safetyClearance = RawThresholdInput{0.02, "m"};
    in.collision.excludeAdjacentLinksByDefault = true;

    // 规则对：目标按 ScopeTarget 规范键（kind 升序、载荷字典序）排列，
    // 端序按 orderedPairEnds 语义（Object<Role<Group）——decode(encode(x))==x
    // 的承载序前提（未规范化输入以"规范化后相等"另行钉住——POL_ID_2 系）。
    PairRule mandatory;
    mandatory.first = ScopeTarget::makeRole("RobotLink");
    mandatory.second = ScopeTarget::makeRole("Tool");
    mandatory.level = PolicyRuleLevel::Must;
    mandatory.reason = "夹持接触必检";
    in.collision.mandatoryPairs = {mandatory};

    PairRule excluded;
    // 端序已按 ScopeTarget 规范键摆放（Object 按 bytes 字节序——tag 1<2，
    // 解码产物承载序与样例一致，decode(encode(x))==x 逐字段成立）。
    excluded.first = ScopeTarget::makeObject(taggedObjectId(0x01));
    excluded.second = ScopeTarget::makeObject(taggedObjectId(0x02));
    excluded.level = PolicyRuleLevel::Should;
    excluded.reason = "相邻连杆重复过滤豁免";
    in.collision.excludedPairs = {excluded};

    // 阈值子模型：nearLimitRatio/conditionNumberWarning 显式、行程上限 4π。
    in.jointThresholds.nearLimitRatio = RawThresholdInput{0.05, ""};      // 无量纲（无单位 token）
    in.jointThresholds.conditionNumberWarning = RawThresholdInput{10.0, ""};
    in.jointThresholds.finiteRotationTravelLimit = RawThresholdInput{4.0 * kPi, "rad"};
    in.jointThresholds.travelLimitCheckEnabled = true;

    // 适用范围：限定模式与对象。
    in.applicability.modes = {core::EvaluationMode::Quick, core::EvaluationMode::Verified};
    in.applicability.modelObjects = {validObjectId()};
    in.applicability.taskObjects = {};
    in.applicability.caseObjects = {validObjectId()};

    // 管理与审计（入字节、不入身份）。
    in.origin.kind = PolicyOriginKind::UserEdited;
    in.origin.sourceObject = anotherObjectId();
    in.origin.note = "由模板策略修订而来";
    in.compatibilityNotes = "迁移说明：无";
    in.numericContractAnchor = "appendixD@v1.16";
    return in;
}

/// 最小原始输入（可选槽位全空、集合全空——presence 编码"缺失"面的样例）。
RawPolicyInput minimalRawInput()
{
    RawPolicyInput in;
    in.policyObject = validObjectId();
    in.collision.enabled = false;   // enabled=false 时安全间距可缺省（§4.3）
    in.numericContractAnchor = "appendixD@v1.16";
    return in;
}

/**
 * @brief 测试夹具级"解析②归一"模拟：RawThresholdInput → SI 真值阈值。
 *
 * POL-T04 落地前由夹具承担归一职责——仅覆盖本套件所需（mm→m 的等值
 * 事实）；完整换算核对（经 core convert 唯一入口）归 POL-T04 套件。
 * 来源固定 Explicit（来源标注不入身份——另有专门用例钉住）。
 */
PolicyThreshold normalizeThreshold(const RawThresholdInput& raw, PolicyThresholdDomain domain)
{
    double si = raw.value;
    if (raw.unitToken == "mm") {
        si = raw.value / 1000.0;   // 夹具级换算：20 mm ＝ 0.02 m（SI 真值等值）
    }
    return PolicyThreshold::make(si, PolicyValueOrigin::Explicit, domain);
}

/// 由 RawPolicyInput 组装语义闭包（模拟解析②~⑤归一后的形态——
/// contentIdentity 的输入契约；仅覆盖样例字段）。
struct SemanticClosure {
    std::uint32_t schemaVersion;
    CollisionRules collision;
    JointThresholds jointThresholds;
    PolicyApplicability applicability;
    std::string numericContractAnchor;
};

/// RawPolicyInput → 语义闭包（按 §4.2 语义闭包集合逐字段搬运＋归一）。
SemanticClosure toSemanticClosure(const RawPolicyInput& raw)
{
    SemanticClosure s;
    s.schemaVersion = raw.schemaVersion;
    s.collision.enabled = raw.collision.enabled;
    s.collision.enabledDomains = raw.collision.enabledDomains;
    if (raw.collision.safetyClearance.has_value()) {
        s.collision.safetyClearance
            = normalizeThreshold(*raw.collision.safetyClearance,
                                 PolicyThresholdDomain::SafetyClearance);
    }
    s.collision.excludeAdjacentLinksByDefault = raw.collision.excludeAdjacentLinksByDefault;
    s.collision.mandatoryPairs = raw.collision.mandatoryPairs;
    s.collision.excludedPairs = raw.collision.excludedPairs;

    if (raw.jointThresholds.nearLimitRatio.has_value()) {
        s.jointThresholds.nearLimitRatio
            = normalizeThreshold(*raw.jointThresholds.nearLimitRatio,
                                 PolicyThresholdDomain::NearLimitRatio);
    }
    if (raw.jointThresholds.conditionNumberWarning.has_value()) {
        s.jointThresholds.conditionNumberWarning
            = normalizeThreshold(*raw.jointThresholds.conditionNumberWarning,
                                 PolicyThresholdDomain::ConditionNumberWarning);
    }
    if (raw.jointThresholds.finiteRotationTravelLimit.has_value()) {
        s.jointThresholds.finiteRotationTravelLimit
            = normalizeThreshold(*raw.jointThresholds.finiteRotationTravelLimit,
                                 PolicyThresholdDomain::FiniteRotationTravelLimit);
    }
    s.jointThresholds.travelLimitCheckEnabled = raw.jointThresholds.travelLimitCheckEnabled;

    s.applicability = raw.applicability;
    s.numericContractAnchor = raw.numericContractAnchor;
    return s;
}

/// 语义闭包 → contentIdentity（PolicyCodec 签名的夹具展开）。
core::ContentIdentity identityOf(const SemanticClosure& s)
{
    return PolicyCodec::contentIdentity(s.schemaVersion, s.collision, s.jointThresholds,
                                        s.applicability, s.numericContractAnchor);
}

/// 语义闭包 → 语义闭包投影编码（同上——encodeSemanticProjection 展开）。
std::vector<std::uint8_t> projectionOf(const SemanticClosure& s)
{
    return PolicyCodec::encodeSemanticProjection(s.schemaVersion, s.collision,
                                                 s.jointThresholds, s.applicability,
                                                 s.numericContractAnchor);
}

/// 在字节序列中查找 ASCII 子串（白盒断言辅助——结构面钉住）。
bool containsAscii(const std::vector<std::uint8_t>& bytes, const char* needle)
{
    const std::size_t n = std::strlen(needle);
    for (std::size_t i = 0; i + n <= bytes.size(); ++i) {
        if (std::memcmp(bytes.data() + i, needle, n) == 0) {
            return true;
        }
    }
    return false;
}

}  // namespace

// =====================================================================
// acceptance 1（POL-ID-1）：canonical 往返与确定性。
// =====================================================================

/** POL-ID-1：全字段样例 decode(encode(x))==x 逐字段相等（CON-06——同
 *  对象字节重复解析必得同结果；codec 层往返是其机制前提）。 */
TEST(PolicyCodecId, RoundTripFullFieldsPreservesAll_POL_ID_1)
{
    const RawPolicyInput in = fullRawInput();
    const std::vector<std::uint8_t> bytes = PolicyCodec::encode(in);
    const RawPolicyInput out = PolicyCodec::decode(bytes);
    // 逐字段精确相等（RawPolicyInput::operator== 为承载序逐字段比较——
    // 样例已规范化，承载序往返成立）。
    EXPECT_TRUE(out == in);
    EXPECT_FALSE(out != in);
    // 再编一轮：二次编码字节与首轮逐字节一致（往返不动点——NFR-COR-02）。
    EXPECT_EQ(PolicyCodec::encode(out), bytes);
}

/** POL-ID-1：最小样例（全 optional 缺失、空集合）往返——presence 字节
 *  "缺失≠空值≠零"面（CR-02 纪律；NFR-COR-03 可选值显式编码）。 */
TEST(PolicyCodecId, RoundTripMinimalFieldsPreservesAbsence_POL_ID_1)
{
    const RawPolicyInput in = minimalRawInput();
    const std::vector<std::uint8_t> bytes = PolicyCodec::encode(in);
    const RawPolicyInput out = PolicyCodec::decode(bytes);
    EXPECT_TRUE(out == in);
    // 缺失面核对：安全间距/全部阈值槽位/兼容注记/origin.sourceObject 均
    // 保持"缺失"（而非空值/零——presence 字节语义）。
    EXPECT_FALSE(out.collision.safetyClearance.has_value());
    EXPECT_FALSE(out.jointThresholds.nearLimitRatio.has_value());
    EXPECT_FALSE(out.jointThresholds.conditionNumberWarning.has_value());
    EXPECT_FALSE(out.jointThresholds.finiteRotationTravelLimit.has_value());
    EXPECT_FALSE(out.compatibilityNotes.has_value());
    EXPECT_FALSE(out.origin.sourceObject.has_value());
}

/** POL-ID-1/NFR-COR-02：同输入两次编码同字节、两次身份计算同值（纯函数
 *  确定性——worker/main 跨进程同身份的前提）。 */
TEST(PolicyCodecId, DeterministicEncodingAndIdentity_NFR_COR_02)
{
    const RawPolicyInput in = fullRawInput();
    EXPECT_EQ(PolicyCodec::encode(in), PolicyCodec::encode(in));

    const SemanticClosure s = toSemanticClosure(fullRawInput());
    const core::ContentIdentity a = identityOf(s);
    const core::ContentIdentity b = identityOf(s);
    EXPECT_EQ(a, b);
    EXPECT_TRUE(a.isValid());   // 身份非全零（发布对象必有语义身份——CON-05）
}

// =====================================================================
// acceptance 1（POL-ID-2）：排序无关性（编码规范序）。
// =====================================================================

/** POL-ID-2（CON-05）：规则清单输入顺序打乱→同字节→同身份（编码按规范
 *  序重排——"输入字段排序变化不改变语义身份"，§5.3）。 */
TEST(PolicyCodecId, RuleListOrderInvariance_YieldsIdenticalBytesAndIdentity_POL_ID_2)
{
    // 构造三条规范化后互异的规则（Object/Role/Group 目标混合——排序键
    // 三类覆盖）。
    PairRule r1;
    r1.first = ScopeTarget::makeObject(validObjectId());
    r1.second = ScopeTarget::makeObject(validObjectId());
    r1.level = PolicyRuleLevel::Must;
    r1.reason = "rule-one";
    PairRule r2;
    r2.first = ScopeTarget::makeRole("Payload");
    r2.second = ScopeTarget::makeRole("Workpiece");
    r2.level = PolicyRuleLevel::Should;
    r2.reason = "rule-two";
    PairRule r3;
    r3.first = ScopeTarget::makeGroup("grpA");
    r3.second = ScopeTarget::makeRole("Tool");
    r3.level = PolicyRuleLevel::Must;
    r3.reason = "rule-three";

    RawPolicyInput a = fullRawInput();
    a.collision.mandatoryPairs = {r1, r2, r3};
    a.collision.excludedPairs = {};
    RawPolicyInput b = a;
    b.collision.mandatoryPairs = {r3, r1, r2};   // 仅承载序不同——语义集合相同

    // 承载序不同（operator== 按承载序——不承担无序化语义）。
    EXPECT_FALSE(a.collision.mandatoryPairs == b.collision.mandatoryPairs);
    // 但编码字节与语义身份逐字节相同（规范化排序——POL-ID-2 本体）。
    EXPECT_EQ(PolicyCodec::encode(a), PolicyCodec::encode(b));
    EXPECT_EQ(identityOf(toSemanticClosure(a)), identityOf(toSemanticClosure(b)));
}

/** POL-ID-2（§4.3 无序对语义）：{first,second} 与 {second,first} 同一对
 *  ——两端交换后同字节（规范化摆放）。 */
TEST(PolicyCodecId, UnorderedPairEndsInvariance_POL_ID_2)
{
    PairRule rule;
    rule.first = ScopeTarget::makeRole("Tool");
    rule.second = ScopeTarget::makeRole("Workpiece");
    rule.level = PolicyRuleLevel::Must;
    rule.reason = "tool-workpiece";

    RawPolicyInput a = minimalRawInput();
    a.collision.mandatoryPairs = {rule};
    RawPolicyInput b = a;
    b.collision.mandatoryPairs[0].first = rule.second;    // 端序交换
    b.collision.mandatoryPairs[0].second = rule.first;

    // 同一对（交换前后）编码同字节、身份相同（无序对语义进编码规范序）。
    EXPECT_EQ(PolicyCodec::encode(a), PolicyCodec::encode(b));
    EXPECT_EQ(identityOf(toSemanticClosure(a)), identityOf(toSemanticClosure(b)));
    // 往返产物为规范化端序（解码→再编码不动点）。
    const RawPolicyInput outA = PolicyCodec::decode(PolicyCodec::encode(a));
    const RawPolicyInput outB = PolicyCodec::decode(PolicyCodec::encode(b));
    EXPECT_TRUE(outA == outB);
}

/** POL-ID-2（CON-05）：集合字段（启用域/适用模式/对象清单）乱序＋重复
 *  →同身份（集合语义规范化：升序去重）。 */
TEST(PolicyCodecId, SetOrderAndDuplicateInvariance_POL_ID_2)
{
    RawPolicyInput a = fullRawInput();
    a.collision.enabledDomains = {CollisionDomain::Self, CollisionDomain::Environment,
                                  CollisionDomain::Tool};
    RawPolicyInput b = a;
    b.collision.enabledDomains = {CollisionDomain::Tool, CollisionDomain::Self,
                                  CollisionDomain::Environment};   // 乱序
    EXPECT_EQ(identityOf(toSemanticClosure(a)), identityOf(toSemanticClosure(b)));

    // 对象清单乱序＋重复条目（重复＝同集合语义——去重后同字节）。
    a.applicability.modelObjects = {validObjectId(), validObjectId(), validObjectId()};
    b = a;
    b.applicability.modelObjects = {a.applicability.modelObjects[2],
                                    a.applicability.modelObjects[0],
                                    a.applicability.modelObjects[1],
                                    a.applicability.modelObjects[0]};   // 乱序＋重复
    const SemanticClosure sa = toSemanticClosure(a);
    const SemanticClosure sb = toSemanticClosure(b);
    EXPECT_EQ(projectionOf(sa), projectionOf(sb));
    EXPECT_EQ(identityOf(sa), identityOf(sb));
    // full 编码同样规范化（乱序输入同字节）。
    EXPECT_EQ(PolicyCodec::encode(a), PolicyCodec::encode(b));
}

// =====================================================================
// acceptance 1（POL-ID-3）：显示单位不入身份。
// =====================================================================

/** POL-ID-3（UX-08/KIN-12）：等值不同显示单位（0.02 m 与 20 mm）→归一后
 *  同 SI 真值→同身份；语义闭包投影编码结构性地不含显示单位 token
 *  （full 编码保真 token、投影不含——POL-ID-3 的机制层钉住）。
 *  注：归一只经 core convert 的解析级断言归 POL-T04——本用例以夹具级
 *  等值换算模拟解析②产物。 */
TEST(PolicyCodecId, DisplayUnitNotInIdentity_POL_ID_3)
{
    // 两个 RawPolicyInput：仅安全间距的显示单位与字面值不同（0.02 m 对
    // 20 mm——SI 真值等值），其余字段全同。
    RawPolicyInput rawMeter = fullRawInput();
    rawMeter.collision.safetyClearance = RawThresholdInput{0.02, "m"};
    RawPolicyInput rawMilli = rawMeter;
    rawMilli.collision.safetyClearance = RawThresholdInput{20.0, "mm"};

    // full 编码保真各自的显示单位（往返载体——decode 可还原 token）。
    const RawPolicyInput outMeter = PolicyCodec::decode(PolicyCodec::encode(rawMeter));
    const RawPolicyInput outMilli = PolicyCodec::decode(PolicyCodec::encode(rawMilli));
    ASSERT_TRUE(outMeter.collision.safetyClearance.has_value());
    ASSERT_TRUE(outMilli.collision.safetyClearance.has_value());
    EXPECT_EQ(outMeter.collision.safetyClearance->unitToken, "m");
    EXPECT_EQ(outMilli.collision.safetyClearance->unitToken, "mm");

    // 归一后 SI 真值相等（夹具级换算自洽核对——20/1000 与 0.02 同一
    // double，IEEE754 除法正确舍入）。
    const SemanticClosure sMeter = toSemanticClosure(rawMeter);
    const SemanticClosure sMilli = toSemanticClosure(rawMilli);
    ASSERT_TRUE(sMeter.collision.safetyClearance.has_value());
    ASSERT_TRUE(sMilli.collision.safetyClearance.has_value());
    EXPECT_EQ(sMeter.collision.safetyClearance->siValue(),
              sMilli.collision.safetyClearance->siValue());

    // 同 SI 语义闭包→同投影字节→同身份（显示单位不影响身份——本体断言）。
    EXPECT_EQ(projectionOf(sMeter), projectionOf(sMilli));
    EXPECT_EQ(identityOf(sMeter), identityOf(sMilli));

    // 结构面钉住：full 编码含 "mm" token 字节，语义投影不含任何单位 token
    // 样例字节（投影无单位槽位——POL-ID-3 的字节布局保证）。
    const std::vector<std::uint8_t> fullBytes = PolicyCodec::encode(rawMilli);
    const std::vector<std::uint8_t> projBytes = projectionOf(sMilli);
    EXPECT_TRUE(containsAscii(fullBytes, "mm"));
    EXPECT_FALSE(containsAscii(projBytes, "mm"));
    EXPECT_FALSE(containsAscii(projBytes, "rad"));
}

// =====================================================================
// acceptance 1（POL-ID-4/5）＋acceptance 2：阈值变化产生新身份；近似
// 相等禁入身份。
// =====================================================================

/** POL-ID-4（CON-05/06）：安全间距 0.02→0.03→身份不等（任何语义字段
 *  变化产生新身份——切片失效与缓存不命中的机制前提，§8.6）。 */
TEST(PolicyCodecId, ThresholdChangeYieldsNewIdentity_POL_ID_4)
{
    const SemanticClosure base = toSemanticClosure(fullRawInput());
    SemanticClosure changed = base;
    changed.collision.safetyClearance
        = PolicyThreshold::make(0.03, PolicyValueOrigin::Explicit,
                                PolicyThresholdDomain::SafetyClearance);
    EXPECT_NE(identityOf(base), identityOf(changed));
    EXPECT_NE(projectionOf(base), projectionOf(changed));   // 字节面同步差异
}

/** POL-ID-5（附录 D 第 12 项/acceptance 2）：阈值相差 1e-15 与 1 ulp——
 *  身份**不等**（位模式编码；浮点近似相等禁作身份键——近似关系无传递
 *  性，容差比较只归 core closeWithin 的数值校验面，永不进入身份）。 */
TEST(PolicyCodecId, NearEqualThresholdsDistinctIdentities_POL_ID_5)
{
    const double base = 0.02;
    // 1e-15 级差异（§11 POL-ID-5 原文例：阈值相差 1e-15）。
    const double epsShift = std::nextafter(base, 1.0) - base;   // 1 ulp 步长
    ASSERT_GT(epsShift, 0.0);
    ASSERT_LT(epsShift, 1e-15);   // 0.02 的 1 ulp ≈ 3.5e-18——远小于 1e-15

    SemanticClosure a = toSemanticClosure(fullRawInput());
    a.collision.safetyClearance
        = PolicyThreshold::make(base, PolicyValueOrigin::Explicit,
                                PolicyThresholdDomain::SafetyClearance);
    // 1 ulp 差异：身份必不等（位模式不同→投影字节不同）。
    SemanticClosure b = a;
    b.collision.safetyClearance
        = PolicyThreshold::make(std::nextafter(base, 1.0), PolicyValueOrigin::Explicit,
                                PolicyThresholdDomain::SafetyClearance);
    EXPECT_NE(a.collision.safetyClearance->siValue(), b.collision.safetyClearance->siValue());
    EXPECT_NE(identityOf(a), identityOf(b));

    // 1e-15 差异（两条不同 double——非有限精度巧合相等）。
    SemanticClosure c = a;
    c.collision.safetyClearance
        = PolicyThreshold::make(base + 1e-15, PolicyValueOrigin::Explicit,
                                PolicyThresholdDomain::SafetyClearance);
    EXPECT_NE(identityOf(a), identityOf(c));
}

// =====================================================================
// 位模式原语（acceptance 1"位模式"面＋acceptance 2 机制层）。
// =====================================================================

/** §5.3 浮点行：有限值位模式 round-trip 精确（0/−0、极值、次正规、
 *  随机位型——同值同字节是 POL-ID-5 的机制前提）。 */
TEST(PolicyCodecFloat, CanonicalF64RoundTripPreservesExactBits)
{
    // 边界样例集：零（±0 位型分别保留——符号位参与身份语义）、极值、
    // 最小次正规、最大有限负值。
    const double samples[] = {
        0.0,
        -0.0,                                   // 符号位保留（−0 ≠ +0 位型）
        std::numeric_limits<double>::min(),     // 最小正规
        -std::numeric_limits<double>::min(),
        std::numeric_limits<double>::max(),
        -std::numeric_limits<double>::max(),
        std::numeric_limits<double>::denorm_min(),   // 最小次正规
        4.0 * kPi,
        0.02,
        -1e-15,
    };
    for (const double v : samples) {
        const std::array<std::uint8_t, 8> bits = PolicyCodec::canonicalF64(v);
        const double back = PolicyCodec::parseCanonicalF64(bits.data());
        // 位级往返（memcmp 而非 ==——±0 的 == 相等但位型不同）。
        double vBits = 0.0;
        std::memcpy(&vBits, &v, sizeof(vBits));
        std::uint64_t vRaw = 0;
        std::memcpy(&vRaw, &vBits, sizeof(vRaw));
        std::uint64_t bRaw = 0;
        for (int i = 0; i < 8; ++i) {
            bRaw = (bRaw << 8) | bits[static_cast<std::size_t>(i)];
        }
        EXPECT_EQ(bRaw, vRaw) << "位模式漂移（样本 " << v << "）";
        EXPECT_EQ(back, v) << "数值往返漂移";
    }
}

/** §5.3/CR-02 浮点纪律：NaN/±Inf 编码入口拒绝（ThresholdNonFinite——
 *  非有限值无稳定位模式语义）。 */
TEST(PolicyCodecFloat, CanonicalF64RejectsNonFiniteAtEncodeEntry)
{
    const double nonFinite[] = {
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::signaling_NaN(),
        std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
    };
    for (const double v : nonFinite) {
        EXPECT_THROW((void)PolicyCodec::canonicalF64(v), PolicyError);
        try {
            (void)PolicyCodec::canonicalF64(v);
            FAIL() << "非有限值应被拒绝";
        } catch (const PolicyError& e) {
            // 码面核对：编码入口拒绝走 §5.2 行 3 同码（ThresholdNonFinite）。
            EXPECT_EQ(e.code(), PolicyErrorCode::ThresholdNonFinite);
        }
    }
}

/** 双端纪律接收端：非有限位型（NaN/±Inf/信号 NaN 载荷位）→
 *  EncodingInvalid（手工构造/传输损坏在此暴露，不静默进入身份）。 */
TEST(PolicyCodecFloat, ParseCanonicalF64RejectsNonFiniteBitPatterns)
{
    // 手工构造非有限位型：quiet NaN（0x7FF8…）、负 NaN（0xFFF8…）、
    // +Inf（0x7FF0…0000）、信号 NaN（载荷位非零）。
    const std::uint64_t nonFiniteBits[] = {
        0x7FF8000000000000ull,
        0xFFF8000000000000ull,
        0x7FF0000000000000ull,
        0xFFF0000000000000ull,
        0x7FF0000000000001ull,   // 信号 NaN（载荷位）
    };
    for (const std::uint64_t bits : nonFiniteBits) {
        std::array<std::uint8_t, 8> bytes{};
        for (int i = 0; i < 8; ++i) {
            bytes[static_cast<std::size_t>(i)]
                = static_cast<std::uint8_t>(bits >> (56 - 8 * i));
        }
        try {
            (void)PolicyCodec::parseCanonicalF64(bytes.data());
            FAIL() << "非有限位型应被拒绝";
        } catch (const PolicyError& e) {
            // 码面核对：接收端违约＝解码期字节契约违约（与编码入口的
            // ThresholdNonFinite 区分——见 parseCanonicalF64 契约）。
            EXPECT_EQ(e.code(), PolicyErrorCode::EncodingInvalid);
        }
    }
}

/** acceptance 2 管线级钉：仅差 1 ulp 的两策略——编码字节不同＋身份不同
 *  （容差不入身份的端到端证据；POL_ID_5 的 full 形态对偶）。 */
TEST(PolicyCodecFloat, OneUlpThresholdDifferenceChangesBytesAndIdentity_Acceptance2)
{
    const RawPolicyInput a = fullRawInput();
    RawPolicyInput b = a;
    ASSERT_TRUE(b.jointThresholds.nearLimitRatio.has_value());
    b.jointThresholds.nearLimitRatio
        = RawThresholdInput{std::nextafter(0.05, 1.0), ""};   // 1 ulp 上移

    EXPECT_NE(PolicyCodec::encode(a), PolicyCodec::encode(b));   // 字节面
    EXPECT_NE(identityOf(toSemanticClosure(a)), identityOf(toSemanticClosure(b)));   // 身份面
}

// =====================================================================
// acceptance 3：CR-02 处置约束——摘要唯一经 core ContentDigester；
// 排除字段不入身份。
// =====================================================================

/** CR-02（acceptance 3）：contentIdentity ＝ core ContentDigester 对语义
 *  闭包投影编码的 SHA-256——手工重算钉住摘要单点（evidence
 *  DigestViaCoreContentDigesterOnly_CR_02 同模式：本单元无第二哈希路径）。 */
TEST(PolicyCodecCr02, DigestViaCoreContentDigesterOnly_CR_02)
{
    const SemanticClosure s = toSemanticClosure(fullRawInput());
    const std::vector<std::uint8_t> projection = projectionOf(s);

    // 手工重算：同一投影字节经 core::ContentDigester（SHA-256）直接摘要。
    core::ContentDigester digester;
    digester.update(projection.data(), projection.size());
    core::ContentIdentity expected;
    expected.bytes = digester.finalize();

    // identityOf(s) 必须与手工重算逐字节一致——否则存在第二摘要路径。
    EXPECT_EQ(identityOf(s), expected);
}

/** CR-02（foundation-api-diff.md §CR-02 排除字段登记）：policyObject/
 *  origin/兼容注记变化→full 编码变化（往返保真面）但语义闭包投影与
 *  身份不变（排除字段结构性不入身份——仅语义闭包字段入身份）。 */
TEST(PolicyCodecCr02, ExcludedFieldsPinned_CR_02)
{
    // 同语义闭包、不同管理/审计字段的两个 RawPolicyInput。
    RawPolicyInput a = fullRawInput();
    RawPolicyInput b = a;
    b.policyObject = validObjectId();                       // 对象身份不同
    b.origin.kind = PolicyOriginKind::Imported;             // 来源类别不同
    b.origin.sourceObject = std::nullopt;                   // 来源对象缺失
    b.origin.note = "另一次导入";                            // 审计备注不同
    b.compatibilityNotes = "不同迁移说明";                    // 兼容注记不同

    // full 编码不同（这些字段入对象字节——decode 可还原，§5.1 管线入口）。
    EXPECT_NE(PolicyCodec::encode(a), PolicyCodec::encode(b));
    // 语义闭包投影相同→身份相同（排除字段不入身份——CR-02 本体）。
    const SemanticClosure sa = toSemanticClosure(a);
    const SemanticClosure sb = toSemanticClosure(b);
    EXPECT_EQ(projectionOf(sa), projectionOf(sb));
    EXPECT_EQ(identityOf(sa), identityOf(sb));
}

/** §4.1"来源标注不影响身份"的实现口径钉：同数值不同阈值来源（Explicit
 *  对 DefaultAppendixD）→同身份（评估行为仅消费数值——CON-05 失效判据
 *  语义正确方向；实现口径随 policy.md v0.4 登记）。 */
TEST(PolicyCodecCr02, ThresholdValueOriginNotInIdentity)
{
    // 同数值（4π）、不同来源标注：a＝DefaultAppendixD（解析③默认填入）、
    // b＝Explicit（用户显式输入同值）。
    SemanticClosure a = toSemanticClosure(fullRawInput());
    a.jointThresholds.finiteRotationTravelLimit
        = PolicyThreshold::make(4.0 * kPi, PolicyValueOrigin::DefaultAppendixD,
                                PolicyThresholdDomain::FiniteRotationTravelLimit);
    SemanticClosure b = a;
    b.jointThresholds.finiteRotationTravelLimit
        = PolicyThreshold::make(4.0 * kPi, PolicyValueOrigin::Explicit,
                                PolicyThresholdDomain::FiniteRotationTravelLimit);
    EXPECT_NE(a.jointThresholds.finiteRotationTravelLimit.origin(),
              b.jointThresholds.finiteRotationTravelLimit.origin());
    EXPECT_EQ(identityOf(a), identityOf(b));
}

// =====================================================================
// 错误路径（encode fail-fast＋decode 字节契约）。
// =====================================================================

/** §5.3/CR-02 编码入口：非有限阈值（NaN/±Inf）→ThresholdNonFinite
 *  （无效值可驻留 RawPolicyInput 供解析诊断保留原文，但无效策略不产生
 *  修订〔SA-15〕——要求持久化即契约违约）。 */
TEST(PolicyCodecErrors, EncodeRejectsNonFiniteThresholds)
{
    RawPolicyInput in = fullRawInput();
    in.collision.safetyClearance = RawThresholdInput{std::numeric_limits<double>::quiet_NaN(), "m"};
    try {
        (void)PolicyCodec::encode(in);
        FAIL() << "NaN 阈值应被编码入口拒绝";
    } catch (const PolicyError& e) {
        EXPECT_EQ(e.code(), PolicyErrorCode::ThresholdNonFinite);
    }

    RawPolicyInput inf = fullRawInput();
    inf.jointThresholds.finiteRotationTravelLimit
        = RawThresholdInput{std::numeric_limits<double>::infinity(), "rad"};
    EXPECT_THROW((void)PolicyCodec::encode(inf), PolicyError);
}

/** encode fail-fast 面：无效 policyObject/未来与未知 schema 代/空锚/
 *  空 reason 规则（调用方契约违约——逐项码面核对）。 */
TEST(PolicyCodecErrors, EncodeRejectsContractViolations)
{
    // 无效对象身份（全零保留值）。
    RawPolicyInput badObject = minimalRawInput();
    badObject.policyObject = core::ObjectId{};   // 全零
    try {
        (void)PolicyCodec::encode(badObject);
        FAIL() << "无效 policyObject 应被拒绝";
    } catch (const PolicyError& e) {
        EXPECT_EQ(e.code(), PolicyErrorCode::PolicyObjectInvalid);
    }
    // 未来代（>当前——PM-06 同款码面）。
    RawPolicyInput future = minimalRawInput();
    future.schemaVersion = PolicySchema::currentVersion + 1;
    try {
        (void)PolicyCodec::encode(future);
        FAIL() << "未来 schema 代应被拒绝";
    } catch (const PolicyError& e) {
        EXPECT_EQ(e.code(), PolicyErrorCode::SchemaVersionFuture);
    }
    // 未知旧代（<当前）。
    RawPolicyInput unknown = minimalRawInput();
    unknown.schemaVersion = PolicySchema::currentVersion - 1;
    try {
        (void)PolicyCodec::encode(unknown);
        FAIL() << "未知 schema 代应被拒绝";
    } catch (const PolicyError& e) {
        EXPECT_EQ(e.code(), PolicyErrorCode::SchemaVersionUnknown);
    }
    // 空数值契约锚（§4.2 必填列）。
    RawPolicyInput noAnchor = minimalRawInput();
    noAnchor.numericContractAnchor.clear();
    try {
        (void)PolicyCodec::encode(noAnchor);
        FAIL() << "空锚应被拒绝";
    } catch (const PolicyError& e) {
        EXPECT_EQ(e.code(), PolicyErrorCode::PolicyObjectInvalid);
    }
    // 规则 reason 空（§4.3 非空必填——detail 谓词复用面）。
    RawPolicyInput noReason = minimalRawInput();
    PairRule bad;
    bad.first = ScopeTarget::makeRole("Tool");
    bad.second = ScopeTarget::makeRole("Workpiece");
    bad.level = PolicyRuleLevel::Must;
    bad.reason = "";   // 违约
    noReason.collision.mandatoryPairs = {bad};
    try {
        (void)PolicyCodec::encode(noReason);
        FAIL() << "空 reason 规则应被拒绝";
    } catch (const PolicyError& e) {
        EXPECT_EQ(e.code(), PolicyErrorCode::PolicyObjectInvalid);
    }
}

/** decode 帧级违约：错 magic/非 full 形态（语义投影拒绝——非往返载体）/
 *  未来与未知版本代。 */
TEST(PolicyCodecErrors, DecodeRejectsWrongMagicFormAndVersions)
{
    const std::vector<std::uint8_t> good = PolicyCodec::encode(minimalRawInput());

    // 错 magic（异种编码混入——CR-02 magic 互异登记的解码面）。
    std::vector<std::uint8_t> badMagic = good;
    badMagic[0] = 'X';
    try {
        (void)PolicyCodec::decode(badMagic);
        FAIL() << "错 magic 应被拒绝";
    } catch (const PolicyError& e) {
        EXPECT_EQ(e.code(), PolicyErrorCode::EncodingInvalid);
    }

    // 语义闭包投影形态（0x01）拒绝——非往返载体（evidence
    // baseline-projection 同款）。
    const SemanticClosure s = toSemanticClosure(minimalRawInput());
    std::vector<std::uint8_t> projection = projectionOf(s);
    ASSERT_EQ(projection[7], PolicySchema::formSemanticProjection);
    try {
        (void)PolicyCodec::decode(projection);
        FAIL() << "语义投影编码应被 decode 拒绝";
    } catch (const PolicyError& e) {
        EXPECT_EQ(e.code(), PolicyErrorCode::EncodingInvalid);
    }

    // 版本代违约（篡改字节内 schemaVersion）。
    std::vector<std::uint8_t> future = good;
    future[8] = 0x00;
    future[9] = 0x00;
    future[10] = 0x00;
    future[11] = static_cast<std::uint8_t>(PolicySchema::currentVersion + 1);
    try {
        (void)PolicyCodec::decode(future);
        FAIL() << "未来版本代应被拒绝";
    } catch (const PolicyError& e) {
        EXPECT_EQ(e.code(), PolicyErrorCode::SchemaVersionFuture);
    }
    std::vector<std::uint8_t> unknown = good;
    unknown[11] = 0x00;   // 当前代 1→0
    try {
        (void)PolicyCodec::decode(unknown);
        FAIL() << "未知版本代应被拒绝";
    } catch (const PolicyError& e) {
        EXPECT_EQ(e.code(), PolicyErrorCode::SchemaVersionUnknown);
    }
}

/** decode 长度契约：截断/尾部垃圾/载荷长度声明不符（长度前缀纪律的
 *  解码面）。 */
TEST(PolicyCodecErrors, DecodeRejectsTruncatedAndPaddedBytes)
{
    const std::vector<std::uint8_t> good = PolicyCodec::encode(fullRawInput());

    // 截断（去尾 1 字节——载荷长度声明大于实际）。
    std::vector<std::uint8_t> truncated(good.begin(), good.end() - 1);
    try {
        (void)PolicyCodec::decode(truncated);
        FAIL() << "截断字节应被拒绝";
    } catch (const PolicyError& e) {
        EXPECT_EQ(e.code(), PolicyErrorCode::EncodingInvalid);
    }

    // 尾部垃圾（追加 1 字节——载荷长度声明小于实际）。
    std::vector<std::uint8_t> padded = good;
    padded.push_back(0x00);
    try {
        (void)PolicyCodec::decode(padded);
        FAIL() << "尾部垃圾应被拒绝";
    } catch (const PolicyError& e) {
        EXPECT_EQ(e.code(), PolicyErrorCode::EncodingInvalid);
    }

    // 载荷长度字段篡改（+1——与实际不符）。
    std::vector<std::uint8_t> lenBumped = good;
    lenBumped[15] = static_cast<std::uint8_t>(lenBumped[15] + 1);
    try {
        (void)PolicyCodec::decode(lenBumped);
        FAIL() << "载荷长度声明不符应被拒绝";
    } catch (const PolicyError& e) {
        EXPECT_EQ(e.code(), PolicyErrorCode::EncodingInvalid);
    }
}

/** decode 载荷级违约：未知枚举值与非法 presence 字节（手工等长替换——
 *  不依赖编码器产出路径；未知值＝损坏或未来格式，不得静默接受）。
 *  定位方式＝minimal 样例的确定性布局偏移＋布局锚断言（漂移即断言失败，
 *  不做字节扫描——扫描在随机身份字节上有误匹配风险）。 */
TEST(PolicyCodecErrors, DecodeRejectsUnknownEnumAndBadPresence)
{
    // minimal 样例载荷布局（encode 字段序决定，帧头 16 字节后）：
    //   [16..31] policyObject(16)｜[32] origin.kind(2)｜[33] sourceObject
    //   presence(0)｜[34] note presence(0)｜[35] compat presence(0)｜
    //   [36..39] anchor len(15)｜[40..54] anchor｜[55] enabled(0)｜
    //   [56..59] domains count｜[60] 域条目…
    // ——未知域枚举注入：构造恰含一个域条目的样例（Scene＝合法值 3），
    // 等长替换 [60] 为 0x7F（越表）。
    RawPolicyInput oneDomain = minimalRawInput();
    oneDomain.collision.enabledDomains = {CollisionDomain::Scene};
    std::vector<std::uint8_t> oneDomainBytes = PolicyCodec::encode(oneDomain);
    // 布局锚：count(1) 的 u32 大端与条目值 3（布局漂移在此显式失败）。
    ASSERT_EQ(oneDomainBytes[56], 0x00);
    ASSERT_EQ(oneDomainBytes[57], 0x00);
    ASSERT_EQ(oneDomainBytes[58], 0x00);
    ASSERT_EQ(oneDomainBytes[59], 0x01);
    ASSERT_EQ(oneDomainBytes[60], 0x03);
    oneDomainBytes[60] = 0x7F;   // 合法域 3 → 越表值
    try {
        (void)PolicyCodec::decode(oneDomainBytes);
        FAIL() << "未知域枚举值应被拒绝";
    } catch (const PolicyError& e) {
        EXPECT_EQ(e.code(), PolicyErrorCode::EncodingInvalid);
    }

    // 非法 presence 字节（0xFF——presence 仅允许 0/1）：minimal 样例
    // 载荷布局固定，载荷起点 16 处依次为 policyObject(16 字节)〔[16..31]〕、
    // origin.kind〔[32]〕、sourceObject presence〔[33]＝0〕——等长替换
    // presence 字节为 0xFF。
    std::vector<std::uint8_t> badPresence = PolicyCodec::encode(minimalRawInput());
    ASSERT_EQ(badPresence[32], 0x02);   // origin.kind＝UserEdited（枚举值 2）——布局锚
    ASSERT_EQ(badPresence[33], 0x00);   // sourceObject presence＝缺失
    badPresence[33] = 0xFF;
    try {
        (void)PolicyCodec::decode(badPresence);
        FAIL() << "非法 presence 字节应被拒绝";
    } catch (const PolicyError& e) {
        EXPECT_EQ(e.code(), PolicyErrorCode::EncodingInvalid);
    }
}

/** decode 载荷级违约：非有限位模式注入（f64 槽位写 NaN 位型——接收端
 *  双端纪律的解码面）＋空 reason 规则（§4.3 必填的解码面）。 */
TEST(PolicyCodecErrors, DecodeRejectsNonFiniteBitPatternAndEmptyReason)
{
    // 非有限位模式注入：minimal＋安全间距样例，[60] clearance presence(1)、
    // [61..68] f64 0.02（大端首字节 0x3F 0x94）——布局锚后等长替换为
    // quiet NaN 位型（0x7FF8…）。
    RawPolicyInput withClearance = minimalRawInput();
    withClearance.collision.safetyClearance = RawThresholdInput{0.02, "m"};
    std::vector<std::uint8_t> bytes = PolicyCodec::encode(withClearance);
    static const std::uint8_t kNaNPattern[8] = {0x7F, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    ASSERT_EQ(bytes[60], 0x01);   // clearance presence＝存在——布局锚
    ASSERT_EQ(bytes[61], 0x3F);   // 0.02 ＝ 0x3F947AE147AE147B（大端首字节）
    ASSERT_EQ(bytes[62], 0x94);
    std::memcpy(bytes.data() + 61, kNaNPattern, 8);
    try {
        (void)PolicyCodec::decode(bytes);
        FAIL() << "非有限位模式应被接收端拒绝";
    } catch (const PolicyError& e) {
        EXPECT_EQ(e.code(), PolicyErrorCode::EncodingInvalid);
    }

    // 空 reason 注入：minimal＋一条规则（Role AAA/BBB、Must、reason "xy"），
    // reason 长度前缀（[83..86]＝u32 2）等长替换为 0——readPairRule 在
    // 尾部精确耗尽检查之前先命中"reason 非空必填"契约（§4.3）。
    RawPolicyInput withRule = minimalRawInput();
    PairRule rule;
    rule.first = ScopeTarget::makeRole("AAA");
    rule.second = ScopeTarget::makeRole("BBB");
    rule.level = PolicyRuleLevel::Must;
    rule.reason = "xy";
    withRule.collision.mandatoryPairs = {rule};
    std::vector<std::uint8_t> ruleBytes = PolicyCodec::encode(withRule);
    // 布局锚：reason len 字段前的 level 字节（Must＝0）与 reason 内容。
    ASSERT_EQ(ruleBytes[82], 0x00);           // level＝Must——布局锚
    ASSERT_EQ(ruleBytes[83], 0x00);           // reason len u32 大端＝2
    ASSERT_EQ(ruleBytes[84], 0x00);
    ASSERT_EQ(ruleBytes[85], 0x00);
    ASSERT_EQ(ruleBytes[86], 0x02);
    ASSERT_EQ(ruleBytes[87], 'x');
    ASSERT_EQ(ruleBytes[88], 'y');
    ruleBytes[86] = 0x00;                     // len 2 → 0（内容字节成为越界尾部）
    try {
        (void)PolicyCodec::decode(ruleBytes);
        FAIL() << "空 reason 规则应被拒绝";
    } catch (const PolicyError& e) {
        EXPECT_EQ(e.code(), PolicyErrorCode::EncodingInvalid);
    }
}

/** UTF-8 与单位 token 往返保真（多字节中文/长 token 直载无转码——字节
 *  级无损是"字节对 project 不透明"往返语义的一部分）。 */
TEST(PolicyCodecErrors, RoundTripPreservesUtf8AndUnitTokens)
{
    RawPolicyInput in = fullRawInput();
    in.collision.mandatoryPairs[0].reason = "夹持接触必检——工具/工件接触对（7.4 边界 d≥m）";
    in.origin.note = "备注含单位符号 m、rad 与标点（·）";
    in.collision.safetyClearance = RawThresholdInput{25.4, "mm"};   // 英制毫米字面值
    const RawPolicyInput out = PolicyCodec::decode(PolicyCodec::encode(in));
    EXPECT_TRUE(out == in);
    ASSERT_TRUE(out.collision.mandatoryPairs.size() == 1);
    EXPECT_EQ(out.collision.mandatoryPairs[0].reason, in.collision.mandatoryPairs[0].reason);
    ASSERT_TRUE(out.collision.safetyClearance.has_value());
    EXPECT_EQ(out.collision.safetyClearance->unitToken, "mm");
    EXPECT_EQ(out.collision.safetyClearance->value, 25.4);
}
