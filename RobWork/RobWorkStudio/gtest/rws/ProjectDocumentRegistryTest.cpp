#include <rws/CallbackProjectDocumentProvider.hpp>
#include <rws/ProjectDocumentProvider.hpp>
#include <rws/ProjectDocumentRegistry.hpp>
#include <rws/ProjectSaveTransaction.hpp>
#include <rws/WorkCellProjectDocumentProvider.hpp>

#include <QDir>
#include <QDataStream>
#include <QFile>
#include <QProcess>
#include <QSet>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <functional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace {

#if defined (Q_OS_WIN) && defined (_WIN64)
static_assert (sizeof (rws::ProjectSaveTransaction) == 40,
               "ProjectSaveTransaction must preserve its committed public layout.");
#endif
static_assert (std::is_constructible_v< rws::ProjectSaveTransaction,
                                        rws::ProjectSaveTransaction::ExistingTargetPolicy >,
               "ProjectSaveTransaction must retain its one-argument policy constructor.");

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
        if (_onLoad)
            _onLoad (resource);
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
        _lastSavedResource = resource;
        _lastSavePath = targetPath;
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
    void onLoad (std::function< void (const rws::ProjectResource&) > callback)
    {
        _onLoad = std::move (callback);
    }
    const rws::ProjectResource& lastSavedResource () const { return _lastSavedResource; }
    QString lastSavePath () const { return _lastSavePath; }

  private:
    QString _failingLoadResource;
    QString _id;
    QString _kind;
    QStringList* _events;             // 可选事件记录（load:xxx / close:xxx）。
    QSet< QString > _dirtyResources;  // 当前被标记为脏的资源集合。
    QString _failingResource;         // 保存时强制失败资源的 ID（为空则不模拟失败）。
    rws::ProjectResource _lastSavedResource;
    QString _lastSavePath;
    std::function< void (const rws::ProjectResource&) > _onLoad;
};

class TransitionStateProvider : public rws::ProjectDocumentProvider
{
  public:
    TransitionStateProvider (QString id, QString kind, QByteArray loadedData,
                             bool dirtyOnLoad, bool throwOnClose, QStringList* events) :
        _id (std::move (id)),
        _kind (std::move (kind)),
        _loadedData (std::move (loadedData)),
        _dirtyOnLoad (dirtyOnLoad),
        _throwOnClose (throwOnClose),
        _events (events)
    {}

    QString providerId () const override { return _id; }
    QStringList supportedResourceKinds () const override { return {_kind}; }
    bool loadResource (const rws::ProjectResource& resource,
                       const rws::ProjectDocumentContext&,
                       QString*) override
    {
        _loaded.insert (resource.id);
        _data.insert (resource.id, _loadedData);
        if (_dirtyOnLoad)
            _dirty.insert (resource.id);
        return true;
    }
    bool saveResource (const rws::ProjectResource&,
                       const rws::ProjectDocumentContext&,
                       const QString&,
                       QString*) override
    {
        return true;
    }
    bool isDirty (const QString& resourceId) const override
    {
        return _dirty.contains (resourceId);
    }
    bool canClose (const QString&, QString*) const override { return true; }
    void markClean (const QString& resourceId) override { _dirty.remove (resourceId); }
    void closeResource (const QString& resourceId) override
    {
        if (_events != nullptr)
            _events->push_back (QStringLiteral ("close:") + resourceId);
        _loaded.remove (resourceId);
        _data.remove (resourceId);
        _dirty.remove (resourceId);
        if (_throwOnClose)
            throw std::runtime_error ("intentional close failure after clearing state");
    }
    bool snapshotResource (const QString& resourceId,
                           QByteArray* snapshot,
                           QString* error) const override
    {
        if (!_loaded.contains (resourceId) || snapshot == nullptr) {
            if (error != nullptr)
                *error = QStringLiteral ("resource is not loaded");
            return false;
        }
        snapshot->clear ();
        QDataStream stream (snapshot, QIODevice::WriteOnly);
        stream << _data.value (resourceId) << _dirty.contains (resourceId);
        return stream.status () == QDataStream::Ok;
    }
    bool restoreResource (const QString& resourceId,
                          const QByteArray& snapshot,
                          QString* error) override
    {
        QByteArray data;
        bool dirty = false;
        QDataStream stream (snapshot);
        stream >> data >> dirty;
        if (stream.status () != QDataStream::Ok) {
            if (error != nullptr)
                *error = QStringLiteral ("snapshot is invalid");
            return false;
        }
        _loaded.insert (resourceId);
        _data.insert (resourceId, data);
        if (dirty)
            _dirty.insert (resourceId);
        else
            _dirty.remove (resourceId);
        return true;
    }

    bool isLoaded (const QString& resourceId) const { return _loaded.contains (resourceId); }
    QByteArray data (const QString& resourceId) const { return _data.value (resourceId); }

  private:
    QString _id;
    QString _kind;
    QByteArray _loadedData;
    bool _dirtyOnLoad;
    bool _throwOnClose;
    QStringList* _events;
    QSet< QString > _loaded;
    QSet< QString > _dirty;
    QHash< QString, QByteArray > _data;
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

#ifdef Q_OS_WIN
bool runWindowsCommand (const QString& command)
{
    QProcess process;
    process.setProgram (QStringLiteral ("cmd.exe"));
    process.setNativeArguments (QStringLiteral ("/D /C ") + command);
    process.start ();
    return process.waitForFinished () && process.exitStatus () == QProcess::NormalExit &&
           process.exitCode () == 0;
}

bool createDirectoryJunction (const QString& linkPath, const QString& targetPath)
{
    return runWindowsCommand (
        QStringLiteral ("mklink /J \"%1\" \"%2\"")
            .arg (QDir::toNativeSeparators (linkPath), QDir::toNativeSeparators (targetPath)));
}

class DirectoryJunctionCleanup
{
  public:
    explicit DirectoryJunctionCleanup (const QString& path) : _path (path) {}
    ~DirectoryJunctionCleanup ()
    {
        if (!_path.isEmpty ())
            runWindowsCommand (QStringLiteral ("rmdir \"%1\"").arg (_path));
    }

  private:
    QString _path;
};
#endif

}    // namespace

TEST (ProjectDocumentRegistryTest, RejectModePreservesTargetCreatedAfterStaging)
{
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString target = QDir (directory.path ()).filePath ("managed.txt");
    rws::ProjectSaveTransaction transaction (
        rws::ProjectSaveTransaction::ExistingTargetPolicy::Reject);
    QString error;
    ASSERT_TRUE (transaction.stageBytes (QByteArray ("candidate"), target, &error));

    QFile external (target);
    ASSERT_TRUE (external.open (QIODevice::WriteOnly));
    ASSERT_EQ (qint64 (8), external.write ("external"));
    external.close ();

    EXPECT_FALSE (transaction.commit (&error));
    EXPECT_FALSE (error.isEmpty ());
    ASSERT_TRUE (external.open (QIODevice::ReadOnly));
    EXPECT_EQ (QByteArray ("external"), external.readAll ());
    const QStringList residue = QDir (directory.path ()).entryList (
        {QStringLiteral ("*.rwstage-*"), QStringLiteral ("*.rwbackup-*")}, QDir::Files);
    EXPECT_TRUE (residue.isEmpty ());
}

TEST (ProjectDocumentRegistryTest, InstalledFilesCanBeRolledBackBeforeFinalize)
{
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString firstTarget = QDir (directory.path ()).filePath ("first.txt");
    const QString secondTarget = QDir (directory.path ()).filePath ("second.txt");
    for (const auto& entry : {qMakePair (firstTarget, QByteArray ("old-first")),
                              qMakePair (secondTarget, QByteArray ("old-second"))}) {
        QFile file (entry.first);
        ASSERT_TRUE (file.open (QIODevice::WriteOnly));
        ASSERT_EQ (entry.second.size (), file.write (entry.second));
    }

    rws::ProjectSaveTransaction transaction;
    QString error;
    ASSERT_TRUE (transaction.stageBytes (QByteArray ("new-first"), firstTarget, &error));
    ASSERT_TRUE (transaction.stageBytes (QByteArray ("new-second"), secondTarget, &error));
    ASSERT_TRUE (transaction.install (&error)) << error.toStdString ();

    QFile first (firstTarget);
    ASSERT_TRUE (first.open (QIODevice::ReadOnly));
    EXPECT_EQ (QByteArray ("new-first"), first.readAll ());
    first.close ();
    QFile second (secondTarget);
    ASSERT_TRUE (second.open (QIODevice::ReadOnly));
    EXPECT_EQ (QByteArray ("new-second"), second.readAll ());
    second.close ();
    EXPECT_EQ (2,
               QDir (directory.path ())
                   .entryList ({QStringLiteral ("*.rwbackup-*")}, QDir::Files)
                   .size ());

    transaction.rollback ();

    ASSERT_TRUE (first.open (QIODevice::ReadOnly));
    EXPECT_EQ (QByteArray ("old-first"), first.readAll ());
    ASSERT_TRUE (second.open (QIODevice::ReadOnly));
    EXPECT_EQ (QByteArray ("old-second"), second.readAll ());
    EXPECT_TRUE (QDir (directory.path ())
                     .entryList ({QStringLiteral ("*.rwstage-*"),
                                  QStringLiteral ("*.rwbackup-*")},
                                 QDir::Files)
                     .isEmpty ());
}

TEST (ProjectDocumentRegistryTest, RollbackPreservesTargetExternallyChangedAfterInstall)
{
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString target = QDir (directory.path ()).filePath ("managed.txt");
    QFile original (target);
    ASSERT_TRUE (original.open (QIODevice::WriteOnly));
    ASSERT_EQ (qint64 (3), original.write ("old"));
    original.close ();

    rws::ProjectSaveTransaction transaction;
    QString error;
    ASSERT_TRUE (transaction.stageBytes (QByteArray ("installed"), target, &error));
    ASSERT_TRUE (transaction.install (&error)) << error.toStdString ();

    QFile external (target);
    ASSERT_TRUE (external.open (QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_EQ (qint64 (8), external.write ("external"));
    external.close ();

    QString rollbackError;
    transaction.rollback (&rollbackError);

    ASSERT_TRUE (external.open (QIODevice::ReadOnly));
    EXPECT_EQ (QByteArray ("external"), external.readAll ());
    EXPECT_FALSE (rollbackError.isEmpty ());
    const QStringList backups = QDir (directory.path ()).entryList (
        {QStringLiteral ("*.rwbackup-*")}, QDir::Files);
    ASSERT_EQ (1, backups.size ());
    const QString backupPath = QDir (directory.path ()).filePath (backups.front ());
    QFile backup (backupPath);
    ASSERT_TRUE (backup.open (QIODevice::ReadOnly));
    EXPECT_EQ (QByteArray ("old"), backup.readAll ());
    EXPECT_TRUE (rollbackError.contains (backupPath));
    EXPECT_TRUE (QDir (directory.path ())
                     .entryList ({QStringLiteral ("*.rwstage-*")}, QDir::Files)
                     .isEmpty ());
}

TEST (ProjectDocumentRegistryTest, RollbackPreservesTargetWhenBackupChangesAfterInstall)
{
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString target = QDir (directory.path ()).filePath ("managed.txt");
    QFile original (target);
    ASSERT_TRUE (original.open (QIODevice::WriteOnly));
    ASSERT_EQ (qint64 (3), original.write ("old"));
    original.close ();

    rws::ProjectSaveTransaction transaction;
    QString error;
    ASSERT_TRUE (transaction.stageBytes (QByteArray ("installed"), target, &error));
    ASSERT_TRUE (transaction.install (&error)) << error.toStdString ();

    const QStringList backups = QDir (directory.path ()).entryList (
        {QStringLiteral ("*.rwbackup-*")}, QDir::Files);
    ASSERT_EQ (1, backups.size ());
    const QString backupPath = QDir (directory.path ()).filePath (backups.front ());
    QFile backup (backupPath);
    ASSERT_TRUE (backup.open (QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_EQ (qint64 (7), backup.write ("altered"));
    backup.close ();

    QString rollbackError;
    transaction.rollback (&rollbackError);

    QFile installed (target);
    ASSERT_TRUE (installed.open (QIODevice::ReadOnly));
    EXPECT_EQ (QByteArray ("installed"), installed.readAll ());
    installed.close ();
    ASSERT_TRUE (backup.open (QIODevice::ReadOnly));
    EXPECT_EQ (QByteArray ("altered"), backup.readAll ());
    EXPECT_TRUE (rollbackError.contains (QStringLiteral ("backup changed")))
        << rollbackError.toStdString ();
    EXPECT_TRUE (rollbackError.contains (backupPath)) << rollbackError.toStdString ();
}

TEST (ProjectDocumentRegistryTest, InstallRejectsStagedContentChangedBeforeInstallation)
{
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString target = QDir (directory.path ()).filePath ("managed.txt");
    QFile original (target);
    ASSERT_TRUE (original.open (QIODevice::WriteOnly));
    ASSERT_EQ (qint64 (3), original.write ("old"));
    original.close ();

    rws::ProjectSaveTransaction transaction;
    QString error;
    ASSERT_TRUE (transaction.stageBytes (QByteArray ("candidate"), target, &error));
    const QStringList staged = QDir (directory.path ()).entryList (
        {QStringLiteral ("*.rwstage-*")}, QDir::Files);
    ASSERT_EQ (1, staged.size ());
    QFile mutation (QDir (directory.path ()).filePath (staged.front ()));
    ASSERT_TRUE (mutation.open (QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_EQ (qint64 (7), mutation.write ("altered"));
    mutation.close ();

    EXPECT_FALSE (transaction.install (&error));
    EXPECT_FALSE (error.isEmpty ());
    QFile preserved (target);
    ASSERT_TRUE (preserved.open (QIODevice::ReadOnly));
    EXPECT_EQ (QByteArray ("old"), preserved.readAll ());
    EXPECT_TRUE (QDir (directory.path ())
                     .entryList ({QStringLiteral ("*.rwstage-*"),
                                  QStringLiteral ("*.rwbackup-*")},
                                 QDir::Files)
                     .isEmpty ());
}

TEST (ProjectDocumentRegistryTest, CopiedTransactionRetainsStagedContentIdentity)
{
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString target = QDir (directory.path ()).filePath ("managed.txt");
    QFile original (target);
    ASSERT_TRUE (original.open (QIODevice::WriteOnly));
    ASSERT_EQ (qint64 (3), original.write ("old"));
    original.close ();

    rws::ProjectSaveTransaction originalTransaction;
    QString error;
    ASSERT_TRUE (originalTransaction.stageBytes (QByteArray ("candidate"), target, &error));
    rws::ProjectSaveTransaction copiedTransaction (originalTransaction);

    ASSERT_TRUE (copiedTransaction.install (&error)) << error.toStdString ();
    QFile installed (target);
    ASSERT_TRUE (installed.open (QIODevice::ReadOnly));
    EXPECT_EQ (QByteArray ("candidate"), installed.readAll ());
    copiedTransaction.rollback ();
}

TEST (ProjectDocumentRegistryTest, MovedTransactionTransfersStagedContentIdentity)
{
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString target = QDir (directory.path ()).filePath ("managed.txt");
    QFile original (target);
    ASSERT_TRUE (original.open (QIODevice::WriteOnly));
    ASSERT_EQ (qint64 (3), original.write ("old"));
    original.close ();

    rws::ProjectSaveTransaction originalTransaction;
    QString error;
    ASSERT_TRUE (originalTransaction.stageBytes (QByteArray ("candidate"), target, &error));
    rws::ProjectSaveTransaction movedTransaction (std::move (originalTransaction));

    ASSERT_TRUE (movedTransaction.install (&error)) << error.toStdString ();
    QFile installed (target);
    ASSERT_TRUE (installed.open (QIODevice::ReadOnly));
    EXPECT_EQ (QByteArray ("candidate"), installed.readAll ());
    movedTransaction.rollback ();
}

TEST (ProjectDocumentRegistryTest, FinalizeCleansProviderAndRemovesBackup)
{
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString target = QDir (directory.path ()).filePath ("managed.txt");
    QFile original (target);
    ASSERT_TRUE (original.open (QIODevice::WriteOnly));
    ASSERT_EQ (qint64 (3), original.write ("old"));
    original.close ();

    FakeDocumentProvider provider ("provider.test", "test.document", nullptr);
    const rws::ProjectResource managed = resource ("managed", "test.document", "managed.txt");
    provider.markDirty (managed.id);
    rws::ProjectDocumentContext context;
    context.projectDirectory = directory.path ();
    rws::ProjectSaveTransaction transaction;
    QString error;
    ASSERT_TRUE (transaction.stage (provider, managed, context, target, &error));
    ASSERT_TRUE (transaction.install (&error)) << error.toStdString ();

    EXPECT_TRUE (provider.isDirty (managed.id));
    EXPECT_EQ (1,
               QDir (directory.path ())
                   .entryList ({QStringLiteral ("*.rwbackup-*")}, QDir::Files)
                   .size ());

    transaction.finalize ();

    EXPECT_FALSE (provider.isDirty (managed.id));
    EXPECT_TRUE (QDir (directory.path ())
                     .entryList ({QStringLiteral ("*.rwbackup-*")}, QDir::Files)
                     .isEmpty ());
    QFile installed (target);
    ASSERT_TRUE (installed.open (QIODevice::ReadOnly));
    EXPECT_EQ (QByteArray ("saved:managed"), installed.readAll ());
}

TEST (ProjectDocumentRegistryTest, FinalizePreservesChangedBackupAndProviderState)
{
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString target = QDir (directory.path ()).filePath ("managed.txt");
    QFile original (target);
    ASSERT_TRUE (original.open (QIODevice::WriteOnly));
    ASSERT_EQ (qint64 (3), original.write ("old"));
    original.close ();

    FakeDocumentProvider provider ("provider.test", "test.document", nullptr);
    const rws::ProjectResource managed = resource ("managed", "test.document", "managed.txt");
    provider.markDirty (managed.id);
    rws::ProjectDocumentContext context;
    context.projectDirectory = directory.path ();
    rws::ProjectSaveTransaction transaction;
    QString error;
    ASSERT_TRUE (transaction.stage (provider, managed, context, target, &error));
    ASSERT_TRUE (transaction.install (&error)) << error.toStdString ();

    const QStringList backups = QDir (directory.path ()).entryList (
        {QStringLiteral ("*.rwbackup-*")}, QDir::Files);
    ASSERT_EQ (1, backups.size ());
    const QString backupPath = QDir (directory.path ()).filePath (backups.front ());
    QFile backup (backupPath);
    ASSERT_TRUE (backup.open (QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_EQ (qint64 (7), backup.write ("altered"));
    backup.close ();

    EXPECT_FALSE (transaction.finalize (&error));
    EXPECT_FALSE (error.isEmpty ());
    EXPECT_TRUE (error.contains (QStringLiteral ("backup changed"))) << error.toStdString ();
    EXPECT_TRUE (provider.isDirty (managed.id));
    EXPECT_EQ (QByteArray ("saved:managed"), [&] {
        QFile file (target);
        EXPECT_TRUE (file.open (QIODevice::ReadOnly));
        return file.readAll ();
    } ());
    EXPECT_EQ (QByteArray ("altered"), [&] {
        QFile file (backupPath);
        EXPECT_TRUE (file.open (QIODevice::ReadOnly));
        return file.readAll ();
    } ());
}

TEST (ProjectDocumentRegistryTest, FinalizePreflightFailurePreservesEarlierBackupsAndDirtyProviders)
{
#ifndef Q_OS_WIN
    GTEST_SKIP () << "Directory junction regression is specific to Windows.";
#else
    QTemporaryDir directory;
    QTemporaryDir external;
    ASSERT_TRUE (directory.isValid ());
    ASSERT_TRUE (external.isValid ());
    const QString projectRoot = QDir (directory.path ()).filePath ("project");
    const QString firstTarget = QDir (projectRoot).filePath ("first.txt");
    const QString secondParent = QDir (projectRoot).filePath ("second");
    const QString secondTarget = QDir (secondParent).filePath ("second.txt");
    ASSERT_TRUE (QDir ().mkpath (secondParent));
    for (const auto& entry : {qMakePair (firstTarget, QByteArray ("old-first")),
                              qMakePair (secondTarget, QByteArray ("old-second"))}) {
        QFile file (entry.first);
        ASSERT_TRUE (file.open (QIODevice::WriteOnly));
        ASSERT_EQ (entry.second.size (), file.write (entry.second));
    }

    FakeDocumentProvider provider ("provider.test", "test.document", nullptr);
    const rws::ProjectResource first = resource ("first", "test.document", "first.txt");
    const rws::ProjectResource second = resource ("second", "test.document", "second/second.txt");
    provider.markDirty (first.id);
    provider.markDirty (second.id);
    rws::ProjectDocumentContext context;
    context.projectDirectory = projectRoot;
    const QString parkedParent = QDir (projectRoot).filePath ("parked-second");
    DirectoryJunctionCleanup cleanup (secondParent);
    rws::ProjectSaveTransaction transaction;
    rws::ProjectSaveTransaction::setContainmentRoot (transaction, projectRoot);
    QString error;
    ASSERT_TRUE (transaction.stage (provider, first, context, firstTarget, &error));
    ASSERT_TRUE (transaction.stage (provider, second, context, secondTarget, &error));
    ASSERT_TRUE (transaction.install (&error)) << error.toStdString ();
    ASSERT_TRUE (QDir ().rename (secondParent, parkedParent));
    ASSERT_TRUE (createDirectoryJunction (secondParent, external.path ()));

    EXPECT_FALSE (transaction.finalize (&error));
    EXPECT_FALSE (error.isEmpty ());
    EXPECT_TRUE (provider.isDirty (first.id));
    EXPECT_TRUE (provider.isDirty (second.id));
    EXPECT_EQ (1, QDir (projectRoot).entryList ({QStringLiteral ("*.rwbackup-*")}, QDir::Files).size ());
    EXPECT_EQ (1, QDir (parkedParent).entryList ({QStringLiteral ("*.rwbackup-*")}, QDir::Files).size ());
#endif
}

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

TEST (ProjectDocumentRegistryTest, SynchronizesLoadedDescriptorsAndLifecycleWithoutReload)
{
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString projectFile = QDir (directory.path ()).filePath ("Demo.rwproj");
    QStringList events;
    FakeDocumentProvider provider ("provider.test", "test.document", &events);
    rws::ProjectDocumentRegistry registry;
    QString error;
    ASSERT_TRUE (registry.registerProvider (&provider, &error));

    rws::ProjectManifest original;
    original.resources.push_back (resource ("consumer", "test.document", "old/consumer.txt"));
    original.resources.push_back (resource ("dependency", "test.document", "dependency.txt"));
    ASSERT_TRUE (registry.loadProjectResources (original, projectFile, &error));
    EXPECT_EQ ((QStringList {"load:consumer", "load:dependency"}), events);

    rws::ProjectManifest updated = original;
    updated.resources[0].path = QStringLiteral ("new/consumer.txt");
    updated.resources[0].dependencies = {QStringLiteral ("dependency")};
    events.clear ();
    ASSERT_TRUE (registry.synchronizeLoadedResources (updated, projectFile, &error))
        << error.toStdString ();
    EXPECT_TRUE (events.isEmpty ());

    provider.markDirty ("consumer");
    ASSERT_TRUE (registry.saveDirtyResources (updated, projectFile, &error))
        << error.toStdString ();
    EXPECT_EQ ((QStringList {"dependency"}), provider.lastSavedResource ().dependencies);
    EXPECT_EQ (QDir::cleanPath (QDir (directory.path ()).filePath ("new")),
               QDir::cleanPath (QFileInfo (provider.lastSavePath ()).absolutePath ()));
    EXPECT_TRUE (QFileInfo (provider.lastSavePath ()).fileName ().contains (
        QStringLiteral (".rwstage-")));

    events.clear ();
    registry.closeResources ();
    EXPECT_EQ ((QStringList {"close:consumer", "close:dependency"}), events);
}

TEST (ProjectDocumentRegistryTest, FailedSynchronizationLeavesLoadedStateUntouched)
{
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString projectFile = QDir (directory.path ()).filePath ("Demo.rwproj");
    QStringList events;
    FakeDocumentProvider provider ("provider.test", "test.document", &events);
    rws::ProjectDocumentRegistry registry;
    QString error;
    ASSERT_TRUE (registry.registerProvider (&provider, &error));

    rws::ProjectManifest original;
    original.resources.push_back (resource ("first", "test.document", "first.txt"));
    original.resources.push_back (resource ("second", "test.document", "second.txt"));
    ASSERT_TRUE (registry.loadProjectResources (original, projectFile, &error));

    rws::ProjectManifest incompatible = original;
    incompatible.resources[0].kind = QStringLiteral ("test.incompatible-document");
    incompatible.resources[1].dependencies = {QStringLiteral ("first")};
    EXPECT_FALSE (registry.synchronizeLoadedResources (incompatible, projectFile, &error));

    provider.markDirty ("first");
    ASSERT_TRUE (registry.saveDirtyResources (original, projectFile, &error));
    EXPECT_EQ (QStringLiteral ("test.document"), provider.lastSavedResource ().kind);
    EXPECT_EQ (QStringLiteral ("first.txt"), provider.lastSavedResource ().path);

    events.clear ();
    registry.closeResources ();
    EXPECT_EQ ((QStringList {"close:second", "close:first"}), events);
}

TEST (ProjectDocumentRegistryTest, DefersReentrantSynchronizationUntilLoadCompletes)
{
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString projectFile = QDir (directory.path ()).filePath ("Demo.rwproj");
    QStringList events;
    FakeDocumentProvider provider ("provider.test", "test.document", &events);
    rws::ProjectDocumentRegistry registry;
    QString error;
    ASSERT_TRUE (registry.registerProvider (&provider, &error));

    rws::ProjectManifest legacy;
    legacy.resources.push_back (resource ("consumer", "test.document", "consumer.txt"));
    legacy.resources.push_back (resource ("dependency", "test.document", "dependency.txt"));
    rws::ProjectManifest reconciled = legacy;
    reconciled.resources[0].dependencies = {QStringLiteral ("dependency")};

    bool synchronizedDuringLoad = false;
    provider.onLoad ([&] (const rws::ProjectResource& loaded) {
        if (loaded.id != QStringLiteral ("consumer") || synchronizedDuringLoad)
            return;
        synchronizedDuringLoad = registry.synchronizeLoadedResources (
            reconciled, projectFile, &error);
    });

    ASSERT_TRUE (registry.loadProjectResources (legacy, projectFile, &error))
        << error.toStdString ();
    ASSERT_TRUE (synchronizedDuringLoad) << error.toStdString ();
    EXPECT_EQ ((QStringList {"load:consumer", "load:dependency"}), events);

    provider.markDirty ("consumer");
    ASSERT_TRUE (registry.saveDirtyResources (reconciled, projectFile, &error))
        << error.toStdString ();
    EXPECT_EQ ((QStringList {"dependency"}), provider.lastSavedResource ().dependencies);

    events.clear ();
    registry.closeResources ();
    EXPECT_EQ ((QStringList {"close:consumer", "close:dependency"}), events);
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

TEST (ProjectDocumentRegistryTest, LoadsAndCanRollbackANewGeneratedResource)
{
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString projectFile = QDir (directory.path ()).filePath ("Robot.rwproj");
    const QString generatedFile = QDir (directory.path ()).filePath ("generated/RobotScene.wc.xml");
    ASSERT_TRUE (QDir ().mkpath (QFileInfo (generatedFile).absolutePath ()));
    QFile file (generatedFile);
    ASSERT_TRUE (file.open (QIODevice::WriteOnly));
    file.write ("generated");
    file.close ();

    QStringList events;
    FakeDocumentProvider provider ("provider.test", "test.document", &events);
    rws::ProjectDocumentRegistry registry;
    QString error;
    ASSERT_TRUE (registry.registerProvider (&provider, &error));

    rws::ProjectResource generated = resource (
        "scene.main", "test.document", "generated/RobotScene.wc.xml");
    generated.ownership = QStringLiteral ("generated");
    generated.required = true;
    rws::ProjectManifest candidate;
    candidate.resources.push_back (generated);
    candidate.entryPoints.insert (QStringLiteral ("mainWorkCell"), generated.id);

    ASSERT_TRUE (registry.loadNewResource (generated, candidate, projectFile, &error))
        << error.toStdString ();
    EXPECT_EQ ((QStringList {"load:scene.main"}), events);
    provider.markDirty (generated.id);
    EXPECT_TRUE (registry.isDirty ());

    ASSERT_TRUE (registry.unloadResource (generated.id));
    EXPECT_EQ ((QStringList {"load:scene.main", "close:scene.main"}), events);
    EXPECT_FALSE (registry.isDirty ());
    EXPECT_FALSE (registry.unloadResource (generated.id));
}

TEST (ProjectDocumentRegistryTest, FailedNewResourceLoadIsNotTracked)
{
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString projectFile = QDir (directory.path ()).filePath ("Robot.rwproj");
    QStringList events;
    FakeDocumentProvider provider ("provider.test", "test.document", &events);
    provider.failLoading ("scene.main");
    rws::ProjectDocumentRegistry registry;
    QString error;
    ASSERT_TRUE (registry.registerProvider (&provider, &error));

    const rws::ProjectResource generated = resource (
        "scene.main", "test.document", "generated/RobotScene.wc.xml");
    rws::ProjectManifest candidate;
    candidate.resources.push_back (generated);
    candidate.entryPoints.insert (QStringLiteral ("mainWorkCell"), generated.id);

    EXPECT_FALSE (registry.loadNewResource (generated, candidate, projectFile, &error));
    EXPECT_FALSE (error.isEmpty ());
    EXPECT_FALSE (registry.unloadResource (generated.id));
    provider.markDirty (generated.id);
    EXPECT_FALSE (registry.isDirty ());
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

TEST (ProjectDocumentRegistryTest,
      CandidateSuccessCloseFailureRestoresAlreadyClosedProviderSnapshots)
{
    QTemporaryDir directory;
    ASSERT_TRUE (directory.isValid ());
    const QString oldProject = QDir (directory.path ()).filePath ("Old.rwproj");
    const QString candidateProject = QDir (directory.path ()).filePath ("Candidate.rwproj");
    QStringList closeEvents;
    TransitionStateProvider providerA (
        "provider.old-a", "test.old-a", "complete-state-a", true, false, &closeEvents);
    TransitionStateProvider providerB (
        "provider.old-b", "test.old-b", "complete-state-b", true, true, &closeEvents);
    TransitionStateProvider candidateProvider (
        "provider.candidate", "test.candidate", "candidate-state", false, false, nullptr);

    rws::ProjectDocumentRegistry registry;
    QString error;
    ASSERT_TRUE (registry.registerProvider (&providerA, &error));
    ASSERT_TRUE (registry.registerProvider (&providerB, &error));
    ASSERT_TRUE (registry.registerProvider (&candidateProvider, &error));

    const rws::ProjectResource oldB = resource ("old.b", "test.old-b", "old-b.json");
    const rws::ProjectResource oldA = resource ("old.a", "test.old-a", "old-a.json");
    rws::ProjectManifest oldManifest;
    oldManifest.resources = {oldB, oldA};
    ASSERT_TRUE (registry.loadProjectResources (oldManifest, oldProject, &error))
        << error.toStdString ();

    const rws::ProjectResource candidate =
        resource ("candidate.main", "test.candidate", "candidate.json");
    rws::ProjectManifest candidateManifest;
    candidateManifest.resources = {candidate};
    rws::ProjectDocumentRegistry::CandidateTransitionReservation reservation;
    ASSERT_TRUE (registry.preflightCandidateTransition (
        candidateManifest.resources, reservation, &error)) << error.toStdString ();
    registry.suspendResourcesForCandidateTransition (std::move (reservation));
    ASSERT_TRUE (registry.loadProjectResources (
        candidateManifest, candidateProject, &error)) << error.toStdString ();

    EXPECT_FALSE (registry.closeSuspendedResourcesAfterCandidateSuccess (&error));
    EXPECT_TRUE (error.contains (QStringLiteral ("intentional close failure")));
    EXPECT_EQ (QStringList ({QStringLiteral ("close:old.a"),
                            QStringLiteral ("close:old.b")}),
               closeEvents);

    QString closeError;
    ASSERT_TRUE (registry.closeResources (&closeError)) << closeError.toStdString ();
    ASSERT_TRUE (registry.restoreSuspendedResourcesAfterCandidateFailure (&error))
        << error.toStdString ();

    EXPECT_TRUE (providerA.isLoaded (oldA.id));
    EXPECT_TRUE (providerB.isLoaded (oldB.id));
    EXPECT_EQ (QByteArray ("complete-state-a"), providerA.data (oldA.id));
    EXPECT_EQ (QByteArray ("complete-state-b"), providerB.data (oldB.id));
    EXPECT_TRUE (providerA.isDirty (oldA.id));
    EXPECT_TRUE (providerB.isDirty (oldB.id));
    EXPECT_EQ (QSet< QString > ({oldA.id, oldB.id}), registry.activeResourceIds ());
    EXPECT_TRUE (registry.isActiveResourceDirty (oldA.id));
    EXPECT_TRUE (registry.isActiveResourceDirty (oldB.id));

    rws::ProjectDocumentRegistry::CandidateTransitionReservation freshReservation;
    error.clear ();
    EXPECT_TRUE (registry.preflightCandidateTransition (
        candidateManifest.resources, freshReservation, &error)) << error.toStdString ();
}
