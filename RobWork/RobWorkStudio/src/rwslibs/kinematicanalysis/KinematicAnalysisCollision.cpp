#include "KinematicAnalysisCollision.hpp"

#include <rw/proximity/CollisionDetector.hpp>
#include <rwlibs/proximitystrategies/ProximityStrategyFactory.hpp>

using rw::core::ownedPtr;
using rw::core::Ptr;
using rw::models::WorkCell;
using rw::proximity::CollisionDetector;
using rwlibs::proximitystrategies::ProximityStrategyFactory;

namespace rws {

Ptr< CollisionDetector > makeKinematicAnalysisCollisionDetector (
    Ptr< WorkCell > workcell)
{
    if (workcell == NULL)
        return NULL;
    return ownedPtr (
        new CollisionDetector (
            workcell,
            ProximityStrategyFactory::makeDefaultCollisionStrategy ()));
}

}    // namespace rws
