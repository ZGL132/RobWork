/**
 * @file   MappingGoldenTest.cpp
 * @brief  传动映射黄金数据用例组（DtMappingGolden）——R1 对角映射解析
 *         算例、虚功/理想功率一致性、双向效率与再生、反射惯量与惯量比、
 *         峰值/RMS/四象限/能量分项、阻断面与病态阻断（任务契约
 *         WP-18-T03 acceptance 1/2 的测试面）。
 *
 * 设计依据：
 *   - units/drivetrain.md §6.2（R1 映射定义——黄金算例的公式权威）、
 *     §8.1/§8.2（虚功/理想功率一致性——黄金对照口径）、§9.2/§9.5（反射
 *     惯量与惯量比）、§10.2～§10.7（效率/能量/四象限）、§14.1（黄金数据
 *     集口径——解析算例＋独立参考实现对照，附录 D 第 9 项标量相对
 *     1×10⁻⁹）、§14.2 DT-G/B 组（故障注入矩阵——用例编号追溯）
 *   - 需求 DYN-04（M-12 精确虚功映射；黄金数据：双向效率/反射惯量/摩擦
 *     不重复计入）、NFR-COR-01（解析算例＋独立参考实现）、AT-07（双向
 *     效率/反射惯量/传动映射黄金数据）
 *   - 任务契约 tasks/foundation/WP-18-T03.json acceptance 1/2/3
 *
 * 黄金数据纪律（§14.1）：本文件的参考期望值全部由**测试侧独立参考实现**
 * 计算（逐元素标量公式——非矩阵库路径），与产品实现（DriveTrainMappingCore）
 * 形成两路求值；另以手算常数锚点（kAnchor*）钉住绝对量级防"两路同错"。
 * 容差：连续量标量相对 1×10⁻⁹（附录 D 第 9 项——本数据集按卡面默认，
 * 不私设更宽容差）；零值锚点用绝对容差。
 *
 * 用例与单元卡故障注入矩阵的对应关系在每 TEST 的注释首行标注（DT-G、
 * DT-B 组编号——§14.2 表行号）。
 */

#include <sdurws/ird/drivetrain/DiagCodes.hpp>
#include <sdurws/ird/drivetrain/MappingCore.hpp>
#include <sdurws/ird/drivetrain/Codec.hpp>
#include <sdurws/ird/drivetrain/Evaluator.hpp>
#include <sdurws/ird/drivetrain/Facts.hpp>

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
// 测试工具（runtime/kinematics 夹具先例的自持副本——固定种子派生 id）
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

/// 单轴关节/电机轴表快捷构造（串联序下标即身份序）。
dt::JointDriveAxis jointAxis(std::size_t index, dt::JointKind kind = dt::JointKind::Revolute)
{
    dt::JointDriveAxis a;
    a.jointId = idFrom<core::ObjectId>("dt-joint-" + std::to_string(index));
    a.kind = kind;
    a.localName = "joint" + std::to_string(index + 1);
    return a;
}

dt::MotorDriveAxis motorAxis(std::size_t index)
{
    dt::MotorDriveAxis a;
    a.motorId = idFrom<core::ObjectId>("dt-motor-" + std::to_string(index));
    a.jointIndex = index; // 轴序纪律：电机轴按对应关节串联序排列
    return a;
}

/// 传动配置身份快捷构造（版本字段>0——构造工厂校验面）。
dt::DriveTrainIdentity testIdentity()
{
    dt::DriveTrainIdentity id;
    id.drivetrainObjectCv.bytes = idFrom<core::ContentVersion>("dt-config-v1").bytes;
    id.algorithmVersion = 1;
    id.contractVersion = 1;
    return id;
}

/// 效率模型快捷构造。
dt::EfficiencyModel eta(double fwd, double bwd,
                        dt::SourcedValueTag src = dt::SourcedValueTag::CatalogBackfill)
{
    dt::EfficiencyModel e;
    e.etaForward = fwd;
    e.etaBackward = bwd;
    e.source = src;
    return e;
}

/// 转子惯量条目快捷构造（kg·m²，电机轴系）。
dt::RotorInertiaModel rotor(double j)
{
    dt::RotorInertiaModel r;
    r.rotorInertia = j;
    r.source = dt::SourcedValueTag::CatalogBackfill;
    return r;
}

/// 单工况正弦采样序列（DT-G1 黄金轨迹——q(t)＝A·sin(W·t)）。
dt::JointSeriesView sineSeries(std::size_t nJoints, double amplitude, double omega,
                               const std::vector<double>& times, double tauJoint,
                               const std::string& segmentId)
{
    dt::JointSeriesView s;
    for (std::size_t j = 0; j < nJoints; ++j) {
        s.jointIds.push_back(idFrom<core::ObjectId>("dt-joint-" + std::to_string(j)));
    }
    s.caseId = idFrom<core::ObjectId>("dt-case-golden");
    s.upstreamSliceId.bytes = idFrom<core::ContentIdentity>("dt-upstream-slice").bytes;
    for (const double t : times) {
        dt::JointDriveSample sample;
        sample.t = t;                                            // s
        sample.q = amplitude * std::sin(omega * t);              // rad
        sample.qd = amplitude * omega * std::cos(omega * t);     // rad/s
        sample.qdd = -amplitude * omega * omega * std::sin(omega * t); // rad/s²
        sample.tauJoint = tauJoint;                              // N·m（常值激励）
        sample.segmentId = segmentId;                            // 轨迹段（原样保持）
        s.samples.push_back(sample);
    }
    return s;
}

/// 相对容差断言（gtest pred_format 两值形态：两个表达式串在前、两个值
/// 在后——PRED_FORMAT2 展开序；附录 D 第 9 项：|a−e| ≤ 1e-9·|e|——黄金
/// 对照统一口径；e==0 时退化为绝对 1e-12，防除零歧义）。
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

}  // namespace

// =====================================================================
// DT-G1：单轴正传动比解析映射（acceptance 1——精确虚功映射；黄金数据）
// =====================================================================

/**
 * c=0.01（减速比 100:1）、J_rotor=1e-4 kg·m²、θ_off=0.5 rad、τ_joint=2 N·m
 * 的正弦轨迹：逐样本解析对照 θ/θ̇/θ̈/τ_ideal/τ_m/pJoint/pRotor（参考实现
 * 路径独立于产品代码）；另以 t=0.25s 手算锚点钉住绝对量级。
 */
TEST(DtMappingGolden, DiagonalMappingAnalytic_WP18T03_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-04", "NFR-COR-01"},
                  std::vector<std::string>{"AT-07"});

    // ---- 黄金模型：单轴 c=0.01＋转子＋零位偏置（位置映射偏置面同例钉住
    // ——DT-G4 合并断言）。
    dt::DriveTrainModel model = dt::makeDiagonalDriveTrainModel(
        {jointAxis(0)}, {motorAxis(0)},
        {dt::TransmissionRatio{0.01, dt::SourcedValueTag::ModelingField}},
        {0.5}, testIdentity(), {eta(0.9, 0.7)}, {rotor(1e-4)});

    // ---- 黄金轨迹：q(t)=0.1·sin(2πt)，t∈{0,0.125,...,0.5}。
    std::vector<double> times;
    for (int i = 0; i <= 4; ++i) {
        times.push_back(0.125 * i);
    }
    const double amplitude = 0.1; // rad
    const double omega = 6.283185307179586; // rad/s（2π——解析常数）
    dt::JointSeriesView series = sineSeries(1, amplitude, omega, times, 2.0, "seg-A");

    dt::DriveTrainMappingCore coreImpl;
    const dt::DriveTrainMappingOutput out = coreImpl.evaluate(model, series, nullptr);

    // 输出面规模：单轴单序列单工作点（§13.8 合法示例形态）。
    ASSERT_EQ(out.motorSeries.size(), 1U);
    ASSERT_EQ(out.points.size(), 1U);
    ASSERT_EQ(out.motorSeries[0].samples.size(), times.size());

    // ---- 逐样本独立参考实现对照（§14.1——标量公式路径，非矩阵库）。
    const double c = 0.01;         // 无量纲
    const double jRotor = 1e-4;    // kg·m²（电机轴系）
    const double thetaOff = 0.5;   // rad
    for (std::size_t i = 0; i < times.size(); ++i) {
        const double q = amplitude * std::sin(omega * times[i]);
        const double qd = amplitude * omega * std::cos(omega * times[i]);
        const double qdd = -amplitude * omega * omega * std::sin(omega * times[i]);
        const auto& s = out.motorSeries[0].samples[i];

        EXPECT_PRED_FORMAT2(closeRel, s.theta, thetaOff + q / c);       // rad
        EXPECT_PRED_FORMAT2(closeRel, s.thetaDot, qd / c);              // rad/s
        EXPECT_PRED_FORMAT2(closeRel, s.thetaDDot, qdd / c);            // rad/s²
        EXPECT_PRED_FORMAT2(closeRel, s.tauIdeal, c * 2.0);             // N·m
        EXPECT_PRED_FORMAT2(closeRel, s.tauMotor, c * 2.0 + jRotor * (qdd / c)); // N·m
        EXPECT_PRED_FORMAT2(closeRel, s.pJoint, 2.0 * qd);              // W
        EXPECT_PRED_FORMAT2(closeRel, s.pRotor, jRotor * (qdd / c) * (qd / c)); // W
    }

    // ---- 手算锚点（t=0.25s：q=0.1、q̇=0、q̈=−0.4π²≈−3.94784 rad/s²）。
    const auto& mid = out.motorSeries[0].samples[2];
    EXPECT_NEAR(mid.theta, 10.5, 1e-9);            // 0.5＋0.1/0.01
    EXPECT_NEAR(mid.thetaDot, 0.0, 1e-12);         // cos(π/2)=0
    EXPECT_NEAR(mid.thetaDDot, -394.7841760435743, 1e-6); // −0.4π²/0.01
    EXPECT_NEAR(mid.tauIdeal, 0.02, 1e-12);        // 0.01×2
    EXPECT_NEAR(mid.tauMotor, 0.02 - 0.03947841760435743, 1e-9); // ＋J·θ̈
}

// =====================================================================
// DT-G2：负传动比符号保留＋虚功恒等（§5.3 带符号 c——值传递通道）
// =====================================================================

TEST(DtMappingGolden, NegativeRatioSignPreserved_WP18T03_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-04"},
                  std::vector<std::string>{});

    // c=−0.01（电机反装——值传递通道；R1 项目对象通道不可达负值＝P-DT-5）。
    dt::DriveTrainModel model = dt::makeDiagonalDriveTrainModel(
        {jointAxis(0)}, {motorAxis(0)},
        {dt::TransmissionRatio{-0.01, dt::SourcedValueTag::UserProvided}},
        {0.0}, testIdentity(), {eta(0.9, 0.7)}, {rotor(1e-4)});

    std::vector<double> times{0.0, 0.25, 0.5};
    dt::JointSeriesView series = sineSeries(1, 0.1, 6.283185307179586, times, 2.0, "seg-B");

    dt::DriveTrainMappingCore coreImpl;
    const dt::DriveTrainMappingOutput out = coreImpl.evaluate(model, series, nullptr);
    ASSERT_EQ(out.motorSeries.size(), 1U);

    for (std::size_t i = 0; i < times.size(); ++i) {
        const auto& js = series.samples[i];
        const auto& s = out.motorSeries[0].samples[i];
        // 符号逐轴保留：c<0 ⟹ 速度/力矩反向（对角元素携带自身符号——
        // D-DT-6，对角形式不丢轴符号）。
        EXPECT_LT(s.thetaDot * js.qd, 0.0) << "样本 " << i;
        EXPECT_LT(s.tauIdeal * js.tauJoint, 0.0) << "样本 " << i;
        // 虚功恒等（理想口径——§8.1）：τ_m·θ̇ 与 τ_j·q̇ 相对 1e-9 相等
        //（数学恒等 c·τ·(q̇/c)＝τ·q̇；浮点路径差异在容差内）。
        const double wMotor = s.tauIdeal * s.thetaDot;
        const double wJoint = js.tauJoint * js.qd;
        EXPECT_NEAR(wMotor, wJoint, 1e-9 * std::max(1.0, std::fabs(wJoint)))
            << "虚功恒等破坏（样本 " << i << "）";
    }
}

// =====================================================================
// DT-G3：多轴对角逐轴独立（各异 c——轴间无串扰）
// =====================================================================

TEST(DtMappingGolden, MultiAxisDiagonalIndependence_WP18T03_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-04"},
                  std::vector<std::string>{});

    const double c1 = 0.01;
    const double c2 = 0.005;
    const double c3 = 0.02;
    dt::DriveTrainModel model = dt::makeDiagonalDriveTrainModel(
        {jointAxis(0), jointAxis(1), jointAxis(2)},
        {motorAxis(0), motorAxis(1), motorAxis(2)},
        {{c1, dt::SourcedValueTag::ModelingField},
         {c2, dt::SourcedValueTag::ModelingField},
         {c3, dt::SourcedValueTag::ModelingField}},
        {0.0, 0.1, 0.2}, testIdentity(), {eta(0.9, 0.7), eta(0.85, 0.6)},
        {rotor(1e-4), rotor(2e-4), rotor(5e-4)});

    std::vector<double> times{0.0, 0.125, 0.25};
    dt::JointSeriesView series = sineSeries(3, 0.1, 6.283185307179586, times, 2.0, "seg-C");

    dt::DriveTrainMappingCore coreImpl;
    const dt::DriveTrainMappingOutput out = coreImpl.evaluate(model, series, nullptr);
    ASSERT_EQ(out.motorSeries.size(), 3U);

    const double expectedC[3] = {c1, c2, c3};
    for (std::size_t k = 0; k < 3; ++k) {
        for (std::size_t i = 0; i < times.size(); ++i) {
            const auto& js = series.samples[i];
            const auto& s = out.motorSeries[k].samples[i];
            // 逐轴独立：每轴只用自己的 c_j（轴序不串——DT-G3 观测点）。
            EXPECT_PRED_FORMAT2(closeRel, s.thetaDot, js.qd / expectedC[k]);
            EXPECT_PRED_FORMAT2(closeRel, s.tauIdeal, expectedC[k] * js.tauJoint);
        }
    }
}

// =====================================================================
// DT-G5：虚功/理想功率一致性逐元素恒等（§8.1/§8.2＋检查器）
// =====================================================================

TEST(DtMappingGolden, VirtualWorkAndIdealPowerConsistency_WP18T03_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-04"},
                  std::vector<std::string>{"AT-07"});

    dt::DriveTrainModel model = dt::makeDiagonalDriveTrainModel(
        {jointAxis(0), jointAxis(1)},
        {motorAxis(0), motorAxis(1)},
        {{0.01, dt::SourcedValueTag::ModelingField},
         {-0.02, dt::SourcedValueTag::UserProvided}},
        {0.0, 0.0}, testIdentity(), {eta(0.9, 0.7), eta(0.9, 0.7)},
        {rotor(1e-4), rotor(1e-4)});

    std::vector<double> times{0.0, 0.1, 0.2, 0.3};
    dt::JointSeriesView series = sineSeries(2, 0.1, 6.283185307179586, times, 2.0, "seg-D");

    dt::DriveTrainMappingCore coreImpl;
    const dt::DriveTrainMappingOutput out = coreImpl.evaluate(model, series, nullptr);
    ASSERT_EQ(out.motorSeries.size(), 2U);

    // ---- 逐样本逐元素恒等（§8.2 口径②——不以总和替代，防正负抵消）：
    // 理想功率 τ_ideal·θ̇ 与关节功率 τ·q̇ 相对 1e-9 相等。
    for (std::size_t k = 0; k < 2; ++k) {
        for (std::size_t i = 0; i < times.size(); ++i) {
            const auto& js = series.samples[i];
            const auto& s = out.motorSeries[k].samples[i];
            const double pIdeal = s.tauIdeal * s.thetaDot;
            EXPECT_NEAR(pIdeal, js.tauJoint * js.qd,
                        1e-9 * std::max(1.0, std::fabs(js.tauJoint * js.qd)))
                << "理想功率恒等破坏（轴 " << k << " 样本 " << i << "）";
        }
    }

    // ---- 检查器自检（§8.4 ②映射自检形态——精确位等判据）：喂入与期望
    // **同一表达式路径**的值（τ_m_ideal＝c·τ_j 与功率＝τ_j·q̇——直接用
    // 上游样本算出），应零发现。注意口径边界：检查器用位等（无阈值——
    // §8.4"运行侧只做精确判据"），而"τ_ideal·θ̇ ≈ τ_j·q̇"的黄金对照属
    // 浮点不同求值路径，其 1e-9 容差断言已在上段完成——两口径不得混用。
    std::vector<double> actualTau;
    std::vector<double> actualPower;
    for (std::size_t i = 0; i < times.size(); ++i) {
        for (std::size_t k = 0; k < 2; ++k) {
            actualTau.push_back(model.ratios[k].c * series.samples[i].tauJoint);
            actualPower.push_back(series.samples[i].tauJoint * series.samples[i].qd);
        }
    }
    EXPECT_TRUE(coreImpl.checkVirtualWork(model, series, actualTau).empty());
    EXPECT_TRUE(coreImpl.checkPowerBalance(model, series, actualPower).empty());

    // ---- 注入一处错值：检查器逐元素定位（样本/轴/实际/期望——ERR-01）。
    actualTau[1 * 2 + 0] += 1e-6; // 轴 0 样本 1
    const std::vector<dt::ConsistencyFinding> findings
        = coreImpl.checkVirtualWork(model, series, actualTau);
    ASSERT_EQ(findings.size(), 1U);
    EXPECT_EQ(findings[0].sampleIndex, 1U);
    EXPECT_EQ(findings[0].axisIndex, 0U);
    EXPECT_EQ(findings[0].field, "tau_ideal_motor[N*m]");
}

// =====================================================================
// 等价验证（NFR-COR-01——常矩阵 C 下与对角传动比等价；契约 acceptance 2）
// =====================================================================

/**
 * 独立参考实现：把对角映射按"常矩阵 C"的矩阵语义计算（τ_m＝Cᵀ·τ_j、
 * θ̇＝C⁻¹·q̇——测试侧显式写矩阵转置/求逆循环），与核心对角输出逐元素
 * 对照（相对 1×10⁻⁹）——验证"无耦合链等价于对角传动比映射"（M-12/DYN-04）
 * 的实现一致性。
 */
TEST(DtMappingGolden, MatrixFormEquivalentToDiagonal_WP18T03_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-01", "DYN-04"},
                  std::vector<std::string>{"AT-38"});

    const std::size_t n = 3;
    const double cArr[3] = {0.01, 0.005, 0.02};
    dt::DriveTrainModel model = dt::makeDiagonalDriveTrainModel(
        {jointAxis(0), jointAxis(1), jointAxis(2)},
        {motorAxis(0), motorAxis(1), motorAxis(2)},
        {{cArr[0], dt::SourcedValueTag::ModelingField},
         {cArr[1], dt::SourcedValueTag::ModelingField},
         {cArr[2], dt::SourcedValueTag::ModelingField}},
        {0.0, 0.0, 0.0}, testIdentity(), {eta(0.9, 0.7), eta(0.9, 0.7), eta(0.9, 0.7)},
        {rotor(1e-4), rotor(1e-4), rotor(1e-4)});

    std::vector<double> times{0.0, 0.125, 0.25, 0.375};
    dt::JointSeriesView series = sineSeries(n, 0.1, 6.283185307179586, times, 2.0, "seg-E");

    dt::DriveTrainMappingCore coreImpl;
    const dt::DriveTrainMappingOutput out = coreImpl.evaluate(model, series, nullptr);
    ASSERT_EQ(out.motorSeries.size(), n);

    // ---- 矩阵语义参考实现：C＝diag(c)；Cᵀ 第 (j,k) 元素＝C(k,j)——力矩
    // τ_m[k]＝Σ_j Cᵀ(k,j)·τ_j[j]＝C(k,k)·τ_j[k]（对角）；速度 θ̇＝C⁻¹·q̇
    // 的第 k 分量＝q̇[k]/C(k,k)。以完整双重循环形式书写（非逐轴捷径），
    // 与核心输出逐元素对照（相对 1×10⁻⁹）。注意：非对角元素严格为 0
    //（工厂构造），只在 (k,k) 处做求逆除法——避免 1/0 参与浮点运算。
    for (std::size_t i = 0; i < times.size(); ++i) {
        for (std::size_t k = 0; k < n; ++k) {
            double tauMotorMatrix = 0.0; // Cᵀ·τ_j 的第 k 行
            double thetaDotMatrix = 0.0; // C⁻¹·q̇ 的第 k 行（对角求逆）
            for (std::size_t j = 0; j < n; ++j) {
                tauMotorMatrix += model.chat(j, k) * series.samples[i].tauJoint;
                if (k == j) {
                    thetaDotMatrix += series.samples[i].qd / model.chat(k, j);
                }
            }
            EXPECT_PRED_FORMAT2(closeRel, out.motorSeries[k].samples[i].tauIdeal,
                                tauMotorMatrix);
            EXPECT_PRED_FORMAT2(closeRel, out.motorSeries[k].samples[i].thetaDot,
                                thetaDotMatrix);
        }
    }
}

// =====================================================================
// DT-B1/B2/B3＋DT-G13：阻断面与病态阻断（acceptance 2——诊断并阻止）
// =====================================================================

/// 直接构造一个"绕过工厂"的模型（阻断面测试需要非法形态——工厂会拒绝）。
dt::DriveTrainModel rawModel(std::vector<dt::JointDriveAxis> joints,
                             std::vector<dt::MotorDriveAxis> motors,
                             dt::RowMatrix chat,
                             std::vector<dt::TransmissionRatio> ratios)
{
    dt::DriveTrainModel m;
    m.jointAxes = std::move(joints);
    m.motorAxes = std::move(motors);
    m.chat = std::move(chat);
    m.ratios = std::move(ratios);
    m.zeroOffsetMotor = std::vector<double>(m.motorAxes.size(), 0.0);
    m.identity = testIdentity();
    return m;
}

/// 2×2 对角矩阵快捷构造。
dt::RowMatrix diag2(double a, double b)
{
    dt::RowMatrix mx;
    mx.rows = 2;
    mx.cols = 2;
    mx.data = {a, 0.0, 0.0, b};
    return mx;
}

TEST(DtMappingGolden, BlockFaceRejections_WP18T03_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-04", "NFR-COR-03"},
                  std::vector<std::string>{"AT-38"});

    dt::DriveTrainMappingCore coreImpl;
    dt::DriveTrainModel good = dt::makeDiagonalDriveTrainModel(
        {jointAxis(0), jointAxis(1)}, {motorAxis(0), motorAxis(1)},
        {{0.01, dt::SourcedValueTag::ModelingField},
         {0.02, dt::SourcedValueTag::ModelingField}},
        {0.0, 0.0}, testIdentity());

    // ---- DT-B1：R1 非对角矩阵被阻断（ evaluate 与 validator 双路径；
    // 不对角化、不拆轴）。chat 非对角元素 1e-6 非零。
    dt::DriveTrainModel nondiag = rawModel(
        {jointAxis(0), jointAxis(1)}, {motorAxis(0), motorAxis(1)},
        dt::RowMatrix{2, 2, {0.01, 1e-6, 0.0, 0.02}},
        {{0.01, dt::SourcedValueTag::ModelingField},
         {0.02, dt::SourcedValueTag::ModelingField}});
    dt::JointSeriesView series = sineSeries(2, 0.1, 6.283185307179586, {0.0, 0.1}, 2.0, "s");
    try {
        (void)coreImpl.evaluate(nondiag, series, nullptr);
        FAIL() << "非对角输入必须被阻断（DT-MATRIX-NONDIAGONAL-LOCKED）";
    } catch (const std::invalid_argument& ex) {
        EXPECT_TRUE(messageStartsWith(ex, dt::kDtMatrixNondiagonalLocked)) << ex.what();
    }
    const dt::CouplingValidationResult nondiagGate
        = coreImpl.validate(nondiag, dt::StageCapability::R1Capability);
    EXPECT_FALSE(nondiagGate.accepted);
    ASSERT_FALSE(nondiagGate.diagnostics.empty());
    EXPECT_EQ(nondiagGate.diagnostics.front().code, dt::kDtMatrixNondiagonalLocked);

    // ---- DT-B3：耦合窗口输入被能力门控阻断（R1 能力——"不提前放开 R1
    // 阻断"红线在 T05 落位后继续生效：三参 evaluate 恒按 R1 语义）。R2
    // 能力位下窗口进入 §7.2 矩阵路径（WP-18-T05）——退化空窗口按维度
    // 违约拒绝（仍阻断、不降级，码面更精确； MappingR2GoldenTest 承载
    // R2 合法窗口的正命题面）。
    dt::DriveTrainModel windowed = good;
    windowed.window = dt::CouplingWindow{};
    {
        // R1 语义（三参 evaluate——恒 R1；validator R1 位）：
        try {
            (void)coreImpl.evaluate(windowed, series, nullptr);
            FAIL() << "R1 能力下耦合窗口输入必须被阻断（DT-COUPLING-STAGE-LOCKED）";
        } catch (const std::invalid_argument& ex) {
            EXPECT_TRUE(messageStartsWith(ex, dt::kDtCouplingStageLocked)) << ex.what();
        }
        const dt::CouplingValidationResult gateR1
            = coreImpl.validate(windowed, dt::StageCapability::R1Capability);
        EXPECT_FALSE(gateR1.accepted);
        ASSERT_FALSE(gateR1.diagnostics.empty());
        EXPECT_EQ(gateR1.diagnostics.front().code, dt::kDtCouplingStageLocked);

        // R2 能力位（T05 矩阵路径）：空窗口＝退化声明——DT-INPUT-DIMENSION-
        // MISMATCH（阻断不降级；不再是能力门控码——能力已启用，问题在
        // 输入形态）。
        const dt::CouplingValidationResult gateR2
            = coreImpl.validate(windowed, dt::StageCapability::R2Capability);
        EXPECT_FALSE(gateR2.accepted);
        ASSERT_FALSE(gateR2.diagnostics.empty());
        EXPECT_EQ(gateR2.diagnostics.front().code, dt::kDtInputDimensionMismatch);
    }

    // ---- DT-B2：prismatic 轴范围外（不静默套用旋转传动）。工厂构造入口
    // 与映射入口同码拒绝（两道防线同一语义——构造与 evaluate 双路径核对）。
    try {
        dt::DriveTrainModel prism = dt::makeDiagonalDriveTrainModel(
            {jointAxis(0, dt::JointKind::Prismatic)}, {motorAxis(0)},
            {{0.01, dt::SourcedValueTag::ModelingField}}, {0.0}, testIdentity());
        (void)prism;
        // 若工厂漏拦（不应发生），evaluate 入口必须拦——两道防线任一命中
        // 即阻断；下方 evaluate 用 rawModel 绕过工厂构造直接验证映射入口。
        dt::DriveTrainModel rawPrism = rawModel(
            {jointAxis(0, dt::JointKind::Prismatic)}, {motorAxis(0)},
            dt::RowMatrix{1, 1, {0.01}}, {{0.01, dt::SourcedValueTag::ModelingField}});
        (void)coreImpl.evaluate(rawPrism, series, nullptr);
        FAIL() << "prismatic 轴必须范围外阻断（DT-AXIS-TYPE-OUT-OF-SCOPE）";
    } catch (const std::invalid_argument& ex) {
        EXPECT_TRUE(messageStartsWith(ex, dt::kDtAxisTypeOutOfScope)) << ex.what();
    }

    // ---- DT-G13：结构有效性族（首个命中码逐项核对）。
    // c=0 → DT-RATIO-ZERO。
    try {
        (void)dt::makeDiagonalDriveTrainModel(
            {jointAxis(0)}, {motorAxis(0)},
            {dt::TransmissionRatio{0.0, dt::SourcedValueTag::ModelingField}},
            {0.0}, testIdentity());
        FAIL() << "c=0 必须被拒绝（DT-RATIO-ZERO）";
    } catch (const std::invalid_argument& ex) {
        EXPECT_TRUE(messageStartsWith(ex, dt::kDtRatioZero)) << ex.what();
    }
    // NaN 矩阵 → DT-MATRIX-NONFINITE。
    {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        dt::DriveTrainModel bad = rawModel(
            {jointAxis(0)}, {motorAxis(0)}, dt::RowMatrix{1, 1, {nan}},
            {{1.0, dt::SourcedValueTag::ModelingField}});
        const dt::CouplingValidationResult gate
            = coreImpl.validate(bad, dt::StageCapability::R1Capability);
        EXPECT_FALSE(gate.accepted);
        ASSERT_FALSE(gate.diagnostics.empty());
        EXPECT_EQ(gate.diagnostics.front().code, dt::kDtMatrixNonfinite);
    }
    // 维度不匹配 → DT-INPUT-DIMENSION-MISMATCH。
    {
        dt::DriveTrainModel bad = rawModel(
            {jointAxis(0), jointAxis(1)}, {motorAxis(0)}, diag2(0.01, 0.02),
            {{0.01, dt::SourcedValueTag::ModelingField},
             {0.02, dt::SourcedValueTag::ModelingField}});
        const dt::CouplingValidationResult gate
            = coreImpl.validate(bad, dt::StageCapability::R1Capability);
        EXPECT_FALSE(gate.accepted);
        ASSERT_FALSE(gate.diagnostics.empty());
        EXPECT_EQ(gate.diagnostics.front().code, dt::kDtInputDimensionMismatch);
    }
    // 轴序不一致 → DT-INPUT-AXIS-ORDER-MISMATCH（不静默重排）。
    try {
        dt::DriveTrainModel bad
            = dt::makeDiagonalDriveTrainModel({jointAxis(0), jointAxis(1)},
                                              {motorAxis(1), motorAxis(0)},
                                              {{0.01, dt::SourcedValueTag::ModelingField},
                                               {0.02, dt::SourcedValueTag::ModelingField}},
                                              {0.0, 0.0}, testIdentity());
        (void)bad; // 工厂本身拒绝——能到达此处说明工厂漏拦，下行断言防回归
        FAIL() << "轴序不一致必须在构造工厂被拒绝";
    } catch (const std::invalid_argument& ex) {
        EXPECT_TRUE(messageStartsWith(ex, dt::kDtInputAxisOrderMismatch)) << ex.what();
    }
    // 空轴表 → DT-INPUT-EMPTY。
    try {
        (void)dt::makeDiagonalDriveTrainModel({}, {}, {}, {}, testIdentity());
        FAIL() << "空轴表必须被拒绝（DT-INPUT-EMPTY）";
    } catch (const std::invalid_argument& ex) {
        EXPECT_TRUE(messageStartsWith(ex, dt::kDtInputEmpty)) << ex.what();
    }
    // 病态对角（条件数 1e9＞1e8）→ DT-MATRIX-ILL-CONDITIONED（比较型：
    // 实际条件数/阈值/无量纲——ERR-01/AT-27）。
    {
        dt::DriveTrainModel bad = rawModel(
            {jointAxis(0), jointAxis(1)}, {motorAxis(0), motorAxis(1)},
            diag2(1e-9, 1.0),
            {{1e-9, dt::SourcedValueTag::ModelingField},
             {1.0, dt::SourcedValueTag::ModelingField}});
        const dt::CouplingValidationResult gate
            = coreImpl.validate(bad, dt::StageCapability::R1Capability);
        EXPECT_FALSE(gate.accepted);
        EXPECT_NEAR(gate.conditionNumber, 1e9, 1.0); // 实测条件数（对角解析）
        ASSERT_FALSE(gate.diagnostics.empty());
        EXPECT_EQ(gate.diagnostics.front().code, dt::kDtMatrixIllConditioned);
        // 比较型三要素（实际/期望/单位——ERR-01）。
        ASSERT_TRUE(gate.diagnostics.front().comparison.has_value());
        EXPECT_NEAR(gate.diagnostics.front().comparison->actual.quantity.value(), 1e9, 1.0);
        EXPECT_NEAR(gate.diagnostics.front().comparison->expected.quantity.value(),
                    dt::kWellConditionedLimit, 1.0);
        EXPECT_EQ(gate.diagnostics.front().comparison->actual.unit.symbol(), "1");
    }
    // 良态边界内（条件数 1e8）不被阻断（阈值含边界——≤1×10⁸ 合法）。
    {
        dt::DriveTrainModel edge = rawModel(
            {jointAxis(0), jointAxis(1)}, {motorAxis(0), motorAxis(1)},
            diag2(1e-8, 1.0),
            {{1e-8, dt::SourcedValueTag::ModelingField},
             {1.0, dt::SourcedValueTag::ModelingField}});
        const dt::CouplingValidationResult gate
            = coreImpl.validate(edge, dt::StageCapability::R1Capability);
        EXPECT_TRUE(gate.accepted);
    }
}

// =====================================================================
// 工厂校验面（构造入口 fail-fast——值非法族）
// =====================================================================

TEST(DtMappingGolden, FactoryValueValidation_WP18T03_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-03"},
                  std::vector<std::string>{});

    // η 越界（>1）→ DT-EFFICIENCY-INVALID。
    try {
        (void)dt::makeDiagonalDriveTrainModel(
            {jointAxis(0)}, {motorAxis(0)},
            {{0.01, dt::SourcedValueTag::ModelingField}}, {0.0}, testIdentity(),
            {eta(1.5, 0.7)});
        FAIL() << "η>1 必须被拒绝（DT-EFFICIENCY-INVALID）";
    } catch (const std::invalid_argument& ex) {
        EXPECT_TRUE(messageStartsWith(ex, dt::kDtEfficiencyInvalid)) << ex.what();
    }
    // 转子惯量非正 → DT-INERTIA-INVALID。
    try {
        (void)dt::makeDiagonalDriveTrainModel(
            {jointAxis(0)}, {motorAxis(0)},
            {{0.01, dt::SourcedValueTag::ModelingField}}, {0.0}, testIdentity(),
            {eta(0.9, 0.7)}, {rotor(0.0)});
        FAIL() << "J_rotor=0 必须被拒绝（DT-INERTIA-INVALID）";
    } catch (const std::invalid_argument& ex) {
        EXPECT_TRUE(messageStartsWith(ex, dt::kDtInertiaInvalid)) << ex.what();
    }
    // 身份版本 0（保留值）拒绝——消息携带字段名（contractVersion）定位。
    {
        dt::DriveTrainIdentity badId = testIdentity();
        badId.contractVersion = 0;
        try {
            (void)dt::makeDiagonalDriveTrainModel(
                {jointAxis(0)}, {motorAxis(0)},
                {{0.01, dt::SourcedValueTag::ModelingField}}, {0.0}, badId);
            FAIL() << "契约版本 0 必须被拒绝（CON-04 保留值）";
        } catch (const std::invalid_argument& ex) {
            EXPECT_TRUE(std::string(ex.what()).find("contractVersion") != std::string::npos)
                << ex.what();
        }
    }
}

// =====================================================================
// 转子项单一计入（acceptance 2"摩擦不重复计入/反射惯量"的力矩面）
// =====================================================================

/**
 * 黄金前提（§9.4/§14.1 组④⑤）：关节侧 τ_joint 已含摩擦（RNEA 权威）且
 * **不含**转子反射效应；本卡唯一计入位置＝电机侧显式项 J_rotor·θ̈。
 * 断言：τ_m−τ_ideal 恰为 J_rotor·θ̈（单一计入——无第二处叠加）；τ_joint
 * 中的摩擦不产生任何额外力矩/损耗项（本卡不叠加第二套摩擦模型——MDL-16
 * 边界：损耗只经 η 折算）。
 */
TEST(DtMappingGolden, RotorSingleInclusionAndNoExtraFriction_WP18T03_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-04", "SEL-10"},
                  std::vector<std::string>{"AT-07", "AT-30"});

    dt::DriveTrainModel model = dt::makeDiagonalDriveTrainModel(
        {jointAxis(0)}, {motorAxis(0)},
        {{0.01, dt::SourcedValueTag::ModelingField}}, {0.0}, testIdentity(),
        {eta(0.8, 0.6)}, {rotor(2e-4)});

    // 轨迹含匀速段（θ̈=0——转子项恰零）与加减速段；τ_joint 含"摩擦分量"
    //（0.3 N·m 常值叠加——上游 RNEA 已计入的黄金前提，本卡不重复建模）。
    std::vector<double> times{0.0, 0.25, 0.5};
    dt::JointSeriesView series = sineSeries(1, 0.1, 6.283185307179586, times, 2.3, "seg-F");

    dt::DriveTrainMappingCore coreImpl;
    const dt::DriveTrainMappingOutput out = coreImpl.evaluate(model, series, nullptr);
    ASSERT_EQ(out.motorSeries.size(), 1U);

    for (std::size_t i = 0; i < times.size(); ++i) {
        const auto& s = out.motorSeries[0].samples[i];
        // 单一计入的准确表达：输出与 M-12 公式**逐位一致**（τ_m 与
        // τ_ideal＋J_rotor·θ̈ 同一表达式求值——位等成立），即除显式转子项
        // 外无任何第二处叠加（重复计入会使位等破坏）。
        EXPECT_EQ(s.tauMotor, s.tauIdeal + 2e-4 * s.thetaDDot)
            << "转子项必须只计入一次（样本 " << i << "）";
        // 匀速样本（θ̈ 精确零——t=0 处 sin(0)=0）：τ_m＝τ_ideal＝c·τ_joint
        //（摩擦只经 τ_joint 进入，无额外摩擦项——量级锚定）。
        if (s.thetaDDot == 0.0) {
            EXPECT_EQ(s.tauMotor, 0.01 * 2.3);
        }
    }
}

// =====================================================================
// DT-G6：双向效率与再生（AT-07"双向效率"——方向折算＋E_loss＞0 恒等式）
// =====================================================================

/**
 * η⁺=0.9/η⁻=0.7 的混合循环（正功段 2 s＋再生段 1 s，匀速 θ̈=0——转子项
 * 恰零，效率折算面单独可验）。能量期望按**梯形积分**语义计算（§10.5
 * 冻结口径——阶跃功率在段边界的过渡区间贡献＝两端均值×时长）：
 *   pJoint＝[10,10,−10,−10]（τ·q̇，q̇=1）⟹
 *   E_joint＝10＋0−10＝0 J（段边界过渡区间正负对消）；
 *   E_pos＝10＋5＋0＝15 J；E_regen＝0＋5＋10＝15 J；
 *   pTrans＝[11.111,11.111,−7,−7]⟹E_motor＝11.111＋2.0556−7＝6.1667 J；
 *   E_loss＝1.111＋2.0556＋3＝6.1667 J（恒＞0——两方向折算均消耗）。
 * 象限：ω＞0 恒，正功段 P_quad＝τ_m·θ̇＞0→Q1；再生段 τ_m＜0→Q2
 * （象限能量按左矩形约定——与占比同区间，见 summarize 注释）。
 */
TEST(DtMappingGolden, BidirectionalEfficiencyEnergy_WP18T03_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-04"},
                  std::vector<std::string>{"AT-07"});

    dt::DriveTrainModel model = dt::makeDiagonalDriveTrainModel(
        {jointAxis(0)}, {motorAxis(0)},
        {{0.01, dt::SourcedValueTag::ModelingField}}, {0.0}, testIdentity(),
        {eta(0.9, 0.7)}, {rotor(1e-4)});

    // 匀速轨迹（q̈=0——q̇=1 rad/s 常值；τ_joint 分段常值）。
    dt::JointSeriesView series;
    series.jointIds = {idFrom<core::ObjectId>("dt-joint-0")};
    series.caseId = idFrom<core::ObjectId>("dt-case-g6");
    series.upstreamSliceId.bytes = idFrom<core::ContentIdentity>("dt-up-g6").bytes;
    const double tauSeg[4] = {10.0, 10.0, -10.0, -10.0}; // N·m（正功段＋再生段）
    for (std::size_t i = 0; i < 4; ++i) {
        dt::JointDriveSample s;
        s.t = static_cast<double>(i); // s（间隔 1 s）
        s.qd = 1.0;                   // rad/s（匀速）
        s.qdd = 0.0;                  // rad/s²（转子项恰零）
        s.tauJoint = tauSeg[i];
        s.segmentId = (i < 2) ? "seg-drive" : "seg-regen";
        series.samples.push_back(s);
    }

    dt::DriveTrainMappingCore coreImpl;
    const dt::DriveTrainMappingOutput out = coreImpl.evaluate(model, series, nullptr);
    ASSERT_EQ(out.points.size(), 1U);
    const auto& p = out.points[0];
    const auto& s0 = out.motorSeries[0].samples[0];
    const auto& s2 = out.motorSeries[0].samples[2];

    // 方向折算（§10.2）：正功 P_trans＝P/η⁺（样本 0）；再生 P_trans＝P·η⁻
    //（样本 2——τ<0、q̇>0）。
    EXPECT_PRED_FORMAT2(closeRel, s0.pTransmission, 10.0 / 0.9);  // W（驱动）
    EXPECT_PRED_FORMAT2(closeRel, s2.pTransmission, -10.0 * 0.7); // W（再生）
    EXPECT_TRUE(s0.efficiencyApplicable);

    // 能量分项（§10.5 梯形积分——期望见用例注释；actual 值已按实现语义
    // 独立手算复核）。
    EXPECT_PRED_FORMAT2(closeRel, p.energy.ePos, 15.0);
    EXPECT_PRED_FORMAT2(closeRel, p.energy.eRegen, 15.0);
    EXPECT_PRED_FORMAT2(closeRel, p.energy.eJoint, 0.0);
    EXPECT_PRED_FORMAT2(closeRel, p.energy.eLoss,
                        10.0 / 9.0 + (10.0 / 0.9 - 7.0) / 2.0 + 3.0);
    EXPECT_GT(p.energy.eLoss, 0.0); // 损耗恒正（两方向折算均消耗——§10.5）
    EXPECT_PRED_FORMAT2(closeRel, p.energy.eMotor,
                        10.0 / 0.9 + (10.0 / 0.9 - 7.0) / 2.0 - 7.0);
    EXPECT_PRED_FORMAT2(closeRel, p.energy.eRotor, 0.0); // 匀速：转子往返净额 0

    // 象限（§10.6——P_quad＝τ_m·θ̇；ω＞0 恒）：正功段 Q1、再生段 Q2。
    EXPECT_EQ(p.q1.sampleCount, 2U);
    EXPECT_PRED_FORMAT2(closeRel, p.q1.timeShare, 2.0 / 3.0);
    EXPECT_PRED_FORMAT2(closeRel, p.q1.energy, 20.0); // 象限能量＝左矩形 ∫pJoint
    EXPECT_EQ(p.q2.sampleCount, 2U);
    EXPECT_PRED_FORMAT2(closeRel, p.q2.energy, -10.0);
    EXPECT_EQ(p.zeroDwell.sampleCount, 0U);
}

// =====================================================================
// DT-G7：零功率/驻留（效率不适用显式标记＋RMS 含驻留——§10.2/§10.4）
// =====================================================================

TEST(DtMappingGolden, DwellAndZeroPower_WP18T03_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-04"},
                  std::vector<std::string>{});

    // c=1（直驱——标量易读）；驻留 2 s（q̇=0、τ 保持）＋运动 1 s。
    dt::DriveTrainModel model = dt::makeDiagonalDriveTrainModel(
        {jointAxis(0)}, {motorAxis(0)},
        {{1.0, dt::SourcedValueTag::ModelingField}}, {0.0}, testIdentity(),
        {eta(0.9, 0.7)}, {rotor(1e-4)});

    dt::JointSeriesView series;
    series.jointIds = {idFrom<core::ObjectId>("dt-joint-0")};
    series.caseId = idFrom<core::ObjectId>("dt-case-g7");
    const double tau[4] = {5.0, 5.0, 3.0, 3.0}; // N·m（驻留保持＋运动段）
    for (std::size_t i = 0; i < 4; ++i) {
        dt::JointDriveSample s;
        s.t = static_cast<double>(i);
        s.qd = (i < 2) ? 0.0 : 2.0; // rad/s（驻留段速度恰零）
        s.tauJoint = tau[i];
        s.segmentId = (i < 2) ? "seg-dwell" : "seg-move";
        series.samples.push_back(s);
    }

    dt::DriveTrainMappingCore coreImpl;
    const dt::DriveTrainMappingOutput out = coreImpl.evaluate(model, series, nullptr);
    ASSERT_EQ(out.points.size(), 1U);
    const auto& p = out.points[0];

    // 驻留样本效率不适用（精确零——显式标记，不伪造数值）。
    EXPECT_FALSE(out.motorSeries[0].samples[0].efficiencyApplicable);
    EXPECT_EQ(out.motorSeries[0].samples[0].pTransmission, 0.0);
    EXPECT_TRUE(out.motorSeries[0].samples[2].efficiencyApplicable);

    // RMS 含驻留（§10.4——力矩保持计入热负载）：τ_m=[5,5,3,3]、T=3、梯形
    // ∫τ²dt＝25＋17＋9＝51 ⟹ RMS(τ)=√(51/3)；ω=[0,0,2,2]、∫ω²dt＝0＋2＋4＝6
    // ⟹ RMS(ω)=√(6/3)=√2。
    EXPECT_PRED_FORMAT2(closeRel, p.tauRms, std::sqrt(51.0 / 3.0));
    EXPECT_PRED_FORMAT2(closeRel, p.omegaRms, std::sqrt(6.0 / 3.0));

    // 零速样本只计时间占比（2/3），不计象限能量（§10.6 表行 5）。
    EXPECT_EQ(p.zeroDwell.sampleCount, 2U);
    EXPECT_PRED_FORMAT2(closeRel, p.zeroDwell.timeShare, 2.0 / 3.0);
    EXPECT_EQ(p.zeroDwell.energy, 0.0);
    EXPECT_EQ(p.q1.sampleCount, 2U);
    // 能量分项不含驻留区间（零功率样本置 0 进梯形积分——§8.3 剔除语义）：
    // pJoint=[0,0,6,6]⟹E_pos＝E_joint＝0＋3＋6＝9 J。
    EXPECT_PRED_FORMAT2(closeRel, p.energy.ePos, 9.0);
    EXPECT_PRED_FORMAT2(closeRel, p.energy.eJoint, 9.0);
}

// =====================================================================
// DT-G8：反射惯量与惯量比（AT-07"反射惯量"——J/c² 与 c²·J_load/J_rotor）
// =====================================================================

TEST(DtMappingGolden, ReflectedInertiaAndInertiaRatio_WP18T03_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-04", "SEL-05"},
                  std::vector<std::string>{"AT-07"});

    dt::DriveTrainModel model = dt::makeDiagonalDriveTrainModel(
        {jointAxis(0), jointAxis(1)}, {motorAxis(0), motorAxis(1)},
        {{0.01, dt::SourcedValueTag::ModelingField},
         {0.02, dt::SourcedValueTag::ModelingField}},
        {0.0, 0.0}, testIdentity(), {eta(0.9, 0.7), eta(0.9, 0.7)},
        {rotor(1e-4), rotor(4e-4)});

    // 负载折算惯量（关节轴系，kg·m²——组装方值传递，P-DT-4 保守消费）：
    // 轴 0 提供 0.5；轴 1 缺失（惯量比不适用——显式标记）。
    std::vector<dt::LoadInertiaEntry> load;
    dt::LoadInertiaEntry e0;
    e0.loadInertiaJointSide = 0.5;
    e0.source = dt::SourcedValueTag::UserProvided;
    load.push_back(e0);

    dt::DriveTrainMappingCore coreImpl;
    const dt::ReflectedInertiaResult inertia = coreImpl.evaluate(model, load);
    ASSERT_EQ(inertia.axes.size(), 2U);

    // 轴 0：J_reflected＝1e-4/0.01²＝1.0 kg·m²（DYN-04 记法 J·i²，i=100）；
    // 惯量比＝0.01²×0.5/1e-4＝0.5（数值事实——无阈值判定，P-DT-2）。
    EXPECT_PRED_FORMAT2(closeRel, inertia.axes[0].jReflectedJointSide, 1.0);
    ASSERT_TRUE(inertia.axes[0].inertiaRatio.has_value());
    EXPECT_PRED_FORMAT2(closeRel, *inertia.axes[0].inertiaRatio, 0.5);

    // 轴 1：J_reflected＝4e-4/0.02²＝1.0；负载缺失→惯量比不适用（§10.7）。
    EXPECT_PRED_FORMAT2(closeRel, inertia.axes[1].jReflectedJointSide, 1.0);
    EXPECT_FALSE(inertia.axes[1].inertiaRatio.has_value());

    // 反射惯量照常进入工作点（效率无关面）；惯量比在主管线（无负载输入）
    // 保持不适用。
    std::vector<double> times{0.0, 0.1};
    dt::JointSeriesView series = sineSeries(2, 0.1, 6.283185307179586, times, 2.0, "s");
    const dt::DriveTrainMappingOutput out = coreImpl.evaluate(model, series, nullptr);
    ASSERT_EQ(out.points.size(), 2U);
    EXPECT_PRED_FORMAT2(closeRel, out.points[0].reflectedInertia, 1.0);
    EXPECT_FALSE(out.points[0].inertiaRatio.has_value());
}

// =====================================================================
// DT-G9：峰值窗与 RMS 多段循环（DYN-03 口径——峰值带时刻/段/工况）
// =====================================================================

TEST(DtMappingGolden, PeakAndRmsMultiSegment_WP18T03_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-04", "DYN-03"},
                  std::vector<std::string>{});

    dt::DriveTrainModel model = dt::makeDiagonalDriveTrainModel(
        {jointAxis(0)}, {motorAxis(0)},
        {{1.0, dt::SourcedValueTag::ModelingField}}, {0.0}, testIdentity(),
        {eta(0.9, 0.7)}, {rotor(1e-4)},
        {std::optional<double>(10.0)} // 额定/参考力矩 10 N·m（负载率分母）
    );

    // 三段循环：τ=[2,8,-6,-6]、q̇=[1,1,1,1]、t=[0,1,2,3]（q̈=0）。
    dt::JointSeriesView series;
    series.jointIds = {idFrom<core::ObjectId>("dt-joint-0")};
    series.caseId = idFrom<core::ObjectId>("dt-case-g9");
    const double tau[4] = {2.0, 8.0, -6.0, -6.0};
    const char* seg[4] = {"seg-1", "seg-2", "seg-3", "seg-3"};
    for (std::size_t i = 0; i < 4; ++i) {
        dt::JointDriveSample s;
        s.t = static_cast<double>(i);
        s.qd = 1.0;
        s.tauJoint = tau[i];
        s.segmentId = seg[i];
        series.samples.push_back(s);
    }

    dt::DriveTrainMappingCore coreImpl;
    const dt::DriveTrainMappingOutput out = coreImpl.evaluate(model, series, nullptr);
    ASSERT_EQ(out.points.size(), 1U);
    const auto& p = out.points[0];

    // 正/负峰值分列（不混取绝对值——§10.4），带时刻/段/工况（"峰值不带
    // 来源即非法输出"——§11.2）。
    ASSERT_TRUE(p.tauPeakPos.present);
    EXPECT_PRED_FORMAT2(closeRel, p.tauPeakPos.value, 8.0);
    EXPECT_PRED_FORMAT2(closeRel, p.tauPeakPos.t, 1.0);
    EXPECT_EQ(p.tauPeakPos.segmentId, "seg-2");
    EXPECT_EQ(p.tauPeakPos.caseId, series.caseId);
    ASSERT_TRUE(p.tauPeakNeg.present);
    EXPECT_PRED_FORMAT2(closeRel, p.tauPeakNeg.value, -6.0);
    EXPECT_EQ(p.tauPeakNeg.segmentId, "seg-3");

    // 速度峰值（带符号实测值）与功率峰值（pMotor＝pJoint/η⁺——q̇=1、
    // q̈=0：峰值＝8/0.9，样本 1）。
    ASSERT_TRUE(p.omegaPeak.present);
    EXPECT_PRED_FORMAT2(closeRel, p.omegaPeak.value, 1.0);
    ASSERT_TRUE(p.powerPeak.present);
    EXPECT_PRED_FORMAT2(closeRel, p.powerPeak.value, 8.0 / 0.9);

    // RMS（完整循环、梯形）：τ²=[4,64,36,36]⟹∫＝34＋50＋36＝120⟹RMS=√40。
    EXPECT_PRED_FORMAT2(closeRel, p.tauRms, std::sqrt(120.0 / 3.0));
    // 负载率（参考值——§10.4）：RMS/额定。
    ASSERT_TRUE(p.loadRatio.has_value());
    EXPECT_PRED_FORMAT2(closeRel, *p.loadRatio, std::sqrt(120.0 / 3.0) / 10.0);
}

// =====================================================================
// 四象限全覆盖（§10.6——Q1～Q4＋零速的符号组合）
// =====================================================================

TEST(DtMappingGolden, QuadrantCoverage_WP18T03_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-04"},
                  std::vector<std::string>{});

    dt::DriveTrainModel model = dt::makeDiagonalDriveTrainModel(
        {jointAxis(0)}, {motorAxis(0)},
        {{1.0, dt::SourcedValueTag::ModelingField}}, {0.0}, testIdentity(),
        {eta(0.9, 0.7)}, {rotor(1e-4)});

    // 符号设计（ω＝q̇、P_quad＝τ_m·θ̇＝τ·q̇；q̈=0——转子项零；象限判定
    // 按 §10.6 表的符号条件——ω、P 符号→编号）：
    //   t0: ω=+1,τ=+2 → P=+2 → Q1   t1: ω=+1,τ=−2 → P=−2 → Q2
    //   t2: ω=−1,τ=−2 → P=+2 → Q4（ω<0 且 P>0——表行 4）
    //   t3: ω=−1,τ=+2 → P=−2 → Q3（ω<0 且 P<0——表行 3）
    //   t4: ω=0, τ=+1 → 零速（span 1）  t5: ω=+1,τ=+1 → Q1（span 0）
    dt::JointSeriesView series;
    series.jointIds = {idFrom<core::ObjectId>("dt-joint-0")};
    series.caseId = idFrom<core::ObjectId>("dt-case-quad");
    const double omega[6] = {1.0, 1.0, -1.0, -1.0, 0.0, 1.0};
    const double tau[6] = {2.0, -2.0, -2.0, 2.0, 1.0, 1.0};
    for (std::size_t i = 0; i < 6; ++i) {
        dt::JointDriveSample s;
        s.t = static_cast<double>(i);
        s.qd = omega[i];
        s.tauJoint = tau[i];
        s.segmentId = "seg-q";
        series.samples.push_back(s);
    }

    dt::DriveTrainMappingCore coreImpl;
    const dt::DriveTrainMappingOutput out = coreImpl.evaluate(model, series, nullptr);
    ASSERT_EQ(out.points.size(), 1U);
    const auto& p = out.points[0];

    EXPECT_EQ(p.q1.sampleCount, 2U); // t0、t5
    EXPECT_EQ(p.q2.sampleCount, 1U);
    EXPECT_EQ(p.q3.sampleCount, 1U);
    EXPECT_EQ(p.q4.sampleCount, 1U);
    EXPECT_EQ(p.zeroDwell.sampleCount, 1U);
    EXPECT_PRED_FORMAT2(closeRel, p.q1.timeShare, 1.0 / 5.0);
    EXPECT_PRED_FORMAT2(closeRel, p.q2.timeShare, 1.0 / 5.0);
    EXPECT_PRED_FORMAT2(closeRel, p.q3.timeShare, 1.0 / 5.0);
    EXPECT_PRED_FORMAT2(closeRel, p.q4.timeShare, 1.0 / 5.0);
    EXPECT_PRED_FORMAT2(closeRel, p.zeroDwell.timeShare, 1.0 / 5.0);
    // 象限能量＝∫pJoint dt（左矩形）：q1＝2×1＋1×0＝2；q2＝−2×1＝−2；
    // q3（t3：pJoint＝τ·ω＝2×(−1)＝−2）；q4（t2：pJoint＝−2×(−1)＝+2）。
    EXPECT_PRED_FORMAT2(closeRel, p.q1.energy, 2.0);
    EXPECT_PRED_FORMAT2(closeRel, p.q2.energy, -2.0);
    EXPECT_PRED_FORMAT2(closeRel, p.q3.energy, -2.0);
    EXPECT_PRED_FORMAT2(closeRel, p.q4.energy, 2.0);
    EXPECT_EQ(p.zeroDwell.energy, 0.0);
}

// =====================================================================
// 数据不足降级（§10.7——效率缺失/转子缺失/估算来源/时间非单调）
// =====================================================================

TEST(DtMappingGolden, DataInsufficientDegradation_WP18T03_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"EVI-02"},
                  std::vector<std::string>{"AT-07"});

    // ---- 效率缺失：力矩/速度照常，功率/能量/四象限降级（不伪造、不以
    // η=1 替代——§10.1/§10.7）。
    dt::DriveTrainModel noEff = dt::makeDiagonalDriveTrainModel(
        {jointAxis(0)}, {motorAxis(0)},
        {{1.0, dt::SourcedValueTag::ModelingField}}, {0.0}, testIdentity(),
        {}, {rotor(1e-4)});
    std::vector<double> times{0.0, 1.0, 2.0};
    dt::JointSeriesView series = sineSeries(1, 0.1, 6.283185307179586, times, 2.0, "s");

    dt::DriveTrainMappingCore coreImpl;
    const dt::DriveTrainMappingOutput out1 = coreImpl.evaluate(noEff, series, nullptr);
    EXPECT_EQ(out1.completeness, dt::CompletenessState::Partial);
    ASSERT_GE(out1.missingItems.size(), 1U);
    EXPECT_EQ(out1.missingItems[0], "efficiency[j=0]");
    ASSERT_EQ(out1.motorSeries.size(), 1U);
    // 力矩照常（理想＋转子项——与含效率模型一致）。
    EXPECT_EQ(out1.motorSeries[0].samples[0].tauMotor, 1.0 * series.samples[0].tauJoint);
    // 功率降级：pTransmission/pMotor 恒 0、能量 0、四象限不产出。
    EXPECT_EQ(out1.motorSeries[0].samples[0].pTransmission, 0.0);
    EXPECT_EQ(out1.motorSeries[0].samples[0].pMotor, 0.0);
    ASSERT_EQ(out1.points.size(), 1U);
    EXPECT_EQ(out1.points[0].energy.eJoint, 0.0);
    EXPECT_EQ(out1.points[0].q1.sampleCount, 0U);
    EXPECT_EQ(out1.points[0].quality, dt::CompletenessState::Partial);
    EXPECT_FALSE(out1.points[0].etaApplied.has_value());

    // ---- 转子缺失：τ_m＝理想口径＋DT-ROTOR-MISSING 诊断＋缺失清单。
    dt::DriveTrainModel noRotor = dt::makeDiagonalDriveTrainModel(
        {jointAxis(0)}, {motorAxis(0)},
        {{1.0, dt::SourcedValueTag::ModelingField}}, {0.0}, testIdentity(),
        {eta(0.9, 0.7)}, {});
    const dt::DriveTrainMappingOutput out2 = coreImpl.evaluate(noRotor, series, nullptr);
    EXPECT_EQ(out2.completeness, dt::CompletenessState::Partial);
    bool rotorMissingItem = false;
    bool rotorMissingDiag = false;
    for (const auto& item : out2.missingItems) {
        if (item == "rotor[j=0]") {
            rotorMissingItem = true;
        }
    }
    for (const auto& rec : out2.diagnostics) {
        if (rec.code == dt::kDtRotorMissing) {
            rotorMissingDiag = true;
        }
    }
    EXPECT_TRUE(rotorMissingItem);
    EXPECT_TRUE(rotorMissingDiag);
    ASSERT_EQ(out2.motorSeries.size(), 1U);
    EXPECT_EQ(out2.motorSeries[0].samples[0].tauMotor,
              out2.motorSeries[0].samples[0].tauIdeal); // 理想口径输出

    // ---- 估算来源（Estimated）贯穿：缺失清单含限定语素材（§10.7）。
    dt::DriveTrainModel estEff = dt::makeDiagonalDriveTrainModel(
        {jointAxis(0)}, {motorAxis(0)},
        {{1.0, dt::SourcedValueTag::ModelingField}}, {0.0}, testIdentity(),
        {eta(0.9, 0.7, dt::SourcedValueTag::Estimated)}, {rotor(1e-4)});
    const dt::DriveTrainMappingOutput out3 = coreImpl.evaluate(estEff, series, nullptr);
    bool estimatedSeen = false;
    for (const auto& item : out3.missingItems) {
        if (item == "estimated-source") {
            estimatedSeen = true;
        }
    }
    EXPECT_TRUE(estimatedSeen);
    EXPECT_TRUE(out3.points[0].estimatedSource);

    // ---- 时间非单调（DT-G12——数据类：不抛、不排序吞错；统计降级）。
    dt::JointSeriesView bad
        = sineSeries(1, 0.1, 6.283185307179586, {0.0, 2.0, 1.0}, 2.0, "s");
    const dt::DriveTrainMappingOutput out4 = coreImpl.evaluate(noEff, bad, nullptr);
    bool nonmonotonicDiag = false;
    for (const auto& rec : out4.diagnostics) {
        if (rec.code == dt::kDtInputTimeNonmonotonic) {
            nonmonotonicDiag = true;
        }
    }
    EXPECT_TRUE(nonmonotonicDiag);
    ASSERT_EQ(out4.points.size(), 1U);
    // 不输出"部分 RMS 冒充完整循环 RMS"——降级为 0＋缺失清单。
    EXPECT_EQ(out4.points[0].tauRms, 0.0);
}

// =====================================================================
// 取消（批次边界——不发布完整结果；TASK-02）
// =====================================================================

namespace {
/// 取消语义替身（恒真——批次边界即取消）。
class CancellingGate final : public dt::ICancellation {
public:
    bool cancellationRequested() const override
    {
        return true;
    }
};
}  // namespace

TEST(DtMappingGolden, CancellationAtBatchBoundary_WP18T03_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"TASK-02", "NFR-PERF-02"},
                  std::vector<std::string>{"AT-34"});

    dt::DriveTrainModel model = dt::makeDiagonalDriveTrainModel(
        {jointAxis(0)}, {motorAxis(0)},
        {{1.0, dt::SourcedValueTag::ModelingField}}, {0.0}, testIdentity(),
        {eta(0.9, 0.7)}, {rotor(1e-4)});
    std::vector<double> times{0.0, 1.0};
    dt::JointSeriesView series = sineSeries(1, 0.1, 6.283185307179586, times, 2.0, "s");

    dt::DriveTrainMappingCore coreImpl;
    CancellingGate gate;
    const dt::DriveTrainMappingOutput out = coreImpl.evaluate(model, series, &gate);
    // 取消后不发布完整结果：空素材＋取消诊断（消费方按 TASK-02 处置）。
    EXPECT_TRUE(out.points.empty());
    EXPECT_TRUE(out.motorSeries.empty());
    EXPECT_EQ(out.completeness, dt::CompletenessState::Partial);
    ASSERT_FALSE(out.missingItems.empty());
    EXPECT_EQ(out.missingItems.front(), "cancelled");
    ASSERT_FALSE(out.diagnostics.empty());
    EXPECT_EQ(out.diagnostics.front().code, dt::kDtEvaluationCancelled);
}

// =====================================================================
// canonical 编解码往返（NFR-COR-02——组装方契约的确定性面）
// =====================================================================

TEST(DtMappingGolden, CodecRoundtrip_WP18T03_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-02", "CON-04"},
                  std::vector<std::string>{});

    dt::DriveTrainModel model = dt::makeDiagonalDriveTrainModel(
        {jointAxis(0), jointAxis(1)}, {motorAxis(0), motorAxis(1)},
        {{0.01, dt::SourcedValueTag::ModelingField},
         {-0.02, dt::SourcedValueTag::UserProvided}},
        {0.25, -0.5}, testIdentity(), {eta(0.9, 0.7), eta(0.85, 0.6)},
        {rotor(1e-4), rotor(2e-4)},
        {std::optional<double>(10.0), std::nullopt});

    // 模型往返等值（同值模型必得同字节——编码确定性锚点）。
    const auto modelBytes = dt::encodeDriveTrainModel(model);
    const dt::DriveTrainModel decoded = dt::decodeDriveTrainModel(modelBytes);
    EXPECT_EQ(decoded, model);
    EXPECT_EQ(dt::encodeDriveTrainModel(decoded), modelBytes);

    // 序列往返等值。
    dt::JointSeriesView series = sineSeries(2, 0.1, 6.283185307179586,
                                            {0.0, 0.5, 1.0}, 2.0, "seg-rt");
    const auto seriesBytes = dt::encodeJointSeries(series);
    EXPECT_EQ(dt::decodeJointSeries(seriesBytes), series);

    // 输出往返等值（端到端产物；模型完整——无降级诊断）。payload 编码边界
    // （Codec.hpp 文件头）：诊断记录不进 payload（经 EvaluationOutput.
    // diagnostics 通道）——往返等值断言以诊断面为空的输出为准，并钉住
    // 解码侧 diagnostics 恒空的行为。
    dt::DriveTrainMappingCore coreImpl;
    const dt::DriveTrainMappingOutput out = coreImpl.evaluate(model, series, nullptr);
    ASSERT_TRUE(out.diagnostics.empty());
    const auto outBytes = dt::encodeMappingOutput(out);
    const dt::DriveTrainMappingOutput outDecoded = dt::decodeMappingOutput(outBytes);
    EXPECT_TRUE(outDecoded.diagnostics.empty());
    EXPECT_EQ(outDecoded, out);

    // 拒绝面：magic 不符／截断字节——调用方契约违约 fail-fast。
    auto corrupted = modelBytes;
    corrupted[0] = 'X';
    EXPECT_THROW((void)dt::decodeDriveTrainModel(corrupted), std::invalid_argument);
    auto truncated = modelBytes;
    truncated.pop_back();
    EXPECT_THROW((void)dt::decodeDriveTrainModel(truncated), std::invalid_argument);
}

// =====================================================================
// Facts DTO 提取（§13.6——只含事实；D-DT-10 命名边界）
// =====================================================================

TEST(DtMappingGolden, FactsExtraction_WP18T03_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"EVI-01", "NFR-COR-04"},
                  std::vector<std::string>{});

    dt::DriveTrainModel model = dt::makeDiagonalDriveTrainModel(
        {jointAxis(0)}, {motorAxis(0)},
        {{1.0, dt::SourcedValueTag::ModelingField}}, {0.0}, testIdentity(),
        {eta(0.9, 0.7)}, {rotor(1e-4)});
    std::vector<double> times{0.0, 1.0, 2.0};
    dt::JointSeriesView series = sineSeries(1, 0.1, 6.283185307179586, times, 2.0, "s");

    dt::DriveTrainMappingCore coreImpl;
    const dt::DriveTrainMappingOutput out = coreImpl.evaluate(model, series, nullptr);

    dt::DriveTrainFactsProvider provider;
    const dt::DriveTrainEvidenceFacts f = provider.facts(out);

    // 身份块逐字段一致（NFR-COR-04 可追溯）。
    EXPECT_EQ(f.identity, model.identity);
    EXPECT_EQ(f.upstreamSliceId, series.upstreamSliceId);
    EXPECT_EQ(f.caseCovered, series.caseId);
    EXPECT_EQ(f.completeness, out.completeness);
    EXPECT_TRUE(f.missingItems.empty());
    // 逐轴事实摘要与工作点一致（数值事实引用面）。
    ASSERT_EQ(f.axes.size(), 1U);
    EXPECT_EQ(f.axes[0].axisId, out.points[0].axisId);
    EXPECT_PRED_FORMAT2(closeRel, f.axes[0].tauRms, out.points[0].tauRms);
    EXPECT_TRUE(f.axes[0].efficiencyApplied);
    EXPECT_EQ(f.diagnosticCount, out.diagnostics.size());
}
