#include "EngineeringRequirementArtifactAdapter.hpp"

#include <rwslibs/engineeringrequirements/RequirementFreezer.hpp>
#include <rwslibs/robotmodelbuilder/RobotModelFingerprint.hpp>

#include <algorithm>

namespace rws {
namespace {

/**
 * @brief 将需求域的工艺语义收敛到 robotanalysiscore 已支持的通用任务类型。
 *
 * MachineLoad/MachineUnload 在 P2 中都以“放置/交接”运动学任务评估；焊缝端点统一
 * 使用 Weld。更细粒度的节拍、接近撤离连续性和工艺动作顺序留给 P3 轨迹评价器，
 * 因此这里不伪造额外的运动学约束。
 */
TaskPointType toTaskPointType(ProcessType processType)
{
    switch (processType) {
    case ProcessType::Pick: return TaskPointType::Pick;
    case ProcessType::Place:
    case ProcessType::MachineLoad:
    case ProcessType::MachineUnload: return TaskPointType::Place;
    case ProcessType::Inspect: return TaskPointType::Inspect;
    case ProcessType::WeldStart:
    case ProcessType::WeldEnd: return TaskPointType::Weld;
    default: return TaskPointType::Generic;
    }
}

/**
 * @brief 将冻结后的关键工位转换为优化器可消费的原子任务点。
 *
 * P2 的运动学评价器可处理固定代表姿态，因此保留冻结器已经解析出的 position/rpy
 * 和公差。接近/撤离规则只写入 note，用于报告明确标注“路径验证待 P3”，避免把
 * 尚未执行的连续轨迹校验误报为已通过。
 */
OptimizationTaskPoint toOptimizationTask(const CompiledPoseTask& station)
{
    OptimizationTaskPoint optimized;
    optimized.required = station.level == RequirementLevel::Must;
    optimized.point.id = station.id;
    optimized.point.name = station.name;
    optimized.point.type = toTaskPointType(station.processType);
    optimized.point.refFrame = station.refFrame;
    optimized.point.tcpFrame = station.tcpFrame;
    optimized.point.position = station.position;
    optimized.point.rpyDeg = station.rpyDeg;
    optimized.point.tolerance.positionMeters = station.tolerance.positionMeters;
    optimized.point.tolerance.orientationDeg = station.tolerance.orientationDeg;
    optimized.point.tolerance.allowToolRollFree = station.tolerance.allowToolRollFree;
    optimized.point.weight = optimized.required ? 1.0 : 0.5;
    optimized.point.enabled = true;
    if (station.pathValidationPending)
        optimized.point.note = "Approach/retract path validation is pending for the P3 trajectory evaluator.";
    return optimized;
}

bool isWorld(const std::string& frame)
{
    return frame.empty() || frame == "WORLD";
}

} // namespace

bool EngineeringRequirementArtifactAdapter::apply(const FrozenRequirementArtifact& artifact,
                                                   StructureOptimizationProblem& problem,
                                                   std::string* error)
{
    // 冻结标识、完整审计指纹和内部模型绑定是跨插件交付的最低门槛。仅有 UI 的
    // frozen 标记不能证明任务已经在真实场景中解析，因此这里必须同时检查三者。
    if (!artifact.compiled.frozen || artifact.requirementFingerprint.empty() ||
        artifact.workcellFingerprint.empty() || artifact.modelBinding.robotModelFingerprint.empty() ||
        artifact.compiled.modelBinding.robotModelFingerprint != artifact.modelBinding.robotModelFingerprint ||
        artifact.compiled.requirementFingerprint != artifact.requirementFingerprint) {
        if (error != nullptr) *error = "Engineering requirement artifact is not a complete frozen artifact.";
        return false;
    }

    const std::string problemFingerprint = RobotModelFingerprint::canonicalSha256(problem.context.modelSpec);
    if (problemFingerprint.empty() || problemFingerprint != artifact.modelBinding.robotModelFingerprint) {
        if (error != nullptr) *error = "Frozen engineering requirements do not match the optimization RobotModelSpec.";
        return false;
    }
    if (!artifact.modelBinding.robotName.empty() &&
        artifact.modelBinding.robotName != problem.context.modelSpec.robotName) {
        if (error != nullptr) *error = "Frozen engineering requirements do not match the optimization robot name.";
        return false;
    }

    // 候选模型工厂目前仅由 RobotModelSpec 构建临时 WorkCell，不能携带外部工装
    // Frame。因此 P2 只允许 WORLD 工位；非 WORLD 工位保留在冻结工件中，待未来
    // 将工装场景作为候选评价上下文加载后再适配，不能错误地按 WORLD 静默计算。
    for (const CompiledPoseTask& station : artifact.compiled.poseTasks) {
        if (!isWorld(station.refFrame)) {
            if (error != nullptr)
                *error = "P2 structure optimization supports only WORLD key stations; station '" +
                    station.id + "' uses frame '" + station.refFrame + "'.";
            return false;
        }
    }

    std::vector<WorkspaceDemandRegion> mustRegions;
    for (const WorkspaceDemandRegion& region : artifact.compiled.workspaceRegions) {
        if (region.level == RequirementLevel::Should) {
            if (error != nullptr)
                *error = "P2 structure optimization does not support optional workspace coverage regions: '" +
                    region.id + "'.";
            return false;
        }
        if (region.level == RequirementLevel::Must) mustRegions.push_back(region);
    }
    if (mustRegions.size() > 1) {
        if (error != nullptr) *error = "P2 structure optimization supports one required workspace coverage region at a time.";
        return false;
    }
    if (!mustRegions.empty() && !isWorld(mustRegions.front().refFrame)) {
        if (error != nullptr)
            *error = "P2 structure optimization supports only a WORLD workspace coverage region; region '" +
                mustRegions.front().id + "' uses frame '" + mustRegions.front().refFrame + "'.";
        return false;
    }

    // 先在局部副本中完成全部变换和约束生成，所有校验成功后一次性写回，保证调用
    // 失败时不会留下“任务已替换但覆盖盒未替换”之类的半更新优化问题。
    StructureOptimizationProblem updated = problem;
    updated.tasks.clear();
    updated.context.taskPoints.clear();
    for (const CompiledPoseTask& station : artifact.compiled.poseTasks) {
        OptimizationTaskPoint task = toOptimizationTask(station);
        updated.context.taskPoints.push_back(task.point);
        updated.tasks.push_back(task);
    }

    // 每次适配前先移除上一次由本适配器生成的覆盖率约束，随后按本次冻结工件
    // 重建。这保证需求区域的名称、阈值或采样密度变化后不会沿用旧阈值，也不会
    // 累积多个同 ID 的约束；用户手工创建的其他约束不会受到影响。
    updated.constraints.erase(std::remove_if(updated.constraints.begin(), updated.constraints.end(),
        [] (const StructureConstraint& constraint) {
            return constraint.id.find("engineering_requirement.workspace.") == 0;
        }), updated.constraints.end());

    if (mustRegions.empty()) {
        // 冻结需求中没有覆盖区域时显式关闭旧项目遗留的覆盖盒，防止上一次项目的
        // 空间约束无意中影响本次优化。只移除本适配器创建的带前缀约束。
        updated.evaluation.coverageBox.enabled = false;
    }
    else {
        const WorkspaceDemandRegion& region = mustRegions.front();
        WorkspaceCoverageBox& box = updated.evaluation.coverageBox;
        box.enabled = true;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            box.minimum[axis] = region.center[axis] - region.size[axis] * 0.5;
            box.maximum[axis] = region.center[axis] + region.size[axis] * 0.5;
            // samplesPerAxis 表示采样点数，而 CoverageBox 的 cells 表示相邻采样点
            // 之间的网格数，因此需要减一并保证退化配置至少有一个网格。
            box.cells[axis] = std::max(1, region.samplesPerAxis - 1);
        }

        const std::string constraintId = "engineering_requirement.workspace." + region.id;
        StructureConstraint coverageConstraint;
        coverageConstraint.id = constraintId;
        coverageConstraint.label = "Frozen engineering requirement workspace coverage: " + region.name;
        coverageConstraint.targetName = region.id;
        coverageConstraint.kind = StructureConstraintKind::MinimumWorkspaceCoverage;
        coverageConstraint.threshold = region.minimumCoverage;
        coverageConstraint.enabled = true;
        coverageConstraint.hard = true;
        updated.constraints.push_back(coverageConstraint);
    }

    updated.requirementProvenance.requirementFingerprint = artifact.requirementFingerprint;
    updated.requirementProvenance.workcellFingerprint = artifact.workcellFingerprint;
    updated.requirementProvenance.compilerVersion = artifact.compilerVersion;
    // 适配器只复制冻结工件本身已经持久化的时间，而不在导入优化器时重新取当前时间；
    // 否则同一份需求工件被重复导入会产生不同审计身份，破坏可复现性。
    updated.requirementProvenance.frozenAt = artifact.frozenAt;
    problem = updated;
    if (error != nullptr) error->clear();
    return true;
}

} // namespace rws
