/**
 * @file   ParamTablePanelGuiTest.cpp
 * @brief  UI-T08 GUI 层用例（QApplication＋真实 Widget 树——§12.1 第三层
 *         分工）：参数表面板的交互行为面——四列结构与数值＋单位同显、
 *         值列就地编辑的就地错误与保留原值、非模态确认区应用流（UX-07）、
 *         取消恢复、剪贴板批量粘贴与影响明细（UX-05）、未接出口禁用、
 *         开发诊断开关不进编辑集、单位切换仅动显示（KIN-12）、面板零
 *         对话框控件。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T08.json：acceptance 1（公共编辑规则
 *     全量＋"不用模态对话框做大量重复编辑"）/ acceptance 2（O-31 处置
 *     ——面板只把确认过的修改集经 ui 自有 IFormEditOutlet 移交替身，
 *     对 project 等对端零依赖，include 面由 BuildRedLineTest 常驻扫描）；
 *   - units/ui.md §13 UI-T08 行、§16.7 v1.0（本文件登记行）；
 *   - 分层理由：编辑规则在模型层（FormEditModelTest）逐条断言；本文件
 *     断言真实控件树的行为面（点击流/剪贴板/就地刷新/开关回调），并以
 *     "值列文本与模型 displayNumberText 同源"互证（UI-POL-1 同款口径）。
 */

#include <gtest/gtest.h>

#include <QAbstractButton>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QWidget>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include <sdurws/ird/ui/FormEditCommon.hpp>

#include <memory>
#include <string>
#include <vector>

namespace {

using namespace sdurws::ird;
using sdurws::ird::ui::IFormEditOutlet;
using sdurws::ird::ui::ParamEditModel;
using sdurws::ird::ui::ParamEditSet;
using sdurws::ird::ui::QuantityFieldSpec;
using sdurws::ird::ui::kApplyHandedOffPrefix;
using sdurws::ird::ui::kCancelRestoredText;
using sdurws::ird::ui::kConfirmPromptPrefix;
using sdurws::ird::ui::kFieldUnsetText;
using sdurws::ird::ui::kNoOutletTooltip;
using sdurws::ird::ui::makeQuantityFieldSpec;

// =====================================================================
// 测试替身与夹具
// =====================================================================

/// 编辑出口替身：记录移交（真实现＝域编辑器，经 IDraftController.
/// attachModule 接入——阶段 B 域消费者；本套件只验证面板侧移交行为）。
class RecordingOutlet final : public ui::IFormEditOutlet {
public:
    void applyEdits(const ParamEditSet& editSet) override { m_sets.push_back(editSet); }
    const std::vector<ParamEditSet>& sets() const { return m_sets; }

private:
    std::vector<ParamEditSet> m_sets;
};

/// 夹具字段（三行小表：长度 m→mm、角度 rad→deg、无量纲整数计数——
/// 行序即注册序，测试按下标定位）。
std::vector<QuantityFieldSpec> fixtureFields()
{
    std::vector<QuantityFieldSpec> fields;
    fields.push_back(makeQuantityFieldSpec(
        "joint-clearance", "关节间隙", core::QuantityKind::Length,
        *core::UnitToken::find("m"), *core::UnitToken::find("mm")));
    fields.push_back(makeQuantityFieldSpec(
        "travel-limit", "行程上限", core::QuantityKind::Angle,
        *core::UnitToken::find("rad"), *core::UnitToken::find("deg")));
    fields.push_back(makeQuantityFieldSpec(
        "solver-max-iterations", "求解器最大迭代次数",
        core::QuantityKind::Dimensionless, *core::UnitToken::find("1"),
        *core::UnitToken::find("1"), std::nullopt, true));
    return fields;
}

class ParamTablePanelGuiTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_model = std::make_unique<ParamEditModel>(fixtureFields());
        m_model->setBaseline("joint-clearance", 0.3);
        m_model->setBaseline("travel-limit", 1.5707963267948966);
        m_model->setBaseline("solver-max-iterations", 100.0);
        m_outlet = std::make_unique<RecordingOutlet>();
    }

    /// 面板构建（默认接替身出口；outlet=nullptr 用例直接传空）。
    QWidget* makePanel(bool withOutlet, const ui::ParamTablePanelOptions& options = {})
    {
        m_panel = std::unique_ptr<QWidget>(ui::createParamTablePanel(
            *m_model, withOutlet ? m_outlet.get() : nullptr, options));
        m_panel->show();
        pump();
        return m_panel.get();
    }

    /// 用例事件泵（singleShot(0) 的就地刷新在此落定——事件驱动判据）。
    static void pump() { QApplication::processEvents(); }

    static QWidget* child(QWidget* panel, const char* name)
    {
        return panel->findChild<QWidget*>(QString::fromLatin1(name));
    }
    static QAbstractButton* button(QWidget* panel, const char* name)
    {
        return panel->findChild<QAbstractButton*>(QString::fromLatin1(name));
    }
    QTableWidget* table() const
    {
        return m_panel->findChild<QTableWidget*>(QString::fromLatin1("ird_param_table"));
    }
    QLabel* impact() const
    {
        return m_panel->findChild<QLabel*>(QString::fromLatin1("ird_param_impact"));
    }
    QLabel* confirmText() const
    {
        return m_panel->findChild<QLabel*>(QString::fromLatin1("ird_param_confirm_text"));
    }
    QWidget* confirmRegion() const
    {
        return m_panel->findChild<QWidget*>(QString::fromLatin1("ird_param_confirm_region"));
    }

    /// 值列就地编辑模拟（程序 setText 触发 itemChanged——与用户编辑同
    /// 一信号路径）；随后 pump 等待排队刷新落定。
    void editValue(int row, const QString& text)
    {
        table()->item(row, 1)->setText(text);
        pump();
    }

    std::unique_ptr<ParamEditModel> m_model;
    std::unique_ptr<RecordingOutlet> m_outlet;
    std::unique_ptr<QWidget> m_panel;
};

// =====================================================================
// ①：四列结构与数值＋单位同显（UX-05/KIN-12——呈现面）
// =====================================================================

/**
 * 面板结构与同显：命名锚齐备；表＝参数｜值｜单位｜状态四列；每行值列
 * 文本与模型 displayNumberText 同源、单位列为当前显示制式符号；未设
 * 值显示"未设"占位（不伪造 0）。
 */
TEST_F(ParamTablePanelGuiTest, StructureAndValueUnitColumns_UI_T08_ACC1)
{
    IRD_TEST_INFO("UX-05", {"KIN-12"}, std::nullopt);
    QWidget* panel = makePanel(true);
    // 根容器 objectName（面板自身——findChild 只搜子树，根用名字断言）。
    EXPECT_EQ(m_panel->objectName(), QString::fromLatin1("ird_param_table_panel"));
    EXPECT_NE(child(panel, "ird_param_filter"), nullptr);
    EXPECT_NE(child(panel, "ird_param_unit_switch"), nullptr);
    EXPECT_NE(child(panel, "ird_param_impact"), nullptr);
    EXPECT_NE(child(panel, "ird_param_apply"), nullptr);
    EXPECT_NE(child(panel, "ird_param_cancel"), nullptr);
    EXPECT_NE(child(panel, "ird_param_paste"), nullptr);
    EXPECT_NE(confirmRegion(), nullptr);

    QTableWidget* t = table();
    ASSERT_NE(t, nullptr);
    ASSERT_EQ(t->columnCount(), 4);
    EXPECT_EQ(t->horizontalHeaderItem(0)->text(), QString::fromUtf8("参数"));
    EXPECT_EQ(t->horizontalHeaderItem(1)->text(), QString::fromUtf8("值"));
    EXPECT_EQ(t->horizontalHeaderItem(2)->text(), QString::fromUtf8("单位"));
    EXPECT_EQ(t->horizontalHeaderItem(3)->text(), QString::fromUtf8("状态"));
    ASSERT_EQ(t->rowCount(), 3);
    // 同源互证：值列文本＝displayNumberText；单位列＝当前制式符号。
    EXPECT_EQ(t->item(0, 1)->text(), QString::fromStdString(m_model->displayNumberText("joint-clearance")));
    EXPECT_EQ(t->item(0, 1)->text(), QString::fromUtf8("300"));
    EXPECT_EQ(t->item(0, 2)->text(), QString::fromUtf8("mm"));
    // 行→键锚在位（UserRole——错误定位/回调的定位机制）。
    EXPECT_EQ(t->item(0, 0)->data(Qt::UserRole).toString().toStdString(), "joint-clearance");
}

// =====================================================================
// ②：非法输入就地显示原因保留原值（UX-05——就地编辑路径）
// =====================================================================

/**
 * 值列输入非法文本：状态列就地显示原因（含原文与"保留原值"）、值列
 * 文本立即回到最后一次有效值、编辑出口零触达；修复输入后状态列清空。
 */
TEST_F(ParamTablePanelGuiTest, InPlaceErrorKeepsValue_UI_T08_ACC1)
{
    IRD_TEST_INFO("UX-05", {}, std::nullopt);
    makePanel(true);
    QTableWidget* t = table();

    editValue(0, "abc");
    EXPECT_EQ(t->item(0, 3)->text().toStdString(), m_model->statusText("joint-clearance"));
    EXPECT_NE(t->item(0, 3)->text().toStdString().find("不是有效数值"), std::string::npos)
        << "状态列就地原因缺失: " << t->item(0, 3)->text().toStdString();
    EXPECT_NE(t->item(0, 3)->text().toStdString().find("保留原值"), std::string::npos);
    // 保留原值：值列回显最后一次有效值（300 mm 的数值半区）。
    EXPECT_EQ(t->item(0, 1)->text(), QString::fromUtf8("300"));
    EXPECT_EQ(m_outlet->sets().size(), 0u) << "非法输入不得触达编辑出口";

    // 修复：合法输入清状态、值列更新为新值的显示投影。
    editValue(0, "250");
    EXPECT_TRUE(t->item(0, 3)->text().isEmpty());
    EXPECT_EQ(t->item(0, 1)->text(), QString::fromUtf8("250"));
    EXPECT_DOUBLE_EQ(*m_model->currentValueSi("joint-clearance"), 0.25);
}

// =====================================================================
// ③：表单级确认应用（UX-07——应用前确认交互，非模态；断言与放行归
// project）
// =====================================================================

/**
 * 应用流：编辑→点"应用…"→内嵌确认区就地呈现（比较型明细，零对话框）
 * →点"确认应用"→修改集移交编辑出口、反馈明示"断言与放行归 project"。
 */
TEST_F(ParamTablePanelGuiTest, ApplyViaInlineConfirmRegion_UX07_UI_T08_ACC1)
{
    IRD_TEST_INFO("UX-07", {}, std::nullopt);
    makePanel(true);
    editValue(0, "250");

    // 第一步：应用→确认区就地可见（非模态——同一时刻面板无任何对话框）。
    button(m_panel.get(), "ird_param_apply")->click();
    pump();
    ASSERT_NE(confirmRegion(), nullptr);
    EXPECT_TRUE(confirmRegion()->isVisible()) << "确认区应就地呈现（非模态）";
    const QString confirm = confirmText()->text();
    EXPECT_TRUE(confirm.startsWith(QString::fromUtf8(kConfirmPromptPrefix)));
    EXPECT_TRUE(confirm.contains(QString::fromUtf8("250 mm"))) << "确认明细应含新值: " << confirm.toStdString();
    EXPECT_TRUE(confirm.contains(QString::fromUtf8("300 mm"))) << "确认明细应含旧值: " << confirm.toStdString();
    EXPECT_EQ(m_outlet->sets().size(), 0u) << "确认前不得移交";

    // 行为面补证：面板子树零对话框控件（与模型层静态扫描互证）。
    EXPECT_EQ(m_panel->findChildren<QDialog*>().size(), 0) << "UX-05 非模态红线：面板不得含对话框";

    // 第二步：确认→移交出口一次；反馈带前缀与边界说明。
    button(m_panel.get(), "ird_param_confirm_yes")->click();
    pump();
    EXPECT_FALSE(confirmRegion()->isVisible());
    ASSERT_EQ(m_outlet->sets().size(), 1u);
    ASSERT_EQ(m_outlet->sets()[0].changes.size(), 1u);
    EXPECT_DOUBLE_EQ(m_outlet->sets()[0].changes[0].newSi, 0.25);
    EXPECT_TRUE(impact()->text().startsWith(QString::fromUtf8(kApplyHandedOffPrefix)));
    EXPECT_TRUE(impact()->text().contains(QString::fromUtf8("project 命令边界")))
        << "反馈应明示断言与放行归 project: " << impact()->text().toStdString();
}

/**
 * "再改改"：确认区可反悔——隐藏且零移交（确认前修改集仍是暂存态）。
 */
TEST_F(ParamTablePanelGuiTest, ConfirmRegionDeclineKeepsStaged_UX07_UI_T08_ACC1)
{
    IRD_TEST_INFO("UX-07", {}, std::nullopt);
    makePanel(true);
    editValue(0, "250");
    button(m_panel.get(), "ird_param_apply")->click();
    pump();
    ASSERT_TRUE(confirmRegion()->isVisible());

    button(m_panel.get(), "ird_param_confirm_no")->click();
    pump();
    EXPECT_FALSE(confirmRegion()->isVisible());
    EXPECT_EQ(m_outlet->sets().size(), 0u) << "未确认不得移交";
    EXPECT_TRUE(m_model->isDirty("joint-clearance")) << "暂存修改应保留供继续编辑";
}

// =====================================================================
// ④：取消恢复（UX-05/DTB 验收列）
// =====================================================================

/**
 * 点"取消恢复"：暂存丢弃、值列回基线显示、出口零触达、反馈为契约文案。
 */
TEST_F(ParamTablePanelGuiTest, CancelRestore_UI_T08_ACC1)
{
    IRD_TEST_INFO("UX-05", {}, std::nullopt);
    makePanel(true);
    editValue(0, "250");
    ASSERT_TRUE(m_model->isDirty("joint-clearance"));

    button(m_panel.get(), "ird_param_cancel")->click();
    pump();
    EXPECT_FALSE(m_model->isDirty("joint-clearance"));
    EXPECT_EQ(table()->item(0, 1)->text(), QString::fromUtf8("300")) << "值列应回基线显示";
    EXPECT_EQ(m_outlet->sets().size(), 0u);
    EXPECT_EQ(impact()->text(), QString::fromUtf8(kCancelRestoredText));
}

// =====================================================================
// ⑤：批量粘贴与影响明细（UX-05——非模态批量入口）
// =====================================================================

/**
 * 剪贴板批量粘贴：合法行暂存、未知键行拒绝；影响明细就地呈现摘要＋
 * 逐行拒绝原因＋影响条目；出口零触达（粘贴只是暂存，应用仍走确认）。
 */
TEST_F(ParamTablePanelGuiTest, BatchPasteImpactDetail_UI_T08_ACC1)
{
    IRD_TEST_INFO("UX-05", {}, std::nullopt);
    makePanel(true);
    QGuiApplication::clipboard()->setText(
        QString::fromUtf8("solver-max-iterations\t150\nno-such-key\t1\n"));

    button(m_panel.get(), "ird_param_paste")->click();
    pump();

    const QString detail = impact()->text();
    EXPECT_TRUE(detail.contains(QString::fromUtf8("粘贴 2 行：接受 1 项、拒绝 1 项")))
        << "影响明细摘要缺失: " << detail.toStdString();
    EXPECT_TRUE(detail.contains(QString::fromUtf8("未知参数：no-such-key")))
        << "拒绝原因应就地呈现: " << detail.toStdString();
    EXPECT_TRUE(detail.contains(QString::fromUtf8("影响"))) << "影响条目应呈现";
    // 暂存已生效（值列显示 150）；出口零触达。
    EXPECT_EQ(table()->item(2, 1)->text(), QString::fromUtf8("150"));
    EXPECT_EQ(m_outlet->sets().size(), 0u);
}

// =====================================================================
// ⑥：未接编辑出口＝应用禁用（§11.4 不虚构可用性）
// =====================================================================

/**
 * outlet=nullptr 面板："应用…"禁用且 tooltip＝契约文案（域编辑器未
 * 装配时不虚构可用性）；其余就地功能不受影响。
 */
TEST_F(ParamTablePanelGuiTest, NoOutletDisablesApply_UI_T08_ACC2)
{
    IRD_TEST_INFO("UX-04", {"UX-07"}, std::nullopt);
    makePanel(false);
    auto* apply = m_panel->findChild<QPushButton*>(QString::fromLatin1("ird_param_apply"));
    ASSERT_NE(apply, nullptr);
    EXPECT_FALSE(apply->isEnabled());
    EXPECT_EQ(apply->toolTip(), QString::fromUtf8(kNoOutletTooltip));
    // 就地编辑照常可用（编辑与移交是两个阶段——出口缺席只挡移交）。
    editValue(0, "250");
    EXPECT_TRUE(m_model->isDirty("joint-clearance"));
    EXPECT_EQ(m_outlet->sets().size(), 0u);
}

// =====================================================================
// ⑦：开发诊断开关不进编辑集（UX-04 承载＋UX-07 后半句）
// =====================================================================

/**
 * "开发诊断"开关（会话显示设置）：勾选只触发注入回调——不产生脏标记、
 * 不产生待应用修改、不触达编辑出口（"仅改变会话显示的操作不弹出保存
 * 或冻结提示"的结构保证）。
 */
TEST_F(ParamTablePanelGuiTest, DevDiagnosticsToggleNeverEntersEditSet_UX04_UX07_UI_T08_ACC1)
{
    IRD_TEST_INFO("UX-04", {"UX-07"}, std::nullopt);
    ui::ParamTablePanelOptions options;
    options.showDevDiagnosticsToggle = true;
    bool reported = false;
    bool reportedState = false;
    options.onDevDiagnosticsToggled = [&reported, &reportedState](bool checked) {
        reported = true;
        reportedState = checked;
    };
    makePanel(true, options);

    auto* devDiag = m_panel->findChild<QAbstractButton*>(QString::fromLatin1("ird_param_dev_diag"));
    ASSERT_NE(devDiag, nullptr) << "高级面板应承载开发诊断开关（UX-04）";
    devDiag->click();
    pump();
    EXPECT_TRUE(reported);
    EXPECT_TRUE(reportedState);
    // 会话显示开关与编辑正交：零脏、零待应用、零移交。
    EXPECT_TRUE(m_model->dirtyKeys().empty());
    EXPECT_TRUE(m_model->pendingChanges().empty());
    EXPECT_EQ(m_outlet->sets().size(), 0u);
    // 开关不进确认流：点应用后确认区只含真实编辑（本用例无编辑→不出现）。
    button(m_panel.get(), "ird_param_apply")->click();
    pump();
    EXPECT_FALSE(confirmRegion()->isVisible()) << "纯显示操作不得触发确认交互";
}

// =====================================================================
// ⑧：单位切换仅影响显示（KIN-12——切换控件的 GUI 面）
// =====================================================================

/**
 * 单位切换下拉：候选＝长度（m/cm/mm）＋角度（rad/deg）两组；初始选中
 * ＝当前制式（长度 mm）；用户切换后单位列与值列随动、SI 真值不变、
 * 零脏零移交（KIN-12"切换不产生修订"）。
 */
TEST_F(ParamTablePanelGuiTest, UnitSwitchIsDisplayOnly_KIN12_UI_T08_ACC1)
{
    IRD_TEST_INFO("UX-05", {"KIN-12"}, std::nullopt);
    makePanel(true);
    auto* combo = m_panel->findChild<QComboBox*>(QString::fromLatin1("ird_param_unit_switch"));
    ASSERT_NE(combo, nullptr);
    // 候选表：长度组 3 项＋角度组 2 项（模型中两类量纲字段齐备）。
    ASSERT_EQ(combo->count(), 5);
    EXPECT_EQ(combo->itemText(0), QString::fromUtf8("长度：m"));
    EXPECT_EQ(combo->itemText(2), QString::fromUtf8("长度：mm"));
    EXPECT_EQ(combo->itemText(3), QString::fromUtf8("角度：rad"));
    EXPECT_EQ(combo->itemText(4), QString::fromUtf8("角度：deg"));
    // 初始选中＝长度当前制式 mm（下标 2）。
    EXPECT_EQ(combo->currentIndex(), 2);
    // 模拟用户选择"长度：cm"（activated 仅用户交互发射——直接调用信号
    // 函数即用户路径〔QT_NO_KEYWORDS 面：不用 emit 关键字，信号调用
    // 语义不变〕；程序 setCurrentIndex 不会误触发，装配回显安全）。
    combo->activated(1);
    pump();
    EXPECT_EQ(table()->item(0, 2)->text(), QString::fromUtf8("cm"));
    EXPECT_EQ(table()->item(0, 1)->text(), QString::fromUtf8("30")) << "值列应随显示制式投影";
    EXPECT_DOUBLE_EQ(*m_model->currentValueSi("joint-clearance"), 0.3) << "SI 真值不变（KIN-12）";
    EXPECT_FALSE(m_model->isDirty("joint-clearance")) << "显示切换不产生脏标记";
    EXPECT_TRUE(m_model->pendingChanges().empty());
    EXPECT_EQ(m_outlet->sets().size(), 0u) << "显示切换不触发移交";
}

// =====================================================================
// ⑨：筛选（UX-05——可见性裁剪的 GUI 面）
// =====================================================================

/**
 * 筛选输入：命中键子串→表只余命中行（行序稳定）；清空→全量恢复。
 */
TEST_F(ParamTablePanelGuiTest, FilterRowsByKeySubstring_UI_T08_ACC1)
{
    IRD_TEST_INFO("UX-05", {}, std::nullopt);
    makePanel(true);
    auto* filter = m_panel->findChild<QLineEdit*>(QString::fromLatin1("ird_param_filter"));
    ASSERT_NE(filter, nullptr);
    filter->setText(QString::fromUtf8("solver"));
    pump();
    ASSERT_EQ(table()->rowCount(), 1);
    EXPECT_EQ(table()->item(0, 0)->data(Qt::UserRole).toString().toStdString(),
              "solver-max-iterations");
    filter->clear();
    pump();
    EXPECT_EQ(table()->rowCount(), 3) << "清空筛选应恢复全量行";
}

}  // namespace
