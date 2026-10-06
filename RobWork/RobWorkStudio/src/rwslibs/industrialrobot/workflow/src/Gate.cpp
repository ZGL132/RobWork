/**
 * @file   Gate.cpp
 * @brief  七阶段门控的纯函数实现——evaluate（§4.4 状态机的投影＋事件重放
 *         形态）与 mapInvalidation（§4.3 级联失效映射）。
 *
 * 设计依据：
 *   - units/workflow.md §4.2（阶段就绪条件——全部来自投影，I-WF-2 不重算）、
 *     §4.3（门控状态词表与级联失效提示——提示数据不自动触发重算）、§4.4
 *     （门控状态机——全部转移可从"投影＋事件"重放推导）、§5.2（事件消费
 *     面——ResultArchived/DependencyInvalidated 的门控处置）、§5.3（刷新
 *     时序——epoch 对齐 I-WF-4）、§11.1（WF-VER-101~107 观测点）
 *   - 需求 UX-12（按就绪条件解锁/锁定；上游完成态或结果失效时级联提示
 *     下游需重算或失效）、UX-10（门控状态词与 ui 词表对齐）
 *   - 任务契约 tasks/foundation/WP-22-T03.json acceptance 1/2（级联提示
 *     用例；只消费快照＋事件、无领域阈值、纯函数面）
 *
 * 实现纪律（acceptance 2 运行断言锁定面，审查对照）：
 *   - 本翻译单元只 include ui/core/evidence/workflow 公共头——零业务域
 *     单元（R-1；契约测试扫描钉住）；
 *   - 零浮点字面量、零"多少算合格"数值比较——判定全部为枚举/布尔事实
 *     透传（I-WF-3 零领域阈值；契约测试扫描钉住）；
 *   - 无成员可变状态、无 I/O、无任务派发——纯函数面（D-WF-3/UX-12）。
 */

#include <sdurws/ird/workflow/Gate.hpp>

#include <sdurws/ird/ui/IStageNavigationModel.hpp>  // ui::stageIdSequence——七阶段冻结序（UX-12；NFR-MNT-03 唯一映射点）

#include <map>      // std::map——事件重放与提示收集的有序容器（迭代序＝键字典序，确定性）
#include <set>      // std::set——受影响阶段域键去重
#include <utility>  // std::pair/移动

namespace sdurws {
namespace ird {
namespace workflow {

// =====================================================================
// GateDecision 值相等（头文件声明的比较面——WF-VER-107 确定性重放用）
// =====================================================================

bool GateDecision::operator==(const GateDecision& o) const
{
    return stage == o.stage && status == o.status && unlocked == o.unlocked
        && reasonKeys == o.reasonKeys && unlockHintKey == o.unlockHintKey
        && missingItemKeys == o.missingItemKeys && staleHints == o.staleHints
        && epoch == o.epoch;
}

namespace {

// =====================================================================
// 内部辅助：阶段序位与事件重放
// =====================================================================

/// 阶段值 → 七板下标（StageId 枚举序＝stageIdSequence 序＝UX-12 冻结序；
/// 越界防御性拒绝——调用方错误 fail-fast）。
std::size_t stageIndex(ui::StageId stage)
{
    const auto idx = static_cast<std::size_t>(stage);
    if (idx >= 7U) {
        throw WorkflowError("stageIndex: 阶段值越界（七值封闭词表外）");
    }
    return idx;
}

/**
 * @brief 单阶段的"结果事实"（事件窗口重放的产物——§5.2 事件消费面）。
 *
 * hasResult＝本阶段聚合域出现过 ResultArchived（有归档结果）；
 * stale＝结果归档之后出现过覆盖本阶段的 DependencyInvalidated（已过期）。
 * 事件按发布序重放：新的归档结果覆盖旧的失效（stale 清零）——"失效后
 * 重算完成即恢复当前"的时序语义。
 */
struct StageResultFact {
    bool hasResult = false;               ///< 是否存在归档结果（本阶段聚合域）
    std::map<std::pair<std::size_t, std::string>, StaleHint>
        staleHints;                       ///< 过期提示（键＝(阶段序,域键)——有序去重，确定性输出序）
};

/**
 * @brief 重放事件窗口，推导七阶段的"结果事实"（纯函数——无副作用）。
 *
 * 只处理 ResultArchived/DependencyInvalidated 两类（其余类别对门控的可见
 * 效果已在汇聚快照中——hasActiveTask/inputComplete 由域自报更新，§5.2
 * 处置表）；域键经 stageForDomain 做域→阶段导航映射（§4.2——本单元拥有
 * 的映射规则，D-WF-2），词表外域键抛 WorkflowError（调用方/对端违约）。
 */
std::array<StageResultFact, 7> replayEvents(const std::vector<GateEvent>& events)
{
    std::array<StageResultFact, 7> facts{};  // 七阶段事实板（值初始化——无结果无失效）
    for (const GateEvent& event : events) {
        // 第一步：把事件关联域键映射为受影响阶段序位集合（同事件可跨阶段）。
        std::set<std::size_t> touchedStages;  // 有序——重放顺序与域键顺序无关化（确定性）
        for (const std::string& domainKey : event.domainKeys) {
            touchedStages.insert(stageIndex(stageForDomain(domainKey)));
        }
        // 第二步：按事件类别更新事实（重放序＝发布序——同阶段"归档↔失效"
        // 的先后决定最终 stale 位，§5.2 时序语义）。
        for (const std::size_t idx : touchedStages) {
            StageResultFact& fact = facts[idx];
            if (event.kind == core::DomainEventKind::ResultArchived) {
                // 归档：本阶段有结果；新结果覆盖旧失效（重算完成即恢复当前）。
                fact.hasResult = true;
                fact.staleHints.clear();
            } else if (event.kind == core::DomainEventKind::DependencyInvalidated) {
                // 失效：只有已有结果才谈得上"过期"（无结果阶段无须提示）。
                if (fact.hasResult) {
                    // 同阶段多域事件：逐域一条提示（定位粒度＝域——§4.3
                    // "需重算对象定位"）；键有序去重——同 (阶段,域) 的重复
                    // 失效只保留最新原因（覆盖插入）。
                    for (const std::string& domainKey : event.domainKeys) {
                        if (stageIndex(stageForDomain(domainKey)) != idx) { continue; }
                        StaleHint hint;
                        hint.stage = static_cast<ui::StageId>(idx);
                        hint.domainKey = domainKey;
                        hint.reasons = event.reasons;      // evidence 原因透传（零加工——D-WF-2）
                        hint.actionKey = kActionRecomputeDownstream;  // R4 建议动作（可跳过）
                        fact.staleHints[std::make_pair(idx, domainKey)] = std::move(hint);
                    }
                }
            }
            // 其余事件类别：忽略（§5.2 处置表——见函数头注释）。
        }
    }
    return facts;
}

}  // namespace

// =====================================================================
// evaluate——七阶段序贯判定（§4.4 状态机的纯函数重放形态）
// =====================================================================

StageGatingState PureStageGateService::evaluate(const GateInputs& inputs) const
{
    // ---- 第 0 步：输入契约校验（调用方错误 fail-fast——不带病判定）----
    // 七快照的 stage 字段必须与数组序位一致：组板错误会把 A 阶段投影当成
    // B 阶段判定，属于必须暴露的装配缺陷（静默错位＝门控全错且不可观测）。
    const std::vector<ui::StageId>& sequence = ui::stageIdSequence();
    std::uint64_t maxEpoch = 0;  // 判定纪元＝七快照 epoch 最大值（I-WF-4 对齐面；uint64 无单位）
    for (std::size_t i = 0; i < inputs.snapshots.size(); ++i) {
        if (inputs.snapshots[i].stage != sequence[i]) {
            throw WorkflowError("evaluate: 快照板序位 " + std::to_string(i)
                                + " 的 stage 字段与冻结序不符（组板契约违约）");
        }
        // epoch 水位（I-WF-4）：任一快照低于上次判定水位＝旧投影——拒绝，
        // 防止旧投影覆盖新事件（§5.3 步骤 2 的 workflow 侧兜底；适配层应
        // 先自行丢弃，本校验是最后一道防线）。
        if (inputs.snapshots[i].epoch < inputs.lastEpoch) {
            throw WorkflowError("evaluate: 快照 epoch " 
                                + std::to_string(inputs.snapshots[i].epoch)
                                + " 低于水位 " + std::to_string(inputs.lastEpoch)
                                + "（旧投影不得覆盖新事件——I-WF-4）");
        }
        if (inputs.snapshots[i].epoch > maxEpoch) {
            maxEpoch = inputs.snapshots[i].epoch;
        }
    }

    // ---- 第 1 步：事件窗口重放（结果事实——§5.2）----
    const std::array<StageResultFact, 7> facts = replayEvents(inputs.events);

    // ---- 第 2 步：七阶段序贯判定（前序链 I-WF-1 依赖前序判定结果——必须
    //      按 StageId 冻结序推进，不可并行化判定顺序）----
    StageGatingState state;
    state.epoch = maxEpoch;
    for (std::size_t i = 0; i < 7U; ++i) {
        const ui::StageId stage = sequence[i];
        const ui::StageReadinessSnapshot& snapshot = inputs.snapshots[i];
        GateDecision decision;
        decision.stage = stage;
        decision.epoch = maxEpoch;  // 每条判定携带本轮纪元（消费方对齐用）

        // 域就绪项按 §4.2 聚合域词表认领（域注册键 → 投影条目；有序映射
        // ——遍历序确定，缺项拼接顺序稳定）。
        std::map<std::string, const ui::DomainReadinessItem*> byKey;
        for (const ui::DomainReadinessItem& item : snapshot.domains) {
            byKey[item.domainKey] = &item;
        }

        // ---- 规则 1：前序链（I-WF-1）——存在 Blocked 前序 → NotStarted。
        // §4.4 状态机：NotStarted ← 前序未过（"前序通过"是 Blocked 的前提
        // ——§4.3 词表）。取**序位最小**的 Blocked 前序作为根因指位（提示
        // 用户解锁最近的上游入口）。前序 Unavailable/NotStarted（I-WF-1
        // 字面"非 Blocked"）不阻断本阶段独立判定——就绪权威在域投影（N1：
        // 各域自报），门控不替域判断"上游产物是否真的够用"。
        const GateDecision* blockedPrefix = nullptr;  // 借用指针——指向本函数局部 decisions，生命周期内有效
        for (std::size_t p = 0; p < i; ++p) {
            if (state.decisions[p].status == ui::StageViewStatus::Blocked) {
                blockedPrefix = &state.decisions[p];
                break;  // 取首个（序位最小）Blocked 前序——确定性
            }
        }
        if (blockedPrefix != nullptr) {
            // 锁定：前序未过。原因指向前序（not-reached），解锁条件＝前序
            // 解锁——不在本阶段编造缺项（本域缺项事实可能尚未产生）。
            decision.status = ui::StageViewStatus::NotStarted;
            decision.unlocked = false;
            decision.reasonKeys.push_back(gateNotReachedReasonKey(blockedPrefix->stage));
            decision.unlockHintKey = gateUnlockHintKey(blockedPrefix->stage);
            state.decisions[i] = std::move(decision);
            continue;  // 本阶段判定完成——下游以上游状态为前序链输入
        }

        // ---- 规则 2：域未装配 → Unavailable（§4.3 词表"域未装配"；区别
        // 于缺项 Blocked——WF-VER-103 观测点）。聚合域词表中任一域无投影
        // 条目即未装配（§6.5：快照 domains 空＝该阶段无已注册域）。
        std::vector<std::string> missingDomains;  // §4.2 词表序的缺失域清单
        for (const std::string& domainKey : stageDomains(stage)) {
            if (byKey.find(domainKey) == byKey.end()) {
                missingDomains.push_back(domainKey);
            }
        }
        if (!missingDomains.empty()) {
            decision.status = ui::StageViewStatus::Unavailable;
            decision.unlocked = false;
            // 原因：基键（"域未装配"）＋逐域缺失键（定位缺哪个域插件）。
            decision.reasonKeys.push_back(gateDomainUnavailableKey(stage));
            for (const std::string& domainKey : missingDomains) {
                decision.reasonKeys.push_back(missingDomainReasonKey(stage, domainKey));
            }
            decision.unlockHintKey = gateUnlockHintKey(stage);
            state.decisions[i] = std::move(decision);
            continue;
        }

        // ---- 规则 3：缺项锁定 → Blocked（§4.2"投影缺项"；§4.4 Blocked ←
        // 前序通过＋投影缺项）。缺项事实完全采信域自报（inputComplete/
        // missingItemKeys——I-WF-2 不重算不增删）；缺项清单按域序拼接
        // （分组语义＝快照域注册序，NFR-COR-02 稳定序）。
        bool anyIncomplete = false;             // 任一域 inputComplete==false
        std::vector<ui::TextKey> missingItems;  // 域自报缺项键（透传拼接）
        bool hasActiveTask = false;             // 任一域 hasActiveTask（规则 4 用）
        for (const std::string& domainKey : stageDomains(stage)) {
            const ui::DomainReadinessItem* item = byKey[domainKey];  // 规则 2 已保证在场
            if (!item->inputComplete) { anyIncomplete = true; }
            for (const ui::TextKey& key : item->missingItemKeys) {
                missingItems.push_back(key);  // 透传（WF-VER-102：不增删）
            }
            if (item->hasActiveTask) { hasActiveTask = true; }
        }
        if (anyIncomplete || !missingItems.empty()) {
            decision.status = ui::StageViewStatus::Blocked;
            decision.unlocked = false;
            decision.missingItemKeys = std::move(missingItems);  // 透传缺项清单
            decision.reasonKeys.push_back(gateBlockedReasonKey(stage));
            decision.unlockHintKey = gateUnlockHintKey(stage);
            // 缺项与结果过期并存场景（§4.4 Completed→Blocked"输入回退"转移）：
            // 事件重放发现本阶段曾有归档结果且已失效时，Blocked 判定**同时**
            // 携带 staleHints——缺项（要修输入）与过期（历史结果不可信）是
            // 两个独立事实，只报缺项会让用户误以为旧结果仍可用（UX-12
            // "结果失效时级联提示"不因缺项而豁免）。
            const StageResultFact& blockedFact = facts[i];
            for (const auto& entry : blockedFact.staleHints) {
                decision.staleHints.push_back(entry.second);
            }
            state.decisions[i] = std::move(decision);
            continue;
        }

        // ---- 规则 4：计算中 → InProgress（§4.3 词表"有活动任务"；
        // §4.4 Blocked→InProgress 前提"缺项修复"已由规则 3 保证）。注意
        // ui 把 in-progress 词保留给"用户所在阶段"会话态（呈现合成）——
        // 本判定是门控状态机的"计算中"态，映射归 P-WF-2 裁决后的适配器
        // （GateDecision 类型注释已登记张力）。
        if (hasActiveTask) {
            decision.status = ui::StageViewStatus::InProgress;
            decision.unlocked = true;  // 计算中＝入口可进入（查看任务进度）
            state.decisions[i] = std::move(decision);
            continue;
        }

        // ---- 规则 5：结果事实（事件重放——§5.2）。输入齐备、无活动任务
        // 时按归档/失效事实定态：
        //   有结果且未失效 → Completed（§4.4 Blocked→Completed"评估完成"）；
        //   有结果但已失效 → Completed＋staleHints（§4.3"Completed＋
        //     results-stale 并存呈现"——历史面板可打开，提示另行携带；
        //     staleHints 中的重算建议**不自动执行**，D-WF-8）；
        //   无结果 → NotStarted＋解锁（"可进入但评估未开始"——UX-10 语境
        //     下的可计算态；下一步动作由建议引擎 R2 引导，§6.3）。
        const StageResultFact& fact = facts[i];
        decision.unlocked = true;  // 前序过＋缺项清——本阶段解锁（UX-12）
        if (fact.hasResult) {
            decision.status = ui::StageViewStatus::Completed;
            if (!fact.staleHints.empty()) {
                // 有序映射 → 向量（迭代序＝(阶段,域键) 字典序——确定性输出）。
                for (const auto& entry : fact.staleHints) {
                    decision.staleHints.push_back(entry.second);
                }
            }
        } else {
            decision.status = ui::StageViewStatus::NotStarted;  // 评估未开始（可进入）
        }
        state.decisions[i] = std::move(decision);
    }
    return state;
}

// =====================================================================
// mapInvalidation——级联失效映射（§4.3 D-WF-2；纯函数零任务派发）
// =====================================================================

std::vector<StaleHint> PureStageGateService::mapInvalidation(
    const core::DomainEvent& ev,
    const ui::StageReadinessSnapshot& snapshot,
    const InvalidationSignal& signal,
    const StageGatingState& gates) const
{
    // ---- 契约校验（调用方错误 fail-fast）----
    // ① 本接口只消费失效事件（§10.2 @note）：其余事件类别是调用序列缺陷
    //    （RevisionCommitted 应走 evaluate 重判定路径），静默返回空会把
    //    误用伪装成"无失效"。
    if (ev.kind != core::DomainEventKind::DependencyInvalidated) {
        throw WorkflowError("mapInvalidation: 仅接受 DependencyInvalidated 事件");
    }
    // ② epoch 对齐（I-WF-4）：快照低于门控状态纪元＝旧投影——拒绝（失效
    //    映射必须基于不小于上次判定的会话事实）。
    if (snapshot.epoch < gates.epoch) {
        throw WorkflowError("mapInvalidation: 快照 epoch "
                            + std::to_string(snapshot.epoch) + " 低于门控状态 epoch "
                            + std::to_string(gates.epoch) + "（旧投影——I-WF-4）");
    }

    // ---- 受影响域 → 受影响阶段（D-WF-2 导航映射；word list 外域名由
    //      stageForDomain fail-fast）----
    // 有序键＝(阶段序, 域键)：去重（同域可经多条信号重复到达）＋确定性
    // 输出序（阶段升序→域键字典序——§6.3 同优先级稳定序同源纪律）。
    std::map<std::pair<std::size_t, std::string>, StaleHint> hints;
    for (const std::string& domainKey : signal.domainKeys) {
        const std::size_t idx = stageIndex(stageForDomain(domainKey));
        StaleHint hint;
        hint.stage = static_cast<ui::StageId>(idx);
        hint.domainKey = domainKey;
        hint.reasons = signal.reasons;  // evidence 原因透传（零加工——不复制失效计算）
        hint.actionKey = kActionRecomputeDownstream;  // 建议动作＝复算下游（可跳过）
        hints[std::make_pair(idx, domainKey)] = std::move(hint);
    }
    if (hints.empty()) {
        return {};  // 无受影响域——无可映射内容（正常路径：范围判定为空）
    }

    // ---- 下游级联（UX-12"级联提示下游需重算"）----
    // 先对受影响阶段序位**定影**（快照集合），再逐阶段枚举下游——禁止在
    // hints 遍历中插入：关联容器迭代中对新元素的可见性未指定，会把级联
    // 传递序变成实现细节（破坏同输入同输出，D-WF-3）。受影响集合的下游
    // 枚举并集已覆盖全链（最小受影响阶段的下游枚举直达 Reporting）。
    std::set<std::size_t> affectedStages;  // 受影响阶段序位（升序去重）
    for (const auto& entry : hints) {
        affectedStages.insert(entry.first.first);
    }
    // 对每个受影响阶段的下游（StageId 序更大者）：仅当其门控状态为
    // Completed（有历史结果可失效——§4.3"下游阶段门控重算 Completed →
    // in-progress＋results-stale"）才产提示；Blocked/NotStarted/Unavailable
    // 的下游无结果可失效，InProgress 的下游重算已在途——都不产重复提示。
    // 提示的域定位＝下游阶段聚合域逐域（聚合阶段两域两条——定位粒度一致）。
    const std::vector<ui::StageId>& sequence = ui::stageIdSequence();
    for (const std::size_t affectedIdx : affectedStages) {
        for (std::size_t t = affectedIdx + 1U; t < 7U; ++t) {
            if (gates.decisions[t].status != ui::StageViewStatus::Completed) {
                continue;  // 下游无"已完成结果"可失效——级联到此为止
            }
            for (const std::string& domainKey : stageDomains(sequence[t])) {
                StaleHint hint;
                hint.stage = sequence[t];
                hint.domainKey = domainKey;
                hint.reasons = signal.reasons;  // 根因同源——上游失效原因透传
                hint.actionKey = kActionRecomputeDownstream;
                // 有序映射插入＝去重（t 同时受影响时已有同键条目，语义同源
                // ——根因均为本次 signal，覆盖无害且结果一致）。
                hints[std::make_pair(t, domainKey)] = std::move(hint);
            }
        }
    }

    // ---- 有序映射 → 输出向量（确定性序；纯函数返回——**不派发任何重算
    //      任务、不触碰任务端口**：重算是用户动作，UX-12/§4.3 红线）。
    std::vector<StaleHint> result;
    result.reserve(hints.size());
    for (const auto& entry : hints) {
        result.push_back(entry.second);
    }
    return result;
}

}  // namespace workflow
}  // namespace ird
}  // namespace sdurws
