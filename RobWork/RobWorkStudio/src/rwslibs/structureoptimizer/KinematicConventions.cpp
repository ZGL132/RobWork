#include "KinematicConventions.hpp"

#include <rw/math/EAA.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace rws {
namespace {

const double kAxisTolerance = 1e-12;

bool finiteVector(const rw::math::Vector3D<>& vector)
{
    return std::isfinite(vector(0)) && std::isfinite(vector(1)) && std::isfinite(vector(2));
}

rw::math::Vector3D<> normalizedOrZero(const rw::math::Vector3D<>& vector)
{
    if (!finiteVector(vector) || vector.norm2() <= kAxisTolerance)
        return rw::math::Vector3D<>(0.0, 0.0, 0.0);
    return rw::math::normalize(vector);
}

}    // namespace

double KinematicConventions::modelCoordinate(double inputCoordinate, double zeroPositionOffset)
{
    return inputCoordinate + zeroPositionOffset;
}

rw::math::Transform3D<> KinematicConventions::jointMotion(CanonicalJointMotion type,
                                                           const rw::math::Vector3D<>& axis,
                                                           double coordinate)
{
    using namespace rw::math;
    if (type == CanonicalJointMotion::Fixed)
        return Transform3D<>::identity();

    const Vector3D<> unitAxis = normalizedOrZero(axis);
    if (unitAxis.norm2() <= kAxisTolerance || !std::isfinite(coordinate))
        return Transform3D<>::identity();

    if (type == CanonicalJointMotion::Revolute)
        return Transform3D<>(Vector3D<>(), EAA<>(unitAxis, coordinate).toRotation3D());

    return Transform3D<>(unitAxis * coordinate);
}

rw::math::Transform3D<> KinematicConventions::composeJointTransform(
    const rw::math::Transform3D<>& parentToJointZero,
    CanonicalJointMotion type,
    const rw::math::Vector3D<>& axis,
    double inputCoordinate,
    double zeroPositionOffset,
    const rw::math::Transform3D<>& jointMotionToChild)
{
    return parentToJointZero *
           jointMotion(type, axis, modelCoordinate(inputCoordinate, zeroPositionOffset)) *
           jointMotionToChild;
}

TangentBasis KinematicConventions::stableTangentBasis(const rw::math::Vector3D<>& referenceAxis)
{
    using namespace rw::math;
    TangentBasis result;
    const Vector3D<> normal = normalizedOrZero(referenceAxis);
    if (normal.norm2() <= kAxisTolerance)
        return result;

    const Vector3D<> globalAxes[] = {Vector3D<>::x(), Vector3D<>::y(), Vector3D<>::z()};
    const Vector3D<>* helper = &globalAxes[0];
    double smallestAlignment = std::fabs(dot(normal, *helper));
    for (std::size_t index = 1; index < 3; ++index) {
        const double alignment = std::fabs(dot(normal, globalAxes[index]));
        if (alignment < smallestAlignment) {
            helper = &globalAxes[index];
            smallestAlignment = alignment;
        }
    }

    result.first = normalizedOrZero(cross(*helper, normal));
    result.second = normalizedOrZero(cross(normal, result.first));
    result.valid = result.first.norm2() > kAxisTolerance && result.second.norm2() > kAxisTolerance;
    return result;
}

rw::math::Vector3D<> KinematicConventions::tiltedAxis(
    const rw::math::Vector3D<>& referenceAxis, double alpha, double beta)
{
    using namespace rw::math;
    const Vector3D<> normal = normalizedOrZero(referenceAxis);
    const TangentBasis basis = stableTangentBasis(normal);
    if (!basis.valid || !std::isfinite(alpha) || !std::isfinite(beta))
        return normal;

    const double rho = std::hypot(alpha, beta);
    if (rho == 0.0)
        return normal;

    return normalize(std::cos(rho) * normal +
                     (std::sin(rho) / rho) * (alpha * basis.first + beta * basis.second));
}

double KinematicConventions::angleBetween(const rw::math::Vector3D<>& first,
                                          const rw::math::Vector3D<>& second)
{
    const rw::math::Vector3D<> unitFirst = normalizedOrZero(first);
    const rw::math::Vector3D<> unitSecond = normalizedOrZero(second);
    if (unitFirst.norm2() <= kAxisTolerance || unitSecond.norm2() <= kAxisTolerance)
        return std::numeric_limits<double>::quiet_NaN();
    return std::acos(std::max(-1.0, std::min(1.0, rw::math::dot(unitFirst, unitSecond))));
}

bool KinematicConventions::isProperRotation(const rw::math::Rotation3D<>& rotation,
                                            double tolerance)
{
    return std::isfinite(tolerance) && tolerance >= 0.0 && rotation.isProperRotation(tolerance);
}

}    // namespace rws
