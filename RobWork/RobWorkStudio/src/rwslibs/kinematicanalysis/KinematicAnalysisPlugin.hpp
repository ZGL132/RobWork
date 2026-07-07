#ifndef RWS_KINEMATICANALYSIS_KINEMATICANALYSISPLUGIN_HPP
#define RWS_KINEMATICANALYSIS_KINEMATICANALYSISPLUGIN_HPP

#include <rws/RobWorkStudioPlugin.hpp>

namespace rws {

class KinematicAnalysisWidget;

class KinematicAnalysisPlugin : public RobWorkStudioPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "dk.sdu.mip.Robwork.RobWorkStudioPlugin/0.1" FILE "plugin.json")
    Q_INTERFACES(rws::RobWorkStudioPlugin)

public:
    KinematicAnalysisPlugin();
    ~KinematicAnalysisPlugin() override;

    void open(rw::models::WorkCell* workcell) override;
    void close() override;
    void initialize() override;

private:
    KinematicAnalysisWidget* _widget;
};

}    // namespace rws

#endif
