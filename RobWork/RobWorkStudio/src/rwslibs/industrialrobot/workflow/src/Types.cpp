/**
 * @file   Types.cpp
 * @brief  workflow 词表与映射的实现——域↔阶段映射表（§4.2）与文案键唯一
 *         构造点（UX-02 键半区）。
 *
 * 设计依据：
 *   - units/workflow.md §4.2（阶段—域映射表——多对一映射与聚合阶段）、
 *     §6.2（stage→goalKey 静态词表）、§6.3（actionKey 词表）、ui.md §3.5
 *     （文案键体系形态——stage.<id>.title 族）
 *   - 需求 UX-02（工程用语：界面只见键与局部名，零哈希/Schema/内部插件名）
 *   - 任务契约 tasks/foundation/WP-22-T03.json acceptance 2（八规则
 *     actionKey 词表）/3（词表零新增——本文件零新增枚举，阶段词表直用
 *     ui::StageId，域键为 §4.2 表逐字）
 *
 * 确定性：全部函数为查表/拼串纯函数（同入同出——NFR-COR-02）；映射表为
 * 编译期常量数组（并发只读安全）。
 */

#include <sdurws/ird/workflow/Types.hpp>

#include <sdurws/ird/ui/IStageNavigationModel.hpp>  // ui::stageToken——阶段 token 唯一映射点（NFR-MNT-03：禁止第二处拼写 token）

#include <array>

namespace sdurws {
namespace ird {
namespace workflow {

// =====================================================================
// 域↔阶段映射（§4.2 表的机器承载——词表逐字，零增删）
// =====================================================================

namespace {

/// 映射条目：域注册键 → 所属阶段（§4.2 表行；trajectory 与 dynamics 同属
/// 轨迹/动力学聚合阶段——"两域均就绪才解锁"的聚合判定行）。
struct DomainStageEntry {
    const char* domainKey;  ///< 域注册键（§4.2 小写形态——域插件注册词表）
    ui::StageId stage;      ///< 所属阶段（ui 七值词表——零新增）
};

/// §4.2 表的完整映射（8 键——七域＋聚合阶段拆分的 dynamics；表序即
/// StageId 序，查表线性扫描即可：词表封闭且仅 8 条，无性能面）。
constexpr std::array<DomainStageEntry, 8> kDomainStageTable{{
    {"modeling",      ui::StageId::Modeling},           // 建模域 → 建模阶段
    {"requirements",  ui::StageId::Requirements},       // 需求域 → 需求阶段
    {"kinematics",    ui::StageId::Kinematics},         // 运动学域 → 运动学阶段
    {"trajectory",    ui::StageId::TrajectoryDynamics}, // 轨迹域 → 轨迹/动力学聚合阶段
    {"dynamics",      ui::StageId::TrajectoryDynamics}, // 动力学域 → 轨迹/动力学聚合阶段（多对一）
    {"selection",     ui::StageId::Selection},          // 选型域 → 选型阶段
    {"optimization",  ui::StageId::Optimization},       // 优化域 → 优化阶段
    {"reporting",     ui::StageId::Reporting},          // 报告域 → 报告阶段
}};

}  // namespace

ui::StageId stageForDomain(const std::string& domainKey)
{
    // 线性查表（词表封闭 8 条——确定性顺序扫描，首个命中即返回）。
    for (const auto& entry : kDomainStageTable) {
        if (domainKey == entry.domainKey) { return entry.stage; }
    }
    // 词表外域名：调用方/对端契约违约（evidence 给出的受影响域必须在
    // §4.2 词表内）——fail-fast，不静默忽略（丢失失效提示＝UX-12 违约）。
    throw WorkflowError("stageForDomain: 词表外域名 '" + domainKey + "'");
}

const std::vector<std::string>& stageDomains(ui::StageId stage)
{
    // 静态词表：每阶段的聚合域键清单（§4.2 表"聚合域投影"列逐行；
    // TrajectoryDynamics 两域——聚合判定输入形状）。函数局部 static（首次
    // 调用初始化，其后并发只读——C++11 魔术静态保证线程安全）。
    static const std::array<std::vector<std::string>, 7> kTable{{
        {"modeling"},                              // Modeling（§4.2 行 1）
        {"requirements"},                          // Requirements（行 2）
        {"kinematics"},                            // Kinematics（行 3）
        {"trajectory", "dynamics"},                // TrajectoryDynamics（行 4——两域聚合）
        {"selection"},                             // Selection（行 5）
        {"optimization"},                          // Optimization（行 6）
        {"reporting"},                             // Reporting（行 7）
    }};
    // 阶段转下标：StageId 枚举序＝§6.4 UX-12 冻结序（ui UiTypes.hpp 词表
    // 注释）；越界值防御性拒绝（七值封闭词表外不可构造，但跨单元边界仍校验）。
    const auto idx = static_cast<std::size_t>(stage);
    if (idx >= kTable.size()) {
        throw WorkflowError("stageDomains: 阶段值越界");
    }
    return kTable[idx];
}

// =====================================================================
// 文案键构造（唯一映射点——全部经 ui::stageToken 取 token，零手拼 token）
// =====================================================================

std::string stageGoalKey(ui::StageId stage)
{
    // 键形 "stage.<token>.goal"：UX-01 要素①"目标"（§6.2 stage→goalKey
    // 静态词表）；值归 ui 文案资源，本函数只产键。
    return std::string("stage.") + ui::stageToken(stage) + ".goal";
}

std::string gateBlockedReasonKey(ui::StageId stage)
{
    // 键形 "stage.<token>.gate.blocked"：Blocked 行"附原因"的原因基键
    // （ui §6.4 表）；缺项明细走 missingItemKeys 透传，不并入本键。
    return std::string("stage.") + ui::stageToken(stage) + ".gate.blocked";
}

std::string gateUnlockHintKey(ui::StageId stage)
{
    // 键形 "stage.<token>.gate.unlock"：§6.4 时序"拒绝(原因/解锁条件)"
    // 的解锁半区；R8 只读提示的 detailKeys 亦复用本键族。
    return std::string("stage.") + ui::stageToken(stage) + ".gate.unlock";
}

std::string gateNotReachedReasonKey(ui::StageId stage)
{
    // 键形 "stage.<token>.gate.not-reached"：token 为**前序**阶段——下游
    // NotStarted 的根因指位（§4.4：前序未过 → 下游锁定，提示用户去解锁
    // 前序而不是在下游找缺项）。
    return std::string("stage.") + ui::stageToken(stage) + ".gate.not-reached";
}

std::string gateDomainUnavailableKey(ui::StageId stage)
{
    // 键形 "stage.<token>.gate.domain-unavailable"：Unavailable 行基键
    // （域未装配区别于缺项 Blocked——WF-VER-103 观测点）。
    return std::string("stage.") + ui::stageToken(stage) + ".gate.domain-unavailable";
}

std::string missingDomainReasonKey(ui::StageId stage, const std::string& domainKey)
{
    // 键形 "stage.<token>.gate.missing-domain.<domainKey>"：逐域缺失原因；
    // domainKey 为 §4.2 域注册词表键（语义键，非哈希/内部标识——UX-02
    // 红线不受影响：词表键在诊断与投影界面本就可见）。
    return std::string("stage.") + ui::stageToken(stage) + ".gate.missing-domain."
        + domainKey;
}

std::string gateStaleMarkKey(ui::StageId stage)
{
    // 键形 "stage.<token>.gate.results-stale"：级联失效"Completed＋
    // results-stale 并存呈现"的标记键（§4.3）；逐条失效原因是结构化数据
    // （evidence::InvalidationReason 经 StaleHint 携带），不经本键承载。
    return std::string("stage.") + ui::stageToken(stage) + ".gate.results-stale";
}

ui::TextKey adviceTitleKey(const std::string& actionKey)
{
    // 键形 "advice.<actionKey>.title"（§6.3 actionKey 词表 → 文案键）。
    // 词表外 token 返回空串而非伪造键：建议产出面对未知 token 应显性暴露
    // （呈现层空键即空文案），词表封闭性由测试钉住（WF-VER-111 键表扫描）。
    static const std::array<const char*, 8> kKnown{{
        kActionFixInputs, kActionRunEvaluation, kActionReviewActiveTask,
        kActionRecomputeDownstream, kActionResolveFindings, kActionAdvanceStage,
        kActionRecoverSession, kActionReadonlyNotice,
    }};
    for (const char* known : kKnown) {
        if (actionKey == known) {
            return "advice." + actionKey + ".title";
        }
    }
    return {};
}

}  // namespace workflow
}  // namespace ird
}  // namespace sdurws
