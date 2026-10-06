/**
 * @file   GateAdviceTest.cpp
 * @brief  七阶段门控与下一步建议的模型测试（直调纯函数面——NFR-MNT-01）：
 *         WF-VER-101~109/111 逐用例承载（units/workflow.md §11.1）。
 *
 * 设计依据：
 *   - units/workflow.md §11.1（验证用例表 WF-VER-101~111——前置/操作/预期
 *     结果/观测点逐条对应下方 TEST 名尾注）、§4（门控模型）、§6（建议引擎）
 *   - 需求 UX-12（七阶段解锁/锁定＋级联提示）、UX-01（四要素）、UX-02
 *     （工程用语键）、UX-10（状态词与 ui 词表对齐）
 *   - 任务契约 tasks/foundation/WP-22-T03.json acceptance 1/2（级联提示
 *     用例；纯函数面/优先级/四要素）
 *
 * 夹具纪律（§11.0 用例登记约定）：门控/建议用固定投影夹具（golden `wf-*`
 * 全量登记随 WP-22-T13；本文件内置最小确定性夹具——各用例独立构造，无
 * 共享可变状态）。数值期望均为解析给定的精确串/枚举值（本单元判定面为
 * 布尔/枚举/键串事实——无浮点容差需求，I-WF-3 零领域阈值同源）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记（testkit §7.3）

#include <sdurws/ird/workflow/Advice.hpp>
#include <sdurws/ird/workflow/Gate.hpp>
#include <sdurws/ird/workflow/Types.hpp>

#include <sdurws/ird/core/Events.hpp>           // DomainEvent 工厂（mapInvalidation 失效事件夹具）
#include <sdurws/ird/core/Identity.hpp>         // Id128::generate（载荷身份）
#include <sdurws/ird/evidence/Currentness.hpp>  // InvalidationReason（原因词表值）
#include <sdurws/ird/ui/IStageNavigationModel.hpp>  // ui::stageIdSequence（七阶段冻结序对照）

#include <regex>
#include <string>
#include <vector>

namespace {

using namespace sdurws::ird;
using ui::StageId;
using ui::StageViewStatus;
using workflow::GateDecision;
using workflow::GateEvent;
using workflow::GateInputs;
using workflow::StageGatingState;
using workflow::StaleHint;

// =====================================================================
// 投影夹具构造（确定性最小夹具——各用例独立组装，无共享状态）
// =====================================================================

/// 单域就绪投影项（ui.md §6.5 DomainReadinessItem 形状——verdict 取
/// Feasible 表示"最近正式判定可行"，缺省 NotApplicable 表示无判定——
/// 两者均不参与门控判定：门控只看 inputComplete/missingItemKeys/
/// hasActiveTask 三布尔事实，I-WF-2）。
ui::DomainReadinessItem makeItem(const std::string& domainKey,
                                 bool inputComplete,
                                 std::vector<ui::TextKey> missingItemKeys = {},
                                 bool hasActiveTask = false)
{
    ui::DomainReadinessItem item;
    item.domainKey = domainKey;
    item.verdict = inputComplete ? core::EngineeringStatus::Feasible
                                 : core::EngineeringStatus::NotApplicable;
    item.inputComplete = inputComplete;
    item.missingItemKeys = std::move(missingItemKeys);
    item.hasActiveTask = hasActiveTask;
    return item;
}

/// 单阶段全就绪快照（该阶段聚合域逐域 ready——workflow::stageDomains
/// 是聚合词表权威，夹具经它组装保证与判定同源）。
ui::StageReadinessSnapshot makeReadySnapshot(StageId stage, std::uint64_t epoch)
{
    ui::StageReadinessSnapshot snapshot;
    snapshot.stage = stage;
    snapshot.epoch = epoch;  // 会话纪元（单调 uint64，夹具用例内自洽即可）
    for (const std::string& domainKey : workflow::stageDomains(stage)) {
        snapshot.domains.push_back(makeItem(domainKey, /*inputComplete=*/true));
    }
    return snapshot;
}

/// 七阶段全就绪快照板（基线"全链就绪"夹具——WF-VER-101/105 用）。
GateInputs makeReadyBoard(std::uint64_t epoch)
{
    GateInputs inputs;
    for (StageId stage : ui::stageIdSequence()) {
        inputs.snapshots[static_cast<std::size_t>(stage)] =
            makeReadySnapshot(stage, epoch);
    }
    return inputs;
}

/// 给某阶段设置"缺项"投影（inputComplete=false＋缺项键——WF-VER-102/104 用）。
void setStageMissing(GateInputs& inputs, StageId stage, const std::string& domainKey,
                     std::vector<ui::TextKey> missingItemKeys)
{
    auto& snapshot = inputs.snapshots[static_cast<std::size_t>(stage)];
    for (ui::DomainReadinessItem& item : snapshot.domains) {
        if (item.domainKey == domainKey) {
            item.inputComplete = false;
            item.missingItemKeys = std::move(missingItemKeys);
            return;
        }
    }
    FAIL() << "夹具错误：阶段无该聚合域 " << domainKey;  // 夹具自检（不静默）
}

/// 依赖失效事件（core 工厂构造——仅身份载荷；域键与原因由调用方关联，
/// 模拟"L5 适配层折叠 evidence 投影"的产物，GateEvent 注释同源）。
core::DomainEvent makeInvalidatedEvent()
{
    core::DependencyInvalidatedPayload payload;
    payload.project = core::ProjectId::generate();
    payload.branch = core::BranchId::generate();
    payload.revision = core::RevisionId::generate();
    return core::DomainEvent::make(payload);
}

/// 归档结果事件夹具（GateEvent 形态——kind 复用 core 词表＋域键关联）。
GateEvent makeArchivedEvent(std::vector<std::string> domainKeys)
{
    GateEvent event;
    event.kind = core::DomainEventKind::ResultArchived;
    event.domainKeys = std::move(domainKeys);
    return event;
}

/// 全阶段归档事件板（每阶段聚合域各一条——"全链有结果"事实夹具，
/// WF-VER-105 的"上游完成态"前置）。
std::vector<GateEvent> makeAllArchivedEvents()
{
    std::vector<GateEvent> events;
    for (StageId stage : ui::stageIdSequence()) {
        events.push_back(makeArchivedEvent(workflow::stageDomains(stage)));
    }
    return events;
}

/// 工程用语红线扫描（WF-VER-111 判据——键不得含哈希/Schema/插件名/内部
/// 标识形态；64 位 hex＝内容摘要形态，"plugin"＝内部插件名，"__"＝内部
/// 标识惯例，"schema."＝Schema 词）。
void expectEngineeringWording(const std::string& key, const char* where)
{
    static const std::regex kHash64(R"re([0-9a-f]{32})re");  // 32 hex 已足判哈希形态（tag+64 hex 的尾部）
    EXPECT_FALSE(std::regex_search(key, kHash64))
        << where << " 含哈希形态键: " << key;
    EXPECT_EQ(key.find("plugin"), std::string::npos)
        << where << " 含插件名键: " << key;
    EXPECT_EQ(key.find("Schema"), std::string::npos)
        << where << " 含 Schema 词键: " << key;
    EXPECT_EQ(key.find("schema."), std::string::npos)
        << where << " 含 schema 键: " << key;
    EXPECT_EQ(key.find("__"), std::string::npos)
        << where << " 含内部标识键: " << key;
}

/// 遍历一条建议产出的全部键做工程用语扫描（要素①③④＋建议条目键）。
void expectAdviceWording(const workflow::StageAdvice& advice, const char* where)
{
    expectEngineeringWording(advice.goalKey, where);
    for (const ui::TextKey& key : advice.missingItemKeys) {
        expectEngineeringWording(key, where);
    }
    for (const ui::TextKey& key : advice.problemKeys) {
        expectEngineeringWording(key, where);
    }
    for (const workflow::NextStepAdvice& step : advice.steps) {
        expectEngineeringWording(step.titleKey, where);
        for (const ui::TextKey& key : step.detailKeys) {
            expectEngineeringWording(key, where);
        }
    }
}

}  // namespace

// =====================================================================
// WF-VER-101：七阶段顺序与解锁（UX-12）
// =====================================================================

/**
 * 阶段 N 解锁当且仅当前序全过＋本域就绪；decisions 顺序与 StageId 冻结序
 * （ui::stageIdSequence——UX-12 七阶段序）一致。观测点：GateDecision 逐
 * 阶段 stage/status/unlocked。
 */
TEST(WfGate, SevenStageUnlockSequence_WP22T03_ACC1_UX12_WFVER101)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-12"},
                  std::vector<std::string>{});  // 验证用例 WF-VER-101

    const workflow::PureStageGateService gate;

    // 场景 A：全链就绪（无事件）——每阶段投影齐备、评估未开始：全部解锁
    // （NotStarted＝"可进入但评估未开始"，unlocked 判定位区分锁定态）。
    {
        const StageGatingState state = gate.evaluate(makeReadyBoard(10));
        const std::vector<StageId>& sequence = ui::stageIdSequence();
        for (std::size_t i = 0; i < state.decisions.size(); ++i) {
            EXPECT_EQ(state.decisions[i].stage, sequence[i])
                << "判定序与 UX-12 冻结序不一致（阶段 " << i << "）";
            EXPECT_TRUE(state.decisions[i].unlocked)
                << "全就绪链应全解锁（阶段 " << i << "）";
            EXPECT_EQ(state.decisions[i].status, StageViewStatus::NotStarted)
                << "就绪但无归档结果＝评估未开始（阶段 " << i << "）";
            EXPECT_EQ(state.decisions[i].epoch, 10U) << "判定纪元＝快照 epoch";
        }
    }

    // 场景 B：kinematics 域缺项——kinematics=Blocked，其**全部下游**锁定
    // （前序未过 → NotStarted＋unlocked=false——I-WF-1 前序链级联锁定），
    // 而 Modeling/Requirements（前序就绪）不受影响。
    {
        GateInputs inputs = makeReadyBoard(11);
        setStageMissing(inputs, StageId::Kinematics, "kinematics",
                        {"stage.kinematics.missing.robot-definition"});
        const StageGatingState state = gate.evaluate(inputs);

        EXPECT_EQ(state.decisions[2].status, StageViewStatus::Blocked);
        EXPECT_FALSE(state.decisions[2].unlocked);
        for (std::size_t i = 3; i < 7; ++i) {
            EXPECT_EQ(state.decisions[i].status, StageViewStatus::NotStarted)
                << "下游锁定态（阶段 " << i << "）";
            EXPECT_FALSE(state.decisions[i].unlocked)
                << "前序 Blocked → 下游不得解锁（阶段 " << i << "）";
            // 锁定原因指向前序（not-reached）——根因指位而非下游缺项。
            ASSERT_FALSE(state.decisions[i].reasonKeys.empty());
            EXPECT_EQ(state.decisions[i].reasonKeys.front(),
                      workflow::gateNotReachedReasonKey(StageId::Kinematics));
            EXPECT_EQ(state.decisions[i].unlockHintKey,
                      std::optional<ui::TextKey>(
                          workflow::gateUnlockHintKey(StageId::Kinematics)));
        }
        EXPECT_TRUE(state.decisions[0].unlocked && state.decisions[1].unlocked)
            << "前序就绪阶段不受下游缺项影响";
    }

    // 场景 C：逐级解锁——补齐 kinematics 投影后其下游恢复独立判定
    // （门控是投影的纯函数：投影修复即解锁，无内部记忆）。
    {
        GateInputs inputs = makeReadyBoard(12);
        setStageMissing(inputs, StageId::Kinematics, "kinematics",
                        {"stage.kinematics.missing.robot-definition"});
        const StageGatingState blocked = gate.evaluate(inputs);
        ASSERT_FALSE(blocked.decisions[3].unlocked);

        // 缺项修复＝域自报恢复就绪（inputComplete=true 且缺项清空——
        // 就绪结论权威在域投影，门控只采信）。
        for (ui::DomainReadinessItem& item : inputs.snapshots[2].domains) {
            if (item.domainKey == "kinematics") {
                item.inputComplete = true;
                item.missingItemKeys.clear();
            }
        }
        const StageGatingState repaired = gate.evaluate(inputs);
        EXPECT_TRUE(repaired.decisions[2].unlocked);
        EXPECT_TRUE(repaired.decisions[3].unlocked) << "缺项修复后下游恢复判定";
    }
}

// =====================================================================
// WF-VER-102：缺项锁定与清单透传（UX-10/UX-12）
// =====================================================================

/**
 * Blocked＋missingItemKeys 逐项透传（不重算不增删——I-WF-2）；缺项清单
 * 与原因键/解锁键三要素齐备（ui §6.4 blocked 行"附原因＋下一步建议"）。
 */
TEST(WfGate, MissingItemsLockedWithTransparency_WP22T03_ACC1_WFVER102)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-10", "UX-12"},
                  std::vector<std::string>{});  // 验证用例 WF-VER-102

    const workflow::PureStageGateService gate;
    const std::vector<ui::TextKey> missing{
        "stage.kinematics.missing.robot-definition",
        "stage.kinematics.missing.home-pose",
    };
    GateInputs inputs = makeReadyBoard(20);
    setStageMissing(inputs, StageId::Kinematics, "kinematics", missing);

    const StageGatingState state = gate.evaluate(inputs);
    const GateDecision& decision = state.decisions[2];

    EXPECT_EQ(decision.status, StageViewStatus::Blocked);
    EXPECT_FALSE(decision.unlocked);
    // 透传等价：顺序与内容逐项一致（域自报序＝快照域序——不排序不去重）。
    EXPECT_EQ(decision.missingItemKeys, missing);
    // 原因基键＋解锁键（缺项明细不并入 reasonKeys——两字段语义分离，
    // 适配器折叠归 P-WF-2 裁决后的呈现面）。
    ASSERT_FALSE(decision.reasonKeys.empty());
    EXPECT_EQ(decision.reasonKeys.front(),
              workflow::gateBlockedReasonKey(StageId::Kinematics));
    EXPECT_EQ(decision.unlockHintKey,
              std::optional<ui::TextKey>(
                  workflow::gateUnlockHintKey(StageId::Kinematics)));
}

// =====================================================================
// WF-VER-103：域未装配（§4.2——区别于 Blocked）
// =====================================================================

/**
 * 投影缺域夹具：聚合域词表内任一域无投影条目 → Unavailable（不是
 * NotStarted 也不是 Blocked——域未装配是能力缺失，缺项是输入缺失）。
 */
TEST(WfGate, DomainAbsentReportsUnavailable_WP22T03_ACC1_WFVER103)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-12"},
                  std::vector<std::string>{});  // 验证用例 WF-VER-103

    const workflow::PureStageGateService gate;
    GateInputs inputs = makeReadyBoard(30);
    // kinematics 阶段快照清空域条目（域插件未装配——§6.5 空＝无已注册域）。
    inputs.snapshots[2].domains.clear();

    const StageGatingState state = gate.evaluate(inputs);
    const GateDecision& decision = state.decisions[2];

    EXPECT_EQ(decision.status, StageViewStatus::Unavailable);
    EXPECT_FALSE(decision.unlocked);
    // 原因：基键＋逐域缺失键（定位缺哪个域插件——Types.cpp 键构造）。
    ASSERT_GE(decision.reasonKeys.size(), 2U);
    EXPECT_EQ(decision.reasonKeys[0],
              workflow::gateDomainUnavailableKey(StageId::Kinematics));
    EXPECT_EQ(decision.reasonKeys[1],
              workflow::missingDomainReasonKey(StageId::Kinematics, "kinematics"));
    EXPECT_EQ(decision.unlockHintKey,
              std::optional<ui::TextKey>(
                  workflow::gateUnlockHintKey(StageId::Kinematics)));
}

// =====================================================================
// WF-VER-104：聚合阶段（轨迹＋动力学）——两域均就绪才解锁
// =====================================================================

/**
 * 一域就绪一域缺项：TrajectoryDynamics=Blocked，缺项清单**只含未就绪域**
 * 的缺项（就绪域的清单为空不污染聚合结果——§4.2 聚合判定行）。
 */
TEST(WfGate, AggregateStagePartialReady_WP22T03_ACC1_WFVER104)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-12"},
                  std::vector<std::string>{});  // 验证用例 WF-VER-104

    const workflow::PureStageGateService gate;
    const std::vector<ui::TextKey> dynamicsMissing{
        "stage.trajectory-dynamics.missing.dynamics-parameters",
    };
    GateInputs inputs = makeReadyBoard(40);
    setStageMissing(inputs, StageId::TrajectoryDynamics, "dynamics",
                    dynamicsMissing);  // dynamics 缺项；trajectory 保持就绪

    const StageGatingState state = gate.evaluate(inputs);
    const GateDecision& decision = state.decisions[3];

    EXPECT_EQ(decision.status, StageViewStatus::Blocked)
        << "聚合阶段任一域缺项即 Blocked（两域均就绪才解锁）";
    EXPECT_EQ(decision.missingItemKeys, dynamicsMissing)
        << "缺项清单只含未就绪域（trajectory 就绪不产缺项）";
    EXPECT_FALSE(decision.unlocked);
    // 下游（selection）锁定——聚合阶段 Blocked 参与前序链。
    EXPECT_FALSE(state.decisions[4].unlocked);
}

// =====================================================================
// WF-VER-105：级联失效映射（UX-12——契约 acceptance 1 核心用例）
// =====================================================================

/**
 * 上游完成态＋结果失效：mapInvalidation 产出受影响阶段与下游 Completed
 * 阶段的提示数据（定位/原因/建议动作三要素齐备）；**不自动触发重算**
 * ——纯函数面（同输入双跑同输出、无副作用），重算建议仅为提示 token。
 */
TEST(WfGate, CascadeInvalidationHints_WP22T03_ACC1_UX12_WFVER105)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-12"},
                  std::vector<std::string>{});  // 验证用例 WF-VER-105

    const workflow::PureStageGateService gate;

    // 前置：全链就绪且全部已有归档结果（"上游完成态"——evaluate 事件重放
    // 后全阶段 Completed）。
    GateInputs inputs = makeReadyBoard(50);
    inputs.events = makeAllArchivedEvents();
    const StageGatingState state = gate.evaluate(inputs);
    for (const GateDecision& decision : state.decisions) {
        ASSERT_EQ(decision.status, StageViewStatus::Completed)
            << "夹具前置：全链应有归档结果";
        ASSERT_TRUE(decision.staleHints.empty());
    }

    // 失效注入：kinematics 域结果失效（evidence 范围判定给出受影响域＋
    // 原因清单——调用方折叠为失效事件与信号）。
    const std::vector<evidence::InvalidationReason> reasons{
        evidence::InvalidationReason{"robot-definition",
                                     evidence::InvalidationKind::ObjectContentChanged,
                                     "机械臂定义对象内容变化（旧版→新版）"},
    };
    GateEvent invalidation;
    invalidation.kind = core::DomainEventKind::DependencyInvalidated;
    invalidation.domainKeys = {"kinematics"};
    invalidation.reasons = reasons;

    // 门控重判定：Kinematics 与下游 Completed 阶段携带 results-stale 提示
    // （Completed 态不变——"完成但过期"并存呈现，§4.3）。
    GateInputs staleInputs = makeReadyBoard(51);
    staleInputs.events = makeAllArchivedEvents();
    staleInputs.events.push_back(invalidation);
    const StageGatingState staleState = gate.evaluate(staleInputs);
    EXPECT_EQ(staleState.decisions[2].status, StageViewStatus::Completed);
    ASSERT_FALSE(staleState.decisions[2].staleHints.empty());
    EXPECT_EQ(staleState.decisions[2].staleHints.front().domainKey, "kinematics");
    EXPECT_EQ(staleState.decisions[2].staleHints.front().reasons, reasons);
    EXPECT_EQ(staleState.decisions[2].staleHints.front().actionKey,
              workflow::kActionRecomputeDownstream);
    // 上游 Modeling/Requirements 不受 kinematics 失效影响（失效只向下游传播）。
    EXPECT_TRUE(staleState.decisions[0].staleHints.empty());
    EXPECT_TRUE(staleState.decisions[1].staleHints.empty());

    // mapInvalidation：受影响阶段自身＋全部下游 Completed 阶段产提示。
    workflow::InvalidationSignal signal;
    signal.domainKeys = {"kinematics"};
    signal.reasons = reasons;
    const core::DomainEvent event = makeInvalidatedEvent();
    const std::vector<StaleHint> hints =
        gate.mapInvalidation(event, staleInputs.snapshots[2], signal, staleState);

    // 期望集（解析给定）：Kinematics(1 条)＋TrajectoryDynamics(2 域)
    // ＋Selection/Optimization/Reporting(各 1)——共 6 条；Modeling/
    // Requirements 是失效阶段的上游，不得提示。
    ASSERT_EQ(hints.size(), 6U);
    EXPECT_EQ(hints[0].stage, StageId::Kinematics);
    EXPECT_EQ(hints[0].domainKey, "kinematics");
    EXPECT_EQ(hints[1].stage, StageId::TrajectoryDynamics);
    EXPECT_EQ(hints[2].stage, StageId::TrajectoryDynamics);
    EXPECT_EQ(hints[3].stage, StageId::Selection);
    EXPECT_EQ(hints[4].stage, StageId::Optimization);
    EXPECT_EQ(hints[5].stage, StageId::Reporting);
    for (const StaleHint& hint : hints) {
        // 三要素齐备：定位（stage＋domainKey）＋原因＋建议动作。
        EXPECT_EQ(hint.reasons, reasons) << "原因透传（evidence 词表值）";
        EXPECT_EQ(hint.actionKey, workflow::kActionRecomputeDownstream);
        EXPECT_NE(hint.domainKey, "");
    }

    // 不自动触发重算的纯函数面证据：同输入双跑同输出（无副作用/无内部
    // 记忆），且产出的 actionKey 是"建议"而非任务派发（无任务句柄——
    // 接口面由契约测试 WF-VER-112 钉住）。
    const std::vector<StaleHint> replay =
        gate.mapInvalidation(event, staleInputs.snapshots[2], signal, staleState);
    EXPECT_EQ(replay, hints);
}

/**
 * 级联范围补充钉：下游未 Completed（无历史结果）不产提示——失效级联
 * 只对"有已完成结果"的下游有效（§4.3 映射规则的负例）。
 */
TEST(WfGate, CascadeSkipsDownstreamWithoutResults_WP22T03_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-12"},
                  std::vector<std::string>{});  // 验证用例 WF-VER-105（级联负例）

    const workflow::PureStageGateService gate;
    // 门控状态：全链就绪但**无任何归档结果**（全 NotStarted——可进入、
    // 评估未开始）：下游无历史结果可失效。
    const StageGatingState state = gate.evaluate(makeReadyBoard(52));

    workflow::InvalidationSignal signal;
    signal.domainKeys = {"modeling"};
    signal.reasons = {evidence::InvalidationReason{
        "baseline", evidence::InvalidationKind::SliceContentChanged, "切片变化"}};
    const std::vector<StaleHint> hints = gate.mapInvalidation(
        makeInvalidatedEvent(), makeReadySnapshot(StageId::Modeling, 52), signal,
        state);

    // 受影响阶段（Modeling）自身仍产提示（定位事实）；下游无结果不产。
    ASSERT_EQ(hints.size(), 1U);
    EXPECT_EQ(hints[0].stage, StageId::Modeling);
}

/**
 * 失效后重算完成（新归档结果）→ stale 清除（事件窗口时序语义：新结果
 * 覆盖旧失效——§5.2 归档事件处置）。
 */
TEST(WfGate, StaleClearedByFreshResult_WP22T03_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-12"},
                  std::vector<std::string>{});  // 验证用例 WF-VER-105（重算恢复）

    const workflow::PureStageGateService gate;
    GateInputs inputs = makeReadyBoard(53);
    inputs.events = makeAllArchivedEvents();
    // 先失效（kinematics），再归档（kinematics）——发布序决定最终态。
    GateEvent invalidation;
    invalidation.kind = core::DomainEventKind::DependencyInvalidated;
    invalidation.domainKeys = {"kinematics"};
    inputs.events.push_back(invalidation);
    inputs.events.push_back(makeArchivedEvent({"kinematics"}));

    const StageGatingState state = gate.evaluate(inputs);
    EXPECT_EQ(state.decisions[2].status, StageViewStatus::Completed);
    EXPECT_TRUE(state.decisions[2].staleHints.empty())
        << "失效后的新归档结果恢复当前性（重算完成的门控可见效果）";
}

// =====================================================================
// WF-VER-106：epoch 一致性（I-WF-4——旧投影不覆盖新事件）
// =====================================================================

/**
 * 快照 epoch 低于水位：evaluate 拒绝（WorkflowError）；mapInvalidation 的
 * 快照低于门控状态纪元同样拒绝——旧投影不覆盖新事件（丢弃的兜底防线）。
 */
TEST(WfGate, EpochWatermarkRejected_WP22T03_ACC2_WFVER106)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-12"},
                  std::vector<std::string>{});  // 验证用例 WF-VER-106

    const workflow::PureStageGateService gate;

    // evaluate：快照 epoch=3 < 水位 5 → WorkflowError（调用方时序违约）。
    GateInputs stale = makeReadyBoard(3);
    stale.lastEpoch = 5;
    EXPECT_THROW(gate.evaluate(stale), workflow::WorkflowError);

    // 合法水位（epoch==lastEpoch）放行——"不小于上次 epoch"（I-WF-4 原文）。
    GateInputs current = makeReadyBoard(5);
    current.lastEpoch = 5;
    EXPECT_NO_THROW(gate.evaluate(current));

    // mapInvalidation：快照 epoch 低于门控状态纪元 → WorkflowError。
    GateInputs base = makeReadyBoard(60);
    const StageGatingState state = gate.evaluate(base);  // state.epoch=60
    workflow::InvalidationSignal signal;
    signal.domainKeys = {"modeling"};
    EXPECT_THROW(gate.mapInvalidation(makeInvalidatedEvent(),
                                      makeReadySnapshot(StageId::Modeling, 59),
                                      signal, state),
                 workflow::WorkflowError);
    EXPECT_NO_THROW(gate.mapInvalidation(makeInvalidatedEvent(),
                                         makeReadySnapshot(StageId::Modeling, 60),
                                         signal, state));
}

/**
 * 输入组板契约：快照 stage 字段与序位不符 → WorkflowError（组板错误
 * fail-fast——静默错位＝门控全错且不可观测）。
 */
TEST(WfGate, BoardStageMismatchRejected_WP22T03_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-12"},
                  std::vector<std::string>{});  // 验证用例 WF-VER-106（组板契约）

    const workflow::PureStageGateService gate;
    GateInputs inputs = makeReadyBoard(70);
    inputs.snapshots[2].stage = StageId::Modeling;  // 序位 2 应为 Kinematics
    EXPECT_THROW(gate.evaluate(inputs), workflow::WorkflowError);
}

// =====================================================================
// WF-VER-107：确定性重放（NFR-COR-02 同型——同输入同输出）
// =====================================================================

/**
 * 复合夹具（部分缺项＋失效/归档事件混合）双跑：evaluate 与 adviseFor
 * 输出逐字段相等（纯函数面承诺——无内部记忆/无时钟/无随机）。
 */
TEST(WfGate, DeterministicReplay_WP22T03_ACC2_WFVER107)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-12", "UX-01"},
                  std::vector<std::string>{});  // 验证用例 WF-VER-107

    const workflow::PureStageGateService gate;
    const workflow::PureNextStepAdvisor advisor;

    GateInputs inputs = makeReadyBoard(80);
    setStageMissing(inputs, StageId::Selection, "selection",
                    {"stage.selection.missing.motor-catalog"});
    inputs.events = makeAllArchivedEvents();
    GateEvent invalidation;
    invalidation.kind = core::DomainEventKind::DependencyInvalidated;
    invalidation.domainKeys = {"trajectory", "kinematics"};
    inputs.events.push_back(invalidation);

    const StageGatingState first = gate.evaluate(inputs);
    const StageGatingState second = gate.evaluate(inputs);
    EXPECT_EQ(first, second) << "同（快照板, 事件窗口）同输出";

    workflow::AdviceInputs adviceInputs;
    adviceInputs.hasArchivedResult = true;
    const workflow::StageAdvice adviceFirst = advisor.adviseFor(
        StageId::TrajectoryDynamics, first, inputs.snapshots[3], adviceInputs);
    const workflow::StageAdvice adviceSecond = advisor.adviseFor(
        StageId::TrajectoryDynamics, second, inputs.snapshots[3], adviceInputs);
    EXPECT_EQ(adviceFirst, adviceSecond) << "建议同输入同输出";
}

// =====================================================================
// WF-VER-108：建议规则优先级（§6.3——R7>R5>R1>R4>R3>R2>R8>R6）
// =====================================================================

/**
 * 恢复＋阻塞诊断＋缺项＋失效＋活动任务并存：steps 的 actionKey 序列等于
 * 优先级词表序（首条＝最高优先）；同优先级多条（R4 两条失效提示）按
 * staleHints 稳定序输出。
 */
TEST(WfAdvice, RulePriorityOrder_WP22T03_ACC2_WFVER108)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-01"},
                  std::vector<std::string>{});  // 验证用例 WF-VER-108

    const workflow::PureStageGateService gate;
    const workflow::PureNextStepAdvisor advisor;

    // 夹具：TrajectoryDynamics 阶段——dynamics 缺项（R1）＋两域失效事件
    // （R4×2）＋trajectory 域有活动任务（R3）。
    GateInputs inputs = makeReadyBoard(90);
    setStageMissing(inputs, StageId::TrajectoryDynamics, "dynamics",
                    {"stage.trajectory-dynamics.missing.dynamics-parameters"});
    inputs.events = {
        GateEvent{core::DomainEventKind::ResultArchived, {"trajectory", "dynamics"}, {}},
        GateEvent{core::DomainEventKind::DependencyInvalidated, {"trajectory"}, {}},
        GateEvent{core::DomainEventKind::DependencyInvalidated, {"dynamics"}, {}},
    };
    const StageGatingState state = gate.evaluate(inputs);

    // 快照注入活动任务事实（trajectory 域）。
    ui::StageReadinessSnapshot snapshot = inputs.snapshots[3];
    for (ui::DomainReadinessItem& item : snapshot.domains) {
        if (item.domainKey == "trajectory") { item.hasActiveTask = true; }
    }

    // 会话态事实：恢复横幅场景（R7）＋阻塞诊断（R5）并存。
    workflow::AdviceInputs adviceInputs;
    adviceInputs.recoveryBannerKeys = {"stage.workflow.recovery.draft-found"};
    adviceInputs.blockingDiagnosticKeys = {"diag.kin-preflight-failed.title"};
    adviceInputs.hasArchivedResult = true;  // 压制 R2（结果在——归档事件已给出）

    const workflow::StageAdvice advice =
        advisor.adviseFor(StageId::TrajectoryDynamics, state, snapshot, adviceInputs);

    // 期望序列（§6.3 优先级 R7>R5>R1>R4>R3；R2/R8/R6 不触发——结果在、
    // 非只读、非 Completed）。缺项（Blocked）与结果过期（staleHints）并存
    // ——缺项与过期是两个独立事实，Blocked 判定同时携带（见 Gate.cpp 规则 3）。
    ASSERT_EQ(advice.steps.size(), 6U);
    EXPECT_EQ(advice.steps[0].actionKey, workflow::kActionRecoverSession);
    EXPECT_EQ(advice.steps[1].actionKey, workflow::kActionResolveFindings);
    EXPECT_EQ(advice.steps[2].actionKey, workflow::kActionFixInputs);
    // R4 两条提示的对象定位＝staleHints 稳定序（(阶段,域键) 字典序——
    // 事件重放输出；"dynamics" < "trajectory"）。
    EXPECT_EQ(advice.steps[3].actionKey, workflow::kActionRecomputeDownstream);
    EXPECT_EQ(advice.steps[3].targetObjectId, "dynamics");
    EXPECT_EQ(advice.steps[4].actionKey, workflow::kActionRecomputeDownstream);
    EXPECT_EQ(advice.steps[4].targetObjectId, "trajectory");
    EXPECT_EQ(advice.steps[5].actionKey, workflow::kActionReviewActiveTask);
}

// =====================================================================
// WF-VER-109：UX-01 四要素齐备（UX-01）
// =====================================================================

/**
 * Blocked 阶段：goal/missing/problems/nextStep 四要素全部齐备且为工程
 * 用语键（键半区——文案值归 ui 资源，UX-02）。
 */
TEST(WfAdvice, Ux01FourElementsPresent_WP22T03_ACC2_UX01_WFVER109)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-01", "UX-02"},
                  std::vector<std::string>{});  // 验证用例 WF-VER-109

    const workflow::PureStageGateService gate;
    const workflow::PureNextStepAdvisor advisor;

    GateInputs inputs = makeReadyBoard(91);
    setStageMissing(inputs, StageId::Kinematics, "kinematics",
                    {"stage.kinematics.missing.robot-definition"});
    const StageGatingState state = gate.evaluate(inputs);
    const workflow::StageAdvice advice = advisor.adviseFor(
        StageId::Kinematics, state, inputs.snapshots[2], {});

    // 要素①目标：goalKey＝stage.<token>.goal 形态（非空、可解析形态）。
    EXPECT_EQ(advice.goalKey, "stage.kinematics.goal");
    // 要素②必需输入：缺项透传非空。
    EXPECT_FALSE(advice.missingItemKeys.empty());
    // 要素③当前问题：门控原因键＋缺项键并入，非空。
    EXPECT_FALSE(advice.problemKeys.empty());
    // 要素④下一步操作：R1 命中（Blocked＋缺项非空）——首条 fix-inputs。
    ASSERT_FALSE(advice.steps.empty());
    EXPECT_EQ(advice.steps.front().actionKey, workflow::kActionFixInputs);
    EXPECT_EQ(advice.steps.front().titleKey, "advice.fix-inputs.title");
    // 全部键过工程用语扫描（WF-VER-111 同判据内嵌）。
    expectAdviceWording(advice, "WF-VER-109");
}

// =====================================================================
// 八规则逐条触发面（§6.3 规则词表全覆盖——R2/R3/R6/R8 独立场景）
// =====================================================================

/**
 * R2 发起计算：输入齐备＋无活动任务＋结果缺失（hasArchivedResult=false）
 * → run-evaluation；R3 与 R2 互斥场景（有任务时 R3 先于 R2——优先级）。
 */
TEST(WfAdvice, RunEvaluationWhenReadyWithoutResult_WP22T03_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-01"},
                  std::vector<std::string>{});  // 验证用例 WF-VER-108（R2 规则）

    const workflow::PureStageGateService gate;
    const workflow::PureNextStepAdvisor advisor;

    // 输入齐备、无事件（无归档结果）：门控 NotStarted（可进入）——R2 命中。
    GateInputs inputs = makeReadyBoard(92);
    const StageGatingState state = gate.evaluate(inputs);
    const workflow::StageAdvice advice = advisor.adviseFor(
        StageId::Modeling, state, inputs.snapshots[0], {});
    ASSERT_FALSE(advice.steps.empty());
    EXPECT_EQ(advice.steps.front().actionKey, workflow::kActionRunEvaluation);
    EXPECT_EQ(advice.steps.front().targetStage, StageId::Modeling);

    // 结果已在（hasArchivedResult=true）：R2 不触发——steps 为空（可跳过：
    // 空建议合法，D-WF-8）。
    workflow::AdviceInputs withResult;
    withResult.hasArchivedResult = true;
    const workflow::StageAdvice noAdvice = advisor.adviseFor(
        StageId::Modeling, state, inputs.snapshots[0], withResult);
    EXPECT_TRUE(noAdvice.steps.empty());
}

/**
 * R6 前进下一阶段：当前 Completed 且下一 NotStarted → advance-stage
 * （targetStage＝下一阶段）；Reporting（末阶段）不触发。
 */
TEST(WfAdvice, AdvanceStageAfterCompletion_WP22T03_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-01"},
                  std::vector<std::string>{});  // 验证用例 WF-VER-108（R6 规则）

    const workflow::PureStageGateService gate;
    const workflow::PureNextStepAdvisor advisor;

    GateInputs inputs = makeReadyBoard(93);
    inputs.events = makeAllArchivedEvents();  // 全链 Completed
    const StageGatingState state = gate.evaluate(inputs);

    // Modeling Completed、Requirements 也是 Completed（全链有结果）——
    // R6 要求下一阶段 NotStarted：构造 Requirements 无结果（清事件重评）。
    GateInputs partial = makeReadyBoard(94);
    partial.events = {makeArchivedEvent(workflow::stageDomains(StageId::Modeling))};
    const StageGatingState partialState = gate.evaluate(partial);
    ASSERT_EQ(partialState.decisions[0].status, StageViewStatus::Completed);
    ASSERT_EQ(partialState.decisions[1].status, StageViewStatus::NotStarted);

    const workflow::StageAdvice advice = advisor.adviseFor(
        StageId::Modeling, partialState, partial.snapshots[0],
        workflow::AdviceInputs{/*recovery*/{}, /*diagnostics*/{},
                               /*hasArchivedResult=*/true,
                               /*sessionReadOnly=*/false});  // 压制 R2（结果在）——隔离 R6 触发面
    ASSERT_FALSE(advice.steps.empty());
    EXPECT_EQ(advice.steps.front().actionKey, workflow::kActionAdvanceStage);
    EXPECT_EQ(advice.steps.front().targetStage, StageId::Requirements);
}

/**
 * R8 只读提示：只读会话 → readonly-notice（详情含门控解锁条件键）；
 * R3 等待/查看任务：hasActiveTask → review-active-task。
 */
TEST(WfAdvice, ReadonlyNoticeAndReviewTask_WP22T03_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-01"},
                  std::vector<std::string>{});  // 验证用例 WF-VER-108（R8/R3 规则）

    const workflow::PureStageGateService gate;
    const workflow::PureNextStepAdvisor advisor;

    // R8：只读会话（writable=false 会话事实经 AdviceInputs 传入）；
    // hasArchivedResult=true 压制 R2——隔离 R8 触发面。
    GateInputs inputs = makeReadyBoard(95);
    const StageGatingState state = gate.evaluate(inputs);
    workflow::AdviceInputs readonlyInputs;
    readonlyInputs.sessionReadOnly = true;
    readonlyInputs.hasArchivedResult = true;
    const workflow::StageAdvice readonly = advisor.adviseFor(
        StageId::Kinematics, state, inputs.snapshots[2], readonlyInputs);
    ASSERT_FALSE(readonly.steps.empty());
    EXPECT_EQ(readonly.steps.front().actionKey, workflow::kActionReadonlyNotice);
    // 详情键＝门控解锁条件提示（§6.3 R8 行"解锁条件提示 unlockHintKey"；
    // NotStarted 解锁态无解锁键——详情为空合法）。
    EXPECT_TRUE(readonly.steps.front().detailKeys.empty());

    // R3：trajectory 域在途任务 → review-active-task（独立于门控态）。
    ui::StageReadinessSnapshot taskSnapshot = makeReadySnapshot(
        StageId::TrajectoryDynamics, 95);
    for (ui::DomainReadinessItem& item : taskSnapshot.domains) {
        item.hasActiveTask = (item.domainKey == "trajectory");  // 在途任务
    }
    GateInputs taskInputs = makeReadyBoard(95);
    taskInputs.snapshots[3] = taskSnapshot;
    const StageGatingState taskState = gate.evaluate(taskInputs);
    EXPECT_EQ(taskState.decisions[3].status, StageViewStatus::InProgress)
        << "有活动任务＝门控 InProgress（§4.3 词表）";
    const workflow::StageAdvice taskAdvice = advisor.adviseFor(
        StageId::TrajectoryDynamics, taskState, taskSnapshot, {});
    ASSERT_FALSE(taskAdvice.steps.empty());
    EXPECT_EQ(taskAdvice.steps.front().actionKey,
              workflow::kActionReviewActiveTask);
}

// =====================================================================
// WF-VER-111：工程用语红线（UX-02——全产出键扫描）
// =====================================================================

/**
 * 七阶段 × 基线/缺项/失效三夹具的全部产出键（reasonKeys/unlockHintKey/
 * missingItemKeys/goalKey/problemKeys/titleKey/detailKeys/actionKey）扫描：
 * 零哈希形态/Schema 词/插件名/内部标识（键表扫描判据——UX-02）。
 */
TEST(WfAdvice, EngineeringWordingKeys_WP22T03_ACC2_UX02_WFVER111)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02"},
                  std::vector<std::string>{});  // 验证用例 WF-VER-111

    const workflow::PureStageGateService gate;
    const workflow::PureNextStepAdvisor advisor;

    for (StageId stage : ui::stageIdSequence()) {
        // 夹具 1：全就绪基线。
        GateInputs baseline = makeReadyBoard(100);
        const StageGatingState baselineState = gate.evaluate(baseline);
        expectAdviceWording(advisor.adviseFor(stage, baselineState,
                                              baseline.snapshots[static_cast<std::size_t>(stage)],
                                              {}),
                            "基线夹具");

        // 夹具 2：本阶段首域缺项（缺项键为域自报工程语义键——非哈希）。
        GateInputs missing = makeReadyBoard(101);
        setStageMissing(missing, stage, workflow::stageDomains(stage).front(),
                        {"stage.missing.sample-item"});
        const StageGatingState missingState = gate.evaluate(missing);
        expectAdviceWording(advisor.adviseFor(stage, missingState,
                                              missing.snapshots[static_cast<std::size_t>(stage)],
                                              {}),
                            "缺项夹具");
        for (const GateDecision& decision : missingState.decisions) {
            for (const ui::TextKey& key : decision.reasonKeys) {
                expectEngineeringWording(key, "门控 reasonKeys");
            }
            if (decision.unlockHintKey.has_value()) {
                expectEngineeringWording(*decision.unlockHintKey, "unlockHintKey");
            }
        }

        // 夹具 3：失效＋归档事件并存（results-stale 标记键＋建议 titleKey）。
        GateInputs stale = makeReadyBoard(102);
        stale.events = makeAllArchivedEvents();
        GateEvent invalidation;
        invalidation.kind = core::DomainEventKind::DependencyInvalidated;
        invalidation.domainKeys = workflow::stageDomains(stage);
        stale.events.push_back(invalidation);
        const StageGatingState staleState = gate.evaluate(stale);
        expectAdviceWording(advisor.adviseFor(stage, staleState,
                                              stale.snapshots[static_cast<std::size_t>(stage)],
                                              {}),
                            "失效夹具");
    }
}

// =====================================================================
// 词表与映射面（Types.hpp——§4.2 域↔阶段映射的直调面）
// =====================================================================

/**
 * §4.2 映射表逐键核对：八域键 → 阶段（trajectory/dynamics 同属聚合阶段）；
 * 词表外域名 → WorkflowError（fail-fast）；聚合域词表 TrajectoryDynamics
 * 两域（§4.2 聚合判定输入形状）。
 */
TEST(WfTypes, DomainStageMapping_WP22T03_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-12"},
                  std::vector<std::string>{});  // 验证用例 WF-VER-101（映射词表面）

    EXPECT_EQ(workflow::stageForDomain("modeling"), StageId::Modeling);
    EXPECT_EQ(workflow::stageForDomain("requirements"), StageId::Requirements);
    EXPECT_EQ(workflow::stageForDomain("kinematics"), StageId::Kinematics);
    EXPECT_EQ(workflow::stageForDomain("trajectory"), StageId::TrajectoryDynamics);
    EXPECT_EQ(workflow::stageForDomain("dynamics"), StageId::TrajectoryDynamics);
    EXPECT_EQ(workflow::stageForDomain("selection"), StageId::Selection);
    EXPECT_EQ(workflow::stageForDomain("optimization"), StageId::Optimization);
    EXPECT_EQ(workflow::stageForDomain("reporting"), StageId::Reporting);
    EXPECT_THROW(workflow::stageForDomain("nonexistent-domain"),
                 workflow::WorkflowError);

    EXPECT_EQ(workflow::stageDomains(StageId::TrajectoryDynamics).size(), 2U);
    EXPECT_EQ(workflow::stageDomains(StageId::Kinematics).size(), 1U);
}
