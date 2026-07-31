#include <rws/ProjectDocumentProvider.hpp>
#include <rws/ProjectDocumentRegistry.hpp>
#include <rws/WorkCellProjectDocumentProvider.hpp>

#include <QDir>
#include <QFile>
#include <QSet>
#include <QTemporaryDir>
#include <gtest/gtest.h>

namespace {

// 测试用假 Provider：不接触真实文档格式，只记录加载/关闭事件、按需写暂存文件，
// 并可通过 markDirty / failSaving 精确控制脏状态与保存失败，用于验证 Registry
// 的加载顺序、脏检查和保存事务行为，无需启动 OpenGL 或插件系统。
class FakeDocumentProvider : public rws::ProjectDocumentProvider
{
  public:
    FakeDocumentProvider (const QString& id, const QString& kind, QStringList* events) :
        _id (id), _kind (kind), _events (events)
    {}

    QString providerId () const override { return _id; }
    QStringList supportedResourceKinds () const override { return {_kind}; }

    bool loadResource (const rws::ProjectResource& resource,
                       const rws::ProjectDocumentContext&,
                       QString*) override
    {
        if (_events != nullptr)
            _events->push_back (QStringLiteral ("load:") + resource.id);
        return true;
    }

    bool saveResource (const rws::ProjectResource& resource,
                       const rws::ProjectDocumentContext&,
                       const QString& targetPath,
                       QString* error) override
    {
        if (resource.id == _failingResource) {
            if (error != nullptr)
                *error = QString::fromUtf8 ("模拟保存失败");
            return false;
        }

        QFile file (targetPath);
        if (!file.open (QIODevice::WriteOnly | QIODevice::Truncate)) {
            if (error != nullptr)
                *error = file.errorString ();
            return false;
        }
        file.write ((QStringLiteral ("saved:") + resource.id).toUtf8 ());
        return true;
    }

    bool isDirty (const QString& resourceId) const override
    {
        return _dirtyResources.contains (resourceId);
    }

    bool canClose (const QString&, QString*) const override { return true; }

    void markClean (const QString& resourceId) override
    {
        _dirtyResources.remove (resourceId);
    }

    void closeResource (const QString& resourceId) override
    {
        if (_events != nullptr)
            _events->push_back (QStringLiteral ("close:") + resourceId);
    }

    // 测试辅助：把指定资源标记为脏，或让指定资源的保存模拟失败。
    void markDirty (const QString& resourceId) { _dirtyResources.insert (resourceId); }
    void failSaving (const QString& resourceId) { _failingResource = resourceId; }

  private:
    QString _id;
    QString _kind;
    QStringList* _events;             // 可选事件记录（load:xxx / close:xxx）。
    QSet< QString > _dirtyResources;  // 当前被标记为脏的资源集合。
    QString _failingResource;         // 保存时强制失败资源的 ID（为空则不模拟失败）。
};

// 构造一个 project 归属的测试资源：固定 id/kind/path，路径按项目相对路径解析。
rws::ProjectResource resource (const QString& id,
                               const QString& kind,
                               const QString& path)
{
    rws::ProjectResource result;
    result.id = id;
    result.kind = kind;
    result.path = path;
    result.ownership = QStringLiteral ("project");
    return result;
}

}    // namespace

// 负向测试：同一 kind 不能被两个 Provider 注册，杜绝加载结果依赖插件注册顺序。
TEST (ProjectDocumentRegistryTest, RejectsDuplicateResourceKind)
{
    // 每个 kind 只能由一个 Provider 解释，否则项目加载结果会依赖插件注册顺序。
    // Registry 必须在注册阶段拒绝歧义，而不是打开项目时静默选择其中一个。
    rws::ProjectDocumentRegistry registry;
    FakeDocumentProvider first ("provider.first", "test.document", nullptr);
    FakeDocumentProvider second ("provider.second", "test.document", nullptr);
    QString error;

    ASSERT_TRUE (registry.registerProvider (&first, &error));
    EXPECT_FALSE (registry.registerProvider (&second, &error));
    EXPECT_FALSE (error.isEmpty ());
}

// 正向测试：即使清单数组把依赖者排在前面，加载顺序也必须满足依赖先于依赖者。
TEST (ProjectDocumentRegistryTest, LoadsResourcesInDependencyOrder)
{
    // 即使清单数组把依赖者放在前面，Registry 也必须先加载其依赖资源，确保插件在
    // loadResource 中能够安全读取已就绪的模型或场景。
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString projectFile = QDir (directory.path ()).filePath ("Demo.rwproj");

    QStringList events;
    FakeDocumentProvider provider ("provider.test", "test.document", &events);
    rws::ProjectDocumentRegistry registry;
    QString error;
    ASSERT_TRUE (registry.registerProvider (&provider, &error));

    rws::ProjectManifest manifest;
    rws::ProjectResource consumer = resource ("consumer", "test.document", "consumer.txt");
    consumer.dependencies.push_back ("dependency");
    manifest.resources.push_back (consumer);
    manifest.resources.push_back (resource ("dependency", "test.document", "dependency.txt"));

    ASSERT_TRUE (registry.loadProjectResources (manifest, projectFile, &error))
        << error.toStdString ();
    EXPECT_EQ ((QStringList {"load:dependency", "load:consumer"}), events);
}

// 正向测试：脏资源经保存事务写入正式文件，且保存成功后脏状态被清除。
TEST (ProjectDocumentRegistryTest, SavesDirtyResourcesAndMarksThemClean)
{
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString projectFile = QDir (directory.path ()).filePath ("Demo.rwproj");
    const QString target = QDir (directory.path ()).filePath ("document.txt");
    QFile original (target);
    ASSERT_TRUE (original.open (QIODevice::WriteOnly));
    original.write ("original");
    original.close ();

    FakeDocumentProvider provider ("provider.test", "test.document", nullptr);
    rws::ProjectDocumentRegistry registry;
    QString error;
    ASSERT_TRUE (registry.registerProvider (&provider, &error));

    rws::ProjectManifest manifest;
    manifest.resources.push_back (resource ("document", "test.document", "document.txt"));
    ASSERT_TRUE (registry.loadProjectResources (manifest, projectFile, &error));
    provider.markDirty ("document");

    ASSERT_TRUE (registry.saveDirtyResources (manifest, projectFile, &error))
        << error.toStdString ();
    EXPECT_FALSE (registry.isDirty ());

    QFile saved (target);
    ASSERT_TRUE (saved.open (QIODevice::ReadOnly));
    EXPECT_EQ (QByteArray ("saved:document"), saved.readAll ());
}

// 负向测试：任一资源暂存失败时，其他资源的正式文件必须保持原内容（无半提交）。
TEST (ProjectDocumentRegistryTest, FailedStagingLeavesOriginalFilesUntouched)
{
    // 所有脏资源必须先完成暂存，随后才能进入替换阶段。第二个资源暂存失败时，
    // 第一个资源的正式文件仍应保持原内容，证明保存事务没有产生半提交状态。
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString projectFile = QDir (directory.path ()).filePath ("Demo.rwproj");
    for (const QString& name : {QStringLiteral ("first.txt"), QStringLiteral ("second.txt")}) {
        QFile file (QDir (directory.path ()).filePath (name));
        ASSERT_TRUE (file.open (QIODevice::WriteOnly));
        file.write ("original");
    }

    FakeDocumentProvider provider ("provider.test", "test.document", nullptr);
    rws::ProjectDocumentRegistry registry;
    QString error;
    ASSERT_TRUE (registry.registerProvider (&provider, &error));

    rws::ProjectManifest manifest;
    manifest.resources.push_back (resource ("first", "test.document", "first.txt"));
    manifest.resources.push_back (resource ("second", "test.document", "second.txt"));
    ASSERT_TRUE (registry.loadProjectResources (manifest, projectFile, &error));
    provider.markDirty ("first");
    provider.markDirty ("second");
    provider.failSaving ("second");

    EXPECT_FALSE (registry.saveDirtyResources (manifest, projectFile, &error));
    QFile first (QDir (directory.path ()).filePath ("first.txt"));
    ASSERT_TRUE (first.open (QIODevice::ReadOnly));
    EXPECT_EQ (QByteArray ("original"), first.readAll ());
    EXPECT_TRUE (registry.isDirty ());
}

// 正向测试：真实 WorkCell Provider 通过回调适配主窗口——加载回调必须收到项目
// 解析后的绝对路径；事务提交后 Provider 与 Registry 的脏状态都被清除。
TEST (ProjectDocumentRegistryTest, WorkCellProviderUsesResolvedPathAndDirtyLifecycle)
{
    // WorkCell Provider 通过回调适配主窗口，测试不需要启动 OpenGL 或插件系统；
    // 这里固定它必须向加载回调传入项目解析后的绝对路径，并在事务提交后清除脏状态。
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString projectFile = QDir (directory.path ()).filePath ("Demo.rwproj");
    const QString workCellPath = QDir (directory.path ()).filePath ("scene.wc.xml");
    QString loadedPath;

    rws::WorkCellProjectDocumentProvider provider (
        [&loadedPath] (const QString& path, QString*) {
            loadedPath = path;
            return true;
        },
        [] (const QString& path, QString* error) {
            QFile file (path);
            if (!file.open (QIODevice::WriteOnly)) {
                if (error != nullptr)
                    *error = file.errorString ();
                return false;
            }
            file.write ("workcell-saved");
            return true;
        });

    rws::ProjectDocumentRegistry registry;
    QString error;
    ASSERT_TRUE (registry.registerProvider (&provider, &error));
    rws::ProjectManifest manifest;
    manifest.resources.push_back (
        resource ("scene.main", "robwork.workcell", "scene.wc.xml"));

    ASSERT_TRUE (registry.loadProjectResources (manifest, projectFile, &error));
    EXPECT_EQ (QDir::cleanPath (workCellPath), loadedPath);
    provider.markDirty ();
    EXPECT_TRUE (registry.isDirty ());

    ASSERT_TRUE (registry.saveDirtyResources (manifest, projectFile, &error));
    EXPECT_FALSE (registry.isDirty ());
    QFile saved (workCellPath);
    ASSERT_TRUE (saved.open (QIODevice::ReadOnly));
    EXPECT_EQ (QByteArray ("workcell-saved"), saved.readAll ());
}
