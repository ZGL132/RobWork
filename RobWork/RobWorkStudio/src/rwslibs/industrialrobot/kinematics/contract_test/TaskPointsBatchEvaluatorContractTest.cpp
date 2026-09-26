/**
 * @file   TaskPointsBatchEvaluatorContractTest.cpp
 * @brief  kin.task-points-batch 契约用例组（KinTaskPointsBatchEval）——
 *         批量评估器的评估通道行为：descriptor 形态、展开三态素材、
 *         完成矩阵与 NotRun、证据组装形状（逐项证据行/搜索未果聚合/
 *         证明素材重绑/verdictInputs）、execution 通道消费（能力声明/
 *         批取消探针/进度/检查点 watermark/默认批大小）、确定性（注入
 *         序无关全序/线程数等价/分片连续区间）。任务契约 WP-15-T05
 *         acceptance 1~5 逐条具名自证。
 *
 * 设计依据：
 *   - units/kinematics.md §4.3（依赖声明行）、§7.1（批量执行图——appliesTo
 *     过滤/NotApplicable 显式标记/去重/NotRun/完整性自检）、§8.2（只产
 *     不判）、§8.3（execution 协作——能力声明/取消粒度/检查点 watermark/
 *     分批进度）、§8.4（确定性——全序/连续区间分片/全序归并）、§9.2
 *     （接口契约原文）
 *   - 治理裁决 O-37（宿主注入——TestView/替身求解器经工厂闭包注入，
 *     R-KIN-1"测试替身先行"口径）；P-KIN-7（检查点通道以
 *     IBatchCheckpointSink 最小端口承载——替身断言 watermark 序列）
 *   - 任务契约 tasks/foundation/WP-15-T05.json
 *
 * 测试策略：可控求解器替身（ScriptedSolver——按 (point,condition) 脚本
 * 返回指定结局，记录调用序与逐点线程 id，不调用取消探针——使批间/项
 * 起点的取消查询次数确定可数）；批量语义经 runBatchComputation 直调
 * 断言（NFR-MNT-01 计算库可直调），输出形状经 evaluate 端到端断言。
 */

#include "../test/KinFkFixture.hpp"

#include <sdurws/ird/evidence/Evaluator.hpp>
#include <sdurws/ird/evidence/Evidence.hpp>
#include <sdurws/ird/execution/TaskTypes.hpp>
#include <sdurws/ird/kinematics/DiagCodes.hpp>  // kKin* 码值常量（禁拼码断言面）
#include <sdurws/ird/kinematics/Evidence.hpp>
#include <sdurws/ird/kinematics/Evaluators.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace sdurws::ird::kinematics::testfixture;
namespace evidence = sdurws::ird::evidence;
namespace execution = sdurws::ird::execution;
namespace kin = sdurws::ird::kinematics;
using kin::BatchAppliesToScope;
using kin::BatchCondition;
using kin::BatchDemandCheck;
using kin::BatchItemStatus;
using kin::BatchQuery;
using kin::BatchRequirementLevel;
using kin::BatchTaskPoint;
using kin::IBatchCheckpointSink;
using kin::InitialValueStrategy;

namespace {

// =====================================================================
// 可控求解器替身（ScriptedSolver——IIkSolver 最小实现：按脚本返回结局
// ＋记录调用序/线程 id；不调用取消探针——取消查询次数可数的前提）
// =====================================================================

class ScriptedSolver final : public kin::IIkSolver {
public:
    /// 脚本键＝(pointOid, conditionId)；未命中走 defaultOutcome（装配期
    /// 填写、运行期只读——solve 为 const）。
    std::map<std::pair<core::ObjectId, core::ObjectId>, kin::IkOutcome> outcomes;
    kin::IkOutcome defaultOutcome;

    /// 观测面（互斥保护——threadCount>1 时多线程并发进入；solve 为
    /// const——观测成员 mutable，写入仅发生在替身内部）。
    mutable std::mutex mutex;
    mutable std::vector<std::pair<core::ObjectId, core::ObjectId>> callOrder;
    mutable std::map<core::ObjectId, std::thread::id> threadOfPoint;

    kin::IkOutcome solve(const kin::IkRequest& request) const override
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            // 批量通道 conditionId 恒有值（IkTargetRef 注——批量必填）。
            callOrder.emplace_back(request.targetRef.pointOid,
                                   *request.targetRef.conditionId);
            threadOfPoint[request.targetRef.pointOid] = std::this_thread::get_id();
        }
        const auto key = std::make_pair(request.targetRef.pointOid,
                                        *request.targetRef.conditionId);
        const auto it = outcomes.find(key);
        kin::IkOutcome outcome = it != outcomes.end() ? it->second : defaultOutcome;
        // 结局 1/4 的解集非空保证（BatchWorkItemRecord 不变式的脚本侧
        // 满足——未填解的脚本补最小解，避免 front() 空向量）。
        if ((outcome.outcomeKind == kin::IkOutcomeKind::SolutionsFound
             || outcome.outcomeKind == kin::IkOutcomeKind::PartialCollision)
            && outcome.solutionSet.solutions.empty()) {
            kin::KinematicSolution s;
            s.q = {0.1, -0.2};
            s.positionResidual = 1e-9;
            s.orientationResidual = 1e-9;
            s.minimumJointMargin = 0.5;
            s.manipulability = 1.0;
            s.conditionNumber = 1.0;
            outcome.solutionSet.solutions.push_back(s);
        }
        return outcome;
    }

    /// 便捷脚本：终局结局（非取消）——结局 2/3 附最小搜索记录（presence
    /// 不变式）、结局 1 附最小最佳解、结局 5 附最小证明素材（组装器
    /// 不变式的脚本侧满足；素材有效性校验归 evidence validateProof）。
    void script(const core::ObjectId& point, const core::ObjectId& condition,
                kin::IkOutcomeKind kind, const evidence::EvaluationRequest& req)
    {
        kin::IkOutcome outcome;
        outcome.outcomeKind = kind;
        if (kind == kin::IkOutcomeKind::SolutionsFound
            || kind == kin::IkOutcomeKind::PartialCollision) {
            kin::KinematicSolution s;
            s.q = {0.1, -0.2};               // 自由度 2（二连杆夹具）
            s.positionResidual = 1e-9;       // m（< 容差）
            s.orientationResidual = 1e-9;    // rad
            s.minimumJointMargin = 0.5;      // 无量纲（D-KIN-6）
            s.manipulability = 1.0;
            s.conditionNumber = 1.0;
            outcome.solutionSet.solutions.push_back(s);
        }
        if (kind == kin::IkOutcomeKind::MultiInitNoConvergence
            || kind == kin::IkOutcomeKind::AllCandidatesFiltered) {
            kin::IkSearchRecord record;
            record.searchBudgetUsed = 10U;
            record.initialGuessesTried = 2U;
            outcome.solutionSet.searchRecord = record;
        }
        if (kind == kin::IkOutcomeKind::AnalyticBoundExceeded) {
            kin::AnalyticBoundMaterial material;
            material.targetDistance = 10.0;  // m（> 工作半径上界）
            material.proof.category = evidence::ProofCategory::AnalyticBound;
            material.proof.claimToken = kin::kReachBeyondLinkSumClaim;
            material.proof.subject = point;
            material.proof.boundExpression = "fixture-bound";
            material.proof.preconditions = "fixture-preconditions";
            material.proof.coverageClaim = std::string(evidence::kCoverageClaimAllAlternatives);
            material.proof.snapshotId = req.snapshot.snapshotId;
            material.proof.sliceId = req.slice.sliceId;
            outcome.proofMaterial = material;
        }
        outcomes.emplace(std::make_pair(point, condition), std::move(outcome));
    }
};

// =====================================================================
// 批检查点替身（watermark 序列记录——P-KIN-7 最小端口的行为断言面）
// =====================================================================

class RecordingCheckpointSink final : public IBatchCheckpointSink {
public:
    std::vector<std::pair<std::uint64_t, std::uint64_t>> watermarks;

    void batchWatermark(std::uint64_t completedBatchCount,
                        std::uint64_t totalBatchCount) override
    {
        watermarks.emplace_back(completedBatchCount, totalBatchCount);
    }
};

// =====================================================================
// 上下文替身：取消查询计数（按阈值触发）＋进度记录
// =====================================================================

class CountingContext final : public evidence::IEvaluationContext {
public:
    /// threshold=取消阈值：第 threshold+1 次查询起返回 true（INT_MAX＝
    /// 永不取消——只计数）。
    explicit CountingContext(int threshold = 2147483647)
        : m_threshold(threshold)
    {
    }

    bool cancellationRequested() const override
    {
        ++m_cancelCalls;
        return m_cancelCalls > m_threshold;
    }
    void reportProgress(std::uint8_t percent, std::string_view phase) override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_percents.push_back(percent);
        m_phases.emplace_back(phase);
    }
    std::optional<std::vector<std::uint8_t>>
    tryObjectBytes(core::ObjectId, core::ContentVersion) const override
    {
        return std::nullopt;
    }

    long cancelCalls() const { return m_cancelCalls; }
    const std::vector<std::uint8_t>& percents() const { return m_percents; }
    const std::vector<std::string>& phases() const { return m_phases; }

private:
    int m_threshold;
    mutable long m_cancelCalls = 0;
    std::mutex m_mutex;
    std::vector<std::uint8_t> m_percents;
    std::vector<std::string> m_phases;
};

// =====================================================================
// 构造小工具（投影值/查询/请求——脚手架，非断言面）
// =====================================================================

core::ObjectId pointId(const std::string& seed)
{
    return idFrom<core::ObjectId>("kin-bp-" + seed);
}
core::ObjectId caseId(const std::string& seed)
{
    return idFrom<core::ObjectId>("kin-bc-" + seed);
}

/// 任务点投影（默认：启用/Must/良构目标——脚本求解下目标值不参与数值）。
BatchTaskPoint makePoint(const std::string& seed, bool enabled = true,
                         BatchRequirementLevel level = BatchRequirementLevel::Must)
{
    BatchTaskPoint p;
    p.pointOid = pointId(seed);
    p.level = level;
    p.enabled = enabled;
    p.targetInBase = trans(1.2, 0.0, 0.0);  // 可达环带内（真实求解路径用）
    p.positionTolerance = 1e-6;
    p.orientationTolerance = 1e-6;
    return p;
}

/// 工况投影（默认：启用/Must/AllStations）。
BatchCondition makeCondition(const std::string& seed, bool enabled = true,
                             BatchAppliesToScope scope = BatchAppliesToScope::AllStations)
{
    BatchCondition c;
    c.conditionId = caseId(seed);
    c.level = BatchRequirementLevel::Must;
    c.enabled = enabled;
    c.appliesTo = scope;
    return c;
}

/// 批量查询（二连杆视图下的良构默认——solver 必注入替身或内置求解器）。
BatchQuery makeQuery(std::vector<BatchTaskPoint> points,
                     std::vector<BatchCondition> conditions,
                     kin::IIkSolver* solver)
{
    BatchQuery q;
    q.points = std::move(points);
    q.conditions = std::move(conditions);
    q.defaultTcp.toolObject = idFrom<core::ObjectId>("kin-tool");
    q.referenceQ = {0.0, 0.0};
    q.initialStrategy = InitialValueStrategy::ReferenceQ;
    q.initialValuesCount = 1U;
    q.iterationLimit = 16U;
    q.solver = solver;
    return q;
}

/// 请求壳（caseSubset 由测试填充；身份面固定派生——绑定断言面）。
evidence::EvaluationRequest makeRequest(std::vector<evidence::CaseId> cases)
{
    evidence::EvaluationRequest req;
    req.mode = core::EvaluationMode::Verified;
    req.snapshot.snapshotId.bytes = digestOf("kin-batch-snap");
    req.slice.sliceId.bytes = digestOf("kin-batch-slice");
    req.task.attempt.value = 1U;
    req.caseSubset = std::move(cases);
    return req;
}

}  // namespace

// =====================================================================
// descriptor——§4.3 kin.task-points-batch 行（七条依赖/双模式/无状态/
// 完全线程安全；键词形 kebab 随附同步偏差——T03/T04 同口径）
// =====================================================================

TEST(KinTaskPointsBatchEval, DescriptorMatchesSection43Row_WP15T05_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-03"},
                  std::vector<std::string>{});

    const evidence::EvaluatorDescriptor d = kin::makeTaskPointsBatchDescriptor();

    // 键（实现键 kebab 形态——卡面点形键的随附同步偏差）与契约版本。
    EXPECT_EQ(d.key, std::string(kin::kTaskPointsBatchEvaluationKey));
    EXPECT_EQ(d.contractVersion, kin::kTaskPointsBatchContractVersion);

    // 依赖声明七条（§4.3 行：pose-metrics 五条＋req.points＋req.conditions；
    // 全部 Required——批量通道无条件依赖）。
    ASSERT_EQ(d.inputs.size(), 7U);
    EXPECT_EQ(d.inputs[0].key, "model.robot-design");
    EXPECT_EQ(d.inputs[1].key, "tcp");
    EXPECT_EQ(d.inputs[2].key, "policy.resolved");
    EXPECT_EQ(d.inputs[3].key, "namemap");
    EXPECT_EQ(d.inputs[4].key, "config.ik");
    EXPECT_EQ(d.inputs[5].key, "req.points");
    EXPECT_EQ(d.inputs[6].key, "req.conditions");
    for (const auto& decl : d.inputs) {
        EXPECT_EQ(decl.requiredness, evidence::DependencyRequiredness::Required);
    }

    // Profile 声明（域不可申报——contentIdentity 零值，evidence §9.5/R-3）。
    EXPECT_EQ(d.profile.profileId, "kin");
    EXPECT_EQ(d.profile.version, "1");
    EXPECT_FALSE(d.profile.contentIdentity.isValid());

    // 模式集两值（Quick/Verified——§4.3 行；批量通道不做 Preview）。
    ASSERT_EQ(d.supportedModes.size(), 2U);
    EXPECT_EQ(d.supportedModes[0], core::EvaluationMode::Quick);
    EXPECT_EQ(d.supportedModes[1], core::EvaluationMode::Verified);

    // stateless＋FullyThreadSafe（§9.2 IKinematicBatchEvaluator 头注——
    // 并行分片要求可重入）。
    EXPECT_TRUE(d.stateless);
    EXPECT_EQ(d.threadSafety, evidence::ThreadSafety::FullyThreadSafe);
}

// =====================================================================
// ACC1——展开：appliesTo 过滤/停用与 None→NotApplicable 显式标记/重复
// 项集合语义去重（悬空引用另见下一用例）
// =====================================================================

TEST(KinTaskPointsBatchEval, ExpansionAppliesToNotApplicableAndDedup_WP15T05_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-03", "EVI-02"},
                  std::vector<std::string>{"ERR-01"});

    TestView view(twoLinkModel());
    ScriptedSolver solver;  // 默认 SolutionsFound——展开语义与结局解耦
    const core::ObjectId p1 = pointId("p1");
    const core::ObjectId p2 = pointId("p2");

    BatchQuery q = makeQuery({makePoint("p1"), makePoint("p2", /*enabled=*/false)},
                             {/*conditions 下方逐条填*/}, &solver);
    q.conditions.push_back(makeCondition("call"));  // AllStations
    BatchCondition stations = makeCondition("cstations", true,
                                            BatchAppliesToScope::Stations);
    stations.stations = {p1, p1, p1};  // 重复登记——集合语义去重
    q.conditions.push_back(stations);
    q.conditions.push_back(makeCondition("cnone", true, BatchAppliesToScope::None));

    const std::vector<evidence::CaseId> cases = {caseId("call"), caseId("cstations"),
                                                 caseId("cnone")};
    CountingContext context;
    const kin::BatchComputation computation =
        kin::runBatchComputation(view, q, makeRequest(cases), context);

    // 期望工作项：call×(p1,p2)、cstations×p1（p2 不在清单——过滤；p1
    // 重复三次去重为一项）、cnone×(p1,p2)（None→NotApplicable 显式标记
    // ——不静默丢弃，ERR-01）＝2+1+2＝5 项。
    ASSERT_EQ(computation.items.size(), 5U);
    ASSERT_EQ(computation.caseSubset.size(), 3U);

    // 逐项状态核对（按 (point,condition) 查表——排序在计算内完成）。
    std::map<std::pair<core::ObjectId, core::ObjectId>, BatchItemStatus> table;
    for (const kin::BatchWorkItemRecord& item : computation.items) {
        table.emplace(std::make_pair(item.pointOid, item.conditionId), item.status);
    }
    EXPECT_EQ(table.at({p1, caseId("call")}), BatchItemStatus::CandidateFound);
    EXPECT_EQ(table.at({p2, caseId("call")}), BatchItemStatus::NotApplicable);
    EXPECT_EQ(table.at({p1, caseId("cstations")}), BatchItemStatus::CandidateFound);
    EXPECT_EQ(table.at({p1, caseId("cnone")}), BatchItemStatus::NotApplicable);
    EXPECT_EQ(table.at({p2, caseId("cnone")}), BatchItemStatus::NotApplicable);
    // NotApplicable 显式标记附原因（ERR-01——不伪造）。
    for (const kin::BatchWorkItemRecord& item : computation.items) {
        if (item.status == BatchItemStatus::NotApplicable) {
            EXPECT_FALSE(item.reason.empty());
        }
    }
    // 计数守恒（acceptance 2 自检公式的展开期观测）。
    EXPECT_EQ(computation.totalWorkItems, 5U);
    EXPECT_EQ(computation.processedItemCount, 5U);
    EXPECT_EQ(computation.notRunItemCount, 0U);
    EXPECT_FALSE(computation.incomplete);
}

// =====================================================================
// ACC1——悬空引用→InputInvalid 素材（V-12；附 KIN-POINT-REF-DANGLING
// 素材行——evidence Invalid 状态＋invalidReason）
// =====================================================================

TEST(KinTaskPointsBatchEval, DanglingStationReferenceYieldsInputInvalid_WP15T05_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-03"},
                  std::vector<std::string>{"V-12"});

    TestView view(twoLinkModel());
    ScriptedSolver solver;
    const core::ObjectId p1 = pointId("p1");
    const core::ObjectId dangling = idFrom<core::ObjectId>("kin-bp-dangling");

    BatchQuery q = makeQuery({makePoint("p1")}, {}, &solver);
    BatchCondition stations = makeCondition("cst", true, BatchAppliesToScope::Stations);
    stations.stations = {p1, dangling};  // 一个有效＋一个悬空
    q.conditions.push_back(stations);

    CountingContext context;
    const kin::BatchComputation computation = kin::runBatchComputation(
        view, q, makeRequest({caseId("cst")}), context);

    // 悬空对象产 InputInvalid 工作项（不静默丢弃）；有效点正常求解。
    ASSERT_EQ(computation.items.size(), 2U);
    std::map<std::pair<core::ObjectId, core::ObjectId>, BatchItemStatus> table;
    const kin::BatchWorkItemRecord* danglingRecord = nullptr;
    for (const kin::BatchWorkItemRecord& item : computation.items) {
        table.emplace(std::make_pair(item.pointOid, item.conditionId), item.status);
        if (item.pointOid == dangling) {
            danglingRecord = &item;
        }
    }
    EXPECT_EQ(table.at({dangling, caseId("cst")}), BatchItemStatus::InputInvalid);
    EXPECT_EQ(table.at({p1, caseId("cst")}), BatchItemStatus::CandidateFound);
    ASSERT_NE(danglingRecord, nullptr);
    EXPECT_FALSE(danglingRecord->reason.empty());

    // 端到端组装：InputInvalid 行为 evidence Invalid 状态＋KIN-POINT-REF-
    // DANGLING 原因诊断（§9.6 行 16 的消费面）。
    ScriptedSolver solver2;
    kin::TaskPointsBatchEvaluator evaluator(
        &view, makeQuery({makePoint("p1")}, {stations}, &solver2));
    CountingContext context2;
    const evidence::EvaluationOutput out =
        evaluator.evaluate(makeRequest({caseId("cst")}), context2);
    bool hasInvalidRow = false;
    for (const evidence::EvidenceItem& row : out.evidence) {
        if (row.status == evidence::EvidenceItemStatus::Invalid) {
            hasInvalidRow = true;
            ASSERT_TRUE(row.invalidReason.has_value());
            EXPECT_EQ(row.invalidReason->code, std::string(kin::kKinPointRefDangling));
        }
    }
    EXPECT_TRUE(hasInvalidRow);
}

// =====================================================================
// ACC1——per-item 计算状态独立（四类脚本结局逐项映射；一项失败不判
// 全任务——只算不判，§7.1）
// =====================================================================

TEST(KinTaskPointsBatchEval, PerItemStatesIndependent_WP15T05_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-03"},
                  std::vector<std::string>{"EVI-01"});

    TestView view(twoLinkModel());
    ScriptedSolver solver;
    const core::ObjectId pFound = pointId("found");
    const core::ObjectId pNoConv = pointId("noconv");
    const core::ObjectId pFiltered = pointId("filtered");
    const core::ObjectId pBound = pointId("bound");

    BatchQuery q = makeQuery(
        {makePoint("found"), makePoint("noconv"), makePoint("filtered"),
         makePoint("bound")},
        {makeCondition("c")}, &solver);
    const evidence::EvaluationRequest req = makeRequest({caseId("c")});
    solver.script(pFound, caseId("c"), kin::IkOutcomeKind::SolutionsFound, req);
    solver.script(pNoConv, caseId("c"),
                  kin::IkOutcomeKind::MultiInitNoConvergence, req);
    solver.script(pFiltered, caseId("c"), kin::IkOutcomeKind::AllCandidatesFiltered,
                  req);
    solver.script(pBound, caseId("c"), kin::IkOutcomeKind::AnalyticBoundExceeded,
                  req);

    CountingContext context;
    const kin::BatchComputation computation =
        kin::runBatchComputation(view, q, req, context);

    // 四项四种状态——互相独立（一项失败不污染他项，§7.1 per-item 独立）。
    std::map<core::ObjectId, BatchItemStatus> table;
    std::map<core::ObjectId, const kin::BatchWorkItemRecord*> records;
    for (const kin::BatchWorkItemRecord& item : computation.items) {
        table[item.pointOid] = item.status;
        records[item.pointOid] = &item;
    }
    EXPECT_EQ(table.at(pFound), BatchItemStatus::CandidateFound);
    EXPECT_EQ(table.at(pNoConv), BatchItemStatus::NoConvergence);
    EXPECT_EQ(table.at(pFiltered), BatchItemStatus::AllFiltered);
    EXPECT_EQ(table.at(pBound), BatchItemStatus::BoundExceeded);

    // 逐态 presence 纪律（BatchWorkItemRecord 不变式的产出侧）。
    EXPECT_TRUE(records.at(pFound)->bestSolution.has_value());
    EXPECT_TRUE(records.at(pNoConv)->searchRecord.has_value());
    EXPECT_TRUE(records.at(pFiltered)->searchRecord.has_value());
    EXPECT_TRUE(records.at(pBound)->proofMaterial.has_value());
    // 全批无不可行判定（只算不判——无 verdictInputs 之外的判定字段，
    // 最终判定归 evidence aggregateVerdict）。
    EXPECT_EQ(computation.totalWorkItems, 4U);
    EXPECT_FALSE(computation.incomplete);
}

// =====================================================================
// ACC2——完成矩阵素材（逐工况终态标记；无取消→全部 executed；停用
// 工况→notApplicable）
// =====================================================================

TEST(KinTaskPointsBatchEval, CaseCompletionMatrixMaterial_WP15T05_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-03", "EVI-02"},
                  std::vector<std::string>{});

    TestView view(twoLinkModel());
    ScriptedSolver solver;
    const core::ObjectId ca = caseId("active");
    const core::ObjectId cd = caseId("disabled");

    BatchQuery q = makeQuery({makePoint("p1"), makePoint("p2")},
                             {makeCondition("active"), makeCondition("disabled", false)},
                             &solver);
    CountingContext context;
    const kin::BatchComputation computation = kin::runBatchComputation(
        view, q, makeRequest({ca, cd}), context);

    // 完成矩阵素材：active→executed（两启用点均求解完成）；disabled→
    // notApplicable（工况停用——不计漏验）。
    ASSERT_EQ(computation.caseCompletion.size(), 2U);
    const kin::BatchCaseCompletion& activeRow =
        computation.caseCompletion[0].conditionId == ca
            ? computation.caseCompletion[0]
            : computation.caseCompletion[1];
    const kin::BatchCaseCompletion& disabledRow =
        computation.caseCompletion[0].conditionId == ca
            ? computation.caseCompletion[1]
            : computation.caseCompletion[0];
    EXPECT_TRUE(activeRow.executed);
    EXPECT_FALSE(activeRow.notRun);
    EXPECT_FALSE(activeRow.notApplicable);
    EXPECT_EQ(activeRow.computedItemCount, 2U);   // p1/p2 求解完成
    EXPECT_EQ(activeRow.notRunItemCount, 0U);
    EXPECT_TRUE(disabledRow.notApplicable);
    EXPECT_FALSE(disabledRow.executed);
    EXPECT_EQ(disabledRow.computedItemCount, 0U);
    // 全批完整（无 NotRun——incomplete=false；§7.1 完整性校验通过面）。
    EXPECT_FALSE(computation.incomplete);
    EXPECT_EQ(computation.notRunItemCount, 0U);
    EXPECT_EQ(computation.completedBatchCount, computation.batchCount);
}

// =====================================================================
// ACC2——分批取消：未完成批如实 NotRun＋incomplete 标记＋
// KIN-RESULT-INCOMPLETE 诊断＋watermark 停止推进（无伪完成）
// =====================================================================

TEST(KinTaskPointsBatchEval, CancellationMarksNotRunAndIncomplete_WP15T05_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-03", "EVI-02", "NFR-PERF-02"},
                  std::vector<std::string>{"TASK-02"});

    TestView view(twoLinkModel());
    ScriptedSolver solver;  // 不调探针——取消查询次数可数（见下阈值）
    std::vector<BatchTaskPoint> points;
    std::vector<evidence::CaseId> cases = {caseId("c")};
    for (int i = 1; i <= 6; ++i) {
        points.push_back(makePoint("p" + std::to_string(i)));
    }
    BatchQuery q = makeQuery(std::move(points), {makeCondition("c")}, &solver);
    q.maxBatchSize = 2U;  // 6 项→3 批；批间/项起点查询次序确定

    // 取消查询序列（脚本求解器零探针调用）：批1 起点(1)＋批1 两项起点
    // (2,3)＝3 次 false；批2 起点＝第 4 次 true——批 2 开始前取消。
    CountingContext context(3);
    RecordingCheckpointSink sink;
    q.checkpointSink = &sink;
    const kin::BatchComputation computation =
        kin::runBatchComputation(view, q, makeRequest(cases), context);

    // 批 1 两项已解、批 2/3 四项 NotRun（如实标记，无伪完成）。
    EXPECT_EQ(computation.totalWorkItems, 6U);
    EXPECT_EQ(computation.processedItemCount, 2U);
    EXPECT_EQ(computation.notRunItemCount, 4U);
    EXPECT_TRUE(computation.incomplete);
    EXPECT_EQ(computation.completedBatchCount, 1U);
    EXPECT_EQ(computation.batchCount, 3U);
    // watermark 只推进到已完成批（(1,3) 一次——批 2 未完成不写）。
    ASSERT_EQ(sink.watermarks.size(), 1U);
    EXPECT_EQ(sink.watermarks[0].first, 1U);
    EXPECT_EQ(sink.watermarks[0].second, 3U);
    // NotRun 项原因文本齐备（ERR-01——显式标记不伪造）。
    for (const kin::BatchWorkItemRecord& item : computation.items) {
        if (item.status == BatchItemStatus::NotRun) {
            EXPECT_FALSE(item.reason.empty());
        }
    }
    // 完成矩阵：唯一工况存在 NotRun 项→notRun 标记（漏验素材交 evidence
    // 覆盖矩阵②级门禁——本单元只标记不判定）。
    ASSERT_EQ(computation.caseCompletion.size(), 1U);
    EXPECT_TRUE(computation.caseCompletion[0].notRun);
    EXPECT_EQ(computation.caseCompletion[0].notRunItemCount, 4U);
    EXPECT_EQ(computation.caseCompletion[0].computedItemCount, 2U);
}

// =====================================================================
// ACC3——证据组装形状：逐项证据行/搜索未果聚合/verdictInputs（REQ-06
// 口径）/payload 绑定（evidence §9.3 EvaluationOutput 形状）
// =====================================================================

TEST(KinTaskPointsBatchEval, BuilderOutputShapeAndVerdictInputs_WP15T05_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-03", "EVI-01", "EVI-02"},
                  std::vector<std::string>{"REQ-06"});

    TestView view(twoLinkModel());
    ScriptedSolver solver;
    const core::ObjectId pFound = pointId("found");
    const core::ObjectId pNoConv = pointId("noconv");
    const core::ObjectId pBoundMust = pointId("boundmust");
    const core::ObjectId pBoundShould = pointId("boundshould");

    BatchQuery q = makeQuery(
        {makePoint("found"), makePoint("noconv"),
         makePoint("boundmust", true, BatchRequirementLevel::Must),
         makePoint("boundshould", true, BatchRequirementLevel::Should)},
        {makeCondition("c")}, &solver);
    const evidence::EvaluationRequest req = makeRequest({caseId("c")});
    solver.script(pFound, caseId("c"), kin::IkOutcomeKind::SolutionsFound, req);
    solver.script(pNoConv, caseId("c"),
                  kin::IkOutcomeKind::MultiInitNoConvergence, req);
    solver.script(pBoundMust, caseId("c"), kin::IkOutcomeKind::AnalyticBoundExceeded,
                  req);
    solver.script(pBoundShould, caseId("c"), kin::IkOutcomeKind::AnalyticBoundExceeded,
                  req);

    kin::TaskPointsBatchEvaluator evaluator(&view, q);
    CountingContext context;
    const evidence::EvaluationOutput out = evaluator.evaluate(req, context);

    // 逐项证据行（每工作项一条"kin.task-point-outcome"行，subject=点、
    // caseScope=工况；四计算终态→Satisfied——presence 纪律摘要必填）＋
    // 搜索未果汇总行（聚合记录在场时追加）。
    ASSERT_EQ(out.evidence.size(), 5U);
    std::size_t outcomeRows = 0;
    std::size_t satisfiedRows = 0;
    std::size_t searchRows = 0;
    for (const evidence::EvidenceItem& row : out.evidence) {
        if (row.itemId == kin::kKinBatchItemOutcomeRowId) {
            ++outcomeRows;
            if (row.status == evidence::EvidenceItemStatus::Satisfied) {
                ++satisfiedRows;
                ASSERT_TRUE(row.artifactDigest.has_value());
                ASSERT_TRUE(row.caseScope.has_value());
                ASSERT_EQ(row.caseScope->size(), 1U);
                EXPECT_EQ(row.caseScope->front(), caseId("c"));
            }
        }
        if (row.itemId == kin::kKinBatchSearchRowId) {
            ++searchRows;
        }
    }
    EXPECT_EQ(outcomeRows, 4U);
    EXPECT_EQ(satisfiedRows, 4U);
    EXPECT_EQ(searchRows, 1U);

    // 搜索未果聚合（组装口径：预算/初值数求和——单条 NoConvergence 项
    // 10/2；过滤清单按全序拼接）。
    ASSERT_TRUE(out.searchRecord.has_value());
    EXPECT_EQ(out.searchRecord->searchBudgetUsed, 10U);
    EXPECT_EQ(out.searchRecord->initialGuessesTried, 2U);

    // verdictInputs（REQ-06 口径填报）：Must 点的解析界限素材→
    // mustViolations；Should 点→shouldViolations；搜索未果不产生违例
    // （C5/C8——DataInsufficient 口径，不得升级为不可行）。
    EXPECT_EQ(out.verdictInputs.mustViolations.size(), 1U);
    EXPECT_EQ(out.verdictInputs.mustViolations[0].itemId, pBoundMust.toCanonical());
    EXPECT_EQ(out.verdictInputs.shouldViolations.size(), 1U);
    EXPECT_EQ(out.verdictInputs.shouldViolations[0].itemId,
              pBoundShould.toCanonical());

    // payload（§5.6 绑定——token/摘要一致性；绑定字段核对随单元测试
    // 的载荷头解析用例）。
    ASSERT_TRUE(out.payload.has_value());
    EXPECT_EQ(out.payload->kindToken, std::string(kin::kTaskPointsBatchPayloadToken));
    core::ContentDigester d;
    d.update(out.payload->canonicalBytes.data(), out.payload->canonicalBytes.size());
    core::ContentIdentity digest;
    digest.bytes = d.finalize();
    EXPECT_EQ(out.payload->digest, digest);

    // 完整批：无 KIN-RESULT-INCOMPLETE（诊断面收敛——组装口径 5）。
    bool hasIncompleteDiag = false;
    for (const auto& diag : out.diagnostics) {
        if (diag.code == std::string(kin::kKinResultIncomplete)) {
            hasIncompleteDiag = true;
        }
    }
    EXPECT_FALSE(hasIncompleteDiag);
}

// =====================================================================
// ACC3——证明素材 producer 重绑（对外交付产生者＝批量评估器——
// validateProof 第③查的注册查证对象；绑定身份保持请求值）
// =====================================================================

TEST(KinTaskPointsBatchEval, ProofProducerReboundToBatchKey_WP15T05_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-03", "EVI-01"},
                  std::vector<std::string>{"D-09"});

    TestView view(twoLinkModel());
    ScriptedSolver solver;
    const core::ObjectId pBound = pointId("bound");
    BatchQuery q = makeQuery({makePoint("bound")}, {makeCondition("c")}, &solver);
    const evidence::EvaluationRequest req = makeRequest({caseId("c")});
    solver.script(pBound, caseId("c"), kin::IkOutcomeKind::AnalyticBoundExceeded, req);

    kin::TaskPointsBatchEvaluator evaluator(&view, q);
    CountingContext context;
    const evidence::EvaluationOutput out = evaluator.evaluate(req, context);

    // 证明素材在场（仅素材不裁定）且 producer 重绑为批量评估键＋契约
    // 版本；snapshotId/sliceId 绑定保持请求值（expectedSliceId——D-09
    // 可校验面）。
    ASSERT_TRUE(out.proof.has_value());
    EXPECT_EQ(out.proof->producer, std::string(kin::kTaskPointsBatchEvaluationKey));
    EXPECT_EQ(out.proof->producerContractVersion, kin::kTaskPointsBatchContractVersion);
    EXPECT_EQ(out.proof->snapshotId, req.snapshot.snapshotId);
    EXPECT_EQ(out.proof->sliceId, req.slice.sliceId);
    EXPECT_EQ(out.proof->category, evidence::ProofCategory::AnalyticBound);
    EXPECT_EQ(out.proof->subject, pBound);
}

// =====================================================================
// ACC4——execution 通道消费：批间取消探针粒度/分批进度/批 watermark
// （V-22 本单元侧；NFR-PERF-02；§8.3 协作取消与检查点）
// =====================================================================

TEST(KinTaskPointsBatchEval, PerBatchCancelProbeProgressAndWatermark_WP15T05_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-03", "NFR-PERF-02"},
                  std::vector<std::string>{"V-22"});

    TestView view(twoLinkModel());
    ScriptedSolver solver;
    std::vector<BatchTaskPoint> points;
    for (int i = 1; i <= 6; ++i) {
        points.push_back(makePoint("p" + std::to_string(i)));
    }
    BatchQuery q = makeQuery(std::move(points), {makeCondition("c")}, &solver);
    q.maxBatchSize = 2U;  // 3 批

    // 完整跑：取消查询每批至少一次（≥批数——V-22 本单元侧）；进度批
    // 粒度（每批恰一次、phase="solve-batch"、percent 单调到 100）；
    // watermark 每批一次 ((k,3) 序列)。
    CountingContext context;
    RecordingCheckpointSink sink;
    q.checkpointSink = &sink;
    const kin::BatchComputation computation =
        kin::runBatchComputation(view, q, makeRequest({caseId("c")}), context);

    // 批间查询＋项起点查询——总数下界＝批数（每批至少一次）。
    EXPECT_GE(context.cancelCalls(), 3L);
    EXPECT_FALSE(computation.incomplete);
    EXPECT_EQ(computation.completedBatchCount, 3U);
    // 进度：批粒度三次、phase 固定、percent 非降且末次 100。
    ASSERT_EQ(context.percents().size(), 3U);
    ASSERT_EQ(context.phases().size(), 3U);
    for (const std::string& phase : context.phases()) {
        EXPECT_EQ(phase, "solve-batch");
    }
    for (std::size_t i = 1; i < context.percents().size(); ++i) {
        EXPECT_GE(context.percents()[i], context.percents()[i - 1]);
    }
    EXPECT_EQ(context.percents().back(), 100U);
    // watermark：每批一次，(k,3) 递增序列（幂等键＝批号+sliceId 的
    // 批号面——§8.3）。
    ASSERT_EQ(sink.watermarks.size(), 3U);
    for (std::uint64_t k = 0; k < sink.watermarks.size(); ++k) {
        EXPECT_EQ(sink.watermarks[k].first, k + 1U);
        EXPECT_EQ(sink.watermarks[k].second, 3U);
    }
}

// =====================================================================
// ACC4——黄金锁定默认批大小 256（D-KIN-6）＋大批量分批实例
// =====================================================================

TEST(KinTaskPointsBatchEval, DefaultBatchSizeIs256_WP15T05_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-03"},
                  std::vector<std::string>{"D-KIN-6"});

    // 黄金锁定常量（修改走设计变更——卡面 §7.1/D-KIN-6）。
    EXPECT_EQ(kin::kBatchDefaultBatchSize, 256U);
    EXPECT_EQ(BatchQuery{}.maxBatchSize, kin::kBatchDefaultBatchSize);

    // 300 项→2 批（256＋44——默认批大小的非整除分批实例化）。
    TestView view(twoLinkModel());
    ScriptedSolver solver;
    std::vector<BatchTaskPoint> points;
    for (int i = 1; i <= 300; ++i) {
        points.push_back(makePoint("p" + std::to_string(i)));
    }
    BatchQuery q = makeQuery(std::move(points), {makeCondition("c")}, &solver);
    ASSERT_EQ(q.maxBatchSize, 256U);  // 默认值未被覆盖
    RecordingCheckpointSink sink;
    q.checkpointSink = &sink;
    CountingContext context;
    const kin::BatchComputation computation =
        kin::runBatchComputation(view, q, makeRequest({caseId("c")}), context);

    EXPECT_EQ(computation.batchCount, 2U);
    EXPECT_EQ(computation.completedBatchCount, 2U);
    ASSERT_EQ(sink.watermarks.size(), 2U);
    EXPECT_EQ(sink.watermarks.back().first, 2U);
    EXPECT_EQ(sink.watermarks.back().second, 2U);
}

// =====================================================================
// ACC5——确定性：注入序无关的全序（展开→排序收敛同一全序；同线程数
// 逐位一致）
// =====================================================================

TEST(KinTaskPointsBatchEval, TotalOrderIndependentOfInjectionOrder_WP15T05_ACC5)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-03"},
                  std::vector<std::string>{"NFR-COR-02"});

    TestView view(twoLinkModel());
    ScriptedSolver solver;
    const core::ObjectId ca = caseId("ca");
    const core::ObjectId cb = caseId("cb");

    std::vector<BatchTaskPoint> points;
    for (int i = 1; i <= 5; ++i) {
        points.push_back(makePoint("p" + std::to_string(i)));
    }
    // 注入序 A：点序反排；工况序 cb,ca。注入序 B：原序＋ca,cb。
    std::vector<BatchTaskPoint> reversed(points.rbegin(), points.rend());
    BatchQuery qa = makeQuery(reversed, {makeCondition("cb"), makeCondition("ca")},
                              &solver);
    BatchQuery qb = makeQuery(points, {makeCondition("ca"), makeCondition("cb")},
                              &solver);

    CountingContext contextA;
    CountingContext contextB;
    const kin::BatchComputation computationA =
        kin::runBatchComputation(view, qa, makeRequest({cb, ca}), contextA);
    const kin::BatchComputation computationB =
        kin::runBatchComputation(view, qb, makeRequest({ca, cb}), contextB);

    // 全序收敛：两种注入序的工作项 (point,condition) 序逐位一致（§8.4
    // ——分片前全序确定）；caseSubset 规范化同序。
    ASSERT_EQ(computationA.items.size(), computationB.items.size());
    for (std::size_t i = 0; i < computationA.items.size(); ++i) {
        EXPECT_EQ(computationA.items[i].pointOid, computationB.items[i].pointOid);
        EXPECT_EQ(computationA.items[i].conditionId, computationB.items[i].conditionId);
        EXPECT_EQ(computationA.items[i].status, computationB.items[i].status);
    }
    EXPECT_EQ(computationA.caseSubset, computationB.caseSubset);

    // 端到端逐位一致（同输入同线程数→payload 字节一致——NFR-COR-01）。
    kin::TaskPointsBatchEvaluator evaluatorA(&view, qa);
    kin::TaskPointsBatchEvaluator evaluatorB(&view, qb);
    CountingContext contextC;
    CountingContext contextD;
    const evidence::EvaluationOutput outA = evaluatorA.evaluate(
        makeRequest({cb, ca}), contextC);
    const evidence::EvaluationOutput outB = evaluatorB.evaluate(
        makeRequest({ca, cb}), contextD);
    ASSERT_TRUE(outA.payload.has_value());
    ASSERT_TRUE(outB.payload.has_value());
    EXPECT_EQ(outA.payload->canonicalBytes, outB.payload->canonicalBytes);
}

// =====================================================================
// ACC5——异线程数等价集合＋稳定排序一致（本实现逐位一致——§8.4 数值
// 等价承诺的从严面）
// =====================================================================

TEST(KinTaskPointsBatchEval, ThreadCountEquivalence_WP15T05_ACC5)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-03"},
                  std::vector<std::string>{"NFR-COR-02", "V-21"});

    TestView view(twoLinkModel());
    ScriptedSolver solver;
    std::vector<BatchTaskPoint> points;
    for (int i = 1; i <= 40; ++i) {
        points.push_back(makePoint("p" + std::to_string(i)));
    }
    const evidence::EvaluationRequest req = makeRequest({caseId("c")});

    BatchQuery q1 = makeQuery(points, {makeCondition("c")}, &solver);
    q1.threadCount = 1U;
    BatchQuery q4 = makeQuery(points, {makeCondition("c")}, &solver);
    q4.threadCount = 4U;

    kin::TaskPointsBatchEvaluator evaluator1(&view, q1);
    kin::TaskPointsBatchEvaluator evaluator4(&view, q4);
    CountingContext context1;
    CountingContext context4;
    const evidence::EvaluationOutput out1 = evaluator1.evaluate(req, context1);
    const evidence::EvaluationOutput out4 = evaluator4.evaluate(req, context4);

    // 等价集合＋稳定排序一致（§8.4 承诺）；本实现结果按全序槽位写回，
    // 实际逐位一致（从严观测）。
    ASSERT_TRUE(out1.payload.has_value());
    ASSERT_TRUE(out4.payload.has_value());
    EXPECT_EQ(out1.payload->canonicalBytes, out4.payload->canonicalBytes);
    // 证据行序（工作项全序）一致。
    ASSERT_EQ(out1.evidence.size(), out4.evidence.size());
    for (std::size_t i = 0; i < out1.evidence.size(); ++i) {
        EXPECT_EQ(out1.evidence[i].subject, out4.evidence[i].subject);
        EXPECT_EQ(out1.evidence[i].status, out4.evidence[i].status);
    }
    // 40 项×两次评估全部求解（替身调用计数——无项因并行丢失，两轮等量）。
    std::lock_guard<std::mutex> lock(solver.mutex);
    EXPECT_EQ(solver.callOrder.size(), 80U);
}

// =====================================================================
// ACC5——并行分片＝全序工作项的连续区间（V-21 断言面——同片线程同、
// 片间线程异、区间按全序下标连续）
// =====================================================================

TEST(KinTaskPointsBatchEval, ShardsAreContiguousRanges_WP15T05_ACC5)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-03"},
                  std::vector<std::string>{"V-21", "NFR-PERF"});

    TestView view(twoLinkModel());
    ScriptedSolver solver;
    std::vector<BatchTaskPoint> points;
    for (int i = 1; i <= 8; ++i) {
        points.push_back(makePoint("p" + std::to_string(i)));
    }
    BatchQuery q = makeQuery(points, {makeCondition("c")}, &solver);
    q.maxBatchSize = 8U;   // 单批——批内 2 分片
    q.threadCount = 2U;

    CountingContext context;
    const kin::BatchComputation computation =
        kin::runBatchComputation(view, q, makeRequest({caseId("c")}), context);

    // 全序下标（计算内排序结果——分片区间的基准序）。
    ASSERT_EQ(computation.items.size(), 8U);
    std::vector<std::thread::id> threadByIndex;
    for (const kin::BatchWorkItemRecord& item : computation.items) {
        threadByIndex.push_back(solver.threadOfPoint.at(item.pointOid));
    }
    // 两分片：[0..3] 同线程、[4..7] 同线程、两线程不同——连续区间
    // （§8.4"分片＝全序工作项的连续区间"）。
    std::thread::id firstHalf = threadByIndex[0];
    std::thread::id secondHalf = threadByIndex[4];
    for (std::size_t i = 0; i < 4; ++i) {
        EXPECT_EQ(threadByIndex[i], firstHalf) << "前半分片线程漂移 @ " << i;
        EXPECT_EQ(threadByIndex[i + 4], secondHalf) << "后半分片线程漂移 @ " << i + 4;
    }
    EXPECT_NE(firstHalf, secondHalf);
}

// =====================================================================
// ACC4——能力声明值（取消 ✓/检查点=批 watermark/暂停不支持如实声明/
// 强制终止代价低——§8.3 提交行原文三件套）
// =====================================================================

TEST(KinTaskPointsBatchEval, CapabilityDeclarationValues_WP15T05_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-03"},
                  std::vector<std::string>{});

    const execution::TaskCapability capability = kin::taskPointsBatchCapability();
    EXPECT_FALSE(capability.supportsPause) << "R1 如实声明：不支持暂停（§8.3）";
    EXPECT_EQ(capability.checkpointGranularity, execution::CheckpointGranularity::Batch)
        << "检查点＝批 watermark（§8.3 能力声明原文）";
    EXPECT_EQ(capability.forceTerminateCost, execution::ForceTerminateCost::Cheap)
        << "强制终止代价＝低（批间边界即安全中止点）";
    // 纯函数确定性（NFR-COR-02——同调用同值）。
    const execution::TaskCapability again = kin::taskPointsBatchCapability();
    EXPECT_EQ(capability.supportsPause, again.supportsPause);
    EXPECT_EQ(capability.checkpointGranularity, again.checkpointGranularity);
    EXPECT_EQ(capability.forceTerminateCost, again.forceTerminateCost);
}
