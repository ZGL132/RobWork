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
