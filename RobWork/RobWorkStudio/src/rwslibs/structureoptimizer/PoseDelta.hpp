#ifndef RWS_STRUCTUREOPTIMIZATION_POSEDELTA_HPP
#define RWS_STRUCTUREOPTIMIZATION_POSEDELTA_HPP

#include "ParameterBinding.hpp"

#include <rw/math/Transform3D.hpp>
#include <rw/math/Vector3D.hpp>

namespace rws {

/**
 * Frozen S34 SE(3) convention.  Rotation-vector deltas never become Euler
 * state: the only supported update is T_next = T_baseline * Exp(delta).
 */
class PoseDelta
{
  public:
    static rw::math::Transform3D<> applyRotationVectorDelta(
        const rw::math::Transform3D<>& baseline, const rw::math::Vector3D<>& rotationVector,
        PoseDeltaComposition composition);
};

}    // namespace rws

#endif    // RWS_STRUCTUREOPTIMIZATION_POSEDELTA_HPP
