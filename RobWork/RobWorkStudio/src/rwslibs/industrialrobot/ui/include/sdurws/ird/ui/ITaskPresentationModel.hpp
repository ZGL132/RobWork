/**
 * @file   ITaskPresentationModel.hpp
 * @brief  任务呈现模型（§10.7）——任务清单/进度轮询/控制请求/归档徽标/
 *         结果当前性徽标的唯一数据装配面（§9.4/§9.5 任务呈现的落位契约）。
 *
 * 设计依据：
 *   - units/ui.md §9.4（任务进度/取消/结果显示全表：9 态短标签＋当前
 *     置顶、200 ms 轮询〔percent 单调/回退丢弃/新 attempt 重置/重复
 *     幂等〕、按能力声明启用的暂停/继续、强杀独立确认、worker 崩溃
 *     重跑入口、Completed 徽标附归档子标、当前性过期附原因且历史可
 *     查看、UI 线程不阻塞等待任务）、§9.5（关闭对话框任务区＝非终态
 *     过滤）、§10.7（ITaskPresentationModel 接口原文——本任务即其
 *     "首消费冻结"落位任务，签名按 O-31 值投影机制冻结并登记 §16.7
 *     v1.5）、§6.3（九态短标签键）、§5.4/§6.2（关闭项目后旧任务进
 *     后台只读清单——P-UI-7 建议口径）；
 *   - execution.md §4.2/§6.1/§10.1/§10.2（TaskSnapshot/ProgressReport/
 *     控制接口语义——C-8 端口的冻结基准，O-31 值投影承载）；
 *   - 需求 TASK-01/02/03（任务呈现/取消/结果）、UX-03（正常取消无错误
 *     呈现）、PM-03、NFR-REL-02（worker 崩溃界面不退出）、NFR-PERF-02
 *     （ui 侧只发请求不等收敛）；
 *   - 任务契约 tasks/foundation/UI-T13.json acceptance 3（UI-TSK-1~5
 *     逐项）＋P-UI-4/P-UI-7 处置。
 *
 * 背景说明（红线边界）：任务状态/进度/能力的权威在 execution（N-4），
 * 本模型零复算——九态短标签是文案键投影（UI-T04 taskStateLabelKey 同源）、
 * 归档相位/终结原因是 token 直读、当前性徽标是 C-7 判定结果的搬运
 * （"由 evidence 提供，ui 不计算"——§9.4 原文）。模型唯一"计算"是
 * §9.4 明文规定的呈现规则：进度乱序处置（单调/重置/幂等）与清单排序
 * （当前置顶）——两者都是呈现纪律，不是业务判定。
 *
 * 线程约束：公共方法一律 UI 线程（§10.7 线程行）；端口实现内部转发
 * execution 任意线程入口（execution 接口线程安全）。**全部即发即忘**——
 * 控制请求返回 Ack 即返回，收敛经状态事件/轮询回看，接口不存在任何
 * "等待任务完成"的形态（§9.4"UI 不阻塞等待任务"原文）。
 *
 * 头文件零 Qt——模型层可无 GUI 测试（§12.1 第一层分工）。
 */

#ifndef SDURWS_IRD_UI_ITASKPRESENTATIONMODEL_HPP
#define SDURWS_IRD_UI_ITASKPRESENTATIONMODEL_HPP

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Evaluation.hpp>      // core::TaskState（九态——表内登记边）
#include <sdurws/ird/core/Events.hpp>          // core::IEventSubscription（subscribe 返回句柄）
#include <sdurws/ird/core/Identity.hpp>        // core::ProjectId/TaskIdentity（身份类型与 core 契约同一）
#include <sdurws/ird/ui/UiProjections.hpp>     // TaskViewProjection/TaskProgressProjection/UiTaskAck/CurrentnessProjection（UI-T13 投影族）
#include <sdurws/ird/ui/UiTypes.hpp>           // TextKey（§3.5）

namespace sdurws::ird {

namespace diagnostics {
struct IDevLogSink;   ///< 开发日志路由（Catalog.hpp 完整定义——前置声明避免整头包含）
}

namespace ui {

class IUiTaskPresentationPort;   ///< C-8 任务呈现面端口（UiPorts.hpp——指针成员前置声明）
class IUiCurrentnessSource;      ///< C-7 当前性端口（UiPorts.hpp——同上）

// =====================================================================
// §10.7 TaskRow（行模型——snap 为 ui 投影承载）
// =====================================================================

/**
 * @brief 任务清单行（§10.7 TaskRow——snap.stateLabelKey/progress 三段）。
 *
 * 语义锚点（§10.7 原文逐字段）：snap＝任务快照（§10.7 为 execution::
 * TaskSnapshot，O-31 下以 TaskViewProjection 承载——值拷贝）；state
 * LabelKey＝九态短标签文案键（taskStateLabelKey 产出——§6.3 词表）；
 * progress＝模型合并后的展示进度（§9.4 乱序处置后的值，nullopt＝尚无）。
 */
struct TaskRow {
    /// 任务快照投影（execution::TaskSnapshot＋TaskCapability 呈现面）。
    TaskViewProjection snap;
    /// 九态短标签文案键（taskStateLabelKey(snap.state)——PM-03/PM-11）。
    TextKey stateLabelKey;
    /// 展示进度（轮询合并后的值——回退值不会出现在此）。
    std::optional<TaskProgressProjection> progress;
};

// =====================================================================
// §9.4 进度合并判定（乱序/重复三规则的唯一权威实现——纯函数）
// =====================================================================

/**
 * @brief 进度更新判定（§9.4"乱序/重复处理"行的词表化）。
 */
enum class ProgressMergeDecision : std::uint8_t {
    Accept,         ///< 接受（首次/正常前进）
    DropRegression, ///< 回退丢弃（同 attempt 内 percent 下降——丢弃＋Dev 日志）
    AttemptReset,   ///< 新 attempt（基线重置——无论高低一律接受）
    Idempotent,     ///< 重复幂等（同 attempt 同 percent——无变化不刷新）
};

/**
 * @brief 判定一次进度更新如何并入展示基线（§9.4 三规则唯一实现点，
 *        纯函数）。
 *
 * 规则（§9.4 原文逐条）：
 *   - "同 (taskId, attemptId) 内 percent 单调（回退值丢弃＋Dev 日志）"
 *     ——同 attempt 且 percent 下降→DropRegression；
 *   - "新 attempt 重置基线"——attempt 变化→AttemptReset（接受）；
 *   - "重复值幂等"——同 attempt 同 percent→Idempotent（相同值重复
 *     到达不触发界面刷新）。
 *
 * @param previous        [in] 展示基线（nullopt＝尚无进度——Accept）
 * @param previousAttempt [in] 基线所属尝试（行快照的 attempt）
 * @param incomingAttempt [in] 来报所属尝试（轮询快照的 attempt）
 * @param incoming        [in] 来报进度
 * @return 判定（调用方按判定并入/丢弃并记 Dev 日志——模型实现）
 */
ProgressMergeDecision mergeProgressDecision(
    const std::optional<TaskProgressProjection>& previous,
    core::AttemptId previousAttempt, core::AttemptId incomingAttempt,
    const TaskProgressProjection& incoming);

// =====================================================================
// §9.4 归档子标／动作可用性／结果当前性徽标（纯函数装配）
// =====================================================================

/**
 * @brief Completed 徽标的归档子标（§9.4"任务完成≠归档完成"呈现值）。
 *
 * present=false＝该任务无归档语义（Preview 或 token 未知——不虚构）；
 * warning=true＝ArchiveFailed（警告标记；EX-ARCHIVE-FAILED 诊断本体经
 * 诊断目录呈现——徽标只承载警告位，不复制诊断事实）。
 */
struct ArchiveBadge {
    /// 是否有归档子标（false＝无归档语义——徽标只有"已完成"主标）。
    bool present = false;
    /// 归档失败警告位（true＝ArchiveFailed——警告标记＋诊断）。
    bool warning = false;
    /// 子标文案键（present 时非空——ui.task.archive.* 词表）。
    TextKey sublabelKey;
};

/**
 * @brief 归档阶段 token→子标（纯函数；execution.md §9 词表直读）。
 *
 * @param archivePhaseToken [in] TaskViewProjection.archivePhaseToken
 *                          （not-applicable/reserved/archiving/archived/
 *                          archive-failed；空串＝无归档语义）
 * @return 子标值（未知 token→present=false——词表演进前的安全兜底，
 *         不虚构归档状态）
 */
ArchiveBadge archiveBadgeFor(const std::string& archivePhaseToken);

/**
 * @brief 行级动作可用性（§9.4 重跑入口/历史查看的呈现判别——纯函数）。
 *
 * rerunAvailable＝Failed ∨ Interrupted（worker 崩溃"诊断＋重跑〔新
 * TaskId——崩溃不自动重试〕"、中断"已中断可重跑"；重跑的提交归编排
 * 方，本位只是入口呈现）；historyAvailable＝终态（历史结果查看——
 * TASK-03"不成为当前结果"；经 project 查询端口浏览，绑定修订）。
 */
struct TaskRowActions {
    bool rerunAvailable = false;     ///< 重跑入口可用（Failed/Interrupted）
    bool historyAvailable = false;   ///< 历史结果查看可用（终态）
};

/// 判定行级动作可用性（纯函数——见 TaskRowActions 注释）。
TaskRowActions taskRowActionsFor(const TaskViewProjection& row);

/**
 * @brief 结果当前性徽标（§9.4"徽标 Current/Superseded（附原因）——由
 *        evidence 提供，ui 不计算"的搬运值）。
 *
 * available=false＝C-7 判定不可用（不可判定/未注入——呈现"无法判定"
 * 口径归 §6.3 七态求值〔UI-T04〕，任务徽标侧不显示、不虚构）。过期
 * 原因逐条原样搬运（CurrentnessProjection.reasons[].detail——对端产
 * 出的人读摘要，呈现层原文显示）；**无「正式通过」字样**——本徽标词
 * 表只有"当前/已过期"（显示纪律放行归 formalPassRenderable，UI-T04）。
 */
struct ResultCurrentnessBadge {
    /// 徽标可用（false＝不可判定/未注入——不显示徽标，不虚构）。
    bool available = false;
    /// true＝Current（ui.task.currentness.current）；false＝Superseded。
    bool current = false;
    /// 徽标文案键（available 时非空）。
    TextKey labelKey;
    /// 过期原因逐条（Superseded 时非空——detail 原样搬运＋依赖键）。
    std::vector<std::string> reasonDetails;
};

/**
 * @brief C-7 当前性投影→徽标（纯函数搬运——判定权威在 evidence）。
 */
ResultCurrentnessBadge resultBadgeFor(const CurrentnessProjection& currentness);

// =====================================================================
// §10.7 ITaskViewObserver（呈现观察者——UI 线程回调）
// =====================================================================

/**
 * @brief 任务呈现观察者（§10.7 subscribe 的回调面——回调在 UI 线程，
 *        模型是同步调用面无跨线程回调）。
 */
class ITaskViewObserver {
public:
    virtual ~ITaskViewObserver() = default;

    /// 任务清单变更（状态迁移/增删/排序——调用方整体重拉 activeTasks）。
    virtual void onTaskRowsChanged() = 0;

    /// 单任务进度更新（idempotent 重复不回调——界面局部刷新面）。
    virtual void onProgressUpdated(const core::TaskIdentity& task) = 0;
};

// =====================================================================
// 强杀独立确认数据（§9.4"强杀＝独立高级操作（带确认＋后果说明）"）
// =====================================================================

/**
 * @brief 强制终止确认对话数据（独立于关闭对话框——§9.4 原文"关闭
 *        对话框不提供强杀选项"，本对话只在任务面板的用户强杀动作中
 *        打开）。
 */
struct ForceTerminateConfirmationData {
    /// 目标任务身份（对话标题行呈现用——呈现名经名称端口解析，模型
    /// 不解析身份文本）。
    core::TaskIdentity task;
    /// 对话标题文案键（固定值 ui.task.force.title）。
    TextKey titleKey;
    /// 后果说明文案键（固定值 ui.task.force.consequence——"任务记
    /// Failed、检查点保留"§9.4 原文口径）。
    TextKey consequenceKey;
};

// =====================================================================
// §10.7 ITaskPresentationModel（接口——首消费冻结签名，O-31 承载）
// =====================================================================

/**
 * @brief 任务呈现模型（§10.7 接口——清单/控制/轮询/订阅的唯一装配面）。
 *
 * 契约要点（§10.7 契约表逐行）：控制类调用前置＝任务属于当前会话项目
 * （后台清单任务→拒绝——DisableReason 承载于 UiTaskAck.background
 * Rejected＋reasonKey，P-UI-7 建议口径"保持只读"）；控制请求按到达序
 * 送达；**无 UI 线程阻塞等待**（全部即发即忘＋事件/轮询回看）；快照
 * 值拷贝；近终态任务保留窗口由对端会话内存态决定（D-07），模型不私设
 * 保留期。非法（§10.7"非法"行）：阻塞等待任务完成；把 Canceled/Failed/
 * Interrupted 呈现为通过；后台任务控制；在 ui 侧重算九态/归档相位。
 */
class ITaskPresentationModel {
public:
    virtual ~ITaskPresentationModel() = default;

    // ---- 清单查询（§10.7 前两方法——当前/后台双清单）----

    /**
     * @brief 当前会话项目的任务行（§10.7"非终态＋近终态窗口"——近终
     *         态保留窗口由对端决定；排序＝当前置顶（taskRowLess 稳定序）。
     */
    virtual std::vector<TaskRow> activeTasks() const = 0;

    /**
     * @brief 后台排空项目的任务行（§5.7/§9.4——只读清单：控制请求
     *         被模型自限拒绝；显示最后一次已知快照）。
     */
    virtual std::vector<TaskRow> backgroundTasks() const = 0;

    /**
     * @brief 按身份取行（先当前清单后台清单；不存在→nullopt）。
     */
    virtual std::optional<TaskRow>
    task(const core::TaskIdentity& task) const = 0;

    // ---- 控制请求（§10.7——即发即忘，Ack 投影返回）----

    /**
     * @brief 请求协作取消（§9.4"取消＝协作（正常取消无错误诊断）"）。
     */
    virtual UiTaskAck requestCancel(const core::TaskIdentity& task) = 0;

    /**
     * @brief 请求暂停（§9.4"按任务能力声明启用；不支持暂停→feedback
     *         显式提示不静默"——见实现注释的双层反馈保证）。
     */
    virtual UiTaskAck requestPause(const core::TaskIdentity& task) = 0;

    /**
     * @brief 请求继续（§9.4 Paused⇄Running——新 attempt；进度基线随
     *         AttemptReset 规则重置）。
     */
    virtual UiTaskAck requestResume(const core::TaskIdentity& task) = 0;

    /**
     * @brief 请求强制终止（§10.7"高级操作＋确认"——userConfirmed 必须
     *         来自独立确认对话〔forceTerminateConfirmation 装配数据→
     *         用户确认〕；未确认→显式拒绝 Ack，不发对端）。
     *
     * @param task          [in] 目标任务
     * @param userConfirmed [in] 用户已在独立确认对话中确认
     */
    virtual UiTaskAck requestForceTerminate(const core::TaskIdentity& task,
                                            bool userConfirmed) = 0;

    /**
     * @brief 装配强杀确认对话数据（独立确认流的入口——后果说明固定
     *         文案，目标身份直读）。
     */
    virtual ForceTerminateConfirmationData
    forceTerminateConfirmation(const core::TaskIdentity& task) const = 0;

    // ---- 进度轮询（§10.7——默认 200 ms）----

    /**
     * @brief 启动进度轮询（§10.7 默认 200 ms——轮询的心跳归 L5 定时器，
     *         模型提供 pollOnce 执行面；interval 仅登记当前周期）。
     * @param interval [in] 轮询周期（>0；缺省调用请传默认常量
     *                 kDefaultProgressPollInterval）
     */
    virtual void startProgressPolling(std::chrono::milliseconds interval) = 0;

    /**
     * @brief 停止轮询（后置：轮询停止后无残余定时器事件——§10.7 后置
     *         行；本方法只翻位，L5 定时器随之停摆）。
     */
    virtual void stopProgressPolling() = 0;

    /**
     * @brief 执行一轮进度轮询（L5 定时器心跳的模型执行面——对每活动
     *         行取快照并按 mergeProgressDecision 并入；幂等可重入）。
     */
    virtual void pollOnce() = 0;

    /**
     * @brief 当前轮询周期（测试/L5 观测面——默认 200 ms）。
     */
    virtual std::chrono::milliseconds progressPollInterval() const = 0;

    // ---- 清单刷新与订阅 ----

    /**
     * @brief 全量刷新当前项目清单（TaskStatusChanged 事件的消费半区——
     *         重拉快照、并入进度基线、重排、通知观察者）。
     */
    virtual void refresh() = 0;

    /**
     * @brief 订阅呈现变更（RAII——析构即退订；回调 UI 线程）。
     */
    virtual std::unique_ptr<core::IEventSubscription>
    subscribe(ITaskViewObserver& observer) = 0;

    // ---- 会话联动（§5.4/§6.2——关闭项目后旧任务进后台只读清单）----

    /**
     * @brief 绑定当前会话项目（打开成功/切换绑定后由会话路径调用——
     *         清单切换到新项目并刷新）。
     */
    virtual void attachProject(const core::ProjectId& project) = 0;

    /**
     * @brief 当前项目降级为后台只读清单（关闭/切换的持有点路径调用——
     *         §9.4"关闭项目后旧任务进后台只读清单（控制按钮禁用）"；
     *         P-UI-7 建议口径的编程面）。
     */
    virtual void demoteCurrentToBackground() = 0;
};

/// 进度轮询默认周期（§10.7"默认 200ms"——ms 单位显式常量）。
inline constexpr std::chrono::milliseconds kDefaultProgressPollInterval{200};

// =====================================================================
// 装配依赖与工厂
// =====================================================================

/**
 * @brief 任务呈现模型的装配依赖（L5 装配期注入；指针非 owning）。
 */
struct TaskPresentationDeps {
    /// C-8 任务呈现面端口（**必注入**——无端口则模型无数据/控制面，
    /// 工厂 fail-fast）。
    IUiTaskPresentationPort* taskPort = nullptr;
    /// C-7 当前性端口（可空——结果徽标不可用态，须显式声明语义）。
    IUiCurrentnessSource* currentness = nullptr;
    /// 开发日志路由（可空——回退丢弃/装配异常的 Dev 记录静默跳过）。
    diagnostics::IDevLogSink* devLog = nullptr;
};

/**
 * @brief 创建任务呈现模型（UI 线程构造）。
 *
 * @param deps [in] 装配依赖（taskPort 空→抛 std::invalid_argument）
 * @return 模型实例
 *
 * @throws std::invalid_argument taskPort 为空（装配契约违约）
 */
std::unique_ptr<ITaskPresentationModel>
createTaskPresentationModel(TaskPresentationDeps deps);

}  // namespace ui
}  // namespace ird

#endif  // SDURWS_IRD_UI_ITASKPRESENTATIONMODEL_HPP
