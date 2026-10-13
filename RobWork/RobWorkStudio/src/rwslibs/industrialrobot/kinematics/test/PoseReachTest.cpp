/**
 * @file   PoseReachTest.cpp
 * @brief  独立位姿可达性分析用例组（KinPoseReachability，T16 批）——
 *         计划预览（IK 目标数）、方向覆盖率报告（存在性口径＋五态聚合
 *         优先级）、后台运行/协作取消（partial 如实＋watermark 保留）、
 *         方向集与确定性口径（§7.2 黄金设施复用＋同种子重跑逐位一致）、
 *         评估器形态（零证据行/KIN-NO-TCP 零素材/载荷编码确定性）。
 *         任务契约 WP-15-T16 acceptance 1~4 逐条具名自证。
 *
 * 设计依据：
 *   - units/kinematics.md §7.3（R2 边界）、§7.2（黄金设施——方向集/
 *     roll 均分/位姿组装/状态词表）、§8.3/§8.4（检查点/确定性）、
 *     §10.2（EX-T03/EX-T05 本单元侧观测点同口径）
 *   - REQUIREMENTS KIN-11（位置列表×方向采样×工具滚转采样、计划预览、
 *     后台运行、协作取消、方向覆盖率报告）、AT-26（可达性计划预览与
 *     方向覆盖率报告）
 *   - 黄金几何：KinFkFixture 二连杆模型（视图真值面——脚本求解下模型
 *     仅承载自由度/TCP 解析，RegionCoverageTest 同款口径）
 *   - 任务契约 tasks/foundation/WP-15-T16.json
 *
 * 测试策略：可控求解器替身（SequenceSolver——按调用序脚本结局：目标
 * 枚举序黄金锁定使"第 k 次调用"与"第 k 个目标"一一对应，结局编排
 * 精确可控；记录请求目标供接线断言，不调用取消探针——使取消查询次数
 * 确定可数）；计算语义经 runPoseReachabilityComputation 直调断言
 * （NFR-MNT-01 计算库可直调），评估器素材形状经 evaluate 端到端断言。
 */

#include "KinFkFixture.hpp"

#include <sdurws/ird/evidence/Evaluator.hpp>
#include <sdurws/ird/kinematics/DiagCodes.hpp>  // kKin* 码值常量（禁拼码断言面）
#include <sdurws/ird/kinematics/Evaluators.hpp>
#include <sdurws/ird/kinematics/Ik.hpp>
#include <sdurws/ird/kinematics/PoseReach.hpp>
#include <sdurws/ird/kinematics/Sampling.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace sdurws::ird::kinematics::testfixture;
namespace evidence = sdurws::ird::evidence;
namespace kin = sdurws::ird::kinematics;
using kin::PoseReachabilityQuery;
using kin::PoseReachTarget;
using kin::PoseReachTargetResult;
using kin::SampleState;

namespace {

// =====================================================================
// 小工具（脚手架——非断言面）
// =====================================================================

/// π（rad——黄金设施黄金值的测试侧对照常量；与产品实现同一数学量，
/// 独立书写以防常量漂移静默）。
constexpr double kPi = 3.14159265358979323846;

/// 均匀结局脚本（n 个同结局——SequenceSolver 脚本便捷构造；显式小工具
/// 防两参构造歧义——脚本表只有 vector 单参形态）。
std::vector<kin::IkOutcomeKind> uniformScript(std::size_t n, kin::IkOutcomeKind kind)
{
    return std::vector<kin::IkOutcomeKind>(n, kind);
}

/// 分析查询便捷构造（默认：3 位置×2 方向×2 roll＝12 目标——三批
/// 黄金规模；solver 必注入——脚本求解下模型仅承载自由度/TCP）。
PoseReachabilityQuery makeQuery(std::vector<rw::math::Vector3D<double>> positions,
                                kin::IIkSolver* solver, std::uint32_t directions = 2U,
                                std::uint32_t rolls = 2U, std::uint64_t seed = 42U)
{
    PoseReachabilityQuery q;
    q.positions = std::move(positions);
    q.directionSamples = directions;
    q.rollSamples = rolls;
    q.defaultTcp.toolObject = idFrom<core::ObjectId>("kin-tool");
    q.referenceQ = {0.0, 0.0};
    q.initialStrategy = kin::InitialValueStrategy::ReferenceQ;
    q.initialValuesCount = 1U;
    q.iterationLimit = 16U;
    q.seed = seed;
    q.solver = solver;
    return q;
}

/// 默认位置列表（三位置黄金编排：p0 半径内全可达、p1 半径外解析界限、
/// p2 半径内混合——脚本结局编排的坐标面；单位 m、基座系 {B}）。
std::vector<rw::math::Vector3D<double>> threePositions()
{
    return {rw::math::Vector3D<double>(0.4, 0.0, 0.0),
            rw::math::Vector3D<double>(2.5, 0.0, 0.0),
            rw::math::Vector3D<double>(0.8, 0.0, 0.0)};
}

/// 请求壳（身份面固定派生——绑定断言面；mode=Quick——本通道
/// supportedModes 单值，AT-26 Quick/估算性质）。
evidence::EvaluationRequest makeRequest()
{
    evidence::EvaluationRequest req;
    req.mode = core::EvaluationMode::Quick;
    req.snapshot.snapshotId.bytes = digestOf("kin-pr-snap");
    req.slice.sliceId.bytes = digestOf("kin-pr-slice");
    req.task.attempt.value = 5U;
    return req;
}

// =====================================================================
// 序列脚本求解器替身（按调用序出结局——目标枚举序黄金锁定使调用序＝
// 目标序，结局编排精确可控；记录逐请求目标/容差供接线断言；不调用
// 取消探针——取消查询次数确定可数，RegionCoverageTest ScriptedSolver
// 的序表变体）
// =====================================================================

class SequenceSolver final : public kin::IIkSolver {
public:
    /// 结局脚本（按调用序循环消费——脚本耗尽后恒 SolutionsFound，
    /// 便于只关注前缀编排的用例）。
    explicit SequenceSolver(std::vector<kin::IkOutcomeKind> script)
        : m_script(std::move(script))
    {
    }

    kin::IkOutcome solve(const kin::IkRequest& request) const override
    {
        // 接线断言面：记录目标位置与双容差（逐目标完整位姿 IK 的
        // 请求形状核查素材）。
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_positions.push_back(request.targetInBase.P());
            m_orientationTolerances.push_back(request.orientationTolerance);
        }

        kin::IkOutcome outcome;
        const std::size_t k = m_call.fetch_add(1U, std::memory_order_relaxed);
        outcome.outcomeKind = k < m_script.size() ? m_script[k]
                                                  : kin::IkOutcomeKind::SolutionsFound;
        // 组装器 presence 不变式的脚本侧满足（本组用例不消费解面——
        // RegionCoverageTest 同款）。
        if ((outcome.outcomeKind == kin::IkOutcomeKind::SolutionsFound
             || outcome.outcomeKind == kin::IkOutcomeKind::PartialCollision)
            && outcome.solutionSet.solutions.empty()) {
            kin::KinematicSolution s;
            s.q = {0.1, -0.2};               // 自由度 2（二连杆夹具）
            s.positionResidual = 1e-9;       // m（< 容差）
            s.orientationResidual = 1e-9;    // rad
            outcome.solutionSet.solutions.push_back(s);
        }
        if (outcome.outcomeKind == kin::IkOutcomeKind::MultiInitNoConvergence
            || outcome.outcomeKind == kin::IkOutcomeKind::AllCandidatesFiltered) {
            kin::IkSearchRecord record;
            record.searchBudgetUsed = 10U;
            record.initialGuessesTried = 1U;
            outcome.solutionSet.searchRecord = record;
        }
        return outcome;
    }

    const std::vector<rw::math::Vector3D<double>>& requestedPositions() const
    {
        return m_positions;
    }
    const std::vector<double>& requestedOrientationTolerances() const
    {
        return m_orientationTolerances;
    }

private:
    std::vector<kin::IkOutcomeKind> m_script;
    mutable std::atomic<std::size_t> m_call{0};
    mutable std::mutex m_mutex;
    mutable std::vector<rw::math::Vector3D<double>> m_positions;
    mutable std::vector<double> m_orientationTolerances;
};

// =====================================================================
// 取消联动替身（第一批 watermark 后置取消旗标——EX-T03 行为断言：
// partial 停止派发、watermark 保留可续；RegionCoverageTest 同款自持）
// =====================================================================

class CancelAfterWatermarkSink final : public kin::IBatchCheckpointSink {
public:
    void batchWatermark(std::uint64_t completedBatchCount,
                        std::uint64_t totalBatchCount) override
    {
        watermarks.emplace_back(completedBatchCount, totalBatchCount);
        if (completedBatchCount >= m_cancelAfter) {
            m_cancelled = true;
        }
    }

    void arm(std::uint64_t cancelAfter) { m_cancelAfter = cancelAfter; }
    bool cancelRequested() const { return m_cancelled; }

    std::vector<std::pair<std::uint64_t, std::uint64_t>> watermarks;

private:
    std::uint64_t m_cancelAfter = 0U;
    bool m_cancelled = false;
};

/// 可取消＋进度记录上下文（取消旗标自检查点替身读取；进度逐条记录
/// ——EX-T05 断言面；RegionCoverageTest CancellableContext 的记录变体）。
class RecordingCancellableContext final : public evidence::IEvaluationContext {
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
// ACC1（AT-26）——计划预览：位置×方向×roll 的 IK 目标数
// =====================================================================

/**
 * 本用例验证（KIN-11/AT-26）：计划预览的 IK 目标数＝|位置|×方向数×
 * roll 数——执行前评估目标规模的独立入口（3×4×2=24 黄金数字）。
 */
TEST(KinPoseReachability, PlanPreviewTargetCount_WP15T16_ACC1_AT26)
{
    PoseReachabilityQuery q = makeQuery(threePositions(), nullptr, 4U, 2U);

    const kin::PoseReachabilityPlan plan = kin::planPoseReachability(q);
    EXPECT_EQ(3U, plan.positionCount);
    EXPECT_EQ(4U, plan.directionCount);
    EXPECT_EQ(2U, plan.rollCount);
    EXPECT_EQ(24U, plan.ikTargetCount);  // 3 位置×4 方向×2 roll——黄金数字
}

/**
 * 本用例验证（KIN-11）：计划预览与目标生成/报告三面同源——预览的
 * ikTargetCount＝生成集 plannedTargets＝执行后报告分母（分母一致防
 * 两套真值漂移）。
 */
TEST(KinPoseReachability, PlanPreviewMatchesGeneration_WP15T16_ACC1_KIN11)
{
    SequenceSolver solver(uniformScript(12U, kin::IkOutcomeKind::SolutionsFound));
    PoseReachabilityQuery q = makeQuery(threePositions(), &solver, 2U, 2U);
    TestView view(twoLinkModel());
    NoopContext ctx;

    // 预览与生成同源：ikTargetCount＝plannedTargets＝目标条数。
    const kin::PoseReachabilityPlan plan = kin::planPoseReachability(q);
    const kin::PoseReachTargetSet targets = kin::generatePoseReachTargets(q);
    ASSERT_EQ(plan.ikTargetCount, targets.plannedTargets);
    ASSERT_EQ(plan.ikTargetCount, targets.targets.size());

    // 执行面同源：全 SolutionsFound 脚本下的报告分母同值（预览/生成/
    // 报告三面一致——computePoseReachCoverage 全局守恒式终检的面）。
    const kin::PoseReachabilityComputation c =
        kin::runPoseReachabilityComputation(view, q, makeRequest(), ctx);
    EXPECT_EQ(plan.ikTargetCount, c.report.targets.planned);
    EXPECT_EQ(12U, c.report.targets.planned);
    EXPECT_EQ(12U, c.report.targets.reached);
}

/**
 * 本用例验证（NFR-COR-03）：装配契约违约 fail-fast 矩阵——空位置列表/
 * 零方向/零 roll/非有限位置/SeededRandom＋seed=0/容差非法/批大小 0
 * 一律 std::invalid_argument，不钳制不置零。
 */
TEST(KinPoseReachability, PlanPreviewRejectsInvalidQuery_WP15T16_KIN11_NFRCOR03)
{
    // 空位置列表：独立分析无目标无意义——调用方组装错误（PoseReach.hpp
    // 字段注的装配校验位阶决策）。
    EXPECT_THROW(kin::planPoseReachability(makeQuery({}, nullptr)),
                 std::invalid_argument);

    // 零方向/零 roll：方向覆盖率分母为 0 的"分析"无判定意义。
    EXPECT_THROW(kin::planPoseReachability(makeQuery(threePositions(), nullptr, 0U, 2U)),
                 std::invalid_argument);
    EXPECT_THROW(kin::planPoseReachability(makeQuery(threePositions(), nullptr, 2U, 0U)),
                 std::invalid_argument);

    // 非有限位置（NaN/Inf——单位 m、基座系 {B}）：拒绝入算不置零。
    std::vector<rw::math::Vector3D<double>> nanPos{
        rw::math::Vector3D<double>(0.4, std::nan(""), 0.0)};
    EXPECT_THROW(kin::planPoseReachability(makeQuery(nanPos, nullptr)),
                 std::invalid_argument);

    // 容差非法（≤0——附录 D 第 1 项语义）。
    PoseReachabilityQuery badTol = makeQuery(threePositions(), nullptr);
    badTol.positionTolerance = 0.0;
    EXPECT_THROW(kin::planPoseReachability(badTol), std::invalid_argument);

    // 批大小 0（执行协作参数——T06 同款校验位阶）。
    PoseReachabilityQuery badBatch = makeQuery(threePositions(), nullptr);
    badBatch.maxBatchSize = 0U;
    EXPECT_THROW(kin::planPoseReachability(badBatch), std::invalid_argument);

    // SeededRandom＋seed=0：确定性种子 0 非法（I-KIN-4——不做 0→1
    // 静默替换）；其余策略 seed=0 合法（ReferenceQ 不消费种子）。
    PoseReachabilityQuery badSeed = makeQuery(threePositions(), nullptr);
    badSeed.initialStrategy = kin::InitialValueStrategy::SeededRandom;
    badSeed.seed = 0U;  // 显式清零——makeQuery 缺省 seed=42 是合法值
    EXPECT_THROW(kin::planPoseReachability(badSeed), std::invalid_argument);
    PoseReachabilityQuery okSeed = makeQuery(threePositions(), nullptr);
    okSeed.initialStrategy = kin::InitialValueStrategy::ReferenceQ;
    okSeed.seed = 0U;
    EXPECT_NO_THROW(kin::planPoseReachability(okSeed));
}

/**
 * 本用例验证（D-KIN-4/T05 两分口径）：执行面装配校验——referenceQ
 * 维度须＝设备自由度（二连杆 DOF=2）；TCP 选择键不命中＝装配期
 * fail-fast（KinTypes.hpp TcpRef 解析规则——禁模糊匹配）。
 */
TEST(KinPoseReachability, ExecutionRejectsViewCoupledViolations_WP15T16_DKIN4)
{
    SequenceSolver solver({});
    TestView view(twoLinkModel());
    NoopContext ctx;

    // referenceQ 维度不符（3 ≠ DOF 2）。
    PoseReachabilityQuery badDof = makeQuery(threePositions(), &solver);
    badDof.referenceQ = {0.0, 0.0, 0.0};
    EXPECT_THROW(kin::runPoseReachabilityComputation(view, badDof, makeRequest(), ctx),
                 std::invalid_argument);

    // referenceQ 非有限分量。
    PoseReachabilityQuery badFin = makeQuery(threePositions(), &solver);
    badFin.referenceQ = {0.0, std::nan("")};
    EXPECT_THROW(kin::runPoseReachabilityComputation(view, badFin, makeRequest(), ctx),
                 std::invalid_argument);

    // tcpKey 不命中（非空键须与工具 localName 精确相等——"tool_1"是
    // 夹具工具 localName，"nope"不命中）。
    PoseReachabilityQuery badKey = makeQuery(threePositions(), &solver);
    badKey.defaultTcp.tcpKey = "nope";
    EXPECT_THROW(kin::runPoseReachabilityComputation(view, badKey, makeRequest(), ctx),
                 std::invalid_argument);

    // 合法查询不再抛（同 query 同形状——校验通过面自证）。
    PoseReachabilityQuery okKey = makeQuery(threePositions(), &solver);
    okKey.defaultTcp.tcpKey = "tool_1";
    EXPECT_NO_THROW(kin::runPoseReachabilityComputation(view, okKey, makeRequest(), ctx));
}

// =====================================================================
// ACC4——方向集与确定性口径（§7.2 黄金设施复用＋同种子重跑一致）
// =====================================================================

/**
 * 本用例验证（KIN-11 acceptance 4/§7.3 复用义务）：目标集黄金——枚举
 * 序 targetIndex＝(i·D＋j)·R＋r（位置最慢×方向居中×roll 最快）；逐
 * 目标位姿＝poseFromDirectionRoll(generateDirections(D)[j],
 * generateRolls(R)[r], positions[i])——生成零自有采样数学，黄金设施
 * 复用直证（任何黄金漂移即时可见）。
 */
TEST(KinPoseReachability, TargetGenerationGoldenFacility_WP15T16_ACC4_KIN11)
{
    PoseReachabilityQuery q = makeQuery(threePositions(), nullptr, 3U, 2U);
    const kin::PoseReachTargetSet targets = kin::generatePoseReachTargets(q);

    // 黄金设施产出（与生成同源函数直取——对照基准）。
    const std::vector<rw::math::Vector3D<double>> directions =
        kin::generateDirections(3U);
    const std::vector<double> rolls = kin::generateRolls(2U);
    ASSERT_EQ(3U, directions.size());
    ASSERT_EQ(2U, rolls.size());

    // 枚举序与逐目标黄金对照（18 目标全量核对——3 位置×3 方向×2 roll；
    // 不抽样）。
    ASSERT_EQ(18U, targets.targets.size());
    for (std::uint32_t i = 0; i < 3U; ++i) {
        for (std::uint32_t j = 0; j < 3U; ++j) {
            for (std::uint32_t r = 0; r < 2U; ++r) {
                const std::uint64_t idx =
                    (static_cast<std::uint64_t>(i) * 3U + j) * 2U + r;
                const PoseReachTarget& t = targets.targets[idx];
                EXPECT_EQ(idx, t.targetIndex);
                EXPECT_EQ(i, t.positionIndex);
                EXPECT_EQ(j, t.directionIndex);
                EXPECT_EQ(r, t.rollIndex);
                // 位置列表原值透传（不重排不改写——基座系 {B}，m）。
                EXPECT_EQ(q.positions[i][0], t.position[0]);
                EXPECT_EQ(q.positions[i][1], t.position[1]);
                EXPECT_EQ(q.positions[i][2], t.position[2]);
                // 方向/roll 与黄金生成逐分量一致（黄金锁定面）。
                EXPECT_EQ(directions[j][0], t.direction[0]);
                EXPECT_EQ(directions[j][1], t.direction[1]);
                EXPECT_EQ(directions[j][2], t.direction[2]);
                EXPECT_EQ(rolls[r], t.roll);
                // 位姿＝黄金组装直算（T_base_tcp，基座系 {B}）——逐分量。
                const rw::math::Transform3D<double> expected =
                    kin::poseFromDirectionRoll(directions[j], rolls[r],
                                               q.positions[i]);
                for (int a = 0; a < 3; ++a) {
                    EXPECT_EQ(expected.P()[a], t.pose.P()[a]);
                    for (int b = 0; b < 3; ++b) {
                        EXPECT_EQ(expected.R()(a, b), t.pose.R()(a, b));
                    }
                }
            }
        }
    }
}

/**
 * 本用例验证（KIN-11 acceptance 4/NFR-COR-01）：同种子重跑结果一致——
 * 同 query 两跑的目标集/结果集逐位相等（确定性；D-KIN-6 同款纪律——
 * 方向集/roll 黄金确定性生成，seed 只进初值）。
 */
TEST(KinPoseReachability, DeterministicRerunIdentical_WP15T16_ACC4_NFRCOR01)
{
    // 脚本结局按调用序确定（12 目标编排一遍——两跑消费同一脚本形态；
    // SequenceSolver 每实例独立计数，两跑各建一个同脚本实例）。
    std::vector<kin::IkOutcomeKind> script(12U, kin::IkOutcomeKind::SolutionsFound);

    SequenceSolver solverA(script);
    PoseReachabilityQuery qA = makeQuery(threePositions(), &solverA);
    TestView view(twoLinkModel());
    NoopContext ctxA;
    const kin::PoseReachabilityComputation a =
        kin::runPoseReachabilityComputation(view, qA, makeRequest(), ctxA);

    SequenceSolver solverB(script);
    PoseReachabilityQuery qB = makeQuery(threePositions(), &solverB);
    NoopContext ctxB;
    const kin::PoseReachabilityComputation b =
        kin::runPoseReachabilityComputation(view, qB, makeRequest(), ctxB);

    // 目标集与结果集逐位相等（operator== 全字段——黄金确定性面）。
    ASSERT_EQ(a.targets.targets.size(), b.targets.targets.size());
    for (std::size_t k = 0; k < a.targets.targets.size(); ++k) {
        EXPECT_EQ(a.targets.targets[k].targetIndex, b.targets.targets[k].targetIndex);
        EXPECT_EQ(a.targets.targets[k].pose.P()[0], b.targets.targets[k].pose.P()[0]);
        EXPECT_EQ(a.targets.targets[k].pose.R()(0, 0), b.targets.targets[k].pose.R()(0, 0));
    }
    ASSERT_EQ(a.results.results.size(), b.results.results.size());
    for (std::size_t k = 0; k < a.results.results.size(); ++k) {
        EXPECT_TRUE(a.results.results[k] == b.results.results[k]) << "槽位 " << k;
    }
    // 覆盖报告逐字段相等（三位置覆盖表＋全局汇总）。
    ASSERT_EQ(a.perPosition.size(), b.perPosition.size());
    for (std::size_t k = 0; k < a.perPosition.size(); ++k) {
        EXPECT_TRUE(a.perPosition[k] == b.perPosition[k]) << "位置 " << k;
    }
    EXPECT_TRUE(a.report == b.report);
}

// =====================================================================
// ACC3（AT-26）——方向覆盖率报告（存在性口径＋五态聚合优先级）
// =====================================================================

/**
 * 本用例验证（KIN-11/AT-26）：方向覆盖率报告黄金计数——三位置编排
 * （p0 全可达→reachedDirections=D；p1 全解析界限→unreachableDirections
 * ＝D；p2 按 roll 交替 Found/NoConvergence→每方向 ∃Reached 即可达
 * （存在性口径）但 roll 粒度 DataInsufficient>0 ⇒ downgraded）。
 * 脚本结局按枚举序编排：p0 8 目标全 Found、p1 8 目标全 Bound、
 * p2 8 目标交替（r=0 Found、r=1 NoConvergence）。
 */
TEST(KinPoseReachability, DirectionCoverageReport_WP15T16_ACC3_AT26)
{
    // 结局脚本（2 方向×2 roll＝4 目标/位置；枚举序 r 最快）。
    std::vector<kin::IkOutcomeKind> script;
    script.insert(script.end(), 4U, kin::IkOutcomeKind::SolutionsFound);       // p0
    script.insert(script.end(), 4U, kin::IkOutcomeKind::AnalyticBoundExceeded); // p1
    for (std::uint32_t j = 0; j < 2U; ++j) {                                   // p2
        script.push_back(kin::IkOutcomeKind::SolutionsFound);        // r=0
        script.push_back(kin::IkOutcomeKind::MultiInitNoConvergence); // r=1
    }
    SequenceSolver solver(script);
    PoseReachabilityQuery q = makeQuery(threePositions(), &solver, 2U, 2U);
    TestView view(twoLinkModel());
    NoopContext ctx;

    const kin::PoseReachabilityComputation c =
        kin::runPoseReachabilityComputation(view, q, makeRequest(), ctx);

    // 完整执行（无取消）：不完整标记为假。
    ASSERT_EQ(12U, c.results.results.size());
    EXPECT_FALSE(c.report.incomplete);
    EXPECT_EQ(12U, c.report.targets.planned);

    // p0：全方向可达（存在性口径——每方向两 roll 均 Found）。
    ASSERT_EQ(3U, c.perPosition.size());
    EXPECT_EQ(0U, c.perPosition[0].positionIndex);
    EXPECT_EQ(2U, c.perPosition[0].plannedDirections);
    EXPECT_EQ(2U, c.perPosition[0].reachedDirections);
    EXPECT_EQ(0U, c.perPosition[0].unreachableDirections);
    EXPECT_EQ(0U, c.perPosition[0].dataInsufficientDirections);
    EXPECT_EQ(0U, c.perPosition[0].notRunDirections);
    EXPECT_EQ(4U, c.perPosition[0].poses.reached);
    EXPECT_FALSE(c.perPosition[0].downgraded);
    EXPECT_FALSE(c.perPosition[0].incomplete);

    // p1：全方向确定性不可达（每方向两 roll 均解析界限证明——方向级
    // Unreachable 的唯一聚合路径，C2③）。
    EXPECT_EQ(0U, c.perPosition[1].reachedDirections);
    EXPECT_EQ(2U, c.perPosition[1].unreachableDirections);
    EXPECT_EQ(4U, c.perPosition[1].poses.unreachable);
    EXPECT_FALSE(c.perPosition[1].downgraded);

    // p2：每方向 {r0 Found, r1 NoConvergence}——存在性口径判可达
    // （∃Reached），但 roll 粒度 DataInsufficient=2>0 ⇒ 降级（方向级
    // dataInsufficient=0——存在性已满足，聚合不重复计降级方向；每方向
    // 恰 1 Reached＋1 DataInsufficient ⇒ roll 粒度计数各 2）。
    EXPECT_EQ(2U, c.perPosition[2].reachedDirections);
    EXPECT_EQ(0U, c.perPosition[2].dataInsufficientDirections);
    EXPECT_EQ(2U, c.perPosition[2].poses.reached);
    EXPECT_EQ(2U, c.perPosition[2].poses.dataInsufficient);
    EXPECT_TRUE(c.perPosition[2].downgraded);
    EXPECT_FALSE(c.perPosition[2].incomplete);

    // 全局汇总（目标粒度合计＋标记传播：reached＝p0 4＋p2 2、
    // unreachable＝p1 4、dataInsufficient＝p2 2）。
    EXPECT_EQ(6U, c.report.targets.reached);
    EXPECT_EQ(4U, c.report.targets.unreachable);
    EXPECT_EQ(2U, c.report.targets.dataInsufficient);
    EXPECT_EQ(0U, c.report.targets.notRun);
    EXPECT_TRUE(c.report.downgraded);
    EXPECT_FALSE(c.report.incomplete);
}

/**
 * 本用例验证（KIN-11 方向聚合优先级）：混合态保守归并——
 * ① Unreachable＋DataInsufficient 混合方向 → dataInsufficient（无分子
 * 凭据且非全证明，C5/C8 绝不升格）；② 方向级聚合以存在性优先
 * （∃Reached 压倒一切混合）。
 */
TEST(KinPoseReachability, DirectionAggregateConservative_WP15T16_ACC3_KIN11)
{
    // 单位置×1 方向×2 roll：{Bound, NoConvergence} 混合 → 方向归
    // dataInsufficient（规则 4——保守，不产确定性不可达结论）。
    std::vector<kin::IkOutcomeKind> script{
        kin::IkOutcomeKind::AnalyticBoundExceeded,
        kin::IkOutcomeKind::MultiInitNoConvergence};
    SequenceSolver solver(script);
    PoseReachabilityQuery q = makeQuery({rw::math::Vector3D<double>(0.5, 0.0, 0.0)},
                                        &solver, 1U, 2U);
    TestView view(twoLinkModel());
    NoopContext ctx;

    const kin::PoseReachabilityComputation c =
        kin::runPoseReachabilityComputation(view, q, makeRequest(), ctx);

    ASSERT_EQ(1U, c.perPosition.size());
    EXPECT_EQ(1U, c.perPosition[0].plannedDirections);
    EXPECT_EQ(0U, c.perPosition[0].reachedDirections);
    EXPECT_EQ(0U, c.perPosition[0].unreachableDirections);   // 混合不判确定性
    EXPECT_EQ(1U, c.perPosition[0].dataInsufficientDirections);
    EXPECT_EQ(0U, c.perPosition[0].notRunDirections);
    EXPECT_TRUE(c.perPosition[0].downgraded);
    EXPECT_TRUE(c.report.downgraded);
}

/**
 * 本用例验证（KIN-11 目标接线/§7.2 黄金组装消费）：逐目标 IK 请求的
 * 目标位姿＝目标集黄金位姿（poseFromDirectionRoll 产出直供
 * IkRequest.targetInBase）＋姿态容差逐目标生效（完整位姿目标——本
 * 通道无"姿态无约束"形态）。
 */
TEST(KinPoseReachability, TargetPoseWiring_WP15T16_KIN11)
{
    SequenceSolver solver({});
    PoseReachabilityQuery q = makeQuery(threePositions(), &solver, 2U, 2U);
    q.orientationTolerance = 1e-4;  // rad——非默认值，断言透传
    TestView view(twoLinkModel());
    NoopContext ctx;

    const kin::PoseReachabilityComputation c =
        kin::runPoseReachabilityComputation(view, q, makeRequest(), ctx);

    // 逐请求目标位姿与目标集黄金位姿一致（请求序＝目标枚举序）。
    const kin::PoseReachTargetSet targets = kin::generatePoseReachTargets(q);
    ASSERT_EQ(targets.targets.size(), solver.requestedPositions().size());
    for (std::size_t k = 0; k < targets.targets.size(); ++k) {
        EXPECT_EQ(targets.targets[k].pose.P()[0],
                  solver.requestedPositions()[k][0]);
        EXPECT_EQ(targets.targets[k].pose.P()[1],
                  solver.requestedPositions()[k][1]);
        EXPECT_EQ(targets.targets[k].pose.P()[2],
                  solver.requestedPositions()[k][2]);
    }
    // 姿态容差逐请求透传（完整位姿目标——每请求都是查询容差原值）。
    for (const double tol : solver.requestedOrientationTolerances()) {
        EXPECT_EQ(1e-4, tol);
    }
}

// =====================================================================
// ACC2——后台运行/协作取消（EX-T03/EX-T05 语义——执行管线同构面）
// =====================================================================

/**
 * 本用例验证（KIN-11/EX-T03）：协作取消 partial 如实——第一批
 * watermark 后取消：已批目标终态保留、未派发目标如实 NotRun（原因
 * 文本非空）、report.incomplete=true（不产正式覆盖结论）、watermark
 * 记录保留可续、检查点通道收到的 watermark 形如 (1,3)。
 */
TEST(KinPoseReachability, CancellationPartialNotRun_WP15T16_ACC2_EXT03)
{
    SequenceSolver solver({});
    PoseReachabilityQuery q = makeQuery(threePositions(), &solver, 2U, 2U);
    q.maxBatchSize = 4U;  // 12 目标→3 批（4+4+4）
    TestView view(twoLinkModel());

    CancelAfterWatermarkSink sink;
    sink.arm(1U);  // 第一批 watermark 后置取消旗标
    q.checkpointSink = &sink;  // 检查点通道接替身——watermark 驱动取消旗标
    RecordingCancellableContext ctx;
    ctx.attach(&sink);

    const kin::PoseReachabilityComputation c =
        kin::runPoseReachabilityComputation(view, q, makeRequest(), ctx);

    // 检查点面：恰一批完成（watermark (1,3)——取消后不再推进）。
    ASSERT_EQ(1U, sink.watermarks.size());
    EXPECT_EQ(1U, sink.watermarks[0].first);
    EXPECT_EQ(3U, sink.watermarks[0].second);

    // partial 面：第一批 4 目标终态保留（Reached），其余 8 目标 NotRun。
    ASSERT_EQ(12U, c.results.results.size());
    std::uint64_t reached = 0;
    std::uint64_t notRun = 0;
    for (const PoseReachTargetResult& r : c.results.results) {
        if (r.state == SampleState::Reached) {
            ++reached;
            EXPECT_TRUE(r.reason.empty());   // 终态目标不携带取消原因
        } else if (r.state == SampleState::NotRun) {
            ++notRun;
            EXPECT_FALSE(r.reason.empty());  // ERR-01——NotRun 必填原因
        }
    }
    EXPECT_EQ(4U, reached);
    EXPECT_EQ(8U, notRun);

    // 覆盖面：incomplete=true（不产正式覆盖结论——§7.2 同口径）；
    // 分母不剔除（planned 恒 12——R8 纪律）；含 NotRun 的方向聚合为
    // notRun（聚合规则 3——p0 两方向 {2 Found} 后批停止……实际前 4
    // 目标＝p0 全部 4 目标，p0 两方向各 2 roll 全 Reached）。
    EXPECT_TRUE(c.report.incomplete);
    EXPECT_EQ(12U, c.report.targets.planned);
    EXPECT_EQ(4U, c.report.targets.reached);
    EXPECT_EQ(8U, c.report.targets.notRun);
    EXPECT_EQ(2U, c.perPosition[0].reachedDirections);
    EXPECT_EQ(0U, c.perPosition[1].reachedDirections);
    EXPECT_EQ(2U, c.perPosition[1].notRunDirections);
}

/**
 * 本用例验证（KIN-11/EX-T05）：进度上报批粒度——每批恰一次
 * reportProgress、phase token＝"solve-pose-target"（kPoseReachabilityPhase
 * 唯一书写点）、percent 单调推进至 100。
 */
TEST(KinPoseReachability, ProgressBatchGranularity_WP15T16_ACC2_EXT05)
{
    SequenceSolver solver(uniformScript(12U, kin::IkOutcomeKind::SolutionsFound));
    PoseReachabilityQuery q = makeQuery(threePositions(), &solver, 2U, 2U);
    q.maxBatchSize = 4U;  // 12 目标→3 批
    TestView view(twoLinkModel());

    // 记录型上下文（不附加取消旗标——全量执行面）。
    RecordingCancellableContext ctx;
    kin::PoseReachabilityComputation c =
        kin::runPoseReachabilityComputation(view, q, makeRequest(), ctx);

    // 每批恰一次（3 批→3 次上报）＋phase token 唯一书写点。
    ASSERT_EQ(3U, ctx.percents().size());
    for (const std::string& phase : ctx.phases()) {
        EXPECT_EQ(std::string(kin::kPoseReachabilityPhase), phase);
    }
    // percent 单调推进至 100（(4·100)/12=33、(8·100)/12=66、100）。
    EXPECT_EQ(33U, ctx.percents()[0]);
    EXPECT_EQ(66U, ctx.percents()[1]);
    EXPECT_EQ(100U, ctx.percents()[2]);
}

// =====================================================================
// 评估器形态（素材组装/零证据行/诊断分轨/载荷编码确定性）
// =====================================================================

/**
 * 本用例验证（KIN-11/R2 边界 §7.3）：评估器端到端——payload 携带分析
 * 素材（域 token＝kin.pose-reachability.v1、摘要 32 字节）；**零证据
 * 行**（R2 边界的结构表达——探索式结果无证据资格面）；完整执行零诊断。
 */
TEST(KinPoseReachability, EvaluatePayloadNoEvidenceRow_WP15T16_KIN11)
{
    SequenceSolver solver({});
    PoseReachabilityQuery q = makeQuery(threePositions(), &solver, 2U, 2U);
    TestView view(twoLinkModel());
    NoopContext ctx;

    kin::PoseReachabilityEvaluator evaluator(&view, q);
    const evidence::EvaluationOutput out = evaluator.evaluate(makeRequest(), ctx);

    // 载荷面（分析素材——域 token/摘要形态）。
    ASSERT_TRUE(out.payload.has_value());
    EXPECT_EQ(std::string(kin::kPoseReachabilityPayloadToken), out.payload->kindToken);
    EXPECT_EQ(32U, out.payload->digest.bytes.size());
    EXPECT_FALSE(out.payload->canonicalBytes.empty());

    // 零证据行（R2 边界——文件头边界 2 的结构表达）。
    EXPECT_TRUE(out.evidence.empty());

    // 完整执行零诊断（无取消/无碰撞评价缺失）。
    EXPECT_TRUE(out.diagnostics.empty());
}

/**
 * 本用例验证（KIN-NO-TCP 两分口径/T03-T05-T06 同源）：无工具模型＝
 * 批量级零素材——评估器产 KIN-NO-TCP 诊断＋零 payload（模型侧缺失≠
 * 工程不可行，判定留消费侧）。
 */
TEST(KinPoseReachability, EvaluateNoTcpZeroMaterial_WP15T16_KIN05)
{
    SequenceSolver solver({});
    PoseReachabilityQuery q = makeQuery(threePositions(), &solver, 2U, 2U);
    // 无工具模型（tools 空集＋defaultTcpIndex nullopt——夹具 withTool=false）。
    TestView view(makeModel(
        std::vector<rt::CanonicalJoint>{
            revolute("j1", rw::math::Vector3D<double>(0, 0, 1),
                     trans(0, 0, 0), -2.97, 2.97),
            revolute("j2", rw::math::Vector3D<double>(0, 0, 1),
                     trans(1.1, 0, 0), -kPi, kPi)},
        trans(0.7, 0, 0), trans(0, 0, 0), false));
    NoopContext ctx;

    kin::PoseReachabilityEvaluator evaluator(&view, q);
    const evidence::EvaluationOutput out = evaluator.evaluate(makeRequest(), ctx);

    // 零素材面＋KIN-NO-TCP 诊断（码值经 DiagCodes 常量——禁拼码断言）。
    EXPECT_FALSE(out.payload.has_value());
    ASSERT_EQ(1U, out.diagnostics.size());
    EXPECT_EQ(std::string(kin::kKinNoTcp), out.diagnostics[0].code);
}

/**
 * 本用例验证（KIN-11/EX-T03 诊断分轨）：取消后评估器产
 * KIN-RESULT-INCOMPLETE 诊断（NotRun 计数入 cause——partial 不产正式
 * 覆盖结论的显式标记面）。
 */
TEST(KinPoseReachability, EvaluateIncompleteDiagnostic_WP15T16_EXT03)
{
    SequenceSolver solver({});
    PoseReachabilityQuery q = makeQuery(threePositions(), &solver, 2U, 2U);
    q.maxBatchSize = 4U;
    TestView view(twoLinkModel());

    CancelAfterWatermarkSink sink;
    sink.arm(1U);
    q.checkpointSink = &sink;  // 检查点通道接替身——watermark 驱动取消旗标
    RecordingCancellableContext ctx;
    ctx.attach(&sink);

    kin::PoseReachabilityEvaluator evaluator(&view, q);
    const evidence::EvaluationOutput out = evaluator.evaluate(makeRequest(), ctx);

    // 载荷仍交付（partial 素材如实——含 NotRun 状态表），诊断一条。
    ASSERT_TRUE(out.payload.has_value());
    ASSERT_EQ(1U, out.diagnostics.size());
    EXPECT_EQ(std::string(kin::kKinResultIncomplete), out.diagnostics[0].code);
}

/**
 * 本用例验证（NFR-COR-01/CON-04 载荷面）：载荷编码确定性——同输入
 * 同字节；输入变化（脚本结局不同）即字节不同（magic "IRDPR01" 前缀
 * 自证通道形态）。
 */
TEST(KinPoseReachability, PayloadCanonicalDeterministic_WP15T16_NFRCOR01)
{
    // 全 Found 编排一跑。
    SequenceSolver solverA(uniformScript(12U, kin::IkOutcomeKind::SolutionsFound));
    PoseReachabilityQuery qA = makeQuery(threePositions(), &solverA, 2U, 2U);
    TestView view(twoLinkModel());
    NoopContext ctx;
    const kin::PoseReachabilityComputation a =
        kin::runPoseReachabilityComputation(view, qA, makeRequest(), ctx);
    const std::vector<std::uint8_t> bytesA =
        kin::encodePoseReachabilityPayloadCanonical(a, makeRequest().task);

    // 同形态第二跑——逐字节相等。
    SequenceSolver solverB(uniformScript(12U, kin::IkOutcomeKind::SolutionsFound));
    PoseReachabilityQuery qB = makeQuery(threePositions(), &solverB, 2U, 2U);
    const kin::PoseReachabilityComputation b =
        kin::runPoseReachabilityComputation(view, qB, makeRequest(), ctx);
    const std::vector<std::uint8_t> bytesB =
        kin::encodePoseReachabilityPayloadCanonical(b, makeRequest().task);
    ASSERT_EQ(bytesA.size(), bytesB.size());
    EXPECT_TRUE(bytesA == bytesB);

    // 不同结局编排（p1 改 NoConvergence）——字节必不同（状态表差异入
    // 载荷——素材面完整性）。
    std::vector<kin::IkOutcomeKind> mixed =
        uniformScript(12U, kin::IkOutcomeKind::SolutionsFound);
    for (std::size_t k = 4U; k < 8U; ++k) {
        mixed[k] = kin::IkOutcomeKind::MultiInitNoConvergence;
    }
    SequenceSolver solverC(std::move(mixed));
    PoseReachabilityQuery qC = makeQuery(threePositions(), &solverC, 2U, 2U);
    const kin::PoseReachabilityComputation c =
        kin::runPoseReachabilityComputation(view, qC, makeRequest(), ctx);
    const std::vector<std::uint8_t> bytesC =
        kin::encodePoseReachabilityPayloadCanonical(c, makeRequest().task);
    ASSERT_EQ(bytesA.size(), bytesC.size());
    EXPECT_FALSE(bytesA == bytesC);

    // magic 前缀（通道形态自证——布局漂移即前缀断言失败）。
    ASSERT_GE(bytesA.size(), 11U);
    EXPECT_EQ('I', static_cast<char>(bytesA[0]));
    EXPECT_EQ('R', static_cast<char>(bytesA[1]));
    EXPECT_EQ('D', static_cast<char>(bytesA[2]));
    EXPECT_EQ('P', static_cast<char>(bytesA[3]));
    EXPECT_EQ('R', static_cast<char>(bytesA[4]));
    EXPECT_EQ('0', static_cast<char>(bytesA[5]));
    EXPECT_EQ('1', static_cast<char>(bytesA[6]));
}

/**
 * 本用例验证（R2 边界的模式面/依赖面）：描述符形状——评估键
 * kin-pose-reachability（kebab 词形）、supportedModes＝{Quick} 单值
 * （§7.3"Quick/估算性质"——Verified 不支持＝不进正式证据链）、依赖
 * 五条（无 req.regions/sampling-plans/conditions——位置列表直给）、
 * stateless＋FullyThreadSafe。
 */
TEST(KinPoseReachability, DescriptorShape_WP15T16_KIN11)
{
    const evidence::EvaluatorDescriptor d = kin::makePoseReachabilityDescriptor();
    EXPECT_EQ(std::string(kin::kPoseReachabilityEvaluationKey), d.key);
    EXPECT_EQ(kin::kPoseReachabilityContractVersion, d.contractVersion);
    ASSERT_EQ(1U, d.supportedModes.size());
    EXPECT_EQ(core::EvaluationMode::Quick, d.supportedModes[0]);

    // 依赖五条：三条 Required（model.robot-design/tcp/config.ik）＋
    // policy.resolved 一条 Required＋collision-models 一条 Conditional
    // （V13-01——策略未启用碰撞不进切片）。
    ASSERT_EQ(5U, d.inputs.size());
    EXPECT_TRUE(d.stateless);
    EXPECT_EQ(evidence::ThreadSafety::FullyThreadSafe, d.threadSafety);
}

/**
 * 本用例验证（O-37 工厂契约）：工厂闭包注入——create() 无参签名产出
 * 绑定 (视图, 查询) 的评估器实例，descriptor 与评估器一致。
 */
TEST(KinPoseReachability, FactoryClosureInjection_WP15T16_O37)
{
    SequenceSolver solver({});
    PoseReachabilityQuery q = makeQuery(threePositions(), &solver, 2U, 2U);
    TestView view(twoLinkModel());

    kin::PoseReachabilityEvaluatorFactory factory(&view, q);
    const evidence::EvaluatorDescriptor& fd = factory.descriptor();
    EXPECT_EQ(std::string(kin::kPoseReachabilityEvaluationKey), fd.key);

    std::unique_ptr<evidence::IEngineeringEvaluator> eval = factory.create();
    ASSERT_TRUE(eval != nullptr);
    EXPECT_EQ(std::string(kin::kPoseReachabilityEvaluationKey),
              eval->descriptor().key);
}
