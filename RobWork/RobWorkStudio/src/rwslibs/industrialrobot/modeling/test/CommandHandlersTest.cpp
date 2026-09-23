/**
 * @file   CommandHandlersTest.cpp
 * @brief  建模命令处理器族用例组（MdlCommandHandlers）——契约
 *         tasks/foundation/WP-13-T08.json acceptance 4 的具名自证＋
 *         acceptance 1/3 的 prepare 侧载体：
 *
 *   ACC4 五命令注册与 prepare 管线（§9.3）：commandType 无点形态
 *        （O-35 裁决——apply-robot-design 等）＋requiresDualCompile 逐命令
 *        表（apply-named-poses=false）＋prepare：decode 失败→
 *        RejectedInvalidInput、基线防御性复核（expectedRevision 不一致
 *        fail-fast）、新对象 ObjectId 经 ctx.objectId() 分配、inverse
 *        快照式逆命令载荷（受影响对象前一 (oid,cv) canonical 字节集——
 *        D-MDL-9）、中文命令摘要
 *   ACC1/ACC3 prepare 侧载体：共用 AssertionSuite 的断言在 prepare 内
 *        就地阻止（RejectedHardAssert＋MDL-ASSERT-*）与行程 Confirmable
 *        产出（④端口阈值——provider 调用观测）
 *
 * 设计依据：units/modeling.md §9.3/§9.4.8、units/project.md §5.3/§6.9；
 * shared 夹具＝test/CommandFixtures.hpp。
 */

#include "CommandFixtures.hpp"

#include <sdurws/ird/modeling/DiagCodes.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <stdexcept>
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

/// 夹具：服务集＋五处理器装配（每用例独立）。
struct HandlerFixture {
    std::unique_ptr<policy::IJointLimitEvaluator> evaluator =
        policy::makeJointLimitEvaluator();
    TestNameContext names;
    TestPolicyProvider provider;
    TestQueryPort query;
    MockCompilePort compile;
    HandlerServices services{
        AssertionSuite::Ports{evaluator.get(), &names}, &provider, makeOid(),
        std::nullopt};

    /// 名称解析装配（行程评估的 runtimeName 源）。
    void bindNames(const RobotDesign& design)
    {
        for (const JointEntry& j : design.joints) {
            names.byId[j.objectId] = "Robot/" + j.localName;
        }
    }
};

/// 载荷单槽装配（新对象——全零身份）。
inline PayloadObjectSlot newSlot(std::string token, const ObjectVariant& object)
{
    PayloadObjectSlot slot;
    slot.allocateNew = true;                       // 新对象——prepare 经 ctx 取号
    slot.objectId = core::ObjectId{};              // 全零保留值（未分配标记）
    slot.objectTypeToken = std::move(token);
    slot.objectBytes = encodeVariant(object);
    return slot;
}

/// 载荷单槽装配（既有对象替换——显式基线身份）。
inline PayloadObjectSlot replaceSlot(const core::ObjectId& oid, std::string token,
                                     const ObjectVariant& object)
{
    PayloadObjectSlot slot;
    slot.allocateNew = false;
    slot.objectId = oid;
    slot.objectTypeToken = std::move(token);
    slot.objectBytes = encodeVariant(object);
    return slot;
}

}  // namespace

// =====================================================================
// 命令面与载荷编解码（§9.3 命令清单＋decode 段）
// =====================================================================

/**
 * @brief 五命令 token 表：无点形态（O-35 裁决——服从 §4.4.4 冻结语法
 *        ^[a-z0-9-]{3,64}）＋requiresDualCompile 逐命令表
 *        （apply-named-poses=false，其余 true）。
 *
 * 验证：acceptance 4 第一句（commandType 无点形态＋逐命令表）。
 */
TEST(MdlCommandHandlers, CommandTokenTableDotFreeAndDualCompileTable)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-06", "ARC-01"}, std::vector<std::string>{});

    HandlerFixture f;
    ApplyRobotDesignHandler robot(f.services);
    ApplyToolDefinitionHandler tool(f.services);
    ApplySceneObjectsHandler scene(f.services);
    ApplyNamedPosesHandler poses(f.services);
    ApplyDrivetrainDesignHandler drivetrain(f.services);

    // 无点 token 表（§9.3 命令清单——字面逐一对账）。
    EXPECT_EQ(robot.commandType(), "apply-robot-design");
    EXPECT_EQ(tool.commandType(), "apply-tool-definition");
    EXPECT_EQ(scene.commandType(), "apply-scene-objects");
    EXPECT_EQ(poses.commandType(), "apply-named-poses");
    EXPECT_EQ(drivetrain.commandType(), "apply-drivetrain-design");
    for (const std::string& token :
         {robot.commandType(), tool.commandType(), scene.commandType(),
          poses.commandType(), drivetrain.commandType()}) {
        // 无点（find 返回 npos＝未找到点——O-35 裁决的语法面）。
        EXPECT_EQ(token.find('.'), std::string::npos)
            << "token 含点（违 O-35 无点裁决）: " << token;
        EXPECT_GE(token.size(), std::size_t{3});
        EXPECT_LE(token.size(), std::size_t{64});
    }

    // 载荷版本演进点（§9.4.8——currentPayloadVersion final；受理集合）。
    EXPECT_EQ(robot.currentPayloadVersion(), kCommandPayloadVersion);
    EXPECT_EQ(poses.currentPayloadVersion(), kCommandPayloadVersion);
}

/**
 * @brief 载荷编解码往返＋破损面（encode→tryDecode 逐字段相等；截断/
 *        版本不受理/尾随字节→nullopt——NFR-DEP-04）。
 */
TEST(MdlCommandHandlers, PayloadCodecRoundtripAndDamage)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-06"}, std::vector<std::string>{"NFR-DEP-04"});

    const RobotDesign design = makeSingleJointDesign(-1.0, 1.0);
    CommandPayload payload;
    payload.mode = CommandPayload::Mode::Apply;
    payload.objects.push_back(newSlot(std::string(kRobotDesignObjectType), design));

    // 往返逐字段相等（确定性——NFR-COR-02）。
    const auto bytes = encodeCommandPayload(payload);
    const auto decoded = tryDecodeCommandPayload(bytes);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_TRUE(*decoded == payload);

    // 截断＝破损。
    EXPECT_FALSE(tryDecodeCommandPayload(std::vector<std::uint8_t>(
        bytes.begin(), bytes.begin() + static_cast<long>(bytes.size() / 2))).has_value());
    // 空字节＝破损。
    EXPECT_FALSE(tryDecodeCommandPayload({}).has_value());
    // 尾随字节＝破损（追加垃圾）。
    auto trailing = bytes;
    trailing.push_back(0xFF);
    EXPECT_FALSE(tryDecodeCommandPayload(trailing).has_value());
    // 版本不受理（改版本字节——NFR-DEP-04 拒绝猜测）。
    auto wrongVersion = bytes;
    wrongVersion[sizeof("IRDMCP1")] = 0x63;  // 版本字段低位字节 1→99
    EXPECT_FALSE(tryDecodeCommandPayload(wrongVersion).has_value());
}

/**
 * @brief 五命令族 L5 装配注册（project.md §6.5）：全 token 可查＋重复
 *        注册边界拒绝（fail-fast）。
 */
TEST(MdlCommandHandlers, RegistryRegistrationAndDuplicateRejection)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-01"}, std::vector<std::string>{});

    HandlerFixture f;
    project::HandlerRegistry registry;
    registerModelingCommandHandlers(registry, f.services);

    EXPECT_EQ(registry.size(), std::size_t{5});
    EXPECT_NE(registry.find("apply-robot-design"), nullptr);
    EXPECT_NE(registry.find("apply-tool-definition"), nullptr);
    EXPECT_NE(registry.find("apply-scene-objects"), nullptr);
    EXPECT_NE(registry.find("apply-named-poses"), nullptr);
    EXPECT_NE(registry.find("apply-drivetrain-design"), nullptr);
    EXPECT_EQ(registry.find("apply-nonexistent"), nullptr);

    // 同族二次注册＝装配错误（std::invalid_argument——注册表边界拒绝）。
    EXPECT_THROW(registerModelingCommandHandlers(registry, f.services),
                 std::invalid_argument);
}

// =====================================================================
// prepare 管线（§9.3——decode/基线/断言/计划组装）
// =====================================================================

/**
 * @brief 首次 apply-robot-design：decode→基线（空闭包）→断言通过→计划
 *        组装——新对象经 ctx.objectId() 取号、中文摘要、首次应用不可逆
 *        声明（nullopt——越过首修订无撤销）、requiresDualCompile=true、
 *        处理器零编译调用（S5 编排归 project——V-19 mock 观测面）。
 */
TEST(MdlCommandPrepare, FirstApplyPlansWithAllocatedIdsAndSummary)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-06", "ARC-01"},
                  std::vector<std::string>{"AT-01"});

    HandlerFixture f;
    f.provider.policyToReturn.emplace(makePolicy(4.0 * kPi));  // ④端口应答（根命令行程校验域）
    const project::RevisionView baseline = makeBaselineView(core::RevisionId::generate());
    f.query.view = baseline;

    const RobotDesign design = makeSingleJointDesign(-2.0 * kPi, 2.0 * kPi);
    f.bindNames(design);
    CommandPayload payload;
    payload.objects.push_back(newSlot(std::string(kRobotDesignObjectType), design));

    ApplyRobotDesignHandler handler(f.services);
    project::HandlerContext ctx(f.query, &f.compile, nullptr);
    project::CommandPlan plan;
    std::vector<core::DiagnosticRecord> diags;

    const auto outcome = handler.prepare(
        ctx, makeEnvelope(baseline, std::string(kCmdApplyRobotDesign), payload),
        baseline, plan, diags);

    ASSERT_EQ(outcome, project::PrepareOutcome::Planned);
    // 计划面：根写入（身份已取号）＋双编译声明＋中文摘要。
    ASSERT_EQ(plan.objectWrites.size(), std::size_t{1});
    EXPECT_EQ(plan.objectWrites[0].objectTypeToken, std::string(kRobotDesignObjectType));
    ASSERT_TRUE(plan.objectWrites[0].objectId.has_value());
    EXPECT_TRUE(plan.objectWrites[0].objectId->isValid());
    EXPECT_TRUE(plan.requiresDualCompile);  // §9.3 表行 1
    EXPECT_NE(plan.summary.find("应用机器人设计"), std::string::npos);  // 中文摘要
    EXPECT_FALSE(plan.summary.empty());
    // 首次应用：受影响对象无前版——不可逆声明（nullopt）。
    EXPECT_FALSE(plan.inverseCommandType.has_value());
    EXPECT_FALSE(plan.inversePayloadCanonical.has_value());
    // 待确认集为空（行程 4π 边界合规——T＝L 不超限）。
    EXPECT_TRUE(plan.confirmableFindings.empty());
    // 处理器零编译调用（双编译编排归 project S5——mock 观测）。
    EXPECT_EQ(f.compile.callCount, 0);
}

/**
 * @brief 快照式逆命令载荷（D-MDL-9）：基线已有根的替换应用→inverse 声明
 *        ＝同 commandType 的 Restore 载荷，槽＝受影响对象前一版本
 *        (oid, canonical 字节) 原样快照。
 */
TEST(MdlCommandPrepare, ReplaceApplyProducesSnapshotInversePayload)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-06"}, std::vector<std::string>{"D-MDL-9"});

    HandlerFixture f;
    // 基线闭包：根对象 r0。
    f.provider.policyToReturn.emplace(makePolicy(4.0 * kPi));  // ④端口应答（根命令行程校验域）
    const RobotDesign oldDesign = makeSingleJointDesign(-1.0, 1.0);
    const core::ObjectId rootOid = makeOid();
    f.query.view = makeBaselineView(core::RevisionId::generate());
    f.query.addObject(rootOid, std::string(kRobotDesignObjectType), encodeVariant(oldDesign));

    // 替换载荷：同身份新值（displayName 变化——不进编译身份的呈现字段）。
    RobotDesign newDesign = oldDesign;
    newDesign.displayName = "更名后的机器人";
    f.bindNames(newDesign);  // 行程评估的 runtimeName 解析源（R-4 注入面）
    CommandPayload payload;
    payload.objects.push_back(
        replaceSlot(rootOid, std::string(kRobotDesignObjectType), newDesign));

    ApplyRobotDesignHandler handler(f.services);
    project::HandlerContext ctx(f.query, &f.compile, nullptr);
    project::CommandPlan plan;
    std::vector<core::DiagnosticRecord> diags;
    const auto outcome = handler.prepare(
        ctx, makeEnvelope(f.query.view, std::string(kCmdApplyRobotDesign), payload),
        f.query.view, plan, diags);

    ASSERT_EQ(outcome, project::PrepareOutcome::Planned);
    // 逆命令声明：同 commandType＋Restore 快照载荷。
    ASSERT_TRUE(plan.inverseCommandType.has_value());
    EXPECT_EQ(*plan.inverseCommandType, std::string(kCmdApplyRobotDesign));
    ASSERT_TRUE(plan.inversePayloadCanonical.has_value());
    const auto inverse = tryDecodeCommandPayload(*plan.inversePayloadCanonical);
    ASSERT_TRUE(inverse.has_value());
    EXPECT_EQ(inverse->mode, CommandPayload::Mode::Restore);
    ASSERT_EQ(inverse->objects.size(), std::size_t{1});
    // 逆槽＝受影响对象前一 (oid, canonical 字节集) 原样（D-MDL-9）。
    EXPECT_EQ(inverse->objects[0].objectId.toCanonical(), rootOid.toCanonical());
    EXPECT_FALSE(inverse->objects[0].allocateNew);
    EXPECT_TRUE(inverse->objects[0].objectBytes == encodeVariant(oldDesign));
}

/**
 * @brief apply-named-poses：requiresDualCompile=false（不进 Description——
 *        §9.3 表行 4）＋根 poseSetRef 回填（"至多一份"）。
 */
TEST(MdlCommandPrepare, NamedPosesSkipsDualCompileAndBackfillsRef)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-06"}, std::vector<std::string>{});

    HandlerFixture f;
    const RobotDesign root = makeSingleJointDesign(-1.0, 1.0);
    const core::ObjectId rootOid = makeOid();
    f.query.view = makeBaselineView(core::RevisionId::generate());
    f.query.addObject(rootOid, std::string(kRobotDesignObjectType), encodeVariant(root));

    PoseSet poses;
    poses.objectId = core::ObjectId{};  // 新对象（保留值——prepare 取号）
    PoseSetEntry entry;
    entry.key = "home";
    entry.jointConfiguration = {0.0};
    poses.entries.push_back(entry);
    CommandPayload payload;
    payload.objects.push_back(newSlot(std::string(kNamedPoseSetObjectType), poses));

    ApplyNamedPosesHandler handler(f.services);
    project::HandlerContext ctx(f.query, &f.compile, nullptr);
    project::CommandPlan plan;
    std::vector<core::DiagnosticRecord> diags;
    const auto outcome = handler.prepare(
        ctx, makeEnvelope(f.query.view, std::string(kCmdApplyNamedPoses), payload),
        f.query.view, plan, diags);

    ASSERT_EQ(outcome, project::PrepareOutcome::Planned);
    // 双编译表：apply-named-poses＝false（§9.3 命令清单）。
    EXPECT_FALSE(plan.requiresDualCompile);
    // 写入集＝位姿集＋根（poseSetRef 回填）。
    ASSERT_EQ(plan.objectWrites.size(), std::size_t{2});
    bool rootRefSet = false;
    for (const project::ObjectWrite& w : plan.objectWrites) {
        if (w.objectTypeToken == std::string(kRobotDesignObjectType)) {
            RobotDesignCodec codec;
            auto decoded = codec.decode(w.payloadCanonical, kCurrentFormatVersion);
            ASSERT_TRUE(decoded.ok());
            const auto& written = std::get<RobotDesign>(decoded.get());
            ASSERT_TRUE(written.poseSetRef.has_value());
            EXPECT_TRUE(written.poseSetRef->isValid());  // 已回填取号身份
            rootRefSet = true;
        }
    }
    EXPECT_TRUE(rootRefSet);
}

/**
 * @brief decode 失败→RejectedInvalidInput（§9.3 管线第一分支；
 *        invalid-payload）。
 */
TEST(MdlCommandPrepare, MalformedPayloadRejectedInvalidInput)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-06"}, std::vector<std::string>{});

    HandlerFixture f;
    const project::RevisionView baseline = makeBaselineView(core::RevisionId::generate());
    f.query.view = baseline;

    ApplyRobotDesignHandler handler(f.services);
    project::HandlerContext ctx(f.query, &f.compile, nullptr);
    project::CommandPlan plan;
    std::vector<core::DiagnosticRecord> diags;

    project::CommandEnvelope envelope = makeEnvelope(
        baseline, std::string(kCmdApplyRobotDesign), CommandPayload{});
    envelope.payloadCanonical = {0x01, 0x02, 0x03};  // 非 canonical 载荷（破损）
    const auto outcome = handler.prepare(ctx, envelope, baseline, plan, diags);
    EXPECT_EQ(outcome, project::PrepareOutcome::RejectedInvalidInput);
}

/**
 * @brief 基线防御性复核：expectedRevision 与 baseSnapshot 不一致＝调用方
 *        契约违约 fail-fast（S2 已拦截过期基线——§9.3 处理器内纵深防线）。
 */
TEST(MdlCommandPrepare, BaselineDefensiveRecheckFailsFast)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-06"}, std::vector<std::string>{"PM-04"});

    HandlerFixture f;
    const project::RevisionView baseline = makeBaselineView(core::RevisionId::generate());
    f.query.view = baseline;

    const RobotDesign design = makeSingleJointDesign(-1.0, 1.0);
    CommandPayload payload;
    payload.objects.push_back(newSlot(std::string(kRobotDesignObjectType), design));

    ApplyRobotDesignHandler handler(f.services);
    project::HandlerContext ctx(f.query, &f.compile, nullptr);
    project::CommandPlan plan;
    std::vector<core::DiagnosticRecord> diags;

    project::CommandEnvelope envelope =
        makeEnvelope(baseline, std::string(kCmdApplyRobotDesign), payload);
    envelope.expectedRevision = core::RevisionId::generate();  // 与基线不一致
    EXPECT_THROW((void)handler.prepare(ctx, envelope, baseline, plan, diags),
                 std::invalid_argument);
}

/**
 * @brief ACC3 prepare 侧载体：行程超限应用→Planned＋ConfirmableFinding
 *        （④端口 resolvePolicy 被消费——阈值唯一来源观测；M-10/SA-15）。
 */
TEST(MdlCommandPrepare, TravelExceededProducesConfirmableViaPolicyPort)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-06", "SA-15"},
                  std::vector<std::string>{"AT-01"});

    HandlerFixture f;
    f.provider.policyToReturn.emplace(makePolicy(4.0 * kPi));  // emplace——策略对象不可赋值（const 成员）  // ④端口应答（默认口径）
    const project::RevisionView baseline = makeBaselineView(core::RevisionId::generate());
    f.query.view = baseline;

    const RobotDesign design = makeSingleJointDesign(-6.0 * kPi, 6.0 * kPi);  // ±1080°
    f.bindNames(design);
    CommandPayload payload;
    payload.objects.push_back(newSlot(std::string(kRobotDesignObjectType), design));

    ApplyRobotDesignHandler handler(f.services);
    project::HandlerContext ctx(f.query, &f.compile, nullptr);
    project::CommandPlan plan;
    std::vector<core::DiagnosticRecord> diags;
    const auto outcome = handler.prepare(
        ctx, makeEnvelope(baseline, std::string(kCmdApplyRobotDesign), payload),
        baseline, plan, diags);

    // 超限不就地阻断——转待确认集（确认编排归 project S4）。
    ASSERT_EQ(outcome, project::PrepareOutcome::Planned);
    ASSERT_EQ(plan.confirmableFindings.size(), std::size_t{1});
    EXPECT_EQ(plan.confirmableFindings[0].record.code, std::string(kMdl06TravelLimit));
    EXPECT_EQ(f.provider.resolveCalls, 1);  // ④端口被消费（阈值唯一来源通道）
    // 摘要含确认留痕段（§9.3"确认留痕"）。
    EXPECT_NE(plan.summary.find("行程上限待确认"), std::string::npos);
}

/**
 * @brief ACC1/ACC2 prepare 侧载体：断言域就地阻止——continuous 工作范围
 *        未确认→RejectedHardAssert＋MDL-ASSERT-RANGE-NOT-FINITE 逐项定位
 *        （解码门可过的合法字节——硬断言面拦截，非载荷破损）。
 */
TEST(MdlCommandPrepare, HardAssertBlocksWithPreciseDiagnostic)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-06", "MDL-12"}, std::vector<std::string>{});

    HandlerFixture f;
    const project::RevisionView baseline = makeBaselineView(core::RevisionId::generate());
    f.query.view = baseline;

    // continuous 且工作范围未确认——解码门可过（不变量不要求提供）、
    // 断言域阻断（MDL-12）。
    RobotDesign design;
    design.joints.push_back(makeContinuousJoint(makeOid(), "Jspin", false));
    design.links.push_back(makeLink(makeOid(), "base"));
    design.links.push_back(makeLink(makeOid(), "link1"));
    CommandPayload payload;
    payload.objects.push_back(newSlot(std::string(kRobotDesignObjectType), design));

    ApplyRobotDesignHandler handler(f.services);
    project::HandlerContext ctx(f.query, &f.compile, nullptr);
    project::CommandPlan plan;
    std::vector<core::DiagnosticRecord> diags;
    const auto outcome = handler.prepare(
        ctx, makeEnvelope(baseline, std::string(kCmdApplyRobotDesign), payload),
        baseline, plan, diags);

    ASSERT_EQ(outcome, project::PrepareOutcome::RejectedHardAssert);
    ASSERT_FALSE(diags.empty());
    bool found = false;
    for (const auto& d : diags) {
        if (d.code == std::string(kMdlAssertRangeNotFinite)) {
            found = true;
            ASSERT_TRUE(d.subject.has_value());  // 精确定位（对象 ObjectId）
            ASSERT_TRUE(d.localName.has_value());
            EXPECT_EQ(*d.localName, "Jspin");
        }
    }
    EXPECT_TRUE(found) << "缺少 MDL-ASSERT-RANGE-NOT-FINITE 定位诊断";
    // 无修订力（plan 零消费面——project 不消费拒绝态计划）。
    EXPECT_TRUE(plan.objectWrites.empty());
}
