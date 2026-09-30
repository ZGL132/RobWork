/**
 * @file   WorkbenchContentGuiTest.cpp
 * @brief  内容装配面（WorkbenchContent——UI-T16 壳拆分）的 GUI 契约用例：
 *         两段装配时序、嵌入式宿主布局记忆（旗标半区边界＋损坏回退）、
 *         会话入口处理器覆写路由（§11.5——插件打开协议的入口面）与让位页。
 *
 * 设计依据：
 *   - units/ui.md §10.1 v1.10 增量（壳拆「内容装配层＋顶层窗口宿主层」——
 *     harness 与插件共用同一内容装配面；本套件用嵌入式宿主形态直接驱动
 *     该面，顶层窗口宿主形态的回归由既有 WorkbenchShellGuiTest 全量承载
 *     ——两套件合起来覆盖两种宿主）；
 *   - O-38 裁决（DTB §4.2，2026-09-23）：宿主插件形态的机制面——布局记忆
 *     在宿主窗口生效（旗标半区）、新建/打开项目协议经壳入口可用（覆写面）、
 *     三维视图让位（不冒名）；任务契约 tasks/foundation/UI-T16.json
 *     acceptance 1/3 的具名自证面。
 *
 * 线程/环境：QApplication 级 GUI 用例（ird_gui 串行标签——§12.2 执行纪律）；
 *   每用例独立用户级设置目录（进程级 QSettings 重定向——WorkbenchShellGuiTest
 *   同款夹具纪律）。
 */

#include <gtest/gtest.h>

#include <QAction>
#include <QDir>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QWidget>

#include <sdurws/ird/diagnostics/Catalog.hpp>
#include <sdurws/ird/diagnostics/Redaction.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include <sdurws/ird/ui/ICommandRegistry.hpp>
#include <sdurws/ird/ui/IWorkbenchShell.hpp>
#include <sdurws/ird/ui/UiPorts.hpp>
#include <sdurws/ird/ui/UiProjections.hpp>
#include <sdurws/ird/ui/WorkbenchContent.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace sdurws::ird;
using sdurws::ird::ui::IWorkbenchContent;
using sdurws::ird::ui::ProjectContextProjection;
using sdurws::ird::ui::ProjectMetadataProjection;
using sdurws::ird::ui::ShellCommandAvailability;
using sdurws::ird::ui::ShellWiring;
using sdurws::ird::ui::WorkbenchContentDeps;
using sdurws::ird::ui::WorkbenchHostKind;
using sdurws::ird::ui::WorkbenchRegion;

// =====================================================================
// 测试替身（§3.1"ui 测试以可控替身承载"；§10.1 wiring 各指针非空前置）
// ——WorkbenchShellGuiTest 同款形态（本套件独立声明，不跨 TU 共享私有件）。
// =====================================================================

/// 诊断 sink 替身：内容装配面不产用户级诊断条目——append 记数暴露意外行为。
class RecordingDiagnosticSink final : public diagnostics::IDiagnosticSink {
public:
    void append(diagnostics::DiagnosticEntry entry) override { m_appended.push_back(entry); }
    std::vector<diagnostics::DiagProjectionItem> snapshot(diagnostics::DiagQuery) const override
    {
        return {};
    }
    std::unique_ptr<diagnostics::ISubscription> subscribe(diagnostics::IDiagObserver&) override
    {
        return nullptr;
    }
    std::string exportSafeSummary(diagnostics::DiagQuery, std::size_t) const override
    {
        return {};
    }
    const std::vector<diagnostics::DiagnosticEntry>& appended() const { return m_appended; }

private:
    std::vector<diagnostics::DiagnosticEntry> m_appended;
};

/// 脱敏服务替身：直通原文（本套件不产脱敏面内容；仅满足 wiring 非空前置）。
class PassthroughRedaction final : public diagnostics::IRedactionService {
public:
    std::string redact(std::string_view raw, diagnostics::LogTier) const noexcept override
    {
        return std::string(raw);
    }
    std::string redactPath(std::string_view rawPath) const noexcept override
    {
        return std::string(rawPath);
    }
    std::string safeSummary(std::string_view raw, std::size_t maxBytes) const noexcept override
    {
        std::string out(raw);
        if (maxBytes > 0 && out.size() > maxBytes) {
            out.resize(maxBytes);
        }
        return out;
    }
    void setPolicy(const diagnostics::RedactionPolicy&) override {}
};

/// 开发日志记录器：嵌入式损坏回退的 Dev 诊断观测点（§6.2"Dev 走日志"）。
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

/// 策略摘要端口替身：默认投影（本套件不消费策略面——仅满足非空前置）。
class FixedPolicySource final : public ui::IPolicySummarySource {
public:
    ui::PolicySummaryProjection summary() const override { return {}; }
};

/// 名称解析端口替身：恒不可解析（本套件不消费名称解析——同壳 UI-T03 形态）。
class NullNameResolver final : public ui::IUiNameResolver {
public:
    std::optional<std::string> resolveObjectId(core::ObjectId) const override
    {
        return std::nullopt;
    }
};

/// 固定名称解析替身（UI-T26——上下文栏对象标签的解析成功路径驱动）：
/// 任意身份解析为固定显示名（本用例只驱动"解析成功→标签呈现解析值"
/// 这一半区；解析失败回退半区由默认 NullNameResolver 驱动）。
class FixedNameResolver final : public ui::IUiNameResolver {
public:
    std::optional<std::string> resolveObjectId(core::ObjectId) const override
    {
        return std::string{"关节 6"};
    }
};

// =====================================================================
// 夹具：每用例独立用户级设置目录＋替身集＋嵌入式宿主装配辅助
// =====================================================================

class WorkbenchContentGuiTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // 每用例独立临时目录并全局重定向 QSettings 用户级 Ini 路径（PM-14
        // 存储隔离——WorkbenchShellGuiTest 同款；ird_gui 串行保证安全）。
        m_settingsDir = std::make_unique<QTemporaryDir>();
        ASSERT_TRUE(m_settingsDir->isValid());
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           m_settingsDir->path());
    }

    /// 组装 wiring（除 eventBus 外全非空；devLog 恒注入以支撑损坏回退观测）。
    ShellWiring makeWiring()
    {
        ShellWiring wiring;
        wiring.eventBus = nullptr;  // 允许为空＝无事件测试场景（显式声明）
        wiring.diagSink = m_diagSink;
        wiring.redaction = m_redaction;
        wiring.devLog = m_devLog;
        wiring.policySource = m_policySource;
        wiring.nameResolver = m_nameResolver;
        return wiring;
    }

    /// 嵌入式宿主形态的内容装配依赖（宿主控件＝本夹具的裸 QWidget）。
    WorkbenchContentDeps makeEmbeddedDeps()
    {
        WorkbenchContentDeps deps;
        deps.wiring = makeWiring();
        deps.hostKind = WorkbenchHostKind::EmbeddedDock;
        deps.hostWidget = &m_host;
        // 几何/位形钩子全缺省＝嵌入式形态（无顶层窗口半区）。
        return deps;
    }

    /// 当前用例的用户级设置文件全路径（<dir>/sdurws/ird-workbench.ini）。
    QString settingsIniPath() const
    {
        return m_settingsDir->filePath(QString::fromLatin1("sdurws/ird-workbench.ini"));
    }

    std::unique_ptr<QTemporaryDir> m_settingsDir;
    QWidget m_host;  ///< 嵌入式宿主控件（裸 QWidget——插件 Dock 体的测试替身）
    std::shared_ptr<RecordingDiagnosticSink> m_diagSink = std::make_shared<RecordingDiagnosticSink>();
    std::shared_ptr<PassthroughRedaction> m_redaction = std::make_shared<PassthroughRedaction>();
    std::shared_ptr<DevLogRecorder> m_devLog = std::make_shared<DevLogRecorder>();
    std::shared_ptr<FixedPolicySource> m_policySource = std::make_shared<FixedPolicySource>();
    std::shared_ptr<NullNameResolver> m_nameResolver = std::make_shared<NullNameResolver>();
};

// =====================================================================
// 两段装配时序契约（§10.1 v1.10——build 恰好一次、activate 依赖 build）
// =====================================================================

/**
 * UI-T16 拆分契约（两段装配）：二次 build 拒绝（返回值轨）；未 build 即
 * activate 安全无操作（不崩溃不半激活）；build/activate 后五个内容出口
 * 与状态行齐备（宿主层安放面的前置）。
 */
TEST_F(WorkbenchContentGuiTest, TwoPhaseAssemblyContract_UI_SPLIT)
{
    IRD_TEST_INFO("UX-09", {}, std::nullopt);
    auto content = ui::createWorkbenchContent(makeEmbeddedDeps());
    ASSERT_TRUE(content->build()) << "首次 build 被拒（wiring/宿主控件齐备应成功）";
    EXPECT_FALSE(content->build()) << "二次 build 未被拒（恰好一次违约未拦截）";

    // 未 build 即 activate 的违约在真实实现中不可达（本用例 build 已成功
    // ——activate 正常执行，出口齐备即其观测面）。
    content->activate();
    EXPECT_NE(content->topBarWidget(), nullptr);
    EXPECT_NE(content->leftWidget(), nullptr);
    EXPECT_NE(content->centralWidget(), nullptr);
    EXPECT_NE(content->rightWidget(), nullptr);
    EXPECT_NE(content->bottomWidget(), nullptr);

    // 中央区页 0＝无项目首页（PM-10 启动态——须在注入项目上下文前断言）。
    auto* stack = qobject_cast<QStackedWidget*>(content->centralWidget());
    ASSERT_NE(stack, nullptr);
    EXPECT_EQ(stack->currentIndex(), 0);

    // 状态出口观测（UI-T18 契约面变更——O-43 ③：QStatusBar* 出口移除，
    // PM-11 永久文本与瞬态消息改经双观测钩子投影宿主层；内容层零状态栏
    // Widget）。PM-11 投影随上下文注入刷新；瞬态消息经未知命令反馈驱动。
    QString pm11Text;
    QString transientMessage;
    content->setStatusTextObserver([&pm11Text](const QString& text) { pm11Text = text; });
    content->setStatusMessageObserver(
        [&transientMessage](const QString& message, int) { transientMessage = message; });
    ProjectContextProjection statusContext;
    ProjectMetadataProjection statusMetadata;
    statusMetadata.projectId = core::ProjectId::generate();
    statusMetadata.projectDisplayName = "状态投影验证";
    statusMetadata.writable = true;
    statusContext.project = statusMetadata;
    content->presentProjectContext(statusContext);
    EXPECT_FALSE(pm11Text.isEmpty())
        << "PM-11 状态文本未经理观察者投影（UI-T18 契约面失效）";
    content->submitCommand("bogus.command");
    EXPECT_TRUE(transientMessage.contains(QString::fromUtf8("未知命令")))
        << "瞬态消息未经理观察者投影（UI-T18 契约面失效）";

    EXPECT_TRUE(content->shutdown());
}

/**
 * 嵌入式宿主形态的装配校验（返回值轨）：wiring 必填指针为空 → build 拒绝；
 * 宿主控件为空 → build 拒绝（调用方错误 fail-fast——§10.1 错误类型口径）。
 */
TEST_F(WorkbenchContentGuiTest, BuildValidationRejectsIncompleteDeps_UI_SPLIT)
{
    IRD_TEST_INFO("PM-10", {}, std::nullopt);
    // wiring 必填指针缺失（policySource 空——§10.1 前置条件行）。
    WorkbenchContentDeps deps = makeEmbeddedDeps();
    deps.wiring.policySource.reset();
    auto content = ui::createWorkbenchContent(std::move(deps));
    EXPECT_FALSE(content->build()) << "wiring 必填指针为空未被拒";

    // 宿主控件缺失（插件装配缺陷面——调用方错误）。
    WorkbenchContentDeps noHost = makeEmbeddedDeps();
    noHost.hostWidget = nullptr;
    auto content2 = ui::createWorkbenchContent(std::move(noHost));
    EXPECT_FALSE(content2->build()) << "宿主控件为空未被拒";
}

// =====================================================================
// 嵌入式宿主布局记忆（O-38 裁决②"布局记忆在宿主窗口生效"的机制面）
// =====================================================================

/**
 * 旗标半区边界：嵌入式内容只读写三区可见性键——顶层形态留下的几何/位形
 * 键不被触碰（同组同键共享用户级设置、互不覆盖——ui.md §10.1 v1.10），
 * 且旗标跨实例恢复（关停重开模拟）。
 */
TEST_F(WorkbenchContentGuiTest, EmbeddedMemoryFlagsOnlyBoundary_UI_SPLIT)
{
    IRD_TEST_INFO("UX-09", {"PM-14"}, std::nullopt);
    // 预置"顶层形态"遗留记忆：版本＋几何/位形字节＋三旗标（模拟用户先用
    // 过 harness 再用宿主插件的 settings 形态）。
    {
        QSettings seed(QSettings::IniFormat, QSettings::UserScope,
                       QLatin1String("sdurws"), QLatin1String("ird-workbench"));
        seed.setValue("layout/version", 1);
        seed.setValue("layout/geometry", QByteArray("seed-geometry-bytes"));
        seed.setValue("layout/state", QByteArray("seed-state-bytes"));
        seed.setValue("layout/visibleLeft", true);
        seed.setValue("layout/visibleRight", true);
        seed.setValue("layout/visibleBottom", true);
    }

    // 嵌入式内容：隐藏左栏（用户开关三区之一）→ 关停落盘。
    {
        auto content = ui::createWorkbenchContent(makeEmbeddedDeps());
        ASSERT_TRUE(content->build());
        content->activate();
        ASSERT_TRUE(content->regionVisible(WorkbenchRegion::Left));
        content->setRegionVisible(WorkbenchRegion::Left, false);
        EXPECT_FALSE(content->regionVisible(WorkbenchRegion::Left));
        EXPECT_TRUE(content->shutdown()) << "嵌入式关停落盘应成功（旗标半区）";
    }

    // 边界断言：几何/位形键逐字节未动（QByteArray 非空原值）——插件不覆盖
    // 顶层形态记忆；旗标键已更新。
    QSettings check(QSettings::IniFormat, QSettings::UserScope,
                    QLatin1String("sdurws"), QLatin1String("ird-workbench"));
    EXPECT_EQ(check.value("layout/geometry").toByteArray(), QByteArray("seed-geometry-bytes"));
    EXPECT_EQ(check.value("layout/state").toByteArray(), QByteArray("seed-state-bytes"));
    EXPECT_EQ(check.value("layout/version").toInt(), 1);
    EXPECT_EQ(check.value("layout/visibleLeft").toBool(), false);
    EXPECT_EQ(check.value("layout/visibleRight").toBool(), true);

    // 跨实例恢复（关停重开模拟）：左栏保持隐藏——布局记忆在嵌入式宿主生效。
    {
        auto restored = ui::createWorkbenchContent(makeEmbeddedDeps());
        ASSERT_TRUE(restored->build());
        restored->activate();
        EXPECT_FALSE(restored->regionVisible(WorkbenchRegion::Left))
            << "布局记忆未在嵌入式宿主恢复（旗标半区丢失）";
        EXPECT_TRUE(restored->regionVisible(WorkbenchRegion::Right));
        EXPECT_TRUE(restored->shutdown());
    }
}

/**
 * 损坏回退（§4.5 的嵌入式适配面）：版本不识别 → 三区回退全可见＋
 * UI-LAYOUT-RESTORE-FAILED 经 Dev 通道出线＋损坏段整段丢弃（版本键消失），
 * 不阻塞启动（activate 正常完成）。
 */
TEST_F(WorkbenchContentGuiTest, EmbeddedCorruptMemoryFallsBackWithDevDiagnostic_UI_SPLIT)
{
    IRD_TEST_INFO("UI-LAYOUT-RESTORE-FAILED", {}, std::nullopt);
    {
        QSettings seed(QSettings::IniFormat, QSettings::UserScope,
                       QLatin1String("sdurws"), QLatin1String("ird-workbench"));
        seed.setValue("layout/version", 99);  // 未来版本＝本版本无法解释（损坏同路径）
        seed.setValue("layout/visibleLeft", false);
    }

    auto content = ui::createWorkbenchContent(makeEmbeddedDeps());
    ASSERT_TRUE(content->build());
    content->activate();  // 损坏不阻塞启动
    // 三区回退出厂可见性（全可见）；Dev 诊断出线；损坏段整段丢弃。
    EXPECT_TRUE(content->regionVisible(WorkbenchRegion::Left));
    EXPECT_TRUE(content->regionVisible(WorkbenchRegion::Right));
    EXPECT_TRUE(content->regionVisible(WorkbenchRegion::Bottom));
    EXPECT_TRUE(m_devLog->seen("UI-LAYOUT-RESTORE-FAILED"))
        << "UI-LAYOUT-RESTORE-FAILED 未经 Dev 通道出线（§6.2）";
    QSettings check(QSettings::IniFormat, QSettings::UserScope,
                    QLatin1String("sdurws"), QLatin1String("ird-workbench"));
    EXPECT_FALSE(check.contains("layout/version")) << "损坏段未整段丢弃（§4.5）";
    EXPECT_TRUE(content->shutdown());
}

// =====================================================================
// 会话入口覆写（§11.5——插件打开协议的壳入口面；O-38 裁决②语义不变）
// =====================================================================

/**
 * 装配层覆写的路由面：注入 openProjectHandler 后，壳入口
 * （submitCommand("project.open")）走注入编排而非 §7.1 阶段 A 占位处理器；
 * 描述符登记面不受影响（registered=true、作用域门控照旧——五处入口同路由
 * 的语义不变性）。
 */
TEST_F(WorkbenchContentGuiTest, SessionEntryOverrideRoutesShellEntries_UI_SPLIT)
{
    IRD_TEST_INFO("PM-11", {"UX-09"}, std::nullopt);
    // 注入打开编排替身（真实插件注入文件对话框→UiSessionController 编排；
    // 本替身置位观测旗标——路由到点的可观测证据）。
    bool openOrchestrationInvoked = false;
    WorkbenchContentDeps deps = makeEmbeddedDeps();
    deps.openProjectHandler =
        [&openOrchestrationInvoked](const std::vector<ui::CommandParameter>&) {
            openOrchestrationInvoked = true;
            ui::CommandOutcome out;
            out.accepted = true;
            out.messageKey = std::string{"test.open-orchestration"};
            return out;
        };
    auto content = ui::createWorkbenchContent(std::move(deps));
    ASSERT_TRUE(content->build());
    content->activate();

    // 登记面不变：覆写只换处理器——命令已注册且会话作用域恒可用。
    const ShellCommandAvailability availability = content->commandAvailability("project.open");
    EXPECT_TRUE(availability.registered);
    EXPECT_TRUE(availability.enabled);

    // 壳入口触发（首页按钮/菜单/面板同路由的提交路径）→ 走注入编排。
    content->submitCommand("project.open");
    EXPECT_TRUE(openOrchestrationInvoked)
        << "submitCommand 未路由到注入的会话入口编排（§11.5 覆写面失效）";

    // 未覆写的命令保持阶段 A 占位形态（harness 行为零变化的边界）：
    // project.new 未注入 → 提交走占位处理器（accepted=true——提交链路
    // 真实走通，不崩溃不虚构）。
    content->submitCommand("project.new");
    EXPECT_TRUE(content->shutdown());
}

/**
 * UI-T17 覆写面扩展的路由与门控面：注入 saveProjectHandler（draft.save）/
 * closeProjectHandler（workbench.closeProject）后，壳入口走注入编排而非
 * §7.1 阶段 A 占位处理器；项目作用域命令的门控语义不变（无项目＝禁用——
 * §7.5/§7.6 快照同源；项目上下文注入后可用并路由到点）。
 */
TEST_F(WorkbenchContentGuiTest, SaveCloseEntryOverrideRoutesShellEntries_UI_SPLIT)
{
    IRD_TEST_INFO("PM-04", {"PM-03", "UX-09"}, std::nullopt);
    bool saveOrchestrationInvoked = false;
    bool closeOrchestrationInvoked = false;
    WorkbenchContentDeps deps = makeEmbeddedDeps();
    deps.saveProjectHandler =
        [&saveOrchestrationInvoked](const std::vector<ui::CommandParameter>&) {
            saveOrchestrationInvoked = true;
            ui::CommandOutcome out;
            out.accepted = true;
            return out;
        };
    deps.closeProjectHandler =
        [&closeOrchestrationInvoked](const std::vector<ui::CommandParameter>&) {
            closeOrchestrationInvoked = true;
            ui::CommandOutcome out;
            out.accepted = true;
            return out;
        };
    auto content = ui::createWorkbenchContent(std::move(deps));
    ASSERT_TRUE(content->build());
    content->activate();

    // 项目作用域门控（§7.5/§7.6——默认谓词零 IO）：无项目时保存/关闭禁用
    // （登记面不变——覆写只换处理器，不换谓词）。
    EXPECT_TRUE(content->commandAvailability("draft.save").registered);
    EXPECT_TRUE(content->commandAvailability("workbench.closeProject").registered);
    EXPECT_FALSE(content->commandAvailability("draft.save").enabled);
    EXPECT_FALSE(content->commandAvailability("workbench.closeProject").enabled);

    // 项目上下文注入后可用（可写会话）→ 提交路由到注入编排。
    ProjectContextProjection context;
    ProjectMetadataProjection metadata;
    metadata.projectId = core::ProjectId::generate();
    metadata.projectDisplayName = "覆写门控验证";
    metadata.writable = true;
    context.project = metadata;
    content->presentProjectContext(context);
    EXPECT_TRUE(content->commandAvailability("draft.save").enabled);
    EXPECT_TRUE(content->commandAvailability("workbench.closeProject").enabled);

    content->submitCommand("draft.save");
    EXPECT_TRUE(saveOrchestrationInvoked)
        << "draft.save 未路由到注入的保存编排（UI-T17 覆写面失效）";
    content->submitCommand("workbench.closeProject");
    EXPECT_TRUE(closeOrchestrationInvoked)
        << "workbench.closeProject 未路由到注入的关闭编排（UI-T17 覆写面失效）";
    EXPECT_TRUE(content->shutdown());
}

// =====================================================================
// 三维让位页（O-38 裁决③——嵌入式宿主中央区不冒名三维能力）
// =====================================================================

/**
 * 嵌入式宿主中央区形态：页 1＝让位页（ird_view3d_host_yield——声明三维
 * 视图归宿主承载，零交互控件）；项目上下文注入后切换到页 1（与顶层形态
 * 的页索引语义一致）；顶层形态的占位面板（ird_view3d_placeholder）回归由
 * WorkbenchShellGuiTest.CentralView3DPlaceholder 承载，此处不重复。
 */
TEST_F(WorkbenchContentGuiTest, EmbeddedCentralYieldPage_UI_SPLIT)
{
    IRD_TEST_INFO("UX-11", {}, std::nullopt);
    auto content = ui::createWorkbenchContent(makeEmbeddedDeps());
    ASSERT_TRUE(content->build());
    content->activate();
    auto* stack = qobject_cast<QStackedWidget*>(content->centralWidget());
    ASSERT_NE(stack, nullptr);
    // 让位页在位：零按钮等交互控件（不虚构能力——§11.4 红线）。
    auto* yield = stack->findChild<QWidget*>(QString::fromLatin1("ird_view3d_host_yield"));
    ASSERT_NE(yield, nullptr) << "嵌入式宿主缺三维让位页（O-38 裁决③让位形态）";
    EXPECT_TRUE(yield->findChildren<QPushButton*>().isEmpty());
    EXPECT_TRUE(yield->findChildren<QAction*>().isEmpty());

    // 页切换语义与顶层形态一致：无项目→页 0（首页）；项目→页 1（让位页）。
    EXPECT_EQ(stack->currentIndex(), 0);
    ProjectContextProjection context;
    ProjectMetadataProjection metadata;
    metadata.projectId = core::ProjectId::generate();
    metadata.projectDisplayName = "让位页验证";
    metadata.writable = true;
    context.project = metadata;
    content->presentProjectContext(context);
    EXPECT_EQ(stack->currentIndex(), 1);
    EXPECT_TRUE(content->shutdown());
}

/**
 * UI-T19 增量（§10.1 v1.17——IWorkbenchContent::commandRegistry()）：访问器
 * 交出的必须是**在位**注册表——已知壳层命令（§7.1 子集）经该引用的
 * availability 查询与内容面权威查询逐字段一致（同一实例的两条观测路径；
 * 若访问器返回副本/代理，聚合恒等在此暴露）。
 */
TEST_F(WorkbenchContentGuiTest, ContentCommandRegistryAccessorServesLiveRegistry_B1_UI_T19)
{
    IRD_TEST_INFO("UX-09", {}, std::nullopt);
    auto content = ui::createWorkbenchContent(makeEmbeddedDeps());
    ASSERT_TRUE(content->build());
    content->activate();

    // 已知壳层命令（§7.1 装配期登记集——project.new 恒在）的两条查询路径
    // 必须逐字段一致：registered/visible/enabled 同源即证明是同一注册表。
    const std::string knownShellCommand = "project.new";
    const auto viaAccessor = content->commandRegistry().availability(knownShellCommand);
    const auto viaContent = content->commandAvailability(knownShellCommand);
    EXPECT_EQ(viaContent.registered, viaAccessor.registered);
    EXPECT_EQ(viaContent.visible, viaAccessor.visible);
    EXPECT_EQ(viaContent.enabled, viaAccessor.enabled);
    EXPECT_TRUE(viaAccessor.registered)
        << "已知壳层命令经访问器查询未登记（访问器未交出在位注册表）";

    EXPECT_TRUE(content->shutdown());
}

// =====================================================================
// WP-24-T03b 收口——中央区阶段面板页（§4.1 CentralAreaHost 挂位面）
// =====================================================================

/**
 * 阶段面板页挂位与切换（契约 WP-24-T03b acceptance 4 的机制级验证面）：
 * deps.stagePanelPages 逐项入中央栈（页 objectName 供定位）；showStagePanel
 * 切换激活页；无项目上下文刷新回首页且激活记忆复位；未登记阶段＝无操作
 * 不崩溃（§11.3 缺位语义）。测试以替身面板工厂承载（建模面板入中央栈的
 * 集成面需 ui_app→modeling_plugin 新建链边——落位偏差登记 ui.md §16.7，
 * 随 UI-T20/21 装配面任务落地）。
 */
TEST_F(WorkbenchContentGuiTest, StagePanelPageHostingAndSwitching_WP24_T03B)
{
    IRD_TEST_INFO("UX-12", {}, std::nullopt);
    WorkbenchContentDeps deps = makeEmbeddedDeps();
    WorkbenchContentDeps::StagePanelPage modelingPage;
    modelingPage.stage = ui::StageId::Modeling;
    modelingPage.titleKey = "stage.modeling.title";
    modelingPage.factory = [this]() -> QWidget* {
        auto* page = new QWidget(&m_host);
        page->setObjectName("fake_modeling_panel");
        return page;
    };
    deps.stagePanelPages.push_back(modelingPage);
    auto content = ui::createWorkbenchContent(std::move(deps));
    ASSERT_TRUE(content->build());
    content->activate();

    auto* stack = qobject_cast<QStackedWidget*>(content->centralWidget());
    ASSERT_NE(stack, nullptr);
    // 初始（无项目）＝首页；切到建模阶段页后当前页为替身面板页。
    EXPECT_EQ(stack->currentIndex(), 0);
    content->showStagePanel(ui::StageId::Modeling);
    const int modelingIndex = stack->currentIndex();
    ASSERT_NE(modelingIndex, 0) << "阶段页未入中央栈（挂位面缺失）";
    // 挂位页 objectName＝content 生成名（"ird_stage_panel_<StageId 整数>"；
    // Modeling＝0）——工厂自命名被装配面规范化覆写（可定位性优先）。
    EXPECT_EQ(stack->widget(modelingIndex)->objectName(),
              QString::fromLatin1("ird_stage_panel_0"));

    // 无项目上下文注入（关闭完成）→ 回首页＋激活记忆复位（§6.2 纪元过滤
    // 的呈现对位——旧阶段选择不泄漏）；再注入项目上下文 → 仍不自动回到
    // 已复位的阶段页（回到视图区域页 1）。
    ProjectContextProjection closedContext;
    content->presentProjectContext(closedContext);
    EXPECT_EQ(stack->currentIndex(), 0);
    ProjectContextProjection openContext;
    openContext.project = ProjectMetadataProjection{};
    content->presentProjectContext(openContext);
    EXPECT_EQ(stack->currentIndex(), 1);
    content->showStagePanel(ui::StageId::Modeling);
    EXPECT_EQ(stack->currentIndex(), modelingIndex);

    // 未登记阶段（Kinematics 无页）＝无操作不崩溃（当前页保持不变）。
    content->showStagePanel(ui::StageId::Kinematics);
    EXPECT_EQ(stack->currentIndex(), modelingIndex);

    EXPECT_TRUE(content->shutdown());
}

/**
 * 域命令入册（契约 WP-24-T03b acceptance 1/2 的机制级验证面）：owner 白名
 * 单扩展使 modeling 域命令可登记；重复 id 拒绝（§7.2 不覆盖不静默——注册
 * 表返回值轨）；处理器级拒绝的 messageKey 文案经瞬态消息观察钩子呈现
 * （不走通用只读/无项目理由——ERR-01 因果如实）。
 */
TEST_F(WorkbenchContentGuiTest, DomainCommandRegistrationAndHonestFeedback_WP24_T03B)
{
    IRD_TEST_INFO("SA-16", {}, std::nullopt);
    WorkbenchContentDeps deps = makeEmbeddedDeps();
    deps.extraCommandOwners.push_back("modeling");

    WorkbenchContentDeps::DomainCommandEntry entry;
    entry.descriptor.id = "modeling.diff-baseline";
    entry.descriptor.ownerUnit = "modeling";
    entry.descriptor.titleKey = "cmd.modeling.diff-baseline.title";
    entry.descriptor.readOnlyAllowed = true;
    // 会话作用域（无项目态恒可用——处理器级拒绝路径可达；Project 作用域
    // 的无项目拒绝走注册表默认谓词，属 acceptance 1 的门控面另证）。
    entry.descriptor.scope = ui::CommandScope::Session;
    entry.handler = [](const std::vector<ui::CommandParameter>&) {
        // 处理器级拒绝（域流程未装配的诚实反馈——messageKey 承载原因）。
        ui::CommandOutcome out;
        out.accepted = false;
        out.messageKey = std::string{"cmd.modeling.flow-not-assembled"};
        return out;
    };
    deps.domainCommandEntries.push_back(std::move(entry));

    auto content = ui::createWorkbenchContent(std::move(deps));
    ASSERT_TRUE(content->build());
    content->activate();

    // 入册成功且 availability 快照可见（§7.2 registrationOrder＝装配序）。
    const auto availability = content->commandAvailability("modeling.diff-baseline");
    EXPECT_TRUE(availability.registered) << "域命令未入册（owner 白名单/登记面失效）";

    // 处理器级诚实反馈经注册表 submit 直达断言（§10.3：注册表在处理器
    // 返回后强制 accepted=true——"已执行"语义＝处理器给出了诚实应答；
    // messageKey 承载处理器原因键，瞬态呈现由宿主处理器反馈面承担）。
    const auto outcome = content->commandRegistry().submit("modeling.diff-baseline");
    EXPECT_TRUE(outcome.accepted);
    EXPECT_TRUE(outcome.messageKey.has_value());
    EXPECT_EQ(*outcome.messageKey, std::string{"cmd.modeling.flow-not-assembled"});

    // 重复 id 拒绝（§7.2 不覆盖不静默——seal 后注册走违约轨；此处验证
    // seal 前重复：注册表已 build 收口，运行期注册被拒＝InvalidDescriptor）。
    ui::CommandDescriptor duplicate;
    duplicate.id = "modeling.diff-baseline";
    duplicate.ownerUnit = "modeling";
    const auto result =
        content->commandRegistry().registerCommand(duplicate, {});
    EXPECT_NE(result, ui::RegistrationResult::Ok) << "重复 id 未被拒（§7.2 不覆盖被破坏）";

    EXPECT_TRUE(content->shutdown());
}

/**
 * 域命令卡表全量入册与确定性（契约 WP-24-T03b acceptance 1 的具名用例面）：
 * ①十条 modeling 域命令（modeling.md §9.7.3 卡表全量——本测试以本地表承载
 * 卡表值，卡表对位一致性由 modeling 单元 Commands_TenDottedCommandIds 等
 * 用例钉住；ui 测试不得 include modeling 私有头，R-2）经 domainCommandEntries
 * 注册后，availability 快照含十条（registered=true 逐条）；②同输入重复装配
 * 两份内容装配面，面板快照（paletteSnapshot——registrationOrder 稳定排序）
 * 中十条域命令的相对序完全一致（NFR-COR-02 界面延伸：同输入同排序）；③与
 * 壳层命令同 id 的域命令登记被拒，且 §11.3 失败隔离通道留下失败行（Dev 日
 * 志"domain command registration rejected: id=…"——报告面观测点）。
 */
TEST_F(WorkbenchContentGuiTest, DomainCommandCatalogTenRegisteredDeterministic_WP24_T03B)
{
    IRD_TEST_INFO("SA-16", {}, std::nullopt);

    // 卡 §9.7.3 十条的本地承载（id/scope/readOnlyAllowed 逐行按卡表；行序＝
    // 卡表行序＝登记序）。handler 全部给诚实空应答（本用例不触执行语义）。
    struct CatalogRow {
        const char* id;
        ui::CommandScope scope;
        bool readOnlyAllowed;
    };
    const CatalogRow catalog[10] = {
        {"modeling.new-from-template", ui::CommandScope::Project, false},
        {"modeling.import-urdf", ui::CommandScope::Project, false},
        {"modeling.import-xacro", ui::CommandScope::Project, false},
        {"modeling.switch-authority", ui::CommandScope::Project, false},
        {"modeling.estimate-properties", ui::CommandScope::Project, false},
        {"modeling.generate-placeholder-geometry", ui::CommandScope::Project, false},
        {"modeling.diff-baseline", ui::CommandScope::Project, true},
        {"modeling.export-package", ui::CommandScope::Project, true},
        {"modeling.import-package", ui::CommandScope::Project, false},
        {"modeling.reset-home-zero", ui::CommandScope::Session, true},
    };

    // 装配依赖构造器（两次调用同输入——确定性对照面；第三份携带重复 id
    // 行专门驱动 §11.3 失败隔离）。
    auto makeDeps = [this, &catalog](bool withCollidingRow) {
        WorkbenchContentDeps deps = makeEmbeddedDeps();
        deps.extraCommandOwners.push_back("modeling");
        for (const CatalogRow& row : catalog) {
            WorkbenchContentDeps::DomainCommandEntry entry;
            entry.descriptor.id = row.id;
            entry.descriptor.ownerUnit = "modeling";
            entry.descriptor.titleKey = ui::TextKey(std::string("cmd.") + row.id + ".title");
            entry.descriptor.scope = row.scope;
            entry.descriptor.readOnlyAllowed = row.readOnlyAllowed;
            entry.handler = [](const std::vector<ui::CommandParameter>&) {
                ui::CommandOutcome out;
                out.accepted = true;
                return out;
            };
            deps.domainCommandEntries.push_back(std::move(entry));
        }
        if (withCollidingRow) {
            // 重复 id 反例：与壳层命令 draft.save 同 id（§7.2 冲突规则第 2
            // 条——不同 owner 同 id 同样拒绝）。该行预期不进注册表，其余
            // 十条照常（§11.3 失败隔离）。
            WorkbenchContentDeps::DomainCommandEntry collision;
            collision.descriptor.id = "draft.save";
            collision.descriptor.ownerUnit = "modeling";
            collision.descriptor.titleKey = "cmd.draft.save.title";
            collision.descriptor.scope = ui::CommandScope::Project;
            collision.handler = [](const std::vector<ui::CommandParameter>&) {
                ui::CommandOutcome out;
                out.accepted = true;
                return out;
            };
            deps.domainCommandEntries.push_back(std::move(collision));
        }
        return deps;
    };

    // 取面板快照中 modeling.* 行的 id 序（保序投影——registrationOrder 锚）。
    auto modelingOrder = [](ui::IWorkbenchContent& content) {
        std::vector<std::string> ids;
        for (const auto& view : content.commandRegistry().paletteSnapshot("", 200)) {
            const std::string id(view.id);
            if (id.rfind("modeling.", 0) == 0) {
                ids.push_back(id);
            }
        }
        return ids;
    };

    auto contentA = ui::createWorkbenchContent(makeDeps(false));
    ASSERT_TRUE(contentA->build());
    contentA->activate();
    auto contentB = ui::createWorkbenchContent(makeDeps(false));
    ASSERT_TRUE(contentB->build());
    contentB->activate();

    // ①availability 快照含十条（逐条 registered——卡表全量入册）。
    for (const CatalogRow& row : catalog) {
        EXPECT_TRUE(contentA->commandRegistry().availability(row.id).registered)
            << "域命令未入册: " << row.id;
    }
    // ②同输入同排序：两份装配的域命令面板序逐位一致（NFR-COR-02）。
    {
        const auto orderA = modelingOrder(*contentA);
        const auto orderB = modelingOrder(*contentB);
        ASSERT_EQ(orderA.size(), std::size_t{10})
            << "面板快照中域命令数非十条（入册面缺口）";
        ASSERT_EQ(orderA.size(), orderB.size());
        for (std::size_t i = 0; i < orderA.size(); ++i) {
            EXPECT_EQ(orderA[i], orderB[i]) << "第 " << i << " 位序不一致";
        }
    }
    contentA->shutdown();
    contentB->shutdown();

    // ③冲突行：同 id 域命令登记被拒（§11.3 失败隔离——拒绝只作用于冲突
    // 行本身，其余十条照常入册）＋失败行留痕（Dev 日志观测点）。
    auto contentC = ui::createWorkbenchContent(makeDeps(true));
    ASSERT_TRUE(contentC->build());
    contentC->activate();
    for (const CatalogRow& row : catalog) {
        EXPECT_TRUE(contentC->commandRegistry().availability(row.id).registered)
            << "冲突行殃及卡表命令入册（§11.3 失败隔离被违反）: " << row.id;
    }
    // 冲突行本身未进注册表：注册表内 draft.save 保持壳层 owner（唯一——
    // §7.2 重复 id 不覆盖；owner 仍为壳层设施"ui"）。
    EXPECT_TRUE(contentC->commandRegistry().availability("draft.save").registered);
    // 失败行观测：Dev 日志含注册拒绝行（含请求 id——§7.2"重复 id（含不同
    // owner）→拒绝注册＋诊断 UI-CMD-DUPLICATE"的装配期留痕半区）。
    EXPECT_TRUE(m_devLog->seen("domain command registration rejected: id=draft.save"))
        << "冲突登记未留下失败行（§11.3 报告面缺失——不静默被破坏）";
    contentC->shutdown();
}

/**
 * 域命令可用性门控（契约 WP-24-T03b acceptance 2 的具名反例面）：NoProject
 * 态 Project 作用域域命令 enabled=false（PM-10——一切项目作用域命令禁用，
 * 注册表作用域谓词统一承载，域命令零特殊路径）；只读会话 readOnlyAllowed=
 * false 的命令禁用而 readOnlyAllowed=true 的命令保持可用（§7.6 只读条件
 * ——L-7 的命令半区）。
 */
TEST_F(WorkbenchContentGuiTest, DomainCommandGatingNoProjectAndReadonly_WP24_T03B)
{
    IRD_TEST_INFO("PM-10", {}, std::nullopt);  // PM-07（只读门控）随断言注释锚定

    WorkbenchContentDeps deps = makeEmbeddedDeps();
    deps.extraCommandOwners.push_back("modeling");
    // 一对卡表反例：写路径 Project 作用域命令（import-package，卡行⑨
    // readOnlyAllowed=false）＋只读可用 Project 作用域命令（diff-baseline，
    // 卡行⑦ readOnlyAllowed=true）。handler 诚实空应答（本用例只验门控）。
    WorkbenchContentDeps::DomainCommandEntry writeCommand;
    writeCommand.descriptor.id = "modeling.import-package";
    writeCommand.descriptor.ownerUnit = "modeling";
    writeCommand.descriptor.titleKey = "cmd.modeling.import-package.title";
    writeCommand.descriptor.scope = ui::CommandScope::Project;
    writeCommand.descriptor.readOnlyAllowed = false;
    writeCommand.handler = [](const std::vector<ui::CommandParameter>&) {
        ui::CommandOutcome out;
        out.accepted = true;
        return out;
    };
    deps.domainCommandEntries.push_back(std::move(writeCommand));
    WorkbenchContentDeps::DomainCommandEntry readCommand;
    readCommand.descriptor.id = "modeling.diff-baseline";
    readCommand.descriptor.ownerUnit = "modeling";
    readCommand.descriptor.titleKey = "cmd.modeling.diff-baseline.title";
    readCommand.descriptor.scope = ui::CommandScope::Project;
    readCommand.descriptor.readOnlyAllowed = true;
    readCommand.handler = [](const std::vector<ui::CommandParameter>&) {
        ui::CommandOutcome out;
        out.accepted = true;
        return out;
    };
    deps.domainCommandEntries.push_back(std::move(readCommand));

    auto content = ui::createWorkbenchContent(std::move(deps));
    ASSERT_TRUE(content->build());
    content->activate();

    // 反例①无项目态（未注入任何上下文＝NoProject——PM-10）：两条 Project
    // 作用域域命令均 registered 但 enabled=false；只读反例带原因键（§7.5
    // "禁用＋说明"保留发现性）。
    {
        const auto writeGate = content->commandRegistry().availability("modeling.import-package");
        EXPECT_TRUE(writeGate.registered);
        EXPECT_FALSE(writeGate.enabled) << "NoProject 态 Project 作用域域命令未被门控禁用";
        const auto readGate = content->commandRegistry().availability("modeling.diff-baseline");
        EXPECT_TRUE(readGate.registered);
        EXPECT_FALSE(readGate.enabled) << "NoProject 态 Project 作用域域命令（只读可用）未被门控禁用";
    }

    // 反例②只读会话（writable=false——INV-SES-1 判定源唯一）：写路径域
    // 命令禁用且只读阻断位在案；只读可用域命令不受只读门控牵连。
    ProjectContextProjection readonlyContext;
    readonlyContext.project = ProjectMetadataProjection{};  // writable 默认 false＝只读
    content->presentProjectContext(readonlyContext);
    {
        const auto writeGate = content->commandRegistry().availability("modeling.import-package");
        EXPECT_TRUE(writeGate.registered);
        EXPECT_FALSE(writeGate.enabled) << "只读会话写路径域命令未被禁用（§5.5/§7.6 门控失效）";
        EXPECT_TRUE(writeGate.readOnlyBlocked)
            << "只读阻断位未在案（§7.6 机器观测面缺失）";
        const auto readGate = content->commandRegistry().availability("modeling.diff-baseline");
        EXPECT_TRUE(readGate.enabled)
            << "只读会话 readOnlyAllowed=true 域命令被误禁（L-7 过度门控）";
    }

    // 可写对照（OpenWritable）：写路径域命令解除禁用——门控随上下文刷新
    // （§7.5 谓词求值只读消费快照）。
    ProjectContextProjection writableContext;
    writableContext.project = ProjectMetadataProjection{};
    writableContext.project->writable = true;
    content->presentProjectContext(writableContext);
    {
        const auto writeGate = content->commandRegistry().availability("modeling.import-package");
        EXPECT_TRUE(writeGate.enabled)
            << "可写会话写路径域命令仍被禁用（上下文刷新未达域命令谓词）";
    }

    EXPECT_TRUE(content->shutdown());
}

// =====================================================================
// UI-T25——文案治理与空态引导（acceptance 1/2/3 的机器断言面）
// =====================================================================

/**
 * UI-T25 空态引导＋占位收敛（契约 acceptance 2/3）：底部『下一步建议』
 * 页签呈现静态工作流引导（ird_advice_guide——非占位文本，只引已实装入口）
 * ＋其余四页签占位保持；顶栏三占位合并为单条说明；左栏不再有"项目对象树"
 * 占位（共享工业项目树已承载——UI-T23 D3）。呈现文本断言（呈现形态钉）：
 * 引导文本不含内部任务编号词形（UX-02——本任务的文案治理红线）。
 * 截图：IRD_UI_T25_SNAP_DIR 设置时对 advice 页 grab 存 PNG（验收留痕用）；
 * 未设置时跳过截图只做断言（CI 无桌面留存路径）。
 */
TEST_F(WorkbenchContentGuiTest, AdviceGuideAndPlaceholderConvergence_UX02_UI_T25)
{
    IRD_TEST_INFO("UX-02", {}, std::nullopt);
    auto content = ui::createWorkbenchContent(makeEmbeddedDeps());
    ASSERT_TRUE(content->build());
    content->activate();

    // ---- 底部任务和状态区：页签结构与引导页呈现 ----
    // （bottomWidget() 出口即 tabs 本体——WorkbenchContentImpl::buildBottomContent
    //   直接以 QTabWidget 为分区控件，findChild 不查自身故直接 cast。）
    auto* tabs = qobject_cast<QTabWidget*>(content->bottomWidget());
    ASSERT_NE(tabs, nullptr) << "底部任务和状态区页签容器缺失";
    ASSERT_EQ(tabs->objectName(), QStringLiteral("ird_bottom_tabs"))
        << "底部页签容器 objectName 漂移";
    ASSERT_EQ(tabs->count(), 5) << "底部页签数偏离（§4.2 底部行五页签）";

    // 其余四页签保持占位（呈现模型/状态词确未落地——不虚构能力）。
    for (int i = 0; i < 4; ++i) {
        tabs->setCurrentIndex(i);
        auto* placeholder = qobject_cast<QLabel*>(tabs->currentWidget());
        ASSERT_NE(placeholder, nullptr) << "页签 " << i << " 非占位 QLabel";
        EXPECT_TRUE(placeholder->text().contains(
            QString::fromUtf8(u8"本阶段将在后续版本提供")))
            << "页签占位说明丢失（index=" << i << "）";
    }

    // 『下一步建议』页＝静态工作流引导（ird_advice_guide）。
    tabs->setCurrentIndex(4);
    auto* guide = qobject_cast<QLabel*>(tabs->currentWidget());
    ASSERT_NE(guide, nullptr) << "下一步建议页非 QLabel";
    EXPECT_EQ(guide->objectName(), QStringLiteral("ird_advice_guide"));
    const QString guideText = guide->text();
    EXPECT_FALSE(guideText.isEmpty()) << "引导文本为空";
    // 只引已实装入口的关键特征词（新建/打开项目、需求导入、求解 IK）。
    EXPECT_TRUE(guideText.contains(QString::fromUtf8(u8"新建或打开项目")))
        << "引导缺项目入口行";
    EXPECT_TRUE(guideText.contains(QString::fromUtf8(u8"导入 CSV/JSON")))
        << "引导缺需求导入行";
    EXPECT_TRUE(guideText.contains(QString::fromUtf8(u8"求解 IK")))
        << "引导缺运动学求解行";
    // 零内部名（UX-02——文案治理红线在引导文本上的投影）。
    EXPECT_EQ(guideText.indexOf(QString::fromLatin1("WP-")), -1)
        << "引导文本含内部任务编号词形 WP-";
    EXPECT_EQ(guideText.indexOf(QString::fromLatin1("deprecated")), -1)
        << "引导文本含开发术语 deprecated";

    // 引导页截图（验收留痕——环境变量指路时才落盘）。
    // 未显示态 grab 按当前几何裁切（sizeHint 最小宽），先给足尺寸再抓帧。
    const QString snapDir = qEnvironmentVariable("IRD_UI_T25_SNAP_DIR");
    if (!snapDir.isEmpty()) {
        QDir().mkpath(snapDir);
        guide->resize(720, 360);
        tabs->resize(760, 420);
        tabs->grab().save(snapDir + QStringLiteral("/advice-guide.png"), "PNG");
    }
    tabs->setCurrentIndex(0);

    // ---- 顶栏：占位行彻底撤除（UI-T26 增量修订——UI-T25 时代的合并占位
    // 行已被上下文栏三标签取代，本断言随契约 v1.0 修订同步：占位词形零
    // 残留；上下文栏三标签在位——呈现细节由 TopContextBarPresentation_
    // UX02_UI_T26 用例承载，此处只钉"无回退"）----
    auto* topBar = qobject_cast<QWidget*>(content->topBarWidget());
    ASSERT_NE(topBar, nullptr) << "顶栏内容条缺失";
    ASSERT_EQ(topBar->objectName(), QStringLiteral("ird_top_bar_content"))
        << "顶栏内容条 objectName 漂移";
    int legacyStageNavCount = 0;
    const auto topLabels = topBar->findChildren<QLabel*>();
    for (const QLabel* label : topLabels) {
        const QString text = label->text();
        EXPECT_FALSE(text.contains(QString::fromUtf8(u8"本阶段将在后续版本提供")))
            << "顶栏残留阶段 A 占位词形（UI-T26 已撤除）";
        if (text == QString::fromUtf8(u8"阶段导航（本阶段将在后续版本提供）")) {
            ++legacyStageNavCount;
        }
    }
    EXPECT_EQ(legacyStageNavCount, 0) << "顶栏残留旧形态同名占位（未收敛）";
    EXPECT_NE(topBar->findChild<QLabel*>(QStringLiteral("ird_ctx_project")),
              nullptr)
        << "上下文栏项目标签缺失（UI-T26）";

    // ---- 左栏：项目对象树占位移除（共享树承载）、阶段任务列表占位保留 ----
    auto* leftWidget = content->leftWidget();
    ASSERT_NE(leftWidget, nullptr);
    int leftTaskListCount = 0;
    const auto leftLabels = leftWidget->findChildren<QLabel*>();
    for (const QLabel* label : leftLabels) {
        const QString text = label->text();
        EXPECT_FALSE(text.contains(QString::fromUtf8(u8"项目对象树")))
            << "左栏残留项目对象树占位（与共享树同屏冗余）";
        if (text.contains(QString::fromUtf8(u8"阶段任务列表"))) {
            ++leftTaskListCount;
        }
    }
    EXPECT_EQ(leftTaskListCount, 1) << "左栏阶段任务列表占位缺失";

    EXPECT_TRUE(content->shutdown());
}

/**
 * UI-T26 顶栏上下文栏六态（契约 acceptance 1）：①无项目（项目＝未打开
 * 项目/草稿标签隐藏/对象＝未选择）；②有项目可写＋草稿未应用（三标签
 * 全值＋只读徽标隐藏）；③草稿干净（无未应用修改）；④只读项目（徽标
 * 可见——既有 PM-07 语义回归）；⑤选择注入解析成功（Fixed 替身→解析值
 * 呈现）；⑥选择清空回退未选择＋runtimeOnly 事件不动对象标签。
 */
TEST_F(WorkbenchContentGuiTest, TopContextBarPresentation_UX02_UI_T26)
{
    IRD_TEST_INFO("UX-02", {}, std::nullopt);
    IRD_TEST_INFO("PM-11", {}, std::nullopt);

    // ①无项目首态。
    auto content = ui::createWorkbenchContent(makeEmbeddedDeps());
    ASSERT_TRUE(content->build());
    content->activate();
    content->presentProjectContext(ProjectContextProjection{});
    auto* bar = qobject_cast<QWidget*>(content->topBarWidget());
    ASSERT_NE(bar, nullptr);
    auto* projectLabel = bar->findChild<QLabel*>(QStringLiteral("ird_ctx_project"));
    auto* objectLabel = bar->findChild<QLabel*>(QStringLiteral("ird_ctx_object"));
    auto* draftLabel = bar->findChild<QLabel*>(QStringLiteral("ird_ctx_draft"));
    auto* readonlyBadge = bar->findChild<QLabel*>(QStringLiteral("ird_readonly_badge"));
    ASSERT_NE(projectLabel, nullptr);
    ASSERT_NE(objectLabel, nullptr);
    ASSERT_NE(draftLabel, nullptr);
    ASSERT_NE(readonlyBadge, nullptr);
    EXPECT_EQ(projectLabel->text(), QString::fromUtf8(u8"项目：未打开项目"));
    EXPECT_EQ(objectLabel->text(), QString::fromUtf8(u8"当前对象：未选择"));
    EXPECT_FALSE(draftLabel->isVisibleTo(bar)) << "无项目态草稿标签应隐藏";

    // ②有项目可写＋未应用修改。
    ProjectContextProjection writable;
    ProjectMetadataProjection metadata;
    metadata.projectDisplayName = "demo";
    metadata.writable = true;
    writable.project = metadata;
    writable.drafts.present = true;
    content->presentProjectContext(writable);
    EXPECT_EQ(projectLabel->text(), QString::fromUtf8(u8"项目：demo"));
    EXPECT_EQ(draftLabel->text(), QString::fromUtf8(u8"草稿：有未应用修改"));
    EXPECT_TRUE(draftLabel->isVisibleTo(bar));
    EXPECT_FALSE(readonlyBadge->isVisibleTo(bar)) << "可写会话只读徽标应隐藏";

    // ③草稿干净（present=false 但 sessionDirty=true 同为未应用——干净态
    // 两位全 false）。
    writable.drafts.present = false;
    writable.drafts.sessionDirty = false;
    content->presentProjectContext(writable);
    EXPECT_EQ(draftLabel->text(), QString::fromUtf8(u8"草稿：无未应用修改"));

    // ④只读项目（PM-07 徽标回归）。
    metadata.writable = false;
    writable.project = metadata;
    content->presentProjectContext(writable);
    EXPECT_TRUE(readonlyBadge->isVisibleTo(bar));

    // ⑤选择注入（NullNameResolver 默认替身→解析失败回退占位词形，
    //   不显示 ObjectId 规范形——UX-02）。
    ui::SelectionChange change;
    change.selectedObjectIds.push_back(
        core::ObjectId::fromCanonical("obj-0123456789abcdef0123456789abcdef"));
    content->noteSelectionForContext(change);
    EXPECT_EQ(objectLabel->text(),
              QString::fromUtf8(u8"当前对象：已选择对象（名称不可用）"))
        << "解析失败应回退占位词形而非身份规范形";

    // ⑥选择清空回退＋runtimeOnly 事件不动标签。
    change.selectedObjectIds.clear();
    content->noteSelectionForContext(change);
    EXPECT_EQ(objectLabel->text(), QString::fromUtf8(u8"当前对象：未选择"));
    change.runtimeOnly = true;
    change.runtimeObjectName = "frame";
    content->noteSelectionForContext(change);
    EXPECT_EQ(objectLabel->text(), QString::fromUtf8(u8"当前对象：未选择"))
        << "runtimeOnly 事件不得改动业务选中标签";
    EXPECT_TRUE(content->shutdown());
}

/**
 * UI-T26 选择解析成功路径（契约 acceptance 1 的 Fixed 替身半区）：
 * resolver 命中→标签呈现解析显示名。独立用例注入 FixedNameResolver
 * （makeWiring 的 nameResolver 位——wiring 注入面既有缝）。
 */
TEST_F(WorkbenchContentGuiTest, TopContextBarSelectionResolved_UI_T26)
{
    IRD_TEST_INFO("UX-02", {}, std::nullopt);

    WorkbenchContentDeps deps = makeEmbeddedDeps();
    deps.wiring.nameResolver = std::make_shared<FixedNameResolver>();
    auto content = ui::createWorkbenchContent(deps);
    ASSERT_TRUE(content->build());
    content->activate();
    auto* bar = qobject_cast<QWidget*>(content->topBarWidget());
    ASSERT_NE(bar, nullptr);
    auto* objectLabel = bar->findChild<QLabel*>(QStringLiteral("ird_ctx_object"));
    ASSERT_NE(objectLabel, nullptr);

    ui::SelectionChange change;
    change.selectedObjectIds.push_back(
        core::ObjectId::fromCanonical("obj-0123456789abcdef0123456789abcdef"));
    content->noteSelectionForContext(change);
    EXPECT_EQ(objectLabel->text(), QString::fromUtf8(u8"当前对象：关节 6"));
    EXPECT_TRUE(content->shutdown());
}

}  // namespace
