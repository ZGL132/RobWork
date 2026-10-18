/**
 * @file   KinP4FlowsTest.cpp
 * @brief  KIN-P4 面板流回归（零 Qt——审查报告 §4 第三条"默认 TCP 三入口
 *         统一经名称解析"与"导出/复算闭环"的编排半区自证）。
 *
 * 设计依据：
 *   - 审查报告 audit/kin-current/运动学模块重新审查-20261016.md §3/§4：
 *     位姿页刷新用空 TcpRef（FK 不会仅凭空 tcpKey 自动找到 toolObject）、
 *     设默认 TCP 传空 TcpRef、正式导出/复算未装配——本文件覆盖修复的
 *     编排面（widget 半区在 kinematics/gui_test/KinHostLoopGuiTest）；
 *   - 夹具：KinFkFixture 六轴黄金模型（sixAxisModel——KIN-P2 同款真实
 *     求解器用例载体）＋TestView 替身；真实 IkSolver/FkEvaluator。
 *
 * 用例与验收映射（审查 §5 KIN-P4 行）：
 *   - TCP 解析单点三态（有工具/无工具/空视图）——三入口统一语义的
 *     唯一实现点 resolveSessionTcp；
 *   - 求解转后台携带目标位姿（solveTargetInBase——SessionSolve 通道
 *     的载荷根因半区，审查 §4 第二条）；
 *   - 复算闭环：原任务定义（seed/referenceQ）自原解集身份显式携带＋
 *     快照重绑定为当前会话锚（buildKinRecomputeRequest 消费——§7.4）。
 *
 * 线程约束：全部编排仅 UI 线程（§3.4——测试在 main 线程直调）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/审查条目追溯

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <sdurws/ird/kinematics/DiagCodes.hpp>   // kKinNoTcp（稳定码——禁字符串拼码）
#include <sdurws/ird/kinematics/Fk.hpp>          // FkEvaluator（真实产品实现）
#include <sdurws/ird/kinematics/Ik.hpp>          // IkSolver/IkRequest（真实求解器）
#include <sdurws/ird/kinematics/KinTypes.hpp>
#include <sdurws/ird/kinematics/SolutionSet.hpp>
#include "KinFkFixture.hpp"              // sixAxisModel/TestView（同单元夹具）
#include "plugin/KinPanelFlows.hpp"      // 被测编排（TCP 解析/求解/复算）
#include "plugin/KinPanelRotation.hpp"   // F-919 RPY↔Rotation3D 私有转换（等价钉被测面）
#include "plugin/KinPanelTypes.hpp"      // 会话态/服务缝

// F-919 等价钉：仅集成模式（sdurw_math 目标存在）编译——独立冒烟树无框架库，
// 调用 RPY::toRotation3D 会把链接缺陷带回测试目标（CMake 侧门控见 CMakeLists）。
#ifdef IRD_HAVE_SDURW_MATH
#include <rw/math/RPY.hpp>
#endif

// ---- 使用声明（与被测代码同一命名空间——用例可读性）--------------------
using namespace sdurws::ird;
using namespace sdurws::ird::kinematics;
using namespace sdurws::ird::kinematics::testfixture;

namespace {

// =====================================================================
// 夹具（六轴模型＋真实求解器/评估器——KIN-P4 闭环的真实计算面）
// =====================================================================

/// 慢求解器替身（转后台路径的确定性触发——固定睡眠后转调真实求解器；
/// 预算注入 1 ms 使任何真实求解都超界）。
class SlowRealSolver final : public IIkSolver {
public:
    explicit SlowRealSolver(const IIkSolver* real) : m_real(real) {}

    IkOutcome solve(const IkRequest& request) const override
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        return m_real->solve(request);
    }

private:
    const IIkSolver* m_real;  ///< 真实求解器（非 owning——产品实例）
};

/// P4 流夹具（六轴黄金模型＋真实求解器——与 gui_test 端到端同一计算面）。
struct P4Harness {
    runtime::CanonicalModel model;   ///< 规范模型（六轴黄金算例——默认工具在册）
    TestView view;                   ///< 宿主注入替身（IKinRuntimeView）
    IkSolver solver;                 ///< 真实求解器（纯函数服务）
    FkEvaluator fk;                  ///< 真实 FK 评估器
    KinSessionPose sessionPose;      ///< 会话姿态（真实容器）
    KinModuleSessionState session;   ///< 插件会话态
    KinPanelServices services;       ///< 服务缝

    P4Harness()
        : model(sixAxisModel()),
          view(model)
    {
        // 会话基线合法（I-KIN-4 seed≥1——装配纪律同则）＋可解析身份。
        session.savedConfig.seed = 42;
        session.savedConfig.regionBudget.seed = 42;
        session.snapshotId =
            core::ContentIdentity::tryFromCanonical(
                "cid-a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1")
                .value_or(core::ContentIdentity{});
        services.modelView = &view;
        services.ikSolver = &solver;
        services.fkEvaluator = &fk;
        services.sessionPose = &sessionPose;
    }
};

/// 六轴零位下的 TCP 目标（真实 FK 产出——精确可达点，IK 从零位收敛）。
rw::math::Transform3D<double> zeroPoseTcpTarget(const P4Harness& h)
{
    const std::vector<double> zeros(6, 0.0);  // 六轴全零（dof=6——黄金模型）
    const auto metrics = h.fk.evaluate(h.view, resolveSessionTcp(h.services).tcp,
                                       zeros);
    return metrics.get().tcpInBase;  // 夹具黄金几何——FK 必成功（fail-fast）
}

}  // namespace

// =====================================================================
// TCP 解析单点（★ KIN-P4 审查 §4——三入口统一语义的唯一实现）
// =====================================================================

/// 有工具快照：解析成功且身份自描述（TcpRef{默认工具对象, 局部名}——
/// 修复前位姿页构造空 TcpRef、设默认 TCP 传 TcpRef{}，解析缺失）。
TEST(KinP4Flows, ResolveSessionTcp_ResolvesDefaultTool_KIN_P4)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-14"}, std::vector<std::string>{});
    P4Harness h;

    const KinTcpResolution tcp = resolveSessionTcp(h.services);
    ASSERT_TRUE(tcp.ok) << tcp.statusText;
    EXPECT_EQ(tcp.tcp.toolObject, h.model.tools().at(0).objectId);
    EXPECT_EQ(tcp.tcp.tcpKey, h.model.tools().at(0).localName);
}

/// 无工具快照：解析失败＝KIN-NO-TCP＋建模侧指引（显式拒绝——绝不以空
/// TcpRef 继续走流；修复前空身份的后果随入口各异：FK 拒评/求解器
/// fail-fast/设默认写悬空引用）。
TEST(KinP4Flows, ResolveSessionTcp_NoToolExplicitReject_KIN_P4)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-14"}, std::vector<std::string>{});
    runtime::CanonicalModel toolless =
        makeModel({revolute("j1", rw::math::Vector3D<double>(0, 0, 1),
                            trans(0, 0, 0), -2.0, 2.0)},
                  trans(0.5, 0, 0), trans(0, 0, 0), /*withTool=*/false);
    TestView view(toolless);
    KinPanelServices services;
    services.modelView = &view;

    const KinTcpResolution tcp = resolveSessionTcp(services);
    EXPECT_FALSE(tcp.ok);
    EXPECT_NE(tcp.statusText.find(kKinNoTcp), std::string::npos)
        << tcp.statusText;
}

/// 求解请求装配复用单点：assembleSessionSolveRequest 的 TCP 与解析单点
/// 同源（修复前两处独立解析——第二份实现的漂移面）。
TEST(KinP4Flows, AssembleSessionSolveRequest_UsesSameTcpResolution_KIN_P4)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-14"}, std::vector<std::string>{});
    P4Harness h;

    const KinSolveRequestAssembly assembly =
        assembleSessionSolveRequest(h.services, h.session);
    ASSERT_TRUE(assembly.ok) << assembly.statusText;
    const KinTcpResolution tcp = resolveSessionTcp(h.services);
    ASSERT_TRUE(tcp.ok);
    EXPECT_EQ(assembly.request.tcp, tcp.tcp);
}

// =====================================================================
// 求解转后台携带目标（★ KIN-P4 审查 §4 第二条——SessionSolve 通道载荷）
// =====================================================================

/// 超预算转后台：请求载荷携带目标位姿（solveTargetInBase——修复前
/// SessionSolve 请求无法还原"求解什么"，通道只能整体拒绝）。
TEST(KinP4Flows, RunSinglePointSolve_DeferredCarriesTarget_KIN_P4)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-02"},
                  std::vector<std::string>{});
    P4Harness h;
    SlowRealSolver slow(&h.solver);
    h.services.ikSolver = &slow;

    KinBackgroundRequest captured;
    h.services.backgroundSubmit = [&captured](const KinBackgroundRequest& bg) {
        captured = bg;
        KinBackgroundAck ack;
        ack.accepted = true;
        ack.taskRef = "kin-solve-test";
        return ack;
    };
    const P4Harness targetHarness;  // 目标取自同模型零位 FK（仅读几何）
    const rw::math::Transform3D<double> target =
        zeroPoseTcpTarget(targetHarness);

    const KinSolveRequestAssembly assembly =
        assembleSessionSolveRequest(h.services, h.session);
    ASSERT_TRUE(assembly.ok);
    const KinSolveFlowResult r = runSinglePointSolve(
        h.services, h.session, target, assembly.request,
        std::chrono::milliseconds(1));  // 预算注入 1 ms——确定性超界
    EXPECT_TRUE(r.deferredToBackground);
    EXPECT_EQ(captured.kind, KinBackgroundKind::SessionSolve);
    ASSERT_TRUE(captured.solveTargetInBase.has_value());
    EXPECT_EQ(*captured.solveTargetInBase, target);  // 载荷＝本次目标位姿
}

// =====================================================================
// 复算闭环（★ KIN-P4 审查 §4——"原任务定义与当前快照重绑定"）
// =====================================================================

/// 复算黄金路径：原解集在场→seed/referenceQ 自原身份显式携带→快照重
/// 绑定为当前会话锚→真实求解→新解集入会话态（身份可观测：snapshotId/
/// seed 换绑与携带对齐）。
TEST(KinP4Flows, RecomputeSession_RebindsIdentityToCurrentSnapshot_KIN_P4)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-08"},
                  std::vector<std::string>{});
    P4Harness h;
    const rw::math::Transform3D<double> target = zeroPoseTcpTarget(h);

    // ---- 原求解（会话级——身份含 seed=42/referenceQ=零位缺省）。
    const KinSolveRequestAssembly assembly =
        assembleSessionSolveRequest(h.services, h.session);
    ASSERT_TRUE(assembly.ok);
    const KinSolveFlowResult solved =
        runSinglePointSolve(h.services, h.session, target, assembly.request);
    ASSERT_FALSE(solved.deferredToBackground);
    ASSERT_TRUE(h.session.lastSessionSolutionSet != nullptr);
    EXPECT_EQ(h.session.lastSessionSolutionSet->requestIdentity.seed, 42U);

    // ---- 改型换绑（模拟重开/新快照：会话锚前移——原解集保留在场）。
    const core::ContentIdentity newSnapshotId =
        core::ContentIdentity::tryFromCanonical(
            "cid-b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2")
            .value_or(core::ContentIdentity{});
    h.session.snapshotId = newSnapshotId;

    // ---- 复算（真实求解器——同键同种子、当前快照锚）。
    const KinRecomputeFlowResult r =
        recomputeSessionFromIdentity(h.services, h.session, target);
    ASSERT_TRUE(r.ok) << r.statusText;
    ASSERT_TRUE(r.recompute.has_value());
    // 复算请求值（域设施产出——快照锚＝当前会话，seed 自原身份携带）。
    EXPECT_EQ(r.recompute->snapshotId, newSnapshotId);
    EXPECT_EQ(r.recompute->seed, 42U);
    EXPECT_EQ(r.recompute->evaluationKey, std::string(kTaskPointIkEvaluationKey));
    // 新解集（会话唯一写点覆盖——身份重绑定可观测）。
    ASSERT_TRUE(h.session.lastSessionSolutionSet != nullptr);
    EXPECT_EQ(h.session.lastSessionSolutionSet->requestIdentity.snapshotId,
              newSnapshotId);
    EXPECT_EQ(h.session.lastSessionSolutionSet->requestIdentity.seed, 42U);
    EXPECT_FALSE(h.session.lastSessionSolutionSet->solutions.empty());
    EXPECT_NE(r.statusText.find("复算"), std::string::npos) << r.statusText;
}

/// 复算拒绝态：无原解集（会话无产物）与空快照锚（未绑定）——如实拒绝
/// 零求解（域设施对空快照 fail-fast 的前置拦截给可读原因）。
TEST(KinP4Flows, RecomputeSession_ExplicitRejectStates_KIN_P4)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-08"}, std::vector<std::string>{});
    P4Harness h;
    const rw::math::Transform3D<double> target = zeroPoseTcpTarget(h);

    // 拒绝态 1：会话无产物（原任务定义不在场）。
    const KinRecomputeFlowResult noOriginal =
        recomputeSessionFromIdentity(h.services, h.session, target);
    EXPECT_FALSE(noOriginal.ok);
    EXPECT_NE(noOriginal.statusText.find("复算不可用"), std::string::npos)
        << noOriginal.statusText;

    // 拒绝态 2：原解集在场但当前会话未绑定快照（空锚——域设施拒绝）。
    IkSolutionSet seedSet;
    seedSet.requestIdentity.seed = 5;
    h.session.lastSessionSolutionSet =
        std::make_shared<const IkSolutionSet>(seedSet);
    h.session.snapshotId = core::ContentIdentity{};  // 未绑定
    const KinRecomputeFlowResult noSnapshot =
        recomputeSessionFromIdentity(h.services, h.session, target);
    EXPECT_FALSE(noSnapshot.ok);
    EXPECT_NE(noSnapshot.statusText.find("快照"), std::string::npos)
        << noSnapshot.statusText;
}

// F-919 等价钉（仅集成模式编译；独立冒烟树无框架库，见 CMakeLists 门控）：
// KinPanelRotation 以 Eigen 头文件轴角组合（正向）与 RPY.cpp 同式闭式解（反向）
// 替代 rw::math::RPY 的非内联符号。本用例对正向/反向逐元对照 rw 实现，
// 角度组覆盖零位、三轴单项、混合角与 ±90° 万向锁——顺序/符号/epsilon 分支
// 任一漂移都会在此变红；独立冒烟该用例编译为空，不把非内联符号带回链接面。
#ifdef IRD_HAVE_SDURW_MATH
TEST(KinPanelRotation, MatchesRwMathBothDirections_KIN_F919)
{
    const double cases[][3] = {
        {0.0, 0.0, 0.0},          // 恒等（零位）
        {0.7, 0.0, 0.0},          // 仅 roll（rw 语义：绕 z）
        {0.0, 0.7, 0.0},          // 仅 pitch（绕 y）
        {0.0, 0.0, 0.7},          // 仅 yaw（绕 x）
        {0.3, -0.4, 0.5},         // 混合角（顺序敏感——Z·Y·X 与 X·Y·Z 在此不等）
        {0.2, 1.5707963267948966, -0.3},   // +90° 万向锁邻域
        {-0.2, -1.5707963267948966, 0.3},  // -90° 万向锁邻域
    };
    for (const auto& a : cases) {
        const rw::math::Rotation3D<double> ours = rotationFromRpySi(a[0], a[1], a[2]);
        const rw::math::Rotation3D<double> ref =
            rw::math::RPY<double>(a[0], a[1], a[2]).toRotation3D();
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                EXPECT_NEAR(ours(r, c), ref(r, c), 1e-12)
                    << "forward rpy=(" << a[0] << "," << a[1] << "," << a[2]
                    << ") at (" << r << "," << c << ")";
            }
        }
        // 反向：矩阵→RPY（含万向锁 epsilon 分支）与 rw 实现同式对照。
        const std::array<double, 3> oursRpy = rpyFromRotationSi(ref);
        const rw::math::RPY<double> refRpy(ref);
        for (int k = 0; k < 3; ++k) {
            EXPECT_NEAR(oursRpy[k], refRpy[k], 1e-12)
                << "inverse rpy=(" << a[0] << "," << a[1] << "," << a[2]
                << ") component " << k;
        }
    }
}
#endif  // IRD_HAVE_SDURW_MATH
