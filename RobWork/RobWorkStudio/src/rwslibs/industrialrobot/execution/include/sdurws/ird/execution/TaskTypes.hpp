/**
 * @file   TaskTypes.hpp
 * @brief  任务身份与记录类型——TaskId 强类型、TaskSubmission/TaskRecord/
 *         TaskSnapshot、能力声明承载（EvaluatorRuntimeCapabilities）、
 *         进度与终结原因（§4 任务/运行/尝试身份＋§5.5 能力声明）。
 *
 * 设计依据：
 *   - units/execution.md §4（三级身份概念总表与混用防线、记录字段表、
 *     身份分配协议）、§5.5（能力声明——TASK-01）、§5.6（TaskState→
 *     TaskOutcome 终态映射——core.md §10.3 交接项的承接落点）、§3.1
 *     （TaskTypes.hpp 组成行：TaskId、TaskSubmission、TaskCapability、
 *     TaskPriority、TaskSnapshot、ProgressReport、TerminationCause、
 *     TaskRecord 投影）
 *   - 需求 TASK-01（状态机＋能力声明）、TASK-03（五元组身份——值类型归
 *     core，execution 只分配与携带）、NFR-REL-03（中断任务显示"已中断"）
 *   - 任务契约 tasks/foundation/EX-T02.json acceptance 2/3/4（三级身份并发
 *     分配唯一；forceTerminateCost 仅声明与呈现；能力声明经注册表扩展声明
 *     ——P-EX-7 处置）
 *
 * 背景说明（三级身份的所有权边界，§4.1 混用防线——本头最易误读处）：
 *   - TaskId＝"用户提交的一次逻辑任务"是哪一个。core **未定义**此类型，
 *     按卡 §4.1 由 execution 以 core §4.1 同纪律新增（tag "tsk-"、全零
 *     保留、往返严格、强类型），属 execution 单元数据模型，**不进** core
 *     事件五元组；
 *   - RunId/AttemptId/TaskIdentity（五元组）值类型归 core
 *     （sdurws/ird/core/Identity.hpp），execution 只分配与携带、零重定义
 *     （§2.2 不可越界列；契约测试 IdentityVocabularyContractTest 钉住）；
 *   - 分配协议（§4.3）：提交受理→TaskId::generate()；派发登记→
 *     core::RunId::generate()＋AttemptId=1；暂停后继续/检查点恢复→同
 *     RunId、AttemptId+1（旧 attempt 由 RunRegistry 移入 superseded——
 *     登记表归 EX-T04，本头只承载记录字段）。
 *
 * 持久化边界（§5.4/D-13/P-EX-6）：本头全部类型为**会话内存值类型**——
 * 不提供任何磁盘序列化设施；任务磁盘痕迹唯一经 project（归档预留
 * results/<run-id>/ 与 checkpoints/<run-id>/<seq>/）。Queued 期任务纯内存，
 * 主进程崩溃即消失、不呈现"已中断"。
 *
 * 头内布局说明（相对 §3.1 组成表的一处归置）：ArchivePhase 在卡中登记于
 * RunRegistry.hpp 组成行，但其唯一消费字段是本头 TaskRecord::archivePhase
 * （§4.2），而 RunRegistry.hpp（EX-T04）反过来要 include 本头取 TaskId——
 * 为免头循环，ArchivePhase 枚举随其消费字段落在本头，EX-T04 直接复用、
 * 零重定义（枚举值域 §5.6 原文：NotApplicable/Reserved/Archiving/Archived/
 * ArchiveFailed）。偏差随 EX-T02 文档同步在单元卡 §15.4 登记。
 *
 * 线程约束：TaskRecord 以调度线程为唯一写者（§4.2 通用约定——并发只读
 * 经 TaskSnapshot 深拷贝发布）；TaskId::generate()/core::RunId::generate()
 * 线程安全（thread_local 引擎）；EvaluatorRuntimeCapabilities 内部互斥
 * （注册期写入、运行期只读，两相可能分属不同线程——L5 装配线程与调度线程）。
 */

#ifndef SDURWS_IRD_EXECUTION_TASKTYPES_HPP
#define SDURWS_IRD_EXECUTION_TASKTYPES_HPP

#include <array>
#include <chrono>       // std::chrono::milliseconds（运行时限声明——evaluationTimeout）
#include <cstdint>
#include <functional>   // std::hash 特化
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>      // std::pair
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>      // DiagnosticRecord（任务级诊断承载）
#include <sdurws/ird/core/Evaluation.hpp>    // TaskState/TaskOutcome/EvaluationMode（词表归 core）
#include <sdurws/ird/core/Identity.hpp>      // RunId/AttemptId（三级身份的 core 值类型）
#include <sdurws/ird/evidence/Evaluator.hpp> // EvaluationKey（评估键——能力声明注册键）
#include <sdurws/ird/evidence/Snapshot.hpp>  // AnalysisSnapshot（提交绑定的冻结快照）

namespace sdurws::ird::execution {

// =====================================================================
// TaskId——一次逻辑任务的强类型身份（§4.1 第一行）
// =====================================================================

/**
 * @brief 任务身份（规范文本 "tsk-<32 个小写十六进制>"，128 位强类型）。
 *
 * 回答的问题：用户提交的**一次逻辑任务**是哪一个（§4.1 概念表原文）。
 * 分配者＝execution（提交受理时，§4.3 分配协议第一行）。绑定一份冻结
 * 输入（快照＋切片＋模式＋评估器）：改求解配置重跑＝新 TaskId；同一
 * 输入重复提交＝两个独立任务（幂等性不按提交内容承诺——§4.2 taskId 行：
 * "重复提交得到不同 TaskId"）。
 *
 * 纪律（与 core §4.1 六类型同源，core.md D-03）：
 *   - 强类型：独立 struct，不与任何 Id128 类型互转（含 core 六类型）；
 *   - 保留值：全零字节＝"空/未设置"，isValid() 恒 false，generate() 保证非零；
 *   - 往返严格：parse(format(x))==x；tag 逐字符匹配、长度恰 36、字符集
 *     仅 [0-9a-f]（大写拒绝）；
 *   - 不进 core 事件五元组：事件与通道消息携带 core::TaskIdentity
 *     （五元组）；TaskRecord 内部以 TaskId 为主键（§4.1 混用防线 1）。
 *
 * 为什么不消费 core 的 detail 解析/格式化函数：core Identity.hpp 的
 * detail 命名空间明确登记为"非公共契约（R-2 纪律：私有细节不入跨单元
 * 承诺）"——跨单元消费它会把 execution 绑定到 core 的非承诺实现面。
 * 本类型按"core 同纪律新增"原文在单元内自持实现（TaskTypes.cpp 逐条
 * 对照纪律注释）。
 *
 * 线程安全：generate() 使用 thread_local 引擎（多线程并发生成安全——
 * EX-SUB-1 的并发唯一性用例载体）；其余纯值操作。
 */
struct TaskId {
    /// 128 位原始字节；全零＝空（保留值纪律）。字节序＝规范文本序。
    std::array<std::uint8_t, 16> bytes{};

    /// 生成非零随机新值（thread_local mt19937_64；零则重取）。线程安全。
    static TaskId generate();

    /// 严格解析 "tsk-<32 小写 hex>"；tag/长度/字符集违约抛 ExecutionError
    /// （InvalidState——调用方传入了非法文本属契约违约；detail 前缀
    /// "execution/taskid-parse:"）。
    static TaskId fromCanonical(std::string_view text);

    /// try 轨：解析失败返回 nullopt 不抛（对端数据核对场景）。
    static std::optional<TaskId> tryFromCanonical(std::string_view text) noexcept;

    /// 规范文本 "tsk-<32 小写 hex>"（与 parse 构成往返）。
    std::string toCanonical() const;

    /// 非全零（保留值恒 false）。
    bool isValid() const noexcept;

    /// 字节精确相等（core 附录 D 第 12 项同纪律：身份无容差）。
    bool operator==(const TaskId& o) const noexcept { return bytes == o.bytes; }
    bool operator!=(const TaskId& o) const noexcept { return !(*this == o); }
    /// 字节字典序（容器键用；无业务排序语义）。
    bool operator<(const TaskId& o) const noexcept { return bytes < o.bytes; }
};

// =====================================================================
// 提交与任务的枚举/小类型（§4.2/§5.5/§5.6/§6.1）
// =====================================================================

/// 任务优先级（§6.1 队列排序行：双优先级＋同级 FIFO；不抢占——已 Running
/// 任务不被更高优先级打断，取消/暂停只能由用户发起）。
enum class TaskPriority { Interactive, Background };

/**
 * @brief 任务终结原因（§4.2 termination 行：仅终态非空）。
 *
 * 四类终态（Canceled/Failed/Completed/Interrupted——与 core::TaskState
 * 终态同名一一对应，§4.2"四类"原文）＋ ForceTerminated（Failed 类的
 * **显式强杀标记**：T13"不伪装为普通失败"——§5.3/§7.4；§9.1 接纳步 7
 * 与 §9.5 abandon(ForceTerminated) 按其单列）。状态轴上强杀终态是
 * core::TaskState::Failed；本枚举携带区分标记——状态/原因两轴由
 * TaskStateMachine 在 T13 同步写入（state=Failed、termination=
 * ForceTerminated），消费方按需取用。
 */
enum class TerminationCause { Canceled, Failed, Completed, Interrupted, ForceTerminated };

/**
 * @brief 归档阶段（§5.6 辅轴——**独立于任务状态**的正交投影）。
 *
 * 回答"结果落盘走到哪一步"：任务 Completed 不代表归档完成
 * （Completed＋ArchiveFailed 是合法且必须呈现的组合——EX-ARC-5 反例）；
 * Preview 任务不产生 envelope、恒 NotApplicable（§4.2 archivePhase 行）。
 * 推进由归档路径（EX-T04 §9.5）执行，本枚举只定义值域。
 */
enum class ArchivePhase { NotApplicable, Reserved, Archiving, Archived, ArchiveFailed };

/**
 * @brief 检查点身份（§4.1 概念表末行：{run, sequence} 复合值）。
 *
 * 分配者＝execution（写出时分配）；目录编址归 project
 * （checkpoints/<run-id>/<seq>/——project §4.1）。与最终结果的边界
 * （§8.1 冻结规则）：检查点是**可续中间状态**，不构成任何结论、不作
 * 正式缓存命中。规范文本 "chk-<run>-<seq>"（诊断/日志承载）。
 *
 * EX-T02 只定义身份值类型（TaskSubmission.resumeFrom 引用它——§4.2
 * submission 行）；检查点写出/恢复编排归 ICheckpointCoordinator（§10.6，
 * EX-T08 落地时 include 本头，不重定义）。
 */
struct CheckpointId {
    core::RunId run;             ///< 所属运行（core 强类型；分配者 execution）
    std::uint64_t sequence = 0;  ///< 运行内检查点序号，≥1 合法；0＝空（保留值）

    /// run isValid 且 sequence ≥1。
    bool isValid() const noexcept { return run.isValid() && sequence >= 1; }
    /// 规范文本 "chk-<run 规范文本>-<十进制 seq>"。
    std::string toCanonical() const;
    bool operator==(const CheckpointId& o) const noexcept
    {
        return run == o.run && sequence == o.sequence;
    }
    bool operator!=(const CheckpointId& o) const noexcept { return !(*this == o); }
    bool operator<(const CheckpointId& o) const noexcept
    {
        return run != o.run ? run < o.run : sequence < o.sequence;
    }
};

/// 检查点粒度声明（§5.5：评估器写检查点的边界单位——暂停在边界达成）。
enum class CheckpointGranularity { None, Batch, Segment, Sample };

/// 强制终止代价声明（§5.5：Cheap/Moderate/Expensive——**仅声明与呈现，
/// 不影响协议**：任何代价档位下取消/强杀走同一状态机路径与超时协议，
/// 差异只在呈现层提示，契约 acceptance 3 钉住"不影响协议"）。
enum class ForceTerminateCost { Cheap, Moderate, Expensive };

/**
 * @brief 任务能力声明（§5.5——TASK-01 的执行侧承载）。
 *
 * 生命周期：评估器**注册期**声明（经 EvaluatorRuntimeCapabilities 注册表
 * 扩展声明，P-EX-7 处置：evidence EvaluatorDescriptor 字段面冻结为七字段、
 * 不含执行期能力——两侧行为约束经各自单元卡登记，execution 不私改对端）；
 * 派发时由调度器查表推导并写入 TaskRecord.capability，此后**不可变、
 * 运行期只读**（§4.2 capability 行）。
 *
 * 默认值＝"最小能力"（§5.5 括注）：不支持暂停、无检查点边界——
 * supportsPause=false 的任务收到暂停请求必须**显式反馈**
 * （EX-CAPABILITY-UNSUPPORTED，不静默——EX-SM-7/卡行禁止项）。
 * forceTerminateCost 缺省取 Moderate（未声明评估器的中性呈现值；代价
 * 仅呈现、不影响协议——取 Cheap/Expensive 都会在呈现层给出无依据承诺，
 * 故取中间档，登记为实现缺省而非上游阈值）。
 */
struct TaskCapability {
    bool supportsPause = false;                                 ///< 暂停支持（R2 承诺，ARCH §4.3）
    CheckpointGranularity checkpointGranularity = CheckpointGranularity::None;  ///< 检查点粒度（暂停确认边界单位）
    ForceTerminateCost forceTerminateCost = ForceTerminateCost::Moderate;       ///< 强杀代价（仅声明与呈现——§5.5）

    /**
     * @brief 评估器声明的单次运行时限（EX-WKR-5 的"能力声明字段"承载位）。
     *
     * 背景（§11 EX-WKR-5 行"评估器超过声明时限（能力声明字段）"）：运行
     * 超时监视需要每个评估器各自的时限声明（批量 IK 与长优化的合理时限
     * 相差数量级，不能全局一刀切），而执行期能力的声明位置按 §5.5/P-EX-7
     * 归 execution 侧注册表扩展声明（evidence EvaluatorDescriptor 七字段
     * 不含执行能力）——本字段即该声明的承载位，随 TaskCapability 一并经
     * EvaluatorRuntimeCapabilities 注册、派发时写入 TaskRecord 后不可变。
     *
     * 语义：nullopt＝未声明→**不启用**运行超时监视（超时判定缺上游依据，
     * 宁可不监视也不误杀长任务——§11 EX-WKR-5 前置列"评估器超过声明时限"
     * 的反义即"无声明无超时"）；声明值＝单次尝试（Running 相）的最长时长，
     * 超过→走卡死强杀路径（§7.1；编排见 Controller——EX-T03），诊断以
     * EX-WORKER-HUNG＋运行超时标记与心跳失联区分（EX-WKR-5"诊断区分
     * timeout"）。单位 ms（steady 时钟量程）；取值须 >0（0 视为未声明——
     * 语义上"零时限"等于立即超时，无业务意义，按未声明处理并归入未声明
     * 同一分支，不另设错误路径）。
     *
     * 登记说明：本字段是 §5.5 三件套（supportsPause/checkpointGranularity/
     * forceTerminateCost）之外的执行期声明扩展——依据即 §11 EX-WKR-5 行
     * 明文"（能力声明字段）"，增量偏差随单元卡 §15.4 变更记录登记
     * （DTB §5.4 单元卡增量修订）。
     */
    std::optional<std::chrono::milliseconds> evaluationTimeout;

    bool operator==(const TaskCapability& o) const noexcept
    {
        return supportsPause == o.supportsPause
            && checkpointGranularity == o.checkpointGranularity
            && forceTerminateCost == o.forceTerminateCost
            && evaluationTimeout == o.evaluationTimeout;
    }
    bool operator!=(const TaskCapability& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 评估器运行期能力声明注册表（§5.5 原文名 EvaluatorRuntimeCapabilities
 *        的注册表扩展声明——P-EX-7 处置的 execution 侧落点）。
 *
 * 背景说明：evidence::EvaluatorDescriptor 恰含其 §9.2 七字段（工程评估
 * 能力：supportedModes/stateless/threadSafety），**不含**任何执行期
 * 能力字段——其文件头明文把执行能力划归"execution 侧注册表扩展声明"
 * （evidence.md §13 交接项）。本类即该扩展：以 evidence::EvaluationKey
 * 为注册键（与 evidence EvaluatorRegistry 同键空间；键词形闸门
 * isValidEvaluationKey 归 evidence/装配清单侧，本表不重复校验），声明
 * 暂停/检查点粒度/强杀代价三件执行期能力。
 *
 * 使用约定（§5.5）：派发时按 submission.evaluatorKey 查表；未声明的
 * 评估器按"最小能力"处理（lookupOrMinimal 返回 TaskCapability 缺省值
 * ——不支持暂停）。查得的能力在派发时写入 TaskRecord.capability 并从
 * 此不可变（§4.2）。
 *
 * 线程安全：内部 std::mutex——声明（L5 装配线程）与查询（调度线程）
 * 可并发；粒度为整表互斥（注册表条目少、查询频度低，无性能诉求）。
 */
class EvaluatorRuntimeCapabilities {
public:
    EvaluatorRuntimeCapabilities() = default;

    /**
     * @brief 注册期声明一份能力（同键重复声明拒绝——覆盖禁止）。
     *
     * @param key        [in] 评估键（evidence::EvaluationKey 词形）
     * @param capability [in] 执行期能力三件套
     * @return true＝声明成功；false＝该键已有声明（装配清单漂移的
     *         早期暴露——同一评估键两份能力声明属装配错误，拒绝而非
     *         静默覆盖，与 evidence 注册表"重复键拒绝"同精神）
     */
    bool declare(const evidence::EvaluationKey& key, TaskCapability capability);

    /// 精确查询（未声明返回 nullopt——调用方决定回退策略）。
    std::optional<TaskCapability> tryLookup(const evidence::EvaluationKey& key) const;

    /// 查询＋最小能力回退（§5.5"无能力字段时按最小能力处理"——未声明
    /// 评估器恒可得一份能力：不支持暂停/无检查点边界）。派发推导用此轨。
    TaskCapability lookupOrMinimal(const evidence::EvaluationKey& key) const;

private:
    mutable std::mutex m_mutex;  ///< 保护下表的互斥（声明/查询可能跨线程）
    /// 键→能力（注册期写入、运行期只读——互斥保护跨相访问；vector 线性
    /// 查找——评估器数量为个位数量级，无性能诉求）。
    std::vector<std::pair<evidence::EvaluationKey, TaskCapability>> m_entries;
};

/**
 * @brief 进度报告（§6.1：ProgressReport{percent, phaseToken, batchesDone/Total}）。
 *
 * 线程约束：作为 TaskRecord.progress 流式更新（调度线程写；对外发布
 * 节流 ≤10 Hz、实现参数 D-07——§6.1）；进度**不入**领域事件（core.md
 * D-09——进度走 execution 自有通道与查询投影）。
 */
struct ProgressReport {
    int percent = 0;                ///< 完成百分比，取值 [0,100]（无量纲百分比，非物理量）
    std::string phaseToken;         ///< 进度阶段 token（非空——UX-10 进度阶段的机器判读面）
    std::uint64_t batchesDone = 0;  ///< 已完成批次数（≤ batchesTotal；计数，无量纲）
    std::uint64_t batchesTotal = 0; ///< 批次总数（0＝批次总数未声明——允许，此时 done 应为 0）

    /**
     * @brief 工厂（§4.2 progress 行合法性约束的前置强制）。
     *
     * @throws ExecutionError(InvalidState) percent 越界（<0 或 >100）、
     *         phaseToken 空串、batchesDone > batchesTotal——三者均为
     *         调用方契约违约（worker 上报通道组装错误），fail-fast。
     */
    static ProgressReport make(int percent, std::string phaseToken,
                               std::uint64_t batchesDone, std::uint64_t batchesTotal);

    bool operator==(const ProgressReport& o) const noexcept
    {
        return percent == o.percent && phaseToken == o.phaseToken
            && batchesDone == o.batchesDone && batchesTotal == o.batchesTotal;
    }
    bool operator!=(const ProgressReport& o) const noexcept { return !(*this == o); }
};

// =====================================================================
// 提交与记录（§4.2 字段表——字段序/默认值/可变性逐行对照）
// =====================================================================

/**
 * @brief 任务提交请求（§4.2 submission 行：请求内容＝快照/评估器/模式/
 *        优先级/resumeFrom）。
 *
 * 值语义；一经受理即不可变（TaskRecord.submission 不可变——§4.2）。
 * 快照冻结性（evidence builder 产物、snapshotId 非零）与修订闭包/
 * 评估器注册/写权限/预算四道校验由提交验证清单 §6.3 V1~V4 把关——那是
 * 调度器（EX-T05）的职责；本结构只承载，不做内容校验（EX-T02 状态机
 * 不消费快照内容）。
 *
 * resumeFrom：从既有检查点续跑的请求（重跑＝新 TaskId/新 RunId 但可
 * 携带旧运行检查点——§4.3 重启/恢复路径、§8.1 恢复流程）。
 */
struct TaskSubmission {
    evidence::AnalysisSnapshot snapshot;   ///< 评估输入的完整不可变快照（冻结产物）
    evidence::EvaluationKey evaluatorKey;  ///< 评估键（evidence 词形；派发前 find 命中——§6.3 V1）
    std::uint32_t contractVersion = 0;     ///< 评估器契约版本（须与快照复现块一致——§6.3 V1）
    core::EvaluationMode mode = core::EvaluationMode::Quick;  ///< 评估模式（§8.1 表 1 透传属性）
    TaskPriority priority = TaskPriority::Background;         ///< 队列优先级（缺省 Background——Interactive 由调用方显式选择）
    std::optional<CheckpointId> resumeFrom;                   ///< 续跑检查点（nullopt＝全新运行）

    bool operator==(const TaskSubmission& o) const
    {
        return snapshot == o.snapshot && evaluatorKey == o.evaluatorKey
            && contractVersion == o.contractVersion && mode == o.mode
            && priority == o.priority && resumeFrom == o.resumeFrom;
    }
    bool operator!=(const TaskSubmission& o) const { return !(*this == o); }
};

/**
 * @brief 任务主记录（§4.2 TaskRecord 字段表——字段/默认/可变性逐行对照）。
 *
 * 生命周期：提交受理（TaskId 分配）→ 终态后保留窗口（默认 30 min，
 * 实现参数 D-07）→ releaseResources 回收。**会话内存态**（§5.4/D-13：
 * 不持久化——磁盘痕迹唯一经 project 归档预留与检查点；Queued 任务崩溃
 * 即消失，P-EX-6）。
 *
 * 线程约束（§4.2 通用约定）：**调度线程为唯一写者**；并发只读安全仅经
 * TaskSnapshot 深拷贝发布（本结构的可变引用不得跨线程共享）。
 * 可变性约束由 TaskStateMachine 强制：state 仅状态机转移修改；
 * termination 终态时一次写入；taskId/submission/capability 受理后不可变。
 */
struct TaskRecord {
    TaskId taskId;                                     ///< 主键（非零——generate 保证；重复提交＝不同 TaskId）
    TaskSubmission submission;                         ///< 请求内容（不可变）
    TaskCapability capability;                         ///< 能力声明（注册期声明、派发时推导、此后只读——§5.5）
    core::TaskState state = core::TaskState::Queued;   ///< 当前状态机状态（仅状态机转移修改；九态词表归 core）
    std::optional<core::RunId> run;                    ///< 当前关联运行（Queued 期可空——P-EX-6：无磁盘身份）
    core::AttemptId attempt{0};                        ///< 当前尝试（run 非空时 ≥1；0＝未派发〔保留值〕）
    std::optional<ProgressReport> progress;            ///< 最近进度（流式更新；percent≤100、phaseToken 非空）
    std::optional<TerminationCause> termination;       ///< 终结原因（仅终态非空；一次写入）
    ArchivePhase archivePhase = ArchivePhase::NotApplicable;  ///< 归档阶段（独立于任务状态——§5.6 辅轴；Preview 恒 NotApplicable）
    std::vector<core::DiagnosticRecord> diagnostics;   ///< 任务级诊断累积（追加；稳定码合法——ERR-01）

    bool operator==(const TaskRecord& o) const;
    bool operator!=(const TaskRecord& o) const { return !(*this == o); }
};

/**
 * @brief 任务只读投影（§4.2"TaskRecord 投影"/§10.1 tryTask 返回值形态）。
 *
 * 用途：查询接口向任意线程发布的**深拷贝快照**（§4.2 通用约定"查询经
 * 快照拷贝"；§10.8"查询永不被状态写阻塞（不可变快照）"）。只含呈现/
 * 决策所需投影字段，不含 diagnostics 全量与 submission 载荷（防 DTO
 * 膨胀——消费者需要时经任务级接口另行获取）。值语义；构造后不可变。
 */
struct TaskSnapshot {
    TaskId taskId;                                   ///< 主键投影
    core::TaskState state = core::TaskState::Queued; ///< 状态投影（九态）
    std::optional<core::RunId> run;                  ///< 运行绑定投影（Queued 期为空）
    core::AttemptId attempt{0};                      ///< 尝试投影
    std::optional<ProgressReport> progress;          ///< 最近进度投影
    std::optional<TerminationCause> termination;     ///< 终结原因投影（终态非空）
    ArchivePhase archivePhase = ArchivePhase::NotApplicable;  ///< 归档阶段投影（Completed＋ArchiveFailed 的正交观测面——EX-ARC-5）

    bool operator==(const TaskSnapshot& o) const noexcept;
    bool operator!=(const TaskSnapshot& o) const noexcept { return !(*this == o); }
};

// =====================================================================
// 终态映射与判定辅助（§5.6——core.md §10.3 交接项的承接）
// =====================================================================

/**
 * @brief 判断任务状态是否终态（§5.1：Canceled/Completed/Failed/Interrupted
 *        四终态；Queued/Preparing/Running/Paused/Canceling 非终态）。
 *
 * @param state [in] core 九态之一
 * @return true＝终态（无出边——§5.2 矩阵"终态无出边"；重跑＝新 TaskId）
 */
bool isTerminalTaskState(core::TaskState state) noexcept;

/**
 * @brief TaskState→TaskOutcome 终态映射（§5.6 冻结表——core.md §10.3 向
 *        execution 交接项的承接落点）。
 *
 * 映射规则（§5.6 原文）：Completed→Completed；Canceled→Canceled；
 * Failed→Failed；Interrupted→Interrupted；非终态无 outcome。
 * 注意两轴正交：TaskState（状态机轴）与 TaskOutcome（信封结果轴）终态
 * 同名但属两个轴——本函数是执行侧的唯一映射点，四轴互不推导（任务
 * Completed 不意味工程通过、不意味归档完成——§5.6 表）。
 *
 * @param state [in] core 九态之一
 * @return 终态→对应 TaskOutcome；非终态→nullopt（"非终态无 outcome"
 *         的显式表达——不伪造结果，TASK-02/NFR-REL-03 同源纪律）
 */
std::optional<core::TaskOutcome> terminalOutcome(core::TaskState state) noexcept;

}  // namespace sdurws::ird::execution

// ---- std::hash 特化（TaskId 进哈希容器——RunRegistry 主键索引预用面） ----
namespace std {

template <> struct hash<sdurws::ird::execution::TaskId> {
    /// FNV-1a 64 字节直扫（execution 自持实现——不消费 core detail 的
    /// fnv1a128，理由同 TaskId 注：detail 非跨单元承诺面）。哈希质量仅
    /// 须满足容器散列（同字节同哈希、分布均匀），非密码学承诺，与
    /// core §4.1 的 std::hash 定位一致。
    std::size_t operator()(const sdurws::ird::execution::TaskId& id) const noexcept
    {
        // FNV-1a 64 位偏移基 0xcbf29ce484222325 与素数 0x100000001b3 的
        // 十进制字面量（64 位无符号回绕乘法即算法定义）。
        std::uint64_t h = 14695981039346656037ULL;
        for (const std::uint8_t b : id.bytes) {
            h ^= static_cast<std::uint64_t>(b);
            h *= 1099511628211ULL;
        }
        return static_cast<std::size_t>(h);
    }
};

}  // namespace std

#endif  // SDURWS_IRD_EXECUTION_TASKTYPES_HPP
