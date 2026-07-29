#include "EngineeringRequirementsPlugin.hpp"
#include "GeometryFeatureResolver.hpp"
#include "EngineeringRequirementsWidget.hpp"

#include <rws/RobWorkStudio.hpp>
#include <rws/RWStudioView3D.hpp>

#include <rw/graphics/DrawableNode.hpp>
#include <rw/graphics/WorkCellScene.hpp>
#include <rw/kinematics/Frame.hpp>
#include <rw/kinematics/Kinematics.hpp>
#include <rw/math/Constants.hpp>
#include <rw/math/RPY.hpp>
#include <rw/models/WorkCell.hpp>

#include <QTimer>

namespace {

const int stationMarkerMask = rw::graphics::DrawableNode::Virtual |
                              rw::graphics::DrawableNode::DrawableObject;

void requestSceneRedraw(rws::RobWorkStudio* studio)
{
    if (studio != nullptr && !studio->getView().isNull())
        studio->getView()->update();
}

rw::math::Transform3D<> stationTransform(const rws::PoseTask& station,
                                         const rw::models::WorkCell& workcell,
                                         const rw::kinematics::State& state, bool* valid)
{
    std::array<double, 3> position = station.position;
    std::array<double, 3> rpyDeg = station.rpyDeg;
    if (station.source == rws::PoseTaskSource::GeometryFeature) {
        rws::GeometryFeatureResolution resolution;
        if (!rws::GeometryFeatureResolver::resolve(station.geometryFeature, station.refFrame,
                                                   workcell, state, resolution, nullptr)) {
            if (valid != nullptr) *valid = false;
            return rw::math::Transform3D<>::identity();
        }
        position = resolution.position;
        rpyDeg = resolution.rpyDeg;
    }
    rw::kinematics::Frame* reference = station.refFrame.empty() || station.refFrame == "WORLD" ?
        workcell.getWorldFrame() : workcell.findFrame(station.refFrame);
    if (reference == nullptr) {
        if (valid != nullptr) *valid = false;
        return rw::math::Transform3D<>::identity();
    }
    if (valid != nullptr) *valid = true;
    const rw::math::Transform3D<> worldTreference = rw::kinematics::Kinematics::worldTframe(reference, state);
    const double degToRad = rw::math::Pi / 180.0;
    return worldTreference * rw::math::Transform3D<>(
        rw::math::Vector3D<>(position[0], position[1], position[2]),
        rw::math::RPY<>(rpyDeg[0] * degToRad, rpyDeg[1] * degToRad, rpyDeg[2] * degToRad));
}

} // namespace

namespace rws {
EngineeringRequirementsPlugin::EngineeringRequirementsPlugin() :
    RobWorkStudioPlugin("EngineeringRequirements", QIcon())
{}
EngineeringRequirementsPlugin::~EngineeringRequirementsPlugin()
{
    clearStationMarkers();
    if (getRobWorkStudio() != nullptr) {
        getRobWorkStudio()->frameSelectedEvent().remove(this);
        getRobWorkStudio()->stateChangedEvent().remove(this);
    }
}
void EngineeringRequirementsPlugin::initialize() {
    _widget = new EngineeringRequirementsWidget(this);
    setWidget(_widget);
    connect(_widget, &EngineeringRequirementsWidget::geometryFeaturePickRequested,
            this, &EngineeringRequirementsPlugin::beginGeometryFeaturePick);
    connect(_widget, &EngineeringRequirementsWidget::requirementsChanged,
            this, &EngineeringRequirementsPlugin::scheduleStationMarkerRefresh);
    if (getRobWorkStudio() != nullptr) {
        getRobWorkStudio()->frameSelectedEvent().add(
            [this](rw::kinematics::Frame* frame) { handleFrameSelected(frame); }, this);
        getRobWorkStudio()->stateChangedEvent().add(
            [this](const rw::kinematics::State& state) { handleStateChanged(state); }, this);
    }
}
void EngineeringRequirementsPlugin::open(rw::models::WorkCell* workcell) {
    if (_widget != nullptr) _widget->setWorkCell(workcell);
    refreshStationMarkers();
}
void EngineeringRequirementsPlugin::close() {
    clearStationMarkers();
    _geometryFeaturePickActive = false;
    if (_widget != nullptr) _widget->setWorkCell(nullptr);
}

void EngineeringRequirementsPlugin::beginGeometryFeaturePick()
{
    _geometryFeaturePickActive = true;
}

void EngineeringRequirementsPlugin::handleFrameSelected(rw::kinematics::Frame* frame)
{
    if (!_geometryFeaturePickActive || _widget == nullptr || frame == nullptr) return;
    _geometryFeaturePickActive = false;
    _widget->applyGeometryFeatureFrame(QString::fromStdString(frame->getName()));
}

void EngineeringRequirementsPlugin::handleStateChanged(const rw::kinematics::State& state)
{
    updateStationMarkers(state);
}

void EngineeringRequirementsPlugin::scheduleStationMarkerRefresh()
{
    if (_markerRefreshPending) return;
    _markerRefreshPending = true;
    QTimer::singleShot(0, this, [this] {
        _markerRefreshPending = false;
        refreshStationMarkers();
    });
}

void EngineeringRequirementsPlugin::clearStationMarkers()
{
    RobWorkStudio* studio = getRobWorkStudio();
    if (studio != nullptr && !studio->getWorkCellScene().isNull()) {
        for (const auto& item : _stationAxes) studio->getWorkCellScene()->removeDrawable(item.second);
        for (const auto& item : _stationLabels) studio->getWorkCellScene()->removeDrawable(item.second);
    }
    _stationAxes.clear();
    _stationLabels.clear();
    requestSceneRedraw(studio);
}

void EngineeringRequirementsPlugin::refreshStationMarkers()
{
    _markerRefreshPending = false;
    clearStationMarkers();
    RobWorkStudio* studio = getRobWorkStudio();
    if (studio == nullptr || _widget == nullptr || studio->getWorkCell().isNull() ||
        studio->getWorkCellScene().isNull())
        return;
    const RequirementSet requirements = _widget->requirementSet();
    rw::graphics::WorkCellScene::Ptr scene = studio->getWorkCellScene();
    for (const PoseTask& station : requirements.poseTasks) {
        if (station.level == RequirementLevel::Info || station.id.empty()) continue;
        const std::string name = "EngineeringRequirement." + station.id;
        rw::graphics::DrawableNode::Ptr axis = scene->addFrameAxis(
            name, 0.05, studio->getWorkCell()->getWorldFrame(), stationMarkerMask);
        _stationAxes[station.id] = axis;
        const std::string label = station.name.empty() ? station.id : station.name;
        _stationLabels[station.id] = scene->addText(name + ".label", label,
                                                     studio->getWorkCell()->getWorldFrame(),
                                                     stationMarkerMask);
    }
    updateStationMarkers(studio->getState());
    requestSceneRedraw(studio);
}

void EngineeringRequirementsPlugin::updateStationMarkers(const rw::kinematics::State& state)
{
    RobWorkStudio* studio = getRobWorkStudio();
    if (studio == nullptr || _widget == nullptr || studio->getWorkCell().isNull()) return;
    const RequirementSet requirements = _widget->requirementSet();
    for (const PoseTask& station : requirements.poseTasks) {
        const auto axis = _stationAxes.find(station.id);
        if (axis == _stationAxes.end()) continue;
        bool valid = false;
        const rw::math::Transform3D<> worldTstation = stationTransform(
            station, *studio->getWorkCell(), state, &valid);
        axis->second->setVisible(valid);
        axis->second->setTransform(worldTstation);
        const auto label = _stationLabels.find(station.id);
        if (label != _stationLabels.end()) {
            label->second->setVisible(valid);
            label->second->setTransform(worldTstation * rw::math::Transform3D<>(
                rw::math::Vector3D<>(0.0, 0.0, 0.04)));
        }
    }
    requestSceneRedraw(studio);
}
} // namespace rws
