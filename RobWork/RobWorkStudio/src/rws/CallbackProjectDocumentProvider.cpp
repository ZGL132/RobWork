#include "CallbackProjectDocumentProvider.hpp"

#include "ProjectPathResolver.hpp"

#include <QDataStream>
#include <QIODevice>

#include <utility>

namespace rws {

// 构造函数：按值接收标识与各回调并移动到成员。loadHandler/saveHandler 为必填，
// canClose/close/clean 可选；未提供的扩展回调在对应生命周期操作中按默认行为处理。
CallbackProjectDocumentProvider::CallbackProjectDocumentProvider (
    QString providerId,
    QString kind,
    LoadHandler loadHandler,
    SaveHandler saveHandler,
    CanCloseHandler canCloseHandler,
    CloseHandler closeHandler,
    CleanHandler cleanHandler,
    SnapshotHandler snapshotHandler,
    RestoreHandler restoreHandler) :
    _providerId (std::move (providerId)), _kind (std::move (kind)),
    _loadHandler (std::move (loadHandler)), _saveHandler (std::move (saveHandler)),
    _canCloseHandler (std::move (canCloseHandler)), _closeHandler (std::move (closeHandler)),
    _cleanHandler (std::move (cleanHandler)), _snapshotHandler (std::move (snapshotHandler)),
    _restoreHandler (std::move (restoreHandler))
{}

// 返回构造时传入的插件身份标识，用于注册表查重。
QString CallbackProjectDocumentProvider::providerId () const
{
    return _providerId;
}

// 本 Provider 只处理构造时声明的单一 kind。
QStringList CallbackProjectDocumentProvider::supportedResourceKinds () const
{
    return {_kind};
}

bool CallbackProjectDocumentProvider::loadResource (const ProjectResource& resource,
                                                    const ProjectDocumentContext& context,
                                                    QString* error)
{
    QString resolvedPath;
    // 路径安全必须在调用插件回调前完成。这样即使第三方插件的 JSON 加载器没有路径
    // 校验能力，也无法借项目清单的 "../" 或绝对路径绕出项目根目录。
    if (!ProjectPathResolver::resolveResource (
            context.projectFilePath, resource, resolvedPath, error))
        return false;
    if (!_loadHandler || !_loadHandler (resolvedPath, context, error))
        return false;

    // 只有领域对象成功替换后才切换资源 ID 和清除脏状态；失败时仍保留此前已打开的
    // 文档身份，避免保存操作错误地写入加载失败的新资源。
    _resourceId = resource.id;
    _dirty = false;
    return true;
}

bool CallbackProjectDocumentProvider::saveResource (const ProjectResource& resource,
                                                    const ProjectDocumentContext& context,
                                                    const QString& targetPath,
                                                    QString* error)
{
    if (resource.id != _resourceId) {
        if (error != nullptr) {
            *error = QString::fromUtf8 ("Provider“%1”未加载资源“%2”。")
                         .arg (_providerId)
                         .arg (resource.id);
        }
        return false;
    }

    // targetPath 是 ProjectSaveTransaction 创建的同目录暂存文件。回调不接收正式
    // 资源路径，确保多文件保存中任一 Provider 暂存失败时不会污染上次成功版本。
    if (!_saveHandler || !_saveHandler (targetPath, context, error)) {
        if (error != nullptr && error->isEmpty ())
            *error = QString::fromUtf8 ("Provider“%1”保存资源“%2”失败。")
                         .arg (_providerId)
                         .arg (resource.id);
        return false;
    }
    return true;
}

// 脏状态仅对当前持有的资源生效；资源 ID 不匹配时视为不脏。
bool CallbackProjectDocumentProvider::isDirty (const QString& resourceId) const
{
    return resourceId == _resourceId && _dirty;
}

bool CallbackProjectDocumentProvider::canClose (const QString& resourceId, QString* reason) const
{
    if (resourceId != _resourceId)
        return true;
    // 后台计算等插件私有状态不能泄漏到 Registry；通过可选回调把“是否允许关闭”的
    // 决策留在领域模块。未提供回调的普通 JSON 文档默认允许关闭。
    return !_canCloseHandler || _canCloseHandler (reason);
}

void CallbackProjectDocumentProvider::markClean (const QString& resourceId)
{
    if (resourceId == _resourceId) {
        _dirty = false;
        // 此回调只在 ProjectSaveTransaction 全部资源提交成功后触发。Widget 因而不会
        // 把“仅写入暂存文件”的中间状态误认为已经安全保存到正式项目资源。
        if (_cleanHandler)
            _cleanHandler ();
    }
}

// 关闭当前资源：先通知插件的领域清理回调，再清空资源 ID 与脏标记；
// 不是当前资源时是空操作。
void CallbackProjectDocumentProvider::closeResource (const QString& resourceId)
{
    if (resourceId != _resourceId)
        return;
    if (_closeHandler)
        _closeHandler ();
    _resourceId.clear ();
    _dirty = false;
}

// 快照当前领域内存状态（候选项目替换前）：把资源 ID、脏标记与领域快照一并编码为
// 不透明字节。未绑定资源、缺少快照/恢复回调或编码失败时返回 false，明确拒绝过渡。
bool CallbackProjectDocumentProvider::snapshotResource (const QString& resourceId,
                                                        QByteArray* snapshot,
                                                        QString* error) const
{
    if (snapshot == nullptr || resourceId != _resourceId || !_snapshotHandler ||
        !_restoreHandler) {
        if (error != nullptr)
            *error = QStringLiteral ("Provider '%1' cannot snapshot resource '%2'.")
                         .arg (_providerId, resourceId);
        return false;
    }

    QByteArray documentSnapshot;
    if (!_snapshotHandler (&documentSnapshot, error))
        return false;

    // 把（资源 ID, 脏标记, 领域快照）打包进 QByteArray，恢复时校验 ID 防止错位恢复。
    QDataStream stream (snapshot, QIODevice::WriteOnly);
    stream << _resourceId << _dirty << documentSnapshot;
    if (stream.status () != QDataStream::Ok) {
        if (error != nullptr)
            *error = QStringLiteral ("Provider '%1' could not encode its snapshot.")
                         .arg (_providerId);
        return false;
    }
    return true;
}

// 从快照恢复领域内存状态（候选项目回滚时）：解码后先校验资源 ID 一致，再调用领域
// 恢复回调；成功后恢复 Provider 自身的资源 ID 与脏标记。
bool CallbackProjectDocumentProvider::restoreResource (const QString& resourceId,
                                                       const QByteArray& snapshot,
                                                       QString* error)
{
    if (!_restoreHandler) {
        if (error != nullptr)
            *error = QStringLiteral ("Provider '%1' cannot restore resource '%2'.")
                         .arg (_providerId, resourceId);
        return false;
    }

    QString savedResourceId;
    bool savedDirty = false;
    QByteArray documentSnapshot;
    QDataStream stream (snapshot);
    stream >> savedResourceId >> savedDirty >> documentSnapshot;
    if (stream.status () != QDataStream::Ok || savedResourceId != resourceId) {
        if (error != nullptr)
            *error = QStringLiteral ("Provider '%1' received an invalid snapshot for '%2'.")
                         .arg (_providerId, resourceId);
        return false;
    }
    if (!_restoreHandler (documentSnapshot, error))
        return false;

    _resourceId = savedResourceId;
    _dirty = savedDirty;
    return true;
}

// 直接置脏：插件应在领域数据确实变化后调用；未绑定资源时是空操作。
void CallbackProjectDocumentProvider::markDirty ()
{
    if (!_resourceId.isEmpty ())
        _dirty = true;
}

// 按插件领域快照的实测结果覆盖脏状态；dirty=false 用于用户把值改回原值时清脏。
void CallbackProjectDocumentProvider::setDirty (bool dirty)
{
    if (!_resourceId.isEmpty ())
        _dirty = dirty;
}

void CallbackProjectDocumentProvider::adoptGeneratedResource (const QString& resourceId)
{
    // 仅在 Registry 已登记首次编辑生成的资源后调用。该资源没有历史文件，因此 Provider
    // 需要无需 loadResource 即绑定资源身份，后续才能进入与普通资源相同的保存事务。
    _resourceId = resourceId;
    // 绑定本身不表示领域配置已改变；调用者会在设置 Widget 快照基线后传入真实脏状态。
    _dirty = false;
}

}    // namespace rws
