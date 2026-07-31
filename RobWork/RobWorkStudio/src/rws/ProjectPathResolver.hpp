#ifndef RWS_PROJECTPATHRESOLVER_HPP
#define RWS_PROJECTPATHRESOLVER_HPP

#include "ProjectManifest.hpp"

namespace rws {

/**
 * @brief 统一解析项目资源路径并执行项目目录边界检查。
 *
 * project/generated 资源只能使用相对于项目文件的路径；解析器拒绝绝对路径和
 * “..” 越出项目根目录的路径，避免项目文件意外引用或覆盖工程之外的文件。
 */
class ProjectPathResolver
{
  public:
    // 按资源的 ownership 分派解析：project/generated 走相对路径（强制项目内），
    // external 允许绝对路径、否则仍按项目目录解释。失败时经 error 回填原因。
    static bool resolveResource (const QString& projectFilePath,
                                 const ProjectResource& resource,
                                 QString& resolvedPath,
                                 QString* error = nullptr);

    // 把相对于项目文件的路径拼接为绝对路径，并做目录边界检查：
    // 拒绝绝对路径入参、拒绝 ".." 越出项目根目录的情况，最终结果规范化为绝对路径。
    static bool resolveProjectRelativePath (const QString& projectFilePath,
                                            const QString& relativePath,
                                            QString& resolvedPath,
                                            QString* error = nullptr);
};

}    // namespace rws

#endif    // RWS_PROJECTPATHRESOLVER_HPP
