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

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <iostream>
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
 *
 * KIN-P1（A01 回归）扩展：支持注入任意 T_world_base（审查条目 A01 的
 * 世界→基座转换回归需要平移/倒装/侧装三种基座安放——基座安放真值在
 * runtime 权威面，测试替身按 runtime 语义原样返回该值：接口名
 * worldToBase() 的**值语义是 T_world_base**（Snapshot.hpp §6.4 行），
 * 替身不做任何二次换算）。
 */
class FixtureView final : public IKinRuntimeView {
public:
    /// 缺省＝地面安装（T_world_base 恒等——既有用例语义不变）。
    FixtureView()
        : FixtureView(rw::math::Transform3D<double>::identity())
    {
    }
    /// 指定基座安放（审查 A01 回归——平移/倒装/侧装三预设由用例注入）。
    explicit FixtureView(rw::math::Transform3D<double> tWorldBase)
        : m_model(twoLinkModel())
        , m_tWorldBase(tWorldBase)
    {
    }
    /**
     * @brief 为姿态消费回归设置真实工具安装旋转，关节链仍为解析二连杆。
     * @param tWorldBase [in] 基座在世界系的位姿（m、正交旋转）。
     * @param tcpRotation [in] 法兰到TCP的旋转（无量纲）；TCP平移仍为0.7 m。
     * 测试持有模型值，后台仅只读；builder非法输入抛出，不能伪造求解结果。
     */
    FixtureView(rw::math::Transform3D<double> tWorldBase,
                rw::math::Rotation3D<double> tcpRotation)
        : m_model(makeModel(twoLinkModel().chain().joints,
                            rw::math::Transform3D<double>(
                                rw::math::Vector3D<double>(0.7, 0, 0), tcpRotation)))
        , m_tWorldBase(tWorldBase)
    {
    }
    const runtime::CanonicalModel& model() const override { return m_model; }
    rw::math::Transform3D<double> worldToBase() const override
    {
        return m_tWorldBase;  // 值语义＝T_world_base（runtime §6.4 接口注）
    }

private:
    runtime::CanonicalModel m_model;              // 值持有（构造一次——并发只读）
    rw::math::Transform3D<double> m_tWorldBase;   // 基座安放真值（世界系→基座系读法 core §4.6）
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

    /// ★ KIN-P4：以注入求解器重建执行器（换绑取消用例——慢求解替身驱
    /// 动确定性取消窗口；其余 deps 与产品装配同形）。重建＝旧实例析构
    /// （在途任务随闭包存活到终态——harness 单任务序，无残留竞态）。
    void rebuildExecutorWithSolver(const kinematics::IIkSolver* solver)
    {
        executor = std::make_shared<KinEvaluationExecutor>(
            KinEvaluationExecutor::Deps{editor.get(), nullptr,
                                        QCoreApplication::instance(),
                                        [this](const KinChannelBackgroundResultNote& note) {
                                            notes.push_back(note);
                                        },
                                        nullptr, solver});
        executor->attachView(view, boundId);
        executor->setConfiguration(makeLegalConfig());
    }

    /// ★ KIN-P4：以注入编辑器重建执行器（批量切片用例——任务点工作集
    /// 供数；同上重建语义）。
    void rebuildExecutorWithEditor(req::IRequirementEditor* customEditor)
    {
        executor = std::make_shared<KinEvaluationExecutor>(
            KinEvaluationExecutor::Deps{customEditor, nullptr,
                                        QCoreApplication::instance(),
                                        [this](const KinChannelBackgroundResultNote& note) {
                                            notes.push_back(note);
                                        }});
        executor->attachView(view, boundId);
        executor->setConfiguration(makeLegalConfig());
    }
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

/// 非 RegionCoverage kind 的诚实拒绝面（★ KIN-P4 改造——审查 §4 第二条
/// "对 SessionSolve 与 TaskPointsBatch 的拒绝改为真实分发"后，两通道已
/// 产品执行；本用例保留 ERR-01 语义面，断言对象翻转为新拒绝面）：
///   - SessionSolve 缺目标位姿载荷→拒绝（"缺目标"——后台通道无法还原
///     "求解什么"）；
///   - TaskPointsBatch 工作集无任务点→拒绝（"工作集无任务点"）。
TEST(KinEvaluationChannel, Submit_RejectsIncompleteBackgroundRequests_KIN_P4)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-07", "ERR-01"},
                  std::vector<std::string>{});
    ExecutorHarness h;

    KinChannelBackgroundRequest solve;
    solve.kind = KinChannelBackgroundKind::SessionSolve;
    solve.snapshotId = h.boundId;  // 快照一致（载荷完整性检查在其后——本
                                   // 用例隔离"缺目标"这一拒绝面）
    solve.epoch = h.executor->epoch();
    // solve.solveTargetInBase 缺省 nullopt——载荷缺目标。
    const KinChannelBackgroundAck solveAck = h.executor->submit(solve);
    EXPECT_FALSE(solveAck.accepted);
    EXPECT_NE(solveAck.reason.find("缺目标"), std::string::npos)
        << solveAck.reason;

    KinChannelBackgroundRequest batch;
    batch.kind = KinChannelBackgroundKind::TaskPointsBatch;
    batch.snapshotId = h.boundId;
    batch.epoch = h.executor->epoch();
    // 工作集无任务点（一区域一计划夹具——points 空）。
    const KinChannelBackgroundAck batchAck = h.executor->submit(batch);
    EXPECT_FALSE(batchAck.accepted);
    EXPECT_NE(batchAck.reason.find("工作集无任务点"), std::string::npos)
        << batchAck.reason;
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

/// 双口径发散（UI-T65 返工——契约 acceptance 3 位置口径的守护用例）：
/// 同区域 4 样本＝位置 2（全 Reached）＋位姿 2（全 Unreachable）——
/// 位置口径 2/2＝1.0 ≥ 目标 0.7（Good），而全样本混计 2/4＝0.5 < 0.7
/// （Weak）。断言框色跟随**位置口径**——若实现退化为混计（acc/
/// ui-t65/1 阻断 A 的缺陷形态：计数不分 SampleKind），本用例红。
TEST(KinEvaluationChannel, RegionTint_FollowsPositionAxisNotMixed_UI_T65)
{
    IRD_TEST_INFO(std::vector<std::string>{"F-495", "KIN-04", "REQ-03"},
                  std::vector<std::string>{});
    // 区域锚（两区域——发散场景外加他区域过滤面）。
    const core::ObjectId regionA = core::ObjectId::tryFromCanonical(
        "obj-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa").value_or(core::ObjectId{});
    const core::ObjectId regionB = core::ObjectId::tryFromCanonical(
        "obj-bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb").value_or(core::ObjectId{});
    ASSERT_TRUE(regionA.isValid());
    ASSERT_TRUE(regionB.isValid());

    // 样本账面构造（坐标值与框色无关——计数只看区域锚/kind/状态）。
    std::vector<KinChannelSampleRecord> samples;
    // 区域 A：位置 2 全达标（位置口径分子 2）。
    for (std::uint64_t i = 0; i < 2; ++i) {
        KinChannelSampleRecord rec;
        rec.sampleIndex = i;
        rec.state = KinChannelSampleState::Reached;
        rec.position = rw::math::Vector3D<double>(0.1 * (i + 1), 0.0, 0.3);
        rec.regionObjectId = regionA;
        rec.kind = KinChannelSampleKind::Position;
        samples.push_back(rec);
    }
    // 区域 A：位姿 2 全不可达（混计口径的"拖低项"——不入位置轴）。
    for (std::uint64_t i = 0; i < 2; ++i) {
        KinChannelSampleRecord rec;
        rec.sampleIndex = 2 + i;
        rec.state = KinChannelSampleState::Unreachable;
        rec.position = rw::math::Vector3D<double>(0.1 * (i + 1), 0.0, 0.3);
        rec.regionObjectId = regionA;
        rec.kind = KinChannelSampleKind::Pose;
        samples.push_back(rec);
    }
    // 区域 B：位置 1 未达标（他区域样本——不得漏入 A 的计数）。
    KinChannelSampleRecord other;
    other.sampleIndex = 4;
    other.state = KinChannelSampleState::Reached;
    other.position = rw::math::Vector3D<double>(0.5, 0.0, 0.3);
    other.regionObjectId = regionB;
    other.kind = KinChannelSampleKind::Position;
    samples.push_back(other);

    // 发散断言本体：位置口径 2/2＝1.0 ≥ 0.7 → Good（混计 2/4＝0.5 < 0.7
    // 会得 Weak——两种口径在本场景分档不同，用例判别力成立）。
    EXPECT_EQ(view3DRegionTintFromSamples(samples, regionA, 0.7),
              View3DTint::Good);
    // 计数半区逐值钉扎（tally 直查——分母/分子恰为位置轴口径）。
    const RegionPositionTally tally =
        tallyRegionPositionCoverage(samples, regionA);
    EXPECT_EQ(tally.planned, 2U);  // 位置分母不含位姿 2 与他区域 1
    EXPECT_EQ(tally.reached, 2U);  // 位置分子＝Position∧Reached
    // 他区域过滤：B 自身 1/1 达标；对 B 的查询不受 A 的 4 样本干扰。
    EXPECT_EQ(view3DRegionTintFromSamples(samples, regionB, 0.7),
              View3DTint::Good);
    // 无目标＝None（不虚构档位）；目标高于比率＝Weak（1.0 < 1.1）。
    EXPECT_EQ(view3DRegionTintFromSamples(samples, regionA,
                                          std::optional<double>{}),
              View3DTint::None);
    EXPECT_EQ(view3DRegionTintFromSamples(samples, regionA, 1.1),
              View3DTint::Weak);
    // 零位置分母区域（只有位姿样本）：位置口径无定义→None（而非用
    // 位姿样本顶替分母——混计口径在本场景会虚产 0/2→Weak）。
    std::vector<KinChannelSampleRecord> poseOnly;
    KinChannelSampleRecord poseRec;
    poseRec.sampleIndex = 0;
    poseRec.state = KinChannelSampleState::Unreachable;
    poseRec.regionObjectId = regionA;
    poseRec.kind = KinChannelSampleKind::Pose;
    poseOnly.push_back(poseRec);
    EXPECT_EQ(view3DRegionTintFromSamples(poseOnly, regionA, 0.7),
              View3DTint::None);
}

// =====================================================================
// 世界→基座坐标转换（KIN-P1——审查条目 A01 回归：宿主把 T_world_base
// 误当 T_base_world 用，世界采样点转入基座系少取一次逆）
// =====================================================================

// ---- A01 回归夹具：可参数化工作集＋自定义视图的执行器组装 --------------

/// 生成"一区域一计划"工作集（区域盒中心/尺寸/位置网格计数可参数化——
/// A01 各安放用例以不同盒形/网格数复用同一构造；区域参考系缺省 World）。
req::RequirementWorkingSet makeOneRegionOnePlanWorkset(
    const rw::math::Vector3D<double>& center,
    const rw::math::Vector3D<double>& size,
    const std::array<std::uint32_t, 3>& gridCounts)
{
    req::RequirementWorkingSet ws;

    req::WorkRegion region;
    region.objectId = core::ObjectId::tryFromCanonical(
        "obj-11111111111111111111111111111111").value_or(core::ObjectId{});
    region.name = "装配区 A";
    region.enabled = true;
    region.refFrame = req::RequirementReference{};  // World 缺省
    region.box.center = center;                     // 世界系坐标（m）
    region.box.size = size;                         // 三边长（m；非退化）
    region.positionSampling.method = req::PositionSamplingMethod::Grid;
    region.positionSampling.counts = gridCounts;
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

/// 位置样本提取（kind==Position 的样本坐标序列——位姿样本与位置样本
/// 共享同一位置轴坐标，按 kind 过滤避免重复计数）。
std::vector<rw::math::Vector3D<double>> positionKindSamples(
    const KinChannelCoverageResult& result)
{
    std::vector<rw::math::Vector3D<double>> out;
    for (const auto& record : result.samples) {
        if (record.kind == KinChannelSampleKind::Position) {
            out.push_back(record.position);  // 基座系坐标（通道直投）
        }
    }
    return out;
}

/// 闭合世界点断言：p_world'＝T_world_base·p_base 逐分量复核（A01 验收
/// "世界/基座转换闭环"的执行器半区——评估面拿到的基座系样本必须能经
/// runtime 读取点直乘回到原世界点）。
void expectWorldRoundTrip(const rw::math::Transform3D<double>& tWorldBase,
                          const rw::math::Vector3D<double>& pBase,
                          const rw::math::Vector3D<double>& pWorldExpected,
                          double tol)
{
    const rw::math::Vector3D<double> back = tWorldBase * pBase;
    EXPECT_NEAR(back[0], pWorldExpected[0], tol);
    EXPECT_NEAR(back[1], pWorldExpected[1], tol);
    EXPECT_NEAR(back[2], pWorldExpected[2], tol);
}

/// 平移安放（Ground＋基座世界位 (1,0,0)）下的世界→基座反例（审查 A01
/// 原始复现数据）：世界区域中心 (0.65,0,0.3)——基座坐标必须为
/// (−0.35,0,0.3)（0.65−1），修复前宿主把 T_world_base 当 T_base_world
/// 相乘得 (+1.65,0,0.3)，区域评估位置整体错位。
TEST(KinEvaluationChannel, ProjectPlan_WorldToBaseTranslation_KIN_P1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-04", "KIN-07", "MDL-22"},
                  std::vector<std::string>{"AT-37"});
    // 基座安放：Ground 旋转（恒等）＋基座位于世界 (1,0,0)——T_world_base。
    auto view = std::make_shared<FixtureView>(trans(1.0, 0.0, 0.0));
    ScriptedEditor editor(makeOneRegionOnePlanWorkset(
        rw::math::Vector3D<double>(0.65, 0.0, 0.3),
        rw::math::Vector3D<double>(0.2, 0.2, 0.2),
        {1U, 1U, 1U}));
    std::vector<KinChannelBackgroundResultNote> notes;
    KinEvaluationExecutor executor(KinEvaluationExecutor::Deps{
        &editor, nullptr, QCoreApplication::instance(),
        [&notes](const KinChannelBackgroundResultNote& note) {
            notes.push_back(note);
        }});
    const core::ContentIdentity boundId =
        core::ContentIdentity::tryFromCanonical(
            "cid-3333333333333333333333333333333333333333333333333333333333333333")
            .value_or(core::ContentIdentity{});
    executor.attachView(view, boundId);
    executor.setConfiguration(makeLegalConfig());

    KinChannelBackgroundRequest request;
    request.kind = KinChannelBackgroundKind::RegionCoverage;
    request.snapshotId = boundId;
    request.epoch = executor.epoch();
    const KinChannelBackgroundAck ack = executor.submit(request);
    ASSERT_TRUE(ack.accepted) << ack.reason;

    // 排空回投（与 ExecutorHarness::drain 同口径——processEvents 自旋）。
    for (int i = 0; i < 500 && notes.empty(); ++i) {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_FALSE(notes.empty()) << "回投超时——执行器任务未完成";

    const auto result = executor.latestCoverageResult();
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->ok) << result->errorText;
    // Grid 1×1×1＝体心单位置样本（域 generateGridPositions 体心规则）。
    const std::vector<rw::math::Vector3D<double>> positions =
        positionKindSamples(*result);
    ASSERT_EQ(positions.size(), 1U);
    // 基座系期望：(0.65,0,0.3)−(1,0,0)＝(−0.35,0,0.3)——非 (1.65,0,0.3)。
    EXPECT_NEAR(positions[0][0], -0.35, 1e-12);
    EXPECT_NEAR(positions[0][1], 0.0, 1e-12);
    EXPECT_NEAR(positions[0][2], 0.3, 1e-12);
    // 闭环：直乘 T_world_base 回到世界点 (0.65,0,0.3)。
    expectWorldRoundTrip(view->worldToBase(), positions[0],
                         rw::math::Vector3D<double>(0.65, 0.0, 0.3), 1e-12);
}

/// 倒装安放（Inverted 预设 R_x(π)，基座世界位 (0,0,2)——runtime §6.5②
/// 手算例同款安放）下的世界→基座转换：世界区域中心 (0,0,0.3) 的基座
/// 坐标＝R_x(π)·((0,0,0.3)−(0,0,2))＝(0,0,1.7)。修复前宿主代码把
/// T_world_base 的旋转直接乘进盒参考系，旋转检测把倒装安放下的一切
/// 区域（含 World 参考系）全部误拒——本用例同时钉住"倒装安放可评估"
/// 与"坐标精确"两个半区。
TEST(KinEvaluationChannel, ProjectPlan_InvertedBasePreset_KIN_P1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-04", "KIN-07", "MDL-22"},
                  std::vector<std::string>{"AT-37"});
    // T_world_base＝(平移 (0,0,2)，R_x(π)＝diag(1,−1,−1))（P-RT-4 冻结
    // 预设矩阵——9 参直构与 runtime installationPresetRotation 逐元素
    // 一致，测试自持不依赖 runtime 符号）。
    auto view = std::make_shared<FixtureView>(rw::math::Transform3D<double>(
        rw::math::Vector3D<double>(0.0, 0.0, 2.0),
        rw::math::Rotation3D<double>(1.0, 0.0, 0.0,
                                     0.0, -1.0, 0.0,
                                     0.0, 0.0, -1.0)));
    ScriptedEditor editor(makeOneRegionOnePlanWorkset(
        rw::math::Vector3D<double>(0.0, 0.0, 0.3),
        rw::math::Vector3D<double>(0.2, 0.2, 0.2),
        {1U, 1U, 1U}));
    std::vector<KinChannelBackgroundResultNote> notes;
    KinEvaluationExecutor executor(KinEvaluationExecutor::Deps{
        &editor, nullptr, QCoreApplication::instance(),
        [&notes](const KinChannelBackgroundResultNote& note) {
            notes.push_back(note);
        }});
    const core::ContentIdentity boundId =
        core::ContentIdentity::tryFromCanonical(
            "cid-3333333333333333333333333333333333333333333333333333333333333333")
            .value_or(core::ContentIdentity{});
    executor.attachView(view, boundId);
    executor.setConfiguration(makeLegalConfig());

    KinChannelBackgroundRequest request;
    request.kind = KinChannelBackgroundKind::RegionCoverage;
    request.snapshotId = boundId;
    request.epoch = executor.epoch();
    const KinChannelBackgroundAck ack = executor.submit(request);
    ASSERT_TRUE(ack.accepted) << ack.reason;

    for (int i = 0; i < 500 && notes.empty(); ++i) {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_FALSE(notes.empty()) << "回投超时——执行器任务未完成";

    const auto result = executor.latestCoverageResult();
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->ok) << result->errorText;
    const std::vector<rw::math::Vector3D<double>> positions =
        positionKindSamples(*result);
    ASSERT_EQ(positions.size(), 1U);
    // 基座系期望：R⁻¹(p_w−t)＝R_x(π)·(0,0,−1.7)＝(0,0,+1.7)。
    EXPECT_NEAR(positions[0][0], 0.0, 1e-12);
    EXPECT_NEAR(positions[0][1], 0.0, 1e-12);
    EXPECT_NEAR(positions[0][2], 1.7, 1e-12);
    // 闭环：T_world_base·(0,0,1.7)＝(0,0,2)+(0,0,−1.7)＝(0,0,0.3)。
    expectWorldRoundTrip(view->worldToBase(), positions[0],
                         rw::math::Vector3D<double>(0.0, 0.0, 0.3), 1e-12);
}

/// 侧装安放（Wall 预设 R_y(π/2)，基座世界位 (1,0,0)）＋非立方盒
/// Grid 2×1×1：钉住轴交换半区——参考系盒经轴交换旋转后**仍然轴对齐**
/// （预设矩阵为符号置换阵），尺寸随轴置换、中心点精确变换。世界盒 z 边
/// 0.6 m 转为基座盒 x 边，域体心采样两格落基座 x＝−0.3±0.15——尺寸
/// 置换与中心变换同时可观测。
TEST(KinEvaluationChannel, ProjectPlan_WallBasePreset_KIN_P1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-04", "KIN-07", "MDL-22"},
                  std::vector<std::string>{"AT-37"});
    // T_world_base＝(平移 (1,0,0)，R_y(π/2)＝[[0,0,1],[0,1,0],[−1,0,0]])
    // （P-RT-4 冻结预设矩阵——9 参直构，行序同 runtime 卡 §6.2 表）。
    auto view = std::make_shared<FixtureView>(rw::math::Transform3D<double>(
        rw::math::Vector3D<double>(1.0, 0.0, 0.0),
        rw::math::Rotation3D<double>(0.0, 0.0, 1.0,
                                     0.0, 1.0, 0.0,
                                     -1.0, 0.0, 0.0)));
    // 非立方盒：x 边 0.2（两格）、y 边 0.4、z 边 0.6——尺寸置换可观测。
    ScriptedEditor editor(makeOneRegionOnePlanWorkset(
        rw::math::Vector3D<double>(0.65, 0.0, 0.3),
        rw::math::Vector3D<double>(0.2, 0.4, 0.6),
        {2U, 1U, 1U}));
    std::vector<KinChannelBackgroundResultNote> notes;
    KinEvaluationExecutor executor(KinEvaluationExecutor::Deps{
        &editor, nullptr, QCoreApplication::instance(),
        [&notes](const KinChannelBackgroundResultNote& note) {
            notes.push_back(note);
        }});
    const core::ContentIdentity boundId =
        core::ContentIdentity::tryFromCanonical(
            "cid-3333333333333333333333333333333333333333333333333333333333333333")
            .value_or(core::ContentIdentity{});
    executor.attachView(view, boundId);
    executor.setConfiguration(makeLegalConfig());

    KinChannelBackgroundRequest request;
    request.kind = KinChannelBackgroundKind::RegionCoverage;
    request.snapshotId = boundId;
    request.epoch = executor.epoch();
    const KinChannelBackgroundAck ack = executor.submit(request);
    ASSERT_TRUE(ack.accepted) << ack.reason;

    for (int i = 0; i < 500 && notes.empty(); ++i) {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_FALSE(notes.empty()) << "回投超时——执行器任务未完成";

    const auto result = executor.latestCoverageResult();
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->ok) << result->errorText;
    const std::vector<rw::math::Vector3D<double>> positions =
        positionKindSamples(*result);
    ASSERT_EQ(positions.size(), 2U);
    // 投影盒期望（基座系）：中心＝R_y(−π/2)·((0.65,0,0.3)−(1,0,0))
    // ＝(−0.3,0,−0.35)；尺寸＝(size_z,size_y,size_x)＝(0.6,0.4,0.2)
    // （轴交换置换——世界盒 z 边 0.6 转为基座盒 x 边）。域 Grid 体心
    // 采样在**投影后的基座盒**内沿基座 x 取两格：x＝−0.3±0.15。
    EXPECT_NEAR(positions[0][0], -0.45, 1e-9);
    EXPECT_NEAR(positions[0][1], 0.0, 1e-9);
    EXPECT_NEAR(positions[0][2], -0.35, 1e-9);
    EXPECT_NEAR(positions[1][0], -0.15, 1e-9);
    EXPECT_NEAR(positions[1][1], 0.0, 1e-9);
    EXPECT_NEAR(positions[1][2], -0.35, 1e-9);
    // 闭环：两样本直乘 T_world_base 回到世界点——
    //   (−0.45,0,−0.35) → (−0.35,0,0.45)+(1,0,0)＝(0.65,0,0.45)；
    //   (−0.15,0,−0.35) → (−0.35,0,0.15)+(1,0,0)＝(0.65,0,0.15)。
    expectWorldRoundTrip(view->worldToBase(), positions[0],
                         rw::math::Vector3D<double>(0.65, 0.0, 0.45), 1e-9);
    expectWorldRoundTrip(view->worldToBase(), positions[1],
                         rw::math::Vector3D<double>(0.65, 0.0, 0.15), 1e-9);
}

/// 轴交换旋转的模型参考系（ModelFrame 旋转 R_z(π/2)）精确支持：轴交换
/// （符号置换）旋转下盒仍轴对齐——解析面按 runtime 反解设施求
/// T_base_ref 后中心/尺寸精确置换，不拒绝（runtime 设施对任意正交旋转
/// 均可反解，拒绝面只保留给"盒不可轴对齐表示"的任意角旋转）。
TEST(KinEvaluationChannel, ProjectPlan_ModelFrameAxisPermutingRotation_KIN_P1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-04", "KIN-07"},
                  std::vector<std::string>{});
    auto view = std::make_shared<FixtureView>();  // 地面安装（恒等）
    ScriptedEditor editor(makeOneRegionOnePlanWorkset(
        rw::math::Vector3D<double>(0.65, 0.0, 0.3),
        rw::math::Vector3D<double>(0.2, 0.2, 0.2),
        {1U, 1U, 1U}));
    // 区域参考系改 ModelFrame（载荷 oid 合法——结构自洽面）。
    req::RequirementWorkingSet ws = editor.workingSet();
    const core::ObjectId frameOid = core::ObjectId::tryFromCanonical(
        "obj-44444444444444444444444444444444").value_or(core::ObjectId{});
    ASSERT_TRUE(frameOid.isValid());
    ws.regions.entries[0].refFrame.kind = req::RequirementRefKind::ModelFrame;
    ws.regions.entries[0].refFrame.objectId = frameOid;
    ScriptedEditor frameEditor(ws);
    // 解析缝：该模型帧的世界位姿＝绕 Z 轴 +90°（轴交换旋转）。
    std::vector<KinChannelBackgroundResultNote> notes;
    KinEvaluationExecutor::Deps deps;
    deps.editor = &frameEditor;
    deps.resolveFrame =
        [](const core::ObjectId&) {
            return std::optional<rw::math::Transform3D<double>>(
                rw::math::Transform3D<double>(
                    rw::math::Vector3D<double>(0.0, 0.0, 0.0),
                    rw::math::Rotation3D<double>(0.0, -1.0, 0.0,
                                                 1.0, 0.0, 0.0,
                                                 0.0, 0.0, 1.0)));
        };
    deps.routeContext = QCoreApplication::instance();
    deps.resultRoute =
        [&notes](const KinChannelBackgroundResultNote& note) {
            notes.push_back(note);
        };
    const core::ContentIdentity boundId =
        core::ContentIdentity::tryFromCanonical(
            "cid-3333333333333333333333333333333333333333333333333333333333333333")
            .value_or(core::ContentIdentity{});
    KinEvaluationExecutor executor(std::move(deps));
    executor.attachView(view, boundId);
    executor.setConfiguration(makeLegalConfig());

    KinChannelBackgroundRequest request;
    request.kind = KinChannelBackgroundKind::RegionCoverage;
    request.snapshotId = boundId;
    request.epoch = executor.epoch();
    const KinChannelBackgroundAck ack = executor.submit(request);
    ASSERT_TRUE(ack.accepted) << ack.reason;

    for (int i = 0; i < 500 && notes.empty(); ++i) {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_FALSE(notes.empty()) << "回投超时——执行器任务未完成";

    const auto result = executor.latestCoverageResult();
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->ok) << result->errorText;
    const std::vector<rw::math::Vector3D<double>> positions =
        positionKindSamples(*result);
    ASSERT_EQ(positions.size(), 1U);
    // 基座系期望：R_z(π/2)·(0.65,0,0.3)＝(0,0.65,0.3)——x/y 轴交换精确
    // 置换（中心点经 runtime 反解设施变换，无保守放大）。
    EXPECT_NEAR(positions[0][0], 0.0, 1e-12);
    EXPECT_NEAR(positions[0][1], 0.65, 1e-12);
    EXPECT_NEAR(positions[0][2], 0.3, 1e-12);
}

/// 任意角旋转参考系保持显式拒绝（R1 轴对齐盒约束的诚实拒绝面——KIN-P1
/// 只把"轴交换/轴翻转"形态纳入精确支持；任意角（如 R_z(π/6)）下盒变换
/// 后不再轴对齐，保守包络会静默改变覆盖率语义，维持拒绝并如实告知）。
TEST(KinEvaluationChannel, ProjectPlan_ArbitraryRotatedFrameStillRejected_KIN_P1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-04", "KIN-07"},
                  std::vector<std::string>{});
    auto view = std::make_shared<FixtureView>();  // 地面安装（恒等）
    ScriptedEditor editor(makeOneRegionOnePlanWorkset(
        rw::math::Vector3D<double>(0.65, 0.0, 0.3),
        rw::math::Vector3D<double>(0.2, 0.2, 0.2),
        {1U, 1U, 1U}));
    // 区域参考系改 ModelFrame，解析缝给任意角旋转 R_z(π/6)（非符号置换
    // ——盒变换后呈 30° 斜置，RegionBox 无法精确表示）。
    req::RequirementWorkingSet ws = editor.workingSet();
    const core::ObjectId frameOid = core::ObjectId::tryFromCanonical(
        "obj-44444444444444444444444444444444").value_or(core::ObjectId{});
    ASSERT_TRUE(frameOid.isValid());
    ws.regions.entries[0].refFrame.kind = req::RequirementRefKind::ModelFrame;
    ws.regions.entries[0].refFrame.objectId = frameOid;
    ScriptedEditor frameEditor(ws);
    constexpr double kCos30 = 0.86602540378443865;  // cos(π/6)（任意角代表值）
    constexpr double kSin30 = 0.5;                  // sin(π/6)
    KinEvaluationExecutor executor(KinEvaluationExecutor::Deps{
        &frameEditor,
        [](const core::ObjectId&) {
            return std::optional<rw::math::Transform3D<double>>(
                rw::math::Transform3D<double>(
                    rw::math::Vector3D<double>(0.0, 0.0, 0.0),
                    rw::math::Rotation3D<double>(
                        kCos30, -kSin30, 0.0,
                        kSin30, kCos30, 0.0,
                        0.0, 0.0, 1.0)));
        },
        QCoreApplication::instance(), {}});
    // 先绑定快照（拒绝必须来自切片的旋转判定而非"快照缺位"——拒绝原因
    // 归属精确是本用例断言面的一半）。
    const core::ContentIdentity boundId =
        core::ContentIdentity::tryFromCanonical(
            "cid-3333333333333333333333333333333333333333333333333333333333333333")
            .value_or(core::ContentIdentity{});
    executor.attachView(view, boundId);
    executor.setConfiguration(makeLegalConfig());

    KinChannelBackgroundRequest request;
    request.kind = KinChannelBackgroundKind::RegionCoverage;
    request.snapshotId = boundId;
    request.epoch = executor.epoch();
    const KinChannelBackgroundAck ack = executor.submit(request);
    // 受理拒绝（切片失败——旋转参考系如实拒绝，不静默扩权）。
    EXPECT_FALSE(ack.accepted);
    EXPECT_NE(ack.reason.find("旋转"), std::string::npos) << ack.reason;
}

// =====================================================================
// 换绑清账与迟到丢弃（UI-T64 acceptance 2/4——既有回归保持不变）
// =====================================================================

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

// =====================================================================
// ★ KIN-P4：SessionSolve/TaskPointsBatch 真实分发（审查报告 §4 第二条
// 的具名自证面——"库存在评估器"升级为"通道真实执行"）
// =====================================================================

/// 工作集夹具：一任务点（World 参考系；位置 (1.8,0,0)——两连杆全展点，
/// 零位即达；容差/等级缺省）＋一启用工况（Must/AllStations——批量展开
/// "启用点集×caseSubset"的分母面：无工况＝0 工作项的空验证，域 §7.1
/// 展开语义如此，正式验证需要工况组合在案）。
req::RequirementWorkingSet makeWorksetWithTaskPoint()
{
    req::RequirementWorkingSet ws;

    req::TaskPoint point;
    point.objectId = core::ObjectId::tryFromCanonical(
        "obj-44444444444444444444444444444444").value_or(core::ObjectId{});
    point.name = "抓取点 P1";
    point.enabled = true;
    point.refFrame = req::RequirementReference{};  // World 缺省
    point.pose.position =
        core::SourcedValue<rw::math::Vector3D<double>>::provided(
            rw::math::Vector3D<double>(1.8, 0.0, 0.0),
            core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
    ws.points.entries.push_back(point);

    req::OperatingCondition condition;
    condition.objectId = core::ObjectId::tryFromCanonical(
        "obj-55555555555555555555555555555555").value_or(core::ObjectId{});
    condition.name = "标准负载工况";
    condition.level = req::RequirementLevel::Must;
    condition.enabled = true;  // caseSubset 分母（启用工况）
    ws.conditions.entries.push_back(condition);
    return ws;
}

/// 可控求解器替身：阻塞循环直至取消探针为真（SessionSolve 换绑取消的
/// 确定性载体——真实求解器的取消探针同语义，慢求解使窗口确定可控）。
class SlowCancelSolver final : public kinematics::IIkSolver {
public:
    kinematics::IkOutcome solve(const kinematics::IkRequest& request) const override
    {
        for (;;) {
            if (request.cancellationProbe
                && request.cancellationProbe()) {
                kinematics::IkOutcome cancelled;
                cancelled.cancelled = true;  // 取消非结局（§5.4——零终局字段）
                return cancelled;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
};

/// SessionSolve 真实分发：受理→后台真实求解（内置 IkSolver）→结构化
/// 账面＋note。修复前该请求被"单点求解后台通道未装配"整体拒绝。
TEST(KinEvaluationChannel, SolveChannel_BackgroundRealSolve_KIN_P4)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-02"},
                  std::vector<std::string>{});
    ExecutorHarness h;

    KinChannelBackgroundRequest request;
    request.kind = KinChannelBackgroundKind::SessionSolve;
    request.snapshotId = h.boundId;
    request.epoch = h.executor->epoch();
    // 目标位姿 (1.8,0,0)（基座系——两连杆全展点，零位精确解）。
    request.solveTargetInBase = rw::math::Transform3D<double>(
        rw::math::Vector3D<double>(1.8, 0.0, 0.0),
        rw::math::Rotation3D<double>(1, 0, 0, 0, 1, 0, 0, 0, 1));
    const KinChannelBackgroundAck ack = h.executor->submit(request);
    ASSERT_TRUE(ack.accepted) << ack.reason;
    EXPECT_NE(ack.taskRef.find("kin-solve-"), std::string::npos) << ack.taskRef;

    h.drain();
    // 结构化账面（真实求解产物——snapshotId/configDigest 锚与提交值同源）。
    const std::optional<KinChannelSolveResult> result =
        h.executor->latestSolveResult();
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->ok) << result->errorText;
    ASSERT_TRUE(result->solutionSet != nullptr);
    EXPECT_FALSE(result->solutionSet->solutions.empty());
    EXPECT_EQ(result->snapshotId, h.boundId);
    // 完成通知（kind 对齐——宿主 route 消费面按 kind 分流结构化取数）。
    ASSERT_EQ(h.notes.size(), 1U);
    EXPECT_EQ(h.notes[0].kind, KinChannelBackgroundKind::SessionSolve);
    EXPECT_FALSE(h.notes[0].interrupted);
}

/// SessionSolve 换绑取消：在途任务随快照换绑置位取消标志→慢求解器经
/// 取消探针早停→取消态如实呈现（非结局，零解集入账）＋迟到锚丢弃。
/// 真实可取消路径——修复前同步求解返回后才判超时，"转后台"通道本身
/// 不存在，无取消可言。
TEST(KinEvaluationChannel, SolveChannel_RebindCancelsInflight_KIN_P4)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-02", "NFR-REL-03"},
                  std::vector<std::string>{});
    ExecutorHarness h;
    h.noteTarget = 1;
    // 注入慢求解器（阻塞至取消探针为真——换绑拍置位的确定性窗口）。
    auto slow = std::make_shared<SlowCancelSolver>();
    h.rebuildExecutorWithSolver(slow.get());

    KinChannelBackgroundRequest request;
    request.kind = KinChannelBackgroundKind::SessionSolve;
    request.snapshotId = h.boundId;
    request.epoch = h.executor->epoch();
    request.solveTargetInBase = rw::math::Transform3D<double>(
        rw::math::Vector3D<double>(1.8, 0.0, 0.0),
        rw::math::Rotation3D<double>(1, 0, 0, 0, 1, 0, 0, 0, 1));
    const KinChannelBackgroundAck ack = h.executor->submit(request);
    ASSERT_TRUE(ack.accepted) << ack.reason;

    // 提交后立即换绑（置位在途取消——真实取消路径的宿主触发面）。
    h.executor->attachView(h.view, h.boundId);

    h.drain();
    // 取消回执按提交锚迟到丢弃（换绑推进纪元——取消产物不入账，账面
    // 保持空——L-K12 与结果路径同一账面纪律：迟到的不分成败都不打新锚）。
    const std::optional<KinChannelSolveResult> result =
        h.executor->latestSolveResult();
    EXPECT_FALSE(result.has_value())
        << (result.has_value() ? result->summaryText : "");
    // note 中断如实（NFR-REL-03——取消/迟到都不美化；摘要带迟到判定）。
    ASSERT_EQ(h.notes.size(), 1U);
    EXPECT_TRUE(h.notes[0].interrupted);
    EXPECT_NE(h.notes[0].summaryText.find("迟到"), std::string::npos)
        << h.notes[0].summaryText;
}

/// TaskPointsBatch 真实分发：工作集任务点切片→后台 runBatchComputation
/// →逐点结果账面（状态词/完成矩阵）。修复前被"批量验证执行通道未装配"
/// 整体拒绝。
TEST(KinEvaluationChannel, BatchChannel_RealComputationAndOutcomes_KIN_P4)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-03"},
                  std::vector<std::string>{});
    // 工作集换为一任务点（两连杆全展点——零位即达的确定性解）。
    auto editor = std::make_shared<ScriptedEditor>(makeWorksetWithTaskPoint());
    ExecutorHarness h;
    h.rebuildExecutorWithEditor(editor.get());
    h.noteTarget = 1;

    KinChannelBackgroundRequest request;
    request.kind = KinChannelBackgroundKind::TaskPointsBatch;
    request.snapshotId = h.boundId;
    request.epoch = h.executor->epoch();
    const KinChannelBackgroundAck ack = h.executor->submit(request);
    ASSERT_TRUE(ack.accepted) << ack.reason;
    EXPECT_NE(ack.taskRef.find("kin-batch-"), std::string::npos) << ack.taskRef;

    h.drain();
    // 逐点账面（真实批量计算产物——点状态 completed＋完成矩阵 true）。
    const std::map<core::ObjectId, KinChannelTaskPointOutcome> outcomes =
        h.executor->latestBatchOutcomes();
    ASSERT_EQ(outcomes.size(), 1U);
    const auto& outcome = outcomes.begin()->second;
    EXPECT_EQ(outcome.outcomeState, "completed") << outcome.outcomeText;
    EXPECT_TRUE(outcome.requiredCoverageDone);
    // 结果绑定锚（消费方核对键——宿主 taskPoints 投影的合入前提）。
    ASSERT_TRUE(h.executor->latestBatchSnapshotId().has_value());
    EXPECT_EQ(*h.executor->latestBatchSnapshotId(), h.boundId);
    // 完成通知（kind 对齐）。
    ASSERT_EQ(h.notes.size(), 1U);
    EXPECT_EQ(h.notes[0].kind, KinChannelBackgroundKind::TaskPointsBatch);
}

/**
 * @brief REQ-09/AT-23：混合非零RPY必须经参考系和基座合成后进入真实IK。
 * 零关节时TCP姿态是解析常量；只传参考系姿态将没有满足完整位姿的解。
 */
TEST(KinEvaluationChannel, BatchFixedMixedRpyWithRotatedFrames_REQ09_AT23)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-01", "REQ-09"},
                  std::vector<std::string>{"AT-23"});
    constexpr double pi = 3.14159265358979323846;
    const double h = std::sqrt(3.0) / 2.0;
    // 独立解析常量：Rz(π/6)·Ry(π/6)·Rx(π/2)，与产品RPY构造无关。
    const rw::math::Rotation3D<double> tcpR(
        0.75, h / 2, 0.5, h / 2, 0.25, -h, -0.5, h, 0);
    const rw::math::Transform3D<double> worldBase(
        rw::math::Vector3D<double>(1, 2, 3), rotZ(pi / 2));
    const rw::math::Transform3D<double> baseRef(
        rw::math::Vector3D<double>(0.2, 0.3, 0.4), rotZ(pi / 2));
    const auto frameId = idFrom<core::ObjectId>("req09-position-frame");
    auto ws = makeWorksetWithTaskPoint();
    auto& point = ws.points.entries.front();
    point.refFrame.kind = req::RequirementRefKind::ModelFrame;
    point.refFrame.objectId = frameId;
    point.pose.position = core::SourcedValue<rw::math::Vector3D<double>>::provided(
        // 比全展边界内移1 μm（远小于1 mm位置容差），避免多次世界变换的
        // 浮点舍入恰好越过解析半径上界；本用例集中验证姿态，而非边界证明。
        rw::math::Vector3D<double>(-0.3, -1.6 + 1e-6, -0.4),
        core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
    point.pose.constrainedDof = {true, true, true, true, true, true};
    point.pose.orientation.fixedRpy = {pi / 2, pi / 6, -pi / 3};
    ScriptedEditor editor(ws);
    ExecutorHarness harn;
    harn.executor = std::make_shared<KinEvaluationExecutor>(KinEvaluationExecutor::Deps{
        &editor, [=](const core::ObjectId& id)
            -> std::optional<rw::math::Transform3D<double>> {
            return id == frameId ? std::optional<rw::math::Transform3D<double>>(
                worldBase * baseRef) : std::nullopt;
        }, QCoreApplication::instance(),
        [&harn](const KinChannelBackgroundResultNote& note) { harn.notes.push_back(note); }});
    harn.executor->attachView(std::make_shared<FixtureView>(worldBase, tcpR), harn.boundId);
    auto config = makeLegalConfig();
    config.initialStrategy = InitialValueStrategy::ReferenceQ;
    harn.executor->setConfiguration(config);
    KinChannelBackgroundRequest request;
    request.kind = KinChannelBackgroundKind::TaskPointsBatch;
    request.snapshotId = harn.boundId;
    request.epoch = harn.executor->epoch();
    const auto ack = harn.executor->submit(request);
    ASSERT_TRUE(ack.accepted) << ack.reason;
    harn.drain();
    const auto results = harn.executor->latestBatchOutcomes();
    ASSERT_EQ(results.size(), 1U);
    EXPECT_EQ(results.begin()->second.outcomeState, "completed")
        << "非零混合RPY与旋转参考系应还原零位TCP，而非使用参考系旋转";
}

/** @brief REQ-09/AT-23：对齐目标与位置参考系必须分别解析，悬空目标禁止受理。 */
TEST(KinEvaluationChannel, BatchAlignFrameResolvesTargetAndRejectsDangling_REQ09_AT23)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-09"}, std::vector<std::string>{"AT-23"});
    const auto targetId = idFrom<core::ObjectId>("req09-align-frame");
    auto ws = makeWorksetWithTaskPoint();
    ws.points.entries.front().pose.orientation.kind = req::OrientationRuleKind::AlignFrame;
    ws.points.entries.front().pose.orientation.targetFrame.kind = req::RequirementRefKind::ModelFrame;
    ws.points.entries.front().pose.orientation.targetFrame.objectId = targetId;
    ws.points.entries.front().pose.constrainedDof = {true, true, true, true, true, true};
    ScriptedEditor editor(ws);
    ExecutorHarness harn;
    bool available = true;
    const auto tcpR = rotZ(0.6);
    harn.executor = std::make_shared<KinEvaluationExecutor>(KinEvaluationExecutor::Deps{
        &editor, [&](const core::ObjectId& id)
            -> std::optional<rw::math::Transform3D<double>> {
            return available && id == targetId
                ? std::optional<rw::math::Transform3D<double>>(
                    rw::math::Transform3D<double>(rw::math::Vector3D<double>(3, 4, 5), tcpR))
                : std::nullopt;
        }, QCoreApplication::instance(),
        [&harn](const KinChannelBackgroundResultNote& note) { harn.notes.push_back(note); }});
    harn.executor->attachView(std::make_shared<FixtureView>(trans(0, 0, 0), tcpR), harn.boundId);
    auto config = makeLegalConfig();
    config.initialStrategy = InitialValueStrategy::ReferenceQ;
    harn.executor->setConfiguration(config);
    KinChannelBackgroundRequest request;
    request.kind = KinChannelBackgroundKind::TaskPointsBatch;
    request.snapshotId = harn.boundId;
    request.epoch = harn.executor->epoch();
    const auto ack = harn.executor->submit(request);
    ASSERT_TRUE(ack.accepted) << ack.reason;
    harn.drain();
    const auto results = harn.executor->latestBatchOutcomes();
    ASSERT_EQ(results.size(), 1U);
    EXPECT_EQ(results.begin()->second.outcomeState, "completed");
    available = false;
    const auto rejected = harn.executor->submit(request);
    EXPECT_FALSE(rejected.accepted) << "悬空对齐引用不得降级为单位姿态继续求解";
    EXPECT_NE(rejected.reason.find(ws.points.entries.front().name), std::string::npos);
    EXPECT_TRUE(rejected.taskRef.empty());
}

}  // namespace
