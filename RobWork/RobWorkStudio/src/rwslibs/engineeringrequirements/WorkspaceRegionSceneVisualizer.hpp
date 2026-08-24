#ifndef RWS_ENGINEERINGREQUIREMENTS_WORKSPACEREGIONSCENEVISUALIZER_HPP
#define RWS_ENGINEERINGREQUIREMENTS_WORKSPACEREGIONSCENEVISUALIZER_HPP

#include <rw/core/Ptr.hpp>
#include <rw/graphics/DrawableNode.hpp>
#include <rw/graphics/WorkCellScene.hpp>
#include <rw/math/Transform3D.hpp>

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace rw { namespace kinematics { class Frame; } }

namespace rws {

//! 共享的区域单元可视状态，避免可视化器依赖运动学 Validate 类型。
enum class WorkspaceRegionVisualState
{
    Good,
    Weak,
    Failed,
    Unknown
};

struct WorkspaceRegionVisualCell
{
    std::array< double, 3 > position = {{0.0, 0.0, 0.0}};
    WorkspaceRegionVisualState state = WorkspaceRegionVisualState::Unknown;
};

struct WorkspaceRegionVisualSpec
{
    std::string id;
    std::string label;
    rw::math::Transform3D<> worldTReference;
    std::array< double, 3 > center = {{0.0, 0.0, 0.0}};
    std::array< double, 3 > size = {{0.1, 0.1, 0.1}};
    std::vector< WorkspaceRegionVisualCell > cells;
    std::size_t totalCellCount = 0;
    bool showCells = false;
};

static const std::size_t MaxWorkspaceRegionVisualCells = 2000;

//! 计算参考系下区域中心对应的世界变换。
rw::math::Transform3D<> workspaceRegionWorldTransform (
    const rw::math::Transform3D<>& worldTReference,
    const std::array< double, 3 >& center);

//! 为大量网格单元生成稳定的均匀抽样索引，并保留首尾单元。
std::vector< std::size_t > workspaceRegionDisplayIndices (
    std::size_t totalCount, std::size_t maxCount = MaxWorkspaceRegionVisualCells);

//! 返回图例使用的 RGBA 颜色，分量范围为 [0, 1]。
std::array< double, 4 > workspaceRegionColor (WorkspaceRegionVisualState state);

/**
 * @brief 当前区域的轻量三维 Drawable 管理器。
 *
 * 节点全部使用 Virtual | DrawableObject 掩码，仅用于辅助显示，不进入碰撞模型。
 */
class WorkspaceRegionSceneVisualizer
{
  public:
    WorkspaceRegionSceneVisualizer () = default;
    WorkspaceRegionSceneVisualizer (rw::graphics::WorkCellScene::Ptr scene,
                                    rw::kinematics::Frame* worldFrame);

    void setScene (rw::graphics::WorkCellScene::Ptr scene,
                   rw::kinematics::Frame* worldFrame);
    void clear ();
    bool show (const WorkspaceRegionVisualSpec& spec,
               const std::string& namePrefix,
               std::string* error = nullptr);

    std::size_t displayedCellCount () const { return _displayedCellCount; }
    std::size_t totalCellCount () const { return _totalCellCount; }

  private:
    rw::graphics::WorkCellScene::Ptr _scene;
    rw::kinematics::Frame* _worldFrame = nullptr;
    std::vector< rw::graphics::DrawableNode::Ptr > _nodes;
    std::size_t _displayedCellCount = 0;
    std::size_t _totalCellCount = 0;
};

}    // namespace rws

#endif    // RWS_ENGINEERINGREQUIREMENTS_WORKSPACEREGIONSCENEVISUALIZER_HPP
