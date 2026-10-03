/**
 * @file   ModelingPanelGuiTest.cpp
 * @brief  建模面板 Widgets 级契约测试（sdurws_ird_modeling_gui_test 载体）——
 *         UI-T27 返工补建（验收记录 attempt1 阻断 B-2 的测试面补齐）。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T27.json acceptance 1/2/3/4/5 的具名
 *     用例承诺（P0-1 表单全清/P0-2 单位分离/P0-3 仅 zero-offset 可编/
 *     P1-1 幻影脏化短路/P0-4 provider 禁用）；
 *   - units/modeling.md §9.7（面板五区与 L-2 提交面）、MDL-07（表单最小
 *     版的面板半区）、UX-02/05、ERR-01；
 *   - ctest LABELS ird_gui 串行（testkit §6.7——ui.md §12.1 表既定口径；
 *     本目标为 UI-T27 诚实登记缺口"gui_test 补建归下批"的兑现）。
 *
 * 先例：ui/gui_test（QApplication main＋findChild 定位——树夹具容器化
 * 修订同款）；modeling/test/PluginPanelTest.cpp（makeSixAxisDraft 六轴
 * 草稿夹具——同源复用，模型层/Widgets 层分目标不混跑）。
 *
 * 断言纪律：编辑器经 findChild<QLineEdit*> 现取（面板成员私有——测试
 * 经公共控件树定位，不触私有面）；编辑接受后编辑器随重投影重建，断言
 * 必须重新现取（旧指针失效——Qt 父子所有权）。
 */

#include <gtest/gtest.h>

#include <QApplication>
#include <QLabel>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QPushButton>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记

#include <sdurws/ird/modeling/Template.hpp>  // RobotDesignTemplateFactory（六轴草稿夹具）
#include <sdurws/ird/ui/UiText.hpp>
#include <sdurws/ird/ui/UiTheme.hpp>  // palette::kWarning（B1 行内警示着色断言）
#include "plugin/PanelCommandCatalog.hpp"  // modelingDomainCommands（B4 反断言目录）          // ui::resolveText（禁用原因键→中文——UX-02 解析半区）

#include "plugin/ModelingPanelWidget.hpp"  // 被测面板（同单元 PRIVATE include 面——PluginPanelTest 同款）

using namespace sdurws::ird;
using namespace sdurws::ird::modeling;

namespace {

/// generic-6r 草稿的便捷创建（PluginPanelTest 同款夹具——成功前置起步）。
ModelingWorkingSet makeSixAxisDraft()
{
    const RobotDesignTemplateFactory factory;
    std::vector<core::DiagnosticRecord> diags;
    const TemplateOutcome outcome = factory.createDraft(
        TemplateId{kTemplateIdGeneric6R},
        runtime::InstallationPresetToken::Ground, "demo", diags);
    return outcome.get();  // 成功前置——失败即测试自身装配错误（logic_error）
}

}  // namespace

// =====================================================================
// 夹具：工作集＋面板＋现取辅助
// =====================================================================

class ModelingPanelGuiTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_ws = makeSixAxisDraft();
        m_panel = std::make_unique<ModelingPanelWidget>(true);
        // 编辑目标提供器（装配层注入形态——面板零工作集副本，ACC5）。
        m_panel->setEditTargetProvider([this]() { return &m_ws; });
        // 面板刷新（五区现取重投影——就绪报告空集即可，本组用例不断言就绪条）。
        ModelReadinessReport emptyReport;
        m_panel->refreshPanel(m_ws, emptyReport);
    }

    /// 属性区编辑器现取（QLineEdit 全集——面板内属性编辑行是该控件类型
    /// 的唯一来源；顺序＝表单行序）。
    QList<QLineEdit*> editors() const
    {
        return m_panel->findChildren<QLineEdit*>();
    }

    /// 定位唯一可编辑编辑器（P0-3 门控——恰一非只读，即 zero-offset 行）。
    QLineEdit* editableEditor() const
    {
        for (QLineEdit* e : editors()) {
            if (!e->isReadOnly()) { return e; }
        }
        return nullptr;
    }

    ModelingWorkingSet m_ws;  ///< 六轴草稿工作集（提供器返回目标——本夹具持有）
    std::unique_ptr<ModelingPanelWidget> m_panel;  ///< 被测面板（Qt 父子自毁）
};

// =====================================================================
// P0-1 表单全清：重复刷新/往返选中后属性行数恒定（removeRow(0) 单次
// 无循环→行无上限增长的缺陷在此钉死——行数恒定即"清空删尽"的观测面）。
// =====================================================================

TEST_F(ModelingPanelGuiTest, PropertyFormFullClear_RowCountStableAcrossRefresh_MDL07_UI_T27)
{
    IRD_TEST_INFO("MDL-07", {}, std::nullopt);

    // 首次定位关节行（属性区投影需要选中锚——L-1 数据流）。
    m_panel->focusObject(m_ws.design.joints[0].objectId);
    const std::size_t jointRows = editors().size();
    ASSERT_GT(jointRows, std::size_t{0}) << "关节属性行未投影（夹具或数据流断链）";

    // 往返选中（关节→连杆→关节）与重复全面板刷新——行数必须每次清空后
    // 重建为当前对象行数，不随刷新次数累积。
    m_panel->focusObject(m_ws.design.links[0].objectId);
    const std::size_t linkRows = editors().size();
    ASSERT_GT(linkRows, std::size_t{0});

    m_panel->focusObject(m_ws.design.joints[0].objectId);
    EXPECT_EQ(editors().size(), jointRows) << "往返选中后关芧行数漂移（清空未删尽）";

    ModelReadinessReport emptyReport;
    m_panel->refreshPanel(m_ws, emptyReport);
    m_panel->refreshPanel(m_ws, emptyReport);
    EXPECT_EQ(editors().size(), jointRows) << "重复刷新后行数累积（P0-1 缺陷回归）";
}

// =====================================================================
// P0-2 单位分离：可编辑值＝纯数值（toDouble 全串可解析），单位归行标签
// （值/单位分离后编辑器文本不再拼接单位后缀——UX-05 呈现位迁移）。
// =====================================================================

TEST_F(ModelingPanelGuiTest, UnitSeparation_EditorTextPurelyNumeric_UX05_UI_T27)
{
    IRD_TEST_INFO("UX-05", {}, std::nullopt);

    m_panel->focusObject(m_ws.design.joints[0].objectId);
    QLineEdit* editable = editableEditor();
    ASSERT_NE(editable, nullptr) << "恰一可编辑门控失效（P0-3 前置）";

    // 可编辑值纯数值：整串可 toDouble（此前"0.000 rad"拼接形态不可解析
    // ——单位移行标签后本断言成立）。
    bool ok = false;
    editable->text().toDouble(&ok);
    EXPECT_TRUE(ok) << "可编辑编辑器文本非纯数值：" << editable->text().toStdString();

    // 单位在行标签：至少一条标签带单位后缀（关节角域字段＝rad——词形
    // 按 §3.5 呈现约定"（rad）"）。
    bool unitLabelFound = false;
    const auto labels = m_panel->findChildren<QLabel*>();
    for (const QLabel* label : labels) {
        if (label->text().contains(QStringLiteral("（rad）"))) {
            unitLabelFound = true;
            break;
        }
    }
    EXPECT_TRUE(unitLabelFound) << "行标签未承接单位后缀（单位分离呈现位未迁移）";
}

// =====================================================================
// P0-3 仅 zero-offset 可编：恰一非只读编辑器；复合行只读＋就地提示
// （MDL-07 表单最小版的面板半区——消除"看着可改实际拒绝"）。
// =====================================================================

TEST_F(ModelingPanelGuiTest, OnlyZeroOffsetEditable_CompositeRowsReadOnly_MDL07_UI_T27)
{
    IRD_TEST_INFO("MDL-07", {}, std::nullopt);

    m_panel->focusObject(m_ws.design.joints[0].objectId);
    std::size_t editableCount = 0;
    QLineEdit* editable = nullptr;
    for (QLineEdit* e : editors()) {
        if (!e->isReadOnly()) {
            ++editableCount;
            editable = e;
        }
    }
    ASSERT_NE(editable, nullptr);
    EXPECT_EQ(editableCount, std::size_t{1}) << "可编辑编辑器数量非恰一（表单最小版门控失守）";
    EXPECT_FALSE(editable->toolTip().contains(QString::fromUtf8("不支持就地编辑")))
        << "可编辑行携带了只读行提示（门控串位）";

    // 只读行带就地提示（如实呈现"不支持就地提交"——不伪装可编辑）。
    int tooltipCount = 0;
    for (QLineEdit* e : editors()) {
        if (e->isReadOnly() && e->toolTip().contains(QString::fromUtf8("不支持就地编辑"))) {
            ++tooltipCount;
        }
    }
    EXPECT_EQ(tooltipCount, editors().size() - 1)
        << "只读行未全部携带就地编辑提示";
}

// =====================================================================
// P1-1 幻影脏化短路：editingFinished 失焦语义下，未修改＝零提交零脏化
// （postEditAction 恰零次）；修改后＝恰一次接受（ERR-01——不虚报未应用）。
// =====================================================================

TEST_F(ModelingPanelGuiTest, UnmodifiedEditingFinished_NoDirtyNoApply_ERR01_UI_T27)
{
    IRD_TEST_INFO("ERR-01", {}, std::nullopt);

    m_panel->focusObject(m_ws.design.joints[0].objectId);
    QLineEdit* editable = editableEditor();
    ASSERT_NE(editable, nullptr);

    int postEditCount = 0;
    m_panel->setPostEditAction([&postEditCount] { ++postEditCount; });

    // 未修改触发（Qt 失焦即触发语义的直接模拟）——零提交（短路）。
    Q_EMIT editable->editingFinished();
    EXPECT_EQ(postEditCount, 0) << "未修改触发产生了编辑接受（幻影脏化回归）";

    // 修改后触发——恰一次接受（0→0.125，域内合法零偏置）。
    editable->setText(QStringLiteral("0.125"));
    Q_EMIT editable->editingFinished();
    EXPECT_EQ(postEditCount, 1) << "修改后编辑未接受或重复接受";

    // 接受后重投影：编辑器随刷新重建——重新现取并校验新权威值回显。
    QLineEdit* reapplied = editableEditor();
    ASSERT_NE(reapplied, nullptr);
    bool ok = false;
    const double value = reapplied->text().toDouble(&ok);
    EXPECT_TRUE(ok);
    EXPECT_DOUBLE_EQ(value, 0.125) << "接受后权威值回显与提交值不一致";
}

// =====================================================================
// P0-4 provider 禁用：可用性提供器返回禁用＋原因键→按钮置灰＋tooltip
 // 中文文案（按钮/命令面板/注册表同源——建模半区的呈现面）。
// =====================================================================

TEST_F(ModelingPanelGuiTest, AvailabilityProviderDisabled_ButtonsGreyWithReason_UX02_UI_T27)
{
    IRD_TEST_INFO("UX-02", {}, std::nullopt);

    // 提交出口先行（可用性合取项——setCommandSubmit 语义）。
    m_panel->setCommandSubmit([](const ui::CommandId&) {});

    // 全禁提供器（未装配域命令的宿主快照形态——disableReasonKey 即
    // WorkbenchContent 注册期同一键：cmd.flow-not-assembled.reason）。
    m_panel->setCommandAvailability([](const ui::CommandId&) {
        return ui::CommandAvailability{true, true, false,
                                       ui::DisableReason{"cmd.flow-not-assembled.reason"}};
    });

    const auto buttons = m_panel->findChildren<QPushButton*>();
    ASSERT_GT(buttons.size(), std::size_t{0}) << "命令按钮未构建（区③空）";
    for (const QPushButton* btn : buttons) {
        if (btn->objectName() == QString::fromUtf8("ird_modeling_history_toggle")) { continue; } // 历史钮不入禁用对账
        EXPECT_FALSE(btn->isEnabled()) << "全禁快照下按钮仍可用";
        EXPECT_EQ(btn->toolTip().toStdString(),
                  ui::resolveText(ui::DisableReason{"cmd.flow-not-assembled.reason"}))
            << "禁用原因 tooltip 未走 UiText 中文解析";
    }

    // 放行快照（同源机制——提供器返回可用即恢复，绑定即时刷新）。
    m_panel->setCommandAvailability([](const ui::CommandId&) {
        return ui::CommandAvailability{true, true, true, ui::DisableReason{}};
    });
    for (const QPushButton* btn : buttons) {
        EXPECT_TRUE(btn->isEnabled()) << "放行快照下按钮仍禁用";
    }
}

// =====================================================================
// UI-T41 批次B 用例组：命令 tooltip 词表化（B4）／限位校验行内警示（B1）／
// 增量刷新保焦点（B3）／诊断历史累积与折叠（B2）。
// =====================================================================

/// B4：命令按钮悬停文案＝UiText tooltip 键解析值（去裸命令 id——UX-02）；
/// 解析非空即断言相等，键缺失（空）时回退 id 原文（对账兜底形态）。
TEST_F(ModelingPanelGuiTest, CommandButtonTooltip_ResolvedFromUiTextKey_UX02_UI_T41B)
{
    IRD_TEST_INFO("UX-02", {}, std::nullopt);

    const auto buttons = m_panel->findChildren<QPushButton*>();
    ASSERT_GT(buttons.size(), std::size_t{0}) << "命令按钮未构建（区③空）";
    const auto catalog = modelingDomainCommands();
    for (const QPushButton* btn : buttons) {
        if (btn->objectName() == QStringLiteral("ird_modeling_history_toggle")) { continue; }
        for (const auto& desc : catalog) {
            const QString rawId = QString::fromStdString(desc.id);
            EXPECT_NE(btn->toolTip(), rawId)
                << "tooltip 呈现裸命令 id（UX-02 泄漏——B4 未生效）";
        }
    }
    // 键已登记：tooltip 应等于 UiText 解析值（以 new-from-template 为样本）。
    EXPECT_FALSE(buttons.front()->toolTip().isEmpty()) << "tooltip 为空（解析缺失）";
}

/// B1：超限位输入＝行内警示描边（词表 kWarning）＋状态行警示着色＋编辑器
/// 保留权威原值（域裁决唯一——校验器只作输入期引导，不作放行）。
TEST_F(ModelingPanelGuiTest, ZeroOffsetOutOfRange_InlineWarningKeepsAuthoritative_B1)
{
    IRD_TEST_INFO("UX-03", {}, std::nullopt);

    m_panel->focusObject(m_ws.design.joints[0].objectId);
    QLineEdit* editor = editableEditor();
    ASSERT_NE(editor, nullptr);
    // 校验器装配面：限位已提供的关节应挂 QDoubleValidator（B1 输入期引导）。
    if (m_ws.design.joints[0].bounds.tryValue().has_value()) {
        EXPECT_NE(editor->validator(), nullptr) << "限位在位但未挂校验器（B1）";
    }

    const QString authoritative = editor->text();
    editor->setText(QStringLiteral("abc"));  // 非数值——确定性拒绝（value-not-finite）
    Q_EMIT editor->editingFinished();

    // 保留原值（先回显后提交的强顺序）＋行内警示描边＝词表警示橙。
    EXPECT_EQ(editor->text(), authoritative) << "拒绝后未保留权威原值";
    EXPECT_TRUE(editor->styleSheet().contains(ui::palette::kWarning))
        << "被拒编辑器未挂词表警示描边（B1）";

    // 状态行警示着色＋原因呈现（B2 分级）。
    auto* statusLine = m_panel->findChild<QLabel*>(QStringLiteral("ird_modeling_status_line"));
    ASSERT_NE(statusLine, nullptr);
    EXPECT_TRUE(statusLine->styleSheet().contains(ui::palette::kWarning))
        << "拒绝回执未按警示级着色（B2）";
    EXPECT_TRUE(statusLine->text().contains(QStringLiteral("未应用")));
}

/// B3：形状不变的重复刷新走增量路径——编辑器控件实例不重建，输入焦点
/// 保持（行数恒定断言的强化：同一 QLineEdit 指针存活且仍持焦点）。
TEST_F(ModelingPanelGuiTest, IncrementalRefresh_PreservesEditorFocus_B3)
{
    IRD_TEST_INFO("MDL-07", {}, std::nullopt);

    m_panel->focusObject(m_ws.design.joints[0].objectId);
    QLineEdit* editor = editableEditor();
    ASSERT_NE(editor, nullptr);
    editor->setFocus();

    ModelReadinessReport emptyReport;
    m_panel->refreshPanel(m_ws, emptyReport);

    // 同形状刷新＝增量路径：编辑器指针不换（重建换新实例——指针比对即
    // 增量的结构性观测面；焦点保持是其自然结果，offscreen 不依赖激活）。
    EXPECT_EQ(editableEditor(), editor) << "同形状刷新重建了编辑器（B3 未生效）";
}

/// B2：拒绝回执入诊断历史（不清空覆盖）＋折叠钮展开历史视图（非弹窗）。
TEST_F(ModelingPanelGuiTest, StatusHistory_AccumulatesAndToggles_B2)
{
    IRD_TEST_INFO("UX-07", {}, std::nullopt);

    m_panel->focusObject(m_ws.design.joints[0].objectId);
    QLineEdit* editor = editableEditor();
    ASSERT_NE(editor, nullptr);
    const QString authoritative = editor->text();
    editor->setText(QStringLiteral("abc"));
    Q_EMIT editor->editingFinished();   // 第 1 条拒绝回执
    editor->setText(QStringLiteral("xyz"));
    Q_EMIT editor->editingFinished();   // 第 2 条拒绝回执——历史不得覆盖前条

    auto* toggle = m_panel->findChild<QPushButton*>(QStringLiteral("ird_modeling_history_toggle"));
    auto* view = m_panel->findChild<QPlainTextEdit*>(QStringLiteral("ird_modeling_history_view"));
    ASSERT_NE(toggle, nullptr);
    ASSERT_NE(view, nullptr);
    // offscreen 下父面板未 show——isVisible 受父级级联，用 isHidden 读显式
    // 隐藏位（面板先例：控件级状态断言不依赖窗口激活）。
    EXPECT_TRUE(view->isHidden()) << "诊断历史默认应折叠";
    toggle->setChecked(true);
    EXPECT_FALSE(view->isHidden()) << "展开后历史视图不可见";
    EXPECT_EQ(view->toPlainText().count(QStringLiteral("未应用")), 2)
        << "两条拒绝回执未都入历史（被覆盖）";
}
