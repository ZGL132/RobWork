/**
 * @file   WorkspaceTest.cpp
 * @brief  工作空间探索式采样与指标可视化浏览用例组（KinWorkspace，
 *         WP-15-T14 批）——采样计划确定性（随机均匀/关节网格两通道同
 *         种子重跑一致，§7.2 黄金设施复用）、KIN-13 配置语义（seed=0
 *         非法不静默替换）、协作取消/进度/检查点（partial 如实）、点云
 *         着色/投影/图例/导出值行、可视化点会话回写（KIN-06 唯一写点）
 *         与 R2 边界（碰撞统一策略零布尔、计划身份会话内溯源）。任务
 *         契约 WP-15-T14 acceptance 1~6 逐条具名自证（AT-25 追溯）。
 *
 * 设计依据：
 *   - units/kinematics.md §7.3（R2 边界）、§7.2（采样黄金设施——
 *     splitmix64 派生/网格体心/斐波那契螺旋方向集，D-KIN-6）、§8.3/
 *     §8.4（取消/进度/检查点/确定性）、§10.2（V-11/22/27 本单元侧
 *     观测点）、§9.8（L-K4 回写）、§4.4（KIN-13 合法域）
 *   - REQUIREMENTS KIN-09/AT-25/KIN-06/AT-04/V13-01
 *   - 黄金几何：KinFkFixture 二连杆模型（视图真值面——脚本求解下模型
 *     仅承载自由度/TCP 解析）
 *   - 任务契约 tasks/foundation/WP-15-T14.json
 *
 * 测试策略：可控求解器替身（ScriptedSolver——按注入谓词返回指定结局，
 * 记录逐请求目标/容差；不调用取消探针——取消查询次数确定可数）＋可
 * 取消上下文替身（进度/取消记录可断言）。计算核经 runWorkspaceSampling
 * 直调（NFR-MNT-01 计算库可直调——T05/T06 同款）；渲染数据/投影/图例/
 * 导出为纯函数断言；回写经 KinSessionPose 真容器断言"仅会话姿态"。
 */

#include "KinFkFixture.hpp"

#include <sdurws/ird/evidence/Evaluator.hpp>
#include <sdurws/ird/kinematics/AnalysisConfig.hpp>
#include <sdurws/ird/kinematics/Commands.hpp>   // KinSessionPose（回写唯一写点）
#include <sdurws/ird/kinematics/Ik.hpp>
#include <sdurws/ird/kinematics/Render.hpp>     // writebackToSessionPose（KIN-06 出口）
#include <sdurws/ird/kinematics/Sampling.hpp>
#include <sdurws/ird/kinematics/Workspace.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace sdurws::ird::kinematics::testfixture;
namespace evidence = sdurws::ird::evidence;
namespace kin = sdurws::ird::kinematics;
using kin::SampleState;
using kin::WorkspaceColorMode;
using kin::WorkspacePointCloudRenderData;
using kin::WorkspaceProjectionPlane;
using kin::WorkspaceSamplingQuery;

namespace {

/// π（rad——位置样本姿态无约束落值的测试侧判别常量；与产品实现同一
/// 数学量，独立书写以防常量漂移静默）。
constexpr double kPi = 3.14159265358979323846;

// =====================================================================
// 小工具（脚手架——非断言面）
// =====================================================================

/// 探索式区域（盒心 (0.5,0.5,0.5)、边长 1——盒域 [0,1]³，格心坐标
/// 全正便于黄金断言；与 RegionCoverageTest 同几何）。
kin::RegionBox makeRegion()
{
    kin::RegionBox box;
    box.center = rw::math::Vector3D<double>(0.5, 0.5, 0.5);
    box.size = rw::math::Vector3D<double>(1.0, 1.0, 1.0);
    return box;
}

/// 合法求解配置（seed=42/线程 1——I-KIN-4 良构面；容差/迭代与 T06
/// 查询面同值——两通道可比对的语义前提）。
kin::AnalysisConfiguration makeConfig(std::uint64_t seed = 42U)
{
    kin::AnalysisConfiguration config;
    config.initialStrategy = kin::InitialValueStrategy::ReferenceQ;
    config.initialValuesCount = 1U;
    config.iterationLimit = 16U;
    config.positionResidualTolerance = 1e-6;
    config.orientationResidualTolerance = 1e-6;
    config.ikDedupThresholdPerAxis = 1e-6;
    config.regionBudget.seed = seed;
    config.regionBudget.threadCount = 1U;
    config.seed = seed;
    return config;
}

/// 探索式查询（Grid 5×5×4＝100 样本——与 T06 ACC1 黄金同几何同计数；
/// solver 必注入——脚本求解下模型仅承载自由度/TCP）。
WorkspaceSamplingQuery makeQuery(kin::IIkSolver* solver, std::uint64_t seed = 42U)
{
    WorkspaceSamplingQuery q;
    q.region = makeRegion();
    q.position.method = kin::PositionSamplingDefinition::Method::Grid;
    q.position.gridCounts = {5U, 5U, 4U};
    q.orientation.directionSamples = 1U;
    q.orientation.rollSamples = 1U;
    q.config = makeConfig(seed);
    q.referenceQ = {0.0, 0.0};
    q.tcp.toolObject = idFrom<core::ObjectId>("kin-tool");
    q.solver = solver;
    return q;
}

/// TCP 工具引用（快照内解析键——KinFkFixture 工具 oid 同源）。
kin::TcpRef toolRef() {
    kin::TcpRef tcp;
    tcp.toolObject = idFrom<core::ObjectId>("kin-tool");
    return tcp;
}

// =====================================================================
// 可控求解器替身（按注入谓词脚本结局；记录逐请求目标/容差——接线
// 断言面；不调用取消探针——取消查询次数确定可数）
// =====================================================================

class ScriptedSolver final : public kin::IIkSolver {
public:
    /// 结局谓词：入参＝请求（目标位姿可判别）；返回结局种类。
    using Predicate = std::function<kin::IkOutcomeKind(const kin::IkRequest&)>;

    explicit ScriptedSolver(Predicate predicate) : m_predicate(std::move(predicate)) {}

    kin::IkOutcome solve(const kin::IkRequest& request) const override
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            kin::IkOutcomeKind scripted = m_predicate(request);
            m_outcomes.push_back(scripted);
        }

        kin::IkOutcome outcome;
        outcome.outcomeKind = m_predicate(request);
        // 无碰撞会话＝硬过滤③跳过＋证据缺失标记（真实 IkSolver 行为的
        // 同款模拟——collisionNotEvaluated 的语义来源，KIN-05：检查不在
        // 范围绝不解读为无碰撞）。
        outcome.collisionNotEvaluated = (request.collisionSession == nullptr);
        // 组装器 presence 不变式的脚本侧满足（Reached 样本带一个代表
        // 解——workspace bestSolution 提取的断言源；指标值全字段可判别）。
        if ((outcome.outcomeKind == kin::IkOutcomeKind::SolutionsFound
             || outcome.outcomeKind == kin::IkOutcomeKind::PartialCollision)
            && outcome.solutionSet.solutions.empty()) {
            kin::KinematicSolution s;
            s.q = {0.1, -0.2};               // 自由度 2（二连杆夹具）
            s.positionResidual = 2.0e-7;     // m（< 容差 1e-6）
            s.orientationResidual = 3.0e-7;  // rad
            s.manipulability = 0.25;         // 无量纲（脚本值——判别锚）
            s.conditionNumber = 4.0;         // 无量纲
            s.minimumJointMargin = 0.75;     // 无量纲归一化比
            s.signature = kin::configurationSignature(s.q);  // 记录键（I-KIN-3——真实求解器同填）
            outcome.solutionSet.solutions.push_back(s);
        }
        if (outcome.outcomeKind == kin::IkOutcomeKind::MultiInitNoConvergence
            || outcome.outcomeKind == kin::IkOutcomeKind::AllCandidatesFiltered) {
            kin::IkSearchRecord record;
            record.searchBudgetUsed = 10U;
            record.initialGuessesTried = 2U;
            outcome.solutionSet.searchRecord = record;
        }
        return outcome;
    }

    const std::vector<kin::IkOutcomeKind>& outcomes() const { return m_outcomes; }

private:
    Predicate m_predicate;
    mutable std::mutex m_mutex;
    mutable std::vector<kin::IkOutcomeKind> m_outcomes;
};

/// 常用谓词：按样本位置 x 分流（≤0.5 → 可达；>0.5 → 解析界限证明）。
kin::IkOutcomeKind reachByX(const kin::IkRequest& request)
{
    return request.targetInBase.P()[0] <= 0.5
        ? kin::IkOutcomeKind::SolutionsFound
        : kin::IkOutcomeKind::AnalyticBoundExceeded;
}

/// 常用谓词：按样本位置 z 分流（>0.6 → 搜索未果；其余可达）。
kin::IkOutcomeKind dataInsufficientByZ(const kin::IkRequest& request)
{
    return request.targetInBase.P()[2] > 0.6
        ? kin::IkOutcomeKind::MultiInitNoConvergence
        : kin::IkOutcomeKind::SolutionsFound;
}

/// 设施失败替身（所有请求返回 FK 首评失败——V-27 同族值面断言源）。
class FacilityFailureSolver final : public kin::IIkSolver {
public:
    kin::IkOutcome solve(const kin::IkRequest&) const override
    {
        kin::IkOutcome outcome;
        outcome.facilityFailure = kin::IkFacilityFailure{
            kin::IkFacilityFailureStage::InitialFkEvaluation, "替身注入的视图失效"};
        return outcome;
    }
};

// =====================================================================
// 取消/进度/检查点替身（watermark 到量后置取消旗标；进度全记录）
// =====================================================================

class CancelAfterWatermarkSink final : public kin::IBatchCheckpointSink {
public:
    void batchWatermark(std::uint64_t completedBatchCount,
                        std::uint64_t totalBatchCount) override
    {
        watermarks.emplace_back(completedBatchCount, totalBatchCount);
        // m_cancelAfter=0＝不取消（未 arm）——0 哨兵不参与比较（否则
        // 首批 watermark 即恒取消，Progress 组的完整运行被误伤）。
        if (m_cancelAfter > 0U && completedBatchCount >= m_cancelAfter) {
            m_cancelled = true;
        }
    }

    void arm(std::uint64_t cancelAfter) { m_cancelAfter = cancelAfter; }
    bool cancelRequested() const { return m_cancelled; }

    std::vector<std::pair<std::uint64_t, std::uint64_t>> watermarks;

private:
    std::uint64_t m_cancelAfter = 0U;  // 0＝不取消
    bool m_cancelled = false;
};

/// 可取消上下文（取消旗标自检查点替身读取；进度/phase 全记录可断言）。
class RecordingContext final : public evidence::IEvaluationContext {
public:
    void attach(const CancelAfterWatermarkSink* sink) { m_sink = sink; }

    bool cancellationRequested() const override
    {
        return m_sink != nullptr && m_sink->cancelRequested();
    }
    void reportProgress(std::uint8_t percent, std::string_view phase) override
    {
        m_percents.push_back(percent);
        m_phases.emplace_back(phase);
    }
    std::optional<std::vector<std::uint8_t>>
    tryObjectBytes(core::ObjectId, core::ContentVersion) const override
    {
        return std::nullopt;
    }

    const std::vector<std::uint8_t>& percents() const { return m_percents; }
    const std::vector<std::string>& phases() const { return m_phases; }

private:
    const CancelAfterWatermarkSink* m_sink = nullptr;
    std::vector<std::uint8_t> m_percents;
    std::vector<std::string> m_phases;
};

}  // namespace

// =====================================================================
// ACC1——采样计划确定性：随机均匀/关节网格两通道同种子重跑样本集一致
// （§7.2 黄金设施复用——splitmix64 派生/网格体心）；样本数/种子/网格
// 步数可配；seed=0 非法不静默替换（I-KIN-4/KIN-13）
// =====================================================================

TEST(KinWorkspace, GridSameSeedRerunIdentical_WP15T14_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-09", "KIN-13"},
                  std::vector<std::string>{"AT-25"});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);
    ScriptedSolver solver(&reachByX);

    // 关节网格通道（Grid 体心——§7.2 黄金设施）：同种子两次运行，样本
    // 集逐位一致（同集同序——D-KIN-6 的探索式复用面）。样本集结构＝
    // 位置段（Grid 乘积 100）＋位姿段（位置×方向×roll＝100×1×1）——
    // §7.2 生成设施的双段输出，共 200。
    RecordingContext ctx1;
    const kin::WorkspaceSampleRun run1 =
        kin::runWorkspaceSampling(view, makeQuery(&solver), ctx1);
    RecordingContext ctx2;
    const kin::WorkspaceSampleRun run2 =
        kin::runWorkspaceSampling(view, makeQuery(&solver), ctx2);

    ASSERT_EQ(run1.samples.samples.size(), std::size_t{200});
    ASSERT_EQ(run2.samples.samples.size(), run1.samples.samples.size());
    for (std::size_t i = 0; i < run1.samples.samples.size(); ++i) {
        EXPECT_EQ(run1.samples.samples[i].sampleIndex,
                  run2.samples.samples[i].sampleIndex);
        EXPECT_EQ(run1.samples.samples[i].position,
                  run2.samples.samples[i].position);
    }
    // 样本集身份一致（同 (计划,种子) 同身份——重跑核对键）。
    EXPECT_EQ(run1.sampleSetIdentity, run2.sampleSetIdentity);
}

TEST(KinWorkspace, RandomSameSeedRerunIdenticalAndSeedSensitive_WP15T14_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-09", "KIN-13"},
                  std::vector<std::string>{"AT-25", "V-20"});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);
    ScriptedSolver solver(&reachByX);

    // 随机均匀通道（Random——splitmix64 派生）：同种子重跑逐位一致。
    // 样本集＝位置段 37＋位姿段 37（37×1×1）＝74。
    WorkspaceSamplingQuery qA = makeQuery(&solver);
    qA.position.method = kin::PositionSamplingDefinition::Method::Random;
    qA.position.randomCount = 37U;
    RecordingContext ctxA1;
    const kin::WorkspaceSampleRun runA1 = kin::runWorkspaceSampling(view, qA, ctxA1);
    RecordingContext ctxA2;
    const kin::WorkspaceSampleRun runA2 = kin::runWorkspaceSampling(view, qA, ctxA2);
    ASSERT_EQ(runA1.samples.samples.size(), std::size_t{74});
    ASSERT_EQ(runA2.samples.samples.size(), runA1.samples.samples.size());
    for (std::size_t i = 0; i < runA1.samples.samples.size(); ++i) {
        EXPECT_EQ(runA1.samples.samples[i].position,
                  runA2.samples.samples[i].position);
    }

    // 不同种子→不同样本集（种子入身份——V-20：不静默复用）；样本数
    // 可配（37≠100——同查询面换数即换计划）。
    WorkspaceSamplingQuery qB = qA;
    qB.config.regionBudget.seed = 43U;
    qB.config.seed = 43U;
    RecordingContext ctxB;
    const kin::WorkspaceSampleRun runB = kin::runWorkspaceSampling(view, qB, ctxB);
    ASSERT_EQ(runB.samples.samples.size(), std::size_t{74});
    bool anyDifferent = false;
    for (std::size_t i = 0; i < runB.samples.samples.size(); ++i) {
        if (!(runB.samples.samples[i].position == runA1.samples.samples[i].position)) {
            anyDifferent = true;
        }
    }
    EXPECT_TRUE(anyDifferent);
    EXPECT_NE(runA1.sampleSetIdentity, runB.sampleSetIdentity);
}

TEST(KinWorkspace, GridSpacingToCounts_WP15T14_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-09"},
                  std::vector<std::string>{"AT-25"});

    // 步长规范化（D-REQ-2 同款语义——floor(size/spacing)+1）：
    // size=1、spacing=0.25 → 每轴 5 格心（0.1/0.3/0.5/0.7/0.9）。
    const auto counts = kin::gridCountsFromSpacing(
        rw::math::Vector3D<double>(1.0, 1.0, 1.0),
        rw::math::Vector3D<double>(0.25, 0.25, 0.25));
    EXPECT_EQ(counts, (std::array<std::uint32_t, 3>{5U, 5U, 5U}));

    // 区间小于一个步长 → 单格心（floor=0 +1）。
    const auto single = kin::gridCountsFromSpacing(
        rw::math::Vector3D<double>(0.1, 0.2, 0.3),
        rw::math::Vector3D<double>(1.0, 1.0, 1.0));
    EXPECT_EQ(single, (std::array<std::uint32_t, 3>{1U, 1U, 1U}));

    // 非法步长/盒尺寸 fail-fast（不钳制不置零——NFR-COR-03）。
    EXPECT_THROW(kin::gridCountsFromSpacing(
                     rw::math::Vector3D<double>(1.0, 1.0, 1.0),
                     rw::math::Vector3D<double>(0.25, 0.0, 0.25)),
                 std::invalid_argument);
    EXPECT_THROW(kin::gridCountsFromSpacing(
                     rw::math::Vector3D<double>(-1.0, 1.0, 1.0),
                     rw::math::Vector3D<double>(0.25, 0.25, 0.25)),
                 std::invalid_argument);
}

TEST(KinWorkspace, SeedZeroRejectedNotSilentlyReplaced_WP15T14_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-13"},
                  std::vector<std::string>{"I-KIN-4", "V-03"});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);
    ScriptedSolver solver(&reachByX);

    // 种子权威面＝AnalysisConfiguration（KIN-13）：regionBudget.seed=0
    // 与 config.seed=0 均非法——校验拒绝（不做 0→1 静默替换）。
    WorkspaceSamplingQuery qBudget = makeQuery(&solver);
    qBudget.config.regionBudget.seed = 0U;
    EXPECT_THROW(kin::validateWorkspaceSamplingQuery(view, qBudget),
                 std::invalid_argument);

    WorkspaceSamplingQuery qSeed = makeQuery(&solver);
    qSeed.config.seed = 0U;
    EXPECT_THROW(kin::validateWorkspaceSamplingQuery(view, qSeed),
                 std::invalid_argument);
}

TEST(KinWorkspace, QueryValidationFailFast_WP15T14)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-09", "NFR-COR-03"},
                  std::vector<std::string>{"AT-25"});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);
    ScriptedSolver solver(&reachByX);

    // referenceQ 维度违约（设备自由度 2）。
    WorkspaceSamplingQuery qDof = makeQuery(&solver);
    qDof.referenceQ = {0.0};
    EXPECT_THROW(kin::validateWorkspaceSamplingQuery(view, qDof),
                 std::invalid_argument);

    // 区域盒非正（RegionBox 契约）。
    WorkspaceSamplingQuery qBox = makeQuery(&solver);
    qBox.region.size = rw::math::Vector3D<double>(1.0, 0.0, 1.0);
    EXPECT_THROW(kin::validateWorkspaceSamplingQuery(view, qBox),
                 std::invalid_argument);

    // 姿态采样计数下界（≥1——requirements 词表原文）。
    WorkspaceSamplingQuery qOri = makeQuery(&solver);
    qOri.orientation.rollSamples = 0U;
    EXPECT_THROW(kin::validateWorkspaceSamplingQuery(view, qOri),
                 std::invalid_argument);

    // 合法查询不抛（对照组——前述违例均为真拒绝而非校验器全抛）。
    EXPECT_NO_THROW(kin::validateWorkspaceSamplingQuery(view, makeQuery(&solver)));
}

// =====================================================================
// ACC2——采样可取消有进度：协作取消如实 NotRun/partial；进度经事件
// 分发；取消/失败不产完整结果（V-11/22 同口径）
// =====================================================================

TEST(KinWorkspace, CancelMidRunPartialNotRun_WP15T14_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-09", "TASK-02", "NFR-PERF-02"},
                  std::vector<std::string>{"AT-25", "V-22"});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);
    ScriptedSolver solver(&reachByX);

    // 批大小 40（200 样本→5 批）——第一批 watermark 后置取消。
    CancelAfterWatermarkSink sink;
    sink.arm(1U);
    RecordingContext ctx;
    ctx.attach(&sink);

    WorkspaceSamplingQuery q = makeQuery(&solver);
    q.maxBatchSize = 40U;
    q.checkpointSink = &sink;
    const kin::WorkspaceSampleRun run = kin::runWorkspaceSampling(view, q, ctx);

    // partial 如实：cancelled=true、NotRun 样本存在且 reason 非空、
    // 计数守恒（completedCount＋notRunCount＝总数——不产完整结果）。
    EXPECT_TRUE(run.cancelled);
    EXPECT_EQ(run.samples.samples.size(), std::size_t{200});
    EXPECT_EQ(run.completedCount + run.notRunCount, std::uint64_t{200});
    EXPECT_GT(run.notRunCount, std::uint64_t{0});
    std::uint64_t notRunSeen = 0;
    for (const kin::WorkspaceSampleResult& r : run.results) {
        if (r.state == SampleState::NotRun) {
            ++notRunSeen;
            EXPECT_FALSE(r.reason.empty());
        }
    }
    EXPECT_EQ(notRunSeen, run.notRunCount);
    // watermark 只推进到取消点（第一批完成——后续批不派发不推进）。
    ASSERT_EQ(sink.watermarks.size(), std::size_t{1});
    EXPECT_EQ(sink.watermarks[0].first, std::uint64_t{1});
    EXPECT_EQ(sink.watermarks[0].second, std::uint64_t{5});
}

TEST(KinWorkspace, ProgressAndCheckpointSequence_WP15T14_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-09", "EX-T05", "EX-T08"},
                  std::vector<std::string>{"AT-25"});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);
    ScriptedSolver solver(&reachByX);

    // 完整运行（5 批×40，200 样本）：进度单调非降且终值 100；phase 恒
    // kWorkspaceSamplingPhase；watermark 序列 (1,5)..(5,5)。
    CancelAfterWatermarkSink sink;  // 不取消
    RecordingContext ctx;
    ctx.attach(&sink);

    WorkspaceSamplingQuery q = makeQuery(&solver);
    q.maxBatchSize = 40U;
    q.checkpointSink = &sink;
    const kin::WorkspaceSampleRun run = kin::runWorkspaceSampling(view, q, ctx);

    EXPECT_FALSE(run.cancelled);
    EXPECT_EQ(run.notRunCount, std::uint64_t{0});
    EXPECT_EQ(run.completedCount, std::uint64_t{200});

    ASSERT_FALSE(ctx.percents().empty());
    for (std::size_t i = 1; i < ctx.percents().size(); ++i) {
        EXPECT_LE(ctx.percents()[i - 1], ctx.percents()[i]);
    }
    EXPECT_EQ(ctx.percents().back(), std::uint8_t{100});
    for (const std::string& phase : ctx.phases()) {
        EXPECT_EQ(phase, kin::kWorkspaceSamplingPhase);
    }
    ASSERT_EQ(sink.watermarks.size(), std::size_t{5});
    for (std::uint64_t b = 1; b <= 5; ++b) {
        EXPECT_EQ(sink.watermarks[b - 1],
                  (std::pair<std::uint64_t, std::uint64_t>{b, 5U}));
    }
}

TEST(KinWorkspace, FacilityFailureSampleDataInsufficient_WP15T14)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-03", "D-KIN-7"},
                  std::vector<std::string>{"V-27"});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);
    FacilityFailureSolver solver;

    // 设施失败值面（V-27 同族）：样本→DataInsufficient＋reason 携带
    // 失败阶段；绝不输出不可行（状态词表无"不可行"——Unreachable 不
    // 出现）；不中断批内其余样本（全部样本同轨处理）。
    RecordingContext ctx;
    WorkspaceSamplingQuery q = makeQuery(&solver);
    q.position.gridCounts = {2U, 1U, 1U};  // 位置 2＋位姿 2＝4 样本——小面快
    const kin::WorkspaceSampleRun run = kin::runWorkspaceSampling(view, q, ctx);

    ASSERT_EQ(run.results.size(), std::size_t{4});
    for (const kin::WorkspaceSampleResult& r : run.results) {
        EXPECT_EQ(r.state, SampleState::DataInsufficient);
        EXPECT_FALSE(r.reason.empty());
        EXPECT_NE(r.reason.find("求解设施失败"), std::string::npos);
        EXPECT_FALSE(r.bestSolution.has_value());
    }
    EXPECT_EQ(run.completedCount, std::uint64_t{4});
}

/// KIN-P2（审查 A02）——工作空间探索式采样的位置样本经**真实 IkSolver**
/// 在退化链上闭环（审查 A02 的 Workspace.cpp 通道回归钉，与
/// RegionCoverageTest 的 Evaluators.cpp 通道同源反例）：coaxialChainModel
/// （六轴同轴、TCP 轴距 1 m）上的已知可达点 (0,1,0)（q*=(π/2,0,…)）。
/// 修复前：位置样本 FullPose＋π 容差——姿态项参与 6 维迭代，位置可达
/// 点被牵制为 DataInsufficient；修复后：taskSpace=PositionOnly——
/// Reached（探索式点云的正确着色语义）。位姿样本的结局不在断言面
/// （退化链上任意固定姿态目标几乎必然搜索未果——保守 DataInsufficient
/// 语义本身正确）。
TEST(KinWorkspace, PositionSampleRealSolverDegenerateChain_KIN_P2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-09", "KIN-02", "KIN-04"},
                  std::vector<std::string>{"AT-25"});

    const rt::CanonicalModel model = coaxialChainModel();
    TestView view(model);
    const kin::IkSolver realSolver;  // 真实求解器（A02 闭环——非脚本结局）

    // 单位置样本查询：区域盒心＝(0,1,0)（审查反例点）、边长 0.02——
    // Grid 1×1×1 格心＝盒心；位姿维度 1×1（合法域——其结局不断言）。
    WorkspaceSamplingQuery q;
    q.region = makeRegion();
    q.region.center = rw::math::Vector3D<double>(0.0, 1.0, 0.0);
    q.region.size = rw::math::Vector3D<double>(0.02, 0.02, 0.02);
    q.position.method = kin::PositionSamplingDefinition::Method::Grid;
    q.position.gridCounts = {1U, 1U, 1U};
    q.orientation.directionSamples = 1U;
    q.orientation.rollSamples = 1U;
    q.config = makeConfig(42U);
    q.config.initialValuesCount = 4U;  // 多初值——修复面不依赖初值碰巧命中
    q.config.iterationLimit = 256U;    // 审查反例同预算（修复前该预算下 0 解）
    q.referenceQ.assign(6, 0.0);
    q.tcp = toolRef();
    q.solver = &realSolver;

    RecordingContext ctx;
    const kin::WorkspaceSampleRun run = kin::runWorkspaceSampling(view, q, ctx);

    // 样本集：1 位置＋1 位姿（生成序黄金锁定——位置样本全序首位）。
    ASSERT_EQ(run.samples.samples.size(), std::size_t{2});
    EXPECT_EQ(run.samples.samples[0].kind, kin::SampleKind::Position);
    EXPECT_NEAR(run.samples.samples[0].position[0], 0.0, 1e-12);
    EXPECT_NEAR(run.samples.samples[0].position[1], 1.0, 1e-12);
    // 位置样本（槽 0）终态＝Reached（修复前 DataInsufficient——A02）。
    EXPECT_EQ(run.results[0].state, SampleState::Reached);
    ASSERT_TRUE(run.results[0].bestSolution.has_value());
    EXPECT_LE(run.results[0].bestSolution->positionResidual, 1e-6);
}

// =====================================================================
// ACC3——点云着色/投影/图例/导出值行（AT-25；渲染数据本单元产、呈现
// 归 ui View3D——§3.3 Render.hpp 纪律）
// =====================================================================

TEST(KinWorkspace, PointCloudStatesAndScalars_WP15T14_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-09"},
                  std::vector<std::string>{"AT-25"});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);
    ScriptedSolver solver(&reachByX);

    // 200 样本（位置段 100＋位姿段 100——位姿段样本位置同 Grid 分布）：
    // x≤0.5 → Reached（两段各 60，共 120）；x>0.5 → Unreachable（两段
    // 各 40，共 80——解析界限）。
    RecordingContext ctx;
    const kin::WorkspaceSampleRun run =
        kin::runWorkspaceSampling(view, makeQuery(&solver), ctx);
    ASSERT_EQ(run.results.size(), std::size_t{200});

    const WorkspacePointCloudRenderData cloud = kin::buildWorkspacePointCloud(run);
    ASSERT_EQ(cloud.points.size(), run.results.size());
    EXPECT_EQ(cloud.contractVersion, kin::kWorkspaceRenderContractVersion);

    // 点序＝样本全序（sampleIndex 升序连续——确定性序）。
    for (std::size_t i = 0; i < cloud.points.size(); ++i) {
        EXPECT_EQ(cloud.points[i].sampleIndex, run.results[i].sampleIndex);
        EXPECT_EQ(cloud.points[i].position, run.results[i].tcpPosition);
    }

    // 状态类别与解级标量投影：Reached 点有最佳解（脚本值可判别）；
    // Unreachable 点无解（hasSolution=false、六标量占位 0）。
    std::uint64_t reached = 0;
    std::uint64_t unreachable = 0;
    for (const auto& p : cloud.points) {
        if (p.state == SampleState::Reached) {
            ++reached;
            EXPECT_TRUE(p.hasSolution);
            EXPECT_DOUBLE_EQ(p.manipulability, 0.25);
            EXPECT_DOUBLE_EQ(p.conditionNumber, 4.0);
            EXPECT_DOUBLE_EQ(p.minimumJointMargin, 0.75);
            EXPECT_NEAR(p.positionResidual, 2.0e-7, 1e-15);
            EXPECT_NEAR(p.orientationResidual, 3.0e-7, 1e-15);
        } else if (p.state == SampleState::Unreachable) {
            ++unreachable;
            EXPECT_FALSE(p.hasSolution);
            EXPECT_DOUBLE_EQ(p.manipulability, 0.0);
        }
    }
    EXPECT_EQ(reached, std::uint64_t{120});
    EXPECT_EQ(unreachable, std::uint64_t{80});
    EXPECT_EQ(reached + unreachable, std::uint64_t{200});

    // 位置值域＝实测极值（盒域 [0,1]³ 的格心——x/y∈{0.1..0.9}、
    // z∈{0.125..0.875}）。
    EXPECT_NEAR(cloud.positionMin[0], 0.1, 1e-12);
    EXPECT_NEAR(cloud.positionMax[0], 0.9, 1e-12);
    EXPECT_NEAR(cloud.positionMin[2], 0.125, 1e-12);
    EXPECT_NEAR(cloud.positionMax[2], 0.875, 1e-12);
}

TEST(KinWorkspace, ProjectionsXY_XZ_YZ_WP15T14_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-09"},
                  std::vector<std::string>{"AT-25"});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);
    ScriptedSolver solver(&reachByX);
    RecordingContext ctx;
    const kin::WorkspaceSampleRun run =
        kin::runWorkspaceSampling(view, makeQuery(&solver), ctx);
    const WorkspacePointCloudRenderData cloud = kin::buildWorkspacePointCloud(run);
    ASSERT_FALSE(cloud.points.empty());

    // 三面投影舍轴语义（XY→(x,y)、XZ→(x,z)、YZ→(y,z)；单位 m）。
    const auto xy = kin::projectWorkspacePoints(cloud, WorkspaceProjectionPlane::XY);
    const auto xz = kin::projectWorkspacePoints(cloud, WorkspaceProjectionPlane::XZ);
    const auto yz = kin::projectWorkspacePoints(cloud, WorkspaceProjectionPlane::YZ);
    ASSERT_EQ(xy.size(), cloud.points.size());
    ASSERT_EQ(xz.size(), cloud.points.size());
    ASSERT_EQ(yz.size(), cloud.points.size());
    for (std::size_t i = 0; i < cloud.points.size(); ++i) {
        const rw::math::Vector3D<double>& p = cloud.points[i].position;
        EXPECT_DOUBLE_EQ(xy[i].first, p[0]);
        EXPECT_DOUBLE_EQ(xy[i].second, p[1]);
        EXPECT_DOUBLE_EQ(xz[i].first, p[0]);
        EXPECT_DOUBLE_EQ(xz[i].second, p[2]);
        EXPECT_DOUBLE_EQ(yz[i].first, p[1]);
        EXPECT_DOUBLE_EQ(yz[i].second, p[2]);
    }
}

TEST(KinWorkspace, LegendsStateCollisionAndScalarRange_WP15T14_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-09"},
                  std::vector<std::string>{"AT-25"});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);
    ScriptedSolver solver(&reachByX);
    RecordingContext ctx;
    const kin::WorkspaceSampleRun run =
        kin::runWorkspaceSampling(view, makeQuery(&solver), ctx);
    const WorkspacePointCloudRenderData cloud = kin::buildWorkspacePointCloud(run);

    // 状态图例：五类条目、稳定词表序（Reached 居首——枚举声明序）。
    const kin::WorkspaceColorLegend stateLegend =
        kin::workspaceLegendFor(cloud, WorkspaceColorMode::ByState);
    EXPECT_EQ(stateLegend.mode, WorkspaceColorMode::ByState);
    ASSERT_EQ(stateLegend.entries.size(), std::size_t{5});
    EXPECT_EQ(stateLegend.entries[0].categoryToken, "reached");
    EXPECT_EQ(stateLegend.entries[4].categoryToken, "not-run");
    EXPECT_FALSE(stateLegend.hasRange);

    // 标量图例（可操作度）：单条＋实测值域（脚本值 0.25——全部有解点
    // 同值 → min=max=0.25）；无解点不入值域。
    const kin::WorkspaceColorLegend manipLegend =
        kin::workspaceLegendFor(cloud, WorkspaceColorMode::ByManipulability);
    ASSERT_EQ(manipLegend.entries.size(), std::size_t{1});
    EXPECT_TRUE(manipLegend.hasRange);
    EXPECT_DOUBLE_EQ(manipLegend.entries[0].minValue, 0.25);
    EXPECT_DOUBLE_EQ(manipLegend.entries[0].maxValue, 0.25);

    // 碰撞图例：三类（无会话→全部"未评价"——KIN-05 保守呈现面）。
    const kin::WorkspaceColorLegend collLegend =
        kin::workspaceLegendFor(cloud, WorkspaceColorMode::ByCollision);
    ASSERT_EQ(collLegend.entries.size(), std::size_t{3});
    EXPECT_EQ(collLegend.entries[0].categoryToken, "collision-free");
    EXPECT_EQ(collLegend.entries[1].categoryToken, "in-collision");
    EXPECT_EQ(collLegend.entries[2].categoryToken, "not-evaluated");
    for (const auto& p : cloud.points) {
        EXPECT_EQ(p.collisionClass, std::uint8_t{2});
    }
}

TEST(KinWorkspace, ExportCsvDeterministic_WP15T14_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-09", "KIN-08"},
                  std::vector<std::string>{"AT-25", "AT-04"});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);
    ScriptedSolver solver(&reachByX);
    RecordingContext ctx;
    const kin::WorkspaceSampleRun run =
        kin::runWorkspaceSampling(view, makeQuery(&solver), ctx);
    const WorkspacePointCloudRenderData cloud = kin::buildWorkspacePointCloud(run);

    // 导出值行（PNG 呈现侧数据源——数据面义务；PNG 文件生成归 ui）：
    // 表头字段序固定；两次生成逐位一致（确定性）；行数＝点数＋表头。
    const std::string csv1 = kin::workspacePointCloudExportCsv(cloud);
    const std::string csv2 = kin::workspacePointCloudExportCsv(cloud);
    EXPECT_EQ(csv1, csv2);

    const std::string header =
        "sample-index,x-m,y-m,z-m,state,collision,has-solution,manipulability,"
        "condition-number,min-joint-margin,position-residual-m,"
        "orientation-residual-rad\n";
    EXPECT_EQ(csv1.substr(0, header.size()), header);
    EXPECT_EQ(std::count(csv1.begin(), csv1.end(), '\n'),
              static_cast<int>(cloud.points.size()) + 1);
    // 行内容抽检（首行——sampleIndex 0：格心 (0.1,0.1,0.125)，x≤0.5
    // → Reached；%.17g 数值文本不逐字断言（CRT 指数格式差异——脆弱
    // 断言），只锚定序号/类别/解标记/脚本标量值的字段级词表）。
    const std::size_t line1End = csv1.find('\n', header.size());
    ASSERT_NE(line1End, std::string::npos);
    const std::string line1 = csv1.substr(header.size(), line1End - header.size());
    EXPECT_EQ(line1.substr(0, 2), "0,");                       // 序号段
    EXPECT_NE(line1.find(",reached,"), std::string::npos);      // 状态 token
    EXPECT_NE(line1.find(",not-evaluated,"), std::string::npos); // 碰撞 token
    EXPECT_NE(line1.find(",1,"), std::string::npos);            // 有解标记
    EXPECT_NE(line1.find(",0.25,"), std::string::npos);         // 可操作度
    EXPECT_NE(line1.find(",4,"), std::string::npos);            // 条件数
    EXPECT_NE(line1.find(",0.75,"), std::string::npos);         // 最小余量
}

TEST(KinWorkspace, RenderVocabularyTokens_WP15T14_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-09"},
                  std::vector<std::string>{"AT-25"});

    // 呈现词表三组 token（图例/导出行的稳定键——禁第二处字面量的
    // 词表面断言）：着色七值/投影三值/渲染模式两值（散点/包络——
    // Envelope 只是模式词表承载，包络几何估算归 KIN-10/T15）。
    EXPECT_EQ(kin::workspaceColorModeToken(WorkspaceColorMode::ByState), "by-state");
    EXPECT_EQ(kin::workspaceColorModeToken(WorkspaceColorMode::ByManipulability),
              "by-manipulability");
    EXPECT_EQ(kin::workspaceColorModeToken(WorkspaceColorMode::ByConditionNumber),
              "by-condition-number");
    EXPECT_EQ(kin::workspaceColorModeToken(WorkspaceColorMode::ByMinJointMargin),
              "by-min-joint-margin");
    EXPECT_EQ(kin::workspaceColorModeToken(WorkspaceColorMode::ByPositionResidual),
              "by-position-residual");
    EXPECT_EQ(kin::workspaceColorModeToken(WorkspaceColorMode::ByOrientationResidual),
              "by-orientation-residual");
    EXPECT_EQ(kin::workspaceColorModeToken(WorkspaceColorMode::ByCollision),
              "by-collision");

    EXPECT_EQ(kin::workspaceProjectionPlaneToken(WorkspaceProjectionPlane::XY), "xy");
    EXPECT_EQ(kin::workspaceProjectionPlaneToken(WorkspaceProjectionPlane::XZ), "xz");
    EXPECT_EQ(kin::workspaceProjectionPlaneToken(WorkspaceProjectionPlane::YZ), "yz");

    EXPECT_EQ(kin::workspaceRenderStyleToken(kin::WorkspaceRenderStyle::Scatter),
              "scatter");
    EXPECT_EQ(kin::workspaceRenderStyleToken(kin::WorkspaceRenderStyle::Envelope),
              "envelope");

    // 碰撞三态 token（ByCollision 类别键——越界值保守缺省"未评价"）。
    EXPECT_EQ(kin::workspaceCollisionToken(0U), "collision-free");
    EXPECT_EQ(kin::workspaceCollisionToken(1U), "in-collision");
    EXPECT_EQ(kin::workspaceCollisionToken(2U), "not-evaluated");
    EXPECT_EQ(kin::workspaceCollisionToken(9U), "not-evaluated");
}

// =====================================================================
// ACC4——可视化点回写仅改会话姿态（KIN-06——KinSessionPose 唯一写点、
// 零修订、不入身份）
// =====================================================================

TEST(KinWorkspace, PointWritebackSessionPoseOnly_WP15T14_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-09", "KIN-06"},
                  std::vector<std::string>{"AT-25", "AT-04", "V-18"});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);
    ScriptedSolver solver(&reachByX);
    RecordingContext ctx;
    const kin::WorkspaceSampleRun run =
        kin::runWorkspaceSampling(view, makeQuery(&solver), ctx);

    // 找一个 Reached 样本（有最佳解——回写源）。
    std::uint64_t reachedIndex = UINT64_MAX;
    for (const kin::WorkspaceSampleResult& r : run.results) {
        if (r.state == SampleState::Reached && r.bestSolution.has_value()) {
            reachedIndex = r.sampleIndex;
            break;
        }
    }
    ASSERT_NE(reachedIndex, UINT64_MAX);

    // 回写值（RenderPointWriteback——结构性只有 q＋签名两成员：类型
    // 层面不存在产生修订/失效/入身份的通道，KIN-06 结构保证）。
    const kin::RenderPointWriteback wb =
        kin::workspacePointWriteback(run, reachedIndex);
    EXPECT_EQ(wb.q, std::vector<double>({0.1, -0.2}));
    EXPECT_FALSE(wb.configurationSignature.empty());

    // 唯一写点消费（writebackToSessionPose——KinSessionPose 纯值容器；
    // 零修订零失效零事件——V-18 的会话面）。
    kin::KinSessionPose session;
    ASSERT_FALSE(session.isSet());
    kin::writebackToSessionPose(session, wb);
    EXPECT_TRUE(session.isSet());
    EXPECT_EQ(session.jointConfiguration(), std::vector<double>({0.1, -0.2}));

    // 无解点回写拒绝（fail-fast——UI 层应禁用无解点回写入口）。
    std::uint64_t unreachableIndex = UINT64_MAX;
    for (const kin::WorkspaceSampleResult& r : run.results) {
        if (r.state == SampleState::Unreachable) {
            unreachableIndex = r.sampleIndex;
            break;
        }
    }
    ASSERT_NE(unreachableIndex, UINT64_MAX);
    EXPECT_THROW(kin::workspacePointWriteback(run, unreachableIndex),
                 std::invalid_argument);
    // 越界样本拒绝。
    EXPECT_THROW(kin::workspacePointWriteback(run, 99999U),
                 std::invalid_argument);
}

// =====================================================================
// ACC5——R2 边界：探索式结果不进 Must 区域判定/必验覆盖（结构保证）；
// 碰撞证据按统一策略（零第二布尔）；计划身份会话内溯源
// =====================================================================

TEST(KinWorkspace, PlanIdentityDeterministicAndSeedLayered_WP15T14_ACC5)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-09", "KIN-04"},
                  std::vector<std::string>{"AT-25"});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);
    ScriptedSolver solver(&reachByX);

    // 同查询同计划身份（确定性）；不同区域/计数不同身份（身份对参数
    // 计算而非对枚举列表——大样本集不枚举入身份）。
    const kin::SamplingPlan planA =
        kin::workspaceSamplingPlan(makeQuery(&solver));
    const kin::SamplingPlan planA2 =
        kin::workspaceSamplingPlan(makeQuery(&solver));
    EXPECT_EQ(planA.planContentIdentity, planA2.planContentIdentity);
    EXPECT_FALSE(planA.regionObjectId.isValid());  // 探索式无 req 区域——保留值

    WorkspaceSamplingQuery qOther = makeQuery(&solver);
    qOther.position.gridCounts = {4U, 5U, 4U};
    const kin::SamplingPlan planB = kin::workspaceSamplingPlan(qOther);
    EXPECT_NE(planA.planContentIdentity, planB.planContentIdentity);

    // 身份分层：种子变化不改计划内容身份（种子在样本集身份面——
    // §7.2 公式同构分面），但改样本集身份。
    WorkspaceSamplingQuery qSeedB = makeQuery(&solver, 43U);
    const kin::SamplingPlan planSeedB = kin::workspaceSamplingPlan(qSeedB);
    EXPECT_EQ(planA.planContentIdentity, planSeedB.planContentIdentity);
    RecordingContext ctx;
    const kin::WorkspaceSampleRun runSeedB =
        kin::runWorkspaceSampling(view, qSeedB, ctx);
    EXPECT_NE(runSeedB.sampleSetIdentity,
              kin::sampleSetIdentity(planA.planContentIdentity,
                                     kin::RegionSamplingBudget{42U, 1U}));
}

TEST(KinWorkspace, CollisionViaPolicySessionNotBoolean_WP15T14_ACC5)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-09", "UX-08", "ARC-05"},
                  std::vector<std::string>{"AT-25", "V13-01"});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);
    ScriptedSolver solver(&reachByX);

    // 无会话（策略未注入）→ 逐样本 collisionNotEvaluated（检查不在
    // 范围——绝不解读为无碰撞）；点云类别恒"未评价"。
    RecordingContext ctx;
    const kin::WorkspaceSampleRun run =
        kin::runWorkspaceSampling(view, makeQuery(&solver), ctx);
    for (const kin::WorkspaceSampleResult& r : run.results) {
        EXPECT_TRUE(r.collisionNotEvaluated);
    }
    const WorkspacePointCloudRenderData cloud = kin::buildWorkspacePointCloud(run);
    for (const auto& p : cloud.points) {
        EXPECT_EQ(p.collisionClass, std::uint8_t{2});
    }

    // 结构边界（V13-01 的类型面）：查询无碰撞布尔字段——碰撞检查的
    // 在场性只由 collisionSession 指针承载（会话在场即启用——统一
    // 工程策略）。本断言以注释锚定 review：WorkspaceSamplingQuery
    // 成员清单中不存在任何"collision enabled"语义的 bool 成员。
    //（静态可断言面归 BuildRedLineTest 的产品扫描域；此处以运行面
    // 行为断言＋文档登记承载。）
}

namespace {

/// 最小碰撞会话替身（仅承载"会话在场"——Workspace 把会话指针透传给
/// 求解器；ScriptedSolver 按指针在场性置 collisionNotEvaluated，不调用
/// evaluate，故本替身评估体不可达。接口形状漂移由 override 编译期钉住）。
class PresentCollisionSession final : public kin::IKinCollisionSession {
public:
    kin::IkCollisionVerdict evaluate(const std::vector<double>& q) const override
    {
        (void)q;
        return kin::IkCollisionVerdict{};  // 不可达面（见类注）
    }
};

}  // namespace

/**
 * R3 审计 A7 回归钉（audit-r3-fixes-1 返工批 O-4——2026-10-15 修复
 * bf531476 的"评价凭据分派"新支钉扎）：碰撞会话在场时，
 * buildWorkspacePointCloud 的碰撞三态按"评价凭据"分派——
 *   - 携带已评价解（bestSolution 在场——会话在场时求解有解必经碰撞检
 *     查的凭据）→ 0（已检查无碰撞）；
 *   - 无凭据样本（搜索未果结局 2——无解、且因会话在场而不携带
 *     collisionNotEvaluated 标记）→ 2（保守未评价）。
 * 原缺陷形态把无凭据样本落兜底 else 误标 0（"collision-free"写入导出
 * CSV）——违反 Workspace.hpp 与映射自身注释的"绝不把未知呈现为无碰撞"
 * 契约（KIN-05 保守读法）。既有两处 collisionClass 断言均走"无会话→
 * collisionNotEvaluated→2"支，本用例补会话在场下的两新支直钉。
 */
TEST(KinWorkspace, PointCloudCollisionDispatchByEvaluationCredential_R3_A7)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-09", "KIN-05"},
                  std::vector<std::string>{"AT-25", "R3-A7"});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);
    // 按位置 z 分流：z>0.6 → 搜索未果（MultiInitNoConvergence——零候选
    // 无解轨）；其余可达（SolutionsFound——有解轨）。会话在场替身使逐
    // 样本 collisionNotEvaluated 保持 false（ScriptedSolver 按会话指针
    // 在场性置位——真实求解器行为同款模拟，KIN-05 语义来源）。
    ScriptedSolver solver(&dataInsufficientByZ);
    PresentCollisionSession session;

    RecordingContext ctx;
    WorkspaceSamplingQuery q = makeQuery(&solver);
    q.collisionSession = &session;         // 统一策略注入面（V13-01——零布尔）
    q.position.gridCounts = {5U, 1U, 3U};  // z 格心 {1/6, 1/2, 5/6}——两轨均非空
    const kin::WorkspaceSampleRun run = kin::runWorkspaceSampling(view, q, ctx);
    ASSERT_FALSE(run.results.empty());

    // 样本面自证：两轨都被真实触达（新支 0 与钉扎面 2 均非空集——防空
    // 断言恒绿的形态）。
    std::uint64_t withSolution = 0;
    std::uint64_t noCredential = 0;
    for (const kin::WorkspaceSampleResult& r : run.results) {
        ASSERT_FALSE(r.collisionNotEvaluated)
            << "会话在场样本不得携带未评价标记（KIN-05 语义来源前提）";
        if (r.bestSolution.has_value()) {
            ++withSolution;
            EXPECT_EQ(r.state, SampleState::Reached);
        } else {
            ++noCredential;
            EXPECT_EQ(r.state, SampleState::DataInsufficient);  // 结局 2→2/3 映射
        }
    }
    EXPECT_GT(withSolution, std::uint64_t{0});
    EXPECT_GT(noCredential, std::uint64_t{0});

    // 三态分派钉：有解→0（已评价凭据）；无凭据→2（保守未评价——原缺陷
    // 形态此处为 0，变异还原即红）。
    const WorkspacePointCloudRenderData cloud = kin::buildWorkspacePointCloud(run);
    ASSERT_EQ(cloud.points.size(), run.results.size());
    for (std::size_t i = 0; i < cloud.points.size(); ++i) {
        if (run.results[i].bestSolution.has_value()) {
            EXPECT_EQ(cloud.points[i].collisionClass, std::uint8_t{0})
                << "样本 " << i << "（有解＝已评价凭据——0 才表示已检查无碰撞）";
        } else {
            EXPECT_EQ(cloud.points[i].collisionClass, std::uint8_t{2})
                << "样本 " << i << "（无凭据——保守未评价，KIN-05）";
        }
    }
}

TEST(KinWorkspace, RerunAfterCancelSameSampleSet_WP15T14_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-09", "NFR-REL-03"},
                  std::vector<std::string>{"AT-25", "V-22"});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);
    ScriptedSolver solver(&reachByX);

    // 取消后重跑同查询同种子→同一样本集（§7.2"重跑同一样本集"——
    // NotRun 的可续评前提；身份一致＝对账锚）。
    CancelAfterWatermarkSink sink;
    sink.arm(1U);
    RecordingContext ctxCancelled;
    ctxCancelled.attach(&sink);
    WorkspaceSamplingQuery q = makeQuery(&solver);
    q.maxBatchSize = 40U;
    q.checkpointSink = &sink;
    const kin::WorkspaceSampleRun runCancelled =
        kin::runWorkspaceSampling(view, q, ctxCancelled);
    EXPECT_TRUE(runCancelled.cancelled);

    RecordingContext ctxFull;
    const kin::WorkspaceSampleRun runFull =
        kin::runWorkspaceSampling(view, q, ctxFull);
    EXPECT_FALSE(runFull.cancelled);

    // 同集同序（取消运行的样本集与完整运行逐位一致）。
    ASSERT_EQ(runCancelled.samples.samples.size(), runFull.samples.samples.size());
    for (std::size_t i = 0; i < runFull.samples.samples.size(); ++i) {
        EXPECT_EQ(runCancelled.samples.samples[i].position,
                  runFull.samples.samples[i].position);
    }
    EXPECT_EQ(runCancelled.sampleSetIdentity, runFull.sampleSetIdentity);
}
