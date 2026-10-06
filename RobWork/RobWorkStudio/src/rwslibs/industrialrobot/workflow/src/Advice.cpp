/**
 * @file   Advice.cpp
 * @brief  下一步建议引擎的纯函数实现——UX-01 四要素组装与八规则判定
 *         （§6.2 映射表＋§6.3 规则词表逐条承载）。
 *
 * 设计依据：
 *   - units/workflow.md §6.1（建议是导航性建议，不是工程判定——零领域
 *     阈值零工程判定）、§6.2（UX-01 四要素映射表）、§6.3（八规则触发
 *     条件/产出/优先级 R7>R5>R1>R4>R3>R2>R8>R6；同优先级稳定序；建议
 *     可跳过不自动执行）、§6.4（数据流——消费门控产出/快照/任务状态/
 *     恢复横幅，惰性重算）、§11.1（WF-VER-108/109/111 观测点）
 *   - 需求 UX-01（目标/必需输入/当前问题/下一步操作四要素齐备）、UX-02
 *     （工程用语——全部产出为文案键）、UX-07/UX-13（建议不旁路命令与
 *     确认流——可跳过、非门禁）
 *   - 任务契约 tasks/foundation/WP-22-T03.json acceptance 2（八规则优先级
 *     与可跳过；UX-01 四要素；纯函数面同输入同输出）
 *
 * 实现纪律：本翻译单元只 include workflow/ui 公共头（零业务域——R-1）；
 * 无浮点字面量、无数值阈值（I-WF-3）；无动作执行面（无函数指针/std::function
 * 成员——D-WF-8，契约测试扫描钉住）。
 */

#include <sdurws/ird/workflow/Advice.hpp>

#include <algorithm>  // std::find_if——problemKeys 去重保序
#include <utility>    // std::move

namespace sdurws {
namespace ird {
namespace workflow {

namespace {

// =====================================================================
// 内部辅助（全部为纯函数——规则装配的确定性构件）
// =====================================================================

/// 建议条目快速装配（各规则共用的字段模板——titleKey 经唯一构造点取得，
/// 词表外 token 返回空键由测试显性暴露，见 Types.cpp adviceTitleKey）。
NextStepAdvice makeAdvice(const char* actionKey,
                          ui::StageId targetStage,
                          std::string targetObjectId,
                          std::vector<ui::TextKey> detailKeys)
{
    NextStepAdvice advice;
    advice.actionKey = actionKey;
    advice.targetStage = targetStage;
    advice.targetObjectId = std::move(targetObjectId);
    advice.titleKey = adviceTitleKey(actionKey);  // 键构造唯一映射点（UX-02）
    advice.detailKeys = std::move(detailKeys);
    return advice;
}

/// 去重保序追加（problemKeys 的组装规则：多来源键合并——门控原因/过期
/// 标记/阻塞诊断——重复键只留首现位置；顺序即来源优先序，确定性）。
void appendUnique(std::vector<ui::TextKey>& target, const std::vector<ui::TextKey>& source)
{
    for (const ui::TextKey& key : source) {
        const bool seen = std::any_of(target.begin(), target.end(),
                                      [&](const ui::TextKey& k) { return k == key; });
        if (!seen) {
            target.push_back(key);
        }
    }
}

}  // namespace

// =====================================================================
// adviseFor——八规则判定与四要素组装（§6.3 逐条承载，无隐藏规则）
// =====================================================================

StageAdvice PureNextStepAdvisor::adviseFor(ui::StageId stage,
                                           const StageGatingState& gates,
                                           const ui::StageReadinessSnapshot& snapshot,
                                           const AdviceInputs& inputs) const
{
    // ---- 前置：门控板序位校验（调用方错误 fail-fast）——建议消费门控
    // 产出（D-WF-1 四段职责链），板必须为七阶段完整判定。
    const auto idx = static_cast<std::size_t>(stage);
    if (idx >= gates.decisions.size()) {
        throw WorkflowError("adviseFor: 阶段值越界或门控板不完整");
    }
    const GateDecision& decision = gates.decisions[idx];

    // ---- 要素①目标（§6.2：stage→goalKey 静态词表——Types.hpp 构造点）。
    StageAdvice advice;
    advice.goalKey = stageGoalKey(stage);

    // ---- 要素②必需输入（缺项键透传——I-WF-2 不重算不增删；Blocked 时
    // 非空，其余态为空透传）。
    advice.missingItemKeys = decision.missingItemKeys;

    // ---- 要素③当前问题（§6.2：门控 reasonKeys＋失效提示 staleHints＋
    // 诊断目录阻塞条目三来源合并）。results-stale 是标记键（逐条失效原因
    // 是结构化数据走 StaleHint——gateStaleMarkKey 注释），阻塞诊断键为
    // 调用方透传事实（diagnostics 词表），均去重保序合并。
    appendUnique(advice.problemKeys, decision.reasonKeys);
    if (!decision.staleHints.empty()) {
        advice.problemKeys.push_back(gateStaleMarkKey(stage));
    }
    appendUnique(advice.problemKeys, inputs.blockingDiagnosticKeys);

    // ---- 要素④下一步操作（八规则按优先级装配；命中即追加——产出**全部
    // 命中规则**的有序清单，首条即"下一步"；同优先级多条按产出序＝触发
    // 事实的稳定序——R4 按 staleHints 的 (阶段,域键) 序，见 Gate.cpp）。
    std::vector<NextStepAdvice>& steps = advice.steps;

    // R7 处理恢复项（最高优先——恢复与阻塞优先于前进，§6.3 优先级注）。
    // 触发＝存在恢复横幅场景（未保存草稿/中断任务/未完成保存已忽略）；
    // 详情键＝场景键透传（横幅动作对齐——§6.3 R7 行）。
    if (!inputs.recoveryBannerKeys.empty()) {
        steps.push_back(makeAdvice(kActionRecoverSession, stage, {},
                                   inputs.recoveryBannerKeys));
    }

    // R5 处理阻塞诊断。触发＝当前阶段存在阻塞级诊断（Preflight/就绪校验）；
    // 详情键＝诊断键透传（诊断目录定位——§6.3 R5 行）。
    if (!inputs.blockingDiagnosticKeys.empty()) {
        steps.push_back(makeAdvice(kActionResolveFindings, stage, {},
                                   inputs.blockingDiagnosticKeys));
    }

    // R1 补全输入。触发＝当前阶段 Blocked 且缺项非空（§6.3 触发条件原文）；
    // 详情键＝缺项清单（附缺项分组与对象定位——分组语义＝快照域序拼接，
    // 见 Gate.cpp 规则 3）。
    if (decision.status == ui::StageViewStatus::Blocked
        && !decision.missingItemKeys.empty()) {
        steps.push_back(makeAdvice(kActionFixInputs, stage, {},
                                   decision.missingItemKeys));
    }

    // R4 复算下游。触发＝级联失效（results-stale——门控 staleHints 非空，
    // 由 evaluate 事件重放或 mapInvalidation 通道产出）；每条提示一条建议
    // （对象定位＝受影响域键——呈现局部名经⑥名称端口，本单元携身份键）；
    // 多条顺序＝staleHints 稳定序（同优先级按对象键序——§6.3 确定性注）。
    for (const StaleHint& hint : decision.staleHints) {
        steps.push_back(makeAdvice(kActionRecomputeDownstream, hint.stage,
                                   hint.domainKey, {gateStaleMarkKey(stage)}));
    }

    // R3 等待/查看任务。触发＝任一域 hasActiveTask（投影事实——不以门控
    // InProgress 状态为准：hasActiveTask 是事实，状态是其投影之一）；
    // 任务清单入口数据由呈现层装配（九态短标签——§6.3 R3 行）。
    const bool hasActiveTask = std::any_of(snapshot.domains.begin(),
                                           snapshot.domains.end(),
                                           [](const ui::DomainReadinessItem& item) {
                                               return item.hasActiveTask;
                                           });
    if (hasActiveTask) {
        steps.push_back(makeAdvice(kActionReviewActiveTask, stage, {}, {}));
    }

    // R2 发起计算。触发＝输入齐备（任一域缺项则由 R1 覆盖）＋无活动任务
    // ＋结果缺失（"结果缺失"事实＝无归档结果——调用方自事件记忆取数，
    // AdviceInputs.hasArchivedResult；本单元不重算结果存在性——I-WF-2）。
    const bool inputsComplete = std::all_of(snapshot.domains.begin(),
                                            snapshot.domains.end(),
                                            [](const ui::DomainReadinessItem& item) {
                                                return item.inputComplete;
                                            });
    const bool noMissingItems = decision.missingItemKeys.empty();
    if (inputsComplete && noMissingItems && !hasActiveTask
        && !inputs.hasArchivedResult) {
        steps.push_back(makeAdvice(kActionRunEvaluation, stage, {}, {}));
    }

    // R8 只读提示。触发＝只读会话（writable==false——会话事实经
    // AdviceInputs 传入，唯一数据源为会话 writable 位）；详情键＝解锁条件
    // 提示（门控 unlockHintKey——§6.3 R8 行"解锁条件提示 unlockHintKey"）。
    if (inputs.sessionReadOnly) {
        std::vector<ui::TextKey> readonlyDetails;
        if (decision.unlockHintKey.has_value()) {
            readonlyDetails.push_back(*decision.unlockHintKey);
        }
        steps.push_back(makeAdvice(kActionReadonlyNotice, stage, {},
                                   std::move(readonlyDetails)));
    }

    // R6 前进下一阶段（最低优先——前进类建议殿后）。触发＝当前阶段
    // Completed 且下一阶段 NotStarted（§6.3 触发条件原文；Reporting 为
    // 末阶段无下一阶段——不触发）。注意下一阶段 NotStarted 可能是"前序
    // 未过"锁定也可能是"评估未开始"可进入——按 §6.3 字面不区分（可跳过
    // 建议：误指向前向入口无害，用户经导航自由操作——D-WF-8）。
    if (decision.status == ui::StageViewStatus::Completed
        && idx + 1U < gates.decisions.size()
        && gates.decisions[idx + 1U].status == ui::StageViewStatus::NotStarted) {
        steps.push_back(makeAdvice(kActionAdvanceStage,
                                   static_cast<ui::StageId>(idx + 1U), {}, {}));
    }

    // ---- 产出即返回（惰性求值面——调用方在需要时调用，事件不自动触发
    // 本函数；可跳过：steps 为空的阶段合法且不阻断任何导航，D-WF-8）。
    return advice;
}

}  // namespace workflow
}  // namespace ird
}  // namespace sdurws
