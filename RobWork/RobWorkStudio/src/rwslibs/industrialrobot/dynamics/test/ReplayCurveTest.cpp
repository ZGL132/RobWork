/**
 * @file   ReplayCurveTest.cpp
 * @brief  WP-17-T08 用例组——DYN-08 数据面（DynCurveProjector／
 *         DynReplayData／DynReplaySampler／DynPeakLocate／DynCommands／
 *         DynDyn08Chain 六组）：各关节曲线联动投影、三维轨迹时刻回放
 *         数据构建与插值查表、峰值定位查询、领域命令受理与零修订契约、
 *         "峰值定位→游标跳转→三维姿态同步"全链联动（任务契约
 *         WP-17-T08 acceptance 逐条的执行证明面）。
 *
 * 设计依据：
 *   - units/dynamics.md §9.5（show-curves/locate-peak/replay-at 三命令
 *     数据面口径＋"UI 线程零计算"＋"回放数据随 payload 不入 dyn
 *     Profile"）、§4.6（非 Ok 行剔除/Empty 显式）、§4.3（不插值外推）、
 *     §10.7（命令适配器——零计算零修订）、§4.5（量纲类型化）
 *   - 需求 DYN-08（提供各关节曲线联动、峰值定位和三维轨迹时刻回放）、
 *     AT-04（预览类交互不产生项目修订——零修订机器断言）
 *   - 任务契约 WP-17-T08.json acceptance 1/2（"各关节曲线联动、峰值定
 *     位和三维轨迹时刻回放数据（DYN-08）用例通过"；"联动与回放不产生
 *     项目修订（会话/研究态零修订，AT-04 同口径）"）
 *
 * 数值对照口径（附录 D C7——测试对照与产品容差分离）：本文件黄金值全部
 *   **手工按投影定义算出**（投影＝样本行原值直拷或相邻两样本线性混合，
 *   无浮点递推/积分——期望值为一次线性式，断言容差取绝对 1e-12 即可；
 *   峰值期望值＝computePeaks 行集直读对账＋逐点手工黄金，双重对照）。
 *   产品代码零自设阈值（P-DYN-9 口径不因本任务引入）。
 */

#include <sdurws/ird/dynamics/Commands.hpp>
#include <sdurws/ird/dynamics/DynTypes.hpp>
#include <sdurws/ird/dynamics/Envelope.hpp>
#include <sdurws/ird/dynamics/Errors.hpp>
#include <sdurws/ird/dynamics/Replay.hpp>
#include <sdurws/ird/dynamics/SeriesBuilder.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "TwoLinkFixture.hpp"  // idFrom/digestOf——确定性 id/摘要派生助手

using namespace sdurws::ird::dynamics::testfixture;

using sdurws::ird::core::ContentIdentity;
using sdurws::ird::core::ObjectId;
using sdurws::ird::dynamics::CommandOutcome;
using sdurws::ird::dynamics::CommandPayload;
using sdurws::ird::dynamics::CurveProjection;
using sdurws::ird::dynamics::DynamicsCommandHandler;
using sdurws::ird::dynamics::DynamicsEnvelopeCalculator;
using sdurws::ird::dynamics::DynamicsError;
using sdurws::ird::dynamics::DynamicsReplayProjector;
using sdurws::ird::dynamics::DynamicsSample;
using sdurws::ird::dynamics::DynamicsCurveProjector;
using sdurws::ird::dynamics::DynamicsSeries;
using sdurws::ird::dynamics::DynamicsSeriesBuilder;
using sdurws::ird::dynamics::DynamicsPeakLocator;
using sdurws::ird::dynamics::DynJointType;
using sdurws::ird::dynamics::DynamicsValidity;
using sdurws::ird::dynamics::JointCurves;
using sdurws::ird::dynamics::PeakRecord;
using sdurws::ird::dynamics::ReplayData;
using sdurws::ird::dynamics::ReplaySample;
using sdurws::ird::dynamics::SampleNumericState;
using sdurws::ird::dynamics::SeriesIdentity;

namespace {

// =====================================================================
// 黄金序列构造（两关节×三时刻——曲线/回放/峰值定位共用的手工黄金；
// 解析式逐值可手算，插值中点为一次线性式）。
//
// 时间轴 t ∈ {0, 1, 2} s；段结构：t<1 归段 0、t≥1 归段 1（两段——段
// 定位键的黄金覆盖）。
//   关节 0（Revolute，力矩 N·m）：q=t²〔rad〕、q̇=2t〔rad/s〕、q̈=2
//     〔rad/s²〕、τ=10−t〔N·m〕、P=10t〔W〕（构造值——投影器不做
//     P=τ·q̇ 恒等式校验，统计器分层见 SeriesBuilder.hpp 文件头）
//   关节 1（Prismatic，力 N）：q=1−t〔m〕、q̇=−1〔m/s〕、q̈=0〔m/s²〕、
//     τ=−5−t〔N〕（全负——反向峰值素材）、P=−3t〔W〕
// 黄金值表（手工展开）：
//   t=0：J0 (0,0,2,10, 0)   J1 (1,−1,0,−5,  0)
//   t=1：J0 (1,2,2, 9,10)   J1 (0,−1,0,−6, −3)
//   t=2：J0 (4,4,2, 8,20)   J1 (−1,−1,0,−7,−6)
// =====================================================================

/// 黄金时刻表，单位 s。
constexpr double kGoldenT[3] = {0.0, 1.0, 2.0};
/// 时刻所属段（t<1→0，t≥1→1——段定位键黄金）。
constexpr std::uint32_t kGoldenSeg[3] = {0, 1, 1};
/// 黄金时刻数。
constexpr std::size_t kGoldenSteps = 3;

/// 手工黄金行值（五量通道——下标＝时刻序）。
constexpr double kQ0[3] = {0.0, 1.0, 4.0};      ///< 关节 0 位置，rad
constexpr double kQd0[3] = {0.0, 2.0, 4.0};     ///< 关节 0 速度，rad/s
constexpr double kQdd0[3] = {2.0, 2.0, 2.0};    ///< 关节 0 加速度，rad/s²
constexpr double kTau0[3] = {10.0, 9.0, 8.0};   ///< 关节 0 总力矩，N·m
constexpr double kP0[3] = {0.0, 10.0, 20.0};    ///< 关节 0 功率，W
constexpr double kQ1[3] = {1.0, 0.0, -1.0};     ///< 关节 1 位置，m
constexpr double kQd1[3] = {-1.0, -1.0, -1.0};  ///< 关节 1 速度，m/s
constexpr double kQdd1[3] = {0.0, 0.0, 0.0};    ///< 关节 1 加速度，m/s²
constexpr double kTau1[3] = {-5.0, -6.0, -7.0}; ///< 关节 1 总力，N
constexpr double kP1[3] = {0.0, -3.0, -6.0};    ///< 关节 1 功率，W

/// 样本行构造助手（同 StatisticsTest 形态——五分项字段置 0 自洽：
/// 投影/统计只消费 τ_total 与运动学通道，恒等式校验归 RNEA 黄金算例）。
DynamicsSample goldenRow(std::size_t step, std::uint32_t joint)
{
    DynamicsSample r;
    r.t = kGoldenT[step];                              // s
    r.segmentIndex = kGoldenSeg[step];                 // 所在轨迹段（0 基）
    r.conditionId = idFrom<ObjectId>("d08-cond");      // 工况 id（序列统一值）
    r.jointIndex = joint;
    r.jointObjectId = joint == 0 ? idFrom<ObjectId>("d08-j0")
                                 : idFrom<ObjectId>("d08-j1");
    r.jointType = joint == 0 ? DynJointType::Revolute : DynJointType::Prismatic;
    r.q = joint == 0 ? kQ0[step] : kQ1[step];          // rad 或 m
    r.qd = joint == 0 ? kQd0[step] : kQd1[step];       // rad/s 或 m/s
    r.qdd = joint == 0 ? kQdd0[step] : kQdd1[step];    // rad/s² 或 m/s²
    r.tauGravity = 0.0;                                // 分项不做拆解（助手注释）
    r.tauInertia = 0.0;
    r.tauCoriolisCentrifugal = 0.0;
    r.tauFriction = 0.0;
    r.tauExternal = 0.0;                               // R1 恒 0（P-DYN-3）
    r.tauTotal = joint == 0 ? kTau0[step] : kTau1[step]; // N·m 或 N
    r.mechanicalPower = joint == 0 ? kP0[step] : kP1[step]; // W
    r.energyIntegralJ = 0.0;                           // J（本面不消费）
    r.payloadVariantIndex = 0;                         // 基线变体
    r.toolObjectId = ObjectId{};                       // 无工具模型
    r.numericState = SampleNumericState::Ok;
    return r;
}

/// 序列身份块构造助手（同 StatisticsTest 形态——确定性 id/摘要派生）。
SeriesIdentity makeIdentity(const std::string& tag, std::size_t planned)
{
    SeriesIdentity id;
    id.snapshotId.bytes = digestOf(tag + "-snap");
    id.sliceId.bytes = digestOf(tag + "-slice");
    id.trajectoryPayloadId.bytes = digestOf(tag + "-trj");
    id.conditionId = idFrom<ObjectId>(tag + "-cond");
    id.toolObjectId = ObjectId{};
    id.dynConfigDigest = "sha256-" + tag;
    id.task.project = idFrom<sdurws::ird::core::ProjectId>(tag + "-prj");
    id.task.branch = idFrom<sdurws::ird::core::BranchId>(tag + "-brn");
    id.task.revision = idFrom<sdurws::ird::core::RevisionId>(tag + "-rev");
    id.task.run = idFrom<sdurws::ird::core::RunId>(tag + "-run");
    id.task.attempt.value = 1u;
    id.plannedSampleCount = planned;
    return id;
}

/// 组装黄金序列（行序 (t, jointIndex) 评估器输出序——构建器冻结）。
DynamicsSeries makeGoldenSeries(const std::string& tag)
{
    DynamicsSeriesBuilder b;
    for (std::size_t s = 0; s < kGoldenSteps; ++s) {
        b.addSample(goldenRow(s, 0));
        b.addSample(goldenRow(s, 1));
    }
    return b.finalize(makeIdentity(tag, kGoldenSteps));  // 3 刻度全到→Complete
}

/// 期望值相对容差断言（期望 0 时退化为绝对 1e-15——避免零除）。
void expectNearRel(double actual, double expected, const char* what)
{
    const double tol = std::max(1e-15, 1e-12 * std::abs(expected));
    EXPECT_NEAR(actual, expected, tol) << what << "（实得 " << actual
                                       << "，期望 " << expected << "）";
}

}  // namespace

// =====================================================================
// DynCurveProjector 组——曲线联动投影（show-curves 数据面）。
// =====================================================================

/**
 * @brief 黄金投影：两关节五通道逐点与源样本直拷对账（DYN-08 曲线联动
 *        数据面主用例——AT 逐字段核对"读归档 payload 投影"零失真）。
 */
TEST(DynCurveProjector, ProjectsPerJointFiveChannelsGolden)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-08"}, std::vector<std::string>{});
    const DynamicsSeries series = makeGoldenSeries("d08-curve");
    const DynamicsCurveProjector projector;
    const CurveProjection proj = projector.projectCurves(series);

    // 元数据透传：工况/来源序列内容身份/完整性。
    EXPECT_TRUE(proj.conditionId == series.conditionId);
    EXPECT_TRUE(proj.sourceSeriesId == series.contentIdentity);
    EXPECT_EQ(proj.completeness, DynamicsValidity::Completeness::Complete);

    // 关节行集：两关节，jointIndex 升序（稳定序）。
    ASSERT_EQ(proj.joints.size(), 2u);
    EXPECT_EQ(proj.joints[0].jointIndex, 0u);
    EXPECT_EQ(proj.joints[1].jointIndex, 1u);

    // 逐关节黄金核对：元数据＋五通道逐点＝源样本原值（零重算零平滑）。
    const JointCurves& j0 = proj.joints[0];
    EXPECT_TRUE(j0.jointObjectId == idFrom<ObjectId>("d08-j0"));
    EXPECT_EQ(j0.jointType, DynJointType::Revolute);
    EXPECT_EQ(j0.nonOkCount, 0u);
    ASSERT_EQ(j0.t.size(), kGoldenSteps);
    for (std::size_t s = 0; s < kGoldenSteps; ++s) {
        expectNearRel(j0.t[s], kGoldenT[s], "J0 时间轴");
        expectNearRel(j0.q[s], kQ0[s], "J0 q（rad）");
        expectNearRel(j0.qd[s], kQd0[s], "J0 qd（rad/s）");
        expectNearRel(j0.qdd[s], kQdd0[s], "J0 qdd（rad/s²）");
        expectNearRel(j0.generalizedForce[s], kTau0[s], "J0 τ（N·m）");
        expectNearRel(j0.mechanicalPower[s], kP0[s], "J0 P（W）");
    }
    const JointCurves& j1 = proj.joints[1];
    EXPECT_TRUE(j1.jointObjectId == idFrom<ObjectId>("d08-j1"));
    EXPECT_EQ(j1.jointType, DynJointType::Prismatic);
    EXPECT_EQ(j1.nonOkCount, 0u);
    ASSERT_EQ(j1.t.size(), kGoldenSteps);
    for (std::size_t s = 0; s < kGoldenSteps; ++s) {
        expectNearRel(j1.t[s], kGoldenT[s], "J1 时间轴");
        expectNearRel(j1.q[s], kQ1[s], "J1 q（m）");
        expectNearRel(j1.qd[s], kQd1[s], "J1 qd（m/s）");
        expectNearRel(j1.qdd[s], kQdd1[s], "J1 qdd（m/s²）");
        expectNearRel(j1.generalizedForce[s], kTau1[s], "J1 F（N）");
        expectNearRel(j1.mechanicalPower[s], kP1[s], "J1 P（W）");
    }
}

/**
 * @brief 非 Ok 行剔除与计数（NFR-COR-03：非有限值不进曲线——逐字段
 *        不失真由 nonOkCount 如实标注；Partial 透传不阻断投影）。
 */
TEST(DynCurveProjector, ExcludesNonOkRowsAndCountsThem)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-08"}, std::vector<std::string>{});
    // 黄金序列中关节 1 的 t=1 行置非有限输入（评估器样本级标记形态——
    // §4.6 行保留、值非有限）。
    DynamicsSeriesBuilder b;
    for (std::size_t s = 0; s < kGoldenSteps; ++s) {
        b.addSample(goldenRow(s, 0));
        DynamicsSample row = goldenRow(s, 1);
        if (s == 1) {
            row.numericState = SampleNumericState::NonFiniteInput; // 样本级标记
            row.qd = std::numeric_limits<double>::quiet_NaN();     // 非有限值
        }
        b.addSample(row);
    }
    const DynamicsSeries series = b.finalize(makeIdentity("d08-curve-nan", kGoldenSteps));
    ASSERT_EQ(series.validity.completeness, DynamicsValidity::Completeness::Partial);

    const DynamicsCurveProjector projector;
    const CurveProjection proj = projector.projectCurves(series);

    // 关节 0 不受污染：3 点全在、零剔除。
    ASSERT_EQ(proj.joints.size(), 2u);
    EXPECT_EQ(proj.joints[0].t.size(), kGoldenSteps);
    EXPECT_EQ(proj.joints[0].nonOkCount, 0u);
    // 关节 1：非 Ok 行剔除（NaN 不进曲线——画出来即失真），计数如实。
    EXPECT_EQ(proj.joints[1].t.size(), kGoldenSteps - 1u);
    EXPECT_EQ(proj.joints[1].nonOkCount, 1u);
    // 剩余两点必须是 t=0/t=2 原值（顺序保持——非 Ok 行剔除不重排）。
    expectNearRel(proj.joints[1].t[0], 0.0, "J1 首点 t");
    expectNearRel(proj.joints[1].t[1], 2.0, "J1 末点 t");
    expectNearRel(proj.joints[1].q[1], kQ1[2], "J1 末点 q（m）");
    // 完整性透传 Partial（UI 标注数据缺失的呈现依据）。
    EXPECT_EQ(proj.completeness, DynamicsValidity::Completeness::Partial);
}

/**
 * @brief 行序结构违约 fail-fast（防御面——时间倒退／同刻度关节行重复：
 *        投影是显示直接数据源，取值歧义必须 fail-fast 暴露不静默）。
 */
TEST(DynCurveProjector, RejectsBackwardTimeAndDuplicateJointRows)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-08"}, std::vector<std::string>{});
    const DynamicsCurveProjector projector;

    // 时间倒退（t=1 行后接 t=0 行）：构建器对倒退是 addSample 即抛（
    // SeriesBuilder 契约"addSample 即抛 DynamicsError，非 finalize"）——
    // 上游违约的正式拦截点在构建器；投影器同款防御拦截"绕过构建器的
    // 手工序列"（重复行分支直构验证）。
    {
        DynamicsSeriesBuilder b;
        b.addSample(goldenRow(1, 0));
        EXPECT_THROW(b.addSample(goldenRow(0, 0)), DynamicsError);
    }
    // 投影器同款防御：手工打乱已冻结序列的行序（绕过构建器注入倒退行
    // ——防御面拦截"不可能形态"，token 逐字核对）。
    {
        DynamicsSeries series = makeGoldenSeries("d08-bwd2");
        std::swap(series.samples[0], series.samples[4]);  // t=0 J0 ↔ t=1 J0
        bool threw = false;
        try {
            (void)projector.projectCurves(series);
        } catch (const DynamicsError& e) {
            threw = true;
            EXPECT_EQ(e.token(), "series-non-monotonic");
        }
        EXPECT_TRUE(threw);
    }
    // 同刻度关节行重复：直接构造越 builder 的行序（构建器对重复行不
    // 拒收仅 finalize 标记——投影器作为只读消费者必须 fail-fast，与
    // SeriesBuilder 的"标记"通道差异见 Replay.hpp 诚实登记）。
    {
        DynamicsSeries series = makeGoldenSeries("d08-dup");
        series.samples.insert(series.samples.begin() + 1, goldenRow(0, 0)); // (t=0,J0) 重复
        EXPECT_THROW(projector.projectCurves(series), DynamicsError);
        try {
            (void)projector.projectCurves(series);
        } catch (const DynamicsError& e) {
            EXPECT_EQ(e.token(), "series-row-duplicate");
        }
    }
}

/**
 * @brief 空序列 Empty 显式语义（joints 空——绝不伪造 0 值曲线，
 *        NFR-COR-03）。
 */
TEST(DynCurveProjector, EmptySeriesYieldsEmptyProjection)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-08"}, std::vector<std::string>{});
    DynamicsSeriesBuilder b;
    const DynamicsSeries series = b.finalize(makeIdentity("d08-curve-empty", 0));
    ASSERT_EQ(series.validity.completeness, DynamicsValidity::Completeness::Empty);

    const DynamicsCurveProjector projector;
    const CurveProjection proj = projector.projectCurves(series);
    EXPECT_TRUE(proj.joints.empty());
    EXPECT_EQ(proj.completeness, DynamicsValidity::Completeness::Empty);
    EXPECT_TRUE(proj.conditionId == series.conditionId);
}

// =====================================================================
// DynReplayData 组——回放数据构建（replay-at 数据面：行→帧重组）。
// =====================================================================

/**
 * @brief 帧重组黄金：逐时刻全关节帧（帧 t/段/帧内关节升序/五量直拷）。
 */
TEST(DynReplayData, GroupsRowsIntoFramesGolden)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-08"}, std::vector<std::string>{});
    const DynamicsSeries series = makeGoldenSeries("d08-replay");
    const DynamicsReplayProjector projector;
    const ReplayData data = projector.buildReplayData(series);

    EXPECT_TRUE(data.conditionId == series.conditionId);
    EXPECT_TRUE(data.sourceSeriesId == series.contentIdentity);
    EXPECT_EQ(data.completeness, DynamicsValidity::Completeness::Complete);
    EXPECT_EQ(data.jointCount, 2u);
    ASSERT_EQ(data.frames.size(), kGoldenSteps);

    for (std::size_t s = 0; s < kGoldenSteps; ++s) {
        const auto& frame = data.frames[s];
        expectNearRel(frame.t, kGoldenT[s], "帧 t（s）");
        EXPECT_EQ(frame.segmentIndex, kGoldenSeg[s]);  // 段定位键直通
        EXPECT_TRUE(frame.completeAllJoints);          // 全关节到齐
        ASSERT_EQ(frame.joints.size(), 2u);
        EXPECT_EQ(frame.joints[0].jointIndex, 0u);     // 帧内链序升序
        EXPECT_EQ(frame.joints[1].jointIndex, 1u);
        expectNearRel(frame.joints[0].q, kQ0[s], "帧 J0 q（rad）");
        expectNearRel(frame.joints[0].generalizedForce, kTau0[s], "帧 J0 τ（N·m）");
        expectNearRel(frame.joints[1].q, kQ1[s], "帧 J1 q（m）");
        expectNearRel(frame.joints[1].mechanicalPower, kP1[s], "帧 J1 P（W）");
    }
    // 帧时刻严格递增（插值二分的前提——构建纪律的黄金核对）。
    EXPECT_TRUE(data.frames[0].t < data.frames[1].t);
    EXPECT_TRUE(data.frames[1].t < data.frames[2].t);
}

/**
 * @brief 缺行帧完整性标注（某时刻关节缺 Ok 行→帧不完整但其余关节状态
 *        照常交付——不截断伪造，§4.6 精神）。
 */
TEST(DynReplayData, MarksIncompleteFramesForMissingRows)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-08"}, std::vector<std::string>{});
    DynamicsSeriesBuilder b;
    for (std::size_t s = 0; s < kGoldenSteps; ++s) {
        b.addSample(goldenRow(s, 0));
        DynamicsSample row = goldenRow(s, 1);
        if (s == 1) {
            row.numericState = SampleNumericState::Overflow;  // 该行不进帧
        }
        b.addSample(row);
    }
    const DynamicsSeries series = b.finalize(makeIdentity("d08-replay-gap", kGoldenSteps));

    const DynamicsReplayProjector projector;
    const ReplayData data = projector.buildReplayData(series);
    ASSERT_EQ(data.frames.size(), kGoldenSteps);
    EXPECT_TRUE(data.frames[0].completeAllJoints);
    // t=1 帧：关节 1 缺行→帧不完整＋仅含关节 0（其余关节不丢）。
    EXPECT_FALSE(data.frames[1].completeAllJoints);
    ASSERT_EQ(data.frames[1].joints.size(), 1u);
    EXPECT_EQ(data.frames[1].joints[0].jointIndex, 0u);
    EXPECT_TRUE(data.frames[2].completeAllJoints);
}

/**
 * @brief 空序列 Empty 显式语义（frames 空＋jointCount=0——不伪造帧）。
 */
TEST(DynReplayData, EmptySeriesYieldsNoFrames)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-08"}, std::vector<std::string>{});
    DynamicsSeriesBuilder b;
    const DynamicsSeries series = b.finalize(makeIdentity("d08-replay-empty", 0));
    const DynamicsReplayProjector projector;
    const ReplayData data = projector.buildReplayData(series);
    EXPECT_TRUE(data.frames.empty());
    EXPECT_EQ(data.jointCount, 0u);
    EXPECT_EQ(data.completeness, DynamicsValidity::Completeness::Empty);
}

// =====================================================================
// DynReplaySampler 组——插值查表（replay-at"按 t 驱动会话姿态"）。
// =====================================================================

/**
 * @brief 精确命中帧点：原值直拷 exact=true（样本时刻零失真）。
 */
TEST(DynReplaySampler, ExactHitCopiesFrame)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-08"}, std::vector<std::string>{});
    const DynamicsSeries series = makeGoldenSeries("d08-samp");
    const DynamicsReplayProjector projector;
    const ReplayData data = projector.buildReplayData(series);

    const std::optional<ReplaySample> hit = projector.sampleAt(data, 1.0);
    ASSERT_TRUE(hit.has_value());
    EXPECT_TRUE(hit->exact);
    expectNearRel(hit->t, 1.0, "命中时刻");
    EXPECT_EQ(hit->segmentIndex, kGoldenSeg[1]);  // 帧段号直通
    EXPECT_TRUE(hit->completeAllJoints);
    ASSERT_EQ(hit->joints.size(), 2u);
    // 原值直拷（非混合——与 t=1 黄金行逐位一致）。
    expectNearRel(hit->joints[0].q, kQ0[1], "J0 q（rad）");
    expectNearRel(hit->joints[0].qd, kQd0[1], "J0 qd（rad/s）");
    expectNearRel(hit->joints[1].q, kQ1[1], "J1 q（m）");
    expectNearRel(hit->joints[1].generalizedForce, kTau1[1], "J1 F（N）");
}

/**
 * @brief 区间中点线性插值黄金（α=0.5——一次线性式逐点手算）＋段归属
 *        取左帧。
 */
TEST(DynReplaySampler, MidpointInterpolationGolden)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-08"}, std::vector<std::string>{});
    const DynamicsSeries series = makeGoldenSeries("d08-lerp");
    const DynamicsReplayProjector projector;
    const ReplayData data = projector.buildReplayData(series);

    // t=0.5：区间 [t0,t1]，α=0.5；段取左帧（段 0——[tL,tR) 左闭口径）。
    const std::optional<ReplaySample> mid = projector.sampleAt(data, 0.5);
    ASSERT_TRUE(mid.has_value());
    EXPECT_FALSE(mid->exact);
    EXPECT_EQ(mid->segmentIndex, kGoldenSeg[0]);
    EXPECT_TRUE(mid->completeAllJoints);  // 两帧关节齐全→插值帧齐全
    ASSERT_EQ(mid->joints.size(), 2u);
    // 关节 0（α=0.5 黄金）：q=0.5 rad、q̇=1 rad/s、q̈=2 rad/s²、τ=9.5 N·m、
    // P=5 W；关节 1：q=0.5 m、τ=−5.5 N、P=−1.5 W。
    expectNearRel(mid->joints[0].q, 0.5, "J0 插值 q（rad）");
    expectNearRel(mid->joints[0].qd, 1.0, "J0 插值 qd（rad/s）");
    expectNearRel(mid->joints[0].qdd, 2.0, "J0 插值 qdd（rad/s²）");
    expectNearRel(mid->joints[0].generalizedForce, 9.5, "J0 插值 τ（N·m）");
    expectNearRel(mid->joints[0].mechanicalPower, 5.0, "J0 插值 P（W）");
    expectNearRel(mid->joints[1].q, 0.5, "J1 插值 q（m）");
    expectNearRel(mid->joints[1].qd, -1.0, "J1 插值 qd（m/s）");
    expectNearRel(mid->joints[1].generalizedForce, -5.5, "J1 插值 F（N）");
    expectNearRel(mid->joints[1].mechanicalPower, -1.5, "J1 插值 P（W）");

    // 端点亦为帧点（t=0/2 精确命中——区间边界的黄金核对）。
    const std::optional<ReplaySample> first = projector.sampleAt(data, 0.0);
    ASSERT_TRUE(first.has_value());
    EXPECT_TRUE(first->exact);
}

/**
 * @brief 越界不外推＋非有限时刻 fail-fast（§4.3"不插值外推"纪律——
 *        越界数据不存在，呈现侧停在端点；NaN 时刻是调用方错误）。
 */
TEST(DynReplaySampler, RejectsOutOfRangeAndNonFiniteTime)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-08"}, std::vector<std::string>{});
    const DynamicsSeries series = makeGoldenSeries("d08-range");
    const DynamicsReplayProjector projector;
    const ReplayData data = projector.buildReplayData(series);

    // 越界（负时刻/超末帧）→空态不外推。
    EXPECT_FALSE(projector.sampleAt(data, -0.1).has_value());
    EXPECT_FALSE(projector.sampleAt(data, 2.1).has_value());
    // 非有限时刻→调用方错误 fail-fast（NFR-COR-03 不静默）。
    bool threw = false;
    try {
        (void)projector.sampleAt(data, std::numeric_limits<double>::quiet_NaN());
    } catch (const DynamicsError& e) {
        threw = true;
        EXPECT_EQ(e.token(), "replay-time-invalid");
    }
    EXPECT_TRUE(threw);
}

/**
 * @brief 空数据集空态＋缺行帧的逐关节独立插值（单侧独有关节取单侧值并
 *        置不完整位——无对侧数据源不虚构）。
 */
TEST(DynReplaySampler, EmptyDataAndPartialFrameInterpolation)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-08"}, std::vector<std::string>{});
    const DynamicsReplayProjector projector;

    // 空数据集→空态（不伪造帧；数据集取自空序列的构建产物——非默认
    // 构造，成员语义完整）。
    {
        DynamicsSeriesBuilder b;
        const DynamicsSeries emptySeries = b.finalize(makeIdentity("d08-samp-empty", 0));
        const ReplayData empty = projector.buildReplayData(emptySeries);
        ASSERT_TRUE(empty.frames.empty());
        EXPECT_FALSE(projector.sampleAt(empty, 0.0).has_value());
    }
    // t=1 缺关节 1 行：t=0.5 插值时关节 1 仅左帧（t=0）有数据→单侧取
    // 左值＋帧不完整；关节 0 双帧齐全正常插值。
    {
        DynamicsSeriesBuilder b;
        for (std::size_t s = 0; s < kGoldenSteps; ++s) {
            b.addSample(goldenRow(s, 0));
            DynamicsSample row = goldenRow(s, 1);
            if (s == 1) {
                row.numericState = SampleNumericState::NonFiniteOutput;
            }
            b.addSample(row);
        }
        const DynamicsSeries series =
            b.finalize(makeIdentity("d08-lerp-partial", kGoldenSteps));
        const ReplayData data = projector.buildReplayData(series);
        const std::optional<ReplaySample> mid = projector.sampleAt(data, 0.5);
        ASSERT_TRUE(mid.has_value());
        EXPECT_FALSE(mid->exact);
        EXPECT_FALSE(mid->completeAllJoints);  // 关节 1 单侧取值→不完整
        ASSERT_EQ(mid->joints.size(), 2u);
        // 关节 0：正常插值黄金（同 MidpointInterpolationGolden）。
        expectNearRel(mid->joints[0].q, 0.5, "J0 插值 q（rad）");
        // 关节 1：单侧取左帧原值（q=1 m、F=−5 N——t=0 行）。
        expectNearRel(mid->joints[1].q, kQ1[0], "J1 单侧 q（m）");
        expectNearRel(mid->joints[1].generalizedForce, kTau1[0], "J1 单侧 F（N）");
    }
}

// =====================================================================
// DynPeakLocate 组——峰值定位（locate-peak 数据面：消费 computePeaks）。
// =====================================================================

/**
 * @brief 逐关节逐 token 定位与 computePeaks 行集对账（行序契约钉扎）＋
 *        峰值三要素黄金抽查（值/时刻/段——DYN-03 定位语义）。
 */
TEST(DynPeakLocate, LocatesEveryTokenAgainstComputePeaks)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-08", "DYN-03"},
                  std::vector<std::string>{});
    const DynamicsSeries series = makeGoldenSeries("d08-peak");
    const DynamicsEnvelopeCalculator calculator;
    const std::vector<PeakRecord> peaks = calculator.computePeaks(series);
    ASSERT_EQ(peaks.size(), 2u * 6u);  // 两 Ok 关节×每关节 6 行（token 表）

    const DynamicsPeakLocator locator;
    // 全表对账：locate(joint, token) 与行集 (位次×6+token) 逐位一致。
    for (std::uint32_t joint = 0; joint < 2; ++joint) {
        for (int token = 0; token < 6; ++token) {
            const std::optional<PeakRecord> hit = locator.locate(series, peaks, joint, token);
            ASSERT_TRUE(hit.has_value()) << "joint=" << joint << " token=" << token;
            const PeakRecord& expect = peaks[joint * 6u + static_cast<std::size_t>(token)];
            EXPECT_DOUBLE_EQ(hit->value, expect.value);
            EXPECT_DOUBLE_EQ(hit->tPeakS, expect.tPeakS);
            EXPECT_EQ(hit->segmentIndex, expect.segmentIndex);
            EXPECT_DOUBLE_EQ(hit->windowStartS, expect.windowStartS);
            EXPECT_DOUBLE_EQ(hit->windowEndS, expect.windowEndS);
        }
    }
    // 黄金抽查（token 表：2=速度 0=力矩正 1=力矩反 5=功率反——手工峰值）：
    // 关节 0 速度峰值＝max|2t|=4 rad/s @ t=2（段 1）；关节 0 力矩正
    // ＝max(10−t)=10 N·m @ t=0；关节 1 力矩正＝max(−5−t)=−5 N（全负
    // ——带符号实际值语义）@ t=0；关节 1 功率反＝max(−(−3t))=6 W @ t=2。
    const auto v0 = locator.locate(series, peaks, 0, 2);
    ASSERT_TRUE(v0.has_value());
    expectNearRel(v0->value, 4.0, "J0 速度峰值（rad/s）");
    expectNearRel(v0->tPeakS, 2.0, "J0 速度峰值时刻（s）");
    EXPECT_EQ(v0->segmentIndex, 1u);
    const auto tp0 = locator.locate(series, peaks, 0, 0);
    ASSERT_TRUE(tp0.has_value());
    expectNearRel(tp0->value, 10.0, "J0 力矩正峰（N·m）");
    expectNearRel(tp0->tPeakS, 0.0, "J0 力矩正峰时刻（s）");
    const auto tp1 = locator.locate(series, peaks, 1, 0);
    ASSERT_TRUE(tp1.has_value());
    expectNearRel(tp1->value, -5.0, "J1 力矩正峰（N——全负循环带符号）");
    const auto pn1 = locator.locate(series, peaks, 1, 5);
    ASSERT_TRUE(pn1.has_value());
    expectNearRel(pn1->value, 6.0, "J1 功率反峰（W）");
    expectNearRel(pn1->tPeakS, 2.0, "J1 功率反峰时刻（s）");
}

/**
 * @brief 无 Ok 行关节定位空态＋空序列空态（显式未找到——不伪造零值
 *        峰值记录，NFR-COR-03）。
 */
TEST(DynPeakLocate, ReturnsNulloptForJointWithoutOkRows)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-08"}, std::vector<std::string>{});
    // 关节 1 全部行非 Ok：行集只含关节 0（6 行）——关节 1 定位空态。
    {
        DynamicsSeriesBuilder b;
        for (std::size_t s = 0; s < kGoldenSteps; ++s) {
            b.addSample(goldenRow(s, 0));
            DynamicsSample row = goldenRow(s, 1);
            row.numericState = SampleNumericState::NonFiniteInput;
            b.addSample(row);
        }
        const DynamicsSeries series =
            b.finalize(makeIdentity("d08-peak-nonan", kGoldenSteps));
        const DynamicsEnvelopeCalculator calculator;
        const std::vector<PeakRecord> peaks = calculator.computePeaks(series);
        ASSERT_EQ(peaks.size(), 6u);  // 仅关节 0 的 6 行

        const DynamicsPeakLocator locator;
        EXPECT_TRUE(locator.locate(series, peaks, 0, 0).has_value());
        EXPECT_FALSE(locator.locate(series, peaks, 1, 0).has_value());  // 空态
    }
    // 空序列→空态。
    {
        DynamicsSeriesBuilder b;
        const DynamicsSeries series = b.finalize(makeIdentity("d08-peak-empty", 0));
        const DynamicsPeakLocator locator;
        EXPECT_FALSE(locator.locate(series, {}, 0, 0).has_value());
    }
}

/**
 * @brief 词表越界与行集不同源 fail-fast（定位错位比定位失败更危险——
 *        结构对账违约必须暴露，token 逐字核对）。
 */
TEST(DynPeakLocate, RejectsTokenOutOfRangeAndMismatchedPeaks)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-08"}, std::vector<std::string>{});
    const DynamicsSeries series = makeGoldenSeries("d08-peak-bad");
    const DynamicsEnvelopeCalculator calculator;
    const std::vector<PeakRecord> peaks = calculator.computePeaks(series);
    const DynamicsPeakLocator locator;

    // token 越界（6＝词表外；−1 同理）。
    for (const int badToken : {6, -1}) {
        bool threw = false;
        try {
            (void)locator.locate(series, peaks, 0, badToken);
        } catch (const DynamicsError& e) {
            threw = true;
            EXPECT_EQ(e.token(), "peak-token-out-of-range");
        }
        EXPECT_TRUE(threw) << "token=" << badToken;
    }
    // 行集截断（不同源/不完整）→结构对账 fail-fast。
    const std::vector<PeakRecord> truncated(peaks.begin(), peaks.end() - 1);
    bool threw = false;
    try {
        (void)locator.locate(series, truncated, 0, 0);
    } catch (const DynamicsError& e) {
        threw = true;
        EXPECT_EQ(e.token(), "peaks-series-mismatch");
    }
    EXPECT_TRUE(threw);
}

// =====================================================================
// DynCommands 组——领域命令适配器（§10.7）＋零修订契约（AT-04）。
// =====================================================================

/**
 * @brief 五命令全受理＋零修订四语义位（acceptance 2 机器断言面：任一
 *        命令受理结果 producesRevision=false——AT-04 会话/研究态零修订）。
 */
TEST(DynCommands, AcceptsAllFiveTokensWithZeroRevisionContract)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-08"}, std::vector<std::string>{"AT-04"});
    const DynamicsCommandHandler handler;
    for (const std::string_view token : sdurws::ird::dynamics::kCommandTokens) {
        const CommandPayload payload;  // commandToken 空＝不交叉校验
        const CommandOutcome out = handler.handle(token, payload);
        EXPECT_TRUE(out.accepted) << token;
        // 零修订契约四语义位（kReplaySessionContract 同值——逐位断言）。
        EXPECT_TRUE(out.sessionStateOnly) << token;
        EXPECT_FALSE(out.writesDesignModel) << token;
        EXPECT_FALSE(out.producesRevision) << token;   // ★ AT-04 断言面
        EXPECT_FALSE(out.invalidatesResults) << token;
        EXPECT_TRUE(out.rejectionToken.empty()) << token;
    }
    // replay-at 负载携带时刻/关节参数照常受理（负载字段不被解读——零
    // 计算纪律）。
    CommandPayload replay;
    replay.replayTimeS = 1.25;  // s
    replay.jointIndex = 3;
    const CommandOutcome out = handler.handle(sdurws::ird::dynamics::kCmdReplayAt, replay);
    EXPECT_TRUE(out.accepted);
    EXPECT_FALSE(out.producesRevision);
}

/**
 * @brief 表外/错配负载拒绝（词表封闭——大小写敏感精确匹配；负载交叉
 *        校验防 UI 组装错位；拒绝不抛异常＝UI 可呈现不可用态）。
 */
TEST(DynCommands, RejectsUnknownAndMismatchedPayloadTokens)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-08"}, std::vector<std::string>{});
    const DynamicsCommandHandler handler;
    const CommandPayload payload;

    // 表外 token（未收录/大小写不同/前缀不同）→拒绝＋"unknown-token"。
    for (const std::string_view bad :
         {std::string_view{"dynamics.unknown"}, std::string_view{"Dynamics.Analyze"},
          std::string_view{"dynamics.analyze "}, std::string_view{""}}) {
        const CommandOutcome out = handler.handle(bad, payload);
        EXPECT_FALSE(out.accepted) << bad;
        EXPECT_EQ(out.rejectionToken, "unknown-token") << bad;
        // 拒绝态不作任何会话效果承诺（四语义位全 false）。
        EXPECT_FALSE(out.producesRevision);
        EXPECT_FALSE(out.sessionStateOnly);
    }
    // 负载交叉校验错配→拒绝＋"payload-token-mismatch"。
    {
        CommandPayload mismatch;
        mismatch.commandToken = "dynamics.show-curves";  // 与命令不一致
        const CommandOutcome out =
            handler.handle(sdurws::ird::dynamics::kCmdReplayAt, mismatch);
        EXPECT_FALSE(out.accepted);
        EXPECT_EQ(out.rejectionToken, "payload-token-mismatch");
    }
}

/**
 * @brief 零修订会话契约常量值冻结（kReplaySessionContract 四位——与 ui
 *        单元 View3DSessionPoseContract 同值对账：两侧各自以文档出处
 *        字面冻结，值漂移即本用例失败；出处＝ui.md/View3DContract.hpp
 *        KIN-06/AT-04 钉住值）。
 */
TEST(DynCommands, SessionPoseContractValuesFrozen)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-08"}, std::vector<std::string>{"AT-04", "KIN-06"});
    // 四位钉住（真/假/假/假——语义变更必须走需求修订，代码不得私改）。
    EXPECT_TRUE(sdurws::ird::dynamics::kReplaySessionContract.sessionStateOnly);
    EXPECT_FALSE(sdurws::ird::dynamics::kReplaySessionContract.writesDesignModel);
    EXPECT_FALSE(sdurws::ird::dynamics::kReplaySessionContract.producesRevision);
    EXPECT_FALSE(sdurws::ird::dynamics::kReplaySessionContract.invalidatesResults);
    // 词表字面冻结（§9.5 表 token 原文——跨版本命令契约，漂移即断链）。
    EXPECT_EQ(sdurws::ird::dynamics::kCmdAnalyze, "dynamics.analyze");
    EXPECT_EQ(sdurws::ird::dynamics::kCmdShowCurves, "dynamics.show-curves");
    EXPECT_EQ(sdurws::ird::dynamics::kCmdLocatePeak, "dynamics.locate-peak");
    EXPECT_EQ(sdurws::ird::dynamics::kCmdReplayAt, "dynamics.replay-at");
    EXPECT_EQ(sdurws::ird::dynamics::kCmdExportCurveData, "dynamics.export-curve-data");
    EXPECT_EQ(sdurws::ird::dynamics::kCommandTokens.size(), 5u);
}

// =====================================================================
// DynDyn08Chain 组——DYN-08 全链联动（峰值定位→游标跳转→三维姿态同步
// ＋零修订命令面）。
// =====================================================================

/**
 * @brief 端到端联动：序列→computePeaks→locate（游标跳转目标）→
 *        sampleAt(tPeak)（三维姿态同步驱动数据）→replay-at 命令受理
 *        （零修订）——"曲线游标跳转峰值时刻＋三维姿态同步"链路的
 *        领域侧黄金演练；全链纯只读（结构性零修订——AT-04）。
 */
TEST(DynDyn08Chain, PeakLocateDrivesReplayPoseChain)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-08"}, std::vector<std::string>{"AT-04"});
    const DynamicsSeries series = makeGoldenSeries("d08-chain");

    // ① 曲线联动数据面（show-curves——面板五通道渲染源）。
    const DynamicsCurveProjector curveProjector;
    const CurveProjection curves = curveProjector.projectCurves(series);
    ASSERT_EQ(curves.joints.size(), 2u);

    // ② 峰值定位（locate-peak）：关节 0 速度峰值→游标跳转时刻 t=2 s。
    const DynamicsEnvelopeCalculator calculator;
    const std::vector<PeakRecord> peaks = calculator.computePeaks(series);
    const DynamicsPeakLocator locator;
    const std::optional<PeakRecord> hit = locator.locate(series, peaks, 0, 2);
    ASSERT_TRUE(hit.has_value());
    ASSERT_DOUBLE_EQ(hit->tPeakS, 2.0);

    // ③ 三维姿态同步（replay-at 数据面）：按峰值时刻查关节状态——
    //    精确命中帧点，q 驱动数据在手（FK 归 runtime，本域零姿态计算）。
    const DynamicsReplayProjector replayProjector;
    const ReplayData replay = replayProjector.buildReplayData(series);
    const std::optional<ReplaySample> pose = replayProjector.sampleAt(replay, hit->tPeakS);
    ASSERT_TRUE(pose.has_value());
    EXPECT_TRUE(pose->exact);
    ASSERT_EQ(pose->joints.size(), 2u);
    expectNearRel(pose->joints[0].q, kQ0[2], "峰值时刻 J0 q（rad）");
    expectNearRel(pose->joints[1].q, kQ1[2], "峰值时刻 J1 q（m）");

    // ④ 命令面零修订（回放驱动经 replay-at 受理——AT-04 机器断言）。
    const DynamicsCommandHandler handler;
    CommandPayload payload;
    payload.commandToken = std::string{sdurws::ird::dynamics::kCmdReplayAt};
    payload.replayTimeS = hit->tPeakS;
    const CommandOutcome out = handler.handle(sdurws::ird::dynamics::kCmdReplayAt, payload);
    EXPECT_TRUE(out.accepted);
    EXPECT_FALSE(out.producesRevision);  // 零修订——链路终点语义钉扎
    EXPECT_FALSE(out.invalidatesResults);
}
