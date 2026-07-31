#include "ProjectManager.hpp"

#include "ProjectManifestJson.hpp"
#include "ProjectPathResolver.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryDir>
#include <QUuid>

namespace rws {
namespace {

// 统一错误回填工具：仅当调用方提供了 error 指针时才写入错误描述。
void setError (QString* error, const QString& message)
{
    if (error != nullptr)
        *error = message;
}

// 生成当前 UTC 时间的 ISO-8601 字符串，用于清单的 createdAt / modifiedAt 元数据。
QString nowUtc ()
{
    return QDateTime::currentDateTimeUtc ().toString (Qt::ISODate);
}

// 在不覆盖既有目标文件的前提下执行原子复制。QFile::copy 直接写入目标路径，一旦进程或
// 磁盘操作中断可能遗留半截资源；这里先用 QSaveFile 写临时文件，commit 成功后才让目标
// 对后续清单更新可见，保证导入和克隆的“先文件、后清单”顺序具有可恢复性。
bool copyFileAtomically (const QString& sourcePath, const QString& targetPath, QString* error)
{
    const QFileInfo sourceInfo (sourcePath);
    if (!sourceInfo.exists () || !sourceInfo.isFile ()) {
        setError (error,
                  QString::fromUtf8 ("源资源文件不存在或不是普通文件：%1。").arg (sourcePath));
        return false;
    }
    if (QFileInfo::exists (targetPath)) {
        setError (error,
                  QString::fromUtf8 ("目标资源文件已存在，拒绝覆盖：%1。").arg (targetPath));
        return false;
    }

    const QFileInfo targetInfo (targetPath);
    if (!QDir ().mkpath (targetInfo.absolutePath ())) {
        setError (error,
                  QString::fromUtf8 ("无法创建资源目标目录：%1。").arg (targetInfo.absolutePath ()));
        return false;
    }

    QFile source (sourceInfo.absoluteFilePath ());
    if (!source.open (QIODevice::ReadOnly)) {
        setError (error,
                  QString::fromUtf8 ("无法读取源资源文件：%1。").arg (source.errorString ()));
        return false;
    }

    QSaveFile target (targetPath);
    if (!target.open (QIODevice::WriteOnly)) {
        setError (error,
                  QString::fromUtf8 ("无法写入资源临时文件：%1。").arg (target.errorString ()));
        return false;
    }

    // 分块复制可避免一次性把大型 WorkCell、网格或插件 JSON 全部读入内存。
    while (!source.atEnd ()) {
        const QByteArray chunk = source.read (64 * 1024);
        if (chunk.isEmpty () && source.error () != QFile::NoError) {
            setError (error,
                      QString::fromUtf8 ("读取源资源文件失败：%1。").arg (source.errorString ()));
            return false;
        }
        if (!chunk.isEmpty () && target.write (chunk) != chunk.size ()) {
            setError (error,
                      QString::fromUtf8 ("写入资源临时文件失败：%1。").arg (target.errorString ()));
            return false;
        }
    }
    if (!target.commit ()) {
        setError (error,
                  QString::fromUtf8 ("资源文件原子提交失败：%1。").arg (target.errorString ()));
        return false;
    }
    return true;
}

}    // namespace

bool ProjectManager::createProject (const QString& projectFilePath,
                                    const ProjectManifest& manifest,
                                    QString* error)
{
    // 在调用方传入的清单副本上补全自动生成的元数据，避免把写入的内容又改回传入对象。
    ProjectManifest candidate = manifest;
    if (candidate.project.id.trimmed ().isEmpty ())
        candidate.project.id = QUuid::createUuid ().toString (QUuid::WithoutBraces);
    if (candidate.project.createdAt.isEmpty ())
        candidate.project.createdAt = nowUtc ();
    candidate.project.modifiedAt = nowUtc ();
    if (candidate.project.application.isEmpty ())
        candidate.project.application = QStringLiteral ("RobWorkStudio");

    // 先做结构校验，再做路径校验；两者都通过才写盘。
    if (!ProjectManifestJson::validate (candidate, error))
        return false;
    if (projectFilePath.trimmed ().isEmpty ()) {
        setError (error, QString::fromUtf8 ("项目文件路径不能为空。"));
        return false;
    }

    const QFileInfo projectInfo (projectFilePath);
    const QString absoluteProjectFile = projectInfo.absoluteFilePath ();
    // 目标目录可能尚未创建（例如用户在保存对话框中输入了新目录名），这里主动补建。
    if (!QDir ().mkpath (projectInfo.absolutePath ())) {
        setError (error,
                  QString::fromUtf8 ("无法创建项目目录：%1。").arg (projectInfo.absolutePath ()));
        return false;
    }
    if (!writeManifest (absoluteProjectFile, candidate, error))
        return false;

    // 全部成功后才接管项目上下文，并复位脏标记。
    _projectFilePath = QDir::cleanPath (absoluteProjectFile);
    _manifest = candidate;
    _dirty = false;
    return true;
}

bool ProjectManager::createProjectFromWorkCell (const QString& projectFilePath,
                                                const QString& sourceWorkCellPath,
                                                QString* error)
{
    // 先校验源文件和项目清单路径，避免复制已经完成后才发现无法创建项目文件。这里刻意
    // 拒绝覆盖同名 .rwproj，防止“从 WorkCell 创建”误把已有项目的清单替换掉。
    const QFileInfo sourceInfo (sourceWorkCellPath);
    if (!sourceInfo.exists () || !sourceInfo.isFile ()) {
        setError (error,
                  QString::fromUtf8 ("WorkCell 源文件不存在或不是普通文件：%1。").arg (
                      sourceWorkCellPath));
        return false;
    }
    if (projectFilePath.trimmed ().isEmpty ()) {
        setError (error, QString::fromUtf8 ("项目文件路径不能为空。"));
        return false;
    }

    const QString absoluteProjectFile = QFileInfo (projectFilePath).absoluteFilePath ();
    if (QFileInfo::exists (absoluteProjectFile)) {
        setError (error,
                  QString::fromUtf8 ("目标项目文件已存在，拒绝覆盖：%1。").arg (
                      absoluteProjectFile));
        return false;
    }

    ProjectManifest manifest;
    manifest.project.name = QFileInfo (absoluteProjectFile).completeBaseName ();
    manifest.project.description = QString::fromUtf8 ("从现有 WorkCell 迁移创建的项目");
    manifest.settings.insert (QStringLiteral ("pathPolicy"), QStringLiteral ("project-relative"));

    ProjectResource workCell;
    workCell.id = QStringLiteral ("scene.main");
    workCell.kind = QStringLiteral ("robwork.workcell");
    workCell.path = QStringLiteral ("scenes/main.wc.xml");
    workCell.ownership = QStringLiteral ("project");
    workCell.required = true;
    manifest.resources.push_back (workCell);
    manifest.entryPoints.insert (QStringLiteral ("mainWorkCell"), workCell.id);

    QString targetWorkCell;
    if (!ProjectPathResolver::resolveResource (
            absoluteProjectFile, workCell, targetWorkCell, error))
        return false;
    if (!copyFileAtomically (sourceInfo.absoluteFilePath (), targetWorkCell, error))
        return false;

    // createProject 仅在写入清单成功后才接管当前上下文。若清单写入失败，刚复制的资源不再
    // 有任何清单引用，因此主动删除它，避免在目标目录留下误导性的半成品项目。
    if (!createProject (absoluteProjectFile, manifest, error)) {
        QFile::remove (targetWorkCell);
        return false;
    }
    return true;
}

bool ProjectManager::openProject (const QString& projectFilePath, QString* error)
{
    // 先确认文件真实存在且是普通文件（而非目录等），避免后续读取报出难懂的错误。
    const QFileInfo projectInfo (projectFilePath);
    if (!projectInfo.exists () || !projectInfo.isFile ()) {
        setError (error,
                  QString::fromUtf8 ("项目文件不存在：%1。").arg (projectFilePath));
        return false;
    }

    QFile file (projectInfo.absoluteFilePath ());
    if (!file.open (QIODevice::ReadOnly)) {
        setError (error,
                  QString::fromUtf8 ("无法读取项目文件：%1。").arg (file.errorString ()));
        return false;
    }

    ProjectManifest candidate;
    if (!ProjectManifestJson::fromJson (file.readAll (), candidate, error))
        return false;

    // JSON 结构校验不能替代路径安全校验。打开项目时预先解析每一个 project/generated
    // 资源，尽早报告越界路径，避免后续插件加载阶段才以不明确的文件错误失败。
    for (const ProjectResource& resource : candidate.resources) {
        QString resolvedPath;
        if (!ProjectPathResolver::resolveResource (projectInfo.absoluteFilePath (),
                                                   resource,
                                                   resolvedPath,
                                                   error))
            return false;

        // required 资源属于项目能够正常工作的最低集合。只有所有必需资源都存在时
        // 才替换当前项目上下文，从而保证打开失败不会破坏用户原来已打开的项目。
        if (resource.required && !QFileInfo::exists (resolvedPath)) {
            setError (error,
                      QString::fromUtf8 ("项目必需资源不存在：%1（资源 ID：%2）。")
                          .arg (resolvedPath)
                          .arg (resource.id));
            return false;
        }
    }

    _projectFilePath = QDir::cleanPath (projectInfo.absoluteFilePath ());
    _manifest = candidate;
    _dirty = false;
    return true;
}

bool ProjectManager::saveProject (QString* error)
{
    if (!hasProject ()) {
        setError (error, QString::fromUtf8 ("当前没有打开的项目。"));
        return false;
    }

    // 在副本上更新修改时间并重新校验，校验或写盘失败时内存清单保持原样。
    ProjectManifest candidate = _manifest;
    candidate.project.modifiedAt = nowUtc ();
    if (!ProjectManifestJson::validate (candidate, error))
        return false;
    if (!writeManifest (_projectFilePath, candidate, error))
        return false;

    // 写盘成功后，用写入过的副本替换内存清单，并清除脏标记。
    _manifest = candidate;
    _dirty = false;
    return true;
}

bool ProjectManager::importResource (const QString& sourcePath,
                                     const ProjectResource& resource,
                                     QString* error)
{
    if (!hasProject ()) {
        setError (error, QString::fromUtf8 ("当前没有打开的项目。"));
        return false;
    }

    // 历史文件导入会被复制进项目，因此只接受 project 归属。generated 代表软件后续生成
    // 的产物，external 则不复制且不具备可搬迁性，二者都不应混入本导入入口。
    ProjectResource imported = resource;
    if (imported.ownership.trimmed ().isEmpty ())
        imported.ownership = QStringLiteral ("project");
    if (imported.ownership != QStringLiteral ("project")) {
        setError (error,
                  QString::fromUtf8 ("导入资源的 ownership 必须为 project。"));
        return false;
    }

    // 先把候选资源加入副本并运行完整清单校验。这样重复 ID、空 kind、非法依赖等错误均在
    // 任何磁盘写入前返回，失败时 _manifest 和 _dirty 保持原值。
    ProjectManifest candidate = _manifest;
    candidate.resources.push_back (imported);
    if (!ProjectManifestJson::validate (candidate, error))
        return false;

    QString targetPath;
    if (!ProjectPathResolver::resolveResource (_projectFilePath, imported, targetPath, error))
        return false;
    if (!copyFileAtomically (sourcePath, targetPath, error))
        return false;

    // 文件原子提交成功后，内存清单才开始引用它。清单由调用方下一次 saveProject 统一写入，
    // 以便与 Provider 的多资源保存事务保持相同的提交顺序。
    _manifest = candidate;
    _dirty = true;
    return true;
}

bool ProjectManager::addGeneratedResource (const ProjectResource& resource, QString* error)
{
    if (!hasProject ()) {
        setError (error, QString::fromUtf8 ("当前没有打开的项目。"));
        return false;
    }

    // 生成资源必须先在候选清单上完成结构和路径校验，再替换内存清单。目标文件此时可以
    // 尚不存在，因为它会由 Provider 的后续暂存保存创建；这里绝不能把空文件提前写入磁盘。
    ProjectManifest candidate = _manifest;
    candidate.resources.push_back (resource);
    if (!ProjectManifestJson::validate (candidate, error))
        return false;
    QString resolvedPath;
    if (!ProjectPathResolver::resolveResource (_projectFilePath, resource, resolvedPath, error))
        return false;

    _manifest = candidate;
    _dirty = true;
    return true;
}

bool ProjectManager::cloneProject (const QString& targetProjectFilePath, QString* error)
{
    if (!hasProject ()) {
        setError (error, QString::fromUtf8 ("当前没有打开的项目。"));
        return false;
    }
    if (targetProjectFilePath.trimmed ().isEmpty ()) {
        setError (error, QString::fromUtf8 ("目标项目文件路径不能为空。"));
        return false;
    }

    const QString targetProjectFile = QFileInfo (targetProjectFilePath).absoluteFilePath ();
    const QString targetDirectory = QFileInfo (targetProjectFile).absolutePath ();
    if (QDir::cleanPath (targetProjectFile) == QDir::cleanPath (_projectFilePath)) {
        setError (error, QString::fromUtf8 ("不能将项目克隆到当前项目文件。"));
        return false;
    }

    // 克隆以“一个项目目录对应一个 .rwproj”为边界。要求目标目录完全不存在，能保证临时
    // 目录最终 rename 时是原子切换，也避免错误覆盖用户在同一目录下创建的其他文件。
    if (QFileInfo::exists (targetDirectory)) {
        setError (error,
                  QString::fromUtf8 ("目标项目目录必须不存在，拒绝写入已有目录：%1。").arg (
                      targetDirectory));
        return false;
    }
    const QString parentDirectory = QFileInfo (targetDirectory).absolutePath ();
    if (!QDir ().mkpath (parentDirectory)) {
        setError (error,
                  QString::fromUtf8 ("无法创建目标项目的父目录：%1。").arg (parentDirectory));
        return false;
    }

    ProjectManifest candidate = _manifest;
    candidate.project.id = QUuid::createUuid ().toString (QUuid::WithoutBraces);
    candidate.project.name = QFileInfo (targetProjectFile).completeBaseName ();
    candidate.project.createdAt = nowUtc ();
    candidate.project.modifiedAt = candidate.project.createdAt;
    if (!ProjectManifestJson::validate (candidate, error))
        return false;

    // 所有资源先写入同一父目录下的临时项目目录。临时目录与最终目录处于同一卷时，最后的
    // rename 不需要逐个移动文件；中途任一复制失败时 QTemporaryDir 自动清理整个暂存目录。
    QTemporaryDir stagingDirectory (
        QDir (parentDirectory).filePath (QStringLiteral (".rwproj-clone-XXXXXX")));
    if (!stagingDirectory.isValid ()) {
        setError (error, QString::fromUtf8 ("无法创建项目克隆临时目录。"));
        return false;
    }
    const QString stagedProjectFile =
        QDir (stagingDirectory.path ()).filePath (QFileInfo (targetProjectFile).fileName ());
    QSet< QString > copiedTargetPaths;
    for (const ProjectResource& resource : candidate.resources) {
        if (resource.ownership == QStringLiteral ("external"))
            continue;

        QString sourcePath;
        if (!ProjectPathResolver::resolveResource (_projectFilePath, resource, sourcePath, error))
            return false;
        if (!QFileInfo (sourcePath).exists () || !QFileInfo (sourcePath).isFile ()) {
            setError (error,
                      QString::fromUtf8 ("无法克隆缺失或非普通文件的项目资源：%1（资源 ID：%2）。")
                          .arg (sourcePath)
                          .arg (resource.id));
            return false;
        }

        QString targetPath;
        if (!ProjectPathResolver::resolveResource (
                stagedProjectFile, resource, targetPath, error))
            return false;
        // 多个资源允许共享同一项目内文件；路径相同意味着基于同一相对路径解析，复制一次即可。
        if (copiedTargetPaths.contains (targetPath))
            continue;
        if (!copyFileAtomically (sourcePath, targetPath, error))
            return false;
        copiedTargetPaths.insert (targetPath);
    }

    if (!writeManifest (stagedProjectFile, candidate, error))
        return false;
    if (!QDir ().rename (stagingDirectory.path (), targetDirectory)) {
        setError (error,
                  QString::fromUtf8 ("项目克隆目录提交失败：%1。").arg (targetDirectory));
        return false;
    }
    // rename 完成后临时目录原路径已不存在，关闭自动清理避免析构阶段对已提交项目执行删除。
    stagingDirectory.setAutoRemove (false);

    // 最终目录及清单都已完整落盘，现在才替换内存上下文；因此所有前置失败都保留源项目。
    _projectFilePath = QDir::cleanPath (targetProjectFile);
    _manifest = candidate;
    _dirty = false;
    return true;
}

void ProjectManager::closeProject ()
{
    // 关闭不在这里静默保存。是否允许丢弃脏数据必须由上层 UI 或后续统一文档管理器
    // 决定；本类只负责清空当前项目上下文。
    _projectFilePath.clear ();
    _manifest = ProjectManifest ();
    _dirty = false;
}

bool ProjectManager::resolveResource (const QString& resourceId,
                                      QString& resolvedPath,
                                      QString* error) const
{
    // 外部资源解析入口：先确保有打开的项目，再按稳定 ID 查找资源定义。
    if (!hasProject ()) {
        setError (error, QString::fromUtf8 ("当前没有打开的项目。"));
        return false;
    }

    // 通过 findResource 取出资源副本，避免外部持有 QVector 内部元素指针。
    ProjectResource resource;
    if (!_manifest.findResource (resourceId, resource)) {
        setError (error,
                  QString::fromUtf8 ("项目中不存在资源：%1。").arg (resourceId));
        return false;
    }
    // 真正的路径拼接与越界检查全部委托给 ProjectPathResolver。
    return ProjectPathResolver::resolveResource (_projectFilePath, resource, resolvedPath, error);
}

bool ProjectManager::writeManifest (const QString& projectFilePath,
                                    const ProjectManifest& manifest,
                                    QString* error) const
{
    // 使用 QSaveFile 实现“原子写入”：先写临时文件，全部成功后再 rename 覆盖原文件，
    // 避免断电或写入中断留下半截、损坏的项目清单。
    QSaveFile file (projectFilePath);
    if (!file.open (QIODevice::WriteOnly | QIODevice::Text)) {
        setError (error,
                  QString::fromUtf8 ("无法写入项目文件：%1。").arg (file.errorString ())); 
        return false;
    }

    const QByteArray json = ProjectManifestJson::toJson (manifest);
    if (file.write (json) != json.size ()) {
        setError (error,
                  QString::fromUtf8 ("项目文件写入不完整：%1。").arg (file.errorString ())); 
        return false;
    }
    if (!file.commit ()) {
        setError (error,
                  QString::fromUtf8 ("项目文件原子提交失败：%1。").arg (file.errorString ())); 
        return false;
    }
    return true;
}

}    // namespace rws
