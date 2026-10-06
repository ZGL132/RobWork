/**
 * @file   PluginPanelTest.cpp
 * @brief  kinematics 插件界面链路测试（模型层——QCoreApplication 级）——
 *         任务契约 WP-15-T12 acceptance 1~4 的具名自证面（L-K1~L-K12
 *         主链覆盖）。
 *
 * 设计依据：
 *   - units/kinematics.md §9.8（十二条数据流 L-K1~L-K12——每用例以
 *     L-Kx 具名；四面板消费契约；域命令清单七行八 id）、§3.2（插件链接
 *     面＝本计算库＋sdurws_ird_ui）、§12（本单元无草稿——buildDraftCommand
 *     恒 nullopt 的语义锚）；
 *   - 先例：modeling/test/PluginPanelTest.cpp（WP-13-T15——模型层测试
 *     范式：不启动 GUI；GUI 呈现另行 envUnavailable 登记）；
 *     kinematics/test/KinFkFixture.hpp（TestView/规范模型构造器）；
 *   - 需求 UX-04/05、NFR-PERF-01、KIN-06/AT-04、KIN-08/AT-04、KIN-12/
 *     AT-27、PM-07、SA-16；任务契约 tasks/foundation/WP-15-T12.json。
 *
 * 测试范围声明：GUI 呈现（widget 渲染/交互点击）不在本目标——呈现归
 * sdurws_ird_kinematics_app harness 的手动验证留痕（DTB §5.1 v0.20；
 * DoD 第 5 条），本目标内以 GuiPresentation 用例 envUnavailable 登记
 * （AGENTS §4.2"未执行的测试不得标注通过"）。widget 零业务判定的静态
 * 半区（禁 sort/线程原语扫描）归 PluginGraphContractTest。
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

#include <sdurws/ird/kinematics/Fk.hpp>        // FkEvaluator（真实产品实现）
#include <sdurws/ird/kinematics/Ik.hpp>        // IkSolver/IkRequest/IkOutcome
#include <sdurws/ird/kinematics/KinTypes.hpp>  // 值模型
#include <sdurws/ird/kinematics/SolutionSet.hpp>
#include <sdurws/ird/ui/ICommandRegistry.hpp>  // CommandDescriptor 冻结形状断言面
#include <sdurws/ird/ui/UiTypes.hpp>           // DomainReadinessItem/StageId
#include "plugin/KinPanelCommandCatalog.hpp"   // 命令目录/登记记录/就绪投影/只读门控
#include "plugin/KinPanelChannelTranslation.hpp"  // UI-T64——通道↔私有值翻译（单测面）
#include "plugin/KinPanelFlows.hpp"            // L-K2~L-K12 编排（被测主面）
#include "plugin/KinPanelModel.hpp"            // 行集投影（L-K1）
#include "plugin/KinPanelTypes.hpp"            // 会话态/服务缝
#include "plugin/KinPanelUnits.hpp"            // L-K8 单位重投影
#include "plugin/KinematicsUiModule.hpp"       // UI-T64——具体模块（services() 测试访问面）
#include <sdurws/ird/kinematics/KinematicsPluginAssembly.hpp>  // 装配契约头（assembly/ PUBLIC include 面——链 plugin 目标传播） // 装配门面（契约头——acceptance 3）
#include "KinFkFixture.hpp"                    // TestView/twoLinkModel（同目录夹具）

// ---- 使用声明（与被测头同一命名空间——用例可读性）--------------------
using namespace sdurws::ird;
using namespace sdurws::ird::kinematics;
using namespace sdurws::ird::kinematics::testfixture;   // KinFkFixture 夹具（TestView/twoLinkModel——testfixture 命名空间）

namespace {

// =====================================================================
// 测试替身与夹具（模型层——O-37 宿主注入形态的脚本化面）
// =====================================================================

/**
 * @brief 脚本化求解器替身（IIkSolver——返回预设 IkOutcome；调用计数供
 *        L-K8"零重算"断言；solveDelayMs 供内联耗时的确定性注入——L-K2
 *        超界分支触发，真实求解耗时不稳定故脚本化）。
 */
class ScriptedSolver final : public IIkSolver {
public:
    IkOutcome scripted;              ///< 预设产出（值——solve 恒返回其副本）
    mutable std::atomic<int> callCount{0};  ///< solve 调用计数（零重算断言锚）
    int solveDelayMs = 0;            ///< 脚本延迟 ms（超界路径的确定性触发）

    IkOutcome solve(const IkRequest& /*request*/) const override
    {
        ++callCount;
        if (solveDelayMs > 0) {
            // 脚本化延迟——内联耗时的确定性来源（仅超界用例注入）。
            std::this_thread::sleep_for(std::chrono::milliseconds(solveDelayMs));
        }
        return scripted;
    }
};

/**
 * @brief 脚本化设默认门面替身（IKinematicsCommandHandler——记录提交与
 *        回显脚本；两方法计数分离——L-K9 的经①端口断言锚）。
 */
class ScriptedCommandHandler final : public IKinematicsCommandHandler {
public:
    CommandSubmission nextTcp;       ///< setProjectDefaultTcp 的预设回显
    CommandSubmission nextDevice;    ///< setProjectDefaultDevice 的预设回显
    mutable int tcpCalls = 0;        ///< TCP 路径调用计数
    mutable int deviceCalls = 0;     ///< 设备路径调用计数

    CommandSubmission setProjectDefaultTcp(const TcpRef& tcp) override
    {
        ++tcpCalls;
        lastTcp = tcp;  // 记录入参（经①端口语义的透传断言）
        return nextTcp;
    }

    CommandSubmission setProjectDefaultDevice(const core::ObjectId& robotOid) override
    {
        ++deviceCalls;
        lastDeviceOid = robotOid;
        return nextDevice;
    }

    mutable TcpRef lastTcp;              ///< 最近 TCP 入参记录
    mutable core::ObjectId lastDeviceOid;///< 最近设备入参记录
};

/**
 * @brief 求解目标位姿构造（位置 xyz＋恒等旋转）。
 *
 * 旋转用 9 参内联构造——不实例化引用 rw 库符号（Rotation3D::identity）
 * 的 Transform3D 单参构造，避免测试可执行文件的冒烟链接期未解析符号
 * （core §3.2"冒烟不引用 rw 库符号"同款口径）。
 */
rw::math::Transform3D<double> makePoseTarget(double x, double y, double z)
{
    return rw::math::Transform3D<double>(
        rw::math::Vector3D<double>(x, y, z),
        rw::math::Rotation3D<double>(1.0, 0.0, 0.0,
                                     0.0, 1.0, 0.0,
                                     0.0, 0.0, 1.0));
}

/**
 * @brief 插件测试夹具（二连杆解析模型＋真实 FK/IK＋记录缝）。
 *
 * 会话基线纪律：AnalysisConfiguration 默认 seed=0 非法（I-KIN-4）——
 * 构造即置合法 seed（装配纪律的测试面同则）。
 */
struct Harness {
    runtime::CanonicalModel model;   // 规范模型（值持有——二连杆黄金算例）
    TestView view;                   // 宿主注入替身（IKinRuntimeView 最小实现）
    FkEvaluator fk;                  // 真实 FK 评估器（无状态产品实现）
    ScriptedSolver solver;           // 脚本化求解器（L-K2/L-K8 断言面）
    ScriptedCommandHandler handler;  // 脚本化设默认门面（L-K9）
    KinSessionPose sessionPose;      // 会话姿态（真实容器——KIN-06 唯一写点）
    KinModuleSessionState session;   // 插件会话态
    KinPanelServices services;       // 服务缝

    // ---- 记录缝计数（零修订/零重算断言锚）----
    int commandSubmitCount = 0;      ///< 命令提交出口计数（L-K4/L-K10 零修订）
    int persistCount = 0;            ///< 用户级配置保存缝计数（L-K7）
    std::string exportedPath;        ///< 导出写出路径记录（L-K10）
    std::string exportedContent;     ///< 导出内容记录（L-K10）

    Harness()
        : model(twoLinkModel()),
          view(model)
    {
        // 会话基线必须合法（默认 seed=0 非法——I-KIN-4；装配纪律同则）。
        session.savedConfig.seed = 7;
        session.savedConfig.regionBudget.seed = 7;
        services.modelView = &view;
        services.ikSolver = &solver;
        services.fkEvaluator = &fk;
        services.sessionPose = &sessionPose;
        services.commandHandler = &handler;
        services.commandSubmit = [this](const std::string&) { ++commandSubmitCount; };
        services.configPersist = [this](const AnalysisConfiguration&) {
            ++persistCount;
            return true;
        };
        services.exportWriter =
            [this](const std::string& path, const std::string& content) {
                exportedPath = path;
                exportedContent = content;
                return true;
            };
    }

    /// 预设求解产出（两解＋稳定排序可观察——L-K2/L-K1 的输入面）。
    void scriptTwoSolutions()
    {
        IkSolutionSet set;
        KinematicSolution a;  // 解 A：裕量小（worstBy 裕量序在前——视图序 0）
        a.q = {0.1, 0.2};
        a.minimumJointMargin = 0.1;
        a.manipulability = 1.0;
        a.conditionNumber = 2.0;
        a.positionResidual = 1e-7;
        a.signature = "sig-a";
        KinematicSolution b;  // 解 B：裕量大
        b.q = {0.3, -0.4};
        b.minimumJointMargin = 0.8;
        b.manipulability = 0.5;
        b.conditionNumber = 1.5;
        b.positionResidual = 2e-7;
        b.signature = "sig-b";
        // 输入序＝B 裕量大在后也须按四键排前——sortSolutions 在视图构造时
        // 执行，这里手动排好（视图契约"以已求解 SolutionSet 构造"）。
        set.solutions = {b, a};
        set.statistics.rawCount = 4;
        set.statistics.convergedCount = 2;
        set.statistics.dedupedCount = 2;
        set.statistics.filteredCount = 0;
        solver.scripted = IkOutcome{};
        solver.scripted.outcomeKind = IkOutcomeKind::SolutionsFound;
        solver.scripted.solutionSet = std::move(set);
    }
};

// =====================================================================
// acceptance 1：界面链路用例（L-K1~L-K12 主链覆盖——具名自证）
// =====================================================================

/// L-K1 选中联动：解表选中→SolutionRef→检查器行集（健康摘要/关节/碰撞）。
TEST(PluginPanelLinks, LK1_SolutionSelectionInspectorLink_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-04", "KIN-07"},
                  std::vector<std::string>{});
    Harness h;
    h.scriptTwoSolutions();
    h.solver.scripted.solutionSet.requestIdentity.snapshotId =
        core::ContentIdentity{};
    KinematicSolutionSet view(h.solver.scripted.solutionSet);

    // 解行集：sorted() 序两解、rank 即视图序（零自排——消费域设施）。
    const auto rows = solutionRowsFor(view, false, std::nullopt);
    ASSERT_EQ(rows.size(), 2U);
    EXPECT_EQ(rows[0].signature, "sig-b");  // 裕量大者四键序在前
    EXPECT_EQ(rows[1].signature, "sig-a");

    // 选中视图序 1（sig-a）→检查器行集非空且含裕量/碰撞行（联动数据）。
    const auto inspector = solutionInspectorRows(view, SolutionRef{1}, std::nullopt);
    ASSERT_FALSE(inspector.empty());
    bool hasMargin = false;
    bool hasCollision = false;
    for (const auto& r : inspector) {
        hasMargin = hasMargin || r.key == "insp-margin";
        hasCollision = hasCollision || r.key == "insp-collision";
    }
    EXPECT_TRUE(hasMargin);
    EXPECT_TRUE(hasCollision);

    // 越界指称＝空行集（不虚构行——陈旧指称的缺省面）。
    EXPECT_TRUE(solutionInspectorRows(view, SolutionRef{99}, std::nullopt).empty());
}

/// L-K2 单点求解（内联成功路径）：求解→解集入会话态→解表可投影。
TEST(PluginPanelLinks, LK2_SinglePointSolveInline_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-04", "NFR-PERF-01"},
                  std::vector<std::string>{"AT-34"});
    Harness h;
    h.scriptTwoSolutions();

    IkRequest request;  // 契约缺省（模型视图由编排函数绑定）
    const auto result = runSinglePointSolve(
        h.services, h.session,
        makePoseTarget(0.1, 0.0, 0.4),
        request);
    ASSERT_FALSE(result.deferredToBackground);   // 内联路径
    EXPECT_EQ(h.solver.callCount.load(), 1);
    ASSERT_TRUE(h.session.lastSessionSolutionSetView != nullptr);  // 会话态唯一写点
    EXPECT_EQ(h.session.lastSessionSolutionSetView->sorted().size(), 2U);
    EXPECT_TRUE(h.session.lastSessionSolutionSetView->sorted()[0].signature == "sig-b");

    // 会话级求解零命令提交（会话级≠正式评估——不写 results）。
    EXPECT_EQ(h.commandSubmitCount, 0);
}

/// L-K2 超界转后台：内联耗时超注入预算→自动经后台缝提交＋状态反馈。
TEST(PluginPanelLinks, LK2_InlineBudgetExceededDefersToBackground_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-PERF-01", "UX-04"},
                  std::vector<std::string>{});
    Harness h;
    h.scriptTwoSolutions();
    h.solver.solveDelayMs = 5;  // 脚本延迟——内联耗时确定性超预算
    int submitted = 0;
    KinBackgroundKind seenKind = KinBackgroundKind::TaskPointsBatch;
    h.services.backgroundSubmit =
        [&](const KinBackgroundRequest& r) {
            ++submitted;
            seenKind = r.kind;
            KinBackgroundAck ack;
            ack.accepted = true;
            ack.taskRef = "bg-1";
            return ack;
        };

    // 预算注入 0ms——确定性触发超界分支（产品缺省恒 kInlineBudgetMs）。
    const auto result = runSinglePointSolve(
        h.services, h.session,
        makePoseTarget(0.1, 0.0, 0.4),
        IkRequest{}, std::chrono::milliseconds{0});
    EXPECT_TRUE(result.deferredToBackground);       // 自动转后台
    EXPECT_EQ(submitted, 1);
    EXPECT_TRUE(seenKind == KinBackgroundKind::SessionSolve);  // 会话级转后台
    EXPECT_NE(result.statusText.find("超预算"), std::string::npos);  // 状态反馈
}

/// L-K3 批量验证提交-进度-取消：受理→任务投影行现取；完成回执呈现。
TEST(PluginPanelLinks, LK3_BatchSubmitAndTaskProjection_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-05", "KIN-03"},
                  std::vector<std::string>{"AT-34"});
    Harness h;
    KinBackgroundRequest captured;
    h.services.backgroundSubmit =
        [&](const KinBackgroundRequest& r) {
            captured = r;
            KinBackgroundAck ack;
            ack.accepted = true;
            ack.taskRef = "task-9";
            return ack;
        };
    // 任务投影行（ui ITaskPresentationModel 投影值——运行中/进度 40%）。
    h.services.taskRows = [] {
        KinTaskStatusRow row;
        row.taskRefText = "task-9";
        row.stateLabelKey = "state.running.label";
        row.percent = 40;
        return std::vector<KinTaskStatusRow>{row};
    };

    const std::string text = submitBatchValidation(h.services, h.session);
    EXPECT_NE(text.find("task-9"), std::string::npos);       // 受理回显任务引用
    EXPECT_TRUE(captured.kind == KinBackgroundKind::TaskPointsBatch);
    EXPECT_EQ(captured.epoch, h.session.epoch);              // 纪元随请求携带
    EXPECT_EQ(captured.configDigest,
              analysisConfigurationDigest(h.session.savedConfig));  // 配置绑定

    // 进度面：任务投影行现取（进度 40% 如实——回退值不出现在投影源）。
    const auto rows = h.services.taskRows();
    ASSERT_EQ(rows.size(), 1U);
    EXPECT_EQ(rows[0].percent.value_or(-1), 40);

    // 完成回执（正常路径——摘要原样呈现）。
    KinBackgroundResultNote note;
    note.acceptedEpoch = h.session.epoch;
    note.kind = KinBackgroundKind::TaskPointsBatch;
    note.summaryText = "42/60 点可达";
    EXPECT_EQ(noteBackgroundResult(h.session, note), "42/60 点可达");
}

/// L-K6 区域覆盖运行：提交受理→覆盖行集投影（降级/零样本标识直投）。
TEST(PluginPanelLinks, LK6_CoverageRunAndCoverageRows_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-05", "KIN-04"},
                  std::vector<std::string>{});
    Harness h;
    KinBackgroundKind seen = KinBackgroundKind::SessionSolve;
    h.services.backgroundSubmit =
        [&](const KinBackgroundRequest& r) {
            seen = r.kind;
            KinBackgroundAck ack;
            ack.accepted = true;
            ack.taskRef = "cov-1";
            return ack;
        };
    const std::string text = submitCoverageEvaluation(h.services, h.session);
    EXPECT_NE(text.find("cov-1"), std::string::npos);
    EXPECT_TRUE(seen == KinBackgroundKind::RegionCoverage);   // 覆盖类别正确

    // 覆盖行集（CoverageResult 直投——计数比文本＋降级/零样本标识）。
    CoverageResult result;
    result.position = CoverageTotals{100, 60, 40, 0, 0, 0};   // 60/100 可达
    result.positionDefined = true;
    result.orientation = CoverageTotals{50, 30, 10, 10, 0, 0};// 数据不足 10
    result.orientationDefined = true;
    result.downgraded = true;                                  // 降级标识
    const auto rows = coverageRows(result);
    ASSERT_EQ(rows.size(), 2U);
    EXPECT_EQ(rows[0].ratioText, "60/100");                    // 存在性口径
    EXPECT_EQ(rows[1].ratioText, "30/50");                     // 全局口径
    EXPECT_TRUE(rows[1].degraded);

    // 零样本轴：比率不存在（不虚构 0%——V-13 呈现半区）。
    CoverageResult empty;
    empty.position = CoverageTotals{};
    empty.positionDefined = false;
    const auto emptyRows = coverageRows(empty);
    EXPECT_TRUE(emptyRows[0].zeroSamples);
    EXPECT_NE(emptyRows[0].ratioText.find("零样本"), std::string::npos);
}

/// L-K4 会话姿态（双击回写/复位 Home）：零修订＋零命令提交（KIN-06/AT-04）。
TEST(PluginPanelLinks, LK4_SessionPoseWritebackZeroRevision_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-06"},
                  std::vector<std::string>{"AT-04"});
    Harness h;
    // 可视化点回写值（writebackOf 的同构值——q 为 rad|m 权威向量）。
    RenderPointWriteback wb;
    wb.q = {0.25, -0.5};
    wb.configurationSignature = "sig-wb";
    const std::string text = applyPoseWriteback(&h.sessionPose, wb);
    EXPECT_NE(text.find("零修订"), std::string::npos);        // 如实呈现
    ASSERT_TRUE(h.sessionPose.isSet());                        // 会话态已写入
    EXPECT_EQ(h.sessionPose.jointConfiguration(), (std::vector<double>{0.25, -0.5}));

    // 复位 Home（第三入口——覆写会话姿态）。
    const std::string homeText = resetSessionHome(&h.sessionPose, {0.0, 0.0});
    EXPECT_EQ(h.sessionPose.jointConfiguration(), (std::vector<double>{0.0, 0.0}));

    // 零修订：命令提交出口计数为零（结构上本流无提交通道——计数复核）。
    EXPECT_EQ(h.commandSubmitCount, 0);
}

/// L-K5 TCP 回填目标：会话态读→显示制式回填（未设置＝nullopt 不读值）。
TEST(PluginPanelLinks, LK5_TcpBackfillFromSession_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-04", "KIN-12"},
                  std::vector<std::string>{});
    Harness h;
    // 未设置：回退编辑器缺省值（不读值——KinSessionPose 契约）。
    EXPECT_FALSE(tcpBackfillText(&h.sessionPose, h.session, {0.1, 0.2}).has_value());

    h.sessionPose.setJointConfiguration({0.5, -0.25});
    // SI 直显（无投影）。
    const auto si = tcpBackfillText(&h.sessionPose, h.session, {0.5, -0.25});
    ASSERT_TRUE(si.has_value());
    EXPECT_NE(si->find("0.5"), std::string::npos);
    // deg 制式：0.5 rad → 28.6x°（显示投影——回填文本随制式变化）。
    h.session.displayUnits = tryFindKinDisplayUnits("mm", "deg");
    ASSERT_TRUE(h.session.displayUnits.has_value());
    const auto deg = tcpBackfillText(&h.sessionPose, h.session, {0.5, -0.25});
    ASSERT_TRUE(deg.has_value());
    EXPECT_NE(deg->find("28.6"), std::string::npos);
}

/// L-K7 配置修改提示：编辑→校验→依赖提示→保存；非法整批拒绝；零自动重算。
TEST(PluginPanelLinks, LK7_ConfigEditHintNoAutoRecompute_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-13"},
                  std::vector<std::string>{"AT-27"});
    Harness h;
    const AnalysisConfiguration before = h.session.savedConfig;

    // 合法变更（迭代上限 1→8；容差 1e-6→2e-6）→提示行含受影响评估键＋
    // 保存缝触发。
    const auto result = applyConfigurationEdit(
        h.session, before,
        {{"iteration-limit", 8.0}, {"position-residual-tolerance", 2e-6}},
        h.services.configPersist);
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(h.persistCount, 1);                              // 用户级保存触发
    EXPECT_EQ(h.session.savedConfig.iterationLimit, 8U);       // 基线前移
    ASSERT_FALSE(result.hintRows.empty());                     // 依赖提示非空
    bool hasAffectedKey = false;
    for (const auto& row : result.hintRows) {
        hasAffectedKey = hasAffectedKey
            || row.displayText.find("kin-task-point-ik") != std::string::npos;
    }
    EXPECT_TRUE(hasAffectedKey);                               // §9.5 通道键直投

    // 非法变更（seed=0——I-KIN-4 拒绝不静默替换）→整批拒绝零变更。
    const auto rejected = applyConfigurationEdit(
        h.session, h.session.savedConfig, {{"seed", 0.0}}, h.services.configPersist);
    EXPECT_FALSE(rejected.ok);
    EXPECT_EQ(h.session.savedConfig.iterationLimit, 8U);       // 原配置保持
    EXPECT_EQ(h.persistCount, 1);                              // 拒绝路径零保存

    // 零自动重算：全程求解器零调用（L-K7"不自动重算"的结构面复核）。
    EXPECT_EQ(h.solver.callCount.load(), 0);
}

/// L-K8 单位切换重投影：mm/deg 制式→行文本重建；零重算零修订（V-17）。
TEST(PluginPanelLinks, LK8_UnitSwitchReprojectionNoRecompute_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-12"},
                  std::vector<std::string>{"AT-27"});
    Harness h;
    // R1 冻结子集：mm/deg 合法；R2 token（inch）拒绝——词表封闭。
    const auto units = tryFindKinDisplayUnits("mm", "deg");
    ASSERT_TRUE(units.has_value());
    EXPECT_FALSE(tryFindKinDisplayUnits("inch", "deg").has_value());

    // 指标行重投影：SI 真值不变、显示文本随制式（0.5 m → 500 mm）。
    std::vector<KinNamedValueRow> rows;
    KinNamedValueRow x;
    x.key = "tcp-x";
    x.label = "TCP X";
    x.siValue = 0.5;
    x.unitToken = "m";
    x.displayText = formatKinQuantityText(0.5, "m", std::nullopt);
    rows.push_back(x);
    const auto reprojected = reprojectMetricRows(rows, units);
    ASSERT_EQ(reprojected.size(), 1U);
    EXPECT_DOUBLE_EQ(reprojected[0].siValue, 0.5);             // SI 权威不动
    EXPECT_NE(reprojected[0].displayText.find("500"), std::string::npos);

    // 零重算/零修订：切换后求解器零调用、命令出口零提交。
    EXPECT_EQ(h.solver.callCount.load(), 0);
    EXPECT_EQ(h.commandSubmitCount, 0);
}

/// L-K9 设默认 TCP/设备：经门面→①端口投影（新修订回执→依赖重算提示；
/// 只读拒绝；NotCommitted 诊断透传）。
TEST(PluginPanelLinks, LK9_SetDefaultViaGateway_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-14"},
                  std::vector<std::string>{"AT-27"});
    Harness h;
    // Committed 脚本（新修订回执）。
    CommandSubmission committed;
    committed.kind = CommandSubmission::Kind::Committed;
    committed.newRevision = core::RevisionId::fromCanonical("rev-00000000000000000000000000000002");
    h.handler.nextTcp = committed;
    const std::string tcpText = submitSetDefaultTcp(h.services, h.session, TcpRef{});
    EXPECT_NE(tcpText.find("rev-00000000000000000000000000000002"), std::string::npos);
    EXPECT_NE(tcpText.find("需重算"), std::string::npos);      // 依赖失效提示
    EXPECT_EQ(h.handler.tcpCalls, 1);                          // 经门面→①端口

    // NotCommitted 脚本（诊断透传——零修订）。
    CommandSubmission rejected;  // 缺省 NotCommitted
    rejected.diagnostics.push_back(core::DiagnosticRecord::make(
        core::DiagCode("KIN-NO-DEVICE"), std::nullopt, std::string("d"),
        std::nullopt, std::string("ctx"), std::string("cause"),
        std::string("action")));
    h.handler.nextDevice = rejected;
    const std::string deviceText = submitSetDefaultDevice(
        h.services, h.session, core::ObjectId{});
    EXPECT_NE(deviceText.find("KIN-NO-DEVICE"), std::string::npos);

    // 只读会话：写类命令拒绝（不触达门面）。
    h.session.writable = false;
    EXPECT_EQ(h.handler.tcpCalls, 1);
    const std::string denied = submitSetDefaultTcp(h.services, h.session, TcpRef{});
    EXPECT_NE(denied.find("只读"), std::string::npos);
    EXPECT_EQ(h.handler.tcpCalls, 1);                          // 门面零调用
}

/// L-K10 结果导出：会话解集→JSON/CSV 副本（io 缝写出）；零修订。
TEST(PluginPanelLinks, LK10_ExportJsonCsvNoRevision_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-08"},
                  std::vector<std::string>{"AT-04"});
    Harness h;
    // 无结果：如实说明（不虚构导出物）。
    EXPECT_NE(exportSessionResults(h.services, h.session, "a.json", false).find(
                  "无可导出"), std::string::npos);

    h.scriptTwoSolutions();
    // 会话解集入位（与 L-K2 同一写点——直接经编排函数建立）。
    (void)runSinglePointSolve(h.services, h.session,
                              makePoseTarget(0.1, 0.0, 0.4),
                              IkRequest{});
    const std::string jsonText =
        exportSessionResults(h.services, h.session, "kin.json", false);
    EXPECT_EQ(h.exportedPath, "kin.json");
    EXPECT_NE(h.exportedContent.find("snapshot"), std::string::npos);  // JSON 头
    EXPECT_NE(jsonText.find("零修订"), std::string::npos);            // AT-04

    const std::string csvText =
        exportSessionResults(h.services, h.session, "kin.csv", true);
    EXPECT_EQ(h.exportedPath, "kin.csv");
    EXPECT_FALSE(h.exportedContent.empty());

    // 零修订复核＋缝未装配降级（如实说明——不虚构写出）。
    EXPECT_EQ(h.commandSubmitCount, 0);
    h.services.exportWriter = nullptr;
    EXPECT_NE(exportSessionResults(h.services, h.session, "x.json", false).find(
                  "未装配"), std::string::npos);
}

/// L-K11 只读模式：写类命令禁用/提交拒绝；只读类命令（FK 预览/单位切换/
/// 导出/复位）保持可用。
TEST(PluginPanelLinks, LK11_ReadOnlyRejectsWriteCommands_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-05", "PM-07"},
                  std::vector<std::string>{});
    Harness h;
    h.session.writable = false;
    // §9.8 表 readOnlyAllowed=false 的四 id 全部禁用。
    EXPECT_FALSE(kinCommandEnabledInSession("kinematics.validate-task-points", false));
    EXPECT_FALSE(kinCommandEnabledInSession("kinematics.evaluate-coverage", false));
    EXPECT_FALSE(kinCommandEnabledInSession("kinematics.set-default-tcp", false));
    EXPECT_FALSE(kinCommandEnabledInSession("kinematics.set-default-device", false));
    // 只读类命令保持可用（会话级 FK 预览/单点 IK/导出/复位）。
    EXPECT_TRUE(kinCommandEnabledInSession("kinematics.analyze-pose", false));
    EXPECT_TRUE(kinCommandEnabledInSession("kinematics.solve-ik", false));
    EXPECT_TRUE(kinCommandEnabledInSession("kinematics.export-results", false));
    EXPECT_TRUE(kinCommandEnabledInSession("kinematics.reset-session-pose", false));

    // 提交拒绝（诊断语义状态行——批量/覆盖两路）。
    int submitted = 0;
    h.services.backgroundSubmit =
        [&submitted](const KinBackgroundRequest&) {
            ++submitted;
            return KinBackgroundAck{};
        };
    EXPECT_NE(submitBatchValidation(h.services, h.session).find("只读"),
              std::string::npos);
    EXPECT_NE(submitCoverageEvaluation(h.services, h.session).find("只读"),
              std::string::npos);
    EXPECT_EQ(submitted, 0);                                   // 缝零触达

    // 会话级求解（只读可用——FK 预览语义）与单位切换不受只读影响。
    h.scriptTwoSolutions();
    const auto solve = runSinglePointSolve(
        h.services, h.session,
        makePoseTarget(0.1, 0.0, 0.4),
        IkRequest{});
    EXPECT_FALSE(solve.deferredToBackground);
    EXPECT_EQ(h.solver.callCount.load(), 1);
}

/// L-K12 任务状态投影：中断如实呈现；迟到结果不进当前会话（纪元比对）。
TEST(PluginPanelLinks, LK12_TaskStatusProjectionAndStaleDrop_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-05", "NFR-REL-03"},
                  std::vector<std::string>{"AT-10", "AT-11"});
    Harness h;
    // 任务投影行（"已中断"如实——stateLabelKey 原样透传零改写）。
    h.services.taskRows = [] {
        KinTaskStatusRow row;
        row.taskRefText = "task-7";
        row.stateLabelKey = "state.interrupted.label";  // ui 词表原样
        row.interrupted = true;
        return std::vector<KinTaskStatusRow>{row};
    };
    const auto rows = h.services.taskRows();
    ASSERT_EQ(rows.size(), 1U);
    EXPECT_TRUE(rows[0].interrupted);
    EXPECT_EQ(rows[0].stateLabelKey, "state.interrupted.label");  // 零美化

    // 中断回执：状态行以"已中断"如实前缀（NFR-REL-03）。
    KinBackgroundResultNote interrupted;
    interrupted.acceptedEpoch = h.session.epoch;
    interrupted.interrupted = true;
    interrupted.summaryText = "30/60 点已计算";
    const std::string text = noteBackgroundResult(h.session, interrupted);
    EXPECT_EQ(text.find("已中断"), 0U);
    EXPECT_NE(text.find("30/60"), std::string::npos);

    // 迟到结果（纪元过期——提交后发生过会话推进）：丢弃不进当前会话。
    KinBackgroundResultNote stale;
    stale.acceptedEpoch = h.session.epoch - 1;
    stale.summaryText = "陈旧覆盖结果";
    const std::string dropped = noteBackgroundResult(h.session, stale);
    EXPECT_NE(dropped.find("迟到"), std::string::npos);
    EXPECT_EQ(dropped.find("陈旧覆盖结果"), std::string::npos);   // 摘要不进呈现
}

/// GUI 呈现（四面板 widget 渲染/交互点击）：仅登记不执行——GUI 环境的
/// 手动验证归 sdurws_ird_kinematics_app harness 留痕（DTB §5.1 v0.20/
/// DoD 第 5 条）；无界面测试目标不启动 GUI（modeling V-22 先例同款）。
TEST(PluginPanelLinks, KinGuiPresentationRegisteredNotExecuted_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-04", "UX-05"},
                  std::vector<std::string>{});
    sdurws::ird::testkit::report::setOutcome(
        sdurws::ird::testkit::report::Outcome::EnvUnavailable,
        "四面板 GUI 呈现与交互点验仅登记不执行（DTB §5.1 v0.20——GUI 手动"
        "验证通道归 sdurws_ird_kinematics_app harness 规程留痕；无界面测试"
        "目标不含 GUI 平台插件，testkit §6.7）");
    GTEST_SKIP() << "GUI 呈现仅登记（envUnavailable 如实登记——不标注通过）";
}

// =====================================================================
// acceptance 3：域命令注册（§9.8 清单七行八 id——装配描述符形状与词表）
// =====================================================================

/// 域命令清单：八 id 逐条对齐 §9.8 表（id/readOnlyAllowed/scope/SA-16）。
TEST(KinCommandCatalog, DomainCommandsMatchSection98Table_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-04", "SA-16"},
                  std::vector<std::string>{});
    const auto commands = kinematicsDomainCommands();
    ASSERT_EQ(commands.size(), 8U);  // 七行——set-default 同行两 id

    // 逐行对照表（id→readOnlyAllowed→scope——卡面权威）。
    struct Expectation {
        const char* id;
        bool readOnlyAllowed;
        ui::CommandScope scope;
    };
    const Expectation table[] = {
        {"kinematics.analyze-pose", true, ui::CommandScope::Session},
        {"kinematics.solve-ik", true, ui::CommandScope::Session},
        {"kinematics.validate-task-points", false, ui::CommandScope::Project},
        {"kinematics.evaluate-coverage", false, ui::CommandScope::Project},
        {"kinematics.set-default-tcp", false, ui::CommandScope::Project},
        {"kinematics.set-default-device", false, ui::CommandScope::Project},
        {"kinematics.export-results", true, ui::CommandScope::Project},
        {"kinematics.reset-session-pose", true, ui::CommandScope::Session},
    };
    ASSERT_EQ(commands.size(), sizeof(table) / sizeof(table[0]));
    for (std::size_t i = 0; i < commands.size(); ++i) {
        EXPECT_EQ(commands[i].id, table[i].id) << "行 " << i;
        EXPECT_EQ(commands[i].readOnlyAllowed, table[i].readOnlyAllowed) << table[i].id;
        EXPECT_TRUE(commands[i].scope == table[i].scope) << table[i].id;
        EXPECT_EQ(commands[i].ownerUnit, "kinematics");            // 白名单 token
        EXPECT_EQ(commands[i].titleKey,
                  std::string("cmd.") + table[i].id + ".title");   // §3.5 键约定
        EXPECT_TRUE(commands[i].category == ui::CommandCategory::Stage);
        EXPECT_FALSE(commands[i].defaultShortcut.has_value());     // SA-16：零私占
        EXPECT_TRUE(commands[i].bindable);                         // 用户级可绑
    }
    // 唯一性（重复 id＝注册边界拒绝面——此处清单即唯一产出点自证）。
    for (std::size_t i = 0; i < commands.size(); ++i) {
        for (std::size_t j = i + 1; j < commands.size(); ++j) {
            EXPECT_NE(commands[i].id, commands[j].id);
        }
    }
}

/// 词表分离：kinematics.* CommandId 与 project commandType（无点）零交叉。
TEST(KinCommandCatalog, CommandIdAndProjectTokenNamespacesDisjoint_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"SA-16"}, std::vector<std::string>{});
    const auto commands = kinematicsDomainCommands();
    const auto projectTokens = kinematicsProjectCommandTokens();
    for (const auto& c : commands) {
        for (const auto& t : projectTokens) {
            EXPECT_NE(c.id, t);  // 词表交叉即违约（两套命名空间不混用）
        }
    }
    EXPECT_EQ(projectTokens.size(), 1U);  // kFacadedModelingCommand 唯一值
}

/// 只读门控投影与写类判定一致性（L-K11 id 半区与 §9.8 表同源）。
TEST(KinCommandCatalog, WriteCommandPredicateMatchesReadOnlyAllowed_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-07"}, std::vector<std::string>{});
    for (const auto& c : kinematicsDomainCommands()) {
        EXPECT_EQ(isKinWriteCommand(c.id), !c.readOnlyAllowed) << c.id;
    }
    // 未知 id：非写且不可用（不虚构可达性）。
    EXPECT_FALSE(isKinWriteCommand("kinematics.not-a-command"));
    EXPECT_FALSE(kinCommandEnabledInSession("kinematics.not-a-command", true));
}

// =====================================================================
// acceptance 1/3：装配描述符形状（§10.9 冻结形状＋面板落位）
// =====================================================================

/// 装配描述符：pluginId/stages/capabilities/commands/panels 全形状断言。
TEST(KinPluginAssembly, DescriptorShapeAndPanelPlacement_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-04"}, std::vector<std::string>{});
    KinematicsPluginAssembly bundle = createKinematicsPluginAssembly();
    EXPECT_EQ(bundle.descriptor.pluginId, "kinematics");       // 白名单 token
    ASSERT_EQ(bundle.descriptor.stages.size(), 1U);
    EXPECT_TRUE(bundle.descriptor.stages[0] == ui::StageId::Kinematics);
    EXPECT_TRUE(bundle.descriptor.capabilities.providesStagePanel);
    EXPECT_TRUE(bundle.descriptor.capabilities.providesReadonlyProjection);
    EXPECT_TRUE(bundle.descriptor.capabilities.registersCommands);
    EXPECT_EQ(bundle.descriptor.commands.size(), 8U);          // §9.8 八命令

    // 面板落位：两条登记记录（主面板＝§9.8 行 1~4 合一 Tab 容器；求解
    // 配置高级面板＝行 5，advanced=true——UX-04 高级参数收拢）。
    ASSERT_EQ(bundle.descriptor.panels.size(), 2U);
    EXPECT_FALSE(bundle.descriptor.panels[0].advanced);
    EXPECT_TRUE(bundle.descriptor.panels[0].stage == ui::StageId::Kinematics);
    EXPECT_TRUE(bundle.descriptor.panels[1].advanced);
    EXPECT_EQ(bundle.descriptor.panels[1].titleKey,
              std::string("stage.kinematics.panel.solver-config.title"));

    // 登记记录值化清单：§9.8 面板表五行逐行（key/titleKey/advanced）。
    const KinPanelRegistration reg = kinematicsPanelRegistration();
    ASSERT_EQ(reg.panels.size(), 5U);
    EXPECT_EQ(reg.panels[0].key, "pose-metrics");
    EXPECT_EQ(reg.panels[1].key, "task-points");
    EXPECT_EQ(reg.panels[2].key, "region-coverage");
    EXPECT_EQ(reg.panels[3].key, "results-view");
    EXPECT_EQ(reg.panels[4].key, "solver-config");
    EXPECT_TRUE(reg.panels[4].advanced);
    EXPECT_FALSE(reg.panels[0].advanced);
}

/// 就绪投影（§6.5 汇聚源——P-UI-6 处置：冻结形状直投零判定）。
TEST(KinPluginAssembly, ReadinessProjectionDefaultsAndActiveTask_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02"}, std::vector<std::string>{});
    // 缺省行（输入不完整——不伪造可行性）。
    const auto empty = kinematicsReadinessProjection(false, false);
    ASSERT_EQ(empty.size(), 1U);
    EXPECT_EQ(empty[0].domainKey, "kinematics");
    EXPECT_TRUE(empty[0].verdict == core::EngineeringStatus::DataInsufficient);
    EXPECT_FALSE(empty[0].inputComplete);
    EXPECT_FALSE(empty[0].hasActiveTask);
    EXPECT_FALSE(empty[0].missingItemKeys.empty());            // 缺项键如实

    // 在途任务事实直投（hasActiveTask——汇聚呈现素材）。
    const auto active = kinematicsReadinessProjection(true, true);
    EXPECT_TRUE(active[0].inputComplete);
    EXPECT_TRUE(active[0].hasActiveTask);
    EXPECT_TRUE(active[0].missingItemKeys.empty());
}

// =====================================================================
// UI-T64——装配通道翻译与注入（F-490① 上游批：通道值面→私有缝翻译
// 单点＋门面 install/note 消费面；需求追溯＝KIN-07/KIN-04/L-K6/P-KIN-7）
// =====================================================================

/// 翻译函数逐字段单测（请求/回执/通知/任务点/任务状态——五方向全拷贝
/// 断言：任何一侧字段漂移都在翻译编译点或此处断言点暴露）。
TEST(KinAssemblyChannels, TranslationRoundTripFieldByField_UI_T64)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-07", "KIN-04"},
                  std::vector<std::string>{"AT-03"});

    // ---- 请求翻译：kind 三值逐一映射＋身份/纪元逐字段。
    KinChannelBackgroundRequest channelRequest;
    channelRequest.kind = KinChannelBackgroundKind::RegionCoverage;
    channelRequest.snapshotId = core::ContentIdentity::tryFromCanonical(
        "cid-1111111111111111111111111111111111111111111111111111111111111111")
                                    .value_or(core::ContentIdentity{});
    channelRequest.configDigest = core::ContentIdentity::tryFromCanonical(
        "cid-2222222222222222222222222222222222222222222222222222222222222222")
                                      .value_or(core::ContentIdentity{});
    channelRequest.epoch = 42;
    const KinBackgroundRequest privateRequest = toPrivateRequest(channelRequest);
    EXPECT_TRUE(privateRequest.kind == KinBackgroundKind::RegionCoverage);
    EXPECT_TRUE(privateRequest.snapshotId == channelRequest.snapshotId);
    EXPECT_TRUE(privateRequest.configDigest == channelRequest.configDigest);
    EXPECT_EQ(privateRequest.epoch, 42U);

    // ---- 回执翻译：受理/原因/任务引用三字段。
    KinChannelBackgroundAck channelAck;
    channelAck.accepted = true;
    channelAck.reason = "覆盖评估已受理";
    channelAck.taskRef = "kin-cov-42-1";
    const KinBackgroundAck privateAck = toPrivateAck(channelAck);
    EXPECT_TRUE(privateAck.accepted);
    EXPECT_EQ(privateAck.reason, "覆盖评估已受理");
    EXPECT_EQ(privateAck.taskRef, "kin-cov-42-1");

    // ---- 通知翻译：纪元/类别/中断/摘要四字段（迟到判定三要素）。
    KinChannelBackgroundResultNote channelNote;
    channelNote.acceptedEpoch = 42;
    channelNote.kind = KinChannelBackgroundKind::RegionCoverage;
    channelNote.interrupted = false;
    channelNote.summaryText = "覆盖评估完成：3/4 点可达（位置口径）";
    const KinBackgroundResultNote privateNote = toPrivateNote(channelNote);
    EXPECT_EQ(privateNote.acceptedEpoch, 42U);
    EXPECT_TRUE(privateNote.kind == KinBackgroundKind::RegionCoverage);
    EXPECT_FALSE(privateNote.interrupted);
    EXPECT_EQ(privateNote.summaryText, "覆盖评估完成：3/4 点可达（位置口径）");

    // ---- 任务点行翻译：六字段（含 optional 三态原样搬运）。
    KinChannelTaskPointRow channelPoint;
    channelPoint.pointOid = core::ObjectId::generate();
    channelPoint.label = "任务点 P1";
    channelPoint.enabled = false;
    channelPoint.outcomeState = std::string("reached");
    channelPoint.outcomeText = std::string("2 解/残差 0.1 mm");
    channelPoint.requiredCoverageDone = true;
    const KinTaskPointRow privatePoint = toPrivateTaskPoint(channelPoint);
    EXPECT_TRUE(privatePoint.pointOid == channelPoint.pointOid);
    EXPECT_EQ(privatePoint.label, "任务点 P1");
    EXPECT_FALSE(privatePoint.enabled);
    EXPECT_TRUE(privatePoint.outcomeState.has_value()
                && *privatePoint.outcomeState == "reached");
    EXPECT_TRUE(privatePoint.outcomeText.has_value()
                && *privatePoint.outcomeText == "2 解/残差 0.1 mm");
    EXPECT_TRUE(privatePoint.requiredCoverageDone.has_value()
                && *privatePoint.requiredCoverageDone);

    // ---- 任务状态行翻译：四字段。
    KinChannelTaskStatusRow channelRow;
    channelRow.taskRefText = "kin-cov-42-1";
    channelRow.stateLabelKey = "task.state.running";
    channelRow.interrupted = false;
    channelRow.percent = std::nullopt;
    const KinTaskStatusRow privateRow = toPrivateTaskStatus(channelRow);
    EXPECT_EQ(privateRow.taskRefText, "kin-cov-42-1");
    EXPECT_EQ(privateRow.stateLabelKey, "task.state.running");
    EXPECT_FALSE(privateRow.interrupted);
    EXPECT_FALSE(privateRow.percent.has_value());
}

/// 门面通道注入集成（installAssemblyChannels→模块缝回读翻译结果＋
/// noteAssemblyBackgroundResult 三态文案——迟到判定/中断如实/正常）。
TEST(KinAssemblyChannels, InstallChannelsAndNoteRoute_UI_T64)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-07", "L-K6"},
                  std::vector<std::string>{"AT-03"});

    // 装配门面（产品入口——真实模块＋真实描述符）。
    KinematicsPluginAssembly bundle = createKinematicsPluginAssembly();

    // 注入前：backgroundSubmit 缝为空（F-490① 登记的原始形态——诚实
    // 禁用"执行通道未装配"）。
    {
        KinematicsPluginAssembly probe = createKinematicsPluginAssembly();
        auto* probeModule = static_cast<KinematicsUiModule*>(probe.module.get());
        EXPECT_FALSE(probeModule->services().backgroundSubmit);
    }

    // ---- 通道注入（脚本化提交缝——受理透传断言面）。
    TestView view(twoLinkModel());
    KinChannelBackgroundAck scriptedAck;
    scriptedAck.accepted = true;
    scriptedAck.reason = "脚本受理";
    scriptedAck.taskRef = "kin-cov-1-1";
    KinematicsAssemblyChannels channels;
    channels.modelView = &view;
    channels.backgroundSubmit =
        [scriptedAck](const KinChannelBackgroundRequest& request) {
            // 通道请求字段透传断言（翻译闭包方向①：私有请求→通道请求）。
            EXPECT_TRUE(request.kind == KinChannelBackgroundKind::RegionCoverage);
            EXPECT_EQ(request.epoch, 7U);
            return scriptedAck;
        };
    bundle.installAssemblyChannels(channels);

    // 模块缝回读：提交缝非空且经翻译往返保真（私有请求进→通道请求出→
    // 通道回执回→私有回执出）；modelView 指针直传；未触缝（ikSolver）
    // 保持空——mergeAssemblyChannels 逐缝语义不误伤。
    auto* module = static_cast<KinematicsUiModule*>(bundle.module.get());
    ASSERT_TRUE(module->services().backgroundSubmit);
    KinBackgroundRequest privateRequest;
    privateRequest.kind = KinBackgroundKind::RegionCoverage;
    privateRequest.epoch = 7;
    const KinBackgroundAck ack =
        module->services().backgroundSubmit(privateRequest);
    EXPECT_TRUE(ack.accepted);
    EXPECT_EQ(ack.reason, "脚本受理");
    EXPECT_EQ(ack.taskRef, "kin-cov-1-1");
    EXPECT_EQ(module->services().modelView, &view);
    EXPECT_FALSE(module->services().ikSolver);

    // ---- 完成通知三态（门面 noteAssemblyBackgroundResult——经
    // KinPanelFlows::noteBackgroundResult 权威消费；模块内会话纪元经
    // bindSessionFacts 写入＝UI-T64 新门面方法的同用例消费）。
    kinematics::AnalysisConfiguration baseline;
    baseline.seed = 1;
    baseline.regionBudget.seed = 1;
    bundle.bindSessionFacts(core::ContentIdentity{}, 7, true, baseline);
    KinChannelBackgroundResultNote note;
    note.acceptedEpoch = 7;
    note.kind = KinChannelBackgroundKind::RegionCoverage;
    note.summaryText = "覆盖评估完成：3/4 点可达（位置口径）";
    // 正常态：纪元一致——原样摘要。
    EXPECT_EQ(bundle.noteAssemblyBackgroundResult(note),
              "覆盖评估完成：3/4 点可达（位置口径）");
    // 迟到态：纪元不符——丢弃并如实反馈（L-K12）。
    bundle.bindSessionFacts(core::ContentIdentity{}, 8, true, baseline);
    const std::string late = bundle.noteAssemblyBackgroundResult(note);
    EXPECT_NE(late.find("迟到结果已丢弃"), std::string::npos);
    // 中断态：纪元一致＋中断标记——"已中断"前缀（NFR-REL-03）。
    bundle.bindSessionFacts(core::ContentIdentity{}, 7, true, baseline);
    note.interrupted = true;
    const std::string interrupted = bundle.noteAssemblyBackgroundResult(note);
    EXPECT_NE(interrupted.find("已中断"), std::string::npos);
}

}  // namespace
