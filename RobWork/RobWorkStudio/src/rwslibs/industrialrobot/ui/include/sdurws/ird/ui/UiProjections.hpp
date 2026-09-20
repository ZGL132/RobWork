/**
 * @file   UiProjections.hpp
 * @brief  ui 单元值投影承载——对端能力经 ui 自有端口进入时的只读值形态
 *         （O-31 裁决 2026-09-19 的 ui 侧载体）。
 *
 * 设计依据：
 *   - units/ui.md §3.3（公共头布局：UiProjections.hpp＝"值投影承载……§3.2
 *     冻结基准的 ui 侧载体"）、§3.2（消费的上游类型清单——投影值形态冻结
 *     基准：投影 schema 逐字段语义以对端锚点为唯一权威，不重定义权威类型、
 *     不另立语义——NFR-MNT-03 单一权威不变，投影≠重定义）；
 *   - O-31 裁决（DTB §4 登记行＋ui.md v0.4 §3.1/§10 引导注）：对
 *     project/evidence/execution/policy/runtime 不增边，对端类型一律经 ui
 *     自有端口以值投影形态进入 ui；投影具体形状随首个消费该面的 UI 任务
 *     实现冻结并做单元卡增量修订登记（本任务＝UI-T03，登记于 ui.md §10.1）；
 *   - 需求 PM-11（标题/状态栏格式 `<显示名>[*][（只读）]`——formatProject
 *     StatusText 为该格式的唯一权威实现）、PM-07（只读呈现）、PM-10（最近
 *     项目条目）、PM-14（用户级设置承载的值面）；
 *   - UI-T04 增量（登记 ui.md §16.7 v0.6）：七态公共状态呈现承载——
 *     StatusWord 词表与 StatusWordProjection（§6.3 冻结稿）、七态触发数据源
 *     值投影（当前性/证据清单/正式通过资格/就绪/任务活动——§3.2 冻结基准的
 *     ui 侧载体，O-31 裁决）、九态短标签文案键（PM-03/PM-11）、§6.3/§6.8
 *     显示纪律数据面；
 *   - UI-T06 增量（登记 ui.md §16.7 v0.8）：C-4 命令信封/命令结果投影
 *     （CommandEnvelopeProjection/CommandResultProjection——§10.3
 *     CommandOutcome.revisionResult 的 O-31 承载形态，任务契约 acceptance 3：
 *     "经 ui 自有命令网关端口＋命令结果值投影承载"，产品面零对端
 *     include）；
 *   - UI-T07 增量（登记 ui.md §16.7 v0.9）：工程策略摘要投影逐字段细化
 *     （PolicyThresholdProjection＋PolicySummaryProjection 逐阈值字段展开
 *     ——"首消费细化"机制兑现：UI-T03 计数形态改由逐字段派生，聚合不漂移；
 *     C-10 端口返回值的呈现面，冻结基准 policy.md §4.3/§4.4/§10.6）。
 *
 * 背景说明（为什么状态栏输入是"投影"而不是 project 头文件里的类型）：
 *   ui 产品面对 project 单元零链接零 include（O-31/ARCH §3.5），而状态栏/
 *   顶栏需要"项目显示名、可写性、存在未应用草稿"等事实。这些事实的语义
 *   权威仍在 project（ProjectMetadataView.projectDisplayName、DraftProjection.
 *   present 等，锚点见各字段注释），ui 只定义**承载同一语义的值类型**，
 *   由 L5 应用壳装配期把 project 查询结果翻译成本投影后注入 ui（§10.1
 *   ShellWiring 注入面）。语义锚点变更时投影随单元卡增量修订同步。
 *
 * 线程安全：全部纯值类型（并发只读安全）。formatProjectStatusText 纯函数
 * （同输入同输出——NFR-COR-02）。
 */

#ifndef SDURWS_IRD_UI_UIPROJECTIONS_HPP
#define SDURWS_IRD_UI_UIPROJECTIONS_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Evaluation.hpp>  // core::TaskState/TaskOutcome/EngineeringStatus（表内登记边：任务轴九态/结局/工程判定词表）
#include <sdurws/ird/core/Identity.hpp>    // core::ProjectId（P-PR-1 同案：身份类型与 core 契约同一，不本地重定义）
#include <sdurws/ird/diagnostics/Catalog.hpp>  // diagnostics::DiagProjectionItem（表内登记边：failed 态"附定位与修复建议"的呈现载体）

namespace sdurws::ird {
namespace ui {

// =====================================================================
// 项目元数据投影（C-5 语义承载——冻结基准 project.md §5.2 ProjectMetadataView）
// =====================================================================

/**
 * @brief 项目元数据的 ui 侧只读投影（工作台壳状态栏/顶栏/命令门控的输入）。
 *
 * 语义锚点（不改义，NFR-MNT-03）：
 *   - projectDisplayName ← ProjectMetadataView 的显示名字段（project.md
 *     PersistenceFormat §4.4.3 冻结记录字段）；UX-02 工程用语：显示给用户
 *     的名称，零哈希/零内部标识。
 *   - writable ← 打开模式与锁状态的合成事实（project.md §5.1 OpenMode/
 *     LockInfo；PM-07 只读打开）：false＝只读会话——状态栏追加"（只读）"
 *     后缀、全部写命令按 §7.6 只读条件禁用。
 *   - projectId ← core::ProjectId（与 core 契约类型同一——P-PR-1 先例）。
 *
 * 值语义；投影不可变使用（调用方持有快照，ui 不回写——§10.1"接口不交出
 * 内部可变状态"同口径）。
 */
struct ProjectMetadataProjection {
    /// 项目身份（core 契约类型同一；仅作关联键呈现，ui 不解析其内容）。
    core::ProjectId projectId;
    /// 项目显示名（PM-11 格式首段；UTF-8）。
    std::string projectDisplayName;
    /// 可写性（true＝可写会话；false＝只读——PM-07 呈现与 §7.6 命令门控输入）。
    bool writable = false;
};

// =====================================================================
// 草稿存在性投影（C-5 语义承载——冻结基准 project.md §5.4/§8.5 DraftProjection）
// =====================================================================

/**
 * @brief "存在未应用草稿"的 ui 侧投影（PM-11 标题 `*` 标记的唯一输入）。
 *
 * 语义锚点：present ← DraftProjection 的逐模块 present 位任一为真
 * （project.md §8.5：多模块汇总投影；"会话级'未应用修改'标记（标题 *，
 * PM-11）由 ui 基于投影中 present 项计算"——本类型即该计算的两输入之一）。
 * sessionDirty 为 ui 会话态脏标记（编辑后未落盘的会话内事实，§4.2 状态栏
 * 行"present ∨ 会话脏标记"）。
 *
 * 为什么是两个独立位而不是一个 bool：present 是磁盘事实（project 权威），
 * sessionDirty 是会话内事实（ui 自有）——来源不同、生命周期不同（会话
 * 脏标记随切换/关闭清除，磁盘投影随 DraftService 变化），合并会丢失
 * "脏从何来"的可观测性（§8.4 定时落盘失败保留脏标记的场景依赖此区分）。
 */
struct DraftPresenceProjection {
    /// 磁盘上存在未应用草稿（DraftProjection.present 任一模块为真——§8.5）。
    bool present = false;
    /// 会话脏标记（编辑发生且尚未落盘/应用——ui 会话态，§4.2）。
    bool sessionDirty = false;

    /// @brief PM-11 判定：是否存在未应用修改（present ∨ sessionDirty）。
    bool anyUnapplied() const noexcept { return present || sessionDirty; }
};

// =====================================================================
// 工作台上下文投影（状态栏/首页/命令门控的复合输入——§4.2/§4.3/§7.5）
// =====================================================================

/**
 * @brief 工作台当前项目上下文的复合投影（WorkbenchShell::presentProject
 *        Context 的注入口值类型）。
 *
 * project 为 nullopt＝无项目（PM-10 无项目首页态：中央区首页、七阶段导航
 * 禁用、项目作用域命令 enabled=false、命令面板仍可达——§4.3/§7.4）；有值
 * ＝项目打开态（状态栏进入 PM-11 格式、中央区切换阶段面板栈）。
 *
 * 说明（为什么上下文是一个复合值而不是逐字段 setter）：一次注入原子生效，
 * 避免元数据与草稿位中间态外泄到界面（状态栏格式三段必须取同一时刻的
 * 一致快照——PM-11 的 * 与（只读）语义分别来自不同来源，分次注入会出现
 * "显示名是新项目、可写性是旧项目"的拼接态）。
 */
struct ProjectContextProjection {
    /// 项目元数据投影（nullopt＝无项目——首页态，PM-10）。
    std::optional<ProjectMetadataProjection> project;
    /// 草稿存在性投影（无项目时被忽略——首页态无草稿语义）。
    DraftPresenceProjection drafts{};
};

// =====================================================================
// 最近项目条目（PM-10/PM-14——用户级设置承载的值面）
// =====================================================================

/**
 * @brief 最近项目列表条目（无项目首页"最近项目"入口的数据源）。
 *
 * 语义锚点（PM-10）：上限 10、按规范路径去重、失效项**保留**并提示
 * "项目位置不可用"。available 在每次读取列表时按当前文件系统事实计算
 * （失效≠删除——用户可能重新挂载/移动后经"重新选择"恢复）。
 */
struct RecentProjectEntry {
    /// 规范化项目路径（去重键；filesystem::weakly_canonical 形态，UTF-8）。
    std::string canonicalPath;
    /// 位置当前是否可用（false＝失效项——呈现"项目位置不可用"提示，PM-10）。
    bool available = true;
};

// =====================================================================
// 命令信封与命令结果投影（C-4 语义承载——冻结基准 project.md §5.3.1；
// UI-T06 首消费冻结，O-31 裁决载体：ui.md §16.7 v0.4 §10 引导注）
// =====================================================================
//
// 本节背景（O-31 处置——任务契约 UI-T06 acceptance 3）：
//   ui.md §10.3 CommandOutcome.revisionResult 的原文形态是
//   project::CommandResult（C-4 对端类型）。按 O-31 裁决（DTB §4，
//   2026-09-19 所有者裁决"ui 侧最小注入接口模式"），对端类型不直接进入
//   ui 头文件——本节以**值投影**承载同一语义（投影≠重定义：字段语义逐条
//   锚定 project.md §5.3.1，NFR-MNT-03 单一权威不变；reporting
//   ModelSummary／runtime §13.2 同案）。产品面对 project 零链接零 include
//   （NoCrossUnitInclude_O31_UI_BUILD 守卫常驻自证）；协作经 ui 自有命令
//   网关端口 IUiCommandGateway（UiPorts.hpp）注入，L5 应用壳装配期以单行
//   适配器把 project::ProjectCommandService::submit 翻译成本投影。

/**
 * @brief 命令信封的 ui 侧投影（C-4 提交面——冻结基准 project.md §5.3.1
 *        CommandEnvelope，O-31 值投影、L5 适配）。
 *
 * 字段语义锚点（不改义，NFR-MNT-03；与对端 CommandEnvelope 逐字段对应）：
 *   - branch＝目标分支（core::BranchId——表内身份类型直用，§3.2）；
 *   - expectedRevision＝期望基线修订（nullopt＝该分支当前 tip，提交期
 *     解析——§5.3.1 原文；PM-04 草稿应用时取 draft.baseRevisionId，
 *     RV-10 StaleRevisionRejected 的判据输入）；
 *   - commandType＝处理器注册 token（project HandlerRegistry 词形）；
 *   - payloadFormatVersion/payloadCanonical＝载荷版本与域 canonical 字节
 *     （不透明——ui 不解析域载荷，只搬运；§5.3.1 原文"处理器不自行申报
 *     ContentVersion"由对端计算）。
 *
 * 值语义；ui 侧组装后只读使用（信封提交后不回写——命令修订语义权威在
 * project，ui 零业务判定，§7.5 红线）。
 */
struct CommandEnvelopeProjection {
    /// 目标分支（core 表内身份类型——与 core 契约同一，不本地重定义）。
    core::BranchId branch{};
    /// 期望基线修订（nullopt＝分支 tip——§5.3.1 缺省语义）。
    std::optional<core::RevisionId> expectedRevision;
    /// 处理器注册 token（如建支/元数据命令族 token——project 词表）。
    std::string commandType;
    /// 载荷格式版本（处理器受理版本——§5.3.1；失配＝invalid-payload）。
    std::uint32_t payloadFormatVersion = 0;
    /// 域 canonical 字节（不透明载荷——ui 只搬运不解析）。
    std::vector<std::uint8_t> payloadCanonical;
};

/**
 * @brief 命令结果的 ui 侧投影（C-4 返回面——冻结基准 project.md §5.3.1
 *        CommandResult，O-31 值投影、L5 适配）。
 *
 * 字段语义锚点（不改义）：
 *   - status 四值与对端 CommandStatus 词表一一对应（Committed/Rejected/
 *     Aborted/Failed——§5.3.1 variant 注记）；
 *   - newRevision＝新修订身份（**Committed 时唯一非空**——§5.3.1 原文；
 *     失败/拒绝/中止不产生任何修订）；
 *   - rejectionReason/abortReason＝对端 reason 稳定 token 的投影承载
 *     （Rejected：hard-assert-failed/confirmations-rejected/stale-revision/
 *     unknown-command/invalid-payload/not-writable；Aborted：canceled/
 *     interaction-lost/context-closing——§5.3.1 variant 注记原文；token
 *     不镜像为 ui 枚举，避免双权威词表 NFR-MNT-03，CurrentnessProjection.
 *     Reason.kindToken 同案）；
 *   - findings/diagnostics/error 明细**不在本投影**：确认对话数据面随
 *     UI-T13（CommandInteractionBridge/确认呈现任务）按"首消费冻结"机制
 *     增量登记（ui.md v0.4 §10 引导注的既定机制）——本任务（UI-T06）的
 *     消费面只有"命令完成呈现"（§7.7 ④）所需的 status/newRevision/reason。
 *
 * 值语义；投影不可变使用（调用方持有快照，ui 不回写 project）。
 */
struct CommandResultProjection {
    /// 结果四值（与对端 CommandStatus 词表一一对应——§5.3.1）。
    enum class Status : std::uint8_t {
        Committed, ///< 已提交（恰好产生一个新修订——newRevision 非空）
        Rejected,  ///< 被拒绝（rejectionReason 携带 reason token；无修订）
        Aborted,   ///< 被中止（abortReason 携带 reason token；无修订）
        Failed,    ///< 执行失败（对端 StoreError 明细归 UI-T13 增量面；无修订）
    };

    /// 结果状态（默认 Rejected＝"未提交"的安全空值——非 Committed 即无修订）。
    Status status = Status::Rejected;
    /// 新修订身份（仅 status==Committed 非空——§5.3.1 后置条件原文）。
    std::optional<core::RevisionId> newRevision;
    /// 拒绝原因 token（status==Rejected 时非空；词表见类型注释）。
    std::string rejectionReason;
    /// 中止原因 token（status==Aborted 时非空；词表见类型注释）。
    std::string abortReason;
};

// =====================================================================
// 工程策略摘要投影（C-10 语义承载——冻结基准 policy.md §9.1/§10.6；
// ui.md §6.7 策略摘要只读卡）
// =====================================================================

/**
 * @brief 单个策略阈值字段的 ui 侧投影（逐字段细化——UI-T07 首消费冻结，
 *        登记 ui.md §16.7 v0.9）。
 *
 * 语义锚点（不改义，NFR-MNT-03）：对端类型为 policy::PolicyThreshold
 * （policy.md §4.4：SI 真值＋来源，全部字段构造后不可变）。本投影只承载
 * 呈现所需的"有值/无值＋SI 数值"两要素：
 *   - present==true：siValue 为该字段的 SI 真值（单位由字段位置固定——
 *     安全间距 m／行程上限 rad／判定阈值无量纲，policy.md §4.4"单位由
 *     字段位置固定"原文；显示单位换算属呈现投影，见
 *     formatPolicyThreshold——KIN-12 同案，不改真值不进身份）；
 *   - present==false：该字段"显式不适用"（对端 std::optional 空——
 *     policy.md §4.5 四态承载之"不适用"态＋P-POL-2/O-10"不发明数值"
 *     保守口径的直读投影；呈现层必须显示「显式不适用」，禁止补默认值）。
 *
 * 为什么不带 origin（来源）字段：§4.4 来源词表的编码 token 尚未随 codec
 * 冻结（对端 PolicyValueOrigin 的持久化形态未定），ui 侧先行投影会制造
 * 第二词表权威（NFR-MNT-03）；摘要卡面向的呈现问题（"阈值是多少、适用
 * 与否"）不依赖来源，来源呈现随 codec token 冻结后的增量修订补入。
 *
 * 值语义；ui 侧只读使用（投影≠重定义——字段语义权威在 policy.md §4.4）。
 */
struct PolicyThresholdProjection {
    /// 阈值是否显式提供（false＝显式不适用——呈现「显式不适用」，不发明数值）。
    bool present = false;
    /// SI 真值（present==true 时有效；单位由字段域固定——m/无量纲/rad）。
    double siValue = 0.0;
};

/**
 * @brief 工程策略只读摘要的 ui 侧投影（IPolicySummarySource 的返回值）。
 *
 * 语义锚点（ui.md §6.7）：数据＝EngineeringPolicySet 公开字段投影（碰撞
 * 域启用/安全间距/过滤对计数/行程上限阈值/判定阈值，含"显式不适用"态）；
 * **不改变策略内容身份**（投影≠重定义——权威字段语义在 policy.md，本类型
 * 只承载呈现所需的开关位、阈值与计数）。
 *
 * UI-T07 逐字段细化（登记 ui.md §16.7 v0.9——"逐字段展开随 UI-T07 消费
 * 时按增量修订登记细化"机制的兑现，UI-T03 计数形态由此被取代）：
 *   - UI-T03 的 thresholdCount/explicitNotApplicableCount 数据成员改为
 *     由逐字段投影**派生**的同名 const 方法（聚合语义可复现、单一来源
 *     ——逐字段是唯一事实，聚合不再可能与之漂移）；filterPairCount 保留
 *     数据形态（对端 excludedPairs 向量规模的直读投影）并补
 *     mandatoryPairCount（必检对——与过滤对同源对偶，摘要行"必检 n 对
 *     ·过滤 m 对"的另一半）；
 *   - enabledDomainTokens 逐项为对端 CollisionDomain 的冻结 token
 *     （policy.md §4.3：self/environment/tool/scene——词表锚点同
 *     CurrentnessProjection::Reason::kindToken 的"对端稳定 token 直用、
 *     不镜像枚举"口径）；
 *   - 判定阈值＝nearLimitRatio/conditionNumberWarning（policy.md §4.4
 *     字段行——警告判定阈值族）；行程上限＝finiteRotationTravelLimit
 *     （已发布集合恒有值——4π 是唯一冻结默认，origin=DefaultAppendixD）。
 *
 * 值语义；ui 侧只读使用（ui 不回写策略——§6.7 摘要只读红线，修改一律
 * 走编辑适配器→①命令端口路径）。
 */
struct PolicySummaryProjection {
    /// 策略源是否可解析（false＝未装载/不可用——呈现层显示占位，不虚构数值）。
    bool available = false;
    /// 碰撞域启用位（EngineeringPolicySet.collision.enabled 的直读投影；
    /// §6.7"碰撞域启用"原文——这是**计算开关**，呈现于"工程策略（计算
    /// 权威）"组，与会话显示设置严格分组异名，POL-ID-3）。
    bool collisionDomainEnabled = false;
    /// 启用域 token 清单（对端 CollisionDomain 冻结 token：self/environment/
    /// tool/scene——policy.md §4.3；enabled==false 时适配器置空清单）。
    std::vector<std::string> enabledDomainTokens;
    /// 安全间距阈值（collision.safetyClearance 直读投影；SI m，[0,+∞)——
    /// 碰撞停用时可为显式不适用，policy.md §4.3）。
    PolicyThresholdProjection safetyClearance;
    /// 近限位比警告阈值（jointThresholds.nearLimitRatio 直读投影；无量纲，
    /// (0,1]；nullopt＝该检查显式不适用——P-POL-2 不发明数值）。
    PolicyThresholdProjection nearLimitRatio;
    /// 条件数警告阈值（jointThresholds.conditionNumberWarning 直读投影；
    /// 无量纲，[1,+∞)；nullopt 语义同上）。
    PolicyThresholdProjection conditionNumberWarning;
    /// 行程上限阈值（jointThresholds.finiteRotationTravelLimit 直读投影；
    /// SI rad，(0,+∞)——已发布集合恒有值：4π 是唯一冻结默认）。
    PolicyThresholdProjection travelLimit;
    /// 必检对计数（collision.mandatoryPairs 规模——"不可被过滤覆盖"的对，
    /// policy.md §4.3/§7.2）。
    std::size_t mandatoryPairCount = 0;
    /// 过滤对计数（collision.excludedPairs 规模——§6.7"过滤对计数"原文；
    /// 允许忽略对，理由必填可追溯）。
    std::size_t filterPairCount = 0;

    /**
     * @brief 已提供阈值字段计数（四个阈值槽位中 present==true 的个数）。
     *
     * UI-T03 聚合语义的派生复现：行程上限在已发布集合恒有值，故
     * available 集合该值 ≥1；本方法不读 available——聚合只由逐字段事实
     * 决定（未装载投影各槽位默认 present=false，自然得 0）。
     */
    std::size_t thresholdCount() const noexcept
    {
        std::size_t count = 0;
        if (safetyClearance.present) { ++count; }
        if (nearLimitRatio.present) { ++count; }
        if (conditionNumberWarning.present) { ++count; }
        if (travelLimit.present) { ++count; }
        return count;
    }

    /**
     * @brief "显式不适用"阈值字段计数（四个阈值槽位中 present==false 的
     *        个数——§6.7 括注的汇总呈现源；ERR-01"不适用"逐字段显式化，
     *        不与"未装载"混淆）。
     */
    std::size_t explicitNotApplicableCount() const noexcept
    {
        return static_cast<std::size_t>(4) - thresholdCount();
    }
};

// =====================================================================
// PM-11 状态栏格式唯一权威（`<显示名>[*][（只读）]`）
// =====================================================================

/**
 * @brief 计算状态栏/标题栏的项目状态文本（PM-11 格式的唯一权威实现）。
 *
 * 格式（需求 PM-11 原文）：`<显示名>[*][（只读）]`
 *   - 显示名 ← ProjectMetadataProjection.projectDisplayName；
 *   - `*` ＝存在未应用草稿（DraftPresenceProjection：present ∨ sessionDirty，
 *     §4.2 状态栏行原文"DraftProjection.present ∨ 会话脏标记"）；
 *   - `（只读）`（全角括号）＝writable=false。
 * 无项目时返回"未打开项目"（首页态状态行——PM-10 无项目呈现的最小说明，
 * 不虚构项目事实）。
 *
 * 为什么是公共纯函数而不是 WorkbenchShell 私有逻辑：PM-11 格式是 ui 单元
 * 对需求承接的契约点（§2.2"PM-11＝主责"），单一权威实现便于模型层测试
 * （无需 GUI 环境即可逐行断言格式矩阵）并供后续标题栏/报告侧复用。
 *
 * @param context [in] 工作台上下文投影（一次一致快照——见类型注释）
 * @return 状态栏文本（UTF-8；纯函数——同输入同输出，NFR-COR-02）
 */
inline std::string formatProjectStatusText(const ProjectContextProjection& context)
{
    // 无项目分支：首页态固定说明文本（PM-10；无显示名可格式化）。
    if (!context.project.has_value()) {
        return "未打开项目";
    }
    // 首段＝显示名（PM-11 格式首元素）。
    std::string text = context.project->projectDisplayName;
    // 第二段＝未应用修改标记：present（磁盘草稿）或 sessionDirty（会话脏）
    // 任一为真即追加 `*`（半角星号——PM-11 格式原文字面）。
    if (context.drafts.anyUnapplied()) {
        text += '*';
    }
    // 第三段＝只读后缀：writable=false 时追加全角"（只读）"（PM-07 呈现；
    // 全角括号为需求格式原文，勿改半角）。
    if (!context.project->writable) {
        text += "（只读）";
    }
    return text;
}

// =====================================================================
// 七态公共状态呈现（UI-T04；UX-06/UX-10/PM-11——ui.md §6.3 冻结稿承载）
// =====================================================================
//
// 本节背景（为什么"状态词"是投影而不是判定）：
//   UX-10 要求全工作台用统一状态词呈现计算处境；词表唯一所有者是 ui
//   （NFR-MNT-03，O-11），但 ui 是纯呈现单元（N-5/N-12：不拥有工程判定、
//   不拥有业务真值）——七态因此只是**呈现投影**：每个触发数据源都来自
//   权威方（会话态/execution/evidence/diagnostics），ui 只按 §6.3 冻结的
//   求值优先级选取"该呈现哪个词"，不新增任何状态语义（R-2 红线：禁止
//   下游拿呈现态反推业务判定）。求值入口是纯函数 evaluateStatusWord
//   （同输入同输出——NFR-COR-02），供 UiProjectionStore（§10.6）/阶段
//   投影（UI-T09）在 UI 线程调用。

/**
 * @brief UX-10 七态状态词表（§6.3 冻结 token——ui 唯一所有者，P-UI-1
 *        处置：冻结确认闭合前按本稿实现、不改词表）。
 *
 * 枚举序＝§6.3 词表行序（**不是**求值优先级序——优先级是另一条冻结规则，
 * 见 statusWordPriority）。token 为小写连字符（与 core 词表同风格），
 * 经 statusWordToken 取得；呈现文案键经 statusWordLabelKey 取得。
 */
enum class StatusWord : std::uint8_t {
    EmptyProject,     ///< 空项目（token "empty-project"；§6.3 词表行 1）
    Incomplete,       ///< 未完成（token "incomplete"；就绪投影无效——输入级）
    Computing,        ///< 计算中（token "computing"；存在非终态任务）
    ResultsStale,     ///< 结果过期（token "results-stale"；含 NotEvaluable 呈现归入——P-UI-2）
    DataInsufficient, ///< 数据不足（token "data-insufficient"；判定级证据/工况缺失）
    Failed,           ///< 失败（token "failed"；最近任务失败或 Error 级诊断活跃）
    Computable,       ///< 可计算（token "computable"；就绪有效且无更高优先状态）
};

/**
 * @brief 当前性"不可判定"原因词表（§6.3 P-UI-2 呈现承载）。
 *
 * 语义锚点（不改义）：evidence 的当前性计算只有 Current/Superseded 两个
 * **持久语义状态**；"无法判定"（NotEvaluable）是**计算结果形态**（status
 * ＝空＋原因，evidence.md §8.1 规则表行 2）——本枚举只承载该形态的两种
 * 成因（与 evidence UnevaluableCause 语义一一对应），供结果过期呈现区的
 * "无法判定"原因文案选键（P-UI-2 建议口径：归入 results-stale 呈现，
 * 不显示为 Current、不显示为通过；冻结前不私定其它口径）。
 */
enum class NotEvaluableCause : std::uint8_t {
    CrossContext,         ///< 跨上下文（原项目结果不成为另一项目/会话的当前结果——TASK-03）
    UnresolvedDependency, ///< 依赖无法解析（对象/资源缺失——不得默认 Current）
};

// ---------------------------------------------------------------------
// 七态触发数据源值投影（O-31 裁决载体：§3.2 冻结基准的 ui 侧值形态）
// ---------------------------------------------------------------------

/**
 * @brief 就绪投影（incomplete 触发面）。
 *
 * 语义锚点（§6.3 incomplete 行）：evidence.md §6.4.1 ①级 ReadinessSummary
 * （VerdictInput.readiness——{valid, invalidMustItems[]}）：任一启用 Must
 * 条目非法即 valid==false（REQ-06"输入未完成"——正式评估不派发）。ui 只
 * 呈现"未完成"，不复算就绪（N-5：就绪校验归请求方/域）。
 */
struct ReadinessProjection {
    /// 就绪校验结论（true＝全部启用 Must 条目合法；false＝输入未完成）。
    bool valid = true;
    /// 非法 Must 条目键清单（valid==false 时非空——呈现"未完成"明细的数据源）。
    std::vector<std::string> invalidMustKeys;
};

/**
 * @brief 任务活动投影（computing/failed 触发面）。
 *
 * 语义锚点（§3.2 execution 行——O-31 值投影，L5 适配 execution 的
 * TaskSnapshot/ProgressReport 只读面）：任务九态/结局/工程判定**直接使用
 * core 词表类型**（ui→core 是表内登记边，无需投影转换——§3.2"词表"行）；
 * ui 只读不控制（控制面归 ITaskPresentationModel，§10.7/UI-T13）。
 *
 * 字段语义（§6.3 触发数据源原文）：
 *   - activeStates＝当前作用域全部非终态任务状态（∈ {Queued, Preparing,
 *     Running, Paused, Canceling}；终态任务不入本清单——非空即 computing）；
 *   - latestOutcome＝最近终态任务的结局（failed 行判定源）；
 *   - latestEngineeringStatus＝最近正式评估的工程判定（data-insufficient
 *     行判定源——判定级 DataInsufficient；输入级归 incomplete）；
 *   - progressStageKey/cancelAvailable＝计算中呈现的伴随数据（acceptance
 *     显示纪律"计算中附进度阶段与取消"——§9.4 phaseToken 文案键投影与
 *     UX-10 取消入口可用位）。
 */
struct TaskActivityProjection {
    /// 当前作用域非终态任务状态集合（空＝无在途计算——computing 不成立）。
    std::vector<core::TaskState> activeStates;
    /// 最近终态任务结局（nullopt＝本会话尚无终态任务——"可选"语义与 §6.3
    /// computable 行"Completed（可选）"一致）。
    std::optional<core::TaskOutcome> latestOutcome;
    /// 最近正式评估工程判定（nullopt＝尚无正式评估）。
    std::optional<core::EngineeringStatus> latestEngineeringStatus;
    /// 进度阶段文案键（§9.4 ProgressReport.phaseToken 的 ui 侧承载；仅在
    /// computing 呈现中使用，其余七态忽略）。
    std::string progressStageKey;
    /// 取消入口是否可用（UX-10"计算中必须最先呈现＋取消"——呈现位，真实
    /// 取消可行性归 execution Ack）。
    bool cancelAvailable = false;
};

/**
 * @brief 结果当前性投影（results-stale 触发面——C-7 语义承载）。
 *
 * 语义锚点（§3.2 evidence 行——冻结基准 evidence.md §8.1 CurrentnessResult，
 * O-31 值投影、L5 适配；**投影≠重定义**——字段语义逐条对齐对端锚点）：
 *   - status＝nullopt 表达"不可判定"计算形态（**不是第三持久态**——上游
 *     词表仅 Current/Superseded 两态，evidence §8.1 规则表行 2）；
 *   - status==Superseded 时 reasons 携带逐条目失效原因清单（§6.3 过期行
 *     "附 InvalidationReason 清单"——KIN-13 重算提示的数据源）；
 *   - 无默认 Current：status==nullopt 时消费者**不得当作 Current 使用**
 *     （evidence §8.1 规则表行 4；ui 侧呈现归 results-stale——P-UI-2）。
 */
struct CurrentnessProjection {
    /// 持久语义状态（仅两值——与 evidence CurrentnessStatus 一一对应）。
    enum class Status : std::uint8_t {
        Current,    ///< 结果切片与目标重建切片内容身份相等
        Superseded, ///< 结果相对目标上下文过期（reasons 必非空承载原因）
    };

    /**
     * @brief 单条失效原因（evidence.md §8.1 步骤 4 {dependencyKey, kind,
     *        detail} 的 ui 侧承载）。
     *
     * kind 不镜像为 ui 枚举：失效类别词表权威在 evidence（§8.1 步骤 4），
     * ui 仅呈现——适配器以对端稳定 token 字符串传入（kindToken），避免
     * 双权威词表（NFR-MNT-03）；detail 为对端产出的人读差异摘要
     * （同差异同文本，NFR-COR-02），呈现层原样显示不二次加工（UX-02）。
     */
    struct Reason {
        /// 涉事依赖键（条目级差异＝语义角色键；契约级/身份级兜底为空串）。
        std::string dependencyKey;
        /// 失效类别稳定 token（对端 InvalidationKind 词表的持久化形态）。
        std::string kindToken;
        /// 旧→新差异人读摘要（中文＋规范身份文本——呈现层原文显示）。
        std::string detail;
    };

    /// 判定状态（nullopt＝不可判定——NotEvaluable 计算形态，非第三持久态）。
    std::optional<Status> status;
    /// 不可判定原因（status==nullopt 时必有值；两持久态时必无值——presence
    /// 纪律与对端 CurrentnessResult.unevaluableCause 一致）。
    std::optional<NotEvaluableCause> unevaluableCause;
    /// 逐条目失效原因清单（Superseded 时非空；Current/不可判定恒空）。
    std::vector<Reason> reasons;
};

/**
 * @brief 证据清单条目投影（data-insufficient 呈现面——C-7 语义承载）。
 *
 * 语义锚点（§3.2 evidence 行 EvidenceManifest/EvidenceItemStatus——冻结
 * 基准 evidence.md §6.2）：五值状态与对端 EvidenceItemStatus 一一对应；
 * note 承载 Invalid/NotApplicable 的必填原因（ERR-01：不伪造、留原因）。
 */
struct EvidenceItemProjection {
    /// 证据项五值状态（语义与 evidence EvidenceItemStatus 一一对应）。
    enum class Status : std::uint8_t {
        Satisfied,     ///< 产物存在且绑定校验通过
        Missing,       ///< 无产物——不满足（缺失项全量列出，不短路）
        Invalid,       ///< 产物存在但绑定校验失败——不满足（note 必填）
        Unverified,    ///< 产物存在但未在满足正式要求的条件下验证
        NotApplicable, ///< 适用条件不满足——不计缺失（note 必填）
    };
    /// 证据项 id（对应 Profile 项 itemId——"<域>.<项>" 词形）。
    std::string itemId;
    /// 证据项状态。
    Status status = Status::Missing;
    /// Invalid/NotApplicable 必填原因（其余状态为空串——ERR-01 不伪造）。
    std::string note;
};

/**
 * @brief 证据清单投影（data-insufficient 呈现"缺失项全量清单"的数据源——
 *        §6.8 组合呈现行"OpenWritable＋Completed＋DataInsufficient"）。
 *
 * 只携带呈现所需最小面（条目清单）；清单是否构成"数据不足"判定**不由 ui
 * 决定**（N-5——判定权威在 evidence EngineeringStatus），ui 仅在七态求值
 * 命中 data-insufficient 时把不满足项全量列出（acceptance 显示纪律）。
 */
struct EvidenceManifestProjection {
    /// 逐项证据（对应 Profile 项产出状态；空清单＝无可呈现条目）。
    std::vector<EvidenceItemProjection> items;
};

/**
 * @brief 正式通过资格投影（显示纪律唯一放行数据源——C-7 语义承载）。
 *
 * 语义锚点（§3.2 evidence 行 FormalPassEligibility——冻结基准 evidence.md
 * §7.2 资格表行 1）：五条件同时满足方可 eligible（mode==Verified ∧
 * outcome==Completed ∧ 覆盖矩阵完备 ∧ 必需证据齐备 ∧ EngineeringStatus
 * ==Feasible）；unmetConditions 为对端产出的未满足条件稳定 token 清单。
 *
 * **显示纪律（§6.3/§6.8，RPT-05 冻结措辞同样适用于界面）**：ui 不得自行
 * 由 outcome/工程状态组合出"通过"结论；不满足时禁止渲染"正式通过"字样
 * ——渲染放行见 formalPassRenderable()（本投影是其唯一数据源）。
 */
struct FormalPassEligibilityProjection {
    /// 资格事实面是否可解析（false＝尚无正式评估/资格不可得——呈现层不得
    /// 显示任何"通过"字样，也不虚构资格）。
    bool available = false;
    /// 五条件同时满足（available==false 时必为 false）。
    bool eligible = false;
    /// 未满足条件稳定 token 清单（对端 §7.2 词表：mode-not-verified /
    /// outcome-not-completed / coverage-incomplete / evidence-incomplete /
    /// status-not-feasible；顺序＝条件表序）。
    std::vector<std::string> unmetConditions;
};

// ---------------------------------------------------------------------
// 七态求值输入与输出
// ---------------------------------------------------------------------

/**
 * @brief 七态求值的触发数据源快照（§6.3 映射表"触发数据源（权威方）"列的
 *        值聚合——一次一致快照，全部字段来自权威方投影/词表）。
 *
 * 为什么是单一聚合值而不是逐字段入参：§6.1 投影管线按"修订身份对齐"组装
 * 快照——七态求值必须消费同一时刻的各权威事实（分次取数会出现"任务轴是
 * 新事件、当前性轴是旧快照"的拼接态，违反 §6.1 快照一致性纪律）。
 *
 * 谁填充：UiProjectionStore/L5 装配层经各查询端口与 C-7/C-8 值投影组装
 * （pull-on-event——§6.1）；ui 对各轴只读，不复算任何判定（N-5/N-12）。
 */
struct StatusFacts {
    /// 会话是否绑定已打开项目（false＝UiSessionState ∈ {NoProject}——
    /// §5.2 会话态；Opening/Draining/Closed 的呈现路由归 §6.2/§5.7，
    /// 不在本投影语义内）。
    bool projectOpen = false;
    /// 就绪投影（incomplete 触发面——REQ-06/ReadinessSummary）。
    ReadinessProjection readiness{};
    /// 任务活动投影（computing/failed/data-insufficient 触发面——core 词表）。
    TaskActivityProjection tasks{};
    /// Error 级诊断是否活跃（failed 触发面之二——§6.3 failed 行触发数据源
    /// 原文第二分句）。
    bool errorDiagActive = false;
    /// 失败相关诊断投影项（failed 呈现"附对象定位与修复建议"的载体——
    /// §9.1 DiagProjectionItem 原样承载，ui 不改写诊断语义）。
    std::vector<diagnostics::DiagProjectionItem> failureDiagnostics{};
    /// 结果当前性投影（results-stale 触发面——C-7）。
    CurrentnessProjection currentness{};
    /// 证据清单投影（data-insufficient"缺失项全量清单"数据源——C-7）。
    EvidenceManifestProjection evidence{};
    /// 正式通过资格投影（显示纪律唯一放行数据源——C-7）。
    FormalPassEligibilityProjection formalPass{};
};

/**
 * @brief 七态状态投影输出（UI-STG-3 观测点"StatusWordProjection 输出"）。
 *
 * word 之外的字段都是**伴随呈现数据**（acceptance 显示纪律：计算中附进度
 * 阶段与取消、过期附原因、失败附定位、数据不足附缺失清单）——与 word 同
 * 一次求值原子产出，呈现层不得跨快照拼接。formalPass 恒随行（无论命中
 * 哪个七态），保证"是否可显示正式通过"永远有同快照的数据源。
 *
 * 值语义；ui 不得把本投影当业务真值消费（R-2 红线——呈现态≠判定）。
 */
struct StatusWordProjection {
    /// 命中的七态（§6.3 求值优先级首个命中——呈现词，非新判定）。
    StatusWord word = StatusWord::EmptyProject;
    /// 进度阶段文案键（word==Computing 时取自任务轴；其余态为空——
    /// §9.4 phaseToken 承载，键值解析随 UI-T09 UiText）。
    std::string progressStageKey;
    /// 取消入口可用位（word==Computing 时取自任务轴；UX-10）。
    bool cancelAvailable = false;
    /// 逐条目失效原因清单（word==ResultsStale 且当前性==Superseded 时非空
    /// ——§6.3"附 InvalidationReason 清单"）。
    std::vector<CurrentnessProjection::Reason> staleReasons;
    /// 「无法判定」原因（word==ResultsStale 且当前性为 NotEvaluable 计算形态
    /// 时有值——P-UI-2 建议口径；文案键经 currentnessUnevaluableLabelKey）。
    std::optional<NotEvaluableCause> notEvaluableCause;
    /// 不满足证据项全量清单（word==DataInsufficient 时非空——§6.8"缺失项
    /// 全量清单"；Missing/Invalid/Unverified，Satisfied/NotApplicable 不列）。
    std::vector<EvidenceItemProjection> unsatisfiedEvidence;
    /// 失败诊断投影项（word==Failed 时原样携带——§6.3"附对象定位与修复
    /// 建议"；含 subject/localName 定位与 actionKind 处置入口）。
    std::vector<diagnostics::DiagProjectionItem> failureDiagnostics;
    /// 正式通过资格投影（恒随行——显示纪律唯一放行数据源，与 word 同快照）。
    FormalPassEligibilityProjection formalPass;
};

// ---------------------------------------------------------------------
// 词表 token 与文案键（键冻结于本头；中文值为 UI-T09 UiText 前的过渡承载
// ——UI-T03"壳内文案表为 UI-T09 UiText 过渡承载"同案，迁移时键不变）
// ---------------------------------------------------------------------

/**
 * @brief 七态冻结 token（§6.3 词表 token 列；小写连字符）。
 *
 * @param word [in] 七态值（全七值皆有登记 token——未知值为调用方错误，
 *             返回空串并不够用，故契约约定只传合法枚举值）
 * @return 冻结 token（"empty-project"/"incomplete"/"computing"/
 *         "results-stale"/"data-insufficient"/"failed"/"computable"）
 */
const char* statusWordToken(StatusWord word) noexcept;

/**
 * @brief 七态呈现文案键（键约定 state.<token>.label——§3.5 文案键体系，
 *        PM-03/PM-11 同族）。
 *
 * @param word [in] 七态值
 * @return 文案键（如 "state.empty-project.label"；值解析归 UiText/UI-T09，
 *         过渡值见 statusWordTransitionalLabel）
 */
std::string statusWordLabelKey(StatusWord word);

/**
 * @brief 七态中文呈现文本（UI-T09 UiText 文案资源落地前的过渡承载——
 *        §6.3 词表"中文"列原文，键值分离过渡期值随头文件走）。
 *
 * @param word [in] 七态值
 * @return 中文短词（"空项目"/"未完成"/"计算中"/"结果过期"/"数据不足"/
 *         "失败"/"可计算"；UI-T09 后本函数退役，键不变）
 */
std::string statusWordTransitionalLabel(StatusWord word);

/**
 * @brief 七态求值优先级（§6.3 冻结优先级：empty-project ＞ computing ＞
 *        incomplete ＞ failed ＞ data-insufficient ＞ results-stale ＞
 *        computable）。
 *
 * 返回值越小优先级越高（0＝最高）。理由（§6.3 原文）：计算中必须最先呈现
 * （用户提供取消入口 UX-10）；未完成阻断新正式运行；失败与数据不足次之；
 * 过期仍可查看历史；全无则可计算。
 *
 * @param word [in] 七态值
 * @return 优先级序号（0~6；工作台总徽标聚合用——§6.3"取各活跃阶段中
 *         优先级最高者"）
 */
int statusWordPriority(StatusWord word) noexcept;

/**
 * @brief 求值优先级最高的七态（工作台总徽标聚合——§6.3 原文"总徽标＝取
 *        各活跃阶段中优先级最高者"）。
 *
 * @param words [in] 各活跃阶段的七态（阶段七态视图随 UI-T09 产出）
 * @return 优先级最高者（同优先级不重叠——词表无重复序号）；空输入返回
 *         nullopt（无活跃阶段，不虚构状态词）
 */
std::optional<StatusWord> dominantStatusWord(const std::vector<StatusWord>& words);

/**
 * @brief core 九态短标签文案键（PM-03/PM-11——acceptance"九态短标签文案键
 *        state.<token>.label 就位"）。
 *
 * 键中的 token 是 core::TaskState 的冻结持久化 token（core.md §4.7 小写
 * 连字符，经 core toToken 词表对应：queued/preparing/running/paused/
 * canceling/canceled/completed/failed/interrupted）；键值分离——值（中文
 * 短标签）归 ui 文案资源（§3.5/P-DIAG-9 交接）。
 *
 * @param state [in] 任务九态（core 词表——ui→core 表内边直用）
 * @return 文案键（如 "state.queued.label"）
 */
std::string taskStateLabelKey(core::TaskState state);

/**
 * @brief core 九态中文短标签（UI-T09 前过渡承载——§6.3 九态短标签行原文：
 *        排队中/准备中/计算中/已暂停/取消中/已取消/已完成/失败/已中断）。
 *
 * @param state [in] 任务九态
 * @return 中文短标签（与键一一对应；UI-T09 后本函数退役，键不变）
 */
std::string taskStateTransitionalLabel(core::TaskState state);

/**
 * @brief 当前性「无法判定」原因文案键（P-UI-2 呈现承载——结果过期呈现区
 *        的原因条目键，约定 state.currentness.unevaluable.<cause>.label）。
 *
 * @param cause [in] 不可判定成因
 * @return 文案键（"state.currentness.unevaluable.cross-context.label" /
 *         "state.currentness.unevaluable.unresolved-dependency.label"）
 */
std::string currentnessUnevaluableLabelKey(NotEvaluableCause cause);

/**
 * @brief 当前性「无法判定」原因中文文本（UI-T09 前过渡承载——§6.3 P-UI-2
 *        建议口径的原因区文案："当前性无法判定（依赖缺失/上下文变化）"）。
 *
 * @param cause [in] 不可判定成因
 * @return 中文原因文本（UnresolvedDependency→"当前性无法判定（依赖缺失）"；
 *         CrossContext→"当前性无法判定（上下文变化）"）
 */
std::string currentnessUnevaluableTransitionalLabel(NotEvaluableCause cause);

/**
 * @brief 「正式通过」字样渲染放行门（§6.3/§6.8 显示纪律的唯一实现点）。
 *
 * 五条件资格（FormalPassEligibility）是唯一放行数据源：资格事实面不可解析
 * （available==false，含"尚无正式评估"）或五条件未全满足（eligible==false，
 * 含任一 unmetCondition）时返回 false——呈现层据此**禁止渲染"正式通过"字
 * 样**（RPT-05 冻结措辞同样适用于界面；ui 不得自行由 outcome/工程状态组合
 * 出"通过"结论）。ui 不复算五条件（N-5：资格判定权威在 evidence）。
 *
 * @param p [in] 正式通过资格投影（与七态 word 同快照随行）
 * @return true＝允许渲染"正式通过"；false＝禁止（显示限定语/不显示）
 */
inline bool formalPassRenderable(const FormalPassEligibilityProjection& p) noexcept
{
    // available==false：无资格事实（未评估/不可得）——按未满足处置（不虚构）。
    // eligible==false：五条件未全满足——§7.2 表"同时满足方可 true"。
    return p.available && p.eligible;
}

/**
 * @brief 证据清单中的不满足项全量清单（§6.8"缺失项全量清单"数据源）。
 *
 * 不满足＝Missing/Invalid/Unverified 三态（evidence §6.2 判定后果原文：
 * Missing 全量列出、Invalid 附原因、Unverified 区别于 Missing）；Satisfied
 * 与 NotApplicable（显式不适用、不计缺失——C2/ERR-01）不列。全量不短路
 * （表 2 ④同口径）。
 *
 * @param manifest [in] 证据清单投影
 * @return 不满足项（保持清单原序——呈现顺序不重排，NFR-COR-02 稳定呈现）
 */
std::vector<EvidenceItemProjection>
unsatisfiedEvidenceItems(const EvidenceManifestProjection& manifest);

/**
 * @brief 七态求值（§6.3 七态×权威词表映射表＋求值优先级的唯一实现）。
 *
 * 自上而下按冻结优先级首个命中（empty-project ＞ computing ＞ incomplete
 * ＞ failed ＞ data-insufficient ＞ results-stale ＞ computable）——判定
 * 全部来自权威方数据（呈现映射，不是新判定——R-2 红线）；NotEvaluable
 * （status==nullopt）按 P-UI-2 建议口径归入 results-stale＋「无法判定」
 * 原因呈现，不显示为 Current、不显示为通过。纯函数：同输入同输出
 * （NFR-COR-02），无副作用、不回写任何权威对象（投影只读红线）。
 *
 * @param facts [in] 触发数据源快照（一次一致快照——见 StatusFacts 注释）
 * @return 七态投影（word＋伴随呈现数据＋formalPass 同快照随行）
 *
 * @note UI 线程调用（§3.4 M-1 消费点）；实现零分配上限受输入规模约束
 *       （清单原样拷贝——调用方控制快照规模）。
 */
StatusWordProjection evaluateStatusWord(const StatusFacts& facts);

}  // namespace ui
}  // namespace ird

#endif  // SDURWS_IRD_UI_UIPROJECTIONS_HPP
