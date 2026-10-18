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

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
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
    // 逐轴类型表（KIN-P3/A05——二连杆全转动链，dofJointTypes 提取）。
    const auto rows = solutionRowsFor(view, false, dofJointTypes(h.view.model()),
                                      std::nullopt);
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

/// KIN-P2（审查 A08）——会话求解请求装配：从模型/会话/配置装配完整
/// 合法请求（修复前真实按钮以默认构造 IkRequest 提交——TCP 空/初值空/
/// referenceQ 与 intervals 维度 0，真实 IkSolver 校验必 fail-fast）。
/// 本用例钉装配面逐字段来源：TCP＝KIN-14 默认工具解析、初值＝配置×
/// 区间×referenceQ 经域设施物化、referenceQ＝会话姿态、intervals＝模型
/// 评价区间、容差/迭代/去重＝配置直传。
TEST(PluginPanelLinks, LK2_AssembleSessionSolveRequestComplete_KIN_P2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-02", "KIN-13", "KIN-14"},
                  std::vector<std::string>{});
    Harness h;

    // 配置面：SeededRandom×3（"初值与数量来自求解配置"的断言载体——
    // seed=7 会话基线，I-KIN-4 合法域）。
    h.session.savedConfig.initialStrategy = InitialValueStrategy::SeededRandom;
    h.session.savedConfig.initialValuesCount = 3U;
    h.session.savedConfig.iterationLimit = 128U;
    h.session.savedConfig.positionResidualTolerance = 5e-7;        // m
    h.session.savedConfig.orientationResidualTolerance = 2e-6;     // rad
    h.session.savedConfig.ikDedupThresholdPerAxis = 4e-6;          // rad|m 逐轴

    // 会话姿态在场面：referenceQ 必须取会话姿态（非零可判别值）。
    h.sessionPose.setJointConfiguration({0.1, -0.2});

    const auto a = assembleSessionSolveRequest(h.services, h.session);
    ASSERT_TRUE(a.ok) << a.statusText;

    // TCP：KIN-14/A-15默认工具解析（objectId命中快照Tool类别，空键明确
    // 选择默认偏移，不能因同名非默认TCP切换；A08字段①其余身份断言不变）。
    const runtime::CanonicalTool& tool = h.model.tools().front();
    EXPECT_EQ(a.request.tcp.toolObject, tool.objectId);
    EXPECT_TRUE(a.request.tcp.tcpKey.empty());
    const auto loc = h.model.findObject(a.request.tcp.toolObject);
    ASSERT_TRUE(loc.has_value());
    EXPECT_EQ(loc->kind, runtime::CanonicalModel::ObjectKind::Tool);

    // 初值：数量来自配置、值＝makeInitialValues 同参产出（域设施唯一
    // 实现点——装配零本地重写的结构钉）。
    ASSERT_EQ(a.request.initialValues.size(), 3U);
    const auto expected = makeInitialValues(
        InitialValueStrategy::SeededRandom, 3U, 7U,
        evaluationIntervals(h.view), {0.1, -0.2});
    EXPECT_EQ(a.request.initialValues, expected);

    // referenceQ＝会话姿态；intervals＝模型评价区间（evaluationIntervals
    // 同源——逐轴三字段相等，A08 字段③④；JointInterval 无聚合等值——
    // 测试面逐字段比较）。
    EXPECT_EQ(a.request.referenceQ, (std::vector<double>{0.1, -0.2}));
    {
        const auto expectItv = evaluationIntervals(h.view);
        ASSERT_EQ(a.request.intervals.size(), expectItv.size());
        for (std::size_t i = 0; i < expectItv.size(); ++i) {
            EXPECT_EQ(a.request.intervals[i].bounded, expectItv[i].bounded);
            EXPECT_DOUBLE_EQ(a.request.intervals[i].lower, expectItv[i].lower);
            EXPECT_DOUBLE_EQ(a.request.intervals[i].upper, expectItv[i].upper);
        }
    }

    // 配置字段直传（容差/迭代上限/去重阈值——A08 字段⑤）。
    EXPECT_DOUBLE_EQ(a.request.positionTolerance, 5e-7);
    EXPECT_DOUBLE_EQ(a.request.orientationTolerance, 2e-6);
    EXPECT_EQ(a.request.iterationLimit, 128U);
    EXPECT_DOUBLE_EQ(a.request.dedupThresholdPerAxis, 4e-6);
}

/// 装配失败/回退面（显式拒绝语义——绝不提交残缺请求，NFR-COR-03）：
/// 无工具→KIN-NO-TCP；非法配置→KIN-CONFIG-ILLEGAL；会话姿态维度违约
/// →显式拒绝；视图空→拒绝；会话未设置→referenceQ 零位回退。
TEST(PluginPanelLinks, LK2_AssembleSessionSolveRequestFailures_KIN_P2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-02", "NFR-COR-03"},
                  std::vector<std::string>{});

    // a) 无工具模型（tools 空＋defaultTcpIndex 空）→ 装配失败＋稳定
    //    诊断码 KIN-NO-TCP（A08"解析失败显式报错"义务）。
    {
        const auto bare = makeModel(
            std::vector<rt::CanonicalJoint>{
                revolute("b0", rw::math::Vector3D<double>(0, 0, 1),
                         trans(0, 0, 0), -1.0, 1.0)},
            trans(0.3, 0, 0), trans(0, 0, 0), false);
        TestView view(bare);
        KinPanelServices s;
        s.modelView = &view;
        KinModuleSessionState st;
        st.savedConfig.seed = 1;
        st.savedConfig.regionBudget.seed = 1;
        const auto a = assembleSessionSolveRequest(s, st);
        EXPECT_FALSE(a.ok);
        EXPECT_NE(a.statusText.find("KIN-NO-TCP"), std::string::npos);
    }
    // b) 非法配置（未初始化会话的默认构造 seed=0——I-KIN-4 非法）→
    //    KIN-CONFIG-ILLEGAL（非法配置绝不流入 makeInitialValues——
    //    其 seed=0 行为契约即"输出无定义"）。
    {
        Harness h;
        h.session.savedConfig.seed = 0;  // 会话纪律违约面（构造置 7 后回置）
        const auto a = assembleSessionSolveRequest(h.services, h.session);
        EXPECT_FALSE(a.ok);
        EXPECT_NE(a.statusText.find("KIN-CONFIG-ILLEGAL"), std::string::npos);
    }
    // c) 会话姿态维度违约（1 维 ≠ 2 dof）→ 显式拒绝（装配层注入事实
    //    错误不可钳制——NFR-COR-03）。
    {
        Harness h;
        h.sessionPose.setJointConfiguration({0.1});
        const auto a = assembleSessionSolveRequest(h.services, h.session);
        EXPECT_FALSE(a.ok);
        EXPECT_NE(a.statusText.find("维度"), std::string::npos);
    }
    // d) 模型视图缝空 → 拒绝（装配无输入真值面）。
    {
        KinPanelServices s;
        KinModuleSessionState st;
        st.savedConfig.seed = 1;
        st.savedConfig.regionBudget.seed = 1;
        EXPECT_FALSE(assembleSessionSolveRequest(s, st).ok);
    }
    // e) 会话姿态未设置 → referenceQ 零位回退（dof 维全零——确定性
    //    无信息注入回退；面板此态下关节表为空）。
    {
        Harness h;
        const auto a = assembleSessionSolveRequest(h.services, h.session);
        ASSERT_TRUE(a.ok);
        EXPECT_EQ(a.request.referenceQ, (std::vector<double>{0.0, 0.0}));
    }
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
    // 逐轴类型表（KIN-P3/A05——二连杆全转动链；回填文本逐轴按天然量纲
    // 投影，转动轴走角度量纲）。
    const std::vector<runtime::JointType> allRev(
        2, runtime::JointType::Revolute);
    // 未设置：回退编辑器缺省值（不读值——KinSessionPose 契约）。
    EXPECT_FALSE(
        tcpBackfillText(&h.sessionPose, h.session, {0.1, 0.2}, allRev).has_value());

    h.sessionPose.setJointConfiguration({0.5, -0.25});
    // SI 直显（无投影）。
    const auto si = tcpBackfillText(&h.sessionPose, h.session, {0.5, -0.25}, allRev);
    ASSERT_TRUE(si.has_value());
    EXPECT_NE(si->find("0.5"), std::string::npos);
    // deg 制式：0.5 rad → 28.6x°（显示投影——回填文本随制式变化）。
    h.session.displayUnits = tryFindKinDisplayUnits("mm", "deg");
    ASSERT_TRUE(h.session.displayUnits.has_value());
    const auto deg = tcpBackfillText(&h.sessionPose, h.session, {0.5, -0.25}, allRev);
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

/// KIN-P1（审查条目 A07 换算半区回归）：显示制式→SI 逆换算（提交面
/// resolveKinDisplayToSi）——m/cm/mm 三制式输入"同一物理目标 0.1 m"
/// 的正确显示值（0.1/10/100）逆换算回一致 SI；A07 反例数值（mm 显示值
/// 0.1 → 1e-4 m，非 0.1、更非修复前的 100）；与读侧 projectLength 互逆。
TEST(PluginPanelLinks, DisplayToSi_ConsistentSiAcrossUnitSystems_KIN_P1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-12"},
                  std::vector<std::string>{"AT-27"});
    // 三制式一致性：同一物理目标 0.1 m 的各制式正确显示值 → 一致 SI。
    const auto mUnits = tryFindKinDisplayUnits("m", "rad");
    const auto cmUnits = tryFindKinDisplayUnits("cm", "deg");
    const auto mmUnits = tryFindKinDisplayUnits("mm", "deg");
    ASSERT_TRUE(mUnits.has_value());
    ASSERT_TRUE(cmUnits.has_value());
    ASSERT_TRUE(mmUnits.has_value());
    EXPECT_DOUBLE_EQ(resolveKinDisplayToSi(0.1, mUnits), 0.1);   // m：因子 1
    EXPECT_NEAR(resolveKinDisplayToSi(10.0, cmUnits), 0.1, 1e-15);   // 10 cm
    EXPECT_NEAR(resolveKinDisplayToSi(100.0, mmUnits), 0.1, 1e-15);  // 100 mm
    // A07 反例数值钉扎：mm 显示值 0.1（＝0.1 mm）→ 1e-4 m。
    // （修复前缺陷：被 projectLength 正向放大为 100 m。）
    EXPECT_NEAR(resolveKinDisplayToSi(0.1, mmUnits), 1e-4, 1e-18);
    // nullopt＝SI 直显制式恒等（未选制式时输入即 SI）。
    EXPECT_DOUBLE_EQ(resolveKinDisplayToSi(0.1, std::nullopt), 0.1);
    // 与读侧投影互逆：0.1 m 经"逆换算→正向投影"往返回 0.1（mm 制式——
    // 乘除各一次，误差 ≤ 2 ulp；不承诺位精确——AnalysisConfig 呈现层
    // 容差口径）。
    EXPECT_NEAR(mmUnits->projectLength(resolveKinDisplayToSi(0.1, mmUnits)),
                0.1, 1e-15);
    // 因子语义钉扎（core Units §4.4：value_si＝value×factor）。
    EXPECT_DOUBLE_EQ(mmUnits->lengthUnit().siFactor(), 0.001);
    EXPECT_DOUBLE_EQ(cmUnits->lengthUnit().siFactor(), 0.01);
    EXPECT_DOUBLE_EQ(mUnits->lengthUnit().siFactor(), 1.0);
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

/// GUI 呈现（四面板 widget 渲染/交互点击）：本目标（无界面）仅保留登记
/// ——★ KIN-P4 起真实执行面在 sdurws_ird_kinematics_gui_test 的
/// KinHostLoop.PanelPresentation_RealExecution_KIN_P4（真实 QApplication
/// ＋真实控件事件：四页签/按钮门控/状态行点验）与
/// KinHostLoop.SixAxisVariantClosedLoop_KIN_P4（六轴改型全流程闭环）。
/// 手动验证通道归 sdurws_ird_kinematics_app harness 留痕（DTB §5.1/
/// DoD 第 5 条）；无界面测试目标不启动 GUI 的结构性事实不变（modeling
/// V-22 先例同款）——skip 是环境事实的如实登记，不是验证替代。
TEST(PluginPanelLinks, KinGuiPresentationRegisteredNotExecuted_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-04", "UX-05"},
                  std::vector<std::string>{});
    sdurws::ird::testkit::report::setOutcome(
        sdurws::ird::testkit::report::Outcome::EnvUnavailable,
        "GUI 呈现与交互在本无界面目标仅登记（KIN-P4 起真实执行面已迁移至"
        " kinematics_gui_test 的 KinHostLoop 套件——环境事实如实登记，"
        "testkit §6.7）");
    GTEST_SKIP() << "GUI 呈现仅登记（真实执行已迁移 gui_test——环境事实如实）";
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

// =====================================================================
// KIN-P3 回归（审查条目 A03 合法无穷显示／A05 混合链逐轴单位／A09 具名
// Home 来源）——零 Qt 半区；widget 半区在 gui_test
// （KinematicsPanelSessionGuiTest）。修复前红证见构建留痕
// traceability/builds/kin-p3/（红→绿两段执行记录）。
// =====================================================================

/// A03 回归①：奇异链 FK 产出条件数 +∞（合法无穷），指标行构建不抛异常
/// 且语义正确（"奇异/∞"，SI 真值 +∞ 原样保留不截断）。修复前
/// poseMetricRows 对 +∞ 抛 invalid_argument——奇异状态完全无法显示。
TEST(PluginPanelLinks, KinNonFiniteDisplay_SingularConditionNumberRow_KIN_P3)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-01"}, std::vector<std::string>{});
    // 六轴同轴退化链（审查 A02 载体几何——q=0 时雅可比六列全同，σmin=0
    // →条件数 +∞：Fk.hpp 契约"奇异→+∞，以 isFinite 标记承载，不静默截断"）。
    TestView view(coaxialChainModel());
    FkEvaluator fk;
    TcpRef tcp;
    tcp.toolObject = view.model().tools().front().objectId;  // canonical TCP（空键）
    const auto metrics = fk.evaluate(view, tcp, std::vector<double>(6, 0.0));
    ASSERT_TRUE(metrics.ok());
    EXPECT_FALSE(metrics.get().conditionNumberIsFinite);  // 奇异前提成立
    // 合法 +∞ 不抛（修复前此处抛 invalid_argument——审查 A03 反例）。
    std::vector<KinNamedValueRow> rows;
    EXPECT_NO_THROW({ rows = poseMetricRows(metrics.get(), std::nullopt); });
    // 条件数行语义正确：语义词"奇异/∞"、SI 真值 +∞ 原样（显示层不截断）。
    // 行定位按 key（KIN-R2 任务4 起行集含双参考系/RPY/矩阵 9 元——索引
    // 定位脆弱，键为稳定锚）。
    const auto cond = std::find_if(rows.begin(), rows.end(),
                                   [](const KinNamedValueRow& r) {
                                       return r.key == "cond";
                                   });
    ASSERT_NE(cond, rows.end());
    EXPECT_STREQ(cond->displayText.c_str(), "奇异/∞");
    EXPECT_TRUE(std::isinf(cond->siValue) && cond->siValue > 0.0);
    // 近奇异（非奇异有限大条件数）照常数值显示——三分类不误伤有限值。
    PoseMetrics nearSingular;
    nearSingular.conditionNumber = 12345.678;
    const auto nearRows = poseMetricRows(nearSingular, std::nullopt);
    const auto nearCond =
        std::find_if(nearRows.begin(), nearRows.end(),
                     [](const KinNamedValueRow& r) { return r.key == "cond"; });
    ASSERT_NE(nearCond, nearRows.end());
    EXPECT_NE(nearCond->displayText.find("12345.7"), std::string::npos)
        << nearCond->displayText;  // 6 位有效数字 general
}

/// A03 回归②：关节裕量无界 +∞（连续关节无工作范围——合法无界）呈现
/// "无界"；NaN/−∞（非法）仍 fail-fast 拒绝（三分类的拒绝半区不松动）。
TEST(PluginPanelLinks, KinNonFiniteDisplay_UnboundedMarginAndIllegalNaN_KIN_P3)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-01"}, std::vector<std::string>{});
    // 直接构造指标值（行构建面是纯搬运——+∞ 合法性由域设施契约背书：
    // KinTypes.hpp"连续无工作范围关节＝+∞；全无界＝+∞"）。
    PoseMetrics metrics;
    metrics.conditionNumber = 1.5;
    metrics.minimumJointMargin = std::numeric_limits<double>::infinity();
    metrics.jointMargins = {0.5, std::numeric_limits<double>::infinity()};
    std::vector<KinNamedValueRow> rows;
    EXPECT_NO_THROW({ rows = poseMetricRows(metrics, std::nullopt); });
    // 最小裕量行/无界关节行呈现"无界"；有界关节行照常数值显示。
    for (const auto& r : rows) {
        if (r.key == "min-margin" || r.key == "margin-j2") {
            EXPECT_STREQ(r.displayText.c_str(), "无界") << r.key;
        }
        if (r.key == "margin-j1") {
            EXPECT_STREQ(r.displayText.c_str(), "0.5") << r.key;
        }
    }
    // 非法 NaN：上游计算违约——fail-fast 不渲染（拒绝面保留）。
    PoseMetrics nanMetrics;
    nanMetrics.conditionNumber = std::numeric_limits<double>::quiet_NaN();
    EXPECT_THROW(poseMetricRows(nanMetrics, std::nullopt), std::invalid_argument);
    // 非法 −∞：这些量的值域不含负无穷——同违约轨。
    PoseMetrics negInfMetrics;
    negInfMetrics.conditionNumber = -std::numeric_limits<double>::infinity();
    EXPECT_THROW(poseMetricRows(negInfMetrics, std::nullopt), std::invalid_argument);
}

/// A03 回归③：解表/检查器行的合法 +∞ 指标（条件数/裕量）——语义词
/// 呈现且零异常；reprojectMetricRows 对含 +∞ 行的整表重建幂等。
TEST(PluginPanelLinks, KinNonFiniteDisplay_SolutionRowsInfiniteMetrics_KIN_P3)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-01"}, std::vector<std::string>{});
    // 脚本化解集：解 A 全有限、解 B 条件数/裕量合法 +∞（奇异但可行——
    // 现实工程形态：六轴腕奇异点附近的解仍可交付）。
    IkSolutionSet set;
    KinematicSolution a;
    a.q = {0.1, 0.2};
    a.minimumJointMargin = 0.1;
    a.conditionNumber = 2.0;
    a.positionResidual = 1e-7;
    a.signature = "sig-finite";
    KinematicSolution b;
    b.q = {0.3, -0.4};
    b.minimumJointMargin = std::numeric_limits<double>::infinity();  // 全无界
    b.conditionNumber = std::numeric_limits<double>::infinity();     // 奇异
    b.positionResidual = 2e-7;
    b.signature = "sig-infinite";
    set.solutions = {a, b};
    set.statistics.rawCount = 2;
    set.statistics.convergedCount = 2;
    set.statistics.dedupedCount = 2;
    KinematicSolutionSet view(set);

    // 解表行：+∞ 行语义词呈现，有限行照常（混合表零异常零截断）。行序
    // ＝sorted() 四键稳定序（权威在解集视图——断言按签名定位不依赖序）。
    std::vector<KinSolutionRow> rows;
    EXPECT_NO_THROW({
        rows = solutionRowsFor(
            view, false,
            {runtime::JointType::Revolute, runtime::JointType::Revolute},
            std::nullopt);
    });
    ASSERT_EQ(rows.size(), 2U);
    const KinSolutionRow* finite = nullptr;
    const KinSolutionRow* infinite = nullptr;
    for (const auto& r : rows) {
        (r.signature == "sig-finite" ? finite : infinite) = &r;
    }
    ASSERT_NE(finite, nullptr);
    ASSERT_NE(infinite, nullptr);
    EXPECT_STREQ(finite->conditionNumberText.c_str(), "2");
    EXPECT_STREQ(finite->minJointMarginText.c_str(), "0.1");
    EXPECT_STREQ(infinite->conditionNumberText.c_str(), "奇异/∞");
    EXPECT_STREQ(infinite->minJointMarginText.c_str(), "无界");
    // 检查器行：同则（insp-cond"奇异/∞"、insp-margin"无界"）——选中
    // 指称取 +∞ 解在视图稳定序中的 rank。
    std::vector<KinNamedValueRow> inspector;
    EXPECT_NO_THROW({
        inspector = solutionInspectorRows(view,
                                          SolutionRef{infinite->rank},
                                          std::nullopt);
    });
    for (const auto& r : inspector) {
        if (r.key == "insp-cond") {
            EXPECT_STREQ(r.displayText.c_str(), "奇异/∞");
        }
        if (r.key == "insp-margin") {
            EXPECT_STREQ(r.displayText.c_str(), "无界");
        }
    }
    // 整表重投影对 +∞ 行幂等（+∞ 呈现与制式无关）、对有限行重换算；
    // SI 真值逐行不动（V-17）。行数＝TCP 基座系 3＋世界系 3＋RPY 3＋
    // 矩阵 9＋cond/manip/min-margin 3（奇异值/裕量向量空——最小构造）
    // ＝21（KIN-R2 任务4 双参考系＋完整姿态行集布局）。
    const auto reprojected = reprojectMetricRows(
        poseMetricRows([] {
            PoseMetrics m;
            m.conditionNumber = std::numeric_limits<double>::infinity();
            m.manipulability = 0.25;
            return m;
        }(), std::nullopt),
        tryFindKinDisplayUnits("mm", "deg"));
    ASSERT_EQ(reprojected.size(), 21U);
    bool sawCond = false;
    for (const auto& r : reprojected) {
        if (r.key == "cond") {
            sawCond = true;
            EXPECT_STREQ(r.displayText.c_str(), "奇异/∞");
            EXPECT_TRUE(std::isinf(r.siValue));
        }
    }
    EXPECT_TRUE(sawCond);
}

/// F-902 回归：域外无穷白名单按行收紧——域契约**不含**无穷的量（TCP
/// 位置/旋转矩阵元/可操作度/奇异值/残差——Fk.hpp PoseMetrics 字段规则）
/// siValue=+∞（上游双重违约形态）不再静默显示泛化"∞"，走"非法值(+∞)"
/// 显式标记轨；域内合法无穷（条件数/关节裕量——Fk.hpp:108-112/
/// KinTypes.hpp:272-273）语义词呈现不回归（A03 三分类保留）；NaN/−∞
/// 拒绝面不松动。修复前 formatKinQuantityText 对一切 +∞ 返回语义词
/// （缺省"∞"）——F-902 探针实测域外位置行 siValue=+∞ 被静默放行。
TEST(PluginPanelLinks, KinNonFiniteDisplay_OutOfDomainInfinityMarker_F902)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-01"}, std::vector<std::string>{});
    const double inf = std::numeric_limits<double>::infinity();

    // ---- 直接格式化面：白名单参数即域契约判定（有值＝合法、缺省＝域外）。
    // 域外 +∞：位置行（"m"）与无量纲行（"1"）——修复前此处返回"∞"（红）。
    EXPECT_EQ(formatKinQuantityText(inf, "m", std::nullopt), "非法值(+∞)");
    EXPECT_EQ(formatKinQuantityText(inf, "1", std::nullopt), "非法值(+∞)");
    // 域内合法无穷不回归（A03 行为保留）：白名单在场＝语义词原样返回。
    EXPECT_EQ(formatKinQuantityText(inf, "1", std::nullopt, "奇异/∞"),
              "奇异/∞");
    EXPECT_EQ(formatKinQuantityText(inf, "1", std::nullopt, "无界"), "无界");
    // NaN/−∞ 拒绝面不松动：任何行（含白名单行）的值域都不含该两类——
    // fail-fast 不渲染伪数据（与 F-902 的"+∞ 分轨"正交）。
    EXPECT_THROW(
        formatKinQuantityText(std::numeric_limits<double>::quiet_NaN(), "1",
                              std::nullopt),
        std::invalid_argument);
    EXPECT_THROW(formatKinQuantityText(-inf, "1", std::nullopt, "无界"),
                 std::invalid_argument);

    // ---- 行集面：域外行与域内行同表混排互不误伤（白名单按行生效的
    // 表级证据）。域外形态＝直接构造指标值（行构建面纯搬运；FK 契约
    // 恒有限——本行的 +∞ 只能来自上游双重违约，即 F-902 的可达前提）。
    PoseMetrics metrics;
    metrics.tcpInBase.P()[0] = inf;        // TCP X（m——域外）
    metrics.tcpInBase.R()(0, 0) = inf;     // 旋转矩阵元（∈[-1,1]——域外）
    metrics.manipulability = inf;          // 可操作度（≥0 有限——域外）
    metrics.singularValues = {inf};        // 奇异值（SVD 有限产出——域外）
    metrics.conditionNumber = inf;         // 条件数（奇异＝+∞——域内合法）
    metrics.minimumJointMargin = inf;      // 最小裕量（全无界＝+∞——域内合法）
    std::vector<KinNamedValueRow> rows;
    EXPECT_NO_THROW({ rows = poseMetricRows(metrics, std::nullopt); });
    auto findRow = [&rows](const std::string& key) -> const KinNamedValueRow* {
        const auto it = std::find_if(rows.begin(), rows.end(),
                                     [&key](const KinNamedValueRow& r) {
                                         return r.key == key;
                                     });
        return it != rows.end() ? &*it : nullptr;
    };
    // 域外四行：显式标记（修复前为"∞"——红）。
    ASSERT_NE(findRow("tcp-x"), nullptr);
    EXPECT_STREQ(findRow("tcp-x")->displayText.c_str(), "非法值(+∞)");
    ASSERT_NE(findRow("tcp-rot-r0c0"), nullptr);
    EXPECT_STREQ(findRow("tcp-rot-r0c0")->displayText.c_str(), "非法值(+∞)");
    ASSERT_NE(findRow("manip"), nullptr);
    EXPECT_STREQ(findRow("manip")->displayText.c_str(), "非法值(+∞)");
    ASSERT_NE(findRow("sigma-1"), nullptr);
    EXPECT_STREQ(findRow("sigma-1")->displayText.c_str(), "非法值(+∞)");
    // 域内两行：语义词（A03 不回归）。
    ASSERT_NE(findRow("cond"), nullptr);
    EXPECT_STREQ(findRow("cond")->displayText.c_str(), "奇异/∞");
    ASSERT_NE(findRow("min-margin"), nullptr);
    EXPECT_STREQ(findRow("min-margin")->displayText.c_str(), "无界");
    // SI 真值不被呈现层篡改（V-17——标记行同样保留 +∞ 原值）。
    EXPECT_TRUE(std::isinf(findRow("tcp-x")->siValue)
                && findRow("tcp-x")->siValue > 0.0);

    // ---- 重投影幂等：域外标记与制式无关（切 mm/deg 后仍为显式标记，
    // 不退化回泛化"∞"或数值）；域内语义词跨制式稳定（L-K8 同则）。
    const auto mmDeg = tryFindKinDisplayUnits("mm", "deg");
    ASSERT_TRUE(mmDeg.has_value());
    const auto reprojected = reprojectMetricRows(rows, mmDeg);
    ASSERT_EQ(reprojected.size(), rows.size());
    for (const auto& r : reprojected) {
        if (r.key == "tcp-x") {
            EXPECT_STREQ(r.displayText.c_str(), "非法值(+∞)");
        }
        if (r.key == "cond") {
            EXPECT_STREQ(r.displayText.c_str(), "奇异/∞");
        }
    }
}

/// A05 回归①：混合链（转动＋移动）解列逐轴投影——移动轴 0.1 m 在
/// mm/deg 制式下显示 100、转动轴 0.1 rad 显示 5.72958（审查 A05 反例
/// 数值钉扎：修复前全轴按角度＝"5.72958, 5.72958"）。维度失配/Fixed
/// 类型/非有限分量三装配错误轨 fail-fast。
TEST(PluginPanelLinks, MixedChainProjection_PerAxisUnitSelection_KIN_P3)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-12"}, std::vector<std::string>{"AT-27"});
    const auto mmDeg = tryFindKinDisplayUnits("mm", "deg");
    ASSERT_TRUE(mmDeg.has_value());
    // 转动＋移动混合链（revPrismModel 的 dof 类型表——dofJointTypes 口径）。
    const std::vector<runtime::JointType> mixed = {runtime::JointType::Revolute,
                                                   runtime::JointType::Prismatic};
    // mm/deg 制式：q=(0.1 rad, 0.1 m) → "5.72958, 100"（逐轴各走其量纲）；
    // 残差列随长度制式投影（0 m→0 mm）。
    const auto proj = reprojectSolutionColumns({0.1, 0.1}, mixed, 0.0, mmDeg);
    EXPECT_EQ(proj.first, "5.72958, 100");
    EXPECT_EQ(proj.second, "0 mm");
    // SI 直显制式：恒等投影（真值原样）。
    const auto si = reprojectSolutionColumns({0.1, 0.1}, mixed, 0.0, std::nullopt);
    EXPECT_EQ(si.first, "0.1, 0.1");
    // 全转动链（六轴——回归面：单量纲链行为不因逐轴化而变化）。
    const std::vector<runtime::JointType> allRev(6, runtime::JointType::Revolute);
    const auto rev = reprojectSolutionColumns(std::vector<double>(6, 0.1), allRev,
                                              0.0, mmDeg);
    EXPECT_EQ(rev.first,
              "5.72958, 5.72958, 5.72958, 5.72958, 5.72958, 5.72958");
    // 装配错误轨①：维度失配——fail-fast（绝不回退全轴角度猜的旧语义）。
    EXPECT_THROW(reprojectSolutionColumns({0.1, 0.1}, {runtime::JointType::Revolute},
                                          0.0, mmDeg),
                 std::invalid_argument);
    // 装配错误轨②：Fixed 不消耗 q——出现在类型表即装配错误。
    EXPECT_THROW(reprojectSolutionColumns({0.1}, {runtime::JointType::Fixed}, 0.0,
                                          mmDeg),
                 std::invalid_argument);
    // 上游违约轨：关节分量非有限——fail-fast 不渲染。
    EXPECT_THROW(reprojectSolutionColumns(
                     {std::numeric_limits<double>::quiet_NaN(), 0.1}, mixed, 0.0,
                     mmDeg),
                 std::invalid_argument);
}

/// A05 回归②：逐轴类型提取与"数值＋单位"关节文本——dofJointTypes 按
/// 链序滤除 Fixed；formatJointAxisText 逐轴量纲选单位（移动 mm、转动
/// deg、连续 deg）；tcpBackfillText 混合链回填逐轴投影。
TEST(PluginPanelLinks, MixedChainProjection_JointAxisTextAndDofExtraction_KIN_P3)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-12"}, std::vector<std::string>{});
    // 类型提取：混合链＝{Revolute, Prismatic}；六轴＝6×Revolute；全固定
    // 链＝空表（Fixed 全滤——dof=0 边界）。
    TestView mixedView(revPrismModel());
    EXPECT_EQ(dofJointTypes(mixedView.model()),
              (std::vector<runtime::JointType>{runtime::JointType::Revolute,
                                               runtime::JointType::Prismatic}));
    TestView sixView(sixAxisModel());
    EXPECT_EQ(dofJointTypes(sixView.model()),
              std::vector<runtime::JointType>(6, runtime::JointType::Revolute));
    TestView fixedView(allFixedModel());
    EXPECT_TRUE(dofJointTypes(fixedView.model()).empty());
    // "数值＋单位"文本：移动轴 0.1 m→"100 mm"；转动轴 0.1 rad→
    // "5.72958 deg"；连续轴按角度量纲（同转动）；SI 直显用 SI token。
    const auto mmDeg = tryFindKinDisplayUnits("mm", "deg");
    ASSERT_TRUE(mmDeg.has_value());
    EXPECT_EQ(formatJointAxisText(0.1, runtime::JointType::Prismatic, mmDeg),
              "100 mm");
    EXPECT_EQ(formatJointAxisText(0.1, runtime::JointType::Revolute, mmDeg),
              "5.72958 deg");
    EXPECT_EQ(formatJointAxisText(1.0, runtime::JointType::Continuous, mmDeg),
              "57.2958 deg");
    EXPECT_EQ(formatJointAxisText(0.1, runtime::JointType::Prismatic, std::nullopt),
              "0.1 m");
    EXPECT_EQ(formatJointAxisText(0.1, runtime::JointType::Revolute, std::nullopt),
              "0.1 rad");
    // 回填文本（L-K5）：混合链会话姿态逐轴投影（修复前全轴角度——同 A05
    // 缺陷面）：mm/deg 制式下 "5.72958 deg, 100 mm"。
    Harness h;
    h.sessionPose.setJointConfiguration({0.1, 0.1});
    h.session.displayUnits = mmDeg;
    const auto backfill = tcpBackfillText(
        &h.sessionPose, h.session, {0.1, 0.1},
        {runtime::JointType::Revolute, runtime::JointType::Prismatic});
    ASSERT_TRUE(backfill.has_value());
    EXPECT_EQ(*backfill, "5.72958 deg, 100 mm");
    // 维度失配＝装配错误 fail-fast。
    EXPECT_THROW(tcpBackfillText(&h.sessionPose, h.session, {0.1},
                                 {runtime::JointType::Revolute}),
                 std::invalid_argument);
}

/// A09 回归（零 Qt 编排半区）：复位 Home 具名来源四拒绝态＋成功态——
/// 来源缺失/未定义/维度不符显式拒绝零写回，成功路径零修订（命令出口
/// 计数为零——KIN-06/AT-04 结构面复核）。
TEST(PluginPanelLinks, ResetHome_NamedSourceFlowGatesAndWrites_KIN_P3)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-06"}, std::vector<std::string>{"AT-04"});
    Harness h;
    h.sessionPose.setJointConfiguration({0.4, 0.4});  // 当前姿态非零

    // 拒绝态①：会话姿态缝未装配——无写点可落。
    {
        KinPanelServices noPose = h.services;
        noPose.sessionPose = nullptr;
        EXPECT_NE(resetSessionHomeFromNamedSource(noPose).find("不可用"),
                  std::string::npos);
    }
    // 拒绝态②：具名来源缝未装配——显式拒绝（绝不以当前姿态充当 Home）。
    {
        KinPanelServices noHome = h.services;
        noHome.homePose = nullptr;
        const std::string text = resetSessionHomeFromNamedSource(noHome);
        EXPECT_NE(text.find("不可用"), std::string::npos) << text;
        EXPECT_NE(text.find("未装配"), std::string::npos) << text;
    }
    // 拒绝态③：来源在场但模型/会话未定义 Home（nullopt）——明确提示
    // 零写回（当前姿态保持 0.4——修复前此处被原样写回并谎报成功）。
    {
        KinPanelServices absent = h.services;
        absent.homePose = [] { return std::optional<std::vector<double>>{}; };
        const std::string text = resetSessionHomeFromNamedSource(absent);
        EXPECT_NE(text.find("未定义"), std::string::npos) << text;
        EXPECT_NE(text.find("未写回"), std::string::npos) << text;
    }
    // 拒绝态④：来源值维度与设备自由度不符——显式反馈零写回（不钳制
    // 不截断——截断写回伪造复位语义）。
    {
        KinPanelServices mismatch = h.services;
        mismatch.homePose = [] {
            return std::optional<std::vector<double>>(std::vector<double>{1.0});
        };
        const std::string text = resetSessionHomeFromNamedSource(mismatch);
        EXPECT_NE(text.find("维度"), std::string::npos) << text;
        EXPECT_NE(text.find("未写回"), std::string::npos) << text;
    }
    EXPECT_EQ(h.sessionPose.jointConfiguration(), (std::vector<double>{0.4, 0.4}));

    // 成功态：具名 Home 在场——覆写会话姿态；零修订（命令提交出口计数
    // 为零——结构上本流无提交通道，计数复核）。
    h.services.homePose = [] {
        return std::optional<std::vector<double>>(std::vector<double>{1.0, -2.0});
    };
    const std::string ok = resetSessionHomeFromNamedSource(h.services);
    EXPECT_NE(ok.find("已复位 Home"), std::string::npos) << ok;
    EXPECT_EQ(h.sessionPose.jointConfiguration(), (std::vector<double>{1.0, -2.0}));
    EXPECT_EQ(h.commandSubmitCount, 0);
}

/// F-903 回归：Home 来源值含非有限分量（维度合法）——编排层前置拦截
/// 转显式提示轨（与维度违约同轨），异常不再穿越编排函数；会话姿态零
/// 写回。修复前该形态只在写入口（KinSessionPose::setJointConfiguration
/// fail-fast）拒绝——std::invalid_argument 直达 Qt 槽，与维度违约的
/// 提示轨双轨不一致（验收探针实测异常传播、会话态零污染）。
TEST(PluginPanelLinks, ResetHome_NonFiniteSourceExplicitHintTrack_F903)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-06"}, std::vector<std::string>{"AT-04"});
    Harness h;
    h.sessionPose.setJointConfiguration({0.4, 0.4});  // 当前姿态非零（写回断言基线）

    // 值面违约①：NaN 分量（维度 2＝二连杆 dof——维度检查通过、值非法）。
    {
        KinPanelServices nanHome = h.services;
        nanHome.homePose = [] {
            return std::optional<std::vector<double>>(
                std::vector<double>{1.0, std::numeric_limits<double>::quiet_NaN()});
        };
        // 修复前此处抛 std::invalid_argument（写入口 fail-fast 穿越编排
        // 函数——红）；修复后转提示轨不抛。
        std::string text;
        EXPECT_NO_THROW({ text = resetSessionHomeFromNamedSource(nanHome); });
        EXPECT_NE(text.find("非有限"), std::string::npos) << text;
        EXPECT_NE(text.find("未写回"), std::string::npos) << text;
        // 会话姿态零写回（不钳制不置零——NaN 分量不得污染会话态）。
        EXPECT_EQ(h.sessionPose.jointConfiguration(),
                  (std::vector<double>{0.4, 0.4}));
    }
    // 值面违约②：−∞ 分量（同一非有限拒绝面——isfinite 单点判定）。
    {
        KinPanelServices negInfHome = h.services;
        negInfHome.homePose = [] {
            return std::optional<std::vector<double>>(
                std::vector<double>{-std::numeric_limits<double>::infinity(), 0.5});
        };
        std::string text;
        EXPECT_NO_THROW({ text = resetSessionHomeFromNamedSource(negInfHome); });
        EXPECT_NE(text.find("非有限"), std::string::npos) << text;
        EXPECT_EQ(h.sessionPose.jointConfiguration(),
                  (std::vector<double>{0.4, 0.4}));
    }
    EXPECT_EQ(h.commandSubmitCount, 0);  // 拒绝路径零修订（KIN-06/AT-04）
}

// =====================================================================
// KIN-R2 任务3——探索式工作空间采样编排（面板模式编辑入口的零 Qt 半区；
// 域计算核 WorkspaceTest 已钉——本组只钉"面板参数→域查询→产物入会话
// 态→导出/回写走既有通道"的编排面与 F-905 下游感知）
// =====================================================================

/// 关节随机采样全链：面板参数（mode=JointSpaceRandom、样本数 8）→域
/// 查询装配→内联 FK 采样→产物入会话态（逐样本 Reached——FK 构造性
/// 可达、样本携带采样构型）＋模式 token 溯源（F-905——"joint-random"
/// 词表词，禁第二处字面量）。
TEST(PluginPanelLinks, WorkspaceSampling_JointRandomFlow_KIN_R2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-09"}, std::vector<std::string>{});
    Harness h;  // twoLinkModel（带工具——FK 消费合法 TCP）
    KinWorkspaceSamplingParams params;
    params.mode = WorkspaceSamplingMode::JointSpaceRandom;
    params.jointRandomCount = 8;
    KinInlineSamplingContext ctx;
    const KinWorkspaceSamplingFlowResult r =
        runWorkspaceSamplingFlow(h.services, h.session, params, ctx);
    EXPECT_TRUE(r.ok) << r.statusText;
    EXPECT_EQ(r.samplingModeToken, "joint-random");
    EXPECT_NE(r.statusText.find("采样完成"), std::string::npos) << r.statusText;
    ASSERT_TRUE(h.session.lastWorkspaceRun != nullptr);
    const WorkspaceSampleRun& run = *h.session.lastWorkspaceRun;
    EXPECT_EQ(run.mode, WorkspaceSamplingMode::JointSpaceRandom);
    ASSERT_EQ(run.results.size(), 8U);
    for (const WorkspaceSampleResult& s : run.results) {
        // 关节模式 FK 全序：构型可达由 FK 保证——零搜索未果形态（域
        // KIN-09 权威语义；DataInsufficient 仅 FK 值面错误可达）。
        EXPECT_EQ(s.state, SampleState::Reached) << s.sampleIndex;
        EXPECT_FALSE(s.jointConfiguration.empty()) << s.sampleIndex;
        EXPECT_TRUE(s.bestSolution.has_value()) << s.sampleIndex;
    }
}

/// 区域盒模式全链（随机撒点）：区域盒尺寸合法（>0）→逐样本 IK 评估→
/// 产物入会话态＋"cartesian-region" token（旧径行为保持——模式词表
/// 回显）。区域零样本合法口径：randomCount=0 → 空产物 ok=true（状态行
/// 含"共 0"——零样本不拒绝不虚构）。
TEST(PluginPanelLinks, WorkspaceSampling_CartesianRegionFlow_KIN_R2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-09"}, std::vector<std::string>{});
    Harness h;
    KinWorkspaceSamplingParams params;
    params.mode = WorkspaceSamplingMode::CartesianRegion;
    params.regionCenterX = 0.6;  // 基座系 m（部分落在二连杆可达环内）
    params.regionSizeX = 0.2;
    params.regionSizeY = 0.2;
    params.regionSizeZ = 0.2;
    params.positionRandom = true;
    params.regionRandomCount = 4;
    KinInlineSamplingContext ctx;
    const KinWorkspaceSamplingFlowResult r =
        runWorkspaceSamplingFlow(h.services, h.session, params, ctx);
    EXPECT_TRUE(r.ok) << r.statusText;
    EXPECT_EQ(r.samplingModeToken, "cartesian-region");
    ASSERT_TRUE(h.session.lastWorkspaceRun != nullptr);
    // 样本数＝位置样本 4＋退化位姿样本 4（§7.2 样本集语义：位置样本与
    // (位置×方向×roll) 位姿样本分列——单方向单 roll 退化配置下位姿样本
    // 数＝位置数，两类样本全部逐样本评估，域分母口径原样承接）。
    EXPECT_EQ(h.session.lastWorkspaceRun->results.size(), 8U);

    // 零样本口径：randomCount=0 → 空产物（合法——与域零样本判定一致）。
    KinWorkspaceSamplingParams zero = params;
    zero.regionRandomCount = 0;
    const KinWorkspaceSamplingFlowResult rz =
        runWorkspaceSamplingFlow(h.services, h.session, zero, ctx);
    EXPECT_TRUE(rz.ok) << rz.statusText;
    ASSERT_TRUE(h.session.lastWorkspaceRun != nullptr);
    EXPECT_EQ(h.session.lastWorkspaceRun->results.size(), 0U);
}

/// 非法查询拒绝轨：区域尺寸 ≤0（RegionBox 契约违约）与配置 seed=0
/// （I-KIN-4）——域校验 invalid_argument 转显式状态行（不抛、不穿越
/// 调用方），会话态零触碰（lastWorkspaceRun 保持空）。
TEST(PluginPanelLinks, WorkspaceSampling_InvalidQueryExplicitReject_KIN_R2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-09", "NFR-COR-03"},
                  std::vector<std::string>{});
    Harness h;
    KinInlineSamplingContext ctx;
    // 形态①：区域盒零尺寸（RegionBox size 三分量须 >0）。
    {
        KinWorkspaceSamplingParams params;
        params.regionSizeX = 0.0;  // 违例——域校验首错
        const KinWorkspaceSamplingFlowResult r =
            runWorkspaceSamplingFlow(h.services, h.session, params, ctx);
        EXPECT_FALSE(r.ok);
        EXPECT_NE(r.statusText.find("采样查询非法"), std::string::npos)
            << r.statusText;
        EXPECT_EQ(h.session.lastWorkspaceRun, nullptr);  // 会话态零触碰
    }
    // 形态②：配置 seed=0（I-KIN-4——savedConfig 会话纪律的越界注入）。
    {
        h.session.savedConfig.seed = 0;
        KinWorkspaceSamplingParams params;
        params.mode = WorkspaceSamplingMode::JointSpaceRandom;
        params.jointRandomCount = 2;
        const KinWorkspaceSamplingFlowResult r =
            runWorkspaceSamplingFlow(h.services, h.session, params, ctx);
        EXPECT_FALSE(r.ok);
        EXPECT_NE(r.statusText.find("非法"), std::string::npos) << r.statusText;
        EXPECT_EQ(h.session.lastWorkspaceRun, nullptr);
    }
}

/// 点云 CSV 导出（F-905 导出半区）：契约 v2 表头含 sampling-mode 溯源
/// 列、行值携带关节模式 token；导出走既有 exportWriter 缝（路径/内容
/// 由记录缝捕获）＋零修订（命令出口计数为零）。无产物＝如实拒绝。
TEST(PluginPanelLinks, WorkspaceSampling_PointCloudCsvExportV2_F905)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-09"}, std::vector<std::string>{});
    Harness h;
    // 无产物态：如实拒绝（不虚构空文件）。
    EXPECT_NE(exportWorkspacePointCloudCsv(h.services, h.session, "x.csv")
                  .find("无可导出点云"),
              std::string::npos);

    // 关节网格采样（每轴 2 点 ×2 轴＝4 样本）后导出。
    KinWorkspaceSamplingParams params;
    params.mode = WorkspaceSamplingMode::JointSpaceGrid;
    params.jointGridPerAxis = 2;
    KinInlineSamplingContext ctx;
    const KinWorkspaceSamplingFlowResult r =
        runWorkspaceSamplingFlow(h.services, h.session, params, ctx);
    ASSERT_TRUE(r.ok) << r.statusText;

    const std::string status =
        exportWorkspacePointCloudCsv(h.services, h.session, "kin-ws.csv");
    EXPECT_NE(status.find("已导出点云"), std::string::npos) << status;
    EXPECT_NE(status.find("sampling-mode"), std::string::npos) << status;
    EXPECT_EQ(h.exportedPath, "kin-ws.csv");
    // 契约 v2 表头（首行——呈现/测试解析锚）：sampling-mode 列在场；
    // 关节模式行携带 "joint-grid" token（点从哪来的强制溯源）。
    ASSERT_FALSE(h.exportedContent.empty());
    const std::string header =
        h.exportedContent.substr(0, h.exportedContent.find('\n'));
    EXPECT_NE(header.find("sample-index,x-m,y-m,z-m,sampling-mode"),
              std::string::npos)
        << header;
    EXPECT_NE(h.exportedContent.find("joint-grid"), std::string::npos);
    // 数据行数＝样本数 4（表头 1 行＋数据 4 行）。
    const int lines = static_cast<int>(
        std::count(h.exportedContent.begin(), h.exportedContent.end(), '\n'));
    EXPECT_EQ(lines, 5);
    EXPECT_EQ(h.commandSubmitCount, 0);  // 导出零修订（AT-04）
}

/// 样本回写与守卫轨：关节模式样本回写走既有会话回写通道（会话姿态＝
/// 采样构型；零修订——命令出口零触碰）；越界样本域 fail-fast 转状态行
/// 不抛；会话姿态缝未装配＝如实拒绝。
TEST(PluginPanelLinks, WorkspaceSampling_PointWritebackAndGuards_KIN_R2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-09", "KIN-06"},
                  std::vector<std::string>{"AT-04"});
    Harness h;
    // 无产物守卫。
    EXPECT_NE(writebackWorkspacePoint(h.services, h.session, 0)
                  .find("尚未运行采样"),
              std::string::npos);

    KinWorkspaceSamplingParams params;
    params.mode = WorkspaceSamplingMode::JointSpaceRandom;
    params.jointRandomCount = 4;
    KinInlineSamplingContext ctx;
    const KinWorkspaceSamplingFlowResult r =
        runWorkspaceSamplingFlow(h.services, h.session, params, ctx);
    ASSERT_TRUE(r.ok) << r.statusText;
    const std::vector<double> sample0 =
        h.session.lastWorkspaceRun->results.front().jointConfiguration;

    // 回写样本 0：会话姿态＝采样构型（KIN-06 零修订——命令出口零触碰）。
    const std::string ok =
        writebackWorkspacePoint(h.services, h.session, 0);
    EXPECT_NE(ok.find("已更新"), std::string::npos) << ok;
    EXPECT_EQ(h.sessionPose.jointConfiguration(), sample0);
    EXPECT_EQ(h.commandSubmitCount, 0);

    // 越界样本：域 fail-fast 转状态行（不抛——提示轨，F-903 同则）。
    std::string reject;
    EXPECT_NO_THROW({ reject = writebackWorkspacePoint(h.services, h.session, 9999); });
    EXPECT_NE(reject.find("回写被拒"), std::string::npos) << reject;

    // 会话姿态缝未装配：如实拒绝。
    KinPanelServices noPose = h.services;
    noPose.sessionPose = nullptr;
    EXPECT_NE(writebackWorkspacePoint(noPose, h.session, 0)
                  .find("会话姿态未装配"),
              std::string::npos);
}

/// 投影视图 PNG 写出编排（★ KIN-R2 任务6——零 Qt 半区）：四态守卫
/// （无产物/空字节/成功写出/通道未装配）；字节直传不解释（二进制安全
/// 面由 io write(string_view) 承载——此处钉"内容逐字节到缝"的编排
/// 契约；真实 PNG 编码在 gui 半区验证）。
TEST(PluginPanelLinks, WorkspaceSampling_ProjectionPngExportGuards_KIN_R2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-09"}, std::vector<std::string>{});
    Harness h;
    // 无产物态：如实拒绝。
    EXPECT_NE(exportWorkspaceProjectionPng(h.services, h.session, "x.png",
                                           std::string("bytes"))
                  .find("无可导出投影"),
              std::string::npos);
    // 采样后：空字节（呈现面编码异常的误用形态）拒绝；非空字节成功
    // 落缝（路径/内容逐字节捕获——零转码契约）。
    KinWorkspaceSamplingParams params;
    params.mode = WorkspaceSamplingMode::JointSpaceRandom;
    params.jointRandomCount = 3;
    KinInlineSamplingContext ctx;
    const KinWorkspaceSamplingFlowResult r =
        runWorkspaceSamplingFlow(h.services, h.session, params, ctx);
    ASSERT_TRUE(r.ok) << r.statusText;

    EXPECT_NE(exportWorkspaceProjectionPng(h.services, h.session, "x.png",
                                           std::string())
                  .find("画布编码为空"),
              std::string::npos);
    const std::string status = exportWorkspaceProjectionPng(
        h.services, h.session, "kin-ws.png", std::string("\x89PNG-mock", 9));
    EXPECT_NE(status.find("已导出投影视图"), std::string::npos) << status;
    EXPECT_EQ(h.exportedPath, "kin-ws.png");
    EXPECT_EQ(h.exportedContent, std::string("\x89PNG-mock", 9));  // 逐字节
    // 写出缝未装配：如实拒绝。
    KinPanelServices noWriter = h.services;
    noWriter.exportWriter = nullptr;
    EXPECT_NE(exportWorkspaceProjectionPng(noWriter, h.session, "x.png",
                                           std::string("bytes"))
                  .find("写出通道未装配"),
              std::string::npos);
    EXPECT_EQ(h.commandSubmitCount, 0);  // 导出零修订（AT-04）
}

}  // namespace
