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
    std::vector< JointTransformSpec > transformJoints;
    std::vector< DHJointSpec > dhJoints;
    std::vector< DrawableSpec > drawables;
    std::vector< JointLimitSpec > limits;
    std::vector< PoseSpec > poses;
    DynamicModelSpec dynamics;
};

}    // namespace rws

#endif
