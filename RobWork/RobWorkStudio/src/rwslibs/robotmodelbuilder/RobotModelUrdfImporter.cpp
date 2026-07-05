// =============================================================================
//  RobotModelUrdfImporter.cpp
//  说明: 本里程碑只放"未实现"骨架,把 importFile 接口接入构建,
//        后续 Milestone Tasks 2-7 在此文件内逐步扩展 parser /
//        chain 排序 / 几何 / 惯量 / 路径解析 / validate 等逻辑。
//        即使未实现,errors 也会清晰告知调用方,UI 不会看似成功。
// =============================================================================
#include "RobotModelUrdfImporter.hpp"

using namespace rws;

bool RobotModelUrdfImporter::importFile (const QString& urdfPath,
                                         const UrdfImportOptions& options,
                                         UrdfImportResult& result,
                                         QStringList& errors)
{
    Q_UNUSED (urdfPath);
    Q_UNUSED (options);
    result = UrdfImportResult ();
    errors << "URDF import parser has not been implemented yet.";
    return false;
}
