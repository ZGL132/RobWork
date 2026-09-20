/**
 * @file   TaskStatusSource.hpp
 * @brief  execution 任务状态只读投影注入接口（ITaskStatusSource）与投影值
 *         类型（TaskStatusProjection/TaskProgressProjection/
 *         TaskTerminationKind）——报告章节任务终态/"已中断"呈现的数据源
 *         （RPT-T12 产物）。
 *
 * 设计依据：
 *   - units/reporting.md §3.3（execution 投影＝注入式最小接口＋值传递——
 *     "reporting 侧定义 ITaskStatusSource 最小接口，L5 适配"；事件
 *     TaskStatusChanged/ResultArchived 仅用于预览刷新提示、不入报告内容）、
 *     §9.9 execution 行（任务投影 tryTask/progress→章节终态呈现；任务状态
 *     引用＝TaskIdentity〔呈现，非结论〕；execution 不调 reporting）、
 *     §2.1 C-7（execution 任务投影消费行）、§3.2 依赖图（零 execution
 *     编译边——execution 与 reporting 同层且 execution 依赖面更宽，反向边
 *     无必要）
 *   - 需求 TASK-02（任务终态/中断呈现——NFR-REL-03"中断任务显示'已中断'"
 *     的报告侧承接）、RPT-01-B（章节呈现不得把未终态任务当作结论）
 *   - 任务契约 tasks/foundation/RPT-T12.json acceptance 3/5
 *
 * 背景说明（为什么查询键是 core::TaskIdentity 而对端值形态出自 execution）：
 *   execution 的 ITaskScheduler::tryTask(TaskId) 返回 TaskSnapshot 只读
 *   投影、ITaskController::progress(TaskId) 返回进度——二者的**值形态**
 *   （状态词/进度四字段/终结原因五值）是本投影的对齐基准（§3.3 原文
 *   "对齐 execution ITaskScheduler::tryTask/ITaskController::progress 的
 *   值形态"）。但 TaskId/TerminationCause/ArchivePhase 是 execution 自有
 *   类型，本头受"零 execution 类型"纪律约束（§3.3 注入形态——公共头出现
 *   对端类型即表外边＝构建失败，SA-10）。因此：
 *   - 查询键与任务引用＝core::TaskIdentity 五元组（core 词表——§9.9
 *     引用边界行"任务状态引用=TaskIdentity"的逐字落位；TaskId 与
 *     TaskIdentity 的对应关系由 L5 适配器经 execution 登记表解析）；
 *   - 状态轴直接复用 core::TaskState 九态（词表归 core——core.md §4.7，
 *     core 是 ARCH §3.5 登记边；token 消费 core::toToken(TaskState)，
 *     零第二词表）；
 *   - 终结原因（原因轴，execution TerminationCause 五值）以 reporting
 *     自有投影词表 TaskTerminationKind 承载（同名一一映射——落位增量，
 *     §14.4 v0.15 登记）；
 *   - 进度以 TaskProgressProjection 四字段承载（对齐 ProgressReport 的
 *     percent/phaseToken/batchesDone/batchesTotal 值语义）。
 *
 * 与报告内容的边界（§3.3 原文——本头最易误用处）：
 *   报告**内容**只绑定明确 ID 的冻结事实；任务投影只服务章节的任务终态/
 *   "已中断"**呈现**（NFR-REL-03），不构成工程结论（任务 Completed 不意味
 *   工程通过——两轴正交，execution.md §5.6 同源纪律）。execution 事件
 *   （TaskStatusChanged/ResultArchived）不进入报告内容，仅可由 ui 用于
 *   预览刷新提示；本接口是**拉式只读查询**，无事件订阅面。
 *
 * 线程安全：实现方承诺并发只读安全（对齐 tryTask 的"查询永不被状态写
 *   阻塞"投影纪律）；本头全部类型为纯值类型。
 * 确定性：同身份同投影（会话内只读一致视图——§9.9 execution 行"会话内
 *   只读"；单元测试 SameIdentitySameProjection 钉住）。
 */

#ifndef SDURWS_IRD_REPORTING_TASKSTATUSSOURCE_HPP
#define SDURWS_IRD_REPORTING_TASKSTATUSSOURCE_HPP

#include <cstdint>
#include <optional>
#include <string>

#include <sdurws/ird/core/Evaluation.hpp>   // core::TaskState（九态词表归 core——core.md §4.7；core 是登记边）
#include <sdurws/ird/core/Identity.hpp>     // core::TaskIdentity/core::AttemptId（任务引用五元组与尝试序号）

namespace sdurws::ird::reporting {

// =====================================================================
// 终结原因投影词表（execution TerminationCause 值域的呈现投影）
// =====================================================================

/**
 * @brief 任务终结原因投影（§3.3 对齐值形态的原因轴——落位增量，
 *        §14.4 v0.15 登记）。
 *
 * 五值与 execution TerminationCause（execution.md §4.2 termination 行）
 * 同名一一对应，由 L5 适配器做一一映射：
 *   - Canceled/Failed/Completed/Interrupted＝四类终态（与 core::TaskState
 *     终态同名——状态轴与原因轴两轴分立、终态同名但语义不同轴）；
 *   - ForceTerminated＝显式强杀标记（状态轴上是 Failed——本词携带区分
 *     标记，报告呈现"不伪装为普通失败"，execution.md T13 同源纪律）。
 *
 * 为什么不复用 core::TaskState 表达终结原因：core 词表无"强杀"标记
 * （状态轴九态里没有它），而 NFR-REL-03 要求中断呈现、"不伪装为普通失败"
 * 要求强杀可区分——原因轴独立成词表是呈现诚实性的最小承载。
 */
enum class TaskTerminationKind {
    Canceled,         ///< 用户取消（UX-03 正常取消，非错误）
    Failed,           ///< 执行失败
    Completed,        ///< 正常完成
    Interrupted,      ///< 中断（主进程异常退出等——NFR-REL-03"已中断"呈现）
    ForceTerminated,  ///< 强制终止（卡死强杀——呈现上区别于普通失败）
};

/**
 * @brief 终结原因 token（呈现/日志词面——只增不改名；与 core 词表 kebab
 *        风格一致）。
 * @param v [in] 终结原因投影值
 * @return "canceled"/"failed"/"completed"/"interrupted"/"force-terminated"
 *         （前四者与 core::TaskState 四终态 token 逐字相同——两轴终态同名
 *         的词面一致性；"force-terminated" 为原因轴单列标记）
 */
inline const char* toToken(TaskTerminationKind v) noexcept
{
    switch (v) {
    case TaskTerminationKind::Canceled:        return "canceled";
    case TaskTerminationKind::Failed:          return "failed";
    case TaskTerminationKind::Completed:       return "completed";
    case TaskTerminationKind::Interrupted:     return "interrupted";
    case TaskTerminationKind::ForceTerminated: return "force-terminated";
    }
    return "failed";  // 不可达（穷举 switch 已覆盖全值域）——兜底返回，防 MSVC 警告 C4715
}

// =====================================================================
// 投影值类型（对齐 execution TaskSnapshot/ProgressReport 的呈现面）
// =====================================================================

/**
 * @brief 进度投影（§3.3 对齐 ITaskController::progress 值形态——四字段与
 *        execution ProgressReport 逐字段同语义）。
 *
 * 值域（对齐 ProgressReport.make 前置）：percent ∈ [0,100]；phaseToken
 * 非空；batchesDone ≤ batchesTotal；batchesTotal==0＝批次总数未声明（此时
 * done 应为 0）。投影**忠实承载**实现方给出的值、不再校验（校验在 execution
 * 侧构造边界——reporting 是呈现通道不是校验闸；注入面契约要求实现方输入
 * 已合法值）。
 */
struct TaskProgressProjection {
    int percent = 0;                 ///< 完成百分比 [0,100]（无量纲，非物理量）
    std::string phaseToken;          ///< 进度阶段 token（非空——UX-10 机器判读面）
    std::uint64_t batchesDone = 0;   ///< 已完成批次数（计数，无量纲；≤ batchesTotal）
    std::uint64_t batchesTotal = 0;  ///< 批次总数（0＝未声明——允许，此时 done 应为 0）

    bool operator==(const TaskProgressProjection& o) const noexcept
    {
        return percent == o.percent && phaseToken == o.phaseToken
            && batchesDone == o.batchesDone && batchesTotal == o.batchesTotal;
    }
    bool operator!=(const TaskProgressProjection& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 任务状态投影（§3.3 对齐 ITaskScheduler::tryTask 值形态 TaskSnapshot
 *        的呈现面——报告章节任务终态/"已中断"呈现的数据源）。
 *
 * 字面对齐关系（L5 适配器映射义务；acceptance 3）：
 *   - identity ← TaskSnapshot.taskId＋run＋attempt 经 execution 登记表
 *     组装为 core::TaskIdentity（五元组——§9.9 引用边界）；
 *   - state ← TaskSnapshot.state（core::TaskState 九态直传——零转词）；
 *   - progress ← TaskSnapshot.progress（四字段直传；nullopt＝未上报过
 *     进度——Queued 期或无进度任务的如实形态）；
 *   - termination ← TaskSnapshot.termination（五值一一映射；仅终态非空
 *     ——与 TaskSnapshot 的"仅终态非空"纪律一致）。
 *
 * 呈现纪律（TASK-02/RPT-05 同源）：任务终态/中断仅作**呈现**——不构成
 * 工程结论（Completed≠工程通过）；"已中断"必须按 Interrupted 如实呈现
 * （NFR-REL-03——不得渲染为失败或通过）；非终态任务不得出现在结论性
 * 呈现位（报告内容只绑定冻结事实——§3.3）。
 */
struct TaskStatusProjection {
    /// 任务引用（五元组——§9.9"任务状态引用=TaskIdentity〔呈现，非结论〕"）。
    core::TaskIdentity identity{};
    /// 状态机状态（core 九态词表——零第二词表）。
    core::TaskState state = core::TaskState::Queued;
    /// 最近进度投影（nullopt＝未上报过进度——如实承载，不伪造 0% 进度）。
    std::optional<TaskProgressProjection> progress;
    /// 终结原因（仅终态非空——非终态任务必须为 nullopt；实现方契约）。
    std::optional<TaskTerminationKind> termination;

    /// 全字段精确相等（值语义——同身份同投影确定性的比较面）。
    bool operator==(const TaskStatusProjection& o) const
    {
        return identity == o.identity && state == o.state
            && progress == o.progress && termination == o.termination;
    }
    bool operator!=(const TaskStatusProjection& o) const { return !(*this == o); }
};

// =====================================================================
// ITaskStatusSource（§3.3 注入接口——最小面）
// =====================================================================

/**
 * @brief execution 任务状态只读查询源（注入接口——L5 装配适配 execution
 *        的 ITaskScheduler/ITaskController 只读查询）。
 *
 * "最小接口"的落点（§3.3 原文措辞）：恰一个查询方法——无提交/取消/暂停
 * 等任何控制面（控制归 execution 自己的端口），无事件订阅（事件仅作预览
 * 刷新提示、不入报告内容——§3.3），无批量枚举（报告按明确 ID 逐任务呈现
 * ——§9.9 引用边界）。
 *
 * 生命周期与所有权：实现方（L5 装配）持有 execution 侧句柄；消费方以
 * 借用语义在会话内使用，不接管实现对象所有权（与 IModelSummaryProvider
 * 同款约定）。
 */
class ITaskStatusSource {
public:
    virtual ~ITaskStatusSource() = default;

    /**
     * @brief 取任务的只读状态投影（try 轨——不抛）。
     *
     * 前置：task 为有效五元组（identity.isValid()==true——调用方保证；
     * 无效身份属调用方违约，实现方按 try 轨返回 nullopt 不抛——§1.4 try*
     * 约定的注入面形态）。
     *
     * 后置：只读值投影（深拷贝语义——调用方持有，后续任务状态演化不影响
     * 已取回投影）；未登记/已从登记表移除→nullopt（不伪造占位状态）；
     * 命中时 state 忠实投影、仅终态携带 termination。
     *
     * @param task [in] 任务身份五元组（project/branch/revision/run/attempt
     *             全字段有效）
     * @return 状态投影值；任务未登记/不可解析＝nullopt（不抛）
     *
     * 线程安全：并发只读安全（查询不被状态写阻塞——TaskSnapshot 投影纪律
     * 同源）；对同身份同会话幂等（一致读取视图）。
     * 副作用：零（只读查询——不派发任务、不写任何状态）。
     */
    virtual std::optional<TaskStatusProjection> tryStatus(core::TaskIdentity task) const = 0;
};

// =====================================================================
// L5 适配建议（卡行产物列"＋L5 适配建议"——适配代码归 L5 装配，
// 本任务不建 execution 编译边；此处为装配期指引，非本单元代码）
// =====================================================================
/*
 * 适配方向：execution ITaskScheduler::tryTask / ITaskController::progress
 * （TaskId 键）→ reporting ITaskStatusSource::tryStatus（TaskIdentity 键）。
 *
 * 装配要点（供 L5 装配会话执行，非本单元义务）：
 *   1. 适配器持有 execution 调度器/控制器引用与 TaskId↔TaskIdentity 登记表
 *      查询通道（execution RunRegistry——任务派发登记的权威）；
 *   2. tryStatus(task) 映射：以 task（五元组）查登记表得 TaskId→
 *      scheduler.tryTask(taskId) 取 TaskSnapshot→逐字段投影（state 直传/
 *      progress 四字段直传/termination 五值一一映射）；
 *   3. 快照缺席（任务未登记/已过保留窗口）→nullopt——不伪造；
 *   4. 纪律：适配器只读（不调 submit/cancel/pause 等控制面）；并发查询
 *      直通 execution 投影纪律（查询不被写阻塞）。
 */

}  // namespace sdurws::ird::reporting

#endif  // SDURWS_IRD_REPORTING_TASKSTATUSSOURCE_HPP
