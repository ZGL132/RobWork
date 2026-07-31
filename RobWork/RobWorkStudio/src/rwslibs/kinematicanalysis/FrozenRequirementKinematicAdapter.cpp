#include "FrozenRequirementKinematicAdapter.hpp"

#include <rwslibs/engineeringrequirements/RequirementFreezer.hpp>

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

bool FrozenRequirementKinematicAdapter::apply(const FrozenRequirementArtifact& artifact,
                                               const rw::models::WorkCell& workcell,
                                               const rw::kinematics::State& state,
                                               std::vector<TaskPoint>& output,
                                               std::string* error)
{
    // 即使 JSON 有 frozen 标志，也必须复核冻结时的场景和 State。否则夹具移动后仍会把
    // 相对 Frame 的旧位姿送进 IK，产生看似正常、实际对应错误工艺位置的分析结论。
    if (!artifact.compiled.frozen || artifact.requirementFingerprint.empty() ||
        artifact.compiled.requirementFingerprint != artifact.requirementFingerprint) {
        if (error != nullptr) *error = "Engineering requirement artifact is not a complete frozen artifact.";
        return false;
    }
    if (!RequirementFreezer::isScenarioCurrent(artifact, workcell, state, error))
        return false;

    std::vector<TaskPoint> converted;
    converted.reserve(artifact.compiled.poseTasks.size());
    for (const CompiledPoseTask& station : artifact.compiled.poseTasks)
        converted.push_back(toTaskPoint(station));
    output = std::move(converted);
    if (error != nullptr) error->clear();
    return true;
}

} // namespace rws
