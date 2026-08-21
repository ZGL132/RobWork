#ifndef RWS_STRUCTUREOPTIMIZATION_PARAMETERBINDING_HPP
#define RWS_STRUCTUREOPTIMIZATION_PARAMETERBINDING_HPP

#include "DesignVariable.hpp"
#include "CanonicalKinematicModel.hpp"

#include <rw/math/Vector3D.hpp>

#include <limits>

namespace rws {

/**
 * @brief 参数绑定所作用的目标对象类型
 * @details 指明优化设计变量（DesignVariable）最终映射并修改的规范模型实体层级。
 */
enum class TargetObjectType 
{ 
    Unknown,            ///< 未知对象类型（非法初始状态）
    Frame,              ///< 坐标系节点（FrameNode）
    Joint,              ///< 关节连接边（JointEdge）
    Dof,                ///< 自由度定义（DofDefinition）
    DeviceChain,        ///< 机构设备链（DeviceChain）
    ToolBinding,        ///< 工具与 TCP 绑定配置（ToolBinding）
    Geometry,           ///< 可视化几何实体（GeometryBinding）
    CollisionGeometry   ///< 碰撞检测几何实体（CollisionBinding）
};

/**
 * @brief 目标对象上被修改的具体属性标识符（原子属性粒度）
 * @details 详细枚举了可被优化算法作为变量进行参数化驱动的具体物理/几何通道。
 */
enum class TargetPropertyId 
{
    Unknown,

    /* --- 关节原点相对位移属性 (Parent Frame -> Joint Zero) --- */
    ParentToJointTranslationX,      ///< 关节安装原点在父坐标系下的 X 轴向位移
    ParentToJointTranslationY,      ///< 关节安装原点在父坐标系下的 Y 轴向位移
    ParentToJointTranslationZ,      ///< 关节安装原点在父坐标系下的 Z 轴向位移

    /* --- 关节运动轴方向与零位属性 --- */
    MotionAxisTiltU,                ///< 关节运动轴切向倾角分量 U（轴线微调/结构公差校准）
    MotionAxisTiltV,                ///< 关节运动轴切向倾角分量 V
    ZeroPositionOffset,             ///< 关节零位校准偏置量（$q_{offset}$）

    /* --- 关节机械硬限位属性 --- */
    PhysicalLimitLower,             ///< 关节物理硬限位下界
    PhysicalLimitUpper,             ///< 关节物理硬限位上界

    /* --- 关节运行软限位/安全策略属性 --- */
    OperationalLimitLower,          ///< 关节软件运行限位下界
    OperationalLimitUpper,          ///< 关节软件运行限位上界

    /* --- 机器人基座 (Base) 位姿属性 --- */
    BaseTranslationX, BaseTranslationY, BaseTranslationZ,          ///< 基座原点在全局参考系下的平移参数 $(t_x, t_y, t_z)$
    BaseRotationVectorX, BaseRotationVectorY, BaseRotationVectorZ, ///< 基座相对于全局参考系的等效旋转向量分量 $(\omega_x, \omega_y, \omega_z)$

    /* --- 连杆末端到法兰 (Flange) 位姿属性 --- */
    ParentToFlangeTranslationX, ParentToFlangeTranslationY, ParentToFlangeTranslationZ,
    ParentToFlangeRotationVectorX, ParentToFlangeRotationVectorY, ParentToFlangeRotationVectorZ,

    /* --- 法兰到工具中心点 (Flange -> TCP) 安装位姿属性 --- */
    FlangeToTcpTranslationX, FlangeToTcpTranslationY, FlangeToTcpTranslationZ,
    FlangeToTcpRotationVectorX, FlangeToTcpRotationVectorY, FlangeToTcpRotationVectorZ,

    /* --- 参数化几何外形与材料尺寸 --- */
    GeometryRadius,                 ///< 几何体半径参数
    GeometryLength,                 ///< 几何体长度参数
    GeometryWidth,                  ///< 几何体宽度参数
    GeometryHeight,                 ///< 几何体高度参数
    GeometryDepth,                  ///< 几何体深度参数
    GeometryWallThickness,          ///< 几何体管壁厚度参数
    GeometryRigidTransform,         ///< 几何体相对于挂载坐标系的静态刚体变换矩阵
    GeometryScale,                  ///< 几何体各向同性/各向异性缩放因子
    Material                        ///< 物理材料属性参数
};

/**
 * @brief 关节限位的作用范围作用域
 * @note 限位调整必须显式区分为机械物理硬限位或软件运行策略限位，严禁隐式混淆。
 */
enum class JointLimitScope 
{ 
    Unknown,        ///< 未知作用域
    Physical,       ///< 机械物理极限（由机械防撞块、机械死点决定的硬限位）
    Operational     ///< 软件运行极限（由安全策略、避障或工艺包决定的软限位）
};

/**
 * @brief $SO(3)$ 旋转向量姿态增量的显式复合顺序
 * @details 规定位姿更新时增量旋转矩阵是左乘还是右乘。
 */
enum class PoseDeltaComposition 
{ 
    Unknown, 
    Right           ///< 右乘复合：$R_{new} = R_{base} \cdot \Delta R(\boldsymbol{\omega})$（在局部本体坐标系下施加旋转增量）
};

/**
 * @brief 读写目标原子描述符（Read/Write Target）
 * @details 用于优化变量与适配器之间建立明确的读写足迹（Footprint）。
 * 依赖分析器利用读写集构建拓扑依赖图，进行并发安全性检测与冲突排查。
 */
struct ReadWriteTarget
{
    TargetObjectType objectType = TargetObjectType::Unknown; ///< 目标对象类型
    std::string objectId;                                    ///< 目标对象唯一标识符
    TargetPropertyId propertyId = TargetPropertyId::Unknown; ///< 目标属性 ID
    std::string coordinateFrameId;                           ///< 属性表达时所基于的参考坐标系 ID

    /** @brief 比较两个读写目标是否完全一致（用于冲突检测） */
    bool operator==(const ReadWriteTarget& other) const;
};

/**
 * @brief 参数绑定契约核心结构体 (Parameter Binding)
 * @details 建立了“高层抽象设计变量 (DesignVariable)”与“底层规范模型 (CanonicalKinematicModel) 实际物理参数”之间的精确映射桥梁。
 * 负责定义数值如何安全、无歧义地注入到底层模型中。
 */
struct ParameterBinding
{
    std::string id;                                     ///< 参数绑定唯一标识符
    SemanticKind semanticKind = SemanticKind::Unknown;   ///< 设计变量的高层语义类别（如 LinkLength, JointLimitUpper 等）
    TargetObjectType targetObjectType = TargetObjectType::Unknown; ///< 被操作的目标对象类型
    std::string targetObjectId;                         ///< 被操作的目标对象 ID（如 "Joint_2", "Frame_Flange"）
    TargetPropertyId targetPropertyId = TargetPropertyId::Unknown; ///< 被修改的具体物理属性 ID
    std::string coordinateFrameId;                      ///< 变更计算时所基于的基准坐标系 ID
    std::string parameterizationModeId;                 ///< 关联的参数化模式标识
    std::string ownerAdapterId;                         ///< 拥有该绑定控制权的模型适配器 ID
    
    /**
     * @brief 拥有该绑定的适配器契约版本号
     * @note 系统启动与注册时会与适配器注册表（Adapter Registry）严格比对，防止版本不兼容导致的内存错位或计算错误。
     */
    int ownerAdapterVersion = 0;

    std::vector< std::string > requiredCapabilityIds;   ///< 执行此绑定所必须依赖的系统能力/算法组件 ID 列表
    std::vector< ReadWriteTarget > readSet;             ///< 显式读取集：该绑定计算时依赖读取的模型属性列表
    std::vector< ReadWriteTarget > writeSet;            ///< 显式写入集：该绑定更新时会污染/修改的模型属性列表

    /* --- 连杆长度 (LinkLength) 专用几何约束 --- */
    /**
     * @brief 连杆延伸方向所基于的参考坐标系 ID（通常为目标关节的父坐标系）
     * @note 严禁使用隐式默认坐标系，必须显式指定。
     */
    std::string referenceDirectionFrameId;

    /**
     * @brief 连杆长度拉伸/优化的方向向量（单位方向向量 $\mathbf{d} \in \mathbb{R}^3$）
     */
    rw::math::Vector3D<> referenceDirection;

    /* --- 关节轴微调 (MotionAxisTilt) 专用几何约束 --- */
    /**
     * @brief 关节轴允许倾斜的最大圆锥半顶角（单位：弧度）
     * @note 必须显式设置正实数，严禁使用隐式默认值，默认初始化为 NaN 以强制校验。
     */
    double maxAxisTiltAngle = std::numeric_limits< double >::quiet_NaN();

    /**
     * @brief 稳定的单关节轴偏角耦合组 ID
     * @note 将同一关节的 U 向和 V 向切向倾角变量显式耦合在同一个组内，确保落在同一个容差圆锥内。
     */
    std::string axisTiltGroupId;

    /* --- 关节限位 (Joint Limits) 专用安全约束 --- */
    /**
     * @brief 显式区分当前限位变更属于机械物理限位还是软件运行策略限位
     */
    JointLimitScope jointLimitScope = JointLimitScope::Unknown;

    /**
     * @brief 稳定的上下界成对耦合组 ID
     * @note 格式必须严格遵循 `joint-limits:<jointId>`，确保上限与下限变量始终成对协同校验。
     */
    std::string jointLimitGroupId;

    /**
     * @brief 求解后的上下限之间所必须满足的最小有效区间跨度（$\Delta q_{min} = q_{upper} - q_{lower} > 0$）
     * @note 防止优化算法将上下限压缩至重合或出现反转，默认为 NaN 强制显式配置。
     */
    double minimumJointLimitRange = std::numeric_limits< double >::quiet_NaN();

    /**
     * @brief 是否允许修改机械物理硬限位
     * @note 默认处于锁定保护状态 (false)；只有经过工程授权的项目绑定显式置为 true 时才允许算法修改机械硬限位。
     */
    bool allowPhysicalLimitModification = false;

    /**
     * @brief 绝对机械防撞物理安全包络（下界与上界）
     * @note 无论运行限位如何优化，其数值均严禁超出 $[absoluteJointLimitLower, absoluteJointLimitUpper]$ 物理安全红线。
     */
    double absoluteJointLimitLower = std::numeric_limits< double >::quiet_NaN();
    double absoluteJointLimitUpper = std::numeric_limits< double >::quiet_NaN();

    /**
     * @brief 限位边界所表达的关节坐标空间约定（QInput vs QModel）
     * @note 必须是关节状态空间 $Q$ 的坐标约定，严禁传入三维空间坐标系 ID。
     */
    JointCoordinateConvention jointLimitCoordinateConvention =
        JointCoordinateConvention::Unknown;

    /* --- 位姿增量 (Pose Delta) 专用配置 --- */
    /**
     * @brief 稳定的一体化位姿耦合组 ID
     * @note 将平移分量 $(t_x, t_y, t_z)$ 与旋转向量 $(\omega_x, \omega_y, \omega_z)$ 耦合为不可分割的完整 $SE(3)$ 刚体变换。
     */
    std::string poseDeltaGroupId;

    /**
     * @brief 姿态旋转向量更新的李代数复合规则（当前规范冻结为 $SO(3)$ 右乘增量）
     */
    PoseDeltaComposition poseDeltaComposition = PoseDeltaComposition::Unknown;

    /* --- 几何体与版本元数据 --- */
    /**
     * @brief 几何体尺寸参数耦合组 ID
     * @note 同一几何基元内部的多维尺寸参数（如圆柱的半径与长度）仅通过显式组 ID 进行强关联。
     */
    std::string geometryGroupId;

    int bindingVersion = 1;         ///< 绑定数据结构协议版本号
    std::string displayPath;        ///< UI 界面属性树展示路径（仅作前端呈现，如 "Robot/Link2/Geometry/Radius"）

    /**
     * @brief 运行时等价性比较（Runtime Equality）
     * @note 编译器用于判断两个绑定是否完全等价。特意**排除了仅用于 UI 显示的 `displayPath`**，
     * 避免因前端界面重命名或节点路径展示微调导致优化器重新编译设计空间。
     */
    bool runtimeEquals(const ParameterBinding& other) const;
};

/**
 * @brief 参数绑定校验诊断结果
 */
struct ParameterBindingValidationResult
{
    bool valid = true; ///< 绑定配置是否通过语义与数学边界校验
    std::vector< StructureOptimizationDiagnostic > diagnostics; ///< 诊断错误与警告日志列表
};

/**
 * @brief 参数绑定纯函数校验器
 */
class ParameterBindingValidator
{
public:
    /**
     * @brief 校验单个参数绑定的合法性
     * @param binding 待校验的参数绑定实体
     * @return ParameterBindingValidationResult
     * @details 核心校验规则包括：
     * 1. 检查物理属性与目标对象类型的一致性（例如不能对 Frame 修改 JointLimit）；
     * 2. 校验浮点安全约束：`maxAxisTiltAngle`、`minimumJointLimitRange` 等不得为 NaN，且必须满足正定性；
     * 3. 校验限位安全性：当修改物理限位时 `allowPhysicalLimitModification` 必须显式开启，且不得越过绝对机械包络；
     * 4. 校验 $SE(3)$ 位姿更新规则与参考方向向量模长（不得为零向量）。
     */
    static ParameterBindingValidationResult validate(const ParameterBinding& binding);
};

/* --- 辅助序列化与字符串转换函数 --- */

/** @brief 目标对象类型枚举序列化与反序列化 */
std::string targetObjectTypeToString(TargetObjectType type);
bool targetObjectTypeFromString(const std::string& value, TargetObjectType& type);

/** @brief 目标属性 ID 枚举序列化与反序列化 */
std::string targetPropertyIdToString(TargetPropertyId property);
bool targetPropertyIdFromString(const std::string& value, TargetPropertyId& property);

/** @brief 关节限位作用域枚举序列化与反序列化 */
std::string jointLimitScopeToString(JointLimitScope scope);
bool jointLimitScopeFromString(const std::string& value, JointLimitScope& scope);

}    // namespace rws

#endif    // RWS_STRUCTUREOPTIMIZATION_PARAMETERBINDING_HPP