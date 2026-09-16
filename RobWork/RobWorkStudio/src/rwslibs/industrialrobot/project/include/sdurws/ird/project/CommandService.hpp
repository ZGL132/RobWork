/**
 * @file   CommandService.hpp
 * @brief  命令端口（ProjectCommandService，①端口所有者）——唯一写路径
 *         入口的公共契约：命令信封/结果、领域处理器注入协议、确认回调、
 *         双编译最小契约与处理器注册表（PRJ-T10 落位）。
 *
 * 设计依据：
 *   - units/project.md §5.3（命令端口接口原文——本头的类型与方法签名逐条
 *     锚定该节代码块：§5.3.1 提交接口、§5.3.2 领域处理器、§5.3.3 确认
 *     交互回调、§5.3.5 处理器注册、§5.3.6 双编译最小契约）、§6（命令/
 *     确认/撤销与重做——S1～S7 生命周期、expectedRevision 并发校验、
 *     未知命令/非法输入/过期修订的处理、载荷序列化与版本策略、处理器
 *     注册与注入、双编译事务编排）、§3.1（组成表：CommandService.hpp 行
 *     ——本头的模块构成权威）、§3.3（头包含形式）；
 *   - 需求 ARC-01（聚合根＋命令原子产生修订——submit 是唯一写路径入口，
 *     全局串行）、PM-18（撤销/重做＝逆命令提交产生新修订——CommandPlan
 *     的 inverseCommandType 声明面）、SA-15（可确认诊断放行流——S4 编排
 *     与 ICommandInteraction 回调）、ARC-02（端口协作——业务单元经注入
 *     处理器写项目，零反向链接）；
 *   - 任务契约 tasks/foundation/PRJ-T10.json acceptance 1～5（PRJ-TX-1
 *     并发与过期基线、PRJ-TX-3 双编译桩三路、S1～S7 骨架与 §6.3 拒绝
 *     稳定码、§6.4 载荷版本策略、内置/测试处理器与 HandlerRegistry、
 *     P-PR-6/P-PR-1/P-PR-7 处置）。
 *
 * 背景说明（为什么写路径是"服务＋注入处理器"而不是直接写接口——§6.5）：
 *   本软件中一切项目变更都产生不可变修订（PA-2），而"怎样变更才合法"
 *   （物性/限位/策略断言）属各业务域（N-1 边界）。project 拥有处理器
 *   没有的平台语义——事务、串行、确认编排、修订与事件（NFR-MNT-04：
 *   命令服务不是转发包装器）；业务单元提供 ICommandHandler 实现（L5
 *   装配期注册），在 prepare 阶段做业务校验并产出变更计划，project 编排
 *   S1～S7 把计划落成恰好一个修订。**命令服务只编排断言阶段，判定由
 *   注入处理器执行**（P-PR-3 处置——ARCH §7.1 走查读法的单元卡解释，
 *   project 不持有 4π 等业务数值）。
 *
 * 增量落位说明（DTB §5.4 口径登记，三项）：
 *   1. §5.3.2 MetadataChange.newDisplayName（R2 PM-11-S2 项目改名）为
 *      **R2 预留字段**：其落盘语义需要元数据增量支持显示名变更，而
 *      RevisionIndex::deriveNextMetadata 的显式声明制（D-5，PRJ-T06 冻结
 *      API）当前只有 tip 更新＋新增分支两种增量。本任务落位面内任何
 *      prepare 产出该字段 → Rejected(invalid-payload)（防静默丢弃——
 *      "计划声明了但落盘不体现"是不可接受的数据面失真）；R2 改名命令族
 *      随其任务与 derive 增量扩展一并落位。
 *   2. §6.6"CompileRequest 携带专用临时目录"与 §5.3.6 冻结签名（3 字段）
 *      不一致：按 P-PR-7 处置（acceptance 3——§5.3.6 单侧冻结，对端另立
 *      形态前不私改签名）执行**冻结面**，不增字段；编译临时产物的落点
 *      约束（D-09：只允许 .staging/tmp/）由命令服务编排兑现——命令
 *      结束（成功/失败/中止）后清理 .staging/tmp 内容（§6.6"随事务/命令
 *      结束清理"），失败残留由启动恢复扫描兜底（§7.4①）。runtime 详设
 *      如需专用目录字段，随 P-PR-7 交接裁决后增量修订。
 *   3. §5.3.2 ICommandHandler 仅暴露 currentPayloadVersion()（§5.3.1 契约
 *      表前置"payload 版本＝处理器受理版本"）——受理集合的落位口径＝
 *      {currentPayloadVersion()}（§6.4"处理器受理版本集合显式声明"的
 *      当前可表达形态）；多历史版本受理（§6.4"可受理历史版本以保历史
 *      兼容"）随首个真实域处理器的版本演进需求扩接口，不预建。
 *
 * 线程模型（§6.1/§9.8）：submit 可从任意线程调用（内部转命令执行序列
 *   ——每存储上下文一个命令执行槽，互斥；S3～S6 全程持槽）；S4 确认
 *   等待持槽但零事务资源（§5.3.4）；registeredCommandTypes() 与
 *   HandlerRegistry::find() 并发安全（注册表内部互斥——find 亦被查询
 *   端口的 hasUnresolvedPayload 判据跨线程消费）。注册只在 L5 装配期
 *   发生，运行期只读（§5.3.5）。
 */

#ifndef SDURWS_IRD_PROJECT_COMMANDSERVICE_HPP
#define SDURWS_IRD_PROJECT_COMMANDSERVICE_HPP

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/project/QueryPort.hpp>

namespace sdurws::ird::project {

class ICommandInteraction;  // §5.3.3——HandlerContext 持有其指针（完整定义见下）
class IModelCompilePort;    // §5.3.6——HandlerContext 持有其指针（完整定义见下）

// =====================================================================
// §5.3.1 提交接口类型
// =====================================================================

/**
 * @brief 命令信封（§5.3.1 原文形态）——一次提交的调用方输入。
 *
 * 背景说明（§6.4 版本三元组）：命令载荷＝处理器域内 canonical 字节，
 * project **不解释**（透传存储——D-10）；载荷身份由三元组承载：
 * commandType（处理器注册 token）＋payloadFormatVersion（处理器演进
 * 版本戳）＋负载字节。token 语法＝§4.4.4 冻结 ^[a-z0-9-]{3,64}（注册表
 * 与磁盘留痕共用该面——P-PR-9 登记的内置命令族含点示例冲突待裁决，
 * 本头不裁决，注册表按 §4.4.4 语法受理）。
 *
 * 线程安全：纯值类型。
 */
struct CommandEnvelope {
    /// 目标分支（brn- 规范文本；须存在于权威元数据分支表——§6.1 S1）。
    core::BranchId branch{};
    /// 期望基线修订（rev-）。缺省（nullopt）＝该分支当前 tip（提交期
    /// 解析后等同显式——§6.2）；显式值 ≠ tip → Rejected(stale-revision)
    /// （§6.2 并发校验——PM-04 草稿基线与 PM-18 撤销基线同契约）。
    std::optional<core::RevisionId> expectedRevision;
    /// 处理器注册 token（§5.3.1"处理器注册 token"；未注册 →
    /// Rejected(unknown-command)，§6.3）。
    std::string commandType;
    /// 处理器自有负载版本（无单位——格式版本号；不受理 →
    /// Rejected(invalid-payload)，§6.3/§6.4）。
    std::uint32_t payloadFormatVersion = 0;
    /// 域 canonical 负载字节（不透明；project 原样存储——§6.4）。
    std::vector<std::uint8_t> payloadCanonical;
};

/**
 * @brief 提交结果状态（§5.3.1 variant 注释的值化形态）。
 *
 * 背景说明（四态与"无修订"不变量）：Committed{revision} 之外的一切
 * 终态——Rejected（边界/业务拒绝）、Aborted（取消/失效/关闭——§6.7）、
 * Failed（环境/数据侧失败，StoreError）——**都不产生任何修订**（§5.3.1
 * 表后置；暂存区可残留，恢复忽略——§7.4①）。kind 与 reason 的组合
 * 约定：kind==Committed 时两 reason 无意义；kind==Rejected 时 rejection
 * 有效；kind==Aborted 时 abort 有效；kind==Failed 时经 CommandResult::
 * error 携带 StoreError（本结构不带异常对象）。
 *
 * 线程安全：纯值类型。
 */
struct CommandStatus {
    /// 终态类别（§5.3.1 variant 四态）。
    enum class Kind { Committed, Rejected, Aborted, Failed };

    /// Rejected 的理由（§5.3.1 variant rejection 词表——封闭集）。
    enum class Rejection {
        HardAssertFailed,          ///< hard-assert-failed（§6.3：处理器硬断言）
        ConfirmationsRejected,     ///< confirmations-rejected（§5.3.3/§6.7）
        ConfirmationsUnresolved,   ///< confirmations-unresolved（§6.7 非交互提交）
        StaleRevision,             ///< stale-revision（§6.2 过期基线）
        UnknownCommand,            ///< unknown-command（§6.3 未注册 token）
        InvalidPayload,            ///< invalid-payload（§6.3 版本不受理/结构非法）
        NotWritable,               ///< not-writable（§6.1 S1 只读/关闭/失权）
    };

    /// Aborted 的理由（§5.3.1 variant abort 词表——封闭集）。
    enum class Abort {
        Canceled,          ///< canceled（§6.7 用户取消）
        InteractionLost,   ///< interaction-lost（§5.3.3 回调失效/抛出）
        ContextClosing,    ///< context-closing（§6.7 排空期取消——实现口径：
                           ///  执行中门卫 ContextClosed 的映射，DTB §5.4）
    };

    /// 终态类别。
    Kind kind = Kind::Failed;
    /// kind==Rejected 时的理由（他态无意义）。
    Rejection rejection = Rejection::InvalidPayload;
    /// kind==Aborted 时的理由（他态无意义）。
    Abort abort = Abort::Canceled;

    /// 便捷判定（调用方可读性——避免裸比对枚举三元组）。
    [[nodiscard]] bool committed() const noexcept { return kind == Kind::Committed; }
    [[nodiscard]] bool rejected() const noexcept { return kind == Kind::Rejected; }
    [[nodiscard]] bool aborted() const noexcept { return kind == Kind::Aborted; }
    [[nodiscard]] bool failed() const noexcept { return kind == Kind::Failed; }

    /// 值相等（三字段全等——C++17 无 defaulted 比较，手写全比对）。
    bool operator==(const CommandStatus& o) const noexcept
    {
        return kind == o.kind && rejection == o.rejection && abort == o.abort;
    }
    bool operator!=(const CommandStatus& o) const noexcept
    {
        return !(*this == o);
    }
};

/**
 * @brief 提交结果（§5.3.1 原文形态）——submit 的唯一返回通道。
 *
 * 背景说明：Committed 时 newRevision 唯一非空（§5.3.1 字段注释"Committed
 * 时唯一非空"——恰好一个新修订的后置在结果面的表达）；newHeadState 随
 * 附新修订视图（等于 query().tryRevision(newRevision) 的值快照，免二次
 * 查询）；findings 在 Rejected(确认相关) 时回传（调用方呈现未决项）；
 * diagnostics 承载处理器产出的硬断言/编译失败定位与实现产出的过期基线
 * 诊断（用户级稳定码——P-PR-6 链路：码值限于 diagnostics.md §4.6 收编
 * 清单）；error 仅 Failed 态携带环境/数据侧 StoreError（Rejected 的理由
 * 已由 status.rejection 机器可读表达，不重复包装）。
 *
 * 线程安全：纯值类型。
 */
struct CommandResult {
    /// 终态（见 CommandStatus）。
    CommandStatus status{};
    /// 新修订身份（rev-；status.committed() 时唯一非空）。
    std::optional<core::RevisionId> newRevision;
    /// 提交后 HEAD 修订视图（值快照；committed() 时携带）。
    std::optional<RevisionView> newHeadState;
    /// 未决确认集回传（Rejected(确认相关) 时——§5.3.1 字段注释）。
    std::vector<core::ConfirmableFinding> findings;
    /// 诊断记录（硬断言/编译失败定位＝处理器产出；过期基线＝实现产出
    /// ——PRJ-STALE-REVISION-REJECTED，§6.3）。
    std::vector<core::DiagnosticRecord> diagnostics;
    /// Failed 态的环境/数据侧错误（稳定码——§5.0 封闭集）；他态为空。
    std::optional<StoreError> error;

    /// 便捷判定（调用方可读性——委托 status；Rejected(确认相关) 等四态
    /// 判读是消费方高频动作）。
    [[nodiscard]] bool committed() const noexcept { return status.committed(); }
    [[nodiscard]] bool rejected() const noexcept { return status.rejected(); }
    [[nodiscard]] bool aborted() const noexcept { return status.aborted(); }
    [[nodiscard]] bool failed() const noexcept { return status.failed(); }
};

// =====================================================================
// §5.3.2 领域处理器类型
// =====================================================================

/**
 * @brief 处理器声明的对象写入（§5.3.2 原文形态）——CommandPlan 变更集
 *        的元素。
 *
 * 背景说明：ContentVersion 由 project 计算（SHA-256，经
 * codec::contentVersionOf——CR-02 唯一哈希入口），处理器不自行申报
 * （§5.3.2 字段注释）。objectId 为空＝申请新对象（project 在 S6 计划
 * 装配时分配——实现口径：ObjectId::generate；prepare 期内即需要身份
 * 入载荷的处理器经 HandlerContext::objectId() 取号，两条路径并存且
 * 同为"project 分配"语义）。
 *
 * 线程安全：纯值类型。
 */
struct ObjectWrite {
    /// 对象身份（obj-）；空＝申请新对象（project 分配——见结构注释）。
    std::optional<core::ObjectId> objectId;
    /// 对象类型 token（域登记的稳定 token，如 "RobotDesign"；随修订清单
    /// 登记透传——§4.4.2）。非空。
    std::string objectTypeToken;
    /// 域 canonical 负载（透传存储——D-10；长度 0 合法）。
    std::vector<std::uint8_t> payloadCanonical;
};

/**
 * @brief 显式元数据变更（§5.3.2 原文形态）——分支表/显示名等（CreateBranch
 *        等）。
 *
 * 背景说明：createBranchWithBase＝以该**既有分支**的当前 tip 为起点创建
 * 新分支（§4.5.1 走查的 CreateBranch 语义——新条目 baseRevisionId=
 * tipRevisionId=被引分支 tip，label 一次写入——P-PR-8）；新分支身份由
 * project 在 S6 计划装配时分配（BranchId::generate）。newDisplayName 为
 * R2 预留（本头"增量落位说明 1"——产出即 invalid-payload 拒绝）。
 *
 * 线程安全：纯值类型。
 */
struct MetadataChange {
    /// 以该分支 tip 为起点的建支声明（nullopt＝本次不变更分支表）。
    std::optional<core::BranchId> createBranchWithBase;
    /// 新分支显示名（createBranchWithBase 有值时必填；label 创建时一次
    /// 写入——P-PR-8，无改名入口）。
    std::string label;
    /// 项目显示名变更（R2 PM-11-S2 预留——见 CommandService.hpp 文件头
    /// "增量落位说明 1"；本落位面产出即拒绝）。
    std::optional<std::string> newDisplayName;
};

/**
 * @brief 命令计划（§5.3.2 原文形态）——处理器 prepare 的产出：本次提交
 *        要做什么。
 *
 * 背景说明：计划是**声明**，不是执行——project 编排 S4～S6 把计划落成
 * 修订（确认→编译→事务）。requiresDualCompile＝处理器声明需要双编译
 * （MDL-06 类命令——S5 编排，§6.6）；inverseCommandType＋
 * inversePayloadCanonical＝可逆性声明＋逆载荷（§6.9：可逆性是处理器
 * 声明的事实，project 不推断；不可逆命令如 CreateBranch 无 inverse）；
 * summary 随修订持久化（§4.4.4 CommandRecord.summary——PM-12-S1 历史
 * 浏览展示面）。
 *
 * 线程安全：纯值类型。
 */
struct CommandPlan {
    /// 变更集（新内容——新增/改版对象；可为空＝纯元数据命令）。
    std::vector<ObjectWrite> objectWrites;
    /// 显式元数据变更（可空）。
    std::optional<MetadataChange> metadataChange;
    /// 待确认集（SA-15——比较型可确认诊断；S4 编排，§6.7）。
    std::vector<core::ConfirmableFinding> confirmableFindings;
    /// 声明需要双编译（MDL-06——S5；编译端口未注入＝装配违约）。
    bool requiresDualCompile = false;
    /// 逆命令处理器注册 token（nullopt＝不可逆命令——§6.9）。
    std::optional<std::string> inverseCommandType;
    /// 逆命令 canonical 负载（与 inverseCommandType 成对出现）。
    std::optional<std::vector<std::uint8_t>> inversePayloadCanonical;
    /// 命令摘要（随修订持久化——人读；处理器生成，project 原样存储）。
    std::string summary;
};

/**
 * @brief prepare 的三种结果（§5.3.2 原文形态）。
 */
enum class PrepareOutcome {
    /// 计划已产出（out 有效——进入 S4/S5/S6）。
    Planned,
    /// 硬断言失败（MDL-06 就地阻止＋diags 精确定位非法对象）→
    /// Rejected(hard-assert-failed)，无修订。
    RejectedHardAssert,
    /// 非法输入（载荷结构域校验不过）→ Rejected(invalid-payload)，
    /// diags 逐项诊断（域口径——§6.3）。
    RejectedInvalidInput,
};

/**
 * @brief prepare 的执行上下文（§5.3.2 原文"HandlerContext"）——命令服务
 *        在 S3 构造、仅 prepare 调用期有效。
 *
 * 背景说明（提供面＝§5.3.2 原文清单）：objectId() 分配（新增对象的身份
 * 取号点——generate 语义）；query() 只读（处理器基于基线快照之外的补充
 * 读取——基线快照本身经 prepare 参数传入，二者同源）；编译端口访问器
 * （注入的 IModelCompilePort——处理器一般不直接调用编译，S5 由命令服务
 * 编排；访问器供声明性检查）；interaction()（确认回调——§5.3.3，处理
 * 器一般不需要直接交互，待确认集走 CommandPlan 声明面）。
 *
 * 生命周期与所有权：命令服务每 submit 构造一个，引用宿主端口与注入面
 * （非 owning）；不向处理器暴露任何写通道（prepare 只读＋产出计划）。
 *
 * 线程约束：仅在持有命令执行槽的 submit 调用线程使用（§6.1——S3 全程
 * 持槽）；不可复制（上下文绑定单次提交）。
 */
class HandlerContext {
public:
    /// 构造（命令服务内部——S3 装配；引用须在 prepare 调用期有效）。
    HandlerContext(IProjectQueryPort& queryPort,
                   IModelCompilePort* compilePort,
                   ICommandInteraction* interaction) noexcept;

    /// 分配新对象身份（obj- 规范文本；core generate——多线程安全）。
    [[nodiscard]] core::ObjectId objectId();

    /// 只读查询端口（②端口——§5.2；拒绝态随宿主上下文）。
    [[nodiscard]] IProjectQueryPort& query() const noexcept
    {
        return m_query.get();
    }

    /// 注入的双编译端口（可空＝未装配——requiresDualCompile 声明在
    /// 该态下属装配违约，S5 fail-fast）。
    [[nodiscard]] IModelCompilePort* compilePort() const noexcept
    {
        return m_compilePort;
    }

    /// 确认交互回调（可空＝非交互提交——§6.7 confirmations-unresolved）。
    [[nodiscard]] ICommandInteraction* interaction() const noexcept
    {
        return m_interaction;
    }

    HandlerContext(const HandlerContext&) = delete;
    HandlerContext& operator=(const HandlerContext&) = delete;

private:
    std::reference_wrapper<IProjectQueryPort> m_query;  ///< ②端口（非 owning）
    IModelCompilePort* m_compilePort;                   ///< 编译端口（可空）
    ICommandInteraction* m_interaction;                 ///< 确认回调（可空）
};

/**
 * @brief 领域处理器接口（§5.3.2 原文形态）——业务单元实现、L5 装配期
 *        注册（§6.5）；project 不链接业务单元（ARC-02）。
 *
 * 背景说明（prepare 契约——§5.3.2 注释原文）：基于 baseSnapshot
 * （expectedRevision 解析的修订视图）做业务校验并产出计划。硬断言失败
 * → RejectedHardAssert＋diagnostics（就地阻止＋精确定位非法对象，
 * MDL-06）。策略校验（如行程上限）在 prepare 内经处理器自持的④端口
 * 读 EngineeringPolicySet 阈值，超限产出 ConfirmableFinding（M-10）——
 * **project 不持有 4π 数值**（P-PR-3 处置：命令服务只编排断言阶段）。
 *
 * 线程约束：prepare 只在命令执行槽内被调用（同上下文串行）——实现方
 * 无需为同上下文的并发 prepare 设防；跨上下文共享处理器实例时须自行
 * 保证只读性（注册表消费的 handler 状态应视为不可变——§5.3.5"运行期
 * 只读"）。
 */
class ICommandHandler {
public:
    /// 虚析构：经注册表 unique_ptr 持有、多态销毁的常规保障。
    virtual ~ICommandHandler() = default;

    /// 处理器注册 token（§4.4.4 语法 ^[a-z0-9-]{3,64}；注册表内唯一）。
    [[nodiscard]] virtual std::string commandType() const = 0;

    /// 当前受理的载荷版本（无单位——格式版本号；受理集合口径见本头
    /// 文件头"增量落位说明 3"）。
    [[nodiscard]] virtual std::uint32_t currentPayloadVersion() const = 0;

    /**
     * @brief 业务校验并产出计划（§5.3.2 prepare 原文签名）。
     *
     * @param ctx          [in] 执行上下文（objectId 分配/查询/端口访问——
     *                     仅本调用期有效）
     * @param envelope     [in] 命令信封（负载字节域所有——此处开始解释）
     * @param baseSnapshot [in] 基线修订视图（expectedRevision 解析——
     *                     值快照，prepare 期间不受其他提交影响：命令槽
     *                     保证 §6.1）
     * @param out          [out] 计划产出（Planned 时有效；返回拒绝态时
     *                     实现可留空——project 不消费）
     * @param diags        [out] 逐项诊断（硬断言定位/非法输入——域口径；
     *                     随 CommandResult.diagnostics 回传）
     *
     * @return prepare 三态（见 PrepareOutcome）
     *
     * @throws 处理器实现内部异常不属本契约（防御性编程面）：未列抛出——
     *         实现缺陷按调用方错误透传（不吞不改——AGENTS §3 错误纪律）
     */
    virtual PrepareOutcome prepare(HandlerContext& ctx,
                                   const CommandEnvelope& envelope,
                                   const RevisionView& baseSnapshot,
                                   CommandPlan& out,
                                   std::vector<core::DiagnosticRecord>& diags) = 0;
};

// =====================================================================
// §5.3.3 确认交互回调
// =====================================================================

/**
 * @brief 确认交互回调（§5.3.3 原文形态）——ui 实现；命令服务在命令执行
 *        线程调用（SA-15；纯接口防 Widgets 依赖——本头零 Qt）。
 *
 * 背景说明（§5.3.3 契约原文）：isAlive()=false 或 requestConfirmations
 * 抛出 → Aborted(interaction-lost)，不产生修订；返回 nullopt＝整体拒绝
 * → Rejected(confirmations-rejected)；返回与输入一一对应的决策向量
 * （confirmed 凭据；任一缺失/不对应＝存在 rejected → 同上）。线程调度：
 * 实现内部 Marshal 到 UI 线程并等待结果；回调内**禁止**回调命令服务/
 * 查询端口以外任何写入口（防重入死锁——命令序列化槽被持有）。等待确认
 * 占用命令执行槽、零事务资源（§5.3.4）；会话关闭请求经 alive 检查＋
 * 取消通知终结等待——不存在永久等待（无超时自动确认，上游未定义超时
 * 语义）。
 *
 * 生命周期：服务在回调期间持实现对象的强引用语义由调用方装配保证——
 * 注入指针非 owning，其生存期须覆盖每次 submit 调用（§5.3.3"服务持
 * shared_ptr 强引用直至调用返回"的装配侧等价表达：调用方保证回调对象
 * 在 submit 返回前不销毁）。
 */
class ICommandInteraction {
public:
    /// 虚析构：ui 实现多态销毁的常规保障。
    virtual ~ICommandInteraction() = default;

    /// 会话存活判定（false＝界面会话已关闭——§5.3.3 interaction-lost）。
    [[nodiscard]] virtual bool isAlive() const = 0;

    /**
     * @brief 请求用户确认（§5.3.3 原文签名）。
     *
     * @param findings [in] 待确认集（比较型可确认诊断——SA-15）
     * @return 与输入一一对应的确认凭据向量（confirmed）；nullopt＝整体
     *         拒绝；向量与 findings 不一一对应＝存在 rejected（同拒绝
     *         处置——§5.3.3"返回空或任一 rejected"）
     */
    virtual std::optional<std::vector<core::ConfirmationCredential>>
        requestConfirmations(const std::vector<core::ConfirmableFinding>& findings) = 0;
};

// =====================================================================
// §5.3.6 双编译最小契约（runtime 交接；P-PR-7 单侧冻结）
// =====================================================================

/**
 * @brief 双编译请求（§5.3.6 原文形态——三字段冻结签名，不私改；
 *        P-PR-7 处置）。
 *
 * 背景说明：编译输入＝计划闭包（baseSnapshot 引用集＋plannedWrites 合
 * 成的对象集视图，经本请求提供给端口；§6.6）；runtime 只读快照语义
 * （不写项目）。baseRevision＝expectedRevision 解析的基线修订。临时
 * 目录不随本请求携带（§5.3.6 冻结面 vs §6.6 括注的冲突处置见本头
 * 文件头"增量落位说明 2"——临时落点约束由命令服务编排兑现，D-09）。
 *
 * 线程安全：纯值载体（引用成员——构造期绑定，禁止悬空使用）。
 */
struct CompileRequest {
    /// 只读查询端口（基线引用集的取数面——runtime 只读消费）。
    const IProjectQueryPort& query;
    /// 计划闭包（本次声明的对象写入全量）。
    const std::vector<ObjectWrite>& plannedWrites;
    /// 基线修订（rev-；expectedRevision 解析结果）。
    core::RevisionId baseRevision;
};

/**
 * @brief 双编译结果（§5.3.6 原文形态）。
 *
 * 背景说明（§5.3.6 语义原文）：WorkCell 与 DynamicWorkCell **任一失败
 * → ok==false → 不提交**（MDL-06 原子性）；诊断随 diagnostics 回传
 * （失败定位——经 CommandResult.diagnostics 交付调用方）。编译产物为
 * 瞬态（不持久化，修订仅含权威参数化对象）。
 *
 * 线程安全：纯值类型。
 */
struct CompileResult {
    /// true＝双编译（WorkCell＋DynamicWorkCell）全部通过。
    bool ok = false;
    /// 诊断（失败定位/告警——域口径，透传回传）。
    std::vector<core::DiagnosticRecord> diagnostics;
};

/**
 * @brief 双编译端口（§5.3.6 原文形态）——runtime 实现、L5 注入；project
 *        侧仅编排（P-PR-7 单侧冻结：ui/runtime 详设起草时以本契约为
 *        起点交叉核对，对端另立形态前不私改签名）。
 *
 * 背景说明（阶段 A 口径——§5.3.6 原文）：以测试桩实现验证编排（S5 事
 * 务编排语义——PRJ-TX-3）；真实编译链随 runtime/modeling 交付（§13
 * 交接）。编译类命令可与界面并行，但不与下一命令并发（ARCH §4.2——
 * 编译发生在命令执行槽内）。
 */
class IModelCompilePort {
public:
    /// 虚析构：runtime 实现多态销毁的常规保障。
    virtual ~IModelCompilePort() = default;

    /**
     * @brief 双编译（WorkCell＋DynamicWorkCell）。
     *
     * @param request [in] 编译请求（计划闭包＋基线——引用须在调用期有效）
     * @return ok=false ＝任一半边失败（不提交——MDL-06）
     */
    virtual CompileResult compileWorkCellAndDwc(const CompileRequest& request) = 0;
};

// =====================================================================
// §5.3.5 处理器注册表
// =====================================================================

/**
 * @brief 处理器注册表（§5.3.5 原文形态）——L5 装配期一次性注册，运行期
 *        只读。
 *
 * 背景说明（§6.5 防反向链接的实现）：L5 应用壳装配期实例化各业务处理
 * 器（modeling 的 ApplyRobotDesign、selection 的 ApplySelection、project
 * 自有的元数据命令处理器等）并注册进本表；命令服务 S1 经 find() 解析
 * token；查询端口的 hasUnresolvedPayload 判据（§5.2/§6.3 末行）同样消
 * 费本表（历史修订中的未知命令类型 → 修订可读、payload 不解析）。
 *
 * 错误语义：重复 commandType 注册＝**边界拒绝**（§5.3.5 注释原文）——
 * 装配期调用方错误，std::invalid_argument fail-fast（实现口径，DTB
 * §5.4：注册表无稳定码面——§5.0 封闭集无对应码，不私扩）。
 *
 * 线程安全：registerHandler 仅装配期调用（与运行期并发使用是装配纪律
 * 违约）；find()/registeredCommandTypes() 并发安全（内部互斥——find
 * 被查询线程消费）。
 */
class HandlerRegistry {
public:
    /// 构造空注册表（实现于 .cpp——pimpl 的完整类型在那里可见）。
    HandlerRegistry();

    /// 析构（实现于 .cpp——持有 unique_ptr<ICommandHandler> 的多态销毁）。
    ~HandlerRegistry();

    HandlerRegistry(const HandlerRegistry&) = delete;
    HandlerRegistry& operator=(const HandlerRegistry&) = delete;

    /**
     * @brief 注册处理器（所有权移交注册表——unique_ptr 持有至析构）。
     *
     * @param handler [in] 处理器实现（非空；同 token 重复注册→
     *                std::invalid_argument；token 空串同样 fail-fast——
     *                装配错误尽早暴露）
     *
     * @throws std::invalid_argument handler 为空或 token 为空、token 重复
     */
    void registerHandler(std::unique_ptr<ICommandHandler> handler);

    /**
     * @brief 按 token 查找处理器（§5.3.5 原文签名；noexcept）。
     *
     * @param commandType [in] 注册 token（原文视图——查找内不落盘）
     * @return 处理器指针（未注册＝nullptr——S1 据此 unknown-command）
     *
     * 线程安全：并发安全（内部互斥）。
     */
    [[nodiscard]] ICommandHandler* find(std::string_view commandType) const noexcept;

    /**
     * @brief 已注册 token 清单（§5.3.1 ProjectCommandService::
     *        registeredCommandTypes 的数据源——同一注册表的投影，不设
     *        第二状态；确定性排序＝字典序——NFR-COR-02。增量落位口径：
     *        §5.3.5 代码块未列本方法，枚举面为 §5.3.1 端口契约的必要
     *        支撑，落位于此避免命令服务复制注册状态）。
     *
     * 线程安全：并发安全（内部互斥）。
     */
    [[nodiscard]] std::vector<std::string> registeredCommandTypes() const;

    /// 已注册 token 数（观测面——测试与装配自检）。
    [[nodiscard]] std::size_t size() const noexcept;

private:
    /// 注册表本体（token→处理器；互斥保护——find 跨线程）。
    struct Impl;
    std::unique_ptr<Impl> m_impl;  ///< pimpl——公共头不承载容器细节（R-2 精神）
};

// =====================================================================
// §5.3.1 命令端口（①端口所有者）
// =====================================================================

/**
 * @brief 命令端口（ProjectCommandService，①端口所有者，§5.3.1）——唯一
 *        写路径入口（经 ProjectStore::commands() 获取实例引用）。
 *
 * 生命周期与所有权：实现实例由存储上下文持有并随其消亡（§5.1 端口
 *   访问器同款——query() 先例）；消费方（ui/workflow/业务域）只持引用
 *   调用 submit。
 *
 * 错误语义总表（错误二分——AGENTS §3/§5.0；逐项出处见 §6）：
 *   - 边界拒绝（无修订）→ CommandResult.status==Rejected（§6.1 S1/S2、
 *     §6.2、§6.3 表——unknown-command/invalid-payload/hard-assert-failed/
 *     stale-revision/not-writable）；
 *   - 中止（无修订）→ Aborted（§5.3.3 interaction-lost、§6.7 canceled/
 *     context-closing）；
 *   - 环境失败（无修订；暂存残留按 §7.4 恢复口径）→ Failed＋error
 *     （§6.6 compile-failed、S6 磁盘/权限稳定码——executeCommit 门卫与
 *     TxEngine 契约透传）；
 *   - 成功 → Committed{newRevision}——**恰好一个新修订**（§5.3.1 后置；
 *     S7 事件发布失败不回滚——D-18，提交事实不受影响）。
 *
 * 线程安全：submit 可从任意线程并发调用——内部经命令执行槽串行（§6.1
 *   "每存储上下文一个命令执行槽（互斥）"；PRJ-TX-1"第二个等待"）。
 */
class ProjectCommandService {
public:
    /// 虚析构：实现随存储上下文消亡（unique_ptr 成员多态销毁）。
    virtual ~ProjectCommandService() = default;

    /**
     * @brief 唯一写路径入口（§5.3.1 原文签名；S1～S7 生命周期——§6.1）。
     *
     * 执行序（各步失败即返回，全部拒绝/中止/失败路径**零修订**——
     * §5.3.1 表后置）：
     *   [S1] 形式校验：type 已注册（否则 unknown-command）→ payload
     *        版本受理（否则 invalid-payload）→ branch 存在于权威元数据
     *        （否则 invalid-payload——实现口径，DTB §5.4）→ writable
     *        （否则 not-writable＋对应 §5.0 门卫稳定码诊断）；
     *   [S2] 基线解析与并发校验：expectedRevision 缺省＝分支 tip（提交
     *        期解析）；显式 ≠ tip → stale-revision＋
     *        PRJ-STALE-REVISION-REJECTED（§6.2）；
     *   [S3] 处理器 prepare（业务校验＋计划——§5.3.2）；
     *   [S4] 确认放行（待确认集非空——§5.3.3/§6.7：非交互＝
     *        confirmations-unresolved；拒绝＝confirmations-rejected；
     *        失效/抛出＝interaction-lost）；
     *   [S5] 双编译（requiresDualCompile——§6.6：任一失败→
     *        Failed(compile-failed)＋诊断，无修订、暂存区未创建）；
     *   [S6] 事务提交（七步协议 §7.1——经存储上下文唯一写通道）→
     *        恰好一个新修订（seq 单调——O-15）；
     *   [S7] 事件发布（RevisionCommitted→DependencyInvalidated——在
     *        事务第 6 步内；失败不回滚——D-18）。
     *
     * @param envelope    [in] 命令信封（负载三元组——§6.4）
     * @param interaction [in] 确认回调（可空＝非交互提交——§6.7；
     *                    非 owning，生存期覆盖本调用）
     *
     * @return 提交结果（四态——见类注释错误语义总表）
     *
     * @throws std::invalid_argument 装配违约（requiresDualCompile 但编译
     *         端口未注入——fail-fast；实现口径，DTB §5.4）
     *
     * 线程安全：任意线程并发调用（内部命令执行槽串行——第二个等待，
     * §6.1）。
     */
    virtual CommandResult submit(const CommandEnvelope& envelope,
                                 ICommandInteraction* interaction = nullptr) = 0;

    /**
     * @brief 已注册命令 token 清单（§5.3.1 原文签名 noexcept；确定性
     *        排序＝字典序——NFR-COR-02）。
     *
     * noexcept 契约下的分配失败语义：结果向量小（注册 token 数量级），
     * 分配失败即 terminate——与冻结签名一致（实现口径，DTB §5.4）。
     */
    [[nodiscard]] virtual std::vector<std::string> registeredCommandTypes()
        const noexcept = 0;
};

}  // namespace sdurws::ird::project

#endif  // SDURWS_IRD_PROJECT_COMMANDSERVICE_HPP
