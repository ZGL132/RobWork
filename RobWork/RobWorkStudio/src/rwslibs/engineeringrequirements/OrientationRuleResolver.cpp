#include "OrientationRuleResolver.hpp"
#include <rw/kinematics/Kinematics.hpp>
#include <rw/math/Constants.hpp>
#include <rw/math/RPY.hpp>
#include <rw/math/Rotation3D.hpp>
#include <rw/math/Vector3D.hpp>
#include <rw/models/WorkCell.hpp>
#include <cmath>
namespace rws { namespace {
rw::kinematics::Frame* frame(const rw::models::WorkCell& wc, const std::string& name) {
    return name.empty() || name == "WORLD" ? wc.getWorldFrame() : wc.findFrame(name);
}
void storeRpy(const rw::math::Rotation3D<>& rotation, KeyStation& station) {
    const rw::math::RPY<> rpy(rotation);
    for (int axis = 0; axis < 3; ++axis) station.rpyDeg[axis] = rpy[axis] * 180.0 / rw::math::Pi;
}
} // namespace
bool OrientationRuleResolver::applyToStation(KeyStation& station, const rw::models::WorkCell& wc,
                                             const rw::kinematics::State& state, std::string* error) {
    // 非固定姿态必须在冻结时按真实场景解析，禁止沿用编辑期间缓存的 RPY，以免工装移动后误用旧姿态。
    if (station.orientation.mode == OrientationMode::Fixed) { if (error) error->clear(); return true; }
    rw::kinematics::Frame* ref = frame(wc, station.refFrame);
    rw::kinematics::Frame* target = frame(wc, station.orientation.targetFrame);
    if (!ref || !target) { if (error) *error = "Orientation reference or target frame is unavailable."; return false; }
    if (station.orientation.mode == OrientationMode::AlignFrame || station.orientation.mode == OrientationMode::AlignGeometryNormal) {
        rw::math::Rotation3D<> rotation = rw::kinematics::Kinematics::frameTframe(ref, target, state).R();
        if (station.orientation.invertNormal)
            // 翻转 X/Z 轴维持右手系，使工具 Z 轴沿反法线接近。
            rotation = rw::math::Rotation3D<>(-rotation.getCol(0), rotation.getCol(1), -rotation.getCol(2));
        storeRpy(rotation, station); if (error) error->clear(); return true;
    }
    if (station.orientation.mode == OrientationMode::PointAtTarget) {
        const rw::math::Vector3D<> source(station.position[0], station.position[1], station.position[2]);
        rw::math::Vector3D<> z = rw::kinematics::Kinematics::frameTframe(ref, target, state).P() - source;
        const double squaredLength = z[0] * z[0] + z[1] * z[1] + z[2] * z[2];
        if (squaredLength < 1e-12) { if (error) *error = "Pointing target coincides with the key station position."; return false; }
        z = rw::math::normalize(z); rw::math::Vector3D<> up(0.0, 0.0, 1.0);
        if (std::abs(rw::math::dot(up, z)) > 0.99) up = rw::math::Vector3D<>(0.0, 1.0, 0.0);
        const rw::math::Vector3D<> x = rw::math::normalize(rw::math::cross(up, z));
        storeRpy(rw::math::Rotation3D<>(x, rw::math::cross(z, x), z), station); if (error) error->clear(); return true;
    }
    if (error) *error = "Unsupported orientation mode."; return false;
}
} // namespace rws
