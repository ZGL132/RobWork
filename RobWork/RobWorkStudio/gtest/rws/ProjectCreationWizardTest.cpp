#include <rws/ProjectCreationWizard.hpp>
#include <rws/RobotProjectImportWizard.hpp>
#include <rws/WorkCellProjectImportWizard.hpp>

#include <QDir>
#include <QFile>
#include <QApplication>
#include <QLineEdit>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QCoreApplication>
#include <gtest/gtest.h>

TEST (ProjectCreationRequest, BuildsProjectFileFromNameAndLocation)
{
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    rws::ProjectCreationRequest request;
    request.projectName = QStringLiteral ("Assembly Cell");
    request.location = directory.path ();
    request.templateId = rws::ProjectCreationWizard::GenericSixAxisTemplateId;

    QString error;
    ASSERT_TRUE (request.isValid (&error)) << error.toStdString ();
    EXPECT_EQ (QDir (directory.path ()).filePath (QStringLiteral ("Assembly Cell.rwproj")),
               request.projectFilePath ());
}

TEST (ProjectCreationRequest, RejectsMissingTemplateOrUnsafeProjectName)
{
    rws::ProjectCreationRequest request;
    request.projectName = QStringLiteral ("../outside");
    request.location = QDir::tempPath ();
    request.templateId = rws::ProjectCreationWizard::GenericSixAxisTemplateId;

    QString error;
    EXPECT_FALSE (request.isValid (&error));
    EXPECT_FALSE (error.isEmpty ());

    request.projectName = QStringLiteral ("Robot");
    request.templateId.clear ();
    EXPECT_FALSE (request.isValid (&error));
}

TEST (RobotProjectImportRequest, BuildsNormalizedProjectFileAndRejectsUnsafeName)
{
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    QFile source (directory.filePath (QStringLiteral ("robot.urdf")));
    ASSERT_TRUE (source.open (QIODevice::WriteOnly));
    source.write ("<robot name=\"r\"/>\n");
    source.close ();

    rws::RobotProjectImportRequest request;
    request.sourcePath = directory.filePath (QStringLiteral ("robot.urdf"));
    request.projectName = QStringLiteral ("Assembly Cell.rwproj");
    request.location = directory.path ();

    QString error;
    ASSERT_TRUE (request.isValid (&error)) << error.toStdString ();
    EXPECT_EQ (directory.filePath (QStringLiteral ("Assembly Cell.rwproj")),
               request.projectFilePath ());

    request.projectName = QStringLiteral ("../outside");
    EXPECT_FALSE (request.isValid (&error));

    request.projectName = QStringLiteral (" Robot ");
    EXPECT_FALSE (request.isValid (&error));
}

TEST (RobotProjectImportRequest, RejectsInvertedMutableLinkRange)
{
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    QFile source (directory.filePath (QStringLiteral ("robot.urdf")));
    ASSERT_TRUE (source.open (QIODevice::WriteOnly));
    source.write ("<robot name=\"r\"/>\n");
    source.close ();

    rws::RobotProjectImportRequest request;
    request.sourcePath = source.fileName ();
    request.projectName = QStringLiteral ("Robot");
    request.location = directory.path ();
    request.mutableLinkRanges.insert (QStringLiteral ("joint1"), qMakePair (0.8, 0.2));

    QString error;
    EXPECT_FALSE (request.isValid (&error));
    EXPECT_TRUE (error.contains (QStringLiteral ("minimum"), Qt::CaseInsensitive));
}

TEST (RobotProjectImportWizard, ProvidesFivePagesForTheImportWorkflow)
{
    int argc = 1;
    char name[] = "RobotProjectImportWizardTest";
    char* argv[1] = {name};
    QApplication application (argc, argv);
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());

    rws::RobotProjectImportWizard wizard (directory.path ());
    EXPECT_EQ (5, wizard.pageIds ().size ());
    EXPECT_NE (nullptr, wizard.findChild< QLineEdit* > (QStringLiteral ("projectName")));
    EXPECT_NE (nullptr, wizard.findChild< QLineEdit* > (QStringLiteral ("projectLocation")));
    EXPECT_NE (nullptr, wizard.findChild< QLineEdit* > (QStringLiteral ("robotSourcePath")));
}

TEST (RobotProjectImportWizard, BuildsHierarchicalChainAndLocksFixedLinks)
{
    int argc = 1;
    char name[] = "RobotProjectImportWizardSourceTest";
    char* argv[1] = {name};
    QApplication application (argc, argv);
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString sourcePath = directory.filePath (QStringLiteral ("arm.urdf"));
    QFile source (sourcePath);
    ASSERT_TRUE (source.open (QIODevice::WriteOnly));
    source.write ("<robot name=\"arm\"><link name=\"base\"/><link name=\"link1\"/><link name=\"tool\"/>"
                  "<joint name=\"joint1\" type=\"revolute\"><parent link=\"base\"/><child link=\"link1\"/></joint>"
                  "<joint name=\"tool_joint\" type=\"fixed\"><parent link=\"link1\"/><child link=\"tool\"/></joint></robot>\n");
    source.close ();

    rws::RobotProjectImportWizard wizard (directory.path ());
    QLineEdit* sourceField = wizard.findChild< QLineEdit* > (QStringLiteral ("robotSourcePath"));
    ASSERT_NE (nullptr, sourceField);
    sourceField->setText (sourcePath);
    QCoreApplication::processEvents ();

    EXPECT_EQ (QStringLiteral ("arm"),
               wizard.findChild< QLineEdit* > (QStringLiteral ("projectName"))->text ());
    QTreeWidget* links = wizard.findChild< QTreeWidget* > (QStringLiteral ("mutableLinks"));
    ASSERT_NE (nullptr, links);
    ASSERT_EQ (1, links->topLevelItemCount ());
    QTreeWidgetItem* base = links->topLevelItem (0);
    ASSERT_EQ (QStringLiteral ("base"), base->text (0));
    ASSERT_EQ (1, base->childCount ());
    QTreeWidgetItem* link1 = base->child (0);
    EXPECT_EQ (QStringLiteral ("revolute"), link1->text (1));
    EXPECT_EQ (QStringLiteral ("joint1"), link1->data (0, Qt::UserRole).toString ());
    EXPECT_TRUE (link1->flags () & Qt::ItemIsUserCheckable);
    ASSERT_EQ (1, link1->childCount ());
    QTreeWidgetItem* tool = link1->child (0);
    EXPECT_EQ (QStringLiteral ("fixed"), tool->text (1));
    EXPECT_FALSE (tool->flags () & Qt::ItemIsUserCheckable);
    EXPECT_EQ (nullptr, links->itemWidget (tool, 3));
    EXPECT_EQ (nullptr, links->itemWidget (tool, 4));
    link1->setCheckState (2, Qt::Checked);
    EXPECT_EQ (QStringList ({QStringLiteral ("joint1")}), wizard.request ().mutableLinks);
    EXPECT_GE (wizard.request ().options.packageRoots.size (), 1);
}

TEST (WorkCellProjectImportWizard, ProvidesFivePagesAndBindsDetectedRobot)
{
    int argc = 1;
    char name[] = "WorkCellProjectImportWizardTest";
    char* argv[1] = {name};
    QApplication application (argc, argv);
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString source = QDir (QStringLiteral (RWS_TEST_SOURCE_DIR)).filePath (
        QStringLiteral ("RobWork/example/ModelData/XMLDevices/UR-6-85-5-A/UR.wc.xml"));

    rws::WorkCellProjectImportWizard wizard (directory.path ());
    EXPECT_EQ (5, wizard.pageIds ().size ());
    QLineEdit* sourceField = wizard.findChild< QLineEdit* > (QStringLiteral ("workCellSourcePath"));
    ASSERT_NE (nullptr, sourceField);
    sourceField->setText (source);
    QCoreApplication::processEvents ();

    EXPECT_EQ (QStringLiteral ("UR"),
               wizard.findChild< QLineEdit* > (QStringLiteral ("projectName"))->text ());
    QComboBox* device = wizard.findChild< QComboBox* > (QStringLiteral ("targetDevice"));
    QComboBox* tcp = wizard.findChild< QComboBox* > (QStringLiteral ("tcpFrame"));
    ASSERT_NE (nullptr, device);
    ASSERT_NE (nullptr, tcp);
    EXPECT_EQ (1, device->count ());
    EXPECT_FALSE (tcp->currentData ().toString ().isEmpty ());
}
