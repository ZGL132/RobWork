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
