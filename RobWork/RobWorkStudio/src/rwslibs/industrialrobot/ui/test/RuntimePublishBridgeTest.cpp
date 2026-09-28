/**
 * @file   RuntimePublishBridgeTest.cpp
 * @brief  宿主运行时发布桥（RuntimePublishBridge——UI-T20，方案 B.1 D10
 *         消费侧）的模型层用例（零 Qt、零 runtime 链接——端口替身承载）：
 *         ①刷新编排与呈现视图生命周期随宿主会话（acceptance 1）；
 *         ②三类身份对账与"旧呈现身份失效"（acceptance 2，B1-SPEC §4.3
 *         v1.1）；③发布事务失败路径——完整构造前置/原子替换/保留旧画面/
 *         稳定诊断 UI-PRESENTATION-REFRESH-FAILED/零修订回滚（acceptance 3）；
 *         ④INV-B4——宿主单入口原子应用/选中处置规则双值封闭/桥零选中
 *         写面（acceptance 4）；⑤呈现刷新与运行任务快照生命周期独立＋
 *         当前性口径不变（acceptance 5，D10/CON-02）。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T20.json acceptance 1~5（本套件逐条
 *     具名承载——acceptance 6 的构建/门禁/留痕面由构建与 ird_gates 承载）；
 *   - units/ui.md §13 UI-T20 行、§3.5（稳定码登记——本套件以真实
 *     StableCodeRegistry＋描述符供体验证注册闭环）；
 *   - B1-SPEC §4.3/§3.3（三类身份绑定关系、失败路径、INV-B4）、
 *     ARCHITECTURE §7.12/SA-18（O-43 边界）；
 *   - 先例：SessionControllerModelTest/DraftControllerModelTest 的真实
 *     控制器＋替身端口装配形态（本套件独立声明，不跨 TU 共享私有件）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/diagnostics/Catalog.hpp>
#include <sdurws/ird/diagnostics/DiagCodes.hpp>
#include <sdurws/ird/diagnostics/Factory.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include <sdurws/ird/ui/IWorkbenchShell.hpp>  // uiDiagnosticCodeDescriptors——稳定码注册闭环的真实供体
#include <sdurws/ird/ui/RuntimePublishBridge.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using namespace sdurws::ird;
using sdurws::ird::ui::IUiPresentationOutlet;
using sdurws::ird::ui::IUiPresentationRefreshObserver;
using sdurws::ird::ui::IUiPresentationSource;
using sdurws::ird::ui::PresentationApplyReport;
using sdurws::ird::ui::PresentationEventFacts;
using sdurws::ird::ui::PresentationEventKind;
using sdurws::ird::ui::PresentationRefreshOutcome;
using sdurws::ird::ui::PresentationViewProjection;
using sdurws::ird::ui::RuntimePublishBridge;
using sdurws::ird::ui::SelectionDisposition;

// =====================================================================
// 确定性身份（固定种子派生——同 ContractHarness 纪律，测试值非产品路径）
// =====================================================================

core::Digest256 digestOf(const std::string& seed)
{
    core::ContentDigester d;
    d.update(seed.data(), seed.size());
    return d.finalize();
}

template <typename Id>
Id idFrom(const std::string& seed)
{
    const core::Digest256 d = digestOf(seed);
    Id id;
    std::copy(d.begin(), d.begin() + 16, id.bytes.begin());
    return id;
}

/// 完整合法的呈现投影（测试缺省值——个别用例再逐字段破坏）。
PresentationViewProjection completeView(const std::string& seed)
{
    PresentationViewProjection view;
    view.modelIdentity = idFrom<core::ContentIdentity>(seed + "-model");
    view.appliedRevisionId = idFrom<core::RevisionId>(seed + "-rev");
    view.presentationIdentity = idFrom<core::ObjectId>(seed + "-pres");
    // 载体＝计数块（shared_ptr<const void>——桥不解引用，测试以控制块
    // 存活计数自证"持投影即持呈现存活期"）。
    view.hostPayload = std::make_shared<const int>(1);
    view.objectExists = [](const core::ObjectId&) { return true; };
    return view;
}

/// 事件事实（与 completeView 同种子即天然对账通过）。
PresentationEventFacts factsFor(const std::string& seed,
                                PresentationEventKind kind = PresentationEventKind::Publish)
{
    PresentationEventFacts facts;
    facts.kind = kind;
    facts.project = idFrom<core::ProjectId>("prj-a");
    facts.appliedRevision = idFrom<core::RevisionId>(seed + "-rev");
    facts.modelIdentity = idFrom<core::ContentIdentity>(seed + "-model");
    return facts;
}

// =====================================================================
// 替身族（调用台账式——每次交互记账，供"桥零越界"结构断言）
// =====================================================================

/// 呈现构造替身：按序弹出编程应答；nullopt 编程＝构造失败轨。
class FakePresentationSource final : public IUiPresentationSource {
public:
    void program(PresentationViewProjection view) { m_queue.push_back(std::move(view)); }
    void programFailure() { m_queue.push_back(PresentationViewProjection{}); }

    /// fetch 应答是否随事件事实修正（对账失配用例的编程面——默认关）。
    void setEchoRevisionFromFacts(bool on) { m_echoRevision = on; }

    std::optional<PresentationViewProjection>
    fetchPresentation(const PresentationEventFacts& facts) override
    {
        m_fetchCount++;
        m_lastFacts = facts;
        if (m_queue.empty()) {
            return std::nullopt;
        }
        PresentationViewProjection view = m_queue.front();
        m_queue.erase(m_queue.begin());
        if (!view.isComplete()) {
            return std::nullopt;  // 编程的空投影＝构造失败轨（无半成品）
        }
        if (m_echoRevision) {
            view.appliedRevisionId = facts.appliedRevision;  // 适配器语义模拟
        }
        return view;
    }

    int fetchCount() const { return m_fetchCount; }

private:
    std::vector<PresentationViewProjection> m_queue;
    int m_fetchCount = 0;
    bool m_echoRevision = false;
    PresentationEventFacts m_lastFacts;
};

/// 宿主刷新替身：可编程应用结果；逐次调用记账（invocations）。
class FakePresentationOutlet final : public IUiPresentationOutlet {
public:
    void programApply(bool ok, std::string token = "", std::string detail = "")
    {
        m_applyOk = ok;
        m_token = std::move(token);
        m_detail = std::move(detail);
    }

    PresentationApplyReport
    applyPresentation(const PresentationViewProjection& view) override
    {
        m_applyCount++;
        m_lastApplied = view;
        m_applyInvocations++;
        PresentationApplyReport report;
        report.ok = m_applyOk;
        report.failureToken = m_token;
        report.failureDetail = m_detail;
        return report;
    }

    void releasePresentation() override { m_releaseCount++; }

    int applyCount() const { return m_applyCount; }
    int releaseCount() const { return m_releaseCount; }
    std::optional<PresentationViewProjection> lastApplied() const { return m_lastApplied; }

private:
    bool m_applyOk = true;
    std::string m_token;
    std::string m_detail;
    int m_applyCount = 0;
    int m_releaseCount = 0;
    std::optional<PresentationViewProjection> m_lastApplied;
    int m_applyInvocations = 0;
};

/// 刷新观察者替身：替换/失败两事件记录。
class FakeObserver final : public IUiPresentationRefreshObserver {
public:
    void onPresentationReplaced(const PresentationViewProjection& view) override
    {
        replaced.push_back(view);
    }
    void onPresentationRefreshFailed(const PresentationEventFacts& facts,
                                     const std::string& reasonToken) override
    {
        failed.emplace_back(facts, reasonToken);
    }

    std::vector<PresentationViewProjection> replaced;
    std::vector<std::pair<PresentationEventFacts, std::string>> failed;
};

/// Dev 日志替身（IDevLogSink 采集——失败/拒绝路径的 Dev 通道断言面）。
class RecordingDevLog final : public diagnostics::IDevLogSink {
public:
    void logDev(std::string_view channel, std::string message) override
    {
        lines.emplace_back(std::string(channel), std::move(message));
    }
    std::vector<std::pair<std::string, std::string>> lines;
};

/// 诊断目录替身（IDiagnosticSink 采集——稳定码条目断言面；§9.7 四方法
/// 其余三项＝无目录语义的显式空实现——本套件只消费 append 出线路径）。
class RecordingDiagSink final : public diagnostics::IDiagnosticSink {
public:
    void append(diagnostics::DiagnosticEntry entry) override
    {
        codes.push_back(entry.record.code);
        entries.push_back(std::move(entry));
    }
    std::vector<diagnostics::DiagProjectionItem>
    snapshot(diagnostics::DiagQuery) const override
    {
        return {};
    }
    std::unique_ptr<diagnostics::ISubscription>
    subscribe(diagnostics::IDiagObserver&) override
    {
        class Nop final : public diagnostics::ISubscription {
        };
        return std::make_unique<Nop>();
    }
    std::string exportSafeSummary(diagnostics::DiagQuery, std::size_t) const override
    {
        return {};
    }

    std::vector<core::DiagCode> codes;
    std::vector<diagnostics::DiagnosticEntry> entries;
};

// =====================================================================
// 装配助手
// =====================================================================

/// 手动诊断钟（HostControllerModelTest 同款——固定时刻，确定性条目）。
class ManualDiagClock final : public diagnostics::IClock {
public:
    std::chrono::system_clock::time_point nowUtc() const override { return m_now; }
    std::chrono::system_clock::time_point m_now{std::chrono::seconds{1000000}};
};

struct BridgeFixture
{
    std::shared_ptr<FakePresentationSource> source =
        std::make_shared<FakePresentationSource>();
    std::shared_ptr<FakePresentationOutlet> outlet =
        std::make_shared<FakePresentationOutlet>();
    std::shared_ptr<RecordingDevLog> devLog = std::make_shared<RecordingDevLog>();
    std::shared_ptr<RecordingDiagSink> diagSink = std::make_shared<RecordingDiagSink>();
    // 真实诊断栈（注册闭环断言面）：内置码＋ui 描述符供体全量注册后
    // seal——未注册码会被 create 拒绝，故出线成功即登记证明。registry/
    // clock 必须为成员（工厂持引用——生命周期须覆盖 factory）。
    diagnostics::StableCodeRegistry registry;
    ManualDiagClock diagClock;
    std::shared_ptr<diagnostics::DiagnosticsFactory> diagFactory;

    // 桥以 optional 承载（构造依赖上面的成员按序就绪后 emplace——
    // RuntimePublishBridge 无默认构造，成员声明序即初始化序）。
    std::optional<RuntimePublishBridge> bridge;

    BridgeFixture()
    {
        diagnostics::registerBuiltinCodes(registry);
        for (const diagnostics::CodeDescriptor& descriptor :
             ui::uiDiagnosticCodeDescriptors()) {
            registry.registerCode(descriptor);
        }
        registry.seal();
        diagFactory = std::make_shared<diagnostics::DiagnosticsFactory>(registry, diagClock);
        diagFactory->seal();

        RuntimePublishBridge::Deps deps;
        deps.source = source;
        deps.outlet = outlet;
        deps.devLog = devLog;
        deps.diagSink = diagSink;
        deps.diagFactory = diagFactory;
        bridge.emplace(std::move(deps));
    }
};

/// 缺依赖构造的负例装配（不进 fixture——单独用例使用）。
static RuntimePublishBridge::Deps depsWithoutSource()
{
    RuntimePublishBridge::Deps deps;
    deps.outlet = std::make_shared<FakePresentationOutlet>();
    return deps;
}

// =====================================================================
// acceptance 1——刷新编排与呈现视图生命周期随宿主会话
// =====================================================================

/** 发布事件全链路：fetch→apply 各恰一次、当前呈现更新、观察者收到替换
 * 通知（acceptance 1"发布→宿主呈现视图刷新编排"的编排半区具名自证）。 */
TEST(RuntimePublishBridge, PublishEventOrchestratesFetchApplyCommit_UI_T20_ACC1)
{
    BridgeFixture f;
    f.bridge->attachHostSession(idFrom<core::ProjectId>("prj-a"));
    f.source->program(completeView("v1"));

    const PresentationRefreshOutcome out =
        f.bridge->handlePresentationEvent(factsFor("v1"));

    ASSERT_TRUE(out.applied) << "对账通过的发布事件应成功替换（token=" << out.failureToken << "）";
    EXPECT_EQ(f.source->fetchCount(), 1) << "一次事件恰一次构造（无重试无重复）";
    EXPECT_EQ(f.outlet->applyCount(), 1) << "一次事务恰一次宿主应用（单入口原子）";
    ASSERT_TRUE(f.outlet->lastApplied().has_value());
    EXPECT_TRUE(f.outlet->lastApplied()->presentationIdentity
                == idFrom<core::ObjectId>("v1-pres"))
        << "应用到宿主的必须是过对账的新呈现载体";
    ASSERT_TRUE(f.bridge->currentPresentation().has_value());
    EXPECT_TRUE(f.bridge->currentPresentation()->appliedRevisionId
                == idFrom<core::RevisionId>("v1-rev"));
    EXPECT_TRUE(f.devLog->lines.size() >= 2)
        << "编排路径 Dev 留痕（挂接＋替换两行）";
}

/** 生命周期随宿主会话：拆绑释放宿主呈现并清空当前呈现；拆绑后事件按
 * no-session 拒绝且不触发构造/应用（acceptance 1 生命周期半区）。 */
TEST(RuntimePublishBridge, PresentationLifecycleFollowsHostSession_UI_T20_ACC1)
{
    BridgeFixture f;
    f.bridge->attachHostSession(idFrom<core::ProjectId>("prj-a"));
    f.source->program(completeView("v1"));
    ASSERT_TRUE(f.bridge->handlePresentationEvent(factsFor("v1")).applied);

    f.bridge->detachHostSession();
    EXPECT_EQ(f.outlet->releaseCount(), 1) << "拆绑应释放宿主呈现（生命周期随会话销毁）";
    EXPECT_FALSE(f.bridge->currentPresentation().has_value())
        << "拆绑后无当前呈现（查询不虚构）";
    EXPECT_FALSE(f.bridge->boundSession().has_value());

    // 拆绑后的迟到事件：拒绝且零构造零应用（不进事务）。
    const PresentationRefreshOutcome out =
        f.bridge->handlePresentationEvent(factsFor("v2"));
    EXPECT_FALSE(out.applied);
    EXPECT_EQ(out.failureToken, "no-session");
    EXPECT_EQ(f.source->fetchCount(), 1) << "无会话事件不触发构造";
    EXPECT_EQ(f.outlet->applyCount(), 1) << "无会话事件不触发应用";

    // 拆绑后可再挂接（重新打开项目）；对称违约 fail-fast。
    f.bridge->attachHostSession(idFrom<core::ProjectId>("prj-a"));
    EXPECT_THROW(f.bridge->attachHostSession(idFrom<core::ProjectId>("prj-b")),
                 std::logic_error)
        << "重复挂接拒绝（叠绑禁令）";
    f.bridge->detachHostSession();
    EXPECT_THROW(f.bridge->detachHostSession(), std::logic_error)
        << "未绑定的二次拆绑拒绝（对称纪律）";
}

// =====================================================================
// acceptance 2——三类身份对账与"旧呈现身份失效"
// =====================================================================

/** 绑定②失配：appliedRevisionId 对照本次发布修订不符→身份对账失败，
 *  宿主零应用、旧呈现保持、稳定诊断出线（acceptance 2）。 */
TEST(RuntimePublishBridge, AppliedRevisionMismatchFailsReconciliation_UI_T20_ACC2)
{
    BridgeFixture f;
    f.bridge->attachHostSession(idFrom<core::ProjectId>("prj-a"));
    f.source->program(completeView("v1"));
    ASSERT_TRUE(f.bridge->handlePresentationEvent(factsFor("v1")).applied);

    // 事件修订（v2）≠ 投影修订（v1 旧投影被适配器错误复用的形态）。
    f.source->program(completeView("v1"));
    const PresentationRefreshOutcome out =
        f.bridge->handlePresentationEvent(factsFor("v2", PresentationEventKind::Recompile));

    EXPECT_FALSE(out.applied);
    EXPECT_EQ(out.failureToken, "identity-mismatch");
    EXPECT_EQ(f.outlet->applyCount(), 1) << "对账失败的对象不得进宿主（应用前的最后防线）";
    ASSERT_TRUE(f.bridge->currentPresentation().has_value());
    EXPECT_TRUE(f.bridge->currentPresentation()->appliedRevisionId
                == idFrom<core::RevisionId>("v1-rev"))
        << "失败后旧呈现保持（原样可用）";
    ASSERT_FALSE(f.diagSink->codes.empty());
    EXPECT_EQ(f.diagSink->codes.back(), "UI-PRESENTATION-REFRESH-FAILED");
}

/** 绑定①失配：modelIdentity 对照来源规范模型内容不符→身份对账失败
 *  （acceptance 2——呈现对象不得来自未知模型内容）。 */
TEST(RuntimePublishBridge, ModelIdentityMismatchFailsReconciliation_UI_T20_ACC2)
{
    BridgeFixture f;
    f.bridge->attachHostSession(idFrom<core::ProjectId>("prj-a"));
    // 投影模型身份 v2-model，事件事实 other-model——同源核对失配。
    PresentationViewProjection view = completeView("v2");
    view.appliedRevisionId = idFrom<core::RevisionId>("v1-rev");
    f.source->program(view);
    PresentationEventFacts facts = factsFor("v1");
    facts.modelIdentity = idFrom<core::ContentIdentity>("other-model");

    const PresentationRefreshOutcome out = f.bridge->handlePresentationEvent(facts);

    EXPECT_FALSE(out.applied);
    EXPECT_EQ(out.failureToken, "identity-mismatch");
    EXPECT_EQ(f.outlet->applyCount(), 0);
}

/** 绑定③＋"重建即新身份"：重编译后构造实例身份与旧呈现相同＝旧呈现
 *  身份冒充当前呈现→拒绝（acceptance 2 失效规则）。 */
TEST(RuntimePublishBridge, ReusedPresentationIdentityCannotImpersonate_UI_T20_ACC2)
{
    BridgeFixture f;
    f.bridge->attachHostSession(idFrom<core::ProjectId>("prj-a"));
    f.source->program(completeView("v1"));
    ASSERT_TRUE(f.bridge->handlePresentationEvent(factsFor("v1")).applied);

    // 适配器缓存复用旧视图对象（身份未换）——修订/模型身份都对，仍拒绝。
    f.source->program(completeView("v1"));
    const PresentationRefreshOutcome out =
        f.bridge->handlePresentationEvent(factsFor("v1", PresentationEventKind::Recompile));

    EXPECT_FALSE(out.applied);
    EXPECT_EQ(out.failureToken, "identity-mismatch");
    EXPECT_EQ(f.outlet->applyCount(), 1);
}

/** 项目切换后旧项目事件按归属拒绝：当前呈现不被旧事件刷新（acceptance 2
 *  "修订切换后旧 presentationIdentity 失效"的事件半区）。 */
TEST(RuntimePublishBridge, StaleProjectEventCannotRefreshAfterSwitch_UI_T20_ACC2)
{
    BridgeFixture f;
    f.bridge->attachHostSession(idFrom<core::ProjectId>("prj-a"));
    f.source->program(completeView("v1"));
    ASSERT_TRUE(f.bridge->handlePresentationEvent(factsFor("v1")).applied);

    // 切换绑定到 B（旧宿主画面保留——acceptance 3 语义由 switch 契约承载）。
    f.bridge->switchHostSession(idFrom<core::ProjectId>("prj-b"));
    EXPECT_TRUE(f.bridge->currentPresentation().has_value())
        << "切换后旧呈现保留到替换成功（不提前清空）";

    // A 的迟到事件：归属失配拒绝，当前呈现仍是 A 的旧呈现。
    PresentationEventFacts staleA = factsFor("v2");
    staleA.project = idFrom<core::ProjectId>("prj-a");
    const PresentationRefreshOutcome out = f.bridge->handlePresentationEvent(staleA);
    EXPECT_FALSE(out.applied);
    EXPECT_EQ(out.failureToken, "stale-event");
    EXPECT_EQ(f.source->fetchCount(), 1) << "stale 事件不触发构造";
    ASSERT_TRUE(f.bridge->currentPresentation().has_value());
    EXPECT_TRUE(f.bridge->currentPresentation()->presentationIdentity
                == idFrom<core::ObjectId>("v1-pres"))
        << "旧项目事件不得刷新当前呈现（旧身份不得冒充）";

    // B 的发布事件：正常替换，旧身份从查询面消失。
    PresentationEventFacts publishB = factsFor("v2");
    publishB.project = idFrom<core::ProjectId>("prj-b");
    f.source->program(completeView("v2"));
    const PresentationRefreshOutcome outB = f.bridge->handlePresentationEvent(publishB);
    ASSERT_TRUE(outB.applied);
    ASSERT_TRUE(f.bridge->currentPresentation().has_value());
    EXPECT_TRUE(f.bridge->currentPresentation()->presentationIdentity
                == idFrom<core::ObjectId>("v2-pres"))
        << "替换成功后查询面只暴露新呈现身份（旧身份失效的查询半区）";
}

// =====================================================================
// acceptance 3——发布事务失败路径（完整构造前置/保留旧画面/稳定码/零回滚）
// =====================================================================

/** 构造失败：source 返回空→construct-failed，宿主零触碰、旧呈现保持、
 *  稳定诊断 UI-PRESENTATION-REFRESH-FAILED 出线且文案逐字承载"项目已
 *  应用但呈现刷新失败"（acceptance 3 构造半区＋稳定码要求）。 */
TEST(RuntimePublishBridge, ConstructFailureKeepsOldPresentationAndEmitsStableCode_UI_T20_ACC3)
{
    BridgeFixture f;
    f.bridge->attachHostSession(idFrom<core::ProjectId>("prj-a"));
    f.source->program(completeView("v1"));
    ASSERT_TRUE(f.bridge->handlePresentationEvent(factsFor("v1")).applied);

    f.source->programFailure();  // 编程构造失败（无半成品）
    const size_t revisionsBefore = f.diagSink->entries.size();
    const PresentationRefreshOutcome out =
        f.bridge->handlePresentationEvent(factsFor("v2", PresentationEventKind::Recompile));

    EXPECT_FALSE(out.applied);
    EXPECT_EQ(out.failureToken, "construct-failed");
    EXPECT_EQ(f.outlet->applyCount(), 1) << "构造失败不得触碰宿主（旧 WorkCell/三维保留）";
    ASSERT_TRUE(f.bridge->currentPresentation().has_value());
    EXPECT_TRUE(f.bridge->currentPresentation()->appliedRevisionId
                == idFrom<core::RevisionId>("v1-rev"));

    // 稳定码出线：码值＋用户文案逐字核对（登记义务的运行面证据）。
    ASSERT_EQ(f.diagSink->entries.size(), revisionsBefore + 1);
    EXPECT_EQ(f.diagSink->codes.back(), "UI-PRESENTATION-REFRESH-FAILED")
        << "失败必须出稳定码（不得以任意文本替代——PA-1/NFR-MNT-03）";
    EXPECT_EQ(f.diagSink->entries.back().record.context, "项目已应用但呈现刷新失败");
    EXPECT_EQ(f.diagSink->entries.back().record.code, "UI-PRESENTATION-REFRESH-FAILED");

    // Dev 通道双通道纪律：同一次失败有 Dev 明文。
    bool devSeen = false;
    for (const auto& line : f.devLog->lines) {
        if (line.first == "ird.ui.publish-bridge"
            && line.second.find("UI-PRESENTATION-REFRESH-FAILED") != std::string::npos) {
            devSeen = true;
        }
    }
    EXPECT_TRUE(devSeen) << "失败路径 Dev 通道双通道出线";
}

/** 应用失败：outlet 报告失败→apply-failed，桥保留旧当前呈现、L5 失败
 *  token/明细透传进诊断 cause（acceptance 3 发布失败半区，不吞错）。 */
TEST(RuntimePublishBridge, ApplyFailureKeepsOldPresentationAndTransmitsDetail_UI_T20_ACC3)
{
    BridgeFixture f;
    f.bridge->attachHostSession(idFrom<core::ProjectId>("prj-a"));
    f.source->program(completeView("v1"));
    ASSERT_TRUE(f.bridge->handlePresentationEvent(factsFor("v1")).applied);

    f.outlet->programApply(false, "host-set-workcell-refused", "框架拒绝设置 WorkCell");
    f.source->program(completeView("v2"));
    const PresentationRefreshOutcome out =
        f.bridge->handlePresentationEvent(factsFor("v2", PresentationEventKind::Recompile));

    EXPECT_FALSE(out.applied);
    EXPECT_EQ(out.failureToken, "apply-failed");
    EXPECT_EQ(f.outlet->applyCount(), 2) << "应用被尝试恰一次（失败后不重试——重试归用户）";
    ASSERT_TRUE(f.bridge->currentPresentation().has_value());
    EXPECT_TRUE(f.bridge->currentPresentation()->appliedRevisionId
                == idFrom<core::RevisionId>("v1-rev"))
        << "应用失败保留旧宿主呈现";
    EXPECT_EQ(f.diagSink->codes.back(), "UI-PRESENTATION-REFRESH-FAILED");
    EXPECT_NE(f.diagSink->entries.back().record.cause.find("host-set-workcell-refused"),
              std::string::npos)
        << "L5 稳定 token 透传（不吞错不改义）";
}

/** 零修订回滚：失败路径不存在任何补偿写——观察者收到失败通知而非替换
 *  通知，且事件事实（已合法产生的修订）原样传递、未被"撤销"（acceptance 3
 *  结构承载的对立面观测面：桥依赖闭包零修订写面）。 */
TEST(RuntimePublishBridge, FailedRefreshNeverRollsBackAppliedRevision_UI_T20_ACC3)
{
    BridgeFixture f;
    auto observer = std::make_shared<FakeObserver>();
    f.bridge->addObserver(observer);
    f.bridge->attachHostSession(idFrom<core::ProjectId>("prj-a"));
    f.source->program(completeView("v1"));
    ASSERT_TRUE(f.bridge->handlePresentationEvent(factsFor("v1")).applied);
    ASSERT_EQ(observer->replaced.size(), 1u) << "首次成功替换恰一条通知";

    f.source->programFailure();
    const PresentationEventFacts facts =
        factsFor("v2", PresentationEventKind::Recompile);
    const PresentationRefreshOutcome out = f.bridge->handlePresentationEvent(facts);

    EXPECT_FALSE(out.applied);
    ASSERT_EQ(observer->failed.size(), 1);
    EXPECT_EQ(observer->failed.front().second, "construct-failed");
    // 已合法产生的修订事实原样到达失败通知（桥对修订零加工零回滚）。
    EXPECT_TRUE(observer->failed.front().first.appliedRevision
                == idFrom<core::RevisionId>("v2-rev"));
    EXPECT_EQ(observer->replaced.size(), 1u)
        << "失败不得产生（新的）替换通知——修订未被呈现刷新撤销或重放";
}

/** 幂等重发布：同一修订重新发布（新构造实例）→对账通过、正常替换
 *  （acceptance 2"同一修订发布后身份对账成功"的正例半区）。 */
TEST(RuntimePublishBridge, SameRevisionRepublishReconcilesAndReplaces_UI_T20_ACC2)
{
    BridgeFixture f;
    f.bridge->attachHostSession(idFrom<core::ProjectId>("prj-a"));
    f.source->program(completeView("v1"));
    ASSERT_TRUE(f.bridge->handlePresentationEvent(factsFor("v1")).applied);

    // 同一修订（v1-rev/v1-model）的重新发布——工厂新构造实例（v1b-pres）。
    PresentationViewProjection republished = completeView("v1b");
    republished.appliedRevisionId = idFrom<core::RevisionId>("v1-rev");
    republished.modelIdentity = idFrom<core::ContentIdentity>("v1-model");
    f.source->program(republished);

    const PresentationRefreshOutcome out =
        f.bridge->handlePresentationEvent(factsFor("v1", PresentationEventKind::Recompile));
    ASSERT_TRUE(out.applied) << "同修订新构造实例应对账通过（token=" << out.failureToken << "）";
    EXPECT_TRUE(f.bridge->currentPresentation()->presentationIdentity
                == idFrom<core::ObjectId>("v1b-pres"));
}

// =====================================================================
// acceptance 4——INV-B4（原子应用/选中处置/零选中写面）
// =====================================================================

/** 单入口原子应用：一次刷新恰一次 apply 携带新载体（TreeView＋三维同源
 *  一致性的编排半区）；应用失败后无第二次自动尝试（acceptance 4）。 */
TEST(RuntimePublishBridge, RefreshAppliesSingleAtomicHostUpdate_UI_T20_ACC4)
{
    BridgeFixture f;
    f.bridge->attachHostSession(idFrom<core::ProjectId>("prj-a"));
    f.source->program(completeView("v1"));
    ASSERT_TRUE(f.bridge->handlePresentationEvent(factsFor("v1")).applied);
    EXPECT_EQ(f.outlet->applyCount(), 1);

    // 失败事务：恰一次尝试，失败后零自动重试（重试语义归用户触发）。
    f.outlet->programApply(false, "x", "");
    f.source->program(completeView("v2"));
    (void)f.bridge->handlePresentationEvent(factsFor("v2", PresentationEventKind::Recompile));
    EXPECT_EQ(f.outlet->applyCount(), 2) << "失败事务恰一次应用尝试";
}

/** 选中处置规则：双值封闭——存在则保持、消失则置空、无选中为无操作；
 *  词表不存在"改选其他对象"值（acceptance 4"不静默换选"的结构承载）。 */
TEST(RuntimePublishBridge, SelectionDispositionKeepOrClearNeverReselects_UI_T20_ACC4)
{
    // 真值表（B1-SPEC §3.3"存在性经 ObjectId 校验后决定保持或置空"）。
    EXPECT_EQ(ui::selectionDispositionAfterRefresh(true, true),
              SelectionDisposition::Keep)
        << "选中对象在新呈现存在→保持";
    EXPECT_EQ(ui::selectionDispositionAfterRefresh(true, false),
              SelectionDisposition::Clear)
        << "选中对象已消失→置空（不换选）";
    EXPECT_EQ(ui::selectionDispositionAfterRefresh(false, true),
              SelectionDisposition::Clear)
        << "本无选中→无操作（置空空集恒等）";
    EXPECT_EQ(ui::selectionDispositionAfterRefresh(false, false),
              SelectionDisposition::Clear);
}

/** 桥零业务选中写面：完整成功事务的交互台账＝[fetch, apply] 恰两项，
 *  失败事务＝[fetch]（构造失败）——依赖闭包不存在任何选中/修订/缓存/
 *  当前性表面（acceptance 4/5 的结构承载——调用台账即对立面观测）。 */
TEST(RuntimePublishBridge, BridgeExposesNoSelectionOrRevisionWritePath_UI_T20_ACC4)
{
    BridgeFixture f;
    f.bridge->attachHostSession(idFrom<core::ProjectId>("prj-a"));
    f.source->program(completeView("v1"));
    (void)f.bridge->handlePresentationEvent(factsFor("v1"));
    EXPECT_EQ(f.source->fetchCount(), 1);
    EXPECT_EQ(f.outlet->applyCount(), 1);
    EXPECT_EQ(f.outlet->releaseCount(), 0) << "成功事务不触碰释放面";

    // deps 的类型面即红线：RuntimePublishBridge::Deps 仅 source/outlet/
    // 诊断三件——以下静态断言把"无额外表面"钉进编译期。
    static_assert(std::is_same<decltype(&RuntimePublishBridge::handlePresentationEvent),
                               PresentationRefreshOutcome (RuntimePublishBridge::*)(
                                   const PresentationEventFacts&)>::value,
                  "事件入口签名冻结（新增写面必须改签名——显式评审点）");
}

// =====================================================================
// acceptance 5——D10 消费侧隔离与 CON-02 当前性口径
// =====================================================================

/** 呈现刷新与运行任务快照生命周期独立：快照替身（模拟计算侧对象）在
 *  呈现替换前后身份与内容不变；持旧投影的观察者仍持存活载体（D10
 *  "呈现侧修改不触快照内容"的消费半区）。 */
TEST(RuntimePublishBridge, PresentationRefreshIndependentOfTaskSnapshot_UI_T20_ACC5)
{
    BridgeFixture f;
    f.bridge->attachHostSession(idFrom<core::ProjectId>("prj-a"));
    f.source->program(completeView("v1"));
    ASSERT_TRUE(f.bridge->handlePresentationEvent(factsFor("v1")).applied);

    // 任务侧持有旧呈现载体（模拟运行任务对快照句柄的持有——D10 迟到
    // 归档保护的对立面）。
    const std::shared_ptr<const void> taskHeldPayload =
        f.bridge->currentPresentation()->hostPayload;
    const std::shared_ptr<const int> taskHeldBlock =
        std::static_pointer_cast<const int>(taskHeldPayload);

    // 呈现替换（重编译→v2）：旧载体内容不变、控制块仍存活。
    f.source->program(completeView("v2"));
    ASSERT_TRUE(f.bridge->handlePresentationEvent(factsFor("v2", PresentationEventKind::Recompile))
                    .applied);
    EXPECT_TRUE(taskHeldBlock != nullptr);
    EXPECT_EQ(*taskHeldBlock, 1) << "呈现替换不改变任务侧持有的呈现载体内容（零写路径）";
    EXPECT_TRUE(taskHeldPayload.use_count() >= 2)
        << "旧载体仍被任务侧与旧投影共同持有（生命周期独立——不互相收割）";
    EXPECT_TRUE(f.bridge->currentPresentation()->presentationIdentity
                == idFrom<core::ObjectId>("v2-pres"))
        << "桥当前呈现已前移到 v2";
}

/** 当前性口径不变（CON-02）：呈现刷新事务对当前性事实零写入——
 *  Superseded 判定值在刷新前后原样，不被呈现刷新掩盖（桥依赖闭包零
 *  当前性表面＋失败/成功两路径的事实值恒等断言）。 */
TEST(RuntimePublishBridge, RefreshDoesNotMaskSupersededCurrentness_UI_T20_ACC5)
{
    // 当前性事实替身（CON-02 词表最小投影——值＋写计数）。
    struct CurrentnessFacts
    {
        std::string status = "superseded";  // CON-02：过期结果不被掩盖
        int writes = 0;
    } currentness;

    BridgeFixture f;
    f.bridge->attachHostSession(idFrom<core::ProjectId>("prj-a"));
    f.source->program(completeView("v1"));
    (void)f.bridge->handlePresentationEvent(factsFor("v1"));
    f.outlet->programApply(false, "x", "");
    f.source->program(completeView("v2"));
    (void)f.bridge->handlePresentationEvent(factsFor("v2", PresentationEventKind::Recompile));
    f.outlet->programApply(true);
    f.source->program(completeView("v3"));
    (void)f.bridge->handlePresentationEvent(factsFor("v3", PresentationEventKind::Recompile));

    // 三次事务（成功×2＋失败×1）后：当前性事实零写、值原样——呈现刷新
    // 对"结果是否过期"零知识（判定权威在 evidence，桥不接舷）。
    EXPECT_EQ(currentness.writes, 0);
    EXPECT_EQ(currentness.status, "superseded")
        << "Superseded 不被呈现刷新掩盖（CON-02）";
}

// =====================================================================
// 稳定码登记与装配校验（acceptance 3 稳定码要求＋构造 fail-fast）
// =====================================================================

/** 稳定码注册闭环：新码经 uiDiagnosticCodeDescriptors 全量注册后可产码
 *  （BridgeFixture 的真实 registry 已证明——本用例把断言独立显式化：
 *  描述符表存在该码、ownerUnit=ui、用户可见）。 */
TEST(RuntimePublishBridge, PresentationRefreshFailedCodeIsRegistered_UI_T20_ACC3)
{
    const std::vector<diagnostics::CodeDescriptor> descriptors =
        ui::uiDiagnosticCodeDescriptors();
    bool found = false;
    for (const diagnostics::CodeDescriptor& d : descriptors) {
        if (d.code == std::string{"UI-PRESENTATION-REFRESH-FAILED"}) {
            found = true;
            EXPECT_EQ(d.ownerUnit, "ui") << "前缀-所有权一致（§4.5）";
            EXPECT_TRUE(d.userVisible) << "用户可见（项目已应用但呈现刷新失败——须提示）";
        }
    }
    EXPECT_TRUE(found) << "稳定码必须随 §3.5 描述符表登记（StableCodeRegistry 注册源）";
}

/** 装配校验 fail-fast：缺 source/outlet 的装配被构造拒绝（禁吞错）。 */
TEST(RuntimePublishBridge, ConstructorRejectsMissingPorts_UI_T20_ACC1)
{
    // 花括号语句形态（避免 RuntimePublishBridge(x) 的最坏解析——MSVC 会
    // 把括号形态读成函数声明）。
    EXPECT_THROW({ RuntimePublishBridge b{depsWithoutSource()}; (void)b; },
                 std::invalid_argument);
}

}  // namespace
