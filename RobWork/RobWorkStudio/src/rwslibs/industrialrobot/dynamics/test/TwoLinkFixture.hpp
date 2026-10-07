/**
 * @file   TwoLinkFixture.hpp
 * @brief  WP-17-T03 测试夹具——平面二连杆 CanonicalModel 直构（仅测试内
 *         部可见；R-2 纪律：不跨单元 include 他单元测试私有头，本夹具
 *         自持同款确定性 id/摘要助手，直接消费 runtime 公共头
 *         CanonicalModelBuilder——阶段 A 夹具直构先例）。
 *
 * 设计依据：units/dynamics.md §5（RNEA 输入面）、§10.1（评估请求）、
 *   units/runtime.md §4.3（CanonicalModel 字段表——夹具字段逐一对应）；
 *   RT-STUB-0 精神——本夹具是值构造工具，不伪造任何框架行为，全部断言
 *   针对真实产品代码。
 *
 * 链几何（全用例共享，解析算例的基准面）：
 *   - 两旋转关节轴均沿基座系 +Y（平面 XZ 竖直平面机构——静态重力矩绕
 *     Y 轴非零，适合解析对照；水平面转动机器人重力矩恒零无对照价值）；
 *   - 连杆 1 沿 +X 长 L1（质心 c1）；连杆 2 质心 c2（随关节 2 旋转）；
 *   - 单位：长度 m、质量 kg、惯量 kg·m²、角 rad——全 SI。
 *
 * 确定性：id/摘要由固定种子经 core::ContentDigester 派生（SHA-256 前缀）
 *   ——同夹具任意两次运行字节一致（RT-ID-1 同口径）；不用随机 id。
 * 线程安全：纯值构造；每个 TEST 各持一份部件。
 */

#ifndef IRD_DYNAMICS_TEST_TWOLINKFIXTURE_HPP
#define IRD_DYNAMICS_TEST_TWOLINKFIXTURE_HPP

#include <sdurws/ird/core/Provenance.hpp>
#include <sdurws/ird/runtime/CanonicalModel.hpp>
#include <sdurws/ird/runtime/Description.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sdurws::ird::dynamics::testfixture {

using runtime::CanonicalJoint;
using runtime::CanonicalLink;
using runtime::CanonicalModel;
using runtime::CanonicalModelBuilder;
using runtime::CanonicalModelHeader;
using runtime::CanonicalTool;
using runtime::CanonicalDrivetrain;
using runtime::JointBounds;
using runtime::JointType;
using runtime::ObjectRefEntry;
using runtime::RobotChain;
using runtime::WorldPlacement;
namespace core = sdurws::ird::core;

// =====================================================================
// 链几何常量（解析算例基准——全用例共享；单位逐项注明）。
// =====================================================================

inline constexpr double kL1 = 0.5;   ///< 连杆 1 长度（关节 1→关节 2 距离），单位 m
inline constexpr double kC1 = 0.2;   ///< 连杆 1 质心距关节 1，单位 m
inline constexpr double kM1 = 2.0;   ///< 连杆 1 质量，单位 kg
inline constexpr double kC2 = 0.15;  ///< 连杆 2 质心距关节 2，单位 m
inline constexpr double kM2 = 1.0;   ///< 连杆 2 质量，单位 kg
inline constexpr double kI1Yy = 0.01;  ///< 连杆 1 绕关节轴（Y）惯量分量，单位 kg·m²
inline constexpr double kI2Yy = 0.005; ///< 连杆 2 绕关节轴（Y）惯量分量，单位 kg·m²

// =====================================================================
// 派生工具（确定性 id/摘要——同 runtime 测试夹具形态、自持实现）。
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

/// 几何估算来源记录（MDL-05 估算语义——降级计数用例的注入面）。
inline core::ValueProvenance estimateProv()
{
    return core::ValueProvenance::make(core::ProvenanceKind::GeometricEstimate);
}

/// Provided 态标量（测试数值注入入口）。
inline core::SourcedValue<double> val(double v)
{
    return core::SourcedValue<double>::provided(v, userProv());
}

/// SPD 对角惯量（单位 kg·m²——质心系；合法域内的对角张量）。
inline rw::math::InertiaMatrix<double> diagInertia(double ixx, double iyy, double izz)
{
    return rw::math::InertiaMatrix<double>(ixx, 0, 0, 0, iyy, 0, 0, 0, izz);
}

/// 单位旋转（逐元素——不用外联的 Rotation3D::identity()，冒烟纪律同款）。
inline rw::math::Rotation3D<double> identityRotation()
{
    return rw::math::Rotation3D<double>(1, 0, 0, 0, 1, 0, 0, 0, 1);
}

// =====================================================================
// 模型组装参数与入口。
// =====================================================================

/// 逐关节摩擦注入（MDL-16 三元组——full=false 时三分量全 NotProvided）。
struct FrictionSpec {
    bool full = false;       ///< true＝三元组全 Provided；false＝全缺（DYN-06 降级路径）
    double viscous = 0.0;    ///< fv，单位 N·m·s/rad（full 时生效）
    double coulomb = 0.0;    ///< fc，单位 N·m
    double bias = 0.0;       ///< 偏置，单位 N·m
};

/// 末端工具注入（optional 语义——nullopt＝无工具模型）。
struct ToolSpec {
    double mass = 1.0;       ///< 工具质量，单位 kg
    bool hasCom = true;      ///< false＝com 缺失（保守估算路径——D-DYN-6）
    double comX = 0.0;       ///< 工具质心 X（TCP 系下表示），单位 m
    double tcpOffsetX = 0.25; ///< 法兰→TCP 平移 X（法兰系下表示），单位 m
    bool estimateProvenance = false; ///< true＝物性来源标 GeometricEstimate
};

/**
 * @brief 组装平面二连杆 CanonicalModel（阶段 A 夹具直构——CanonicalModel
 *        唯一构造路径 CanonicalModelBuilder；字段合法性约束全满足——
 *        build() 任一不变量违约即抛 RuntimeError，测试夹具自身即被构建器
 *        校验覆盖）。
 *
 * @param world      [in] 世界与基座块（地面/倒挂由 T_world_base 决定——
 *                   MDL-22 单一存储；重力世界系恒 (0,0,−9.81)）
 * @param friction   [in] 摩擦注入（逐关节同参——简化算例）
 * @param tool       [in] 工具注入（nullopt＝无工具）
 * @param ratioPerJoint [in] 传动比注入（DYN-04 边界用例的对照面——RNEA
 *                   不消费该块；空＝全缺省）
 * @return 构造完成的 CanonicalModel（contentIdentity 非零）
 */
inline CanonicalModel makeTwoLinkModel(const WorldPlacement& world,
                                       const FrictionSpec& friction,
                                       const std::optional<ToolSpec>& tool,
                                       const std::vector<double>& ratioPerJoint = {})
{
    // ---- 身份与来源块（§4.3.1——非空 id＋契约版本＋摘要，build() 强制）----
    CanonicalModelHeader header;
    header.project = idFrom<core::ProjectId>("prj-dyn");
    header.branch = idFrom<core::BranchId>("brn-dyn");
    header.revision = idFrom<core::RevisionId>("rev-dyn-1");
    header.revisionSeq = 1;
    header.descriptionContractVersion = 1;
    header.compilerContractVersion = 1;
    header.builtFrom = digestOf("two-link-description-bytes");

    // ---- 修订闭包引用（对象逐一登记——CM-0 值层面：robot/j1/j2/l0/l1/l2
    //      ＋可选 tool；缺登记 build() 即拒绝）----
    const core::ObjectId robot = idFrom<core::ObjectId>("dyn-robot");
    const core::ObjectId j1 = idFrom<core::ObjectId>("dyn-j1");
    const core::ObjectId j2 = idFrom<core::ObjectId>("dyn-j2");
    const core::ObjectId l0 = idFrom<core::ObjectId>("dyn-l0");
    const core::ObjectId l1 = idFrom<core::ObjectId>("dyn-l1");
    const core::ObjectId l2 = idFrom<core::ObjectId>("dyn-l2");
    const core::ObjectId toolId = idFrom<core::ObjectId>("dyn-tool");
    auto addRef = [&header](const core::ObjectId& id, const char* seed, const char* token) {
        ObjectRefEntry e;
        e.objectId = id;
        e.contentVersion = cvFrom(seed);
        e.objectTypeToken = token;
        e.digest = digestOf(std::string{seed} + "-bytes");
        header.objectRefs.push_back(e);
    };
    addRef(robot, "dyn-robot", "robot-design");
    addRef(j1, "dyn-j1", "joint");
    addRef(j2, "dyn-j2", "joint");
    addRef(l0, "dyn-l0", "link");
    addRef(l1, "dyn-l1", "link");
    addRef(l2, "dyn-l2", "link");

    // ---- 机器人链（§4.3.3——两旋转关节轴沿 +Y，平面 XZ 机构）----
    RobotChain chain;
    chain.robotObjectId = robot;
    chain.robotLocalName = "DYN_R2";
    chain.deviceName = "DYN_R2";

    CanonicalJoint joint1;
    joint1.objectId = j1;
    joint1.localName = "joint_1";
    joint1.type = JointType::Revolute;
    joint1.axis = rw::math::Vector3D<double>(0.0, 1.0, 0.0);  // 关节轴：基座系 +Y（单位向量）
    joint1.bounds = JointBounds{-3.14159265358979323846, 3.14159265358979323846};  // rad
    joint1.maxVelocity = val(3.0);   // rad/s
    // 摩擦三元组（MDL-16）：full＝全 Provided；否则保持默认 NotProvided
    // （SourcedValue 默认构造＝未提供——DYN-06 降级路径的注入面）。
    if (friction.full) {
        joint1.friction.viscous = val(friction.viscous);
        joint1.friction.coulomb = val(friction.coulomb);
        joint1.friction.bias = val(friction.bias);
    }
    chain.joints.push_back(joint1);

    CanonicalJoint joint2 = joint1;      // 同参复制后改差异字段
    joint2.objectId = j2;
    joint2.localName = "joint_2";
    // origin＝父连杆系→关节系：沿 +X 平移 L1（单位 m）、零旋转。
    joint2.origin = rw::math::Transform3D<double>(
        rw::math::Vector3D<double>(kL1, 0.0, 0.0), identityRotation());
    chain.joints.push_back(joint2);

    // ---- 连杆（数量＝关节数＋1；links[i] 为关节 i 的父体——链约定）----
    CanonicalLink baseLink;
    baseLink.objectId = l0;
    baseLink.localName = "base_link";    // 基座连杆：物性不入递推（评估器体号约定）
    chain.links.push_back(baseLink);

    CanonicalLink link1;
    link1.objectId = l1;
    link1.localName = "link_1";
    link1.mass = val(kM1);               // kg
    link1.centerOfMass = core::SourcedValue<rw::math::Vector3D<double>>::provided(
        rw::math::Vector3D<double>(kC1, 0.0, 0.0), userProv());   // 连杆系下，m
    link1.inertia = core::SourcedValue<rw::math::InertiaMatrix<double>>::provided(
        diagInertia(0.02, kI1Yy, 0.03), userProv());              // 质心系，kg·m²
    chain.links.push_back(link1);

    CanonicalLink link2;
    link2.objectId = l2;
    link2.localName = "link_2";
    link2.mass = val(kM2);
    link2.centerOfMass = core::SourcedValue<rw::math::Vector3D<double>>::provided(
        rw::math::Vector3D<double>(kC2, 0.0, 0.0), userProv());
    link2.inertia = core::SourcedValue<rw::math::InertiaMatrix<double>>::provided(
        diagInertia(0.008, kI2Yy, 0.012), userProv());
    chain.links.push_back(link2);

    // ---- 工具（optional——KIN-14 默认 TCP；tcpOffset＝T_flange_tcp）----
    std::vector<CanonicalTool> tools;
    std::optional<std::uint32_t> defaultTcp;
    if (tool.has_value()) {
        CanonicalTool t;
        t.objectId = toolId;
        t.localName = "tool_1";
        const core::ValueProvenance prov = tool->estimateProvenance ? estimateProv() : userProv();
        t.mass = core::SourcedValue<double>::provided(tool->mass, prov);  // kg
        if (tool->hasCom) {
            t.centerOfMass = core::SourcedValue<rw::math::Vector3D<double>>::provided(
                rw::math::Vector3D<double>(tool->comX, 0.0, 0.0), prov);  // TCP 系下，m
        }
        t.inertia = core::SourcedValue<rw::math::InertiaMatrix<double>>::provided(
            diagInertia(0.001, 0.001, 0.002), prov);                       // 质心系，kg·m²
        t.tcpOffset = rw::math::Transform3D<double>(
            rw::math::Vector3D<double>(tool->tcpOffsetX, 0.0, 0.0), identityRotation());
        addRef(toolId, "dyn-tool", "tool");
        tools.push_back(t);
        defaultTcp = 0;
    }

    // ---- 传动块（DYN-04 对照面——RNEA 不消费；空＝全缺省）----
    CanonicalDrivetrain drivetrain;
    for (double r : ratioPerJoint) {
        drivetrain.ratioPerJoint.push_back(val(r));  // 无量纲比值
    }

    // ---- 组装（校验＋规范化＋派生＋索引＋身份——build() 唯一入口）----
    return CanonicalModelBuilder()
        .setHeader(header)
        .setWorld(world)
        .setChain(chain)
        .setTools(tools)
        .setDefaultTcpIndex(defaultTcp)
        .setDrivetrain(drivetrain)
        .build();
}

/// 地面安装世界块（默认——R=I、g=(0,0,−9.81)、preset 缺省按 Ground 解释）。
inline WorldPlacement groundWorld() { return WorldPlacement{}; }

/// 倒挂安装世界块（MDL-22 预设 180°＝R_x(π)=diag(1,−1,−1)——P-RT-4 冻结
/// 轴向；安装高度 h 仅平移、不影响重力矩，单位 m）。
inline WorldPlacement invertedWorld(double h = 2.0)
{
    WorldPlacement w;
    // R_x(π) 逐元素写定（{0,±1} 编码无舍入——runtime §6.2 同口径）。
    w.T_world_base = rw::math::Transform3D<double>(
        rw::math::Vector3D<double>(0.0, 0.0, h),
        rw::math::Rotation3D<double>(1, 0, 0, 0, -1, 0, 0, 0, -1));
    return w;
}

}  // namespace sdurws::ird::dynamics::testfixture

#endif  // IRD_DYNAMICS_TEST_TWOLINKFIXTURE_HPP
