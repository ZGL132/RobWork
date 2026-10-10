/**
 * @file   ForwardDynamicsTest.cpp
 * @brief  WP-17-T05 用例组（DynForward）——正动力学响应一致性检查面
 *         （DYN-05）：解析黄金算例（伸直构型匀加速——τ_ref 恒定、RK4 对
 *         二次多项式轨迹精确，误差达机器精度）、四态判定（Passed/Failed/
 *         NotRun/NotApplicable）、异常检测（发散 DYN-FD-DIVERGED、质量阵
 *         奇异 DYN-FD-NUMERIC-ANOMALY）、共源强制（负载事件变体两路同步、
 *         τ_ref 侧诊断透传）、确定性复现与 fail-fast 前置边界。
 *
 * 设计依据：
 *   - units/dynamics.md §6.1（时序图①→⑤——用例逐步对应）、§6.2/§6.3
 *     （输入前置与六条必须明确）、§10.2（契约表——合法/非法示例行）、
 *     §9.4（DYN-FD-* 码）、§11.2（V-13 一致性/V-14 初始状态缺失/V-16
 *     积分不收敛——本文件为其执行证明面）
 *   - 需求 DYN-05（明确控制输入、初始状态、步长和容差的正动力学场景——
 *     响应一致性检查和异常检测）
 *
 * 解析黄金算例（本文件核心对照——手工推导，附录 D C7"测试对照容差与产
 *   品容差分离"）：
 *   平面二连杆伸直构型（q2≡0、q̇2=q̈2=0、零重力、零摩擦、匀加速激励
 *   q1(t)=q₀+v₀t+½at²）下，链退化为绕关节 1 的单刚体转动：
 *     τ_ref = [M11·a, M21·a]（恒定——M 通道为常数，见逐行推导注释），
 *   采样保持（区间内 τ_ctrl 常量）零误差；固定步长 RK4 对不超过 4 阶的
 *   多项式轨迹逐位精确 → q_sim(t) 解析重现 q_ref(t) → 误差达双精度机器
 *   精度量级（<1e-12 rad）。断言 toleranceQ=1e-9 判 Passed＋独立上界
 *   maxErrQ<1e-9（解析界——不依赖产品判定逻辑的对照）。
 *
 * 数值对照口径（附录 D C7）：产品侧容差只经 ForwardCheckSettings 注入
 *   （分析配置来源）；本文件的期望界（1e-9/1e-12 等）全部为测试对照容差，
 *   与产品代码零共享。
 */

#include <sdurws/ird/core/Provenance.hpp>              // SourcedValue/ValueProvenance（负载注入）
#include <sdurws/ird/dynamics/DiagCodes.hpp>
#include <sdurws/ird/dynamics/DynTypes.hpp>
#include <sdurws/ird/dynamics/Errors.hpp>
#include <sdurws/ird/dynamics/ForwardDynamics.hpp>
#include <sdurws/ird/dynamics/InverseDynamics.hpp>
#include <sdurws/ird/evidence/Evaluator.hpp>          // IEvaluationContext（取消/进度宿主）
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "TwoLinkFixture.hpp"

using namespace sdurws::ird::dynamics::testfixture;

using sdurws::ird::core::ObjectId;
using sdurws::ird::dynamics::DynamicsValidity;
using sdurws::ird::dynamics::DynamicsError;
using sdurws::ird::dynamics::EndEffectorPayload;
using sdurws::ird::dynamics::ForwardCheckOutcome;
using sdurws::ird::dynamics::ForwardCheckRequest;
using sdurws::ird::dynamics::ForwardCheckSettings;
using sdurws::ird::dynamics::ForwardDynamicsValidator;
using sdurws::ird::dynamics::InverseDynOutcome;
using sdurws::ird::dynamics::InverseDynRequest;
using sdurws::ird::dynamics::InverseDynSampleInput;
using sdurws::ird::dynamics::InverseDynamicsEvaluator;
using sdurws::ird::dynamics::PayloadEvent;
using sdurws::ird::dynamics::kForwardIntegratorToken;
// DYN-* 码值常量（DiagCodes.hpp 唯一书写点——素材断言共用，禁字符串拼码）。
using sdurws::ird::dynamics::kDynFdConsistencyFailed;
using sdurws::ird::dynamics::kDynFdDiverged;
using sdurws::ird::dynamics::kDynFdInitialStateMissing;
using sdurws::ird::dynamics::kDynFdNumericAnomaly;
using sdurws::ird::dynamics::kDynFrictionMissing;
using sdurws::ird::dynamics::kDynRneaFailed;

/// runtime 命名空间别名（全局作用域下的限定名解析——模型/链类型消费面；
/// 与夹具内命名空间相对解析不同，全局作用域必须显式别名）。
namespace runtime = sdurws::ird::runtime;
namespace core = sdurws::ird::core;

namespace {

// =====================================================================
// 本地替身与助手（同 RneaTest.cpp/StatisticsTest.cpp 形态——替身只承载
// 对话形状，不伪造任何评估逻辑；全部断言针对真实产品代码）。
// =====================================================================

/// 宿主上下文本地替身（evidence 卡口径——取消/进度宿主面）。
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

/// 黄金算例激励参数（伸直构型匀加速——解析推导见文件头；单位逐项注明）。
constexpr double kQ0 = 0.1;        ///< 初始关节角 q₁(0)，单位 rad
constexpr double kV0 = 0.3;        ///< 初始关节角速度 q̇₁(0)，单位 rad/s
constexpr double kAcc = 0.5;       ///< 恒定关节加速度 q̈₁，单位 rad/s²
constexpr double kSampleDt = 0.01; ///< 参考样本间隔，单位 s
constexpr int kSampleCount = 51;   ///< 样本数（0.5 s 时长——51 时刻 50 区间）
constexpr double kMaxStep = 1e-3;  ///< 积分最大步长，单位 s（每区间 10 子步）

/// 标准检查配置（黄金算例面——toleranceQ/Qd 由用例按解析界注入）。
ForwardCheckSettings goldenSettings(double tolQ, double tolQd)
{
    ForwardCheckSettings s;
    s.mode = ForwardCheckSettings::Mode::Standard;
    s.integratorToken = std::string{kForwardIntegratorToken};  // 词表锁定值
    s.maxStepS = kMaxStep;         // s
    s.relTolerance = 1e-6;         // 无量纲（记录性分量——固定步长实现无消费点）
    s.toleranceQ = tolQ;           // rad（用例注入）
    s.toleranceQd = tolQd;         // rad/s（用例注入）
    return s;
}

/// 黄金激励段（伸直构型匀加速：q₁ 匀加速、q₂≡0 恒定——τ_ref 恒定的构型
/// 条件，见文件头推导；t 严格递增、维度＝2 可动关节）。
std::vector<InverseDynSampleInput> goldenTrajectory()
{
    std::vector<InverseDynSampleInput> samples;
    samples.reserve(static_cast<std::size_t>(kSampleCount));
    for (int j = 0; j < kSampleCount; ++j) {
        const double t = j * kSampleDt;              // 样本时刻，单位 s
        InverseDynSampleInput s;
        s.t = t;
        s.segmentIndex = 0;                          // 单段黄金轨迹
        s.q = {kQ0 + kV0 * t + 0.5 * kAcc * t * t, 0.0};   // rad（q₂≡0 伸直）
        s.qd = {kV0 + kAcc * t, 0.0};                // rad/s
        s.qdd = {kAcc, 0.0};                         // rad/s²（恒定加速度激励）
        samples.push_back(std::move(s));
    }
    return samples;
}

/// 黄金请求组装（零重力＋零摩擦二连杆——解析算例的模型/输入面）。
ForwardCheckRequest goldenRequest(double tolQ, double tolQd)
{
    ForwardCheckRequest r;
    r.conditionId = idFrom<ObjectId>("golden-fd-cond");
    r.model = nullptr;  // 由调用方填模型（makeTwoLinkModel 产物——各用例自持）
    r.gravityBase[0] = 0.0;  // 基座系重力 X 分量，单位 m/s²（零重力黄金算例）
    r.gravityBase[1] = 0.0;  // 基座系重力 Y 分量，单位 m/s²
    r.gravityBase[2] = 0.0;  // 基座系重力 Z 分量，单位 m/s²（DYN-05"控制输入/
                             //   初始状态"的解析可控前提——重力项为零）
    r.samples = goldenTrajectory();
    r.forwardCheck = goldenSettings(tolQ, tolQd);
    return r;
}

/**
 * 全零物性二连杆模型（数值异常算例专用——连杆质量/质心/惯量全部
 * NotProvided 的合法模型：§5.5 降级语义不拒绝评估，但质量阵恒为零）。
 * 与 makeTwoLinkModel 同构（R-2 夹具直构先例），仅物性赋值段留空。
 */
inline runtime::CanonicalModel makeZeroPropertyTwoLinkModel()
{
    // 身份/闭包/几何段与夹具同参（确定性 id 派生规则同源）。
    runtime::CanonicalModelHeader header;
    header.project = idFrom<core::ProjectId>("prj-dyn-zero");
    header.branch = idFrom<core::BranchId>("brn-dyn-zero");
    header.revision = idFrom<core::RevisionId>("rev-dyn-zero-1");
    header.revisionSeq = 1;
    header.descriptionContractVersion = 1;
    header.compilerContractVersion = 1;
    header.builtFrom = digestOf("zero-prop-two-link-bytes");

    const core::ObjectId robot = idFrom<core::ObjectId>("zero-robot");
    const core::ObjectId j1 = idFrom<core::ObjectId>("zero-j1");
    const core::ObjectId j2 = idFrom<core::ObjectId>("zero-j2");
    const core::ObjectId l0 = idFrom<core::ObjectId>("zero-l0");
    const core::ObjectId l1 = idFrom<core::ObjectId>("zero-l1");
    const core::ObjectId l2 = idFrom<core::ObjectId>("zero-l2");
    auto addRef = [&header](const core::ObjectId& id, const char* seed, const char* token) {
        runtime::ObjectRefEntry e;
        e.objectId = id;
        e.contentVersion = cvFrom(seed);
        e.objectTypeToken = token;
        e.digest = digestOf(std::string{seed} + "-bytes");
        header.objectRefs.push_back(e);
    };
    addRef(robot, "zero-robot", "robot-design");
    addRef(j1, "zero-j1", "joint");
    addRef(j2, "zero-j2", "joint");
    addRef(l0, "zero-l0", "link");
    addRef(l1, "zero-l1", "link");
    addRef(l2, "zero-l2", "link");

    runtime::RobotChain chain;
    chain.robotObjectId = robot;
    chain.robotLocalName = "DYN_R2_ZERO";
    chain.deviceName = "DYN_R2_ZERO";

    // 关节几何同夹具（轴 +Y、origin 沿 +X 平移 L1）——仅物性留空。
    runtime::CanonicalJoint joint1;
    joint1.objectId = j1;
    joint1.localName = "joint_1";
    joint1.type = runtime::JointType::Revolute;
    joint1.axis = rw::math::Vector3D<double>(0.0, 1.0, 0.0);  // 关节轴：基座系 +Y（单位向量）
    joint1.bounds = runtime::JointBounds{-3.14159265358979323846, 3.14159265358979323846};
    joint1.maxVelocity = val(3.0);   // rad/s
    chain.joints.push_back(joint1);
    runtime::CanonicalJoint joint2 = joint1;
    joint2.objectId = j2;
    joint2.localName = "joint_2";
    joint2.origin = rw::math::Transform3D<double>(
        rw::math::Vector3D<double>(kL1, 0.0, 0.0), identityRotation());  // 单位 m
    chain.joints.push_back(joint2);

    // 连杆段：仅身份/名（物性全 NotProvided——SourcedValue 默认构造）。
    runtime::CanonicalLink baseLink;
    baseLink.objectId = l0;
    baseLink.localName = "base_link";
    chain.links.push_back(baseLink);
    runtime::CanonicalLink link1;
    link1.objectId = l1;
    link1.localName = "link_1";
    chain.links.push_back(link1);
    runtime::CanonicalLink link2;
    link2.objectId = l2;
    link2.localName = "link_2";
    chain.links.push_back(link2);

    return runtime::CanonicalModelBuilder()
        .setHeader(header)
        .setWorld(groundWorld())
        .setChain(chain)
        .setTools({})
        .build();
}

/// 产出状态/误差字段完整性断言（Passed 面的结构不变量——供多用例复用）。
void expectPassedInvariants(const ForwardCheckOutcome& o, std::size_t expectCompared)
{
    EXPECT_EQ(o.state, DynamicsValidity::ForwardCheckState::Passed);
    EXPECT_EQ(o.comparedSamples, expectCompared) << "比较区间数＝样本数−1";
    EXPECT_FALSE(o.numericAnomaly.has_value()) << "Passed 路径无数值异常";
    EXPECT_FALSE(o.cancelled) << "Passed 路径非取消";
    // 误差结构不变量：RMS ≤ max（全网格均方根不超过逐元素最大值）。
    EXPECT_LE(o.rmsErrQ, o.maxErrQ) << "RMS(e_q) ≤ max|e_q|（结构不变量）";
    EXPECT_LE(o.rmsErrQd, o.maxErrQd) << "RMS(e_qd) ≤ max|e_qd|（结构不变量）";
    // 首达时刻落在仿真时间轴内。
    EXPECT_GE(o.tOfMaxErrQ, 0.0) << "max|e_q| 首达时刻非负";
    EXPECT_LE(o.tOfMaxErrQ, (kSampleCount - 1) * kSampleDt) << "首达时刻≤仿真时长";
    // 积分子步已执行（审计面：50 区间×10 子步＝500 理想值；区间端点的
    // 浮点舍入可能让末段多收口一两个极小子步——上界按 10% 裕度约束）。
    EXPECT_GE(o.integratorSteps, 500u) << "子步数≥理想值（区间数×每区间子步数）";
    EXPECT_LE(o.integratorSteps, 550u) << "子步数受浮点收口影响的裕度上界";
}

/// 诊断码检索（素材断言辅助——按 DYN-* 码值找素材行）。
bool hasDiagnostic(const ForwardCheckOutcome& o, std::string_view code)
{
    for (const auto& d : o.diagnostics) {
        if (d.code == code) { return true; }
    }
    return false;
}

}  // namespace

// =====================================================================
// 用例组 DynForward——DYN-05 验收面（响应一致性检查＋异常检测）。
// =====================================================================

/**
 * 解析黄金算例（V-13 执行面）：伸直构型匀加速激励——τ_ref 恒定（采样保持
 * 零误差）、RK4 对二次多项式轨迹精确——仿真应解析重现参考段（误差机器
 * 精度量级），toleranceQ=1e-9 判 Passed；独立解析上界 1e-9 双重对照。
 */
TEST(DynForward, AnalyticStraightConfigurationPassed_DYN05)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-05"},
                  std::vector<std::string>{"AT-07"});
    // 本用例验证（§11.2 V-13）：τ_ref 回放仿真→max/RMS 误差≤配置阈值→
    // Passed；黄金对照容差＝本文件声明的 1e-9（测试对照口径，附录 D C7）。
    const runtime::CanonicalModel model = makeTwoLinkModel(groundWorld(), FrictionSpec{}, std::nullopt);

    ForwardCheckRequest r = goldenRequest(1e-9, 1e-9);  // rad、rad/s（分析容差）
    r.model = &model;

    FakeContext ctx;
    ForwardDynamicsValidator validator;
    const ForwardCheckOutcome o = validator.validate(r, ctx);

    expectPassedInvariants(o, static_cast<std::size_t>(kSampleCount - 1));
    // 独立解析上界（与 Passed 判定同一数据但独立阈值——黄金对照）。
    EXPECT_LT(o.maxErrQ, 1e-9) << "max|e_q| 解析上界（RK4 对二次多项式精确＋采样"
                                  "保持零误差——余量为浮点舍入）";
    EXPECT_LT(o.maxErrQd, 1e-9) << "max|e_qd| 解析上界（rad/s）";
}

/**
 * 一致性超阈值（V-13 失败分支）：同一黄金场景把判定容差压到机器精度以下
 * （1e-18 rad——分析配置来源的合法值），误差必然超阈 → Failed＋
 * DYN-FD-CONSISTENCY-FAILED 比较型素材（实际误差/阈值/单位）——比较型
 * 诊断的执行证明面（§9.4 行 11）。
 */
TEST(DynForward, ConsistencyFailedBelowMachinePrecision_DYN05)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-05"}, std::vector<std::string>{});
    const runtime::CanonicalModel model = makeTwoLinkModel(groundWorld(), FrictionSpec{}, std::nullopt);

    ForwardCheckRequest r = goldenRequest(1e-18, 1e-18);  // 阈值＜浮点舍入余量
    r.model = &model;

    FakeContext ctx;
    ForwardDynamicsValidator validator;
    const ForwardCheckOutcome o = validator.validate(r, ctx);

    EXPECT_EQ(o.state, DynamicsValidity::ForwardCheckState::Failed);
    EXPECT_TRUE(hasDiagnostic(o, kDynFdConsistencyFailed))
        << "超阈值必须产 DYN-FD-CONSISTENCY-FAILED 素材（§9.4 行 11）";
    EXPECT_GT(o.maxErrQ, 1e-18) << "实际误差＞阈值（失败证据自洽）";
    EXPECT_EQ(o.comparedSamples, static_cast<std::size_t>(kSampleCount - 1));
    EXPECT_FALSE(o.numericAnomaly.has_value()) << "超阈值非数值异常（判定分支区分）";
}

/**
 * 积分发散检出（V-16 执行面，DYN-05 异常检测本义）：超大加速度激励
 * （1e12 rad/s²）→ τ_ref 量级 1e11 N·m → q̇ 每子步增长 1e10 量级，数步内
 * 状态量级越过发散检出界 → Failed＋DYN-FD-DIVERGED＋numericAnomaly 定位
 * 文本；失败不判模型无效（§6.3.5——素材为 warning 级建议证据项语义）。
 */
TEST(DynForward, DivergenceDetected_DYN05)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-05"}, std::vector<std::string>{});
    const runtime::CanonicalModel model = makeTwoLinkModel(groundWorld(), FrictionSpec{}, std::nullopt);

    ForwardCheckRequest r = goldenRequest(1e-9, 1e-9);
    r.model = &model;
    // 注入超大激励（恒定 q̈=1e12 rad/s²——发散算例；参考轨迹同步改写保持
    // 激励自洽——发散先于比较发生，参考值量级不参与判定）。
    const double kHugeAcc = 1e12;  // rad/s²
    for (std::size_t j = 0; j < r.samples.size(); ++j) {
        const double t = j * kSampleDt;
        r.samples[j].q = {kQ0 + kV0 * t + 0.5 * kHugeAcc * t * t, 0.0};
        r.samples[j].qd = {kV0 + kHugeAcc * t, 0.0};
        r.samples[j].qdd = {kHugeAcc, 0.0};
    }
    // 大步长减少子步数（发散检出不需要精细积分）。
    r.forwardCheck.maxStepS = 0.05;  // s（0.5 s 时长 10 区间×每区间 ≤10 子步）

    FakeContext ctx;
    ForwardDynamicsValidator validator;
    const ForwardCheckOutcome o = validator.validate(r, ctx);

    EXPECT_EQ(o.state, DynamicsValidity::ForwardCheckState::Failed);
    EXPECT_TRUE(hasDiagnostic(o, kDynFdDiverged))
        << "发散必须产 DYN-FD-DIVERGED 素材（§9.4 行 10——异常检测本义）";
    ASSERT_TRUE(o.numericAnomaly.has_value()) << "发散必须携带异常定位文本";
    EXPECT_NE(o.numericAnomaly->find("发散"), std::string::npos)
        << "异常文本注明发散语义（实际：" << *o.numericAnomaly << "）";
}

/**
 * 数值异常检出（质量阵奇异）：全零物性链（两连杆质量/质心/惯量全部
 * NotProvided——§5.5 降级语义下合法模型）→ M(q)≡0 → 线性求解主元越界 →
 * Failed＋DYN-FD-NUMERIC-ANOMALY（§9.4 行 16——本批增码的消费证明面）。
 */
TEST(DynForward, NumericAnomalySingularMassMatrix_DYN05)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-05", "DYN-06"},
                  std::vector<std::string>{});
    // 全零物性模型（连杆物性字段保持 SourcedValue 默认＝NotProvided）：
    // 提取层不拒绝（缺失走降级素材——§5.5），但质量阵恒为零——正动力学
    // 无惯量可积分，属数值异常（非调用方错误、非发散）。
    const runtime::CanonicalModel model = makeZeroPropertyTwoLinkModel();

    ForwardCheckRequest r = goldenRequest(1e-9, 1e-9);
    r.model = &model;

    FakeContext ctx;
    ForwardDynamicsValidator validator;
    const ForwardCheckOutcome o = validator.validate(r, ctx);

    EXPECT_EQ(o.state, DynamicsValidity::ForwardCheckState::Failed);
    EXPECT_TRUE(hasDiagnostic(o, kDynFdNumericAnomaly))
        << "质量阵奇异必须产 DYN-FD-NUMERIC-ANOMALY 素材（§9.4 行 16）";
    ASSERT_TRUE(o.numericAnomaly.has_value()) << "数值异常必须携带定位文本";
    EXPECT_NE(o.numericAnomaly->find("质量矩阵"), std::string::npos)
        << "异常文本定位质量阵（实际：" << *o.numericAnomaly << "）";
    // τ_ref 侧降级素材透传（全零物性链的摩擦全缺——共源事实随 outcome 可见）。
    EXPECT_TRUE(hasDiagnostic(o, kDynFrictionMissing))
        << "τ_ref 侧降级素材透传（共源事实）";
}

/**
 * 回归（F-581，P1——audit/unit-code-review-20261009）：τ_ref 批量逆动力
 * 学在某样本数值失败时按 §5.6 截断后续产出（总行数＝(失败样本序+1)×n
 * ＜样本数×n）——原实现按满网格下标 at(j*n+i) 提取，截断发生即
 * std::out_of_range（非契约异常类型）穿出 validate；M/h 单样本提取面
 * （callRneaSample）同步加 usable 通道（行数≠n 或行态非 Ok＝不可用，
 * 走 DYN-FD-NUMERIC-ANOMALY 值面）。
 *
 * 注入：第 2 样本（j=1）qd[0]＝1e200（有限输入——离心项 w²·r≈1e400 溢
 * 出为 Inf，两关节力矩同拍非有限；§5.6"该样本起本工况失败记录，后续
 * 样本不产出"→产出行数＝n＜51×n）。
 */
TEST(DynForward, RneaPartialRowsDoNotEscapeContract_F581)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-05"},
                  std::vector<std::string>{});
    const runtime::CanonicalModel model = makeTwoLinkModel(groundWorld(), FrictionSpec{}, std::nullopt);

    ForwardCheckRequest r = goldenRequest(1e-9, 1e-9);
    r.model = &model;
    // 首样本保持有限（过初始状态检查）——失败面钉在第 2 样本。
    r.samples[1].qd[0] = 1e200;  // rad/s（有限；RNEA 输出必非有限）

    FakeContext ctx;
    ForwardDynamicsValidator validator;
    ForwardCheckOutcome o;
    // 修复面：截断按"样本不可信"值面消费——异常不再穿出（修复前此处
    // std::out_of_range 逃逸 validate，本断言即回归钉）。
    EXPECT_NO_THROW(o = validator.validate(r, ctx));
    EXPECT_FALSE(o.cancelled);
    // 截断语义：j=1 起 τ_ref 不可信——仿真至多完成区间 0，比较数据不
    // 完整（不伪造全程通过）。
    EXPECT_LT(o.comparedSamples, static_cast<std::size_t>(kSampleCount - 1))
        << "截断后比较样本数必须小于完整区间数";
    EXPECT_NE(o.state, DynamicsValidity::ForwardCheckState::Passed)
        << "τ_ref 截断（部分比较数据）不得判定 Passed";
}

/**
 * 初始状态缺失→NotRun（V-14 执行面）：样本数 <2（无仿真区间）→ NotRun＋
 * DYN-FD-INITIAL-STATE-MISSING 素材——建议证据项缺失不阻断、不伪造
 * Passed（§6.2/§6.3.4）。
 */
TEST(DynForward, NotRunEmptyAndSingleSample_DYN05)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-05"}, std::vector<std::string>{});
    const runtime::CanonicalModel model = makeTwoLinkModel(groundWorld(), FrictionSpec{}, std::nullopt);
    FakeContext ctx;
    ForwardDynamicsValidator validator;

    // 空样本。
    ForwardCheckRequest rEmpty = goldenRequest(1e-9, 1e-9);
    rEmpty.model = &model;
    rEmpty.samples.clear();
    ForwardCheckOutcome oEmpty = validator.validate(rEmpty, ctx);
    EXPECT_EQ(oEmpty.state, DynamicsValidity::ForwardCheckState::NotRun);
    EXPECT_TRUE(hasDiagnostic(oEmpty, kDynFdInitialStateMissing))
        << "初始状态缺失必须产 DYN-FD-INITIAL-STATE-MISSING（§9.4 行 9）";

    // 单样本（有初始状态、无区间——同码承载）。
    ForwardCheckRequest rSingle = goldenRequest(1e-9, 1e-9);
    rSingle.model = &model;
    rSingle.samples.resize(1);
    ForwardCheckOutcome oSingle = validator.validate(rSingle, ctx);
    EXPECT_EQ(oSingle.state, DynamicsValidity::ForwardCheckState::NotRun);
    EXPECT_TRUE(hasDiagnostic(oSingle, kDynFdInitialStateMissing));
}

/**
 * 初始状态非有限→NotRun（V-14 扩展）：q₀ 含 NaN——不可积分起点，不伪造
 * Passed（NFR-COR-03；§6.2"缺失→NotRun＋素材"）。
 */
TEST(DynForward, NotRunNonFiniteInitialState_DYN05)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-05", "NFR-COR-03"},
                  std::vector<std::string>{});
    const runtime::CanonicalModel model = makeTwoLinkModel(groundWorld(), FrictionSpec{}, std::nullopt);

    ForwardCheckRequest r = goldenRequest(1e-9, 1e-9);
    r.model = &model;
    r.samples.front().q[1] = std::numeric_limits<double>::quiet_NaN();  // rad（NaN 注入）

    FakeContext ctx;
    ForwardDynamicsValidator validator;
    const ForwardCheckOutcome o = validator.validate(r, ctx);

    EXPECT_EQ(o.state, DynamicsValidity::ForwardCheckState::NotRun);
    EXPECT_TRUE(hasDiagnostic(o, kDynFdInitialStateMissing));
    // NaN 字段显式无效（未比较——不伪造 0，§4.6 无效语义）。
    EXPECT_TRUE(std::isnan(o.maxErrQ)) << "未比较路径误差字段＝NaN（显式无效）";
}

/**
 * mode=Skip→NotApplicable（§6.3.4 显式不适用）：Skip 短路于全部其余校验
 * 之前（未配置积分器/容差是合法 Skip 形态）；不产素材（非缺失语义）。
 */
TEST(DynForward, NotApplicableWhenSkipped_DYN05)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-05"}, std::vector<std::string>{});
    const runtime::CanonicalModel model = makeTwoLinkModel(groundWorld(), FrictionSpec{}, std::nullopt);

    ForwardCheckRequest r = goldenRequest(1e-9, 1e-9);
    r.model = &model;
    r.forwardCheck.mode = ForwardCheckSettings::Mode::Skip;
    r.forwardCheck.integratorToken.clear();  // Skip 下未配置＝合法（短路）

    FakeContext ctx;
    ForwardDynamicsValidator validator;
    const ForwardCheckOutcome o = validator.validate(r, ctx);

    EXPECT_EQ(o.state, DynamicsValidity::ForwardCheckState::NotApplicable);
    EXPECT_TRUE(o.diagnostics.empty()) << "显式不适用非缺失——零素材";
    EXPECT_TRUE(std::isnan(o.maxErrQ)) << "未执行路径误差字段＝NaN";
}

/**
 * 取消语义（V-35 取消观察域）：宿主预先置取消→validate 返回 NotRun＋
 * cancelled 位＋零错误级诊断（取消非错误，UX-03）。
 */
TEST(DynForward, CancelledReturnsNotRunZeroError_DYN05_UX03)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-05"}, std::vector<std::string>{});
    const runtime::CanonicalModel model = makeTwoLinkModel(groundWorld(), FrictionSpec{}, std::nullopt);

    ForwardCheckRequest r = goldenRequest(1e-9, 1e-9);
    r.model = &model;

    FakeContext ctx;
    ctx.mCancelled = true;  // 宿主取消（τ_ref 主调用首样本即观测）
    ForwardDynamicsValidator validator;
    const ForwardCheckOutcome o = validator.validate(r, ctx);

    EXPECT_TRUE(o.cancelled) << "取消位透传（部分产出语义）";
    EXPECT_EQ(o.state, DynamicsValidity::ForwardCheckState::NotRun)
        << "取消＝检查未完成（非 Failed）";
    for (const auto& d : o.diagnostics) {
        EXPECT_NE(d.code, kDynRneaFailed) << "取消路径零错误级诊断（UX-03）";
    }
}

/**
 * 确定性复现（NFR-COR-02）：同输入两次 validate → 四态/误差四元组/首达
 * 时刻/子步数/诊断清单逐位相等（固定步长 RK4 纯确定——无随机源/时钟）。
 */
TEST(DynForward, DeterministicRepeat_NFR_COR_02)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-05", "NFR-COR-02"},
                  std::vector<std::string>{});
    const runtime::CanonicalModel model = makeTwoLinkModel(groundWorld(), FrictionSpec{}, std::nullopt);

    ForwardCheckRequest r = goldenRequest(1e-9, 1e-9);
    r.model = &model;

    FakeContext ctx;
    ForwardDynamicsValidator validator;
    const ForwardCheckOutcome a = validator.validate(r, ctx);
    const ForwardCheckOutcome b = validator.validate(r, ctx);

    EXPECT_EQ(a.state, b.state);
    EXPECT_EQ(a.comparedSamples, b.comparedSamples);
    EXPECT_EQ(a.maxErrQ, b.maxErrQ) << "逐位相等（固定步长确定性）";
    EXPECT_EQ(a.rmsErrQ, b.rmsErrQ);
    EXPECT_EQ(a.maxErrQd, b.maxErrQd);
    EXPECT_EQ(a.rmsErrQd, b.rmsErrQd);
    EXPECT_EQ(a.tOfMaxErrQ, b.tOfMaxErrQ);
    EXPECT_EQ(a.tOfMaxErrQd, b.tOfMaxErrQd);
    EXPECT_EQ(a.integratorSteps, b.integratorSteps);
    ASSERT_EQ(a.diagnostics.size(), b.diagnostics.size());
    for (std::size_t i = 0; i < a.diagnostics.size(); ++i) {
        EXPECT_EQ(a.diagnostics[i], b.diagnostics[i]) << "诊断素材逐位相等 @" << i;
    }
}

/**
 * 负载事件共源（§6.1 ⑤——非法示例"用另一工况的负载做仿真"的结构性
 * 拦截证明）：中段 Grasp 负载——τ_ref 与正向仿真在同一事件时间线下变体
 * 同步切换（区间边界生效），一致性保持 Passed；τ_ref 侧摩擦缺失素材
 * （零摩擦夹具）透传可见。
 */
TEST(DynForward, PayloadEventSameSourceConsistency_DYN05)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-05", "DYN-02"},
                  std::vector<std::string>{});
    const runtime::CanonicalModel model = makeTwoLinkModel(groundWorld(), FrictionSpec{}, std::nullopt);

    ForwardCheckRequest r = goldenRequest(1e-9, 1e-9);
    r.model = &model;
    // 条件负载（法兰挂载点质量负载——物性必填面：mass Provided、com/inertia
    // 提供完整避免估算分支干扰黄金判定）。
    EndEffectorPayload payload;
    payload.objectId = idFrom<ObjectId>("fd-payload");
    payload.mass = core::SourcedValue<double>::provided(
        0.5, core::ValueProvenance::make(core::ProvenanceKind::UserProvided));  // kg
    payload.centerOfMass = core::SourcedValue<rw::math::Vector3D<double>>::provided(
        rw::math::Vector3D<double>(0.0, 0.0, 0.0),
        core::ValueProvenance::make(core::ProvenanceKind::UserProvided));  // TCP 系下，m
    payload.inertia = core::SourcedValue<rw::math::InertiaMatrix<double>>::provided(
        diagInertia(0.001, 0.001, 0.001),
        core::ValueProvenance::make(core::ProvenanceKind::UserProvided));  // kg·m²
    payload.initiallyMounted = false;  // 事件前未挂载
    r.payloads.push_back(payload);
    // 中段夹取事件：tEvent＝样本 25 时刻（0.25 s）——变体在区间边界生效。
    PayloadEvent ev;
    ev.tEvent = 25 * kSampleDt;  // s
    ev.kind = PayloadEvent::Kind::Grasp;
    ev.payloadIndex = 0;
    r.events.push_back(ev);

    FakeContext ctx;
    ForwardDynamicsValidator validator;
    const ForwardCheckOutcome o = validator.validate(r, ctx);

    expectPassedInvariants(o, static_cast<std::size_t>(kSampleCount - 1));
    EXPECT_LT(o.maxErrQ, 1e-9)
        << "事件切换后两路仍同变体（共源强制）——误差保持机器精度量级";
    // 摩擦缺失素材透传（夹具摩擦全缺——τ_ref 侧 DYN-FRICTION-MISSING 可见）。
    EXPECT_TRUE(hasDiagnostic(o, kDynFrictionMissing))
        << "τ_ref 侧降级素材透传（共源事实）";
}

/**
 * fail-fast 前置边界（§10.0 调用方错误轨——配置/身份/输入非法逐项）：
 * Standard 模式配置非法（词表外 token／步长/容差非正）、模型空、工况 id
 * 空、重力非有限、维度不匹配、时间倒退——全部 DynamicsError、零产出。
 */
TEST(DynForward, FailFastContractViolations_DYN05)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-05"}, std::vector<std::string>{});
    const runtime::CanonicalModel model = makeTwoLinkModel(groundWorld(), FrictionSpec{}, std::nullopt);
    FakeContext ctx;
    ForwardDynamicsValidator validator;

    // token 词表外（§4.2 封闭词表——锁定值之外即违约）。
    {
        ForwardCheckRequest r = goldenRequest(1e-9, 1e-9);
        r.model = &model;
        r.forwardCheck.integratorToken = "ode-adaptive";
        EXPECT_THROW(validator.validate(r, ctx), DynamicsError);
    }
    // maxStepS 非正。
    {
        ForwardCheckRequest r = goldenRequest(1e-9, 1e-9);
        r.model = &model;
        r.forwardCheck.maxStepS = 0.0;  // s（非法）
        EXPECT_THROW(validator.validate(r, ctx), DynamicsError);
    }
    // relTolerance 非正。
    {
        ForwardCheckRequest r = goldenRequest(1e-9, 1e-9);
        r.model = &model;
        r.forwardCheck.relTolerance = -1e-6;  // 无量纲（非法）
        EXPECT_THROW(validator.validate(r, ctx), DynamicsError);
    }
    // toleranceQ 非有限。
    {
        ForwardCheckRequest r = goldenRequest(1e-9, 1e-9);
        r.model = &model;
        r.forwardCheck.toleranceQ = std::numeric_limits<double>::infinity();  // rad（非法）
        EXPECT_THROW(validator.validate(r, ctx), DynamicsError);
    }
    // toleranceQd 非正。
    {
        ForwardCheckRequest r = goldenRequest(1e-9, 1e-9);
        r.model = &model;
        r.forwardCheck.toleranceQd = 0.0;  // rad/s（非法）
        EXPECT_THROW(validator.validate(r, ctx), DynamicsError);
    }
    // 模型空。
    {
        ForwardCheckRequest r = goldenRequest(1e-9, 1e-9);
        r.model = nullptr;
        EXPECT_THROW(validator.validate(r, ctx), DynamicsError);
    }
    // 工况 id 空。
    {
        ForwardCheckRequest r = goldenRequest(1e-9, 1e-9);
        r.model = &model;
        r.conditionId = ObjectId{};  // 空 id
        EXPECT_THROW(validator.validate(r, ctx), DynamicsError);
    }
    // 重力非有限。
    {
        ForwardCheckRequest r = goldenRequest(1e-9, 1e-9);
        r.model = &model;
        r.gravityBase[2] = std::numeric_limits<double>::quiet_NaN();  // m/s²（非法）
        EXPECT_THROW(validator.validate(r, ctx), DynamicsError);
    }
    // 样本维度不匹配（DYN-DIMENSION-MISMATCH 语义——fail-fast）。
    {
        ForwardCheckRequest r = goldenRequest(1e-9, 1e-9);
        r.model = &model;
        r.samples[3].qd = {1.0};  // 长度 1≠2
        EXPECT_THROW(validator.validate(r, ctx), DynamicsError);
    }
    // 时间倒退（DYN-SERIES-NON-MONOTONIC 语义——fail-fast）。
    {
        ForwardCheckRequest r = goldenRequest(1e-9, 1e-9);
        r.model = &model;
        r.samples[7].t = r.samples[5].t;  // s（倒退/重复）
        EXPECT_THROW(validator.validate(r, ctx), DynamicsError);
    }
}

/**
 * 初始状态 q/qd 长度不一致→input-invalid（F-644）：长度错配的样本若落在
 * 首样本（初始状态），原实现的非有限扫描会以 q 的长度索引 qd 越界读取
 * （未定义行为）——修正后入口防线按"自由度不匹配"拒绝，消息含两个长度
 * 的比较数据；正常路径回归由全套件承载（本文件黄金算例用例组）。
 */
TEST(DynForward, InitialStateQLengthMismatchInputInvalid_F644)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-05"}, std::vector<std::string>{});
    // 本用例验证（F-644）：首样本 q 长度 2、qd 长度 1（自由度不匹配）——
    // validate 前置防线抛 DynamicsError（不越界、不产出半成品）。
    const runtime::CanonicalModel model =
        makeTwoLinkModel(groundWorld(), FrictionSpec{}, std::nullopt);
    ForwardCheckRequest r = goldenRequest(1e-9, 1e-9);
    r.model = &model;
    r.samples.front().qd = {0.3};  // rad/s（长度 1≠q 长度 2——错配注入）

    FakeContext ctx;
    ForwardDynamicsValidator validator;
    try {
        const ForwardCheckOutcome o = validator.validate(r, ctx);
        (void)o;
        FAIL() << "q/qd 长度不一致未抛出 DynamicsError";
    } catch (const DynamicsError& e) {
        EXPECT_NE(std::string(e.what()).find("input-invalid"), std::string::npos)
            << "token 前缀缺失：" << e.what();
        EXPECT_NE(std::string(e.what()).find("自由度不匹配"), std::string::npos)
            << "消息未说明自由度不匹配：" << e.what();
        EXPECT_NE(std::string(e.what()).find("qd 长度 1"), std::string::npos)
            << "消息缺长度比较数据：" << e.what();
    }
}
