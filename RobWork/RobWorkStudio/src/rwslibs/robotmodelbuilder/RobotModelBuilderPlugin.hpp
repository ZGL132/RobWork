#ifndef RWS_ROBOTMODELBUILDER_PLUGIN_HPP
#define RWS_ROBOTMODELBUILDER_PLUGIN_HPP

#include <rws/RobWorkStudioPlugin.hpp>

namespace rws {

class RobotModelBuilderWidget;

class RobotModelBuilderPlugin : public RobWorkStudioPlugin
{
    Q_OBJECT
#ifndef RWS_USE_STATIC_LINK_PLUGINS
    Q_INTERFACES (rws::RobWorkStudioPlugin)
    Q_PLUGIN_METADATA (IID "dk.sdu.mip.Robwork.RobWorkStudioPlugin/0.1" FILE "plugin.json")
#endif
  public:
    RobotModelBuilderPlugin ();
    virtual ~RobotModelBuilderPlugin ();

    void initialize ();
    void open (rw::models::WorkCell* workcell);
    void close ();

  private Q_SLOTS:
    void loadSceneFile (const QString& filename);

  private:
    RobotModelBuilderWidget* _widget;
};

}    // namespace rws

#endif
