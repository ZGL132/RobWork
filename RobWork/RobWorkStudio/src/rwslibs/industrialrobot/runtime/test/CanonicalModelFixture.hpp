/**
 * @file   CanonicalModelFixture.hpp
 * @brief  RT-T04 测试夹具——可复现的对象 id/摘要＋minimal/rich 两档模型部件
 *         包（CanonicalModelTest 与 CodecTest 共用；仅测试内部可见）。
 *
 * 设计依据：units/runtime.md §4.3（字段表——夹具字段逐一对应）、§11（测试
 * 设施口径——同夹具输入、确定性）、RT-STUB-0（替身边界声明精神——本夹具
 * 是值构造工具，不伪造任何框架行为；全部断言针对真实产品代码）。
 *
 * 确定性：id/摘要由固定种子经 core::ContentDigester 派生（SHA-256 前缀），
 * 不用 ObjectId::generate()（随机）——"同夹具"在任意两次运行中字节一致，
 * RT-ID-1 的跨构建稳定断言因此可跨进程复核。
 *
 * 线程安全：纯值构造（无共享状态）；每个 TEST 各持一份部件包。
 */

#ifndef SDURWS_IRD_RUNTIME_TEST_CANONICALMODELFIXTURE_HPP
#define SDURWS_IRD_RUNTIME_TEST_CANONICALMODELFIXTURE_HPP

#include <sdurws/ird/runtime/CanonicalModel.hpp>
#include <sdurws/ird/runtime/Codec.hpp>
#include <sdurws/ird/runtime/Description.hpp>
#include <sdurws/ird/runtime/Errors.hpp>
#include <sdurws/ird/runtime/Resource.hpp>
#include <sdurws/ird/runtime/Sources.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sdurws::ird::runtime::testfixture {

using sdurws::ird::runtime::CanonicalJoint;
using sdurws::ird::runtime::CanonicalLink;
using sdurws::ird::runtime::CanonicalModel;
using sdurws::ird::runtime::CanonicalModelBuilder;
using sdurws::ird::runtime::CanonicalModelHeader;
using sdurws::ird::runtime::CanonicalSceneObject;
using sdurws::ird::runtime::CanonicalTool;
using sdurws::ird::runtime::CanonicalDrivetrain;
using sdurws::ird::runtime::JointType;
using sdurws::ird::runtime::ResourceRef;
using sdurws::ird::runtime::ResourceState;
using sdurws::ird::runtime::RobotChain;
using sdurws::ird::runtime::RuntimeError;
using sdurws::ird::runtime::RuntimeErrorCode;
using sdurws::ird::runtime::WorldPlacement;
namespace core = sdurws::ird::core;

// =====================================================================
// 派生工具（确定性 id/摘要——注释见文件头）。
// =====================================================================

/// 固定种子的 SHA-256 摘要（测试专用确定性来源——非产品路径）。
inline core::Digest256 digestOf(const std::string& seed)
{
    core::ContentDigester d;
    d.update(seed.data(), seed.size());
    return d.finalize();
}

/// 从固定种子派生 16 字节强类型 id（前 16 字节；测试值——非密码学用途）。
template <typename Id>
inline Id idFrom(const std::string& seed)
{
    const core::Digest256 d = digestOf(seed);
    Id id;
    std::copy(d.begin(), d.begin() + 16, id.bytes.begin());
    return id;
}

/// 内容版本（32 字节摘要——CON-01 身份/版本包络的测试值）。
inline core::ContentVersion cvFrom(const std::string& seed)
{
    core::ContentVersion cv;
    cv.bytes = digestOf(seed);
    return cv;
}

/// 用户输入来源记录（core P-1 校验通过的最简形态）。
inline core::ValueProvenance userProv()
{
    return core::ValueProvenance::make(core::ProvenanceKind::UserProvided);
}

/// Provided 态标量（测试数值注入入口）。
inline core::SourcedValue<double> val(double v)
{
    return core::SourcedValue<double>::provided(v, userProv());
}

/// SPD 对角惯量（单位 kg·m²——质心系；合法域内的对角张量）。
inline rw::math::InertiaMatrix<double> diagInertia(double i1, double i2, double i3)
{
    return rw::math::InertiaMatrix<double>(i1, 0, 0, 0, i2, 0, 0, 0, i3);
}

// =====================================================================
// 部件包（与 CanonicalModelBuilder 的输入一一对应——反例注入统一入口）。
// =====================================================================

struct Fixture {
    CanonicalModelHeader header;
    WorldPlacement world;
    RobotChain chain;
    std::vector<CanonicalTool> tools;
    std::optional<std::uint32_t> defaultTcp;
    std::vector<CanonicalSceneObject> scene;
    CanonicalDrivetrain drivetrain;
    std::vector<ResourceRef> manifest;
    std::vector<core::DiagnosticRecord> diagnostics;

    /// 组装 builder（值拷贝——测试可在副本上注入反例后 build）。
    CanonicalModelBuilder toBuilder() const
    {
        return CanonicalModelBuilder()
            .setHeader(header)
            .setWorld(world)
            .setChain(chain)
            .setTools(tools)
            .setDefaultTcpIndex(defaultTcp)
            .setScene(scene)
            .setDrivetrain(drivetrain)
            .setResourceManifest(manifest)
            .setDiagnostics(diagnostics);
    }

    /// 直接构造（等价 toBuilder().build()）。
    CanonicalModel build() const { return toBuilder().build(); }
};

// =====================================================================
// minimal 夹具：2 旋转关节、无物性/摩擦（能力缺失路径）、1 条未引用资源。
// =====================================================================

inline Fixture minimalFixture()
{
    Fixture f;
    // ---- 身份与来源块（§4.3.1）----
    f.header.project = idFrom<core::ProjectId>("prj-A");
    f.header.branch = idFrom<core::BranchId>("brn-A");
    f.header.revision = idFrom<core::RevisionId>("rev-1");
    f.header.revisionSeq = 7;
    f.header.descriptionContractVersion = 1;
    f.header.compilerContractVersion = 1;
    f.header.builtFrom = digestOf("description-bytes");

    // ---- 修订闭包引用（对象逐一登记——CM-0 值层面）----
    const core::ObjectId robot = idFrom<core::ObjectId>("robot");
    const core::ObjectId j1 = idFrom<core::ObjectId>("j1");
    const core::ObjectId j2 = idFrom<core::ObjectId>("j2");
    const core::ObjectId l0 = idFrom<core::ObjectId>("l0");
    const core::ObjectId l1 = idFrom<core::ObjectId>("l1");
    const core::ObjectId l2 = idFrom<core::ObjectId>("l2");
    auto addRef = [&f](const core::ObjectId& id, const char* seed, const char* token) {
        ObjectRefEntry e;
        e.objectId = id;
        e.contentVersion = cvFrom(seed);
        e.objectTypeToken = token;
        e.digest = digestOf(std::string{seed} + "-bytes");
        f.header.objectRefs.push_back(e);
    };
    addRef(robot, "robot", "robot-design");
    addRef(j1, "j1", "joint");
    addRef(j2, "j2", "joint");
    addRef(l0, "l0", "link");
    addRef(l1, "l1", "link");
    addRef(l2, "l2", "link");

    // ---- 资源清单（§4.3.5——逐条摘要非零；minimal 不被引用）----
    ResourceRef mesh;
    mesh.resourceId = idFrom<core::ObjectId>("res-1");
    mesh.contentDigest = digestOf("mesh-1-bytes");
    mesh.state = ResourceState::Recorded;
    mesh.accessVersion = 1;
    f.manifest.push_back(mesh);

    // ---- 世界与基座（§4.3.2——默认地面：R=I、g=(0,0,−9.81)、preset 缺省）----

    // ---- 机器人链（§4.3.3——2 旋转关节，限位/限速 Provided）----
    f.chain.robotObjectId = robot;
    f.chain.robotLocalName = "IRB_T";
    f.chain.deviceName = "IRB_T";

    CanonicalJoint joint1;
    joint1.objectId = j1;
    joint1.localName = "joint_1";
    joint1.type = JointType::Revolute;
    joint1.axis = rw::math::Vector3D<double>(0.0, 0.0, 1.0);
    joint1.bounds = JointBounds{-2.97, 2.97};  // 单位 rad（聚合逐成员初始化）
    joint1.maxVelocity = val(2.5);
    f.chain.joints.push_back(joint1);

    CanonicalJoint joint2;
    joint2.objectId = j2;
    joint2.localName = "joint_2";
    joint2.type = JointType::Revolute;
    joint2.axis = rw::math::Vector3D<double>(0.0, 1.0, 0.0);
    const double pi = 3.14159265358979323846;
    joint2.bounds = JointBounds{-pi, pi};  // 单位 rad
    joint2.maxVelocity = val(2.0);
    f.chain.joints.push_back(joint2);

    for (int i = 0; i < 3; ++i) {
        CanonicalLink link;
        link.objectId = (i == 0) ? l0 : (i == 1 ? l1 : l2);
        link.localName = (i == 0) ? "base_link" : ("link_" + std::to_string(i));
        f.chain.links.push_back(link);
    }
    return f;
}

// JointBounds/JointBounds 聚合无构造函数——夹具用 C++17 聚合逐成员花括号
// 初始化（如 JointBounds{-2.97, 2.97}），语义＝lower/upper 依序赋值。

// =====================================================================
// rich 夹具：minimal ＋全物性＋工具/场景/传动/摩擦＋资源引用（全能力位）。
// =====================================================================

inline Fixture richFixture()
{
    Fixture f = minimalFixture();

    const core::ObjectId t1 = idFrom<core::ObjectId>("t1");
    const core::ObjectId s1 = idFrom<core::ObjectId>("s1");
    const core::ObjectId r2 = idFrom<core::ObjectId>("res-2");

    // 闭包引用补全（工具/场景对象）。
    {
        ObjectRefEntry e;
        e.objectId = t1;
        e.contentVersion = cvFrom("t1");
        e.objectTypeToken = "tool";
        e.digest = digestOf("t1-bytes");
        f.header.objectRefs.push_back(e);
        e.objectId = s1;
        e.contentVersion = cvFrom("s1");
        e.objectTypeToken = "scene";
        e.digest = digestOf("s1-bytes");
        f.header.objectRefs.push_back(e);
    }

    // 第二条资源（场景/碰撞几何引用——与 res-1 区分资源索引多条目路径）。
    ResourceRef mesh2;
    mesh2.resourceId = r2;
    mesh2.contentDigest = digestOf("mesh-2-bytes");
    mesh2.sourcePathHint = std::string{"assets/mesh2.stl"};
    mesh2.state = ResourceState::Solidified;
    mesh2.accessVersion = 2;
    f.manifest.push_back(mesh2);

    // 资源引用便捷量（按 resourceId+digest 命中 manifest——§4.3.3 引用约束）。
    const ResourceRef ref1 = f.manifest.at(0);
    const ResourceRef ref2 = mesh2;

    // ---- 全连杆物性（mass/com/inertia Provided——hasFullMassInertia=true）----
    const double masses[3] = {5.0, 4.0, 3.0};
    for (std::size_t i = 0; i < f.chain.links.size(); ++i) {
        CanonicalLink& l = f.chain.links.at(i);
        l.mass = val(masses[i]);
        l.centerOfMass = core::SourcedValue<rw::math::Vector3D<double>>::provided(
            rw::math::Vector3D<double>(0.0, 0.0, static_cast<double>(i) * 0.1), userProv());
        l.inertia = core::SourcedValue<rw::math::InertiaMatrix<double>>::provided(
            diagInertia(0.01, 0.02, 0.03), userProv());
        if (i == 1) { l.visual = ref1; }
        if (i == 2) { l.collision = ref2; }
    }

    // ---- 逐关节摩擦（MDL-16 全 Provided——hasFrictionModel=true）----
    for (CanonicalJoint& j : f.chain.joints) {
        j.friction.viscous = val(0.1);
        j.friction.coulomb = val(0.2);
        j.friction.bias = val(0.01);
    }

    // ---- 工具（首项默认 TCP——KIN-14）＋场景对象＋传动 ----
    CanonicalTool tool;
    tool.objectId = t1;
    tool.localName = "tool_1";
    tool.geometry = ref1;
    tool.mass = val(1.2);
    tool.centerOfMass = core::SourcedValue<rw::math::Vector3D<double>>::provided(
        rw::math::Vector3D<double>(0.0, 0.0, 0.05), userProv());
    tool.inertia = core::SourcedValue<rw::math::InertiaMatrix<double>>::provided(
        diagInertia(0.001, 0.001, 0.002), userProv());
    f.tools.push_back(tool);
    f.defaultTcp = 0;

    CanonicalSceneObject obstacle;
    obstacle.objectId = s1;
    obstacle.localName = "obstacle_1";
    obstacle.worldPose = rw::math::Transform3D<double>(
        rw::math::Vector3D<double>(1.0, 0.0, 0.0),
        rw::math::Rotation3D<double>(1, 0, 0, 0, 1, 0, 0, 0, 1));
    obstacle.geometry = ref2;
    f.scene.push_back(obstacle);

    f.drivetrain.ratioPerJoint = {val(1.0), val(1.0)};

    // ---- 诊断（警告级——非硬错误码；全字段往返/排除用例的承载）----
    core::DiagnosticRecord warn = core::DiagnosticRecord::make(
        std::string{"RT-CAPABILITY-MISSING"}, std::nullopt, std::string{"joint_1"},
        std::string{}, std::string{"能力缺失警告（测试夹具）"},
        std::string{"maxAcceleration 未提供"}, std::string{"建模侧补全后重编译"});
    f.diagnostics.push_back(warn);
    return f;
}

// =====================================================================
// 断言助手。
// =====================================================================

/// 断言 builder.build() 抛出指定稳定码的 RuntimeError（fail-fast 轨——调用方
/// 违约；接收 builder 值副本，构造成功即记失败）。
inline void expectBuildThrows(CanonicalModelBuilder builder, RuntimeErrorCode code,
                              const char* what)
{
    try {
        builder.build();
        ADD_FAILURE() << what << "：未抛出（期望稳定码 " << token(code) << "）";
    } catch (const RuntimeError& e) {
        EXPECT_EQ(e.code(), code) << what << "：稳定码不符（实得 " << token(e.code())
                                  << "，detail=" << e.what() << "）";
    } catch (const std::exception& e) {
        ADD_FAILURE() << what << "：抛出非 RuntimeError 异常（" << e.what() << "）";
    }
}

}  // namespace sdurws::ird::runtime::testfixture

#endif  // SDURWS_IRD_RUNTIME_TEST_CANONICALMODELFIXTURE_HPP
