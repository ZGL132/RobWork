/**
 * @file   RobotDesignTest.cpp
 * @brief  RobotDesign 值模型用例组（MdlRobotDesign）——合法实例不变量
 *         I-MDL-1～I-MDL-10 逐条注册（用例名绑定卡 §4.10 编号）＋双权威
 *         编辑权限守卫（§7.3 C-1/C-2；V-14 场景——MDL-02/09）。
 *
 * 设计依据：
 *   - units/modeling.md §4.10（不变量表——"用例名绑定 I-MDL-1～I-MDL-12
 *     编号"，任务契约 WP-13-T03 acceptance 3）、§7.1～§7.3（双权威/冲突
 *     矩阵）、V-14 行（"DH 权威模型|编辑 axis|调用方错误拒绝＋'派生只读'
 *     提示|AuthorityViolation；工作集字节不变"——acceptance 5）、§4.3
 *     （字段表——夹具字段语义）
 *   - 需求 MDL-01/02/09（任务 requirements 列）、NFR-COR-03（不静默修复）
 *   - 任务契约 tasks/foundation/WP-13-T03.json acceptance 3/5
 */

#include <sdurws/ird/modeling/Codec.hpp>      // 编码器——V-14"工作集字节不变"以字节相等断言
#include <sdurws/ird/modeling/ObjectTypes.hpp>
#include <sdurws/ird/modeling/Parts.hpp>     // 传动不变量 I-MDL-11/12（acceptance 3 的 R1 coupling 拒绝）
#include <sdurws/ird/modeling/RobotDesign.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

using sdurws::ird::core::FieldState;
using sdurws::ird::core::ObjectId;
using sdurws::ird::core::ProvenanceKind;
using sdurws::ird::core::SourcedValue;
using sdurws::ird::core::ValueProvenance;
using sdurws::ird::modeling::AuthorityLockedField;
using sdurws::ird::modeling::AuthorityMode;
using sdurws::ird::modeling::BasePlacement;
using sdurws::ird::modeling::BodyData;
using sdurws::ird::modeling::CouplingDesign;
using sdurws::ird::modeling::CouplingStage;
using sdurws::ird::modeling::DrivetrainDesign;
using sdurws::ird::modeling::DhParameters;
using sdurws::ird::modeling::GeometryKind;
using sdurws::ird::modeling::GeometryRef;
using sdurws::ird::modeling::InertiaTensor;
using sdurws::ird::modeling::InvariantId;
using sdurws::ird::modeling::InvariantViolation;
using sdurws::ird::modeling::JointEntry;
using sdurws::ird::modeling::JointLimits;
using sdurws::ird::modeling::JointType;
using sdurws::ird::modeling::LinkEntry;
using sdurws::ird::modeling::ResourceRef;
using sdurws::ird::modeling::ResourceState;
using sdurws::ird::modeling::RobotDesign;
using sdurws::ird::modeling::TcpRef;
using sdurws::ird::modeling::authorityEditGuard;
using sdurws::ird::modeling::checkInvariants;
using sdurws::ird::modeling::invariantIdToken;
using sdurws::ird::modeling::kCurrentFormatVersion;

namespace rwmath = rw::math;
namespace runtime = sdurws::ird::runtime;  // 预设词表（InstallationPresetToken——runtime 公共面）

namespace {

// =====================================================================
// 夹具辅助（全部确定性——Id 按 v 定长十六进制构造，不用随机 generate：
// 编码字节比较要求跨用例可读的身份）
// =====================================================================

/// 由 64 位序号构造确定 ObjectId（"obj-<32hex>"——低 8 位十六进制有效）。
ObjectId makeOid(std::uint64_t v)
{
    char text[40] = {};
    std::snprintf(text, sizeof(text), "obj-%024llx%08llx",
                  static_cast<unsigned long long>(0),
                  static_cast<unsigned long long>(v));
    return ObjectId::fromCanonical(text);
}

/// 用户来源的 SourcedValue（ValueProvenance 工厂——P-1 校验过的最小构造）。
template <class T>
SourcedValue<T> provided(T value, ProvenanceKind kind = ProvenanceKind::UserProvided)
{
    return SourcedValue<T>::provided(std::move(value), ValueProvenance::make(kind));
}

/// 恒等位姿（逐元素构造——冒烟纪律：不调用外联符号，runtime Description
/// detail::identityTransform3D 同款）。
rwmath::Transform3D<double> identityPose()
{
    return rwmath::Transform3D<double>(
        rwmath::Vector3D<double>(0.0, 0.0, 0.0),
        rwmath::Rotation3D<double>(1.0, 0.0, 0.0,
                                   0.0, 1.0, 0.0,
                                   0.0, 0.0, 1.0));
}

/// 有限限位（±π/2，rad——旋转关节常用半开摆幅）。
JointLimits halfPiLimits() { return {-1.5707963267948966, 1.5707963267948966}; }

/// 单位惯量（对角 0.01 kg·m²——SPD 且满足三角不等式：0.01≤0.01+0.01）。
InertiaTensor unitInertia()
{
    InertiaTensor t;
    t.ixx = 0.01;
    t.iyy = 0.01;
    t.izz = 0.01;
    return t;
}

/// 合法关节（Revolute，axis=+Z 单位向量——I-MDL-6 通过；rad 限位）。
JointEntry makeJoint(std::uint64_t id, const std::string& name)
{
    JointEntry j;
    j.objectId = makeOid(id);
    j.localName = name;
    j.type = JointType::Revolute;
    j.axis = provided(rwmath::Vector3D<double>(0.0, 0.0, 1.0));
    // 原点：恒等 T_parent_joint（JointPose＝Transform3D 同构值类型，冒烟
    // header-only 纪律——见 RobotDesign.hpp JointPose 类注）
    j.origin = provided(sdurws::ird::modeling::JointPose(identityPose()));
    j.zeroOffset = 0.0;  // rad
    j.bounds = provided(halfPiLimits());
    return j;
}

/// 合法连杆（质量 1 kg＋单位惯量——I-MDL-5 通过）。
LinkEntry makeLink(std::uint64_t id, const std::string& name)
{
    LinkEntry l;
    l.objectId = makeOid(id);
    l.localName = name;
    l.body.mass = provided(1.0);  // kg
    l.body.centerOfMass = provided(rwmath::Vector3D<double>(0.0, 0.0, 0.05));  // m
    l.body.inertia = provided(unitInertia());
    return l;
}

/// 合法显式权威设计（2 关节＋3 连杆；checkInvariants 应为空——各违例用例
/// 以它为基底做单点破坏，保证"只触发目标不变量"）。
RobotDesign makeValidExplicitDesign()
{
    RobotDesign d;
    d.schemaVersion = sdurws::ird::modeling::kRobotDesignSchemaVersion;
    d.displayName = "six-axis-reference";
    d.authority = AuthorityMode::Explicit;
    d.basePlacement.preset = runtime::InstallationPresetToken::Ground;
    d.basePlacement.basePosition = provided(rwmath::Vector3D<double>(0.0, 0.0, 0.75));  // m
    d.joints = {makeJoint(1, "j1"), makeJoint(2, "j2")};
    d.links = {makeLink(11, "base_link"), makeLink(12, "l2"), makeLink(13, "l3")};
    d.toolRefs = {makeOid(100)};
    d.defaultTcp = TcpRef{makeOid(100), "tcp-center"};
    return d;
}

/// 合法 DH 权威设计（axis/origin 派生待重算＝NotProvided；dhDerived 权威）。
RobotDesign makeValidStandardDhDesign()
{
    RobotDesign d = makeValidExplicitDesign();
    d.authority = AuthorityMode::StandardDH;
    DhParameters dh;
    dh.thetaOffset = 0.0;  // rad
    dh.d = 0.30;           // m
    dh.a = 0.45;           // m
    dh.alpha = 1.5707963267948966;  // rad（π/2——相邻轴垂直的典型构型）
    for (JointEntry& j : d.joints) {
        j.axis = SourcedValue<rwmath::Vector3D<double>>::notProvided();   // 派生待重算
        j.origin = SourcedValue<sdurws::ird::modeling::JointPose>::notProvided();
        j.dhDerived = dh;
    }
    return d;
}

/// 断言辅助：违例清单恰含指定不变量的指定子串定位。
::testing::AssertionResult hasViolation(const std::vector<InvariantViolation>& v,
                                         InvariantId id, const std::string& subjectPart)
{
    for (const InvariantViolation& item : v) {
        if (item.id == id && item.subject.find(subjectPart) != std::string::npos) {
            return ::testing::AssertionSuccess();
        }
    }
    return ::testing::AssertionFailure()
           << "未找到违例 " << invariantIdToken(id) << "（subject 含 \"" << subjectPart
           << "\"）；实际条数=" << v.size();
}

/// 合法传动（两关节 ratio 均 Provided>0；无 coupling——I-MDL-11 通过）。
DrivetrainDesign makeValidDrivetrain()
{
    DrivetrainDesign dt;
    dt.schemaVersion = sdurws::ird::modeling::kRobotDrivetrainSchemaVersion;
    dt.objectId = makeOid(300);
    dt.ratioPerJoint = {provided(101.0), provided(121.0)};  // 无量纲（减速比）
    return dt;
}

}  // namespace

// =====================================================================
// 不变量 I-MDL-1～I-MDL-12 逐条注册（acceptance 3——用例名绑定编号）
// =====================================================================

/// 基底合法性自检：夹具本身必须全过——否则下方"单点破坏"用例失去对照面。
TEST(MdlRobotDesign, ValidExplicitDesignPassesAllInvariants_WP13T03_ACC3)
{
    IRD_TEST_INFO("MDL-01", {}, std::nullopt);
    EXPECT_TRUE(checkInvariants(makeValidExplicitDesign()).empty());
    EXPECT_TRUE(checkInvariants(makeValidStandardDhDesign()).empty());
    EXPECT_TRUE(checkInvariants(makeValidDrivetrain(), CouplingStage::R1Locked).empty());
}

/// I-MDL-1（结构）：joints≥1；links==joints+1（runtime StructureInvalid 同口径）。
TEST(MdlRobotDesign, IMdl1_StructureSizes_WP13T03_ACC3)
{
    IRD_TEST_INFO("MDL-01", {}, std::nullopt);
    // 空关节链：joints≥1 违例
    RobotDesign noJoints = makeValidExplicitDesign();
    noJoints.joints.clear();
    noJoints.links.clear();
    EXPECT_TRUE(hasViolation(checkInvariants(noJoints), InvariantId::IMdl1, "joints"));
    // 连杆数≠关节数+1：runtime 同口径违例
    RobotDesign badLinks = makeValidExplicitDesign();
    badLinks.links.pop_back();
    EXPECT_TRUE(hasViolation(checkInvariants(badLinks), InvariantId::IMdl1, "links"));
}

/// I-MDL-2（身份唯一）：同模型 ObjectId/localName 不得重复（不静默重命名）。
TEST(MdlRobotDesign, IMdl2_IdentityUniqueness_WP13T03_ACC3)
{
    IRD_TEST_INFO("ARC-04", {}, std::nullopt);
    // ObjectId 重复：关节与连杆同身份
    RobotDesign dupOid = makeValidExplicitDesign();
    dupOid.links[1].objectId = dupOid.joints[0].objectId;
    EXPECT_TRUE(hasViolation(checkInvariants(dupOid), InvariantId::IMdl2, "objectId"));
    // localName 重复（同作用域＝关节∪连杆命名空间）
    RobotDesign dupName = makeValidExplicitDesign();
    dupName.links[0].localName = dupName.joints[0].localName;
    EXPECT_TRUE(hasViolation(checkInvariants(dupName), InvariantId::IMdl2, "localName"));
}

/// I-MDL-3（单位合法）：Provided 值含 NaN/Inf 即非法——不静默置 0。
TEST(MdlRobotDesign, IMdl3_ProvidedValuesFinite_WP13T03_ACC3)
{
    IRD_TEST_INFO("NFR-COR-03", {}, std::nullopt);
    // NaN 轴向（无量纲）：NotFinite 面的 I-MDL-3 违例
    RobotDesign nanAxis = makeValidExplicitDesign();
    nanAxis.joints[0].axis = provided(rwmath::Vector3D<double>(
        std::numeric_limits<double>::quiet_NaN(), 0.0, 1.0));
    EXPECT_TRUE(hasViolation(checkInvariants(nanAxis), InvariantId::IMdl3, "joints[0].axis"));
    // Inf 零位偏置（rad）
    RobotDesign infOffset = makeValidExplicitDesign();
    infOffset.joints[1].zeroOffset = std::numeric_limits<double>::infinity();
    EXPECT_TRUE(hasViolation(checkInvariants(infOffset), InvariantId::IMdl3, "joints[1].zeroOffset"));
    // Inf 质量（kg）
    RobotDesign infMass = makeValidExplicitDesign();
    infMass.links[0].body.mass = provided(std::numeric_limits<double>::infinity());
    EXPECT_TRUE(hasViolation(checkInvariants(infMass), InvariantId::IMdl3, "links[0].body.mass"));
    // 缺失（NotProvided）不触发 I-MDL-3——MDL-06 降级语义
    RobotDesign missingBase = makeValidExplicitDesign();
    missingBase.basePlacement.basePosition = SourcedValue<rwmath::Vector3D<double>>::notProvided();
    EXPECT_TRUE(checkInvariants(missingBase).empty());
}

/// I-MDL-4（限位有序/适用）＋§7.3 非法组合：continuous 带有限 bounds、
/// prismatic 带 workingRange、qmin≥qmax。
TEST(MdlRobotDesign, IMdl4_LimitOrderAndIllegalCombinations_WP13T03_ACC3)
{
    IRD_TEST_INFO("MDL-06", {}, std::nullopt);
    // 非法组合①：continuous 带有限 bounds（§7.3——bounds 应 NotApplicable）
    RobotDesign contWithBounds = makeValidExplicitDesign();
    contWithBounds.joints[0].type = JointType::Continuous;
    contWithBounds.joints[0].bounds = provided(halfPiLimits());
    EXPECT_TRUE(hasViolation(checkInvariants(contWithBounds), InvariantId::IMdl4,
                             "bounds.continuous"));
    // 非法组合②：prismatic 带 workingRange（§7.3——workingRange 仅 continuous）
    RobotDesign prismWithRange = makeValidExplicitDesign();
    prismWithRange.joints[1].type = JointType::Prismatic;
    prismWithRange.joints[1].bounds = provided(JointLimits{0.0, 0.5});  // m
    prismWithRange.joints[1].workingRange = provided(JointLimits{-3.0, 3.0});  // rad
    EXPECT_TRUE(hasViolation(checkInvariants(prismWithRange), InvariantId::IMdl4,
                             "workingRange.nonContinuous"));
    // 断言④前半：有限限位 qmin≥qmax（rad）
    RobotDesign badOrder = makeValidExplicitDesign();
    badOrder.joints[0].bounds = provided(JointLimits{1.0, -1.0});
    EXPECT_TRUE(hasViolation(checkInvariants(badOrder), InvariantId::IMdl4, "bounds.order"));
    // continuous 的 workingRange 必须有限区间 qmin'<qmax'
    RobotDesign badRange = makeValidExplicitDesign();
    badRange.joints[0].type = JointType::Continuous;
    badRange.joints[0].bounds = SourcedValue<JointLimits>::notApplicable();  // ERR-01 显式
    badRange.joints[0].workingRange = provided(JointLimits{3.0, -3.0});
    EXPECT_TRUE(hasViolation(checkInvariants(badRange), InvariantId::IMdl4,
                             "workingRange.order"));
}

/// I-MDL-5（物性合法）：m>0（断言①）；惯量 SPD（断言②）＋三角不等式（断言③）。
TEST(MdlRobotDesign, IMdl5_BodyPhysicalAssertions_WP13T03_ACC3)
{
    IRD_TEST_INFO("MDL-05", {}, std::nullopt);
    // 断言①：质量≤0（kg）
    RobotDesign badMass = makeValidExplicitDesign();
    badMass.links[0].body.mass = provided(0.0);
    EXPECT_TRUE(hasViolation(checkInvariants(badMass), InvariantId::IMdl5, "mass"));
    // 断言②：非正定惯量（对角 (1,1,-1) kg·m²——最小特征值<0）
    RobotDesign notSpd = makeValidExplicitDesign();
    InertiaTensor indefinite;
    indefinite.ixx = 1.0;
    indefinite.iyy = 1.0;
    indefinite.izz = -1.0;
    notSpd.links[0].body.inertia = provided(indefinite);
    EXPECT_TRUE(hasViolation(checkInvariants(notSpd), InvariantId::IMdl5, "inertia.spd"));
    // 断言③：三角不等式（对角 (1,1,3)——3>1+1，主矩不能构成三角形）
    RobotDesign badTriangle = makeValidExplicitDesign();
    InertiaTensor elongated;
    elongated.ixx = 1.0;
    elongated.iyy = 1.0;
    elongated.izz = 3.0;
    badTriangle.links[0].body.inertia = provided(elongated);
    EXPECT_TRUE(hasViolation(checkInvariants(badTriangle), InvariantId::IMdl5,
                             "inertia.triangle"));
    // 缺失物性不触发断言（DataInsufficient 降级——MDL-06）
    RobotDesign missingBody = makeValidExplicitDesign();
    missingBody.links[2].body = BodyData{};
    EXPECT_TRUE(checkInvariants(missingBody).empty());
}

/// I-MDL-6（轴有效）：可动关节 axis 非零、有限、可归一化；Fixed 不适用。
TEST(MdlRobotDesign, IMdl6_AxisValidity_WP13T03_ACC3)
{
    IRD_TEST_INFO("MDL-11", {}, std::nullopt);
    // 零轴＝非法（导入侧 MDL-11 仅报告；构造/编辑边界拒绝）
    RobotDesign zeroAxis = makeValidExplicitDesign();
    zeroAxis.joints[0].axis = provided(rwmath::Vector3D<double>(0.0, 0.0, 0.0));
    EXPECT_TRUE(hasViolation(checkInvariants(zeroAxis), InvariantId::IMdl6, "axis.zero"));
    // Fixed 关节无轴语义——不适用（axis 缺失不违例）
    RobotDesign fixedJoint = makeValidExplicitDesign();
    fixedJoint.joints[1].type = JointType::Fixed;
    fixedJoint.joints[1].axis = SourcedValue<rwmath::Vector3D<double>>::notProvided();
    fixedJoint.joints[1].bounds = SourcedValue<JointLimits>::notApplicable();
    EXPECT_TRUE(checkInvariants(fixedJoint).empty());
}

/// I-MDL-7（基座合法）：custom 预设必填 customEaa（rad）且旋转正交（1×10⁻¹²）。
TEST(MdlRobotDesign, IMdl7_BasePlacementCustom_WP13T03_ACC3)
{
    IRD_TEST_INFO("MDL-22", {}, std::nullopt);
    // custom 而 customEaa 缺失：非法（runtime §6.1 同口径）
    RobotDesign customMissing = makeValidExplicitDesign();
    customMissing.basePlacement.preset = runtime::InstallationPresetToken::Custom;
    EXPECT_TRUE(hasViolation(checkInvariants(customMissing), InvariantId::IMdl7,
                             "customEaa.missing"));
    // custom＋合法 EAA（绕 Y 轴 π/2——Rodrigues 产物正交）：通过
    RobotDesign customOk = makeValidExplicitDesign();
    customOk.basePlacement.preset = runtime::InstallationPresetToken::Custom;
    customOk.basePlacement.customEaa = provided(rwmath::Vector3D<double>(
        0.0, 1.5707963267948966, 0.0));  // rad
    EXPECT_TRUE(checkInvariants(customOk).empty());
}

/// I-MDL-8（权威互斥）：StandardDH 态 axis/origin 不得为非派生来源；
/// DerivedReadOnly 缓存载态与 NotProvided 待重算载态均合法。
TEST(MdlRobotDesign, IMdl8_AuthorityExclusivity_WP13T03_ACC3)
{
    IRD_TEST_INFO("MDL-02", {}, std::nullopt);
    // §7.3 非法组合：authority=StandardDH 且 axis 为 UserProvided 来源
    RobotDesign userAxisInDh = makeValidStandardDhDesign();
    userAxisInDh.joints[0].axis = provided(rwmath::Vector3D<double>(0.0, 0.0, 1.0));
    EXPECT_TRUE(hasViolation(checkInvariants(userAxisInDh), InvariantId::IMdl8,
                             "axis.provenance"));
    // 派生缓存已重算（DerivedReadOnly 来源）：合法载态
    RobotDesign derivedAxis = makeValidStandardDhDesign();
    derivedAxis.joints[0].axis =
        provided(rwmath::Vector3D<double>(0.0, 0.0, 1.0), ProvenanceKind::DerivedReadOnly);
    EXPECT_TRUE(checkInvariants(derivedAxis).empty());
    // 显式权威下 dhDerived 缓存存在：不违例（不参与编码身份——D-MDL-5，
    // 编码面用例见 CodecTest）
    RobotDesign explicitWithCache = makeValidExplicitDesign();
    explicitWithCache.joints[0].dhDerived = DhParameters{};
    EXPECT_TRUE(checkInvariants(explicitWithCache).empty());
}

/// I-MDL-9（引用完整，值模型可判面）：defaultTcp∈toolRefs；引用表无重复。
TEST(MdlRobotDesign, IMdl9_ReferenceIntegrity_WP13T03_ACC3)
{
    IRD_TEST_INFO("KIN-14", {}, std::nullopt);
    // defaultTcp 指向引用表外的工具：拒绝
    RobotDesign danglingTcp = makeValidExplicitDesign();
    danglingTcp.defaultTcp = TcpRef{makeOid(999), "tcp-center"};
    EXPECT_TRUE(hasViolation(checkInvariants(danglingTcp), InvariantId::IMdl9,
                             "defaultTcp.toolOid"));
    // toolRefs 重复：拒绝（"无重复"——§4.3 表行）
    RobotDesign dupToolRef = makeValidExplicitDesign();
    dupToolRef.toolRefs.push_back(makeOid(100));
    EXPECT_TRUE(hasViolation(checkInvariants(dupToolRef), InvariantId::IMdl9, "toolRefs"));
    // sceneRefs 重复：同规则
    RobotDesign dupSceneRef = makeValidExplicitDesign();
    dupSceneRef.sceneRefs = {makeOid(200), makeOid(200)};
    EXPECT_TRUE(hasViolation(checkInvariants(dupSceneRef), InvariantId::IMdl9, "sceneRefs"));
}

/// I-MDL-10（资源状态机）：Recorded 带 externalRecord；Solidified 带
/// solidifiedObject（CON-03 固化单向）。
TEST(MdlRobotDesign, IMdl10_ResourceStateMachine_WP13T03_ACC3)
{
    IRD_TEST_INFO("CON-03", {}, std::nullopt);
    // Recorded 缺 externalRecord：违例
    RobotDesign bareRecorded = makeValidExplicitDesign();
    ResourceRef res;
    res.resourceId = "mesh-base";
    res.state = ResourceState::Recorded;
    bareRecorded.resourceManifest.push_back(res);
    EXPECT_TRUE(hasViolation(checkInvariants(bareRecorded), InvariantId::IMdl10,
                             "externalRecord"));
    // Solidified 缺 solidifiedObject：违例
    RobotDesign bareSolidified = makeValidExplicitDesign();
    ResourceRef solid;
    solid.resourceId = "mesh-base";
    solid.state = ResourceState::Solidified;
    bareSolidified.resourceManifest.push_back(solid);
    EXPECT_TRUE(hasViolation(checkInvariants(bareSolidified), InvariantId::IMdl10,
                             "solidifiedObject"));
    // 两态完整登记：通过
    RobotDesign complete = makeValidExplicitDesign();
    ResourceRef recorded;
    recorded.resourceId = "mesh-a";
    recorded.state = ResourceState::Recorded;
    recorded.externalRecord = sdurws::ird::modeling::ExternalResourceRecord{
        "D:/assets/mesh-a.stl", sdurws::ird::core::Digest256{}};
    complete.resourceManifest.push_back(recorded);
    EXPECT_TRUE(checkInvariants(complete).empty());
}

/// I-MDL-11（传动合法）：ratio 有限>0；R2 下 C 方阵自洽且条件数 ≤1×10⁸。
TEST(MdlRobotDesign, IMdl11_DrivetrainRatios_WP13T03_ACC3)
{
    IRD_TEST_INFO("MDL-16", {}, std::nullopt);
    // ratio ≤0（无量纲）：违例
    DrivetrainDesign zeroRatio = makeValidDrivetrain();
    zeroRatio.ratioPerJoint[0] = provided(0.0);
    EXPECT_TRUE(hasViolation(checkInvariants(zeroRatio, CouplingStage::R1Locked),
                             InvariantId::IMdl11, "ratioPerJoint[0]"));
    // ratio 非有限：违例（NaN 不静默）
    DrivetrainDesign nanRatio = makeValidDrivetrain();
    nanRatio.ratioPerJoint[1] =
        provided(std::numeric_limits<double>::quiet_NaN());
    EXPECT_TRUE(hasViolation(checkInvariants(nanRatio, CouplingStage::R1Locked),
                             InvariantId::IMdl11, "ratioPerJoint[1]"));
    // R2 下条件数超 1×10⁸（P-RT-7 设计默认——P-MDL-7 登记的阈值）：违例
    DrivetrainDesign badCond = makeValidDrivetrain();
    CouplingDesign cp;
    cp.rows = 2;
    cp.cols = 2;
    cp.c = {1.0, 0.0, 0.0, 1.0};
    cp.jointRangeFirst = 0;
    cp.jointRangeLast = 1;
    cp.conditionNumber = 1e9;  // 无量纲——超 P-RT-7 上限
    badCond.coupling = cp;
    EXPECT_TRUE(hasViolation(checkInvariants(badCond, CouplingStage::R2Enabled),
                             InvariantId::IMdl11, "conditionNumber"));
    // R2 下非方阵：违例（行×列与元素数不一致）
    DrivetrainDesign notSquare = makeValidDrivetrain();
    CouplingDesign rect = cp;
    rect.conditionNumber = 2.0;
    rect.rows = 1;
    rect.c = {1.0, 0.0};
    notSquare.coupling = rect;
    EXPECT_TRUE(hasViolation(checkInvariants(notSquare, CouplingStage::R2Enabled),
                             InvariantId::IMdl11, "notSquare"));
}

/// I-MDL-12（R1 耦合阶段锁）：R1 下 coupling 不得配置（§4.7"存在即阻断"；
/// acceptance 3 非法组合第三项）。
TEST(MdlRobotDesign, IMdl12_CouplingR1Locked_WP13T03_ACC3)
{
    IRD_TEST_INFO("MDL-21", {}, std::nullopt);
    DrivetrainDesign withCoupling = makeValidDrivetrain();
    CouplingDesign cp;
    cp.rows = 2;
    cp.cols = 2;
    cp.c = {1.0, 0.5, 0.5, 1.0};
    cp.jointRangeFirst = 0;
    cp.jointRangeLast = 1;
    cp.conditionNumber = 3.0;
    withCoupling.coupling = cp;
    // R1：存在即违例（稳定码 MDL-21-COUPLING-STAGE-LOCKED 随 T18/R2 注册——
    // 值面违例先行，§9.5 不预建）
    EXPECT_TRUE(hasViolation(checkInvariants(withCoupling, CouplingStage::R1Locked),
                             InvariantId::IMdl12, "r1-locked"));
    // R2：同对象合法（阶段开关语义）
    EXPECT_TRUE(checkInvariants(withCoupling, CouplingStage::R2Enabled).empty());
}

// =====================================================================
// 双权威编辑权限守卫（§7.3 C-1/C-2；MDL-09 Axis 编辑权限随权威模式——
// acceptance 5 的 V-14 场景）
// =====================================================================

/// V-14：DH 权威模型编辑 axis → AuthorityViolation 拒绝＋"派生只读"提示；
/// 拒绝无副作用——工作集字节不变（以编码字节相等断言）。
TEST(MdlRobotDesign, V14_DhAuthorityAxisEditRejectedBytesUnchanged_WP13T03_ACC5)
{
    IRD_TEST_INFO("MDL-02", {}, std::nullopt);
    IRD_TEST_INFO("MDL-09", {}, std::nullopt);
    const RobotDesign dh = makeValidStandardDhDesign();
    const sdurws::ird::modeling::RobotDesignCodec codec;

    // 编辑前字节（canonical 基线）
    const auto before = codec.encode(sdurws::ird::modeling::ObjectVariant(dh),
                                     kCurrentFormatVersion);
    ASSERT_TRUE(before.ok());

    // 编辑 axis 的权限判定：拒绝＋AuthorityViolation＋派生只读提示
    const auto rejection =
        authorityEditGuard(AuthorityMode::StandardDH, AuthorityLockedField::Axis);
    ASSERT_TRUE(rejection.has_value());
    EXPECT_EQ(rejection->code, sdurws::ird::modeling::ModelingErrorCode::AuthorityViolation);
    EXPECT_NE(rejection->detail.find("派生只读"), std::string::npos);
    // params 可定位（field/authority——就地错误呈现的定位面）
    bool hasField = false;
    bool hasMode = false;
    for (const auto& kv : rejection->params) {
        hasField = hasField || (kv.first == "field" && kv.second == "axis");
        hasMode = hasMode || (kv.first == "authority" && kv.second == "StandardDH");
    }
    EXPECT_TRUE(hasField);
    EXPECT_TRUE(hasMode);

    // 拒绝后工作集字节不变：守卫是纯函数、无状态写入——再次编码同对象
    // 必得同字节（V-14"工作集字节不变"的机器判据）
    const auto after = codec.encode(sdurws::ird::modeling::ObjectVariant(dh),
                                    kCurrentFormatVersion);
    ASSERT_TRUE(after.ok());
    EXPECT_EQ(before.get(), after.get());
}

/// Axis/origin/dhDerived 的编辑权限随权威模式四象限（MDL-09——acceptance 5）。
TEST(MdlRobotDesign, Mdl09_AuthorityFieldPermissionMatrix_WP13T03_ACC5)
{
    IRD_TEST_INFO("MDL-09", {}, std::nullopt);
    // 显式权威：axis/origin 可编辑（权威一等字段）；dhDerived 拒绝（C-2）
    EXPECT_FALSE(authorityEditGuard(AuthorityMode::Explicit, AuthorityLockedField::Axis).has_value());
    EXPECT_FALSE(authorityEditGuard(AuthorityMode::Explicit, AuthorityLockedField::Origin).has_value());
    ASSERT_TRUE(authorityEditGuard(AuthorityMode::Explicit, AuthorityLockedField::DhDerived).has_value());
    EXPECT_EQ(authorityEditGuard(AuthorityMode::Explicit, AuthorityLockedField::DhDerived)->code,
              sdurws::ird::modeling::ModelingErrorCode::AuthorityViolation);
    // DH 权威：dhDerived 可编辑；axis/origin 拒绝（C-1——V-14 场景面）
    EXPECT_FALSE(authorityEditGuard(AuthorityMode::StandardDH, AuthorityLockedField::DhDerived).has_value());
    EXPECT_TRUE(authorityEditGuard(AuthorityMode::StandardDH, AuthorityLockedField::Axis).has_value());
    EXPECT_TRUE(authorityEditGuard(AuthorityMode::StandardDH, AuthorityLockedField::Origin).has_value());
}

/// schema 主版本单一权威：对象字段缺省值＝ObjectTypes.hpp 常量（禁写字面量
/// 的落地面——漂移即在此暴露）。
TEST(MdlRobotDesign, SchemaVersionDefaultsToRegisteredConstant_WP13T03_ACC1)
{
    IRD_TEST_INFO("CON-01", {}, std::nullopt);
    EXPECT_EQ(RobotDesign{}.schemaVersion, sdurws::ird::modeling::kRobotDesignSchemaVersion);
    EXPECT_EQ(sdurws::ird::modeling::kRobotDesignSchemaVersion, 1u);  // 首版＝1（§4.3"≥1"）
}
