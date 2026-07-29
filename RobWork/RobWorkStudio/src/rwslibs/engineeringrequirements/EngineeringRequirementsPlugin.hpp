#ifndef RWS_ENGINEERINGREQUIREMENTS_ENGINEERINGREQUIREMENTSPLUGIN_HPP
#define RWS_ENGINEERINGREQUIREMENTS_ENGINEERINGREQUIREMENTSPLUGIN_HPP

#include <rws/RobWorkStudioPlugin.hpp>

#include <map>

namespace rw { namespace graphics { class DrawableNode; } }
namespace rw { namespace kinematics { class Frame; class State; } }

namespace rws {
class EngineeringRequirementsWidget;

class EngineeringRequirementsPlugin : public RobWorkStudioPlugin {
    Q_OBJECT
#ifndef RWS_USE_STATIC_LINK_PLUGINS
    Q_PLUGIN_METADATA(IID "dk.sdu.mip.Robwork.RobWorkStudioPlugin/0.1" FILE "plugin.json")
    Q_INTERFACES(rws::RobWorkStudioPlugin)
#endif
public:
    EngineeringRequirementsPlugin();
    ~EngineeringRequirementsPlugin() override;
    void initialize() override;
    void open(rw::models::WorkCell* workcell) override;
    void close() override;
private:
    void beginGeometryFeaturePick();
    void handleFrameSelected(rw::kinematics::Frame* frame);
    void handleStateChanged(const rw::kinematics::State& state);
    void scheduleStationMarkerRefresh();
    void refreshStationMarkers();
    void updateStationMarkers(const rw::kinematics::State& state);
    void clearStationMarkers();

    EngineeringRequirementsWidget* _widget = nullptr;
    bool _geometryFeaturePickActive = false;
    bool _markerRefreshPending = false;
    std::map<std::string, rw::core::Ptr<rw::graphics::DrawableNode> > _stationAxes;
    std::map<std::string, rw::core::Ptr<rw::graphics::DrawableNode> > _stationLabels;
};
} // namespace rws

#endif
