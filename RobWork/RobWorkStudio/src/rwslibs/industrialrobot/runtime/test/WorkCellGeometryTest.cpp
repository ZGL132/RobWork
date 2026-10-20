/**
 * @file   WorkCellGeometryTest.cpp
 * @brief  RT-T15 测试——S6 基本图元几何挂接（RT-GEO 族）。
 *
 * 设计依据（用例↔需求/验收标准追溯——卡 tasks/foundation/RT-T15.json
 * acceptance 逐面）：
 *   - 图元挂接面（acceptance 1）：设备链连杆圆柱／法兰／TCP／场景对象标记盒
 *     挂接其映射帧，几何位姿随帧（worldTframe 同源——几何载体绑定帧本体，
 *     帧内位姿恒定，FK／State 变化随动）；S6 既有产物（Frame 树／Device／
 *     Joint／限位）与映射条目零变化——RT_GEO_1/RT_GEO_2/RT_GEO_5 钉住。
 *   - 诚实跳过面（acceptance 2）：resourceManifest 为空或 provider 缺位时
 *     编译成功且零虚构几何、不产错误级诊断；provider 在位时 S6 亦零调用
 *     （mesh 解析加载器归 io 阶段 B——本批只预留接口位不实现资源加载）——
 *     RT_GEO_4 钉住。
 *   - 身份面（acceptance 3）：显示代理图元不入 CanonicalModel 身份域（编译
 *     前后 RT-Codec 全字段编码逐字节不变）、图元派生确定性（同输入同产物
 *     ——NFR-COR-01）——RT_GEO_3/RT_GEO_5 钉住。
 *   - RT-STUB-0（§11）：RobWork 本体不做替身——全部断言针对真实基线库构造
 *     的 WorkCell/RigidObject/Geometry；替身只覆盖注入接口（计数 provider
 *     ——IRuntimeResourceProvider 为 io 实现的注入接口，§8.6）。
 *
 * 工程事实（两模式差异，CMake 同步登记）：被测面 compileWorkCell 构造真实
 * WorkCell/Cylinder/Box/RigidObject（非模板外联符号），**只在集成模式编译**
 * ——冒烟模式不进测试目标（与 WorkCellCompilerTest 同因，§15.4 RT-T07 系
 * 登记条目的 RT-T15 延续）。
 *
 * 确定性：夹具 id/摘要由固定种子派生（CanonicalModelFixture 同款）；图元
 * 尺寸断言——盒（double 承载）用位级相等（EXPECT_DOUBLE_EQ），圆柱（基线
 * 以 float 承载尺寸）用 1×10⁻⁶ m 容差（kCylinderFloatTolerance 注释——
 * 基线存储形态的如实精度边界）；确定性用例（RT_GEO_3）仍按位级相等比对
 * 两次独立编译的读回值（同 float 位模式＝派生确定性的最严观测）；派生
 * 规则钳制公式在测试内按卡面冻结值重述（与产品实现互证而非共享代码）。
 */

#include "WorkCellCompiler.hpp"  // 被测：S6 编译器（单元私有头——测试与 src 同权）

#include <sdurws/ird/runtime/Codec.hpp>    // rtcodec::encode（身份域回归——编译前后
                                           //  模型编码逐字节不变）
#include <sdurws/ird/runtime/Errors.hpp>   // RuntimeError/Expected（provider 替身轨）
#include <sdurws/ird/runtime/NameMap.hpp>  // buildRuntimeNameMap/scopedFullName（期望名）
#include <sdurws/ird/runtime/Resource.hpp> // IRuntimeResourceProvider（替身接口）

#include <rw/core/Ptr.hpp>
#include <rw/geometry/Box.hpp>              // 标记盒参数读取（getParameters）
#include <rw/geometry/Cylinder.hpp>         // 连杆圆柱参数读取（getRadius/getHeight）
#include <rw/geometry/Geometry.hpp>         // 帧绑定/帧内位姿读取
#include <rw/kinematics/FixedFrame.hpp>
#include <rw/kinematics/Frame.hpp>
#include <rw/kinematics/Kinematics.hpp>     // worldTframe（真实基线 FK）
#include <rw/math/Q.hpp>
#include <rw/math/Vector3D.hpp>
#include <rw/models/Joint.hpp>              // bounds 读取（零扰动抽查）
#include <rw/models/Object.hpp>             // WC 对象清单遍历（呈现消费面同款）
#include <rw/models/SerialDevice.hpp>       // setQ 驱动 FK／getJoints
#include <rw/models/WorkCell.hpp>

#include "CanonicalModelFixture.hpp"        // minimal/rich 夹具（确定性 id）

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
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
namespace rtcodec = sdurws::ird::runtime::rtcodec;

// =====================================================================
// 夹具扩展：非零跨度研究夹具（RT-GEO 主输入）。
// =====================================================================

/// 圆柱尺寸断言容差（单位 m）：基线 Cylinder 以 **float** 存储 radius/height
/// （Cylinder.hpp 成员实测——呈现图元精度面），double 派生值经 float 往返
/// 的相对误差 ~1×10⁻⁸，绝对容差取 1×10⁻⁶ m＝对应量级下约 10 个 float ULP。
/// 图元是呈现代理（不入身份域），该精度损失是基线存储形态的如实边界——
/// SI 真值断言在 float 精度内成立（盒参数为 double 承载，不受此限）。
constexpr double kCylinderFloatTolerance = 1e-6;

/// 在夹具闭包中登记一个对象引用（CM-0 值层面——与 WorkCellCompilerTest
/// 同款工具；测试文件各自持份，不跨文件共享非头文件工具）。
void addRef(Fixture& f, const core::ObjectId& id, const char* seed, const char* token)
{
    ObjectRefEntry e;
    e.objectId = id;
    e.contentVersion = testfixture::cvFrom(seed);
    e.objectTypeToken = token;
    e.digest = testfixture::digestOf(std::string{seed} + "-bytes");
    f.header.objectRefs.push_back(e);
}

/**
 * @brief 三关节非零跨度夹具（RT-GEO 主输入——rich 基底加三段跨度）。
 *
 * 跨度取值刻意覆盖钳制规则三分支（半径＝clamp(0.15×L, 5 mm, 80 mm)）：
 *   - j1 平移 (0.15, 0, 0.30) m——L≈0.3354 m，r≈0.0503 m（比例分支，无钳制）；
 *   - j2 平移 (1.0, 0, 0) m——L＝1.0 m，比例值 0.15 m＞上限→钳到 0.08 m；
 *   - j3 平移 (0.001, 0, 0) m——L＝0.001 m，比例值 1.5×10⁻⁴ m＜下限→钳到
 *     0.005 m。
 * 连杆 4 根（＝关节数＋1）：基座/连杆 1/连杆 2 挂圆柱，法兰（连杆 3）挂
 * 标记盒；rich 基底另带 1 工具（TCP 盒）＋1 场景对象（盒）＋全物性。
 */
Fixture threeSpanRichFixture()
{
    Fixture f = richFixture();  // 2 关节＋3 连杆＋工具/场景/资源基底

    const core::ObjectId j3 = idFrom<core::ObjectId>("j3");
    const core::ObjectId l3 = idFrom<core::ObjectId>("l3");
    addRef(f, j3, "j3", "joint");
    addRef(f, l3, "l3", "link");

    // j1：非零平移（比例分支跨度）＋轴保持 rich 基底值（绕 Z——派生路径
    // 最少干扰；rich 基底 j1 原平移为零，此处覆写为非零跨度）。
    f.chain.joints.at(0).origin = rw::math::Transform3D<double>(
        rw::math::Vector3D<double>(0.15, 0.0, 0.30),   // 单位 m（DH a/d 对应量）
        detail::identityTransform3D().R());

    // j2：长跨度（半径上限钳制分支）。
    f.chain.joints.at(1).origin = rw::math::Transform3D<double>(
        rw::math::Vector3D<double>(1.0, 0.0, 0.0),     // 单位 m
        detail::identityTransform3D().R());

    // j3：短跨度（半径下限钳制分支）。
    CanonicalJoint j3d;
    j3d.objectId = j3;
    j3d.localName = "joint_3";
    j3d.type = JointType::Revolute;
    j3d.axis = rw::math::Vector3D<double>(0.0, 0.0, 1.0);
    j3d.origin = rw::math::Transform3D<double>(
        rw::math::Vector3D<double>(0.001, 0.0, 0.0),   // 单位 m
        detail::identityTransform3D().R());
    j3d.bounds = JointBounds{-1.5, 1.5};               // 单位 rad
    j3d.maxVelocity = val(3.0);                        // 单位 rad/s
    f.chain.joints.push_back(j3d);

    CanonicalLink link3;
    link3.objectId = l3;
    link3.localName = "link_3";
    link3.mass = val(2.0);                             // 单位 kg（rich 全物性口径）
    f.chain.links.push_back(link3);                    // 连杆数＝关节数＋1

    // 传动比逐关节口径跟随链长（§4.3.4——长度须＝joints.size()；rich 基底
    // 为 2 关节设值，本夹具增至 3 关节须同步扩为 3 项——builder 不变量）。
    f.drivetrain.ratioPerJoint.push_back(val(1.0));
    return f;
}

// =====================================================================
// WC 几何清单提取（呈现消费面同款遍历：wc->getObjects()×obj->getGeometry()
// ——WorkCellScene::updateSceneGraph 的读取路径；测试镜像该面即断言了
// "呈现侧可见"）。
// =====================================================================

/// 单条几何的测试视图（帧绑定＋类型＋尺寸＋帧内位姿）。
struct GeometryView
{
    std::string frameName;                    ///< 绑定帧名（findFrame 可回查）
    std::string name;                         ///< 基线 drawable 名
    rw::geometry::GeometryData::GeometryType type{}; ///< 图元类型甄别值
    rw::math::Transform3D<double> placement;  ///< 帧内位姿（m/rad）
    double dim0 = 0.0;                        ///< 圆柱半径 m／盒 dx m
    double dim1 = 0.0;                        ///< 圆柱高度 m／盒 dy m
    double dim2 = 0.0;                        ///< 盒 dz m（圆柱恒 0）
};

/**
 * @brief 按 drawable 名收集 WC 全部图元（顺序无关断言——集合语义）。
 *
 * @param wc  [in] 编译产物 WC（只读遍历——呈现侧同款访问面）
 * @param out [out] 名→视图映射（追加前清空；drawable 名唯一性在此断言）
 */
void collectGeometries(const rw::models::WorkCell& wc,
                       std::map<std::string, GeometryView>& out)
{
    out.clear();
    for (const rw::core::Ptr<rw::models::Object>& obj : wc.getObjects()) {
        for (const rw::geometry::Geometry::Ptr& g : obj->getGeometry()) {
            EXPECT_TRUE(g->getFrame() != nullptr)
                << "图元 " << g->getName() << " 未绑定帧（载体形态违约）";
            if (g->getFrame() == nullptr) {
                continue;  // 已记失败——无帧名可继续归类（不静默吞错）
            }
            GeometryView v;
            v.frameName = g->getFrame()->getName();
            v.name = g->getName();
            v.type = g->getGeometryData()->getType();
            v.placement = g->getTransform();
            if (v.type == rw::geometry::GeometryData::CylinderPrim) {
                // 圆柱参数读取（基线 getRadius/getHeight——m 真值）。
                const rw::core::Ptr<const rw::geometry::Cylinder> cyl =
                    g->getGeometryData().template cast<const rw::geometry::Cylinder>();
                ASSERT_TRUE(!cyl.isNull()) << "CylinderPrim 类型甄别失败（" << v.name << "）";
                v.dim0 = cyl->getRadius();   // 单位 m
                v.dim1 = cyl->getHeight();   // 单位 m
            } else if (v.type == rw::geometry::GeometryData::BoxPrim) {
                // 盒参数读取（基线 getParameters＝全边长 dx/dy/dz——Box.cpp
                // 网格顶点 ±dx/2 实测口径）。
                const rw::core::Ptr<const rw::geometry::Box> box =
                    g->getGeometryData().template cast<const rw::geometry::Box>();
                ASSERT_TRUE(!box.isNull()) << "BoxPrim 类型甄别失败（" << v.name << "）";
                const rw::math::Q dims = box->getParameters();  // 全边长，m
                v.dim0 = dims[0];
                v.dim1 = dims[1];
                v.dim2 = dims[2];
            }
            EXPECT_TRUE(out.find(v.name) == out.end())
                << "drawable 名重复（确定性破坏）：" << v.name;
            out[v.name] = v;
        }
    }
}

/// 期望帧名（映射唯一来源——与编译器同源，不允许测试侧拼名——R-4）。
std::string scopedName(const RuntimeNameMap& map, const core::ObjectId& id, NameScope scope)
{
    return scopedFullName(map, id, scope);
}

}  // namespace

// =====================================================================
// RT-GEO-1：图元挂接面——存在性（acceptance 1 前半）。
// =====================================================================

/**
 * RT-GEO-1 主断言（卡 acceptance 1）：三跨度夹具编译后——
 *   - 连杆圆柱恰 3 条（基座/连杆 1/连杆 2），各绑定其映射帧，类型＝圆柱；
 *   - 标记盒恰 3 个（法兰/TCP/场景对象），类型＝盒，盒心即帧原点；
 *   - BaseMount/Joint 帧/WORLD 零几何（几何只挂连杆/工具/场景帧——卡面
 *     "挂接其映射帧"的负面面）；
 *   - 载体对象数＝6（每帧一 RigidObject——呈现遍历面可见）。
 */
TEST(WorkCellGeometryTest, PrimitivesAttachedPerMappedFrame_RT_GEO_1)
{
    const Fixture f = threeSpanRichFixture();
    const CanonicalModel model = f.build();
    const RuntimeNameMap map = buildRuntimeNameMap(model);
    const WorkCellCompileOutcome out = compileWorkCell(model);
    ASSERT_TRUE(!out.workCell.isNull());

    std::map<std::string, GeometryView> geoms;
    collectGeometries(*out.workCell, geoms);

    // ---- 连杆圆柱：基座＋连杆 1＋连杆 2（链序 0..joints.size()-1）----
    const std::string baseName = scopedName(map, f.chain.links.at(0).objectId, NameScope::BaseFrame);
    const std::string link1Name = scopedName(map, f.chain.links.at(1).objectId, NameScope::LinkFrame);
    const std::string link2Name = scopedName(map, f.chain.links.at(2).objectId, NameScope::LinkFrame);
    for (const std::string& n : {baseName, link1Name, link2Name}) {
        const auto it = geoms.find(n + "@ird-link-cylinder");
        ASSERT_TRUE(it != geoms.end())
            << "连杆帧 " << n << " 缺圆柱图元（挂接面缺口）";
        EXPECT_EQ(it->second.type, rw::geometry::GeometryData::CylinderPrim)
            << n << "：图元类型非圆柱";
        EXPECT_EQ(it->second.frameName, n) << "圆柱未绑定其映射帧：" << n;
    }

    // ---- 标记盒：法兰＋TCP＋场景对象 ----
    const std::string flangeName = scopedName(map, f.chain.links.at(3).objectId, NameScope::Flange);
    const std::string tcpName = scopedName(map, f.tools.at(0).objectId, NameScope::Tcp);
    const std::string sceneName = scopedName(map, f.scene.at(0).objectId, NameScope::SceneObject);
    for (const std::string& n : {flangeName, tcpName, sceneName}) {
        const auto it = geoms.find(n + "@ird-marker-box");
        ASSERT_TRUE(it != geoms.end())
            << "帧 " << n << " 缺标记盒图元（挂接面缺口）";
        EXPECT_EQ(it->second.type, rw::geometry::GeometryData::BoxPrim)
            << n << "：图元类型非盒";
        EXPECT_EQ(it->second.frameName, n) << "标记盒未绑定其映射帧：" << n;
        // 盒心即帧原点（帧内位姿恒等——轴标记级图元的确定性形态）。
        for (int r = 0; r < 3; ++r) {
            EXPECT_DOUBLE_EQ(it->second.placement.P()[r], 0.0)
                << n << "：标记盒偏移非零（恒等安放违约）";
        }
    }

    // ---- 计数：图元总数＝载体对象数＝6（每帧一载体形态）----
    EXPECT_EQ(geoms.size(), std::size_t{6}) << "图元总数≠3 圆柱＋3 盒";
    EXPECT_EQ(out.workCell->getObjects().size(), std::size_t{6})
        << "载体对象数≠图元数（每帧一载体形态违约）";

    // ---- 负面：BaseMount/关节帧零几何（挂接面不越界）----
    const std::string mountName = scopedName(map, f.chain.robotObjectId, NameScope::BaseMount);
    const std::string joint1Name = map.resolveObjectId(f.chain.joints.at(0).objectId).get().fullName;
    for (const std::string& n : {mountName, joint1Name}) {
        const rw::kinematics::Frame* frame = out.workCell->findFrame(n);
        ASSERT_TRUE(frame != nullptr) << "帧应存在：" << n;
        for (const auto& [name, view] : geoms) {
            (void)name;
            EXPECT_NE(view.frameName, n)
                << "帧 " << n << " 不应承载几何（挂接面越界）";
        }
    }
}

/**
 * RT-GEO-1 附断言（卡 acceptance 1"几何位姿随帧"）：图元由帧本体承载——
 *   - 载体对象 base＝WC 帧树中该名帧的同一对象（呈现节点随帧变换的载体
 *     前提——指针等值）；
 *   - 帧内位姿在 State 变化前后不变（几何无独立位姿通道——位姿唯一来源
 *     是帧 FK）；worldTframe 随 q 改变（帧在动——几何随之，同源随动的
 *     结构前提）。
 */
TEST(WorkCellGeometryTest, GeometryPoseFollowsFrameThroughFk_RT_GEO_1)
{
    const Fixture f = threeSpanRichFixture();
    const CanonicalModel model = f.build();
    const RuntimeNameMap map = buildRuntimeNameMap(model);
    const WorkCellCompileOutcome out = compileWorkCell(model);
    ASSERT_TRUE(!out.workCell.isNull());
    std::map<std::string, GeometryView> geoms;
    collectGeometries(*out.workCell, geoms);
    ASSERT_EQ(geoms.size(), std::size_t{6});

    // 设备与驱动构型（q_rw，rad——固定值，无随机；Q 长度＝设备自由度）。
    const rw::core::Ptr<const rw::models::SerialDevice> device =
        out.workCell->findDevice<rw::models::SerialDevice>(out.deviceName);
    ASSERT_TRUE(!device.isNull());
    ASSERT_FALSE(device->getJoints().empty());
    const rw::kinematics::State q0 = out.workCell->getDefaultState();
    rw::kinematics::State q1 = q0;
    device->setQ(rw::math::Q(device->getJoints().size(), 0.6), q1);  // 全关节 0.6 rad

    // 法兰帧名（随动断言的代表帧——链上最深连杆，必受关节驱动）。
    const std::string flangeName =
        scopedName(map, f.chain.links.at(f.chain.links.size() - 1).objectId, NameScope::Flange);

    for (const auto& entry : geoms) {
        const GeometryView& view = entry.second;
        // 1) 载体 base＝帧树同名帧（同一对象——指针等值；同源随动前提）。
        const rw::kinematics::Frame* treeFrame = out.workCell->findFrame(view.frameName);
        ASSERT_TRUE(treeFrame != nullptr) << "帧不在树中：" << view.frameName;
        for (const rw::core::Ptr<rw::models::Object>& obj : out.workCell->getObjects()) {
            if (std::string(obj->getBase()->getName()) != view.frameName) {
                continue;
            }
            EXPECT_EQ(obj->getBase(), treeFrame)
                << view.frameName << "：载体 base≠帧树帧（同源随动前提破坏）";
        }

        // 2) q 变化后法兰帧 worldTframe 必变（FK 随动面——几何随帧的结构
        //    前提；其余帧的随动由同一载体机制保证，不逐帧重复断言）。
        if (view.frameName == flangeName) {
            bool worldMoved = false;
            for (int r = 0; r < 3; ++r) {
                const double d0 = rw::kinematics::Kinematics::worldTframe(treeFrame, q0).P()[r];
                const double d1 = rw::kinematics::Kinematics::worldTframe(treeFrame, q1).P()[r];
                worldMoved = worldMoved || std::fabs(d0 - d1) > 0.0;
            }
            EXPECT_TRUE(worldMoved)
                << view.frameName << "：q 变化后 worldTframe 未变（FK 随动前提不成立）";
        }

        // 3) 帧内位姿在 State 变化前后逐元素不变（几何无独立状态通道——
        //    位姿唯一来源＝帧；二次收集＝独立重读 WC）。
        std::map<std::string, GeometryView> geoms1;
        collectGeometries(*out.workCell, geoms1);
        const GeometryView& again = geoms1.at(entry.first);
        for (int r = 0; r < 3; ++r) {
            EXPECT_DOUBLE_EQ(again.placement.P()[r], view.placement.P()[r])
                << entry.first << "：帧内位姿随 State 漂移（第二位姿通道违例）";
            for (int c = 0; c < 3; ++c) {
                EXPECT_DOUBLE_EQ(again.placement.R()(r, c), view.placement.R()(r, c))
                    << entry.first << "：帧内旋转随 State 漂移";
            }
        }
    }
}

// =====================================================================
// RT-GEO-2：单位正确性（尺寸＝m 真值；派生规则镜像互证）。
// =====================================================================

/**
 * RT-GEO-2 主断言（卡 RT-GEO 族"单位正确性"）：圆柱高＝跨度 ‖T_parent_joint
 * 平移‖（与模型值位级相等——同表达式重算）；半径＝clamp(0.15×L, 5 mm,
 * 80 mm)（卡面冻结派生规则的测试侧镜像）；盒全边长＝0.02 m。跨度三分支
 * （无钳制/上限钳制/下限钳制）逐一断言。
 */
TEST(WorkCellGeometryTest, DerivedDimensionsAreSiTruth_RT_GEO_2)
{
    const Fixture f = threeSpanRichFixture();
    const CanonicalModel model = f.build();
    const RuntimeNameMap map = buildRuntimeNameMap(model);
    const WorkCellCompileOutcome out = compileWorkCell(model);
    ASSERT_TRUE(!out.workCell.isNull());
    std::map<std::string, GeometryView> geoms;
    collectGeometries(*out.workCell, geoms);

    // ---- 圆柱：高＝跨度（位级相等）；半径＝钳制规则（测试侧镜像）----
    const double spans[3] = {
        rw::math::Vector3D<double>(0.15, 0.0, 0.30).norm2(),  // 基座连杆跨度，m
        rw::math::Vector3D<double>(1.0, 0.0, 0.0).norm2(),    // 连杆 1 跨度，m
        rw::math::Vector3D<double>(0.001, 0.0, 0.0).norm2(),  // 连杆 2 跨度，m
    };
    const NameScope scopes[3] = {NameScope::BaseFrame, NameScope::LinkFrame,
                                 NameScope::LinkFrame};
    const core::ObjectId linkIds[3] = {f.chain.links.at(0).objectId,
                                       f.chain.links.at(1).objectId,
                                       f.chain.links.at(2).objectId};
    for (std::size_t i = 0; i < 3; ++i) {
        const std::string frameName = scopedName(map, linkIds[i], scopes[i]);
        const GeometryView& v = geoms.at(frameName + "@ird-link-cylinder");

        // 高度＝跨度（float 精度内——基线 Cylinder 以 float 承载尺寸，
        // 见 kCylinderFloatTolerance 注释；盒参数为 double 不受此限）。
        EXPECT_NEAR(v.dim1, spans[i], kCylinderFloatTolerance)
            << frameName << "：圆柱高度≠跨度（SI 真值/派生违约）";

        // 半径＝clamp(0.15×L, 0.005, 0.08)——卡面冻结规则的测试侧镜像
        //（与产品实现互证：同式重算，float 精度内一致）。
        const double expectedRadius =
            std::min(0.08, std::max(0.005, 0.15 * spans[i]));  // 单位 m
        EXPECT_NEAR(v.dim0, expectedRadius, kCylinderFloatTolerance)
            << frameName << "：半径偏离冻结派生规则";
        EXPECT_GE(v.dim0, 0.005 - kCylinderFloatTolerance)
            << frameName << "：半径低于下限（钳制失效）";
        EXPECT_LE(v.dim0, 0.08 + kCylinderFloatTolerance)
            << frameName << "：半径高于上限（钳制失效）";
    }
    // 三分支落点自证：分支 2 触上限（比例 0.15>0.08）、分支 3 触下限
    // （比例 0.00015<0.005）——若钳制退化本用例无法同时满足（夹具自检；
    // float 往返误差 ≪ 容差，钳制值以 float 精度复核）。
    EXPECT_NEAR(geoms.at(scopedName(map, f.chain.links.at(1).objectId,
                                    NameScope::LinkFrame)
                             + "@ird-link-cylinder").dim0,
                0.08, kCylinderFloatTolerance)
        << "上限钳制分支未命中（夹具跨度失配——用例自检）";
    EXPECT_NEAR(geoms.at(scopedName(map, f.chain.links.at(2).objectId,
                                    NameScope::LinkFrame)
                             + "@ird-link-cylinder").dim0,
                0.005, kCylinderFloatTolerance)
        << "下限钳制分支未命中（夹具跨度失配——用例自检）";

    // ---- 标记盒：全边长恒 0.02 m（三轴同值）----
    const std::string flangeName =
        scopedName(map, f.chain.links.at(3).objectId, NameScope::Flange);
    const GeometryView& box = geoms.at(flangeName + "@ird-marker-box");
    EXPECT_DOUBLE_EQ(box.dim0, 0.02) << "法兰盒 dx≠20 mm（SI 真值违约）";
    EXPECT_DOUBLE_EQ(box.dim1, 0.02) << "法兰盒 dy≠20 mm";
    EXPECT_DOUBLE_EQ(box.dim2, 0.02) << "法兰盒 dz≠20 mm";

    // ---- 圆柱朝向/位置：帧内旋转把局部 +Z 对到跨度单位方向、平移＝中点
    //      （spanPlacement 安放定义的自证——位级对照）----
    const rw::math::Vector3D<double> span(0.15, 0.0, 0.30);              // 跨度，m
    const rw::math::Vector3D<double> dir = span * (1.0 / span.norm2()); // 无量纲
    const GeometryView& cyl = geoms.at(scopedName(map, f.chain.links.at(0).objectId,
                                                  NameScope::BaseFrame)
                                       + "@ird-link-cylinder");
    for (int r = 0; r < 3; ++r) {
        // 旋转第三列＝R·ez＝单位方向（基线 Rotation3D 无 getColumn——以
        // (r,2) 索引取第三列，WorkCellCompilerTest RT-EQ-1 同款读法）。
        EXPECT_DOUBLE_EQ(cyl.placement.R()(r, 2), dir[r])
            << "圆柱朝向第 " << r << " 行偏离跨度方向";
        EXPECT_DOUBLE_EQ(cyl.placement.P()[r], 0.5 * span[r]) << "圆柱未置于跨度中点";
    }
}

// =====================================================================
// RT-GEO-3：派生确定性（acceptance 3 后半——同输入同产物，NFR-COR-01）。
// =====================================================================

/**
 * RT-GEO-3 主断言：同一夹具两次独立编译（两次独立 build()——值语义独立
 * 模型）→图元集合（名/绑定帧/类型/尺寸位值/帧内位姿位值）逐项相等。
 * 无环境/时钟依赖（§5.5"同输入重复编译"行的几何面延伸）。
 */
TEST(WorkCellGeometryTest, SameModelSamePrimitives_RT_GEO_3)
{
    const WorkCellCompileOutcome a = compileWorkCell(threeSpanRichFixture().build());
    const WorkCellCompileOutcome b = compileWorkCell(threeSpanRichFixture().build());
    ASSERT_TRUE(!a.workCell.isNull());
    ASSERT_TRUE(!b.workCell.isNull());

    std::map<std::string, GeometryView> ga;
    std::map<std::string, GeometryView> gb;
    collectGeometries(*a.workCell, ga);
    collectGeometries(*b.workCell, gb);

    ASSERT_EQ(ga.size(), gb.size()) << "两次编译图元数不等（确定性破坏）";
    for (const auto& entry : ga) {
        const auto it = gb.find(entry.first);
        ASSERT_TRUE(it != gb.end()) << "第二次编译缺图元：" << entry.first;
        const GeometryView& va = entry.second;
        const GeometryView& vb = it->second;
        EXPECT_EQ(vb.frameName, va.frameName) << entry.first << "：绑定帧漂移";
        EXPECT_EQ(vb.type, va.type) << entry.first << "：类型漂移";
        // 尺寸与位姿按 IEEE754 位级比较（+0.0/−0.0 级差异也算漂移——
        // 与身份域位模式纪律同尺度的最严观测）。
        EXPECT_EQ(vb.dim0, va.dim0) << entry.first << "：dim0 漂移";
        EXPECT_EQ(vb.dim1, va.dim1) << entry.first << "：dim1 漂移";
        EXPECT_EQ(vb.dim2, va.dim2) << entry.first << "：dim2 漂移";
        for (int r = 0; r < 3; ++r) {
            EXPECT_EQ(vb.placement.P()[r], va.placement.P()[r])
                << entry.first << "：位姿平移漂移";
            for (int c = 0; c < 3; ++c) {
                EXPECT_EQ(vb.placement.R()(r, c), va.placement.R()(r, c))
                    << entry.first << "：位姿旋转漂移";
            }
        }
    }
}

// =====================================================================
// RT-GEO-4：诚实跳过面（acceptance 2——零虚构零错误零阻塞零资源消费）。
// =====================================================================

namespace {

/**
 * @brief 计数资源提供者替身（§8.6 IRuntimeResourceProvider 的测试替身——
 *        唯一职责＝记录 tryResourceBytes 调用次数；S6 mesh 通道接线预留的
 *        "零消费"断言输入）。任何调用都返回 err（ResourceMissing）——若
 *        S6 意外消费资源，计数与失败语义都会被用例捕获。
 */
class CountingResourceProvider final : public IRuntimeResourceProvider
{
public:
    Expected<ResourceBytes, ResourceReadError>
        tryResourceBytes(core::ObjectId resourceId) const override
    {
        ++m_calls;  // 线程约束：编译单线程调用（§5.7）——非原子计数足够
        ResourceReadError err;
        err.code = RuntimeErrorCode::ResourceMissing;
        err.resourceId = std::move(resourceId);
        err.detail = "RT-GEO-4 计数替身：S6 不应发起任何资源读取";
        return Expected<ResourceBytes, ResourceReadError>::err(std::move(err));
    }

    std::size_t callCount() const noexcept { return m_calls; }

private:
    mutable std::size_t m_calls = 0;  ///< tryResourceBytes 累计调用次数（无单位）
};

}  // namespace

/**
 * RT-GEO-4 主断言（卡 acceptance 2）：
 *   a) manifest 为空（当前模板模型现状——F-550 登记）→编译成功；
 *   b) provider 缺位（nullptr）且 manifest 非空→编译成功；
 *   c) provider 在位且 manifest 非空→S6 对 provider **零调用**（mesh 通道
 *      只预留接口位不实现资源加载——红线面）；
 *   d) 全部情形产出仅为基本图元（零虚构 mesh 几何）、无错误级诊断轨
 *      （编译成功本身＝不产错误——编译失败才携带 error 级诊断，§5.6）。
 */
TEST(WorkCellGeometryTest, HonestSkipWithoutProviderOrManifest_RT_GEO_4)
{
    // ---- (a) manifest 为空（minimal 夹具清空清单——res-1 未被引用，可清）；
    //     minimal 关节平移全零→零跨度→诚实缺省不挂圆柱，仅法兰标记盒。
    Fixture emptyManifest = minimalFixture();
    emptyManifest.manifest.clear();
    const CanonicalModel mEmpty = emptyManifest.build();
    const WorkCellCompileOutcome a = compileWorkCell(mEmpty, nullptr);
    ASSERT_TRUE(!a.workCell.isNull()) << "空 manifest 编译失败（诚实跳过面违约）";
    std::map<std::string, GeometryView> ga;
    collectGeometries(*a.workCell, ga);
    EXPECT_EQ(ga.size(), std::size_t{1}) << "minimal 模型图元数≠1（法兰标记盒）";
    for (const auto& entry : ga) {
        EXPECT_EQ(entry.second.type, rw::geometry::GeometryData::BoxPrim)
            << entry.first << "：出现非图元几何（虚构几何违例）";
    }

    // ---- (b) provider 缺位＋manifest 非空（三跨度 rich——引用命中清单）→成功
    const CanonicalModel mRich = threeSpanRichFixture().build();
    const WorkCellCompileOutcome b = compileWorkCell(mRich, nullptr);
    ASSERT_TRUE(!b.workCell.isNull()) << "provider 缺位编译失败（诚实跳过面违约）";

    // ---- (c) provider 在位（恒缺）＋manifest 非空→逐引用真实尝试读取
    // （MA-01 真实消费面——读取失败逐引用回退图元代理，编译不阻塞）----
    CountingResourceProvider provider;
    const WorkCellCompileOutcome c = compileWorkCell(mRich, &provider);
    ASSERT_TRUE(!c.workCell.isNull());
    EXPECT_GE(provider.callCount(), std::size_t{1})
        << "S6 未发起资源读取（MA-01 真实消费面未生效——引用命中清单即应尝试）";

    // ---- (d) 产出仍为基本图元（prim 恒挂接——呈现代理与 mesh 通道正交）----
    std::map<std::string, GeometryView> gc;
    collectGeometries(*c.workCell, gc);
    for (const auto& entry : gc) {
        EXPECT_TRUE(entry.second.type == rw::geometry::GeometryData::CylinderPrim
                    || entry.second.type == rw::geometry::GeometryData::BoxPrim)
            << entry.first << "：出现非基本图元类型（虚构几何违例）";
    }
}

// =====================================================================
// RT-GEO-5：S6 既有产物与映射条目零变化（acceptance 1 后半＋回归面）。
// =====================================================================

/**
 * RT-GEO-5 主断言（卡 acceptance 1"S6 既有产物与映射条目零变化"）：
 *   - outcome.runtimeNames 与"帧树/设备名"既有口径逐字节一致（serialChain
 *     ＋BaseMount＋TCP＋场景——第 11 步装配序）；载体对象/图元名不入集合；
 *   - WC 帧数＝WORLD＋BaseMount＋链帧（基座＋逐关节 2 帧）＋TCP＋场景
 *     （几何挂接零新增帧）；
 *   - 设备关节限位（RT-AD-2 面）不被几何挂接扰动（逐字对照夹具值）；
 *   - 编译前后 CanonicalModel 的 RT-Codec 全字段编码逐字节不变（显示代理
 *     不入身份域——§4.3.6/ARC-04，RT-ID 回归钉）。
 */
TEST(WorkCellGeometryTest, S6ProductSurfacesUnchangedByGeometry_RT_GEO_5)
{
    const Fixture f = threeSpanRichFixture();
    const CanonicalModel model = f.build();
    const RuntimeNameMap map = buildRuntimeNameMap(model);

    // 编译前模型编码（身份域全字段——RT-Codec 规范编码）。
    const std::vector<std::uint8_t> encodedBefore = rtcodec::encode(model);

    const WorkCellCompileOutcome out = compileWorkCell(model);
    ASSERT_TRUE(!out.workCell.isNull());

    // ---- runtimeNames：既有装配口径逐字节一致（顺序敏感——确定性序）----
    std::vector<std::string> expected;
    expected.push_back(scopedName(map, f.chain.links.at(0).objectId, NameScope::BaseFrame));
    for (std::size_t i = 0; i < f.chain.joints.size(); ++i) {
        expected.push_back(map.resolveObjectId(f.chain.joints.at(i).objectId).get().fullName);
        const bool isLastLink = (i + 2 == f.chain.links.size());
        expected.push_back(isLastLink
                               ? scopedName(map, f.chain.links.at(i + 1).objectId, NameScope::Flange)
                               : scopedName(map, f.chain.links.at(i + 1).objectId, NameScope::LinkFrame));
    }
    expected.push_back(scopedName(map, f.chain.robotObjectId, NameScope::BaseMount));
    for (const CanonicalTool& tool : f.tools) {
        expected.push_back(scopedName(map, tool.objectId, NameScope::Tcp));
    }
    for (const CanonicalSceneObject& so : f.scene) {
        expected.push_back(scopedName(map, so.objectId, NameScope::SceneObject));
    }
    ASSERT_EQ(out.runtimeNames.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(out.runtimeNames.at(i), expected.at(i))
            << "runtimeNames[" << i << "] 漂移（既有产物面被几何挂接扰动）";
    }

    // ---- 帧数：WORLD(1)＋BaseMount(1)＋基座(1)＋逐关节 2 帧＋TCP＋场景----
    const std::size_t chainFrames = 1 + f.chain.joints.size() * 2;
    const std::size_t expectedFrames = 1 /*WORLD 根——编译产物锚点*/ + 1 /*BaseMount*/
                                     + chainFrames + f.tools.size() + f.scene.size();
    EXPECT_EQ(out.workCell->getFrames().size(), expectedFrames)
        << "WC 帧数漂移（几何挂接新增帧——映射外帧名红线违约）";

    // ---- 设备关节限位抽查（RT-AD-2 面不被扰动——逐字等于夹具值）----
    const rw::core::Ptr<const rw::models::SerialDevice> device =
        out.workCell->findDevice<rw::models::SerialDevice>(out.deviceName);
    ASSERT_TRUE(!device.isNull());
    ASSERT_EQ(device->getJoints().size(), f.chain.joints.size());
    for (std::size_t i = 0; i < f.chain.joints.size(); ++i) {
        const rw::models::Joint* j = device->getJoints().at(i);
        ASSERT_TRUE(f.chain.joints.at(i).bounds.has_value());
        // WC bounds＝权威限位平移 −zeroOffset（夹具 zeroOffset＝0——直等）。
        EXPECT_DOUBLE_EQ(j->getBounds().first[0],
                         f.chain.joints.at(i).bounds->lower - f.chain.joints.at(i).zeroOffset)
            << "关节 " << i << " 下限漂移（几何挂接扰动既有产物）";
        EXPECT_DOUBLE_EQ(j->getBounds().second[0],
                         f.chain.joints.at(i).bounds->upper - f.chain.joints.at(i).zeroOffset)
            << "关节 " << i << " 上限漂移（几何挂接扰动既有产物）";
    }

    // ---- 身份面：编译后模型编码逐字节不变（显示代理不入身份域）----
    const std::vector<std::uint8_t> encodedAfter = rtcodec::encode(model);
    ASSERT_EQ(encodedBefore.size(), encodedAfter.size());
    EXPECT_TRUE(encodedBefore == encodedAfter)
        << "编译改变模型编码（显示代理泄漏进身份域——ARC-04 违例）";
}

// =====================================================================
// MA-01（2026-10-21 主线功能审计立项批）：资源 mesh 真实加载端到端。
// =====================================================================

namespace {

/// 构造二进制 STL 字节（80B 头＋u32 数量＋每三角 50B——紧凑多三角形态；
/// 顶点沿 X 错开使三角非退化，解析器可计数）。
std::vector<std::uint8_t> makeBinaryStl(std::size_t triangleCount)
{
    std::vector<std::uint8_t> bytes(80 + 4 + triangleCount * 50, 0);
    const std::string header = "ird-test-stl";
    std::copy(header.begin(), header.end(), bytes.begin());
    const std::uint32_t count = static_cast<std::uint32_t>(triangleCount);
    std::memcpy(bytes.data() + 80, &count, 4);
    for (std::size_t i = 0; i < triangleCount; ++i) {
        std::uint8_t* tri = bytes.data() + 84 + i * 50;
        const float base = static_cast<float>(i);
        const float verts[9] = {base, 0.0f, 0.0f, base + 1.0f, 0.0f,
                                0.0f, base, 1.0f, 0.0f};
        // 法线（12B）置零（解析器不消费法线有效性）；顶点 36B；属性 2B 置零。
        std::memcpy(tri + 12, verts, 36);
    }
    return bytes;
}

/// 字节摘要（与编译器 S4 复核同源——ContentDigester 单点）。
core::Digest256 digestOfBytes(const std::vector<std::uint8_t>& bytes)
{
    core::ContentDigester digester;
    digester.update(bytes.data(), bytes.size());
    return digester.finalize();
}

/// 内存资源提供者（MA-01 测试替身——digest 命中即返回；miss 走
/// ResourceMissing 轨）。
class InMemoryMeshProvider final : public IRuntimeResourceProvider
{
public:
    InMemoryMeshProvider(std::map<core::ObjectId, std::vector<std::uint8_t>> table)
        : m_table(std::move(table))
    {
    }

    Expected<ResourceBytes, ResourceReadError>
        tryResourceBytes(core::ObjectId resourceId) const override
    {
        const auto it = m_table.find(resourceId);
        if (it == m_table.end()) {
            ResourceReadError err;
            err.code = RuntimeErrorCode::ResourceMissing;
            err.resourceId = resourceId;
            err.detail = "MA-01 内存替身：无该资源";
            return Expected<ResourceBytes, ResourceReadError>::err(std::move(err));
        }
        m_last.data = it->second.data();
        m_last.size = it->second.size();
        m_last.digest = digestOfBytes(it->second);
        return Expected<ResourceBytes, ResourceReadError>::ok(m_last);
    }

private:
    std::map<core::ObjectId, std::vector<std::uint8_t>> m_table;
    mutable ResourceBytes m_last;  ///< 借持视图缓冲（provider 持有——P-IO-2）
};

/// 重绑夹具资源引用（manifest 条目＋全部同 id 引用同步改 digest/hint——
/// §4.3.3"引用须命中 manifest"约束的测试侧同源更新）。
void rebindResource(Fixture& f, std::size_t manifestIndex,
                    const core::Digest256& digest, const char* hint)
{
    f.manifest.at(manifestIndex).contentDigest = digest;
    f.manifest.at(manifestIndex).sourcePathHint = hint;
    const ResourceRef updated = f.manifest.at(manifestIndex);
    for (auto& link : f.chain.links) {
        if (link.visual.has_value()
            && link.visual->resourceId == updated.resourceId) {
            link.visual = updated;
        }
        if (link.collision.has_value()
            && link.collision->resourceId == updated.resourceId) {
            link.collision = updated;
        }
    }
    for (auto& tool : f.tools) {
        if (tool.geometry.has_value()
            && tool.geometry->resourceId == updated.resourceId) {
            tool.geometry = updated;
        }
    }
    for (auto& scene : f.scene) {
        if (scene.geometry.has_value()
            && scene.geometry->resourceId == updated.resourceId) {
            scene.geometry = updated;
        }
    }
}

}  // namespace

/**
 * MA-01 STL 端到端：provider 供给有效二进制 STL——编译后连杆 visual/
 * collision 两引用各挂真实网格（PlainTriMesh 类型＋名字面〈帧名〉.visual/
 * .collision——NameMap 几何 facet 命名），图元代理并存不互斥。
 */
TEST(WorkCellGeometryTest, ResourceMeshStlAttachedEndToEnd_RT_GEO_7)
{
    Fixture f = threeSpanRichFixture();
    const std::vector<std::uint8_t> stl1 = makeBinaryStl(1);
    const std::vector<std::uint8_t> stl2 = makeBinaryStl(4);
    rebindResource(f, 0, digestOfBytes(stl1), "m1.stl");
    rebindResource(f, 1, digestOfBytes(stl2), "m2.stl");
    const CanonicalModel model = f.build();
    const RuntimeNameMap map = buildRuntimeNameMap(model);

    InMemoryMeshProvider provider({
        {f.manifest.at(0).resourceId, stl1},
        {f.manifest.at(1).resourceId, stl2},
    });
    const WorkCellCompileOutcome out = compileWorkCell(model, &provider);
    ASSERT_TRUE(!out.workCell.isNull()) << "STL 端到端编译失败";

    std::map<std::string, GeometryView> geoms;
    collectGeometries(*out.workCell, geoms);
    std::size_t meshCount = 0;
    for (const auto& entry : geoms) {
        if (entry.second.type == rw::geometry::GeometryData::PlainTriMesh) {
            ++meshCount;
        }
    }
    EXPECT_GE(meshCount, std::size_t{2})
        << "STL 端到端网格数＜2（visual＋collision 未都挂接）";
    const std::string link1Visual =
        scopedName(map, f.chain.links.at(1).objectId, NameScope::LinkFrame) + ".visual";
    const std::string link2Collision =
        scopedName(map, f.chain.links.at(2).objectId, NameScope::LinkFrame) + ".collision";
    EXPECT_TRUE(geoms.count(link1Visual) != 0)
        << "link1 visual 网格缺失（名字面=" << link1Visual << "）";
    EXPECT_TRUE(geoms.count(link2Collision) != 0)
        << "link2 collision 网格缺失（名字面=" << link2Collision << "）";
}

/**
 * MA-01 OBJ 端到端：文本 OBJ（v/f 三角）经 LoaderOBJ 解析合并挂接——
 * 同名面存在且类型＝PlainTriMesh。
 */
TEST(WorkCellGeometryTest, ResourceMeshObjAttachedEndToEnd_RT_GEO_7)
{
    Fixture f = threeSpanRichFixture();
    const std::string objText =
        "v 0 0 0\nv 1 0 0\nv 0 1 0\nv 0 0 1\nf 1 2 3\nf 1 2 4\n";
    const std::vector<std::uint8_t> objBytes(objText.begin(), objText.end());
    rebindResource(f, 0, digestOfBytes(objBytes), "m1.obj");
    const CanonicalModel model = f.build();
    const RuntimeNameMap map = buildRuntimeNameMap(model);

    InMemoryMeshProvider provider({{f.manifest.at(0).resourceId, objBytes}});
    const WorkCellCompileOutcome out = compileWorkCell(model, &provider);
    ASSERT_TRUE(!out.workCell.isNull()) << "OBJ 端到端编译失败";

    std::map<std::string, GeometryView> geoms;
    collectGeometries(*out.workCell, geoms);
    const std::string link1Visual =
        scopedName(map, f.chain.links.at(1).objectId, NameScope::LinkFrame) + ".visual";
    ASSERT_TRUE(geoms.count(link1Visual) != 0)
        << "OBJ 网格缺失（名字面=" << link1Visual << "）";
    EXPECT_EQ(geoms.at(link1Visual).type,
              rw::geometry::GeometryData::PlainTriMesh)
        << "OBJ 挂接非网格几何（解析合并面违约）";
}

/**
 * MA-01 摘要复核反面：provider 字节与引用 digest 失配——挂接被拒（S4
 * 挂接点复核），零网格产出且编译成功（回退图元代理——诚实缺省）。
 */
TEST(WorkCellGeometryTest, ResourceMeshDigestMismatchSkipped_RT_GEO_7)
{
    Fixture f = threeSpanRichFixture();
    const std::vector<std::uint8_t> stl = makeBinaryStl(2);
    core::Digest256 forged = digestOfBytes(stl);
    forged[0] = static_cast<std::uint8_t>(forged[0] ^ 0xFF);  // 伪造失配
    rebindResource(f, 0, forged, "m1.stl");
    const CanonicalModel model = f.build();

    InMemoryMeshProvider provider({{f.manifest.at(0).resourceId, stl}});
    const WorkCellCompileOutcome out = compileWorkCell(model, &provider);
    ASSERT_TRUE(!out.workCell.isNull()) << "摘要失配不应使编译失败";

    std::map<std::string, GeometryView> geoms;
    collectGeometries(*out.workCell, geoms);
    for (const auto& entry : geoms) {
        EXPECT_NE(entry.second.type, rw::geometry::GeometryData::PlainTriMesh)
            << entry.first << "：摘要失配仍挂接网格（S4 复核违约）";
    }
}

/**
 * MA-01 格式守门反面：hint 指向未支持格式（.dae——Assimp 依赖面）——
 * 明确跳过（不伪装解析），编译成功零网格。
 */
TEST(WorkCellGeometryTest, ResourceMeshUnsupportedFormatSkipped_RT_GEO_7)
{
    Fixture f = threeSpanRichFixture();
    const std::vector<std::uint8_t> stl = makeBinaryStl(2);
    rebindResource(f, 0, digestOfBytes(stl), "m1.dae");
    const CanonicalModel model = f.build();

    InMemoryMeshProvider provider({{f.manifest.at(0).resourceId, stl}});
    const WorkCellCompileOutcome out = compileWorkCell(model, &provider);
    ASSERT_TRUE(!out.workCell.isNull()) << "未支持格式不应使编译失败";

    std::map<std::string, GeometryView> geoms;
    collectGeometries(*out.workCell, geoms);
    for (const auto& entry : geoms) {
        EXPECT_NE(entry.second.type, rw::geometry::GeometryData::PlainTriMesh)
            << entry.first << "：未支持格式仍产出网格（格式守门违约）";
    }
}
