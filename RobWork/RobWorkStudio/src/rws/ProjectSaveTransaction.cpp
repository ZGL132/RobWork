#include "ProjectSaveTransaction.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QUuid>

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

}    // namespace

// 析构兜底：未提交即销毁事务时自动回滚，保证任何情况下不留半替换状态。
ProjectSaveTransaction::~ProjectSaveTransaction ()
{
    if (!_committed)
        rollback ();
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
    _staged.push_back (staged);
    return true;
}

// 提交阶段：按暂存顺序逐个把正式文件备份、再把暂存文件安装为正式文件。
// 任一步失败立即回滚全部已安装目标；只有所有资源都替换成功才进入清理阶段。
bool ProjectSaveTransaction::commit (QString* error)
{
    for (int index = 0; index < _staged.size (); ++index) {
        StagedResource& item = _staged[index];
        // 若正式目标已存在，先原子改名成备份文件，为回滚保留原内容。
        if (QFileInfo::exists (item.targetPath)) {
            if (!QFile::rename (item.targetPath, item.backupPath)) {
                setError (error,
                          QString::fromUtf8 ("无法备份待替换资源：%1。").arg (
                              item.targetPath));
                rollback ();
                return false;
            }
            item.targetBackedUp = true;
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

    // 只有所有替换都成功后才删除备份并清除 Provider 脏状态，保证调用方看到的
    // “已保存”状态一定对应完整提交，而不是部分文件已经更新。
    for (StagedResource& item : _staged) {
        if (item.targetBackedUp)
            QFile::remove (item.backupPath);
        if (item.provider != nullptr)
            item.provider->markClean (item.resource.id);
    }
    _committed = true;
    return true;
}

// 回滚：逆序处理所有已暂存资源——删除已安装的正式文件、用备份还原原文件、
// 清理残留暂存文件。逆序保证后安装的资源先恢复，与提交顺序对称。
void ProjectSaveTransaction::rollback ()
{
    for (int index = _staged.size () - 1; index >= 0; --index) {
        StagedResource& item = _staged[index];
        if (item.targetInstalled)
            QFile::remove (item.targetPath);
        if (item.targetBackedUp && QFileInfo::exists (item.backupPath))
            QFile::rename (item.backupPath, item.targetPath);
        if (QFileInfo::exists (item.stagedPath))
            QFile::remove (item.stagedPath);
        item.targetInstalled = false;
        item.targetBackedUp = false;
    }
}

}    // namespace rws
