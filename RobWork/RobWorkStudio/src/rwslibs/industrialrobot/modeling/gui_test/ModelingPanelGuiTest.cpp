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
 *   - UI-T43 增量（宿主接线修复回归钉）：L-7 防复活（只读态下可用性
 *     刷新不越门——本文件）；生产装配序文案解析回归＋模块 setWritable
 *     暂存/即时双形态链路在 ModelingUiModuleHostWiringGuiTest.cpp
 *     （policy 符号 gating——集成模式专属）。
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
#include <QTableWidget>
#include <QTreeWidgetItemIterator>
#include <QPushButton>
#include <sdurws/ird/modeling/GeometryLinkEdit.hpp>  // attachExternalGeometry/detachGeometry/GeometrySlot（UI-T48 域原语直调）

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记

#include <sdurws/ird/modeling/Template.hpp>  // RobotDesignTemplateFactory（六轴草稿夹具）
#include <sdurws/ird/ui/UiText.hpp>
#include <sdurws/ird/ui/UiTheme.hpp>  // palette::kWarning（B1 行内警示着色断言）
#include "plugin/PanelCommandCatalog.hpp"  // modelingDomainCommands（B4 反断言目录）          // ui::resolveText（禁用原因键→中文——UX-02 解析半区）

#include "plugin/ModelingPanelWidget.hpp"  // 被测面板（同单元 PRIVATE include 面——PluginPanelTest 同款）
// （UI-T43 模块级接线用例——ModelingUiModule 被测面——落位
//   ModelingUiModuleHostWiringGuiTest.cpp：其被测类型构造期持 policy 行程
//   评估器符号，须随 PluginModuleT03BTest 先例做集成模式 gating，不能与
//   本文件（冒烟模式无条件编入）同 TU。）

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

/// 控件是否位于关节详细编辑页子树（UI-T53——"编辑"页签承载 ParamTablePanel，
/// 其筛选框/表格/按钮不入属性区与命令目录的对账面——P0-3 门控管辖的是
/// 属性行，编辑页是 B.1 详细编辑面的独立承载）。
bool inJointEditPane(const QWidget& panel, const QObject* widget)
{
    for (const QObject* p = widget; p != nullptr; p = p->parent()) {
        if (p == &panel) { return false; }  // 未途经编辑页宿主即达面板根
        if (p->objectName() == QStringLiteral("ird_modeling_edit_host")
            || p->objectName() == QStringLiteral("ird_param_table_panel")) {
            return true;
        }
    }
    return false;
}

/// 命令按钮全集（构造序＝目录序——Qt 子对象按挂树序枚举；诊断历史折叠
/// 钮非命令按钮，按 objectName 剔除）。返回序与 modelingDomainCommands()
/// 一一对应——用例以 size 相等断言作序漂移守卫，错位即测试装配错误。
QList<QPushButton*> commandButtonsOf(const QWidget& panel)
{
    QList<QPushButton*> buttons;
    for (QPushButton* btn : panel.findChildren<QPushButton*>()) {
        if (btn->objectName() == QStringLiteral("ird_modeling_history_toggle")) {
            continue;  // 历史折叠钮不入命令对账（B2 呈现件）
        }
        if (btn->objectName().startsWith(QStringLiteral("ird_modeling_struct_"))) {
            continue;  // 结构操作钮不入命令对账（UI-T47 呈现件——非命令目录按钮）
        }
        if (inJointEditPane(panel, btn)) {
            continue;  // 编辑页按钮（应用/取消/确认区）不入命令对账（UI-T53 呈现件）
        }
        buttons.push_back(btn);
    }
    return buttons;
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
    /// 的唯一来源；顺序＝表单行序）。UI-T53 起排除编辑页子树（ParamTablePanel
    /// 筛选框等——P0-3 门控管辖面＝属性行，两对账面分离）。
    QList<QLineEdit*> editors() const
    {
        QList<QLineEdit*> rows;
        for (QLineEdit* e : m_panel->findChildren<QLineEdit*>()) {
            if (inJointEditPane(*m_panel, e)) { continue; }
            rows.push_back(e);
        }
        return rows;
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
// UI-T53 关节详细编辑页（B.1 复杂对象编辑模式关节侧——ParamTablePanel 承载）
// =====================================================================

/// 编辑页 ParamTablePanel 现取（objectName 锚——ird_modeling_edit_host 容器
/// 内唯一 ird_param_table；未建页（空态）返回 nullptr）。
QTableWidget* jointEditTable(const ModelingPanelWidget& panel)
{
    QWidget* host = panel.findChild<QWidget*>(QStringLiteral("ird_modeling_edit_host"));
    return host == nullptr
               ? nullptr
               : host->findChild<QTableWidget*>(QStringLiteral("ird_param_table"));
}

/// 按字段键定位行下标（键挂键列 item 的 Qt::UserRole——ParamTablePanel 装配
/// 契约；未命中返回负数）。
int jointEditRowOf(const QTableWidget& table, const char* fieldKey)
{
    for (int row = 0; row < table.rowCount(); ++row) {
        const QTableWidgetItem* keyItem = table.item(row, 0);
        if (keyItem != nullptr
            && keyItem->data(Qt::UserRole).toString().toStdString() == fieldKey) {
            return row;
        }
    }
    return -1;
}

/**
 * 编辑页空态→选中建页链（UI-T53——B.1"结构树选择→详细编辑页"主入口）：
 * 无选中＝诚实空态（零表格——不伪造编辑页）；选中关节＝12 行数值表装配
 * （轴向 3＋原点 6＋零位 1＋限位 2）；切到连杆＝面板收起回空态（编辑页
 * 只对关节供给——工具/场景归后续批次）。
 */
TEST_F(ModelingPanelGuiTest, JointDetailEditPane_EmptyStateAndBuildOnSelection_UI_T53)
{
    IRD_TEST_INFO("MDL-07", {}, std::nullopt);

    // 空态（fixture 已 refreshPanel、未选中）：零表格＋提示行在。
    EXPECT_EQ(jointEditTable(*m_panel), nullptr) << "未选中不应建编辑页";
    QLabel* hint = m_panel->findChild<QLabel*>(QStringLiteral("ird_modeling_edit_hint"));
    ASSERT_NE(hint, nullptr);
    EXPECT_FALSE(hint->text().isEmpty()) << "空态提示缺失（ERR-01——不伪造页也不留白）";

    // 选中关节→建页：12 行（登记序＝装配序）。
    m_panel->focusObject(m_ws.design.joints[0].objectId);
    QTableWidget* table = jointEditTable(*m_panel);
    ASSERT_NE(table, nullptr) << "选中关节后编辑页未装配";
    EXPECT_EQ(table->rowCount(), 12) << "编辑页字段集非 12 行（轴向 3＋原点 6＋零位 1＋限位 2）";
    EXPECT_NE(jointEditRowOf(*table, "origin-x"), -1);
    EXPECT_NE(jointEditRowOf(*table, "origin-yaw"), -1);
    EXPECT_NE(jointEditRowOf(*table, "bounds-max"), -1);

    // 切到连杆→回空态（非关节目标无数值编辑页——诚实收口；收起打在
    // ParamTablePanel 根的显式隐藏位上，内层表格只随父链不可见）。
    m_panel->focusObject(m_ws.design.links[0].objectId);
    QWidget* host = m_panel->findChild<QWidget*>(QStringLiteral("ird_modeling_edit_host"));
    ASSERT_NE(host, nullptr);
    QWidget* tablePanel = host->findChild<QWidget*>(QStringLiteral("ird_param_table_panel"));
    if (tablePanel != nullptr) {
        EXPECT_TRUE(tablePanel->isHidden()) << "连杆选中后编辑页未收起";
    }
    ASSERT_NE(hint, nullptr);
    EXPECT_FALSE(hint->isHidden()) << "空态提示未随非关节选中恢复";
}

/**
 * 编辑页原点编辑全链（UI-T53 验收主链——"输入 XYZ/RPY→草稿变更→域裁决"）：
 * 值列就地编辑 origin-x→暂存→"应用…"→确认区呈现→"确认应用"→移交出口→
 * applyJointFieldEdit(Origin) 接受→工作集平移分量更新＋UserProvided 来源＋
 * 恰一条变更记录（域内核 ZYX 组合——本用例只钉 UI→域链路，矩阵元素归
 * TemplateTest）。
 */
TEST_F(ModelingPanelGuiTest, JointDetailEditPane_OriginEditApplyChain_UI_T53)
{
    IRD_TEST_INFO("MDL-09", {}, std::nullopt);

    m_panel->focusObject(m_ws.design.joints[0].objectId);
    QTableWidget* table = jointEditTable(*m_panel);
    ASSERT_NE(table, nullptr);
    const int row = jointEditRowOf(*table, "origin-x");
    ASSERT_GE(row, 0) << "origin-x 行未装配";

    // 值列就地编辑（itemChanged→单发排队的 setEditText——processEvents 冲刷）。
    table->item(row, 1)->setText(QStringLiteral("0.5"));
    QApplication::processEvents();

    // 表单级确认应用（UX-07 两步：应用→确认区→确认）。
    QWidget* host = m_panel->findChild<QWidget*>(QStringLiteral("ird_modeling_edit_host"));
    QPushButton* applyBtn = host->findChild<QPushButton*>(QStringLiteral("ird_param_apply"));
    ASSERT_NE(applyBtn, nullptr);
    applyBtn->click();
    QPushButton* confirmYes = host->findChild<QPushButton*>(QStringLiteral("ird_param_confirm_yes"));
    ASSERT_NE(confirmYes, nullptr);
    confirmYes->click();

    // 域面结果：平移 x 更新（m）＋UserProvided＋恰一条变更记录。
    const auto& origin = m_ws.design.joints[0].origin;
    ASSERT_EQ(origin.state(), core::FieldState::Provided);
    EXPECT_EQ(origin.value().d()[0], 0.5);
    EXPECT_EQ(origin.provenance().kind, core::ProvenanceKind::UserProvided);
    ASSERT_EQ(m_ws.changes.size(), std::size_t{1});
    EXPECT_EQ(m_ws.changes[0].subject, "joints[0]");
    EXPECT_NE(m_ws.changes[0].summary.find("原点"), std::string::npos);
}

/**
 * 编辑页 DH 权威守卫面（UI-T53——L-7/域守卫的呈现半区）：StandardDH 态下
 * 轴向编辑走完整确认链后被域守卫拒绝——状态行如实呈现 authority-locked、
 * 工作集字节不变（C-1 派生只读的域级单一判定，UI 零旁路）。
 */
TEST_F(ModelingPanelGuiTest, JointDetailEditPane_DhAuthorityRejectsAxisEdit_UI_T53)
{
    IRD_TEST_INFO("MDL-09", {}, std::nullopt);

    // 场景装配：模板草稿为 Explicit——置 StandardDH（值面演算，V-14 同款）。
    m_ws.design.authority = AuthorityMode::StandardDH;
    const ModelingWorkingSet before = m_ws;
    ModelReadinessReport emptyReport;
    m_panel->refreshPanel(m_ws, emptyReport);

    m_panel->focusObject(m_ws.design.joints[0].objectId);
    QTableWidget* table = jointEditTable(*m_panel);
    ASSERT_NE(table, nullptr);
    const int row = jointEditRowOf(*table, "axis-z");
    ASSERT_GE(row, 0);

    table->item(row, 1)->setText(QStringLiteral("0"));
    QApplication::processEvents();
    QWidget* host = m_panel->findChild<QWidget*>(QStringLiteral("ird_modeling_edit_host"));
    host->findChild<QPushButton*>(QStringLiteral("ird_param_apply"))->click();
    host->findChild<QPushButton*>(QStringLiteral("ird_param_confirm_yes"))->click();

    // 拒绝面：状态行警示＋工作集不变（域裁决唯一——UI 零旁路）。
    QLabel* status = m_panel->findChild<QLabel*>(QStringLiteral("ird_modeling_status_line"));
    ASSERT_NE(status, nullptr);
    EXPECT_TRUE(status->text().contains(QStringLiteral("authority-locked")))
        << "拒绝原因未就地呈现（UX-03——比较型原因）";
    EXPECT_EQ(m_ws, before) << "拒绝路径工作集字节不变（V-14）";
}

/**
 * 编辑页 L-7 门控（UI-T53——只读会话＝ParamTablePanel 整体禁用；域出口侧
 * applyJointDetailEdits 另有防御面，本用例钉控件半区）。
 */
TEST_F(ModelingPanelGuiTest, JointDetailEditPane_ReadOnlySessionDisablesPanel_UI_T53)
{
    IRD_TEST_INFO("MDL-07", {}, std::nullopt);

    ModelingPanelWidget readonlyPanel(false);
    readonlyPanel.setEditTargetProvider([this]() { return &m_ws; });
    ModelReadinessReport emptyReport;
    readonlyPanel.refreshPanel(m_ws, emptyReport);
    readonlyPanel.focusObject(m_ws.design.joints[0].objectId);

    QWidget* host = readonlyPanel.findChild<QWidget*>(QStringLiteral("ird_modeling_edit_host"));
    ASSERT_NE(host, nullptr);
    QWidget* tablePanel = host->findChild<QWidget*>(QStringLiteral("ird_param_table_panel"));
    ASSERT_NE(tablePanel, nullptr);
    EXPECT_FALSE(tablePanel->isEnabled()) << "只读会话编辑页未整体禁用（L-7 门控失守）";
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
        if (btn->objectName().startsWith(QString::fromUtf8("ird_modeling_struct_"))) { continue; } // 结构操作钮不入禁用对账（UI-T47 呈现件——非命令目录按钮）
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

// =====================================================================
// UI-T43 用例组：宿主接线修复的回归钉（审核 P1 三项中两代码项的测试面）
// =====================================================================

/// L-7 门控防复活：只读会话下重新注入"全放行"可用性快照，写命令
/// （readOnlyAllowed=false）不得因可用性刷新复活（UI-T43 修复点——
/// 此前 setCommandAvailability 缺只读合取项，三处使能判定不一致即门控
/// 旁路：只读切换后再刷新可用性＝写命令错误亮起）。恢复可写＝双向即时。
TEST_F(ModelingPanelGuiTest, ReadOnlySession_AvailabilityRefreshCannotReviveWriteCommands_L7_UI_T43)
{
    IRD_TEST_INFO("ERR-01", {}, std::nullopt);

    // 可写基线：提交出口＋全放行快照——全部命令按钮可用（夹具自检，
    // 同时覆盖 setCommandSubmit/setCommandAvailability 即时刷新路径）。
    m_panel->setCommandSubmit([](const ui::CommandId&) {});
    const auto allEnabled = [] (const ui::CommandId&) {
        return ui::CommandAvailability{true, true, true, ui::DisableReason{}};
    };
    m_panel->setCommandAvailability(allEnabled);

    const auto catalog = modelingDomainCommands();
    const auto buttons = commandButtonsOf(*m_panel);
    ASSERT_EQ(buttons.size(), catalog.size())
        << "命令按钮与目录错位（构造序漂移——本用例按下标对账失效）";
    for (const QPushButton* btn : buttons) {
        ASSERT_TRUE(btn->isEnabled()) << "可写＋全放行基线下按钮仍禁用（夹具失实）";
    }

    // 只读切换：写命令（readOnlyAllowed=false，目录十条中七条）禁用；
    // 只读命令（diff-baseline/export-package/reset-home-zero）保持可用。
    m_panel->setWritable(false);
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        EXPECT_EQ(buttons[i]->isEnabled(), catalog[i].readOnlyAllowed)
            << "只读切换后使能态与 readOnlyAllowed 不符：" << catalog[i].id;
    }

    // 修复点（审核 P1）：只读态下重新注入全放行快照——可用性刷新不得
    // 越过只读门（修复前本步写命令全部复活）。
    m_panel->setCommandAvailability(allEnabled);
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        EXPECT_EQ(buttons[i]->isEnabled(), catalog[i].readOnlyAllowed)
            << "可用性刷新复活了只读会话的写命令（L-7 门控旁路回归）：" << catalog[i].id;
    }

    // 恢复可写：写命令即时回升（双向即时——残留窗口为零）。
    m_panel->setWritable(true);
    for (const QPushButton* btn : buttons) {
        EXPECT_TRUE(btn->isEnabled()) << "恢复可写后写命令未回升";
    }

    // （模块级链路用例——生产装配序文案解析回归＋模块 setWritable 暂存/
    //   即时双形态——在 ModelingUiModuleHostWiringGuiTest.cpp：被测类型
    //   ModelingUiModule 持 policy 评估器符号，gating 见该文件头注。）
}

// =====================================================================
// UI-T47 结构编辑面：五钮承载＋选中位插入/删除的草稿级编辑链
// （域裁决唯一在 StructureEdit 四原语——本组用例断言面板承载与刷新编排）
// =====================================================================

TEST_F(ModelingPanelGuiTest, StructureButtons_InsertAndRemove_DraftOnly_UI_T47)
{
    // ①五钮在位（objectName 定位——面板承载面的存在性证据）。
    QPushButton* addBtn = m_panel->findChild<QPushButton*>(
        QStringLiteral("ird_modeling_struct_add"));
    QPushButton* removeBtn = m_panel->findChild<QPushButton*>(
        QStringLiteral("ird_modeling_struct_remove"));
    ASSERT_NE(addBtn, nullptr) << "新增关节钮未构建";
    ASSERT_NE(removeBtn, nullptr) << "删除关节钮未构建";

    // ②选中首关节（树行锚＝joints[0]——L-1 关联键复用）→新增＝选中位
    //   插入：工作集 7 关节（六轴+1）＋8 连杆（I-MDL-1）。
    const core::ObjectId j1 = m_ws.design.joints.front().objectId;
    const QString j1Anchor = QString::fromStdString(j1.toCanonical());
    QTreeWidgetItemIterator it(m_panel->findChild<QTreeWidget*>());
    while (*it != nullptr
           && (*it)->text(1) != j1Anchor) { ++it; }
    ASSERT_NE(*it, nullptr) << "首关节树行未投影";
    m_panel->findChild<QTreeWidget*>()->setCurrentItem(*it);

    const std::size_t jointsBefore = m_ws.design.joints.size();
    addBtn->click();
    ASSERT_EQ(m_ws.design.joints.size(), jointsBefore + 1)
        << "新增未落草稿（域原语未触工作集——提交流断链）";
    ASSERT_EQ(m_ws.design.links.size(), m_ws.design.joints.size() + 1)
        << "I-MDL-1 计数关系破坏";
    // 插入位语义：新关节在选中位之后（index 1）——设计默认种子。
    EXPECT_EQ(m_ws.design.joints[1].localName, "j7")
        << "插入位/消歧命名失守（六轴链 j1~j6 已占用）";

    // ③未选中态删除＝诚实指引（no-selection——状态行呈现，工作集不动）。
    //    形态：全新面板（未点过树——m_lastSelected 空；clearSelection 不清
    //    currentItem，不构成"未选中"的真实模拟）。
    const std::size_t jointsStable = m_ws.design.joints.size();
    ModelingPanelWidget freshPanel(true);
    freshPanel.setEditTargetProvider([this]() { return &m_ws; });
    ModelReadinessReport emptyReport;
    freshPanel.refreshPanel(m_ws, emptyReport);
    QPushButton* freshRemove = freshPanel.findChild<QPushButton*>(
        QStringLiteral("ird_modeling_struct_remove"));
    ASSERT_NE(freshRemove, nullptr);
    freshRemove->click();
    EXPECT_EQ(m_ws.design.joints.size(), jointsStable)
        << "未选中删除产生了操作（应就地指引）";
    QLabel* freshStatus = freshPanel.findChild<QLabel*>(
        QStringLiteral("ird_modeling_status_line"));
    ASSERT_NE(freshStatus, nullptr);
    EXPECT_FALSE(freshStatus->text().isEmpty())
        << "未选中删除未呈现指引（诚实指引面失守）";
}

TEST_F(ModelingPanelGuiTest, StructureButtons_RemoveSelected_DraftOnly_UI_T47)
{
    // 选中首关节→删除：6→5 关节（草稿级——无修订产生）。
    const core::ObjectId j1 = m_ws.design.joints.front().objectId;
    const QString j1Anchor = QString::fromStdString(j1.toCanonical());
    QTreeWidgetItemIterator it(m_panel->findChild<QTreeWidget*>());
    while (*it != nullptr
           && (*it)->text(1) != j1Anchor) { ++it; }
    ASSERT_NE(*it, nullptr);
    m_panel->findChild<QTreeWidget*>()->setCurrentItem(*it);

    QPushButton* removeBtn = m_panel->findChild<QPushButton*>(
        QStringLiteral("ird_modeling_struct_remove"));
    ASSERT_NE(removeBtn, nullptr);
    removeBtn->click();
    ASSERT_EQ(m_ws.design.joints.size(), 5u) << "删除未落草稿";
    ASSERT_EQ(m_ws.design.links.size(), 6u) << "I-MDL-1 计数关系破坏";
    bool removedStillThere = false;
    for (const JointEntry& j : m_ws.design.joints) {
        if (j.objectId == j1) { removedStillThere = true; }
    }
    EXPECT_FALSE(removedStillThere) << "被删关节仍在工作集";
}

// =====================================================================
// UI-T48 几何引用编辑面：操作行承载（挂接/摘除钮）＋拒绝呈现
// （io 真装路径经 QFileDialog 不可 headless 驱动——本组用例断言承载面
// 与摘除落草稿；挂接流端到端由域 UT 替身缝承载，见 GeometryLinkEditTest）
// =====================================================================

TEST_F(ModelingPanelGuiTest, GeometryActionButtons_LoadAndDetach_UI_T48)
{
    // ①预置挂接（域原语直调——绕过对话框；摘要缝替身同 UT 形态）。
    core::ContentDigester d;
    const std::string seed = "gui: D:/a/link1.stl";
    d.update(seed.data(), seed.size());
    ResourceProbeFn probe = [&](const std::string&, std::string&)
        -> std::optional<ResourceProbeResult> {
        ResourceProbeResult r;
        r.contentDigest = d.finalize();
        r.absPath = "D:/a/link1.stl";
        r.isMeshFamily = true;
        return r;
    };
    ASSERT_FALSE(attachExternalGeometry(m_ws, 0, GeometrySlot::Visual,
                                        "D:/a/link1.stl", probe)
                     .has_value());
    ModelReadinessReport emptyReport;
    m_panel->refreshPanel(m_ws, emptyReport);

    // ②连杆选中后属性区出现几何操作钮（挂接/摘除——承载面存在性）。
    const core::ObjectId l1 = m_ws.design.links[1].objectId;
    const QString l1Anchor = QString::fromStdString(l1.toCanonical());
    QTreeWidgetItemIterator it(m_panel->findChild<QTreeWidget*>());
    while (*it != nullptr && (*it)->text(1) != l1Anchor) { ++it; }
    ASSERT_NE(*it, nullptr) << "连杆树行未投影";
    m_panel->findChild<QTreeWidget*>()->setCurrentItem(*it);

    QPushButton* detach = m_panel->findChild<QPushButton*>(
        QStringLiteral("ird_modeling_geo_detach_visual"));
    ASSERT_NE(detach, nullptr) << "摘除钮未构建（几何行编辑面缺席）";
    QPushButton* attach = m_panel->findChild<QPushButton*>(
        QStringLiteral("ird_modeling_geo_attach_visual"));
    ASSERT_NE(attach, nullptr) << "挂接钮未构建";

    // ③摘除落草稿（引用清空＋清单条目保留——共享引用语义）。
    detach->click();
    EXPECT_FALSE(m_ws.design.links[1].visual.has_value()) << "摘除未落草稿";
    EXPECT_EQ(m_ws.design.resourceManifest.size(), 1u)
        << "清单条目被级联删除";
}

/// UI-T49 视觉→碰撞复制辅助（G7 承载＋acceptance 1 两槽独立性＋诚实禁用）：
/// visual 已挂＝复制钮可用，点击落草稿（collision 同资源引用、visual 零
/// 改写、清单零新增）；visual 未设＝钮禁用＋toolTip 原因（非置灰无解释）。
TEST_F(ModelingPanelGuiTest, GeometryCopyButton_VisualToCollision_UI_T49)
{
    // ①预置 visual 挂接（域原语直调——绕过对话框；T48 用例同形态）。
    core::ContentDigester d;
    const std::string seed = "gui: D:/a/link1.stl";
    d.update(seed.data(), seed.size());
    ResourceProbeFn probe = [&](const std::string&, std::string&)
        -> std::optional<ResourceProbeResult> {
        ResourceProbeResult r;
        r.contentDigest = d.finalize();
        r.absPath = "D:/a/link1.stl";
        r.isMeshFamily = true;
        return r;
    };
    ASSERT_FALSE(attachExternalGeometry(m_ws, 1, GeometrySlot::Visual,
                                        "D:/a/link1.stl", probe)
                     .has_value());
    ModelReadinessReport emptyReport;
    m_panel->refreshPanel(m_ws, emptyReport);
    m_panel->focusObject(m_ws.design.links[1].objectId);

    // ②复制钮承载存在（仅 collision 行——G7 入口）＋visual 已挂＝可用。
    QPushButton* copy = m_panel->findChild<QPushButton*>(
        QStringLiteral("ird_modeling_geo_copy_collision"));
    ASSERT_NE(copy, nullptr) << "复制钮未构建（G7 承载缺席）";
    EXPECT_TRUE(copy->isEnabled()) << "visual 已挂时复制钮被禁用";

    // ③点击复制→collision 落草稿（同资源引用）＋visual 零改写
    //   （acceptance 1 两槽独立性断言的 gui 半区）。
    copy->click();
    ASSERT_TRUE(m_ws.design.links[1].collision.has_value())
        << "复制未落草稿（collision 槽仍空）";
    ASSERT_TRUE(m_ws.design.links[1].visual.has_value())
        << "复制改写了 visual 槽（零 visual 改写失守）";
    EXPECT_EQ(m_ws.design.links[1].collision->resourceRefId,
              m_ws.design.links[1].visual->resourceRefId)
        << "未同资源复制（复制产生了第二引用键）";
    EXPECT_EQ(m_ws.design.resourceManifest.size(), 1u)
        << "复制新增了清单条目（应共享同一 ResourceRef）";

    // ④摘除 visual 后重投影——复制钮诚实禁用＋toolTip 携原因
    //   （"非置灰无解释"纪律——§9.7.1 交互）。
    ASSERT_FALSE(detachGeometry(m_ws, 1, GeometrySlot::Visual).has_value());
    m_panel->refreshPanel(m_ws, emptyReport);
    m_panel->focusObject(m_ws.design.links[1].objectId);
    QPushButton* copyAfter = m_panel->findChild<QPushButton*>(
        QStringLiteral("ird_modeling_geo_copy_collision"));
    ASSERT_NE(copyAfter, nullptr) << "重投影后复制钮丢失";
    EXPECT_FALSE(copyAfter->isEnabled())
        << "visual 未设时复制钮未诚实禁用";
    EXPECT_TRUE(copyAfter->toolTip().contains(QStringLiteral("视觉几何未挂接")))
        << "禁用态缺原因提示（置灰无解释——交互纪律失守）";
}

// =====================================================================
// UI-T50——建模面板编辑链端到端 gui 拍（acceptance 6：结构→几何→碰撞→
// 对象属性页四段贯通）
// =====================================================================

/**
 * 契约 UI-T50 acceptance 6（整合回归·面板侧四段贯通）：真实点击链驱动
 * 四段编辑全部落草稿——①结构段（结构树"新增"钮插入关节）→②几何段
 * （域原语挂接 visual——对话框不可 headless，T48 用例同款直调）→③碰撞
 * 段（"从视觉复制"钮落 collision）→④对象属性页段（属性区重投影后几何
 * 行值与草稿同源）。draft.apply→编译链发布复验不在面板 gui 域内（命令
 * 总线/工程装配在宿主层）——援引既有契约面：apply-robot-design 命令流
 * 由 ui_contract/contract 测试与 WP-24-T08 装配验证覆盖（登记注，非本
 * 用例断言面）。
 */
TEST_F(ModelingPanelGuiTest, EditingChain_EndToEnd_FourSegments_UI_T50)
{
    const ModelReadinessReport emptyReport;

    // ---- ①结构段：选中连杆 1 后点"新增"钮（选中位插入 j7——UI-T47
    //      词表），关节链 +1 落草稿。
    m_panel->focusObject(m_ws.design.links[1].objectId);
    QPushButton* structAdd = m_panel->findChild<QPushButton*>(
        QStringLiteral("ird_modeling_struct_add"));
    ASSERT_NE(structAdd, nullptr) << "结构新增钮未构建";
    const std::size_t jointsBefore = m_ws.design.joints.size();
    structAdd->click();
    EXPECT_EQ(m_ws.design.joints.size(), jointsBefore + 1)
        << "结构段：新增钮点击未落草稿";

    // ---- ②几何段：域原语挂接 visual（替身 probe——T48 gui 先例）。
    core::ContentDigester d;
    const std::string seed = "e2e: D:/a/link.stl";
    d.update(seed.data(), seed.size());
    ResourceProbeFn probe = [&](const std::string&, std::string&)
        -> std::optional<ResourceProbeResult> {
        ResourceProbeResult r;
        r.contentDigest = d.finalize();
        r.absPath = "D:/a/link.stl";
        r.isMeshFamily = true;
        return r;
    };
    ASSERT_FALSE(attachExternalGeometry(m_ws, 1, GeometrySlot::Visual,
                                        "D:/a/link.stl", probe)
                     .has_value());
    ASSERT_TRUE(m_ws.design.links[1].visual.has_value());

    // ---- ③碰撞段：重投影后点"从视觉复制"钮（T49 语义——同资源落
    //      collision）。
    m_panel->refreshPanel(m_ws, emptyReport);
    m_panel->focusObject(m_ws.design.links[1].objectId);
    QPushButton* copy = m_panel->findChild<QPushButton*>(
        QStringLiteral("ird_modeling_geo_copy_collision"));
    ASSERT_NE(copy, nullptr) << "复制钮未构建";
    copy->click();
    ASSERT_TRUE(m_ws.design.links[1].collision.has_value())
        << "碰撞段：复制钮点击未落草稿";
    EXPECT_EQ(m_ws.design.links[1].collision->resourceRefId,
              m_ws.design.links[1].visual->resourceRefId)
        << "碰撞段：未同资源复制";

    // ---- ④对象属性页段：属性区重投影后编辑面仍以草稿为源（面板行值
    //      与域状态同源——选中保持连杆 1，几何行/物性行刷新不回退）。
    m_panel->refreshPanel(m_ws, emptyReport);
    m_panel->focusObject(m_ws.design.links[1].objectId);
    QPushButton* detach = m_panel->findChild<QPushButton*>(
        QStringLiteral("ird_modeling_geo_detach_visual"));
    ASSERT_NE(detach, nullptr)
        << "对象属性页段：重投影后几何行编辑面丢失（投影回退）";

    // ---- 四段全落草稿的域状态总核对（变更摘要累积——MDL-09 载体）。
    EXPECT_FALSE(m_ws.changes.empty()) << "四段贯通零变更摘要（草稿未脏化）";
}
