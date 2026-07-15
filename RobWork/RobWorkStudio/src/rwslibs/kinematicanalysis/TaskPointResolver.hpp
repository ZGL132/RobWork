#ifndef RWS_KINEMATICANALYSIS_TASKPOINTRESOLVER_HPP
#define RWS_KINEMATICANALYSIS_TASKPOINTRESOLVER_HPP

#include "KinematicAnalysisTypes.hpp"

#include <rw/core/Ptr.hpp>
#include <rw/kinematics/Frame.hpp>
#include <rw/kinematics/State.hpp>
#include <rw/models/Device.hpp>
#include <rw/models/WorkCell.hpp>

#include <vector>

namespace rws {

//! @brief Resolved form of a TaskPoint after looking up frames in the WorkCell.
//! 解析后的 TaskPoint:把 refFrame / tcpFrame 解析为真实 Frame 指针 + 目标位姿
//! 转换到 device base 坐标系下,供 KinematicAnalyzer::analyzeIk 进一步使用。
struct ResolvedTaskPoint
{
    //! @brief 转换到 device base 坐标系下的目标位姿。
    //! 保留原始 id/name/type/tolerance/weight/enabled/note;
    //! refFrame / tcpFrame 写为 device base / resolved TCP 名称,便于下游按 base 解释。
    TaskPoint targetInDeviceBase;

    //! @brief 解析后的 TCP 帧指针;可能为 NULL(resolver 失败或 default 也为空)。
    rw::core::Ptr< const rw::kinematics::Frame > tcpFrame;

    //! @brief 解析过程中产生的告警(KIN_TASK_REF_NOT_FOUND 等),
    //! 失败时也至少包含 1 条与 failure reason 对应的告警。
    std::vector< AnalysisWarning > warnings;

    //! @brief 解析失败时的主要原因;成功时为 None。
    KinematicFailureReason failure = KinematicFailureReason::None;

    //! @brief 解析成功 = true,失败 = false。调用方应优先用 valid 判断。
    bool valid = false;
};

//! @brief 把 TaskPoint 解析为可被 Analyzer 直接使用的目标。
//!
//! 解析规则:
//!   1. workcell == nullptr → valid=false, failure=NoDevice, warning KIN_TASK_NO_WORKCELL
//!   2. device == nullptr  → valid=false, failure=NoDevice
//!   3. tcpFrame:
//!        a) taskPoint.tcpFrame 非空且 workcell 存在 → 使用该 TCP;
//!        b) 找不到 → valid=false, failure=NoTcpFrame, warning KIN_TASK_TCP_NOT_FOUND
//!        c) 为空 → 使用 defaultTcpFrame;若也空 → valid=false, failure=NoTcpFrame
//!   4. refFrame:
//!        a) "WORLD" → 把 world 下目标转换到 device base 下;
//!        b) 等于 device->getBase()->getName() → 目标已在 base 下,直接使用;
//!        c) 其他 → 在 WorkCell 查找 frame;找不到 → valid=false, failure=InvalidTarget,
//!            warning KIN_TASK_REF_NOT_FOUND;
//!        d) 找到 → 用当前 state 计算 baseTref * refTtarget,目标转换到 base 下。
//!
//! 成功时:targetInDeviceBase 保留原 id/name/type/tolerance/weight/enabled/note;
//!         refFrame / tcpFrame 写为 device base / resolved TCP 名称。
ResolvedTaskPoint resolveTaskPoint (
    rw::models::WorkCell* workcell,
    rw::core::Ptr< rw::models::Device > device,
    rw::core::Ptr< const rw::kinematics::Frame > defaultTcpFrame,
    const rw::kinematics::State& state,
    const TaskPoint& taskPoint);

//! @brief 默认 world 帧名称(支持大小写不敏感比较)。
constexpr const char* kTaskWorldFrameName = "WORLD";

}    // namespace rws

#endif    // RWS_KINEMATICANALYSIS_TASKPOINTRESOLVER_HPP