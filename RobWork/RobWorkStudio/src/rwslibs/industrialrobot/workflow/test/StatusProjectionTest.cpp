/**
 * @file   StatusProjectionTest.cpp
 * @brief  标题栏/状态栏与恢复横幅状态投影的模型测试（WP-22-T09——
 *         units/workflow.md §7.6 纯函数组装核的直调半区；WF-VER-217
 *         标题格式逐组合、PM-11 五路来源透传、PM-15 三场景择一与动作
 *         可用位）。
 *
 * 设计依据：
 *   - units/workflow.md §7.6（标题栏 `<显示名>[*][（只读）]`；状态栏＝
 *     项目名、当前方案、结果状态（是否过期）、未保存标记与只读后缀；
 *     恢复横幅三场景一句话汇总＋查看详情/恢复草稿/放弃）、§10.2/§10.3
 *     （IStatusProjectionProvider 接口面；纯函数面确定性 NFR-COR-02；
 *     调用方错误 fail-fast）、§11.2（WF-VER-217 观测点＝TitleStatusData
 *     ——各状态组合逐项正确；218 为契约承载）、P-UI-2（不可判定归入
 *     results-stale 呈现）
 *   - REQUIREMENTS.md §17 PM-11/PM-15 原文（格式串 `<显示名>[*][（只读）]`
 *     ＋三场景词形＋三动作——黄金串与黄金键的出处）、AT-21
 *   - 任务契约 tasks/foundation/WP-22-T09.json acceptance 1（标题栏/
 *     状态栏格式与状态来源用例）、acceptance 2（三场景＋三动作）、
 *     acceptance 3（未执行测试不得标注通过——本文件全部用例真实执行）
 *
 * 测试形态（§11.0——模型测试＝直调计算库纯函数面）：buildTitleText/
 * buildTitleStatus/resultsStatusLabelKey/buildRecoveryBanner/recovery-
 * SummaryKey 全部纯函数直调（黄金串取 PM-11 格式原文、黄金键取 ui UiText
 * 恢复横幅键族冻结串）；取数端口以脚本化桩承载（事实可编程——来源
 * 映射与接口消费路径的断言面）；真实落盘半区（三场景盘面注入＋统一
 * 诊断目录集成＋store 来源真实链）在契约测试 StatusBannerContractTest.cpp。
 *
 * 接口消费纪律（WP-20-T03 首轮漏检教训）：IStatusProjectionProvider 承诺
 * 的每个公共方法（titleStatus/recoveryBanner）至少各一条**经接口引用
 * 消费**的用例钉扎（Provider_* 组）——不留只测自由函数的接口盲区。
 *
 * 追溯约定（AGENTS.md §2.7）：用例名与断言注释标注需求/AT 编号。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记
#include <sdurws/ird/workflow/Projection.hpp>
#include <sdurws/ird/workflow/Types.hpp>  // WorkflowError（fail-fast 断言）

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace sdurws::ird;
using workflow::RecoveryBannerData;
using workflow::RecoveryFacts;
using workflow::RecoveryScenario;
using workflow::RecoveryScenarioFact;
using workflow::TitleFacts;
using workflow::TitleStatusData;
using workflow::WorkflowError;

// =====================================================================
// 黄金常量（需求原文词形与 ui 冻结键串——改词形须 REQUIREMENTS/ui 双向同步）
// =====================================================================

/// PM-11 冻结格式的只读后缀黄金词形（"（只读）"——需求原文格式串字面；
/// 测试作为调用方从 ui 文案表解析后传入 buildTitleText）。
constexpr const char* kReadonlySuffix = "（只读）";

/// 可写黄金项目显示名（黄金串半区——含多字节字符，钉 UTF-8 拼装）。
constexpr const char* kDisplayName = "焊接工作站A线";

/// 黄金方案名（活动分支 label——状态栏"当前方案"字段值）。
constexpr const char* kSchemeLabel = "主方案";

// =====================================================================
// 桩：标题事实端口（脚本化替身——事实可编程，取数次数可观测）
// =====================================================================

class TitlePortStub final : public workflow::ITitleFactPort {
public:
    TitleFacts facts;   ///< collectFacts() 返回值（事实脚本位）
    mutable int calls = 0;  ///< 取数次数（现取现组装断言——无缓存面）

    TitleFacts collectFacts() const override
    {
        ++calls;
        return facts;
    }
};

// =====================================================================
// 桩：恢复事实端口（脚本化替身——场景清单可编程）
// =====================================================================

class RecoveryPortStub final : public workflow::IRecoveryFactPort {
public:
    RecoveryFacts facts;  ///< collectFacts() 返回值（事实脚本位）
    int calls = 0;        ///< 取数次数

    RecoveryFacts collectFacts() override
    {
        ++calls;
        return facts;
    }
};

/// 当前性投影夹具（Superseded＋两条失效原因——搬运不计算的断言素材；
/// 字段语义锚 ui::CurrentnessProjection，词表对齐 evidence §8.1）。
ui::CurrentnessProjection supersededProjection()
{
    ui::CurrentnessProjection p;
    p.status = ui::CurrentnessProjection::Status::Superseded;
    ui::CurrentnessProjection::Reason r1;
    r1.dependencyKey = "kinematics/robot-parameter";
    r1.kindToken = "object-content-changed";
    r1.detail = "contentVersion a1b2… → c3d4…（对象内容变化）";
    ui::CurrentnessProjection::Reason r2;
    r2.dependencyKey = "trajectory/plan";
    r2.kindToken = "slice-content-changed";
    r2.detail = "切片整体内容变化（身份摘要兜底）";
    p.reasons = {r1, r2};
    return p;
}

/// 场景事实便捷构造（itemCount 恒 ≥1——端口契约）。
RecoveryScenarioFact fact(RecoveryScenario scenario, std::size_t count)
{
    RecoveryScenarioFact f;
    f.scenario = scenario;
    f.itemCount = count;
    return f;
}

// =====================================================================
// WF-VER-217：标题栏格式（PM-11 冻结格式——四种状态组合逐字黄金串）
// =====================================================================

/// 格式逐位组合：dirty×readOnly 全组合黄金串（`<显示名>[*][（只读）]`
/// ——PM-11 原文格式串；含多字节显示名，钉 UTF-8 逐位拼装）。
TEST(WfProjection, TitleText_FourCombinations_GoldenStrings_P11)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-11"}, std::vector<std::string>{});

    // 组合 1：可写＋已保存——纯显示名。
    TitleStatusData base;
    base.displayName = kDisplayName;
    base.readonlySuffixKey = workflow::kTitleReadonlySuffixKey;
    EXPECT_EQ(workflow::buildTitleText(base, kReadonlySuffix), kDisplayName);

    // 组合 2：可写＋未保存——追加 `*`（PM-11"未保存标记"位）。
    TitleStatusData dirty = base;
    dirty.dirty = true;
    EXPECT_EQ(workflow::buildTitleText(dirty, kReadonlySuffix),
              std::string(kDisplayName) + "*");

    // 组合 3：只读＋已保存——追加"（只读）"（PM-11"只读后缀"位）。
    TitleStatusData readonly_ = base;
    readonly_.readOnly = true;
    EXPECT_EQ(workflow::buildTitleText(readonly_, kReadonlySuffix),
              std::string(kDisplayName) + kReadonlySuffix);

    // 组合 4：只读＋未保存——`*` 在前、"（只读）"在后（格式序冻结）。
    TitleStatusData both = base;
    both.dirty = true;
    both.readOnly = true;
    EXPECT_EQ(workflow::buildTitleText(both, kReadonlySuffix),
              std::string(kDisplayName) + "*" + kReadonlySuffix);
}

/// 空显示名＝无项目会话——返回空串（宿主面不渲染标题状态，不虚构
/// 占位标题——§7.6 无项目态语义）。
TEST(WfProjection, TitleText_NoProject_EmptyString_P11)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-11"}, std::vector<std::string>{});

    TitleStatusData empty;
    empty.dirty = true;   // 即便携带脏标记——无显示名即无标题
    empty.readOnly = true;
    EXPECT_EQ(workflow::buildTitleText(empty, kReadonlySuffix), std::string{});
}

// =====================================================================
// PM-11：来源透传（TitleFacts → TitleStatusData——五路来源零加工）
// =====================================================================

/// 五路来源逐字段透传＋键半区填充（来源映射＝acceptance 1"状态来源"
/// 的断言面：显示名/方案/脏/只读位原样，当前性投影含逐条失效原因
/// 原样搬运——本单元零当前性计算）。
TEST(WfProjection, TitleStatus_FactsSourceMapping_P11)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-11"}, std::vector<std::string>{});

    TitleFacts facts;
    facts.displayName = kDisplayName;
    facts.writable = false;  // 只读会话（PM-07 降级——来源 store.writable()）
    facts.schemeLabel = kSchemeLabel;
    facts.anyDirty = true;
    facts.resultsCurrentness = supersededProjection();

    const TitleStatusData data = workflow::buildTitleStatus(facts);

    // 事实半区逐字段（readOnly＝writable 取反——PM-11 只读后缀位）。
    EXPECT_EQ(data.displayName, kDisplayName);
    EXPECT_TRUE(data.readOnly);
    EXPECT_EQ(data.schemeLabel, kSchemeLabel);
    EXPECT_TRUE(data.dirty);
    // 当前性投影搬运：status 与逐条失效原因三元组原样（零重算零删减
    // ——"过期附原因"呈现扩展的素材完整性，UX-10）。
    ASSERT_TRUE(data.resultsCurrentness.status.has_value());
    EXPECT_EQ(*data.resultsCurrentness.status,
              ui::CurrentnessProjection::Status::Superseded);
    ASSERT_EQ(data.resultsCurrentness.reasons.size(), 2u);
    EXPECT_EQ(data.resultsCurrentness.reasons[0].dependencyKey,
              "kinematics/robot-parameter");
    EXPECT_EQ(data.resultsCurrentness.reasons[1].detail,
              "切片整体内容变化（身份摘要兜底）");

    // 键半区：只读后缀/方案标签恒填充；结果状态键＝stale（见下组）。
    EXPECT_EQ(data.readonlySuffixKey, workflow::kTitleReadonlySuffixKey);
    EXPECT_EQ(data.schemeLabelKey, workflow::kStatusBarSchemeLabelKey);
    EXPECT_EQ(data.resultsStatusLabelKey, workflow::kResultsStatusLabelStaleKey);
}

// =====================================================================
// P-UI-2：结果状态选键（三态——不可判定归入过期呈现）
// =====================================================================

/// Current→当前键；Superseded→过期键；nullopt（不可判定）→过期键
/// （P-UI-2 冻结口径：判不了的结果绝不显示为"当前"——evidence"无默认
/// Current"的呈现面兑现）。
TEST(WfProjection, ResultsStatusLabelKey_ThreeStates_PUI2)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-11"}, std::vector<std::string>{});

    ui::CurrentnessProjection current;
    current.status = ui::CurrentnessProjection::Status::Current;
    EXPECT_EQ(workflow::resultsStatusLabelKey(current),
              workflow::kResultsStatusLabelCurrentKey);

    EXPECT_EQ(workflow::resultsStatusLabelKey(supersededProjection()),
              workflow::kResultsStatusLabelStaleKey);

    ui::CurrentnessProjection notEvaluable;  // status==nullopt（不可判定）
    notEvaluable.unevaluableCause = ui::NotEvaluableCause::UnresolvedDependency;
    EXPECT_EQ(workflow::resultsStatusLabelKey(notEvaluable),
              workflow::kResultsStatusLabelStaleKey);
}

// =====================================================================
// PM-15：恢复横幅三场景（逐场景黄金键／择一序／详情行／参数面）
// =====================================================================

/// 场景键映射黄金值（三值封闭词表逐值——键串与 ui UiText 恢复横幅键族
/// 同串同值，SA-12；封闭性＝三键齐全且互异）。
TEST(WfProjection, RecoverySummaryKey_WordTableGolden_P15)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-15"}, std::vector<std::string>{"AT-21"});

    EXPECT_EQ(workflow::recoverySummaryKey(RecoveryScenario::IgnoredUnfinishedSave),
              "ui.recovery.banner.summary.ignored-saves");
    EXPECT_EQ(workflow::recoverySummaryKey(RecoveryScenario::InterruptedTask),
              "ui.recovery.banner.summary.interrupted");
    EXPECT_EQ(workflow::recoverySummaryKey(RecoveryScenario::OrphanDraft),
              "ui.recovery.banner.summary.orphan-drafts");
}

/// 场景①单命中：主文案＝忽略保存键；无详情行无参数；恢复/放弃均不可用
/// （无草稿可恢复可放弃——动作位形稳定但禁用）。
TEST(WfProjection, Banner_ScenarioIgnoredSave_Alone_P15)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-15"}, std::vector<std::string>{"AT-21"});

    RecoveryFacts facts;
    facts.writable = true;
    facts.scenarios = {fact(RecoveryScenario::IgnoredUnfinishedSave, 2)};

    const std::optional<RecoveryBannerData> banner =
        workflow::buildRecoveryBanner(facts);
    ASSERT_TRUE(banner.has_value());
    EXPECT_EQ(banner->scenario, RecoveryScenario::IgnoredUnfinishedSave);
    EXPECT_EQ(banner->summaryKey, "ui.recovery.banner.summary.ignored-saves");
    EXPECT_TRUE(banner->summaryArgs.empty());  // 场景①键无计数占位
    EXPECT_EQ(banner->itemCount, 2u);
    EXPECT_TRUE(banner->detailKeys.empty());
    // 三动作键恒在场（位形稳定——PM-15 冻结三动作）＋黄金键值。
    EXPECT_EQ(banner->actionDetailsKey, "ui.recovery.banner.action.details");
    EXPECT_EQ(banner->actionRestoreKey, "ui.recovery.banner.action.restore");
    EXPECT_EQ(banner->actionDiscardKey, "ui.recovery.banner.action.discard");
    EXPECT_FALSE(banner->restoreAvailable);  // 无草稿——恢复禁用
    EXPECT_FALSE(banner->discardAvailable);  // 无草稿——放弃禁用
}

/// 场景②单命中：主文案＝中断任务键（"已中断可重跑"呈现的事实半区）。
TEST(WfProjection, Banner_ScenarioInterrupted_Alone_P15)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-15"}, std::vector<std::string>{"AT-21"});

    RecoveryFacts facts;
    facts.writable = true;
    facts.scenarios = {fact(RecoveryScenario::InterruptedTask, 1)};

    const std::optional<RecoveryBannerData> banner =
        workflow::buildRecoveryBanner(facts);
    ASSERT_TRUE(banner.has_value());
    EXPECT_EQ(banner->scenario, RecoveryScenario::InterruptedTask);
    EXPECT_EQ(banner->summaryKey, "ui.recovery.banner.summary.interrupted");
    EXPECT_TRUE(banner->summaryArgs.empty());
    EXPECT_TRUE(banner->detailKeys.empty());
    EXPECT_FALSE(banner->restoreAvailable);
    EXPECT_FALSE(banner->discardAvailable);
}

/// 场景③单命中（可写会话）：主文案＝孤儿草稿键＋{0} 计数参数；恢复与
/// 放弃双可用（有草稿可恢复可显式放弃）。
TEST(WfProjection, Banner_ScenarioOrphanDraft_Writable_BothActions_P15)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-15"}, std::vector<std::string>{"AT-21"});

    RecoveryFacts facts;
    facts.writable = true;
    facts.scenarios = {fact(RecoveryScenario::OrphanDraft, 3)};

    const std::optional<RecoveryBannerData> banner =
        workflow::buildRecoveryBanner(facts);
    ASSERT_TRUE(banner.has_value());
    EXPECT_EQ(banner->scenario, RecoveryScenario::OrphanDraft);
    EXPECT_EQ(banner->summaryKey, "ui.recovery.banner.summary.orphan-drafts");
    // {0} 位置参数＝计数文本（UX-02：计数是数值参数不是文案——经参数
    // 通道由 ui 呈现，不进键）。
    ASSERT_EQ(banner->summaryArgs.size(), 1u);
    EXPECT_EQ(banner->summaryArgs[0], "3");
    EXPECT_EQ(banner->itemCount, 3u);
    EXPECT_TRUE(banner->detailKeys.empty());
    EXPECT_TRUE(banner->restoreAvailable);   // 有草稿可恢复（载入查看非写操作）
    EXPECT_TRUE(banner->discardAvailable);   // 可写会话——显式 discard 可用
}

/// 三场景并存择一序（PM-15"一句话汇总"的核心编排语义）：主文案＝场景①
/// （行动紧迫度最高），场景②③按同序降级详情行——横幅不堆叠。
TEST(WfProjection, Banner_ThreeScenarios_PriorityOrderAndDetailFallback_P15)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-15"}, std::vector<std::string>{"AT-21"});

    // 清单故意按"最不重要在前"排列——择一由冻结紧迫度序决定，不依赖
    // 清单次序（确定性口径：同事实集同输出，与折叠序无关）。
    RecoveryFacts facts;
    facts.writable = true;
    facts.scenarios = {fact(RecoveryScenario::OrphanDraft, 1),
                       fact(RecoveryScenario::InterruptedTask, 2),
                       fact(RecoveryScenario::IgnoredUnfinishedSave, 1)};

    const std::optional<RecoveryBannerData> banner =
        workflow::buildRecoveryBanner(facts);
    ASSERT_TRUE(banner.has_value());
    EXPECT_EQ(banner->scenario, RecoveryScenario::IgnoredUnfinishedSave);
    EXPECT_EQ(banner->summaryKey, "ui.recovery.banner.summary.ignored-saves");
    // 详情行固定序＝紧迫度序（②中断在前、③草稿在后——NFR-COR-02）。
    ASSERT_EQ(banner->detailKeys.size(), 2u);
    EXPECT_EQ(banner->detailKeys[0], "ui.recovery.banner.summary.interrupted");
    EXPECT_EQ(banner->detailKeys[1], "ui.recovery.banner.summary.orphan-drafts");
    // 三场景并存时动作可用性仍只看场景③（有草稿可恢复/放弃）。
    EXPECT_TRUE(banner->restoreAvailable);
    EXPECT_TRUE(banner->discardAvailable);
}

/// 空事实集 → nullopt（无恢复事实＝横幅不渲染——不虚构恢复叙事，
/// PM-15 横幅只在有事实时呈现）。
TEST(WfProjection, Banner_EmptyFacts_NoBanner_P15)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-15"}, std::vector<std::string>{"AT-21"});

    RecoveryFacts facts;
    facts.writable = true;
    // scenarios 空＝三事实皆空。
    EXPECT_FALSE(workflow::buildRecoveryBanner(facts).has_value());
}

/// 只读会话禁放弃（ui §8.3-4 口径：放弃＝显式 discard＝写操作——只读
/// 会话入口禁用数据；恢复＝载入查看非写操作，仍可用）。
TEST(WfProjection, Banner_ReadOnly_DiscardDisabled_RestoreEnabled_P15)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-15"}, std::vector<std::string>{"AT-21"});

    RecoveryFacts facts;
    facts.writable = false;  // 只读会话（PM-07 降级/显式只读同语义）
    facts.scenarios = {fact(RecoveryScenario::OrphanDraft, 1)};

    const std::optional<RecoveryBannerData> banner =
        workflow::buildRecoveryBanner(facts);
    ASSERT_TRUE(banner.has_value());
    EXPECT_TRUE(banner->restoreAvailable);   // 恢复＝载入查看——只读不禁
    EXPECT_FALSE(banner->discardAvailable);  // 放弃＝写操作——只读禁用
}

/// 确定性重放（NFR-COR-02 同型）：同事实集双跑逐字段一致（含详情行序
/// 与参数面——投影值的确定性承诺）。
TEST(WfProjection, Banner_DeterministicReplay_NFRCOR02)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-02"}, std::vector<std::string>{});

    RecoveryFacts facts;
    facts.writable = true;
    facts.scenarios = {fact(RecoveryScenario::InterruptedTask, 1),
                       fact(RecoveryScenario::OrphanDraft, 4),
                       fact(RecoveryScenario::IgnoredUnfinishedSave, 1)};

    const std::optional<RecoveryBannerData> first =
        workflow::buildRecoveryBanner(facts);
    const std::optional<RecoveryBannerData> second =
        workflow::buildRecoveryBanner(facts);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(*first, *second);  // operator== 全字段（含 detailKeys 固定序）
}

// =====================================================================
// 调用方契约违约（fail-fast——§10.3 错误语义行）
// =====================================================================

/// 零计数场景入清单 → WorkflowError（调用方拼装违约——无事实场景入
/// 清单会让"一句话汇总"说出不存在的事实）。
TEST(WfProjection, Banner_ZeroCountScenario_FailFast)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-15"}, std::vector<std::string>{});

    RecoveryFacts facts;
    facts.scenarios = {fact(RecoveryScenario::InterruptedTask, 0)};
    EXPECT_THROW(workflow::buildRecoveryBanner(facts), WorkflowError);
}

/// 重复场景 → WorkflowError（折叠端口一次性输出契约——每场景至多一条）。
TEST(WfProjection, Banner_DuplicateScenario_FailFast)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-15"}, std::vector<std::string>{});

    RecoveryFacts facts;
    facts.scenarios = {fact(RecoveryScenario::OrphanDraft, 1),
                       fact(RecoveryScenario::OrphanDraft, 2)};
    EXPECT_THROW(workflow::buildRecoveryBanner(facts), WorkflowError);
}

// =====================================================================
// IStatusProjectionProvider 接口消费路径（WP-20-T03 教训——公共接口的
// 每个方法至少一条经接口引用消费的用例钉扎，不留接口盲区）
// =====================================================================

/// 经 IStatusProjectionProvider& 接口消费 titleStatus()（接口路径钉扎：
/// 虚派发到端口组合实现——titlePort 现取现组装，取数计数 ≥1 佐证无缓存）。
TEST(WfProjection, Provider_InterfaceTitleStatus_P11)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-11"}, std::vector<std::string>{});

    TitlePortStub titlePort;
    titlePort.facts.displayName = kDisplayName;
    titlePort.facts.anyDirty = true;
    RecoveryPortStub recoveryPort;  // 空事实——横幅腿在本组独立钉扎

    // 经基类引用消费（接口面——消费方持有 IStatusProjectionProvider&）。
    workflow::StatusProjectionProvider provider(titlePort, recoveryPort);
    workflow::IStatusProjectionProvider& iface = provider;
    const TitleStatusData data = iface.titleStatus();

    EXPECT_EQ(data.displayName, kDisplayName);
    EXPECT_TRUE(data.dirty);
    EXPECT_EQ(titlePort.calls, 1);  // 现取现组装（无缓存面——每次调用取数）
}

/// 经接口消费 recoveryBanner()：有事实 → 横幅值；空事实 → nullopt 直通
/// （"不渲染"语义经接口原样传达——编排核单点表达）。
TEST(WfProjection, Provider_InterfaceRecoveryBanner_P15)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-15"}, std::vector<std::string>{"AT-21"});

    TitlePortStub titlePort;
    RecoveryPortStub recoveryPort;
    recoveryPort.facts.writable = true;
    recoveryPort.facts.scenarios = {fact(RecoveryScenario::InterruptedTask, 1)};

    workflow::StatusProjectionProvider provider(titlePort, recoveryPort);
    workflow::IStatusProjectionProvider& iface = provider;

    const std::optional<RecoveryBannerData> banner = iface.recoveryBanner();
    ASSERT_TRUE(banner.has_value());
    EXPECT_EQ(banner->scenario, RecoveryScenario::InterruptedTask);
    EXPECT_EQ(recoveryPort.calls, 1);

    // 空事实腿：nullopt 直通（宿主面不渲染——不虚构横幅）。
    recoveryPort.facts.scenarios.clear();
    EXPECT_FALSE(iface.recoveryBanner().has_value());
}

}  // namespace
