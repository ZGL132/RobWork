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
 *     项目条目）、PM-14（用户级设置承载的值面）。
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
#include <optional>
#include <string>

#include <sdurws/ird/core/Identity.hpp>  // core::ProjectId（P-PR-1 同案：身份类型与 core 契约同一，不本地重定义）

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
// 工程策略摘要投影（C-10 语义承载——冻结基准 policy.md §9.1/§10.6；
// ui.md §6.7 策略摘要只读卡）
// =====================================================================

/**
 * @brief 工程策略只读摘要的 ui 侧投影（IPolicySummarySource 的返回值）。
 *
 * 语义锚点（ui.md §6.7）：数据＝EngineeringPolicySet 公开字段投影（碰撞
 * 域启用/安全间距/过滤对计数/行程上限阈值/判定阈值，含"显式不适用"态）；
 * **不改变策略内容身份**（投影≠重定义——权威字段语义在 policy.md，本类型
 * 只承载呈现所需的计数与开关位）。
 *
 * UI-T03 形态说明：本投影由 WorkbenchShell 持有端口承载（§10.1 ShellWiring），
 * 首个消费面＝UI-T07（策略摘要只读卡）。计数形态（而非逐阈值字段）是
 * UI-T03 的最小冻结：逐字段展开随 UI-T07 消费时按增量修订登记细化
 * （ui.md v0.4 §10 引导注的既定机制）。
 */
struct PolicySummaryProjection {
    /// 策略源是否可解析（false＝未装载/不可用——呈现层显示占位，不虚构数值）。
    bool available = false;
    /// 碰撞域启用位（EngineeringPolicySet 碰撞域开关投影；§6.7）。
    bool collisionDomainEnabled = false;
    /// 阈值类字段计数（行程上限/判定阈值等——呈现"阈值 n 项"汇总用）。
    std::size_t thresholdCount = 0;
    /// 过滤对计数（§6.7"过滤对计数"原文）。
    std::size_t filterPairCount = 0;
    /// "显式不适用"态字段计数（§6.7 括注——ERR-01"不适用"呈现的汇总来源）。
    std::size_t explicitNotApplicableCount = 0;
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

}  // namespace ui
}  // namespace ird

#endif  // SDURWS_IRD_UI_UIPROJECTIONS_HPP
