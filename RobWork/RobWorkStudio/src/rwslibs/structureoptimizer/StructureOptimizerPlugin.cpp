#include "StructureOptimizerPlugin.hpp"
#include "StructureOptimizerWidget.hpp"

#include <rws/RobWorkStudio.hpp>

namespace rws {

StructureOptimizerPlugin::StructureOptimizerPlugin() :
    RobWorkStudioPlugin("StructureOptimizer", QIcon()),
    _widget(nullptr)
{
}

StructureOptimizerPlugin::~StructureOptimizerPlugin()
{
}

void StructureOptimizerPlugin::initialize()
{
    _widget = new StructureOptimizerWidget(this);
    setWidget(_widget);
}

void StructureOptimizerPlugin::open(rw::models::WorkCell* workcell)
{
    (void)workcell;
    // 后续 Task 实现 WorkCell 内容加载
}

void StructureOptimizerPlugin::close()
{
    // 后续 Task 实现清理
}

} // namespace rws
