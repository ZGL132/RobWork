/**
 * @file   CanonicalBridgeTest.cpp
 * @brief  CanonicalBridge 用例组（MdlCanonicalBridge）——契约
 *         tasks/foundation/WP-13-T12.json acceptance 逐条具名自证：
 *
 *   ACC1 字段映射表（卡 §9.1）逐行 UT——descriptionContractVersion/
 *        robotLocalName/joints（Explicit 直映＋DH 权威先经 §7.4 展开＋
 *        零位折叠）/links（body/shape 解引用 resourceManifest）/tools
 *        （经 toolRefs 解引用、defaultTcp 居首）/scene（经 sceneRefs 解
 *        引用、worldPose 世界系固连不预乘安装旋转）/base（编辑表示透传，
 *        不在此计算 R_world_base）/drivetrain/friction/resourceRefs
 *        （Recorded/Solidified 两态映射）；全 SI——单位 SI 化唯一经 core
 *        Units 换算入口（I-MDL-3/runtime §4.4 单位纪律 modeling 侧履行点）
 *   ACC2 确定性（ARC-03）：同闭包→同 Description（逐字段深比较）；reader
 *        纯函数（同字节→同输出；闭包字节变化→输出变化＝无缓存）；不重复
 *        计算任何内容身份（资源摘要透传不自算——声明摘要≠字节真实摘要时
 *        输出仍按声明值；ContentVersion/builtFrom/contentIdentity 归
 *        project/runtime——Description 类型层无承载字段）
 *   ACC3 reader 注入形态（P-RT-5/O-36 已裁决）：read(objectBytes,
 *        formatVersion) 单根字节输入＋部件对象经闭包域字节源解析；闭包
 *        复核范围＝真实存储对象、关节/连杆子 ObjectId 为模型内标识（O-36
 *        裁决口径——P-MDL-1 关闭面）
 *   ACC4 编译诊断定位（MDL-06/DTB 完成条件）：RefMissing（闭包缺对象/
 *        token 不匹配/必备输入缺失）、UnitIllegal（Invalid 态字段——无
 *        SI 真值可映射）、DhExpandFailed（DH 权威链展开前提不成立）、
 *        SchemaVersionUnsupported（版本门）→错误定位到对象 ObjectId/字段；
 *        不完整模型三层防线边界（值面拒绝——不假设 runtime 修复输入；
 *        runtime S3/S5 兜底不归本单元）；两态不产诊断记录（值面错误＋
 *        定位参数——产码唯一经 IDiagnosticFactory）
 *   ACC5 R-4 红线断言：reader 不做名称解析/前缀拼接/消歧（名称原样透传；
 *        重复拒绝不改名——消歧唯一归 runtime）；localName 输入质量面断言
 *        （合法字符集 [A-Za-z0-9_.-]、同作用域无重复——V-26 建模侧，
 *        AT-18 联合观测输入面）；资源状态声明口径与 runtime §9.1 一致
 *        （Recorded 声明态＋声明键、Solidified 声明态＋闭包复核）
 *
 * 设计依据：units/modeling.md §9.1/§9.2/§9.4.5/§7.4/§7.6、§14.3（O-36/
 * P-MDL-1 裁决登记）；runtime.md §4.2/§4.4/§5.2 S2/§9.1；core Units
 * （SA-12 唯一换算入口）；期望值独立抄写（Rodrigues/矩阵手算——测试不引
 * 实现常量）。共享夹具＝test/BridgeFixtures.hpp（测试域替身不入公共面）。
 *
 * 线程安全：全部用例单线程（纯函数服务语义）。
 */

#include "BridgeFixtures.hpp"

#include <sdurws/ird/io/IoFwd.hpp>                  // kAccessVersion——accessVersion 期望值来源（io 单点）
#include <sdurws/ird/modeling/CanonicalBridge.hpp>  // 被测面：builder＋reader＋闭包抽象
#include <sdurws/ird/modeling/Codec.hpp>
#include <sdurws/ird/modeling/ObjectTypes.hpp>
#include <sdurws/ird/modeling/Parts.hpp>
#include <sdurws/ird/modeling/RobotDesign.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace modeling = ::sdurws::ird::modeling;  // 被测类型所在命名空间别名
namespace core = ::sdurws::ird::core;           // core 侧类型（身份/来源/字段态）
namespace runtime = ::sdurws::ird::runtime;     // runtime 侧类型（Description/错误面）
namespace io = ::sdurws::ird::io;               // io 侧常量（kAccessVersion——读取契约版本）
namespace rwmath = rw::math;
using namespace modeling;  // 被测面（值模型/builder/reader）
using modeling::testbridge::BridgeClosure;
using modeling::testbridge::encode;
using modeling::testbridge::explicitPose;
using modeling::testbridge::kPi;
using modeling::testbridge::makeLink;
using modeling::testbridge::makeOid;
using modeling::testbridge::makeRecordedResource;
using modeling::testbridge::makeRevoluteJoint;
using modeling::testbridge::makeSolidifiedResource;
using modeling::testbridge::userProv;

namespace {

// =====================================================================
// 期望值辅助（测试内独立实现——期望值与被测面零共享代码路径）
// =====================================================================

/// Rodrigues 轴角旋转（独立实现；公式 R = I + sinθ·[k]× ＋ (1−cosθ)·[k]×²，
/// axis 须为单位向量、角 rad）。
rwmath::Rotation3D<double> expectedAxisAngle(const rwmath::Vector3D<double>& axis,
                                             double angle)
{
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    const double t = 1.0 - c;
    const double kx = axis[0];
    const double ky = axis[1];
    const double kz = axis[2];
    return rwmath::Rotation3D<double>(
        c + t * kx * kx,      t * kx * ky - s * kz, t * kx * kz + s * ky,
        t * kx * ky + s * kz, c + t * ky * ky,      t * ky * kz - s * kx,
        t * kx * kz - s * ky, t * ky * kz + s * kx, c + t * kz * kz);
}

/// R·R（逐元素——期望值合成用）。
rwmath::Rotation3D<double> expectedRotMul(const rwmath::Rotation3D<double>& a,
                                          const rwmath::Rotation3D<double>& b)
{
    rwmath::Rotation3D<double> out;
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            double s = 0.0;
            for (std::size_t k = 0; k < 3; ++k) { s += a(i, k) * b(k, j); }
            out(i, j) = s;
        }
    }
    return out;
}

// =====================================================================
// 装配与取回辅助
// =====================================================================

/// 将根对象装配进闭包（token 路由位——builder 的根定位入口）。
void putRoot(BridgeClosure& closure, const RobotDesign& design)
{
    closure.put(std::string(kRobotDesignObjectType), encode(design));
}

/// builder 构造＋闭包构建（两态直返——用例内断言 ok/err）。
runtime::Expected<runtime::RobotDesignDescription, ModelingError>
    buildFrom(BridgeClosure& closure)
{
    const CanonicalModelInputBuilder builder;
    std::vector<core::DiagnosticRecord> diags;
    return builder.build(closure, diags);
}

/// reader 构造＋根字节读取（返回 reader 侧两态；version 缺省 1）。
runtime::Expected<runtime::RobotDesignDescription, runtime::RuntimeError>
    readRoot(const BridgeClosure& closure, const RobotDesign& design,
             std::uint32_t version = 1)
{
    const RobotDesignReader reader(&closure);
    return reader.read(encode(design), version);
}

/// 根对象（1 关节 2 连杆——最小合法；用例按需注入缺陷面）。
RobotDesign minimalDesign()
{
    RobotDesign design;
    design.displayName = "bot";
    design.joints.push_back(makeRevoluteJoint(makeOid(), "J1", -1.0, 1.0));
    design.links.push_back(makeLink(makeOid(), "base"));
    design.links.push_back(makeLink(makeOid(), "link1"));
    return design;
}

// ---- Description 深比较（确定性断言的载体——runtime Description 无
// operator==；逐字段/逐元素比较，rw 值走元素访问器）--------------------

bool sameSourcedDouble(const core::SourcedValue<double>& a,
                       const core::SourcedValue<double>& b)
{
    if (a.state() != b.state()) { return false; }
    if (a.state() == core::FieldState::Provided) {
        return a.value() == b.value() && a.provenance() == b.provenance();
    }
    return true;  // 非 Provided 态无载荷可比（等价于全等）
}

bool sameVec(const rwmath::Vector3D<double>& a, const rwmath::Vector3D<double>& b)
{
    for (std::size_t i = 0; i < 3; ++i) {
        if (!(a[i] == b[i])) { return false; }
    }
    return true;
}

bool sameTransform(const rwmath::Transform3D<double>& a,
                   const rwmath::Transform3D<double>& b)
{
    if (!sameVec(a.P(), b.P())) { return false; }
    for (std::size_t r = 0; r < 3; ++r) {
        for (std::size_t c = 0; c < 3; ++c) {
            if (!(a.R()(r, c) == b.R()(r, c))) { return false; }
        }
    }
    return true;
}

bool sameSourcedVec(const core::SourcedValue<rwmath::Vector3D<double>>& a,
                    const core::SourcedValue<rwmath::Vector3D<double>>& b)
{
    if (a.state() != b.state()) { return false; }
    if (a.state() != core::FieldState::Provided) { return true; }
    return sameVec(a.value(), b.value()) && a.provenance() == b.provenance();
}

bool sameInertia(const rwmath::InertiaMatrix<double>& a,
                 const rwmath::InertiaMatrix<double>& b)
{
    for (std::size_t r = 0; r < 3; ++r) {
        for (std::size_t c = 0; c < 3; ++c) {
            if (!(a(r, c) == b(r, c))) { return false; }
        }
    }
    return true;
}

bool sameSourcedInertia(const core::SourcedValue<rwmath::InertiaMatrix<double>>& a,
                        const core::SourcedValue<rwmath::InertiaMatrix<double>>& b)
{
    if (a.state() != b.state()) { return false; }
    if (a.state() != core::FieldState::Provided) { return true; }
    return sameInertia(a.value(), b.value()) && a.provenance() == b.provenance();
}

bool sameResourceRef(const runtime::ResourceRef& a, const runtime::ResourceRef& b)
{
    return a.resourceId == b.resourceId && a.contentDigest == b.contentDigest
        && a.state == b.state && a.accessVersion == b.accessVersion
        && a.sourcePathHint.has_value() == b.sourcePathHint.has_value();
}

bool sameDescription(const runtime::RobotDesignDescription& a,
                     const runtime::RobotDesignDescription& b)
{
    if (a.descriptionContractVersion != b.descriptionContractVersion) { return false; }
    if (a.robotLocalName != b.robotLocalName) { return false; }
    if (a.joints.size() != b.joints.size() || a.links.size() != b.links.size()
        || a.tools.size() != b.tools.size() || a.scene.size() != b.scene.size()
        || a.friction.size() != b.friction.size()
        || a.resourceRefs.size() != b.resourceRefs.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.joints.size(); ++i) {
        const runtime::JointDescription& ja = a.joints[i];
        const runtime::JointDescription& jb = b.joints[i];
        if (ja.objectId != jb.objectId || ja.localName != jb.localName
            || ja.type != jb.type || !sameVec(ja.axis, jb.axis)
            || !sameTransform(ja.origin, jb.origin)
            || !sameSourcedDouble(ja.lower, jb.lower)
            || !sameSourcedDouble(ja.upper, jb.upper)
            || !sameSourcedDouble(ja.maxVelocity, jb.maxVelocity)
            || !sameSourcedDouble(ja.maxAcceleration, jb.maxAcceleration)) {
            return false;
        }
        if (ja.workingRange.has_value() != jb.workingRange.has_value()) { return false; }
        if (ja.workingRange.has_value()
            && !(ja.workingRange->lower == jb.workingRange->lower
                 && ja.workingRange->upper == jb.workingRange->upper)) {
            return false;
        }
    }
    for (std::size_t i = 0; i < a.links.size(); ++i) {
        const runtime::LinkDescription& la = a.links[i];
        const runtime::LinkDescription& lb = b.links[i];
        if (la.objectId != lb.objectId || la.localName != lb.localName
            || !sameSourcedDouble(la.mass, lb.mass)
            || !sameSourcedVec(la.centerOfMass, lb.centerOfMass)
            || !sameSourcedInertia(la.inertia, lb.inertia)
            || la.material.has_value() != lb.material.has_value()) {
            return false;
        }
        if (la.material.has_value() && *la.material != *lb.material) { return false; }
        if (la.visual.has_value() != lb.visual.has_value()) { return false; }
        if (la.visual.has_value() && !sameResourceRef(la.visual->resource,
                                                      lb.visual->resource)) {
            return false;
        }
        if (la.collision.has_value() != lb.collision.has_value()) { return false; }
        if (la.collision.has_value() && !sameResourceRef(la.collision->resource,
                                                         lb.collision->resource)) {
            return false;
        }
    }
    for (std::size_t i = 0; i < a.tools.size(); ++i) {
        const runtime::ToolDescription& ta = a.tools[i];
        const runtime::ToolDescription& tb = b.tools[i];
        if (ta.objectId != tb.objectId || ta.localName != tb.localName
            || !sameSourcedDouble(ta.mass, tb.mass)
            || !sameSourcedVec(ta.centerOfMass, tb.centerOfMass)
            || !sameSourcedInertia(ta.inertia, tb.inertia)
            || !sameTransform(ta.tcpOffset, tb.tcpOffset)) {
            return false;
        }
    }
    for (std::size_t i = 0; i < a.scene.size(); ++i) {
        const runtime::SceneObjectDescription& sa = a.scene[i];
        const runtime::SceneObjectDescription& sb = b.scene[i];
        if (sa.objectId != sb.objectId || sa.localName != sb.localName
            || !sameTransform(sa.worldPose, sb.worldPose)
            || !sameResourceRef(sa.geometry.resource, sb.geometry.resource)) {
            return false;
        }
    }
    if (a.base.preset != b.base.preset
        || !sameSourcedVec(a.base.customEaa, b.base.customEaa)
        || !sameVec(a.base.basePosition, b.base.basePosition)) {
        return false;
    }
    if (a.drivetrain.ratioPerJoint.size() != b.drivetrain.ratioPerJoint.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.drivetrain.ratioPerJoint.size(); ++i) {
        if (!sameSourcedDouble(a.drivetrain.ratioPerJoint[i],
                               b.drivetrain.ratioPerJoint[i])) {
            return false;
        }
    }
    for (std::size_t i = 0; i < a.friction.size(); ++i) {
        if (!sameSourcedDouble(a.friction[i].viscous, b.friction[i].viscous)
            || !sameSourcedDouble(a.friction[i].coulomb, b.friction[i].coulomb)
            || !sameSourcedDouble(a.friction[i].bias, b.friction[i].bias)) {
            return false;
        }
    }
    for (std::size_t i = 0; i < a.resourceRefs.size(); ++i) {
        if (!sameResourceRef(a.resourceRefs[i], b.resourceRefs[i])) { return false; }
    }
    return true;
}

}  // namespace

// =====================================================================
// ACC1：字段映射表逐行（卡 §9.1）
// =====================================================================

/**
 * @brief ACC1 行 1/2：descriptionContractVersion ← schemaVersion（同源，
 *        初值 1）＋robotLocalName ← displayName 透传（进 RuntimeNameMap
 *        设备作用域的名称源——MDL-14 输入面）。
 */
TEST(MdlCanonicalBridge, ContractVersionAndRobotNamePassthrough_WP13T12_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-03", "MDL-14"},
                  std::vector<std::string>{});

    RobotDesign design;  // 缺省＝schemaVersion 1（单一权威常量）
    design.displayName = "IRB6700";
    design.joints.push_back(makeRevoluteJoint(makeOid(), "J1"));
    design.links.push_back(makeLink(makeOid(), "base"));
    design.links.push_back(makeLink(makeOid(), "link1"));

    BridgeClosure closure;
    putRoot(closure, design);
    auto built = buildFrom(closure);
    ASSERT_TRUE(built.ok()) << built.error().detail;
    const runtime::RobotDesignDescription& desc = built.get();

    // 契约版本与建模 schemaVersion 同源（R-MDL-3 单点演进；初值 1）。
    EXPECT_EQ(desc.descriptionContractVersion, design.schemaVersion);
    EXPECT_EQ(desc.descriptionContractVersion, 1u);
    // 机器人局部名＝根对象模型名（透传——不改写/不加前缀，R-4）。
    EXPECT_EQ(desc.robotLocalName, "IRB6700");
}

/**
 * @brief ACC1 行 3（Explicit 直映）：axis/origin/type/身份/名称透传＋零位
 *        折叠（origin_desc = origin·R(axis, q0)——期望值测试内独立 Rodrigues
 *        合成）＋限位平移 −q0（权威 q→RobWork q）＋速度/加速度 NotProvided
 *        （建模 schema 无落点——不发明字段）。
 */
TEST(MdlCanonicalBridge, JointsExplicitMappingWithZeroOffsetFold_WP13T12_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-06", "ARC-03"},
                  std::vector<std::string>{});

    const double q0 = 0.5;  // 权威零位偏置，rad（非零——折叠面可见）
    const core::ObjectId jid = makeOid();
    RobotDesign design;
    design.displayName = "bot";
    // bounds (−2.0, 2.0) rad＋零位 0.5 rad——折叠后 (−2.5, 1.5) rad。
    design.joints.push_back(makeRevoluteJoint(jid, "J1", -2.0, 2.0, q0));
    design.links.push_back(makeLink(makeOid(), "base"));
    design.links.push_back(makeLink(makeOid(), "link1"));

    BridgeClosure closure;
    putRoot(closure, design);
    auto built = buildFrom(closure);
    ASSERT_TRUE(built.ok()) << built.error().detail;
    ASSERT_EQ(built.get().joints.size(), 1u);
    const runtime::JointDescription& jd = built.get().joints.front();

    // 身份/名称/类型透传（不改写——R-4；objectId 为模型内标识 O-36）。
    EXPECT_TRUE(jd.objectId == jid);
    EXPECT_EQ(jd.localName, "J1");
    EXPECT_EQ(jd.type, runtime::JointType::Revolute);
    // 轴透传（单位向量——权威一等字段 MDL-09）。
    EXPECT_TRUE(sameVec(jd.axis, rwmath::Vector3D<double>(0.0, 0.0, 1.0)));
    // 零位折叠：origin_desc = origin·R(axis, q0)——期望值独立合成（恒等
    // 平移不变；旋转＝绕 z 轴 q0）。
    const rwmath::Rotation3D<double> expectR = expectedAxisAngle(
        rwmath::Vector3D<double>(0.0, 0.0, 1.0), q0);
    EXPECT_TRUE(sameVec(jd.origin.P(), rwmath::Vector3D<double>(0.0, 0.0, 0.0)));
    for (std::size_t r = 0; r < 3; ++r) {
        for (std::size_t c = 0; c < 3; ++c) {
            EXPECT_NEAR(jd.origin.R()(r, c), expectR(r, c), 1e-15)
                << "(" << r << "," << c << ")";
        }
    }
    // 限位平移（SI 真值门恒等通过——rad；权威 q−q0＝RobWork q）。
    ASSERT_EQ(jd.lower.state(), core::FieldState::Provided);
    ASSERT_EQ(jd.upper.state(), core::FieldState::Provided);
    EXPECT_DOUBLE_EQ(jd.lower.value(), -2.0 - q0);
    EXPECT_DOUBLE_EQ(jd.upper.value(), 2.0 - q0);
    // 速度/加速度：schema 无落点→NotProvided（能力缺失降级非失败——§5.6）。
    EXPECT_EQ(jd.maxVelocity.state(), core::FieldState::NotProvided);
    EXPECT_EQ(jd.maxAcceleration.state(), core::FieldState::NotProvided);
    EXPECT_FALSE(jd.workingRange.has_value());
}

/**
 * @brief ACC1 行 3（DH 权威先经 §7.4 展开）：单关节标准 DH 链（θ_offset/
 *        d/a/α 手算期望——T_{i-1,i} = Rot_z(θ+q0)·Trans_z(d)·Trans_x(a)·
 *        Rot_x(α)），展开产物经同一零位折叠纪律映射（T09 等价验证同规）。
 */
TEST(MdlCanonicalBridge, JointsDhAuthorityExpandsToExplicit_WP13T12_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-02", "MDL-10"},
                  std::vector<std::string>{});

    const double theta = 0.3;   // θ_offset，rad（DH 几何参数——含偏置前）
    const double q0 = 0.2;      // zeroOffset，rad（权威零位——显式分离）
    const double dM = 0.15;     // 连杆偏距，m
    const double aM = 0.4;      // 连杆长度，m
    const double alpha = 0.25;  // 扭转角，rad

    const core::ObjectId jid = makeOid();
    RobotDesign design;
    design.displayName = "bot";
    JointEntry joint = makeRevoluteJoint(jid, "J1", -1.0, 1.0, q0);
    joint.dhDerived = DhParameters{theta, dM, aM, alpha};
    design.authority = AuthorityMode::StandardDH;
    design.joints.push_back(std::move(joint));
    design.links.push_back(makeLink(makeOid(), "base"));
    design.links.push_back(makeLink(makeOid(), "link1"));

    BridgeClosure closure;
    putRoot(closure, design);
    auto built = buildFrom(closure);
    ASSERT_TRUE(built.ok()) << built.error().detail;
    ASSERT_EQ(built.get().joints.size(), 1u);
    const runtime::JointDescription& jd = built.get().joints.front();

    // 手算期望（§7.4 原文公式逐步）：相对变换 T = Rot_z(θ+q0)·Trans_z(d)·
    // Trans_x(a)·Rot_x(α)——平移＝Rz(θ+q0)·(a,0,d)；旋转＝Rz(θ+q0)·Rx(α)。
    // 零位折叠后：origin_desc = T·R(axis=展开轴, q0)（同规折叠）。
    const double thetaTotal = theta + q0;
    const rwmath::Rotation3D<double> rz(std::cos(thetaTotal), -std::sin(thetaTotal), 0.0,
                                        std::sin(thetaTotal), std::cos(thetaTotal), 0.0,
                                        0.0, 0.0, 1.0);
    const rwmath::Rotation3D<double> rx(1.0, 0.0, 0.0,
                                        0.0, std::cos(alpha), -std::sin(alpha),
                                        0.0, std::sin(alpha), std::cos(alpha));
    // Rot_z·(a,0,d)：x'＝a·cosθ总、y'＝a·sinθ总、z'＝d。
    const rwmath::Vector3D<double> expectP(aM * std::cos(thetaTotal),
                                           aM * std::sin(thetaTotal), dM);
    const rwmath::Rotation3D<double> t = expectedRotMul(rz, rx);
    // 展开轴＝T_{0,i}·(0,0,1)＝旋转阵第三列（单关节：T_base_joint1＝T）。
    const rwmath::Vector3D<double> expectAxis(t(0, 2), t(1, 2), t(2, 2));
    const rwmath::Rotation3D<double> folded =
        expectedRotMul(t, expectedAxisAngle(expectAxis, q0));

    // 轴＝归一化 T_{0,i}·(0,0,1)（§7.4 z_i 原文）——两侧浮点求值序不同，
    // 以 1e-12 近等比较（浮点累乘的容差纪律，T09 同款）。
    for (std::size_t k = 0; k < 3; ++k) {
        EXPECT_NEAR(jd.axis[k], expectAxis[k], 1e-12) << "axis[" << k << "]";
    }
    for (std::size_t k = 0; k < 3; ++k) {
        EXPECT_NEAR(jd.origin.P()[k], expectP[k], 1e-12) << "P[" << k << "]";
    }
    for (std::size_t r = 0; r < 3; ++r) {
        for (std::size_t c = 0; c < 3; ++c) {
            EXPECT_NEAR(jd.origin.R()(r, c), folded(r, c), 1e-12)
                << "(" << r << "," << c << ")";
        }
    }
    // 限位透传＋折叠（−q0；DH 态 bounds 为两态均权威字段——§7.2）。
    ASSERT_EQ(jd.lower.state(), core::FieldState::Provided);
    EXPECT_DOUBLE_EQ(jd.lower.value(), -1.0 - q0);
    EXPECT_DOUBLE_EQ(jd.upper.value(), 1.0 - q0);
}

/**
 * @brief ACC1 行 4：links 行——物性三元（kg/m/kg·m²，质心系 M-2）＋材料＋
 *        visual 几何解引用 resourceManifest（六分量→3×3 对称展开手算对照）。
 */
TEST(MdlCanonicalBridge, LinksMappingBodyAndGeometry_WP13T12_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-05", "MDL-16"},
                  std::vector<std::string>{});

    const core::Digest256 meshDigest{{1}};  // 非零摘要（测试值——内容寻址面）
    const core::ObjectId lid = makeOid();
    RobotDesign design;
    design.displayName = "bot";
    design.joints.push_back(makeRevoluteJoint(makeOid(), "J1"));
    design.links.push_back(makeLink(makeOid(), "base"));
    LinkEntry link = makeLink(lid, "link1");
    link.body.mass = core::SourcedValue<double>::provided(4.5, userProv());  // kg
    link.body.centerOfMass =
        core::SourcedValue<rwmath::Vector3D<double>>::provided(
            rwmath::Vector3D<double>(0.001, 0.0, 0.02), userProv());  // m，连杆系
    // 六分量惯量（kg·m²，质心系——含非对角项验证对称展开；主矩
    // 0.02/0.03/0.04 满足三角不等式——解码门 I-MDL-5 合法实例）。
    link.body.inertia = core::SourcedValue<InertiaTensor>::provided(
        InertiaTensor{0.02, 0.03, 0.04, 0.001, 0.002, 0.003}, userProv());
    link.body.material = MaterialRef{"aluminum", {}};
    GeometryRef visual;
    visual.resourceRefId = "mesh/link1";
    visual.kind = GeometryKind::Mesh;
    link.visual = visual;
    design.links.push_back(std::move(link));
    design.resourceManifest.push_back(makeRecordedResource("mesh/link1", meshDigest));

    BridgeClosure closure;
    putRoot(closure, design);
    auto built = buildFrom(closure);
    ASSERT_TRUE(built.ok()) << built.error().detail;
    ASSERT_EQ(built.get().links.size(), 2u);
    const runtime::LinkDescription& ld = built.get().links[1];

    EXPECT_EQ(ld.localName, "link1");
    ASSERT_EQ(ld.mass.state(), core::FieldState::Provided);
    EXPECT_DOUBLE_EQ(ld.mass.value(), 4.5);  // kg（SI 恒等通过）
    ASSERT_EQ(ld.centerOfMass.state(), core::FieldState::Provided);
    EXPECT_TRUE(sameVec(ld.centerOfMass.value(),
                        rwmath::Vector3D<double>(0.001, 0.0, 0.02)));
    // 六分量→3×3 对称展开（行主序；ixy/ixz/iyz 落对称位）。
    ASSERT_EQ(ld.inertia.state(), core::FieldState::Provided);
    EXPECT_DOUBLE_EQ(ld.inertia.value()(0, 0), 0.02);
    EXPECT_DOUBLE_EQ(ld.inertia.value()(1, 1), 0.03);
    EXPECT_DOUBLE_EQ(ld.inertia.value()(2, 2), 0.04);
    EXPECT_DOUBLE_EQ(ld.inertia.value()(0, 1), 0.001);
    EXPECT_DOUBLE_EQ(ld.inertia.value()(1, 0), 0.001);
    EXPECT_DOUBLE_EQ(ld.inertia.value()(0, 2), 0.002);
    EXPECT_DOUBLE_EQ(ld.inertia.value()(2, 0), 0.002);
    EXPECT_DOUBLE_EQ(ld.inertia.value()(1, 2), 0.003);
    EXPECT_DOUBLE_EQ(ld.inertia.value()(2, 1), 0.003);
    // 材料标识透传（估算来源用——显示/追溯属性）。
    ASSERT_TRUE(ld.material.has_value());
    EXPECT_EQ(*ld.material, "aluminum");
    // visual 几何：清单解引用→内容寻址 ResourceRef（摘要透传——不自算）。
    ASSERT_TRUE(ld.visual.has_value());
    EXPECT_TRUE(ld.visual->resource.contentDigest == meshDigest);
    EXPECT_EQ(ld.visual->resource.state, runtime::ResourceState::Recorded);
    // 路径不入 Description＝不入身份（§9.1"路径不入身份"桥接侧履行）。
    EXPECT_FALSE(ld.visual->resource.sourcePathHint.has_value());
    // accessVersion 取 io 单点常量（P-RT-6 裁决值——不私写字面量）。
    EXPECT_EQ(ld.visual->resource.accessVersion, io::kAccessVersion);
    // 缺省碰撞几何→optional 空透传。
    EXPECT_FALSE(ld.collision.has_value());
}

/**
 * @brief ACC1 行 5：tools 行——经 toolRefs 解引用；defaultTcp 引用工具居首
 *        （runtime"首项为默认 TCP"）；tcpOffset ＝ mountInterface ∘ 所选
 *        TCP（默认工具按 tcpKey，其余取首条）；CM-0 复核范围＝真实存储
 *        工具对象（经闭包）。
 */
TEST(MdlCanonicalBridge, ToolsMappingDefaultTcpFirstAndTcpCompose_WP13T12_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-13", "KIN-14"},
                  std::vector<std::string>{});

    const core::ObjectId toolA = makeOid();
    const core::ObjectId toolB = makeOid();
    RobotDesign design;
    design.displayName = "bot";
    design.joints.push_back(makeRevoluteJoint(makeOid(), "J1"));
    design.links.push_back(makeLink(makeOid(), "base"));
    design.links.push_back(makeLink(makeOid(), "link1"));
    design.toolRefs = {toolA, toolB};
    design.defaultTcp = TcpRef{toolB, "tip2"};  // 默认指向第二工具——居首验证

    // 工具 A（两 TCP——非默认工具取首条）。
    ToolDefinition a;
    a.objectId = toolA;
    a.localName = "gripperA";
    a.tcpList.push_back(TcpEntry{"tipA1", explicitPose(0.0, 0.0, 0.05, 0.0), ""});
    a.tcpList.push_back(TcpEntry{"tipA2", explicitPose(0.0, 0.0, 0.06, 0.0), ""});
    // 工具 B（默认工具——tcpKey "tip2" 指向第二条）。
    ToolDefinition b;
    b.objectId = toolB;
    b.localName = "gripperB";
    b.tcpList.push_back(TcpEntry{"tip2a", explicitPose(0.0, 0.0, 0.10, 0.0), ""});
    b.tcpList.push_back(TcpEntry{"tip2", explicitPose(0.01, 0.02, 0.12, kPi / 2), ""});

    BridgeClosure closure;
    putRoot(closure, design);
    closure.put(toolA, std::string(kToolDefinitionObjectType), encode(a));
    closure.put(toolB, std::string(kToolDefinitionObjectType), encode(b));

    auto built = buildFrom(closure);
    ASSERT_TRUE(built.ok()) << built.error().detail;
    ASSERT_EQ(built.get().tools.size(), 2u);
    // 次序：默认工具居首。
    EXPECT_TRUE(built.get().tools[0].objectId == toolB);
    EXPECT_TRUE(built.get().tools[1].objectId == toolA);
    // tcpOffset 合成手算（默认工具 B 的 "tip2"：T_flange_tcp = T_f_t∘T_t_tcp；
    // mountInterface 恒等→平移＝tcp offset 平移、旋转＝绕 z π/2）。
    const runtime::ToolDescription& first = built.get().tools[0];
    EXPECT_TRUE(sameVec(first.tcpOffset.P(),
                        rwmath::Vector3D<double>(0.01, 0.02, 0.12)));
    EXPECT_NEAR(first.tcpOffset.R()(0, 0), std::cos(kPi / 2), 1e-15);
    EXPECT_NEAR(first.tcpOffset.R()(1, 0), std::sin(kPi / 2), 1e-15);
    // 非默认工具 A 取首条 "tipA1"。
    const runtime::ToolDescription& second = built.get().tools[1];
    EXPECT_TRUE(sameVec(second.tcpOffset.P(),
                        rwmath::Vector3D<double>(0.0, 0.0, 0.05)));
}

/**
 * @brief ACC1 行 6＋M-11：scene 行——经 sceneRefs 解引用；worldPose 世界系
 *        固连**不预乘安装旋转**（基座预设倒挂时场景位姿原样透传——§6.4
 *        禁止项 3 的建模侧履行；矩阵归 runtime 编译产物）。
 */
TEST(MdlCanonicalBridge, SceneWorldPoseNotPreMultipliedByInstallRotation_WP13T12_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-15", "MDL-22"},
                  std::vector<std::string>{});

    const core::Digest256 tableDigest{{2}};
    const core::ObjectId sid = makeOid();
    RobotDesign design;
    design.displayName = "bot";
    design.basePlacement.preset = runtime::InstallationPresetToken::Inverted;  // 倒挂
    design.joints.push_back(makeRevoluteJoint(makeOid(), "J1"));
    design.links.push_back(makeLink(makeOid(), "base"));
    design.links.push_back(makeLink(makeOid(), "link1"));
    design.sceneRefs = {sid};
    SceneObject table;
    table.objectId = sid;
    table.localName = "table";
    // 非恒等世界系位姿（平移＋绕 z 旋转——若被预乘安装旋转即刻可见）。
    table.worldPose = explicitPose(1.0, 2.0, 0.0, 0.7);
    table.geometry = GeometryRef{"scene/table", testbridge::identityTransform3D(), GeometryKind::Mesh};
    design.resourceManifest.push_back(makeRecordedResource("scene/table", tableDigest));

    BridgeClosure closure;
    putRoot(closure, design);
    closure.put(sid, std::string(kSceneObjectObjectType), encode(table));

    auto built = buildFrom(closure);
    ASSERT_TRUE(built.ok()) << built.error().detail;
    ASSERT_EQ(built.get().scene.size(), 1u);
    const runtime::SceneObjectDescription& sd = built.get().scene.front();
    EXPECT_EQ(sd.localName, "table");
    // 世界系固连：位姿与建模对象逐元素一致（不预乘 R_x(π)——M-11）。
    EXPECT_TRUE(sameVec(sd.worldPose.P(), rwmath::Vector3D<double>(1.0, 2.0, 0.0)));
    EXPECT_NEAR(sd.worldPose.R()(0, 0), std::cos(0.7), 1e-15);
    EXPECT_NEAR(sd.worldPose.R()(1, 0), std::sin(0.7), 1e-15);
    EXPECT_NEAR(sd.worldPose.R()(1, 1), std::cos(0.7), 1e-15);
    // 场景几何经清单解引用（Recorded 声明面）。
    EXPECT_EQ(sd.geometry.resource.state, runtime::ResourceState::Recorded);
    EXPECT_TRUE(sd.geometry.resource.contentDigest == tableDigest);
}

/**
 * @brief ACC1 行 7（M-11/P-RT-4）：base 行——preset/customEaa/basePosition
 *        编辑表示透传；Description.base 无任何旋转矩阵承载（类型层证据：
 *        modeling 不计算/缓存 R_world_base——runtime 唯一计算）。
 */
TEST(MdlCanonicalBridge, BaseMappingCarriesEditorRepresentationOnly_WP13T12_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-22"},
                  std::vector<std::string>{"AT-37"});

    RobotDesign design;
    design.displayName = "bot";
    design.basePlacement.preset = runtime::InstallationPresetToken::Custom;
    design.basePlacement.customEaa =
        core::SourcedValue<rwmath::Vector3D<double>>::provided(
            rwmath::Vector3D<double>(0.0, kPi / 2, 0.0), userProv());  // rad
    design.basePlacement.basePosition =
        core::SourcedValue<rwmath::Vector3D<double>>::provided(
            rwmath::Vector3D<double>(0.5, 0.0, 1.2), userProv());  // m，世界系
    design.joints.push_back(makeRevoluteJoint(makeOid(), "J1"));
    design.links.push_back(makeLink(makeOid(), "base"));
    design.links.push_back(makeLink(makeOid(), "link1"));

    BridgeClosure closure;
    putRoot(closure, design);
    auto built = buildFrom(closure);
    ASSERT_TRUE(built.ok()) << built.error().detail;
    const runtime::BasePlacementDescription& base = built.get().base;
    // 预设/编辑表示透传（同一 runtime 枚举实体——词表一致性类型级钉住）。
    static_assert(std::is_same<decltype(design.basePlacement.preset),
                               decltype(base.preset)>::value,
                  "预设词表为 runtime 单一权威枚举（P-RT-4）");
    EXPECT_EQ(base.preset, runtime::InstallationPresetToken::Custom);
    ASSERT_EQ(base.customEaa.state(), core::FieldState::Provided);
    EXPECT_TRUE(sameVec(base.customEaa.value(),
                        rwmath::Vector3D<double>(0.0, kPi / 2, 0.0)));
    EXPECT_TRUE(sameVec(base.basePosition, rwmath::Vector3D<double>(0.5, 0.0, 1.2)));
    // 不在此计算 R_world_base（M-11）：BasePlacementDescription 结构仅承载
    // 编辑表示（preset/customEaa/basePosition）——旋转矩阵只存在于 runtime
    // 编译产物（BaseWorldTransform），Description 类型层即无承载位。
}

/**
 * @brief ACC1 行 8/9：drivetrain 行（ratioPerJoint 透传＋无量纲 SI 门）＋
 *        friction 行（逐关节摩擦透传；缺失走 DataInsufficient 降级——
 *        判定归 dynamics，DYN-06）。
 */
TEST(MdlCanonicalBridge, DrivetrainAndFrictionMapping_WP13T12_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-16", "MDL-21"},
                  std::vector<std::string>{});

    const core::ObjectId did = makeOid();
    RobotDesign design;
    design.displayName = "bot";
    design.joints.push_back(makeRevoluteJoint(makeOid(), "J1"));
    design.joints.push_back(makeRevoluteJoint(makeOid(), "J2"));
    design.links.push_back(makeLink(makeOid(), "base"));
    design.links.push_back(makeLink(makeOid(), "link1"));
    design.links.push_back(makeLink(makeOid(), "link2"));
    design.drivetrainRef = did;

    DrivetrainDesign dt;
    dt.objectId = did;
    dt.ratioPerJoint.push_back(
        core::SourcedValue<double>::provided(101.0, userProv()));  // 无量纲
    dt.ratioPerJoint.push_back(
        core::SourcedValue<double>::provided(81.0, userProv()));
    FrictionEntry friction;
    friction.viscous = core::SourcedValue<double>::provided(0.8, userProv());   // N·m·s/rad
    friction.coulomb = core::SourcedValue<double>::provided(1.5, userProv());   // N·m
    friction.bias = core::SourcedValue<double>::notProvided();                  // 缺失→降级
    dt.frictionPerJoint.push_back(friction);
    dt.frictionPerJoint.push_back(FrictionEntry{});  // 全缺省（全 NotProvided）

    BridgeClosure closure;
    putRoot(closure, design);
    closure.put(did, std::string(kRobotDrivetrainObjectType), encode(dt));
    auto built = buildFrom(closure);
    ASSERT_TRUE(built.ok()) << built.error().detail;

    // 传动比逐关节透传（R1 无耦合——解码门 I-MDL-12 锁定）。
    ASSERT_EQ(built.get().drivetrain.ratioPerJoint.size(), 2u);
    ASSERT_EQ(built.get().drivetrain.ratioPerJoint[0].state(), core::FieldState::Provided);
    EXPECT_DOUBLE_EQ(built.get().drivetrain.ratioPerJoint[0].value(), 101.0);
    EXPECT_FALSE(built.get().drivetrain.coupling.has_value());
    // 摩擦逐关节透传＋NotProvided 保真（降级语义不失真——DYN-06）。
    ASSERT_EQ(built.get().friction.size(), 2u);
    ASSERT_EQ(built.get().friction[0].viscous.state(), core::FieldState::Provided);
    EXPECT_DOUBLE_EQ(built.get().friction[0].viscous.value(), 0.8);
    EXPECT_EQ(built.get().friction[0].bias.state(), core::FieldState::NotProvided);
    EXPECT_EQ(built.get().friction[1].viscous.state(), core::FieldState::NotProvided);
}

/**
 * @brief ACC1 行 10＋ACC5 资源声明口径：resourceRefs 全量映射——Recorded→
 *        确定性声明键（同输入同键；非对象不复核闭包）；Solidified→固化
 *        ObjectId＋闭包存在性复核；声明口径与 runtime §9.1 一致（Recorded
 *        重读＋digest 复核、Solidified 免复查的声明面）。
 */
TEST(MdlCanonicalBridge, ResourceRefsBothStatesMapping_WP13T12_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"CON-03", "CON-05"},
                  std::vector<std::string>{});

    const core::Digest256 recordedDigest{{3}};
    const core::Digest256 solidDigest{{4}};
    const core::ObjectId solidOid = makeOid();
    RobotDesign design;
    design.displayName = "bot";
    design.joints.push_back(makeRevoluteJoint(makeOid(), "J1"));
    design.links.push_back(makeLink(makeOid(), "base"));
    design.links.push_back(makeLink(makeOid(), "link1"));
    design.resourceManifest.push_back(
        makeRecordedResource("mesh/recorded", recordedDigest));
    design.resourceManifest.push_back(
        makeSolidifiedResource("mesh/solid", solidDigest, solidOid));

    BridgeClosure closure;
    putRoot(closure, design);
    // 固化资源对象在闭包（token "resource-object" 归 project/io 词表——
    // builder 只核存在性不核 token，O-36 真实存储对象口径）。
    closure.put(solidOid, std::string("resource-object"),
                std::vector<std::uint8_t>{0x00});

    auto built = buildFrom(closure);
    ASSERT_TRUE(built.ok()) << built.error().detail;
    ASSERT_EQ(built.get().resourceRefs.size(), 2u);

    // Recorded 条目：声明键确定性（同清单条目同键——重复构建一致）＋摘要
    // 透传；非对象——闭包无登记仍成功（不复核闭包，O-36）。
    const runtime::ResourceRef& recorded = built.get().resourceRefs[0];
    EXPECT_EQ(recorded.state, runtime::ResourceState::Recorded);
    EXPECT_TRUE(recorded.contentDigest == recordedDigest);
    EXPECT_FALSE(recorded.sourcePathHint.has_value());  // 路径不入 Description
    auto second = buildFrom(closure);
    ASSERT_TRUE(second.ok());
    EXPECT_TRUE(recorded.resourceId == second.get().resourceRefs[0].resourceId)
        << "Recorded 声明键确定性（同清单条目同键——NFR-COR-02）";

    // Solidified 条目：固化 ObjectId 透传＋闭包复核通过＋声明口径。
    const runtime::ResourceRef& solid = built.get().resourceRefs[1];
    EXPECT_EQ(solid.state, runtime::ResourceState::Solidified);
    EXPECT_TRUE(solid.resourceId == solidOid);
    EXPECT_TRUE(solid.contentDigest == solidDigest);
    EXPECT_EQ(solid.accessVersion, io::kAccessVersion);
}

// =====================================================================
// ACC2：确定性（ARC-03）
// =====================================================================

/**
 * @brief ACC2：同闭包→同 Description（全新编码两次装配——逐字段深比较）；
 *        reader 纯函数：同字节同输出，且重复 read 重新经闭包取数（无缓存
 *        ——每次按当前闭包域重算）；闭包域字节变化→输出变化（同源绑定的
 *        当前性）。
 */
TEST(MdlCanonicalBridge, SameClosureSameDescriptionDeterministic_WP13T12_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-03"},
                  std::vector<std::string>{});

    const core::ObjectId toolOid = makeOid();
    RobotDesign design;
    design.displayName = "bot";
    design.joints.push_back(makeRevoluteJoint(makeOid(), "J1"));
    design.links.push_back(makeLink(makeOid(), "base"));
    design.links.push_back(makeLink(makeOid(), "link1"));
    design.toolRefs = {toolOid};
    design.defaultTcp = TcpRef{toolOid, "tip"};

    ToolDefinition tool;
    tool.objectId = toolOid;
    tool.localName = "tool";
    tool.tcpList.push_back(TcpEntry{"tip", explicitPose(0.0, 0.0, 0.1, 0.0), ""});

    // 第一次构建（独立编码装配）。
    BridgeClosure closureA;
    putRoot(closureA, design);
    closureA.put(toolOid, std::string(kToolDefinitionObjectType), encode(tool));
    auto first = buildFrom(closureA);
    ASSERT_TRUE(first.ok()) << first.error().detail;

    // 第二次构建（全新闭包对象＋全新编码字节——同语义输入）。
    BridgeClosure closureB;
    putRoot(closureB, design);
    closureB.put(toolOid, std::string(kToolDefinitionObjectType), encode(tool));
    auto second = buildFrom(closureB);
    ASSERT_TRUE(second.ok()) << second.error().detail;

    EXPECT_TRUE(sameDescription(first.get(), second.get()))
        << "同闭包→同 Description（ARC-03 建模侧确定性——逐字段一致）";

    // reader 面同字节同输出（纯函数；闭包取回计数持续增长＝无缓存）。
    BridgeClosure closureC;
    putRoot(closureC, design);
    closureC.put(toolOid, std::string(kToolDefinitionObjectType), encode(tool));
    auto readA = readRoot(closureC, design);
    ASSERT_TRUE(readA.ok()) << readA.error().what();
    const int callsAfterFirst = closureC.tryObjectCalls;
    auto readB = readRoot(closureC, design);
    ASSERT_TRUE(readB.ok());
    EXPECT_TRUE(sameDescription(readA.get(), readB.get()));
    EXPECT_GT(closureC.tryObjectCalls, callsAfterFirst)
        << "重复 read 重新经闭包取数（无结果缓存——每次编译按当前闭包域重算）";

    // 闭包域字节变化→输出变化（工具改名——输出随之更新，不复用旧值）。
    tool.localName = "toolRenamed";
    BridgeClosure closureD;
    putRoot(closureD, design);
    closureD.put(toolOid, std::string(kToolDefinitionObjectType), encode(tool));
    auto third = buildFrom(closureD);
    ASSERT_TRUE(third.ok());
    EXPECT_FALSE(sameDescription(first.get(), third.get()))
        << "闭包字节变化→Description 变化（同源绑定的当前性）";
    EXPECT_EQ(third.get().tools.front().localName, "toolRenamed");
}

/**
 * @brief ACC2：不重复计算任何内容身份——资源摘要透传不自算：清单声明
 *        摘要≠资源字节真实摘要时，输出仍按**声明值**（建模侧若自算 SHA-256
 *        即刻可见差异）；ContentVersion/builtFrom/contentIdentity 不在
 *        Description 承载面（类型层无字段——归 project/runtime）。
 */
TEST(MdlCanonicalBridge, NoContentIdentityRecompute_WP13T12_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"CON-05", "ARC-04"},
                  std::vector<std::string>{});

    const core::Digest256 declaredDigest{{0xAB}};  // 声明摘要（非字节真实摘要）
    RobotDesign design;
    design.displayName = "bot";
    design.joints.push_back(makeRevoluteJoint(makeOid(), "J1"));
    design.links.push_back(makeLink(makeOid(), "base"));
    design.links.push_back(makeLink(makeOid(), "link1"));
    design.resourceManifest.push_back(
        makeRecordedResource("mesh/declared", declaredDigest));

    BridgeClosure closure;
    putRoot(closure, design);
    auto built = buildFrom(closure);
    ASSERT_TRUE(built.ok()) << built.error().detail;
    ASSERT_EQ(built.get().resourceRefs.size(), 1u);
    // 输出摘要＝清单声明值（透传）——若 builder 对任何字节自算摘要，此断
    // 言即失败（声明值与真实字节摘要无理由一致）。
    EXPECT_TRUE(built.get().resourceRefs.front().contentDigest == declaredDigest)
        << "资源摘要按声明值透传（CON-05：身份计算归 project/runtime——"
           "modeling 不重复计算任何内容身份）";
}

// =====================================================================
// ACC3：reader 注入形态（P-RT-5/O-36）
// =====================================================================

/**
 * @brief ACC3：read(objectBytes, formatVersion) 单根字节输入＋部件对象经
 *        构造期绑定的闭包域字节源解析（§9.2 注入形态）；闭包 token 位刻意
 *        不装配根对象——read() 的根来自入参字节（runtime S2 已按 token
 *        路由），部件来自闭包（按 id）。
 */
TEST(MdlCanonicalBridge, ReaderSingleRootInputAndClosureBinding_WP13T12_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-14"},
                  std::vector<std::string>{});

    const core::ObjectId toolOid = makeOid();
    RobotDesign design;
    design.displayName = "bot";
    design.joints.push_back(makeRevoluteJoint(makeOid(), "J1"));
    design.links.push_back(makeLink(makeOid(), "base"));
    design.links.push_back(makeLink(makeOid(), "link1"));
    design.toolRefs = {toolOid};
    design.defaultTcp = TcpRef{toolOid, "tip"};

    ToolDefinition tool;
    tool.objectId = toolOid;
    tool.localName = "tool";
    tool.tcpList.push_back(TcpEntry{"tip", explicitPose(0.0, 0.0, 0.1, 0.0), ""});

    BridgeClosure closure;
    closure.put(toolOid, std::string(kToolDefinitionObjectType), encode(tool));

    const RobotDesignReader reader(&closure);
    const auto parsed = reader.read(encode(design), 1u);
    ASSERT_TRUE(parsed.ok()) << parsed.error().what();
    EXPECT_EQ(parsed.get().robotLocalName, "bot");
    ASSERT_EQ(parsed.get().tools.size(), 1u);
    EXPECT_EQ(parsed.get().tools.front().localName, "tool");
    // 部件对象确经闭包解析（解引用计数增长——同源绑定面可见）。
    EXPECT_EQ(closure.tryObjectCalls, 1);
}

/**
 * @brief ACC3：reader 版本门（格式版本不识别拒绝——NFR-DEP-04；runtime S2
 *        固定传 1 的归属面：reader 失败→InputInvalid 含定位，§5.2）。
 */
TEST(MdlCanonicalBridge, ReaderFormatVersionGate_WP13T12_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-06"},
                  std::vector<std::string>{});

    RobotDesign design = minimalDesign();
    BridgeClosure closure;
    putRoot(closure, design);
    auto rejected = readRoot(closure, design, 2u);
    ASSERT_FALSE(rejected.ok());
    EXPECT_EQ(rejected.error().code(), runtime::RuntimeErrorCode::InputInvalid);
    // 版本语义并入 detail（SchemaVersionUnsupported 的 reader 侧预演转译）。
    EXPECT_NE(std::string(rejected.error().what()).find("SchemaVersionUnsupported"),
              std::string::npos);
}

/**
 * @brief ACC3（O-36 裁决口径——P-MDL-1 关闭面）：闭包只含真实存储对象
 *        （根——本用例无任何部件引用），关节/连杆子 ObjectId **不在**
 *        闭包而构建成功——objectRefs 复核范围＝真实存储对象，子 ObjectId
 *        为模型内标识（内嵌根对象字节、随 Description 透传供 S5 唯一性面）。
 */
TEST(MdlCanonicalBridge, ClosureScopeRealStoredObjectsOnly_WP13T12_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-04"},
                  std::vector<std::string>{});

    const core::ObjectId jid = makeOid();
    const core::ObjectId lid0 = makeOid();
    const core::ObjectId lid1 = makeOid();
    RobotDesign design;
    design.displayName = "bot";
    design.joints.push_back(makeRevoluteJoint(jid, "J1"));
    design.links.push_back(makeLink(lid0, "base"));
    design.links.push_back(makeLink(lid1, "link1"));

    BridgeClosure closure;
    putRoot(closure, design);  // 闭包仅根对象——关节/连杆子 id 不在闭包
    auto built = buildFrom(closure);
    ASSERT_TRUE(built.ok())
        << "关节/连杆子 ObjectId 不要求∈闭包（O-36 裁决：复核范围＝真实存储"
           "对象；模型内标识内嵌根字节）——" << built.error().detail;
    // 子 ObjectId 随 Description 透传（S5 唯一性/名称映射锚输入面）。
    EXPECT_TRUE(built.get().joints.front().objectId == jid);
    EXPECT_TRUE(built.get().links[0].objectId == lid0);
    EXPECT_TRUE(built.get().links[1].objectId == lid1);
    // 部件解引用零次（无外部引用——闭包复核范围的负证）。
    EXPECT_EQ(closure.tryObjectCalls, 0);
}

/**
 * @brief ACC3：装配契约违约 fail-fast——空闭包指针构造 reader 即拒绝
 *        （§9.2 注入形态的构成性前提；编程错误不走值面——AGENTS 错误
 *        语义）。
 */
TEST(MdlCanonicalBridge, ReaderNullClosureFailFast_WP13T12_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{}, std::vector<std::string>{});

    EXPECT_THROW(const RobotDesignReader reader(nullptr), std::invalid_argument);
}

// =====================================================================
// ACC4：编译诊断定位（MDL-06/DTB 完成条件）
// =====================================================================

/// 错误 params 中是否含（key, value）定位对（定位断言的辅助——保序表扫描）。
bool hasParam(const ModelingError& e, const std::string& key,
              const std::string& value)
{
    for (const auto& [k, v] : e.params) {
        if (k == key && v == value) { return true; }
    }
    return false;
}

/**
 * @brief ACC4：RefMissing——引用对象不在闭包（悬空 toolRef）→错误定位到
 *        对象 ObjectId（params object-id）；两态不产诊断记录（值面错误＋
 *        定位参数——产码唯一经 IDiagnosticFactory，builder 不持工厂）。
 */
TEST(MdlCanonicalBridge, RefMissingLocatesObject_WP13T12_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-06"},
                  std::vector<std::string>{});

    RobotDesign design = minimalDesign();
    const core::ObjectId ghost = makeOid();
    design.toolRefs = {ghost};
    design.defaultTcp = TcpRef{ghost, "tip"};

    BridgeClosure closure;
    putRoot(closure, design);
    std::vector<core::DiagnosticRecord> diags;
    CanonicalModelInputBuilder builder;
    auto built = builder.build(closure, diags);
    ASSERT_FALSE(built.ok());
    EXPECT_EQ(built.error().code, ModelingErrorCode::RefMissing);
    // 定位到对象 ObjectId（错误 params 携带——验收"诊断定位到对象/字段"）。
    EXPECT_TRUE(hasParam(built.error(), "object-id", ghost.toCanonical()))
        << "RefMissing 定位到悬空对象 ObjectId";
    // 两态不产诊断记录（契约预留位——诊断内容由命令层/工厂产码）。
    EXPECT_TRUE(diags.empty());
}

/**
 * @brief ACC4：RefMissing——闭包对象 token 不匹配（存储登记类型与引用期望
 *        不符——路由失联/防混入）→定位到对象＋字段。
 */
TEST(MdlCanonicalBridge, RefMissingTokenMismatch_WP13T12_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-06", "CON-01"},
                  std::vector<std::string>{});

    RobotDesign design = minimalDesign();
    const core::ObjectId toolOid = makeOid();
    design.toolRefs = {toolOid};
    design.defaultTcp = TcpRef{toolOid, "tip"};

    // 工具身份的字节以场景对象 token 登记（路由失联面）。
    ToolDefinition tool;
    tool.objectId = toolOid;
    tool.localName = "tool";
    tool.tcpList.push_back(TcpEntry{"tip", explicitPose(0.0, 0.0, 0.1, 0.0), ""});

    BridgeClosure closure;
    putRoot(closure, design);
    closure.put(toolOid, std::string(kSceneObjectObjectType), encode(tool));

    auto built = buildFrom(closure);
    ASSERT_FALSE(built.ok());
    EXPECT_EQ(built.error().code, ModelingErrorCode::RefMissing);
    EXPECT_TRUE(hasParam(built.error(), "object-id", toolOid.toCanonical()))
        << "token 不匹配定位到对象 ObjectId";
}

/**
 * @brief ACC4：UnitIllegal——Invalid 态字段（已提供但非法，保留原串）无
 *        SI 真值可映射→拒绝并定位到字段（I-MDL-3 单位合法的建模侧履行点；
 *        不静默丢弃——NFR-COR-03）。
 */
TEST(MdlCanonicalBridge, UnitIllegalInvalidStateField_WP13T12_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-06"},
                  std::vector<std::string>{});

    RobotDesign design = minimalDesign();
    design.joints.front().bounds =
        core::SourcedValue<JointLimits>::invalid("not-a-number");

    BridgeClosure closure;
    putRoot(closure, design);
    auto built = buildFrom(closure);
    ASSERT_FALSE(built.ok());
    EXPECT_EQ(built.error().code, ModelingErrorCode::UnitIllegal);
    EXPECT_NE(built.error().detail.find("Invalid"), std::string::npos);
    EXPECT_TRUE(hasParam(built.error(), "field", "joints[0].bounds"))
        << "UnitIllegal 定位到字段路径";
}

/**
 * @brief ACC4：DhExpandFailed——DH 权威链展开前提不成立（权威参数缺失/
 *        非旋转关节混入 DH 链）→定位到对象 ObjectId/字段（消费方到不了
 *        Description 构造——§9.4.7 展开契约）。
 */
TEST(MdlCanonicalBridge, DhExpandFailedLocalized_WP13T12_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-02", "MDL-10"},
                  std::vector<std::string>{});

    // 反例一：DH 权威态关节缺 dhDerived（无可展开参数）。
    RobotDesign missing = minimalDesign();
    missing.authority = AuthorityMode::StandardDH;
    {
        BridgeClosure closure;
        putRoot(closure, missing);
        auto built = buildFrom(closure);
        ASSERT_FALSE(built.ok());
        EXPECT_EQ(built.error().code, ModelingErrorCode::DhExpandFailed);
        EXPECT_NE(built.error().detail.find("dhDerived"), std::string::npos);
    }
    // 反例二：DH 链混入移动关节（标准 DH 只参数化旋转关节——§7.4）。
    // J1 先补齐 dhDerived（合法 DH 态）——保证命中"非旋转关节"分支而非
    // 前一分支。
    RobotDesign prismatic = minimalDesign();
    prismatic.authority = AuthorityMode::StandardDH;
    {
        prismatic.joints.front().dhDerived = DhParameters{};
        JointEntry pj = makeRevoluteJoint(makeOid(), "J2", 0.0, 1.0);
        pj.type = JointType::Prismatic;
        pj.dhDerived = DhParameters{};  // 有参数但类型不合——展开前提仍不成立
        prismatic.joints.push_back(pj);
        prismatic.links.push_back(makeLink(makeOid(), "link2"));
        BridgeClosure closure;
        putRoot(closure, prismatic);
        auto built = buildFrom(closure);
        ASSERT_FALSE(built.ok());
        EXPECT_EQ(built.error().code, ModelingErrorCode::DhExpandFailed);
        EXPECT_NE(built.error().detail.find("非旋转关节"), std::string::npos);
    }
}

/**
 * @brief ACC4：SchemaVersionUnsupported——解码门（字节主版本超出支持——
 *        NFR-DEP-04 拒绝不猜测）：字节主版本补丁为 99（头布局 magic(7)＋
 *        major(4) 小端）即得"未来版本"字节。
 */
TEST(MdlCanonicalBridge, SchemaVersionUnsupportedDecodeGate_WP13T12_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-06"},
                  std::vector<std::string>{});

    RobotDesign design = minimalDesign();
    std::vector<std::uint8_t> patched = encode(design);
    patched[7] = 99;  // major 低字节（小端——99<256 即可）
    BridgeClosure closure;
    closure.put(std::string(kRobotDesignObjectType), patched);
    auto built = buildFrom(closure);
    ASSERT_FALSE(built.ok());
    EXPECT_EQ(built.error().code, ModelingErrorCode::SchemaVersionUnsupported);
}

// =====================================================================
// ACC5：R-4 红线＋输入质量面（V-26/AT-18）
// =====================================================================

/**
 * @brief ACC5（R-4）：reader 不做名称解析/前缀拼接/消歧——名称原样透传
 *        （逐字节一致）；重复名称拒绝而非改名（链作用域重复由解码门
 *        I-MDL-2 拒绝＝MalformedPayload——绝无"自动改成 J1_2"的消歧面）。
 */
TEST(MdlCanonicalBridge, NoNameGenerationOrDisambiguation_WP13T12_ACC5)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-14", "ARC-04"},
                  std::vector<std::string>{"AT-18"});

    // 名称含合法字符集全谱字符——逐字节透传（无前缀/后缀/合法化改写）。
    RobotDesign design = minimalDesign();
    const std::string fancy = "J_1.axis-2";
    design.joints.front().localName = fancy;

    BridgeClosure closure;
    putRoot(closure, design);
    auto built = buildFrom(closure);
    ASSERT_TRUE(built.ok()) << built.error().detail;
    EXPECT_EQ(built.get().joints.front().localName, fancy)
        << "名称原样透传（消歧/合法化唯一归 runtime——R-4）";
    EXPECT_EQ(built.get().robotLocalName, "bot");

    // 链作用域重复：解码门拒绝（MalformedPayload——I-MDL-2），绝不消歧改名。
    RobotDesign dup = minimalDesign();
    dup.joints.front().localName = "same";
    dup.links[0].localName = "same";  // 链作用域（joints∪links）重复
    BridgeClosure closureDup;
    putRoot(closureDup, dup);
    auto builtDup = buildFrom(closureDup);
    ASSERT_FALSE(builtDup.ok());
    EXPECT_EQ(builtDup.error().code, ModelingErrorCode::MalformedPayload)
        << "重复拒绝而非改名（消歧唯一归 runtime——R-4；解码门 I-MDL-2）";
}

/**
 * @brief ACC5（V-26/AT-18 输入质量面）：localName 合法字符集断言（空串/
 *        字符集外→IllegalName，定位到字段）；robotLocalName 输入面（空/
 *        含 '/'——runtime §4.3.3 硬规则的建模侧预演）；工具作用域重复→
 *        IllegalName（跨对象作用域，解码门不覆盖）。
 */
TEST(MdlCanonicalBridge, LocalNameInputQualityFace_WP13T12_ACC5)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-14"},
                  std::vector<std::string>{"AT-18"});

    // ①关节名含字符集外字符（空格）→IllegalName。
    {
        RobotDesign design = minimalDesign();
        design.joints.front().localName = "J 1";  // 空格∈[A-Za-z0-9_.-] 之外
        BridgeClosure closure;
        putRoot(closure, design);
        auto built = buildFrom(closure);
        ASSERT_FALSE(built.ok());
        EXPECT_EQ(built.error().code, ModelingErrorCode::IllegalName);
        EXPECT_TRUE(hasParam(built.error(), "field", "joints[0].localName"));
    }
    // ②robotLocalName 含 '/'→IllegalName（进设备作用域的名称不可含分隔符）。
    {
        RobotDesign design = minimalDesign();
        design.displayName = "IRB/6700";
        BridgeClosure closure;
        putRoot(closure, design);
        auto built = buildFrom(closure);
        ASSERT_FALSE(built.ok());
        EXPECT_EQ(built.error().code, ModelingErrorCode::IllegalName);
        EXPECT_NE(built.error().detail.find("robotLocalName"), std::string::npos);
    }
    // ③robotLocalName 空→IllegalName（不伪造默认名——不假设 runtime 修复）。
    {
        RobotDesign design = minimalDesign();
        design.displayName.clear();
        BridgeClosure closure;
        putRoot(closure, design);
        auto built = buildFrom(closure);
        ASSERT_FALSE(built.ok());
        EXPECT_EQ(built.error().code, ModelingErrorCode::IllegalName);
    }
    // ④工具作用域重复（两个独立工具对象同名——解码门不覆盖跨对象作用域）。
    {
        const core::ObjectId toolA = makeOid();
        const core::ObjectId toolB = makeOid();
        RobotDesign design = minimalDesign();
        design.toolRefs = {toolA, toolB};
        design.defaultTcp = TcpRef{toolA, "tip"};
        ToolDefinition a;
        a.objectId = toolA;
        a.localName = "gripper";
        a.tcpList.push_back(TcpEntry{"tip", explicitPose(0.0, 0.0, 0.05, 0.0), ""});
        ToolDefinition b;
        b.objectId = toolB;
        b.localName = "gripper";  // 与工具 A 同名（tools 作用域重复）
        b.tcpList.push_back(TcpEntry{"tip", explicitPose(0.0, 0.0, 0.06, 0.0), ""});
        BridgeClosure closure;
        putRoot(closure, design);
        closure.put(toolA, std::string(kToolDefinitionObjectType), encode(a));
        closure.put(toolB, std::string(kToolDefinitionObjectType), encode(b));
        auto built = buildFrom(closure);
        ASSERT_FALSE(built.ok());
        EXPECT_EQ(built.error().code, ModelingErrorCode::IllegalName)
            << "工具作用域重复→输入面拒绝（消歧归 runtime——R-4；建模侧只断言）";
    }
}

/**
 * @brief ACC4/ACC5（不完整模型三层防线边界＋可映射性拒绝）：必备输入缺失
 *        值面拒绝——不假设 runtime 修复输入（runtime S3/S5 兜底不归本单元）：
 *        可动关节权威轴缺失/场景对象无几何引用（runtime 环境几何必有——
 *        不可伪造）→RefMissing 定位；两态零诊断记录。
 */
TEST(MdlCanonicalBridge, IncompleteModelRefusesWithoutRuntimeRepair_WP13T12_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-06"},
                  std::vector<std::string>{});

    // ①可动关节权威轴缺失（Explicit 权威一等字段缺失——Description 无缺失
    // 承载，拒绝映射，不伪造几何）。
    {
        RobotDesign design = minimalDesign();
        design.joints.front().axis =
            core::SourcedValue<rwmath::Vector3D<double>>::notProvided();
        BridgeClosure closure;
        putRoot(closure, design);
        std::vector<core::DiagnosticRecord> diags;
        CanonicalModelInputBuilder builder;
        auto built = builder.build(closure, diags);
        ASSERT_FALSE(built.ok());
        EXPECT_EQ(built.error().code, ModelingErrorCode::RefMissing);
        EXPECT_NE(built.error().detail.find("axis"), std::string::npos);
        EXPECT_TRUE(diags.empty()) << "两态不产诊断记录（值面错误＋定位参数）";
    }
    // ②场景对象无几何引用（runtime CanonicalSceneObject.geometry 必有——
    // 不伪造资源引用，定位到对象）。
    {
        const core::ObjectId sid = makeOid();
        RobotDesign design = minimalDesign();
        design.sceneRefs = {sid};
        SceneObject bare;
        bare.objectId = sid;
        bare.localName = "bare";  // geometry 缺省＝nullopt
        BridgeClosure closure;
        putRoot(closure, design);
        closure.put(sid, std::string(kSceneObjectObjectType), encode(bare));
        auto built = buildFrom(closure);
        ASSERT_FALSE(built.ok());
        EXPECT_EQ(built.error().code, ModelingErrorCode::RefMissing);
        EXPECT_TRUE(hasParam(built.error(), "object-id", sid.toCanonical()))
            << "场景几何缺失定位到对象 ObjectId";
    }
}

/**
 * @brief ACC1（Fixed 关节映射）：Fixed 无可动轴（I-MDL-6 不适用）→axis
 *        缺省时以 +Z 单位向量中性填充（结构承载面，无运动语义）；origin
 *        必备（链几何）。
 */
TEST(MdlCanonicalBridge, FixedJointAxisNeutralFill_WP13T12_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-12"},
                  std::vector<std::string>{});

    RobotDesign design = minimalDesign();
    JointEntry fixed = makeRevoluteJoint(makeOid(), "Jfix", 0.0, 0.0);
    fixed.type = JointType::Fixed;
    fixed.bounds = core::SourcedValue<JointLimits>::notApplicable();  // Fixed 无限位
    fixed.axis = core::SourcedValue<rwmath::Vector3D<double>>::notProvided();
    design.joints.push_back(fixed);
    design.links.push_back(makeLink(makeOid(), "link2"));

    BridgeClosure closure;
    putRoot(closure, design);
    auto built = buildFrom(closure);
    ASSERT_TRUE(built.ok()) << built.error().detail;
    ASSERT_EQ(built.get().joints.size(), 2u);
    const runtime::JointDescription& fj = built.get().joints[1];
    EXPECT_EQ(fj.type, runtime::JointType::Fixed);
    EXPECT_TRUE(sameVec(fj.axis, rwmath::Vector3D<double>(0.0, 0.0, 1.0)))
        << "Fixed 关节轴中性填充（+Z——结构承载；无运动语义）";
    EXPECT_EQ(fj.lower.state(), core::FieldState::NotProvided);
    EXPECT_EQ(fj.upper.state(), core::FieldState::NotProvided);
}
