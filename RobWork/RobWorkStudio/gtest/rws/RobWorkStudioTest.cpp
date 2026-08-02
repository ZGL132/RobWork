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
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QMetaObject>
#include <QString>
#include <QTemporaryDir>
#include <QTimer>
#include <gtest/gtest.h>

#include <algorithm>

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

}    // namespace

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
