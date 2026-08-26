#include <rws/ProjectCreationWizard.hpp>
#include <rws/RobotProjectImportWizard.hpp>
#include <rws/WorkCellProjectImportWizard.hpp>

#include <QDir>
#include <QFile>
#include <QApplication>
#include <QLineEdit>
#include <QComboBox>
#include <QTemporaryDir>
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

TEST (RobotProjectImportWizard, ProvidesFourPagesWithoutOptimizationPreselection)
{
    int argc = 1;
    char name[] = "RobotProjectImportWizardTest";
    char* argv[1] = {name};
    QApplication application (argc, argv);
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());

    rws::RobotProjectImportWizard wizard (directory.path ());
    EXPECT_EQ (4, wizard.pageIds ().size ());
    EXPECT_NE (nullptr, wizard.findChild< QLineEdit* > (QStringLiteral ("projectName")));
    EXPECT_NE (nullptr, wizard.findChild< QLineEdit* > (QStringLiteral ("projectLocation")));
    EXPECT_NE (nullptr, wizard.findChild< QLineEdit* > (QStringLiteral ("robotSourcePath")));
    EXPECT_EQ (nullptr, wizard.findChild< QWidget* > (QStringLiteral ("mutableLinks")));
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
