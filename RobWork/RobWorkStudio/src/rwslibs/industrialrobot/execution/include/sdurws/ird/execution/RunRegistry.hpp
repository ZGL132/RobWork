/**
 * @file   RunRegistry.hpp
 * @brief  运行登记表与迟到结果两段式接纳（RunRegistry/Admission）——
 *         完整五元组核对、九步接纳程序、名称反解编排、envelope 组装
 *         与归档触发（§9＋ARCH §4.5 的公共契约面）。
 *
 * 设计依据：
 *   - units/execution.md §9.1（登记记录全字段 RunRegistration）、§9.2
 *     （九步接纳程序——A8 两段式、一次性判定）、§9.3（迟到结果处置规则
 *     ——重投递幂等/内容冲突/有界重试/abandon）、§9.4（项目切换后迟到
 *     结果归档流程 S7 execution 侧）、§9.5（与 project 的归档协作——
 *     P-PR-4 冻结：调度线程调用＋writer 互斥串行化＋完成事件幂等无上限
 *     ＋abandon 三时机）、§10.5（IRunRegistry/IResultAdmission 接口原文）
 *   - ARCHITECTURE.md §4.5（A1 处置/A8 修订：第一段登记表核对〔任一字段
 *     不符即拒绝〕→第二段按登记记录接纳并归档〔归档位置取自登记记录、
 *     不重新推导；HEAD 不参与接纳判定〕→当前性独立判定归 evidence）
 *   - 需求 TASK-03（五元组身份/会话隔离）、PM-13（切换场景——AT-10）、
 *     CON-02（三态正交——取消/失败不入正式）、CON-05（内容寻址——
 *     当前性按内容身份另判）、CON-06（先反解后接纳）、TASK-02（执行侧
 *     落点：已终结登记拒绝 FinalEnvelope——§9.2 步 7）
 *   - 任务契约 tasks/foundation/EX-T04.json acceptance 1～5（EX-REG-1~8、
 *     EX-ORT-1；P-EX-5/P-PR-4/P-EX-3/P-EX-1 处置）
 *
 * 背景说明（为什么接纳必须是"一次性判定"，ARCH S7/A7 的教训）：
 *   迟到的完成事件跨项目到达时，若先按"当前会话"判失配丢弃、再按原
 *   项目接纳，就存在两段歧义（先丢弃后接纳的状态窗口）。九步程序对
 *   每个到达**只查登记表**（不查"当前项目"、不查 HEAD）——登记表里
 *   有完整五元组就核对，没有就丢弃：合法迟到结果因此永不因项目切换
 *   被误丢（卡行禁止项），未知/伪造到达也永不写入任何项目的 results/
 *   （AT-10 反例）。归档位置一律取登记记录的 runDir 原值（A8：即使
 *   运行期间 HEAD 已前进、项目已切换，结果仍写入原修订的目录）。
 *
 * 归置登记（相对 §10.5/§9.1 原文的必要增量，全部登记单元卡 §15.4）：
 *   ①RejectReason 在 §10.5 六值词表基础上**表尾追加**四值（NameUnresolved/
 *     PayloadUndecodable/ConstructionFailed/ContentConflict）——九步步 8/9
 *     与重投递冲突需要自己的失败面，追加不改既有值的序（枚举演进纪律：
 *     只能表尾追加并留痕）；
 *   ②RunRegistration 增 mode 与 archivePhase 两个镜像字段——步 9 组装
 *     envelope 需要 mode、§10.5 archivePhase(RunId) 查询需要阶段镜像
 *     （§9.1 字段表未列，属实现承接的补全）；
 *   ③新增 IRunArchiveGateway/RunEvaluationMaterials/IEvaluationOutputDecoder
 *     三个协作承载类型——§9.1 resourceContext 与 §10.5 接口原文点名的
 *     "登记记录持有归档会话引用/存储上下文句柄"需要具体形状（§3.3 注入
 *     模式；IEvaluationOutputDecoder 见 Ports.hpp 文件头登记）。
 *
 * 线程约束（P-PR-4 单侧冻结，§9.5 原文）：本头全部可变操作（registerRun/
 *   appendAttempt/markTerminated/admitResult/归档端口调用）**仅调度线程**
 *   ——execution 不并发调用归档端口、不另持项目写锁（project 内部经
 *   writer 互斥与命令服务/草稿落盘串行化——project §9.8）；classifyArrival/
 *   tryRun/archivePhase 是纯查表只读面，供同线程编排与其他线程的快照
 *   投影使用（返回值/拷贝语义保证无共享可变状态外泄）。
 */

#ifndef SDURWS_IRD_EXECUTION_RUNREGISTRY_HPP
#define SDURWS_IRD_EXECUTION_RUNREGISTRY_HPP

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>       // DiagnosticRecord（拒绝诊断承载）
#include <sdurws/ird/core/Evaluation.hpp>     // TaskState/EvaluationMode（词表归 core）
#include <sdurws/ird/core/Events.hpp>         // IDomainEventBus（ResultArchived 发布面）
#include <sdurws/ird/core/Identity.hpp>       // TaskIdentity 五元组/RunId/AttemptId/ContentIdentity
#include <sdurws/ird/evidence/Evaluator.hpp>  // EvaluationKey/EvaluationOutput/IProducerRegistryView
#include <sdurws/ird/evidence/Envelope.hpp>   // ResultEnvelope（接纳唯一权威产出——SA-13）
#include <sdurws/ird/evidence/Verdict.hpp>    // aggregateVerdict 输入/输出类型
#include <sdurws/ird/execution/Errors.hpp>
#include <sdurws/ird/execution/Ports.hpp>     // INameResolverAdapter/IEvaluationOutputDecoder/IExecutionDiagnosticsSink
#include <sdurws/ird/execution/TaskTypes.hpp> // TaskId/TerminationCause/ArchivePhase（§4/§5.6）
#include <sdurws/ird/project/ArchivePort.hpp> // IResultArchivePort/ArchiveSessionRef（登记边——ARCH §3.5）

namespace sdurws::ird::execution {

// =====================================================================
// ResultKind——到达结果的类型词表（§9.1 allowedResultKinds 行值域）
// =====================================================================

/**
 * @brief 到达结果的类型（§9.1 allowedResultKinds 行原文五值——按 mode
 *        与阶段限定，登记时声明该运行允许哪些；步 6 核对面）。
 *
 * 值域冻结（§9.1 原文枚举集，不增不减）；Preview 运行不登记本表
 * （表 1/EVI-01：Preview 不产生结果对象——提交验证 V1~V4 拦截，EX-T05）。
 */
enum class ResultKind {
    FinalEnvelope,          ///< 最终结果信封（九步 8~9 全流程的到达类型）
    ResultBatch,            ///< 结果批次（分批流式回传的批次文件——NFR-PERF-03）
    CheckpointBatch,        ///< 检查点批次（§8.1；持久化编排归 EX-T08）
    Progress,               ///< 进度帧（§6.1——进度走自有通道，不入领域事件）
    PartialDataOnTerminate, ///< 终结时部分数据（诊断性——envelope PartialDataRef 的数据面）
};

// =====================================================================
// 派发期接纳事实面与归档协作网关（§9.1 resourceContext 的具体形状）
// =====================================================================

/**
 * @brief 一次运行的接纳事实面（派发期装配、随登记记录持有到接纳完成）。
 *
 * 背景说明（为什么这些事实在登记期固化）：九步步 9 的 envelope 组装需要
 *   三类"运行自己的事实"——①被评估的冻结快照（aggregateVerdict/
 *   validateCombination 的事实面）；②产生者/Profile 两张注册表的只读投影
 *   （validateProof 与 Profile 绑定核对的查询面）；③请求侧判定素材
 *   （readiness/snapshotGate/coverage——提交验证与派发期装配的产出，
 *   worker 只回传产出侧素材，两侧在接纳处合流成完整 VerdictInput）。
 *   这些事实在派发时点冻结后随登记记录持有（§9.1 resourceContext 的
 *   "worker 快照引用"义务：快照引用持到归档完成——§7.5/§13 交接清单），
 *   保证迟到接纳（可能晚于提交后任意时长）看到的与运行时看到的同一。
 *
 * 生命周期：snapshot/producers/profiles 由调用方（派发编排）以 shared_ptr
 *   持有并存入登记记录——引用计数保证迟到接纳期间存活；本结构不做
 *   任何深拷贝（快照可能很大——只读共享）。
 *
 * 线程约束：登记后运行期只读（调度线程独占写域）；各 shared_ptr 目标的
 *   内部并发安全由其实现方承诺（注册表投影按只读查询设计——evidence 同款）。
 */
struct RunEvaluationMaterials {
    /// 被评估的冻结快照（evidence builder 产物；非空——接纳组装事实面）。
    std::shared_ptr<const evidence::AnalysisSnapshot> snapshot;
    /// 产生者注册表只读投影（validateProof 的注册查询面——非空）。
    std::shared_ptr<const evidence::IProducerRegistryView> producers;
    /// 证据 Profile 注册表只读投影（aggregateVerdict 的 Profile 查询面——非空）。
    std::shared_ptr<const evidence::IProfileRegistryView> profiles;
    /// 产出位置声明（进入 envelope.producer.producedIn——worker 派发＝
    /// Worker、调度线程内联评估＝MainProcess；派发方式属派发期事实）。
    evidence::ProducerProcess producedIn = evidence::ProducerProcess::Worker;
    /// 请求侧判定素材：输入就绪摘要（REQ-06——提交验证 V1 的产出）。
    evidence::ReadinessSummary readiness;
    /// 请求侧判定素材：快照身份门禁结果（§6.4.1②——派发期装配）。
    evidence::SnapshotGateResult snapshotGate;
    /// 请求侧判定素材：必验工况覆盖矩阵（EVI-02——跨批次汇总的事实分母）。
    evidence::CaseCoverageMatrix coverage;
    /// 请求侧判定素材：区域采样证据（§6.4.3——可选承载，默认空）。
    std::vector<evidence::RegionCoverageEvidence> regionCoverages;
    /// 证据清单绑定三元组（派发期自评估器注册清单查得——§9.4 manifest
    /// 行 {key, contractVersion, profileIdentity} 的登记侧副本；接纳时
    /// 与到达的证据项集组装成完整 EvidenceManifest——EvaluationOutput
    /// 只携带项集，绑定面属登记事实）。
    evidence::EvidenceProfileRef manifestProfile;
};

/**
 * @brief 归档协作网关——execution 对本运行存储上下文写面的最小消费口
 *        （§9.5 协作要素表的执行侧承载）。
 *
 * 背景说明（为什么经网关而不直接持 IResultArchivePort）：
 *   §9.3 迟到写防御路径（ARCH §6.8 A7 原文）要求"归档 begin 遇
 *   context-closed→先尝试重新取得写权限"——"重新 open writable"是存储
 *   上下文生命周期操作（ProjectStore 工厂/关闭协议归 workflow/L5 编排，
 *   PA-1：execution 不私建第二套开关协议）。网关把"当前写面端口"与
 *   "重取写权限"两个动作封成一个每运行一份的协作点：L5 装配期实现
 *   （转发到该运行的 ProjectStore），测试以脚本化替身注入故障。
 *
 * P-PR-4 冻结义务（acceptance 4）：本网关的全部方法**仅调度线程调用**
 *   （admitResult 的归档段）；project 侧内部经 writer 互斥串行化（§9.8），
 *   execution 不并发调用、不另持项目锁、不以 PID/心跳/UI 状态为权限依据。
 *
 * 线程约束：仅调度线程。
 */
class IRunArchiveGateway {
public:
    virtual ~IRunArchiveGateway() = default;

    /**
     * @brief 本运行存储上下文的归档写面（ProjectStore::archive() 的转发
     *        形态）。返回引用须在网关存活期有效（存储上下文由对端持有）。
     */
    virtual project::IResultArchivePort& port() const = 0;

    /**
     * @brief A7 防御：重新取得写权限（§9.3"重新 open writable"的执行侧
     *        触发点——具体重开动作归 L5 对工作流关闭协议的编排）。
     *
     * @return true＝写权限已重新取得（port() 此后返回新写面）；false＝
     *         重取失败（锁被他持/介质只读）——接纳侧据以拒绝归档并出
     *         EX-ARCHIVE-AUTHORITY-LOST 诊断（结果不落盘，用户可重跑）
     */
    virtual bool reacquireWriteAuthority() = 0;
};

/**
 * @brief 登记记录的资源上下文（§9.1 resourceContext 行三件套的承载）。
 *
 * 对应关系（§9.1 原文 {worker 快照引用, 归档会话引用, 存储上下文句柄}）：
 *   - worker 快照引用 → evaluation（快照＋两投影＋请求侧素材——接纳
 *     事实面）与 workerAttachment（EX-T06 worker 进程接管的附件保留位，
 *     类型擦除——EX-T04 阶段恒空）；
 *   - 归档会话引用 → archiveSession（Preparing 段 archive.begin 空会话
 *     预留——§9.5"派发期预留"行；EX-T05 调度器派发时建立，EX-T04 直纳
 *     路径在归档段补建）；
 *   - 存储上下文句柄 → archive（网关——写面端口＋A7 重取）。
 *
 * 持有纪律（A7/§7.5）：本结构里的引用使存储上下文保持存活至接纳归档
 *   完成（finalize 或 abandon）——引用清零由 execution 的终结路径驱动
 *   （§9.3"UI 关闭与存储上下文释放分离"行）。
 *
 * 线程约束：仅调度线程访问（登记记录的写者域）。
 */
struct RunResourceContext {
    /// 接纳事实面（非空——正式运行必有；Preview 不登记）。
    std::shared_ptr<const RunEvaluationMaterials> evaluation;
    /// 归档协作网关（非空——正式运行必有归档归属）。
    std::shared_ptr<IRunArchiveGateway> archive;
    /// 归档预留会话（Preparing begin 产物；空＝预留未建立——归档段补建）。
    project::ArchiveSessionRef archiveSession;
    /// worker 快照引用保留位（类型擦除；EX-T06 worker 监督接管——当前恒空）。
    std::shared_ptr<void> workerAttachment;
};

// =====================================================================
// RunRegistration——登记记录全字段（§9.1 逐行）
// =====================================================================

/**
 * @brief 运行登记记录（§9.1 全字段——接纳判定的唯一核对基准）。
 *
 * 生命周期：registerRun 时创建（登记表节点——引用/指针稳定，unordered_map
 *   节点不搬移）；随注册表析构。**接纳后归档位置一律取本记录的 runDir
 *   原值，不重新推导**（ARCH §4.5 A8 原文——项目切换/HEAD 前进都不改变
 *   归属）。可变字段（currentState/terminationCause/supersededAttempts/
 *   archivePhase）仅调度线程经注册表方法推进。
 *
 * 线程约束：非线程安全的可变记录——tryRun() 以整记录拷贝对外发布只读
 *   快照；内部可变引用不出注册表（§4.2 TaskRecord 同款纪律）。
 */
struct RunRegistration {
    /// 完整五元组（当前 attempt）——接纳判定的唯一核对基准（§9.1 首行）。
    core::TaskIdentity identity{};
    /// 关联任务（§9.1 task 行；TaskId 归 execution 数据模型——§4.1）。
    TaskId task{};
    /// 评估键（透传 RunManifest——project §4.4.7；词形归 evidence）。
    evidence::EvaluationKey evaluatorKey;
    /// 评估器契约版本（透传 RunManifest；CON-04 绑定面）。
    std::uint32_t contractVersion = 0;
    /// 输入绑定三身份（§9.1 扩展核对字段——步 5 第二道绑定的登记侧）。
    core::ContentIdentity snapshotId;
    core::ContentIdentity sliceId;
    core::ContentIdentity inputBaselineId;
    /// CON-06 绑定（§9.1：策略与名称映射内容身份——名称反解依据）。
    core::ContentIdentity policyIdentity;
    core::ContentIdentity nameMapIdentity;
    /// 评估模式（归置增量：步 9 envelope 组装需要——登记单元卡 §15.4）。
    core::EvaluationMode mode = core::EvaluationMode::Verified;
    /// 归档目标：任务快照绑定修订的 results/<run-id>/（§9.1 原文——
    /// **不重新推导**；项目切换后合法迟到结果归档至本值）。
    std::filesystem::path runDir;
    /// 任务类型 token（透传 RunManifest；非空可打印 ASCII——project §4.4.7）。
    std::string runKind;
    /// 允许的结果类型集（步 6 核对面；§9.1 五值子集，登记时声明）。
    std::vector<ResultKind> allowedResultKinds;
    /// 登记时刻（UTC 墙钟——注册表时钟；观测面，不参与判定）。
    std::chrono::system_clock::time_point registeredAtUtc{};
    /// 登记时刻任务状态镜像（随任务状态机同步——调度线程推进）。
    core::TaskState currentState = core::TaskState::Preparing;
    /// 归档阶段镜像（归置增量：§10.5 archivePhase(RunId) 查询的承载——
    /// §5.6 辅轴独立于任务状态；登记单元卡 §15.4）。
    ArchivePhase archivePhase = ArchivePhase::NotApplicable;
    /// 终结原因（仅终态非空；步 7 接纳语义裁量的输入——已 Canceled/
    /// Failed/ForceTerminated 的登记拒绝 FinalEnvelope）。
    std::optional<TerminationCause> terminationCause;
    /// 被取代尝试（§9.1：携带其中之一→StaleAttempt 拒绝——步 4）。
    std::vector<core::AttemptId> supersededAttempts;
    /// 资源上下文（§9.1 resourceContext——引用持有义务见结构注释）。
    RunResourceContext resourceContext;
    /// 登记表 schema 版本（§9.1：当前 1——kRegistryVersion）。
    std::uint32_t registryVersion = 1;
};

/**
 * @brief 登记输入（§10.5 registerRun(TaskId, const RegistrationInput&) 的
 *        入参面——派发编排把一次派发的全部登记事实打包于此）。
 *
 * 校验在 registerRun 内执行（调用方契约违约 fail-fast——ExecutionError）；
 * 本结构只承载不做校验（TaskSubmission 同款分工）。
 *
 * 值语义；线程约束：仅调度线程构造与传递。
 */
struct RegistrationInput {
    /// 完整五元组（run/attempt 已由派发分配——§4.3 分配协议）。
    core::TaskIdentity identity{};
    /// 评估键与契约版本（透传 RunManifest）。
    evidence::EvaluationKey evaluatorKey;
    std::uint32_t contractVersion = 0;
    /// 输入绑定三身份（快照/切片/输入基准——派发绑定值）。
    core::ContentIdentity snapshotId;
    core::ContentIdentity sliceId;
    core::ContentIdentity inputBaselineId;
    /// CON-06 绑定（策略/名称映射内容身份——快照 refs 冻结值）。
    core::ContentIdentity policyIdentity;
    core::ContentIdentity nameMapIdentity;
    /// 评估模式（步 9 组装消费；Preview 不登记——提交验证拦截）。
    core::EvaluationMode mode = core::EvaluationMode::Verified;
    /// 归档目标（results/<run-id>/——登记原值，归档不重新推导）。
    std::filesystem::path runDir;
    /// 任务类型 token（透传 RunManifest）。
    std::string runKind;
    /// 允许的结果类型集（步 6 核对面）。
    std::vector<ResultKind> allowedResultKinds;
    /// 登记时刻任务状态（派发登记点＝Preparing——§4.3 分配协议）。
    core::TaskState currentState = core::TaskState::Preparing;
    /// 资源上下文（接纳事实面＋归档网关——见 RunResourceContext 注释）。
    RunResourceContext resources;
};

// =====================================================================
// ArrivalEnvelope / AdmissionDecision / AdmissionOutcome（§10.5 承载）
// =====================================================================

/**
 * @brief 到达的结果信封（§10.5 原文形态——通道 FinalOutput 或重投递的
 *        到达面；身份自报，以登记表核对为准）。
 *
 * 五元组与绑定字段全部**自报**——九步 1~5 逐一与登记记录比对，自报值
 *   不被信任（防通道串扰/伪造到达）。payloadCanon 按 §10.5 携带
 *   EvaluationOutput canonical 字节（FinalEnvelope 时；批次到达时为批次
 *   字节）——字节级契约归通道协议（EX-T06），execution 经
 *   IEvaluationOutputDecoder 注入解码（Ports.hpp 归置登记）。
 *
 * 值语义（可移动——admitResult 按 rvalue 消耗）；线程约束：仅调度线程。
 */
struct ArrivalEnvelope {
    /// 五元组（自报——步 1 查表键＝run，步 2 全字段核对）。
    core::TaskIdentity identity;
    /// 绑定自报（第二道核对——步 5）。
    core::ContentIdentity snapshotId;
    core::ContentIdentity sliceId;
    /// 评估键与契约版本自报（步 5）。
    evidence::EvaluationKey evaluatorKey;
    std::uint32_t contractVersion = 0;
    /// CON-06 绑定自报（步 5；名称反解永远用**登记值**，本字段仅核对）。
    core::ContentIdentity policyIdentity;
    core::ContentIdentity nameMapIdentity;
    /// 结果类型（步 6 ∈ allowedResultKinds 核对）。
    ResultKind kind = ResultKind::FinalEnvelope;
    /// canonical 载荷（FinalEnvelope＝EvaluationOutput canonical；批次＝
    /// 批次字节；§10.5 原文注）。
    std::vector<std::uint8_t> payloadCanon;
};

/**
 * @brief 接纳判定（§10.5 原文形态——一次性判定的结论；纯值无副作用）。
 *
 * RejectReason 词表演进登记（单元卡 §15.4）：前六值＝§10.5 原文（值序
 *   冻结）；末四值为 EX-T04 表尾追加——NameUnresolved（步 8 名称反解
 *   失败——CON-06"先反解后接纳"）、PayloadUndecodable（步 9 前置的
 *   载荷解码失败——通道数据错误）、ConstructionFailed（步 9 envelope
 *   组装校验未过——make/validateCombination 拒绝，任务转 Failed——T9）、
 *   ContentConflict（重投递内容冲突——§9.3/D-14，不覆盖既有归档）。
 */
struct AdmissionDecision {
    /// 判定结论（§10.5 原文三值：接纳/幂等忽略/拒绝）。
    enum class Verdict { Admitted, DuplicateIgnored, Rejected };

    /// 拒绝原因（Rejected 时有效；词表见结构注释的演进登记）。
    enum class RejectReason {
        UnknownRun,         ///< 步 1：runId 无登记（含跨项目迟到事件——AT-10）
        IdentityMismatch,   ///< 步 2：五元组任一字段与登记不符
        StaleAttempt,       ///< 步 4：attempt ∈ supersededAttempts
        BindingMismatch,    ///< 步 5：第二道绑定核对不符（防通道串扰）
        KindNotAllowed,     ///< 步 6：结果类型 ∉ allowedResultKinds
        AlreadyTerminated,  ///< 步 7：已终结登记收到 FinalEnvelope（TASK-02）
        NameUnresolved,     ///< 步 8：运行时名称反解失败（CON-06——EX-T04 追加）
        PayloadUndecodable, ///< 步 9 前置：载荷解码失败（通道数据错误——追加）
        ConstructionFailed, ///< 步 9：envelope 组装校验拒绝（T9——追加）
        ContentConflict,    ///< 重投递内容与已归档不一致（D-14——追加）
    };

    Verdict verdict = Verdict::Rejected;               ///< 判定结论
    RejectReason reason = RejectReason::UnknownRun;    ///< 拒绝原因（Rejected 时有效）
    /// 拒绝/幂等说明诊断（拒绝→开发级稳定码诊断——§9.3"迟到结果拒绝
    /// 是否留诊断"行；Admitted 时为空）。
    std::vector<core::DiagnosticRecord> diagnostics;
};

/**
 * @brief 接纳编排结果（admitResult 的完整产出面——判定＋权威产物＋两轴
 *        投影；实现形状，§10.5 后置注的承载，登记单元卡 §15.4）。
 *
 * 两轴投影的正交观测（§5.6——EX-ORT-1 的 API 面）：
 *   - taskStateAfter：接纳编排后的任务状态投影（T8 Completed／T9 Failed
 *     ——ConstructionFailed；其余拒绝不改变任务状态，投影＝登记镜像）；
 *   - archivePhaseAfter：归档阶段投影（Completed＋ArchiveFailed 是合法
 *     组合——EX-ARC-5"任务完成≠归档完成"）。
 *
 * 值语义；线程约束：仅调度线程。
 */
struct AdmissionOutcome {
    /// 九步判定结论（步 1~7＋步 8/9 的失败面——词表见 AdmissionDecision）。
    AdmissionDecision decision;
    /// 正式结果对象（Admitted 时非空——ResultEnvelope::make 的唯一权威
    /// 产出，SA-13；DuplicateIgnored/Rejected 恒空：幂等不重产出、拒绝
    /// 不构造——TASK-02"取消/失败不入正式"的 API 面）。
    std::shared_ptr<const evidence::ResultEnvelope> envelope;
    /// 任务状态投影（见结构注释——T8/T9/不变三态）。
    core::TaskState taskStateAfter = core::TaskState::Running;
    /// 归档阶段投影（§5.6 辅轴——与任务状态正交）。
    ArchivePhase archivePhaseAfter = ArchivePhase::NotApplicable;
};

// =====================================================================
// IRunRegistry / IResultAdmission（§10.5 接口原文）
// =====================================================================

/**
 * @brief 运行登记表接口（§10.5 原文——派发登记/尝试追加/一次性判定）。
 *
 * 实现 Lifecycle：实现随调度器创建（调度线程域）；classifyArrival/tryRun
 *   为纯查表（无副作用——§10.5 注），可被同线程编排随意调用。
 *
 * 线程约束：全部方法仅调度线程（P-PR-4 域；见文件头）。
 */
class IRunRegistry {
public:
    virtual ~IRunRegistry() = default;

    /**
     * @brief 派发时登记（§4.3 分配协议第二段——五元组＋runDir＋扩展
     *        字段＋资源上下文一次性入表）。
     *
     * @param task   [in] 关联任务（isValid 强制）
     * @param input  [in] 登记输入（身份/绑定/归档位置/资源——见结构注释）
     * @return 登记表内记录的可变引用（节点稳定——unordered_map 不搬移；
     *         调度线程域内有效，供派发编排回填预留会话等）
     *
     * @throws ExecutionError(InvalidState) 调用方契约违约：task/identity
     *         无效、run 重复登记、evaluatorKey/runKind 空、runDir 空、
     *         allowedResultKinds 空、resources.evaluation/archive 空指针
     */
    virtual RunRegistration& registerRun(TaskId task, const RegistrationInput& input) = 0;

    /**
     * @brief 尝试追加（§4.3：重试/暂停后继续→新 AttemptId；旧 attempt
     *        移入 supersededAttempts——步 4 的迟到判定数据源）。
     *
     * @param run         [in] 运行（须已登记）
     * @param nextAttempt [in] 新尝试号（须严格大于当前 attempt 且不在
     *                    superseded 中——调用方违约 fail-fast）
     *
     * @throws ExecutionError(InvalidState) run 未登记、attempt 不前进
     */
    virtual void appendAttempt(core::RunId run, core::AttemptId nextAttempt) = 0;

    /**
     * @brief 一次性判定（九步之 1~7；纯查表＋比对，无副作用——§10.5 注）。
     *
     * 步 2/步 4 的次序细则（§9.2 两行并存的自洽化，实现冻结）：五元组
     *   核对中 attemptId 字段按三分支处理——等于登记当前 attempt→通过；
     *   属 supersededAttempts→放行至步 4 出 StaleAttempt；其余→步 2 出
     *   IdentityMismatch。由此 §9.2 步 2（任一字段不符→Mismatch）与
     *   步 4（陈旧 attempt→StaleAttempt）同时可观测，语义不互相吞没。
     *
     * @param arrival [in] 到达信封（只读——判定不改到达）
     * @return 判定结论（verdict==Admitted 表示 1~7 全绿——步 8/9 在
     *         IResultAdmission::admitResult 继续）
     */
    virtual AdmissionDecision classifyArrival(const ArrivalEnvelope& arrival) const = 0;

    /**
     * @brief 按运行身份取登记记录只读快照（整记录拷贝——无共享可变
     *        状态外泄；noexcept：查表无失败路径，未登记返回 nullopt）。
     */
    virtual std::optional<RunRegistration> tryRun(core::RunId run) const noexcept = 0;

    /**
     * @brief 终结标记（§4.3/§9.1：终态镜像＋terminationCause 一次写入
     *        ——步 7 的判定输入；取消/失败/强杀路径由调度编排调用）。
     *
     * @throws ExecutionError(InvalidState) run 未登记、重复终结
     */
    virtual void markTerminated(core::RunId run, TerminationCause cause) = 0;
};

/**
 * @brief 结果接纳接口（§10.5 原文——九步之 8~9＋归档触发＋事件）。
 *
 * 职责边界：步 1~7 的判定归 IRunRegistry::classifyArrival（纯查询）；
 *   本接口从步 8 起（名称反解→envelope 组装→归档→事件），并承载
 *   §9.3 的重投递幂等/内容冲突处置。
 *
 * 线程约束：admitResult 仅调度线程串行调用（§10.5 注——P-PR-4 域）。
 */
class IResultAdmission {
public:
    virtual ~IResultAdmission() = default;

    /**
     * @brief 接纳一次到达（九步 8~9＋归档触发＋缓存登记＋事件——§10.5
     *        原文注；缓存登记面随 EX-T08 缓存治理接线，本任务不含）。
     *
     * 后置（§10.5 原文注的展开）：
     *   - Admitted→envelope 构造校验通过→T8（Running→Completed）→
     *     归档事务（§9.5：begin→writeBatch→finalize→ResultArchived 事件；
     *     归档失败不改任务终态——§9.3 末行）；
     *   - 构造失败（ConstructionFailed/PayloadUndecodable/NameUnresolved）
     *     →T9＋诊断（§9.2 步 9"make 失败→Running→Failed"）；
     *   - 步 1~7 拒绝→任务状态不变（到达被丢弃——拒绝≠任务失败）；
     *   - DuplicateIgnored→幂等成功（不重写、不报错——§9.3 重投递行）。
     *
     * @param arrival [in,out] 到达信封（rvalue——载荷被移动消费）
     * @return 接纳编排结果（判定＋权威产物＋两轴投影——见结构注释）
     *
     * @throws ExecutionError(InvalidState) 调用方契约违约：到达五元组
     *         无效（保留值身份不可作到达——fail-fast）；其余到达数据
     *         问题一律结构化拒绝（不走异常——AGENTS §3 二分的可预期侧）
     */
    virtual AdmissionOutcome admitResult(ArrivalEnvelope&& arrival) = 0;

    /// 归档阶段查询（§10.5 原文；未登记运行→NotApplicable；noexcept 纯查）。
    virtual ArchivePhase archivePhase(core::RunId run) const noexcept = 0;
};

// =====================================================================
// RunRegistry——登记表实现（§9.1/§4.3）
// =====================================================================

/**
 * @brief 运行登记表实现（IRunRegistry 的执行侧本体——调度线程域）。
 *
 * 生命周期：调度器创建并独占（EX-T05 接线）；本实现无内部锁——单写者
 *   纪律由"仅调度线程"约束承担（§4.2 通用约定）。
 *
 * 时钟：registeredAtUtc 经注入 UtcClockFn 产出（缺省 system_clock::now）；
 *   测试注入固定时钟保证留痕确定性（ManualClock 同款纪律——不 sleep）。
 */
class RunRegistry final : public IRunRegistry {
public:
    /// UTC 墙钟注入形态（返回登记时刻；空＝system_clock::now）。
    using UtcClockFn = std::function<std::chrono::system_clock::time_point()>;

    /// 登记表 schema 版本（§9.1 registryVersion 行：当前 1）。
    static constexpr std::uint32_t kRegistryVersion = 1;

    /// 构造（时钟可注入——测试确定性）。
    explicit RunRegistry(UtcClockFn clock = nullptr);

    RunRegistration& registerRun(TaskId task, const RegistrationInput& input) override;
    void appendAttempt(core::RunId run, core::AttemptId nextAttempt) override;
    AdmissionDecision classifyArrival(const ArrivalEnvelope& arrival) const override;
    std::optional<RunRegistration> tryRun(core::RunId run) const noexcept override;
    void markTerminated(core::RunId run, TerminationCause cause) override;

    // ---- 编排内部维护面（ResultAdmission 专用——不在 IRunRegistry 契约内） ----

    /**
     * @brief 接纳完成镜像（T8——登记镜像 currentState→Completed；由
     *         ResultAdmission 在步 9 通过后调用；任务状态机的真实转移
     *         归调度编排持有，本镜像只保登记表与状态机一致——§9.1
     *         currentState 行"随任务状态机同步"）。
     * @throws ExecutionError(InvalidState) run 未登记
     */
    void noteAdmissionCompleted(core::RunId run);

    /**
     * @brief 归档阶段镜像推进（§5.6 辅轴——ResultAdmission 归档事务的
     *         各阶段调用；不改变 currentState——两轴正交的落地）。
     * @throws ExecutionError(InvalidState) run 未登记
     */
    void noteArchivePhase(core::RunId run, ArchivePhase phase);

    /**
     * @brief 归档会话回写（§9.1 resourceContext"归档会话引用"的登记侧
     *         维护——ResultAdmission 补建 begin 的会话写回登记记录，后续
     *         到达复用同一会话；finalize/abandon 终结后以空句柄回写清除）。
     * @throws ExecutionError(InvalidState) run 未登记
     */
    void noteArchiveSession(core::RunId run, project::ArchiveSessionRef session);

private:
    /// 查表（未登记返回 nullptr；两态重载——镜像推进用可变版）。
    RunRegistration* find(core::RunId run) noexcept;
    const RunRegistration* find(core::RunId run) const noexcept;

    UtcClockFn m_clock;                                ///< UTC 时钟（空＝now）
    /// 登记表（键＝RunId；节点式容器——registerRun 返回引用稳定不搬移）。
    std::unordered_map<core::RunId, RunRegistration> m_runs;
};

// =====================================================================
// ResultAdmission——九步 8~9＋归档触发编排（§9.2/§9.3/§9.5）
// =====================================================================

/**
 * @brief 结果接纳编排实现（IResultAdmission 本体——§9.2 步 8/9、§9.3
 *        重投递/冲突/有界重试、§9.5 归档协作的执行侧串接点）。
 *
 * 编排序（对 §9.2 九步的承接位置）：
 *   步 1~7（classifyArrival，登记表）→ 步 8（名称反解——INameResolverAdapter
 *   以登记 nameMapIdentity 绑定；先反解后接纳，CON-06）→ 步 9（aggregateVerdict
 *   →validateCombination 快照绑定重载→ResultEnvelope::make——evidence 校验器
 *   单点复用不复制，acceptance 3）→ T8/归档事务（§9.5：begin〔预留补建＋
 *   A7 防御〕→writeBatch→finalize→ResultArchived 事件）。
 *
 * 归档失败语义（§9.3 末行——实现参数 D-11）：writeBatch/finalize 环境类
 *   失败→有界自动重试（默认 2 次、退避 1 s→4 s）→仍失败→abandon＋
 *   EX-ARCHIVE-FAILED 诊断＋任务终态不变（Completed 保持、archivePhase=
 *   ArchiveFailed）；ArchiveConflict 不重试（数据冲突非瞬态——abandon＋
 *   Rejected(ContentConflict)，不覆盖既有归档）。
 *
 * 完成事件幂等无上限（P-PR-4 冻结义务之二）：同 (run,attempt) 的相同
 *   FinalOutput 重投递每次都幂等处理（DuplicateIgnored——不重写、不报
 *   错、不限制次数）；ResultArchived 事件仅在首次 finalize 成功后发布
 *   一次（幂等命中不重发——project D-14 同源语义）。
 *
 * 线程约束：仅调度线程（构造/注入/setter 亦同域——装配在调度线程启动
 *   前完成的 L5 形态）。
 */
class ResultAdmission final : public IResultAdmission {
public:
    /// 归档重试实现参数（§9.3 末行 D-11——非上游值，登记单元卡 §15.4）。
    struct Config {
        std::uint32_t archiveRetries = 2;          ///< 有界重试次数（0＝禁重试）
        std::chrono::milliseconds archiveBackoffInitial{1000}; ///< 首次退避（单位 ms）
        std::chrono::milliseconds archiveBackoffCap{4000};     ///< 退避上限（单位 ms）
    };

    /// 重试退避的延迟注入（缺省 std::this_thread::sleep_for；测试注入
    /// no-op 虚拟推进——不 sleep，testkit §6.5 同纪律）。
    using DelayFn = std::function<void(std::chrono::milliseconds)>;

    /// UTC 墙钟（finalize 时间戳；空＝system_clock::now）。
    using UtcClockFn = std::function<std::chrono::system_clock::time_point()>;

    /**
     * @brief 构造（登记表与诊断 sink 必备；协作件经 setter 注入）。
     *
     * @param registry   [in] 登记表（非所有权引用——调用方保证存活期覆盖）
     * @param diagnostics [in] 诊断 sink（拒绝诊断＋开发通道——非空）
     * @param clock      [in] UTC 时钟（空＝system_clock::now）
     * @param config     [in] 归档重试参数（缺省＝D-11 值）
     * @param delay      [in] 退避延迟（空＝sleep_for——生产形态）
     */
    ResultAdmission(RunRegistry& registry, IExecutionDiagnosticsSink& diagnostics,
                    UtcClockFn clock = nullptr, Config config = {}, DelayFn delay = nullptr);

    /// 注入名称反解适配（步 8；空指针＝无反解能力——到达载荷含运行时
    /// 名称时按 NameUnresolved 拒绝，fail-closed 不跳过）。
    void setNameResolver(const INameResolverAdapter* resolver) noexcept;

    /// 注入评估产出解码器（FinalEnvelope 到达的步 9 前置；空指针时
    /// FinalEnvelope 到达按 PayloadUndecodable 拒绝——装配缺失不静默）。
    void setOutputDecoder(const IEvaluationOutputDecoder* decoder) noexcept;

    /// 注入事件总线（ResultArchived 发布面；空＝不发布〔总线未装配〕
    /// ——开发通道留痕一次，不算错误：core D-09 事件不持久化）。
    void setEventBus(core::IDomainEventBus* bus) noexcept;

    AdmissionOutcome admitResult(ArrivalEnvelope&& arrival) override;
    ArchivePhase archivePhase(core::RunId run) const noexcept override;

    /// 累计拒绝到达数（§9.3"丢弃计数（观测通道健康）"的观测面）。
    std::uint64_t rejectedArrivalCount() const noexcept;

private:
    /// 归档事务（begin〔A7 防御〕→writeBatch→finalize〔FinalEnvelope〕）
    /// 的执行结果。
    enum class ArchiveResult { Finalized, BatchWritten, Failed, Conflict, AuthorityLost };

    /// FinalEnvelope 接纳（步 8~9＋T8＋归档＋事件）。
    AdmissionOutcome admitFinalEnvelope(ArrivalEnvelope&& arrival,
                                        const RunRegistration& record);
    /// ResultBatch 接纳（分类已过——批次落盘，不 finalize）。
    AdmissionOutcome admitResultBatch(ArrivalEnvelope&& arrival,
                                      const RunRegistration& record);
    /// 其余类型的登记核对面处置（EX-T04 边界——见 admitResult 内注）。
    AdmissionOutcome admitNonArchivedKind(ArrivalEnvelope&& arrival,
                                          const RunRegistration& record);

    /// 归档事务主体（供两类接纳复用；finalizeManifest 空＝批次路径）。
    ArchiveResult runArchiveTransaction(const RunRegistration& record,
                                        const core::TaskIdentity& identity,
                                        const std::vector<std::uint8_t>& payloadBytes,
                                        const std::string& itemName,
                                        bool doFinalize,
                                        AdmissionOutcome& outcome);

    RunRegistry& m_registry;                        ///< 登记表（非所有权）
    IExecutionDiagnosticsSink& m_diagnostics;       ///< 诊断 sink（非所有权）
    UtcClockFn m_clock;                             ///< UTC 时钟（空＝now）
    Config m_config;                                ///< 归档重试参数（D-11）
    DelayFn m_delay;                                ///< 退避延迟（空＝sleep_for）
    const INameResolverAdapter* m_nameResolver = nullptr;   ///< 步 8 反解（可空）
    const IEvaluationOutputDecoder* m_decoder = nullptr;    ///< 步 9 前置解码（可空）
    core::IDomainEventBus* m_eventBus = nullptr;    ///< ResultArchived 发布面（可空）
    /// 已接纳 FinalEnvelope 的载荷指纹（run→SHA-256 十六进制——重投递
    /// 幂等判据，§9.3"归档 manifest 摘要一致"的执行侧镜像）。
    std::unordered_map<core::RunId, std::string> m_admittedFinalDigest;
    /// 已接纳批次的载荷指纹（run→最近批次 SHA-256——批次级重投递判据）。
    std::unordered_map<core::RunId, std::string> m_batchDigest;
    std::uint64_t m_rejectedArrivals = 0;           ///< 丢弃计数（§9.3 观测面）
    /// "总线未装配"开发留痕的一次性标记（避免每次接纳重复告警）。
    bool m_eventBusAbsenceReported = false;
};

}  // namespace sdurws::ird::execution

#endif  // SDURWS_IRD_EXECUTION_RUNREGISTRY_HPP
