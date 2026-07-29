#include "EngineeringRequirementsPlugin.hpp"
#include "EngineeringRequirementsWidget.hpp"

namespace rws {
EngineeringRequirementsPlugin::EngineeringRequirementsPlugin() :
    RobWorkStudioPlugin("EngineeringRequirements", QIcon())
{}
EngineeringRequirementsPlugin::~EngineeringRequirementsPlugin() = default;
void EngineeringRequirementsPlugin::initialize() {
    _widget = new EngineeringRequirementsWidget(this);
    setWidget(_widget);
}
void EngineeringRequirementsPlugin::open(rw::models::WorkCell* workcell) {
    if (_widget != nullptr) _widget->setWorkCell(workcell);
}
void EngineeringRequirementsPlugin::close() {
    if (_widget != nullptr) _widget->setWorkCell(nullptr);
}
} // namespace rws
