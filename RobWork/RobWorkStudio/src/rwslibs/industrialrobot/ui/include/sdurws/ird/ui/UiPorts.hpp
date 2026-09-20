/**
 * @file   UiPorts.hpp
 * @brief  ui 单元最小端口接口——对 project/policy/runtime 协作面的 ui 自有
 *         注入接口（O-31 裁决 2026-09-19 的注入面载体）。
 *
 * 设计依据：
 *   - units/ui.md §3.3（公共头布局：UiPorts.hpp＝"C-3/4/5/7/8/10/11 最小
 *     端口接口（O-31 裁决注入面……接口形状随 UI-T03+ 各消费任务在其 §10
 *     对应节实现冻结并增量修订登记，L5 适配）"）、§3.1（依赖边红线：产品面
 *     零对 project/evidence/execution/policy/runtime 的链接或 include）；
 *   - O-31 裁决（DTB §4 登记行）：确认 ui 侧最小注入接口模式——对端类型不
 *     直接进入 ui 头文件，ui 定义最小端口接口，L5 应用壳装配期以单行适配器
 *     绑定对端实现（reporting.md §3.3／evidence.md §3.3／EX ICompileCacheJudge
 *     等 10 单元"消费方自定义最小注入接口＋值传递"同款惯例）；
 *   - 本任务（UI-T03）冻结的两个端口：C-10（策略摘要）、C-11（名称解析）——
 *     均 §10.1 ShellWiring 持有；注册表类型归 ui 自有头（P-UI-5/CF-1 结论，
 *     本头不涉及——ICommandRegistry 随 UI-T06 落其自有头 §10.3）。
 *     UI-T04 增量冻结：C-7（当前性端口 IUiCurrentnessSource——七态求值触发
 *     数据源与显示纪律放行数据源；登记 ui.md §16.7 v0.6）。
 *     UI-T06 增量冻结：C-4（命令网关端口 IUiCommandGateway——§10.3
 *     CommandOutcome.revisionResult 的 O-31 承载面；登记 ui.md §16.7 v0.8：
 *     任务契约 acceptance 3"经 ui 自有命令网关端口＋命令结果值投影承载"）。
 *
 * 背景说明（为什么不直接 include policy::IPolicyProvider / runtime::
 * IRuntimeNameResolver）：ARCH §3.5 依赖白名单只有 ui→core、ui→diagnostics
 * 两条边；ui 与 policy/runtime 的协作是**运行时注入**而非编译依赖。若 ui
 * 头出现对端类型，ui 即对对端产生编译依赖（增边违红线）。最小端口把依赖
 * 方向倒转为"对端适配 ui"——L5 装配器同时看见两边，由它写适配器，ui 保持
 * 零对端知识。端口方法语义以对端卡为冻结基准（见各方法注释锚点），适配器
 * 不得改义。
 *
 * 线程约束：端口实现由 L5 注入，调用一律发生在 UI 线程（§3.4 Marshal 纪律
 * M-1 的 ui 侧消费点）；实现自身如带内部状态须自行保证 UI 线程串行假设。
 */

#ifndef SDURWS_IRD_UI_UIPORTS_HPP
#define SDURWS_IRD_UI_UIPORTS_HPP

#include <optional>
#include <string>

#include <sdurws/ird/core/Identity.hpp>        // core::ObjectId（C-11 端口入参——身份类型与 core 契约同一）
#include <sdurws/ird/ui/UiProjections.hpp>     // PolicySummaryProjection（C-10 端口返回的值投影）

namespace sdurws::ird {
namespace ui {

// =====================================================================
// C-11：名称解析端口（冻结基准 runtime.md §7.3 resolveObjectId 语义）
// =====================================================================

/**
 * @brief 对象名称解析的 ui 自有最小端口（L5 适配 runtime::IRuntimeNameResolver）。
 *
 * 语义冻结（不改义——ui.md v0.4 C-11 行原文）：resolveObjectId 取
 * localName 语义不变——给定 core::ObjectId，返回该对象在 RuntimeNameMap 中
 * 的局部名（消歧后呈现名；runtime.md §7.3 RuntimeName.localName）。
 *
 * 为什么返回 optional 而不是 runtime 侧 Expected 形态：ui 呈现层只关心
 * "有名字可显示/没有"二态；失败细节（unknown-object 等 RuntimeErrorCode）
 * 对呈现无用（显示占位即可），把 Expected 折叠为 optional 是**消费侧最小
 * 面**（适配器持有完整语义，需要时 ui 侧后续任务可经增量修订扩宽）。解析
 * 失败时调用方显示占位文案，**不得**自行拼接/截断任何名称（R-4：名称
 * 前缀拼接/剥离禁止——解析统一归 runtime 语义）。
 *
 * UI-T03 消费状态：WorkbenchShell 持有本端口（ShellWiring.nameResolver），
 * 左栏对象树（真实消费点）随其实现任务落位——持有与空值校验先于消费，
 * 形状按 §10.1 裁决签名冻结。
 */
class IUiNameResolver {
public:
    virtual ~IUiNameResolver() = default;

    /**
     * @brief 解析对象身份为呈现用局部名（runtime.md §7.3 语义的 ui 侧投影）。
     *
     * @param id [in] 对象身份（core::ObjectId；全零保留值由实现按未知处置）
     * @return 局部名（UTF-8；RuntimeName.localName）；无法解析→nullopt
     *         （呈现层回退占位文案，不虚构名称）
     *
     * @note 实现应快速返回（呈现路径，UI 线程——NFR-PERF-01 交互预算）；
     *       阻塞解析属实现违约（长查询应走缓存/后台，runtime.md §8.3）。
     */
    virtual std::optional<std::string> resolveObjectId(core::ObjectId id) const = 0;
};

// =====================================================================
// C-10：策略摘要端口（冻结基准 policy.md §9.1/§10.6 只读投影语义）
// =====================================================================

/**
 * @brief 工程策略只读摘要的 ui 自有最小端口（L5 适配 policy::IPolicyProvider）。
 *
 * 语义冻结（不改义——ui.md v0.4 C-10 行原文）：策略摘要只读面语义不变
 * （ui.md §6.7：EngineeringPolicySet 公开字段投影——碰撞域启用/阈值/过滤对
 * 计数，含"显式不适用"态；显示值不改变策略内容身份）。ui 对策略只有
 * **只读**访问：任何"修改策略"动作走 §6.7 跳转＋命令端口提交路径，本端口
 * 永不承载写语义。
 *
 * 返回值＝PolicySummaryProjection（UiProjections.hpp——值投影，O-31 载体）。
 * UI-T03 冻结计数形态；逐字段细化随 UI-T07（首个消费面）按增量修订登记。
 *
 * UI-T03 消费状态：WorkbenchShell 持有本端口（ShellWiring.policySource），
 * 右栏策略摘要卡（真实消费点）随 UI-T07 落位。
 */
class IPolicySummarySource {
public:
    virtual ~IPolicySummarySource() = default;

    /**
     * @brief 取当前策略只读摘要（§6.7 策略摘要数据源）。
     *
     * @return 策略摘要投影（available=false＝策略未装载——呈现占位，不虚构
     *         数值）；纯查询，不抛（实现内部失败以 available=false 表达）
     *
     * @note UI 线程调用（§3.4 M-1）；实现应返回快照值（无锁/短临界区——
     *       NFR-PERF-01）。
     */
    virtual PolicySummaryProjection summary() const = 0;
};

// =====================================================================
// C-7：当前性端口（冻结基准 evidence.md §6/§7/§8——结果与当前性投影面；
// UI-T04 首消费冻结，登记 ui.md §16.7 v0.6）
// =====================================================================

/**
 * @brief 证据当前性与资格事实的 ui 自有最小端口（L5 适配 evidence 只读面）。
 *
 * 语义冻结（不改义——ui.md v0.4 C-7 行原文）：evidence 结果与当前性投影
 * ——CurrentnessResult（过期原因）、EvidenceManifest（证据清单）、
 * FormalPassEligibility（正式通过资格）。ui 对 evidence 只有**只读**访问：
 * 当前性/资格判定权威在 evidence（N-5），本端口只回传**已判定的值投影**
 * （UiProjections.hpp——CurrentnessProjection/EvidenceManifestProjection/
 * FormalPassEligibilityProjection，字段语义逐条锚定 evidence.md §8.1/§6.2/
 * §7.2），ui 侧不复算、不缓存改写（evidence"派生投影不可写回"纪律在
 * 消费侧同样成立）。
 *
 * 为什么三个方法而不是一个聚合方法：三个事实面的失效/重算节奏不同（当前
 * 性随修订事件重算、清单随评估变化、资格随最近评估变化），聚合会让适配器
 * 被迫每次全量取数；分离方法让装配层按需绑定。调用方（投影管线）负责把
 * 三者组装进同一次 StatusFacts 快照（§6.1 快照一致性——一致性归组装方，
 * 不在端口内）。
 *
 * UI-T04 消费状态：七态求值的触发数据源（evaluateStatusWord 输入面）与
 * 显示纪律放行数据源（formalPassRenderable）；端口实现由 L5 注入并适配
 * evidence 会话态索引/资格检查结果——ui 测试以可控替身承载（§3.1）。
 */
class IUiCurrentnessSource {
public:
    virtual ~IUiCurrentnessSource() = default;

    /**
     * @brief 取当前结果当前性投影（§6.3 results-stale 触发面）。
     *
     * @return 当前性投影（status==nullopt＝不可判定计算形态——不得当作
     *         Current 使用，evidence §8.1 规则表行 4；纯查询，不抛——
     *         实现内部失败以 nullopt＋unevaluableCause 表达，不虚构 Current）
     *
     * @note UI 线程调用（§3.4 M-1）；实现应返回快照值（NFR-PERF-01）。
     */
    virtual CurrentnessProjection currentness() const = 0;

    /**
     * @brief 取证据清单投影（§6.8"缺失项全量清单"数据源）。
     *
     * @return 证据清单投影（空清单＝无可呈现条目；纯查询，不抛）
     *
     * @note UI 线程调用；条目 note 已由 evidence 侧按 ERR-01 保证
     *       （Invalid/NotApplicable 必填原因），ui 原样呈现不加工。
     */
    virtual EvidenceManifestProjection evidenceManifest() const = 0;

    /**
     * @brief 取正式通过资格投影（§6.3 显示纪律唯一放行数据源）。
     *
     * @return 资格投影（available==false＝尚无正式评估/资格不可得——
     *         禁止渲染"正式通过"字样；纯查询，不抛）
     *
     * @note UI 线程调用；五条件判定权威在 evidence（§7.2），ui 只读放行位。
     */
    virtual FormalPassEligibilityProjection formalPassEligibility() const = 0;
};

// =====================================================================
// C-4：命令网关端口（冻结基准 project.md §5.3.1 ProjectCommandService::
// submit 语义；UI-T06 首消费冻结，登记 ui.md §16.7 v0.8）
// =====================================================================

/**
 * @brief 命令提交的 ui 自有最小端口（L5 适配 project::ProjectCommandService）。
 *
 * 语义冻结（不改义——ui.md §7.7 时序③原文）：命令执行**唯一写路径经
 * project**——ProjectCommandService::submit(envelope, interaction)（§5.3.1：
 * 全局串行、成功恰好产生一个新修订、失败/拒绝/中止不产生任何修订、要求
 * writable）。ui 对命令执行**零业务判定**（§7.5 红线：界面使能态不是业务
 * 判定；真正校验在 project 命令边界）——本端口只把信封投影翻译为对端
 * 提交、把对端 CommandResult 折叠回 ui 值投影（UiProjections.hpp 的
 * CommandResultProjection），**不改写结论、不吞错误**。
 *
 * 为什么确认回调（ICommandInteraction）不在本端口：O-31 v0.4 裁决 C-6
 * 方向外翻——project::ICommandInteraction 由 L5 适配器实现并委托 ui 自有
 * 交互回调端口（P-PR-7 Bridge 归 UI-T13）；本端口只承载"提交→结果"同步
 * 面。适配器在 submit 内部把对端交互回调 Marshal 回 UI 线程确认对话
 * （§3.4 命令执行线程行）——该时序对 ui 命令处理器不可见。
 *
 * UI-T06 消费状态：端口由 L5 注入后交给命令处理器使用（§7.7 ②处理器组装
 * 信封→③经本端口提交）；注册表本身**不调用**本端口（注册表只派发处理器
 * ——§10.3 副作用行），ui 测试以可控替身承载（§3.1）。
 */
class IUiCommandGateway {
public:
    virtual ~IUiCommandGateway() = default;

    /**
     * @brief 提交命令信封并取回结果投影（§7.7 时序③的 ui 侧入口）。
     *
     * @param envelope [in] 命令信封投影（CommandEnvelopeProjection——
     *                 §5.3.1 CommandEnvelope 的 ui 值形态）
     * @return 命令结果投影（Committed 时 newRevision 唯一非空；Rejected/
     *         Aborted/Failed 携带 reason token——§5.3.1 variant 词表；纯值，
     *         不抛——对端 StoreError 明细折叠为 Failed＋UI-T13 增量面）
     *
     * @note 调用线程＝命令处理器所在线程（UI 线程启动、处理器自行转交——
     *       §10.3 线程行；对端内部转命令执行序列并同步返回，§5.3.1 线程行
     *       "任意线程调用（内部转命令执行序列）"）。
     */
    virtual CommandResultProjection submit(const CommandEnvelopeProjection& envelope) = 0;
};

}  // namespace ui
}  // namespace ird

#endif  // SDURWS_IRD_UI_UIPORTS_HPP
