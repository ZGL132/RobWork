#ifndef RWS_PROJECTDOCUMENTREGISTRY_HPP
#define RWS_PROJECTDOCUMENTREGISTRY_HPP

#include "ProjectDocumentProvider.hpp"

#include <QHash>
#include <QVector>

namespace rws {

class ProjectSaveTransaction;

/**
 * @brief 项目文档 Provider 注册表和生命周期协调器。
 *
 * Registry 不理解具体业务 JSON/XML 内容，只按资源 kind 找到 Provider，并以清单
 * dependencies 建立加载顺序。未安装的可选 Provider 会被跳过；未安装的必需
 * Provider 会使项目打开失败，防止项目在缺少关键能力时静默变形。
 */
class ProjectDocumentRegistry
{
  public:
    // 注册一个 Provider：校验 providerId 非空且唯一、kind 列表非空且不与已注册的
    // kind 冲突。本类保存非拥有型指针，Provider 生命周期由注册方（主窗口）负责。
    bool registerProvider (ProjectDocumentProvider* provider, QString* error = nullptr);

    // 按清单加载全部项目资源：先做依赖拓扑排序（含环检测），再按顺序逐个调用
    // Provider 加载。加载失败或必需资源缺 Provider 时回滚并返回 false。
    bool loadProjectResources (const ProjectManifest& manifest,
                               const QString& projectFilePath,
                               QString* error = nullptr);
    bool loadProjectResources (const ProjectManifest& manifest,
                               const QString& projectFilePath,
                               QString* error,
                               QStringList* warnings);

    // 把所有脏资源通过 ProjectSaveTransaction 统一保存；先暂存、后替换，任一失败
    // 即整体回滚，保证不会出现半提交状态。
    bool saveDirtyResources (const ProjectManifest& manifest,
                              const QString& projectFilePath,
                              QString* error = nullptr);

    // 向尚未可见的恢复快照目录写入当前 Provider 状态。已加载资源从内存序列化，
    // 未加载资源按磁盘副本保留；此操作不会清除 Provider 的脏标记。
    bool saveAutosaveResources (const ProjectManifest& manifest,
                                const QString& sourceProjectFilePath,
                                const QString& snapshotProjectFilePath,
                                QString* error = nullptr);

    // 把运行中首次创建的项目资源加入已加载列表。该资源尚无磁盘文件，因此不能走 load；
    // 但其后续保存、脏检测和关闭会与打开项目时加载的资源完全使用同一套事务生命周期。
    bool activateGeneratedResource (const ProjectResource& resource,
                                    const QString& projectFilePath,
                                    QString* error = nullptr);

    // 在不关闭其它项目文档的前提下，把同一稳定资源 ID 重新加载到新的受管路径。
    // 加载失败时尝试恢复旧资源，成功后才替换 Registry 中的资源描述和解析路径。
    bool reloadResource (const ProjectResource& resource,
                         const ProjectManifest& manifest,
                         const QString& projectFilePath,
                         QString* error = nullptr);

    // 询问所有已加载资源是否都允许关闭；任一资源拒绝关闭则返回 false 并回填原因。
    bool canClose (QString* reason = nullptr) const;
    // 按依赖逆序关闭全部已加载资源，并清空加载列表。
    void closeResources ();

    // 是否有任何已加载资源处于脏状态；dirtyResourceIds 返回脏资源 ID 列表。
    bool isDirty () const;
    QStringList dirtyResourceIds () const;

    // 按资源 kind 查找负责的 Provider；未注册时返回 nullptr。
    ProjectDocumentProvider* providerForKind (const QString& kind) const;

  private:
    // 一个已加载资源的记录：资源定义、负责的 Provider 与解析后的磁盘路径。
    struct LoadedResource
    {
        ProjectResource resource;
        ProjectDocumentProvider* provider = nullptr;
        QString resolvedPath;
    };

    // 对清单全部资源做依赖拓扑排序（DFS），结果保证依赖先于依赖者；发现环返回 false。
    bool buildLoadOrder (const ProjectManifest& manifest,
                         QVector< ProjectResource >& ordered,
                         QString* error) const;

    // DFS 访问单个资源：marks 中 0=未访问、1=正在访问（再次遇到即环）、2=已完成。
    bool visitResource (const ProjectManifest& manifest,
                        const QString& resourceId,
                        QHash< QString, int >& marks,
                        QVector< ProjectResource >& ordered,
                        QString* error) const;

    QHash< QString, ProjectDocumentProvider* > _providersById;    // providerId → Provider。
    QHash< QString, ProjectDocumentProvider* > _providersByKind;  // kind → Provider。
    QVector< LoadedResource > _loaded;                            // 当前已加载的资源（按加载顺序）。
};

}    // namespace rws

#endif    // RWS_PROJECTDOCUMENTREGISTRY_HPP
