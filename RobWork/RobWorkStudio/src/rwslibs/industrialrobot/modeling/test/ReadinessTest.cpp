/**
 * @file   ReadinessTest.cpp
 * @brief  模型就绪校验用例组（MdlReadiness）——契约
 *         tasks/foundation/WP-13-T08.json acceptance 1～3 的检查器侧具名
 *         自证（断言分域反例 V-15、行程确认 V-16 的检查器载体；prepare
 *         侧载体见 CommandHandlersTest，管线侧见契约测试）：
 *
 *   ACC1 落位与分层（§9.4.4/§8.2）：check 纯函数（重复调用逐字段相等）＋
 *        合法工作集全层通过＋层 token 全值＋短路优先级（L0 阻断→后层
 *        "未执行"）＋L11 策略不可解析报 Blocking 且明示"行程校验未执行"
 *        （不静默跳过）＋结果组稳定排序
 *   ACC2 断言分域反例（V-15，MDL-06/AT-01）：①m≤0（MASS-NONPOSITIVE，
 *        精确三要素定位）②惯量非 SPD（INERTIA-NOT-SPD）③qmin≥qmax
 *        （LIMIT-INTERVAL）④物性全缺失＋已确认 continuous→可应用
 *        （缺失走预告、continuous 豁免限位断言——V15-04）
 *   ACC3 行程上限确认（V-16，MDL-06④/M-10/SA-15）：±1080° 多圈关节→
 *        TRAVEL-LIMIT ConfirmableFinding（实际行程/阈值/单位 rad 三要素，
 *        阈值唯一来自 policy——默认 4π 边界不超限、配置 16π 后正常放行）
 *
 * 设计依据：units/modeling.md §8.2/§9.4.4/§9.3、units/policy.md §9.4/§4.4/
 * §7.4；shared 夹具＝test/CommandFixtures.hpp。
 */

#include "CommandFixtures.hpp"

#include <sdurws/ird/modeling/DiagCodes.hpp>   // 码常量同源对账
#include <sdurws/ird/modeling/Readiness.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace sdurws::ird::modeling::testfixture;
namespace modeling = ::sdurws::ird::modeling;  // 全局域测试——被测类型所在命名空间别名
namespace core = ::sdurws::ird::core;          // core 侧类型（身份/诊断）
namespace diagnostics = ::sdurws::ird::diagnostics;  // diagnostics 侧（稳定码注册表）
namespace policy = ::sdurws::ird::policy;      // policy 侧类型（行程评估器/策略集）
namespace project = ::sdurws::ird::project;    // project 侧类型（命令契约）
using namespace modeling;                     // 被测面（Checker/Handlers/值模型）

namespace {

/// 夹具：套件＋校验器（每用例独立——纯函数面，构造廉价）。
///
/// 套件端口＝真实行程评估器（policy 唯一实现——判定数学与生产同源）＋
/// 名称映射替身（R-4 注入面——modeling 不自建名称映射）。
struct ReadinessFixture {
    std::unique_ptr<policy::IJointLimitEvaluator> evaluator =
        policy::makeJointLimitEvaluator();
    TestNameContext names;
    TestPolicyProvider provider;
    AssertionSuite suite{AssertionSuite::Ports{evaluator.get(), &names}};
    ModelReadinessChecker checker{suite};

    /// 挂名称解析（行程评估的 runtimeName 源——R-4 注入面）。
    void bindNames(const RobotDesign& design)
    {
        for (const JointEntry& j : design.joints) {
            names.byId[j.objectId] = "Robot/" + j.localName;
        }
    }
};

}  // namespace

// =====================================================================
// ACC1 落位与分层（§9.4.4/§8.2）
// =====================================================================

/**
 * @brief 合法工作集全层通过＋check 纯函数（同输入逐字段相等）。
 *
 * 验证：acceptance 1（check 纯函数、L0→L11 分层、逐层明细）；模板级
 * 默认参数（行程恰 4π 边界）在默认阈值策略下不超限（T＝L 不超限——
 * D-08 边界含于合规侧）。
 */
TEST(MdlReadiness, LegalWorkingSetAllLayersPassAndPureFunction)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-06"}, std::vector<std::string>{"AT-01"});

    ReadinessFixture f;
    const policy::EngineeringPolicySet policy = makePolicy(4.0 * kPi);
    // ±2π 限位旋转关节：行程 4π＝默认阈值——边界合规。
    const RobotDesign design = makeSingleJointDesign(-2.0 * kPi, 2.0 * kPi);
    f.bindNames(design);

    const ModelingWorkingSet ws{design, {}, makeOid(), {}, {}, {}, {}};
    const CheckContext ctx{&policy, true, std::nullopt};

    const ModelReadinessReport first = f.checker.check(ws, ctx);
    const ModelReadinessReport second = f.checker.check(ws, ctx);

    // 纯函数：同输入重复 check 输出逐字段相等（§9.4.4——NFR-COR-02）。
    EXPECT_TRUE(first == second);
    // 无阻断：物性缺失走预告（Warning）→ReadyWithNotes；状态与层明细一致。
    EXPECT_EQ(readinessStatusToken(first.status), "ReadyWithNotes");
    EXPECT_TRUE(first.blockers.empty());
    for (std::size_t i = 0; i < first.layers.size(); ++i) {
        EXPECT_TRUE(first.layers[i].passed) << "层 " << i << " 未通过: "
                                           << first.layers[i].note;
        // 短路标记不应出现（全层执行）。
        EXPECT_EQ(first.layers[i].note.find("未执行"), std::string::npos)
            << "层 " << i << " 意外未执行";
    }
    // 物性缺失预告面（连杆 mass/inertia 未提供——DataInsufficient 预告）。
    EXPECT_FALSE(first.warnings.empty());
    for (const auto& w : first.warnings) {
        EXPECT_EQ(w.code, std::string(kMdlReadinessPhysicsMissing));
    }
}

/**
 * @brief 短路优先级（§8.2"顺序即短路优先级——高层依赖低层通过"）。
 *
 * 验证：acceptance 1 分层表逐层 UT——L0 结构阻断（links==joints+1 破坏）
 * →L1..L11 层明细＝"未执行"，且后层不产出任何结论。
 */
TEST(MdlReadiness, ShortCircuitPriorityStopsAtFirstBlockingLayer)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-06"}, std::vector<std::string>{});

    ReadinessFixture f;
    RobotDesign design = makeSingleJointDesign(-1.0, 1.0);
    design.links.pop_back();  // 破坏 I-MDL-1（links==joints+1）——L0 阻断

    const ModelingWorkingSet ws{design, {}, makeOid(), {}, {}, {}, {}};
    const ModelReadinessReport report = f.checker.check(ws, CheckContext{});

    EXPECT_EQ(readinessStatusToken(report.status), "NotReady");
    EXPECT_TRUE(report.layers[0].passed == false);  // L0 阻断
    EXPECT_NE(report.layers[0].note.find("阻断"), std::string::npos);
    for (std::size_t i = 1; i < report.layers.size(); ++i) {
        EXPECT_NE(report.layers[i].note.find("未执行"), std::string::npos)
            << "层 " << i << " 未被短路";
        EXPECT_FALSE(report.layers[i].passed);
    }
}

/**
 * @brief L11 策略不可解析：报 Blocking 且明示"行程校验未执行"（§9.4.4
 *        非法调用行——不静默跳过行程校验）。
 *
 * 验证：acceptance 1——"L11 策略不可解析报 Blocking 不静默跳过行程校验"。
 */
TEST(MdlReadiness, L11PolicyUnresolvableBlocksWithoutSilentTravelSkip)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-06", "ARC-05"}, std::vector<std::string>{});

    ReadinessFixture f;
    // 含有限限位旋转关节——行程校验相关性成立（策略依赖触发面）。
    const RobotDesign design = makeSingleJointDesign(-2.0 * kPi, 2.0 * kPi);
    f.bindNames(design);

    const ModelingWorkingSet ws{design, {}, makeOid(), {}, {}, {}, {}};
    // resolvedPolicy=nullptr——策略不可解析。
    const ModelReadinessReport report = f.checker.check(ws, CheckContext{});

    EXPECT_EQ(readinessStatusToken(report.status), "NotReady");
    // L11 层明细＝未通过（Blocking 落在最后一层——无短路截断前层）。
    EXPECT_FALSE(report.layers[11].passed);
    // 呈现级阻断 note：定位 resolvedPolicy＋明示行程校验未执行。
    bool found = false;
    for (const ReadinessNote& note : report.notes) {
        if (note.layer == ReadinessLayer::L11CompileRequestable
            && note.subjectPath == "resolvedPolicy") {
            found = true;
            EXPECT_NE(note.summary.find("策略不可解析"), std::string::npos);
            EXPECT_NE(note.summary.find("行程校验未执行"), std::string::npos);
            EXPECT_TRUE(note.blocking);
        }
    }
    EXPECT_TRUE(found) << "缺少 L11 策略不可解析阻断结论";
    // 不产出行程确认（评估未执行——明示而非静默）。
    EXPECT_TRUE(report.confirmables.empty());
}

/**
 * @brief 结果组稳定排序：blockers/notes 按（层号→subject 字典序）。
 *
 * 验证：acceptance 1——"结果 blockers/warnings/confirmables 三组稳定排序
 * ——按层号→对象 id 字典序"。
 */
TEST(MdlReadiness, ResultGroupsStableOrderByLayerThenSubject)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-06"}, std::vector<std::string>{"NFR-COR-02"});

    ReadinessFixture f;
    // 两个限位区间违例关节（qmin≥qmax）——同层两条 coded 阻断。
    RobotDesign design;
    const core::ObjectId jointB = makeOid();
    const core::ObjectId jointA = makeOid();
    // 故意以 B 先、A 后的装配序——输出应按对象 id 字典序（与装配序无关）。
    design.joints.push_back(
        makeRevoluteJoint(jointB, "JB", 3.0, 1.0));  // qmin>qmax
    design.joints.push_back(
        makeRevoluteJoint(jointA, "JA", 2.0, 0.0));  // qmin>qmax
    design.links.push_back(makeLink(makeOid(), "base"));
    design.links.push_back(makeLink(makeOid(), "l1"));
    design.links.push_back(makeLink(makeOid(), "l2"));

    const ModelingWorkingSet ws{design, {}, makeOid(), {}, {}, {}, {}};
    const ModelReadinessReport report = f.checker.check(ws, CheckContext{});

    ASSERT_EQ(report.blockers.size(), std::size_t{2});
    for (const auto& b : report.blockers) {
        EXPECT_EQ(b.code, std::string(kMdlAssertLimitInterval));
    }
    // 字典序（与装配序无关——对象 id 字典序排序契约；身份为随机生成，
    // 断言相对序＝min/max——确定性由排序本身保证）。
    const std::string oidA = jointA.toCanonical();
    const std::string oidB = jointB.toCanonical();
    EXPECT_EQ(report.blockers[0].subject->toCanonical(), std::min(oidA, oidB));
    EXPECT_EQ(report.blockers[1].subject->toCanonical(), std::max(oidA, oidB));
    EXPECT_EQ(readinessStatusToken(report.status), "NotReady");
}

// =====================================================================
// ACC2 断言分域反例（V-15，MDL-06、AT-01）
// =====================================================================

/**
 * @brief V-15①：已提供质量 m≤0 → MDL-ASSERT-MASS-NONPOSITIVE。
 *
 * 验证：acceptance 2——就地阻断＋精确定位（subject=ObjectId＋localName＋
 * 比较型三要素）；缺失不触发（NotProvided 走预告——对照组）。
 */
TEST(MdlReadinessAssertionDomains, MassNonpositiveBlocksPrecisely)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-06"}, std::vector<std::string>{"AT-01"});

    ReadinessFixture f;
    RobotDesign design = makeSingleJointDesign(-1.0, 1.0);
    const core::ObjectId linkOid = design.links[1].objectId;
    design.links[1].body.mass = core::SourcedValue<double>::provided(
        -5.0, userProvenance());  // 已提供 m≤0——断言①触发

    const ModelingWorkingSet ws{design, {}, makeOid(), {}, {}, {}, {}};
    const ModelReadinessReport report = f.checker.check(ws, CheckContext{});

    EXPECT_EQ(readinessStatusToken(report.status), "NotReady");
    ASSERT_EQ(report.blockers.size(), std::size_t{1});
    const core::DiagnosticRecord& b = report.blockers[0];
    EXPECT_EQ(b.code, std::string(kMdlAssertMassNonpositive));
    // 精确定位三要素：subject=ObjectId＋localName＋比较型三要素。
    ASSERT_TRUE(b.subject.has_value());
    EXPECT_EQ(b.subject->toCanonical(), linkOid.toCanonical());
    ASSERT_TRUE(b.localName.has_value());
    EXPECT_EQ(*b.localName, "link1");
    ASSERT_TRUE(b.comparison.has_value());
    ASSERT_TRUE(b.comparison->actual.quantity.tryValue().has_value());
    EXPECT_DOUBLE_EQ(*b.comparison->actual.quantity.tryValue(), -5.0);
    EXPECT_EQ(b.comparison->actual.unit, *core::UnitToken::find("kg"));
    ASSERT_TRUE(b.comparison->expected.quantity.tryValue().has_value());
    EXPECT_DOUBLE_EQ(*b.comparison->expected.quantity.tryValue(), 0.0);
    EXPECT_EQ(b.comparison->expected.unit, *core::UnitToken::find("kg"));
    // L5 层明细＝阻断（层归属正确）。
    EXPECT_FALSE(report.layers[5].passed);
}

/**
 * @brief V-15②：惯量非 SPD → MDL-ASSERT-INERTIA-NOT-SPD（比较型三要素
 *        actual=λmin、expected=0）。
 */
TEST(MdlReadinessAssertionDomains, InertiaNotSpdBlocks)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-06"}, std::vector<std::string>{"AT-01"});

    ReadinessFixture f;
    RobotDesign design = makeSingleJointDesign(-1.0, 1.0);
    InertiaTensor inertia;  // diag(-1, 2, 2)——λmin=-1 非正定
    inertia.ixx = -1.0;
    inertia.iyy = 2.0;
    inertia.izz = 2.0;
    const core::ObjectId linkOid = design.links[1].objectId;
    design.links[1].body.inertia = core::SourcedValue<InertiaTensor>::provided(
        inertia, userProvenance());

    const ModelingWorkingSet ws{design, {}, makeOid(), {}, {}, {}, {}};
    const ModelReadinessReport report = f.checker.check(ws, CheckContext{});

    ASSERT_EQ(report.blockers.size(), std::size_t{1});
    EXPECT_EQ(report.blockers[0].code, std::string(kMdlAssertInertiaNotSpd));
    EXPECT_EQ(report.blockers[0].subject->toCanonical(), linkOid.toCanonical());
    ASSERT_TRUE(report.blockers[0].comparison.has_value());
    ASSERT_TRUE(report.blockers[0].comparison->actual.quantity.tryValue().has_value());
    EXPECT_DOUBLE_EQ(*report.blockers[0].comparison->actual.quantity.tryValue(), -1.0);
    EXPECT_EQ(report.blockers[0].comparison->actual.unit, *core::UnitToken::find("kg*m^2"));
    EXPECT_EQ(readinessStatusToken(report.status), "NotReady");
}

/**
 * @brief V-15③：qmin≥qmax → MDL-ASSERT-LIMIT-INTERVAL（actual=qmin、
 *        expected=qmax、rad）。
 */
TEST(MdlReadinessAssertionDomains, LimitIntervalBlocks)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-06", "MDL-12"}, std::vector<std::string>{"AT-01"});

    ReadinessFixture f;
    const RobotDesign design = makeSingleJointDesign(2.0 * kPi, kPi);  // qmin>qmax
    const core::ObjectId jointOid = design.joints[0].objectId;

    const ModelingWorkingSet ws{design, {}, makeOid(), {}, {}, {}, {}};
    const ModelReadinessReport report = f.checker.check(ws, CheckContext{});

    ASSERT_EQ(report.blockers.size(), std::size_t{1});
    EXPECT_EQ(report.blockers[0].code, std::string(kMdlAssertLimitInterval));
    EXPECT_EQ(report.blockers[0].subject->toCanonical(), jointOid.toCanonical());
    ASSERT_TRUE(report.blockers[0].localName.has_value());
    EXPECT_EQ(*report.blockers[0].localName, "J1");
    ASSERT_TRUE(report.blockers[0].comparison.has_value());
    EXPECT_DOUBLE_EQ(*report.blockers[0].comparison->actual.quantity.tryValue(),
                     2.0 * kPi);
    EXPECT_DOUBLE_EQ(*report.blockers[0].comparison->expected.quantity.tryValue(), kPi);
    EXPECT_EQ(report.blockers[0].comparison->actual.unit, *core::UnitToken::find("rad"));
}

/**
 * @brief V-15④：物性全缺失＋含已确认有限范围 continuous → 可应用
 *        （ReadyWithNotes——缺失走预告、continuous 豁免限位断言与行程
 *        检查；V15-04/MDL-12）。
 */
TEST(MdlReadinessAssertionDomains, MissingPhysicsWithConfirmedContinuousApplicable)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-06", "MDL-12"}, std::vector<std::string>{});

    ReadinessFixture f;
    RobotDesign design;
    // continuous＋已确认有限工作范围——豁免限位断言（无 bounds）与行程。
    design.joints.push_back(makeContinuousJoint(makeOid(), "Jspin", true));
    design.links.push_back(makeLink(makeOid(), "base"));   // 物性全缺失
    design.links.push_back(makeLink(makeOid(), "link1"));  // 物性全缺失
    f.bindNames(design);

    const ModelingWorkingSet ws{design, {}, makeOid(), {}, {}, {}, {}};
    const ModelReadinessReport report = f.checker.check(ws, CheckContext{});

    // 可应用（无 Blocking；缺失＝预告 Warning；continuous 无待确认）。
    EXPECT_EQ(readinessStatusToken(report.status), "ReadyWithNotes");
    EXPECT_TRUE(report.blockers.empty());
    EXPECT_FALSE(report.warnings.empty());  // 缺失预告在（DataInsufficient 面）
    EXPECT_TRUE(report.confirmables.empty());  // continuous 行程豁免
}

// =====================================================================
// ACC3 行程上限确认（V-16，MDL-06④/M-10、SA-15）
// =====================================================================

/**
 * @brief V-16：±1080° 多圈关节（±6π rad，行程 12π）→ 默认 4π 阈值超限
 *        → MDL-06-TRAVEL-LIMIT ConfirmableFinding（实际行程/阈值/单位
 *        rad 三要素；阈值唯一来自 policy JointThresholds 默认——本地无
 *        第二常量）。
 */
TEST(MdlReadinessTravelLimit, MultiTurnJointProducesConfirmableFinding)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-06", "SA-15"},
                  std::vector<std::string>{"AT-01"});

    ReadinessFixture f;
    const policy::EngineeringPolicySet policy = makePolicy(4.0 * kPi);  // 默认口径
    const RobotDesign design = makeSingleJointDesign(-6.0 * kPi, 6.0 * kPi);  // ±1080°
    const core::ObjectId jointOid = design.joints[0].objectId;
    f.bindNames(design);

    const ModelingWorkingSet ws{design, {}, makeOid(), {}, {}, {}, {}};
    const ModelReadinessReport report = f.checker.check(ws, CheckContext{&policy, true, std::nullopt});

    // 超限不阻断状态（Confirmable 级——SA-15 待确认）。
    EXPECT_NE(readinessStatusToken(report.status), "NotReady");
    EXPECT_TRUE(report.blockers.empty());
    ASSERT_EQ(report.confirmables.size(), std::size_t{1});
    const core::ConfirmableFinding& finding = report.confirmables[0];
    EXPECT_EQ(finding.record.code, std::string(kMdl06TravelLimit));
    // 比较型三要素：actual=12π rad、expected=4π rad。
    ASSERT_TRUE(finding.record.comparison.has_value());
    ASSERT_TRUE(finding.record.comparison->actual.quantity.tryValue().has_value());
    EXPECT_DOUBLE_EQ(*finding.record.comparison->actual.quantity.tryValue(), 12.0 * kPi);
    ASSERT_TRUE(finding.record.comparison->expected.quantity.tryValue().has_value());
    EXPECT_DOUBLE_EQ(*finding.record.comparison->expected.quantity.tryValue(), 4.0 * kPi);
    EXPECT_EQ(finding.record.comparison->actual.unit, *core::UnitToken::find("rad"));
    EXPECT_EQ(finding.record.comparison->expected.unit, *core::UnitToken::find("rad"));
    // 精确定位＋待确认态＋阈值来源留痕（policyContentId 入 cause——四元组
    // 组装素材，§6.7）。
    EXPECT_EQ(finding.record.subject->toCanonical(), jointOid.toCanonical());
    EXPECT_EQ(finding.state, core::ConfirmationState::Pending);
    EXPECT_NE(finding.record.cause.find("policyContentId=cid-"), std::string::npos);
}

/**
 * @brief 行程恰等于阈值不超限（T＝L 边界含于合规侧——policy §7.4/D-08）＋
 *        策略配置更大阈值后正常放行（无待确认——"policy 配置阈值后正常
 *        放行"，acceptance 3）。
 */
TEST(MdlReadinessTravelLimit, BoundaryTravelAndConfiguredThresholdPass)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-06", "ARC-05"}, std::vector<std::string>{});

    ReadinessFixture f;
    const RobotDesign design = makeSingleJointDesign(-2.0 * kPi, 2.0 * kPi);  // 行程 4π
    f.bindNames(design);
    const ModelingWorkingSet ws{design, {}, makeOid(), {}, {}, {}, {}};

    // ① T＝L（默认 4π）：不超限——无发现。
    const policy::EngineeringPolicySet defaultPolicy = makePolicy(4.0 * kPi);
    const ModelReadinessReport atBoundary =
        f.checker.check(ws, CheckContext{&defaultPolicy, true, std::nullopt});
    EXPECT_TRUE(atBoundary.confirmables.empty());
    EXPECT_TRUE(atBoundary.blockers.empty());

    // ② 配置 16π：12π 场景亦合规——"配置阈值后正常放行"。
    const policy::EngineeringPolicySet loosePolicy = makePolicy(16.0 * kPi);
    const RobotDesign multiTurn = makeSingleJointDesign(-6.0 * kPi, 6.0 * kPi);
    f.bindNames(multiTurn);
    const ModelingWorkingSet ws2{multiTurn, {}, makeOid(), {}, {}, {}, {}};
    const ModelReadinessReport configured =
        f.checker.check(ws2, CheckContext{&loosePolicy, true, std::nullopt});
    EXPECT_TRUE(configured.confirmables.empty());
    EXPECT_EQ(readinessStatusToken(configured.status), "ReadyWithNotes");
}
