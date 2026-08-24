#include "WorkspaceRegionSceneVisualizer.hpp"

#include <rw/geometry/Line.hpp>
#include <rw/kinematics/Frame.hpp>
#include <rw/math/Vector3D.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

namespace {

const int regionVisualMask = rw::graphics::DrawableNode::Virtual |
                              rw::graphics::DrawableNode::DrawableObject;

bool finitePositive (const std::array< double, 3 >& value)
{
    for (const double component : value) {
        if (!std::isfinite (component) || component <= 0.0)
            return false;
    }
    return true;
}

rw::math::Vector3D<> point (const std::array< double, 3 >& value)
{
    return rw::math::Vector3D<> (value[0], value[1], value[2]);
}

std::vector< rw::geometry::Line > boxLines (const std::array< double, 3 >& size)
{
    const double x = size[0] * 0.5;
    const double y = size[1] * 0.5;
    const double z = size[2] * 0.5;
    const std::array< rw::math::Vector3D<> , 8 > corners = {{
        rw::math::Vector3D<> (-x, -y, -z), rw::math::Vector3D<> (x, -y, -z),
        rw::math::Vector3D<> (x, y, -z), rw::math::Vector3D<> (-x, y, -z),
        rw::math::Vector3D<> (-x, -y, z), rw::math::Vector3D<> (x, -y, z),
        rw::math::Vector3D<> (x, y, z), rw::math::Vector3D<> (-x, y, z)
    }};
    const int edges[12][2] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
        {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}
    };
    std::vector< rw::geometry::Line > lines;
    lines.reserve (12);
    for (const auto& edge : edges)
        lines.emplace_back (corners[edge[0]], corners[edge[1]]);
    return lines;
}

void appendCross (std::vector< rw::geometry::Line >& lines,
                  const std::array< double, 3 >& position, double halfSize)
{
    const rw::math::Vector3D<> center = point (position);
    lines.emplace_back (center - rw::math::Vector3D<> (halfSize, 0.0, 0.0),
                        center + rw::math::Vector3D<> (halfSize, 0.0, 0.0));
    lines.emplace_back (center - rw::math::Vector3D<> (0.0, halfSize, 0.0),
                        center + rw::math::Vector3D<> (0.0, halfSize, 0.0));
    lines.emplace_back (center - rw::math::Vector3D<> (0.0, 0.0, halfSize),
                        center + rw::math::Vector3D<> (0.0, 0.0, halfSize));
}

}    // namespace

namespace rws {

rw::math::Transform3D<> workspaceRegionWorldTransform (
    const rw::math::Transform3D<>& worldTReference,
    const std::array< double, 3 >& center)
{
    return worldTReference * rw::math::Transform3D<> (point (center));
}

std::vector< std::size_t > workspaceRegionDisplayIndices (
    const std::size_t totalCount, const std::size_t maxCount)
{
    if (totalCount == 0 || maxCount == 0)
        return {};
    if (totalCount <= maxCount) {
        std::vector< std::size_t > result (totalCount);
        for (std::size_t i = 0; i < totalCount; ++i)
            result[i] = i;
        return result;
    }
    std::vector< std::size_t > result;
    result.reserve (maxCount);
    for (std::size_t i = 0; i < maxCount; ++i) {
        const std::size_t index = (i * (totalCount - 1)) / (maxCount - 1);
        if (result.empty () || result.back () != index)
            result.push_back (index);
    }
    return result;
}

std::array< double, 4 > workspaceRegionColor (const WorkspaceRegionVisualState state)
{
    switch (state) {
        case WorkspaceRegionVisualState::Good:    return {{0.15, 0.80, 0.25, 0.85}};
        case WorkspaceRegionVisualState::Weak:    return {{1.00, 0.72, 0.10, 0.90}};
        case WorkspaceRegionVisualState::Failed:  return {{0.90, 0.15, 0.12, 0.90}};
        case WorkspaceRegionVisualState::Unknown:
        default:                                   return {{0.55, 0.55, 0.55, 0.75}};
    }
}

WorkspaceRegionSceneVisualizer::WorkspaceRegionSceneVisualizer (
    rw::graphics::WorkCellScene::Ptr scene, rw::kinematics::Frame* worldFrame) :
    _scene (scene), _worldFrame (worldFrame)
{}

void WorkspaceRegionSceneVisualizer::setScene (
    rw::graphics::WorkCellScene::Ptr scene, rw::kinematics::Frame* worldFrame)
{
    clear ();
    _scene = scene;
    _worldFrame = worldFrame;
}

void WorkspaceRegionSceneVisualizer::clear ()
{
    if (!_scene.isNull ()) {
        for (const rw::graphics::DrawableNode::Ptr& node : _nodes)
            if (!node.isNull ())
                _scene->removeDrawable (node);
    }
    _nodes.clear ();
    _displayedCellCount = 0;
    _totalCellCount = 0;
}

bool WorkspaceRegionSceneVisualizer::show (const WorkspaceRegionVisualSpec& spec,
                                           const std::string& namePrefix,
                                           std::string* error)
{
    clear ();
    if (_scene.isNull () || _worldFrame == nullptr) {
        if (error != nullptr) *error = "3D scene or WORLD frame is unavailable.";
        return false;
    }
    if (spec.id.empty () || !finitePositive (spec.size)) {
        if (error != nullptr) *error = "Workspace region id or size is invalid.";
        return false;
    }

    const rw::math::Transform3D<> worldTRegion =
        workspaceRegionWorldTransform (spec.worldTReference, spec.center);
    rw::graphics::DrawableGeometryNode::Ptr boundary = _scene->addLines (
        namePrefix + spec.id + ".bounds", boxLines (spec.size), _worldFrame,
        regionVisualMask);
    boundary->setColor (0.15, 0.55, 1.0, 0.90);
    boundary->setDrawType (rw::graphics::DrawableNode::WIRE);
    boundary->setTransform (worldTRegion);
    _nodes.push_back (boundary);

    const rw::graphics::DrawableNode::Ptr axis = _scene->addFrameAxis (
        namePrefix + spec.id + ".axis", std::max (0.02, std::min (spec.size[0],
        std::min (spec.size[1], spec.size[2])) * 0.35), _worldFrame, regionVisualMask);
    axis->setTransform (worldTRegion);
    _nodes.push_back (axis);

    const std::string label = spec.label.empty () ? spec.id : spec.label;
    std::ostringstream caption;
    caption << label << "  [" << spec.size[0] << " x " << spec.size[1]
            << " x " << spec.size[2] << "]";
    const rw::graphics::DrawableNode::Ptr text = _scene->addText (
        namePrefix + spec.id + ".label", caption.str (), _worldFrame, regionVisualMask);
    text->setTransform (worldTRegion * rw::math::Transform3D<> (
        rw::math::Vector3D<> (0.0, 0.0, spec.size[2] * 0.6)));
    _nodes.push_back (text);

    _totalCellCount = spec.totalCellCount == 0 ? spec.cells.size () : spec.totalCellCount;
    if (!spec.showCells || spec.cells.empty ())
        return true;

    const std::vector< std::size_t > indices = workspaceRegionDisplayIndices (
        spec.cells.size (), MaxWorkspaceRegionVisualCells);
    std::array< std::vector< rw::geometry::Line >, 4 > linesByState;
    const double minSize = std::min (spec.size[0], std::min (spec.size[1], spec.size[2]));
    const double halfSize = std::max (0.001, minSize * 0.025);
    for (const std::size_t index : indices) {
        const WorkspaceRegionVisualCell& cell = spec.cells[index];
        const int stateIndex = static_cast< int > (cell.state);
        if (stateIndex >= 0 && stateIndex < static_cast< int > (linesByState.size ()))
            appendCross (linesByState[static_cast< std::size_t > (stateIndex)],
                         cell.position, halfSize);
    }
    for (std::size_t i = 0; i < linesByState.size (); ++i) {
        if (linesByState[i].empty ())
            continue;
        rw::graphics::DrawableGeometryNode::Ptr cells = _scene->addLines (
            namePrefix + spec.id + ".cells." + std::to_string (i), linesByState[i],
            _worldFrame, regionVisualMask);
        const std::array< double, 4 > color = workspaceRegionColor (
            static_cast< WorkspaceRegionVisualState > (i));
        cells->setColor (color[0], color[1], color[2], color[3]);
        cells->setDrawType (rw::graphics::DrawableNode::SOLID);
        _nodes.push_back (cells);
    }
    _displayedCellCount = indices.size ();
    return true;
}

}    // namespace rws
