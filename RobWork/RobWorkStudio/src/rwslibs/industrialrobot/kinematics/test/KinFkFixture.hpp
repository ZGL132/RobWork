/**
 * @file   KinFkFixture.hpp
 * @brief  kinematics T03 测试夹具——确定性规范模型构造器（解析黄金算例
 *         的四套几何）、宿主注入视图替身（IKinRuntimeView 实现——R-KIN-1
 *         "测试替身先行"）、上下文替身与独立参考计算（4×4 齐次坐标独立
 *         实现——附录 D 第 9 项"独立参考实现对照"类别）。
 *
 * 设计依据：
 *   - units/kinematics.md §10.1（黄金数据集 kin-fk-analytic 口径——本夹具
 *     为 T03 先行解析算例 UT 的载体，T13 收口全量数据集）、§14.3 R-KIN-1
 *   - runtime/test/CanonicalModelFixture.hpp 的确定性 id 派生先例（本夹具
 *     自持副本——runtime 测试内部头不跨单元消费，R-2 精神）
 *   - 任务契约 tasks/foundation/WP-15-T03.json（acceptance 1/2/3/4 的
 *     测试面）
 *
 * 确定性：对象 id 由固定种子经 core::ContentDigester 派生（同夹具跨进程
 * 字节一致）；黄金算例几何常量为字面量（无随机）。
 * 线程安全：纯值构造（无共享状态）；每个 TEST 各持一份。
 */

#ifndef SDURWS_IRD_KINEMATICS_TEST_KINFKFIXTURE_HPP
#define SDURWS_IRD_KINEMATICS_TEST_KINFKFIXTURE_HPP

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/evidence/Evaluator.hpp>
#include <sdurws/ird/kinematics/Evaluators.hpp>
#include <sdurws/ird/kinematics/Fk.hpp>
#include <sdurws/ird/kinematics/KinTypes.hpp>
#include <sdurws/ird/runtime/CanonicalModel.hpp>
#include <sdurws/ird/runtime/Description.hpp>
#include <sdurws/ird/runtime/Sources.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sdurws::ird::kinematics::testfixture {

namespace core = sdurws::ird::core;
namespace rt = sdurws::ird::runtime;
using sdurws::ird::kinematics::TcpRef;

// =====================================================================
// 确定性 id/摘要派生（runtime 测试夹具先例的自持副本——固定种子）。
// =====================================================================

inline core::Digest256 digestOf(const std::string& seed)
{
    core::ContentDigester d;
    d.update(seed.data(), seed.size());
    return d.finalize();
}

/// 从固定种子派生 16 字节强类型 id（测试值——非密码学用途）。
template <typename Id>
inline Id idFrom(const std::string& seed)
{
    const core::Digest256 d = digestOf(seed);
    Id id;
    std::copy(d.begin(), d.begin() + 16, id.bytes.begin());
    return id;
}

inline core::ContentVersion cvFrom(const std::string& seed)
{
    core::ContentVersion cv;
    cv.bytes = digestOf(seed);
    return cv;
}

// =====================================================================
// 几何小工具（rw 值类型；Transform3D 构造序＝(平移, 旋转)）。
// =====================================================================

inline rw::math::Rotation3D<double> identityR()
{
    return rw::math::Rotation3D<double>(1, 0, 0, 0, 1, 0, 0, 0, 1);
}

inline rw::math::Transform3D<double> trans(double x, double y, double z)
{
    return rw::math::Transform3D<double>(rw::math::Vector3D<double>(x, y, z),
                                         identityR());
}

/// 绕 z 轴的解析旋转（rad——测试黄金值用；与产品 Rodrigues 无关）。
inline rw::math::Rotation3D<double> rotZ(double a)
{
    const double c = std::cos(a);
    const double s = std::sin(a);
    return rw::math::Rotation3D<double>(c, -s, 0, s, c, 0, 0, 0, 1);
}

// =====================================================================
// 规范模型构造器（解析黄金四套几何＋通用装配）。
// =====================================================================

/**
 * @brief 通用模型装配：给定关节清单与 TCP 偏置，补齐 header/闭包/链/工具
 *        并经 CanonicalModelBuilder 构造（builder 校验失败＝夹具缺陷，
 *        直接抛出——测试 fail-fast）。
 *
 * @param joints   [in] 关节链（链序；Fixed 关节不消耗 q）
 * @param tcpOffset [in] 法兰→TCP 变换（工具唯一 canonical TCP）
 * @param worldToBase [in] T_world_base（默认地面安装——恒等）
 * @param withTool [in] false＝装配无工具模型（KIN-NO-TCP"未配置"分支的
 *                      测试面——tools 空集＋defaultTcpIndex nullopt）
 * @return 构造完成的规范模型（值语义）
 */
inline rt::CanonicalModel makeModel(const std::vector<rt::CanonicalJoint>& joints,
                                    const rw::math::Transform3D<double>& tcpOffset,
                                    const rw::math::Transform3D<double>& worldToBase = trans(0, 0, 0),
                                    bool withTool = true)
{
    rt::CanonicalModelHeader header;
    header.project = idFrom<core::ProjectId>("kin-prj");
    header.branch = idFrom<core::BranchId>("kin-brn");
    header.revision = idFrom<core::RevisionId>("kin-rev-1");
    header.revisionSeq = 1;
    header.descriptionContractVersion = 1;
    header.compilerContractVersion = 1;
    header.builtFrom = digestOf("kin-fixture-description");

    // 闭包引用登记（CM-0：机器人＋逐关节＋逐连杆＋工具全部入清单）。
    // 全部登记先于 setHeader（builder 持 header 值副本——后补不生效）。
    const core::ObjectId robot = idFrom<core::ObjectId>("kin-robot");
    const core::ObjectId tool = idFrom<core::ObjectId>("kin-tool");
    auto addRef = [&header](const core::ObjectId& id, const char* seed, const char* token) {
        rt::ObjectRefEntry e;
        e.objectId = id;
        e.contentVersion = cvFrom(seed);
        e.objectTypeToken = token;
        e.digest = digestOf(std::string{seed} + "-bytes");
        header.objectRefs.push_back(e);
    };
    addRef(robot, "kin-robot", "robot-design");
    for (std::size_t i = 0; i < joints.size(); ++i) {
        addRef(joints[i].objectId, ("kin-j" + std::to_string(i)).c_str(), "joint");
    }
    if (withTool) {
        addRef(tool, "kin-tool", "tool");
    }

    rt::RobotChain chain;
    chain.robotObjectId = robot;
    chain.robotLocalName = "IRD_FK_T";
    chain.deviceName = "IRD_FK_T";
    chain.joints = joints;
    // 连杆数＝关节数＋1（含基座连杆——builder 不变量）；id 派生与登记同步。
    for (std::size_t i = 0; i <= joints.size(); ++i) {
        const core::ObjectId lid = idFrom<core::ObjectId>("kin-l" + std::to_string(i));
        addRef(lid, ("kin-l" + std::to_string(i)).c_str(), "link");
        rt::CanonicalLink link;
        link.objectId = lid;
        link.localName = "link_" + std::to_string(i);
        chain.links.push_back(link);
    }

    rt::CanonicalTool t;
    t.objectId = tool;
    t.localName = "tool_1";
    t.tcpOffset = tcpOffset;

    rt::WorldPlacement world;
    world.T_world_base = worldToBase;

    // builder 按值持有（setter 链在副本上续调——不绑临时引用）。
    rt::CanonicalModelBuilder builder
        = rt::CanonicalModelBuilder().setHeader(header).setWorld(world).setChain(chain);
    // tools 非空 → defaultTcpIndex 必填且指向第 0 项（builder 不变量）；
    // 无工具模型 → tools 空集＋defaultTcpIndex nullopt（builder 不变量）。
    if (withTool) {
        return builder.setTools({t}).setDefaultTcpIndex(0U).build();
    }
    return builder.setTools({}).setDefaultTcpIndex(std::nullopt).build();
}

/// 旋转关节便捷构造（Revolute 必带有限限位——builder 不变量）。
inline rt::CanonicalJoint revolute(const std::string& name,
                                   const rw::math::Vector3D<double>& axis,
                                   const rw::math::Transform3D<double>& origin,
                                   double lower, double upper)
{
    rt::CanonicalJoint j;
    j.objectId = idFrom<core::ObjectId>(name);
    j.localName = name;
    j.type = rt::JointType::Revolute;
    j.axis = axis;
    j.origin = origin;
    j.bounds = rt::JointBounds{lower, upper};  // 单位 rad
    return j;
}

/// 连续关节便捷构造（Continuous 无 bounds；工作范围可选——MDL-12）。
inline rt::CanonicalJoint continuous(const std::string& name,
                                     const rw::math::Vector3D<double>& axis,
                                     bool withWorkingRange)
{
    rt::CanonicalJoint j;
    j.objectId = idFrom<core::ObjectId>(name);
    j.localName = name;
    j.type = rt::JointType::Continuous;
    j.axis = axis;
    if (withWorkingRange) {
        j.workingRange = rt::WorkingRange{-0.5, 0.5};  // 单位 rad（分析消费区间）
    }
    return j;
}

/// 移动关节便捷构造（Prismatic 必带有限限位，单位 m）。
inline rt::CanonicalJoint prismatic(const std::string& name,
                                    const rw::math::Vector3D<double>& axis,
                                    double lower, double upper)
{
    rt::CanonicalJoint j;
    j.objectId = idFrom<core::ObjectId>(name);
    j.localName = name;
    j.type = rt::JointType::Prismatic;
    j.axis = axis;
    j.bounds = rt::JointBounds{lower, upper};
    return j;
}

// ---- 解析黄金算例的几何常量（§10.1 kin-fk-analytic 先行批）----

/// 黄金算例 A：平面二连杆（两旋转关节轴平行 Z；L1/L2 单位 m）。
inline rt::CanonicalModel twoLinkModel()
{
    constexpr double L1 = 1.1;
    constexpr double L2 = 0.7;
    std::vector<rt::CanonicalJoint> joints;
    joints.push_back(revolute("j1", rw::math::Vector3D<double>(0, 0, 1),
                              trans(0, 0, 0), -2.97, 2.97));
    joints.push_back(revolute("j2", rw::math::Vector3D<double>(0, 0, 1),
                              trans(L1, 0, 0), -3.14159265358979323846,
                              3.14159265358979323846));
    return makeModel(joints, trans(L2, 0, 0));
}

/// 黄金算例 B：转动＋移动（雅可比列正交——奇异值可全解析：σ＝{√(L²+1), 1}）。
inline rt::CanonicalModel revPrismModel()
{
    constexpr double L = 1.3;
    std::vector<rt::CanonicalJoint> joints;
    joints.push_back(revolute("j1", rw::math::Vector3D<double>(0, 0, 1),
                              trans(0, 0, 0), -2.9, 2.9));
    joints.push_back(prismatic("j2", rw::math::Vector3D<double>(1, 0, 0), -1.0, 1.0));
    return makeModel(joints, trans(L, 0, 0));
}

/// 黄金算例 C：六轴臂（独立参考实现对照——姿态/雅可比/奇异值恒等式）。
inline rt::CanonicalModel sixAxisModel()
{
    std::vector<rt::CanonicalJoint> joints;
    joints.push_back(revolute("j1", rw::math::Vector3D<double>(0, 0, 1),
                              trans(0, 0, 0), -3.14, 3.14));
    joints.push_back(revolute("j2", rw::math::Vector3D<double>(0, 1, 0),
                              trans(0, 0, 0.4), -1.5, 1.5));
    joints.push_back(revolute("j3", rw::math::Vector3D<double>(0, 1, 0),
                              trans(0.5, 0, 0), -1.5, 1.5));
    joints.push_back(revolute("j4", rw::math::Vector3D<double>(1, 0, 0),
                              trans(0.5, 0, 0), -3.14, 3.14));
    joints.push_back(revolute("j5", rw::math::Vector3D<double>(0, 1, 0),
                              trans(0.3, 0, 0), -1.5, 1.5));
    joints.push_back(revolute("j6", rw::math::Vector3D<double>(1, 0, 0),
                              trans(0.2, 0, 0), -3.14, 3.14));
    // TCP 偏置带旋转（Rz(π/2)——法兰→TCP 旋转复合路径的覆盖；平移 m）。
    return makeModel(joints,
                     rw::math::Transform3D<double>(
                         rw::math::Vector3D<double>(0.15, 0, 0.1), rotZ(1.57079632679489661923)));
}

/// 黄金算例 D：全固定链（dof=0 边界——刚体位姿＋空指标面）。
inline rt::CanonicalModel allFixedModel()
{
    std::vector<rt::CanonicalJoint> joints;
    rt::CanonicalJoint j;
    j.objectId = idFrom<core::ObjectId>("fix1");
    j.localName = "fixed_1";
    j.type = rt::JointType::Fixed;
    j.axis = rw::math::Vector3D<double>(0, 0, 1);  // 轴须非零（MDL-11——Fixed 亦然）
    j.origin = trans(0, 0, 0.9);
    joints.push_back(j);
    return makeModel(joints, trans(0, 0, 0.1));
}

/// 裕量归一化算例：有界中点/限位、连续带工作范围、连续无工作范围。
inline rt::CanonicalModel marginModel()
{
    std::vector<rt::CanonicalJoint> joints;
    // q=1 ∈ (-1,3)：中点 → margin 1。
    joints.push_back(revolute("m1", rw::math::Vector3D<double>(0, 0, 1),
                              trans(0, 0, 0), -1.0, 3.0));
    // q=2 ∈ (0,2)：上限位 → margin 0。
    joints.push_back(revolute("m2", rw::math::Vector3D<double>(0, 1, 0),
                              trans(0, 0, 0.1), 0.0, 2.0));
    // 连续＋工作范围 (-0.5,0.5)：q=1 → 超限 → margin 负（违例素材）。
    joints.push_back(continuous("m3", rw::math::Vector3D<double>(0, 0, 1), true));
    // 连续无工作范围：margin +∞。
    joints.push_back(continuous("m4", rw::math::Vector3D<double>(1, 0, 0), false));
    return makeModel(joints, trans(0.2, 0, 0.3));
}

// =====================================================================
// 宿主注入替身（O-37 裁决形态的测试面——R-KIN-1"测试替身先行"）：
// IKinRuntimeView 的最小实现，模型值持有、T_world_base 可设。
// =====================================================================

class TestView final : public IKinRuntimeView {
public:
    explicit TestView(rt::CanonicalModel model)
        : m_model(std::move(model)),
          m_worldToBase(m_model.world().T_world_base)  // 初始化列表直构——不走默认构造
    {
    }

    const rt::CanonicalModel& model() const override { return m_model; }
    rw::math::Transform3D<double> worldToBase() const override { return m_worldToBase; }

private:
    rt::CanonicalModel m_model;                   ///< 值持有（替身自有副本）
    rw::math::Transform3D<double> m_worldToBase;  ///< 取自模型 world 块（§4.2 唯一来源）
};

/// evidence 上下文替身（§11 测试设施口径——只验证契约形状）。
class NoopContext final : public evidence::IEvaluationContext {
public:
    bool cancellationRequested() const override { return false; }
    void reportProgress(std::uint8_t, std::string_view) override {}
    std::optional<std::vector<std::uint8_t>>
    tryObjectBytes(core::ObjectId, core::ContentVersion) const override
    {
        return std::nullopt;
    }
};

// =====================================================================
// 独立参考实现（附录 D 第 9 项"独立参考实现对照"类别）——4×4 齐次坐标
// 直算，与产品的 Eigen 组件域实现零共享代码（仅共享数学定义）。
// =====================================================================

using Mat4 = std::array<std::array<double, 4>, 4>;

inline Mat4 identity4()
{
    Mat4 m{};
    for (int i = 0; i < 4; ++i) { m[i][i] = 1.0; }
    return m;
}

/// Rodrigues 旋转（独立实现——与产品 AngleAxis 路径无共享代码）。
inline Mat4 rodrigues(double ax, double ay, double az, double angle)
{
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    const double t = 1.0 - c;
    Mat4 m{};
    m[0][0] = t * ax * ax + c;
    m[0][1] = t * ax * ay - s * az;
    m[0][2] = t * ax * az + s * ay;
    m[1][0] = t * ax * ay + s * az;
    m[1][1] = t * ay * ay + c;
    m[1][2] = t * ay * az - s * ax;
    m[2][0] = t * ax * az - s * ay;
    m[2][1] = t * ay * az + s * ax;
    m[2][2] = t * az * az + c;
    m[3][3] = 1.0;
    return m;
}

inline Mat4 translation4(double x, double y, double z)
{
    Mat4 m = identity4();
    m[0][3] = x;
    m[1][3] = y;
    m[2][3] = z;
    return m;
}

inline Mat4 mul4(const Mat4& a, const Mat4& b)
{
    Mat4 r{};
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            double s = 0.0;
            for (int k = 0; k < 4; ++k) { s += a[i][k] * b[k][j]; }
            r[i][j] = s;
        }
    }
    return r;
}

/// 6×n 雅可比的独立参考计算（几何定义直算——与产品实现零共享代码）。
/// 返回行优先 6·n；关节输入＝(类型, 轴基座系, 轴点基座系)——由参考 FK
/// 逐关节累计（同参考路径，避免与产品共享中间量）。
struct RefChainResult {
    Mat4 tcp;                          ///< T_base_tcp（独立参考 FK）
    std::vector<double> jacobian6xN;   ///< 行优先 6·n（基础雅可比）
};

inline RefChainResult referenceFk(const rt::CanonicalModel& model,
                                  const std::vector<double>& q)
{
    const auto& joints = model.chain().joints;
    const rt::CanonicalTool& tool = model.tools().at(0);

    Mat4 acc = identity4();
    std::vector<double> jac;
    std::size_t qi = 0;
    std::vector<std::array<double, 3>> axisPts;
    std::vector<std::array<double, 3>> axisDirs;
    std::vector<rt::JointType> kinds;

    for (const auto& j : joints) {
        // origin 复合（父→关节系）。
        Mat4 o = identity4();
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                o[r][c] = j.origin.R()(static_cast<std::size_t>(r),
                                       static_cast<std::size_t>(c));
            }
            o[r][3] = j.origin.P()[static_cast<std::size_t>(r)];
        }
        acc = mul4(acc, o);
        if (j.type == rt::JointType::Fixed) { continue; }

        // 轴点/轴方向（运动前、基座系——雅可比源数据）。
        std::array<double, 3> dir = {0.0, 0.0, 0.0};
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                dir[r] += acc[r][c] * j.axis[static_cast<std::size_t>(c)];
            }
        }
        axisPts.push_back({acc[0][3], acc[1][3], acc[2][3]});
        axisDirs.push_back(dir);
        kinds.push_back(j.type);

        if (j.type == rt::JointType::Prismatic) {
            // 移动沿关节系轴：运动变换＝T( axis·q )（关节系内直写）。
            Mat4 motion = translation4(j.axis[0] * q[qi], j.axis[1] * q[qi],
                                       j.axis[2] * q[qi]);
            acc = mul4(acc, motion);
        } else {
            acc = mul4(acc, rodrigues(j.axis[0], j.axis[1], j.axis[2], q[qi]));
        }
        ++qi;
    }

    // TCP 偏置复合。
    Mat4 t = identity4();
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            t[r][c] = tool.tcpOffset.R()(static_cast<std::size_t>(r),
                                         static_cast<std::size_t>(c));
        }
        t[r][3] = tool.tcpOffset.P()[static_cast<std::size_t>(r)];
    }
    RefChainResult out;
    out.tcp = mul4(acc, t);

    // 几何雅可比（基座系——标准定义直算；行主序 [row*n+col]——与
    // PoseMetrics.jacobian 的行优先契约一致）。
    const double px = out.tcp[0][3];
    const double py = out.tcp[1][3];
    const double pz = out.tcp[2][3];
    const std::size_t n = axisPts.size();
    out.jacobian6xN.assign(6 * n, 0.0);
    for (std::size_t k = 0; k < n; ++k) {
        std::array<double, 6> col = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
        if (kinds[k] == rt::JointType::Prismatic) {
            col[0] = axisDirs[k][0];
            col[1] = axisDirs[k][1];
            col[2] = axisDirs[k][2];
        } else {
            const std::array<double, 3>& a = axisDirs[k];
            std::array<double, 3> d = {px - axisPts[k][0], py - axisPts[k][1],
                                       pz - axisPts[k][2]};
            col[0] = a[1] * d[2] - a[2] * d[1];
            col[1] = a[2] * d[0] - a[0] * d[2];
            col[2] = a[0] * d[1] - a[1] * d[0];
            col[3] = a[0];
            col[4] = a[1];
            col[5] = a[2];
        }
        for (std::size_t r = 0; r < 6; ++r) {
            out.jacobian6xN[r * n + k] = col[r];
        }
    }
    return out;
}

// =====================================================================
// 容差断言（附录 D 通用比较公式：|a−b| ≤ ε_rel·|ref|＋ε_abs）。
// =====================================================================

/// 黄金算例标量容差声明（第 9 项：标量相对 1×10⁻⁹＋逐例声明 ε_abs）。
inline constexpr double kGoldenRel = 1e-9;
inline constexpr double kGoldenAbs = 1e-12;
/// 位姿容差声明（第 4 项：位置 1×10⁻⁹ m、姿态元素 1×10⁻⁹）。
inline constexpr double kPoseTol = 1e-9;

inline void expectGoldenNear(double actual, double ref, const char* what)
{
    ASSERT_LE(std::abs(actual - ref), kGoldenRel * std::abs(ref) + kGoldenAbs)
        << what << "：actual=" << actual << " ref=" << ref;
}

}  // namespace sdurws::ird::kinematics::testfixture

#endif  // SDURWS_IRD_KINEMATICS_TEST_KINFKFIXTURE_HPP
