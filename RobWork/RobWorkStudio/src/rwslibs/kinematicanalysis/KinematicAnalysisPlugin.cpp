#include "KinematicAnalysisPlugin.hpp"

#include "KinematicAnalysisWidget.hpp"

#include <rws/RobWorkStudio.hpp>

using namespace rws;

KinematicAnalysisPlugin::KinematicAnalysisPlugin() :
    RobWorkStudioPlugin("KinematicAnalysis", QIcon()),
    _widget(NULL)
{
}

KinematicAnalysisPlugin::~KinematicAnalysisPlugin()
{
}

void KinematicAnalysisPlugin::initialize()
{
    _widget = new KinematicAnalysisWidget(this);
    setWidget(_widget);
    _widget->setRobWorkStudio(getRobWorkStudio());
}

void KinematicAnalysisPlugin::open(rw::models::WorkCell* workcell)
{
    if (_widget != NULL)
        _widget->setWorkCell(workcell);
}

void KinematicAnalysisPlugin::close()
{
    if (_widget != NULL)
        _widget->setWorkCell(NULL);
}
