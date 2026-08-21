#ifndef RWS_STRUCTUREOPTIMIZATION_DESIGNVARIABLE_HPP
#define RWS_STRUCTUREOPTIMIZATION_DESIGNVARIABLE_HPP

#include "StructureOptimizationContracts.hpp"

#include <string>
#include <vector>

namespace rws {

/**
 * @brief 设计变量角色（自变量 vs 因变量）
 */
enum class VariableRole 
{ 
    Independent,    ///< 独立自变量：优化算法直接寻优或用户主动调节的核心决策变量
    Derived         ///< 派生因变量：由独立变量通过几何约束、物理关系或数学表达式计算推导得出
};

/**
 * @brief 变量的数学值域类型
 */
enum class VariableDomain 
{ 
    Continuous,     ///< 连续域：可在 $[x_{min}, x_{max}]$ 实数区间内任意取值（适用于大部分尺寸、位姿优化）
    Integer,        ///< 整数域：取值限定为整数序列（适用于离散齿数、分段数等）
    Discrete        ///< 离散选项域：仅能从预设的离散集合列表中选取（如标准型材规格、减速器型号、材料牌号）
};

/**
 * @brief 设计变量的物理工程单位
 */
enum class DesignVariableUnit 
{ 
    Unitless,       ///< 无量纲 / 标幺值 / 纯比例系数
    Metres,         ///< 米（长度、截面尺寸、位移）
    Radians,        ///< 弧度（角度、旋转量，标准计算内核使用）
    Degrees,        ///< 角度/度（界面交互或特定角度参数）
    Kilograms,      ///< 千克（质量参数）
    NewtonMetres    ///< 牛·米（关节力矩、额定负载参数）
};

/**
 * @brief 设计变量的创建/来源途径
 */
enum class DesignVariableSource 
{ 
    User,           ///< 用户手动创建与定义的变量
    Template,       ///< 来自系统预置机器人拓扑模板（如标准 6 自由度串联构型模板）
    Imported,       ///< 从外部文件（CAD、URDF 或配置文件）导入解析得到
    Legacy          ///< 兼容旧版本保留的历史变量定义
};

/**
 * @brief 设计变量在当前参数化上下文中的生效状态
 */
enum class DesignVariableStatus 
{ 
    Available,                  ///< 正常可用：参与当前设计空间的构建与优化求解
    DisabledByParameterization, ///< 被当前参数化模式禁用（如选择了固定臂长模式，连杆长度变量被冻结）
    Inapplicable,               ///< 当前拓扑结构不适用（例如非旋转关节不适用轴倾角变量）
    Invalid                     ///< 变量定义无效或配置冲突（如上下界倒置、依赖循环等）
};

/**
 * @brief 设计变量的稳定语义类别（Semantic Kind）
 * @details 供设计空间编译器（Design-space Compiler）和运动学适配器识别该变量所对应的真实物理/几何实体，
 * 避免依赖字符串名称解析，确保在构型变换与参数映射过程中的语义稳定性。
 */
enum class SemanticKind
{
    Unknown,

    /* --- 连杆与关节几何参数 --- */
    LinkLength,                 ///< 连杆长度（沿主要延伸方向的几何跨度）
    JointOriginOffsetX,         ///< 关节原点相对父坐标系在 X 方向的位移偏置
    JointOriginOffsetY,         ///< 关节原点相对父坐标系在 Y 方向的位移偏置
    JointOriginOffsetZ,         ///< 关节原点相对父坐标系在 Z 方向的位移偏置
    JointOffsetAlongAxis,       ///< 沿关节运动轴方向的偏置（类似 DH 参数中的 $d$）
    JointAxisTiltU,             ///< 关节运动轴在局部平面的第一倾角分量（用于偏角误差/结构倾斜）
    JointAxisTiltV,             ///< 关节运动轴在局部平面的第二倾角分量

    /* --- 关节运动学限位与零位 --- */
    JointZeroOffset,            ///< 关节零位校准偏置角/位移（$q_{offset}$）
    JointLimitLower,            ///< 关节活动范围下限
    JointLimitUpper,            ///< 关节活动范围上限

    /* --- 基座坐标系位姿参数 --- */
    BaseTx, BaseTy, BaseTz,     ///< 机器人基座相对于世界原点的平移量 $(t_x, t_y, t_z)$
    BaseRotationVectorX,        ///< 机器人基座旋转向量（等效旋转轴乘转角）X 分量
    BaseRotationVectorY,        ///< 机器人基座旋转向量 Y 分量
    BaseRotationVectorZ,        ///< 机器人基座旋转向量 Z 分量

    /* --- 工具中心点 (TCP) 位姿参数 --- */
    TcpTx, TcpTy, TcpTz,        ///< TCP 相对末端法兰的安装平移量 $(t_x, t_y, t_z)$
    TcpRotationVectorX,         ///< TCP 相对法兰的旋转向量 X 分量
    TcpRotationVectorY,         ///< TCP 相对法兰的旋转向量 Y 分量
    TcpRotationVectorZ,         ///< TCP 相对法兰的旋转向量 Z 分量

    /* --- 末端法兰位姿参数 --- */
    FlangeTx, FlangeTy, FlangeTz, ///< 连杆末端到法兰面的平移参数
    FlangeRotationVectorX,        ///< 法兰面相对旋转向量 X 分量
    FlangeRotationVectorY,        ///< 法兰面相对旋转向量 Y 分量
    FlangeRotationVectorZ,        ///< 法兰面相对旋转向量 Z 分量

    /* --- 连杆截面与参数化几何外形 --- */
    LinkRadius,                 ///< 连杆截面外径 / 圆柱连杆半径
    LinkWidth,                  ///< 连杆外轮廓宽度
    LinkHeight,                 ///< 连杆外轮廓高度
    LinkCrossSectionX,          ///< 连杆截面在局部 X 方向的特征尺寸
    LinkCrossSectionY,          ///< 连杆截面在局部 Y 方向的特征尺寸
    LinkWallThickness,          ///< 连杆薄壁/中空截面的壁厚
    LinkScale,                  ///< 连杆整体缩放因子

    /* --- 通用几何基元参数 --- */
    GeometryRadius,             ///< 绑定几何体的半径
    GeometryLength,             ///< 绑定几何体的长度
    GeometryWidth,              ///< 绑定几何体的宽度
    GeometryHeight,             ///< 绑定几何体的高度
    GeometryDepth,              ///< 绑定几何体的深度
    GeometryWallThickness,      ///< 绑定几何体的壁厚
    GeometryRigidTransform,     ///< 几何体本身的相对刚体安装位姿

    /* --- 物理与材料属性 --- */
    ParameterizedMaterial       ///< 参数化材料属性（弹性模量、密度、泊松比等）
};

/**
 * @brief 离散取值选项实体
 * @details 当变量域为 `VariableDomain::Discrete` 时，用于提供可选的具体规格选项。
 */
struct DiscreteOption
{
    std::string id;                 ///< 选项唯一标识符（如 "Profile_100x100x4"）
    std::string displayName;        ///< UI 界面展示名称（如 "方管 100×100 (厚度 4mm)"）
    std::string payloadReference;   ///< 关联数据载荷的引用 ID（如对应材料库、截面几何属性表的唯一键）
};

/**
 * @brief 设计变量的核心数据定义
 * @details 采用纯数据实体设计，与 Qt 界面层（如 QTableWidget / QAbstractTableModel）完全解耦，
 * 专门服务于设计空间编译、结构拓扑更新及优化求解器的数学建模。
 */
struct DesignVariableDefinition
{
    std::string id;                 ///< 变量全局唯一标识符（如 "dv_link2_length"）
    std::string displayName;        ///< 人类可读名称（如 "连杆 2 臂长"）
    SemanticKind semanticKind = SemanticKind::Unknown; ///< 稳定的物理语义标识
    VariableRole role = VariableRole::Independent;     ///< 变量角色（独立自变量 / 派生因变量）
    std::string groupId;            ///< 所属变量分组 ID（如 "Link_2_Geometry", "Base_Mounting"）
    std::string parameterizationModeId; ///< 所属参数化模式标识

    double nominalValue = 0.0;      ///< 初始名义标称值（设计初始点）
    double currentValue = 0.0;      ///< 当前迭代/评估状态下的实际取值
    VariableDomain domain = VariableDomain::Continuous; ///< 数学定义域类型

    /* --- 连续/整数变量的边界与步长约束 --- */
    double minimum = 0.0;           ///< 寻优搜索下界 $x_{min}$
    double maximum = 0.0;           ///< 寻优搜索上界 $x_{max}$
    double step = 0.0;              ///< 采样步长 / 离散步进增量（0.0 表示无步长限制）

    /* --- 离散变量配置 --- */
    std::vector< DiscreteOption > discreteOptions; ///< 离散可选集合（仅当 domain == Discrete 时有效）

    DesignVariableUnit unit = DesignVariableUnit::Unitless; ///< 物理量纲单位
    std::string frameId;            ///< 该变量作用的目标坐标系 ID
    bool enabled = true;            ///< 是否启用该变量（为 false 时不参与当前轮次的优化迭代）

    /* --- 依赖与派生关系表达 --- */
    std::vector< std::string > dependencies; ///< 依赖的上游变量 ID 列表（用于拓扑排序与依赖更新）
    std::string derivedExpressionId;         ///< 派生计算表达式或规则的标识符（当 role == Derived 时生效）

    std::string bindingId;          ///< 绑定的底层模型对象 ID（如 JointEdge ID 或 GeometryBinding ID）
    DesignVariableSource source = DesignVariableSource::User; ///< 变量创建来源
    std::string applicability;      ///< 适用条件描述规则（如特定工况条件或构型要求）
    std::string description;        ///< 详细工程说明文档 / Tooltip 提示文本
    DesignVariableStatus status = DesignVariableStatus::Available; ///< 当前可用状态
};

/**
 * @brief 设计变量集合的校验与诊断结果
 */
struct DesignVariableValidationResult
{
    bool valid = true; ///< 变量集整体逻辑是否自洽合法
    std::vector< StructureOptimizationDiagnostic > diagnostics; ///< 诊断错误与警告日志列表
};

/**
 * @brief 设计变量集合纯函数校验器
 */
class DesignVariableValidator
{
public:
    /**
     * @brief 对设计变量集合执行完整性与数学自洽性校验
     * @param variables 待校验的设计变量列表
     * @return DesignVariableValidationResult
     * @details 核心校验规则包括：
     * 1. 变量 ID 唯一性；
     * 2. 上下界合法性（$x_{min} \le x_{nominal} \le x_{max}$）；
     * 3. 依赖关系必须构成有向无环图 (DAG)，严禁循环依赖；
     * 4. 离散变量必须至少包含一个有效选项；
     * 5. 绑定的 `frameId` / `bindingId` 必须在拓扑模型中存在。
     */
    static DesignVariableValidationResult validate(
        const std::vector< DesignVariableDefinition >& variables);
};

/* --- 辅助序列化与字符串转换函数 --- */

/** @brief 变量角色枚举序列化与反序列化 */
std::string variableRoleToString(VariableRole role);
bool variableRoleFromString(const std::string& value, VariableRole& role);

/** @brief 变量值域枚举序列化与反序列化 */
std::string variableDomainToString(VariableDomain domain);
bool variableDomainFromString(const std::string& value, VariableDomain& domain);

/** @brief 语义类型枚举序列化与反序列化 */
std::string semanticKindToString(SemanticKind kind);
bool semanticKindFromString(const std::string& value, SemanticKind& kind);

/** @brief 物理单位枚举序列化与反序列化 */
std::string designVariableUnitToString(DesignVariableUnit unit);
bool designVariableUnitFromString(const std::string& value, DesignVariableUnit& unit);

}    // namespace rws

#endif    // RWS_STRUCTUREOPTIMIZATION_DESIGNVARIABLE_HPP