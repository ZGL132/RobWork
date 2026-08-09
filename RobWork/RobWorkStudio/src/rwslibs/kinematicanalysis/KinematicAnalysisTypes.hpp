#ifndef RWS_KINEMATICANALYSIS_KINEMATICANALYSISTYPES_HPP
#define RWS_KINEMATICANALYSIS_KINEMATICANALYSISTYPES_HPP

// 引入机器人分析插件共用的基础类型。
//   - AnalysisStatus     :分析状态枚举(Pass/Warning/Fail/Unknown)
//   - AnalysisWarning    :通用告警结构(code/severity/source/message)
//   - AnalysisResultHeader:报告头(pluginName/version/timestamp)
//   - TaskPoint          :任务点位姿数据结构(id/name/position/rpy/tolerance)
//   - TaskPointType      :任务点类型枚举(Generic/Pose/...)
//   - MetricValue        :度量名+数值结构(供 manipulabilityMap 等使用)
// 这些类型在 RobotAnalysisCore 插件中定义并被本插件的所有子模块共享。
#include <rwslibs/robotanalysiscore/RobotAnalysisTypes.hpp>
#include <rwslibs/robotanalysiscore/RequirementExecutionTypes.hpp>

#include <rw/math/Q.hpp>
#include <rw/math/Transform3D.hpp>

#include <array>
#include <string>
#include <vector>

namespace rws {

// =============================================================================
//  证据阶段(evidence stage)枚举
// =============================================================================
//
// AnalysisEvidenceStage 描述一条分析结论的"证据强度/可信度",区分估算、
// 快速验证与验收级验证三类证据。
//   - Estimated:估算值(例如随机采样的工作空间样本,未经逐点精确验证);
//   - Quick    :快速探索阶段的结论(牺牲部分精度换取速度);
//   - Verified :已通过 Verified 验收级验证的结论(最可信)。
// 阶段沿 Estimated -> Quick -> Verified 单调递增;下游 UI / Report 据此提示用户
// 当前结论处于哪个证据等级,避免把"估算"误当作"验收"。
enum class AnalysisEvidenceStage
{
    Estimated,
    Quick,
    Verified
};

// =============================================================================
//  可行性(feasibility)枚举
// =============================================================================
//
// Feasibility 回答"在当前证据下,该需求/结论是否可行":
//   - Feasible        :有足够证据表明可行;
//   - Infeasible      :有证据表明不可行(IK 无解、碰撞、超限位等);
//   - DataInsufficient:证据不足,既不能证明可行也不能证明不可行;
//   - NotEvaluated    :尚未评估(结构默认值)。
// 与 Quality 正交:Quality 回答"即使可行,质量多好",Feasibility 回答"能不能做"。
// 聚合规则见 buildRequirementValidationSummary / buildAggregateResult。
enum class Feasibility
{
    Feasible,
    Infeasible,
    DataInsufficient,
    NotEvaluated
};

// =============================================================================
//  质量(quality)枚举
// =============================================================================
//
// Quality 是 Feasibility 的补充维度,描述"可行解的质量等级":
//   - Good    :质量良好(裕度充足、无奇异、无碰撞);
//   - Degraded:可行但质量退化(接近奇异 / 接近限位 / 可操作度偏低);
//   - Critical:严重退化或不可用(奇异、碰撞、数据不足等);
//   - Unknown :未知(未评估或证据不足)。
// 约定:仅在 Feasibility == Feasible 时 Quality 才有积极意义,其余情况多为
// Unknown 或 Critical;具体映射由各聚合函数负责,便于 UI 用颜色区分等级。
enum class Quality
{
    Good,
    Degraded,
    Critical,
    Unknown
};

// =============================================================================
//  长度/角度单位枚举
// =============================================================================
//
// 这两个枚举描述 IK 输入框与单位显示偏好:不同地区的工程师可能用度或弧度,
// 用毫米而不是米,本插件 UI 通过这两个枚举动态切换显示/解析单位。
enum class KinematicLengthUnit
{
    Meters,        // SI 单位;RobWork 内部坐标系使用
    Centimeters,    // 一些机械设计图纸常用
    Millimeters,    // 机械精度常用
    Inches          // 欧美部分离线工具使用
};

enum class KinematicAngleUnit
{
    Degrees,        // 最直观,RobWork 内部 RPY 默认使用
    Radians,        // 与 IK 求解器内部一致
    Grads,          // 一些测量仪器用 400 grad = 360°
    Turns            // 用 1 表示 360°
};

// =============================================================================
//  运动学分析失败原因枚举
// =============================================================================
//
// KinematicFailureReason 用于区分"分析失败"的具体根因。
// 注意它与 AnalysisStatus(整体状态枚举)不同 —— 失败原因细化到具体原因,
// 而状态枚举只给出"严重程度等级"。
// 出现在 report JSON / 警告表格 / UI 标签中,让用户明白下次该改什么。
enum class KinematicFailureReason
{
    None,            // 占位:无失败或不属于失败
    NoDevice,        // WorkCell 中找不到可用 Device(可能是空 WorkCell)
    NoTcpFrame,      // TCP 帧未配置且设备没有默认末端帧
    IkNoSolution,    // IK 求解器返回空解集(目标不可达 / 关节范围不够)
    Collision,       // 解处于碰撞状态(需要重新规划)
    TargetResidual,  // FK 验算残差超过任务点允许的位置/姿态容差
    JointLimit,      // 解超出关节限位
    NearJointLimit,  // 解接近关节限位(不是 Fail,但要 Warning)
    Singular,        // 解处雅可比奇异(条件数过差,目标姿态无法精确达到)
    NearSingular,    // 解处雅可比条件数恶化但未奇异
    InvalidTarget,   // 目标位姿本身无效(超出 FK 可达范围等)
    SolverError,     // 求解器抛出异常(底层 IK 库错误)
    CollisionDetectorUnavailable,
    FrameNotFound
};

// =============================================================================
//  工作空间表格的着色策略枚举
// =============================================================================
//
// WorkspaceColorMode 仅影响可视化偏好的颜色选择 —— 实际分析逻辑(可达性判断等)
// 不依赖此枚举。UI 在 Workspace tab 的 Color 下拉中让用户选择。
enum class WorkspaceColorMode
{
    Reachability,        // 按 IK 是否成功着色
    Manipulability,      // 按可操作度(σ 之积)着色
    JointLimitMargin,    // 按关节裕度着色(越接近限位越深)
    Collision            // 按是否碰撞着色
};

// =============================================================================
//  工作空间采样的关节空间遍历策略枚举
// =============================================================================
//
// WorkspaceSamplingMode 决定如何在关节限位的超立方体中产生样本点。
//   - RandomUniform:均匀随机采样,适合统计性覆盖;
//   - Grid         :每关节按固定步数切网格,适合确定性边界探测。
// 注:URDF 传统工作空间分析默认 RandomUniform;Grid 模式在 DOF 多时组合数爆炸,
//      所以代码会基于 dof 截断到合理上限(见 WorkspaceSamplingDiagnostics)。
enum class WorkspaceSamplingMode
{
    RandomUniform,   // 在关节限位立方体内均匀随机采样
    Grid             // 每关节按固定步数切网格,组合数 = steps^dof
};

// =============================================================================
//  工作空间采样的参数配置
// =============================================================================
//
// WorkspaceSamplingConfig 是用户可调的全部采样参数。
// 字段含义:
//   - mode             :采样策略(随机/网格)
//   - sampleCount      :随机模式下即为采样点数;网格模式下为组合上限
//   - gridStepsPerJoint:网格模式下每关节的离散步数
//   - checkCollision   :是否对每个样本调用碰撞检测器(关闭时速度更快)
//   - randomSeed       :RNG 种子(0 → sanitize 为 1,确保可复现)
struct WorkspaceSamplingConfig
{
    WorkspaceSamplingMode mode         = WorkspaceSamplingMode::RandomUniform;
    int sampleCount                    = 1000;   // 总采样数(随机模式=采样点数,网格模式=截断上限)
    int gridStepsPerJoint              = 5;      // 网格模式下每个关节的步数(总组合 = steps^dof)
    bool checkCollision                = true;   // 是否调用碰撞检测器(关闭时不做碰撞检查)
    unsigned int randomSeed            = 1;      // RNG 种子;为 0 时回退到 1(避免 mt19937(0) 行为退化)
};

// =============================================================================
//  工作空间采样的诊断信息
// =============================================================================
//
// WorkspaceSamplingDiagnostics 记录 sanitize 与计划过程中的所有修正点。
//   - requestedSamples       :用户输入的 sampleCount(夹到 ≥ 0)
//   - plannedSamples         :实际要执行的样本数(Grid 模式可能被截断)
//   - theoreticalGridSamples :Grid 模式按 dof × steps 算出的理论组合总数
//   - gridCountTruncated     :Grid 总数 > plannedSamples 时为 true
//   - sampleCountClamped     :sampleCount 被修正(负数 → 0, 过大 → MaxWorkspaceSampleCount)
//   - gridStepsClamped       :gridStepsPerJoint 被修正
//   - randomSeedAdjusted     :randomSeed == 0 被替换为 1
// UI diagnostics label 和 Report 都使用此结构展示给用户。
struct WorkspaceSamplingDiagnostics
{
    std::size_t requestedSamples = 0;
    std::size_t plannedSamples = 0;
    std::size_t theoreticalGridSamples = 0;
    bool gridCountTruncated = false;
    bool sampleCountClamped = false;
    bool gridStepsClamped = false;
    bool randomSeedAdjusted = false;
};

// =============================================================================
//  工作空间样本集的统计摘要
// =============================================================================
//
// WorkspaceSummary 描述整个 workspace 样本集的状态分布 + 关键指标,
// 由 summarizeWorkspaceSamples 一次性算出,供 UI summary 与 Report 复用。
// 设计原则:
//   - 状态分布类(状态计数/碰撞计数)在所有样本上统计;
//   - 数值统计类(manip/cond/margin)在"有限值"样本上统计,
//     用 hasManipulability / hasCondition / hasJointLimitMargin 标记是否有数据。
//     避免 +inf/-inf/NaN 让 min/max/avg 失真。
struct WorkspaceSummary
{
    std::size_t totalCount = 0;        // 总样本数
    std::size_t passCount = 0;          // status == Pass 的样本数
    std::size_t warningCount = 0;       // status == Warning 的样本数
    std::size_t failCount = 0;          // status == Fail 的样本数
    std::size_t unknownCount = 0;       // status == Unknown 的样本数
    std::size_t collisionCount = 0;     // inCollision == true 的样本数
    std::size_t collisionFreeCount = 0; // inCollision == false 的样本数

    bool hasManipulability = false;    // 是否有有效 manipulability 数据
    double minManipulability = 0.0;    // 有限 manipulability 的最小值
    double maxManipulability = 0.0;    // 有限 manipulability 的最大值
    double avgManipulability = 0.0;    // 有限 manipulability 的平均值
    double p10Manipulability = 0.0;     // 10 分位数(衡量退化区域)

    bool hasCondition = false;
    double minCondition = 0.0;
    double maxCondition = 0.0;
    double avgCondition = 0.0;

    bool hasJointLimitMargin = false;
    double minJointLimitMargin = 0.0;
};

// =============================================================================
//  位姿可达性采样配置
// =============================================================================
//
// PoseReachabilityConfig 用于围绕若干空间位置采样工具方向,
// 评估"在每个位置周围,工具能朝哪些方向到达"。
//   - directionSamples :单位球面上工具 Z 轴方向采样数(使用斐波那契螺旋均匀分布,
//                      避免经纬度网格的两极聚集问题)
//   - rollSamples      :绕工具 Z 轴的滚动采样数(覆盖自转自由度)
//   - checkCollision   :是否调用碰撞检测器
struct PoseReachabilityConfig
{
    int directionSamples = 24;   // 单位球上工具 Z 轴方向的采样数(斐波那契螺旋)
    int rollSamples      = 1;    // 围绕工具 Z 轴的滚动采样数(用于考察绕自身轴的旋转)
    bool checkCollision  = true;
};

// =============================================================================
//  位姿可达性诊断信息
// =============================================================================
//
// PoseReachabilityDiagnostics 记录 sanitize 修正和计划细节。
//   - positionCount           :用户输入的位置数
//   - requestedDirectionSamples :原始 directionSamples(夹到 ≥ 0)
//   - requestedRollSamples      :原始 rollSamples
//   - plannedDirectionsPerPosition:每个 position 实际的方向数 = dir × roll
//   - plannedIkTargets        :所有 position 的总 IK 数
//   - directionSamplesClamped  :directionSamples 被修正
//   - rollSamplesClamped       :rollSamples 被修正
//   - targetCountCapped       :总目标数超过 MaxPoseReachabilityTargets 时为 true
struct PoseReachabilityDiagnostics
{
    std::size_t positionCount = 0;
    std::size_t requestedDirectionSamples = 0;
    std::size_t requestedRollSamples = 0;
    std::size_t plannedDirectionsPerPosition = 0;
    std::size_t plannedIkTargets = 0;
    bool directionSamplesClamped = false;
    bool rollSamplesClamped = false;
    bool targetCountCapped = false;
};

// =============================================================================
//  位姿可达性样本集的统计摘要
// =============================================================================
//
// PoseReachabilitySummary 描述一组 Position 的方向覆盖率统计。
// 与 WorkspaceSummary 不同:这里 focus 在"每个位置覆盖了多少方向"而非
// 单个样本的状态。minCoverage/maxCoverage 帮助用户发现覆盖不均的位置。
struct PoseReachabilitySummary
{
    std::size_t totalPositions = 0;     // 位置总数
    std::size_t passCount = 0;
    std::size_t warningCount = 0;
    std::size_t failCount = 0;
    std::size_t unknownCount = 0;
    std::size_t sampledDirections = 0;  // 所有位置的方向×滚动求和
    std::size_t reachableDirections = 0; // 所有位置可达方向求和
    std::size_t sampledOrientationSamples = 0;
    std::size_t reachableOrientationSamples = 0;
    double averageDirectionCoverage = 0.0;
    double minDirectionCoverage = 0.0;
    double maxDirectionCoverage = 0.0;
    double averageOrientationCoverage = 0.0;
    double minOrientationCoverage = 0.0;
    double maxOrientationCoverage = 0.0;
    double averageCoverage = 0.0;       // 所有位置 coverage 的平均值
    double minCoverage = 0.0;
    double maxCoverage = 0.0;
    std::size_t partialCount = 0;       // 因取消而未完成的位置数
    std::size_t plannedIkTargets = 0;   // 本次运行计划的总 IK 数
    std::size_t completedIkTargets = 0; // 实际完成的总 IK 数(可能 < planned 因取消)
};

// =============================================================================
//  用户可调的阈值集合
// =============================================================================
//
// KinematicThresholds 是所有"近限位/奇异"的判定阈值,
// 可由用户在 Report tab 修改后实时影响后续分析。
// 阈值选择原则:
//   - 越紧的阈值 → 越容易触发警告/失败 → 更保守的评价
//   - 默认值参考主流工业机器人常见限位 / 主流运动学教科书
struct KinematicThresholds
{
    double nearJointLimitRatio      = 0.05;   // 关节裕度低于此比例视为接近限位
    double singularValueWarning     = 1e-4;   // 最小奇异值小于该阈值即视为接近奇异
    double conditionWarning         = 100.0;  // 条件数 ≥ 此值视为需要警告
    double conditionFail            = 1000.0; // 条件数 ≥ 此值视为奇异失败
    double manipulabilityWarning    = 1e-5;   // 可操作度低于此值视为退化
    double positionToleranceMeters  = 0.001;  // 期望位姿位置容差(米)
    double orientationToleranceDeg  = 1.0;    // 期望位姿姿态容差(度)
    double ikDuplicateQThreshold    = 1e-4;   // IK 候选 Q 的无穷范数去重阈值(rad/m)
};

// =============================================================================
//  单个配置(关节值 q)的完整运动学评估
// =============================================================================
//
// ConfigurationEvaluation 是一次"在给定 q 下的评估快照":
//   - stage/feasibility/quality :评估等级与结论(见上方三个枚举);
//   - provenance                :需求执行溯源(记录由哪条冻结需求派生而来);
//   - q / tcpPose               :被评估的关节值与对应的 FK 位姿;
//   - jointLimitMargins         :各关节归一化裕度,minimumJointMargin 取其中最小;
//   - jacobianRowMajor          :baseJframe 的 6 x n 雅可比(行优先扁平化,便于序列化);
//   - singularValues            :雅可比 SVD 的奇异值(降序);
//   - conditionNumber           :sigma_max / sigma_min,奇异时为 +inf;
//   - manipulability            :奇异值之积(可操作度),= 0 表示奇异;
//   - collisionChecked/inCollision:是否做了碰撞检查及结果;
//   - failureReasons/warnings   :失败原因与告警列表。
// 该结构由 ConfigurationEvaluator 产出,并被 analyzeCurrentPose / 工作空间采样
// 等场景复用;它不绑定任何 UI 类型,可直接序列化到 Report JSON。
struct ConfigurationEvaluation
{
    AnalysisEvidenceStage stage = AnalysisEvidenceStage::Quick;
    Feasibility feasibility = Feasibility::NotEvaluated;
    Quality quality = Quality::Unknown;
    RequirementExecutionProvenance provenance;
    rw::math::Q q;
    rw::math::Transform3D<> tcpPose;
    std::vector< double > jointLimitMargins;
    double minimumJointMargin = 0.0;
    std::vector< double > jacobianRowMajor;
    int jacobianRows = 0;
    int jacobianCols = 0;
    std::vector< double > singularValues;
    double conditionNumber = 0.0;
    double manipulability = 0.0;
    bool collisionChecked = false;
    bool inCollision = false;
    std::vector< KinematicFailureReason > failureReasons;
    std::vector< AnalysisWarning > warnings;
};

// =============================================================================
//  单个 IK 候选解的评分记录
// =============================================================================
//
// TargetCandidate 把"一个候选解"与其评价指标打包,供 UI 排序与评分展示:
//   - configuration        :该解在 q 处的完整运动学评估;
//   - positionErrorMeters  :FK 结果与目标的位置误差(米);
//   - orientationErrorDeg  :FK 结果与目标的姿态误差(度);
//   - distanceToReferenceQ :与参考关节值(通常为当前 q)的 L2 距离,
//                            用于表达"尽量少动"的路径偏好;
//   - score                :综合评分(越小越优),由 TargetEvaluator 计算,
//                            排序规则见 sortIkSolutionsForDisplay。
struct TargetCandidate
{
    ConfigurationEvaluation configuration;
    double positionErrorMeters = 0.0;
    double orientationErrorDeg = 0.0;
    double distanceToReferenceQ = 0.0;
    double score = 0.0;
};

// =============================================================================
//  单个目标位姿的完整执行评估
// =============================================================================
//
// TargetEvaluation 是一次"任务点(target)级别的评估结果",是目标评估的核心输出:
//   - stage/feasibility/quality :整体等级与结论;
//   - level                    :需求等级(Must/Should/Info 等);
//   - provenance/itemProvenance:需求执行溯源 + 条目溯源(记录来自哪条需求条目);
//   - target                   :被评估的任务点(输入);
//   - candidates               :所有候选解(评分后,由高到低);
//   - failureReasons/warnings  :聚合失败原因与告警。
// 该结构由 TargetEvaluator 产出;Diagnose 和批量接口映射为
// KinematicIkAnalysisResult,执行契约流程则直接消费它。
struct TargetEvaluation
{
    AnalysisEvidenceStage stage = AnalysisEvidenceStage::Quick;
    Feasibility feasibility = Feasibility::NotEvaluated;
    Quality quality = Quality::Unknown;
    RequirementExecutionLevel level = RequirementExecutionLevel::Must;
    RequirementExecutionProvenance provenance;
    RequirementItemProvenance itemProvenance;
    TaskPoint target;
    std::vector< TargetCandidate > candidates;
    std::vector< KinematicFailureReason > failureReasons;
    std::vector< AnalysisWarning > warnings;
};

// =============================================================================
//  工作区覆盖盒中单个格点的评估结果
// =============================================================================
//
// RegionCellResult 描述覆盖盒按笛卡尔网格剖分后,单个格点的采样结论:
//   - index      :格点在 (nx, ny, nz) 网格中的索引;
//   - position   :格点的世界坐标(米);
//   - feasibility / quality:该格点的结论(位置是否可达、质量如何);
//   - reachableOrientationCount / sampledOrientationCount:该格点可达 / 采样朝向数,
//                    用于计算该格点的朝向覆盖率;
//   - bestManipulability / bestJointMargin:该格点所有可达样本中的最佳指标,
//                    供颜色映射 / 排序使用;
//   - failureReasons:该格点全部失败原因(去重后的集合)。
// 每个覆盖盒的 cells 集合由 RegionCoverageResult 持有,供可视化与诊断逐格点展示。
struct RegionCellResult
{
    std::array< int, 3 > index = {{0, 0, 0}};
    std::array< double, 3 > position = {{0.0, 0.0, 0.0}};
    Feasibility feasibility = Feasibility::NotEvaluated;
    Quality quality = Quality::Unknown;
    int reachableOrientationCount = 0;
    int sampledOrientationCount = 0;
    double bestManipulability = 0.0;
    double bestJointMargin = 0.0;
    std::vector< KinematicFailureReason > failureReasons;
};

// =============================================================================
//  工作区覆盖盒的整体覆盖评估结果
// =============================================================================
//
// RegionCoverageResult 聚合一个覆盖盒(RequirementExecutionRegion)的评估结论:
//   - stage / feasibility / quality:整体等级;
//   - provenance / itemProvenance :执行溯源 + 条目溯源;
//   - regionId                   :对应覆盖盒的 id;
//   - totalCells / reachableCells:总格点数 / 可达格点数;
//   - sampledOrientations / reachableOrientations:总采样朝向 / 可达朝向;
//   - positionCoverage           :reachableCells / totalCells(位置覆盖率);
//   - orientationCoverage        :reachableOrientations / sampledOrientations(朝向覆盖率);
//   - cells                      :每个格点的明细(供可视化 / 诊断);
//   - warnings                   :聚合告警。
// 位置覆盖率与朝向覆盖率分开统计:一个格点位置可达但朝向不全,仍会拉低朝向
// 覆盖率——这正是覆盖盒 Must 级验收(Must 区域硬约束)的核心判定依据。
struct RegionCoverageResult
{
    AnalysisEvidenceStage stage = AnalysisEvidenceStage::Verified;
    Feasibility feasibility = Feasibility::NotEvaluated;
    Quality quality = Quality::Unknown;
    RequirementExecutionProvenance provenance;
    RequirementItemProvenance itemProvenance;
    std::string regionId;
    int totalCells = 0;
    int reachableCells = 0;
    int sampledOrientations = 0;
    int reachableOrientations = 0;
    double positionCoverage = 0.0;
    double orientationCoverage = 0.0;
    std::vector< RegionCellResult > cells;
    std::vector< AnalysisWarning > warnings;
};

// =============================================================================
//  Must 级需求执行验证的汇总
// =============================================================================
//
// RequirementValidationSummary 是 validateRequirements 的顶层输出,汇总所有
// Must 级任务点与覆盖盒的验证结果:
//   - stage / feasibility / quality:整体证据阶段 / 可行性 / 质量;
//   - provenance                   :需求执行溯源;
//   - mustTaskCount / mustTaskFeasibleCount :参与验证的 Must 任务点总数,及其中
//                      Feasible 的数量;
//   - mustRegionCount / mustRegionFeasibleCount:参与验证的 Must 覆盖盒总数,及其中
//                      Feasible 的数量;
//   - taskResults / regionResults :每个任务点 / 覆盖盒的明细结果;
//   - warnings                    :汇总告警(如 KIN_MUST_TASK_INFEASIBLE)。
// 聚合规则(buildRequirementValidationSummary):
//   - 无 Must 项         -> NotEvaluated;
//   - 任一 Must 项数据不足 -> DataInsufficient + Critical;
//   - 任一 Must 项不可行   -> Infeasible + Critical;
//   - 全部可行           -> Feasible,quality 取所有 Must 项中最差等级。
struct RequirementValidationSummary
{
    AnalysisEvidenceStage stage = AnalysisEvidenceStage::Verified;
    Feasibility feasibility = Feasibility::NotEvaluated;
    Quality quality = Quality::Unknown;
    RequirementExecutionProvenance provenance;
    int mustTaskCount = 0;
    int mustTaskFeasibleCount = 0;
    int mustRegionCount = 0;
    int mustRegionFeasibleCount = 0;
    std::vector< TargetEvaluation > taskResults;
    std::vector< RegionCoverageResult > regionResults;
    std::vector< AnalysisWarning > warnings;
};

// =============================================================================
//  "Current Pose" 分析结果
// =============================================================================
//
// KinematicCurrentPoseResult 是在当前 state 下,对所选 device/TCP 帧做完整评估的结果:
//   - FK 得到 TCP 在 base 坐标系下的位姿
//   - 雅可比矩阵 J = [Jv; Jw](6×n,行优先序列化)
//   - SVD 分解得到奇异值序列(降序)和条件数 σ_max/σ_min
//   - 关节裕度 = min_i dist(q[i], [lower[i], upper[i]]) / (upper[i] - lower[i])
//
// 用途:Report tab、插件外 JSON、CSV 报告的"当前位姿 + 工作条件"行。
struct KinematicCurrentPoseResult
{
    AnalysisStatus status = AnalysisStatus::Unknown;  // 综合状态(由各子状态聚合)
    std::string deviceName;                           // 设备名(若已解析)
    std::string tcpFrameName;                         // 实际使用的 TCP 帧名
    std::vector< double > q;                          // 当前关节值(行优先扁平 vector)
    std::array< double, 3 > tcpPosition = {{0.0, 0.0, 0.0}};  // TCP 在 base 坐标系下的位置 (m)
    std::array< double, 3 > tcpRpyDeg   = {{0.0, 0.0, 0.0}};  // TCP 在 base 坐标系下的 RPY (deg)
    std::vector< double > jointLimitMargins;          // 各关节的归一化裕度 [0, 0.5]
    double minJointLimitMargin = 0.0;                 // 上述裕度的最小值(整个裕度链)
    std::vector< double > jacobianRowMajor;           // 6×n 雅可比(行优先,扁平化便于序列化)
    int jacobianRows = 0;                             // 通常为 6(3 个线速度 + 3 个角速度)
    int jacobianCols = 0;                             // 设备 DOF
    std::vector< double > singularValues;             // 雅可比的奇异值(降序)
    double conditionNumber = 0.0;                     // σ_max / σ_min,奇异时为 +inf
    double manipulability  = 0.0;                     // 雅可比的奇异值之积,= 0 表示奇异
    std::vector< AnalysisWarning > warnings;           // 各种告警(超限位/奇异等)
};

// =============================================================================
//  IK 求解的单条候选解
// =============================================================================
//
// KinematicIkSolution 描述 IK 求解器返回的一个候选解及其评估指标。
// 注意 IK 求解器通常返回多个候选(对应不同分支),我们要逐条评估并展示给用户。
//   - score         :综合评分(越小越好);由 KinematicMetrics.computeSolutionScore 计算
//   - failureReasons:该解的失败原因列表;解为空时仍可能有 NoSolution 标记
struct KinematicIkSolution
{
    AnalysisStatus status = AnalysisStatus::Unknown;
    std::vector< double > q;                          // 该解对应的关节值
    double distanceToCurrentQ    = 0.0;               // 与当前 state 的 q 之差(L2 距离)
    double minJointLimitMargin   = 0.0;               // 该解的关节最小裕度
    // 以下字段为重构新增的细粒度评估数据:把原本只在 ConfigurationEvaluation 中的
    // 逐关节裕度 / 雅可比 / 奇异值也复制到每个候选解上,详情表格可逐解直接展示,
    // 无需回溯配置评估。jacobianRowMajor 与 singularValues 采用行优先扁平化存储,
    // 便于序列化到 Report JSON 与 CSV 导出。
    std::vector< double > jointLimitMargins;
    double manipulability        = 0.0;
    double conditionNumber       = 0.0;
    // 该解处的 6×n 雅可比矩阵(行优先扁平化)及其维度(行通常为 6,列为 DOF)。
    std::vector< double > jacobianRowMajor;
    int jacobianRows             = 0;
    int jacobianCols             = 0;
    // 雅可比 SVD 分解得到的奇异值(降序),用于复算条件数/可操作度。
    std::vector< double > singularValues;
    double positionErrorMeters   = 0.0;               // FK 与目标位置差(米)
    double orientationErrorDeg   = 0.0;               // FK 与目标姿态差(度)
    // 是否对该解实际执行了碰撞检测(未执行时 inCollision 字段无意义)。
    bool collisionChecked        = false;
    bool inCollision             = false;             // 碰撞检测器标记
    double score                 = 0.0;               // 综合评分(越小越好)
    std::vector< KinematicFailureReason > failureReasons;  // 该解的失败原因列表
};

// =============================================================================
//  单个目标位姿的 IK 分析结果
// =============================================================================
//
// KinematicIkAnalysisResult 是一个 TaskPoint 的 IK 分析全集:
//   - status          :多个解中"最严重程度"的状态聚合
//   - failureReason   :主要失败原因
//   - rawCandidateCount:求解器返回的原始候选数
//   - usableSolutionCount:无碰撞、status != Fail 的可用解数
//   - solutions       :所有候选解,已按 UI 排序规则排好(见 sortIkSolutionsForDisplay)
struct KinematicIkAnalysisResult
{
    AnalysisStatus status = AnalysisStatus::Unknown;   // 所有解中"最严重程度"的状态聚合
    KinematicFailureReason failureReason = KinematicFailureReason::None;
    TaskPoint target;                                 // 输入目标点
    // 用户是否请求了对每个候选解做碰撞检查:false 时碰撞证据应视为 NotEvaluated,
    // Apply 判定(canApplyIkSolution)会跳过碰撞约束,只依据状态与残差。
    bool collisionCheckRequested = false;
    std::size_t rawCandidateCount = 0;
    std::size_t usableSolutionCount = 0;
    std::vector< KinematicIkSolution > solutions;     // 已按 UI 排序规则排好
    std::vector< AnalysisWarning > warnings;
};

// =============================================================================
//  IK 解集统计摘要
// =============================================================================
//
// KinematicIkSummary 供 UI 表格上方的状态条 / Report tab 一次性读取,
// 避免每次重算都要遍历 solutions 列表。
//   totalCount   = solutions.size()(去重后总数);
//   usableCount  = 无碰撞 && status != Fail 的解数;
//   passCount    = status == Pass 的解数;
//   warningCount = status == Warning 的解数;
//   failCount    = status == Fail 的解数(可能含诊断性 Fail,见汇总说明)。
struct KinematicIkSummary
{
    std::size_t totalCount = 0;
    std::size_t usableCount = 0;
    std::size_t passCount = 0;
    std::size_t warningCount = 0;
    std::size_t failCount = 0;
};

// =============================================================================
//  任务点层面的可达性分析结果
// =============================================================================
//
// TaskPointReachabilityResult 在 IK 基础上叠加任务级判断(是否启用等)。
// 注意 IK 状态可能本身 OK,但任务点被禁用,导致 task-level status = Unknown。
// primaryFailure 字段给出"主因",使 UI 可以选择性地高亮主要原因。
struct TaskPointReachabilityResult
{
    AnalysisStatus status = AnalysisStatus::Unknown;
    TaskPoint taskPoint;
    KinematicIkAnalysisResult ik;                    // 上述 IK 结果
    KinematicFailureReason primaryFailure    = KinematicFailureReason::None;
    std::vector< KinematicFailureReason > failureReasons;
};

// =============================================================================
//  工作空间采样的单条样本
// =============================================================================
//
// WorkspaceSample 是一个 Q → TCP 位姿的评估记录:
//   - q         :采样得到的关节值
//   - tcpPosition:FK(q) 得到的 TCP 在 base 坐标系下的位置
//   - 各种 metric:manipulability / cond / margin / inCollision
//   - status    :聚合状态(碰撞 → Fail, manip 太低 → Warning, 等等)
struct WorkspaceSample
{
    AnalysisEvidenceStage stage = AnalysisEvidenceStage::Estimated;
    Feasibility feasibility = Feasibility::NotEvaluated;
    Quality quality = Quality::Unknown;
    unsigned int sampleSeed = 0;
    int sampleCount = 0;
    std::vector< double > q;                          // 采样得到的关节值
    std::array< double, 3 > tcpPosition = {{0.0, 0.0, 0.0}};  // 由此关节值 FK 得到的 TCP 位置
    double manipulability     = 0.0;
    double minJointLimitMargin = 0.0;
    double conditionNumber    = 0.0;
    bool inCollision          = false;
    bool collisionChecked     = false;
    AnalysisStatus status     = AnalysisStatus::Unknown;
};

// =============================================================================
//  位姿可达性单点结果
// =============================================================================
//
// PoseReachabilitySample 是"一个空间位置"的可达性结果。
// 注意 sampledDirections 不等于 plannedIkTargets —— 前者是 sanitized 后实际
// 跑的方向/滚动组合数,后者仅用于 progress 显示(包含 capped 状态)。
//
// 代表性 Q:点击可视化点时复现该位置的关键配置,只在可达时保存一次。
struct PoseReachabilitySample
{
    std::array< double, 3 > position = {{0.0, 0.0, 0.0}};
    int sampledDirections = 0;          // 工具 Z 方向样本数
    int reachableDirections = 0;        // 至少一个 roll 姿态可达的工具 Z 方向数
    int sampledOrientationSamples = 0;  // directionSamples × rollSamples
    int reachableOrientationSamples = 0;// 完整姿态可达样本数
    double directionCoverage = 0.0;     // reachableDirections / sampledDirections
    double orientationCoverage = 0.0;   // reachableOrientationSamples / sampledOrientationSamples
    double coverage = 0.0;              // 兼容字段，始终映射为 orientationCoverage
    AnalysisStatus status = AnalysisStatus::Unknown;
    std::size_t plannedIkTargets = 0;     // 该 position 的 IK 计划数
    std::size_t completedIkTargets = 0;   // 该 position 已完成的 IK 数
    bool partial = false;                 // 取消/中断导致未完成全部 IK
    bool hasRepresentativeQ = false;      // 是否保存了 representativeQ
    std::vector< double > representativeQ; // 代表性 IK 解的 Q(可达时保存)
    int representativeDirectionIndex = -1; // representativeQ 对应的方向索引
    int representativeRollIndex = -1;     // representativeQ 对应的滚动索引
};

// =============================================================================
//  聚合结果(一次性聚合四种分析)
// =============================================================================
//
// KinematicAnalysisResult 把"当前位姿 / 任务点 / 工作空间 / 位姿可达性"四类
// 子结果整合到同一个结构,加上 reachableRate(任务点可达比例)及若干汇总告警。
// 这就是 Report tab / JSON 导出 / CSV 导出的"最顶层数据"。
struct KinematicAnalysisResult
{
    AnalysisResultHeader header;
    AnalysisStatus status = AnalysisStatus::Unknown;     // 整体聚合状态
    Feasibility feasibility = Feasibility::NotEvaluated;
    Quality quality = Quality::Unknown;
    AnalysisEvidenceStage evidenceStage = AnalysisEvidenceStage::Estimated;
    KinematicCurrentPoseResult currentPose;              // 当前位姿
    std::vector< TaskPointReachabilityResult > taskPointResults;  // 任务点结果
    double reachableRate = 0.0;                          // 任务点的可达率
    std::vector< PoseReachabilitySample > poseReachability;        // 位姿可达性
    std::vector< WorkspaceSample > workspaceSamples;              // 工作空间
    std::vector< AnalysisWarning > singularityWarnings;            // 奇异警告汇总
    std::vector< AnalysisWarning > jointLimitWarnings;             // 关节限位警告汇总
    std::vector< MetricValue > manipulabilityMap;                  // min/max/mean/p10 等可操作度指标
    std::vector< AnalysisWarning > warnings;                       // 综合告警
};

// =============================================================================
//  枚举 / 单位字符串与单位换算 helper(在 .cpp 中实现)
// =============================================================================

// 将枚举转换为可读字符串(用于日志/UI/CSV)。
const char* toString(KinematicFailureReason reason);
const char* toString(AnalysisEvidenceStage stage);
const char* toString(Feasibility feasibility);
const char* toString(Quality quality);
const char* toString(KinematicLengthUnit unit);
const char* toString(KinematicAngleUnit unit);
bool analysisEvidenceStageFromString(const std::string& text,
                                     AnalysisEvidenceStage& value,
                                     std::string* error = nullptr);
bool feasibilityFromString(const std::string& text,
                           Feasibility& value,
                           std::string* error = nullptr);
bool qualityFromString(const std::string& text,
                       Quality& value,
                       std::string* error = nullptr);
bool kinematicFailureReasonFromString(const std::string& text,
                                      KinematicFailureReason& value,
                                      std::string* error = nullptr);
// 返回显示单位后缀(如 "m" / "cm" / "mm" / "in" / "deg" / "rad")。
const char* unitSuffix(KinematicLengthUnit unit);
const char* unitSuffix(KinematicAngleUnit unit);

// 米 ↔ 显示单位的换算(供 UI SpinBox 使用)。
double displayLengthFromMeters(double meters, KinematicLengthUnit unit);
double metersFromDisplayLength(double displayValue, KinematicLengthUnit unit);
// 度 ↔ 显示单位的换算。
double displayAngleFromDegrees(double degrees, KinematicAngleUnit unit);
double degreesFromDisplayAngle(double displayValue, KinematicAngleUnit unit);

}    // namespace rws

#endif    // RWS_KINEMATICANALYSIS_KINEMATICANALYSISTYPES_HPP
