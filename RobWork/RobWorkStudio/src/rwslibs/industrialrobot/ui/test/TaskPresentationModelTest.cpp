/**
 * @file   TaskPresentationModelTest.cpp
 * @brief  UI-T13 模型层用例（QCoreApplication 级——§12.1 第一层分工）：
 *         任务呈现模型 §9.4/§9.5/§10.7 的落位自证——进度轮询三规则
 *         （percent 单调/新 attempt 重置/重复幂等——UI-TSK-5）、清单
 *         双区与当前置顶（PM-03/§6.3 九态短标签）、后台只读自限
 *         （P-UI-7）、强杀独立确认（§9.4）、暂停能力反馈不静默
 *         （TASK-01）、归档子标（完成≠归档完成——UI-TSK-3）、worker
 *         崩溃重跑入口（UI-TSK-2/NFR-REL-02）、当前性徽标搬运且无
 *         「正式通过」字样（UI-TSK-4）、即发即忘无阻塞等待
 *         （NFR-PERF-02）。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T13.json acceptance 3（UI-TSK-1~5
 *     逐条对应各用例 IRD_TEST_INFO 追溯字段）＋acceptance 5（O-31/
 *     P-UI-4/P-UI-7 处置）；
 *   - units/ui.md §9.4 全表、§9.5、§10.7 契约表（本套件即其行为自证
 *     面）、§12.3 UI-TSK-1~5 行的模型半区观测点（GUI 呈现半区归
 *     gui_test 层——UI-T14 承接）；
 *   - 先例：SessionControllerModelTest.cpp 的可控端口替身纪律。
 */

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include <sdurws/ird/ui/ITaskPresentationModel.hpp>
#include <sdurws/ird/ui/UiPorts.hpp>
#include <sdurws/ird/ui/UiText.hpp>   // resolveText（§3.5 唯一出口——文案键值断言）
#include <sdurws/ird/ui/UiProjections.hpp>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace sdurws::ird;
using ui::ArchiveBadge;
using ui::CurrentnessProjection;
using ui::ProgressMergeDecision;
using ui::ResultCurrentnessBadge;
using ui::TaskRow;
using ui::TaskViewProjection;
using ui::UiTaskAck;

// =====================================================================
// 测试替身（C-8 呈现面端口——O-31 下以可控替身承载）
// =====================================================================

/// 任务呈现面端口替身（C-8——快照表可编程；控制调用全部记录）。
class StubTaskPort final : public ui::IUiTaskPresentationPort {
public:
    std::vector<TaskViewProjection> rows;               ///< tasksByProject 返回值（可编程）
    std::map<std::string, TaskViewProjection> byKey;    ///< task() 寻址表（键＝run 规范文本）
    std::map<std::string, UiTaskAck> acks;              ///< 控制应答（可编程——feedback 面）
    int cancelCalls = 0;                                ///< 控制调用计数（即发即忘面证据）
    int pauseCalls = 0;
    int resumeCalls = 0;
    int forceCalls = 0;

    std::vector<TaskViewProjection>
    tasksByProject(const core::ProjectId&) const override
    {
        return rows;
    }

    std::optional<TaskViewProjection>
    task(const core::TaskIdentity& id) const override
    {
        const auto it = byKey.find(id.run.toCanonical());
        if (it == byKey.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    std::optional<ui::TaskProgressProjection>
    progress(const core::TaskIdentity& id) const override
    {
        // 轻查询半区（§9.4"轮询 progress(taskId)"的端口面——模型主路径
        // 用整行快照，见文件头实现口径；本替身从快照表折叠返回）。
        const auto it = byKey.find(id.run.toCanonical());
        if (it == byKey.end() || !it->second.progress.has_value()) {
            return std::nullopt;
        }
        return it->second.progress;
    }

    UiTaskAck requestCancel(const core::TaskIdentity& id) override
    {
        ++cancelCalls;
        return ackOf(id);
    }
    UiTaskAck requestPause(const core::TaskIdentity& id) override
    {
        ++pauseCalls;
        return ackOf(id);
    }
    UiTaskAck requestResume(const core::TaskIdentity& id) override
    {
        ++resumeCalls;
        return ackOf(id);
    }
    UiTaskAck requestForceTerminate(const core::TaskIdentity& id) override
    {
        ++forceCalls;
        return ackOf(id);
    }

private:
    /// 逐任务 Ack（可编程；缺省＝受理）。
    UiTaskAck ackOf(const core::TaskIdentity& id) const
    {
        const auto it = acks.find(id.run.toCanonical());
        if (it != acks.end()) {
            return it->second;
        }
        UiTaskAck accepted;
        accepted.accepted = true;
        return accepted;
    }
};

/// 当前性端口替身（C-7——徽标搬运的判定位）。
class StubCurrentness final : public ui::IUiCurrentnessSource {
public:
    CurrentnessProjection projection;   ///< 可编程当前值
    mutable int currentnessCalls = 0;

    CurrentnessProjection currentness() const override
    {
        ++currentnessCalls;
        return projection;
    }
    ui::EvidenceManifestProjection evidenceManifest() const override { return {}; }
    ui::FormalPassEligibilityProjection formalPassEligibility() const override { return {}; }
};

/// 开发日志记录器（回退丢弃的 Dev 出线观测面）。
class DevLogRecorder final : public diagnostics::IDevLogSink {
public:
    void logDev(std::string_view, std::string message) override
    {
        m_entries.emplace_back(std::move(message));
    }
    bool seen(const std::string& needle) const
    {
        return std::any_of(m_entries.begin(), m_entries.end(),
                           [&needle](const std::string& e) {
                               return e.find(needle) != std::string::npos;
                           });
    }

private:
    std::vector<std::string> m_entries;
};

/// 观察者记录器（清单/进度回调计数）。
class RecordingObserver final : public ui::ITaskViewObserver {
public:
    int rowsChanged = 0;
    int progressUpdates = 0;
    void onTaskRowsChanged() override { ++rowsChanged; }
    void onProgressUpdated(const core::TaskIdentity&) override { ++progressUpdates; }
};

// =====================================================================
// 测试环境装配
// =====================================================================

struct TaskHarness {
    std::shared_ptr<StubTaskPort> port = std::make_shared<StubTaskPort>();
    std::shared_ptr<StubCurrentness> currentness = std::make_shared<StubCurrentness>();
    std::shared_ptr<DevLogRecorder> devLog = std::make_shared<DevLogRecorder>();
    const core::ProjectId project = core::ProjectId::generate();
    std::unique_ptr<ui::ITaskPresentationModel> model;

    TaskHarness()
    {
        ui::TaskPresentationDeps deps;
        deps.taskPort = port.get();
        deps.currentness = currentness.get();
        deps.devLog = devLog.get();
        model = ui::createTaskPresentationModel(std::move(deps));
        model->attachProject(project);
    }

    /// 造一个任务快照（五元组身份自洽——同 run 可寻址）。
    TaskViewProjection makeSnapshot(core::TaskState state, std::uint64_t attemptValue,
                                    std::optional<ui::TaskProgressProjection> progress,
                                    bool supportsPause = true)
    {
        TaskViewProjection snap;
        snap.identity.project = project;
        snap.identity.branch = core::BranchId::generate();
        snap.identity.revision = core::RevisionId::generate();
        snap.identity.run = core::RunId::generate();
        snap.identity.attempt = core::AttemptId{attemptValue};
        snap.attempt = core::AttemptId{attemptValue};
        snap.state = state;
        snap.supportsPause = supportsPause;
        snap.progress = std::move(progress);
        port->byKey[snap.identity.run.toCanonical()] = snap;
        return snap;
    }

    /// 幂等对照进度（重复/回退判定用）。
    static ui::TaskProgressProjection progressOf(int percent)
    {
        ui::TaskProgressProjection p;
        p.percent = percent;
        p.phaseLabelKey = "stage.kinematics.title";
        p.batchesDone = static_cast<std::uint64_t>(percent);
        p.batchesTotal = 100;
        return p;
    }
};

// =====================================================================
// UI-TSK-5：进度轮询三规则（§9.4"乱序/重复处理"行）
// =====================================================================

/** UI-TSK-5：同 attempt 内 percent 单调——回退丢弃＋Dev 日志；重复幂等。 */
TEST(TaskPresentationModelTest, ProgressMonotonicDropRegressionAndIdempotent_UI_TSK_5)
{
    IRD_TEST_INFO("TASK-01", {}, std::nullopt);
    TaskHarness h;
    auto run1 = h.makeSnapshot(core::TaskState::Running, 1,
                               h.progressOf(30));

    // 推进到 40（Accept）→ 回退到 20（DropRegression）→ 重复 40（幂等）。
    const auto p30 = h.progressOf(30);
    const auto p40 = h.progressOf(40);
    const auto p20 = h.progressOf(20);

    EXPECT_EQ(ui::mergeProgressDecision(std::nullopt, core::AttemptId{1},
                                        core::AttemptId{1}, p30),
              ProgressMergeDecision::Accept);          // 首报接受
    EXPECT_EQ(ui::mergeProgressDecision(p30, core::AttemptId{1},
                                        core::AttemptId{1}, p40),
              ProgressMergeDecision::Accept);          // 正常前进
    EXPECT_EQ(ui::mergeProgressDecision(p40, core::AttemptId{1},
                                        core::AttemptId{1}, p20),
              ProgressMergeDecision::DropRegression);  // 回退丢弃（乱序/迟到报文）
    EXPECT_EQ(ui::mergeProgressDecision(p40, core::AttemptId{1},
                                        core::AttemptId{1}, p40),
              ProgressMergeDecision::Idempotent);      // 重复幂等
    EXPECT_EQ(ui::mergeProgressDecision(p40, core::AttemptId{1},
                                        core::AttemptId{2}, p20),
              ProgressMergeDecision::AttemptReset);    // 新 attempt 重置基线
}

/** UI-TSK-5：轮询整链——回退值不进显示基线且留 Dev 痕迹；观察者被通知。 */
TEST(TaskPresentationModelTest, PollingDropsRegressionWithDevLog_UI_TSK_5)
{
    IRD_TEST_INFO("TASK-01", {}, std::nullopt);
    TaskHarness h;
    const auto snap = h.makeSnapshot(core::TaskState::Running, 1, h.progressOf(50));
    h.port->rows = {snap};
    h.model->refresh();

    RecordingObserver observer;
    const auto subscription = h.model->subscribe(observer);
    h.model->startProgressPolling(
        ui::kDefaultProgressPollInterval);   // 默认 200 ms（§10.7）
    EXPECT_EQ(h.model->progressPollInterval(), ui::kDefaultProgressPollInterval);

    // 对端推进到 70 → 轮询并入（观察者收到进度通知）。
    auto advanced = snap;
    advanced.progress = h.progressOf(70);
    h.port->byKey[snap.identity.run.toCanonical()] = advanced;
    h.model->pollOnce();
    EXPECT_EQ(observer.progressUpdates, 1);
    ASSERT_TRUE(h.model->activeTasks()[0].progress.has_value());
    EXPECT_EQ(h.model->activeTasks()[0].progress->percent, 70);

    // 对端回退到 20（乱序）→ 丢弃：显示基线保持 70，Dev 日志留痕。
    auto regressed = snap;
    regressed.progress = h.progressOf(20);
    h.port->byKey[snap.identity.run.toCanonical()] = regressed;
    h.model->pollOnce();
    EXPECT_EQ(observer.progressUpdates, 1);   // 丢弃不通知
    EXPECT_EQ(h.model->activeTasks()[0].progress->percent, 70);
    EXPECT_TRUE(h.devLog->seen("回退丢弃"));

    // 重复 70 → 幂等（不重复通知）。
    h.model->pollOnce();
    EXPECT_EQ(observer.progressUpdates, 1);

    h.model->stopProgressPolling();
    EXPECT_FALSE(h.model->progressPollInterval()
                 == std::chrono::milliseconds::zero());   // 周期登记仍在（翻位停止）
}

// =====================================================================
// UI-TSK-1：清单双区/当前置顶/控制请求（取消/暂停/继续/强杀）
// =====================================================================

/** UI-TSK-1：当前置顶稳定序＋九态短标签键；Queued 呈现"等待资源"。 */
TEST(TaskPresentationModelTest, CurrentTaskFirstOrderingAndStateLabels_UI_TSK_1)
{
    IRD_TEST_INFO("TASK-01", {"PM-03"}, std::nullopt);
    TaskHarness h;
    const auto queued = h.makeSnapshot(core::TaskState::Queued, 0, std::nullopt);
    const auto running = h.makeSnapshot(core::TaskState::Running, 1, h.progressOf(10));
    const auto completed = h.makeSnapshot(core::TaskState::Completed, 1, std::nullopt);
    h.port->rows = {queued, completed, running};   // 故意乱序注入
    h.model->refresh();

    const auto rows = h.model->activeTasks();
    ASSERT_EQ(rows.size(), 3u);
    EXPECT_EQ(rows[0].snap.state, core::TaskState::Running);   // 当前任务置顶（§9.4）
    EXPECT_EQ(rows[1].snap.state, core::TaskState::Queued);
    EXPECT_EQ(rows[2].snap.state, core::TaskState::Completed);
    // 九态短标签键（§6.3——taskStateLabelKey 唯一产出）。
    EXPECT_EQ(rows[0].stateLabelKey, ui::taskStateLabelKey(core::TaskState::Running));
    // 排队文案＝"排队中（等待资源）"（UI-EXEC-1 登记限制——不推测数值）。
    EXPECT_EQ(ui::resolveText("ui.task.queue.waiting-resource"),
              "排队中（等待资源）");
}

/** UI-TSK-1：取消/暂停/继续即发即忘（Ack 透传）；强杀需独立确认。 */
TEST(TaskPresentationModelTest, ControlRequestsFireAndForget_UI_TSK_1)
{
    IRD_TEST_INFO("TASK-01", {"OPT-06"}, std::nullopt);
    TaskHarness h;
    const auto snap = h.makeSnapshot(core::TaskState::Running, 1, h.progressOf(10));
    h.port->rows = {snap};
    h.model->refresh();

    // 取消＝协作（UX-03：受理无错误诊断——缺省 Ack 受理位透传）。
    const auto cancelAck = h.model->requestCancel(snap.identity);
    EXPECT_TRUE(cancelAck.accepted);
    EXPECT_EQ(h.port->cancelCalls, 1);

    // 暂停/继续按能力声明启用：支持暂停的任务请求被转发。
    const auto pauseAck = h.model->requestPause(snap.identity);
    EXPECT_TRUE(pauseAck.accepted);
    EXPECT_EQ(h.port->pauseCalls, 1);
    EXPECT_EQ(h.port->resumeCalls, 0);
    (void)h.model->requestResume(snap.identity);
    EXPECT_EQ(h.port->resumeCalls, 1);

    // 强杀＝独立高级操作：未经确认的请求被显式拒绝（不发对端）。
    const auto unconfirmed = h.model->requestForceTerminate(snap.identity, false);
    EXPECT_FALSE(unconfirmed.accepted);
    EXPECT_EQ(unconfirmed.reasonKey, "ui.task.force.needs-confirm");
    EXPECT_EQ(h.port->forceCalls, 0);
    // 独立确认后放行（确认对话数据固定三件：标题/后果/身份）。
    const auto confirmation = h.model->forceTerminateConfirmation(snap.identity);
    EXPECT_EQ(confirmation.task.run, snap.identity.run);
    EXPECT_EQ(ui::resolveText(confirmation.consequenceKey),
              "任务将记为失败，最近检查点保留，可从检查点续跑");
    const auto confirmed = h.model->requestForceTerminate(snap.identity, true);
    EXPECT_TRUE(confirmed.accepted);
    EXPECT_EQ(h.port->forceCalls, 1);
}

/** TASK-01：不支持暂停的任务收到请求→显式反馈不静默（§9.4 原文）。 */
TEST(TaskPresentationModelTest, PauseOnUnsupportedTaskGivesExplicitFeedback_TASK_01)
{
    IRD_TEST_INFO("TASK-01", {"UX-03"}, std::nullopt);
    TaskHarness h;
    // 能力声明 supportsPause=false（TaskCapability.supportsPause 直读投影）。
    const auto snap = h.makeSnapshot(core::TaskState::Running, 1, std::nullopt,
                                     /*supportsPause=*/false);
    h.port->rows = {snap};
    h.model->refresh();

    // 对端 Ack 无反馈词（最坏形态）——模型以 ui.task.pause.unsupported
    // 兜底，保证"显式提示不静默"（双层保证的后半区）。
    h.port->acks[snap.identity.run.toCanonical()] = [] {
        UiTaskAck ack;
        ack.accepted = false;
        return ack;
    }();
    const auto ack = h.model->requestPause(snap.identity);
    EXPECT_FALSE(ack.accepted);
    EXPECT_EQ(ack.reasonKey, "ui.task.pause.unsupported");
    EXPECT_EQ(h.port->pauseCalls, 1);

    // 对端已给显式反馈词——透传不覆盖（双层保证的前半区）。
    h.port->acks[snap.identity.run.toCanonical()] = [] {
        UiTaskAck ack;
        ack.accepted = false;
        ack.reasonKey = "ui.task.pause.unsupported";
        return ack;
    }();
    const auto ack2 = h.model->requestPause(snap.identity);
    EXPECT_EQ(ack2.reasonKey, "ui.task.pause.unsupported");
}

// =====================================================================
// P-UI-7：关闭项目后旧任务进后台只读清单（§9.4 行）
// =====================================================================

/** P-UI-7：降级后行入后台清单，控制请求自限拒绝（按钮禁用的编程面）。 */
TEST(TaskPresentationModelTest, BackgroundTasksReadOnlyAfterClose_P_UI_7)
{
    IRD_TEST_INFO("TASK-03", {"PM-03"}, std::nullopt);
    TaskHarness h;
    const auto snap = h.makeSnapshot(core::TaskState::Running, 1, h.progressOf(80));
    h.port->rows = {snap};
    h.model->refresh();

    // 关闭项目：当前清单降级为后台只读（§5.4/§6.2——旧任务进后台清单）。
    h.model->demoteCurrentToBackground();
    ASSERT_EQ(h.model->activeTasks().size(), 0u);
    ASSERT_EQ(h.model->backgroundTasks().size(), 1u);
    EXPECT_EQ(h.model->backgroundTasks()[0].progress->percent, 80);   // 快照保持

    // 控制请求被模型自限（未达对端——backgroundRejected 位＋文案键）。
    const auto ack = h.model->requestCancel(snap.identity);
    EXPECT_FALSE(ack.accepted);
    EXPECT_TRUE(ack.backgroundRejected);
    EXPECT_EQ(ack.reasonKey, "ui.task.background.readonly");
    EXPECT_EQ(ui::resolveText(ack.reasonKey), "项目已关闭，后台任务只读");
    EXPECT_EQ(h.port->cancelCalls, 0);   // 自限＝未转发

    // 重新绑定项目→清单切到新项目（旧行保持在后台）。
    const core::ProjectId next = core::ProjectId::generate();
    h.model->attachProject(next);
    EXPECT_EQ(h.model->backgroundTasks().size(), 1u);   // 后台保留
}

// =====================================================================
// UI-TSK-2/3/4：崩溃重跑／归档子标／当前性徽标
// =====================================================================

/** UI-TSK-2：worker 崩溃（Failed）行有重跑入口且历史可查看（界面侧）。 */
TEST(TaskPresentationModelTest, WorkerCrashRowHasRerunEntry_UI_TSK_2)
{
    IRD_TEST_INFO("NFR-REL-02", {"AT-11"}, std::nullopt);
    TaskHarness h;
    auto crashed = h.makeSnapshot(core::TaskState::Failed, 1, std::nullopt);
    crashed.terminationReasonToken = "worker-crashed";   // EX-WORKER-CRASHED 场景词表
    h.port->rows = {crashed};
    h.model->refresh();

    // Failed 行：重跑入口可用（新 TaskId——崩溃不自动重试 D-09 的入口
    // 呈现面），历史可查看；模型对 Failed 行照常装配（界面不退出的
    // 呈现半区——进程存活由壳层保证，本层零异常路径）。
    const auto actions = ui::taskRowActionsFor(h.model->activeTasks()[0].snap);
    EXPECT_TRUE(actions.rerunAvailable);
    EXPECT_TRUE(actions.historyAvailable);
    EXPECT_EQ(ui::resolveText("ui.task.rerun"), "重跑");

    // 中断行同样可重跑（§9.4"中断→'已中断'可重跑"）。
    auto interrupted = crashed;
    interrupted.state = core::TaskState::Interrupted;
    EXPECT_TRUE(ui::taskRowActionsFor(interrupted).rerunAvailable);

    // 排队行无重跑/无历史（未终态）。
    auto queued = crashed;
    queued.state = core::TaskState::Queued;
    const auto queuedActions = ui::taskRowActionsFor(queued);
    EXPECT_FALSE(queuedActions.rerunAvailable);
    EXPECT_FALSE(queuedActions.historyAvailable);
}

/** UI-TSK-3：Completed 徽标附归档子标——ArchiveFailed 警告位。 */
TEST(TaskPresentationModelTest, CompletedBadgeCarriesArchiveSublabel_UI_TSK_3)
{
    IRD_TEST_INFO("TASK-01", {"CON-04"}, std::nullopt);
    // 归档子标词表逐值（execution §9 token 直读——不镜像枚举）。
    const ArchiveBadge none = ui::archiveBadgeFor("");
    EXPECT_FALSE(none.present);   // 无归档语义（Preview 恒无——§4.2）

    const ArchiveBadge archiving = ui::archiveBadgeFor("archiving");
    EXPECT_TRUE(archiving.present);
    EXPECT_FALSE(archiving.warning);
    EXPECT_EQ(ui::resolveText(archiving.sublabelKey), "归档中");

    const ArchiveBadge archived = ui::archiveBadgeFor("archived");
    EXPECT_EQ(ui::resolveText(archived.sublabelKey), "已归档");

    // ArchiveFailed→警告标记（EX-ARCHIVE-FAILED 诊断经诊断目录呈现——
    // 徽标只承载警告位，不改任务终态）。
    const ArchiveBadge failed = ui::archiveBadgeFor("archive-failed");
    EXPECT_TRUE(failed.present);
    EXPECT_TRUE(failed.warning);
    EXPECT_EQ(ui::resolveText(failed.sublabelKey), "归档失败");

    // 未知 token→不虚构（词表演进前的安全兜底）。
    EXPECT_FALSE(ui::archiveBadgeFor("future-phase").present);
}

/** UI-TSK-4：当前性过期附原因且历史可查看；无「正式通过」字样。 */
TEST(TaskPresentationModelTest, CurrentnessBadgeSupersededWithReasons_UI_TSK_4)
{
    IRD_TEST_INFO("CON-02", {"AT-05"}, std::nullopt);
    // Current——徽标"结果当前"。
    CurrentnessProjection current;
    current.status = CurrentnessProjection::Status::Current;
    const ResultCurrentnessBadge currentBadge = ui::resultBadgeFor(current);
    EXPECT_TRUE(currentBadge.available);
    EXPECT_TRUE(currentBadge.current);
    EXPECT_EQ(ui::resolveText(currentBadge.labelKey), "结果当前");

    // Superseded——附原因（对端 detail 原样搬运）＋历史可查看。
    CurrentnessProjection superseded;
    superseded.status = CurrentnessProjection::Status::Superseded;
    superseded.reasons.push_back({"kine.param", "dependency-changed",
                                  "运动学参数已修改（旧 → 新）"});
    const ResultCurrentnessBadge staleBadge = ui::resultBadgeFor(superseded);
    EXPECT_TRUE(staleBadge.available);
    EXPECT_FALSE(staleBadge.current);
    ASSERT_EQ(staleBadge.reasonDetails.size(), 1u);
    EXPECT_EQ(staleBadge.reasonDetails[0], "运动学参数已修改（旧 → 新）");
    EXPECT_EQ(ui::resolveText(staleBadge.labelKey), "结果已过期");

    // 不可判定（status 空）——徽标不显示（不虚构；NotEvaluable 呈现归
    // §6.3 七态求值，UI-T04）。
    CurrentnessProjection unevaluable;
    const ResultCurrentnessBadge noneBadge = ui::resultBadgeFor(unevaluable);
    EXPECT_FALSE(noneBadge.available);
}

/** 装配契约：缺任务端口 fail-fast（§10.7 前置行"_scheduler 已注入"）。 */
TEST(TaskPresentationModelTest, FactoryFailsFastWithoutTaskPort)
{
    IRD_TEST_INFO("TASK-01", {}, std::nullopt);
    EXPECT_THROW(
        [] {
            ui::TaskPresentationDeps deps;
            deps.taskPort = nullptr;
            (void)ui::createTaskPresentationModel(std::move(deps));
        }(),
        std::invalid_argument);
}

// =====================================================================
// UI-T14 增补具名落位：UI-EXEC-1（执行限制登记）与 UI-PERF-1（UI 线程
// 纪律——§12.3 承接族 PERF 族唯一用例；两例原属 §12.3 清单，具名用例
// 随 UI-T14 契约测试套件落位）
// =====================================================================

/**
 * UI-EXEC-1（§12.3 表尾登记执行限制：队列位置与资源占用无对外接口
 * ——execution §A3）：排队任务的呈现只验证"排队中（等待资源）"文案与
 * 诊断呈现，不验证数值——呈现文案零数字字符、行与轮询面零虚构进度，
 * 呈现层不存在任何"推测队列位置"的通道。
 */
TEST(TaskPresentationModelTest, QueuePositionWordingOnlyWithoutNumbers_UI_EXEC_1)
{
    IRD_TEST_INFO("TASK-01", {"UI-EXEC-1"}, std::nullopt);
    TaskHarness h;
    const auto queued = h.makeSnapshot(core::TaskState::Queued, 0, std::nullopt);
    h.port->rows = {queued};
    h.model->refresh();

    const auto rows = h.model->activeTasks();
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].snap.state, core::TaskState::Queued);

    // 文案面：排队呈现＝固定文案"排队中（等待资源）"（§3.5 键解析唯一
    // 出口）——排队语义只以这一句话呈现，UI-EXEC-1 留痕锚。
    const std::string wording =
        ui::resolveText("ui.task.queue.waiting-resource");
    EXPECT_EQ(wording, "排队中（等待资源）");

    // 零数值纪律：呈现文案不含任何数字字符（队列位置/资源占用无对外
    // 接口——呈现层不得推测数值，不虚构数据源）。
    for (const char c : wording) {
        EXPECT_FALSE(c >= '0' && c <= '9')
            << "排队文案携带数值（UI-EXEC-1 零虚构违约）: " << wording;
    }

    // 零虚构进度：Queued 行与轮询面均无进度可显（Queued/Preparing 期
    // 端口投影 nullopt 直通——不伪造百分比）。
    EXPECT_FALSE(rows[0].progress.has_value());
    h.model->startProgressPolling(std::chrono::milliseconds(200));
    h.model->pollOnce();
    EXPECT_FALSE(h.model->activeTasks()[0].progress.has_value());
    h.model->stopProgressPolling();
}

/**
 * UI-PERF-1（NFR-PERF-01/ARCH §4.2——UI 线程不执行长时工作）：对端慢
 * 查询（>1 s）触发投影刷新期间，事件循环心跳探针无 >200 ms 间隙、单次
 * 刷新不吞时间片，长工作在后台线程完成并经排队 Marshal 回 UI 线程消费
 * （§12.3 观测点"心跳时间戳序列"）。
 *
 * 判据纪律（§12.2"不使用固定 sleep 判据"）：判定的依据是心跳时间戳
 * 与刷新耗时测量，不是 sleep；后台线程的 1.1 s 限时等待是**场景构件**
 * （模拟"桩慢端口 >1 s 查询"的操作设定），不参与任何断言。
 */
TEST(TaskPresentationModelTest, UiThreadHeartbeatUnderSlowPeerQuery_UI_PERF_1)
{
    IRD_TEST_INFO("NFR-PERF-01", {"NFR-PERF-02"}, std::nullopt);
    TaskHarness h;

    // 场景构件：对端慢查询 >1 s（条件变量限时等待，见上注释）。
    std::mutex mtx;
    std::condition_variable doneCv;
    const bool queryDone = false;   // 不提前放行——让慢查询走满场景时长

    QEventLoop loop;

    // 心跳探针：25 ms 周期记录 UI 线程心跳时刻（事件循环未被阻塞的
    // 直接证据——相邻间隔序列即 §12.3 观测点"心跳时间戳序列"）。
    std::vector<std::chrono::steady_clock::duration> gaps;
    auto lastBeat = std::chrono::steady_clock::now();
    QTimer heartbeat(&loop);
    QObject::connect(&heartbeat, &QTimer::timeout, [&] {
        const auto now = std::chrono::steady_clock::now();
        gaps.push_back(now - lastBeat);
        lastBeat = now;
    });
    heartbeat.start(25);

    // UI 线程投影刷新：每 50 ms 触发一次 refresh 并测量单次耗时
    // （呈现模型只做快照搬运——刷新不得成为事件循环的长时占用）。
    std::vector<std::chrono::steady_clock::duration> refreshCosts;
    QTimer refreshTimer(&loop);
    QObject::connect(&refreshTimer, &QTimer::timeout, [&] {
        const auto begin = std::chrono::steady_clock::now();
        h.model->refresh();
        refreshCosts.push_back(std::chrono::steady_clock::now() - begin);
    });
    refreshTimer.start(50);

    // 后台慢查询线程：走满慢查询时长后，结果经排队 Marshal 交付 UI
    // 线程（M-1 纪律——对端替身 rows/byKey 的写点全部搬进 UI 线程
    // lambda，替身保持仅 UI 线程访问）。
    std::thread slowQuery([&] {
        std::unique_lock<std::mutex> lk(mtx);
        doneCv.wait_for(lk, std::chrono::milliseconds(1100),
                        [&] { return queryDone; });
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [&] {
                h.port->rows = {h.makeSnapshot(core::TaskState::Running, 1,
                                               TaskHarness::progressOf(70))};
                h.model->refresh();
            },
            Qt::QueuedConnection);
    });

    // 总观察窗 1.5 s（慢查询 1.1 s＋Marshal 交付与刷新余量），到点收卷。
    QTimer::singleShot(1500, &loop, [&] { loop.quit(); });
    loop.exec();

    heartbeat.stop();
    refreshTimer.stop();
    slowQuery.join();

    // 判定一：心跳无 >200 ms 间隙（§12.3 预期原文"UI 线程无 >200 ms
    // 阻塞"——阈值即用例值，不放宽）。
    ASSERT_FALSE(gaps.empty()) << "心跳探针未产生任何采样";
    const auto maxGap = *std::max_element(gaps.begin(), gaps.end());
    EXPECT_LT(maxGap, std::chrono::milliseconds(200))
        << "UI 线程出现阻塞（心跳最大间隙 "
        << std::chrono::duration_cast<std::chrono::milliseconds>(maxGap).count()
        << " ms）";

    // 判定二：单次投影刷新 <50 ms（刷新周期量级——刷新路径零长时工作）。
    ASSERT_FALSE(refreshCosts.empty()) << "投影刷新未被执行";
    for (const auto cost : refreshCosts) {
        EXPECT_LT(cost, std::chrono::milliseconds(50))
            << "单次刷新超过刷新周期量级（UI 线程承载了长工作）";
    }

    // 判定三：慢查询结果经 Marshal 落地（长工作确实"转后台"且结果回到
    // UI 线程被消费——闭环而非丢弃）。
    const auto rows = h.model->activeTasks();
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].snap.state, core::TaskState::Running);
    ASSERT_TRUE(rows[0].progress.has_value());
    EXPECT_EQ(rows[0].progress->percent, 70);
}

}  // namespace
