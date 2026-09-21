/**
 * @file   TaskPresentation.cpp
 * @brief  任务呈现模型实现（UI-T13——§9.4/§9.5/§10.7 的落位承载）。
 *
 * 设计依据：
 *   - units/ui.md §9.4（任务呈现全表——清单/轮询三规则/能力启用/强杀
 *     独立确认/崩溃重跑/归档子标/当前性徽标/后台只读）、§9.5（关闭
 *     对话框＝非终态过滤）、§10.7（接口契约表）、§3.4（M-1——本模型
 *     无跨线程回调，观察者在 UI 线程同步通知）、§6.3（九态短标签键）；
 *   - 需求 TASK-01/02/03、UX-03、NFR-REL-02、NFR-PERF-02；
 *   - 契约 acceptance 3（UI-TSK-1~5）＋P-UI-7（后台只读自限）。
 *
 * 实现口径登记（ui.md §16.7 v1.5 同步）：
 *   - 轮询执行面＝pollOnce（L5 定时器心跳驱动——模型零 Qt 定时器，
 *     "轮询停止后无残余定时器事件"由 L5 停摆定时器承载，模型只翻位）；
 *   - 轮询取**整行快照**（port.task——attempt 与 progress 同视图，避免
 *     "进度轻查询与新 attempt 竞争被误判回退"的乱序窗口；port.progress
 *     作为轻查询半区保留在端口契约面，由契约测试 exercised）；
 *   - 暂停反馈双层保证：端口 Ack 反馈优先透传；行已知不支持暂停且
 *     Ack 无反馈时以 ui.task.pause.unsupported 兜底——显式提示不静默。
 */

#include <sdurws/ird/ui/ITaskPresentationModel.hpp>

#include <sdurws/ird/ui/UiPorts.hpp>   // IUiTaskPresentationPort/IUiCurrentnessSource（依赖端口完整定义）

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace sdurws::ird {
namespace ui {

// =====================================================================
// §9.4 纯函数装配（唯一权威实现点）
// =====================================================================

ProgressMergeDecision mergeProgressDecision(
    const std::optional<TaskProgressProjection>& previous,
    core::AttemptId previousAttempt, core::AttemptId incomingAttempt,
    const TaskProgressProjection& incoming)
{
    // 规则序（§9.4 原文顺序）：先 attempt 轴（新 attempt 重置基线），
    // 再同 attempt 内的单调/幂等轴。previous 为空＝首报——直接接受。
    if (!previous.has_value()) {
        return ProgressMergeDecision::Accept;
    }
    if (!(incomingAttempt == previousAttempt)) {
        // 新 attempt（暂停继续/重试产生）——基线重置，无论高低一律接受
        // （"继续不重复统计"的呈现半区：新 attempt 从头计进度）。
        return ProgressMergeDecision::AttemptReset;
    }
    if (incoming.percent < previous->percent) {
        // 同 attempt 内回退——乱序/迟到报文，丢弃（调用方记 Dev 日志）。
        return ProgressMergeDecision::DropRegression;
    }
    if (incoming.percent == previous->percent && incoming == *previous) {
        // 完全相同的重复报文——幂等（不触发界面刷新）。
        return ProgressMergeDecision::Idempotent;
    }
    // 同 attempt 正常前进（percent↑ 或同 percent 但批次/阶段变化）。
    return ProgressMergeDecision::Accept;
}

ArchiveBadge archiveBadgeFor(const std::string& archivePhaseToken)
{
    // execution.md §9 词表直读——token 直用不镜像枚举（NFR-MNT-03）。
    // 空串/not-applicable＝无归档语义（Preview 任务恒无——§4.2）；未知
    // token（词表演进前到达）＝不虚构归档状态（安全兜底，同 diagAction
    // For 的未知族兜底口径）。
    ArchiveBadge badge;
    if (archivePhaseToken == "reserved") {
        badge.present = true;
        badge.sublabelKey = "ui.task.archive.reserved";
    } else if (archivePhaseToken == "archiving") {
        badge.present = true;
        badge.sublabelKey = "ui.task.archive.archiving";
    } else if (archivePhaseToken == "archived") {
        badge.present = true;
        badge.sublabelKey = "ui.task.archive.archived";
    } else if (archivePhaseToken == "archive-failed") {
        // ArchiveFailed→警告标记（EX-ARCHIVE-FAILED 诊断本体经诊断目录
        // 呈现——徽标只承载警告位，不改任务终态，§9.4 原文）。
        badge.present = true;
        badge.warning = true;
        badge.sublabelKey = "ui.task.archive.failed";
    }
    return badge;
}

TaskRowActions taskRowActionsFor(const TaskViewProjection& row)
{
    TaskRowActions actions;
    // 重跑入口：Failed（worker 崩溃"诊断＋重跑"——新 TaskId，不自动重试
    // D-09）∨ Interrupted（"已中断"可重跑，NFR-REL-03）。
    actions.rerunAvailable = row.state == core::TaskState::Failed
                          || row.state == core::TaskState::Interrupted;
    // 历史结果查看：终态即可浏览（TASK-03——历史不成为"当前结果"；
    // "过期历史可查看"§6.8）。非终态无历史结果可看。
    switch (row.state) {
    case core::TaskState::Completed:
    case core::TaskState::Failed:
    case core::TaskState::Interrupted:
    case core::TaskState::Canceled:
        actions.historyAvailable = true;
        break;
    default:
        actions.historyAvailable = false;
        break;
    }
    return actions;
}

ResultCurrentnessBadge resultBadgeFor(const CurrentnessProjection& currentness)
{
    // 搬运不计算（§9.4"由 evidence 提供，ui 不计算"）：status 空＝不可
    // 判定——徽标不显示（NotEvaluable 的呈现归 §6.3 七态求值，UI-T04）。
    ResultCurrentnessBadge badge;
    if (!currentness.status.has_value()) {
        return badge;
    }
    badge.available = true;
    if (*currentness.status == CurrentnessProjection::Status::Current) {
        badge.current = true;
        badge.labelKey = "ui.task.currentness.current";
        return badge;
    }
    // Superseded：附原因（逐条 detail 原样搬运——对端产出人读摘要，
    // 呈现层不加工）；词表只有"已过期"，无「正式通过」字样（显示纪律
    // 放行归 formalPassRenderable——UI-T04 唯一放行数据源）。
    badge.labelKey = "ui.task.currentness.superseded";
    badge.reasonDetails.reserve(currentness.reasons.size());
    for (const auto& reason : currentness.reasons) {
        badge.reasonDetails.push_back(reason.detail);
    }
    return badge;
}

// =====================================================================
// 清单排序（当前置顶——§9.4 稳定序）
// =====================================================================

namespace {

/**
 * @brief 任务九态的呈现优先级（当前置顶——Running 最前，终态殿后）。
 *
 * 组内次级键＝身份字典序（TaskIdentity.operator<——确定性全序，
 * NFR-COR-02：同输入同序，不依赖到达顺序）。
 */
int taskStatePresentationRank(core::TaskState state) noexcept
{
    switch (state) {
    case core::TaskState::Running:     return 0;   // 当前任务置顶（§9.4）
    case core::TaskState::Paused:      return 1;
    case core::TaskState::Canceling:   return 2;
    case core::TaskState::Preparing:   return 3;
    case core::TaskState::Queued:      return 4;   // 排队（"等待资源"文案）
    case core::TaskState::Completed:   return 5;   // 终态组（近终态窗口内）
    case core::TaskState::Failed:      return 6;
    case core::TaskState::Interrupted: return 7;
    case core::TaskState::Canceled:    return 8;
    }
    return 9;   // 全枚举不可达；保守殿后
}

/// 行排序（先状态秩，后身份字典序——稳定全序）。
bool taskRowLess(const TaskViewProjection& a, const TaskViewProjection& b)
{
    const int rankA = taskStatePresentationRank(a.state);
    const int rankB = taskStatePresentationRank(b.state);
    if (rankA != rankB) {
        return rankA < rankB;
    }
    return a.identity < b.identity;
}

/// Dev 日志通道 token（"diag/<域>"命名族——回退丢弃的出线）。
constexpr std::string_view kTaskPresentationDevChannel = "diag/ui-tasks";

/**
 * @brief 模型实现（§10.7 契约表的行为承载——细节见类各方法注释）。
 */
class TaskPresentationModel final : public ITaskPresentationModel {
public:
    explicit TaskPresentationModel(TaskPresentationDeps deps)
        : m_deps(std::move(deps))
    {
    }

    // ---- 清单查询 ----

    std::vector<TaskRow> activeTasks() const override
    {
        return m_active;   // 值拷贝（§10.7"快照值拷贝"）；序＝refresh 维护的稳定序
    }

    std::vector<TaskRow> backgroundTasks() const override
    {
        return m_background;   // 只读清单（最后一次已知快照）
    }

    std::optional<TaskRow> task(const core::TaskIdentity& id) const override
    {
        // 先当前清单后台清单（当前会话任务优先命中）。
        for (const auto& row : m_active) {
            if (row.snap.identity == id) {
                return row;
            }
        }
        for (const auto& row : m_background) {
            if (row.snap.identity == id) {
                return row;
            }
        }
        return std::nullopt;   // 不存在（终态回收/他项目）——不虚构
    }

    // ---- 控制请求（即发即忘——§10.7"无 UI 线程阻塞等待"）----

    UiTaskAck requestCancel(const core::TaskIdentity& id) override
    {
        if (auto rejected = rejectIfBackground(id)) {
            return *rejected;   // 后台只读自限（P-UI-7——未达对端）
        }
        // 端口转发（对端是寻址/受理权威——RunRegistry 核对面）。
        return m_deps.taskPort->requestCancel(id);
    }

    UiTaskAck requestPause(const core::TaskIdentity& id) override
    {
        if (auto rejected = rejectIfBackground(id)) {
            return *rejected;
        }
        UiTaskAck ack = m_deps.taskPort->requestPause(id);
        ensurePauseFeedback(id, ack);   // 显式提示不静默——双层保证（见文件头）
        return ack;
    }

    UiTaskAck requestResume(const core::TaskIdentity& id) override
    {
        if (auto rejected = rejectIfBackground(id)) {
            return *rejected;
        }
        return m_deps.taskPort->requestResume(id);
    }

    UiTaskAck requestForceTerminate(const core::TaskIdentity& id,
                                    bool userConfirmed) override
    {
        if (auto rejected = rejectIfBackground(id)) {
            return *rejected;
        }
        if (!userConfirmed) {
            // 强杀独立确认前置（§9.4"带确认＋后果说明"）：未经独立确认
            // 对话的强杀请求被显式拒绝——不发对端（防误触发的编程面
            // 防线；GUI 的确认对话由 forceTerminateConfirmation 数据
            // 驱动）。
            UiTaskAck ack;
            ack.accepted = false;
            ack.reasonKey = "ui.task.force.needs-confirm";
            return ack;
        }
        return m_deps.taskPort->requestForceTerminate(id);
    }

    ForceTerminateConfirmationData
    forceTerminateConfirmation(const core::TaskIdentity& id) const override
    {
        ForceTerminateConfirmationData data;
        data.task = id;
        data.titleKey = "ui.task.force.title";
        data.consequenceKey = "ui.task.force.consequence";
        return data;
    }

    // ---- 进度轮询 ----

    void startProgressPolling(std::chrono::milliseconds interval) override
    {
        // 周期登记（>0 前置——非正周期属调用方违约，fail-fast）。
        if (interval <= std::chrono::milliseconds::zero()) {
            throw std::invalid_argument(
                "ui/task-presentation/polling: 轮询周期必须为正值（默认 200 ms）");
        }
        m_pollInterval = interval;
        m_polling = true;
    }

    void stopProgressPolling() override
    {
        // 只翻位——定时器归 L5（"停止后无残余定时器事件"由 L5 停摆承载，
        // 文件头实现口径）。
        m_polling = false;
    }

    void pollOnce() override
    {
        if (!m_polling) {
            return;   // 未启动轮询——心跳到达也忽略（防御 L5 泄漏的定时器）
        }
        bool rowsChanged = false;
        for (auto& row : m_active) {
            // 取整行快照（attempt＋progress 同视图——文件头实现口径）。
            const auto snapshot = m_deps.taskPort->task(row.snap.identity);
            if (!snapshot.has_value()) {
                continue;   // 任务已不可见（终态回收竞态）——下轮 refresh 收敛
            }
            if (snapshot->state != row.snap.state
                || snapshot->archivePhaseToken != row.snap.archivePhaseToken) {
                // 状态/归档相位迁移——并入快照（事件即时刷新的轮询兜底）。
                row.snap.state = snapshot->state;
                row.snap.archivePhaseToken = snapshot->archivePhaseToken;
                row.snap.terminationReasonToken = snapshot->terminationReasonToken;
                row.stateLabelKey = ui::taskStateLabelKey(row.snap.state);
                rowsChanged = true;
            }
            if (!snapshot->progress.has_value()) {
                continue;   // 尚无进度（Queued/Preparing 期常态）
            }
            const auto decision = mergeProgressDecision(
                row.progress, row.snap.attempt, snapshot->attempt, *snapshot->progress);
            switch (decision) {
            case ProgressMergeDecision::Accept:
            case ProgressMergeDecision::AttemptReset:
                row.progress = *snapshot->progress;
                row.snap.attempt = snapshot->attempt;   // 重置基线轴
                notifyProgress(row.snap.identity);
                break;
            case ProgressMergeDecision::DropRegression:
                // 回退丢弃＋Dev 日志（§9.4"回退值丢弃＋Dev 日志"原文）。
                logDev("进度回退丢弃：taskId=" + row.snap.identity.run.toCanonical()
                       + " percent=" + std::to_string(snapshot->progress->percent)
                       + " < 基线 " + std::to_string(
                             row.progress ? row.progress->percent : 0));
                break;
            case ProgressMergeDecision::Idempotent:
                break;   // 重复幂等——不刷新不通知
            }
        }
        if (rowsChanged) {
            reorderActive();
            notifyRowsChanged();
        }
    }

    std::chrono::milliseconds progressPollInterval() const override
    {
        return m_pollInterval;
    }

    // ---- 清单刷新与订阅 ----

    void refresh() override
    {
        if (!m_currentProject.has_value()) {
            if (!m_active.empty()) {
                m_active.clear();
                notifyRowsChanged();
            }
            return;   // 无项目首页态——清单空（不虚构）
        }
        // 端口全量快照（§9.4 任务清单数据源——tasksByProject）。
        const auto snapshots = m_deps.taskPort->tasksByProject(*m_currentProject);
        std::vector<TaskRow> rows;
        rows.reserve(snapshots.size());
        for (auto& snap : snapshots) {
            TaskRow row;
            row.snap = std::move(snap);
            row.stateLabelKey = ui::taskStateLabelKey(row.snap.state);   // §6.3 键
            // 进度基线并入：已展示值按三规则守恒（refresh 不回退显示——
            // 乱序快照不得倒退进度条，与 pollOnce 同纪律）。
            if (row.snap.progress.has_value()) {
                const auto decision = mergeProgressDecision(
                    std::nullopt, row.snap.attempt, row.snap.attempt,
                    *row.snap.progress);
                (void)decision;   // 首并入恒 Accept——显式标注无未用告警意图
                row.progress = row.snap.progress;
            }
            // 已有展示进度的新快照若无进度（清零竞态）——保留旧值不回退。
            for (const auto& existing : m_active) {
                if (existing.snap.identity == row.snap.identity
                    && !row.progress.has_value()) {
                    row.progress = existing.progress;
                }
            }
            rows.push_back(std::move(row));
        }
        m_active = std::move(rows);
        reorderActive();
        notifyRowsChanged();
    }

    std::unique_ptr<core::IEventSubscription> subscribe(ITaskViewObserver& observer) override
    {
        // 观察者非 owning（引用生存期契约＝观察者析构前先退订——句柄
        // RAII）；回调在 UI 线程同步发生（模型无后台线程）。
        m_observers.push_back(&observer);
        return std::unique_ptr<core::IEventSubscription>(
            new TaskViewSubscription(*this, observer));
    }

    // ---- 会话联动 ----

    void attachProject(const core::ProjectId& project) override
    {
        m_currentProject = project;
        refresh();   // 切清单（§5.4 S2——B 项目任务呈现）
    }

    void demoteCurrentToBackground() override
    {
        // 当前清单并入后台只读清单（§9.4"关闭项目后旧任务进后台只读
        // 清单"——P-UI-7 建议口径；行保持最后一次已知快照，后台任务
        // 的归档进度经事件/轮询不可达〔调度归旧项目持有点〕，如实呈现
        // 快照态）。
        for (auto& row : m_active) {
            m_background.push_back(std::move(row));
        }
        m_active.clear();
        m_currentProject.reset();
        notifyRowsChanged();
    }

private:
    /// 订阅句柄（RAII——析构即退订；退订幂等）。
    class TaskViewSubscription final : public core::IEventSubscription {
    public:
        TaskViewSubscription(TaskPresentationModel& model, ITaskViewObserver& observer)
            : m_model(&model)
            , m_observer(&observer)
        {
        }

        ~TaskViewSubscription() override { unsubscribe(); }
        TaskViewSubscription(const TaskViewSubscription&) = delete;
        TaskViewSubscription& operator=(const TaskViewSubscription&) = delete;

        void unsubscribe() override
        {
            if (m_model != nullptr) {
                m_model->removeObserver(*m_observer);
                m_model = nullptr;
                m_observer = nullptr;
            }
        }

    private:
        TaskPresentationModel* m_model;
        ITaskViewObserver* m_observer;
    };

    /// 后台行自限（P-UI-7——控制按钮禁用的编程面；命中返回显式拒绝 Ack）。
    std::optional<UiTaskAck> rejectIfBackground(const core::TaskIdentity& id)
    {
        for (const auto& row : m_background) {
            if (row.snap.identity == id) {
                UiTaskAck ack;
                ack.accepted = false;
                ack.reasonKey = "ui.task.background.readonly";
                ack.backgroundRejected = true;
                return ack;
            }
        }
        return std::nullopt;
    }

    /// 暂停反馈双层保证（文件头实现口径）：端口反馈优先；行已知不支持
    /// 暂停且 Ack 无反馈→ui.task.pause.unsupported 兜底（不静默）。
    void ensurePauseFeedback(const core::TaskIdentity& id, UiTaskAck& ack)
    {
        if (ack.accepted || !ack.reasonKey.empty()) {
            return;   // 受理或对端已给显式反馈——透传
        }
        for (const auto& row : m_active) {
            if (row.snap.identity == id && !row.snap.supportsPause) {
                ack.reasonKey = "ui.task.pause.unsupported";
                return;
            }
        }
    }

    /// 当前置顶重排（taskRowLess 稳定全序）。
    void reorderActive()
    {
        std::stable_sort(m_active.begin(), m_active.end(),
                         [](const TaskRow& a, const TaskRow& b) {
                             return taskRowLess(a.snap, b.snap);
                         });
    }

    /// 观察者注销（退订幂等——重复移除无副作用）。
    void removeObserver(ITaskViewObserver& observer)
    {
        m_observers.erase(
            std::remove(m_observers.begin(), m_observers.end(), &observer),
            m_observers.end());
    }

    /// 清单变更通知（UI 线程同步——§10.7 线程行）。
    void notifyRowsChanged()
    {
        // 回调表快照遍历（观察者在回调内退订的安全防御——先拷贝再走）。
        const auto observers = m_observers;
        for (auto* observer : observers) {
            observer->onTaskRowsChanged();
        }
    }

    /// 进度更新通知（幂等重复不达此——pollOnce 已过滤）。
    void notifyProgress(const core::TaskIdentity& id)
    {
        const auto observers = m_observers;
        for (auto* observer : observers) {
            observer->onProgressUpdated(id);
        }
    }

    /// Dev 日志出线（可空——静默跳过）。
    void logDev(const std::string& message)
    {
        if (m_deps.devLog != nullptr) {
            m_deps.devLog->logDev(kTaskPresentationDevChannel, message);
        }
    }

    TaskPresentationDeps m_deps;                    ///< 装配依赖（非 owning）
    std::optional<core::ProjectId> m_currentProject;  ///< 当前会话项目（nullopt＝无项目/已降级）
    std::vector<TaskRow> m_active;                  ///< 当前清单（稳定序——当前置顶）
    std::vector<TaskRow> m_background;              ///< 后台只读清单（最后已知快照）
    std::vector<ITaskViewObserver*> m_observers;    ///< 观察者表（非 owning）
    std::chrono::milliseconds m_pollInterval = kDefaultProgressPollInterval;  ///< 轮询周期（§10.7 默认 200 ms）
    bool m_polling = false;                         ///< 轮询启用位（L5 定时器据此驱动 pollOnce）
};

}  // namespace

std::unique_ptr<ITaskPresentationModel>
createTaskPresentationModel(TaskPresentationDeps deps)
{
    // 装配契约 fail-fast（无任务端口的模型没有存在意义——§2.3 调用方
    // 错误语义）。
    if (deps.taskPort == nullptr) {
        throw std::invalid_argument(
            "ui/task-presentation/deps: taskPort 为空——任务呈现模型必注入 C-8 呈现面端口");
    }
    return std::make_unique<TaskPresentationModel>(std::move(deps));
}

}  // namespace ui
}  // namespace ird
