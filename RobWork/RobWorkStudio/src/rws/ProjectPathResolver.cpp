#include "ProjectPathResolver.hpp"

#include <QDir>
#include <QDirIterator>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <cstring>
#include <functional>
#ifdef Q_OS_WIN
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#include <windows.h>
#else
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
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

#ifndef Q_OS_WIN
QString posixErrorText (const QString& operation, const QString& path)
{
    return QStringLiteral ("%1 failed for '%2': %3")
        .arg (operation, path, QString::fromLocal8Bit (strerror (errno)));
}

bool splitSafeAbsolutePath (const QString& path, QStringList& components, QString* error)
{
    if (!path.startsWith (QLatin1Char ('/'))) {
        setError (error, QStringLiteral ("Project cleanup paths must be absolute: %1").arg (path));
        return false;
    }
    const QStringList raw = path.split (QLatin1Char ('/'), Qt::KeepEmptyParts);
    components.clear ();
    for (int index = 1; index < raw.size (); ++index) {
        const QString& component = raw[index];
        if (component.isEmpty () || component == QStringLiteral (".") ||
            component == QStringLiteral ("..")) {
            setError (error,
                      QStringLiteral ("Project cleanup path contains an unsafe component: %1")
                          .arg (path));
            return false;
        }
        components.push_back (component);
    }
    return true;
}

int openDirectoryComponents (const QStringList& components, QString* error,
                             const QString& path)
{
    int current = open ("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (current < 0) {
        setError (error, posixErrorText (QStringLiteral ("Opening filesystem root"), path));
        return -1;
    }
    for (const QString& component : components) {
        const QByteArray name = QFile::encodeName (component);
        const int next = openat (
            current, name.constData (), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (next < 0) {
            setError (error, posixErrorText (QStringLiteral ("Opening directory component"), path));
            close (current);
            return -1;
        }
        close (current);
        current = next;
    }
    return current;
}

bool sameIdentity (const struct stat& left, const struct stat& right)
{
    return left.st_dev == right.st_dev && left.st_ino == right.st_ino;
}

bool splitSafeRelativePath (const QString& relativePath, QStringList& components,
                            QString* error)
{
    const QString normalized = QDir::fromNativeSeparators (relativePath);
    if (normalized.isEmpty () || QDir::isAbsolutePath (normalized)) {
        setError (error, QStringLiteral ("Anchored project path must be relative: %1").arg (relativePath));
        return false;
    }
    components = normalized.split (QLatin1Char ('/'), Qt::KeepEmptyParts);
    for (const QString& component : components) {
        if (component.isEmpty () || component == QStringLiteral (".") ||
            component == QStringLiteral ("..")) {
            setError (error, QStringLiteral ("Anchored project path is unsafe: %1").arg (relativePath));
            return false;
        }
    }
    return true;
}

bool openPosixRelativeParent (int root, const QString& relativePath, int& parent,
                              QByteArray& leaf, QString* error)
{
    QStringList components;
    if (!splitSafeRelativePath (relativePath, components, error))
        return false;
    leaf = QFile::encodeName (components.takeLast ());
    parent = dup (root);
    if (parent < 0) {
        setError (error, posixErrorText (QStringLiteral ("Duplicating anchored root"), relativePath));
        return false;
    }
    for (const QString& component : components) {
        const QByteArray name = QFile::encodeName (component);
        const int next = openat (
            parent, name.constData (), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (next < 0) {
            setError (error, posixErrorText (QStringLiteral ("Opening anchored parent"), relativePath));
            close (parent);
            parent = -1;
            return false;
        }
        close (parent);
        parent = next;
    }
    return true;
}

struct PosixTarget
{
    ~PosixTarget ()
    {
        if (parent >= 0)
            close (parent);
        if (root >= 0)
            close (root);
    }

    int root = -1;
    int parent = -1;
    QByteArray leaf;
    bool entryIsRoot = false;
};

bool openPosixTarget (const QString& projectRoot, const QString& targetPath,
                      PosixTarget& target, QString* error)
{
    QStringList rootComponents;
    QStringList targetComponents;
    if (!splitSafeAbsolutePath (projectRoot, rootComponents, error) ||
        !splitSafeAbsolutePath (targetPath, targetComponents, error) ||
        rootComponents.isEmpty () || targetComponents.size () < rootComponents.size ())
        return false;
    for (int index = 0; index < rootComponents.size (); ++index) {
        if (rootComponents[index] != targetComponents[index]) {
            setError (error,
                      QStringLiteral ("Project cleanup target is outside the project root: %1")
                          .arg (targetPath));
            return false;
        }
    }

    target.entryIsRoot = targetComponents.size () == rootComponents.size ();
    if (target.entryIsRoot) {
        QStringList parentComponents = rootComponents;
        target.leaf = QFile::encodeName (parentComponents.takeLast ());
        target.parent = openDirectoryComponents (parentComponents, error, projectRoot);
        return target.parent >= 0;
    }

    target.root = openDirectoryComponents (rootComponents, error, projectRoot);
    if (target.root < 0)
        return false;
    target.parent = dup (target.root);
    if (target.parent < 0) {
        setError (error, posixErrorText (QStringLiteral ("Duplicating project root"), projectRoot));
        return false;
    }
    for (int index = rootComponents.size (); index + 1 < targetComponents.size (); ++index) {
        const QByteArray component = QFile::encodeName (targetComponents[index]);
        const int next = openat (target.parent, component.constData (),
                                 O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (next < 0) {
            setError (error,
                      posixErrorText (QStringLiteral ("Opening cleanup parent"), targetPath));
            return false;
        }
        close (target.parent);
        target.parent = next;
    }
    target.leaf = QFile::encodeName (targetComponents.constLast ());
    return true;
}

bool removePosixDirectoryContents (int directory, const QString& displayPath, QString* error)
{
    const int scanDescriptor = dup (directory);
    if (scanDescriptor < 0) {
        setError (error, posixErrorText (QStringLiteral ("Duplicating cleanup directory"), displayPath));
        return false;
    }
    DIR* stream = fdopendir (scanDescriptor);
    if (stream == nullptr) {
        close (scanDescriptor);
        setError (error, posixErrorText (QStringLiteral ("Reading cleanup directory"), displayPath));
        return false;
    }

    bool success = true;
    errno = 0;
    while (dirent* entry = readdir (stream)) {
        const QByteArray name (entry->d_name);
        if (name == "." || name == "..")
            continue;
        struct stat entryInfo {};
        if (fstatat (directory, name.constData (), &entryInfo, AT_SYMLINK_NOFOLLOW) != 0) {
            setError (error,
                      posixErrorText (QStringLiteral ("Inspecting cleanup entry"), displayPath));
            success = false;
            break;
        }
        if (S_ISDIR (entryInfo.st_mode)) {
            const int child = openat (directory, name.constData (),
                                      O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            struct stat openedInfo {};
            if (child < 0 || fstat (child, &openedInfo) != 0 ||
                !sameIdentity (entryInfo, openedInfo) ||
                !removePosixDirectoryContents (
                    child, displayPath + QLatin1Char ('/') + QFile::decodeName (name), error)) {
                if (child >= 0)
                    close (child);
                success = false;
                break;
            }
            close (child);
            if (unlinkat (directory, name.constData (), AT_REMOVEDIR) != 0) {
                setError (error,
                          posixErrorText (QStringLiteral ("Removing cleanup directory"), displayPath));
                success = false;
                break;
            }
        }
        else if (unlinkat (directory, name.constData (), 0) != 0) {
            setError (error,
                      posixErrorText (QStringLiteral ("Removing cleanup entry"), displayPath));
            success = false;
            break;
        }
        errno = 0;
    }
    if (success && errno != 0) {
        setError (error, posixErrorText (QStringLiteral ("Reading cleanup directory"), displayPath));
        success = false;
    }
    closedir (stream);
    return success;
}

bool reconcilePosixDirectory (int directory, const QString& relativePrefix,
                              const QSet< QString >& baselineFiles,
                              const QSet< QString >& baselineDirectories,
                              QString* error)
{
    const int scanDescriptor = dup (directory);
    if (scanDescriptor < 0) {
        setError (error, posixErrorText (QStringLiteral ("Duplicating anchored directory"), relativePrefix));
        return false;
    }
    DIR* stream = fdopendir (scanDescriptor);
    if (stream == nullptr) {
        close (scanDescriptor);
        setError (error, posixErrorText (QStringLiteral ("Reading anchored directory"), relativePrefix));
        return false;
    }

    bool success = true;
    errno = 0;
    while (dirent* entry = readdir (stream)) {
        const QByteArray name (entry->d_name);
        if (name == "." || name == "..")
            continue;
        const QString decoded = QFile::decodeName (name);
        const QString relative = relativePrefix.isEmpty ()
                                     ? decoded
                                     : relativePrefix + QLatin1Char ('/') + decoded;
        struct stat entryInfo {};
        if (fstatat (directory, name.constData (), &entryInfo, AT_SYMLINK_NOFOLLOW) != 0) {
            setError (error, posixErrorText (QStringLiteral ("Inspecting anchored entry"), relative));
            success = false;
            break;
        }

        const bool retainDirectory = baselineDirectories.contains (relative) &&
            S_ISDIR (entryInfo.st_mode);
        const bool retainFile = baselineFiles.contains (relative) && S_ISREG (entryInfo.st_mode);
        if (retainDirectory) {
            const int child = openat (directory, name.constData (),
                                      O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            struct stat openedInfo {};
            if (child < 0 || fstat (child, &openedInfo) != 0 ||
                !sameIdentity (entryInfo, openedInfo) ||
                !reconcilePosixDirectory (
                    child, relative, baselineFiles, baselineDirectories, error)) {
                if (child >= 0)
                    close (child);
                success = false;
                break;
            }
            close (child);
        }
        else if (!retainFile) {
            if (S_ISDIR (entryInfo.st_mode)) {
                const int child = openat (directory, name.constData (),
                                          O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
                struct stat openedInfo {};
                if (child < 0 || fstat (child, &openedInfo) != 0 ||
                    !sameIdentity (entryInfo, openedInfo) ||
                    !removePosixDirectoryContents (child, relative, error)) {
                    if (child >= 0)
                        close (child);
                    success = false;
                    break;
                }
                close (child);
                if (unlinkat (directory, name.constData (), AT_REMOVEDIR) != 0) {
                    setError (error, posixErrorText (QStringLiteral ("Removing candidate directory"), relative));
                    success = false;
                    break;
                }
            }
            else if (unlinkat (directory, name.constData (), 0) != 0) {
                setError (error, posixErrorText (QStringLiteral ("Removing candidate entry"), relative));
                success = false;
                break;
            }
        }
        errno = 0;
    }
    if (success && errno != 0) {
        setError (error, posixErrorText (QStringLiteral ("Reading anchored directory"), relativePrefix));
        success = false;
    }
    closedir (stream);
    return success;
}
#endif

#ifdef Q_OS_WIN
bool sameWindowsIdentity (const BY_HANDLE_FILE_INFORMATION& left,
                          const BY_HANDLE_FILE_INFORMATION& right)
{
    return left.dwVolumeSerialNumber == right.dwVolumeSerialNumber &&
        left.nFileIndexHigh == right.nFileIndexHigh &&
        left.nFileIndexLow == right.nFileIndexLow;
}

// 以"删除+只读属性"权限打开目标并读取文件信息。拒绝重解析点（reparse point，含符号
// 链接与 junction），防止删除操作跟随链接逃出项目目录；句柄保持打开以便后续按句柄删除。
bool openDeletionHandle (const QString& path, HANDLE& handle, BY_HANDLE_FILE_INFORMATION& info,
                         QString* error, bool allowReparsePoint = false)
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
    if (!allowReparsePoint && (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
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

// Delete the contents of an already verified ordinary directory, retaining its handle until the
// directory itself is marked for deletion. Every child is classified and deleted through one
// no-follow handle so reparse points are never traversed or reinterpreted by path.
bool removeOpenWindowsDirectoryTree (HANDLE directory, const QString& directoryPath,
                                     QString* error)
{
    {
        QDirIterator entries (directoryPath,
                              QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden |
                                  QDir::System,
                              QDirIterator::NoIteratorFlags);
        while (entries.hasNext ()) {
            const QString childPath = entries.next ();
            HANDLE child = INVALID_HANDLE_VALUE;
            BY_HANDLE_FILE_INFORMATION childInfo {};
            QString childError;
            if (!openDeletionHandle (childPath, child, childInfo, &childError, true)) {
                setError (error, childError);
                return false;
            }

            const DWORD attributes = childInfo.dwFileAttributes;
            const bool isReparsePoint = attributes & FILE_ATTRIBUTE_REPARSE_POINT;
            const bool isDirectory = attributes & FILE_ATTRIBUTE_DIRECTORY;
            const bool removed = isReparsePoint || !isDirectory
                                     ? deleteOpenHandle (child, childPath, &childError)
                                     : removeOpenWindowsDirectoryTree (
                                           child, childPath, &childError);
            CloseHandle (child);
            if (!removed) {
                setError (error, childError);
                return false;
            }
        }
    }

    return deleteOpenHandle (directory, directoryPath, error);
}

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

    const bool removed = removeOpenWindowsDirectoryTree (directory, directoryPath, error);
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

bool ProjectPathResolver::isLinkOrReparsePoint (const QString& path)
{
#ifdef Q_OS_WIN
    const std::wstring native = QDir::toNativeSeparators (path).toStdWString ();
    const DWORD attributes = GetFileAttributesW (native.c_str ());
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT);
#else
    return QFileInfo (path).isSymLink ();
#endif
}

bool ProjectPathResolver::removeContainedUnsafeEntry (const QString& projectRoot,
                                                      const QString& entryPath,
                                                      QString* error)
{
    const QString root = QDir::cleanPath (QFileInfo (projectRoot).absoluteFilePath ());
    const QString entry = QDir::cleanPath (QFileInfo (entryPath).absoluteFilePath ());
    if (!isInsideRoot (root, entry)) {
        setError (error,
                  QStringLiteral ("Unsafe project cleanup target is outside the project root: %1")
                      .arg (entryPath));
        return false;
    }

    ProjectWriteGuard parentGuard;
    const bool entryIsRoot = entry.compare (root,
#ifdef Q_OS_WIN
                                            Qt::CaseInsensitive
#else
                                            Qt::CaseSensitive
#endif
                                                ) == 0;
    if (!entryIsRoot) {
        const QString parent = QFileInfo (entry).absolutePath ();
        if (!validateContainedWritePath (root, parent, error))
            return false;
        const QString guardTarget =
            QDir (parent).filePath (QStringLiteral (".rw-unsafe-entry-guard"));
        if (!ProjectWriteGuard::acquire (root, guardTarget, parentGuard, error))
            return false;
    }

#ifdef Q_OS_WIN
    HANDLE handle = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION info {};
    if (!openDeletionHandle (entry, handle, info, error, true))
        return false;
    if (!(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) && !entryIsRoot) {
        CloseHandle (handle);
        setError (error,
                  QStringLiteral ("Unsafe project cleanup target is not a reparse point: %1")
                      .arg (entry));
        return false;
    }
    const bool removed = deleteOpenHandle (handle, entry, error);
    CloseHandle (handle);
    return removed;
#else
    PosixTarget target;
    if (!openPosixTarget (projectRoot, entryPath, target, error))
        return false;
    struct stat info {};
    if (fstatat (target.parent, target.leaf.constData (), &info, AT_SYMLINK_NOFOLLOW) != 0) {
        setError (error, posixErrorText (QStringLiteral ("Inspecting unsafe project entry"), entryPath));
        return false;
    }
    if (!target.entryIsRoot && !S_ISLNK (info.st_mode)) {
        setError (error,
                  QStringLiteral ("Unsafe project cleanup target is an ordinary entry: %1")
                      .arg (entryPath));
        return false;
    }
    if (S_ISDIR (info.st_mode)) {
        setError (error,
                  QStringLiteral ("Unsafe project cleanup target is a directory: %1")
                      .arg (entryPath));
        return false;
    }
    if (unlinkat (target.parent, target.leaf.constData (), 0) == 0)
        return true;
    setError (error,
              posixErrorText (QStringLiteral ("Removing unsafe project entry"), entryPath));
    return false;
#endif
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
    PosixTarget target;
    if (!openPosixTarget (projectRoot, filePath, target, error) || target.entryIsRoot)
        return false;
    struct stat info {};
    if (fstatat (target.parent, target.leaf.constData (), &info, AT_SYMLINK_NOFOLLOW) != 0 ||
        !S_ISREG (info.st_mode)) {
        setError (error, QStringLiteral ("Project cleanup target is not an ordinary file: %1").arg (filePath));
        return false;
    }
    if (unlinkat (target.parent, target.leaf.constData (), 0) == 0)
        return true;
    setError (error, posixErrorText (QStringLiteral ("Removing project file"), filePath));
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
    PosixTarget target;
    if (!openPosixTarget (projectRoot, directoryPath, target, error) || target.entryIsRoot)
        return false;
    struct stat info {};
    if (fstatat (target.parent, target.leaf.constData (), &info, AT_SYMLINK_NOFOLLOW) != 0 ||
        !S_ISDIR (info.st_mode)) {
        setError (error, QStringLiteral ("Project cleanup directory is unavailable or not empty: %1").arg (directoryPath));
        return false;
    }
    if (unlinkat (target.parent, target.leaf.constData (), AT_REMOVEDIR) == 0)
        return true;
    setError (error, posixErrorText (QStringLiteral ("Removing empty project directory"), directoryPath));
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
    PosixTarget target;
    if (!openPosixTarget (projectRoot, directoryPath, target, error) || target.entryIsRoot)
        return false;
    struct stat entryInfo {};
    if (fstatat (target.parent, target.leaf.constData (), &entryInfo, AT_SYMLINK_NOFOLLOW) != 0 ||
        !S_ISDIR (entryInfo.st_mode)) {
        setError (error, QStringLiteral ("Project cleanup target is not an ordinary directory: %1").arg (directoryPath));
        return false;
    }
    const int directory = openat (target.parent, target.leaf.constData (),
                                  O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    struct stat openedInfo {};
    if (directory < 0 || fstat (directory, &openedInfo) != 0 ||
        !sameIdentity (entryInfo, openedInfo)) {
        if (directory >= 0)
            close (directory);
        setError (error, QStringLiteral ("Project cleanup directory identity changed: %1").arg (directoryPath));
        return false;
    }
    const bool contentsRemoved = removePosixDirectoryContents (directory, directoryPath, error);
    close (directory);
    if (contentsRemoved &&
        unlinkat (target.parent, target.leaf.constData (), AT_REMOVEDIR) == 0)
        return true;
    if (contentsRemoved)
        setError (error, posixErrorText (QStringLiteral ("Removing project directory tree"), directoryPath));
    return false;
#endif
}

// 析构：关闭所有已获取的目录句柄（Windows），释放对路径组件的占用。
ProjectWriteGuard::~ProjectWriteGuard ()
{
    release ();
}

void ProjectWriteGuard::release ()
{
#ifdef Q_OS_WIN
    for (void* handle : _directoryHandles)
        CloseHandle (static_cast< HANDLE > (handle));
    _directoryHandles.clear ();
    _rootDirectoryHandleIndex = -1;
#else
    if (_rootDirectoryFd >= 0)
        close (_rootDirectoryFd);
    _rootDirectoryFd = -1;
    _rootDevice = 0;
    _rootInode = 0;
#endif
    _rootPath.clear ();
}

// 获取写入守卫：先做包含性校验，再（Windows）沿目标父链从项目根逐级打开目录句柄——
// 缺失目录先创建再打开；拒绝重解析点，防止符号链接逃逸。全部成功后由 guard 持有句柄，
// 使后续写/删操作期间外部无法替换路径组件。
bool ProjectWriteGuard::acquire (const QString& projectRoot,
                                 const QString& targetPath,
                                 ProjectWriteGuard& guard,
                                 QString* error)
{
    guard.release ();
    if (!ProjectPathResolver::validateContainedWritePath (projectRoot, targetPath, error))
        return false;
#ifndef Q_OS_WIN
    QStringList rootComponents;
    QStringList targetComponents;
    if (!splitSafeAbsolutePath (projectRoot, rootComponents, error) ||
        !splitSafeAbsolutePath (targetPath, targetComponents, error) ||
        rootComponents.isEmpty () || targetComponents.size () < rootComponents.size ())
        return false;
    for (int index = 0; index < rootComponents.size (); ++index) {
        if (rootComponents[index] != targetComponents[index]) {
            setError (error,
                      QStringLiteral ("Project write target is outside the project root: %1")
                          .arg (targetPath));
            return false;
        }
    }
    guard._rootDirectoryFd = openDirectoryComponents (rootComponents, error, projectRoot);
    if (guard._rootDirectoryFd < 0)
        return false;
    struct stat rootInfo {};
    if (fstat (guard._rootDirectoryFd, &rootInfo) != 0) {
        setError (error, posixErrorText (QStringLiteral ("Inspecting project root"), projectRoot));
        guard.release ();
        return false;
    }
    guard._rootDevice = static_cast< quint64 > (rootInfo.st_dev);
    guard._rootInode = static_cast< quint64 > (rootInfo.st_ino);
    guard._rootPath = projectRoot;
    return true;
#else
    const QString root = QFileInfo (projectRoot).absoluteFilePath ();
    const QString target = QFileInfo (targetPath).absoluteFilePath ();
    const QString parent = target.compare (root, Qt::CaseInsensitive) == 0
                               ? root
                               : QDir::cleanPath (QDir::fromNativeSeparators (target) +
                                                  QStringLiteral ("/.."));
    const QString relative = QDir (root).relativeFilePath (parent);
    QString current = root;
    const QStringList components = relative == QStringLiteral (".")
                                       ? QStringList {}
                                       : QDir::fromNativeSeparators (relative).split (
                                             '/', Qt::SkipEmptyParts);
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
            (attributes.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            if (handle != INVALID_HANDLE_VALUE)
                CloseHandle (handle);
            setError (error, QStringLiteral ("Project write directory is unavailable or a reparse point: %1").arg (current));
            guard.release ();
            return false;
        }
        guard._directoryHandles.push_back (handle);
    }
    guard._rootDirectoryHandleIndex = 0;
    guard._rootPath = root;
    return true;
#endif
}

bool ProjectWriteGuard::createMissingProjectRoot (
    const QString& projectRoot, const QStringList& missingDirectories, QString* error)
{
    if (_rootPath.isEmpty () || missingDirectories.isEmpty ()) {
        setError (error, QStringLiteral ("Missing project-root creation has no active ancestor anchor."));
        return false;
    }
    if (!validateRootIdentity (error))
        return false;

    const QString root = QDir::cleanPath (QFileInfo (projectRoot).absoluteFilePath ());
    QString currentPath = QDir::cleanPath (QFileInfo (_rootPath).absoluteFilePath ());
    QStringList orderedDirectories = missingDirectories;
    std::reverse (orderedDirectories.begin (), orderedDirectories.end ());
#ifdef Q_OS_WIN
    const Qt::CaseSensitivity sensitivity = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity sensitivity = Qt::CaseSensitive;
#endif
    for (QString& directory : orderedDirectories) {
        directory = QDir::cleanPath (QFileInfo (directory).absoluteFilePath ());
        if (QFileInfo (directory).absolutePath ().compare (currentPath, sensitivity) != 0) {
            setError (error,
                      QStringLiteral ("Missing project directories do not form an anchored chain: %1")
                          .arg (directory));
            return false;
        }
        currentPath = directory;
    }
    if (currentPath.compare (root, sensitivity) != 0) {
        setError (error,
                  QStringLiteral ("Missing project directory chain does not end at: %1").arg (root));
        return false;
    }

#ifdef Q_OS_WIN
    const int retainedHandleCount = _directoryHandles.size ();
    QStringList createdDirectories;
    QVector< BY_HANDLE_FILE_INFORMATION > createdIdentities;
    const auto cleanupCreatedDirectories = [&] {
        for (int index = _directoryHandles.size () - 1;
             index >= retainedHandleCount;
             --index) {
            HANDLE handle = static_cast< HANDLE > (_directoryHandles[index]);
            const int createdIndex = index - retainedHandleCount;
            const QString& directory = createdDirectories[createdIndex];
            BY_HANDLE_FILE_INFORMATION anchoredInfo {};
            BY_HANDLE_FILE_INFORMATION currentInfo {};
            const std::wstring native =
                QDir::toNativeSeparators (directory).toStdWString ();
            HANDLE current = CreateFileW (
                native.c_str (), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            const bool identityMatches =
                GetFileInformationByHandle (handle, &anchoredInfo) &&
                current != INVALID_HANDLE_VALUE &&
                GetFileInformationByHandle (current, &currentInfo) &&
                sameWindowsIdentity (anchoredInfo, createdIdentities[createdIndex]) &&
                sameWindowsIdentity (anchoredInfo, currentInfo);
            if (current != INVALID_HANDLE_VALUE)
                CloseHandle (current);
            CloseHandle (handle);
            _directoryHandles.removeAt (index);
            if (identityMatches)
                RemoveDirectoryW (native.c_str ());
        }
    };

    for (const QString& directory : orderedDirectories) {
        const std::wstring native = QDir::toNativeSeparators (directory).toStdWString ();
        if (!CreateDirectoryW (native.c_str (), nullptr)) {
            setError (error,
                      GetLastError () == ERROR_ALREADY_EXISTS
                          ? QStringLiteral ("A missing project directory appeared unexpectedly: %1")
                                .arg (directory)
                          : QStringLiteral ("Could not create the missing project directory: %1")
                                .arg (directory));
            cleanupCreatedDirectories ();
            return false;
        }

        HANDLE handle = CreateFileW (
            native.c_str (), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        BY_HANDLE_FILE_INFORMATION createdInfo {};
        if (handle == INVALID_HANDLE_VALUE ||
            !GetFileInformationByHandle (handle, &createdInfo) ||
            !(createdInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (createdInfo.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            if (handle != INVALID_HANDLE_VALUE)
                CloseHandle (handle);
            setError (error,
                      QStringLiteral ("Created project directory is unavailable or unsafe: %1")
                          .arg (directory));
            cleanupCreatedDirectories ();
            return false;
        }

        HANDLE current = CreateFileW (
            native.c_str (), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        BY_HANDLE_FILE_INFORMATION currentInfo {};
        const bool identityMatches = current != INVALID_HANDLE_VALUE &&
            GetFileInformationByHandle (current, &currentInfo) &&
            !(currentInfo.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
            sameWindowsIdentity (createdInfo, currentInfo);
        if (current != INVALID_HANDLE_VALUE)
            CloseHandle (current);
        if (!identityMatches) {
            CloseHandle (handle);
            setError (error,
                      QStringLiteral ("Created project directory identity changed: %1")
                          .arg (directory));
            cleanupCreatedDirectories ();
            return false;
        }

        _directoryHandles.push_back (handle);
        createdDirectories.push_back (directory);
        createdIdentities.push_back (createdInfo);
    }
    _rootDirectoryHandleIndex = _directoryHandles.size () - 1;
    _rootPath = root;
    return true;
#else
    const int ancestor = _rootDirectoryFd;
    QVector< int > createdHandles;
    QVector< QByteArray > createdNames;
    const auto cleanupCreatedDirectories = [&] {
        for (int index = createdHandles.size () - 1; index >= 0; --index) {
            const int parent = index == 0 ? ancestor : createdHandles[index - 1];
            unlinkat (parent, createdNames[index].constData (), AT_REMOVEDIR);
            close (createdHandles[index]);
        }
        createdHandles.clear ();
        createdNames.clear ();
    };

    int parent = ancestor;
    for (const QString& directory : orderedDirectories) {
        const QByteArray name = QFile::encodeName (QFileInfo (directory).fileName ());
        if (mkdirat (parent, name.constData (), 0777) != 0) {
            setError (error,
                      errno == EEXIST
                          ? QStringLiteral ("A missing project directory appeared unexpectedly: %1")
                                .arg (directory)
                          : posixErrorText (
                                QStringLiteral ("Creating missing project directory"), directory));
            cleanupCreatedDirectories ();
            return false;
        }

        const int child = openat (
            parent, name.constData (), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        struct stat openedInfo {};
        struct stat pathInfo {};
        const bool identityMatches = child >= 0 && fstat (child, &openedInfo) == 0 &&
            fstatat (parent, name.constData (), &pathInfo, AT_SYMLINK_NOFOLLOW) == 0 &&
            S_ISDIR (pathInfo.st_mode) && sameIdentity (openedInfo, pathInfo);
        if (!identityMatches) {
            if (child >= 0)
                close (child);
            setError (error,
                      QStringLiteral ("Created project directory identity changed: %1")
                          .arg (directory));
            cleanupCreatedDirectories ();
            return false;
        }
        createdHandles.push_back (child);
        createdNames.push_back (name);
        parent = child;
    }

    struct stat rootInfo {};
    const int currentRoot = openDirectoryComponents (
        QDir::fromNativeSeparators (root).split (QLatin1Char ('/'), Qt::SkipEmptyParts),
        error,
        root);
    struct stat currentInfo {};
    const bool rootIdentityMatches = fstat (createdHandles.constLast (), &rootInfo) == 0 &&
        currentRoot >= 0 && fstat (currentRoot, &currentInfo) == 0 &&
        sameIdentity (rootInfo, currentInfo);
    if (currentRoot >= 0)
        close (currentRoot);
    if (!rootIdentityMatches) {
        if (error != nullptr && error->isEmpty ())
            *error = QStringLiteral ("Created project root identity changed: %1").arg (root);
        cleanupCreatedDirectories ();
        return false;
    }

    close (ancestor);
    for (int index = 0; index + 1 < createdHandles.size (); ++index)
        close (createdHandles[index]);
    _rootDirectoryFd = createdHandles.constLast ();
    _rootDevice = static_cast< quint64 > (rootInfo.st_dev);
    _rootInode = static_cast< quint64 > (rootInfo.st_ino);
    _rootPath = root;
    return true;
#endif
}

bool ProjectWriteGuard::removeRelativeDirectoryTree (const QString& relativePath,
                                                     QString* error)
{
    if (_rootPath.isEmpty () || !isContainedRelativePath (relativePath) ||
        QDir::cleanPath (relativePath) == QStringLiteral (".")) {
        setError (error, QStringLiteral ("Anchored cleanup path is unsafe: %1").arg (relativePath));
        return false;
    }
#ifdef Q_OS_WIN
    if (!validateRootIdentity (error))
        return false;
    return ProjectPathResolver::removeContainedDirectoryTree (
        _rootPath, QDir (_rootPath).filePath (relativePath), error);
#else
    int parent = -1;
    QByteArray leaf;
    if (!openPosixRelativeParent (_rootDirectoryFd, relativePath, parent, leaf, error))
        return false;
    struct stat entryInfo {};
    const int directory = openat (parent, leaf.constData (),
                                  O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    struct stat openedInfo {};
    const bool opened = fstatat (parent, leaf.constData (), &entryInfo, AT_SYMLINK_NOFOLLOW) == 0 &&
        S_ISDIR (entryInfo.st_mode) && directory >= 0 &&
        fstat (directory, &openedInfo) == 0 && sameIdentity (entryInfo, openedInfo);
    if (!opened) {
        if (directory >= 0)
            close (directory);
        close (parent);
        setError (error, QStringLiteral ("Anchored cleanup target is not an ordinary directory: %1")
                             .arg (relativePath));
        return false;
    }
    const bool contentsRemoved = removePosixDirectoryContents (directory, relativePath, error);
    close (directory);
    const bool removed = contentsRemoved &&
        unlinkat (parent, leaf.constData (), AT_REMOVEDIR) == 0;
    if (contentsRemoved && !removed)
        setError (error, posixErrorText (QStringLiteral ("Removing anchored directory"), relativePath));
    close (parent);
    return removed;
#endif
}

bool ProjectWriteGuard::reconcileRelativeTree (
    const QSet< QString >& baselineFiles,
    const QSet< QString >& baselineDirectories,
    QString* error)
{
#ifdef Q_OS_WIN
    Q_UNUSED (baselineFiles);
    Q_UNUSED (baselineDirectories);
    setError (error, QStringLiteral ("Anchored tree reconciliation uses the Windows guarded rollback path."));
    return false;
#else
    if (_rootDirectoryFd < 0) {
        setError (error, QStringLiteral ("Project transaction anchor is not active."));
        return false;
    }
    return reconcilePosixDirectory (
        _rootDirectoryFd, QString (), baselineFiles, baselineDirectories, error);
#endif
}

bool ProjectWriteGuard::ensureRelativeDirectories (
    const QSet< QString >& relativeDirectories, QString* error)
{
    QStringList ordered = relativeDirectories.values ();
    std::sort (ordered.begin (), ordered.end (), [] (const QString& left, const QString& right) {
        return left.count (QLatin1Char ('/')) < right.count (QLatin1Char ('/'));
    });
#ifdef Q_OS_WIN
    if (!validateRootIdentity (error))
        return false;
    for (const QString& relative : ordered) {
        if (!isContainedRelativePath (relative) || relative == QStringLiteral (".")) {
            setError (error, QStringLiteral ("Anchored directory path is unsafe: %1").arg (relative));
            return false;
        }
        const QString path = QDir (_rootPath).filePath (relative);
        if (!QFileInfo::exists (path) && !QDir ().mkdir (path)) {
            setError (error, QStringLiteral ("Could not restore project directory: %1").arg (path));
            return false;
        }
        ProjectWriteGuard directoryGuard;
        if (!ProjectWriteGuard::acquire (_rootPath, path, directoryGuard, error))
            return false;
    }
    return true;
#else
    for (const QString& relative : ordered) {
        QStringList components;
        if (!splitSafeRelativePath (relative, components, error))
            return false;
        int current = dup (_rootDirectoryFd);
        if (current < 0) {
            setError (error, posixErrorText (QStringLiteral ("Duplicating anchored root"), relative));
            return false;
        }
        bool success = true;
        for (const QString& component : components) {
            const QByteArray name = QFile::encodeName (component);
            struct stat pathInfo {};
            if (fstatat (current, name.constData (), &pathInfo, AT_SYMLINK_NOFOLLOW) != 0) {
                if (errno != ENOENT || mkdirat (current, name.constData (), 0777) != 0) {
                    setError (error, posixErrorText (QStringLiteral ("Restoring project directory"), relative));
                    success = false;
                    break;
                }
                if (fstatat (current, name.constData (), &pathInfo, AT_SYMLINK_NOFOLLOW) != 0) {
                    setError (error, posixErrorText (QStringLiteral ("Inspecting restored directory"), relative));
                    success = false;
                    break;
                }
            }
            const int next = openat (current, name.constData (),
                                     O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            struct stat openedInfo {};
            if (!S_ISDIR (pathInfo.st_mode) || next < 0 ||
                fstat (next, &openedInfo) != 0 || !sameIdentity (pathInfo, openedInfo)) {
                if (next >= 0)
                    close (next);
                setError (error, QStringLiteral ("Restored project directory is unsafe: %1").arg (relative));
                success = false;
                break;
            }
            close (current);
            current = next;
        }
        close (current);
        if (!success)
            return false;
    }
    return true;
#endif
}

bool ProjectWriteGuard::restoreRelativeFileAtomically (
    const QString& backupPath, const QString& relativeTarget, QString* error)
{
    if (!isContainedRelativePath (relativeTarget) ||
        QDir::cleanPath (relativeTarget) == QStringLiteral (".")) {
        setError (error, QStringLiteral ("Anchored restore target is unsafe: %1").arg (relativeTarget));
        return false;
    }
#ifdef Q_OS_WIN
    if (!validateRootIdentity (error))
        return false;
    const QString target = QDir (_rootPath).filePath (relativeTarget);
    ProjectWriteGuard parentGuard;
    if (!ProjectWriteGuard::acquire (_rootPath, target, parentGuard, error))
        return false;
    QFile source (backupPath);
    QSaveFile destination (target);
    if (!source.open (QIODevice::ReadOnly) || !destination.open (QIODevice::WriteOnly)) {
        setError (error, QStringLiteral ("Could not open project baseline restore: %1").arg (target));
        return false;
    }
    while (!source.atEnd ()) {
        const QByteArray chunk = source.read (1024 * 1024);
        if ((chunk.isEmpty () && source.error () != QFileDevice::NoError) ||
            destination.write (chunk) != chunk.size ()) {
            destination.cancelWriting ();
            setError (error, QStringLiteral ("Could not write project baseline restore: %1").arg (target));
            return false;
        }
    }
    if (!destination.commit ()) {
        setError (error, QStringLiteral ("Could not commit project baseline restore: %1").arg (target));
        return false;
    }
    return true;
#else
    int parent = -1;
    QByteArray leaf;
    if (!openPosixRelativeParent (
            _rootDirectoryFd, relativeTarget, parent, leaf, error))
        return false;
    const QByteArray encodedBackup = QFile::encodeName (backupPath);
    const int source = open (encodedBackup.constData (), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    struct stat sourceInfo {};
    if (source < 0 || fstat (source, &sourceInfo) != 0 || !S_ISREG (sourceInfo.st_mode)) {
        if (source >= 0)
            close (source);
        close (parent);
        setError (error, posixErrorText (QStringLiteral ("Opening baseline backup"), backupPath));
        return false;
    }

    static std::atomic< quint64 > temporaryCounter {0};
    QByteArray temporaryName;
    int temporary = -1;
    for (int attempt = 0; attempt < 100 && temporary < 0; ++attempt) {
        temporaryName = QByteArray (".rw-restore-") + QByteArray::number (getpid ()) + '-' +
            QByteArray::number (temporaryCounter.fetch_add (1));
        temporary = openat (parent, temporaryName.constData (),
                            O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (temporary < 0 && errno != EEXIST)
            break;
    }
    if (temporary < 0) {
        close (source);
        close (parent);
        setError (error, posixErrorText (QStringLiteral ("Creating atomic restore file"), relativeTarget));
        return false;
    }

    bool copied = true;
    char buffer[1024 * 1024];
    while (copied) {
        const ssize_t count = read (source, buffer, sizeof (buffer));
        if (count == 0)
            break;
        if (count < 0) {
            if (errno == EINTR)
                continue;
            copied = false;
            break;
        }
        ssize_t offset = 0;
        while (offset < count) {
            const ssize_t written = write (temporary, buffer + offset, count - offset);
            if (written < 0 && errno == EINTR)
                continue;
            if (written <= 0) {
                copied = false;
                break;
            }
            offset += written;
        }
    }
    copied = copied && fsync (temporary) == 0;
    close (source);
    if (close (temporary) != 0)
        copied = false;
    if (!copied || renameat (parent, temporaryName.constData (), parent, leaf.constData ()) != 0) {
        const int failure = errno;
        unlinkat (parent, temporaryName.constData (), 0);
        close (parent);
        errno = failure;
        setError (error, posixErrorText (QStringLiteral ("Committing atomic baseline restore"), relativeTarget));
        return false;
    }
    const bool synced = fsync (parent) == 0;
    close (parent);
    if (!synced) {
        setError (error, posixErrorText (QStringLiteral ("Syncing baseline restore directory"), relativeTarget));
        return false;
    }
    return true;
#endif
}

bool ProjectWriteGuard::captureRelativeInventory (
    ProjectAnchoredInventory& inventory, bool includeDigests, QString* error) const
{
    inventory = ProjectAnchoredInventory {};
#ifdef Q_OS_WIN
    if (!validateRootIdentity (error))
        return false;
    std::function< bool (const QString&, const QString&) > visit;
    visit = [&] (const QString& directory, const QString& prefix) {
        QDirIterator entries (directory,
                              QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden |
                                  QDir::System);
        while (entries.hasNext ()) {
            const QFileInfo info (entries.next ());
            const QString relative = prefix.isEmpty ()
                                         ? info.fileName ()
                                         : prefix + QLatin1Char ('/') + info.fileName ();
            if (ProjectPathResolver::isLinkOrReparsePoint (info.absoluteFilePath ())) {
                setError (error, QStringLiteral ("Anchored inventory contains an unsafe entry: %1").arg (relative));
                return false;
            }
            if (info.isDir ()) {
                inventory.directories.insert (relative);
                if (!visit (info.absoluteFilePath (), relative))
                    return false;
            }
            else if (info.isFile ()) {
                inventory.files.insert (relative);
                inventory.fileSizes.insert (relative, info.size ());
                inventory.totalRegularBytes += static_cast< quint64 > (info.size ());
                if (includeDigests) {
                    QFile file (info.absoluteFilePath ());
                    if (!file.open (QIODevice::ReadOnly))
                        return false;
                    QCryptographicHash hash (QCryptographicHash::Sha256);
                    hash.addData (&file);
                    inventory.fileDigests.insert (relative, hash.result ());
                }
            }
            else {
                setError (error, QStringLiteral ("Anchored inventory contains a non-regular entry: %1").arg (relative));
                return false;
            }
        }
        return true;
    };
    return visit (_rootPath, QString ());
#else
    if (_rootDirectoryFd < 0) {
        setError (error, QStringLiteral ("Project transaction anchor is not active."));
        return false;
    }
    std::function< bool (int, const QString&) > visit;
    visit = [&] (int directory, const QString& prefix) {
        const int scanDescriptor = dup (directory);
        DIR* stream = scanDescriptor >= 0 ? fdopendir (scanDescriptor) : nullptr;
        if (stream == nullptr) {
            if (scanDescriptor >= 0)
                close (scanDescriptor);
            setError (error, posixErrorText (QStringLiteral ("Reading anchored inventory"), prefix));
            return false;
        }
        bool success = true;
        errno = 0;
        while (dirent* entry = readdir (stream)) {
            const QByteArray name (entry->d_name);
            if (name == "." || name == "..")
                continue;
            const QString decoded = QFile::decodeName (name);
            const QString relative = prefix.isEmpty ()
                                         ? decoded
                                         : prefix + QLatin1Char ('/') + decoded;
            struct stat pathInfo {};
            if (fstatat (directory, name.constData (), &pathInfo, AT_SYMLINK_NOFOLLOW) != 0) {
                setError (error, posixErrorText (QStringLiteral ("Inspecting anchored inventory"), relative));
                success = false;
                break;
            }
            if (S_ISDIR (pathInfo.st_mode)) {
                const int child = openat (directory, name.constData (),
                                          O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
                struct stat openedInfo {};
                if (child < 0 || fstat (child, &openedInfo) != 0 ||
                    !sameIdentity (pathInfo, openedInfo)) {
                    if (child >= 0)
                        close (child);
                    setError (error, QStringLiteral ("Anchored inventory directory changed: %1").arg (relative));
                    success = false;
                    break;
                }
                inventory.directories.insert (relative);
                success = visit (child, relative);
                close (child);
                if (!success)
                    break;
            }
            else if (S_ISREG (pathInfo.st_mode)) {
                const int file = openat (directory, name.constData (),
                                         O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
                struct stat openedInfo {};
                if (file < 0 || fstat (file, &openedInfo) != 0 ||
                    !sameIdentity (pathInfo, openedInfo)) {
                    if (file >= 0)
                        close (file);
                    setError (error, QStringLiteral ("Anchored inventory file changed: %1").arg (relative));
                    success = false;
                    break;
                }
                inventory.files.insert (relative);
                inventory.fileSizes.insert (relative, static_cast< qint64 > (openedInfo.st_size));
                inventory.totalRegularBytes += static_cast< quint64 > (openedInfo.st_size);
                if (includeDigests) {
                    QCryptographicHash hash (QCryptographicHash::Sha256);
                    QByteArray buffer (1024 * 1024, Qt::Uninitialized);
                    while (true) {
                        const ssize_t count = read (file, buffer.data (), buffer.size ());
                        if (count == 0)
                            break;
                        if (count < 0 && errno == EINTR)
                            continue;
                        if (count < 0) {
                            setError (error, posixErrorText (QStringLiteral ("Hashing anchored file"), relative));
                            success = false;
                            break;
                        }
                        hash.addData (buffer.constData (), count);
                    }
                    if (success)
                        inventory.fileDigests.insert (relative, hash.result ());
                }
                close (file);
                if (!success)
                    break;
            }
            else {
                setError (error, QStringLiteral ("Anchored inventory contains an unsafe entry: %1").arg (relative));
                success = false;
                break;
            }
            errno = 0;
        }
        if (success && errno != 0) {
            setError (error, posixErrorText (QStringLiteral ("Reading anchored inventory"), prefix));
            success = false;
        }
        closedir (stream);
        return success;
    };
    return visit (_rootDirectoryFd, QString ());
#endif
}

bool ProjectWriteGuard::validateRootIdentity (QString* error) const
{
    if (_rootPath.isEmpty ()) {
        setError (error, QStringLiteral ("Project transaction anchor is not active."));
        return false;
    }
#ifdef Q_OS_WIN
    if (_rootDirectoryHandleIndex < 0 ||
        _rootDirectoryHandleIndex >= _directoryHandles.size ()) {
        setError (error, QStringLiteral ("Project transaction anchor has no root handle."));
        return false;
    }
    BY_HANDLE_FILE_INFORMATION anchored {};
    if (!GetFileInformationByHandle (
            static_cast< HANDLE > (_directoryHandles[_rootDirectoryHandleIndex]), &anchored)) {
        setError (error, QStringLiteral ("Could not inspect the anchored project root."));
        return false;
    }
    const std::wstring native = QDir::toNativeSeparators (_rootPath).toStdWString ();
    HANDLE current = CreateFileW (native.c_str (), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                  FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    BY_HANDLE_FILE_INFORMATION currentInfo {};
    const bool valid = current != INVALID_HANDLE_VALUE &&
        GetFileInformationByHandle (current, &currentInfo) &&
        !(currentInfo.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
        sameWindowsIdentity (anchored, currentInfo);
    if (current != INVALID_HANDLE_VALUE)
        CloseHandle (current);
#else
    QStringList rootComponents;
    if (!splitSafeAbsolutePath (_rootPath, rootComponents, error))
        return false;
    const int current = openDirectoryComponents (rootComponents, error, _rootPath);
    struct stat currentInfo {};
    const bool valid = current >= 0 && fstat (current, &currentInfo) == 0 &&
        _rootDevice == static_cast< quint64 > (currentInfo.st_dev) &&
        _rootInode == static_cast< quint64 > (currentInfo.st_ino);
    if (current >= 0)
        close (current);
#endif
    if (!valid) {
        setError (error,
                  QStringLiteral ("The project root identity changed during the transaction: %1")
                      .arg (_rootPath));
        return false;
    }
    return true;
}

bool ProjectPathResolver::validateRobWorkCompatiblePath (const QString& path,
                                                          QString* error)
{
#ifdef Q_OS_WIN
    for (const QChar character : path) {
        if (character.unicode () > 0x7f) {
            setError (error,
                      QStringLiteral ("RobWork's Windows XML loader requires an ASCII-only "
                                      "project and WorkCell path. Choose a location and project "
                                      "name containing only English letters, digits, and symbols."));
            return false;
        }
    }
#else
    Q_UNUSED (path);
#endif
    if (error != nullptr)
        error->clear ();
    return true;
}

}    // namespace rws
