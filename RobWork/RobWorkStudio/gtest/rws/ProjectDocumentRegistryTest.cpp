#include <rws/CallbackProjectDocumentProvider.hpp>
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
                       QString* error) override
    {
        if (_events != nullptr)
            _events->push_back (QStringLiteral ("load:") + resource.id);
        if (resource.id == _failingLoadResource) {
            if (error != nullptr)
                *error = QString::fromUtf8 ("模拟可选资源加载失败");
            return false;
        }
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
    void failLoading (const QString& resourceId) { _failingLoadResource = resourceId; }

  private:
    QString _failingLoadResource;
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

TEST (ProjectDocumentRegistryTest, LoadsConsumerWithManagedPassiveAssetDependency)
{
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString projectFile = QDir (directory.path ()).filePath ("Demo.rwproj");

    QStringList events;
    FakeDocumentProvider provider ("provider.test", "test.document", &events);
    rws::ProjectDocumentRegistry registry;
    QString error;
    ASSERT_TRUE (registry.registerProvider (&provider, &error));

    rws::ProjectManifest manifest;
    rws::ProjectResource passive = resource (
        "scene.generated.device", "robwork.passive-asset", "generated/Robot.wc.xml");
    passive.required = true;
    rws::ProjectResource consumer = resource (
        "scene.main", "test.document", "generated/RobotScene.wc.xml");
    consumer.required = true;
    consumer.dependencies.push_back (passive.id);
    manifest.resources.push_back (consumer);
    manifest.resources.push_back (passive);

    ASSERT_TRUE (registry.loadProjectResources (manifest, projectFile, &error))
        << error.toStdString ();
    EXPECT_EQ ((QStringList {"load:scene.main"}), events);
}

TEST (ProjectDocumentRegistryTest, ReloadsStableResourceAtPromotedPath)
{
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString projectFile = QDir (directory.path ()).filePath ("Demo.rwproj");
    QString loadedPath;

    rws::WorkCellProjectDocumentProvider provider (
        [&loadedPath] (const QString& path, QString*) {
            loadedPath = path;
            return true;
        },
        [] (const QString&, QString*) { return true; });
    rws::ProjectDocumentRegistry registry;
    QString error;
    ASSERT_TRUE (registry.registerProvider (&provider, &error));

    rws::ProjectManifest manifest;
    const rws::ProjectResource original = resource (
        "scene.main", "robwork.workcell", "scenes/main.wc.xml");
    manifest.resources.push_back (original);
    ASSERT_TRUE (registry.loadProjectResources (manifest, projectFile, &error));

    rws::ProjectResource promoted = original;
    promoted.path = QStringLiteral ("generated/robot-models/RobotScene.wc.xml");
    ASSERT_TRUE (registry.reloadResource (promoted, manifest, projectFile, &error))
        << error.toStdString ();
    EXPECT_EQ (QDir::cleanPath (QDir (directory.path ()).filePath (promoted.path)), loadedPath);
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
    QString stagedSavePath;

    rws::WorkCellProjectDocumentProvider provider (
        [&loadedPath] (const QString& path, QString*) {
            loadedPath = path;
            return true;
        },
        [&stagedSavePath] (const QString& path, QString* error) {
            stagedSavePath = path;
            // DOMWorkCellSaver selects its writer from the final suffix. A transaction path
            // must therefore retain the complete .wc.xml suffix while remaining a sibling.
            if (!path.endsWith (QStringLiteral (".wc.xml"), Qt::CaseInsensitive)) {
                if (error != nullptr)
                    *error = QStringLiteral ("WorkCell staging path lost its .wc.xml suffix");
                return false;
            }
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
    EXPECT_TRUE (QFileInfo (stagedSavePath).fileName ().contains (QStringLiteral (".rwstage-")));
    EXPECT_FALSE (registry.isDirty ());
    QFile saved (workCellPath);
    ASSERT_TRUE (saved.open (QIODevice::ReadOnly));
    EXPECT_EQ (QByteArray ("workcell-saved"), saved.readAll ());
}

TEST (ProjectDocumentRegistryTest, CallbackProviderStagesResolvedResourceAndTracksDirtyState)
{
    // 第三阶段的业务插件都通过同一类回调式 Provider 接入：Registry 仍负责路径
    // 解析和保存事务，而插件只接收绝对读取路径或暂存写入路径。该测试先固定边界，
    // 防止某个业务插件绕过事务直接覆盖项目正式资源。
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString projectFile = QDir (directory.path ()).filePath ("Demo.rwproj");
    QString loadedPath;
    bool markedClean = false;

    rws::CallbackProjectDocumentProvider provider (
        "plugin.callback",
        "test.callback-document",
        [&loadedPath] (const QString& path, const rws::ProjectDocumentContext&, QString*) {
            loadedPath = path;
            return true;
        },
        [] (const QString& path, const rws::ProjectDocumentContext&, QString* error) {
            QFile file (path);
            if (!file.open (QIODevice::WriteOnly | QIODevice::Truncate)) {
                if (error != nullptr)
                    *error = file.errorString ();
                return false;
            }
            file.write ("callback-saved");
            return true;
        },
        rws::CallbackProjectDocumentProvider::CanCloseHandler (),
        rws::CallbackProjectDocumentProvider::CloseHandler (),
        [&markedClean] () { markedClean = true; });

    rws::ProjectDocumentRegistry registry;
    QString error;
    ASSERT_TRUE (registry.registerProvider (&provider, &error));
    rws::ProjectManifest manifest;
    manifest.resources.push_back (resource ("callback.document",
                                            "test.callback-document",
                                            "documents/callback.json"));

    ASSERT_TRUE (registry.loadProjectResources (manifest, projectFile, &error))
        << error.toStdString ();
    EXPECT_EQ (QDir::cleanPath (QDir (directory.path ()).filePath ("documents/callback.json")),
               loadedPath);

    provider.markDirty ();
    ASSERT_TRUE (registry.isDirty ());
    ASSERT_TRUE (registry.saveDirtyResources (manifest, projectFile, &error))
        << error.toStdString ();
    EXPECT_FALSE (provider.isDirty ("callback.document"));
    EXPECT_TRUE (markedClean);

    QFile saved (QDir (directory.path ()).filePath ("documents/callback.json"));
    ASSERT_TRUE (saved.open (QIODevice::ReadOnly));
    EXPECT_EQ (QByteArray ("callback-saved"), saved.readAll ());
}

// 正向测试：首次编辑时生成的资源没有历史文件，因此不能走普通加载回调；Registry 应直接
// 激活它并交给 Provider 的首次保存流程。提交成功后，新文件必须落在项目目录内且脏状态
// 被清除，避免下一次保存重复写入同一份未变更的配置。
TEST (ProjectDocumentRegistryTest, ActivatesGeneratedResourceForFirstTransactionalSave)
{
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString projectFile = QDir (directory.path ()).filePath ("Demo.rwproj");

    bool loadCalled = false;
    bool markedClean = false;
    rws::CallbackProjectDocumentProvider provider (
        "provider.generated", "test.generated-document",
        [&loadCalled] (const QString&, const rws::ProjectDocumentContext&, QString*) {
            // 生成资源没有旧文件，若调用此回调就说明错误地进入了常规加载路径。
            loadCalled = true;
            return false;
        },
        [] (const QString& path, const rws::ProjectDocumentContext&, QString* error) {
            QFile file (path);
            if (!file.open (QIODevice::WriteOnly | QIODevice::Truncate)) {
                if (error != nullptr)
                    *error = file.errorString ();
                return false;
            }
            file.write ("generated-document");
            return true;
        },
        rws::CallbackProjectDocumentProvider::CanCloseHandler (),
        rws::CallbackProjectDocumentProvider::CloseHandler (),
        [&markedClean] () { markedClean = true; });

    rws::ProjectDocumentRegistry registry;
    QString error;
    ASSERT_TRUE (registry.registerProvider (&provider, &error));
    const rws::ProjectResource generated = resource (
        "analysis.main", "test.generated-document", "analysis/generated.json");

    ASSERT_TRUE (registry.activateGeneratedResource (generated, projectFile, &error))
        << error.toStdString ();
    provider.adoptGeneratedResource (generated.id);
    provider.setDirty (true);

    rws::ProjectManifest manifest;
    manifest.resources.push_back (generated);
    ASSERT_TRUE (registry.saveDirtyResources (manifest, projectFile, &error))
        << error.toStdString ();
    EXPECT_FALSE (loadCalled);
    EXPECT_FALSE (registry.isDirty ());
    EXPECT_TRUE (markedClean);

    QFile saved (QDir (directory.path ()).filePath ("analysis/generated.json"));
    ASSERT_TRUE (saved.open (QIODevice::ReadOnly));
    EXPECT_EQ (QByteArray ("generated-document"), saved.readAll ());
}

// 可选资源的 Provider 即使已安装，也可能因旧版本数据或缺失的附属文件无法加载；这不能
// 阻止仅依赖核心 WorkCell 的项目打开，Registry 应记录尝试后跳过该可选资源。
TEST (ProjectDocumentRegistryTest, SkipsOptionalResourceWhenProviderLoadFails)
{
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString projectFile = QDir (directory.path ()).filePath ("Demo.rwproj");
    QStringList events;
    FakeDocumentProvider provider ("provider.test", "test.document", &events);
    provider.failLoading ("optional.analysis");

    rws::ProjectManifest manifest;
    manifest.project.id = "optional-test";
    manifest.project.name = "optional-test";
    rws::ProjectResource optional = resource ("optional.analysis", "test.document", "analysis.json");
    optional.required = false;
    manifest.resources.push_back (optional);

    rws::ProjectDocumentRegistry registry;
    QString error;
    ASSERT_TRUE (registry.registerProvider (&provider, &error)) << error.toStdString ();
    EXPECT_TRUE (registry.loadProjectResources (manifest, projectFile, &error)) << error.toStdString ();
    EXPECT_TRUE (events.contains ("load:optional.analysis"));
    EXPECT_FALSE (registry.isDirty ());
}

TEST (ProjectDocumentRegistryTest, ReportsWarningWhenOptionalResourceIsSkipped)
{
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString projectFile = QDir (directory.path ()).filePath ("Demo.rwproj");
    QStringList events;
    FakeDocumentProvider provider ("provider.test", "test.document", &events);
    provider.failLoading ("optional.analysis");

    rws::ProjectManifest manifest;
    manifest.project.id = "optional-test";
    manifest.project.name = "optional-test";
    rws::ProjectResource optional = resource ("optional.analysis", "test.document", "analysis.json");
    optional.required = false;
    manifest.resources.push_back (optional);

    rws::ProjectDocumentRegistry registry;
    QString error;
    QStringList warnings;
    ASSERT_TRUE (registry.registerProvider (&provider, &error)) << error.toStdString ();
    EXPECT_TRUE (registry.loadProjectResources (manifest, projectFile, &error, &warnings))
        << error.toStdString ();
    EXPECT_TRUE (error.isEmpty ());
    ASSERT_EQ (1, warnings.size ());
    EXPECT_TRUE (warnings.front ().contains ("optional.analysis"));
}

TEST (ProjectDocumentRegistryTest, AutosaveSerializesDirtyProviderWithoutMarkingItClean)
{
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString projectFile = QDir (directory.path ()).filePath ("Demo.rwproj");
    const QString sourceFile = QDir (directory.path ()).filePath ("analysis.json");
    const QString snapshotProject = QDir (directory.path ()).filePath ("snapshot/snapshot.rwproj");
    QFile source (sourceFile);
    ASSERT_TRUE (source.open (QIODevice::WriteOnly));
    source.write ("disk-version");
    source.close ();

    QStringList events;
    FakeDocumentProvider provider ("provider.test", "test.document", &events);
    rws::ProjectManifest manifest;
    manifest.resources.push_back (resource ("analysis", "test.document", "analysis.json"));
    rws::ProjectDocumentRegistry registry;
    QString error;
    ASSERT_TRUE (registry.registerProvider (&provider, &error));
    ASSERT_TRUE (registry.loadProjectResources (manifest, projectFile, &error));
    provider.markDirty ("analysis");

    ASSERT_TRUE (registry.saveAutosaveResources (manifest, projectFile, snapshotProject, &error))
        << error.toStdString ();
    EXPECT_TRUE (registry.isDirty ());
    QFile snapshot (QDir (QFileInfo (snapshotProject).absolutePath ()).filePath ("analysis.json"));
    ASSERT_TRUE (snapshot.open (QIODevice::ReadOnly));
    EXPECT_EQ (QByteArray ("saved:analysis"), snapshot.readAll ());
}
