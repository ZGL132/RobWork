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
    bool autoLinkGeometry = false;
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

// 动力学：单个 link 的物理参数
// inertia 6 个数：Ixx Iyy Izz Ixy Ixz Iyz（主轴惯量 + 惯量积）
struct LinkDynamicsSpec
{
    std::string linkName;     // 显示名，仅 UI/日志用
    std::string objectName;   // 必须匹配 .wc.xml 中 Frame/Joint 名称，例如 Joint1
    double mass;              // kg
    std::array< double, 3 > cog;                 // 质心 (x, y, z) m
    std::array< double, 6 > inertia;             // (Ixx, Iyy, Izz, Ixy, Ixz, Iyz)
    bool estimateInertia;     // 若 true，RobWorkSim 自行估算（需要几何）
    std::string material;     // 材料名，例如 Aluminum
};

struct JointForceLimitSpec
{
    std::string jointName;
    double maxForce;          // Nm（旋转）或 N（移动）
};

// 动力学整体开关 + 数据
struct DynamicModelSpec
{
    bool generateDynamicWorkCell = false;   // 是否输出 .dwc.xml
    std::string baseFrame        = "Base"; // 基座 frame 引用
    std::string baseMaterial     = "Steel";
    std::vector< LinkDynamicsSpec > links;
    std::vector< JointForceLimitSpec > forceLimits;
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
    DynamicModelSpec dynamics;
};

}    // namespace rws

#endif
