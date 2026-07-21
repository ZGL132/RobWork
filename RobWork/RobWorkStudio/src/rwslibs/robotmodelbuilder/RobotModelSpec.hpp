// =============================================================================
//  文件: RobotModelSpec.hpp
//  说明: RobotModelBuilder 插件的纯数据模型定义文件 (Plain Data Model)。
//        本文件定义了机器人运动学、几何模型、场景参考系、动力学及碰撞配置等
//        所有在 UI 与 XML 生成器之间传递的数据结构。
// =============================================================================
#ifndef RWS_ROBOTMODELBUILDER_ROBOTMODELSPEC_HPP
#define RWS_ROBOTMODELBUILDER_ROBOTMODELSPEC_HPP

#include <array>
#include <cctype>
#include <string>
#include <vector>

namespace rws {

/// 运动学视图模式: JointRPYPos (SE(3)真值模式) 或 DHProjection (DH投影模式)
enum class KinematicsViewMode
{
    JointRPYPos,   ///< 基于关节 + RPY 欧拉角 + Pos 平移的 SE(3) 真值模式
    DHProjection   ///< 基于 DH (Denavit-Hartenberg) 参数的投影视图模式
};

/// 关节/参考系类型
enum class JointKind
{
    Revolute,      ///< 旋转关节
    Prismatic,     ///< 移动关节
    FixedFrame,    ///< 固定参考系
    ToolFrame,     ///< 工具参考系 (TCP 末端)
    Unknown        ///< 未知类型
};

// -----------------------------------------------------------------------------
//  SceneFrameType / PoseMode
//  说明: 场景 frame (Milestone 3 起) 在 WorkCell 里使用:
//          * SceneFrameType::Normal  : 普通 frame，无可动/Daf 标记;
//          * SceneFrameType::Fixed   : 物理上固定 (Loader 输出 type="Fixed");
//          * SceneFrameType::Movable : 可动物理对象 (Loader 输出 type="Movable");
//          * PoseMode::RPYPos        : 用 <RPY>/<Pos> 输出位姿;
//          * PoseMode::Transform4x4  : 用 <Transform> 输出 4x4 阵;
// -----------------------------------------------------------------------------

/// 场景参考系的物理属性类型
enum class SceneFrameType
{
    Normal,        ///< 普通参考系 (无显式类型标记)
    Fixed,         ///< 固定物理参考系 (type="Fixed")
    Movable        ///< 可动物理参考系 (type="Movable")
};

/// 位姿表达模式
enum class PoseMode
{
    RPYPos,        ///< 使用 <RPY> 欧拉角 + <Pos> 位移向量表示
    Transform4x4   ///< 使用 <Transform> 4x4 齐次变换矩阵表示
};

namespace detail {
/// 辅助函数: 忽略大小写的字符串比较
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

/// 辅助函数: 移除字符串首尾空白字符
inline std::string trimmed (const std::string& s)
{
    const std::size_t b = s.find_first_not_of (" \t\r\n");
    if (b == std::string::npos)
        return std::string ();
    const std::size_t e = s.find_last_not_of (" \t\r\n");
    return s.substr (b, e - b + 1);
}
}    // namespace detail

/// 将字符串转换为 JointKind 枚举
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

/// 判断关节类型是否为可动关节 (Revolute 或 Prismatic)
inline bool isMovable (JointKind kind)
{
    return kind == JointKind::Revolute || kind == JointKind::Prismatic;
}

/// 判断关节类型是否为刚性固定参考系 (FixedFrame 或 ToolFrame)
inline bool isRigidFrame (JointKind kind)
{
    return kind == JointKind::FixedFrame || kind == JointKind::ToolFrame;
}

// -----------------------------------------------------------------------------
//  SceneFrameType / PoseMode 字符串 ↔ enum 转换 (Qt-free)
// -----------------------------------------------------------------------------

/// 字符串转 SceneFrameType 枚举
inline SceneFrameType sceneFrameTypeFromString (const std::string& type)
{
    const std::string t = detail::trimmed (type);
    if (detail::iequals (t, "Movable"))
        return SceneFrameType::Movable;
    if (detail::iequals (t, "Normal"))
        return SceneFrameType::Normal;
    return SceneFrameType::Fixed;
}

/// SceneFrameType 枚举转 C 风格字符串
inline const char* sceneFrameTypeToString (SceneFrameType type)
{
    switch (type) {
        case SceneFrameType::Movable: return "Movable";
        case SceneFrameType::Normal:  return "Normal";
        case SceneFrameType::Fixed:
        default:                      return "Fixed";
    }
}

/// 字符串转 PoseMode 枚举
inline PoseMode poseModeFromString (const std::string& mode)
{
    const std::string m = detail::trimmed (mode);
    if (detail::iequals (m, "Transform4x4") || detail::iequals (m, "Transform"))
        return PoseMode::Transform4x4;
    return PoseMode::RPYPos;
}

/// PoseMode 枚举转 C 风格字符串
inline const char* poseModeToString (PoseMode mode)
{
    return mode == PoseMode::Transform4x4 ? "Transform4x4" : "RPYPos";
}

/// 运动学行数据 (基础变换结构)
struct KinematicRow
{
    std::string name;                   ///< 参考系/关节名称
    std::string type;                   ///< 关节类型名称 ("Revolute", "Prismatic" 等)
    std::array< double, 3 > rpyDeg;     ///< RPY 欧拉角 (单位: 度, Z-Y-X 顺序)
    std::array< double, 3 > pos;        ///< 平移位置 (单位: 米, x, y, z)
};

/// DH 参数关节描述结构体 (Schilling 约定)
struct DHJointSpec
{
    std::string name;                   ///< 关节名称
    double alphaDeg;                    ///< alpha 参数: 绕 X 轴旋转角 (单位: 度)
    double a;                           ///< a 参数: 沿 X 轴平移距离 (单位: 米)
    double d;                           ///< d 参数: 沿 Z 轴平移距离 (单位: 米)
    double offsetDeg;                   ///< offset 参数: 零位偏置角 (单位: 度)
};

/// SE(3) 关节变换描述结构体
struct JointTransformSpec
{
    std::string name;                   ///< 关节/参考系名称
    std::string type;                   ///< 类型字符串 ("Revolute", "Prismatic", "FixedFrame", "ToolFrame")
    std::array< double, 3 > rpyDeg;     ///< RPY 欧拉角 (单位: 度, Z-Y-X 顺序)
    std::array< double, 3 > pos;        ///< 平移位移 (单位: 米, x, y, z)
};

// -----------------------------------------------------------------------------
//  FrameSpec
//  说明: 一个场景 frame (在 Scene XML 中以 <Frame> 出现)。
//        refframe 必须是 WORLD / RobotBase / 同 spec.sceneFrames 已存在的 name;
//        设备内部 frame (Base / Joint* / TCP / ToolFrame) 不能被场景 frame 引用。
// -----------------------------------------------------------------------------
struct FrameSpec
{
    std::string name;                   ///< Frame 名称
    std::string refFrame;               ///< 父系 Frame 名称 (参照坐标系)
    SceneFrameType frameType = SceneFrameType::Fixed; ///< 场景 Frame 类型 (Fixed, Movable, Normal)
    bool daf = false;                   ///< 是否为动态附着参考系 (Dynamically Attached Frame)
    PoseMode poseMode = PoseMode::RPYPos; ///< 位姿表达模式 (RPYPos 或 Transform4x4)
    std::array< double, 3 > rpyDeg = {{0, 0, 0}};    ///< RPY 欧拉角 (单位: 度)
    std::array< double, 3 > pos = {{0, 0, 0}};       ///< 平移向量 (单位: 米)
    std::array< double, 16 > transform = {{1, 0, 0, 0,
                                           0, 1, 0, 0,
                                           0, 0, 1, 0,
                                           0, 0, 0, 1}}; ///< 4x4 齐次变换矩阵 (行优先，PoseMode::Transform4x4 使用)
};

// -----------------------------------------------------------------------------
//  GeometryKind / SceneGeometrySpec
//  说明: 场景几何体描述。挂载到已有的场景 frame (由 refFrame 指向)。
// -----------------------------------------------------------------------------

/// 几何体形状类型枚举
enum class GeometryKind
{
    Box,           ///< 长方体/立方体
    Cylinder,      ///< 圆柱体
    Sphere,        ///< 球体
    Cone,          ///< 圆锥体
    Plane,         ///< 平面
    STL,           ///< STL 网格文件
    Mesh,          ///< 通用网格文件
    Polytope,      ///< 多面体/凸包网格
    Unknown        ///< 未知形状
};

/// 场景几何体参数结构体
struct SceneGeometrySpec
{
    std::string name;                   ///< 几何体名称
    std::string refFrame;               ///< 挂载的参考系名称
    GeometryKind kind = GeometryKind::Box; ///< 几何形状类型
    std::array< double, 3 > size = {{0.1, 0.1, 0.1}}; ///< 尺寸: Box (x,y,z) 或 Plane (x,y)
    double radius = 0.05;               ///< 半径: 圆柱 / 球 / 圆锥
    double length = 0.1;                ///< 高度/长度: 圆柱 / 圆锥
    std::string file;                   ///< 模型文件路径 (Mesh/STL/Polytope 类型使用)
    std::array< double, 3 > rpyDeg = {{0, 0, 0}}; ///< 在 refFrame 下的 RPY 姿态偏置 (度)
    std::array< double, 3 > pos = {{0, 0, 0}};    ///< 在 refFrame 下的位置偏置 (米)
    std::array< double, 3 > rgb = {{0.6, 0.6, 0.6}}; ///< 颜色 RGB 通道值 [0.0, 1.0]
    bool collisionModel = true;         ///< 是否同时作为碰撞模型 (输出 colmodel="Enabled")
};

// -----------------------------------------------------------------------------
//  GeometryKind 字符串 ↔ enum 转换 (Qt-free)
// -----------------------------------------------------------------------------

/// 字符串转 GeometryKind 枚举 ("STL" 识别为 STL)
inline GeometryKind geometryKindFromString (const std::string& value)
{
    const std::string v = detail::trimmed (value);
    if (detail::iequals (v, "Box"))
        return GeometryKind::Box;
    if (detail::iequals (v, "Cylinder"))
        return GeometryKind::Cylinder;
    if (detail::iequals (v, "Sphere"))
        return GeometryKind::Sphere;
    if (detail::iequals (v, "Cone"))
        return GeometryKind::Cone;
    if (detail::iequals (v, "Plane"))
        return GeometryKind::Plane;
    if (detail::iequals (v, "STL"))
        return GeometryKind::STL;
    if (detail::iequals (v, "Mesh"))
        return GeometryKind::Mesh;
    if (detail::iequals (v, "Polytope"))
        return GeometryKind::Polytope;
    return GeometryKind::Unknown;
}

/// GeometryKind 枚举转 C 风格字符串
inline const char* geometryKindToString (GeometryKind kind)
{
    switch (kind) {
        case GeometryKind::Box:      return "Box";
        case GeometryKind::Cylinder: return "Cylinder";
        case GeometryKind::Sphere:   return "Sphere";
        case GeometryKind::Cone:     return "Cone";
        case GeometryKind::Plane:    return "Plane";
        case GeometryKind::STL:      return "STL";
        case GeometryKind::Mesh:     return "Mesh";
        case GeometryKind::Polytope: return "Polytope";
        case GeometryKind::Unknown:
        default:                     return "Unknown";
    }
}

/// 可视化几何体描述结构体 (Drawable)
struct DrawableSpec
{
    std::string name;                   ///< Drawable 可视化几何体名称
    std::string refFrame;               ///< 挂载的参考系/关节名称
    std::string shape = "Box";          ///< 形状字符串 ("Box", "Cylinder", "Sphere", "Mesh" 等)
    std::string filePath;               ///< 外部三维模型网格文件路径
    std::array< double, 3 > dimensions = {{0.1, 0.1, 0.1}}; ///< 长宽高三维尺寸 (米)
    double radius = 0.05;               ///< 半径 (Cylinder/Sphere/Cone 使用)
    double length = 0.1;                ///< 长度/高度 (Cylinder/Cone 使用)
    std::array< double, 3 > rpyDeg = {{0, 0, 0}}; ///< 相对 refFrame 的 RPY 偏置 (度)
    std::array< double, 3 > pos = {{0, 0, 0}};    ///< 相对 refFrame 的 Pos 偏置 (米)
    std::array< double, 3 > rgb = {{0.6, 0.6, 0.6}}; ///< 颜色 RGB 通道 [0.0, 1.0]
    bool collisionModel = false;        ///< 是否同时充当碰撞模型 (colmodel="Enabled")
    bool autoLinkGeometry = false;      ///< 是否为自动重算位姿的连杆圆柱 (如 Link1To2)
};

// -----------------------------------------------------------------------------
//  CollisionModelSpec
//  说明: 独立的碰撞模型结构体 —— 与视觉 Drawable 解耦。
// -----------------------------------------------------------------------------
struct CollisionModelSpec
{
    std::string name;                   ///< 碰撞模型名称
    std::string refFrame;               ///< 挂载的参考系/关节名称
    std::string shape = "Box";          ///< 形状类型字符串 (不支持 Plane/STL，Mesh 走 Polytope)
    std::string filePath;               ///< 网格文件路径 (Mesh/Polytope 类型使用)
    std::array< double, 3 > dimensions = {{0.1, 0.1, 0.1}}; ///< 长宽高三维尺寸 (米)
    double radius = 0.05;               ///< 半径 (米)
    double length = 0.1;                ///< 长度 (米)
    std::array< double, 3 > rpyDeg = {{0, 0, 0}}; ///< 相对 refFrame 的 RPY 偏置 (度)
    std::array< double, 3 > pos = {{0, 0, 0}};    ///< 相对 refFrame 的 Pos 偏置 (米)
};

/// 关节运动限位参数结构体
struct JointLimitSpec
{
    std::string jointName;              ///< 对应的可动关节名称
    double posMin;                      ///< 位置下限 (Revolute 为度, Prismatic 为米)
    double posMax;                      ///< 位置上限 (Revolute 为度, Prismatic 为米)
    double velMax;                      ///< 最大速度 (Revolute 为 deg/s, Prismatic 为 m/s)
    double accMax;                      ///< 最大加速度 (Revolute 为 deg/s^2, Prismatic 为 m/s^2)
};

/// 预设位姿参数结构体 (Q)
struct PoseSpec
{
    std::string name;                   ///< 预设位姿名称 (例如 "Zero", "Ready")
    std::vector< double > q;            ///< 各可动关节角/位置数组 (UI层 Revolute 为度，导出时自动转弧度)
};

/// 连杆动力学参数结构体
struct LinkDynamicsSpec
{
    std::string linkName;               ///< 动力学 Link 名称 (如 "Link1")
    std::string objectName;             ///< 绑定的可动关节名称
    double mass;                        ///< 质量 (单位: kg)
    std::array< double, 3 > cog;        ///< 质心位置 (Center of Gravity, 相对关节系, 单位: 米)
    std::array< double, 6 > inertia;    ///< 惯性张量独立项: [Ixx, Iyy, Izz, Ixy, Ixz, Iyz]
    bool estimateInertia;               ///< 是否由 RobWork 自动估算惯量矩阵 (<EstimateInertia/>)
    std::string material;               ///< 材质名称 (例如 "Steel", "Aluminum")
};

/// 关节最大驱动力限制结构体
struct JointForceLimitSpec
{
    std::string jointName;              ///< 对应的可动关节名称
    double maxForce;                    ///< 最大驱动力/力矩上限 (Revolute 为 N·m, Prismatic 为 N)
};

/// 动力学模型全局配置参数结构体
struct DynamicModelSpec
{
    bool generateDynamicWorkCell = false; ///< 是否生成物理仿真 DWC XML 文件 (.dwc.xml)
    std::string baseFrame        = "Base"; ///< 动力学基座参考系名称
    std::string baseMaterial     = "Steel";///< 基座材质名称
    std::vector< LinkDynamicsSpec > links; ///< 各连杆动力学参数列表
    std::vector< JointForceLimitSpec > forceLimits; ///< 各关节驱动力上限列表
};

// -----------------------------------------------------------------------------
//  Milestone 6: CollisionSetup / ProximitySetup + Scene <Include> 列表定义
// -----------------------------------------------------------------------------

/// XML 包含项的类型分类
enum class IncludeKind
{
    Device,        ///< 机器人/设备模型包含项 (.wc.xml)
    WorkCell,      ///< 场景工作单元包含项
    Collision,     ///< 碰撞配置文件包含项 (CollisionSetup)
    Proximity      ///< 临近配置文件包含项 (ProximitySetup)
};

/// Scene XML 中的 <Include> 节点描述结构体
struct IncludeSpec
{
    std::string file;                   ///< 被包含的 XML 文件相对/绝对路径
    IncludeKind kind = IncludeKind::Device; ///< 包含项类型
};

/// 坐标系对结构体 (用于碰撞排除对)
struct FramePairSpec
{
    std::string first;                  ///< 第一个 Frame 名称
    std::string second;                 ///< 第二个 Frame 名称
};

/// 碰撞矩阵配置参数结构体 (CollisionSetup.xml)
struct CollisionSetupSpec
{
    bool enabled                       = true;  ///< 是否使能并输出 CollisionSetup.xml
    std::string file                   = "CollisionSetup.xml"; ///< 输出的碰撞配置文件文件名
    bool excludeAdjacentLinkPairs      = true;  ///< 是否自动排除相邻关节连杆对的碰撞检查
    bool excludeStaticPairs            = false; ///< 是否排除静态固定参考系之间的碰撞检查
    std::vector< FramePairSpec > excludePairs;  ///< 用户自定义排除碰撞的 Frame 对列表
    std::vector< std::string > volatileFrames;  ///< 易变/频繁变动参考系列表 (<Volatile>)
};

/// 临近检测规则类型
enum class ProximityRuleKind
{
    Include,       ///< 包含规则 (<Include>)
    Exclude        ///< 排除规则 (<Exclude>)
};

/// 临近检测规则配置项
struct ProximityRuleSpec
{
    ProximityRuleKind kind      = ProximityRuleKind::Include; ///< 规则类型 (Include / Exclude)
    std::string patternA;               ///< 匹配参考系 A 的通配符/正则表达式 (PatternA)
    std::string patternB;               ///< 匹配参考系 B 的通配符/正则表达式 (PatternB)
};

/// 临近检测全局配置结构体 (ProximitySetup.xml)
struct ProximitySetupSpec
{
    bool enabled                  = false; ///< 是否使能并输出 ProximitySetup.xml
    std::string file              = "ProximitySetup.xml"; ///< 输出的临近检测配置文件文件名
    bool useIncludeAll            = true;  ///< 是否默认包含所有检测对 (UseIncludeAll)
    bool useExcludeStaticPairs    = false; ///< 是否排除静态参考系对 (UseExcludeStaticPairs)
    std::vector< ProximityRuleSpec > rules; ///< 自定义临近检测通配规则列表
};

/// 机器人模型顶层数据结构 (Top-Level Root Spec)
struct RobotModelSpec
{
    std::string robotName;              ///< 机器人模型名称 (如 "GenericSixAxis")
    std::string saveDirectory;          ///< XML 文件保存的目标磁盘目录
    KinematicsViewMode mode;            ///< 运动学视图模式 (JointRPYPos / DHProjection)
    bool exportDhJointsAdvanced = false;///< 高级选项: 是否导出 <DHJoint> 标签 (须满足无损投影条件)
    bool showFrameAxes;                 ///< 是否在 RobWorkStudio 中显示坐标轴 Property
    bool generateDrawables;             ///< 是否生成可视化几何体节点 (<Drawable>)
    bool generateScene;                 ///< 是否生成场景 WorkCell 文件 (Scene.wc.xml)
    FrameSpec robotBaseFrame;           ///< 场景中的机器人基座坐标系 (RobotBase)
    std::vector< FrameSpec > sceneFrames;             ///< 场景中的外部参考系列表 (Table, Workpiece等)
    std::vector< SceneGeometrySpec > sceneGeometries; ///< 场景外部几何体列表
    std::vector< JointTransformSpec > transformJoints;///< SE(3) 关节与变换真值列表
    std::vector< DHJointSpec > dhJoints;               ///< DH 关节参数列表 (投影派生视图)
    std::vector< DrawableSpec > drawables;             ///< 可视化几何体列表
    std::vector< CollisionModelSpec > collisionModels; ///< 独立碰撞几何模型列表
    std::vector< JointLimitSpec > limits;              ///< 关节运动限位列表
    std::vector< PoseSpec > poses;                     ///< 预设关节位姿列表
    DynamicModelSpec dynamics;                         ///< 动力学模型配置与参数
    std::vector< IncludeSpec > includes;               ///< Scene XML 顶部的自定义 <Include> 列表
    CollisionSetupSpec collisionSetup;                 ///< 碰撞矩阵配置 (CollisionSetup.xml)
    ProximitySetupSpec proximitySetup;                 ///< 临近检测配置 (ProximitySetup.xml)
};

}    // namespace rws

#endif