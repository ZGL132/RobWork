/**
 * @file   WorkflowHostAdapters.hpp
 * @brief  workflow 宿主装配面（L5 装配层）——编排核端口的标准适配器族、
 *         基线虚类 ILifecycleFlowController 的宿主实现
 *         （WorkflowLifecycleController）与装配核查函数
 *         （assembleWorkflowLifecycleController——F-536 同族防静默空转）。
 *         （宿主收口批次 ASM-WF 落位。）
 *
 * 设计依据：
 *   - ARCHITECTURE L5 装配层口径（宿主装配层同时可见多单元公共头，R-1
 *     约束的是 workflow 计算库的依赖面不约束装配层；L4 编排单元的端口
 *     接缝在此桥接真实对端）＋既有 plugin/ 先例（WorkflowCommandCatalog——
 *     R-3 文件域隔离面；ui_plugin 宿主装配消费域 assembly 门面的目标级
 *     登记边先例）
 *   - units/workflow.md §10.2 v0.5~v1.2 偏差登记（九端口接缝"实现归
 *     L5 装配层"的逐端口兑现）：IDomainInitSubmitter（v0.5——modeling
 *     公共契约消费面）、ICloseDecisionPort/ICloseDraftPort/ICloseDrainPort
 *     （v0.7——ui 宿主面决策/DraftService 投影/execution 排空折叠）、
 *     IExternalRelinkPort（v0.9——io 检测＋project 命令提交双对端折叠）、
 *     ITitleFactPort/IRecoveryFactPort（v1.0——PM-11 五路/PM-15 目录
 *     折叠）、ISchemeMetricPort/IComparisonDiffPort（v1.2——指标投影/
 *     modeling diff 桥接）
 *   - F-536（宿主策略端口缺口——"端口未装配（装配缺陷）"教训）：装配
 *     期对端口有无实现逐一核查、缺即 fail-fast；运行期对内部缝缺失给
 *     显式拒绝面——不允许静默空转
 *   - units/project.md §9.7＋units/execution.md §7.5（ICloseDrainPort 的
 *     execution 桥接口径——shutdown(DrainPolicy)＋drained 有界轮询）
 *
 * ★ 落位范围口径（诚实登记，防扩大）：
 *   1. 桥接面按依赖白名单分层——workflow 八边内（project/execution/
 *      diagnostics/io/ui）的真实对端**真实桥接**（store 查询/命令面、
 *      execution 调度与排空、诊断目录折叠）；业务域侧（modeling 载荷
 *      组装、io 检测设施、modeling diff 服务、各域指标投影）按 R-1 以
 *      **注入缝**承载（宿主装配批次注入真实桥——本适配器承载接缝形状
 *      与 F-536 拒绝面；缺失登记于单元卡 §10.2 本批偏差登记段）。
 *   2. 呈现面只承诺 UX-03 三字段通用出口（presentFailure）＋可选通知
 *      （presentNotice）——对话框控件归 ui 宿主面（D-WF-6），本面零
 *      Qt 控件（plugin/ 文件域允许 Qt，但适配器逻辑保持零 Qt 以便
 *      测试目标直链消费）。
 *   3. WorkflowLifecycleController 六方法＝编排核转发的宿主形态——
 *      步骤推进/实时摘要等向导 UI 交互仍归宿主（编排核只在确认时刻
 *      被消费，NewProjectWizardFlow 注）。
 *
 * 线程约束：全部类型为主线程会话内使用（§10.3 流程编排行）；缝回调
 * 在编排调用线程同步发生（模态语义）。
 */

#ifndef IRD_WORKFLOW_PLUGIN_WORKFLOWHOSTADAPTERS_HPP
#define IRD_WORKFLOW_PLUGIN_WORKFLOWHOSTADAPTERS_HPP

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Identity.hpp>            // core::ProjectId/ObjectId（会话状态与重关联定位——core 强类型）
#include <sdurws/ird/diagnostics/Catalog.hpp>      // diagnostics::DiagCatalog/IDevLogSink（恢复事实桥对端——白名单边）
#include <sdurws/ird/execution/Scheduler.hpp>      // execution::TaskScheduler/TaskController（排空桥接对端——白名单边）
#include <sdurws/ird/project/CommandService.hpp>   // project::CommandEnvelope（领域初始化提交缝的信封面——①端口）
#include <sdurws/ird/project/ProjectStore.hpp>     // project::ProjectStore（查询/命令面桥接对端——白名单边）
#include <sdurws/ird/workflow/Comparison.hpp>      // workflow::ISchemeMetricPort/IComparisonDiffPort（v1.2 端口）
#include <sdurws/ird/workflow/Lifecycle.hpp>       // workflow::IDomainInitSubmitter/ICloseDecisionPort/ICloseDraftPort/ICloseDrainPort/IExternalRelinkPort/ILifecycleFlowController 等（v0.5~v0.9 端口与编排核）
#include <sdurws/ird/workflow/Projection.hpp>      // workflow::ITitleFactPort/IRecoveryFactPort（v1.0 端口）
#include <sdurws/ird/ui/UiTypes.hpp>               // ui::CurrentnessProjection（标题事实搬运值——ui 公共值面）

namespace sdurws {
namespace ird {
namespace workflow {

// =====================================================================
// 端口适配器族（九接缝＋三个生命周期执行端口——逐一兑现"实现归 L5"）
// =====================================================================

/**
 * @brief 领域初始化提交适配器（IDomainInitSubmitter 的 L5 标准实现）。
 *
 * 分工：载荷组装半区＝注入缝（PayloadComposer——宿主装配批次消费
 * modeling 公共契约组装 DomainInitRequest 对应的领域命令信封；R-1 下
 * workflow 零 modeling 头，缝即接缝）；提交与结果折叠半区＝本适配器
 * 真实桥接 project ①端口（store.commands().submit——白名单边）。
 *
 * F-536 拒绝面：缝未注入（空 std::function）即 WorkflowError fail-fast
 * （提交入口是强义务缝——静默返回失败会变成"创建永远失败"的空转）。
 *
 * 线程约束：主线程会话内（向导确认动作）。
 */
class DomainInitSubmitterAdapter final : public IDomainInitSubmitter {
public:
    /// 载荷组装缝（宿主注入）：按请求组装领域命令信封；返回 nullopt＝
    /// 组装通道未产出（数据侧拒绝——committed=false 如实呈现，不吞）。
    using PayloadComposer = std::function<std::optional<project::CommandEnvelope>(
        project::ProjectStore& store, const DomainInitRequest& request)>;

    /// @param composer [in] 载荷组装缝（装配期注入——见类注 F-536 拒绝面）
    explicit DomainInitSubmitterAdapter(PayloadComposer composer);

    DomainInitResult submitInitialization(project::ProjectStore& store,
                                          const DomainInitRequest& request) override;

private:
    PayloadComposer m_composer;///< 载荷组装缝（非 owning 函数对象）
};

/**
 * @brief 统一确认决策适配器（ICloseDecisionPort 的 L5 标准实现）。
 *
 * 双决策缝＝ui 宿主面对话框收集（D-WF-6：控件归 ui、决策词表归本单元
 * ——适配器只做词表值的中转）；缝未注入即 WorkflowError fail-fast
 * （决策点没有"默认继续"——静默放行会绕过 PM-03 统一确认，F-536）。
 *
 * 线程约束：主线程会话内（模态阻塞语义——缝回调阻塞至用户作答）。
 */
class DialogCloseDecisionAdapter final : public ICloseDecisionPort {
public:
    /// 草稿三选收集缝（呈现数据入——用户决策出；宿主渲染三选对话框）。
    using DraftDecisionFn = std::function<DraftDisposition(const CloseDialogData&)>;
    /// 任务二选收集缝（同上——二选＋9 态短标签清单）。
    using TaskDecisionFn = std::function<RunningTaskDecision(const CloseDialogData&)>;

    DialogCloseDecisionAdapter(DraftDecisionFn draftDecision,
                               TaskDecisionFn taskDecision);

    DraftDisposition collectDraftDisposition(const CloseDialogData& data) override;
    RunningTaskDecision collectRunningTaskDecision(const CloseDialogData& data) override;

private:
    DraftDecisionFn m_draftDecision;///< 草稿三选缝
    TaskDecisionFn m_taskDecision;  ///< 任务二选缝
};

/**
 * @brief 草稿处置适配器（ICloseDraftPort 的 L5 标准实现）。
 *
 * 分工：清单半区＝**真实桥接** project 查询面（store.query().listDrafts
 * ——磁盘半源实测）∪ 会话半区缝（SessionHalf.unapplied——编辑器未保存
 * 态权威，ui 会话层）；保存/放弃动作＝会话缝（草稿文档组装归编辑器
 * 会话——编排核零草稿内容知识，ICloseDraftPort 端口契约原文）。
 *
 * 错误语义：会话缝未注入 → WorkflowError fail-fast（草稿态权威缺失
 * ＝装配缺陷——静默视为"无草稿"会丢失用户编辑，F-536）；动作返回
 * false＝对端失败（编排核转 Failed——本适配器零吞错直通）。
 */
class StoreCloseDraftAdapter final : public ICloseDraftPort {
public:
    /// 编辑器会话半区（草稿态权威——ui 会话层注入）。
    struct SessionHalf {
        /// 未应用草稿模块清单（会话脏半源；空＝无）。
        std::function<std::vector<std::string>()> unapplied;
        /// 保存草稿动作（PM-04 落盘——false＝失败直通）。
        std::function<bool()> save;
        /// 放弃草稿动作（false＝失败直通）。
        std::function<bool()> discard;
    };

    /// @param store   [in] 项目存储上下文（非 owning——磁盘半源查询）
    /// @param session [in] 会话半区（见类注——F-536 拒绝面）
    StoreCloseDraftAdapter(project::ProjectStore& store, SessionHalf session);

    std::vector<std::string> unappliedDraftModules() override;
    bool saveDrafts() override;
    bool discardDrafts() override;

private:
    project::ProjectStore& m_store;///< 存储上下文（非 owning）
    SessionHalf m_session;         ///< 会话半区（草稿态权威）
};

/**
 * @brief 任务排空适配器（ICloseDrainPort 的 L5 标准实现——execution
 *        §7.5 的 workflow 侧视图真实桥）。
 *
 * 桥接面（全部白名单边）：hasActiveTask/taskStates＝TaskScheduler::
 * tasksByProject 九态投影（非终态判定＝Queued/Preparing/Running/Paused/
 * Canceling——core::TaskState 词表零新增）；waitDrain＝shutdown(
 * DrainPolicy::CancelQueuedAndWait)＋**有界轮询** drained()（无永久等待
 * ——execution §7.5；超限 false＝编排核转 Failed）；cooperativeCancel＝
 * 对任务清单逐任务 TaskController::requestCancel（任意线程命令通道）
 * 后走同一有界排空（取消协议的清理/abandon 语义归 execution——N4）。
 *
 * 线程约束：主线程会话内（编排核调用线程）；内部等待轮询只观察不推进
 * （execution 后台机制推进——InFlightArchiveContractTest 桥同款纪律）。
 */
class ExecutionCloseDrainAdapter final : public ICloseDrainPort {
public:
    /// @param scheduler  [in] 执行调度器（非 owning）
    /// @param controller [in] 取消/终止协议引擎（非 owning——协作取消通道）
    /// @param drainBound [in] 排空有界等待上限（默认 30 s——与契约测试
    ///                   桥同量级的工程余量；非上游值，登记实现参数）
    ExecutionCloseDrainAdapter(execution::TaskScheduler& scheduler,
                               execution::TaskController& controller,
                               std::chrono::milliseconds drainBound
                                   = std::chrono::seconds{30});

    bool hasActiveTask(core::ProjectId project) override;
    std::vector<core::TaskState> taskStates(core::ProjectId project) override;
    bool waitDrain() override;
    bool cooperativeCancel() override;

private:
    /// 有界排空轮询（shutdown 后调用——drained 无阻塞快照逐拍观察）。
    bool drainBounded();

    execution::TaskScheduler& m_scheduler; ///< 执行调度器（非 owning）
    execution::TaskController& m_controller;///< 协议引擎（非 owning）
    core::ProjectId m_project;       ///< 协作取消的项目过滤键（hasActiveTask 回填）
    std::chrono::milliseconds m_drainBound;///< 排空有界等待上限
};

/**
 * @brief 外部源重关联适配器（IExternalRelinkPort 的 L5 标准实现）。
 *
 * 双缝全注入（边界登记——wp19-t09 §19.3 同款诚实条款）：probe 半区
 * 桥接 io 缺失/变化检测（NFR-REL-04——检测设施与 ExternalRefRecord
 * 登记基准比对的公共消费面随宿主装配批次接线）；relink 半区桥接
 * project ①命令端口（重关联命令 token/载荷归 project 存储侧
 * WP-04-T17——契约未生成，且 P-PR-9 纪律下本面零命令 token 知识、
 * 不私自预置词形）。缝未注入 → WorkflowError fail-fast（重关联入口
 * 静默空转＝用户动作无响应，F-536）。
 */
class IoExternalRelinkAdapter final : public IExternalRelinkPort {
public:
    /// 检测缝（io 桥——按请求资源定位登记记录并比对现路径内容）。
    using ProbeFn = std::function<ExternalSourceStatus(const RelinkRequest&)>;
    /// 提交缝（project ①端口桥——显式提交执行）。
    using RelinkFn = std::function<RelinkExecution(const RelinkRequest&,
                                                   const ExternalSourceStatus&)>;

    IoExternalRelinkAdapter(ProbeFn probe, RelinkFn relink);

    ExternalSourceStatus probe(const RelinkRequest& request) override;
    RelinkExecution relink(const RelinkRequest& request,
                           const ExternalSourceStatus& status) override;

private:
    ProbeFn m_probe;    ///< 检测缝
    RelinkFn m_relink;  ///< 提交缝
};

/**
 * @brief 标题栏事实适配器（ITitleFactPort 的 L5 标准实现）。
 *
 * 分工：store 半区＝**真实桥接**（displayName=project.json 权威元数据/
 * writable=INV-SES-1 唯一判定源/schemeLabel 磁盘兜底=branchTips 首行
 * label/anyDirty 磁盘半源=listDrafts 实测）；会话半区＝注入缝
 * （SessionHalf——活动分支登记/会话脏/当前性投影，宿主会话层与 ui C-7
 * 权威）。会话半区字段可逐项缺省（端口契约"环境失败折叠安全缺省"
 * ——标题栏常驻呈现面不允许单次取数失败炸宿主）：缺省时 schemeLabel
 * 退 branchTips 兜底、anyDirty 只算磁盘半源、当前性归 nullopt（不可
 * 判定——P-UI-2 归入过期呈现）。store 查询异常同折叠（不外抛）。
 *
 * 线程约束：const 只读可并发（端口契约——titleStatus() 并发安全的
 * 对端承诺；缝实现须各自满足 const 并发契约）。
 */
class StoreTitleFactAdapter final : public ITitleFactPort {
public:
    /// 会话半区（宿主会话层/ui C-7 权威——可逐项缺省，见类注）。
    struct SessionHalf {
        /// 会话脏汇总位（DraftingSummary.anyDirty——nullopt=未装配）。
        std::function<std::optional<bool>()> sessionDirty;
        /// 活动分支显示名（会话层登记——nullopt=未装配）。
        std::function<std::optional<std::string>()> activeBranchLabel;
        /// 当前性投影（ui C-7 搬运——nullopt=未装配→不可判定）。
        std::function<std::optional<ui::CurrentnessProjection>()> currentness;
    };

    StoreTitleFactAdapter(const project::ProjectStore& store, SessionHalf session);

    TitleFacts collectFacts() const override;

private:
    const project::ProjectStore& m_store;///< 存储上下文（非 owning——只读）
    SessionHalf m_session;               ///< 会话半区（可逐项缺省）
};

/**
 * @brief 恢复事实适配器（IRecoveryFactPort 的 L5 标准实现——PM-15
 *        "经统一诊断目录集成"的结构兑现桥）。
 *
 * 桥接面：DiagCatalog::snapshot 全量快照按稳定码逐字识别三场景
 * （PRJ-RECOVERY-IGNORED-UNCOMMITTED→①/EX-TASK-INTERRUPTED→②/
 * PRJ-RECOVERY-ORPHAN-DRAFT→③）；计数事实源＝ReportHalf.report 缝
 * （open 产物 RecoveryReport 清单长度——宿主在打开后持有；涉事条目
 * 口径≠目录折叠口径，RecoveryScenarioFact 端口契约）。横幅事实**只**
 * 经目录进入本单元（不直连 RecoveryReport 判定存在性——PA-1）。缝
 * 未注入 → 折叠空清单（横幅不渲染——端口契约降级；打开期产物未持有
 * ＝环境缺省，与 ITitleFactPort 同款"不炸宿主"纪律）。
 */
class StoreRecoveryFactAdapter final : public IRecoveryFactPort {
public:
    /// 计数事实半区（open 产物持有者注入——可空＝折叠空清单）。
    struct ReportHalf {
        /// 恢复报告快照（open ⑤步产物——清单长度即涉事条目计数）。
        std::function<std::optional<project::RecoveryReport>()> report;
    };

    /// @param catalog [in] 统一诊断目录（非 owning——真实桥对端）
    /// @param writable [in] 会话写权限（store->writable() 同源注入——
    ///                 放弃动作可用性输入；可空函数＝true 缺省）
    /// @param reportHalf [in] 计数事实半区（可空——见类注降级）
    StoreRecoveryFactAdapter(const diagnostics::DiagCatalog& catalog,
                             std::function<std::optional<bool>()> writable,
                             ReportHalf reportHalf);

    RecoveryFacts collectFacts() override;

private:
    const diagnostics::DiagCatalog& m_catalog;///< 统一诊断目录（非 owning）
    std::function<std::optional<bool>()> m_writable;///< 写权限缝（可空）
    ReportHalf m_report;               ///< 计数事实半区（可空）
};

/**
 * @brief 指标投影适配器（ISchemeMetricPort 的 L5 标准实现——委托缝）。
 *
 * §8.1 红线（本单元零指标口径知识）决定本端口**没有**可真实桥接的
 * workflow 侧半区：指标列/比较基准/当前性投影全部来自各域归档结果
 * 投影（各域数据源未落位前——阶段 C 可算性约束），缝即真实桥的宿主
 * 装配位。缝未注入 → WorkflowError fail-fast（比较入口静默空转＝
 * 用户看到空比较面而无解释，F-536）。
 */
class DelegatingSchemeMetricPort final : public ISchemeMetricPort {
public:
    /// 取数缝（与端口方法同形——宿主装配注入真实投影桥）。
    using CollectFn = std::function<std::vector<SchemeMetricFacts>(
        const std::vector<core::BranchId>& schemes)>;

    explicit DelegatingSchemeMetricPort(CollectFn collect);

    std::vector<SchemeMetricFacts> collect(
        const std::vector<core::BranchId>& schemes) const override;

private:
    CollectFn m_collect;///< 取数缝
};

/**
 * @brief diff 投影适配器（IComparisonDiffPort 的 L5 标准实现）。
 *
 * 缺省形态＝**恒 available=false**（端口契约明文的诚实降级："通道未
 * 装配→available=false，entries 必空"——modeling IModelDiffService 桥
 * 接在 R-1 下归宿主装配批次，本适配器承载降级面不虚构差异，P-OPT-8）；
 * 注入 DiffFn 后委托（宿主装配后翻转——不改变编排核消费方式）。
 */
class UnavailableComparisonDiffPort final : public IComparisonDiffPort {
public:
    /// 取数缝（可选注入——nullopt=恒降级；有值=委托）。
    using DiffFn = std::function<SchemeDiffFacts(const core::BranchId&,
                                                 const core::BranchId&)>;

    explicit UnavailableComparisonDiffPort(DiffFn diff = DiffFn{});

    SchemeDiffFacts diff(const core::BranchId& baseline,
                         const core::BranchId& candidate) const override;

private:
    DiffFn m_diff;///< 取数缝（可空——恒降级形态）
};

/**
 * @brief 另存执行适配器（ISaveAsPort 的 L5 标准实现——委托缝）。
 *
 * "复制执行归 project"（§7.4 行一）——WP-04-T18 存储侧契约未生成
 * （豁免 dependsOn 边的触达面），真实复制桥归宿主装配批次；本适配器
 * 承载接缝形状与 F-536 拒绝面（缝未注入 → WorkflowError）。
 */
class DelegatingSaveAsPort final : public ISaveAsPort {
public:
    using ExecuteFn = std::function<Execution(project::ProjectStore&,
                                              const SaveAsRequest&,
                                              IFlowCancelToken*,
                                              const FlowProgressCallback&)>;
    explicit DelegatingSaveAsPort(ExecuteFn execute);
    Execution executeCopy(project::ProjectStore& source,
                          const SaveAsRequest& request,
                          IFlowCancelToken* cancel,
                          const FlowProgressCallback& progress) override;

private:
    ExecuteFn m_execute;///< 复制执行缝
};

/**
 * @brief 包导出执行适配器（IPackageExportPort 的 L5 标准实现——委托缝；
 *        "导出执行归 io"——io 桥接归宿主装配批次，F-536 拒绝面同上）。
 */
class DelegatingPackageExportPort final : public IPackageExportPort {
public:
    using ExecuteFn = std::function<Execution(project::ProjectStore&,
                                              const PackageExportRequest&,
                                              IFlowCancelToken*,
                                              const FlowProgressCallback&)>;
    explicit DelegatingPackageExportPort(ExecuteFn execute);
    Execution exportPackage(project::ProjectStore& source,
                            const PackageExportRequest& request,
                            IFlowCancelToken* cancel,
                            const FlowProgressCallback& progress) override;

private:
    ExecuteFn m_execute;///< 导出执行缝
};

/**
 * @brief 包导入执行适配器（IPackageImportPort 的 L5 标准实现——委托缝；
 *        "校验归 io、发布归 project"端口内折叠——桥接归宿主装配批次）。
 */
class DelegatingPackageImportPort final : public IPackageImportPort {
public:
    using ExecuteFn = std::function<PackageImportExecution(
        const PackageImportRequest&, IFlowCancelToken*, const FlowProgressCallback&)>;
    explicit DelegatingPackageImportPort(ExecuteFn execute);
    PackageImportExecution importPackage(const PackageImportRequest& request,
                                         IFlowCancelToken* cancel,
                                         const FlowProgressCallback& progress) override;

private:
    ExecuteFn m_execute;///< 导入执行缝（校验＋发布端口内折叠）
};

// =====================================================================
// 宿主装配集（端口集＋会话桥——WorkflowLifecycleController 的注入面）
// =====================================================================

/**
 * @brief 生命周期端口集（编排核所需的全部端口接缝——装配核查面）。
 *
 * 指针全部非 owning（调用方保证存活期覆盖控制器使用期）；装配核查
 * （assembleWorkflowLifecycleController）逐一检查非空——任一为空即
 * WorkflowError 点名缺失端口（F-536：装配缺陷不允许静默空转）。
 */
struct WorkflowHostPorts {
    // ---- 九接缝（§10.2 v0.5~v1.2 登记清单）----
    IDomainInitSubmitter* domainInit = nullptr;       ///< 领域初始化提交（v0.5）
    ICloseDecisionPort* closeDecisions = nullptr;     ///< 统一确认决策收集（v0.7）
    ICloseDraftPort* closeDrafts = nullptr;           ///< 草稿处置（v0.7）
    ICloseDrainPort* closeDrain = nullptr;            ///< 任务排空（v0.7）
    IRelinkDecisionPort* relinkDecisions = nullptr;   ///< 重关联显式确认收集（v0.9）
    IExternalRelinkPort* externalRelink = nullptr;    ///< 外部源检测/提交（v0.9）
    ITitleFactPort* titleFacts = nullptr;             ///< 标题栏事实（v1.0）
    IRecoveryFactPort* recoveryFacts = nullptr;       ///< 恢复事实（v1.0）
    ISchemeMetricPort* schemeMetrics = nullptr;       ///< 方案指标投影（v1.2）
    IComparisonDiffPort* comparisonDiff = nullptr;    ///< 方案 diff 投影（v1.2）

    // ---- 生命周期执行端口（v0.7/v0.8 另存/包面——六方法闭环所需）----
    ISaveAsPort* saveAs = nullptr;                    ///< 另存复制执行
    IPackageExportPort* packageExport = nullptr;      ///< 包导出执行
    IPackageImportPort* packageImport = nullptr;      ///< 包导入执行
};

/**
 * @brief 宿主会话桥（控制器的宿主面回调集——收集/呈现/激活三类）。
 *
 * 模态语义：收集缝在编排调用线程同步阻塞至用户作答，返回 false/nullopt
 * ＝用户取消（取消不是错误——UX-03；编排核不进入或零副作用）。
 * 呈现缝：presentFailure 为**必填**（失败可见面——F-565/566 同族纪律：
 * 失败静默＝伪造成功；装配核查强制）；presentNotice 可空（成功/取消
 * 的提示面——宿主可自处理）。装配核查对必填缝逐一检查（F-536）。
 */
struct WorkflowHostBridges {
    // ---- 会话状态（权威归宿主会话层——PM-12"活动分支登记在会话层"）----
    /// 当前存储上下文（无项目会话＝nullptr——requestClose/startSaveAs 等
    /// 需要当前项目的入口在无项目时 WorkflowError 装配缺陷拒绝）。
    std::function<project::ProjectStore*()> currentStore;
    /// 当前项目身份（任务清单过滤键——CloseFlowRequest.projectId）。
    std::function<core::ProjectId()> currentProjectId;
    /// 会话激活（store/projectId 移交宿主会话层——新建/打开/切换/另存
    /// 进入的统一出口；宿主接手后负责会话层登记与投影刷新）。
    std::function<void(std::unique_ptr<project::ProjectStore>, core::ProjectId)> activateStore;

    // ---- 收集缝（模态——返回 false/nullopt＝用户取消）----
    /// PM-01 三步向导输入收集（步骤推进/实时摘要是宿主面事件驱动——
    /// 本缝只在确认成功时返回 true 并回填全部输入）。
    std::function<bool(NewProjectInputs&)> collectNewProjectInputs;
    /// PM-02 Dialog 来源路径收集（打开对话框；nullopt＝取消）。
    std::function<std::optional<std::filesystem::path>()> selectOpenPath;
    /// PM-03 Switch 候选项目路径收集（nullopt＝取消——切换零发生）。
    std::function<std::optional<std::filesystem::path>()> selectCandidateProjectPath;
    /// PM-05 另存请求收集（目标目录＋勾选——含记忆默认的宿主半区）。
    std::function<bool(SaveAsRequest&)> collectSaveAsRequest;
    /// PM-05 包导出请求收集（目标文件＋勾选）。
    std::function<bool(PackageExportRequest&)> collectPackageExport;
    /// PM-05 包导入请求收集（包文件＋目标目录；调用方可预填 packFile）。
    std::function<bool(PackageImportRequest&)> collectPackageImport;
    /// PM-09 重定向路径收集：nullopt＝取消流程；有值且 path 空＝保持
    /// 登记路径重连；有值且非空＝重定向到该路径。
    std::function<std::optional<std::filesystem::path>()> selectRelinkPath;

    // ---- 呈现缝 ----
    /// UX-03 三字段失败呈现（**必填**——全部六流程失败经此出口，见类注）。
    std::function<void(const std::string& context, const std::string& cause,
                       const std::string& recommendedAction)> presentFailure;
    /// 生命周期通知（成功/取消提示——可空；summary 为编排摘要串）。
    std::function<void(const std::string& summary)> presentNotice;

    // ---- 装配注入（编排核透传参数——可空合法）----
    core::IDomainEventBus* eventBus = nullptr;      ///< ⑤端口装配面（createNew/open 透传）
    project::IDiagnosticsSink* diagnosticsSink = nullptr;///< project 侧码记录登记面（透传）
};

/**
 * @brief 生命周期流程控制器的宿主实现（ILifecycleFlowController 逐字
 *        兑现——六方法把宿主缝收集的用户输入交给对应编排核）。
 *
 * 方法到编排核的映射（编排语义全部在编排核——本类零流程知识，只做
 * 缝编排与结果分派）：
 *   startNewProjectWizard → NewProjectWizardFlow::commit
 *   openProject           → OpenProjectFlow::run（.rwpack 分流→包导入向导）
 *   requestClose          → CloseFlow::run
 *   startSaveAsWizard     → SaveAsFlow::run
 *   startPackageWizard    → PackageExportFlow::run / PackageImportFlow::run
 *                           （Import 完成后按打开协议进入目标目录）
 *   startRelinkFlow       → RelinkFlow::run
 *
 * 线程约束：主线程会话内（六方法全部模态用户流程）。
 */
class WorkflowLifecycleController final : public ILifecycleFlowController {
public:
    /**
     * @brief 构造（装配点——通常经 assembleWorkflowLifecycleController
     *        装配核查后创建；直接构造不做端口核查）。
     */
    WorkflowLifecycleController(WorkflowHostPorts ports, WorkflowHostBridges bridges);

    // ---- ILifecycleFlowController（§10.2 Draft 签名逐字）----
    void startNewProjectWizard() override;
    void openProject(OpenSource source, std::string path) override;
    CloseFlowResult requestClose(CloseKind kind) override;
    void startSaveAsWizard() override;
    void startPackageWizard(PackageFlowKind kind) override;
    void startRelinkFlow(core::ObjectId resource) override;

private:
    /// 取当前存储上下文（无项目即 WorkflowError——需要当前项目的入口在
    /// 无项目会话被调用＝宿主装配缺陷，F-536 拒绝面）。
    project::ProjectStore& requireCurrentStore();

    /// 包导入向导的预填包文件（ASM-UI 修复③——收集一次语义的会话内
    /// 承载位）：openProject 的 .rwpack 分流把目标包路径登记于此后转
    /// startPackageWizard(Import)；后者构造请求时消费并**立即清空**
    /// （一次性——不跨流程残留）。此前形态＝分流段与向导段各调一次
    /// collectPackageImport（用户被问两次且首次预填丢失——asm-wf 验收
    /// 建议级①）。空值＝直接入口（无预填，收集缝自行补齐）。
    std::optional<std::filesystem::path> m_pendingImportPackFile;

    WorkflowHostPorts m_ports;    ///< 端口集（非 owning 指针集）
    WorkflowHostBridges m_bridges;///< 宿主会话桥（函数对象集）
};

/**
 * @brief 装配核查＋控制器装配（F-536 同族——装配期一次）。
 *
 * 核查清单（逐一执行，任一失败即 WorkflowError 并**点名缺失项**）：
 *   1. WorkflowHostPorts 全部 13 个端口指针非空（九接缝＋三执行端口＋
 *      重关联决策端口——编排核六方法的全部触达面）；
 *   2. WorkflowHostBridges 必填缝（currentStore/currentProjectId/
 *      activateStore/collectNewProjectInputs/selectOpenPath/
 *      selectCandidateProjectPath/collectSaveAsRequest/
 *      collectPackageExport/collectPackageImport/selectRelinkPath/
 *      presentFailure）非空——呈现缝缺失＝失败静默（伪造成功面），
 *      收集缝缺失＝入口不可用空转，一律装配违约。
 *
 * @param ports   [in] 端口集（核查通过后按值收编——指针仍非 owning）
 * @param bridges [in] 宿主会话桥（核查通过后按值收编）
 * @return 就绪控制器（六方法可直接消费）
 *
 * @throws WorkflowError 任一端口/必填缝缺失（消息点名缺失项——修复装配
 *         而不是捕获后继续）
 */
WorkflowLifecycleController assembleWorkflowLifecycleController(
    const WorkflowHostPorts& ports, const WorkflowHostBridges& bridges);

}  // namespace workflow
}  // namespace ird
}  // namespace sdurws

#endif  // IRD_WORKFLOW_PLUGIN_WORKFLOWHOSTADAPTERS_HPP
