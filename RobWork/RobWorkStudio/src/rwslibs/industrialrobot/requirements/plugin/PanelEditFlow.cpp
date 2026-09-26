/**
 * @file   PanelEditFlow.cpp
 * @brief  requirements 面板编辑流实现——L-R2/L-R9 编排与两级撤销呈现。
 *
 * 设计依据：units/requirements.md §9.8（L-R2/L-R4/L-R9）、§9.3（编辑器域
 * 裁决契约）；契约 WP-14-T08 acceptance 2/4。实现纪律：本文件对编辑合法性
 * 零判定（域校验链唯一裁决）；结果分流即全部逻辑。
 */

#include "PanelEditFlow.hpp"

namespace sdurws::ird::requirements {

EditSubmitOutcome submitEntryEdit(IRequirementEditor& editor, IRequirementEditSink& sink,
                                  const RequirementEdit& edit)
{
    // ①域裁决唯一入口（applyEdit 校验链——拒绝时工作集字节不变是其域内
    //   强保证，本函数不重复校验、不回滚）。
    const EditOutcome outcome = editor.applyEdit(edit);

    // ②接受分支：增量刷新＋脏标记（L-R2"接受刷新＋脏标记"——PM-04）。
    if (outcome.accepted) {
        sink.onEditApplied(outcome.changeSummary);
        sink.notifySessionDirty();
        return EditSubmitOutcome::Applied;
    }

    // ③拒绝分支：就地错误＋保留原值（非模态——UX-03/07；错误 token 由域
    //   错误码词表产出，detail 原文透传——脱敏归呈现链）。
    sink.onEditRejected(
        EditRejection{std::string(requirementErrorCodeToken(outcome.error.code)),
                      outcome.error.detail});
    return EditSubmitOutcome::Rejected;
}

EditSubmitOutcome submitBatchEdit(IRequirementEditor& editor, IRequirementEditSink& sink,
                                  const EditBatch& batch)
{
    // ①批次参数面拒绝（TemplateArray 服务产出 err 态批次——参数来自用户
    //   表单，属正常业务路径：就地呈现修正后重试，UX-03；零数据变更由
    //   "批次未进编辑器"结构保证）。
    if (!batch.ok) {
        sink.onEditRejected(
            EditRejection{std::string(requirementErrorCodeToken(batch.error.code)),
                          batch.error.detail});
        return EditSubmitOutcome::Rejected;
    }

    // ②编辑器批量入口（一次批次一次入栈——REQ-11 批量整体回滚；拒绝＝
    //   工作集与栈全部不变，不落半批）。
    const EditOutcome outcome = editor.applyEdit(batch);
    if (!outcome.accepted) {
        sink.onEditRejected(
            EditRejection{std::string(requirementErrorCodeToken(outcome.error.code)),
                          outcome.error.detail});
        return EditSubmitOutcome::Rejected;
    }

    // ③接受分支：批次警告逐条知情登记（warning 不阻断——镜像待人工处理/
    //   重生成冲突等，应用照常；诊断记录直投呈现链）→ 增量刷新＋脏标记。
    for (const core::DiagnosticRecord& warning : outcome.diagnostics) {
        sink.onBatchWarning(warning);
    }
    sink.onEditApplied(outcome.changeSummary);
    sink.notifySessionDirty();
    return EditSubmitOutcome::Applied;
}

TwoLevelUndoView twoLevelUndoView(const IRequirementEditor& editor,
                                  const LocalUndoTracker& tracker)
{
    // 全部事实现取（draftStatus＝{dirty, baseRevisionId, edits}——编辑器
    // 状态的直投投影；撤销可位来自跟踪器记账——draftStatus 的差值计数推
    // 不出栈态，见 LocalUndoTracker 类注）。本函数无缓存成员。
    const RequirementDraftStatus status = editor.draftStatus();
    TwoLevelUndoView view;
    view.canUndoLocal = tracker.canUndo();  // 草稿级可撤销（记账事实）
    view.canRedoLocal = tracker.canRedo();  // 草稿级可重做（记账事实）
    view.editCount = status.edits;
    // 固定标签词（两级动作在面板上是两处独立控件——不合并，防语义混用）。
    view.draftUndoLabel = "撤销本次编辑（草稿级）";
    view.projectUndoLabel = "撤销上次应用（项目级）";
    return view;
}

}  // namespace sdurws::ird::requirements
