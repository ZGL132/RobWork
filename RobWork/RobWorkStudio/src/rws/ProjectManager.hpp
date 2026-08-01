#ifndef RWS_PROJECTMANAGER_HPP
#define RWS_PROJECTMANAGER_HPP

#include "ProjectManifest.hpp"

namespace rws {

class ProjectDocumentRegistry;

/**
 * @brief 第一阶段的项目生命周期管理器。
 *
 * 当前版本只管理项目清单本身，尚未把各插件文档 Provider 接入保存事务。先稳定
 * 项目文件格式、路径规则和主程序入口，再逐步迁移业务插件，可以降低改造风险。
 */
class ProjectManager
{
  public:
    // 项目完整性问题保持为结构化数据，界面可按类型分组显示、提供重新定位或清理入口，
    // 不需要再解析本地化错误文本来判断问题类别。
    struct IntegrityIssue
    {
        enum class Type { MissingResource, UnreferencedFile, ChangedSinceAutosave };
        Type type = Type::MissingResource;
        QString resourceId;
        QString path;
        QString message;
    };
    ProjectManager () = default;

    // 创建新项目：在磁盘上写入 .rwproj 清单文件，并让本管理器接管该项目的上下文。
    // 参数 projectFilePath 是项目文件的目标路径；manifest 是待持久化的清单，其中
    // 缺失的 id / createdAt 等字段会被自动补全；error 可选，失败时回填错误描述。
    bool createProject (const QString& projectFilePath,
                        const ProjectManifest& manifest,
                        QString* error = nullptr);

    // 从已有 WorkCell 创建可自包含的项目：把源 XML 复制到目标项目的 scenes/main.wc.xml，
    // 再写入带 mainWorkCell 入口的清单。源文件始终只读；任一步骤失败时不会替换当前项目。
    bool createProjectFromWorkCell (const QString& projectFilePath,
                                    const QString& sourceWorkCellPath,
                                    QString* error = nullptr);

    // 打开既有项目：读取并校验 .rwproj 文件，预解析全部资源路径并检查 required
    // 资源是否真实存在；任一步失败都不会破坏当前已打开的项目上下文。
    bool openProject (const QString& projectFilePath, QString* error = nullptr);

    // 保存当前项目：把内存中的清单写回磁盘。没有打开任何项目时直接失败。
    bool saveProject (QString* error = nullptr);

    // 把项目外的历史 XML/JSON 文件导入当前项目。resource.path 必须是项目内相对路径，
    // ownership 为空时自动设为 project；复制成功后才更新内存清单并置脏，调用方随后通过
    // saveProject 持久化清单。导入不加载 Provider，避免半加载状态影响当前编辑会话。
    bool importResource (const QString& sourcePath,
                         const ProjectResource& resource,
                         QString* error = nullptr);

    // 将软件首次编辑时生成的资源登记到当前清单。与 importResource 不同，此接口不复制
    // 外部文件，也不要求目标文件已存在；调用者必须随后通过 ProjectDocumentRegistry 的
    // 保存事务写入该路径，避免生成空文件或绕过项目内路径校验。
    bool addGeneratedResource (const ProjectResource& resource, QString* error = nullptr);

    // 原子替换一个既有资源的描述，并把它新引用的被动资产加入清单。资源 ID 保持不变，
    // 因而 entryPoints 与其它插件依赖无需重写；全部候选项通过清单校验后才修改当前项目。
    bool replaceResourceAndAddAssets (const ProjectResource& resource,
                                      const QVector< ProjectResource >& assets,
                                      QString* error = nullptr);

    // 克隆当前项目到一个尚不存在的目标目录。只复制 ownership 为 project/generated 的文件，
    // external 资源继续保留其原始引用；新清单生成独立 project.id，并在所有文件落盘后才切换
    // 当前项目上下文，从而让“另存为”失败时仍能继续使用源项目。
    bool cloneProject (const QString& targetProjectFilePath, QString* error = nullptr);

    // 创建崩溃恢复快照：把当前清单及所有 project/generated 资源写入项目私有的
    // .rwproject/autosave 目录。external 资源不属于项目，既不能复制也不能在恢复时覆盖。
    bool createAutosaveSnapshot (QString* error = nullptr) const;
    bool createAutosaveSnapshot (ProjectDocumentRegistry& documents, QString* error = nullptr) const;
    // 查询是否存在一个可被恢复流程读取的完整快照清单。
    bool hasAutosaveSnapshot () const;
    // 用最后一次成功创建的恢复快照替换当前项目的清单及项目自有资源。
    bool restoreAutosaveSnapshot (QString* error = nullptr);
    // 用户成功保存或明确放弃恢复数据后删除项目私有恢复槽。
    bool discardAutosaveSnapshot (QString* error = nullptr) const;
    // 检查当前项目的资源可用性、目录孤儿文件，以及与最近恢复快照相比发生的字节变化。
    QVector< IntegrityIssue > inspectIntegrity (QString* error = nullptr) const;
    bool removeUnreferencedFiles (const QStringList& paths, QString* error = nullptr);
    bool relocateResource (const QString& resourceId,
                           const QString& replacementPath,
                           QString* error = nullptr);

    // 将当前项目的清单和自有资源导出为标准 ZIP 容器 rwpack；external 资源不会被带入。
    bool exportPackage (const QString& packageFilePath, QString* error = nullptr) const;
    // 解包到一个此前不存在的目录，并返回内部 project.rwproj 的绝对路径；不自动切换当前项目。
    static bool extractPackage (const QString& packageFilePath,
                                const QString& targetDirectory,
                                QString& projectFilePath,
                                QString* error = nullptr);

    // 关闭当前项目：清空项目文件路径与内存清单，并把脏标记复位。
    // 注意：本方法不会静默保存，丢弃未保存修改的决策权在上层 UI。
    void closeProject ();

    // 状态查询：当前是否持有项目；清单是否被改动过（脏标记）。
    bool hasProject () const { return !_projectFilePath.isEmpty (); }
    bool isDirty () const { return _dirty; }
    // 置脏/清脏：后续阶段会由插件文档注册表在业务文档变化时调用。
    void markDirty (bool dirty = true) { _dirty = dirty; }

    // 访问器：当前项目文件路径与内存中的清单（只读引用）。
    QString projectFilePath () const { return _projectFilePath; }
    const ProjectManifest& manifest () const { return _manifest; }

    // 按稳定资源 ID 解析资源在磁盘上的实际路径。
    // 内部委托 ProjectPathResolver 执行项目目录边界检查，越界路径会失败。
    bool resolveResource (const QString& resourceId,
                          QString& resolvedPath,
                          QString* error = nullptr) const;

  private:
    bool createAutosaveSnapshot (ProjectDocumentRegistry* documents, QString* error) const;
    // 把清单以原子方式（QSaveFile）写入指定项目文件，避免写入中断留下半截文件。
    bool writeManifest (const QString& projectFilePath,
                        const ProjectManifest& manifest,
                        QString* error) const;

    QString _projectFilePath;    // 当前项目文件绝对路径；为空表示未打开项目。
    ProjectManifest _manifest;   // 当前项目的内存清单（内存模型）。
    bool _dirty = false;         // 脏标记：清单在保存后又被改动则为 true。
};

}    // namespace rws

#endif    // RWS_PROJECTMANAGER_HPP
