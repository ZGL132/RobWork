/**
 * @file JointGroupedFormGuiTest.cpp
 * @brief GF-A真实Windows控件验收：输入暂存、整表拒绝、单位/类型、离页及旧页保护。
 * 依据MDL-01/07/09/12、UX-05/07/09、AT-01/17。每例创建真实模板和窗口，
 * 数值校验/候选采纳调用真实领域；无计算替身。控件归Qt父子树，仅GUI线程。
 */
#include <gtest/gtest.h>
#include <QApplication>
#include <QComboBox>
#include <QClipboard>
#include <QDir>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QTableWidget>
#include <QToolButton>
#include "plugin/JointGroupedForm.hpp"
#include "plugin/ModelingPanelWidget.hpp"
#include "plugin/PanelRefresh.hpp"
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
using namespace sdurws::ird;
using namespace sdurws::ird::modeling;
namespace {
/** @brief 模板初始值来自真实工厂；成功是夹具前置，失败由get抛出。 */
ModelingWorkingSet draft()
{
    std::vector<core::DiagnosticRecord> diagnostics;
    return RobotDesignTemplateFactory{}.createDraft(TemplateId{kTemplateIdGeneric6R},
        runtime::InstallationPresetToken::Ground,"gf-test",diagnostics).get();
}
/** @brief 向真实QLineEdit发送键盘输入，覆盖textEdited路径；物理单位取当前标签。 */
void enter(QLineEdit* edit,const QString& text)
{
    ASSERT_NE(edit,nullptr);ASSERT_TRUE(edit->isEnabled());
    edit->setFocus();edit->selectAll();
    QKeyEvent event(QEvent::KeyPress,Qt::Key_unknown,Qt::NoModifier,text);
    QApplication::sendEvent(edit,&event);QApplication::processEvents();
    ASSERT_EQ(edit->text(),text);
}
/** @brief 整表两步确认；仅当前页的按钮可被定位，不触及隐藏的通用表提交。 */
void confirm(QWidget& form)
{
    auto* apply=form.findChild<QPushButton*>("ird_param_apply");ASSERT_NE(apply,nullptr);
    apply->click();QApplication::processEvents();
    auto* yes=form.findChild<QPushButton*>("ird_param_confirm_yes");ASSERT_NE(yes,nullptr);
    yes->click();QApplication::processEvents();
}
/** @brief 每例独立真实窗口与真实工作集；submit只计数领域接受而非替换领域算法。 */
class JointGroupedFormGuiTest:public ::testing::Test {
protected:
    ModelingWorkingSet ws;std::unique_ptr<JointGroupedForm> form;int accepted=0;
    void SetUp() override
    {
        ws=draft();
        form=std::make_unique<JointGroupedForm>(ws.design.joints[0],ws.design.authority,
            [this](const JointEntry& before,JointEntry& after){
                auto e=applyJointFormEdit(ws,before,after);
                if(!e){++accepted;after=ws.design.joints[0];}return e;
            },nullptr);
        form->resize(760,760);form->show();QApplication::processEvents();
    }
    void TearDown() override {form.reset();QApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);}
    QLineEdit* field(const char* key){return form->findChild<QLineEdit*>(QString("ird_joint_field_")+key);}
    void shot(const QString& name)
    {
        const auto out=qEnvironmentVariable("IRD_MODELING_GUI_EVIDENCE_OUT");
        if(!out.isEmpty()){QDir().mkpath(out);ASSERT_TRUE(form->grab().save(out+"/gf-"+name+".png"));}
    }
};
/** @brief GF-01/14：四分组、14字段和常驻操作，宽/窄两侧均真实布局可达。 */
TEST_F(JointGroupedFormGuiTest, GroupLayoutAndNarrowViewport_UX09_T30)
{
    IRD_TEST_INFO("UX-09",{"AT-01"},std::nullopt);
    for(const char* key:{"basic","axis","origin","limits"}){
        auto* group=form->findChild<QWidget*>(QString("ird_joint_group_")+key);ASSERT_NE(group,nullptr);EXPECT_TRUE(group->isVisible());}
    EXPECT_EQ(form->findChildren<QLineEdit*>(QRegularExpression("ird_joint_field_.*")).size(),14);
    auto* scroll=form->findChild<QScrollArea*>("ird_joint_form_scroll");ASSERT_NE(scroll,nullptr);
    auto* apply=form->findChild<QPushButton*>("ird_param_apply");ASSERT_NE(apply,nullptr);
    shot("wide");
    for(int width:{480,320,760}){form->resize(width,760);QApplication::processEvents();
        EXPECT_TRUE(form->rect().contains(apply->geometry()));EXPECT_TRUE(apply->isVisible());
        EXPECT_LE(scroll->geometry().bottom(),apply->geometry().top());}
    form->resize(320,760);QApplication::processEvents();shot("narrow");
    EXPECT_LE(scroll->widget()->width(),scroll->viewport()->width())
        << "320像素属性窗应重排为单列，不能把数值与单位推到右侧视区外";
    std::cout<<"[gf-layout] body min="<<scroll->widget()->minimumWidth()<<" hint="<<scroll->widget()->minimumSizeHint().width()
             <<" layoutMin="<<scroll->widget()->layout()->minimumSize().width()<<std::endl;
    for(auto* child:scroll->widget()->findChildren<QWidget*>()){
        if(child->isVisible()&&(child->minimumSizeHint().width()>320||child->minimumWidth()>320||child->width()>320)){
            std::cout<<"[gf-layout] "<<child->metaObject()->className()<<" "<<child->objectName().toStdString()
                     <<" width="<<child->width()<<" min="<<child->minimumWidth()<<" minHint="<<child->minimumSizeHint().width()<<std::endl;}}
}
/** @brief GF-A删除重复编辑视图：完整字段只在分组主表出现，不保留高级参数表。 */
TEST_F(JointGroupedFormGuiTest, CompleteGroupedFormHasNoDuplicateAdvancedTable_UX05_T30)
{
    IRD_TEST_INFO("UX-05",{"AT-01"},std::nullopt);
    EXPECT_EQ(form->findChild<QWidget*>("ird_joint_advanced_toggle"),nullptr);
    EXPECT_EQ(form->findChild<QTableWidget*>("ird_param_table"),nullptr);
    EXPECT_EQ(form->findChild<QPushButton*>("ird_param_paste"),nullptr);
    EXPECT_EQ(form->findChildren<QLineEdit*>(QRegularExpression("ird_joint_field_.*")).size(),14);
}
/** @brief GF-02/03：合法名称/原点与负速度共存，失败工作集与事件数完全不变。 */
TEST_F(JointGroupedFormGuiTest, AtomicFailureKeepsTextThenOneAcceptance_MDL01_T30)
{
    IRD_TEST_INFO("MDL-01",{"AT-01"},std::nullopt);const auto before=ws;
    enter(form->findChild<QLineEdit*>("ird_joint_local_name"),"joint_new");
    enter(field("origin-z"),"0.15");enter(field("max-velocity"),"-1");
    EXPECT_EQ(ws,before);confirm(*form);EXPECT_EQ(ws,before);EXPECT_EQ(accepted,0);
    EXPECT_EQ(field("max-velocity")->text(),"-1");EXPECT_TRUE(form->hasPending());shot("rejected");
    enter(field("max-velocity"),"0");confirm(*form);
    EXPECT_EQ(accepted,1);ASSERT_EQ(ws.changes.size(),1u);EXPECT_EQ(ws.design.joints[0].localName,"joint_new");
    EXPECT_DOUBLE_EQ(ws.design.joints[0].origin.value().d()[2],0.15);
    EXPECT_DOUBLE_EQ(ws.design.joints[0].maxVelocity.value(),0);EXPECT_FALSE(form->hasPending());
    confirm(*form);EXPECT_EQ(accepted,1); // 无改动不能制造成功/空历史。
}
/** @brief GF-07：显示单位与导数量共同换算，SI和工作集不动，非法原串阻止切换。 */
TEST_F(JointGroupedFormGuiTest, UnitsUseCoreAndKeepInvalidText_UX05_T30)
{
    IRD_TEST_INFO("UX-05",{"AT-01"},std::nullopt);const auto before=ws;
    enter(field("origin-z"),"0.15");enter(field("max-velocity"),"3.141592653589793");
    auto* length=form->findChild<QComboBox*>("ird_joint_length_unit");auto* angle=form->findChild<QComboBox*>("ird_joint_angle_unit");
    length->setCurrentText("mm");angle->setCurrentText("deg");QApplication::processEvents();
    EXPECT_DOUBLE_EQ(field("origin-z")->text().toDouble(),150);EXPECT_NEAR(field("max-velocity")->text().toDouble(),180,1e-8);EXPECT_EQ(ws,before);
    enter(field("origin-x"),"wrong");length->setCurrentText("m");QApplication::processEvents();
    EXPECT_EQ(length->currentText(),"mm");EXPECT_EQ(field("origin-x")->text(),"wrong");EXPECT_EQ(ws,before);
    enter(field("origin-x"),"100");confirm(*form);EXPECT_DOUBLE_EQ(ws.design.joints[0].origin.value().d()[0],0.1);
}
/** @brief GF-06：角量转线量只暂存且清空运动值，不能把旧rad当成m提交。 */
TEST_F(JointGroupedFormGuiTest, TypeStageRequiresNewDimensions_MDL01_T30)
{
    IRD_TEST_INFO("MDL-01",{"AT-01"},std::nullopt);const auto before=ws;
    auto* type=form->findChild<QComboBox*>("ird_modeling_edit_type");
    type->setCurrentIndex(type->findData(static_cast<int>(JointType::Prismatic)));QApplication::processEvents();
    EXPECT_EQ(ws,before);EXPECT_TRUE(field("zero-offset")->text().isEmpty());confirm(*form);EXPECT_EQ(ws,before);
    enter(field("zero-offset"),"0.02");enter(field("bounds-min"),"-0.2");enter(field("bounds-max"),"0.3");
    confirm(*form);EXPECT_EQ(accepted,1);EXPECT_EQ(ws.design.joints[0].type,JointType::Prismatic);
    EXPECT_DOUBLE_EQ(ws.design.joints[0].zeroOffset,0.02);EXPECT_EQ(ws.design.joints[0].maxVelocity.state(),core::FieldState::NotProvided);
}
/** @brief GF-06/MDL-12：Continuous不暗用旧硬限位；无工作范围仍可作为不完整草稿确认。 */
TEST_F(JointGroupedFormGuiTest, ContinuousRangeRequiresExplicitCarry_MDL12_T30)
{
    IRD_TEST_INFO("MDL-12",{"AT-17"},std::nullopt);
    auto* type=form->findChild<QComboBox*>("ird_modeling_edit_type");
    type->setCurrentIndex(type->findData(static_cast<int>(JointType::Continuous)));QApplication::processEvents();
    EXPECT_TRUE(field("working-range-min")->text().isEmpty());
    form->findChild<QPushButton*>("ird_joint_carry_interval")->click();
    EXPECT_FALSE(field("working-range-min")->text().isEmpty());confirm(*form);
    EXPECT_EQ(ws.design.joints[0].bounds.state(),core::FieldState::NotApplicable);
    EXPECT_EQ(ws.design.joints[0].workingRange.state(),core::FieldState::Provided);EXPECT_EQ(accepted,1);
}
/** @brief GF-08：只在主表输入控件使用系统剪贴板；非法文本保留且阻断整表采纳。 */
TEST_F(JointGroupedFormGuiTest, MainFieldUsesNativeClipboardAndKeepsAtomicValidation_UX05_T30)
{
    IRD_TEST_INFO("UX-05",{"AT-01"},std::nullopt);const auto before=ws;
    auto* clipboard=QApplication::clipboard();const QString saved=clipboard->text();
    clipboard->setText("bad");ASSERT_EQ(clipboard->text(),"bad")<<"必须验证Windows系统剪贴板，而非内部模拟数据";
    auto* edit=field("origin-x");ASSERT_NE(edit,nullptr);edit->setFocus();edit->selectAll();
    QKeyEvent paste(QEvent::KeyPress,Qt::Key_V,Qt::ControlModifier);QApplication::sendEvent(edit,&paste);QApplication::processEvents();
    EXPECT_EQ(edit->text(),"bad");confirm(*form);EXPECT_EQ(ws,before);EXPECT_EQ(accepted,0);
    enter(edit,"0.23");confirm(*form);EXPECT_EQ(accepted,1);EXPECT_DOUBLE_EQ(ws.design.joints[0].origin.value().d()[0],0.23);
    form->bindCurrent(ws.design.joints[0],ws.design.authority,false);EXPECT_FALSE(field("origin-x")->isEnabled());
    clipboard->setText(saved);
}

TEST_F(JointGroupedFormGuiTest, LeaveDecisionsNeverPartiallyCommit_UX07_T30)
{
    IRD_TEST_INFO("UX-07",{"AT-01"},std::nullopt);int left=0;const auto before=ws;
    enter(field("origin-x"),"0.2");EXPECT_FALSE(form->requestLeave([&]{++left;}));
    form->findChild<QPushButton*>("ird_joint_leave_stay")->click();EXPECT_EQ(left,0);EXPECT_TRUE(form->hasPending());
    form->requestLeave([&]{++left;});form->findChild<QPushButton*>("ird_joint_leave_discard")->click();
    EXPECT_EQ(left,1);EXPECT_EQ(ws,before);EXPECT_FALSE(form->hasPending());
    enter(field("origin-x"),"0.2");form->requestLeave([&]{++left;});form->findChild<QPushButton*>("ird_joint_leave_apply")->click();
    EXPECT_EQ(left,1);form->findChild<QPushButton*>("ird_param_confirm_yes")->click();EXPECT_EQ(left,2);EXPECT_EQ(accepted,1);
}
/** @brief GF-10：同基线刷新保留非法文本，外部基线变化禁旧提交，显式重置采纳新草稿。 */
TEST_F(JointGroupedFormGuiTest, ExternalChangePreservesInputUntilReset_MDL07_T30)
{
    IRD_TEST_INFO("MDL-07",{"AT-01"},std::nullopt);
    enter(field("origin-x"),"bad");form->bindCurrent(ws.design.joints[0],ws.design.authority,true);
    EXPECT_EQ(field("origin-x")->text(),"bad");ws.design.joints[0].zeroOffset=0.8;
    form->bindCurrent(ws.design.joints[0],ws.design.authority,true);EXPECT_EQ(field("origin-x")->text(),"bad");
    EXPECT_FALSE(form->findChild<QPushButton*>("ird_param_apply")->isEnabled());
    form->resetInputs();EXPECT_FALSE(form->hasPending());EXPECT_DOUBLE_EQ(field("zero-offset")->text().toDouble(),0.8);
    EXPECT_TRUE(form->findChild<QPushButton*>("ird_param_apply")->isEnabled());
}
/** @brief GF-05：只读可浏览单位与折叠；DH/Fixed不适用控制不能借高级表编辑。 */
TEST_F(JointGroupedFormGuiTest, ReadOnlyAndAuthorityGates_MDL09_T30)
{
    IRD_TEST_INFO("MDL-09",{"AT-01"},std::nullopt);
    form->bindCurrent(ws.design.joints[0],ws.design.authority,false);
    EXPECT_FALSE(field("origin-x")->isEnabled());EXPECT_TRUE(form->findChild<QComboBox*>("ird_joint_angle_unit")->isEnabled());
    form->bindCurrent(ws.design.joints[0],AuthorityMode::StandardDH,true);
    EXPECT_FALSE(field("axis-x")->isEnabled());EXPECT_FALSE(field("origin-x")->isEnabled());
    EXPECT_TRUE(field("zero-offset")->isEnabled());
}
/** @brief GF-05/06：可动转Fixed不要求在已禁用栏重填零偏，速度/加速度显式变不适用。 */
TEST_F(JointGroupedFormGuiTest, FixedTransitionsPreserveHistoricalMotionValues_MDL01_T30)
{
    IRD_TEST_INFO("MDL-01",{"AT-01"},std::nullopt);
    auto* type=form->findChild<QComboBox*>("ird_modeling_edit_type");
    type->setCurrentIndex(type->findData(static_cast<int>(JointType::Prismatic)));QApplication::processEvents();
    enter(field("zero-offset"),"0.02");enter(field("bounds-min"),"-0.2");enter(field("bounds-max"),"0.3");confirm(*form);
    type->setCurrentIndex(type->findData(static_cast<int>(JointType::Fixed)));QApplication::processEvents();
    EXPECT_FALSE(field("zero-offset")->isEnabled());EXPECT_TRUE(field("origin-x")->isEnabled());confirm(*form);
    EXPECT_EQ(ws.design.joints[0].type,JointType::Fixed);EXPECT_DOUBLE_EQ(ws.design.joints[0].zeroOffset,0.02);
    EXPECT_EQ(ws.design.joints[0].maxVelocity.state(),core::FieldState::NotApplicable);
    type->setCurrentIndex(type->findData(static_cast<int>(JointType::Revolute)));QApplication::processEvents();
    EXPECT_TRUE(field("zero-offset")->text().isEmpty());const auto fixed=ws;confirm(*form);EXPECT_EQ(ws,fixed);
}
/** @brief GF-12/14：整周RPY别名不制造历史；Esc只退出差异，Tab跳转不越过确认。 */
TEST_F(JointGroupedFormGuiTest, EulerEquivalentAndKeyboardStayInEdit_UX07_T30)
{
    IRD_TEST_INFO("UX-07",{"AT-01"},std::nullopt);const auto before=ws;
    enter(field("origin-yaw"),"6.283185307179586");EXPECT_FALSE(form->hasPending());confirm(*form);EXPECT_EQ(accepted,0);EXPECT_EQ(ws,before);
    enter(field("origin-x"),"0.25");form->findChild<QPushButton*>("ird_param_apply")->click();
    auto* region=form->findChild<QWidget*>("ird_param_confirm_region");ASSERT_TRUE(region->isVisible());
    field("origin-x")->setFocus();QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QApplication::sendEvent(field("origin-x"),&escape);
    QApplication::processEvents();EXPECT_TRUE(region->isHidden());EXPECT_TRUE(form->hasPending());EXPECT_EQ(ws,before);
    QKeyEvent tab(QEvent::KeyPress,Qt::Key_Tab,Qt::NoModifier);QApplication::sendEvent(field("origin-x"),&tab);
    EXPECT_EQ(QApplication::focusWidget(),field("origin-y"));
    QKeyEvent enterKey(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(field("origin-y"),&enterKey);
    EXPECT_EQ(ws,before);EXPECT_EQ(accepted,0);
}

/** @brief GF-10：整表重演使用稳定OID，重排不串写；外部同目标冲突保持完整工作集。 */
TEST(JointFormReplayTest, OidReplayAndConflictAreAtomic_MDL07_T30)
{
    IRD_TEST_INFO("MDL-07",{"AT-01"},std::nullopt);
    auto ws=draft();const auto expected=ws.design.joints[0];auto proposed=expected;proposed.localName="renamed";proposed.zeroOffset=0.2;
    PendingEdit edit;edit.form=PendingJointForm{expected,proposed};PanelRefreshCoordinator coordinator;
    coordinator.recordPending(edit);std::swap(ws.design.joints[0],ws.design.joints[1]);
    coordinator.setSinks([&]()->ModelingWorkingSet&{return ws;},[]{});
    EXPECT_EQ(coordinator.onRevisionEvent(std::nullopt),ReplayOutcome::Replayed);
    EXPECT_EQ(ws.design.joints[1].localName,"renamed");EXPECT_NE(ws.design.joints[0].localName,"renamed");
    const auto before=ws;EXPECT_EQ(coordinator.onRevisionEvent(std::nullopt),ReplayOutcome::BlockedAtEdit);EXPECT_EQ(ws,before);
}
}
