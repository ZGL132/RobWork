#ifndef RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONTYPES_HPP
#define RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONTYPES_HPP

// 基础类型: TaskPoint, RobotDesignContext, AnalysisWarning, KinematicThresholds, WorkspaceSamplingConfig
#include <rwslibs/robotanalysiscore/RobotAnalysisTypes.hpp>
#include <rwslibs/robotanalysiscore/EngineeringOptimizationTypes.hpp>
#include <rwslibs/robotanalysiscore/RequirementExecutionTypes.hpp>
#include <rwslibs/kinematicanalysis/KinematicAnalysisTypes.hpp>

#include <QJsonObject>

#include <memory>

#include <array>
#include <functional>
#include <string>
#include <vector>

namespace rws {

struct KinematicBaselineSnapshot;

// =============================================================================
//  设计变量 / 约束 / 策略 / 状态枚举
// =============================================================================

//! @brief 可优化的结构设计变量种类。
enum class StructureVariableKind
{
    JointPositionX,      //!< 关节 X 平移 (Transform 模式)
    JointPositionY,      //!< 关节 Y 平移 (Transform 模式)
    JointPositionZ,      //!< 关节 Z 平移 (Transform 模式)
    JointRotationRoll,   //!< 关节 X 轴旋转 (Roll)
    JointRotationPitch,  //!< 关节 Y 轴旋转 (Pitch)
    JointRotationYaw,    //!< 关节 Z 轴旋转 (Yaw)
    DhA,                 //!< DH 参数 a (沿 X 轴平移)
    DhD,                 //!< DH 参数 d (沿 Z 轴平移)
    BaseHeight,          //!< 基座高度
    TcpOffsetX,          //!< TCP X 偏移
    TcpOffsetY,          //!< TCP Y 偏移
    TcpOffsetZ,          //!< TCP Z 偏移
    LinkRadius,          //!< 连杆截面半径
    LinkWidth,           //!< 连杆截面宽度
    LinkHeight           //!< 连杆截面高度
};

//! @brief 结构约束种类。
enum class StructureConstraintKind
{
    ModelValid,                //!< 模型必须有效 (不存在被零除、NaN 等情况)
    RequiredTaskReachable,     //!< 必需任务点必须可达
    RequiredTaskCollisionFree, //!< 必需任务点必须无碰撞
    MinimumJointMargin,        //!< 所有关节裕度最小值
    MaximumTotalLength,        //!< 运动链总长度上限
    MaximumBaseHeight,         //!< 基座高度上限
    MaximumCrossSection,       //!< 最大横截面积
    MaximumLinkSlenderness,    //!< 最大连杆长细比
    MinimumWorkspaceCoverage   //!< 最低工作空间覆盖率
};

//! @brief 优化搜索策略。
enum class StructureStrategyKind
{
    Random,   //!< 纯随机采样
    Grid,     //!< 网格遍历
    Hybrid    //!< 混合策略 (网格粗搜 + 局部随机精细)
};

//! @brief 评估阶段 (粗评 vs 精评)。
enum class StructureEvaluationStage
{
    Quick,     //!< 快速评估 (低采样、低精度)
    Verified   //!< 精确验证 (高采样、全碰撞检测)
};

//! @brief 候选解状态。
enum class StructureCandidateStatus
{
    Pending,     //!< 尚未评估
    Feasible,    //!< 通过所有硬约束
    Infeasible,  //!< 违反至少一个硬约束
    Failed,      //!< 评估过程中出错
    Canceled     //!< 用户取消或被调度器中断
};

// =============================================================================
//  优化任务点
// =============================================================================
//! @brief 带 required 标记的任务点。
struct OptimizationTaskPoint
{
    TaskPoint point;       //!< 基础任务点 (位姿、容差等)
    bool      required = true;  //!< 是否为必需任务点
};

/**
 * @brief 工程需求冻结工件在结构优化项目中的审计溯源信息。
 *
 * 结构优化器不保存也不重新解释需求编辑器中的表单字段，而是只记录已冻结工件的
 * 内容指纹、冻结时的 WorkCell/State 指纹和编译器版本。这样优化项目、报告和导出
 * 结果都能回答“本次结果基于哪一份经过验证的工程需求”，并在需求或场景变化后让
 * 上层工作流明确要求重新冻结，而不是将旧结果伪装为仍然有效。
 */
struct EngineeringRequirementProvenance
{
    std::string requirementFingerprint; //!< 冻结 RequirementSet 的 SHA256 内容指纹
    std::string workcellFingerprint;    //!< 冻结时 WorkCell 与 State 的联合 SHA256 指纹
    std::string environmentFingerprint; //!< 冻结时外部工装与环境 SHA256 指纹
    std::string compilerVersion;        //!< 生成冻结工件的需求编译器/冻结器版本
    // 使用 UTC ISO-8601 时间戳记录需求工件真正完成冻结的时刻。它不参与优化评分，
    // 但使报告、导出项目和问题追溯可以按时间线核对“哪一次冻结”提供了输入。
    std::string executionFingerprint;
    std::string frozenAt;
};

/**
 * @brief 由冻结需求工件提供给候选模型工厂的场景重建快照。
 *
 * 它只表示冻结时的工装/工件环境和来源审计信息，不替代 context.modelSpec 中待优化的
 * 机器人本体。候选评价时用该快照补入 Frame、场景几何和碰撞模型，再写出变异后的机器人。
 */
struct StructureOptimizationScenarioSnapshot
{
    int schemaVersion = 0;
    std::string sourceWorkCellPath;
    std::string sourceFileFingerprint;
    std::string snapshotFingerprint;
    std::string deviceName;
    std::string environmentFingerprint;
    std::string stateFingerprint;
    RobotModelSpec sceneSpec;
    // 场景文件的运行时根目录：用于把项目相对路径解析为磁盘绝对路径。仅存在于内存，
    // 不属于序列化字段，旧项目缺失时按空字符串处理。
    // Runtime-only root for project-relative scenario paths; it is not serialized.
    std::string baseDirectory;

    // 快照是否可用于重建场景：只有同时具备版本号与指纹才算有效，
    // 避免"仅有版本号而无指纹"的半填充对象被误用作重建输入。
    bool available() const { return schemaVersion > 0 && !snapshotFingerprint.empty(); }
};

enum class CanonicalModelShadowStatus { CanonicalModelMissing, Current, Stale, Invalid };

struct CanonicalModelShadow
{
    CanonicalModelShadowStatus status = CanonicalModelShadowStatus::CanonicalModelMissing;
    std::shared_ptr< KinematicBaselineSnapshot > snapshot;
    bool hasSnapshot() const { return snapshot != nullptr; }
};

// =============================================================================
//  设计变量
// =============================================================================
//! @brief 单个可优化的结构设计变量。
struct StructureDesignVariable
{
    std::string id;                        //!< 唯一标识符
    std::string label;                     //!< 显示标签
    std::string targetName;                //!< 目标关节/坐标系名称
    std::string unit;                      //!< 物理单位字符串 (如 "mm", "deg")
    StructureVariableKind kind = StructureVariableKind::JointPositionX; //!< 变量种类

    double currentValue   = 0.0;           //!< 当前值
    double minimum        = 0.0;           //!< 最小值
    double maximum        = 1.0;           //!< 最大值
    double step           = 0.1;           //!< 搜索步长

    double preferredValue      = 0.0;      //!< 工程师偏好值 (目标值)
    double preferenceWeight     = 0.0;      //!< 偏好权重 [0, 1]

    bool enabled                  = true;  //!< 是否参与优化
    bool syncAssociatedGeometry   = false; //!< 是否自动同步关联连杆几何
    // 变量定义域（连续/整数/离散）及其离散取值列表，决定搜索空间的取值方式。
    EngineeringVariableDomainDefinition domainDefinition;
};

// =============================================================================
//  约束条件
// =============================================================================
//! @brief 优化约束条件。
struct StructureConstraint
{
    std::string id;                             //!< 唯一标识符
    std::string label;                          //!< 显示标签
    std::string targetName;                     //!< 目标名称 (关节/坐标系/任务点 ID)
    StructureConstraintKind kind = StructureConstraintKind::ModelValid; //!< 约束种类

    double threshold          = 0.0;            //!< 主阈值
    double secondaryThreshold = 0.0;            //!< 辅助/第二阈值

    bool enabled = true;                        //!< 是否启用
    bool hard    = true;                        //!< true=硬约束 (必须满足) / false=软约束 (优化倾向)
};

// =============================================================================
//  权重配置
// =============================================================================
//! @brief 多目标优化中各目标的权重。
struct StructureOptimizationWeights
{
    double reachability    = 0.35;  //!< 可达性权重
    double manipulability  = 0.20;  //!< 可操作度权重
    double jointMargin     = 0.15;  //!< 关节裕度权重
    double collision       = 0.15;  //!< 碰撞避免权重
    double compactness     = 0.10;  //!< 紧凑度权重
    double preference      = 0.05;  //!< 工程偏好权重
};

// =============================================================================
//  工作空间覆盖盒
// =============================================================================
//! @brief 工作空间覆盖评估的包围盒。
struct WorkspaceCoverageBox
{
    std::string id;                                      //!< 区域唯一标识，用于硬约束和报告逐项关联
    std::string referenceFrame = "WORLD";               //!< 最小/最大坐标所属的冻结场景参考系
    std::array< double, 3 > minimum = {{ -1.0, -1.0, -1.0 }};  //!< 包围盒最小值 (x, y, z) [m]
    std::array< double, 3 > maximum = {{ 1.0, 1.0, 1.0 }};     //!< 包围盒最大值 (x, y, z) [m]
    std::array< int, 3 >    cells   = {{ 10, 10, 10 }};         //!< 各轴单元格数
    bool   enabled    = false;                    //!< 是否启用工作空间覆盖评估
};

// =============================================================================
//  评估配置
// =============================================================================
//! @brief 评估配置: 阈值、粗评/精评采样参数、覆盖盒。
struct StructureEvaluationConfig
{
    KinematicThresholds   thresholds;        //!< 运动学阈值
    WorkspaceSamplingConfig quickWorkspace;  //!< 快速评估阶段的采样参数
    WorkspaceSamplingConfig verifiedWorkspace; //!< 精确验证阶段的采样参数
    WorkspaceCoverageBox  coverageBox;       //!< 工作空间覆盖盒
    // coverageBox 是旧项目和现有编辑器使用的单区域兼容入口；冻结需求导入时必须使用
    // coverageBoxes 保留每个 Must 区域的 ID、参考系和独立覆盖率，不能将多个区域合并。
    std::vector< WorkspaceCoverageBox > coverageBoxes;
    bool checkCollision = true;              //!< 是否启用碰撞检测
    std::string evaluatorId = "structure.kinematics";
    std::string evaluatorVersion = "1";
};

// =============================================================================
//  运行配置
// =============================================================================
//! @brief 优化运行参数。
struct StructureOptimizationRunConfig
{
    StructureStrategyKind strategy           = StructureStrategyKind::Hybrid;

    int candidateCount           = 300;      //!< 总候选解数
    int eliteCount               = 20;       //!< 精英候选数 (进入下一轮)
    int localEliteCount          = 5;        //!< 局部精英数
    int finalVerificationCount   = 3;        //!< 最终精确验证的候选数
    int maxLocalSweeps           = 20;       //!< 局部精细搜索最大扫描数
    int gridSteps                = 3;        //!< 网格模式每维步数

    unsigned int randomSeed      = 1;        //!< 随机种子 (0 表示随机)
};

// =============================================================================
//  任务点指标
// =============================================================================
//! @brief 单个任务点的评估指标。
struct StructureTaskMetric
{
    std::string taskId;              //!< 任务点 ID
    std::string taskName;            //!< 任务点名称
    std::string failure;             //!< 失败原因描述 (空表示未失败)

    bool required          = true;   //!< 是否为必需任务点
    bool reachable         = false;  //!< 是否可达
    bool inCollision       = false;  //!< 是否处于碰撞

    double weight           = 1.0;   //!< 权重
    double manipulability   = 0.0;   //!< 可操作度
    double jointMargin      = 0.0;   //!< 最小关节裕度

    int usableSolutionCount = 0;     //!< 可用 IK 解数
};

/**
 * @brief 单个冻结工作空间区域的覆盖率结果。
 *
 * TCP 样本先被转换到区域 referenceFrame，再按区域局部盒网格统计，因此旋转或移动的
 * 工装坐标系不会被错误地当作 WORLD 轴对齐区域。该结果通过 id 供约束和报告准确引用。
 */
struct StructureWorkspaceRegionMetric
{
    std::string id;
    std::string referenceFrame;
    double coverage = 0.0;
    std::size_t occupiedCellCount = 0;
    std::size_t totalCellCount = 0;
};

// =============================================================================
//  原始指标
// =============================================================================
//! @brief 候选解的原始评估指标。
struct StructureRawMetrics
{
    bool modelValid = false;         //!< 模型是否有效

    int requiredTaskCount     = 0;   //!< 必需任务点数
    int requiredReachableCount = 0;  //!< 可达的必需任务点数
    int optionalTaskCount     = 0;   //!< 可选任务点数
    int optionalReachableCount = 0;  //!< 可达的可选任务点数

    double weightedReachability   = 0.0; //!< 加权可达性
    bool taskEvaluationDataInsufficient = false; //!< 必需的任务验证未产生完整证据
    double manipulabilityP10      = 0.0; //!< 可操作度 10 分位数
    double jointMarginP10         = 0.0; //!< 关节裕度 10 分位数
    double minimumJointMargin     = 0.0; //!< 全局最小关节裕度

    double collisionFreeRate      = 0.0; //!< 无碰撞样本比例 [0, 1]
    double workspaceCoverage      = 0.0; //!< 工作空间覆盖率 [0, 1]
    bool workspaceCoverageDataInsufficient = false; //!< 覆盖率采样未产生可用数据
    std::size_t workspaceOccupiedCellCount = 0; //!< 覆盖率已占用栅格数
    std::size_t workspaceTotalCellCount = 0; //!< 覆盖率栅格总数
    std::vector< StructureWorkspaceRegionMetric > workspaceRegionMetrics; //!< 多区域覆盖率明细

    double totalKinematicLength   = 0.0; //!< 运动链总长度 (m)
    double baseHeight             = 0.0; //!< 基座高度 (m)
    double maxCrossSection        = 0.0; //!< 最大横截面积 (m^2)
    double maxLinkSlenderness     = 0.0; //!< 最大连杆长细比

    double engineeringPreference       = 0.0; //!< 工程偏好吻合度 [0, 1]
    double modelBuildSeconds           = 0.0; //!< 模型构建耗时 (s)
    double kinematicEvaluationSeconds  = 0.0; //!< 运动学评估耗时 (s)
    double workspaceEvaluationSeconds  = 0.0; //!< 工作空间评估耗时 (s)

    std::vector< StructureTaskMetric > taskMetrics; //!< 各任务点指标
};

// =============================================================================
//  分量得分
// =============================================================================
//! @brief 候选解在各优化目标上的分量得分。
struct StructureComponentScores
{
    double reachability   = 0.0; //!< 可达性得分
    double manipulability = 0.0; //!< 可操作度得分
    double jointMargin    = 0.0; //!< 关节裕度得分
    double collision      = 0.0; //!< 碰撞得分
    double compactness    = 0.0; //!< 紧凑度得分
    double preference     = 0.0; //!< 工程偏好得分
};

// =============================================================================
//  候选解结果
// =============================================================================
//! @brief 单个候选解的完整结果。
struct StructureCandidateResult
{
    int index = -1;                      //!< 候选解索引

    std::vector< double > values;        //!< 设计变量值集 (顺序与 problem.variables 一致)

    StructureCandidateStatus  status  = StructureCandidateStatus::Pending;
    StructureEvaluationStage  stage   = StructureEvaluationStage::Quick;

    bool   feasible   = false;          //!< 是否满足所有硬约束
    double totalScore = 0.0;            //!< 加权综合得分

    StructureRawMetrics       raw;                  //!< 原始指标
    StructureComponentScores  scores;               //!< 分量得分

    std::vector< std::string > violatedConstraints; //!< 违反的约束 ID 列表
    std::vector< std::string > warnings;            //!< 候选解级别警告
};

// =============================================================================
//  进度信息
// =============================================================================
//! @brief 优化进度报告。
struct StructureProgress
{
    std::string stage;             //!< 当前阶段名 (如 "Generating", "Quick", "Verified")
    int         completed = 0;     //!< 已完成数
    int         planned   = 0;     //!< 计划总数
    double      bestScore = 0.0;   //!< 当前最佳得分
};

// =============================================================================
//  运行诊断
// =============================================================================
//! @brief 优化运行性能诊断。
struct StructureRunDiagnostics
{
    int    generatedCandidates    = 0;    //!< 生成的候选解数
    int    evaluatedCandidates    = 0;    //!< 已评估的候选解数
    int    cacheHits             = 0;    //!< 缓存命中数
    int    quickEvaluatedCandidates = 0; //!< Quick 阶段实际评估次数
    int    verifiedEliteCandidates = 0; //!< 精英 Verified 阶段实际评估次数
    int    finalVerifiedCandidates = 0; //!< 最终 Verified 阶段实际评估次数
    int    sensitivityEvaluations = 0;  //!< 灵敏度分析实际评估次数

    double totalSeconds                = 0.0; //!< 总耗时
    double modelBuildSeconds           = 0.0; //!< 模型构建耗时
    double kinematicEvaluationSeconds  = 0.0; //!< 运动学评估耗时
    double workspaceEvaluationSeconds  = 0.0; //!< 工作空间评估耗时
};

// =============================================================================
//  优化问题
// =============================================================================
/**
 * @brief 结构优化问题的根对象，聚合优化所需的全部输入。
 *
 * 除传统设计变量/任务点外，自 P1 起携带冻结需求工件提供的三块信息：
 *  - requirementProvenance：需求与场景的内容指纹及冻结时间，用于审计与失效检测；
 *  - requirementExecution：冻结后的执行契约，Verified 阶段由公共 evaluator 直接消费；
 *  - scenarioSnapshot：冻结时的工装/环境重建快照，供候选模型工厂补全场景。
 * 三者共同保证"优化结果可追溯到一份经过验证的工程需求"。
 */
//! @brief 完整的结构优化问题定义。
struct StructureOptimizationProblem
{
    RobotDesignContext              context;     //!< 机器人设计上下文
    std::vector< OptimizationTaskPoint > tasks;      //!< 任务点列表
    std::vector< StructureDesignVariable >  variables;  //!< 设计变量列表
    std::vector< StructureConstraint >      constraints; //!< 约束条件列表

    StructureOptimizationWeights    weights;    //!< 多目标权重
    std::vector< ObjectiveTerm >      objectives; //!< 通用指标目标 (P1 起可持久化)
    std::vector< ConstraintRule >     metricConstraints; //!< 通用指标约束
    StructureEvaluationConfig       evaluation; //!< 评估配置
    StructureOptimizationRunConfig  run;        //!< 运行配置
    EngineeringRequirementProvenance requirementProvenance; //!< 可选的需求冻结工件审计来源
    RequirementExecutionSet requirementExecution; //!< 冻结执行契约，供公共 evaluator 直接消费
    StructureOptimizationScenarioSnapshot scenarioSnapshot; //!< 冻结场景重建信息
    CanonicalModelShadow canonicalModelShadow; //!< Optional canonical migration shadow.
    QJsonObject extensions; //!< Unknown root JSON fields retained for forward-compatible read/save.
};

// =============================================================================
//  灵敏度分析
// =============================================================================
//! @brief 单个设计变量的灵敏度分析入口。
struct StructureSensitivityEntry
{
    std::string variableId;                              //!< 变量 ID
    double delta = 0.0;                                   //!< 扰动步长绝对值
    double perturbedValue = 0.0;                          //!< 扰动后的变量值
    double scoreDrop = 0.0;                               //!< 综合得分下降量
    bool feasible = false;                                //!< 扰动后是否仍可行
    std::vector<std::string> violatedConstraints;          //!< 扰动后违反的约束
};

//! @brief 灵敏度分析结果。
struct StructureSensitivityResult
{
    std::vector<StructureSensitivityEntry> entries;       //!< 各变量灵敏度入口
    double maximumScoreDrop = 0.0;                        //!< 最大得分下降
    double meanScoreDrop = 0.0;                           //!< 平均得分下降
    std::vector<std::string> criticalVariableIds;          //!< 关键变量 ID
    std::string robustnessGrade = "Unknown";              //!< 鲁棒性等级 A/B/C/D
};

// =============================================================================
//  优化结果
// =============================================================================
/**
 * @brief 基线候选的不可变审计身份。
 *
 * legacy candidates 仅用于现有 UI 展示；本结构保存新流水线的候选、模型、环境、
 * 工具与计划指纹，避免后续候选投影覆盖基线的可追溯性。
 */
struct BaselineEvaluationAudit
{
    int index = -1;
    std::string candidateFingerprint;
    std::string modelFingerprint;
    std::string environmentFingerprint;
    std::string toolFingerprint;
    std::string planFingerprint;
};

//! @brief 结构优化运行结果。
struct StructureOptimizationResult
{
    bool canceled = false;                //!< 是否被取消

    std::string startedAt;                //!< 开始时间 (ISO 8601)
    std::string completedAt;              //!< 完成时间 (ISO 8601)

    int baselineCandidateIndex = -1;      //!< 基线候选解 (原始设计) 索引
    int bestCandidateIndex     = -1;      //!< 最佳候选解索引
    BaselineEvaluationAudit baselineAudit; //!< 新流水线基线的审计身份

    std::vector< StructureCandidateResult > candidates;   //!< 所有候选解
    StructureRunDiagnostics                 diagnostics;   //!< 运行诊断
    StructureSensitivityResult              sensitivity;   //!< 灵敏度分析结果
    std::vector< AnalysisWarning >          warnings;      //!< 全局警告
};

} // namespace rws

#endif // RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONTYPES_HPP
