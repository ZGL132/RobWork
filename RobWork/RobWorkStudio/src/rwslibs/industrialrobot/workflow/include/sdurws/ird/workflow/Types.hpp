/**
 * @file   Types.hpp
 * @brief  workflow 单元公共词表与值类型——建议动作 token、域↔阶段映射、
 *         门控/建议文案键的唯一构造点与单元错误类型（O2/O3 的词表面）。
 *
 * 设计依据：
 *   - units/workflow.md §3.1（公共头布局表——Types.hpp 行："门控/建议/流程
 *     词表（复用 ui StageId——零新增枚举）〔WP-22-T03+〕"）、§4.2（阶段—域
 *     映射表——域→阶段多对一映射规则 D-WF-2 的词表权威）、§6.2/§6.3（UX-01
 *     四要素与建议八规则——actionKey token 归本卡词表）、§6.5/§14.1 D-WF-7
 *     （R1 零新增 WF- 稳定诊断码——本头不含任何诊断码，只有文案键）
 *   - 需求 UX-01（四要素——目标/必需输入/当前问题/下一步操作）、UX-02
 *     （工程用语——建议与提示全部走文案键，零哈希/Schema/内部插件名）、
 *     UX-12（七阶段门控——阶段词表与顺序冻结于 ui §10.2/§6.4）
 *   - 任务契约 tasks/foundation/WP-22-T03.json acceptance 2/3（建议八规则
 *     优先级词表；门控数据形状按 P-WF-2 谈判起点、词表零新增）
 *
 * 背景说明（本头为什么只放"词表与映射"，不放判定规则）：workflow 是编排
 * 单元——它消费各域自报的就绪投影（ui StageStatusModel 汇聚快照）与平台
 * 事件，产出七阶段导航的门控状态与"下一步建议"。本头承载的是这套产出物
 * 中**语义 token 层**：建议动作叫什么（actionKey 八 token）、域与阶段如何
 * 对应（§4.2 映射表）、文案键如何拼（UX-02 键半区）。判定规则本身在
 * Gate.hpp/IStageGateService 与 Advice.hpp/INextStepAdvisor——词表与规则
 * 分头，让 ui 侧消费词表时不必依赖规则实现。
 *
 * P-WF-2 谈判起点声明（契约 acceptance 3）：门控数据形状（GateDecision
 * 字段/status 枚举归属/reasonKeys 词表载体）三方契约未定稿——本单元按
 * workflow.md §4.3 现状实现：status 复用 ui::StageViewStatus 六值（零新增
 * 枚举——D-WF-4）；reasonKeys/unlockHintKey/文案键以 ui::TextKey 承载，
 * 键串形态遵 ui.md §3.5 文案键体系（"stage.<id>.title"族的 stage.<token>
 * 前缀约定），**键串构造集中在本头函数（唯一映射点，NFR-MNT-03）**；token
 * 词表载体若经 P-WF-2 裁决移交 ui 公共头，仅需迁移本头的构造实现，消费方
 * 接口不变。
 *
 * 线程安全：纯函数与编译期常量（并发只读安全）；WorkflowError 为异常值
 * 类型（抛出与捕获无共享状态）。
 */

#ifndef SDURWS_IRD_WORKFLOW_TYPES_HPP
#define SDURWS_IRD_WORKFLOW_TYPES_HPP

#include <stdexcept>
#include <string>
#include <vector>

#include <sdurws/ird/ui/UiTypes.hpp>  // ui::StageId/ui::TextKey——词表唯一权威（D-WF-4 零新增枚举）

namespace sdurws {
namespace ird {
namespace workflow {

// =====================================================================
// 单元错误类型（§10.3 接口属性表"错误语义"行：调用方错误 fail-fast）
// =====================================================================

/**
 * @brief workflow 单元调用方契约违约异常（fail-fast——不带病前进）。
 *
 * 触发场景（全部属于**调用方错误**，不是环境错误——环境/对端错误走对端
 * 稳定诊断码透传，不经本类型，§10.3 错误语义行）：
 *   - evaluate()：七阶段快照板的 stage 字段与数组序位不符（组装错误）；
 *     快照 epoch 低于水位（I-WF-4——旧投影不得覆盖新事件，§5.3 步骤 2）；
 *   - mapInvalidation()：传入非失效事件（本接口只消费
 *     DependencyInvalidated——§10.2 @note）；快照 epoch 低于门控状态
 *     epoch（旧投影）；signal.domainKeys 出现映射词表外域名（对端给的
 *     受影响域键越界）。
 *   - stageForDomain()：词表外域名。
 *
 * 语义：抛出即表示调用序列存在缺陷，**必须**修复调用方而不是捕获后继续
 * ——与 core Errors.hpp 的调用方错误 fail-fast 精神一致（PA-1/各单元
 * 任务卡错误语义行同款口径）。
 */
class WorkflowError : public std::runtime_error {
public:
    /// @param message [in] 违约说明（人读中文，日志/断言消息用——不进
    ///        用户可见面，UX-02：界面只见文案键解析结果）。
    explicit WorkflowError(const std::string& message)
        : std::runtime_error("workflow/caller-contract: " + message) {}
};

// =====================================================================
// 建议动作 token 词表（§6.3 建议规则表 actionKey 列——本卡词表；
// 文案值归 diagnostics/ui 文案体系，此处只有语义 token）
// =====================================================================

/**
 * @brief 八条建议规则的动作 token（§6.3 表 actionKey 列冻结词形）。
 *
 * token 是建议的**语义身份**（机器可判读、测试可断言），不是文案：用户
 * 看到的文字经 adviceTitleKey(actionKey) 得文案键、再经 ui UiText 解析
 * （UX-02 工程用语的键/值半区分工）。规则与 token 的对应：
 *   R1 补全输入→fix-inputs；R2 发起计算→run-evaluation；
 *   R3 等待/查看任务→review-active-task；R4 复算下游→recompute-downstream；
 *   R5 处理阻塞诊断→resolve-findings；R6 前进下一阶段→advance-stage；
 *   R7 处理恢复项→recover-session；R8 只读提示→readonly-notice。
 */
inline constexpr const char* kActionFixInputs          = "fix-inputs";
inline constexpr const char* kActionRunEvaluation      = "run-evaluation";
inline constexpr const char* kActionReviewActiveTask   = "review-active-task";
inline constexpr const char* kActionRecomputeDownstream= "recompute-downstream";
inline constexpr const char* kActionResolveFindings    = "resolve-findings";
inline constexpr const char* kActionAdvanceStage       = "advance-stage";
inline constexpr const char* kActionRecoverSession     = "recover-session";
inline constexpr const char* kActionReadonlyNotice     = "readonly-notice";

// =====================================================================
// 域↔阶段映射词表（§4.2 阶段—域映射表——O2"映射规则"的唯一定义点）
// =====================================================================

/**
 * @brief 域注册键 → 所属阶段的映射（§4.2 表——域→阶段多对一）。
 *
 * 这是级联失效提示（UX-12）与门控投影取数的**导航映射**权威：evidence
 * 判定失效范围给出"受影响域键"，本函数把它翻译为"受影响阶段"——workflow
 * 不复制失效计算（D-WF-2/N9），只做这一步域→阶段导航映射。
 *
 * 词表来源（§4.2 表"聚合域投影"列）：modeling/requirements/kinematics/
 * trajectory/dynamics/selection/optimization/reporting 八键；trajectory
 * 与 dynamics 同属轨迹/动力学聚合阶段（§4.2 聚合判定行）。
 *
 * @param domainKey [in] 域注册键（域插件注册词表——§4.2 原文小写形态）
 * @return 所属阶段（ui::StageId 七值词表——零新增）
 * @throws WorkflowError 词表外域名（调用方/对端契约违约——fail-fast：
 *         未知域的失效范围无法导航，静默忽略会把失效提示丢进黑洞）
 */
ui::StageId stageForDomain(const std::string& domainKey);

/**
 * @brief 阶段的聚合域键清单（§4.2 表逐行——阶段就绪投影的取数词表）。
 *
 * 用途：evaluate() 逐阶段从 StageReadinessSnapshot.domains 中按本清单
 * 认领域就绪项——清单内域缺失＝域未装配（Unavailable 判定输入，§4.3
 * 词表）；聚合阶段 TrajectoryDynamics 返回两键（§4.2"两域均就绪才解锁"
 * 聚合判定的输入形状）。
 *
 * @param stage [in] 阶段值（ui::StageId 七值词表）
 * @return 该阶段的域键清单（按 §4.2 表列序稳定；TrajectoryDynamics 为
 *         trajectory→dynamics 序——确定性 NFR-COR-02；生命周期＝静态词表
 *         并发只读安全；调用方不取得所有权）
 * @throws WorkflowError stage 越界（防御性——七值封闭词表外不可构造，
 *         但跨单元边界仍校验，调用方错误 fail-fast）
 */
const std::vector<std::string>& stageDomains(ui::StageId stage);

// =====================================================================
// 文案键构造（UX-02 键半区的唯一映射点——键串形态遵 ui.md §3.5 体系）
// =====================================================================

/**
 * @brief 阶段目标文案键（UX-01 四要素之"目标"——§6.2 stage→goalKey
 *        静态词表的构造点）。
 *
 * 键形："stage.<token>.goal"（遵 ui.md §3.5 "stage.<id>.title" 的
 * stage.<token> 前缀约定；token 经 ui::stageToken 唯一取得——禁止调用方
 * 自拼 token，NFR-MNT-03）。值（中英文）归 ui 文案资源——本函数只产键。
 *
 * @param stage [in] 阶段值
 * @return 目标文案键（工程用语键——经 ui UiText 解析后呈现）
 */
std::string stageGoalKey(ui::StageId stage);

/**
 * @brief 阶段门控阻塞原因文案键（GateDecision.reasonKeys 的 Blocked 行）。
 *
 * 键形："stage.<token>.gate.blocked"——blocked 行"附原因＋下一步建议"
 * （ui §6.4 表）的原因半区；缺项明细经 GateDecision.missingItemKeys
 * （域自报缺项键透传，不在此拼装——I-WF-2 不重算）。
 */
std::string gateBlockedReasonKey(ui::StageId stage);

/**
 * @brief 阶段解锁条件文案键（GateDecision.unlockHintKey——§6.4 时序
 *        "拒绝(原因/解锁条件)"的解锁半区）。
 *
 * 键形："stage.<token>.gate.unlock"。R8 只读提示（§6.3）也复用本键族
 * 作为解锁条件提示的 detailKeys 载体。
 */
std::string gateUnlockHintKey(ui::StageId stage);

/**
 * @brief 前序阶段未通过的原因文案键（下游阶段 NotStarted 的 reasonKeys）。
 *
 * 键形："stage.<token>.gate.not-reached"——token 为**前序**阶段：下游
 * 锁定的根因在前序（§4.4 状态机 NotStarted ← 前序未过），提示把用户
 * 指向前序解锁，而不是让下游阶段背缺项（缺项清单是本域自报事实——
 * 前序未过时本域可能尚无有效缺项）。
 */
std::string gateNotReachedReasonKey(ui::StageId stage);

/**
 * @brief 阶段域未装配原因文案键（Unavailable 判定的 reasonKeys 基键）。
 *
 * 键形："stage.<token>.gate.domain-unavailable"——域未装配（缺插件/
 * 装配失败，§4.3 词表）区别于缺项 Blocked（WF-VER-103 观测点）；具体
 * 缺失域键经 GateDecision.reasonKeys 中逐域追加的
 * "stage.<token>.gate.missing-domain.<domainKey>" 形态键（见
 * missingDomainReasonKey）。
 */
std::string gateDomainUnavailableKey(ui::StageId stage);

/**
 * @brief 单个缺失域的原因文案键（Unavailable 判定的逐域 reasonKeys）。
 *
 * 键形："stage.<token>.gate.missing-domain.<domainKey>"——domainKey 是
 * 域注册词表键（§4.2，非哈希非内部标识，UX-02 红线不受影响）。
 */
std::string missingDomainReasonKey(ui::StageId stage, const std::string& domainKey);

/**
 * @brief 结果过期标记文案键（级联失效"results-stale 并存呈现"的标记键，
 *        §4.3 级联映射——Completed 门控态不变、提示数据另行携带）。
 *
 * 键形："stage.<token>.gate.results-stale"。进入 problemKeys（§6.2
 * "当前问题"要素）与 R4 建议的 detailKeys；逐条失效原因（对象定位＋
 * 旧→新差异）是结构化数据（evidence::InvalidationReason，StaleHint
 * 携带），不经本键承载——键只标记"有过期"，明细由呈现层从结构化数据
 * 解析（UX-02：数值与对象名经名称端口呈现，不进键）。
 */
std::string gateStaleMarkKey(ui::StageId stage);

/**
 * @brief 建议标题文案键（NextStepAdvice.titleKey 的构造点）。
 *
 * 键形："advice.<actionKey>.title"（actionKey 为 §6.3 八 token——见
 * kAction* 常量）；值归 ui 文案资源。
 *
 * @param actionKey [in] 建议动作 token（词表内值；词表外 token 返回空串
 *        而不抛——建议产出面对未知 token 应显性暴露空键而非伪造文案键，
 *        词表封闭性由测试钉住）
 * @return 标题文案键（空串＝token 越界）
 */
ui::TextKey adviceTitleKey(const std::string& actionKey);

}  // namespace workflow
}  // namespace ird
}  // namespace sdurws

#endif  // SDURWS_IRD_WORKFLOW_TYPES_HPP
