/**
 * @file   RegionCoverageTest.cpp
 * @brief  区域采样与覆盖率用例组（KinRegionCoverage，T06 批）——固定
 *         计数黄金样例（100/60，双口径分别统计）、降级与零样本（数据
 *         不足保留分母/零样本不定义）、sampleSetIdentity 公式与快照对
 *         账、复评同一样本集（生成黄金/双射对齐/取消 partial 重跑）、
 *         逐样本评估接线（IK 位置/位姿分流、碰撞缺检测器、镜像独立
 *         计数）。任务契约 WP-15-T06 acceptance 1~5 逐条具名自证。
 *
 * 设计依据：
 *   - units/kinematics.md §7.2（覆盖率图全链）、§9.2（IWorkspaceSampler
 *     契约）、§3.4/D-KIN-6（确定性生成黄金——Grid 体心/斐波那契螺旋/
 *     roll 均分）、§8.4（确定性）、§10.2（V-13/V-14/V-15/V-22/V-23
 *     的本单元侧观测点）
 *   - REQUIREMENTS KIN-04（R3/R8 口径原文）、KIN-05；evidence §4.1.4
 *   - 黄金几何：KinFkFixture 二连杆模型（视图真值面——脚本求解下模型
 *     仅承载自由度/TCP 解析）
 *   - 任务契约 tasks/foundation/WP-15-T06.json
 *
 * 测试策略：可控求解器替身（ScriptedSolver——按注入谓词返回指定结局，
 * 记录逐请求目标/容差供接线断言，不调用取消探针——使取消查询次数确定
 * 可数）；覆盖率语义经 runRegionCoverageComputation 直调断言（NFR-MNT-01
 * 计算库可直调），素材形状与对账轨经 evaluate 端到端断言。
 */

#include "KinFkFixture.hpp"

#include <sdurws/ird/evidence/Evaluator.hpp>
#include <sdurws/ird/evidence/Snapshot.hpp>     // SamplingPlanRef（快照冻结凭据）
#include <sdurws/ird/kinematics/Coverage.hpp>
#include <sdurws/ird/kinematics/DiagCodes.hpp>  // kKin* 码值常量（禁拼码断言面）
#include <sdurws/ird/kinematics/Evaluators.hpp>
#include <sdurws/ird/kinematics/Ik.hpp>
#include <sdurws/ird/kinematics/Sampling.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace sdurws::ird::kinematics::testfixture;
namespace evidence = sdurws::ird::evidence;
namespace kin = sdurws::ird::kinematics;
using kin::CoverageResult;
using kin::RegionCoverageQuery;
using kin::RegionSamplingBudget;
using kin::SampleKind;
using kin::SamplingPlan;
using kin::SampleSet;
using kin::SampleState;
using kin::SampleResultSet;

namespace {

/// π（rad——位置样本姿态无约束落值的测试侧判别常量；与产品实现同一
/// 数学量，独立书写以防常量漂移静默）。
constexpr double kPi = 3.14159265358979323846;

// =====================================================================
// 小工具（脚手架——非断言面）
// =====================================================================

core::ObjectId regionId(const std::string& seed)
{
    return idFrom<core::ObjectId>("kin-rg-" + seed);
}
core::ContentIdentity planIdentityFrom(const std::string& seed)
{
    core::ContentIdentity id;
    id.bytes = digestOf("kin-rp-" + seed);
    return id;
}

/// 采样计划投影（默认：盒心 (0.5,0.5,0.5)——盒域 [0,1]³，格心坐标全正
/// 便于黄金断言；Grid 计数＋单方向单 roll）。
SamplingPlan makePlan(const std::string& seed,
                      const std::array<std::uint32_t, 3>& gridCounts,
                      std::uint32_t directionSamples = 1U,
                      std::uint32_t rollSamples = 1U)
{
    SamplingPlan plan;
    plan.regionObjectId = regionId(seed);
    plan.planContentIdentity = planIdentityFrom(seed);
    plan.box.center = rw::math::Vector3D<double>(0.5, 0.5, 0.5);
    plan.box.size = rw::math::Vector3D<double>(1.0, 1.0, 1.0);
    plan.position.method = kin::PositionSamplingDefinition::Method::Grid;
    plan.position.gridCounts = gridCounts;
    plan.orientation.directionSamples = directionSamples;
    plan.orientation.rollSamples = rollSamples;
    return plan;
}

/// 采样预算（seed 固定非 0——I-KIN-4；黄金面断言可复算）。
RegionSamplingBudget makeBudget(std::uint64_t seed = 42U)
{
    RegionSamplingBudget budget;
    budget.seed = seed;
    budget.threadCount = 1U;
    return budget;
}

/// 区域覆盖查询（脚本求解下模型仅承载自由度/TCP——solver 必注入）。
RegionCoverageQuery makeQuery(std::vector<SamplingPlan> plans, kin::IIkSolver* solver,
                              std::uint64_t seed = 42U)
{
    RegionCoverageQuery q;
    q.plans = std::move(plans);
    q.defaultTcp.toolObject = idFrom<core::ObjectId>("kin-tool");
    q.referenceQ = {0.0, 0.0};
    q.initialStrategy = kin::InitialValueStrategy::ReferenceQ;
    q.initialValuesCount = 1U;
    q.iterationLimit = 16U;
    q.budget = makeBudget(seed);
    q.solver = solver;
    return q;
}

/// 由计划＋预算组装一致的快照冻结凭据（对账 matched 面——与评估器重算
/// 同源函数，保证 matched 前置）。
evidence::SamplingPlanRef makeRef(const SamplingPlan& plan,
                                  const RegionSamplingBudget& budget)
{
    evidence::SamplingPlanRef ref;
    ref.regionObjectId = plan.regionObjectId;
    ref.planContentIdentity = plan.planContentIdentity;
    ref.plannedPositionSamples = kin::plannedPositionSampleCount(plan);
    ref.plannedPoseSamples = kin::plannedPoseSampleCount(plan);
    ref.sampleSetIdentity = kin::sampleSetIdentity(plan.planContentIdentity, budget);
    return ref;
}

/// 请求壳（快照冻结凭据由测试填充；身份面固定派生——绑定断言面）。
evidence::EvaluationRequest makeRequest(std::vector<evidence::SamplingPlanRef> refs)
{
    evidence::EvaluationRequest req;
    req.mode = core::EvaluationMode::Verified;
    req.snapshot.snapshotId.bytes = digestOf("kin-cov-snap");
    req.snapshot.samplingPlans = std::move(refs);
    req.slice.sliceId.bytes = digestOf("kin-cov-slice");
    req.task.attempt.value = 3U;
    return req;
}

// =====================================================================
// 可控求解器替身（按注入谓词脚本结局；记录逐请求目标/容差——逐样本
// 接线断言面；不调用取消探针——取消查询次数确定可数）
// =====================================================================

class ScriptedSolver final : public kin::IIkSolver {
public:
    /// 结局谓词：入参＝请求（目标位姿/容差可判别）；返回结局种类。
    using Predicate = std::function<kin::IkOutcomeKind(const kin::IkRequest&)>;

    /// 样本种类判别（测试侧标记——按姿态容差是否为"无约束落值 π"）。
    enum class RequestKind : std::uint8_t { Position, Pose };

    /// 记录的单次请求摘要（接线断言面）。
    struct RecordedRequest {
        rw::math::Vector3D<double> targetPosition;
        double positionTolerance = 0.0;
        double orientationTolerance = 0.0;
        RequestKind kind = RequestKind::Position;
    };

    explicit ScriptedSolver(Predicate predicate) : m_predicate(std::move(predicate)) {}

    kin::IkOutcome solve(const kin::IkRequest& request) const override
    {
        RecordedRequest rec;
        rec.targetPosition = request.targetInBase.P();
        rec.positionTolerance = request.positionTolerance;
        rec.orientationTolerance = request.orientationTolerance;
        // 位置样本判别：评估器以"姿态无约束落值 π"承载存在性口径——
        // 记录面按该容差判别样本种类（与评估器实现内同一判据）。
        rec.kind = request.orientationTolerance >= kPi ? RequestKind::Position
                                                       : RequestKind::Pose;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_requests.push_back(rec);
        }

        kin::IkOutcome outcome;
        outcome.outcomeKind = m_predicate(request);
        // 组装器 presence 不变式的脚本侧满足（本组用例不消费解面）。
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
            record.initialGuessesTried = 2U;
            outcome.solutionSet.searchRecord = record;
        }
        return outcome;
    }

    const std::vector<RecordedRequest>& requests() const { return m_requests; }

private:
    Predicate m_predicate;
    mutable std::mutex m_mutex;
    mutable std::vector<RecordedRequest> m_requests;
};

/// 常用谓词：按样本位置 x 分流（≤0.5 → 可达；>0.5 → 解析界限证明——
/// 确定性不可达的唯一素材路径）。100 样本网格（5×5×4，x∈{0.1..0.9}）
/// 下恰 60 个样本 x≤0.5——ACC1 黄金计数。
kin::IkOutcomeKind reachByX(const kin::IkRequest& request)
{
    return request.targetInBase.P()[0] <= 0.5
        ? kin::IkOutcomeKind::SolutionsFound
        : kin::IkOutcomeKind::AnalyticBoundExceeded;
}

/// 常用谓词：按样本位置 z 分流（>0.6 → 搜索未果——数据不足变体；
/// 其余可达）。z∈{0.125,0.375,0.625,0.875} → 恰 50 样本数据不足
/// （0.625/0.875 两个片层×25）。
kin::IkOutcomeKind dataInsufficientByZ(const kin::IkRequest& request)
{
    return request.targetInBase.P()[2] > 0.6
        ? kin::IkOutcomeKind::MultiInitNoConvergence
        : kin::IkOutcomeKind::SolutionsFound;
}

// =====================================================================
// 取消联动替身（第一批 watermark 后置取消旗标——V-22 检查点面的行为
// 断言：partial 停止派发、watermark 保留可续）
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

/// 可取消上下文（取消旗标自检查点替身读取；进度照收）。
class CancellableContext final : public evidence::IEvaluationContext {
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

private:
    const CancelAfterWatermarkSink* m_sink = nullptr;
    std::vector<std::uint8_t> m_percents;
    std::vector<std::string> m_phases;
};

/// 载荷字节流顺序读取器（测试侧解码——与产品编码器同布局，布局漂移即
/// 断言失败；TaskPointsBatchTest 同款）。
class ByteReader {
public:
    explicit ByteReader(const std::vector<std::uint8_t>& bytes) : m_bytes(bytes) {}

    std::uint8_t u8() { return m_bytes.at(m_pos++); }
    std::uint32_t u32()
    {
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            v |= static_cast<std::uint32_t>(m_bytes.at(m_pos++)) << (8 * i);
        }
        return v;
    }
    std::uint64_t u64()
    {
        std::uint64_t v = 0;
        for (int i = 0; i < 8; ++i) {
            v |= static_cast<std::uint64_t>(m_bytes.at(m_pos++)) << (8 * i);
        }
        return v;
    }
    std::string str(std::size_t n)
    {
        std::string out(m_bytes.begin() + m_pos, m_bytes.begin() + m_pos + n);
        m_pos += n;
        return out;
    }

private:
    const std::vector<std::uint8_t>& m_bytes;
    std::size_t m_pos = 0;
};

}  // namespace

// =====================================================================
// ACC1——固定计数黄金样例：计划 100 可达 60→60%（位置覆盖率＝存在性
// 口径、姿态覆盖率＝全局口径分别统计；整数计数无浮点容差——V-14）
// =====================================================================

TEST(KinRegionCoverage, FixedCountGoldenReach60_WP15T06_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-04"},
                  std::vector<std::string>{"V-14"});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);

    // 计划 100 位置样本（Grid 5×5×4；D=R=1 → 位姿样本同为 100）。
    const SamplingPlan plan = makePlan("golden", {5U, 5U, 4U});
    ScriptedSolver solver(reachByX);  // x≤0.5 → 可达（恰 60 样本）
    const RegionCoverageQuery query = makeQuery({plan}, &solver);
    const evidence::EvaluationRequest req = makeRequest({makeRef(plan, query.budget)});

    CancellableContext context;
    const kin::RegionCoverageComputation computation =
        kin::runRegionCoverageComputation(view, query, req, context);

    // 分母＝计划样本总数（KIN-04 R8——禁止按评估结果剔除样本）；
    // 分子＝Reached 计数。整数计数精确相等（无浮点容差）。
    EXPECT_EQ(computation.samples.plannedPositionSamples, 100U);
    EXPECT_EQ(computation.samples.plannedPoseSamples, 100U);
    EXPECT_EQ(computation.coverage.position.planned, 100U);
    EXPECT_EQ(computation.coverage.position.reached, 60U);   // 60/100＝60%
    EXPECT_EQ(computation.coverage.position.unreachable, 40U);
    EXPECT_EQ(computation.coverage.position.dataInsufficient, 0U);
    // 姿态覆盖率（全局口径）独立统计——同谓词下同为 60/100。
    EXPECT_EQ(computation.coverage.orientation.planned, 100U);
    EXPECT_EQ(computation.coverage.orientation.reached, 60U);
    // 无降级/无不完整（纯不可达不降级——不可达样本保留分母）。
    EXPECT_FALSE(computation.coverage.downgraded);
    EXPECT_FALSE(computation.coverage.incomplete);
    EXPECT_TRUE(computation.coverage.positionDefined);
    EXPECT_TRUE(computation.coverage.orientationDefined);
}

TEST(KinRegionCoverage, PositionExistenceVsPoseGlobal_WP15T06_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-04"},
                  std::vector<std::string>{"V-14"});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);

    // D=2：位姿样本＝100×2×1＝200。谓词：位置样本（姿态无约束）按 x
    // 存在性判定；位姿样本按"目标 z 轴方向 z 分量>0"判定（方向集 N=2
    // 的 j=0 方向 z=+0.5、j=1 方向 z=−0.5——恰一半方向可达）。
    const SamplingPlan plan = makePlan("dual", {5U, 5U, 4U}, 2U, 1U);
    ScriptedSolver solver([](const kin::IkRequest& request) {
        if (request.orientationTolerance >= kPi) {
            // 位置样本——存在性口径：x≤0.5 即存在有效解。
            return request.targetInBase.P()[0] <= 0.5
                ? kin::IkOutcomeKind::SolutionsFound
                : kin::IkOutcomeKind::AnalyticBoundExceeded;
        }
        // 位姿样本——全局口径：位置与方向都须成立（目标旋转第三列即
        // 方向 d——R＝R_dir·Rz(roll) 的右乘不改变 z 轴像）。
        const double dz = request.targetInBase.R()(2, 2);
        return (request.targetInBase.P()[0] <= 0.5 && dz > 0.0)
            ? kin::IkOutcomeKind::SolutionsFound
            : kin::IkOutcomeKind::AnalyticBoundExceeded;
    });
    const RegionCoverageQuery query = makeQuery({plan}, &solver);
    const evidence::EvaluationRequest req = makeRequest({makeRef(plan, query.budget)});

    CancellableContext context;
    const kin::RegionCoverageComputation computation =
        kin::runRegionCoverageComputation(view, query, req, context);

    // 位置覆盖率（存在性口径）：60/100——任一姿态存在即达，与方向数无关。
    EXPECT_EQ(computation.coverage.position.planned, 100U);
    EXPECT_EQ(computation.coverage.position.reached, 60U);
    // 姿态覆盖率（全局口径）：60/200＝30%——(位置×姿态) 逐组合计数，
    // 非逐位置平均（KIN-04 R3 原文口径的两口径分离实证）。
    EXPECT_EQ(computation.coverage.orientation.planned, 200U);
    EXPECT_EQ(computation.coverage.orientation.reached, 60U);
    EXPECT_EQ(computation.samples.samples.size(), 300U);  // 100 位置＋200 位姿
}

// =====================================================================
// ACC2——降级与零样本：数据不足样本保留分母不计分子＋整体降级（V-14
// 数据不足变体）；零样本→覆盖率不定义＋KIN-COVERAGE-ZERO-SAMPLES，
// 绝不输出 0%/100%（V-13）
// =====================================================================

TEST(KinRegionCoverage, DataInsufficientKeepsDenominatorDowngrades_WP15T06_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-04", "EVI-01"},
                  std::vector<std::string>{"V-14"});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);

    const SamplingPlan plan = makePlan("insuf", {5U, 5U, 4U});
    ScriptedSolver solver(dataInsufficientByZ);  // z>0.6 → 搜索未果（50 样本）
    const RegionCoverageQuery query = makeQuery({plan}, &solver);
    const evidence::EvaluationRequest req = makeRequest({makeRef(plan, query.budget)});

    CancellableContext context;
    const kin::RegionCoverageComputation computation =
        kin::runRegionCoverageComputation(view, query, req, context);

    // 数据不足样本保留分母、不计分子、单独计数（KIN-04 R8）：
    // 分母恒 100＝50 可达＋50 数据不足（守恒式）。
    EXPECT_EQ(computation.coverage.position.planned, 100U);
    EXPECT_EQ(computation.coverage.position.reached, 50U);
    EXPECT_EQ(computation.coverage.position.dataInsufficient, 50U);
    // 结论整体降级 DataInsufficient（EVI-01 降级路径的素材面）。
    EXPECT_TRUE(computation.coverage.downgraded);
    EXPECT_FALSE(computation.coverage.incomplete);
}

TEST(KinRegionCoverage, ZeroSampleEvaluateDiagAndMaterial_WP15T06_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-04"},
                  std::vector<std::string>{"V-13"});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);
    NoopContext context;

    // 计数乘积=0 计划（x 轴计数 0——I-REQ-6 合法存储的零样本场景）。
    const SamplingPlan plan = makePlan("zero", {0U, 5U, 4U});
    ScriptedSolver solver(reachByX);
    const RegionCoverageQuery query = makeQuery({plan}, &solver);
    const evidence::EvaluationRequest req = makeRequest({makeRef(plan, query.budget)});

    kin::WorkspaceSampler evaluator(&view, query);
    const evidence::EvaluationOutput out = evaluator.evaluate(req, context);

    // KIN-COVERAGE-ZERO-SAMPLES 诊断在产（§9.6 行 14——禁拼码断言）。
    bool hasZeroDiag = false;
    for (const core::DiagnosticRecord& d : out.diagnostics) {
        if (d.code == std::string(kin::kKinCoverageZeroSamples)) {
            hasZeroDiag = true;
        }
    }
    EXPECT_TRUE(hasZeroDiag);

    // 素材仍交付（V-13 观测点 CoverageResult——计数全零＋不定义标记；
    // 载荷无比率/百分比字段——"绝不输出 0%/100%"的结构保证）。
    ASSERT_TRUE(out.payload.has_value());
    ByteReader reader(out.payload->canonicalBytes);
    EXPECT_EQ(reader.str(7), "IRDCV01");
    EXPECT_EQ(reader.u32(), 1U);  // codec 版本
    EXPECT_EQ(reader.u8(), 0U);   // incomplete
    EXPECT_EQ(reader.u8(), 1U);   // downgraded——零样本 → 整体降级
    EXPECT_EQ(reader.u8(), 0U);   // positionDefined——覆盖率不定义
    EXPECT_EQ(reader.u8(), 0U);   // orientationDefined
    // 逐计划证据行仍 Satisfied（产物存在——零样本素材面）。
    ASSERT_EQ(out.evidence.size(), 1U);
    EXPECT_EQ(out.evidence.front().itemId, std::string(kin::kKinRegionCoverageRowId));
    EXPECT_EQ(out.evidence.front().status, evidence::EvidenceItemStatus::Satisfied);
}

// =====================================================================
// ACC3——样本集冻结与身份对账：sampleSetIdentity 公式（canonical 字节
// 布局独立复算）；快照对账不一致/未冻结→KIN-SAMPLE-IDENTITY-MISMATCH
// →证据缺失（V-15；O-38 冻结前以快照对账为准）
// =====================================================================

TEST(KinRegionCoverage, SampleSetIdentityFormulaAndDeterminism_WP15T06_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-04", "CON-05"},
                  std::vector<std::string>{"V-15"});

    const core::ContentIdentity planId = planIdentityFrom("idplan");
    const RegionSamplingBudget budget = makeBudget(7U);

    // 公式黄金复算（测试侧独立组装 canonical 字节——与 Sampling.cpp 布局
    // 注逐字节一致，布局漂移即用例失败）：SHA-256 over ("IRDSSID1" ‖
    // u32(1) ‖ u32(len) ‖ planContentIdentity 规范文本 ‖ u64 seed)。
    const std::string planText = planId.toCanonical();
    std::vector<std::uint8_t> canonical;
    const char magic[] = "IRDSSID1";
    canonical.insert(canonical.end(), magic, magic + 7);
    for (int i = 0; i < 4; ++i) {
        canonical.push_back(static_cast<std::uint8_t>((1U >> (8 * i)) & 0xFFu));
    }
    const std::uint32_t len = static_cast<std::uint32_t>(planText.size());
    for (int i = 0; i < 4; ++i) {
        canonical.push_back(static_cast<std::uint8_t>((len >> (8 * i)) & 0xFFu));
    }
    canonical.insert(canonical.end(), planText.begin(), planText.end());
    const std::uint64_t seed = budget.seed;
    for (int i = 0; i < 8; ++i) {
        canonical.push_back(static_cast<std::uint8_t>((seed >> (8 * i)) & 0xFFu));
    }
    core::ContentDigester digester;
    digester.update(canonical.data(), canonical.size());
    core::ContentIdentity expected;
    expected.bytes = digester.finalize();

    // 实现值＝黄金复算值；同输入重算稳定；异种子必异（身份对计划参数
    // 计算而非对枚举列表——evidence §4.1.4）。
    const core::ContentIdentity computed = kin::sampleSetIdentity(planId, budget);
    EXPECT_TRUE(computed == expected);
    EXPECT_TRUE(kin::sampleSetIdentity(planId, budget) == computed);
    EXPECT_FALSE(kin::sampleSetIdentity(planId, makeBudget(8U)) == computed);

    // 同 (plan,budget) 同样本集同序（D-KIN-6——复评不增删更换样本的
    // 生成面保证；逐位对齐断言）。
    const SamplingPlan plan = makePlan("idplan", {3U, 2U, 2U});
    const SampleSet a = kin::generateSampleSet({plan}, budget);
    const SampleSet b = kin::generateSampleSet({plan}, budget);
    ASSERT_EQ(a.samples.size(), b.samples.size());
    for (std::size_t i = 0; i < a.samples.size(); ++i) {
        EXPECT_EQ(a.samples[i].sampleIndex, b.samples[i].sampleIndex);
        EXPECT_EQ(a.samples[i].kind, b.samples[i].kind);
        EXPECT_TRUE(a.samples[i].position == b.samples[i].position);
        EXPECT_TRUE(a.samples[i].pose == b.samples[i].pose);
    }
}

TEST(KinRegionCoverage, IdentityMismatchDropsEvidence_WP15T06_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-04"},
                  std::vector<std::string>{"V-15"});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);
    NoopContext context;

    const SamplingPlan plan = makePlan("mism", {2U, 2U, 2U});
    ScriptedSolver solver(reachByX);
    const RegionCoverageQuery query = makeQuery({plan}, &solver);
    kin::WorkspaceSampler evaluator(&view, query);

    // 变体一：快照 ref 的 sampleSetIdentity 被篡改（不一致）。
    evidence::SamplingPlanRef tampered = makeRef(plan, query.budget);
    tampered.sampleSetIdentity.bytes = digestOf("tampered-identity");
    const evidence::EvaluationOutput outTampered =
        evaluator.evaluate(makeRequest({tampered}), context);
    ASSERT_EQ(outTampered.diagnostics.size(), 1U);
    EXPECT_EQ(outTampered.diagnostics.front().code,
              std::string(kin::kKinSampleIdentityMismatch));
    // 证据缺失：零 payload、零证据行（绝不沿用不一致样本集——无覆盖率输出）。
    EXPECT_FALSE(outTampered.payload.has_value());
    EXPECT_TRUE(outTampered.evidence.empty());

    // 变体二：快照无该区域 SamplingPlanRef（未冻结）——同轨拒绝。
    const evidence::EvaluationOutput outUnfrozen =
        evaluator.evaluate(makeRequest({}), context);
    ASSERT_EQ(outUnfrozen.diagnostics.size(), 1U);
    EXPECT_EQ(outUnfrozen.diagnostics.front().code,
              std::string(kin::kKinSampleIdentityMismatch));
    EXPECT_FALSE(outUnfrozen.payload.has_value());

    // 变体三：分母被篡改（plannedXxxSamples 与重算不一致）——同轨拒绝
    // （分母字段同属冻结凭据——evidence §4.1.4 分母来源）。
    evidence::SamplingPlanRef tamperedCount = makeRef(plan, query.budget);
    tamperedCount.plannedPositionSamples += 1U;
    const evidence::EvaluationOutput outCount =
        evaluator.evaluate(makeRequest({tamperedCount}), context);
    ASSERT_EQ(outCount.diagnostics.size(), 1U);
    EXPECT_EQ(outCount.diagnostics.front().code,
              std::string(kin::kKinSampleIdentityMismatch));

    // 对照：一致 ref → 正常产出（对账轨不触发、零额外诊断）。
    const evidence::EvaluationOutput outMatched =
        evaluator.evaluate(makeRequest({makeRef(plan, query.budget)}), context);
    EXPECT_TRUE(outMatched.payload.has_value());
    EXPECT_EQ(outMatched.diagnostics.size(), 0U);
}

// =====================================================================
// ACC4——复评不得增删更换样本：生成黄金锁定（Grid 体心/斐波那契螺旋/
// roll 均分独立参考复算）；sampleIndex 对齐分母（双射核查）；取消
// partial→NotRun 保留、重跑同一样本集（V-22/23 本单元侧）
// =====================================================================

TEST(KinRegionCoverage, GoldenGeneratorsAndSameOrderRerun_WP15T06_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-04", "NFR-COR-01"},
                  std::vector<std::string>{"V-14"});

    // Grid 体心规则黄金（2×2×1、盒域 [0,1]³——格心 0.25/0.75、z 单格 0.5）。
    const kin::RegionBox box{rw::math::Vector3D<double>(0.5, 0.5, 0.5),
                        rw::math::Vector3D<double>(1, 1, 1)};
    const std::vector<rw::math::Vector3D<double>> grid =
        kin::generateGridPositions(box, {2U, 2U, 1U});
    ASSERT_EQ(grid.size(), 4U);
    EXPECT_DOUBLE_EQ(grid[0][0], 0.25);   // 枚举序：x 最慢、z 最快（黄金锁定）
    EXPECT_DOUBLE_EQ(grid[0][1], 0.25);
    EXPECT_DOUBLE_EQ(grid[0][2], 0.5);
    EXPECT_DOUBLE_EQ(grid[1][0], 0.25);
    EXPECT_DOUBLE_EQ(grid[1][1], 0.75);
    EXPECT_DOUBLE_EQ(grid[2][0], 0.75);

    // 斐波那契螺旋方向集黄金（N=3——独立参考复算：z_j＝1−(2j+1)/N、
    // φ_j＝j·黄金角；测试侧独立书写公式——产品实现的黄金对照）。
    const std::vector<rw::math::Vector3D<double>> dirs = kin::generateDirections(3U);
    ASSERT_EQ(dirs.size(), 3U);
    const double goldenAngle = 2.39996322972865332223;
    for (std::uint32_t j = 0; j < 3; ++j) {
        const double z = 1.0 - (2.0 * j + 1.0) / 3.0;
        const double r = std::sqrt(1.0 - z * z);
        EXPECT_NEAR(dirs[j][0], r * std::cos(j * goldenAngle), 1e-15);
        EXPECT_NEAR(dirs[j][1], r * std::sin(j * goldenAngle), 1e-15);
        EXPECT_NEAR(dirs[j][2], z, 1e-15);
    }

    // roll 均分黄金（M=2——[−π,π) 体心：−π/2、+π/2）。
    const std::vector<double> rolls = kin::generateRolls(2U);
    ASSERT_EQ(rolls.size(), 2U);
    EXPECT_DOUBLE_EQ(rolls[0], -kPi / 2.0);
    EXPECT_DOUBLE_EQ(rolls[1], kPi / 2.0);

    // Random：同 seed 同序列、异 seed 异序列（§3.4 随机性唯一来源）。
    const std::vector<rw::math::Vector3D<double>> r1 =
        kin::generateRandomPositions(box, 8U, 99U);
    const std::vector<rw::math::Vector3D<double>> r2 =
        kin::generateRandomPositions(box, 8U, 99U);
    const std::vector<rw::math::Vector3D<double>> r3 =
        kin::generateRandomPositions(box, 8U, 100U);
    ASSERT_EQ(r1.size(), 8U);
    for (std::size_t i = 0; i < 8; ++i) {
        EXPECT_TRUE(r1[i] == r2[i]);
        EXPECT_FALSE(r1[i] == r3[i]);
    }

    // 复评同集同序（多计划面——计划序×计划内序，全局 sampleIndex 连续）。
    const SamplingPlan p1 = makePlan("rrp1", {2U, 1U, 1U});
    const SamplingPlan p2 = makePlan("rrp2", {1U, 2U, 1U});
    const SampleSet s1 = kin::generateSampleSet({p1, p2}, makeBudget(5U));
    const SampleSet s2 = kin::generateSampleSet({p1, p2}, makeBudget(5U));
    ASSERT_EQ(s1.samples.size(), s2.samples.size());
    for (std::size_t i = 0; i < s1.samples.size(); ++i) {
        EXPECT_EQ(s1.samples[i].sampleIndex, s2.samples[i].sampleIndex);
        EXPECT_EQ(s1.samples[i].regionObjectId, s2.samples[i].regionObjectId);
    }
    // 跨计划连续编号（分母对齐键的结构前提）。
    EXPECT_EQ(s1.samples.front().sampleIndex, 0U);
    EXPECT_EQ(s1.samples.back().sampleIndex, s1.samples.size() - 1U);
}

TEST(KinRegionCoverage, SampleIndexAlignsDenominatorBijection_WP15T06_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-04"},
                  std::vector<std::string>{"V-22"});

    const SamplingPlan p1 = makePlan("alp1", {2U, 2U, 1U});  // 4 位置＋4 位姿
    const SamplingPlan p2 = makePlan("alp2", {3U, 1U, 1U});  // 3 位置＋3 位姿
    const SampleSet set = kin::generateSampleSet({p1, p2}, makeBudget());
    ASSERT_EQ(set.samples.size(), 14U);
    EXPECT_EQ(set.plannedPositionSamples, 7U);
    EXPECT_EQ(set.plannedPoseSamples, 7U);

    // 全量结果 → 覆盖率计算通过（分母完整性核查键对齐）。
    SampleResultSet full;
    for (const auto& s : set.samples) {
        kin::SampleResultRecord r;
        r.sampleIndex = s.sampleIndex;
        r.state = SampleState::Reached;
        full.results.push_back(r);
    }
    const CoverageResult ok = kin::computeCoverage(set, full);
    EXPECT_EQ(ok.position.reached, 7U);
    EXPECT_EQ(ok.orientation.reached, 7U);

    // 缺项 → logic_error（分母完整性，fail-fast 不静默）。
    SampleResultSet missing = full;
    missing.results.pop_back();
    EXPECT_THROW(kin::computeCoverage(set, missing), std::logic_error);

    // 重复 → logic_error。
    SampleResultSet dup = full;
    dup.results.push_back(dup.results.front());
    EXPECT_THROW(kin::computeCoverage(set, dup), std::logic_error);
}

TEST(KinRegionCoverage, CancellationPartialNotRunRerunSameSet_WP15T06_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-04", "TASK-02"},
                  std::vector<std::string>{"V-22", "V-23"});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);

    const SamplingPlan plan = makePlan("cancel", {5U, 5U, 4U});  // 200 样本
    ScriptedSolver solver(reachByX);
    RegionCoverageQuery query = makeQuery({plan}, &solver);
    query.maxBatchSize = 10U;  // 20 批——第一批 watermark 后联动取消

    const evidence::EvaluationRequest req = makeRequest({makeRef(plan, query.budget)});

    // 第一跑：第一批完成后取消 → 剩余样本如实 NotRun（partial）。
    CancelAfterWatermarkSink sink;
    sink.arm(1U);
    query.checkpointSink = &sink;  // 检查点通道接入（watermark 联动取消）
    CancellableContext ctx1;
    ctx1.attach(&sink);
    const kin::RegionCoverageComputation partial =
        kin::runRegionCoverageComputation(view, query, req, ctx1);

    EXPECT_EQ(partial.coverage.position.reached, 10U);    // 第一批 10 位置样本
    EXPECT_EQ(partial.coverage.position.notRun, 90U);     // 其余位置样本
    EXPECT_EQ(partial.coverage.orientation.notRun, 100U); // 位姿样本未派发
    EXPECT_TRUE(partial.coverage.incomplete);             // partial——不产正式覆盖率
    // watermark 记录（批 1/20——检查点保留可续，V-23 本单元侧）。
    ASSERT_EQ(sink.watermarks.size(), 1U);
    EXPECT_EQ(sink.watermarks.front().first, 1U);
    EXPECT_EQ(sink.watermarks.front().second, 20U);

    // 第二跑（重跑）：无取消——同一样本集（逐位对齐）、补全覆盖率。
    CancellableContext ctx2;
    const kin::RegionCoverageComputation rerun =
        kin::runRegionCoverageComputation(view, query, req, ctx2);
    ASSERT_EQ(rerun.samples.samples.size(), partial.samples.samples.size());
    for (std::size_t i = 0; i < rerun.samples.samples.size(); ++i) {
        // 重跑同一样本集（D-KIN-6/§7.2——不增删更换样本）。
        EXPECT_EQ(rerun.samples.samples[i].sampleIndex,
                  partial.samples.samples[i].sampleIndex);
        EXPECT_TRUE(rerun.samples.samples[i].position
                    == partial.samples.samples[i].position);
        EXPECT_TRUE(rerun.samples.samples[i].pose == partial.samples.samples[i].pose);
    }
    EXPECT_EQ(rerun.coverage.position.reached, 60U);
    EXPECT_FALSE(rerun.coverage.incomplete);
}

// =====================================================================
// ACC5——逐样本评估接线：IK 分流（位置样本姿态无约束 π/位姿样本完整
// 位姿）＋碰撞（缺检测器→DataInsufficient；无要求无会话→检查不在范
// 围不降级）＋镜像独立计数
// =====================================================================

TEST(KinRegionCoverage, PerSampleIkWiringPositionVsPose_WP15T06_ACC5)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-04", "KIN-02"},
                  std::vector<std::string>{"V-14"});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);

    // 2 位置样本（Grid 2×1×1）＋（D=R=1）2 位姿样本。
    const SamplingPlan plan = makePlan("wire", {2U, 1U, 1U});
    ScriptedSolver solver(
        [](const kin::IkRequest&) { return kin::IkOutcomeKind::SolutionsFound; });
    const RegionCoverageQuery query = makeQuery({plan}, &solver);
    const evidence::EvaluationRequest req = makeRequest({makeRef(plan, query.budget)});

    CancellableContext context;
    const kin::RegionCoverageComputation computation =
        kin::runRegionCoverageComputation(view, query, req, context);

    // 逐请求接线断言：4 次求解（先 2 位置后 2 位姿——生成序黄金锁定）；
    // 位置样本目标位置＝格心、姿态容差＝π（无约束落值）、位置容差＝
    // 查询值；位姿样本姿态容差＝查询值、目标位置与位置样本同点。
    ASSERT_EQ(solver.requests().size(), 4U);
    EXPECT_EQ(solver.requests()[0].kind, ScriptedSolver::RequestKind::Position);
    EXPECT_DOUBLE_EQ(solver.requests()[0].targetPosition[0], 0.25);
    EXPECT_DOUBLE_EQ(solver.requests()[0].orientationTolerance, kPi);
    EXPECT_DOUBLE_EQ(solver.requests()[0].positionTolerance,
                     query.positionTolerance);
    EXPECT_EQ(solver.requests()[2].kind, ScriptedSolver::RequestKind::Pose);
    EXPECT_DOUBLE_EQ(solver.requests()[2].orientationTolerance,
                     query.orientationTolerance);
    EXPECT_DOUBLE_EQ(solver.requests()[2].targetPosition[0],
                     solver.requests()[0].targetPosition[0]);
    // 全部样本 Reached（谓词恒可达）。
    EXPECT_EQ(computation.coverage.position.reached, 2U);
    EXPECT_EQ(computation.coverage.orientation.reached, 2U);
}

TEST(KinRegionCoverage, CollisionDemandWithoutDetectorDataInsufficient_WP15T06_ACC5)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-04", "KIN-05"},
                  std::vector<std::string>{"V-14"});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);
    NoopContext context;

    // 计划级碰撞要求在场＋无会话 → 全部样本 DataInsufficient（KIN-05：
    // 缺检测器绝不视为无碰撞）＋整体降级；求解零调用（缺检测器轨短路
    // ——不产不可达素材）。
    SamplingPlan plan = makePlan("coldet", {2U, 1U, 1U});
    plan.demands.collisionFreeRequired = true;
    ScriptedSolver solver(
        [](const kin::IkRequest&) { return kin::IkOutcomeKind::SolutionsFound; });
    const RegionCoverageQuery query = makeQuery({plan}, &solver);
    kin::WorkspaceSampler evaluator(&view, query);
    const evidence::EvaluationOutput out =
        evaluator.evaluate(makeRequest({makeRef(plan, query.budget)}), context);

    EXPECT_EQ(solver.requests().size(), 0U);

    // 载荷标记面：incomplete=0、downgraded=1（缺检测器 → 整体降级
    // DataInsufficient——EVI-01 降级路径）。
    ASSERT_TRUE(out.payload.has_value());
    ByteReader reader(out.payload->canonicalBytes);
    EXPECT_EQ(reader.str(7), "IRDCV01");
    EXPECT_EQ(reader.u32(), 1U);
    EXPECT_EQ(reader.u8(), 0U);  // incomplete
    EXPECT_EQ(reader.u8(), 1U);  // downgraded

    // 对照：无碰撞要求且无会话 → 检查不在范围（不降级，正常求解——
    // collisionNotEvaluated 是"不在范围"标记，非缺陷）。
    SamplingPlan plain = makePlan("plain", {2U, 1U, 1U});
    const RegionCoverageQuery plainQuery = makeQuery({plain}, &solver);
    kin::WorkspaceSampler plainEvaluator(&view, plainQuery);
    const evidence::EvaluationOutput plainOut =
        plainEvaluator.evaluate(makeRequest({makeRef(plain, plainQuery.budget)}),
                                context);
    EXPECT_EQ(solver.requests().size(), 4U);  // 逐样本正常求解（2 位置＋2 位姿）
    ASSERT_TRUE(plainOut.payload.has_value());
    ByteReader plainReader(plainOut.payload->canonicalBytes);
    plainReader.str(7);
    plainReader.u32();
    EXPECT_EQ(plainReader.u8(), 0U);  // incomplete
    EXPECT_EQ(plainReader.u8(), 0U);  // downgraded——无要求无会话≠缺陷
}

TEST(KinRegionCoverage, MirrorPlanSamplesIndependent_WP15T06_ACC5)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-04"},
                  std::vector<std::string>{"V-14"});

    // 原生计划＋镜像派生计划（同几何、异身份）——镜像样本独立计数：
    // 分母相加、样本独立编号、跨计划无去重。
    const SamplingPlan primary = makePlan("mirp", {2U, 1U, 1U});
    SamplingPlan mirror = makePlan("mirq", {2U, 1U, 1U});
    mirror.mirrorOf = primary.regionObjectId;  // 派生溯源元数据
    const SampleSet set = kin::generateSampleSet({primary, mirror}, makeBudget());

    // 分母＝两计划之和（独立计数）；样本连续编号无去重。
    EXPECT_EQ(set.plannedPositionSamples, 4U);
    EXPECT_EQ(set.plannedPoseSamples, 4U);
    ASSERT_EQ(set.samples.size(), 8U);
    std::uint64_t primaryCount = 0;
    std::uint64_t mirrorCount = 0;
    for (const auto& s : set.samples) {
        if (s.regionObjectId == primary.regionObjectId) {
            ++primaryCount;
        } else if (s.regionObjectId == mirror.regionObjectId) {
            ++mirrorCount;
        }
    }
    EXPECT_EQ(primaryCount, 4U);
    EXPECT_EQ(mirrorCount, 4U);
    // 同几何计划的位置逐位相同但样本独立（镜像独立计数的语义面）。
    EXPECT_TRUE(set.samples[0].position == set.samples[4].position);
    EXPECT_EQ(set.samples[0].sampleIndex, 0U);
    EXPECT_EQ(set.samples[4].sampleIndex, 4U);
}

// =====================================================================
// 碰撞设施臂（WP-15-T07——acceptance 2）：碰撞要求在场＋构型碰撞评价
// 未完成（设施异常/策略侧不可判）→ 该样本 DataInsufficient（绝不视为
// 无碰撞）＋KIN-COLLISION-UNAVAILABLE 诊断在产（§9.6 行 9 产码面随
// 本任务落位——T06 预留轨的收口）
// =====================================================================

namespace {

/// 设施异常求解替身（结局 1＋碰撞评价缺失标记——coverage 的逐样本
/// 通道以 IkOutcome.collisionEvidenceMissing 承载设施臂，T07 表尾追加）。
class EvidenceMissingSolver final : public kin::IIkSolver {
public:
    kin::IkOutcome solve(const kin::IkRequest&) const override
    {
        kin::IkOutcome outcome;
        outcome.outcomeKind = kin::IkOutcomeKind::SolutionsFound;
        kin::KinematicSolution s;
        s.q = {0.1, -0.2};              // 自由度 2（二连杆夹具）
        s.positionResidual = 1e-9;      // m（< 容差）
        s.orientationResidual = 1e-9;   // rad
        s.collisionStatus.evaluated = false;  // 评价未完成（证据缺失）
        outcome.solutionSet.solutions.push_back(s);
        outcome.collisionEvidenceMissing = true;
        return outcome;
    }
};

}  // namespace

TEST(KinRegionCoverage, CollisionFacilityFailureSampleInsufficient_WP15T07_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-05", "KIN-04"},
                  std::vector<std::string>{});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);
    NoopContext context;

    // 计划级碰撞要求在场＋会话在场但评价缺失（设施臂）→ 全部样本
    // DataInsufficient＋整体降级（KIN-05：绝不视为无碰撞）。
    SamplingPlan plan = makePlan("colfac", {2U, 1U, 1U});
    plan.demands.collisionFreeRequired = true;
    EvidenceMissingSolver solver;
    const RegionCoverageQuery query = makeQuery({plan}, &solver);
    kin::WorkspaceSampler evaluator(&view, query);
    const evidence::EvaluationOutput out =
        evaluator.evaluate(makeRequest({makeRef(plan, query.budget)}), context);

    // 载荷标记面：downgraded=1（设施臂与缺检测器臂同素材面——要求在场
    // 而评价未完成 → 整体降级 DataInsufficient）。
    ASSERT_TRUE(out.payload.has_value());
    ByteReader reader(out.payload->canonicalBytes);
    EXPECT_EQ(reader.str(7), "IRDCV01");
    EXPECT_EQ(reader.u32(), 1U);
    EXPECT_EQ(reader.u8(), 0U);  // incomplete
    EXPECT_EQ(reader.u8(), 1U);  // downgraded——碰撞证据缺失 → 降级

    // KIN-COLLISION-UNAVAILABLE 诊断在产（样本计数 ≥1——§9.6 行 9）。
    bool hasUnavailableDiag = false;
    for (const core::DiagnosticRecord& d : out.diagnostics) {
        if (d.code == std::string(kin::kKinCollisionUnavailable)) {
            hasUnavailableDiag = true;
        }
    }
    EXPECT_TRUE(hasUnavailableDiag)
        << "碰撞要求在场而评价未完成应产出 KIN-COLLISION-UNAVAILABLE";

    // 对照：无碰撞要求且同构型求解（评价缺失标记不在范围语境）——
    // 样本正常 Reached、无 KIN-COLLISION-UNAVAILABLE（标记面≠缺陷面）。
    SamplingPlan plain = makePlan("colplain", {2U, 1U, 1U});
    const RegionCoverageQuery plainQuery = makeQuery({plain}, &solver);
    kin::WorkspaceSampler plainEvaluator(&view, plainQuery);
    const evidence::EvaluationOutput plainOut =
        plainEvaluator.evaluate(makeRequest({makeRef(plain, plainQuery.budget)}),
                                context);
    ASSERT_TRUE(plainOut.payload.has_value());
    ByteReader plainReader(plainOut.payload->canonicalBytes);
    plainReader.str(7);
    plainReader.u32();
    EXPECT_EQ(plainReader.u8(), 0U);  // incomplete
    EXPECT_EQ(plainReader.u8(), 0U);  // downgraded——无要求≠缺陷
    bool plainHasUnavailableDiag = false;
    for (const core::DiagnosticRecord& d : plainOut.diagnostics) {
        if (d.code == std::string(kin::kKinCollisionUnavailable)) {
            plainHasUnavailableDiag = true;
        }
    }
    EXPECT_FALSE(plainHasUnavailableDiag)
        << "无碰撞要求时评价缺失标记不产 KIN-COLLISION-UNAVAILABLE";
}
