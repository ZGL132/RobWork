/**
 * @file   UiSessionController.cpp
 * @brief  UI 会话控制器实现——§5.2 状态机推进、§5.4 关闭/切换对话框机制、
 *         §5.6 Draining 四级防线与 §5.3/§5.7 迟到写提示（UI-T11）。
 *
 * 实现与设计的逐条对应（review 对照表）：
 *   - assembleCloseDialogData ＝ §5.4 S1"数据装配"框（草稿区/任务区行集
 *     直通＋可用位派生——纯函数，行集不加工保证 NFR-COR-02 稳定呈现）；
 *   - openProject ＝ §5.2 状态机图 openProject 迁移（Opening→成功绑定/
 *     失败回退——"失败→错误页，停留/回退 NoProject，不动当前项目"）；
 *   - beginClose/beginSwitch/resolveCloseDialog ＝ §5.4 S1/S2 时序
 *     （取消/保存/放弃/等待/协作取消五分支＋切换候选验证"成功才切"）；
 *   - pollDrain ＝ §5.6 四级防线（closed() 兜底轮询/持有点释放/T_force
 *     强制终止确认/T_force2 放弃等待——INV-SES-4 的执行点）；
 *   - makeContextInvalidNotice ＝ §5.3 行 4/§5.7（UI-SESSION-CONTEXT-
 *     INVALID 双通道出线：目录 Warning＋Dev 日志；不重试不缓存由数据
 *     形状保证——提示值不携带动作位）；
 *   - onStoreClosed ＝ §5.2 subscribeClose 回调（当前会话关闭完成＋后台
 *     持有点释放——INV-SES-3 的释放点）。
 *
 * 线程纪律（§3.4 M-1）：本实现无锁——全部状态变更假定 UI 线程串行；
 * 存储上下文关闭回调的线程投递义务在 L5 端口适配器（IUiStoreCloseObserver
 * 契约注释），回调纪律被破坏时防线 4 的 closed() 轮询兜底。
 */

#include <sdurws/ird/ui/UiSessionController.hpp>

#include <stdexcept>
#include <utility>

namespace sdurws::ird {
namespace ui {

namespace {

// ---------------------------------------------------------------------
// 机制文案键（§3.5 键体系——键冻结于本文件，值解析经 UiText/文案资源；
// 关闭对话框机制的数据面只携带键，GUI 层解析呈现——键值分离纪律）
// ---------------------------------------------------------------------

/// 强制结束确认框主文案键（§5.6 防线 3——"强制结束并关闭"确认语义）。
constexpr const char* kForceCloseMessageKey = "ui.session.force-close.confirm";
/// 已释放上下文提示主文案键（§5.3 行 4——"项目上下文已释放，操作未执行"）。
constexpr const char* kContextInvalidMessageKey = "ui.session.context-invalid.title";
/// 已释放上下文提示建议键（§5.3 行 4——"建议重新打开"；入口编排归 workflow）。
constexpr const char* kContextInvalidAdviceKey = "ui.session.context-invalid.advice";

/// Dev 日志通道 token（≤48 字符——§7.2 LogChannel；与壳层 "diag/ui" 同族）。
constexpr const char* kSessionDevChannel = "diag/ui/session";

/**
 * @brief 单调时钟缺省供应（steadyClock 未注入时的缺省——测试注入假时钟，
 *        生产走 steady_clock；不用实时钟——T_force/T_force2 是时长阈值，
 *        墙钟跳变不得影响防线计时，§5.6 阈值语义）。
 */
std::chrono::steady_clock::time_point defaultSteadyNow()
{
    return std::chrono::steady_clock::now();
}

}  // namespace

// =====================================================================
// 关闭对话框数据装配（§5.4 S1"数据装配"框的唯一实现点）
// =====================================================================

CloseDialogData assembleCloseDialogData(const CloseDialogInputs& inputs)
{
    CloseDialogData data;
    // 标题区＝项目显示名（UX-02——调用方保证已是显示名，零哈希校验随
    // UiText 侧 ensureNoInternalIdentity 承担，装配层不二次加工）。
    data.projectDisplayName = inputs.projectDisplayName;
    // 草稿区＝磁盘草稿行集直通（§5.4"DraftService::list(branch)＋会话脏
    // 模块"——行序保持端口快照原序，不排序：NFR-COR-02 稳定呈现）。
    data.draftRows = inputs.diskDraftRows;
    // 任务区＝非终态任务行集直通（§9.5"tasksByProject 过滤非终态"——
    // 过滤由端口侧完成，装配不复滤：§3.2 端口语义不重定义）。
    data.taskRows = inputs.nonTerminalTaskRows;
    // 会话脏标记独立携带（§8.5——与磁盘草稿行集并列的事实位，不并入
    // 行集：会话脏模块未必有磁盘草稿行，虚构行会误导"保存"的落盘预期）。
    data.sessionDirty = inputs.sessionDirty;
    // [保存草稿]可用位＝可写会话（§5.4"[保存草稿]（可写会话）"原文；
    // 只读会话保存按钮不可选——§5.5 draft.save 为写操作禁用的呈现半区）。
    data.saveDraftsAvailable = inputs.writable;
    // 任务区空位＝无在途任务（"等待"此时直接进入 Draining——影响面为
    // 空的确认不需要用户在任务二选间抉择）。
    data.noActiveTasks = inputs.nonTerminalTaskRows.empty();
    return data;
}

// =====================================================================
// 构造/析构（装配校验——fail-fast）
// =====================================================================

UiSessionController::UiSessionController(UiSessionControllerDeps deps)
    : m_deps(std::move(deps))
{
    // 装配错误 fail-fast（AGENTS §3 错误语义：调用方错误走异常，不留给
    // 运行期——没有打开端口的控制器无法进入 Opening，状态机无从谈起）。
    if (!m_deps.storeFactory) {
        throw std::invalid_argument(
            "ui/session: storeFactory 端口为空（IUiStoreFactoryPort 必填——"
            "打开五步协议入口，§5.2 Opening 态数据面）");
    }
    // 时钟缺省：steady_clock（见 defaultSteadyNow 注释——时长阈值不用墙钟）。
    if (!m_deps.steadyClock) {
        m_deps.steadyClock = defaultSteadyNow;
    }
}

UiSessionController::~UiSessionController() = default;

// =====================================================================
// 会话状态观测
// =====================================================================

bool UiSessionController::isReadOnlySession() const noexcept
{
    // INV-SES-1（§5.2 原文）：OpenStoreResult.writable 是只读判定的唯一
    // 数据源——本函数只读打开结果直写字段，结构上不存在心跳/控件状态/
    // "上次结果"的第二来源（无项目会话时返回 false：无项目态的写禁用由
    // PM-10 无项目门控承担，不与只读语义混淆）。
    return hasOpenSession() && !m_session.writable;
}

// =====================================================================
// 打开（§5.2 NoProject→Opening→Open*）
// =====================================================================

SessionOpenReport UiSessionController::openProject(const std::string& canonicalPath,
                                                   UiOpenMode mode)
{
    // 打开请求只能从无项目态发起（切换走 beginSwitch——INV-SES-2 的调用
    // 面防线：不存在"已有 Open* 会话再叠一个打开"的路径）。
    if (m_state != UiSessionState::NoProject) {
        throw std::logic_error(
            "ui/session: openProject 仅可在 NoProject 态调用（当前态非无项目"
            "——已打开会话的切换走 beginSwitch，§5.4 S2）");
    }
    m_state = UiSessionState::Opening;
    SessionPortBundle bundle;
    // 五步协议①~④在 ProjectStoreFactory 内（§5.2 Opening 框原文）——
    // ui 只消费投影结果与端口绑定，不感知协议内步骤（O-31：对端能力经
    // 端口进入，ui 零对端知识）。
    const OpenStoreOutcome outcome = m_deps.storeFactory->open(canonicalPath, mode, bundle);
    SessionOpenReport report;
    if (!outcome.ok) {
        // 失败→错误页，回退 NoProject（§5.2 Opening 框"失败→错误页，
        // 停留/回退 NoProject，不动当前项目"原文——当前无项目，回退即至）。
        m_state = UiSessionState::NoProject;
        report.ok = false;
        report.failure = outcome.failure;
        // 失败细节 Dev 级出线（排障面——用户侧错误页数据已在返回值轨）。
        emitDevLine("open failed: path=" + canonicalPath
                    + " code=" + outcome.failure.errorCodeToken
                    + " detail=" + outcome.failure.detail);
        return report;
    }
    // 成功：绑定会话（状态/epoch/订阅/上下文投影一次完成——bindSession
    // 是"打开成功"epoch 递增的唯一执行点之一，§6.2）。
    bindSession(outcome.opened, std::move(bundle));
    report.ok = true;
    report.opened = outcome.opened;
    // 降级只读＝打开成功的一种（PM-07 不阻塞等待）——横幅随打开立即
    // 装配（§5.3 行 1~3：成因→文案键＋actionKind 的映射在纯函数内）。
    if (!outcome.opened.metadata.writable && outcome.opened.readOnlyCause.has_value()) {
        report.readOnlyBanner =
            assembleReadOnlyBanner(*outcome.opened.readOnlyCause, outcome.opened.lockHolder);
    }
    return report;
}

// =====================================================================
// 关闭/切换：统一确认对话框机制（§5.4 S1/S2）
// =====================================================================

CloseDialogData UiSessionController::assembleDialogForCurrentSession() const
{
    // 输入快照一次取齐（§6.1 快照一致性——可用位与行集同刻）：
    //   - 磁盘草稿行 ← C-5 草稿清单端口（listDrafts 快照）；
    //   - 非终态任务行 ← C-8 任务端口（tasksByProject 过滤非终态语义）；
    //   - 会话脏/可写位 ← 控制器会话态（sessionDirty 是 ui 自有事实，
    //     writable 是 INV-SES-1 数据源直通）。
    CloseDialogInputs inputs;
    inputs.projectDisplayName = m_session.metadata.projectDisplayName;
    inputs.writable = m_session.writable;
    inputs.sessionDirty = m_sessionDirty;
    if (m_session.drafts) {
        inputs.diskDraftRows = m_session.drafts->listDrafts();
    }
    if (m_session.tasks) {
        inputs.nonTerminalTaskRows = m_session.tasks->nonTerminalTasks(m_session.metadata.projectId);
    }
    return assembleCloseDialogData(inputs);
}

CloseDialogData UiSessionController::beginClose(UiCloseIntent intent)
{
    // 对话框只能在已打开会话上发起（§5.2 状态表 Open* 行"closeRequest/
    // exitRequest"触发列——NoProject/Opening/Draining/Closed 均无此入口）。
    if (!hasOpenSession()) {
        throw std::logic_error(
            "ui/session: beginClose 仅可在已打开会话（OpenWritable/OpenReadOnly）"
            "调用——无项目态没有关闭语义（§5.2 状态表触发列）");
    }
    // 挂起请求是单槽（呈现层必须对同一请求决议一次——双请求并行使"取消
    // 回到原状态"的语义不可判定，调用次序违约 fail-fast）。
    if (m_dialogPending) {
        throw std::logic_error(
            "ui/session: 已有挂起的关闭/切换对话框请求未决议（begin* 与 "
            "resolve* 必须配对——单槽纪律）");
    }
    m_dialogPending = true;
    m_pendingIntent = intent;
    m_pendingSwitch = false;
    m_pendingCandidatePath.clear();
    // 状态不变（§5.2：对话框呈现期间会话仍在 Open*——"[取消]→回到原
    // 状态"要求原状态从未离开）。
    return assembleDialogForCurrentSession();
}

CloseDialogData UiSessionController::beginSwitch(const std::string& candidatePath,
                                                 UiOpenMode mode)
{
    // 同 beginClose：须已打开会话＋单槽空闲（§5.4 S2"切换请求→对话框
    // （同上，针对 A）"——对话框机制与关闭共用，差别只在决议后段）。
    if (!hasOpenSession()) {
        throw std::logic_error(
            "ui/session: beginSwitch 仅可在已打开会话（OpenWritable/OpenReadOnly）"
            "调用——切换的对话框针对当前项目 A 装配（§5.4 S2）");
    }
    if (m_dialogPending) {
        throw std::logic_error(
            "ui/session: 已有挂起的关闭/切换对话框请求未决议（begin* 与 "
            "resolve* 必须配对——单槽纪律）");
    }
    m_dialogPending = true;
    m_pendingIntent = UiCloseIntent::CloseProject;  // A 的处置语义＝关闭（去向由切换段接管）
    m_pendingSwitch = true;
    m_pendingCandidatePath = candidatePath;
    m_pendingCandidateMode = mode;
    return assembleDialogForCurrentSession();
}

CloseDialogResolution UiSessionController::resolveCloseDialog(const CloseDecision& decision)
{
    if (!m_dialogPending) {
        throw std::logic_error(
            "ui/session: resolveCloseDialog 无挂起的对话框请求（begin* 未调用"
            "或已决议——调用次序违约）");
    }
    CloseDialogResolution out;

    // ---- [取消] 分支：回到原状态（§5.4 S1"[取消]→回到原状态（中止）"
    // 原文——状态从未离开 Open*，清除挂起槽即恢复；零处置执行）。
    if (!decision.confirmed) {
        m_dialogPending = false;
        m_pendingSwitch = false;
        m_pendingCandidatePath.clear();
        out.status = CloseDialogResolution::Status::Cancelled;
        return out;
    }

    // ---- 草稿处置（§5.4 S1 草稿区两执行分支）----
    if (decision.draft == DraftDisposition::Save) {
        // [保存] → DraftController.saveAll(Manual)（可写会话）→ 继续。
        // 两重前置契约校验（fail-fast——静默降级会造成"以为保存了"的
        // 假象，违反禁吞错纪律）：
        //   1. 只读会话选保存＝§5.5 违约（draft.save 是写操作，禁用清单
        //      在呈现层已置灰——决议仍选保存属调用方契约破坏）；
        //   2. 接线点缺失＝装配违约（saveAllDraftsManual→IDraftController::
        //      saveAll(Manual)，§8/UI-T12 落位后接线）。
        if (!m_session.writable) {
            throw std::logic_error(
                "ui/session: 只读会话决议[保存草稿]（§5.5 只读禁用清单——"
                "draft.save 为写操作；CloseDialogData.saveDraftsAvailable="
                "false 时呈现层不得给出保存选项）");
        }
        if (!m_deps.saveAllDraftsManual) {
            throw std::logic_error(
                "ui/session: 决议[保存草稿]但 saveAllDraftsManual 未接线"
                "（装配违约——保存执行归 DraftController::saveAll(Manual)，"
                "§8.6/UI-T12）");
        }
        if (!m_deps.saveAllDraftsManual()) {
            // 保存失败→关闭中止、回到原状态（§5.4"[保存]→继续"的失败侧
            // 保守出口——未落盘的草稿不允许随关闭静默丢弃；呈现层可据
            // SaveFailed 重开对话框或就地提示后重试）。
            m_dialogPending = false;
            m_pendingSwitch = false;
            m_pendingCandidatePath.clear();
            out.status = CloseDialogResolution::Status::SaveFailed;
            emitDevLine("close aborted: saveAll(Manual) reported failure");
            return out;
        }
        m_sessionDirty = false;
    } else {
        // [放弃] → 会话脏数据丢弃（§5.4"[放弃]→会话脏数据丢弃→继续"；
        // 磁盘草稿保留策略归 §8.6 DraftController——会话半区清除即可，
        // 磁盘事实不动）。
        m_sessionDirty = false;
    }

    // ---- 任务处置（§5.4 S1 任务区二选）----
    if (decision.task == TaskDisposition::CooperativeCancel && m_session.tasks) {
        // [协作取消] → 对每个非终态任务 requestCancel（§5.4 S1"[协作取消]
        // →对每个非终态任务 ITaskController::requestCancel"原文；2 s 进入
        // Canceling/10 s 收敛由 execution 保证——NFR-PERF-02，ui 只发请求
        // 不等待）。重查而非复用对话框行集：决议期间任务集可能变化（有
        // 任务自然终态），陈旧行会向对端发送无意义取消请求。
        for (const TaskRowProjection& row :
             m_session.tasks->nonTerminalTasks(m_session.metadata.projectId)) {
            (void)m_session.tasks->requestCancel(row.identity);
        }
    }
    // [等待] 分支无额外动作——Draining 中保持进度可见（§5.6 防线 1：
    // 等待有界面反馈——任务行集/归档阶段由呈现层经任务端口持续读取）。

    if (m_pendingSwitch) {
        // ---- 切换流后段（§5.4 S2）：候选验证 → A 入后台持有点 → B 绑定。
        SessionPortBundle bundle;
        const OpenStoreOutcome candidate =
            m_deps.storeFactory->open(m_pendingCandidatePath, m_pendingCandidateMode, bundle);
        if (!candidate.ok) {
            // 候选验证失败→错误页，A 界面会话不变（§5.4 S2"失败→错误页，
            // A 界面会话不变"＋PM-03"候选验证成功才切"原文——A 从未离开
            // Open*，草稿/任务处置已按决议执行但不触发任何上下文变更）。
            m_dialogPending = false;
            m_pendingSwitch = false;
            m_pendingCandidatePath.clear();
            out.status = CloseDialogResolution::Status::CandidateRejected;
            out.candidateErrorCodeToken = candidate.failure.errorCodeToken;
            out.candidatePath = candidate.failure.projectPath;
            emitDevLine("switch rejected (candidate open failed): path="
                        + candidate.failure.projectPath
                        + " code=" + candidate.failure.errorCodeToken);
            return out;
        }
        // 验证成功→A 进入 Draining（后台持有点保活，迟到结果照常归档，
        // §5.4 S2"成功→A 进入 Draining（后台持有点保活）"原文）——A 的
        // 关闭信号先发（§5.2 Draining 进入动作＝requestClose：拒绝新写并
        // 开始在途计数排空；后台排空进度经持有点任务行观察，§5.7）。
        DrainingHold hold;
        hold.metadata = m_session.metadata;
        hold.store = m_session.store;
        hold.tasks = m_session.tasks;
        hold.subscription = std::move(m_closeSubscription);
        (void)hold.store->requestClose();
        m_holds.push_back(std::move(hold));
        // UI 会话立即绑定 B（不等 A 排空——§5.4 S2"UI 会话立即绑定 B"
        // 原文）；bindSession 内完成 B 的状态/订阅/上下文投影与
        // "切换绑定新项目"的 epoch 递增（§6.2）。INV-SES-2 的结构性
        // 保证：B 进入 Open* 的唯一路径前置＝A 已移出 m_session（转入
        // 持有点）——任一时刻至多一个 Open* 绑定。
        m_session = SessionBinding{};
        m_dialogPending = false;
        m_pendingSwitch = false;
        m_pendingCandidatePath.clear();
        bindSession(candidate.opened, std::move(bundle));
        out.status = CloseDialogResolution::Status::Confirmed;
        return out;
    }

    // ---- 关闭流后段（§5.4 S1）：确认通过 → store.requestClose() → Draining。
    // §5.2 状态表：CloseConfirmed＝"对话框确认通过，执行所选处置"——
    // 处置（草稿/任务）已在上方完成，本行起进入关闭协议。
    m_state = UiSessionState::CloseConfirmed;
    m_drainIntent = m_pendingIntent;
    m_dialogPending = false;
    m_pendingCandidatePath.clear();
    // requestClose()：拒绝新写；返回在途引用数（§5.2 Draining 框"store.
    // requestClose()（拒绝新写；返回在途引用数）"原文）——计数留作防线 1
    // 的初始界面反馈值（归档会话/在途事务/草稿落盘；§5.7 引用计数族）。
    m_drainInFlightReferences = m_session.store->requestClose();
    // Draining 计时起点（防线 3/4 的 T_force/T_force2 均自此刻起算——
    // 取消等待后再次关闭时在此重启，§5.6 两阈值的有界等待语义）。
    m_drainStartedAt = m_deps.steadyClock();
    m_forcePromptPresented = false;
    m_forceExecuted = false;
    m_drainAbandoned = false;
    m_state = UiSessionState::Draining;
    emitDevLine("draining entered: in-flight references="
                + std::to_string(m_drainInFlightReferences));
    // 零在途同步完成路径：requestClose 后立即 closed()==true（对端无在途
    // 引用时回调可能与本调用同步到达）——不走轮询，直接完成关闭。
    if (m_session.store->isClosed()) {
        finishCurrentClose();
    }
    out.status = CloseDialogResolution::Status::Confirmed;
    return out;
}

// =====================================================================
// Draining 防线（§5.6——INV-SES-4）
// =====================================================================

DrainPollReport UiSessionController::pollDrain()
{
    // 轮询对象＝当前 Draining 会话或任一后台持有点（两者皆无＝无排空
    // 面可观察，调用次序违约）。
    if (m_state != UiSessionState::Draining && m_holds.empty()) {
        throw std::logic_error(
            "ui/session: pollDrain 无 Draining 会话且无后台持有点"
            "（无轮询对象——关闭/切换流程未进入等待阶段）");
    }
    DrainPollReport report;

    // ---- 后台持有点轮询（切换场景旧项目排空——subscribeClose 回调的
    // 兜底检测，与防线 4 的 closed() 轮询同源机制；持有点无强制终止
    // 路径：§5.7"迟到结果继续归档……不催促、不跳过"——A 的归档节奏
    // 不受 UI 会话切换影响，也不受关闭防线约束）。
    for (auto it = m_holds.begin(); it != m_holds.end();) {
        if (it->store && it->store->isClosed()) {
            emitDevLine("draining hold released (poll): prj="
                        + it->metadata.projectId.toCanonical());
            it = m_holds.erase(it);  // 释放保活引用（§5.1——回调/轮询后释放）
            ++report.holdsReleased;
        } else {
            ++it;
        }
    }

    // ---- 当前会话 Draining 防线链（自上而下：完成→放弃→强制确认→等待）。
    if (m_state == UiSessionState::Draining) {
        const std::chrono::milliseconds elapsed = std::chrono::duration_cast<
            std::chrono::milliseconds>(m_deps.steadyClock() - m_drainStartedAt);
        report.inFlightReferences = m_drainInFlightReferences;

        // 防线 4 检测半区：兜底轮询 closed()（§5.6 极端行——subscribeClose
        // 回调丢失时轮询仍能观察到关闭完成；回调正常时此处与本轮询等价）。
        if (m_session.store && m_session.store->isClosed()) {
            finishCurrentClose();
            report.status = DrainPollReport::Status::ClosedNow;
            return report;
        }
        // 防线 4 执行半区：T_force2 到点→放弃等待（§5.6 极端行"放弃等待、
        // 记录 UI-SESSION-CONTEXT-INVALID（Dev 级）并退出——数据损失限制
        // 在未归档结果（检查点已保留），绝不无限等待"；INV-SES-4 的最终
        // 界，自 Draining 起点总预算有界，不依赖用户在场）。
        if (elapsed >= m_deps.forceWaitGiveUpThreshold) {
            if (!m_forceExecuted) {
                // 防线 3 未执行过（用户未确认/不在场）→ 补行强制终止序列：
                // "数据损失限于未归档结果（检查点保留）"的前提是任务被
                // 终止且检查点保留——不终止就放弃会遗留孤儿任务长期占锁，
                // 违背防线语义（§5.6 防线 3 的动作序列在防线 4 的自动路径
                // 同样适用）。
                executeForceSequence();
            }
            emitContextInvalidDiagnostic();
            m_drainAbandoned = true;
            finishCurrentClose();
            report.status = DrainPollReport::Status::GivenUp;
            return report;
        }
        // 防线 3：T_force 到点→对话框转为"强制结束并关闭"确认（§5.6
        // 防线 3 原文；呈现一次后不重复触发——拒绝后继续等待，T_force2
        // 兜底仍生效）。
        if (!m_forcePromptPresented && elapsed >= m_deps.forceWaitThreshold) {
            m_forcePromptPresented = true;
            report.status = DrainPollReport::Status::ForceConfirmDue;
            return report;
        }
    }

    report.status = DrainPollReport::Status::Draining;
    return report;
}

ForceCloseDialogData UiSessionController::forceCloseDialogData() const
{
    // 数据只在防线 3 触发后可取（呈现层只应在 pollDrain 报告
    // ForceConfirmDue 后调用——提前取数＝调用次序违约）。
    if (m_state != UiSessionState::Draining || !m_forcePromptPresented) {
        throw std::logic_error(
            "ui/session: forceCloseDialogData 仅在 Draining 且 T_force 触发后"
            "可取（pollDrain 报告 ForceConfirmDue 是前置）");
    }
    ForceCloseDialogData data;
    data.projectDisplayName = m_session.metadata.projectDisplayName;
    // 等待时长文本（秒，向下取整；单位显式——AGENTS §2.5 物理量单位
    // 纪律；渲染在此完成以保证防线反馈与实际等待一致，呈现层不二次计时）。
    const auto elapsedSeconds = std::chrono::duration_cast<std::chrono::seconds>(
        m_deps.steadyClock() - m_drainStartedAt);
    data.waitedText = std::to_string(elapsedSeconds.count()) + " s";
    // 影响面＝仍处非终态的任务数（现查现填——决议与呈现之间任务集可能
    // 变化，确认框显示的是当前真实影响面）。
    data.activeTaskCount = m_session.tasks
        ? m_session.tasks->nonTerminalTasks(m_session.metadata.projectId).size()
        : std::size_t{0};
    data.messageKey = kForceCloseMessageKey;
    return data;
}

void UiSessionController::resolveForceCloseDialog(bool confirmed)
{
    // 前置：Draining 中且防线 3 已触发（未触发的强杀确认＝§9.4"强杀＝
    // 独立高级操作"的旁路，拒绝）。
    if (m_state != UiSessionState::Draining || !m_forcePromptPresented) {
        throw std::logic_error(
            "ui/session: resolveForceCloseDialog 仅在 Draining 且 T_force 触发"
            "后可调用（强杀只经防线 3 确认——§9.4 关闭对话框不提供强杀选项）");
    }
    if (!confirmed) {
        // 拒绝 → 继续等待（§5.6 防线 3 的确认语义——拒绝不关闭；确认框
        // 不重复弹出，T_force2 兜底仍然生效：INV-SES-4 不依赖用户在场）。
        return;
    }
    // 确认 → 强制终止序列＋L5 abandonAll 兜底（§5.6 防线 3"执行
    // requestForceTerminate……＋L5 关闭控制器 abandonAll(ForceTerminated)
    // 兜底"原文）。接线缺失＝装配违约 fail-fast（半执行的强杀——只终止
    // 任务不释放 L5 侧资源——比显性失败更危险，禁吞错）。
    if (!m_deps.forceAbandonAll) {
        throw std::logic_error(
            "ui/session: 确认强制结束但 forceAbandonAll 未接线（装配违约——"
            "L5 关闭控制器 abandonAll(ForceTerminated) 兜底是防线 3 的组成"
            "半区，§5.6/§11.5）");
    }
    executeForceSequence();
}

bool UiSessionController::cancelDrainWait()
{
    if (m_state != UiSessionState::Draining) {
        throw std::logic_error(
            "ui/session: cancelDrainWait 仅在 Draining 态可调用（§5.2 状态表"
            "Draining 行——取消等待是 Draining 的期间允许项）");
    }
    if (m_drainAbandoned) {
        // 已 abandon（强制终止已执行/T_force2 已放弃）→ 不可恢复（§5.2
        // 状态表 Draining 行"取消等待→（仅当尚未 abandon 时）恢复显示"
        // 括注原文——强杀后的上下文处置不可逆）。
        return false;
    }
    // 恢复显示：状态回到关闭前的 Open*（writable 是 INV-SES-1 直通——
    // 关闭信号已发出但上下文尚未释放，显示恢复为只读/可写原态）；订阅
    // 保留（上下文若在恢复显示期间完成释放，回调仍会把会话推进到关闭
    // 完成——显示恢复不撤销关闭请求，requestClose 幂等）；防线状态复位
    // （再次关闭时 re-arm）。
    m_state = m_session.writable ? UiSessionState::OpenWritable
                                 : UiSessionState::OpenReadOnly;
    m_forcePromptPresented = false;
    m_forceExecuted = false;
    m_drainAbandoned = false;
    emitDevLine("drain wait cancelled: display restored for prj="
                + m_session.metadata.projectId.toCanonical());
    return true;
}

// =====================================================================
// 迟到写提示（§5.3 行 4/§5.7——UI-SES-7/UI-LCY-1）
// =====================================================================

ContextInvalidNotice UiSessionController::makeContextInvalidNotice()
{
    // 双通道出线（目录 Warning＋Dev 日志）后返回提示值——"不重试、不缓存
    // 待写"由数据形状保证：ContextInvalidNotice 只有文案键，没有动作位，
    // 也没有任何待写载荷入口（§5.7 原文）。
    emitContextInvalidDiagnostic();
    ContextInvalidNotice notice;
    notice.messageKey = kContextInvalidMessageKey;
    notice.adviceKey = kContextInvalidAdviceKey;
    return notice;
}

// =====================================================================
// 存储上下文关闭回调（subscribeClose 订阅面——IUiStoreCloseObserver）
// =====================================================================

void UiSessionController::onStoreClosed(const core::ProjectId& project)
{
    // 当前会话匹配 → 完成关闭（Draining 常规路径；取消等待后恢复显示的
    // Open* 态收到回调同样成立：关闭请求早已发出，上下文释放是既成事实，
    // 会话必须与存储上下文对齐——显示恢复不豁免关闭推进，§5.1 分离原则
    // "存储上下文释放……closed()==true"的唯一事实面）。
    if (m_session.store && m_session.metadata.projectId == project) {
        if (m_state == UiSessionState::Draining) {
            finishCurrentClose();
        } else if (hasOpenSession()) {
            emitDevLine("store closed while drain wait cancelled — finalizing: prj="
                        + project.toCanonical());
            finishCurrentClose();
        }
        return;
    }
    // 后台持有点匹配 → 释放保活引用（INV-SES-3 的回调释放路径——轮询
    // 是其兜底，见 pollDrain）。
    for (auto it = m_holds.begin(); it != m_holds.end(); ++it) {
        if (it->metadata.projectId == project) {
            emitDevLine("draining hold released (callback): prj=" + project.toCanonical());
            m_holds.erase(it);
            return;
        }
    }
    // 无匹配（重复回调/未知项目）→ 忽略＋Dev 日志（幂等防御——回调
    // 纪律被破坏时不使状态机失稳）。
    emitDevLine("store closed callback without matching session/hold (ignored): prj="
                + project.toCanonical());
}

// =====================================================================
// 私有迁移助手（状态字段的单一写点群——全部 UI 线程）
// =====================================================================

void UiSessionController::bindSession(const OpenedProjectFacts& facts,
                                      SessionPortBundle&& bindings)
{
    // 端口绑定契约校验（IUiStoreFactoryPort::open 后置——ok 却无存储
    // 端口＝适配器违约，fail-fast 不留给运行期空解引用）。
    if (!bindings.store) {
        throw std::logic_error(
            "ui/session: 打开成功但 store 端口为空（IUiStoreFactoryPort 适配器"
            "违约——SessionPortBundle.store 必填，O-31 注入契约）");
    }
    m_session.metadata = facts.metadata;
    m_session.store = std::move(bindings.store);
    m_session.drafts = std::move(bindings.drafts);
    m_session.tasks = std::move(bindings.tasks);
    m_session.writable = facts.metadata.writable;
    m_sessionDirty = false;
    m_lastAnchorProjectId = facts.metadata.projectId;  // 迟到写提示的项目锚（关闭后保留）
    // 状态迁移：writable 唯一判定（INV-SES-1——OpenWritable/OpenReadOnly
    // 的分叉只看打开结果直写位，§5.2 状态表两行进入条件原文）。
    m_state = facts.metadata.writable ? UiSessionState::OpenWritable
                                      : UiSessionState::OpenReadOnly;
    // epoch 递增（§6.2："打开成功/切换绑定新项目"——本函数是两个触发
    // 场景的共同执行点：首次打开与切换绑定各递增一次，一次绑定恰一次）。
    ++m_epoch;
    // 订阅关闭回调（§5.2 Draining 框"订阅 subscribeClose"——句柄成员
    // 持有，析构/显式 reset 即退订）。
    m_closeSubscription = m_session.store->subscribeClose(*this);
    presentContextLocked();
}

void UiSessionController::resetToNoProject()
{
    m_state = UiSessionState::NoProject;
    m_sessionDirty = false;
    m_forcePromptPresented = false;
    m_forceExecuted = false;
    m_drainAbandoned = false;
    m_drainInFlightReferences = 0;
}

void UiSessionController::presentContextLocked()
{
    // 上下文快照一次组装（ProjectContextProjection 的原子性要求——元数据
    // 与草稿位同刻，状态栏三段不得拼接，§4.2/§10.1 注入口语义）。
    ProjectContextProjection ctx;
    if (m_state == UiSessionState::OpenWritable || m_state == UiSessionState::OpenReadOnly) {
        ctx.project = m_session.metadata;
        DraftPresenceProjection drafts;
        // present＝磁盘存在未应用草稿（DraftPresenceProjection 语义锚——
        // 草稿清单非空即 present；端口快照现取，UI 线程短查询）。
        drafts.present = m_session.drafts && !m_session.drafts->listDrafts().empty();
        drafts.sessionDirty = m_sessionDirty;
        ctx.drafts = drafts;
    }
    m_context = ctx;
    if (m_deps.presentContext) {
        m_deps.presentContext(ctx);
    }
}

void UiSessionController::executeForceSequence()
{
    // 强制终止序列（§5.6 防线 3/§9.4：任务记 Failed＋EX-FORCE-TERMINATED，
    // 最近检查点保留可续——后果由 execution 承担，ui 只发请求；重查非终态
    // 集，已自然终态的任务不发强杀）。
    m_forceExecuted = true;
    m_drainAbandoned = true;
    if (m_session.tasks) {
        for (const TaskRowProjection& row :
             m_session.tasks->nonTerminalTasks(m_session.metadata.projectId)) {
            (void)m_session.tasks->requestForceTerminate(row.identity);
        }
    }
    // L5 abandonAll 兜底（§11.5 分工表"强制放弃兜底：abandonAll 调用
    // （L5）"——调用权在 L5，本接线点是 ui 侧的触发半区；接线缺失的
    // fail-fast 校验在两个触发入口（resolveForceCloseDialog/pollDrain
    // 放弃路径）完成，本函数假定已校验）。
    m_deps.forceAbandonAll();
    emitDevLine("force terminate sequence executed: prj="
                + m_session.metadata.projectId.toCanonical());
}

void UiSessionController::finishCurrentClose()
{
    // 关闭完成（§5.2 Draining→Closed→NoProject：Closed 是瞬态——状态表
    // "期间允许：—（立即转 NoProject/Opening）"原文，本调用内一次推到
    // 底；Closed 停驻不可观测，审计轨迹经 m_closeCompletedCount 计数）。
    m_closeSubscription.reset();   // 先退订（句柄析构即退订——防重复回调）
    m_session = SessionBinding{};  // 释放存储上下文引用（§5.1"回调后释放"）
    ++m_closeCompletedCount;
    ++m_epoch;                     // §6.2"关闭完成"——当前会话关闭的递增点
    const bool exitRequested = (m_drainIntent == UiCloseIntent::ExitApplication);
    resetToNoProject();
    if (exitRequested) {
        // 退出就绪位（§9.5"应用退出时由 L5/workflow 调 scheduler.shutdown(
        // DrainPolicy)＋drained()"——调度器排空调用权在 L5，ui 只置位；
        // P-UI-3 已销账：对接语义＝CancelQueuedAndWait（§9.5 原文））。
        m_exitPending = true;
    }
    presentContextLocked();        // 无项目首页快照（PM-10）
}

// =====================================================================
// 诊断/日志出线（§3.5 码表＋Dev 通道）
// =====================================================================

void UiSessionController::emitDevLine(const std::string& message) const
{
    // Dev 级事实唯一出线（§6.2——Dev 码不入目录，devLog 允许为空＝显式
    // 声明的无日志场景，空时不虚构通道）。
    if (m_deps.devLog) {
        m_deps.devLog->logDev(kSessionDevChannel, message);
    }
}

void UiSessionController::emitContextInvalidDiagnostic()
{
    // Dev 通道恒出线（§5.6 极端路径的排障半区——无论是否有目录，开发侧
    // 都应可见；"（Dev 级）"排障明文与目录条目双通道并存，见下）。
    emitDevLine(
        "UI-SESSION-CONTEXT-INVALID: 已释放上下文的写请求被拒/等待放弃"
        "（context-closed 呈现；不重试、不缓存待写，§5.3 行 4/§5.7）");
    // 目录通道（码表权威——§3.5 行：internal/Warning，用户可见；无目录
    // 场景跳过不虚构条目）。创建经 IDiagnosticFactory::create 唯一入口
    // （§9.2——码必须已在 StableCodeRegistry 登记，未登记＝装配遗漏，
    // 工厂 CodeUnknown 拒绝，不允许绕过注册表产码）。
    if (!m_deps.diagFactory || !m_deps.diagSink) {
        return;
    }
    core::DiagnosticRecord record = core::DiagnosticRecord::make(
        "UI-SESSION-CONTEXT-INVALID",
        core::ObjectId::generate(),
        std::nullopt,   // localName（无作用对象定位——上下文级提示）
        std::nullopt,   // runtimeName（同上）
        "项目上下文已释放，操作未执行（建议重新打开项目）",
        "对已释放存储上下文的写请求被拒绝（context-closed），或关闭等待超时"
        "放弃（数据损失限于未归档结果，检查点已保留）",
        "重新打开项目（入口编排归 workflow）；不重试、不缓存待写",
        std::nullopt);  // 比较型三要素（非数值判定场景——缺省不携带）
    diagnostics::DiagContext context;
    context.sourceUnit = "ui";                       // §4.2 sourceUnit 词表含 ui
    context.sourceInterface = "session.context-invalid";
    // 项目锚定（迟到写提示总能对应到具体项目——绑定期的"最后上下文项目"
    // 在关闭完成后仍保留：UI-LCY-1 场景＝Closed 后的迟到提交，会话绑定
    // 已清空但提示仍应锚定来源项目）。
    context.project = m_lastAnchorProjectId;
    m_deps.diagSink->append(m_deps.diagFactory->create(record, context));
}

}  // namespace ui
}  // namespace ird
