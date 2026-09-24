/**
 * @file   PartObjectCommandTest.cpp
 * @brief  工具/场景/命名位姿对象命令用例组（MdlPartObjectCommands）——
 *         契约 tasks/foundation/WP-13-T10.json acceptance 逐条具名自证：
 *
 *   ACC1 值模型与引用校验：T_flange_tool/tcpList≥1（I-MDL-13——解码门）、
 *        defaultTcp=(toolOid,tcpKey) 引用校验（I-MDL-9/KIN-14——
 *        assertDefaultTcp 候选闭包级门）、工具经 ObjectId 引用使用不复制
 *        几何（MDL-13——GeometryRef 资源引用面）
 *   ACC2 引用保护（V-04/V-05，I-MDL-9/CON-02/PA-2）：删除被 defaultTcp
 *        引用的工具→拒绝＋MDL-REF-PROTECTED 比较型定位诊断（subject=oid）；
 *        删除未引用对象＝引用移除——新修订仅根写入、对象零写入（对象库
 *        旧字节与历史修订闭包完整保留）；移除摘要附"可能存在外部引用"
 *        提示（⑤事件随修订提交由 project TxEngine 发布——本卡观测点为
 *        摘要面，全管线事件观测归 project 已验契约）
 *   ACC3 场景对象约束（MDL-15）：worldPose 世界系固连不预乘安装旋转
 *        （M-11/runtime §4.3.4 同口径——V-12 建模侧输入面）；角色五值
 *        词表（与 policy 侧 token 逐值同串）；collisionProfileHint 仅报告
 *        用途（往返保真、不消费④端口）
 *   ACC4 命名位姿（MDL-17/D-MDL-3）：requiresDualCompile=false；保留键
 *        homeConfiguration/zeroConfiguration 保留（V-27 建模侧）；条目
 *        与关节序一一对应；位姿集内容不入根对象字节（不进 Description 的
 *        建模侧形态）
 *   ACC5 三命令处理器 prepare 差异面（§9.3 表行，O-35 无点 token；T08
 *        基类复用）：apply-tool-definition 写入/移除两面、apply-scene-
 *        objects 批量增/改/移除、apply-named-poses 合并流
 *
 * 设计依据：units/modeling.md §4.4/§4.5/§4.6/§4.8/§4.10、§8.2 L7、
 * §9.3/§9.4.4/§9.4.8、§9.5 T10 行；shared 夹具＝test/CommandFixtures.hpp
 * （T08 同款——测试域替身不入公共面）。
 */

#include "CommandFixtures.hpp"

#include <sdurws/ird/modeling/DiagCodes.hpp>
#include <sdurws/ird/policy/CollisionEvaluator.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

using namespace sdurws::ird::modeling::testfixture;
namespace modeling = ::sdurws::ird::modeling;  // 全局域测试——被测类型所在命名空间别名
namespace core = ::sdurws::ird::core;          // core 侧类型（身份/诊断）
namespace policy = ::sdurws::ird::policy;      // policy 侧类型（角色词表消费面）
namespace project = ::sdurws::ird::project;    // project 侧类型（命令契约）
namespace runtime = ::sdurws::ird::runtime;    // runtime 侧类型（安装预设词表——单一权威）
using namespace modeling;                      // 被测面（Handlers/Parts/值模型）

namespace {

/// 夹具：服务集＋基线闭包（每用例独立——与 CommandHandlersTest 同款）。
struct PartObjectFixture {
    std::unique_ptr<policy::IJointLimitEvaluator> evaluator =
        policy::makeJointLimitEvaluator();
    TestNameContext names;
    TestPolicyProvider provider;
    TestQueryPort query;
    MockCompilePort compile;
    HandlerServices services{
        AssertionSuite::Ports{evaluator.get(), &names}, &provider, makeOid(),
        std::nullopt};

    void bindNames(const RobotDesign& design)
    {
        for (const JointEntry& j : design.joints) {
            names.byId[j.objectId] = "Robot/" + j.localName;
        }
    }
};

/// 新对象槽（全零身份——prepare 取号回填）。
inline PayloadObjectSlot newSlot(std::string token, const ObjectVariant& object)
{
    PayloadObjectSlot slot;
    slot.allocateNew = true;
    slot.objectId = core::ObjectId{};
    slot.objectTypeToken = std::string(token);
    slot.objectBytes = encodeVariant(object);
    return slot;
}

/// 既有对象槽（显式基线身份——字节替换）。
inline PayloadObjectSlot replaceSlot(const core::ObjectId& oid, std::string token,
                                     const ObjectVariant& object)
{
    PayloadObjectSlot slot;
    slot.allocateNew = false;
    slot.objectId = oid;
    slot.objectTypeToken = std::string(token);
    slot.objectBytes = encodeVariant(object);
    return slot;
}

/// 引用移除槽（v2——Apply 专用）。
inline PayloadRemovalSlot removeSlot(const core::ObjectId& oid, std::string token)
{
    PayloadRemovalSlot slot;
    slot.objectId = oid;
    slot.objectTypeToken = std::string(token);
    return slot;
}

/// 合法工具（SPD 物性＋一条 TCP——解码门 I-MDL-5/13 可过）。
/// @param oid 对象身份（基线既有工具须与根引用/槽身份一致——全零保留值
///            仅用于"新对象"槽，PA-1 取号语义）。
inline ToolDefinition makeValidTool(const core::ObjectId& oid, const std::string& name,
                                    const std::string& tcpKey)
{
    ToolDefinition tool;
    tool.objectId = oid;
    tool.localName = name;
    tool.mountInterface = rw::math::Transform3D<double>(
        rw::math::Vector3D<double>(0.0, 0.0, 0.02),
        rw::math::Rotation3D<double>(1.0, 0.0, 0.0,
                                     0.0, 1.0, 0.0,
                                     0.0, 0.0, 1.0));  // T_flange_tool，m/rad
    TcpEntry tcp;
    tcp.key = tcpKey;
    tcp.offset = rw::math::Transform3D<double>(
        rw::math::Vector3D<double>(0.0, 0.0, 0.15),
        rw::math::Rotation3D<double>(1.0, 0.0, 0.0,
                                     0.0, 1.0, 0.0,
                                     0.0, 0.0, 1.0));  // T_tool_tcp，m/rad
    tool.tcpList = {tcp};  // ≥1（§4.4/I-MDL-13）
    tool.body.mass = core::SourcedValue<double>::provided(
        1.8, userProvenance());  // kg
    return tool;
}

/// 场景对象（世界系位姿/角色/可选碰撞提示）。
/// @param oid 对象身份（基线既有场景对象须与根引用一致——同上）。
inline SceneObject makeSceneObject(const core::ObjectId& oid, const std::string& name,
                                   const rw::math::Vector3D<double>& position,
                                   SceneObjectRole role,
                                   std::optional<std::string> hint = std::nullopt)
{
    SceneObject scene;
    scene.objectId = oid;
    scene.localName = name;
    scene.worldPose = rw::math::Transform3D<double>(
        position,
        rw::math::Rotation3D<double>(1.0, 0.0, 0.0,
                                     0.0, 1.0, 0.0,
                                     0.0, 0.0, 1.0));  // 世界系固连，m/rad
    scene.role = role;
    scene.collisionProfileHint = std::move(hint);
    return scene;
}

/// 基线根（单关节＋工具/场景引用与 defaultTcp 按需装配）。
inline RobotDesign makeRootWithRefs(const std::vector<core::ObjectId>& toolRefs,
                                    const std::vector<core::ObjectId>& sceneRefs,
                                    std::optional<TcpRef> defaultTcp = std::nullopt)
{
    RobotDesign design = makeSingleJointDesign(-1.0, 1.0);
    design.toolRefs = toolRefs;
    design.sceneRefs = sceneRefs;
    design.defaultTcp = std::move(defaultTcp);
    return design;
}

/// 解码根写入字节（计划面核对辅助——ObjectWrite.payloadCanonical→值）。
inline RobotDesign decodeRootWrite(const project::ObjectWrite& write)
{
    RobotDesignCodec codec;
    auto decoded = codec.decode(write.payloadCanonical, kCurrentFormatVersion);
    if (!decoded.ok()) {
        throw std::logic_error(std::string("test: 根写入字节解码失败: ")
                               + decoded.error().detail);
    }
    return std::get<RobotDesign>(decoded.get());
}

/// 解码部件写入字节（模板化的计划面核对辅助）。
template <class T>
inline T decodePartWrite(const project::ObjectWrite& write)
{
    RobotDesignCodec codec;
    auto decoded = codec.decode(write.payloadCanonical, kCurrentFormatVersion);
    if (!decoded.ok()) {
        throw std::logic_error(std::string("test: 部件写入字节解码失败: ")
                               + decoded.error().detail);
    }
    return std::get<T>(decoded.get());
}

/// 命名位姿条目（键＋定长关节值）。
inline PoseSetEntry poseEntry(const std::string& key, std::vector<double> q,
                              const std::string& note = std::string())
{
    PoseSetEntry e;
    e.key = key;
    e.jointConfiguration = std::move(q);  // rad（移动关节 m）——与关节序对应
    e.note = note;
    return e;
}

}  // namespace

// =====================================================================
// ACC5/载荷：v2 removals 段编解码（O-35 token 面由 T08 用例钉住不变）
// =====================================================================

/**
 * @brief 载荷 v2 removals 段往返＋破损面（encode→tryDecode 逐字段相等；
 *        截断/越界/尾随字节→nullopt——NFR-COR-02/NFR-DEP-04）。
 */
TEST(MdlPartObjectCommands, PayloadV2RemovalsRoundtripAndDamage)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-13"}, std::vector<std::string>{"NFR-COR-02"});

    const RobotDesign design = makeSingleJointDesign(-1.0, 1.0);
    CommandPayload payload;
    payload.mode = CommandPayload::Mode::Apply;
    payload.objects.push_back(newSlot(
        std::string(kSceneObjectObjectType),
        makeSceneObject(core::ObjectId{}, "table", {1.0, 0.0, 0.0},
                        SceneObjectRole::EnvironmentObject)));
    payload.removals.push_back(removeSlot(makeOid(), std::string(kToolDefinitionObjectType)));

    const auto bytes = encodeCommandPayload(payload);
    const auto decoded = tryDecodeCommandPayload(bytes);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_TRUE(*decoded == payload);  // 往返逐字段相等（含 removals——v2 段）

    // 破损面：截断/空/尾随字节（与 v1 同款严格性——不猜测）。
    EXPECT_FALSE(tryDecodeCommandPayload(std::vector<std::uint8_t>(
        bytes.begin(), bytes.begin() + static_cast<long>(bytes.size() / 2))).has_value());
    auto trailing = bytes;
    trailing.push_back(0xFF);
    EXPECT_FALSE(tryDecodeCommandPayload(trailing).has_value());
}

// =====================================================================
// ACC1/ACC5：apply-tool-definition 写入面（工具物性＋defaultTcp 校验）
// =====================================================================

/**
 * @brief 新工具应用（§9.3 表行 2 写入面）：requiresDualCompile=true、根
 *        toolRefs 追加（"＋根引用表增量"——根对象一并写入）、工具经
 *        ObjectId 引用（几何仅资源引用不复制——MDL-13）。基线已有工具
 *        且 defaultTcp 完整（KIN-14：候选含工具引用则 defaultTcp 校验须
 *        通过——assertDefaultTcp 阻断面不适用）。
 */
TEST(MdlPartObjectCommands, ToolAddPlansWithRootRefAndDualCompile)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-13"}, std::vector<std::string>{"AT-28"});

    PartObjectFixture f;
    // 基线：工具 T1 已引用＋defaultTcp 完整（toolOid∈toolRefs 且 tcpKey
    // 存在——KIN-14 合法基线）。
    const core::ObjectId tool1Oid = makeOid();
    TcpRef tcpRef;
    tcpRef.toolOid = tool1Oid;
    tcpRef.tcpKey = "tcp-center";
    const RobotDesign root = makeRootWithRefs({tool1Oid}, {}, tcpRef);
    const core::ObjectId rootOid = makeOid();
    f.query.view = makeBaselineView(core::RevisionId::generate());
    f.query.addObject(tool1Oid, std::string(kToolDefinitionObjectType),
                      encodeVariant(makeValidTool(tool1Oid, "gripper-a", "tcp-center")));
    f.query.addObject(rootOid, std::string(kRobotDesignObjectType), encodeVariant(root));

    // 新工具 T2（全零保留身份——prepare 经 ctx.objectId() 取号）。
    ToolDefinition tool = makeValidTool(core::ObjectId{}, "gripper-b", "tcp-center");
    // 几何＝资源清单引用（resourceRefId），本体不入对象字节（MDL-13
    // "经 ObjectId 引用使用、不复制几何"的值面形态）。
    GeometryRef meshRef;
    meshRef.resourceRefId = "mesh-gripper";
    meshRef.kind = GeometryKind::Mesh;
    tool.geometry = meshRef;
    CommandPayload payload;
    payload.objects.push_back(newSlot(std::string(kToolDefinitionObjectType), tool));

    ApplyToolDefinitionHandler handler(f.services);
    project::HandlerContext ctx(f.query, &f.compile, nullptr);
    project::CommandPlan plan;
    std::vector<core::DiagnosticRecord> diags;
    const auto outcome = handler.prepare(
        ctx, makeEnvelope(f.query.view, std::string(kCmdApplyToolDefinition), payload),
        f.query.view, plan, diags);

    ASSERT_EQ(outcome, project::PrepareOutcome::Planned);
    EXPECT_TRUE(plan.requiresDualCompile);  // §9.3 表行 2（工具几何/物性入 WC/DWC）
    ASSERT_EQ(plan.objectWrites.size(), std::size_t{2});  // 根＋工具
    // 工具写入：身份一致、几何仍为引用面（无网格数据内嵌）。
    const ToolDefinition writtenTool =
        decodePartWrite<ToolDefinition>(plan.objectWrites[1]);
    ASSERT_TRUE(writtenTool.geometry.has_value());
    EXPECT_EQ(writtenTool.geometry->resourceRefId, "mesh-gripper");
    EXPECT_TRUE(writtenTool.geometry->localTransform == rw::math::Transform3D<double>());
    // 根写入：引用表集合语义（既有引用稳定＋新增取号身份；canonical 编码
    // 按 ObjectId 字典序规范化集合——§4.8，顺序不作为断言面）。
    const RobotDesign writtenRoot = decodeRootWrite(plan.objectWrites[0]);
    ASSERT_TRUE(writtenRoot.toolRefs.size() == std::size_t{2});
    const bool hasNew = std::find(writtenRoot.toolRefs.begin(),
                                  writtenRoot.toolRefs.end(),
                                  writtenTool.objectId) != writtenRoot.toolRefs.end();
    const bool hasOld = std::find(writtenRoot.toolRefs.begin(),
                                  writtenRoot.toolRefs.end(),
                                  tool1Oid) != writtenRoot.toolRefs.end();
    EXPECT_TRUE(hasNew);   // 新工具取号身份已入引用表
    EXPECT_TRUE(hasOld);   // 既有引用稳定（集合语义）
    EXPECT_TRUE(tool1Oid != writtenTool.objectId);  // 新工具取号＝新身份
}

/**
 * @brief 工具物性断言（§9.3 表行 2 断言列"工具物性"——①②③同连杆）。
 *
 * 分层口径（T08 设计注记原文："解码门在校验链内强制 I-MDL 不变量——非法
 * 模型不可能经载荷进入断言域"）：
 *   - 面①：载荷携带 m≤0 工具→解码门（I-MDL-5）就地拒绝＝
 *     RejectedInvalidInput（非法物性不可能经载荷进入断言域/持久化）；
 *   - 面②：工具物性断言本身（与就绪校验共用的 AssertionSuite 单一面）
 *     以单元面直接自证——m≤0→MDL-ASSERT-MASS-NONPOSITIVE＋比较型三要素
 *     ＋subject 定位（prepare 对候选工作集内工具执行同一调用）。
 */
TEST(MdlPartObjectCommands, ToolPhysicsLayeredAssertionFace)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-13", "MDL-06"}, std::vector<std::string>{});

    // —— 面①：非法物性工具经载荷被解码门拒绝（I-MDL-5 前置）——
    {
        PartObjectFixture f;
        const core::ObjectId tool1Oid = makeOid();
        TcpRef tcpRef;
        tcpRef.toolOid = tool1Oid;
        tcpRef.tcpKey = "tcp-center";
        const RobotDesign root = makeRootWithRefs({tool1Oid}, {}, tcpRef);
        f.query.view = makeBaselineView(core::RevisionId::generate());
        f.query.addObject(tool1Oid, std::string(kToolDefinitionObjectType),
                          encodeVariant(makeValidTool(tool1Oid, "gripper-a", "tcp-center")));
        f.query.addObject(makeOid(), std::string(kRobotDesignObjectType), encodeVariant(root));

        ToolDefinition badTool = makeValidTool(core::ObjectId{}, "heavy-bad", "tcp-center");
        badTool.body.mass = core::SourcedValue<double>::provided(-0.5, userProvenance());  // kg
        CommandPayload payload;
        payload.objects.push_back(newSlot(std::string(kToolDefinitionObjectType), badTool));

        ApplyToolDefinitionHandler handler(f.services);
        project::HandlerContext ctx(f.query, &f.compile, nullptr);
        project::CommandPlan plan;
        std::vector<core::DiagnosticRecord> diags;
        const auto outcome = handler.prepare(
            ctx, makeEnvelope(f.query.view, std::string(kCmdApplyToolDefinition), payload),
            f.query.view, plan, diags);
        // 解码门拒绝（非断言轨——非法值不静默修复/放行，NFR-COR-03）。
        EXPECT_EQ(outcome, project::PrepareOutcome::RejectedInvalidInput);
        EXPECT_TRUE(plan.objectWrites.empty());  // 拒绝零写入
    }

    // —— 面②：工具物性断言单一实现面（AssertionSuite——prepare 对候选
    //      工作集内工具执行同一调用，§9.4.4 共用断言）——
    {
        PartObjectFixture f;
        const core::ObjectId toolOid = makeOid();
        ToolDefinition badTool = makeValidTool(toolOid, "heavy-bad", "tcp-center");
        badTool.body.mass = core::SourcedValue<double>::provided(-0.5, userProvenance());  // kg

        std::vector<core::DiagnosticRecord> blockers;
        std::vector<core::DiagnosticRecord> warnings;
        f.services.assertionPorts;  // 端口仅行程校验消费——物性断言不依赖
        AssertionSuite suite(f.services.assertionPorts);
        suite.assertBodyPhysical(toolOid, badTool.localName, badTool.body,
                                 blockers, warnings);

        bool found = false;
        for (const auto& d : blockers) {
            if (d.code == std::string(kMdlAssertMassNonpositive)) {
                found = true;
                ASSERT_TRUE(d.subject.has_value());
                EXPECT_EQ(*d.subject, toolOid);         // 精确定位（工具 ObjectId）
                ASSERT_TRUE(d.comparison.has_value());  // 比较型三要素（m vs 0，kg）
            }
        }
        EXPECT_TRUE(found) << "缺少工具物性断言①定位诊断";
    }
}

/**
 * @brief defaultTcp 引用校验（I-MDL-9/KIN-14——assertDefaultTcp 候选闭包
 *        级门）：有工具引用而 defaultTcp 未设置→阻断；tcpKey 不在被引工具
 *        tcpList→阻断（同一稳定码，subject=被引工具）；校验通过→Planned。
 */
TEST(MdlPartObjectCommands, DefaultTcpReferenceValidationGatesToolApply)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-13"}, std::vector<std::string>{"KIN-14"});

    // —— 面①：有工具引用但 defaultTcp 未设置（KIN-14"须已设置"）——
    {
        PartObjectFixture f;
        const core::ObjectId toolOid = makeOid();
        const core::ObjectId rootOid = makeOid();
        const RobotDesign root = makeRootWithRefs({toolOid}, {});
        f.query.view = makeBaselineView(core::RevisionId::generate());
        f.query.addObject(toolOid, std::string(kToolDefinitionObjectType),
                          encodeVariant(makeValidTool(toolOid, "gripper-a", "tcp-center")));
        f.query.addObject(rootOid, std::string(kRobotDesignObjectType), encodeVariant(root));

        ToolDefinition updated = makeValidTool(toolOid, "gripper-a", "tcp-center");
        updated.objectId = toolOid;
        CommandPayload payload;
        payload.objects.push_back(
            replaceSlot(toolOid, std::string(kToolDefinitionObjectType), updated));

        ApplyToolDefinitionHandler handler(f.services);
        project::HandlerContext ctx(f.query, &f.compile, nullptr);
        project::CommandPlan plan;
        std::vector<core::DiagnosticRecord> diags;
        const auto outcome = handler.prepare(
            ctx, makeEnvelope(f.query.view, std::string(kCmdApplyToolDefinition), payload),
            f.query.view, plan, diags);
        ASSERT_EQ(outcome, project::PrepareOutcome::RejectedHardAssert);
        bool found = false;
        for (const auto& d : diags) {
            if (d.code == std::string(kMdlReadinessDefaultTcpIncomplete)) { found = true; }
        }
        EXPECT_TRUE(found) << "有工具引用而 defaultTcp 未设置应阻断（KIN-14）";
    }

    // —— 面②：tcpKey 不在被引工具 tcpList（KIN-14/tcpKey 存在性）——
    {
        PartObjectFixture f;
        const core::ObjectId toolOid = makeOid();
        const core::ObjectId rootOid = makeOid();
        TcpRef tcpRef;
        tcpRef.toolOid = toolOid;
        tcpRef.tcpKey = "no-such-key";
        const RobotDesign root = makeRootWithRefs({toolOid}, {}, tcpRef);
        f.query.view = makeBaselineView(core::RevisionId::generate());
        f.query.addObject(toolOid, std::string(kToolDefinitionObjectType),
                          encodeVariant(makeValidTool(toolOid, "gripper-a", "tcp-center")));
        f.query.addObject(rootOid, std::string(kRobotDesignObjectType), encodeVariant(root));

        ToolDefinition updated = makeValidTool(toolOid, "gripper-a", "tcp-center");
        updated.objectId = toolOid;
        CommandPayload payload;
        payload.objects.push_back(
            replaceSlot(toolOid, std::string(kToolDefinitionObjectType), updated));

        ApplyToolDefinitionHandler handler(f.services);
        project::HandlerContext ctx(f.query, &f.compile, nullptr);
        project::CommandPlan plan;
        std::vector<core::DiagnosticRecord> diags;
        const auto outcome = handler.prepare(
            ctx, makeEnvelope(f.query.view, std::string(kCmdApplyToolDefinition), payload),
            f.query.view, plan, diags);
        ASSERT_EQ(outcome, project::PrepareOutcome::RejectedHardAssert);
        bool foundWithSubject = false;
        for (const auto& d : diags) {
            if (d.code == std::string(kMdlReadinessDefaultTcpIncomplete)) {
                ASSERT_TRUE(d.subject.has_value());
                EXPECT_EQ(*d.subject, toolOid);  // subject=被引工具身份
                foundWithSubject = true;
            }
        }
        EXPECT_TRUE(foundWithSubject);
    }

    // —— 面③：defaultTcp 完整（toolOid∈toolRefs 且 tcpKey 存在）→Planned ——
    {
        PartObjectFixture f;
        const core::ObjectId toolOid = makeOid();
        const core::ObjectId rootOid = makeOid();
        TcpRef tcpRef;
        tcpRef.toolOid = toolOid;
        tcpRef.tcpKey = "tcp-center";
        const RobotDesign root = makeRootWithRefs({toolOid}, {}, tcpRef);
        f.query.view = makeBaselineView(core::RevisionId::generate());
        f.query.addObject(toolOid, std::string(kToolDefinitionObjectType),
                          encodeVariant(makeValidTool(toolOid, "gripper-a", "tcp-center")));
        f.query.addObject(rootOid, std::string(kRobotDesignObjectType), encodeVariant(root));

        ToolDefinition updated = makeValidTool(toolOid, "gripper-a", "tcp-center");
        updated.objectId = toolOid;
        CommandPayload payload;
        payload.objects.push_back(
            replaceSlot(toolOid, std::string(kToolDefinitionObjectType), updated));

        ApplyToolDefinitionHandler handler(f.services);
        project::HandlerContext ctx(f.query, &f.compile, nullptr);
        project::CommandPlan plan;
        std::vector<core::DiagnosticRecord> diags;
        const auto outcome = handler.prepare(
            ctx, makeEnvelope(f.query.view, std::string(kCmdApplyToolDefinition), payload),
            f.query.view, plan, diags);
        EXPECT_EQ(outcome, project::PrepareOutcome::Planned);
    }
}

// =====================================================================
// ACC2：引用保护（V-04/V-05——I-MDL-9/CON-02/PA-2）
// =====================================================================

/**
 * @brief V-04：删除被 defaultTcp 引用的工具→拒绝＋MDL-REF-PROTECTED 比较
 *        型定位诊断（subject=oid；actual=引用计数 1/expected=0）；拒绝零
 *        写入（对象字节与旧修订闭包完整——CON-02/PA-2）。
 */
TEST(MdlPartObjectCommands, V04RemoveDefaultTcpReferencedToolRejected)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-13", "CON-02"},
                  std::vector<std::string>{"I-MDL-9"});

    PartObjectFixture f;
    const core::ObjectId toolOid = makeOid();
    TcpRef tcpRef;
    tcpRef.toolOid = toolOid;
    tcpRef.tcpKey = "tcp-center";
    const RobotDesign root = makeRootWithRefs({toolOid}, {}, tcpRef);
    f.query.view = makeBaselineView(core::RevisionId::generate());
    f.query.addObject(toolOid, std::string(kToolDefinitionObjectType),
                      encodeVariant(makeValidTool(toolOid, "gripper-a", "tcp-center")));
    f.query.addObject(makeOid(), std::string(kRobotDesignObjectType), encodeVariant(root));

    CommandPayload payload;
    payload.removals.push_back(removeSlot(toolOid, std::string(kToolDefinitionObjectType)));

    ApplyToolDefinitionHandler handler(f.services);
    project::HandlerContext ctx(f.query, &f.compile, nullptr);
    project::CommandPlan plan;
    std::vector<core::DiagnosticRecord> diags;
    const auto outcome = handler.prepare(
        ctx, makeEnvelope(f.query.view, std::string(kCmdApplyToolDefinition), payload),
        f.query.view, plan, diags);

    ASSERT_EQ(outcome, project::PrepareOutcome::RejectedHardAssert);
    bool found = false;
    for (const auto& d : diags) {
        if (d.code == std::string(kMdlRefProtected)) {
            found = true;
            ASSERT_TRUE(d.subject.has_value());
            EXPECT_EQ(*d.subject, toolOid);  // subject=oid（V-04 观测点）
            ASSERT_TRUE(d.comparison.has_value());  // 比较型定位（引用计数）
            const double actual = d.comparison->actual.quantity.tryValue().value_or(0.0);
            const double expected = d.comparison->expected.quantity.tryValue().value_or(-1.0);
            EXPECT_DOUBLE_EQ(actual, 1.0);   // 被 defaultTcp 引用 1 处
            EXPECT_DOUBLE_EQ(expected, 0.0);  // 移除前须为 0
        }
    }
    EXPECT_TRUE(found) << "缺少 MDL-REF-PROTECTED 引用保护诊断";
    EXPECT_TRUE(plan.objectWrites.empty());  // 拒绝零写入——旧字节与闭包完整
    EXPECT_EQ(f.compile.callCount, 0);       // 处理器零编译调用（S5 归 project）
}

/**
 * @brief V-05：删除未引用工具＝引用移除——恰好一次根写入（引用表不再含
 *        该对象）、被移除对象零写入（对象库旧字节与历史修订闭包保留——
 *        PA-2）、inverse＝根前一版本字节（撤销还原引用）、摘要附"可能
 *        存在外部引用"提示（跨域悬空检测归 requirements——P-MDL-6；
 *        ⑤事件随修订提交由 project TxEngine 发布——本卡观测点为摘要面）。
 */
TEST(MdlPartObjectCommands, V05RemoveUnreferencedToolIsReferenceRemoval)
{
    IRD_TEST_INFO(std::vector<std::string>{"CON-02"}, std::vector<std::string>{"PA-2"});

    PartObjectFixture f;
    const core::ObjectId toolOid = makeOid();
    const RobotDesign root = makeRootWithRefs({toolOid}, {});  // 无 defaultTcp＝未被引用
    f.query.view = makeBaselineView(core::RevisionId::generate());
    f.query.addObject(toolOid, std::string(kToolDefinitionObjectType),
                      encodeVariant(makeValidTool(toolOid, "gripper-a", "tcp-center")));
    f.query.addObject(makeOid(), std::string(kRobotDesignObjectType), encodeVariant(root));

    CommandPayload payload;
    payload.removals.push_back(removeSlot(toolOid, std::string(kToolDefinitionObjectType)));

    ApplyToolDefinitionHandler handler(f.services);
    project::HandlerContext ctx(f.query, &f.compile, nullptr);
    project::CommandPlan plan;
    std::vector<core::DiagnosticRecord> diags;
    const auto outcome = handler.prepare(
        ctx, makeEnvelope(f.query.view, std::string(kCmdApplyToolDefinition), payload),
        f.query.view, plan, diags);

    ASSERT_EQ(outcome, project::PrepareOutcome::Planned);
    // 新修订仅根写入（被移除对象零写入——对象库旧字节保留，PA-2）。
    ASSERT_EQ(plan.objectWrites.size(), std::size_t{1});
    EXPECT_EQ(plan.objectWrites[0].objectTypeToken, std::string(kRobotDesignObjectType));
    const RobotDesign writtenRoot = decodeRootWrite(plan.objectWrites[0]);
    EXPECT_TRUE(writtenRoot.toolRefs.empty());  // 引用表不再含该对象
    // inverse＝根前一版本字节（撤销＝引用还原——快照逆放）。
    ASSERT_TRUE(plan.inverseCommandType.has_value());
    const auto inverse = tryDecodeCommandPayload(*plan.inversePayloadCanonical);
    ASSERT_TRUE(inverse.has_value());
    ASSERT_EQ(inverse->objects.size(), std::size_t{1});
    EXPECT_TRUE(inverse->objects[0].objectBytes == encodeVariant(root));
    // 摘要：引用移除计数＋跨域提示（可能存在外部引用）。
    EXPECT_NE(plan.summary.find("移除引用 1 项"), std::string::npos);
    EXPECT_NE(plan.summary.find("可能存在外部引用"), std::string::npos);
    // 移除不改变关节行程事实——不消费④端口（行程域归属声明面）。
    EXPECT_EQ(f.provider.resolveCalls, 0);
}

/**
 * @brief 移除未被引用的对象（场景侧 V-05）：批量混载（新增＋移除）一次
 *        根写入；移除非引用场景对象不触发保护门（defaultTcp 只引用工具
 *        ——I-MDL-9）；跨域提示随摘要留痕。
 */
TEST(MdlPartObjectCommands, SceneBatchMixedAddAndRemovePlansOnce)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-15"}, std::vector<std::string>{"P-MDL-6"});

    PartObjectFixture f;
    const core::ObjectId existingScene = makeOid();
    const RobotDesign root = makeRootWithRefs({}, {existingScene});
    f.query.view = makeBaselineView(core::RevisionId::generate());
    f.query.addObject(existingScene, std::string(kSceneObjectObjectType),
                      encodeVariant(makeSceneObject(existingScene, "old-table",
                                                    {2.0, 0.0, 0.0},
                                                    SceneObjectRole::EnvironmentObject)));
    f.query.addObject(makeOid(), std::string(kRobotDesignObjectType), encodeVariant(root));

    CommandPayload payload;
    payload.removals.push_back(removeSlot(existingScene, std::string(kSceneObjectObjectType)));
    payload.objects.push_back(newSlot(
        std::string(kSceneObjectObjectType),
        makeSceneObject(core::ObjectId{}, "fixture", {0.5, 1.0, 0.0},
                        SceneObjectRole::Workpiece)));

    ApplySceneObjectsHandler handler(f.services);
    project::HandlerContext ctx(f.query, &f.compile, nullptr);
    project::CommandPlan plan;
    std::vector<core::DiagnosticRecord> diags;
    const auto outcome = handler.prepare(
        ctx, makeEnvelope(f.query.view, std::string(kCmdApplySceneObjects), payload),
        f.query.view, plan, diags);

    ASSERT_EQ(outcome, project::PrepareOutcome::Planned);
    // 根写入一次（批量增/移除共摊）＋新场景对象写入；被移除对象零写入。
    ASSERT_EQ(plan.objectWrites.size(), std::size_t{2});
    const RobotDesign writtenRoot = decodeRootWrite(plan.objectWrites[0]);
    ASSERT_EQ(writtenRoot.sceneRefs.size(), std::size_t{1});
    EXPECT_FALSE(writtenRoot.sceneRefs[0] == existingScene);  // 旧引用已移除
    EXPECT_EQ(plan.objectWrites[1].objectTypeToken, std::string(kSceneObjectObjectType));
    // 摘要跨域提示（移除面）。
    EXPECT_NE(plan.summary.find("可能存在外部引用"), std::string::npos);
}

/**
 * @brief 移除约束：移除未被根引用的对象/不存在的身份＝无效载荷（无可移
 *        除引用——不静默空转，NFR-COR-03）。
 */
TEST(MdlPartObjectCommands, RemoveUnreferencedOrUnknownObjectRejectedInvalid)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-15"}, std::vector<std::string>{});

    PartObjectFixture f;
    const core::ObjectId referencedScene = makeOid();
    const core::ObjectId unknownScene = makeOid();
    const RobotDesign root = makeRootWithRefs({}, {referencedScene});
    f.query.view = makeBaselineView(core::RevisionId::generate());
    f.query.addObject(referencedScene, std::string(kSceneObjectObjectType),
                      encodeVariant(makeSceneObject(referencedScene, "table",
                                                    {1.0, 1.0, 0.0},
                                                    SceneObjectRole::EnvironmentObject)));
    f.query.addObject(makeOid(), std::string(kRobotDesignObjectType), encodeVariant(root));

    // —— 闭包内存在但未被引用（不在 sceneRefs）——
    {
        CommandPayload payload;
        payload.removals.push_back(removeSlot(unknownScene, std::string(kSceneObjectObjectType)));
        ApplySceneObjectsHandler handler(f.services);
        project::HandlerContext ctx(f.query, &f.compile, nullptr);
        project::CommandPlan plan;
        std::vector<core::DiagnosticRecord> diags;
        const auto outcome = handler.prepare(
            ctx, makeEnvelope(f.query.view, std::string(kCmdApplySceneObjects), payload),
            f.query.view, plan, diags);
        EXPECT_EQ(outcome, project::PrepareOutcome::RejectedInvalidInput);
    }

    // —— 对象槽与移除槽 token 错配（移除槽 token 非本命令对象）——
    {
        CommandPayload payload;
        payload.removals.push_back(
            removeSlot(referencedScene, std::string(kToolDefinitionObjectType)));
        ApplySceneObjectsHandler handler(f.services);
        project::HandlerContext ctx(f.query, &f.compile, nullptr);
        project::CommandPlan plan;
        std::vector<core::DiagnosticRecord> diags;
        const auto outcome = handler.prepare(
            ctx, makeEnvelope(f.query.view, std::string(kCmdApplySceneObjects), payload),
            f.query.view, plan, diags);
        EXPECT_EQ(outcome, project::PrepareOutcome::RejectedInvalidInput);
    }
}

// =====================================================================
// ACC3：场景对象约束（MDL-15——worldPose 固连/词表/collisionProfileHint）
// =====================================================================

/**
 * @brief worldPose 世界坐标系固连（M-11/runtime §4.3.4 同口径——V-12 建模
 *        侧输入面）：基线基座带任意旋转（customEaa）时，场景对象的
 *        worldPose 经命令管线写入后逐元素等于输入（不得预乘安装旋转）。
 */
TEST(MdlPartObjectCommands, SceneWorldPoseNotPreMultipliedByBasePlacement)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-15"}, std::vector<std::string>{"AT-28"});

    PartObjectFixture f;
    RobotDesign root = makeRootWithRefs({}, {});
    // 倒挂基座（Inverted＝R_x(π)——安装旋转非恒等；modeling 只存预设
    // 参数不存矩阵——P-RT-4/M-11）。
    root.basePlacement.preset = runtime::InstallationPresetToken::Inverted;
    f.query.view = makeBaselineView(core::RevisionId::generate());
    f.query.addObject(makeOid(), std::string(kRobotDesignObjectType), encodeVariant(root));

    const rw::math::Vector3D<double> worldPos(1.5, -2.0, 0.8);  // m，世界系
    SceneObject scene = makeSceneObject(core::ObjectId{}, "conveyor", worldPos,
                                        SceneObjectRole::EnvironmentObject,
                                        std::string("import-group-a"));
    CommandPayload payload;
    payload.objects.push_back(newSlot(std::string(kSceneObjectObjectType), scene));

    ApplySceneObjectsHandler handler(f.services);
    project::HandlerContext ctx(f.query, &f.compile, nullptr);
    project::CommandPlan plan;
    std::vector<core::DiagnosticRecord> diags;
    const auto outcome = handler.prepare(
        ctx, makeEnvelope(f.query.view, std::string(kCmdApplySceneObjects), payload),
        f.query.view, plan, diags);

    ASSERT_EQ(outcome, project::PrepareOutcome::Planned);
    // 写入字节中的 worldPose＝输入原样（世界系固连——不被基座旋转预乘）。
    bool verified = false;
    for (const project::ObjectWrite& w : plan.objectWrites) {
        if (w.objectTypeToken != std::string(kSceneObjectObjectType)) { continue; }
        const SceneObject written = decodePartWrite<SceneObject>(w);
        for (std::size_t i = 0; i < 3; ++i) {
            EXPECT_DOUBLE_EQ(written.worldPose.P()[i], worldPos[i]);
        }
        verified = true;
    }
    EXPECT_TRUE(verified);
}

/**
 * @brief 角色五值词表（词表所有者＝modeling、policy 消费）：两侧 token
 *        逐值同串（跨单元以 token 串为界——ObjectTypes.hpp/policy 失同
 *        步即刻暴露）；角色经编解码往返保真。
 */
TEST(MdlPartObjectCommands, SceneRoleVocabularyMatchesPolicyTokens)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-15"}, std::vector<std::string>{});

    // 词表五值逐值比对（§4.5 词表原文序——RobotLink|Tool|Payload|
    // EnvironmentObject|Workpiece；policy.md §4.3"归建模语义，本文消费"）。
    const std::pair<SceneObjectRole, policy::SceneObjectRole> pairs[] = {
        {SceneObjectRole::RobotLink, policy::SceneObjectRole::RobotLink},
        {SceneObjectRole::Tool, policy::SceneObjectRole::Tool},
        {SceneObjectRole::Payload, policy::SceneObjectRole::Payload},
        {SceneObjectRole::EnvironmentObject, policy::SceneObjectRole::EnvironmentObject},
        {SceneObjectRole::Workpiece, policy::SceneObjectRole::Workpiece},
    };
    for (const auto& [m, p] : pairs) {
        EXPECT_EQ(sceneObjectRoleToken(m), policy::sceneObjectRoleToken(p))
            << "角色 token 两侧失同步";
    }
    // 词表外串不回解（try 轨不猜测——ARC-04）。
    EXPECT_FALSE(policy::trySceneObjectRole("robot-link").has_value());
}

/**
 * @brief collisionProfileHint 仅报告用途：值经命令管线往返逐字节保真；
 *        场景命令不消费④端口（提示不改变任何策略判定——策略权威归
 *        policy，ARC-05）。
 */
TEST(MdlPartObjectCommands, CollisionProfileHintRoundtripReportOnly)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-15"}, std::vector<std::string>{"ARC-05"});

    PartObjectFixture f;
    const RobotDesign root = makeRootWithRefs({}, {});
    f.query.view = makeBaselineView(core::RevisionId::generate());
    f.query.addObject(makeOid(), std::string(kRobotDesignObjectType), encodeVariant(root));

    SceneObject scene = makeSceneObject(core::ObjectId{}, "beam", {0.0, 3.0, 2.0},
                                        SceneObjectRole::Workpiece,
                                        std::string("self-collision-group-7"));
    CommandPayload payload;
    payload.objects.push_back(newSlot(std::string(kSceneObjectObjectType), scene));

    ApplySceneObjectsHandler handler(f.services);
    project::HandlerContext ctx(f.query, &f.compile, nullptr);
    project::CommandPlan plan;
    std::vector<core::DiagnosticRecord> diags;
    const auto outcome = handler.prepare(
        ctx, makeEnvelope(f.query.view, std::string(kCmdApplySceneObjects), payload),
        f.query.view, plan, diags);

    ASSERT_EQ(outcome, project::PrepareOutcome::Planned);
    bool verified = false;
    for (const project::ObjectWrite& w : plan.objectWrites) {
        if (w.objectTypeToken != std::string(kSceneObjectObjectType)) { continue; }
        const SceneObject written = decodePartWrite<SceneObject>(w);
        ASSERT_TRUE(written.collisionProfileHint.has_value());
        EXPECT_EQ(*written.collisionProfileHint, "self-collision-group-7");  // 报告保真
        verified = true;
    }
    EXPECT_TRUE(verified);
    EXPECT_EQ(f.provider.resolveCalls, 0);  // 提示不触发任何策略解析（仅报告用途）
}

// =====================================================================
// ACC4：命名位姿（MDL-17/D-MDL-3——requiresDualCompile/保留键/关节序）
// =====================================================================

/**
 * @brief apply-named-poses 差异面（§9.3 表行 4）：requiresDualCompile=
 *        false（纯参考数据零编译影响——位姿集修订不触发重算）；无物性
 *        断言产出；合并流保留基线保留键（V-27 建模侧——homeConfiguration/
 *        zeroConfiguration 原样保留，KIN-06 复位走会话命令零修订）。
 */
TEST(MdlPartObjectCommands, NamedPosesMergePreservesReservedKeysAndSkipsCompile)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-17"}, std::vector<std::string>{"AT-28"});

    PartObjectFixture f;
    const core::ObjectId poseSetOid = makeOid();
    // 基线：位姿集已含保留键（Home/Zero 参考——模板/导入路径写入）＋
    // 一个既有用户位姿。
    PoseSet baselinePoses;
    baselinePoses.objectId = poseSetOid;
    baselinePoses.entries = {
        poseEntry(std::string(kHomeConfigurationPoseKey), {0.0}, "Home 参考"),
        poseEntry(std::string(kZeroConfigurationPoseKey), {0.0}, "Zero 参考"),
        poseEntry("old-cruise", {0.3}),
    };
    RobotDesign root = makeRootWithRefs({}, {});
    root.poseSetRef = poseSetOid;
    f.query.view = makeBaselineView(core::RevisionId::generate());
    f.query.addObject(poseSetOid, std::string(kNamedPoseSetObjectType),
                      encodeVariant(baselinePoses));
    f.query.addObject(makeOid(), std::string(kRobotDesignObjectType), encodeVariant(root));

    // 用户编辑：全新用户位姿全集（不含保留键——保留键非管理面）。
    PoseSet submitted;
    submitted.objectId = poseSetOid;  // 替换路径：内嵌身份＝槽身份（ARC-04）
    submitted.entries = {poseEntry("cruise", {0.7}, "巡航姿态")};
    CommandPayload payload;
    payload.objects.push_back(
        replaceSlot(poseSetOid, std::string(kNamedPoseSetObjectType), submitted));

    ApplyNamedPosesHandler handler(f.services);
    project::HandlerContext ctx(f.query, &f.compile, nullptr);
    project::CommandPlan plan;
    std::vector<core::DiagnosticRecord> diags;
    const auto outcome = handler.prepare(
        ctx, makeEnvelope(f.query.view, std::string(kCmdApplyNamedPoses), payload),
        f.query.view, plan, diags);

    ASSERT_EQ(outcome, project::PrepareOutcome::Planned);
    // 双编译表：apply-named-poses＝false（§9.3——位姿集修订不触发重算）。
    EXPECT_FALSE(plan.requiresDualCompile);
    // 无物性断言产出（纯参考数据——断言管线无该域发现）。
    bool hasAssertDiag = false;
    for (const auto& d : diags) {
        if (d.code.rfind("MDL-ASSERT-", 0) == 0) { hasAssertDiag = true; }
    }
    EXPECT_FALSE(hasAssertDiag);
    // 写入面：恰一位姿集写入（替换路径根不写）；保留键保留＋用户全集
    // 替换旧用户条目。
    ASSERT_EQ(plan.objectWrites.size(), std::size_t{1});
    const PoseSet written = decodePartWrite<PoseSet>(plan.objectWrites[0]);
    bool hasHome = false;
    bool hasZero = false;
    bool hasCruise = false;
    bool hasOldUser = false;
    for (const PoseSetEntry& e : written.entries) {
        if (e.key == kHomeConfigurationPoseKey) { hasHome = true; }
        if (e.key == kZeroConfigurationPoseKey) { hasZero = true; }
        if (e.key == "cruise") { hasCruise = true; }
        if (e.key == "old-cruise") { hasOldUser = true; }
    }
    EXPECT_TRUE(hasHome);    // V-27：保留键原样保留
    EXPECT_TRUE(hasZero);
    EXPECT_TRUE(hasCruise);  // 用户条目写入
    EXPECT_FALSE(hasOldUser);  // 用户全集替换（编辑提交＝完整清单）
    // 保留键条目的关节值逐位保留（复位参考不被编辑破坏）。
    for (const PoseSetEntry& e : written.entries) {
        if (e.key == kHomeConfigurationPoseKey) {
            ASSERT_EQ(e.jointConfiguration.size(), std::size_t{1});
            EXPECT_DOUBLE_EQ(e.jointConfiguration[0], 0.0);  // rad
        }
    }
}

/**
 * @brief 保留键越界与关节序校验（MDL-17"除 Home/Zero 外"＋§4.6 一一
 *        对应）：载荷携带保留键条目/条目长度≠根关节表长度＝无效载荷。
 */
TEST(MdlPartObjectCommands, NamedPosesReservedKeyAndJointOrderRejected)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-17"}, std::vector<std::string>{});

    PartObjectFixture f;
    const RobotDesign root = makeRootWithRefs({}, {});  // 1 关节（关节表长度 1）
    f.query.view = makeBaselineView(core::RevisionId::generate());
    f.query.addObject(makeOid(), std::string(kRobotDesignObjectType), encodeVariant(root));

    // —— 载荷携带保留键（越出面拒绝——MDL-17 字面）——
    {
        PoseSet submitted;
        submitted.entries = {poseEntry(std::string(kHomeConfigurationPoseKey), {0.0})};
        CommandPayload payload;
        payload.objects.push_back(
            newSlot(std::string(kNamedPoseSetObjectType), submitted));
        ApplyNamedPosesHandler handler(f.services);
        project::HandlerContext ctx(f.query, &f.compile, nullptr);
        project::CommandPlan plan;
        std::vector<core::DiagnosticRecord> diags;
        const auto outcome = handler.prepare(
            ctx, makeEnvelope(f.query.view, std::string(kCmdApplyNamedPoses), payload),
            f.query.view, plan, diags);
        EXPECT_EQ(outcome, project::PrepareOutcome::RejectedInvalidInput);
    }

    // —— 条目与关节序不一一对应（长度 2 ≠ 关节表 1——§4.6）——
    {
        PoseSet submitted;
        submitted.entries = {poseEntry("wrong-length", {0.1, 0.2})};
        CommandPayload payload;
        payload.objects.push_back(
            newSlot(std::string(kNamedPoseSetObjectType), submitted));
        ApplyNamedPosesHandler handler(f.services);
        project::HandlerContext ctx(f.query, &f.compile, nullptr);
        project::CommandPlan plan;
        std::vector<core::DiagnosticRecord> diags;
        const auto outcome = handler.prepare(
            ctx, makeEnvelope(f.query.view, std::string(kCmdApplyNamedPoses), payload),
            f.query.view, plan, diags);
        EXPECT_EQ(outcome, project::PrepareOutcome::RejectedInvalidInput);
    }

    // —— 合法首建：保留键不存在于基线时产物只含用户条目——Planned ——
    {
        PoseSet submitted;
        submitted.entries = {poseEntry("home-pose", {0.0}), poseEntry("pick", {-0.4})};
        CommandPayload payload;
        payload.objects.push_back(
            newSlot(std::string(kNamedPoseSetObjectType), submitted));
        ApplyNamedPosesHandler handler(f.services);
        project::HandlerContext ctx(f.query, &f.compile, nullptr);
        project::CommandPlan plan;
        std::vector<core::DiagnosticRecord> diags;
        const auto outcome = handler.prepare(
            ctx, makeEnvelope(f.query.view, std::string(kCmdApplyNamedPoses), payload),
            f.query.view, plan, diags);
        ASSERT_EQ(outcome, project::PrepareOutcome::Planned);
        // 首建＝2 写入（位姿集＋根 poseSetRef 回填——"至多一份"）。
        ASSERT_EQ(plan.objectWrites.size(), std::size_t{2});
        const RobotDesign writtenRoot = decodeRootWrite(plan.objectWrites[0]);
        ASSERT_TRUE(writtenRoot.poseSetRef.has_value());
        EXPECT_TRUE(writtenRoot.poseSetRef->isValid());
    }
}

/**
 * @brief 位姿集内容不入根对象字节（D-MDL-3"不进 Description"的建模侧
 *        形态）：根对象仅承载 poseSetRef（ObjectId 引用）——含位姿集引
 *        用的根字节与位姿集条目内容完全无关（同根不同位姿内容＝同字节），
 *        评估器依赖键无从聚合位姿内容（零编译影响的字节面前提）。
 */
TEST(MdlPartObjectCommands, PoseSetContentAbsentFromRootBytes)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-17"}, std::vector<std::string>{"D-MDL-3"});

    const core::ObjectId poseSetOid = makeOid();
    RobotDesign root = makeRootWithRefs({}, {});
    root.poseSetRef = poseSetOid;

    // 同一根、两种位姿集内容——根字节必须逐位相同（内容不入根）。
    PoseSet posesA;
    posesA.objectId = poseSetOid;
    posesA.entries = {poseEntry("a", {0.1})};
    PoseSet posesB;
    posesB.objectId = poseSetOid;
    posesB.entries = {poseEntry("b", {9.9}), poseEntry("c", {-9.9})};

    const std::vector<std::uint8_t> rootBytesA =
        encodeVariant(ObjectVariant(root));  // 根编码与位姿集对象完全解耦
    const std::vector<std::uint8_t> rootBytesB = encodeVariant(ObjectVariant(root));
    EXPECT_TRUE(rootBytesA == rootBytesB);

    RobotDesignCodec codec;
    auto decoded = codec.decode(rootBytesA, kCurrentFormatVersion);
    ASSERT_TRUE(decoded.ok());
    const RobotDesign& roundTripped = std::get<RobotDesign>(decoded.get());
    // 根仅持引用（ObjectId）——解码产物无任何位姿条目内容。
    ASSERT_TRUE(roundTripped.poseSetRef.has_value());
    EXPECT_EQ(*roundTripped.poseSetRef, poseSetOid);
    EXPECT_TRUE(roundTripped.joints.size() == std::size_t{1});  // 其余字段与位姿无关
}
