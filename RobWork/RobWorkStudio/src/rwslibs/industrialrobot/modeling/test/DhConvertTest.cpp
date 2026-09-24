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

/// 单步 DH 变换 T_{i-1,i}(q=0)＝Rot_z(θ+q0)·Trans_z(d)·Trans_x(a)·Rot_x(α)。
rw::math::Transform3D<double> refStep(double thetaOffset, double q0, double d,
                                      double a, double alpha)
{
    // 乘积平移＝Rz(θ+q0)·(a,0,d)（Rot_z 在左——携带平移）；旋转＝Rz·Rx。
    return rw::math::Transform3D<double>(
        refRotVec(refRotZ(thetaOffset + q0), rw::math::Vector3D<double>(a, 0.0, d)),
        refRotMul(refRotZ(thetaOffset + q0), refRotX(alpha)));
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
 * 逐级累乘得 origin=T_parent_joint（当前步相对变换）、axis=z_i=
 * T_{0,i}·(0,0,1) 归一化单位向量；身份/名称/类型/zeroOffset/限位透传；
 * axis 范数恒 1（§9.4.7 @post）。
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

    // 独立参考：逐级累乘（期望值——测试内另行实现，同一卡面公式）。
    rw::math::Transform3D<double> acc =
        rw::math::Transform3D<double>(rw::math::Vector3D<double>(0, 0, 0),
                                      rw::math::Rotation3D<double>(1, 0, 0, 0, 1, 0, 0, 0, 1));
    std::vector<rw::math::Transform3D<double>> refAccum;
    std::vector<rw::math::Transform3D<double>> refStepT;
    for (const DhChainJoint& j : chain.joints) {
        const rw::math::Transform3D<double> step =
            refStep(j.dh.thetaOffset, j.zeroOffset, j.dh.d, j.dh.a, j.dh.alpha);
        refStepT.push_back(step);
        acc = refTransformMul(acc, step);
        refAccum.push_back(acc);
    }

    std::vector<core::DiagnosticRecord> diags;
    const DhExplicitConverter converter;
    const ExpandOutcome out = converter.dhToExplicit(chain, diags);

    ASSERT_TRUE(out.ok);
    ASSERT_EQ(out.joints.size(), chain.joints.size());
    ASSERT_TRUE(diags.empty()) << "展开成功路径不产诊断（§9.4.7 @post）";
    for (std::size_t i = 0; i < out.joints.size(); ++i) {
        const JointEntry& e = out.joints[i];
        // origin＝当前步相对变换 T_parent_joint（非累积值——core.md §4.6）。
        expectPoseNear(e.origin.value(), refStepT[i], 1e-12,
                       (std::string("joints[") + std::to_string(i) + "] origin").c_str());
        // axis＝归一化 T_{0,i}·(0,0,1)（§7.4 z_i 原文）。
        const rw::math::Rotation3D<double>& r = refAccum[i].R();
        const rw::math::Vector3D<double> zRaw(r(0, 2), r(1, 2), r(2, 2));
        const double zNorm = zRaw.norm2();
        expectVecNear(e.axis.value(),
                      rw::math::Vector3D<double>(zRaw[0] / zNorm, zRaw[1] / zNorm,
                                                 zRaw[2] / zNorm),
                      1e-12,
                      (std::string("joints[") + std::to_string(i) + "] axis").c_str());
        EXPECT_NEAR(e.axis.value().norm2(), 1.0, 1e-12) << "axis 须为单位向量（@post）";
        // 透传字段：身份/名称/类型/零位（θ_offset 与 zeroOffset 分离——
        // 几何含 q0 旋转，但 zeroOffset 字段原样保留）。
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
 * ACC1 零位对齐：q=0（RobWork 零位）时显式位姿==DH 派生位姿——显式链
 * 累积位姿与参考 DH 累积（Rot_z(θ+q0) 含权威零位旋转）逐位一致；
 * θ_offset 与 zeroOffset 字段显式分离（zeroOffset 不并入 θ 字段）。
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

    // q=0 显式 FK＝origin 累积；与参考 DH 累积（含 zeroOffset 旋转）比对。
    rw::math::Transform3D<double> explicitAcc =
        rw::math::Transform3D<double>(rw::math::Vector3D<double>(0, 0, 0),
                                      rw::math::Rotation3D<double>(1, 0, 0, 0, 1, 0, 0, 0, 1));
    rw::math::Transform3D<double> refAcc = explicitAcc;
    for (std::size_t i = 0; i < joints.size(); ++i) {
        explicitAcc = refTransformMul(explicitAcc, static_cast<rw::math::Transform3D<double>>(
                                            joints[i].origin.value()));
        const DhChainJoint& j = chain.joints[i];
        refAcc = refTransformMul(refAcc, refStep(j.dh.thetaOffset, j.zeroOffset, j.dh.d,
                                                 j.dh.a, j.dh.alpha));
        // 逐关节：显式累积位姿 == DH 派生位姿（q=0 零位对齐——MDL-10）。
        expectPoseNear(JointPose(explicitAcc), refAcc, 1e-12,
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

    // 判定：Exact（解唯一——去重容差内无可辨识第二解）。
    EXPECT_EQ(result.determination, DhDetermination::Exact);
    EXPECT_EQ(dhDeterminationToken(result.determination), "Exact");
    EXPECT_TRUE(result.solutionSet.empty());
    EXPECT_TRUE(result.freeCoordinates.empty());
    EXPECT_EQ(result.convergence, DhConvergenceState::Converged);
    // 逐关节逐项回收（参数级 ≤ 第 5 项上界——roundtrip 参数级一致性的
    // 单侧验证；δθ/δd/δa/δα 逐项独立断言）。
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
    // 字典序定值：自由坐标取中性值 0。
    for (const std::size_t coord : result.freeCoordinates) {
        const std::size_t joint = coord / 4;
        const std::size_t k = coord % 4;
        ASSERT_EQ(k, 0U) << "本样例自由坐标应为 θ（k=0）";
        EXPECT_DOUBLE_EQ(result.parameters[joint].thetaOffset, 0.0);
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
