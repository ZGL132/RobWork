/**
 * @file   CommandServiceImpl.hpp
 * @brief  命令服务实现（CommandServiceImpl）——§5.3/§6 命令端口契约的
 *         实现载体：S1～S7 生命周期编排、命令执行槽（全局串行）、处理
 *         器注册表消费、确认放行决策映射与双编译事务编排（私有实现头，
 *         不出 include/——R-2 纪律）。
 *
 * 设计依据：
 *   - units/project.md §5.3（命令端口各节——提交接口/领域处理器/确认回
 *     调/处理器注册/双编译最小契约）、§6.1（命令生命周期 S1～S7 与串行
 *     化范围——"每存储上下文一个命令执行槽（互斥）；S3～S6 全程持槽"）、
 *     §6.2（expectedRevision 并发校验）、§6.3（未知命令/非法输入/过期
 *     修订的处理——Rejected 稳定语义）、§6.4（载荷序列化与版本策略）、
 *     §6.5（处理器注册与注入——project 编译期仅依赖 core）、§6.6（双
 *     编译事务编排——任一失败不提交、临时资源隔离、回滚＝丢弃内存计划）、
 *     §6.7（可确认诊断放行流——非交互提交拒绝/关闭取消路径）、§6.8（命
 *     令摘要、对象变更集与事件的关系）、§9.6（失权写入防护——S1 形式
 *     校验的 writable 半边）、§12 PRJ-T10 行（串行槽/S1-S7/注册表/编译
 *     编排）；
 *   - 需求 ARC-01（命令原子产生修订＋全局串行）、PM-18（可逆性声明）、
 *     PM-04（过期基线→StaleRevisionRejected）、SA-15（确认放行——S4
 *     编排面）、MDL-06（双编译原子性——S5）；
 *   - 任务契约 tasks/foundation/PRJ-T10.json acceptance 1～5。
 *
 * 背景说明（本类在单元内的位置——"编排者"边界）：
 *   命令服务是 S1～S7 的**编排者**：S3 的业务断言由注入处理器执行
 *   （P-PR-3 处置——本类零业务数值）、S5 的编译由注入端口执行（P-PR-7
 *   ——本类零编译算法）、S6 的七步文件事务归 TxEngine（§7）、S7 的事
 *   件发布在事务第 6 步内（TxEngine——D-18 重试口径）。本类自有语义＝
 *   形式校验（S1）、基线解析与过期拒绝（S2）、槽串行化、确认决策映射
 *   （S4——决策判定在本类，凭据绑定复核与留痕归 PRJ-T11）、计划到
 *   CommitPlan 的装配（S6 前半：身份分配点——tx::CommitPlan 登记）。
 *
 * 实现口径登记（DTB §5.4——单元卡未定义判据，详见各私有方法注释）：
 *   ①S1 未知分支 → Rejected(invalid-payload)（§5.0 封闭集无"未知分支"
 *     稳定码，不私扩——§5.2 branchHistory 先例同口径）；
 *   ②Rejected(not-writable) 的 error 字段携带对应 §5.0 门卫稳定码
 *     （ContextClosed/LockHeldByOther/WriteRejected——与 executeCommit
 *     门卫同表），并产出同码用户诊断（PRJ-WRITE-AUTHORITY-LOST/
 *     PRJ-LOCK-HELD——diagrec 共享工厂）；执行中途门卫 ContextClosed
 *     → Aborted(context-closing)（§6.7 排空语义——S1 已过、命令进行中
 *     被关闭属"中止"而非"拒绝"）；
 *   ③CreateBranch 型元数据变更（metadataChange.createBranchWithBase）
 *     的提交形态＝无 tipUpdate＋addedBranches 条目（base/tip＝源分支
 *     tip——§4.5.1 走查步骤 1/3：CreateBranch 修订非任何分支 tip）；
 *     其余提交一律携带 tipUpdate{活动分支→新修订}（§4.5 三种指针表）；
 *   ④逆命令载荷版本＝处理器 currentPayloadVersion()（§4.4.4
 *     InverseCommand.payloadFormatVersion"处理器自有版本"——逆载荷由
 *     处理器按其当前格式产出；CommandPlan 未单列逆版本字段）；
 *   ⑤编译临时产物清理（D-09/§6.6）：命令结束后清空 .staging/tmp 内容
 *     （目录保留——§4.1"启动清理（按需重建）"同形态），失败仅开发诊断；
 *   ⑥HandlerRegistry 重复注册＝std::invalid_argument（装配期调用方错误
 *     ——注册表面无稳定码，不私扩）。
 *
 * P-PR-9 阻断登记（acceptance 4 内置元数据命令族——本任务不落位）：
 *   units/project.md §15.3 P-PR-9（PRJ-T04 登记，明示"PRJ-T10 落位内置
 *   元数据命令族前需裁决，不在代码中私自裁决"）：§4.4.4 冻结语法
 *   ^[a-z0-9-]{3,64} 与 §6.5 内置命令族示例 project.create-branch（含
 *   点）矛盾，Codec.cpp 已按无点语法冻结拒绝语义。注册 token 的二选一
 *   属所有者裁决面——本实现提供 HandlerRegistry 全部注册/查询/判据
 *   注入机制与测试处理器（无点 token），内置命令族（project.create-
 *   branch 等）待裁决后按其任务增量落位。
 *
 * 线程模型（§6.1/§9.8——高危信息）：
 *   - m_slot（命令执行槽）：submit 全程互斥——同上下文同时至多一个命令
 *     在执行（PRJ-TX-1"第二个等待"）；槽内调用处理器 prepare（业务代码
 *     只在此线程运行）；
 *   - 注册表 find 并发安全（HandlerRegistry 内部互斥——查询线程的
 *     hasUnresolvedPayload 判据消费）；注册仅装配期（运行期注册是装配
 *     纪律违约）；
 *   - m_host 的查询面（writable/closed/query）各自线程安全（§5.1/§4.7）。
 *
 * 所有权与生命周期：由 ProjectStoreImpl 持有（每上下文一个实例）；host/
 * compilePort/sink 三个引用/指针全部非 owning——生存期由装配方保证覆盖
 * 本对象（OpenStoreRequest 契约口径）。compilePort 可空＝未装配（声明
 * requiresDualCompile 的命令在此态 fail-fast）。
 */

#ifndef SDURWS_IRD_PROJECT_SRC_COMMANDSERVICEIMPL_HPP
#define SDURWS_IRD_PROJECT_SRC_COMMANDSERVICEIMPL_HPP

#include <mutex>
#include <string>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/project/CommandService.hpp>
#include <sdurws/ird/project/StoreTypes.hpp>

namespace sdurws::ird::project {

class ProjectStoreImpl;  // 宿主（完整定义于 ProjectStoreImpl.hpp——实现
                         // 文件 include；friend 窄访问收敛于本类）

/**
 * @brief §5.3.1 ProjectCommandService 的实现（final——不再派生；消费者
 *        只面向公共抽象，实现细节不外泄——R-2）。
 *
 * 生命周期与所有权：仅由 ProjectStoreImpl 构造创建（unique_ptr 持有，
 *   经 ProjectStore::commands() 以接口引用交付；装配期注册经 registry()
 *   访问器——同单元内部通道，测试与未来 L5 装配消费）。
 */
class CommandServiceImpl final : public ProjectCommandService {
public:
    /**
     * @brief 构造命令服务（仅宿主可达——装配于宿主构造体内）。
     *
     * @param host        [in] 宿主存储上下文（非 owning——查询面/写通道/
     *                    诊断 sink/项目目录的提供方；生存期覆盖本对象）
     * @param compilePort [in] 双编译端口（非 owning，可空＝未装配——
     *                    §5.3.6 L5 注入面；requiresDualCompile 命令在
     *                    空端口态 fail-fast）
     * @param sink        [in] 诊断 sink（非 owning，可空＝丢弃诊断——
     *                    §5.0 约定）
     */
    CommandServiceImpl(ProjectStoreImpl& host,
                       IModelCompilePort* compilePort,
                       IDiagnosticsSink* sink) noexcept;

    /// 析构（注册表 unique_ptr 随成员销毁——处理器由装配方经注册表移交
    /// 所有权，本类不外泄）。
    ~CommandServiceImpl() override;

    // ---- ProjectCommandService（§5.3.1；语义详见公共头注释） ----

    [[nodiscard]] CommandResult submit(
        const CommandEnvelope& envelope,
        ICommandInteraction* interaction = nullptr) override;
    [[nodiscard]] std::vector<std::string> registeredCommandTypes()
        const noexcept override;

    /**
     * @brief 处理器注册表访问器（装配期注册入口——§6.5"L5 应用壳装配
     *        期"的机制化；命令服务的 S1 与查询端口的 hasUnresolvedPayload
     *        判据共用本表）。
     *
     * 契约：注册只在装配期进行（运行期注册属装配纪律违约——§5.3.5
     * "运行期只读"）；本访问器属具体实现类型（不在 §5.3.1 抽象端口
     * 契约面——消费方为本单元装配/测试与未来 L5 壳）。
     */
    [[nodiscard]] HandlerRegistry& registry() noexcept
    {
        return m_registry;
    }

    /**
     * @brief 双编译端口装配通道（§5.3.6 L5 注入面的装配侧——指针非
     *        owning，所有权归装配方；构造默认空＋本通道＝两种装配形态
     *        [构造注入/装配后注入]并存，OpenStoreRequest 无端口字段〔§5.1
     *        冻结〕下的机制面；提交前装配、运行期替换属装配纪律违约）。
     */
    void setCompilePort(IModelCompilePort* port) noexcept
    {
        m_compilePort = port;
    }

private:
    // ---- S1～S7 的私有步骤（submit 的线性编排体；每步失败即返回） ----

    /**
     * @brief S1 形式校验（§6.1：type 已注册→payload 版本受理→branch
     *        存在于权威元数据→writable）。
     *
     * @param envelope    [in] 命令信封
     * @param interaction [in] 确认回调（传透给上下文——S3/S4 消费）
     * @param handler     [out] 解析到的处理器（S1 通过后非空）
     * @param result      [out] S1 失败时填充终态（rejected/aborted）——
     *                    调用方据 result.status.kind 短路
     * @return true＝S1 通过（可进入 S2）；false＝已终态（result 就绪）
     */
    bool runFormChecks(const CommandEnvelope& envelope,
                       ICommandInteraction* interaction,
                       ICommandHandler*& handler,
                       CommandResult& result);

    /**
     * @brief S2 基线解析与并发校验（§6.2）：解析分支 tip、判定
     *        expectedRevision 失配（→ stale-revision＋
     *        PRJ-STALE-REVISION-REJECTED）、装配基线修订视图。
     *
     * @param envelope  [in] 命令信封（expectedRevision/branch）
     * @param baseView  [out] 基线修订视图（tip 的值快照——S3/S5 输入）
     * @param result    [out] S2 失败时填充终态
     * @return true＝S2 通过；false＝已终态
     */
    bool resolveBaseline(const CommandEnvelope& envelope,
                         RevisionView& baseView,
                         CommandResult& result);

    /**
     * @brief S3～S6 的计划装配与事务提交：处理器 prepare（S3）→确认
     *        放行（S4）→双编译（S5）→CommitPlan 装配与七步事务（S6）。
     *
     * @param envelope    [in] 命令信封
     * @param handler     [in] S1 解析的处理器
     * @param baseView    [in] S2 的基线视图
     * @param interaction [in] 确认回调（可空）
     * @param result      [out] 终态（本方法必产生终态——Committed 或
     *                    各失败面）
     */
    void executePlan(const CommandEnvelope& envelope,
                     ICommandHandler& handler,
                     const RevisionView& baseView,
                     ICommandInteraction* interaction,
                     CommandResult& result);

    /**
     * @brief 清空编译临时目录内容（D-09/§6.6 实现口径⑤——命令结束
     *        后调用；目录保留、失败仅开发诊断不致命）。
     */
    void cleanCompileTmp();

    /// 宿主存储上下文（写通道/查询面/目录——非 owning，见类头）。
    ProjectStoreImpl& m_host;
    /// 双编译端口（可空——§5.3.6 L5 注入面）。
    IModelCompilePort* m_compilePort;
    /// 诊断 sink（可空——§5.0）。
    IDiagnosticsSink* m_sink;
    /// 处理器注册表（§5.3.5——S1 解析与查询判据共用；registry() 交付）。
    HandlerRegistry m_registry;
    /// 命令执行槽（§6.1——submit 全程互斥；每存储上下文一个）。
    std::mutex m_slot;
};

}  // namespace sdurws::ird::project

#endif  // SDURWS_IRD_PROJECT_SRC_COMMANDSERVICEIMPL_HPP
