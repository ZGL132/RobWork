/**
 * @file   UndoRedo.hpp
 * @brief  撤销/重做服务（UndoRedoService）——"撤销/重做＝逆命令提交产生
 *         新修订"的公共契约：状态视图、会话重做记录与服务端口（PRJ-T13
 *         落位）。
 *
 * 设计依据：
 *   - units/project.md §5.5（撤销/重做服务接口原文——UndoRedoStatus/
 *     UndoRedoService 的字段与方法签名逐条锚定该节代码块）、§6.9（撤销/
 *     重做语义详表——可逆性声明/撤销＝新修订/栈归属/新命令清空 redo/
 *     空历史/过期基线/跨分支/与草稿局部撤销的边界，本头行为语义的权威）、
 *     §3.1（组成表：`UndoRedo.hpp` 行——本头承载 UndoRedoService、
 *     UndoRedoStatus、InverseRecord 三类型）、§3.3（头包含形式）、
 *     §12 PRJ-T13 行（产物 `UndoRedoServiceImpl.*`——逆命令提交、会话
 *     栈、边界）；
 *   - 需求 PM-18（项目命令撤销/重做＝新修订的提交机制、空历史明确稳定
 *     提示、与草稿局部撤销的边界）、PA-2（不可变历史——撤销/重做只增
 *     新修订，不改写既有修订）、D-11（撤销栈归属＝会话内按分支，不
 *     持久化）、AT-29（「应用→撤销→重做产生新修订且历史不改写」验收
 *     锚点——自 DTB WP-04-T13 需求列迁注）；
 *   - 任务契约 tasks/foundation/PRJ-T13.json acceptance 1～4。
 *
 * 背景说明（为什么"撤销"不是回滚——PM-18/PA-2 的机制承接）：
 *   本软件中修订一经提交即为不可变证据（PA-2），不存在"改写旧修订"的
 *   撤销。撤销的实际机制＝**把逆命令当作一条普通命令提交**（经命令端口
 *   的 S1～S7 全流程），产生恰好一个新修订——历史只增不改。"逆命令的
 *   类型与载荷"由业务处理器在 prepare 时声明（§6.9：可逆性是处理器
 *   声明的事实，project 不推断），持久化于修订的命令留痕 inverse 字段
 *   （§4.4.4）；本服务只负责把它取出来、装进信封、交给命令端口（§6.5
 *   注册协议——project 不代行业务逆命令）。重做＝重新提交被撤销命令的
 *   原始载荷，同样只产生新修订。
 *
 * 背景说明（栈归属 D-11——为什么 undo 与 redo 的可用性来源不同）：
 *   - canUndo **由磁盘推导**：分支 tip 修订的 inverse 记录存在即可撤销
 *     （§5.5 注释"重启后历史仍在——undo 能力由 tip 修订的 inverse 记录
 *     推导"）——重启/重开项目后第一个 undo 不依赖任何会话记忆；
 *   - canRedo **仅会话内有效**：重做所需的"被撤销命令原始载荷"记录
 *     （InverseRecord）只保存在本会话内存（D-11 不持久化）——重启后
 *     会话栈为空，canRedo 恒 false（此时"撤回顾先的撤销"仍可经 undo
 *     完成：撤销产生的修订自身携带 inverse，可用性同样由 tip 推导）。
 *
 * 与草稿局部撤销的边界（§2.3 非目标/§6.9 末行——PM-18 与 REQ-11 分界）：
 *   本服务只承接**项目命令级**撤销（产生修订）；未应用草稿内的编辑级
 *   局部撤销（需求集撤销等，REQ-11）归 ui DraftController＋各业务域
 *   编辑器，不经命令服务、不产生修订（N-10）。本头不提供任何草稿编辑
 *   撤销入口。
 *
 * P-PR-1 处置（acceptance 4）：本头消费的身份类型（core::BranchId/
 * core::RevisionId）一律来自 core.md v0.1 公共契约头 Identity.hpp（经
 * QueryPort.hpp 传递引入），以该 Draft 基线消费、不私改 core；core 冻
 * 结出 diff 后按影响面增量同步（§15.3 既有口径）。
 *
 * 线程模型（§9.8）：status/undo/redo 可从任意线程调用；会话栈由实现内
 *   部互斥保护；undo/redo 的提交段经命令端口的命令执行槽串行（§6.1）
 *   ——同分支并发 undo/redo 的安全性由命令端口 S2 过期基线校验兜底
 *   （§6.2：后到者 Rejected(stale-revision)，不产生修订）。
 */

#ifndef SDURWS_IRD_PROJECT_UNDOREDO_HPP
#define SDURWS_IRD_PROJECT_UNDOREDO_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/project/CommandService.hpp>
#include <sdurws/ird/project/StoreTypes.hpp>

namespace sdurws::ird::project {

// =====================================================================
// §5.5 状态视图与会话记录类型
// =====================================================================

/**
 * @brief 撤销/重做状态视图（§5.5 原文形态）——菜单绑定（ui）的可用性
 *        与提示数据源。
 *
 * 背景说明（"空历史稳定提示数据"的落位口径——PM-18）：空历史（tip 无
 * inverse/分支不存在/上下文拒绝态）时本结构返回**明确定义的全 false＋
 * 空摘要**组合——不抛错、无不确定性；"无可撤销/可重做"的人读文案归 ui
 * （PA-1 权威唯一：文案不归 project），本结构只携带机器可读的可用性位
 * 与命令摘要。undoSummary/redoSummary 在可用时分别携带"将被撤销的命
 * 令摘要"（tip 修订的命令摘要）与"将被重做的命令摘要"（撤销时点记录
 * 的被撤销命令摘要）——ui 菜单悬停提示直接消费。
 *
 * blockedReason 语义（§6.9"过期基线"行）：会话 redo 栈因分支 tip 被本
 * 服务之外的新命令推进而清空时，此后第一次 status() 以
 * StaleRevisionRejected 说明"redo 为何消失"（§6.9 过期基线→
 * stale-revision 同契约）；旗标一次性（再次 status() 回到平凡空状态）。
 * 该理由仅经本字段机器可读表达，不产用户级诊断（status 是轮询面——
 * 重复上报属噪音；真正的提交拒绝由命令端口按其诊断链路产出）。
 *
 * 线程安全：纯值类型。
 */
struct UndoRedoStatus {
    /// true＝该分支当前 tip 修订携带 inverse 记录（可撤销——磁盘推导，
    /// 会话无关）。
    bool canUndo = false;
    /// true＝会话 redo 栈非空且栈顶记录与当前 tip 一致（仅会话内——D-11）。
    bool canRedo = false;
    /// 将被撤销命令的人读摘要（canUndo 时非空——tip 修订 commandSummary；
    /// 否则 nullopt）。
    std::optional<std::string> undoSummary;
    /// 将被重做命令的人读摘要（canRedo 时非空——撤销时点记录的被撤销
    /// 命令摘要；否则 nullopt）。
    std::optional<std::string> redoSummary;
    /// 边界说明（§5.5 注释"过期基线/不可逆/跨分支边界说明"的机器可读
    /// 面——当前落位＝§6.9 过期基线的 StaleRevisionRejected；其余边界
    /// 以可用性位表达，无对应稳定码不私造——CR-08）。
    std::optional<StoreError> blockedReason;
};

/**
 * @brief 会话重做记录（§3.1 组成表点名的 InverseRecord）——一次成功撤
 *        销在会话内留下的 redo 栈条目（D-11：仅内存、不持久化）。
 *
 * 背景说明（字段来源与"project 只提交"的证据链）：undo 成功提交后，撤
 * 销产生的新修订（undoRevision）自身的 inverse 记录＝被撤销命令的原始
 * 表达（对称声明面——由业务处理器在 prepare 时产出，§6.9"可逆性是处
 * 理器声明的事实"）。本记录在撤销时点从该视图**拷贝** redoCommandType/
 * redoPayloadFormatVersion/redoPayloadCanonical 三元组（§6.4 载荷版本
 * 三元组）；redo 执行时以该拷贝组装信封提交——project 不构造、不解释
 * 任何业务逆命令（§6.5 注册协议；载荷域所有，透传——D-10）。
 *
 * 拷贝时机口径（实现口径登记，DTB §5.4）：在 undo 提交后立即读取撤销修
 * 订视图并拷贝（修订不可变——PA-2，拷贝与 redo 时点重读等价；取拷贝是
 * 让"重做所需数据仅存于会话"的 D-11 语义在结构上自明——记录随会话消
 * 亡，磁盘虽有同源事实但 redo 不从磁盘重建）。
 *
 * 线程安全：纯值类型。
 */
struct InverseRecord {
    /// 撤销产生的修订身份（rev-；＝入栈时点的分支 tip——redo 前置一致
    /// 性检查的比对键：tip 不再等于此值即被新命令清空，§6.9）。
    core::RevisionId undoRevision{};
    /// 被撤销命令的人读摘要（撤销前 tip 修订的 commandSummary——
    /// redoSummary 的展示数据源）。
    std::string undoneSummary;
    /// 原始命令的处理器注册 token（§4.4.4 语法；＝撤销修订 inverse 的
    /// commandType——业务处理器声明面）。
    std::string redoCommandType;
    /// 原始命令载荷的处理器自有版本（无单位——格式版本号；§6.4 三元组）。
    std::uint32_t redoPayloadFormatVersion = 0;
    /// 原始命令 canonical 载荷字节（域所有；透传不解释——D-10）。
    std::vector<std::uint8_t> redoPayloadCanonical;
};

// =====================================================================
// §5.5 撤销/重做服务（①命令端口之上的会话级编排面）
// =====================================================================

/**
 * @brief 撤销/重做服务（§5.5 原文形态）——逆命令提交的会话级编排面
 *        （经 ProjectStore::undoRedo() 获取实例引用；PRJ-T13 挂载）。
 *
 * 生命周期与所有权：实现实例由存储上下文持有并随其消亡（与 query()/
 *   commands()/drafts() 同款——端口无独立生命周期）；同一上下文多次
 *   调用 undoRedo() 返回同一实例（会话栈归属该实例——跨实例不共享，
 *   D-11"会话内"的机制落点）。消费方（ui/workflow）只持引用调用。
 *
 * 错误语义总表（错误二分——AGENTS §3/§5.0）：
 *   - status()：noexcept 恒不抛——查询拒绝态（Closed 等）与数据侧异常
 *     一律降级为全 false＋空摘要的稳定数据（真实错误经 undo/redo 返回
 *     值观察）；
 *   - undo()/redo() 的**可用性前置违约**（canUndo/canRedo 为 false 时
 *     调用）＝调用方契约违约 → std::invalid_argument fail-fast（菜单
 *     绑定纪律：先 status() 后动作；Rejection 词表无对应项，不私扩——
 *     §5.2 branchHistory 未知分支同口径）；
 *   - undo()/redo() 的**提交期结果**＝命令端口 submit 的四态原样透传
 *     （Committed/Rejected/Aborted/Failed——含只读上下文 not-writable、
 *     并发竞争 stale-revision、逆命令 token 未注册 unknown-command 等）；
 *     查询阶段的环境/状态异常映射为 Failed/Aborted（见方法注释）。
 *   - 全部失败路径零修订（submit 后置保证——§5.3.1）；undo/redo 的会
 *     话栈只在**提交成功**后变更（失败不改会话状态——重试语义）。
 *
 * 线程安全：三方法均可从任意线程并发调用（会话栈内部互斥；提交段经
 *   命令执行槽串行——并发 undo/redo 竞争由 S2 过期基线校验裁决，后到
 *   者 Rejected(stale-revision)——§6.2）。
 */
class UndoRedoService {
public:
    /// 虚析构：实现随存储上下文消亡（unique_ptr 成员多态销毁）。
    virtual ~UndoRedoService() = default;

    /**
     * @brief 撤销/重做可用性与提示数据（§5.5 原文签名 noexcept）。
     *
     * 执行序：读权威分支表定位该分支 tip（INV-M3——分支不存在＝稳定空
     * 状态，不抛）→惰性同步会话态（tip 相对本服务上次观察已前进＝分支
     * 上发生了本服务之外的新提交 → 清空该分支 redo 栈并置一次性 stale
     * 说明——§6.9"新命令清空 redo"/"过期基线"两行）→读 tip 修订视图
     * 推导 canUndo/undoSummary→比对会话 redo 栈推导 canRedo/redoSummary。
     *
     * @param branch [in] 目标分支（brn- 规范文本；不存在于权威分支表＝
     *               稳定空状态——noexcept 契约下不作为调用方违约抛出）
     * @return 状态视图（见 UndoRedoStatus 字段注释）
     *
     * 线程安全：并发安全（noexcept 轮询面——内部互斥＋全异常兜底）。
     */
    [[nodiscard]] virtual UndoRedoStatus status(core::BranchId branch) const noexcept = 0;

    /**
     * @brief 撤销＝提交逆命令（§5.5 原文：envelope 的 type/payload 取自
     *        当前 tip 修订的 inverse 记录，expectedRevision＝tip）。
     *
     * 执行序：同步会话态（同 status）→读 tip 修订视图→取其 inverse
     * 组装 CommandEnvelope{branch, expectedRevision=tip, inverse 三元组}
     * →经命令端口 submit（S1～S7 全流程）→Committed 时记录会话 redo
     * 条目（从撤销修订的 inverse 拷贝原始命令三元组——InverseRecord）
     * 并更新会话观察点。
     *
     * @param branch      [in] 目标分支（须满足 status(branch).canUndo——
     *                    违约 std::invalid_argument）
     * @param interaction [in] 确认回调（透传 submit——逆命令处理器声明
     *                    待确认集时走 §6.7 放行流；可空＝非交互提交）
     *
     * @return 提交结果（四态；Committed 时 newRevision＝撤销产生的新修
     *         订——恰好一个、历史只增不改，PA-2/AT-29）
     *
     * @throws std::invalid_argument 该分支当前不可撤销（前置违约）
     *
     * 线程安全：任意线程并发调用（竞争面由命令端口 S2 裁决——见类注释）。
     */
    virtual CommandResult undo(core::BranchId branch,
                               ICommandInteraction* interaction = nullptr) = 0;

    /**
     * @brief 重做＝重新提交被撤销命令的原始载荷（§5.5/§6.9——产生新
     *        修订；仅会话内有效，D-11）。
     *
     * 执行序：同步会话态（同 status——新命令已致 redo 栈清空时前置不
     * 再成立）→取会话 redo 栈顶记录组装 CommandEnvelope{branch,
     * expectedRevision=tip, 记录的原始命令三元组}→经命令端口 submit→
     * Committed 时弹出栈顶并更新会话观察点（失败不动栈——可重试）。
     *
     * @param branch      [in] 目标分支（须满足 status(branch).canRedo——
     *                    违约 std::invalid_argument）
     * @param interaction [in] 确认回调（透传 submit——同 undo）
     *
     * @return 提交结果（四态；Committed 时 newRevision＝重做产生的新修
     *         订——重放原始载荷的**新**修订，非复现旧修订身份）
     *
     * @throws std::invalid_argument 该分支当前不可重做（前置违约——含
     *         redo 栈被新命令清空后的调用）
     *
     * 线程安全：任意线程并发调用（同 undo）。
     */
    virtual CommandResult redo(core::BranchId branch,
                               ICommandInteraction* interaction = nullptr) = 0;
};

}  // namespace sdurws::ird::project

#endif  // SDURWS_IRD_PROJECT_UNDOREDO_HPP
