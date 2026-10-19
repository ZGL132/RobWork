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
