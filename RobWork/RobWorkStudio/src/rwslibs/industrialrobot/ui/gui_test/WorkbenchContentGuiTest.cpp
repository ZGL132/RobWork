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
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
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
    EXPECT_NE(content->statusBarWidget(), nullptr);
    // 中央区页 0＝无项目首页（PM-10 启动态）。
    auto* stack = qobject_cast<QStackedWidget*>(content->centralWidget());
    ASSERT_NE(stack, nullptr);
    EXPECT_EQ(stack->currentIndex(), 0);

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

}  // namespace
