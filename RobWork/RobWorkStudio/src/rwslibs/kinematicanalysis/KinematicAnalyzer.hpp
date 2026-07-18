#ifndef RWS_KINEMATICANALYSIS_KINEMATICANALYSIS_HPP
#define RWS_KINEMATICANALYSIS_KINEMATICANALYSIS_HPP

#include "KinematicAnalysisTypes.hpp"
#include "KinematicMetrics.hpp"

#include <rw/core/Ptr.hpp>
#include <rw/kinematics/Frame.hpp>
#include <rw/kinematics/State.hpp>
#include <rw/math/Q.hpp>
#include <rw/models/Device.hpp>
#include <rw/models/WorkCell.hpp>
#include <rw/proximity/CollisionDetector.hpp>

namespace rws {

// 位姿可达性后台分析的可选回调,用于协作取消和进度通知。
// isCancellationRequested 每轮 IK 循环检查一次;onProgress 在每组方向采样后调用。
struct PoseReachabilityRunCallbacks
{
    bool (*isCancellationRequested) (void* userData) = NULL;
    void (*onProgress) (std::size_t completedTargets,
                        std::size_t plannedTargets,
                        void* userData) = NULL;
    void* userData = NULL;
};

struct WorkspaceSamplingRunCallbacks
{
    bool (*isCancellationRequested) (void* userData) = NULL;
    void (*onProgress) (std::size_t completedSamples,
                        std::size_t plannedSamples,
                        void* userData) = NULL;
    void* userData = NULL;
};

// KinematicAnalyzer 是运动学分析的核心无 UI 组件。
// 它不持有任何 Qt 类型,只接受 RobWork 的 Device / Frame / State / CollisionDetector
// 输入,产出 POD 结果结构。这样:
//   - 可被多个 Widget / 插件复用;
//   - 可被测试程序直接调用(KinematicAnalysisTest.cpp);
//   - 与未来 C++ / Python API 扩展解耦。
class KinematicAnalyzer
{
  public:
    KinematicAnalyzer ();

    // 设置/获取判定阈值(关节裕度、奇异、可操作度、位姿容差)。
    void setThresholds (const KinematicThresholds& thresholds);
    const KinematicThresholds& thresholds () const;

    // 在当前 state 下评估指定 device/TCP 帧的正运动学位形:
    //   - 若 tcpFrame 为 NULL 则回退到 device->getEnd();若仍为空则返回 Fail。
    //   - 计算 TCP 位置 / RPY、雅可比、奇异值/条件数/可操作度、关节裕度。
    KinematicCurrentPoseResult analyzeCurrentPose (
        rw::core::Ptr< rw::models::Device > device,
        rw::core::Ptr< const rw::kinematics::Frame > tcpFrame,
        const rw::kinematics::State& state) const;

    // 对一个目标 TaskPoint 求解 IK,返回所有候选解的逐一指标和评分。
    // collisionDetector 为空时跳过碰撞检查;不修改入参 state。
    KinematicIkAnalysisResult analyzeIk (
        rw::core::Ptr< rw::models::Device > device,
        rw::core::Ptr< const rw::kinematics::Frame > tcpFrame,
        const rw::kinematics::State& state,
        const TaskPoint& target,
        rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector = NULL) const;

    // 批量处理一组任务点:为每个 point 调 analyzeIk,并汇总成任务层结果。
    std::vector< TaskPointReachabilityResult > analyzeTaskPoints (
        rw::core::Ptr< rw::models::Device > device,
        rw::core::Ptr< const rw::kinematics::Frame > tcpFrame,
        const rw::kinematics::State& state,
        const std::vector< TaskPoint >& taskPoints,
        rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector = NULL) const;

    // P1:WorkCell-aware 单点分析。内部先调 TaskPointResolver 把
    // refFrame / tcpFrame 解析到 base 坐标系,失败则 status=Fail + reason;
    // disabled 点直接保留旧逻辑(不跑 resolver,不计 reachable)。
    TaskPointReachabilityResult analyzeTaskPoint (
        rw::models::WorkCell* workcell,
        rw::core::Ptr< rw::models::Device > device,
        rw::core::Ptr< const rw::kinematics::Frame > defaultTcpFrame,
        const rw::kinematics::State& state,
        const TaskPoint& taskPoint,
        rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector = NULL) const;

    // P1:WorkCell-aware 批量分析。逐点调 analyzeTaskPoint(workcell-aware)。
    std::vector< TaskPointReachabilityResult > analyzeTaskPoints (
        rw::models::WorkCell* workcell,
        rw::core::Ptr< rw::models::Device > device,
        rw::core::Ptr< const rw::kinematics::Frame > defaultTcpFrame,
        const rw::kinematics::State& state,
        const std::vector< TaskPoint >& taskPoints,
        rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector = NULL) const;

    // 计算任务点的可达率:
    //   - 跳过 disabled 的点;
    //   - Pass 与 Warning 都算 reachable;
    //   - 空任务集或全部 disabled 返回 0.0(避免除零)。
    double calculateReachableRate (
        const std::vector< TaskPointReachabilityResult >& results) const;

    // 在关节限位立方体内采样,每个样本做 FK + 关节裕度 + 雅可比指标 + 碰撞检查。
    // mode=RandomUniform 用固定种子可复现;
    // mode=Grid 按每关节等距,总组合过大时按字典序截断到 sampleCount。
    std::vector< WorkspaceSample > sampleWorkspace (
        rw::core::Ptr< rw::models::Device > device,
        rw::core::Ptr< const rw::kinematics::Frame > tcpFrame,
        const rw::kinematics::State& state,
        const WorkspaceSamplingConfig& config,
        rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector = NULL) const;

    std::vector< WorkspaceSample > sampleWorkspace (
        rw::core::Ptr< rw::models::Device > device,
        rw::core::Ptr< const rw::kinematics::Frame > tcpFrame,
        const rw::kinematics::State& state,
        const WorkspaceSamplingConfig& config,
        rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector,
        const WorkspaceSamplingRunCallbacks& callbacks) const;

    // 在给定的若干空间位置周围,用 Fibonacci 螺旋采样工具 Z 方向,
    // 再绕 Z 轴采样 roll,逐个跑 IK,统计可达方向占比(coverage)。
    // 无回调版本直接调用有回调版本(默认空回调)。
    std::vector< PoseReachabilitySample > analyzePoseReachability (
        rw::core::Ptr< rw::models::Device > device,
        rw::core::Ptr< const rw::kinematics::Frame > tcpFrame,
        const rw::kinematics::State& state,
        const std::vector< std::array< double, 3 > >& positions,
        const PoseReachabilityConfig& config,
        rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector = NULL) const;

    // P5:带回调的重载,支持协作取消和进度通知。
    std::vector< PoseReachabilitySample > analyzePoseReachability (
        rw::core::Ptr< rw::models::Device > device,
        rw::core::Ptr< const rw::kinematics::Frame > tcpFrame,
        const rw::kinematics::State& state,
        const std::vector< std::array< double, 3 > >& positions,
        const PoseReachabilityConfig& config,
        rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector,
        const PoseReachabilityRunCallbacks& callbacks) const;

    // 把上述四种分析结果组装成单个 KinematicAnalysisResult:
    //   - 填好 header;
    //   - 取 worstStatus 汇总到 result.status;
    //   - 计算 reachableRate、可操作度的 min/max/mean/p10 指标;
    //   - 按 warning.code 把奇异 / 关节告警分桶。
    KinematicAnalysisResult buildAggregateResult (
        const KinematicCurrentPoseResult& currentPose,
        const std::vector< TaskPointReachabilityResult >& taskPointResults,
        const std::vector< WorkspaceSample >& workspaceSamples,
        const std::vector< PoseReachabilitySample >& poseReachability) const;

  private:
    KinematicThresholds _thresholds;
};

// 按 UI 展示偏好对 IK 解排序(无碰撞优先,然后按位姿残差、关节裕度、可操作度、
// 与当前 q 的距离、字典序 q 排)。
void sortIkSolutionsForDisplay (std::vector< KinematicIkSolution >& solutions);
void addUniqueIkCandidate (std::vector< rw::math::Q >& candidates,
                           const rw::math::Q& candidate,
                           double proximityLimit);
std::size_t countUsableIkSolutions (const std::vector< KinematicIkSolution >& solutions);

// 遍历 solutions,统计 total / usable / pass / warning / fail 五类计数,
// 供 IK tab 顶部 summary 标签使用。
KinematicIkSummary summarizeIkSolutions (const std::vector< KinematicIkSolution >& solutions);

}    // namespace rws

#endif    // RWS_KINEMATICANALYSIS_KINEMATICANALYSIS_HPP
