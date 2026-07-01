#include "RobotModelBuilderPlugin.hpp"

#include <rws/RobWorkStudio.hpp>

#include <QLabel>
#include <QVBoxLayout>

using namespace rws;

RobotModelBuilderPlugin::RobotModelBuilderPlugin () :
    RobWorkStudioPlugin ("RobotModelBuilder", QIcon ()), _widget (NULL)
{}

RobotModelBuilderPlugin::~RobotModelBuilderPlugin ()
{}

void RobotModelBuilderPlugin::initialize ()
{
    QWidget* widget  = new QWidget (this);
    QVBoxLayout* lay = new QVBoxLayout (widget);
    lay->addWidget (new QLabel ("RobotModelBuilder"));
    widget->setLayout (lay);
    setWidget (widget);
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
