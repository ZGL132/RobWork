#include "ProjectPathResolver.hpp"

#include <QDir>
#include <QFileInfo>

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

}    // namespace rws
