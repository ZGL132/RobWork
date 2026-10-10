/**
 * @file   MappingR2GoldenTest.cpp
 * @brief  R2 耦合矩阵映射黄金数据用例组（DtMappingR2Golden）——耦合窗口
 *         精确映射（τ_motor＝Cᵀ·τ_joint＋J_rotor·θ̈_motor、θ̈_motor＝
 *         C⁺·q̈_joint——§7.1 公式组冻结口径）、高速多轴联动交叉耦合项
 *         保留（AT-38 M-12）、R1 无耦合链等价对角映射回归锚（C＝对角
 *         传动比时与既有对角路径逐项一致）、R1 阻断反例保留、§7.2 矩阵
 *         形态全表阻断（非方/奇异/病态/非有限——阻止不降级）、R2 反射
 *         惯量完整矩阵与窗口投影、三方消费同一矩阵内容身份（§7.4-4）
 *         ——任务契约 WP-18-T05 acceptance 1/2/3 的测试面。
 *
 * 设计依据：
 *   - units/drivetrain.md §5.3（方向/符号/轴序全文唯一约定）、§7.1～
 *     §7.4（R2 公式组/矩阵形态表/良态阈值 P-RT-7/C⁺ 使用前提）、§8.1/
 *     §8.2（虚功恒等——窗口粒度）、§9.3/§9.5（R2 反射惯量完整矩阵＋
 *     窗口投影标记）、§13.1/§13.3/§13.4（接口契约）、§14.1（黄金数据
 *     口径——解析算例＋独立参考实现，附录 D 第 9 项标量相对 1×10⁻⁹）、
 *     §14.2 DT-R2-1～13 组（R2 故障注入矩阵——用例编号追溯）、§16.1
 *     （MDL-21 全链交接）
 *   - 需求 MDL-21（R2 耦合矩阵消费侧）、DYN-04（M-12 精确虚功映射——
 *     不采用对角化或准静态近似、不丢弃交叉耦合项）、AT-38（耦合矩阵
 *     定义→映射一致；高速多轴联动含交叉耦合项；R1 阻断反例）、
 *     SEL-03/04/05（三方消费同一矩阵内容身份——§7.4 第 4 步）
 *   - 任务契约 tasks/foundation/WP-18-T05.json acceptance 1/2/3
 *
 * 黄金数据纪律（§14.1）：本文件的参考期望值全部由**测试侧独立参考实现**
 * 计算（逐元素标量闭式——下三角逆/转置乘积的解析形态，非产品 Gauss-
 * Jordan 路径），与产品实现（DriveTrainMappingCore R2 路径）形成两路
 * 求值；另以手算常数锚点钉住绝对量级防"两路同错"。容差：连续量标量
 * 相对 1×10⁻⁹（附录 D 第 9 项）；零值锚点用绝对容差。
 *
 * 用例与单元卡故障注入矩阵的对应关系在每 TEST 的注释首行标注（DT-R2
 * 组编号——§14.2 表行）。
 */

#include <sdurws/ird/drivetrain/Codec.hpp>
#include <sdurws/ird/drivetrain/DiagCodes.hpp>
#include <sdurws/ird/drivetrain/MappingCore.hpp>
#include <sdurws/ird/drivetrain/MappingTypes.hpp>
#include <sdurws/ird/drivetrain/Series.hpp>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace dt = sdurws::ird::drivetrain;
namespace core = sdurws::ird::core;

namespace {

// =====================================================================
// 测试工具（MappingGoldenTest 夹具先例的自持副本——固定种子派生 id）
// =====================================================================

/// 从固定种子派生 16 字节强类型 id（测试值——确定性，非随机）。
template <typename Id>
Id idFrom(const std::string& seed)
{
    core::ContentDigester d;
    d.update(seed.data(), seed.size());
    const core::Digest256 digest = d.finalize(); // SHA-256（core 唯一算法面）
    Id id;
    for (std::size_t i = 0; i < 16; ++i) {
        id.bytes[i] = digest[i]; // Digest256＝std::array<uint8_t,32>——取前 16 字节
    }
    return id;
}

/// 关节/电机轴快捷构造（串联序下标即身份序；同种子必得同 id）。
dt::JointDriveAxis jointAxis(std::size_t index)
{
    dt::JointDriveAxis a;
    a.jointId = idFrom<core::ObjectId>("dt2-joint-" + std::to_string(index));
    a.kind = dt::JointKind::Revolute;
    a.localName = "joint" + std::to_string(index + 1);
    return a;
}

dt::MotorDriveAxis motorAxis(std::size_t index)
{
    dt::MotorDriveAxis a;
    a.motorId = idFrom<core::ObjectId>("dt2-motor-" + std::to_string(index));
    a.jointIndex = index; // 轴序纪律：电机轴按对应关节串联序排列
    return a;
}

/// 传动配置身份快捷构造（版本字段>0——构造工厂校验面）。
dt::DriveTrainIdentity testIdentity()
{
    dt::DriveTrainIdentity id;
    id.drivetrainObjectCv.bytes = idFrom<core::ContentVersion>("dt2-config-v1").bytes;
    id.algorithmVersion = 1;
    id.contractVersion = 1;
    return id;
}

/// 效率/转子快捷构造。
dt::EfficiencyModel eta(double fwd, double bwd)
{
    dt::EfficiencyModel e;
    e.etaForward = fwd;
    e.etaBackward = bwd;
    e.source = dt::SourcedValueTag::CatalogBackfill;
    return e;
}

dt::RotorInertiaModel rotor(double j)
{
    dt::RotorInertiaModel r;
    r.rotorInertia = j; // kg·m²（电机轴系）
    r.source = dt::SourcedValueTag::CatalogBackfill;
    return r;
}

/// 单工况正弦采样序列（黄金轨迹——q(t)＝A·sin(W·t)；逐时刻单样本、各轴
/// 共用该时刻值——JointSeriesView 既有语义）。
dt::JointSeriesView sineSeries(std::size_t nJoints, double amplitude, double omega,
                               const std::vector<double>& times, double tauJoint,
                               const std::string& segmentId)
{
    dt::JointSeriesView s;
    for (std::size_t j = 0; j < nJoints; ++j) {
        s.jointIds.push_back(idFrom<core::ObjectId>("dt2-joint-" + std::to_string(j)));
    }
    s.caseId = idFrom<core::ObjectId>("dt2-case-golden");
    s.upstreamSliceId.bytes = idFrom<core::ContentIdentity>("dt2-upstream-slice").bytes;
    for (const double t : times) {
        dt::JointDriveSample sample;
        sample.t = t;                                                  // s
        sample.q = amplitude * std::sin(omega * t);                    // rad
        sample.qd = amplitude * omega * std::cos(omega * t);           // rad/s
        sample.qdd = -amplitude * omega * omega * std::sin(omega * t); // rad/s²
        sample.tauJoint = tauJoint;                                    // N·m
        sample.segmentId = segmentId;
        s.samples.push_back(sample);
    }
    return s;
}

/// 相对容差断言（附录 D 第 9 项：|a−e| ≤ 1e-9·|e|；e==0 时退化为绝对
/// 1e-12，防除零歧义）。
::testing::AssertionResult closeRel(const char* actualExpr, const char* expectedExpr,
                                    double actual, double expected)
{
    const double tol = 1e-9 * std::fabs(expected);
    const double diff = std::fabs(actual - expected);
    if (diff <= tol || (expected == 0.0 && diff <= 1e-12)) {
        return ::testing::AssertionSuccess();
    }
    return ::testing::AssertionFailure()
           << "actual（" << actualExpr << "）=" << actual
           << "；expected（" << expectedExpr << "）=" << expected
           << "；diff=" << diff << "（容差 1e-9 相对——附录 D 第 9 项）";
}

/// 消息首码是否为给定 DT-* 码（fail-fast 消息形态——卡 §13.8）。
bool messageStartsWith(const std::invalid_argument& ex, std::string_view code)
{
    return ex.what() == code
           || (std::string(ex.what()).rfind(std::string(code) + "：", 0) == 0);
}

/// 行主序方阵快捷构造（2×2——黄金算例的窗口矩阵形态）。
dt::RowMatrix mat2(double a00, double a01, double a10, double a11)
{
    dt::RowMatrix m;
    m.rows = 2;
    m.cols = 2;
    m.data = {a00, a01, a10, a11};
    return m;
}

/// 耦合窗口快捷构造（C＋窗口关节 id 表——两轴窗口）。
dt::CouplingWindow makeWindow(dt::RowMatrix c, core::ObjectId j0, core::ObjectId j1)
{
    dt::CouplingWindow w;
    w.C = std::move(c);
    w.jointRange = {std::move(j0), std::move(j1)};
    w.conditionNumber = 0.0; // 申报值 0＝未申报（映射入口以重算值为准）
    return w;
}

/**
 * @brief 手工构造窗口模型（绕过工厂——阻断面测试需要非法形态；chat 由
 *        调用方给全——块对角一致性检查的对象；调用方保证 chat/窗口/ratios
 *        自洽，除非该用例专门构造不一致形态）。
 */
dt::DriveTrainModel rawCoupledModel(std::size_t n, dt::RowMatrix chat,
                                    std::vector<double> diagRatios,
                                    dt::CouplingWindow window)
{
    dt::DriveTrainModel m;
    for (std::size_t i = 0; i < n; ++i) {
        m.jointAxes.push_back(jointAxis(i));
        m.motorAxes.push_back(motorAxis(i));
    }
    m.chat = std::move(chat);
    m.ratios.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        m.ratios[i] = dt::TransmissionRatio{diagRatios[i], dt::SourcedValueTag::ModelingField};
    }
    m.zeroOffsetMotor.assign(n, 0.0);
    m.window = std::move(window);
    m.identity = testIdentity();
    return m;
}

/// 未声明窗口的非对角模型（DT-B1 反例形态——非对角元 1e-6 非零，AT-38
/// nondiag-2axis 同款；R1/R2 两能力位下同码阻断）。
dt::DriveTrainModel rawNondiagonalModel()
{
    dt::DriveTrainModel m;
    for (std::size_t i = 0; i < 2; ++i) {
        m.jointAxes.push_back(jointAxis(i));
        m.motorAxes.push_back(motorAxis(i));
    }
    m.chat = mat2(0.01, 1e-6, 0.0, 0.02);
    m.ratios = {dt::TransmissionRatio{0.01, dt::SourcedValueTag::ModelingField},
                dt::TransmissionRatio{0.02, dt::SourcedValueTag::ModelingField}};
    m.zeroOffsetMotor = {0.0, 0.0};
    m.identity = testIdentity();
    return m;
}

/**
 * @brief 黄金耦合模型（2×2 上三角窗口——逆矩阵解析可写）：
 *
 *   C_w ＝ [[0.02, 0.004],   C_w⁻¹ ＝ [[50, −20],
 *           [0,    0.01]]            [0,  100]]
 * （det＝2×10⁻⁴；伴随法解析逆——参考实现与产品 Gauss-Jordan 双路独立）。
 * κ(C_w)＝σmax/σmin≈2.3——深良态域。θ_off＝{0.5, −0.25} rad（偏置只影响
 * 绝对位置——§5.3）。
 */
dt::DriveTrainModel goldenCoupledModel()
{
    return dt::makeCoupledDriveTrainModel(
        {jointAxis(0), jointAxis(1)}, {motorAxis(0), motorAxis(1)},
        {0, 1}, mat2(0.02, 0.004, 0.0, 0.01),
        {},                                   // 无自由轴（全窗口）
        {0.5, -0.25},                         // θ_off（rad）
        testIdentity(),
        {eta(0.9, 0.7), eta(0.9, 0.7)},
        {rotor(1e-4), rotor(2e-4)});
}

/// 黄金序列（4 样本正弦——映射/统计面成立的最小完整循环）。
dt::JointSeriesView goldenSeries()
{
    return sineSeries(2, 0.1, 6.283185307179586, {0.0, 0.125, 0.25, 0.375}, 2.0,
                      "seg-R2");
}

}  // namespace

// =====================================================================
// DT-R2-1/DT-R2-10：合法常矩阵黄金映射（acceptance 1——精确虚功映射）
// =====================================================================

/**
 * 全窗口耦合模型（上三角 C_w，交叉项 0.004 非零）正弦轨迹：逐样本逐元素
 * 对照 θ/θ̇/θ̈/τ_ideal/τ_m/pJoint/pRotor（测试侧解析逆双路求值）；手算
 * 锚点（t=0.5s：q̇＝−0.2π rad/s ⟹ θ̇_0＝30·q̇、θ̇_1＝100·q̇；τ_ideal＝
 * Cᵀ·[2,2]ᵀ＝[0.04, 0.028] N·m——轴 1 的理想力矩含轴 0 的交叉贡献
 * 0.008 N·m）钉住绝对量级。
 */
TEST(DtMappingR2Golden, CoupledWindowAnalytic_WP18T05_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-21", "DYN-04"},
                  std::vector<std::string>{"AT-38"});

    const dt::DriveTrainModel model = goldenCoupledModel();
    const dt::JointSeriesView series = goldenSeries();

    dt::DriveTrainMappingCore coreImpl;
    // 四参形态——R2 能力位显式声明（无隐式能力提升）。
    const dt::DriveTrainMappingOutput out
        = coreImpl.evaluate(model, series, nullptr, dt::StageCapability::R2Capability);
    ASSERT_EQ(out.motorSeries.size(), 2U);
    EXPECT_TRUE(out.diagnostics.empty());
    EXPECT_EQ(out.completeness, dt::CompletenessState::Complete);

    // ---- 独立参考实现（逐样本标量闭式——C_w⁻¹ 用解析形态 [[50,−20],[0,
    // 100]]，非产品 Gauss-Jordan 路径）：θ＝θ_off＋C⁻¹·q；θ̇＝C⁻¹·q̇；
    // θ̈＝C⁻¹·q̈；τ_ideal＝Cᵀ·τ；τ_m＝τ_ideal＋J·θ̈（M-12 口径④）。
    const double cinv[2][2] = {{50.0, -20.0}, {0.0, 100.0}}; // 解析逆（无量纲）
    const double cw[2][2] = {{0.02, 0.004}, {0.0, 0.01}};    // 窗口矩阵（无量纲）
    for (std::size_t i = 0; i < series.samples.size(); ++i) {
        const double q = series.samples[i].q;          // rad
        const double qd = series.samples[i].qd;        // rad/s
        const double qdd = series.samples[i].qdd;      // rad/s²
        const double tau = series.samples[i].tauJoint; // N·m（两轴同值——序列语义）
        const double jRotor[2] = {1e-4, 2e-4};         // kg·m²
        const double thetaOff[2] = {0.5, -0.25};       // rad
        for (std::size_t k = 0; k < 2; ++k) {
            // 矩阵-向量积按行展开（θ 系＝C⁻¹ 第 k 行；力矩＝Cᵀ 第 k 行＝
            // C 第 k 列——固定下标升序求和）。
            const double thetaRef = thetaOff[k] + cinv[k][0] * q + cinv[k][1] * q;
            const double thetaDotRef = cinv[k][0] * qd + cinv[k][1] * qd;
            const double thetaDDotRef = cinv[k][0] * qdd + cinv[k][1] * qdd;
            const double tauIdealRef = cw[0][k] * tau + cw[1][k] * tau;
            const double tauMotorRef = tauIdealRef + jRotor[k] * thetaDDotRef;
            const double pJointRef = tauIdealRef * thetaDotRef;              // W
            const double pRotorRef = jRotor[k] * thetaDDotRef * thetaDotRef; // W

            const dt::MotorDriveSample& s = out.motorSeries[k].samples[i];
            EXPECT_PRED_FORMAT2(closeRel, s.theta, thetaRef);
            EXPECT_PRED_FORMAT2(closeRel, s.thetaDot, thetaDotRef);
            EXPECT_PRED_FORMAT2(closeRel, s.thetaDDot, thetaDDotRef);
            EXPECT_PRED_FORMAT2(closeRel, s.tauIdeal, tauIdealRef);
            EXPECT_PRED_FORMAT2(closeRel, s.tauMotor, tauMotorRef);
            EXPECT_PRED_FORMAT2(closeRel, s.pJoint, pJointRef);
            EXPECT_PRED_FORMAT2(closeRel, s.pRotor, pRotorRef);
        }
    }

    // ---- 手算锚点（t=0.5s 单样本评估——绝对量级钉子）。正弦在 t=0.5：
    // q̇＝0.1·2π·cos(π)＝−0.2π rad/s；q＝0.1·sin(π)＝0 rad。
    dt::JointSeriesView one = sineSeries(2, 0.1, 6.283185307179586, {0.5}, 2.0, "anchor");
    const dt::DriveTrainMappingOutput outA
        = coreImpl.evaluate(model, one, nullptr, dt::StageCapability::R2Capability);
    ASSERT_EQ(outA.motorSeries.size(), 2U);
    const double qd05 = 0.1 * 6.283185307179586 * std::cos(6.283185307179586 * 0.5);
    // θ̇_0＝(50−20)·q̇＝30·q̇；θ̇_1＝100·q̇（解析逆行组合——手算值）。
    EXPECT_NEAR(outA.motorSeries[0].samples[0].thetaDot, 30.0 * qd05, 1e-9);
    EXPECT_NEAR(outA.motorSeries[1].samples[0].thetaDot, 100.0 * qd05, 1e-9);
    // τ_ideal＝Cᵀ·[2,2]ᵀ：轴 0＝0.02·2＋0·2＝0.04；轴 1＝0.004·2＋0.01·2
    // ＝0.028 N·m（含轴 0 交叉贡献 0.008——非对角项逐元素进入，AT-38）。
    EXPECT_NEAR(outA.motorSeries[0].samples[0].tauIdeal, 0.04, 1e-12);
    EXPECT_NEAR(outA.motorSeries[1].samples[0].tauIdeal, 0.028, 1e-12);
    // 绝对位置含零位偏置：θ_0＝0.5＋(C⁻¹·q)_0（q=0 ⟹ θ_0＝0.5 rad）；
    // θ_1＝−0.25 rad——偏置只影响绝对位置（§5.3）。
    EXPECT_NEAR(outA.motorSeries[0].samples[0].theta, 0.5, 1e-12);
    EXPECT_NEAR(outA.motorSeries[1].samples[0].theta, -0.25, 1e-12);
}

// =====================================================================
// DT-R2-8：高速多轴联动交叉项保留（acceptance 1——AT-38 M-12 口径）
// =====================================================================

/**
 * 混合链（自由轴 0＋窗口 {1,2}）高速轨迹（ω＝25 rad/s、A＝0.5、τ 随时间
 * 变化）：断言①运动学交叉项——窗口轴 1 的 θ̇ 经 C⁻¹ 非对角元获得跨轴
 * 贡献（30·q̇ ≠ 对角绕过值 50·q̇，每样本可测差异）；②力矩交叉项逐元素
 * 非零保留（τ_ideal,1＝0.014·τ ≠ 对角绕过值 0.01·τ）；③虚功窗口恒等
 * Σ τ_ideal,k·θ̇_k＝τ_wᵀ·q̇_w 逐样本成立（§8.2 ②窗口粒度）；④含转子项
 * 力矩＝τ_ideal＋J·θ̈ 解析对照。交叉项被丢弃（对角化/准静态等效）即实现
 * 缺陷——拒绝绕过（AT-38/M-12 红线）。
 */
TEST(DtMappingR2Golden, HighSpeedCrossCouplingPreserved_WP18T05_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-04", "MDL-21"},
                  std::vector<std::string>{"AT-38"});

    // 混合链：窗口 {1,2}（C 同黄金形态），自由轴 0（c=0.005）。
    const dt::DriveTrainModel model = dt::makeCoupledDriveTrainModel(
        {jointAxis(0), jointAxis(1), jointAxis(2)},
        {motorAxis(0), motorAxis(1), motorAxis(2)},
        {1, 2}, mat2(0.02, 0.004, 0.0, 0.01),
        {dt::TransmissionRatio{0.005, dt::SourcedValueTag::ModelingField}},
        {0.0, 0.0, 0.0}, testIdentity(),
        {eta(0.9, 0.7), eta(0.9, 0.7), eta(0.9, 0.7)},
        {rotor(1e-4), rotor(1e-4), rotor(1e-4)});

    // 高速轨迹：ω＝25 rad/s（远高于黄金组的 2π）；τ 逐样本变化（激发
    // 交叉项的时变贡献）。样本时刻的 ω·t∈[1.25,10]，|cos(ω·t)|≥0.28——
    // 速度远离过零，符号类断言（象限/方向）不受舍入影响。
    std::vector<double> times;
    for (int i = 0; i < 9; ++i) {
        times.push_back(0.05 * static_cast<double>(i)); // s（0～0.4）
    }
    dt::JointSeriesView series = sineSeries(3, 0.5, 25.0, times, 2.0, "seg-HS");
    for (std::size_t i = 0; i < series.samples.size(); ++i) {
        series.samples[i].tauJoint = 2.0 + 0.5 * std::sin(3.0 * times[i]); // N·m
    }

    dt::DriveTrainMappingCore coreImpl;
    const dt::DriveTrainMappingOutput out
        = coreImpl.evaluate(model, series, nullptr, dt::StageCapability::R2Capability);
    ASSERT_EQ(out.motorSeries.size(), 3U);

    for (std::size_t i = 0; i < series.samples.size(); ++i) {
        const double tau = series.samples[i].tauJoint; // N·m
        const double qd = series.samples[i].qd;        // rad/s
        const double qdd = series.samples[i].qdd;      // rad/s²

        // ---- ①运动学交叉项：窗口轴 1 的 θ̇＝(50−20)·q̇＝30·q̇；对角化
        // 绕过值＝50·q̇——每样本可测差异（交叉项丢弃＝实现缺陷）。
        const double thetaDot1 = out.motorSeries[1].samples[i].thetaDot;
        EXPECT_PRED_FORMAT2(closeRel, thetaDot1, 30.0 * qd);
        EXPECT_PRED_FORMAT2(closeRel, out.motorSeries[2].samples[i].thetaDot, 100.0 * qd);
        const double diagBypass = 50.0 * qd; // 对角化绕过值（rad/s）
        EXPECT_GT(std::fabs(thetaDot1 - diagBypass), 1e-6 * std::fabs(diagBypass))
            << "窗口轴 1 的速度映射不得退化为对角绕过（交叉项被丢弃＝实现缺陷）";

        // ---- ②力矩交叉项逐元素：全局轴 1＝窗口位次 0（C 第 0 列＝
        // [0.02,0]）⟹ τ_ideal,1＝0.02·τ；全局轴 2＝窗口位次 1（C 第 1 列
        // ＝[0.004,0.01]）⟹ τ_ideal,2＝0.014·τ——交叉贡献 0.004·τ 逐元素
        // 非零（对角绕过值 0.01·τ，拒绝等效）。
        const double tauIdeal1 = out.motorSeries[1].samples[i].tauIdeal;
        EXPECT_PRED_FORMAT2(closeRel, tauIdeal1, 0.02 * tau);
        const double tauIdeal2 = out.motorSeries[2].samples[i].tauIdeal;
        EXPECT_PRED_FORMAT2(closeRel, tauIdeal2, 0.014 * tau);
        EXPECT_GT(std::fabs(tauIdeal2 - 0.01 * tau), 1e-9 * std::fabs(tau))
            << "窗口力矩映射不得对角化（交叉项逐元素非零保留——AT-38/M-12）";

        // ---- ③虚功窗口恒等（§8.2 ②窗口粒度）：Σ τ_ideal,k·θ̇_k
        // ＝ τ_wᵀ·q̇_w＝2·τ·q̇（两窗口关节同值——序列语义）。
        const double motorSide
            = out.motorSeries[1].samples[i].tauIdeal * out.motorSeries[1].samples[i].thetaDot
              + out.motorSeries[2].samples[i].tauIdeal * out.motorSeries[2].samples[i].thetaDot;
        const double jointSide = 2.0 * tau * qd; // W（τ_wᵀ·q̇_w 手算式）
        EXPECT_PRED_FORMAT2(closeRel, motorSide, jointSide)
            << "虚功窗口恒等破坏（样本 " << i << "）";

        // ---- ④含转子项力矩＝τ_ideal＋J·θ̈（θ̈_1＝(50−20)·q̈＝30·q̈ 解析）。
        EXPECT_PRED_FORMAT2(closeRel, out.motorSeries[1].samples[i].tauMotor,
                            0.02 * tau + 1e-4 * 30.0 * qdd);
    }
}

// =====================================================================
// DT-R2-7（正命题）：R1 无耦合链等价对角映射回归锚（acceptance 3）
// =====================================================================

/**
 * C＝对角传动比时与既有对角路径输出逐项一致——回归锚：同一物理配置分别
 * 以 R1 对角模型（无窗口）与 R2 窗口对角模型（C_w＝diag）评估：
 *   - 自由轴（不在窗口）输出与 R1 路径**逐位相等**（同一标量表达式——
 *     operator== 全等断言，回归锚的最强形态）；
 *   - 窗口轴全部数值逐项一致（相对 1×10⁻⁹——矩阵路径与标量路径求值序
 *     不同，位等不成立、黄金容差内全等——卡 §6.2"映射误差"行的口径）；
 *   - 工作点统计（RMS/峰值/象限计数/能量）逐项一致（容差内）。
 */
TEST(DtMappingR2Golden, WindowDiagonalEquivalentToR1_WP18T05_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-04", "MDL-21"},
                  std::vector<std::string>{"AT-38"});

    const double c0 = 0.02;  // 自由轴传动比（无量纲）
    const double c1 = 0.01;  // 窗口轴 1（无量纲）
    const double c2 = 0.005; // 窗口轴 2（无量纲）

    // R1 对角模型（既有路径——三参 evaluate 即 R1 能力）。
    const dt::DriveTrainModel r1Model = dt::makeDiagonalDriveTrainModel(
        {jointAxis(0), jointAxis(1), jointAxis(2)},
        {motorAxis(0), motorAxis(1), motorAxis(2)},
        {{c0, dt::SourcedValueTag::ModelingField},
         {c1, dt::SourcedValueTag::ModelingField},
         {c2, dt::SourcedValueTag::ModelingField}},
        {0.0, 0.3, -0.2}, testIdentity(),
        {eta(0.9, 0.7), eta(0.85, 0.65), eta(0.9, 0.7)},
        {rotor(1e-4), rotor(2e-4), rotor(5e-5)});

    // R2 窗口对角模型：窗口 {1,2}、C_w＝diag(c1,c2)、自由轴 0——同一物理
    // 配置的 R2 声明形态（对角 C 保留在窗口内＝"无耦合链"的矩阵表达）。
    const dt::DriveTrainModel r2Model = dt::makeCoupledDriveTrainModel(
        {jointAxis(0), jointAxis(1), jointAxis(2)},
        {motorAxis(0), motorAxis(1), motorAxis(2)},
        {1, 2}, mat2(c1, 0.0, 0.0, c2),
        {dt::TransmissionRatio{c0, dt::SourcedValueTag::ModelingField}},
        {0.0, 0.3, -0.2}, testIdentity(),
        {eta(0.9, 0.7), eta(0.85, 0.65), eta(0.9, 0.7)},
        {rotor(1e-4), rotor(2e-4), rotor(5e-5)});

    std::vector<double> times{0.0, 0.1, 0.2, 0.3, 0.4};
    dt::JointSeriesView series = sineSeries(3, 0.2, 8.0, times, 1.5, "seg-EQ");

    dt::DriveTrainMappingCore coreImpl;
    const dt::DriveTrainMappingOutput outR1 = coreImpl.evaluate(r1Model, series, nullptr);
    const dt::DriveTrainMappingOutput outR2 = coreImpl.evaluate(
        r2Model, series, nullptr, dt::StageCapability::R2Capability);
    ASSERT_EQ(outR1.motorSeries.size(), 3U);
    ASSERT_EQ(outR2.motorSeries.size(), 3U);

    // ---- 自由轴逐位相等（同一标量表达式路径——operator== 全等）。
    EXPECT_EQ(outR2.motorSeries[0], outR1.motorSeries[0])
        << "自由轴输出必须与 R1 路径逐位一致（同一表达式——回归锚最强形态）";

    // ---- 窗口轴逐项一致（相对 1×10⁻⁹——跨求值路径黄金容差）。
    for (const std::size_t k : {1U, 2U}) {
        ASSERT_EQ(outR1.motorSeries[k].samples.size(), times.size());
        ASSERT_EQ(outR2.motorSeries[k].samples.size(), times.size());
        for (std::size_t i = 0; i < times.size(); ++i) {
            const dt::MotorDriveSample& r1 = outR1.motorSeries[k].samples[i];
            const dt::MotorDriveSample& r2 = outR2.motorSeries[k].samples[i];
            EXPECT_PRED_FORMAT2(closeRel, r2.theta, r1.theta);
            EXPECT_PRED_FORMAT2(closeRel, r2.thetaDot, r1.thetaDot);
            EXPECT_PRED_FORMAT2(closeRel, r2.thetaDDot, r1.thetaDDot);
            EXPECT_PRED_FORMAT2(closeRel, r2.tauIdeal, r1.tauIdeal);
            EXPECT_PRED_FORMAT2(closeRel, r2.tauMotor, r1.tauMotor);
            EXPECT_PRED_FORMAT2(closeRel, r2.pJoint, r1.pJoint);
            EXPECT_PRED_FORMAT2(closeRel, r2.pTransmission, r1.pTransmission);
            EXPECT_PRED_FORMAT2(closeRel, r2.pRotor, r1.pRotor);
            EXPECT_PRED_FORMAT2(closeRel, r2.pMotor, r1.pMotor);
            EXPECT_EQ(r2.efficiencyApplicable, r1.efficiencyApplicable);
            EXPECT_EQ(r2.quadrant, r1.quadrant);
        }
        // 工作点统计逐项一致（RMS/象限计数/能量；峰值带时刻）。
        const dt::MotorOperatingPoint& p1 = outR1.points[k];
        const dt::MotorOperatingPoint& p2 = outR2.points[k];
        EXPECT_PRED_FORMAT2(closeRel, p2.tauRms, p1.tauRms);
        EXPECT_PRED_FORMAT2(closeRel, p2.omegaRms, p1.omegaRms);
        EXPECT_EQ(p2.q1.sampleCount, p1.q1.sampleCount);
        EXPECT_EQ(p2.q2.sampleCount, p1.q2.sampleCount);
        EXPECT_EQ(p2.q3.sampleCount, p1.q3.sampleCount);
        EXPECT_EQ(p2.q4.sampleCount, p1.q4.sampleCount);
        EXPECT_EQ(p2.zeroDwell.sampleCount, p1.zeroDwell.sampleCount);
        EXPECT_PRED_FORMAT2(closeRel, p2.energy.eMotor, p1.energy.eMotor);
        EXPECT_PRED_FORMAT2(closeRel, p2.energy.eJoint, p1.energy.eJoint);
        EXPECT_PRED_FORMAT2(closeRel, p2.energy.eLoss, p1.energy.eLoss);
        EXPECT_EQ(p2.tauPeakPos.present, p1.tauPeakPos.present);
        EXPECT_PRED_FORMAT2(closeRel, p2.tauPeakPos.value, p1.tauPeakPos.value);
        EXPECT_EQ(p2.tauPeakPos.t, p1.tauPeakPos.t);
        // 反射惯量对角视图一致（kg·m²）。
        EXPECT_PRED_FORMAT2(closeRel, outR2.inertia.axes[k].jReflectedJointSide,
                            outR1.inertia.axes[k].jReflectedJointSide);
    }
}

// =====================================================================
// DT-R2-12/DT-B1/DT-B3：R1 阻断反例保留＋未声明非对角阻断（acceptance 3）
// =====================================================================

/**
 * "不提前放开 R1 阻断"红线的 T05 回归面：①R1 能力＋合法耦合窗口→
 * DT-COUPLING-STAGE-LOCKED（evaluate 与 validator 双路径）；②R1＋非对角
 * （无窗口）→DT-MATRIX-NONDIAGONAL-LOCKED；③R2 能力＋未声明窗口的非对
 * 角 chat→仍 DT-MATRIX-NONDIAGONAL-LOCKED（交叉耦合必须经窗口声明，不
 * 静默拆轴——码与能力位无关）；④R2＋退化空窗口→DT-INPUT-DIMENSION-
 * MISMATCH（阻断不降级，码面更精确）。
 */
TEST(DtMappingR2Golden, R1BlockRejectionsRetained_WP18T05_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-21", "DYN-04"},
                  std::vector<std::string>{"AT-38"});

    dt::DriveTrainMappingCore coreImpl;
    dt::IDriveTrainMappingEvaluator& evaluator = coreImpl; // 接口消费面
    dt::ICouplingMatrixValidator& validator = coreImpl;    // 接口消费面
    const dt::JointSeriesView series = goldenSeries();

    // ---- ①R1＋合法耦合窗口：能力门控阻断（双路径）。
    {
        const dt::DriveTrainModel coupled = goldenCoupledModel();
        bool threw = false;
        try {
            (void)evaluator.evaluate(coupled, series, nullptr); // 三参＝R1 语义
            FAIL() << "R1 能力下耦合窗口必须被阻断（DT-COUPLING-STAGE-LOCKED）";
        } catch (const std::invalid_argument& ex) {
            threw = true;
            EXPECT_TRUE(messageStartsWith(ex, dt::kDtCouplingStageLocked)) << ex.what();
        }
        EXPECT_TRUE(threw);
        const dt::CouplingValidationResult gate
            = validator.validate(coupled, dt::StageCapability::R1Capability);
        EXPECT_FALSE(gate.accepted);
        ASSERT_FALSE(gate.diagnostics.empty());
        EXPECT_EQ(gate.diagnostics.front().code, std::string(dt::kDtCouplingStageLocked));
    }

    // ---- ②R1＋非对角（无窗口）：结构阻断（evaluate fail-fast——消息以
    // 码开头；卡 §13.8 形态）。
    {
        const dt::DriveTrainModel nondiag = rawNondiagonalModel();
        bool threw = false;
        try {
            (void)evaluator.evaluate(nondiag, series, nullptr);
            FAIL() << "非对角输入必须被阻断（DT-MATRIX-NONDIAGONAL-LOCKED）";
        } catch (const std::invalid_argument& ex) {
            threw = true;
            EXPECT_TRUE(messageStartsWith(ex, dt::kDtMatrixNondiagonalLocked)) << ex.what();
        }
        EXPECT_TRUE(threw);
    }

    // ---- ③R2＋未声明窗口的非对角 chat：同码阻断（交叉耦合必须经窗口
    // 声明——不静默拆轴；四参 R2 入口与 validator R2 位双路径）。
    {
        const dt::DriveTrainModel nondiag = rawNondiagonalModel();
        bool threw = false;
        try {
            (void)evaluator.evaluate(nondiag, series, nullptr,
                                     dt::StageCapability::R2Capability);
            FAIL() << "未声明窗口的非对角输入在 R2 下仍必须被阻断";
        } catch (const std::invalid_argument& ex) {
            threw = true;
            EXPECT_TRUE(messageStartsWith(ex, dt::kDtMatrixNondiagonalLocked)) << ex.what();
        }
        EXPECT_TRUE(threw);
        const dt::CouplingValidationResult gate
            = validator.validate(nondiag, dt::StageCapability::R2Capability);
        EXPECT_FALSE(gate.accepted);
        ASSERT_FALSE(gate.diagnostics.empty());
        EXPECT_EQ(gate.diagnostics.front().code,
                  std::string(dt::kDtMatrixNondiagonalLocked));
    }

    // ---- ④R2＋退化空窗口：维度违约（阻断不降级——0×0 矩阵在"方阵"空
    // 真判定上通过，非空性必须显式拦截）。
    {
        dt::DriveTrainModel degenerate = dt::makeDiagonalDriveTrainModel(
            {jointAxis(0), jointAxis(1)}, {motorAxis(0), motorAxis(1)},
            {{0.01, dt::SourcedValueTag::ModelingField},
             {0.02, dt::SourcedValueTag::ModelingField}},
            {0.0, 0.0}, testIdentity());
        degenerate.window = dt::CouplingWindow{}; // 空窗口（0×0/空表）
        const dt::CouplingValidationResult gate
            = validator.validate(degenerate, dt::StageCapability::R2Capability);
        EXPECT_FALSE(gate.accepted) << "退化空窗口必须被阻断（不降级）";
        ASSERT_FALSE(gate.diagnostics.empty());
        EXPECT_EQ(gate.diagnostics.front().code,
                  std::string(dt::kDtInputDimensionMismatch));
    }
}

// =====================================================================
// DT-R2-2～6：§7.2 矩阵形态全表阻断（acceptance 3——阻止不降级）
// =====================================================================

/**
 * R2 能力下非法矩阵形态逐码核对（全部在映射入口被拒、不产出半结果）：
 * 非方（DT-MATRIX-NONSQUARE）、奇异（DT-MATRIX-SINGULAR——不得以伪逆放
 * 行）、病态（DT-MATRIX-ILL-CONDITIONED——比较型：κ/阈值/无量纲）、非
 * 有限（DT-MATRIX-NONFINITE）、窗口引用越界（DT-INPUT-DIMENSION-
 * MISMATCH）、chat 与块对角组合不一致（DT-INPUT-DIMENSION-MISMATCH）。
 * 工厂面同码拒绝（构造入口 fail-fast——两道防线同一语义）。
 */
TEST(DtMappingR2Golden, MatrixGateBlockers_WP18T05_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-21", "NFR-COR-03"},
                  std::vector<std::string>{"AT-38"});

    dt::DriveTrainMappingCore coreImpl;
    dt::ICouplingMatrixValidator& validator = coreImpl;
    dt::IDriveTrainMappingEvaluator& evaluator = coreImpl;
    const dt::JointSeriesView series = goldenSeries();

    // ---- 非方矩阵（窗口声明 2 关节＋3×2 矩阵——工厂与校验器双路径）。
    {
        dt::RowMatrix nonsquare;
        nonsquare.rows = 3;
        nonsquare.cols = 2;
        nonsquare.data = {0.02, 0.004, 0.0, 0.01, 0.0, 0.0};
        bool threw = false;
        try {
            (void)dt::makeCoupledDriveTrainModel(
                {jointAxis(0), jointAxis(1), jointAxis(2)},
                {motorAxis(0), motorAxis(1), motorAxis(2)},
                {1, 2}, nonsquare,
                {dt::TransmissionRatio{0.005, dt::SourcedValueTag::ModelingField}},
                {0.0, 0.0, 0.0}, testIdentity());
            FAIL() << "非方耦合矩阵必须被工厂拒绝（DT-MATRIX-NONSQUARE）";
        } catch (const std::invalid_argument& ex) {
            threw = true;
            EXPECT_TRUE(messageStartsWith(ex, dt::kDtMatrixNonsquare)) << ex.what();
        }
        EXPECT_TRUE(threw);
    }

    // ---- 奇异矩阵（rank-1：[[1,2],[2,4]]——σmin≈0；不得以伪逆放行：
    // evaluate 四参 R2 入口同样 fail-fast，不产出半结果。chat/窗口/ratios
    // 自洽——{1,4}＝对角元，保证首个命中码落在奇异检查而非一致性检查）。
    {
        const dt::DriveTrainModel singular = rawCoupledModel(
            2, mat2(1.0, 2.0, 2.0, 4.0), {1.0, 4.0},
            makeWindow(mat2(1.0, 2.0, 2.0, 4.0), jointAxis(0).jointId,
                       jointAxis(1).jointId));
        const dt::CouplingValidationResult gate
            = validator.validate(singular, dt::StageCapability::R2Capability);
        EXPECT_FALSE(gate.accepted);
        ASSERT_FALSE(gate.diagnostics.empty());
        EXPECT_EQ(gate.diagnostics.front().code, std::string(dt::kDtMatrixSingular));
        // 比较型字段（ERR-01）：σmin/σmax 比值实陈——actual 落入奇异分界带。
        ASSERT_TRUE(gate.diagnostics.front().comparison.has_value());
        EXPECT_LE(gate.diagnostics.front().comparison->actual.quantity.value(), 1e-12);

        bool threw = false;
        try {
            (void)evaluator.evaluate(singular, series, nullptr,
                                     dt::StageCapability::R2Capability);
            FAIL() << "奇异矩阵不得经伪逆放行（DT-MATRIX-SINGULAR）";
        } catch (const std::invalid_argument& ex) {
            threw = true;
            EXPECT_TRUE(messageStartsWith(ex, dt::kDtMatrixSingular)) << ex.what();
        }
        EXPECT_TRUE(threw);
    }

    // ---- 病态矩阵（diag(1,1×10⁹)：σmin/σmax＝1×10⁻⁹＞1×10⁻¹² 非奇异、
    // κ＝1×10⁹＞1×10⁸ 超限——比较型：实际 κ/阈值 1×10⁸/无量纲）。
    {
        const dt::DriveTrainModel ill = rawCoupledModel(
            2, mat2(1.0, 0.0, 0.0, 1e9), {1.0, 1e9},
            makeWindow(mat2(1.0, 0.0, 0.0, 1e9), jointAxis(0).jointId,
                       jointAxis(1).jointId));
        const dt::CouplingValidationResult gate
            = validator.validate(ill, dt::StageCapability::R2Capability);
        EXPECT_FALSE(gate.accepted);
        ASSERT_FALSE(gate.diagnostics.empty());
        EXPECT_EQ(gate.diagnostics.front().code,
                  std::string(dt::kDtMatrixIllConditioned));
        ASSERT_TRUE(gate.diagnostics.front().comparison.has_value());
        // κ 实陈值＝1×10⁹（对角阵谱条件数＝max|c|/min|c| 的精确形态）。
        EXPECT_NEAR(gate.diagnostics.front().comparison->actual.quantity.value(), 1e9,
                    1e9 * 1e-9);
        EXPECT_NEAR(gate.diagnostics.front().comparison->expected.quantity.value(),
                    dt::kWellConditionedLimit, dt::kWellConditionedLimit * 1e-12);
        EXPECT_GT(gate.conditionNumber, dt::kWellConditionedLimit);
    }

    // ---- 非有限元素（窗口矩阵含 NaN——NFR-COR-03 精确判据）。
    {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const dt::DriveTrainModel bad = rawCoupledModel(
            2, mat2(0.02, nan, 0.0, 0.01), {0.02, 0.01},
            makeWindow(mat2(0.02, nan, 0.0, 0.01), jointAxis(0).jointId,
                       jointAxis(1).jointId));
        const dt::CouplingValidationResult gate
            = validator.validate(bad, dt::StageCapability::R2Capability);
        EXPECT_FALSE(gate.accepted);
        ASSERT_FALSE(gate.diagnostics.empty());
        EXPECT_EQ(gate.diagnostics.front().code, std::string(dt::kDtMatrixNonfinite));
    }

    // ---- 窗口引用越界（jointRange 携带不在轴表的 id——工厂不可达，校验
    // 器为第二道防线；非严格升序同码）。
    {
        const dt::CouplingWindow win = makeWindow(
            mat2(0.02, 0.004, 0.0, 0.01),
            idFrom<core::ObjectId>("ghost-joint-0"), jointAxis(1).jointId);
        const dt::DriveTrainModel ghost = rawCoupledModel(
            2, mat2(0.02, 0.004, 0.0, 0.01), {0.02, 0.01}, win);
        const dt::CouplingValidationResult gate
            = validator.validate(ghost, dt::StageCapability::R2Capability);
        EXPECT_FALSE(gate.accepted);
        ASSERT_FALSE(gate.diagnostics.empty());
        EXPECT_EQ(gate.diagnostics.front().code,
                  std::string(dt::kDtInputDimensionMismatch));
    }

    // ---- chat 与块对角组合不一致（窗口合法但 Ĉ 手工拼错——维度/结构违
    // 约同码；§7.1 归一化唯一视图纪律）。
    {
        dt::DriveTrainModel inconsistent = goldenCoupledModel();
        inconsistent.chat.data[0 * 2 + 1] += 1e-9; // 窗口块 (0,1) 与 C_w 不再一致
        const dt::CouplingValidationResult gate
            = validator.validate(inconsistent, dt::StageCapability::R2Capability);
        EXPECT_FALSE(gate.accepted);
        ASSERT_FALSE(gate.diagnostics.empty());
        EXPECT_EQ(gate.diagnostics.front().code,
                  std::string(dt::kDtInputDimensionMismatch));
    }

    // ---- 工厂面：窗口关节下标重复、自由轴 c=0（构造入口 fail-fast——与
    // 映射入口同一语义两道防线）。
    {
        bool threw = false;
        try {
            (void)dt::makeCoupledDriveTrainModel(
                {jointAxis(0), jointAxis(1)}, {motorAxis(0), motorAxis(1)},
                {1, 1}, mat2(0.02, 0.0, 0.0, 0.01), {}, {0.0, 0.0}, testIdentity());
            FAIL() << "窗口下标重复必须被工厂拒绝（DT-INPUT-DIMENSION-MISMATCH）";
        } catch (const std::invalid_argument& ex) {
            threw = true;
            EXPECT_TRUE(messageStartsWith(ex, dt::kDtInputDimensionMismatch)) << ex.what();
        }
        EXPECT_TRUE(threw);

        threw = false;
        try {
            // 合法 1×1 窗口（关节 0）＋自由轴 c=0——首个命中码落在
            // DT-RATIO-ZERO（矩阵形态自洽，不触发 NONSQUARE）。
            dt::RowMatrix m1;
            m1.rows = 1;
            m1.cols = 1;
            m1.data = {0.02};
            (void)dt::makeCoupledDriveTrainModel(
                {jointAxis(0), jointAxis(1)}, {motorAxis(0), motorAxis(1)},
                {0}, m1,
                {dt::TransmissionRatio{0.0, dt::SourcedValueTag::ModelingField}},
                {0.0, 0.0}, testIdentity());
            FAIL() << "自由轴 c=0 必须被工厂拒绝（DT-RATIO-ZERO）";
        } catch (const std::invalid_argument& ex) {
            threw = true;
            EXPECT_TRUE(messageStartsWith(ex, dt::kDtRatioZero)) << ex.what();
        }
        EXPECT_TRUE(threw);
    }
}

// =====================================================================
// DT-R2-11：R2 反射惯量完整矩阵（acceptance 2——交叉项保留＋投影标记）
// =====================================================================

/**
 * 混合链（自由轴 0＋窗口 {1,2}）反射惯量（§9.3/§9.5，经
 * IReflectedInertiaEvaluator 接口消费）：①完整矩阵＝(C⁻¹)ᵀ·diag(J)·C⁻¹
 * ——测试侧标量参考对照（交叉惯量项 (1,2) 非零保留）；②构造对称（(1,2)
 * ＝(2,1) 位等）；③对角视图＝完整矩阵对角元（窗口轴附投影标记、自由轴
 * 无）；④惯量比（窗口投影式）解析对照；⑤对角元全正（构造正定的必要面
 * ——Cholesky 守护在产品侧）。
 */
TEST(DtMappingR2Golden, ReflectedInertiaR2FullMatrix_WP18T05_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-21", "DYN-04"},
                  std::vector<std::string>{"AT-38"});

    const dt::DriveTrainModel model = dt::makeCoupledDriveTrainModel(
        {jointAxis(0), jointAxis(1), jointAxis(2)},
        {motorAxis(0), motorAxis(1), motorAxis(2)},
        {1, 2}, mat2(0.02, 0.004, 0.0, 0.01),
        {dt::TransmissionRatio{0.005, dt::SourcedValueTag::ModelingField}},
        {0.0, 0.0, 0.0}, testIdentity(), {},
        {rotor(1e-4), rotor(2e-4), rotor(5e-5)});

    // 负载折算惯量（关节轴系 kg·m²——按电机轴下标配对；P-DT-4 保守消费）。
    std::vector<dt::LoadInertiaEntry> load;
    load.push_back(dt::LoadInertiaEntry{0.1, dt::SourcedValueTag::UserProvided});
    load.push_back(dt::LoadInertiaEntry{0.2, dt::SourcedValueTag::UserProvided});
    load.push_back(dt::LoadInertiaEntry{0.05, dt::SourcedValueTag::UserProvided});

    dt::DriveTrainMappingCore coreImpl;
    dt::IReflectedInertiaEvaluator& inertiaEval = coreImpl; // 接口消费面
    const dt::ReflectedInertiaResult result = inertiaEval.evaluate(model, load);
    ASSERT_EQ(result.axes.size(), 3U);
    ASSERT_TRUE(result.jointSideFullMatrix.has_value()) << "R2 窗口须产出完整矩阵";
    const dt::RowMatrix& jref = *result.jointSideFullMatrix;
    ASSERT_EQ(jref.rows, 3U);
    ASSERT_EQ(jref.cols, 3U);

    // ---- 测试侧标量参考：C_w⁻¹＝[[50,−20],[0,100]]（解析）；J_ref 窗口
    // 块 (i,j)＝Σ_m J_m·Cinv(m,i)·Cinv(m,j)；自由轴 J/c²。
    const double cinv[2][2] = {{50.0, -20.0}, {0.0, 100.0}}; // 解析逆（无量纲）
    const double jW[2] = {2e-4, 5e-5};    // 窗口转子（kg·m²，电机轴系）
    const double loadW[2] = {0.2, 0.05};  // 窗口负载（kg·m²，关节轴系）
    // 自由轴（k=0）：J/c²（关节轴系）。
    EXPECT_PRED_FORMAT2(closeRel, jref(0, 0), 1e-4 / (0.005 * 0.005));
    EXPECT_FALSE(result.axes[0].windowProjected);
    // 窗口块交叉惯量项 (1,2)（全局）＝Σ J_m·Cinv(m,0)·Cinv(m,1)——非零
    // 保留（不对角化输出；本例＝2e-4·50·(−20)＝−0.2 kg·m²）。
    const double ref12
        = jW[0] * cinv[0][0] * cinv[0][1] + jW[1] * cinv[1][0] * cinv[1][1];
    EXPECT_GT(std::fabs(ref12), 0.0) << "交叉惯量项必须非零（构造前提）";
    EXPECT_PRED_FORMAT2(closeRel, jref(1, 2), ref12);
    // 构造对称：位等（同一表达式交换 r/c——乘法交换律位等）。
    EXPECT_EQ(jref(1, 2), jref(2, 1)) << "J_ref 构造保证对称（位等）";
    // 对角视图＝完整矩阵对角元（投影标记面）。
    for (const std::size_t k : {1U, 2U}) {
        EXPECT_PRED_FORMAT2(closeRel, result.axes[k].jReflectedJointSide, jref(k, k));
        EXPECT_TRUE(result.axes[k].windowProjected)
            << "窗口轴对角视图必须附投影限定标记（§9.5）";
        EXPECT_GT(jref(k, k), 0.0) << "对角元全正（构造正定的必要面）";
    }
    // 惯量比（§9.5 R2 窗口投影式）：(C⁻¹ᵀ·J_load·C⁻¹)(0,0)/J_rotor,win0
    // ＝（0.2·2500＋0.05·0)/2e-4＝2.5×10⁶（无量纲；阈值判定归 selection
    // ——P-DT-2）。
    const double loadDiag1 = loadW[0] * cinv[0][0] * cinv[0][0]
                             + loadW[1] * cinv[1][0] * cinv[1][0];
    ASSERT_TRUE(result.axes[1].inertiaRatio.has_value());
    EXPECT_PRED_FORMAT2(closeRel, *result.axes[1].inertiaRatio, loadDiag1 / jW[0]);
}

// =====================================================================
// DT-R2-13／§7.4-4：三方消费同一矩阵内容身份（acceptance 2）
// =====================================================================

/**
 * SEL-03/04/05 消费同一矩阵内容身份（§7.4 第 4 步）：一次 R2 映射输出的
 * 身份块（drivetrainObjectCv——robot-drivetrain 对象 ContentVersion）经
 * payload canonical 字节三方解码（模拟三方消费切片——§11.3 同源同身份）
 * 后逐字节一致；核对原语 checkMatrixContentIdentityAlignment 全一致为
 * true；篡改任一切片身份→false 且 firstDivergence 定位；空清单按不一致
 * 处置。权威边界：接纳层判定权归 evidence（PA-1）——本原语是值比较面。
 */
TEST(DtMappingR2Golden, SameMatrixIdentityThreeConsumers_WP18T05_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-21", "DYN-04"},
                  std::vector<std::string>{"AT-38"});

    const dt::DriveTrainModel model = goldenCoupledModel();
    const dt::JointSeriesView series = goldenSeries();

    dt::DriveTrainMappingCore coreImpl;
    const dt::DriveTrainMappingOutput out
        = coreImpl.evaluate(model, series, nullptr, dt::StageCapability::R2Capability);

    // 消费通道：payload canonical 字节（③端口 EvaluationOutput.payload 的
    // 承载形态）→ 三方各自解码还原"同一结果对象"。
    const std::vector<std::uint8_t> payloadBytes = dt::encodeMappingOutput(out);
    const dt::DriveTrainMappingOutput sel3 = dt::decodeMappingOutput(payloadBytes);
    const dt::DriveTrainMappingOutput sel4 = dt::decodeMappingOutput(payloadBytes);
    const dt::DriveTrainMappingOutput sel5 = dt::decodeMappingOutput(payloadBytes);

    // 三方切片各自携带的 robot-drivetrain 对象 ContentVersion 逐字节一致
    // （evidence"三种等价"纪律——位等）。
    EXPECT_EQ(sel3.identity.drivetrainObjectCv.bytes, out.identity.drivetrainObjectCv.bytes);
    EXPECT_EQ(sel4.identity.drivetrainObjectCv.bytes, out.identity.drivetrainObjectCv.bytes);
    EXPECT_EQ(sel5.identity.drivetrainObjectCv.bytes, out.identity.drivetrainObjectCv.bytes);

    // 接纳层核对原语（值比较面）。
    const dt::MatrixIdentityAlignment aligned = dt::checkMatrixContentIdentityAlignment(
        {sel3.identity, sel4.identity, sel5.identity});
    EXPECT_TRUE(aligned.aligned);

    // 篡改三方之一（不同配置版本）→不一致＋定位首个分歧切片。
    dt::DriveTrainIdentity tampered = sel5.identity;
    tampered.drivetrainObjectCv.bytes[0] ^= 0xFF; // 位翻转＝不同 ContentVersion
    const dt::MatrixIdentityAlignment diverged = dt::checkMatrixContentIdentityAlignment(
        {sel3.identity, sel4.identity, tampered});
    EXPECT_FALSE(diverged.aligned);
    EXPECT_EQ(diverged.firstDivergence, 2U);

    // 空清单按不一致处置（无消费切片可核对——防误用放行）。
    EXPECT_FALSE(dt::checkMatrixContentIdentityAlignment({}).aligned);
}
