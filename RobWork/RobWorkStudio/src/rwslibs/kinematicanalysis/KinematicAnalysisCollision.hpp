#ifndef RWS_KINEMATICANALYSIS_KINEMATICANALYSISCOLLISION_HPP
#define RWS_KINEMATICANALYSIS_KINEMATICANALYSISCOLLISION_HPP

// RobWork 的智能指针 Ptr:用于跨函数传递所有权的轻量句柄。
#include <rw/core/Ptr.hpp>

// 提前声明避免包含完整 WorkCell / CollisionDetector 头文件(它们较重)。
// 真正使用需要在 .cpp 中 include 完整类型。
namespace rw { namespace models { class WorkCell; } }
namespace rw { namespace proximity { class CollisionDetector; } }

namespace rws {

// =============================================================================
//  makeKinematicAnalysisCollisionDetector:为后台分析创建独立的碰撞检测器
// =============================================================================
//
// 设计动机:之前后台 worker 复用了 _studio->getCollisionDetector(),
// 但该 detector 由 UI 主线程 / RobWorkStudio 场景管理;
// 在 worker 线程中使用可能产生线程安全风险,且 detector 生命周期与 UI 耦合。
//
// 这个工厂函数从传入的 WorkCell 指针独立构造一个新的 CollisionDetector,
// 由 rw::core::Ptr 管理所有权,worker 函数返回时 detector 自动释放,
// 不会与 UI / 场景共享状态,完全线程隔离。
//
// @param workcell 目标 WorkCell;若为 NULL 直接返回 NULL(调用方需自行处理)
// @return 新的 CollisionDetector 指针;workcell 为空时返回 NULL
// 注:策略策略使用 ProximityStrategyFactory::makeDefaultCollisionStrategy(),
// 适用于大多数通用 URDF/SDF 工作单元。
rw::core::Ptr< rw::proximity::CollisionDetector > makeKinematicAnalysisCollisionDetector (
    rw::core::Ptr< rw::models::WorkCell > workcell);

}    // namespace rws

#endif    // RWS_KINEMATICANALYSIS_KINEMATICANALYSISCOLLISION_HPP
