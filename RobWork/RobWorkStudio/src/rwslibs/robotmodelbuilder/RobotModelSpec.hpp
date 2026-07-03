// =============================================================================
//  文件: RobotModelSpec.hpp
//  说明: 整个 RobotModelBuilder 插件的"数据模型"层。这里只放纯数据结构,
//        用来描述一个 6 轴机器人模型的所有可配置项:
//           - 基本信息(名字、保存路径、UI 视图模式)
//           - 关节(SE(3) Joint+RPY+Pos 唯一真值 + DH 投影视图缓存)
//           - Drawable(可视化几何体)
//           - 关节限位与预设位姿
//           - 动力学参数(用于生成 DynamicWorkCell)
//        数据真值约定:
//           * transformJoints(SE(3))是唯一可编辑真值;
//           * dhJoints 仅作为 DH 投影视图缓存,由 transformJoints 派生;
//           * 默认 XML 输出 <Joint>+<RPY>/<Pos>;
//           * <DHJoint> 仅作为隐藏高级选项(全部 Revolute + 无损投影时)输出。
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
//  KinematicsViewMode
//  说明: 仅用于决定 UI 上哪个表可见:
//        - JointRPYPos: 显示 SE(3) Joint+RPY+Pos 表(DH 表隐藏)
//        - DHProjection: 显示 DH 投影视图表(SE(3) 表隐藏;但 SE(3) 仍是真值,
//                       用户在 DH 表上的编辑会被忽略)
//        注意:这不再是"建模方式"开关;建模方式已经由 transformJoints
//        决定,DH 永远是派生视图。
// -----------------------------------------------------------------------------
enum class KinematicsViewMode
{
    JointRPYPos,   // UI 默认:显示 SE(3) Joint+RPY+Pos 表
    DHProjection   // UI 备用:显示 DH 投影视图(只读)
};

// -----------------------------------------------------------------------------
//  JointKind
//  说明: 关节在运动学链上的语义角色(本期仅作为文档性枚举;
//        transformJoints 内的 type 仍以 std::string 表达 "Revolute"/"Prismatic",
//        后续可平滑迁移到 enum 表达)。
// -----------------------------------------------------------------------------
enum class JointKind
{
    Revolute,     // 旋转关节
    Prismatic,    // 移动关节
    FixedFrame,   // 固定参考帧(无自由度,只占位)
    ToolFrame     // 末端工具参考帧
};

// -----------------------------------------------------------------------------
//  KinematicRow
//  说明: SE(3) 关节的"一行真值"表示,作为未来把 transformJoints/dhJoints
//        合并到单一 vector 的目标结构。本期暂不替换 vector 字段,但已经
//        在头文件里确立"单一真值"的语义:
//           * name  : 关节名
//           * type  : 关节类型(以字符串表达,与 RobWork XML 兼容)
//           * rpyDeg: 父系 -> 本系 的 RPY(Z-Y-X 顺序,度)
//           * pos   : 父系 -> 本系 的平移(米)
//        后续会替换 transformJoints / dhJoints 这两个并列 vector。
// -----------------------------------------------------------------------------
struct KinematicRow
{
    std::string name;                // 关节名(同时作为 XML 节点名)
    std::string type;                // 关节类型,如 "Revolute" / "Prismatic"
    std::array< double, 3 > rpyDeg;  // 父系 -> 本系 的 RPY(度)
    std::array< double, 3 > pos;     // 父系 -> 本系 的平移(米)
};

// -----------------------------------------------------------------------------
//  DHJointSpec
//  说明: 单个 DH 关节的"投影视图"参数集合;不再作为建模真值,也不再有
//        对应的独立 XML 输出模式(默认 XML 永远输出 <Joint>):
//        - alphaDeg : 绕 X_{i-1} 旋转到 Z_{i-1} 与 X_i 平行的扭转角(度)
//        - a        : 沿 X_i 的连杆长度
//        - d        : 沿 Z_{i-1} 的连杆偏距
//        - offsetDeg: 关节零位偏移角(theta0)
//        UI 上由 transformJoints 投影得到,用户在 DH 表上的编辑会被忽略。
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
//  说明: SE(3) 关节的"唯一真值"表示,对应默认输出的 RobWork <Joint> 节点:
//        - type   : Revolute/Prismatic 等
//        - rpyDeg : 从父关节坐标系到本关节坐标系的 Z-Y-X 欧拉角(度)
//        - pos    : 同上,平移分量(米)
//        UI 上是唯一可编辑表;DH 表由本结构派生。
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
//        数据真值约定:
//          * transformJoints  是 SE(3) 唯一真值;
//          * dhJoints        是 transformJoints 派生出的 DH 投影缓存,只读;
//          * mode            仅决定 UI 上哪个表可见,不影响真值与 XML 输出;
//          * 默认 XML 永远输出 <Joint>+<RPY>/<Pos>;
//          * 只有当 exportDhJointsAdvanced=true 且 canExportDhJoints() 通过时,
//            才输出 <DHJoint>;否则回退到默认 <Joint>。
// -----------------------------------------------------------------------------
struct RobotModelSpec
{
    std::string robotName;                          // 机器人名,会作为 .wc.xml 文件名前缀
    std::string saveDirectory;                      // XML 保存目录
    KinematicsViewMode mode;                        // UI view mode(JointRPYPos / DHProjection)
    bool exportDhJointsAdvanced = false;            // Hidden advanced export: write <DHJoint> only if lossless.
    bool showFrameAxes;                             // 是否在每个 Frame/Joint 上画坐标轴
    bool generateDrawables;                         // 是否输出 <Drawable> 节点
    bool generateScene;                             // 是否额外生成 Scene.wc.xml
    std::vector< JointTransformSpec > transformJoints;  // SE(3) 真值(唯一可编辑)
    std::vector< DHJointSpec > dhJoints;                // DH 投影视图缓存(由 transformJoints 派生,只读)
    std::vector< DrawableSpec > drawables;          // 全部 Drawable
    std::vector< JointLimitSpec > limits;           // 全部关节限位
    std::vector< PoseSpec > poses;                  // 全部预设位姿
    DynamicModelSpec dynamics;                      // 动力学参数
};

}    // namespace rws

#endif
