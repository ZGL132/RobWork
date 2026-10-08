/**
 * @file   Projection.cpp
 * @brief  标题栏/状态栏与恢复横幅的状态投影实现（WP-22-T09）——场景键
 *         映射、标题文本组装、结果状态选键、投影值组装与 O9 服务端口
 *         组合（units/workflow.md §7.6/§10.2）。
 *
 * 设计依据：
 *   - units/workflow.md §7.6（PM-11 标题格式 `<显示名>[*][（只读）]`＋
 *     状态栏四字段；PM-15 恢复横幅三场景一句话汇总＋三动作）、§10.2
 *     （IStatusProjectionProvider Draft 签名逐字兑现）、§10.3（纯函数面
 *     确定性 NFR-COR-02；调用方错误 fail-fast——WorkflowError）
 *   - REQUIREMENTS.md §17 PM-11/PM-15 原文（格式串与三场景词形的唯一
 *     需求锚）、AT-21（残留草稿恢复/中断任务提示的观测点）
 *   - ui.md §9.1（恢复横幅装配面 assembleRecoveryBanner——三场景择一
 *     序与键串的同构对端；本文件键常量与其同串同值，SA-12 两卡同步）
 *
 * 背景说明（实现要点）：本翻译单元是**纯编排核**——全部函数对来源事实
 * 零加工（键半区填充＋格式拼装＋择一序），无任何对端服务依赖（取数经
 * Projection.hpp 两端口接缝）；唯一语义决策是三场景择一序（行动紧迫度：
 * 忽略保存＞中断任务＞未保存草稿——与 ui 恢复横幅装配面同序，同一句
 * PM-15 的两面对齐）与"放弃"可用位的写操作推导（ui §8.3-4 口径）。
 */

#include <sdurws/ird/workflow/Projection.hpp>

#include <sdurws/ird/workflow/Types.hpp>  // WorkflowError——调用方契约违约 fail-fast（单元错误类型唯一权威）

namespace sdurws {
namespace ird {
namespace workflow {

// =====================================================================
// 场景 → 一句话汇总键（唯一映射点——NFR-MNT-03）
// =====================================================================

std::string recoverySummaryKey(RecoveryScenario scenario)
{
    // 三值封闭词表的逐值映射（键串与 ui UiText 恢复横幅键族同串同值——
    // SA-12 单一权威在 ui 文案表，本处是 workflow 侧登记位）。
    switch (scenario) {
    case RecoveryScenario::IgnoredUnfinishedSave:
        return kBannerSummaryIgnoredSaveKey;
    case RecoveryScenario::InterruptedTask:
        return kBannerSummaryInterruptedKey;
    case RecoveryScenario::OrphanDraft:
        return kBannerSummaryOrphanDraftKey;
    }
    // 词表外值防御性返回空串（不伪造键——与 adviceTitleKey 未知 token
    // 同款口径；enum 词表封闭性由测试钉住，跨单元边界仍防御）。
    return {};
}

// =====================================================================
// 标题文本组装（PM-11 冻结格式唯一组装点）
// =====================================================================

std::string buildTitleText(const TitleStatusData& data,
                           const std::string& readonlySuffixText)
{
    // 无项目会话早退：空显示名＝标题无主体可渲染（宿主面显示产品名/
    // 首页——不在本投影语义内），返回空串不虚构占位标题。
    if (data.displayName.empty()) {
        return {};
    }

    // PM-11 冻结格式逐位拼装：<显示名> ＋ [*] ＋ [（只读）]。
    // `*` 位＝有未应用修改（anyDirty 事实，ui/project 权威）；
    // 后缀位＝只读会话（writable 事实取反，INV-SES-1 唯一判定源）。
    std::string title = data.displayName;
    if (data.dirty) {
        title += '*';
    }
    if (data.readOnly) {
        title += readonlySuffixText;  // 已解析文本（kTitleReadonlySuffixKey → ui 文案表）
    }
    return title;
}

// =====================================================================
// 结果状态选键（P-UI-2 冻结口径——不可判定归入过期呈现）
// =====================================================================

std::string resultsStatusLabelKey(const ui::CurrentnessProjection& projection)
{
    // status==nullopt（不可判定）与 Superseded 同键：evidence"无默认
    // Current"纪律的呈现面兑现——判不了的结果绝不显示为"当前"（P-UI-2：
    // 不可判定归入 results-stale 呈现，不显示为通过）。
    if (projection.status.has_value()
        && *projection.status == ui::CurrentnessProjection::Status::Current) {
        return kResultsStatusLabelCurrentKey;
    }
    return kResultsStatusLabelStaleKey;
}

// =====================================================================
// 标题状态组装（事实透传＋键半区填充）
// =====================================================================

TitleStatusData buildTitleStatus(const TitleFacts& facts)
{
    TitleStatusData data;

    // ---- 事实半区：五路来源逐一透传（零加工——权威在对端，PA-1）。
    data.displayName = facts.displayName;
    data.readOnly = !facts.writable;  // 只读位＝写权限事实取反（PM-07 降级/显式只读两形态同位表达）
    data.schemeLabel = facts.schemeLabel;
    data.dirty = facts.anyDirty;
    data.resultsCurrentness = facts.resultsCurrentness;

    // ---- 键半区：常量键填充＋结果状态按投影选键（键值半区分工——值归
    // ui 文案表，本单元只产键）。
    data.readonlySuffixKey = kTitleReadonlySuffixKey;
    data.schemeLabelKey = kStatusBarSchemeLabelKey;
    data.resultsStatusLabelKey = resultsStatusLabelKey(facts.resultsCurrentness);

    return data;
}

// =====================================================================
// 恢复横幅组装（三场景择一＋动作可用位推导）
// =====================================================================

namespace {

/// 场景的冻结紧迫度序值（序越小越优先——行动紧迫度：忽略保存＞中断
/// 任务＞未保存草稿；与 ui 恢复横幅装配面择一序同构对齐）。
int scenarioUrgency(RecoveryScenario scenario)
{
    switch (scenario) {
    case RecoveryScenario::IgnoredUnfinishedSave:
        return 0;
    case RecoveryScenario::InterruptedTask:
        return 1;
    case RecoveryScenario::OrphanDraft:
        return 2;
    }
    return 3;  // 词表外值殿后（防御性——封闭词表外不可构造，见枚举注）
}

/// 场景③命中查找（恢复/放弃动作可用位的判定输入——只有"检测到未保存
/// 草稿"场景携带这两个动作的语义对象）。
const RecoveryScenarioFact* findOrphanDraftFact(const RecoveryFacts& facts)
{
    for (const RecoveryScenarioFact& fact : facts.scenarios) {
        if (fact.scenario == RecoveryScenario::OrphanDraft) {
            return &fact;
        }
    }
    return nullptr;
}

}  // namespace

std::optional<RecoveryBannerData> buildRecoveryBanner(const RecoveryFacts& facts)
{
    // ---- 第 1 步：前置校验（调用方拼装契约——fail-fast）。
    // 零计数场景入清单＝"一句话汇总说出不存在的事实"；重复场景＝折叠
    // 端口一次性输出契约被破坏——两者都是调用方错误（环境失败应折叠为
    // 空清单，不进本函数）。
    for (std::size_t i = 0; i < facts.scenarios.size(); ++i) {
        const RecoveryScenarioFact& fact = facts.scenarios[i];
        if (fact.itemCount == 0) {
            throw WorkflowError(
                "buildRecoveryBanner: 零计数场景入清单（scenario="
                + std::to_string(static_cast<unsigned>(fact.scenario))
                + "）——无事实场景不得入清单（fail-fast）");
        }
        for (std::size_t j = i + 1; j < facts.scenarios.size(); ++j) {
            if (facts.scenarios[j].scenario == fact.scenario) {
                throw WorkflowError(
                    "buildRecoveryBanner: 重复场景（scenario="
                    + std::to_string(static_cast<unsigned>(fact.scenario))
                    + "）——每场景至多一条折叠事实（fail-fast）");
            }
        }
    }

    // ---- 第 2 步：空集早退——无恢复事实＝横幅不渲染（PM-15 横幅只在
    // 有事实时呈现；nullopt 是"不渲染"的编排语义，不是错误）。
    if (facts.scenarios.empty()) {
        return std::nullopt;
    }

    // ---- 第 3 步：择一主文案——按冻结紧迫度序取首个命中场景（清单
    // 次序无关：线性扫描全部场景取序值最小者，同序确定性由封闭词表
    // 保证——NFR-COR-02）。
    const RecoveryScenarioFact* primary = &facts.scenarios.front();
    for (const RecoveryScenarioFact& fact : facts.scenarios) {
        if (scenarioUrgency(fact.scenario) < scenarioUrgency(primary->scenario)) {
            primary = &fact;
        }
    }

    RecoveryBannerData banner;
    banner.scenario = primary->scenario;
    banner.summaryKey = recoverySummaryKey(primary->scenario);
    banner.itemCount = primary->itemCount;

    // 场景③主文案携带计数位置参数（键形"检测到 {0} 份未保存草稿"——
    // {0}＝计数文本；UX-02：计数是数值参数不是文案，经参数通道呈现）。
    if (primary->scenario == RecoveryScenario::OrphanDraft) {
        banner.summaryArgs.push_back(std::to_string(primary->itemCount));
    }

    // ---- 第 4 步：其余命中场景降级详情行（同紧迫度序追加——横幅只
    // 一句话主文案，其余事实以详情行互补呈现，不堆叠；确定性：固定序）。
    for (int urgency = 0; urgency <= 2; ++urgency) {
        for (const RecoveryScenarioFact& fact : facts.scenarios) {
            if (&fact != primary && scenarioUrgency(fact.scenario) == urgency) {
                banner.detailKeys.push_back(recoverySummaryKey(fact.scenario));
            }
        }
    }

    // ---- 第 5 步：三动作键恒填充（位形稳定——动作不隐藏只禁用）＋
    // 可用位推导。可用位语义（ui §8.3-4 口径）：恢复草稿＝载入查看，
    // 不是写操作——只读会话不禁（restoreAvailable 只看场景③命中）；
    // 放弃＝显式 discard＝写操作——只读会话禁用（discardAvailable＝
    // 场景③命中 ∧ 可写；写入口的权威拒绝仍归 project S1 门卫，PA-1
    // 不越权——本位只是入口禁用数据）。
    const RecoveryScenarioFact* orphan = findOrphanDraftFact(facts);
    banner.actionDetailsKey = kBannerActionDetailsKey;
    banner.actionRestoreKey = kBannerActionRestoreKey;
    banner.actionDiscardKey = kBannerActionDiscardKey;
    banner.restoreAvailable = orphan != nullptr;
    banner.discardAvailable = orphan != nullptr && facts.writable;

    return banner;
}

// =====================================================================
// O9 服务（端口组合实现——Draft 接口逐字兑现）
// =====================================================================

StatusProjectionProvider::StatusProjectionProvider(
    const ITitleFactPort& titlePort, IRecoveryFactPort& recoveryPort) noexcept
    : m_titlePort(&titlePort), m_recoveryPort(&recoveryPort)
{
}

TitleStatusData StatusProjectionProvider::titleStatus() const
{
    // 现取现组装（无缓存——事实新鲜度由端口快照保证；titleStatus 的
    // const 并发安全由 title 端口的 const collectFacts 承诺传导）。
    return buildTitleStatus(m_titlePort->collectFacts());
}

std::optional<RecoveryBannerData> StatusProjectionProvider::recoveryBanner() const
{
    // 现取现组装；nullopt（无恢复事实）直通——"不渲染"语义不在此折叠，
    // 由编排核统一表达（单点语义）。
    return buildRecoveryBanner(m_recoveryPort->collectFacts());
}

}  // namespace workflow
}  // namespace ird
}  // namespace sdurws
