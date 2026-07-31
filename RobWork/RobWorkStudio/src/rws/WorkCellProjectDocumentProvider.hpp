#ifndef RWS_WORKCELLPROJECTDOCUMENTPROVIDER_HPP
#define RWS_WORKCELLPROJECTDOCUMENTPROVIDER_HPP

#include "ProjectDocumentProvider.hpp"

#include <functional>

namespace rws {

/**
 * @brief 将主窗口中的 WorkCell 文档接入统一项目生命周期。
 *
 * Provider 不直接持有三维视图、碰撞检测器或插件列表，而是通过加载/保存回调调用
 * RobWorkStudio 现有逻辑。这样既复用成熟的 WorkCell 打开流程，又避免核心项目层
 * 反向依赖完整主窗口类型。
 */
class WorkCellProjectDocumentProvider : public ProjectDocumentProvider
{
  public:
    // 加载/保存回调类型：第一个参数是解析后的文件路径，第二个是错误回填。
    // 由主窗口提供 lambda 复用现有 WorkCell 打开/保存逻辑。
    using LoadHandler = std::function< bool (const QString&, QString*) >;
    using SaveHandler = std::function< bool (const QString&, QString*) >;

    WorkCellProjectDocumentProvider (LoadHandler loadHandler, SaveHandler saveHandler);

    // 固定标识 "rws.workcell"，用于注册表查重。
    QString providerId () const override;
    // 只处理 "robwork.workcell" 一种 kind。
    QStringList supportedResourceKinds () const override;

    // 解析资源路径后调用加载回调；回调成功才切换活动资源 ID 并复位脏标记。
    bool loadResource (const ProjectResource& resource,
                       const ProjectDocumentContext& context,
                       QString* error) override;

    // 校验资源确实由本 Provider 持有后，把当前 WorkCell 保存到 targetPath
    //（保存事务提供的暂存路径），不直接覆盖正式文件。
    bool saveResource (const ProjectResource& resource,
                       const ProjectDocumentContext& context,
                       const QString& targetPath,
                       QString* error) override;

    // 只有当前持有的资源且其脏标记为 true 才算脏。
    bool isDirty (const QString& resourceId) const override;
    // WorkCell 无后台任务或外部锁，恒可关闭；未保存修改由主窗口统一提示。
    bool canClose (const QString& resourceId, QString* reason) const override;
    // 保存事务成功提交后清除脏标记。
    void markClean (const QString& resourceId) override;
    // 关闭当前持有的资源：清空资源 ID 与脏标记。
    void closeResource (const QString& resourceId) override;

    /** @brief 标记当前已加载的 WorkCell 资源发生了可持久化修改。 */
    void markDirty ();

  private:
    LoadHandler _loadHandler;   // 加载回调（复用主窗口打开流程）。
    SaveHandler _saveHandler;   // 保存回调（复用主窗口 DOM 保存流程）。
    QString _resourceId;        // 当前持有的资源 ID；空表示未加载任何项目资源。
    bool _dirty = false;        // 当前资源是否有未保存修改。
};

}    // namespace rws

#endif    // RWS_WORKCELLPROJECTDOCUMENTPROVIDER_HPP
