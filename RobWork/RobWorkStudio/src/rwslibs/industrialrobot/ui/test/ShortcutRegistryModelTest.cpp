/**
 * @file   ShortcutRegistryModelTest.cpp
 * @brief  UI-T06 模型层用例（QCoreApplication 级——§12.1 第一层分工）：
 *         全局快捷键唯一注册点的冲突拒绝/改绑解绑/冲突流/持久化回放
 *         （§12.3 UI-HKY-1/2 的模型层观测点；UI-HKY-3 静态扫描见
 *         BuildRedLineTest——QShortcut 唯一创建点豁免面）。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T06.json acceptance 1（重复快捷键绑定在
 *     注册边界拒绝＋UI-HOTKEY-CONFLICT：冲突键＋已占用命令在 context、
 *     数值比较字段标「不适用」）、acceptance 2（rebind/解绑为用户级设置
 *     持久化——PM-14；解绑后命令仍面板可达）；
 *   - units/ui.md §7.3（注册/改绑规则原文）、§10.4（HotkeyResult 词表/
 *     前置校验/冲突订阅）、§3.5（UI-HOTKEY-CONFLICT 码语义——ERR-01
 *     非数值判定不伪造数值）；
 *   - 说明：本套件不调用 attach（QShortcut 需 QApplication GUI 环境——
 *     物理键行为由 GUI 套件 WorkbenchShellGuiTest/CommandPaletteGuiTest
 *     承载；模型层钉住键表逻辑，符合分层）。
 */

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QKeySequence>

#include <sdurws/ird/core/Provenance.hpp>
#include <sdurws/ird/diagnostics/Catalog.hpp>
#include <sdurws/ird/diagnostics/DiagCodes.hpp>
#include <sdurws/ird/diagnostics/Factory.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include <sdurws/ird/ui/IGlobalShortcutRegistry.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace sdurws::ird;
using ui::BindingOrigin;
using ui::CommandDescriptor;
using ui::CommandOutcome;
using ui::CommandParameter;
using ui::CommandRegistryDeps;
using ui::GlobalShortcutRegistryDeps;
using ui::HotkeyBinding;
using ui::HotkeyResult;
using ui::ICommandRegistry;
using ui::IGlobalShortcutRegistry;

// =====================================================================
// 测试替身（与 CommandRegistryModelTest 同款纪律）
// =====================================================================

class DevLogRecorder final : public diagnostics::IDevLogSink {
public:
    void logDev(std::string_view channel, std::string message) override
    {
        m_entries.emplace_back(std::string(channel), std::move(message));
    }
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

class ManualClock final : public diagnostics::IClock {
public:
    std::chrono::system_clock::time_point nowUtc() const override { return m_now; }
    std::chrono::system_clock::time_point m_now{std::chrono::seconds{1000000}};
};

/// 冲突流观察者（§10.4 subscribeConflictFeed 的观测替身）。
class FeedProbe final : public diagnostics::IDiagObserver {
public:
    void onCatalogChanged() override { ++notified; }
    int notified = 0;
};

/// 环境装配：已注册 test.alpha / test.beta（可绑定）＋test.frozen（不可
/// 绑定）＋全链诊断面（码表收编九码——P-UI-10 实测形态）。
struct ShortcutHarness {
    diagnostics::StableCodeRegistry registry;
    ManualClock clock;
    std::shared_ptr<diagnostics::DiagnosticsFactory> factory;
    std::shared_ptr<diagnostics::DiagCatalog> catalog;
    std::shared_ptr<DevLogRecorder> devLog = std::make_shared<DevLogRecorder>();
    std::unique_ptr<ICommandRegistry> commands;
    std::unique_ptr<IGlobalShortcutRegistry> shortcuts;
    std::vector<std::vector<HotkeyBinding>> persisted;  ///< 持久化回调捕获（PM-14 观测面）

    ShortcutHarness()
    {
        diagnostics::registerBuiltinCodes(registry);
        for (const auto& descriptor : ui::uiDiagnosticCodeDescriptors()) {
            registry.registerCode(descriptor);
        }
        factory = std::make_shared<diagnostics::DiagnosticsFactory>(registry, clock);
        catalog = std::make_shared<diagnostics::DiagCatalog>();

        CommandRegistryDeps commandDeps;
        commandDeps.ownerWhitelist = {"ui"};
        commands = ui::createCommandRegistry(std::move(commandDeps));
        for (const char* id : {"test.alpha", "test.beta"}) {
            CommandDescriptor desc;
            desc.id = id;
            desc.ownerUnit = "ui";
            desc.titleKey = std::string("cmd.") + id + ".title";
            EXPECT_EQ(commands->registerCommand(
                          desc, [](const std::vector<CommandParameter>&) {
                              CommandOutcome out;
                              out.accepted = true;
                              return out;
                          }),
                      ui::RegistrationResult::Ok);
        }
        CommandDescriptor frozen;
        frozen.id = "test.frozen";
        frozen.ownerUnit = "ui";
        frozen.titleKey = "cmd.test.frozen.title";
        frozen.bindable = false;  // §7.1 bindable 位——快捷键表拒绝、面板可达
        EXPECT_EQ(commands->registerCommand(
                      frozen, [](const std::vector<CommandParameter>&) {
                          CommandOutcome out;
                          out.accepted = true;
                          return out;
                      }),
                  ui::RegistrationResult::Ok);

        GlobalShortcutRegistryDeps deps;
        deps.commands = commands.get();
        deps.diagFactory = factory;
        deps.diagSink = catalog;
        deps.devLog = devLog;
        deps.onUserBindingsChanged = [this](const std::vector<HotkeyBinding>& user) {
            persisted.push_back(user);  // 每次成功改绑/解绑后回调（写盘快照）
        };
        shortcuts = ui::createGlobalShortcutRegistry(std::move(deps));
    }

    std::size_t countOf(const std::string& code) const
    {
        const auto items = catalog->snapshot(diagnostics::DiagQuery{});
        return static_cast<std::size_t>(std::count_if(
            items.begin(), items.end(),
            [&code](const diagnostics::DiagProjectionItem& i) { return i.code == code; }));
    }
};

// =====================================================================
// UI-HKY-1：冲突拒绝（§7.3——不覆盖不静默＋UI-HOTKEY-CONFLICT 比较型诊断）
// =====================================================================

TEST(ShortcutRegistryModel, ConflictRejectedWithDiagnostic_UI_HKY_1)
{
    IRD_TEST_INFO("UI-HKY-1", {}, std::nullopt);
    ShortcutHarness h;
    FeedProbe probe;
    const auto feed = h.shortcuts->subscribeConflictFeed(probe);

    // 默认集：Ctrl+S → test.alpha。
    ASSERT_TRUE(h.shortcuts->registerDefault("test.alpha", QKeySequence("Ctrl+S")).isOk());

    // 同键绑到另一命令 → Conflict{existingCommand}＋UI-HOTKEY-CONFLICT；
    // 不覆盖（lookup 仍指向 test.alpha；绑定表只有一条）。
    const HotkeyResult conflict =
        h.shortcuts->registerDefault("test.beta", QKeySequence("Ctrl+S"));
    EXPECT_EQ(conflict.kind(), HotkeyResult::Kind::Conflict);
    ASSERT_TRUE(conflict.existingCommand().has_value());
    EXPECT_EQ(*conflict.existingCommand(), std::string("test.alpha"));
    EXPECT_EQ(h.shortcuts->lookup(QKeySequence("Ctrl+S")).value_or("<none>"),
              std::string("test.alpha"));
    EXPECT_EQ(h.shortcuts->bindings().size(), std::size_t{1});

    // 诊断出线：用户级 Warning 入目录（一条）＋比较型字段「不适用」。
    EXPECT_EQ(h.countOf("UI-HOTKEY-CONFLICT"), std::size_t{1});
    const auto items = h.catalog->snapshot(diagnostics::DiagQuery{});
    const auto it = std::find_if(items.begin(), items.end(),
                                 [](const diagnostics::DiagProjectionItem& i) {
                                     return i.code == "UI-HOTKEY-CONFLICT";
                                 });
    ASSERT_NE(it, items.end());
    ASSERT_TRUE(it->comparison.has_value()) << "冲突诊断必须携带比较型字段（§3.5）";
    EXPECT_EQ(it->comparison->actual.quantity.state(), core::FieldState::NotApplicable);
    EXPECT_EQ(it->comparison->expected.quantity.state(), core::FieldState::NotApplicable);

    // 冲突流观察者收到通知（无载荷推送——订阅方经快照拉取，§10.4）。
    EXPECT_EQ(probe.notified, 1);
    feed->unsubscribe();
    (void)h.shortcuts->registerDefault("test.beta", QKeySequence("Ctrl+S"));
    EXPECT_EQ(probe.notified, 1) << "退订后不再通知（RAII 句柄幂等）";
}

TEST(ShortcutRegistryModel, PreconditionsUnknownAndNotBindable_UI_HKY_1b)
{
    IRD_TEST_INFO("UI-HKY-1", {}, std::nullopt);
    ShortcutHarness h;

    // 未注册命令 → UnknownCommand（§10.4 前置校验）。
    EXPECT_EQ(h.shortcuts->registerDefault("ghost.cmd", QKeySequence("Ctrl+G")).kind(),
              HotkeyResult::Kind::UnknownCommand);
    EXPECT_EQ(h.shortcuts->rebind("ghost.cmd", QKeySequence("Ctrl+G")).kind(),
              HotkeyResult::Kind::UnknownCommand);

    // bindable=false → NotBindable（§7.3——面板可达但快捷键表拒绝）。
    EXPECT_EQ(h.shortcuts->registerDefault("test.frozen", QKeySequence("Ctrl+F")).kind(),
              HotkeyResult::Kind::NotBindable);

    // 空键 → NotBindable（空键不是绑定——入参违约）。
    EXPECT_EQ(h.shortcuts->registerDefault("test.alpha", QKeySequence()).kind(),
              HotkeyResult::Kind::NotBindable);

    // 上述违约零表变更、零目录条目（拒绝在注册边界——不出用户级诊断；
    // Dev 通道有留痕的不只这两类，此处只断言目录干净）。
    EXPECT_EQ(h.shortcuts->bindings().size(), std::size_t{0});
    EXPECT_EQ(h.countOf("UI-HOTKEY-CONFLICT"), std::size_t{0});
}

// =====================================================================
// UI-HKY-2：改绑/解绑（§7.3——冲突拒绝、解绑总是允许、PM-14 持久化）
// =====================================================================

TEST(ShortcutRegistryModel, RebindUnbindPersistence_UI_HKY_2)
{
    IRD_TEST_INFO("UI-HKY-2", {}, std::nullopt);
    ShortcutHarness h;
    ASSERT_TRUE(h.shortcuts->registerDefault("test.alpha", QKeySequence("Ctrl+S")).isOk());
    ASSERT_TRUE(h.shortcuts->registerDefault("test.beta", QKeySequence("Ctrl+P")).isOk());

    // 改绑冲突：test.alpha 改到 Ctrl+P（test.beta 已占用）→ 拒绝，原绑定
    // 保持（不覆盖不静默）。
    const HotkeyResult conflict = h.shortcuts->rebind("test.alpha", QKeySequence("Ctrl+P"));
    EXPECT_EQ(conflict.kind(), HotkeyResult::Kind::Conflict);
    EXPECT_EQ(h.shortcuts->lookup(QKeySequence("Ctrl+S")).value_or("<none>"),
              std::string("test.alpha"));

    // 改绑成功：Ctrl+S → Ctrl+T，来源转 User；持久化回调收到 User 集
    // （PM-14——Default 集不入持久化载荷）。
    const std::size_t persistedBefore = h.persisted.size();
    ASSERT_TRUE(h.shortcuts->rebind("test.alpha", QKeySequence("Ctrl+T")).isOk());
    EXPECT_EQ(h.shortcuts->lookup(QKeySequence("Ctrl+T")).value_or("<none>"),
              std::string("test.alpha"));
    EXPECT_EQ(h.shortcuts->lookup(QKeySequence("Ctrl+S")), std::nullopt)
        << "旧键已随改绑释放（一键一命令双向唯一）";
    ASSERT_EQ(h.persisted.size(), persistedBefore + 1);
    ASSERT_EQ(h.persisted.back().size(), std::size_t{1});
    EXPECT_EQ(h.persisted.back().front().command, std::string("test.alpha"));
    EXPECT_EQ(h.persisted.back().front().origin, BindingOrigin::User);

    // 解绑（key=nullopt）总是允许（§7.3 原文）；解绑后命令仍可查询可达
    // （面板兜底——acceptance 2"未绑定命令经面板可达"）。
    ASSERT_TRUE(h.shortcuts->rebind("test.alpha", std::nullopt).isOk());
    EXPECT_EQ(h.shortcuts->lookup(QKeySequence("Ctrl+T")), std::nullopt);
    EXPECT_FALSE(h.commands->query({}).empty());
    const auto views = h.commands->query({});
    EXPECT_NE(std::find_if(views.begin(), views.end(), [](const ui::CommandView& v) {
                  return v.id == "test.alpha";
              }),
              views.end())
        << "解绑后命令仍经面板可达（UX-13 兜底）";
    ASSERT_FALSE(h.persisted.empty());
    EXPECT_TRUE(h.persisted.back().empty()) << "解绑后 User 集为空（持久化同步）";

    // 同命令改绑回当前键＝幂等成功（显式用户操作，非自冲突）。
    ASSERT_TRUE(h.shortcuts->registerDefault("test.beta", QKeySequence("Ctrl+P")).isOk());
    EXPECT_EQ(h.shortcuts->rebind("test.beta", QKeySequence("Ctrl+P")).kind(),
              HotkeyResult::Kind::Ok);
    // 解绑未绑定命令＝幂等成功；解绑未注册命令＝UnknownCommand。
    EXPECT_TRUE(h.shortcuts->rebind("test.alpha", std::nullopt).isOk());
    EXPECT_EQ(h.shortcuts->rebind("ghost.cmd", std::nullopt).kind(),
              HotkeyResult::Kind::UnknownCommand);
}

TEST(ShortcutRegistryModel, RestoreUserBindingsDropsInvalidEntries_PM14)
{
    IRD_TEST_INFO("PM-14", {}, std::nullopt);
    ShortcutHarness h;
    ASSERT_TRUE(h.shortcuts->registerDefault("test.alpha", QKeySequence("Ctrl+S")).isOk());

    // 回放载荷（逐条 rebind 语义按序应用——坏一条丢一条，不炸整表，§4.5
    // 同纪律）：①合法覆盖（alpha→Ctrl+T，释放 Ctrl+S）；②未注册命令（丢弃）；
    // ③不可绑定命令（丢弃）；④Ctrl+S 已被①释放→成功（beta 占用）；
    // ⑤Ctrl+T 已被①占用→冲突丢弃（＋Dev 日志）。
    const std::vector<HotkeyBinding> history = {
        {QKeySequence("Ctrl+T"), "test.alpha", BindingOrigin::User},
        {QKeySequence("Ctrl+G"), "ghost.cmd", BindingOrigin::User},
        {QKeySequence("Ctrl+F"), "test.frozen", BindingOrigin::User},
        {QKeySequence("Ctrl+S"), "test.beta", BindingOrigin::User},
        {QKeySequence("Ctrl+T"), "test.beta", BindingOrigin::User},
    };
    h.shortcuts->restoreUserBindings(history);

    // 终态：①⑤两条有效——alpha→Ctrl+T（User）、beta→Ctrl+S（User）；
    // ②③④违约/冲突条目全部丢弃。
    const auto bindings = h.shortcuts->bindings();
    ASSERT_EQ(bindings.size(), std::size_t{2});
    EXPECT_EQ(h.shortcuts->lookup(QKeySequence("Ctrl+T")).value_or("<none>"),
              std::string("test.alpha"));
    EXPECT_EQ(h.shortcuts->lookup(QKeySequence("Ctrl+S")).value_or("<none>"),
              std::string("test.beta"));
    for (const HotkeyBinding& binding : bindings) {
        EXPECT_EQ(binding.origin, BindingOrigin::User) << "回放条目来源统一 User";
    }
    EXPECT_TRUE(h.devLog->seen("快捷键历史条目丢弃")) << "丢弃条目必须 Dev 留痕（不静默）";
}

}  // namespace
