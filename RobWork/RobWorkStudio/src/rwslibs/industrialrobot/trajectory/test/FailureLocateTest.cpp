/**
 * @file   FailureLocateTest.cpp
 * @brief  失败段诊断定位的运行期测试（WP-16-T09）——**端到端半区**（T08
 *         真实时间参数化管线在限值未定义模型上产出失败素材→经 locateFa-
 *         ilures 组装为稳定诊断条目：素材产面与诊断组装面全链）＋**四类
 *         素材组装半区**（无路径/分支跳变/奇异邻域/限制超标四类素材一次
 *         投递，TRJ-06"定位到具体段落与原因"逐类断言）＋ERR-01 字段义务
 *         与确定性回归。
 *
 * 设计依据：
 *   - units/trajectory.md §14.4（诊断构造经 IDiagnosticFactory——本套件
 *     以真实 StableCodeRegistry＋DiagnosticsFactory 验证全链）、§12.6
 *     （超限与失败定位——时间侧产码路径："限值未定义→+inf 语义→时间无
 *     界→TRJ-TIME-PARAM-FAILED 素材〔原因＝限值未定义；建议动作＝补模型
 *     限值〕"）、§8.3/§8.5（分支跳变/奇异邻域素材语义）、§7.6（无路径素
 *     材轨）、§20 追踪矩阵 TRJ-06 行（验证行 V-03/07/16/17/18——本套件
 *     钉 V-07 不阻断承载与 V-18 素材轨的**诊断条目侧**；V-16/17 素材产面
 *     已随 T08 TimeParamTest 验证）
 *   - 需求 TRJ-06（P0——四类失败定位到具体段落与原因）、ERR-01（字段义
 *     务）、NFR-COR-02（确定性）
 *   - 任务契约 tasks/foundation/WP-16-T09.json（acceptance 1——"无路径/
 *     分支跳变/奇异邻域/限制超标定位到具体段落与原因（TRJ-06，TRJ-* 稳
 *     定码）用例通过"；acceptance 2——ERR-01 字段义务）
 *
 * 端到端算例说明（为何选"限值未定义"做管线侧算例）：T08 缩放的数学精确
 * 性（等比缩放下速度峰值 ÷α、加速度峰值 ÷α²）使正常有限限值输入一轮达
 * 标——TRJ-LIMIT-EXCEEDED 的管线产面是防御路径（TimeParam.hpp LimitUnrea-
 * chable 注），黄金输入不可达；限值未定义（+inf）路径是 §12.6 明文的正
 * 常产面（V-18 锚点），素材稳定可复现。限制超标的**素材→条目**半区以直
 * 构素材覆盖（素材形态与 T08 makeLimitExceededRecord 同款——rad/s 单位
 * 三要素）；其素材产面正确性归 T08 套件（已执行留痕），本套件不重复。
 *
 * 本套件为**集成模式专属**（端到端半区构造 rw::math::Q 对象——PtpSequen-
 * ceTest 同款 gating；冒烟模式不编译本文件；组装契约的纯值面验证见
 * contract_test/FailureLocateContractTest.cpp，两模式皆跑）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/diagnostics/Catalog.hpp>
#include <sdurws/ird/diagnostics/DiagCodes.hpp>
#include <sdurws/ird/diagnostics/Factory.hpp>
#include <sdurws/ird/trajectory/DiagCodes.hpp>
#include <sdurws/ird/trajectory/Errors.hpp>
#include <sdurws/ird/trajectory/FailureLocate.hpp>
#include <sdurws/ird/trajectory/PlanConfig.hpp>
#include <sdurws/ird/trajectory/Smooth.hpp>
#include <sdurws/ird/trajectory/TimeParam.hpp>
#include <sdurws/ird/trajectory/TrjTypes.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <rw/math/Q.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace trj = sdurws::ird::trajectory;
namespace diag = sdurws::ird::diagnostics;
namespace core = sdurws::ird::core;

namespace {

// =====================================================================
// 装置：固定时钟＋真实码表/工厂＋管线黄金算例骨架（TimeParamTest 同款）
// =====================================================================

/// 固定时钟（emittedAtUtc 确定性——2026-10-11T00:00:00Z）。
class FixedClock final : public diag::IClock {
public:
    std::chrono::system_clock::time_point nowUtc() const override
    {
        return std::chrono::system_clock::time_point{
            std::chrono::seconds(1792051200)};
    }
};

/// 组装单关节折线几何（makeLinearJointPathGeometry 公共工厂——插值性：
/// s=k/(n-1) 恒等于第 k 路点）。
std::shared_ptr<const trj::IPathGeometry> jointPath(
    const std::vector<double>& waypoints)
{
    std::vector<rw::math::Q> points;
    points.reserve(waypoints.size());
    for (double q : waypoints) {
        points.emplace_back(1, q);  // 单关节构型（rad）
    }
    return trj::makeLinearJointPathGeometry(points);
}

/// 端到端用限值视图（+inf 用 std::numeric_limits<double>::infinity()）。
trj::JointDynamicLimits limits1(double velocity, double acceleration)
{
    trj::JointDynamicLimits lim;
    lim.velocityLimit = velocity;
    lim.accelerationLimit = acceleration;
    return lim;
}

/// 空请求骨架（合法域默认值——黄金算例声明）。
trj::TimeParamRequest baseTimeRequest()
{
    trj::TimeParamRequest request;
    request.limitsScaleFactor = 1.0;
    request.motionLawToken = trj::kTimeParamMethodQuinticSplineC2;
    request.sampleStepS = 0.001;   // s——采样步长
    request.maxIterations = 8;     // 缩放迭代上限
    return request;
}

// =====================================================================
// 测试夹具：真实码表＋工厂（生产装配序同款）
// =====================================================================

class FailureLocateTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        trj::registerTrajectoryCodes(m_registry);
        m_registry.seal();
        m_factory = std::make_unique<diag::DiagnosticsFactory>(
            m_registry, m_clock);
    }

    static core::ContentIdentity someIdentity()
    {
        return core::ContentIdentity::fromCanonical(
            "cid-00000000000000000000000000000000000000000000000000000000000000"
            "02");
    }

    static core::ObjectId someObject(const std::string& hex)
    {
        return core::ObjectId::fromCanonical("obj-" + hex);
    }

    trj::FailureLocateRequest baseRequest()
    {
        trj::FailureLocateRequest request;
        request.snapshotId = someIdentity();
        request.sliceId = someIdentity();
        return request;
    }

    diag::StableCodeRegistry m_registry;
    FixedClock m_clock;
    std::unique_ptr<diag::DiagnosticsFactory> m_factory;
};

/// 段级素材骨架（中文文案——ERR-01 必填面）。
trj::FailedSegmentRecord segmentRecord(const std::string& reasonToken,
                                       std::uint32_t segmentIndex,
                                       const std::string& phaseToken,
                                       std::optional<double> s = std::nullopt)
{
    trj::FailedSegmentRecord record;
    record.segmentIndex = segmentIndex;
    record.phaseToken = phaseToken;
    record.reasonToken = reasonToken;
    record.pathParameter = s;
    record.cause = "测试原因（中文——ERR-01）";
    record.recommendedAction = "测试建议动作（中文——ERR-01）";
    return record;
}

/// 速度比较型三要素（rad/s——与 T08 makeLimitExceededRecord 同款量纲）。
core::ComparativeFields velocityComparison(double actualPeak, double limit)
{
    core::ComparativeFields cmp;
    cmp.actual.quantity = core::SourcedValue<double>::provided(
        actualPeak,
        core::ValueProvenance::make(core::ProvenanceKind::DerivedReadOnly, {},
                                    {}, std::string("time-param-peak")));
    cmp.actual.unit = *core::UnitToken::find("rad/s");
    cmp.expected.quantity = core::SourcedValue<double>::provided(
        limit,
        core::ValueProvenance::make(core::ProvenanceKind::DerivedReadOnly, {},
                                    {}, std::string("time-param-limit")));
    cmp.expected.unit = *core::UnitToken::find("rad/s");
    return cmp;
}

}  // namespace

// =====================================================================
// 端到端半区：T08 真实管线产素材 → 诊断条目（V-18 素材轨的诊断侧）
// =====================================================================

/**
 * 端到端：限值未定义模型（+inf 限值）经真实 timeParameterize 产出
 * TRJ-TIME-PARAM-FAILED 全轨迹级素材（§12.6/V-18）→ locateFailures 组装
 * →断言诊断条目：全轨迹级定位上下文、error 阻断级、文案透传、无比较型
 * 伪造（限值未定义没有"实际 vs 期望"数值对——NFR-COR-03）。
 */
TEST_F(FailureLocateTest, EndToEndUndefinedLimitMaterializesDiagnostic)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-06", "TRJ-05", "ERR-01"},
                  std::vector<std::string>{"AT-06"}, std::nullopt);

    // ---- 第一步：真实时间参数化（限值未定义——速度 +inf，§12.6"时间无
    // 界"→LimitUnreachable，不伪造节拍）。
    trj::TimeParamRequest timeRequest = baseTimeRequest();
    timeRequest.segments.push_back([&] {
        trj::TimeParamSegment segment;
        segment.segmentIndex = 0u;
        segment.geometry = jointPath({0.0, 1.0});  // 单关节 0→1 rad
        return segment;
    }());
    timeRequest.jointLimits.push_back(
        limits1(std::numeric_limits<double>::infinity(), 10.0));
    const trj::TimeParamResult timeResult =
        trj::timeParameterize(timeRequest);

    // 管线素材前置断言（产面正确性——T08 契约的回归锚）。
    ASSERT_EQ(timeResult.status, trj::TimeParamStatus::LimitUnreachable);
    ASSERT_FALSE(timeResult.timeParam.has_value());   // 不伪造节拍
    ASSERT_EQ(timeResult.failureRecords.size(), 1u);
    EXPECT_EQ(timeResult.failureRecords[0].reasonToken,
              std::string(trj::kTrjTimeParamFailed));
    EXPECT_EQ(timeResult.failureRecords[0].segmentIndex,
              trj::kWholeTrajectorySegmentIndex);

    // ---- 第二步：素材投递组装（T08 产物为输入——契约实施要点）。
    trj::FailureLocateRequest request = baseRequest();
    request.subjectFallback = someObject("00000000000000000000000000000021");
    request.records = timeResult.failureRecords;

    const trj::FailureLocateResult result =
        trj::locateFailures(request, m_registry, *m_factory);

    // ---- 第三步：诊断条目断言（ERR-01 字段＋TRJ-06 定位）。
    ASSERT_EQ(result.entries.size(), 1u);
    const diag::DiagnosticEntry& entry = result.entries[0];
    EXPECT_EQ(entry.record.code, std::string(trj::kTrjTimeParamFailed));
    // 全轨迹级定位（0xFFFFFFFF 不渲染段序号——"全轨迹级"词面）。
    EXPECT_EQ(entry.record.context, "全轨迹级（阶段 time-param）");
    // 文案透传（原因＝限值未定义；建议动作＝补模型限值——§12.6 语义）。
    EXPECT_NE(entry.record.cause.find("限值未定义"), std::string::npos);
    EXPECT_NE(entry.record.recommendedAction.find("maxVelocity"),
              std::string::npos);
    // error 码→阻断级；码表严重度原值。
    EXPECT_EQ(entry.record.level, core::DiagnosticLevel::Error);
    EXPECT_EQ(entry.severity, diag::DiagnosticSeverity::Error);
    // 限值未定义无数值对——条目不伪造比较型字段（显式缺席）。
    EXPECT_FALSE(entry.record.comparison.has_value());
    // 对象绑定走回退链（素材全轨迹级未绑对象）。
    ASSERT_TRUE(entry.record.subject.has_value());
    EXPECT_EQ(*entry.record.subject,
              someObject("00000000000000000000000000000021"));
    // 关联身份块（§14.4 评估路径必填）。
    EXPECT_EQ(entry.context.snapshotId, request.snapshotId);
    EXPECT_EQ(entry.context.sliceId, request.sliceId);
}

// =====================================================================
// 四类素材组装半区：TRJ-06"具体段落与原因"逐类断言（§14.4 词表前三类
// ＋限制超标；素材形态与各管线产面同款）
// =====================================================================

/**
 * 四类失败一次投递：无路径（TRJ-NO-PATH）/分支跳变（TRJ-BRANCH-JUMP）/
 * 奇异邻域（TRJ-SINGULAR-NEIGHBORHOOD）/限制超标（TRJ-LIMIT-EXCEEDED）——
 * 逐类断言：稳定码透传为条目 code、段落定位（段序号/阶段/s）进上下文、
 * 原因文案透传、warning 类不阻断（V-07"不阻断"承载）、比较型三要素齐备。
 */
TEST_F(FailureLocateTest, FourFailureClassesLocateSegmentAndCause)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-06", "ERR-01"},
                  std::vector<std::string>{"AT-06"}, std::nullopt);

    trj::FailureLocateRequest request = baseRequest();
    request.subjectFallback = someObject("00000000000000000000000000000022");

    // ---- 类 1：无路径（§7.6 行 5"IK 无解→段失败定位"素材形态——段 2、
    // 阶段 plan-ptp、不可定位到点）。
    trj::FailedSegmentRecord noPath = segmentRecord(
        std::string(trj::kTrjNoPath), 2u, trj::kPhasePlanPtp);
    noPath.cause = "终点任务点 IK 无解（候选解集投影为空）";
    noPath.recommendedAction = "调整任务点位姿或增加初值策略后重新评估";
    request.records.push_back(std::move(noPath));

    // ---- 类 2：分支跳变（§8.3 素材形态——段 0、plan-line、s=0.5、
    // 附两端解摘要文案；warning 码）。
    trj::FailedSegmentRecord branch = segmentRecord(
        std::string(trj::kTrjBranchJump), 0u, trj::kPhasePlanLine, 0.5);
    branch.cause = "s=0.5 处 IK 分支跳变（逐轴差超 ikContinuityThreshold）";
    branch.recommendedAction = "调整直线路径或显式构型切换（经避障搜索衔接）";
    request.records.push_back(std::move(branch));

    // ---- 类 3：奇异邻域（§8.5 素材形态——比较型条件数/阈值/无量纲；
    // warning 码——V-07"不阻断"）。
    trj::FailedSegmentRecord singular = segmentRecord(
        std::string(trj::kTrjSingularNeighborhood), 0u, trj::kPhasePlanLine,
        0.75);
    singular.cause = "雅可比条件数 2500 超告警阈值 1000（奇异邻域）";
    singular.recommendedAction = "绕开奇异邻域（调整路径）或复核告警阈值";
    core::ComparativeFields cond;
    cond.actual.quantity = core::SourcedValue<double>::provided(
        2500.0,
        core::ValueProvenance::make(core::ProvenanceKind::DerivedReadOnly, {},
                                    {}, std::string("condition-number")));
    cond.actual.unit = *core::UnitToken::find("1");
    cond.expected.quantity = core::SourcedValue<double>::provided(
        1000.0, core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
    cond.expected.unit = *core::UnitToken::find("1");
    singular.comparison = std::move(cond);
    request.records.push_back(std::move(singular));

    // ---- 类 4：限制超标（§12.6 素材形态——段 1、time-param、s=0.25、
    // 比较型峰值/限值 rad/s；error 码——V-16 诊断侧）。
    trj::FailedSegmentRecord limit = segmentRecord(
        std::string(trj::kTrjLimitExceeded), 1u, trj::kPhaseTimeParam, 0.25);
    limit.cause = "关节 0 峰值速度超限（缩放迭代达上限仍超——§12.6）";
    limit.recommendedAction = "放宽该关节速度限值或延长目标节拍后重新评估";
    limit.comparison = velocityComparison(3.14, 1.0);
    request.records.push_back(std::move(limit));

    const trj::FailureLocateResult result =
        trj::locateFailures(request, m_registry, *m_factory);

    // ---- 总断言：四条目、序＝输入序、一一对应。
    ASSERT_EQ(result.entries.size(), 4u);
    ASSERT_EQ(result.locatedRecords.size(), 4u);

    // ---- 类 1：无路径（error；段级上下文；非比较型）。
    {
        const diag::DiagnosticEntry& e = result.entries[0];
        EXPECT_EQ(e.record.code, std::string(trj::kTrjNoPath));
        EXPECT_EQ(e.record.context, "轨迹段 2（阶段 plan-ptp）");
        EXPECT_NE(e.record.cause.find("IK 无解"), std::string::npos);
        EXPECT_EQ(e.severity, diag::DiagnosticSeverity::Error);
        EXPECT_EQ(e.record.level, core::DiagnosticLevel::Error);
        EXPECT_FALSE(e.record.comparison.has_value());
    }
    // ---- 类 2：分支跳变（warning＝不阻断；s 定位进上下文）。
    {
        const diag::DiagnosticEntry& e = result.entries[1];
        EXPECT_EQ(e.record.code, std::string(trj::kTrjBranchJump));
        EXPECT_EQ(e.record.context,
                  "轨迹段 0（阶段 plan-line） 路径参数 s=0.500000");
        EXPECT_EQ(e.severity, diag::DiagnosticSeverity::Warning);
        EXPECT_EQ(e.record.level, core::DiagnosticLevel::Warning);
    }
    // ---- 类 3：奇异邻域（warning；比较型无量纲三要素）。
    {
        const diag::DiagnosticEntry& e = result.entries[2];
        EXPECT_EQ(e.record.code, std::string(trj::kTrjSingularNeighborhood));
        EXPECT_EQ(e.record.context,
                  "轨迹段 0（阶段 plan-line） 路径参数 s=0.750000");
        ASSERT_TRUE(e.record.comparison.has_value());
        EXPECT_EQ(std::string(e.record.comparison->actual.unit.symbol()), "1");
        ASSERT_TRUE(e.record.comparison->actual.quantity.tryValue().has_value());
        EXPECT_DOUBLE_EQ(*e.record.comparison->actual.quantity.tryValue(),
                         2500.0);
        EXPECT_EQ(e.severity, diag::DiagnosticSeverity::Warning);
    }
    // ---- 类 4：限制超标（error；比较型 rad/s 三要素——UX-03 单位侧）。
    {
        const diag::DiagnosticEntry& e = result.entries[3];
        EXPECT_EQ(e.record.code, std::string(trj::kTrjLimitExceeded));
        EXPECT_EQ(e.record.context,
                  "轨迹段 1（阶段 time-param） 路径参数 s=0.250000");
        ASSERT_TRUE(e.record.comparison.has_value());
        EXPECT_EQ(std::string(e.record.comparison->actual.unit.symbol()),
                  "rad/s");
        ASSERT_TRUE(e.record.comparison->expected.quantity.tryValue().has_value());
        EXPECT_DOUBLE_EQ(*e.record.comparison->expected.quantity.tryValue(),
                         1.0);
        EXPECT_EQ(e.severity, diag::DiagnosticSeverity::Error);
        EXPECT_EQ(e.record.level, core::DiagnosticLevel::Error);
    }

    // ---- 素材回放面：locatedRecords 与 entries 一一对应（同序同值——
    // 定位可反查原始素材）。
    EXPECT_TRUE(result.locatedRecords[0] == request.records[0]);
    EXPECT_TRUE(result.locatedRecords[3] == request.records[3]);
}

/**
 * 确定性回归（NFR-COR-02）：同一四类素材请求两次组装——条目 ERR-01 语义
 * 面逐字段等价（record 全等＋固定时钟 emittedAtUtc 相等）；entryId 单调
 * 分配不属值等价面。
 */
TEST_F(FailureLocateTest, RepeatedAssemblyIsValueDeterministic)
{
    IRD_TEST_INFO("NFR-COR-02", {}, std::nullopt);

    trj::FailureLocateRequest request = baseRequest();
    request.subjectFallback = someObject("00000000000000000000000000000023");
    request.records.push_back(segmentRecord(std::string(trj::kTrjNoPath), 4u,
                                            trj::kPhasePlanAvoid));
    trj::FailedSegmentRecord limit = segmentRecord(
        std::string(trj::kTrjLimitExceeded), 4u, trj::kPhaseTimeParam, 0.0);
    limit.comparison = velocityComparison(2.0, 1.0);
    request.records.push_back(std::move(limit));

    const trj::FailureLocateResult first =
        trj::locateFailures(request, m_registry, *m_factory);
    const trj::FailureLocateResult second =
        trj::locateFailures(request, m_registry, *m_factory);

    ASSERT_EQ(first.entries.size(), 2u);
    for (std::size_t i = 0; i < first.entries.size(); ++i) {
        EXPECT_TRUE(first.entries[i].record == second.entries[i].record)
            << "条目 " << i;
        EXPECT_EQ(first.entries[i].emittedAtUtc, second.entries[i].emittedAtUtc);
        EXPECT_EQ(first.locatedRecords[i], second.locatedRecords[i]);
    }
}

// =====================================================================
// WP-16-T14——TRJ-08-S1 的超限/失败段定位诊断（jerk 维度素材链）
// =====================================================================

/**
 * 端到端（TRJ-08-S1/§12.6 行 3 jerk 半区——失败定位诊断全链）：v2 运动
 * 律（septic-spline-c3）＋jerk 限值未设（+inf）→真实 timeParameterize 产
 * 出 TRJ-TIME-PARAM-FAILED 素材（cause 定位 jerk 缺失）→ locateFailures
 * 组装 →断言条目：全轨迹级上下文、error 阻断、jerk 文案透传、建议动作
 * 指向补设 jerk 限值。
 */
TEST_F(FailureLocateTest, EndToEndSepticUndefinedJerkLimitMaterializesDiagnostic)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-08-S1", "TRJ-06", "ERR-01"},
                  std::vector<std::string>{}, std::nullopt);

    // ---- 第一步：真实时间参数化（v2＋jerk 限值未设——§12.6 行 3 同源
    // 语义的 S1 半区：时间无界→LimitUnreachable，不伪造节拍）。
    trj::TimeParamRequest timeRequest = baseTimeRequest();
    timeRequest.motionLawToken = trj::kTimeParamMethodSepticSplineC3;
    timeRequest.segments.push_back([&] {
        trj::TimeParamSegment segment;
        segment.segmentIndex = 0u;
        segment.geometry = jointPath({0.0, 1.0});
        return segment;
    }());
    trj::JointDynamicLimits limits = limits1(1.0, 10.0);
    limits.jerkLimit = std::numeric_limits<double>::infinity();  // 未设值
    timeRequest.jointLimits.push_back(limits);
    const trj::TimeParamResult timeResult =
        trj::timeParameterize(timeRequest);

    // 管线素材前置断言（产面正确性——WP-16-T14 契约回归锚）。
    ASSERT_EQ(timeResult.status, trj::TimeParamStatus::LimitUnreachable);
    ASSERT_FALSE(timeResult.timeParam.has_value());
    ASSERT_EQ(timeResult.failureRecords.size(), 1u);
    EXPECT_EQ(timeResult.failureRecords[0].reasonToken,
              std::string(trj::kTrjTimeParamFailed));
    EXPECT_NE(timeResult.failureRecords[0].cause.find("jerk"),
              std::string::npos);

    // ---- 第二步：素材投递组装（真实全链）。
    trj::FailureLocateRequest request = baseRequest();
    request.subjectFallback = someObject("00000000000000000000000000000024");
    request.records = timeResult.failureRecords;
    const trj::FailureLocateResult result =
        trj::locateFailures(request, m_registry, *m_factory);

    // ---- 第三步：诊断条目断言（ERR-01 字段＋TRJ-06/§12.6 定位）。
    ASSERT_EQ(result.entries.size(), 1u);
    const diag::DiagnosticEntry& entry = result.entries[0];
    EXPECT_EQ(entry.record.code, std::string(trj::kTrjTimeParamFailed));
    EXPECT_EQ(entry.record.context, "全轨迹级（阶段 time-param）");
    EXPECT_NE(entry.record.cause.find("jerk"), std::string::npos);
    EXPECT_NE(entry.record.recommendedAction.find("jerk"), std::string::npos);
    EXPECT_EQ(entry.record.level, core::DiagnosticLevel::Error);
    EXPECT_FALSE(entry.record.comparison.has_value());  // 限值缺失无数值对
    ASSERT_TRUE(entry.record.subject.has_value());
    EXPECT_EQ(*entry.record.subject,
              someObject("00000000000000000000000000000024"));
}

/**
 * 超限段定位素材组装（TRJ-08-S1——jerk 超限素材的比较型倍数形态经诊断
 * 链）：直构段级 jerk 超限素材（segmentIndex=3、s=0.75、comparison＝实
 * 测/限值倍数〔无量纲 token "1"——core 单位注册表无 rad/s³ 词形的合规
 * 承载，见 TimeParam.cpp 头注登记；SI 直值经 cause 文案〕）→ locateFa-
 * ilures →断言条目：段定位上下文（段 3＋s=0.750000）、TRJ-LIMIT-EXCEEDED
 * 比较型三要素保留（无量纲倍数 1.25 vs 限值线 1.0）、error 阻断级。
 * 素材产面（达上限防御路径——正常输入数学不可达，TimeParam.cpp 头注登
 * 记）由本用例以同款形态直构覆盖其**组装面**；管线产面正确性随 T08 速
 * 度/加速度半区同构（makeJerkLimitExceededRecord 与 makeLimitExceededRec-
 * ord 同一组装骨架）。
 */
TEST_F(FailureLocateTest, JerkLimitExceededRatioMaterialLocatesSegment)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-08-S1", "TRJ-06"},
                  std::vector<std::string>{}, std::nullopt);

    trj::FailureLocateRequest request = baseRequest();
    request.subjectFallback = someObject("00000000000000000000000000000025");

    // jerk 超限段级素材（WP-16-T14 倍数形态——comparison.actual＝峰值相
    // 对限值的倍数 1.25、expected＝限值线 1.0、单位 token "1"）。
    trj::FailedSegmentRecord jerkLimit = segmentRecord(
        std::string(trj::kTrjLimitExceeded), 3u, trj::kPhaseTimeParam, 0.75);
    jerkLimit.cause =
        "关节 0 峰值 jerk 超限（实测 125000.000000，限值 100000.000000，"
        "单位 rad/s^3——缩放迭代达上限仍超——§12.6/TRJ-08-S1）";
    jerkLimit.recommendedAction = "放宽该关节 jerk 限值或降低 limitsScaleFactor 后重评";
    core::ComparativeFields cmp;
    cmp.actual.quantity = core::SourcedValue<double>::provided(
        1.25,
        core::ValueProvenance::make(core::ProvenanceKind::DerivedReadOnly, {},
                                    {}, std::string("time-param-peak-jerk-ratio")));
    cmp.actual.unit = *core::UnitToken::find("1");
    cmp.expected.quantity = core::SourcedValue<double>::provided(
        1.0,
        core::ValueProvenance::make(core::ProvenanceKind::DerivedReadOnly, {},
                                    {}, std::string("time-param-limit")));
    cmp.expected.unit = *core::UnitToken::find("1");
    jerkLimit.comparison = std::move(cmp);
    request.records.push_back(std::move(jerkLimit));

    const trj::FailureLocateResult result =
        trj::locateFailures(request, m_registry, *m_factory);

    ASSERT_EQ(result.entries.size(), 1u);
    const diag::DiagnosticEntry& entry = result.entries[0];
    // 超限段定位诊断（TRJ-08-S1 验收后半——段序号＋段内参数进上下文）。
    EXPECT_EQ(entry.record.code, std::string(trj::kTrjLimitExceeded));
    EXPECT_EQ(entry.record.context,
              "轨迹段 3（阶段 time-param） 路径参数 s=0.750000");
    EXPECT_NE(entry.record.cause.find("jerk"), std::string::npos);
    EXPECT_NE(entry.record.cause.find("rad/s^3"), std::string::npos)
        << "比较型素材的 SI 直值量纲应经文案承载: " << entry.record.cause;
    // 比较型三要素保留（无量纲倍数——单位 token 透传不换算）。
    ASSERT_TRUE(entry.record.comparison.has_value());
    EXPECT_EQ(std::string(entry.record.comparison->actual.unit.symbol()), "1");
    ASSERT_TRUE(entry.record.comparison->actual.quantity.tryValue().has_value());
    EXPECT_DOUBLE_EQ(*entry.record.comparison->actual.quantity.tryValue(), 1.25);
    EXPECT_DOUBLE_EQ(*entry.record.comparison->expected.quantity.tryValue(), 1.0);
    EXPECT_EQ(entry.severity, diag::DiagnosticSeverity::Error);
    EXPECT_EQ(entry.record.level, core::DiagnosticLevel::Error);
}
