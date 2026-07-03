// =============================================================================
//  RobotModelSpec.hpp
//  Plain data model for the RobotModelBuilder plugin.
// =============================================================================
#ifndef RWS_ROBOTMODELBUILDER_ROBOTMODELSPEC_HPP
#define RWS_ROBOTMODELBUILDER_ROBOTMODELSPEC_HPP

#include <array>
#include <cctype>
#include <string>
#include <vector>

namespace rws {

enum class KinematicsViewMode
{
    JointRPYPos,
    DHProjection
};

enum class JointKind
{
    Revolute,
    Prismatic,
    FixedFrame,
    ToolFrame,
    Unknown
};

// -----------------------------------------------------------------------------
//  SceneFrameType / PoseMode
//  说明: 场景 frame(Milestone 3 起)在 WorkCell 里使用:
//          * SceneFrameType::Normal  : 普通 frame,无可动/Daf 标记;
//          * SceneFrameType::Fixed   : 物理上固定(Loader 可能输出 type="Fixed");
//          * SceneFrameType::Movable : 可动物理对象,Loader 输出 type="Movable";
//          * PoseMode::RPYPos        : 用 <RPY>/<Pos> 输出位姿;
//          * PoseMode::Transform4x4  : 用 <Transform> 输出 4x4(本 Milestone 暂不在 XML 启用);
// -----------------------------------------------------------------------------
enum class SceneFrameType
{
    Normal,
    Fixed,
    Movable
};

enum class PoseMode
{
    RPYPos,
    Transform4x4
};

namespace detail {
inline bool iequals (const std::string& lhs, const char* rhs)
{
    std::size_t i = 0;
    while (i < lhs.size () && rhs[i] != '\0') {
        const unsigned char a = static_cast< unsigned char >(lhs[i]);
        const unsigned char b = static_cast< unsigned char >(rhs[i]);
        if (std::tolower (a) != std::tolower (b))
            return false;
        ++i;
    }
    return i == lhs.size () && rhs[i] == '\0';
}

inline std::string trimmed (const std::string& s)
{
    const std::size_t b = s.find_first_not_of (" \t\r\n");
    if (b == std::string::npos)
        return std::string ();
    const std::size_t e = s.find_last_not_of (" \t\r\n");
    return s.substr (b, e - b + 1);
}
}    // namespace detail

inline JointKind typeToKind (const std::string& type)
{
    const std::string t = detail::trimmed (type);
    if (detail::iequals (t, "Revolute"))
        return JointKind::Revolute;
    if (detail::iequals (t, "Prismatic"))
        return JointKind::Prismatic;
    if (detail::iequals (t, "FixedFrame"))
        return JointKind::FixedFrame;
    if (detail::iequals (t, "ToolFrame"))
        return JointKind::ToolFrame;
    return JointKind::Unknown;
}

inline bool isMovable (JointKind kind)
{
    return kind == JointKind::Revolute || kind == JointKind::Prismatic;
}

inline bool isRigidFrame (JointKind kind)
{
    return kind == JointKind::FixedFrame || kind == JointKind::ToolFrame;
}

// -----------------------------------------------------------------------------
//  SceneFrameType / PoseMode 字符串 ↔ enum 转换(Qt-free)
// -----------------------------------------------------------------------------
inline SceneFrameType sceneFrameTypeFromString (const std::string& type)
{
    const std::string t = detail::trimmed (type);
    if (detail::iequals (t, "Movable"))
        return SceneFrameType::Movable;
    if (detail::iequals (t, "Normal"))
        return SceneFrameType::Normal;
    return SceneFrameType::Fixed;
}

inline const char* sceneFrameTypeToString (SceneFrameType type)
{
    switch (type) {
        case SceneFrameType::Movable: return "Movable";
        case SceneFrameType::Normal:  return "Normal";
        case SceneFrameType::Fixed:
        default:                       return "Fixed";
    }
}

inline PoseMode poseModeFromString (const std::string& mode)
{
    const std::string m = detail::trimmed (mode);
    if (detail::iequals (m, "Transform4x4") || detail::iequals (m, "Transform"))
        return PoseMode::Transform4x4;
    return PoseMode::RPYPos;
}

inline const char* poseModeToString (PoseMode mode)
{
    return mode == PoseMode::Transform4x4 ? "Transform4x4" : "RPYPos";
}

struct KinematicRow
{
    std::string name;
    std::string type;
    std::array< double, 3 > rpyDeg;
    std::array< double, 3 > pos;
};

struct DHJointSpec
{
    std::string name;
    double alphaDeg;
    double a;
    double d;
    double offsetDeg;
};

struct JointTransformSpec
{
    std::string name;
    std::string type;
    std::array< double, 3 > rpyDeg;
    std::array< double, 3 > pos;
};

// -----------------------------------------------------------------------------
//  FrameSpec
//  说明: 一个场景 frame(在 Scene XML 中以 <Frame> 出现)。
//        refframe 必须是 WORLD / RobotBase / 同 spec.sceneFrames 已存在的 name;
//        设备内部 frame(Base / Joint* / TCP / ToolFrame)不能被场景 frame 引用,
//        反之亦然(Milestone 3 边界)。
// -----------------------------------------------------------------------------
struct FrameSpec
{
    std::string name;                                                // frame 名称
    std::string refFrame;                                            // 父系 frame 名
    SceneFrameType frameType = SceneFrameType::Fixed;
    bool daf = false;                                                // 物理 Daf 物体标记
    PoseMode poseMode = PoseMode::RPYPos;                            // 位姿表达模式
    std::array< double, 3 > rpyDeg = {{0, 0, 0}};                    // RPY(度,RobotBase/Z-Y-X 顺序)
    std::array< double, 3 > pos = {{0, 0, 0}};                       // 平移(米)
    std::array< double, 16 > transform = {{1, 0, 0, 0,
                                           0, 1, 0, 0,
                                           0, 0, 1, 0,
                                           0, 0, 0, 1}};             // PoseMode::Transform4x4 用
};

struct DrawableSpec
{
    std::string name;
    std::string refFrame;
    std::string shape;
    double radius;
    double length;
    std::array< double, 3 > rpyDeg;
    std::array< double, 3 > pos;
    std::array< double, 3 > rgb;
    bool collisionModel;
    bool autoLinkGeometry = false;
};

struct JointLimitSpec
{
    std::string jointName;
    double posMin;
    double posMax;
    double velMax;
    double accMax;
};

struct PoseSpec
{
    std::string name;
    std::vector< double > q;
};

struct LinkDynamicsSpec
{
    std::string linkName;
    std::string objectName;
    double mass;
    std::array< double, 3 > cog;
    std::array< double, 6 > inertia;
    bool estimateInertia;
    std::string material;
};

struct JointForceLimitSpec
{
    std::string jointName;
    double maxForce;
};

struct DynamicModelSpec
{
    bool generateDynamicWorkCell = false;
    std::string baseFrame        = "Base";
    std::string baseMaterial     = "Steel";
    std::vector< LinkDynamicsSpec > links;
    std::vector< JointForceLimitSpec > forceLimits;
};

struct RobotModelSpec
{
    std::string robotName;
    std::string saveDirectory;
    KinematicsViewMode mode;
    bool exportDhJointsAdvanced = false;
    bool showFrameAxes;
    bool generateDrawables;
    bool generateScene;
    FrameSpec robotBaseFrame;                                        // Milestone 3:场景 RobotBase
    std::vector< FrameSpec > sceneFrames;                            // Milestone 3:场景 frame 列表
    std::vector< JointTransformSpec > transformJoints;
    std::vector< DHJointSpec > dhJoints;
    std::vector< DrawableSpec > drawables;
    std::vector< JointLimitSpec > limits;
    std::vector< PoseSpec > poses;
    DynamicModelSpec dynamics;
};

}    // namespace rws

#endif
