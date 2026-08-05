#include "FrozenRequirementKinematicAdapter.hpp"

#include <rwslibs/engineeringrequirements/RequirementFreezer.hpp>
#include <rwslibs/robotanalysiscore/RequirementExecutionJson.hpp>
#include <rwslibs/robotanalysiscore/RequirementExecutionTypes.hpp>

#include <QString>

namespace rws {
namespace {

/**
 * @brief 将需求侧工艺类型映射为运动学任务类型。
 *
 * 该映射只服务于当前 IK 批量分析的分类和界面展示；接近、撤离和节拍语义仍保留在
 * 冻结工件中，等待 P3 轨迹评价器消费，不能在这里伪装成已经通过连续路径验证。
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

TaskPoint toTaskPoint(const CompiledPoseTask& station)
{
    TaskPoint point;
    point.id = station.id;
    point.name = station.name;
    point.type = toTaskPointType(station.processType);
    point.refFrame = station.refFrame;
    point.tcpFrame = station.tcpFrame;
    point.position = station.position;
    point.rpyDeg = station.rpyDeg;
    point.tolerance.positionMeters = station.tolerance.positionMeters;
    point.tolerance.orientationDeg = station.tolerance.orientationDeg;
    point.tolerance.allowToolRollFree = station.tolerance.allowToolRollFree;
    point.weight = station.level == RequirementLevel::Must ? 1.0 : 0.5;
    point.enabled = true;
    if (!station.orientation.resolutionEvidence.empty())
        point.note = "Orientation resolution: " + station.orientation.resolutionEvidence;
    if (station.pathValidationPending) {
        if (!point.note.empty()) point.note += " | ";
        point.note += "Approach/retract path validation is pending for the P3 trajectory evaluator.";
    }
    const auto appendPath = [&point] (const char* name, const ApproachRetractRule& rule) {
        if (!rule.enabled) return;
        if (!point.note.empty()) point.note += " | ";
        point.note += std::string(name) + " path: axis=" +
            (rule.axis == OffsetAxis::ReferenceZ ? "ReferenceZ" : "ToolZ") +
            ", distanceMeters=" + std::to_string(rule.distanceMeters) +
            ", collisionFreeRequired=" + (rule.collisionFreeRequired ? "true" : "false");
    };
    appendPath("Approach", station.approach);
    appendPath("Retract", station.retract);
    return point;
}

TaskPointType toTaskPointType(RequirementExecutionProcessType processType)
{
    switch (processType) {
    case RequirementExecutionProcessType::Pick: return TaskPointType::Pick;
    case RequirementExecutionProcessType::Place:
    case RequirementExecutionProcessType::MachineLoad:
    case RequirementExecutionProcessType::MachineUnload: return TaskPointType::Place;
    case RequirementExecutionProcessType::Inspect: return TaskPointType::Inspect;
    case RequirementExecutionProcessType::WeldStart:
    case RequirementExecutionProcessType::WeldEnd: return TaskPointType::Weld;
    default: return TaskPointType::Generic;
    }
}

TaskPoint toTaskPoint(const RequirementExecutionTask& task)
{
    TaskPoint point;
    point.id = task.id;
    point.name = task.name;
    point.type = toTaskPointType(task.processType);
    point.refFrame = task.refFrame;
    point.tcpFrame = task.tcpFrame;
    point.position = task.position;
    point.rpyDeg = task.rpyDeg;
    point.tolerance.positionMeters = task.positionToleranceMeters;
    point.tolerance.orientationDeg = task.orientationToleranceDeg;
    point.tolerance.allowToolRollFree = task.allowToolRollFree;
    point.weight = task.level == RequirementExecutionLevel::Must ? 1.0 : 0.5;
    point.enabled = true;
    if (!task.resolutionEvidence.empty())
        point.note = "Orientation resolution: " + task.resolutionEvidence;
    const auto appendPath = [&point] (const char* name, const RequirementExecutionPathRule& rule) {
        if (!rule.enabled) return;
        if (!point.note.empty()) point.note += " | ";
        point.note += std::string(name) + " path: axis=" +
            (rule.axis == RequirementExecutionOffsetAxis::ReferenceZ ? "ReferenceZ" : "ToolZ") +
            ", distanceMeters=" + std::to_string(rule.distanceMeters) +
            ", collisionFreeRequired=" + (rule.collisionFreeRequired ? "true" : "false");
    };
    appendPath("Approach", task.approach);
    appendPath("Retract", task.retract);
    return point;
}

// 将编译态覆盖盒(WorkspaceDemandRegion)完整投影为执行契约覆盖盒
// (RequirementExecutionRegion)。枚举字段用 static_cast 保持同序映射；除映射外不做
// 任何默认值回填，确保采样、朝向、接受阈值与验证阶段在适配器边界不发生漂移。
RequirementExecutionRegion toExecutionRegion(const WorkspaceDemandRegion& source)
{
    RequirementExecutionRegion region;
    region.id = source.id;
    region.name = source.name;
    region.level = static_cast<RequirementExecutionLevel>(source.level);
    region.compileState = static_cast<RequirementExecutionCompileState>(source.compileState);
    region.excludedReason = source.excludedReason;
    region.refFrame = source.refFrame;
    region.tcpFrame = source.tcpFrame;
    region.center = source.center;
    region.size = source.size;
    region.minimumCoverage = source.minimumCoverage;
    region.samplesPerAxis = source.samplesPerAxis;
    region.orientationMode =
        static_cast<RequirementExecutionOrientationMode>(source.orientationMode);
    region.orientationTargetFrame = source.orientationTargetFrame;
    region.orientationTargetGeometry = source.orientationTargetGeometry;
    region.orientationTargetPoint = source.orientationTargetPoint;
    region.fixedRpyDeg = source.fixedRpyDeg;
    region.directionSamples = source.directionSamples;
    region.rollSamples = source.rollSamples;
    region.minimumOrientationCoverage = source.minimumOrientationCoverage;
    region.minimumVerificationStage =
        static_cast<RequirementExecutionStage>(source.minimumVerificationStage);
    region.collisionFreeRequired = source.collisionFreeRequired;
    region.positionToleranceMeters = source.positionToleranceMeters;
    region.orientationToleranceDeg = source.orientationToleranceDeg;
    region.minimumJointMargin = source.minimumJointMargin;
    region.minimumManipulability = source.minimumManipulability;
    return region;
}

} // namespace

bool FrozenRequirementKinematicAdapter::parseArtifactJson(
    const QJsonObject& projectOrArtifact,
    FrozenRequirementArtifact& artifact,
    std::string* error)
{
    // 保存后的需求项目采用 RequirementSet 作为根对象，冻结审计证据嵌套在
    // frozenArtifact 中；独立工件则直接以 FrozenEngineeringRequirementArtifact
    // 为根对象。先在这里统一解包，避免每个下游插件各自实现一套容易漂移的判断。
    const QJsonValue nestedArtifact = projectOrArtifact.value("frozenArtifact");
    QJsonObject artifactObject;
    if (nestedArtifact.isObject()) {
        artifactObject = nestedArtifact.toObject();
    } else if (projectOrArtifact.value("type").toString() ==
               "FrozenEngineeringRequirementArtifact") {
        artifactObject = projectOrArtifact;
    } else {
        const QString rootType = projectOrArtifact.value("type").toString();
        if (nestedArtifact.isUndefined()) {
            // RequirementSet 根对象而没有 frozenArtifact 时，最常见原因是用户保存
            // 前尚未执行“冻结需求”。该情况不能退化为直接编译可编辑需求，否则会绕过
            // WorkCell/State 指纹复核，破坏冻结工件的可审计边界。
            // RequirementSetJson 的历史兼容格式没有顶层 type 字段，因此不能只按
            // type 识别项目。modelBinding 加任务/覆盖区域字段是该格式稳定的结构特征。
            const bool looksLikeRequirementProject =
                rootType == "EngineeringRequirementSet" ||
                (projectOrArtifact.value("modelBinding").isObject() &&
                 (projectOrArtifact.value("poseTasks").isArray() ||
                  projectOrArtifact.value("boxRegions").isArray()));
            if (looksLikeRequirementProject) {
                if (error != nullptr) {
                    *error = "Selected engineering requirements project is not frozen. "
                             "Freeze it in EngineeringRequirements and save the project again.";
                }
            } else {
                if (error != nullptr) {
                    *error = "Selected JSON is neither a frozen engineering requirement artifact "
                             "nor an EngineeringRequirements project with a frozenArtifact field. "
                             "Detected root type: " +
                             (rootType.isEmpty() ? std::string("<missing>") : rootType.toStdString()) + ".";
                }
            }
        } else {
            // 字段存在却不是对象通常意味着用户手工改坏了 JSON，或使用了旧的外部
            // 导出工具。明确指出字段而不交给底层 schema 校验，可直接缩短现场排查。
            if (error != nullptr) {
                *error = "Engineering requirements project has an invalid frozenArtifact field; "
                         "the field must be a JSON object created by the freeze operation.";
            }
        }
        return false;
    }

    if (!FrozenRequirementArtifactJson::fromObject(artifactObject, artifact, error)) {
        // 嵌套字段存在但不是冻结工件时保留底层的完整校验结果，同时补充输入位置，
        // 方便区分“选错文件”和“冻结记录损坏”。
        if (error != nullptr) {
            *error = "Invalid frozenArtifact object: " + *error;
        }
        return false;
    }
    return true;
}

bool FrozenRequirementKinematicAdapter::apply(const FrozenRequirementArtifact& artifact,
                                               const rw::models::WorkCell& workcell,
                                               const rw::kinematics::State& state,
                                               std::vector<TaskPoint>& output,
                                               std::string* error)
{
    return apply(artifact, workcell, state, output, error, std::string());
}

bool FrozenRequirementKinematicAdapter::apply(const FrozenRequirementArtifact& artifact,
                                               const rw::models::WorkCell& workcell,
                                               const rw::kinematics::State& state,
                                               std::vector<TaskPoint>& output,
                                               std::string* error,
                                               const std::string& artifactBaseDirectory)
{
    return applyWithValidation(
        artifact, workcell, state, output, error, nullptr, nullptr, artifactBaseDirectory);
}

// —— FrozenKinematicRequirementInput 重载族：在既有"仅工位任务点"的输出类型之外，
// 新增同时输出工位任务点与工作区覆盖盒的输入契约类型。所有重载都汇聚到带
// artifactBaseDirectory 的 applyWithValidation 完整实现，避免各调用方重复实现
// 校验/投影逻辑。参数解释：
//   artifactBaseDirectory —— 工件所在目录，用于解析相对路径的场景快照资源。
bool FrozenRequirementKinematicAdapter::apply(const FrozenRequirementArtifact& artifact,
                                               const rw::models::WorkCell& workcell,
                                               const rw::kinematics::State& state,
                                               FrozenKinematicRequirementInput& output,
                                               std::string* error)
{
    return apply(artifact, workcell, state, output, error, std::string());
}

bool FrozenRequirementKinematicAdapter::apply(
    const FrozenRequirementArtifact& artifact,
    const rw::models::WorkCell& workcell,
    const rw::kinematics::State& state,
    FrozenKinematicRequirementInput& output,
    std::string* error,
    const std::string& artifactBaseDirectory)
{
    return applyWithValidation(
        artifact, workcell, state, output, error, nullptr, nullptr, artifactBaseDirectory);
}

bool FrozenRequirementKinematicAdapter::applyWithValidation(
    const FrozenRequirementArtifact& artifact,
    const rw::models::WorkCell& workcell,
    const rw::kinematics::State& state,
    std::vector<TaskPoint>& output,
    std::string* error,
    bool* robotStateChanged,
    std::vector<std::string>* warnings)
{
    return applyWithValidation(artifact, workcell, state, output, error, robotStateChanged,
                               warnings, std::string());
}

// 旧版输出类型(std::vector<TaskPoint>)的适配器实现：委托给 FrozenKinematicRequirementInput
// 重载，再把结果中的工位任务点移交出去(覆盖盒对旧调用方不可见，保持向后兼容)。
bool FrozenRequirementKinematicAdapter::applyWithValidation(
    const FrozenRequirementArtifact& artifact,
    const rw::models::WorkCell& workcell,
    const rw::kinematics::State& state,
    std::vector<TaskPoint>& output,
    std::string* error,
    bool* robotStateChanged,
    std::vector<std::string>* warnings,
    const std::string& artifactBaseDirectory)
{
    FrozenKinematicRequirementInput converted;
    if (!applyWithValidation(artifact, workcell, state, converted, error, robotStateChanged,
                             warnings, artifactBaseDirectory))
        return false;
    output = std::move(converted.tasks);
    return true;
}

// 默认 artifactBaseDirectory 为空串的便捷重载，转发到完整实现。
bool FrozenRequirementKinematicAdapter::applyWithValidation(
    const FrozenRequirementArtifact& artifact,
    const rw::models::WorkCell& workcell,
    const rw::kinematics::State& state,
    FrozenKinematicRequirementInput& output,
    std::string* error,
    bool* robotStateChanged,
    std::vector<std::string>* warnings)
{
    return applyWithValidation(artifact, workcell, state, output, error, robotStateChanged,
                               warnings, std::string());
}

bool FrozenRequirementKinematicAdapter::applyWithValidation(
    const FrozenRequirementArtifact& artifact,
    const rw::models::WorkCell& workcell,
    const rw::kinematics::State& state,
    FrozenKinematicRequirementInput& output,
    std::string* error,
    bool* robotStateChanged,
    std::vector<std::string>* warnings,
    const std::string& artifactBaseDirectory)
{
    // 即使 JSON 有 frozen 标志，也必须复核冻结时的场景和 State。否则夹具移动后仍会把
    // 相对 Frame 的旧位姿送进 IK，产生看似正常、实际对应错误工艺位置的分析结论。
    if (!artifact.compiled.frozen) {
        if (error != nullptr) *error = "Engineering requirement artifact has no compiled frozen requirements.";
        return false;
    }
    if (artifact.requirementFingerprint.empty()) {
        if (error != nullptr) *error = "Engineering requirement artifact has no requirement fingerprint.";
        return false;
    }
    if (artifact.compiled.requirementFingerprint != artifact.requirementFingerprint) {
        if (error != nullptr)
            *error = "Engineering requirement artifact compiled requirements do not match its fingerprint.";
        return false;
    }
    if (artifact.schemaVersion >= 4) {
        if (artifact.executionFingerprint.empty() ||
            artifact.executionFingerprint != RequirementExecutionJson::fingerprint(artifact.execution) ||
            !RequirementExecutionJson::validate(artifact.execution, error)) {
            if (error != nullptr && error->empty())
                *error = "Requirement execution contract is missing or has been modified.";
            return false;
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
            return false;
        }
        // 额外执行编译快照与执行契约的交叉一致性校验，双保险防止漂移。
        if (!RequirementFreezer::validateExecutionConsistency(artifact, error))
            return false;
    }
    // 复核冻结时刻的场景与 State，避免夹具移动后把旧位姿送进 IK。
    FrozenRequirementValidationResult validation;
    if (!RequirementFreezer::validateScenario(
            artifact, workcell, state, &validation, error, artifactBaseDirectory))
        return false;

    // 投影阶段：只把 Included 的项送入运动学输入(Excluded/Invalid 均跳过)。
    // v4 从执行契约读取工位任务点与覆盖盒；v3 从编译快照读取，覆盖盒经
    // toExecutionRegion 投影为执行契约类型，保证下游拿到统一类型。
    FrozenKinematicRequirementInput converted;
    if (artifact.schemaVersion >= 4) {
        converted.tasks.reserve(artifact.execution.tasks.size());
        for (const RequirementExecutionTask& task : artifact.execution.tasks) {
            if (task.compileState != RequirementExecutionCompileState::Included) continue;
            converted.tasks.push_back(toTaskPoint(task));
        }
        converted.workspaceRegions.reserve(artifact.execution.workspaceRegions.size());
        for (const RequirementExecutionRegion& region : artifact.execution.workspaceRegions) {
            if (region.compileState != RequirementExecutionCompileState::Included) continue;
            converted.workspaceRegions.push_back(region);
        }
    }
    else {
        converted.tasks.reserve(artifact.compiled.poseTasks.size());
        for (const CompiledPoseTask& station : artifact.compiled.poseTasks) {
            if (station.compileState != RequirementCompileState::Included) continue;
            converted.tasks.push_back(toTaskPoint(station));
        }
        converted.workspaceRegions.reserve(artifact.compiled.workspaceRegions.size());
        for (const WorkspaceDemandRegion& region : artifact.compiled.workspaceRegions) {
            if (region.compileState != RequirementCompileState::Included) continue;
            converted.workspaceRegions.push_back(toExecutionRegion(region));
        }
    }
    // 统一移交输出与场景校验的副作用(机器人状态变化标记、告警)。
    output = std::move(converted);
    if (robotStateChanged != nullptr) *robotStateChanged = validation.robotStateChanged;
    if (warnings != nullptr) *warnings = validation.warnings;
    if (error != nullptr) error->clear();
    return true;
}

} // namespace rws
