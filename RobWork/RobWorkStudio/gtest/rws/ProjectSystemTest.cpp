#include <rws/ProjectManager.hpp>
#include <rws/ProjectManifestJson.hpp>
#include <rws/ProjectPathResolver.hpp>

#include <rw/loaders/WorkCellLoader.hpp>
#include <rw/models/Device.hpp>
#include <rw/models/WorkCell.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <zip.h>

#include <algorithm>

namespace {

QByteArray readFile (const QString& path)
{
    QFile file (path);
    if (!file.open (QIODevice::ReadOnly))
        return {};
    return file.readAll ();
}

QString sourcePath (const QString& relativePath)
{
    return QDir (QStringLiteral (RWS_TEST_SOURCE_DIR)).filePath (relativePath);
}

int managedGeometryResourceCount (const rws::ProjectManifest& manifest)
{
    int count = 0;
    for (const rws::ProjectResource& resource : manifest.resources) {
        if (resource.path.startsWith (QStringLiteral ("scenes/geometry/")))
            ++count;
    }
    return count;
}

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
    const QByteArray workCellXml ("<WorkCell name=\"Legacy\" />\n");
    ASSERT_EQ (sourceFile.write (workCellXml), workCellXml.size ());
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
// 回归测试：迁移 WorkCell 时入口 XML 的相对 Include 也必须复制到项目内对应位置。只复制
// scenes/main.wc.xml 会把解析基准从源目录改为 scenes，随后 WorkCellLoader 找不到被 Include
// 的设备 XML 并在打开项目时失败。
TEST (ProjectSystemTest, ManagerCopiesRelativeWorkCellDependenciesIntoProject)
{
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString sourceDirectory = QDir (directory.path ()).filePath ("legacy");
    const QString projectDirectory = QDir (directory.path ()).filePath ("project");
    ASSERT_TRUE (QDir ().mkpath (sourceDirectory));

    const QString sourceWorkCell = QDir (sourceDirectory).filePath ("scene.wc.xml");
    const QString sourceDevice = QDir (sourceDirectory).filePath ("robot.wc.xml");
    const QString sourceGeometry = QDir (sourceDirectory).filePath ("geometry/shape.stl");
    const QByteArray sceneXml (
        "<WorkCell name=\"MigratedScene\">\n"
        "  <Include file=\"robot.wc.xml\" />\n"
        "</WorkCell>\n");
    const QByteArray deviceXml (
        "<SerialDevice name=\"MigratedRobot\">\n"
        "  <Frame name=\"Base\" />\n"
        "  <Drawable name=\"RobotGeometry\" refframe=\"Base\">\n"
        // RobWork 的 WorkCell XML 允许几何引用省略扩展名；真实模型包常同时提供 .ac、.stl
        // 等同基名文件，迁移器必须复制这些候选文件，不能把无扩展名文本误当作真实文件名。
        "    <Polytope file=\"geometry/shape\" />\n"
        "  </Drawable>\n"
        "</SerialDevice>\n");
    const QByteArray geometryData (
        "solid shape\n"
        "  facet normal 0 0 1\n"
        "    outer loop\n"
        "      vertex 0 0 0\n"
        "      vertex 1 0 0\n"
        "      vertex 0 1 0\n"
        "    endloop\n"
        "  endfacet\n"
        "endsolid shape\n");
    for (const auto& source : {std::make_pair (sourceWorkCell, sceneXml),
                               std::make_pair (sourceDevice, deviceXml)}) {
        QFile file (source.first);
        ASSERT_TRUE (file.open (QIODevice::WriteOnly));
        ASSERT_EQ (source.second.size (), file.write (source.second));
    }
    ASSERT_TRUE (QDir ().mkpath (QFileInfo (sourceGeometry).absolutePath ()));
    QFile geometryFile (sourceGeometry);
    ASSERT_TRUE (geometryFile.open (QIODevice::WriteOnly));
    ASSERT_EQ (geometryData.size (), geometryFile.write (geometryData));
    // 在调用迁移逻辑前关闭写入端，确保 QFile 缓冲区已刷新到磁盘，测试验证的才是复制器读取
    // 的真实源文件内容，而不是尚停留在测试进程内存中的未提交数据。
    geometryFile.close ();

    rws::ProjectManager manager;
    QString error;
    const QString projectFile = QDir (projectDirectory).filePath ("Migrated.rwproj");
    ASSERT_TRUE (manager.createProjectFromWorkCell (projectFile, sourceWorkCell, &error))
        << error.toStdString ();

    const QString copiedDevice = QDir (projectDirectory).filePath ("scenes/robot.wc.xml");
    QFile copiedDeviceFile (copiedDevice);
    ASSERT_TRUE (copiedDeviceFile.open (QIODevice::ReadOnly));
    EXPECT_EQ (deviceXml, copiedDeviceFile.readAll ());

    // XML 依赖树中的非 XML 网格/多面体文件同样要迁移；它们是叶子节点，无需再解析，但
    // 路径必须保留为 scenes/geometry/...，这样入口 XML 内的 Polytope 相对路径仍然有效。
    QFile copiedGeometry (QDir (projectDirectory).filePath ("scenes/geometry/shape.stl"));
    ASSERT_TRUE (copiedGeometry.open (QIODevice::ReadOnly));
    EXPECT_EQ (geometryData, copiedGeometry.readAll ());

    // 被入口 XML 间接引用的文件也是项目资产。若它们没有进入 manifest，clone/rwpack
    // 只复制 main.wc.xml 后会生成在原项目可用、迁移后损坏的工程。
    bool deviceManaged = false;
    bool geometryManaged = false;
    for (const rws::ProjectResource& resource : manager.manifest ().resources) {
        deviceManaged = deviceManaged || resource.path == QStringLiteral ("scenes/robot.wc.xml");
        geometryManaged = geometryManaged ||
                          resource.path == QStringLiteral ("scenes/geometry/shape.stl");
    }
    EXPECT_TRUE (deviceManaged);
    EXPECT_TRUE (geometryManaged);
}

TEST (ProjectSystemTest, ManagerCreatesLoadableProjectFromTopLevelUrDevice)
{
    QTemporaryDir target;
    ASSERT_TRUE (target.isValid ());
    const QString source = sourcePath (
        QStringLiteral ("RobWork/example/ModelData/XMLDevices/UR-6-85-5-A/UR.wc.xml"));
    const QByteArray sourceBefore = readFile (source);
    ASSERT_FALSE (sourceBefore.isEmpty ());

    rws::ProjectManager manager;
    QString error;
    const QString projectFile =
        QDir (target.path ()).filePath (QStringLiteral ("UrProject/UrProject.rwproj"));
    ASSERT_TRUE (manager.createProjectFromWorkCell (projectFile, source, &error))
        << error.toStdString ();

    QString scenePath;
    ASSERT_TRUE (manager.resolveResource (QStringLiteral ("scene.main"), scenePath, &error))
        << error.toStdString ();
    const rw::models::WorkCell::Ptr workcell =
        rw::loaders::WorkCellLoader::Factory::load (scenePath.toStdString ());
    ASSERT_FALSE (workcell.isNull ());
    EXPECT_FALSE (workcell->findDevice ("UR-6-85-5-A").isNull ());
    EXPECT_EQ (sourceBefore, readFile (source));
    EXPECT_EQ (16, managedGeometryResourceCount (manager.manifest ()))
        << "Every .ac and .stl dependency must be a managed project resource.";
}

TEST (ProjectSystemTest, ManagerRejectsCopiedWorkCellThatRobWorkCannotLoad)
{
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString sourceDirectory = QDir (directory.path ()).filePath (QStringLiteral ("source"));
    ASSERT_TRUE (QDir ().mkpath (sourceDirectory));
    const QString source = QDir (sourceDirectory).filePath (QStringLiteral ("invalid.wc.xml"));
    QFile sourceFile (source);
    ASSERT_TRUE (sourceFile.open (QIODevice::WriteOnly));
    ASSERT_GT (sourceFile.write ("<UnsupportedRobWorkDocument />\n"), 0);
    sourceFile.close ();

    rws::ProjectManager manager;
    const QString projectDirectory = QDir (directory.path ()).filePath (QStringLiteral ("project"));
    const QString projectFile = QDir (projectDirectory).filePath (QStringLiteral ("Invalid.rwproj"));
    QString error;
    EXPECT_FALSE (manager.createProjectFromWorkCell (projectFile, source, &error));
    EXPECT_FALSE (error.isEmpty ());
    EXPECT_FALSE (manager.hasProject ());
    EXPECT_FALSE (QFileInfo::exists (projectFile));
    EXPECT_FALSE (QFileInfo::exists (
        QDir (projectDirectory).filePath (QStringLiteral ("scenes/main.wc.xml"))));
    EXPECT_TRUE (QFileInfo::exists (source));
}

TEST (ProjectSystemTest, ManagerPromotesGeneratedWorkCellWithoutChangingStableEntryId)
{
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString projectFile = QDir (directory.path ()).filePath ("Demo.rwproj");

    rws::ProjectManager manager;
    QString error;
    ASSERT_TRUE (manager.createProject (projectFile, makeManifest (), &error))
        << error.toStdString ();

    rws::ProjectResource deviceAsset;
    deviceAsset.id = QStringLiteral ("scene.generated.device");
    deviceAsset.kind = QStringLiteral ("robwork.passive-asset");
    deviceAsset.path = QStringLiteral ("generated/robot-models/Robot.wc.xml");
    deviceAsset.ownership = QStringLiteral ("generated");
    deviceAsset.required = true;

    rws::ProjectResource promoted;
    ASSERT_TRUE (manager.manifest ().findResource (QStringLiteral ("scene.main"), promoted));
    promoted.path = QStringLiteral ("generated/robot-models/RobotScene.wc.xml");
    promoted.ownership = QStringLiteral ("generated");
    promoted.dependencies = QStringList () << deviceAsset.id;

    ASSERT_TRUE (manager.replaceResourceAndAddAssets (
        promoted, QVector< rws::ProjectResource > () << deviceAsset, &error))
        << error.toStdString ();

    EXPECT_EQ (QStringLiteral ("scene.main"),
               manager.manifest ().entryPoints.value (QStringLiteral ("mainWorkCell")));
    rws::ProjectResource stored;
    ASSERT_TRUE (manager.manifest ().findResource (QStringLiteral ("scene.main"), stored));
    EXPECT_EQ (promoted.path, stored.path);
    EXPECT_EQ (QStringLiteral ("generated"), stored.ownership);
    EXPECT_EQ (QStringList () << deviceAsset.id, stored.dependencies);
    ASSERT_TRUE (manager.manifest ().findResource (deviceAsset.id, stored));
    EXPECT_EQ (deviceAsset.path, stored.path);
    EXPECT_TRUE (manager.isDirty ());
}

TEST (ProjectSystemTest, ManagerRejectsInvalidResourceReplacementWithoutChangingManifest)
{
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString projectFile = QDir (directory.path ()).filePath ("Demo.rwproj");
    rws::ProjectManager manager;
    QString error;
    ASSERT_TRUE (manager.createProject (projectFile, makeManifest (), &error))
        << error.toStdString ();

    const rws::ProjectManifest before = manager.manifest ();
    const bool dirtyBefore = manager.isDirty ();
    rws::ProjectResource invalid;
    ASSERT_TRUE (before.findResource (QStringLiteral ("scene.main"), invalid));
    invalid.dependencies = QStringList () << QStringLiteral ("missing.upstream");
    EXPECT_FALSE (manager.replaceResourceAndAddAssets (
        invalid, QVector< rws::ProjectResource > (), &error));
    EXPECT_FALSE (error.isEmpty ());
    EXPECT_EQ (dirtyBefore, manager.isDirty ());
    EXPECT_EQ (rws::ProjectManifestJson::toJson (before),
               rws::ProjectManifestJson::toJson (manager.manifest ()));
}

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
// 正向测试：插件首次编辑生成的业务资源必须只登记在当前项目根目录下的相对路径，并且在
// 用户执行“保存项目”前不创建空文件。文件创建权属于 ProjectSaveTransaction，避免清单
// 和业务 JSON 在不同操作中分别落盘后形成无法恢复的半完成项目。
TEST (ProjectSystemTest, ManagerRegistersGeneratedResourceInsideProjectWithoutCreatingFile)
{
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString projectFile = QDir (directory.path ()).filePath ("Demo.rwproj");

    rws::ProjectManager manager;
    QString error;
    ASSERT_TRUE (manager.createProject (projectFile, makeManifest (), &error))
        << error.toStdString ();

    rws::ProjectResource analysis;
    analysis.id = "kinematic-analysis.main";
    analysis.kind = "rws.kinematic-analysis";
    analysis.path = "analysis/kinematic-analysis.json";
    analysis.ownership = "project";
    analysis.required = false;
    // 设备和 TCP 名称从入口 WorkCell 解析，显式依赖确保项目重新打开时加载顺序稳定。
    analysis.dependencies = {"scene.main"};

    ASSERT_TRUE (manager.addGeneratedResource (analysis, &error)) << error.toStdString ();
    EXPECT_TRUE (manager.isDirty ());

    rws::ProjectResource stored;
    ASSERT_TRUE (manager.manifest ().findResource (analysis.id, stored));
    EXPECT_EQ (analysis.path, stored.path);
    EXPECT_EQ (QStringList ({"scene.main"}), stored.dependencies);

    QString resolvedPath;
    ASSERT_TRUE (manager.resolveResource (analysis.id, resolvedPath, &error))
        << error.toStdString ();
    EXPECT_EQ (QDir::cleanPath (
                   QDir (directory.path ()).filePath ("analysis/kinematic-analysis.json")),
               resolvedPath);
    EXPECT_FALSE (QFileInfo::exists (resolvedPath));
}

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

// 恢复测试：自动恢复快照必须同时保存项目清单和项目自有资源。模拟异常退出后，磁盘上的
// 清单及 WorkCell 都可能已经被后续错误写入；恢复操作应以最后一个完整快照为准，而不是
// 只恢复 .rwproj 清单后留下与清单版本不匹配的旧资源文件。
TEST (ProjectSystemTest, ManagerRestoresAutosaveSnapshotWithOwnedResources)
{
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString projectFile = QDir (directory.path ()).filePath ("Recovery.rwproj");
    const QString workCellFile = QDir (directory.path ()).filePath ("scenes/main.wc.xml");
    const QByteArray originalWorkCell ("<WorkCell name=\"Recovered\" />\n");
    const QByteArray corruptedWorkCell ("<WorkCell name=\"Corrupted\" />\n");

    ASSERT_TRUE (QDir ().mkpath (QFileInfo (workCellFile).absolutePath ()));
    QFile workCell (workCellFile);
    ASSERT_TRUE (workCell.open (QIODevice::WriteOnly));
    ASSERT_EQ (originalWorkCell.size (), workCell.write (originalWorkCell));
    workCell.close ();

    rws::ProjectManager manager;
    QString error;
    ASSERT_TRUE (manager.createProject (projectFile, makeManifest (), &error)) << error.toStdString ();
    ASSERT_TRUE (manager.createAutosaveSnapshot (&error)) << error.toStdString ();
    EXPECT_TRUE (manager.hasAutosaveSnapshot ());

    ASSERT_TRUE (workCell.open (QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_EQ (corruptedWorkCell.size (), workCell.write (corruptedWorkCell));
    workCell.close ();
    QFile corruptedManifest (projectFile);
    ASSERT_TRUE (corruptedManifest.open (QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_EQ (qint64 (9), corruptedManifest.write ("{broken}\n"));
    corruptedManifest.close ();

    ASSERT_TRUE (manager.restoreAutosaveSnapshot (&error)) << error.toStdString ();
    QFile restoredWorkCell (workCellFile);
    ASSERT_TRUE (restoredWorkCell.open (QIODevice::ReadOnly));
    EXPECT_EQ (originalWorkCell, restoredWorkCell.readAll ());

    rws::ProjectManifest restoredManifest;
    QFile restoredManifestFile (projectFile);
    ASSERT_TRUE (restoredManifestFile.open (QIODevice::ReadOnly));
    ASSERT_TRUE (rws::ProjectManifestJson::fromJson (
        restoredManifestFile.readAll (), restoredManifest, &error)) << error.toStdString ();
    EXPECT_EQ ("scene.main", restoredManifest.entryPoints.value ("mainWorkCell"));
}

TEST (ProjectSystemTest, RestoreRejectsInvalidTargetWithoutChangingResources)
{
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString projectFile = QDir (directory.path ()).filePath ("Recovery.rwproj");
    const QString firstFile = QDir (directory.path ()).filePath ("scenes/main.wc.xml");
    const QString secondFile = QDir (directory.path ()).filePath ("analysis/result.json");
    ASSERT_TRUE (QDir ().mkpath (QFileInfo (firstFile).absolutePath ()));
    ASSERT_TRUE (QDir ().mkpath (QFileInfo (secondFile).absolutePath ()));
    for (const QString& path : {firstFile, secondFile}) {
        QFile file (path);
        ASSERT_TRUE (file.open (QIODevice::WriteOnly));
        ASSERT_EQ (qint64 (8), file.write ("snapshot"));
    }

    rws::ProjectManifest manifest = makeManifest ();
    rws::ProjectResource analysis;
    analysis.id = "analysis.result";
    analysis.kind = "test.analysis";
    analysis.path = "analysis/result.json";
    analysis.ownership = "project";
    manifest.resources.push_back (analysis);

    rws::ProjectManager manager;
    QString error;
    ASSERT_TRUE (manager.createProject (projectFile, manifest, &error)) << error.toStdString ();
    ASSERT_TRUE (manager.createAutosaveSnapshot (&error)) << error.toStdString ();

    QFile first (firstFile);
    ASSERT_TRUE (first.open (QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_EQ (qint64 (7), first.write ("current"));
    first.close ();
    ASSERT_TRUE (QFile::remove (secondFile));
    ASSERT_TRUE (QDir ().mkpath (secondFile));

    EXPECT_FALSE (manager.restoreAutosaveSnapshot (&error));
    QFile restoredFirst (firstFile);
    ASSERT_TRUE (restoredFirst.open (QIODevice::ReadOnly));
    EXPECT_EQ (QByteArray ("current"), restoredFirst.readAll ());
    EXPECT_TRUE (QFileInfo (secondFile).isDir ());
}

// 诊断测试：完整性检查必须区分“清单指向的资源丢失”“目录内未登记的遗留文件”和
// “相对自动保存快照已变更”的资源，供上层界面分别给出恢复、清理或保存提示。
TEST (ProjectSystemTest, ManagerReportsMissingUnreferencedAndChangedResources)
{
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString projectFile = QDir (directory.path ()).filePath ("Integrity.rwproj");
    const QString workCellFile = QDir (directory.path ()).filePath ("scenes/main.wc.xml");
    ASSERT_TRUE (QDir ().mkpath (QFileInfo (workCellFile).absolutePath ()));
    QFile workCell (workCellFile);
    ASSERT_TRUE (workCell.open (QIODevice::WriteOnly));
    ASSERT_EQ (qint64 (12), workCell.write ("<WorkCell />"));
    workCell.close ();

    rws::ProjectManager manager;
    QString error;
    ASSERT_TRUE (manager.createProject (projectFile, makeManifest (), &error)) << error.toStdString ();
    ASSERT_TRUE (manager.createAutosaveSnapshot (&error)) << error.toStdString ();
    ASSERT_TRUE (workCell.open (QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_EQ (qint64 (26), workCell.write ("<WorkCell name=\"Changed\"/>"));
    workCell.close ();
    QFile orphan (QDir (directory.path ()).filePath ("legacy-note.txt"));
    ASSERT_TRUE (orphan.open (QIODevice::WriteOnly));
    orphan.close ();

    const QVector< rws::ProjectManager::IntegrityIssue > issues = manager.inspectIntegrity (&error);
    EXPECT_TRUE (error.isEmpty ());
    EXPECT_TRUE (std::any_of (issues.cbegin (), issues.cend (), [] (const auto& issue) {
        return issue.type == rws::ProjectManager::IntegrityIssue::Type::ChangedSinceAutosave &&
            issue.resourceId == "scene.main";
    }));
    EXPECT_TRUE (std::any_of (issues.cbegin (), issues.cend (), [] (const auto& issue) {
        return issue.type == rws::ProjectManager::IntegrityIssue::Type::UnreferencedFile &&
            issue.path.endsWith ("legacy-note.txt");
    }));
}

// 归档回归：rwpack 必须使用项目相对目录保存自有资源。解包到全新目录后，清单及其
// WorkCell 仍可按原路径解析，证明归档没有携带机器相关的绝对路径。
TEST (ProjectSystemTest, ManagerPackagesAndExtractsPortableProject)
{
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString projectFile = QDir (directory.path ()).filePath ("Source.rwproj");
    const QString workCellFile = QDir (directory.path ()).filePath ("scenes/main.wc.xml");
    ASSERT_TRUE (QDir ().mkpath (QFileInfo (workCellFile).absolutePath ()));
    QFile workCell (workCellFile);
    ASSERT_TRUE (workCell.open (QIODevice::WriteOnly));
    ASSERT_EQ (qint64 (13), workCell.write ("<WorkCell />\n"));
    workCell.close ();

    rws::ProjectManager source;
    QString error;
    ASSERT_TRUE (source.createProject (projectFile, makeManifest (), &error)) << error.toStdString ();
    const QString packageFile = QDir (directory.path ()).filePath ("portable.rwpack");
    ASSERT_TRUE (source.exportPackage (packageFile, &error)) << error.toStdString ();

    QString extractedProject;
    const QString extractedDirectory = QDir (directory.path ()).filePath ("extracted");
    ASSERT_TRUE (rws::ProjectManager::extractPackage (
        packageFile, extractedDirectory, extractedProject, &error)) << error.toStdString ();
    rws::ProjectManager reopened;
    ASSERT_TRUE (reopened.openProject (extractedProject, &error)) << error.toStdString ();
    EXPECT_TRUE (QFileInfo::exists (QDir (extractedDirectory).filePath ("scenes/main.wc.xml")));
}

TEST (ProjectSystemTest, PackageUsesCurrentInMemoryManifest)
{
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString projectFile = QDir (directory.path ()).filePath ("Source.rwproj");
    const QString workCellFile = QDir (directory.path ()).filePath ("scenes/main.wc.xml");
    const QString analysisFile = QDir (directory.path ()).filePath ("analysis/result.json");
    ASSERT_TRUE (QDir ().mkpath (QFileInfo (workCellFile).absolutePath ()));
    ASSERT_TRUE (QDir ().mkpath (QFileInfo (analysisFile).absolutePath ()));
    for (const QString& path : {workCellFile, analysisFile}) {
        QFile file (path);
        ASSERT_TRUE (file.open (QIODevice::WriteOnly));
        file.write ("content");
    }

    rws::ProjectManager manager;
    QString error;
    ASSERT_TRUE (manager.createProject (projectFile, makeManifest (), &error)) << error.toStdString ();
    rws::ProjectResource analysis;
    analysis.id = "analysis.result";
    analysis.kind = "test.analysis";
    analysis.path = "analysis/result.json";
    analysis.ownership = "project";
    ASSERT_TRUE (manager.addGeneratedResource (analysis, &error)) << error.toStdString ();

    const QString packageFile = QDir (directory.path ()).filePath ("portable.rwpack");
    ASSERT_TRUE (manager.exportPackage (packageFile, &error)) << error.toStdString ();
    QString extractedProject;
    const QString extractedDirectory = QDir (directory.path ()).filePath ("extracted");
    ASSERT_TRUE (rws::ProjectManager::extractPackage (
        packageFile, extractedDirectory, extractedProject, &error)) << error.toStdString ();

    rws::ProjectManager reopened;
    ASSERT_TRUE (reopened.openProject (extractedProject, &error)) << error.toStdString ();
    rws::ProjectResource extractedAnalysis;
    EXPECT_TRUE (reopened.manifest ().findResource ("analysis.result", extractedAnalysis));
}

TEST (ProjectSystemTest, ExtractRejectsPackageMissingDeclaredOwnedResource)
{
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString projectFile = QDir (directory.path ()).filePath ("Source.rwproj");
    const QString workCellFile = QDir (directory.path ()).filePath ("scenes/main.wc.xml");
    ASSERT_TRUE (QDir ().mkpath (QFileInfo (workCellFile).absolutePath ()));
    QFile workCell (workCellFile);
    ASSERT_TRUE (workCell.open (QIODevice::WriteOnly));
    workCell.write ("<WorkCell />");
    workCell.close ();

    rws::ProjectManager manager;
    QString error;
    ASSERT_TRUE (manager.createProject (projectFile, makeManifest (), &error));
    const QString packageFile = QDir (directory.path ()).filePath ("broken.rwpack");
    ASSERT_TRUE (manager.exportPackage (packageFile, &error));

    int zipError = 0;
    zip_t* archive = zip_open (packageFile.toUtf8 ().constData (), ZIP_CHECKCONS, &zipError);
    ASSERT_NE (nullptr, archive);
    const zip_int64_t resourceIndex = zip_name_locate (archive, "scenes/main.wc.xml", ZIP_FL_ENC_UTF_8);
    ASSERT_GE (resourceIndex, 0);
    ASSERT_EQ (0, zip_delete (archive, static_cast< zip_uint64_t > (resourceIndex)));
    ASSERT_EQ (0, zip_close (archive));

    QString extractedProject;
    const QString target = QDir (directory.path ()).filePath ("extracted");
    EXPECT_FALSE (rws::ProjectManager::extractPackage (packageFile, target, extractedProject, &error));
    EXPECT_FALSE (QFileInfo::exists (target));
}

TEST (ProjectSystemTest, ManagerCleansUnreferencedFilesAndRelocatesMissingResources)
{
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString projectFile = QDir (directory.path ()).filePath ("Integrity.rwproj");
    const QString workCellFile = QDir (directory.path ()).filePath ("scenes/main.wc.xml");
    const QString replacementFile = QDir (directory.path ()).filePath ("replacement.wc.xml");
    ASSERT_TRUE (QDir ().mkpath (QFileInfo (workCellFile).absolutePath ()));
    for (const QString& path : {workCellFile, replacementFile}) {
        QFile file (path);
        ASSERT_TRUE (file.open (QIODevice::WriteOnly));
        file.write ("content");
    }
    const QString orphanFile = QDir (directory.path ()).filePath ("orphan.txt");
    QFile orphan (orphanFile);
    ASSERT_TRUE (orphan.open (QIODevice::WriteOnly));
    orphan.close ();

    rws::ProjectManager manager;
    QString error;
    ASSERT_TRUE (manager.createProject (projectFile, makeManifest (), &error));
    ASSERT_TRUE (manager.removeUnreferencedFiles ({orphanFile}, &error)) << error.toStdString ();
    EXPECT_FALSE (QFileInfo::exists (orphanFile));

    ASSERT_TRUE (QFile::remove (workCellFile));
    ASSERT_TRUE (manager.relocateResource ("scene.main", replacementFile, &error)) << error.toStdString ();
    QString relocated;
    ASSERT_TRUE (manager.resolveResource ("scene.main", relocated, &error));
    EXPECT_TRUE (QFileInfo (relocated).isFile ());
    EXPECT_NE (QDir::cleanPath (workCellFile), QDir::cleanPath (relocated));
}
