#ifndef RWS_ENGINEERINGREQUIREMENTS_WORKSPACESAMPLINGGRID_HPP
#define RWS_ENGINEERINGREQUIREMENTS_WORKSPACESAMPLINGGRID_HPP

#include "EngineeringRequirementTypes.hpp"

#include <array>
#include <cstddef>
#include <string>

namespace rws {

/**
 * @brief 冻结前将工作区域尺寸和三轴采样间距解析为包含边界的 XYZ 网格。
 */
struct WorkspaceSamplingGrid
{
    std::array< int, 3 > pointCounts = {{0, 0, 0}};
    std::size_t totalPointCount = 0;
};

bool resolveWorkspaceSamplingGrid (
    const std::array< double, 3 >& size,
    const std::array< double, 3 >& sampleSpacingMeters,
    RequirementVerificationStage verificationStage,
    WorkspaceSamplingGrid& grid,
    std::string* error = nullptr);

}    // namespace rws

#endif    // RWS_ENGINEERINGREQUIREMENTS_WORKSPACESAMPLINGGRID_HPP
