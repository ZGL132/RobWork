/**
 * @file   PanelEditFlow.hpp
 * @brief  requirements 面板编辑流（零 Qt）——L-R2 字段编辑流、L-R9 批次
 *         编辑流与两级撤销呈现的数据面（卡 §9.8 界面逻辑表）。
 *
 * 设计依据：
 *   - units/requirements.md §9.8 界面逻辑表（L-R2"字段编辑：控件提交→
 *     applyEdit→接受刷新＋脏标记；拒绝就地原因保留原值——UX-03/05/07"、
 *     L-R4"撤销/重做两级：项目级 UndoRedoService／草稿级编辑器 undoLocal
 *     （REQ-11 需求集撤销＝批量整体回滚——单条 applyEdit 批次一次入栈）"、
 *     L-R9"镜像/阵列/模板：面板发起→precheckDerivation→参数表单→
 *     EditBatch→applyEdit（一次批次一次入栈）→派生条目带溯源徽标"）、
 *     §9.3（IRequirementEditor——域裁决唯一入口）、§7.2（派生溯源）
 *   - 需求 UX-03（就地原因）/UX-05（批量＋单位同显）/UX-07（内联非模态
 *     优先）、REQ-11（需求集批量撤销）；任务契约
 *     tasks/foundation/WP-14-T08.json acceptance 2/4
 *
 * 背景说明：插件零计算逻辑（DTB 禁止项）——本文件对编辑的**一切合法性
 * 判定零参与**：接受/拒绝唯一由编辑器域校验链裁决（applyEdit 拒绝时工作
 * 集字节不变是其域内强保证——Editor.hpp @post）；本文件只负责调用编排与
 * 结果分流（接受→增量刷新＋脏通知；拒绝→就地错误出口）。"不弹模态"由
 * 结构保证：本头没有任何模态呈现面，拒绝一律经 sink 即时回传（UX-07）。
 *
 * 线程约束：仅 UI 线程访问（§3.4——编辑器为会话对象，非线程安全）。
 * 确定性：同输入序列→同调用序→同结果（无环境读取）。
 */

#ifndef IRD_REQUIREMENTS_PLUGIN_PANELEDITFLOW_HPP
#define IRD_REQUIREMENTS_PLUGIN_PANELEDITFLOW_HPP

#include <cstdint>
#include <string>

#include <sdurws/ird/core/DiagData.hpp>                // core::DiagnosticRecord（批次警告承载）
#include <sdurws/ird/requirements/Editor.hpp>          // IRequirementEditor/EditOutcome（域裁决唯一入口）
#include <sdurws/ird/requirements/TemplateArray.hpp>   // EditBatch（L-R9 批次入口值面）
#include "PanelStationModel.hpp"                       // StationFieldRow（只读门控复用行模型——同目录私有头）

namespace sdurws::ird::requirements {

// =====================================================================
// 编辑流出口（IRequirementEditSink——widget 层实现；模型层测试以记录替身实现）
// =====================================================================

/**
 * @brief 就地编辑拒绝明细（L-R2"就地比较型错误"的承载——UX-03）。
 *
 * codeToken＝域错误码稳定 token（requirementErrorCodeToken 产出——
 * "DuplicateName"/"AllDofFree" 等）；detail＝域错误自带细节（RequirementError
 * .detail 直投，零加工——敏感性约束：未经脱敏不得直呈用户，呈现层经
 * diagnostics 脱敏设施，Errors.hpp 契约）。
 */
struct EditRejection {
    std::string codeToken;  ///< 域错误码 token（机器判别串）
    std::string detail;     ///< 域错误细节（原文透传——脱敏归 diagnostics 呈现链）

    bool operator==(const EditRejection& o) const noexcept
    {
        return codeToken == o.codeToken && detail == o.detail;
    }
    bool operator!=(const EditRejection& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 编辑流出口（widget 层实现；满足 L-R2 的三路分流——卡 §9.8 行）。
 *
 * 生命周期/所有权：面板 widget 实现（非 owning 引用传入编辑流函数——
 * 调用期存活由调用方保证）。线程约束：全部回调仅在 UI 线程触发（§3.4）。
 */
class IRequirementEditSink {
public:
    virtual ~IRequirementEditSink() = default;

    /**
     * @brief 编辑接受（L-R2：树/检查器/校验条增量刷新信号——widget 据此
     *        现取工作集重投影，不使用旧副本）。
     *
     * @param changeSummary [in] 本次编辑的人读摘要（EditOutcome.changeSummary
     *                      直投——刷新定位与变更说明素材）
     */
    virtual void onEditApplied(const std::string& changeSummary) = 0;

    /**
     * @brief 会话脏通知（PM-04/PM-11——标题 `*` 标记的生产者接线点；与
     *        ui DraftController.notifySessionDirty 汇合——接线随装配）。
     */
    virtual void notifySessionDirty() = 0;

    /**
     * @brief 编辑拒绝（L-R2：就地错误＋保留原值——非模态；widget 在字段行
     *        原位呈现 reason，值控件回退显示工作集权威值）。
     *
     * @param rejection [in] 拒绝明细（域错误 token＋细节）
     */
    virtual void onEditRejected(const EditRejection& rejection) = 0;

    /**
     * @brief 批次警告知情登记（L-R9：REQ-DERIVE-MIRROR-PENDING 等 warning
     *        级诊断——不阻断应用，应用照常；widget 逐条在状态行登记呈现，
     *        非 modal）。
     *
     * @param warning [in] 批次警告诊断（EditBatch.diagnostics 元素直投）
     */
    virtual void onBatchWarning(const core::DiagnosticRecord& warning) = 0;
};

// =====================================================================
// L-R2 字段编辑流
// =====================================================================

/**
 * @brief 字段编辑流提交结果（L-R2 两态——分流结果供测试/调用方判别）。
 */
enum class EditSubmitOutcome {
    Applied,  ///< 域接受（工作集已更新＋onEditApplied＋notifySessionDirty 已发）
    Rejected  ///< 域拒绝（工作集不变＋onEditRejected 已发——原值保留）
};

/**
 * @brief 提交一次条目编辑（L-R2 唯一入口——§9.8 行"控件提交→applyEdit"
 *        的 requirements 侧落位；条目 upsert/删除/根头编辑统一经
 *        RequirementEdit 值承载）。
 *
 * 编排序（零判定——全部裁决在编辑器域校验链内）：
 *   ① 调 editor.applyEdit（域校验链：条目不变量→集合唯一性→跨集合 id
 *      唯一→删除引用保护——拒绝时工作集字节不变，本函数不重复校验、
 *      不回滚）；
 *   ② accepted==true：sink.onEditApplied（增量刷新）→sink.notifySessionDirty
 *      （脏标记——PM-04/PM-11）；
 *   ③ accepted==false：sink.onEditRejected（就地呈现；无任何模态路径）。
 *
 * @param editor [in,out] 目标编辑器（接受时工作集已更新——仅 UI 线程）
 * @param sink   [in] 编辑流出口（调用期存活——非 owning）
 * @param edit   [in] 编辑差值（值语义——编辑器接管拷贝）
 * @return 分流结果（Applied/Rejected——与 sink 回调一一对应）
 *
 * 非 UI 线程调用＝契约违约（§3.4；调试期经 PanelRefresh 的线程守卫钉住）。
 */
EditSubmitOutcome submitEntryEdit(IRequirementEditor& editor, IRequirementEditSink& sink,
                                  const RequirementEdit& edit);

// =====================================================================
// L-R9 批次编辑流（镜像/阵列/模板——REQ-11 批量整体撤销）
// =====================================================================

/**
 * @brief 提交一个派生编辑批次（L-R9 主干——§9.8 行"EditBatch→applyEdit
 *        （一次批次一次入栈）"的编排；批次原子性在编辑器域内：接受＝
 *        恰一次撤销入栈、一次整体回滚，拒绝＝工作集与栈全部不变）。
 *
 * 编排序（零判定——批次合法性裁决唯一在域函数与编辑器）：
 *   ① batch.ok==false（参数表单的域面拒绝——TemplateArray 服务产出 err
 *      态批次）：sink.onEditRejected（就地——参数修正后重试，UX-03；
 *      不 fail-fast：参数来自用户表单，属正常业务路径）；
 *   ② batch.ok==true → editor.applyEdit(batch)：
 *      - 接受：sink.onEditApplied（批次摘要）＋批次警告诊断
 *        （REQ-DERIVE-MIRROR-PENDING 等 warning——不阻断应用，知情登记）
 *        逐条投递 onBatchWarning；sink.notifySessionDirty；
 *      - 拒绝：sink.onEditRejected（编辑器校验链同源——零数据变更）。
 *
 * @param editor [in,out] 目标编辑器（仅 UI 线程）
 * @param sink   [in] 编辑流出口（同 submitEntryEdit）
 * @param batch  [in] 编辑批次（TemplateArray 服务产出——值语义）
 * @return 分流结果（Applied/Rejected——与 sink 回调对应）
 *
 * 非 UI 线程调用＝契约违约（§3.4）。
 */
EditSubmitOutcome submitBatchEdit(IRequirementEditor& editor, IRequirementEditSink& sink,
                                  const EditBatch& batch);

// =====================================================================
// L-R4 两级撤销呈现（草稿级 undoLocal／项目级 UndoRedoService 分离呈现）
// =====================================================================

/**
 * @brief 两级撤销的呈现模型（L-R4"两级撤销/重做分离呈现"的数据面——
 *        两个独立动作位＋互不混用的标签词表）。
 *
 * 分离纪律（结构保证）：草稿级动作直接调 IRequirementEditor::undoLocal/
 * redoLocal（零修订——编辑器局部撤销栈）；项目级动作是**转发面**（经
 * 装配注入的提交出口转发到 ui 项目撤销命令——UndoRedoService 语义归
 * project，本单元不复制不代理其判定）。本结构只承载两行呈现事实，两个
 * 动作在面板上是两处独立控件（不合并为一个"撤销"——避免语义混用）。
 */
struct TwoLevelUndoView {
    bool canUndoLocal = false;   ///< 草稿级可撤销（编辑器撤销栈非空）
    bool canRedoLocal = false;   ///< 草稿级可重做（重做栈非空）
    std::uint64_t editCount = 0; ///< 已应用编辑数（draftStatus 直投——差值语义）
    std::string draftUndoLabel;  ///< 草稿级动作标签（"撤销本次编辑（草稿级）"——固定词）
    std::string projectUndoLabel;///< 项目级动作标签（"撤销上次应用（项目级）"——固定词）
};

/**
 * @brief 草稿级撤销深度跟踪器（canUndo/canRedo 的精确事实源——L-R4 呈现
 *        半区；widget 在编辑/撤销动作后维护，仅 UI 线程）。
 *
 * 为什么需要跟踪：编辑器公开面只有动作轨（undoLocal/redoLocal 返回是否
 * 成功）与差值计数（edits——撤销/重做不改变），无法只读查询栈深。本跟踪器
 * 以"入栈次数−净撤销深度"记账出两个可位（与编辑器栈操作一一对应——
 * submitEntryEdit/submitBatchEdit 的 Applied 分支 recordAppliedEdit；
 * undo/redo 包裹编辑器动作并记账）。loadBaseline（重载基线）后由 widget
 * 调 reset 归零（栈随基线重置——Editor.hpp @post）。
 *
 * 线程约束：仅 UI 线程（§3.4——与编辑器同一会话对象纪律）。非线程安全。
 */
class LocalUndoTracker {
public:
    LocalUndoTracker() = default;
    LocalUndoTracker(const LocalUndoTracker&) = delete;
    LocalUndoTracker& operator=(const LocalUndoTracker&) = delete;

    /// @brief 编辑接受后记账（编辑器撤销栈入栈一步——Applied 分支调用）。
    void recordAppliedEdit() noexcept { m_applies += 1; }

    /// @brief 草稿级撤销一步（包裹 editor.undoLocal；成功→净撤销深度 +1）。
    bool undo(IRequirementEditor& editor)
    {
        if (!editor.undoLocal()) {
            return false;  // 栈空——编辑器域内保证零变更，记账不动
        }
        m_undoDepth += 1;
        return true;
    }

    /// @brief 草稿级重做一步（包裹 editor.redoLocal；成功→净撤销深度 −1）。
    bool redo(IRequirementEditor& editor)
    {
        if (!editor.redoLocal()) {
            return false;  // 重做栈空——零变更
        }
        m_undoDepth -= 1;
        return true;
    }

    /// @brief 重载基线后归零（栈随基线重置——widget 在 loadBaseline 成功后调用）。
    void reset() noexcept
    {
        m_applies = 0;
        m_undoDepth = 0;
    }

    /// @brief 可撤销（入栈次数−净撤销深度>0——栈非空的记账事实）。
    bool canUndo() const noexcept { return m_applies > m_undoDepth; }
    /// @brief 可重做（净撤销深度>0——重做栈非空的记账事实）。
    bool canRedo() const noexcept { return m_undoDepth > 0; }

private:
    std::uint64_t m_applies = 0;    ///< 自基线以来的入栈次数（applyEdit 接受计数）
    std::uint64_t m_undoDepth = 0;  ///< 净撤销深度（撤销−重做——重做栈深度的事实镜像）
};

/**
 * @brief 投影两级撤销呈现（L-R4——从编辑器状态与撤销跟踪器现取，零缓存）。
 *
 * @param editor  [in] 需求编辑器（已载入基线；仅 UI 线程）
 * @param tracker [in] 草稿级撤销跟踪器（widget 随编辑/撤销动作维护——
 *                canUndo/canRedo 的精确事实源；draftStatus 的差值计数推不
 *                出栈态——"撤销/重做不改变 edits 计数"，Editor.hpp 差值语义）
 * @return 呈现模型（确定性）
 *
 * 纯函数（对入参只读）；确定性；不抛。
 */
TwoLevelUndoView twoLevelUndoView(const IRequirementEditor& editor,
                                  const LocalUndoTracker& tracker);

}  // namespace sdurws::ird::requirements

#endif  // IRD_REQUIREMENTS_PLUGIN_PANELEDITFLOW_HPP
