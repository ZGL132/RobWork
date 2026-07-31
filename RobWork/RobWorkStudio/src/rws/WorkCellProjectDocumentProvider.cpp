#include "WorkCellProjectDocumentProvider.hpp"

#include "ProjectPathResolver.hpp"

#include <utility>

namespace rws {

// 构造函数：接收主窗口提供的加载/保存回调，二者均可为空（加载/保存时按失败处理）。
WorkCellProjectDocumentProvider::WorkCellProjectDocumentProvider (LoadHandler loadHandler,
                                                                  SaveHandler saveHandler) :
    _loadHandler (std::move (loadHandler)), _saveHandler (std::move (saveHandler))
{}

// Provider 唯一标识；注册表用它检测重复注册。
QString WorkCellProjectDocumentProvider::providerId () const
{
    return QStringLiteral ("rws.workcell");
}

// 本 Provider 仅处理 robwork.workcell 类型资源。
QStringList WorkCellProjectDocumentProvider::supportedResourceKinds () const
{
    return {QStringLiteral ("robwork.workcell")};
}

// 加载资源：先解析资源到磁盘上的绝对路径，再调用加载回调真正打开 WorkCell。
bool WorkCellProjectDocumentProvider::loadResource (const ProjectResource& resource,
                                                    const ProjectDocumentContext& context,
                                                    QString* error)
{
    QString resolvedPath;
    if (!ProjectPathResolver::resolveResource (
            context.projectFilePath, resource, resolvedPath, error))
        return false;
    if (!_loadHandler || !_loadHandler (resolvedPath, error))
        return false;

    // 只有加载回调完整成功后才切换活动资源 ID，防止失败资源被误标记为已加载。
    _resourceId = resource.id;
    _dirty = false;
    return true;
}

// 保存资源：只允许保存本 Provider 当前持有的资源；实际内容写入由保存回调完成。
// 注意 targetPath 是事务分配的暂存路径，正式替换交给 ProjectSaveTransaction。
bool WorkCellProjectDocumentProvider::saveResource (const ProjectResource& resource,
                                                    const ProjectDocumentContext&,
                                                    const QString& targetPath,
                                                    QString* error)
{
    if (resource.id != _resourceId) {
        if (error != nullptr) {
            *error = QString::fromUtf8 ("WorkCell Provider 未加载资源：%1。").arg (
                resource.id);
        }
        return false;
    }
    return _saveHandler && _saveHandler (targetPath, error);
}

// 脏状态仅对当前持有的资源生效；资源 ID 不匹配时视为不脏。
bool WorkCellProjectDocumentProvider::isDirty (const QString& resourceId) const
{
    return resourceId == _resourceId && _dirty;
}

bool WorkCellProjectDocumentProvider::canClose (const QString&, QString*) const
{
    // WorkCell 当前没有后台任务或外部锁需要阻止关闭；未保存修改由主窗口统一提示。
    return true;
}

// 保存成功后清除脏标记；只对当前持有的资源生效。
void WorkCellProjectDocumentProvider::markClean (const QString& resourceId)
{
    if (resourceId == _resourceId)
        _dirty = false;
}

// 关闭当前资源：清空资源 ID 与脏标记；不是当前资源时是空操作。
void WorkCellProjectDocumentProvider::closeResource (const QString& resourceId)
{
    if (resourceId != _resourceId)
        return;
    _resourceId.clear ();
    _dirty = false;
}

// 供主窗口在 JOG、setState 等场景调用：只有绑定项目资源时才置脏，
// 项目外临时打开的独立 WorkCell 不会被错误纳入项目保存。
void WorkCellProjectDocumentProvider::markDirty ()
{
    if (!_resourceId.isEmpty ())
        _dirty = true;
}

}    // namespace rws
