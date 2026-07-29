#include "GeometryFeatureResolver.hpp"

#include <rw/kinematics/Kinematics.hpp>
#include <rw/math/Constants.hpp>
#include <rw/math/RPY.hpp>
#include <rw/models/WorkCell.hpp>

namespace rws {
namespace {

rw::kinematics::Frame* findReferenceFrame(const rw::models::WorkCell& workcell, const std::string& name)
{
    if (name.empty() || name == "WORLD") return workcell.getWorldFrame();
    return workcell.findFrame(name);
}

} // namespace

bool GeometryFeatureResolver::resolve(const GeometryFeatureReference& feature, const std::string& referenceFrame,
                                      const rw::models::WorkCell& workcell, const rw::kinematics::State& state,
                                      GeometryFeatureResolution& resolution, std::string* error)
{
    if (feature.type == GeometryFeatureType::None || feature.frameName.empty()) {
        if (error != nullptr) *error = "A geometry feature frame is required.";
        return false;
    }
    rw::kinematics::Frame* reference = findReferenceFrame(workcell, referenceFrame);
    rw::kinematics::Frame* source = workcell.findFrame(feature.frameName);
    if (reference == nullptr || source == nullptr) {
        if (error != nullptr) *error = "The geometry feature or reference frame is no longer available in the WorkCell.";
        return false;
    }
    const rw::math::Transform3D<> referenceTfeature =
        rw::kinematics::Kinematics::frameTframe(reference, source, state);
    const rw::math::RPY<> rpy(referenceTfeature.R());
    for (int axis = 0; axis < 3; ++axis) {
        resolution.position[axis] = referenceTfeature.P()[axis];
        resolution.rpyDeg[axis] = rpy[axis] * 180.0 / rw::math::Pi;
    }
    if (error != nullptr) error->clear();
    return true;
}

bool GeometryFeatureResolver::applyToStation(const GeometryFeatureReference& feature,
                                             const rw::models::WorkCell& workcell,
                                             const rw::kinematics::State& state, KeyStation& station,
                                             std::string* error)
{
    GeometryFeatureResolution resolution;
    if (!resolve(feature, station.refFrame, workcell, state, resolution, error)) return false;
    station.geometryFeature = feature;
    station.source = PoseTaskSource::GeometryFeature;
    station.position = resolution.position;
    station.rpyDeg = resolution.rpyDeg;
    station.orientation.targetFrame = feature.frameName;
    station.orientation.targetGeometry = "frame:" + feature.frameName;
    station.orientation.mode = feature.type == GeometryFeatureType::FramePlaneNormal ?
        OrientationMode::AlignGeometryNormal : OrientationMode::AlignFrame;
    if (error != nullptr) error->clear();
    return true;
}

} // namespace rws
