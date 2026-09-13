/**
 * @file   JointLimitsTest.cpp
 * @brief  关节限位与行程阈值评估用例组（POL-T08）——POL-JNT-1/2：
 *         行程上限 4π 边界（T＝L 不超限/T＞L 超限）、比较型三要素、
 *         continuous 豁免、P-POL-2 显式 NotApplicable（不伪造数值）、
 *         区间/工程范围非法诊断级检出（D-13 不阻断）、近限位与闭区间
 *         边界、逐构型逐关节裕量口径、查询违约矩阵、确定性与稳定排序。
 *
 * 设计依据：
 *   - units/policy.md §11（POL-JNT-1 用例行：行程=4π 边界/＞4π/continuous
 *     关节——边界不超限、超限输出比较型三要素、continuous 豁免；
 *     POL-JNT-2 用例行：阈值 nullopt/qmin≥qmax/近限位边界——NotApplicable
 *     标记、区间非法诊断级发现＋跳过、裕量按 §7.4 边界、"不伪造数值"
 *     观测点）、§7.4（行程上限行/近限位比行/限位违例行——边界值决策表）、
 *     §9.4（IJointLimitEvaluator 契约与边界段——D-13 判定权归命令处理器）、
 *     §12 POL-T08 行（验证方式＝POL-JNT-1/2；完成条件＝行程上限边界〔4π〕
 *     与比较型三要素用例通过）
 *   - 需求 MDL-06④（行程上限策略校验默认 4π——附录 D 第 11 项唯一冻结
 *     默认；比较型诊断＋显式确认放行——本套件钉住比较型供给侧）、
 *     MDL-12（continuous 工程工作范围有限性）、KIN-13（阈值随策略传递）、
 *     D-13/SA-15（检出不阻断；比较型结果供 ConfirmableFinding）、
 *     AT-01（§10.4 多圈关节改阈值后正常放行——6π 显式阈值反例）、
 *     ERR-01（不适用显式标记——P-POL-2/O-10 保守口径）
 *
 * 用例追溯命名（DTB §5.5）：用例名尾部带需求/用例组编号（POL_JNT_1/
 * POL_JNT_2 等），正文断言处注明验证的条款；解析算例数值均为解析已知
 * 答案（π 的整数倍与 0.5/0.1 级二进制精确值——无近似容差需求）。
 *
 * 范围声明：本套件为**集成模式专属**（JointLimits.hpp 经 CollisionQuery.hpp
 * 复用 CollisionEvaluationStatus 词表——rw include 链传导，冒烟模式不编译
 * 本文件；与 src/JointLimits.cpp 同因同 gating）。本套件不验证诊断码值的
 * 注册表登记（码值权威归 diagnostics StableCodeRegistry——PA-1），只断言
 * 建议码 token 与记录结构。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/core/Provenance.hpp>
#include <sdurws/ird/policy/Contexts.hpp>
#include <sdurws/ird/policy/Errors.hpp>
#include <sdurws/ird/policy/JointLimits.hpp>
#include <sdurws/ird/policy/PolicySet.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace sdurws::ird::policy;

/// core 契约类型的短别名（测试可读性——policy 套件同款）。
namespace core = sdurws::ird::core;

namespace {

// =====================================================================
// 测试夹具（PolicySetTest/CollisionEvaluationTest 同款结构）。
// =====================================================================

/// double π 字面量（与 PolicySet.hpp 同源——期望值不另引入第二常量源）。
constexpr double kPi = 3.141592653589793;

/// 手工构造的有效关节对象身份（首字节打标——字节字典序可控）。
core::ObjectId taggedObjectId(std::uint8_t tag)
{
    core::ObjectId id;
    id.bytes[0] = tag;
    return id;
}

/// 手工构造的非全零内容身份（发布门仅核对 isValid——语义身份真实性归
/// POL-T03/T04，本套件不模拟真实摘要）。
core::ContentIdentity nonZeroContentIdentity(std::uint8_t tag)
{
    core::ContentIdentity cid;
    cid.bytes[0] = tag;
    return cid;
}

/// 有效碰撞规则子模型（§4.6 合法实例口径——本套件不消费碰撞语义，
/// 仅为发布门装配合法实例）。
CollisionRules validCollisionRules()
{
    return CollisionRules::make(
        true,
        {CollisionDomain::Self, CollisionDomain::Environment, CollisionDomain::Tool},
        PolicyThreshold::make(0.02, PolicyValueOrigin::Explicit,
                              PolicyThresholdDomain::SafetyClearance),
        true, {}, {});
}

/// 默认阈值子模型（§4.6 合法实例口径：行程上限 4π DefaultAppendixD、
/// 近限位比 0.05 Explicit、条件数未设置＝显式不适用〔P-POL-2〕）。
JointThresholds defaultJointThresholds()
{
    return JointThresholds::make(
        0.05, PolicyValueOrigin::Explicit,
        std::nullopt, PolicyValueOrigin::Explicit,
        kDefaultFiniteRotationTravelLimit, PolicyValueOrigin::DefaultAppendixD);
}

/// 有效已发布策略（其余审计/身份字段取合法占位——发布门装配）。
EngineeringPolicySet makePolicy(JointThresholds thresholds)
{
    return EngineeringPolicySet::make(
        taggedObjectId(0x01),   // 策略对象身份（非全零）
        kPolicySchemaVersionCurrent,
        nonZeroContentIdentity(0xAB),
        validCollisionRules(),
        std::move(thresholds),
        PolicyApplicability{},
        PolicyOrigin{PolicyOriginKind::Template, std::nullopt, std::nullopt},
        PolicyValidationState::Valid);
}

/**
 * @brief 名称映射替身（IPolicyNameContext 测试实现——对象→运行时名应答表；
 *        缺席对象返回 nullopt——ARC-04 不猜测的替身侧契约）。
 */
class TestNameContext final : public IPolicyNameContext {
public:
    std::map<core::ObjectId, std::string> byId;      ///< 对象→运行时名
    core::ContentIdentity mapIdentity;               ///< nameMapContentIdentity 应答

    std::optional<core::ObjectId> tryObjectId(const std::string&) const override
    {
        return std::nullopt;   // 本套件不消费正向解析——固定 nullopt 即可
    }

    std::optional<std::string> tryRuntimeName(core::ObjectId object) const override
    {
        const auto it = byId.find(object);
        return it == byId.end() ? std::nullopt : std::optional<std::string>(it->second);
    }

    core::ContentIdentity nameMapContentIdentity() const override { return mapIdentity; }
};

/// 装配"单关节＋单构型"查询（最常用形态的便捷封装）。
JointLimitQuery singleSampleQuery(JointLimitSpec spec, std::vector<double> positions)
{
    JointLimitQuery q;
    q.joints.push_back(std::move(spec));
    q.configurations.push_back(std::move(positions));
    return q;
}

/// 有限限位关节规格（两端显式）。
JointLimitSpec limitedJoint(std::uint8_t tag, std::string localName, double qmin, double qmax)
{
    JointLimitSpec spec;
    spec.jointObject = taggedObjectId(tag);
    spec.localName = std::move(localName);
    spec.qMin = qmin;
    spec.qMax = qmax;
    return spec;
}

/// continuous 关节规格（工程工作范围必填——first/second）。
JointLimitSpec continuousJoint(std::uint8_t tag, std::string localName, double lo, double hi)
{
    JointLimitSpec spec;
    spec.jointObject = taggedObjectId(tag);
    spec.localName = std::move(localName);
    spec.isContinuous = true;
    spec.engineeringRange = std::make_pair(lo, hi);
    return spec;
}

/// 以 kind 过滤发现（断言辅助——不计排序地按种类检索）。
const JointLimitFinding* findKind(const JointLimitEvaluation& e, JointLimitFindingKind kind)
{
    for (const JointLimitFinding& f : e.findings) {
        if (f.kind == kind) {
            return &f;
        }
    }
    return nullptr;
}

/// 断言输出含指定检查的 NotApplicable 标记（acceptance 3 观测点辅助）。
const JointCheckNotApplicable*
findNotApplicable(const JointLimitEvaluation& e, JointCheckNotApplicable::Check check)
{
    for (const JointCheckNotApplicable& m : e.notApplicableChecks) {
        if (m.check == check) {
            return &m;
        }
    }
    return nullptr;
}

}  // namespace

// =====================================================================
// POL-JNT-1——行程上限比较（4π 边界/超限三要素/continuous 豁免/显式阈值）。
// =====================================================================

/// 行程恰为 4π（边界）→ 不超限（T＝L 边界含于合规侧——D-08/§7.4 冻结）；
/// 同时核对边界构型的裕量数值（q=0 ∈ [−2π,2π]：margin=2π、r=1.0）。
TEST(JointLimits, TravelAtFourPiBoundaryIsNotExceeded_POL_JNT_1)
{
    const EngineeringPolicySet policy = makePolicy(defaultJointThresholds());
    TestNameContext names;
    const core::ObjectId j1 = taggedObjectId(0x10);
    names.byId[j1] = "Robot/Arm/J1";

    // 行程 T＝|2π−(−2π)|＝4π ＝ L（DefaultAppendixD 默认）——恰在边界。
    JointLimitQuery q = singleSampleQuery(limitedJoint(0x10, "J1", -2.0 * kPi, 2.0 * kPi),
                                          {0.0});

    JointLimitEvaluator evaluator;
    const JointLimitEvaluation e = evaluator.evaluate(q, policy, names);

    // 完成态：边界不产出发现（比较型结论＝不超限以"无发现"表达）。
    ASSERT_EQ(CollisionEvaluationStatus::Completed, e.status);
    EXPECT_TRUE(e.finalized);
    EXPECT_TRUE(e.findings.empty()) << "T＝L（4π）不得判超限（§7.4 边界含于合规侧，D-08）";
    EXPECT_TRUE(e.diagnostics.empty());
    // 裕量逐构型齐备（有限限位关节 → Provided）。
    ASSERT_EQ(std::size_t{1}, e.margins.size());
    EXPECT_EQ(core::FieldState::Provided, e.margins[0].marginToNearestLimit.state());
    EXPECT_EQ(core::FieldState::Provided, e.margins[0].nearLimitRatioValue.state());
    // q=0 的解析已知裕量：margin＝2π（到任一端等距），r＝2π/2π＝1。
    EXPECT_DOUBLE_EQ(2.0 * kPi, e.margins[0].marginToNearestLimit.tryValue().value());
    EXPECT_DOUBLE_EQ(1.0, e.margins[0].nearLimitRatioValue.tryValue().value());
}

/// 行程 6π ＞ 4π → TravelLimitExceeded，比较型三要素齐备（实际行程/阈值/
/// rad 单位语义；level=Must——SA-15 ConfirmableFinding 素材，MDL-06④）。
TEST(JointLimits, TravelAboveFourPiCarriesComparativeTriple_POL_JNT_1)
{
    const EngineeringPolicySet policy = makePolicy(defaultJointThresholds());
    TestNameContext names;
    const core::ObjectId j1 = taggedObjectId(0x10);
    names.byId[j1] = "Robot/Arm/J1";

    // 行程 T＝|3π−(−3π)|＝6π ＞ L＝4π（解析已知答案——π 整数倍二进制精确）。
    JointLimitQuery q = singleSampleQuery(limitedJoint(0x10, "J1", -3.0 * kPi, 3.0 * kPi),
                                          {0.0});

    JointLimitEvaluator evaluator;
    const JointLimitEvaluation e = evaluator.evaluate(q, policy, names);

    // 检出超限：评估仍 Completed（D-13——检出≠评估失败），finalized=true。
    ASSERT_EQ(CollisionEvaluationStatus::Completed, e.status);
    EXPECT_TRUE(e.finalized);
    ASSERT_EQ(std::size_t{1}, e.findings.size());
    const JointLimitFinding& f = e.findings[0];
    EXPECT_EQ(JointLimitFindingKind::TravelLimitExceeded, f.kind);
    EXPECT_EQ(j1, f.jointObject);
    EXPECT_EQ("Robot/Arm/J1", f.runtimeName);
    // 比较型三要素（POL-JNT-1 观测点——actual/threshold 齐备，rad）：
    // 实际行程＝6π；阈值＝4π（DefaultAppendixD——kDefaultFiniteRotationTravelLimit）。
    EXPECT_DOUBLE_EQ(6.0 * kPi, f.actualValue);
    EXPECT_DOUBLE_EQ(kDefaultFiniteRotationTravelLimit, f.thresholdValue);
    EXPECT_EQ(PolicyRuleLevel::Must, f.level);
    // 定位字段齐备（localName 显示辅助）。
    EXPECT_EQ("J1", f.localName);
}

/// continuous 关节豁免行程上限检查（§7.4"continuous 关节免除"行——工程
/// 工作范围再宽也不产出行程/限位发现；裕量以 NotApplicable 显式标记，
/// 不伪造数值）。
TEST(JointLimits, ContinuousJointExemptFromTravelAndLimitChecks_POL_JNT_1)
{
    const EngineeringPolicySet policy = makePolicy(defaultJointThresholds());
    TestNameContext names;
    const core::ObjectId jc = taggedObjectId(0x20);
    names.byId[jc] = "Robot/Arm/Wrist";

    // 多圈 continuous：工程范围 (−100, 100) rad——远超 4π 但检查豁免；
    // 两个构型（各 1 值——内层与关节表等长同序）。
    JointLimitQuery q;
    q.joints.push_back(continuousJoint(0x20, "Wrist", -100.0, 100.0));
    q.configurations.push_back({50.0});
    q.configurations.push_back({-50.0});

    JointLimitEvaluator evaluator;
    const JointLimitEvaluation e = evaluator.evaluate(q, policy, names);

    ASSERT_EQ(CollisionEvaluationStatus::Completed, e.status);
    EXPECT_TRUE(e.finalized);
    EXPECT_TRUE(e.findings.empty()) << "continuous 关节豁免行程上限与限位违例检查"
                                       "（MDL-12/§7.4——工程范围有限性另行检出）";
    // 裕量记录逐构型齐备，但恒 NotApplicable（无限位可比——不伪造数值）。
    ASSERT_EQ(std::size_t{2}, e.margins.size());
    for (const JointMarginRecord& m : e.margins) {
        EXPECT_EQ(core::FieldState::NotApplicable, m.marginToNearestLimit.state());
        EXPECT_EQ(core::FieldState::NotApplicable, m.nearLimitRatioValue.state());
    }
}

/// 多圈机型经工程策略入口改阈值为 6π（Explicit）→ 行程 6π 复检通过
/// 正常放行（AT-01 反例素材/§10.4 走查——策略阈值可配置性的解析算例）。
TEST(JointLimits, ExplicitSixPiThresholdPassesMultiTurnJoint_POL_JNT_1)
{
    // 显式 6π 阈值（origin=Explicit——多圈关节调阈值场景）。
    JointThresholds thresholds = JointThresholds::make(
        0.05, PolicyValueOrigin::Explicit,
        std::nullopt, PolicyValueOrigin::Explicit,
        6.0 * kPi, PolicyValueOrigin::Explicit);
    const EngineeringPolicySet policy = makePolicy(std::move(thresholds));
    TestNameContext names;
    const core::ObjectId j1 = taggedObjectId(0x10);
    names.byId[j1] = "Robot/Arm/J1";

    // 行程 T＝6π ＝ L（6π）——恰在边界：不超限（T＝L 含于合规侧，D-08）。
    JointLimitQuery q = singleSampleQuery(limitedJoint(0x10, "J1", -3.0 * kPi, 3.0 * kPi),
                                          {0.0});

    JointLimitEvaluator evaluator;
    const JointLimitEvaluation e = evaluator.evaluate(q, policy, names);

    ASSERT_EQ(CollisionEvaluationStatus::Completed, e.status);
    EXPECT_TRUE(e.findings.empty()) << "显式阈值 6π 下行程 6π 不超限（AT-01 多圈"
                                       "改阈值后正常放行——§10.4）";
    EXPECT_TRUE(e.finalized);
}

/// 行程上限校验开关关闭（travelLimitCheckEnabled=false）→ 不产出行程发现，
/// 且输出 TravelLimit 检查的显式 NotApplicable 标记（§4.4 校验开关行的
/// 评估期语义——"关闭"必须可见，防"无发现"被读作"检查通过"）。
TEST(JointLimits, TravelCheckDisabledEmitsExplicitMarker)
{
    JointThresholds thresholds = JointThresholds::make(
        0.05, PolicyValueOrigin::Explicit,
        std::nullopt, PolicyValueOrigin::Explicit,
        kDefaultFiniteRotationTravelLimit, PolicyValueOrigin::DefaultAppendixD,
        false);   // travelLimitCheckEnabled=false（§4.4）
    const EngineeringPolicySet policy = makePolicy(std::move(thresholds));
    TestNameContext names;
    const core::ObjectId j1 = taggedObjectId(0x10);
    names.byId[j1] = "Robot/Arm/J1";

    // 行程 8π ≫ 4π——若误执行行程检查必产出发现（检查确实被关闭的对照）。
    JointLimitQuery q = singleSampleQuery(limitedJoint(0x10, "J1", -4.0 * kPi, 4.0 * kPi),
                                          {0.0});

    JointLimitEvaluator evaluator;
    const JointLimitEvaluation e = evaluator.evaluate(q, policy, names);

    ASSERT_EQ(CollisionEvaluationStatus::Completed, e.status);
    EXPECT_EQ(nullptr, findKind(e, JointLimitFindingKind::TravelLimitExceeded))
        << "开关关闭后不得执行行程检查";
    // 显式标记（可观测性——§4.5 NotApplicable 语义）。
    const JointCheckNotApplicable* marker =
        findNotApplicable(e, JointCheckNotApplicable::Check::TravelLimit);
    ASSERT_NE(nullptr, marker);
    EXPECT_NE(std::string::npos, marker->cause.find("travelLimitCheckEnabled=false"))
        << "标记 cause 须可追溯到策略开关原文";
}

// =====================================================================
// POL-JNT-2——近限位/区间（P-POL-2 显式 NotApplicable、区间与工程范围
// 诊断级检出〔D-13〕、近限位与闭区间边界、裕量口径）。
// =====================================================================

/// acceptance 3（O-10/P-POL-2 保守口径）：nearLimitRatio/conditionNumberWarning
/// 未设置（nullopt）→ 输出两项显式 NotApplicable 标记；近限位构型不产出
/// NearLimit 发现；裕量照常 Provided（不因阈值缺席而伪造/缺席）。
TEST(JointLimits, UnsetThresholdsOutputExplicitNotApplicable_POL_JNT_2)
{
    // 近限位比与条件数均未设置（P-POL-2：无冻结默认——不发明数值）。
    JointThresholds thresholds = JointThresholds::make(
        std::nullopt, PolicyValueOrigin::Explicit,
        std::nullopt, PolicyValueOrigin::Explicit,
        kDefaultFiniteRotationTravelLimit, PolicyValueOrigin::DefaultAppendixD);
    const EngineeringPolicySet policy = makePolicy(std::move(thresholds));
    TestNameContext names;
    const core::ObjectId j1 = taggedObjectId(0x10);
    names.byId[j1] = "Robot/Arm/J1";

    // 构型贴近上限（margin=0.1/半宽 2π→r≈0.016≪任何常规阈值）——若伪造
    // 默认阈值必产出 NearLimit，本用例即暴露。
    const double qmax = 2.0 * kPi;
    JointLimitQuery q = singleSampleQuery(limitedJoint(0x10, "J1", -2.0 * kPi, qmax),
                                          {qmax - 0.1});

    JointLimitEvaluator evaluator;
    const JointLimitEvaluation e = evaluator.evaluate(q, policy, names);

    ASSERT_EQ(CollisionEvaluationStatus::Completed, e.status);
    EXPECT_TRUE(e.finalized);
    // 两项检查级显式标记（acceptance 3 的直接观测点）。
    const JointCheckNotApplicable* nearMarker =
        findNotApplicable(e, JointCheckNotApplicable::Check::NearLimitRatio);
    ASSERT_NE(nullptr, nearMarker) << "nearLimitRatio 未设置→必须输出显式 NotApplicable";
    EXPECT_NE(std::string::npos, nearMarker->cause.find("P-POL-2"));
    const JointCheckNotApplicable* condMarker =
        findNotApplicable(e, JointCheckNotApplicable::Check::ConditionNumberWarning);
    ASSERT_NE(nullptr, condMarker) << "conditionNumberWarning 未设置→同口径标记";
    // 不伪造数值：无 NearLimit 发现（无阈值即无判定），裕量仍如实 Provided。
    EXPECT_EQ(nullptr, findKind(e, JointLimitFindingKind::NearLimit));
    ASSERT_EQ(std::size_t{1}, e.margins.size());
    EXPECT_EQ(core::FieldState::Provided, e.margins[0].marginToNearestLimit.state());
    // 期望值＝§7.4 裕量公式（qmax−q——对测试输入原样求值，浮点一致）。
    const double expectedMargin = qmax - (qmax - 0.1);
    EXPECT_DOUBLE_EQ(expectedMargin, e.margins[0].marginToNearestLimit.tryValue().value());
    // 输出形结构断言（六成员——notApplicableChecks 为 §9.4 五成员之外唯一
    // 实现增量，登记于 policy.md §15.4 v0.9；新增成员即本绑定编译失败）。
    auto& [status, findings, margins, notApplicable, diagnostics, finalized] = e;
    (void) status;
    (void) findings;
    (void) margins;
    (void) notApplicable;
    (void) diagnostics;
    (void) finalized;
    EXPECT_EQ(std::size_t{2}, e.notApplicableChecks.size());
}

/// 区间非法（qmin ≥ qmax）→ IntervalInvalid 诊断级发现＋POLICY-JNT-TABLE-
/// INVALID 伴随诊断（比较型三要素齐备）＋跳过该关节（无裕量）——**状态仍
/// Completed**（D-13：检出不阻断，判定权归命令处理器——acceptance 2）。
TEST(JointLimits, IntervalInvalidIsDiagnosticLevelAndSkipsJoint_POL_JNT_2)
{
    const EngineeringPolicySet policy = makePolicy(defaultJointThresholds());
    TestNameContext names;
    const core::ObjectId bad = taggedObjectId(0x30);
    const core::ObjectId good = taggedObjectId(0x40);
    names.byId[bad] = "Robot/Arm/BadJ";
    names.byId[good] = "Robot/Arm/GoodJ";

    // 关节表两关节：bad 区间 qmin＞qmax（含行程远超 4π 的加重情节——若误
    // 评行程必产出 TravelLimitExceeded，可证明"跳过"确实生效）；good 正常。
    JointLimitQuery q;
    q.joints.push_back(limitedJoint(0x30, "BadJ", 3.0 * kPi, -3.0 * kPi));   // 区间非法
    q.joints.push_back(limitedJoint(0x40, "GoodJ", -1.0, 1.0));              // 合法
    q.configurations.push_back({0.0, 0.5});
    q.configurations.push_back({0.0, -0.5});

    JointLimitEvaluator evaluator;
    const JointLimitEvaluation e = evaluator.evaluate(q, policy, names);

    // D-13 核心：检出非法≠评估失败——Completed＋finalized=true（阻断决策
    // 归命令处理器；本评估器不代替放行/阻止——acceptance 2）。
    ASSERT_EQ(CollisionEvaluationStatus::Completed, e.status);
    EXPECT_TRUE(e.finalized);
    // IntervalInvalid 发现（比较三要素：actual=qmin=3π、threshold=qmax=−3π）。
    const JointLimitFinding* f = findKind(e, JointLimitFindingKind::IntervalInvalid);
    ASSERT_NE(nullptr, f);
    EXPECT_EQ(bad, f->jointObject);
    EXPECT_DOUBLE_EQ(3.0 * kPi, f->actualValue);
    EXPECT_DOUBLE_EQ(-3.0 * kPi, f->thresholdValue);
    EXPECT_EQ(PolicyRuleLevel::Must, f->level);
    // 伴随稳定诊断：建议码＋比较型三要素（ERR-01/UX-03——诊断自足定位）。
    ASSERT_EQ(std::size_t{1}, e.diagnostics.size());
    EXPECT_EQ("POLICY-JNT-TABLE-INVALID", e.diagnostics[0].code);
    ASSERT_TRUE(e.diagnostics[0].subject.has_value());
    EXPECT_EQ(bad, *e.diagnostics[0].subject);
    ASSERT_TRUE(e.diagnostics[0].comparison.has_value());
    EXPECT_EQ(core::FieldState::Provided,
              e.diagnostics[0].comparison->actual.quantity.state());
    // 跳过语义三证：该关节无裕量记录；无行程发现（行程 6π 未被评估）；
    // 合法关节照常全评（有裕量、无发现）。
    for (const JointMarginRecord& m : e.margins) {
        EXPECT_NE(bad, m.jointObject) << "非法区间关节必须整体跳过（不伪造裕量）";
    }
    EXPECT_EQ(nullptr, findKind(e, JointLimitFindingKind::TravelLimitExceeded));
    ASSERT_EQ(std::size_t{2}, e.margins.size());
    for (const JointMarginRecord& m : e.margins) {
        EXPECT_EQ(good, m.jointObject);
        EXPECT_DOUBLE_EQ(0.5, m.marginToNearestLimit.tryValue().value());   // min(0.5, 0.5)
        EXPECT_DOUBLE_EQ(0.5, m.nearLimitRatioValue.tryValue().value());    // 0.5/半宽 1
    }
}

/// 工程工作范围非法两形态：min≥max 与端点非有限（MDL-12 有限性）→
/// EngineeringRangeInvalid 诊断级发现＋伴随诊断＋跳过（D-13）；
/// 非有限端点在伴随诊断中以 Invalid 态保留 "nan" 原文（不伪造数值）。
TEST(JointLimits, EngineeringRangeInvalidIsDiagnosticLevel_POL_JNT_2)
{
    const EngineeringPolicySet policy = makePolicy(defaultJointThresholds());
    TestNameContext names;
    const core::ObjectId a = taggedObjectId(0x31);
    const core::ObjectId b = taggedObjectId(0x32);
    names.byId[a] = "Robot/Arm/ContA";
    names.byId[b] = "Robot/Arm/ContB";

    // 两个非法 continuous：A 端点相等（min≥max）；B 端点 NaN（非有限）。
    JointLimitQuery q;
    q.joints.push_back(continuousJoint(0x31, "ContA", 5.0, 5.0));
    q.joints.push_back(continuousJoint(0x32, "ContB",
                                       std::numeric_limits<double>::quiet_NaN(), 10.0));
    q.configurations.push_back({0.0, 0.0});

    JointLimitEvaluator evaluator;
    const JointLimitEvaluation e = evaluator.evaluate(q, policy, names);

    // D-13：检出为诊断级，状态 Completed（不阻断）。
    ASSERT_EQ(CollisionEvaluationStatus::Completed, e.status);
    EXPECT_TRUE(e.finalized);
    // 两条 EngineeringRangeInvalid 发现（跳过→无裕量记录）。
    ASSERT_EQ(std::size_t{2}, e.findings.size());
    for (const JointLimitFinding& f : e.findings) {
        EXPECT_EQ(JointLimitFindingKind::EngineeringRangeInvalid, f.kind);
        EXPECT_EQ(PolicyRuleLevel::Must, f.level);
    }
    EXPECT_TRUE(e.margins.empty()) << "非法工程范围关节必须跳过（不伪造裕量）";
    // 伴随诊断：建议码＋定位；B 的 NaN 端点以 Invalid 态保留原文 "nan"。
    ASSERT_EQ(std::size_t{2}, e.diagnostics.size());
    EXPECT_EQ("POLICY-JNT-ENGINEERING-RANGE-INVALID", e.diagnostics[0].code);
    EXPECT_EQ("POLICY-JNT-ENGINEERING-RANGE-INVALID", e.diagnostics[1].code);
    EXPECT_EQ(core::FieldState::Invalid,
              e.diagnostics[1].comparison->actual.quantity.state());
    EXPECT_EQ(std::string("nan"),
              e.diagnostics[1].comparison->actual.quantity.invalidRawInput());
}

/// 近限位边界（§7.4 严格小于）：r＝阈值不警告（D-08 同口径——边界含于
/// 安全侧）；r＜阈值警告且比较三要素为无量纲、level=Should（Should 级默认）。
TEST(JointLimits, NearLimitBoundaryIsStrictLessThanWithShouldLevel_POL_JNT_2)
{
    // 本用例需可控阈值——显式 0.1（§4.4 域窗 (0,1] 合法）。
    JointThresholds thresholds = JointThresholds::make(
        0.1, PolicyValueOrigin::Explicit,
        std::nullopt, PolicyValueOrigin::Explicit,
        kDefaultFiniteRotationTravelLimit, PolicyValueOrigin::DefaultAppendixD);
    const EngineeringPolicySet strictPolicy = makePolicy(std::move(thresholds));
    TestNameContext names;
    const core::ObjectId j1 = taggedObjectId(0x10);
    names.byId[j1] = "Robot/Arm/J1";

    // 区间 [0,10]（半宽 5）：q=9.5→r=0.1（恰等于阈值——不警告）；
    // q=9.6→r=0.08（严格小于——警告）。
    JointLimitQuery q;
    q.joints.push_back(limitedJoint(0x10, "J1", 0.0, 10.0));
    q.configurations.push_back({9.5});
    q.configurations.push_back({9.6});

    JointLimitEvaluator evaluator;
    const JointLimitEvaluation e = evaluator.evaluate(q, strictPolicy, names);

    ASSERT_EQ(CollisionEvaluationStatus::Completed, e.status);
    // 唯一 NearLimit 发现来自样本 1（r=0.08＜0.1）；样本 0 的 r=0.1＝阈值
    // 不警告——严格小于的边界语义（§7.4 近限位比行）。
    const JointLimitFinding* f = findKind(e, JointLimitFindingKind::NearLimit);
    ASSERT_NE(nullptr, f);
    EXPECT_EQ(j1, f->jointObject);
    const double expectedRatio = (10.0 - 9.6) / 5.0;   // §7.4 口径：margin/半宽
    EXPECT_DOUBLE_EQ(expectedRatio, f->actualValue);
    EXPECT_DOUBLE_EQ(0.1, f->thresholdValue);
    EXPECT_EQ(PolicyRuleLevel::Should, f->level) << "近限位警告为 Should 级默认（§7.4）";
    // 裕量逐样本齐备且边界样本 r 恰为 0.1（Provided 数值——非伪造）。
    ASSERT_EQ(std::size_t{2}, e.margins.size());
    EXPECT_DOUBLE_EQ(0.1, e.margins[0].nearLimitRatioValue.tryValue().value());
}

/// 限位违例闭区间边界（§7.4 限位违例行）：q＝端点不违例；越端产出
/// LimitViolated（actual=违例位置、threshold=被越过的限位端，rad，Must）；
/// 越限样本裕量为负有符号事实（保号口径）。
TEST(JointLimits, LimitViolationClosedIntervalBoundary_POL_JNT_2)
{
    const EngineeringPolicySet policy = makePolicy(defaultJointThresholds());
    TestNameContext names;
    const core::ObjectId j1 = taggedObjectId(0x10);
    names.byId[j1] = "Robot/Arm/J1";

    // 样本：10.0＝上限（闭区间不违例）；10.5（越上限）；−0.5（越下限）。
    JointLimitQuery q;
    q.joints.push_back(limitedJoint(0x10, "J1", 0.0, 10.0));
    q.configurations.push_back({10.0});
    q.configurations.push_back({10.5});
    q.configurations.push_back({-0.5});

    JointLimitEvaluator evaluator;
    const JointLimitEvaluation e = evaluator.evaluate(q, policy, names);

    ASSERT_EQ(CollisionEvaluationStatus::Completed, e.status);
    EXPECT_TRUE(e.finalized);
    // 恰在上限的样本 0：无违例（q ∈ [qmin,qmax] 闭区间）但 r=0＜0.05→
    // NearLimit 警告（限位端贴限与违例是两类结论）。
    // 越限样本：每关节每 kind 至多一条——actual 取首个命中样本（10.5）。
    const JointLimitFinding* v = findKind(e, JointLimitFindingKind::LimitViolated);
    ASSERT_NE(nullptr, v);
    EXPECT_DOUBLE_EQ(10.5, v->actualValue);
    EXPECT_DOUBLE_EQ(10.0, v->thresholdValue) << "阈值侧＝被越过的限位端（上限）";
    EXPECT_EQ(PolicyRuleLevel::Must, v->level);
    // 样本 0 的近限位警告（margin=0→r=0）与越限样本的负裕量保号记录。
    const JointLimitFinding* n = findKind(e, JointLimitFindingKind::NearLimit);
    ASSERT_NE(nullptr, n);
    EXPECT_DOUBLE_EQ(0.0, n->actualValue);
    ASSERT_EQ(std::size_t{3}, e.margins.size());
    EXPECT_DOUBLE_EQ(0.0, e.margins[0].marginToNearestLimit.tryValue().value());
    EXPECT_DOUBLE_EQ(-0.5, e.margins[1].marginToNearestLimit.tryValue().value())
        << "越限样本裕量为负有符号事实（违例深度可见，不伪造为 0/正值）";
}

/// 裕量 §7.4 口径的解析算例（KIN-01 素材数值正确性）：区间 [−π/2, π/2]、
/// 三构型已知裕量/比值；记录排序＝(sampleIndex, jointObject)。
TEST(JointLimits, MarginValuesFollowSpecFormulaAndOrdering_POL_JNT_2)
{
    const EngineeringPolicySet policy = makePolicy(defaultJointThresholds());
    TestNameContext names;
    const core::ObjectId ja = taggedObjectId(0x50);
    const core::ObjectId jb = taggedObjectId(0x51);
    names.byId[ja] = "Robot/Arm/A";
    names.byId[jb] = "Robot/Arm/B";

    const double halfPi = kPi / 2.0;
    const double quarterPi = kPi / 4.0;
    JointLimitQuery q;
    // 表序故意"乱序"（B 在前 A 在后）——输出序须与装配顺序无关（规范序）。
    q.joints.push_back(limitedJoint(0x51, "B", -1.0, 1.0));
    q.joints.push_back(limitedJoint(0x50, "A", -halfPi, halfPi));
    q.configurations.push_back({0.0, -halfPi});   // A 中点 / A 下端点
    q.configurations.push_back({1.0, quarterPi}); // B 上端点 / A 45°

    JointLimitEvaluator evaluator;
    const JointLimitEvaluation e = evaluator.evaluate(q, policy, names);

    ASSERT_EQ(CollisionEvaluationStatus::Completed, e.status);
    ASSERT_EQ(std::size_t{4}, e.margins.size());
    // 排序契约：sampleIndex 升序 → jointObject 字典序（0x50＜0x51）。
    EXPECT_EQ(std::size_t{0}, e.margins[0].sampleIndex);
    EXPECT_EQ(ja, e.margins[0].jointObject);
    EXPECT_EQ(std::size_t{0}, e.margins[1].sampleIndex);
    EXPECT_EQ(jb, e.margins[1].jointObject);
    EXPECT_EQ(std::size_t{1}, e.margins[2].sampleIndex);
    EXPECT_EQ(ja, e.margins[2].jointObject);
    EXPECT_EQ(std::size_t{1}, e.margins[3].sampleIndex);
    EXPECT_EQ(jb, e.margins[3].jointObject);
    // 解析已知裕量（§7.4：margin=min(q−qmin, qmax−q)、r=margin/半宽）：
    // 样本 0·A（q=−π/2＝下端点）：margin=0、r=0（贴限不违例——闭区间）。
    EXPECT_DOUBLE_EQ(0.0, e.margins[0].marginToNearestLimit.tryValue().value());
    EXPECT_DOUBLE_EQ(0.0, e.margins[0].nearLimitRatioValue.tryValue().value());
    // 样本 0·B（q=0 中点）：margin=1（半宽）、r=1。
    EXPECT_DOUBLE_EQ(1.0, e.margins[1].marginToNearestLimit.tryValue().value());
    EXPECT_DOUBLE_EQ(1.0, e.margins[1].nearLimitRatioValue.tryValue().value());
    // 样本 1·A（q=π/4）：margin=π/2−π/4=π/4、r=（π/4）/（π/2）=0.5。
    EXPECT_DOUBLE_EQ(quarterPi, e.margins[2].marginToNearestLimit.tryValue().value());
    EXPECT_DOUBLE_EQ(0.5, e.margins[2].nearLimitRatioValue.tryValue().value());
    // 样本 1·B（q=1.0＝上限端点）：margin=0、r=0。
    EXPECT_DOUBLE_EQ(0.0, e.margins[3].marginToNearestLimit.tryValue().value());
}

// =====================================================================
// 失败轨与查询违约矩阵（§7.5/§9.4 错误语义）。
// =====================================================================

/// 构型位置非有限（NaN）→ 评估 Failed＋POLICY-CLL-EVALUATION-FAILED、
/// findings/margins 不产出（§7.5 不伪造不入 findings）、finalized=false
/// （非终态不入正式证据——CON-04）。
TEST(JointLimits, NonFinitePositionFailsEvaluationWithoutFabrication)
{
    const EngineeringPolicySet policy = makePolicy(defaultJointThresholds());
    TestNameContext names;
    const core::ObjectId j1 = taggedObjectId(0x10);
    names.byId[j1] = "Robot/Arm/J1";

    JointLimitQuery q = singleSampleQuery(limitedJoint(0x10, "J1", -1.0, 1.0),
                                          {std::numeric_limits<double>::quiet_NaN()});

    JointLimitEvaluator evaluator;
    const JointLimitEvaluation e = evaluator.evaluate(q, policy, names);

    EXPECT_EQ(CollisionEvaluationStatus::Failed, e.status);
    EXPECT_FALSE(e.finalized);
    EXPECT_TRUE(e.findings.empty());
    EXPECT_TRUE(e.margins.empty());
    ASSERT_EQ(std::size_t{1}, e.diagnostics.size());
    EXPECT_EQ("POLICY-CLL-EVALUATION-FAILED", e.diagnostics[0].code);
    // 原文保留（%.17g 的 "nan" 字面量——NFR-COR-03 不静默转 0/通过）。
    EXPECT_NE(std::string::npos, e.diagnostics[0].cause.find("nan"));
}

/// 名称不可解析（映射缺席）→ Failed＋POLICY-CLL-NAME-UNRESOLVED、
/// finalized=false——不猜测（ARC-04；Errors.hpp NameUnresolved 注释的
/// "评估期同因转 Failed＋同码诊断"路径）。
TEST(JointLimits, UnresolvableJointNameFailsWithoutGuessing)
{
    const EngineeringPolicySet policy = makePolicy(defaultJointThresholds());
    TestNameContext names;   // 空映射——任何对象都不可解析

    JointLimitQuery q = singleSampleQuery(limitedJoint(0x10, "J1", -1.0, 1.0), {0.0});

    JointLimitEvaluator evaluator;
    const JointLimitEvaluation e = evaluator.evaluate(q, policy, names);

    EXPECT_EQ(CollisionEvaluationStatus::Failed, e.status);
    EXPECT_FALSE(e.finalized);
    EXPECT_TRUE(e.findings.empty());
    ASSERT_EQ(std::size_t{1}, e.diagnostics.size());
    EXPECT_EQ("POLICY-CLL-NAME-UNRESOLVED", e.diagnostics[0].code);
    ASSERT_TRUE(e.diagnostics[0].subject.has_value());
    EXPECT_EQ(taggedObjectId(0x10), *e.diagnostics[0].subject);
}

/// 查询契约违约矩阵（装配形态违约→PolicyError(QueryInvalid) fail-fast，
/// §9.4 Spec 契约行）：空表/空构型/无效身份/重复身份/单侧限位/continuous
/// 缺工程范围/构型维度不符——逐项触发，token 前缀可核。
TEST(JointLimits, QueryContractViolationsFailFast)
{
    const EngineeringPolicySet policy = makePolicy(defaultJointThresholds());
    TestNameContext names;
    names.byId[taggedObjectId(0x10)] = "Robot/Arm/J1";
    JointLimitEvaluator evaluator;

    auto evaluateSafe = [&evaluator, &policy, &names](const JointLimitQuery& q) {
        try {
            evaluator.evaluate(q, policy, names);
            return std::string{};
        }
        catch (const PolicyError& err) {
            return std::string{err.what()};
        }
    };

    // ① 空关节表。
    {
        JointLimitQuery q;
        EXPECT_NE(std::string::npos, evaluateSafe(q).find("policy/cll-query-invalid"));
    }
    // ② 空构型序列。
    {
        JointLimitQuery q;
        q.joints.push_back(limitedJoint(0x10, "J1", -1.0, 1.0));
        EXPECT_NE(std::string::npos, evaluateSafe(q).find("policy/cll-query-invalid"));
    }
    // ③ 无效身份（全零保留值）。
    {
        JointLimitSpec spec = limitedJoint(0x10, "J1", -1.0, 1.0);
        spec.jointObject = core::ObjectId{};   // 全零保留值
        EXPECT_NE(std::string::npos,
                  evaluateSafe(singleSampleQuery(spec, {0.0})).find("policy/cll-query-invalid"));
    }
    // ④ 重复身份。
    {
        JointLimitQuery q;
        q.joints.push_back(limitedJoint(0x10, "J1", -1.0, 1.0));
        q.joints.push_back(limitedJoint(0x10, "J1-dup", -2.0, 2.0));
        q.configurations.push_back({0.0, 0.0});
        EXPECT_NE(std::string::npos, evaluateSafe(q).find("policy/cll-query-invalid"));
    }
    // ⑤ 单侧限位（qMin/qMax 须同有同无）。
    {
        JointLimitSpec spec = limitedJoint(0x10, "J1", -1.0, 1.0);
        spec.qMax = std::nullopt;   // 仅剩 qMin——违约
        EXPECT_NE(std::string::npos,
                  evaluateSafe(singleSampleQuery(spec, {0.0})).find("policy/cll-query-invalid"));
    }
    // ⑥ continuous 缺必填工程工作范围。
    {
        JointLimitSpec spec = continuousJoint(0x10, "J1", -1.0, 1.0);
        spec.engineeringRange = std::nullopt;   // "必填"缺失——装配违约（§15.4 两分）
        EXPECT_NE(std::string::npos,
                  evaluateSafe(singleSampleQuery(spec, {0.0})).find("policy/cll-query-invalid"));
    }
    // ⑦ 构型维度与关节表长度不符。
    {
        JointLimitQuery q;
        q.joints.push_back(limitedJoint(0x10, "J1", -1.0, 1.0));
        q.configurations.push_back({0.0, 0.0});   // 2 值对 1 关节
        EXPECT_NE(std::string::npos, evaluateSafe(q).find("policy/cll-query-invalid"));
    }
}

// =====================================================================
// 确定性与稳定排序（§9.4/NFR-COR-02——重复评估与装配顺序无关性）。
// =====================================================================

/// 重复评估逐字段相等（operator==）＋关节表两种装配顺序输出完全一致
/// （输出与装配顺序无关——规范序处理的确定性承诺）＋findings 排序键
/// （对象字典序×kind）。
TEST(JointLimits, DeterministicOutputIndependentOfTableOrder)
{
    const EngineeringPolicySet policy = makePolicy(defaultJointThresholds());
    TestNameContext names;
    const core::ObjectId j1 = taggedObjectId(0x61);
    const core::ObjectId j2 = taggedObjectId(0x62);
    const core::ObjectId j3 = taggedObjectId(0x63);
    names.byId[j1] = "Robot/Arm/J1";
    names.byId[j2] = "Robot/Arm/J2";
    names.byId[j3] = "Robot/Arm/J3";

    // 场景：J1 行程超限（6π）；J2 有越限样本＋近限样本；J3 continuous。
    JointLimitQuery orderA;
    orderA.joints.push_back(limitedJoint(0x61, "J1", -3.0 * kPi, 3.0 * kPi));
    orderA.joints.push_back(limitedJoint(0x62, "J2", 0.0, 10.0));
    orderA.joints.push_back(continuousJoint(0x63, "J3", -5.0, 5.0));
    orderA.configurations.push_back({0.0, 9.97, 1.0});
    orderA.configurations.push_back({0.0, 10.5, -1.0});

    // 同一场景的另一种装配顺序（J3/J2/J1 倒序）。
    JointLimitQuery orderB;
    orderB.joints.push_back(limitedJoint(0x63, "J3", -5.0, 5.0));
    orderB.joints.back().isContinuous = true;
    orderB.joints.back().engineeringRange = std::make_pair(-5.0, 5.0);
    orderB.joints.push_back(limitedJoint(0x62, "J2", 0.0, 10.0));
    orderB.joints.push_back(limitedJoint(0x61, "J1", -3.0 * kPi, 3.0 * kPi));
    orderB.configurations.push_back({1.0, 9.97, 0.0});
    orderB.configurations.push_back({-1.0, 10.5, 0.0});

    JointLimitEvaluator evaluator;
    const JointLimitEvaluation ea = evaluator.evaluate(orderA, policy, names);
    const JointLimitEvaluation eb = evaluator.evaluate(orderB, policy, names);
    const JointLimitEvaluation ea2 = evaluator.evaluate(orderA, policy, names);

    // 重复评估逐字段相等（NFR-COR-02）。
    EXPECT_EQ(ea, ea2);
    // 装配顺序无关（同一场景两种表序→逐字段一致——含诊断与标记序）。
    EXPECT_EQ(ea, eb);
    // 非空性自检（场景确有发现——防止上述"相等"退化为"双方全空"）。
    EXPECT_FALSE(ea.findings.empty());
    EXPECT_FALSE(ea.margins.empty());
    // findings 排序键：jointObject 字节字典序（0x61＜0x62）×kind。
    for (std::size_t i = 1; i < ea.findings.size(); ++i) {
        const JointLimitFinding& prev = ea.findings[i - 1];
        const JointLimitFinding& curr = ea.findings[i];
        EXPECT_TRUE(prev.jointObject < curr.jointObject
                    || (prev.jointObject == curr.jointObject
                        && static_cast<int>(prev.kind) <= static_cast<int>(curr.kind)))
            << "findings 须按 (对象字典序×kind) 稳定排序（§9.4）";
    }
    // 超 4π 的 J1 行程发现确在场（比较型供给侧的场景有效性）。
    EXPECT_NE(nullptr, findKind(ea, JointLimitFindingKind::TravelLimitExceeded));
}

/// 接口契约冒烟：makeJointLimitEvaluator 产出的实例经 IJointLimitEvaluator
/// 接口引用调用等价（§9.4 消费面——处理器/kinematics 的实际调用形态）。
TEST(JointLimits, FactoryInstanceSatisfiesInterfaceContract)
{
    const EngineeringPolicySet policy = makePolicy(defaultJointThresholds());
    TestNameContext names;
    const core::ObjectId j1 = taggedObjectId(0x10);
    names.byId[j1] = "Robot/Arm/J1";
    JointLimitQuery q = singleSampleQuery(limitedJoint(0x10, "J1", -2.0 * kPi, 2.0 * kPi),
                                          {0.0});

    const std::unique_ptr<IJointLimitEvaluator> evaluator = makeJointLimitEvaluator();
    ASSERT_TRUE(evaluator != nullptr);
    const JointLimitEvaluation viaInterface = evaluator->evaluate(q, policy, names);

    JointLimitEvaluator direct;
    EXPECT_EQ(viaInterface, direct.evaluate(q, policy, names))
        << "接口形态与直构形态必须等价（唯一实现——ARC-05）";
}
