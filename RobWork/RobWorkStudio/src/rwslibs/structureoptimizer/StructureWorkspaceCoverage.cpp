#include "StructureWorkspaceCoverage.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace rws {

namespace {

bool isUsable(const WorkspaceSample& sample)
{
    return !sample.inCollision &&
           (sample.status == AnalysisStatus::Pass ||
            sample.status == AnalysisStatus::Warning);
}

bool isValid(const WorkspaceCoverageBox& box)
{
    for (int axis = 0; axis < 3; ++axis) {
        if (box.cells[axis] <= 0 ||
            !std::isfinite(box.minimum[axis]) ||
            !std::isfinite(box.maximum[axis]) ||
            box.maximum[axis] <= box.minimum[axis]) {
            return false;
        }
    }
    return true;
}

std::size_t totalCells(const WorkspaceCoverageBox& box)
{
    return static_cast<std::size_t>(box.cells[0]) *
           static_cast<std::size_t>(box.cells[1]) *
           static_cast<std::size_t>(box.cells[2]);
}

bool cellFor(const WorkspaceSample& sample, const WorkspaceCoverageBox& box,
             std::size_t* cell)
{
    std::size_t index[3] = {0u, 0u, 0u};
    for (int axis = 0; axis < 3; ++axis) {
        const double coordinate = sample.tcpPosition[axis];
        if (!std::isfinite(coordinate) || coordinate < box.minimum[axis] ||
            coordinate > box.maximum[axis]) {
            return false;
        }
        const double fraction = (coordinate - box.minimum[axis]) /
                                (box.maximum[axis] - box.minimum[axis]);
        const std::size_t cells = static_cast<std::size_t>(box.cells[axis]);
        index[axis] = std::min(
            cells - 1u,
            static_cast<std::size_t>(std::floor(fraction * static_cast<double>(cells))));
    }
    *cell = index[0] + static_cast<std::size_t>(box.cells[0]) *
        (index[1] + static_cast<std::size_t>(box.cells[1]) * index[2]);
    return true;
}

} // namespace

StructureWorkspaceCoverageResult StructureWorkspaceCoverage::analyze(
    const std::vector<WorkspaceSample>& samples, const WorkspaceCoverageBox& box)
{
    StructureWorkspaceCoverageResult result;
    if (!isValid(box))
        return result;

    result.totalCellCount = totalCells(box);
    std::set<std::size_t> occupied;
    for (const WorkspaceSample& sample : samples) {
        std::size_t cell = 0u;
        if (isUsable(sample) && cellFor(sample, box, &cell))
            occupied.insert(cell);
    }
    result.occupiedCellCount = occupied.size();
    result.coverage = result.totalCellCount == 0u ? 0.0 :
        static_cast<double>(result.occupiedCellCount) /
        static_cast<double>(result.totalCellCount);
    return result;
}

double StructureWorkspaceCoverage::calculate(
    const std::vector<WorkspaceSample>& samples, const WorkspaceCoverageBox& box)
{
    return analyze(samples, box).coverage;
}

} // namespace rws
