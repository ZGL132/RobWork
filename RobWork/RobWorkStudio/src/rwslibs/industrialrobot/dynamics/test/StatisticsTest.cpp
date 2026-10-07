/**
 * @file   StatisticsTest.cpp
 * @brief  WP-17-T04 用例组——输出序列与峰值/RMS/功率能量统计面（DynSeries/
 *         DynEnvelope/DynPowerEnergy/DynStatsIntegration 四组）：峰值持续
 *         时间窗与所在轨迹段黄金算例、时间加权 RMS 含驻留解析对照（禁样
 *         本平均对照）、E⁺/E⁻ 分项与制动段符号、序列身份冻结与内容身份
 *         确定性、类型化不混算（转动 N·m/移动 N）、Empty 与无效显式 NaN
 *         不伪造、fail-fast 边界、RNEA→序列→统计端到端（任务契约
 *         WP-17-T04 acceptance 逐条的执行证明面）。
 *
 * 设计依据：
 *   - units/dynamics.md §4.4/§4.6（序列数据模型与纪律）、§7.2（峰值三要
 *     素＋持续时间窗＝峰值样本连续等值 run——D-DYN-8 无阈值化）、§7.3
 *     （RMS 时间加权含驻留——D-DYN-9 禁样本平均）、§7.5（功率/能量符号
 *     约定）、§10.3/§10.4/§10.5（三计算器契约表）、§4.5（量纲——转动
 *     N·m/移动 N 类型化）
 *   - 需求 DYN-03（输出每轴转角/位移、转速、加速度、类型化广义力、机械
 *     功率、峰值和 RMS 包络；峰值必须报告持续时间窗和所在轨迹段，RMS
 *     基于完整任务循环〔含驻留〕）
 *   - 决策 D-DYN-8/D-DYN-9（窗口无阈值化/时间加权）、D-DYN-4（类型化广
 *     义力存储 SI double＋jointType 标签——统计逐关节不混算）
 *
 * 数值对照口径（附录 D C7"测试对照容差与产品容差分离"）：黄金期望值在
 *   本文件逐用例**手工按公式算出**（梯形黄金＝对采样网格逐相邻样本对
 *   展开——与卡 §7.3"公式固定、黄金算例对照解析积分"同口径），断言容差
 *   为**相对 1e-12**（双精度同序求和的表示误差远低于此）；无任何容差进
 *   入产品代码（产品侧零自设阈值——P-DYN-9 能量量纲运行容差缺位不得预
 *   填，本文件是唯一对照面）。
 */

#include <sdurws/ird/dynamics/DynTypes.hpp>
#include <sdurws/ird/dynamics/Envelope.hpp>
#include <sdurws/ird/dynamics/Errors.hpp>
#include <sdurws/ird/dynamics/InverseDynamics.hpp>
#include <sdurws/ird/dynamics/PowerEnergy.hpp>
#include <sdurws/ird/dynamics/SeriesBuilder.hpp>
#include <sdurws/ird/evidence/Evaluator.hpp>          // IEvaluationContext（端到端用例宿主面）
#include <sdurws/ird/runtime/CanonicalModel.hpp>      // 混合链直构（测试夹具——含 CanonicalModelBuilder）
#include <sdurws/ird/runtime/Description.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "TwoLinkFixture.hpp"

using namespace sdurws::ird::dynamics::testfixture;

using sdurws::ird::core::ContentIdentity;
using sdurws::ird::core::ObjectId;
using sdurws::ird::dynamics::DynamicsEnvelopeCalculator;
using sdurws::ird::dynamics::DynamicsSample;
using sdurws::ird::dynamics::DynamicsSeries;
using sdurws::ird::dynamics::DynamicsSeriesBuilder;
using sdurws::ird::dynamics::DynamicsValidity;
using sdurws::ird::dynamics::DynJointType;
using sdurws::ird::dynamics::DynamicsError;
using sdurws::ird::dynamics::InverseDynOutcome;
using sdurws::ird::dynamics::InverseDynRequest;
using sdurws::ird::dynamics::InverseDynSampleInput;
using sdurws::ird::dynamics::InverseDynamicsEvaluator;
using sdurws::ird::dynamics::OperatingConditionResult;
using sdurws::ird::dynamics::PeakRecord;
using sdurws::ird::dynamics::PowerEnergyCalculator;
using sdurws::ird::dynamics::PowerEnergySummary;
using sdurws::ird::dynamics::SampleNumericState;
using sdurws::ird::dynamics::SeriesIdentity;

namespace {

// =====================================================================
// 本地替身与助手（同 RneaTest.cpp 形态——替身只承载对话形状，不伪造
 //  任何评估逻辑；全部断言针对真实产品代码）。
// =====================================================================

/// 宿主上下文本地替身（evidence 卡口径——端到端用例的取消/进度宿主面）。
class FakeContext final : public sdurws::ird::evidence::IEvaluationContext {
public:
    bool cancellationRequested() const override { return mCancelled; }
    void reportProgress(std::uint8_t, std::string_view) override {}
    std::optional<std::vector<std::uint8_t>> tryObjectBytes(
        sdurws::ird::core::ObjectId, sdurws::ird::core::ContentVersion) const override
    {
        return std::nullopt;
    }
    bool mCancelled = false;  ///< 置 true＝模拟宿主取消请求
};

/// 黄金对照相对容差（本文件声明——测试对照口径，附录 D C7 分离；双精度
/// 同序求和表示误差远低于此）。
constexpr double kGoldenRelTol = 1e-12;

/// 世界系重力幅值（单位 m/s²——解析式的 g；WorldPlacement 默认 (0,0,−9.81)）。
constexpr double kG = 9.81;

/// 相对容差断言（期望为 0 时退化为绝对 1e-15——避免零除）。
void expectNearRel(double actual, double expected, const char* what)
{
    const double tol = std::max(1e-15, kGoldenRelTol * std::abs(expected));
    EXPECT_NEAR(actual, expected, tol) << what << "（实得 " << actual << "，期望 " << expected
                                       << "）";
}

/// 样本行构造助手（手工黄金序列用——分项字段按"统计器只消费 τ_total/
/// 功率/速度/加速度"的事实填充自洽值：五分项恒等式在此恒成立，与构建
/// 器/统计器"不校验恒等式"的分层一致，SeriesBuilder.hpp 文件头）。
DynamicsSample rowAt(double t, std::uint32_t segment, std::uint32_t joint,
                     DynJointType jointType, double q, double qd, double qdd,
                     double tauTotal, double power, double energyJ = 0.0)
{
    DynamicsSample r;
    r.t = t;                                  // s
    r.segmentIndex = segment;                 // 所在轨迹段（0 基）
    r.conditionId = idFrom<ObjectId>("golden-cond");   // 工况 id（黄金序列统一值）
    r.jointIndex = joint;
    r.jointObjectId = idFrom<ObjectId>("golden-joint");
    r.jointType = jointType;                  // 转动 N·m／移动 N（类型化标签）
    r.q = q;                                  // rad 或 m
    r.qd = qd;                                // rad/s 或 m/s
    r.qdd = qdd;                              // rad/s² 或 m/s²
    r.tauGravity = 0.0;                       // 黄金行不做分项拆解（见助手注释）
    r.tauInertia = 0.0;
    r.tauCoriolisCentrifugal = 0.0;
    r.tauFriction = 0.0;
    r.tauExternal = 0.0;                      // R1 恒 0（P-DYN-3）
    r.tauTotal = tauTotal;                    // N·m 或 N（按 jointType）
    r.mechanicalPower = power;                // W（=τ_total·q̇——构造方保证）
    r.energyIntegralJ = energyJ;              // J（梯形积分状态——构造方算）
    r.payloadVariantIndex = 0;                // 基线变体
    r.toolObjectId = ObjectId{};              // 无工具模型＝空 id
    r.numericState = SampleNumericState::Ok;
    return r;
}

/// 序列身份块构造助手（确定性 id/摘要——同夹具派生规则；planned 由
/// 调用方按用例场景给）。
SeriesIdentity makeIdentity(const std::string& tag, std::size_t planned)
{
    SeriesIdentity id;
    id.snapshotId.bytes = digestOf(tag + "-snap");
    id.sliceId.bytes = digestOf(tag + "-slice");
    id.trajectoryPayloadId.bytes = digestOf(tag + "-trj");
    id.conditionId = idFrom<ObjectId>(tag + "-cond");
    id.toolObjectId = ObjectId{};                 // 无工具模型（合法空 id）
    id.dynConfigDigest = "sha256-" + tag;         // config.dyn 摘要（非空即可——测试值）
    id.task.project = idFrom<sdurws::ird::core::ProjectId>(tag + "-prj");
    id.task.branch = idFrom<sdurws::ird::core::BranchId>(tag + "-brn");
    id.task.revision = idFrom<sdurws::ird::core::RevisionId>(tag + "-rev");
    id.task.run = idFrom<sdurws::ird::core::RunId>(tag + "-run");
    id.task.attempt.value = 1u;   // AttemptId＝u64 尝试序号（≥1 合法——非 Id128）
    id.plannedSampleCount = planned;
    return id;
}

// =====================================================================
// 混合链夹具（Prismatic 竖直滑升＋Revolute 水平回转——类型化不混算黄金
// 算例的模型面；自持直构同 TwoLinkFixture 形态，CanonicalModelBuilder
// 唯一入口）。
//
// 几何与解析（全 SI）：
//   - 关节 1 移动：轴沿基座系 +Z（竖直滑升）；连杆 1（滑臂）质量 m1、
//     质心在轴上（水平偏置 0）；
//   - 关节 2 转动：轴沿基座系 +Z（水平面回转，位于 z=h）；连杆 2 质量
//     m2、质心距轴 r（水平面内回转——高度恒不变）；绕 Z 转动惯量
//     I2zz＝0.002＋m2·r²（连杆自轴分量＋质心点质量分量，kg·m²）；
//   - 激励：关节 1 静止（q̇1=q̈1=0）、关节 2 恒角加速 q̈2=α（q̇2=αt、
//     q2=αt²/2）。
//   - 解析（竖直轴、水平面运动——惯性项全部无竖直分量）：
//     τ1（移动关节，N）＝竖直支撑力＝(m1+m2)·g（恒定——回转加速度
//       均在水平面、q̈1=0 无竖直惯性分量、高度不变重力项恒定）；
//     τ2（转动关节，N·m）＝I2zz·α（绕固定轴的角加速项恒定——回转对
//       称使 I2zz 与 q2 无关、q̇2 平行轴无科氏矩、轴与重力平行无重力
//       矩；注意匀速段离心力沿半径方向对轴心无力矩，故黄金激励用恒
//       角加速而非匀速）。
// =====================================================================

/// 混合链黄金常量（单位逐项注明）。
constexpr double kPrismMass = 1.0;    ///< 滑臂质量，kg
constexpr double kRevMass = 0.5;      ///< 回转臂质量，kg
constexpr double kRevComR = 0.2;      ///< 回转臂质心距回转轴，m
constexpr double kRevIzzSelf = 0.002; ///< 回转臂绕自身竖直轴惯量分量，kg·m²（夹具注入值）
constexpr double kAlpha = 4.0;        ///< 回转角加速度，rad/s²（恒定激励）
constexpr double kMixG = 9.81;        ///< 重力幅值，m/s²（WorldPlacement 默认）

/// 解析期望：移动关节轴力（N——支撑总重）。
double prismAxialForce() { return (kPrismMass + kRevMass) * kMixG; }
/// 解析期望：转动关节力矩（N·m）＝绕 Z 总转动惯量×角加速度。
double revTorque()
{
    return (kRevIzzSelf + kRevMass * kRevComR * kRevComR) * kAlpha;
}

/// 组装混合链 CanonicalModel（Prismatic+Revolute——测试内直构，构建器
/// 唯一入口；字段合法性由 build() 强制）。
inline CanonicalModel makePrismRevModel()
{
    CanonicalModelHeader header;
    header.project = idFrom<sdurws::ird::core::ProjectId>("mix-prj");
    header.branch = idFrom<sdurws::ird::core::BranchId>("mix-brn");
    header.revision = idFrom<sdurws::ird::core::RevisionId>("mix-rev-1");
    header.revisionSeq = 1;
    header.descriptionContractVersion = 1;
    header.compilerContractVersion = 1;
    header.builtFrom = digestOf("mix-description-bytes");

    const ObjectId robot = idFrom<ObjectId>("mix-robot");
    const ObjectId j1 = idFrom<ObjectId>("mix-j1");
    const ObjectId j2 = idFrom<ObjectId>("mix-j2");
    const ObjectId l0 = idFrom<ObjectId>("mix-l0");
    const ObjectId l1 = idFrom<ObjectId>("mix-l1");
    const ObjectId l2 = idFrom<ObjectId>("mix-l2");
    auto addRef = [&header](const ObjectId& id, const char* seed, const char* token) {
        sdurws::ird::runtime::ObjectRefEntry e;
        e.objectId = id;
        e.contentVersion = cvFrom(seed);
        e.objectTypeToken = token;
        e.digest = digestOf(std::string{seed} + "-bytes");
        header.objectRefs.push_back(e);
    };
    addRef(robot, "mix-robot", "robot-design");
    addRef(j1, "mix-j1", "joint");
    addRef(j2, "mix-j2", "joint");
    addRef(l0, "mix-l0", "link");
    addRef(l1, "mix-l1", "link");
    addRef(l2, "mix-l2", "link");

    sdurws::ird::runtime::RobotChain chain;
    chain.robotObjectId = robot;
    chain.robotLocalName = "DYN_MIX";
    chain.deviceName = "DYN_MIX";

    // 关节 1：竖直移动（axis=+Z；bounds 单位 m、maxVelocity 单位 m/s）。
    sdurws::ird::runtime::CanonicalJoint joint1;
    joint1.objectId = j1;
    joint1.localName = "lift_joint";
    joint1.type = sdurws::ird::runtime::JointType::Prismatic;
    joint1.axis = rw::math::Vector3D<double>(0.0, 0.0, 1.0);  // 基座系 +Z（单位向量）
    joint1.bounds = sdurws::ird::runtime::JointBounds{0.0, 1.0};  // m
    joint1.maxVelocity = val(1.0);                            // m/s
    chain.joints.push_back(joint1);

    // 关节 2：水平回转（axis=+Z；origin＝滑臂端 (0,0,0.5)、零旋转）。
    sdurws::ird::runtime::CanonicalJoint joint2;
    joint2.objectId = j2;
    joint2.localName = "turn_joint";
    joint2.type = sdurws::ird::runtime::JointType::Revolute;
    joint2.axis = rw::math::Vector3D<double>(0.0, 0.0, 1.0);  // 基座系 +Z（单位向量）
    joint2.bounds = sdurws::ird::runtime::JointBounds{-3.14159265358979323846,
                                                      3.14159265358979323846};  // rad
    joint2.maxVelocity = val(3.0);                            // rad/s
    joint2.origin = rw::math::Transform3D<double>(
        rw::math::Vector3D<double>(0.0, 0.0, 0.5), identityRotation());
    chain.joints.push_back(joint2);

    // 连杆（数量＝关节数＋1；links[i] 为关节 i 的父体——链约定）。
    sdurws::ird::runtime::CanonicalLink baseLink;
    baseLink.objectId = l0;
    baseLink.localName = "mix_base";
    chain.links.push_back(baseLink);

    sdurws::ird::runtime::CanonicalLink link1;
    link1.objectId = l1;
    link1.localName = "lift_link";
    link1.mass = val(kPrismMass);                             // kg
    link1.centerOfMass = sdurws::ird::core::SourcedValue<rw::math::Vector3D<double>>::
        provided(rw::math::Vector3D<double>(0.0, 0.0, 0.1), userProv());  // 轴上，m
    link1.inertia = sdurws::ird::core::SourcedValue<rw::math::InertiaMatrix<double>>::
        provided(diagInertia(0.01, 0.01, 0.001), userProv());             // 质心系，kg·m²
    chain.links.push_back(link1);

    sdurws::ird::runtime::CanonicalLink link2;
    link2.objectId = l2;
    link2.localName = "turn_link";
    link2.mass = val(kRevMass);                               // kg
    link2.centerOfMass = sdurws::ird::core::SourcedValue<rw::math::Vector3D<double>>::
        provided(rw::math::Vector3D<double>(kRevComR, 0.0, 0.0), userProv());  // m
    link2.inertia = sdurws::ird::core::SourcedValue<rw::math::InertiaMatrix<double>>::
        provided(diagInertia(0.001, 0.001, kRevIzzSelf), userProv());         // kg·m²
    chain.links.push_back(link2);

    return CanonicalModelBuilder()
        .setHeader(header)
        .setWorld(groundWorld())
        .setChain(chain)
        .setTools({})
        .build();
}

}  // namespace

// =====================================================================
// DynSeries 组——序列构建器（§10.3：排序/计数/身份冻结/内容身份）。
// =====================================================================

/**
 * 用例 1：身份冻结＋行序稳定排序＋完整性计数（acceptance 1 序列输出面）。
 * 乱序喂入同刻度多关节行（构建器契约内形态）——finalize 后按
 * (t, jointIndex) 升序；Complete 判定（actual==planned 且无非 Ok 行）。
 */
TEST(DynSeries, IdentityFreezeOrderingAndCompleteCounting)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-03"}, std::vector<std::string>{});
    // 本用例验证：构建器按 §4.6 纪律冻结序列——行序 (t,jointIndex) 稳定
    // 排序、身份块逐字段拷入、planned/actual 计数与 Complete 判定。
    DynamicsSeriesBuilder b;
    // 乱序仅限**同刻度内**（构建器拒收跨时刻乱序——时间倒退 fail-fast，
    // 见用例 3）：t=0.1 的关节 1 行先于关节 0 行喂入，finalize 排序修复
    // 为 (t, jointIndex) 序。
    b.addSample(rowAt(0.0, 0, 0, DynJointType::Revolute, 0.0, 0.0, 0.0, 1.0, 0.0));
    b.addSample(rowAt(0.1, 0, 1, DynJointType::Revolute, 0.1, 0.2, 0.3, 4.0, 0.8));
    b.addSample(rowAt(0.1, 0, 0, DynJointType::Revolute, 0.0, 0.1, 0.2, 2.0, 0.2));
    ASSERT_EQ(b.sampleCount(), 3u);

    SeriesIdentity id = makeIdentity("order", 2);   // 计划 2 个样本时刻
    const DynamicsSeries series = b.finalize(id);

    // 行序：(0.0,j0) → (0.1,j0) → (0.1,j1)（NFR-COR-02 稳定序）。
    ASSERT_EQ(series.samples.size(), 3u);
    EXPECT_DOUBLE_EQ(series.samples[0].t, 0.0);
    EXPECT_EQ(series.samples[0].jointIndex, 0u);
    EXPECT_DOUBLE_EQ(series.samples[1].t, 0.1);
    EXPECT_EQ(series.samples[1].jointIndex, 0u);
    EXPECT_DOUBLE_EQ(series.samples[2].t, 0.1);
    EXPECT_EQ(series.samples[2].jointIndex, 1u);

    // 身份块逐字段冻结（§4.4——identity → series 拷入）。
    EXPECT_TRUE(series.snapshotId == id.snapshotId);
    EXPECT_TRUE(series.sliceId == id.sliceId);
    EXPECT_TRUE(series.trajectoryPayloadId == id.trajectoryPayloadId);
    EXPECT_TRUE(series.conditionId == id.conditionId);
    EXPECT_EQ(series.evaluatorContractVersion, id.evaluatorContractVersion);
    EXPECT_EQ(series.algorithmVersion, id.algorithmVersion);
    EXPECT_EQ(series.dynConfigDigest, id.dynConfigDigest);
    EXPECT_TRUE(series.task == id.task);

    // 完整性：actual=2 刻度==planned=2 且无非 Ok 行→Complete。
    EXPECT_EQ(series.validity.completeness, DynamicsValidity::Completeness::Complete);
    EXPECT_EQ(series.validity.plannedSampleCount, 2u);
    EXPECT_EQ(series.validity.actualSampleCount, 2u);
    EXPECT_EQ(series.validity.nonFiniteCount, 0u);
    // 内容身份非全零（CON-05——序列可寻址）。
    EXPECT_TRUE(series.contentIdentity.isValid());
}

/**
 * 用例 2：内容身份确定性（CON-05/NFR-COR-02）——同输入两次 finalize 同
 * 摘要；任一样本行任一字段变化→摘要变化（内容寻址失效判据）。
 */
TEST(DynSeries, ContentIdentityDeterministicAndSensitive)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-03"}, std::vector<std::string>{});
    // 本用例验证：canonical 编码的字节级确定性（同输入字节→同摘要）与
    // 灵敏度（一行一个力矩位的差异即换摘要——缓存/失效判据的前提）。
    auto buildOnce = [](double perturb) {
        DynamicsSeriesBuilder b;
        b.addSample(rowAt(0.0, 0, 0, DynJointType::Revolute, 0.0, 0.5, 0.0, 2.0 + perturb,
                          1.0 + 0.5 * perturb));
        b.addSample(rowAt(0.5, 0, 0, DynJointType::Revolute, 0.1, 0.5, 0.0, 2.0 + perturb,
                          1.0 + 0.5 * perturb));
        return b.finalize(makeIdentity("det", 2));
    };
    const DynamicsSeries a = buildOnce(0.0);
    const DynamicsSeries b = buildOnce(0.0);
    EXPECT_TRUE(a.contentIdentity == b.contentIdentity) << "同输入两次构建必须同摘要";

    const DynamicsSeries c = buildOnce(1e-9);  // 力矩微扰——摘要必须变
    EXPECT_FALSE(a.contentIdentity == c.contentIdentity) << "样本值变化必须改变内容身份";
    EXPECT_TRUE(a.contentIdentity.isValid());
}

/**
 * 用例 3：时间倒退 fail-fast（§10.3 前置行——DYN-SERIES-NON-MONOTONIC
 * 语义，不排序修复；调用方错误轨）。
 */
TEST(DynSeries, TimeBackwardsFailsFast)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-03"}, std::vector<std::string>{});
    // 本用例验证：addSample 时间倒退（t < 前行 t）立即抛 DynamicsError
    // （token series-non-monotonic——§4.6"不排序修复、不插值抹平"）。
    DynamicsSeriesBuilder b;
    b.addSample(rowAt(0.5, 0, 0, DynJointType::Revolute, 0.0, 0.0, 0.0, 1.0, 0.0));
    try {
        b.addSample(rowAt(0.4, 0, 0, DynJointType::Revolute, 0.0, 0.0, 0.0, 1.0, 0.0));
        FAIL() << "时间倒退必须 fail-fast";
    } catch (const DynamicsError& e) {
        EXPECT_EQ(e.token(), "series-non-monotonic");
        EXPECT_NE(std::string(e.what()).find("DYN-SERIES-NON-MONOTONIC"), std::string::npos)
            << "异常 message 应携带稳定码语义定位：" << e.what();
    }
}

/**
 * 用例 4：重复 (t, jointIndex) 行对——addSample 不报错、finalize 标记
 * （§10.3 非法示例字面口径：diagRefs 素材＋压 Partial，不静默放行）。
 */
TEST(DynSeries, DuplicateRowMarkedAtFinalize)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-03"}, std::vector<std::string>{});
    // 本用例验证：防御性二次校验的标记语义——同一 (t,jointIndex) 行加
    // 两次不抛（重复时间戳），但 finalize 后 diagRefs 含标记、完整性
    // 压为 Partial（正常评估器输出无此形态）。
    DynamicsSeriesBuilder b;
    b.addSample(rowAt(0.0, 0, 0, DynJointType::Revolute, 0.0, 0.0, 0.0, 1.0, 0.0));
    b.addSample(rowAt(0.0, 0, 0, DynJointType::Revolute, 0.0, 0.0, 0.0, 1.0, 0.0));  // 重复对
    const DynamicsSeries series = b.finalize(makeIdentity("dup", 1));
    EXPECT_EQ(series.validity.completeness, DynamicsValidity::Completeness::Partial);
    EXPECT_FALSE(series.diagRefs.empty()) << "重复行对必须在 diagRefs 留标记";
}

/**
 * 用例 5：身份块缺失 fail-fast（§10.0"缺身份拒绝"——七类逐项）。
 */
TEST(DynSeries, IdentityMissingFailsFast)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-03"}, std::vector<std::string>{});
    // 本用例验证：finalize 对身份块任一缺失分量拒绝冻结（snapshotId/
    // sliceId/trajectoryPayloadId/conditionId/task/algorithmVersion/
    // dynConfigDigest——调用方错误轨，不产出半成品序列）。
    DynamicsSeriesBuilder b;
    b.addSample(rowAt(0.0, 0, 0, DynJointType::Revolute, 0.0, 0.0, 0.0, 1.0, 0.0));

    auto expectRejected = [](SeriesIdentity id, const char* what) {
        DynamicsSeriesBuilder local;
        local.addSample(rowAt(0.0, 0, 0, DynJointType::Revolute, 0.0, 0.0, 0.0, 1.0, 0.0));
        EXPECT_THROW(local.finalize(id), DynamicsError) << what;
    };
    {
        SeriesIdentity id = makeIdentity("miss-snap", 1);
        id.snapshotId = ContentIdentity{};                      // 全零
        expectRejected(id, "snapshotId 全零必须拒绝");
    }
    {
        SeriesIdentity id = makeIdentity("miss-slice", 1);
        id.sliceId = ContentIdentity{};
        expectRejected(id, "sliceId 全零必须拒绝");
    }
    {
        SeriesIdentity id = makeIdentity("miss-trj", 1);
        id.trajectoryPayloadId = ContentIdentity{};
        expectRejected(id, "trajectoryPayloadId 全零必须拒绝");
    }
    {
        SeriesIdentity id = makeIdentity("miss-cond", 1);
        id.conditionId = ObjectId{};
        expectRejected(id, "工况空 id 必须拒绝");
    }
    {
        SeriesIdentity id = makeIdentity("miss-task", 1);
        id.task = sdurws::ird::core::TaskIdentity{};            // 五元组全零
        expectRejected(id, "运行身份不完整必须拒绝");
    }
    {
        SeriesIdentity id = makeIdentity("miss-alg", 1);
        id.algorithmVersion.clear();
        expectRejected(id, "算法版本空串必须拒绝");
    }
    {
        SeriesIdentity id = makeIdentity("miss-cfg", 1);
        id.dynConfigDigest.clear();
        expectRejected(id, "config.dyn 摘要空串必须拒绝");
    }
}

/**
 * 用例 6：Empty 序列语义与缺口计数（§4.6/§7.6——Empty 不产出统计；
 * planned>actual→Partial 且 actual 只计实际时刻）。
 */
TEST(DynSeries, EmptySemanticsAndGapCounting)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-03"}, std::vector<std::string>{});
    // 本用例验证：空序列冻结为 Empty（身份块＋contentIdentity 仍有效——
    // 序列可寻址、统计不产出）；缺口（计划 3 实际 2）→Partial 计数如实。
    {
        DynamicsSeriesBuilder b;
        const DynamicsSeries series = b.finalize(makeIdentity("empty", 0));
        EXPECT_EQ(series.validity.completeness, DynamicsValidity::Completeness::Empty);
        EXPECT_TRUE(series.samples.empty());
        EXPECT_TRUE(series.contentIdentity.isValid()) << "空序列仍可寻址（身份块进摘要）";
    }
    {
        DynamicsSeriesBuilder b;
        b.addSample(rowAt(0.0, 0, 0, DynJointType::Revolute, 0.0, 0.0, 0.0, 1.0, 0.0));
        b.addSample(rowAt(0.5, 0, 0, DynJointType::Revolute, 0.0, 0.0, 0.0, 1.0, 0.0));
        // 计划 3 时刻只到 2——缺口 1（DYN-SAMPLE-GAP 素材的上游职责，
        // 构建器侧如实计数）。
        const DynamicsSeries series = b.finalize(makeIdentity("gap", 3));
        EXPECT_EQ(series.validity.completeness, DynamicsValidity::Completeness::Partial);
        EXPECT_EQ(series.validity.plannedSampleCount, 3u);
        EXPECT_EQ(series.validity.actualSampleCount, 2u);
    }
}

// =====================================================================
// DynEnvelope 组——峰值（窗＋段）与时间加权 RMS（§7.2/§7.3）。
// =====================================================================

/**
 * 用例 7：峰值黄金算例——平顶窗、孤立窗、段报告、行序 token 表（DYN-03
 * "峰值必须报告持续时间窗和所在轨迹段"的主对照）。
 *
 * 手工网格（单关节转动，t=0..1.0 步 0.1；前 6 点段 0、后 5 点段 1）：
 *   τ  = [1,−2,5,5,5,−4,−6, 2, 1,0,0]  N·m
 *   qd = [0, 1,2,2,2, 1,0.5,0.2,0,0,0] rad/s
 *   qdd= [0, 1,3,3,3, 1, 0, 0, 0,0,0]  rad/s²
 *   P  = τ·qd = [0,−2,10,10,10,−4,−3,0.4,0,0,0] W
 * 期望（token 序 0..5）：
 *   tauPositive = 5      窗 [0.2,0.4]（平顶）tPeak=0.2 段 0
 *   tauNegative = max(−τ)=6   t=0.6（孤立）窗 [0.6,0.6] 段 1
 *   velocity    = max|qd| =2  窗 [0.2,0.4] tPeak=0.2
 *   acceleration= max|qdd|=3  窗 [0.2,0.4]
 *   powerPositive = max P =10 窗 [0.2,0.4]
 *   powerNegative = max(−P)=4 t=0.5（孤立）窗 [0.5,0.5] 段 0
 */
TEST(DynEnvelope, PeakGoldenFlatTopWindowSegmentAndTokenOrder)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-03"}, std::vector<std::string>{});
    // 本用例验证：峰值三要素（值/发生时间/所在段）＋持续等值窗（平顶自
    // 然覆盖整段、孤立峰值窗宽为零）＋每关节 6 行 token 序（§7.2 枚举
    // 序）＋来源工况透传。
    const double tau[11] = {1, -2, 5, 5, 5, -4, -6, 2, 1, 0, 0};
    const double qd[11] = {0, 1, 2, 2, 2, 1, 0.5, 0.2, 0, 0, 0};
    const double qdd[11] = {0, 1, 3, 3, 3, 1, 0, 0, 0, 0, 0};

    DynamicsSeriesBuilder b;
    for (int k = 0; k < 11; ++k) {
        const double t = 0.1 * k;                       // s
        const std::uint32_t seg = (k <= 5) ? 0u : 1u;   // 前 6 点段 0、后 5 点段 1
        b.addSample(rowAt(t, seg, 0, DynJointType::Revolute, 0.0, qd[k], qdd[k], tau[k],
                          tau[k] * qd[k]));
    }
    const DynamicsSeries series = b.finalize(makeIdentity("peak", 11));

    const DynamicsEnvelopeCalculator calc;
    const std::vector<PeakRecord> peaks = calc.computePeaks(series);
    ASSERT_EQ(peaks.size(), 6u) << "单关节恒 6 行（token 表）";

    // token 0：τ_max⁺＝5，平顶窗 [0.2,0.4]、tPeak=0.2、段 0。
    EXPECT_DOUBLE_EQ(peaks[0].value, 5.0);
    EXPECT_DOUBLE_EQ(peaks[0].tPeakS, 0.2);
    EXPECT_EQ(peaks[0].segmentIndex, 0u);
    EXPECT_DOUBLE_EQ(peaks[0].windowStartS, 0.2);
    EXPECT_DOUBLE_EQ(peaks[0].windowEndS, 0.4);
    EXPECT_TRUE(peaks[0].conditionId == series.conditionId);

    // token 1：τ_max⁻＝max(−τ)＝6，孤立 t=0.6（段 1）窗宽零。
    EXPECT_DOUBLE_EQ(peaks[1].value, 6.0);
    EXPECT_DOUBLE_EQ(peaks[1].tPeakS, 0.6);
    EXPECT_EQ(peaks[1].segmentIndex, 1u);
    EXPECT_DOUBLE_EQ(peaks[1].windowStartS, 0.6);
    EXPECT_DOUBLE_EQ(peaks[1].windowEndS, 0.6);

    // token 2/3：速度/加速度幅值峰（平顶窗同 [0.2,0.4]）。
    EXPECT_DOUBLE_EQ(peaks[2].value, 2.0);
    EXPECT_DOUBLE_EQ(peaks[2].windowStartS, 0.2);
    EXPECT_DOUBLE_EQ(peaks[2].windowEndS, 0.4);
    EXPECT_DOUBLE_EQ(peaks[3].value, 3.0);
    EXPECT_DOUBLE_EQ(peaks[3].windowStartS, 0.2);
    EXPECT_DOUBLE_EQ(peaks[3].windowEndS, 0.4);

    // token 4：功率正向 max(P)=10（平顶窗）；token 5：反向 max(−P)=4，
    // 孤立 t=0.5（段 0）。
    EXPECT_DOUBLE_EQ(peaks[4].value, 10.0);
    EXPECT_DOUBLE_EQ(peaks[4].windowStartS, 0.2);
    EXPECT_DOUBLE_EQ(peaks[4].windowEndS, 0.4);
    EXPECT_DOUBLE_EQ(peaks[5].value, 4.0);
    EXPECT_DOUBLE_EQ(peaks[5].tPeakS, 0.5);
    EXPECT_EQ(peaks[5].segmentIndex, 0u);
    EXPECT_DOUBLE_EQ(peaks[5].windowStartS, 0.5);
    EXPECT_DOUBLE_EQ(peaks[5].windowEndS, 0.5);
}

/**
 * 用例 8：时间加权 RMS 黄金算例——含驻留（分母 T_cycle=3s 含 2s 驻留）
 * 与禁样本平均对照（DYN-03"RMS 基于完整任务循环（含驻留）"主对照）。
 *
 * 网格（单关节转动）：t = 0, 0.5, 1.0, 2.0, 3.0
 *   运动段 [0,1]：τ=2 N·m、qd=1 rad/s；驻留段 (1,3]：τ=0.5 N·m（重力
 *   保持矩）、qd=0——驻留自然以时间权重进入。
 * 梯形黄金（逐相邻对展开——§7.3 公式固定）：
 *   [0,0.5]: ½(4+4)×0.5 = 2
 *   [0.5,1]: ½(4+4)×0.5 = 2
 *   [1,2]:   ½(4+0.25)×1 = 2.125
 *   [2,3]:   ½(0.25+0.25)×1 = 0.25
 *   Σ = 6.375；T_cycle = 3 ⇒ RMS = sqrt(6.375/3) = sqrt(2.125)
 * 对照（禁）样本平均：sqrt((3×4+2×0.25)/5)=sqrt(2.5)——两口径显著不同，
 * 本用例同时断言时间加权结果偏离样本平均，钉扎 D-DYN-9。
 */
TEST(DynEnvelope, RmsTimeWeightedWithDwellGolden)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-03"}, std::vector<std::string>{});
    // 本用例验证：RMS 时间加权（梯形对采样网格逐对展开）、分母含驻留
    // 全程、并显式偏离样本平均（禁样本平均的口径钉扎）。
    const double t[5] = {0.0, 0.5, 1.0, 2.0, 3.0};   // s（运动段密、驻留段稀）
    const double tau[5] = {2.0, 2.0, 2.0, 0.5, 0.5}; // N·m
    const double qd[5] = {1.0, 1.0, 1.0, 0.0, 0.0};  // rad/s（驻留 q̇=0）

    DynamicsSeriesBuilder b;
    for (int k = 0; k < 5; ++k) {
        const std::uint32_t seg = (t[k] <= 1.0) ? 0u : 1u;  // 驻留段标段 1
        b.addSample(rowAt(t[k], seg, 0, DynJointType::Revolute, 0.0, qd[k], 0.0, tau[k],
                          tau[k] * qd[k]));
    }
    const DynamicsSeries series = b.finalize(makeIdentity("rms-dwell", 5));

    const DynamicsEnvelopeCalculator calc;
    const double rms = calc.computeRms(series, 0);
    // 黄金：sqrt(6.375/3)（手算梯形——见用例头注释）。
    expectNearRel(rms * rms, 6.375 / 3.0, "RMS²＝梯形 Σ/T_cycle（含驻留）");
    // 含驻留的分辨力：若分母错误地不含驻留（T=1），RMS²=6.375——断言
    // 实值显著偏离该错值（口径双向钉扎）。
    EXPECT_GT(std::abs(rms * rms - 6.375), 1.0)
        << "分母必须含驻留全程（T_cycle=3s 而非 1s）";
    // 禁样本平均对照：样本平均 RMS²=12.5/5=2.5 ≠ 时间加权 2.125——
    // 差异显著（0.375），证明实现不是 Σ/√N。
    const double sampleMeanRms2 = (3.0 * 4.0 + 2.0 * 0.25) / 5.0;
    EXPECT_GT(std::abs(rms * rms - sampleMeanRms2), 0.3)
        << "时间加权与样本平均必须可区分（D-DYN-9 钉扎）";
}

/**
 * 用例 9：非 Ok 行不进统计（NFR-COR-03——NaN 不污染峰值、RMS 跳行后
 * 与手算梯形一致）。
 *
 * 网格：t=0..1 步 0.1、τ 恒 2，仅 t=0.3 行标 NonFiniteInput（τ=NaN）。
 * 期望：峰值仍 2（NaN 行被剔除）；速度峰窗在等值 run 处被非 Ok 行切断
 * （t=0.2 的窗只到自身）；RMS=2（τ 恒 2、跳行后相邻有效对 dt 补齐 1s）。
 */
TEST(DynEnvelope, NonOkRowsExcludedFromStatistics)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-03", "NFR-COR-03"},
                  std::vector<std::string>{});
    // 本用例验证：非 Ok 行（NonFiniteInput）不进峰值与 RMS——NaN 不
    // 静默转 0、不污染统计；等值窗以 Ok 行连续性为准。
    DynamicsSeriesBuilder b;
    for (int k = 0; k < 11; ++k) {
        DynamicsSample r = rowAt(0.1 * k, 0, 0, DynJointType::Revolute, 0.0, 0.5, 0.0,
                                 2.0, 1.0);
        if (k == 3) {                       // t=0.3：非有限输入行
            r.numericState = SampleNumericState::NonFiniteInput;
            r.tauTotal = std::numeric_limits<double>::quiet_NaN();
            r.mechanicalPower = std::numeric_limits<double>::quiet_NaN();
        }
        b.addSample(r);
    }
    const DynamicsSeries series = b.finalize(makeIdentity("nonok", 11));
    ASSERT_EQ(series.validity.completeness, DynamicsValidity::Completeness::Partial);
    EXPECT_EQ(series.validity.nonFiniteCount, 1u);

    const DynamicsEnvelopeCalculator calc;
    const std::vector<PeakRecord> peaks = calc.computePeaks(series);
    ASSERT_EQ(peaks.size(), 6u);
    EXPECT_DOUBLE_EQ(peaks[0].value, 2.0) << "τ_max⁺ 不受 NaN 行污染";
    EXPECT_DOUBLE_EQ(peaks[4].value, 1.0) << "功率正向峰不受 NaN 行污染";

    // RMS：τ 恒 2（Ok 行）——跳过 t=0.3 后相邻有效对时差补齐（0.2→0.4
    // 的 dt=0.2），Σ=½(4+4)×1.0=4，T=1 ⇒ RMS=2。
    const double rms = calc.computeRms(series, 0);
    expectNearRel(rms, 2.0, "跳行后 RMS 与手算梯形一致");
}

/**
 * 用例 10：Empty/无效显式 NaN（§7.6"绝不 0 值伪装"；§7.3 时间缺失不
 * 伪造 RMS）。
 */
TEST(DynEnvelope, EmptyAndDegenerateExplicitInvalid)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-03"}, std::vector<std::string>{});
    // 本用例验证：空序列峰值＝空 vector（Empty 不产出）；单样本/零跨度
    // RMS＝NaN（显式无效——不伪造 0）。
    const DynamicsEnvelopeCalculator calc;

    {
        DynamicsSeriesBuilder b;
        const DynamicsSeries emptySeries = b.finalize(makeIdentity("env-empty", 0));
        EXPECT_TRUE(calc.computePeaks(emptySeries).empty()) << "Empty 不产出峰值行";
        EXPECT_TRUE(std::isnan(calc.computeRms(emptySeries, 0))) << "Empty RMS 显式无效";
    }
    {
        DynamicsSeriesBuilder b;
        b.addSample(rowAt(0.0, 0, 0, DynJointType::Revolute, 0.0, 0.0, 0.0, 5.0, 0.0));
        const DynamicsSeries single = b.finalize(makeIdentity("env-single", 1));
        EXPECT_FALSE(calc.computePeaks(single).empty());
        EXPECT_TRUE(std::isnan(calc.computeRms(single, 0)))
            << "单样本无时间区间——RMS 显式 NaN（不伪造）";
    }
}

// =====================================================================
// DynPowerEnergy 组——功率/能量分项（§7.5）。
// =====================================================================

/**
 * 用例 11：能量黄金算例——含驻留循环（§10.5 合法示例面）与 E_net 对照
 * 样本行能量积分状态（V-22 语义——两条积分链的被验证性质）。
 *
 * 网格：t=0,0.5,1,2,3；运动段 [0,1] τ=2、qd=1（P=2 W 恒）；驻留段
 * (1,3] τ=0.5、qd=0（P=0）。P 网格值＝τ·qd 逐点＝[2,2,2,0,0]（跳变落
 * 在 t=1 采样点之后——梯形按网格值逐对展开）。
 *   E⁺ = ∫max(P,0)dt ＝ ½(2+2)×0.5 ＋ ½(2+2)×0.5 ＋ ½(2+0)×1 ＋ ½(0+0)×1
 *      = 1 ＋ 1 ＋ 1 ＋ 0 = 3 J（驻留段 (1,3] 区间贡献 0——分母仍含 2s）
 *   E⁻ = 0 J；E_net = 3 J；T_cycle = 3 s；meanPower = 1 W
 *   powerPeak 幅值 = 2 W（平顶 run 覆盖 [0,1]）；样本行 energyIntegralJ
 *   末值（同梯形逐对累积）= 3 J ⇒ E_net 与之对照一致。
 */
TEST(DynPowerEnergy, EnergySplitGoldenWithDwellAndNetMatch)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-03"}, std::vector<std::string>{});
    // 本用例验证：E⁺/E⁻/E_net/meanPower 手工梯形黄金、驻留计入分母与
    // 区间、功率峰值窗、E_net 与样本行能量积分状态的一致性（对照性质）。
    const double t[5] = {0.0, 0.5, 1.0, 2.0, 3.0};    // s
    const double tau[5] = {2.0, 2.0, 2.0, 0.5, 0.5};  // N·m
    const double qd[5] = {1.0, 1.0, 1.0, 0.0, 0.0};   // rad/s
    const double p[5] = {2.0, 2.0, 2.0, 0.0, 0.0};    // W（τ·qd 逐点）

    // 样本行能量积分状态（同梯形逐对累积）：E(0)=0；E(0.5)=1；E(1)=2；
    // E(2)=2+½(2+0)×1=3；E(3)=3（J）。
    const double e[5] = {0.0, 1.0, 2.0, 3.0, 3.0};

    DynamicsSeriesBuilder b;
    for (int k = 0; k < 5; ++k) {
        b.addSample(rowAt(t[k], (t[k] <= 1.0) ? 0u : 1u, 0, DynJointType::Revolute, 0.0,
                          qd[k], 0.0, tau[k], p[k], e[k]));
    }
    const DynamicsSeries series = b.finalize(makeIdentity("pe-golden", 5));

    const PowerEnergyCalculator calc;
    const PowerEnergySummary sum = calc.compute(series);
    ASSERT_EQ(sum.joints.size(), 1u);
    const PowerEnergySummary::JointPowerEnergy& row = sum.joints[0];

    EXPECT_TRUE(sum.timeParamAvailable);
    EXPECT_TRUE(sum.includesDwell) << "积分按序列全程执行——驻留计入";
    expectNearRel(sum.cycleDurationS, 3.0, "T_cycle 含驻留全程");
    expectNearRel(row.positiveEnergyJ, 3.0, "E⁺（驱动功——含跳变段梯形项）");
    expectNearRel(row.negativeEnergyJ, 0.0, "E⁻（无制动段）");
    expectNearRel(row.netEnergyJ, 3.0, "E_net");
    expectNearRel(row.meanPowerW, 1.0, "平均功率 E_net/T_cycle");
    expectNearRel(row.powerPeak.value, 2.0, "功率幅值峰值");
    EXPECT_DOUBLE_EQ(row.powerPeak.windowStartS, 0.0);
    EXPECT_DOUBLE_EQ(row.powerPeak.windowEndS, 1.0);
    EXPECT_EQ(row.powerPeak.segmentIndex, 0u);
    // E_net 与样本行能量积分状态对照（末样本 E(t_N)——V-22 语义）。
    expectNearRel(row.netEnergyJ, series.samples.back().energyIntegralJ,
                  "E_net 对照样本行能量积分末值");
}

/**
 * 用例 12：制动段符号（§7.5——E⁻<0<E⁺；功率峰值多段等值取峰值样本所
 * 在 run）。
 *
 * 网格 t=0,1,2：τ=2 恒、qd=[1,0,−1]（对称减速制动）→P=[2,0,−2]。
 *   E⁺=[0,1]:½(2+0)=1；[1,2]:0 ⇒ 1 J
 *   E⁻=[1,2]:½(0−2)×1=−1 ⇒ −1 J；E_net=0；meanPower=0
 *   |P|=[2,0,2]——峰值 2 在 t=0 与 t=2 两段等值；取峰值样本所在 run
 *   ⇒ 窗 [0,0]、tPeak=0（多段等值的确定性口径）。
 */
TEST(DynPowerEnergy, BrakingSignAndMultiRunPeakWindow)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-03"}, std::vector<std::string>{});
    // 本用例验证：制动/发电段 E⁻<0<E⁺（§10.5 合法示例"含制动段"）；
    // 幅值峰值多段等值时窗取峰值样本所在连续 run（确定性）。
    DynamicsSeriesBuilder b;
    const double qd[3] = {1.0, 0.0, -1.0};   // rad/s（对称减速）
    for (int k = 0; k < 3; ++k) {
        b.addSample(rowAt(1.0 * k, 0, 0, DynJointType::Revolute, 0.0, qd[k], 0.0, 2.0,
                          2.0 * qd[k]));
    }
    const DynamicsSeries series = b.finalize(makeIdentity("brake", 3));

    const PowerEnergyCalculator calc;
    const PowerEnergySummary sum = calc.compute(series);
    ASSERT_EQ(sum.joints.size(), 1u);
    expectNearRel(sum.joints[0].positiveEnergyJ, 1.0, "E⁺");
    expectNearRel(sum.joints[0].negativeEnergyJ, -1.0, "E⁻（制动负功）");
    EXPECT_LT(sum.joints[0].negativeEnergyJ, 0.0);
    EXPECT_GT(sum.joints[0].positiveEnergyJ, 0.0);
    expectNearRel(sum.joints[0].netEnergyJ, 0.0, "E_net（对称循环为零）");
    expectNearRel(sum.joints[0].powerPeak.value, 2.0, "幅值峰值");
    EXPECT_DOUBLE_EQ(sum.joints[0].powerPeak.tPeakS, 0.0) << "多段等值取时间轴首个";
    EXPECT_DOUBLE_EQ(sum.joints[0].powerPeak.windowStartS, 0.0);
    EXPECT_DOUBLE_EQ(sum.joints[0].powerPeak.windowEndS, 0.0) << "窗＝峰值样本所在 run";

    // 峰值分列口径对照（Envelope 计算器——正/反向分列各自的窗）：
    const DynamicsEnvelopeCalculator env;
    const std::vector<PeakRecord> peaks = env.computePeaks(series);
    ASSERT_EQ(peaks.size(), 6u);
    EXPECT_DOUBLE_EQ(peaks[4].value, 2.0);   // max(P)=2 @t=0 窗 [0,0]
    EXPECT_DOUBLE_EQ(peaks[5].value, 2.0);   // max(−P)=2 @t=2 窗 [2,2]
    EXPECT_DOUBLE_EQ(peaks[5].tPeakS, 2.0);
    EXPECT_DOUBLE_EQ(peaks[5].windowStartS, 2.0);
    EXPECT_DOUBLE_EQ(peaks[5].windowEndS, 2.0);
}

/**
 * 用例 13：无效时间显式无效（§4.6"无时间参数"——不伪造积分/平均功率）。
 */
TEST(DynPowerEnergy, DegenerateTimeExplicitInvalid)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-03"}, std::vector<std::string>{});
    // 本用例验证：单样本序列（无时间区间）→timeParamAvailable=false＋
    // 能量/平均功率/时长全部 NaN（NotProvided——不伪造 0）；空序列→
    // joints 空。
    const PowerEnergyCalculator calc;
    {
        DynamicsSeriesBuilder b;
        b.addSample(rowAt(0.0, 0, 0, DynJointType::Revolute, 0.0, 1.0, 0.0, 2.0, 2.0));
        const PowerEnergySummary sum = calc.compute(b.finalize(makeIdentity("pe-single", 1)));
        ASSERT_EQ(sum.joints.size(), 1u);
        EXPECT_FALSE(sum.timeParamAvailable);
        EXPECT_FALSE(sum.includesDwell);
        EXPECT_TRUE(std::isnan(sum.cycleDurationS));
        EXPECT_TRUE(std::isnan(sum.joints[0].positiveEnergyJ));
        EXPECT_TRUE(std::isnan(sum.joints[0].netEnergyJ));
        EXPECT_TRUE(std::isnan(sum.joints[0].meanPowerW));
    }
    {
        DynamicsSeriesBuilder b;
        const PowerEnergySummary sum = calc.compute(b.finalize(makeIdentity("pe-empty", 0)));
        EXPECT_TRUE(sum.joints.empty()) << "Empty 不产出逐关节行";
        EXPECT_FALSE(sum.timeParamAvailable);
    }
}

// =====================================================================
// DynStatsIntegration 组——端到端与类型化（RNEA→序列→统计）。
// =====================================================================

/**
 * 用例 14：端到端黄金——RNEA 静态含驻留循环（qd=0 全程）→构建器冻结→
 * 三计算器：重力保持矩的峰值/RMS/功率全链（acceptance 1/2 的公共接口
 * 消费路径——不留只测内部函数的盲区）。
 *
 * 解析（二连杆地面安装、q=[0,0]、q̇=q̈=0）：
 *   τ1 = −g·(m1c1+m2(L1+c2)) = −10.3005 N·m；τ2 = −g·m2c2 = −1.4715 N·m
 *   （恒定——平顶窗覆盖全跨度 [0,2]）；qd=0 ⇒ P=0、E⁺=E⁻=0。
 */
TEST(DynStatsIntegration, RneaStaticCycleEndToEnd)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-01", "DYN-03"}, std::vector<std::string>{});
    // 本用例验证：公共接口消费链 InverseDynamicsEvaluator→
    // DynamicsSeriesBuilder→三计算器全绿——峰值/RMS=重力保持矩解析值、
    // 平顶窗全跨度、功率为零不伪造、工况 id 与段传播。
    const CanonicalModel model = makeTwoLinkModel(groundWorld(), FrictionSpec{}, std::nullopt);
    const ObjectId condition = idFrom<ObjectId>("e2e-cond");

    // 静态循环（含"驻留"全程——q̇=0 即重力保持矩工况）：t=0,1,2 三点、
    // 段 0/1/2（三点跨三段——段传播可断言）。
    InverseDynRequest req;
    req.conditionId = condition;
    req.model = &model;
    for (int k = 0; k < 3; ++k) {
        InverseDynSampleInput s;
        s.t = 1.0 * k;
        s.segmentIndex = static_cast<std::uint32_t>(k);
        s.q = {0.0, 0.0};
        s.qd = {0.0, 0.0};
        s.qdd = {0.0, 0.0};
        req.samples.push_back(s);
    }
    FakeContext ctx;
    const InverseDynOutcome outcome = InverseDynamicsEvaluator().evaluate(req, ctx);
    ASSERT_EQ(outcome.validity.completeness, DynamicsValidity::Completeness::Complete);

    // RNEA 样本行 → 构建器（validitySeed 透传评估器事实）。
    DynamicsSeriesBuilder b;
    for (const DynamicsSample& r : outcome.samples) { b.addSample(r); }
    SeriesIdentity id = makeIdentity("e2e", 3);
    id.conditionId = condition;               // 工况 id 与评估请求一致
    id.validitySeed = outcome.validity;       // 事实透传（摩擦缺失标记——见断言）
    const DynamicsSeries series = b.finalize(id);

    // 序列面：6 行（3 时刻×2 关节）、Complete、摩擦缺失事实透传（模型
    // 无摩擦三元组——§5.5 第 1 层）、内容身份有效。
    ASSERT_EQ(series.samples.size(), 6u);
    EXPECT_EQ(series.validity.completeness, DynamicsValidity::Completeness::Complete);
    EXPECT_EQ(series.validity.plannedSampleCount, 3u);
    EXPECT_EQ(series.validity.actualSampleCount, 3u);
    EXPECT_TRUE(series.validity.frictionMissing);
    EXPECT_TRUE(series.contentIdentity.isValid());

    // 解析重力矩（T03 用例 1 同式——转动关节 N·m）。
    const double tau1 = -kG * (kM1 * kC1 + kM2 * (kL1 + kC2));
    const double tau2 = -kG * kM2 * kC2;

    const DynamicsEnvelopeCalculator env;
    const std::vector<PeakRecord> peaks = env.computePeaks(series);
    ASSERT_EQ(peaks.size(), 12u) << "两关节×6 行";
    // 关节 0 行组（token 0..5）＝peaks[0..5]：τ 恒负 ⇒ tau+=tau1 本身、
    // tau−=|tau1|；qd=0 平顶窗覆盖全跨度 [0,2]。
    EXPECT_DOUBLE_EQ(peaks[0].value, tau1);
    EXPECT_DOUBLE_EQ(peaks[1].value, -tau1);
    EXPECT_DOUBLE_EQ(peaks[2].value, 0.0);
    EXPECT_DOUBLE_EQ(peaks[2].windowStartS, 0.0);
    EXPECT_DOUBLE_EQ(peaks[2].windowEndS, 2.0) << "平顶窗自然覆盖整段平顶";
    EXPECT_DOUBLE_EQ(peaks[4].value, 0.0) << "功率峰恒 0（qd=0）——如实 0 不是伪装";
    EXPECT_TRUE(peaks[0].conditionId == condition);
    // 关节 1 行组（token 0..5）＝peaks[6..11]。
    EXPECT_DOUBLE_EQ(peaks[6].value, tau2);
    EXPECT_DOUBLE_EQ(peaks[7].value, -tau2);

    // RMS＝重力保持矩幅值（常数全跨度平顶）。
    expectNearRel(env.computeRms(series, 0), std::abs(tau1), "关节 0 力矩 RMS");
    expectNearRel(env.computeRms(series, 1), std::abs(tau2), "关节 1 力矩 RMS");

    // 功率/能量：qd=0 ⇒ P=0 ⇒ E⁺=E⁻=E_net=meanPower=0（真实零——
    // 与"无效 NaN"对照：timeParamAvailable=true 时零就是零）。
    const PowerEnergyCalculator pe;
    const PowerEnergySummary sum = pe.compute(series);
    ASSERT_EQ(sum.joints.size(), 2u);
    EXPECT_TRUE(sum.timeParamAvailable);
    EXPECT_TRUE(sum.includesDwell);
    expectNearRel(sum.cycleDurationS, 2.0, "T_cycle=2s");
    expectNearRel(sum.joints[0].positiveEnergyJ, 0.0, "E⁺ 真实零");
    expectNearRel(sum.joints[0].netEnergyJ, 0.0, "E_net 真实零");
    EXPECT_EQ(sum.joints[0].jointIndex, 0u);
    EXPECT_EQ(sum.joints[1].jointIndex, 1u);
}

/**
 * 用例 15：类型化不混算黄金（DTB 禁止项"力矩/力类型化混用"——转动
 * N·m／移动 N 逐关节独立统计的解析对照）。
 *
 * 混合链解析（见文件头夹具注释）：移动关节轴力恒 (m1+m2)g=14.715 N、
 * 转动关节矩恒 I2zz·α=0.088 N·m——两关关节统计量纲不同、数值独立：
 * 若实现跨关节聚合，任一侧统计必被另一侧量值污染（14.715 与 0.088
 * 相差两个数量级，污染可检出）。
 */
TEST(DynStatsIntegration, MixedJointTypeTypingGolden)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-03"}, std::vector<std::string>{});
    // 本用例验证：Prismatic+Revolute 混合链逐关节统计——移动关节峰值/
    // RMS＝轴力（N）、转动关节＝I2zz·α（N·m），jointType 标签逐行传播，
    // 两量纲数值互不混算。
    const CanonicalModel model = makePrismRevModel();
    const ObjectId condition = idFrom<ObjectId>("mix-cond");

    // 激励：关节 1（移动）静止；关节 2（转动）恒角加速 α=4 rad/s²——
    // t=0..1 步 0.25（q2=αt²/2、qd2=αt、qdd2=α 恒）。
    InverseDynRequest req;
    req.conditionId = condition;
    req.model = &model;
    for (int k = 0; k <= 4; ++k) {
        const double t = 0.25 * k;
        InverseDynSampleInput s;
        s.t = t;
        s.segmentIndex = 0u;
        s.q = {0.0, 0.5 * kAlpha * t * t};     // rad（权威角）
        s.qd = {0.0, kAlpha * t};              // m/s、rad/s
        s.qdd = {0.0, kAlpha};                 // m/s²、rad/s²
        req.samples.push_back(s);
    }
    FakeContext ctx;
    const InverseDynOutcome outcome = InverseDynamicsEvaluator().evaluate(req, ctx);
    ASSERT_EQ(outcome.validity.completeness, DynamicsValidity::Completeness::Complete);

    // 类型化标签传播：关节 0 行 Prismatic（力 N）、关节 1 行 Revolute
    // （矩 N·m）——样本行 jointType 逐行正确。
    for (const DynamicsSample& r : outcome.samples) {
        if (r.jointIndex == 0u) {
            EXPECT_EQ(r.jointType, DynJointType::Prismatic);
        } else {
            EXPECT_EQ(r.jointType, DynJointType::Revolute);
        }
    }

    DynamicsSeriesBuilder b;
    for (const DynamicsSample& r : outcome.samples) { b.addSample(r); }
    SeriesIdentity id = makeIdentity("mix", 5);
    id.conditionId = condition;
    const DynamicsSeries series = b.finalize(id);

    // 解析黄金（N 与 N·m 两套独立期望——见夹具头注释）。
    const double fAxis = prismAxialForce();   // 14.715 N（关节 0 τ 恒正值）
    const double tauRev = revTorque();        // 0.088 N·m（关节 1 τ 恒正值）
    const DynamicsEnvelopeCalculator env;
    const std::vector<PeakRecord> peaks = env.computePeaks(series);
    ASSERT_EQ(peaks.size(), 12u);
    // 关节 0（移动）：τ_total 恒 fAxis ⇒ tau+=fAxis；全循环无负力矩 ⇒
    // tau−=max(−τ)=−fAxis（幅值形态的带符号语义——见 PeakRecord 注释）。
    expectNearRel(peaks[0].value, fAxis, "移动关节轴力峰（N）");
    expectNearRel(peaks[1].value, -fAxis, "移动关节反向幅值（全正循环为负形态）");
    expectNearRel(peaks[2].value, 0.0, "移动关节速度峰（m/s——静止）");
    // 关节 1（转动）：τ 恒 tauRev ⇒ 同款正/反形态；速度峰=α·t_N=4 rad/s。
    expectNearRel(peaks[6].value, tauRev, "转动关节矩峰（N·m）");
    expectNearRel(peaks[7].value, -tauRev, "转动关节反向幅值");
    expectNearRel(peaks[8].value, kAlpha, "转动关节速度峰（rad/s——末样本）");

    // RMS 逐关节解析（恒力矩/恒力——RMS=幅值）：两量纲数值互不污染。
    expectNearRel(env.computeRms(series, 0), fAxis, "移动关节力 RMS（N）");
    expectNearRel(env.computeRms(series, 1), tauRev, "转动关节矩 RMS（N·m）");

    // 功率：qd1=0 ⇒ 关节 0 功率恒 0；qd2 随时间增长 ⇒ E_net=∫τ·qd dt
    // ＝τ·α·∫t dt（恒力矩×线性速度——解析 0.44 J @T=1s）。
    const PowerEnergyCalculator pe;
    const PowerEnergySummary sum = pe.compute(series);
    ASSERT_EQ(sum.joints.size(), 2u);
    expectNearRel(sum.joints[0].netEnergyJ, 0.0, "移动关节净能量（静止）");
    expectNearRel(sum.joints[1].netEnergyJ, tauRev * kAlpha * 0.5, "转动关节净能量（J）");
    expectNearRel(sum.joints[1].powerPeak.value, tauRev * kAlpha,
                  "转动关节功率峰（W——末样本）");
}

/**
 * 用例 16：OperatingConditionResult 聚合面（§4.4 类型面——编排层装配
 * 形态的字段承载自证；本单元不提供编排器，只验证类型可承载三计算器
 * 产出且字段自洽）。
 */
TEST(DynStatsIntegration, OperatingConditionResultAggregationShape)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-03"}, std::vector<std::string>{});
    // 本用例验证：§4.4 OperatingConditionResult 类型承载——conditionId/
    // series/peaks/powerEnergy/validity 装配后字段自洽（聚合值与来源
    // 计算器输出逐位一致）。
    const double t[3] = {0.0, 1.0, 2.0};       // s
    const double tau[3] = {3.0, 3.0, 1.0};     // N·m
    DynamicsSeriesBuilder b;
    for (int k = 0; k < 3; ++k) {
        b.addSample(rowAt(t[k], 0, 0, DynJointType::Revolute, 0.0, 1.0, 0.0, tau[k],
                          tau[k] * 1.0));
    }
    const DynamicsSeries series = b.finalize(makeIdentity("ocr", 3));

    const DynamicsEnvelopeCalculator env;
    const PowerEnergyCalculator pe;
    OperatingConditionResult result;
    result.conditionId = series.conditionId;
    result.series = series;
    result.peaks = env.computePeaks(series);
    result.powerEnergy = pe.compute(series);
    result.validity = series.validity;

    EXPECT_TRUE(result.conditionId == result.series.conditionId);
    ASSERT_EQ(result.peaks.size(), 6u);
    EXPECT_DOUBLE_EQ(result.peaks[0].value, 3.0);
    EXPECT_NEAR(result.powerEnergy.joints[0].positiveEnergyJ,
                0.5 * (3.0 + 3.0) * 1.0 + 0.5 * (3.0 + 1.0) * 1.0, 1e-12);
    EXPECT_EQ(result.validity.completeness, DynamicsValidity::Completeness::Complete);
}
