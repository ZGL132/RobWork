#ifndef RWS_STRUCTUREOPTIMIZER_STRUCTUREOPTIMIZERPLUGIN_HPP
#define RWS_STRUCTUREOPTIMIZER_STRUCTUREOPTIMIZERPLUGIN_HPP

#include <QObject>
#include <rws/RobWorkStudioPlugin.hpp>

namespace rws {

class StructureOptimizerWidget;

//! @brief StructureOptimizer 插件入口。
//!
//! 扩展 RobWorkStudioPlugin, 通过 Qt 插件系统加载。
class StructureOptimizerPlugin : public rws::RobWorkStudioPlugin
{
    Q_OBJECT
#ifndef RWS_USE_STATIC_LINK_PLUGINS
    Q_PLUGIN_METADATA(IID "dk.sdu.mip.Robwork.RobWorkStudioPlugin/0.1" FILE "plugin.json")
    Q_INTERFACES(rws::RobWorkStudioPlugin)
#endif

public:
    StructureOptimizerPlugin();
    ~StructureOptimizerPlugin() override;

    void open(rw::models::WorkCell* workcell) override;
    void close() override;
    void initialize() override;

    /// @brief 从文件路径加载场景
    void loadSceneFile(const QString& filename);

private:
    StructureOptimizerWidget* _widget;
};

} // namespace rws

#endif // RWS_STRUCTUREOPTIMIZER_STRUCTUREOPTIMIZERPLUGIN_HPP
