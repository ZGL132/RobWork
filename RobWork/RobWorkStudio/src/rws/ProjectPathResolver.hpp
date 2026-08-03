#ifndef RWS_PROJECTPATHRESOLVER_HPP
#define RWS_PROJECTPATHRESOLVER_HPP

#include "ProjectManifest.hpp"

#include <QHash>
#include <QSet>
#include <QStringList>
#include <QVector>

namespace rws {

struct ProjectAnchoredInventory
{
    QSet< QString > files;
    QSet< QString > directories;
    QHash< QString, QByteArray > fileDigests;
    QHash< QString, qint64 > fileSizes;
    quint64 totalRegularBytes = 0;
};

/**
 * @brief 统一解析项目资源路径并执行项目目录边界检查。
 *
 * project/generated 资源只能使用相对于项目文件的路径；解析器拒绝绝对路径和
 * “..” 越出项目根目录的路径，避免项目文件意外引用或覆盖工程之外的文件。
 */
class ProjectPathResolver
{
  public:
    // 按资源的 ownership 分派解析：project/generated 走相对路径（强制项目内），
    // external 允许绝对路径、否则仍按项目目录解释。失败时经 error 回填原因。
    static bool resolveResource (const QString& projectFilePath,
                                 const ProjectResource& resource,
                                 QString& resolvedPath,
                                 QString* error = nullptr);

    // 把相对于项目文件的路径拼接为绝对路径，并做目录边界检查：
    // 拒绝绝对路径入参、拒绝 ".." 越出项目根目录的情况，最终结果规范化为绝对路径。
    static bool resolveProjectRelativePath (const QString& projectFilePath,
                                            const QString& relativePath,
                                            QString& resolvedPath,
                                            QString* error = nullptr);

    // 校验候选写入路径确实位于项目根之内（弱规范化后比较，防符号链接逃逸），
    // 供事务、清理与机器人导入等所有写操作在触碰磁盘前统一做包含性检查。
    static bool validateContainedWritePath (const QString& projectRoot,
                                            const QString& candidatePath,
                                            QString* error = nullptr);

    static bool isLinkOrReparsePoint (const QString& path);
    static bool removeContainedUnsafeEntry (const QString& projectRoot,
                                             const QString& entryPath,
                                             QString* error = nullptr);

    // 在项目根内安全删除一个普通文件（Windows 用句柄删除，拒绝符号链接/重解析点）。
    static bool removeContainedFile (const QString& projectRoot,
                                     const QString& filePath,
                                     QString* error = nullptr);
    // 安全删除项目根内一个空目录。
    static bool removeContainedEmptyDirectory (const QString& projectRoot,
                                               const QString& directoryPath,
                                               QString* error = nullptr);
    // 安全递归删除项目根内的目录树（Windows 句柄级删除，避免路径竞争）。
    static bool removeContainedDirectoryTree (const QString& projectRoot,
                                              const QString& directoryPath,
                                              QString* error = nullptr);
};

// 项目写入守卫：在写/删目标前沿路径逐级获取目录句柄（Windows），并把目标所在目录
// 打开为防删除句柄，防止外部进程在操作窗口内替换/删除路径组件（TOCTOU 防护）。
// 析构时统一释放全部句柄。不可拷贝。
class ProjectWriteGuard
{
  public:
    ProjectWriteGuard () = default;
    ~ProjectWriteGuard ();
    ProjectWriteGuard (const ProjectWriteGuard&) = delete;
    ProjectWriteGuard& operator= (const ProjectWriteGuard&) = delete;

    // 获取写入守卫：先做包含性校验，再（Windows）沿目标父链逐级打开目录句柄，
    // 缺失目录先创建再打开；任一环节失败即释放已获取句柄并返回 false。
    static bool acquire (const QString& projectRoot,
                         const QString& targetPath,
                         ProjectWriteGuard& guard,
                         QString* error = nullptr);
    bool createMissingProjectRoot (const QString& projectRoot,
                                   const QStringList& missingDirectories,
                                   QString* error = nullptr);
    bool removeRelativeDirectoryTree (const QString& relativePath,
                                      QString* error = nullptr);
    bool reconcileRelativeTree (const QSet< QString >& baselineFiles,
                                const QSet< QString >& baselineDirectories,
                                QString* error = nullptr);
    bool ensureRelativeDirectories (const QSet< QString >& relativeDirectories,
                                    QString* error = nullptr);
    bool restoreRelativeFileAtomically (const QString& backupPath,
                                        const QString& relativeTarget,
                                        QString* error = nullptr);
    bool captureRelativeInventory (ProjectAnchoredInventory& inventory,
                                   bool includeDigests,
                                   QString* error = nullptr) const;
    bool validateRootIdentity (QString* error = nullptr) const;
    void release ();

  private:
    QVector< void* > _directoryHandles;    // Windows 目录句柄（析构时关闭）。
#ifdef Q_OS_WIN
    int _rootDirectoryHandleIndex = -1;
#endif
#ifndef Q_OS_WIN
    int _rootDirectoryFd = -1;
    quint64 _rootDevice = 0;
    quint64 _rootInode = 0;
#endif
    QString _rootPath;
};

}    // namespace rws

#endif    // RWS_PROJECTPATHRESOLVER_HPP
