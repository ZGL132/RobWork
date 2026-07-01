#include "RobotModelBuilderPlugin.hpp"

#include "RobotModelBuilderWidget.hpp"

#include <rws/RobWorkStudio.hpp>

using namespace rws;

RobotModelBuilderPlugin::RobotModelBuilderPlugin () :
    RobWorkStudioPlugin ("RobotModelBuilder", QIcon ()), _widget (NULL)
{}

RobotModelBuilderPlugin::~RobotModelBuilderPlugin ()
{}

void RobotModelBuilderPlugin::initialize ()
{
    _widget = new RobotModelBuilderWidget (this);
    connect (_widget, SIGNAL (loadSceneRequested (const QString&)), this,
             SLOT (loadSceneFile (const QString&)));
    setWidget (_widget);
}

void RobotModelBuilderPlugin::open (rw::models::WorkCell* workcell)
{}

void RobotModelBuilderPlugin::close ()
{}

void RobotModelBuilderPlugin::loadSceneFile (const QString& filename)
{
    if (getRobWorkStudio () != NULL)
        getRobWorkStudio ()->setWorkcell (filename.toStdString ());
}
