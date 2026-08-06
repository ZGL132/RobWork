// =============================================================================
//  RegionCoverageEvaluator.hpp —— 工作空间区域覆盖率评估(声明)
// =============================================================================
//
// 本组件对一个"执行需求区域"(RequirementExecutionRegion)做体素化扫描,
// 回答两个覆盖率问题:
//   - 位置覆盖率:区域内有多少网格单元机械臂能到达;
//   - 姿态覆盖率:在可到达的单元上,按配置的朝向策略(固定 / 对齐帧 /
//     指向目标 / 对齐几何法向)与滚动采样,有多少离散姿态可达。
//
// 流程分三步,可分别独立调用:
//   1. generateGrid:把长方体区域按 samplesPerAxis 均匀切分为网格单元;
//   2. generateTargets:为单个单元生成该处要评估的一组 TaskPoint(不同朝向);
//   3. evaluate:串行跑完所有单元,聚合出 positionCoverage /
//      orientationCoverage 与整体 feasibility / quality。
//
// 覆盖阈值(minimumCoverage / minimumOrientationCoverage)是 0~1 的比例;
// 满足则区域 Feasible,完全覆盖则为 Good,部分覆盖为 Degraded。
#ifndef RWS_KINEMATICANALYSIS_REGIONCOVERAGEEVALUATOR_HPP
#define RWS_KINEMATICANALYSIS_REGIONCOVERAGEEVALUATOR_HPP

#include "KinematicAnalysisContext.hpp"

namespace rws {

// =============================================================================
//  RegionTargetGenerationResult —— 单个网格单元的目标生成结果
// =============================================================================
//
// generateTargets 的输出:在给定 cell 处、按区域朝向策略构造的一组任务点。
// feasibility 为 DataInsufficient 时表示该单元的朝向策略无法应用(如参考帧
// 解析失败、指向目标与单元重合),此时 targets 为空且 warnings 含具体原因。
struct RegionTargetGenerationResult
{
    // 证据等级:区域评估一律按 Verified 处理(逐点真实求解而非估计)。
    AnalysisEvidenceStage stage = AnalysisEvidenceStage::Verified;
    // 目标生成是否成功:NotEvaluated(正常)或 DataInsufficient(失败)。
    Feasibility feasibility = Feasibility::NotEvaluated;
    // 该单元要逐个评估的任务点列表(每个朝向一个 TaskPoint)。
    std::vector< TaskPoint > targets;
    // 生成阶段的告警(参考帧未找到、参数越界等)。
    std::vector< AnalysisWarning > warnings;
};

// =============================================================================
//  RegionCoverageEvaluator —— 区域覆盖率评估器(无状态,可安全复用)
// =============================================================================
//
// 三个阶段函数均只依赖入参,类不保存状态。evaluate 是组合入口,
// generateGrid / generateTargets 可单独用于 UI 预览(例如只画网格不求解)。
class RegionCoverageEvaluator
{
  public:
    // 阶段 1:把区域体素化为网格单元。只做几何计算(位置采样 + 坐标系变换),
    // 不做 IK / 碰撞,结果 feasibility 置为 NotEvaluated,待 evaluate 填充。
    RegionCoverageResult generateGrid(
        const AnalysisContext& context,
        const RequirementExecutionRegion& region) const;

    // 阶段 2:为单个网格单元生成一组任务点(按朝向策略),供阶段 3 逐个求解。
    RegionTargetGenerationResult generateTargets(
        const AnalysisContext& context,
        const RequirementExecutionRegion& region,
        const RegionCellResult& cell) const;

    // 阶段 3(组合入口):串行评估所有单元并聚合覆盖率与整体可行性。
    // cancellation 用于用户中断:一旦触发即停止新单元求解,并记录 KIN_REGION_CANCELLED。
    RegionCoverageResult evaluate(
        const AnalysisContext& context,
        const RequirementExecutionRegion& region,
        const CancellationToken& cancellation = CancellationToken ()) const;
};

} // namespace rws

#endif // RWS_KINEMATICANALYSIS_REGIONCOVERAGEEVALUATOR_HPP
