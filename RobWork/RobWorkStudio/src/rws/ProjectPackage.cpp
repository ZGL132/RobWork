#include "ProjectPackage.hpp"

#include "ProjectManifestJson.hpp"
#include "ProjectPathResolver.hpp"
#include "ProjectSaveTransaction.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QUuid>
#include <QTemporaryDir>

#include <zip.h>

namespace rws {
namespace {

// 统一错误回填工具：仅当调用方提供了 error 指针时才写入错误描述。
void setError (QString* error, const QString& message)
{
    if (error != nullptr)
        *error = message;
}

// 归档条目路径安全校验：条目必须是项目根目录内的相对普通路径，拒绝绝对路径、
// 空路径以及 ".." 目录穿越，防止恶意 ZIP 解包时覆盖项目外文件。
bool safeArchivePath (const QString& path)
{
    const QString normalized = QDir::fromNativeSeparators (QDir::cleanPath (path));
    return !normalized.isEmpty () && !QFileInfo (normalized).isAbsolute () &&
        normalized != QStringLiteral ("..") && !normalized.startsWith (QStringLiteral ("../"));
}

// 把一个磁盘文件加入打开的 ZIP 归档。UTF-8 编码保证中文文件名可移植；
// 失败时释放已创建的 zip_source 并按 zip_file_add 的语义由 libzip 收回条目。
bool addFile (zip_t* archive, const QString& sourcePath, const QString& archivePath, QString* error)
{
    zip_source_t* source = zip_source_file (archive, sourcePath.toUtf8 ().constData (), 0, 0);
    if (source == nullptr || zip_file_add (archive,
                                           archivePath.toUtf8 ().constData (),
                                           source,
                                           ZIP_FL_ENC_UTF_8 | ZIP_FL_OVERWRITE) < 0) {
        if (source != nullptr)
            zip_source_free (source);
        setError (error, QString::fromUtf8 ("无法加入 rwpack 条目：%1。").arg (archivePath));
        return false;
    }
    return true;
}

}    // namespace

// 打包：把项目清单与全部 project/generated 资源写入标准 ZIP(rwpack)。
// 先写到同目录的暂存文件，全部条目加入成功后才经 ProjectSaveTransaction 原子提交到
// 正式 .rwpack 路径，避免中途失败留下半截压缩包。
bool ProjectPackage::create (const QString& projectFilePath,
                             const ProjectManifest& manifest,
                             const QString& packageFilePath,
                             QString* error)
{
    const QFileInfo packageInfo (packageFilePath);
    if (!QDir ().mkpath (packageInfo.absolutePath ())) {
        setError (error, QString::fromUtf8 ("无法创建 rwpack 目录：%1。").arg (packageInfo.absolutePath ()));
        return false;
    }
    // 归档先写入同目录唯一暂存文件；ZIP 完成后再用事务原子改名到正式路径。
    const QString stagedPackagePath = packageInfo.absoluteFilePath () +
        QStringLiteral (".rwpack-stage-") + QUuid::createUuid ().toString (QUuid::WithoutBraces);
    int zipError = 0;
    zip_t* archive = zip_open (stagedPackagePath.toUtf8 ().constData (), ZIP_CREATE | ZIP_TRUNCATE, &zipError);
    if (archive == nullptr) {
        setError (error, QString::fromUtf8 ("无法创建 rwpack 文件：%1。").arg (packageFilePath));
        return false;
    }
    const QString root = QFileInfo (projectFilePath).absolutePath ();
    // 清单以临时文件形式加入归档（条目名固定 project.rwproj），避免 libzip 直接读内存字节的复杂编码。
    QTemporaryFile manifestFile (QDir (root).filePath (QStringLiteral (".rwpack-manifest-XXXXXX")));
    const QByteArray manifestJson = ProjectManifestJson::toJson (manifest);
    bool ok = manifestFile.open () && manifestFile.write (manifestJson) == manifestJson.size () &&
        manifestFile.flush ();
    manifestFile.close ();
    if (ok)
        ok = addFile (archive, manifestFile.fileName (), QStringLiteral ("project.rwproj"), error);
    // external 资源由外部系统管理，不进入归档，保证包可安全发送到其它机器。
    for (const ProjectResource& resource : manifest.resources) {
        if (!ok || resource.ownership == QStringLiteral ("external"))
            continue;
        QString source;
        if (!ProjectPathResolver::resolveResource (projectFilePath, resource, source, error) ||
            !QFileInfo (source).isFile ()) {
            setError (error, QString::fromUtf8 ("无法打包项目资源：%1。").arg (resource.id));
            ok = false;
            continue;
        }
        // 归档条目使用相对路径，解包后项目仍可移植；越界路径会被拒绝。
        const QString relative = QDir::fromNativeSeparators (QDir (root).relativeFilePath (source));
        ok = safeArchivePath (relative) && addFile (archive, source, relative, error);
    }
    if (!ok) {
        zip_discard (archive);
        QFile::remove (stagedPackagePath);
        return false;
    }
    if (zip_close (archive) != 0) {
        setError (error, QString::fromUtf8 ("无法提交 rwpack 文件：%1。").arg (
            QString::fromUtf8 (zip_strerror (archive))));
        zip_discard (archive);
        QFile::remove (stagedPackagePath);
        return false;
    }
    // 用事务把暂存包安装为正式包，失败时回滚且不残留半成品。
    ProjectSaveTransaction transaction;
    const bool committed = transaction.stageCopy (stagedPackagePath, packageInfo.absoluteFilePath (), error) &&
        transaction.commit (error);
    QFile::remove (stagedPackagePath);
    return committed;
}

// 解包：校验目标目录不存在，把 rwpack 安全解压到同目录的暂存目录，验证清单与全部
// 自有资源完整后，把整个暂存目录原子 rename 到目标位置。任何非法/缺失条目都会失败，
// 不会把损坏的包半途暴露给用户。
bool ProjectPackage::extract (const QString& packageFilePath,
                              const QString& targetDirectory,
                              QString& projectFilePath,
                              QString* error)
{
    if (QFileInfo::exists (targetDirectory)) {
        setError (error, QString::fromUtf8 ("rwpack 解包目标目录必须不存在：%1。").arg (targetDirectory));
        return false;
    }
    int zipError = 0;
    zip_t* archive = zip_open (packageFilePath.toUtf8 ().constData (), ZIP_RDONLY, &zipError);
    if (archive == nullptr) {
        setError (error, QString::fromUtf8 ("无法打开 rwpack 文件。"));
        return false;
    }
    const QString parent = QFileInfo (targetDirectory).absolutePath ();
    // 先解压到同父目录的暂存目录，全部成功后再原子切换到用户指定位置。
    QTemporaryDir staging (QDir (parent).filePath (QStringLiteral (".rwpack-stage-XXXXXX")));
    const zip_int64_t entryCount = zip_get_num_entries (archive, 0);
    // 解包资源上限：条目数量防 ZIP 炸弹条目膨胀，总大小防磁盘耗尽。
    constexpr zip_uint64_t maxEntryCount = 10000;
    constexpr zip_uint64_t maxTotalSize = 2ULL * 1024ULL * 1024ULL * 1024ULL;
    zip_uint64_t totalSize = 0;
    bool ok = staging.isValid () && entryCount >= 0 &&
        static_cast< zip_uint64_t > (entryCount) <= maxEntryCount;
    for (zip_uint64_t index = 0; ok && index < static_cast< zip_uint64_t > (entryCount); ++index) {
        const char* name = zip_get_name (archive, index, ZIP_FL_ENC_GUESS);
        zip_stat_t stat;
        zip_stat_init (&stat);
        // 每个条目必须可安全解析、单条不超 1GB、累计不超上限，且是项目内相对路径。
        if (name == nullptr || zip_stat_index (archive, index, 0, &stat) != 0 ||
            stat.size > 1024ULL * 1024ULL * 1024ULL || stat.size > maxTotalSize - totalSize ||
            !safeArchivePath (QString::fromUtf8 (name))) {
            ok = false;
            break;
        }
        totalSize += stat.size;
        const QString output = QDir (staging.path ()).filePath (QString::fromUtf8 (name));
        if (!QDir ().mkpath (QFileInfo (output).absolutePath ())) { ok = false; break; }
        zip_file_t* input = zip_fopen_index (archive, index, 0);
        // 单条目也用 QSaveFile 原子写入，流式读取避免一次性加载大文件。
        QSaveFile file (output);
        if (input == nullptr || !file.open (QIODevice::WriteOnly)) { if (input) zip_fclose (input); ok = false; break; }
        QByteArray buffer (64 * 1024, Qt::Uninitialized);
        zip_int64_t read = 0;
        while ((read = zip_fread (input, buffer.data (), buffer.size ())) > 0)
            if (file.write (buffer.constData (), read) != read) { ok = false; break; }
        zip_fclose (input);
        if (read < 0 || !ok || !file.commit ()) { ok = false; break; }
    }
    zip_close (archive);
    const QString stagedProject = QDir (staging.path ()).filePath (QStringLiteral ("project.rwproj"));
    ProjectManifest manifest;
    QFile manifestFile (stagedProject);
    if (!ok) {
        setError (error, QString::fromUtf8 ("rwpack 包含无效或无法写入的条目。"));
        return false;
    }
    // 清单必须可解析，否则整个包视为损坏。
    if (!manifestFile.open (QIODevice::ReadOnly) ||
        !ProjectManifestJson::fromJson (manifestFile.readAll (), manifest, error))
        return false;
    // 包内声明的每个自有资源都必须真实存在，缺任一文件即拒绝解包。
    for (const ProjectResource& resource : manifest.resources) {
        if (resource.ownership == QStringLiteral ("external"))
            continue;
        QString resourcePath;
        if (!ProjectPathResolver::resolveResource (stagedProject, resource, resourcePath, error) ||
            !QFileInfo (resourcePath).isFile ()) {
            setError (error, QString::fromUtf8 ("rwpack 缺少项目资源：%1。").arg (resource.id));
            return false;
        }
    }
    // Windows 会拒绝重命名仍被 QFile 持有的目录。清单已完整读入并校验完毕，必须先释放
    // 该句柄，才能把临时解包目录作为一个整体原子切换到用户指定的目标位置。
    manifestFile.close ();
    if (!QDir (parent).rename (QFileInfo (staging.path ()).fileName (),
                              QFileInfo (targetDirectory).fileName ())) {
        setError (error, QString::fromUtf8 ("无法提交 rwpack 解包目录：%1。").arg (targetDirectory));
        return false;
    }
    staging.setAutoRemove (false);
    projectFilePath = QDir (targetDirectory).filePath (QStringLiteral ("project.rwproj"));
    return true;
}

}    // namespace rws
