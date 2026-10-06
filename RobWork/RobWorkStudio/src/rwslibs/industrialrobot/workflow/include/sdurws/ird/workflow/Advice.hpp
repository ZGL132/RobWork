/**
 * @file   Advice.hpp
 * @brief  下一步建议引擎（O3）——UX-01 四要素承载 StageAdvice、建议八规则
 *         产出 NextStepAdvice 与建议器接口（§6.1~§6.4）。
 *
 * 设计依据：
 *   - units/workflow.md §6（下一步建议引擎——§6.1 定位与边界、§6.2 UX-01
 *     四要素映射、§6.3 建议规则词表与优先级、§6.4 建议引擎数据流）、
 *     §10.2（接口签名设计基线 INextStepAdvisor——输入形状的实现口径增补
 *     已登记单元卡 §14.5 v0.4）、§11.1（WF-VER-108/109/111）
 *   - 需求 UX-01（首次进入阶段时显示目标、必需输入、当前问题和下一步
 *     操作——四要素齐备）、UX-02（工程用语——产出全部为文案键）、UX-07
 *     （建议不旁路确认流——D-WF-8 可跳过、不自动执行）
 *   - 任务契约 tasks/foundation/WP-22-T03.json acceptance 2（建议八规则
 *     优先级与可跳过；UX-01 四要素；纯函数面同输入同输出）
 *
 * 背景说明（建议是导航提示，不是工程判定——§6.1）：建议引擎回答"当前
 * 阶段该做什么、缺什么、去哪修"，不回答"结果是否合格"——规则只消费门控
 * 状态、缺项清单、任务状态与投影事实，零领域阈值、零工程判定（I-WF-3
 * 同源纪律）。建议**可跳过**（D-WF-8）：advice 是提示不是门禁，用户可
 * 忽略并经命令面板/导航自由操作；点击建议只做导航/打开入口，动作执行
 * 仍经命令与用户确认——建议引擎不自动执行任何动作。
 *
 * 确定性：adviseFor 为纯函数面（同输入同输出——D-WF-3）；同优先级多条
 * 建议按（阶段序, 对象键字典序）稳定输出（§6.3 确定性规则）。
 *
 * 线程安全：全部类型为纯值；PureNextStepAdvisor 无成员状态，const 方法
 * 可并发调用。
 */

#ifndef SDURWS_IRD_WORKFLOW_ADVICE_HPP
#define SDURWS_IRD_WORKFLOW_ADVICE_HPP

#include <string>
#include <vector>

#include <sdurws/ird/ui/UiTypes.hpp>     // ui::StageId/StageReadinessSnapshot/TextKey（P-WF-2 谈判起点形状）
#include <sdurws/ird/workflow/Gate.hpp>  // StageGatingState（建议消费门控产出——§6.4 数据流第一输入）
#include <sdurws/ird/workflow/Types.hpp> // actionKey 词表/文案键构造/WorkflowError

namespace sdurws {
namespace ird {
namespace workflow {

// =====================================================================
// 建议产出值类型（UX-01 四要素的承载）
// =====================================================================

/**
 * @brief 单条下一步建议（§6.2"下一步操作"要素的产出形状——§10.2 基线
 *        NextStepAdvice{actionKey, targetStage, targetObjectId?, titleKey,
 *        detailKeys[]} 逐字段兑现）。
 *
 * 红线（D-WF-8）：本结构是导航提示——消费者点击只应做导航/打开对应入口，
 * 动作执行仍经命令端口与用户确认；本结构不含可执行回调句柄（零函数指针/
 * std::function——建议引擎不旁路命令流）。
 */
struct NextStepAdvice {
    /// 建议动作 token（§6.3 八规则词表——kAction* 常量；机器判读与测试
    /// 断言面；文案经 titleKey 解析）。
    std::string actionKey;
    /// 建议目标阶段（R2＝当前阶段、R4＝受影响阶段、R6＝下一阶段、其余
    /// ＝当前阶段；ui 七值词表）。
    ui::StageId targetStage = ui::StageId::Modeling;
    /// 目标对象定位（可空串＝无对象级定位；携带对象身份键——呈现局部名
    /// 经⑥名称端口解析，UX-02；本单元不解析名称——R-4 名称语义归 runtime）。
    std::string targetObjectId;
    /// 建议标题文案键（adviceTitleKey(actionKey)——UX-01 呈现主文案）。
    ui::TextKey titleKey;
    /// 详情文案键清单（缺项键/解锁条件键/诊断键的透传——零加工；UX-02）。
    std::vector<ui::TextKey> detailKeys;

    /// 值相等（确定性重放 WF-VER-107/108 的比较面）。
    bool operator==(const NextStepAdvice& o) const
    {
        return actionKey == o.actionKey && targetStage == o.targetStage
            && targetObjectId == o.targetObjectId && titleKey == o.titleKey
            && detailKeys == o.detailKeys;
    }
    bool operator!=(const NextStepAdvice& o) const { return !(*this == o); }
};

/**
 * @brief 单阶段建议（UX-01 四要素齐备的完整产出——§6.2 映射表逐行）。
 *
 * 四要素与数据来源（§6.2 表——全部来自投影/门控/会话态事实，零领域计算）：
 *   ①目标＝stageGoalKey(stage)（静态词表键）；②必需输入＝缺项键透传＋
 *   分组（分组语义＝快照域序拼接，I-WF-2 不增删）；③当前问题＝门控
 *   reasonKeys＋results-stale 标记键＋阻塞级诊断键（去重保序）；④下一步
 *   操作＝steps（按 §6.3 优先级排序，首条即"下一步"）。
 */
struct StageAdvice {
    /// 要素①目标文案键（stage.<token>.goal——Types.hpp 构造点）。
    ui::TextKey goalKey;
    /// 要素②必需输入（缺项文案键清单——GateDecision.missingItemKeys
    /// 透传，**不重算不增删**；域未缺项时为空）。
    std::vector<ui::TextKey> missingItemKeys;
    /// 要素③当前问题（门控原因键＋过期标记键＋阻塞诊断键——去重保序；
    /// 无问题时为空）。
    std::vector<ui::TextKey> problemKeys;
    /// 要素④下一步操作清单（按 §6.3 优先级 R7>R5>R1>R4>R3>R2>R8>R6 排序，
    /// 同优先级按（阶段序,对象键字典序）稳定；无命中规则时为空——建议
    /// 非门禁，空清单合法且不阻断导航，D-WF-8）。
    std::vector<NextStepAdvice> steps;

    /// 值相等（全字段）。
    bool operator==(const StageAdvice& o) const
    {
        return goalKey == o.goalKey && missingItemKeys == o.missingItemKeys
            && problemKeys == o.problemKeys && steps == o.steps;
    }
    bool operator!=(const StageAdvice& o) const { return !(*this == o); }
};

/**
 * @brief 建议会话态附加事实（adviseFor 的第四输入——§6.4 数据流图中
 *        "任务状态投影/恢复横幅状态"两路的实现承载）。
 *
 * 为什么需要本结构：§6.3 八规则中 R7（恢复横幅场景）与 R5（阻塞级诊断）
 * 的事实源在会话设施（恢复横幅状态 §7.6、诊断目录 §9 diagnostics 行），
 * R2 的"结果缺失"事实在事件记忆（ResultArchived 窗口），R8 的只读事实在
 * 会话 writable 位——四者都不在 (gates, snapshot) 形状内（ui §6.5 快照
 * 只携带域就绪投影）。调用方（L5 适配层）从各自权威源取数组装本值对象；
 * 本单元对四事实零加工零推导（词表外不发明——§2.4 非所有权）。
 *
 * 空值语义：全部缺省＝"无恢复场景、无阻塞诊断、无归档结果、可写会话"
 * ——adviseFor 对缺省输入只可能命中 R1/R3/R4/R6（纯投影/门控事实规则），
 * 保证基线签名（§10.2 三参）行为可由缺省第四参复现。
 */
struct AdviceInputs {
    /// 恢复横幅场景键清单（§7.6 三场景：未完成保存已忽略/任务已中断/
    /// 检测到未保存草稿——R7 触发事实；非空即存在待处理恢复项）。
    std::vector<ui::TextKey> recoveryBannerKeys;
    /// 当前阶段阻塞级诊断键清单（诊断目录中 Preflight/就绪校验类条目的
    /// 文案键——R5 触发事实；键形遵 diagnostics 体系 diag.*）。
    std::vector<ui::TextKey> blockingDiagnosticKeys;
    /// 当前阶段是否已有归档结果（R2"结果缺失"的否定面——调用方自事件
    /// 记忆（ResultArchived 窗口）取数；true＝结果在，R2 不触发）。
    bool hasArchivedResult = false;
    /// 是否只读会话（writable==false——R8 触发事实；唯一数据源为会话
    /// writable 位，ui §5.5/INV-SES-1 同源，本单元不预判）。
    bool sessionReadOnly = false;
};

// =====================================================================
// 建议器接口（§10.2 设计基线 INextStepAdvisor）
// =====================================================================

/**
 * @brief 下一步建议引擎（O3——UX-01 四要素；导航性建议，非工程判定）。
 *
 * 实现纪律：只消费（门控状态, 快照, 会话态事实）三面输入——零领域阈值、
 * 零工程判定、零动作执行（I-WF-3/D-WF-8）；产出按 §6.3 优先级词表稳定
 * 排序；事件不自动触发建议重算——建议惰性求值（§5.3 步骤 5），由呈现层
 * 在需要时调用。
 */
class INextStepAdvisor {
public:
    virtual ~INextStepAdvisor() = default;

    /**
     * @brief 计算单阶段建议（§6.3 八规则——惰性调用，纯函数面）。
     *
     * 八规则触发条件（全部来自输入事实，§6.3 表原文）与优先级序：
     *   R7 recover-session（inputs.recoveryBannerKeys 非空）
     *   > R5 resolve-findings（inputs.blockingDiagnosticKeys 非空）
     *   > R1 fix-inputs（门控 Blocked 且缺项非空）
     *   > R4 recompute-downstream（门控 staleHints 非空——每提示一条）
     *   > R3 review-active-task（快照任一域 hasActiveTask）
     *   > R2 run-evaluation（输入齐备＋无活动任务＋无归档结果）
     *   > R8 readonly-notice（inputs.sessionReadOnly）
     *   > R6 advance-stage（门控 Completed 且下一阶段 NotStarted）
     *
     * @param stage [in] 目标阶段（ui 七值词表）
     * @param gates [in] 门控状态（evaluate 产出——建议消费门控结果，
     *              不自行重判门控——D-WF-1 四段职责链）
     * @param snapshot [in] 该阶段域就绪汇聚快照（hasActiveTask/inputComplete
     *                  等投影事实的取数面）
     * @param inputs [in] 会话态附加事实（缺省值＝基线三参语义，见结构注）
     * @return 四要素建议（steps 按优先级稳定序；可跳过——空 steps 合法）
     *
     * @throws WorkflowError stage 与 gates.decisions 序位越界（调用方错误
     *         fail-fast——gates 板必须为七阶段完整判定）
     *
     * @threadSafe const 只读，可并发。
     * @determinism 同输入同输出（NFR-COR-02 同型）。
     */
    virtual StageAdvice adviseFor(ui::StageId stage,
                                  const StageGatingState& gates,
                                  const ui::StageReadinessSnapshot& snapshot,
                                  const AdviceInputs& inputs) const = 0;
};

/**
 * @brief 建议器默认实现（纯函数面——无状态，D-WF-3；规则即 §6.3 表的
 *        逐条承载，无隐藏规则）。
 */
class PureNextStepAdvisor final : public INextStepAdvisor {
public:
    StageAdvice adviseFor(ui::StageId stage,
                          const StageGatingState& gates,
                          const ui::StageReadinessSnapshot& snapshot,
                          const AdviceInputs& inputs) const override;
};

}  // namespace workflow
}  // namespace ird
}  // namespace sdurws

#endif  // SDURWS_IRD_WORKFLOW_ADVICE_HPP
