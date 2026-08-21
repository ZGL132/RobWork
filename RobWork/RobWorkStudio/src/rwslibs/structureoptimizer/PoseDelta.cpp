#include "PoseDelta.hpp"

#include <rw/math/EAA.hpp>

namespace rws {

rw::math::Transform3D<> PoseDelta::applyRotationVectorDelta(
    const rw::math::Transform3D<>& baseline, const rw::math::Vector3D<>& rotationVector,
    const PoseDeltaComposition composition)
{
    if (composition != PoseDeltaComposition::Right)
        return baseline;
    const rw::math::Transform3D<> delta(
        rw::math::Vector3D<>(),
        rw::math::EAA<>(rotationVector(0), rotationVector(1), rotationVector(2)).toRotation3D());
    return baseline * delta;
}

}    // namespace rws
