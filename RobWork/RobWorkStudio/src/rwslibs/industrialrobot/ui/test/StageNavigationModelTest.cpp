/**
 * @file   StageNavigationModelTest.cpp
 * @brief  UI-T09 模型层用例（QCoreApplication 级——§12.1 第一层分工）：
 *         七阶段状态投影与门控注入矩阵（UI-STG-1）、阶段切换时序与就地
 *         拒绝数据（UI-STG-2）、StageStatusModel 单侧冻结汇聚（§6.5）、
 *         投影不拥有门控（N-11）与 UiText 工程用语体系（§3.5/§6.6）。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T09.json acceptance 1（UI-STG-1/2：
 *     六态投影矩阵＋blocked 附原因与缺项＋只读 ViewOnly＋requestNavigate
 *     允许/拒绝时序——拒绝不改 currentStage、就地提示原因＋缺项＋下一步
 *     建议）、acceptance 2（§6.5 汇聚单侧冻结形状 epoch 标注；投影只消费
 *     不拥有门控规则——N-11 无第二套；UX-02 一切文本经 UiText：数值带
 *     单位、零哈希/Schema/插件名）、acceptance 3（O-31：门控/域就绪经
 *     ui 自有端口以桩承载——"未产出→桩"口径；P-UI-6 单侧冻结形状；
 *     P-UI-1 词表未改一字——七态 token 断言对 §6.3 冻结稿）；
 *   - units/ui.md §6.4（阶段呈现状态表＋时序图）、§6.5（快照形状原文）、
 *     §10.2（接口契约表）、§12.3 UI-STG-1/2 行（前置＝"桩 workflow 门控
 *     输出/桩门控允许拒绝"、观测点＝"StageView 快照/观察者事件序"）、
 *     §3.5（文案键体系）、§6.6（数值带单位/不适用占位/零内部名）；
 *   - 先例：StatusWordModelTest.cpp 的桩注入＋IRD_TEST_INFO 追溯形态。
 *
 * 为什么门控/域就绪桩是"可控替身"而不是集成桩：O-31 裁决下 ui 产品面对
 * workflow/域插件零链接零 include——替身实现 ui 自有端口（IUiStageGate/
 * IUiDomainReadinessSource），L5 装配期才以适配器绑定对端（§3.1 测试
 * 以替身承载同款惯例）。替身同时充当"桩门控"的角色（WP-22-T03 未产出，
 * 契约卡行"未产出→桩"）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/core/Units.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include <sdurws/ird/ui/IStageNavigationModel.hpp>
#include <sdurws/ird/ui/UiProjections.hpp>
#include <sdurws/ird/ui/UiText.hpp>
#include <sdurws/ird/ui/UiTypes.hpp>

#include <array>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace sdurws::ird;
using ui::DomainReadinessItem;
using ui::IStageNavigationModel;
using ui::IStageViewObserver;
using ui::NavigateResult;
using ui::StageGateDecision;
using ui::StageGateView;
using ui::StageId;
using ui::StageNavigationModelDeps;
using ui::StageReadinessSnapshot;
using ui::StageView;
using ui::StageViewStatus;
using ui::StatusFacts;

// =====================================================================
// 桩门控（UI-STG-1/2 前置列"桩 workflow 门控"——可控替身，记录评估输入）
// =====================================================================

/**
 * @brief 可控门控替身：呈现态按阶段可编程；evaluate 记录收到的快照
 *        （§6.4 时序"门控评估（消费 StageStatusModel 汇聚投影）"的观测面）
 *        并回放预置判定。
 */
class StubStageGate final : public ui::IUiStageGate
{
public:
    /// 呈现输出表（下标＝static_cast<size_t>(StageId)；默认 not-started）。
    std::vector<StageGateView> views{7};

    /// evaluate 回放判定（默认拒绝且无数据——保守缺省）。
    StageGateDecision nextDecision;

    /// evaluate 观测：调用次数与最近收到的快照（数据流断言用；mutable——
    /// 端口方法为 const 查询，替身在只读方法内记录观测数据）。
    mutable int evaluateCalls = 0;
    mutable std::optional<StageReadinessSnapshot> lastSnapshot;

    StageGateView presentStage(StageId stage) const override
    {
        // 七阶段封闭词表内的下标访问（替身只被模型以合法值调用）。
        return views.at(static_cast<std::size_t>(stage));
    }

    StageGateDecision evaluate(const StageReadinessSnapshot& snapshot) const override
    {
        ++evaluateCalls;
        lastSnapshot = snapshot;  // 值拷贝留存——测试断言"门控看到了什么"
        return nextDecision;
    }
};

// =====================================================================
// 桩域就绪源（§6.5 汇聚输入替身——每源一域，按阶段返回只读投影）
// =====================================================================

class StubDomainSource final : public ui::IUiDomainReadinessSource
{
public:
    /// 阶段→域就绪项清单（缺省阶段＝空清单——该域在此阶段无投影）。
    std::map<StageId, std::vector<DomainReadinessItem>> itemsByStage;

    std::vector<DomainReadinessItem>
    domainReadiness(StageId stage) const override
    {
        const auto it = itemsByStage.find(stage);
        return it != itemsByStage.end() ? it->second : std::vector<DomainReadinessItem>{};
    }
};

/// 构造一个域就绪项（全部字段显式——单侧冻结形状逐字段断言用）。
DomainReadinessItem makeItem(const std::string& domainKey,
                             core::EngineeringStatus verdict, bool inputComplete,
                             std::vector<std::string> missingKeys, bool hasActiveTask)
{
    DomainReadinessItem item;
    item.domainKey = domainKey;
    item.verdict = verdict;
    item.inputComplete = inputComplete;
    item.missingItemKeys = std::move(missingKeys);
    item.hasActiveTask = hasActiveTask;
    return item;
}

// =====================================================================
// 会话事实替身（纪元/可写性/可导航性/每阶段七态源——模型装配依赖）
// =====================================================================

/// 可变捕获包（测试内模拟会话态演化——真实源归 UiProjectionStore/§5.2 会话态机）。
struct SessionFakes
{
    std::uint64_t epoch = 7;                 ///< 当前会话纪元（§6.2）
    bool writable = true;                    ///< 可写性（§5.5/§6.4 view-only 数据源）
    bool navigable = true;                   ///< 会话非 Opening/Closed（§10.2 前置条件）
    bool projectOpen = true;                 ///< 七态求值的会话位（§6.3 empty-project 触发）

    /// @brief 七态触发数据源缺省：computable 基线（就绪有效＋当前 Current）。
    std::optional<ui::StatusFacts> factsFor(StageId) const
    {
        ui::StatusFacts facts;
        facts.projectOpen = projectOpen;
        facts.readiness.valid = true;
        facts.currentness.status = ui::CurrentnessProjection::Status::Current;
        return facts;
    }
};

/// 组装依赖（全部必注入项接替身——装配契约 fail-fast 由缺依赖用例单独验证）。
StageNavigationModelDeps makeDeps(StubStageGate& gate, const SessionFakes& session)
{
    StageNavigationModelDeps deps;
    deps.gate = &gate;
    deps.epochSource = [&session] { return session.epoch; };
    deps.writableSource = [&session] { return session.writable; };
    deps.sessionNavigableSource = [&session] { return session.navigable; };
    deps.sevenStateFacts = [&session](StageId stage) { return session.factsFor(stage); };
    deps.initialStage = StageId::Modeling;
    return deps;
}

/// 记录型观察者（UI-STG-2 观测点"观察者事件序"——回调次数与回调时电流）。
class RecordingObserver final : public IStageViewObserver
{
public:
    int calls = 0;                                 ///< 收到通知的次数
    std::vector<StageId> currentAtCallback;        ///< 每次回调时拉取的 currentStage

    void onStageViewsChanged(const IStageNavigationModel& model) override
    {
        ++calls;
        currentAtCallback.push_back(model.currentStage());  // pull-on-event
    }
};

/// 构造"允许"判定。
StageGateDecision allowDecision()
{
    StageGateDecision decision;
    decision.allowed = true;
    return decision;
}

/// 构造"拒绝"判定（原因＋缺项＋解锁建议——就地提示三要素数据面）。
StageGateDecision rejectDecision(std::vector<std::string> reasonKeys,
                                 std::optional<std::string> unlockHint)
{
    StageGateDecision decision;
    decision.allowed = false;
    decision.reasonKeys = std::move(reasonKeys);
    decision.unlockHintKey = std::move(unlockHint);
    return decision;
}

// =====================================================================
// 用例组一：七阶段状态投影（UI-STG-1——契约 acceptance 1）
// =====================================================================

/**
 * UI-STG-1 主矩阵：stageViews 固定七项顺序，且与逐阶段注入的门控态一致。
 * 前置＝"遍历注入各门控态"（§12.3 行）；观测点＝StageView 快照。
 */
TEST(StageNavModel, StageViewsFixedOrderAndGateProjection_UISTG1_UI_T09_ACC1)
{
    IRD_TEST_INFO("UX-12", {}, std::nullopt);

    StubStageGate gate;
    SessionFakes session;
    // 遍历注入门控四态（§6.4 表门控可判定集合）：每态占一个阶段。
    gate.views.at(static_cast<std::size_t>(StageId::Modeling)).status = StageViewStatus::Completed;
    gate.views.at(static_cast<std::size_t>(StageId::Requirements)).status = StageViewStatus::Blocked;
    gate.views.at(static_cast<std::size_t>(StageId::Kinematics)).status = StageViewStatus::Unavailable;
    // TrajectoryDynamics 起保持默认 NotStarted（"可进入但从未访问"缺省）。

    StageNavigationModelDeps deps = makeDeps(gate, session);
    deps.initialStage = StageId::Reporting;  // 当前阶段移到末位——不遮蔽门控态核对
    auto model = ui::createStageNavigationModel(std::move(deps));
    const std::vector<StageView> views = model->stageViews();

    // 固定七项＋§6.4 UX-12 顺序（stageIdSequence 为序权威——模型与测试同源）。
    ASSERT_EQ(views.size(), std::size_t{7});
    const std::vector<StageId>& order = ui::stageIdSequence();
    ASSERT_EQ(order.size(), std::size_t{7});
    for (std::size_t i = 0; i < 7; ++i) {
        EXPECT_EQ(views[i].stage, order[i]) << "第 " << i << " 位阶段序漂移";
    }
    // 投影与注入一致（四态逐位核对＋缺省态核对）。
    EXPECT_EQ(views[0].status, StageViewStatus::Completed);      // 门控 completed
    EXPECT_EQ(views[1].status, StageViewStatus::Blocked);        // 门控 blocked
    EXPECT_EQ(views[2].status, StageViewStatus::Unavailable);    // 门控 unavailable
    EXPECT_EQ(views[3].status, StageViewStatus::NotStarted);     // 门控缺省
    // 用户所在位（Reporting）＝in-progress 会话合成（§6.4 表行 2）。
    EXPECT_EQ(model->currentStage(), StageId::Reporting);
    EXPECT_EQ(views[6].status, StageViewStatus::InProgress);
}

/**
 * blocked 行"附原因＋下一步建议"：blockingReasonKeys/nextStepKey 逐字
 * 透传（UX-01：建议文本由 workflow 提供，ui 只呈现——不生成不加工）；
 * 门控未提供建议时 nextStepKey 为 nullopt（不虚构）。
 */
TEST(StageNavModel, BlockedCarriesReasonMissingAndNextStep_UISTG1_UI_T09_ACC1)
{
    IRD_TEST_INFO("UX-01", {}, std::nullopt);

    StubStageGate gate;
    SessionFakes session;
    StageGateView& blocked = gate.views.at(static_cast<std::size_t>(StageId::Requirements));
    blocked.status = StageViewStatus::Blocked;
    blocked.blockingReasonKeys = {
        "stage.gate.prerequisite-incomplete.reason",
        "stage.missing.items.count",  // 缺项键与原因键同列——门控折叠产出
    };
    blocked.nextStepKey = "stage.next-step.complete-requirements.hint";

    auto model = ui::createStageNavigationModel(makeDeps(gate, session));
    const std::vector<StageView> views = model->stageViews();
    const StageView& view = views[1];  // requirements 位（§6.4 固定序第 2）

    EXPECT_EQ(view.status, StageViewStatus::Blocked);
    ASSERT_EQ(view.blockingReasonKeys.size(), std::size_t{2});
    EXPECT_EQ(view.blockingReasonKeys[0], "stage.gate.prerequisite-incomplete.reason");
    EXPECT_EQ(view.blockingReasonKeys[1], "stage.missing.items.count");
    ASSERT_TRUE(view.nextStepKey.has_value());
    EXPECT_EQ(*view.nextStepKey, "stage.next-step.complete-requirements.hint");

    // 对照面：门控未提供建议 → nullopt（呈现层不虚构建议，UX-01）。
    gate.views.at(static_cast<std::size_t>(StageId::Kinematics)).status = StageViewStatus::Blocked;
    gate.views.at(static_cast<std::size_t>(StageId::Kinematics)).nextStepKey = std::nullopt;
    EXPECT_FALSE(model->stageViews()[2].nextStepKey.has_value());
}

/**
 * 只读项目 ViewOnly（UI-STG-1 观测点原文）：writable=false 时全阶段
 * ViewOnly（§6.4 view-only 行数据源 writable=false；门控态与用户所在位
 * 都被覆盖——只读会话"可查看不可执行"，可执行性由 §5.5 命令层禁用承担）；
 * 门控的原因/建议键照常透传（投影不加工）。
 */
TEST(StageNavModel, ReadOnlyProjectAllStagesViewOnly_UISTG1_UI_T09_ACC1)
{
    IRD_TEST_INFO("UX-12", {}, std::nullopt);

    StubStageGate gate;
    SessionFakes session;
    session.writable = false;  // PM-07 只读打开
    gate.views.at(static_cast<std::size_t>(StageId::Modeling)).status = StageViewStatus::Completed;
    gate.views.at(static_cast<std::size_t>(StageId::Requirements)).status = StageViewStatus::Blocked;
    gate.views.at(static_cast<std::size_t>(StageId::Requirements)).blockingReasonKeys
        = {"stage.gate.prerequisite-incomplete.reason"};

    auto model = ui::createStageNavigationModel(makeDeps(gate, session));
    const std::vector<StageView> views = model->stageViews();
    for (const StageView& view : views) {
        EXPECT_EQ(view.status, StageViewStatus::ViewOnly) << "只读会话全阶段 ViewOnly";
    }
    // 只读不裁剪门控键（blocked 阶段的原因仍可见——查看语义保留）。
    EXPECT_EQ(views[1].blockingReasonKeys.size(), std::size_t{1});
}

/**
 * in-progress 行合成（§6.4 数据源＝currentStage 会话态）：可写会话中
 * 用户所在阶段呈 InProgress（覆盖门控缺省/其它态），其余阶段透传门控。
 */
TEST(StageNavModel, InProgressSynthesizedFromCurrentStage_UISTG1_UI_T09_ACC1)
{
    IRD_TEST_INFO("UX-12", {}, std::nullopt);

    StubStageGate gate;
    SessionFakes session;
    gate.views.at(static_cast<std::size_t>(StageId::Kinematics)).status = StageViewStatus::NotStarted;

    auto model = ui::createStageNavigationModel(makeDeps(gate, session));
    // 初始阶段 Modeling → InProgress；kinematics 保持门控 NotStarted。
    const std::vector<StageView> views = model->stageViews();
    EXPECT_EQ(views[0].status, StageViewStatus::InProgress);
    EXPECT_EQ(views[2].status, StageViewStatus::NotStarted);

    // 切换到 kinematics（放行）后 in-progress 随会话态迁移。
    gate.nextDecision = allowDecision();
    ASSERT_TRUE(model->requestNavigate(StageId::Kinematics).navigated());
    const std::vector<StageView> after = model->stageViews();
    EXPECT_EQ(after[0].status, StageViewStatus::NotStarted);     // 建模回到门控缺省态
    EXPECT_EQ(after[2].status, StageViewStatus::InProgress);     // 运动学＝用户所在
}

/**
 * 阶段七态投影（§6.3"七态按评估域/阶段分别投影"＋§10.2 sevenState）：
 * 每阶段取触发数据源经唯一求值实现点 evaluateStatusWord 求值、输出冻结
 * token（P-UI-1：词表未改一字）；无数据源阶段为 nullopt（不虚构状态词）。
 */
TEST(StageNavModel, SevenStateTokenPerStageFromFrozenEvaluator_UISTG1_UI_T09_ACC2)
{
    IRD_TEST_INFO("UX-10", {}, std::nullopt);

    StubStageGate gate;
    SessionFakes session;
    // 每阶段数据源可不同：kinematics 在途任务 → computing；requirements
    // 无数据源 → nullopt；其余走 computable 基线。
    StageNavigationModelDeps deps = makeDeps(gate, session);
    deps.sevenStateFacts = [&session](StageId stage) -> std::optional<ui::StatusFacts> {
        if (stage == StageId::Kinematics) {
            auto facts = *session.factsFor(stage);
            facts.tasks.activeStates = {core::TaskState::Running};
            return facts;
        }
        if (stage == StageId::Requirements) {
            return std::nullopt;  // 该阶段无七态数据源——呈现占位
        }
        return session.factsFor(stage);
    };
    auto model = ui::createStageNavigationModel(std::move(deps));
    const std::vector<StageView> views = model->stageViews();

    // computing 触发面 → §6.3 冻结 token（statusWordToken 是 token 唯一来源）。
    ASSERT_TRUE(views[2].sevenState.has_value());
    const ui::StatusFacts kinematicsFacts = [&session] {
        auto facts = *session.factsFor(StageId::Kinematics);
        facts.tasks.activeStates = {core::TaskState::Running};
        return facts;
    }();
    EXPECT_EQ(*views[2].sevenState,
              std::string(ui::statusWordToken(ui::evaluateStatusWord(kinematicsFacts).word)));
    EXPECT_EQ(*views[2].sevenState, "computing");

    // 无数据源 → nullopt；基线阶段 → computable。
    EXPECT_FALSE(views[1].sevenState.has_value());
    ASSERT_TRUE(views[0].sevenState.has_value());
    EXPECT_EQ(*views[0].sevenState, "computable");
}

/**
 * epoch 标注（契约 acceptance 2）：views 与汇聚快照携带纪元源当前值；
 * 纪元推进后下一次合成反映新值（§6.2 迟到投影由消费方按 epoch 丢弃）。
 */
TEST(StageNavModel, EpochAnnotatedOnViewsAndSnapshot_UI_T09_ACC2)
{
    IRD_TEST_INFO("UX-12", {}, std::nullopt);

    StubStageGate gate;
    SessionFakes session;
    session.epoch = 42;
    auto model = ui::createStageNavigationModel(makeDeps(gate, session));

    for (const StageView& view : model->stageViews()) {
        EXPECT_EQ(view.epoch, std::uint64_t{42});
    }
    EXPECT_EQ(model->readinessSnapshot(StageId::Kinematics).epoch, std::uint64_t{42});

    // 会话切换 epoch++（§6.2）——投影消费纪元源，新快照带新纪元。
    session.epoch = 43;
    for (const StageView& view : model->stageViews()) {
        EXPECT_EQ(view.epoch, std::uint64_t{43});
    }
}

// =====================================================================
// 用例组二：阶段切换时序（UI-STG-2——契约 acceptance 1）
// =====================================================================

/**
 * 允许支（§6.4 时序"允许"）：Allowed＋currentStage 更新＋观察者事件；
 * 事件序＝先更新后通知（回调内拉取的 currentStage 已是目标阶段——
 * 观察者事件序观测点）。
 */
TEST(StageNavModel, NavigateAllowedUpdatesStageAndNotifies_UISTG2_UI_T09_ACC1)
{
    IRD_TEST_INFO("UX-12", {}, std::nullopt);

    StubStageGate gate;
    SessionFakes session;
    gate.nextDecision = allowDecision();
    auto model = ui::createStageNavigationModel(makeDeps(gate, session));
    RecordingObserver observer;
    auto subscription = model->subscribe(observer);

    const NavigateResult result = model->requestNavigate(StageId::Kinematics);
    EXPECT_EQ(result.kind, NavigateResult::Kind::Allowed);
    EXPECT_EQ(result.stage, StageId::Kinematics);
    EXPECT_EQ(model->currentStage(), StageId::Kinematics);  // 会话态已切换
    // 面板切换事件（§6.4"允许：面板切换事件"——模型层观测＝观察者通知）。
    ASSERT_EQ(observer.calls, 1);
    EXPECT_EQ(observer.currentAtCallback.back(), StageId::Kinematics);
}

/**
 * 拒绝支（§6.4 时序"拒绝"）：currentStage 不变、无事件；结果原样携带
 * 门控判定的原因＋缺项键与解锁/下一步建议——就地提示三要素（acceptance 1：
 * 拒绝不改变 currentStage、就地提示原因＋缺项＋下一步建议）。
 */
TEST(StageNavModel, NavigateRejectedKeepsStageAndCarriesPrompt_UISTG2_UI_T09_ACC1)
{
    IRD_TEST_INFO("UX-12", {}, std::nullopt);

    StubStageGate gate;
    SessionFakes session;
    gate.nextDecision = rejectDecision(
        {"stage.gate.prerequisite-incomplete.reason",
         "stage.missing.items.count"},
        std::optional<std::string>{"stage.next-step.complete-requirements.hint"});
    auto model = ui::createStageNavigationModel(makeDeps(gate, session));
    RecordingObserver observer;
    auto subscription = model->subscribe(observer);

    const NavigateResult result = model->requestNavigate(StageId::Kinematics);
    EXPECT_EQ(result.kind, NavigateResult::Kind::Rejected);
    EXPECT_EQ(model->currentStage(), StageId::Modeling);   // currentStage 不变
    EXPECT_EQ(observer.calls, 0);                          // 无面板切换事件
    // 就地提示数据逐字透传（模型零加工——N-11：ui 不改写门控判定）。
    ASSERT_EQ(result.reasonKeys.size(), std::size_t{2});
    EXPECT_EQ(result.reasonKeys[0], "stage.gate.prerequisite-incomplete.reason");
    EXPECT_EQ(result.reasonKeys[1], "stage.missing.items.count");
    ASSERT_TRUE(result.unlockHintKey.has_value());
    EXPECT_EQ(*result.unlockHintKey, "stage.next-step.complete-requirements.hint");
    EXPECT_FALSE(result.navigated());
}

/**
 * 只读导航（§6.4 补充规则"阶段导航全部可点击查看"）：RejectedReadOnly
 * 路径——导航生效（currentStage 更新＋事件，消费方装配只读面板），但
 * **不经门控评估**（门控判定的是进入执行的准入；只读浏览不构成解锁，
 * 可执行性由 §5.5 命令层禁用承担——N-11 不受影响）。
 */
TEST(StageNavModel, ReadOnlyNavigateIsViewOnlyPath_UISTG2_UI_T09_ACC1)
{
    IRD_TEST_INFO("UX-12", {}, std::nullopt);

    StubStageGate gate;
    SessionFakes session;
    session.writable = false;
    auto model = ui::createStageNavigationModel(makeDeps(gate, session));
    RecordingObserver observer;
    auto subscription = model->subscribe(observer);

    const NavigateResult result = model->requestNavigate(StageId::Reporting);
    EXPECT_EQ(result.kind, NavigateResult::Kind::RejectedReadOnly);
    EXPECT_EQ(result.stage, StageId::Reporting);
    EXPECT_EQ(model->currentStage(), StageId::Reporting);  // 查看路径照常切换
    ASSERT_EQ(observer.calls, 1);                          // 面板切换事件照常
    EXPECT_EQ(gate.evaluateCalls, 0);                      // 门控未被咨询
}

/**
 * 前置条件违约（§10.2 契约表"前置条件"行：requestNavigate 需会话非
 * Opening/Closed）——调用方错误 fail-fast 抛 std::logic_error，会话态
 * 不变、无事件（不把装配/时序缺陷伪装成业务拒绝）。
 */
TEST(StageNavModel, SessionPreconditionFailsFast_UI_T09_ACC1)
{
    IRD_TEST_INFO("UX-12", {}, std::nullopt);

    StubStageGate gate;
    SessionFakes session;
    session.navigable = false;  // Opening/Closed 会话
    gate.nextDecision = allowDecision();
    auto model = ui::createStageNavigationModel(makeDeps(gate, session));
    RecordingObserver observer;
    auto subscription = model->subscribe(observer);

    EXPECT_THROW(model->requestNavigate(StageId::Kinematics), std::logic_error);
    EXPECT_EQ(model->currentStage(), StageId::Modeling);
    EXPECT_EQ(observer.calls, 0);
    EXPECT_EQ(gate.evaluateCalls, 0);  // 前置自检先于门控咨询
}

/**
 * 幂等导航：目标==当前 → Allowed 短路返回，不重评估门控、不发事件
 * （状态未变——重复评估徒增门控调用且事件语义失真）。
 */
TEST(StageNavModel, NavigateToCurrentStageIdempotent_UI_T09_ACC1)
{
    IRD_TEST_INFO("UX-12", {}, std::nullopt);

    StubStageGate gate;
    SessionFakes session;
    gate.nextDecision = rejectDecision({"any.reason"}, std::nullopt);
    auto model = ui::createStageNavigationModel(makeDeps(gate, session));
    RecordingObserver observer;
    auto subscription = model->subscribe(observer);

    const NavigateResult result = model->requestNavigate(StageId::Modeling);
    EXPECT_EQ(result.kind, NavigateResult::Kind::Allowed);
    EXPECT_EQ(gate.evaluateCalls, 0);   // 未咨询门控
    EXPECT_EQ(observer.calls, 0);       // 未发事件
    EXPECT_EQ(model->currentStage(), StageId::Modeling);
}

/**
 * 订阅纪律（§10.2"观察者弱引用，退订幂等"）：退订后不再通知；重复
 * 退订幂等不崩；其余观察者照常收通知。
 */
TEST(StageNavModel, UnsubscribeStopsNotificationsAndIsIdempotent_UISTG2_UI_T09_ACC1)
{
    IRD_TEST_INFO("UX-12", {}, std::nullopt);

    StubStageGate gate;
    SessionFakes session;
    gate.nextDecision = allowDecision();
    auto model = ui::createStageNavigationModel(makeDeps(gate, session));
    RecordingObserver first;
    RecordingObserver second;
    auto firstHandle = model->subscribe(first);
    auto secondHandle = model->subscribe(second);

    firstHandle->unsubscribe();
    firstHandle->unsubscribe();  // 重复退订幂等

    ASSERT_TRUE(model->requestNavigate(StageId::Kinematics).navigated());
    EXPECT_EQ(first.calls, 0);   // 退订后不再投递
    EXPECT_EQ(second.calls, 1);  // 其余观察者照常

    // RAII 兜底：句柄析构＝退订（secondHandle 离开作用域后无泄漏通知面）。
    secondHandle.reset();
    ASSERT_TRUE(model->requestNavigate(StageId::Selection).navigated());
    EXPECT_EQ(second.calls, 1);
}

// =====================================================================
// 用例组三：StageStatusModel 汇聚与投影不拥有门控（§6.5/N-11——acceptance 2）
// =====================================================================

/**
 * §6.5 单侧冻结形状：快照域项按注册序稳定拼接（NFR-COR-02）、字段逐字
 * 透传（domainKey/verdict/inputComplete/missingItemKeys/hasActiveTask）、
 * epoch 标注；无注册域的阶段快照 domains 为空（不虚构就绪事实）。
 */
TEST(StageNavModel, ReadySnapshotAggregatesInRegistrationOrder_UI_T09_ACC2)
{
    IRD_TEST_INFO("UX-12", {}, std::nullopt);

    StubStageGate gate;
    SessionFakes session;
    StubDomainSource kinematicsSource;
    kinematicsSource.itemsByStage[StageId::Kinematics] = {
        makeItem("kinematics", core::EngineeringStatus::Feasible, true, {}, false),
    };
    StubDomainSource trajectorySource;
    trajectorySource.itemsByStage[StageId::Kinematics] = {
        makeItem("trajectory", core::EngineeringStatus::NotApplicable, false,
                 {"stage.trajectory.missing.waypoints.item"}, true),
        makeItem("dynamics", core::EngineeringStatus::DataInsufficient, false,
                 {"stage.dynamics.missing.payload.item"}, false),
    };
    StageNavigationModelDeps deps = makeDeps(gate, session);
    deps.domainSources = {&kinematicsSource, &trajectorySource};  // 注册序
    auto model = ui::createStageNavigationModel(std::move(deps));

    const StageReadinessSnapshot snapshot = model->readinessSnapshot(StageId::Kinematics);
    EXPECT_EQ(snapshot.stage, StageId::Kinematics);
    EXPECT_EQ(snapshot.epoch, std::uint64_t{7});  // SessionFakes 缺省纪元
    // 汇聚序＝注册序（跨源先 kinematicsSource 后 trajectorySource）。
    ASSERT_EQ(snapshot.domains.size(), std::size_t{3});
    EXPECT_EQ(snapshot.domains[0].domainKey, "kinematics");
    EXPECT_EQ(snapshot.domains[1].domainKey, "trajectory");
    EXPECT_EQ(snapshot.domains[2].domainKey, "dynamics");
    // 字段逐字透传（汇聚只搬运——§6.5 红线）。
    EXPECT_EQ(snapshot.domains[0].verdict, core::EngineeringStatus::Feasible);
    EXPECT_TRUE(snapshot.domains[0].inputComplete);
    EXPECT_FALSE(snapshot.domains[1].inputComplete);
    ASSERT_EQ(snapshot.domains[1].missingItemKeys.size(), std::size_t{1});
    EXPECT_EQ(snapshot.domains[1].missingItemKeys[0],
              "stage.trajectory.missing.waypoints.item");
    EXPECT_TRUE(snapshot.domains[1].hasActiveTask);
    EXPECT_EQ(snapshot.domains[2].verdict, core::EngineeringStatus::DataInsufficient);

    // 未注册域的阶段 → 空清单（不虚构）。
    EXPECT_TRUE(model->readinessSnapshot(StageId::Reporting).domains.empty());
}

/**
 * §6.4 时序数据流实证："门控评估（消费 ui 的 StageStatusModel 汇聚投影）"
 * ——requestNavigate 传给门控的快照＝域源数据＋当前纪元（替身记录面）。
 */
TEST(StageNavModel, GateReceivesAggregatedSnapshot_UISTG2_UI_T09_ACC2)
{
    IRD_TEST_INFO("UX-12", {}, std::nullopt);

    StubStageGate gate;
    SessionFakes session;
    gate.nextDecision = rejectDecision({}, std::nullopt);
    StubDomainSource source;
    source.itemsByStage[StageId::Kinematics] = {
        makeItem("kinematics", core::EngineeringStatus::NotApplicable, false,
                 {"stage.kinematics.missing.model.item"}, false),
    };
    StageNavigationModelDeps deps = makeDeps(gate, session);
    deps.domainSources = {&source};
    auto model = ui::createStageNavigationModel(std::move(deps));

    (void)model->requestNavigate(StageId::Kinematics);
    ASSERT_EQ(gate.evaluateCalls, 1);
    ASSERT_TRUE(gate.lastSnapshot.has_value());
    EXPECT_EQ(gate.lastSnapshot->stage, StageId::Kinematics);
    EXPECT_EQ(gate.lastSnapshot->epoch, std::uint64_t{7});
    ASSERT_EQ(gate.lastSnapshot->domains.size(), std::size_t{1});
    EXPECT_EQ(gate.lastSnapshot->domains[0].domainKey, "kinematics");
    ASSERT_EQ(gate.lastSnapshot->domains[0].missingItemKeys.size(), std::size_t{1});
    EXPECT_EQ(gate.lastSnapshot->domains[0].missingItemKeys[0],
              "stage.kinematics.missing.model.item");
}

/**
 * N-11 红线的装配面：未注入门控端口的模型不得存在（缺门控会把"无判定"
 * 伪装成"全部放行"＝第二套门控语义）——工厂 fail-fast 抛 std::invalid_argument。
 */
TEST(StageNavModel, MissingGateDependencyFailsFast_N11_UI_T09_ACC2)
{
    IRD_TEST_INFO("UX-12", {}, std::nullopt);

    StageNavigationModelDeps deps;  // 全缺省——gate 等必注入项均为空
    EXPECT_THROW(ui::createStageNavigationModel(std::move(deps)), std::invalid_argument);

    // 会话事实源缺失同样 fail-fast（构造函数装配自检清单）。
    StubStageGate gate;
    SessionFakes session;
    StageNavigationModelDeps partial = makeDeps(gate, session);
    partial.sevenStateFacts = nullptr;
    EXPECT_THROW(ui::createStageNavigationModel(std::move(partial)), std::invalid_argument);
}

// =====================================================================
// 用例组四：UiText 工程用语体系（§3.5/§6.6——acceptance 2，UX-02）
// =====================================================================

/**
 * 阶段标题键解析（§3.5 "stage.<id>.title" 族）：七键逐一解析为 §6.4
 * 括注中文名；token 段经 stageToken 唯一映射（调用方不手写 token）；
 * 未登记键 fail-fast（不回显键名/不空串——静默降级会把缺陷渲染给用户）。
 */
TEST(UiText, StageTitleKeysResolveAndUnknownKeyFailsFast_UI_T09_ACC2)
{
    IRD_TEST_INFO("UX-02", {}, std::nullopt);

    EXPECT_EQ(ui::resolveText(std::string("stage.") + ui::stageToken(StageId::Modeling) + ".title"), "建模");
    EXPECT_EQ(ui::resolveText(std::string("stage.") + ui::stageToken(StageId::Requirements) + ".title"), "需求");
    EXPECT_EQ(ui::resolveText(std::string("stage.") + ui::stageToken(StageId::Kinematics) + ".title"), "运动学");
    EXPECT_EQ(ui::resolveText(std::string("stage.") + ui::stageToken(StageId::TrajectoryDynamics) + ".title"),
              "轨迹/动力学");
    EXPECT_EQ(ui::resolveText(std::string("stage.") + ui::stageToken(StageId::Selection) + ".title"), "选型");
    EXPECT_EQ(ui::resolveText(std::string("stage.") + ui::stageToken(StageId::Optimization) + ".title"), "优化");
    EXPECT_EQ(ui::resolveText(std::string("stage.") + ui::stageToken(StageId::Reporting) + ".title"), "报告");

    // 未登记键＝调用方契约违约（fail-fast——§3.5 唯一出口的显性失败）。
    EXPECT_THROW(ui::resolveText("stage.no-such-stage.title"), std::invalid_argument);
}

/**
 * 参数替换纪律：位置占位 {0} 替换；引用越界/参数冗余/参数含摘要形态
 * 各自 fail-fast（替换纪律见 UiText.hpp 契约注释）。
 */
TEST(UiText, ArgSubstitutionDiscipline_UI_T09_ACC2)
{
    IRD_TEST_INFO("UX-02", {}, std::nullopt);

    // 正常替换：缺项计数摘要行（{0}＝计数）。
    EXPECT_EQ(ui::resolveText("stage.missing.items.count", {"3"}), "缺项 3 项");

    // 占位符引用越界（文本引用 {0} 但调用方零参数）。
    EXPECT_THROW(ui::resolveText("stage.missing.items.count", {}), std::invalid_argument);
    // 参数冗余（调用方多传——参数面与文案不齐）。
    EXPECT_THROW(ui::resolveText("stage.missing.items.count", {"3", "4"}),
                 std::invalid_argument);
    // 参数含 64 位十六进制摘要形态（UX-02：内容身份绝不进用户可见文本）。
    const std::string digest(64, 'a');  // 64 个十六进制字符＝SHA-256 呈现形态
    EXPECT_THROW(ui::resolveText("stage.missing.items.count", {digest}),
                 std::invalid_argument);
    // 同形态经独立守卫同样拦截（呈现组装点的防御性自检入口）。
    EXPECT_THROW(ui::ensureNoInternalIdentity("对象 " + digest + " 的摘要"),
                 std::invalid_argument);
    EXPECT_NO_THROW(ui::ensureNoInternalIdentity("0123456789abcdef"));  // 短十六进制合法
}

/**
 * 数值带单位显示（§6.6"参数中的数值一律带单位显示——core
 * Quantity::displayValueIn 唯一换算入口"）：换算正确、单位同显、量纲
 * 违约抛 core 错误（ui 不吞不改 core 错误语义）。
 */
TEST(UiText, QuantityTextWithUnit_UI_T09_ACC2)
{
    IRD_TEST_INFO("UX-02", {}, std::nullopt);

    const core::Length stroke = core::Length::fromSi(0.0254);  // SI 真值 m
    const auto mm = core::UnitToken::find("mm");
    const auto m = core::UnitToken::find("m");
    ASSERT_TRUE(mm.has_value());
    ASSERT_TRUE(m.has_value());
    // 显示投影：mm 制式 25.4、m 制式 0.0254（同一 SI 真值——KIN-12）。
    EXPECT_EQ(ui::formatQuantityText(stroke, *mm), "25.4 mm");
    EXPECT_EQ(ui::formatQuantityText(stroke, *m), "0.0254 m");

    // 量纲违约：把转矩单位喂给长度量（装配错误——core CoreError 上抛）。
    const auto torqueUnit = core::UnitToken::find("N*m");
    ASSERT_TRUE(torqueUnit.has_value());
    EXPECT_THROW(ui::formatQuantityText(stroke, *torqueUnit), core::CoreError);
}

/**
 * "不适用"占位（§6.6/ERR-01：不适用字段显示"不适用"，不伪造 0）。
 */
TEST(UiText, NotApplicablePlaceholder_UI_T09_ACC2)
{
    IRD_TEST_INFO("UX-02", {}, std::nullopt);
    EXPECT_EQ(ui::notApplicableText(), "不适用");
}

/**
 * 零内部名断言（UI-STG-1 验收要点"界面零哈希/Schema/插件名"的表面
 * 执行点）：内建文案表全表扫描——键解析值零 64 位十六进制摘要、零
 * Schema 字样、零内部命名空间/目标名词形（UX-02）。
 */
TEST(UiText, BuiltinTableZeroInternalIdentity_UISTG1_UI_T09_ACC2)
{
    IRD_TEST_INFO("UX-02", {}, std::nullopt);

    const std::vector<ui::TextKey> keys = ui::registeredTextKeys();
    ASSERT_FALSE(keys.empty());
    for (const ui::TextKey& key : keys) {
        // 键可解析（盘点面与查找面一致——登记即解析）。
        const std::string text = ui::resolveText(key);
        // 零哈希：任何 64 连续十六进制段都是摘要呈现形态（CON-05）。
        EXPECT_NO_THROW(ui::ensureNoInternalIdentity(text))
            << "键 " << key << " 的文本含摘要形态";
        // 零 Schema 版本字样（大小写两形——呈现文案不应出现）。
        EXPECT_EQ(text.find("schema"), std::string::npos) << "键 " << key;
        EXPECT_EQ(text.find("Schema"), std::string::npos) << "键 " << key;
        // 零内部插件名/框架内部标识词形（sdurws 目标族/ird 命名空间）。
        EXPECT_EQ(text.find("sdurws"), std::string::npos) << "键 " << key;
        EXPECT_EQ(text.find("ird::"), std::string::npos) << "键 " << key;
    }
}

/**
 * 过渡标签值源切换回归（acceptance 3——P-UI-1 词表未改一字）：三个过渡
 * 标签函数的值经 UiText 解析且与键一一对应（UI-T04 钉住的中文值逐字不变
 * ——键不变、值同源迁移至 UiText 内建表，"一切文本经 UiText::resolve"）。
 */
TEST(UiText, TransitionalLabelsRouteThroughUiText_UI_T09_ACC2)
{
    IRD_TEST_INFO("UX-10", {}, std::nullopt);

    // 七态短标签：值与 UiText 解析同源（§6.3"中文"列——全七值逐一核对）。
    const std::array<ui::StatusWord, 7> allWords{
        ui::StatusWord::EmptyProject,     ui::StatusWord::Incomplete,
        ui::StatusWord::Computing,        ui::StatusWord::ResultsStale,
        ui::StatusWord::DataInsufficient, ui::StatusWord::Failed,
        ui::StatusWord::Computable,
    };
    for (const ui::StatusWord word : allWords) {
        EXPECT_EQ(ui::statusWordTransitionalLabel(word),
                  ui::resolveText(ui::statusWordLabelKey(word)))
            << "七态值源漂移：token=" << ui::statusWordToken(word);
    }
    // 逐字钉住两例（§6.3 词表中文列原文——值迁移零漂移的直观锚）。
    EXPECT_EQ(ui::statusWordTransitionalLabel(ui::StatusWord::EmptyProject), "空项目");
    EXPECT_EQ(ui::statusWordTransitionalLabel(ui::StatusWord::Computable), "可计算");

    // 九态短标签（PM-03——§6.3 九态短标签行原文）。
    const std::array<core::TaskState, 9> allStates{
        core::TaskState::Queued,   core::TaskState::Preparing,
        core::TaskState::Running,  core::TaskState::Paused,
        core::TaskState::Canceling, core::TaskState::Canceled,
        core::TaskState::Completed, core::TaskState::Failed,
        core::TaskState::Interrupted,
    };
    for (const core::TaskState state : allStates) {
        EXPECT_EQ(ui::taskStateTransitionalLabel(state),
                  ui::resolveText(ui::taskStateLabelKey(state)))
            << "九态值源漂移";
    }
    EXPECT_EQ(ui::taskStateTransitionalLabel(core::TaskState::Queued), "排队中");

    // 当前性「无法判定」原因两键（P-UI-2 建议口径原文）。
    for (const ui::NotEvaluableCause cause :
         {ui::NotEvaluableCause::CrossContext,
          ui::NotEvaluableCause::UnresolvedDependency}) {
        EXPECT_EQ(ui::currentnessUnevaluableTransitionalLabel(cause),
                  ui::resolveText(ui::currentnessUnevaluableLabelKey(cause)));
    }
    EXPECT_EQ(ui::currentnessUnevaluableTransitionalLabel(
                  ui::NotEvaluableCause::UnresolvedDependency),
              "当前性无法判定（依赖缺失）");
}

}  // namespace
