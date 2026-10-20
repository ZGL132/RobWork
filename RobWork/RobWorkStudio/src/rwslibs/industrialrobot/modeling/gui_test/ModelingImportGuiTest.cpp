/**
 * @file ModelingImportGuiTest.cpp
 * @brief WP-13-T24：导入参数与报告的真实Windows模态窗口验收。
 *
 * MDL-11/19、AT-15/31：驱动生产对话宿主，不替换窗口、不绕过校验。
 * QApplication事件循环由单个GUI测试进程拥有；定时器仅模拟用户应答。
 * 观察实际控件、滚动条、焦点、错误提示与窗口截图，不冒充三维验收。
 */
#include <gtest/gtest.h>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QPushButton>
#include <QScrollBar>
#include <QTableWidget>
#include <QTimer>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include "plugin/ModelingCommandFlows.hpp"
#include <sdurws/ird/modeling/Template.hpp>
#include <sdurws/ird/modeling/ModelDiff.hpp>

using namespace sdurws::ird::modeling;
namespace {
/**
 * @brief 单测试UI线程拥有的模态看门狗，五秒未响应则拒绝窗口，避免失败后挂进程。
 * 所有权：timer由本值持有，析构停止；不保留窗口裸指针。
 */
class ModalWatchdog {
public:
    ModalWatchdog()
    {
        QObject::connect(&timer,&QTimer::timeout,[]{
            ADD_FAILURE()<<"生产模态对话框未按预期响应";
            if(auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget())){dialog->reject();}
        });
        timer.setSingleShot(true);timer.start(5000);
    }
private:
    QTimer timer; ///< 仅当前测试UI线程访问，析构撤销超时回调。
};
/** @brief 保存真实窗口截图；[in] name为本测试唯一文件名，目录由runner显式指定。 */
void snapshot(QDialog& dialog,const QString& name)
{
    const auto out=qEnvironmentVariable("IRD_MODELING_GUI_EVIDENCE_OUT");
    if(out.isEmpty()){return;} // 常规无截图跑法仍执行全部控件断言，不伪造图片。
    ASSERT_TRUE(QDir().mkpath(out));
    ASSERT_TRUE(dialog.grab().save(QDir(out).filePath(name+".png")));
}
}

/** @brief MDL-05：空段元表拒绝、长表滚动以及取消均不产生估算结果。 */
TEST(ModelingImportGui, PropertyEmptyLongTableCancel_MDL05)
{
    IRD_TEST_INFO("MDL-05",{},std::nullopt);ModalWatchdog watchdog;
    QTimer::singleShot(0,[]{
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());ASSERT_NE(dialog,nullptr);
        auto* table=dialog->findChild<QTableWidget*>("ird_modeling_segments_table");ASSERT_NE(table,nullptr);
        auto* remove=dialog->findChild<QPushButton*>("ird_modeling_segments_remove");ASSERT_NE(remove,nullptr);remove->click();
        EXPECT_EQ(table->rowCount(),0);dialog->findChild<QPushButton*>("ird_modeling_segments_confirm")->click();
        EXPECT_TRUE(dialog->isVisible());
        const auto message=dialog->findChild<QLabel*>("ird_modeling_segments_feedback")->text();
        EXPECT_TRUE(message.contains(QStringLiteral("至少一个段元")));
        EXPECT_FALSE(message.contains("@pre"));
        auto* add=dialog->findChild<QPushButton*>("ird_modeling_segments_add");ASSERT_NE(add,nullptr);
        for(int row=0;row<40;++row){add->click();}
        QApplication::processEvents();EXPECT_GT(table->verticalScrollBar()->maximum(),0);
        snapshot(*dialog,"property-segments-long");dialog->reject();
    });
    EXPECT_FALSE(qtModelingDialogHost().editPropertySegments("base").has_value());
}

/** @brief MDL-08/UX-13：正式命令必须显示逐项差异，而非只有计数的消息框。 */
TEST(ModelingImportGui, FullDiffFromProductionCommand_MDL08_UX13_AT12)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-08","UX-13"},{"AT-12"},std::nullopt);
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    ModuleSessionState session;
    session.draft=RobotDesignTemplateFactory{}.createDraft(TemplateId{kTemplateIdGeneric6R},
        sdurws::ird::runtime::InstallationPresetToken::Ground,"diff-demo",diags).get();
    using namespace sdurws::ird;
    session.draft.design.links[0].body.mass=core::SourcedValue<double>::provided(
        1.5,core::ValueProvenance::make(core::ProvenanceKind::UserProvided,
            session.draft.design.joints[0].objectId));
    session.draft.design.links[1].body.mass=core::SourcedValue<double>::provided(
        3.5,core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
    session.baselineSnapshot=session.draft;
    const auto baseline=*session.baselineSnapshot;
    session.draft.design.displayName="changed-name";
    ASSERT_FALSE(applyJointFieldEdit(session.draft,0,JointEditField::ZeroOffset,JointEditValue{0.25}));
    session.draft.design.links[0].body.mass=core::SourcedValue<double>::provided(
        2.5,core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
    session.draft.design.links[1].body.mass=core::SourcedValue<double>::provided(
        3.5,core::ValueProvenance::make(core::ProvenanceKind::ImportMapped));
    const auto before=session.draft;
    ModalWatchdog watchdog;
    QTimer::singleShot(0,[]{
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        ASSERT_NE(dialog,nullptr);
        EXPECT_EQ(dialog->objectName(),"ird_modeling_diff");
        auto* table=dialog->findChild<QTableWidget*>("ird_modeling_diff_table");
        EXPECT_NE(table,nullptr)<<"F16：生产页只有数量，缺逐项分组、前后值与定位";
        if(table){
            ASSERT_EQ(table->rowCount(),4);ASSERT_EQ(table->columnCount(),9);
            EXPECT_EQ(table->editTriggers(),QAbstractItemView::NoEditTriggers);
            bool structure=false,parameter=false,property=false,sourceOnly=false;
            for(int row=0;row<table->rowCount();++row){
                const auto field=table->item(row,2)->text();
                if(field=="displayName"){
                    structure=true;EXPECT_EQ(table->item(row,0)->text(),QStringLiteral("结构"));
                    EXPECT_EQ(table->item(row,5)->text(),"diff-demo");
                    EXPECT_EQ(table->item(row,6)->text(),"changed-name");
                }
                if(field=="joints[0].zeroOffset"){
                    parameter=true;EXPECT_EQ(table->item(row,0)->text(),QStringLiteral("参数"));
                    EXPECT_EQ(table->item(row,4)->text(),"rad");
                    EXPECT_TRUE(table->item(row,6)->text().contains("0.25"));
                }
                if(field=="links[0].body.mass"){
                    property=true;EXPECT_EQ(table->item(row,0)->text(),QStringLiteral("物性"));
                    EXPECT_EQ(table->item(row,4)->text(),"kg");
                    EXPECT_TRUE(table->item(row,5)->text().contains("1.5"));
                    EXPECT_TRUE(table->item(row,5)->text().contains(QStringLiteral("关节 j1")));
                    EXPECT_FALSE(table->item(row,5)->text().contains("obj-"));
                    EXPECT_TRUE(table->item(row,6)->text().contains("2.5"));
                }
                if(field=="links[1].body.mass"){
                    sourceOnly=true;EXPECT_EQ(table->item(row,7)->text(),QStringLiteral("否"));
                    EXPECT_EQ(table->item(row,8)->text(),QStringLiteral("是"));
                }
            }
            EXPECT_TRUE(structure&&parameter&&property&&sourceOnly);
            snapshot(*dialog,"diff-three-groups");
        }
        dialog->reject();
    });
    std::string summary;
    EXPECT_TRUE(executeModelingCommand("modeling.diff-baseline",session,{},qtModelingDialogHost(),summary));
    EXPECT_EQ(session.draft,before);
    EXPECT_EQ(*session.baselineSnapshot,baseline)<<"比较不允许覆盖基线";
}

/** @brief UX-13/AT-12：同一身份由旋转改为移动，前后值必须各带正确rad/m单位。 */
TEST(ModelingImportGui, DiffUnitsFollowBothJointTypes_UX13_AT12)
{
    IRD_TEST_INFO("UX-13",{"AT-12"},std::nullopt);
    using namespace sdurws::ird;
    std::vector<core::DiagnosticRecord> diags;
    const auto base=RobotDesignTemplateFactory{}.createDraft(TemplateId{kTemplateIdGeneric6R},
        runtime::InstallationPresetToken::Ground,"unit-diff",diags).get();
    auto candidate=base;candidate.design.joints[0].type=JointType::Prismatic;
    candidate.design.joints[0].zeroOffset=0.125;
    ModalWatchdog watchdog;
    QTimer::singleShot(0,[]{
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());ASSERT_NE(dialog,nullptr);
        auto* table=dialog->findChild<QTableWidget*>("ird_modeling_diff_table");ASSERT_NE(table,nullptr);
        bool found=false;
        for(int row=0;row<table->rowCount();++row){
            if(table->item(row,2)->text()=="joints[0].zeroOffset"){
                found=true;EXPECT_EQ(table->item(row,4)->text(),QStringLiteral("基线 rad；草稿 m"));
            }
        }
        EXPECT_TRUE(found);dialog->reject();
    });
    qtModelingDialogHost().showModelDiff(ModelDiffService{}.diff(base,candidate),base,candidate,{});
}

/** @brief UX-13/AT-12：原服务长报告、空表和无定位回调均诚实呈现，不截掉详情。 */
TEST(ModelingImportGui, DiffLongValuesEmptyAndUnwiredLocation_UX13_AT12)
{
    IRD_TEST_INFO("UX-13",{"AT-12"},std::nullopt);
    using namespace sdurws::ird;
    std::vector<core::DiagnosticRecord> diags;
    const auto base=RobotDesignTemplateFactory{}.createDraft(TemplateId{kTemplateIdGeneric6R},
        runtime::InstallationPresetToken::Ground,"long-diff",diags).get();
    auto candidate=base;
    candidate.design.notes=std::string(12000,'Z');
    // 使用真实服务长输出而非手造报告；内容摘要不允许作为用户参数显示。
    ResourceRef resource;resource.resourceId="new-resource";resource.contentDigest.fill(0xaa);
    candidate.design.resourceManifest.push_back(resource);
    const auto report=ModelDiffService{}.diff(base,candidate);
    ModalWatchdog watchdog;
    QTimer::singleShot(0,[]{
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());ASSERT_NE(dialog,nullptr);
        auto* table=dialog->findChild<QTableWidget*>("ird_modeling_diff_table");ASSERT_NE(table,nullptr);
        auto* detail=dialog->findChild<QPlainTextEdit*>("ird_modeling_diff_detail");ASSERT_NE(detail,nullptr);
        EXPECT_TRUE(detail->isReadOnly());
        dialog->resize(780,650);QApplication::processEvents(); // 真窄窗口必须可横向浏览全部列。
        EXPECT_GT(table->horizontalScrollBar()->maximum(),0);
        for(int row=0;row<table->rowCount();++row){
            if(table->item(row,2)->text()=="notes"){
                // 原字段存在但文本为空，与“新增条目在基线不存在”必须区分。
                EXPECT_EQ(table->item(row,5)->text(),QStringLiteral("（空）"));
                table->setCurrentCell(row,0);
                EXPECT_TRUE(detail->toPlainText().contains(QString(12000,'Z')));
                detail->selectAll();EXPECT_FALSE(detail->textCursor().selectedText().isEmpty());
                EXPECT_TRUE(detail->textInteractionFlags().testFlag(Qt::TextSelectableByMouse));
            }
            if(table->item(row,2)->text().startsWith("resourceManifest[")){
                EXPECT_EQ(table->item(row,5)->text(),QStringLiteral("（不存在）"));
            }
            for(int col=0;col<table->columnCount();++col){
                EXPECT_FALSE(table->item(row,col)->text().contains(QString(64,'a')));
            }
        }
        auto* jump=dialog->findChild<QPushButton*>("ird_modeling_diff_locate");ASSERT_NE(jump,nullptr);jump->click();
        auto* feedback=dialog->findChild<QLabel*>("ird_modeling_diff_feedback");ASSERT_NE(feedback,nullptr);
        EXPECT_TRUE(feedback->text().contains(QStringLiteral("未装配")));
        snapshot(*dialog,"diff-long-values");dialog->reject();
    });
    qtModelingDialogHost().showModelDiff(report,base,candidate,{});
    QTimer::singleShot(0,[]{
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());ASSERT_NE(dialog,nullptr);
        auto* table=dialog->findChild<QTableWidget*>("ird_modeling_diff_table");ASSERT_NE(table,nullptr);
        EXPECT_EQ(table->rowCount(),0);
        EXPECT_FALSE(dialog->findChild<QPushButton*>("ird_modeling_diff_locate")->isEnabled());
        EXPECT_TRUE(dialog->findChild<QLabel*>("ird_modeling_diff_feedback")->text().contains(QStringLiteral("无差异")));
        snapshot(*dialog,"diff-empty");dialog->reject();
    });
    qtModelingDialogHost().showModelDiff(ModelDiffService{}.diff(base,base),base,base,{});
}

TEST(ModelingImportGui, ParametersRejectDuplicateThenApplyLiteralOverride_MDL19_AT31)
{
    IRD_TEST_INFO("MDL-19",{"AT-31"},std::nullopt);
    ModalWatchdog watchdog;
    QTimer::singleShot(0,[]{
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        ASSERT_NE(dialog,nullptr);
        EXPECT_EQ(dialog->objectName(),"ird_modeling_xacro_parameters");
        auto* table=dialog->findChild<QTableWidget*>("ird_modeling_xacro_parameter_table");
        auto* buttons=dialog->findChild<QDialogButtonBox*>();
        ASSERT_NE(table,nullptr);ASSERT_NE(buttons,nullptr);
        ASSERT_EQ(table->rowCount(),2);
        EXPECT_EQ(table->item(0,1)->text(),"1.25");
        table->item(1,0)->setText("travel");
        buttons->button(QDialogButtonBox::Ok)->click();
        EXPECT_TRUE(dialog->isVisible());
        auto* error=dialog->findChild<QLabel*>("ird_modeling_xacro_parameter_error");
        ASSERT_NE(error,nullptr);EXPECT_FALSE(error->text().isEmpty());
        snapshot(*dialog,"xacro-duplicate-error");
        table->item(1,0)->setText("width");table->item(0,1)->setText("2.75");
        snapshot(*dialog,"xacro-parameters");
        buttons->button(QDialogButtonBox::Ok)->click();
    });
    const auto result=qtModelingDialogHost().editXacroParameters({{"travel","1.25"},{"width","0.5"}});
    ASSERT_TRUE(result.has_value());ASSERT_EQ(result->size(),1u);
    EXPECT_EQ(result->front().first,"travel");EXPECT_EQ(result->front().second,"2.75");
}

TEST(ModelingImportGui, ParameterCancelReturnsNoSubstitution_MDL19_AT31)
{
    IRD_TEST_INFO("MDL-19",{"AT-31"},std::nullopt);
    ModalWatchdog watchdog;
    QTimer::singleShot(0,[]{
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());ASSERT_NE(dialog,nullptr);
        auto* table=dialog->findChild<QTableWidget*>();ASSERT_NE(table,nullptr);
        table->item(0,1)->setText("9.5");
        dialog->reject();
    });
    EXPECT_FALSE(qtModelingDialogHost().editXacroParameters({{"travel","1.25"}}).has_value());
}

TEST(ModelingImportGui, FullImportReportScrollableSelectableAndCancelSafe_MDL11_AT15)
{
    IRD_TEST_INFO("MDL-11",{"AT-15"},std::nullopt);
    QString report=QStringLiteral("导入映射报告\n");
    for(int i=0;i<100;++i){report+=QStringLiteral("[映射] joint[j%1]/limit/lower → joints[%1].bounds = -1.57 rad\n").arg(i);}
    ModalWatchdog watchdog;
    QTimer::singleShot(0,[report]{
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());ASSERT_NE(dialog,nullptr);
        auto* text=dialog->findChild<QPlainTextEdit*>("ird_modeling_import_report_text");
        ASSERT_NE(text,nullptr);EXPECT_TRUE(text->isReadOnly());EXPECT_EQ(text->toPlainText(),report);
        EXPECT_GT(text->verticalScrollBar()->maximum(),0);
        EXPECT_TRUE(text->textInteractionFlags().testFlag(Qt::TextSelectableByMouse));
        EXPECT_TRUE(dialog->rect().contains(text->geometry()));
        snapshot(*dialog,"import-report");
        dialog->reject();
    });
    EXPECT_FALSE(qtModelingDialogHost().confirmImport(report));
}
