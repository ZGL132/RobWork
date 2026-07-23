#ifndef RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONTYPES_HPP
#define RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONTYPES_HPP

// 基础类型: TaskPoint, RobotDesignContext, AnalysisWarning, KinematicThresholds, WorkspaceSamplingConfig
#include <rwslibs/robotanalysiscore/RobotAnalysisTypes.hpp>
#include <rwslibs/kinematicanalysis/KinematicAnalysisTypes.hpp>

#include <array>
#include <functional>
#include <string>
#include <vector>

namespace rws {

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
    bool checkCollision = true;              //!< 是否启用碰撞检测
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
    double manipulabilityP10      = 0.0; //!< 可操作度 10 分位数
    double jointMarginP10         = 0.0; //!< 关节裕度 10 分位数
    double minimumJointMargin     = 0.0; //!< 全局最小关节裕度

    double collisionFreeRate      = 0.0; //!< 无碰撞样本比例 [0, 1]
    double workspaceCoverage      = 0.0; //!< 工作空间覆盖率 [0, 1]

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

    double totalSeconds                = 0.0; //!< 总耗时
    double modelBuildSeconds           = 0.0; //!< 模型构建耗时
    double kinematicEvaluationSeconds  = 0.0; //!< 运动学评估耗时
    double workspaceEvaluationSeconds  = 0.0; //!< 工作空间评估耗时
};

// =============================================================================
//  优化问题
// =============================================================================
//! @brief 完整的结构优化问题定义。
struct StructureOptimizationProblem
{
    RobotDesignContext              context;     //!< 机器人设计上下文
    std::vector< OptimizationTaskPoint > tasks;      //!< 任务点列表
    std::vector< StructureDesignVariable >  variables;  //!< 设计变量列表
    std::vector< StructureConstraint >      constraints; //!< 约束条件列表

    StructureOptimizationWeights    weights;    //!< 多目标权重
    StructureEvaluationConfig       evaluation; //!< 评估配置
    StructureOptimizationRunConfig  run;        //!< 运行配置
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
//! @brief 结构优化运行结果。
struct StructureOptimizationResult
{
    bool canceled = false;                //!< 是否被取消

    std::string startedAt;                //!< 开始时间 (ISO 8601)
    std::string completedAt;              //!< 完成时间 (ISO 8601)

    int baselineCandidateIndex = -1;      //!< 基线候选解 (原始设计) 索引
    int bestCandidateIndex     = -1;      //!< 最佳候选解索引

    std::vector< StructureCandidateResult > candidates;   //!< 所有候选解
    StructureRunDiagnostics                 diagnostics;   //!< 运行诊断
    StructureSensitivityResult              sensitivity;   //!< 灵敏度分析结果
    std::vector< AnalysisWarning >          warnings;      //!< 全局警告
};

} // namespace rws

#endif // RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONTYPES_HPP
