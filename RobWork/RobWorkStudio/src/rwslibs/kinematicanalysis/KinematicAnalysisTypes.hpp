#ifndef RWS_KINEMATICANALYSIS_KINEMATICANALYSISTYPES_HPP
#define RWS_KINEMATICANALYSIS_KINEMATICANALYSISTYPES_HPP

// 引入机器人分析插件共用的基础类型(AnalysisStatus、AnalysisWarning、
// AnalysisResultHeader、TaskPoint、TaskPointType、MetricValue 等)。
#include <rwslibs/robotanalysiscore/RobotAnalysisTypes.hpp>

#include <array>
#include <string>
#include <vector>

namespace rws {

// 表示一次运动学分析失败的原因标签。用于在报告/UI 中以可读方式呈现失败根因,
// 而不仅仅是状态枚举。
enum class KinematicFailureReason
{
    None,            // 无失败(占位,例如无解但 IK 解非空时)
    NoDevice,        // WorkCell 中找不到可用 Device
    NoTcpFrame,      // TCP 帧未配置且设备末端帧不存在
    IkNoSolution,    // IK 求解器返回空解集
    Collision,       // 解处于碰撞状态
    JointLimit,      // 解超出关节限位
    NearJointLimit,  // 解接近关节限位
    Singular,        // 解处雅可比奇异(条件数过差)
    NearSingular,    // 解处雅可比条件数恶化但未奇异
    InvalidTarget,   // 目标位姿本身无效(超出 FK 可达范围等)
    SolverError      // 求解器抛出异常
};

// 工作空间表格在 UI 上的着色策略(目前仅影响可视化偏好,
// 实际分析逻辑不依赖此枚举)。
enum class WorkspaceColorMode
{
    Reachability,        // 按可达性着色
    Manipulability,      // 按可操作度着色
    JointLimitMargin,    // 按关节裕度着色
    Collision            // 按碰撞标志着色
};

// 工作空间采样的关节空间遍历策略。
enum class WorkspaceSamplingMode
{
    RandomUniform,   // 在关节限位立方体内均匀随机采样
    Grid             // 每关节按固定步数切网格
};

// 工作空间采样的参数配置。
struct WorkspaceSamplingConfig
{
    WorkspaceSamplingMode mode         = WorkspaceSamplingMode::RandomUniform;
    int sampleCount                    = 1000;   // 总采样数(随机模式下即为采样点数,网格模式下为截断上限)
    int gridStepsPerJoint              = 5;      // 网格模式下每个关节的步数(总组合 = steps^dof)
    bool checkCollision                = true;   // 是否调用碰撞检测器
    unsigned int randomSeed            = 1;      // RNG 种子;为 0 时回退到 1
};

// 位姿可达性(在某位置周围旋转工具方向)的采样配置。
struct PoseReachabilityConfig
{
    int directionSamples = 24;   // 单位球上工具 Z 轴方向的采样数(斐波那契螺旋)
    int rollSamples      = 1;    // 围绕工具 Z 轴的滚动采样数(用于考察绕自身轴的旋转)
    bool checkCollision  = true;
};

// 阈值集合,所有"近限位/奇异"的判断都参考这里;可由用户在 Report tab 修改。
struct KinematicThresholds
{
    double nearJointLimitRatio      = 0.05;   // 关节裕度低于此比例视为接近限位
    double singularValueWarning     = 1e-4;   // 最小奇异值小于该阈值即视为接近奇异
    double conditionWarning         = 100.0;  // 条件数 ≥ 此值视为需要警告
    double conditionFail            = 1000.0; // 条件数 ≥ 此值视为奇异失败
    double manipulabilityWarning    = 1e-5;   // 可操作度低于此值视为退化
    double positionToleranceMeters  = 0.001;  // 期望位姿位置容差
    double orientationToleranceDeg  = 1.0;    // 期望位姿姿态容差(度)
};

// "Current pose" 分析结果:在当前 state 下,对所选 device/TCP 帧做 FK +
// Jacobian + 奇异值分解 + 关节裕度等综合评估。
struct KinematicCurrentPoseResult
{
    AnalysisStatus status = AnalysisStatus::Unknown;  // 综合状态
    std::string deviceName;                           // 设备名(若已解析)
    std::string tcpFrameName;                         // 实际使用的 TCP 帧名
    std::vector< double > q;                          // 当前关节值(行优先扁平 vector)
    std::array< double, 3 > tcpPosition = {{0.0, 0.0, 0.0}};  // TCP 在 base 坐标系下的位置 (m)
    std::array< double, 3 > tcpRpyDeg   = {{0.0, 0.0, 0.0}};  // TCP 在 base 坐标系下的 RPY (deg)
    std::vector< double > jointLimitMargins;          // 各关节的归一化裕度 [0, 0.5]
    double minJointLimitMargin = 0.0;                 // 上述裕度的最小值
    std::vector< double > jacobianRowMajor;           // 6×n 雅可比(行优先,扁平化便于序列化)
    int jacobianRows = 0;                             // 通常为 6
    int jacobianCols = 0;                             // 设备 DOF
    std::vector< double > singularValues;             // 雅可比的奇异值(降序)
    double conditionNumber = 0.0;                     // σ_max / σ_min,奇异时为 +inf
    double manipulability  = 0.0;                     // 雅可比的奇异值之积
    std::vector< AnalysisWarning > warnings;           // 各种告警(超限位/奇异等)
};

// 一次 IK 求解返回的"候选解"中的一条,带分析与评分。
struct KinematicIkSolution
{
    AnalysisStatus status = AnalysisStatus::Unknown;
    std::vector< double > q;                          // 该解对应的关节值
    double distanceToCurrentQ    = 0.0;               // 与当前 state 的 q 之差(L2 距离)
    double minJointLimitMargin   = 0.0;               // 该解的关节最小裕度
    double manipulability        = 0.0;
    double conditionNumber       = 0.0;
    double positionErrorMeters   = 0.0;               // FK 与目标位置差
    double orientationErrorDeg   = 0.0;               // FK 与目标姿态差(度)
    bool inCollision             = false;             // 碰撞检测器标记
    double score                 = 0.0;               // 综合评分(越小越好)
    std::vector< KinematicFailureReason > failureReasons;  // 该解的失败原因列表
};

// 单个目标位姿(TaskPoint)的 IK 分析结果。
struct KinematicIkAnalysisResult
{
    AnalysisStatus status = AnalysisStatus::Unknown;   // 所有解中"最严重程度"的状态聚合
    TaskPoint target;                                 // 输入目标点
    std::vector< KinematicIkSolution > solutions;     // 已按 UI 排序规则排好
    std::vector< AnalysisWarning > warnings;
};

// 任务点层面的可达性分析结果(在 IK 基础上叠加"是否启用"等任务级判断)。
struct TaskPointReachabilityResult
{
    AnalysisStatus status = AnalysisStatus::Unknown;
    TaskPoint taskPoint;
    KinematicIkAnalysisResult ik;                    // 上述 IK 结果
    KinematicFailureReason primaryFailure    = KinematicFailureReason::None;
    std::vector< KinematicFailureReason > failureReasons;
};

// 工作空间采样的单条样本。
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

// 位姿可达性(围绕某个空间点旋转工具方向)单点结果。
struct PoseReachabilitySample
{
    std::array< double, 3 > position = {{0.0, 0.0, 0.0}};
    int sampledDirections = 0;     // 总尝试的方向数 = directionSamples × rollSamples
    int reachableDirections = 0;   // 至少有一个非碰撞 Pass/Warning 解的方向数
    double coverage = 0.0;          // reachableDirections / sampledDirections
    AnalysisStatus status = AnalysisStatus::Unknown;
};

// 聚合结果:当前位姿 + 任务点 + 工作空间 + 位姿可达性 + 阈值导出的汇总。
struct KinematicAnalysisResult
{
    AnalysisResultHeader header;
    AnalysisStatus status = AnalysisStatus::Unknown;
    KinematicCurrentPoseResult currentPose;
    std::vector< TaskPointReachabilityResult > taskPointResults;
    double reachableRate = 0.0;                                  // 任务点的可达率
    std::vector< PoseReachabilitySample > poseReachability;
    std::vector< WorkspaceSample > workspaceSamples;
    std::vector< AnalysisWarning > singularityWarnings;
    std::vector< AnalysisWarning > jointLimitWarnings;
    std::vector< MetricValue > manipulabilityMap;                // min/max/mean/p10 等可操作度指标
    std::vector< AnalysisWarning > warnings;                     // 综合告警
};

// 将枚举转换为可读字符串(用于日志/UI)。
const char* toString(KinematicFailureReason reason);

}    // namespace rws

#endif    // RWS_KINEMATICANALYSIS_KINEMATICANALYSISTYPES_HPP