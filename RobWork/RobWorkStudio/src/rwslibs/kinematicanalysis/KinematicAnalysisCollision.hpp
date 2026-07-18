#ifndef RWS_KINEMATICANALYSIS_KINEMATICANALYSISCOLLISION_HPP
#define RWS_KINEMATICANALYSIS_KINEMATICANALYSISCOLLISION_HPP

#include <rw/core/Ptr.hpp>

namespace rw { namespace models { class WorkCell; } }
namespace rw { namespace proximity { class CollisionDetector; } }

namespace rws {

rw::core::Ptr< rw::proximity::CollisionDetector > makeKinematicAnalysisCollisionDetector (
    rw::core::Ptr< rw::models::WorkCell > workcell);

}    // namespace rws

#endif    // RWS_KINEMATICANALYSIS_KINEMATICANALYSISCOLLISION_HPP
