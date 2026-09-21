/**
 * @file   DiagPresentationModelTest.cpp
 * @brief  UI-T13 模型层用例（QCoreApplication 级——§12.1 第一层分工）：
 *         诊断呈现模型 §9.1/§10.8 的落位自证——过滤/去重折叠/原因链
 *         （UI-DIA-1）、actionKind→动作映射表与呈现映射（UX-03 正常
 *         取消无错误呈现）、比较型三要素数值＋单位同显与四态占位
 *         （ERR-01/MDL-06 不伪造数值）、恢复横幅（PM-15）、Tier-U
 *         日志面板（flush 仅关闭路径——模型无 flush 面）、脱敏仅转发
 *         （NFR-SEC-07 呈现层不自行脱敏）、确认投影经端口间接读（§9.2）、
 *         目录订阅 Marshal（§3.4 M-1）。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T13.json acceptance 1/4/5（逐条对应
 *     各用例 IRD_TEST_INFO 追溯字段）；knownPitfalls O-31（已裁决随整链
 *     放行——对端能力经 ui 自有端口替身承载）、P-UI-4/P-UI-7（建议
 *     口径）、P-PR-7（Bridge 线程模型——CommandInteractionBridge 套件）；
 *   - units/ui.md §9.1 全表、§10.8 契约表（本套件即其行为自证面）、
 *     §3.4/§3.5（Marshal/文案键）、§12.3 UI-DIA-1 行的模型半区观测点
 *     （GUI 呈现半区归 gui_test 层——UI-T14 承接）；
 *   - 先例：DraftControllerModelTest.cpp 的诊断全链＋可控替身注入形态。
 */

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QThread>

#include <sdurws/ird/diagnostics/Aggregation.hpp>  // CauseLink::chainOf（链缝的 L5 意向实现——真实设施驱动）
#include <sdurws/ird/diagnostics/Catalog.hpp>
#include <sdurws/ird/diagnostics/DiagCodes.hpp>
#include <sdurws/ird/diagnostics/Factory.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include <sdurws/ird/ui/IDiagnosticPresentationModel.hpp>
#include <sdurws/ird/ui/IWorkbenchShell.hpp>  // uiDiagnosticCodeDescriptors（§3.5 码表描述符供体——诊断全链装配）
#include <sdurws/ird/ui/UiPorts.hpp>
#include <sdurws/ird/ui/UiText.hpp>    // resolveText/notApplicableText（§3.5 唯一出口）
#include <sdurws/ird/ui/UiProjections.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace sdurws::ird;
using diagnostics::DiagProjectionItem;
using diagnostics::DiagQuery;
using ui::ComparisonText;
using ui::ComparisonValueText;
using ui::DiagAction;
using ui::DiagActionKind;
using ui::DiagPresentationRoute;

// =====================================================================
// 测试替身（与 DraftControllerModelTest 同款纪律——O-31 下端口以可控
// 替身承载，L5 装配期才以适配器绑定对端）
// =====================================================================

/// 开发日志记录器（快照失败/链回指的 Dev 出线观测面）。
class DevLogRecorder final : public diagnostics::IDevLogSink {
public:
    void logDev(std::string_view channel, std::string message) override
    {
        m_entries.emplace_back(std::string(channel), std::move(message));
    }
    bool seen(const std::string& needle) const
    {
        return std::any_of(m_entries.begin(), m_entries.end(),
                           [&needle](const auto& e) {
                               return e.second.find(needle) != std::string::npos;
                           });
    }

private:
    std::vector<std::pair<std::string, std::string>> m_entries;
};

/// 手动诊断时钟（工厂 emittedAtUtc 注入——确定性）。
class ManualDiagClock final : public diagnostics::IClock {
public:
    std::chrono::system_clock::time_point nowUtc() const override { return m_now; }
    std::chrono::system_clock::time_point m_now{std::chrono::seconds{1000000}};
};

/// 脱敏服务替身（§9.1 脱敏行——模型只转发不加工；替身输出确定性可断言：
/// 返回带标记的占位而非原文，转发链路一旦断裂测试即红）。
class RedactionStub final : public diagnostics::IRedactionService {
public:
    std::string redact(std::string_view raw, diagnostics::LogTier) const noexcept override
    {
        return "[R]" + std::string(raw);
    }
    std::string redactPath(std::string_view rawPath) const noexcept override
    {
        m_redactPathCalls.fetch_add(1);
        return "[P]" + std::string(rawPath);
    }
    std::string safeSummary(std::string_view raw, std::size_t) const noexcept override
    {
        return "[S]" + std::string(raw);
    }
    void setPolicy(const diagnostics::RedactionPolicy&) override { m_policyCalls++; }

    mutable std::atomic<int> m_redactPathCalls{0};   ///< redactPath 转发计数（转发面证据；mutable——const 接口内计数）
    int m_policyCalls = 0;                   ///< setPolicy 计数（模型侧应恒 0——配置写路径只经端口）
};

/// 确认投影呈现半区替身（C-9——§9.2"ui 经 project 间接读"的注入面）。
class FindingQueryStub final : public ui::IUiFindingQueryPort {
public:
    std::vector<diagnostics::FindingRecord> records;
    std::vector<diagnostics::FindingRecord> pendingConfirmations() const override
    {
        return records;
    }
};

/// Tier-U 日志数据源替身（§9.1 日志面板行）。
class UserLogStub final : public ui::IUiUserLogSource {
public:
    std::vector<ui::UserLogEntry> entries;
    bool devAvailable = false;
    int snapshotCalls = 0;

    std::vector<ui::UserLogEntry> snapshot() const override
    {
        m_calls.fetch_add(1);
        return entries;
    }
    bool devLogFileAvailable() const override { return devAvailable; }

private:
    mutable std::atomic<int> m_calls{0};

public:
    int callCount() const { return m_calls.load(); }
};

// =====================================================================
// 测试环境装配（诊断全链——DraftControllerModelTest 同案）
// =====================================================================

struct DiagHarness {
    diagnostics::StableCodeRegistry registry;
    ManualDiagClock diagClock;
    std::shared_ptr<diagnostics::DiagnosticsFactory> factory;
    std::shared_ptr<diagnostics::DiagCatalog> catalog;
    std::shared_ptr<DevLogRecorder> devLog = std::make_shared<DevLogRecorder>();
    std::shared_ptr<RedactionStub> redaction = std::make_shared<RedactionStub>();
    std::shared_ptr<FindingQueryStub> findingQuery = std::make_shared<FindingQueryStub>();
    std::shared_ptr<UserLogStub> userLog = std::make_shared<UserLogStub>();
    std::vector<diagnostics::DiagnosticEntry> appendedEntries;   ///< 链缝数据源（append 前另存——causedBy 只在信封上，投影不携带）
    bool wireChainSeam = true;                                   ///< 链缝开关（退化用例置 false）
    std::unique_ptr<ui::IDiagnosticPresentationModel> model;

    DiagHarness()
    {
        // 诊断全链（内建码表＋UI-* 九码收编——未注册码被工厂拒绝）。
        diagnostics::registerBuiltinCodes(registry);
        for (const auto& descriptor : ui::uiDiagnosticCodeDescriptors()) {
            registry.registerCode(descriptor);
        }
        factory = std::make_shared<diagnostics::DiagnosticsFactory>(registry, diagClock);
        catalog = std::make_shared<diagnostics::DiagCatalog>();
        rebuild();
    }

    void rebuild()
    {
        ui::DiagnosticPresentationDeps deps;
        deps.sink = catalog.get();
        deps.redaction = redaction.get();
        deps.findingQuery = findingQuery.get();
        deps.userLog = userLog.get();
        deps.devLog = devLog.get();
        if (wireChainSeam) {
            // 链查询缝（§16.7 v1.5 单侧冻结）的 L5 意向实现投影：持有
            // 条目视图的装配侧设施经 diagnostics CauseLink::chainOf 求
            // 全链（起点→根因）——本测试即用该真实设施驱动。
            deps.chainOf = [this](diagnostics::DiagEntryId start) {
                return diagnostics::CauseLink::chainOf(appendedEntries, start);
            };
        }
        model = ui::createDiagnosticPresentationModel(std::move(deps));
    }

    /// 造一条用户级记录（码必须已注册；context/cause/action 非空——C-3）。
    core::DiagnosticRecord makeRecord(const char* code, core::ObjectId subject)
    {
        return core::DiagnosticRecord::make(
            code, subject, std::string("obj-").append(32, 'a'), std::nullopt,
            "上下文描述", "原因描述", "建议动作", std::nullopt);
    }

    /// 造一条命令路径上下文（sourceUnit/sourceInterface 必填——工厂校验）。
    diagnostics::DiagContext makeContext()
    {
        diagnostics::DiagContext context;
        context.sourceUnit = "ui";
        context.sourceInterface = "presentation-test";
        return context;
    }

    /// 造条目并记录（append 前另存入链缝数据源——causedBy 只在信封
    /// DiagnosticEntry 上，投影不携带——§9.7 投影边界）。
    diagnostics::DiagEntryId appendEntry(diagnostics::DiagnosticEntry entry)
    {
        appendedEntries.push_back(entry);
        catalog->append(std::move(entry));
        return appendedEntries.back().entryId;
    }

    /// 造一条比较型记录（三要素——Confirmable 前置/三要素呈现测试用）。
    core::DiagnosticRecord makeComparativeRecord(const char* code, core::ObjectId subject)
    {
        core::ComparativeFields comparison;
        comparison.actual.quantity = core::SourcedValue<double>::provided(
            12.5, core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
        comparison.actual.unit = core::UnitToken::find("mm").value();
        comparison.expected.quantity = core::SourcedValue<double>::provided(
            10.0, core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
        comparison.expected.unit = core::UnitToken::find("mm").value();
        return core::DiagnosticRecord::make(
            code, subject, std::string("obj-").append(32, 'a'), std::nullopt,
            "上下文描述", "原因描述", "建议动作", comparison);
    }
};

// =====================================================================
// UI-DIA-1：过滤/去重折叠/原因链（§9.1 诊断表/详情行）
// =====================================================================

/** UI-DIA-1（acceptance 1）：query 透传 sink 过滤与稳定序——模型零再加工。 */
TEST(DiagPresentationModelTest, QueryForwardsFilterAndStableOrder_UI_DIA_1)
{
    IRD_TEST_INFO("UX-03", {}, std::nullopt);
    DiagHarness h;
    const auto subject = core::ObjectId::generate();

    // 两条不同码的用户级条目（同对象——不合并；入目录序＝稳定序基准）。
    h.catalog->append(h.factory->create(
        h.makeRecord("RT-INPUT-INVALID", subject), h.makeContext()));
    h.catalog->append(h.factory->create(
        h.makeRecord("RT-CANCELLED", subject), h.makeContext()));

    // 无过滤→全量，序＝目录稳定序（orderKey 升序——§6.4）。
    const auto all = h.model->query(DiagQuery{});
    ASSERT_EQ(all.size(), 2u);
    EXPECT_EQ(all[0].code, "RT-INPUT-INVALID");
    EXPECT_EQ(all[1].code, "RT-CANCELLED");

    // 按分类过滤→恰命中（过滤语义在 sink——§9.7，模型透传）。
    DiagQuery filtered;
    filtered.category = diagnostics::DiagnosticCategory::Canceled;
    const auto canceled = h.model->query(filtered);
    ASSERT_EQ(canceled.size(), 1u);
    EXPECT_EQ(canceled[0].code, "RT-CANCELLED");
}

/** UI-DIA-1：同 dedupKey 折叠为单条目＋occurrences 计数（§6.4 呈现面）。 */
TEST(DiagPresentationModelTest, DedupFoldShowsOccurrences_UI_DIA_1)
{
    IRD_TEST_INFO("UX-03", {"NFR-REL-05"}, std::nullopt);
    DiagHarness h;
    const auto subject = core::ObjectId::generate();

    // 同码同对象同上下文两次命中——目录去重（append 即去重，§6.4）：
    // 呈现面只见单条目＋occurrences=2（"折叠显示 occurrences"原文）。
    h.catalog->append(h.factory->create(
        h.makeRecord("RT-INPUT-INVALID", subject), h.makeContext()));
    h.catalog->append(h.factory->create(
        h.makeRecord("RT-INPUT-INVALID", subject), h.makeContext()));

    const auto items = h.model->query(DiagQuery{});
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items[0].occurrences, 2u);
}

/** UI-DIA-1：expandChain 沿 causedBy 单父链逐级上溯（首元素＝起始条目）。 */
TEST(DiagPresentationModelTest, ExpandChainWalksCausedBy_UI_DIA_1)
{
    IRD_TEST_INFO("UX-03", {}, std::nullopt);
    DiagHarness h;
    const auto subject = core::ObjectId::generate();

    // 链例：root ← mid ← leaf（causedBy 指向已存在条目——§6.3 校验）。
    const auto rootId = h.appendEntry(h.factory->create(
        h.makeRecord("RT-WC-COMPILE-FAILED", subject), h.makeContext()));
    auto mid = h.factory->create(
        h.makeRecord("RT-WC-COMPILE-FAILED", core::ObjectId::generate()),
        h.makeContext());
    mid.causedBy = rootId;
    const auto midId = h.appendEntry(mid);
    auto leaf = h.factory->create(
        h.makeRecord("RT-INPUT-INVALID", core::ObjectId::generate()),
        h.makeContext());
    leaf.causedBy = midId;
    const auto leafId = h.appendEntry(leaf);

    // 链缝在位（L5 意向实现＝diagnostics CauseLink::chainOf）：起点→
    // 根因全链投影。
    const auto chain = h.model->expandChain(leafId);
    ASSERT_EQ(chain.size(), 3u);
    EXPECT_EQ(chain[0].entryId, leafId);
    EXPECT_EQ(chain[1].entryId, midId);
    EXPECT_EQ(chain[2].entryId, rootId);

    // 不存在条目→空集（不虚构链——§10.8 错误类型行精神）。
    EXPECT_TRUE(h.model->expandChain(99999).empty());
}

/** UI-DIA-1：链缝缺席（装配缺省）→退化"起点自身即根"——零虚构。 */
TEST(DiagPresentationModelTest, ExpandChainDegradesWithoutSeam_UI_DIA_1)
{
    IRD_TEST_INFO("UX-03", {}, std::nullopt);
    DiagHarness h;
    h.wireChainSeam = false;   // 显式声明缺省装配
    h.rebuild();

    const auto rootId = h.appendEntry(h.factory->create(
        h.makeRecord("RT-WC-COMPILE-FAILED", core::ObjectId::generate()),
        h.makeContext()));

    // 无缝：起点存在→单条目链（自身即根——§6.3 rootOf 退化形态）。
    const auto chain = h.model->expandChain(rootId);
    ASSERT_EQ(chain.size(), 1u);
    EXPECT_EQ(chain[0].entryId, rootId);
    // 不存在→空集。
    EXPECT_TRUE(h.model->expandChain(424242).empty());
}

// =====================================================================
// §9.1 动作映射表与呈现映射（全部路由跳转/命令，不直调领域服务）
// =====================================================================

/** §9.1 动作行原文五条＋none＋未知族兜底（映射表冻结断言）。 */
TEST(DiagPresentationModelTest, ActionMappingTableFrozen_UI_DIA_1)
{
    IRD_TEST_INFO("UX-03", {}, std::nullopt);
    // §9.1 动作行逐字对应：fix-input→跳转编辑；contact-holder→显示 PID；
    // rerun-interrupted→重跑入口；confirm-or-fix→确认对话；inspect-log→
    // 日志页。
    EXPECT_EQ(ui::diagActionFor("fix-input").kind,
              DiagActionKind::NavigateToEdit);
    EXPECT_EQ(ui::diagActionFor("contact-holder").kind,
              DiagActionKind::ShowLockHolder);
    EXPECT_EQ(ui::diagActionFor("rerun-interrupted").kind,
              DiagActionKind::OpenRerunEntry);
    EXPECT_EQ(ui::diagActionFor("confirm-or-fix").kind,
              DiagActionKind::OpenConfirmDialog);
    EXPECT_EQ(ui::diagActionFor("inspect-log").kind,
              DiagActionKind::OpenLogPanel);
    // UX-03：正常取消无动作（"none"族）。
    EXPECT_EQ(ui::diagActionFor("none").kind, DiagActionKind::None);
    // 未知族（词表演进前的安全兜底）——不虚构动作。
    EXPECT_EQ(ui::diagActionFor("not-a-known-family").kind, DiagActionKind::None);
}

/** §4.4 全动作族 totality——diagnostics actionKindToken 全产出可映射。 */
TEST(DiagPresentationModelTest, ActionMappingCoversAllCategoryTokens_UI_DIA_1)
{
    IRD_TEST_INFO("ERR-01", {}, std::nullopt);
    // §4.3 全 15 分类经 actionKindToken 产出的动作族逐一经映射表——
    // "none" 之外全有呈现去向（totality：不抛、不 unknown-crash）。
    static constexpr diagnostics::DiagnosticCategory kAll[] = {
        diagnostics::DiagnosticCategory::InputInvalid,
        diagnostics::DiagnosticCategory::FormatOrVersion,
        diagnostics::DiagnosticCategory::PermissionOrLock,
        diagnostics::DiagnosticCategory::ResourceMissing,
        diagnostics::DiagnosticCategory::PolicyDenied,
        diagnostics::DiagnosticCategory::Confirmable,
        diagnostics::DiagnosticCategory::ExecutionFailed,
        diagnostics::DiagnosticCategory::Canceled,
        diagnostics::DiagnosticCategory::Interrupted,
        diagnostics::DiagnosticCategory::Timeout,
        diagnostics::DiagnosticCategory::DataInsufficient,
        diagnostics::DiagnosticCategory::EvidenceMissing,
        diagnostics::DiagnosticCategory::InfeasibilityProof,
        diagnostics::DiagnosticCategory::Internal,
        diagnostics::DiagnosticCategory::SecurityOrRedaction,
    };
    for (const auto category : kAll) {
        const DiagAction action =
            ui::diagActionFor(std::string(diagnostics::actionKindToken(category)));
        if (category == diagnostics::DiagnosticCategory::Canceled) {
            EXPECT_EQ(action.kind, DiagActionKind::None);   // 取消无动作（UX-03）
        } else {
            EXPECT_NE(action.kind, DiagActionKind::None);
        }
    }
}

/** §9.1 呈现映射：取消→无错误呈现（UX-03）；其余逐行锚定。 */
TEST(DiagPresentationModelTest, PresentationRouteCancelHasNoErrorSurface_UX_03)
{
    IRD_TEST_INFO("UX-03", {}, std::nullopt);
    // 呈现映射（§9.1"呈现映射"段原文逐行）：
    EXPECT_EQ(ui::diagnosticRouteFor(diagnostics::DiagnosticCategory::InputInvalid),
              DiagPresentationRoute::FailureWithObjectFocus);   // 输入非法→失败态＋对象定位
    EXPECT_EQ(ui::diagnosticRouteFor(diagnostics::DiagnosticCategory::PermissionOrLock),
              DiagPresentationRoute::ReadOnlyBanner);           // 权限或锁→只读横幅
    EXPECT_EQ(ui::diagnosticRouteFor(diagnostics::DiagnosticCategory::Confirmable),
              DiagPresentationRoute::ConfirmDialog);            // 可确认→确认对话
    EXPECT_EQ(ui::diagnosticRouteFor(diagnostics::DiagnosticCategory::Canceled),
              DiagPresentationRoute::NoErrorPresentation);      // 取消→无错误呈现
    EXPECT_EQ(ui::diagnosticRouteFor(diagnostics::DiagnosticCategory::Interrupted),
              DiagPresentationRoute::InterruptedRerunnable);    // 中断→"已中断"可重跑
    EXPECT_EQ(ui::diagnosticRouteFor(diagnostics::DiagnosticCategory::Internal),
              DiagPresentationRoute::DevLogOrRecoveryBanner);   // 内部→开发日志/恢复横幅
    EXPECT_EQ(ui::diagnosticRouteFor(diagnostics::DiagnosticCategory::InfeasibilityProof),
              DiagPresentationRoute::NoErrorPresentation);      // 有效结论非错误
}

// =====================================================================
// §9.1 比较型三要素（数值＋单位同显；四态占位不伪造数值）
// =====================================================================

/** UI-DIA-1：三要素四态呈现——同显/不适用/无效保留原串/未提供。 */
TEST(DiagPresentationModelTest, ComparisonValuesUnitCoDisplayAndPlaceholders_UI_DIA_1)
{
    IRD_TEST_INFO("UX-03", {"ERR-01", "MDL-06"}, std::nullopt);
    const auto provenance =
        core::ValueProvenance::make(core::ProvenanceKind::UserProvided);

    // Provided：数值＋单位同显（KIN-12 纪律——"12.5 mm"一串呈现）。
    core::ComparativeValue provided;
    provided.quantity = core::SourcedValue<double>::provided(12.5, provenance);
    provided.unit = core::UnitToken::find("mm").value();
    const ComparisonValueText providedText = ui::formatComparisonValue(provided);
    EXPECT_EQ(providedText.state, core::FieldState::Provided);
    EXPECT_NE(providedText.text.find("12.5"), std::string::npos);
    EXPECT_NE(providedText.text.find("mm"), std::string::npos);

    // NotApplicable：「不适用」占位——不伪造 0（ERR-01）。
    core::ComparativeValue notApplicable;
    notApplicable.quantity = core::SourcedValue<double>::notApplicable();
    notApplicable.unit = core::UnitToken::find("mm").value();
    const ComparisonValueText naText = ui::formatComparisonValue(notApplicable);
    EXPECT_EQ(naText.state, core::FieldState::NotApplicable);
    EXPECT_EQ(naText.text, ui::notApplicableText());
    EXPECT_EQ(naText.text.find('0'), std::string::npos);

    // Invalid：「无效（原串）」——保留原文（NFR-COR-03 不得静默转 0）。
    core::ComparativeValue invalid;
    invalid.quantity = core::SourcedValue<double>::invalid("abc");
    invalid.unit = core::UnitToken::find("mm").value();
    const ComparisonValueText invalidText = ui::formatComparisonValue(invalid);
    EXPECT_EQ(invalidText.state, core::FieldState::Invalid);
    EXPECT_NE(invalidText.text.find("abc"), std::string::npos);

    // NotProvided：「未提供」（MDL-06 降级语义的事实呈现）。
    core::ComparativeValue notProvided;
    notProvided.quantity = core::SourcedValue<double>::notProvided();
    notProvided.unit = core::UnitToken::find("mm").value();
    const ComparisonValueText npText = ui::formatComparisonValue(notProvided);
    EXPECT_EQ(npText.state, core::FieldState::NotProvided);
    EXPECT_EQ(npText.text, ui::resolveText("ui.diag.comparison.not-provided"));

    // 两侧装配（实际/期望同行产出——确认对话/详情共用）。
    core::ComparativeFields fields;
    fields.actual = provided;
    fields.expected = notApplicable;
    const ComparisonText both = ui::formatComparison(fields);
    EXPECT_EQ(both.actual.state, core::FieldState::Provided);
    EXPECT_EQ(both.expected.state, core::FieldState::NotApplicable);
}

// =====================================================================
// §9.1 恢复横幅（PM-15）与 Tier-U 日志面板
// =====================================================================

/** PM-15：恢复横幅一句话汇总择一＋详情行＋固定三动作。 */
TEST(DiagPresentationModelTest, RecoveryBannerSummaryAndActions_PM_15)
{
    IRD_TEST_INFO("PM-15", {}, std::nullopt);
    // 三事实皆空→不呈现（不虚构恢复叙事）。
    const auto empty = ui::assembleRecoveryBanner(false, false, 0);
    EXPECT_FALSE(empty.any);

    // 单事实：未保存草稿——主文案含 {0} 计数占位，参数面随投影携带。
    const auto draftsOnly = ui::assembleRecoveryBanner(false, false, 3);
    ASSERT_TRUE(draftsOnly.any);
    EXPECT_EQ(draftsOnly.summaryKey, "ui.recovery.banner.summary.orphan-drafts");
    ASSERT_EQ(draftsOnly.summaryArgs.size(), 1u);
    EXPECT_EQ(ui::resolveText(draftsOnly.summaryKey, draftsOnly.summaryArgs),
              "检测到 3 份未保存草稿");
    EXPECT_TRUE(draftsOnly.detailKeys.empty());

    // 多事实：忽略保存为主（紧迫度最高），其余降级详情行——横幅只一句话。
    const auto multi = ui::assembleRecoveryBanner(true, true, 2);
    ASSERT_TRUE(multi.any);
    EXPECT_EQ(multi.summaryKey, "ui.recovery.banner.summary.ignored-saves");
    ASSERT_EQ(multi.detailKeys.size(), 2u);
    // 固定三动作键（查看详情/恢复草稿/放弃——§9.1 原文）。
    EXPECT_EQ(multi.actionDetailsKey, "ui.recovery.banner.action.details");
    EXPECT_EQ(multi.actionRestoreKey, "ui.recovery.banner.action.restore");
    EXPECT_EQ(multi.actionDiscardKey, "ui.recovery.banner.action.discard");
    // 全部键可解析（键值分离——值源登记无缺键）。
    EXPECT_NO_THROW(ui::resolveText(multi.summaryKey));
    for (const auto& key : multi.detailKeys) {
        EXPECT_NO_THROW(ui::resolveText(key));
    }
}

/** §9.1 日志面板：Tier-U 快照直通＋Dev 仅跳转位＋空源占位。 */
TEST(DiagPresentationModelTest, UserLogPanelSnapshotAndDevJump_UI_DIA_1)
{
    IRD_TEST_INFO("NFR-REL-05", {}, std::nullopt);
    DiagHarness h;
    h.userLog->entries.push_back(ui::UserLogEntry{
        "RT-CANCELLED", "diag.rt-cancelled.title", {},
        std::chrono::system_clock::now()});
    h.userLog->devAvailable = true;

    const auto panel = ui::assembleUserLogPanel(h.userLog.get());
    ASSERT_EQ(panel.entries.size(), 1u);
    EXPECT_TRUE(panel.devFileJumpAvailable);   // Dev 级仅"打开开发日志文件"跳转
    EXPECT_EQ(panel.devJumpLabelKey, "ui.logpanel.dev.jump");

    // 无数据源＝空面板占位（不虚构条目）。
    const auto emptyPanel = ui::assembleUserLogPanel(nullptr);
    EXPECT_TRUE(emptyPanel.entries.empty());
    EXPECT_FALSE(emptyPanel.devFileJumpAvailable);
    EXPECT_EQ(emptyPanel.emptyPanelKey, "ui.logpanel.empty");
}

// =====================================================================
// §9.2 确认投影呈现半区（经 project 间接读）＋脱敏转发
// =====================================================================

/** §9.2：pendingConfirmations 经端口透传；无端口＝空集（零虚构）。 */
TEST(DiagPresentationModelTest, PendingConfirmationsViaPortOnly_UI_CMD_4)
{
    IRD_TEST_INFO("SA-15", {}, std::nullopt);
    DiagHarness h;
    // 端口在位：替身返回一条记录——模型透传（不调 IConfirmableFinding
    // Service 服务端接口——ui 自有端口是唯一路径，§9.2 规则表）。
    h.findingQuery->records.resize(1);
    EXPECT_EQ(h.model->pendingConfirmations().size(), 1u);

    // 端口缺席（装配显式声明）→空集，不虚构确认面。
    ui::DiagnosticPresentationDeps deps;
    deps.sink = h.catalog.get();
    deps.redaction = h.redaction.get();
    deps.findingQuery = nullptr;   // 显式空
    deps.userLog = h.userLog.get();
    deps.devLog = h.devLog.get();
    const auto bare = ui::createDiagnosticPresentationModel(std::move(deps));
    EXPECT_TRUE(bare->pendingConfirmations().empty());
}

/** NFR-SEC-07：redactedPath 只转发服务（模型零自行脱敏/零缓存）。 */
TEST(DiagPresentationModelTest, RedactedPathForwardsService_NFR_SEC_07)
{
    IRD_TEST_INFO("NFR-SEC-07", {}, std::nullopt);
    DiagHarness h;
    // 替身输出带 [P] 标记——转发链路断裂（模型自行加工或返回原文）即红。
    const std::string result =
        h.model->redactedPath("D:\\data\\proj\\model.stl");
    EXPECT_EQ(result, "[P]D:\\data\\proj\\model.stl");
    EXPECT_GE(h.redaction->m_redactPathCalls.load(), 1);

    // 装配缺脱敏服务→fail-fast（无服务不得呈现原文路径）。
    EXPECT_THROW(
        [] {
            DiagHarness brokenHarness;
            ui::DiagnosticPresentationDeps broken;
            broken.sink = brokenHarness.catalog.get();
            broken.redaction = nullptr;
            (void)ui::createDiagnosticPresentationModel(std::move(broken));
        }(),
        std::invalid_argument);

    // 装配缺 sink→fail-fast（无数据源）。
    EXPECT_THROW(
        [] {
            DiagHarness noSinkHarness;
            ui::DiagnosticPresentationDeps noSink;
            noSink.sink = nullptr;
            noSink.redaction = noSinkHarness.redaction.get();
            (void)ui::createDiagnosticPresentationModel(std::move(noSink));
        }(),
        std::invalid_argument);
}

// =====================================================================
// §10.8 subscribe（目录变更→Marshal→UI 线程分发——§3.4 M-1）
// =====================================================================

/** M-1：目录通知线程的回调被 Marshal 到 UI 线程；退订后在途通知丢弃。 */
TEST(DiagPresentationModelTest, SubscribeMarshalToUiThreadAndUnsubscribe_M_1)
{
    IRD_TEST_INFO("UX-03", {}, std::nullopt);
    DiagHarness h;
    const QThread* const uiThread = QThread::currentThread();

    class RecordingObserver final : public diagnostics::IDiagObserver {
    public:
        std::atomic<int> calls{0};
        const QThread* callbackThread = nullptr;
        void onCatalogChanged() override
        {
            callbackThread = QThread::currentThread();
            calls.fetch_add(1);
        }
    };
    RecordingObserver observer;
    const auto subscription = h.model->subscribe(observer);

    // 后台线程触发目录变更（append 的通知在调用方线程同步派发——阶段 A
    // 口径，§9.7）：回调必须经 Marshal 在 UI 线程执行。
    {
        std::thread appender([&h] {
            const auto subject = core::ObjectId::generate();
            h.catalog->append(h.factory->create(
                h.makeRecord("RT-CANCELLED", subject), h.makeContext()));
        });
        appender.join();
    }
    const int spinGuard = 200;
    for (int i = 0; i < spinGuard && observer.calls.load() == 0; ++i) {
        QCoreApplication::processEvents();   // 排队投递的泵送（不使用 sleep 判据）
    }
    EXPECT_EQ(observer.calls.load(), 1);
    EXPECT_EQ(observer.callbackThread, uiThread);

    // 退订→再变更→无回调（RAII 句柄 unsubscribe 幂等）。
    subscription->unsubscribe();
    {
        std::thread appender([&h] {
            const auto subject = core::ObjectId::generate();
            h.catalog->append(h.factory->create(
                h.makeRecord("RT-CANCELLED", subject), h.makeContext()));
        });
        appender.join();
    }
    for (int i = 0; i < spinGuard; ++i) {
        QCoreApplication::processEvents();
    }
    EXPECT_EQ(observer.calls.load(), 1);
}

}  // namespace
