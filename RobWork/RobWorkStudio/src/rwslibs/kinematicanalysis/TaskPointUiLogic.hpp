#ifndef RWS_KINEMATICANALYSIS_TASKPOINTUILOGIC_HPP
#define RWS_KINEMATICANALYSIS_TASKPOINTUILOGIC_HPP

// 引入分析数据结构和分析器接口:
//   - KinematicAnalysisTypes:TaskPoint / KinematicCurrentPoseResult / Thresholds 等
//   - KinematicAnalyzer     :核心无 UI 分析器(analyzeIk / analyzeTaskPoint)
#include "KinematicAnalysisTypes.hpp"
#include "KinematicAnalyzer.hpp"

// RobWork 类型:智能指针 / Frame / State / 变换 / Device / WorkCell / 碰撞检测器。
// 注意:此头只做前向声明,具体类型仅在 .cpp 引入。
#include <rw/core/Ptr.hpp>
#include <rw/kinematics/Frame.hpp>
#include <rw/kinematics/State.hpp>
#include <rw/math/Transform3D.hpp>
#include <rw/models/Device.hpp>
#include <rw/models/WorkCell.hpp>
#include <rw/proximity/CollisionDetector.hpp>

#include <string>
#include <vector>

namespace rws {

// =============================================================================
//  analyzeSelectedTaskPointRows:增量分析选中的任务点
// =============================================================================
//
// 设计动机:
//   - UI 在用户多选任务点行后调用此函数;
//   - 对于之前已分析过的行(previousResults 中存在对应 taskPoint.id),
//     直接复用旧结果,避免重复求解 IK(可能很慢);
//   - 对于新选中的行,调 analyzer.analyzeTaskPoint() 重新求解。
//
// 参数语义:
//   - analyzer          :核心分析器(无状态,线程安全 const);
//   - workcell          :用于 WorkCell-aware 帧解析;空时退化为非 WorkCell 模式;
//   - device            :选中的 device;
//   - defaultTcpFrame   :无 WorkCell 时的默认 TCP 帧;
//   - state             :分析时的 state 副本;
//   - allPoints         :所有任务点(可来自 taskPointModel);
//   - selectedRows      :UI 选中的行索引;
//   - previousResults   :之前一次全量分析结果,用于复用;
//
// 返回:与 selectedRows 顺序对应的结果列表(数量 = selectedRows.size() 中"新"行数 + 旧复用),
//       UI 负责按 selectedRows 顺序重新排列。
std::vector< TaskPointReachabilityResult > analyzeSelectedTaskPointRows (
    const KinematicAnalyzer& analyzer,
    rw::models::WorkCell* workcell,
    rw::core::Ptr< rw::models::Device > device,
    rw::core::Ptr< const rw::kinematics::Frame > defaultTcpFrame,
    const rw::kinematics::State& state,
    const std::vector< TaskPoint >& allPoints,
    const std::vector< int >& selectedRows,
    const std::vector< TaskPointReachabilityResult >& previousResults,
    rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector = NULL);

// =============================================================================
//  taskPointFromCurrentTcpPose:把当前 TCP 位姿装成 TaskPoint
// =============================================================================
//
// 用于"Import current TCP as task point"按钮:
//   1) 从 baseTtcp 取出 RPY;
//   2) 根据 thresholds 填好容差(用 positionToleranceMeters / orientationToleranceDeg);
//   3) weight 默认 1.0,allowToolRollFree 默认 false;
//   4) type 设为 Generic(由 UI 上层决定是否改)。
//
// 参数:
//   - id                :新任务点的 id(通常由 UI 用时间戳生成);
//   - tcpFrameName      :TCP 帧名(用于 refFrame 字段);
//   - deviceBaseFrameName:base frame 名(用于 refFrame 字段);
//   - baseTtcp          :base 坐标系下 TCP 的 4×4 变换;
//   - thresholds        :从当前 UI 阈值读取的容差(用于 tolerance 字段)。
//
// 返回:完整的 TaskPoint,可直接 push 到 TaskPointTableModel。
TaskPoint taskPointFromCurrentTcpPose (
    const std::string& id,
    const std::string& tcpFrameName,
    const std::string& deviceBaseFrameName,
    const rw::math::Transform3D<>& baseTtcp,
    const KinematicThresholds& thresholds);

}    // namespace rws

#endif    // RWS_KINEMATICANALYSIS_TASKPOINTUILOGIC_HPP
