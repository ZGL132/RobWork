/**
 * @file   CloseFlowTest.cpp
 * @brief  关闭/切换/退出统一确认编排的模型测试（WP-22-T06——units/
 *         workflow.md §7.3 编排核的直调半区；PM-03/PM-12/ARCH §6.8 A7）。
 *
 * 设计依据：
 *   - units/workflow.md §7.3（流程图 REQ→D1→C1→D2→C2→DRAIN→SWITCH/EXIT
 *     逐步兑现＋三条注记：等待语义 A7／任务清单短标签数据源＝execution
 *     九态〔词表归 core，workflow 只取数〕／PM-12 会话选择零写入）、
 *     §11.2（用例 209 的数据面承载半区——对话框流程＝GUI〔设计〕，
 *     决策词表/呈现数据/编排轨迹在此以桩端口直调承载）、§10.3（错误
 *     二分：调用方错误 WorkflowError fail-fast；环境/对端错误值轨道），
 *     §14.1 D-WF-6（宿主面归 ui——本测试以桩决策端口替代对话框）
 *   - REQUIREMENTS.md §17 PM-03 原文（三选＋二选＋9 态短标签清单；切换
 *     ＝关闭后候选验证成功才切上下文；退出复用同一流程，取消可中止）、
 *     PM-12 原文（分支切换前先处置未应用草稿〔PM-04 规则〕；URDF 基线
 *     修订只读，编辑只发生在方案分支）、AT-20/AT-21
 *   - ARCHITECTURE.md §6.8 A7（存储上下文与锁的生命周期：界面会话与
 *     存储上下文分离——"等待"选项即等待在途归档＋草稿落盘完成；协作
 *     取消亦经归档检查点后结束）
 *   - 任务契约 tasks/foundation/WP-22-T06.json acceptance 1（三选＋二选
 *     ＋9 态短标签＋取消可中止——PM-03 用例）
 *
 * 测试形态（§11.0——模型测试＝直调计算库纯函数面）：编排核为静态函数、
 * 端口全部桩化（决策脚本化、草稿/排空计数化、存储上下文最小桩）——
 * 零磁盘写入、零真实对端。真实落盘半区（候选验证成功移交、零写入的
 * HEAD 字节复核）在契约测试 CloseFlowContractTest.cpp（跨单元联合）。
 *
 * 追溯约定（AGENTS.md §2.7）：用例名与断言注释标注需求/AT 编号。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/Evaluation.hpp>  // core::TaskState 九态＋toToken 冻结表
#include <sdurws/ird/core/Identity.hpp>    // core::ProjectId/BranchId（强类型构造）
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记
#include <sdurws/ird/workflow/Lifecycle.hpp>

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace sdurws::ird;
using workflow::CloseDialogData;
using workflow::CloseFlowOutcome;
using workflow::CloseKind;
using workflow::DraftDisposition;
using workflow::RunningTaskDecision;
using workflow::SchemeBranchSwitchOutcome;

// =====================================================================
// 桩：最小存储上下文（编排核只消费 requestClose/closed——其余接口诚实
// 拒绝：调用即测试违约，fail-fast 暴露而非静默返回假数据）。
// =====================================================================

class CloseStoreStub final : public project::ProjectStore {
public:
    int requestCloseCalls = 0;///< requestClose 调用计数（零关闭断言面）
    bool closedState = false; ///< Closed 态（requestClose 即置位——桩内同步排空）

    bool writable() const noexcept override { return !closedState; }
    project::LockInfo lockInfo() const override { return project::LockInfo{}; }
    core::ProjectId projectId() const noexcept override { return core::ProjectId{}; }
    project::SchemaInfo schema() const override { return project::SchemaInfo{}; }
    std::filesystem::path canonicalPath() const override { return {}; }

    // ---- 编排核不消费的端口引用（调用＝测试违约——诚实拒绝）----
    project::IProjectQueryPort& query() const noexcept override
    {
        std::abort();  // 不可达：编排核零查询调用（见类注）
    }
    project::ProjectCommandService& commands() const noexcept override
    {
        std::abort();
    }
    project::DraftService& drafts() const noexcept override { std::abort(); }
    project::UndoRedoService& undoRedo() const noexcept override { std::abort(); }
    project::IResultArchivePort& archive() const noexcept override { std::abort(); }

    // ---- 关闭协议（编排核消费面——计数＋同步置位）----
    std::uint32_t requestClose() override
    {
        ++requestCloseCalls;
        closedState = true;  // 桩内无在途引用——同步完成排空（pending==0 收尾）
        return 0;
    }
    bool closed() const noexcept override { return closedState; }
    void subscribeClose(project::ICloseObserver&) override {}
};

// =====================================================================
// 桩：决策收集（脚本化回答＋呈现数据捕获——对话框数据面断言）
// =====================================================================

class DecisionStub final : public workflow::ICloseDecisionPort {
public:
    workflow::DraftDisposition draftAnswer = DraftDisposition::Cancel;///< 草稿三选脚本
    workflow::RunningTaskDecision taskAnswer =
        RunningTaskDecision::CancelFlow;                              ///< 任务二选脚本
    std::vector<CloseDialogData> draftPrompts;  ///< 收到的草稿决策点呈现数据
    std::vector<CloseDialogData> taskPrompts;   ///< 收到的任务决策点呈现数据

    workflow::DraftDisposition collectDraftDisposition(
        const CloseDialogData& data) override
    {
        draftPrompts.push_back(data);
        return draftAnswer;
    }
    workflow::RunningTaskDecision collectRunningTaskDecision(
        const CloseDialogData& data) override
    {
        taskPrompts.push_back(data);
        return taskAnswer;
    }
};

// =====================================================================
// 桩：草稿处置（清单脚本＋动作计数与成败脚本）
// =====================================================================

class DraftPortStub final : public workflow::ICloseDraftPort {
public:
    std::vector<std::string> modules;///< 未应用草稿模块清单（空＝无草稿）
    bool saveOk = true;              ///< saveDrafts 成败脚本
    bool discardOk = true;           ///< discardDrafts 成败脚本
    int listCalls = 0;
    int saveCalls = 0;
    int discardCalls = 0;

    std::vector<std::string> unappliedDraftModules() override
    {
        ++listCalls;
        return modules;
    }
    bool saveDrafts() override
    {
        ++saveCalls;
        return saveOk;
    }
    bool discardDrafts() override
    {
        ++discardCalls;
        return discardOk;
    }
};

// =====================================================================
// 桩：任务排空（活动判定/清单/两分支成败与计数）
// =====================================================================

class DrainPortStub final : public workflow::ICloseDrainPort {
public:
    bool active = false;                    ///< hasActiveTask 脚本
    std::vector<core::TaskState> states;    ///< taskStates 脚本（九态透传）
    bool waitOk = true;                     ///< waitDrain 成败脚本
    bool cancelOk = true;                   ///< cooperativeCancel 成败脚本
    int activeCalls = 0;
    int listCalls = 0;
    int waitCalls = 0;
    int cancelCalls = 0;

    bool hasActiveTask(core::ProjectId) override
    {
        ++activeCalls;
        return active;
    }
    std::vector<core::TaskState> taskStates(core::ProjectId) override
    {
        ++listCalls;
        return states;
    }
    bool waitDrain() override
    {
        ++waitCalls;
        return waitOk;
    }
    bool cooperativeCancel() override
    {
        ++cancelCalls;
        return cancelOk;
    }
};

// =====================================================================
// 组装辅助：默认请求（三桩＋空项目身份）
// =====================================================================

struct FlowRig {
    CloseStoreStub store;
    DecisionStub decisions;
    DraftPortStub drafts;
    DrainPortStub drain;

    workflow::CloseFlowRequest request()
    {
        workflow::CloseFlowRequest r;
        r.decisions = &decisions;
        r.drafts = &drafts;
        r.drain = &drain;
        return r;
    }
};

/// 肯定不存在的候选路径（Switch 失败路径用——临时目录下的独名子路径，
/// 本测试零创建、零残留）。
std::filesystem::path missingCandidatePath()
{
    return std::filesystem::temp_directory_path()
           / "ird-wf-close-model-no-such-candidate";
}

// =====================================================================
// 词表：场景键与九态短标签（PM-03 对话框数据面）
// =====================================================================

/**
 * 场景键词表（PM-03 对话框上下文——UX-02 键半区）：CloseKind 三值与
 * 方案分支切换场景键的词形逐字断言；词表外值防御性返回空串（不伪造键）。
 */
TEST(WfClose, ScenarioKeyVocabulary)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-03"}, std::vector<std::string>{});

    EXPECT_EQ(workflow::closeScenarioKey(CloseKind::Close), "close.flow.close");
    EXPECT_EQ(workflow::closeScenarioKey(CloseKind::Switch), "close.flow.switch");
    EXPECT_EQ(workflow::closeScenarioKey(CloseKind::Exit), "close.flow.exit");
    EXPECT_EQ(std::string(workflow::kSchemeSwitchScenarioKey),
              "close.flow.scheme-switch");
}

/**
 * 九态短标签（PM-03"对话框内嵌任务清单（9 态短标签）"——§7.3 注二：
 * 数据源＝execution 九态任务状态，词表归 core，workflow 只取数呈现）：
 * 逐态断言与 core::toToken 冻结表逐字一致〔queued/preparing/running/
 * paused/canceling/canceled/completed/failed/interrupted〕；保序（输入序
 * 即输出序）与空清单语义。
 */
TEST(WfClose, TaskStateShortLabelsAllNineStates)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-03"}, std::vector<std::string>{"AT-21"});

    // 九态冻结序（core::TaskState 枚举序）逐项对照 toToken 词形——零新增
    // 状态词（SA-12/D-WF-4）。
    const std::vector<core::TaskState> nine = {
        core::TaskState::Queued,     core::TaskState::Preparing,
        core::TaskState::Running,    core::TaskState::Paused,
        core::TaskState::Canceling,  core::TaskState::Canceled,
        core::TaskState::Completed,  core::TaskState::Failed,
        core::TaskState::Interrupted,
    };
    const std::vector<std::string> expected = {
        "queued", "preparing", "running", "paused", "canceling",
        "canceled", "completed", "failed", "interrupted",
    };
    const std::vector<std::string> actual =
        workflow::closeTaskStateTokens(nine);
    ASSERT_EQ(actual.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(actual[i], expected[i]) << "九态第 " << i << " 位短标签";
    }

    // 保序与空清单：短标签清单与输入逐位对应（含重复）；空输入＝空输出。
    const std::vector<std::string> reordered = workflow::closeTaskStateTokens(
        {core::TaskState::Running, core::TaskState::Queued,
         core::TaskState::Running});
    ASSERT_EQ(reordered.size(), 3u);
    EXPECT_EQ(reordered[0], "running");
    EXPECT_EQ(reordered[1], "queued");
    EXPECT_EQ(reordered[2], "running");
    EXPECT_TRUE(workflow::closeTaskStateTokens({}).empty());
}

// =====================================================================
// 主干：无草稿无任务（D1/D2 均"否"——零决策调用直达排空关闭）
// =====================================================================

/**
 * 无草稿无任务（§7.3 流程 D1"否"→D2"否"→DRAIN）：零决策调用（对话框
 * 不弹出）＋幂等排空仍执行（D2"否"边直达 DRAIN 节点）＋存储上下文关闭
 * （requestClose 一次、Closed 置位）＋Proceed——PM-03 关闭主干。
 */
TEST(WfClose, NoDraftNoTaskProceedsAndClosesStore)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-03"}, std::vector<std::string>{"AT-20"});

    FlowRig rig;
    const auto req = rig.request();
    const auto outcome = workflow::CloseFlow::run(CloseKind::Close, rig.store, req);

    ASSERT_EQ(outcome.result, CloseFlowOutcome::Result::Proceed);
    EXPECT_EQ(outcome.abortedAt, CloseFlowOutcome::AbortStage::None);
    EXPECT_FALSE(outcome.failure.has_value());
    EXPECT_EQ(outcome.candidateStore, nullptr);

    // 零决策调用（无草稿无任务——对话框不弹出）。
    EXPECT_TRUE(rig.decisions.draftPrompts.empty());
    EXPECT_TRUE(rig.decisions.taskPrompts.empty());
    // 幂等排空仍执行（DRAIN 节点——等待分支）＋存储上下文关闭。
    EXPECT_EQ(rig.drain.waitCalls, 1);
    EXPECT_EQ(rig.drain.cancelCalls, 0);
    EXPECT_EQ(rig.store.requestCloseCalls, 1);
    EXPECT_TRUE(rig.store.closed());
    EXPECT_TRUE(outcome.waitedForArchiveDrain);
    EXPECT_FALSE(outcome.cooperativeCancelled);
}

// =====================================================================
// 草稿三选（D1/C1——保存/放弃/取消；取消可中止整个流程 AT-20/21）
// =====================================================================

/**
 * 草稿三选之"取消"（PM-03"取消可中止"——AT-20/21）：中止发生在草稿点
 * （Aborted{DraftPrompt}），排空与存储关闭零发生（当前项目原状）、草稿
 * 动作零调用（保留原状）；决策点呈现数据携带草稿模块清单与场景键
 * （对话框"将丢失哪些编辑"的呈现材料）。
 */
TEST(WfClose, DraftCancelAbortsWholeFlow)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-03"}, std::vector<std::string>{"AT-20", "AT-21"});

    FlowRig rig;
    rig.drafts.modules = {"requirements", "kinematics"};
    rig.decisions.draftAnswer = DraftDisposition::Cancel;
    const auto req = rig.request();
    const auto outcome = workflow::CloseFlow::run(CloseKind::Close, rig.store, req);

    ASSERT_EQ(outcome.result, CloseFlowOutcome::Result::Aborted);
    EXPECT_EQ(outcome.abortedAt, CloseFlowOutcome::AbortStage::DraftPrompt);
    EXPECT_FALSE(outcome.failure.has_value());

    // 中止语义：草稿动作零调用（原状）、排空零调用、存储关闭零发生。
    EXPECT_EQ(rig.drafts.saveCalls, 0);
    EXPECT_EQ(rig.drafts.discardCalls, 0);
    EXPECT_EQ(rig.drain.waitCalls, 0);
    EXPECT_EQ(rig.drain.cancelCalls, 0);
    EXPECT_EQ(rig.store.requestCloseCalls, 0);
    EXPECT_FALSE(rig.store.closed());

    // 决策点呈现数据（C1 对话框材料）：草稿清单透传＋场景键＋kind。
    ASSERT_EQ(rig.decisions.draftPrompts.size(), 1u);
    EXPECT_EQ(rig.decisions.draftPrompts[0].draftModules,
              (std::vector<std::string>{"requirements", "kinematics"}));
    EXPECT_EQ(rig.decisions.draftPrompts[0].scenarioKey, "close.flow.close");
    EXPECT_EQ(rig.decisions.draftPrompts[0].kind, CloseKind::Close);
    EXPECT_TRUE(rig.decisions.draftPrompts[0].taskStates.empty());
    // 任务决策点未到达（中止在前）。
    EXPECT_TRUE(rig.decisions.taskPrompts.empty());
}

/**
 * 草稿三选之"保存草稿"（PM-04 保存语义——仅落 drafts/ 零修订）：保存
 * 动作恰好一次，流程继续至排空与关闭（Proceed）——三选不改变主干。
 */
TEST(WfClose, DraftSaveContinuesToDrainAndClose)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-03"}, std::vector<std::string>{"AT-21"});

    FlowRig rig;
    rig.drafts.modules = {"requirements"};
    rig.decisions.draftAnswer = DraftDisposition::Save;
    const auto req = rig.request();
    const auto outcome = workflow::CloseFlow::run(CloseKind::Close, rig.store, req);

    ASSERT_EQ(outcome.result, CloseFlowOutcome::Result::Proceed);
    EXPECT_EQ(rig.drafts.saveCalls, 1);
    EXPECT_EQ(rig.drafts.discardCalls, 0);
    EXPECT_EQ(rig.store.requestCloseCalls, 1);
    EXPECT_TRUE(rig.store.closed());
}

/**
 * 草稿三选之"放弃"（用户显式丢弃未应用修改）：放弃动作恰好一次，流程
 * 继续至排空与关闭（Proceed）。
 */
TEST(WfClose, DraftDiscardContinuesToDrainAndClose)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-03"}, std::vector<std::string>{"AT-21"});

    FlowRig rig;
    rig.drafts.modules = {"trajectory"};
    rig.decisions.draftAnswer = DraftDisposition::Discard;
    const auto req = rig.request();
    const auto outcome = workflow::CloseFlow::run(CloseKind::Close, rig.store, req);

    ASSERT_EQ(outcome.result, CloseFlowOutcome::Result::Proceed);
    EXPECT_EQ(rig.drafts.discardCalls, 1);
    EXPECT_EQ(rig.drafts.saveCalls, 0);
    EXPECT_EQ(rig.store.requestCloseCalls, 1);
    EXPECT_TRUE(rig.store.closed());
}

/**
 * 草稿动作失败（环境/对端错误——端口布尔轨道 false）：Failed＋UX-03
 * 三字段齐备，且排空与存储关闭零发生（失败不带病关闭——错误语义二分，
 * §10.3）。
 */
TEST(WfClose, DraftActionFailureFailsWithoutClosing)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-03"}, std::vector<std::string>{});

    FlowRig rig;
    rig.drafts.modules = {"requirements"};
    rig.decisions.draftAnswer = DraftDisposition::Save;
    rig.drafts.saveOk = false;
    const auto req = rig.request();
    const auto outcome = workflow::CloseFlow::run(CloseKind::Close, rig.store, req);

    ASSERT_EQ(outcome.result, CloseFlowOutcome::Result::Failed);
    ASSERT_TRUE(outcome.failure.has_value());
    EXPECT_FALSE(outcome.failure->context.empty());
    EXPECT_FALSE(outcome.failure->cause.empty());
    EXPECT_FALSE(outcome.failure->recommendedAction.empty());
    EXPECT_EQ(rig.store.requestCloseCalls, 0);
    EXPECT_FALSE(rig.store.closed());
    EXPECT_EQ(rig.drain.waitCalls, 0);
}

// =====================================================================
// 任务二选（D2/C2——等待/协作取消/取消流程；9 态清单呈现）
// =====================================================================

/**
 * 任务决策点呈现数据（PM-03"对话框内嵌任务清单（9 态短标签）"）：
 * taskStates 九态透传零加工、场景键与 kind 正确、草稿字段为空（决策点
 * 裁剪——零占位行）。
 */
TEST(WfClose, TaskPromptCarriesNineStateList)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-03"}, std::vector<std::string>{"AT-21"});

    FlowRig rig;
    rig.drain.active = true;
    rig.drain.states = {core::TaskState::Running, core::TaskState::Queued,
                        core::TaskState::Paused};
    rig.decisions.taskAnswer = RunningTaskDecision::CancelFlow;
    const auto req = rig.request();
    (void)workflow::CloseFlow::run(CloseKind::Exit, rig.store, req);

    ASSERT_EQ(rig.decisions.taskPrompts.size(), 1u);
    EXPECT_EQ(rig.decisions.taskPrompts[0].taskStates,
              (std::vector<core::TaskState>{core::TaskState::Running,
                                            core::TaskState::Queued,
                                            core::TaskState::Paused}));
    EXPECT_EQ(rig.decisions.taskPrompts[0].scenarioKey, "close.flow.exit");
    EXPECT_EQ(rig.decisions.taskPrompts[0].kind, CloseKind::Exit);
    EXPECT_TRUE(rig.decisions.taskPrompts[0].draftModules.empty());
}

/**
 * 任务二选之"等待"（ARCH §6.8 A7"等待"选项——等待在途归档＋草稿落盘
 * 完成）：等待分支排空恰好一次（协作取消零调用）、随后存储上下文关闭
 * （Proceed）——等待覆盖在途归档的编排面证据。
 */
TEST(WfClose, TaskWaitDrainsThenCloses)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-03", "TASK-03"}, std::vector<std::string>{"AT-21"});

    FlowRig rig;
    rig.drain.active = true;
    rig.decisions.taskAnswer = RunningTaskDecision::Wait;
    const auto req = rig.request();
    const auto outcome = workflow::CloseFlow::run(CloseKind::Close, rig.store, req);

    ASSERT_EQ(outcome.result, CloseFlowOutcome::Result::Proceed);
    EXPECT_EQ(rig.drain.waitCalls, 1);
    EXPECT_EQ(rig.drain.cancelCalls, 0);
    EXPECT_TRUE(outcome.waitedForArchiveDrain);
    EXPECT_FALSE(outcome.cooperativeCancelled);
    EXPECT_EQ(rig.store.requestCloseCalls, 1);
    EXPECT_TRUE(rig.store.closed());
}

/**
 * 任务二选之"协作取消"（§7.3 CANCEL 节点——逐任务 requestCancel→排空，
 * 取消即清理临时区〔承载归 execution 取消协议〕）：协作取消恰好一次且
 * 免重复调度排空（取消分支已含排空）、随后存储上下文关闭（Proceed）＋
 * 观测位登记（AT-21 承接证据）。
 */
TEST(WfClose, TaskCooperativeCancelCancelsAndDrains)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-03"}, std::vector<std::string>{"AT-21"});

    FlowRig rig;
    rig.drain.active = true;
    rig.decisions.taskAnswer = RunningTaskDecision::CooperativeCancel;
    const auto req = rig.request();
    const auto outcome = workflow::CloseFlow::run(CloseKind::Close, rig.store, req);

    ASSERT_EQ(outcome.result, CloseFlowOutcome::Result::Proceed);
    EXPECT_EQ(rig.drain.cancelCalls, 1);
    EXPECT_EQ(rig.drain.waitCalls, 0) << "协作取消分支已含排空——免重复 shutdown";
    EXPECT_TRUE(outcome.cooperativeCancelled);
    EXPECT_FALSE(outcome.waitedForArchiveDrain);
    EXPECT_EQ(rig.store.requestCloseCalls, 1);
    EXPECT_TRUE(rig.store.closed());
}

/**
 * 任务二选之"取消流程"（§7.3 C2→ABORT 边——PM-03"取消可中止"在任务
 * 决策点）：中止发生在任务点（Aborted{TaskPrompt}），草稿已按用户决策
 * 处置（编排到达顺序——D1 在 D2 前）、排空与存储关闭零发生。
 */
TEST(WfClose, TaskCancelFlowAbortsAfterDraftStage)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-03"}, std::vector<std::string>{"AT-21"});

    FlowRig rig;
    rig.drafts.modules = {"optimization"};
    rig.decisions.draftAnswer = DraftDisposition::Discard;
    rig.drain.active = true;
    rig.decisions.taskAnswer = RunningTaskDecision::CancelFlow;
    const auto req = rig.request();
    const auto outcome = workflow::CloseFlow::run(CloseKind::Close, rig.store, req);

    ASSERT_EQ(outcome.result, CloseFlowOutcome::Result::Aborted);
    EXPECT_EQ(outcome.abortedAt, CloseFlowOutcome::AbortStage::TaskPrompt);
    EXPECT_EQ(rig.drafts.discardCalls, 1) << "草稿点在前——已按用户决策处置";
    EXPECT_EQ(rig.drain.waitCalls, 0);
    EXPECT_EQ(rig.drain.cancelCalls, 0);
    EXPECT_EQ(rig.store.requestCloseCalls, 0);
    EXPECT_FALSE(rig.store.closed());
}

/**
 * 协作取消失败（排空未在承诺窗口完成——端口布尔轨道 false）：Failed＋
 * UX-03 三字段，存储关闭零发生（不带病关闭）。
 */
TEST(WfClose, CooperativeCancelFailureFailsWithoutClosing)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-03"}, std::vector<std::string>{"AT-21"});

    FlowRig rig;
    rig.drain.active = true;
    rig.decisions.taskAnswer = RunningTaskDecision::CooperativeCancel;
    rig.drain.cancelOk = false;
    const auto req = rig.request();
    const auto outcome = workflow::CloseFlow::run(CloseKind::Close, rig.store, req);

    ASSERT_EQ(outcome.result, CloseFlowOutcome::Result::Failed);
    ASSERT_TRUE(outcome.failure.has_value());
    EXPECT_FALSE(outcome.failure->cause.empty());
    EXPECT_EQ(rig.store.requestCloseCalls, 0);
    EXPECT_FALSE(rig.store.closed());
}

/**
 * 等待排空失败（在途运行未全部终结——端口布尔轨道 false）：Failed＋
 * UX-03 三字段，存储关闭零发生。
 */
TEST(WfClose, WaitDrainFailureFailsWithoutClosing)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-03"}, std::vector<std::string>{});

    FlowRig rig;
    rig.drain.waitOk = false;
    const auto req = rig.request();
    const auto outcome = workflow::CloseFlow::run(CloseKind::Close, rig.store, req);

    ASSERT_EQ(outcome.result, CloseFlowOutcome::Result::Failed);
    ASSERT_TRUE(outcome.failure.has_value());
    EXPECT_FALSE(outcome.failure->cause.empty());
    EXPECT_EQ(rig.store.requestCloseCalls, 0);
    EXPECT_FALSE(rig.store.closed());
}

// =====================================================================
// 退出复用同一流程（PM-03 原文——Exit 与 Close 同路径）
// =====================================================================

/**
 * 退出复用同一流程（PM-03"退出复用同一流程"）：kind=Exit 与 kind=Close
 * 在同桩同决策脚本下产出同结果、同决策点次数、同动作计数、同观测位——
 * kind 差异只体现在呈现数据（场景键 close.flow.exit）。
 */
TEST(WfClose, ExitReusesSameFlowAsClose)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-03"}, std::vector<std::string>{"AT-20"});

    // Close 基线：有草稿（保存）＋有任务（协作取消）。
    FlowRig closeRig;
    closeRig.drafts.modules = {"requirements"};
    closeRig.decisions.draftAnswer = DraftDisposition::Save;
    closeRig.drain.active = true;
    closeRig.decisions.taskAnswer = RunningTaskDecision::CooperativeCancel;
    const auto closeReq = closeRig.request();
    const auto closeOutcome =
        workflow::CloseFlow::run(CloseKind::Close, closeRig.store, closeReq);

    // Exit 同构输入：仅 kind 不同。
    FlowRig exitRig;
    exitRig.drafts.modules = {"requirements"};
    exitRig.decisions.draftAnswer = DraftDisposition::Save;
    exitRig.drain.active = true;
    exitRig.decisions.taskAnswer = RunningTaskDecision::CooperativeCancel;
    const auto exitReq = exitRig.request();
    const auto exitOutcome =
        workflow::CloseFlow::run(CloseKind::Exit, exitRig.store, exitReq);

    // 同结果、同观测位、同动作计数（退出无第二套流程）。
    EXPECT_EQ(exitOutcome.result, closeOutcome.result);
    EXPECT_EQ(exitOutcome.result, CloseFlowOutcome::Result::Proceed);
    EXPECT_EQ(exitOutcome.abortedAt, closeOutcome.abortedAt);
    EXPECT_EQ(exitOutcome.cooperativeCancelled,
              closeOutcome.cooperativeCancelled);
    EXPECT_EQ(exitOutcome.waitedForArchiveDrain,
              closeOutcome.waitedForArchiveDrain);
    EXPECT_EQ(exitRig.drafts.saveCalls, closeRig.drafts.saveCalls);
    EXPECT_EQ(exitRig.drain.cancelCalls, closeRig.drain.cancelCalls);
    EXPECT_EQ(exitRig.store.requestCloseCalls, closeRig.store.requestCloseCalls);
    // 场景键随 kind 登记（呈现上下文的唯一差异）。
    ASSERT_EQ(exitRig.decisions.draftPrompts.size(), 1u);
    EXPECT_EQ(exitRig.decisions.draftPrompts[0].scenarioKey, "close.flow.exit");
}

// =====================================================================
// 切换：候选验证失败不动当前项目（SWITCH 节点——编排序保证）
// =====================================================================

/**
 * 切换候选验证失败（§7.3 SWITCH 节点"验证失败不动当前项目"）：候选
 * 路径不存在 → open ①兜底失败（not-a-project）→ Failed＋UX-03 呈现，
 * 且当前 store 的关闭零发生（requestClose 未调、closed 为假——编排序
 * 保证：候选验证先于存储上下文排空）。真实落盘双项目复核在契约测试
 * （CloseFlowContractTest）。
 */
TEST(WfClose, SwitchCandidateFailureLeavesCurrentUntouched)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-03"}, std::vector<std::string>{"AT-20"});

    FlowRig rig;
    workflow::CloseFlowRequest req = rig.request();
    req.candidatePath = missingCandidatePath();
    const auto outcome =
        workflow::CloseFlow::run(CloseKind::Switch, rig.store, req);

    ASSERT_EQ(outcome.result, CloseFlowOutcome::Result::Failed);
    ASSERT_TRUE(outcome.failure.has_value());
    EXPECT_FALSE(outcome.failure->cause.empty());
    EXPECT_FALSE(outcome.failure->recommendedAction.empty());
    EXPECT_EQ(outcome.candidateStore, nullptr);
    // 不动当前项目：排空/关闭零发生——存储上下文保持 Active。
    EXPECT_EQ(rig.store.requestCloseCalls, 0);
    EXPECT_FALSE(rig.store.closed());
    EXPECT_EQ(rig.drain.waitCalls, 1) << "调度排空在候选验证之前已完成（流程图 DRAIN→SWITCH）";
}

// =====================================================================
// 前置校验（调用方装配违约 fail-fast——WorkflowError）
// =====================================================================

/**
 * 装配违约 fail-fast（§10.3 错误二分——调用方错误）：Switch 缺候选路径、
 * 非 Switch 携带候选路径、三端口任一为空，均抛 WorkflowError 且零副作用
 * （零决策/零排空/零关闭）。
 */
TEST(WfClose, PreconditionViolationsFailFast)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-03"}, std::vector<std::string>{});

    // Switch 缺候选路径。
    {
        FlowRig rig;
        const auto req = rig.request();
        EXPECT_THROW((void)workflow::CloseFlow::run(CloseKind::Switch, rig.store, req),
                     workflow::WorkflowError);
        EXPECT_EQ(rig.store.requestCloseCalls, 0);
    }
    // 非 Switch 携带候选路径。
    {
        FlowRig rig;
        auto req = rig.request();
        req.candidatePath = missingCandidatePath();
        EXPECT_THROW((void)workflow::CloseFlow::run(CloseKind::Close, rig.store, req),
                     workflow::WorkflowError);
        EXPECT_EQ(rig.store.requestCloseCalls, 0);
    }
    // 端口缺失（逐个）。
    {
        FlowRig rig;
        auto req = rig.request();
        req.decisions = nullptr;
        EXPECT_THROW((void)workflow::CloseFlow::run(CloseKind::Close, rig.store, req),
                     workflow::WorkflowError);
    }
    {
        FlowRig rig;
        auto req = rig.request();
        req.drafts = nullptr;
        EXPECT_THROW((void)workflow::CloseFlow::run(CloseKind::Close, rig.store, req),
                     workflow::WorkflowError);
    }
    {
        FlowRig rig;
        auto req = rig.request();
        req.drain = nullptr;
        EXPECT_THROW((void)workflow::CloseFlow::run(CloseKind::Close, rig.store, req),
                     workflow::WorkflowError);
    }
}

// =====================================================================
// 方案分支切换（PM-12——零写入会话选择；前置/取消/批准）
// =====================================================================

/**
 * 方案分支切换前置 fail-fast（PM-12 装配违约）：目标分支与当前分支相同
 * （含空目标）抛 WorkflowError——同分支切换属宿主装配缺陷（分支清单
 * 呈现层应禁用当前分支项），零写入承诺不覆盖无意义调用。
 */
TEST(WfClose, SchemeSwitchPreconditionViolationsFailFast)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-12"}, std::vector<std::string>{});

    FlowRig rig;
    DecisionStub& decisions = rig.decisions;
    DraftPortStub& drafts = rig.drafts;

    workflow::SchemeBranchSwitchRequest same;
    same.currentBranch = core::BranchId::fromCanonical("brn-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    same.targetBranch = same.currentBranch;
    EXPECT_THROW((void)workflow::SchemeBranchSwitchFlow::run(same, decisions, drafts),
                 workflow::WorkflowError);

    workflow::SchemeBranchSwitchRequest empty;
    empty.currentBranch = core::BranchId::fromCanonical("brn-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    // targetBranch 缺省（空规范文本）。
    EXPECT_THROW((void)workflow::SchemeBranchSwitchFlow::run(empty, decisions, drafts),
                 workflow::WorkflowError);
    EXPECT_EQ(decisions.draftPrompts.size(), 0u);
}

/**
 * 方案分支切换之草稿三选"取消"（PM-12→PM-04 规则——分支切换前先处置
 * 未应用草稿）：取消＝切换零发生（Aborted），决策点呈现携带方案切换
 * 场景键（kSchemeSwitchScenarioKey）与 Switch kind（对话框上下文与
 * 项目切换区分）。
 */
TEST(WfClose, SchemeSwitchDraftCancelAborts)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-12"}, std::vector<std::string>{"AT-21"});

    FlowRig rig;
    rig.drafts.modules = {"requirements"};
    rig.decisions.draftAnswer = DraftDisposition::Cancel;

    workflow::SchemeBranchSwitchRequest req;
    req.currentBranch = core::BranchId::fromCanonical("brn-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    req.targetBranch = core::BranchId::fromCanonical("brn-bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");

    const auto outcome =
        workflow::SchemeBranchSwitchFlow::run(req, rig.decisions, rig.drafts);

    ASSERT_EQ(outcome.result, SchemeBranchSwitchOutcome::Result::Aborted);
    EXPECT_EQ(outcome.abortedAt, CloseFlowOutcome::AbortStage::DraftPrompt);
    EXPECT_EQ(rig.drafts.saveCalls, 0);
    EXPECT_EQ(rig.drafts.discardCalls, 0);
    ASSERT_EQ(rig.decisions.draftPrompts.size(), 1u);
    EXPECT_EQ(rig.decisions.draftPrompts[0].scenarioKey,
              std::string(workflow::kSchemeSwitchScenarioKey));
    EXPECT_EQ(rig.decisions.draftPrompts[0].kind, CloseKind::Switch);
    EXPECT_EQ(rig.decisions.draftPrompts[0].draftModules,
              (std::vector<std::string>{"requirements"}));
}

/**
 * 方案分支切换之草稿三选"保存"（PM-04 保存＝在原分支落盘——用户显式
 * 决策的写，非切换写）：保存恰好一次后切换批准（Proceed）。
 */
TEST(WfClose, SchemeSwitchDraftSaveProceeds)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-12"}, std::vector<std::string>{"AT-21"});

    FlowRig rig;
    rig.drafts.modules = {"requirements"};
    rig.decisions.draftAnswer = DraftDisposition::Save;

    workflow::SchemeBranchSwitchRequest req;
    req.currentBranch = core::BranchId::fromCanonical("brn-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    req.targetBranch = core::BranchId::fromCanonical("brn-bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");

    const auto outcome =
        workflow::SchemeBranchSwitchFlow::run(req, rig.decisions, rig.drafts);

    ASSERT_EQ(outcome.result, SchemeBranchSwitchOutcome::Result::Proceed);
    EXPECT_EQ(outcome.abortedAt, CloseFlowOutcome::AbortStage::None);
    EXPECT_FALSE(outcome.failure.has_value());
    EXPECT_EQ(rig.drafts.saveCalls, 1);
}

/**
 * 方案分支切换无草稿直达批准（零决策调用——零写入编排面）：Proceed
 * 且草稿动作零调用（切换本身零触达的编排面证据；真实落盘的 HEAD 字节
 * 复核在契约测试 WF-VER-212）。
 */
TEST(WfClose, SchemeSwitchNoDraftProceedsWithoutDecisions)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-12"}, std::vector<std::string>{});

    FlowRig rig;
    workflow::SchemeBranchSwitchRequest req;
    req.currentBranch = core::BranchId::fromCanonical("brn-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    req.targetBranch = core::BranchId::fromCanonical("brn-bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");

    const auto outcome =
        workflow::SchemeBranchSwitchFlow::run(req, rig.decisions, rig.drafts);

    ASSERT_EQ(outcome.result, SchemeBranchSwitchOutcome::Result::Proceed);
    EXPECT_TRUE(rig.decisions.draftPrompts.empty());
    EXPECT_EQ(rig.drafts.saveCalls, 0);
    EXPECT_EQ(rig.drafts.discardCalls, 0);
}

/**
 * 方案分支切换草稿动作失败：Failed＋UX-03 三字段（零吞错——切换不批准）。
 */
TEST(WfClose, SchemeSwitchDraftFailureFails)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-12"}, std::vector<std::string>{});

    FlowRig rig;
    rig.drafts.modules = {"selection"};
    rig.decisions.draftAnswer = DraftDisposition::Discard;
    rig.drafts.discardOk = false;

    workflow::SchemeBranchSwitchRequest req;
    req.currentBranch = core::BranchId::fromCanonical("brn-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    req.targetBranch = core::BranchId::fromCanonical("brn-bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");

    const auto outcome =
        workflow::SchemeBranchSwitchFlow::run(req, rig.decisions, rig.drafts);

    ASSERT_EQ(outcome.result, SchemeBranchSwitchOutcome::Result::Failed);
    ASSERT_TRUE(outcome.failure.has_value());
    EXPECT_FALSE(outcome.failure->context.empty());
    EXPECT_FALSE(outcome.failure->cause.empty());
    EXPECT_FALSE(outcome.failure->recommendedAction.empty());
}

}  // namespace
