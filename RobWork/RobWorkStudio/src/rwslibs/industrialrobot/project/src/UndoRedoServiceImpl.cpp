/**
 * @file   UndoRedoServiceImpl.cpp
 * @brief  撤销/重做服务实现——UndoRedoServiceImpl.hpp 声明的落地：可用性
 *         推导（tip 逆命令磁盘推导／会话 redo 栈）、信封组装与命令端口
 *         提交编排（PRJ-T13——§12 行产物 `UndoRedoServiceImpl.*`）。
 *
 * 设计依据：units/project.md §5.5/§6.9/§6.2/§6.5（语义权威——逐方法出处
 *   见 UndoRedoServiceImpl.hpp 文件头与本文件各实现段注释）；需求
 *   PM-18/PA-2/D-11；契约 tasks/foundation/PRJ-T13.json acceptance 1～4。
 *
 * 实现口径登记（DTB §5.4，UndoRedoServiceImpl.hpp 文件头三项的落地位置）：
 *   ①惰性同步——syncOnEntry（本文件第 1 段）；
 *   ②redo 条目数据源＝撤销修订 inverse（undo 的 Committed 后段）；
 *   ③status() 稳定降级与 blockedReason 一次性旗标（status 实现段）。
 */

#include "UndoRedoServiceImpl.hpp"

#include <stdexcept>
#include <utility>

#include <sdurws/ird/project/QueryPort.hpp>

#include "ProjectStoreImpl.hpp"

namespace sdurws::ird::project {

namespace {

/**
 * @brief 定位分支 tip 并读取修订视图（undo/redo 共用的前置解析段）。
 *
 * 执行序：遍历权威分支表（INV-M3——tip 一律取自 HEAD 引用版本的分支表，
 * 禁经历史修订元数据重建）→命中后读修订视图（revision 强语义轨：闭包
 * 内必可取，取不到属数据侧异常如实抛出）。
 *
 * @param query  [in] 查询端口（宿主 query() 交付的②端口）
 * @param branch [in] 目标分支
 * @return tip 修订视图（值快照——PA-3）
 *
 * @throws std::invalid_argument 分支不在权威分支表（undo/redo 的可用性
 *         前置违约——调用方未先经 status() 判定；§5.2 branchHistory
 *         未知分支同口径 fail-fast）
 * @throws StoreError ContextClosed／StoreCorrupt（查询拒绝态/数据侧——
 *         由调用方 mapQueryFailure 映射为 CommandResult 终态）
 *
 * 线程约束：仅 undo/redo 调用（查询端口自身并发安全——§4.7）。
 */
RevisionView resolveTipViewOrThrow(const IProjectQueryPort& query,
                                   const core::BranchId& branch)
{
    // 权威分支表线性扫描（分支数个位量级——方案分支不是海量集合；命中
    // 即止，未命中＝未知分支前置违约）。
    std::optional<core::RevisionId> tip;
    for (const BranchTip& entry : query.branchTips()) {
        if (entry.id == branch) {
            tip = entry.tip;
            break;
        }
    }
    if (!tip.has_value()) {
        throw std::invalid_argument(
            "project/undoredo: branch not in authoritative branch table"
            "（undo/redo 前置违约——先 status() 判定可用性，§5.5/§6.9） branch="
            + branch.toCanonical());
    }
    // tip 的修订视图（revision 强轨——调用方已断言可撤销/可重做语义，
    // 取不到即数据侧异常而非"没有"；映射面见 mapQueryFailure）。
    return query.revision(*tip);
}

}  // namespace

// =====================================================================
// 构造与端口契约
// =====================================================================

UndoRedoServiceImpl::UndoRedoServiceImpl(ProjectStoreImpl& host) noexcept
    : m_host(host)
{
}

UndoRedoServiceImpl::~UndoRedoServiceImpl() = default;

// =====================================================================
// 惰性同步（实现口径①——§6.9"新命令清空 redo"的检测落点）
// =====================================================================

void UndoRedoServiceImpl::syncOnEntry(BranchUndoState& state,
                                      const core::RevisionId& tip)
{
    // 观察点比对：本服务上次产出/观察的 tip 与权威分支表当前 tip 不符
    // ＝分支上发生了本服务之外的提交（同会话直接 submit 或跨上下文变更
    // ——§6.9 行 4"提交任何新命令（含 undo 之外的）→ redo 栈清空"）。
    // 首次观察（lastServiceTip 为空）只建立基线——空栈无可清空，也无从
    // 谈"被新命令清掉"（stale 语义不成立）。
    if (state.lastServiceTip.has_value() && *state.lastServiceTip != tip) {
        if (!state.redoStack.empty()) {
            // §6.9 标准语义：redo 历史随新命令作废（修订本身不受影响——
            // 历史只增，作废的只是"会话内的重做路径"）。
            state.redoStack.clear();
            // 置一次性说明旗标（§6.9 行 6"过期基线→blockedReason=
            // stale-revision"）：下一次 status() 以机器可读面解释 redo
            // 为何消失，随后回到平凡空状态（旗标消费即清除——轮询面
            // 不重复呈现同一事实）。
            state.redoClearedStale = true;
        }
    }
    state.lastServiceTip = tip;
}

// =====================================================================
// 信封组装（§6.5——project 只拷贝三元组，不构造业务逆命令）
// =====================================================================

CommandEnvelope UndoRedoServiceImpl::buildUndoEnvelope(
    const core::BranchId& branch, const RevisionView& tipView)
{
    // 前置：调用方已判定 inverse 存在（canUndo 推导面）；此处解引用是
    // 该判定的直接消费，非防御性空假设。
    const InverseRef& inverse = *tipView.inverse;
    CommandEnvelope envelope;
    envelope.branch = branch;
    // expectedRevision＝tip（§5.5 原文）：并发竞争的裁决交给命令端口
    // S2（§6.2）——本服务不做第二套基线校验。
    envelope.expectedRevision = tipView.id;
    envelope.commandType = inverse.commandType;
    envelope.payloadFormatVersion = inverse.payloadFormatVersion;
    // 磁盘契约（std::string）→信封（字节向量）的值拷贝：载荷域所有、
    // 原样透传（D-10）——不解释、不改写、不校验内容。
    envelope.payloadCanonical.assign(inverse.payloadCanonical.begin(),
                                     inverse.payloadCanonical.end());
    return envelope;
}

CommandEnvelope UndoRedoServiceImpl::buildRedoEnvelope(
    const core::BranchId& branch, const InverseRecord& record,
    const core::RevisionId& tip)
{
    CommandEnvelope envelope;
    envelope.branch = branch;
    // expectedRevision＝当前 tip（调用方已校验 tip==record.undoRevision
    // ——记录入栈后分支无新提交，重做落在撤销产生的那个修订上）。
    envelope.expectedRevision = tip;
    envelope.commandType = record.redoCommandType;
    envelope.payloadFormatVersion = record.redoPayloadFormatVersion;
    envelope.payloadCanonical = record.redoPayloadCanonical;
    return envelope;
}

// =====================================================================
// 失败映射与诊断通道
// =====================================================================

CommandResult UndoRedoServiceImpl::mapQueryFailure(const StoreError& e) const
{
    // 与命令端口 submit 的 StoreError 映射同表（CommandServiceImpl 实现
    // 口径②）：ContextClosed＝执行中被关闭，属"中止"而非"拒绝"（§6.7
    // 排空语义）；其余环境/数据侧码＝Failed 透传（不吞不改——AGENTS
    // §3 错误纪律）。全部映射零修订。
    CommandResult result;
    if (e.code() == StoreErrorCode::ContextClosed) {
        result.status.kind = CommandStatus::Kind::Aborted;
        result.status.abort = CommandStatus::Abort::ContextClosing;
        result.error = e;
        return result;
    }
    result.status.kind = CommandStatus::Kind::Failed;
    result.error = e;
    return result;
}

IDiagnosticsSink* UndoRedoServiceImpl::devSink() const noexcept
{
    // 宿主的窄访问器（friend 面——与 QueryPortImpl::queryDevSink 同款
    // 先例）；可空＝装配未注入，诊断丢弃（§5.0 约定）。
    return m_host.undoredoDevSink();
}

// =====================================================================
// status——可用性推导与稳定提示数据（noexcept）
// =====================================================================

UndoRedoStatus UndoRedoServiceImpl::status(core::BranchId branch) const noexcept
{
    // noexcept 契约下的总兜底（实现口径③）：查询拒绝态（Closed）、数据
    // 侧异常乃至分配失败一律降级为"稳定空状态"——全 false＋空摘要。真
    // 实错误面由 undo/redo 的返回值承载（二者非 noexcept，如实透传）。
    try {
        // ---- 定位分支 tip（权威分支表——INV-M3）。分支不存在＝稳定空
        //      状态（§5.5 noexcept 契约下不作为调用方违约抛出——轮询面
        //      对任意输入都要有确定输出）。
        std::optional<core::RevisionId> tip;
        for (const BranchTip& entry : m_host.query().branchTips()) {
            if (entry.id == branch) {
                tip = entry.tip;
                break;
            }
        }
        if (!tip.has_value()) {
            return UndoRedoStatus{};
        }

        // ---- 惰性同步（实现口径①）：锁内比对观察点、必要时清空 redo
        //      栈并置 stale 旗标。窗口期说明：此后到视图读取之间的新提
        //      交不影响本次结果的正确性（undo 提交时 S2 兜底），只可能
        //      让本次 status 略微滞后——轮询面下一次调用即收敛。
        {
            std::lock_guard<std::mutex> guard(m_mutex);
            syncOnEntry(m_branches[branch], *tip);
        }

        // ---- tip 修订视图（try 轨：tip 来自权威分支表，理论必在闭包
        //      内；nullopt 属装载违约的防御面——按稳定空状态降级，不抛）。
        const std::optional<RevisionView> tipView = m_host.query().tryRevision(*tip);
        if (!tipView.has_value()) {
            return UndoRedoStatus{};
        }

        // ---- 可用性推导：undo 看 tip inverse（磁盘推导——重启后会话
        //      栈为空仍成立，§5.5 注释原文）；redo 看会话栈顶与 tip 的
        //      一致性（栈顶记录的 undoRevision 即撤销后的 tip——被新命
        //      令推进过则上文 syncOnEntry 已清空，此处恒一致）。
        UndoRedoStatus result;
        std::lock_guard<std::mutex> guard(m_mutex);
        // 非 const 引用：一次性 stale 旗标的消费是本方法的写面（mutable
        // 成员＋互斥保护——noexcept 轮询面的惰性同步语义）。
        BranchUndoState& state = m_branches[branch];
        if (tipView->inverse.has_value()) {
            result.canUndo = true;
            // 将被撤销的命令＝tip 修订的命令（其摘要即菜单提示数据——
            // PM-18"空历史明确稳定提示"的可用侧表达）。
            result.undoSummary = tipView->commandSummary;
        }
        if (!state.redoStack.empty()
            && state.redoStack.back().undoRevision == *tip) {
            result.canRedo = true;
            // 将被重做的命令＝撤销时点记录的被撤销命令摘要。
            result.redoSummary = state.redoStack.back().undoneSummary;
        }
        // ---- 一次性 stale 说明（§6.9 行 6）：读即消费——同一事实只向
        //      调用方呈现一次，随后回到平凡空状态。
        if (state.redoClearedStale) {
            state.redoClearedStale = false;
            result.blockedReason = StoreError(
                StoreErrorCode::StaleRevisionRejected,
                "project/undoredo: redo 栈已因分支 tip 被本服务之外的新命令"
                "推进而清空（§6.9 过期基线/新命令清空——standard semantics）"
                " branch=" + branch.toCanonical());
        }
        return result;
    } catch (...) {
        // 拒绝态/数据侧/内存——全部降级（noexcept 契约，实现口径③）。
        return UndoRedoStatus{};
    }
}

// =====================================================================
// undo——提交逆命令（§5.5：envelope 取自当前 tip 的 inverse 记录）
// =====================================================================

CommandResult UndoRedoServiceImpl::undo(core::BranchId branch,
                                        ICommandInteraction* interaction)
{
    // 查询/装配段的 StoreError（Closed/损坏）由 catch 收敛为 CommandResult
    // 终态；invalid_argument（前置违约）按调用方错误语义原样抛出
    // （fail-fast——UndoRedo.hpp 类注释错误总表）。
    try {
        std::string undoneSummary;
        CommandEnvelope envelope;
        {
            std::lock_guard<std::mutex> guard(m_mutex);
            BranchUndoState& state = m_branches[branch];

            // ---- 前置解析与可用性判定：tip 视图必须携带 inverse（§6.9
            //      "空历史：tip 修订无 inverse〔首修订/不可逆命令〕→
            //      canUndo=false"——此处调用却无 inverse 属前置违约）。
            const RevisionView tipView
                = resolveTipViewOrThrow(m_host.query(), branch);
            if (!tipView.inverse.has_value()) {
                throw std::invalid_argument(
                    "project/undoredo: undo 前置违约——当前 tip 修订无 inverse"
                    " 记录（首修订/不可逆命令不可撤销，§6.9） branch="
                    + branch.toCanonical());
            }

            // ---- 惰性同步（实现口径①）：tip 相对观察点前进即清空 redo
            //      栈（撤销动作本身也是"分支上的命令"——但它是本服务产
            //      出，观察点随后由本方法更新，不触发自清空）。
            syncOnEntry(state, tipView.id);

            // ---- 组装信封（§5.5 原文）并记录"将被撤销命令"的摘要——
            //      redo 条目的展示数据（人读摘要随修订持久化、视图不可
            //      变，此处拷贝的是展示值，非数据依赖）。
            envelope = buildUndoEnvelope(branch, tipView);
            undoneSummary = tipView.commandSummary;
        }
        // 锁外提交（锁序纪律：不嵌套命令执行槽——submit 内回调处理器，
        // 处理器回调 status() 不得死锁）；并发竞争由 S2 裁决（§6.2）。
        CommandResult result = m_host.commands().submit(envelope, interaction);

        // ---- 会话栈维护（只在提交成功后变更——失败路径栈不动，可重试）。
        if (result.committed() && result.newRevision.has_value()) {
            std::lock_guard<std::mutex> guard(m_mutex);
            BranchUndoState& state = m_branches[branch];

            // 实现口径②：读撤销修订视图，从其 inverse 拷贝"被撤销命令
            // 的原始表达"（对称声明面——由逆命令处理器在 prepare 时声
            // 明，§6.9 可逆性声明制）。修订不可变（PA-2），此处拷贝与
            // redo 时点重读等价；取拷贝使 D-11"redo 仅会话内"在结构上
            // 自明（记录随会话消亡）。
            const std::optional<RevisionView> undoView
                = m_host.query().tryRevision(*result.newRevision);
            if (undoView.has_value() && undoView->inverse.has_value()) {
                InverseRecord record;
                record.undoRevision = *result.newRevision;
                record.undoneSummary = undoneSummary;
                record.redoCommandType = undoView->inverse->commandType;
                record.redoPayloadFormatVersion
                    = undoView->inverse->payloadFormatVersion;
                record.redoPayloadCanonical.assign(
                    undoView->inverse->payloadCanonical.begin(),
                    undoView->inverse->payloadCanonical.end());
                state.redoStack.push_back(std::move(record));
            } else if (IDiagnosticsSink* sink = devSink()) {
                // 非对称声明族：撤销修订自身不可逆——redo 不可用（canRedo
                // 保持 false）。不静默：开发通道保留观察面（无收编用户码
                // 不私造——CR-08；行为语义见实现口径②）。
                sink->reportDev(
                    "project/undoredo",
                    "undo 提交成功但撤销修订无 inverse 声明（非对称可逆族）"
                    "——redo 不入栈 branch=" + branch.toCanonical());
            }

            // 观察点推进到撤销修订（下次进入比对以此为基准——本服务产出
            // 不触发自清空）。
            state.lastServiceTip = *result.newRevision;
            // 成功撤销建立了新的 redo 路径——先前的 stale 说明已过时
            // （其解释的对象〔旧 redo 栈〕已被新路径取代），一并消费。
            state.redoClearedStale = false;
        }
        return result;
    } catch (const StoreError& e) {
        return mapQueryFailure(e);
    }
}

// =====================================================================
// redo——重放被撤销命令的原始载荷（§6.9；仅会话内——D-11）
// =====================================================================

CommandResult UndoRedoServiceImpl::redo(core::BranchId branch,
                                        ICommandInteraction* interaction)
{
    try {
        core::RevisionId tip{};
        CommandEnvelope envelope;
        {
            std::lock_guard<std::mutex> guard(m_mutex);
            BranchUndoState& state = m_branches[branch];

            // ---- 前置解析与同步：先读当前 tip 并惰性同步——若分支被新
            //      命令推进过，redo 栈已在此清空，随后的一致性检查自然
            //      失败（§6.9"新命令清空 redo"的前置表达）。
            const RevisionView tipView
                = resolveTipViewOrThrow(m_host.query(), branch);
            syncOnEntry(state, tipView.id);
            tip = tipView.id;

            // ---- 栈顶一致性：重做只针对"最近一次撤销"（栈顶记录的
            //      undoRevision 必须正是当前 tip——否则被撤销命令已不是
            //      tip 的直接前驱，重放将脱离会话语义）。空栈或错位＝
            //      前置违约（调用方未先经 status() 判定）。
            if (state.redoStack.empty()
                || state.redoStack.back().undoRevision != tip) {
                throw std::invalid_argument(
                    "project/undoredo: redo 前置违约——会话 redo 栈为空或栈顶"
                    "记录与当前 tip 不一致（redo 仅会话内有效——D-11；先 "
                    "status() 判定可用性，§6.9） branch=" + branch.toCanonical());
            }

            // ---- 组装信封（§6.9"redo＝重放原始载荷产生新修订"）：三元
            //      组取自会话记录（撤销时点拷贝的处理器声明面——project
            //      不构造业务命令，§6.5）。
            envelope = buildRedoEnvelope(branch, state.redoStack.back(), tip);
        }
        // 锁外提交（同 undo——锁序纪律与 S2 竞争裁决）。
        CommandResult result = m_host.commands().submit(envelope, interaction);

        // ---- 会话栈维护（只在提交成功后弹出——失败路径栈不动，重做可
        //      重试）。防并发错弹：仅当栈顶仍是本次重做的记录时弹出
        //      （并发窗口内另一线程可能已重构栈——错位时保留现场，交由
        //      下次进入的惰性同步收敛）。
        if (result.committed() && result.newRevision.has_value()) {
            std::lock_guard<std::mutex> guard(m_mutex);
            BranchUndoState& state = m_branches[branch];
            if (!state.redoStack.empty()
                && state.redoStack.back().undoRevision == tip) {
                state.redoStack.pop_back();
            }
            // 观察点推进到重做修订；stale 说明随成功路径消费（同 undo）。
            state.lastServiceTip = *result.newRevision;
            state.redoClearedStale = false;
        }
        return result;
    } catch (const StoreError& e) {
        return mapQueryFailure(e);
    }
}

}  // namespace sdurws::ird::project
