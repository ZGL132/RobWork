/**
 * @file   PolicySummaryCardGuiTest.cpp
 * @brief  UI-T07 GUI 层用例（QApplication＋真实 Widget 树——§12.1 第三层）：
 *         右栏工程策略摘要只读卡的呈现行为——分组行渲染与模型契约同源、
 *         未装载占位、卡面只读性（零可编辑策略控件）、修改策略入口的延期
 *         反馈、上下文注入触发的端口重拉（UI-POL-1 登记——ui.md §12.3）。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T07.json acceptance 1（摘要只读＋分组
 *     异名＋影响范围说明——POL-ID-3）/ acceptance 2（编辑入口＝延期提示，
 *     表单归阶段 B；ui 零判定权/零计算开关权威）/ acceptance 3（O-31：
 *     卡经 ShellWiring.policySource 自有端口承载——L5 适配点在装配层，
 *     ui 测试以可控替身承载，替身计数即端口消费证据）；
 *   - units/ui.md §6.7（策略摘要只读卡）、§12.3 UI-POL-1 行（v0.9 登记：
 *     前置/操作/预期/观测点）、§12.2 执行纪律、§3.1（"ui 测试以可控替身
 *     承载"）、§11.4（不虚构业务能力——延期提示口径）；
 *   - 同构先例：WorkbenchShellGuiTest.cpp（夹具形态/替身清单/事件泵）。
 *
 * 模型/GUI 分工：行文本的语义（分组异名/换算/不适用）在模型层
 * （PolicySummaryCardTest.cpp）逐行断言；本文件断言真实控件树的行为面，
 * 并以"行文本与 policySummaryRows 输出逐行同源"互证（UI-V3D-1 清单
 * 同源核对同款口径）。
 */

#include <gtest/gtest.h>

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QWidget>

#include <sdurws/ird/diagnostics/Catalog.hpp>
#include <sdurws/ird/diagnostics/Redaction.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include <sdurws/ird/ui/IWorkbenchShell.hpp>
#include <sdurws/ird/ui/PolicySummaryCard.hpp>
#include <sdurws/ird/ui/UiPorts.hpp>
#include <sdurws/ird/ui/UiProjections.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace sdurws::ird;
using sdurws::ird::ui::IWorkbenchShell;
using sdurws::ird::ui::PolicySummaryDisplayUnits;
using sdurws::ird::ui::PolicySummaryGroup;
using sdurws::ird::ui::PolicySummaryProjection;
using sdurws::ird::ui::PolicySummaryRow;
using sdurws::ird::ui::PolicyThresholdProjection;
using sdurws::ird::ui::ProjectContextProjection;
using sdurws::ird::ui::ShellWiring;
using sdurws::ird::ui::WorkbenchRegion;
using sdurws::ird::ui::kPolicyEditDeferredNotice;
using sdurws::ird::ui::policySummaryGroupTitle;
using sdurws::ird::ui::policySummaryRows;
using sdurws::ird::ui::PolicySummaryDisplayUnits;

// =====================================================================
// 测试替身（ui.md §3.1"ui 测试以可控替身承载"；§10.1 wiring 非空前置）
// =====================================================================

/// 诊断 sink 替身：本套件无用户级诊断产出面（append 记数暴露意外产条目）。
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

/// 脱敏服务替身：直通原文（仅满足 wiring 非空前置）。
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

/// 名称解析替身：恒不可解析（本套件无对象树消费面）。
class NullNameResolver final : public ui::IUiNameResolver {
public:
    std::optional<std::string> resolveObjectId(core::ObjectId) const override
    {
        return std::nullopt;
    }
};

/**
 * 策略摘要端口替身（C-10 的 ui 测试可控承载面）：持一份可变更的投影值；
 * summary() 调用计数即"卡从注入端口取数"的观测面（acc3——端口承载的
 * 消费证据；L5 真适配 policy::IPolicyProvider 归装配层任务）。
 */
class ControlledPolicySource final : public ui::IPolicySummarySource {
public:
    ui::PolicySummaryProjection summary() const override
    {
        ++m_calls;
        return m_projection;
    }

    void setProjection(const ui::PolicySummaryProjection& projection) { m_projection = projection; }
    int callCount() const { return m_calls; }

private:
    ui::PolicySummaryProjection m_projection{};
    mutable int m_calls = 0;
};

/// 已装载参考投影（与模型层 loadedProjection 同值——跨层同源基准）。
PolicySummaryProjection loadedProjection()
{
    PolicySummaryProjection summary;
    summary.available = true;
    summary.collisionDomainEnabled = true;
    summary.enabledDomainTokens = {"self", "environment", "tool"};
    summary.safetyClearance = PolicyThresholdProjection{true, 0.3};
    summary.nearLimitRatio = PolicyThresholdProjection{true, 0.85};
    summary.travelLimit =
        PolicyThresholdProjection{true, 4.0 * 3.141592653589793};
    summary.mandatoryPairCount = 2;
    summary.filterPairCount = 1;
    return summary;
}

// =====================================================================
// 夹具：每用例独立用户级设置目录（与 WorkbenchShellGuiTest 同款隔离面）
// =====================================================================

class PolicySummaryCardGuiTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_settingsDir = std::make_unique<QTemporaryDir>();
        ASSERT_TRUE(m_settingsDir->isValid());
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           m_settingsDir->path());
    }

    ShellWiring makeWiring()
    {
        ShellWiring wiring;
        wiring.eventBus = nullptr;  // 允许为空＝无事件测试场景（显式声明）
        wiring.diagSink = m_diagSink;
        wiring.redaction = m_redaction;
        wiring.devLog = nullptr;    // 允许为空＝无日志测试场景（显式声明）
        wiring.policySource = m_policySource;
        wiring.nameResolver = m_nameResolver;
        return wiring;
    }

    /// 用例事件泵（显示/布局变更落定——事件驱动判据）。
    static void pump() { QApplication::processEvents(); }

    /// 初始化壳并取主窗口（断言在调用方——保持用例内次序可读）。
    std::unique_ptr<IWorkbenchShell> startShell()
    {
        auto shell = ui::createWorkbenchShell();
        if (!shell->initialize(makeWiring())) {
            return nullptr;
        }
        if (shell->mainWindow() != nullptr) {
            shell->mainWindow()->show();
            pump();
        }
        return shell;
    }

    /// 取摘要卡容器（objectName 锚——找不到返回 nullptr）。
    static QWidget* findCard(QWidget* window)
    {
        return window == nullptr
            ? nullptr
            : window->findChild<QWidget*>(QString::fromLatin1("ird_policy_summary_card"));
    }

    /// 取卡内行标签（objectName＝"ird_policy_row_"＋行键）。
    static QLabel* findRowLabel(QWidget* card, const std::string& key)
    {
        return card == nullptr
            ? nullptr
            : card->findChild<QLabel*>(QString::fromLatin1("ird_policy_row_")
                                           + QString::fromStdString(key));
    }

    std::unique_ptr<QTemporaryDir> m_settingsDir;
    std::shared_ptr<RecordingDiagnosticSink> m_diagSink = std::make_shared<RecordingDiagnosticSink>();
    std::shared_ptr<PassthroughRedaction> m_redaction = std::make_shared<PassthroughRedaction>();
    std::shared_ptr<ControlledPolicySource> m_policySource = std::make_shared<ControlledPolicySource>();
    std::shared_ptr<NullNameResolver> m_nameResolver = std::make_shared<NullNameResolver>();
};

// =====================================================================
// UI-POL-1①：已装载投影的分组行渲染（acc1 数据面＋acc3 端口消费）
// =====================================================================

/**
 * §12.3 UI-POL-1 行（装载形态）：initialize 后右栏卡呈现分组行集——
 * 观测点①分组标题与模型契约同文（分组异名的呈现面）；②各行文本与
 * policySummaryRows 输出逐行同源（GUI 只渲染不持语义）；③端口消费计数
 * ≥1（卡从 ShellWiring.policySource 取数——acc3 注入承载的行为证据）。
 */
TEST_F(PolicySummaryCardGuiTest, LoadedProjection_RendersModelRows_UI_T07_ACC1_ACC3)
{
    IRD_TEST_INFO("UX-08", {}, std::nullopt);
    m_policySource->setProjection(loadedProjection());
    std::unique_ptr<IWorkbenchShell> shell = startShell();
    ASSERT_NE(shell, nullptr) << "壳初始化失败";
    QWidget* window = shell->mainWindow();
    ASSERT_NE(window, nullptr);

    // 卡容器在位（右栏——§6.7"诊断与设置区"内嵌）。
    QWidget* card = findCard(window);
    ASSERT_NE(card, nullptr) << "右栏策略摘要卡缺失";

    // 观测点③：端口消费（initialize 装配期取数≥1——注入承载的消费证据）。
    EXPECT_GE(m_policySource->callCount(), 1) << "卡未从注入端口取数";

    // 观测点①：两分组标题在位且与模型契约同文（分组异名——POL-ID-3）。
    auto* authorityHeader =
        card->findChild<QLabel*>(QString::fromLatin1("ird_policy_group_authority"));
    auto* displayHeader =
        card->findChild<QLabel*>(QString::fromLatin1("ird_policy_group_display"));
    ASSERT_NE(authorityHeader, nullptr) << "计算权威组标题缺失";
    ASSERT_NE(displayHeader, nullptr) << "会话显示设置组标题缺失";
    EXPECT_EQ(authorityHeader->text().toStdString(),
              policySummaryGroupTitle(PolicySummaryGroup::PolicyAuthority));
    EXPECT_EQ(displayHeader->text().toStdString(),
              policySummaryGroupTitle(PolicySummaryGroup::SessionDisplay));

    // 观测点②：逐行与模型契约同源（"label：value"——GUI 不二次加工）。
    const std::vector<PolicySummaryRow> model = policySummaryRows(
        loadedProjection(), PolicySummaryDisplayUnits::siDefaults());
    ASSERT_FALSE(model.empty());
    for (const PolicySummaryRow& row : model) {
        QLabel* label = findRowLabel(card, row.key);
        ASSERT_NE(label, nullptr) << "行控件缺失: " << row.key;
        const QString expected = QString::fromUtf8(row.label) + u8"："
                                 + QString::fromUtf8(row.value);
        EXPECT_EQ(label->text(), expected)
            << "行文本与模型契约不同源: " << row.key;
    }
}

// =====================================================================
// UI-POL-1②：未装载占位（acc1 不虚构数值）
// =====================================================================

/**
 * §12.3 UI-POL-1 行（未装载形态）：默认投影（available=false）→ 占位行
 * "工程策略：未装载"；策略字段行不出现（不虚构数值）；显示设置组照常
 * 在位（边界说明与策略装载无关）。
 */
TEST_F(PolicySummaryCardGuiTest, Unloaded_RendersPlaceholder_UI_T07_ACC1)
{
    IRD_TEST_INFO("UX-08", {}, std::nullopt);
    std::unique_ptr<IWorkbenchShell> shell = startShell();
    ASSERT_NE(shell, nullptr);
    QWidget* card = findCard(shell->mainWindow());
    ASSERT_NE(card, nullptr);

    auto* placeholder = findRowLabel(card, "policy");
    ASSERT_NE(placeholder, nullptr) << "未装载占位行缺失";
    EXPECT_EQ(placeholder->text(),
              QString::fromUtf8("工程策略") + u8"：" + QString::fromUtf8("未装载"));
    EXPECT_EQ(findRowLabel(card, "collision"), nullptr) << "未装载形态不应出现碰撞行";
    EXPECT_NE(findRowLabel(card, "display-separation"), nullptr)
        << "显示设置组应恒在";
}

// =====================================================================
// UI-POL-1③：卡面只读性（acc1"摘要只读"红线）
// =====================================================================

/**
 * 摘要只读红线：卡子树内零可编辑策略控件（行全部是只读 QLabel）；唯一
 * 可交互控件＝"修改策略…"跳转按钮（acc2 入口，非策略编辑器）——不以
 * 控件存在推断编辑能力（§11.4）。
 */
TEST_F(PolicySummaryCardGuiTest, CardIsReadOnly_NoEditableControls_UI_T07_ACC1)
{
    IRD_TEST_INFO("UX-08", {}, std::nullopt);
    m_policySource->setProjection(loadedProjection());
    std::unique_ptr<IWorkbenchShell> shell = startShell();
    ASSERT_NE(shell, nullptr);
    QWidget* card = findCard(shell->mainWindow());
    ASSERT_NE(card, nullptr);

    // 可编辑控件全族扫描（QLineEdit/QCheckBox/QSpinBox/QComboBox/QSlider/
    // QDateTimeEdit/QDoubleSpinBox）——零命中。
    EXPECT_EQ(card->findChildren<QLineEdit*>().size(), 0);
    EXPECT_EQ(card->findChildren<QCheckBox*>().size(), 0);
    EXPECT_EQ(card->findChildren<QSpinBox*>().size(), 0);
    EXPECT_EQ(card->findChildren<QDoubleSpinBox*>().size(), 0);
    EXPECT_EQ(card->findChildren<QComboBox*>().size(), 0);
    EXPECT_EQ(card->findChildren<QSlider*>().size(), 0);
    EXPECT_EQ(card->findChildren<QDateTimeEdit*>().size(), 0);

    // 唯一按钮＝修改策略跳转入口（无第二可交互面）。
    const auto buttons = card->findChildren<QAbstractButton*>();
    ASSERT_EQ(buttons.size(), 1) << "卡面出现未登记的可交互控件";
    EXPECT_EQ(buttons.front()->objectName(), QString::fromLatin1("ird_policy_edit_button"));
    EXPECT_EQ(buttons.front()->text(), QString::fromUtf8("修改策略…"));
}

// =====================================================================
// UI-POL-1④：修改策略入口＝延期提示（acc2 表单归阶段 B）
// =====================================================================

/**
 * 点击"修改策略…"：呈现延期提示（kPolicyEditDeferredNotice 原文——表单
 * 归阶段 B 的占位口径）；不打开任何编辑器、不触达策略端口（提交权在
 * ①命令端口——阶段 A 无表单即零提交面）。
 */
TEST_F(PolicySummaryCardGuiTest, EditEntry_ShowsDeferredNotice_UI_T07_ACC2)
{
    IRD_TEST_INFO("UX-08", {}, std::nullopt);
    m_policySource->setProjection(loadedProjection());
    std::unique_ptr<IWorkbenchShell> shell = startShell();
    ASSERT_NE(shell, nullptr);
    QWidget* card = findCard(shell->mainWindow());
    ASSERT_NE(card, nullptr);

    auto* button = card->findChild<QAbstractButton*>(
        QString::fromLatin1("ird_policy_edit_button"));
    ASSERT_NE(button, nullptr);
    auto* notice = card->findChild<QLabel*>(
        QString::fromLatin1("ird_policy_edit_notice"));
    ASSERT_NE(notice, nullptr);
    EXPECT_FALSE(notice->isVisible()) << "提示在点击前不应可见";

    const int callsBefore = m_policySource->callCount();
    button->click();
    pump();

    // 延期提示可见且为契约原文；编辑器零打开（卡面仍只读）；端口零触达。
    EXPECT_TRUE(notice->isVisible());
    EXPECT_EQ(notice->text(), QString::fromUtf8(kPolicyEditDeferredNotice));
    EXPECT_EQ(card->findChildren<QLineEdit*>().size(), 0);
    EXPECT_EQ(m_policySource->callCount(), callsBefore)
        << "编辑入口不得触达策略端口";
}

// =====================================================================
// UI-POL-1⑤：上下文注入触发的端口重拉（acc3 注入承载＋刷新锚点）
// =====================================================================

/**
 * presentProjectContext 注入后卡重拉端口快照：替身投影变更→再注入→行
 * 文本随新快照更新（事件驱动刷新归投影管线的阶段 A 锚点语义——
 * ShellWiring.policySource 是唯一数据源，无 ui 侧缓存改写）。
 */
TEST_F(PolicySummaryCardGuiTest, RefreshOnContextInjection_UI_T07_ACC3)
{
    IRD_TEST_INFO("UX-08", {}, std::nullopt);
    m_policySource->setProjection(loadedProjection());
    std::unique_ptr<IWorkbenchShell> shell = startShell();
    ASSERT_NE(shell, nullptr);
    QWidget* card = findCard(shell->mainWindow());
    ASSERT_NE(card, nullptr);
    auto* collisionRow = findRowLabel(card, "collision");
    ASSERT_NE(collisionRow, nullptr);
    EXPECT_EQ(collisionRow->text(),
              QString::fromUtf8("碰撞检查") + u8"："
                  + QString::fromUtf8("启用（self、environment、tool）"));

    // 变更替身投影（碰撞停用）→ 上下文再注入（壳的刷新锚点）→ 行更新。
    // 注意：刷新＝卡内容清空重建——重建前行控件的指针全部失效，断言必须
    // 以卡容器（存活面）为锚重新查找行控件（objectName 稳定可定位）。
    PolicySummaryProjection updated = loadedProjection();
    updated.collisionDomainEnabled = false;
    updated.enabledDomainTokens.clear();
    m_policySource->setProjection(updated);

    const int callsBefore = m_policySource->callCount();
    shell->presentProjectContext(ProjectContextProjection{});
    pump();

    EXPECT_GT(m_policySource->callCount(), callsBefore) << "上下文注入未触发端口重拉";
    QLabel* refreshedRow = findRowLabel(card, "collision");
    ASSERT_NE(refreshedRow, nullptr) << "重建后碰撞行缺失";
    EXPECT_EQ(refreshedRow->text(),
              QString::fromUtf8("碰撞检查") + u8"：" + QString::fromUtf8("停用"));

    shell->shutdown();
}

}  // namespace
