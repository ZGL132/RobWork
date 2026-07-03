// =============================================================================
//  文件: RobotModelSpec.hpp
//  说明: 整个 RobotModelBuilder 插件的"数据模型"层。这里只放纯数据结构,
//        用来描述一个 6 轴机器人模型的所有可配置项:
//           - 基本信息(名字、保存路径、显示模式)
//           - 关节(DH 参数 / Joint+RPY+Pos 两种建模方式)
//           - Drawable(可视化几何体)
//           - 关节限位与预设位姿
//           - 动力学参数(用于生成 DynamicWorkCell)
//        UI 层(Wdiget)负责把数据展示为表单,XML 层(XmlWriter)负责把它
//        序列化为 RobWork / RobWorkSim 能识别的 XML 文件。
// =============================================================================
#ifndef RWS_ROBOTMODELBUILDER_ROBOTMODELSPEC_HPP
#define RWS_ROBOTMODELBUILDER_ROBOTMODELSPEC_HPP

#include <array>
#include <string>
#include <vector>

namespace rws {

// -----------------------------------------------------------------------------
//  RobotModelMode
//  说明: 描述机器人建模方式,二者只选其一:
//        - DH         : 用标准 DH 参数(alpha/a/d/offset)建模,序列化为 <DHJoint>
//        - JointRPYPos: 用 RobWork 的 <Joint> + <RPY>/<Pos> 直接建模
// -----------------------------------------------------------------------------
enum class RobotModelMode
{
    DH,           // 标准 DH 参数建模(对应 XML 中的 <DHJoint type="schilling">)
    JointRPYPos   // 关节+RPY+位置建模(对应 XML 中的 <Joint>+<RPY>/<Pos>)
};

// -----------------------------------------------------------------------------
//  DHJointSpec
//  说明: 单个 DH 关节的参数集合,对应 RobWork <DHJoint> 节点:
//        - alphaDeg : 绕 X_{i-1} 旋转到 Z_{i-1} 与 X_i 平行的扭转角(度)
//        - a        : 沿 X_i 的连杆长度
//        - d        : 沿 Z_{i-1} 的连杆偏距
//        - offsetDeg: 关节零位偏移角(theta0)
// -----------------------------------------------------------------------------
struct DHJointSpec
{
    std::string name;        // 关节名(如 "Joint1"),会写入 XML
    double alphaDeg;         // alpha(度)
    double a;                // a(m)
    double d;                // d(m)
    double offsetDeg;        // 零位偏移角(度)
};

// -----------------------------------------------------------------------------
//  JointTransformSpec
//  说明: 单个 Joint+RPY+Pos 关节的描述,对应 RobWork <Joint> 节点:
//        - type   : Revolute/Prismatic 等
//        - rpyDeg : 从父关节坐标系到本关节坐标系的 Z-Y-X 欧拉角(度)
//        - pos    : 同上,平移分量(米)
// -----------------------------------------------------------------------------
struct JointTransformSpec
{
    std::string name;                // 关节名
    std::string type;                // 关节类型(常见 Revolute / Prismatic)
    std::array< double, 3 > rpyDeg;  // 父系 -> 本系 的 RPY(Z-Y-X, 度)
    std::array< double, 3 > pos;     // 父系 -> 本系 的平移(米)
};

// -----------------------------------------------------------------------------
//  DrawableSpec
//  说明: 一个可视化几何体(目前固定为圆柱 Cylinder),对应 <Drawable> 节点:
//        - shape          : 仅作语义标签保留,目前固定 "Cylinder"
//        - radius/length  : 圆柱半径与长度
//        - rpyDeg/pos     : 在参考坐标系下的位姿
//        - rgb            : 颜色 RGB(0~1)
//        - collisionModel : 是否同时作为碰撞模型
//        - autoLinkGeometry: 若为 true,表示此 Drawable 是自动生成的连杆圆柱,
//                            在保存前由 applyLinkGeometry() 根据关节几何重新计算
//                              pos/rpyDeg/length
// -----------------------------------------------------------------------------
struct DrawableSpec
{
    std::string name;                // Drawable 名称(如 "Link1To2")
    std::string refFrame;            // 相对哪个 Frame/关节 放置(必须已存在)
    std::string shape;               // 形状标签,目前固定 "Cylinder"
    double radius;                   // 半径(米)
    double length;                   // 长度(米)
    std::array< double, 3 > rpyDeg;  // 在 refFrame 下的 RPY(度)
    std::array< double, 3 > pos;     // 在 refFrame 下的位置(米)
    std::array< double, 3 > rgb;     // RGB 颜色,每通道 0~1
    bool collisionModel;             // 是否同时生成碰撞模型
    bool autoLinkGeometry = false;   // 标记该 Drawable 是否由建模系统自动生成
};

// -----------------------------------------------------------------------------
//  JointLimitSpec
//  说明: 单个关节的位置/速度/加速度限制,会序列化为 <PosLimit>/<VelLimit>/<AccLimit>
//        注意:在写 XML 之前会做 度 -> 弧度 的转换。
// -----------------------------------------------------------------------------
struct JointLimitSpec
{
    std::string jointName;       // 关联的关节名
    double posMinDeg;            // 最小位置(度)
    double posMaxDeg;            // 最大位置(度)
    double velMaxDeg;            // 最大速度(度/秒)
    double accMaxDeg;            // 最大加速度(度/秒^2)
};

// -----------------------------------------------------------------------------
//  PoseSpec
//  说明: 一个命名预设位姿,对应 XML 中的 <Q name="...">...</Q>。
//        qDeg 长度为 6,与 JointCount 一一对应(单位:度)。
// -----------------------------------------------------------------------------
struct PoseSpec
{
    std::string name;                // 位姿名(如 "Zero" / "Ready")
    std::array< double, 6 > qDeg;    // 6 个关节角(度)
};

// -----------------------------------------------------------------------------
//  LinkDynamicsSpec
//  说明: 动力学:单个 link 的物理参数,用于生成 <RigidDevice>/<Link>。
//        inertia 6 个数:Ixx Iyy Izz Ixy Ixz Iyz(主轴惯量 + 惯量积)。
//        当 estimateInertia = true 时,RobWorkSim 会基于几何自动估算惯量,
//        此时可以填占位值(惯量会被忽略)。
// -----------------------------------------------------------------------------
struct LinkDynamicsSpec
{
    std::string linkName;        // 显示名,仅 UI/日志用
    std::string objectName;      // 必须匹配 .wc.xml 中 Frame/Joint 名称,例如 Joint1
    double mass;                 // 质量(kg)
    std::array< double, 3 > cog;          // 质心(米),在 objectName 坐标系下
    std::array< double, 6 > inertia;      // (Ixx, Iyy, Izz, Ixy, Ixz, Iyz)
    bool estimateInertia;        // 若 true,RobWorkSim 自行估算(需要几何)
    std::string material;        // 材料名,例如 Aluminum
};

// -----------------------------------------------------------------------------
//  JointForceLimitSpec
//  说明: 单个关节的最大力/力矩限制,会序列化为 <ForceLimit joint="...">。
//        旋转关节单位 Nm,移动关节单位 N。
// -----------------------------------------------------------------------------
struct JointForceLimitSpec
{
    std::string jointName;       // 关节名
    double maxForce;             // Nm(旋转)或 N(移动)
};

// -----------------------------------------------------------------------------
//  DynamicModelSpec
//  说明: 动力学整体开关 + 全部 link / 力限数据。
//        generateDynamicWorkCell 控制是否输出 .dwc.xml。
// -----------------------------------------------------------------------------
struct DynamicModelSpec
{
    bool generateDynamicWorkCell = false;     // 是否输出 .dwc.xml
    std::string baseFrame        = "Base";   // 基座 frame 引用,用于 <KinematicBase>
    std::string baseMaterial     = "Steel";  // 基座材料,用于 <MaterialID>
    std::vector< LinkDynamicsSpec > links;            // 全部 link 的动力学参数
    std::vector< JointForceLimitSpec > forceLimits;   // 全部关节的力限
};

// -----------------------------------------------------------------------------
//  RobotModelSpec
//  说明: 把上述所有片段组装成"一个完整的机器人模型"。
//        Widget 会从这个对象读数据/写数据;XmlWriter 把它变成 XML。
// -----------------------------------------------------------------------------
struct RobotModelSpec
{
    std::string robotName;                          // 机器人名,会作为 .wc.xml 文件名前缀
    std::string saveDirectory;                      // XML 保存目录
    RobotModelMode mode;                            // 建模方式(DH / JointRPYPos)
    bool showFrameAxes;                             // 是否在每个 Frame/Joint 上画坐标轴
    bool generateDrawables;                         // 是否输出 <Drawable> 节点
    bool generateScene;                             // 是否额外生成 Scene.wc.xml
    std::vector< DHJointSpec > dhJoints;            // DH 关节列表(mode = DH 时使用)
    std::vector< JointTransformSpec > transformJoints;  // Joint+RPY+Pos 列表(mode = JointRPYPos 时使用)
    std::vector< DrawableSpec > drawables;          // 全部 Drawable
    std::vector< JointLimitSpec > limits;           // 全部关节限位
    std::vector< PoseSpec > poses;                  // 全部预设位姿
    DynamicModelSpec dynamics;                      // 动力学参数
};

}    // namespace rws

#endif