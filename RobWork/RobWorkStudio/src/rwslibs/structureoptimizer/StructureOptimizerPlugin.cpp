#include "StructureOptimizerPlugin.hpp"
#include "StructureOptimizerWidget.hpp"

#include <rws/RobWorkStudio.hpp>
#include <rw/loaders/rwxml/XMLRWLoader.hpp>

namespace rws {

StructureOptimizerPlugin::StructureOptimizerPlugin() :
    RobWorkStudioPlugin("StructureOptimizer", QIcon(":/structureoptimizer/structureoptimizer_icon.png"))
{
    _widget = new StructureOptimizerWidget();
    setWidget(_widget);
}

StructureOptimizerPlugin::~StructureOptimizerPlugin()
{
}

void StructureOptimizerPlugin::initialize()
{
    _widget->setPreviewHost(this);
}

void StructureOptimizerPlugin::open(rw::models::WorkCell* workcell)
{
    (void)workcell;
}

void StructureOptimizerPlugin::close()
{
    _widget->setPreviewHost(nullptr);
}

void StructureOptimizerPlugin::loadSceneFile(const QString& filename)
{
    if (getRobWorkStudio() != NULL)
        getRobWorkStudio()->setWorkcell(filename.toStdString());
}

QString StructureOptimizerPlugin::currentWorkCellPath()
{
    RobWorkStudio* studio = getRobWorkStudio();
    if (studio == nullptr || studio->getWorkCell().isNull())
        return QString();
    try {
        const std::string path = static_cast<std::string>(
            studio->getWorkCell()->getPropertyMap().get<std::string>(
                rw::loaders::XMLRWLoader::getWorkCellFileNameId()));
        return QString::fromStdString(path);
    } catch (...) {
        return QString();
    }
}

bool StructureOptimizerPlugin::openWorkCell(const QString& path, QString* error)
{
    RobWorkStudio* studio = getRobWorkStudio();
    if (studio == nullptr || path.isEmpty()) {
        if (error != nullptr)
            *error = "RobWorkStudio is unavailable.";
        return false;
    }
    try {
        studio->setWorkcell(path.toStdString());
        return true;
    } catch (const std::exception& exception) {
        if (error != nullptr)
            *error = QString::fromStdString(exception.what());
        return false;
    }
}

} // namespace rws
