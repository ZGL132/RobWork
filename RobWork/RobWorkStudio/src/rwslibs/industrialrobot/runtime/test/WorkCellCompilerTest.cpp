/**
 * @file   WorkCellCompilerTest.cpp
 * @brief  RT-T07 测试——S6 WorkCell 编译器＋只读视图＋异常转译。
 *
 * 设计依据（用例↔需求/验收标准追溯）：
 *   - RT-AD-1（§8.4）：RobWork 异常→稳定诊断转译——真实基线异常（StateStructure
 *     重名帧 RW_THROW）喂入转译函数，断言 RT-WC-COMPILE-FAILED 与调用栈剥离；
 *     bad_alloc→RT-RESOURCE-BUDGET；未知标准异常→RT-ROBWORK-ERROR 事件码。
 *   - RT-AD-2（§8.2）：显式设值——逐关节对照 CanonicalModel vs WC 实际
 *     bounds/maxVelocity/maxAcceleration，无基线构造默认残留（基线默认
 *     bounds＝±DBL_MAX、限速/限加速度＝1，Joint.cpp 构造实测）。
 *   - RT-EQ-1（ARC-03/AT-16/附录 D 第 4 项）：canonical↔WC FK 等价——
 *     测试内手写独立 FK（仅测试对照工具，不构成产品 FK 证明——§11 RT-EQ-1
 *     行替身边界声明）vs RobWork Device FK，逐关节轴线/原点 ≤1×10⁻⁹ m/rad。
 *   - RT-BW-4（MDL-22/AT-37，S9 部分）：BaseMount 唯一写入读回一致＋
 *     ≈T·T 二次叠加反例检出＋场景/基座帧无安装分量（§6.4 禁止清单 3）。
 *   - RT-STUB-0（§11）：RobWork 本体不做替身——结构断言全部针对真实基线库
 *     构造的 WorkCell/SerialDevice；替身只覆盖注入接口（本文件无注入接口）。
 *
 * 工程事实（两模式差异，CMake 同步登记）：本文件链接真实框架库（Kinematics/
 * SerialDevice/StateStructure 为非模板外联符号），**只在集成模式编译**——
 * 冒烟模式不进测试目标（units/runtime.md §15.4 RT-T07 登记条目）。
 *
 * 确定性：夹具 id/摘要由固定种子派生（CanonicalModelFixture 同款）；FK 对照
 * 构型为固定数组（无随机）——全部断言跨进程可复现（NFR-COR-02）。
 */

#include "WorkCellCompiler.hpp"  // 被测：S6 编译器（单元私有头——测试与 src 同权）

#include <sdurws/ird/runtime/Adapter.hpp>              // WorkCellConstView/translateRobWorkError
#include <sdurws/ird/runtime/BaseWorldTransform.hpp>   // checkBaseMountConsistency（S9 规则面）
#include <sdurws/ird/runtime/Errors.hpp>               // token（稳定码断言）
#include <sdurws/ird/runtime/NameMap.hpp>              // buildRuntimeNameMap（期望名来源）

#include <rw/core/Ptr.hpp>
#include <rw/kinematics/FixedFrame.hpp>
#include <rw/kinematics/Kinematics.hpp>     // frameTframe/worldTframe（真实基线 FK）
#include <rw/kinematics/StateStructure.hpp> // RT-AD-1 真实异常触发点
#include <rw/math/Q.hpp>
#include <rw/models/Joint.hpp>              // WC 侧关节字段读取（RT-AD-2）
#include <rw/models/SerialDevice.hpp>
#include <rw/models/WorkCell.hpp>

#include "CanonicalModelFixture.hpp"        // minimal/rich 夹具（确定性 id）

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace {

using namespace sdurws::ird::runtime;
using sdurws::ird::runtime::testfixture::Fixture;
using sdurws::ird::runtime::testfixture::idFrom;
using sdurws::ird::runtime::testfixture::minimalFixture;
using sdurws::ird::runtime::testfixture::richFixture;
using sdurws::ird::runtime::testfixture::val;

namespace core = sdurws::ird::core;

/// 附录 D 第 4 项容差（FK/编译链等价验证——1×10⁻⁹ m / 1×10⁻⁹ rad，无单位
/// 量纲的旋转元素同尺度）；一致性检查与 S9 用同一档案。
constexpr double kFkTolerance = 1e-9;

// =====================================================================
// 夹具扩展（基于 CanonicalModelFixture——确定性 id 派生同款）。
// =====================================================================

/// 在夹具闭包中登记一个对象引用（CM-0 值层面——builder 校验引用∈清单）。
void addRef(Fixture& f, const core::ObjectId& id, const char* seed, const char* token)
{
    sdurws::ird::runtime::ObjectRefEntry e;
    e.objectId = id;
    e.contentVersion = testfixture::cvFrom(seed);
    e.objectTypeToken = token;
    e.digest = testfixture::digestOf(std::string{seed} + "-bytes");
    f.header.objectRefs.push_back(e);
}

/**
 * @brief 三关节研究夹具（RT-EQ-1 主输入）：覆盖权威模型的关键形态——
 *        非 Z 轴（j2 绕 Y、j3 绕 (1,0,1)/√2）、非零零位偏置（0.2/−0.35/0.7
 *        rad）、非恒等 origin 旋转（j1 绕 Z 转 90°）与逐关节限位/限速。
 *        全部字段 Provided——显式设值断言（RT-AD-2）无缺省歧义。
 */
Fixture threeJointFixture()
{
    Fixture f = minimalFixture();  // 2 关节＋3 连杆基底（闭包/资源/身份块齐备）

    const core::ObjectId j3 = idFrom<core::ObjectId>("j3");
    const core::ObjectId l3 = idFrom<core::ObjectId>("l3");
    addRef(f, j3, "j3", "joint");
    addRef(f, l3, "l3", "link");

    // j1：绕 Z、带 90° 静态旋转与 0.2 rad 零位偏置（对齐补偿路径＋偏置
    // 映射路径同时激活）。
    f.chain.joints.at(0).origin = rw::math::Transform3D<double>(
        rw::math::Vector3D<double>(0.1, 0.0, 0.2),
        rw::math::Rotation3D<double>(0.0, -1.0, 0.0,
                                     1.0, 0.0, 0.0,
                                     0.0, 0.0, 1.0));  // R_z(π/2)（逐元素：c=0,s=1）
    f.chain.joints.at(0).zeroOffset = 0.2;              // 单位 rad
    f.chain.joints.at(0).maxAcceleration = val(8.0);    // 单位 rad/s²

    // j2：绕 Y（非 Z 轴对齐路径）、−0.35 rad 偏置。
    f.chain.joints.at(1).zeroOffset = -0.35;            // 单位 rad
    f.chain.joints.at(1).maxAcceleration = val(6.0);

    // j3：绕对角轴 (1,0,1)/√2（一般 Rodrigues 分支）、0.7 rad 偏置。
    const double invSqrt2 = 0.70710678118654752440;     // 1/√2（双精度字面）
    CanonicalJoint j3d;
    j3d.objectId = j3;
    j3d.localName = "joint_3";
    j3d.type = JointType::Revolute;
    j3d.axis = rw::math::Vector3D<double>(invSqrt2, 0.0, invSqrt2);
    j3d.origin = rw::math::Transform3D<double>(
        rw::math::Vector3D<double>(0.05, 0.0, 0.25),
        detail::identityTransform3D().R());
    j3d.bounds = JointBounds{-1.5, 1.5};                // 单位 rad
    j3d.maxVelocity = val(3.0);                          // 单位 rad/s
    j3d.maxAcceleration = val(4.0);                      // 单位 rad/s²
    f.chain.joints.push_back(j3d);

    CanonicalLink link3;
    link3.objectId = l3;
    link3.localName = "link_3";
    f.chain.links.push_back(link3);  // 连杆数＝关节数＋1（builder 不变量）
    return f;
}

/**
 * @brief 倒挂＋场景夹具（RT-BW-4 主输入）：§6.5 数值例的倒挂安装
 *        （R_world_base＝R_x(π)＝diag(1,−1,−1)、t＝(0,0,2) m）＋一个世界系
 *        固连场景对象（验证环境几何不随安装姿态旋转——§6.4 禁止清单 3）。
 */
Fixture invertedWithSceneFixture()
{
    Fixture f = minimalFixture();

    // 倒挂安装（§6.2 P-RT-4 冻结矩阵 R_x(π)，逐元素写入——编码无舍入）。
    f.world.T_world_base = rw::math::Transform3D<double>(
        rw::math::Vector3D<double>(0.0, 0.0, 2.0),      // 吊装高度，单位 m
        rw::math::Rotation3D<double>(1.0, 0.0, 0.0,
                                     0.0, -1.0, 0.0,
                                     0.0, 0.0, -1.0));

    // 场景对象（世界系固连；几何引用命中 manifest[0]——res-1 已在夹具内）。
    const core::ObjectId s1 = idFrom<core::ObjectId>("s1");
    addRef(f, s1, "s1", "scene");
    CanonicalSceneObject obstacle;
    obstacle.objectId = s1;
    obstacle.localName = "obstacle_bw4";
    obstacle.worldPose = rw::math::Transform3D<double>(
        rw::math::Vector3D<double>(0.5, -0.4, 1.0),     // 世界系坐标，单位 m
        detail::identityTransform3D().R());
    obstacle.geometry = f.manifest.at(0);
    f.scene.push_back(obstacle);
    return f;
}

// =====================================================================
// canonical 侧手写 FK（RT-EQ-1 的测试对照工具——§11"替身 FK 是测试对照
// 工具，不构成产品 FK 算法证明"；按 §4.3.3 链语义逐级复合）。
// =====================================================================

/// 测试内独立 Rodrigues（绕单位轴 axis 转 angle——与产品
/// rotationFromCustomEaa 无共享代码，互证而非循环引用）。
rw::math::Rotation3D<double> canonicalRodrigues(const rw::math::Vector3D<double>& axis,
                                                double angle)
{
    const double c = std::cos(angle);                    // 单位 rad
    const double s = std::sin(angle);
    const double t = 1.0 - c;
    const double x = axis[0], y = axis[1], z = axis[2];
    return rw::math::Rotation3D<double>(
        c + x * x * t,      x * y * t - z * s,  x * z * t + y * s,
        y * x * t + z * s,  c + y * y * t,      y * z * t - x * s,
        z * x * t - y * s,  z * y * t + x * s,  c + z * z * t);
}

/// 位姿对（平移 m＋旋转）——canonical FK 的中间载体（读法 core §4.6）。
struct Pose
{
    rw::math::Vector3D<double> p{0.0, 0.0, 0.0};
    rw::math::Rotation3D<double> R = detail::identityTransform3D().R();
};

/// 位姿复合（T_ac＝T_ab·T_bc——逐元素独立实现，不用 rw operator*，
/// 保持对照实现的独立性）。
Pose composePose(const Pose& a, const Pose& b)
{
    Pose out;
    for (int r = 0; r < 3; ++r) {
        out.p[r] = a.p[r]
            + a.R(r, 0) * b.p[0] + a.R(r, 1) * b.p[1] + a.R(r, 2) * b.p[2];
        for (int cIdx = 0; cIdx < 3; ++cIdx) {
            out.R(r, cIdx) = a.R(r, 0) * b.R(0, cIdx) + a.R(r, 1) * b.R(1, cIdx)
                           + a.R(r, 2) * b.R(2, cIdx);
        }
    }
    return out;
}

/// 逐元素接近断言（旋转 9 元素＋平移 3 元素；容差＝附录 D 第 4 项档案）。
void expectTransformClose(const rw::math::Transform3D<double>& actual,
                          const rw::math::Rotation3D<double>& expectedR,
                          const rw::math::Vector3D<double>& expectedP,
                          const char* what)
{
    for (int r = 0; r < 3; ++r) {
        EXPECT_NEAR(actual.P()[r], expectedP[r], kFkTolerance)
            << what << "：平移分量 " << r << " 超差（第 4 项容差 1e-9 m）";
        for (int c = 0; c < 3; ++c) {
            EXPECT_NEAR(actual.R()(r, c), expectedR(r, c), kFkTolerance)
                << what << "：旋转元素 (" << r << "," << c << ") 超差（1e-9）";
        }
    }
}

}  // namespace

// =====================================================================
// RT-AD-2：显式设值（§8.2——逐字段对照，杜绝 RobWork 构造默认残留）。
// =====================================================================

/**
 * RT-AD-2 主断言：minimal 夹具（offset＝0）编译后逐关节对照——
 *   bounds 逐字等于权威（−2.97/2.97、−π/π）；maxVelocity＝2.5/2.0；
 *   maxAcceleration 未提供→WC 显式 +inf（★ 基线构造默认＝1〔Joint.cpp〕，
 *   +inf≠1＝设值路径执行的直接证据；±DBL_MAX 同理未出现）。
 */
TEST(WorkCellCompilerTest, JointFieldsExplicit_RT_AD_2)
{
    const CanonicalModel model = minimalFixture().build();
    const WorkCellCompileOutcome out = compileWorkCell(model);
    ASSERT_TRUE(!out.workCell.isNull());

    const RuntimeNameMap nameMap = buildRuntimeNameMap(model);
    ASSERT_EQ(model.chain().joints.size(), 2u);
    for (std::size_t i = 0; i < model.chain().joints.size(); ++i) {
        const CanonicalJoint& cj = model.chain().joints.at(i);
        const std::string frameName = nameMap.resolveObjectId(cj.objectId).get().fullName;
        const rw::kinematics::Frame* frame = out.workCell->findFrame(frameName);
        ASSERT_NE(frame, nullptr) << "关节帧缺失: " << frameName;
        const rw::models::Joint* joint = dynamic_cast<const rw::models::Joint*>(frame);
        ASSERT_NE(joint, nullptr) << frameName << " 应为基线 Joint（dof=1）";

        // bounds：offset＝0→WC 值逐字等于权威（Rad）。
        ASSERT_TRUE(cj.bounds.has_value());
        EXPECT_DOUBLE_EQ(joint->getBounds().first[0], cj.bounds->lower)
            << frameName << " qmin 与权威不符（RT-AD-2）";
        EXPECT_DOUBLE_EQ(joint->getBounds().second[0], cj.bounds->upper)
            << frameName << " qmax 与权威不符（RT-AD-2）";

        // maxVelocity：Provided→逐字相等。
        ASSERT_TRUE(cj.maxVelocity.tryValue().has_value());
        EXPECT_DOUBLE_EQ(joint->getMaxVelocity()[0], *cj.maxVelocity.tryValue())
            << frameName << " maxVelocity 与权威不符（RT-AD-2）";

        // maxAcceleration：夹具未提供→显式 +inf（基线默认 1 被覆写——若为 1
        // 或 ±DBL_MAX 即"RobWork 默认残留"，RT-AD-2 失败）。
        ASSERT_FALSE(cj.maxAcceleration.tryValue().has_value());
        EXPECT_EQ(joint->getMaxAcceleration()[0],
                  std::numeric_limits<double>::infinity())
            << frameName << " maxAcceleration 应显式 +inf（非基线默认 1/±DBL_MAX）";
    }
}

/** RT-AD-2 偏置映射：非零 zeroOffset 下 WC bounds＝权威区间平移 −offset
 *  （q_rw＝q_auth−offset——CanonicalJoint::zeroOffset 口径的数值验证）。 */
TEST(WorkCellCompilerTest, JointBoundsOffsetMapping_RT_AD_2)
{
    Fixture f = minimalFixture();
    f.chain.joints.at(0).zeroOffset = 0.5;  // 单位 rad（权威零位＝RobWork 0.5）
    const CanonicalModel model = f.build();
    const WorkCellCompileOutcome out = compileWorkCell(model);
    ASSERT_TRUE(!out.workCell.isNull());

    const RuntimeNameMap nameMap = buildRuntimeNameMap(model);
    const CanonicalJoint& cj = model.chain().joints.at(0);
    const rw::models::Joint* joint = dynamic_cast<const rw::models::Joint*>(
        out.workCell->findFrame(nameMap.resolveObjectId(cj.objectId).get().fullName));
    ASSERT_NE(joint, nullptr);
    // 权威 [−2.97, 2.97] − 0.5 → WC 侧 [−3.47, 2.47]（q_rw 域，rad）。
    EXPECT_NEAR(joint->getBounds().first[0], -3.47, 1e-12);
    EXPECT_NEAR(joint->getBounds().second[0], 2.47, 1e-12);
}

/** RT-AD-2 连续关节：规范侧无 bounds（MDL-12）→WC 显式 ±inf 限位（显式
 *  语义表达——与基线默认 ±DBL_MAX 可区分）。 */
TEST(WorkCellCompilerTest, ContinuousJointExplicitInfiniteBounds_RT_AD_2)
{
    Fixture f = minimalFixture();
    CanonicalJoint& j2 = f.chain.joints.at(1);
    j2.type = JointType::Continuous;
    j2.bounds = std::nullopt;               // 连续关节必无限位（builder 校验）
    const CanonicalModel model = f.build();
    const WorkCellCompileOutcome out = compileWorkCell(model);
    ASSERT_TRUE(!out.workCell.isNull());

    const RuntimeNameMap nameMap = buildRuntimeNameMap(model);
    const rw::models::Joint* joint = dynamic_cast<const rw::models::Joint*>(
        out.workCell->findFrame(nameMap.resolveObjectId(j2.objectId).get().fullName));
    ASSERT_NE(joint, nullptr);
    EXPECT_EQ(joint->getBounds().first[0], -std::numeric_limits<double>::infinity());
    EXPECT_EQ(joint->getBounds().second[0], std::numeric_limits<double>::infinity());
    // 限位缺省→限速仍按 Provided 显式（continuation of 显式设值纪律）。
    ASSERT_TRUE(j2.maxVelocity.tryValue().has_value());
    EXPECT_DOUBLE_EQ(joint->getMaxVelocity()[0], *j2.maxVelocity.tryValue());
}

/** RT-AD-2/结构面：固定关节（MDL-12 Fixed）在 WC 中是 dof＝0 的 FixedFrame
 *  ——不入设备活动关节集（设备活动关节 1 个）。 */
TEST(WorkCellCompilerTest, FixedJointIsFrameNotJoint_RT_T07)
{
    Fixture f = minimalFixture();
    f.chain.joints.at(1).type = JointType::Fixed;
    const CanonicalModel model = f.build();
    const WorkCellCompileOutcome out = compileWorkCell(model);
    ASSERT_TRUE(!out.workCell.isNull());

    const RuntimeNameMap nameMap = buildRuntimeNameMap(model);
    const rw::kinematics::Frame* fixedFrame =
        out.workCell->findFrame(nameMap.resolveObjectId(model.chain().joints.at(1).objectId).get().fullName);
    ASSERT_NE(fixedFrame, nullptr);
    EXPECT_EQ(dynamic_cast<const rw::models::Joint*>(fixedFrame), nullptr)
        << "固定关节不应产生基线 Joint（无 dof）";

    rw::core::Ptr<rw::models::SerialDevice> deviceMutable =
        out.workCell->findDevice<rw::models::SerialDevice>(out.deviceName);
    ASSERT_TRUE(!deviceMutable.isNull());
    ASSERT_EQ(deviceMutable->getJoints().size(), 1u) << "活动关节集应只含旋转关节 j1";
}

// =====================================================================
// RT-EQ-1：canonical↔WC FK 等价（附录 D 第 4 项容差 1×10⁻⁹ m/rad）。
// =====================================================================

/**
 * RT-EQ-1 主断言（三关节研究夹具 × 3 个固定构型）：测试内手写 canonical FK
 * （§4.3.3 链语义：F(link_i)＝F(link_{i-1})·T_parent_joint_i·R_axis(q_auth_i)，
 *  q_auth＝zeroOffset＋q_rw）vs 真实基线 Device FK（Kinematics::frameTframe）
 * ——逐关节轴线（rad 尺度夹角/向量差）与原点（m）≤1×10⁻⁹；法兰全位姿逐
 * 元素一致。
 */
TEST(WorkCellCompilerTest, FkEquivalence_RT_EQ_1)
{
    const CanonicalModel model = threeJointFixture().build();
    const WorkCellCompileOutcome out = compileWorkCell(model);
    ASSERT_TRUE(!out.workCell.isNull());

    const rw::core::Ptr<const rw::models::SerialDevice> device =
        out.workCell->findDevice<rw::models::SerialDevice>(out.deviceName);
    ASSERT_TRUE(!device.isNull());
    ASSERT_EQ(device->getJoints().size(), 3u);

    // 逐对象名（期望名唯一来源＝映射——与编译器同源）。
    const RuntimeNameMap nameMap = buildRuntimeNameMap(model);
    std::vector<const rw::kinematics::Frame*> jointFrames;
    for (const CanonicalJoint& cj : model.chain().joints) {
        jointFrames.push_back(
            out.workCell->findFrame(nameMap.resolveObjectId(cj.objectId).get().fullName));
        ASSERT_NE(jointFrames.back(), nullptr);
    }
    const rw::kinematics::Frame* const flangeFrame = device->getEnd();
    ASSERT_NE(flangeFrame, nullptr);

    // 固定构型组（q_rw，rad——确定性，无随机；覆盖正/负/跨零）。
    const std::vector<std::array<double, 3>> configs = {
        {0.0, 0.0, 0.0}, {0.3, -0.7, 1.1}, {-1.2, 0.9, 2.5}};

    for (const auto& qRw : configs) {
        // RobWork 侧：设备 q 写入线程私有 State（§8.7 State 纪律）。
        rw::kinematics::State state = out.workCell->getDefaultState();
        device->setQ(rw::math::Q(3, qRw[0], qRw[1], qRw[2]), state);

        // canonical 侧：从基座连杆系（BaseFrame，设备基座）逐级复合。
        Pose canonical{rw::math::Vector3D<double>(0.0, 0.0, 0.0),
                       detail::identityTransform3D().R()};
        for (std::size_t i = 0; i < model.chain().joints.size(); ++i) {
            const CanonicalJoint& cj = model.chain().joints.at(i);
            // 关节 i 的权威原点/轴（父连杆系表达——§4.3.3）。
            const Pose jointAnchor = composePose(canonical, Pose{cj.origin.P(), cj.origin.R()});
            // 权威旋转：绕权威轴转 q_auth＝zeroOffset＋q_rw（rad）。
            const rw::math::Rotation3D<double> rotAfter = canonicalRodrigues(
                cj.axis, cj.zeroOffset + qRw[i]);

            // ---- 原点对照（m；旋转面在轴线/连杆系断言中覆盖）----
            const rw::math::Transform3D<double> rwJointT =
                rw::kinematics::Kinematics::frameTframe(device->getBase(), jointFrames[i], state);
            for (int r = 0; r < 3; ++r) {
                ASSERT_NEAR(rwJointT.P()[r], jointAnchor.p[r], kFkTolerance)
                    << "构型[" << &qRw - configs.data() << "] 关节" << i
                    << " 原点分量 " << r << " 超差";
            }

            // ---- 轴线对照（基线关节 Z 列 vs 权威轴的父链投影）----
            // canonical 轴（基座系）＝R(F_link_{i-1})·T_i.R·axis（绕轴旋转
            // 不动轴自身——R_axis(q_auth)·axis＝axis）。
            const rw::math::Vector3D<double> canonicalAxis{
                jointAnchor.R(0, 0) * cj.axis[0] + jointAnchor.R(0, 1) * cj.axis[1]
                    + jointAnchor.R(0, 2) * cj.axis[2],
                jointAnchor.R(1, 0) * cj.axis[0] + jointAnchor.R(1, 1) * cj.axis[1]
                    + jointAnchor.R(1, 2) * cj.axis[2],
                jointAnchor.R(2, 0) * cj.axis[0] + jointAnchor.R(2, 1) * cj.axis[1]
                    + jointAnchor.R(2, 2) * cj.axis[2]};
            // 基线 Rotation3D 无 getColumn——以 (r,2) 索引取 Z 列（关节轴）。
            const rw::math::Vector3D<double> rwAxis{rwJointT.R()(0, 2),
                                                   rwJointT.R()(1, 2),
                                                   rwJointT.R()(2, 2)};
            for (int r = 0; r < 3; ++r) {
                ASSERT_NEAR(rwAxis[r], canonicalAxis[r], kFkTolerance)
                    << "构型[" << &qRw - configs.data() << "] 关节" << i
                    << " 轴线分量 " << r << " 超差（1e-9）";
            }

            // 推进 canonical 链：连杆 i+1 ＝ 关节旋转后系。
            canonical = composePose(jointAnchor, Pose{{0.0, 0.0, 0.0}, rotAfter});
        }

        // ---- 法兰全位姿对照（连杆系无残差——尾巴补偿的端到端证据）----
        const rw::math::Transform3D<double> rwFlangeT =
            rw::kinematics::Kinematics::frameTframe(device->getBase(), flangeFrame, state);
        expectTransformClose(rwFlangeT, canonical.R, canonical.p, "RT-EQ-1 法兰位姿");
    }
}

/** RT-EQ 系（§5.5 重复编译结构相等）：同输入两次编译——帧名集合/设备名/
 *  逐关节运动字段（1e-9）逐项相等（WC 无内容身份，结构相等即等价面）。 */
TEST(WorkCellCompilerTest, RepeatCompileStructuralEquality_RT_EQ)
{
    const CanonicalModel model = threeJointFixture().build();
    const WorkCellCompileOutcome a = compileWorkCell(model);
    const WorkCellCompileOutcome b = compileWorkCell(model);
    ASSERT_TRUE(!a.workCell.isNull());
    ASSERT_TRUE(!b.workCell.isNull());

    EXPECT_EQ(a.deviceName, b.deviceName);
    ASSERT_EQ(a.runtimeNames.size(), b.runtimeNames.size());
    for (std::size_t i = 0; i < a.runtimeNames.size(); ++i) {
        EXPECT_EQ(a.runtimeNames[i], b.runtimeNames[i]) << "帧名序不一致@" << i;
    }

    // 逐帧运动字段（帧集合同名同构——按名配对读取）。
    for (const std::string& name : a.runtimeNames) {
        const rw::kinematics::Frame* fa = a.workCell->findFrame(name);
        const rw::kinematics::Frame* fb = b.workCell->findFrame(name);
        ASSERT_NE(fa, nullptr);
        ASSERT_NE(fb, nullptr);
        const auto* ja = dynamic_cast<const rw::models::Joint*>(fa);
        const auto* jb = dynamic_cast<const rw::models::Joint*>(fb);
        if (ja != nullptr && jb != nullptr) {
            EXPECT_NEAR(ja->getBounds().first[0], jb->getBounds().first[0], kFkTolerance);
            EXPECT_NEAR(ja->getBounds().second[0], jb->getBounds().second[0], kFkTolerance);
            EXPECT_NEAR(ja->getMaxVelocity()[0], jb->getMaxVelocity()[0], kFkTolerance);
            EXPECT_NEAR(ja->getMaxAcceleration()[0], jb->getMaxAcceleration()[0], kFkTolerance);
        }
    }
}

// =====================================================================
// RT-BW-4（S9 部分）：BaseMount 唯一写入＋二次叠加反例（AT-37）。
// =====================================================================

/** RT-BW-4 前半：倒挂夹具（§6.5 数值例）编译后 BaseMount 读回值与权威
 *  T_world_base 逐元素一致（附录 D 第 4 项容差）——唯一写入点事后验证。 */
TEST(WorkCellCompilerTest, BaseMountReadback_RT_BW_4)
{
    const CanonicalModel model = invertedWithSceneFixture().build();
    const WorkCellCompileOutcome out = compileWorkCell(model);
    ASSERT_TRUE(!out.workCell.isNull());

    // BaseMount 是派生条目（共享 robot ObjectId）——经 (objectId, scope)
    // 定位取全名（与编译器同口径，不硬编码）。
    const RuntimeNameMap nameMap = buildRuntimeNameMap(model);
    std::string mountName;
    for (const RuntimeNameMap::Entry& e : nameMap.entries()) {
        if (e.scope == NameScope::BaseMount) { mountName = e.fullName; }
    }
    ASSERT_FALSE(mountName.empty());
    const rw::kinematics::Frame* mount = out.workCell->findFrame(mountName);
    ASSERT_NE(mount, nullptr);

    // 读回（默认状态）与权威逐元素比较——编译器内部 S9 自检已过，此处独立复核。
    expectTransformClose(mount->getTransform(out.workCell->getDefaultState()),
                         model.world().T_world_base.R(),
                         model.world().T_world_base.P(),
                         "RT-BW-4 BaseMount 读回");
}

/** RT-BW-4 后半（AT-37 反例）：二次叠加形态 T·T 必须被 S9 规则检出——
 *  倒挂 R_x(π) 自乘＝I（"渲染回正"假象），doubleAppliedPattern＝true。 */
TEST(WorkCellCompilerTest, DoubleAppliedDetected_RT_BW_4)
{
    const CanonicalModel model = invertedWithSceneFixture().build();
    const rw::math::Transform3D<double>& T = model.world().T_world_base;

    // 模拟缺陷消费方/编译段：把安装变换又乘了一次（R²＝I——旋转回正、
    // 平移叠乘）。用 rw operator* 构造候选（T·T）。
    const rw::math::Transform3D<double> doubleApplied = T * T;
    const std::optional<BaseMountDeviation> dev =
        checkBaseMountConsistency(doubleApplied, T);
    ASSERT_TRUE(dev.has_value()) << "二次叠加未被检出（AT-37 反例漏网）";
    EXPECT_TRUE(dev->doubleAppliedPattern)
        << "偏差应呈 ≈T·T 形态（倒挂 R_x(π)²＝I）";
}

/** RT-BW-4/禁止清单 3：世界系固连场景帧位姿＝权威 worldPose（不预乘安装
 *  旋转）；BaseFrame 相对 BaseMount＝恒等（安装分量只在 BaseMount 一处）。 */
TEST(WorkCellCompilerTest, SceneAndBaseFramesCarryNoInstallComponent_RT_BW_4)
{
    const CanonicalModel model = invertedWithSceneFixture().build();
    const WorkCellCompileOutcome out = compileWorkCell(model);
    ASSERT_TRUE(!out.workCell.isNull());
    const rw::kinematics::State state = out.workCell->getDefaultState();
    const rw::kinematics::Frame* world = out.workCell->getWorldFrame();

    // 场景帧：世界系位姿应逐元素等于权威 worldPose（倒挂下若被预乘 R_x(π)
    // 会在 z/旋转载明显差异——1e-9 内即证明未叠加）。
    const CanonicalSceneObject& scene = model.scene().front();
    const RuntimeNameMap nameMap = buildRuntimeNameMap(model);
    std::string sceneName;
    for (const RuntimeNameMap::Entry& e : nameMap.entries()) {
        if (e.objectId == scene.objectId && e.scope == NameScope::SceneObject) {
            sceneName = e.fullName;
        }
    }
    const rw::kinematics::Frame* sceneFrame = out.workCell->findFrame(sceneName);
    ASSERT_NE(sceneFrame, nullptr);
    const rw::math::Transform3D<double> sceneWorld =
        rw::kinematics::Kinematics::worldTframe(sceneFrame, state);
    expectTransformClose(sceneWorld, scene.worldPose.R(), scene.worldPose.P(),
                         "RT-BW-4 场景帧世界位姿（禁止清单 3）");

    // BaseFrame 相对 BaseMount＝恒等（§6.3"其他一切 Frame 变换……不含安装
    // 分量"的结构面）。
    std::string baseFrameName;
    for (const RuntimeNameMap::Entry& e : nameMap.entries()) {
        if (e.scope == NameScope::BaseFrame) { baseFrameName = e.fullName; }
    }
    const rw::kinematics::Frame* baseFrame = out.workCell->findFrame(baseFrameName);
    ASSERT_NE(baseFrame, nullptr);
    std::string mountName2;
    for (const RuntimeNameMap::Entry& e : nameMap.entries()) {
        if (e.scope == NameScope::BaseMount) { mountName2 = e.fullName; }
    }
    const rw::kinematics::Frame* mountFrame = out.workCell->findFrame(mountName2);
    ASSERT_NE(mountFrame, nullptr);
    const rw::math::Transform3D<double> mountTbase =
        rw::kinematics::Kinematics::frameTframe(mountFrame, baseFrame, state);
    expectTransformClose(mountTbase, detail::identityTransform3D().R(),
                         rw::math::Vector3D<double>(0.0, 0.0, 0.0),
                         "RT-BW-4 BaseFrame 无安装分量");
}

// =====================================================================
// RT-AD-1：RobWork 异常→稳定诊断转译（§8.4）。
// =====================================================================

/**
 * RT-AD-1 主断言：真实基线异常（StateStructure 拒绝重名帧——RW_THROW，
 * StateStructure.cpp"Frame name is not unique"）喂入转译函数——
 *   码＝RT-WC-COMPILE-FAILED；context 含 stage；cause 含消息正文；
 *   cause 不含文件/行与 Id 前缀（NFR-REL-05：用户诊断不含调用栈）。
 */
TEST(WorkCellCompilerTest, TranslateRealRobWorkException_RT_AD_1)
{
    // 真实基线触发：同一 StateStructure 挂两个同名 FixedFrame。
    rw::core::Ptr<rw::kinematics::StateStructure> tree =
        rw::core::ownedPtr(new rw::kinematics::StateStructure());
    tree->addFrame(rw::core::ownedPtr(
        new rw::kinematics::FixedFrame("dup_frame", detail::identityTransform3D())));
    bool caught = false;
    try {
        tree->addFrame(rw::core::ownedPtr(
            new rw::kinematics::FixedFrame("dup_frame", detail::identityTransform3D())));
        ADD_FAILURE() << "基线未拒绝重名帧（前提失效——RT-AD-1 触发点不成立）";
    } catch (const rw::core::Exception& ex) {
        caught = true;
        const core::DiagnosticRecord record =
            translateRobWorkError(ex, "S6", std::nullopt);
        // 稳定码（registryCode 冻结映射——S6 段）。
        EXPECT_EQ(record.code, "RT-WC-COMPILE-FAILED");
        // stage 进 context（§8.4 转译表"＋stage"）。
        EXPECT_NE(record.context.find("S6"), std::string::npos);
        // 消息正文保留（基线消息正文——可定位失败原因）。
        EXPECT_NE(record.cause.find("Frame name is not unique"), std::string::npos)
            << "cause 应含基线消息正文: " << record.cause;
        // 调用栈剥离：cause 不得含抛出点文件名/行号前缀与 Id 段。
        EXPECT_EQ(record.cause.find("StateStructure.cpp"), std::string::npos)
            << "cause 泄漏了抛出位置（NFR-REL-05）: " << record.cause;
        EXPECT_EQ(record.cause.find("Id["), std::string::npos)
            << "cause 泄漏了 Id 前缀: " << record.cause;
    }
    ASSERT_TRUE(caught) << "未捕获到真实 rw 异常";
}

/** RT-AD-1 分支：std::bad_alloc→RT-RESOURCE-BUDGET（§5.5 内存不足行）。 */
TEST(WorkCellCompilerTest, TranslateBadAlloc_RT_AD_1)
{
    try {
        throw std::bad_alloc();
    } catch (const std::exception& ex) {
        const core::DiagnosticRecord record = translateRobWorkError(ex, "S6", std::nullopt);
        EXPECT_EQ(record.code, "RT-RESOURCE-BUDGET");
        EXPECT_NE(record.cause.find("内存不足"), std::string::npos);
    }
}

/** RT-AD-1 分支：非 rw 的标准异常→RT-ROBWORK-ERROR 事件码（Errors.hpp
 *  registryCode 注释口径——事件码产出归转译点）。 */
TEST(WorkCellCompilerTest, TranslateUnknownStdException_RT_AD_1)
{
    try {
        throw std::runtime_error("baseline-adapter-sentinel");
    } catch (const std::exception& ex) {
        const core::DiagnosticRecord record = translateRobWorkError(ex, "S6", std::nullopt);
        EXPECT_EQ(record.code, "RT-ROBWORK-ERROR");
        EXPECT_NE(record.cause.find("baseline-adapter-sentinel"), std::string::npos);
    }
}

// =====================================================================
// WorkCellConstView（§8.3 只读包装——RT-T07 交付面）。
// =====================================================================

/** 视图查询面：findFrame 命中/未命中（nullptr 不默认命中）/frameCount/
 *  findDevice 命中与未命中/defaultState 值拷贝独立性。 */
TEST(WorkCellConstViewTest, ViewQueries_RT_T07)
{
    const CanonicalModel model = minimalFixture().build();
    const WorkCellCompileOutcome out = compileWorkCell(model);
    ASSERT_TRUE(!out.workCell.isNull());

    const WorkCellConstView view(out.workCell);
    EXPECT_EQ(&view.workCell(), out.workCell.get());

    // 帧查询：命中（设备帧名逐字节）／未命中 nullptr（RT-NM-3 不默认命中）。
    EXPECT_NE(view.findFrame("IRB_T.joint_1"), nullptr);
    EXPECT_EQ(view.findFrame("IRB_T.joint_9"), nullptr);
    EXPECT_EQ(view.findFrame(""), nullptr);

    // 规模：WORLD＋BaseMount＋Base＋2 关节帧＋2 连杆帧（Flange 计入连杆）。
    EXPECT_EQ(view.frameCount(), 7u);

    // 设备查询：按映射设备名命中／未知名空 Ptr。
    const rw::core::Ptr<const rw::models::SerialDevice> device = view.findDevice("IRB_T");
    ASSERT_TRUE(!device.isNull());
    EXPECT_EQ(device->getJoints().size(), 2u);
    EXPECT_TRUE(view.findDevice("no_such_device").isNull());

    // defaultState：值拷贝且相互独立（State 是每线程可变工作区——改其一
    // 不影响另一；基线 State 无 operator==，独立性经设备 q 行为断言）。
    rw::core::Ptr<rw::models::SerialDevice> dev =
        out.workCell->findDevice<rw::models::SerialDevice>("IRB_T");
    ASSERT_TRUE(!dev.isNull());
    rw::kinematics::State s1 = view.defaultState();
    const rw::kinematics::State s2 = view.defaultState();
    dev->setQ(rw::math::Q(2, 1.0, 0.5), s1);  // 只改 s1（单位 rad）
    EXPECT_NE(dev->getQ(s1)[0], dev->getQ(s2)[0])
        << "修改 s1 不应影响独立的 s2（defaultState 值拷贝语义）";
}

/** 空句柄拒绝：WorkCellConstView 构造收到空 Ptr→RuntimeError（RobWorkError，
 *  runtime/robwork-null-handle——§8.4 空句柄行；fail-fast 不产空视图）。 */
TEST(WorkCellConstViewTest, NullHandleRejected_RT_T07)
{
    // 具名空句柄（防 most-vexing-parse——临时对象括号形态会被解析为函数
    // 声明，构造根本不发生）。
    rw::core::Ptr<const rw::models::WorkCell> emptyHandle{};
    try {
        WorkCellConstView view(emptyHandle);
        ADD_FAILURE() << "空句柄构造应 fail-fast";
    } catch (const RuntimeError& e) {
        EXPECT_EQ(e.code(), RuntimeErrorCode::RobWorkError);
        EXPECT_NE(std::string(e.what()).find("runtime/robwork-null-handle"),
                  std::string::npos);
    }
}

// =====================================================================
// 编译入口的结构断言（§12 RT-T07——Frame 树/Device 名唯一写入的产物形态）。
// =====================================================================

/** 烟囱＋命名格式：minimal 编译产物帧集合与映射逐一相等（§7.2——编译器
 *  写入名＝映射输出名；含派生名 IRB_T.BaseMount/Base/Flange 格式锁定）。 */
TEST(WorkCellCompilerTest, FrameNamesMatchNameMap_RT_T07)
{
    const CanonicalModel model = minimalFixture().build();
    const WorkCellCompileOutcome out = compileWorkCell(model);
    ASSERT_TRUE(!out.workCell.isNull());
    EXPECT_EQ(out.deviceName, "IRB_T");  // 映射 Device 条目＝机器人局部名（无前缀）

    // 设备名可直接 findDevice 命中（基线侧同名注册）。
    EXPECT_TRUE(!out.workCell->findDevice("IRB_T").isNull());

    // runtimeNames 覆盖全部写入名且逐名可在 WC 命中（actualNames ⊆ WC 实际——
    // S8 交叉校验的反向自证：编译器声称写的每个名字都真实存在）。
    ASSERT_FALSE(out.runtimeNames.empty());
    for (const std::string& name : out.runtimeNames) {
        EXPECT_NE(out.workCell->findFrame(name), nullptr)
            << "runtimeNames 中的名未落到 WC: " << name;
    }
    // 派生名格式（§7.1 范围表示例形态）。
    EXPECT_NE(out.workCell->findFrame("IRB_T.BaseMount"), nullptr);
    EXPECT_NE(out.workCell->findFrame("IRB_T.Base"), nullptr);
    EXPECT_NE(out.workCell->findFrame("IRB_T.Flange"), nullptr);
    EXPECT_NE(out.workCell->findFrame("IRB_T.joint_1"), nullptr);
    EXPECT_NE(out.workCell->findFrame("IRB_T.link_1"), nullptr);
}

/** rich 夹具（全能力位：工具＋场景）编译烟囱——Tcp 帧挂法兰、场景帧挂
 *  WORLD（结构可达性经 FK 查询验证），产物完整返回。 */
TEST(WorkCellCompilerTest, RichFixtureCompilesWithToolAndScene_RT_T07)
{
    const CanonicalModel model = richFixture().build();
    const WorkCellCompileOutcome out = compileWorkCell(model);
    ASSERT_TRUE(!out.workCell.isNull());

    const RuntimeNameMap nameMap = buildRuntimeNameMap(model);
    // Tcp 帧（工具局部名作局部名——NameMap.cpp Tcp 行）在 WC 命中。
    std::string tcpName;
    for (const RuntimeNameMap::Entry& e : nameMap.entries()) {
        if (e.scope == NameScope::Tcp) { tcpName = e.fullName; }
    }
    ASSERT_FALSE(tcpName.empty());
    EXPECT_NE(out.workCell->findFrame(tcpName), nullptr);
    // 设备可查且端帧＝Flange。
    const rw::core::Ptr<const rw::models::SerialDevice> device =
        out.workCell->findDevice<rw::models::SerialDevice>(out.deviceName);
    ASSERT_TRUE(!device.isNull());
    EXPECT_STREQ(device->getEnd()->getName().c_str(), "IRB_T.Flange");
}
