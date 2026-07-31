#ifndef RWS_PROJECTMANIFEST_HPP
#define RWS_PROJECTMANIFEST_HPP

#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

namespace rws {

/**
 * @brief 项目资源的清单描述。
 *
 * 该结构只描述“资源是什么、放在哪里以及依赖谁”，不持有 WorkCell、机器人模型
 * 或插件业务对象本身。这样可以让项目清单保持轻量，并继续复用现有 XML/JSON 加载器。
 */
struct ProjectResource
{
    QString id;              // 稳定资源 ID，跨会话、跨机器保持唯一，供依赖和入口引用。
    QString kind;            // 资源类型标识，例如 "robwork.workcell"；后续用于选择加载器。
    QString path;            // 资源路径；语义由 ownership 决定（详见 ProjectPathResolver）。
    QString ownership;       // 归属类型："project" | "generated" | "external"（见 validate）。
    bool required = false;   // required=true 表示该资源缺失时项目无法正常工作，打开会失败。
    QStringList dependencies;    // 本资源依赖的其它资源 ID 列表，用于关系完整性校验。
};

/**
 * @brief 项目基本信息。
 *
 * id 是项目克隆和审计时使用的稳定标识；name 只是显示名称，允许用户修改。
 * 时间和创建程序信息属于可选元数据，不参与资源加载决策。
 */
struct ProjectMetadata
{
    QString id;                 // 项目稳定标识（UUID）；克隆/审计时用于区分不同项目实例。
    QString name;               // 显示名称，仅用于界面展示，允许用户随意修改。
    QString description;        // 项目说明文本（可选）。
    QString createdAt;          // 创建时间（UTC ISO-8601），创建时自动生成。
    QString modifiedAt;         // 最近修改时间（UTC ISO-8601），每次保存时更新。
    QString application;        // 创建项目的应用程序名称（默认 "RobWorkStudio"）。
    QString applicationVersion; // 创建项目的应用程序版本号（可选）。
};

/**
 * @brief RobWorkStudio 项目清单的内存模型。
 *
 * 第一阶段只定义清单层，不把插件文档状态混入这里。后续阶段会由
 * ProjectDocumentRegistry 负责把本模型中的资源绑定到具体插件文档。
 */
struct ProjectManifest
{
    enum { CurrentSchemaVersion = 1 };

    QString format = QStringLiteral ("RobWorkStudioProject");    // 文件格式标识，防误判其它 JSON。
    int schemaVersion = CurrentSchemaVersion;                    // 清单结构版本号，用于向后兼容迁移。
    ProjectMetadata project;                                     // 项目基本信息（见 ProjectMetadata）。
    // 入口资源映射：语义键（如 "mainWorkCell"）→ 资源 ID。打开项目时按入口自动加载。
    QMap< QString, QString > entryPoints;
    QVector< ProjectResource > resources;    // 项目内全部资源定义列表。
    QJsonObject plugins;    // 插件专属配置（原样透传的扩展点，本阶段不解释其内容）。
    QJsonObject settings;   // 项目级设置项（原样透传的扩展点，本阶段不解释其内容）。

    /**
     * @brief 根据稳定资源 ID 查找资源。
     *
     * 通过 out 返回副本，避免暴露 QVector 内部元素指针，并防止后续插入资源时
     * 出现悬空引用。
     */
    bool findResource (const QString& resourceId, ProjectResource& out) const;
};

}    // namespace rws

#endif    // RWS_PROJECTMANIFEST_HPP
