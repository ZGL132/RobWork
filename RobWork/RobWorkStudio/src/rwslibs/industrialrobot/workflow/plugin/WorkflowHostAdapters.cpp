/**
 * @file   WorkflowHostAdapters.cpp
 * @brief  workflow 宿主装配面的实现翻译单元（头文件为权威契约——本文件
 *         承载适配器桥接面、控制器六方法编排与装配核查的逐步兑现）。
 *
 * 设计依据：见 WorkflowHostAdapters.hpp 文件头（L5 装配层口径、九端口
 * 逐端口兑现清单、F-536 拒绝面纪律、D-WF-6 宿主面分工）。本文件新增
 * 语义＝无（全部在头注释）。
 */

#include "WorkflowHostAdapters.hpp"

#include <exception>
#include <utility>

#include <sdurws/ird/project/QueryPort.hpp>  // project::IProjectQueryPort（listDrafts/branchTips——真实桥查询面）
#include <sdurws/ird/workflow/Types.hpp>     // workflow::WorkflowError（装配缺陷 fail-fast）

namespace sdurws {
namespace ird {
namespace workflow {

namespace {

/// 装配缺陷异常（消息点名缺失项——F-536 拒绝面的统一文案形态）。
WorkflowError missingPortError(const std::string& what)
{
    return WorkflowError("宿主装配缺陷：" + what + " 未装配（禁止静默空转）");
}

}  // namespace

// =====================================================================
// DomainInitSubmitterAdapter（v0.5——载荷组装缝＋①端口真实折叠）
// =====================================================================

DomainInitSubmitterAdapter::DomainInitSubmitterAdapter(PayloadComposer composer)
    : m_composer(std::move(composer))
{
}

DomainInitResult DomainInitSubmitterAdapter::submitInitialization(
    project::ProjectStore& store, const DomainInitRequest& request)
{
    // 缝未注入＝装配缺陷 fail-fast（强义务缝——静默失败会让"新建向导
    // 模板/URDF 路径永远失败"而无解释，F-536）。
    if (!m_composer) {
        throw missingPortError("领域初始化载荷组装缝（PayloadComposer）");
    }

    DomainInitResult result;
    // 组装半区（宿主消费 modeling 公共契约——R-1 缝）；nullopt＝组装
    // 通道拒绝（数据侧），如实转失败呈现（零吞错）。
    std::optional<project::CommandEnvelope> envelope;
    try {
        envelope = m_composer(store, request);
    } catch (const std::exception& e) {
        result.causeText = std::string("领域载荷组装异常：") + e.what();
        result.actionText = "检查模板/外部源输入后重试";
        return result;
    }
    if (!envelope.has_value()) {
        result.causeText = "领域载荷组装通道未产出信封（宿主组装半区拒绝）";
        result.actionText = "检查模板/外部源输入后重试";
        return result;
    }

    // ①端口提交（project 命令面真实桥）＋结果折叠：Committed→committed
    // ＋baselineRevision；其余三态→committed=false＋诊断原样透传（D-WF-7
    // 零加工）＋UX-03 半区组装。端口契约"禁止 committed=true 而无修订"
    // 由 project 侧 Committed⇒newRevision 契约保证，此处防御复核。
    project::CommandResult submitted;
    try {
        submitted = store.commands().submit(*envelope);
    } catch (const std::exception& e) {
        result.causeText = std::string("领域命令提交异常：") + e.what();
        result.actionText = "检查存储上下文状态后重试";
        return result;
    }
    result.diagnostics = submitted.diagnostics;  // 对端诊断透传（成功告警与失败定位）
    if (submitted.committed() && submitted.newRevision.has_value()) {
        result.committed = true;
        result.baselineRevision = submitted.newRevision;
        return result;
    }
    if (!result.diagnostics.empty()) {
        result.causeText = result.diagnostics.front().code
                       + std::string(": ") + result.diagnostics.front().cause;
    } else if (submitted.error.has_value()) {
        result.causeText = submitted.error->what();
    } else {
        result.causeText = "领域命令未提交成功（提交期校验/中止/环境失败）";
    }
    result.actionText = "根据透传诊断处理后重试";
    return result;
}

// =====================================================================
// DialogCloseDecisionAdapter（v0.7——双决策缝中转）
// =====================================================================

DialogCloseDecisionAdapter::DialogCloseDecisionAdapter(DraftDecisionFn draftDecision,
                                                       TaskDecisionFn taskDecision)
    : m_draftDecision(std::move(draftDecision))
    , m_taskDecision(std::move(taskDecision))
{
}

DraftDisposition DialogCloseDecisionAdapter::collectDraftDisposition(
    const CloseDialogData& data)
{
    if (!m_draftDecision) {
        throw missingPortError("草稿三选决策缝");
    }
    return m_draftDecision(data);  // 词表值中转（控件呈现归宿主——D-WF-6）
}

RunningTaskDecision DialogCloseDecisionAdapter::collectRunningTaskDecision(
    const CloseDialogData& data)
{
    if (!m_taskDecision) {
        throw missingPortError("任务二选决策缝");
    }
    return m_taskDecision(data);
}

// =====================================================================
// StoreCloseDraftAdapter（v0.7——store 磁盘半源真实桥＋会话缝）
// =====================================================================

StoreCloseDraftAdapter::StoreCloseDraftAdapter(project::ProjectStore& store,
                                               SessionHalf session)
    : m_store(store)
    , m_session(std::move(session))
{
}

std::vector<std::string> StoreCloseDraftAdapter::unappliedDraftModules()
{
    if (!m_session.unapplied) {
        throw missingPortError("草稿会话半区（unapplied）");
    }
    // 合一取数（端口契约）：会话脏半源（编辑器未保存态权威）∪ 磁盘
    // 半源（drafts/<branch>/ 当前有效草稿——listDrafts 实测）。去重保序
    // （会话半源序优先——呈现序归实现方语义，模块身份唯一即可）。
    std::vector<std::string> modules = m_session.unapplied();
    try {
        for (const project::BranchTip& tip : m_store.query().branchTips()) {
            for (const project::DraftInfo& draft : m_store.query().listDrafts(tip.id)) {
                bool seen = false;
                for (const std::string& existing : modules) {
                    if (existing == draft.moduleId) {  // DraftInfo.moduleId＝模块 token
                        seen = true;
                        break;
                    }
                }
                if (!seen) {
                    modules.push_back(draft.moduleId);
                }
            }
        }
    } catch (const std::exception&) {
        // 磁盘半源查询失败（上下文关闭等环境事实）——会话半源已覆盖
        // 编辑器权威，磁盘半源缺失按"不可判"折叠为空增量（端口契约
        // "呈现面不允许单次取数失败炸宿主"——ITitleFactPort 同款纪律）。
    }
    return modules;
}

bool StoreCloseDraftAdapter::saveDrafts()
{
    if (!m_session.save) {
        throw missingPortError("草稿保存动作缝（save）");
    }
    return m_session.save();  // PM-04 落盘（仅 drafts/ 零修订）——false 直通
}

bool StoreCloseDraftAdapter::discardDrafts()
{
    if (!m_session.discard) {
        throw missingPortError("草稿放弃动作缝（discard）");
    }
    return m_session.discard();
}

// =====================================================================
// ExecutionCloseDrainAdapter（v0.7——execution §7.5 真实桥）
// =====================================================================

ExecutionCloseDrainAdapter::ExecutionCloseDrainAdapter(execution::TaskScheduler& scheduler,
                                                       execution::TaskController& controller,
                                                       std::chrono::milliseconds drainBound)
    : m_scheduler(scheduler)
    , m_controller(controller)
    , m_drainBound(drainBound)
{
}

bool ExecutionCloseDrainAdapter::hasActiveTask(core::ProjectId project)
{
    m_project = project;  // 协作取消的项目过滤键（同源取数回填）
    // 非终态判定（九态词表 core::TaskState——D2 判定语义）。
    for (const execution::TaskSnapshot& snap : m_scheduler.tasksByProject(project)) {
        switch (snap.state) {
        case core::TaskState::Queued:
        case core::TaskState::Preparing:
        case core::TaskState::Running:
        case core::TaskState::Paused:
        case core::TaskState::Canceling:
            return true;
        default:
            break;  // 终态（Completed/Canceled/Failed/Interrupted）不在途
        }
    }
    return false;
}

std::vector<core::TaskState> ExecutionCloseDrainAdapter::taskStates(core::ProjectId project)
{
    // 九态投影透传（顺序与去重归实现对端语义——编排核零加工）。
    std::vector<core::TaskState> states;
    for (const execution::TaskSnapshot& snap : m_scheduler.tasksByProject(project)) {
        states.push_back(snap.state);
    }
    return states;
}

bool ExecutionCloseDrainAdapter::drainBounded()
{
    // 有界轮询（无永久等待——execution §7.5；drained()＝无在途运行的
    // 无阻塞快照查询；编排线程只观察不推进）。
    const auto deadline = std::chrono::steady_clock::now() + m_drainBound;
    while (!m_scheduler.drained()) {
        if (std::chrono::steady_clock::now() > deadline) {
            return false;  // 有界超限——编排核转 Failed（不永久阻塞）
        }
        std::this_thread::yield();
    }
    return true;
}

bool ExecutionCloseDrainAdapter::waitDrain()
{
    // §7.5"等待"分支：停止派发＋取消排队＋在途执行至归档完成。
    m_scheduler.shutdown(execution::DrainPolicy::CancelQueuedAndWait);
    return drainBounded();
}

bool ExecutionCloseDrainAdapter::cooperativeCancel()
{
    // §7.5"协作取消"分支：对项目任务清单逐任务 requestCancel（任意线程
    // 命令通道——TaskController 协议；批量入口 requestCancelAll 有主锁
    // 域约束〔F-578〕故不消费），再走同一有界排空。
    for (const execution::TaskSnapshot& snap : m_scheduler.tasksByProject(m_project)) {
        (void)m_controller.requestCancel(snap.taskId);
    }
    m_scheduler.shutdown(execution::DrainPolicy::CancelQueuedAndWait);
    return drainBounded();
}

// =====================================================================
// IoExternalRelinkAdapter（v0.9——双缝）
// =====================================================================

IoExternalRelinkAdapter::IoExternalRelinkAdapter(ProbeFn probe, RelinkFn relink)
    : m_probe(std::move(probe))
    , m_relink(std::move(relink))
{
}

ExternalSourceStatus IoExternalRelinkAdapter::probe(const RelinkRequest& request)
{
    if (!m_probe) {
        throw missingPortError("外部源检测缝（probe——io 桥）");
    }
    return m_probe(request);
}

IExternalRelinkPort::RelinkExecution IoExternalRelinkAdapter::relink(
    const RelinkRequest& request, const ExternalSourceStatus& status)
{
    if (!m_relink) {
        throw missingPortError("外部源重关联提交缝（relink——project ①端口桥）");
    }
    return m_relink(request, status);
}

// =====================================================================
// StoreTitleFactAdapter（v1.0——store 半区真实桥＋会话半区降级）
// =====================================================================

StoreTitleFactAdapter::StoreTitleFactAdapter(const project::ProjectStore& store,
                                             SessionHalf session)
    : m_store(store)
    , m_session(std::move(session))
{
}

TitleFacts StoreTitleFactAdapter::collectFacts() const
{
    TitleFacts facts;
    try {
        // 显示名（project.json 权威元数据——INV-M3）与写权限（INV-SES-1
        // 唯一判定源）真实取数。
        facts.displayName =
            m_store.query().currentMetadata().record.projectDisplayName;
        facts.writable = m_store.writable();

        // 活动分支 label：会话缝优先（活动分支登记权威在会话层——PM-12），
        // 未装配/未登记时退 branchTips 首行兜底（单分支项目的自然形态；
        // 多分支下兜底值不保证是活动分支——宿主装配应注入会话缝）。
        if (m_session.activeBranchLabel) {
            if (auto label = m_session.activeBranchLabel()) {
                facts.schemeLabel = std::move(*label);
            }
        }
        if (facts.schemeLabel.empty()) {
            const std::vector<project::BranchTip> tips = m_store.query().branchTips();
            if (!tips.empty()) {
                facts.schemeLabel = tips.front().label;
            }
        }

        // anyDirty＝会话脏 ∨ 磁盘草稿在（PM-11"数据归 ui/project"两源
        // 合并——本适配器只算磁盘半源实测，会话半源经缝）。
        bool anyDirty = false;
        if (m_session.sessionDirty) {
            if (auto dirty = m_session.sessionDirty()) {
                anyDirty = *dirty;
            }
        }
        if (!anyDirty) {
            for (const project::BranchTip& tip : m_store.query().branchTips()) {
                if (!m_store.query().listDrafts(tip.id).empty()) {
                    anyDirty = true;  // .new/.bak 残留不列入（listDrafts 扫描语义）
                    break;
                }
            }
        }
        facts.anyDirty = anyDirty;

        // 当前性投影＝ui C-7 值搬运（零重算——evidence 权威 PA-1）；
        // 未装配→缺省（status==nullopt 不可判定——P-UI-2 归入过期呈现）。
        if (m_session.currentness) {
            if (auto currentness = m_session.currentness()) {
                facts.resultsCurrentness = std::move(*currentness);
            }
        }
    } catch (const std::exception&) {
        // 环境失败折叠安全缺省（端口契约——标题栏常驻呈现面不允许单次
        // 取数失败炸宿主；已填充字段保留，未填充走缺省）。
    }
    return facts;
}

// =====================================================================
// StoreRecoveryFactAdapter（v1.0——统一诊断目录真实桥）
// =====================================================================

StoreRecoveryFactAdapter::StoreRecoveryFactAdapter(
    const diagnostics::DiagCatalog& catalog,
    std::function<std::optional<bool>()> writable,
    ReportHalf reportHalf)
    : m_catalog(catalog)
    , m_writable(std::move(writable))
    , m_report(std::move(reportHalf))
{
}

RecoveryFacts StoreRecoveryFactAdapter::collectFacts()
{
    RecoveryFacts facts;
    // 会话写权限（store->writable() 同源注入——可空函数＝true 缺省）。
    if (m_writable) {
        if (auto w = m_writable()) {
            facts.writable = *w;
        }
    }
    // 计数事实半区（open 产物——可空＝折叠空清单，见类注降级纪律）。
    std::optional<project::RecoveryReport> report;
    if (m_report.report) {
        report = m_report.report();
    }

    try {
        // 全量快照按稳定码逐字识别三场景（目录为**存在性**权威——横幅
        // 事实只经目录进入本单元，PA-1 不直连 RecoveryReport 判定）。
        const std::vector<diagnostics::DiagProjectionItem> view =
            m_catalog.snapshot(diagnostics::DiagQuery{});
        bool hasIgnored = false;
        bool hasOrphan = false;
        std::size_t interruptedTasks = 0;
        for (const diagnostics::DiagProjectionItem& item : view) {
            if (item.code == "PRJ-RECOVERY-IGNORED-UNCOMMITTED") {
                hasIgnored = true;  // 场景①目录事实
            } else if (item.code == "PRJ-RECOVERY-ORPHAN-DRAFT") {
                hasOrphan = true;   // 场景③目录事实
            } else if (item.code == "EX-TASK-INTERRUPTED") {
                // 中断条目计数＝目录折叠计数合计（每任务一条标注——
                // occurrences 即中断任务条目数；口径≠涉事清单口径——
                // RecoveryScenarioFact 端口契约明文不混用）。
                interruptedTasks += item.occurrences;
            }
        }

        // 仅命中场景入清单（itemCount ≥ 1——无事实不入，端口契约；
        // 计数取对端清单事实长度，不是目录折叠计数）。
        if (hasIgnored && report.has_value()
            && !report->ignoredStagingTxs.empty()) {
            facts.scenarios.push_back(RecoveryScenarioFact{
                RecoveryScenario::IgnoredUnfinishedSave,
                report->ignoredStagingTxs.size()});
        }
        if (interruptedTasks > 0) {
            facts.scenarios.push_back(RecoveryScenarioFact{
                RecoveryScenario::InterruptedTask, interruptedTasks});
        }
        if (hasOrphan && report.has_value() && !report->orphanDraftFiles.empty()) {
            facts.scenarios.push_back(RecoveryScenarioFact{
                RecoveryScenario::OrphanDraft, report->orphanDraftFiles.size()});
        }
    } catch (const std::exception&) {
        // 目录不可达等环境失败折叠空清单（端口契约——横幅缺省＝不渲染，
        // 呈现面不允许单次取数失败炸宿主）。
        facts.scenarios.clear();
    }
    return facts;
}

// =====================================================================
// 委托缝适配器（metric/diff/saveAs/export/import——F-536 拒绝面统一）
// =====================================================================

DelegatingSchemeMetricPort::DelegatingSchemeMetricPort(CollectFn collect)
    : m_collect(std::move(collect))
{
}

std::vector<SchemeMetricFacts> DelegatingSchemeMetricPort::collect(
    const std::vector<core::BranchId>& schemes) const
{
    if (!m_collect) {
        throw missingPortError("方案指标投影缝（collect）");
    }
    return m_collect(schemes);
}

UnavailableComparisonDiffPort::UnavailableComparisonDiffPort(DiffFn diff)
    : m_diff(std::move(diff))
{
}

SchemeDiffFacts UnavailableComparisonDiffPort::diff(const core::BranchId& baseline,
                                                    const core::BranchId& candidate) const
{
    if (!m_diff) {
        // 通道未装配＝端口契约的诚实降级形态（available=false＋entries
        // 必空——编排核补降级警告，不虚构差异，P-OPT-8）。
        SchemeDiffFacts facts;
        facts.available = false;
        (void)baseline;
        (void)candidate;
        return facts;
    }
    return m_diff(baseline, candidate);
}

DelegatingSaveAsPort::DelegatingSaveAsPort(ExecuteFn execute)
    : m_execute(std::move(execute))
{
}

ISaveAsPort::Execution DelegatingSaveAsPort::executeCopy(
    project::ProjectStore& source, const SaveAsRequest& request,
    IFlowCancelToken* cancel, const FlowProgressCallback& progress)
{
    if (!m_execute) {
        throw missingPortError("另存复制执行缝（ISaveAsPort——project 存储侧桥）");
    }
    return m_execute(source, request, cancel, progress);
}

DelegatingPackageExportPort::DelegatingPackageExportPort(ExecuteFn execute)
    : m_execute(std::move(execute))
{
}

IPackageExportPort::Execution DelegatingPackageExportPort::exportPackage(
    project::ProjectStore& source, const PackageExportRequest& request,
    IFlowCancelToken* cancel, const FlowProgressCallback& progress)
{
    if (!m_execute) {
        throw missingPortError("包导出执行缝（IPackageExportPort——io 桥）");
    }
    return m_execute(source, request, cancel, progress);
}

DelegatingPackageImportPort::DelegatingPackageImportPort(ExecuteFn execute)
    : m_execute(std::move(execute))
{
}

PackageImportExecution DelegatingPackageImportPort::importPackage(
    const PackageImportRequest& request, IFlowCancelToken* cancel,
    const FlowProgressCallback& progress)
{
    if (!m_execute) {
        throw missingPortError("包导入执行缝（IPackageImportPort——io/project 桥）");
    }
    return m_execute(request, cancel, progress);
}

// =====================================================================
// WorkflowLifecycleController（六方法＝编排核转发的宿主形态）
// =====================================================================

WorkflowLifecycleController::WorkflowLifecycleController(WorkflowHostPorts ports,
                                                         WorkflowHostBridges bridges)
    : m_ports(ports)
    , m_bridges(std::move(bridges))
{
}

project::ProjectStore& WorkflowLifecycleController::requireCurrentStore()
{
    if (!m_bridges.currentStore) {
        throw missingPortError("会话桥 currentStore");
    }
    project::ProjectStore* store = m_bridges.currentStore();
    if (store == nullptr) {
        // 无项目会话调用需要当前项目的入口＝宿主装配缺陷（入口可用性
        // 裁剪归宿主——PM-10"无项目时禁用"承载在宿主面）。
        throw WorkflowError("宿主装配缺陷：无项目会话下调用了需要当前项目的生命周期入口");
    }
    return *store;
}

void WorkflowLifecycleController::startNewProjectWizard()
{
    if (!m_bridges.collectNewProjectInputs || !m_bridges.activateStore
        || !m_bridges.presentFailure) {
        throw WorkflowError("宿主装配缺陷：新建向导必需缝未装配");
    }
    // 第 1 段：输入收集（宿主面三步向导——确认成功才返回 true；取消＝
    // 零副作用退出〔编排核确认前零写路径——"取消不留半成品"〕）。
    NewProjectInputs inputs;
    if (!m_bridges.collectNewProjectInputs(inputs)) {
        return;  // 用户取消——不是错误（UX-03）
    }
    // 第 2 段：确认编排（步骤③确认动作——存储侧创建＋领域初始化提交
    // ＋失败收尾全在编排核）。
    NewProjectOutcome outcome = NewProjectWizardFlow::commit(
        inputs, m_ports.domainInit, m_bridges.eventBus, m_bridges.diagnosticsSink);
    // 第 3 段：结果分派（成功→会话激活移交；失败→UX-03 三串呈现——
    // 输入保留在宿主收集缝侧供重试）。
    if (outcome.created && outcome.store != nullptr && outcome.projectId.has_value()) {
        m_bridges.activateStore(std::move(outcome.store), *outcome.projectId);
        if (m_bridges.presentNotice) {
            m_bridges.presentNotice("项目已创建：" + inputs.displayName);
        }
        return;
    }
    if (outcome.failure.has_value()) {
        m_bridges.presentFailure(outcome.failure->context, outcome.failure->cause,
                                 outcome.failure->recommendedAction);
    }
}

void WorkflowLifecycleController::openProject(OpenSource source, std::string path)
{
    if (!m_bridges.activateStore || !m_bridges.presentFailure
        || !m_bridges.selectOpenPath || !m_bridges.collectPackageImport) {
        throw WorkflowError("宿主装配缺陷：打开流程必需缝未装配");
    }
    // 第 1 段：路径来源（Dialog 来源由宿主打开对话框给出；空路径＝用户
    // 取消——零副作用退出）。
    std::filesystem::path target(path);
    if (target.empty()) {
        auto selected = m_bridges.selectOpenPath();
        if (!selected.has_value() || selected->empty()) {
            return;  // 取消（非错误——UX-03）
        }
        target = std::move(*selected);
    }
    // 第 2 段：五步协议编排（分流/失败呈现/不动当前项目全在编排核）。
    OpenProjectOutcome outcome = OpenProjectFlow::run(source, target,
                                                      m_bridges.eventBus,
                                                      m_bridges.diagnosticsSink);
    // 第 3 段：结果分派——.rwpack 分流→路由包导入向导（packFile 预填，
    // 宿主收集缝补目标目录）；成功→激活；失败→呈现。
    if (outcome.targetKind == OpenTargetKind::PackageFile) {
        // ASM-UI 修复③（asm-wf 验收建议级①）：分流段**只登记预填、不
        // 收集**——收集动作统一归 startPackageWizard(Import) 恰一次执行。
        // 此前形态＝分流段先 collectPackageImport（预填 packFile）收集
        // 一次、向导段再 collectPackageImport（空请求——预填丢失）收集
        // 第二次：真实宿主对话框场景下用户面对两次输入收集。修复后收
        // 集**恰一次**且预填保留（§7.2 打开协议分流零副作用＋§7.4 向导
        // 一次收集的语义复合——用户意图流"打开 .rwpack→导入向导→进
        // 目标"只含一次向导交互）。
        m_pendingImportPackFile = target;
        startPackageWizard(PackageFlowKind::Import);  // 路由导入向导编排
        return;
    }
    if (outcome.opened && outcome.store != nullptr && outcome.projectId.has_value()) {
        m_bridges.activateStore(std::move(outcome.store), *outcome.projectId);
        if (m_bridges.presentNotice) {
            m_bridges.presentNotice("项目已打开：" + outcome.canonicalPath.string());
        }
        return;
    }
    if (outcome.failure.has_value()) {
        m_bridges.presentFailure(outcome.failure->context, outcome.failure->cause,
                                 outcome.failure->recommendedAction);
    }
}

CloseFlowResult WorkflowLifecycleController::requestClose(CloseKind kind)
{
    if (!m_bridges.currentProjectId || !m_bridges.presentFailure
        || (kind == CloseKind::Switch && !m_bridges.selectCandidateProjectPath)) {
        throw WorkflowError("宿主装配缺陷：关闭流程必需缝未装配");
    }
    project::ProjectStore& store = requireCurrentStore();

    // 候选路径（仅 Switch——切换＝关闭后候选验证成功才切上下文；用户
    // 在候选选择点取消＝切换零发生，返回 Aborted〔两决策点之前的取消
    // ——abortedAt 保持 None：未进入草稿/任务决策点〕）。
    std::filesystem::path candidatePath;
    if (kind == CloseKind::Switch) {
        auto candidate = m_bridges.selectCandidateProjectPath();
        if (!candidate.has_value() || candidate->empty()) {
            CloseFlowOutcome aborted;
            aborted.result = CloseFlowOutcome::Result::Aborted;
            aborted.abortedAt = CloseFlowOutcome::AbortStage::None;
            return aborted;
        }
        candidatePath = std::move(*candidate);
    }

    // 统一确认编排（三选/二选/排空/候选验证/存储关闭——全在编排核；
    // 三端口为装配注入的决策/草稿/排空适配器）。
    CloseFlowRequest request;
    request.projectId = m_bridges.currentProjectId();
    request.candidatePath = candidatePath;
    request.decisions = m_ports.closeDecisions;
    request.drafts = m_ports.closeDrafts;
    request.drain = m_ports.closeDrain;
    CloseFlowOutcome outcome = CloseFlow::run(kind, store, request);

    // 结果分派：Switch 且 Proceed→候选移交激活（切换上下文＝激活候选）；
    // Failed→呈现（Aborted 的取消呈现归宿主决策缝自身——编排零副作用）。
    if (outcome.result == CloseFlowOutcome::Result::Proceed
        && kind == CloseKind::Switch && outcome.candidateStore != nullptr) {
        if (m_bridges.activateStore) {
            // ASM-UI 修复④（asm-wf 验收建议级④）：候选项目身份以**候选
            // store 自身**权威取值，不再复用切换前的会话桥 currentProjectId。
            // 此前形态＝activateStore(候选 store, currentProjectId())——
            // 把旧项目的身份登记给新 store（宿主会话层的激活身份与存储
            // 身份错配：任务过滤键/标题栏投影按错误项目取数）。候选身份
            // 权威＝ProjectStore::projectId()（打开③步已与 project.json/
            // HEAD 校验一致——project.md 身份行），move 前取值（移出后
            // 指针失效）。激活面契约不变量随取值同源成立：激活的 store
            // 与 projectId 恒同一项目。
            const core::ProjectId candidateId = outcome.candidateStore->projectId();
            m_bridges.activateStore(std::move(outcome.candidateStore),
                                    candidateId);
        }
    }
    if (outcome.result == CloseFlowOutcome::Result::Failed
        && outcome.failure.has_value()) {
        m_bridges.presentFailure(outcome.failure->context, outcome.failure->cause,
                                 outcome.failure->recommendedAction);
    }
    return outcome;
}

void WorkflowLifecycleController::startSaveAsWizard()
{
    if (!m_bridges.collectSaveAsRequest || !m_bridges.presentFailure) {
        throw WorkflowError("宿主装配缺陷：另存向导必需缝未装配");
    }
    project::ProjectStore& store = requireCurrentStore();

    // 请求收集（目标目录＋勾选——记忆默认的宿主半区在收集缝内）。
    SaveAsRequest request;
    if (!m_bridges.collectSaveAsRequest(request)) {
        return;  // 取消（非错误）
    }
    // 编排（复制→换新 projectId→按打开协议进入——全在编排核）。
    SaveAsOutcome outcome = SaveAsFlow::run(store, request, *m_ports.saveAs,
                                            nullptr, {}, m_bridges.eventBus,
                                            m_bridges.diagnosticsSink);
    if (outcome.result == SaveAsOutcome::Result::Entered && outcome.store != nullptr
        && outcome.projectId.has_value()) {
        if (m_bridges.activateStore) {
            m_bridges.activateStore(std::move(outcome.store), *outcome.projectId);
        }
        if (m_bridges.presentNotice) {
            m_bridges.presentNotice("另存完成，已进入新项目");
        }
        return;
    }
    if (outcome.result == SaveAsOutcome::Result::Failed
        && outcome.failure.has_value()) {
        m_bridges.presentFailure(outcome.failure->context, outcome.failure->cause,
                                 outcome.failure->recommendedAction);
    }
}

void WorkflowLifecycleController::startPackageWizard(PackageFlowKind kind)
{
    if (!m_bridges.presentFailure) {
        throw WorkflowError("宿主装配缺陷：包向导呈现缝未装配");
    }

    // kind 分派（Draft 签名 kind 参数的编排核路由点——Export/Import 各自
    // 编排核，v0.8 分立承载）。
    if (kind == PackageFlowKind::Export) {
        if (!m_bridges.collectPackageExport) {
            throw WorkflowError("宿主装配缺陷：包导出收集缝未装配");
        }
        project::ProjectStore& store = requireCurrentStore();
        PackageExportRequest request;
        if (!m_bridges.collectPackageExport(request)) {
            return;  // 取消（非错误）
        }
        PackageExportOutcome outcome =
            PackageExportFlow::run(store, request, *m_ports.packageExport);
        if (outcome.result == PackageExportOutcome::Result::Completed) {
            if (m_bridges.presentNotice) {
                m_bridges.presentNotice("包导出完成：" + request.targetFile.string());
            }
            return;
        }
        if (outcome.result == PackageExportOutcome::Result::Failed
            && outcome.failure.has_value()) {
            m_bridges.presentFailure(outcome.failure->context, outcome.failure->cause,
                                     outcome.failure->recommendedAction);
        }
        return;
    }

    // Import：请求收集（packFile 可由打开分流预填——ASM-UI 修复③：预填
    // 自 m_pendingImportPackFile 消费，消费即清空＝一次性承载）→
    // 导入编排（校验/发布/清理端口内折叠）→完成即按打开协议进入目标
    // 目录（PM-05"按打开协议进入"同源语义——复用 openProject 的编排核）。
    // 收集恰一次：无论本方法由打开分流路由而来（预填在位）还是直接入口
    // （无预填），collectPackageImport 在本流程内只被调用一次——用户只
    // 面对一次输入收集。
    if (!m_bridges.collectPackageImport) {
        throw WorkflowError("宿主装配缺陷：包导入收集缝未装配");
    }
    PackageImportRequest request;
    if (m_pendingImportPackFile.has_value()) {
        // 打开分流预填消费（移动取出＋立即清空——会话内不跨流程残留；
        // 用户在收集缝取消后再次直接打开包向导＝全新流程零预填残留）。
        request.packFile = std::move(*m_pendingImportPackFile);
        m_pendingImportPackFile.reset();
    }
    if (!m_bridges.collectPackageImport(request)) {
        return;  // 取消（非错误）
    }
    PackageImportOutcome outcome =
        PackageImportFlow::run(request, *m_ports.packageImport);
    if (outcome.result == PackageImportOutcome::Result::Completed) {
        if (m_bridges.presentNotice) {
            m_bridges.presentNotice("包导入完成：" + request.targetDir.string());
        }
        // 按打开协议进入（Dialog 来源——路径已知，不再弹选择对话框）。
        openProject(OpenSource::Dialog, request.targetDir.string());
        return;
    }
    if (outcome.result == PackageImportOutcome::Result::Failed
        && outcome.failure.has_value()) {
        m_bridges.presentFailure(outcome.failure->context, outcome.failure->cause,
                                 outcome.failure->recommendedAction);
    }
}

void WorkflowLifecycleController::startRelinkFlow(core::ObjectId resource)
{
    if (!m_ports.relinkDecisions || !m_bridges.selectRelinkPath
        || !m_bridges.presentFailure) {
        throw WorkflowError("宿主装配缺陷：重关联流程必需缝未装配");
    }

    // 重定向路径收集：nullopt＝取消；有值且空 path＝保持登记路径重连。
    RelinkRequest request;
    request.resource = resource;
    auto selected = m_bridges.selectRelinkPath();
    if (!selected.has_value()) {
        return;  // 用户取消（非错误——UX-03）
    }
    request.newPath = std::move(*selected);

    // 编排（检测→显式确认〔relinkDecisions 端口〕→显式提交→新修订——
    // 全在编排核；编排核签名零存储上下文＝"失败不动当前项目"）。
    RelinkOutcome outcome =
        RelinkFlow::run(request, *m_ports.externalRelink, *m_ports.relinkDecisions);
    if (outcome.result == RelinkOutcome::Result::Failed
        && outcome.failure.has_value()) {
        m_bridges.presentFailure(outcome.failure->context, outcome.failure->cause,
                                 outcome.failure->recommendedAction);
        return;
    }
    if (m_bridges.presentNotice) {
        switch (outcome.result) {
        case RelinkOutcome::Result::Relinked:
            m_bridges.presentNotice("外部源已重新关联（新修订 "
                                    + outcome.revisionId->toCanonical() + "）");
            break;
        case RelinkOutcome::Result::NotNeeded:
            m_bridges.presentNotice("外部源无缺失无变化，无需重关联");
            break;
        default:
            break;  // Canceled——取消提示归宿主确认缝自身
        }
    }
}

// =====================================================================
// assembleWorkflowLifecycleController（F-536 装配核查）
// =====================================================================

WorkflowLifecycleController assembleWorkflowLifecycleController(
    const WorkflowHostPorts& ports, const WorkflowHostBridges& bridges)
{
    // 端口核查（逐一点名——缺哪台说哪台，修复装配而不是捕获后继续）。
    if (ports.domainInit == nullptr) {
        throw missingPortError("端口 IDomainInitSubmitter");
    }
    if (ports.closeDecisions == nullptr) {
        throw missingPortError("端口 ICloseDecisionPort");
    }
    if (ports.closeDrafts == nullptr) {
        throw missingPortError("端口 ICloseDraftPort");
    }
    if (ports.closeDrain == nullptr) {
        throw missingPortError("端口 ICloseDrainPort");
    }
    if (ports.relinkDecisions == nullptr) {
        throw missingPortError("端口 IRelinkDecisionPort");
    }
    if (ports.externalRelink == nullptr) {
        throw missingPortError("端口 IExternalRelinkPort");
    }
    if (ports.titleFacts == nullptr) {
        throw missingPortError("端口 ITitleFactPort");
    }
    if (ports.recoveryFacts == nullptr) {
        throw missingPortError("端口 IRecoveryFactPort");
    }
    if (ports.schemeMetrics == nullptr) {
        throw missingPortError("端口 ISchemeMetricPort");
    }
    if (ports.comparisonDiff == nullptr) {
        throw missingPortError("端口 IComparisonDiffPort");
    }
    if (ports.saveAs == nullptr) {
        throw missingPortError("端口 ISaveAsPort");
    }
    if (ports.packageExport == nullptr) {
        throw missingPortError("端口 IPackageExportPort");
    }
    if (ports.packageImport == nullptr) {
        throw missingPortError("端口 IPackageImportPort");
    }

    // 必填会话桥核查（呈现缝缺失＝失败静默〔伪造成功面〕；收集缝缺失
    // ＝入口不可用空转——一律装配违约）。
    if (!bridges.currentStore) {
        throw missingPortError("会话桥 currentStore");
    }
    if (!bridges.currentProjectId) {
        throw missingPortError("会话桥 currentProjectId");
    }
    if (!bridges.activateStore) {
        throw missingPortError("会话桥 activateStore");
    }
    if (!bridges.collectNewProjectInputs) {
        throw missingPortError("收集缝 collectNewProjectInputs");
    }
    if (!bridges.selectOpenPath) {
        throw missingPortError("收集缝 selectOpenPath");
    }
    if (!bridges.selectCandidateProjectPath) {
        throw missingPortError("收集缝 selectCandidateProjectPath");
    }
    if (!bridges.collectSaveAsRequest) {
        throw missingPortError("收集缝 collectSaveAsRequest");
    }
    if (!bridges.collectPackageExport) {
        throw missingPortError("收集缝 collectPackageExport");
    }
    if (!bridges.collectPackageImport) {
        throw missingPortError("收集缝 collectPackageImport");
    }
    if (!bridges.selectRelinkPath) {
        throw missingPortError("收集缝 selectRelinkPath");
    }
    if (!bridges.presentFailure) {
        throw missingPortError("呈现缝 presentFailure（失败可见面）");
    }

    return WorkflowLifecycleController(ports, bridges);
}

}  // namespace workflow
}  // namespace ird
}  // namespace sdurws
