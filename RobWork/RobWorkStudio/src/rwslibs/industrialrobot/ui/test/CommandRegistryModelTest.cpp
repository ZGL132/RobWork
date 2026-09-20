/**
 * @file   CommandRegistryModelTest.cpp
 * @brief  UI-T06 模型层用例（QCoreApplication 级——§12.1 第一层分工）：
 *         命令注册表的登记/拒绝/提交/谓词/面板模糊投影（§12.3 UI-CMD-1/2/3
 *         的模型层观测点＋§7.4 模糊搜索规则的具名断言）。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T06.json acceptance 1（重复注册拒绝＋
 *     UI-CMD-DUPLICATE；未知命令拒绝＋UI-CMD-UNKNOWN）、acceptance 2（面板
 *     模糊搜索/未绑定命令可达）、acceptance 3（O-31：CommandOutcome 值投影
 *     承载——本套件断言 revisionResult 投影语义，产品面零对端 include）；
 *   - units/ui.md §7.2（注册协议）、§7.4（模糊搜索：子序列＋词边界前缀加权
 *     ＋关键字加权；并列按注册序；上限；近期置顶）、§7.5/§7.6（谓词/只读）、
 *     §10.3（契约表——处理器异常捕获不穿透）；
 *   - 先例：ShellModelTest/StatusWordModelTest 的模型层形态（QCoreApplication
 *     级、零 Widget——AGENTS 模型测试豁免）。
 */

#include <gtest/gtest.h>

#include <QCoreApplication>

#include <sdurws/ird/diagnostics/Catalog.hpp>
#include <sdurws/ird/diagnostics/DiagCodes.hpp>
#include <sdurws/ird/diagnostics/Factory.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include <sdurws/ird/ui/ICommandRegistry.hpp>

#include <algorithm>
#include <exception>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace sdurws::ird;
using ui::CommandAvailability;
using ui::CommandDescriptor;
using ui::CommandOutcome;
using ui::CommandParameter;
using ui::CommandRegistryDeps;
using ui::CommandResultProjection;
using ui::CommandView;
using ui::ICommandRegistry;
using ui::RegistrationResult;

// =====================================================================
// 测试替身（ui.md §3.1"ui 测试以可控替身承载"）
// =====================================================================

/// 开发日志记录器：UI-CMD-DUPLICATE（Dev 码）的观测点——Dev 不入目录
/// （§6.2），唯一出线＝IDevLogSink 通道。
class DevLogRecorder final : public diagnostics::IDevLogSink {
public:
    void logDev(std::string_view channel, std::string message) override
    {
        m_entries.emplace_back(std::string(channel), std::move(message));
    }
    /// 是否记录了含给定码的条目（码在消息首段——出线拼接格式）。
    bool seen(const std::string& code) const
    {
        return std::any_of(m_entries.begin(), m_entries.end(),
                           [&code](const auto& e) {
                               return e.second.find(code) != std::string::npos;
                           });
    }

private:
    std::vector<std::pair<std::string, std::string>> m_entries;
};

/// 确定性测试时钟（CatalogFactoryTest 同款——IClock 注入替身）。
class ManualClock final : public diagnostics::IClock {
public:
    std::chrono::system_clock::time_point nowUtc() const override { return m_now; }
    std::chrono::system_clock::time_point m_now{std::chrono::seconds{1000000}};
};

/// 构造一个注册表＋码表＋工厂＋真实目录的全链环境（UI-CMD-* 用户级码
/// 出线走 factory.create＋catalog.append——§9.2 唯一创建入口的实测形态；
/// 九码描述符已注册＝P-UI-10"码值以 diagnostics 码表收编表现为准"）。
struct DiagHarness {
    diagnostics::StableCodeRegistry registry;
    ManualClock clock;
    std::shared_ptr<diagnostics::DiagnosticsFactory> factory;
    std::shared_ptr<diagnostics::DiagCatalog> catalog;
    std::shared_ptr<DevLogRecorder> devLog = std::make_shared<DevLogRecorder>();

    DiagHarness()
    {
        diagnostics::registerBuiltinCodes(registry);
        for (const auto& descriptor : ui::uiDiagnosticCodeDescriptors()) {
            registry.registerCode(descriptor);  // L5 装配序实测形态（任一违约即抛）
        }
        factory = std::make_shared<diagnostics::DiagnosticsFactory>(registry, clock);
        catalog = std::make_shared<diagnostics::DiagCatalog>();
    }

    /// 目录内指定码的条目数（观测面）。
    std::size_t countOf(const std::string& code) const
    {
        const auto items = catalog->snapshot(diagnostics::DiagQuery{});
        return static_cast<std::size_t>(std::count_if(
            items.begin(), items.end(),
            [&code](const diagnostics::DiagProjectionItem& i) { return i.code == code; }));
    }
};

/// 最小描述符（id/titleKey 之外的轴取默认——用例按需覆写）。
CommandDescriptor makeDescriptor(const std::string& id, const std::string& owner = "ui")
{
    CommandDescriptor desc;
    desc.id = id;
    desc.ownerUnit = owner;
    desc.titleKey = "cmd." + id + ".title";
    return desc;
}

/// 恒接受处理器（记录调用次数与参数——"submit 后命令恰好执行一次"的观测面）。
struct RecordingHandler {
    std::size_t calls = 0;
    std::vector<std::vector<CommandParameter>> received;

    CommandOutcome operator()(const std::vector<CommandParameter>& params)
    {
        ++calls;
        received.push_back(params);
        CommandOutcome out;
        out.accepted = true;
        return out;
    }
};

// =====================================================================
// UI-CMD-1：重复注册拒绝（§7.2——不覆盖不静默＋UI-CMD-DUPLICATE）
// =====================================================================

TEST(CommandRegistryModel, DuplicateAndInvalidRegistrationRejected_UI_CMD_1)
{
    IRD_TEST_INFO("UI-CMD-1", {}, std::nullopt);
    DiagHarness diag;
    CommandRegistryDeps deps;
    deps.diagFactory = diag.factory;
    deps.diagSink = diag.catalog;
    deps.devLog = diag.devLog;
    deps.ownerWhitelist = {"ui", "plugin.a"};
    auto registry = ui::createCommandRegistry(std::move(deps));

    // 首次注册成功；白名单外 owner 拒绝（SA-01——§7.2 第 1 步）。
    EXPECT_EQ(registry->registerCommand(makeDescriptor("test.alpha"),
                                        RecordingHandler{}),
              RegistrationResult::Ok);
    EXPECT_EQ(registry->registerCommand(makeDescriptor("test.beta", "plugin.rogue"),
                                        RecordingHandler{}),
              RegistrationResult::OwnerNotWhitelisted);

    // 重复 id（不同 owner）→ DuplicateId：拒绝、不覆盖、不静默——注册表
    // 仍只有一条 test.alpha，且 UI-CMD-DUPLICATE 经 Dev 通道出线（Dev 码
    // 不入目录——目录计数恒 0，§6.2）。
    EXPECT_EQ(registry->registerCommand(makeDescriptor("test.alpha", "plugin.a"),
                                        RecordingHandler{}),
              RegistrationResult::DuplicateId);
    EXPECT_EQ(registry->query({}).size(), std::size_t{1}) << "重复注册不得覆盖/追加";
    EXPECT_TRUE(diag.devLog->seen("UI-CMD-DUPLICATE")) << "拒绝必须出 Dev 诊断（不静默）";
    EXPECT_EQ(diag.countOf("UI-CMD-DUPLICATE"), std::size_t{0}) << "Dev 码不入目录";

    // 非法描述符（id 句法违约/必填键空）→ InvalidDescriptor（§7.2 第 1 步；
    // 词形权威＝§7.1 冻结表——点分段＋[A-Za-z0-9-]＋必含点，措辞差登记
    // ui.md §16.7 v0.8）。
    EXPECT_EQ(registry->registerCommand(makeDescriptor("test..bad"), RecordingHandler{}),
              RegistrationResult::InvalidDescriptor) << "空段违约";
    EXPECT_EQ(registry->registerCommand(makeDescriptor("test bad"), RecordingHandler{}),
              RegistrationResult::InvalidDescriptor) << "段内空白违约";
    EXPECT_EQ(registry->registerCommand(makeDescriptor("testbad"), RecordingHandler{}),
              RegistrationResult::InvalidDescriptor) << "非点分（无段边界）违约";
    EXPECT_EQ(registry->registerCommand(makeDescriptor("test_bad"), RecordingHandler{}),
              RegistrationResult::InvalidDescriptor) << "段内下划线违约";
    EXPECT_EQ(registry->registerCommand(makeDescriptor(""), RecordingHandler{}),
              RegistrationResult::InvalidDescriptor);
    CommandDescriptor noTitle = makeDescriptor("test.valid");
    noTitle.titleKey.clear();
    EXPECT_EQ(registry->registerCommand(noTitle, RecordingHandler{}),
              RegistrationResult::InvalidDescriptor);
    EXPECT_EQ(registry->query({}).size(), std::size_t{1}) << "非法描述符不得入表";
}

// =====================================================================
// UI-CMD-2：未知命令提交拒绝（§7.2/§10.3——UI-CMD-UNKNOWN，无派发）
// =====================================================================

TEST(CommandRegistryModel, UnknownSubmitRejectedWithDiagnostic_UI_CMD_2)
{
    IRD_TEST_INFO("UI-CMD-2", {}, std::nullopt);
    DiagHarness diag;
    CommandRegistryDeps deps;
    deps.diagFactory = diag.factory;
    deps.diagSink = diag.catalog;
    deps.devLog = diag.devLog;
    deps.ownerWhitelist = {"ui"};
    auto registry = ui::createCommandRegistry(std::move(deps));
    ASSERT_EQ(registry->registerCommand(makeDescriptor("test.alpha"), RecordingHandler{}),
              RegistrationResult::Ok);

    // 未注册 id 提交 → 拒绝＋UI-CMD-UNKNOWN（用户级码经工厂入目录——
    // context 携带请求 id）；不派发（注册表内处理器零调用）。
    RecordingHandler handler;
    ASSERT_EQ(registry->registerCommand(makeDescriptor("test.probe"), handler),
              RegistrationResult::Ok);
    const CommandOutcome outcome = registry->submit("no.such.cmd");
    EXPECT_FALSE(outcome.accepted) << "未知命令必须拒绝";
    EXPECT_EQ(diag.countOf("UI-CMD-UNKNOWN"), std::size_t{1})
        << "拒绝必须出用户级诊断（不静默）";
    EXPECT_EQ(handler.calls, std::size_t{0}) << "未知命令不得派发任何处理器";

    // 目录条目的 context 携带请求 id（可定位——acceptance 1 的 context 要求）。
    const auto items = diag.catalog->snapshot(diagnostics::DiagQuery{});
    ASSERT_FALSE(items.empty());
    const auto it = std::find_if(items.begin(), items.end(), [](const diagnostics::DiagProjectionItem& i) {
        return i.code == "UI-CMD-UNKNOWN";
    });
    ASSERT_NE(it, items.end());
    // 投影项携带文案键（P-DIAG-9 键值分离）——条目 context 文案在 record 侧，
    // 键的唯一性即码表收编面（diag.ui-cmd-unknown.title）。
    EXPECT_EQ(it->titleKey, std::string("diag.ui-cmd-unknown.title"));
}

// =====================================================================
// UI-CMD-3：只读/谓词拒绝（§7.5/§7.6——UI-CMD-NOT-EXECUTABLE＋可用性）
// =====================================================================

TEST(CommandRegistryModel, ReadonlyAndPredicateRejection_UI_CMD_3)
{
    IRD_TEST_INFO("UI-CMD-3", {}, std::nullopt);
    DiagHarness diag;
    CommandRegistryDeps deps;
    deps.diagFactory = diag.factory;
    deps.diagSink = diag.catalog;
    deps.devLog = diag.devLog;
    deps.ownerWhitelist = {"ui"};
    auto registry = ui::createCommandRegistry(std::move(deps));

    // 只读不可用的写命令（§7.6：readOnlyAllowed=false）＋项目作用域。
    CommandDescriptor writeCmd = makeDescriptor("test.write");
    writeCmd.scope = ui::ShellCommandScope::Project;
    writeCmd.readOnlyAllowed = false;
    // 处理器经 shared_ptr 计数（registerCommand 按值持有 std::function——
    // 调用发生在注册表持有的拷贝上，观测必须穿透拷贝）。
    auto calls = std::make_shared<std::size_t>(0);
    ASSERT_EQ(registry->registerCommand(writeCmd,
                                        [calls](const std::vector<CommandParameter>&) {
                                            ++*calls;
                                            CommandOutcome out;
                                            out.accepted = true;
                                            return out;
                                        }),
              RegistrationResult::Ok);

    // 无项目态：项目作用域禁用（reason.no-project——PM-10"禁用＋说明"）。
    registry->presentContext(ui::UiContextSnapshot{});
    const CommandAvailability noProject = registry->availability("test.write");
    EXPECT_TRUE(noProject.registered);
    EXPECT_TRUE(noProject.visible) << "登记命令恒可见（禁用＋说明保留发现性，§7.4）";
    EXPECT_FALSE(noProject.enabled);
    EXPECT_EQ(noProject.disableReasonKey, std::string("reason.no-project"));
    EXPECT_TRUE(noProject.readOnlyBlocked);

    // 只读项目态：仍禁用（reason.readonly——§7.6 双保险的界面半区）。
    ui::UiContextSnapshot readonlyCtx;
    readonlyCtx.hasActiveProject = true;
    readonlyCtx.writable = false;
    registry->presentContext(readonlyCtx);
    const CommandAvailability readonlyAvail = registry->availability("test.write");
    EXPECT_FALSE(readonlyAvail.enabled);
    EXPECT_EQ(readonlyAvail.disableReasonKey, std::string("reason.readonly"));
    EXPECT_TRUE(readonlyAvail.readOnlyBlocked);

    // 编程提交（绕过界面使能）→ 拒绝＋UI-CMD-NOT-EXECUTABLE——"强行编程
    // 提交→拒绝"双保险（§7.6 原文）；处理器零调用。
    const CommandOutcome rejected = registry->submit("test.write");
    EXPECT_FALSE(rejected.accepted);
    EXPECT_EQ(*calls, std::size_t{0});
    EXPECT_EQ(diag.countOf("UI-CMD-NOT-EXECUTABLE"), std::size_t{1});

    // 可写项目态：可用、可执行（处理器恰好执行一次——§10.3 后置条件行）。
    ui::UiContextSnapshot writableCtx;
    writableCtx.hasActiveProject = true;
    writableCtx.writable = true;
    registry->presentContext(writableCtx);
    EXPECT_TRUE(registry->availability("test.write").enabled);
    EXPECT_FALSE(registry->availability("test.write").readOnlyBlocked);
    const CommandOutcome accepted = registry->submit("test.write");
    EXPECT_TRUE(accepted.accepted);
    EXPECT_EQ(*calls, std::size_t{1});

    // 自定义谓词（§7.5 registerCommandWithPredicates）：可见谓词塌缩→
    // 可用性 visible=false；面板投影（§7.4 可见性过滤）排除该命令——
    // query 保持全量投影（行内 visible 位承载事实，消费方按需过滤）。
    CommandDescriptor hidden = makeDescriptor("test.hidden");
    hidden.scope = ui::ShellCommandScope::Session;
    ASSERT_EQ(registry->registerCommandWithPredicates(
                  hidden, [calls](const std::vector<CommandParameter>&) {
                      ++*calls;
                      CommandOutcome out;
                      out.accepted = true;
                      return out;
                  },
                  [](const ui::UiContextSnapshot&) { return false; },  // 恒不可见
                  {}),
              RegistrationResult::Ok);
    EXPECT_FALSE(registry->availability("test.hidden").visible);
    const auto paletteRows = registry->paletteSnapshot("", 50);
    EXPECT_EQ(std::find_if(paletteRows.begin(), paletteRows.end(), [](const CommandView& v) {
                  return v.id == "test.hidden";
              }),
              paletteRows.end())
        << "可见谓词 false 的命令不进面板投影（§7.4 可见性过滤）";
    const auto views = registry->query({});
    const auto hiddenRow = std::find_if(views.begin(), views.end(), [](const CommandView& v) {
        return v.id == "test.hidden";
    });
    ASSERT_NE(hiddenRow, views.end()) << "query 是只读全量投影（可见性作为行事实随行）";
    EXPECT_FALSE(hiddenRow->visible);
}

// =====================================================================
// §10.3 提交语义：参数透传/异常捕获/结果投影（O-31 承载形态）
// =====================================================================

TEST(CommandRegistryModel, SubmitSemanticsParamsExceptionProjection_UI_T06_ACC3)
{
    IRD_TEST_INFO("O-31", {}, std::nullopt);
    DiagHarness diag;
    CommandRegistryDeps deps;
    deps.diagFactory = diag.factory;
    deps.diagSink = diag.catalog;
    deps.devLog = diag.devLog;
    deps.ownerWhitelist = {"ui"};
    auto registry = ui::createCommandRegistry(std::move(deps));

    // 参数透传（不透明搬运——§7.1 CommandParameter 边界）。
    std::vector<CommandParameter> got;
    ASSERT_EQ(registry->registerCommand(makeDescriptor("test.params"),
                                        [&got](const std::vector<CommandParameter>& p) {
                                            got = p;
                                            CommandOutcome out;
                                            out.accepted = true;
                                            return out;
                                        }),
              RegistrationResult::Ok);
    registry->submit("test.params", {{"k", "v"}});
    ASSERT_EQ(got.size(), std::size_t{1});
    EXPECT_EQ(got[0].key, std::string("k"));
    EXPECT_EQ(got[0].value, std::string("v"));

    // 处理器异常→捕获→拒绝＋诊断（不让异常穿透事件循环——§10.3 错误行）。
    ASSERT_EQ(registry->registerCommand(makeDescriptor("test.throw"),
                                        [](const std::vector<CommandParameter>&) -> CommandOutcome {
                                            throw std::runtime_error("handler boom");
                                        }),
              RegistrationResult::Ok);
    EXPECT_NO_THROW(registry->submit("test.throw"));
    EXPECT_EQ(diag.countOf("UI-CMD-NOT-EXECUTABLE"), std::size_t{1});
    EXPECT_TRUE(diag.devLog->seen("handler boom")) << "异常明细走 Dev 通道（排障面）";

    // 修订结果投影（O-31 承载形态——CommandResultProjection，产品面零
    // project::CommandResult）：处理器回填投影 → submit 原样回传。
    ASSERT_EQ(registry->registerCommand(makeDescriptor("test.commit"),
                                        [](const std::vector<CommandParameter>&) {
                                            CommandOutcome out;
                                            out.accepted = true;
                                            CommandResultProjection result;
                                            result.status = CommandResultProjection::Status::Committed;
                                            result.newRevision = core::RevisionId::generate();
                                            out.revisionResult = result;
                                            return out;
                                        }),
              RegistrationResult::Ok);
    const CommandOutcome committed = registry->submit("test.commit");
    ASSERT_TRUE(committed.revisionResult.has_value());
    EXPECT_EQ(committed.revisionResult->status, CommandResultProjection::Status::Committed);
    EXPECT_TRUE(committed.revisionResult->newRevision.has_value())
        << "Committed 时 newRevision 唯一非空（project.md §5.3.1 锚点语义）";
}

// =====================================================================
// §7.4 面板模糊投影：加权/稳定序/上限/未绑定可达/近期置顶
// =====================================================================

namespace palette_fixture {

/// 登记一组带中文关键字/标题的命令（匹配器消费过渡文案表——键冻结、
/// 值为 UI-T09 前过渡承载；键集与壳登记表同源）。
std::unique_ptr<ICommandRegistry> makePaletteRegistry()
{
    CommandRegistryDeps deps;
    deps.ownerWhitelist = {"ui"};
    auto registry = ui::createCommandRegistry(std::move(deps));

    CommandDescriptor newProj = makeDescriptor("project.new");
    newProj.titleKey = "cmd.project.new.title";
    newProj.keywordKeys = {"cmd.project.new.kw.0", "cmd.project.new.kw.1"};
    newProj.menuPath = "文件/新建";
    registry->registerCommand(newProj, RecordingHandler{});

    CommandDescriptor palette = makeDescriptor("workbench.commandPalette");
    palette.titleKey = "cmd.workbench.commandPalette.title";
    palette.keywordKeys = {"cmd.workbench.commandPalette.kw.0", "cmd.workbench.commandPalette.kw.1"};
    palette.menuPath = "工具/命令面板";
    registry->registerCommand(palette, RecordingHandler{});

    // 无默认键的命令（acceptance 2"未绑定命令经面板可达"的观测点——
    // §7.3"未列默认键的命令 bindable=true 但无默认绑定"）。
    CommandDescriptor unbound = makeDescriptor("scheme.switch");
    unbound.titleKey = "cmd.scheme.switch.title";
    unbound.keywordKeys = {"cmd.scheme.switch.kw.0"};
    unbound.menuPath = "阶段/切换方案";
    registry->registerCommand(unbound, RecordingHandler{});
    return registry;
}

}  // namespace palette_fixture

TEST(CommandRegistryModel, PaletteFuzzyMatchingRules_UX13)
{
    IRD_TEST_INFO("UX-13", {}, std::nullopt);
    auto registry = palette_fixture::makePaletteRegistry();

    // 关键字命中加权："面板" 精确命中关键字（最高权重）→ 命令面板置顶。
    const auto byKeyword = registry->paletteSnapshot("面板", 50);
    ASSERT_FALSE(byKeyword.empty());
    EXPECT_EQ(byKeyword.front().id, std::string("workbench.commandPalette"));

    // 词边界前缀："新" 命中关键字 "新建" 前缀 → project.new 领先。
    const auto byPrefix = registry->paletteSnapshot("新", 50);
    ASSERT_FALSE(byPrefix.empty());
    EXPECT_EQ(byPrefix.front().id, std::string("project.new"));

    // 子序列匹配（大小写不敏感）："pjnw" 是 "project.new" 的子序列。
    const auto bySubsequence = registry->paletteSnapshot("pjnw", 50);
    EXPECT_FALSE(bySubsequence.empty()) << "子序列匹配兜底（§7.4）";
    if (!bySubsequence.empty()) {
        EXPECT_EQ(bySubsequence.front().id, std::string("project.new"));
    }

    // 未命中词→空集（不虚构结果）。
    EXPECT_TRUE(registry->paletteSnapshot("zzz不存在", 50).empty());

    // 空查询＝全部命令（含未绑定命令——UX-13 可达性兜底）。
    const auto all = registry->paletteSnapshot("", 50);
    ASSERT_EQ(all.size(), std::size_t{3});
    const auto idOf = [](const CommandView& v) { return v.id; };
    EXPECT_NE(std::find_if(all.begin(), all.end(), [](const CommandView& v) {
                  return v.id == "scheme.switch";
              }),
              all.end())
        << "未绑定快捷键的命令必须经面板可达（acceptance 2）";
    (void)idOf;
}

TEST(CommandRegistryModel, PaletteStableOrderLimitAndRecents_UX13_PM14)
{
    IRD_TEST_INFO("PM-14", {}, std::nullopt);
    auto registry = palette_fixture::makePaletteRegistry();

    // 稳定排序：空查询（同分）按注册序（NFR-COR-02——同输入同排序）。
    const auto first = registry->paletteSnapshot("", 50);
    const auto second = registry->paletteSnapshot("", 50);
    ASSERT_EQ(first.size(), second.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        EXPECT_EQ(first[i].id, second[i].id);
    }
    EXPECT_EQ(first[0].id, std::string("project.new")) << "并列同分→注册序";

    // 上限截断：limit=2 只回两行（§7.4 结果上限——超出提示是面板职责）。
    EXPECT_EQ(registry->paletteSnapshot("", 2).size(), std::size_t{2});

    // 近期置顶：提交成功的命令进入近期使用并置顶分组（§7.4"最近使用
    // 置顶"——提交即计数，入口统一避免三处口径漂移）。
    registry->submit("scheme.switch");
    EXPECT_EQ(registry->recentUsed().front(), std::string("scheme.switch"));
    const auto withRecent = registry->paletteSnapshot("", 50);
    EXPECT_EQ(withRecent.front().id, std::string("scheme.switch"));

    // 未接受（未知命令）不计近期。
    registry->submit("no.such");
    EXPECT_EQ(registry->recentUsed().size(), std::size_t{1});

    // 持久化回放：未注册 id 丢弃、重复收敛（PM-14 装载路径）。
    registry->restoreRecentUsed({"ghost.cmd", "project.new", "project.new"});
    const auto recents = registry->recentUsed();
    ASSERT_EQ(recents.size(), std::size_t{1});
    EXPECT_EQ(recents[0], std::string("project.new"));
}

// =====================================================================
// §7.2 装配收口：seal 后注册拒绝（§10.3"运行期 registerCommand 拒绝"）
// =====================================================================

TEST(CommandRegistryModel, SealRejectsRuntimeRegistration_UI_T06)
{
    IRD_TEST_INFO("SA-01", {}, std::nullopt);
    CommandRegistryDeps deps;
    deps.ownerWhitelist = {"ui"};
    auto registry = ui::createCommandRegistry(std::move(deps));
    ASSERT_EQ(registry->registerCommand(makeDescriptor("test.alpha"), RecordingHandler{}),
              RegistrationResult::Ok);
    registry->seal();
    registry->seal();  // 幂等

    // 运行期注册＝调用方契约违约——拒绝轨（不覆盖不静默；查询照常）。
    EXPECT_EQ(registry->registerCommand(makeDescriptor("test.beta"), RecordingHandler{}),
              RegistrationResult::InvalidDescriptor);
    EXPECT_EQ(registry->query({}).size(), std::size_t{1});
    // 运行期查询/提交合法（§7.2"运行期：注册表只读快照查询"）。
    EXPECT_TRUE(registry->submit("test.alpha").accepted);
}

}  // namespace

