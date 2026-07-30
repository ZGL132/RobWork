#ifndef RWS_ENGINEERINGREQUIREMENTS_ENGINEERINGREQUIREMENTTYPES_HPP
#define RWS_ENGINEERINGREQUIREMENTS_ENGINEERINGREQUIREMENTTYPES_HPP

#include <array>
#include <string>
#include <vector>

namespace rws {

/**
 * @brief 需求等级枚举
 * 定义工程需求在算法优化和路径规划中的硬性/软性约束程度
 */
enum class RequirementLevel { 
    Must,   ///< 强约束：必须满足（若无法满足则会导致编译或规划失败）
    Should, ///< 软约束：尽量满足（若无法满足会给出诊断警告，但不阻断流程）
    Info    ///< 信息项：仅作为参考或备注（不参与底层算法计算）
};

/**
 * @brief 工位来源类型枚举
 * 记录该工位是如何在系统中被创建出来的
 */
enum class PoseTaskSource { 
    Manual,          ///< 手工输入或编辑
    CapturedTcp,     ///< 从当前 3D 场景中的机器人 TCP 直接捕获
    FrameOffset,     ///< 基于某个参考系的相对偏移量生成（例如镜像生成）
    GeometryFeature, ///< 关联了 3D 几何特征（如几何体 Surface/Frame）
    Template,        ///< 通过工艺模板（如料箱取料、码垛）或阵列算法批量生成
    Imported         ///< 从外部文件（CSV/JSON）导入
};

/**
 * @brief 工艺类型枚举
 * 赋予工位具体的机器人应用语义，便于后续针对不同工艺调用不同的优化策略
 */
enum class ProcessType { 
    Generic,       ///< 通用工位
    Pick,          ///< 取料工位
    Place,         ///< 放料工位
    MachineLoad,   ///< 机床上料
    MachineUnload, ///< 机床下料
    Inspect,       ///< 视觉/传感器检测
    WeldStart,     ///< 焊缝起点
    WeldEnd,       ///< 焊缝终点
    ToolChange,    ///< 换工具工位
    SafeStandby,   ///< 安全待机点
    Handover       ///< 人机/双机交接工位
};

/**
 * @brief 姿态规则/对齐模式枚举
 */
enum class OrientationMode { 
    Fixed,               ///< 固定姿态（显式使用 Roll-Pitch-Yaw 旋转角）
    AlignFrame,          ///< 对齐到指定坐标系（Frame）的姿态
    AlignGeometryNormal, ///< 对齐到几何特征的法线方向（如工件表面法线）
    PointAtTarget        ///< 工具 Z 轴指向目标点/目标坐标系
};

/**
 * @brief 接近与撤离的偏移参考轴
 */
enum class OffsetAxis { 
    ToolZ,     ///< 沿工具坐标系（TCP）的 Z 轴偏移
    ReferenceZ ///< 沿参考坐标系（Reference Frame）的 Z 轴偏移
};

/**
 * @brief 几何特征类型
 */
enum class GeometryFeatureType { 
    None,             ///< 无几何特征关联
    FrameOrigin,      ///< 绑定到几何 Frame 的原点
    FramePlaneNormal  ///< 绑定到几何 Frame 的平面法线方向
};

/**
 * @brief 机器人模型绑定信息
 * 用于确保研发需求与特定的机器人规格/结构强关联，支持版本追溯与指纹校验
 */
struct RobotModelBinding {
    std::string sourcePath;            ///< 机器人模型文件（.rmb.json）的磁盘路径
    std::string robotModelFingerprint; ///< 机器人模型内容的哈希指纹（SHA256），防止模型被篡改
    std::string robotName;             ///< 机器人模型名称（如 UR-6-85-5-A）
};

/**
 * @brief 位姿公差要求
 */
/**
 * @brief 工程需求域的位姿公差。
 *
 * 与 RobotAnalysisTypes.hpp 中供运动学/动力学计算使用的 PoseTolerance 分开
 * 命名，避免需求工件适配到分析插件时两个独立领域模型在 rws 命名空间发生
 * 重定义；二者的数值含义仍可由适配器显式转换。
 */
struct RequirementPoseTolerance {
    double positionMeters = 0.001;   ///< 位置容差（单位：米，默认 1mm）
    double orientationDeg = 1.0;     ///< 姿态容差（单位：度，默认 1°）
    bool allowToolRollFree = false;  ///< 是否允许末端工具绕轴自由滚转（轴对称工具如喷枪/吸盘通常设为 true，增加 IK 解空间）
};

/**
 * @brief 姿态约束规则
 * 详尽定义机器人到达工位时的末端姿态计算逻辑
 */
struct OrientationRule {
    OrientationMode mode = OrientationMode::Fixed; ///< 姿态对齐模式
    std::string targetFrame;                       ///< 对齐的目标坐标系名称
    std::string targetGeometry;                    ///< 对齐的目标几何体标识
    std::string targetPoint;                       ///< 指向模式下相对参考系的目标点文本，格式为 "x, y, z"（m）
    bool invertNormal = false;                     ///< 是否反转法线方向（如从背面接近）
    bool allowToolRollFree = false;                ///< 是否允许绕工具轴自由滚转
    double rollMinimumDeg = -180.0;                ///< 滚转角下限（单位：度）
    double rollMaximumDeg = 180.0;                ///< 滚转角上限（单位：度）
    // 此字段只由冻结阶段的姿态解析器写入，记录规则、目标来源及解析后的代表 RPY。
    // 它随冻结工件持久化，供结构优化报告审计；编辑态需求不依赖该字段作任何物理计算。
    std::string resolutionEvidence;
};

/**
 * @brief 接近（Approach）与撤离（Retract）路径动作规则
 */
struct ApproachRetractRule {
    bool enabled = false;              ///< 是否启用该动作规则
    OffsetAxis axis = OffsetAxis::ToolZ;///< 偏移方向参照轴
    double distanceMeters = 0.0;       ///< 接近/撤离距离（单位：米）
    bool collisionFreeRequired = true; ///< 动作段是否要求无碰撞（将在运动学优化段校验）
};

/**
 * @brief 可行性校验策略
 */
struct ValidationPolicy {
    bool collisionFreeRequired = true; ///< 是否强制要求工位无碰撞
    double minimumJointMargin = 0.0;   ///< 最小关节限位裕度（避免机器人接近关节极限，弧度或米）
    double minimumManipulability = 0.0;///< 最小可操作度裕度（避免机器人处于奇异位姿附近）
};

/**
 * @brief 3D 场景中的几何特征引用
 */
struct GeometryFeatureReference {
    GeometryFeatureType type = GeometryFeatureType::None; ///< 几何特征类型
    std::string frameName;                                ///< 目标 Frame 名称
    std::string objectName;                               ///< 所属 3D 模型对象名称
    std::string geometryName;                             ///< 几何体名
};

/**
 * @brief 模板/阵列生成的参数键值对
 */
struct GenerationParameter {
    std::string key;   ///< 参数名（如 "rowSpacingMeters"）
    std::string value; ///< 参数值字符串表示（如 "0.05"）
};

/**
 * @brief 批量工位生成溯源信息
 * 使模板生成的工位保持可追溯性，以便后续支持“一键更新模板”或“解除关联”
 */
struct StationGenerationProvenance {
    std::string generatorId;                   ///< 生成器/模板 ID（如 "BinPicking.v1"）
    std::string instanceId;                    ///< 模板生成的实例唯一 ID（如 "bin_A"）
    bool linked = false;                       ///< 是否仍与模板保持关联（若为 false 表示已被工程师手动取消关联）
    std::vector<GenerationParameter> parameters;///< 生成时使用的参数快照
};

/**
 * @brief 外部数据导入溯源信息
 * 记录数据的原始文件及行号，便于后续审计和定位问题数据
 */
struct ImportProvenance {
    std::string sourcePath; ///< 导入源 CSV/JSON 文件路径
    int recordNumber = 0;   ///< 原始文件中的记录行号/索引
};

/**
 * @brief 关键工位核心结构体（编辑态）
 * 包含了定义一个关键工位所需的所有工程意图、公差、规则与溯源信息
 */
struct KeyStation {
    std::string id;                                     ///< 工位唯一标识符
    std::string name;                                   ///< 工位显示名称
    ProcessType processType = ProcessType::Generic;     ///< 工艺语义类型
    RequirementLevel level = RequirementLevel::Must;    ///< 需求等级
    PoseTaskSource source = PoseTaskSource::Manual;     ///< 工位来源
    std::string refFrame = "WORLD";                     ///< 参考坐标系名称（默认世界坐标系）
    std::string tcpFrame;                               ///< 机器人末端工具坐标系（TCP）名称
    std::array<double, 3> position = {{0.0, 0.0, 0.0}}; ///< 位置 [X, Y, Z]（单位：米）
    std::array<double, 3> rpyDeg = {{0.0, 0.0, 0.0}};   ///< 固定姿态 [Roll, Pitch, Yaw]（单位：度）
    RequirementPoseTolerance tolerance;                 ///< 公差要求
    GeometryFeatureReference geometryFeature;           ///< 绑定的几何特征
    StationGenerationProvenance generation;            ///< 模板/阵列生成溯源
    ImportProvenance importProvenance;                  ///< 外部导入溯源
    OrientationRule orientation;                       ///< 姿态生成规则
    ApproachRetractRule approach;                      ///< 接近动作规则
    ApproachRetractRule retract;                       ///< 撤离动作规则
    ValidationPolicy validation;                       ///< 校验策略
    double confidence = 1.0;                            ///< 工位可信度/权重 [0.0, 1.0]
    std::string note;                                   ///< 备注说明
};

/// 保持向下兼容的别名（MVP 早期及 JSON 字段中使用的名称）
using PoseTask = KeyStation;

/**
 * @brief 包络盒子/工作空间需求区域（编辑态）
 * 用于定义机器人必须覆盖或避开的空间 3D 区域
 */
struct BoxRegion {
    std::string id;                                   ///< 区域唯一标识符
    std::string name;                                 ///< 区域显示名称
    RequirementLevel level = RequirementLevel::Must;  ///< 需求等级
    std::string refFrame = "WORLD";                   ///< 参考坐标系
    std::array<double, 3> center = {{0.0, 0.0, 0.0}}; ///< 区域中心点位置 [X, Y, Z]（单位：米）
    std::array<double, 3> size = {{0.1, 0.1, 0.1}};   ///< 区域尺寸 [DX, DY, DZ]（单位：米）
    double minimumCoverage = 0.8;                     ///< 要求的最小空间覆盖率 [0.0, 1.0]（如 80%）
    int samplesPerAxis = 5;                           ///< 空间采样离散密度（每个轴的采样点数）
};

/**
 * @brief 未冻结的工程需求集合（编辑态数据模型）
 * 包含完整的人机交互编辑字段、未验证的建议项以及界面状态
 */
struct RequirementSet {
    int schemaVersion = 1;                  ///< 数据结构 Schema 版本号
    std::string name;                       ///< 需求集名称
    int version = 1;                        ///< 需求版本号
    bool frozen = false;                    ///< 冻结标志（true 表示已校验冻结，禁止直接编辑）
    RobotModelBinding modelBinding;         ///< 绑定的机器人模型信息
    std::vector<PoseTask> poseTasks;        ///< 关键工位列表
    std::vector<BoxRegion> boxRegions;      ///< 工作区域需求列表
};

/**
 * @brief 已编译/冻结的关键工位（执行态模型）
 * 去除了纯 UI/溯源相关的冗余数据，仅保留 downstream（运动学优化/路径规划）算法所需的字段
 */
struct CompiledPoseTask {
    std::string id;                                     ///< 工位 ID
    std::string name;                                   ///< 工位名称
    RequirementLevel level = RequirementLevel::Must;    ///< 需求等级
    std::string refFrame;                               ///< 参考坐标系
    std::string tcpFrame;                               ///< TCP 坐标系
    std::array<double, 3> position = {{0.0, 0.0, 0.0}}; ///< 解析后的位置 [X, Y, Z]
    std::array<double, 3> rpyDeg = {{0.0, 0.0, 0.0}};   ///< 解析后的姿态 [Roll, Pitch, Yaw]
    RequirementPoseTolerance tolerance;                 ///< 容差
    ProcessType processType = ProcessType::Generic;     ///< 工艺语义
    GeometryFeatureReference geometryFeature;           ///< 几何特征引用
    OrientationRule orientation;                       ///< 姿态规则
    ValidationPolicy validation;                       ///< 校验策略
    bool pathValidationPending = false;                 ///< 是否存在待处理的接近/撤离路径校验
};

/**
 * @brief 已编译/冻结的工作空间需求区域
 */
struct WorkspaceDemandRegion {
    std::string id;                                   ///< 区域 ID
    std::string name;                                 ///< 区域名称
    RequirementLevel level = RequirementLevel::Must;  ///< 需求等级
    std::string refFrame;                             ///< 参考坐标系
    std::array<double, 3> center = {{0.0, 0.0, 0.0}}; ///< 中心点
    std::array<double, 3> size = {{0.1, 0.1, 0.1}};   ///< 尺寸
    double minimumCoverage = 0.8;                     ///< 要求覆盖率
    int samplesPerAxis = 5;                           ///< 采样密度
};

/**
 * @brief 需求校验诊断日志
 * 在 RequirementCompiler 编译/校验需求时生成的错误或警告信息
 */
struct RequirementDiagnostic {
    std::string requirementId;                      ///< 出错的需求/工位 ID
    RequirementLevel level = RequirementLevel::Must;///< 严重程度
    std::string message;                            ///< 诊断详细描述信息
    bool blocking = true;                           ///< 是否为阻断性错误（true 会阻断编译冻结）
};

/**
 * @brief 已编译/冻结的工程需求集合（只读执行态模型）
 * 经过 `RequirementCompiler::compile()` 校验产出，带有可信的算法指纹，作为下游求解器的输入
 */
struct CompiledRequirementSet {
    int schemaVersion = 1;                              ///< Schema 版本号
    bool frozen = false;                                ///< 是否成功冻结
    std::string compilerVersion = "EngineeringRequirements.MVP.1"; ///< 编译器版本信息
    RobotModelBinding modelBinding;                     ///< 绑定的机器人模型
    std::string requirementFingerprint;                 ///< 整个需求集内容计算出的 SHA256 指纹
    std::vector<CompiledPoseTask> poseTasks;            ///< 编译通过的关键工位集合
    std::vector<WorkspaceDemandRegion> workspaceRegions;///< 编译通过的工作区域集合
    std::vector<RequirementDiagnostic> diagnostics;    ///< 编译过程中收集到的非阻断性诊断日志
};

// ============================================================================
// 辅助枚举转换工具函数（用于字符串与枚举双向映射，常用于 JSON 读写及 UI 显示）
// ============================================================================
const char* toString(RequirementLevel value);
const char* toString(PoseTaskSource value);
const char* toString(ProcessType value);
const char* toString(OrientationMode value);
const char* toString(OffsetAxis value);
const char* toString(GeometryFeatureType value);

bool requirementLevelFromString(const std::string& text, RequirementLevel& value);
bool poseTaskSourceFromString(const std::string& text, PoseTaskSource& value);
bool processTypeFromString(const std::string& text, ProcessType& value);
bool orientationModeFromString(const std::string& text, OrientationMode& value);
bool offsetAxisFromString(const std::string& text, OffsetAxis& value);
bool geometryFeatureTypeFromString(const std::string& text, GeometryFeatureType& value);

} // namespace rws

#endif
