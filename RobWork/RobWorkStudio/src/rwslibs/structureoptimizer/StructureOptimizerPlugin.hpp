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
    Q_PLUGIN_METADATA(IID "dk.sdu.mip.Robwork.RobWorkStudioPlugin/0.1" FILE "plugin.json")
    Q_INTERFACES(rws::RobWorkStudioPlugin)

public:
    StructureOptimizerPlugin();
    ~StructureOptimizerPlugin() override;

    void open(rw::models::WorkCell* workcell) override;
    void close() override;
    void initialize() override;

private:
    StructureOptimizerWidget* _widget;
};

} // namespace rws

#endif // RWS_STRUCTUREOPTIMIZER_STRUCTUREOPTIMIZERPLUGIN_HPP
