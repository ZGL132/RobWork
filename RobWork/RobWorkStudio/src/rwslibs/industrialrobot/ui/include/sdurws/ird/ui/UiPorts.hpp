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
 *     UI-T09 增量冻结：C-12（阶段门控端口 IUiStageGate——§6.4 呈现输出与
 *     导航评估；workflow.md 未产出，P-UI-6 单侧冻结）与域就绪只读投影源
 *     IUiDomainReadinessSource（§6.5 StageStatusModel 汇聚输入——域插件经
 *     注册端口上报的只读投影；登记 ui.md §16.7 v1.1）。两端口在 WP-22-T03
 *     产出前以桩承载（契约卡行"未产出→桩"口径；ui 测试以可控替身注入）。
 *     UI-T10 增量冻结：关于框数据源端口 IUiAboutDataSource（§11.4 关于
 *     对话框的装配报告半区＋冻结版本基线半区——版本数据由 L5 提供、ui 呈
 *     现，NFR-DEP-05；登记 ui.md §16.7 v1.2）。WP-24-T01 版本基线产出前
 *     available=false 承载（版本值零虚构）；装配器落地前报告为空集
 *     （清单＝白名单占位行）。
 *     UI-T11 增量冻结：C-3/C-5/C-8 会话生命周期端口（§5.2 状态机的数据
 *     面，登记 ui.md §16.7 v1.3）——IUiStoreFactoryPort（打开五步协议的
 *     ui 侧入口，§10.1 注释行"project/execution 实例随打开流程注入
 *     UiSessionController"的落地面）、IUiProjectStorePort＋
 *     IUiStoreCloseObserver（C-3 关闭协议：requestClose/closed/
 *     subscribeClose）、IUiDraftQueryPort（C-5 草稿清单——关闭对话框
 *     草稿区数据装配）、IUiSessionTaskPort（C-8 任务查询/协作取消/强制
 *     终止——关闭对话框任务区＋§5.6 防线）。四端口的会话绑定集
 *     SessionPortBundle 随打开结果注入（"实例随打开流程注入"原文）；
 *     L5 适配 project::ProjectStoreFactory/ProjectStore/DraftService/
 *     ITaskScheduler/ITaskController，ui 测试以可控替身承载（§3.1）。
 *     UI-T12 增量冻结：C-5 草稿写半区端口 IUiDraftStorePort（§8 草稿
 *     控制器——save/tryLoad/discard 三原语，登记 ui.md §16.7 v1.4）。
 *     语义冻结基准＝project.md §5.4 DraftService：save＝"保存仅落
 *     drafts/，不产生修订"（PM-04 分工红线）、tryLoad＝损坏回退 .bak
 *     的读轨（PM-08）、discard＝写门卫下的幂等放弃（§8.3-4"放弃＝显式
 *     discard"）。L5 适配 project::DraftService，ui 测试以可控替身承载。
 *     UI-T13 增量冻结（登记 ui.md §16.7 v1.5）：①C-6 交互回调端口
 *     IUiCommandInteraction（O-31 方向外翻的 ui 侧承接——§9.2 确认流：
 *     L5 适配器实现 project::ICommandInteraction 并委托本端口，
 *     CommandInteractionBridge 实现本端口；形状逐字镜像 project.md
 *     §5.3.3 冻结点——P-PR-7"对端单侧冻结互为起点不私改"）；②C-8 任务
 *     呈现面端口 IUiTaskPresentationPort（§9.4/§10.7 任务呈现模型的
 *     数据/控制面——tasksByProject 快照/进度查询/暂停继续/强杀，L5 适配
 *     execution ITaskScheduler/ITaskController；v1.3 的 IUiSessionTaskPort
 *     保持冻结不动——关闭对话框三原语面不变，呈现面另立端口避免触碰
 *     已冻结形状）；③C-9 确认投影呈现半区 IUiFindingQueryPort（§9.2
 *     "pendingFor(revision) 投影只读显示——ui 经 project 间接读、不直接调
 *     diagnostics 服务端接口"原文的注入面，L5 适配 project→diagnostics
 *     pendingFor）＋Tier-U 日志面板数据源 IUiUserLogSource（§9.1 日志
 *     面板行）＋脱敏配置端口 IUiRedactionPolicyPort（§9.1 脱敏行"ui 只
 *     提供配置界面（L5 会话变更 setPolicy）"的注入面，L5 适配
 *     diagnostics::IRedactionService）。
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

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Events.hpp>         // core::IEventSubscription（subscribeClose 返回的 RAII 句柄——表内登记边）
#include <sdurws/ird/core/Identity.hpp>       // core::ObjectId（C-11 端口入参）/ProjectId/TaskIdentity（C-8 会话端口入参——身份类型与 core 契约同一）
#include <sdurws/ird/diagnostics/Catalog.hpp>     // diagnostics::DiagProjectionItem/DiagQuery（C-9 只读投影值面——表内登记边 ui→diagnostics）
#include <sdurws/ird/diagnostics/Confirmable.hpp> // diagnostics::FindingRecord（C-9 确认投影呈现半区值面——表内登记边）
#include <sdurws/ird/diagnostics/Redaction.hpp>   // diagnostics::RedactionPolicy（脱敏配置端口值面——表内登记边）
#include <sdurws/ird/ui/AboutDialog.hpp>      // PluginAssemblyReport/AboutVersionBaseline（IUiAboutDataSource 值面——§10.9/§11.4）
#include <sdurws/ird/ui/UiProjections.hpp>    // PolicySummaryProjection（C-10）＋OpenStoreOutcome/TaskRowProjection/TaskViewProjection 等（会话/任务端口值面）
#include <sdurws/ird/ui/UiTypes.hpp>          // StageId/StageViewStatus/DomainReadinessItem/StageReadinessSnapshot/TextKey（C-12 端口值面——§3.3 公共值类型头）＋UiOpenMode（UI-T11）

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

// =====================================================================
// C-12：阶段门控端口（冻结基准 ui.md §6.4 阶段导航与门控——workflow.md
// 未产出，P-UI-6 单侧冻结；UI-T09 首消费冻结，登记 ui.md §16.7 v1.1）
// =====================================================================

/**
 * @brief 单阶段门控呈现输出的 ui 侧承载（§6.4 阶段呈现状态表的数据源投影）。
 *
 * P-UI-6 处置（契约 acceptance 3）：workflow 门控三方契约未定稿前按本形状
 * 实现（单侧冻结——谈判起点；workflow.md 产出后核对，不兼容时按影响面
 * 增量同步，不私改对端）。字段语义（§6.4 表"数据源"列原文）：
 *   - status 为门控可判定的四态之一（completed/blocked/unavailable/
 *     not-started——表中各行数据源"workflow 门控输出"）；in-progress
 *     （会话态）与 view-only（writable=false）**不是门控输出**，由导航
 *     模型按会话事实合成（§6.4 表行 2/6 数据源原文——门控适配器若输出
 *     这两态属实现违约，模型按 not-started 兜底防御）；
 *   - blockingReasonKeys＝blocked 行"附原因＋下一步建议"的原因/缺项文案键
 *     （失败定位经诊断/缺项列表——缺项明细归域/VerdictTrace，门控只回键）；
 *   - nextStepKey＝下一步建议文案键（UX-01：建议文本由 workflow 提供，
 *     ui 只呈现与跳转，不生成建议文本）。
 *
 * 值语义；门控判定权威在 workflow（N-11 无第二套状态机）——模型与呈现层
 * 均不得由本输出反推门控规则。
 */
struct StageGateView {
    /// 门控呈现态（默认 NotStarted＝"尚无门控数据"的安全空值——不虚构
    /// completed/unavailable 语义）。
    StageViewStatus status = StageViewStatus::NotStarted;
    /// 阻塞原因/缺项文案键（blocked 时非空；其余态可为空——透传不加工）。
    std::vector<TextKey> blockingReasonKeys;
    /// 下一步建议文案键（nullopt＝门控未提供建议——呈现层不虚构，UX-01）。
    std::optional<TextKey> nextStepKey;
};

/**
 * @brief 导航门控判定结果（§6.4 阶段切换时序"允许｜拒绝(原因/解锁条件)"
 *        的值承载）。
 *
 * reasonKeys 的内容契约：包含**原因键**与**缺项键**两类——门控评估时消费
 * StageReadinessSnapshot（§6.4 时序图"门控评估（消费 ui 的 StageStatusModel
 * 汇聚投影）"框内原文），把其中的 missingItemKeys 折叠进拒绝数据，就地
 * 提示才有"原因＋缺项＋下一步建议"三要素（契约 acceptance 1：拒绝时
 * 就地提示原因＋缺项＋下一步建议）。
 */
struct StageGateDecision {
    /// 是否放行（false＝拒绝——拒绝不携带 stage，导航模型不改变 currentStage）。
    bool allowed = false;
    /// 拒绝原因＋缺项文案键（allowed==false 时呈现"就地提示"的数据源；
    /// allowed==true 时应为空——放行无提示语义）。
    std::vector<TextKey> reasonKeys;
    /// 解锁条件/下一步建议文案键（§6.4 时序"拒绝(原因/解锁条件)"的
    /// 解锁半区；nullopt＝门控未提供）。
    std::optional<TextKey> unlockHintKey;
};

/**
 * @brief 阶段门控的 ui 自有最小端口（L5 适配 workflow 阶段门控——
 *        WP-22-T03 未产出前以桩承载，契约卡行"未产出→桩"口径）。
 *
 * 语义冻结（不改义——ui.md §6.4 原文）：进入条件的业务判定归 workflow
 * 门控，ui 不复制（§6.4 表头）；门控评估消费 ui 的 StageStatusModel 汇聚
 * 投影（§6.4 时序图）——evaluate 的入参即该快照，门控从快照读域就绪
 * 事实、输出允许/拒绝。ui 侧对判定结果零加工：模型只透传 decision 组装
 * NavigateResult（投影只消费、不拥有门控规则——契约 acceptance 2/N-11）。
 *
 * 为什么是两个方法而不是一个：呈现输出（stageViews 全量七阶段需要的
 * 素材）与导航评估（requestNavigate 单阶段按需评估）节奏不同——前者纯
 * 读高频、后者携带汇聚快照构建成本。分离让适配器按需取数
 * （IUiCurrentnessSource 三方法同款取舍）。
 *
 * 线程约束：调用一律发生在 UI 线程（§3.4 M-1 消费点——presentStage 由
 * stageViews 调用、evaluate 由 requestNavigate 调用，均 UI 线程）。
 */
class IUiStageGate {
public:
    virtual ~IUiStageGate() = default;

    /**
     * @brief 取单阶段门控呈现输出（§6.4 阶段呈现状态表数据源）。
     *
     * @param stage [in] 目标阶段（七阶段词表值）
     * @return 门控呈现投影（status 为门控可判定四态；见 StageGateView 注释）
     *
     * @note 纯查询，不抛（实现内部数据源不可得时按 §10.2 错误类型行以
     *       blocked＋"stage.gate.unavailable.reason"表达"门控数据不可用"，
     *       文案经 UiText 解析——不虚构 completed/unavailable 语义）。
     */
    virtual StageGateView presentStage(StageId stage) const = 0;

    /**
     * @brief 评估导航请求（§6.4 时序"门控评估"步——消费 StageStatusModel）。
     *
     * @param snapshot [in] 目标阶段的域就绪汇聚快照（导航模型现取现传——
     *                 epoch 标注构建时刻；门控不得缓存改写，N-5/N-11）
     * @return 允许/拒绝判定（拒绝时 reasonKeys 含原因＋缺项键、unlockHintKey
     *         含解锁条件——就地提示三要素的数据面）
     *
     * @note UI 线程调用；判定权威在 workflow（本端口实现侧）——ui 对
     *       返回值零加工（透传组装 NavigateResult，不重判不补判）。
     */
    virtual StageGateDecision evaluate(const StageReadinessSnapshot& snapshot) const = 0;
};

// =====================================================================
// §6.5 域就绪只读投影源端口（StageStatusModel 汇聚输入——域插件经注册
// 端口上报的只读投影；UI-T09 首消费冻结，登记 ui.md §16.7 v1.1）
// =====================================================================

/**
 * @brief 域就绪只读投影的 ui 自有最小端口（§6.5 "StageStatusModel 只汇聚
 *        域插件经注册端口上报的只读投影"的注册面）。
 *
 * 谁实现：域插件/装配层（L5 适配）——每个域插件注册一个源实例上报其
 * 只读投影；多个域以 StageNavigationModelDeps.domainSources 的注册序进入
 * 汇聚（快照 domains 按该序稳定排列——NFR-COR-02）。数据语义权威在域
 * 侧（verdict＝最近正式判定、inputComplete＝域就绪校验结论）——汇聚只
 * 搬运，不计算门控、不判定就绪（§6.5 红线原文；N-11 无第二套）。
 *
 * UI-T09 消费状态：StageNavigationModel::readinessSnapshot 现取现拷贝
 * （§10.2 线程行"任意线程拉取（值拷贝）"）；WP-22-T03 门控数据源未产出
 * 前以桩注入（契约卡行"未产出→桩"）。
 */
class IUiDomainReadinessSource {
public:
    virtual ~IUiDomainReadinessSource() = default;

    /**
     * @brief 取该源承载的域就绪投影（§6.5 DomainReadinessItem 形状）。
     *
     * @param stage [in] 目标阶段（域按阶段上报——同一域在不同阶段可有
     *              不同就绪事实）
     * @return 域就绪项清单（可含多个域项——复合适配器场景；空清单＝该
     *         阶段无本源投影，不计入快照）
     *
     * @note 线程契约：readinessSnapshot 供 workflow 任意线程拉取（§10.2），
     *       实现须自行保证本方法可与其 UI 线程调用并发（快照值拷贝、
     *       短临界区——NFR-PERF-01）；纯查询，不抛（内部不可得以空清单
     *       表达，不虚构就绪事实）。
     */
    virtual std::vector<DomainReadinessItem>
    domainReadiness(StageId stage) const = 0;
};

// =====================================================================
// §11.4 关于框数据源端口（装配报告半区＋冻结版本基线半区；UI-T10 首消费
// 冻结，登记 ui.md §16.7 v1.2）
// =====================================================================

/**
 * @brief 关于对话框数据的 ui 自有最小端口（L5 适配装配器与冻结版本基线
 *        ——§11.4"版本数据由 L5 提供、ui 呈现"的注入面）。
 *
 * 语义冻结（不改义——ui.md §11.4/§10.9 原文）：
 *   - assemblyReports：装配报告查询（§10.9 IPluginUiRegistrar::
 *     assemblyReports() 同名同义——装配期累积、每插件恰好一条）。端口
 *     返回值形状＝PluginAssemblyReport（AboutDialog.hpp 承载的 §10.9 冻结
 *     形状），适配器从装配器报告直拷，不改字段语义；真实注册端口
 *     IPluginUiRegistrar 随装配任务落位后，L5 适配器改绑其查询（适配点
 *     单一，ui 侧零改动）。
 *   - versionBaseline：冻结版本基线投影（NFR-DEP-05——基线权威记录在
 *     L5/部署侧，本端口只回传已登记的呈现值）。WP-24-T01 基线产出前以
 *     available=false 承载（版本区呈现「未装载」占位——版本值零虚构）。
 *
 * 为什么两个方法在一个端口：两者的唯一消费者都是关于对话框（§11.4 单一
 * 呈现面），装配期一次性注入即可；变更节奏差异（报告随装配累积、基线
 * 冻结不变）不影响关于框"打开时现取现用"的消费形态（不缓存、不订阅）。
 *
 * 线程约束：调用一律发生在 UI 线程（§3.4 M-1 消费点——help.about 命令
 * 处理器打开关于框时现取）；实现应返回快照值（短临界区——NFR-PERF-01）。
 */
class IUiAboutDataSource {
public:
    virtual ~IUiAboutDataSource() = default;

    /**
     * @brief 取装配报告集（§10.9 assemblyReports 语义的 ui 侧投影）。
     *
     * @return 报告集（阶段 A 装配器未落地＝空集——关于框清单退化为白名
     *         单占位行，UI-PLG-2 的合法形态之一；纯查询，不抛——内部不
     *         可得以空集表达，不虚构装配事实）
     *
     * @note UI 线程调用；白名单外条目由消费侧装配函数丢弃（§11.4 交集）。
     */
    virtual std::vector<PluginAssemblyReport> assemblyReports() const = 0;

    /**
     * @brief 取冻结版本基线投影（NFR-DEP-05 的呈现值面）。
     *
     * @return 基线投影（available==false＝基线未装载——版本区占位呈现，
     *         不逐项伪造；纯查询，不抛）
     *
     * @note UI 线程调用；组件版本值为注入原样（ui 不改写、不补默认——
     *       "与冻结基线一致"的呈现半区语义）。
     */
    virtual AboutVersionBaseline versionBaseline() const = 0;
};

// =====================================================================
// C-3/C-5/C-8 会话生命周期端口（§5.2 状态机数据面——UI-T11 首消费冻结，
// 登记 ui.md §16.7 v1.3；O-31 裁决注入面："project/execution 实例随打开
// 流程注入 UiSessionController"的 ui 自有端口承载）
// =====================================================================

// 前置声明（SessionPortBundle 的 shared_ptr 成员只需不完整类型；完整
// 定义在各端口节——头内声明序：绑定集→工厂端口→回调面→三端口）。
class IUiProjectStorePort;
class IUiDraftQueryPort;
class IUiSessionTaskPort;

/**
 * @brief 会话端口绑定集（一次成功打开的协作面打包——§10.1 注释行
 *        "project/execution 实例随打开流程注入 UiSessionController"的
 *        ui 侧值承载，O-31 裁决）。
 *
 * 三端口与 C 表的对应：store＝C-3 关闭协议面、drafts＝C-5 只读清单面、
 * tasks＝C-8 查询/控制面。全部共享引用（所有权在 L5/装配层——§10.1
 * 所有权行原文）；控制器对旧项目保持 store 引用直到 subscribeClose
 * 回调（INV-SES-3——shared 持有即保活，§5.1 分离原则）。
 */
struct SessionPortBundle {
    /// 存储上下文端口（C-3——requestClose/closed/subscribeClose）。
    std::shared_ptr<IUiProjectStorePort> store;
    /// 草稿清单端口（C-5 只读半区——关闭对话框草稿区）。
    std::shared_ptr<IUiDraftQueryPort> drafts;
    /// 会话任务端口（C-8——任务区＋协作取消＋强制终止）。
    std::shared_ptr<IUiSessionTaskPort> tasks;
};

/**
 * @brief 打开五步协议的 ui 自有最小端口（L5 适配 project::ProjectStore
 *        Factory——§5.2 Opening 态"五步协议①~④在 ProjectStoreFactory 内"
 *        原文的 ui 侧入口）。
 *
 * 语义冻结（不改义——冻结基准 project.md §5.1 打开协议）：open 执行完整
 * 五步协议（定位/校验/锁获取/恢复/上下文构建），**成功才返回 ok=true**；
 * 降级只读（请求 Writable 而锁竞争/介质只读/权限不足）也是成功（PM-07
 * 不阻塞等待——降级事实以 writable=false＋readOnlyCause 表达）。失败
 * （校验失败/格式不识别等）返回 ok=false——"错误页定位具体文件；当前
 * 项目不动"（§5.3 行 6）。
 *
 * 为什么绑定集经出参而不是返回值成员：SessionPortBundle 的三个端口是
 * **由对端实例派生的适配器对象**（store 端口包装 ProjectStore 实例、
 * drafts 端口包装 DraftService、tasks 端口包装 ITaskScheduler/
 * ITaskController——§10.1 注释行"实例随打开流程注入"原文），其生命周期
 * 从属于本次打开；出参形态让"打开失败＝无绑定泄漏"在签名上自明（失败时
 * 装配层不写 outBindings）。
 *
 * UI-T11 消费状态：UiSessionController::openProject/beginSwitch 的候选
 * 验证调用面；ui 测试以可控替身承载（§3.1——替身同时充当"桩 Project
 * StoreFactory"角色）。
 */
class IUiStoreFactoryPort {
public:
    virtual ~IUiStoreFactoryPort() = default;

    /**
     * @brief 执行打开五步协议并产出会话端口绑定（§5.2 Opening 态入口）。
     *
     * @param canonicalPath [in] 项目规范路径（weakly_canonical 形态——
     *                      失败错误页"定位具体文件"的数据源，§5.3 行 6）
     * @param mode          [in] 请求模式（Writable＝失败降级只读不阻塞——
     *                      PM-07；ReadOnly＝显式只读，§5.5）
     * @param outBindings   [out] 成功时写入会话端口绑定集（store/drafts/
     *                      tasks 三端口由 L5 适配器从对端实例派生——
     *                      失败时不写入，保持调用方原值）
     * @return 打开结果投影（ok==true 时 opened 携带 writable 等 INV-SES-1
     *         唯一数据源；ok==false 时 failure 携带对端错误稳定 token——
     *         media-read-only/access-denied/format-legacy/schema-future
     *         等 StoreError 词表，§5.3 显示差异的判别输入）
     *
     * @note UI 线程调用（§5.2 时序——openProject 由 workflow 编排入口在
     *       UI 线程发起；五步协议内部的文件 IO 由对端 ProjectStoreFactory
     *       自行承担线程纪律，project.md §5.1——ui 不感知不复制）。
     */
    virtual OpenStoreOutcome open(const std::string& canonicalPath,
                                  UiOpenMode mode,
                                  SessionPortBundle& outBindings) = 0;
};

/**
 * @brief 存储上下文关闭完成回调面（subscribeClose 的观察者——§5.2
 *        Draining 态"订阅 subscribeClose（归档完成＋草稿 flush＋锁释放后
 *        回调）"原文的 ui 侧承载）。
 *
 * 回调语义（不改义）：存储上下文释放的前置＝本实例全部在途运行接纳归档
 * 完成＋草稿落盘完成（§5.7——requestClose 只是进入 Draining 的信号，不
 * 催促、不跳过归档），回调到达即 closed()==true。线程约束：回调可能在
 * 对端线程到达（project 归档路径），实现方（UiSessionController）负责
 * 按自身线程纪律消费（§3.4 M-1 同款自限——控制器状态仅在 UI 线程变更，
 * 迟到回调经有界轮询收敛，见控制器 pollDrain 注释）。
 */
class IUiStoreCloseObserver {
public:
    virtual ~IUiStoreCloseObserver() = default;

    /**
     * @brief 存储上下文已释放通知（closed()==true——§5.2 Draining→Closed
     *        迁移的触发面）。
     *
     * @param project [in] 已释放的项目身份（后台多持有点场景下区分是哪个
     *                旧项目——INV-SES-3 持有点按此释放）
     */
    virtual void onStoreClosed(const core::ProjectId& project) = 0;
};

/**
 * @brief 项目存储上下文的 ui 自有最小端口（L5 适配 project::ProjectStore
 *        ——C-3 关闭协议面：requestClose/closed/subscribeClose 三原语，
 *        §5.2 状态机图 Draining 框原文）。
 *
 * 语义冻结（不改义——project.md §5.1 关闭协议）：
 *   - requestClose()：拒绝新写；返回在途引用数（归档会话/在途事务/草稿
 *     落盘——§5.7 引用计数族）。**不催促、不跳过归档**（§5.7 原文——
 *     SA-17/A7 分离原则：UI 会话结束≠存储上下文结束）。
 *   - isClosed()：closed()==true 语义——在途引用清零且锁句柄已显式释放；
 *     兜底轮询面（§5.6 防线 4：subscribeClose 回调丢失时轮询 closed()）。
 *   - subscribeClose()：归档完成＋草稿 flush＋锁释放后回调一次。
 *
 * 所有权语义（INV-SES-3 的代码面）：ui 经 shared_ptr 持有本端口＝持有
 * 旧项目存储上下文的保活引用——"ui 不得提前销毁 ProjectStore 引用（保持
 * shared 持有直到 subscribeClose 回调）"（§5.1 分离原则原文）。端口实例
 * 的真实生命周期归 L5 装配层（ui 释放引用后由装配层决定对端实例去向）。
 */
class IUiProjectStorePort {
public:
    virtual ~IUiProjectStorePort() = default;

    /**
     * @brief 请求关闭存储上下文（§5.2 Draining 进入动作）。
     *
     * @return 在途引用数（归档会话/在途事务/草稿落盘——§5.7；>0 表示
     *         Draining 将持续到引用清零；0 表示无在途，上下文可即刻释放）
     *
     * @note UI 线程调用；重复调用语义（取消等待后再次关闭）由对端保证
     *       幂等（project.md §5.1 关闭协议——信号语义，非命令队列）。
     */
    virtual std::uint32_t requestClose() = 0;

    /**
     * @brief 查询上下文是否已释放（§5.6 防线 4 的兜底轮询面）。
     *
     * @return true＝在途引用清零且锁句柄已释放（closed()==true 语义）
     */
    virtual bool isClosed() const = 0;

    /**
     * @brief 订阅关闭完成回调（§5.2 Draining 态订阅动作）。
     *
     * @param observer [in] 回调观察者（弱引用语义——观察者析构前调用方
     *                 须先退订；UiSessionController 以成员身份持有订阅
     *                 句柄，句柄析构即退订）
     * @return RAII 订阅句柄（unique_ptr——析构即退订，重复退订幂等）
     */
    virtual std::unique_ptr<core::IEventSubscription>
    subscribeClose(IUiStoreCloseObserver& observer) = 0;
};

/**
 * @brief 草稿清单查询的 ui 自有最小端口（L5 适配 project::DraftService::
 *        list——C-5 只读半区，§5.4 S1"DraftService::list(branch)＋会话脏
 *        模块→草稿区"的数据面）。
 *
 * 为什么只有读方法：关闭对话框对草稿只有**呈现**与**处置决议**两个动作
 * ——保存/放弃的执行归 DraftController（§8.1 分工红线/UI-T12），本端口
 * 不承载写语义（§5.5 只读禁用清单在写侧另有双层防线）。会话脏模块由
 * UiSessionController 以会话态补入装配（DraftPresenceProjection.
 * sessionDirty 同源的行级展开）。
 */
class IUiDraftQueryPort {
public:
    virtual ~IUiDraftQueryPort() = default;

    /**
     * @brief 取草稿清单投影（§5.4 S1 草稿区行集）。
     *
     * @return 草稿行集（磁盘存在未应用草稿的模块清单；空＝无磁盘草稿；
     *         纯查询，不抛——内部不可得以空清单表达，不虚构草稿事实）
     *
     * @note UI 线程调用；应返回快照值（NFR-PERF-01）。
     */
    virtual std::vector<DraftRowProjection> listDrafts() const = 0;
};

/**
 * @brief 草稿写半区的 ui 自有最小端口（L5 适配 project::DraftService::
 *        save/tryLoad/discard——C-5 写半区，UI-T12 首消费冻结，登记
 *        ui.md §16.7 v1.4；§8 DraftController 的对端执行面）。
 *
 * 为什么读写在两个端口：C-5 读半区（IUiDraftQueryPort）随 UI-T11 冻结为
 * 关闭对话框的呈现面；写半区（本端口）的消费面是 §8 DraftController——
 * 两者调用线程纪律不同（读＝UI 线程短查询；写＝一律经 ui 后台落盘线程
 * 串行执行，§3.4 线程表"草稿 save 调用"行）。分端口让两种线程契约各自
 * 单一，适配器不必在同一类型内同时满足两类纪律。
 *
 * 语义冻结（不改义——冻结基准 project.md §5.4，O-31 值投影承载）：
 *   - save：仅落 drafts/ 目录（单文件原子替换＋.bak 轮换），**不产生任何
 *     修订**（PM-04 保存/应用分离红线）；只读/关闭/失权拒绝走返回值轨
 *     （DraftSaveOutcome.ok=false＋稳定 token），不抛环境错误；
 *   - tryLoad：无草稿＝Missing（常态，非错误）；current 损坏→对端自动
 *     回退 .bak（RecoveredFromBackup，旧损坏文件保留供人工核查——PM-08）；
 *     两者皆不可得＝Corrupt（稳定 token＋明细透传）；.new 崩溃残留由对端
 *     触达即丢弃并置 newResidueDropped（§8.3-3 恢复横幅素材）；
 *   - discard：目标态＝该草稿文件不存在（幂等——本来就没有也是 ok）；
 *     写操作（§8.3-4"放弃＝显式 discard"），只读上下文被对端拒绝。
 *
 * 线程约束：实现须线程安全（L5 适配对端"任意线程可调（内部串行）"语义
 * ——project.md §9.8）；ui 侧仅从 §3.4"ui 后台落盘线程"经 DraftController
 * 的串行执行器调用本端口（UI 线程零磁盘 IO 红线的执行面——产品代码把
 * 端口调用封闭在执行器任务内，测试以线程标记替身自证）。
 */
class IUiDraftStorePort {
public:
    virtual ~IUiDraftStorePort() = default;

    /**
     * @brief 落盘一份草稿文档（§8.2 数据流"转投 ui 后台落盘线程→save"）。
     *
     * @param document [in] 草稿文档投影（DraftDocumentProjection——归属
     *                 三元组/baseRevisionId/payload/origin 值拷贝；ui 不
     *                 解析 payload，L5 适配器原样装回 project::DraftDocument）
     * @return 落盘结果（ok=false 时 errorToken 非空——对端 StoreError
     *         稳定 token 直用，不镜像枚举，NFR-MNT-03）
     */
    virtual DraftSaveOutcome save(const DraftDocumentProjection& document) = 0;

    /**
     * @brief 读取一份草稿（§8.3 恢复流程第 1~2 步的对端执行面）。
     *
     * @param moduleId [in] 模块 token（drafts/&lt;branch&gt;/&lt;module&gt;.draft.json
     *                 的 module 段；白名单校验归对端）
     * @return 读取结果（四态词表见 DraftLoadOutcome；损坏回退 .bak 的
     *         事实经 status==RecoveredFromBackup 表达，诊断明细随附）
     */
    virtual DraftLoadOutcome tryLoad(const std::string& moduleId) = 0;

    /**
     * @brief 放弃一份草稿（§8.3-4 恢复横幅[放弃]的执行面——显式 discard）。
     *
     * @param moduleId [in] 模块 token
     * @return 放弃结果（ok=false 时 errorToken 非空——只读上下文拒绝等）
     */
    virtual DraftDiscardOutcome discard(const std::string& moduleId) = 0;
};

/**
 * @brief 会话任务查询与控制的 ui 自有最小端口（L5 适配 execution::
 *        ITaskScheduler/ITaskController——C-8，§5.4 任务区与 §5.6 防线
 *        的数据/控制面）。
 *
 * 语义冻结（不改义）：
 *   - nonTerminalTasks ← ITaskScheduler::tasksByProject(pid) 过滤非终态
 *     （§9.5"对话框任务清单"原文；非终态词表＝core::TaskState ∈
 *     {Queued, Preparing, Running, Paused, Canceling}，§6.3）；
 *   - requestCancel ← ITaskController::requestCancel（协作取消——2 s 进入
 *     Canceling/10 s 收敛由 execution 保证，NFR-PERF-02；ui 只发请求，
 *     不等待收敛；"正常取消无错误诊断"UX-03）；
 *   - requestForceTerminate ← ITaskController::requestForceTerminate
 *     （强杀——任务记 Failed＋EX-FORCE-TERMINATED，最近检查点保留可续，
 *     §5.6 防线 3/§9.4；独立高级操作带确认，关闭对话框常规路径不提供）。
 */
class IUiSessionTaskPort {
public:
    virtual ~IUiSessionTaskPort() = default;

    /**
     * @brief 取指定项目的非终态任务行集（§5.4 S1 任务区数据装配）。
     *
     * @param project [in] 项目身份（tasksByProject 的等值过滤键）
     * @return 非终态任务行（§9.5 过滤语义；空＝无在途任务；纯查询，不抛）
     *
     * @note UI 线程调用；应返回快照值（NFR-PERF-01——§9.4 任务清单行
     *       "UI 不阻塞等待任务"原文）。
     */
    virtual std::vector<TaskRowProjection>
    nonTerminalTasks(const core::ProjectId& project) const = 0;

    /**
     * @brief 请求协作取消一个任务（§5.4 S1[协作取消] 的逐任务执行面）。
     *
     * @param task [in] 任务身份五元组（RunRegistry 核对面——execution
     *             §7 接纳判定权威在对端，ui 透传身份不加工）
     * @return 请求受理位（对端 Ack 的折叠——true＝已受理进入取消协议；
     *         false＝对端拒绝/任务已终态；受理后收敛时序归 NFR-PERF-02，
     *         ui 经 Draining 轮询观察，不阻塞等待）
     */
    virtual bool requestCancel(const core::TaskIdentity& task) = 0;

    /**
     * @brief 请求强制终止一个任务（§5.6 防线 3 的执行面——仅 T_force
     *        确认后调用）。
     *
     * @param task [in] 任务身份五元组
     * @return 请求受理位（对端 Ack 折叠；强杀后果＝任务记 Failed＋
     *         EX-FORCE-TERMINATED＋最近检查点保留可续——§9.4 后果说明
     *         原文，呈现于 ForceCloseDialogData）
     */
    virtual bool requestForceTerminate(const core::TaskIdentity& task) = 0;
};

// =====================================================================
// C-6 交互回调端口（§9.2 确认流——UI-T13 首消费冻结，登记 ui.md §16.7
// v1.5；O-31 裁决方向外翻的 ui 侧承接面）
// =====================================================================

/**
 * @brief 命令确认交互回调的 ui 自有端口（O-31 v0.4 §2.1.2 C-6 行：
 *        "L5 适配器实现 project::ICommandInteraction，委托 ui 自有交互
 *        回调端口"——CommandInteractionBridge 实现本端口，§9.2）。
 *
 * 为什么要自有端口而不是 ui 直接实现 project 接口：O-31 裁决下 ui 产品
 * 面对 project 零链接零 include——若 Bridge 实现 project::ICommandInteraction，
 * ui 头必须 include project 公共头（增边违红线）。方向外翻后依赖倒转：
 * L5 装配器同时看见两边，写单行适配器（实现 project 接口、把调用原样
 * 委托给本端口）；ui 保持零对端知识。**形状逐字镜像** project.md §5.3.3
 * 冻结点（isAlive＋requestConfirmations 两方法、签名与返回值语义一致）——
 * P-PR-7 处置："Bridge 线程模型按 project.md §5.3.3 冻结点实现，对端
 * 单侧冻结互为起点不私改"。适配器不得改义（不得吞错/不得自行放行）。
 *
 * 语义冻结（不改义——§9.2 全节）：
 *   - isAlive：会话存活探针——UI 会话在拆除（窗口关闭/项目切换的
 *     Draining 之后）时置 false；命令等待期收到关闭请求由 project 主动
 *     取消（Aborted(interaction-lost)，无修订、无用户诊断——§5.6-5）。
 *   - requestConfirmations：命令执行线程同步调用（占命令槽、零事务
 *     资源、无超时自动确认——P-DIAG-6）；实现内 Marshal 至 UI 线程弹
 *     确认对话并阻塞等待；返回值三态＝全部确认（凭据向量与 findings
 *     一一对应）/整体拒绝（nullopt——对端按 Rejected(confirmations-
 *     rejected) 处置，无修订）/会话失效（nullopt——interaction-lost）。
 *     实现侧（Bridge）只交 credentials：确认判定权、submitConfirmations
 *     留痕、编译前复核全在 project＋diagnostics（SA-15——ui 不持判定权
 *     不自行放行）。
 *
 * 线程约束：isAlive 任意线程（原子读）；requestConfirmations 仅命令
 * 执行线程（对端 ProjectCommandService §6.1 线程模型传导）。
 */
class IUiCommandInteraction {
public:
    virtual ~IUiCommandInteraction() = default;

    /**
     * @brief 会话存活判定（false＝界面会话已关闭——§5.3.3
     *        interaction-lost 的探针半区）。
     *
     * @return true＝会话在；false＝已拆除（等待方须立即收束）
     */
    [[nodiscard]] virtual bool isAlive() const = 0;

    /**
     * @brief 请求用户确认（§5.3.3 冻结签名的镜像——见类注释三态语义）。
     *
     * @param findings [in] 待确认集（比较型可确认诊断——SA-15；
     *                 空集＝无事可确认，实现直接返回空凭据向量）
     * @return 与 findings 一一对应的凭据向量（全部确认）；nullopt＝整体
     *         拒绝或会话失效（对端按拒绝/中止处置——两者对 ui 同为
     *         "不产生修订"，判别由对端探针时序决定）
     */
    virtual std::optional<std::vector<core::ConfirmationCredential>>
    requestConfirmations(const std::vector<core::ConfirmableFinding>& findings) = 0;
};

// =====================================================================
// C-8 任务呈现面端口（§9.4/§10.7——UI-T13 首消费冻结，登记 ui.md
// §16.7 v1.5；v1.3 冻结的 IUiSessionTaskPort 三原语面保持不动）
// =====================================================================

/**
 * @brief 任务查询与控制的 ui 自有最小端口·呈现面（L5 适配 execution::
 *        ITaskScheduler/ITaskController——C-8，§9.4 任务呈现与 §10.7
 *        ITaskPresentationModel 的对端执行面）。
 *
 * 为什么与 IUiSessionTaskPort 分立：v1.3 冻结的会话任务端口只承载关闭
 * 对话框的三原语（非终态清单/协作取消/强制终止），本端口承载 §9.4 全量
 * 呈现面（全态清单＋进度＋暂停/继续＋Ack 反馈）——两者消费者不同（关闭
 * 对话框 vs 任务面板）、方法集不同。已冻结形状不触碰（增量修订只允许
 * 表尾追加，改动冻结方法＝契约变更），另立端口是 O-31"首消费冻结"机制
 * 的标准动作（IUiDraftQueryPort/IUiDraftStorePort 读写分立同案）。
 *
 * 语义冻结（不改义——冻结基准 execution.md §10.1/§10.2，O-31 值投影
 * 承载，TaskViewProjection/UiTaskAck 字段注释逐条锚定对端类型）：
 *   - tasksByProject ← ITaskScheduler::tasksByProject（§9.4 任务清单
 *     数据；含终态——近终态窗口行的呈现数据源，§10.7"非终态＋近终态
 *     窗口"由消费方过滤）；
 *   - task/progress ← tryTask/progress 查询（深拷贝快照——§4.2"查询经
 *     快照拷贝"；进度轮询 §9.4"UI 线程 200 ms 轮询"的对端半区）；
 *   - requestCancel/requestPause/requestResume/requestForceTerminate ←
 *     ITaskController 同名方法（Ack 结构投影——accepted＋currentState＋
 *     feedback 显式反馈；不支持暂停的任务收到请求→feedback 非空"显式
 *     提示不静默"，§9.4 原文；**全部即发即忘**——ui 不阻塞等待收敛，
 *     NFR-PERF-02"ui 侧只发请求"）。
 *
 * 线程约束：ui 侧仅 UI 线程调用（§3.4 M-1）；实现须线程安全（对端
 * execution 接口线程安全——§10.7 线程行"内部转发到 execution 任意线程
 * 入口"）；应返回快照值（NFR-PERF-01——UI 线程零长查询）。
 */
class IUiTaskPresentationPort {
public:
    virtual ~IUiTaskPresentationPort() = default;

    /**
     * @brief 取指定项目的任务快照清单（§9.4 任务清单数据装配）。
     *
     * @param project [in] 项目身份（tasksByProject 的等值过滤键）
     * @return 任务行快照（含终态近窗口行——§10.7"近终态任务保留窗口
     *         （默认 30 min，对齐 execution D-07）"的呈现数据由对端
     *         会话内存态决定；空＝无任务；纯查询，不抛）
     *
     * @note UI 线程调用；应返回快照值（§4.2"查询永不被状态写阻塞"）。
     */
    virtual std::vector<TaskViewProjection>
    tasksByProject(const core::ProjectId& project) const = 0;

    /**
     * @brief 按身份取单个任务快照（§10.7 task 的对端半区）。
     *
     * @param task [in] 任务身份五元组（寻址键——RunRegistry 核对面）
     * @return 任务行快照；不存在（终态回收/他项目任务）→nullopt（不虚构）
     */
    virtual std::optional<TaskViewProjection>
    task(const core::TaskIdentity& task) const = 0;

    /**
     * @brief 查询任务最近进度（§9.4"200 ms 轮询 progress(taskId)"的
     *        对端半区——调度器节流 ≤10 Hz 的查询式投影）。
     *
     * @param task [in] 任务身份五元组
     * @return 最近进度投影（nullopt＝尚无进度〔Queued/Preparing 期〕或
     *         任务不存在——两态对呈现同为"无进度可显示"，不区分不虚构）
     */
    virtual std::optional<TaskProgressProjection>
    progress(const core::TaskIdentity& task) const = 0;

    /**
     * @brief 请求协作取消（§9.4"取消＝协作"——正常取消无错误诊断
     *        UX-03；2 s 进入 Canceling/10 s 收敛由 execution 保证，
     *        ui 只发请求不等收敛）。
     *
     * @param task [in] 任务身份五元组
     * @return Ack 投影（accepted=false 时 feedback 承载对端显式反馈）
     */
    virtual UiTaskAck requestCancel(const core::TaskIdentity& task) = 0;

    /**
     * @brief 请求暂停（§9.4"暂停/继续按任务能力声明启用；不支持暂停的
     *        任务收到请求→StatusAck.feedback 显式提示不静默"原文）。
     *
     * @param task [in] 任务身份五元组
     * @return Ack 投影（不支持暂停→accepted=false＋feedback 非空——
     *         呈现层必须显示该反馈，不得静默吞掉）
     */
    virtual UiTaskAck requestPause(const core::TaskIdentity& task) = 0;

    /**
     * @brief 请求继续（§9.4 Paused⇄Running——新 attemptId，继续不重复
     *        统计；进度基线随新 attempt 重置的呈现半区）。
     *
     * @param task [in] 任务身份五元组
     * @return Ack 投影
     */
    virtual UiTaskAck requestResume(const core::TaskIdentity& task) = 0;

    /**
     * @brief 请求强制终止（§9.4"强杀＝独立高级操作（带确认＋后果说明：
     *        任务记 Failed、检查点保留）"的对端执行面——调用前置＝用户
     *        已在独立确认对话中明确确认，§10.7"高级操作＋确认"）。
     *
     * @param task [in] 任务身份五元组
     * @return Ack 投影（后果＝任务记 Failed＋EX-FORCE-TERMINATED＋
     *         最近检查点保留可续——§9.4 后果说明原文）
     */
    virtual UiTaskAck requestForceTerminate(const core::TaskIdentity& task) = 0;
};

// =====================================================================
// C-9 确认投影呈现半区端口（§9.2"非命令期 pending 查看"行——UI-T13
// 首消费冻结，登记 ui.md §16.7 v1.5）
// =====================================================================

/**
 * @brief 待确认项只读查询的 ui 自有最小端口（L5 适配 project→
 *        diagnostics::IConfirmableFindingService::pendingFor——§9.2
 *        "pendingFor(revision) 投影只读显示（ui 经 project 间接读——
 *        不直接调 diagnostics 服务端接口）"原文的注入面）。
 *
 * 为什么经端口而不是直接持有 IConfirmableFindingService：§9.2 规则表
 * 明文"ui 经 project 间接读"——确认记录的服务端归属是 project 命令编排
 * （§5.3.3/§6.7），ui 直接调服务端接口会绕过该归属（且 diagnostics
 * 服务接口对 ui 的合法调用面不含"列待确认项"——§9.3 合法调用面行）。
 * L5 适配器持有 project 会话上下文，能按"当前修订"取 pendingFor；
 * ui 只见只读值拷贝（呈现用，不改状态——FindingRecord 值语义）。
 *
 * 线程约束：UI 线程调用（§3.4 M-1；确认对话数据装配路径）；实现应短小
 * （快照值拷贝——NFR-PERF-01）。未注入（空指针）＝无确认呈现面——
 * pendingConfirmations 返回空集，不虚构（§11.4 零虚构同案）。
 */
class IUiFindingQueryPort {
public:
    virtual ~IUiFindingQueryPort() = default;

    /**
     * @brief 取当前会话全部待确认项（§10.8 pendingConfirmations 的
     *        对端半区——非命令期 pending 只读查看的数据源）。
     *
     * @return 服务端记录值拷贝清单（Pending 态；空＝无待确认项；纯
     *         查询，不抛——内部不可得以空集表达）
     */
    virtual std::vector<diagnostics::FindingRecord> pendingConfirmations() const = 0;
};

// =====================================================================
// Tier-U 日志面板数据源端口（§9.1 日志面板行——UI-T13 首消费冻结，
// 登记 ui.md §16.7 v1.5）
// =====================================================================

/**
 * @brief Tier-U 用户日志只读快照的 ui 自有最小端口（L5 适配 diagnostics
 *        日志设施——§9.1"日志面板 Tier-U（用户诊断：无调用栈/内部哈希）"
 *        的数据面）。
 *
 * 为什么需要端口：ILogger（Logging.hpp §9.6）是只写接口（无读回/订阅
 * 面——日志写文件管线，§7.3）；面板"回看最近用户日志"需要读已写行，
 * 读回能力归持有日志文件的 L5 侧。ui 只消费脱敏后的 Tier-U 快照
 * （Tier-U 行在管线内已过内部模式过滤——NFR-REL-05"无调用栈/内部
 * 哈希"由产出侧保证，ui 不二次脱敏——§9.1 脱敏行"呈现层不自行脱敏"）。
 *
 * flush 纪律（§3.4 线程表"禁止等待 flush（ILogger flush 仅关闭路径）"）：
 * 本端口**不提供** flush/写面——日志面板是纯只读呈现；flush 只发生在
 * L5 关闭路径直接调 diagnostics 设施（ui 头不存在该调用点＝结构性保证）。
 *
 * 线程约束：UI 线程调用；实现应返回快照值（短临界区——NFR-PERF-01）。
 * 未注入（空指针）＝无日志面板数据——面板呈现占位，不虚构条目。
 */
class IUiUserLogSource {
public:
    virtual ~IUiUserLogSource() = default;

    /**
     * @brief 取 Tier-U 日志快照（面板回看数据，最新在后）。
     *
     * @return 用户级日志条目快照（空＝无日志；纯查询，不抛）
     */
    virtual std::vector<UserLogEntry> snapshot() const = 0;

    /**
     * @brief 开发日志文件是否可跳转（§9.1"Dev 级仅'打开开发日志文件'
     *        跳转（不内嵌显示）"的可用位——文件路径归 L5，ui 不解析）。
     *
     * @return true＝L5 侧可提供开发日志文件入口（呈现"打开开发日志"
     *         动作）；false＝不可得（动作隐藏，不虚构路径）
     */
    virtual bool devLogFileAvailable() const = 0;
};

// =====================================================================
// 脱敏配置端口（§9.1 脱敏行——UI-T13 首消费冻结，登记 ui.md §16.7 v1.5）
// =====================================================================

/**
 * @brief 脱敏策略配置的 ui 自有最小端口（L5 适配 diagnostics::
 *        IRedactionService——§9.1"ui 只提供配置界面（L5 会话变更
 *        setPolicy）"的注入面）。
 *
 * 分工红线（§9.1 脱敏行原文）：呈现层不自行脱敏——显示前文本已由
 * diagnostics 管线处理（诊断上下文快照/日志行在产出侧脱敏）；ui 只提供
 * **配置界面**（策略选择值经本端口交给 L5，L5 调 IRedactionService::
 * setPolicy 写时拷贝切换）。ui 不持有 IRedactionService 引用、不复算
 * 脱敏结果（§10.8 IDiagnosticPresentationModel.redactedPath 的转发面
 * 由装配把服务只读引用注入模型——配置**写**路径只经本端口）。
 *
 * 线程约束：UI 线程调用（配置界面交互路径；对端 setPolicy 并发安全
 * ——Redaction.hpp 契约表"策略快照读/写时拷贝切换"）。
 */
class IUiRedactionPolicyPort {
public:
    virtual ~IUiRedactionPolicyPort() = default;

    /**
     * @brief 取当前脱敏策略快照（配置界面回显当前值）。
     *
     * @return 当前策略（nullopt＝服务未挂接——界面呈现"未配置"占位，
     *         不虚构默认值）；纯查询，不抛
     */
    virtual std::optional<diagnostics::RedactionPolicy> currentPolicy() const = 0;

    /**
     * @brief 应用新脱敏策略（配置界面[确定]的执行半区——L5 转调
     *        IRedactionService::setPolicy，写时拷贝切换：变更后新调用
     *        生效，已写行不回溯——§9.5 配置行原文）。
     *
     * @param policy [in] 新策略（值拷贝）
     */
    virtual void applyPolicy(const diagnostics::RedactionPolicy& policy) = 0;
};

}  // namespace ui
}  // namespace ird

#endif  // SDURWS_IRD_UI_UIPORTS_HPP