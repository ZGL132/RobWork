/**
 * @file   Commands.cpp
 * @brief  命令集编排核实现（WP-22-T12）——词表/分流/报告导出编排与
 *         五类命令的统一执行编排（设计依据见 Commands.hpp 文件头）。
 *
 * 实现口径：
 *   - 词表与分流表同源（workflowCommandIds() 为唯一行集，kindOfCommand
 *     逐 id 查表——两处不一致由测试词表用例钉住，NFR-MNT-03 单点）；
 *   - 分流执行零跨类副作用：每类命令只触达自己的端口（P-WF-3 碰撞分支
 *     的最小语义、KIN-06 会话分支的零修订，都是**结构性**保证——不依赖
 *     端口实现方自觉）；
 *   - 对端结果零加工透传（D-WF-7 零归码——R1 零新增 WF- 稳定码的持续
 *     兑现：失败呈现走文案键＋对端状态词，本单元不登记任何新稳定码）。
 *
 * 线程安全：无共享可变状态；词表为函数内静态常量（首次调用初始化——
 * C++11 魔法静态，并发安全）。
 */

#include <sdurws/ird/workflow/Commands.hpp>

#include <utility>

namespace sdurws {
namespace ird {
namespace workflow {

// =====================================================================
// 命令 id 词表（行序＝ui.md §7.1 冻结表行序——登记序稳定排序锚）
// =====================================================================

const std::vector<std::string>& workflowCommandIds()
{
    // 函数内静态＝初始化恰一次（并发只读安全）；行序与单元卡 §8.3
    // "最小命令集＋工业高频"清单逐条对应，新增命令必须在此增行并同步
    // kindOfCommand 分流表与插件侧描述符行（三处一体的词表纪律）。
    static const std::vector<std::string> kIds = {
        kCmdProjectNew,        // 新建项目（最小清单）
        kCmdProjectOpen,       // 打开项目（最小清单）
        kCmdDraftSave,         // 保存草稿（最小清单）
        kCmdDraftApply,        // 应用修改（最小清单——修订类）
        kCmdProjectUndo,       // 撤销（最小清单——修订类）
        kCmdProjectRedo,       // 重做（最小清单——修订类）
        kCmdSchemeSwitch,      // 切换方案（最小清单——会话类零写入）
        kCmdProjectSaveAs,     // 另存为（最小清单）
        kCmdPackageExport,     // 包导出（最小清单）
        kCmdReportExport,      // 报告导出（最小清单——编排 reporting 服务）
        kCmdCollisionCheck,    // 运行碰撞检查（工业高频——P-WF-3 最小语义）
        kCmdViewDisplayMode,   // 切换显示模式（工业高频——会话类）
        kCmdViewResetHome,     // 复位关节至 Home（工业高频——KIN-06 零修订）
        kCmdViewResetZero,     // 复位关节至 Zero（工业高频——KIN-06 零修订）
    };
    return kIds;
}

WorkflowCommandKind kindOfCommand(const std::string& commandId)
{
    // 逐 id 查表（14 条封闭集——词表外 id 属调用方/对端契约违约，fail-fast
    // 不静默：静默返回会让"命令面板可达但提交无效"的缺陷逃过装配期自检）。
    if (commandId == kCmdDraftApply) {
        return WorkflowCommandKind::Revision;      // 应用修改→①端口
    }
    if (commandId == kCmdProjectUndo) {
        return WorkflowCommandKind::Revision;      // 撤销→①端口（逆命令）
    }
    if (commandId == kCmdProjectRedo) {
        return WorkflowCommandKind::Revision;      // 重做→①端口（重放载荷）
    }
    if (commandId == kCmdProjectNew) {
        return WorkflowCommandKind::LifecycleFlow; // 新建→向导触发
    }
    if (commandId == kCmdProjectOpen) {
        return WorkflowCommandKind::LifecycleFlow; // 打开→对话框入口触发
    }
    if (commandId == kCmdProjectSaveAs) {
        return WorkflowCommandKind::LifecycleFlow; // 另存为→向导触发
    }
    if (commandId == kCmdPackageExport) {
        return WorkflowCommandKind::LifecycleFlow; // 包导出→向导触发
    }
    if (commandId == kCmdDraftSave) {
        return WorkflowCommandKind::SessionAction; // 保存草稿→会话端口（草稿写轨非修订）
    }
    if (commandId == kCmdSchemeSwitch) {
        return WorkflowCommandKind::SessionAction; // 切换方案→会话端口（PM-12 零写入）
    }
    if (commandId == kCmdViewDisplayMode) {
        return WorkflowCommandKind::SessionAction; // 显示模式→会话端口（纯呈现偏好）
    }
    if (commandId == kCmdViewResetHome) {
        return WorkflowCommandKind::SessionAction; // 复位 Home→会话端口（KIN-06 会话姿态）
    }
    if (commandId == kCmdViewResetZero) {
        return WorkflowCommandKind::SessionAction; // 复位 Zero→会话端口（KIN-06 会话姿态）
    }
    if (commandId == kCmdCollisionCheck) {
        return WorkflowCommandKind::CollisionCheck;// 碰撞检查→P-WF-3 会话级最小语义
    }
    if (commandId == kCmdReportExport) {
        return WorkflowCommandKind::ReportExport;  // 报告导出→reporting 服务编排
    }

    // 词表外：抛调用方契约违约（异常消息含 id 便于装配期定位——人读
    // 诊断细节，不进用户可见面 UX-02）。
    throw WorkflowError("kindOfCommand: 未登记的命令 id \"" + commandId
                        + "\"（词表见 workflowCommandIds——两卡同步义务）");
}

// =====================================================================
// 报告导出编排核（⑤类——RPT-02 编排 reporting 导出服务）
// =====================================================================

ReportExportCommandOutcome runReportExportCommand(IReportExportDialogPort& dialog,
                                                  reporting::IReportExportService& service)
{
    // 第一步：导出参数收集（宿主对话框——D-WF-6）。用户取消＝正常业务
    // 分支（UX-03 取消非错误）：直接 Canceled 返回且**不触达**导出服务
    // （零副作用——选填参数半途放弃不留任何请求痕迹，测试钉扎）。
    const ReportExportDialogResult collected = dialog.collectExportInputs();
    if (!collected.confirmed) {
        ReportExportCommandOutcome canceled;
        canceled.status = ReportExportCommandOutcome::Status::Canceled;
        canceled.previewHostAvailable = false;  // P-UI-9 缺位如实（恒 false）
        canceled.reasonKey = "cmd.report.export.canceled";
        return canceled;
    }

    // 第二步：请求组装（ReportExportInputs→ReportExportRequest 逐字段
    // 搬运，零加工零补造——项目/报告身份由对话框端口从会话上下文取数，
    // 格式合法性〔≥1 含 Html 无重复〕的终审归 reporting 服务请求校验步，
    // 本核不重复实现 PA-1）。
    reporting::ReportExportRequest request;
    request.project = collected.inputs.project;
    request.reportId = collected.inputs.reportId;
    request.formats = collected.inputs.formats;
    request.destination = collected.inputs.destination;
    request.withEvidenceBundle = collected.inputs.withEvidenceBundle;

    // 第三步：服务编排（渲染→二次渲染确定性比对→一致性→发布/写出——
    // 导出实现归 reporting 零重实现）。取消＝五成员全空结果（reporting
    // 落位偏差③形态）→Canceled；失败＝error 在场→token(code) 零加工
    // 透传（stableCode）＋呈现键。
    const reporting::ReportExportResult result = service.exportReport(request);
    ReportExportCommandOutcome outcome;
    outcome.previewHostAvailable = false;  // P-WF-4/P-UI-9：预览宿主缺位——如实呈现，不虚构预览通道
    if (result.error.has_value()) {
        // 失败终态：对端稳定错误词透传（"reporting/..." 词形——D-WF-7
        // 零归码，R1 零新增 WF- 码）；呈现键指向可重试建议（导出失败
        // 保留选择与路径可重试——reporting §7.4 语义）。
        outcome.status = ReportExportCommandOutcome::Status::Failed;
        outcome.stableCode = std::string(reporting::token(result.error->code()));
        outcome.reasonKey = "cmd.report.export.failed";
        return outcome;
    }
    // 取消（published/files/bundleDigest 全空且无 error）与成功（任一在
    // 场）的判别——取消态五成员全空是 reporting 的既定形态约定。
    const bool canceledAtCheckpoint =
        !result.published.has_value() && result.files.empty()
        && !result.bundleDigest.has_value();
    if (canceledAtCheckpoint) {
        outcome.status = ReportExportCommandOutcome::Status::Canceled;
        outcome.reasonKey = "cmd.report.export.canceled";
        return outcome;
    }
    outcome.status = ReportExportCommandOutcome::Status::Completed;
    outcome.reasonKey = "cmd.report.export.completed";
    return outcome;
}

// =====================================================================
// 命令编排核（分流表——每类命令只触达自己的端口）
// =====================================================================

namespace {

/// 修订结果折叠（RevisionOutcome→WorkflowCommandOutcome——committed 位
/// 即 accepted 位；呈现键直通；stableCode 不上抛：机器半区归宿主诊断
/// 面，用户呈现只走文案键）。
WorkflowCommandOutcome foldRevision(const RevisionOutcome& revision)
{
    WorkflowCommandOutcome out;
    out.accepted = revision.committed;
    out.messageKey = revision.reasonKey;
    return out;
}

/// 会话动作结果折叠（同构直转——UX-02 键值分工：键进结果、值归文案表）。
WorkflowCommandOutcome foldSession(const SessionActionResult& action)
{
    WorkflowCommandOutcome out;
    out.accepted = action.accepted;
    out.messageKey = action.reasonKey;
    return out;
}

}  // namespace

WorkflowCommandOutcome runWorkflowCommand(const std::string& commandId,
                                          const WorkflowCommandPorts& ports)
{
    // 第一步：判类（词表外 id 在此 fail-fast——分流表是词表的第二道
    // 一致性防线）。
    const WorkflowCommandKind kind = kindOfCommand(commandId);

    switch (kind) {
    case WorkflowCommandKind::Revision: {
        // ①类：产生修订的命令一律经修订端口（①命令端口——ARCH §7.11）。
        // 端口空＝装配违约（L5 必须接齐——fail-fast 不静默）。
        if (ports.revisions == nullptr) {
            throw WorkflowError("runWorkflowCommand: 命令 \"" + commandId
                                + "\" 需要修订端口（IRevisionCommandPort）——装配违约");
        }
        if (commandId == kCmdDraftApply) {
            return foldRevision(ports.revisions->applyDraft());
        }
        if (commandId == kCmdProjectUndo) {
            return foldRevision(ports.revisions->undo());
        }
        return foldRevision(ports.revisions->redo());  // kCmdProjectRedo——判类已保证
    }
    case WorkflowCommandKind::LifecycleFlow: {
        // ②类：触发生命周期向导（触发即返回——流程完成态经会话事件回
        // 投影面；命令处理器零阻塞，UI 线程纪律）。
        if (ports.lifecycle == nullptr) {
            throw WorkflowError("runWorkflowCommand: 命令 \"" + commandId
                                + "\" 需要流程启动端口（ILifecycleFlowLauncher）——装配违约");
        }
        if (commandId == kCmdProjectNew) {
            ports.lifecycle->launchNewProjectWizard();
        } else if (commandId == kCmdProjectOpen) {
            ports.lifecycle->launchOpenProjectDialog();
        } else if (commandId == kCmdProjectSaveAs) {
            ports.lifecycle->launchSaveAsWizard();
        } else {
            ports.lifecycle->launchPackageExportWizard();  // kCmdPackageExport
        }
        WorkflowCommandOutcome triggered;
        triggered.accepted = true;  // 触发即受理（向导内部取消是流程 own 态，非命令失败）
        return triggered;
    }
    case WorkflowCommandKind::SessionAction: {
        // ③类：会话动作——**结构性零修订**：本分支只触达会话端口，修订
        // 端口零调用（KIN-06/PM-12；acceptance 2 的编排面保证，契约测试
        // WF-VER-223 以真实 store 复核零修订）。
        if (ports.session == nullptr) {
            throw WorkflowError("runWorkflowCommand: 命令 \"" + commandId
                                + "\" 需要会话端口（IWorkflowSessionPort）——装配违约");
        }
        if (commandId == kCmdDraftSave) {
            return foldSession(ports.session->saveAllDrafts());
        }
        if (commandId == kCmdSchemeSwitch) {
            return foldSession(ports.session->switchSchemeBranch());
        }
        if (commandId == kCmdViewDisplayMode) {
            return foldSession(ports.session->cycleDisplayMode());
        }
        if (commandId == kCmdViewResetHome) {
            return foldSession(ports.session->resetJointsToHome());
        }
        return foldSession(ports.session->resetJointsToZero());  // kCmdViewResetZero
    }
    case WorkflowCommandKind::CollisionCheck: {
        // ④类：碰撞检查——P-WF-3 会话级最小语义：只触达碰撞端口（修订
        // 端口/报告服务零调用——不产生修订、不产生正式证据、不进报告
        // 正式章节；三方契约裁决前不得扩大的结构性保证）。
        if (ports.collision == nullptr) {
            throw WorkflowError("runWorkflowCommand: 命令 \"" + commandId
                                + "\" 需要碰撞检查端口（ICollisionCheckPort）——装配违约");
        }
        const CollisionCheckOutcome check = ports.collision->startSessionCheck();
        WorkflowCommandOutcome out;
        out.accepted = check.accepted;
        out.messageKey = check.reasonKey;
        return out;
    }
    case WorkflowCommandKind::ReportExport: {
        // ⑤类：报告导出——参数收集端口＋导出服务双端口（两端口任一空
        // ＝装配违约；服务为 reporting 公共契约——白名单边消费）。
        if (ports.reportDialog == nullptr || ports.reportExport == nullptr) {
            throw WorkflowError("runWorkflowCommand: 命令 \"" + commandId
                                + "\" 需要导出参数收集端口（IReportExportDialogPort）与"
                                  " reporting 导出服务（IReportExportService）——装配违约");
        }
        const ReportExportCommandOutcome exported =
            runReportExportCommand(*ports.reportDialog, *ports.reportExport);
        WorkflowCommandOutcome out;
        out.accepted = exported.status == ReportExportCommandOutcome::Status::Completed;
        out.messageKey = exported.reasonKey;
        return out;
    }
    }

    // 封闭枚举不可达（五值全 case）——防御性兜底保持返回值完整（某些
    // 编译器对无 return 路径告警）。
    throw WorkflowError("runWorkflowCommand: 未处理的分流类别（词表封闭性破坏）");
}

}  // namespace workflow
}  // namespace ird
}  // namespace sdurws
