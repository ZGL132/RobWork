#include "KinematicAnalysisEnvelope.hpp"

#include <rw/kinematics/Frame.hpp>
#include <rw/kinematics/Kinematics.hpp>
#include <rw/math/Transform3D.hpp>
#include <rw/models/Device.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace rws;

namespace {

double supportValue (const QPointF& point, double angle)
{
    return point.x () * std::cos (angle) + point.y () * std::sin (angle);
}

std::array< double, 3 > tcpPosition (
    const rw::models::Device* device,
    const rw::kinematics::Frame* tcpFrame,
    const rw::math::Q& q,
    const rw::kinematics::State& inputState)
{
    rw::kinematics::State state = inputState;
    const_cast< rw::models::Device* > (device)->setQ (q, state);
    const rw::kinematics::Frame* endFrame =
        tcpFrame != nullptr ? tcpFrame : device->getEnd ();
    const rw::math::Transform3D<> baseTtcp =
        rw::kinematics::Kinematics::frameTframe (
            device->getBase (), endFrame, state);
    return {{baseTtcp.P ()[0], baseTtcp.P ()[1], baseTtcp.P ()[2]}};
}

QPointF projectedTcp (
    const rw::models::Device* device,
    const rw::kinematics::Frame* tcpFrame,
    const rw::math::Q& q,
    const rw::kinematics::State& state,
    VisualProjection projection)
{
    return rws::projectEnvelopePosition (
        tcpPosition (device, tcpFrame, q, state), projection);
}

rw::math::Q clampQ (
    const rw::math::Q& q,
    const std::pair< rw::math::Q, rw::math::Q >& bounds)
{
    rw::math::Q result = q;
    for (std::size_t i = 0; i < result.size (); ++i)
        result[i] = std::max (bounds.first[i], std::min (bounds.second[i], result[i]));
    return result;
}

rw::math::Q optimizeDirection (
    const rw::models::Device* device,
    const rw::kinematics::Frame* tcpFrame,
    const rw::kinematics::State& state,
    const std::pair< rw::math::Q, rw::math::Q >& bounds,
    const rw::math::Q& seed,
    double angle,
    int coordinateIterations,
    VisualProjection projection,
    const std::shared_ptr< std::atomic< bool > >& cancel)
{
    rw::math::Q best = clampQ (seed, bounds);
    double bestValue = supportValue (
        projectedTcp (device, tcpFrame, best, state, projection), angle);
    rw::math::Q step (best.size ());
    for (std::size_t i = 0; i < step.size (); ++i)
        step[i] = std::max (1e-6, (bounds.second[i] - bounds.first[i]) * 0.25);

    for (int iter = 0; iter < coordinateIterations; ++iter) {
        if (cancel != nullptr && cancel->load ())
            return best;
        for (std::size_t joint = 0; joint < best.size (); ++joint) {
            if (cancel != nullptr && cancel->load ())
                return best;
            for (double sign : {-1.0, 1.0}) {
                rw::math::Q candidate = best;
                candidate[joint] += sign * step[joint];
                candidate = clampQ (candidate, bounds);
                const double value = supportValue (
                    projectedTcp (device, tcpFrame, candidate, state, projection), angle);
                if (value > bestValue) {
                    bestValue = value;
                    best = candidate;
                }
            }
        }
        for (std::size_t i = 0; i < step.size (); ++i)
            step[i] *= 0.5;
    }
    return best;
}

std::vector< rw::math::Q > envelopeSeeds (
    const std::pair< rw::math::Q, rw::math::Q >& bounds)
{
    const std::size_t dof = bounds.first.size ();
    rw::math::Q mid (dof);
    for (std::size_t i = 0; i < dof; ++i)
        mid[i] = 0.5 * (bounds.first[i] + bounds.second[i]);
    std::vector< rw::math::Q > seeds;
    seeds.push_back (mid);
    for (std::size_t i = 0; i < dof; ++i) {
        rw::math::Q low = mid;
        low[i] = bounds.first[i];
        seeds.push_back (low);
        rw::math::Q high = mid;
        high[i] = bounds.second[i];
        seeds.push_back (high);
    }
    return seeds;
}

}    // namespace

QPointF rws::projectEnvelopePosition (
    const std::array< double, 3 >& position,
    VisualProjection projection)
{
    switch (projection) {
        case VisualProjection::XY: return QPointF (position[0], position[1]);
        case VisualProjection::XZ: return QPointF (position[0], position[2]);
        case VisualProjection::YZ: return QPointF (position[1], position[2]);
    }
    return QPointF (position[0], position[1]);
}

AnalysisEnvelopeData rws::computeWorkspaceEnvelope (
    const rw::models::Device* device,
    const rw::kinematics::Frame* tcpFrame,
    const rw::kinematics::State& state,
    const WorkspaceEnvelopeConfig& config)
{
    AnalysisEnvelopeData envelope;
    envelope.projection = config.projection;
    if (device == nullptr || device->getBase () == nullptr || device->getEnd () == nullptr)
        return envelope;

    const int directions = std::max (12, config.angularDirections);
    const int iterations = std::max (1, config.coordinateIterations);
    const std::pair< rw::math::Q, rw::math::Q > bounds = device->getBounds ();
    const std::vector< rw::math::Q > seeds = envelopeSeeds (bounds);

    envelope.boundary.reserve (static_cast< std::size_t > (directions));
    for (int i = 0; i < directions; ++i) {
        // 检查取消请求
        if (config.cancel != nullptr && config.cancel->load ())
            return envelope;

        const double angle = 2.0 * rw::math::Pi * static_cast< double > (i) /
            static_cast< double > (directions);
        bool haveBest = false;
        rw::math::Q best = seeds.front ();
        double bestValue = -std::numeric_limits< double >::infinity ();
        for (const rw::math::Q& seed : seeds) {
            const rw::math::Q candidate = optimizeDirection (
                device, tcpFrame, state, bounds, seed, angle, iterations,
                config.projection, config.cancel);
            const QPointF point = projectedTcp (
                device, tcpFrame, candidate, state, config.projection);
            const double value = supportValue (point, angle);
            if (!haveBest || value > bestValue) {
                haveBest = true;
                bestValue = value;
                best = candidate;
            }
        }
        envelope.boundary.push_back (
            projectedTcp (device, tcpFrame, best, state, config.projection));
    }
    updateEnvelopeDimensions (envelope);
    return envelope;
}
