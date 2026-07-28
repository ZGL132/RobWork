#ifndef RWS_STRUCTUREOPTIMIZATION_STRUCTUREWORKSPACECOVERAGE_HPP
#define RWS_STRUCTUREOPTIMIZATION_STRUCTUREWORKSPACECOVERAGE_HPP

#include "StructureOptimizationTypes.hpp"

#include <cstddef>
#include <vector>

namespace rws {

struct StructureWorkspaceCoverageResult
{
    double coverage = 0.0;
    std::size_t occupiedCellCount = 0;
    std::size_t totalCellCount = 0;
};

//! Calculates box-cell coverage from collision-free usable workspace samples.
class StructureWorkspaceCoverage
{
  public:
    static StructureWorkspaceCoverageResult analyze(
        const std::vector<WorkspaceSample>& samples,
        const WorkspaceCoverageBox& box);

    static double calculate(const std::vector<WorkspaceSample>& samples,
                            const WorkspaceCoverageBox& box);
};

} // namespace rws

#endif // RWS_STRUCTUREOPTIMIZATION_STRUCTUREWORKSPACECOVERAGE_HPP
