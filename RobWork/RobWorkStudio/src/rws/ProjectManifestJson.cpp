#include "ProjectManifestJson.hpp"

#include <QJsonArray>
#include <QJsonParseError>
#include <QSet>

namespace rws {
namespace {

// 统一错误回填工具：仅当调用方提供了 error 指针时才写入错误描述。
void setError (QString* error, const QString& message)
{
    if (error != nullptr)
        *error = message;
}

// 读取必填字符串字段：字段必须存在、是字符串类型且去除首尾空白后非空。
// 用于 format / id / name / 资源 id、kind、path 等不可缺省字段。
bool readRequiredString (const QJsonObject& object,
                         const QString& key,
                         QString& value,
                         QString* error)
{
    const QJsonValue jsonValue = object.value (key);
    if (!jsonValue.isString () || jsonValue.toString ().trimmed ().isEmpty ()) {
        setError (error, QString::fromUtf8 ("项目清单字段“%1”必须是非空字符串。").arg (key));
        return false;
    }
    value = jsonValue.toString ();
    return true;
}

// 判断资源列表里是否已存在指定 ID，用于入口引用和依赖引用的存在性检查。
bool containsResource (const QVector< ProjectResource >& resources, const QString& id)
{
    for (const ProjectResource& resource : resources) {
        if (resource.id == id)
            return true;
    }
    return false;
}

// 解析 resources 数组：逐项提取必填字段（id/kind/path）与可选字段（ownership/
// required/dependencies），任何一项类型或内容不合法都整体失败并返回原因。
bool parseResources (const QJsonObject& root,
                     QVector< ProjectResource >& resources,
                     QString* error)
{
    const QJsonValue resourcesValue = root.value (QStringLiteral ("resources"));
    if (!resourcesValue.isArray ()) {
        setError (error, QString::fromUtf8 ("项目清单的 resources 必须是数组。"));
        return false;
    }

    for (const QJsonValue& resourceValue : resourcesValue.toArray ()) {
        if (!resourceValue.isObject ()) {
            setError (error, QString::fromUtf8 ("resources 数组中的每一项必须是对象。"));
            return false;
        }

        const QJsonObject object = resourceValue.toObject ();
        ProjectResource resource;
        if (!readRequiredString (object, QStringLiteral ("id"), resource.id, error) ||
            !readRequiredString (object, QStringLiteral ("kind"), resource.kind, error) ||
            !readRequiredString (object, QStringLiteral ("path"), resource.path, error))
            return false;

        resource.ownership =
            object.value (QStringLiteral ("ownership")).toString (QStringLiteral ("project"));
        resource.required = object.value (QStringLiteral ("required")).toBool (false);

        const QJsonValue dependencies = object.value (QStringLiteral ("dependencies"));
        if (!dependencies.isUndefined () && !dependencies.isArray ()) {
            setError (error,
                      QString::fromUtf8 ("资源“%1”的 dependencies 必须是数组。").arg (
                          resource.id));
            return false;
        }
        for (const QJsonValue& dependency : dependencies.toArray ()) {
            if (!dependency.isString () || dependency.toString ().isEmpty ()) {
                setError (error,
                          QString::fromUtf8 ("资源“%1”的依赖 ID 必须是非空字符串。").arg (
                              resource.id));
                return false;
            }
            resource.dependencies.push_back (dependency.toString ());
        }
        resources.push_back (resource);
    }
    return true;
}

}    // namespace

// 线性查找稳定资源 ID；命中时通过 out 返回资源副本（而非内部指针）。
bool ProjectManifest::findResource (const QString& resourceId, ProjectResource& out) const
{
    for (const ProjectResource& resource : resources) {
        if (resource.id == resourceId) {
            out = resource;
            return true;
        }
    }
    return false;
}

// 序列化：把内存清单组装为有序 QJsonObject。字段顺序与解析逻辑一一对应。
QJsonObject ProjectManifestJson::toObject (const ProjectManifest& manifest)
{
    QJsonObject root;
    root.insert (QStringLiteral ("format"), manifest.format);
    root.insert (QStringLiteral ("schemaVersion"), manifest.schemaVersion);

    QJsonObject project;
    project.insert (QStringLiteral ("id"), manifest.project.id);
    project.insert (QStringLiteral ("name"), manifest.project.name);
    project.insert (QStringLiteral ("description"), manifest.project.description);
    project.insert (QStringLiteral ("createdAt"), manifest.project.createdAt);
    project.insert (QStringLiteral ("modifiedAt"), manifest.project.modifiedAt);
    project.insert (QStringLiteral ("application"), manifest.project.application);
    project.insert (QStringLiteral ("applicationVersion"), manifest.project.applicationVersion);
    root.insert (QStringLiteral ("project"), project);

    QJsonObject entryPoints;
    for (auto iterator = manifest.entryPoints.constBegin ();
         iterator != manifest.entryPoints.constEnd ();
         ++iterator) {
        entryPoints.insert (iterator.key (), iterator.value ());
    }
    root.insert (QStringLiteral ("entryPoints"), entryPoints);

    QJsonArray resources;
    for (const ProjectResource& resource : manifest.resources) {
        QJsonObject item;
        item.insert (QStringLiteral ("id"), resource.id);
        item.insert (QStringLiteral ("kind"), resource.kind);
        item.insert (QStringLiteral ("path"), resource.path);
        item.insert (QStringLiteral ("ownership"), resource.ownership);
        item.insert (QStringLiteral ("required"), resource.required);

        QJsonArray dependencies;
        for (const QString& dependency : resource.dependencies)
            dependencies.append (dependency);
        item.insert (QStringLiteral ("dependencies"), dependencies);
        resources.append (item);
    }
    root.insert (QStringLiteral ("resources"), resources);
    root.insert (QStringLiteral ("plugins"), manifest.plugins);
    root.insert (QStringLiteral ("settings"), manifest.settings);
    return root;
}

QByteArray ProjectManifestJson::toJson (const ProjectManifest& manifest,
                                        QJsonDocument::JsonFormat format)
{
    // 先转成 QJsonObject，再以指定格式（默认缩进）编码为字节串。
    return QJsonDocument (toObject (manifest)).toJson (format);
}

// 反序列化：从 QJsonObject 重建内存清单。先解析到一个局部 parsed 对象，
// 全部校验通过后才赋值给入参 manifest，保证失败时入参保持原样。
bool ProjectManifestJson::fromObject (const QJsonObject& object,
                                      ProjectManifest& manifest,
                                      QString* error)
{
    ProjectManifest parsed;
    if (!readRequiredString (object, QStringLiteral ("format"), parsed.format, error))
        return false;
    if (parsed.format != QStringLiteral ("RobWorkStudioProject")) {
        setError (error,
                  QString::fromUtf8 ("不是 RobWorkStudio 项目文件，format=%1。").arg (
                      parsed.format));
        return false;
    }

    const QJsonValue schemaValue = object.value (QStringLiteral ("schemaVersion"));
    if (!schemaValue.isDouble ()) {
        setError (error, QString::fromUtf8 ("项目清单的 schemaVersion 必须是整数。"));
        return false;
    }
    parsed.schemaVersion = schemaValue.toInt ();
    if (parsed.schemaVersion <= 0 ||
        parsed.schemaVersion > ProjectManifest::CurrentSchemaVersion) {
        setError (error,
                  QString::fromUtf8 ("不支持的项目清单版本：%1，当前版本为 %2。")
                      .arg (parsed.schemaVersion)
                      .arg (ProjectManifest::CurrentSchemaVersion));
        return false;
    }

    const QJsonValue projectValue = object.value (QStringLiteral ("project"));
    if (!projectValue.isObject ()) {
        setError (error, QString::fromUtf8 ("项目清单的 project 必须是对象。"));
        return false;
    }
    const QJsonObject project = projectValue.toObject ();
    if (!readRequiredString (project, QStringLiteral ("id"), parsed.project.id, error) ||
        !readRequiredString (project, QStringLiteral ("name"), parsed.project.name, error))
        return false;

    // 除 id 和 name 外，其余元数据都允许为空，以兼容早期项目和外部生成的清单。
    parsed.project.description = project.value (QStringLiteral ("description")).toString ();
    parsed.project.createdAt = project.value (QStringLiteral ("createdAt")).toString ();
    parsed.project.modifiedAt = project.value (QStringLiteral ("modifiedAt")).toString ();
    parsed.project.application = project.value (QStringLiteral ("application")).toString ();
    parsed.project.applicationVersion =
        project.value (QStringLiteral ("applicationVersion")).toString ();

    // 入口资源解析：每个入口键对应一个非空字符串类型的资源 ID，取值完整性校验
    // 交给最后的 validate()（检查被引用的资源确实存在）。
    const QJsonValue entryPointsValue = object.value (QStringLiteral ("entryPoints"));
    if (!entryPointsValue.isObject ()) {
        setError (error, QString::fromUtf8 ("项目清单的 entryPoints 必须是对象。"));
        return false;
    }
    const QJsonObject entryPoints = entryPointsValue.toObject ();
    for (auto iterator = entryPoints.constBegin (); iterator != entryPoints.constEnd ();
         ++iterator) {
        if (!iterator.value ().isString () || iterator.value ().toString ().isEmpty ()) {
            setError (error,
                      QString::fromUtf8 ("入口资源“%1”必须指向非空资源 ID。").arg (
                          iterator.key ()));
            return false;
        }
        parsed.entryPoints.insert (iterator.key (), iterator.value ().toString ());
    }

    if (!parseResources (object, parsed.resources, error))
        return false;
    parsed.plugins = object.value (QStringLiteral ("plugins")).toObject ();
    parsed.settings = object.value (QStringLiteral ("settings")).toObject ();

    if (!validate (parsed, error))
        return false;
    manifest = parsed;
    return true;
}

// 反序列化：先做 JSON 语法解析，再委托 fromObject 完成结构校验。
bool ProjectManifestJson::fromJson (const QByteArray& json,
                                    ProjectManifest& manifest,
                                    QString* error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson (json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject ()) {
        setError (error,
                  QString::fromUtf8 ("项目清单 JSON 无法解析：%1。").arg (
                      parseError.errorString ())); 
        return false;
    }
    return fromObject (document.object (), manifest, error);
}

// 结构校验：不涉及任何磁盘访问，只检查内存清单自身的一致性。
// 检查项依次为：format / schemaVersion / 必填字段 / 资源 ID 唯一性 / ownership
// 取值 / 入口引用 / 依赖引用。
bool ProjectManifestJson::validate (const ProjectManifest& manifest, QString* error)
{
    if (manifest.format != QStringLiteral ("RobWorkStudioProject")) {
        setError (error, QString::fromUtf8 ("项目清单 format 不正确。"));
        return false;
    }
    if (manifest.schemaVersion <= 0 ||
        manifest.schemaVersion > ProjectManifest::CurrentSchemaVersion) {
        setError (error, QString::fromUtf8 ("项目清单 schemaVersion 不受支持。"));
        return false;
    }
    if (manifest.project.id.trimmed ().isEmpty () ||
        manifest.project.name.trimmed ().isEmpty ()) {
        setError (error,
                  QString::fromUtf8 ("项目必须包含非空的 project.id 和 project.name。"));
        return false;
    }

    // 资源 ID 唯一性：插件之间通过稳定 ID 建立依赖，重复 ID 会让解析结果不确定。
    QSet< QString > resourceIds;
    for (const ProjectResource& resource : manifest.resources) {
        if (resource.id.trimmed ().isEmpty () || resource.kind.trimmed ().isEmpty () ||
            resource.path.trimmed ().isEmpty ()) {
            setError (error,
                      QString::fromUtf8 ("每个项目资源都必须包含非空的 id、kind 和 path。"));
            return false;
        }
        if (resourceIds.contains (resource.id)) {
            setError (error,
                      QString::fromUtf8 ("项目资源 ID 重复：%1。").arg (resource.id));
            return false;
        }
        resourceIds.insert (resource.id);

        // ownership 只允许三种取值：project（项目自含）、external（外部引用）、generated（生成产物）。
        if (resource.ownership != QStringLiteral ("project") &&
            resource.ownership != QStringLiteral ("external") &&
            resource.ownership != QStringLiteral ("generated")) {
            setError (error,
                      QString::fromUtf8 ("资源“%1”的 ownership 值不受支持：%2。")
                          .arg (resource.id)
                          .arg (resource.ownership));
            return false;
        }
    }

    // 入口引用完整性：每个入口键都必须指向 resources 中真实存在的资源 ID。
    for (auto iterator = manifest.entryPoints.constBegin ();
         iterator != manifest.entryPoints.constEnd ();
         ++iterator) {
        if (!containsResource (manifest.resources, iterator.value ())) {
            setError (error,
                      QString::fromUtf8 ("入口“%1”指向不存在的资源：%2。")
                          .arg (iterator.key ())
                          .arg (iterator.value ()));
            return false;
        }
    }

    // 依赖引用完整性：每个资源的依赖 ID 都必须在 resources 中存在，禁止悬空依赖。
    for (const ProjectResource& resource : manifest.resources) {
        for (const QString& dependency : resource.dependencies) {
            if (!containsResource (manifest.resources, dependency)) {
                setError (error,
                          QString::fromUtf8 ("资源“%1”依赖不存在的资源：%2。")
                              .arg (resource.id)
                              .arg (dependency));
                return false;
            }
        }
    }
    return true;
}

}    // namespace rws
