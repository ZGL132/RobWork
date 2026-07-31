#ifndef RWS_PROJECTMANIFESTJSON_HPP
#define RWS_PROJECTMANIFESTJSON_HPP

#include "ProjectManifest.hpp"

#include <QByteArray>
#include <QJsonDocument>

namespace rws {

/**
 * @brief 项目清单 JSON 编解码和结构校验工具。
 *
 * JSON 层只判断清单字段是否完整、类型是否正确，以及 ID 和依赖关系是否自洽。
 * 路径是否越出项目目录由 ProjectPathResolver 单独处理，避免职责混杂。
 */
class ProjectManifestJson
{
  public:
    // 内存清单 → 有序 QJsonObject（序列化）。各字段按固定顺序输出，便于 diff 与审阅。
    static QJsonObject toObject (const ProjectManifest& manifest);

    // 内存清单 → JSON 文本字节串。默认输出带缩进的易读格式，便于手工检查项目文件。
    static QByteArray toJson (const ProjectManifest& manifest,
                              QJsonDocument::JsonFormat format = QJsonDocument::Indented);

    // QJsonObject → 内存清单（反序列化）。包含完整结构校验；失败时通过 error 回填原因。
    static bool fromObject (const QJsonObject& object,
                            ProjectManifest& manifest,
                            QString* error = nullptr);

    // JSON 文本 → 内存清单。先做语法解析，再委托 fromObject 做结构校验。
    static bool fromJson (const QByteArray& json,
                          ProjectManifest& manifest,
                          QString* error = nullptr);

    // 独立的结构校验入口：校验 format、schemaVersion、必填字段、资源 ID 唯一性、
    // ownership 取值，以及入口/依赖引用的资源是否真实存在。
    static bool validate (const ProjectManifest& manifest, QString* error = nullptr);
};

}    // namespace rws

#endif    // RWS_PROJECTMANIFESTJSON_HPP
