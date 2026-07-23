#include "StructureOptimizerPlugin.hpp"
#include "StructureOptimizerWidget.hpp"

#include <rws/RobWorkStudio.hpp>

namespace rws {

StructureOptimizerPlugin::StructureOptimizerPlugin() :
    RobWorkStudioPlugin("StructureOptimizer", QIcon())
{
    _widget = new StructureOptimizerWidget();
    setWidget(_widget);
}

StructureOptimizerPlugin::~StructureOptimizerPlugin()
{
}

void StructureOptimizerPlugin::initialize()
{
    // Widget 已在构造函数中创建,此处无需额外初始化
}

void StructureOptimizerPlugin::open(rw::models::WorkCell* workcell)
{
    (void)workcell;
}

void StructureOptimizerPlugin::close()
{
}

void StructureOptimizerPlugin::loadSceneFile(const QString& filename)
{
    if (getRobWorkStudio() != NULL)
        getRobWorkStudio()->setWorkcell(filename.toStdString());
}

} // namespace rws
