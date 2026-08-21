#ifndef RWS_STRUCTUREOPTIMIZATION_CANONICALKINEMATICMODEL_HPP
#define RWS_STRUCTUREOPTIMIZATION_CANONICALKINEMATICMODEL_HPP

#include "StructureOptimizationContracts.hpp"

#include <rw/math/Transform3D.hpp>
#include <rw/math/Vector3D.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace rws {

/**
 * @brief 规范坐标系角色类型
 * @note 坐标系的角色仅作为拓扑注解使用；所有实际的运动学自由度与相对位移均由 JointEdge（关节边）持有。
 */
enum class CanonicalFrameType
{
    Base,       ///< 机器人基座标系（机构的根/基座基准）
    Link,       ///< 机器人连杆坐标系（承上启下的刚体构件）
    Fixed,      ///< 拓扑固定的坐标系（无运动自由度）
    Flange,     ///< 机械臂末端法兰坐标系（工具安装面基准）
    Tool,       ///< 工具坐标系（如 TCP，工具中心点）
    Auxiliary   ///< 辅助/参考坐标系（传感器安装点、附加几何参考等）
};

/**
 * @brief 规范 $SE(3)$ 运动学模型所采用的关节运动语义类型
 */
enum class CanonicalJointType
{
    Revolute,   ///< 旋转关节（1 个旋转自由度，单位：弧度）
    Prismatic,  ///< 移动/棱柱关节（1 个平移自由度，单位：米）
    Fixed       ///< 固定关节（无运动，仅表示刚性位姿变换）
};

/**
 * @brief 活动自由度 (DOF) 的显式物理坐标单位
 */
enum class CanonicalCoordinateUnit
{
    Radians,    ///< 弧度（用于旋转关节）
    Metres      ///< 米（用于平移关节）
};

/**
 * @brief 关节限位的坐标约定定义
 * @details 用于明确限位数值是基于原始输入值 $q_{input}$ 还是应用了零位偏移后的模型内坐标 $q_{model}$。
 */
enum class JointCoordinateConvention
{
    Unknown,    ///< 未知约定（校验时通常会发出警告或错误）
    QInput,     ///< 原始输入坐标系下的限位（在加减 zeroPositionOffset 之前）
    QModel      ///< 内部规范模型坐标系下的限位（已包含 zeroPositionOffset）
};

/**
 * @brief 运动学图中的坐标系节点 (Frame Node)
 */
struct FrameNode
{
    std::string id;         ///< 坐标系唯一标识符（UUID 或内部唯一字符串）
    std::string name;       ///< 人类可读的坐标系名称（如 "Link_1", "Flange"）
    CanonicalFrameType type = CanonicalFrameType::Auxiliary; ///< 坐标系类型角色
    std::string sourceObjectId; ///< 来源对象 ID（用于追溯导入源，如 CAD 模型或外部装配体部件）
};

/**
 * @brief 规范关节限位结构体
 */
struct CanonicalJointLimits
{
    bool enabled = false;   ///< 是否启用了该限位
    double lower = 0.0;     ///< 限位下界
    double upper = 0.0;     ///< 限位上界
    CanonicalCoordinateUnit unit = CanonicalCoordinateUnit::Radians; ///< 限位数值的物理单位
    
    /**
     * @brief 坐标约定类型
     * @note 严禁隐式推断限位是施加在零位偏移之前还是之后，必须在此显式声明。
     */
    JointCoordinateConvention coordinateConvention = JointCoordinateConvention::Unknown;
};

/**
 * @brief 父子坐标系之间的关节连接边 (Joint Edge)
 * @details 包含从父坐标系到子坐标系的完整 $SE(3)$ 刚体变换以及显式的运动轴向量。
 * 运动学计算公式为：
 * $$T_{parent \to child}(q) = T_{parent \to jointZero} \cdot T_{motion}(q + q_{offset}) \cdot T_{jointMotion \to child}$$
 * 该 POD 结构体中不包含欧拉角、DH 参数或名义连杆长度，一切以显式矩阵变换与轴向量为唯一真实基准。
 */
struct JointEdge
{
    std::string id;         ///< 关节唯一标识符
    std::string name;       ///< 关节名称（如 "Joint_1"）
    CanonicalJointType type = CanonicalJointType::Fixed; ///< 关节运动学类型
    std::string parentFrameId;  ///< 父坐标系 ID
    std::string childFrameId;   ///< 子坐标系 ID
    
    rw::math::Transform3D<> parentToJointZero; ///< 从父坐标系原点到关节零位位姿的变换矩阵
    rw::math::Vector3D<> motionAxisInJoint = rw::math::Vector3D<>::z(); ///< 关节局部坐标系下的运动轴向量（默认为局部 Z 轴）
    rw::math::Transform3D<> jointMotionToChild; ///< 从关节运动后位姿到子坐标系原点的变换矩阵
    
    double zeroPositionOffset = 0.0; ///< 关节零位偏移量（用于校准模型零位与输入零位间的差值）
    CanonicalJointLimits physicalLimits;    ///< 物理极限（机械硬限位）
    CanonicalJointLimits operationalLimits; ///< 运行极限（软件软限位 / 安全范围）
    
    std::string dofId;          ///< 关联的自由度定义 ID（若为固定关节则可为空）
    std::string sourceObjectId; ///< 来源对象 ID
};

/**
 * @brief 自由度 (DOF) 的显式映射定义
 */
struct DofDefinition
{
    std::string id;         ///< DOF 唯一标识符
    std::string jointId;    ///< 该 DOF 所驱动的目标关节 ID
    std::size_t qIndex = 0; ///< 在状态向量 $Q$ 中的索引下标（从 0 开始）
    CanonicalJointType type = CanonicalJointType::Fixed; ///< 运动类型（Revolute / Prismatic）
    CanonicalCoordinateUnit unit = CanonicalCoordinateUnit::Radians; ///< 状态量物理单位
};

/**
 * @brief 机构运动学链 (Device Kinematic Chain)
 * @details 定义从根节点到末端执行器的一组有序关节与自由度序列。
 */
struct DeviceChain
{
    std::string id;             ///< 机构链唯一标识符
    std::string rootFrameId;    ///< 链的根坐标系 ID（通常为 Base）
    std::string tipFrameId;     ///< 链的末端坐标系 ID（通常为 Flange）
    std::vector< std::string > orderedJointIds; ///< 从根到末端按运动学顺序排列的关节 ID 列表
    std::vector< std::string > orderedDofIds;   ///< 按状态向量 $Q$ 映射顺序排列的 DOF ID 列表
};

/**
 * @brief 工具与末端夹具绑定定义
 */
struct ToolBinding
{
    std::string id;             ///< 工具绑定唯一标识符
    std::string flangeFrameId;  ///< 机械臂法兰坐标系 ID
    std::string tcpFrameId;     ///< 工具中心点 (TCP) 坐标系 ID
    rw::math::Transform3D<> flangeToTcp; ///< 从法兰安装面到 TCP 的静态 $SE(3)$ 相对变换
    std::vector< std::string > geometryBindingIds;  ///< 挂载在该工具上的可视化几何实体 ID 列表
    std::vector< std::string > collisionBindingIds; ///< 挂载在该工具上的碰撞几何实体 ID 列表
};

/**
 * @brief 规范模型拥有的参数化几何体类型
 * @note 几何数据归规范模型所有，严禁由 UI 层控制其实体定义。
 */
enum class CanonicalGeometryKind 
{ 
    Unknown,    ///< 未知类型
    Cylinder,   ///< 圆柱体（参数：radius, length）
    Box,        ///< 长方体（参数：length, width, height）
    Tube,       ///< 圆管/空心柱（参数：radius, length, wallThickness）
    Mesh        ///< 任意三角网格（外部加载的模型文件）
};

/**
 * @brief 可视化几何体绑定 (Visual Geometry Binding)
 */
struct GeometryBinding
{
    std::string id;                 ///< 几何体绑定唯一标识符
    std::string referenceFrameId;   ///< 绑定的父参考坐标系 ID（随该坐标系运动）
    CanonicalGeometryKind kind = CanonicalGeometryKind::Unknown; ///< 几何体类型
    
    /**
     * @brief 优化算法所有权标记
     * @note 用户导入/创建的几何体默认不可修改，只有显式标记为 true 时，结构优化算法才允许修改其尺寸参数。
     */
    bool optimizationOwned = false;
    
    /**
     * @brief 是否允许刚体位姿调整
     * @note 网格模型不支持截面尺寸参数化，仅允许通过调整 rigid transform 来参与优化。
     */
    bool allowRigidTransform = false;
    
    rw::math::Transform3D<> referenceToGeometry; ///< 从参考坐标系到几何体局部中心的相对变换
    
    /* 参数化尺寸定义（根据 kind 选用不同字段） */
    double radius = 0.0;        ///< 半径（用于 Cylinder, Tube）
    double length = 0.0;        ///< 长度 / X 方向尺寸
    double width = 0.0;         ///< 宽度 / Y 方向尺寸
    double height = 0.0;        ///< 高度 / Z 方向尺寸
    double depth = 0.0;         ///< 深度尺寸
    double wallThickness = 0.0; ///< 壁厚（用于 Tube）
    
    std::string sourceObjectId; ///< 来源对象 ID（如引用的外部 Mesh 文件路径或资产 ID）
};

/**
 * @brief 碰撞几何体绑定 (Collision Geometry Binding)
 * @note 碰撞模型必须独立校验，不会自动回退或直接复用可视化网格作为碰撞体。
 */
struct CollisionBinding
{
    std::string id;                 ///< 碰撞绑定唯一标识符
    std::string referenceFrameId;   ///< 绑定的父参考坐标系 ID
    CanonicalGeometryKind kind = CanonicalGeometryKind::Unknown; ///< 碰撞体类型（推荐使用轻量级基元，如 Box/Cylinder）
    bool optimizationOwned = false; ///< 结构优化是否可修改该碰撞体尺寸
    bool allowRigidTransform = false; ///< 是否允许对碰撞体位姿进行优化变换
    rw::math::Transform3D<> referenceToGeometry; ///< 碰撞体相对于参考坐标系的安装位姿
    
    /* 参数化尺寸定义 */
    double radius = 0.0;
    double length = 0.0;
    double width = 0.0;
    double height = 0.0;
    double depth = 0.0;
    double wallThickness = 0.0;
    
    std::string sourceObjectId; ///< 来源对象 ID
};

/**
 * @brief 导入运动学模型的规范数据模型（核心聚合根）
 * @details 约定为不可变（Immutable-by-convention）纯数据传输对象 (DTO/POD)。
 * 设计上与 Qt Widgets、GUI 线程以及 RobWork WorkCell 的内部生命周期完全解耦，
 * 专门用于结构优化算法的无状态计算、序列化与拓扑分析。
 */
struct CanonicalKinematicModel
{
    int schemaVersion = 1;              ///< 数据结构协议版本号（用于向后兼容与版本迁移）
    std::string modelId;                ///< 当前规范模型的全局唯一标识符
    std::string sourceFingerprint;       ///< 原始模型数据的哈希指纹（用于检测导入源是否有变动）
    std::string environmentFingerprint;  ///< 场景环境配置的哈希指纹
    
    std::string rootFrameId;            ///< 全局场景树的根坐标系 ID
    std::string baseFrameId;            ///< 机器人的基座坐标系 ID
    std::string activeDeviceChainId;    ///< 当前处于激活状态的机构链 ID
    
    std::vector< FrameNode > frames;            ///< 拓扑中包含的所有坐标系节点
    std::vector< JointEdge > joints;            ///< 连接坐标系的关节边列表
    std::vector< DofDefinition > dofs;          ///< 自由度与向量索引的显式映射表
    std::vector< DeviceChain > deviceChains;    ///< 定义的运动学设备链列表
    std::vector< ToolBinding > toolBindings;    ///< 工具与 TCP 绑定配置
    std::vector< GeometryBinding > geometryBindings;   ///< 可视化几何体列表
    std::vector< CollisionBinding > collisionBindings; ///< 碰撞几何体列表
};

/**
 * @brief 规范运动学模型的校验诊断结果
 */
struct CanonicalKinematicModelValidationResult
{
    bool valid = true; ///< 模型拓扑与数据完整性是否通过校验
    std::vector< StructureOptimizationDiagnostic > diagnostics; ///< 校验过程中发现的警告或错误诊断列表
};

/**
 * @brief 规范拓扑与显式 Q 映射的纯函数式校验器
 */
class CanonicalKinematicModelValidator
{
public:
    /**
     * @brief 执行静态拓扑校验
     * @param model 待校验的规范运动学模型
     * @return 校验结果（包含是否合法及详细诊断日志）
     * @details 核心检查项包括：
     * 1. 坐标系 ID 与关节 ID 的唯一性；
     * 2. 图结构是否连通无环（树状或确定性链）；
     * 3. $Q$ 向量的索引下标是否连续且无重复映射；
     * 4. 关节运动轴、限位及坐标约定是否完备。
     */
    static CanonicalKinematicModelValidationResult validate(const CanonicalKinematicModel& model);
};

/* 辅助函数声明：关节坐标约定与字符串之间的序列化转换 */

/** @brief 将 JointCoordinateConvention 枚举转换为可读字符串 */
std::string jointCoordinateConventionToString(JointCoordinateConvention convention);

/** @brief 从字符串反序列化为 JointCoordinateConvention 枚举 */
bool jointCoordinateConventionFromString(const std::string& value,
                                         JointCoordinateConvention& convention);

/** @brief 检查给定的坐标约定枚举值是否合法（非 Unknown） */
bool isValidJointCoordinateConvention(JointCoordinateConvention convention);

}    // namespace rws

#endif    // RWS_STRUCTUREOPTIMIZATION_CANONICALKINEMATICMODEL_HPP