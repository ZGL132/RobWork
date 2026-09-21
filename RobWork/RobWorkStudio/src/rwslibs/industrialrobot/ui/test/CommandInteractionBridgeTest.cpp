/**
 * @file   CommandInteractionBridgeTest.cpp
 * @brief  UI-T13 模型层用例（QCoreApplication 级——§12.1 第一层分工）：
 *         命令确认交互桥 §9.2/§9.3 的落位自证——对话装配映射（比较型
 *         三要素/作用对象/选项键/Dev 折叠区——UI-CMD-4/5 数据面）、批量
 *         决议三态（确认→凭据/拒绝→nullopt）、principal 会话采集缓存
 *         （P-UI-4）、会话拆除唤醒（interaction-lost——UI-CMD-7）、输入
 *         变化失效标注（UI-CMD-6）、无超时阻塞等待（P-DIAG-6）、装配
 *         fail-fast（SA-15——ui 只交 credentials）。
 *
 * 线程编排说明（§9.2 时序图的测试投影）：测试主线程＝UI 线程（functor
 * 在此执行）；显式 std::thread 充当命令执行线程。挂起形态的呈现器在
 * **嵌套事件泵**中等待（同真实模态对话的 exec 循环——UI 线程保持响应），
 * 拆除/释放由旁路线程触发（markSessionDismantled 为 noexcept 原子面）。
 * 不使用固定 sleep 判据——全部用原子标志位＋事件泵送自旋上限。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T13.json acceptance 2/5（UI-CMD-4~7
 *     ＋O-31/P-PR-7/P-UI-4 处置）；§12.3 UI-CMD-4~7 行的模型半区观测点
 *     （端到端命令流归 UI-T14 契约测试套件与 gui 层）；
 *   - units/ui.md §9.2 全节（时序图＋规则明细表）、§9.3（对话数据契约）、
 *     §3.4（线程模型——命令执行线程行＋M-1）。
 */

#include <gtest/gtest.h>

#include <QCoreApplication>

#include <sdurws/ird/diagnostics/Catalog.hpp>
#include <sdurws/ird/diagnostics/Confirmable.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include <sdurws/ird/ui/CommandInteractionBridge.hpp>
#include <sdurws/ird/ui/UiPorts.hpp>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace sdurws::ird;
using ui::CommandInteractionBridge;
using ui::ConfirmDialogData;
using ui::ConfirmDialogOutcome;
using ui::ConfirmDialogResolution;

// =====================================================================
// 测试替身（§3.1"ui 测试以可控替身承载"——呈现器/时钟/补全/principal）
// =====================================================================

/// 手动时钟（confirmedAtUtc 注入——确定性断言）。
class ManualDiagClock final : public diagnostics::IClock {
public:
    std::chrono::system_clock::time_point nowUtc() const override { return m_now; }
    std::chrono::system_clock::time_point m_now{std::chrono::seconds{1234567}};
};

/**
 * @brief 确认对话呈现器替身（可编程决议＋进入计数＋可挂起）。
 *
 * hold 模式＝"对话打开中"形态：在嵌套事件泵内等待 released（同真实
 * 模态对话 exec 循环——UI 线程保持事件响应）；holdSpins 记录挂起泵送
 * 轮数（"无自动超时"用例的等待时长观测面）。
 */
class StubPresenter final : public ui::IUiConfirmDialogPresenter {
public:
    ConfirmDialogOutcome programmed = ConfirmDialogOutcome::Confirmed;  ///< 决议（可编程）
    std::vector<ConfirmDialogData> seen;                                ///< 收到的装配值（捕获断言）
    std::atomic<int> entered{0};                                        ///< 对话打开次数
    std::atomic<bool> hold{false};                                      ///< 挂起位（hold=true 时嵌套泵送等待）
    std::atomic<bool> released{false};                                  ///< 释放位
    std::atomic<int> holdSpins{0};                                      ///< 挂起泵送轮数（等待时长观测面）
    std::atomic<int> inputChangedNotes{0};                              ///< noteInputChanged 次数

    ConfirmDialogResolution showConfirmDialog(const ConfirmDialogData& data) override
    {
        entered.fetch_add(1);
        seen.push_back(data);
        // 挂起模式：嵌套泵送等待释放（真实模态对话的 exec 循环投影）。
        while (hold.load() && !released.load()) {
            QCoreApplication::processEvents();
            holdSpins.fetch_add(1);
            std::this_thread::yield();
        }
        ConfirmDialogResolution resolution;
        resolution.outcome = released.load() ? ConfirmDialogOutcome::Abandoned
                                             : programmed;
        return resolution;
    }

    void noteInputChanged() override { inputChangedNotes.fetch_add(1); }
};

// =====================================================================
// 测试环境装配
// =====================================================================

struct BridgeHarness {
    std::shared_ptr<StubPresenter> presenter = std::make_shared<StubPresenter>();
    ManualDiagClock clock;
    std::unique_ptr<CommandInteractionBridge> bridge;

    /// 可比较型发现（C-1：comparison 必在——工厂校验）。
    static core::ConfirmableFinding makeFinding(const char* code)
    {
        core::ComparativeFields comparison;
        comparison.actual.quantity = core::SourcedValue<double>::provided(
            42.0, core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
        comparison.actual.unit = core::UnitToken::find("N*m").value();
        comparison.expected.quantity = core::SourcedValue<double>::provided(
            40.0, core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
        comparison.expected.unit = core::UnitToken::find("N*m").value();
        core::DiagnosticRecord record = core::DiagnosticRecord::make(
            code, core::ObjectId::generate(), std::string("obj-").append(32, 'b'),
            std::nullopt, "关节力矩超限", "实际力矩 42 N·m 超过阈值 40 N·m",
            "确认继续或修正输入", comparison);
        return core::ConfirmableFinding::make(record);
    }

    void build(CommandInteractionBridge::Deps deps)
    {
        if (!deps.presenter) {
            deps.presenter = presenter;   // 缺省挂接替身呈现器（可显式覆盖）
        }
        if (deps.clock == nullptr) {
            deps.clock = &clock;
        }
        if (!deps.principalProvider) {
            deps.principalProvider = [] { return std::string("tester"); };
        }
        bridge = std::make_unique<CommandInteractionBridge>(std::move(deps));
    }

    /// 主线程泵送直至谓词满足或自旋上限（不使用 sleep 判据——§12.3；
    /// 谓词内不得再依赖主线程执行——挂起形态的等待面在嵌套泵内）。
    static void pumpUntil(const std::function<bool()>& predicate)
    {
        const int spinGuard = 20000;
        for (int i = 0; i < spinGuard && !predicate(); ++i) {
            QCoreApplication::processEvents();
            std::this_thread::yield();
        }
    }
};

// =====================================================================
// SA-15/装配契约：fail-fast 与 principal 缓存（P-UI-4）
// =====================================================================

/** 装配契约：无呈现器 fail-fast；principal 会话启动采集缓存一次。 */
TEST(CommandInteractionBridgeTest, FactoryFailFastAndPrincipalCached_P_UI_4)
{
    IRD_TEST_INFO("SA-15", {}, std::nullopt);
    // 无呈现器＝桥无法履行确认流——调用方装配违约 fail-fast。
    EXPECT_THROW(
        [] {
            CommandInteractionBridge::Deps broken;
            broken.presenter = nullptr;
            CommandInteractionBridge brokenBridge(broken);
            (void)brokenBridge;
        }(),
        std::invalid_argument);

    // principal＝会话启动采集缓存（provider 只在构造期被调用一次——
    // P-UI-4"不逐次弹问"）。
    BridgeHarness h;
    int providerCalls = 0;
    CommandInteractionBridge::Deps deps;
    deps.presenter = h.presenter;
    deps.principalProvider = [&providerCalls] {
        ++providerCalls;
        return std::string("session-user");
    };
    h.build(std::move(deps));
    EXPECT_EQ(h.bridge->principal(), "session-user");
    EXPECT_EQ(providerCalls, 1);
}

// =====================================================================
// UI-CMD-4/5：确认流三态（未确认不提交；ui 只交 credentials）
// =====================================================================

/** UI-CMD-5：全部确认→凭据向量一一对应（principal＋注入时钟时刻）。 */
TEST(CommandInteractionBridgeTest, ConfirmedFlowProducesCredentials_UI_CMD_5)
{
    IRD_TEST_INFO("SA-15", {"AT-01"}, std::nullopt);
    BridgeHarness h;
    h.presenter->programmed = ConfirmDialogOutcome::Confirmed;
    h.build(CommandInteractionBridge::Deps{});

    const std::vector<core::ConfirmableFinding> findings = {
        BridgeHarness::makeFinding("POLICY-THRESHOLD-OUT-OF-RANGE"),
        BridgeHarness::makeFinding("POLICY-THRESHOLD-OUT-OF-RANGE"),
    };

    std::optional<std::vector<core::ConfirmationCredential>> outcome;
    std::thread commandThread([&] {
        outcome = h.bridge->requestConfirmations(findings);   // 命令执行线程角色
    });
    BridgeHarness::pumpUntil([&] { return h.presenter->entered.load() >= 1; });
    commandThread.join();

    ASSERT_TRUE(outcome.has_value());                 // 确认→凭据（非 nullopt）
    ASSERT_EQ(outcome->size(), findings.size());      // 与输入一一对应（§5.3.3）
    EXPECT_EQ((*outcome)[0].principal, "tester");     // principal＝会话缓存
    EXPECT_EQ((*outcome)[0].confirmedAtUtc, h.clock.m_now);   // 组装时刻（注入时钟）
    // 对话装配值断言（§9.3 映射——比较型三要素在 finding 内随行）。
    ASSERT_EQ(h.presenter->seen.size(), 1u);
    ASSERT_EQ(h.presenter->seen[0].items.size(), 2u); // 批量一次呈现
    EXPECT_EQ(h.presenter->seen[0].principal, "tester");
    EXPECT_EQ(h.presenter->seen[0].titleKey, "ui.dlg.confirm.title");
    EXPECT_EQ(h.presenter->seen[0].noSkipHintKey, "ui.dlg.confirm.no-skip");
    EXPECT_EQ(h.presenter->seen[0].items[0].finding.record.comparison.has_value(),
              true);
}

/** UI-CMD-4：用户拒绝→nullopt（对端按 confirmations-rejected 处置，
 *  无修订——桥不自行放行）。 */
TEST(CommandInteractionBridgeTest, RejectedFlowYieldsNoCredentials_UI_CMD_4)
{
    IRD_TEST_INFO("SA-15", {"MDL-06"}, std::nullopt);
    BridgeHarness h;
    h.presenter->programmed = ConfirmDialogOutcome::Rejected;
    h.build(CommandInteractionBridge::Deps{});

    const std::vector<core::ConfirmableFinding> findings = {
        BridgeHarness::makeFinding("POLICY-THRESHOLD-OUT-OF-RANGE"),
    };
    std::optional<std::vector<core::ConfirmationCredential>> outcome;
    std::thread commandThread([&] { outcome = h.bridge->requestConfirmations(findings); });
    BridgeHarness::pumpUntil([&] { return h.presenter->entered.load() >= 1; });
    commandThread.join();

    EXPECT_FALSE(outcome.has_value());   // 未确认不提交（SA-15——不产出凭据）
}

/** 空集防御：findings 为空→空凭据向量（不打开对话）。 */
TEST(CommandInteractionBridgeTest, EmptyFindingsShortCircuitWithoutDialog)
{
    IRD_TEST_INFO("SA-15", {}, std::nullopt);
    BridgeHarness h;
    h.build(CommandInteractionBridge::Deps{});
    const auto outcome = h.bridge->requestConfirmations({});
    ASSERT_TRUE(outcome.has_value());
    EXPECT_TRUE(outcome->empty());
    EXPECT_EQ(h.presenter->entered.load(), 0);   // 无事可确认——无对话
}

// =====================================================================
// UI-CMD-7：会话拆除唤醒（isAlive 探针——Aborted(interaction-lost)）
// =====================================================================

/** UI-CMD-7：对话挂起期间拆除→等待被唤醒返回 nullopt（无用户诊断、
 *  无修订——无永久等待，P-DIAG-6＋§5.6-5）。拆除由旁路线程触发
 *  （markSessionDismantled＝noexcept 原子面；主线程此时在嵌套泵内）。 */
TEST(CommandInteractionBridgeTest, DismantleWakesBlockedCommand_UI_CMD_7)
{
    IRD_TEST_INFO("SA-15", {}, std::nullopt);
    BridgeHarness h;
    h.presenter->hold = true;   // 对话"打开中"（嵌套泵送等待释放）
    h.build(CommandInteractionBridge::Deps{});

    const std::vector<core::ConfirmableFinding> findings = {
        BridgeHarness::makeFinding("POLICY-THRESHOLD-OUT-OF-RANGE"),
    };
    std::optional<std::vector<core::ConfirmationCredential>> outcome;
    std::thread commandThread([&] { outcome = h.bridge->requestConfirmations(findings); });

    // 旁路线程：等对话打开→会话拆除（探针翻转＋唤醒）→释放替身
    // （晚到的"决议"被安全丢弃——不崩不悬垂）。
    std::thread dismantler([&] {
        while (h.presenter->entered.load() < 1) {
            std::this_thread::yield();
        }
        EXPECT_TRUE(h.bridge->isAlive());
        h.bridge->markSessionDismantled();
        EXPECT_FALSE(h.bridge->isAlive());
        h.presenter->released = true;
    });

    // 主线程：泵送至释放（functor 在嵌套泵内——拆除后 released 翻转，
    // 嵌套泵退出、functor 收口、外层泵送返回）。
    BridgeHarness::pumpUntil([&] { return h.presenter->released.load(); });
    commandThread.join();
    dismantler.join();

    EXPECT_FALSE(outcome.has_value());   // Aborted(interaction-lost)——nullopt
    // 拆除后新请求直接 nullopt（对话不再打开——探针短路径）。
    EXPECT_FALSE(h.bridge->requestConfirmations(findings).has_value());
    EXPECT_EQ(h.presenter->entered.load(), 1);
}

/** P-DIAG-6：无自动超时——等待持续到外部决议/拆除，无时钟路径。 */
TEST(CommandInteractionBridgeTest, NoAutoTimeoutWhileWaiting_P_DIAG_6)
{
    IRD_TEST_INFO("SA-15", {"P-DIAG-6"}, std::nullopt);
    BridgeHarness h;
    h.presenter->hold = true;
    h.build(CommandInteractionBridge::Deps{});

    const std::vector<core::ConfirmableFinding> findings = {
        BridgeHarness::makeFinding("POLICY-THRESHOLD-OUT-OF-RANGE"),
    };
    std::atomic<bool> commandReturned{false};
    std::thread commandThread([&] {
        (void)h.bridge->requestConfirmations(findings);
        commandReturned = true;
    });

    // 旁路线程：等对话打开→观察多轮挂起泵送（模拟任意长的用户思考期）
    // →期间命令线程必须仍在等待（无超时自动确认）→释放。
    std::thread observer([&] {
        while (h.presenter->entered.load() < 1) {
            std::this_thread::yield();
        }
        while (h.presenter->holdSpins.load() < 50) {
            std::this_thread::yield();
        }
        EXPECT_FALSE(commandReturned.load());   // 50 轮泵送后仍未返回＝无超时
        h.presenter->released = true;           // 外部收口（真实系统＝决议或拆除）
    });

    BridgeHarness::pumpUntil([&] { return h.presenter->released.load(); });
    commandThread.join();
    observer.join();
    EXPECT_TRUE(commandReturned.load());        // 释放后才收口（等待非自旋耗尽）
    EXPECT_GE(h.presenter->holdSpins.load(), 50);
}

// =====================================================================
// UI-CMD-6：输入变化失效标注（§9.2"对话标注'输入已变化'"）
// =====================================================================

/** UI-CMD-6：修订提交后新对话带失效标注＋已开对话收到实时标注。 */
TEST(CommandInteractionBridgeTest, RevisionCommitAnnotatesStaleInput_UI_CMD_6)
{
    IRD_TEST_INFO("SA-15", {}, std::nullopt);
    BridgeHarness h;
    h.build(CommandInteractionBridge::Deps{});

    // 提交前对话：无标注。
    h.presenter->programmed = ConfirmDialogOutcome::Rejected;
    const std::vector<core::ConfirmableFinding> findings = {
        BridgeHarness::makeFinding("POLICY-THRESHOLD-OUT-OF-RANGE"),
    };
    std::thread first([&] { (void)h.bridge->requestConfirmations(findings); });
    BridgeHarness::pumpUntil([&] { return h.presenter->entered.load() >= 1; });
    first.join();
    ASSERT_EQ(h.presenter->seen.size(), 1u);
    EXPECT_FALSE(h.presenter->seen[0].items[0].staleInputHint);

    // 修订提交通知：代次递增＋实时标注通道（本例无在开对话——note
    // 计数面断言通道在位）。
    h.bridge->noteRevisionCommitted();
    BridgeHarness::pumpUntil(
        [&] { return h.presenter->inputChangedNotes.load() >= 1; });
    EXPECT_EQ(h.presenter->inputChangedNotes.load(), 1);

    // 提交后对话：条目带"输入已变化"标注＋批级失效横幅键在位。
    std::thread second([&] { (void)h.bridge->requestConfirmations(findings); });
    BridgeHarness::pumpUntil([&] { return h.presenter->entered.load() >= 2; });
    second.join();
    ASSERT_EQ(h.presenter->seen.size(), 2u);
    EXPECT_TRUE(h.presenter->seen[1].items[0].staleInputHint);
    EXPECT_EQ(h.presenter->seen[1].staleInputKey, "ui.dlg.confirm.stale-input");
}

// =====================================================================
// §9.3 装配映射：补全回调双层与 Dev 折叠区
// =====================================================================

/** §9.3：补全回调命中→作用对象/选项键/确认键/基线随行；Dev 折叠区只
 *  呈现命令类型 token（载荷摘要永不进用户文本——UX-02）。 */
TEST(CommandInteractionBridgeTest, EnrichmentFillsRecordFieldsAndDevFold_UI_CMD_5)
{
    IRD_TEST_INFO("UX-02", {}, std::nullopt);
    BridgeHarness h;
    const auto finding = BridgeHarness::makeFinding("POLICY-THRESHOLD-OUT-OF-RANGE");
    const core::ObjectId scopeObject = core::ObjectId::generate();

    // 带补全的桥：enrich 返回 FindingRecord（§9.3 双层数据的取回缝——
    // "经 project 间接读"同源面）。
    CommandInteractionBridge::Deps deps;
    deps.presenter = h.presenter;
    deps.enrich = [scopeObject](const core::ConfirmableFinding&) {
        diagnostics::FindingRecord record;
        record.subjectScope = {scopeObject};
        record.confirmTextKey = "diag.policy-threshold-out-of-range.confirm";
        record.optionKeys = {"ui.dlg.confirm.option.confirm",
                             "ui.dlg.confirm.option.reject"};
        record.baseRevisionId = core::RevisionId::generate();
        record.sourceCommandType = "policy.update";
        return record;
    };
    h.build(std::move(deps));

    h.presenter->programmed = ConfirmDialogOutcome::Rejected;
    std::thread commandThread([&] {
        (void)h.bridge->requestConfirmations({finding});
    });
    BridgeHarness::pumpUntil([&] { return h.presenter->entered.load() >= 1; });
    commandThread.join();

    ASSERT_EQ(h.presenter->seen.size(), 1u);
    const ConfirmDialogData& data = h.presenter->seen[0];
    ASSERT_EQ(data.items.size(), 1u);
    EXPECT_EQ(data.items[0].subjectScope.size(), 1u);          // 作用对象列表
    EXPECT_EQ(data.items[0].baseRevisionCanonical.rfind("rev-", 0), 0);  // 基线显示
    EXPECT_EQ(data.items[0].confirmTextKey,
              "diag.policy-threshold-out-of-range.confirm");   // 确认文案键随行
    // Dev 折叠区：只有命令类型 token，无哈希形态内容（UX-02 呈现边界）。
    ASSERT_EQ(data.devFoldLines.size(), 1u);
    EXPECT_NE(data.devFoldLines[0].find("command-type=policy.update"),
              std::string::npos);
}

/** §9.3 缺省形态：无补全回调→core 级数据＋通用确认/拒绝二键（零虚构）。 */
TEST(CommandInteractionBridgeTest, DefaultAssemblyWithoutEnricherZeroFabrication)
{
    IRD_TEST_INFO("UX-02", {}, std::nullopt);
    const auto finding = BridgeHarness::makeFinding("POLICY-THRESHOLD-OUT-OF-RANGE");
    const auto data = ui::assembleConfirmDialogData({finding}, "tester", false, nullptr);
    ASSERT_EQ(data.items.size(), 1u);
    // 码表键约定推导（diag.<code-lower>.title/.detail——P-DIAG-9）。
    EXPECT_EQ(data.items[0].titleKey, "diag.policy-threshold-out-of-range.title");
    EXPECT_EQ(data.items[0].detailKey, "diag.policy-threshold-out-of-range.detail");
    // 通用二选项＋record.subject 单对象（缺省作用对象）。
    ASSERT_EQ(data.items[0].optionKeys.size(), 2u);
    EXPECT_EQ(data.items[0].subjectScope.size(), 1u);
    EXPECT_EQ(data.items[0].baseRevisionCanonical, std::string());   // 无基线不显示
    EXPECT_TRUE(data.devFoldLines.empty());   // 无补全→无 Dev 折叠区
}

}  // namespace
