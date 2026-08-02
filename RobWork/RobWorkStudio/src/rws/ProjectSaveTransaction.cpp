#include "ProjectSaveTransaction.hpp"

#include "ProjectPathResolver.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>
#include <QUuid>

#include <utility>
#include <memory>
#include <vector>

namespace rws {
namespace {

// 统一错误回填工具：仅当调用方提供了 error 指针时才写入错误描述。
void setError (QString* error, const QString& message)
{
    if (error != nullptr)
        *error = message;
}

// 在正式目标文件的同目录生成唯一临时路径（暂存或备份）。标记插入基本名与
// 完整后缀之间，使 DOMWorkCellSaver 等依赖末尾扩展名选择格式的 Provider 仍能识别 .wc.xml。
QString uniqueSiblingPath (const QString& targetPath, const QString& marker)
{
    const QFileInfo targetInfo (targetPath);
    const QString token = marker + QUuid::createUuid ().toString (QUuid::WithoutBraces);
    const QString completeSuffix = targetInfo.completeSuffix ();
    const QString fileName = completeSuffix.isEmpty ()
                                 ? targetInfo.fileName () + token
                                 : targetInfo.completeBaseName () + token + QStringLiteral (".") +
                                       completeSuffix;
    return QDir (targetInfo.absolutePath ()).filePath (fileName);
}

// 计算文件 SHA-256 指纹（分块读取），用于校验备份内容是否仍与预期一致。
QByteArray fileFingerprint (const QString& path)
{
    QFile file (path);
    if (!file.open (QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash (QCryptographicHash::Sha256);
    while (!file.atEnd ()) {
        const QByteArray chunk = file.read (64 * 1024);
        if (chunk.isEmpty () && file.error () != QFile::NoError)
            return {};
        hash.addData (chunk);
    }
    return hash.result ().toHex ();
}

// 与事务对象外部关联的登记状态：包含性根（写/删边界）以及暂存/备份文件的内容哈希，
// 用于回滚时的"只删除自己写入/备份的文件"校验。因事务可拷贝/移动，状态需按对象指针登记。
struct TransactionState
{
    QString containmentRoot;
    QHash< QString, QByteArray > stagedContentHashes;
    QHash< QString, QByteArray > backupContentHashes;
};

// 登记表的互斥锁与全局映射（事务状态与事务对象分离，避免增大栈上的事务对象体积）。
QMutex transactionStateMutex;
QHash< const ProjectSaveTransaction*, TransactionState > transactionStates;

// 为事务登记包含性根；此后该事务的写/删操作都受此根边界约束。
void setContainmentRoot (const ProjectSaveTransaction* transaction, const QString& root)
{
    QMutexLocker lock (&transactionStateMutex);
    transactionStates[transaction].containmentRoot = root;
}

// 查询事务的包含性根（空表示未约束）。
QString containmentRootFor (const ProjectSaveTransaction* transaction)
{
    QMutexLocker lock (&transactionStateMutex);
    return transactionStates.value (transaction).containmentRoot;
}

// 记录暂存文件的内容哈希，供回滚时校验暂存文件仍属于本事务。
void recordStagedContentHash (const ProjectSaveTransaction* transaction,
                              const QString& stagedPath,
                              const QByteArray& hash)
{
    QMutexLocker lock (&transactionStateMutex);
    transactionStates[transaction].stagedContentHashes.insert (stagedPath, hash);
}

// 读取暂存文件内容哈希。
QByteArray stagedContentHash (const ProjectSaveTransaction* transaction, const QString& stagedPath)
{
    QMutexLocker lock (&transactionStateMutex);
    return transactionStates.value (transaction).stagedContentHashes.value (stagedPath);
}

// 记录备份文件的内容哈希（备份 = 被替换的正式文件原内容）。
void recordBackupContentHash (const ProjectSaveTransaction* transaction,
                              const QString& backupPath,
                              const QByteArray& hash)
{
    QMutexLocker lock (&transactionStateMutex);
    transactionStates[transaction].backupContentHashes.insert (backupPath, hash);
}

// 读取备份文件内容哈希。
QByteArray backupContentHash (const ProjectSaveTransaction* transaction, const QString& backupPath)
{
    QMutexLocker lock (&transactionStateMutex);
    return transactionStates.value (transaction).backupContentHashes.value (backupPath);
}

void clearStagedContentHashes (const ProjectSaveTransaction* transaction)
{
    QMutexLocker lock (&transactionStateMutex);
    const auto iterator = transactionStates.find (transaction);
    if (iterator != transactionStates.end ())
        iterator->stagedContentHashes.clear ();
    if (iterator != transactionStates.end ())
        iterator->backupContentHashes.clear ();
}

// 删除事务的登记状态（事务析构时调用，避免映射残留）。
void forgetTransactionState (const ProjectSaveTransaction* transaction)
{
    QMutexLocker lock (&transactionStateMutex);
    transactionStates.remove (transaction);
}

// 拷贝事务状态：源无登记时移除目标登记，否则复制（用于拷贝构造/赋值）。
void copyTransactionState (const ProjectSaveTransaction* source,
                           const ProjectSaveTransaction* destination)
{
    QMutexLocker lock (&transactionStateMutex);
    const auto sourceState = transactionStates.constFind (source);
    if (sourceState == transactionStates.constEnd ())
        transactionStates.remove (destination);
    else
        transactionStates.insert (destination, sourceState.value ());
}

// 搬移事务状态：从源转移到目标并清除源（用于移动构造/赋值）。
void moveTransactionState (const ProjectSaveTransaction* source,
                           const ProjectSaveTransaction* destination)
{
    QMutexLocker lock (&transactionStateMutex);
    const auto sourceState = transactionStates.find (source);
    if (sourceState == transactionStates.end ()) {
        transactionStates.remove (destination);
        return;
    }
    transactionStates.insert (destination, std::move (sourceState.value ()));
    transactionStates.erase (sourceState);
}

// 按事务的包含性根获取写入守卫：无约束时直接放行，有约束时对目标路径取守卫。
bool acquireTransactionWriteGuard (const ProjectSaveTransaction* transaction,
                                   const QString& targetPath,
                                   ProjectWriteGuard& guard,
                                   QString* error)
{
    const QString root = containmentRootFor (transaction);
    return root.isEmpty () || ProjectWriteGuard::acquire (root, targetPath, guard, error);
}

// 删除事务文件：无包含性根时直接 QFile::remove；有根时走包含校验的安全删除。
bool removeTransactionFile (const ProjectSaveTransaction* transaction,
                            const QString& path,
                            QString* error)
{
    const QString root = containmentRootFor (transaction);
    if (root.isEmpty ()) {
        if (QFile::remove (path))
            return true;
        setError (error, QStringLiteral ("Could not remove transaction file: %1").arg (path));
        return false;
    }
    return ProjectPathResolver::removeContainedFile (root, path, error);
}

// 在包含性根内按文件名 + 内容哈希定位安全的备份文件（用于回滚还原备份）。
// 仅当文件存在于项目根内、可取得写入守卫且内容与登记哈希一致时才返回，避免
// 误用同名但内容不同的外部文件或符号链接。
QString locateSafeBackupPath (const ProjectSaveTransaction* transaction, const QString& backupPath)
{
    const QString root = containmentRootFor (transaction);
    const QByteArray expectedHash = backupContentHash (transaction, backupPath);
    if (root.isEmpty () || expectedHash.isEmpty ())
        return {};

    const QString backupName = QFileInfo (backupPath).fileName ();
    QDirIterator iterator (root,
                           QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden,
                           QDirIterator::Subdirectories);
    while (iterator.hasNext ()) {
        const QString candidate = iterator.next ();
        if (QFileInfo (candidate).fileName () != backupName)
            continue;
        ProjectWriteGuard writeGuard;
        if (!ProjectWriteGuard::acquire (root, candidate, writeGuard, nullptr))
            continue;
        if (fileFingerprint (candidate) == expectedHash)
            return QDir::cleanPath (candidate);
    }
    return {};
}

}    // namespace

// 析构兜底：未提交即销毁事务时自动回滚，保证任何情况下不留半替换状态。
ProjectSaveTransaction::ProjectSaveTransaction (ExistingTargetPolicy policy) :
    _existingTargetPolicy (policy)
{}

ProjectSaveTransaction::ProjectSaveTransaction (const ProjectSaveTransaction& other) :
    _existingTargetPolicy (other._existingTargetPolicy), _staged (other._staged),
    _installed (other._installed), _committed (other._committed)
{
    copyTransactionState (&other, this);
}

ProjectSaveTransaction& ProjectSaveTransaction::operator= (const ProjectSaveTransaction& other)
{
    if (this == &other)
        return *this;
    if (!_committed)
        rollback ();
    _existingTargetPolicy = other._existingTargetPolicy;
    _staged = other._staged;
    _installed = other._installed;
    _committed = other._committed;
    copyTransactionState (&other, this);
    return *this;
}

ProjectSaveTransaction::ProjectSaveTransaction (ProjectSaveTransaction&& other) :
    _existingTargetPolicy (other._existingTargetPolicy), _staged (std::move (other._staged)),
    _installed (other._installed), _committed (other._committed)
{
    moveTransactionState (&other, this);
    other._installed = false;
    other._committed = true;
}

ProjectSaveTransaction& ProjectSaveTransaction::operator= (ProjectSaveTransaction&& other)
{
    if (this == &other)
        return *this;
    if (!_committed)
        rollback ();
    _existingTargetPolicy = other._existingTargetPolicy;
    _staged = std::move (other._staged);
    _installed = other._installed;
    _committed = other._committed;
    moveTransactionState (&other, this);
    other._installed = false;
    other._committed = true;
    return *this;
}

ProjectSaveTransaction::~ProjectSaveTransaction ()
{
    if (!_committed)
        rollback ();
    forgetTransactionState (this);
}

void ProjectSaveTransaction::setContainmentRoot (ProjectSaveTransaction& transaction,
                                                  const QString& containmentRoot)
{
    ::rws::setContainmentRoot (&transaction, containmentRoot);
}

bool ProjectSaveTransaction::validateTargetPath (const QString& targetPath, QString* error) const
{
    const QString containmentRoot = containmentRootFor (this);
    return containmentRoot.isEmpty () ||
        ProjectPathResolver::validateContainedWritePath (containmentRoot, targetPath, error);
}

// 暂存阶段：确保目标目录存在，然后要求 Provider 把内容写入暂存路径。
// 此阶段不触碰正式文件，因此多资源暂存过程中任何一处失败都不会产生半提交。
bool ProjectSaveTransaction::stage (ProjectDocumentProvider& provider,
                                    const ProjectResource& resource,
                                    const ProjectDocumentContext& context,
                                    const QString& targetPath,
                                    QString* error)
{
    const QFileInfo targetInfo (targetPath);
    if (!validateTargetPath (targetInfo.absoluteFilePath (), error))
        return false;
    ProjectWriteGuard writeGuard;
    const QString root = containmentRootFor (this);
    if (!root.isEmpty () && !ProjectWriteGuard::acquire (root, targetInfo.absoluteFilePath (),
                                                         writeGuard, error))
        return false;
    // 目标目录可能尚未创建（如资源首次保存到新子目录），这里主动补建。
    if (!QDir ().mkpath (targetInfo.absolutePath ())) {
        setError (error,
                  QString::fromUtf8 ("无法创建资源目录：%1。").arg (
                      targetInfo.absolutePath ()));
        return false;
    }

    StagedResource staged;
    staged.provider = &provider;
    staged.resource = resource;
    staged.targetPath = targetInfo.absoluteFilePath ();
    staged.stagedPath = uniqueSiblingPath (staged.targetPath, QStringLiteral (".rwstage-"));
    staged.backupPath = uniqueSiblingPath (staged.targetPath, QStringLiteral (".rwbackup-"));

    // Provider 只能写暂存路径，不能直接接触正式目标。这样后续资源暂存失败时，
    // 之前已经成功暂存的文档也不会污染磁盘上的最后一次有效版本。
    if (!provider.saveResource (resource, context, staged.stagedPath, error)) {
        // 暂存失败后立即清理残留暂存文件；调用方销毁事务时其余资源一并回滚。
        QFile::remove (staged.stagedPath);
        if (error != nullptr && error->isEmpty ()) {
            *error = QString::fromUtf8 ("资源“%1”暂存失败。").arg (resource.id);
        }
        return false;
    }
    // Provider 声称成功但没生成文件属于契约违约，同样按失败处理。
    if (!QFileInfo::exists (staged.stagedPath) || !QFileInfo (staged.stagedPath).isFile ()) {
        QFile::remove (staged.stagedPath);
        setError (error,
                  QString::fromUtf8 ("资源“%1”的 Provider 未生成暂存文件。").arg (
                      resource.id));
        return false;
    }
    const QByteArray stagedHash = fileFingerprint (staged.stagedPath);
    if (stagedHash.isEmpty ()) {
        QFile::remove (staged.stagedPath);
        setError (error,
                  QStringLiteral ("Could not fingerprint staged transaction content: %1")
                      .arg (staged.stagedPath));
        return false;
    }
    recordStagedContentHash (this, staged.stagedPath, stagedHash);

    _staged.push_back (staged);
    return true;
}

// 暂存一个既有普通文件的副本：把源文件复制到目标同目录的暂存路径，供恢复快照、
// rwpack 输出等非 Provider 场景与业务文档走同一套“先暂存、后统一提交”事务。
bool ProjectSaveTransaction::stageCopy (const QString& sourcePath,
                                        const QString& targetPath,
                                        QString* error)
{
    const QFileInfo sourceInfo (sourcePath);
    const QFileInfo targetInfo (targetPath);
    if (!sourceInfo.isFile ()) {
        setError (error, QString::fromUtf8 ("无法暂存不存在的源文件：%1。").arg (sourcePath));
        return false;
    }
    if (targetInfo.exists () && !targetInfo.isFile ()) {
        setError (error, QString::fromUtf8 ("事务目标不是普通文件：%1。").arg (targetPath));
        return false;
    }
    if (!validateTargetPath (targetInfo.absoluteFilePath (), error))
        return false;
    ProjectWriteGuard writeGuard;
    const QString root = containmentRootFor (this);
    if (!root.isEmpty () && !ProjectWriteGuard::acquire (root, targetInfo.absoluteFilePath (),
                                                         writeGuard, error))
        return false;
    if (!QDir ().mkpath (targetInfo.absolutePath ())) {
        setError (error, QString::fromUtf8 ("无法创建资源目录：%1。").arg (targetInfo.absolutePath ()));
        return false;
    }
    StagedResource staged;
    staged.targetPath = targetInfo.absoluteFilePath ();
    staged.stagedPath = uniqueSiblingPath (staged.targetPath, QStringLiteral (".rwstage-"));
    staged.backupPath = uniqueSiblingPath (staged.targetPath, QStringLiteral (".rwbackup-"));
    if (!QFile::copy (sourceInfo.absoluteFilePath (), staged.stagedPath)) {
        setError (error, QString::fromUtf8 ("无法暂存源文件：%1。").arg (sourcePath));
        return false;
    }
    const QByteArray stagedHash = fileFingerprint (staged.stagedPath);
    if (stagedHash.isEmpty ()) {
        QFile::remove (staged.stagedPath);
        setError (error,
                  QStringLiteral ("Could not fingerprint staged transaction content: %1")
                      .arg (staged.stagedPath));
        return false;
    }
    recordStagedContentHash (this, staged.stagedPath, stagedHash);
    _staged.push_back (staged);
    return true;
}

// 暂存一段内存字节到目标同目录的暂存文件：用于把项目清单等内存内容与磁盘资源
// 一起纳入同一提交单元，保证清单与资源要么全部更新、要么全部回滚。
bool ProjectSaveTransaction::stageBytes (const QByteArray& bytes,
                                         const QString& targetPath,
                                         QString* error)
{
    const QFileInfo targetInfo (targetPath);
    if (targetInfo.exists () && !targetInfo.isFile ()) {
        setError (error, QString::fromUtf8 ("事务目标不是普通文件：%1。").arg (targetPath));
        return false;
    }
    if (!validateTargetPath (targetInfo.absoluteFilePath (), error))
        return false;
    ProjectWriteGuard writeGuard;
    const QString root = containmentRootFor (this);
    if (!root.isEmpty () && !ProjectWriteGuard::acquire (root, targetInfo.absoluteFilePath (),
                                                         writeGuard, error))
        return false;
    if (!QDir ().mkpath (targetInfo.absolutePath ())) {
        setError (error, QString::fromUtf8 ("无法创建资源目录：%1。").arg (targetInfo.absolutePath ()));
        return false;
    }
    StagedResource staged;
    staged.targetPath = targetInfo.absoluteFilePath ();
    staged.stagedPath = uniqueSiblingPath (staged.targetPath, QStringLiteral (".rwstage-"));
    staged.backupPath = uniqueSiblingPath (staged.targetPath, QStringLiteral (".rwbackup-"));
    QSaveFile file (staged.stagedPath);
    if (!file.open (QIODevice::WriteOnly) || file.write (bytes) != bytes.size () || !file.commit ()) {
        setError (error, QString::fromUtf8 ("无法暂存内存文件：%1。").arg (targetPath));
        return false;
    }
    const QByteArray stagedHash = fileFingerprint (staged.stagedPath);
    if (stagedHash.isEmpty ()) {
        QFile::remove (staged.stagedPath);
        setError (error,
                  QStringLiteral ("Could not fingerprint staged transaction content: %1")
                      .arg (staged.stagedPath));
        return false;
    }
    recordStagedContentHash (this, staged.stagedPath, stagedHash);
    _staged.push_back (staged);
    return true;
}

// 安装阶段保留全部备份，使调用方能在后续验证或外部状态切换失败时整体回滚。
bool ProjectSaveTransaction::install (QString* error)
{
    if (_installed)
        return true;

    for (int index = 0; index < _staged.size (); ++index) {
        StagedResource& item = _staged[index];
        if (!validateTargetPath (item.targetPath, error)) {
            rollback (nullptr);
            return false;
        }
        ProjectWriteGuard writeGuard;
        const QString root = containmentRootFor (this);
        if (!root.isEmpty () && !ProjectWriteGuard::acquire (root, item.targetPath,
                                                             writeGuard, error)) {
            rollback (nullptr);
            return false;
        }
        const QByteArray expectedStagedHash = stagedContentHash (this, item.stagedPath);
        if (expectedStagedHash.isEmpty () ||
            fileFingerprint (item.stagedPath) != expectedStagedHash) {
            setError (error,
                      QStringLiteral ("Staged transaction content changed before installation: %1")
                          .arg (item.stagedPath));
            rollback (nullptr);
            return false;
        }
        if (_existingTargetPolicy == ExistingTargetPolicy::Reject &&
            QFileInfo::exists (item.targetPath)) {
            setError (error,
                      QString::fromUtf8 ("事务目标已经存在，拒绝覆盖：%1。").arg (
                          item.targetPath));
            rollback ();
            return false;
        }
        // 若正式目标已存在，先原子改名成备份文件，为回滚保留原内容。
        if (QFileInfo::exists (item.targetPath)) {
            const QByteArray originalHash = fileFingerprint (item.targetPath);
            if (!QFile::rename (item.targetPath, item.backupPath)) {
                setError (error,
                          QString::fromUtf8 ("无法备份待替换资源：%1。").arg (
                              item.targetPath));
                rollback ();
                return false;
            }
            item.targetBackedUp = true;
            if (!originalHash.isEmpty ())
                recordBackupContentHash (this, item.backupPath, originalHash);
        }

        // 再把暂存文件安装到正式位置；失败时原文件仍可在备份路径找回。
        if (!QFile::rename (item.stagedPath, item.targetPath)) {
            setError (error,
                      QString::fromUtf8 ("无法提交资源暂存文件：%1。").arg (
                          item.targetPath));
            rollback ();
            return false;
        }
        item.targetInstalled = true;
    }

    _installed = true;
    return true;
}

void ProjectSaveTransaction::finalize ()
{
    finalize (nullptr);
}

bool ProjectSaveTransaction::finalize (QString* error)
{
    if (!_installed || _committed)
        return true;

    struct FinalizeBackup
    {
        StagedResource* item;
        std::unique_ptr< ProjectWriteGuard > guard;
    };
    std::vector< FinalizeBackup > backups;
    backups.reserve (_staged.size ());
    for (StagedResource& item : _staged) {
        if (item.targetBackedUp) {
            auto writeGuard = std::make_unique< ProjectWriteGuard > ();
            if (!acquireTransactionWriteGuard (this, item.targetPath, *writeGuard, error))
                return false;
            if (!QFileInfo::exists (item.backupPath)) {
                setError (error,
                          QStringLiteral ("Transaction backup is missing before finalization: %1")
                              .arg (item.backupPath));
                return false;
            }
            const QByteArray expectedBackupHash = backupContentHash (this, item.backupPath);
            if (expectedBackupHash.isEmpty () ||
                fileFingerprint (item.backupPath) != expectedBackupHash) {
                setError (error,
                          QStringLiteral ("Transaction backup changed before finalization; it was preserved for recovery: %1")
                              .arg (item.backupPath));
                return false;
            }
            backups.push_back ({&item, std::move (writeGuard)});
        }
    }

    QStringList cleanupWarnings;
    for (const FinalizeBackup& backup : backups) {
        QString cleanupError;
        if (!removeTransactionFile (this, backup.item->backupPath, &cleanupError))
            cleanupWarnings.push_back (
                cleanupError.isEmpty ()
                    ? QStringLiteral ("Could not remove transaction backup: %1")
                          .arg (backup.item->backupPath)
                    : cleanupError);
    }
    for (StagedResource& item : _staged) {
        if (item.provider != nullptr)
            item.provider->markClean (item.resource.id);
        item.targetBackedUp = false;
        item.targetInstalled = false;
    }
    clearStagedContentHashes (this);
    _committed = true;
    if (!cleanupWarnings.isEmpty ())
        setError (error, cleanupWarnings.join (QLatin1Char ('\n')));
    return true;
}

bool ProjectSaveTransaction::commit (QString* error)
{
    if (!install (error))
        return false;
    return finalize (error);
}

// 回滚：逆序处理所有已暂存资源——删除已安装的正式文件、用备份还原原文件、
// 清理残留暂存文件。逆序保证后安装的资源先恢复，与提交顺序对称。
void ProjectSaveTransaction::rollback ()
{
    rollback (nullptr);
}

void ProjectSaveTransaction::rollback (QString* error)
{
    if (_committed)
        return;

    QStringList rollbackIssues;
    for (int index = _staged.size () - 1; index >= 0; --index) {
        StagedResource& item = _staged[index];
        ProjectWriteGuard writeGuard;
        QString guardError;
        if (!acquireTransactionWriteGuard (this, item.targetPath, writeGuard, &guardError)) {
            rollbackIssues.push_back (
                QStringLiteral ("Transaction rollback could not acquire a write guard: %1")
                    .arg (guardError));
            if (item.targetInstalled)
                rollbackIssues.push_back (
                    QStringLiteral ("Transaction rollback preserved target: %1").arg (item.targetPath));
            if (item.targetBackedUp)
            {
                const QString recoveryPath = locateSafeBackupPath (this, item.backupPath);
                if (!recoveryPath.isEmpty ())
                    rollbackIssues.push_back (
                        QStringLiteral ("Original transaction backup is available for recovery: %1")
                            .arg (recoveryPath));
                else
                    rollbackIssues.push_back (
                        QStringLiteral ("Transaction rollback could not verify a safe backup recovery path; manual recovery may be required."));
            }
            continue;
        }
        bool mayRestore = true;
        if (item.targetBackedUp) {
            const QByteArray expectedBackupHash = backupContentHash (this, item.backupPath);
            if (expectedBackupHash.isEmpty () || !QFileInfo::exists (item.backupPath) ||
                fileFingerprint (item.backupPath) != expectedBackupHash) {
                mayRestore = false;
                rollbackIssues.push_back (
                    QStringLiteral ("Transaction rollback preserved target because backup changed; recovery copy: %1")
                        .arg (item.backupPath));
            }
        }
        if (item.targetInstalled) {
            const QByteArray currentHash = fileFingerprint (item.targetPath);
            const QByteArray expectedStagedHash = stagedContentHash (this, item.stagedPath);
            mayRestore = mayRestore && !expectedStagedHash.isEmpty () &&
                currentHash == expectedStagedHash;
            if (mayRestore) {
                if (!removeTransactionFile (this, item.targetPath, nullptr))
                    rollbackIssues.push_back (
                        QStringLiteral ("Could not remove installed target: %1").arg (item.targetPath));
            }
            else if (!mayRestore) {
                rollbackIssues.push_back (
                    QStringLiteral ("Transaction rollback preserved externally changed target: %1")
                        .arg (item.targetPath));
            }
        }
        if (item.targetBackedUp && QFileInfo::exists (item.backupPath)) {
            if (mayRestore) {
                if (!QFile::rename (item.backupPath, item.targetPath))
                    rollbackIssues.push_back (
                        QStringLiteral ("Could not restore transaction backup: %1").arg (item.targetPath));
            }
            else
                rollbackIssues.push_back (
                    QStringLiteral ("Original transaction backup is available for recovery: %1")
                        .arg (item.backupPath));
        }
        if (QFileInfo::exists (item.stagedPath))
            removeTransactionFile (this, item.stagedPath, nullptr);
        item.targetInstalled = false;
        item.targetBackedUp = false;
    }
    clearStagedContentHashes (this);
    _installed = false;
    if (!rollbackIssues.isEmpty ())
        setError (error, rollbackIssues.join (QLatin1Char ('\n')));
}

}    // namespace rws
