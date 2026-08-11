#include <rws/WorkflowBinding.hpp>
#include <rws/ProjectManager.hpp>

#include <QTemporaryDir>
#include <gtest/gtest.h>

TEST (WorkflowBinding, PersistsStableProjectContext)
{
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());

    rws::WorkflowBinding binding;
    binding.projectId = QStringLiteral ("project-1");
    binding.targetDevice = QStringLiteral ("UR5");
    binding.tcpFrame = QStringLiteral ("Tool");
    binding.sceneResourceId = QStringLiteral ("scene.main");
    binding.modelResourceId = QStringLiteral ("robot-model.main");
    binding.sourceKind = QStringLiteral ("urdf");
    binding.sourceFingerprint = QStringLiteral ("source-v1");

    QString error;
    ASSERT_TRUE (binding.write (directory.path (), &error)) << error.toStdString ();

    rws::WorkflowBinding loaded;
    ASSERT_TRUE (rws::WorkflowBinding::read (directory.path (), loaded, &error)) << error.toStdString ();
    EXPECT_EQ (binding.projectId, loaded.projectId);
    EXPECT_EQ (binding.targetDevice, loaded.targetDevice);
    EXPECT_EQ (binding.tcpFrame, loaded.tcpFrame);
    EXPECT_EQ (binding.sceneResourceId, loaded.sceneResourceId);
    EXPECT_EQ (binding.modelResourceId, loaded.modelResourceId);
}

TEST (WorkflowBinding, RejectsMissingRequiredIdentifiers)
{
    rws::WorkflowBinding binding;
    QString error;

    EXPECT_FALSE (binding.isValid (&error));
    EXPECT_FALSE (error.isEmpty ());
}

TEST (WorkflowBinding, ProjectManagerRewritesBindingProjectIdDuringClone)
{
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString sourceProject = directory.filePath (QStringLiteral ("source/project.rwproj"));
    const QString clonedProject = directory.filePath (QStringLiteral ("clone/project.rwproj"));

    rws::ProjectManifest manifest;
    manifest.project.name = QStringLiteral ("Source");
    rws::ProjectManager manager;
    QString error;
    ASSERT_TRUE (manager.createProject (sourceProject, manifest, &error)) << error.toStdString ();

    rws::WorkflowBinding binding;
    binding.projectId = manager.manifest ().project.id;
    binding.targetDevice = QStringLiteral ("UR5");
    binding.tcpFrame = QStringLiteral ("Tool");
    binding.sceneResourceId = QStringLiteral ("scene.main");
    binding.modelResourceId = QStringLiteral ("robot-model.main");
    ASSERT_TRUE (manager.saveWorkflowBinding (binding, &error)) << error.toStdString ();
    ASSERT_TRUE (manager.cloneProject (clonedProject, &error)) << error.toStdString ();

    rws::WorkflowBinding cloned;
    ASSERT_TRUE (manager.loadWorkflowBinding (cloned, &error)) << error.toStdString ();
    EXPECT_EQ (manager.manifest ().project.id, cloned.projectId);
    EXPECT_NE (binding.projectId, cloned.projectId);
}

TEST (WorkflowBinding, PackageRoundTripRetainsBinding)
{
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString projectFile = directory.filePath (QStringLiteral ("source/project.rwproj"));
    const QString packageFile = directory.filePath (QStringLiteral ("project.rwpack"));
    const QString extractedDirectory = directory.filePath (QStringLiteral ("extracted"));

    rws::ProjectManifest manifest;
    manifest.project.name = QStringLiteral ("Source");
    rws::ProjectManager manager;
    QString error;
    ASSERT_TRUE (manager.createProject (projectFile, manifest, &error)) << error.toStdString ();
    rws::WorkflowBinding binding;
    binding.projectId = manager.manifest ().project.id;
    binding.targetDevice = QStringLiteral ("UR5");
    binding.tcpFrame = QStringLiteral ("Tool");
    binding.sceneResourceId = QStringLiteral ("scene.main");
    binding.modelResourceId = QStringLiteral ("robot-model.main");
    ASSERT_TRUE (manager.saveWorkflowBinding (binding, &error)) << error.toStdString ();
    ASSERT_TRUE (manager.exportPackage (packageFile, &error)) << error.toStdString ();

    QString extractedProject;
    ASSERT_TRUE (rws::ProjectManager::extractPackage (
        packageFile, extractedDirectory, extractedProject, &error)) << error.toStdString ();
    rws::ProjectManager reopened;
    ASSERT_TRUE (reopened.openProject (extractedProject, &error)) << error.toStdString ();
    rws::WorkflowBinding extracted;
    ASSERT_TRUE (reopened.loadWorkflowBinding (extracted, &error)) << error.toStdString ();
    EXPECT_EQ (reopened.manifest ().project.id, extracted.projectId);
}

TEST (WorkflowBinding, AutosaveRestoreRestoresBindingWithProjectResources)
{
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString projectFile = directory.filePath (QStringLiteral ("source/project.rwproj"));

    rws::ProjectManifest manifest;
    manifest.project.name = QStringLiteral ("Source");
    rws::ProjectManager manager;
    QString error;
    ASSERT_TRUE (manager.createProject (projectFile, manifest, &error)) << error.toStdString ();

    rws::WorkflowBinding original;
    original.projectId = manager.manifest ().project.id;
    original.targetDevice = QStringLiteral ("UR5");
    original.tcpFrame = QStringLiteral ("Tool");
    original.sceneResourceId = QStringLiteral ("scene.main");
    original.modelResourceId = QStringLiteral ("robot-model.main");
    ASSERT_TRUE (manager.saveWorkflowBinding (original, &error)) << error.toStdString ();
    ASSERT_TRUE (manager.createAutosaveSnapshot (&error)) << error.toStdString ();

    rws::WorkflowBinding changed = original;
    changed.targetDevice = QStringLiteral ("UR10");
    ASSERT_TRUE (manager.saveWorkflowBinding (changed, &error)) << error.toStdString ();
    ASSERT_TRUE (manager.restoreAutosaveSnapshot (&error)) << error.toStdString ();

    rws::WorkflowBinding restored;
    ASSERT_TRUE (manager.loadWorkflowBinding (restored, &error)) << error.toStdString ();
    EXPECT_EQ (original.targetDevice, restored.targetDevice);
}
