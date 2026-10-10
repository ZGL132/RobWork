/**
 * @file   DhConvertTest.cpp
 * @brief  DH↔显式转换器用例组（MdlDhConvert）——acceptance 1～3 的具名
 *         自证：无损展开（卡 §7.4 公式逐级累乘/零位对齐/基座混入拒绝）、
 *         五状态各含样例（结构检查先行/逐关节逐项上界判定）、退化族
 *         字典序定值与确定性（NFR-COR-02）。
 *
 * 设计依据：
 *   - units/modeling.md §7.4（标准 DH 公式与展开操作定义——测试的独立
 *     参考实现按同一卡面公式**另行实现**，期望值不引实现内部函数）、
 *     §7.5（两阶段判定——NotExpressible 终判不进求解；附录 D 第 5 项
 *     逐关节逐项上界；字典序定值）、§9.4.7（DhErrorCode/五状态契约）、
 *     §9.5（T09 行三码）、§10.2（V-11 故障注入矩阵行）
 *   - REQUIREMENTS.md §25 附录 D 第 5 项（C3：轴线方向角 ≤1×10⁻⁹ rad 且
 *     原点位置 ≤1×10⁻⁹ m；E 仅为呈现指标）、MDL-10/MDL-02
 *   - 任务契约 tasks/foundation/WP-13-T09.json acceptance 1/2/3
 *     （acceptance 4/5 的编译链对照与命令协作面见 DhConvertEquivalence
 *     Test——集成模式专属 gating，本文件两模式均编译）
 *
 * 测试自证基线声明（NFR-COR-01）：期望位姿/轴向由测试内独立编写的
 * 参考展开函数计算（逐元素 rw 数学——与产品实现不同代码路径、同一
 * 卡面公式），构成解析对照；不消费产品实现的任何内部累积结果。
 */

#include <sdurws/ird/modeling/DhConvert.hpp>

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/core/Provenance.hpp>
#include <sdurws/ird/modeling/DiagCodes.hpp>
#include <sdurws/ird/modeling/RobotDesign.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

using namespace sdurws::ird::modeling;  // NOLINT——用例直接面对被测契约面
namespace core = ::sdurws::ird::core;   // core 侧类型（身份/诊断/来源标记）

namespace {

// π（测试内独立抄写——期望值不引实现常量，CommandFixtures 同款纪律）。
constexpr double kPi = 3.14159265358979323846;

/// 用户输入来源标记（methodTag 语法合规——CommandFixtures 同款）。
core::ValueProvenance userProvenance()
{
    return core::ValueProvenance::make(core::ProvenanceKind::UserProvided,
                                       std::nullopt, std::nullopt,
                                       std::string("test-fixture"));
}

/// 有限限位旋转关节（Explicit 权威：axis/origin/bounds 全 Provided）。
JointEntry makeExplicitJoint(const core::ObjectId& oid, const std::string& name,
                             const rw::math::Vector3D<double>& axis,
                             const JointPose& origin, double zeroOffsetRad)
{
    JointEntry joint;
    joint.objectId = oid;
    joint.localName = name;
    joint.type = JointType::Revolute;
    joint.axis = core::SourcedValue<rw::math::Vector3D<double>>::provided(
        axis, userProvenance());
    joint.origin = core::SourcedValue<JointPose>::provided(origin, userProvenance());
    joint.zeroOffset = zeroOffsetRad;
    joint.bounds = core::SourcedValue<JointLimits>::provided(
        JointLimits{-kPi, kPi}, userProvenance());
    return joint;
}

/// DH 链条目（测试便捷装配）。
DhChainJoint makeDhEntry(const core::ObjectId& oid, const std::string& name,
                         double thetaOffset, double d, double a, double alpha,
                         double zeroOffset)
{
    DhChainJoint entry;
    entry.dh.thetaOffset = thetaOffset;
    entry.dh.d = d;
    entry.dh.a = a;
    entry.dh.alpha = alpha;
    entry.zeroOffset = zeroOffset;
    entry.type = JointType::Revolute;
    entry.objectId = oid;
    entry.localName = name;
    entry.bounds = core::SourcedValue<JointLimits>::provided(
        JointLimits{-kPi, kPi}, userProvenance());
    return entry;
}

// ---- 测试内独立参考展开（卡 §7.4 公式的另行实现——自证基线） ----

/// Rot_z(ψ)（逐元素——与产品无共享代码）。
rw::math::Rotation3D<double> refRotZ(double psi)
{
    const double c = std::cos(psi);
    const double s = std::sin(psi);
    return rw::math::Rotation3D<double>(c, -s, 0.0, s, c, 0.0, 0.0, 0.0, 1.0);
}

/// Rot_x(α)（逐元素）。
rw::math::Rotation3D<double> refRotX(double alpha)
{
    const double c = std::cos(alpha);
    const double s = std::sin(alpha);
    return rw::math::Rotation3D<double>(1.0, 0.0, 0.0, 0.0, c, -s, 0.0, s, c);
}

/// R·v（逐元素——rw 的 R·v 运算符同为外联符号面，冒烟纪律）。
rw::math::Vector3D<double> refRotVec(const rw::math::Rotation3D<double>& r,
                                     const rw::math::Vector3D<double>& v)
{
    return rw::math::Vector3D<double>(
        r(0, 0) * v[0] + r(0, 1) * v[1] + r(0, 2) * v[2],
        r(1, 0) * v[0] + r(1, 1) * v[1] + r(1, 2) * v[2],
        r(2, 0) * v[0] + r(2, 1) * v[1] + r(2, 2) * v[2]);
}

/// R·R（逐元素——与产品无共享代码；rw operator* 内经 multiply() 外联
/// 符号，冒烟模式不可链接——测试参考实现同样走逐元素算术）。
rw::math::Rotation3D<double> refRotMul(const rw::math::Rotation3D<double>& ra,
                                       const rw::math::Rotation3D<double>& rb)
{
    rw::math::Rotation3D<double> out;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            double s = 0.0;
            for (int k = 0; k < 3; ++k) { s += ra(i, k) * rb(k, j); }
            out(i, j) = s;
        }
    }
    return out;
}

/// T·T（逐元素——R 部 refRotMul、平移＝Ra·pb＋pa；冒烟纪律同上）。
rw::math::Transform3D<double> refTransformMul(const rw::math::Transform3D<double>& ta,
                                              const rw::math::Transform3D<double>& tb)
{
    const rw::math::Rotation3D<double> r = refRotMul(ta.R(), tb.R());
    const rw::math::Vector3D<double> pb = tb.P();
    const rw::math::Vector3D<double> p = refRotVec(ta.R(), pb) + ta.P();
    return rw::math::Transform3D<double>(p, r);
}

/// 单步 DH 变换 T_{i-1,i}(q_model=0)＝Rot_z(θ)·Trans_z(d)·Trans_x(a)·
/// Rot_x(α)（F-590 零位烘焙纪律：zeroOffset 不入几何——q0 参数保留是为
/// 参考实现可复用于"含零位旋转"的对照面，展开期望恒传 0；F-631 后本式
/// 仍为"标准 DH 级联"的解析参考——一般位姿 FK 对照的级联侧）。
rw::math::Transform3D<double> refStep(double thetaOffset, double q0, double d,
                                      double a, double alpha)
{
    // 乘积平移＝Rz(θ+q0)·(a,0,d)（Rot_z 在左——携带平移）；旋转＝Rz·Rx。
    return rw::math::Transform3D<double>(
        refRotVec(refRotZ(thetaOffset + q0), rw::math::Vector3D<double>(a, 0.0, d)),
        refRotMul(refRotZ(thetaOffset + q0), refRotX(alpha)));
}

/// 纯 z 螺旋核心 Rz(θ)·Tz(d)（F-631 拆分语义——测试参考实现：平移恒
/// (0,0,d)，Rz 不动 z 轴点；与产品 dhCoreTransform 同式、无共享代码）。
rw::math::Transform3D<double> refCore(double theta, double d)
{
    return rw::math::Transform3D<double>(
        refRotVec(refRotZ(theta), rw::math::Vector3D<double>(0.0, 0.0, d)),
        refRotZ(theta));
}

/// 连杆静段 Tx(a)·Rx(α)（F-631 拆分语义——平移 (a,0,0)、旋转 Rx(α)；
/// 与产品 dhStaticSegment 同式、无共享代码）。
rw::math::Transform3D<double> refStatic(double a, double alpha)
{
    return rw::math::Transform3D<double>(
        rw::math::Vector3D<double>(a, 0.0, 0.0), refRotX(alpha));
}

/// Rodrigues 轴角旋转 R(k,ψ)（k 单位向量、ψ rad——一般位姿 FK 对照的
/// 运行时组合序参考；逐元素 Rodrigues 公式，与产品 axisAngleRotation
/// 同式、无共享代码）。
rw::math::Rotation3D<double> refAxisAngle(const rw::math::Vector3D<double>& k,
                                          double psi)
{
    const double c = std::cos(psi);
    const double s = std::sin(psi);
    const double t = 1.0 - c;
    const double kx = k[0];
    const double ky = k[1];
    const double kz = k[2];
    return rw::math::Rotation3D<double>(
        c + t * kx * kx,      t * kx * ky - s * kz, t * kx * kz + s * ky,
        t * kx * ky + s * kz, c + t * ky * ky,      t * ky * kz - s * kx,
        t * kx * kz - s * ky, t * ky * kz + s * kx, c + t * kz * kz);
}

/**
 * F-631 拆分语义的闭式期望（测试内另行实现——与产品无共享代码）：
 *   origin_i = [Tx(a_{i-1})Rx(α_{i-1})]·Rz(θ_i)·Tz(d_i)
 *              ·[i＝末关：Tx(a_i)Rx(α_i)]
 *   axis_i   = (0,0,1)（中间关节——头部静段 Rx(α_{i-1}) 与累积回拉
 *              Rx(−α_{i-1}) 相消）｜(0, sinα_i, cosα_i)（末关节——尾部
 *              静段 Rx(α_i) 由 axis 吸收）。
 * 推导依据：运行时 origin·R(axis,ψ) 转轴过 origin 平移后的点，标准 DH
 * 单步转轴 z_{i-1} 过父帧原点——origin 平移必须沿转轴本身；链尾静段落
 * 末关节尾部使零位累积 Π origin 严格等于标准级联（units/modeling.md
 * §7.4 F-631 增量修订）。
 */
void refSplitExpansion(const DhChain& chain,
                       std::vector<rw::math::Transform3D<double>>& refOrigin,
                       std::vector<rw::math::Vector3D<double>>& refAxis)
{
    const std::size_t n = chain.joints.size();
    rw::math::Transform3D<double> prevStatic(
        rw::math::Vector3D<double>(0.0, 0.0, 0.0),
        rw::math::Rotation3D<double>(1, 0, 0, 0, 1, 0, 0, 0, 1));
    refOrigin.clear();
    refAxis.clear();
    for (std::size_t i = 0; i < n; ++i) {
        const DhChainJoint& j = chain.joints[i];
        const bool isLast = (i + 1 == n);
        rw::math::Transform3D<double> origin = prevStatic;
        origin = refTransformMul(origin, refCore(j.dh.thetaOffset, j.dh.d));
        if (isLast) {
            origin = refTransformMul(origin, refStatic(j.dh.a, j.dh.alpha));
        }
        refOrigin.push_back(origin);
        refAxis.push_back(isLast ? rw::math::Vector3D<double>(
                                       0.0, std::sin(j.dh.alpha),
                                       std::cos(j.dh.alpha))
                                 : rw::math::Vector3D<double>(0.0, 0.0, 1.0));
        prevStatic = refStatic(j.dh.a, j.dh.alpha);
    }
}

/// 位姿逐元素近似相等（gtest 辅助——附录 D C4 逐元素比较精神）。
void expectPoseNear(const JointPose& actual, const rw::math::Transform3D<double>& expected,
                    double tol, const char* what)
{
    const rw::math::Transform3D<double> t = static_cast<rw::math::Transform3D<double>>(actual);
    for (int i = 0; i < 3; ++i) {
        ASSERT_NEAR(t.P()[i], expected.P()[i], tol) << what << " 平移分量 " << i;
    }
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            ASSERT_NEAR(t.R()(r, c), expected.R()(r, c), tol)
                << what << " 旋转 (" << r << "," << c << ")";
        }
    }
}

void expectVecNear(const rw::math::Vector3D<double>& actual,
                   const rw::math::Vector3D<double>& expected, double tol,
                   const char* what)
{
    for (int i = 0; i < 3; ++i) {
        ASSERT_NEAR(actual[i], expected[i], tol) << what << " 分量 " << i;
    }
}

/**
 * 由 DH 链经被测展开产出显式关节夹具（Exact 判定样例的构造基——产物即
 * "DH 可精确表达的显式链"）。
 */
std::vector<JointEntry> expandedExplicitJoints(const DhChain& chain,
                                               std::vector<core::DiagnosticRecord>& diags)
{
    const DhExplicitConverter converter;
    const ExpandOutcome expanded = converter.dhToExplicit(chain, diags);
    EXPECT_TRUE(expanded.ok);
    return expanded.joints;
}

/// 诊断集中是否含指定稳定码。
bool hasDiagnostic(const std::vector<core::DiagnosticRecord>& diags,
                   const std::string& code)
{
    for (const core::DiagnosticRecord& r : diags) {
        if (r.code == code) { return true; }
    }
    return false;
}

}  // namespace

// =====================================================================
// acceptance 1：DH→显式无损展开（§7.4 公式/零位对齐/基座混入拒绝）
// =====================================================================

/**
 * ACC1 无损展开主用例：三关节非平凡 DH 链（含非零 θ 偏置与零位偏置）——
 * F-631 拆分语义闭式期望逐关节对照：origin=[Tx(a_{i-1})Rx(α_{i-1})]·
 * Rz(θ)·Tz(d)·[末关：Tx(a_i)Rx(α_i)]、axis=ez（中间）｜(0,sinα,cosα)
 * （末关节）；身份/名称/类型/zeroOffset/限位透传；axis 范数恒 1
 * （§9.4.7 @post）。
 *
 * F-590 重钉：展开几何为零位烘焙纪律下的 q_model=0 位姿——参考核心取
 * Rot_z(θ)（zeroOffset 不入几何；权威零位旋转的唯一烘焙归口在
 * modeling→Description 映射的单一折叠）。
 */
TEST(MdlDhConvert, DhToExplicitAccumulatesCardFormula_WP13T09_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-10", "MDL-02"},
                  std::vector<std::string>{"AT-16"});

    DhChain chain;
    chain.joints.push_back(makeDhEntry(core::ObjectId::generate(), "J1",
                                       0.3, 0.15, 0.40, kPi / 2, 0.10));
    chain.joints.push_back(makeDhEntry(core::ObjectId::generate(), "J2",
                                       -0.2, 0.05, 0.30, kPi / 3, -0.05));
    chain.joints.push_back(makeDhEntry(core::ObjectId::generate(), "J3",
                                       1.1, 0.25, 0.10, -kPi / 4, 0.20));

    // 独立参考：F-631 拆分语义闭式期望（refSplitExpansion——测试内另行
    // 实现，q0 恒 0——展开几何不含 zeroOffset，F-590）。
    std::vector<rw::math::Transform3D<double>> refOrigin;
    std::vector<rw::math::Vector3D<double>> refAxis;
    refSplitExpansion(chain, refOrigin, refAxis);

    std::vector<core::DiagnosticRecord> diags;
    const DhExplicitConverter converter;
    const ExpandOutcome out = converter.dhToExplicit(chain, diags);

    ASSERT_TRUE(out.ok);
    ASSERT_EQ(out.joints.size(), chain.joints.size());
    ASSERT_TRUE(diags.empty()) << "展开成功路径不产诊断（§9.4.7 @post）";
    for (std::size_t i = 0; i < out.joints.size(); ++i) {
        const JointEntry& e = out.joints[i];
        // origin＝当前步相对变换 T_parent_joint（非累积值——core.md §4.6）。
        expectPoseNear(e.origin.value(), refOrigin[i], 1e-12,
                       (std::string("joints[") + std::to_string(i) + "] origin").c_str());
        // axis＝关节系内转轴方向（ez 中间｜(0,sinα,cosα) 末关节——F-631）。
        expectVecNear(e.axis.value(), refAxis[i], 1e-12,
                      (std::string("joints[") + std::to_string(i) + "] axis").c_str());
        EXPECT_NEAR(e.axis.value().norm2(), 1.0, 1e-12) << "axis 须为单位向量（@post）";
        // 透传字段：身份/名称/类型/零位（θ_offset 与 zeroOffset 分离——
        // 几何不含 q0 旋转（F-590 零位烘焙纪律），zeroOffset 字段原样保留）。
        EXPECT_TRUE(e.objectId == chain.joints[i].objectId);
        EXPECT_EQ(e.localName, chain.joints[i].localName);
        EXPECT_EQ(e.type, JointType::Revolute);
        EXPECT_DOUBLE_EQ(e.zeroOffset, chain.joints[i].zeroOffset);
        ASSERT_TRUE(e.dhDerived.has_value());
        EXPECT_DOUBLE_EQ(e.dhDerived->thetaOffset, chain.joints[i].dh.thetaOffset);
        // 限位透传（两态均权威——§7.2）。
        ASSERT_TRUE(e.bounds.tryValue().has_value());
        EXPECT_DOUBLE_EQ(e.bounds.tryValue()->first, -kPi);
    }
}

/**
 * ACC1 零位对齐＋零位级联还原：q_model=0（＝DH 变量零位——q_authoritative
 * = q_model − zeroOffset 的建模域坐标）时——F-631 拆分语义下中间关节的
 * 显式累积位姿＝参考 DH 级联·本关节静段逆 S_i⁻¹（链帧相对 DH 帧的回拉），
 * **末关节累积＝参考级联逐位还原**（链尾静段落末关节尾部——全链 FK 严格
 * 等于标准 DH 的可执行形态）；θ_offset 与 zeroOffset 字段显式分离
 * （zeroOffset 不并入 θ 字段、不预烘入 origin——F-590）。
 */
TEST(MdlDhConvert, DhToExplicitZeroAlignment_WP13T09_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-10", "MDL-02"},
                  std::vector<std::string>{"AT-16"});

    DhChain chain;
    chain.joints.push_back(makeDhEntry(core::ObjectId::generate(), "J1",
                                       0.0, 0.1, 0.5, 0.0, 0.35));
    chain.joints.push_back(makeDhEntry(core::ObjectId::generate(), "J2",
                                       0.0, 0.2, 0.2, kPi / 2, -0.25));

    std::vector<core::DiagnosticRecord> diags;
    const std::vector<JointEntry> joints = expandedExplicitJoints(chain, diags);
    ASSERT_EQ(joints.size(), 2U);

    // q_model=0 显式 FK＝origin 累积；与参考 DH 级联（θ_offset 原值、
    // 不含 zeroOffset 旋转——F-590）比对：中间关节差链帧回拉 S_i⁻¹、
    // 末关节逐位还原（F-631 拆分语义——链尾静段落末关节尾部）。
    rw::math::Transform3D<double> explicitAcc =
        rw::math::Transform3D<double>(rw::math::Vector3D<double>(0, 0, 0),
                                      rw::math::Rotation3D<double>(1, 0, 0, 0, 1, 0, 0, 0, 1));
    rw::math::Transform3D<double> refAcc = explicitAcc;
    for (std::size_t i = 0; i < joints.size(); ++i) {
        explicitAcc = refTransformMul(explicitAcc, static_cast<rw::math::Transform3D<double>>(
                                            joints[i].origin.value()));
        const DhChainJoint& j = chain.joints[i];
        refAcc = refTransformMul(refAcc, refStep(j.dh.thetaOffset, 0.0, j.dh.d,
                                                 j.dh.a, j.dh.alpha));
        // 链帧回拉 S_i⁻¹＝Rx(−α_i)·Tx(−a_i)（平移 (−a_i,0,0)——Rx 不动 x
        // 轴点；末关节 i+1==n 时 S_i⁻¹＝恒等——级联逐位还原）。
        const bool isLast = (i + 1 == joints.size());
        const rw::math::Transform3D<double> pullBack =
            isLast ? rw::math::Transform3D<double>(
                         rw::math::Vector3D<double>(0, 0, 0),
                         rw::math::Rotation3D<double>(1, 0, 0, 0, 1, 0, 0, 0, 1))
                   : rw::math::Transform3D<double>(
                         rw::math::Vector3D<double>(-j.dh.a, 0.0, 0.0),
                         refRotX(-j.dh.alpha));
        const rw::math::Transform3D<double> expected
            = refTransformMul(refAcc, pullBack);
        expectPoseNear(JointPose(explicitAcc), expected, 1e-12,
                       (std::string("q=0 累积位姿 joints[") + std::to_string(i) + "]").c_str());
        // 分离性：zeroOffset 独立成字段，θ_offset 不被并入。
        EXPECT_DOUBLE_EQ(joints[i].zeroOffset, j.zeroOffset);
        ASSERT_TRUE(joints[i].dhDerived.has_value());
        EXPECT_DOUBLE_EQ(joints[i].dhDerived->thetaOffset, j.dh.thetaOffset);
    }
}

/**
 * ACC1 输入契约违约两态：空链→ChainEmpty；基座—世界变换混入→
 * DegenerateBase 拒绝（§7.6 隔离声明：转换只作用于关节链——M-11）；
 * 两态均不产诊断（值面承载）、不产出半成品关节。
 */
TEST(MdlDhConvert, DhToExplicitRejectsEmptyAndMixedBase_WP13T09_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-10", "MDL-22"},
                  std::vector<std::string>{"AT-16"});

    const DhExplicitConverter converter;

    // 空链。
    DhChain empty;
    std::vector<core::DiagnosticRecord> diags;
    const ExpandOutcome emptyOut = converter.dhToExplicit(empty, diags);
    EXPECT_FALSE(emptyOut.ok);
    EXPECT_EQ(emptyOut.errorCode, DhErrorCode::ChainEmpty);
    EXPECT_EQ(dhErrorCodeToken(emptyOut.errorCode), "ChainEmpty");
    EXPECT_TRUE(emptyOut.joints.empty());
    EXPECT_TRUE(diags.empty()) << "输入契约违约不产诊断（无登记码——值面承载）";

    // 基座变换混入（mixedInBaseTransform 非空—— DegenerateBase）。
    DhChain mixed;
    mixed.joints.push_back(makeDhEntry(core::ObjectId::generate(), "J1", 0.1, 0.1, 0.1, 0.0, 0.0));
    mixed.mixedInBaseTransform = rw::math::Transform3D<double>(
        rw::math::Vector3D<double>(0.0, 0.0, 0.5),
        rw::math::Rotation3D<double>(1, 0, 0, 0, 1, 0, 0, 0, 1));
    std::vector<core::DiagnosticRecord> diags2;
    const ExpandOutcome mixedOut = converter.dhToExplicit(mixed, diags2);
    EXPECT_FALSE(mixedOut.ok);
    EXPECT_EQ(mixedOut.errorCode, DhErrorCode::DegenerateBase);
    EXPECT_EQ(dhErrorCodeToken(mixedOut.errorCode), "DegenerateBase");
    EXPECT_TRUE(mixedOut.joints.empty());
    EXPECT_TRUE(diags2.empty());
}

// =====================================================================
// acceptance 2：五状态各含样例（结构检查先行＋逐关节逐项上界判定）
// =====================================================================

/**
 * ACC2 NotExpressible 终判样例：prismatic 关节在链中→第一阶结构检查
 * （解析，先于一切数值求解）终判不进求解——parameters 空、无求解痕迹；
 * MDL-DH-NOT-EXPRESSIBLE（error 级）诊断 subject＝违规关节精确定位。
 * fixed 连接同轨（非单自由度旋转）。
 */
TEST(MdlDhConvert, ExplicitToDhNotExpressibleTerminal_WP13T09_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-10"},
                  std::vector<std::string>{"AT-16"});

    const DhExplicitConverter converter;

    // prismatic 在链中（第 2 关节）。
    std::vector<JointEntry> joints;
    joints.push_back(makeExplicitJoint(core::ObjectId::generate(), "J1",
                                       rw::math::Vector3D<double>(0, 0, 1), JointPose{}, 0.0));
    JointEntry prism = makeExplicitJoint(core::ObjectId::generate(), "J2",
                                         rw::math::Vector3D<double>(0, 1, 0), JointPose{}, 0.0);
    prism.type = JointType::Prismatic;
    joints.push_back(prism);
    joints.push_back(makeExplicitJoint(core::ObjectId::generate(), "J3",
                                       rw::math::Vector3D<double>(1, 0, 0), JointPose{}, 0.0));

    std::vector<core::DiagnosticRecord> diags;
    const DhConversionResult result = converter.explicitToDh(joints, diags);

    // 终判：五态互斥中的 NotExpressible；无参数产出（不进入求解）。
    EXPECT_EQ(result.determination, DhDetermination::NotExpressible);
    EXPECT_EQ(dhDeterminationToken(result.determination), "NotExpressible");
    EXPECT_TRUE(result.parameters.empty());
    EXPECT_TRUE(result.solutionSet.empty());
    EXPECT_TRUE(result.deviations.empty());
    // 终判诊断：error 级码面＋subject＝违规关节（prismatic 所在——J2）。
    ASSERT_EQ(diags.size(), 1U);
    EXPECT_EQ(diags[0].code, std::string(kMdlDhNotExpressible));
    ASSERT_TRUE(diags[0].subject.has_value());
    EXPECT_TRUE(*diags[0].subject == joints[1].objectId) << "定位＝prismatic 关节";
    ASSERT_TRUE(diags[0].localName.has_value());
    EXPECT_EQ(*diags[0].localName, "J2");

    // fixed 连接同轨（结构前提同样不满足）。
    std::vector<JointEntry> fixedChain;
    fixedChain.push_back(makeExplicitJoint(core::ObjectId::generate(), "J1",
                                           rw::math::Vector3D<double>(0, 0, 1), JointPose{}, 0.0));
    fixedChain[0].type = JointType::Fixed;
    std::vector<core::DiagnosticRecord> diags2;
    const DhConversionResult result2 = converter.explicitToDh(fixedChain, diags2);
    EXPECT_EQ(result2.determination, DhDetermination::NotExpressible);
    EXPECT_TRUE(result2.parameters.empty());
    EXPECT_EQ(diags2.size(), 1U);
    EXPECT_EQ(diags2[0].code, std::string(kMdlDhNotExpressible));

    // continuous 未确认工作范围：不视同旋转（MDL-12 确认后方视同）。
    std::vector<JointEntry> contChain;
    contChain.push_back(makeExplicitJoint(core::ObjectId::generate(), "J1",
                                          rw::math::Vector3D<double>(0, 0, 1), JointPose{}, 0.0));
    contChain[0].type = JointType::Continuous;  // workingRange 保持 NotProvided
    std::vector<core::DiagnosticRecord> diags3;
    const DhConversionResult result3 = converter.explicitToDh(contChain, diags3);
    EXPECT_EQ(result3.determination, DhDetermination::NotExpressible);
}

/**
 * ACC2 Exact 样例（标准 DH 约定可表达链）：由已知 DH 链展开的显式关节
 * 求解→Exact；选定解逐关节逐项回收原 DH 参数（附录 D 第 5 项上界内）；
 * 判定偏差全项在容差内、收敛态 Converged、无诊断产出。
 */
TEST(MdlDhConvert, ExplicitToDhExactOnDhDerivedChain_WP13T09_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-10"},
                  std::vector<std::string>{"AT-16"});

    // 非平凡六轴样例（UR 类拓扑——全轴不平行/不重合；★ 末关节给非零
    // a（工具偏距）：在卡面目标集（原点+z 轴）下，末关节 a=0 会使 θ_n
    // 不可辨识（绕自身 z 的旋转不改变原点与 z 轴）——那类链属
    // ExactNonUnique，见平行/重合轴用例）。
    DhChain chain;
    chain.joints.push_back(makeDhEntry(core::ObjectId::generate(), "J1",
                                       0.0, 0.15, 0.10, kPi / 2, 0.05));
    chain.joints.push_back(makeDhEntry(core::ObjectId::generate(), "J2",
                                       kPi / 2, 0.0, 0.45, 0.0, 0.0));
    chain.joints.push_back(makeDhEntry(core::ObjectId::generate(), "J3",
                                       0.0, 0.0, 0.12, kPi / 2, 0.0));
    chain.joints.push_back(makeDhEntry(core::ObjectId::generate(), "J4",
                                       kPi / 2, 0.35, 0.0, kPi / 2, 0.0));
    chain.joints.push_back(makeDhEntry(core::ObjectId::generate(), "J5",
                                       0.0, 0.0, 0.0, -kPi / 2, 0.0));
    chain.joints.push_back(makeDhEntry(core::ObjectId::generate(), "J6",
                                       -kPi / 2, 0.08, 0.05, 0.0, 0.0));

    std::vector<core::DiagnosticRecord> expandDiags;
    const std::vector<JointEntry> joints = expandedExplicitJoints(chain, expandDiags);
    ASSERT_EQ(joints.size(), 6U);

    const DhExplicitConverter converter;
    std::vector<core::DiagnosticRecord> diags;
    const DhConversionResult result = converter.explicitToDh(joints, diags);

    // 判定：ExactNonUnique（F-631 拆分语义下的诚实一阶结论——该腕部构型
    // （J5 的 α=−π/2 与 J6 的 a/d 结构）使末关节 θ 与倒数第二关节 a 在
    // （原点+z 轴）目标集下一阶不可分：δ=(θ_5+=ε, a_4−=a_5·ε) 为雅可比
    // 零方向；二阶残差 ~a·ε² 在 ε≈1e-4 时 ≈2.5e-10＜1e-9——容差内存在
    // 可分辨第二解，按 §7.5 判据归 ExactNonUnique；字典序钉值重解因二阶
    // 残差超差被拒（接受门＝语义门），选定解保持真值参数（下断言）。
    EXPECT_EQ(result.determination, DhDetermination::ExactNonUnique);
    EXPECT_EQ(dhDeterminationToken(result.determination), "ExactNonUnique");
    EXPECT_FALSE(result.solutionSet.empty());
    // 自由坐标＝{末关节 θ（坐标 20）、末关节 α（坐标 23）}（4×关节＋
    // {0:θ,1:d,2:a,3:α} 字典序——5 号关节的 θ/α 槽）。
    ASSERT_EQ(result.freeCoordinates.size(), 2U);
    EXPECT_EQ(result.freeCoordinates[0], 20U);
    EXPECT_EQ(result.freeCoordinates[1], 23U);
    EXPECT_EQ(result.convergence, DhConvergenceState::Converged);
    // 参数级回收（roundtrip 参数级一致性的单侧验证——钉值重解被语义门
    // 拒绝后选定解保持真值；δθ/δd/δa/δα 逐项独立断言）。
    ASSERT_EQ(result.parameters.size(), 6U);
    for (std::size_t i = 0; i < 6; ++i) {
        const DhParameters& p = result.parameters[i];
        const DhParameters& ref = chain.joints[i].dh;
        EXPECT_NEAR(p.thetaOffset, ref.thetaOffset, 1e-9) << "θ_offset 关节 " << i;
        EXPECT_NEAR(p.d, ref.d, 1e-9) << "d 关节 " << i;
        EXPECT_NEAR(p.a, ref.a, 1e-9) << "a 关节 " << i;
        EXPECT_NEAR(p.alpha, ref.alpha, 1e-9) << "α 关节 " << i;
    }
    // 判定偏差明细全项在容差内（C3 逐关节逐项）＋无诊断。
    ASSERT_EQ(result.deviations.size(), 6U);
    for (std::size_t i = 0; i < 6; ++i) {
        EXPECT_LE(result.deviations[i].axisAngleDeviation, 1e-9);
        EXPECT_LE(result.deviations[i].originPositionDeviation, 1e-9);
    }
    EXPECT_TRUE(diags.empty()) << "Exact 正路径不产诊断";
}

/**
 * ACC2/ACC3 ExactNonUnique 样例（重合轴退化族）：a=0 且 α=0 的相邻关节
 * →绕同一轴线的旋转不可分辨→解族存在→ExactNonUnique；自由参数按字典
 * 序规则定中性值 0（freeCoordinates 记录字典序坐标）；解集报告非空；
 * 重复调用逐字段相等（NFR-COR-02 禁止随机挑选）。
 */
TEST(MdlDhConvert, ExplicitToDhExactNonUniqueCoincidentAxes_WP13T09_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-10", "NFR-COR-02"},
                  std::vector<std::string>{"AT-16"});

    // 两关节共线（α1=α2=0 且 a1=a2=0）——θ1/θ2 均为自由参数（绕同一
    // 轴线的旋转不改变累积原点与 z 轴）。
    DhChain chain;
    chain.joints.push_back(makeDhEntry(core::ObjectId::generate(), "J1",
                                       0.7, 0.30, 0.0, 0.0, 0.0));
    chain.joints.push_back(makeDhEntry(core::ObjectId::generate(), "J2",
                                       1.1, 0.25, 0.0, 0.0, 0.0));

    std::vector<core::DiagnosticRecord> expandDiags;
    const std::vector<JointEntry> joints = expandedExplicitJoints(chain, expandDiags);

    const DhExplicitConverter converter;
    std::vector<core::DiagnosticRecord> diags;
    const DhConversionResult result = converter.explicitToDh(joints, diags);

    // 判定：ExactNonUnique（退化族）。
    EXPECT_EQ(result.determination, DhDetermination::ExactNonUnique);
    EXPECT_EQ(dhDeterminationToken(result.determination), "ExactNonUnique");
    EXPECT_EQ(result.convergence, DhConvergenceState::Converged);
    // 几何量回收：d/a/α 逐项在容差内（θ 属自由族——不逐一断言）。
    ASSERT_EQ(result.parameters.size(), 2U);
    EXPECT_NEAR(result.parameters[0].d, 0.30, 1e-9);
    EXPECT_NEAR(result.parameters[1].d, 0.25, 1e-9);
    EXPECT_NEAR(result.parameters[0].a, 0.0, 1e-9);
    EXPECT_NEAR(result.parameters[0].alpha, 0.0, 1e-9);
    // 字典序定值：自由坐标 θ1=0、θ2=0（坐标 0 与 4——4×关节+0 布局）。
    ASSERT_EQ(result.freeCoordinates.size(), 2U);
    EXPECT_EQ(result.freeCoordinates[0], 0U);  // θ1（字典序在前）
    EXPECT_EQ(result.freeCoordinates[1], 4U);  // θ2
    EXPECT_DOUBLE_EQ(result.parameters[0].thetaOffset, 0.0);
    EXPECT_DOUBLE_EQ(result.parameters[1].thetaOffset, 0.0);
    // 解集报告：首解＋定值后选解（≥2 证人——禁止随机挑选的可见面）。
    ASSERT_GE(result.solutionSet.size(), 2U);
    // 正路径无诊断（ExactNonUnique 是有效工程结论非错误）。
    EXPECT_TRUE(diags.empty());
    // 确定性：同输入重复调用逐字段相等（NFR-COR-02）。
    std::vector<core::DiagnosticRecord> diagsRepeat;
    const DhConversionResult repeat = converter.explicitToDh(joints, diagsRepeat);
    EXPECT_TRUE(result == repeat);
}

/**
 * ACC3 平行相邻轴退化族：α=0（平行）→ExactNonUnique＋字典序定值；
 * 确定性求解不读时钟/环境（两次调用同解同诊断序）。
 */
TEST(MdlDhConvert, ExplicitToDhExactNonUniqueParallelAxes_WP13T09_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-10", "NFR-COR-02"},
                  std::vector<std::string>{"AT-16"});

    // 平行相邻轴（α2=0）且第二关节 a=0：θ2 对（原点+z 轴）目标不可辨识
    // （绕公共轴的旋转不改变任何帧的原点与 z 轴）——退化族→ExactNonUnique。
    DhChain chain;
    chain.joints.push_back(makeDhEntry(core::ObjectId::generate(), "J1",
                                       0.4, 0.10, 0.30, 0.0, 0.0));
    chain.joints.push_back(makeDhEntry(core::ObjectId::generate(), "J2",
                                       0.9, 0.20, 0.0, 0.0, 0.0));

    std::vector<core::DiagnosticRecord> expandDiags;
    const std::vector<JointEntry> joints = expandedExplicitJoints(chain, expandDiags);

    const DhExplicitConverter converter;
    std::vector<core::DiagnosticRecord> diags;
    const DhConversionResult result = converter.explicitToDh(joints, diags);

    EXPECT_EQ(result.determination, DhDetermination::ExactNonUnique);
    EXPECT_FALSE(result.freeCoordinates.empty()) << "平行相邻轴（末级 a=0）必存在自由族";
    // 自由坐标（F-631 拆分语义）：θ2（坐标 4——绕公共轴旋转不改变目标，
    // 经典自由族）＋a2（坐标 6——a_1 前移入末步头部后，θ_1/a_2 等构成绕
    // 公共轴的等价补偿族，字典序贪心取 a_2 为自由代表；a_2 输入即 0，
    // 钉值平凡接受）。θ1 由 E_1 位置目标钉死（a_0=0.3≠0 断开 θ_0 族）。
    ASSERT_EQ(result.freeCoordinates.size(), 2U);
    EXPECT_EQ(result.freeCoordinates[0], 4U);  // θ2（字典序在前）
    EXPECT_EQ(result.freeCoordinates[1], 6U);  // a2（F-631 新增自由代表）
    // 字典序定值：自由坐标取中性值 0。
    for (const std::size_t coord : result.freeCoordinates) {
        const std::size_t joint = coord / 4;
        const std::size_t k = coord % 4;
        if (k == 0U) {
            EXPECT_DOUBLE_EQ(result.parameters[joint].thetaOffset, 0.0);
        } else {
            ASSERT_EQ(k, 2U) << "本样例自由坐标应为 θ（k=0）或 a（k=2）";
            EXPECT_DOUBLE_EQ(result.parameters[joint].a, 0.0);
        }
    }
    // 几何回收：间距（a）与偏距（d）在容差内。
    EXPECT_NEAR(result.parameters[0].a, 0.30, 1e-9);
    EXPECT_NEAR(result.parameters[0].d, 0.10, 1e-9);
    EXPECT_NEAR(result.parameters[1].d, 0.20, 1e-9);
    // 确定性：重复调用同解同诊断（NFR-COR-02——不读时钟/环境）。
    std::vector<core::DiagnosticRecord> diagsRepeat;
    const DhConversionResult repeat = converter.explicitToDh(joints, diagsRepeat);
    EXPECT_TRUE(result == repeat);
    EXPECT_TRUE(diagsRepeat.size() == diags.size());
}

/**
 * ACC3 Approximate 样例：显式链不可被 DH 精确表达（原点位置微扰 1e-6 m
 * ——第一关节的 θ 被位置钉死后轴不可达，扰动不可吸收）→收敛但逐项超
 * 附录 D 第 5 项上界→Approximate：附 E 度量（>0）与收敛态；诊断
 * MDL-DH-APPROXIMATE（Warning）带比较型三要素；parameters 仅为近似参考
 * （C-4 不得成为权威 DH——权威资格由 determination 承载，消费方据其拒绝）。
 */
TEST(MdlDhConvert, ExplicitToDhApproximateNotAuthority_WP13T09_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-10"},
                  std::vector<std::string>{"AT-16"});

    DhChain chain;
    chain.joints.push_back(makeDhEntry(core::ObjectId::generate(), "J1",
                                       0.3, 0.15, 0.40, kPi / 2, 0.0));
    chain.joints.push_back(makeDhEntry(core::ObjectId::generate(), "J2",
                                       0.8, 0.10, 0.25, -kPi / 3, 0.0));

    std::vector<core::DiagnosticRecord> expandDiags;
    std::vector<JointEntry> joints = expandedExplicitJoints(chain, expandDiags);
    // 不可吸收扰动：J1 原点沿 x 平移 1e-6 m（θ1 被位置唯一钉死——轴目标
    // 不变的组合不可达，见用例注；残差 ~1e-6 ≫ 1e-9）。
    const rw::math::Transform3D<double> moved =
        static_cast<rw::math::Transform3D<double>>(joints[0].origin.value());
    joints[0].origin = core::SourcedValue<JointPose>::provided(
        JointPose(rw::math::Transform3D<double>(
            moved.P() + rw::math::Vector3D<double>(1e-6, 0.0, 0.0), moved.R())),
        userProvenance());

    const DhExplicitConverter converter;
    std::vector<core::DiagnosticRecord> diags;
    const DhConversionResult result = converter.explicitToDh(joints, diags);

    // 判定：Approximate（收敛但超差）。
    EXPECT_EQ(result.determination, DhDetermination::Approximate);
    EXPECT_EQ(dhDeterminationToken(result.determination), "Approximate");
    EXPECT_EQ(result.convergence, DhConvergenceState::Converged) << "近似的前提是收敛";
    // E 度量（呈现指标）> 0——达到的误差值被报告（MDL-10 原文）。
    EXPECT_GT(result.errorMetricE, 0.0);
    // 逐关节逐项明细中至少一项超上界（C3——判定与 E 分离）。
    bool anyOver = false;
    for (const DhJointDeviation& dev : result.deviations) {
        anyOver = anyOver
                  || dev.axisAngleDeviation > 1e-9
                  || dev.originPositionDeviation > 1e-9;
    }
    EXPECT_TRUE(anyOver);
    // 诊断：MDL-DH-APPROXIMATE（Warning）＋比较型三要素。
    ASSERT_EQ(diags.size(), 1U);
    EXPECT_EQ(diags[0].code, std::string(kMdlDhApproximate));
    ASSERT_TRUE(diags[0].comparison.has_value()) << "须附比较型三要素";
    // 近似参考参数存在，但 determination≠Exact 族＝无权威资格（C-4——
    // 权威切换域门据此拒绝，见 DhConvertEquivalenceTest 的命令面用例）。
    EXPECT_EQ(result.parameters.size(), 2U);
}

/**
 * ACC3 AnalysisFailed 样例：有限但量级 1e200 m 的原点目标（结构检查
 * 通过）→正规方程溢出→求解器数值失败→AnalysisFailed：不构成语义结论
 * （无参数产出、收敛态 Diverged）；诊断 MDL-DH-ANALYSIS-FAILED（error）。
 */
TEST(MdlDhConvert, ExplicitToDhAnalysisFailedNotSemantic_WP13T09_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-10"},
                  std::vector<std::string>{"AT-16"});

    // 结构合法（有限、可归一化）但数值溢出的链——1e200² 超出 double 值域。
    std::vector<JointEntry> joints;
    JointEntry j1 = makeExplicitJoint(core::ObjectId::generate(), "J1",
                                      rw::math::Vector3D<double>(0, 0, 1), JointPose{}, 0.0);
    j1.origin = core::SourcedValue<JointPose>::provided(
        JointPose(rw::math::Transform3D<double>(
            rw::math::Vector3D<double>(1e200, 0.0, 0.0),
            rw::math::Rotation3D<double>(1, 0, 0, 0, 1, 0, 0, 0, 1))),
        userProvenance());
    joints.push_back(j1);
    joints.push_back(makeExplicitJoint(core::ObjectId::generate(), "J2",
                                       rw::math::Vector3D<double>(0, 1, 0),
                                       JointPose(rw::math::Transform3D<double>(
                                           rw::math::Vector3D<double>(0.0, 1e200, 0.0),
                                           rw::math::Rotation3D<double>(1, 0, 0, 0, 1, 0, 0, 0, 1))),
                                       0.0));

    const DhExplicitConverter converter;
    std::vector<core::DiagnosticRecord> diags;
    const DhConversionResult result = converter.explicitToDh(joints, diags);

    // 判定：AnalysisFailed（数值失败轴——不构成语义结论）。
    EXPECT_EQ(result.determination, DhDetermination::AnalysisFailed);
    EXPECT_EQ(dhDeterminationToken(result.determination), "AnalysisFailed");
    EXPECT_EQ(result.convergence, DhConvergenceState::Diverged);
    EXPECT_TRUE(result.parameters.empty()) << "数值失败不产出可用参数";
    EXPECT_TRUE(result.solutionSet.empty());
    EXPECT_TRUE(result.deviations.empty());
    // 诊断：MDL-DH-ANALYSIS-FAILED。
    ASSERT_EQ(diags.size(), 1U);
    EXPECT_EQ(diags[0].code, std::string(kMdlDhAnalysisFailed));
}

// =====================================================================
// F-631 一般位姿 FK 等价（审计二轮最高优先修复的自证面——拆分语义下
 // explicit 链 FK 对照标准 DH 解析式级联；修复前转轴过子原点、一般位姿
// 位置误差 2a·sin(θ/2)，a=0 时逐位一致故既有用例不可检出）
// =====================================================================

/**
 * 一般位姿 FK 组合序（F-631 拆分语义的显式链 FK 消费面）：逐关节
 * acc·=origin_i·R(ez, zeroOffset_i+q_i)，其中**末关节 origin 拆分**为
 * [纯段]·R(ez,ψ)·[尾段]（纯段/尾段由展开 origin 与输入静段的分解一致
 * 性先行校验；旋转统一绕纯段关节系 z——推导 pure⁻¹·级联·tail⁻¹＝Rz(ψ)）
 * ——全链恒等于标准级联 Π Rz(θ_total)Tz(d)Tx(a)Rx(α)。
 *
 * 语义边界注：末关节存储 axis＝Rx(−α)·ez 服务运行时未拆分组合
 * origin·R(axis,ψ) 的物理转轴方向（世界系＝z_{n-1}，F-591 口径）；拆分序
 * 的全链 FK 恢复路径绕纯段 z 旋转（本 helper）——两者的差异即运行时单
 * 关节表达的定理性残差（用例④量化为 2a·sin(q/2)）。
 */
rw::math::Transform3D<double> refSplitFk(
    const DhChain& chain, const std::vector<JointEntry>& joints,
    const std::vector<double>& q)
{
    const std::size_t n = chain.joints.size();
    rw::math::Transform3D<double> acc(
        rw::math::Vector3D<double>(0.0, 0.0, 0.0),
        rw::math::Rotation3D<double>(1, 0, 0, 0, 1, 0, 0, 0, 1));
    for (std::size_t i = 0; i < n; ++i) {
        const DhChainJoint& j = chain.joints[i];
        const bool isLast = (i + 1 == n);
        acc = refTransformMul(acc, static_cast<rw::math::Transform3D<double>>(
                                        joints[i].origin.value()));
        if (isLast) {
            // 末关节拆分：先把已乘入的 origin 中的尾部静段撤出（左乘其逆
            // ——Tx(a)Rx(α) 的逆＝Rx(−α)Tx(−a)），旋转后再补回（组合序＝
            // [纯段]·R·[尾段]）。
            const rw::math::Transform3D<double> tailInverse(
                rw::math::Vector3D<double>(-j.dh.a, 0.0, 0.0),
                refRotX(-j.dh.alpha));
            acc = refTransformMul(acc, tailInverse);
        }
        const rw::math::Rotation3D<double> rot = refAxisAngle(
            rw::math::Vector3D<double>(0.0, 0.0, 1.0), j.zeroOffset + q[i]);
        acc = refTransformMul(
            acc, rw::math::Transform3D<double>(rw::math::Vector3D<double>(0, 0, 0), rot));
        if (isLast) {
            acc = refTransformMul(acc, refStatic(j.dh.a, j.dh.alpha));
        }
    }
    return acc;
}

/**
 * F-631 ①单关节一般位姿 FK：a=0.5、d=0.2、α=π/3、θ_offset=0.3，q∈
 * {0, 0.5, 1.1, π}——展开 origin 拆分（纯段 Rz·Tz＋尾段 Tx·Rx）后按一般
 * 位姿组合序对照标准 DH 解析式 T＝Rz(θ_offset+zeroOffset+q)·Tz(d)·Tx(a)·
 * Rx(α) 级联：位置与姿态误差各 <1e-12。修复机理自证：原实现把 Tx(a)Rx(α)
 * 烘入 origin 尾部，运行时 origin·R(axis,q) 组合转轴过子原点——本用例在
 * a≠0 一般位姿处锁定拆分语义严格还原级联（AT-16/MDL-10；audit F-631）。
 */
TEST(MdlDhConvert, F631SingleJointGeneralQFkMatchesCascade)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-10", "MDL-02"},
                  std::vector<std::string>{"AT-16"});

    DhChain chain;
    chain.joints.push_back(makeDhEntry(core::ObjectId::generate(), "J1",
                                       0.3, 0.2, 0.5, kPi / 3, 0.0));

    std::vector<core::DiagnosticRecord> diags;
    const std::vector<JointEntry> joints = expandedExplicitJoints(chain, diags);
    ASSERT_EQ(joints.size(), 1U);

    // 拆分一致性前置：展开 origin 必须恰为 [纯段 Rz·Tz]·[尾段 Tx·Rx]——
    // 一般位姿组合序的可分解性由展开几何保证（F-631 落位推导）。
    const DhChainJoint& j = chain.joints[0];
    const rw::math::Transform3D<double> expectedOrigin = refTransformMul(
        refCore(j.dh.thetaOffset, j.dh.d), refStatic(j.dh.a, j.dh.alpha));
    expectPoseNear(joints[0].origin.value(), expectedOrigin, 1e-12,
                   "单关节 origin＝纯段·尾段（F-631 拆分存储）");

    const double qs[] = {0.0, 0.5, 1.1, kPi};
    for (const double q : qs) {
        SCOPED_TRACE((std::string("q=") + std::to_string(q)).c_str());
        // 显式链 FK（拆分组合序——末关节撤尾/旋转/补尾）。
        const rw::math::Transform3D<double> fk = refSplitFk(chain, joints, {q});
        // 标准 DH 解析级联（θ 总＝θ_offset+zeroOffset+q——零位分离 F-590）。
        const rw::math::Transform3D<double> cascade
            = refStep(j.dh.thetaOffset + j.zeroOffset + q, 0.0, j.dh.d,
                      j.dh.a, j.dh.alpha);
        expectPoseNear(JointPose(fk), cascade, 1e-12, "单关节一般位姿 FK 对照级联");
    }
}

/**
 * F-631 ②两关节一般位姿 FK：a₁=a₂=0.3、非平凡 θ/d/α（含 π/2 扭转），
 * 一般 q 组合 {(0.2,−0.4), (1.0,0.7), (π/2,π/3)}——拆分组合序 FK 对照
 * 标准级联级联 <1e-12（位置/姿态）。中间静段经 origin 头部落位、链尾
 * 静段落末关节尾部——全链逐关节严格还原级联（audit F-631）。
 */
TEST(MdlDhConvert, F631TwoJointGeneralQFkMatchesCascade)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-10", "MDL-02"},
                  std::vector<std::string>{"AT-16"});

    DhChain chain;
    chain.joints.push_back(makeDhEntry(core::ObjectId::generate(), "J1",
                                       0.2, 0.15, 0.3, kPi / 2, 0.05));
    chain.joints.push_back(makeDhEntry(core::ObjectId::generate(), "J2",
                                       -0.4, 0.25, 0.3, -kPi / 3, -0.05));

    std::vector<core::DiagnosticRecord> diags;
    const std::vector<JointEntry> joints = expandedExplicitJoints(chain, diags);
    ASSERT_EQ(joints.size(), 2U);

    const double qSets[][2] = {{0.2, -0.4}, {1.0, 0.7}, {kPi / 2, kPi / 3}};
    for (const auto& qs : qSets) {
        SCOPED_TRACE((std::string("q=(") + std::to_string(qs[0]) + ","
                      + std::to_string(qs[1]) + ")").c_str());
        const rw::math::Transform3D<double> fk
            = refSplitFk(chain, joints, {qs[0], qs[1]});
        // 标准级联：逐步 Rz(θ_total)·Tz(d)·Tx(a)·Rx(α) 左乘累乘。
        rw::math::Transform3D<double> cascade(
            rw::math::Vector3D<double>(0.0, 0.0, 0.0),
            rw::math::Rotation3D<double>(1, 0, 0, 0, 1, 0, 0, 0, 1));
        for (std::size_t i = 0; i < 2; ++i) {
            const DhChainJoint& j = chain.joints[i];
            cascade = refTransformMul(
                cascade, refStep(j.dh.thetaOffset + j.zeroOffset + qs[i], 0.0,
                                 j.dh.d, j.dh.a, j.dh.alpha));
        }
        expectPoseNear(JointPose(fk), cascade, 1e-12, "两关节一般位姿 FK 对照级联");
    }
}

/**
 * F-631 ③a=0 链零漂移：全 a=0、α=0 链（重合轴族形态）的展开 origin/axis
 * 与**修复前黄金闭式**（Rz(θ)·Tz(d)·Tx(0)·Rx(0)——旧 dhStepTransform 同式
 * 参考逐位复算）逐位一致（EXPECT_DOUBLE_EQ 全分量）。机理：a=0 时原实现
 * 转轴过子原点与过父原点重合（2a·sin=0）、静段 Tx(0)Rx(0)＝精确恒等不
 * 改变乘积——拆分后逐位不变是本修复的零回归底线（audit F-631）。
 */
TEST(MdlDhConvert, F631ZeroADriftFreeAgainstPreFixGolden)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-10", "NFR-COR-01"},
                  std::vector<std::string>{"AT-16"});

    DhChain chain;
    chain.joints.push_back(makeDhEntry(core::ObjectId::generate(), "J1",
                                       0.7, 0.30, 0.0, 0.0, 0.0));
    chain.joints.push_back(makeDhEntry(core::ObjectId::generate(), "J2",
                                       1.1, 0.25, 0.0, 0.0, 0.0));
    chain.joints.push_back(makeDhEntry(core::ObjectId::generate(), "J3",
                                       -0.4, 0.15, 0.0, 0.0, 0.0));

    std::vector<core::DiagnosticRecord> diags;
    const std::vector<JointEntry> joints = expandedExplicitJoints(chain, diags);
    ASSERT_EQ(joints.size(), 3U);

    for (std::size_t i = 0; i < joints.size(); ++i) {
        const DhChainJoint& j = chain.joints[i];
        // 修复前闭式（旧 dhStepTransform 同式参考——a=0、α=0）。
        const rw::math::Transform3D<double> preFix
            = refStep(j.dh.thetaOffset, 0.0, j.dh.d, j.dh.a, j.dh.alpha);
        const rw::math::Transform3D<double> origin
            = static_cast<rw::math::Transform3D<double>>(joints[i].origin.value());
        SCOPED_TRACE((std::string("joints[") + std::to_string(i) + "]").c_str());
        for (int c = 0; c < 3; ++c) {
            EXPECT_EQ(origin.P()[c], preFix.P()[c]) << "origin 平移逐位（分量 " << c << "）";
        }
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                EXPECT_EQ(origin.R()(r, c), preFix.R()(r, c))
                    << "origin 旋转逐位（" << r << "," << c << "）";
            }
        }
        // axis：修复前 (0, sinα, cosα)＝(0,0,1)（α=0 精确）——逐位不变。
        const auto axis = joints[i].axis.value();
        EXPECT_EQ(axis[0], 0.0);
        EXPECT_EQ(axis[1], std::sin(j.dh.alpha));  // sin(0)=+0.0 精确
        EXPECT_EQ(axis[2], std::cos(j.dh.alpha));  // cos(0)=1.0 精确
    }
}

/**
 * F-631 ④0.5227 复现对照：a=0.5 单关节 q=1.1 的新拆分序法兰世界原点与
 * 旧实现偏差恰为 2a·sin(q/2)。推导：旧实现把 Tx(a)Rx(α) 烘入 origin 尾部
 * ——法兰系原点＝origin 旧平移＝Rz(θ_off)·(a,0,d)，对关节角 q 定常（旋转
 * 绕自身原点）；新拆分序法兰原点＝Rz(θ_off+q)·(a,0,d)（绕 z_{i-1} 轨摆
 * ——DH 语义）。两式同乘 Rz(−θ_off) 后偏差＝|u−Rz(q)·u|（u＝(a,0,d)）＝
 * |(a,0,d)−(a·cosq, a·sinq, d)|＝a·|(1−cosq, sinq, 0)|＝2a·sin(q/2)——
 * 与 d、θ_off 无关（本例 2·0.5·sin(0.55)≈0.5227 m，audit F-631 实测值）。
 */
TEST(MdlDhConvert, F631FlangeOrbitDeviation2aSinHalfQ)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-10", "MDL-02"},
                  std::vector<std::string>{"AT-16"});

    const double a = 0.5;
    const double d = 0.2;
    const double thetaOffset = 0.3;
    const double alpha = 0.4;
    const double q = 1.1;

    DhChain chain;
    chain.joints.push_back(makeDhEntry(core::ObjectId::generate(), "J1",
                                       thetaOffset, d, a, alpha, 0.0));

    std::vector<core::DiagnosticRecord> diags;
    const std::vector<JointEntry> joints = expandedExplicitJoints(chain, diags);
    ASSERT_EQ(joints.size(), 1U);

    // 新拆分序法兰原点（[纯段]·R·[尾段] 的平移分量）。
    const rw::math::Transform3D<double> newFk = refSplitFk(chain, joints, {q});
    // 旧实现法兰原点：origin 旧闭式（Rz·Tz·Tx·Rx）的平移＝Rz(θ_off)·(a,0,d)
    // ——旋转 R(axis 旧,q) 绕自身原点、不动原点（旧组合 origin·R(axis,q)）。
    const rw::math::Transform3D<double> oldOrigin = refStep(thetaOffset, 0.0, d, a, alpha);
    const double deviation
        = (newFk.P() - oldOrigin.P()).norm2();

    // 期望＝2a·sin(q/2)（推导见用例注；1e-9 吸收双精度乘积序噪声）。
    const double expected = 2.0 * a * std::sin(q / 2.0);
    EXPECT_NEAR(deviation, expected, 1e-9)
        << "新拆分序与旧实现法兰原点偏差须恰为 2a·sin(q/2)（F-631 复现）";
}
