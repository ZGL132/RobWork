/**
 * @file   UndoRedoServiceImpl.hpp
 * @brief  撤销/重做服务实现（UndoRedoServiceImpl）——§5.5/§6.9 契约的
 *         实现载体：tip 逆命令推导（undo）、会话重做栈（redo）、按分支
 *         的惰性同步与新命令清空语义（私有实现头，不出 include/——R-2
 *         纪律）。
 *
 * 设计依据：
 *   - units/project.md §5.5（撤销/重做服务——接口原文与"栈归属：会话内
 *     按分支维护，不持久化；重启后 canUndo 由 tip 修订的 inverse 记录
 *     推导，redo 仅会话内有效"）、§6.9（语义详表逐行：可撤销命令的
 *     记录＝处理器声明/撤销＝新修订/撤销栈归属/新命令清空 redo 栈/空
 *     历史稳定提示/过期基线 stale-revision/跨分支隔离/与草稿局部撤销
 *     的边界）、§5.3.1（CommandEnvelope/CommandResult——undo/redo 的
 *     提交载体与结果透传面）、§6.2（expectedRevision 并发校验——并发
 *     undo/redo 竞争的裁决面）、§6.5（注册协议——逆命令由业务处理器
 *     产出，project 只提交）、§12 PRJ-T13 行（产物：逆命令提交、会话
 *     栈、边界）；
 *   - 需求 PM-18（撤销/重做＝新修订、空历史稳定提示、草稿边界）、
 *     PA-2（历史不改写）、D-11（会话内按分支、不持久化）、AT-29
 *     （应用→撤销→重做验收锚点）；
 *   - 任务契约 tasks/foundation/PRJ-T13.json acceptance 1～4。
 *
 * 背景说明（本类在单元内的位置——"会话编排面"边界）：
 *   本类**没有任何写路径**：undo/redo 一律组装 CommandEnvelope 后经命
 *   令端口 submit（S1～S7、事务、事件、诊断全部归命令端口——§6.5
 *   "命令服务不是转发包装器"的反向成立：本类也不复制命令语义）。本类
 *   自有语义只有三件：①可用性推导（undo 看 tip 的 inverse——纯磁盘事
 *   实；redo 看会话栈——D-11）；②会话栈维护（按分支隔离、新命令惰性
 *   清空、一次性 stale 说明——§6.9）；③信封组装（载荷版本三元组从
 *   inverse 记录/会话记录拷贝——project 不构造业务逆命令）。
 *
 * 实现口径登记（DTB §5.4——单元卡未定义判据，三项）：
 *   ①"新命令提交"的检测形态＝**惰性同步**（§6.9"会话内该分支提交任
 *     何新命令→redo 栈清空"的机制落点）：本服务不订阅事件总线（会话
 *     级组件不新增总线依赖面），每次 status/undo/redo 进入时读权威分
 *     支表比对上次观察 tip——不一致即判定"发生了本服务之外的提交"，
 *     清空该分支 redo 栈（§6.9 标准语义）并置一次性 stale 说明
 *     （§6.9"过期基线"行）。清空只发生在"栈非空且 tip 前进"时（空栈
 *     时的 tip 前进只更新观察点——无可清空即无 stale 语义）。
 *   ②undo 后 redo 条目的数据源＝撤销修订视图的 inverse 记录（对称声
 *     明面——由逆命令处理器在 prepare 时声明"原始命令"，§6.9）。撤
 *     销修订无 inverse（非对称声明族）时不入 redo 栈——canRedo 保持
 *     false，经开发诊断保留观察面（不静默，CR-08 精神：无收编码不私
 *     造，开发通道上报）。
 *   ③status() 的分支不存在/查询拒绝态＝稳定空状态（noexcept 契约的
 *     降级表达——UndoRedo.hpp 类注释错误总表）；blockedReason 只承载
 *     §6.9 过期基线一项（StaleRevisionRejected——复用 §4.4.8 封闭集
 *     现有码，不私扩），且不产用户级诊断（轮询面去重——真正的提交拒
 *     绝由命令端口诊断链路产出）。
 *
 * 线程模型（§9.8——高危信息）：
 *   - m_mutex 保护会话栈表（m_branches）：status/undo/redo 的会话读写
 *     段全程持锁；提交段（submit）**不持本锁**（锁序纪律：本锁绝不嵌
 *     套命令执行槽——submit 内部回调处理器，处理器若回调 status() 不
 *     死锁）；
 *   - 并发 undo/redo 的正确性不依赖本锁：信封携带 expectedRevision＝
 *     tip，提交竞争由命令端口 S2 过期基线校验裁决（§6.2——后到者
 *     Rejected(stale-revision)，零修订；会话栈只在 Committed 后变更，
 *     失败路径栈不变）；
 *   - m_host 的查询面（query()/commands()）各自线程安全（§4.7/§5.1）。
 *
 * 所有权与生命周期：由 ProjectStoreImpl 持有（每上下文一个实例——
 *   "会话"的机制边界即存储上下文）；host 引用非 owning，生存期由宿主
 *   成员声明序保证（本类构造于装配末尾、析构于最前——宿主引用不悬空）。
 */

#ifndef SDURWS_IRD_PROJECT_SRC_UNDOREDOSERVICEIMPL_HPP
#define SDURWS_IRD_PROJECT_SRC_UNDOREDOSERVICEIMPL_HPP

#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/project/CommandService.hpp>
#include <sdurws/ird/project/StoreTypes.hpp>
#include <sdurws/ird/project/UndoRedo.hpp>

namespace sdurws::ird::project {

class ProjectStoreImpl;  // 宿主（完整定义于 ProjectStoreImpl.hpp——实现
                         // 文件 include；查询面经公共访问器消费，非 friend）

/**
 * @brief §5.5 UndoRedoService 的实现（final——不再派生；消费者只面向
 *        公共抽象，会话栈细节不外泄——R-2）。
 *
 * 生命周期与所有权：仅由 ProjectStoreImpl 构造创建（unique_ptr 持有，
 *   经 ProjectStore::undoRedo() 以接口引用交付）。
 */
class UndoRedoServiceImpl final : public UndoRedoService {
public:
    /**
     * @brief 构造撤销/重做服务（仅宿主可达——装配于宿主构造体内）。
     *
     * @param host [in] 宿主存储上下文（非 owning——查询端口/命令端口/
     *             诊断 sink 的提供方；生存期覆盖本对象）
     */
    explicit UndoRedoServiceImpl(ProjectStoreImpl& host) noexcept;

    /// 析构（会话栈随成员销毁——D-11 不持久化的终局形态）。
    ~UndoRedoServiceImpl() override;

    // ---- UndoRedoService（§5.5；语义详见公共头注释） ----

    [[nodiscard]] UndoRedoStatus status(core::BranchId branch) const noexcept override;
    [[nodiscard]] CommandResult undo(core::BranchId branch,
                                     ICommandInteraction* interaction = nullptr) override;
    [[nodiscard]] CommandResult redo(core::BranchId branch,
                                     ICommandInteraction* interaction = nullptr) override;

private:
    /**
     * @brief 单分支的会话撤销/重做状态（D-11——仅内存，随上下文消亡）。
     *
     * 背景：undo 的可用性不取自本结构（tip inverse 磁盘推导——§5.5），
     * 本结构只承载 redo 半区与"本服务观察点"。观察点是 §6.9"新命令
     * 清空 redo"的检测基准：本服务每次产出/观察的分支 tip 记录于此，
     * 与权威分支表不符即判定发生了本服务之外的提交。
     */
    struct BranchUndoState {
        /// 本服务最后一次观察/产出的分支 tip（nullopt＝尚未观察——首次
        /// 观察只建立基线，无可清空即无 stale 语义）。
        std::optional<core::RevisionId> lastServiceTip;
        /// 会话 redo 栈（栈顶＝back——最近一次撤销；元素为撤销时点拷贝
        /// 的原始命令三元组——InverseRecord）。
        std::vector<InverseRecord> redoStack;
        /// 一次性 stale 说明旗标（§6.9"过期基线"——栈因 tip 前进被清空
        /// 后置位，由下一次 status() 读取并以 blockedReason 表达后消费）。
        bool redoClearedStale = false;
    };

    /// 会话状态表（按分支隔离——§6.9"跨分支"行；mutable＋互斥：status
    /// 为 const noexcept 也要完成惰性同步）。
    mutable std::mutex m_mutex;
    mutable std::map<core::BranchId, BranchUndoState> m_branches;

    /// 宿主存储上下文（查询端口/命令端口/诊断 sink——非 owning）。
    ProjectStoreImpl& m_host;

    // ---- 私有步骤（调用方已持 m_mutex；查询异常向上传播——各公有方法
    //      按其契约兜底） ----

    /**
     * @brief 惰性同步（实现口径①）：比对权威分支表中该分支当前 tip 与
     *        本服务观察点；不一致且 redo 栈非空 → 清空＋置 stale 旗标；
     *        无论如何刷新观察点。
     *
     * @param state [in,out] 该分支会话状态（表内条目，已存在或就地建立）
     * @param tip   [in] 权威分支表的当前 tip（本次进入时读取的地面事实）
     */
    static void syncOnEntry(BranchUndoState& state, const core::RevisionId& tip);

    /**
     * @brief 组装 undo 信封（§5.5 原文：type/payload 取自 tip 修订的
     *        inverse 记录，expectedRevision＝tip）。
     *
     * @param branch  [in] 目标分支
     * @param tipView [in] tip 修订视图（须携带 inverse——调用方前置校验）
     * @return 提交信封（载荷版本三元组从 inverse 拷贝；磁盘契约的
     *         std::string 载荷转字节向量——值拷贝，D-10 透传）
     */
    [[nodiscard]] static CommandEnvelope buildUndoEnvelope(
        const core::BranchId& branch, const RevisionView& tipView);

    /**
     * @brief 组装 redo 信封（§6.9"redo＝重放原始载荷"）：三元组取自会
     *        话记录（撤销时点从撤销修订 inverse 拷贝——InverseRecord）。
     *
     * @param branch [in] 目标分支
     * @param record [in] redo 栈顶记录
     * @param tip    [in] 当前 tip（＝expectedRevision——与记录的
     *               undoRevision 一致性已由调用方校验）
     * @return 提交信封
     */
    [[nodiscard]] static CommandEnvelope buildRedoEnvelope(
        const core::BranchId& branch, const InverseRecord& record,
        const core::RevisionId& tip);

    /**
     * @brief 查询阶段的 StoreError 映射（undo/redo 共用的失败出口——
     *        CommandServiceImpl submit 的映射面同表，实现口径③）：
     *        ContextClosed → Aborted(context-closing)（§6.7 排空语义）；
     *        其余环境/数据侧码 → Failed 透传。
     *
     * @param e [in] 查询阶段捕获的存储异常
     * @return 承载终态的结果（零修订语义不变）
     */
    [[nodiscard]] CommandResult mapQueryFailure(const StoreError& e) const;

    /**
     * @brief 开发诊断通道（实现口径②的观察面——非对称声明族不入 redo
     *         栈时上报；可空＝丢弃，§5.0 sink 约定）。
     */
    [[nodiscard]] IDiagnosticsSink* devSink() const noexcept;
};

}  // namespace sdurws::ird::project

#endif  // SDURWS_IRD_PROJECT_SRC_UNDOREDOSERVICEIMPL_HPP
