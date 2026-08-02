#include "ProjectPathResolver.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <filesystem>
#ifdef Q_OS_WIN
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#include <windows.h>
#endif

namespace rws {
namespace {

// 统一错误回填工具：仅当调用方提供了 error 指针时才写入错误描述。
void setError (QString* error, const QString& message)
{
    if (error != nullptr)
        *error = message;
}

// 目录边界检查：判断 candidatePath 是否位于 rootPath 之内（含等于 rootPath 本身）。
bool isInsideRoot (const QString& rootPath, const QString& candidatePath)
{
#ifdef Q_OS_WIN
    const Qt::CaseSensitivity sensitivity = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity sensitivity = Qt::CaseSensitive;
#endif

    // QDir::cleanPath 在 Windows 上通常仍返回“/”，而 QDir::separator() 返回“\”。
    // 若直接混用，两条语义相同的路径可能因为分隔符不同被误判为越界。
    // 因此先统一把分隔符规范化为“/”，再做大小写敏感（Linux）/ 不敏感（Windows）比较。
    const QString root = QDir::fromNativeSeparators (QDir::cleanPath (rootPath));
    const QString candidate =
        QDir::fromNativeSeparators (QDir::cleanPath (candidatePath));
    if (candidate.compare (root, sensitivity) == 0)
        return true;

    // 前缀匹配时必须保证 root 以分隔符结尾，否则 "/proj" 会把 "/proj-extra" 误判为子路径。
    const QString rootWithSeparator = root.endsWith (QLatin1Char ('/'))
                                          ? root
                                          : root + QLatin1Char ('/');
    return candidate.startsWith (rootWithSeparator, sensitivity);
}

// 判断一个相对路径不逃出父目录：拒绝绝对路径、裸 ".." 以及 ".." 前缀，
// 用于验证候选写入路径相对项目根属于安全的子路径。
bool isContainedRelativePath (const QString& relativePath)
{
    const QString normalized = QDir::fromNativeSeparators (QDir::cleanPath (relativePath));
    return !QDir::isAbsolutePath (normalized) && normalized != QStringLiteral ("..") &&
        !normalized.startsWith (QStringLiteral ("../"));
}

// 弱规范化路径（std::filesystem::weakly_canonical）：解析已存在的符号链接并去重
// "."/".."，但允许尾段尚不存在（候选写入目标可能尚未创建）。用于包含性比较，
// 防止符号链接把"看起来在项目内"的路径实际指向项目外。
bool weaklyCanonicalPath (const QString& path, QString& canonicalPath)
{
    std::error_code errorCode;
#ifdef Q_OS_WIN
    const std::filesystem::path input (QFileInfo (path).absoluteFilePath ().toStdWString ());
#else
    const std::filesystem::path input (QFileInfo (path).absoluteFilePath ().toStdString ());
#endif
    const std::filesystem::path canonical = std::filesystem::weakly_canonical (input, errorCode);
    if (errorCode || canonical.empty ())
        return false;
#ifdef Q_OS_WIN
    canonicalPath = QString::fromStdWString (canonical.native ());
#else
    canonicalPath = QString::fromStdString (canonical.native ());
#endif
    canonicalPath = QDir::cleanPath (QDir::fromNativeSeparators (canonicalPath));
    return true;
}

#ifdef Q_OS_WIN
// 以"删除+只读属性"权限打开目标并读取文件信息。拒绝重解析点（reparse point，含符号
// 链接与 junction），防止删除操作跟随链接逃出项目目录；句柄保持打开以便后续按句柄删除。
bool openDeletionHandle (const QString& path, HANDLE& handle, BY_HANDLE_FILE_INFORMATION& info,
                         QString* error)
{
    const std::wstring native = QDir::toNativeSeparators (path).toStdWString ();
    handle = CreateFileW (native.c_str (), DELETE | FILE_READ_ATTRIBUTES,
                          FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                          FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE || !GetFileInformationByHandle (handle, &info)) {
        if (handle != INVALID_HANDLE_VALUE)
            CloseHandle (handle);
        setError (error, QStringLiteral ("Could not open project cleanup target safely: %1").arg (path));
        return false;
    }
    if (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
        CloseHandle (handle);
        handle = INVALID_HANDLE_VALUE;
        setError (error, QStringLiteral ("Project cleanup target is a reparse point: %1").arg (path));
        return false;
    }
    return true;
}

// 通过已打开的句柄标记删除（FileDispositionInfo），避免"打开→删除"之间路径被替换。
bool deleteOpenHandle (HANDLE handle, const QString& path, QString* error)
{
    FILE_DISPOSITION_INFO disposition {};
    disposition.DeleteFile = TRUE;
    if (SetFileInformationByHandle (handle, FileDispositionInfo, &disposition, sizeof (disposition)))
        return true;
    setError (error, QStringLiteral ("Could not remove project cleanup target: %1").arg (path));
    return false;
}

// Windows 下安全删除一个普通文件：先打开删除句柄、校验非目录且非重解析点，再按句柄删除。
bool removeWindowsFile (const QString& filePath, QString* error)
{
    HANDLE file = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION info {};
    if (!openDeletionHandle (filePath, file, info, error))
        return false;
    if (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        CloseHandle (file);
        setError (error, QStringLiteral ("Project cleanup target is not a file: %1").arg (filePath));
        return false;
    }
    const bool removed = deleteOpenHandle (file, filePath, error);
    CloseHandle (file);
    return removed;
}

// Windows 下安全递归删除目录树：对每个子项递归调用（目录→自身、文件→removeWindowsFile），
// 全部子项清空后再按句柄删除目录本身。
bool removeWindowsDirectoryTree (const QString& directoryPath, QString* error)
{
    HANDLE directory = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION info {};
    if (!openDeletionHandle (directoryPath, directory, info, error))
        return false;
    if (!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        CloseHandle (directory);
        setError (error, QStringLiteral ("Project cleanup target is not a directory: %1").arg (directoryPath));
        return false;
    }

    const QFileInfoList entries = QDir (directoryPath).entryInfoList (
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
    for (const QFileInfo& entry : entries) {
        QString entryError;
        const bool removed = entry.isDir ()
                                 ? removeWindowsDirectoryTree (entry.absoluteFilePath (), &entryError)
                                 : removeWindowsFile (entry.absoluteFilePath (), &entryError);
        if (!removed) {
            CloseHandle (directory);
            setError (error, entryError);
            return false;
        }
    }
    const bool removed = deleteOpenHandle (directory, directoryPath, error);
    CloseHandle (directory);
    return removed;
}
#endif

}    // namespace

bool ProjectPathResolver::resolveResource (const QString& projectFilePath,
                                           const ProjectResource& resource,
                                           QString& resolvedPath,
                                           QString* error)
{
    // 空路径无论如何都是非法资源定义，直接拒绝。
    if (resource.path.trimmed ().isEmpty ()) {
        setError (error, QString::fromUtf8 ("资源“%1”的路径不能为空。").arg (resource.id));
        return false;
    }

    // project / generated 资源受边界保护，强制按“项目相对路径”解析。
    if (resource.ownership == QStringLiteral ("project") ||
        resource.ownership == QStringLiteral ("generated")) {
        return resolveProjectRelativePath (projectFilePath, resource.path, resolvedPath, error);
    }

    // external 资源允许使用绝对路径；相对路径仍按项目目录解释，避免进程当前工作
    // 目录不同导致同一项目加载出不同文件。
    const QFileInfo fileInfo (resource.path);
    if (fileInfo.isAbsolute ()) {
        resolvedPath = QDir::cleanPath (fileInfo.absoluteFilePath ());
        return true;
    }
    return resolveProjectRelativePath (projectFilePath, resource.path, resolvedPath, error);
}

bool ProjectPathResolver::resolveProjectRelativePath (const QString& projectFilePath,
                                                      const QString& relativePath,
                                                      QString& resolvedPath,
                                                      QString* error)
{
    // 项目文件本身必须是绝对路径，否则后续基于“项目目录”的边界判断没有可靠基准。
    const QFileInfo projectInfo (projectFilePath);
    if (projectFilePath.trimmed ().isEmpty () || !projectInfo.isAbsolute ()) {
        setError (error, QString::fromUtf8 ("项目文件路径必须是非空绝对路径。"));
        return false;
    }

    // project/generated 资源的路径必须保持相对，绝对路径一律拒绝，
    // 防止清单内容变成对工程外文件的隐式引用。
    if (QFileInfo (relativePath).isAbsolute ()) {
        setError (error,
                  QString::fromUtf8 ("项目自有资源不能使用绝对路径：%1。").arg (relativePath));
        return false;
    }

    // 拼接“项目目录 + 相对路径”，再做规范化，然后检查结果是否仍位于项目目录内。
    const QString projectDirectory = QDir::cleanPath (projectInfo.absolutePath ());
    const QString normalizedRelative = QDir::fromNativeSeparators (relativePath);
    const QString candidate =
        QDir::cleanPath (QDir (projectDirectory).filePath (normalizedRelative));
    if (!isInsideRoot (projectDirectory, candidate)) {
        setError (error,
                  QString::fromUtf8 ("资源路径越出项目目录：%1；项目目录为：%2。")
                      .arg (relativePath)
                      .arg (projectDirectory));
        return false;
    }

    resolvedPath = candidate;
    return true;
}

// 校验候选写入路径包含于项目根：两侧都做弱规范化后再比较相对路径，从而拒绝
// 符号链接逃逸与 ".." 越界；项目根必须是已存在的绝对目录，候选须为绝对路径。
bool ProjectPathResolver::validateContainedWritePath (const QString& projectRoot,
                                                       const QString& candidatePath,
                                                       QString* error)
{
    const QFileInfo rootInfo (projectRoot);
    const QFileInfo candidateInfo (candidatePath);
    if (!rootInfo.isAbsolute () || !rootInfo.isDir () || !candidateInfo.isAbsolute ()) {
        setError (error,
                  QStringLiteral ("The project write target requires an existing absolute root: %1")
                      .arg (projectRoot));
        return false;
    }

    QString canonicalRoot;
    QString canonicalCandidate;
    if (!weaklyCanonicalPath (rootInfo.absoluteFilePath (), canonicalRoot) ||
        !weaklyCanonicalPath (candidateInfo.absoluteFilePath (), canonicalCandidate)) {
        setError (error,
                  QStringLiteral ("Could not resolve project write target safely: %1")
                      .arg (candidatePath));
        return false;
    }

    const QString relative = QDir (canonicalRoot).relativeFilePath (canonicalCandidate);
    if (isContainedRelativePath (relative))
        return true;

    setError (error,
              QStringLiteral ("Project write target resolves outside the project root: %1")
                  .arg (candidatePath));
    return false;
}

// 安全删除项目根内的普通文件：先做包含性校验，再按平台走句柄级（Windows）或
// 普通文件检查（非 Windows）删除；拒绝符号链接，防止链接指向项目外。
bool ProjectPathResolver::removeContainedFile (const QString& projectRoot,
                                               const QString& filePath,
                                               QString* error)
{
    if (!validateContainedWritePath (projectRoot, filePath, error))
        return false;
#ifdef Q_OS_WIN
    ProjectWriteGuard parentGuard;
    if (!ProjectWriteGuard::acquire (projectRoot, filePath, parentGuard, error))
        return false;
    return removeWindowsFile (filePath, error);
#else
    const QFileInfo info (filePath);
    if (!info.isFile () || info.isSymLink ()) {
        setError (error, QStringLiteral ("Project cleanup target is not an ordinary file: %1").arg (filePath));
        return false;
    }
    if (QFile::remove (filePath))
        return true;
    setError (error, QStringLiteral ("Could not remove project cleanup target: %1").arg (filePath));
    return false;
#endif
}

// 安全删除项目根内的空目录：包含性校验 + 目录判定 + 非空检查后删除。
bool ProjectPathResolver::removeContainedEmptyDirectory (const QString& projectRoot,
                                                         const QString& directoryPath,
                                                         QString* error)
{
    if (!validateContainedWritePath (projectRoot, directoryPath, error))
        return false;
#ifdef Q_OS_WIN
    ProjectWriteGuard parentGuard;
    if (!ProjectWriteGuard::acquire (projectRoot, directoryPath, parentGuard, error))
        return false;
    HANDLE directory = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION info {};
    if (!openDeletionHandle (directoryPath, directory, info, error))
        return false;
    if (!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        CloseHandle (directory);
        setError (error, QStringLiteral ("Project cleanup target is not a directory: %1").arg (directoryPath));
        return false;
    }
    if (!QDir (directoryPath).entryList (QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden)
             .isEmpty ()) {
        CloseHandle (directory);
        setError (error, QStringLiteral ("Project cleanup directory is not empty: %1").arg (directoryPath));
        return false;
    }
    const bool removed = deleteOpenHandle (directory, directoryPath, error);
    CloseHandle (directory);
    return removed;
#else
    const QFileInfo info (directoryPath);
    if (!info.isDir () || info.isSymLink () ||
        !QDir (directoryPath).entryList (QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden).isEmpty ()) {
        setError (error, QStringLiteral ("Project cleanup directory is unavailable or not empty: %1").arg (directoryPath));
        return false;
    }
    if (QDir ().rmdir (directoryPath))
        return true;
    setError (error, QStringLiteral ("Could not remove project cleanup directory: %1").arg (directoryPath));
    return false;
#endif
}

// 安全递归删除项目根内的目录树（Windows 走句柄级 removeWindowsDirectoryTree，
// 非 Windows 校验为普通目录后 removeRecursively）。
bool ProjectPathResolver::removeContainedDirectoryTree (const QString& projectRoot,
                                                        const QString& directoryPath,
                                                        QString* error)
{
    if (!validateContainedWritePath (projectRoot, directoryPath, error))
        return false;
#ifdef Q_OS_WIN
    ProjectWriteGuard parentGuard;
    if (!ProjectWriteGuard::acquire (projectRoot, directoryPath, parentGuard, error))
        return false;
    return removeWindowsDirectoryTree (directoryPath, error);
#else
    const QFileInfo info (directoryPath);
    if (!info.isDir () || info.isSymLink ()) {
        setError (error, QStringLiteral ("Project cleanup target is not an ordinary directory: %1").arg (directoryPath));
        return false;
    }
    if (QDir (directoryPath).removeRecursively ())
        return true;
    setError (error, QStringLiteral ("Could not remove project cleanup directory tree: %1").arg (directoryPath));
    return false;
#endif
}

// 析构：关闭所有已获取的目录句柄（Windows），释放对路径组件的占用。
ProjectWriteGuard::~ProjectWriteGuard ()
{
#ifdef Q_OS_WIN
    for (void* handle : _directoryHandles)
        CloseHandle (static_cast< HANDLE > (handle));
#endif
}

// 获取写入守卫：先做包含性校验，再（Windows）沿目标父链从项目根逐级打开目录句柄——
// 缺失目录先创建再打开；拒绝重解析点，防止符号链接逃逸。全部成功后由 guard 持有句柄，
// 使后续写/删操作期间外部无法替换路径组件。
bool ProjectWriteGuard::acquire (const QString& projectRoot,
                                 const QString& targetPath,
                                 ProjectWriteGuard& guard,
                                 QString* error)
{
    if (!ProjectPathResolver::validateContainedWritePath (projectRoot, targetPath, error))
        return false;
#ifndef Q_OS_WIN
    Q_UNUSED (guard);
    return true;
#else
    const QString root = QFileInfo (projectRoot).absoluteFilePath ();
    const QString parent = QDir::cleanPath (
        QDir::fromNativeSeparators (QFileInfo (targetPath).absoluteFilePath ()) +
        QStringLiteral ("/.."));
    const QString relative = QDir (root).relativeFilePath (parent);
    QString current = root;
    const QStringList components = QDir::fromNativeSeparators (relative).split ('/', Qt::SkipEmptyParts);
    for (int index = -1; index < components.size (); ++index) {
        if (index >= 0)
            current = QDir (current).filePath (components[index]);
        const std::wstring native = QDir::toNativeSeparators (current).toStdWString ();
        HANDLE handle = CreateFileW (native.c_str (), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                     FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle == INVALID_HANDLE_VALUE && index >= 0 && GetLastError () == ERROR_FILE_NOT_FOUND) {
            CreateDirectoryW (native.c_str (), nullptr);
            handle = CreateFileW (native.c_str (), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                  FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        }
        BY_HANDLE_FILE_INFORMATION attributes {};
        if (handle == INVALID_HANDLE_VALUE ||
            !GetFileInformationByHandle (handle, &attributes) ||
            (index >= 0 && (attributes.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))) {
            if (handle != INVALID_HANDLE_VALUE)
                CloseHandle (handle);
            setError (error, QStringLiteral ("Project write directory is unavailable or a reparse point: %1").arg (current));
            return false;
        }
        guard._directoryHandles.push_back (handle);
    }
    return true;
#endif
}

}    // namespace rws
