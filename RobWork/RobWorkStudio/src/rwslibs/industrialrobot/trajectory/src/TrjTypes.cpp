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
        // 声明，路点必须落到该域）。WP-16-T16 批（TRJ-08-S3）第三分支：
        // ContinuousProcess 段要求**同段域一致**（全部 q 或全部 target——
        // 连续工艺段是工艺语义标记而非第三种几何构造器，几何域由调用方
        // 的几何求值器决定；混带/缺失＝段级 variant 违约，与既有两值同
        // 源纪律）。分支穷举三词表值（无 default——词表扩值时此处编译
        // 期不可达分支显性化，review 可见点）。
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
        } else if (segment.spaceType == SegmentSpaceType::CartesianLine) {
            if (!hasTarget || hasQ) {
                throw TrajectoryError(
                    "trajectory/segment/domain-mismatch",
                    "CartesianLine 段路点须携带 target 且不携带 q（路点下标 "
                        + std::to_string(i) + "：q=" + (hasQ ? "有" : "无")
                        + "，target=" + (hasTarget ? "有" : "无") + "）");
            }
        } else {  // SegmentSpaceType::ContinuousProcess（词表第三值——TRJ-08-S3）
            // 域一致性的锚＝首路点（i==0 已入循环——以"路点自身与其余
            // 路点"的一致性表达：任一路点与其他路点域不同即混带。实现
            // 上按"本段内全部路点同域"判——用首路点作参照，循环内逐点
            // 对照（首点与自身对照恒真，零特判）。
            const Waypoint& first = segment.waypoints.front();
            const bool domainIsJoint = first.q.has_value();
            const bool pointMatchesDomain =
                domainIsJoint ? (hasQ && !hasTarget) : (hasTarget && !hasQ);
            if (!pointMatchesDomain) {
                throw TrajectoryError(
                    "trajectory/segment/domain-mismatch",
                    "ContinuousProcess 段路点域须同段一致（全部 q 或全部 "
                        "target，不得混带或缺失；路点下标 "
                        + std::to_string(i) + "：q=" + (hasQ ? "有" : "无")
                        + "，target=" + (hasTarget ? "有" : "无")
                        + "，段域=" + (domainIsJoint ? "关节" : "笛卡尔")
                        + "）");
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
