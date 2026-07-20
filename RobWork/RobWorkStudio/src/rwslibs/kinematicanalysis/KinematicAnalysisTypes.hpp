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

#include <array>
#include <string>
#include <vector>

namespace rws {

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
    SolverError      // 求解器抛出异常(底层 IK 库错误)
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
    double manipulability        = 0.0;
    double conditionNumber       = 0.0;
    double positionErrorMeters   = 0.0;               // FK 与目标位置差(米)
    double orientationErrorDeg   = 0.0;               // FK 与目标姿态差(度)
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
    std::vector< double > q;                          // 采样得到的关节值
    std::array< double, 3 > tcpPosition = {{0.0, 0.0, 0.0}};  // 由此关节值 FK 得到的 TCP 位置
    double manipulability     = 0.0;
    double minJointLimitMargin = 0.0;
    double conditionNumber    = 0.0;
    bool inCollision          = false;
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
    int sampledDirections = 0;     // 总尝试的方向数 = directionSamples × rollSamples
    int reachableDirections = 0;   // 至少有一个非碰撞 Pass/Warning 解的方向数
    double coverage = 0.0;          // reachableDirections / sampledDirections
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
const char* toString(KinematicLengthUnit unit);
const char* toString(KinematicAngleUnit unit);
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
