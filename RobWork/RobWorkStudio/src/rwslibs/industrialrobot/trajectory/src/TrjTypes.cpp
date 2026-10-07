/**
 * @file   TrjTypes.cpp
 * @brief  轨迹数据模型的实现翻译单元——段结构不变量守卫
 *         （validateTrajectorySegment；WP-16-T04 批）。
 *
 * 设计依据：
 *   - units/trajectory.md §6.2（TrajectorySegment/Waypoint 不变量原文）、
 *     §6.3（字段级义务清单——"kind==TaskPoint 必填 sourceTaskPoint、
 *     Dwell 必填 dwellDurationS"）、§15.0（错误类型——调用方契约违约
 *     fail-fast 走 TrajectoryError）
 *   - 需求 NFR-COR-03（非法输入拒绝，不钳制不静默）
 *
 * 确定性（NFR-COR-02）：校验序固定、首错即抛——同一坏段恒得同一异常
 * 文案；无迭代依赖输入序之外的任何状态。
 */

#include <sdurws/ird/trajectory/TrjTypes.hpp>

#include <sdurws/ird/trajectory/Errors.hpp>

namespace sdurws::ird::trajectory {

void validateTrajectorySegment(const TrajectorySegment& segment)
{
    // 校验 1：段内路点数量下界。§6.2 原文"段内路点（含段端点；至少 2
    // 个）"——单路点无法构成几何段（没有起终点），是构造方编程错误。
    if (segment.waypoints.size() < 2U) {
        throw TrajectoryError("trajectory/segment/waypoint-count",
                              "段内路点须 ≥2（含段端点），实际 "
                                  + std::to_string(segment.waypoints.size())
                                  + "（段序号 "
                                  + std::to_string(segment.segmentIndex) + "）");
    }

    for (std::size_t i = 0; i < segment.waypoints.size(); ++i) {
        const Waypoint& wp = segment.waypoints[i];

        // 校验 2：双域互斥——JointLinear 段必带 q 不带 target，CartesianLine
        // 段必带 target 不带 q。§6.2"关节空间或笛卡尔空间二选一（variant
        // 承载；同一路点不得双域同时有效）"；域缺失同样违约（段类型已
        // 声明，路点必须落到该域）。
        const bool hasQ = wp.q.has_value();
        const bool hasTarget = wp.target.has_value();
        if (segment.spaceType == SegmentSpaceType::JointLinear) {
            if (!hasQ || hasTarget) {
                throw TrajectoryError(
                    "trajectory/segment/domain-mismatch",
                    "JointLinear 段路点须携带 q 且不携带 target（路点下标 "
                        + std::to_string(i) + "：q=" + (hasQ ? "有" : "无")
                        + "，target=" + (hasTarget ? "有" : "无") + "）");
            }
        } else {  // SegmentSpaceType::CartesianLine
            if (!hasTarget || hasQ) {
                throw TrajectoryError(
                    "trajectory/segment/domain-mismatch",
                    "CartesianLine 段路点须携带 target 且不携带 q（路点下标 "
                        + std::to_string(i) + "：q=" + (hasQ ? "有" : "无")
                        + "，target=" + (hasTarget ? "有" : "无") + "）");
            }
        }

        // 校验 3：路点的段定位键与本段一致——§6.3"segmentIndex 单调连续；
        // 诊断/复检/动画共用同一定位键"，错位即定位失效。
        if (wp.segmentIndex != segment.segmentIndex) {
            throw TrajectoryError(
                "trajectory/segment/waypoint-index",
                "路点 segmentIndex 与所属段不一致（路点下标 "
                    + std::to_string(i) + "：路点值 "
                    + std::to_string(wp.segmentIndex) + "，段值 "
                    + std::to_string(segment.segmentIndex) + "）");
        }

        // 校验 4：任务点路点必须可追溯（NFR-COR-04/ARC-04——来源对象 ID
        // 缺失则证据链断链，属构造方编程错误）。
        if (wp.kind == WaypointKind::TaskPoint && !wp.sourceTaskPoint.has_value()) {
            throw TrajectoryError(
                "trajectory/segment/task-point-source",
                "kind==TaskPoint 的路点必须携带 sourceTaskPoint（路点下标 "
                    + std::to_string(i) + "）");
        }

        // 校验 5：驻留路点的时长契约（§5.4 步骤 3——驻留映射为时间轴常值
        // 段；时长非正则无驻留语义，应由调用方不产出 Dwell 路点）。
        if (wp.kind == WaypointKind::Dwell
            && (!wp.dwellDurationS.has_value() || *wp.dwellDurationS <= 0.0)) {
            throw TrajectoryError(
                "trajectory/segment/dwell-duration",
                "kind==Dwell 的路点必须携带 dwellDurationS>0（s；路点下标 "
                    + std::to_string(i) + "）");
        }
    }
}

}  // namespace sdurws::ird::trajectory
