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
