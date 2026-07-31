#ifndef RWS_PROJECTDOCUMENTPROVIDER_HPP
#define RWS_PROJECTDOCUMENTPROVIDER_HPP

#include "ProjectManifest.hpp"

#include <QString>
#include <QStringList>

namespace rws {

/**
 * @brief Provider 加载和保存资源时使用的只读项目上下文。
 *
 * 上下文不拥有清单，也不允许 Provider 通过指针修改清单。项目清单的变更必须经过
 * ProjectManager，避免某个插件在保存过程中偷偷改变其他插件的资源索引。
 */
struct ProjectDocumentContext
{
    QString projectFilePath;       // 当前项目 .rwproj 文件的绝对路径。
    QString projectDirectory;      // 项目文件所在目录（用于解析相对资源路径）。
    const ProjectManifest* manifest = nullptr;    // 只读的项目清单指针。
};

/**
 * @brief 项目资源与具体业务文档之间的适配接口。
 *
 * 每种资源格式只实现一个 Provider，例如 WorkCell、机器人模型和工程需求分别由
 * 不同 Provider 负责。Provider 不直接决定保存事务如何提交，saveResource 只负责
 * 将内容写到传入的暂存路径；最终替换由 ProjectSaveTransaction 统一完成。
 */
class ProjectDocumentProvider
{
  public:
    virtual ~ProjectDocumentProvider () = default;

    // 提供方唯一标识（如 "rws.workcell"），用于注册表查重与排错。
    virtual QString providerId () const = 0;
    // 声明该 Provider 能处理的资源 kind 列表（如 "robwork.workcell"）。
    // 每个 kind 只允许被一个 Provider 注册，避免加载结果依赖插件注册顺序。
    virtual QStringList supportedResourceKinds () const = 0;

    // 加载一个资源：把项目中的资源路径解析并加载为具体业务文档。
    // 成功返回 true；失败返回 false 并经 error 回填原因。
    virtual bool loadResource (const ProjectResource& resource,
                               const ProjectDocumentContext& context,
                               QString* error) = 0;

    // 保存一个资源：把当前文档内容写入 targetPath。
    // 注意 targetPath 是保存事务分配的暂存路径，Provider 不应直接覆盖正式文件；
    // 正式替换由 ProjectSaveTransaction 统一完成。
    virtual bool saveResource (const ProjectResource& resource,
                               const ProjectDocumentContext& context,
                               const QString& targetPath,
                               QString* error) = 0;

    // 查询指定资源是否有未保存的修改（脏状态）。
    virtual bool isDirty (const QString& resourceId) const = 0;
    // 询问指定资源是否允许关闭；不允许时经 reason 说明原因（如后台任务未结束）。
    virtual bool canClose (const QString& resourceId, QString* reason) const = 0;
    // 保存事务全部成功后调用，清除指定资源的脏标记。
    virtual void markClean (const QString& resourceId) = 0;
    // 关闭指定资源，释放其持有的文档状态。
    virtual void closeResource (const QString& resourceId) = 0;
};

}    // namespace rws

#endif    // RWS_PROJECTDOCUMENTPROVIDER_HPP
