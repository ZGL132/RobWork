#include "ProjectDocumentRegistry.hpp"

#include "ProjectPathResolver.hpp"
#include "ProjectSaveTransaction.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace rws {
namespace {

// 统一错误回填工具：仅当调用方提供了 error 指针时才写入错误描述。
void setError (QString* error, const QString& message)
{
    if (error != nullptr)
        *error = message;
}

// 从清单和项目文件路径构造 Provider 的只读上下文。项目目录取绝对路径，保证各
// Provider 内部相对路径解析以项目文件为基准，而不是依赖进程当前工作目录。
ProjectDocumentContext makeContext (const ProjectManifest& manifest,
                                    const QString& projectFilePath)
{
    ProjectDocumentContext context;
    context.projectFilePath = QFileInfo (projectFilePath).absoluteFilePath ();
    context.projectDirectory = QFileInfo (context.projectFilePath).absolutePath ();
    context.manifest = &manifest;
    return context;
}

}    // namespace

// 注册 Provider：先做完整合法性校验（非空、providerId 唯一、kind 非空且不冲突），
// 全部通过后才写入两个索引表，保证注册是原子操作。
bool ProjectDocumentRegistry::registerProvider (ProjectDocumentProvider* provider,
                                                QString* error)
{
    if (provider == nullptr || provider->providerId ().trimmed ().isEmpty ()) {
        setError (error, QString::fromUtf8 ("Provider 及其 providerId 不能为空。"));
        return false;
    }
    if (_providersById.contains (provider->providerId ())) {
        setError (error,
                  QString::fromUtf8 ("Provider ID 重复：%1。").arg (provider->providerId ()));
        return false;
    }

    const QStringList kinds = provider->supportedResourceKinds ();
    if (kinds.isEmpty ()) {
        setError (error,
                  QString::fromUtf8 ("Provider“%1”没有声明任何资源 kind。").arg (
                      provider->providerId ()));
        return false;
    }
    for (const QString& kind : kinds) {
        if (kind.trimmed ().isEmpty () || _providersByKind.contains (kind)) {
            setError (error,
                      QString::fromUtf8 ("资源 kind 重复或为空：%1。").arg (kind));
            return false;
        }
    }

    _providersById.insert (provider->providerId (), provider);
    for (const QString& kind : kinds)
        _providersByKind.insert (kind, provider);
    return true;
}

// 加载整个项目的资源文档。流程：先对依赖图做拓扑排序，再关闭旧文档，随后按顺序
// 逐个解析路径并调用 Provider 加载；任何资源失败都会关闭已加载部分并返回 false。
bool ProjectDocumentRegistry::loadProjectResources (const ProjectManifest& manifest,
                                                    const QString& projectFilePath,
                                                    QString* error)
{
    return loadProjectResources (manifest, projectFilePath, error, nullptr);
}

bool ProjectDocumentRegistry::loadProjectResources (const ProjectManifest& manifest,
                                                    const QString& projectFilePath,
                                                    QString* error,
                                                    QStringList* warnings)
{
    QVector< ProjectResource > ordered;
    if (!buildLoadOrder (manifest, ordered, error))
        return false;

    // 先验证完整依赖图，再关闭当前文档。若新项目存在依赖环，用户当前已打开的
    // 文档仍保持原状，不会因为尚未开始的加载尝试而被提前释放。
    closeResources ();

    const ProjectDocumentContext context = makeContext (manifest, projectFilePath);
    for (const ProjectResource& resource : ordered) {
        // 按 kind 找 Provider：可选资源缺 Provider 直接跳过；必需资源缺 Provider
        // 会让项目无法在缺少关键能力时正确工作，必须整体失败。
        ProjectDocumentProvider* provider = providerForKind (resource.kind);
        if (provider == nullptr) {
            if (resource.required) {
                setError (error,
                          QString::fromUtf8 ("必需资源“%1”没有可用 Provider，kind=%2。")
                              .arg (resource.id)
                              .arg (resource.kind));
                closeResources ();
                return false;
            }
            if (warnings != nullptr) {
                warnings->push_back (QString::fromUtf8 (
                    "可选资源“%1”没有可用 Provider，已跳过。").arg (resource.id));
            }
            continue;
        }

        QString resolvedPath;
        const bool pathResolved = ProjectPathResolver::resolveResource (
            projectFilePath, resource, resolvedPath, error);
        const bool resourceLoaded = pathResolved && provider->loadResource (resource, context, error);
        if (!resourceLoaded) {
            // 必需资源失败必须终止打开；可选资源允许因旧数据、附属文件缺失或插件兼容性
            // 问题降级跳过。清空本次局部错误，避免返回成功却把失败文本误传给调用方。
            if (!resource.required) {
                if (warnings != nullptr) {
                    const QString reason = error != nullptr && !error->isEmpty () ?
                        *error : QString::fromUtf8 ("资源加载失败。");
                    warnings->push_back (QString::fromUtf8 (
                        "可选资源“%1”已跳过：%2").arg (resource.id, reason));
                }
                if (error != nullptr)
                    error->clear ();
                continue;
            }
            // 若 Provider 已回填具体错误则保留，否则补一个带资源 ID 的通用错误。
            if (error != nullptr && error->isEmpty ()) {
                *error = QString::fromUtf8 ("加载项目资源失败：%1。").arg (resource.id);
            }
            closeResources ();
            return false;
        }

        // 加载成功后登记资源记录，供后续保存、脏检查和关闭使用。
        LoadedResource loaded;
        loaded.resource = resource;
        loaded.provider = provider;
        loaded.resolvedPath = resolvedPath;
        _loaded.push_back (loaded);
    }
    return true;
}

// 保存全部脏资源：把所有脏资源依次写入保存事务，最后统一 commit。
// 任一台账的 stage 失败时事务析构会自动回滚，正式文件不会被改动。
bool ProjectDocumentRegistry::saveDirtyResources (const ProjectManifest& manifest,
                                                  const QString& projectFilePath,
                                                  QString* error)
{
    const ProjectDocumentContext context = makeContext (manifest, projectFilePath);
    ProjectSaveTransaction transaction;
    for (const LoadedResource& loaded : _loaded) {
        if (!loaded.provider->isDirty (loaded.resource.id))
            continue;
        if (!transaction.stage (*loaded.provider,
                                loaded.resource,
                                context,
                                loaded.resolvedPath,
                                error))
            return false;
    }
    return transaction.commit (error);
}

// 自动保存：把当前 Provider 中已加载的文档序列化到快照目录（写入暂存目标），未加载的
// 自有资源按磁盘副本保留。这样恢复快照也能捕捉 Provider 内存里尚未落盘的编辑状态；
// 本操作不清除 Provider 的脏标记，脏状态仍由“保存项目”事务统一处理。
bool ProjectDocumentRegistry::saveAutosaveResources (const ProjectManifest& manifest,
                                                     const QString& sourceProjectFilePath,
                                                     const QString& snapshotProjectFilePath,
                                                     QString* error)
{
    const ProjectDocumentContext snapshotContext = makeContext (manifest, snapshotProjectFilePath);
    for (const ProjectResource& resource : manifest.resources) {
        if (resource.ownership == QStringLiteral ("external"))
            continue;

        QString targetPath;
        if (!ProjectPathResolver::resolveResource (
                snapshotProjectFilePath, resource, targetPath, error) ||
            !QDir ().mkpath (QFileInfo (targetPath).absolutePath ())) {
            if (error != nullptr && error->isEmpty ())
                *error = QString::fromUtf8 ("无法创建自动保存资源目录。");
            return false;
        }

        const LoadedResource* loadedResource = nullptr;
        for (const LoadedResource& loaded : _loaded) {
            if (loaded.resource.id == resource.id) {
                loadedResource = &loaded;
                break;
            }
        }
        if (loadedResource != nullptr) {
            if (!loadedResource->provider->saveResource (resource, snapshotContext, targetPath, error))
                return false;
            continue;
        }

        QString sourcePath;
        if (!ProjectPathResolver::resolveResource (sourceProjectFilePath, resource, sourcePath, error) ||
            !QFileInfo (sourcePath).isFile ()) {
            if (!resource.required)
                continue;
            if (error != nullptr && error->isEmpty ())
                *error = QString::fromUtf8 ("无法读取自动保存资源：%1。").arg (resource.id);
            return false;
        }
        QFile source (sourcePath);
        QSaveFile target (targetPath);
        if (!source.open (QIODevice::ReadOnly) || !target.open (QIODevice::WriteOnly) ||
            target.write (source.readAll ()) < 0 || !target.commit ()) {
            setError (error, QString::fromUtf8 ("无法写入自动保存资源：%1。").arg (resource.id));
            return false;
        }
    }
    return true;
}

// 逐资源询问是否允许关闭；只要有一个资源拒绝（如仍有后台任务）就整体不关闭。
// 生成资源没有历史文件可供 Provider 加载。Registry 仍将其登记为已加载资源，
// 使第一次保存通过与其它文档相同的暂存事务创建正式 JSON，而不是由插件直接写盘。
bool ProjectDocumentRegistry::activateGeneratedResource (const ProjectResource& resource,
                                                         const QString& projectFilePath,
                                                         QString* error)
{
    ProjectDocumentProvider* provider = providerForKind (resource.kind);
    if (provider == nullptr) {
        setError (error, QString::fromUtf8 ("生成资源“%1”没有可用 Provider。")
                              .arg (resource.id));
        return false;
    }
    for (const LoadedResource& loaded : _loaded) {
        if (loaded.resource.id == resource.id)
            return true;
    }

    QString resolvedPath;
    if (!ProjectPathResolver::resolveResource (projectFilePath, resource, resolvedPath, error))
        return false;

    LoadedResource loaded;
    loaded.resource = resource;
    loaded.provider = provider;
    loaded.resolvedPath = resolvedPath;
    _loaded.push_back (loaded);
    return true;
}

bool ProjectDocumentRegistry::canClose (QString* reason) const
{
    for (const LoadedResource& loaded : _loaded) {
        if (!loaded.provider->canClose (loaded.resource.id, reason))
            return false;
    }
    return true;
}

void ProjectDocumentRegistry::closeResources ()
{
    // 按加载顺序的逆序关闭，确保依赖者先释放，依赖资源最后释放。
    for (int index = _loaded.size () - 1; index >= 0; --index)
        _loaded[index].provider->closeResource (_loaded[index].resource.id);
    _loaded.clear ();
}

// 是否存在任一脏资源；用于标题栏星号与关闭确认。
bool ProjectDocumentRegistry::isDirty () const
{
    for (const LoadedResource& loaded : _loaded) {
        if (loaded.provider->isDirty (loaded.resource.id))
            return true;
    }
    return false;
}

// 收集所有脏资源的 ID，供 UI 展示未保存内容。
QStringList ProjectDocumentRegistry::dirtyResourceIds () const
{
    QStringList result;
    for (const LoadedResource& loaded : _loaded) {
        if (loaded.provider->isDirty (loaded.resource.id))
            result.push_back (loaded.resource.id);
    }
    return result;
}

// 按 kind 索引直接查询 Provider；未注册的 kind 返回 nullptr。
ProjectDocumentProvider* ProjectDocumentRegistry::providerForKind (const QString& kind) const
{
    return _providersByKind.value (kind, nullptr);
}

// 建立全量加载顺序：对清单中每个资源执行 DFS 拓扑排序。marks 跨资源共享，
// 使环检测和“已访问”状态在整张依赖图中保持一致。
bool ProjectDocumentRegistry::buildLoadOrder (const ProjectManifest& manifest,
                                              QVector< ProjectResource >& ordered,
                                              QString* error) const
{
    QHash< QString, int > marks;
    for (const ProjectResource& resource : manifest.resources) {
        if (!visitResource (manifest, resource.id, marks, ordered, error))
            return false;
    }
    return true;
}

// DFS 访问单个资源（拓扑排序 + 环检测）：
//   mark 0 = 未访问；mark 1 = 递归栈中（再次遇到即存在环）；mark 2 = 已完成。
// 先递归访问全部依赖，再把当前资源追加到有序列表末尾，从而保证依赖先于依赖者。
bool ProjectDocumentRegistry::visitResource (const ProjectManifest& manifest,
                                             const QString& resourceId,
                                             QHash< QString, int >& marks,
                                             QVector< ProjectResource >& ordered,
                                             QString* error) const
{
    const int mark = marks.value (resourceId, 0);
    if (mark == 2)
        return true;
    if (mark == 1) {
        setError (error,
                  QString::fromUtf8 ("项目资源依赖形成环：%1。").arg (resourceId));
        return false;
    }

    ProjectResource resource;
    if (!manifest.findResource (resourceId, resource)) {
        setError (error,
                  QString::fromUtf8 ("依赖排序时找不到资源：%1。").arg (resourceId));
        return false;
    }

    marks.insert (resourceId, 1);
    for (const QString& dependency : resource.dependencies) {
        if (!visitResource (manifest, dependency, marks, ordered, error))
            return false;
    }
    marks.insert (resourceId, 2);
    ordered.push_back (resource);
    return true;
}

}    // namespace rws
