/**
 * @file   DynPluginPanelTest.cpp
 * @brief  WP-17-T09 用例组——界面链路模型层（DynPanelWorkflow/
 *         DynPanelCurves/DynPanelReplay/DynPanelCatalog/DynPanelText/
 *         DynPanelGui 六组）：就绪投影合成、会话命令受理与零修订契约、
 *         曲线通道行集黄金、峰值定位流、回放查表流、装配登记面值断言
 *         与文案解析流（任务契约 acceptance 1/2 的执行证明面）。
 *
 * 设计依据：
 *   - units/dynamics.md §9.5（UI 协作五命令；"UI 线程不得执行动力学
 *     计算"红线；投影行七态素材）、§10.7（命令适配器零计算零修订）、
 *     §11.5（GUI 手动点验流程——本目标零 GUI 呈现用例，harness 通道
 *     承载，envUnavailable 如实登记）、§4.5（量纲类型化）
 *   - 先例：kinematics/test/PluginPanelTest.cpp（模型层范式——不启动
 *     GUI；GUI 呈现另行 envUnavailable 登记——WP-15-T12）；本单元
 *     test/ReplayCurveTest.cpp（黄金序列构造——TwoLinkFixture 夹具）
 *   - 需求 DYN-08、UX-02（工程用语/零哈希进用户文本）、UX-10（七态
 *     素材呈现）、AT-04（预览类交互零修订——机器断言）；任务契约
 *     tasks/foundation/WP-17-T09.json acceptance 1/2
 *
 * 测试范围声明：GUI 呈现（widget 渲染/交互点击）不在本目标——无人值
 * 守门禁不做 GUI 运行验证（既有环境事实），呈现归
 * sdurws_ird_dynamics_app harness 的手动验证留痕（§11.5），本目标内以
 * GuiPresentation 用例 envUnavailable 登记（AGENTS §4.2）。插件面零
 * 计算红线（acceptance 2 的词表扫描半区）归契约测试
 * DynamicsPluginAssemblyContractTest（全文扫描 plugin/＋assembly/）。
 *
 * 数值对照口径（附录 D C7）：黄金值全部手工按投影定义算出（投影＝
 * 样本行原值直拷或相邻样本线性混合，无浮点递推——期望值一次线性式，
 * 绝对容差 1e-12）；产品代码零自设阈值（P-DYN-9 不因本任务引入）。
 */

#include <sdurws/ird/dynamics/Commands.hpp>
#include <sdurws/ird/dynamics/DynTypes.hpp>
#include <sdurws/ird/dynamics/DynamicsPluginAssembly.hpp>
#include <sdurws/ird/dynamics/Envelope.hpp>
#include <sdurws/ird/dynamics/Replay.hpp>
#include <sdurws/ird/dynamics/SeriesBuilder.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "DynPanelCommandCatalog.hpp" // 命令目录/域注册键（plugin 私有头）
#include "DynPanelModel.hpp"          // L-D1~L-D6 模型层流（被测主面）
#include "DynPanelModule.hpp"         // DynPanelModule（缝/会话态归属——完整型）
#include "DynPanelTypes.hpp"          // 服务缝/会话态
#include "TwoLinkFixture.hpp"         // idFrom/digestOf——确定性 id/摘要派生

using namespace sdurws::ird::dynamics::testfixture;

using sdurws::ird::core::ContentIdentity;
using sdurws::ird::core::ObjectId;
using sdurws::ird::dynamics::CommandOutcome;
using sdurws::ird::dynamics::CommandPayload;
using sdurws::ird::dynamics::CurveProjection;
using sdurws::ird::dynamics::DynChannelId;
using sdurws::ird::dynamics::DynChannelRow;
using sdurws::ird::dynamics::DynCommandRecord;
using sdurws::ird::dynamics::DynJointType;
using sdurws::ird::dynamics::DynPeakJump;
using sdurws::ird::dynamics::DynPanelModule;
using sdurws::ird::dynamics::DynPanelServices;
using sdurws::ird::dynamics::DynReadinessRow;
using sdurws::ird::dynamics::DynamicsCurveProjector;
using sdurws::ird::dynamics::DynamicsEnvelopeCalculator;
using sdurws::ird::dynamics::DynamicsPeakLocator;
using sdurws::ird::dynamics::DynamicsPluginAssembly;
using sdurws::ird::dynamics::DynamicsReplayProjector;
using sdurws::ird::dynamics::DynamicsSample;
using sdurws::ird::dynamics::DynamicsSeries;
using sdurws::ird::dynamics::DynamicsSeriesBuilder;
using sdurws::ird::dynamics::DynamicsValidity;
using sdurws::ird::dynamics::JointCurves;
using sdurws::ird::dynamics::PeakRecord;
using sdurws::ird::dynamics::ReplayData;
using sdurws::ird::dynamics::ReplaySample;
using sdurws::ird::dynamics::SampleNumericState;
using sdurws::ird::dynamics::SeriesIdentity;
// 模型层流与词表（被测主面——具名引入，与被测头同一命名空间可读性）。
using sdurws::ird::dynamics::DynModuleSessionState;
using sdurws::ird::dynamics::kCommandTokens;
using sdurws::ird::dynamics::kCmdAnalyze;
using sdurws::ird::dynamics::readinessProjection;
using sdurws::ird::dynamics::applySessionCommand;
using sdurws::ird::dynamics::curveChannelRows;
using sdurws::ird::dynamics::locatePeakJump;
using sdurws::ird::dynamics::replaySampleAt;
using sdurws::ird::dynamics::resolvePanelText;

namespace {

// =====================================================================
// 黄金序列构造（两关节×三时刻——与 ReplayCurveTest 同款手工黄金；
// 解析式逐值可手算，插值中点为一次线性式）。
// 时间轴 t ∈ {0, 1, 2} s；段结构 t<1 归段 0、t≥1 归段 1：
//   关节 0（Revolute，N·m）：q=t²、q̇=2t、q̈=2、τ=10−t、P=10t
//   关节 1（Prismatic，N）： q=1−t、q̇=−1、q̈=0、τ=−5−t、P=−3t
// =====================================================================

/// 黄金时刻表，单位 s。
constexpr double kGoldenT[3] = {0.0, 1.0, 2.0};
/// 时刻所属段（t<1→0，t≥1→1）。
constexpr std::uint32_t kGoldenSeg[3] = {0, 1, 1};
/// 黄金时刻数。
constexpr std::size_t kGoldenSteps = 3;

/// 样本行构造助手（同 ReplayCurveTest——五分项字段置 0 自洽：投影/
/// 行集只消费 τ_total 与运动学通道）。
DynamicsSample goldenRow(std::size_t step, std::uint32_t joint)
{
    DynamicsSample r;
    r.t = kGoldenT[step];                              // s
    r.segmentIndex = kGoldenSeg[step];                 // 所在轨迹段（0 基）
    r.conditionId = idFrom<ObjectId>("p09-cond");
    r.jointIndex = joint;
    r.jointObjectId = joint == 0 ? idFrom<ObjectId>("p09-j0")
                                 : idFrom<ObjectId>("p09-j1");
    r.jointType = joint == 0 ? DynJointType::Revolute : DynJointType::Prismatic;
    r.q = joint == 0 ? kGoldenT[step] * kGoldenT[step] : 1.0 - kGoldenT[step];
    r.qd = joint == 0 ? 2.0 * kGoldenT[step] : -1.0;
    r.qdd = joint == 0 ? 2.0 : 0.0;
    r.tauGravity = 0.0;
    r.tauInertia = 0.0;
    r.tauCoriolisCentrifugal = 0.0;
    r.tauFriction = 0.0;
    r.tauExternal = 0.0;
    r.tauTotal = joint == 0 ? 10.0 - kGoldenT[step] : -5.0 - kGoldenT[step];
    r.mechanicalPower = joint == 0 ? 10.0 * kGoldenT[step] : -3.0 * kGoldenT[step];
    r.energyIntegralJ = 0.0;
    r.payloadVariantIndex = 0;
    r.toolObjectId = ObjectId{};
    r.numericState = SampleNumericState::Ok;
    return r;
}

/// 序列身份块构造助手（同 ReplayCurveTest——确定性 id/摘要派生）。
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

/// 组装黄金序列（行序 (t, jointIndex)——构建器冻结）。
DynamicsSeries makeGoldenSeries(const std::string& tag)
{
    DynamicsSeriesBuilder b;
    for (std::size_t s = 0; s < kGoldenSteps; ++s) {
        b.addSample(goldenRow(s, 0));
        b.addSample(goldenRow(s, 1));
    }
    return b.finalize(makeIdentity(tag, kGoldenSteps));
}

/// 黄金投影（曲线联动数据面——行集/缝用例共用）。
CurveProjection makeGoldenProjection(const std::string& tag)
{
    return DynamicsCurveProjector{}.projectCurves(makeGoldenSeries(tag));
}

/// 黄金回放数据（replay-at 数据面）。
ReplayData makeGoldenReplay(const std::string& tag)
{
    return DynamicsReplayProjector{}.buildReplayData(makeGoldenSeries(tag));
}

/// 黄金峰值行集（locate-peak 缝的组装侧素材——真实统计器产出）。
std::vector<PeakRecord> makeGoldenPeaks(const std::string& tag)
{
    return DynamicsEnvelopeCalculator{}.computePeaks(makeGoldenSeries(tag));
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
// DynPanelWorkflow 组——L-D1 就绪投影＋L-D3 会话命令受理流。
// =====================================================================

/**
 * @brief L-D1：会话事实→投影行逐字段透传（UX-10 七态素材面）；缺省
 *        态不伪造可行性（inputComplete=false/无判定/无在途）。
 */
TEST(DynPanelWorkflow, ReadinessProjectionTransfersSessionFacts_WP17T09_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-10"},
                  std::vector<std::string>{});

    // 缺省会话（零事实注入）→投影行如实呈现"无判定/不完整"——不伪造。
    DynPanelModule module;
    const DynReadinessRow empty = readinessProjection(module.session);
    EXPECT_EQ(empty.domainKey, "dynamics") << "域注册键＝§6.5 词表值";
    EXPECT_EQ(empty.verdict, sdurws::ird::core::EngineeringStatus::NotApplicable)
        << "无判定＝NotApplicable（不伪造可行性）";
    EXPECT_FALSE(empty.inputComplete);
    EXPECT_TRUE(empty.missingItemKeys.empty());
    EXPECT_FALSE(empty.hasActiveTask);

    // 事实注入→逐字段透传（判定权威在域就绪校验——投影零判定）。
    module.session.epoch = 7;
    module.session.inputComplete = true;
    module.session.hasActiveTask = true;
    module.session.verdict =
        sdurws::ird::core::EngineeringStatus::DataInsufficient;
    module.session.missingItemKeys = {"panel.dynamics.missing.friction"};
    const DynReadinessRow row = readinessProjection(module.session);
    EXPECT_EQ(row.verdict, sdurws::ird::core::EngineeringStatus::DataInsufficient);
    EXPECT_TRUE(row.inputComplete);
    EXPECT_TRUE(row.hasActiveTask);
    ASSERT_EQ(row.missingItemKeys.size(), 1u);
    EXPECT_EQ(row.missingItemKeys[0], "panel.dynamics.missing.friction");
}

/**
 * @brief L-D3：五命令全受理＋零修订契约逐条断言（AT-04 机器断言面——
 *        受理结果的 producesRevision 恒 false；四语义位＝
 *        kReplaySessionContract）；会话记录缓冲追加。
 */
TEST(DynPanelWorkflow, AcceptsAllFiveCommandsWithZeroRevision_WP17T09_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02", "DYN-08"},
                  std::vector<std::string>{"AT-04"});

    DynPanelModule module;
    for (std::string_view token : kCommandTokens) {
        // 五命令逐条受理（词表全量——§9.5 表行序）。
        CommandPayload payload;
        payload.commandToken = std::string(token);  // 交叉校验位一致
        const CommandOutcome outcome =
            applySessionCommand(module.session, token, payload);
        EXPECT_TRUE(outcome.accepted) << "token 应受理: " << token;
        // 零修订契约四位（受理时＝kReplaySessionContract 逐位——AT-04）。
        EXPECT_TRUE(outcome.sessionStateOnly) << token;
        EXPECT_FALSE(outcome.writesDesignModel) << token;
        EXPECT_FALSE(outcome.producesRevision) << token;
        EXPECT_FALSE(outcome.invalidatesResults) << token;
        EXPECT_TRUE(outcome.rejectionToken.empty()) << token;
        // 会话记录追加（呈现史——producesRevision 逐条 false）。
        ASSERT_FALSE(module.session.recentCommands.empty());
        const DynCommandRecord& record = module.session.recentCommands.back();
        EXPECT_EQ(record.commandToken, std::string(token));
        EXPECT_TRUE(record.accepted);
        EXPECT_FALSE(record.producesRevision)
            << "AT-04：记录的修订位逐条恒 false: " << token;
    }
    EXPECT_EQ(module.session.recentCommands.size(), 5u);
}

/**
 * @brief L-D3：表外 token 拒绝（用户可见不受理——非异常）＋负载错配
 *        拒绝（§10.7 受理语义透传）＋记录缓冲容量截断（呈现语义）。
 */
TEST(DynPanelWorkflow, RejectsUnknownTokenAndTruncatesRecordBuffer_WP17T09_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-10"},
                  std::vector<std::string>{"AT-04"});

    DynPanelModule module;
    // 表外 token：拒绝＋rejectionToken="unknown-token"（§10.7 词表语义）。
    const CommandPayload empty;
    const CommandOutcome unknown =
        applySessionCommand(module.session, "dynamics.not-a-command", empty);
    EXPECT_FALSE(unknown.accepted);
    EXPECT_EQ(unknown.rejectionToken, "unknown-token");
    EXPECT_FALSE(unknown.producesRevision) << "未受理亦零修订位";

    // 负载错配：payload 交叉校验位与命令不一致→拒绝。
    CommandPayload mismatched;
    mismatched.commandToken = "dynamics.analyze";
    const CommandOutcome mismatch = applySessionCommand(
        module.session, "dynamics.show-curves", mismatched);
    EXPECT_FALSE(mismatch.accepted);
    EXPECT_EQ(mismatch.rejectionToken, "payload-token-mismatch");

    // 缓冲截断：超过 8 条从头丢最旧（呈现缓冲语义——非业务阈值）。
    for (int i = 0; i < 12; ++i) {
        applySessionCommand(module.session, kCmdAnalyze, CommandPayload{});
    }
    EXPECT_EQ(module.session.recentCommands.size(),
              sdurws::ird::dynamics::kDynCommandRecordCapacity);
    // 最旧的拒绝记录已被挤出（先 2 条拒绝＋12 条受理——尾 8 条全受理）。
    for (const DynCommandRecord& record : module.session.recentCommands) {
        EXPECT_EQ(record.commandToken, "dynamics.analyze");
    }
}

// =====================================================================
// DynPanelCurves 组——L-D2 曲线通道行集＋L-D4 峰值定位流。
// =====================================================================

/**
 * @brief L-D2：黄金投影→通道行集黄金（2 关节×5 通道＝10 行；行序
 *        jointIndex×通道序；量纲标签按关节型类型化——DYN-03 红线）。
 */
TEST(DynPanelCurves, ChannelRowsGoldenTwoJointsFiveChannels_WP17T09_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-08", "UX-10"},
                  std::vector<std::string>{});

    const CurveProjection projection = makeGoldenProjection("p09-rows");
    const std::vector<DynChannelRow> rows = curveChannelRows(projection);

    // 行数与行序（jointIndex 升序×通道序——稳定序 NFR-COR-02）。
    ASSERT_EQ(rows.size(), 10u);  // 2 关节×5 通道
    for (std::size_t i = 0; i < rows.size(); ++i) {
        EXPECT_EQ(rows[i].jointIndex, i / 5u) << "行序＝关节×通道";
        EXPECT_EQ(rows[i].channelId, static_cast<int>(i % 5));
    }

    // 关节 0（Revolute）量纲标签——角度系＋N·m（类型化红线呈现面）。
    EXPECT_EQ(rows[0].unitToken, "rad");
    EXPECT_EQ(rows[1].unitToken, "rad/s");
    EXPECT_EQ(rows[2].unitToken, "rad/s^2");
    EXPECT_EQ(rows[3].unitToken, "N·m");
    EXPECT_EQ(rows[4].unitToken, "W");
    // 关节 1（Prismatic）量纲标签——长度系＋N。
    EXPECT_EQ(rows[5].unitToken, "m");
    EXPECT_EQ(rows[6].unitToken, "m/s");
    EXPECT_EQ(rows[7].unitToken, "m/s^2");
    EXPECT_EQ(rows[8].unitToken, "N");
    EXPECT_EQ(rows[9].unitToken, "W");

    // 点数＝黄金 3 刻度；通道键词表抽查（首/尾）。
    for (const DynChannelRow& row : rows) {
        EXPECT_EQ(row.pointCount, kGoldenSteps) << "通道点数＝Ok 行数";
    }
    EXPECT_EQ(rows[0].channelKey, "position");
    EXPECT_EQ(rows[9].channelKey, "mechanical-power");
}

/**
 * @brief L-D2：Partial 投影的剔除计数透传（UX-10"数据不足"标注素材）
 *        与空投影的空行集（Empty 显式语义——不伪造行）。
 */
TEST(DynPanelCurves, ChannelRowsCarryNonOkCountAndEmptySemantics_WP17T09_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-10", "DYN-08"},
                  std::vector<std::string>{});

    // 关节 1 的 t=1 行置非 Ok（评估器样本级标记形态）→投影 Partial。
    DynamicsSeriesBuilder b;
    for (std::size_t s = 0; s < kGoldenSteps; ++s) {
        b.addSample(goldenRow(s, 0));
        DynamicsSample row = goldenRow(s, 1);
        if (s == 1) {
            row.numericState = SampleNumericState::NonFiniteInput;
            row.qd = std::numeric_limits<double>::quiet_NaN();
        }
        b.addSample(row);
    }
    const DynamicsSeries series = b.finalize(makeIdentity("p09-rows-nan", kGoldenSteps));
    const CurveProjection partial =
        DynamicsCurveProjector{}.projectCurves(series);
    const std::vector<DynChannelRow> rows = curveChannelRows(partial);
    ASSERT_EQ(rows.size(), 10u);
    // 关节 0 全 Ok（行 0..4：nonOkCount=0、3 点）；关节 1 剔 1 行（行
    // 5..9：nonOkCount=1、2 点）——剔除计数逐关节透传（UX-10 素材）。
    for (int c = 0; c < 5; ++c) {
        EXPECT_EQ(rows[static_cast<std::size_t>(c)].nonOkCount, 0u);
        EXPECT_EQ(rows[static_cast<std::size_t>(c)].pointCount, 3u);
        EXPECT_EQ(rows[static_cast<std::size_t>(5 + c)].nonOkCount, 1u);
        EXPECT_EQ(rows[static_cast<std::size_t>(5 + c)].pointCount, 2u);
    }

    // 空投影（空序列）→空行集（Empty 显式语义）。
    const DynamicsSeries emptySeries =
        DynamicsSeriesBuilder{}.finalize(makeIdentity("p09-rows-empty", 0));
    const CurveProjection emptyProj =
        DynamicsCurveProjector{}.projectCurves(emptySeries);
    EXPECT_TRUE(curveChannelRows(emptyProj).empty()) << "空投影→空行集";
}

/**
 * @brief L-D4：峰值定位流端到端（真实统计行集→缝翻译→跳转值黄金）＋
 *        三态空态显式区分（未装配/无峰）。
 */
TEST(DynPanelCurves, PeakJumpFlowDrivesCursorTargetGolden_WP17T09_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-08"},
                  std::vector<std::string>{});

    // 真实统计行集（T04 computePeaks）＋缝组装侧翻译（harness/宿主职责
    // 的同构演示——黄金对账：关节 0 力矩正向峰值＝τ=10−t 的 t=0 端，
    // 值 10、窗 [0,0]；token 序 0＝力矩正）。
    const DynamicsSeries series = makeGoldenSeries("p09-peak");
    const std::vector<PeakRecord> peaks = makeGoldenPeaks("p09-peak");
    DynPanelServices services;
    services.peakLocate = [&series, &peaks](std::uint32_t jointIndex,
                                            int tokenIndex)
        -> std::optional<DynPeakJump> {
        const std::optional<PeakRecord> record =
            DynamicsPeakLocator{}.locate(series, peaks, jointIndex, tokenIndex);
        if (!record.has_value()) {
            return std::nullopt;
        }
        DynPeakJump jump;
        jump.jointIndex = jointIndex;
        jump.tokenIndex = tokenIndex;
        jump.value = record->value;
        jump.tPeakS = record->tPeakS;
        jump.segmentIndex = record->segmentIndex;
        return jump;
    };

    std::string reason = "seeded";
    const std::optional<DynPeakJump> jump = locatePeakJump(services, 0, 0, &reason);
    ASSERT_TRUE(jump.has_value()) << "关节 0 token 0 应有峰值";
    EXPECT_TRUE(reason.empty()) << "受理＝空因";
    expectNearRel(jump->tPeakS, 0.0, "关节 0 力矩正峰时刻（τ=10−t 在 t=0）");
    expectNearRel(jump->value, 10.0, "峰值值（N·m）");
    EXPECT_EQ(jump->segmentIndex, 0u);

    // 关节 1 力矩反向峰（τ=−5−t 全负——token 1＝反向幅值 τ_max⁻＝
    // max(−τ)=5+t，峰在 t=2、幅值 7、段 1）——缝翻译对账第二锚。
    const std::optional<DynPeakJump> j1 = locatePeakJump(services, 1, 1, &reason);
    ASSERT_TRUE(j1.has_value());
    expectNearRel(j1->tPeakS, 2.0, "关节 1 力矩反峰时刻");
    expectNearRel(j1->value, 7.0, "反向幅值（N——token 1＝max(−τ)）");
    EXPECT_EQ(j1->segmentIndex, 1u);

    // 空态显式：缝未装配→nullopt＋"not-assembled"（与无峰区分）。
    DynPanelServices bare;
    reason = "seeded";
    EXPECT_FALSE(locatePeakJump(bare, 0, 0, &reason).has_value());
    EXPECT_EQ(reason, "not-assembled");
    // 缝返回空（越界关节——合法空态）→nullopt＋"no-peak"。
    DynPanelServices noPeak;
    noPeak.peakLocate = [](std::uint32_t, int) { return std::nullopt; };
    reason = "seeded";
    EXPECT_FALSE(locatePeakJump(noPeak, 99, 0, &reason).has_value());
    EXPECT_EQ(reason, "no-peak");
}

// =====================================================================
// DynPanelReplay 组——L-D5 回放查表流。
// =====================================================================

/**
 * @brief L-D5：按时刻插值查表黄金（中点 0.5 s＝相邻帧线性混合；精确
 *        命中＝帧原值直拷）＋越界空态（不外推——§4.3 纪律透传）。
 */
TEST(DynPanelReplay, ReplaySampleAtMidpointAndBoundaryGolden_WP17T09_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-08"},
                  std::vector<std::string>{"AT-04"});

    const ReplayData data = makeGoldenReplay("p09-replay");
    ASSERT_EQ(data.frames.size(), kGoldenSteps);

    // 精确命中 t=0（关节 0：q=0、q̇=0、τ=10、P=0——帧原值直拷）。
    const std::optional<ReplaySample> exact = replaySampleAt(data, 0.0);
    ASSERT_TRUE(exact.has_value());
    EXPECT_TRUE(exact->exact);
    EXPECT_EQ(exact->segmentIndex, 0u);
    ASSERT_EQ(exact->joints.size(), 2u);
    expectNearRel(exact->joints[0].q, 0.0, "t=0 关节 0 q");
    expectNearRel(exact->joints[0].generalizedForce, 10.0, "t=0 关节 0 τ");

    // 区间插值 t=0.5（关节 0：q=0.5〔帧值 0 与 1 的线性混合——插值点
    // 是相邻样本值的线性过渡，不是 q(t)=t² 的函数值 0.25〕、τ=9.5〔10
    // 与 9 的中点〕——一次线性式黄金）。
    const std::optional<ReplaySample> mid = replaySampleAt(data, 0.5);
    ASSERT_TRUE(mid.has_value());
    EXPECT_FALSE(mid->exact);
    expectNearRel(mid->joints[0].q, 0.5, "t=0.5 关节 0 q（帧值线性混合）");
    expectNearRel(mid->joints[0].generalizedForce, 9.5, "t=0.5 关节 0 τ（插值）");
    EXPECT_TRUE(mid->completeAllJoints) << "黄金数据全关节齐全";

    // 越界空态（不外推——呈现侧停在端点）。
    EXPECT_FALSE(replaySampleAt(data, -0.1).has_value());
    EXPECT_FALSE(replaySampleAt(data, 2.1).has_value());
}

/**
 * @brief L-D5：回放驱动的零修订语义（KIN-06/AT-04——查表纯函数、
 *        会话态零触：查询前后会话态逐字段不变）。
 */
TEST(DynPanelReplay, ReplayQueryLeavesSessionUntouched_WP17T09_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"DYN-08", "KIN-06"},
                  std::vector<std::string>{"AT-04"});

    DynPanelModule module;
    module.session.epoch = 3;
    module.session.inputComplete = true;
    module.session.hasActiveTask = false;
    module.session.recentCommands.push_back(
        DynCommandRecord{"dynamics.analyze", true, "", false});

    // 查询前快照（值拷贝——逐字段比对的基准）。
    const DynModuleSessionState before = module.session;

    // 回放查询（精确命中＋区间插值各一次——呈现驱动路径）。
    const ReplayData data = makeGoldenReplay("p09-replay-zero");
    ASSERT_TRUE(replaySampleAt(data, 0.0).has_value());
    ASSERT_TRUE(replaySampleAt(data, 0.5).has_value());

    // 会话态逐字段不变（零修订/零失效/零缓冲追加——查表纯函数）。
    EXPECT_EQ(module.session.epoch, before.epoch);
    EXPECT_EQ(module.session.writable, before.writable);
    EXPECT_EQ(module.session.inputComplete, before.inputComplete);
    EXPECT_EQ(module.session.hasActiveTask, before.hasActiveTask);
    EXPECT_EQ(module.session.verdict, before.verdict);
    EXPECT_EQ(module.session.recentCommands.size(),
              before.recentCommands.size());
}

// =====================================================================
// DynPanelCatalog 组——装配登记面（命令目录/域键/描述符/门面绑定）。
// =====================================================================

/**
 * @brief 装配面：五命令描述符与 Commands.hpp 词表逐位一致＋键族形态；
 *        域注册键词表值。
 */
TEST(DynPanelCatalog, DomainCommandsMatchTokenVocabulary_WP17T09_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02", "DYN-08"},
                  std::vector<std::string>{});

    const auto commands = sdurws::ird::dynamics::dynDomainCommands();
    ASSERT_EQ(commands.size(), kCommandTokens.size()) << "五命令全量";
    for (std::size_t i = 0; i < commands.size(); ++i) {
        EXPECT_EQ(commands[i].token, kCommandTokens[i])
            << "token 与词表逐位一致（行序＝§9.5 表行序）";
        // 键族形态：cmd.<token>.title（ui.md §3.5——派生规则单一）。
        EXPECT_EQ(commands[i].titleKey,
                  "cmd." + commands[i].token + ".title");
    }
    EXPECT_EQ(sdurws::ird::dynamics::dynReadinessDomainKey(), "dynamics");
}

/**
 * @brief 装配面：描述符承载完整登记面（T02 两字段逐字保留＋T09 挂位/
 *        域键/命令/面板记录）；pluginId 落于宿主白名单词表（ui.md
 *        §11.1 八 token——本测试自持词表常量对账，零 ui 头包含）。
 */
TEST(DynPanelCatalog, AssemblyDescriptorCarriesRegistrationFace_WP17T09_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02", "UX-10"},
                  std::vector<std::string>{});

    // 白名单词表（ui.md §11.1——编译期常量词表的测试侧对账锚；真实
    // 运行时载体归宿主装配层，本单元零 ui 编译边——P-DYN-8 缺口）。
    constexpr const char* kUiWhitelist[8] = {
        "modeling", "requirements", "kinematics", "trajectory",
        "dynamics", "selection", "optimization", "workflow"};

    const DynamicsPluginAssembly bundle =
        sdurws::ird::dynamics::createDynamicsPluginAssembly();
    // T02 登记值逐字保留（既有契约用例同口径）。
    EXPECT_EQ(bundle.descriptor.pluginId, "dynamics");
    EXPECT_EQ(bundle.descriptor.titleKey, "plugin.dynamics.title");
    bool whitelisted = false;
    for (const char* token : kUiWhitelist) {
        whitelisted = whitelisted || bundle.descriptor.pluginId == token;
    }
    EXPECT_TRUE(whitelisted) << "pluginId 落于 §11.1 白名单词表";

    // T09 挂位/域键/命令/面板面。
    EXPECT_EQ(bundle.descriptor.stageToken, "trajectory-dynamics");
    EXPECT_EQ(bundle.descriptor.readinessDomainKey, "dynamics");
    ASSERT_EQ(bundle.descriptor.commands.size(), 5u);
    EXPECT_EQ(bundle.descriptor.commands[0].token, "dynamics.analyze");
    EXPECT_EQ(bundle.descriptor.commands[4].token,
              "dynamics.export-curve-data");
    ASSERT_EQ(bundle.descriptor.panels.size(), 1u)
        << "工作流页＋曲线视图合一主面板（恰一条登记记录）";
    const auto& panel = bundle.descriptor.panels[0];
    EXPECT_EQ(panel.stageToken, "trajectory-dynamics");
    EXPECT_EQ(panel.titleKey, "plugin.dynamics.panel.workflow.title");
    EXPECT_FALSE(panel.advanced) << "主面板位（UX-04 非 advanced）";
    EXPECT_TRUE(static_cast<bool>(panel.factory)) << "工厂闭包非空";
}

/**
 * @brief 装配面：bind 系／setServices 转发到模块缝（缝注入的回读验证）
 *        ＋session() 访问与刷新转发（面板未创建＝空操作不崩溃）。
 */
TEST(DynPanelCatalog, AssemblyBindsDriveModuleServices_WP17T09_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02"},
                  std::vector<std::string>{});

    DynamicsPluginAssembly bundle =
        sdurws::ird::dynamics::createDynamicsPluginAssembly();
    ASSERT_NE(bundle.module(), nullptr);

    // 会话态注入（装配层路径）。
    bundle.session().epoch = 9;
    bundle.session().writable = false;
    EXPECT_EQ(bundle.session().epoch, 9u);

    // 服务缝整体注入＋三 bind（缝回读——转发面）。
    DynPanelServices services;
    int submitCount = 0;
    services.commandSubmit = [&submitCount](const std::string&) {
        ++submitCount;
    };
    services.textResolver = [](const std::string& key) {
        return "值:" + key;
    };
    bundle.setServices(services);
    bundle.bindCommandAvailability(
        [](const std::string& token) { return token == "dynamics.analyze"; });
    bundle.bindTextResolver(
        [](const std::string& key) { return "覆盖:" + key; });

    ASSERT_TRUE(static_cast<bool>(bundle.module()->services.commandSubmit));
    ASSERT_TRUE(static_cast<bool>(bundle.module()->services.textResolver));
    bundle.module()->services.commandSubmit("dynamics.analyze");
    EXPECT_EQ(submitCount, 1) << "提交缝经 bind 转发生效";
    EXPECT_EQ(bundle.module()->services.textResolver("k"), "覆盖:k")
        << "后绑定覆盖整体注入（bind 语义）";
    ASSERT_TRUE(static_cast<bool>(bundle.module()->services.commandAvailability));
    EXPECT_TRUE(bundle.module()->services.commandAvailability("dynamics.analyze"));
    EXPECT_FALSE(bundle.module()->services.commandAvailability("dynamics.show-curves"));

    // 会话刷新（面板未创建＝空操作——不崩溃）。
    bundle.refreshFromSession();
}

// =====================================================================
// DynPanelText 组——L-D6 文案解析流（UX-02 零哈希守卫）。
// =====================================================================

/**
 * @brief L-D6：缝解析＋空缝键名兜底＋哈希形态回退（UX-02"零哈希进
 *        用户文本"——64 位十六进制串按泄漏处置回退键名）。
 */
TEST(DynPanelText, ResolveTextFallsBackAndGuardsDigestLeak_WP17T09_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02"},
                  std::vector<std::string>{});

    // 缝解析原值。
    DynPanelServices services;
    services.textResolver = [](const std::string& key) {
        return key == "panel.dynamics.panel.workflow.title" ? std::string("工作流")
                                                            : key;
    };
    bool fellBack = false;
    EXPECT_EQ(resolvePanelText(services, "panel.dynamics.panel.workflow.title",
                               &fellBack),
              "工作流");
    EXPECT_FALSE(fellBack);

    // 空缝→键名兜底（开发态可见缺口——kinematics 同纪律）。
    DynPanelServices bare;
    fellBack = false;
    EXPECT_EQ(resolvePanelText(bare, "cmd.dynamics.analyze.title", &fellBack),
              "cmd.dynamics.analyze.title");
    EXPECT_TRUE(fellBack);

    // 解析结果哈希形态→回退键名（泄漏守卫）。
    DynPanelServices leaky;
    leaky.textResolver = [](const std::string&) {
        return std::string("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
    };
    fellBack = false;
    EXPECT_EQ(resolvePanelText(leaky, "panel.dynamics.panel.curves.title",
                               &fellBack),
              "panel.dynamics.panel.curves.title");
    EXPECT_TRUE(fellBack) << "哈希形态解析值不进用户文本";
}

// =====================================================================
// DynPanelGui 组——GUI 呈现边界（诚实登记：envUnavailable，非通过）。
// =====================================================================

/**
 * @brief GUI 呈现不在本目标（无人值守门禁不做 GUI 运行验证——既有环
 *        境事实）：widget 渲染/交互点击归 sdurws_ird_dynamics_app
 *        harness 手动验证通道（单元卡 §11.5 流程），本次未启动、未留
 *        截图——如实登记为环境不可用，不标注通过（AGENTS §4.2）。
 */
TEST(DynPanelGui, WidgetPresentationDeferredToHarnessEnvUnavailable)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02", "UX-10"},
                  std::vector<std::string>{});

    GTEST_SKIP()
        << "envUnavailable：无人值守门禁不做 GUI 运行验证——曲线视图/"
           "工作流页的呈现与交互由 sdurws_ird_dynamics_app harness 手动"
           "点验承载（units/dynamics.md §11.5 流程），本次未启动，"
           "留痕见 traceability/builds/wp17-t09/ 登记";
}
