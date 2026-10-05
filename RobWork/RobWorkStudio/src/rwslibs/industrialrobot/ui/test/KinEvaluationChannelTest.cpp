/**
 * @file   KinEvaluationChannelTest.cpp
 * @brief  kinematics 覆盖评估执行通道测试（UI-T64——F-490① 上游批的
 *         受理/切片/后台执行/账面/迟到判定具名自证面）。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T64.json acceptance 2/3（结构化结果
 *     回路＋诚实拒绝面）；findings F-490①（上游前置——SampleResultSet
 *     产品生产者的行为面）；
 *   - 被测 TU 同源编入（plugin/KinEvaluationChannel.cpp——HostCompilePort
 *     先例形态，CMake 集成树门控块）；测试替身＝脚本编辑器＋夹具视图
 *     （KinFkFixture 两连杆黄金模型——runRegionCoverageComputation 真实
 *     计算面的确定性消费）。
 *
 * 覆盖面（acceptance 映射）：
 *   - 快照缺位拒绝（acceptance 3——ERR-01 不虚构受理）；
 *   - 非 RegionCoverage kind 拒绝（范围声明面——批量/单点通道留后续）；
 *   - 受理→后台执行→结构化账面投影（acceptance 2——逐样本数/覆盖率
 *     计数与绑定键）；
 *   - 换绑清账＋迟到结果按提交锚丢弃（acceptance 2/4——PA 权威纪律的
 *     呈现侧镜像）；
 *   - 空切片拒绝（工作集无采样计划）。
 */

#include <gtest/gtest.h>

#include <QCoreApplication>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/kinematics/KinematicsPanelChannels.hpp>  // 通道值面
#include <sdurws/ird/kinematics/AnalysisConfig.hpp>           // 合法配置基线
#include <sdurws/ird/requirements/Editor.hpp>                 // IRequirementEditor（替身接口）
#include <sdurws/ird/requirements/RequirementTypes.hpp>       // 工作集值面

#include "KinFkFixture.hpp"                 // 两连杆黄金模型（kinematics 测试夹具——header-only）
#include "plugin/KinEvaluationChannel.hpp"  // 被测执行器（同源编入 TU）

// ---- 使用声明（与被测头同一命名空间——用例可读性）--------------------
using namespace sdurws::ird;
using namespace sdurws::ird::ui;
using namespace sdurws::ird::kinematics;
using namespace sdurws::ird::kinematics::testfixture;
namespace req = sdurws::ird::requirements;

namespace {

// =====================================================================
// 测试替身（视图＋编辑器——执行器依赖面的最小脚本化）
// =====================================================================

/**
 * @brief 夹具模型视图（IKinRuntimeView 最小实现——持有两连杆黄金模型，
 *        T_world_base 恒等；P-KIN-2 宿主注入形态的测试面）。
 */
class FixtureView final : public IKinRuntimeView {
public:
    FixtureView() : m_model(twoLinkModel()) {}
    const runtime::CanonicalModel& model() const override { return m_model; }
    rw::math::Transform3D<double> worldToBase() const override
    {
        return rw::math::Transform3D<double>::identity();  // 地面安装（恒等）
    }

private:
    runtime::CanonicalModel m_model;  // 值持有（构造一次——并发只读）
};

/**
 * @brief 脚本化需求编辑器（IRequirementEditor 最小替身——仅 workingSet()
 *        供数，编辑面在执行器测试零消费：实现如实 fail-fast 不伪造）。
 */
class ScriptedEditor final : public req::IRequirementEditor {
public:
    explicit ScriptedEditor(req::RequirementWorkingSet ws) : m_ws(std::move(ws)) {}
    const req::RequirementWorkingSet& workingSet() const noexcept override
    {
        return m_ws;
    }
    req::RequirementLoadOutcome loadBaseline(
        const req::RequirementObjectClosureView&) override
    {
        return {};  // 替身零供数语义——测试不触达（执行器只读工作集）
    }
    req::EditOutcome applyEdit(const req::RequirementEdit&) override
    {
        return {};  // 同上——零编辑消费
    }
    req::EditOutcome applyEdit(const req::EditBatch&) override { return {}; }
    bool undoLocal() noexcept override { return false; }
    bool redoLocal() noexcept override { return false; }
    std::string buildChangeSummary() const override { return {}; }
    req::RequirementDraftStatus draftStatus() const override { return {}; }

private:
    req::RequirementWorkingSet m_ws;  // 预置工作集（构造注入）
};

// ---- 工作集夹具（一区域一计划——区域覆盖通道的最小切片）--------------

/// 生成"一区域一计划"工作集：区域盒中心 (0.7, 0, 0.3)、三边 0.2 m；
/// Grid 2×1×1（两位置样本）；姿态 1 方向×1 滚转；计划引用该区域。
req::RequirementWorkingSet makeOneRegionOnePlanWorkset()
{
    req::RequirementWorkingSet ws;

    req::WorkRegion region;
    region.objectId = core::ObjectId::tryFromCanonical(
        "obj-11111111111111111111111111111111").value_or(core::ObjectId{});
    region.name = "装配区 A";
    region.enabled = true;
    region.refFrame = req::RequirementReference{};  // World 缺省
    region.box.center = rw::math::Vector3D<double>(0.7, 0.0, 0.3);
    region.box.size = rw::math::Vector3D<double>(0.2, 0.2, 0.2);  // m（非退化）
    region.positionSampling.method = req::PositionSamplingMethod::Grid;
    region.positionSampling.counts = {2U, 1U, 1U};
    region.orientationSampling.directionSamples = 1U;
    region.orientationSampling.rollSamples = 1U;
    ws.regions.entries.push_back(region);

    req::SamplingPlan plan;
    plan.objectId = core::ObjectId::tryFromCanonical(
        "obj-22222222222222222222222222222222").value_or(core::ObjectId{});
    plan.regionRef = region.objectId;
    plan.positionSampling = region.positionSampling;
    plan.orientationSampling = region.orientationSampling;
    ws.plans.entries.push_back(plan);
    return ws;
}

/// 合法求解配置基线（I-KIN-4 seed≥1——与产品装配纪律同则）。
AnalysisConfiguration makeLegalConfig()
{
    AnalysisConfiguration config;
    config.seed = 1;
    config.regionBudget.seed = 1;
    return config;
}

/// 执行器夹具：脚本编辑器＋合法配置＋完成等待设施（QCoreApplication
/// 级——回投经 processEvents 自旋排空；超时＝夹具缺陷 fail-fast）。
struct ExecutorHarness {
    std::shared_ptr<FixtureView> view = std::make_shared<FixtureView>();
    std::shared_ptr<ScriptedEditor> editor =
        std::make_shared<ScriptedEditor>(makeOneRegionOnePlanWorkset());
    std::shared_ptr<KinEvaluationExecutor> executor;
    core::ContentIdentity boundId =
        core::ContentIdentity::tryFromCanonical(
            "cid-3333333333333333333333333333333333333333333333333333333333333333")
            .value_or(core::ContentIdentity{});
    std::vector<KinChannelBackgroundResultNote> notes;  // 投递槽捕获（回投面）

    ExecutorHarness()
    {
        executor = std::make_shared<KinEvaluationExecutor>(
            KinEvaluationExecutor::Deps{
                editor.get(), nullptr,
                QCoreApplication::instance(),  // 回投上下文（测试进程 app——
                                               // drain 的 processEvents 排空）
                [this](const KinChannelBackgroundResultNote& note) {
                    notes.push_back(note);  // 回投槽捕获（延迟判定断言面）
                }});
        executor->attachView(view, boundId);  // 纪元 1→2（初值 1 首绑推进）
        executor->setConfiguration(makeLegalConfig());
    }

    /// 排空回投队列（processEvents 自旋——全部在途任务终态到账）。
    void drain()
    {
        for (int i = 0; i < 500; ++i) {  // 5 s 上限（10 ms × 500——CI 余量）
            QCoreApplication::processEvents();
            bool inflight = notes.size() < expectedNotes();
            if (!inflight) {
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        FAIL() << "回投超时——执行器任务未在预算内完成（夹具缺陷或挂死）";
    }
    /// 期望 note 数（子用例各自覆盖——默认 1 次提交 1 投递）。
    int noteTarget = 1;
    int expectedNotes() const { return noteTarget; }
};

// =====================================================================
// 受理拒绝面（acceptance 3——ERR-01 不虚构受理）
// =====================================================================

/// 快照缺位拒绝：解绑态提交 RegionCoverage——accepted=false 且 reason
/// 如实（"快照缺位"），零 taskRef。
TEST(KinEvaluationChannel, Submit_RejectsWhenSnapshotUnbound_UI_T64)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-07", "ERR-01"},
                  std::vector<std::string>{});
    ExecutorHarness h;
    h.executor->attachView(nullptr, core::ContentIdentity{});  // 解绑

    KinChannelBackgroundRequest request;
    request.kind = KinChannelBackgroundKind::RegionCoverage;
    request.epoch = h.executor->epoch();
    const KinChannelBackgroundAck ack = h.executor->submit(request);
    EXPECT_FALSE(ack.accepted);
    EXPECT_NE(ack.reason.find("快照缺位"), std::string::npos);
    EXPECT_TRUE(ack.taskRef.empty());
    EXPECT_FALSE(h.executor->latestCoverageResult().has_value());
}

/// 非 RegionCoverage kind 拒绝：SessionSolve/TaskPointsBatch——范围声明
/// 面如实拒绝（两通道执行缝未装配——本批范围＝区域覆盖 L-K6）。
TEST(KinEvaluationChannel, Submit_RejectsNonCoverageKinds_UI_T64)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-07", "ERR-01"},
                  std::vector<std::string>{});
    ExecutorHarness h;

    KinChannelBackgroundRequest solve;
    solve.kind = KinChannelBackgroundKind::SessionSolve;
    solve.epoch = h.executor->epoch();
    const KinChannelBackgroundAck solveAck = h.executor->submit(solve);
    EXPECT_FALSE(solveAck.accepted);
    EXPECT_NE(solveAck.reason.find("单点求解"), std::string::npos);

    KinChannelBackgroundRequest batch;
    batch.kind = KinChannelBackgroundKind::TaskPointsBatch;
    batch.epoch = h.executor->epoch();
    const KinChannelBackgroundAck batchAck = h.executor->submit(batch);
    EXPECT_FALSE(batchAck.accepted);
    EXPECT_NE(batchAck.reason.find("批量验证"), std::string::npos);
}

/// 空切片拒绝：工作集无采样计划——受理拒绝（"工作集无采样计划"）。
TEST(KinEvaluationChannel, Submit_RejectsEmptyPlanSlice_UI_T64)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-03", "ERR-01"},
                  std::vector<std::string>{});
    ExecutorHarness h;
    h.editor = std::make_shared<ScriptedEditor>(
        req::RequirementWorkingSet{});  // 空工作集
    h.executor = std::make_shared<KinEvaluationExecutor>(
        KinEvaluationExecutor::Deps{h.editor.get(), nullptr, nullptr, {}});
    h.executor->attachView(h.view, h.boundId);
    h.executor->setConfiguration(makeLegalConfig());

    KinChannelBackgroundRequest request;
    request.kind = KinChannelBackgroundKind::RegionCoverage;
    request.snapshotId = h.boundId;
    request.epoch = h.executor->epoch();
    const KinChannelBackgroundAck ack = h.executor->submit(request);
    EXPECT_FALSE(ack.accepted);
    EXPECT_NE(ack.reason.find("工作集无采样计划"), std::string::npos);
}

// =====================================================================
// 受理→执行→账面（acceptance 2——结构化结果回路）
// =====================================================================

/// 主链：受理（taskRef 非空）→后台执行→回投→账面投影（样本数＝位置
/// 2×姿态 1×1＝2＋双口径计数 planned 对齐＋绑定键＝提交时快照身份）。
TEST(KinEvaluationChannel, Submit_RunsAndRecordsCoverage_UI_T64)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-07", "KIN-04", "L-K6"},
                  std::vector<std::string>{"AT-03"});
    ExecutorHarness h;
    h.noteTarget = 1;

    KinChannelBackgroundRequest request;
    request.kind = KinChannelBackgroundKind::RegionCoverage;
    request.snapshotId = h.boundId;
    request.configDigest = analysisConfigurationDigest(makeLegalConfig());
    request.epoch = h.executor->epoch();
    const KinChannelBackgroundAck ack = h.executor->submit(request);
    ASSERT_TRUE(ack.accepted) << ack.reason;
    EXPECT_FALSE(ack.taskRef.empty());

    h.drain();  // 排空回投（后台执行→processEvents 收割）

    // ---- 结构化账面：ok＋绑定键＋样本面。
    const auto result = h.executor->latestCoverageResult();
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->ok);
    EXPECT_TRUE(result->snapshotId == h.boundId);       // 提交锚（消费核对键）
    EXPECT_EQ(result->epoch, request.epoch);
    // 位置样本 2×1×1＝2；位姿样本＝位置×方向×滚转＝2×1×1＝2——分母面
    // 与域 generateSampleSet 全局序对齐（D-KIN-6）。
    EXPECT_EQ(result->position.planned, 2U);
    EXPECT_EQ(result->orientation.planned, 2U);
    EXPECT_EQ(result->samples.size(), 4U);  // 位置 2＋位姿 2（双口径全样本）
    // 逐样本状态五值合法（位置样本∈{Reached,Unreachable,...}——模型黄金
    // 值不在此断言，覆盖面的结构性由分母对账保证）＋UI-T65 投影面：坐标
    // 与区域锚随账面（消费卡投影的过滤/变换输入）。
    for (const auto& record : result->samples) {
        EXPECT_TRUE(record.state == KinChannelSampleState::Reached
                    || record.state == KinChannelSampleState::Unreachable
                    || record.state == KinChannelSampleState::DataInsufficient
                    || record.state == KinChannelSampleState::NotRun);
        // 坐标非零（两连杆黄金模型的工作区样本不在原点——直投面实证）
        // ＋区域锚对齐工作集条目（逐区域过滤键）。
        EXPECT_FALSE(record.position == rw::math::Vector3D<double>(0, 0, 0));
        EXPECT_TRUE(record.regionObjectId.isValid());
    }
    // 任务区投影：一完成行（taskRef 对齐）。
    const auto rows = h.executor->taskRows();
    ASSERT_EQ(rows.size(), 1U);
    EXPECT_EQ(rows[0].taskRefText, ack.taskRef);
    // 投递槽：一 note（完成态——摘要非空）。
    ASSERT_EQ(h.notes.size(), 1U);
    EXPECT_FALSE(h.notes[0].summaryText.empty());
}

// =====================================================================
// 呈现对照映射（UI-T65——F-495 消费卡的纯值词表翻译：表驱动逐档断言；
// 判定零参与——映射面正确性的直接承载）
// =====================================================================

/// 域样本五值→三维格元四值映射（表驱动——Reached 绿/Unreachable 红/
/// DataInsufficient 黄/NotRun·NotApplicable 灰）。
TEST(KinEvaluationChannel, MapSampleStateToCell_TableDriven_UI_T65)
{
    IRD_TEST_INFO(std::vector<std::string>{"F-495", "UX-11"},
                  std::vector<std::string>{});
    EXPECT_EQ(mapSampleStateToCell(KinChannelSampleState::Reached),
              View3DCellState::Good);
    EXPECT_EQ(mapSampleStateToCell(KinChannelSampleState::Unreachable),
              View3DCellState::Failed);
    EXPECT_EQ(mapSampleStateToCell(KinChannelSampleState::DataInsufficient),
              View3DCellState::Weak);
    EXPECT_EQ(mapSampleStateToCell(KinChannelSampleState::NotRun),
              View3DCellState::NotSampled);
    EXPECT_EQ(mapSampleStateToCell(KinChannelSampleState::NotApplicable),
              View3DCellState::NotSampled);
}

/// 覆盖率框色三档对照（表驱动——比率×目标下限；零分母/无目标＝None
/// 不虚构档位——ERR-01 同源诚实面）。
TEST(KinEvaluationChannel, View3DTintFromCoverage_TableDriven_UI_T65)
{
    IRD_TEST_INFO(std::vector<std::string>{"F-495", "REQ-03"},
                  std::vector<std::string>{});
    // 达标档：3/4＝0.75 ≥ 目标 0.7。
    EXPECT_EQ(view3DTintFromCoverage(3, 4, 0.7), View3DTint::Good);
    // 未达档：3/4＝0.75 < 目标 0.8。
    EXPECT_EQ(view3DTintFromCoverage(3, 4, 0.8), View3DTint::Weak);
    // 零达标：0/4＝0 < 任意正目标——Weak（有样本未达——非 Failed 档：
    // Failed 留给"零达标"强信号呈现由消费侧裁定，本映射两档保守）。
    EXPECT_EQ(view3DTintFromCoverage(0, 4, 0.8), View3DTint::Weak);
    // 零分母（零样本区域）＝None；无目标＝None。
    EXPECT_EQ(view3DTintFromCoverage(0, 0, 0.8), View3DTint::None);
    EXPECT_EQ(view3DTintFromCoverage(3, 4, std::optional<double>{}),
              View3DTint::None);
}

/// 换绑清账＋迟到丢弃：提交后立即换绑（纪元推进＋账面清空）——回投
/// 结果按提交锚丢弃（账面仍空）；任务行终态在案（诚实中止/完成面）。
TEST(KinEvaluationChannel, Rebind_ClearsAccountAndDropsLateResult_UI_T64)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-07", "L-K12"},
                  std::vector<std::string>{});
    ExecutorHarness h;
    h.noteTarget = 1;

    KinChannelBackgroundRequest request;
    request.kind = KinChannelBackgroundKind::RegionCoverage;
    request.snapshotId = h.boundId;
    request.epoch = h.executor->epoch();
    const KinChannelBackgroundAck ack = h.executor->submit(request);
    ASSERT_TRUE(ack.accepted) << ack.reason;

    // 提交后立即换绑（模拟发布拍竞态——纪元推进＋账面清空）。
    h.executor->attachView(h.view, h.boundId);
    EXPECT_FALSE(h.executor->latestCoverageResult().has_value());  // 换绑清账

    h.drain();
    // 迟到结果不入账（账面仍空——提交锚不追新会话）；任务终态在案。
    EXPECT_FALSE(h.executor->latestCoverageResult().has_value());
    const auto rows = h.executor->taskRows();
    ASSERT_EQ(rows.size(), 1U);
    EXPECT_EQ(rows[0].taskRefText, ack.taskRef);
    EXPECT_FALSE(rows[0].stateLabelKey.empty());
}

}  // namespace
