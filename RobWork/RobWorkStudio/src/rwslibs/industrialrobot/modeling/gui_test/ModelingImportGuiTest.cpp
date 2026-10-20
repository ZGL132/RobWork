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
#include <QFile>
#include <QFileDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QPushButton>
#include <QScrollBar>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QScopeGuard>   // qScopeGuard（F06 冒烟环境进程内自开自收）
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include "plugin/ModelingCommandFlows.hpp"
#include "plugin/ModelingUiModule.hpp"   // ModuleSessionState（F06 会话态入参）

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

/** @brief F06/MDL-18/AT-33：WorkCell 导入真实 Qt 对话框链——生产宿主
 * openFilePath（smoke 态非原生对话框）选真实 .wc.xml→真实引擎提取→
 * 真实报告对话框承载提取内容（设备名/映射/未保留项可见）→取消零替换。
 * 单设备夹具不经选链（chooseItem 面另由流测试替身覆盖）。
 */
TEST(ModelingImportGui, WorkCellRealFileDialogToReportCancelSafe_MDL18_AT33)
{
    IRD_TEST_INFO("MDL-18",{"AT-33"},std::nullopt);
    // 冒烟对话框形态自备（IRD_UI_PLUGIN_SMOKE 的设计用途＝让生产文件对话
    // 框以非原生形态可自动化——原生模态无法 findChild 应答；ctest/gate 上下
    // 文不带该变量，本用例进程内自开自收，测试结束恢复进程环境）。
    const bool hadSmoke = !qEnvironmentVariable("IRD_UI_PLUGIN_SMOKE").isEmpty();
    qputenv("IRD_UI_PLUGIN_SMOKE", "1");
    const auto restoreSmoke = qScopeGuard([&hadSmoke] {
        if (hadSmoke) { qputenv("IRD_UI_PLUGIN_SMOKE", "1"); }
        else { qunsetenv("IRD_UI_PLUGIN_SMOKE"); }
    });
    // 临时目录真实 .wc.xml（三关节最小可提取单设备——数值面由流测试独立
    // 核对，此处验证真实对话框链与报告内容承载）。
    QTemporaryDir dir;ASSERT_TRUE(dir.isValid());
    const QString wcPath=dir.filePath(QStringLiteral("real.wc.xml"));
    {QFile f(wcPath);ASSERT_TRUE(f.open(QIODevice::WriteOnly|QIODevice::Text));
     f.write(QStringLiteral("<?xml version=\"1.0\"?>\n"
        "<WorkCell name=\"gui cell\">\n"
        "  <SerialDevice name=\"guiarm\">\n"
        "    <Joint name=\"g1\" type=\"Revolute\"><PosLimit min=\"-90\" max=\"90\"/><VelLimit max=\"1.2\"/></Joint>\n"
        "    <Joint name=\"g2\" type=\"Revolute\"><PosLimit min=\"-90\" max=\"90\"/></Joint>\n"
        "    <Joint name=\"g3\" type=\"Revolute\"><PosLimit min=\"-120\" max=\"120\"/></Joint>\n"
        "    <Q name=\"Home\"> 0.1 0.2 0.3 </Q>\n"
        "  </SerialDevice>\n"
        "</WorkCell>\n").toUtf8());}
    // 事件链应答：①真实文件对话框（名称框输入真实路径＋回车＝用户确认）；
    // ②下一拍提取报告对话框（断言内容后 reject＝用户取消——取消安全面）。
    // 两级定时器链：文件对话框关闭须先回到事件循环，报告模态才升起。
    ModalWatchdog watchdog;
    QTimer::singleShot(0,[wcPath]{
        auto* fileDialog=qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
        ASSERT_NE(fileDialog,nullptr)<<"生产文件选择对话框未弹出";
        // 填入文件名编辑框并以回车确认（accept 为 protected——真实用户
        // 路径＝在名称框输入并回车，不走受保护接口）。
        auto* nameEdit=fileDialog->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"));
        ASSERT_NE(nameEdit,nullptr)<<"非原生文件对话框缺少文件名编辑框";
        nameEdit->setText(wcPath);
        // 确认＝触发对话框 accept 槽（与"打开"按钮同一槽；accept 为
        // protected 故经元对象调用——对话框本体/路径选择均为真实生产面）。
        QMetaObject::invokeMethod(fileDialog,"accept");
        QTimer::singleShot(0,[]{
            auto* report=qobject_cast<QDialog*>(QApplication::activeModalWidget());
            ASSERT_NE(report,nullptr);
            EXPECT_EQ(report->objectName(),QStringLiteral("ird_modeling_import_report"));
            auto* text=report->findChild<QPlainTextEdit*>("ird_modeling_import_report_text");
            ASSERT_NE(text,nullptr);
            const QString body=text->toPlainText();
            EXPECT_TRUE(body.contains(QStringLiteral("guiarm")))<<"报告未承载设备名";
            EXPECT_TRUE(body.contains(QStringLiteral("joints[0].bounds")))<<"报告未承载映射落点";
            EXPECT_TRUE(body.contains(QStringLiteral("joint-velocity-limit")))<<"未保留项不可见";
            snapshot(*report,"workcell-import-report");
            report->reject();
        });
    });
    // 生产宿主直接驱动命令流（qtModelingDialogHost 真实模态）。
    ModuleSessionState session;  // 空会话——取消后应保持零关节（未被替换）
    std::string summary;
    const bool executed=executeModelingCommand("modeling.import-workcell",
        session,ModelingFlowDeps{},qtModelingDialogHost(),summary);
    EXPECT_FALSE(executed)<<"报告取消后不得报完成";
    EXPECT_TRUE(summary.empty())<<"取消属正常终止（UX-03），无错误回执";
    EXPECT_TRUE(session.draft.design.joints.empty())<<"取消后草稿被替换（违约）";
}
