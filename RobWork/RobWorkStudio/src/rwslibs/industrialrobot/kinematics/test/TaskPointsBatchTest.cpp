/**
 * @file   TaskPointsBatchTest.cpp
 * @brief  批量任务点验证用例组（KinTaskPointsBatch，T05 批）——真实
 *         求解器路径的逐项状态映射（V-05/V-06/结局 5 素材的批量侧）、
 *         要求值对比素材（实际/要求/单位）、批量载荷 canonical 头绑定
 *         解析（§5.6 六要素）、KIN-RESULT-INCOMPLETE 端到端交付、预终结
 *         工况的完成矩阵口径。
 *
 * 设计依据：
 *   - units/kinematics.md §5.4（结局铁律的批量映射）、§5.5（要求值对比
 *     单位）、§5.6（结果绑定六要素）、§7.1（三态素材/NotApplicable 显式
 *     标记/完整性校验）、§10.2（故障注入矩阵的批量侧）
 *   - 黄金几何：KinFkFixture 平面二连杆（真实 IkSolver 路径——可达/
 *     内孔不可达/超界三反例沿用 T04 用例口径）
 *   - 任务契约 tasks/foundation/WP-15-T05.json acceptance 1/2/3
 *
 * 确定性：目标位姿由夹具 referenceFk 独立参考实现产生；载荷解析按
 * encodeBatchPayloadCanonical 布局逐字段读回（实现内同一布局——布局
 * 漂移即用例失败）。
 */

#include "KinFkFixture.hpp"

#include <sdurws/ird/evidence/Evaluator.hpp>
#include <sdurws/ird/kinematics/DiagCodes.hpp>  // kKinResultIncomplete 码值常量（禁拼码）
#include <sdurws/ird/kinematics/Evidence.hpp>
#include <sdurws/ird/kinematics/Evaluators.hpp>
#include <sdurws/ird/kinematics/Ik.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

using namespace sdurws::ird::kinematics::testfixture;
namespace evidence = sdurws::ird::evidence;
namespace kin = sdurws::ird::kinematics;
using kin::BatchAppliesToScope;
using kin::BatchCondition;
using kin::BatchDemandCheck;
using kin::BatchItemStatus;
using kin::BatchQuery;
using kin::BatchRequirementLevel;
using kin::BatchTaskPoint;
using kin::InitialValueStrategy;

namespace {

// =====================================================================
// 小工具（脚手架——非断言面）
// =====================================================================

/// 定长身份字节 → 向量（gtest 比较面——vector↔vector 同型）。
template <typename IdArray>
std::vector<std::uint8_t> toVec(const IdArray& bytes)
{
    return std::vector<std::uint8_t>(bytes.begin(), bytes.end());
}

/// referenceFk 输出的 4×4 齐次矩阵 → rw 位姿（测试换形——IkTest 同款）。
rw::math::Transform3D<double> transformOf(const Mat4& m)
{
    const rw::math::Rotation3D<double> r(m[0][0], m[0][1], m[0][2], m[1][0],
                                         m[1][1], m[1][2], m[2][0], m[2][1],
                                         m[2][2]);
    const rw::math::Vector3D<double> p(m[0][3], m[1][3], m[2][3]);
    return rw::math::Transform3D<double>(p, r);
}

core::ObjectId pointId(const std::string& seed)
{
    return idFrom<core::ObjectId>("kin-bp-" + seed);
}
core::ObjectId caseId(const std::string& seed)
{
    return idFrom<core::ObjectId>("kin-bc-" + seed);
}

BatchTaskPoint makePoint(const std::string& seed,
                         const rw::math::Transform3D<double>& target)
{
    BatchTaskPoint p;
    p.pointOid = pointId(seed);
    p.level = BatchRequirementLevel::Must;
    p.enabled = true;
    p.targetInBase = target;
    p.positionTolerance = 1e-6;
    p.orientationTolerance = 1e-6;
    return p;
}

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

/// 批量查询（真实求解器路径——不注入替身，内置 IkSolver 生效）。
BatchQuery makeQuery(std::vector<BatchTaskPoint> points,
                     std::vector<BatchCondition> conditions,
                     InitialValueStrategy strategy = InitialValueStrategy::ReferenceQ,
                     std::uint32_t initCount = 1U)
{
    BatchQuery q;
    q.points = std::move(points);
    q.conditions = std::move(conditions);
    q.defaultTcp.toolObject = idFrom<core::ObjectId>("kin-tool");
    q.referenceQ = {0.0, 0.0};
    q.initialStrategy = strategy;
    q.initialValuesCount = initCount;
    q.iterationLimit = 200U;
    return q;
}

evidence::EvaluationRequest makeRequest(std::vector<evidence::CaseId> cases)
{
    evidence::EvaluationRequest req;
    req.mode = core::EvaluationMode::Verified;
    req.snapshot.snapshotId.bytes = digestOf("kin-batch-snap");
    req.slice.sliceId.bytes = digestOf("kin-batch-slice");
    req.task.attempt.value = 7U;
    req.caseSubset = std::move(cases);
    return req;
}

/// 可记录进度的最小上下文。
class ProgressContext final : public evidence::IEvaluationContext {
public:
    bool cancellationRequested() const override { return false; }
    void reportProgress(std::uint8_t percent, std::string_view) override
    {
        m_percents.push_back(percent);
    }
    std::optional<std::vector<std::uint8_t>>
    tryObjectBytes(core::ObjectId, core::ContentVersion) const override
    {
        return std::nullopt;
    }
    const std::vector<std::uint8_t>& percents() const { return m_percents; }

private:
    std::vector<std::uint8_t> m_percents;
};

// =====================================================================
// 载荷字节流的顺序读取器（测试侧解码——与产品编码器同布局，布局漂移
// 即断言失败；仅覆盖头部绑定块，逐项表由通用断言覆盖）
// =====================================================================

class ByteReader {
public:
    explicit ByteReader(const std::vector<std::uint8_t>& bytes)
        : m_bytes(bytes)
    {
    }

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
    std::vector<std::uint8_t> raw(std::size_t n)
    {
        std::vector<std::uint8_t> out(m_bytes.begin() + m_pos,
                                      m_bytes.begin() + m_pos + n);
        m_pos += n;
        return out;
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
// ACC1——真实求解器路径的逐项状态映射：可达收敛→CandidateFound（V-05
// 批量侧）、内孔不可达→NoConvergence（V-06 批量侧）、超界→BoundExceeded
// （结局 5 素材的批量通道承载态——偏差登记随卡 §14.6 v0.5）
// =====================================================================

TEST(KinTaskPointsBatch, RealSolverOutcomeMapping_WP15T05_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-03"},
                  std::vector<std::string>{"V-05", "V-06"});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);

    // 可达目标：referenceFk({0.3,-0.5})（独立参考产生——T04 V-05 同源）；
    // 内孔目标 ‖p‖=0.2 < L1−L2=0.4（数值不可达）；超界目标 ‖p‖=10 >
    // Σ连杆长（解析界限静态可判）。
    const rw::math::Transform3D<double> reachable =
        transformOf(referenceFk(model, {0.3, -0.5}).tcp);
    const core::ObjectId reach = pointId("reachable");
    const core::ObjectId hole = pointId("hole");
    const core::ObjectId beyond = pointId("beyond");
    const core::ObjectId c1 = caseId("c");

    // 三个状态各一次独立计算（初值策略为查询级参数：可达面用
    // ReferenceQ、内孔面用 JointGrid×4——T04 反例口径；超界面先于迭代，
    // 策略无影响）。
    const evidence::EvaluationRequest req = makeRequest({c1});

    // ① 可达→CandidateFound＋最佳解在场（稳定排序首位）。
    ProgressContext context1;
    const kin::BatchComputation done = kin::runBatchComputation(
        view, makeQuery({makePoint("reachable", reachable)}, {makeCondition("c")}),
        req, context1);
    ASSERT_EQ(done.items.size(), 1U);
    EXPECT_EQ(done.items.front().status, BatchItemStatus::CandidateFound);
    ASSERT_TRUE(done.items.front().bestSolution.has_value());
    EXPECT_EQ(done.items.front().outcomeKind, kin::IkOutcomeKind::SolutionsFound);

    // ② 内孔→NoConvergence＋搜索未果记录（C5 素材——不得输出不可行）。
    ProgressContext context2;
    const kin::BatchComputation holeRun = kin::runBatchComputation(
        view,
        makeQuery({makePoint("hole", trans(0.2, 0.0, 0.0))}, {makeCondition("c")},
                  InitialValueStrategy::JointGrid, 4U),
        req, context2);
    ASSERT_EQ(holeRun.items.size(), 1U);
    EXPECT_EQ(holeRun.items.front().status, BatchItemStatus::NoConvergence);
    ASSERT_TRUE(holeRun.items.front().searchRecord.has_value());
    EXPECT_EQ(holeRun.items.front().searchRecord->initialGuessesTried, 4U);
    EXPECT_FALSE(holeRun.items.front().bestSolution.has_value());

    // ③ 超界→BoundExceeded＋证明素材（仅素材不裁定；绑定请求身份）。
    ProgressContext context3;
    const kin::BatchComputation boundRun = kin::runBatchComputation(
        view,
        makeQuery({makePoint("beyond", trans(10.0, 0.0, 0.0))}, {makeCondition("c")}),
        req, context3);
    ASSERT_EQ(boundRun.items.size(), 1U);
    EXPECT_EQ(boundRun.items.front().status, BatchItemStatus::BoundExceeded);
    ASSERT_TRUE(boundRun.items.front().proofMaterial.has_value());
    EXPECT_EQ(boundRun.items.front().proofMaterial->proof.snapshotId,
              req.snapshot.snapshotId);
    EXPECT_EQ(boundRun.items.front().proofMaterial->proof.sliceId, req.slice.sliceId);

    // 各计算全批完整（§7.1 完整性校验通过面）。
    EXPECT_FALSE(done.incomplete);
    EXPECT_FALSE(holeRun.incomplete);
    EXPECT_FALSE(boundRun.incomplete);
}

// =====================================================================
// ACC1/ACC3——要求值对比素材（实际/要求/单位）：残差两行恒产出；裕量
// 要求声明时产出并给实际值/单位；碰撞要求在未启用碰撞时 evaluated=false
// （KIN-05 证据缺失口径——绝不解读为满足）
// =====================================================================

TEST(KinTaskPointsBatch, DemandChecksMaterial_WP15T05_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-03", "REQ-04"},
                  std::vector<std::string>{"KIN-05"});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);
    const rw::math::Transform3D<double> reachable =
        transformOf(referenceFk(model, {0.3, -0.5}).tcp);
    const evidence::EvaluationRequest req = makeRequest({caseId("c")});

    BatchTaskPoint point = makePoint("demanded", reachable);
    point.demands.collisionFreeRequired = true;  // 声明碰撞要求（会话未注入）
    point.demands.minimumJointMargin = 0.0;      // 裕量要求值 0（rad|m 依关节）
    BatchQuery q = makeQuery({point}, {makeCondition("c")});

    ProgressContext context;
    const kin::BatchComputation computation =
        kin::runBatchComputation(view, q, req, context);

    ASSERT_EQ(computation.items.size(), 1U);
    const kin::BatchWorkItemRecord& item = computation.items.front();
    ASSERT_EQ(item.status, BatchItemStatus::CandidateFound);
    ASSERT_EQ(item.demandChecks.size(), 4U);  // 残差×2＋碰撞＋裕量

    // 残差两行（位置 m／姿态 rad——实际 ≤ 要求＝满足；要求值＝点容差）。
    EXPECT_EQ(item.demandChecks[0].kind, BatchDemandCheck::Kind::PositionResidual);
    EXPECT_TRUE(item.demandChecks[0].evaluated);
    EXPECT_TRUE(item.demandChecks[0].satisfied);
    EXPECT_EQ(item.demandChecks[0].unit, "m");
    EXPECT_EQ(item.demandChecks[0].required, 1e-6);
    EXPECT_EQ(item.demandChecks[1].kind, BatchDemandCheck::Kind::OrientationResidual);
    EXPECT_TRUE(item.demandChecks[1].evaluated);
    EXPECT_TRUE(item.demandChecks[1].satisfied);
    EXPECT_EQ(item.demandChecks[1].unit, "rad");

    // 碰撞行：要求已声明但会话未注入→evaluated=false（KIN-05——证据
    // 缺失，绝不解读为满足）；collisionNotEvaluated 标记同步。
    EXPECT_EQ(item.demandChecks[2].kind, BatchDemandCheck::Kind::CollisionFree);
    EXPECT_FALSE(item.demandChecks[2].evaluated);
    EXPECT_FALSE(item.demandChecks[2].satisfied);
    EXPECT_TRUE(item.collisionNotEvaluated);

    // 裕量行：实际值（绝对距离，rad|m 依 arg-min 关节）≥ 要求值 0——
    // 单位落在关节天然单位词表内（rad|m）。
    EXPECT_EQ(item.demandChecks[3].kind, BatchDemandCheck::Kind::MinJointMargin);
    EXPECT_TRUE(item.demandChecks[3].evaluated);
    EXPECT_TRUE(item.demandChecks[3].satisfied);
    EXPECT_GE(item.demandChecks[3].actual, item.demandChecks[3].required);
    EXPECT_TRUE(item.demandChecks[3].unit == "rad" || item.demandChecks[3].unit == "m");
}

// =====================================================================
// ACC2/ACC3——不完整批的端到端交付：KIN-RESULT-INCOMPLETE 诊断（§9.6
// 行 15 消费面）＋逐项 NotRun 行（evidence Missing——漏验素材）＋部分
// 素材仍交付（§7.1 partial record）
// =====================================================================

TEST(KinTaskPointsBatch, IncompleteBatchDeliversResultIncompleteDiag_WP15T05_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-03", "EVI-02"},
                  std::vector<std::string>{"TASK-02"});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);
    const rw::math::Transform3D<double> reachable =
        transformOf(referenceFk(model, {0.3, -0.5}).tcp);

    // 两个可达点（真实求解）×单工况；恒真取消上下文→批间查询命中即停
    // ——全部可解项 NotRun，输出仍携带部分素材（NotRun 表＋incomplete
    // 标记，§7.1 partial record；调用侧据此不产 Completed envelope）。
    BatchQuery q = makeQuery({makePoint("p1", reachable), makePoint("p2", reachable)},
                             {makeCondition("c")});

    class AlwaysCancelContext final : public evidence::IEvaluationContext {
    public:
        bool cancellationRequested() const override { return true; }
        void reportProgress(std::uint8_t, std::string_view) override {}
        std::optional<std::vector<std::uint8_t>>
        tryObjectBytes(core::ObjectId, core::ContentVersion) const override
        {
            return std::nullopt;
        }
    };

    kin::TaskPointsBatchEvaluator evaluator(&view, q);
    AlwaysCancelContext context;
    const evidence::EvaluationRequest req = makeRequest({caseId("c")});
    const evidence::EvaluationOutput out = evaluator.evaluate(req, context);

    // KIN-RESULT-INCOMPLETE 诊断在场（码值经注册常量——禁拼码）。
    bool hasIncompleteDiag = false;
    for (const auto& diag : out.diagnostics) {
        if (diag.code == std::string(kin::kKinResultIncomplete)) {
            hasIncompleteDiag = true;
        }
    }
    EXPECT_TRUE(hasIncompleteDiag);

    // 逐项证据行：NotRun→evidence Missing（无产物——漏验素材，不伪造
    // 摘要——presence 纪律的 NotRun 分支）。
    ASSERT_EQ(out.evidence.size(), 2U);
    for (const evidence::EvidenceItem& row : out.evidence) {
        EXPECT_EQ(row.status, evidence::EvidenceItemStatus::Missing);
        EXPECT_FALSE(row.artifactDigest.has_value());
    }

    // 部分素材仍交付（§7.1 partial record——NotRun 表进载荷）。
    ASSERT_TRUE(out.payload.has_value());
    EXPECT_FALSE(out.payload->canonicalBytes.empty());
}

// =====================================================================
// ACC3——批量载荷头绑定解析（§5.6 六要素：magic/codec 版本/绑定身份块/
// 任务五元组/评估键/契约版本——字节面溯源）
// =====================================================================

TEST(KinTaskPointsBatch, PayloadHeaderBindingLayout_WP15T05_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-03", "CON-05"},
                  std::vector<std::string>{"NFR-COR-04"});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);
    const rw::math::Transform3D<double> reachable =
        transformOf(referenceFk(model, {0.3, -0.5}).tcp);

    BatchQuery q = makeQuery({makePoint("p1", reachable)}, {makeCondition("c")});
    kin::TaskPointsBatchEvaluator evaluator(&view, q);
    ProgressContext context;
    const evidence::EvaluationRequest req = makeRequest({caseId("c")});
    const evidence::EvaluationOutput out = evaluator.evaluate(req, context);
    ASSERT_TRUE(out.payload.has_value());
    const std::vector<std::uint8_t>& bytes = out.payload->canonicalBytes;

    // 头部逐字段读回（布局见 Evidence.hpp encodeBatchPayloadCanonical 注
    // ——字段序即读序；布局漂移即此处失败）。
    ByteReader reader(bytes);
    EXPECT_EQ(reader.str(7), "IRDBP01");
    EXPECT_EQ(reader.u32(), 1U);                              // codec 版本
    EXPECT_EQ(reader.u8(), 0U);                               // incomplete=false
    EXPECT_EQ(reader.raw(32), toVec(req.snapshot.snapshotId.bytes));  // snapshotId
    EXPECT_EQ(reader.raw(32), toVec(req.slice.sliceId.bytes));        // sliceId
    reader.raw(32);                                   // configDigest（零值占位）
    EXPECT_EQ(reader.u8(),
              static_cast<std::uint8_t>(core::EvaluationMode::Verified));  // mode
    EXPECT_EQ(reader.u64(), 0U);                              // seed（ReferenceQ 未用）
    EXPECT_EQ(reader.u32(), 2U);                              // referenceQ 维度
    reader.u64();                                             // referenceQ[0]
    reader.u64();                                             // referenceQ[1]
    // 任务五元组（4×16B＋attempt）。
    EXPECT_EQ(reader.raw(16), toVec(req.task.project.bytes));
    EXPECT_EQ(reader.raw(16), toVec(req.task.branch.bytes));
    EXPECT_EQ(reader.raw(16), toVec(req.task.revision.bytes));
    EXPECT_EQ(reader.raw(16), toVec(req.task.run.bytes));
    EXPECT_EQ(reader.u64(), req.task.attempt.value);
    // 评估键＋契约版本（§5.6 绑定六要素尾元）。
    const std::uint32_t keyLen = reader.u32();
    EXPECT_EQ(reader.str(keyLen), std::string(kin::kTaskPointsBatchEvaluationKey));
    EXPECT_EQ(reader.u32(), kin::kTaskPointsBatchContractVersion);
    // caseSubset（1 项——去重后字典序）。
    EXPECT_EQ(reader.u32(), 1U);
    EXPECT_EQ(reader.raw(16), toVec(caseId("c").bytes));
}

// =====================================================================
// ACC2——预终结项的完成矩阵口径：工况 None→全部工作项 NotApplicable
// 显式标记（不静默丢弃，ERR-01），工况级 notApplicable（不计漏验）
// =====================================================================

TEST(KinTaskPointsBatch, ZeroApplicableCaseMarksNotApplicable_WP15T05_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-03", "EVI-02"},
                  std::vector<std::string>{"ERR-01"});

    const rt::CanonicalModel model = twoLinkModel();
    TestView view(model);

    // 工况 None：工作项全部 NotApplicable 显式标记（原因必填——ERR-01）。
    BatchQuery q = makeQuery({makePoint("p1", trans(1.2, 0.0, 0.0))},
                             {makeCondition("cnone", true, BatchAppliesToScope::None)});
    const evidence::EvaluationRequest req = makeRequest({caseId("cnone")});

    ProgressContext context;
    const kin::BatchComputation computation =
        kin::runBatchComputation(view, q, req, context);

    ASSERT_EQ(computation.items.size(), 1U);
    EXPECT_EQ(computation.items.front().status, BatchItemStatus::NotApplicable);
    EXPECT_FALSE(computation.items.front().reason.empty());
    ASSERT_EQ(computation.caseCompletion.size(), 1U);
    EXPECT_TRUE(computation.caseCompletion[0].notApplicable);
    EXPECT_FALSE(computation.caseCompletion[0].executed);
    EXPECT_FALSE(computation.caseCompletion[0].notRun);
    EXPECT_FALSE(computation.incomplete);
}
