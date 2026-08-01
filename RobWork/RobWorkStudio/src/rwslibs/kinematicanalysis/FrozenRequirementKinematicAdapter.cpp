#include "FrozenRequirementKinematicAdapter.hpp"

#include <rwslibs/engineeringrequirements/RequirementFreezer.hpp>

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
    return point;
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
    return applyWithValidation(artifact, workcell, state, output, error, nullptr, nullptr);
}

bool FrozenRequirementKinematicAdapter::applyWithValidation(const FrozenRequirementArtifact& artifact,
                                               const rw::models::WorkCell& workcell,
                                               const rw::kinematics::State& state,
                                               std::vector<TaskPoint>& output,
                                               std::string* error,
                                               bool* robotStateChanged,
                                               std::vector<std::string>* warnings)
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
    FrozenRequirementValidationResult validation;
    if (!RequirementFreezer::validateScenario(artifact, workcell, state, &validation, error))
        return false;

    std::vector<TaskPoint> converted;
    converted.reserve(artifact.compiled.poseTasks.size());
    for (const CompiledPoseTask& station : artifact.compiled.poseTasks)
        converted.push_back(toTaskPoint(station));
    output = std::move(converted);
    if (robotStateChanged != nullptr) *robotStateChanged = validation.robotStateChanged;
    if (warnings != nullptr) *warnings = validation.warnings;
    if (error != nullptr) error->clear();
    return true;
}

} // namespace rws
