#ifndef RWS_PROJECTPACKAGE_HPP
#define RWS_PROJECTPACKAGE_HPP

#include "ProjectManifest.hpp"

namespace rws {

/**
 * @brief 标准 rwpack（ZIP）归档的安全读写边界。
 *
 * 清单仍使用原始 rwproj JSON，归档只负责传输 project/generated 资源；所有解包条目必须
 * 是项目根目录内的相对普通文件，拒绝绝对路径和目录穿越，避免恶意 ZIP 覆盖用户文件。
 */
class ProjectPackage
{
  public:
    static bool create (const QString& projectFilePath,
                        const ProjectManifest& manifest,
                        const QString& packageFilePath,
                        QString* error = nullptr);
    static bool extract (const QString& packageFilePath,
                         const QString& targetDirectory,
                         QString& projectFilePath,
                         QString* error = nullptr);
};

}    // namespace rws

#endif    // RWS_PROJECTPACKAGE_HPP
