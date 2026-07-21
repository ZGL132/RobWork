#include "TaskPointUiLogic.hpp"

#include <rw/math/RPY.hpp>

using namespace rws;

// =============================================================================
//  analyzeSelectedTaskPointRows:增量分析选中的任务点
// =============================================================================
//
// 算法思路("重置-填充-覆盖"模式):
//   1) results 复制 previousResults 后 resize 到 allPoints.size();
//      这样保证所有槽位都存在,后续可按 row 索引赋值。
//   2) 遍历所有点:若 results[i].taskPoint.id 为空(说明是新增的点,
//      previousResults 还没它),就用 allPoints[i] 填好 taskPoint 字段。
//   3) 仅对 selectedRows 中的行重新调 analyzer.analyzeTaskPoint 求解;
//      其它行保持 previousResults 中的旧结果,实现"增量分析"。
//
// 这种设计的优势:
//   - 用户编辑某个点的位置后,只重算该点(不重算全部);
//   - 未选中的点保持"上次结果",UI 可继续展示旧分析数据。
//
// 边界处理:selectedRows 可能含越界行(比如用户删除了一行),直接 continue 跳过。
std::vector< TaskPointReachabilityResult > rws::analyzeSelectedTaskPointRows (
    const KinematicAnalyzer& analyzer,
    rw::models::WorkCell* workcell,
    rw::core::Ptr< rw::models::Device > device,
    rw::core::Ptr< const rw::kinematics::Frame > defaultTcpFrame,
    const rw::kinematics::State& state,
    const std::vector< TaskPoint >& allPoints,
    const std::vector< int >& selectedRows,
    const std::vector< TaskPointReachabilityResult >& previousResults,
    rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector)
{
    // 1) 重置到 allPoints 大小(继承 previousResults 已有结果)
    std::vector< TaskPointReachabilityResult > results = previousResults;
    results.resize (allPoints.size ());
    // 2) 填充空槽位的 taskPoint(只是元数据,不调 IK)
    for (std::size_t i = 0; i < allPoints.size (); ++i) {
        if (results[i].taskPoint.id.empty ())
            results[i].taskPoint = allPoints[i];
    }

    // 3) 增量求解选中的行
    for (const int row : selectedRows) {
        // 越界保护:UI 模型行号与 allPoints 索引不一致时跳过
        if (row < 0 || static_cast< std::size_t > (row) >= allPoints.size ())
            continue;
        // 真正调 IK;WorkCell-aware 模式(workcell != NULL)会先解析帧
        results[static_cast< std::size_t > (row)] =
            analyzer.analyzeTaskPoint (
                workcell, device, defaultTcpFrame, state,
                allPoints[static_cast< std::size_t > (row)], collisionDetector);
    }
    return results;
}

// =============================================================================
//  taskPointFromCurrentTcpPose:把当前 TCP 位姿转换为 TaskPoint
// =============================================================================
//
// 用于"Import current TCP"按钮:
//   1) 从 baseTtcp 的旋转矩阵 R 提取 RPY(等价于"先 R 再 P 再 Y"内旋);
//   2) 弧度转度,符合 plugin 内部 RPY 单位约定;
//   3) 容差字段直接取自当前 UI 阈值(用户改阈值后下次导入自动跟随);
//   4) 标记为 enabled、note 提示来源,方便用户后续编辑识别。
//
// 注意:位置坐标直接从 baseTtcp.P() 读,假定 baseTtcp 已经是 device base
// 坐标系下的 4×4 变换(由 caller 负责换算)。
TaskPoint rws::taskPointFromCurrentTcpPose (
    const std::string& id,
    const std::string& tcpFrameName,
    const std::string& deviceBaseFrameName,
    const rw::math::Transform3D<>& baseTtcp,
    const KinematicThresholds& thresholds)
{
    // RPY 提取:R 是 3×3 旋转矩阵,rpy(0/1/2) = roll/pitch/yaw(单位:弧度)
    const rw::math::RPY<> rpy (baseTtcp.R ());
    // 弧度 → 度:180/π
    const double toDeg = 180.0 / rw::math::Pi;

    // 构造 TaskPoint
    TaskPoint p;
    p.id          = id;                                          // 由 UI 用时间戳生成
    p.name        = id;                                          // 默认名 = id
    p.type        = TaskPointType::Generic;                      // 通用类型
    p.refFrame    = deviceBaseFrameName;                        // base 坐标系名
    p.tcpFrame    = tcpFrameName;                                // TCP 坐标系名
    // 位置:从 baseTtcp.P() 读 3 元素位置
    p.position    = {{baseTtcp.P ()[0], baseTtcp.P ()[1], baseTtcp.P ()[2]}};
    // RPY:弧度 × 180/π = 度
    p.rpyDeg      = {{rpy (0) * toDeg, rpy (1) * toDeg, rpy (2) * toDeg}};
    // 容差:直接用当前 UI 阈值(用户在 Report tab 改后下次导入跟随)
    p.tolerance.positionMeters = thresholds.positionToleranceMeters;
    p.tolerance.orientationDeg = thresholds.orientationToleranceDeg;
    p.tolerance.allowToolRollFree = false;                      // 默认不允滚转
    p.weight      = 1.0;                                         // 默认权重 1.0
    p.enabled     = true;                                        // 默认启用
    p.note        = "imported from current TCP pose";            // 来源提示
    return p;
}
