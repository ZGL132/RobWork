#include "OrientationRuleResolver.hpp"
#include <rw/kinematics/Kinematics.hpp>
#include <rw/math/Constants.hpp>
#include <rw/math/RPY.hpp>
#include <rw/math/Rotation3D.hpp>
#include <rw/math/Vector3D.hpp>
#include <rw/models/WorkCell.hpp>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
namespace rws { namespace {
rw::kinematics::Frame* frame(const rw::models::WorkCell& wc, const std::string& name) {
    return name.empty() || name == "WORLD" ? wc.getWorldFrame() : wc.findFrame(name);
}
void storeRpy(const rw::math::Rotation3D<>& rotation, KeyStation& station) {
    const rw::math::RPY<> rpy(rotation);
    for (int axis = 0; axis < 3; ++axis) station.rpyDeg[axis] = rpy[axis] * 180.0 / rw::math::Pi;
}

void storeResolutionEvidence(KeyStation& station, const std::string& targetSource)
{
    // P2 只消费一个确定性的代表姿态。证据字符串与冻结 WorkCell/State 指纹配合，
    // 能在报告中还原“何种规则、相对于哪个参考系、由什么目标”产生了这组三个角度。
    std::ostringstream stream;
    stream << std::setprecision(17)
           << "resolver=OrientationRuleResolver.1;mode=" << toString(station.orientation.mode)
           << ";referenceFrame=" << station.refFrame
           << ";" << targetSource
           << ";invertNormal=" << (station.orientation.invertNormal ? "true" : "false")
           << ";rpyDeg=" << station.rpyDeg[0] << ',' << station.rpyDeg[1] << ',' << station.rpyDeg[2];
    station.orientation.resolutionEvidence = stream.str();
}

bool parseReferencePoint(const std::string& text, rw::math::Vector3D<>& point)
{
    // 需求 JSON 和属性编辑器统一把点写为 "x, y, z"。解析时兼容分号与空白，
    // 但严格要求恰好三个有限数，避免把不完整工艺意图静默解释为世界原点。
    std::string normalized = text;
    std::replace(normalized.begin(), normalized.end(), ',', ' ');
    std::replace(normalized.begin(), normalized.end(), ';', ' ');
    std::istringstream stream(normalized);
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    std::string extra;
    if (!(stream >> x >> y >> z) || (stream >> extra) ||
        !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        return false;
    point = rw::math::Vector3D<>(x, y, z);
    return true;
}
} // namespace
bool OrientationRuleResolver::applyToStation(KeyStation& station, const rw::models::WorkCell& wc,
                                             const rw::kinematics::State& state, std::string* error) {
    // 非固定姿态必须在冻结时按真实场景解析，禁止沿用编辑期间缓存的 RPY，以免工装移动后误用旧姿态。
    if (station.orientation.mode == OrientationMode::Fixed) {
        storeResolutionEvidence(station, "target=fixed-rpy");
        if (error) error->clear();
        return true;
    }
    rw::kinematics::Frame* ref = frame(wc, station.refFrame);
    if (!ref) { if (error) *error = "Orientation reference frame is unavailable."; return false; }
    if (station.orientation.mode == OrientationMode::AlignFrame || station.orientation.mode == OrientationMode::AlignGeometryNormal) {
        rw::kinematics::Frame* target = frame(wc, station.orientation.targetFrame);
        if (!target) { if (error) *error = "Orientation target frame is unavailable."; return false; }
        rw::math::Rotation3D<> rotation = rw::kinematics::Kinematics::frameTframe(ref, target, state).R();
        if (station.orientation.invertNormal)
            // 翻转 X/Z 轴维持右手系，使工具 Z 轴沿反法线接近。
            rotation = rw::math::Rotation3D<>(-rotation.getCol(0), rotation.getCol(1), -rotation.getCol(2));
        storeRpy(rotation, station);
        storeResolutionEvidence(station, "targetFrame=" + station.orientation.targetFrame);
        if (error) error->clear();
        return true;
    }
    if (station.orientation.mode == OrientationMode::PointAtTarget) {
        const rw::math::Vector3D<> source(station.position[0], station.position[1], station.position[2]);
        rw::math::Vector3D<> targetPoint;
        if (!station.orientation.targetFrame.empty()) {
            rw::kinematics::Frame* target = frame(wc, station.orientation.targetFrame);
            if (!target) { if (error) *error = "Pointing target frame is unavailable."; return false; }
            targetPoint = rw::kinematics::Kinematics::frameTframe(ref, target, state).P();
        } else if (!parseReferencePoint(station.orientation.targetPoint, targetPoint)) {
            if (error) *error = "Pointing target point must contain three finite coordinates: x, y, z.";
            return false;
        }
        rw::math::Vector3D<> z = targetPoint - source;
        const double squaredLength = z[0] * z[0] + z[1] * z[1] + z[2] * z[2];
        if (squaredLength < 1e-12) { if (error) *error = "Pointing target coincides with the key station position."; return false; }
        z = rw::math::normalize(z); rw::math::Vector3D<> up(0.0, 0.0, 1.0);
        if (std::abs(rw::math::dot(up, z)) > 0.99) up = rw::math::Vector3D<>(0.0, 1.0, 0.0);
        const rw::math::Vector3D<> x = rw::math::normalize(rw::math::cross(up, z));
        storeRpy(rw::math::Rotation3D<>(x, rw::math::cross(z, x), z), station);
        const std::string targetSource = station.orientation.targetFrame.empty()
            ? "targetPoint=" + station.orientation.targetPoint
            : "targetFrame=" + station.orientation.targetFrame;
        storeResolutionEvidence(station, targetSource);
        if (error) error->clear();
        return true;
    }
    if (error) *error = "Unsupported orientation mode."; return false;
}
} // namespace rws
