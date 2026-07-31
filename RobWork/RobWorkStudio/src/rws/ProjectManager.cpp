#include "ProjectManager.hpp"

#include "ProjectManifestJson.hpp"
#include "ProjectPathResolver.hpp"

#include <QDateTime>
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

// 生成当前 UTC 时间的 ISO-8601 字符串，用于清单的 createdAt / modifiedAt 元数据。
QString nowUtc ()
{
    return QDateTime::currentDateTimeUtc ().toString (Qt::ISODate);
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
