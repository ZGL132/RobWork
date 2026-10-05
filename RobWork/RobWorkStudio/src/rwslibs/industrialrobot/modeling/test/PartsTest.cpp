/**
 * @file   PartsTest.cpp
 * @brief  四部件对象用例组（MdlParts）——工具物性断言①②③（§4.4"同连杆"）
 *         与传动不变量 I-MDL-11/I-MDL-12（acceptance 3 的"R1 下 coupling
 *         配置拒绝"）；部件值模型相等/编解码往返由 CodecTest 覆盖，本文件
 *         聚焦部件级不变量核查面。
 *
 * 设计依据：units/modeling.md §4.4（ToolDefinition 断言①②③同连杆）、
 * §4.7（DrivetrainDesign——ratio>0 且有限；R1＝coupling 禁止配置）、§4.10
 * （I-MDL-11/I-MDL-12）；任务契约 tasks/foundation/WP-13-T03.json
 * acceptance 1/3。
 */

#include <sdurws/ird/modeling/Parts.hpp>
#include <sdurws/ird/modeling/Codec.hpp>
#include <sdurws/ird/modeling/RobotDesign.hpp>
#include <sdurws/ird/modeling/Template.hpp>  // ModelingWorkingSet（部件位姿编辑流写入目标——UI-T55）
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

using sdurws::ird::core::ObjectId;
using sdurws::ird::core::ProvenanceKind;
using sdurws::ird::core::SourcedValue;
using sdurws::ird::core::ValueProvenance;
using sdurws::ird::modeling::BodyData;
using sdurws::ird::modeling::CouplingDesign;
using sdurws::ird::modeling::CouplingStage;
using sdurws::ird::modeling::DrivetrainDesign;
using sdurws::ird::modeling::InertiaTensor;
using sdurws::ird::modeling::InvariantId;
using sdurws::ird::modeling::TcpEntry;
using sdurws::ird::modeling::ToolDefinition;
using sdurws::ird::modeling::ModelingWorkingSet;
using sdurws::ird::modeling::PartPoseEditValue;
using sdurws::ird::modeling::PartPoseEditErrorCode;
using sdurws::ird::modeling::TcpEditErrorCode;
using sdurws::ird::modeling::applyTcpAddEdit;
using sdurws::ird::modeling::applyTcpRemoveEdit;
using sdurws::ird::modeling::applyTcpOffsetEdit;
using sdurws::ird::modeling::applyDefaultTcpSwitchEdit;
using sdurws::ird::modeling::tcpEditErrorCodeToken;
using sdurws::ird::modeling::SceneObject;
using sdurws::ird::modeling::applyToolMountEdit;
using sdurws::ird::modeling::applyScenePoseEdit;
using sdurws::ird::modeling::partPoseEditErrorCodeToken;
using sdurws::ird::modeling::checkInvariants;

namespace {

/// 由 64 位序号构造确定 ObjectId（同 RobotDesignTest 口径）。
ObjectId makeOid(std::uint64_t v)
{
    char text[40] = {};
    std::snprintf(text, sizeof(text), "obj-%024llx%08llx",
                  static_cast<unsigned long long>(0),
                  static_cast<unsigned long long>(v));
    return ObjectId::fromCanonical(text);
}

template <class T>
SourcedValue<T> provided(T value)
{
    return SourcedValue<T>::provided(std::move(value),
                                     ValueProvenance::make(ProvenanceKind::UserProvided));
}

/// 合法工具（质量 1.8 kg＋SPD 惯量＋TCP 一条——I-MDL-5 通过）。
ToolDefinition makeValidTool()
{
    ToolDefinition t;
    t.objectId = makeOid(100);
    t.localName = "gripper-a";
    t.body.mass = provided(1.8);  // kg
    InertiaTensor it;
    it.ixx = 0.01;
    it.iyy = 0.01;
    it.izz = 0.004;
    t.body.inertia = provided(it);  // kg·m²
    TcpEntry tcp;
    tcp.key = "tcp-center";
    t.tcpList = {tcp};  // ≥1（§4.4 表行）
    return t;
}

/// 合法传动（ratio Provided>0；无 coupling）。
DrivetrainDesign makeValidDrivetrain()
{
    DrivetrainDesign dt;
    dt.objectId = makeOid(500);
    dt.ratioPerJoint = {provided(101.0)};  // 无量纲
    return dt;
}

/// 断言辅助：违例清单恰含指定不变量与定位子串。
::testing::AssertionResult hasViolation(const std::vector<sdurws::ird::modeling::InvariantViolation>& v,
                                         InvariantId id, const std::string& subjectPart)
{
    for (const auto& item : v) {
        if (item.id == id && item.subject.find(subjectPart) != std::string::npos) {
            return ::testing::AssertionSuccess();
        }
    }
    // 枚举不可直入 gtest Message 流——经 invariantIdToken 转卡面编号串
    return ::testing::AssertionFailure()
           << "未找到违例 " << sdurws::ird::modeling::invariantIdToken(id)
           << "（" << subjectPart << "）";
}

}  // namespace

/// 工具物性断言①②③同连杆（§4.4 表行）：质量≤0/非正定惯量/三角不等式
/// 逐项触发 I-MDL-5；合法工具全过。
TEST(MdlParts, ToolBodyAssertionsSameAsLink_WP13T03_ACC3)
{
    IRD_TEST_INFO("MDL-13", {}, std::nullopt);
    // 合法工具：无违例
    EXPECT_TRUE(checkInvariants(makeValidTool()).empty());
    // 断言①：质量 m≤0（kg）
    ToolDefinition badMass = makeValidTool();
    badMass.body.mass = provided(-0.1);
    EXPECT_TRUE(hasViolation(checkInvariants(badMass), InvariantId::IMdl5, "tool.body.mass"));
    // 断言②：非正定（对角 (1,1,-1) kg·m²）
    ToolDefinition notSpd = makeValidTool();
    InertiaTensor indefinite;
    indefinite.ixx = 1.0;
    indefinite.iyy = 1.0;
    indefinite.izz = -1.0;
    notSpd.body.inertia = provided(indefinite);
    EXPECT_TRUE(hasViolation(checkInvariants(notSpd), InvariantId::IMdl5,
                             "tool.body.inertia.spd"));
    // 断言③：三角不等式（对角 (1,1,3) kg·m²——3>1+1）
    ToolDefinition badTriangle = makeValidTool();
    InertiaTensor elongated;
    elongated.ixx = 1.0;
    elongated.iyy = 1.0;
    elongated.izz = 3.0;
    badTriangle.body.inertia = provided(elongated);
    EXPECT_TRUE(hasViolation(checkInvariants(badTriangle), InvariantId::IMdl5,
                             "tool.body.inertia.triangle"));
    // 工具与连杆共用同一判定实现（单元私有 InertiaMath 单点）——同输入
    // 同判：对角 (1,1,3) 在连杆上同样报三角不等式
    sdurws::ird::modeling::RobotDesign design;
    sdurws::ird::modeling::JointEntry j;
    j.objectId = makeOid(1);
    j.localName = "j1";
    j.type = sdurws::ird::modeling::JointType::Revolute;
    j.bounds = SourcedValue<sdurws::ird::modeling::JointLimits>::notApplicable();
    design.joints = {j};
    design.links.resize(2);
    design.links[0].objectId = makeOid(11);
    design.links[0].localName = "base";
    design.links[1].objectId = makeOid(12);
    design.links[1].localName = "flange";
    design.links[1].body.inertia = provided(elongated);
    EXPECT_TRUE(hasViolation(checkInvariants(design), InvariantId::IMdl5,
                             "links[1].body.inertia.triangle"));
}

/// 传动 I-MDL-12：R1 下 coupling 配置拒绝（存在即阻断——§4.7 原文）。
TEST(MdlParts, DrivetrainCouplingR1Locked_WP13T03_ACC3)
{
    IRD_TEST_INFO("MDL-21", {}, std::nullopt);
    DrivetrainDesign dt = makeValidDrivetrain();
    CouplingDesign cp;
    cp.rows = 1;
    cp.cols = 1;
    cp.c = {2.0};  // 无量纲
    cp.jointRangeFirst = 0;
    cp.jointRangeLast = 0;
    cp.conditionNumber = 1.0;
    dt.coupling = cp;
    // R1 锁定：存在即违例（I-MDL-12——I-MDL-11 行括注定义）
    EXPECT_TRUE(hasViolation(checkInvariants(dt, CouplingStage::R1Locked),
                             InvariantId::IMdl12, "r1-locked"));
    // R2 解锁：同对象过 I-MDL-11/12（阶段语义）
    EXPECT_TRUE(checkInvariants(dt, CouplingStage::R2Enabled).empty());
}

/// 传动 I-MDL-11：ratio 非有限/≤0 拒绝；缺失不拒（DataInsufficient 降级）。
TEST(MdlParts, DrivetrainRatioValidity_WP13T03_ACC3)
{
    IRD_TEST_INFO("MDL-16", {}, std::nullopt);
    // ratio 负值：违例
    DrivetrainDesign negative = makeValidDrivetrain();
    negative.ratioPerJoint[0] = provided(-1.0);
    EXPECT_TRUE(hasViolation(checkInvariants(negative, CouplingStage::R1Locked),
                             InvariantId::IMdl11, "ratioPerJoint[0]"));
    // ratio NaN：违例（不静默置 0——NFR-COR-03）
    DrivetrainDesign nan = makeValidDrivetrain();
    nan.ratioPerJoint[0] =
        provided(std::numeric_limits<double>::quiet_NaN());
    EXPECT_TRUE(hasViolation(checkInvariants(nan, CouplingStage::R1Locked),
                             InvariantId::IMdl11, "ratioPerJoint[0]"));
    // ratio 缺失（NotProvided）：不违例——回填/编辑前暂缺
    DrivetrainDesign missing = makeValidDrivetrain();
    missing.ratioPerJoint[0] = SourcedValue<double>::notProvided();
    EXPECT_TRUE(checkInvariants(missing, CouplingStage::R1Locked).empty());
}

// =====================================================================
// WP-13-T10——工具 TCP 完整（I-MDL-13）与命名位姿合并编辑流（§4.6）
// =====================================================================

/// 合法工具（TCP 键引用锚完整——I-MDL-13 通过面）。
ToolDefinition makeValidToolT10()
{
    ToolDefinition t;
    t.objectId = makeOid(200);
    t.localName = "gripper-b";
    TcpEntry tcp;
    tcp.key = "tcp-center";
    t.tcpList = {tcp};  // ≥1（§4.4 表行——I-MDL-13）
    return t;
}

/**
 * @brief 工具 TCP 完整（I-MDL-13——§4.4 tcpList 行"≥1"的编号化落位，
 *        WP-13-T10）：空表/空键/重复键逐项违例；合法工具通过；解码门经
 *        校验链④自动强制（空 TCP 表的工具字节不可入存——命令载荷与存储
 *        双通道共用同一判定）。
 *
 * 验证：acceptance 1（tcpList≥1——值模型与编辑流落位）。
 */
TEST(MdlParts, ToolTcpListIntegrity_IMdl13_WP13T10_ACC1)
{
    IRD_TEST_INFO("MDL-13", {}, std::nullopt);
    // 合法工具（恰一条 TCP）：无违例。
    EXPECT_TRUE(checkInvariants(makeValidToolT10()).empty());
    // 空 tcpList：违例（defaultTcp 的引用锚悬空——KIN-14 构造侧根源）。
    ToolDefinition empty = makeValidToolT10();
    empty.tcpList.clear();
    EXPECT_TRUE(hasViolation(checkInvariants(empty), InvariantId::IMdl13,
                             "tcpList.empty"));
    // TCP 键为空串：违例（键是 defaultTcp.tcpKey 的引用锚）。
    ToolDefinition emptyKey = makeValidToolT10();
    emptyKey.tcpList[0].key.clear();
    EXPECT_TRUE(hasViolation(checkInvariants(emptyKey), InvariantId::IMdl13,
                             "tcpList[0].key.empty"));
    // TCP 键重复：违例（同键二义——引用锚不唯一）。
    ToolDefinition dup = makeValidToolT10();
    TcpEntry second;
    second.key = "tcp-center";  // 与首条同键
    dup.tcpList.push_back(second);
    EXPECT_TRUE(hasViolation(checkInvariants(dup), InvariantId::IMdl13,
                             "tcpList[0].key.duplicate"));

    // 解码门自动强制：空表工具可编码（编码侧无校验——与既有编码纪律
    // 一致），但解码校验链④拒绝（malformed-invariant I-MDL-13）——非法
    // 对象不可能经载荷/存储进入断言域。
    sdurws::ird::modeling::RobotDesignCodec codec;
    auto encoded = codec.encode(
        sdurws::ird::modeling::ObjectVariant(empty),
        sdurws::ird::modeling::kCurrentFormatVersion);
    ASSERT_TRUE(encoded.ok());
    auto decoded = codec.decode(encoded.get(),
                                sdurws::ird::modeling::kCurrentFormatVersion);
    EXPECT_FALSE(decoded.ok()) << "空 TCP 表的工具字节应被解码门拒绝（I-MDL-13）";
}

/**
 * @brief 命名位姿保留键与合并编辑流（§4.6/D-MDL-3/MDL-17——WP-13-T10）：
 *        保留键字面集合、用户条目校验（空键/重复键/保留键越界/关节序
 *        一一对应）、保留键保留（V-27 建模侧）、键字典序规范化、用户全集
 *        替换语义。
 *
 * 验证：acceptance 4（参考键保留＋条目与关节序一一对应）。
 */
TEST(MdlParts, NamedPoseMergeReservedKeysAndJointOrder_WP13T10_ACC4)
{
    IRD_TEST_INFO("MDL-17", {}, std::nullopt);
    using sdurws::ird::modeling::PoseEditErrorCode;

    // 保留键字面集合（§4.6 原文；词表冻结不改拼）。
    EXPECT_TRUE(sdurws::ird::modeling::isReservedPoseKey("homeConfiguration"));
    EXPECT_TRUE(sdurws::ird::modeling::isReservedPoseKey("zeroConfiguration"));
    EXPECT_FALSE(sdurws::ird::modeling::isReservedPoseKey("home"));
    EXPECT_FALSE(sdurws::ird::modeling::isReservedPoseKey(""));

    auto entry = [](const std::string& key, std::vector<double> q) {
        sdurws::ird::modeling::PoseSetEntry e;
        e.key = key;
        e.jointConfiguration = std::move(q);  // rad（移动关节 m）——与关节序对应
        return e;
    };

    // —— 首建（无基线）：用户条目即产物，键字典序规范化——Ok ——
    {
        std::vector<sdurws::ird::modeling::PoseSetEntry> user = {
            entry("pick", {0.5}), entry("cruise", {0.1})};  // 乱序提交
        const auto out = sdurws::ird::modeling::mergeNamedPoseEntries(
            std::nullopt, std::move(user), 1);
        ASSERT_EQ(out.code, PoseEditErrorCode::Ok);
        ASSERT_TRUE(out.merged.has_value());
        ASSERT_EQ(out.merged->entries.size(), std::size_t{2});
        EXPECT_EQ(out.merged->entries[0].key, "cruise");  // 字典序（codec canonical 域）
        EXPECT_EQ(out.merged->entries[1].key, "pick");
    }

    // —— 保留键保留（V-27 建模侧）：基线保留键原样带入，用户全集替换
    //      旧用户条目 ——
    {
        sdurws::ird::modeling::PoseSet baseline;
        baseline.objectId = makeOid(300);
        baseline.entries = {
            entry("homeConfiguration", {0.0}),  // 保留键（Home 参考）
            entry("zeroConfiguration", {0.0}),  // 保留键（Zero 参考）
            entry("old", {0.9}),
        };
        std::vector<sdurws::ird::modeling::PoseSetEntry> user = {entry("new", {0.2})};
        const auto out = sdurws::ird::modeling::mergeNamedPoseEntries(
            baseline, std::move(user), 1);
        ASSERT_EQ(out.code, PoseEditErrorCode::Ok);
        ASSERT_TRUE(out.merged.has_value());
        // 产物序＝字典序：homeConfiguration < new < old? 否——old 被替换，
        // zeroConfiguration 在末尾（h < n < z）。
        ASSERT_EQ(out.merged->entries.size(), std::size_t{3});
        EXPECT_EQ(out.merged->entries[0].key, "homeConfiguration");
        EXPECT_EQ(out.merged->entries[1].key, "new");
        EXPECT_EQ(out.merged->entries[2].key, "zeroConfiguration");
        // 保留键条目值逐位保留（复位参考不被编辑破坏——KIN-06）。
        EXPECT_EQ(out.merged->entries[0].jointConfiguration,
                  std::vector<double>{0.0});
        // 身份继承基线（同一对象的新版本——ARC-04 跨修订稳定）。
        EXPECT_EQ(out.merged->objectId, baseline.objectId);
    }

    // —— 违例面（按编辑序首个违例即拒绝，不产出半产物——NFR-COR-03）——
    {
        std::vector<sdurws::ird::modeling::PoseSetEntry> user = {entry("", {0.0})};
        const auto out = sdurws::ird::modeling::mergeNamedPoseEntries(
            std::nullopt, std::move(user), 1);
        EXPECT_EQ(out.code, PoseEditErrorCode::EmptyKey);
    }
    {
        std::vector<sdurws::ird::modeling::PoseSetEntry> user = {
            entry("dup", {0.0}), entry("dup", {0.1})};
        const auto out = sdurws::ird::modeling::mergeNamedPoseEntries(
            std::nullopt, std::move(user), 1);
        EXPECT_EQ(out.code, PoseEditErrorCode::DuplicateKey);
        EXPECT_EQ(out.subject, "dup");
    }
    {
        std::vector<sdurws::ird::modeling::PoseSetEntry> user = {
            entry("homeConfiguration", {0.0})};
        const auto out = sdurws::ird::modeling::mergeNamedPoseEntries(
            std::nullopt, std::move(user), 1);
        EXPECT_EQ(out.code, PoseEditErrorCode::ReservedKeyInEdit);
        // 保留键越出面：MDL-17"除 Home/Zero 外保存、命名与恢复"字面。
    }
    {
        std::vector<sdurws::ird::modeling::PoseSetEntry> user = {
            entry("wrong", {0.1, 0.2})};  // 长度 2 ≠ 关节表 1
        const auto out = sdurws::ird::modeling::mergeNamedPoseEntries(
            std::nullopt, std::move(user), 1);
        EXPECT_EQ(out.code, PoseEditErrorCode::JointOrderMismatch);
        EXPECT_EQ(out.subject, "wrong");
    }
}

/**
 * 工具安装接口/场景世界位姿编辑（UI-T55——F-497 兑现③域面）：接受＝六标量
 * 组合为位姿（平移直写＋旋转经域内核 ZYX 正解）＋恰一条变更记录；非有限
 * 拒绝（I-MDL-3）工作集字节不变；越界下标 fail-fast（调用方契约违约）。
 *
 * 旋转核验与 TemplateTest Origin 用例同口径：R20＝-sin(pitch) 字面断言＋
 * 独立反解 1e-12 容差互证（不复制正解公式自证）。
 */
TEST(MdlParts, PartPoseEdits_ToolMountAndSceneWorld_UI_T55)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-22"},
                  std::vector<std::string>{"AT-01"});

    ModelingWorkingSet ws;
    ToolDefinition tool;
    tool.localName = "t1";
    ws.toolObjects.push_back(tool);
    SceneObject scene;
    scene.localName = "s1";
    ws.sceneObjects.push_back(scene);

    // ---- 接受面①：工具安装接口（法兰系 T_flange_tool）----
    constexpr double kRoll = 0.1;
    constexpr double kPitch = 0.2;
    constexpr double kYaw = 0.3;
    const auto toolRejection = applyToolMountEdit(
        ws, 0, PartPoseEditValue{0.4, -0.5, 0.6, kRoll, kPitch, kYaw});
    EXPECT_FALSE(toolRejection.has_value()) << "合法工具安装接口编辑应接受";
    const auto& mount = ws.toolObjects[0].mountInterface;
    EXPECT_DOUBLE_EQ(mount.P()[0], 0.4);
    EXPECT_DOUBLE_EQ(mount.P()[2], 0.6);
    EXPECT_DOUBLE_EQ(mount.R()(2, 0), -std::sin(kPitch));
    const double pitchBack = -std::asin(mount.R()(2, 0));
    const double rollBack = std::atan2(mount.R()(2, 1), mount.R()(2, 2));
    const double yawBack = std::atan2(mount.R()(1, 0), mount.R()(0, 0));
    EXPECT_NEAR(rollBack, kRoll, 1e-12);
    EXPECT_NEAR(pitchBack, kPitch, 1e-12);
    EXPECT_NEAR(yawBack, kYaw, 1e-12);
    ASSERT_EQ(ws.changes.size(), std::size_t{1});
    EXPECT_EQ(ws.changes[0].subject, "tools[0]");
    EXPECT_NE(ws.changes[0].summary.find("安装接口"), std::string::npos);

    // ---- 接受面②：场景世界位姿（世界系固连——M-11）----
    const auto sceneRejection = applyScenePoseEdit(
        ws, 0, PartPoseEditValue{1.0, 2.0, 3.0, 0.0, 0.0, 0.5});
    EXPECT_FALSE(sceneRejection.has_value());
    const auto& world = ws.sceneObjects[0].worldPose;
    EXPECT_DOUBLE_EQ(world.P()[0], 1.0);
    EXPECT_DOUBLE_EQ(world.R()(2, 0), -std::sin(0.0));
    EXPECT_NEAR(std::atan2(world.R()(1, 0), world.R()(0, 0)), 0.5, 1e-12);
    ASSERT_EQ(ws.changes.size(), std::size_t{2});
    EXPECT_EQ(ws.changes[1].subject, "scenes[0]");
    EXPECT_NE(ws.changes[1].summary.find("世界位姿"), std::string::npos);

    // ---- 拒绝面：非有限分量（工作集字节不变——I-MDL-3）----
    const ModelingWorkingSet before = ws;
    const auto nanRejection = applyToolMountEdit(
        ws, 0, PartPoseEditValue{0.0, 0.0, 0.0, 0.0, std::nan(""), 0.0});
    ASSERT_TRUE(nanRejection.has_value());
    EXPECT_EQ(nanRejection->code, PartPoseEditErrorCode::ValueNotFinite);
    EXPECT_EQ(partPoseEditErrorCodeToken(PartPoseEditErrorCode::ValueNotFinite),
              "value-not-finite");
    const auto sceneNan = applyScenePoseEdit(
        ws, 0, PartPoseEditValue{std::nan(""), 0.0, 0.0, 0.0, 0.0, 0.0});
    ASSERT_TRUE(sceneNan.has_value());
    EXPECT_EQ(ws, before) << "拒绝路径工作集字节不变";

    // ---- fail-fast 面：越界下标（调用方契约违约）----
    EXPECT_THROW(applyToolMountEdit(ws, 9, PartPoseEditValue{}),
                 std::invalid_argument);
    EXPECT_THROW(applyScenePoseEdit(ws, 9, PartPoseEditValue{}),
                 std::invalid_argument);
}

/**
 * TCP 列表结构化编辑（UI-T57——F-497 兑现④；MDL-13 不变量流）：
 * 新增（键唯一/非空＋offset ZYX 正解）→删除（键存在/最后一条保护/
 * defaultTcp 引用保护）→offset 编辑→默认切换（根对象写入）。
 * 全拒绝路径工作集字节不变（域内强保证）。
 */
TEST(MdlParts, TcpListEdits_AddRemoveOffsetDefault_UI_T57)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-13"}, std::vector<std::string>{"AT-01"});

    ModelingWorkingSet ws;
    ToolDefinition tool;
    tool.localName = "t1";
    tool.objectId = makeOid(701);
    tool.tcpList.push_back(TcpEntry{});  // 种子键 "" ——先补首个合法键
    tool.tcpList[0].key = "tcp-1";
    ws.toolObjects.push_back(tool);

    // ---- 新增：合法键 tcp-2（offset 全零＋yaw 0.25）----
    const auto add = applyTcpAddEdit(ws, 0, "tcp-2", "法兰 2",
                                     PartPoseEditValue{0.0, 0.0, 0.1, 0.0, 0.0, 0.25});
    EXPECT_FALSE(add.has_value()) << "合法新增应接受";
    ASSERT_EQ(ws.toolObjects[0].tcpList.size(), std::size_t{2});
    EXPECT_DOUBLE_EQ(ws.toolObjects[0].tcpList[1].offset.P()[2], 0.1);
    EXPECT_NEAR(std::atan2(ws.toolObjects[0].tcpList[1].offset.R()(1, 0),
                           ws.toolObjects[0].tcpList[1].offset.R()(0, 0)),
                0.25, 1e-12);
    ASSERT_EQ(ws.changes.size(), std::size_t{1});
    EXPECT_NE(ws.changes[0].summary.find("tcp-2"), std::string::npos);

    // ---- 新增拒绝面：空键/重复键（I-MDL-13）----
    const ModelingWorkingSet before = ws;
    const auto emptyKey = applyTcpAddEdit(ws, 0, "", "x",
                                          PartPoseEditValue{});
    ASSERT_TRUE(emptyKey.has_value());
    EXPECT_EQ(emptyKey->code, TcpEditErrorCode::KeyEmpty);
    const auto dupKey = applyTcpAddEdit(ws, 0, "tcp-2", "x",
                                        PartPoseEditValue{});
    ASSERT_TRUE(dupKey.has_value());
    EXPECT_EQ(dupKey->code, TcpEditErrorCode::KeyDuplicate);
    EXPECT_EQ(ws, before) << "拒绝路径工作集字节不变";

    // ---- 删除拒绝面：最后一条保护（tcpList 仅 1 条时删 tcp-1）----
    // 先删 tcp-2 使表剩 1 条（合法——表非空），再删最后一条拒绝。
    const auto remove2 = applyTcpRemoveEdit(ws, 0, "tcp-2");
    EXPECT_FALSE(remove2.has_value());
    ASSERT_EQ(ws.changes.size(), std::size_t{2});
    const ModelingWorkingSet beforeLast = ws;
    const auto removeLast = applyTcpRemoveEdit(ws, 0, "tcp-1");
    ASSERT_TRUE(removeLast.has_value());
    EXPECT_EQ(removeLast->code, TcpEditErrorCode::LastTcpProtected);
    EXPECT_EQ(ws, beforeLast) << "最后一条保护：工作集字节不变";

    // ---- defaultTcp 引用保护：根引用 tcp-1 后再删拒绝（I-MDL-9）----
    EXPECT_FALSE(applyDefaultTcpSwitchEdit(ws, 0, "tcp-1").has_value());
    ASSERT_EQ(ws.changes.size(), std::size_t{3});
    EXPECT_EQ(ws.design.defaultTcp->toolOid, makeOid(701));
    EXPECT_EQ(ws.design.defaultTcp->tcpKey, "tcp-1");
    const ModelingWorkingSet beforeRef = ws;
    const auto removeRef = applyTcpRemoveEdit(ws, 0, "tcp-1");
    ASSERT_TRUE(removeRef.has_value());
    EXPECT_EQ(removeRef->code, TcpEditErrorCode::DefaultTcpReferenced);
    EXPECT_EQ(ws, beforeRef) << "引用保护：工作集字节不变";

    // ---- offset 编辑：键存在→ZYX 正解写入＋变更记录；键不存在→拒绝 ----
    const auto offsetEdit = applyTcpOffsetEdit(
        ws, 0, "tcp-1", PartPoseEditValue{0.0, 0.0, 0.2, 0.0, 0.0, 0.0});
    EXPECT_FALSE(offsetEdit.has_value());
    EXPECT_DOUBLE_EQ(ws.toolObjects[0].tcpList[0].offset.P()[2], 0.2);
    const auto notFound = applyTcpOffsetEdit(ws, 0, "nope",
                                             PartPoseEditValue{});
    ASSERT_TRUE(notFound.has_value());
    EXPECT_EQ(notFound->code, TcpEditErrorCode::KeyNotFound);

    // ---- offset 编辑 ValueNotFinite 直断言（F-516⑦——此前该拒绝路径仅
    // 经新增混入非有限 offset 间接覆盖；此处 NaN 分量直接命中 I-MDL-3）----
    const ModelingWorkingSet beforeNonFinite = ws;
    const auto nonFinite = applyTcpOffsetEdit(
        ws, 0, "tcp-1",
        PartPoseEditValue{0.0, 0.0, std::numeric_limits<double>::quiet_NaN(),
                          0.0, 0.0, 0.0});
    ASSERT_TRUE(nonFinite.has_value());
    EXPECT_EQ(nonFinite->code, TcpEditErrorCode::ValueNotFinite);
    EXPECT_EQ(ws, beforeNonFinite) << "非有限拒绝：工作集字节不变";

    // ---- 越界 fail-fast（调用方契约违约）----
    EXPECT_THROW(applyTcpAddEdit(ws, 9, "x", "x", PartPoseEditValue{}),
                 std::invalid_argument);
    EXPECT_THROW(applyTcpRemoveEdit(ws, 9, "tcp-1"), std::invalid_argument);
    EXPECT_THROW(applyTcpOffsetEdit(ws, 9, "tcp-1", PartPoseEditValue{}),
                 std::invalid_argument);
    EXPECT_THROW(applyDefaultTcpSwitchEdit(ws, 9, "tcp-1"),
                 std::invalid_argument);
    EXPECT_THROW(applyTcpDisplayNameEdit(ws, 9, "tcp-1", "x"),
                 std::invalid_argument);

    // ---- token（词表对账）----
    EXPECT_EQ(tcpEditErrorCodeToken(TcpEditErrorCode::LastTcpProtected),
              "last-tcp-protected");
}

/**
 * TCP 显示名编辑（UI-T58——UI-T57 卡"诚实边界"顺延项；§4.4 tcpList
 * displayName 行）：接受路径直写＋恰一条变更记录（键与 offset 字节不变）；
 * 空串接受（仅呈现字段——MDL-13 不变量只约束键与列表长度）；键不存在
 * 拒绝字节不变；越界 fail-fast（越界断言并入上方 UI_T57 用例尾段）。
 */
TEST(MdlParts, TcpDisplayNameEdit_PresentationOnlyField_UI_T58)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-13"}, std::vector<std::string>{"UX-02"});

    ModelingWorkingSet ws;
    ToolDefinition tool;
    tool.localName = "t1";
    tool.objectId = makeOid(711);
    TcpEntry tcp0;
    tcp0.key = "tcp-1";
    tcp0.displayName = "TCP tcp-1";
    tool.tcpList.push_back(tcp0);
    ws.toolObjects.push_back(tool);

    // ---- 接受路径：displayName 直写＋一条变更记录；键（引用锚）不动 ----
    const auto edit = applyTcpDisplayNameEdit(ws, 0, "tcp-1", "法兰中心");
    EXPECT_FALSE(edit.has_value()) << "合法显示名编辑应接受";
    EXPECT_EQ(ws.toolObjects[0].tcpList[0].displayName, "法兰中心");
    EXPECT_EQ(ws.toolObjects[0].tcpList[0].key, "tcp-1");
    ASSERT_EQ(ws.changes.size(), std::size_t{1});
    EXPECT_NE(ws.changes[0].summary.find("tcp-1"), std::string::npos);
    EXPECT_NE(ws.changes[0].summary.find("仅呈现"), std::string::npos);

    // ---- 空串接受：仅呈现字段无内容约束（呈现侧回落按 TCP 键呈现）----
    EXPECT_FALSE(applyTcpDisplayNameEdit(ws, 0, "tcp-1", "").has_value());
    EXPECT_EQ(ws.toolObjects[0].tcpList[0].displayName, "");

    // ---- 键不存在拒绝：工作集字节不变 ----
    const ModelingWorkingSet before = ws;
    const auto notFound = applyTcpDisplayNameEdit(ws, 0, "nope", "x");
    ASSERT_TRUE(notFound.has_value());
    EXPECT_EQ(notFound->code, TcpEditErrorCode::KeyNotFound);
    EXPECT_EQ(ws, before) << "拒绝路径工作集字节不变";
}
