#ifndef RWS_PROJECTSAVETRANSACTION_HPP
#define RWS_PROJECTSAVETRANSACTION_HPP

#include "ProjectDocumentProvider.hpp"

#include <QVector>

namespace rws {

/**
 * @brief 多资源保存事务。
 *
 * 事务分为两个明确阶段：stage 只调用 Provider 写入同目录暂存文件，commit 才把
 * 暂存文件替换为正式文件。若任一资源暂存失败，正式文件不会被触碰；若替换阶段
 * 失败，则利用唯一备份文件回滚已经替换的资源。
 */
class ProjectSaveTransaction
{
  public:
    ProjectSaveTransaction () = default;
    // 析构时若尚未提交，自动执行 rollback，防止异常路径留下暂存或半替换文件。
    ~ProjectSaveTransaction ();

    // 暂存阶段：调用 Provider 把文档内容写入同目录的临时暂存文件，不触碰正式文件。
    // 返回 false 时本次暂存失败，事务保持未提交，稍后可由调用方直接销毁以回滚。
    bool stage (ProjectDocumentProvider& provider,
                const ProjectResource& resource,
                const ProjectDocumentContext& context,
                const QString& targetPath,
                QString* error = nullptr);

    // 提交阶段：把全部已暂存文件依次替换为正式文件；任一替换失败则整体回滚，
    // 全部成功后删除备份并清除各 Provider 的脏状态。
    bool commit (QString* error = nullptr);
    // 手动回滚：逆序恢复已安装目标、还原备份、清理暂存文件。
    void rollback ();

  private:
    // 一个已暂存资源的完整状态，用于提交替换与失败回滚。
    struct StagedResource
    {
        ProjectDocumentProvider* provider = nullptr;   // 负责保存该资源的 Provider。
        ProjectResource resource;                     // 资源定义（含 ID、路径等）。
        QString targetPath;     // 正式目标文件绝对路径。
        QString stagedPath;     // 暂存文件路径（Provider 实际写入的位置）。
        QString backupPath;     // 备份文件路径（替换前原文件被移动到这里）。
        bool targetBackedUp = false;    // 原目标文件是否已备份（可能原本不存在）。
        bool targetInstalled = false;   // 暂存文件是否已安装为正式文件。
    };

    QVector< StagedResource > _staged;    // 已暂存资源列表（按暂存顺序）。
    bool _committed = false;              // 是否已完成提交；析构据此决定是否回滚。
};

}    // namespace rws

#endif    // RWS_PROJECTSAVETRANSACTION_HPP
