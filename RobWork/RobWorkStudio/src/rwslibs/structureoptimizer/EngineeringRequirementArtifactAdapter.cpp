#include "EngineeringRequirementArtifactAdapter.hpp"

#include <rwslibs/engineeringrequirements/RequirementFreezer.hpp>
#include <rwslibs/robotanalysiscore/RequirementExecutionJson.hpp>
#include <rwslibs/robotanalysiscore/RequirementExecutionTypes.hpp>
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
    // 将冻结阶段实际使用的姿态解析证据随任务点下传。优化报告据此可以说明“该 RPY
    // 来自何种工程规则”，而不是只展示一个失去语义来源的欧拉角数值。
    if (!station.orientation.resolutionEvidence.empty())
        optimized.point.note = "Orientation resolution: " + station.orientation.resolutionEvidence;
    if (station.pathValidationPending) {
        if (!optimized.point.note.empty()) optimized.point.note += " | ";
        optimized.point.note += "Approach/retract path validation is pending for the P3 trajectory evaluator.";
    }
    const auto appendPath = [&optimized] (const char* name, const ApproachRetractRule& rule) {
        if (!rule.enabled) return;
        if (!optimized.point.note.empty()) optimized.point.note += " | ";
        optimized.point.note += std::string(name) + " path: axis=" +
            (rule.axis == OffsetAxis::ReferenceZ ? "ReferenceZ" : "ToolZ") +
            ", distanceMeters=" + std::to_string(rule.distanceMeters) +
            ", collisionFreeRequired=" + (rule.collisionFreeRequired ? "true" : "false");
    };
    appendPath("Approach", station.approach);
    appendPath("Retract", station.retract);
    return optimized;
}

bool isWorld(const std::string& frame)
{
    return frame.empty() || frame == "WORLD";
}

ProcessType toProcessType(RequirementExecutionProcessType value)
{
    switch (value) {
    case RequirementExecutionProcessType::Generic: return ProcessType::Generic;
    case RequirementExecutionProcessType::Pick: return ProcessType::Pick;
    case RequirementExecutionProcessType::Place: return ProcessType::Place;
    case RequirementExecutionProcessType::MachineLoad: return ProcessType::MachineLoad;
    case RequirementExecutionProcessType::MachineUnload: return ProcessType::MachineUnload;
    case RequirementExecutionProcessType::Inspect: return ProcessType::Inspect;
    case RequirementExecutionProcessType::WeldStart: return ProcessType::WeldStart;
    case RequirementExecutionProcessType::WeldEnd: return ProcessType::WeldEnd;
    case RequirementExecutionProcessType::ToolChange: return ProcessType::ToolChange;
    case RequirementExecutionProcessType::SafeStandby: return ProcessType::SafeStandby;
    case RequirementExecutionProcessType::Handover: return ProcessType::Handover;
    }
    return ProcessType::Generic;
}

CompiledRequirementSet executionSnapshot(const FrozenRequirementArtifact& artifact,
                                         std::string* error)
{
    CompiledRequirementSet snapshot = artifact.compiled;
    if (artifact.schemaVersion < 4 || artifact.executionFingerprint.empty())
        return snapshot;
    if (artifact.executionFingerprint.empty() ||
        artifact.executionFingerprint != RequirementExecutionJson::fingerprint(artifact.execution) ||
        !RequirementExecutionJson::validate(artifact.execution, error)) {
        if (error != nullptr && error->empty())
            *error = "Requirement execution contract is missing or has been modified.";
        return CompiledRequirementSet();
    }
    const RequirementExecutionProvenance& provenance = artifact.execution.provenance;
    if (artifact.execution.schemaVersion < 1 ||
        provenance.requirementFingerprint != artifact.requirementFingerprint ||
        provenance.robotModelFingerprint != artifact.modelBinding.robotModelFingerprint ||
        provenance.workcellFingerprint != artifact.workcellFingerprint ||
        provenance.environmentFingerprint != artifact.environmentFingerprint ||
        provenance.compilerVersion != artifact.compilerVersion ||
        provenance.frozenAt != artifact.frozenAt) {
        if (error != nullptr)
            *error = "Requirement execution contract provenance does not match the frozen artifact.";
        return CompiledRequirementSet();
    }
    snapshot.poseTasks.clear();
    for (const RequirementExecutionTask& source : artifact.execution.tasks) {
        CompiledPoseTask target;
        target.id = source.id;
        target.name = source.name;
        target.level = static_cast<RequirementLevel>(source.level);
        target.processType = toProcessType(source.processType);
        target.refFrame = source.refFrame;
        target.tcpFrame = source.tcpFrame;
        target.position = source.position;
        target.rpyDeg = source.rpyDeg;
        target.tolerance.positionMeters = source.positionToleranceMeters;
        target.tolerance.orientationDeg = source.orientationToleranceDeg;
        target.tolerance.allowToolRollFree = source.allowToolRollFree;
        target.orientation.mode = static_cast<OrientationMode>(source.orientationMode);
        target.orientation.targetFrame = source.orientationTargetFrame;
        target.orientation.targetGeometry = source.orientationTargetGeometry;
        target.orientation.targetPoint = source.orientationTargetPoint;
        target.orientation.invertNormal = source.invertNormal;
        target.orientation.rollMinimumDeg = source.rollMinimumDeg;
        target.orientation.rollMaximumDeg = source.rollMaximumDeg;
        target.orientation.resolutionEvidence = source.resolutionEvidence;
        target.approach.enabled = source.approach.enabled;
        target.approach.axis = source.approach.axis == RequirementExecutionOffsetAxis::ReferenceZ
            ? OffsetAxis::ReferenceZ : OffsetAxis::ToolZ;
        target.approach.distanceMeters = source.approach.distanceMeters;
        target.approach.collisionFreeRequired = source.approach.collisionFreeRequired;
        target.retract.enabled = source.retract.enabled;
        target.retract.axis = source.retract.axis == RequirementExecutionOffsetAxis::ReferenceZ
            ? OffsetAxis::ReferenceZ : OffsetAxis::ToolZ;
        target.retract.distanceMeters = source.retract.distanceMeters;
        target.retract.collisionFreeRequired = source.retract.collisionFreeRequired;
        target.pathValidationPending = source.pathValidationPending ||
            source.approach.enabled || source.retract.enabled;
        target.validation.collisionFreeRequired = source.collisionFreeRequired;
        target.validation.minimumJointMargin = source.minimumJointMargin;
        target.validation.minimumManipulability = source.minimumManipulability;
        target.compileState = static_cast<RequirementCompileState>(source.compileState);
        target.excludedReason = source.excludedReason;
        snapshot.poseTasks.push_back(target);
    }
    snapshot.workspaceRegions.clear();
    for (const RequirementExecutionRegion& source : artifact.execution.workspaceRegions) {
        WorkspaceDemandRegion target;
        target.id = source.id;
        target.name = source.name;
        target.level = static_cast<RequirementLevel>(source.level);
        target.refFrame = source.refFrame;
        target.tcpFrame = source.tcpFrame;
        target.center = source.center;
        target.size = source.size;
        target.minimumCoverage = source.minimumCoverage;
        target.samplesPerAxis = source.samplesPerAxis;
        target.orientationMode = static_cast<OrientationMode>(source.orientationMode);
        target.orientationTargetFrame = source.orientationTargetFrame;
        target.orientationTargetGeometry = source.orientationTargetGeometry;
        target.orientationTargetPoint = source.orientationTargetPoint;
        target.fixedRpyDeg = source.fixedRpyDeg;
        target.directionSamples = source.directionSamples;
        target.rollSamples = source.rollSamples;
        target.minimumOrientationCoverage = source.minimumOrientationCoverage;
        target.minimumVerificationStage = static_cast<RequirementVerificationStage>(source.minimumVerificationStage);
        target.collisionFreeRequired = source.collisionFreeRequired;
        target.positionToleranceMeters = source.positionToleranceMeters;
        target.orientationToleranceDeg = source.orientationToleranceDeg;
        target.minimumJointMargin = source.minimumJointMargin;
        target.minimumManipulability = source.minimumManipulability;
        target.compileState = static_cast<RequirementCompileState>(source.compileState);
        target.excludedReason = source.excludedReason;
        snapshot.workspaceRegions.push_back(target);
    }
    return snapshot;
}

} // namespace

bool EngineeringRequirementArtifactAdapter::apply(const FrozenRequirementArtifact& artifact,
                                                   StructureOptimizationProblem& problem,
                                                   std::string* error)
{
    // 冻结标识、完整审计指纹和内部模型绑定是跨插件交付的最低门槛。仅有 UI 的
    // frozen 标记不能证明任务已经在真实场景中解析，因此这里必须同时检查三者。
    if (artifact.schemaVersion != 3 && artifact.schemaVersion != 4) {
        if (error != nullptr)
            *error = "Frozen engineering requirements use legacy state-based evidence. Validate and freeze the requirements again.";
        return false;
    }
    if (!artifact.compiled.frozen || artifact.requirementFingerprint.empty() ||
        artifact.environmentFingerprint.empty() || artifact.frozenRobotState.deviceName.empty() ||
        artifact.frozenRobotState.tcpFrameName.empty() ||
        artifact.frozenRobotState.kinematicFingerprint.empty() ||
        artifact.frozenRobotState.capturedAt.empty() ||
        artifact.scenario.environmentFingerprint != artifact.environmentFingerprint ||
        artifact.modelBinding.robotModelFingerprint.empty() ||
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

    const CompiledRequirementSet compiled = executionSnapshot(artifact, error);
    if (artifact.schemaVersion >= 4 && compiled.frozen == false)
        return false;

    bool needsFrozenScenario = false;
    for (const CompiledPoseTask& station : compiled.poseTasks)
        needsFrozenScenario = needsFrozenScenario || !isWorld(station.refFrame);
    for (const WorkspaceDemandRegion& region : compiled.workspaceRegions) {
        if (region.compileState != RequirementCompileState::Included) continue;
        needsFrozenScenario = needsFrozenScenario || !isWorld(region.refFrame);
    }
    if (needsFrozenScenario &&
        (artifact.schemaVersion < 2 || artifact.scenario.snapshotFingerprint.empty() ||
         artifact.scenario.sceneSpec.sceneFrames.empty())) {
        if (error != nullptr)
            *error = "Frozen engineering requirements use non-WORLD frames but do not contain a valid scenario snapshot.";
        return false;
    }

    std::vector<WorkspaceDemandRegion> mustRegions;
    for (const WorkspaceDemandRegion& region : compiled.workspaceRegions) {
        if (region.compileState != RequirementCompileState::Included) continue;
        if (region.level == RequirementLevel::Should) {
            if (error != nullptr)
                *error = "P2 structure optimization does not support optional workspace coverage regions: '" +
                    region.id + "'.";
            return false;
        }
        if (region.level == RequirementLevel::Must) mustRegions.push_back(region);
    }

    // 先在局部副本中完成全部变换和约束生成，所有校验成功后一次性写回，保证调用
    // 失败时不会留下“任务已替换但覆盖盒未替换”之类的半更新优化问题。
    StructureOptimizationProblem updated = problem;
    updated.tasks.clear();
    updated.context.taskPoints.clear();
    for (const CompiledPoseTask& station : compiled.poseTasks) {
        if (station.compileState != RequirementCompileState::Included) continue;
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

    updated.evaluation.coverageBoxes.clear();
    if (mustRegions.empty()) {
        // 冻结需求中没有覆盖区域时显式关闭旧项目遗留的覆盖盒，防止上一次项目的
        // 空间约束无意中影响本次优化。只移除本适配器创建的带前缀约束。
        updated.evaluation.coverageBox.enabled = false;
    }
    else {
        // 每一个 Must 覆盖区域必须保留为独立盒。这里同时更新旧 coverageBox，保持原有
        // 编辑器和旧项目格式可见；真正的候选评价仅消费 coverageBoxes 中的完整集合。
        updated.evaluation.coverageBox.enabled = false;
        for (std::size_t regionIndex = 0; regionIndex < mustRegions.size(); ++regionIndex) {
            const WorkspaceDemandRegion& region = mustRegions[regionIndex];
            WorkspaceCoverageBox box;
            box.id = region.id;
            box.referenceFrame = region.refFrame.empty() ? "WORLD" : region.refFrame;
            box.enabled = true;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            box.minimum[axis] = region.center[axis] - region.size[axis] * 0.5;
            box.maximum[axis] = region.center[axis] + region.size[axis] * 0.5;
            // samplesPerAxis 表示采样点数，而 CoverageBox 的 cells 表示相邻采样点
            // 之间的网格数，因此需要减一并保证退化配置至少有一个网格。
            box.cells[axis] = std::max(1, region.samplesPerAxis - 1);
        }
            if (regionIndex == 0) updated.evaluation.coverageBox = box;
            updated.evaluation.coverageBoxes.push_back(box);

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
    }

    updated.requirementProvenance.requirementFingerprint = artifact.requirementFingerprint;
    updated.requirementProvenance.executionFingerprint = artifact.executionFingerprint;
    updated.requirementProvenance.workcellFingerprint = artifact.workcellFingerprint;
    updated.requirementProvenance.environmentFingerprint = artifact.environmentFingerprint;
    updated.requirementProvenance.compilerVersion = artifact.compilerVersion;
    // 适配器只复制冻结工件本身已经持久化的时间，而不在导入优化器时重新取当前时间；
    // 否则同一份需求工件被重复导入会产生不同审计身份，破坏可复现性。
    updated.requirementProvenance.frozenAt = artifact.frozenAt;
    // 只从 schema v2 工件复制可重建场景；schema v1 仍可导入 WORLD 任务，但绝不会被
    // 误认为携带工装碰撞环境，从而保持历史项目的兼容性和新场景流程的正确性。
    updated.scenarioSnapshot = StructureOptimizationScenarioSnapshot();
    if (!artifact.scenario.snapshotFingerprint.empty()) {
        updated.scenarioSnapshot.schemaVersion = artifact.scenario.schemaVersion;
        updated.scenarioSnapshot.sourceWorkCellPath = artifact.scenario.sourceWorkCellPath;
        updated.scenarioSnapshot.sourceFileFingerprint = artifact.scenario.sourceFileFingerprint;
        updated.scenarioSnapshot.snapshotFingerprint = artifact.scenario.snapshotFingerprint;
        updated.scenarioSnapshot.deviceName = artifact.scenario.deviceName;
        updated.scenarioSnapshot.environmentFingerprint = artifact.scenario.environmentFingerprint;
        updated.scenarioSnapshot.stateFingerprint = artifact.scenario.stateFingerprint;
        updated.scenarioSnapshot.sceneSpec = artifact.scenario.sceneSpec;
    }
    problem = updated;
    if (error != nullptr) error->clear();
    return true;
}

} // namespace rws
