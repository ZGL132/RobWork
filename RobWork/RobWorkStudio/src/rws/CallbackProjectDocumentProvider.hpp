#ifndef RWS_CALLBACKPROJECTDOCUMENTPROVIDER_HPP
#define RWS_CALLBACKPROJECTDOCUMENTPROVIDER_HPP

#include "ProjectDocumentProvider.hpp"

#include <functional>

namespace rws {

/**
 * @brief 供业务插件复用的单资源类型 ProjectDocumentProvider 实现。
 *
 * RobotModelBuilder、工程需求和结构优化的 JSON 格式各不相同，但它们参与项目
 * 生命周期时需要完全一致的边界：Registry 负责解析项目相对路径和提交事务，插件
 * 只负责把领域对象读入内存或写到指定暂存路径。本类把这些重复的生命周期规则收敛，
 * 防止插件为了接入项目系统而直接覆盖正式文件。
 */
class CallbackProjectDocumentProvider : public ProjectDocumentProvider
{
  public:
    using LoadHandler = std::function< bool (const QString&,
                                             const ProjectDocumentContext&,
                                             QString*) >;
    using SaveHandler = std::function< bool (const QString&,
                                             const ProjectDocumentContext&,
                                             QString*) >;
    using CanCloseHandler = std::function< bool (QString*) >;
    using CloseHandler = std::function< void () >;
    using CleanHandler = std::function< void () >;

    /**
     * @brief 构造一个只处理一个清单 kind 的 Provider。
     *
     * providerId 与 kind 分别用于插件身份和清单资源类型匹配；两个回调都必须有效。
     * canClose/close 是可选扩展，结构优化可利用 canClose 在后台计算期间拒绝关闭，
     * 其它纯编辑文档则采用默认允许关闭的行为。
     */
    CallbackProjectDocumentProvider (QString providerId,
                                     QString kind,
                                     LoadHandler loadHandler,
                                     SaveHandler saveHandler,
                                     CanCloseHandler canCloseHandler = CanCloseHandler (),
                                     CloseHandler closeHandler = CloseHandler (),
                                     CleanHandler cleanHandler = CleanHandler ());

    QString providerId () const override;
    QStringList supportedResourceKinds () const override;
    bool loadResource (const ProjectResource& resource,
                       const ProjectDocumentContext& context,
                       QString* error) override;
    bool saveResource (const ProjectResource& resource,
                       const ProjectDocumentContext& context,
                       const QString& targetPath,
                       QString* error) override;
    bool isDirty (const QString& resourceId) const override;
    bool canClose (const QString& resourceId, QString* reason) const override;
    void markClean (const QString& resourceId) override;
    void closeResource (const QString& resourceId) override;

    /**
     * @brief 标记当前已加载文档发生可持久化变更。
     *
     * 插件应只在领域数据真正改变后调用本函数；Provider 不检查 UI 控件的焦点或选择
     * 状态，从而避免无意义的标题星号。保存事务完整提交后 Registry 会调用 markClean。
     */
    void markDirty ();

    /**
     * @brief 以插件领域快照的真实比较结果覆盖当前脏状态。
     *
     * 某些 Widget 通过序列化快照判断编辑是否已被用户手动还原；该接口允许它们清除
     * Provider 的脏标记，而不是把一次无实际变化的控件点击误报为未保存修改。
     */
    void setDirty (bool dirty);

  private:
    QString _providerId;    // 插件身份标识（注册表查重用）。
    QString _kind;          // 本 Provider 处理的唯一资源 kind。
    LoadHandler _loadHandler;    // 领域加载回调（接收解析后的绝对路径）。
    SaveHandler _saveHandler;    // 领域保存回调（接收暂存写入路径）。
    CanCloseHandler _canCloseHandler;   // 可选：是否允许关闭（如后台计算中拒绝）。
    CloseHandler _closeHandler;         // 可选：关闭文档时的领域清理。
    CleanHandler _cleanHandler;         // 可选：保存事务完整提交后的清理/重置。
    QString _resourceId;    // 当前持有的资源 ID；空表示未加载任何项目资源。
    bool _dirty = false;    // 当前资源是否有未保存修改。
};

}    // namespace rws

#endif    // RWS_CALLBACKPROJECTDOCUMENTPROVIDER_HPP
