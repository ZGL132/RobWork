#ifndef RWS_ENGINEERINGREQUIREMENTS_ENGINEERINGREQUIREMENTSPLUGIN_HPP
#define RWS_ENGINEERINGREQUIREMENTS_ENGINEERINGREQUIREMENTSPLUGIN_HPP

#include <rws/RobWorkStudioPlugin.hpp>

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
    EngineeringRequirementsWidget* _widget = nullptr;
};
} // namespace rws

#endif
