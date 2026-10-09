/**
 * @file   RneaTest.cpp
 * @brief  RNEA 逆动力学评估器用例组（DynRnea）——二连杆解析算例、静态
 *         重力矩（含倒挂 AT-37 动力侧）、分项恒等式与动力学模型解析对照、
 *         摩擦符号约定与 MDL-16 缺失降级、负载事件变体、外力恒零、
 *         fail-fast 前置、非有限输入标记、DYN-04 传动无关边界与取消语义
 *         （任务契约 WP-17-T03 acceptance 逐条的执行证明面）。
 *
 * 设计依据：
 *   - units/dynamics.md §5（§5.2 分项图/§5.3 输入覆盖/§5.4 负载变体/
 *     §5.5 摩擦降级/§5.6 失败语义）、§10.1（评估器契约表）
 *   - 需求 DYN-01/02、MDL-16（摩擦输入链）、MDL-22（重力投影单一消费）、
 *     DYN-04（关节侧结果与候选传动无关）、DYN-06（降级不包装精确）、
 *     NFR-COR-03（非有限拒绝）、AT-37（倒挂静态重力矩符号/量值正确）
 *   - 任务契约 tasks/foundation/WP-17-T03.json（acceptance 1/2/3）
 *
 * 数值对照口径（附录 D C7"测试对照容差与产品容差分离"）：解析期望值与
 *   RNEA 数值结果的一致性按**相对容差 1e-9**断言（本文件逐用例声明——
 *   双精度解析算例的表示误差远低于此；分项恒等式同容差）；无任何容差
 *   进入产品代码（产品侧零自设阈值——卡 §5.5/§7.2 纪律）。
 */

#include <sdurws/ird/dynamics/InverseDynamics.hpp>

#include <sdurws/ird/dynamics/DiagCodes.hpp>
#include <sdurws/ird/dynamics/DynTypes.hpp>
#include <sdurws/ird/dynamics/Errors.hpp>
#include <sdurws/ird/runtime/BaseWorldTransform.hpp>  // gravityToBase——重力投影规则单点
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "TwoLinkFixture.hpp"

using namespace sdurws::ird::dynamics::testfixture;
using sdurws::ird::core::DiagnosticRecord;
using sdurws::ird::core::ObjectId;
using sdurws::ird::dynamics::DynamicsSample;
using sdurws::ird::dynamics::DynamicsValidity;
using sdurws::ird::dynamics::DynamicsError;
using sdurws::ird::dynamics::EndEffectorPayload;
using sdurws::ird::dynamics::InverseDynOutcome;
using sdurws::ird::dynamics::InverseDynRequest;
using sdurws::ird::dynamics::InverseDynSampleInput;
using sdurws::ird::dynamics::InverseDynamicsEvaluator;
using sdurws::ird::dynamics::PayloadEvent;
using sdurws::ird::dynamics::SampleNumericState;

namespace {

// =====================================================================
// 宿主上下文本地替身（evidence 卡口径：测试以本地替身实现——替身只承载
// 取消/进度对话形状，不伪造任何评估逻辑）。
// =====================================================================

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

// =====================================================================
// 请求组装助手（激励样本/工况身份/重力投影的标准拼装）。
// =====================================================================

/// 单样本激励（全关节同值列——简化算例；单位随关节类型）。
InverseDynSampleInput sampleAt(double t, const std::vector<double>& q,
                               const std::vector<double>& qd, const std::vector<double>& qdd,
                               std::uint32_t segment = 0)
{
    InverseDynSampleInput s;
    s.t = t;
    s.segmentIndex = segment;
    s.q = q;
    s.qd = qd;
    s.qdd = qdd;
    return s;
}

/// 评估请求（重力投影经 runtime 规则函数从模型世界块生成——消费同一
/// 编译变换的示范链：R_world_base 与 g_world 均取自 CanonicalModel 编译
/// 产物，投影＝gravityToBase 单点，本域零二次旋转）。
InverseDynRequest makeRequest(const CanonicalModel& model, const ObjectId& conditionId,
                              const std::vector<InverseDynSampleInput>& samples)
{
    InverseDynRequest req;
    req.conditionId = conditionId;
    req.model = &model;
    const rw::math::Vector3D<double> gBase =
        sdurws::ird::runtime::gravityToBase(model.world().T_world_base.R(),
                                            model.world().gravityWorld);
    req.gravityBase[0] = gBase[0];
    req.gravityBase[1] = gBase[1];
    req.gravityBase[2] = gBase[2];
    req.samples = samples;
    return req;
}

/// 世界系重力幅值（单位 m/s²——解析式的 g；WorldPlacement 默认 (0,0,−9.81)）。
constexpr double kG = 9.81;

/// 解析对照的相对容差（本文件声明——测试对照口径，附录 D C7 分离）。
constexpr double kAnalyticRelTol = 1e-9;

/// 相对容差断言（期望为 0 时退化为绝对 1e-12——避免零除）。
void expectNearRel(double actual, double expected, const char* what)
{
    const double tol = std::max(1e-12, kAnalyticRelTol * std::abs(expected));
    EXPECT_NEAR(actual, expected, tol) << what << "（实得 " << actual << "，期望 " << expected
                                       << "）";
}

/// 在诊断清单中查码（是否存在给定稳定码的素材）。
bool hasDiagCode(const InverseDynOutcome& out, const std::string& code)
{
    for (const DiagnosticRecord& d : out.diagnostics) {
        if (d.code == code) { return true; }
    }
    return false;
}

/// 平面二连杆质量阵 M(q2)（解析基准——拉格朗日推导，见用例 4 注释；
/// 单位 kg·m²，绕关节轴 Y 分量）。
void analyticMassMatrix(double q2, double m11[1], double* m12, double* m22)
{
    *m11 = kM1 * kC1 * kC1 + kI1Yy + kM2 * (kL1 * kL1 + kC2 * kC2 + 2.0 * kL1 * kC2 * std::cos(q2))
         + kI2Yy;
    *m12 = kM2 * (kC2 * kC2 + kL1 * kC2 * std::cos(q2)) + kI2Yy;
    *m22 = kM2 * kC2 * kC2 + kI2Yy;
}

}  // namespace

// =====================================================================
// 用例 1：二连杆静态重力矩解析算例（地面安装）——acceptance 1 的主对照。
// 解析（拉格朗日 G(q)=∂V/∂q 在 q=0 的值；τ 为关节驱动矩——重力使其为负）：
//   τ1 = −g·(m1·c1 + m2·(L1+c2))；τ2 = −g·m2·c2
// =====================================================================

TEST(DynRnea, StaticGravityTwoLinkAnalytic)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-01"}, std::vector<std::string>{"AT-37"});
    // 本用例验证：RNEA 静态重力通道与二连杆解析解逐关节一致（DYN-01 关节
    // 空间精确；AT-37 联动 DYN-01 解析算例的地面安装基准侧）。
    const CanonicalModel model = makeTwoLinkModel(groundWorld(), FrictionSpec{}, std::nullopt);
    const ObjectId condition = idFrom<ObjectId>("cond-static");

    InverseDynRequest req = makeRequest(model, condition,
                                        {sampleAt(0.0, {0.0, 0.0}, {0.0, 0.0}, {0.0, 0.0})});
    FakeContext ctx;
    const InverseDynOutcome out = InverseDynamicsEvaluator().evaluate(req, ctx);

    ASSERT_EQ(out.samples.size(), 2u);          // 逐时刻逐关节行（§4.6 排序纪律）
    EXPECT_EQ(out.samples[0].jointIndex, 0u);
    EXPECT_EQ(out.samples[1].jointIndex, 1u);
    EXPECT_TRUE(out.samples[0].conditionId == condition);

    const double expectedTau1 = -kG * (kM1 * kC1 + kM2 * (kL1 + kC2));  // N·m
    const double expectedTau2 = -kG * kM2 * kC2;                        // N·m
    expectNearRel(out.samples[0].tauGravity, expectedTau1, "关节 1 静态重力矩");
    expectNearRel(out.samples[1].tauGravity, expectedTau2, "关节 2 静态重力矩");

    // 静态（q̇=q̈=0、无摩擦模型）：总力矩＝重力项；其余分项为零。
    expectNearRel(out.samples[0].tauTotal, expectedTau1, "关节 1 静态总力矩");
    expectNearRel(out.samples[1].tauTotal, expectedTau2, "关节 2 静态总力矩");
    EXPECT_DOUBLE_EQ(out.samples[0].tauInertia, 0.0);
    EXPECT_DOUBLE_EQ(out.samples[0].tauCoriolisCentrifugal, 0.0);
    EXPECT_DOUBLE_EQ(out.samples[0].tauExternal, 0.0);   // P-DYN-3：恒 0 不伪造
    EXPECT_EQ(out.samples[0].numericState, SampleNumericState::Ok);
    EXPECT_EQ(out.validity.completeness, DynamicsValidity::Completeness::Complete);
    EXPECT_EQ(out.validity.plannedSampleCount, 1u);
    EXPECT_EQ(out.validity.actualSampleCount, 1u);
    // 无摩擦模型（三元组全 NotProvided）→MDL-16 缺失标记（数值按 0 继续）。
    EXPECT_TRUE(out.validity.frictionMissing);
}

// =====================================================================
// 用例 1b：移动关节滑移臂（F-580 回归——audit/unit-code-review-20261009）
// =====================================================================

/**
 * @brief 组装"近端回转＋沿臂滑移"两关节混合链（构建器直构——模式同
 *        StatisticsTest 混合链夹具）。
 *
 * 几何（基座系，q0=0）：关节 0＝Revolute（axis=+Y，原点＝基座原点）；
 * 关节 1＝Prismatic（axis=+X，原点＝(L1,0,0)——滑移沿臂方向）。物性复用
 * 二连杆夹具常量：m1=kM1（质心距关节 0 原 kC1，沿 x̂）、m2=kM2（质心距
 * 关节 1 原 kC2，沿 x̂）。静力解析（重力 (0,0,−g)，滑移 d＝q1）：
 *   τ0（绕 +Y）＝ −g·(m1·L1 ＋ m2·(L1＋d＋c2))——**d 项＝滑移臂贡献**
 *   （体 2 质心在关节 1 原点之外 d+c2 处，其重力对关节 0 的矩经关节 1
 *   内力/内矩上传时必须计及滑移平移）；
 *   τ1（沿 +X）＝ 0（重力与轴正交——移动关节广义力取沿轴内力）。
 */
inline CanonicalModel makePrismArmModel()
{
    CanonicalModelHeader header;
    header.project = idFrom<core::ProjectId>("arm-prj");
    header.branch = idFrom<core::BranchId>("arm-brn");
    header.revision = idFrom<core::RevisionId>("arm-rev-1");
    header.revisionSeq = 1;
    header.descriptionContractVersion = 1;
    header.compilerContractVersion = 1;
    header.builtFrom = digestOf("arm-description-bytes");

    const ObjectId robot = idFrom<ObjectId>("arm-robot");
    const ObjectId j0 = idFrom<ObjectId>("arm-j0");
    const ObjectId j1 = idFrom<ObjectId>("arm-j1");
    const ObjectId l0 = idFrom<ObjectId>("arm-l0");
    const ObjectId l1 = idFrom<ObjectId>("arm-l1");
    const ObjectId l2 = idFrom<ObjectId>("arm-l2");
    auto addRef = [&header](const ObjectId& id, const char* seed, const char* token) {
        ObjectRefEntry e;
        e.objectId = id;
        e.contentVersion = cvFrom(seed);
        e.objectTypeToken = token;
        e.digest = digestOf(std::string{seed} + "-bytes");
        header.objectRefs.push_back(e);
    };
    addRef(robot, "arm-robot", "robot-design");
    addRef(j0, "arm-j0", "joint");
    addRef(j1, "arm-j1", "joint");
    addRef(l0, "arm-l0", "link");
    addRef(l1, "arm-l1", "link");
    addRef(l2, "arm-l2", "link");

    RobotChain chain;
    chain.robotObjectId = robot;
    chain.robotLocalName = "DYN_ARM_PRISM";
    chain.deviceName = "DYN_ARM_PRISM";

    // 关节 0：近端回转（axis=+Y——重力矩非退化轴；原点＝基座原点）。
    CanonicalJoint joint0;
    joint0.objectId = j0;
    joint0.localName = "shoulder";
    joint0.type = JointType::Revolute;
    joint0.axis = rw::math::Vector3D<double>(0.0, 1.0, 0.0);  // 关节系单位向量
    joint0.bounds = JointBounds{-3.14159265358979323846, 3.14159265358979323846};
    joint0.maxVelocity = val(3.0);  // rad/s
    joint0.origin = rw::math::Transform3D<double>(
        rw::math::Vector3D<double>(0.0, 0.0, 0.0), identityRotation());
    chain.joints.push_back(joint0);

    // 关节 1：沿臂滑移（axis=+X；原点＝(L1,0,0)——关节 0 伸臂端）。
    CanonicalJoint joint1;
    joint1.objectId = j1;
    joint1.localName = "extend";
    joint1.type = JointType::Prismatic;
    joint1.axis = rw::math::Vector3D<double>(1.0, 0.0, 0.0);  // 关节系单位向量
    joint1.bounds = JointBounds{0.0, 1.0};                    // m
    joint1.maxVelocity = val(1.0);                            // m/s
    joint1.origin = rw::math::Transform3D<double>(
        rw::math::Vector3D<double>(kL1, 0.0, 0.0), identityRotation());
    chain.joints.push_back(joint1);

    // 连杆（links[0]＝基座体不参与动力学；links[i] 为关节 i 的父体）。
    CanonicalLink baseLink;
    baseLink.objectId = l0;
    baseLink.localName = "arm_base";
    chain.links.push_back(baseLink);

    CanonicalLink link1;
    link1.objectId = l1;
    link1.localName = "arm_link1";
    link1.mass = val(kM1);  // kg
    link1.centerOfMass = core::SourcedValue<rw::math::Vector3D<double>>::provided(
        rw::math::Vector3D<double>(kC1, 0.0, 0.0), userProv());  // m（沿 x̂）
    link1.inertia = core::SourcedValue<rw::math::InertiaMatrix<double>>::provided(
        diagInertia(0.01, 0.01, 0.01), userProv());  // 质心系，kg·m²
    chain.links.push_back(link1);

    CanonicalLink link2;
    link2.objectId = l2;
    link2.localName = "arm_link2";
    link2.mass = val(kM2);  // kg
    link2.centerOfMass = core::SourcedValue<rw::math::Vector3D<double>>::provided(
        rw::math::Vector3D<double>(kC2, 0.0, 0.0), userProv());  // m（滑移向前）
    link2.inertia = core::SourcedValue<rw::math::InertiaMatrix<double>>::provided(
        diagInertia(0.001, 0.001, 0.001), userProv());  // 质心系，kg·m²
    chain.links.push_back(link2);

    return CanonicalModelBuilder()
        .setHeader(header)
        .setWorld(groundWorld())
        .setChain(chain)
        .setTools({})
        .build();
}

/**
 * 回归（F-580，P1）：移动关节内向递推质心力臂原直接用 comOffset（体原
 * 点→质心），丢失滑移臂 (d·z)×F 贡献——近端回转关节的静态重力矩差
 * m2·g·d（本例 3.924 N·m）。四通道共用同一错误力臂，五分项恒等式
 * （V-03）对此不可见，只有解析对照能暴露——本用例即解析对照钉。
 */
TEST(DynRnea, PrismaticSlipArmGravityMoment_F580)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-01"},
                  std::vector<std::string>{"AT-37"});

    const CanonicalModel model = makePrismArmModel();
    const ObjectId condition = idFrom<ObjectId>("cond-prism-static");
    constexpr double kSlipD = 0.4;  // 滑移量（m；bounds (0,1) 内）

    InverseDynRequest req = makeRequest(model, condition,
                                        {sampleAt(0.0, {0.0, kSlipD}, {0.0, 0.0}, {0.0, 0.0})});
    FakeContext ctx;
    const InverseDynOutcome out = InverseDynamicsEvaluator().evaluate(req, ctx);

    ASSERT_EQ(out.samples.size(), 2u);
    EXPECT_EQ(out.samples[0].jointIndex, 0u);
    EXPECT_EQ(out.samples[1].jointIndex, 1u);

    // 解析面：τ0＝−g·(m1·c1＋m2·(L1＋d＋c2))。修复前 τ0 丢 m2·g·d 项
    // （＝−kG·(m1·c1＋m2·(L1＋c2))），本断言即回归钉。
    const double expectedTau0 =
        -kG * (kM1 * kC1 + kM2 * (kL1 + kSlipD + kC2));  // N·m
    expectNearRel(out.samples[0].tauGravity, expectedTau0, "关节 0 静态重力矩（含滑移臂）");
    expectNearRel(out.samples[0].tauTotal, expectedTau0, "关节 0 静态总力矩");
    // 移动关节广义力＝沿轴内力（重力与 +X 轴正交）。
    EXPECT_NEAR(out.samples[1].tauTotal, 0.0, 1e-12);
    EXPECT_EQ(out.samples[0].numericState, SampleNumericState::Ok);
    EXPECT_EQ(out.validity.completeness, DynamicsValidity::Completeness::Complete);
}

// =====================================================================
// 用例 2：倒挂静态重力矩（AT-37 动力侧）——重力投影消费同一基座—世界
// 编译变换（R_world_base），符号翻转、量值相等，禁止任何下游二次旋转。
// 解析：倒挂 g_base＝R_x(π)ᵀ·g_world＝(0,0,+g) ⇒ τ1=+g(m1c1+m2(L1+c2))。
// =====================================================================

TEST(DynRnea, InvertedMountGravitySignFlipViaBaseProjection)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-22", "DYN-01"}, std::vector<std::string>{"AT-37"});
    // 本用例验证：同一模型（唯一差异＝安装姿态编译进 T_world_base）在倒挂
    // 工况下静态重力矩符号翻转、量值相等——重力入口只有 g_base 投影向量
    // （评估请求无 R_world_base 参数——下游二次旋转结构上不可表达）。
    const CanonicalModel ground = makeTwoLinkModel(groundWorld(), FrictionSpec{}, std::nullopt);
    const CanonicalModel inverted = makeTwoLinkModel(invertedWorld(), FrictionSpec{}, std::nullopt);
    const ObjectId condition = idFrom<ObjectId>("cond-inverted");

    // 投影值自检（消费同一编译变换的证据链：R 取自 model.world() 编译产物，
    // 投影经 runtime gravityToBase 规则单点——评估器外零第二投影实现）。
    const rw::math::Vector3D<double> gBaseInverted =
        sdurws::ird::runtime::gravityToBase(inverted.world().T_world_base.R(),
                                            inverted.world().gravityWorld);
    EXPECT_NEAR(gBaseInverted[0], 0.0, 1e-12);
    EXPECT_NEAR(gBaseInverted[1], 0.0, 1e-12);
    EXPECT_NEAR(gBaseInverted[2], kG, 1e-12);  // 倒挂：基座系重力沿 +Z（§6.5③）

    InverseDynRequest reqG = makeRequest(ground, condition,
                                         {sampleAt(0.0, {0.0, 0.0}, {0.0, 0.0}, {0.0, 0.0})});
    InverseDynRequest reqI = makeRequest(inverted, condition,
                                         {sampleAt(0.0, {0.0, 0.0}, {0.0, 0.0}, {0.0, 0.0})});
    FakeContext ctx;
    const InverseDynOutcome outG = InverseDynamicsEvaluator().evaluate(reqG, ctx);
    const InverseDynOutcome outI = InverseDynamicsEvaluator().evaluate(reqI, ctx);

    const double expectedTau1 = kG * (kM1 * kC1 + kM2 * (kL1 + kC2));  // N·m（倒挂＝正）
    expectNearRel(outI.samples[0].tauGravity, expectedTau1, "倒挂关节 1 静态重力矩");
    expectNearRel(outI.samples[1].tauGravity, kG * kM2 * kC2, "倒挂关节 2 静态重力矩");
    // 符号翻转与量值相等（AT-37：倒挂工况静态重力矩符号/量值正确）。
    EXPECT_NEAR(outI.samples[0].tauGravity, -outG.samples[0].tauGravity,
                kAnalyticRelTol * std::abs(expectedTau1));
    EXPECT_NEAR(outI.samples[1].tauGravity, -outG.samples[1].tauGravity,
                kAnalyticRelTol * std::abs(kG * kM2 * kC2));
    EXPECT_TRUE(outI.samples[0].conditionId == condition);
}

// =====================================================================
// 用例 3：静态重力矩含工具＋末端负载（DYN-02 末端负载输入；D-DYN-6
// 负载 com 缺失保守估算→estimated 强制标记＋DYN-PROPERTY-DOWNGRADED 素材）。
// 解析（负载挂 TCP＝法兰系 (d,0,0)，com 缺失取安装点）：
//   τ1 = −g·(m1c1 + m2(L1+c2) + (mt+mp)·d)；τ2 = −g·(m2c2 + (mt+mp)·d)
// =====================================================================

TEST(DynRnea, StaticGravityWithToolAndPayloadComposition)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-02", "DYN-06"}, std::vector<std::string>{});
    // 本用例验证：工具＋负载合成进末端体（平行轴）后的静态重力矩解析一致；
    // 负载 com 缺失走保守估算（D-DYN-6）且证据降级不包装精确（DYN-06）。
    constexpr double kMt = 1.2;    ///< 工具质量，kg
    constexpr double kD = 0.25;    ///< 法兰→TCP 平移（沿 +X），m
    constexpr double kMp = 0.8;    ///< 负载质量，kg
    const CanonicalModel model =
        makeTwoLinkModel(groundWorld(), FrictionSpec{}, ToolSpec{kMt, true, 0.0, kD, false});
    const ObjectId condition = idFrom<ObjectId>("cond-payload");

    EndEffectorPayload payload;
    payload.objectId = idFrom<ObjectId>("payload-a");
    payload.mass = val(kMp);           // kg（必填）
    // centerOfMass/inertia 保持 NotProvided——保守估算路径（com→安装点）。
    payload.initiallyMounted = true;   // 初始负载（§5.4 基线变体）
    InverseDynRequest req = makeRequest(model, condition,
                                        {sampleAt(0.0, {0.0, 0.0}, {0.0, 0.0}, {0.0, 0.0})});
    req.payloads.push_back(payload);

    FakeContext ctx;
    const InverseDynOutcome out = InverseDynamicsEvaluator().evaluate(req, ctx);
    ASSERT_EQ(out.samples.size(), 2u);

    // 解析（力臂区分）：工具/负载挂 TCP——对关节 1 的力臂＝L1+d（TCP 距
    // 关节 1），对关节 2 的力臂＝d（TCP 距关节 2）；负载 com 缺失取安装点
    // ＝同力臂（D-DYN-6 保守估算不改变挂载位置）。
    const double endOffsetJ1 = kM2 * (kL1 + kC2) + (kMt + kMp) * (kL1 + kD);  // kg·m
    expectNearRel(out.samples[0].tauGravity, -kG * (kM1 * kC1 + endOffsetJ1),
                  "含工具负载的关节 1 静态重力矩");
    expectNearRel(out.samples[1].tauGravity, -kG * (kM2 * kC2 + (kMt + kMp) * kD),
                  "含工具负载的关节 2 静态重力矩");

    // 保守估算标记（D-DYN-6 强制 estimated）＋降级素材（DYN-06）。
    EXPECT_EQ(out.validity.estimatedPayloadCount, 1u);  // 负载 com/inertia 双缺失——计 1 件
    EXPECT_TRUE(hasDiagCode(out, std::string{sdurws::ird::dynamics::kDynPropertyDowngraded}));
    EXPECT_EQ(out.validity.completeness, DynamicsValidity::Completeness::Complete);
}

// =====================================================================
// 用例 4：分项通道与动力学模型解析对照（DYN-01"关节空间精确含全部耦合
// 项"——M(q)q̈ 与科氏/离心解析式逐项核对＋五分项恒等式）。
// 平面二连杆（轴沿 +Y）拉格朗日基准：
//   M11 = m1c1²+I1y+m2(L1²+c2²+2L1c2·cos q2)+I2y；M12 = m2(c2²+L1c2·cos q2)+I2y；
//   M22 = m2c2²+I2y；τ_inertia = M(q)·q̈
//   τ_coriolis = [−h(2q̇1q̇2+q̇2²), +h·q̇1²]，h = m2L1c2·sin q2
//   τ_gravity = [−g(m1c1cos q1 + m2(L1cos q1 + c2cos(q1+q2))), −g·m2c2cos(q1+q2)]
// =====================================================================

TEST(DynRnea, SplitChannelsMatchAnalyticModelAndSumIdentity)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-01", "DYN-02"}, std::vector<std::string>{});
    // 本用例验证：惯性通道＝M(q)·q̈（全耦合质量阵——含 M12 交叉项）、科氏
    // 通道含 q̇ 二次交叉项（不对角化）、重力通道与位形相关解析式一致，且
    // 五分项之和恒等于总力矩（§5.2 恒等式——黄金算例校验分项可加性）。
    const FrictionSpec fric{true, 0.05, 0.10, 0.01};  // fv/fc/bias（N·m·s/rad、N·m、N·m）
    const CanonicalModel model = makeTwoLinkModel(groundWorld(), fric, std::nullopt);
    const ObjectId condition = idFrom<ObjectId>("cond-dyn");

    // 非平凡位形/速度/加速度（rad、rad/s、rad/s²——sin/cos 项全部非零）。
    const double q1 = 0.3, q2 = 0.9, w1 = 0.7, w2 = -1.2, a1 = 1.5, a2 = -0.8;
    InverseDynRequest req =
        makeRequest(model, condition, {sampleAt(0.0, {q1, q2}, {w1, w2}, {a1, a2})});
    FakeContext ctx;
    const InverseDynOutcome out = InverseDynamicsEvaluator().evaluate(req, ctx);
    ASSERT_EQ(out.samples.size(), 2u);

    // —— 惯性通道：M(q)·q̈（解析）——
    double m11 = 0.0, m12 = 0.0, m22 = 0.0;
    analyticMassMatrix(q2, &m11, &m12, &m22);
    expectNearRel(out.samples[0].tauInertia, m11 * a1 + m12 * a2, "M11·a1+M12·a2（惯性τ1）");
    expectNearRel(out.samples[1].tauInertia, m12 * a1 + m22 * a2, "M21·a1+M22·a2（惯性τ2）");

    // —— 科氏/离心通道：q̇ 二次交叉项（解析）——
    const double h = kM2 * kL1 * kC2 * std::sin(q2);  // kg·m²（离心耦合系数）
    expectNearRel(out.samples[0].tauCoriolisCentrifugal, -h * (2.0 * w1 * w2 + w2 * w2),
                  "科氏τ1（含 2q̇1q̇2 交叉项）");
    expectNearRel(out.samples[1].tauCoriolisCentrifugal, h * w1 * w1, "离心τ2（q̇1² 项）");

    // —— 重力通道：位形相关解析式 ——
    const double gTau1 =
        -kG * (kM1 * kC1 * std::cos(q1) + kM2 * (kL1 * std::cos(q1) + kC2 * std::cos(q1 + q2)));
    const double gTau2 = -kG * kM2 * kC2 * std::cos(q1 + q2);
    expectNearRel(out.samples[0].tauGravity, gTau1, "重力τ1（位形相关）");
    expectNearRel(out.samples[1].tauGravity, gTau2, "重力τ2（位形相关）");

    // —— 摩擦通道：fv·q̇ + fc·sgn₀(q̇) + bias ——
    const double expFric1 = 0.05 * w1 + 0.10 * (w1 > 0 ? 1.0 : -1.0) + 0.01;
    const double expFric2 = 0.05 * w2 + 0.10 * (w2 > 0 ? 1.0 : -1.0) + 0.01;
    expectNearRel(out.samples[0].tauFriction, expFric1, "摩擦τ1");
    expectNearRel(out.samples[1].tauFriction, expFric2, "摩擦τ2");

    // —— 五分项恒等式（§5.2——分项可加性逐样本成立；全摩擦模型→无缺失标记）。
    for (int j = 0; j < 2; ++j) {
        const DynamicsSample& row = out.samples[j];
        const double sum = row.tauGravity + row.tauInertia + row.tauCoriolisCentrifugal
                         + row.tauFriction + row.tauExternal;
        EXPECT_NEAR(row.tauTotal, sum, kAnalyticRelTol * std::max(1.0, std::abs(row.tauTotal)))
            << "关节 " << j << " 五分项恒等式";
    }
    EXPECT_FALSE(out.validity.frictionMissing);  // 全 Provided——无降级
    EXPECT_EQ(out.validity.completeness, DynamicsValidity::Completeness::Complete);
}

// =====================================================================
// 用例 5：摩擦符号约定与零速库仑项（DYN-02/MDL-16；§5.5 sgn₀(0)=0——
// 静摩擦不在 R1 模型，黄金算例覆盖 q̇=0 行为）。
// =====================================================================

TEST(DynRnea, FrictionSignConventionAndZeroSpeedCoulomb)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-02", "MDL-16"}, std::vector<std::string>{});
    // 本用例验证：τ_fric = fv·q̇ + fc·sgn₀(q̇) + bias 的三分支符号行为——
    // q̇>0 取 +fc、q̇<0 取 −fc、q̇=0 库仑项为零（仅偏置＋黏性零贡献）。
    const FrictionSpec fric{true, 0.5, 2.0, 0.1};  // fv/fc/bias 单位同 §5.5
    const CanonicalModel model = makeTwoLinkModel(groundWorld(), fric, std::nullopt);
    const ObjectId condition = idFrom<ObjectId>("cond-friction");

    InverseDynRequest req = makeRequest(
        model, condition,
        {sampleAt(0.0, {0.0, 0.0}, {0.0, 0.0}, {0.0, 0.0}),    // 静止
         sampleAt(0.1, {0.0, 0.0}, {1.0, -2.0}, {0.0, 0.0})}); // 正/反向运动（rad/s）
    FakeContext ctx;
    const InverseDynOutcome out = InverseDynamicsEvaluator().evaluate(req, ctx);
    ASSERT_EQ(out.samples.size(), 4u);  // 2 时刻 × 2 关节

    // 静止行（t=0）：sgn₀(0)=0 ⇒ τ_fric = bias（库仑/黏性均为零贡献）。
    EXPECT_DOUBLE_EQ(out.samples[0].tauFriction, 0.1);  // bias（N·m）
    EXPECT_DOUBLE_EQ(out.samples[1].tauFriction, 0.1);
    // 运动行（t=0.1）：q̇>0 → +fc；q̇<0 → −fc（黏性项随符号）。
    EXPECT_NEAR(out.samples[2].tauFriction, 0.5 * 1.0 + 2.0 + 0.1, 1e-12);   // N·m
    EXPECT_NEAR(out.samples[3].tauFriction, 0.5 * (-2.0) - 2.0 + 0.1, 1e-12);
    // 静止行总力矩＝重力＋偏置（静态＋偏置摩擦的恒等面）。
    const double grav1 = -kG * (kM1 * kC1 + kM2 * (kL1 + kC2));
    EXPECT_NEAR(out.samples[0].tauTotal, grav1 + 0.1, 1e-9);
}

// =====================================================================
// 用例 6：摩擦参数缺失→DYN-06 降级素材与可信性标记（MDL-16 M-8 闭环——
// 未填写标记 DataInsufficient 素材；分项值按 0 计入、证据不包装精确）。
// =====================================================================

TEST(DynRnea, FrictionMissingTriggersDyn06Material)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-16", "DYN-06"}, std::vector<std::string>{});
    // 本用例验证：模型摩擦三元组全缺（MDL-16 未填写）时——评估继续（分项
    // 按 0）、validity.frictionMissing=true、诊断含 DYN-FRICTION-MISSING
    // 素材且逐关节可定位（subject＝关节对象 ID）。
    const CanonicalModel model = makeTwoLinkModel(groundWorld(), FrictionSpec{}, std::nullopt);
    const ObjectId condition = idFrom<ObjectId>("cond-fric-missing");
    InverseDynRequest req =
        makeRequest(model, condition, {sampleAt(0.0, {0.1, -0.2}, {0.3, 0.4}, {0.0, 0.0})});
    FakeContext ctx;
    const InverseDynOutcome out = InverseDynamicsEvaluator().evaluate(req, ctx);

    ASSERT_EQ(out.samples.size(), 2u);
    EXPECT_EQ(out.samples[0].tauFriction, 0.0);   // 全缺→分项按 0（数值继续）
    EXPECT_EQ(out.samples[1].tauFriction, 0.0);
    EXPECT_TRUE(out.validity.frictionMissing);    // DYN-06 降级标记
    EXPECT_TRUE(hasDiagCode(out, std::string{sdurws::ird::dynamics::kDynFrictionMissing}));
    // 逐关节可定位（ERR-01：subject 绑定关节对象 ID）。
    int frictionDiags = 0;
    for (const DiagnosticRecord& d : out.diagnostics) {
        if (d.code == std::string{sdurws::ird::dynamics::kDynFrictionMissing}) {
            ++frictionDiags;
            EXPECT_TRUE(d.subject.has_value());
        }
    }
    EXPECT_EQ(frictionDiags, 2);  // 两关节各一条
    EXPECT_EQ(out.validity.completeness, DynamicsValidity::Completeness::Complete);
}

// =====================================================================
// 用例 7：负载事件时间线与模型变体（§5.4——Grasp/Release 切换；事件落在
// 采样间隙→下一样本起生效〔保守边界〕；payloadVariantIndex 随样本可审计）。
// =====================================================================

TEST(DynRnea, PayloadEventTimelineVariants)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-02", "DYN-07"}, std::vector<std::string>{});
    // 本用例验证：Grasp@0.5 s 在 t≥0.5 的样本生效（事件恰落样本即生效——
    // 保守上界"不晚于其后首样本"）；Release@0.8 s 移除；变体索引随事件数
    // 递增；挂载态改变总力矩（负载力臂跳变）。
    constexpr double kMp = 0.6;   ///< 负载质量，kg
    constexpr double kD = 0.25;   ///< TCP 力臂（沿 +X），m
    const CanonicalModel model =
        makeTwoLinkModel(groundWorld(), FrictionSpec{}, ToolSpec{0.5, true, 0.0, kD, false});
    const ObjectId condition = idFrom<ObjectId>("cond-events");

    EndEffectorPayload payload;
    payload.objectId = idFrom<ObjectId>("payload-ev");
    payload.mass = val(kMp);
    payload.initiallyMounted = false;  // 非初始负载——由 Grasp 事件引入
    InverseDynRequest req = makeRequest(
        model, condition,
        {sampleAt(0.0, {0.0, 0.0}, {0.0, 0.0}, {0.0, 0.0}),    // 未挂载
         sampleAt(0.4, {0.0, 0.0}, {0.0, 0.0}, {0.0, 0.0}),    // 未挂载（间隙内事件未到）
         sampleAt(0.5, {0.0, 0.0}, {0.0, 0.0}, {0.0, 0.0}),    // Grasp@0.5 生效
         sampleAt(0.8, {0.0, 0.0}, {0.0, 0.0}, {0.0, 0.0}),    // Release@0.8 生效
         sampleAt(1.0, {0.0, 0.0}, {0.0, 0.0}, {0.0, 0.0})});  // 释放后保持
    req.payloads.push_back(payload);
    req.events.push_back(PayloadEvent{0.5, PayloadEvent::Kind::Grasp, 0});
    req.events.push_back(PayloadEvent{0.8, PayloadEvent::Kind::Release, 0});

    FakeContext ctx;
    const InverseDynOutcome out = InverseDynamicsEvaluator().evaluate(req, ctx);
    ASSERT_EQ(out.samples.size(), 10u);  // 5 时刻 × 2 关节

    // 变体索引审计（§4.4 payloadVariantIndex；0=基线，每次事件切换＋1）。
    // 行序＝每时刻 2 关节行：samples[2k]＝t_k 关节 0、samples[2k+1]＝t_k 关节 1。
    EXPECT_EQ(out.samples[0].payloadVariantIndex, 0u);   // t=0.0（基线）
    EXPECT_EQ(out.samples[2].payloadVariantIndex, 0u);   // t=0.4（事件在间隙——未生效）
    EXPECT_EQ(out.samples[4].payloadVariantIndex, 1u);   // t=0.5（Grasp 生效）
    EXPECT_EQ(out.samples[6].payloadVariantIndex, 2u);   // t=0.8（Release 生效）
    EXPECT_EQ(out.samples[8].payloadVariantIndex, 2u);   // t=1.0（无事件——保持）

    // 关节 2（奇数行）力矩跳变：未挂载 −g·(m2c2+mt·d) → 挂载
    // −g·(m2c2+(mt+mp)·d) → 释放还原（工具 0.5 kg 恒挂载——两侧同含）。
    const double tau2Base = -kG * (kM2 * kC2 + 0.5 * kD);
    const double tau2Loaded = -kG * (kM2 * kC2 + (0.5 + kMp) * kD);
    EXPECT_NEAR(out.samples[3].tauTotal, tau2Base, 1e-9);      // t=0.4 未挂
    EXPECT_NEAR(out.samples[5].tauTotal, tau2Loaded, 1e-9);    // t=0.5 挂载
    EXPECT_NEAR(out.samples[7].tauTotal, tau2Base, 1e-9);      // t=0.8 释放
    EXPECT_NEAR(out.samples[9].tauTotal, tau2Base, 1e-9);      // t=1.0 保持
    EXPECT_EQ(out.validity.completeness, DynamicsValidity::Completeness::Complete);
}

// =====================================================================
// 用例 8：外力项恒零（P-DYN-3——ExternalWrench R1 恒 NotApplicable，
// 不伪造零以外的值；样本行/诊断双面一致）。
// =====================================================================

TEST(DynRnea, ExternalWrenchConstantlyZeroNotApplicable)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-02"}, std::vector<std::string>{});
    // 本用例验证：任意非平凡运动样本的 tauExternal 均为 0 且数值状态正常
    // （外力通道 R1 预留——REQ-04 无外力字段〔P-DYN-3 待裁决〕，评估面
    // 不虚构外力输入，也不产生外力相关诊断）。
    const CanonicalModel model =
        makeTwoLinkModel(groundWorld(), FrictionSpec{true, 0.1, 0.1, 0.02}, std::nullopt);
    const ObjectId condition = idFrom<ObjectId>("cond-ext");
    InverseDynRequest req = makeRequest(
        model, condition, {sampleAt(0.0, {0.5, -0.3}, {1.0, -1.0}, {2.0, 1.0})});
    FakeContext ctx;
    const InverseDynOutcome out = InverseDynamicsEvaluator().evaluate(req, ctx);

    ASSERT_EQ(out.samples.size(), 2u);
    EXPECT_DOUBLE_EQ(out.samples[0].tauExternal, 0.0);
    EXPECT_DOUBLE_EQ(out.samples[1].tauExternal, 0.0);
    EXPECT_EQ(out.samples[0].numericState, SampleNumericState::Ok);
    EXPECT_FALSE(hasDiagCode(out, std::string{sdurws::ird::dynamics::kDynNonFinite}));
}

// =====================================================================
// 用例 9：维度不匹配 fail-fast（§5.6"输入维度不匹配：评估终止"——调用方
// 错误轨，异常携带实际/期望比较数据；DYN-DIMENSION-MISMATCH 语义）。
// =====================================================================

TEST(DynRnea, DimensionMismatchFailFast)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-01"}, std::vector<std::string>{});
    // 本用例验证：轨迹关节数≠模型可动关节数（2）时 evaluate 抛 DynamicsError
    // fail-fast、不产出半成品序列；message 携带维度比较（§10.0 调用方错误）。
    const CanonicalModel model = makeTwoLinkModel(groundWorld(), FrictionSpec{}, std::nullopt);
    InverseDynRequest req = makeRequest(model, idFrom<ObjectId>("cond-dim"),
                                        {sampleAt(0.0, {0.1}, {0.0}, {0.0})});  // 1 列≠2
    FakeContext ctx;
    try {
        const InverseDynOutcome out = InverseDynamicsEvaluator().evaluate(req, ctx);
        (void)out;
        FAIL() << "维度不匹配未抛出 DynamicsError";
    } catch (const DynamicsError& e) {
        EXPECT_NE(std::string(e.what()).find("input-invalid"), std::string::npos)
            << "token 前缀缺失：" << e.what();
        EXPECT_NE(std::string(e.what()).find("2"), std::string::npos)
            << "比较数据（期望维度）缺失：" << e.what();
    }
}

// =====================================================================
// 用例 10：时间非严格递增 fail-fast（§4.6——时间重复/倒退/零间隔拒绝；
// 评估器不排序修复、不插值抹平；DYN-SERIES-NON-MONOTONIC 语义）。
// =====================================================================

TEST(DynRnea, NonMonotonicTimeFailFast)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-01"}, std::vector<std::string>{});
    // 本用例验证：相邻样本 t 相等（零间隔）即拒绝——重复/倒退同轨（<0 比较同式）。
    const CanonicalModel model = makeTwoLinkModel(groundWorld(), FrictionSpec{}, std::nullopt);
    InverseDynRequest req = makeRequest(
        model, idFrom<ObjectId>("cond-mono"),
        {sampleAt(0.0, {0.0, 0.0}, {0.0, 0.0}, {0.0, 0.0}),
         sampleAt(0.0, {0.1, 0.0}, {0.0, 0.0}, {0.0, 0.0})});  // 零间隔（重复）
    FakeContext ctx;
    EXPECT_THROW(InverseDynamicsEvaluator().evaluate(req, ctx), DynamicsError);
}

// =====================================================================
// 用例 11：空模型/空工况 fail-fast（§10.0 身份要求行——缺身份拒绝评估；
// §10.1 前置"工况集非空"）。
// =====================================================================

TEST(DynRnea, NullModelAndEmptyConditionFailFast)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-01"}, std::vector<std::string>{});
    // 本用例验证：模型空指针与工况对象 ID 空两个调用方违约面——各自独立
    // fail-fast（身份块不完整不进入正式评估）。
    const CanonicalModel model = makeTwoLinkModel(groundWorld(), FrictionSpec{}, std::nullopt);
    FakeContext ctx;
    {
        InverseDynRequest req = makeRequest(model, idFrom<ObjectId>("cond-x"),
                                            {sampleAt(0.0, {0.0, 0.0}, {0.0, 0.0}, {0.0, 0.0})});
        req.model = nullptr;
        EXPECT_THROW(InverseDynamicsEvaluator().evaluate(req, ctx), DynamicsError);
    }
    {
        InverseDynRequest req =
            makeRequest(model, ObjectId{}, {sampleAt(0.0, {0.0, 0.0}, {0.0, 0.0}, {0.0, 0.0})});
        EXPECT_THROW(InverseDynamicsEvaluator().evaluate(req, ctx), DynamicsError);
    }
}

// =====================================================================
// 用例 12：非有限输入样本级标记（NFR-COR-03——不静默转 0；评估继续，
// 统计标注非完备；DYN-NON-FINITE 素材可定位）。
// =====================================================================

TEST(DynRnea, NonFiniteInputMarkedAndEvaluationContinues)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-03", "DYN-01"}, std::vector<std::string>{});
    // 本用例验证：q̇=NaN 时**整样本**标记 NonFiniteInput（§4.6 样本级语义
    // ——耦合项传播使逐关节分离评估无意义；力矩域 NaN 不转 0）、后续样本
    // 继续（样本级素材不阻断序列）、validity.nonFiniteCount 按样本时刻
    // 计数且 completeness=Partial、素材可定位（subject＝首个非有限关节）。
    const CanonicalModel model =
        makeTwoLinkModel(groundWorld(), FrictionSpec{true, 0.1, 0.1, 0.02}, std::nullopt);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    InverseDynRequest req = makeRequest(
        model, idFrom<ObjectId>("cond-nan"),
        {sampleAt(0.0, {0.0, 0.0}, {nan, 0.5}, {0.0, 0.0}),    // 样本 0：关节 0 速度非有限
         sampleAt(0.1, {0.1, 0.1}, {0.2, 0.3}, {0.0, 0.0})});  // 样本 1：正常（评估继续）
    FakeContext ctx;
    const InverseDynOutcome out = InverseDynamicsEvaluator().evaluate(req, ctx);

    ASSERT_EQ(out.samples.size(), 4u);  // 全部样本行保留（不截断伪造）
    EXPECT_EQ(out.samples[0].numericState, SampleNumericState::NonFiniteInput);
    EXPECT_EQ(out.samples[1].numericState, SampleNumericState::NonFiniteInput);  // 整样本标记
    EXPECT_TRUE(std::isnan(out.samples[0].tauTotal));   // 不静默转 0
    EXPECT_TRUE(std::isnan(out.samples[1].tauTotal));
    EXPECT_EQ(out.samples[2].numericState, SampleNumericState::Ok);  // 后续样本继续
    EXPECT_TRUE(std::isfinite(out.samples[2].tauTotal));
    EXPECT_EQ(out.validity.nonFiniteCount, 1u);  // 按样本时刻计数
    EXPECT_EQ(out.validity.completeness, DynamicsValidity::Completeness::Partial);
    EXPECT_TRUE(hasDiagCode(out, std::string{sdurws::ird::dynamics::kDynNonFinite}));
    // 素材 subject＝首个非有限关节（关节 0 对象 ID——定位语义）。
    for (const DiagnosticRecord& d : out.diagnostics) {
        if (d.code == std::string{sdurws::ird::dynamics::kDynNonFinite}) {
            EXPECT_TRUE(d.subject.has_value()
                        && *d.subject == model.chain().joints[0].objectId);
        }
    }
}

// =====================================================================
// 用例 13：关节侧结果与候选传动无关（DYN-04 边界——映射归 drivetrain；
// 逐位相同的链/物性/轨迹，仅传动比不同→关节侧序列逐位相同）。
// =====================================================================

TEST(DynRnea, JointSideResultIndependentOfDrivetrain)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-04"}, std::vector<std::string>{});
    // 本用例验证：RNEA 输出只依赖模型＋轨迹＋工况——drivetrain 传动比
    // 变化（候选传动更换）不改变任何关节侧行（DYN-04"输出与候选传动
    // 无关"；映射归 WP-18-T03 的 DriveTrainMappingEvaluator，本域零消费）。
    const std::vector<double> motionQ = {0.2, 0.4};
    const std::vector<double> motionQd = {0.5, -0.3};
    const std::vector<double> motionQdd = {1.0, 0.8};
    const CanonicalModel ratioA = makeTwoLinkModel(groundWorld(), FrictionSpec{true, 0.2, 0.3, 0.01},
                                                   std::nullopt);                       // 全缺省
    const CanonicalModel ratioB = makeTwoLinkModel(groundWorld(), FrictionSpec{true, 0.2, 0.3, 0.01},
                                                   std::nullopt, {2.0, 3.0});           // 候选传动
    const ObjectId condition = idFrom<ObjectId>("cond-dyn04");
    InverseDynRequest reqA = makeRequest(
        ratioA, condition,
        {sampleAt(0.0, {0.0, 0.0}, {0.0, 0.0}, {0.0, 0.0}),
         sampleAt(0.1, motionQ, motionQd, motionQdd)});
    InverseDynRequest reqB = reqA;
    reqB.model = &ratioB;

    FakeContext ctx;
    const InverseDynOutcome outA = InverseDynamicsEvaluator().evaluate(reqA, ctx);
    const InverseDynOutcome outB = InverseDynamicsEvaluator().evaluate(reqB, ctx);

    ASSERT_EQ(outA.samples.size(), outB.samples.size());
    for (std::size_t k = 0; k < outA.samples.size(); ++k) {
        EXPECT_DOUBLE_EQ(outA.samples[k].tauTotal, outB.samples[k].tauTotal)
            << "关节侧行 " << k << " 受候选传动影响（DYN-04 红线）";
        EXPECT_DOUBLE_EQ(outA.samples[k].tauGravity, outB.samples[k].tauGravity);
        EXPECT_DOUBLE_EQ(outA.samples[k].q, outB.samples[k].q);
    }
}

// =====================================================================
// 用例 14：取消语义（§9.1.5/UX-03——取消＝非错误；部分产出＋cancelled
// 位；零错误诊断）。
// =====================================================================

TEST(DynRnea, CancelReturnsPartialWithoutError)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-03"}, std::vector<std::string>{});
    // 本用例验证：宿主预置取消请求→评估在首样本前返回空产出、cancelled=true、
    // completeness=Empty；模型全物性/摩擦齐备（隔离降级素材面）时诊断清单
    // 为空——取消非错误、零错误诊断（UX-03）。
    const CanonicalModel model =
        makeTwoLinkModel(groundWorld(), FrictionSpec{true, 0.1, 0.1, 0.02}, std::nullopt);
    InverseDynRequest req = makeRequest(
        model, idFrom<ObjectId>("cond-cancel"),
        {sampleAt(0.0, {0.0, 0.0}, {0.0, 0.0}, {0.0, 0.0}),
         sampleAt(0.1, {0.0, 0.0}, {0.0, 0.0}, {0.0, 0.0})});
    FakeContext ctx;
    ctx.mCancelled = true;
    const InverseDynOutcome out = InverseDynamicsEvaluator().evaluate(req, ctx);

    EXPECT_TRUE(out.cancelled);
    EXPECT_TRUE(out.samples.empty());
    EXPECT_EQ(out.validity.completeness, DynamicsValidity::Completeness::Empty);
    EXPECT_TRUE(out.diagnostics.empty());  // 取消非错误——零诊断（模型面无降级素材）
}
