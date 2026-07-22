#ifndef RWS_KINEMATICANALYSIS_KINEMATICANALYSIS_HPP
#define RWS_KINEMATICANALYSIS_KINEMATICANALYSIS_HPP

#include "KinematicAnalysisTypes.hpp"
#include "KinematicMetrics.hpp"

// RobWork 核心类型:Ptr 是 RobWork 的侵入式智能指针;Device 表示一个运动学链;
// State 是不可变的工作单元配置;Q 是关节值向量;Frame 是坐标系;CollisionDetector
// 用于碰撞检测(空指针代表跳过碰撞检查)。
#include <rw/core/Ptr.hpp>
#include <rw/kinematics/Frame.hpp>
#include <rw/kinematics/State.hpp>
#include <rw/math/Q.hpp>
#include <rw/models/Device.hpp>
#include <rw/models/WorkCell.hpp>
#include <rw/proximity/CollisionDetector.hpp>

namespace rws {

// =============================================================================
//  位姿可达性后台分析的可选回调
// =============================================================================
//
// 用于协作取消和进度通知。
//   - isCancellationRequested:每次 IK 循环检查一次,返回 true 时立即停止分析;
//   - onProgress              :每组方向采样完成后调用,用于刷新进度条/标签;
//   - userData                :用户自定义上下文指针(例如 QPointer<Widget>),
//                              通过 userData 把异步事件桥接到 UI 线程。
// 此设计允许 UI 不轮询状态即可获得增量反馈,且能即时响应取消。
struct PoseReachabilityRunCallbacks
{
    bool (*isCancellationRequested) (void* userData) = NULL;
    void (*onProgress) (std::size_t completedTargets,
                        std::size_t plannedTargets,
                        void* userData) = NULL;
    void* userData = NULL;
};

// =============================================================================
//  工作空间后台采样的可选回调
// =============================================================================
//
// 与位姿可达性回调语义相同,但参数语义不同:
//   - completedSamples :已完成的样本数(随机模式按样本计,网格模式按方向×滚动计)
//   - plannedSamples   :计划总数;initial 时等于 planned,之后不变
struct WorkspaceSamplingRunCallbacks
{
    bool (*isCancellationRequested) (void* userData) = NULL;
    void (*onProgress) (std::size_t completedSamples,
                        std::size_t plannedSamples,
                        void* userData) = NULL;
    void* userData = NULL;
};

// =============================================================================
//  KinematicAnalyzer:运动学分析核心无 UI 组件
// =============================================================================
//
// KinematicAnalyzer 是运动学分析的核心无 UI 组件。
// 它不持有任何 Qt 类型,只接受 RobWork 的 Device / Frame / State / CollisionDetector
// 输入,产出 POD 结果结构。这样:
//   - 可被多个 Widget / 插件复用;
//   - 可被测试程序直接调用(KinematicAnalysisTest.cpp);
//   - 与未来 C++ / Python API 扩展解耦;
//   - 线程安全:所有方法 const,只要传入的 state 不被外部修改就安全。
// 阈值通过 setThresholds 注入,允许运行时调整评价标准。
class KinematicAnalyzer
{
  public:
    KinematicAnalyzer ();

    // 设置/获取判定阈值(关节裕度、奇异、可操作度、位姿容差)。
    // 线程安全要求:在分析过程中不要改变阈值。
    void setThresholds (const KinematicThresholds& thresholds);
    const KinematicThresholds& thresholds () const;

    // =======================================================================
    //  analyzeCurrentPose:在指定 state 下评估 device/TCP 帧的正运动学位形
    // =======================================================================
    //
    // 流程:
    //   1) 若 device == NULL → 立即返回 Fail (NoDevice);
    //   2) 若 tcpFrame == NULL → 回退到 device->getEnd(),仍为空则返回 Fail;
    //   3) 在传入的 state 上做 FK 得到 TCP 在 base 坐标系下的位姿 (position/rpy);
    //   4) 计算雅可比 J(6×n),并做 SVD 分解得到奇异值序列和条件数;
    //   5) 计算各关节的归一化裕度;
    //   6) 按阈值生成对应 warnings(接近限位 / 奇异 / 条件数超限 / 可操作度过低)。
    // 不修改入参 state(计算过程中可能临时 setQ 然后复原)。
    KinematicCurrentPoseResult analyzeCurrentPose (
        rw::core::Ptr< rw::models::Device > device,
        rw::core::Ptr< const rw::kinematics::Frame > tcpFrame,
        const rw::kinematics::State& state) const;

    // =======================================================================
    //  analyzeIk:对一个目标 TaskPoint 求解 IK,返回所有候选解的逐一指标
    // =======================================================================
    //
    // 流程:
    //   1) 用 TaskPoint 的 refFrame/tcpFrame 解析到 base 坐标系下的目标位姿;
    //   2) 调 RobWork JacobianIKSolver 求解,可能得到多个候选 Q;
    //   3) 对每个候选 Q 做 FK 验算残差(位姿/角度);
    //   4) 计算 manipulability/conditionNumber/jointLimitMargins;
    //   5) 若 collisionDetector != NULL,对每个 Q 做碰撞检测;
    //   6) 用 KinematicMetrics.computeSolutionScore 综合评分;
    //   7) 调用 sortIkSolutionsForDisplay 按 UI 展示偏好排序。
    // collisionDetector == NULL 时跳过碰撞检查(快速模式);
    // 不修改入参 state(候选解评估会临时 setQ,完毕后恢复)。
    KinematicIkAnalysisResult analyzeIk (
        rw::core::Ptr< rw::models::Device > device,
        rw::core::Ptr< const rw::kinematics::Frame > tcpFrame,
        const rw::kinematics::State& state,
        const TaskPoint& target,
        rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector = NULL) const;

    // =======================================================================
    //  analyzeTaskPoints(无 workcell):批量处理一组任务点
    // =======================================================================
    //
    // 流程:对每个 taskPoint 调 analyzeIk,并汇总成任务层结果。
    // 注意:此版本不解析 taskPoint 自带的 refFrame/tcpFrame,而是使用传入的
    // 单一 tcpFrame(由 UI 选定);适合 UI 已有全局 TCP 帧选择的场景。
    std::vector< TaskPointReachabilityResult > analyzeTaskPoints (
        rw::core::Ptr< rw::models::Device > device,
        rw::core::Ptr< const rw::kinematics::Frame > tcpFrame,
        const rw::kinematics::State& state,
        const std::vector< TaskPoint >& taskPoints,
        rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector = NULL) const;

    // =======================================================================
    //  analyzeTaskPoint(workcell-aware 单点版本,P1)
    // =======================================================================
    //
    // 与 analyzeIk 不同的是:先调 TaskPointResolver 把 taskPoint 自带的
    // refFrame / tcpFrame 解析到 base 坐标系;失败则 status = Fail + reason;
    // 禁用的 taskPoint 直接保留旧逻辑(不跑 resolver,不计入 reachable)。
    TaskPointReachabilityResult analyzeTaskPoint (
        rw::models::WorkCell* workcell,
        rw::core::Ptr< rw::models::Device > device,
        rw::core::Ptr< const rw::kinematics::Frame > defaultTcpFrame,
        const rw::kinematics::State& state,
        const TaskPoint& taskPoint,
        rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector = NULL) const;

    // analyzeTaskPoints(workcell-aware 批量版本,P1):逐点调上面那个 workcell-aware。
    std::vector< TaskPointReachabilityResult > analyzeTaskPoints (
        rw::models::WorkCell* workcell,
        rw::core::Ptr< rw::models::Device > device,
        rw::core::Ptr< const rw::kinematics::Frame > defaultTcpFrame,
        const rw::kinematics::State& state,
        const std::vector< TaskPoint >& taskPoints,
        rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector = NULL) const;

    // =======================================================================
    //  calculateReachableRate:计算任务点的可达率
    // =======================================================================
    //
    // 规则:
    //   - 跳过 disabled 的点;
    //   - Pass 与 Warning 都算 reachable;
    //   - 空任务集或全部 disabled 返回 0.0(避免除零)。
    // 公式:reachableRate = reachableCount / nonDisabledCount。
    double calculateReachableRate (
        const std::vector< TaskPointReachabilityResult >& results) const;

    // =======================================================================
    //  sampleWorkspace:在关节限位立方体内采样
    // =======================================================================
    //
    // 每个样本做 FK + 关节裕度 + 雅可比指标 + 碰撞检查(可选),得到 WorkspaceSample 列表。
    // 模式说明:
    //   - RandomUniform:均匀随机采样,用固定 randomSeed 可复现;
    //   - Grid         :每关节按 gridStepsPerJoint 等距,总组合 = steps^dof;
    //                    组合过大时用 plannedWorkspaceSampleCount 截断到 sampleCount。
    // 入口校验:device NULL、tcpFrame NULL、dof==0、限位反向等 → 返回空。
    // 无回调版本直接调用有回调版本(默认空回调)。
    std::vector< WorkspaceSample > sampleWorkspace (
        rw::core::Ptr< rw::models::Device > device,
        rw::core::Ptr< const rw::kinematics::Frame > tcpFrame,
        const rw::kinematics::State& state,
        const WorkspaceSamplingConfig& config,
        rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector = NULL) const;

    // sampleWorkspace(带回调版本):支持协作取消 + 进度通知。
    //   取消:每个样本采样前检查 isCancellationRequested;
    //   进度:每个样本采样后回调 onProgress(completed, planned)。
    // 注意:cancel 不会清空已采样结果,部分结果仍然有效,UI 可继续展示。
    std::vector< WorkspaceSample > sampleWorkspace (
        rw::core::Ptr< rw::models::Device > device,
        rw::core::Ptr< const rw::kinematics::Frame > tcpFrame,
        const rw::kinematics::State& state,
        const WorkspaceSamplingConfig& config,
        rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector,
        const WorkspaceSamplingRunCallbacks& callbacks) const;

    // =======================================================================
    //  analyzePoseReachability:位姿可达性分析
    // =======================================================================
    //
    // 对给定若干空间位置,用 Fibonacci 螺旋在单位球上采样工具 Z 轴方向,
    // 再绕 Z 轴采样 roll,逐个跑 IK,统计可达方向占比(coverage)。
    // 选用 Fibonacci 而非经纬度网格,避免"两极聚集",保证方向被同等采样。
    //
    // 状态聚合规则:
    //   - reachableDirections == 0 → Fail;
    //   - reachableDirections == sampledDirections → Pass;
    //   - 其余 → Warning。
    // 无回调版本直接调用有回调版本(默认空回调)。
    std::vector< PoseReachabilitySample > analyzePoseReachability (
        rw::core::Ptr< rw::models::Device > device,
        rw::core::Ptr< const rw::kinematics::Frame > tcpFrame,
        const rw::kinematics::State& state,
        const std::vector< std::array< double, 3 > >& positions,
        const PoseReachabilityConfig& config,
        rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector = NULL) const;

    // analyzePoseReachability(带回调版本,P5):支持协作取消 + 进度通知。
    // 取消检查在 IK 循环的多个位置(position 边界、IK 之前、IK 之后),
    // 提供"协作取消"(检查点之间立即退出)而非"硬中断"(不安全)。
    // 取消时当前 sample 标记为 partial,representativeQ 保留(若已找到)。
    std::vector< PoseReachabilitySample > analyzePoseReachability (
        rw::core::Ptr< rw::models::Device > device,
        rw::core::Ptr< const rw::kinematics::Frame > tcpFrame,
        const rw::kinematics::State& state,
        const std::vector< std::array< double, 3 > >& positions,
        const PoseReachabilityConfig& config,
        rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector,
        const PoseReachabilityRunCallbacks& callbacks) const;

    // =======================================================================
    //  buildAggregateResult:把四类子结果组装成单个 KinematicAnalysisResult
    // =======================================================================
    //
    // 组装步骤:
    //   1) 填好 header(pluginName/version/timestamp);
    //   2) 取 worstStatus(各子结果状态中的最严重等级)汇总到 result.status;
    //   3) 计算 reachableRate(任务点 Pass+Warning 占 non-disabled 的比例);
    //   4) 从所有样本中聚合 manipulabilityMap(min/max/mean/p10);
    //   5) 按 warning.code 把奇异 / 关节告警分桶汇总到 singularityWarnings /
    //      jointLimitWarnings。
    // 用途:Report tab / JSON 导出 / CSV 导出共用同一份聚合数据。
    KinematicAnalysisResult buildAggregateResult (
        const KinematicCurrentPoseResult& currentPose,
        const std::vector< TaskPointReachabilityResult >& taskPointResults,
        const std::vector< WorkspaceSample >& workspaceSamples,
        const std::vector< PoseReachabilitySample >& poseReachability) const;

  private:
    KinematicThresholds _thresholds;   // 当前生效的判定阈值
};

// =============================================================================
//  IK 解排序 / 去重 / 统计的对外 helper
// =============================================================================

// 按 UI 展示偏好对 IK 解排序:
//   1) 无碰撞优先;
//   2) 残差(位置+角度)越小越优;
//   3) 关节裕度越大越优(更安全);
//   4) 可操作度越大越优;
//   5) 与当前 q 的距离(用户偏好"最接近当前姿态"用);
//   6) 字典序 q 排(确保稳定的最终顺序)。
void sortIkSolutionsForDisplay (std::vector< KinematicIkSolution >& solutions);

// 把 candidate Q 加入 candidates,若其与已有 Q 的无穷范数距离 ≤ proximityLimit 则
// 视为重复并跳过。用于 IK 求解器返回的原始分支去重。
void addUniqueIkCandidate (std::vector< rw::math::Q >& candidates,
                           const rw::math::Q& candidate,
                           double proximityLimit);

void addUniqueIkCandidate (std::vector< rw::math::Q >& candidates,
                           const rw::math::Q& candidate,
                           double proximityLimit,
                           const std::vector< bool >& revoluteJoints);

// 统计 solutions 中"无碰撞 && status != Fail"的解数。
std::size_t countUsableIkSolutions (const std::vector< KinematicIkSolution >& solutions);

// 遍历 solutions,统计 total / usable / pass / warning / fail 五类计数,
// 供 IK tab 顶部 summary 标签使用(避免每次刷新重算)。
KinematicIkSummary summarizeIkSolutions (const std::vector< KinematicIkSolution >& solutions);

}    // namespace rws

#endif    // RWS_KINEMATICANALYSIS_KINEMATICANALYSIS_HPP
