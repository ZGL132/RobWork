#include <rws/ProjectManager.hpp>
#include <rws/ProjectManifestJson.hpp>
#include <rws/ProjectPathResolver.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <gtest/gtest.h>

namespace {

// 构造一个最小但完整的测试清单：一个名为“项目系统测试”的项目，含一个
// 位于 scenes/main.wc.xml 的必需 WorkCell 资源，并把 "mainWorkCell" 作为入口。
rws::ProjectManifest makeManifest ()
{
    rws::ProjectManifest manifest;
    manifest.project.id          = "project-test-id";
    manifest.project.name        = QString::fromUtf8 ("项目系统测试");
    manifest.project.description = QString::fromUtf8 ("验证项目清单的稳定往返行为");

    rws::ProjectResource workCell;
    workCell.id        = "scene.main";
    workCell.kind      = "robwork.workcell";
    workCell.path      = "scenes/main.wc.xml";
    workCell.required  = true;
    workCell.ownership = "project";
    manifest.resources.push_back (workCell);
    manifest.entryPoints.insert ("mainWorkCell", workCell.id);
    return manifest;
}

}    // namespace

// 核心往返测试：确认清单序列化后再反序列化，项目标识、入口资源和资源路径保持完全一致。
TEST (ProjectSystemTest, ManifestRoundTripPreservesCoreFields)
{
    // 项目文件是多个业务资源之间的稳定索引，因此测试必须确认序列化往返后，
    // 项目标识、入口资源和资源路径都不会发生隐式改写或丢失。
    const rws::ProjectManifest source = makeManifest ();
    const QByteArray json              = rws::ProjectManifestJson::toJson (source);

    rws::ProjectManifest parsed;
    QString error;
    ASSERT_TRUE (rws::ProjectManifestJson::fromJson (json, parsed, &error))
        << error.toStdString ();
    EXPECT_EQ (source.project.id, parsed.project.id);
    EXPECT_EQ (source.project.name, parsed.project.name);
    ASSERT_EQ (1, parsed.resources.size ());
    EXPECT_EQ ("scene.main", parsed.entryPoints.value ("mainWorkCell"));
    EXPECT_EQ ("scenes/main.wc.xml", parsed.resources.front ().path);
}

// 负向测试：format 字段不匹配时，即使其余字段看起来正确也必须拒绝加载。
TEST (ProjectSystemTest, ManifestRejectsUnknownFormat)
{
    // format 字段用于避免把任意 JSON 误判为项目文件。即使其他字段看起来正确，
    // 只要格式标识不匹配，也必须在加载任何资源前立即拒绝。
    QByteArray json = rws::ProjectManifestJson::toJson (makeManifest ());
    json.replace ("RobWorkStudioProject", "AnotherProjectFormat");

    rws::ProjectManifest parsed;
    QString error;
    EXPECT_FALSE (rws::ProjectManifestJson::fromJson (json, parsed, &error));
    EXPECT_FALSE (error.isEmpty ());
}

// 负向测试：资源 ID 重复属于不可恢复的清单错误，必须整体拒绝而非隐式去重。
TEST (ProjectSystemTest, ManifestRejectsDuplicateResourceIds)
{
    // 插件之间通过稳定资源 ID 建立依赖；重复 ID 会让解析结果取决于数组顺序，
    // 因而属于不可恢复的清单错误，不能隐式选择第一项或最后一项。
    rws::ProjectManifest manifest = makeManifest ();
    manifest.resources.push_back (manifest.resources.front ());

    rws::ProjectManifest parsed;
    QString error;
    EXPECT_FALSE (rws::ProjectManifestJson::fromJson (
        rws::ProjectManifestJson::toJson (manifest), parsed, &error));
    EXPECT_FALSE (error.isEmpty ());
}

// 负向测试：入口指向不存在的资源时必须拒绝，避免不同机器打开不同工作内容。
TEST (ProjectSystemTest, ManifestRejectsMissingEntryPointResource)
{
    // 入口资源是项目启动时自动加载的权威对象。入口失效时不能猜测其他同类型文件，
    // 否则同一项目在不同机器上可能打开不同的工作内容。
    rws::ProjectManifest manifest = makeManifest ();
    manifest.entryPoints["mainWorkCell"] = "scene.missing";

    rws::ProjectManifest parsed;
    QString error;
    EXPECT_FALSE (rws::ProjectManifestJson::fromJson (
        rws::ProjectManifestJson::toJson (manifest), parsed, &error));
    EXPECT_FALSE (error.isEmpty ());
}

// 正向测试：project 相对路径应解析为“项目目录 + 相对路径”的规范化绝对路径。
TEST (ProjectSystemTest, PathResolverResolvesProjectRelativeResource)
{
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString projectFile = QDir (directory.path ()).filePath ("Demo.rwproj");

    rws::ProjectResource resource;
    resource.id        = "scene.main";
    resource.kind      = "robwork.workcell";
    resource.path      = "scenes/main.wc.xml";
    resource.ownership = "project";

    QString resolved;
    QString error;
    ASSERT_TRUE (
        rws::ProjectPathResolver::resolveResource (projectFile, resource, resolved, &error))
        << error.toStdString ();
    EXPECT_EQ (QDir::cleanPath (QDir (directory.path ()).filePath ("scenes/main.wc.xml")),
               resolved);
}

// 安全测试：project 资源用 “../” 越出项目目录时必须被解析器拒绝。
TEST (ProjectSystemTest, PathResolverRejectsProjectEscape)
{
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString projectFile = QDir (directory.path ()).filePath ("Demo.rwproj");

    rws::ProjectResource resource;
    resource.id        = "scene.main";
    resource.kind      = "robwork.workcell";
    resource.path      = "../outside/main.wc.xml";
    resource.ownership = "project";

    QString resolved;
    QString error;
    EXPECT_FALSE (
        rws::ProjectPathResolver::resolveResource (projectFile, resource, resolved, &error));
    EXPECT_FALSE (error.isEmpty ());
}

// 端到端测试：完整走一遍 创建→关闭→重新打开 的项目生命周期。
TEST (ProjectSystemTest, ManagerCreatesSavesAndReopensProject)
{
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString projectFile = QDir (directory.path ()).filePath ("Demo.rwproj");

    rws::ProjectManager manager;
    QString error;
    ASSERT_TRUE (manager.createProject (projectFile, makeManifest (), &error))
        << error.toStdString ();
    ASSERT_TRUE (QFileInfo::exists (projectFile));
    EXPECT_TRUE (manager.hasProject ());
    EXPECT_EQ (QDir::cleanPath (projectFile), manager.projectFilePath ());

    manager.closeProject ();
    EXPECT_FALSE (manager.hasProject ());

    // main WorkCell 在清单中被标记为 required，因此重新打开前创建对应占位文件。
    // 此处只测试项目管理器的资源存在性门槛，不调用真正的 WorkCell XML 加载器。
    ASSERT_TRUE (QDir (directory.path ()).mkpath ("scenes"));
    QFile requiredWorkCell (QDir (directory.path ()).filePath ("scenes/main.wc.xml"));
    ASSERT_TRUE (requiredWorkCell.open (QIODevice::WriteOnly));
    requiredWorkCell.close ();

    ASSERT_TRUE (manager.openProject (projectFile, &error)) << error.toStdString ();
    EXPECT_EQ (QString::fromUtf8 ("项目系统测试"), manager.manifest ().project.name);
    EXPECT_EQ ("scene.main", manager.manifest ().entryPoints.value ("mainWorkCell"));
}

// 迁移测试：从历史 WorkCell 文件创建项目时，源文件必须被复制到项目内部，避免清单继续
// 引用原工作目录；创建后的项目重新打开时也必须能仅依赖项目目录正常解析入口资源。
TEST (ProjectSystemTest, ManagerCreatesProjectFromExistingWorkCell)
{
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString sourceDirectory = QDir (directory.path ()).filePath ("legacy");
    const QString projectDirectory = QDir (directory.path ()).filePath ("project");
    ASSERT_TRUE (QDir ().mkpath (sourceDirectory));

    const QString sourceWorkCell = QDir (sourceDirectory).filePath ("legacy.wc.xml");
    QFile sourceFile (sourceWorkCell);
    // 使用二进制方式写入，确保断言验证的是导入复制没有修改任何字节，而不是平台的文本换行转换。
    ASSERT_TRUE (sourceFile.open (QIODevice::WriteOnly));
    ASSERT_EQ (sourceFile.write ("<WorkCell />\n"), qint64 (13));
    sourceFile.close ();

    const QString projectFile = QDir (projectDirectory).filePath ("Migrated.rwproj");
    rws::ProjectManager manager;
    QString error;
    ASSERT_TRUE (manager.createProjectFromWorkCell (projectFile, sourceWorkCell, &error))
        << error.toStdString ();

    rws::ProjectResource workCell;
    ASSERT_TRUE (manager.manifest ().findResource ("scene.main", workCell));
    EXPECT_EQ ("robwork.workcell", workCell.kind);
    EXPECT_EQ ("scenes/main.wc.xml", workCell.path);
    EXPECT_EQ ("project", workCell.ownership);
    EXPECT_TRUE (workCell.required);
    EXPECT_EQ ("scene.main", manager.manifest ().entryPoints.value ("mainWorkCell"));

    QString copiedWorkCell;
    ASSERT_TRUE (manager.resolveResource ("scene.main", copiedWorkCell, &error))
        << error.toStdString ();
    EXPECT_TRUE (QFileInfo::exists (copiedWorkCell));
    EXPECT_NE (QDir::cleanPath (sourceWorkCell), QDir::cleanPath (copiedWorkCell));

    manager.closeProject ();
    ASSERT_TRUE (manager.openProject (projectFile, &error)) << error.toStdString ();
}

// 导入测试：业务 JSON 被复制到项目相对路径后才加入内存清单；资源归属未显式指定时默认
// 设为 project，重复 ID 必须拒绝，且已成功导入的文件和清单状态不能被失败请求破坏。
TEST (ProjectSystemTest, ManagerImportsLegacyResourceIntoCurrentProject)
{
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString projectDirectory = QDir (directory.path ()).filePath ("project");
    const QString sourceDirectory = QDir (directory.path ()).filePath ("legacy");
    ASSERT_TRUE (QDir ().mkpath (sourceDirectory));
    const QString projectFile = QDir (projectDirectory).filePath ("Demo.rwproj");

    rws::ProjectManifest manifest;
    manifest.project.id = "import-test";
    manifest.project.name = "Import Test";
    rws::ProjectManager manager;
    QString error;
    ASSERT_TRUE (manager.createProject (projectFile, manifest, &error)) << error.toStdString ();

    const QString sourceFilePath = QDir (sourceDirectory).filePath ("robot.rmb.json");
    QFile sourceFile (sourceFilePath);
    // 使用二进制方式写入，确保断言验证的是导入复制没有修改任何字节，而不是平台的文本换行转换。
    ASSERT_TRUE (sourceFile.open (QIODevice::WriteOnly));
    ASSERT_EQ (sourceFile.write ("{\"name\":\"legacy robot\"}\n"), qint64 (24));
    sourceFile.close ();

    rws::ProjectResource imported;
    imported.id = "model.legacy";
    imported.kind = "robwork.robot-model";
    imported.path = "models/robot.rmb.json";
    ASSERT_TRUE (manager.importResource (sourceFilePath, imported, &error)) << error.toStdString ();
    EXPECT_TRUE (manager.isDirty ());

    rws::ProjectResource stored;
    ASSERT_TRUE (manager.manifest ().findResource (imported.id, stored));
    EXPECT_EQ ("project", stored.ownership);
    QString importedPath;
    ASSERT_TRUE (manager.resolveResource (imported.id, importedPath, &error))
        << error.toStdString ();
    QFile importedFile (importedPath);
    ASSERT_TRUE (importedFile.open (QIODevice::ReadOnly));
    EXPECT_EQ (QByteArray ("{\"name\":\"legacy robot\"}\n"), importedFile.readAll ());

    const int resourceCount = manager.manifest ().resources.size ();
    EXPECT_FALSE (manager.importResource (sourceFilePath, imported, &error));
    EXPECT_FALSE (error.isEmpty ());
    EXPECT_EQ (resourceCount, manager.manifest ().resources.size ());
}

// 可搬迁性测试：项目克隆仅复制项目内资源并创建新的项目 ID；随后整体移动项目目录后，
// 相对资源必须改按新目录解析，而 external 资源仍保持原有的绝对路径语义。
TEST (ProjectSystemTest, ManagerClonesPortableProjectWithNewIdentity)
{
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString sourceDirectory = QDir (directory.path ()).filePath ("source");
    const QString cloneDirectory = QDir (directory.path ()).filePath ("clone");
    const QString movedDirectory = QDir (directory.path ()).filePath ("moved");
    ASSERT_TRUE (QDir ().mkpath (QDir (sourceDirectory).filePath ("scenes")));
    ASSERT_TRUE (QDir ().mkpath (QDir (sourceDirectory).filePath ("models")));

    const QString sourceWorkCell = QDir (sourceDirectory).filePath ("scenes/main.wc.xml");
    const QString sourceModel = QDir (sourceDirectory).filePath ("models/robot.rmb.json");
    const QString externalFile = QDir (directory.path ()).filePath ("external.json");
    for (const QString& filename : {sourceWorkCell, sourceModel, externalFile}) {
        QFile file (filename);
        ASSERT_TRUE (file.open (QIODevice::WriteOnly | QIODevice::Text));
        ASSERT_TRUE (file.write (filename.toUtf8 ()) > 0);
    }

    rws::ProjectManifest manifest = makeManifest ();
    rws::ProjectResource model;
    model.id = "model.main";
    model.kind = "robwork.robot-model";
    model.path = "models/robot.rmb.json";
    model.ownership = "project";
    manifest.resources.push_back (model);
    rws::ProjectResource external;
    external.id = "external.reference";
    external.kind = "external.reference";
    external.path = externalFile;
    external.ownership = "external";
    manifest.resources.push_back (external);

    const QString sourceProject = QDir (sourceDirectory).filePath ("Demo.rwproj");
    rws::ProjectManager manager;
    QString error;
    ASSERT_TRUE (manager.createProject (sourceProject, manifest, &error)) << error.toStdString ();
    const QString sourceProjectId = manager.manifest ().project.id;

    const QString clonedProject = QDir (cloneDirectory).filePath ("Copied.rwproj");
    ASSERT_TRUE (manager.cloneProject (clonedProject, &error)) << error.toStdString ();
    EXPECT_EQ (QDir::cleanPath (clonedProject), manager.projectFilePath ());
    EXPECT_NE (sourceProjectId, manager.manifest ().project.id);
    EXPECT_FALSE (manager.isDirty ());
    EXPECT_TRUE (QFileInfo::exists (QDir (cloneDirectory).filePath ("scenes/main.wc.xml")));
    EXPECT_TRUE (QFileInfo::exists (QDir (cloneDirectory).filePath ("models/robot.rmb.json")));

    // 克隆目录内不得出现 external 资源的副本；它仍由清单中的绝对路径显式引用。
    EXPECT_FALSE (QFileInfo::exists (QDir (cloneDirectory).filePath ("external.json")));

    ASSERT_TRUE (QDir (directory.path ()).rename ("clone", "moved"));
    const QString movedProject = QDir (movedDirectory).filePath ("Copied.rwproj");
    rws::ProjectManager reopened;
    ASSERT_TRUE (reopened.openProject (movedProject, &error)) << error.toStdString ();

    QString movedWorkCell;
    ASSERT_TRUE (reopened.resolveResource ("scene.main", movedWorkCell, &error))
        << error.toStdString ();
    EXPECT_EQ (QDir::cleanPath (QDir (movedDirectory).filePath ("scenes/main.wc.xml")),
               movedWorkCell);
    QString resolvedExternal;
    ASSERT_TRUE (reopened.resolveResource ("external.reference", resolvedExternal, &error))
        << error.toStdString ();
    EXPECT_EQ (QDir::cleanPath (externalFile), resolvedExternal);
}
