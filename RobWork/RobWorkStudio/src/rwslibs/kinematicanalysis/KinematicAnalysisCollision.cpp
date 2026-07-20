#include "KinematicAnalysisCollision.hpp"

// RobWork 碰撞检测器与默认策略工厂头文件。
#include <rw/proximity/CollisionDetector.hpp>
#include <rwlibs/proximitystrategies/ProximityStrategyFactory.hpp>

// 类型别名简化代码。
// ownedPtr / Ptr 是 RobWork 侵入式智能指针:堆分配 + 自动回收。
using rw::core::ownedPtr;
using rw::core::Ptr;
using rw::models::WorkCell;
using rw::proximity::CollisionDetector;
using rwlibs::proximitystrategies::ProximityStrategyFactory;

namespace rws {

// =============================================================================
//  makeKinematicAnalysisCollisionDetector:从 WorkCell 创建独立的碰撞检测器
// =============================================================================
//
// 这是一个轻量工厂,只做"包一层 ownedPtr"的工作:
//
//   1) 如果传入的 workcell 为 NULL,直接返回 NULL;
//      (调用方需要自己处理这种情况,如取消碰撞检查或返回 warning)
//   2) 否则用 ProximityStrategyFactory::makeDefaultCollisionStrategy()
//      选择默认近距离策略(本项目里通常是 Bullet / FCL / PQP 等,取决于
//      RobWork 构建配置);
//   3) 用 ownedPtr(new ...) 创建一个新的 CollisionDetector;
//      智能指针自动管理生命周期 — 函数返回后 detector 由 Ptr 拥有,
//      当 PyObject / shared_ptr / QtFuture 引用的 Ptr 全部释放时自动析构。
//
// 设计动机:后台 QtConcurrent worker 不能直接复用 _studio->getCollisionDetector(),
// 因为后者由 RobWorkStudio UI 场景管理,跨线程使用可能产生线程安全问题。
// 因此 worker 调用本函数从 WorkCell 独立构造,完全线程隔离。
//
// 用法(在 sampleWorkspace/analyzePoseReachability 的 worker lambda 中):
//   const auto detector = makeKinematicAnalysisCollisionDetector(runWorkCell);
//   if (runConfig.checkCollision && detector == NULL)
//       runConfig.checkCollision = false;   // WorkCell 无可碰撞模型,降级
//   worker.sampleWorkspace(..., detector, ...);
//
// 注意:虽然构造函数看似简单,但它是后台线程安全的关键入口,所有 worker 都
// 必须经过它而不是 widget 共享探测器。
Ptr< CollisionDetector > makeKinematicAnalysisCollisionDetector (
    Ptr< WorkCell > workcell)
{
    if (workcell == NULL)
        return NULL;

    // 创建新的 CollisionDetector 实例。
    //   - ProximityStrategyFactory::makeDefaultCollisionStrategy() 选择 RobWork
    //     构建时指定的默认近距离策略;
    //   - ownedPtr(new ...) 把裸指针包装成 Ptr 智能指针,自动管理生命周期。
    return ownedPtr (
        new CollisionDetector (
            workcell,
            ProximityStrategyFactory::makeDefaultCollisionStrategy ()));
}

}    // namespace rws
