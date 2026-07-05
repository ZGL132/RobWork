// =============================================================================
//  RobotModelUrdfImporter.hpp
//  说明: URDF(.urdf / .xml) → RobotModelSpec 导入器骨架。
//        本类只把 URDF 解析成已有的 RobotModelSpec,后续编辑、保存、预览
//        全部走 RobotModelBuilderWidget 与 RobotModelXmlWriter。
//        设计原则(参见 2026-07-05-robot-model-builder-urdf-import.md):
//          * 不新建插件,在 sdurws_robotmodelbuilder 内部实现;
//          * URDF 是输入格式,transformJoints 仍为唯一真值;
//          * DH 仅作为投影视图:导入后 refreshDhProjectionFromTransform;
//          * 第一个实现只支持单条串联链与 fixed frame;
//          * 不引入 ROS 依赖,package:// 路径用用户给定的根目录解析。
// =============================================================================
#ifndef RWS_ROBOTMODELBUILDER_ROBOTMODELURDFIMPORTER_HPP
#define RWS_ROBOTMODELBUILDER_ROBOTMODELURDFIMPORTER_HPP

#include "RobotModelSpec.hpp"

#include <QString>
#include <QStringList>

namespace rws {

/// 导入 URDF 时的可配置选项
struct UrdfImportOptions
{
    QString saveDirectory;                            // RobotModelSpec.saveDirectory
    QStringList packageRoots;                         // package:// 解析的根目录候选
    bool generateScene = true;                        // 是否生成场景 WorkCell
    bool generateDrawables = true;                    // 是否输出 Drawable / 默认几何
    bool generateDynamicWorkCell = true;              // 是否生成 DWC
};

/// 导入结果:已填充好的 RobotModelSpec + 过程警告
struct UrdfImportResult
{
    RobotModelSpec spec;
    QStringList warnings;
};

/// URDF → RobotModelSpec 静态工具类
class RobotModelUrdfImporter
{
  public:
    /// 从 URDF 文件导入到 RobotModelSpec。
    /// 成功返回 true,失败把解析/校验错误写进 errors;
    /// 任何过程中(链顺序、非默认轴、不可解析的 package:// 等)的非致命
    /// 警告走 result.warnings,不阻塞导入。
    static bool importFile (const QString& urdfPath,
                            const UrdfImportOptions& options,
                            UrdfImportResult& result,
                            QStringList& errors);
};

}    // namespace rws

#endif
