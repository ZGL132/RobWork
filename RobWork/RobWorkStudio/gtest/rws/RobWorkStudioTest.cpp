/********************************************************************************
 * Copyright 2016 The Robotics Group, The Maersk Mc-Kinney Moller Institute,
 * Faculty of Engineering, University of Southern Denmark
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 ********************************************************************************/

#include <rw/core/PropertyMap.hpp>
#include <rws/CallbackProjectDocumentProvider.hpp>
#include <rws/ProjectManifestJson.hpp>
#include <rws/ProjectManager.hpp>
#include <rws/RobWorkStudio.hpp>
#include <rws/RobWorkStudioPlugin.hpp>
#include <rwslibs/rwstudioapp/RobWorkStudioApp.hpp>

#include "../TestEnvironment.hpp"

#include <QApplication>
#include <QAbstractButton>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QMessageBox>
#include <QMetaObject>
#include <QProcess>
#include <QString>
#include <QTemporaryDir>
#include <QTimer>
#include <gtest/gtest.h>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include <algorithm>
#include <stdexcept>

using namespace rw::core;
using namespace rw::common;
using namespace rws;

namespace {

QString createEmptyProject (const QString& directoryPath)
{
    // 生命周期测试只需要一个没有入口资源的合法项目：空项目可避免依赖外部测试数据，
    // 同时仍会建立 ProjectManager 与文档注册表上下文，足以验证独立 WorkCell 操作
    // 是否真正解除项目绑定，而不是只替换三维视图中的内存对象。
    rws::ProjectManifest manifest;
    manifest.project.id   = QStringLiteral ("standalone-lifecycle-project");
    manifest.project.name = QStringLiteral ("StandaloneLifecycleProject");

    const QString projectFile = QDir (directoryPath).filePath ("Lifecycle.rwproj");
    rws::ProjectManager manager;
    QString error;
    EXPECT_TRUE (manager.createProject (projectFile, manifest, &error))
        << error.toStdString ();
    manager.closeProject ();
    return projectFile;
}

// 项目上下文门控的测试插件：按 required 参数生成两种插件——
//   需要项目上下文的"业务型"插件(ProjectContextBusinessPlugin)，
//   与不依赖项目的"通用型"插件(ProjectContextGeneralPlugin)。
class ProjectContextTestPlugin : public RobWorkStudioPlugin
{
  public:
    // 构造：required 为 true 时在基类初始化后声明"需要项目上下文"，
    // 供测试验证主窗口的门控禁用/隐藏与恢复逻辑。
    explicit ProjectContextTestPlugin (bool required) :
        RobWorkStudioPlugin (required ? QStringLiteral ("ProjectContextBusinessPlugin")
                                      : QStringLiteral ("ProjectContextGeneralPlugin"),
                             QIcon ())
    {
        if (required)
            setRequiresProjectContext (true);
    }
};

QString createMinimalUrdf (const QString& directoryPath)
{
    const QString urdf = QDir (directoryPath).filePath ("source/TestRobot.urdf");
    EXPECT_TRUE (QDir ().mkpath (QFileInfo (urdf).absolutePath ()))
        << urdf.toStdString ();
    QFile file (urdf);
    EXPECT_TRUE (file.open (QIODevice::WriteOnly | QIODevice::Truncate));
    const QByteArray bytes ("<robot name=\"TestRobot\"><link name=\"base\"/></robot>\n");
    EXPECT_EQ (bytes.size (), file.write (bytes));
    return urdf;
}

std::vector< std::string > recentProjects (RobWorkStudio& studio)
{
    return studio.getSettings ().get< std::vector< std::string > > (
        "LastOpennedFiles", std::vector< std::string > ());
}

void setNoopNewRobotProjectStateCallbacks (NewRobotProjectCallbacks& callbacks)
{
    callbacks.snapshotState = [] (QByteArray& snapshot, QString*) {
        snapshot.clear ();
        return true;
    };
    callbacks.restoreState = [] (const QByteArray&, QString*) { return true; };
}

QStringList treeMetadataSnapshot (const QString& root)
{
    QStringList entries;
    const QDir rootDirectory (root);
    QDirIterator iterator (root,
                           QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden |
                               QDir::System,
                           QDirIterator::Subdirectories);
    while (iterator.hasNext ()) {
        const QFileInfo info (iterator.next ());
        entries.push_back (
            QStringLiteral ("%1|%2|%3")
                .arg (QDir::fromNativeSeparators (
                          rootDirectory.relativeFilePath (info.absoluteFilePath ())))
                .arg (info.isDir () ? QStringLiteral ("directory") : QStringLiteral ("file"))
                .arg (info.isFile () ? info.size () : 0));
    }
    std::sort (entries.begin (), entries.end ());
    return entries;
}

enum class BaselineLimitCase { FileCount, TotalBytes, Depth };

void expectNewRobotProjectRejectsBaselineLimitBeforeSnapshots (BaselineLimitCase limitCase)
{
    int argc = 1;
    char name[] = "RobWorkStudio";
    char* argv[1] = {name};
    QApplication app (argc, argv);
    PropertyMap map;
    RobWorkStudio studio (map);
    QTemporaryDir target;
    ASSERT_TRUE (target.isValid ());

    if (limitCase == BaselineLimitCase::FileCount) {
        const QString files = target.filePath (QStringLiteral ("baseline-files"));
        ASSERT_TRUE (QDir ().mkpath (files));
        for (int index = 0; index < 1025; ++index) {
            QFile file (QDir (files).filePath (QStringLiteral ("%1.bin").arg (index)));
            ASSERT_TRUE (file.open (QIODevice::WriteOnly));
        }
    }
    else if (limitCase == BaselineLimitCase::TotalBytes) {
        QFile file (target.filePath (QStringLiteral ("oversized-baseline.bin")));
        ASSERT_TRUE (file.open (QIODevice::WriteOnly));
        ASSERT_TRUE (file.resize (64LL * 1024 * 1024 + 1));
    }
    else {
        QString directory = target.path ();
        for (int depth = 0; depth < 33; ++depth)
            directory = QDir (directory).filePath (QStringLiteral ("d"));
        ASSERT_TRUE (QDir ().mkpath (directory));
    }

    const QStringList before = treeMetadataSnapshot (target.path ());
    ProjectResource model;
    model.id = QStringLiteral ("robot-model.main");
    model.kind = QStringLiteral ("robwork.robot-model");
    model.path = QStringLiteral ("generated/robot-models/Bounded.rmb.json");
    model.ownership = QStringLiteral ("generated");
    model.required = true;
    CallbackProjectDocumentProvider provider (
        QStringLiteral ("test.bounded-baseline-provider"), model.kind,
        [] (const QString&, const ProjectDocumentContext&, QString*) { return true; },
        CallbackProjectDocumentProvider::SaveHandler ());
    QString error;
    ASSERT_TRUE (studio.registerProjectDocumentProvider (&provider, &error))
        << error.toStdString ();

    int snapshotCount = 0;
    int restoreCount = 0;
    int confirmCount = 0;
    int bootstrapCount = 0;
    NewRobotProjectCallbacks callbacks;
    callbacks.preflight = [] (const QString&, QString*) { return true; };
    callbacks.requiredResources = [model] (const QString&, QVector< ProjectResource >& resources,
                                           QString*) {
        resources.push_back (model);
        return true;
    };
    callbacks.snapshotState = [&] (QByteArray&, QString*) {
        ++snapshotCount;
        return true;
    };
    callbacks.restoreState = [&] (const QByteArray&, QString*) {
        ++restoreCount;
        return true;
    };
    callbacks.confirmClose = [&] (QString*) {
        ++confirmCount;
        return true;
    };
    callbacks.bootstrap = [&] (const QString&, QString*) {
        ++bootstrapCount;
        return true;
    };

    EXPECT_FALSE (studio.createProjectWithRobotModelBuilderPaths (
        target.filePath (QStringLiteral ("Bounded.rwproj")), callbacks, &error));
    EXPECT_TRUE (error.contains (QStringLiteral ("new or empty project directory"),
                                 Qt::CaseInsensitive))
        << error.toStdString ();
    EXPECT_EQ (0, snapshotCount);
    EXPECT_EQ (0, restoreCount);
    EXPECT_EQ (0, confirmCount);
    EXPECT_EQ (0, bootstrapCount);
    EXPECT_EQ (before, treeMetadataSnapshot (target.path ())) << error.toStdString ();
}

void expectNewRobotProjectRejectsInvalidSecondaryPath (const QString& secondaryPath)
{
    int argc = 1;
    char name[] = "RobWorkStudio";
    char* argv[1] = {name};
    QApplication app (argc, argv);
    PropertyMap map;
    RobWorkStudio studio (map);
    QTemporaryDir target;
    ASSERT_TRUE (target.isValid ());

    const QString modelKind = QStringLiteral ("robwork.robot-model");
    const QString helperKind = QStringLiteral ("test.new-project-secondary-path");
    CallbackProjectDocumentProvider modelProvider (
        QStringLiteral ("test.new-project-secondary-path-model-provider"), modelKind,
        [] (const QString&, const ProjectDocumentContext&, QString*) { return true; },
        CallbackProjectDocumentProvider::SaveHandler ());
    CallbackProjectDocumentProvider helperProvider (
        QStringLiteral ("test.new-project-secondary-path-provider"), helperKind,
        [] (const QString&, const ProjectDocumentContext&, QString*) { return true; },
        CallbackProjectDocumentProvider::SaveHandler ());
    QString error;
    ASSERT_TRUE (studio.registerProjectDocumentProvider (&modelProvider, &error))
        << error.toStdString ();
    ASSERT_TRUE (studio.registerProjectDocumentProvider (&helperProvider, &error))
        << error.toStdString ();

    ProjectResource model;
    model.id = QStringLiteral ("robot-model.main");
    model.kind = modelKind;
    model.path = QStringLiteral ("generated/robot-models/Model.rmb.json");
    model.ownership = QStringLiteral ("generated");
    model.required = true;
    ProjectResource helper;
    helper.id = QStringLiteral ("candidate.helper");
    helper.kind = helperKind;
    helper.path = secondaryPath;
    helper.ownership = QStringLiteral ("generated");
    helper.required = true;

    int snapshotCount = 0;
    int restoreCount = 0;
    int confirmCount = 0;
    int bootstrapCount = 0;
    NewRobotProjectCallbacks callbacks;
    callbacks.preflight = [] (const QString&, QString*) { return true; };
    callbacks.requiredResources = [model, helper] (const QString&,
                                                   QVector< ProjectResource >& resources,
                                                   QString*) {
        resources = {model, helper};
        return true;
    };
    callbacks.snapshotState = [&] (QByteArray&, QString*) {
        ++snapshotCount;
        return true;
    };
    callbacks.restoreState = [&] (const QByteArray&, QString*) {
        ++restoreCount;
        return true;
    };
    callbacks.confirmClose = [&] (QString*) {
        ++confirmCount;
        return true;
    };
    callbacks.bootstrap = [&] (const QString&, QString*) {
        ++bootstrapCount;
        return true;
    };

    const QString candidateRoot = target.filePath (QStringLiteral ("candidate/deep"));
    const QString projectFile = QDir (candidateRoot).filePath (QStringLiteral ("Robot.rwproj"));
    EXPECT_FALSE (studio.createProjectWithRobotModelBuilderPaths (projectFile, callbacks, &error));
    EXPECT_FALSE (error.isEmpty ());
    EXPECT_EQ (0, snapshotCount);
    EXPECT_EQ (0, restoreCount);
    EXPECT_EQ (0, confirmCount);
    EXPECT_EQ (0, bootstrapCount);
    EXPECT_FALSE (QFileInfo::exists (candidateRoot));
    EXPECT_FALSE (QFileInfo::exists (projectFile));
}

bool runWindowsCommand (const QString& command)
{
#ifdef Q_OS_WIN
    QProcess process;
    process.setProgram (QStringLiteral ("cmd.exe"));
    process.setNativeArguments (QStringLiteral ("/D /C ") + command);
    process.start ();
    return process.waitForFinished () && process.exitStatus () == QProcess::NormalExit &&
        process.exitCode () == 0;
#else
    Q_UNUSED (command);
    return false;
#endif
}

bool createDirectoryJunction (const QString& linkPath, const QString& targetPath)
{
#ifdef Q_OS_WIN
    return runWindowsCommand (
        QStringLiteral ("mklink /J \"%1\" \"%2\"")
            .arg (QDir::toNativeSeparators (linkPath), QDir::toNativeSeparators (targetPath)));
#else
    Q_UNUSED (linkPath);
    Q_UNUSED (targetPath);
    return false;
#endif
}

bool createDirectorySymlink (const QString& linkPath, const QString& targetPath)
{
#ifdef Q_OS_WIN
    return runWindowsCommand (
        QStringLiteral ("mklink /D \"%1\" \"%2\"")
            .arg (QDir::toNativeSeparators (linkPath), QDir::toNativeSeparators (targetPath)));
#else
    return QFile::link (targetPath, linkPath);
#endif
}

class DirectoryJunctionCleanup
{
  public:
    explicit DirectoryJunctionCleanup (const QString& path) : _path (path) {}
    ~DirectoryJunctionCleanup ()
    {
#ifdef Q_OS_WIN
        runWindowsCommand (QStringLiteral ("rmdir \"%1\"")
                               .arg (QDir::toNativeSeparators (_path)));
#else
        QFile::remove (_path);
#endif
    }

  private:
    QString _path;
};

class DirectoryTreeCleanup
{
  public:
    explicit DirectoryTreeCleanup (const QString& path) : _path (path) {}
    ~DirectoryTreeCleanup ()
    {
        if (QFileInfo::exists (_path))
            QDir (_path).removeRecursively ();
    }

  private:
    QString _path;
};

class DanglingSymlinkCleanup
{
  public:
    explicit DanglingSymlinkCleanup (const QString& path) : _path (path) {}
    ~DanglingSymlinkCleanup ()
    {
        if (QFileInfo (_path).isSymLink ())
            QFile::remove (_path);
    }

  private:
    QString _path;
};

enum class BaselineMutationCase {
    OverwriteThenFail,
    DeleteThenFail,
    JunctionThenFail,
    RootDanglingSymlinkThenFail,
    ModifyThenSucceed
};

void expectNewRobotProjectRestoresUndeclaredBaseline (BaselineMutationCase mutation)
{
    int argc = 1;
    char name[] = "RobWorkStudio";
    char* argv[1] = {name};
    QApplication app (argc, argv);
    PropertyMap map;
    RobWorkStudio studio (map);
    QTemporaryDir current;
    QTemporaryDir target;
    QTemporaryDir external;
    ASSERT_TRUE (current.isValid () && target.isValid () && external.isValid ());
    studio.openFile (createEmptyProject (current.path ()).toStdString ());
    const std::vector< std::string > recentBefore = recentProjects (studio);

    QByteArray overwrittenBytes ("undeclared overwrite sentinel\0", 30);
    overwrittenBytes.append ("binary suffix");
    const QByteArray deletedBytes ("undeclared deleted sentinel");
    const QString overwrittenPath =
        target.filePath (QStringLiteral ("baseline/overwrite/sentinel.bin"));
    const QString deletedDirectory = target.filePath (QStringLiteral ("baseline/deleted/nested"));
    const QString deletedPath = QDir (deletedDirectory).filePath (QStringLiteral ("sentinel.bin"));
    ASSERT_TRUE (QDir ().mkpath (QFileInfo (overwrittenPath).absolutePath ()));
    ASSERT_TRUE (QDir ().mkpath (deletedDirectory));
    QFile overwritten (overwrittenPath);
    QFile deleted (deletedPath);
    ASSERT_TRUE (overwritten.open (QIODevice::WriteOnly));
    ASSERT_EQ (overwrittenBytes.size (), overwritten.write (overwrittenBytes));
    overwritten.close ();
    ASSERT_TRUE (deleted.open (QIODevice::WriteOnly));
    ASSERT_EQ (deletedBytes.size (), deleted.write (deletedBytes));
    deleted.close ();
    const QByteArray externalBytes ("external junction target");
    const QString externalPath = external.filePath (QStringLiteral ("outside.txt"));
    QFile externalFile (externalPath);
    ASSERT_TRUE (externalFile.open (QIODevice::WriteOnly));
    ASSERT_EQ (externalBytes.size (), externalFile.write (externalBytes));
    externalFile.close ();
    const QString junctionPath = mutation == BaselineMutationCase::RootDanglingSymlinkThenFail
                                     ? target.path ()
                                     : deletedDirectory;
    DirectoryJunctionCleanup junctionCleanup (junctionPath);
    bool junctionCreated = false;

    ProjectResource model;
    model.id = QStringLiteral ("robot-model.main");
    model.kind = QStringLiteral ("robwork.robot-model");
    model.path = QStringLiteral ("generated/robot-models/BaselineMutation.rmb.json");
    model.ownership = QStringLiteral ("generated");
    model.required = true;
    CallbackProjectDocumentProvider provider (
        QStringLiteral ("test.baseline-mutation-provider"), model.kind,
        [] (const QString&, const ProjectDocumentContext&, QString*) { return true; },
        CallbackProjectDocumentProvider::SaveHandler ());
    QString error;
    ASSERT_TRUE (studio.registerProjectDocumentProvider (&provider, &error))
        << error.toStdString ();

    NewRobotProjectCallbacks callbacks;
    setNoopNewRobotProjectStateCallbacks (callbacks);
    callbacks.preflight = [] (const QString&, QString*) { return true; };
    callbacks.requiredResources = [model] (const QString&, QVector< ProjectResource >& resources,
                                           QString*) {
        resources.push_back (model);
        return true;
    };
    callbacks.confirmClose = [] (QString*) { return true; };
    callbacks.bootstrap = [&] (const QString&, QString* callbackError) {
        if (mutation == BaselineMutationCase::RootDanglingSymlinkThenFail) {
            if (!QDir (target.path ()).removeRecursively ())
                return false;
            junctionCreated = createDirectorySymlink (
                target.path (), external.filePath (QStringLiteral ("missing-target")));
            if (!junctionCreated)
                return false;
            if (callbackError != nullptr)
                *callbackError =
                    QStringLiteral ("Intentional bootstrap failure after project root replacement.");
            return false;
        }
        if (mutation == BaselineMutationCase::OverwriteThenFail ||
            mutation == BaselineMutationCase::ModifyThenSucceed) {
            QFile replacement (overwrittenPath);
            if (!replacement.open (QIODevice::WriteOnly | QIODevice::Truncate) ||
                replacement.write ("candidate overwrite") != 19)
                return false;
            replacement.close ();
        }
        if (mutation != BaselineMutationCase::OverwriteThenFail) {
            if (!QFile::remove (deletedPath) ||
                !QDir ().rmdir (deletedDirectory))
                return false;
            if (mutation == BaselineMutationCase::JunctionThenFail) {
                junctionCreated = createDirectoryJunction (deletedDirectory, external.path ());
                if (!junctionCreated)
                    return false;
            }
        }
        if (mutation != BaselineMutationCase::ModifyThenSucceed) {
            if (callbackError != nullptr)
                *callbackError = QStringLiteral ("Intentional bootstrap failure after baseline mutation.");
            return false;
        }

        bool created = false;
        if (!studio.ensureGeneratedProjectResource (model, &created, callbackError))
            return false;
        provider.adoptGeneratedResource (model.id);
        provider.markDirty ();
        studio.notifyProjectDocumentChanged ();
        return true;
    };

    const QString targetProject = target.filePath (QStringLiteral ("BaselineMutation.rwproj"));
    EXPECT_FALSE (studio.createProjectWithRobotModelBuilderPaths (
        targetProject, callbacks, &error)) << error.toStdString ();
    EXPECT_FALSE (error.isEmpty ());
    EXPECT_FALSE (QFileInfo::exists (targetProject)) << error.toStdString ();
    EXPECT_FALSE (QFileInfo::exists (target.filePath (QStringLiteral ("generated"))));
    if (mutation == BaselineMutationCase::JunctionThenFail)
        EXPECT_TRUE (junctionCreated);
    if (mutation == BaselineMutationCase::RootDanglingSymlinkThenFail)
        EXPECT_FALSE (junctionCreated);
    EXPECT_FALSE (QFileInfo (target.path ()).isSymLink ());
    EXPECT_TRUE (QFileInfo (target.path ()).isDir ());
    EXPECT_TRUE (QFileInfo (deletedDirectory).isDir ());
    EXPECT_FALSE (QFileInfo (deletedDirectory).isSymLink ());

    QFile overwrittenAfter (overwrittenPath);
    QFile deletedAfter (deletedPath);
    ASSERT_TRUE (overwrittenAfter.open (QIODevice::ReadOnly));
    EXPECT_EQ (overwrittenBytes, overwrittenAfter.readAll ());
    ASSERT_TRUE (deletedAfter.open (QIODevice::ReadOnly));
    EXPECT_EQ (deletedBytes, deletedAfter.readAll ());
    QFile externalAfter (externalPath);
    ASSERT_TRUE (externalAfter.open (QIODevice::ReadOnly));
    EXPECT_EQ (externalBytes, externalAfter.readAll ());
    EXPECT_EQ (QDir::cleanPath (current.path ()), QDir::cleanPath (studio.projectDirectory ()));
    EXPECT_EQ (recentBefore, recentProjects (studio));
}

enum class CandidateFailureCase {
    RollbackInventoryOverflow,
    BootstrapException,
    RestoreStateException,
    ProjectRootRenameAttack,
    ProjectRootIdentityChange,
    ConfirmCloseRootRenameAttack,
    CandidateTreeNestedJunction
};

void expectNewRobotProjectFailureRestoresFullState (CandidateFailureCase failureCase)
{
    int argc = 1;
    char name[] = "RobWorkStudio";
    char* argv[1] = {name};
    QApplication app (argc, argv);
    PropertyMap map;
    RobWorkStudio studio (map);
    QTemporaryDir current;
    QTemporaryDir target;
    QTemporaryDir external;
    ASSERT_TRUE (current.isValid () && target.isValid () && external.isValid ());
    const QString movedRoot = target.path () + QStringLiteral ("-moved");
    DirectoryTreeCleanup movedRootCleanup (movedRoot);
    DanglingSymlinkCleanup rootSymlinkCleanup (target.path ());
    const QString candidateOnlyTree =
        target.filePath (QStringLiteral ("candidate-only/nested"));
    const QString nestedJunction =
        QDir (candidateOnlyTree).filePath (QStringLiteral ("external-link"));
    DirectoryTreeCleanup candidateOnlyTreeCleanup (
        target.filePath (QStringLiteral ("candidate-only")));
    DirectoryJunctionCleanup nestedJunctionCleanup (nestedJunction);
    ASSERT_FALSE (QFileInfo::exists (movedRoot));

    const QByteArray externalBytes ("external root sentinel");
    const QString externalPath = external.filePath (QStringLiteral ("sentinel.bin"));
    QFile externalFile (externalPath);
    ASSERT_TRUE (externalFile.open (QIODevice::WriteOnly));
    ASSERT_EQ (externalBytes.size (), externalFile.write (externalBytes));
    externalFile.close ();

    const QString currentDocument = current.filePath (QStringLiteral ("documents/old.json"));
    ASSERT_TRUE (QDir ().mkpath (QFileInfo (currentDocument).absolutePath ()))
        << currentDocument.toStdString ();
    QFile currentFile (currentDocument);
    ASSERT_TRUE (currentFile.open (QIODevice::WriteOnly));
    ASSERT_EQ (3, currentFile.write ("old"));
    currentFile.close ();

    ProjectResource oldResource;
    oldResource.id = QStringLiteral ("old.document");
    oldResource.kind = QStringLiteral ("test.failure-boundary-old");
    oldResource.path = QStringLiteral ("documents/old.json");
    oldResource.ownership = QStringLiteral ("project");
    oldResource.required = true;
    ProjectManifest oldManifest;
    oldManifest.project.id = QStringLiteral ("failure-boundary-old-project");
    oldManifest.project.name = QStringLiteral ("FailureBoundaryOldProject");
    oldManifest.resources.push_back (oldResource);
    const QString currentProject = current.filePath (QStringLiteral ("Old.rwproj"));
    ProjectManager manager;
    QString error;
    ASSERT_TRUE (manager.createProject (currentProject, oldManifest, &error))
        << error.toStdString ();
    manager.closeProject ();

    int oldLoadCount = 0;
    int oldCloseCount = 0;
    QString oldProviderState;
    CallbackProjectDocumentProvider oldProvider (
        QStringLiteral ("test.failure-boundary-old-provider"), oldResource.kind,
        [&] (const QString&, const ProjectDocumentContext&, QString*) {
            ++oldLoadCount;
            oldProviderState = QStringLiteral ("loaded-from-disk");
            return true;
        },
        CallbackProjectDocumentProvider::SaveHandler (),
        CallbackProjectDocumentProvider::CanCloseHandler (),
        [&] () {
            ++oldCloseCount;
            oldProviderState.clear ();
        });
    ASSERT_TRUE (studio.registerProjectDocumentProvider (&oldProvider, &error))
        << error.toStdString ();
    studio.openFile (currentProject.toStdString ());
    ASSERT_EQ (1, oldLoadCount);
    oldProviderState = QStringLiteral ("unsaved in-memory edit");
    oldProvider.markDirty ();
    studio.notifyProjectDocumentChanged ();
    ASSERT_TRUE (studio.hasUnsavedProjectChanges ());
    const QString windowTitleBefore = studio.windowTitle ();
    const std::vector< std::string > recentBefore = recentProjects (studio);

    const QByteArray overwrittenBytes ("baseline overwrite bytes\0suffix", 31);
    const QByteArray deletedBytes ("baseline deleted bytes");
    const QString overwrittenPath =
        target.filePath (QStringLiteral ("baseline/overwrite/sentinel.bin"));
    const QString deletedDirectory =
        target.filePath (QStringLiteral ("baseline/deleted/nested"));
    const QString deletedPath = QDir (deletedDirectory).filePath (QStringLiteral ("sentinel.bin"));
    ASSERT_TRUE (QDir ().mkpath (QFileInfo (overwrittenPath).absolutePath ()))
        << overwrittenPath.toStdString ();
    ASSERT_TRUE (QDir ().mkpath (deletedDirectory)) << deletedDirectory.toStdString ();
    QFile overwritten (overwrittenPath);
    QFile deleted (deletedPath);
    ASSERT_TRUE (overwritten.open (QIODevice::WriteOnly));
    ASSERT_EQ (overwrittenBytes.size (), overwritten.write (overwrittenBytes));
    overwritten.close ();
    ASSERT_TRUE (deleted.open (QIODevice::WriteOnly));
    ASSERT_EQ (deletedBytes.size (), deleted.write (deletedBytes));
    deleted.close ();
    const QStringList targetBefore = treeMetadataSnapshot (target.path ());

    ProjectResource candidate;
    candidate.id = QStringLiteral ("robot-model.main");
    candidate.kind = QStringLiteral ("robwork.robot-model");
    candidate.path = QStringLiteral ("generated/robot-models/Candidate.rmb.json");
    candidate.ownership = QStringLiteral ("generated");
    candidate.required = true;
    int candidateCloseCount = 0;
    CallbackProjectDocumentProvider candidateProvider (
        QStringLiteral ("test.failure-boundary-candidate-provider"), candidate.kind,
        [] (const QString&, const ProjectDocumentContext&, QString*) { return true; },
        CallbackProjectDocumentProvider::SaveHandler (),
        CallbackProjectDocumentProvider::CanCloseHandler (),
        [&] () { ++candidateCloseCount; });
    ASSERT_TRUE (studio.registerProjectDocumentProvider (&candidateProvider, &error))
        << error.toStdString ();

    QString builderState = QStringLiteral ("previous builder state");
    bool rootRenameAttempted = false;
    bool rootRenameSucceeded = false;
    bool originalRootOccupied = false;
    bool nestedJunctionCreated = false;
    unsigned long rootRenameError = 0;
    unsigned long rootOccupationError = 0;
    NewRobotProjectCallbacks callbacks;
    callbacks.preflight = [] (const QString&, QString*) { return true; };
    callbacks.requiredResources = [candidate] (const QString&,
                                               QVector< ProjectResource >& resources,
                                               QString*) {
        resources.push_back (candidate);
        return true;
    };
    callbacks.snapshotState = [&] (QByteArray& snapshot, QString*) {
        snapshot = builderState.toUtf8 ();
        return true;
    };
    callbacks.restoreState = [&] (const QByteArray& snapshot, QString*) {
        if (failureCase == CandidateFailureCase::RestoreStateException)
            throw std::runtime_error ("intentional restoreState exception");
        builderState = QString::fromUtf8 (snapshot);
        return true;
    };
    callbacks.confirmClose = [&] (QString* callbackError) {
        if (failureCase != CandidateFailureCase::ConfirmCloseRootRenameAttack)
            return true;
#ifdef Q_OS_WIN
        rootRenameAttempted = true;
        const std::wstring nativeProjectRoot =
            QDir::toNativeSeparators (target.path ()).toStdWString ();
        const std::wstring nativeMovedRoot =
            QDir::toNativeSeparators (movedRoot).toStdWString ();
        rootRenameSucceeded =
            MoveFileExW (nativeProjectRoot.c_str (), nativeMovedRoot.c_str (),
                         MOVEFILE_WRITE_THROUGH) != FALSE;
        if (!rootRenameSucceeded) {
            rootRenameError = GetLastError ();
            return true;
        }

        originalRootOccupied =
            CreateDirectoryW (nativeProjectRoot.c_str (), nullptr) != FALSE;
        if (!originalRootOccupied) {
            rootOccupationError = GetLastError ();
            return false;
        }
        if (!QDir ().mkpath (QFileInfo (overwrittenPath).absolutePath ()) ||
            !QDir ().mkpath (deletedDirectory))
            return false;
        QFile replacementOverwritten (overwrittenPath);
        QFile replacementDeleted (deletedPath);
        if (!replacementOverwritten.open (QIODevice::WriteOnly) ||
            replacementOverwritten.write (overwrittenBytes) != overwrittenBytes.size () ||
            !replacementDeleted.open (QIODevice::WriteOnly) ||
            replacementDeleted.write (deletedBytes) != deletedBytes.size ()) {
            if (callbackError != nullptr)
                *callbackError = QStringLiteral ("Could not build the ordinary replacement root.");
            return false;
        }
#else
        Q_UNUSED (callbackError);
#endif
        return true;
    };
    callbacks.bootstrap = [&] (const QString& projectRoot, QString* callbackError) {
        builderState = QStringLiteral ("candidate builder state");
        QFile replacement (overwrittenPath);
        if (!replacement.open (QIODevice::WriteOnly | QIODevice::Truncate) ||
            replacement.write ("candidate overwrite") != 19 ||
            !QFile::remove (deletedPath) || !QDir ().rmdir (deletedDirectory)) {
            return false;
        }
        replacement.close ();
        bool created = false;
        if (!studio.ensureGeneratedProjectResource (candidate, &created, callbackError))
            return false;
        candidateProvider.adoptGeneratedResource (candidate.id);

        if (failureCase == CandidateFailureCase::RollbackInventoryOverflow) {
            const QString overflowRoot = QDir (projectRoot).filePath (QStringLiteral ("overflow"));
            if (!QDir ().mkpath (overflowRoot))
                return false;
            for (int index = 0; index < 4097; ++index) {
                QFile file (QDir (overflowRoot).filePath (
                    QStringLiteral ("%1.tmp").arg (index, 4, 10, QLatin1Char ('0'))));
                if (!file.open (QIODevice::WriteOnly))
                    return false;
            }
            if (callbackError != nullptr)
                *callbackError = QStringLiteral ("Intentional failure after candidate overflow.");
            return false;
        }

        if (failureCase == CandidateFailureCase::ProjectRootRenameAttack) {
#ifdef Q_OS_WIN
            rootRenameAttempted = true;
            const std::wstring nativeProjectRoot =
                QDir::toNativeSeparators (projectRoot).toStdWString ();
            const std::wstring nativeMovedRoot =
                QDir::toNativeSeparators (movedRoot).toStdWString ();
            rootRenameSucceeded =
                MoveFileExW (nativeProjectRoot.c_str (), nativeMovedRoot.c_str (),
                             MOVEFILE_WRITE_THROUGH) != FALSE;
            if (!rootRenameSucceeded)
                rootRenameError = GetLastError ();
            else {
                originalRootOccupied =
                    CreateDirectoryW (nativeProjectRoot.c_str (), nullptr) != FALSE;
                if (!originalRootOccupied)
                    rootOccupationError = GetLastError ();
            }
            if (callbackError != nullptr) {
                *callbackError =
                    QStringLiteral (
                        "Intentional bootstrap failure after project-root rename attempt "
                        "(renameError=%1, occupationError=%2).")
                        .arg (rootRenameError)
                        .arg (rootOccupationError);
            }
#else
            Q_UNUSED (projectRoot);
            Q_UNUSED (callbackError);
#endif
            return false;
        }

        if (failureCase == CandidateFailureCase::ProjectRootIdentityChange) {
#ifndef Q_OS_WIN
            if (!QDir (target.path ()).removeRecursively ())
                return false;
            const QString missingTarget =
                external.filePath (QStringLiteral ("missing-project-root"));
            rootRenameAttempted = true;
            rootRenameSucceeded = QFile::link (missingTarget, target.path ());
            if (callbackError != nullptr) {
                *callbackError = QStringLiteral (
                    "Intentional bootstrap failure after POSIX project-root identity change.");
            }
#endif
            return false;
        }

        if (failureCase == CandidateFailureCase::CandidateTreeNestedJunction) {
#ifdef Q_OS_WIN
            if (!QDir ().mkpath (candidateOnlyTree))
                return false;
            nestedJunctionCreated =
                createDirectoryJunction (nestedJunction, external.path ());
            if (!nestedJunctionCreated)
                return false;
            if (callbackError != nullptr) {
                *callbackError = QStringLiteral (
                    "Intentional bootstrap failure with a nested candidate junction.");
            }
#endif
            return false;
        }

        if (failureCase == CandidateFailureCase::BootstrapException)
            throw std::runtime_error ("intentional bootstrap exception");
        if (callbackError != nullptr)
            *callbackError = QStringLiteral ("Intentional failure before restoreState exception.");
        return false;
    };

    const QString targetProject = target.filePath (QStringLiteral ("Candidate.rwproj"));
    bool created = true;
    EXPECT_NO_THROW (created = studio.createProjectWithRobotModelBuilderPaths (
                         targetProject, callbacks, &error));
    EXPECT_FALSE (created);
    if (failureCase == CandidateFailureCase::ProjectRootRenameAttack) {
        EXPECT_TRUE (rootRenameAttempted);
        EXPECT_FALSE (rootRenameSucceeded)
            << "The project root was renamed while the new-project transaction was active; "
               "MoveFileExW error="
            << rootRenameError;
        if (rootRenameSucceeded)
            EXPECT_TRUE (originalRootOccupied) << "CreateDirectoryW error=" << rootOccupationError;
        EXPECT_FALSE (QFileInfo::exists (movedRoot));
    }
    if (failureCase == CandidateFailureCase::ProjectRootIdentityChange) {
        EXPECT_TRUE (rootRenameAttempted);
        EXPECT_TRUE (rootRenameSucceeded);
        EXPECT_TRUE (QFileInfo (target.path ()).isSymLink ());
        EXPECT_TRUE (error.contains (QStringLiteral ("identity"), Qt::CaseInsensitive))
            << error.toStdString ();
        EXPECT_TRUE (error.contains (QStringLiteral ("rollback was skipped"),
                                     Qt::CaseInsensitive))
            << error.toStdString ();
    }
    if (failureCase == CandidateFailureCase::ConfirmCloseRootRenameAttack) {
        EXPECT_TRUE (rootRenameAttempted);
        EXPECT_FALSE (rootRenameSucceeded)
            << "The existing project root was renamed during confirmClose; MoveFileExW error="
            << rootRenameError;
        if (rootRenameSucceeded)
            EXPECT_TRUE (originalRootOccupied) << "CreateDirectoryW error=" << rootOccupationError;
        EXPECT_FALSE (QFileInfo::exists (movedRoot));
    }
    if (failureCase == CandidateFailureCase::CandidateTreeNestedJunction) {
        if (!nestedJunctionCreated)
            GTEST_SKIP () << "The test process cannot create a Windows directory junction.";
        EXPECT_FALSE (QFileInfo::exists (
            target.filePath (QStringLiteral ("candidate-only")))) << error.toStdString ();
    }
    if (failureCase == CandidateFailureCase::BootstrapException) {
        EXPECT_TRUE (error.contains (QStringLiteral ("bootstrap"), Qt::CaseInsensitive))
            << error.toStdString ();
        EXPECT_TRUE (error.contains (QStringLiteral ("exception"), Qt::CaseInsensitive))
            << error.toStdString ();
    }
    else if (failureCase == CandidateFailureCase::RestoreStateException) {
        EXPECT_TRUE (error.contains (QStringLiteral ("restore"), Qt::CaseInsensitive))
            << error.toStdString ();
        EXPECT_TRUE (error.contains (QStringLiteral ("exception"), Qt::CaseInsensitive))
            << error.toStdString ();
    }

    QFile externalAfter (externalPath);
    if (failureCase != CandidateFailureCase::ProjectRootIdentityChange) {
        QFile overwrittenAfter (overwrittenPath);
        QFile deletedAfter (deletedPath);
        ASSERT_TRUE (overwrittenAfter.open (QIODevice::ReadOnly)) << error.toStdString ();
        EXPECT_EQ (overwrittenBytes, overwrittenAfter.readAll ());
        ASSERT_TRUE (deletedAfter.open (QIODevice::ReadOnly)) << error.toStdString ();
        EXPECT_EQ (deletedBytes, deletedAfter.readAll ());
        EXPECT_TRUE (QFileInfo (deletedDirectory).isDir ());
        EXPECT_FALSE (QFileInfo::exists (targetProject));
        EXPECT_FALSE (QFileInfo::exists (target.filePath (QStringLiteral ("overflow"))));
        EXPECT_FALSE (QFileInfo::exists (target.filePath (QStringLiteral ("generated"))));
        EXPECT_EQ (targetBefore, treeMetadataSnapshot (target.path ())) << error.toStdString ();
    }
    ASSERT_TRUE (externalAfter.open (QIODevice::ReadOnly));
    EXPECT_EQ (externalBytes, externalAfter.readAll ());

    EXPECT_EQ (QDir::cleanPath (current.path ()), QDir::cleanPath (studio.projectDirectory ()));
    EXPECT_EQ (1, oldLoadCount);
    EXPECT_EQ (0, oldCloseCount);
    EXPECT_EQ (1, candidateCloseCount);
    EXPECT_EQ (QStringLiteral ("unsaved in-memory edit"), oldProviderState);
    EXPECT_EQ (failureCase == CandidateFailureCase::RestoreStateException
                   ? QStringLiteral ("candidate builder state")
                   : QStringLiteral ("previous builder state"),
               builderState);
    EXPECT_TRUE (studio.hasUnsavedProjectChanges ());
    EXPECT_EQ (windowTitleBefore, studio.windowTitle ());
    QString candidatePath;
    EXPECT_FALSE (studio.resolveProjectResource (candidate.id, candidatePath, nullptr));
    EXPECT_EQ (recentBefore, recentProjects (studio));
}

}    // namespace

// 项目上下文门控生命周期测试：验证需要项目上下文的插件随"打开项目/关闭项目"
// 完整生命周期被禁用、隐藏与自动恢复，而通用插件始终可用。
TEST (RobWorkStudio, ProjectContextPluginGateTracksProjectLifecycle)
{
    int argc = 1;
    char name[] = "RobWorkStudio";
    char* argv[1] = {name};
    QApplication app (argc, argv);
    PropertyMap map;
    RobWorkStudio studio (map);
    // 分别构造一个通用插件与一个需要项目上下文的"业务型"插件。
    ProjectContextTestPlugin* generalPlugin = new ProjectContextTestPlugin (false);
    ProjectContextTestPlugin* businessPlugin = new ProjectContextTestPlugin (true);
    QTemporaryDir projectDirectory;
    ASSERT_TRUE (projectDirectory.isValid ());

    // 首先校验两个插件的项目上下文声明符合构造参数。
    EXPECT_FALSE (generalPlugin->requiresProjectContext ());
    EXPECT_TRUE (businessPlugin->requiresProjectContext ());

    // 注册插件：通用插件默认隐藏，业务插件默认可见。此时尚未打开任何项目。
    studio.addPlugin (generalPlugin, false);
    studio.addPlugin (businessPlugin, true);
    studio.show ();
    QCoreApplication::processEvents ();

    // 无项目状态：通用插件可用；业务插件应被门控禁用且隐藏(即使初始请求可见)。
    EXPECT_TRUE (generalPlugin->visibilityAction ()->isEnabled ());
    EXPECT_FALSE (businessPlugin->visibilityAction ()->isEnabled ());
    EXPECT_FALSE (businessPlugin->isVisible ());

    // 门控拦截：业务插件主动请求显示也被强制保持隐藏。
    businessPlugin->showPlugin ();
    EXPECT_FALSE (businessPlugin->isVisible ());

    // 通用插件不受门控影响，可正常显示。
    generalPlugin->showPlugin ();
    EXPECT_TRUE (generalPlugin->isVisible ());

    // 打开项目(空项目即可)：业务插件应解锁可用并恢复为可见。
    const QString projectFile = createEmptyProject (projectDirectory.path ());
    studio.openFile (projectFile.toStdString ());
    QCoreApplication::processEvents ();
    EXPECT_TRUE (businessPlugin->visibilityAction ()->isEnabled ());
    EXPECT_TRUE (businessPlugin->isVisible ());

    // 关闭项目：业务插件重新被禁用并隐藏，通用插件不受影响。
    ASSERT_TRUE (QMetaObject::invokeMethod (&studio, "closeProject", Qt::DirectConnection));
    EXPECT_FALSE (businessPlugin->visibilityAction ()->isEnabled ());
    EXPECT_FALSE (businessPlugin->isVisible ());
    EXPECT_TRUE (generalPlugin->visibilityAction ()->isEnabled ());

    // 再次打开项目：业务插件再次解锁并恢复可见，验证门控可随生命周期往复。
    studio.openFile (projectFile.toStdString ());
    QCoreApplication::processEvents ();
    EXPECT_TRUE (businessPlugin->visibilityAction ()->isEnabled ());
    EXPECT_TRUE (businessPlugin->isVisible ());
}

TEST (RobWorkStudio, NewRobotProjectRejectsBaselineFileCountBeforeSnapshots)
{
    expectNewRobotProjectRejectsBaselineLimitBeforeSnapshots (BaselineLimitCase::FileCount);
}

TEST (RobWorkStudio, NewRobotProjectMissingBuilderRejectedBeforePathSelection)
{
    std::vector< RobWorkStudioPlugin* > plugins;
    NewRobotProjectCallbacks callbacks;
    RobWorkStudioPlugin* builder = nullptr;
    QString error;
    int confirmCount = 0;

    EXPECT_FALSE (resolveNewRobotProjectBuilderCallbacks (
        plugins,
        [&] (QString*) {
            ++confirmCount;
            return true;
        },
        callbacks, builder, &error));
    EXPECT_TRUE (error.contains (QStringLiteral ("RobotModelBuilder")));
    EXPECT_EQ (nullptr, builder);
    EXPECT_EQ (0, confirmCount);
    EXPECT_FALSE (callbacks.preflight);
    EXPECT_FALSE (callbacks.snapshotState);
    EXPECT_FALSE (callbacks.restoreState);
    EXPECT_FALSE (callbacks.requiredResources);
    EXPECT_FALSE (callbacks.bootstrap);
    EXPECT_FALSE (callbacks.confirmClose);
}

TEST (RobWorkStudio, NewRobotProjectIncompatibleBuilderRejectedBeforePathSelection)
{
    int argc = 1;
    char name[] = "RobWorkStudio";
    char* argv[1] = {name};
    QApplication app (argc, argv);
    RobWorkStudioPlugin incompatible (QStringLiteral ("RobotModelBuilder"), QIcon ());
    std::vector< RobWorkStudioPlugin* > plugins = {&incompatible};
    NewRobotProjectCallbacks callbacks;
    RobWorkStudioPlugin* builder = nullptr;
    QString error;
    int confirmCount = 0;

    EXPECT_FALSE (resolveNewRobotProjectBuilderCallbacks (
        plugins,
        [&] (QString*) {
            ++confirmCount;
            return true;
        },
        callbacks, builder, &error));
    EXPECT_TRUE (error.contains (QStringLiteral ("incompatible"), Qt::CaseInsensitive))
        << error.toStdString ();
    EXPECT_EQ (nullptr, builder);
    EXPECT_EQ (0, confirmCount);
    EXPECT_FALSE (callbacks.preflight);
    EXPECT_FALSE (callbacks.snapshotState);
    EXPECT_FALSE (callbacks.restoreState);
    EXPECT_FALSE (callbacks.requiredResources);
    EXPECT_FALSE (callbacks.bootstrap);
    EXPECT_FALSE (callbacks.confirmClose);
}

TEST (RobWorkStudio, NewRobotProjectRejectsBaselineTotalBytesBeforeSnapshots)
{
    expectNewRobotProjectRejectsBaselineLimitBeforeSnapshots (BaselineLimitCase::TotalBytes);
}

TEST (RobWorkStudio, NewRobotProjectRejectsBaselineDepthBeforeSnapshots)
{
    expectNewRobotProjectRejectsBaselineLimitBeforeSnapshots (BaselineLimitCase::Depth);
}

TEST (RobWorkStudio, NewRobotProjectRollbackRestoresBaselineAfterCandidateEntryOverflow)
{
    expectNewRobotProjectFailureRestoresFullState (
        CandidateFailureCase::RollbackInventoryOverflow);
}

TEST (RobWorkStudio, NewRobotProjectCatchesBootstrapExceptionAndRestoresFullState)
{
    expectNewRobotProjectFailureRestoresFullState (CandidateFailureCase::BootstrapException);
}

TEST (RobWorkStudio, NewRobotProjectCatchesRestoreStateExceptionAndContinuesRestoration)
{
    expectNewRobotProjectFailureRestoresFullState (
        CandidateFailureCase::RestoreStateException);
}

TEST (RobWorkStudio, NewRobotProjectTransactionGuardBlocksProjectRootRenameDuringBootstrap)
{
#ifndef Q_OS_WIN
    GTEST_SKIP () << "Project-root rename blocking is specific to Windows.";
#else
    expectNewRobotProjectFailureRestoresFullState (
        CandidateFailureCase::ProjectRootRenameAttack);
#endif
}

TEST (RobWorkStudio, NewRobotProjectTransactionGuardAnchorsExistingRootBeforeConfirmClose)
{
#ifndef Q_OS_WIN
    GTEST_SKIP () << "Windows transaction handles prevent project-root replacement.";
#else
    expectNewRobotProjectFailureRestoresFullState (
        CandidateFailureCase::ConfirmCloseRootRenameAttack);
#endif
}

TEST (RobWorkStudio, NewRobotProjectRollbackRemovesCandidateTreeContainingNestedJunction)
{
#ifndef Q_OS_WIN
    GTEST_SKIP () << "Directory junction cleanup is specific to Windows.";
#else
    expectNewRobotProjectFailureRestoresFullState (
        CandidateFailureCase::CandidateTreeNestedJunction);
#endif
}

TEST (RobWorkStudio, LaunchTest)
{
    int argc      = 1;
    char name[]   = "RobWorkStudio";
    char* argv[1] = {name};
    PropertyMap map;
    QApplication app (argc, argv);
    RobWorkStudio rwstudio (map);
    rwstudio.show ();
    app.processEvents ();

    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString projectFile = createEmptyProject (directory.path ());

    // “新建空 WorkCell”是从项目工作模式切换到临时单文档模式的显式用户操作。
    // 若只替换 WorkCell 而保留项目上下文，后续状态修改会继续误标记原项目资源，
    // 因此这里固定要求项目名称必须从标题栏消失。
    rwstudio.openFile (projectFile.toStdString ());
    app.processEvents ();
    ASSERT_TRUE (rwstudio.windowTitle ().contains (QStringLiteral ("StandaloneLifecycleProject")));
    ASSERT_TRUE (QMetaObject::invokeMethod (&rwstudio, "newWorkCell", Qt::DirectConnection));
    EXPECT_FALSE (rwstudio.windowTitle ().contains (QStringLiteral ("StandaloneLifecycleProject")));

    // “打开单个资源”加载独立 WorkCell 时同样必须退出当前项目。测试重新打开项目后
    // 再走公开 openFile 入口，防止菜单、拖放和最近文件列表使用不同的生命周期规则。
    rwstudio.openFile (projectFile.toStdString ());
    app.processEvents ();
    ASSERT_TRUE (rwstudio.windowTitle ().contains (QStringLiteral ("StandaloneLifecycleProject")));
    // 复用传感器测试长期使用的有效 WorkCell，避免在本测试中重新构造 XML 夹具时
    // 引入与项目生命周期无关的碰撞配置属性要求。
    const QString standaloneWorkCell = QString::fromStdString (
        TestEnvironment::testfilesDir () + "SensorTest.wc.xml");
    rwstudio.openFile (standaloneWorkCell.toStdString ());
    app.processEvents ();
    EXPECT_FALSE (rwstudio.windowTitle ().contains (QStringLiteral ("StandaloneLifecycleProject")));

    TimerUtil::sleepMs (1000);
    rwstudio.close ();
    TimerUtil::sleepMs (2000);
}

TEST (RobWorkStudio, AutosaveDoesNotRunWhileProjectRecoveryDialogIsOpen)
{
    int argc      = 1;
    char name[]   = "RobWorkStudio";
    char* argv[1] = {name};
    QApplication app (argc, argv);
    PropertyMap map;
    RobWorkStudio rwstudio (map);

    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString firstProject = QDir (directory.path ()).filePath ("first/First.rwproj");
    const QString secondProject = QDir (directory.path ()).filePath ("second/Second.rwproj");
    QString error;
    const auto createProject = [&error] (const QString& projectFile) {
        const QString resourceFile = QDir (QFileInfo (projectFile).absolutePath ()).filePath (
            "data/state.json");
        if (!QDir ().mkpath (QFileInfo (resourceFile).absolutePath ()))
            return false;
        QFile resource (resourceFile);
        if (!resource.open (QIODevice::WriteOnly) || resource.write ("{}") != 2)
            return false;
        resource.close ();

        ProjectManifest manifest;
        manifest.project.id   = QFileInfo (projectFile).baseName ();
        manifest.project.name = manifest.project.id;
        ProjectResource resourceEntry;
        resourceEntry.id        = QStringLiteral ("test.autosave.resource");
        resourceEntry.kind      = QStringLiteral ("test.autosave");
        resourceEntry.path      = QStringLiteral ("data/state.json");
        resourceEntry.ownership = QStringLiteral ("project");
        resourceEntry.required  = true;
        manifest.resources.push_back (resourceEntry);

        ProjectManager manager;
        return manager.createProject (projectFile, manifest, &error);
    };
    ASSERT_TRUE (createProject (firstProject)) << error.toStdString ();
    ASSERT_TRUE (createProject (secondProject)) << error.toStdString ();
    ProjectManager secondManager;
    ASSERT_TRUE (secondManager.openProject (secondProject, &error)) << error.toStdString ();
    ASSERT_TRUE (secondManager.createAutosaveSnapshot (&error)) << error.toStdString ();
    secondManager.closeProject ();

    int autosaveWrites = 0;
    CallbackProjectDocumentProvider provider (
        QStringLiteral ("test.autosave"),
        QStringLiteral ("test.autosave"),
        [] (const QString&, const ProjectDocumentContext&, QString*) { return true; },
        [&autosaveWrites] (const QString& targetPath, const ProjectDocumentContext&, QString*) {
            ++autosaveWrites;
            QFile output (targetPath);
            return output.open (QIODevice::WriteOnly);
        });
    ASSERT_TRUE (rwstudio.registerProjectDocumentProvider (&provider, &error))
        << error.toStdString ();

    CallbackProjectDocumentProvider generatedProvider (
        QStringLiteral ("test.generated"),
        QStringLiteral ("test.generated"),
        [] (const QString&, const ProjectDocumentContext&, QString*) { return true; },
        [] (const QString& targetPath, const ProjectDocumentContext&, QString*) {
            QFile output (targetPath);
            return output.open (QIODevice::WriteOnly);
        });
    ASSERT_TRUE (rwstudio.registerProjectDocumentProvider (&generatedProvider, &error))
        << error.toStdString ();

    rwstudio.openFile (firstProject.toStdString ());
    ProjectResource resource;
    resource.id        = QStringLiteral ("test.generated.resource");
    resource.kind      = QStringLiteral ("test.generated");
    resource.path      = QStringLiteral ("analysis/autosave.json");
    resource.ownership = QStringLiteral ("generated");
    generatedProvider.adoptGeneratedResource (resource.id);
    ASSERT_TRUE (rwstudio.ensureGeneratedProjectResource (resource, nullptr, &error))
        << error.toStdString ();
    ASSERT_TRUE (rwstudio.ensureGeneratedProjectResource (resource, nullptr, &error))
        << error.toStdString ();

    const QList< QTimer* > timers = rwstudio.findChildren< QTimer* > ();
    ASSERT_EQ (1, timers.size ());
    timers.front ()->stop ();
    timers.front ()->setInterval (1);

    QTimer recoveryDismissal;
    recoveryDismissal.setInterval (5);
    QObject::connect (&recoveryDismissal, &QTimer::timeout, &app, [&app] () {
        for (QWidget* widget : QApplication::topLevelWidgets ()) {
            QMessageBox* dialog = qobject_cast< QMessageBox* > (widget);
            if (dialog == nullptr)
                continue;
            for (QAbstractButton* button : dialog->buttons ()) {
                if (dialog->buttonRole (button) == QMessageBox::DestructiveRole) {
                    button->click ();
                    return;
                }
            }
        }
    });
    recoveryDismissal.start ();
    timers.front ()->start ();
    rwstudio.openFile (secondProject.toStdString ());
    recoveryDismissal.stop ();

    EXPECT_EQ (0, autosaveWrites);
    rwstudio.close ();
}

TEST (RobWorkStudio, PromotesGeneratedSceneWithoutChangingMainWorkCellIdentity)
{
    int argc = 1;
    char name[] = "RobWorkStudio";
    char* argv[1] = {name};
    QApplication app (argc, argv);
    PropertyMap map;
    RobWorkStudio rwstudio (map);

    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString projectFile = QDir (directory.path ()).filePath ("Demo.rwproj");
    const QString originalScene = QDir (directory.path ()).filePath ("scenes/main.wc.xml");
    const QString generatedScene =
        QDir (directory.path ()).filePath ("generated/robot-models/RobotScene.wc.xml");
    const QString generatedDevice =
        QDir (directory.path ()).filePath ("generated/robot-models/Robot.wc.xml");
    ASSERT_TRUE (QDir ().mkpath (QFileInfo (originalScene).absolutePath ()))
        << originalScene.toStdString ();
    ASSERT_TRUE (QDir ().mkpath (QFileInfo (generatedScene).absolutePath ()))
        << generatedScene.toStdString ();
    for (const auto& fileData : {
             std::make_pair (originalScene, QByteArray ("<WorkCell name=\"Original\" />\n")),
             std::make_pair (generatedScene, QByteArray ("<WorkCell name=\"Generated\" />\n")),
             std::make_pair (generatedDevice, QByteArray ("<SerialDevice name=\"Robot\" />\n"))}) {
        QFile file (fileData.first);
        ASSERT_TRUE (file.open (QIODevice::WriteOnly));
        ASSERT_EQ (fileData.second.size (), file.write (fileData.second));
    }

    ProjectManifest manifest;
    manifest.project.id = QStringLiteral ("promotion-test");
    manifest.project.name = QStringLiteral ("PromotionTest");
    ProjectResource workCell;
    workCell.id = QStringLiteral ("scene.main");
    workCell.kind = QStringLiteral ("robwork.workcell");
    workCell.path = QStringLiteral ("scenes/main.wc.xml");
    workCell.ownership = QStringLiteral ("project");
    workCell.required = true;
    manifest.resources.push_back (workCell);
    manifest.entryPoints.insert (QStringLiteral ("mainWorkCell"), workCell.id);
    ProjectManager manager;
    QString error;
    ASSERT_TRUE (manager.createProject (projectFile, manifest, &error))
        << error.toStdString ();
    manager.closeProject ();

    rwstudio.openFile (projectFile.toStdString ());
    ASSERT_TRUE (rwstudio.promoteGeneratedWorkCell (
        generatedScene, QStringList () << generatedDevice, &error))
        << error.toStdString ();
    EXPECT_EQ (QStringLiteral ("scene.main"), rwstudio.mainWorkCellResourceId ());
    QString activePath;
    ASSERT_TRUE (rwstudio.resolveProjectResource (
        QStringLiteral ("scene.main"), activePath, &error))
        << error.toStdString ();
    EXPECT_EQ (QDir::cleanPath (generatedScene), activePath);

}

// 端到端测试：saveCurrentProject 把脏 Provider 内容随完整事务落盘；插件在下游读取
// 项目资源前经 confirmSaveBeforeProjectResourceRead 强制保存(取消则阻止读取)。
TEST (RobWorkStudio, PromotesFirstGeneratedWorkCellForRobotProjectDraft)
{
    int argc = 1;
    char name[] = "RobWorkStudio";
    char* argv[1] = {name};
    QApplication app (argc, argv);
    PropertyMap map;
    RobWorkStudio rwstudio (map);

    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString projectFile = QDir (directory.path ()).filePath ("Robot.rwproj");
    const QString generatedScene =
        QDir (directory.path ()).filePath ("generated/robot-models/RobotScene.wc.xml");
    const QString generatedDevice =
        QDir (directory.path ()).filePath ("generated/robot-models/Robot.wc.xml");
    ASSERT_TRUE (QDir ().mkpath (QFileInfo (generatedScene).absolutePath ()));
    for (const auto& fileData : {
             std::make_pair (generatedScene, QByteArray ("<WorkCell name=\"Generated\" />\n")),
             std::make_pair (generatedDevice, QByteArray ("<SerialDevice name=\"Robot\" />\n"))}) {
        QFile file (fileData.first);
        ASSERT_TRUE (file.open (QIODevice::WriteOnly));
        ASSERT_EQ (fileData.second.size (), file.write (fileData.second));
    }

    ProjectManifest manifest;
    manifest.project.id = QStringLiteral ("robot-draft-promotion");
    manifest.project.name = QStringLiteral ("RobotDraftPromotion");
    ProjectManager manager;
    QString error;
    ASSERT_TRUE (manager.createProject (projectFile, manifest, &error)) << error.toStdString ();
    manager.closeProject ();

    rwstudio.openFile (projectFile.toStdString ());
    ASSERT_TRUE (rwstudio.mainWorkCellResourceId ().isEmpty ());
    ASSERT_TRUE (rwstudio.promoteGeneratedWorkCell (
        generatedScene, QStringList () << generatedDevice, &error))
        << error.toStdString ();
    EXPECT_EQ (QStringLiteral ("scene.main"), rwstudio.mainWorkCellResourceId ());
    QString activePath;
    ASSERT_TRUE (rwstudio.resolveProjectResource (
        QStringLiteral ("scene.main"), activePath, &error));
    EXPECT_EQ (QDir::cleanPath (generatedScene), activePath);
    ASSERT_TRUE (rwstudio.saveCurrentProject (&error)) << error.toStdString ();

    ProjectManager verification;
    ASSERT_TRUE (verification.openProject (projectFile, &error)) << error.toStdString ();
    EXPECT_EQ (QStringLiteral ("scene.main"),
               verification.manifest ().entryPoints.value (QStringLiteral ("mainWorkCell")));
}

TEST (RobWorkStudio, RepeatPromotionRemovesDisabledGeneratedSceneDependencies)
{
    int argc = 1;
    char name[] = "RobWorkStudio";
    char* argv[1] = {name};
    QApplication app (argc, argv);
    PropertyMap map;
    RobWorkStudio rwstudio (map);

    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString projectFile = QDir (directory.path ()).filePath ("Robot.rwproj");
    const QString originalScene = QDir (directory.path ()).filePath ("scenes/main.wc.xml");
    const QDir generatedDirectory (
        QDir (directory.path ()).filePath ("generated/robot-models"));
    const QString generatedScene = generatedDirectory.filePath ("RobotScene.wc.xml");
    const QString generatedDevice = generatedDirectory.filePath ("Robot.wc.xml");
    const QString collisionSetup = generatedDirectory.filePath ("CollisionSetup.xml");
    const QString proximitySetup = generatedDirectory.filePath ("ProximitySetup.xml");
    ASSERT_TRUE (QDir ().mkpath (generatedDirectory.path ()));
    ASSERT_TRUE (QDir ().mkpath (QFileInfo (originalScene).absolutePath ()));
    for (const auto& fileData : {
             std::make_pair (originalScene, QByteArray ("<WorkCell name=\"Original\" />\n")),
             std::make_pair (generatedScene, QByteArray ("<WorkCell name=\"Generated\" />\n")),
             std::make_pair (generatedDevice,
                             QByteArray ("<SerialDevice name=\"Robot\" />\n")),
             std::make_pair (collisionSetup, QByteArray ("<CollisionSetup />\n")),
             std::make_pair (proximitySetup, QByteArray ("<ProximitySetup />\n"))}) {
        QFile file (fileData.first);
        ASSERT_TRUE (file.open (QIODevice::WriteOnly | QIODevice::Truncate));
        ASSERT_EQ (fileData.second.size (), file.write (fileData.second));
    }

    ProjectManifest manifest;
    manifest.project.id = QStringLiteral ("repeat-promotion");
    manifest.project.name = QStringLiteral ("RepeatPromotion");
    ProjectResource original;
    original.id = QStringLiteral ("scene.main");
    original.kind = QStringLiteral ("robwork.workcell");
    original.path = QStringLiteral ("scenes/main.wc.xml");
    original.ownership = QStringLiteral ("project");
    original.required = true;
    manifest.resources.push_back (original);
    manifest.entryPoints.insert (QStringLiteral ("mainWorkCell"), original.id);
    ProjectManager manager;
    QString error;
    ASSERT_TRUE (manager.createProject (projectFile, manifest, &error)) << error.toStdString ();
    manager.closeProject ();

    rwstudio.openFile (projectFile.toStdString ());
    ASSERT_TRUE (rwstudio.promoteGeneratedWorkCell (
        generatedScene,
        QStringList () << generatedDevice << collisionSetup << proximitySetup,
        &error)) << error.toStdString ();
    ASSERT_TRUE (rwstudio.promoteGeneratedWorkCell (
        generatedScene, QStringList () << generatedDevice, &error)) << error.toStdString ();
    ASSERT_TRUE (rwstudio.saveCurrentProject (&error)) << error.toStdString ();

    ProjectManager verification;
    ASSERT_TRUE (verification.openProject (projectFile, &error)) << error.toStdString ();
    ProjectResource promoted;
    ASSERT_TRUE (verification.manifest ().findResource (QStringLiteral ("scene.main"), promoted));
    EXPECT_EQ (QStringList () << QStringLiteral ("scene.generated.device"),
               promoted.dependencies);
    ProjectResource asset;
    EXPECT_TRUE (verification.manifest ().findResource (
        QStringLiteral ("scene.generated.device"), asset));
    EXPECT_FALSE (verification.manifest ().findResource (
        QStringLiteral ("scene.generated.collision"), asset));
    EXPECT_FALSE (verification.manifest ().findResource (
        QStringLiteral ("scene.generated.proximity"), asset));
    ASSERT_TRUE (verification.manifest ().findResource (
        QStringLiteral ("scene.source.original"), asset));
    EXPECT_EQ (QStringLiteral ("scenes/main.wc.xml"), asset.path);

    verification.closeProject ();
    ASSERT_TRUE (QFile::remove (collisionSetup));
    ASSERT_TRUE (QFile::remove (proximitySetup));
    ASSERT_TRUE (verification.openProject (projectFile, &error)) << error.toStdString ();
    EXPECT_EQ (QStringLiteral ("scene.main"),
               verification.manifest ().entryPoints.value (QStringLiteral ("mainWorkCell")));
}

TEST (RobWorkStudio, PublishesDirtyProjectDocumentsAndGuardsProjectResourceReads)
{
    int argc = 1;
    char name[] = "RobWorkStudio";
    char* argv[1] = {name};
    QApplication app (argc, argv);
    PropertyMap map;
    RobWorkStudio rwstudio (map);

    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString projectFile = QDir (directory.path ()).filePath ("Publish.rwproj");
    const QString documentFile = QDir (directory.path ()).filePath ("requirements/main.json");
    ASSERT_TRUE (QDir ().mkpath (QFileInfo (documentFile).absolutePath ()));
    QFile initialDocument (documentFile);
    ASSERT_TRUE (initialDocument.open (QIODevice::WriteOnly));
    ASSERT_EQ (7, initialDocument.write ("initial"));
    initialDocument.close ();

    ProjectManifest manifest;
    manifest.project.id = QStringLiteral ("publish-test");
    manifest.project.name = QStringLiteral ("PublishTest");
    ProjectResource resource;
    resource.id = QStringLiteral ("engineering-requirements.main");
    resource.kind = QStringLiteral ("test.publish-requirements");
    resource.path = QStringLiteral ("requirements/main.json");
    resource.ownership = QStringLiteral ("project");
    resource.required = true;
    manifest.resources.push_back (resource);
    ProjectManager manager;
    QString error;
    ASSERT_TRUE (manager.createProject (projectFile, manifest, &error))
        << error.toStdString ();
    manager.closeProject ();

    int saveCount = 0;
    CallbackProjectDocumentProvider provider (
        QStringLiteral ("test.publish-provider"),
        resource.kind,
        [] (const QString&, const ProjectDocumentContext&, QString*) { return true; },
        [&saveCount] (const QString& targetPath,
                      const ProjectDocumentContext&,
                      QString* error) {
            ++saveCount;
            QFile output (targetPath);
            if (!output.open (QIODevice::WriteOnly | QIODevice::Truncate)) {
                if (error != nullptr)
                    *error = output.errorString ();
                return false;
            }
            return output.write ("published") == 9;
        });
    ASSERT_TRUE (rwstudio.registerProjectDocumentProvider (&provider, &error))
        << error.toStdString ();
    rwstudio.openFile (projectFile.toStdString ());

    EXPECT_FALSE (rwstudio.hasUnsavedProjectChanges ());
    provider.markDirty ();
    EXPECT_TRUE (rwstudio.hasUnsavedProjectChanges ());
    ASSERT_TRUE (rwstudio.saveCurrentProject (&error)) << error.toStdString ();
    EXPECT_FALSE (rwstudio.hasUnsavedProjectChanges ());
    EXPECT_EQ (1, saveCount);
    QFile savedDocument (documentFile);
    ASSERT_TRUE (savedDocument.open (QIODevice::ReadOnly));
    EXPECT_EQ (QByteArray ("published"), savedDocument.readAll ());
    savedDocument.close ();

    provider.markDirty ();
    QMessageBox::StandardButton buttonToClick = QMessageBox::Cancel;
    QTimer dialogResponder;
    dialogResponder.setInterval (5);
    QObject::connect (&dialogResponder, &QTimer::timeout, &app, [&buttonToClick] () {
        for (QWidget* widget : QApplication::topLevelWidgets ()) {
            QMessageBox* dialog = qobject_cast< QMessageBox* > (widget);
            if (dialog != nullptr && dialog->button (buttonToClick) != nullptr) {
                dialog->button (buttonToClick)->click ();
                return;
            }
        }
    });
    dialogResponder.start ();
    EXPECT_FALSE (rwstudio.confirmSaveBeforeProjectResourceRead (&rwstudio));
    dialogResponder.stop ();
    EXPECT_TRUE (rwstudio.hasUnsavedProjectChanges ());
    EXPECT_EQ (1, saveCount);

    buttonToClick = QMessageBox::Save;
    dialogResponder.start ();
    EXPECT_TRUE (rwstudio.confirmSaveBeforeProjectResourceRead (&rwstudio));
    dialogResponder.stop ();
    EXPECT_FALSE (rwstudio.hasUnsavedProjectChanges ());
    EXPECT_EQ (2, saveCount);

    rwstudio.close ();
}

TEST (RobWorkStudio, ClassifiesRobotProjectXmlBeforeDispatch)
{
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());

    const auto writeXml = [&directory] (const QString& name, const QByteArray& contents) {
        const QString path = QDir (directory.path ()).filePath (name);
        QFile file (path);
        EXPECT_TRUE (file.open (QIODevice::WriteOnly));
        EXPECT_EQ (contents.size (), file.write (contents));
        return path;
    };

    const QString urdf = writeXml (QStringLiteral ("robot.xml"),
                                   QByteArray ("<robot name=\"Example\"/>\n"));
    const QString device = writeXml (QStringLiteral ("device.xml"),
                                     QByteArray ("<SerialDevice name=\"Example\"/>\n"));
    const QString workCell = writeXml (QStringLiteral ("scene.xml"),
                                       QByteArray ("<WorkCell name=\"Example\"/>\n"));
    const QString unknown = writeXml (QStringLiteral ("unknown.xml"),
                                      QByteArray ("<configuration/>\n"));

    QString error;
    EXPECT_EQ (RobWorkStudio::RobotProjectSourceKind::Urdf,
               RobWorkStudio::classifyRobotProjectSource (urdf, &error));
    EXPECT_TRUE (error.isEmpty ());
    EXPECT_EQ (RobWorkStudio::RobotProjectSourceKind::RobWorkXml,
               RobWorkStudio::classifyRobotProjectSource (device, &error));
    EXPECT_TRUE (error.isEmpty ());
    EXPECT_EQ (RobWorkStudio::RobotProjectSourceKind::RobWorkXml,
               RobWorkStudio::classifyRobotProjectSource (workCell, &error));
    EXPECT_TRUE (error.isEmpty ());
    EXPECT_EQ (RobWorkStudio::RobotProjectSourceKind::Unsupported,
               RobWorkStudio::classifyRobotProjectSource (unknown, &error));
    EXPECT_FALSE (error.isEmpty ());
}

TEST (RobWorkStudio, RobotProjectPreflightFailurePreservesCurrentProject)
{
    int argc = 1;
    char name[] = "RobWorkStudio";
    char* argv[1] = {name};
    QApplication app (argc, argv);
    PropertyMap map;
    RobWorkStudio studio (map);
    QTemporaryDir current;
    QTemporaryDir source;
    QTemporaryDir target;
    ASSERT_TRUE (current.isValid () && source.isValid () && target.isValid ());
    const QString currentProject = createEmptyProject (current.path ());
    const QString urdf = createMinimalUrdf (source.path ());
    const QString targetProject = target.filePath ("PreflightFailure.rwproj");
    studio.openFile (currentProject.toStdString ());
    const std::vector< std::string > recentBefore = recentProjects (studio);
    EXPECT_EQ (recentBefore.end (),
               std::find (recentBefore.begin (), recentBefore.end (),
                          targetProject.toStdString ()));

    int preflightCount = 0;
    int commitCount = 0;
    RobotProjectImportCallbacks callbacks;
    callbacks.preflight = [&preflightCount] (const QString&, const QString&, QString*) {
        ++preflightCount;
        return false;
    };
    callbacks.confirmClose = [] (QString*) { return true; };
    callbacks.commit = [&commitCount] (const QString&, const QString&, QString*) {
        ++commitCount;
        return true;
    };

    QString error;
    EXPECT_FALSE (studio.createProjectFromRobotFilePaths (
        urdf, targetProject, callbacks, &error));
    EXPECT_EQ (1, preflightCount);
    EXPECT_EQ (0, commitCount);
    EXPECT_TRUE (error.contains (QStringLiteral ("source"), Qt::CaseInsensitive));
    EXPECT_EQ (QDir::cleanPath (current.path ()), QDir::cleanPath (studio.projectDirectory ()));
    EXPECT_FALSE (QFileInfo::exists (targetProject));
    EXPECT_EQ (recentBefore, recentProjects (studio));
}

TEST (RobWorkStudio, RobotProjectManagedCopyPreflightFailureRemovesCandidate)
{
    int argc = 1;
    char name[] = "RobWorkStudio";
    char* argv[1] = {name};
    QApplication app (argc, argv);
    PropertyMap map;
    RobWorkStudio studio (map);
    QTemporaryDir current;
    QTemporaryDir source;
    QTemporaryDir target;
    ASSERT_TRUE (current.isValid () && source.isValid () && target.isValid ());
    const QString currentProject = createEmptyProject (current.path ());
    const QString urdf = createMinimalUrdf (source.path ());
    const QString targetProject = target.filePath ("ManagedPreflightFailure.rwproj");
    studio.openFile (currentProject.toStdString ());
    const std::vector< std::string > recentBefore = recentProjects (studio);

    QString managedPreflightPath;
    int preflightCount = 0;
    RobotProjectImportCallbacks callbacks;
    callbacks.preflight = [&] (const QString& path, const QString&, QString* error) {
        ++preflightCount;
        if (preflightCount == 1)
            return true;
        managedPreflightPath = path;
        if (error != nullptr)
            *error = QStringLiteral ("injected managed preflight failure");
        return false;
    };
    callbacks.confirmClose = [] (QString*) { return true; };
    callbacks.commit = [] (const QString&, const QString&, QString*) { return true; };

    QString error;
    EXPECT_FALSE (studio.createProjectFromRobotFilePaths (
        urdf, targetProject, callbacks, &error));
    EXPECT_EQ (2, preflightCount);
    EXPECT_FALSE (managedPreflightPath.isEmpty ());
    EXPECT_FALSE (QFileInfo::exists (managedPreflightPath));
    EXPECT_TRUE (error.contains (QStringLiteral ("injected managed preflight failure")));
    EXPECT_EQ (QDir::cleanPath (current.path ()), QDir::cleanPath (studio.projectDirectory ()));
    EXPECT_FALSE (QFileInfo::exists (targetProject));
    EXPECT_EQ (recentBefore, recentProjects (studio));
}

TEST (RobWorkStudio, RobotProjectCommitFailureRestoresCurrentProject)
{
    int argc = 1;
    char name[] = "RobWorkStudio";
    char* argv[1] = {name};
    QApplication app (argc, argv);
    PropertyMap map;
    RobWorkStudio studio (map);
    QTemporaryDir current;
    QTemporaryDir source;
    QTemporaryDir target;
    ASSERT_TRUE (current.isValid () && source.isValid () && target.isValid ());

    const QString currentDocument = current.filePath ("documents/old.json");
    ASSERT_TRUE (QDir ().mkpath (QFileInfo (currentDocument).absolutePath ()));
    QFile document (currentDocument);
    ASSERT_TRUE (document.open (QIODevice::WriteOnly));
    ASSERT_EQ (3, document.write ("old"));
    document.close ();
    ProjectManifest oldManifest;
    oldManifest.project.id = QStringLiteral ("old-project");
    oldManifest.project.name = QStringLiteral ("OldProject");
    ProjectResource oldResource;
    oldResource.id = QStringLiteral ("old.document");
    oldResource.kind = QStringLiteral ("test.robot-project-old");
    oldResource.path = QStringLiteral ("documents/old.json");
    oldResource.ownership = QStringLiteral ("project");
    oldResource.required = true;
    oldManifest.resources.push_back (oldResource);
    const QString currentProject = current.filePath ("Old.rwproj");
    ProjectManager manager;
    QString error;
    ASSERT_TRUE (manager.createProject (currentProject, oldManifest, &error))
        << error.toStdString ();
    manager.closeProject ();

    int loadCount = 0;
    int closeCount = 0;
    CallbackProjectDocumentProvider oldProvider (
        QStringLiteral ("test.robot-project-old-provider"), oldResource.kind,
        [&loadCount] (const QString&, const ProjectDocumentContext&, QString*) {
            ++loadCount;
            return true;
        },
        CallbackProjectDocumentProvider::SaveHandler (),
        CallbackProjectDocumentProvider::CanCloseHandler (),
        [&closeCount] () { ++closeCount; });
    ASSERT_TRUE (studio.registerProjectDocumentProvider (&oldProvider, &error))
        << error.toStdString ();
    studio.openFile (currentProject.toStdString ());
    ASSERT_EQ (1, loadCount);
    const std::vector< std::string > recentBefore = recentProjects (studio);
    const QString urdf = createMinimalUrdf (source.path ());
    const QString targetProject = target.filePath ("CommitFailure.rwproj");

    QString committedSource;
    RobotProjectImportCallbacks callbacks;
    callbacks.preflight = [] (const QString&, const QString&, QString*) { return true; };
    callbacks.confirmClose = [] (QString*) { return true; };
    callbacks.commit = [&committedSource] (const QString& path, const QString&, QString*) {
        committedSource = path;
        return false;
    };

    EXPECT_FALSE (studio.createProjectFromRobotFilePaths (
        urdf, targetProject, callbacks, &error));
    EXPECT_FALSE (QFileInfo::exists (targetProject));
    EXPECT_FALSE (QFileInfo::exists (committedSource));
    EXPECT_TRUE (error.contains (QStringLiteral ("commit"), Qt::CaseInsensitive));
    EXPECT_EQ (QDir::cleanPath (current.path ()), QDir::cleanPath (studio.projectDirectory ()));
    EXPECT_EQ (2, loadCount);
    EXPECT_EQ (1, closeCount);
    EXPECT_EQ (recentBefore, recentProjects (studio));
}

TEST (RobWorkStudio, RobotProjectSuccessUpdatesRecentFilesOnlyAfterCommit)
{
    int argc = 1;
    char name[] = "RobWorkStudio";
    char* argv[1] = {name};
    QApplication app (argc, argv);
    PropertyMap map;
    RobWorkStudio studio (map);
    QTemporaryDir current;
    QTemporaryDir source;
    QTemporaryDir target;
    ASSERT_TRUE (current.isValid () && source.isValid () && target.isValid ());
    const QString currentProject = createEmptyProject (current.path ());
    const QString urdf = createMinimalUrdf (source.path ());
    const QString targetProject = target.filePath ("Success.rwproj");
    studio.openFile (currentProject.toStdString ());
    const std::vector< std::string > recentBefore = recentProjects (studio);
    EXPECT_EQ (recentBefore.end (),
               std::find (recentBefore.begin (), recentBefore.end (),
                          targetProject.toStdString ()));

    CallbackProjectDocumentProvider modelProvider (
        QStringLiteral ("test.robot-project-model-provider"),
        QStringLiteral ("robwork.robot-model"),
        [] (const QString&, const ProjectDocumentContext&, QString*) { return true; },
        CallbackProjectDocumentProvider::SaveHandler ());
    QString error;
    ASSERT_TRUE (studio.registerProjectDocumentProvider (&modelProvider, &error))
        << error.toStdString ();

    int preflightCount = 0;
    int commitCount = 0;
    RobotProjectImportCallbacks callbacks;
    callbacks.preflight = [&preflightCount] (const QString& path, const QString&, QString*) {
        ++preflightCount;
        return QFileInfo (path).isFile ();
    };
    callbacks.confirmClose = [] (QString*) { return true; };
    callbacks.commit = [&] (const QString& managedSource,
                            const QString& projectRoot,
                            QString* callbackError) {
        ++commitCount;
        if (!QFileInfo (managedSource).isFile ()) {
            if (callbackError != nullptr)
                *callbackError = QStringLiteral ("managed source was not committed");
            return false;
        }
        ProjectResource model;
        model.id = QStringLiteral ("robot-model.main");
        model.kind = QStringLiteral ("robwork.robot-model");
        model.path = QStringLiteral ("generated/robot-models/TestRobot.rmb.json");
        model.ownership = QStringLiteral ("generated");
        model.required = true;
        model.dependencies << QStringLiteral ("robot-source.main");
        const QString modelPath = QDir (projectRoot).filePath (model.path);
        if (!QDir ().mkpath (QFileInfo (modelPath).absolutePath ()))
            return false;
        QFile modelFile (modelPath);
        if (!modelFile.open (QIODevice::WriteOnly) || modelFile.write ("{}") != 2)
            return false;
        bool created = false;
        const bool registered = studio.ensureGeneratedProjectResource (
            model, &created, callbackError);
        if (registered)
            modelProvider.adoptGeneratedResource (model.id);
        return registered && created;
    };

    ASSERT_TRUE (studio.createProjectFromRobotFilePaths (
        urdf, targetProject, callbacks, &error)) << error.toStdString ();
    EXPECT_EQ (2, preflightCount);
    EXPECT_EQ (1, commitCount);
    EXPECT_EQ (QDir::cleanPath (target.path ()), QDir::cleanPath (studio.projectDirectory ()));
    EXPECT_TRUE (QFileInfo::exists (targetProject));
    const std::vector< std::string > recentAfter = recentProjects (studio);
    EXPECT_NE (recentBefore, recentAfter);
    ASSERT_FALSE (recentAfter.empty ());
    EXPECT_EQ (targetProject.toStdString (), recentAfter.back ());

    ASSERT_TRUE (studio.saveCurrentProject (&error)) << error.toStdString ();
    QFile manifestFile (targetProject);
    ASSERT_TRUE (manifestFile.open (QIODevice::ReadOnly));
    ProjectManifest saved;
    ASSERT_TRUE (ProjectManifestJson::fromJson (manifestFile.readAll (), saved, &error))
        << error.toStdString ();
    ProjectResource model;
    ASSERT_TRUE (saved.findResource (QStringLiteral ("robot-model.main"), model));
    EXPECT_EQ (QStringList () << QStringLiteral ("robot-source.main"), model.dependencies);
}

TEST (RobWorkStudio, NewRobotProjectPreflightFailurePreservesCurrentProject)
{
    int argc = 1;
    char name[] = "RobWorkStudio";
    char* argv[1] = {name};
    QApplication app (argc, argv);
    PropertyMap map;
    RobWorkStudio studio (map);
    QTemporaryDir current;
    QTemporaryDir target;
    ASSERT_TRUE (current.isValid () && target.isValid ());
    const QString currentProject = createEmptyProject (current.path ());
    const QString targetProject = target.filePath ("PreflightFailure.rwproj");
    studio.openFile (currentProject.toStdString ());
    const std::vector< std::string > recentBefore = recentProjects (studio);

    int preflightCount = 0;
    int bootstrapCount = 0;
    int confirmCloseCount = 0;
    NewRobotProjectCallbacks callbacks;
    setNoopNewRobotProjectStateCallbacks (callbacks);
    callbacks.preflight = [&] (const QString&, QString*) {
        ++preflightCount;
        return false;
    };
    callbacks.requiredResources = [] (const QString&, QVector< ProjectResource >&, QString*) {
        return true;
    };
    callbacks.confirmClose = [&] (QString*) {
        ++confirmCloseCount;
        return true;
    };
    callbacks.bootstrap = [&] (const QString&, QString*) {
        ++bootstrapCount;
        return true;
    };

    QString error;
    EXPECT_FALSE (studio.createProjectWithRobotModelBuilderPaths (
        targetProject, callbacks, &error));
    EXPECT_EQ (1, preflightCount);
    EXPECT_EQ (0, bootstrapCount);
    EXPECT_EQ (0, confirmCloseCount);
    EXPECT_EQ (QDir::cleanPath (current.path ()), QDir::cleanPath (studio.projectDirectory ()));
    EXPECT_FALSE (QFileInfo::exists (targetProject));
    EXPECT_EQ (recentBefore, recentProjects (studio));
}

TEST (RobWorkStudio, NewRobotProjectBootstrapFailureRemovesBuilderGeneratedOutput)
{
    int argc = 1;
    char name[] = "RobWorkStudio";
    char* argv[1] = {name};
    QApplication app (argc, argv);
    PropertyMap map;
    RobWorkStudio studio (map);
    QTemporaryDir current;
    QTemporaryDir target;
    ASSERT_TRUE (current.isValid () && target.isValid ());

    const QString currentProject = createEmptyProject (current.path ());
    studio.openFile (currentProject.toStdString ());
    const QString preservedFile = target.filePath ("preserved.txt");
    QFile preserved (preservedFile);
    ASSERT_TRUE (preserved.open (QIODevice::WriteOnly));
    ASSERT_EQ (4, preserved.write ("keep"));
    preserved.close ();

    CallbackProjectDocumentProvider modelProvider (
        QStringLiteral ("test.builder-rollback-provider"),
        QStringLiteral ("robwork.robot-model"),
        [] (const QString&, const ProjectDocumentContext&, QString*) { return true; },
        [] (const QString&, const ProjectDocumentContext&, QString*) { return true; });
    QString error;
    ASSERT_TRUE (studio.registerProjectDocumentProvider (&modelProvider, &error))
        << error.toStdString ();

    ProjectResource model;
    model.id = QStringLiteral ("robot-model.main");
    model.kind = QStringLiteral ("robwork.robot-model");
    model.path = QStringLiteral ("generated/robot-models/Candidate.rmb.json");
    model.ownership = QStringLiteral ("generated");
    model.required = true;
    const QString targetProject = target.filePath ("BootstrapFailure.rwproj");

    NewRobotProjectCallbacks callbacks;
    setNoopNewRobotProjectStateCallbacks (callbacks);
    callbacks.preflight = [] (const QString&, QString*) { return true; };
    QString builderState = QStringLiteral ("previous builder state");
    callbacks.snapshotState = [&] (QByteArray& snapshot, QString*) {
        snapshot = builderState.toUtf8 ();
        return true;
    };
    callbacks.restoreState = [&] (const QByteArray& snapshot, QString*) {
        builderState = QString::fromUtf8 (snapshot);
        return true;
    };
    callbacks.requiredResources = [model] (const QString&, QVector< ProjectResource >& resources,
                                          QString*) {
        resources.push_back (model);
        return true;
    };
    callbacks.confirmClose = [] (QString*) { return true; };
    callbacks.bootstrap = [&] (const QString& projectRoot, QString* callbackError) {
        builderState = QStringLiteral ("candidate builder state");
        const QString generatedPath = QDir (projectRoot).filePath (model.path);
        if (!QDir ().mkpath (QFileInfo (generatedPath).absolutePath ()))
            return false;
        QFile generated (generatedPath);
        if (!generated.open (QIODevice::WriteOnly) || generated.write ("{}") != 2)
            return false;
        bool created = false;
        if (!studio.ensureGeneratedProjectResource (model, &created, callbackError))
            return false;
        modelProvider.adoptGeneratedResource (model.id);
        if (callbackError != nullptr)
            *callbackError = QStringLiteral ("Intentional bootstrap failure.");
        return false;
    };

    EXPECT_FALSE (studio.createProjectWithRobotModelBuilderPaths (
        targetProject, callbacks, &error));
    EXPECT_FALSE (QFileInfo::exists (targetProject));
    EXPECT_FALSE (QFileInfo::exists (target.filePath ("generated")));
    EXPECT_EQ (QStringLiteral ("previous builder state"), builderState);
    QFile preservedAfter (preservedFile);
    ASSERT_TRUE (preservedAfter.open (QIODevice::ReadOnly));
    EXPECT_EQ (QByteArray ("keep"), preservedAfter.readAll ());
}

TEST (RobWorkStudio, NewRobotProjectRejectsUnsnapshotableSharedProviderBeforeConfirm)
{
    int argc = 1;
    char name[] = "RobWorkStudio";
    char* argv[1] = {name};
    QApplication app (argc, argv);
    PropertyMap map;
    RobWorkStudio studio (map);
    QTemporaryDir current;
    QTemporaryDir target;
    ASSERT_TRUE (current.isValid () && target.isValid ());

    ProjectResource model;
    model.id = QStringLiteral ("robot-model.main");
    model.kind = QStringLiteral ("robwork.robot-model");
    model.path = QStringLiteral ("generated/robot-models/Old.rmb.json");
    model.ownership = QStringLiteral ("generated");
    model.required = true;
    const QString currentDocument = current.filePath (model.path);
    ASSERT_TRUE (QDir ().mkpath (QFileInfo (currentDocument).absolutePath ()));
    QFile document (currentDocument);
    ASSERT_TRUE (document.open (QIODevice::WriteOnly));
    ASSERT_EQ (3, document.write ("old"));
    document.close ();

    ProjectManifest manifest;
    manifest.project.name = QStringLiteral ("CurrentRobotProject");
    manifest.resources.push_back (model);
    const QString currentProject = current.filePath ("Current.rwproj");
    ProjectManager manager;
    QString error;
    ASSERT_TRUE (manager.createProject (currentProject, manifest, &error)) << error.toStdString ();
    manager.closeProject ();

    int closeCount = 0;
    QString providerState;
    CallbackProjectDocumentProvider provider (
        QStringLiteral ("test.unsnapshotable-shared-provider"), model.kind,
        [&] (const QString&, const ProjectDocumentContext&, QString*) {
            providerState = QStringLiteral ("loaded current model");
            return true;
        },
        CallbackProjectDocumentProvider::SaveHandler (),
        CallbackProjectDocumentProvider::CanCloseHandler (),
        [&] () {
            ++closeCount;
            providerState.clear ();
        });
    ASSERT_TRUE (studio.registerProjectDocumentProvider (&provider, &error))
        << error.toStdString ();
    studio.openFile (currentProject.toStdString ());
    ASSERT_EQ (QStringLiteral ("loaded current model"), providerState);

    const QString preservedFile = target.filePath ("preserved.txt");
    QFile preserved (preservedFile);
    ASSERT_TRUE (preserved.open (QIODevice::WriteOnly));
    ASSERT_EQ (4, preserved.write ("keep"));
    preserved.close ();
    const QString targetProject = target.filePath ("Rejected.rwproj");
    int confirmCloseCount = 0;
    int bootstrapCount = 0;
    NewRobotProjectCallbacks callbacks;
    setNoopNewRobotProjectStateCallbacks (callbacks);
    callbacks.preflight = [] (const QString&, QString*) { return true; };
    callbacks.requiredResources = [model] (const QString&, QVector< ProjectResource >& resources,
                                          QString*) {
        resources.push_back (model);
        return true;
    };
    callbacks.confirmClose = [&] (QString*) {
        ++confirmCloseCount;
        return true;
    };
    callbacks.bootstrap = [&] (const QString&, QString*) {
        ++bootstrapCount;
        return true;
    };

    EXPECT_FALSE (studio.createProjectWithRobotModelBuilderPaths (
        targetProject, callbacks, &error));
    EXPECT_EQ (0, confirmCloseCount);
    EXPECT_EQ (0, bootstrapCount);
    EXPECT_FALSE (QFileInfo::exists (targetProject));
    EXPECT_FALSE (QFileInfo::exists (target.filePath ("generated")));
    EXPECT_EQ (0, closeCount);
    EXPECT_EQ (QStringLiteral ("loaded current model"), providerState);
    EXPECT_EQ (QDir::cleanPath (current.path ()), QDir::cleanPath (studio.projectDirectory ()));
    QFile preservedAfter (preservedFile);
    ASSERT_TRUE (preservedAfter.open (QIODevice::ReadOnly));
    EXPECT_EQ (QByteArray ("keep"), preservedAfter.readAll ());
}

TEST (RobWorkStudio, NewRobotProjectBootstrapFailureRestoresCurrentProject)
{
    int argc = 1;
    char name[] = "RobWorkStudio";
    char* argv[1] = {name};
    QApplication app (argc, argv);
    PropertyMap map;
    RobWorkStudio studio (map);
    QTemporaryDir current;
    QTemporaryDir target;
    ASSERT_TRUE (current.isValid () && target.isValid ());

    const QString currentDocument = current.filePath ("documents/old.json");
    ASSERT_TRUE (QDir ().mkpath (QFileInfo (currentDocument).absolutePath ()));
    QFile document (currentDocument);
    ASSERT_TRUE (document.open (QIODevice::WriteOnly));
    ASSERT_EQ (3, document.write ("old"));
    document.close ();
    ProjectManifest oldManifest;
    oldManifest.project.id = QStringLiteral ("old-project");
    oldManifest.project.name = QStringLiteral ("OldProject");
    ProjectResource oldResource;
    oldResource.id = QStringLiteral ("old.document");
    oldResource.kind = QStringLiteral ("test.new-project-old");
    oldResource.path = QStringLiteral ("documents/old.json");
    oldResource.ownership = QStringLiteral ("project");
    oldResource.required = true;
    oldManifest.resources.push_back (oldResource);
    const QString currentProject = current.filePath ("Old.rwproj");
    ProjectManager manager;
    QString error;
    ASSERT_TRUE (manager.createProject (currentProject, oldManifest, &error))
        << error.toStdString ();
    manager.closeProject ();

    int loadCount = 0;
    int oldCloseCount = 0;
    QString oldDocumentState;
    CallbackProjectDocumentProvider oldProvider (
        QStringLiteral ("test.new-project-old-provider"), oldResource.kind,
        [&] (const QString&, const ProjectDocumentContext&, QString*) {
            ++loadCount;
            oldDocumentState = QStringLiteral ("loaded-from-disk");
            return true;
        },
        CallbackProjectDocumentProvider::SaveHandler (),
        CallbackProjectDocumentProvider::CanCloseHandler (),
        [&] () {
            ++oldCloseCount;
            oldDocumentState.clear ();
        });
    ASSERT_TRUE (studio.registerProjectDocumentProvider (&oldProvider, &error))
        << error.toStdString ();
    studio.openFile (currentProject.toStdString ());
    ASSERT_EQ (1, loadCount);
    oldDocumentState = QStringLiteral ("unsaved in-memory edit");
    oldProvider.markDirty ();
    studio.notifyProjectDocumentChanged ();
    ASSERT_TRUE (studio.hasUnsavedProjectChanges ());
    const std::vector< std::string > recentBefore = recentProjects (studio);
    const QString targetProject = target.filePath ("BootstrapFailure.rwproj");

    int candidateCloseCount = 0;
    CallbackProjectDocumentProvider candidateProvider (
        QStringLiteral ("test.new-project-candidate-provider"),
        QStringLiteral ("robwork.robot-model"),
        [] (const QString&, const ProjectDocumentContext&, QString*) { return true; },
        CallbackProjectDocumentProvider::SaveHandler (),
        CallbackProjectDocumentProvider::CanCloseHandler (),
        [&] () { ++candidateCloseCount; });
    ASSERT_TRUE (studio.registerProjectDocumentProvider (&candidateProvider, &error))
        << error.toStdString ();

    NewRobotProjectCallbacks callbacks;
    setNoopNewRobotProjectStateCallbacks (callbacks);
    callbacks.preflight = [] (const QString&, QString*) { return true; };
    callbacks.requiredResources = [] (const QString&, QVector< ProjectResource >& resources,
                                      QString*) {
        ProjectResource candidate;
        candidate.id = QStringLiteral ("robot-model.main");
        candidate.kind = QStringLiteral ("robwork.robot-model");
        candidate.path = QStringLiteral ("generated/robot-models/Candidate.rmb.json");
        candidate.ownership = QStringLiteral ("generated");
        candidate.required = true;
        resources.push_back (candidate);
        return true;
    };
    callbacks.confirmClose = [] (QString*) { return true; };
    callbacks.bootstrap = [&] (const QString&, QString* callbackError) {
        ProjectResource candidate;
        candidate.id = QStringLiteral ("robot-model.main");
        candidate.kind = QStringLiteral ("robwork.robot-model");
        candidate.path = QStringLiteral ("generated/robot-models/Candidate.rmb.json");
        candidate.ownership = QStringLiteral ("generated");
        candidate.required = true;
        bool created = false;
        if (!studio.ensureGeneratedProjectResource (candidate, &created, callbackError))
            return false;
        candidateProvider.adoptGeneratedResource (candidate.id);
        return false;
    };

    EXPECT_FALSE (studio.createProjectWithRobotModelBuilderPaths (
        targetProject, callbacks, &error));
    EXPECT_FALSE (QFileInfo::exists (targetProject));
    EXPECT_EQ (QDir::cleanPath (current.path ()), QDir::cleanPath (studio.projectDirectory ()));
    EXPECT_EQ (1, loadCount);
    EXPECT_EQ (0, oldCloseCount);
    EXPECT_EQ (1, candidateCloseCount);
    EXPECT_EQ (QStringLiteral ("unsaved in-memory edit"), oldDocumentState);
    EXPECT_TRUE (studio.hasUnsavedProjectChanges ());
    QString candidatePath;
    EXPECT_FALSE (studio.resolveProjectResource (QStringLiteral ("robot-model.main"),
                                                 candidatePath,
                                                 &error));
    EXPECT_EQ (recentBefore, recentProjects (studio));
}

TEST (RobWorkStudio, NewRobotProjectBootstrapFailureRestoresSharedProviderDocument)
{
    int argc = 1;
    char name[] = "RobWorkStudio";
    char* argv[1] = {name};
    QApplication app (argc, argv);
    PropertyMap map;
    RobWorkStudio studio (map);
    QTemporaryDir current;
    QTemporaryDir target;
    ASSERT_TRUE (current.isValid () && target.isValid ());

    const QString currentDocument = current.filePath ("generated/robot-models/Old.rmb.json");
    ASSERT_TRUE (QDir ().mkpath (QFileInfo (currentDocument).absolutePath ()));
    QFile document (currentDocument);
    ASSERT_TRUE (document.open (QIODevice::WriteOnly));
    ASSERT_EQ (3, document.write ("old"));
    document.close ();

    ProjectManifest oldManifest;
    oldManifest.project.id = QStringLiteral ("old-robot-project");
    oldManifest.project.name = QStringLiteral ("OldRobotProject");
    ProjectResource model;
    model.id = QStringLiteral ("robot-model.main");
    model.kind = QStringLiteral ("robwork.robot-model");
    model.path = QStringLiteral ("generated/robot-models/Old.rmb.json");
    model.ownership = QStringLiteral ("generated");
    model.required = true;
    oldManifest.resources.push_back (model);
    const QString currentProject = current.filePath ("Old.rwproj");
    ProjectManager manager;
    QString error;
    ASSERT_TRUE (manager.createProject (currentProject, oldManifest, &error))
        << error.toStdString ();
    manager.closeProject ();

    QString modelState;
    int closeCount = 0;
    CallbackProjectDocumentProvider provider (
        QStringLiteral ("test.shared-robot-model-provider"), model.kind,
        [&] (const QString&, const ProjectDocumentContext&, QString*) {
            modelState = QStringLiteral ("loaded-from-disk");
            return true;
        },
        CallbackProjectDocumentProvider::SaveHandler (),
        CallbackProjectDocumentProvider::CanCloseHandler (),
        [&] () {
            ++closeCount;
            modelState.clear ();
        },
        CallbackProjectDocumentProvider::CleanHandler (),
        [&] (QByteArray* snapshot, QString*) {
            *snapshot = modelState.toUtf8 ();
            return true;
        },
        [&] (const QByteArray& snapshot, QString*) {
            modelState = QString::fromUtf8 (snapshot);
            return true;
        });
    ASSERT_TRUE (studio.registerProjectDocumentProvider (&provider, &error))
        << error.toStdString ();
    studio.openFile (currentProject.toStdString ());
    modelState = QStringLiteral ("old unsaved in-memory model");
    provider.markDirty ();
    studio.notifyProjectDocumentChanged ();
    ASSERT_TRUE (studio.hasUnsavedProjectChanges ());
    const std::vector< std::string > recentBefore = recentProjects (studio);
    const QString targetProject = target.filePath ("BootstrapFailure.rwproj");

    NewRobotProjectCallbacks callbacks;
    setNoopNewRobotProjectStateCallbacks (callbacks);
    callbacks.preflight = [] (const QString&, QString*) { return true; };
    callbacks.requiredResources = [model] (const QString&, QVector< ProjectResource >& resources,
                                           QString*) {
        ProjectResource candidate = model;
        candidate.path = QStringLiteral ("generated/robot-models/Candidate.rmb.json");
        resources.push_back (candidate);
        return true;
    };
    callbacks.confirmClose = [] (QString*) { return true; };
    callbacks.bootstrap = [&] (const QString&, QString* callbackError) {
        ProjectResource candidate = model;
        candidate.path = QStringLiteral ("generated/robot-models/Candidate.rmb.json");
        bool created = false;
        if (!studio.ensureGeneratedProjectResource (candidate, &created, callbackError))
            return false;
        provider.adoptGeneratedResource (candidate.id);
        modelState = QStringLiteral ("candidate in-memory model");
        provider.markDirty ();
        studio.notifyProjectDocumentChanged ();
        return false;
    };

    EXPECT_FALSE (studio.createProjectWithRobotModelBuilderPaths (
        targetProject, callbacks, &error));
    EXPECT_FALSE (QFileInfo::exists (targetProject));
    EXPECT_EQ (QDir::cleanPath (current.path ()), QDir::cleanPath (studio.projectDirectory ()));
    EXPECT_EQ (1, closeCount);
    EXPECT_EQ (QStringLiteral ("old unsaved in-memory model"), modelState);
    EXPECT_TRUE (studio.hasUnsavedProjectChanges ());
    QString restoredPath;
    EXPECT_TRUE (studio.resolveProjectResource (QStringLiteral ("robot-model.main"),
                                                restoredPath,
                                                &error));
    EXPECT_EQ (QDir::cleanPath (currentDocument), QDir::cleanPath (restoredPath));
    EXPECT_EQ (recentBefore, recentProjects (studio));
}

TEST (RobWorkStudio, NewRobotProjectRejectsIncompleteCallbacksAndInvalidTargetBeforeInvocation)
{
    int argc = 1;
    char name[] = "RobWorkStudio";
    char* argv[1] = {name};
    QApplication app (argc, argv);
    PropertyMap map;
    RobWorkStudio studio (map);
    QTemporaryDir target;
    ASSERT_TRUE (target.isValid ());

    int invoked = 0;
    NewRobotProjectCallbacks incomplete;
    incomplete.preflight = [&] (const QString&, QString*) {
        ++invoked;
        return true;
    };
    QString error;
    EXPECT_NO_THROW (EXPECT_FALSE (studio.createProjectWithRobotModelBuilderPaths (
        target.filePath ("Incomplete.rwproj"), incomplete, &error)));
    EXPECT_EQ (0, invoked);

    const QString existingTarget = target.filePath ("Existing.rwproj");
    QFile existing (existingTarget);
    ASSERT_TRUE (existing.open (QIODevice::WriteOnly));
    ASSERT_EQ (8, existing.write ("existing"));
    existing.close ();

    NewRobotProjectCallbacks complete;
    complete.preflight = [&] (const QString&, QString*) {
        ++invoked;
        return true;
    };
    complete.requiredResources = [&] (const QString&, QVector< ProjectResource >&, QString*) {
        ++invoked;
        return true;
    };
    complete.snapshotState = [&] (QByteArray&, QString*) {
        ++invoked;
        return true;
    };
    complete.restoreState = [&] (const QByteArray&, QString*) {
        ++invoked;
        return true;
    };
    complete.bootstrap = [&] (const QString&, QString*) {
        ++invoked;
        return true;
    };
    complete.confirmClose = [&] (QString*) {
        ++invoked;
        return true;
    };
    EXPECT_FALSE (studio.createProjectWithRobotModelBuilderPaths (
        existingTarget, complete, &error));
    EXPECT_EQ (0, invoked);
}

TEST (RobWorkStudio, NewRobotProjectRejectsTargetBeneathDirectoryJunctionBeforeCallbacks)
{
#ifndef Q_OS_WIN
    GTEST_SKIP () << "Directory junction regression is specific to Windows.";
#else
    int argc = 1;
    char name[] = "RobWorkStudio";
    char* argv[1] = {name};
    QApplication app (argc, argv);
    PropertyMap map;
    RobWorkStudio studio (map);
    QTemporaryDir base;
    QTemporaryDir external;
    ASSERT_TRUE (base.isValid () && external.isValid ());

    const QByteArray sentinelBytes ("junction target sentinel");
    const QString sentinelPath = external.filePath (QStringLiteral ("sentinel.bin"));
    QFile sentinel (sentinelPath);
    ASSERT_TRUE (sentinel.open (QIODevice::WriteOnly));
    ASSERT_EQ (sentinelBytes.size (), sentinel.write (sentinelBytes));
    sentinel.close ();

    const QString junctionPath = base.filePath (QStringLiteral ("project-link"));
    ASSERT_TRUE (createDirectoryJunction (junctionPath, external.path ()));
    DirectoryJunctionCleanup junctionCleanup (junctionPath);

    int preflightCount = 0;
    int confirmCount = 0;
    int bootstrapCount = 0;
    NewRobotProjectCallbacks callbacks;
    setNoopNewRobotProjectStateCallbacks (callbacks);
    callbacks.preflight = [&] (const QString&, QString*) {
        ++preflightCount;
        return true;
    };
    callbacks.requiredResources = [] (const QString&, QVector< ProjectResource >& resources,
                                      QString*) {
        ProjectResource model;
        model.id = QStringLiteral ("robot-model.main");
        model.kind = QStringLiteral ("robwork.robot-model");
        model.path = QStringLiteral ("generated/robot-models/Junction.rmb.json");
        model.ownership = QStringLiteral ("generated");
        model.required = true;
        resources.push_back (model);
        return true;
    };
    callbacks.confirmClose = [&] (QString*) {
        ++confirmCount;
        return true;
    };
    callbacks.bootstrap = [&] (const QString&, QString*) {
        ++bootstrapCount;
        return true;
    };

    QString error;
    const QString targetProject =
        QDir (junctionPath).filePath (QStringLiteral ("candidate/deep/Junction.rwproj"));
    EXPECT_FALSE (studio.createProjectWithRobotModelBuilderPaths (
        targetProject, callbacks, &error));
    EXPECT_FALSE (error.isEmpty ());
    EXPECT_EQ (0, preflightCount);
    EXPECT_EQ (0, confirmCount);
    EXPECT_EQ (0, bootstrapCount);
    EXPECT_FALSE (QFileInfo::exists (QDir (external.path ()).filePath (QStringLiteral ("candidate"))));
    QFile sentinelAfter (sentinelPath);
    ASSERT_TRUE (sentinelAfter.open (QIODevice::ReadOnly));
    EXPECT_EQ (sentinelBytes, sentinelAfter.readAll ());
#endif
}

TEST (RobWorkStudio, NewRobotProjectRejectsTargetBeneathDanglingDirectorySymlinkBeforeCallbacks)
{
    int argc = 1;
    char name[] = "RobWorkStudio";
    char* argv[1] = {name};
    QApplication app (argc, argv);
    PropertyMap map;
    RobWorkStudio studio (map);
    QTemporaryDir base;
    ASSERT_TRUE (base.isValid ());

    const QString junctionPath = base.filePath (QStringLiteral ("project-link"));
    const QString missingTarget = base.filePath (QStringLiteral ("missing-target"));
    ASSERT_FALSE (QFileInfo::exists (missingTarget));
    ASSERT_TRUE (createDirectorySymlink (junctionPath, missingTarget));
    DirectoryJunctionCleanup junctionCleanup (junctionPath);
    ASSERT_TRUE (QFileInfo (junctionPath).isSymLink ());
#ifndef Q_OS_WIN
    ASSERT_FALSE (QFileInfo::exists (junctionPath));
#endif

    int preflightCount = 0;
    int confirmCount = 0;
    int bootstrapCount = 0;
    NewRobotProjectCallbacks callbacks;
    setNoopNewRobotProjectStateCallbacks (callbacks);
    callbacks.preflight = [&] (const QString&, QString*) {
        ++preflightCount;
        return true;
    };
    callbacks.requiredResources = [] (const QString&, QVector< ProjectResource >& resources,
                                      QString*) {
        ProjectResource model;
        model.id = QStringLiteral ("robot-model.main");
        model.kind = QStringLiteral ("robwork.robot-model");
        model.path = QStringLiteral ("generated/robot-models/Junction.rmb.json");
        model.ownership = QStringLiteral ("generated");
        model.required = true;
        resources.push_back (model);
        return true;
    };
    callbacks.confirmClose = [&] (QString*) {
        ++confirmCount;
        return true;
    };
    callbacks.bootstrap = [&] (const QString&, QString*) {
        ++bootstrapCount;
        return true;
    };

    QString error;
    const QString targetProject =
        QDir (junctionPath).filePath (QStringLiteral ("candidate/deep/Junction.rwproj"));
    EXPECT_FALSE (studio.createProjectWithRobotModelBuilderPaths (
        targetProject, callbacks, &error));
    EXPECT_FALSE (error.isEmpty ());
    EXPECT_EQ (0, preflightCount);
    EXPECT_EQ (0, confirmCount);
    EXPECT_EQ (0, bootstrapCount);
    EXPECT_FALSE (QFileInfo::exists (targetProject));
}

TEST (RobWorkStudio, NewRobotProjectRejectsNonGeneratedResourcesBeforeMutation)
{
    int argc = 1;
    char name[] = "RobWorkStudio";
    char* argv[1] = {name};
    QApplication app (argc, argv);
    PropertyMap map;
    RobWorkStudio studio (map);
    QTemporaryDir target;
    QTemporaryDir external;
    ASSERT_TRUE (target.isValid () && external.isValid ());

    const QString externalPath = external.filePath (QStringLiteral ("External.rmb.json"));
    QFile externalFile (externalPath);
    ASSERT_TRUE (externalFile.open (QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_EQ (8, externalFile.write ("external"));
    externalFile.close ();

    struct RejectedDeclaration
    {
        QString ownership;
        QString path;
    };
    const QVector< RejectedDeclaration > declarations = {
        {QStringLiteral ("project"), QStringLiteral ("models/ProjectOwned.rmb.json")},
        {QStringLiteral ("external"), QFileInfo (externalPath).absoluteFilePath ()}};

    for (const RejectedDeclaration& declaration : declarations) {
        SCOPED_TRACE (declaration.ownership.toStdString ());
        int preflightCount = 0;
        int declarationCount = 0;
        int snapshotCount = 0;
        int restoreCount = 0;
        int confirmCount = 0;
        int bootstrapCount = 0;
        const QString candidateRoot =
            target.filePath (QStringLiteral ("candidate-%1/deep").arg (declaration.ownership));
        const QString projectFile = QDir (candidateRoot).filePath (QStringLiteral ("Robot.rwproj"));

        NewRobotProjectCallbacks callbacks;
        callbacks.preflight = [&] (const QString&, QString*) {
            ++preflightCount;
            return true;
        };
        callbacks.requiredResources = [&] (const QString&,
                                           QVector< ProjectResource >& resources,
                                           QString*) {
            ++declarationCount;
            ProjectResource resource;
            resource.id = QStringLiteral ("robot-model.main");
            resource.kind = QStringLiteral ("robwork.robot-model");
            resource.path = declaration.path;
            resource.ownership = declaration.ownership;
            resource.required = true;
            resources.push_back (resource);
            return true;
        };
        callbacks.snapshotState = [&] (QByteArray&, QString*) {
            ++snapshotCount;
            return true;
        };
        callbacks.restoreState = [&] (const QByteArray&, QString*) {
            ++restoreCount;
            return true;
        };
        callbacks.confirmClose = [&] (QString*) {
            ++confirmCount;
            return true;
        };
        callbacks.bootstrap = [&] (const QString&, QString*) {
            ++bootstrapCount;
            return true;
        };

        QString error;
        EXPECT_FALSE (
            studio.createProjectWithRobotModelBuilderPaths (projectFile, callbacks, &error));
        EXPECT_TRUE (error.contains (QStringLiteral ("generated"), Qt::CaseInsensitive))
            << error.toStdString ();
        EXPECT_EQ (1, preflightCount);
        EXPECT_EQ (1, declarationCount);
        EXPECT_EQ (0, snapshotCount);
        EXPECT_EQ (0, restoreCount);
        EXPECT_EQ (0, confirmCount);
        EXPECT_EQ (0, bootstrapCount);
        EXPECT_FALSE (QFileInfo::exists (candidateRoot));
        EXPECT_FALSE (QFileInfo::exists (projectFile));
    }

    ASSERT_TRUE (externalFile.open (QIODevice::ReadOnly));
    EXPECT_EQ (QByteArray ("external"), externalFile.readAll ());
}

TEST (RobWorkStudio, NewRobotProjectRejectsInvalidDeclarationsBeforeSnapshot)
{
    int argc = 1;
    char name[] = "RobWorkStudio";
    char* argv[1] = {name};
    QApplication app (argc, argv);
    PropertyMap map;
    RobWorkStudio studio (map);
    QTemporaryDir target;
    ASSERT_TRUE (target.isValid ());

    const QString modelKind = QStringLiteral ("robwork.robot-model");
    const QString alternateKind = QStringLiteral ("test.new-project-declaration-validation");
    CallbackProjectDocumentProvider modelProvider (
        QStringLiteral ("test.new-project-model-declaration-validation-provider"), modelKind,
        [] (const QString&, const ProjectDocumentContext&, QString*) { return true; },
        CallbackProjectDocumentProvider::SaveHandler ());
    CallbackProjectDocumentProvider alternateProvider (
        QStringLiteral ("test.new-project-declaration-validation-provider"), alternateKind,
        [] (const QString&, const ProjectDocumentContext&, QString*) { return true; },
        CallbackProjectDocumentProvider::SaveHandler ());
    QString error;
    ASSERT_TRUE (studio.registerProjectDocumentProvider (&modelProvider, &error))
        << error.toStdString ();
    ASSERT_TRUE (studio.registerProjectDocumentProvider (&alternateProvider, &error))
        << error.toStdString ();

    const auto resource = [] (const QString& id, const QString& kind, const QString& path) {
        ProjectResource declared;
        declared.id = id;
        declared.kind = kind;
        declared.path = path;
        declared.ownership = QStringLiteral ("generated");
        declared.required = true;
        return declared;
    };
    struct InvalidDeclaration
    {
        QString name;
        QVector< ProjectResource > resources;
    };
    const QVector< InvalidDeclaration > declarations = {
        {QStringLiteral ("missing-main"), {}},
        {QStringLiteral ("empty-id"),
         {resource (QString (), modelKind,
                    QStringLiteral ("generated/robot-models/Empty.rmb.json"))}},
        {QStringLiteral ("wrong-id"),
         {resource (QStringLiteral ("robot-model.other"), modelKind,
                    QStringLiteral ("generated/robot-models/Other.rmb.json"))}},
        {QStringLiteral ("wrong-kind"),
         {resource (QStringLiteral ("robot-model.main"), alternateKind,
                    QStringLiteral ("generated/robot-models/WrongKind.rmb.json"))}},
        {QStringLiteral ("duplicate-id"),
         {resource (QStringLiteral ("robot-model.main"), modelKind,
                    QStringLiteral ("generated/robot-models/First.rmb.json")),
          resource (QStringLiteral ("robot-model.main"), modelKind,
                    QStringLiteral ("generated/robot-models/Second.rmb.json"))}},
        {QStringLiteral ("wrong-directory"),
         {resource (QStringLiteral ("robot-model.main"), modelKind,
                    QStringLiteral ("generated/models/WrongDirectory.rmb.json"))}},
        {QStringLiteral ("wrong-extension"),
         {resource (QStringLiteral ("robot-model.main"), modelKind,
                    QStringLiteral ("generated/robot-models/WrongExtension.json"))}},
        {QStringLiteral ("unnormalized-path"),
         {resource (QStringLiteral ("robot-model.main"), modelKind,
                    QStringLiteral ("generated/robot-models/../robot-models/Model.rmb.json"))}},
        {QStringLiteral ("not-required"),
         {[&] {
             ProjectResource declared =
                 resource (QStringLiteral ("robot-model.main"), modelKind,
                           QStringLiteral ("generated/robot-models/Optional.rmb.json"));
             declared.required = false;
             return declared;
         } ()}},
        {QStringLiteral ("unknown-provider"),
          {resource (QStringLiteral ("robot-model.main"),
                     QStringLiteral ("test.new-project-missing-provider"),
                     QStringLiteral ("generated/robot-models/UnknownProvider.rmb.json"))}},
        {QStringLiteral ("model-dependency"),
         {[&] {
             ProjectResource declared =
                 resource (QStringLiteral ("robot-model.main"), modelKind,
                           QStringLiteral ("generated/robot-models/Dependent.rmb.json"));
             declared.dependencies.push_back (QStringLiteral ("candidate.helper"));
             return declared;
         } (),
          resource (QStringLiteral ("candidate.helper"), alternateKind,
                    QStringLiteral ("generated/helper.json"))}},
        {QStringLiteral ("dependency-cycle"),
         {[&] {
              ProjectResource declared =
                  resource (QStringLiteral ("robot-model.main"), modelKind,
                            QStringLiteral ("generated/robot-models/Cycle.rmb.json"));
              declared.dependencies.push_back (QStringLiteral ("candidate.helper"));
              return declared;
          } (),
          [&] {
              ProjectResource declared =
                  resource (QStringLiteral ("candidate.helper"), alternateKind,
                            QStringLiteral ("generated/cycle-second.json"));
              declared.dependencies.push_back (QStringLiteral ("robot-model.main"));
              return declared;
          } ()}}};

    for (const InvalidDeclaration& declaration : declarations) {
        SCOPED_TRACE (declaration.name.toStdString ());
        int snapshotCount = 0;
        int restoreCount = 0;
        int confirmCount = 0;
        int bootstrapCount = 0;
        NewRobotProjectCallbacks callbacks;
        callbacks.preflight = [] (const QString&, QString*) { return true; };
        callbacks.requiredResources = [&declaration] (
                                          const QString&,
                                          QVector< ProjectResource >& resources,
                                          QString*) {
            resources = declaration.resources;
            return true;
        };
        callbacks.snapshotState = [&] (QByteArray&, QString*) {
            ++snapshotCount;
            return true;
        };
        callbacks.restoreState = [&] (const QByteArray&, QString*) {
            ++restoreCount;
            return true;
        };
        callbacks.confirmClose = [&] (QString*) {
            ++confirmCount;
            return true;
        };
        callbacks.bootstrap = [&] (const QString&, QString*) {
            ++bootstrapCount;
            return true;
        };

        const QString candidateRoot = target.filePath (declaration.name + QStringLiteral ("/deep"));
        const QString projectFile = QDir (candidateRoot).filePath (QStringLiteral ("Robot.rwproj"));
        error.clear ();
        EXPECT_FALSE (
            studio.createProjectWithRobotModelBuilderPaths (projectFile, callbacks, &error));
        EXPECT_FALSE (error.isEmpty ());
        EXPECT_EQ (0, snapshotCount);
        EXPECT_EQ (0, restoreCount);
        EXPECT_EQ (0, confirmCount);
        EXPECT_EQ (0, bootstrapCount);
        EXPECT_FALSE (QFileInfo::exists (candidateRoot));
        EXPECT_FALSE (QFileInfo::exists (projectFile));
    }
}

TEST (RobWorkStudio, NewRobotProjectRejectsUnnormalizedSecondaryPathBeforeSnapshot)
{
    expectNewRobotProjectRejectsInvalidSecondaryPath (
        QStringLiteral ("generated/a/../helper.json"));
}

TEST (RobWorkStudio, NewRobotProjectRejectsNativeSecondaryPathBeforeSnapshot)
{
    expectNewRobotProjectRejectsInvalidSecondaryPath (QStringLiteral ("generated\\helper.json"));
}

TEST (RobWorkStudio, NewRobotProjectBootstrapFailureRestoresDeclaredOutputBytes)
{
    int argc = 1;
    char name[] = "RobWorkStudio";
    char* argv[1] = {name};
    QApplication app (argc, argv);
    PropertyMap map;
    RobWorkStudio studio (map);
    QTemporaryDir current;
    QTemporaryDir target;
    ASSERT_TRUE (current.isValid () && target.isValid ());
    studio.openFile (createEmptyProject (current.path ()).toStdString ());

    ProjectResource model;
    model.id = QStringLiteral ("robot-model.main");
    model.kind = QStringLiteral ("robwork.robot-model");
    model.path = QStringLiteral ("generated/robot-models/Candidate.rmb.json");
    model.ownership = QStringLiteral ("generated");
    model.required = true;
    CallbackProjectDocumentProvider provider (
        QStringLiteral ("test.builder-byte-restore-provider"), model.kind,
        [] (const QString&, const ProjectDocumentContext&, QString*) { return true; },
        [] (const QString&, const ProjectDocumentContext&, QString*) { return true; });
    QString error;
    ASSERT_TRUE (studio.registerProjectDocumentProvider (&provider, &error));

    QByteArray sentinel ("exact preexisting model bytes");
    sentinel.append ('\0');
    sentinel.append ("with binary suffix");
    const QString outputPath = target.filePath (model.path);
    ASSERT_TRUE (QDir ().mkpath (QFileInfo (outputPath).absolutePath ()));
    QFile output (outputPath);
    ASSERT_TRUE (output.open (QIODevice::WriteOnly));
    ASSERT_EQ (sentinel.size (), output.write (sentinel));
    output.close ();

    NewRobotProjectCallbacks callbacks;
    setNoopNewRobotProjectStateCallbacks (callbacks);
    callbacks.preflight = [] (const QString&, QString*) { return true; };
    callbacks.requiredResources = [model] (const QString&, QVector< ProjectResource >& resources,
                                           QString*) {
        resources.push_back (model);
        return true;
    };
    callbacks.confirmClose = [] (QString*) { return true; };
    callbacks.bootstrap = [&] (const QString&, QString* callbackError) {
        QFile replacement (outputPath);
        if (!replacement.open (QIODevice::WriteOnly | QIODevice::Truncate) ||
            replacement.write ("replacement") != 11)
            return false;
        replacement.close ();
        bool created = false;
        if (!studio.ensureGeneratedProjectResource (model, &created, callbackError))
            return false;
        provider.adoptGeneratedResource (model.id);
        if (callbackError != nullptr)
            *callbackError = QStringLiteral ("Intentional bootstrap failure after overwrite.");
        return false;
    };

    EXPECT_FALSE (studio.createProjectWithRobotModelBuilderPaths (
        target.filePath ("Candidate.rwproj"), callbacks, &error));
    QFile restored (outputPath);
    ASSERT_TRUE (restored.open (QIODevice::ReadOnly));
    EXPECT_EQ (sentinel, restored.readAll ());
    restored.close ();
    QDirIterator residue (target.path (),
                          QStringList () << QStringLiteral ("*.rwstage-*")
                                         << QStringLiteral ("*.rwbackup-*")
                                         << QStringLiteral ("*.tmp"),
                          QDir::Files, QDirIterator::Subdirectories);
    EXPECT_FALSE (residue.hasNext ());
}

TEST (RobWorkStudio, NewRobotProjectBootstrapFailureRestoresOverwrittenUndeclaredBaselineFile)
{
    expectNewRobotProjectRestoresUndeclaredBaseline (BaselineMutationCase::OverwriteThenFail);
}

TEST (RobWorkStudio, NewRobotProjectBootstrapFailureRestoresDeletedUndeclaredBaselineFileAndDirectory)
{
    expectNewRobotProjectRestoresUndeclaredBaseline (BaselineMutationCase::DeleteThenFail);
}

TEST (RobWorkStudio, NewRobotProjectBootstrapFailureRestoresBaselineDirectoryReplacedByJunction)
{
#ifdef Q_OS_WIN
    expectNewRobotProjectRestoresUndeclaredBaseline (BaselineMutationCase::JunctionThenFail);
#else
    GTEST_SKIP () << "Directory junction regression is specific to Windows.";
#endif
}

TEST (RobWorkStudio, NewRobotProjectTransactionGuardPreventsBaselineRootReplacement)
{
#ifdef Q_OS_WIN
    expectNewRobotProjectRestoresUndeclaredBaseline (
        BaselineMutationCase::RootDanglingSymlinkThenFail);
#else
    GTEST_SKIP () << "Windows transaction handles prevent project-root replacement.";
#endif
}

TEST (RobWorkStudio, NewRobotProjectIdentityChangeSkipsReplacementFilesystemRollback)
{
#ifdef Q_OS_WIN
    GTEST_SKIP () << "POSIX identity validation semantics are not used on Windows.";
#else
    expectNewRobotProjectFailureRestoresFullState (
        CandidateFailureCase::ProjectRootIdentityChange);
#endif
}

TEST (RobWorkStudio, NewRobotProjectBootstrapSuccessRejectsUndeclaredBaselineMutation)
{
    expectNewRobotProjectRestoresUndeclaredBaseline (BaselineMutationCase::ModifyThenSucceed);
}

TEST (RobWorkStudio, NewRobotProjectFailureRemovesWhollyNewNestedRoot)
{
    int argc = 1;
    char name[] = "RobWorkStudio";
    char* argv[1] = {name};
    QApplication app (argc, argv);
    PropertyMap map;
    RobWorkStudio studio (map);
    QTemporaryDir base;
    ASSERT_TRUE (base.isValid ());

    ProjectResource model;
    model.id = QStringLiteral ("robot-model.main");
    model.kind = QStringLiteral ("robwork.robot-model");
    model.path = QStringLiteral ("generated/robot-models/Candidate.rmb.json");
    model.ownership = QStringLiteral ("generated");
    model.required = true;
    CallbackProjectDocumentProvider provider (
        QStringLiteral ("test.builder-new-root-provider"), model.kind,
        [] (const QString&, const ProjectDocumentContext&, QString*) { return true; },
        [] (const QString&, const ProjectDocumentContext&, QString*) { return true; });
    QString error;
    ASSERT_TRUE (studio.registerProjectDocumentProvider (&provider, &error));

    const QString projectRoot = base.filePath ("missing/child/project");
    const QString projectFile = QDir (projectRoot).filePath ("Candidate.rwproj");
    ASSERT_FALSE (QFileInfo::exists (projectRoot));
    NewRobotProjectCallbacks callbacks;
    setNoopNewRobotProjectStateCallbacks (callbacks);
    callbacks.preflight = [] (const QString&, QString*) { return true; };
    callbacks.requiredResources = [model] (const QString&, QVector< ProjectResource >& resources,
                                           QString*) {
        resources.push_back (model);
        return true;
    };
    callbacks.confirmClose = [] (QString*) { return true; };
    callbacks.bootstrap = [&] (const QString&, QString* callbackError) {
        bool created = false;
        if (!studio.ensureGeneratedProjectResource (model, &created, callbackError))
            return false;
        provider.adoptGeneratedResource (model.id);
        return false;
    };

    EXPECT_FALSE (studio.createProjectWithRobotModelBuilderPaths (
        projectFile, callbacks, &error));
    EXPECT_FALSE (QFileInfo::exists (projectRoot));
}

TEST (RobWorkStudio, NewRobotProjectRejectsBootstrapSuccessWithoutDeclaredActiveResource)
{
    int argc = 1;
    char name[] = "RobWorkStudio";
    char* argv[1] = {name};
    QApplication app (argc, argv);
    PropertyMap map;
    RobWorkStudio studio (map);
    QTemporaryDir current;
    QTemporaryDir target;
    ASSERT_TRUE (current.isValid () && target.isValid ());
    const QString currentProject = createEmptyProject (current.path ());
    studio.openFile (currentProject.toStdString ());
    const std::vector< std::string > recentBefore = recentProjects (studio);

    ProjectResource model;
    model.id = QStringLiteral ("robot-model.main");
    model.kind = QStringLiteral ("robwork.robot-model");
    model.path = QStringLiteral ("generated/robot-models/Candidate.rmb.json");
    model.ownership = QStringLiteral ("generated");
    model.required = true;
    CallbackProjectDocumentProvider provider (
        QStringLiteral ("test.builder-noop-success-provider"), model.kind,
        [] (const QString&, const ProjectDocumentContext&, QString*) { return true; },
        [] (const QString&, const ProjectDocumentContext&, QString*) { return true; });
    QString error;
    ASSERT_TRUE (studio.registerProjectDocumentProvider (&provider, &error));

    NewRobotProjectCallbacks callbacks;
    setNoopNewRobotProjectStateCallbacks (callbacks);
    callbacks.preflight = [] (const QString&, QString*) { return true; };
    callbacks.requiredResources = [model] (const QString&, QVector< ProjectResource >& resources,
                                           QString*) {
        resources.push_back (model);
        return true;
    };
    callbacks.confirmClose = [] (QString*) { return true; };
    callbacks.bootstrap = [] (const QString&, QString*) { return true; };

    const QString targetProject = target.filePath ("Noop.rwproj");
    EXPECT_FALSE (studio.createProjectWithRobotModelBuilderPaths (
        targetProject, callbacks, &error));
    EXPECT_FALSE (QFileInfo::exists (targetProject));
    EXPECT_EQ (QDir::cleanPath (current.path ()), QDir::cleanPath (studio.projectDirectory ()));
    EXPECT_EQ (recentBefore, recentProjects (studio));
}

TEST (RobWorkStudio, NewRobotProjectRejectsCleanBootstrapModelAndRollsBack)
{
    int argc = 1;
    char name[] = "RobWorkStudio";
    char* argv[1] = {name};
    QApplication app (argc, argv);
    PropertyMap map;
    RobWorkStudio studio (map);
    QTemporaryDir current;
    QTemporaryDir target;
    ASSERT_TRUE (current.isValid () && target.isValid ());
    studio.openFile (createEmptyProject (current.path ()).toStdString ());
    const std::vector< std::string > recentBefore = recentProjects (studio);

    ProjectResource model;
    model.id = QStringLiteral ("robot-model.main");
    model.kind = QStringLiteral ("robwork.robot-model");
    model.path = QStringLiteral ("generated/robot-models/Clean.rmb.json");
    model.ownership = QStringLiteral ("generated");
    model.required = true;
    CallbackProjectDocumentProvider provider (
        QStringLiteral ("test.clean-bootstrap-model-provider"), model.kind,
        [] (const QString&, const ProjectDocumentContext&, QString*) { return true; },
        CallbackProjectDocumentProvider::SaveHandler ());
    QString error;
    ASSERT_TRUE (studio.registerProjectDocumentProvider (&provider, &error))
        << error.toStdString ();

    NewRobotProjectCallbacks callbacks;
    setNoopNewRobotProjectStateCallbacks (callbacks);
    callbacks.preflight = [] (const QString&, QString*) { return true; };
    callbacks.requiredResources = [model] (const QString&, QVector< ProjectResource >& resources,
                                           QString*) {
        resources.push_back (model);
        return true;
    };
    callbacks.confirmClose = [] (QString*) { return true; };
    callbacks.bootstrap = [&] (const QString&, QString* callbackError) {
        bool created = false;
        if (!studio.ensureGeneratedProjectResource (model, &created, callbackError))
            return false;
        provider.adoptGeneratedResource (model.id);
        return created;
    };

    const QString targetProject = target.filePath ("Clean.rwproj");
    EXPECT_FALSE (studio.createProjectWithRobotModelBuilderPaths (
        targetProject, callbacks, &error));
    EXPECT_TRUE (error.contains (QStringLiteral ("dirty"), Qt::CaseInsensitive))
        << error.toStdString ();
    EXPECT_FALSE (QFileInfo::exists (targetProject));
    EXPECT_EQ (QDir::cleanPath (current.path ()), QDir::cleanPath (studio.projectDirectory ()));
    EXPECT_EQ (recentBefore, recentProjects (studio));
}

TEST (RobWorkStudio, NewRobotProjectRejectsUndeclaredBootstrapResourcesAndRollsBack)
{
    int argc = 1;
    char name[] = "RobWorkStudio";
    char* argv[1] = {name};
    QApplication app (argc, argv);
    struct ExtraCase
    {
        QString name;
        bool manifestOnly;
    };
    const QVector< ExtraCase > cases = {{QStringLiteral ("ManifestExtra"), true},
                                        {QStringLiteral ("ActiveExtra"), false}};
    for (const ExtraCase& testCase : cases) {
        PropertyMap map;
        RobWorkStudio studio (map);
        QTemporaryDir current;
        QTemporaryDir target;
        ASSERT_TRUE (current.isValid () && target.isValid ());
        studio.openFile (createEmptyProject (current.path ()).toStdString ());
        const std::vector< std::string > recentBefore = recentProjects (studio);
        SCOPED_TRACE (testCase.name.toStdString ());

        ProjectResource model;
        model.id = QStringLiteral ("robot-model.main");
        model.kind = QStringLiteral ("robwork.robot-model");
        model.path = QStringLiteral ("generated/robot-models/Declared.rmb.json");
        model.ownership = QStringLiteral ("generated");
        model.required = true;
        CallbackProjectDocumentProvider modelProvider (
            QStringLiteral ("test.exact-bootstrap-model-provider"), model.kind,
            [] (const QString&, const ProjectDocumentContext&, QString*) { return true; },
            CallbackProjectDocumentProvider::SaveHandler ());
        CallbackProjectDocumentProvider manifestOnlyProvider (
            QStringLiteral ("test.exact-bootstrap-manifest-only-provider"),
            QStringLiteral ("test.manifest-only-resource"),
            [] (const QString&, const ProjectDocumentContext&, QString*) { return true; },
            CallbackProjectDocumentProvider::SaveHandler ());
        QString error;
        ASSERT_TRUE (studio.registerProjectDocumentProvider (&modelProvider, &error));
        ASSERT_TRUE (studio.registerProjectDocumentProvider (&manifestOnlyProvider, &error));

        NewRobotProjectCallbacks callbacks;
        setNoopNewRobotProjectStateCallbacks (callbacks);
        callbacks.preflight = [] (const QString&, QString*) { return true; };
        callbacks.requiredResources = [model] (const QString&,
                                               QVector< ProjectResource >& resources,
                                               QString*) {
            resources.push_back (model);
            return true;
        };
        callbacks.confirmClose = [] (QString*) { return true; };
        callbacks.bootstrap = [&] (const QString&, QString* callbackError) {
            bool created = false;
            if (!studio.ensureGeneratedProjectResource (model, &created, callbackError))
                return false;
            modelProvider.adoptGeneratedResource (model.id);

            ProjectResource extra;
            extra.id = testCase.manifestOnly ? QStringLiteral ("candidate.manifest-extra")
                                             : QStringLiteral ("candidate.active-extra");
            extra.kind = testCase.manifestOnly ? QStringLiteral ("test.manifest-only-resource")
                                               : model.kind;
            extra.path = testCase.manifestOnly
                             ? QStringLiteral ("generated/manifest-extra.json")
                             : QStringLiteral ("generated/robot-models/Extra.rmb.json");
            extra.ownership = QStringLiteral ("generated");
            extra.required = true;
            QString ignored;
            const bool extraActivated =
                studio.ensureGeneratedProjectResource (extra, nullptr, &ignored);
            if (testCase.manifestOnly && extraActivated)
                return false;
            if (!testCase.manifestOnly && !extraActivated)
                return false;

            modelProvider.adoptGeneratedResource (model.id);
            modelProvider.markDirty ();
            studio.notifyProjectDocumentChanged ();
            return true;
        };

        const QString targetProject =
            target.filePath (testCase.name + QStringLiteral (".rwproj"));
        EXPECT_FALSE (studio.createProjectWithRobotModelBuilderPaths (
            targetProject, callbacks, &error));
        EXPECT_FALSE (error.isEmpty ());
        EXPECT_FALSE (QFileInfo::exists (targetProject));
        EXPECT_EQ (QDir::cleanPath (current.path ()), QDir::cleanPath (studio.projectDirectory ()));
        EXPECT_EQ (recentBefore, recentProjects (studio));
    }
}

TEST (RobWorkStudio, NewRobotProjectFailureRemovesAllNewFilesAndPreservesBaselineTree)
{
    int argc = 1;
    char name[] = "RobWorkStudio";
    char* argv[1] = {name};
    QApplication app (argc, argv);
    PropertyMap map;
    RobWorkStudio studio (map);
    QTemporaryDir current;
    QTemporaryDir target;
    ASSERT_TRUE (current.isValid () && target.isValid ());
    studio.openFile (createEmptyProject (current.path ()).toStdString ());

    const QString sentinelPath = target.filePath ("baseline/deep/sentinel.txt");
    ASSERT_TRUE (QDir ().mkpath (QFileInfo (sentinelPath).absolutePath ()));
    QFile sentinel (sentinelPath);
    ASSERT_TRUE (sentinel.open (QIODevice::WriteOnly));
    ASSERT_EQ (8, sentinel.write ("preserve"));
    sentinel.close ();

    ProjectResource model;
    model.id = QStringLiteral ("robot-model.main");
    model.kind = QStringLiteral ("robwork.robot-model");
    model.path = QStringLiteral ("generated/robot-models/Failure.rmb.json");
    model.ownership = QStringLiteral ("generated");
    model.required = true;
    CallbackProjectDocumentProvider provider (
        QStringLiteral ("test.inventory-failure-provider"), model.kind,
        [] (const QString&, const ProjectDocumentContext&, QString*) { return true; },
        CallbackProjectDocumentProvider::SaveHandler ());
    QString error;
    ASSERT_TRUE (studio.registerProjectDocumentProvider (&provider, &error));

    NewRobotProjectCallbacks callbacks;
    setNoopNewRobotProjectStateCallbacks (callbacks);
    callbacks.preflight = [] (const QString&, QString*) { return true; };
    callbacks.requiredResources = [model] (const QString&, QVector< ProjectResource >& resources,
                                           QString*) {
        resources.push_back (model);
        return true;
    };
    callbacks.confirmClose = [] (QString*) { return true; };
    callbacks.bootstrap = [&] (const QString& projectRoot, QString* callbackError) {
        const QString modelPath = QDir (projectRoot).filePath (model.path);
        const QString tempPath = QDir (projectRoot).filePath ("scratch/nested/bootstrap.tmp");
        if (!QDir ().mkpath (QFileInfo (modelPath).absolutePath ()) ||
            !QDir ().mkpath (QFileInfo (tempPath).absolutePath ()))
            return false;
        QFile output (modelPath);
        QFile temp (tempPath);
        if (!output.open (QIODevice::WriteOnly) || output.write ("model") != 5 ||
            !temp.open (QIODevice::WriteOnly) || temp.write ("temporary") != 9)
            return false;
        output.close ();
        temp.close ();
        bool created = false;
        if (!studio.ensureGeneratedProjectResource (model, &created, callbackError))
            return false;
        provider.adoptGeneratedResource (model.id);
        if (callbackError != nullptr)
            *callbackError = QStringLiteral ("Intentional bootstrap failure with undeclared files.");
        return false;
    };

    EXPECT_FALSE (studio.createProjectWithRobotModelBuilderPaths (
        target.filePath ("Failure.rwproj"), callbacks, &error));
    EXPECT_FALSE (QFileInfo::exists (target.filePath ("scratch")));
    EXPECT_FALSE (QFileInfo::exists (target.filePath ("generated")));
    QFile preserved (sentinelPath);
    ASSERT_TRUE (preserved.open (QIODevice::ReadOnly));
    EXPECT_EQ (QByteArray ("preserve"), preserved.readAll ());
}

TEST (RobWorkStudio, NewRobotProjectSuccessRejectsUndeclaredCreatedFile)
{
    int argc = 1;
    char name[] = "RobWorkStudio";
    char* argv[1] = {name};
    QApplication app (argc, argv);
    PropertyMap map;
    RobWorkStudio studio (map);
    QTemporaryDir current;
    QTemporaryDir target;
    ASSERT_TRUE (current.isValid () && target.isValid ());
    studio.openFile (createEmptyProject (current.path ()).toStdString ());
    const std::vector< std::string > recentBefore = recentProjects (studio);

    ProjectResource model;
    model.id = QStringLiteral ("robot-model.main");
    model.kind = QStringLiteral ("robwork.robot-model");
    model.path = QStringLiteral ("generated/robot-models/Success.rmb.json");
    model.ownership = QStringLiteral ("generated");
    model.required = true;
    CallbackProjectDocumentProvider provider (
        QStringLiteral ("test.inventory-success-provider"), model.kind,
        [] (const QString&, const ProjectDocumentContext&, QString*) { return true; },
        CallbackProjectDocumentProvider::SaveHandler ());
    QString error;
    ASSERT_TRUE (studio.registerProjectDocumentProvider (&provider, &error));

    NewRobotProjectCallbacks callbacks;
    setNoopNewRobotProjectStateCallbacks (callbacks);
    callbacks.preflight = [] (const QString&, QString*) { return true; };
    callbacks.requiredResources = [model] (const QString&, QVector< ProjectResource >& resources,
                                           QString*) {
        resources.push_back (model);
        return true;
    };
    callbacks.confirmClose = [] (QString*) { return true; };
    callbacks.bootstrap = [&] (const QString& projectRoot, QString* callbackError) {
        const QString modelPath = QDir (projectRoot).filePath (model.path);
        const QString extraPath = QDir (projectRoot).filePath ("scratch/output.bin");
        if (!QDir ().mkpath (QFileInfo (modelPath).absolutePath ()) ||
            !QDir ().mkpath (QFileInfo (extraPath).absolutePath ()))
            return false;
        QFile modelFile (modelPath);
        QFile extraFile (extraPath);
        if (!modelFile.open (QIODevice::WriteOnly) || modelFile.write ("model") != 5 ||
            !extraFile.open (QIODevice::WriteOnly) || extraFile.write ("extra") != 5)
            return false;
        modelFile.close ();
        extraFile.close ();
        bool created = false;
        if (!studio.ensureGeneratedProjectResource (model, &created, callbackError))
            return false;
        provider.adoptGeneratedResource (model.id);
        provider.markDirty ();
        studio.notifyProjectDocumentChanged ();
        return true;
    };

    const QString targetProject = target.filePath ("Success.rwproj");
    EXPECT_FALSE (studio.createProjectWithRobotModelBuilderPaths (
        targetProject, callbacks, &error));
    EXPECT_TRUE (error.contains (QStringLiteral ("undeclared"), Qt::CaseInsensitive))
        << error.toStdString ();
    EXPECT_FALSE (QFileInfo::exists (targetProject));
    EXPECT_FALSE (QFileInfo::exists (target.filePath ("scratch")));
    EXPECT_FALSE (QFileInfo::exists (target.filePath ("generated")));
    EXPECT_EQ (QDir::cleanPath (current.path ()), QDir::cleanPath (studio.projectDirectory ()));
    EXPECT_EQ (recentBefore, recentProjects (studio));
}

enum class DeclaredBootstrapMutation { CreateFile, OverwriteFile, CreateParentDirectory };

void expectNewRobotProjectRejectsDeclaredBootstrapMutation (DeclaredBootstrapMutation mutation)
{
    int argc = 1;
    char name[] = "RobWorkStudio";
    char* argv[1] = {name};
    QApplication app (argc, argv);
    PropertyMap map;
    RobWorkStudio studio (map);
    QTemporaryDir current;
    QTemporaryDir target;
    ASSERT_TRUE (current.isValid () && target.isValid ());
    const QString currentProject = createEmptyProject (current.path ());
    studio.openFile (currentProject.toStdString ());
    const std::vector< std::string > recentBefore = recentProjects (studio);

    ProjectResource model;
    model.id = QStringLiteral ("robot-model.main");
    model.kind = QStringLiteral ("robwork.robot-model");
    model.path = QStringLiteral ("generated/robot-models/MemoryOnly.rmb.json");
    model.ownership = QStringLiteral ("generated");
    model.required = true;
    CallbackProjectDocumentProvider provider (
        QStringLiteral ("test.memory-only-bootstrap-provider"), model.kind,
        [] (const QString&, const ProjectDocumentContext&, QString*) { return true; },
        CallbackProjectDocumentProvider::SaveHandler ());
    QString error;
    ASSERT_TRUE (studio.registerProjectDocumentProvider (&provider, &error))
        << error.toStdString ();

    QByteArray sentinel ("exact declared model sentinel\0", 30);
    sentinel.append ("binary suffix");
    const QString outputPath = target.filePath (model.path);
    if (mutation == DeclaredBootstrapMutation::OverwriteFile) {
        ASSERT_TRUE (QDir ().mkpath (QFileInfo (outputPath).absolutePath ()))
            << outputPath.toStdString ();
        QFile output (outputPath);
        ASSERT_TRUE (output.open (QIODevice::WriteOnly));
        ASSERT_EQ (sentinel.size (), output.write (sentinel));
    }

    NewRobotProjectCallbacks callbacks;
    setNoopNewRobotProjectStateCallbacks (callbacks);
    callbacks.preflight = [] (const QString&, QString*) { return true; };
    callbacks.requiredResources = [model] (const QString&, QVector< ProjectResource >& resources,
                                           QString*) {
        resources.push_back (model);
        return true;
    };
    callbacks.confirmClose = [] (QString*) { return true; };
    callbacks.bootstrap = [&] (const QString&, QString* callbackError) {
        const QString outputDirectory = QFileInfo (outputPath).absolutePath ();
        if (!QDir ().mkpath (outputDirectory))
            return false;
        if (mutation != DeclaredBootstrapMutation::CreateParentDirectory) {
            QFile output (outputPath);
            if (!output.open (QIODevice::WriteOnly | QIODevice::Truncate) ||
                output.write ("bootstrap wrote model") != 21)
                return false;
        }
        bool created = false;
        if (!studio.ensureGeneratedProjectResource (model, &created, callbackError))
            return false;
        provider.adoptGeneratedResource (model.id);
        provider.markDirty ();
        studio.notifyProjectDocumentChanged ();
        return true;
    };

    const QString targetProject = target.filePath (QStringLiteral ("MemoryOnly.rwproj"));
    EXPECT_FALSE (studio.createProjectWithRobotModelBuilderPaths (
        targetProject, callbacks, &error));
    EXPECT_FALSE (error.isEmpty ());
    EXPECT_FALSE (QFileInfo::exists (targetProject));
    EXPECT_EQ (QDir::cleanPath (current.path ()), QDir::cleanPath (studio.projectDirectory ()));
    EXPECT_EQ (recentBefore, recentProjects (studio));

    if (mutation == DeclaredBootstrapMutation::OverwriteFile) {
        QFile restored (outputPath);
        ASSERT_TRUE (restored.open (QIODevice::ReadOnly));
        EXPECT_EQ (sentinel, restored.readAll ());
    }
    else {
        EXPECT_FALSE (QFileInfo::exists (outputPath));
        EXPECT_FALSE (QFileInfo::exists (target.filePath (QStringLiteral ("generated"))));
    }
}

TEST (RobWorkStudio, NewRobotProjectBootstrapSuccessRejectsCreatedDeclaredOutput)
{
    expectNewRobotProjectRejectsDeclaredBootstrapMutation (
        DeclaredBootstrapMutation::CreateFile);
}

TEST (RobWorkStudio, NewRobotProjectBootstrapSuccessRejectsModifiedDeclaredOutputAndRestoresBytes)
{
    expectNewRobotProjectRejectsDeclaredBootstrapMutation (
        DeclaredBootstrapMutation::OverwriteFile);
}

TEST (RobWorkStudio, NewRobotProjectBootstrapSuccessRejectsCreatedDeclaredOutputParentDirectory)
{
    expectNewRobotProjectRejectsDeclaredBootstrapMutation (
        DeclaredBootstrapMutation::CreateParentDirectory);
}

TEST (RobWorkStudio, NewRobotProjectSuccessKeepsEmptyWorkCellAndDirtyBootstrapResource)
{
    int argc = 1;
    char name[] = "RobWorkStudio";
    char* argv[1] = {name};
    QApplication app (argc, argv);
    PropertyMap map;
    RobWorkStudio studio (map);
    QTemporaryDir current;
    QTemporaryDir target;
    ASSERT_TRUE (current.isValid () && target.isValid ());
    const QString currentProject = createEmptyProject (current.path ());
    const QString targetProject = target.filePath ("Success.rwproj");
    studio.openFile (currentProject.toStdString ());
    const std::vector< std::string > recentBefore = recentProjects (studio);

    CallbackProjectDocumentProvider modelProvider (
        QStringLiteral ("test.new-project-model-provider"),
        QStringLiteral ("robwork.robot-model"),
        [] (const QString&, const ProjectDocumentContext&, QString*) { return true; },
        [] (const QString& targetPath, const ProjectDocumentContext&, QString*) {
            QFile output (targetPath);
            return output.open (QIODevice::WriteOnly | QIODevice::Truncate);
        });
    QString error;
    ASSERT_TRUE (studio.registerProjectDocumentProvider (&modelProvider, &error))
        << error.toStdString ();

    NewRobotProjectCallbacks callbacks;
    setNoopNewRobotProjectStateCallbacks (callbacks);
    callbacks.preflight = [] (const QString&, QString*) { return true; };
    callbacks.requiredResources = [] (const QString&, QVector< ProjectResource >& resources,
                                      QString*) {
        ProjectResource model;
        model.id = QStringLiteral ("robot-model.main");
        model.kind = QStringLiteral ("robwork.robot-model");
        model.path = QStringLiteral ("generated/robot-models/TestRobot.rmb.json");
        model.ownership = QStringLiteral ("generated");
        model.required = true;
        resources.push_back (model);
        return true;
    };
    callbacks.confirmClose = [] (QString*) { return true; };
    callbacks.bootstrap = [&] (const QString& projectRoot, QString* callbackError) {
        ProjectResource model;
        model.id = QStringLiteral ("robot-model.main");
        model.kind = QStringLiteral ("robwork.robot-model");
        model.path = QStringLiteral ("generated/robot-models/TestRobot.rmb.json");
        model.ownership = QStringLiteral ("generated");
        model.required = true;
        bool created = false;
        if (!studio.ensureGeneratedProjectResource (model, &created, callbackError))
            return false;
        modelProvider.adoptGeneratedResource (model.id);
        modelProvider.markDirty ();
        studio.notifyProjectDocumentChanged ();
        return created && QFileInfo (projectRoot).isDir ();
    };

    ASSERT_TRUE (studio.createProjectWithRobotModelBuilderPaths (
        targetProject, callbacks, &error)) << error.toStdString ();
    EXPECT_EQ (QDir::cleanPath (target.path ()), QDir::cleanPath (studio.projectDirectory ()));
    EXPECT_TRUE (studio.mainWorkCellResourceId ().isEmpty ());
    EXPECT_TRUE (studio.hasUnsavedProjectChanges ());
    EXPECT_TRUE (modelProvider.isDirty (QStringLiteral ("robot-model.main")));
    EXPECT_TRUE (QFileInfo::exists (targetProject));
    const std::vector< std::string > recentAfter = recentProjects (studio);
    ASSERT_FALSE (recentAfter.empty ());
    EXPECT_NE (recentBefore, recentAfter);
    EXPECT_EQ (targetProject.toStdString (), recentAfter.back ());

    QString generatedModelPath;
    EXPECT_TRUE (studio.resolveProjectResource (
        QStringLiteral ("robot-model.main"), generatedModelPath, &error));
    EXPECT_FALSE (QFileInfo::exists (generatedModelPath));
    EXPECT_FALSE (QFileInfo::exists (QFileInfo (generatedModelPath).absolutePath ()))
        << generatedModelPath.toStdString ();

    ASSERT_TRUE (studio.saveCurrentProject (&error)) << error.toStdString ();
    ProjectManager verification;
    ASSERT_TRUE (verification.openProject (targetProject, &error)) << error.toStdString ();
    ProjectResource model;
    EXPECT_TRUE (verification.manifest ().findResource (QStringLiteral ("robot-model.main"), model));
}

#ifndef WIN32
TEST (RobWorkStudio, PluginLoadTest)
{
    rws::RobWorkStudioApp rwsApp ("");
    rwsApp.start ();
    rws::RobWorkStudio* rwstudio = rwsApp.getRobWorkStudio ();
    TimerUtil::sleepMs (1000);
    std::vector< rws::RobWorkStudioPlugin* > pl = rwstudio->getPlugins ();

    std::vector< QString > plugins = {"ATaskVisPlugin",
                                      "PlayBack",
                                      "Jog",
                                      "Workcell Editor",
                                      "Log",
                                      "LuaConsole",
                                      "TreeView",
                                      "Planning",
                                      "PropertyView",
                                      "Sensors",
                                      "GTaskVisPlugin"};
    for (QString& pn : plugins) {
        bool exist = false;
        for (rws::RobWorkStudioPlugin* p : pl) {
            if (p->name () == pn) {
                exist = true;
                break;
            }
        }
        if (!exist) {    // Print som debug output
            std::cout << "Could not find '" << pn.toStdString () << "' in list: " << std::endl;
            for (rws::RobWorkStudioPlugin* p : pl) {
                std::cout << p->name ().toStdString () << ", ";
            }
            std::cout << std::endl;
        }

        EXPECT_TRUE (exist);
    }

    rwsApp.close ();
    TimerUtil::sleepMs (1000);
}
#endif
