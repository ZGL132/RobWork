#include "WorkspaceSamplingGrid.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace rws {

bool resolveWorkspaceSamplingGrid (
    const std::array< double, 3 >& size,
    const std::array< double, 3 >& sampleSpacingMeters,
    const RequirementVerificationStage verificationStage,
    WorkspaceSamplingGrid& grid,
    std::string* error)
{
    WorkspaceSamplingGrid resolved;
    const int minimumPoints = verificationStage == RequirementVerificationStage::Verified ? 2 : 1;
    for (std::size_t axis = 0; axis < size.size (); ++axis) {
        if (!std::isfinite (size[axis]) || size[axis] <= 0.0 ||
            !std::isfinite (sampleSpacingMeters[axis]) || sampleSpacingMeters[axis] <= 0.0) {
            if (error != nullptr) *error = "Workspace region size and sample spacing must be finite and positive.";
            return false;
        }
        const double segments = std::ceil (size[axis] / sampleSpacingMeters[axis]);
        if (!std::isfinite (segments) ||
            segments > static_cast< double > (std::numeric_limits< int >::max () - 1)) {
            if (error != nullptr) *error = "Workspace region sample spacing resolves to an invalid point count.";
            return false;
        }
        const int count = std::max (minimumPoints, static_cast< int > (segments) + 1);
        if (count > MaxWorkspaceSamplesPerAxis) {
            if (error != nullptr) *error = "Workspace region sample spacing exceeds the per-axis point limit.";
            return false;
        }
        resolved.pointCounts[axis] = count;
        const std::size_t countAsSize = static_cast< std::size_t > (count);
        if (resolved.totalPointCount != 0 &&
            resolved.totalPointCount > std::numeric_limits< std::size_t >::max () / countAsSize) {
            if (error != nullptr) *error = "Workspace region sample grid is too large.";
            return false;
        }
        resolved.totalPointCount = resolved.totalPointCount == 0 ? countAsSize :
            resolved.totalPointCount * countAsSize;
    }
    grid = resolved;
    return true;
}

}    // namespace rws
