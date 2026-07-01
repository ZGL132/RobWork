#ifndef RWS_ROBOTMODELBUILDER_ROBOTMODELSPEC_HPP
#define RWS_ROBOTMODELBUILDER_ROBOTMODELSPEC_HPP

#include <array>
#include <string>
#include <vector>

namespace rws {

enum class RobotModelMode
{
    DH,
    JointRPYPos
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
};

struct JointLimitSpec
{
    std::string jointName;
    double posMinDeg;
    double posMaxDeg;
    double velMaxDeg;
    double accMaxDeg;
};

struct PoseSpec
{
    std::string name;
    std::array< double, 6 > qDeg;
};

struct RobotModelSpec
{
    std::string robotName;
    std::string saveDirectory;
    RobotModelMode mode;
    bool showFrameAxes;
    bool generateDrawables;
    bool generateScene;
    std::vector< DHJointSpec > dhJoints;
    std::vector< JointTransformSpec > transformJoints;
    std::vector< DrawableSpec > drawables;
    std::vector< JointLimitSpec > limits;
    std::vector< PoseSpec > poses;
};

}    // namespace rws

#endif
