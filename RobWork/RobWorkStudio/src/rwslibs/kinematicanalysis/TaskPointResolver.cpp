#include "TaskPointResolver.hpp"

#include <rw/kinematics/Kinematics.hpp>
#include <rw/math/RPY.hpp>
#include <rw/math/Transform3D.hpp>
#include <rw/math/Vector3D.hpp>

#include <algorithm>
#include <cctype>
#include <string>

using namespace rws;

namespace {

// 字符串小写比较,避免 WORLD / world / World 不一致。
bool iequals (const std::string& a, const std::string& b)
{
    if (a.size () != b.size ())
        return false;
    for (std::size_t i = 0; i < a.size (); ++i) {
        if (std::tolower (static_cast<unsigned char> (a[i])) !=
            std::tolower (static_cast<unsigned char> (b[i])))
            return false;
    }
    return true;
}

// 构造一条 KIN_TASK_* 告警。
AnalysisWarning makeTaskWarning (
    const std::string& code,
    const std::string& message,
    AnalysisStatus severity = AnalysisStatus::Fail)
{
    AnalysisWarning w;
    w.code     = code;
    w.message  = message;
    w.source   = "KinematicAnalysis";
    w.severity = severity;
    return w;
}

}    // namespace

ResolvedTaskPoint rws::resolveTaskPoint (
    rw::models::WorkCell* workcell,
    rw::core::Ptr< rw::models::Device > device,
    rw::core::Ptr< const rw::kinematics::Frame > defaultTcpFrame,
    const rw::kinematics::State& state,
    const TaskPoint& taskPoint)
{
    ResolvedTaskPoint r;
    r.targetInDeviceBase = taskPoint;
    r.failure = KinematicFailureReason::None;
    r.valid   = false;

    // 1. workcell / device 早退。
    if (workcell == nullptr) {
        r.failure = KinematicFailureReason::NoDevice;
        r.warnings.push_back (makeTaskWarning (
            "KIN_TASK_NO_WORKCELL",
            "Task point analysis requires a loaded WorkCell."));
        return r;
    }
    if (device == nullptr) {
        r.failure = KinematicFailureReason::NoDevice;
        r.warnings.push_back (makeTaskWarning (
            "KIN_TASK_NO_DEVICE",
            "Task point analysis requires a valid device."));
        return r;
    }

    // 2. TCP 帧解析:行级 tcpFrame 优先,空则用顶部 default TCP。
    rw::core::Ptr< const rw::kinematics::Frame > resolvedTcp;
    if (!taskPoint.tcpFrame.empty ()) {
        resolvedTcp = rw::core::Ptr< const rw::kinematics::Frame > (
            workcell->findFrame (taskPoint.tcpFrame));
        if (resolvedTcp == nullptr) {
            r.failure = KinematicFailureReason::NoTcpFrame;
            r.warnings.push_back (makeTaskWarning (
                "KIN_TASK_TCP_NOT_FOUND",
                "Task point TCP frame '" + taskPoint.tcpFrame + "' not found in WorkCell."));
            return r;
        }
        r.targetInDeviceBase.tcpFrame = taskPoint.tcpFrame;
    }
    else if (defaultTcpFrame != nullptr) {
        resolvedTcp = defaultTcpFrame;
        r.targetInDeviceBase.tcpFrame = defaultTcpFrame->getName ();
    }
    else {
        r.failure = KinematicFailureReason::NoTcpFrame;
        r.warnings.push_back (makeTaskWarning (
            "KIN_TASK_NO_TCP",
            "Task point has no TCP frame and no default TCP is selected."));
        return r;
    }
    r.tcpFrame = resolvedTcp;

    // 3. refFrame 解析:WORLD / device base / 任意 named frame。
    const std::string ref = taskPoint.refFrame;
    const std::string baseName = device->getBase ()->getName ();
    if (ref.empty () || iequals (ref, kTaskWorldFrameName)) {
        // world 解释:在 world 下构造 Transform3D,转到 base 下。
        const rw::math::RPY<> rpy (
            taskPoint.rpyDeg[0] * rw::math::Pi / 180.0,
            taskPoint.rpyDeg[1] * rw::math::Pi / 180.0,
            taskPoint.rpyDeg[2] * rw::math::Pi / 180.0);
        const rw::math::Transform3D<> worldTtarget (
            rw::math::Vector3D<> (taskPoint.position[0], taskPoint.position[1], taskPoint.position[2]),
            rpy);
        rw::math::Transform3D<> baseTtarget;
        try {
            baseTtarget = rw::kinematics::Kinematics::frameTframe (
                device->getBase (), workcell->getWorldFrame (), state) * worldTtarget;
        }
        catch (const std::exception& ex) {
            r.failure = KinematicFailureReason::InvalidTarget;
            r.warnings.push_back (makeTaskWarning (
                "KIN_TASK_WORLD_TO_BASE",
                std::string ("Failed to transform WORLD target to device base: ") + ex.what ()));
            return r;
        }
        r.targetInDeviceBase.position = {{
            baseTtarget.P ()[0], baseTtarget.P ()[1], baseTtarget.P ()[2]}};
        const rw::math::RPY<> baseRpy (baseTtarget.R ());
        r.targetInDeviceBase.rpyDeg = {{
            baseRpy (0) * 180.0 / rw::math::Pi,
            baseRpy (1) * 180.0 / rw::math::Pi,
            baseRpy (2) * 180.0 / rw::math::Pi}};
        r.targetInDeviceBase.refFrame = baseName;
    }
    else if (iequals (ref, baseName)) {
        // 已经在 device base 下,直接使用。
        r.targetInDeviceBase.refFrame = baseName;
    }
    else {
        // 在 WorkCell 查找 named frame。
        rw::kinematics::Frame* refFrame = workcell->findFrame (ref);
        if (refFrame == nullptr) {
            r.failure = KinematicFailureReason::InvalidTarget;
            r.warnings.push_back (makeTaskWarning (
                "KIN_TASK_REF_NOT_FOUND",
                "Task point refFrame '" + ref + "' not found in WorkCell."));
            return r;
        }
        // refTtarget 构造:在 ref 下给出 position/rpy。
        const rw::math::RPY<> rpy (
            taskPoint.rpyDeg[0] * rw::math::Pi / 180.0,
            taskPoint.rpyDeg[1] * rw::math::Pi / 180.0,
            taskPoint.rpyDeg[2] * rw::math::Pi / 180.0);
        const rw::math::Transform3D<> refTtarget (
            rw::math::Vector3D<> (taskPoint.position[0], taskPoint.position[1], taskPoint.position[2]),
            rpy);
        rw::math::Transform3D<> baseTtarget;
        try {
            baseTtarget = rw::kinematics::Kinematics::frameTframe (
                device->getBase (), refFrame, state) * refTtarget;
        }
        catch (const std::exception& ex) {
            r.failure = KinematicFailureReason::InvalidTarget;
            r.warnings.push_back (makeTaskWarning (
                "KIN_TASK_REF_TRANSFORM",
                std::string ("Failed to transform named-frame target to device base: ") + ex.what ()));
            return r;
        }
        r.targetInDeviceBase.position = {{
            baseTtarget.P ()[0], baseTtarget.P ()[1], baseTtarget.P ()[2]}};
        const rw::math::RPY<> baseRpy (baseTtarget.R ());
        r.targetInDeviceBase.rpyDeg = {{
            baseRpy (0) * 180.0 / rw::math::Pi,
            baseRpy (1) * 180.0 / rw::math::Pi,
            baseRpy (2) * 180.0 / rw::math::Pi}};
        r.targetInDeviceBase.refFrame = baseName;
    }

    r.valid = true;
    return r;
}