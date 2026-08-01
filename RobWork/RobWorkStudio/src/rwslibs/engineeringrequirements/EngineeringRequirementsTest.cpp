#include "EngineeringRequirementTypes.hpp"
#include "GeometryFeatureResolver.hpp"
#include "OrientationRuleResolver.hpp"
#include "RequirementCompiler.hpp"
#include "RequirementFreezer.hpp"
#include "RequirementSetJson.hpp"
#include "RequirementSetUndoStack.hpp"
#include "StationImportService.hpp"
#include "StationTemplateService.hpp"
#include "EngineeringRequirementsWidget.hpp"

#include <rwslibs/robotmodelbuilder/RobotModelXmlWriter.hpp>
#include <rwslibs/robotmodelbuilder/RobotModelFingerprint.hpp>
#include <rwslibs/robotmodelbuilder/RobotModelSpecJson.hpp>

#include <rw/core/Ptr.hpp>
#include <rw/kinematics/FixedFrame.hpp>
#include <rw/kinematics/MovableFrame.hpp>
#include <rw/models/RevoluteJoint.hpp>
#include <rw/kinematics/StateStructure.hpp>
#include <rw/math/Constants.hpp>
#include <rw/math/Q.hpp>
#include <rw/math/RPY.hpp>
#include <rw/math/Transform3D.hpp>
#include <rw/math/Vector3D.hpp>
#include <rw/models/SerialDevice.hpp>
#include <rw/models/WorkCell.hpp>

#include <QCoreApplication>

#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QJsonObject>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cmath>
#include <cstdio>
#include <set>
#include <string>

namespace rws {
// 模板参数的显示策略由界面层统一提供。此处先声明期望的最小接口，以便验证不同
// 工艺类型确实拥有不同的参数集合，而不是让所有模板长期共用同一张表单。
unsigned int templateParameterVisibilityMask(StationTemplateKind kind);
}

namespace {

enum TemplateParameterVisibility : unsigned int {
    TemplateParameterRows = 1U << 0,
    TemplateParameterColumns = 1U << 1,
    TemplateParameterLayers = 1U << 2,
    TemplateParameterRowSpacing = 1U << 3,
    TemplateParameterColumnSpacing = 1U << 4,
    TemplateParameterLayerSpacing = 1U << 5,
    TemplateParameterApproach = 1U << 6,
    TemplateParameterRetract = 1U << 7,
    TemplateParameterClearance = 1U << 8
};

int fail(const char* expression, int line)
{
    std::fprintf(stderr, "FAIL at line %d: %s\n", line, expression);
    return 1;
}

#define REQUIRE(expression) \
    do { if (!(expression)) return fail(#expression, __LINE__); } while (false)

int testFrozenRequirementCompilesOnlyEngineeringTasks()
{
    rws::RequirementSet requirements;
    requirements.name = "MVP requirement set";
    requirements.modelBinding.sourcePath = "robot.rmb.json";
    requirements.modelBinding.robotModelFingerprint = "model-fingerprint";

    rws::PoseTask must;
    must.id = "station_pick";
    must.name = "Pick";
    must.level = rws::RequirementLevel::Must;
    must.refFrame = "WORLD";
    must.tcpFrame = "TCP";
    must.position = {{0.4, 0.1, 0.3}};
    requirements.poseTasks.push_back(must);

    rws::PoseTask should = must;
    should.id = "station_place";
    should.level = rws::RequirementLevel::Should;
    requirements.poseTasks.push_back(should);

    rws::PoseTask info = must;
    info.id = "station_note";
    info.level = rws::RequirementLevel::Info;
    requirements.poseTasks.push_back(info);

    rws::BoxRegion region;
    region.id = "assembly_box";
    region.name = "Assembly box";
    region.level = rws::RequirementLevel::Must;
    region.refFrame = "WORLD";
    region.center = {{0.45, 0.0, 0.35}};
    region.size = {{0.3, 0.2, 0.25}};
    region.minimumCoverage = 0.85;
    requirements.boxRegions.push_back(region);

    rws::CompiledRequirementSet compiled;
    std::string error;
    REQUIRE(rws::RequirementCompiler::compile(requirements, compiled, &error));
    REQUIRE(error.empty());
    REQUIRE(compiled.frozen);
    REQUIRE(compiled.poseTasks.size() == 2);
    REQUIRE(compiled.poseTasks[0].id == "station_pick");
    REQUIRE(compiled.poseTasks[1].level == rws::RequirementLevel::Should);
    REQUIRE(compiled.workspaceRegions.size() == 1);
    REQUIRE(compiled.workspaceRegions[0].minimumCoverage == 0.85);
    REQUIRE(!compiled.requirementFingerprint.empty());
    return 0;
}

int testJsonRoundTripPreservesBindingAndFrozenSnapshot()
{
    rws::RequirementSet requirements;
    requirements.name = "Round trip";
    requirements.version = 3;
    requirements.frozen = true;
    requirements.modelBinding.sourcePath = "C:/project/robot.rmb.json";
    requirements.modelBinding.robotModelFingerprint = "abc123";
    requirements.modelBinding.robotName = "UR-6-85-5-A";

    rws::PoseTask task;
    task.id = "inspect";
    task.name = "Inspect";
    task.level = rws::RequirementLevel::Must;
    task.position = {{0.1, 0.2, 0.3}};
    task.rpyDeg = {{10.0, 20.0, 30.0}};
    requirements.poseTasks.push_back(task);

    const std::string json = rws::RequirementSetJson::toJson(requirements);
    rws::RequirementSet parsed;
    std::string error;
    REQUIRE(rws::RequirementSetJson::fromJson(json, parsed, &error));
    REQUIRE(error.empty());
    REQUIRE(parsed.frozen);
    REQUIRE(parsed.version == 3);
    REQUIRE(parsed.modelBinding.robotModelFingerprint == "abc123");
    REQUIRE(parsed.poseTasks.size() == 1);
    REQUIRE(std::abs(parsed.poseTasks[0].rpyDeg[2] - 30.0) < 1e-12);
    return 0;
}

int testKeyStationPersistsEngineeringIntentAndCompilesWorkPose()
{
    rws::RequirementSet requirements;
    requirements.modelBinding.robotModelFingerprint = "model-fingerprint";

    rws::KeyStation station;
    station.id = "machine_load";
    station.name = "Machine load";
    station.tcpFrame = "TCP";
    station.processType = rws::ProcessType::MachineLoad;
    station.orientation.mode = rws::OrientationMode::AlignFrame;
    station.orientation.targetFrame = "Fixture_A";
    station.orientation.allowToolRollFree = true;
    station.approach.enabled = true;
    station.approach.axis = rws::OffsetAxis::ToolZ;
    station.approach.distanceMeters = 0.10;
    station.retract.enabled = true;
    station.retract.axis = rws::OffsetAxis::ReferenceZ;
    station.retract.distanceMeters = 0.15;
    station.validation.minimumJointMargin = 0.08;
    station.confidence = 0.9;
    requirements.poseTasks.push_back(station);

    const std::string json = rws::RequirementSetJson::toJson(requirements);
    rws::RequirementSet parsed;
    std::string error;
    REQUIRE(rws::RequirementSetJson::fromJson(json, parsed, &error));
    REQUIRE(parsed.poseTasks.size() == 1);
    REQUIRE(parsed.poseTasks[0].processType == rws::ProcessType::MachineLoad);
    REQUIRE(parsed.poseTasks[0].orientation.mode == rws::OrientationMode::AlignFrame);
    REQUIRE(parsed.poseTasks[0].orientation.targetFrame == "Fixture_A");
    REQUIRE(parsed.poseTasks[0].approach.distanceMeters == 0.10);
    REQUIRE(parsed.poseTasks[0].retract.axis == rws::OffsetAxis::ReferenceZ);

    rws::CompiledRequirementSet compiled;
    REQUIRE(rws::RequirementCompiler::compile(parsed, compiled, &error));
    REQUIRE(compiled.poseTasks.size() == 1);
    REQUIRE(compiled.poseTasks[0].processType == rws::ProcessType::MachineLoad);
    REQUIRE(compiled.poseTasks[0].pathValidationPending);
    return 0;
}

int testCompilerKeepsNonBlockingStationDiagnosticsOutOfCompiledTasks()
{
    rws::RequirementSet requirements;
    requirements.modelBinding.robotModelFingerprint = "model-fingerprint";

    rws::KeyStation required;
    required.id = "must_pick";
    required.name = "Must pick";
    required.tcpFrame = "TCP";
    required.orientation.mode = rws::OrientationMode::AlignFrame;
    requirements.poseTasks.push_back(required);

    rws::CompiledRequirementSet compiled;
    std::string error;
    REQUIRE(!rws::RequirementCompiler::compile(requirements, compiled, &error));
    REQUIRE(error.find("target frame") != std::string::npos);

    requirements.poseTasks[0].orientation.targetFrame = "Fixture_A";
    rws::KeyStation advisory = required;
    advisory.id = "should_inspect";
    advisory.name.clear();
    advisory.level = rws::RequirementLevel::Should;
    advisory.refFrame.clear();
    advisory.approach.enabled = true;
    advisory.approach.distanceMeters = -0.01;
    requirements.poseTasks.push_back(advisory);

    REQUIRE(rws::RequirementCompiler::compile(requirements, compiled, &error));
    REQUIRE(compiled.poseTasks.size() == 1);
    REQUIRE(!compiled.diagnostics.empty());
    bool sawNameDiagnostic = false;
    bool sawReferenceDiagnostic = false;
    bool sawApproachDiagnostic = false;
    for (const rws::RequirementDiagnostic& diagnostic : compiled.diagnostics) {
        if (diagnostic.requirementId != "should_inspect") continue;
        REQUIRE(!diagnostic.blocking);
        sawNameDiagnostic = sawNameDiagnostic || diagnostic.message.find("name") != std::string::npos;
        sawReferenceDiagnostic = sawReferenceDiagnostic || diagnostic.message.find("reference frame") != std::string::npos;
        sawApproachDiagnostic = sawApproachDiagnostic || diagnostic.message.find("approach") != std::string::npos;
    }
    REQUIRE(sawNameDiagnostic);
    REQUIRE(sawReferenceDiagnostic);
    REQUIRE(sawApproachDiagnostic);
    return 0;
}

int testGeometryFrameFeatureResolvesAndCompiles()
{
    using namespace rw::kinematics;
    using namespace rw::math;

    StateStructure::Ptr structure = rw::core::ownedPtr(new StateStructure());
    const Frame::Ptr fixture = rw::core::ownedPtr(new FixedFrame(
        "Fixture_A", Transform3D<>(Vector3D<>(0.4, 0.2, 0.3), RPY<>(0.0, 0.0, rw::math::Pi / 2.0))));
    structure->addFrame(fixture, structure->getRoot());
    const rw::models::WorkCell::Ptr workcell = rw::core::ownedPtr(
        new rw::models::WorkCell(structure, "GeometryFeatureTest", ""));

    rws::KeyStation station;
    station.id = "inspect_face";
    station.name = "Inspect fixture face";
    station.tcpFrame = "TCP";
    station.source = rws::PoseTaskSource::GeometryFeature;
    station.geometryFeature.frameName = "Fixture_A";
    station.geometryFeature.type = rws::GeometryFeatureType::FramePlaneNormal;

    rws::GeometryFeatureResolution resolved;
    std::string error;
    REQUIRE(rws::GeometryFeatureResolver::resolve(station.geometryFeature, station.refFrame,
                                                   *workcell, workcell->getDefaultState(), resolved, &error));
    REQUIRE(std::abs(resolved.position[0] - 0.4) < 1e-12);
    REQUIRE(std::abs(resolved.position[1] - 0.2) < 1e-12);
    REQUIRE(std::abs(resolved.rpyDeg[2] - 90.0) < 1e-9);

    REQUIRE(rws::GeometryFeatureResolver::applyToStation(
        station.geometryFeature, *workcell, workcell->getDefaultState(), station, &error));
    REQUIRE(station.orientation.mode == rws::OrientationMode::AlignGeometryNormal);
    REQUIRE(station.orientation.targetFrame == "Fixture_A");

    rws::RequirementSet persisted;
    persisted.modelBinding.robotModelFingerprint = "model-fingerprint";
    persisted.poseTasks.push_back(station);
    rws::RequirementSet reloaded;
    REQUIRE(rws::RequirementSetJson::fromJson(rws::RequirementSetJson::toJson(persisted), reloaded, &error));
    REQUIRE(reloaded.poseTasks[0].geometryFeature.type == rws::GeometryFeatureType::FramePlaneNormal);
    REQUIRE(reloaded.poseTasks[0].geometryFeature.frameName == "Fixture_A");

    rws::RequirementSet requirements;
    requirements.modelBinding.robotModelFingerprint = "model-fingerprint";
    requirements.poseTasks.push_back(station);
    rws::CompiledRequirementSet compiled;
    REQUIRE(rws::RequirementCompiler::compile(requirements, compiled, &error));
    REQUIRE(compiled.poseTasks.size() == 1);
    REQUIRE(compiled.poseTasks[0].geometryFeature.frameName == "Fixture_A");
    return 0;
}

int testFreezerRejectsMissingWorkCellTcpForMustStation()
{
    // 冻结不是纯文本格式检查：即使 TCP 名称非空，只要它不属于当前 WorkCell，
    // 该 Must 工位就绝不能被发布为后续运动学或结构优化的输入。
    using namespace rw::kinematics;
    rws::RequirementSet requirements;
    rws::RobotModelSpec model;
    model.robotName = "FreezeGateRobot";
    requirements.modelBinding.robotName = model.robotName;
    requirements.modelBinding.robotModelFingerprint = rws::RobotModelFingerprint::canonicalSha256(model);

    rws::PoseTask station;
    station.id = "missing_tcp";
    station.name = "Missing TCP";
    station.refFrame = "WORLD";
    station.tcpFrame = "TCP_that_does_not_exist";
    requirements.poseTasks.push_back(station);

    StateStructure::Ptr structure = rw::core::ownedPtr(new StateStructure());
    const rw::models::WorkCell::Ptr workcell = rw::core::ownedPtr(
        new rw::models::WorkCell(structure, "FreezeGateWorkCell", ""));

    rws::FrozenRequirementArtifact artifact;
    std::string error;
    REQUIRE(!rws::RequirementFreezer::freeze(requirements, *workcell,
                                               workcell->getDefaultState(), model, artifact, &error));
    REQUIRE(error.find("robot device") != std::string::npos);
    return 0;
}

int testPointAtTargetResolvesCoordinateTextAndRejectsCoincidentTarget()
{
    // PointAtTarget 的坐标文本以工位参考系表示。该测试覆盖两条不可替代的工程语义：
    // 工具 Z 轴必须准确指向目标点；目标与工位重合时无法确定姿态，冻结必须明确拒绝。
    using namespace rw::kinematics;
    using namespace rw::math;

    StateStructure::Ptr structure = rw::core::ownedPtr(new StateStructure());
    const rw::models::WorkCell::Ptr workcell = rw::core::ownedPtr(
        new rw::models::WorkCell(structure, "PointAtTargetWorkCell", ""));
    rws::KeyStation station;
    station.refFrame = "WORLD";
    station.position = {{0.0, 0.0, 0.0}};
    station.orientation.mode = rws::OrientationMode::PointAtTarget;
    station.orientation.targetPoint = "0.0, 1.0, 0.0";

    std::string error;
    REQUIRE(rws::OrientationRuleResolver::applyToStation(
        station, *workcell, workcell->getDefaultState(), &error));
    const RPY<> resolvedRpy(station.rpyDeg[0] * rw::math::Pi / 180.0,
                             station.rpyDeg[1] * rw::math::Pi / 180.0,
                             station.rpyDeg[2] * rw::math::Pi / 180.0);
    const Vector3D<> toolZ = resolvedRpy.toRotation3D().getCol(2);
    REQUIRE(std::abs(toolZ[0]) < 1e-9);
    REQUIRE(std::abs(toolZ[1] - 1.0) < 1e-9);
    REQUIRE(std::abs(toolZ[2]) < 1e-9);
    // 冻结工件不能只留下 RPY 数字，还必须留下规则模式、目标来源和代表姿态，
    // 以便下游优化报告能够解释该姿态是如何由工程需求确定的。
    REQUIRE(station.orientation.resolutionEvidence.find("PointAtTarget") != std::string::npos);
    REQUIRE(station.orientation.resolutionEvidence.find("targetPoint=0.0, 1.0, 0.0") != std::string::npos);

    station.orientation.targetPoint = "0, 0, 0";
    REQUIRE(!rws::OrientationRuleResolver::applyToStation(
        station, *workcell, workcell->getDefaultState(), &error));
    REQUIRE(error.find("coincides") != std::string::npos);
    return 0;
}

int testOrientationRuleInvertsToolZForFrameAndGeometryNormal()
{
    // 对齐坐标系与对齐几何法向在 P2 都以目标 Frame 的 Z 轴表达法向。工程师勾选
    // “反向法向”后，工具 Z 轴必须稳定地翻转到负方向，同时保持右手姿态矩阵；
    // 这直接决定喷涂、检测和贴合等工艺是从工件正面还是反面接近。
    using namespace rw::kinematics;
    using namespace rw::math;
    StateStructure::Ptr structure = rw::core::ownedPtr(new StateStructure());
    const Frame::Ptr target = rw::core::ownedPtr(new FixedFrame("SurfaceNormal", Transform3D<>()));
    structure->addFrame(target, structure->getRoot());
    const rw::models::WorkCell::Ptr workcell = rw::core::ownedPtr(
        new rw::models::WorkCell(structure, "OrientationInvertWorkCell", ""));

    for (const rws::OrientationMode mode : {rws::OrientationMode::AlignFrame,
                                            rws::OrientationMode::AlignGeometryNormal}) {
        rws::KeyStation station;
        station.refFrame = "WORLD";
        station.orientation.mode = mode;
        station.orientation.targetFrame = "SurfaceNormal";
        station.orientation.invertNormal = true;

        std::string error;
        REQUIRE(rws::OrientationRuleResolver::applyToStation(
            station, *workcell, workcell->getDefaultState(), &error));
        const Rotation3D<> rotation = RPY<>(station.rpyDeg[0] * Pi / 180.0,
                                            station.rpyDeg[1] * Pi / 180.0,
                                            station.rpyDeg[2] * Pi / 180.0)
                                          .toRotation3D();
        REQUIRE(std::abs(rotation.getCol(2)[0]) < 1e-9);
        REQUIRE(std::abs(rotation.getCol(2)[1]) < 1e-9);
        REQUIRE(std::abs(rotation.getCol(2)[2] + 1.0) < 1e-9);
        REQUIRE(station.orientation.resolutionEvidence.find("invertNormal=true") != std::string::npos);
    }
    return 0;
}

int testFrozenArtifactRoundTripRetainsCompiledEvidence()
{
    // 冻结工件必须保存编译后的任务、诊断和环境指纹；仅保存编辑态 RequirementSet
    // 会让 Should 工位的排除结果在重新打开项目后丢失，破坏审计可复现性。
    rws::FrozenRequirementArtifact artifact;
    artifact.schemaVersion = 3;
    artifact.requirementFingerprint = "requirement-sha256";
    artifact.environmentFingerprint = "environment-sha256";
    artifact.workcellFingerprint = "workcell-sha256";
    // 冻结工件中的已编译快照仍须满足编译器的模型绑定契约。这里显式填入
    // 指纹，验证 JSON 往返不会把这项下游可追溯信息遗漏或重置为空字符串。
    artifact.modelBinding.robotModelFingerprint = "model-fingerprint";
    artifact.compiled.frozen = true;
    artifact.compiled.requirementFingerprint = artifact.requirementFingerprint;
    artifact.compiled.modelBinding = artifact.modelBinding;
    rws::CompiledPoseTask station;
    station.id = "compiled_pick";
    station.name = "Compiled pick";
    // 编译快照中的任务来自已通过校验的冻结结果，因此测试样本同样应携带
    // 规范化后的参考系；空参考系属于编辑态非法输入，不能代表可审计工件。
    station.refFrame = "WORLD";
    station.tcpFrame = "ToolTCP";
    station.orientation.mode = rws::OrientationMode::PointAtTarget;
    station.orientation.targetPoint = "0.2, 0.3, 0.4";
    station.orientation.resolutionEvidence =
        "resolver=OrientationRuleResolver.1;mode=PointAtTarget;targetPoint=0.2, 0.3, 0.4";
    artifact.compiled.poseTasks.push_back(station);
    artifact.compiled.diagnostics.push_back(
        {"optional_missing", rws::RequirementLevel::Should, "Excluded from frozen artifact.", false});

    // 场景快照是跨插件交接非 WORLD 工位的最小可复现依据：它既保留冻结时的场景
    // 定义，也将源文件版本、设备标识和状态指纹纳入审计。此处先以独立字段断言
    // JSON 往返契约，防止后续增加候选场景工厂时静默丢失这些关键证据。
    artifact.scenario.sourceWorkCellPath = "D:/demo/cell.wc.xml";
    artifact.scenario.sourceFileFingerprint = "scene-file-sha256";
    artifact.scenario.snapshotFingerprint = "scene-snapshot-sha256";
    artifact.scenario.deviceName = "FreezeRobot";
    artifact.scenario.environmentFingerprint = artifact.environmentFingerprint;
    artifact.scenario.stateFingerprint = artifact.workcellFingerprint;
    artifact.scenario.sceneSpec.robotName = "FrozenScene";
    artifact.scenario.sceneSpec.saveDirectory = "D:/demo";
    artifact.frozenRobotState.deviceName = "FreezeRobot";
    artifact.frozenRobotState.tcpFrameName = "FreezeTcp";
    artifact.frozenRobotState.kinematicFingerprint = "robot-kinematic-sha256";
    artifact.frozenRobotState.q.push_back(0.0);
    artifact.frozenRobotState.tcpWorldPose[15] = 1.0;
    artifact.frozenRobotState.capturedAt = "2026-07-31T00:00:00.000Z";

    // 项目文件需要把冻结工件嵌入编辑态需求 JSON，因此读写器必须提供对象级
    // 接口，而不仅是独立字符串 API；两种入口应还原同一份可审计编译结果。
    const QJsonObject object = rws::FrozenRequirementArtifactJson::toObject(artifact);
    rws::FrozenRequirementArtifact objectRestored;
    std::string objectError;
    REQUIRE(rws::FrozenRequirementArtifactJson::fromObject(object, objectRestored, &objectError));
    REQUIRE(objectRestored.compiled.poseTasks.size() == 1);
    REQUIRE(objectRestored.compiled.poseTasks[0].orientation.resolutionEvidence ==
            station.orientation.resolutionEvidence);
    REQUIRE(objectRestored.schemaVersion == 3);
    REQUIRE(objectRestored.scenario.sourceWorkCellPath == "D:/demo/cell.wc.xml");
    REQUIRE(objectRestored.scenario.sourceFileFingerprint == "scene-file-sha256");
    REQUIRE(objectRestored.scenario.snapshotFingerprint == "scene-snapshot-sha256");
    REQUIRE(objectRestored.scenario.deviceName == "FreezeRobot");

    const std::string json = rws::FrozenRequirementArtifactJson::toJson(artifact);
    rws::FrozenRequirementArtifact restored;
    std::string error;
    REQUIRE(rws::FrozenRequirementArtifactJson::fromJson(json, restored, &error));
    REQUIRE(restored.compiled.frozen);
    REQUIRE(restored.requirementFingerprint == "requirement-sha256");
    REQUIRE(restored.workcellFingerprint == "workcell-sha256");
    REQUIRE(restored.compiled.poseTasks.size() == 1);
    REQUIRE(restored.compiled.diagnostics.size() == 1);
    REQUIRE(restored.compiled.poseTasks[0].orientation.resolutionEvidence ==
            station.orientation.resolutionEvidence);
    REQUIRE(restored.schemaVersion == 3);
    REQUIRE(restored.scenario.sceneSpec.robotName == "FrozenScene");
    return 0;
}

int testFrozenArtifactBecomesStaleWhenWorkCellStateChanges()
{
    // 冻结证据必须绑定到实际 WorkCell 的空间状态。此处只移动一个工装 Frame，
    // 需求和模型都不改动，验证环境指纹仍可识别出工件已经不再适用于当前场景。
    rws::RobotModelSpec model;
    model.robotName = "ArtifactStateRobot";
    rws::RequirementSet requirements;
    requirements.modelBinding.robotName = model.robotName;
    requirements.modelBinding.robotModelFingerprint = rws::RobotModelFingerprint::canonicalSha256(model);

    rw::kinematics::StateStructure::Ptr structure = rw::core::ownedPtr(new rw::kinematics::StateStructure());
    const rw::kinematics::FixedFrame::Ptr base = rw::core::ownedPtr(
        new rw::kinematics::FixedFrame("ArtifactBase", rw::math::Transform3D<>()));
    const rw::models::RevoluteJoint::Ptr joint = rw::core::ownedPtr(
        new rw::models::RevoluteJoint("ArtifactJoint", rw::math::Transform3D<>()));
    const rw::kinematics::MovableFrame::Ptr tcp = rw::core::ownedPtr(
        new rw::kinematics::MovableFrame("ArtifactTcp"));
    structure->addFrame(base, structure->getRoot());
    structure->addFrame(joint, base);
    structure->addFrame(tcp, joint);
    const rw::models::WorkCell::Ptr workcell = rw::core::ownedPtr(
        new rw::models::WorkCell(structure, "ArtifactStateWorkCell", ""));
    const rw::models::SerialDevice::Ptr device = rw::core::ownedPtr(
        new rw::models::SerialDevice(base.get(), tcp.get(), model.robotName,
                                     structure->getDefaultState()));
    workcell->addDevice(device);
    const rw::kinematics::MovableFrame::Ptr fixture = rw::core::ownedPtr(
        new rw::kinematics::MovableFrame("ArtifactFixture"));
    workcell->addFrame(fixture, workcell->getWorldFrame());
    const rw::kinematics::State frozenState = workcell->getDefaultState();

    rws::FrozenRequirementArtifact artifact;
    std::string error;
    REQUIRE(rws::RequirementFreezer::freeze(requirements, *workcell, frozenState, model, artifact, &error));
    REQUIRE(rws::RequirementFreezer::isCurrent(artifact, requirements, *workcell, frozenState, model, &error));

    rw::kinematics::State changedState = frozenState;
    device->setQ(rw::math::Q(1, 0.35), changedState);
    REQUIRE(rws::RequirementFreezer::isCurrent(artifact, requirements, *workcell, changedState, model, &error));

    fixture->setTransform(rw::math::Transform3D<>(rw::math::Vector3D<>(0.05, 0.0, 0.0)), changedState);
    REQUIRE(!rws::RequirementFreezer::isCurrent(artifact, requirements, *workcell, changedState, model, &error));
    REQUIRE(error.find("Fixture or external environment") != std::string::npos);

    rw::kinematics::State changedTcpState = frozenState;
    tcp->setTransform(rw::math::Transform3D<>(rw::math::Vector3D<>(0.01, 0.0, 0.0)), changedTcpState);
    REQUIRE(!rws::RequirementFreezer::isCurrent(artifact, requirements, *workcell, changedTcpState, model, &error));
    REQUIRE(error.find("Robot model or TCP configuration") != std::string::npos);
    return 0;
}

int testFrozenArtifactBecomesStaleWhenSourceWorkCellFileChanges()
{
    // 真实工程中最危险的情况是文件路径未变、工装 XML 却被替换。场景快照的源文件
    // SHA-256 必须在这种情况下使冻结工件失效，不能仅依赖内存中仍未重新加载的 Frame。
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const QString workcellPath = directory.filePath("frozen-cell.wc.xml");
    QFile sourceFile(workcellPath);
    REQUIRE(sourceFile.open(QIODevice::WriteOnly | QIODevice::Text));
    REQUIRE(sourceFile.write("<WorkCell name=\"FrozenCell\" />\n") > 0);
    sourceFile.close();

    rws::RobotModelSpec model;
    model.robotName = "ArtifactSourceRobot";
    rws::RequirementSet requirements;
    requirements.modelBinding.robotName = model.robotName;
    requirements.modelBinding.robotModelFingerprint = rws::RobotModelFingerprint::canonicalSha256(model);
    rw::kinematics::StateStructure::Ptr structure = rw::core::ownedPtr(new rw::kinematics::StateStructure());
    const rw::kinematics::FixedFrame::Ptr base = rw::core::ownedPtr(
        new rw::kinematics::FixedFrame("ArtifactSourceBase", rw::math::Transform3D<>()));
    const rw::models::RevoluteJoint::Ptr joint = rw::core::ownedPtr(
        new rw::models::RevoluteJoint("ArtifactSourceJoint", rw::math::Transform3D<>()));
    const rw::kinematics::FixedFrame::Ptr tcp = rw::core::ownedPtr(
        new rw::kinematics::FixedFrame("ArtifactSourceTcp", rw::math::Transform3D<>()));
    structure->addFrame(base, structure->getRoot());
    structure->addFrame(joint, base);
    structure->addFrame(tcp, joint);
    const rw::models::WorkCell::Ptr workcell = rw::core::ownedPtr(
        new rw::models::WorkCell(structure, "ArtifactSourceWorkCell", workcellPath.toStdString()));
    const rw::models::SerialDevice::Ptr device = rw::core::ownedPtr(
        new rw::models::SerialDevice(base.get(), tcp.get(), model.robotName,
                                     structure->getDefaultState()));
    workcell->addDevice(device);

    rws::FrozenRequirementArtifact artifact;
    std::string error;
    REQUIRE(rws::RequirementFreezer::freeze(requirements, *workcell,
                                               workcell->getDefaultState(), model, artifact, &error));
    REQUIRE(artifact.schemaVersion == 3);
    REQUIRE(!artifact.scenario.sourceFileFingerprint.empty());
    REQUIRE(rws::RequirementFreezer::isCurrent(artifact, requirements, *workcell,
                                                 workcell->getDefaultState(), model, &error));

    REQUIRE(sourceFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate));
    REQUIRE(sourceFile.write("<WorkCell name=\"FrozenCellChanged\" />\n") > 0);
    sourceFile.close();
    REQUIRE(!rws::RequirementFreezer::isCurrent(artifact, requirements, *workcell,
                                                  workcell->getDefaultState(), model, &error));
    REQUIRE(error.find("source WorkCell file") != std::string::npos);
    return 0;
}

int testTemplateGenerationKeepsTraceabilityAndUniqueIds()
{
    rws::RequirementSet requirements;
    rws::PoseTask existing;
    existing.id = "bin_pick_1";
    requirements.poseTasks.push_back(existing);

    rws::StationTemplateRequest request;
    request.kind = rws::StationTemplateKind::BinPicking;
    request.instanceId = "bin_A";
    request.idPrefix = "bin_pick";
    request.namePrefix = "Bin A";
    request.referenceFrame = "Bin_A";
    request.tcpFrame = "ToolTCP";
    request.rows = 2;
    request.columns = 3;
    request.rowSpacingMeters = 0.06;
    request.columnSpacingMeters = 0.08;
    request.approachDistanceMeters = 0.12;

    std::string error;
    REQUIRE(rws::StationTemplateService::appendTemplate(requirements, request, &error));
    REQUIRE(error.empty());
    REQUIRE(requirements.poseTasks.size() == 7);

    std::set<std::string> ids;
    int generatedCount = 0;
    for (const rws::PoseTask& station : requirements.poseTasks) {
        REQUIRE(ids.insert(station.id).second);
        if (station.generation.instanceId != "bin_A") continue;
        ++generatedCount;
        REQUIRE(station.source == rws::PoseTaskSource::Template);
        REQUIRE(station.generation.generatorId == "BinPicking.v1");
        REQUIRE(station.generation.linked);
        REQUIRE(station.processType == rws::ProcessType::Pick);
        REQUIRE(station.refFrame == "Bin_A");
        REQUIRE(station.tcpFrame == "ToolTCP");
        REQUIRE(station.approach.enabled);
        REQUIRE(std::abs(station.approach.distanceMeters - 0.12) < 1e-12);
        const auto findParameter = [&station] (const std::string& key) {
            return std::find_if(station.generation.parameters.begin(), station.generation.parameters.end(),
                                [&key] (const rws::GenerationParameter& parameter) {
                                    return parameter.key == key;
                                });
        };
        const auto idPrefix = findParameter("idPrefix");
        const auto namePrefix = findParameter("namePrefix");
        const auto offsetX = findParameter("operationOffsetX");
        REQUIRE(idPrefix != station.generation.parameters.end());
        REQUIRE(namePrefix != station.generation.parameters.end());
        REQUIRE(offsetX != station.generation.parameters.end());
        REQUIRE(idPrefix->value == "bin_pick");
        REQUIRE(namePrefix->value == "Bin A");
        REQUIRE(offsetX->value == "0");
    }
    REQUIRE(generatedCount == 6);
    return 0;
}

int testTemplateUpdatePreservesDetachedStations()
{
    rws::RequirementSet requirements;
    rws::StationTemplateRequest request;
    request.kind = rws::StationTemplateKind::BinPicking;
    request.instanceId = "bin_A";
    request.idPrefix = "bin_pick";
    request.referenceFrame = "Bin_A";
    request.tcpFrame = "ToolTCP";
    request.rows = 2;
    request.columns = 2;

    std::string error;
    REQUIRE(rws::StationTemplateService::appendTemplate(requirements, request, &error));
    REQUIRE(requirements.poseTasks.size() == 4);
    const std::string detachedId = requirements.poseTasks.front().id;
    requirements.poseTasks.front().name = "Hand tuned bin point";
    REQUIRE(rws::StationTemplateService::detachStation(requirements, detachedId, &error));

    request.rows = 1;
    request.columns = 2;
    rws::TemplateUpdatePreview preview;
    REQUIRE(rws::StationTemplateService::previewTemplateUpdate(requirements, "bin_A", request,
                                                                preview, &error));
    REQUIRE(preview.replacedStationIds.size() == 3);
    REQUIRE(preview.generatedStations.size() == 2);
    REQUIRE(rws::StationTemplateService::applyTemplateUpdate(requirements, preview, &error));
    REQUIRE(requirements.poseTasks.size() == 3);

    bool foundDetached = false;
    int linkedCount = 0;
    for (const rws::PoseTask& station : requirements.poseTasks) {
        if (station.id == detachedId) {
            foundDetached = true;
            REQUIRE(!station.generation.linked);
            REQUIRE(station.name == "Hand tuned bin point");
        }
        if (station.generation.instanceId == "bin_A" && station.generation.linked)
            ++linkedCount;
    }
    REQUIRE(foundDetached);
    REQUIRE(linkedCount == 2);
    return 0;
}

int testRectangularArrayRecordsGenerationAndDoesNotDuplicateIds()
{
    rws::RequirementSet requirements;
    rws::PoseTask source;
    source.id = "inspection";
    source.name = "Inspection";
    source.processType = rws::ProcessType::Inspect;
    source.refFrame = "Fixture_A";
    source.tcpFrame = "ToolTCP";
    requirements.poseTasks.push_back(source);

    rws::StationArrayRequest request;
    request.kind = rws::StationArrayKind::Rectangular;
    request.instanceId = "inspection_grid";
    request.idPrefix = "inspection";
    request.namePrefix = "Inspection grid";
    request.primaryCount = 2;
    request.secondaryCount = 3;
    request.primaryStepMeters = {{0.10, 0.0, 0.0}};
    request.secondaryStepMeters = {{0.0, 0.05, 0.0}};

    std::string error;
    REQUIRE(rws::StationTemplateService::appendArray(requirements, source.id, request, &error));
    REQUIRE(requirements.poseTasks.size() == 7);
    std::set<std::string> ids;
    int generatedCount = 0;
    for (const rws::PoseTask& station : requirements.poseTasks) {
        REQUIRE(ids.insert(station.id).second);
        if (station.generation.instanceId != "inspection_grid") continue;
        ++generatedCount;
        REQUIRE(station.generation.generatorId == "RectangularArray.v1");
        REQUIRE(!station.generation.linked);
        REQUIRE(station.processType == rws::ProcessType::Inspect);
    }
    REQUIRE(generatedCount == 6);
    return 0;
}

int testPolylineArrayDistributesStationsAtEqualArcLength()
{
    // 折线包含直角转弯，用于验证采样依据累计弧长而不是逐段平均分配。
    rws::RequirementSet requirements;
    rws::PoseTask source;
    source.id = "scan";
    source.name = "Scan";
    source.processType = rws::ProcessType::Inspect;
    source.refFrame = "Fixture_A";
    source.tcpFrame = "ToolTCP";
    requirements.poseTasks.push_back(source);

    rws::StationArrayRequest request;
    request.kind = rws::StationArrayKind::Polyline;
    request.instanceId = "scan_curve";
    request.idPrefix = "scan_curve";
    request.namePrefix = "Scan curve";
    request.primaryCount = 5;
    request.polylinePointsMeters = {{{{0.0, 0.0, 0.0}}, {{1.0, 0.0, 0.0}}, {{1.0, 1.0, 0.0}}}};

    std::string error;
    REQUIRE(rws::StationTemplateService::appendArray(requirements, source.id, request, &error));
    REQUIRE(requirements.poseTasks.size() == 6);
    const std::array<std::array<double, 3>, 5> expected = {{{{0.0, 0.0, 0.0}}, {{0.5, 0.0, 0.0}},
        {{1.0, 0.0, 0.0}}, {{1.0, 0.5, 0.0}}, {{1.0, 1.0, 0.0}}}};
    for (std::size_t index = 0; index < expected.size(); ++index) {
        const rws::PoseTask& station = requirements.poseTasks[index + 1];
        REQUIRE(station.generation.generatorId == "PolylineArray.v1");
        for (int axis = 0; axis < 3; ++axis)
            REQUIRE(std::abs(station.position[axis] - expected[index][axis]) < 1e-12);
    }
    return 0;
}

int testStationImportsAreAtomicAndRetainRecordProvenance()
{
    // 第一段导入验证来源信息；第二段故意保留坏数值，确认失败不会改变已存在需求。
    rws::RequirementSet requirements;
    rws::PoseTask existing;
    existing.id = "existing";
    requirements.poseTasks.push_back(existing);
    const std::string csv =
        "id,name,refFrame,tcpFrame,x,y,z,roll,pitch,yaw,level,processType\r\n"
        "inspect_1,Inspection A,Fixture_A,ToolTCP,0.1,0.2,0.3,0,0,90,Must,Inspect\r\n";
    rws::StationImportResult result;
    std::string error;
    REQUIRE(rws::StationImportService::appendCsv(requirements, csv, "inspection.csv", result, &error));
    REQUIRE(result.diagnostics.empty());
    REQUIRE(requirements.poseTasks.size() == 2);
    const rws::PoseTask& imported = requirements.poseTasks.back();
    REQUIRE(imported.source == rws::PoseTaskSource::Imported);
    REQUIRE(imported.importProvenance.sourcePath == "inspection.csv");
    REQUIRE(imported.importProvenance.recordNumber == 2);
    REQUIRE(imported.processType == rws::ProcessType::Inspect);
    REQUIRE(std::abs(imported.rpyDeg[2] - 90.0) < 1e-12);

    const std::size_t beforeInvalidImport = requirements.poseTasks.size();
    const std::string invalidCsv =
        "id,name,refFrame,tcpFrame,x,y,z,roll,pitch,yaw,level,processType\n"
        "inspect_2,Inspection B,Fixture_A,ToolTCP,not-a-number,0.2,0.3,0,0,90,Must,Inspect\n";
    REQUIRE(!rws::StationImportService::appendCsv(requirements, invalidCsv, "invalid.csv", result, &error));
    REQUIRE(requirements.poseTasks.size() == beforeInvalidImport);
    REQUIRE(result.diagnostics.size() == 1);
    REQUIRE(result.diagnostics.front().recordNumber == 2);

    const std::string json =
        "{\"stations\":[{\"id\":\"handover_1\",\"name\":\"Handover\",\"refFrame\":\"WORLD\","
        "\"tcpFrame\":\"ToolTCP\",\"position\":[0.4,0.0,0.2],\"rpyDeg\":[0,0,180],"
        "\"level\":\"Should\",\"processType\":\"Handover\"}]}";
    REQUIRE(rws::StationImportService::appendJson(requirements, json, "handover.json", result, &error));
    REQUIRE(requirements.poseTasks.size() == 3);
    REQUIRE(requirements.poseTasks.back().importProvenance.recordNumber == 1);
    REQUIRE(requirements.poseTasks.back().level == rws::RequirementLevel::Should);
    // 审计来源不能只存在于内存：保存并重新加载需求集后仍应能追溯到原始 CSV 记录。
    rws::RequirementSet restored;
    REQUIRE(rws::RequirementSetJson::fromJson(rws::RequirementSetJson::toJson(requirements), restored, &error));
    REQUIRE(restored.poseTasks.size() == 3);
    REQUIRE(restored.poseTasks[1].importProvenance.sourcePath == "inspection.csv");
    REQUIRE(restored.poseTasks[1].importProvenance.recordNumber == 2);
    return 0;
}

int testRequirementSetUndoRestoresTheSnapshotBeforeBatchOperation()
{
    // 批量操作的撤销必须回到完整的操作前快照，而不是仅删除最后一个工位。
    rws::RequirementSet requirements;
    rws::PoseTask source;
    source.id = "source";
    requirements.poseTasks.push_back(source);
    rws::RequirementSetUndoStack undo;
    undo.pushSnapshot(requirements);
    requirements.poseTasks.push_back(rws::PoseTask());
    requirements.poseTasks.back().id = "generated";
    REQUIRE(undo.canUndo());
    REQUIRE(undo.undo(requirements));
    REQUIRE(requirements.poseTasks.size() == 1);
    REQUIRE(requirements.poseTasks.front().id == "source");
    REQUIRE(!undo.canUndo());
    REQUIRE(undo.canRedo());
    REQUIRE(undo.redo(requirements));
    REQUIRE(requirements.poseTasks.size() == 2);
    return 0;
}

int testGeneratedStationJsonRoundTripPreservesProvenance()
{
    rws::RequirementSet requirements;
    rws::PoseTask station;
    station.id = "handover_1";
    station.source = rws::PoseTaskSource::Template;
    station.generation.generatorId = "Handover.v1";
    station.generation.instanceId = "handover_A";
    station.generation.linked = true;
    station.generation.parameters.push_back({"clearanceMeters", "0.15"});
    requirements.poseTasks.push_back(station);

    rws::RequirementSet restored;
    std::string error;
    REQUIRE(rws::RequirementSetJson::fromJson(rws::RequirementSetJson::toJson(requirements), restored, &error));
    REQUIRE(restored.poseTasks.size() == 1);
    REQUIRE(restored.poseTasks.front().generation.generatorId == "Handover.v1");
    REQUIRE(restored.poseTasks.front().generation.instanceId == "handover_A");
    REQUIRE(restored.poseTasks.front().generation.linked);
    REQUIRE(restored.poseTasks.front().generation.parameters.size() == 1);
    REQUIRE(restored.poseTasks.front().generation.parameters.front().key == "clearanceMeters");
    return 0;
}

int testTemplateParameterVisibilityMatchesProcessSemantics()
{
    const unsigned int binPicking = rws::templateParameterVisibilityMask(rws::StationTemplateKind::BinPicking);
    REQUIRE((binPicking & TemplateParameterRows) != 0U);
    REQUIRE((binPicking & TemplateParameterColumns) != 0U);
    REQUIRE((binPicking & TemplateParameterLayers) != 0U);
    REQUIRE((binPicking & TemplateParameterRowSpacing) != 0U);
    REQUIRE((binPicking & TemplateParameterColumnSpacing) != 0U);
    REQUIRE((binPicking & TemplateParameterLayerSpacing) != 0U);
    REQUIRE((binPicking & (TemplateParameterApproach | TemplateParameterRetract | TemplateParameterClearance)) == 0U);

    const unsigned int machineTending = rws::templateParameterVisibilityMask(rws::StationTemplateKind::MachineTending);
    REQUIRE((machineTending & (TemplateParameterRows | TemplateParameterColumns | TemplateParameterLayers)) == 0U);
    REQUIRE((machineTending & (TemplateParameterApproach | TemplateParameterRetract | TemplateParameterClearance)) ==
            (TemplateParameterApproach | TemplateParameterRetract | TemplateParameterClearance));

    const unsigned int palletizing = rws::templateParameterVisibilityMask(rws::StationTemplateKind::Palletizing);
    REQUIRE((palletizing & (TemplateParameterRows | TemplateParameterColumns | TemplateParameterLayers |
                             TemplateParameterRowSpacing |
                             TemplateParameterColumnSpacing | TemplateParameterLayerSpacing)) ==
            (TemplateParameterRows | TemplateParameterColumns | TemplateParameterLayers |
             TemplateParameterRowSpacing |
             TemplateParameterColumnSpacing | TemplateParameterLayerSpacing));

    for (const rws::StationTemplateKind kind : {rws::StationTemplateKind::Inspection,
                                                 rws::StationTemplateKind::ToolChange,
                                                 rws::StationTemplateKind::Handover}) {
        REQUIRE(rws::templateParameterVisibilityMask(kind) == 0U);
    }
    return 0;
}

int testPalletizingTemplateUsesConfiguredGridDimensions()
{
    // 码垛需求的行、列、层数是工艺输入，服务层不得用固定 2x2 网格替代用户配置。
    rws::RequirementSet requirements;
    rws::StationTemplateRequest request;
    request.kind = rws::StationTemplateKind::Palletizing;
    request.instanceId = "pallet_A";
    request.rows = 3;
    request.columns = 2;
    request.layers = 2;
    std::string error;
    REQUIRE(rws::StationTemplateService::appendTemplate(requirements, request, &error));
    REQUIRE(requirements.poseTasks.size() == 12);
    return 0;
}

int testWidgetBuildsEngineeringRequirementWorkflow()
{
    rws::EngineeringRequirementsWidget widget;
    QTabWidget* tabs = widget.findChild<QTabWidget*>("engineeringRequirementsTabs");
    REQUIRE(tabs != nullptr);
    REQUIRE(tabs->count() == 3);
    REQUIRE(tabs->tabText(0) == QString::fromUtf8("关键工位"));
    REQUIRE(widget.findChild<QPushButton*>("addRequirementPoseTaskButton") != nullptr);
    REQUIRE(widget.findChild<QPushButton*>("addRequirementBoxRegionButton") != nullptr);
    REQUIRE(widget.findChild<QPushButton*>("freezeRequirementSetButton") != nullptr);
    REQUIRE(widget.findChild<QPushButton*>("saveRequirementSetButton") != nullptr);
    return 0;
}

int testWidgetExposesSemanticKeyStationInspector()
{
    rws::EngineeringRequirementsWidget widget;
    REQUIRE(widget.findChild<QWidget*>("keyStationList") != nullptr);
    REQUIRE(widget.findChild<QWidget*>("keyStationProcessTypeCombo") != nullptr);
    REQUIRE(widget.findChild<QWidget*>("keyStationOrientationModeCombo") != nullptr);
    REQUIRE(widget.findChild<QWidget*>("keyStationReferenceFrameCombo") != nullptr);
    REQUIRE(widget.findChild<QWidget*>("keyStationTcpFrameCombo") != nullptr);
    REQUIRE(widget.findChild<QWidget*>("keyStationApproachEnabled") != nullptr);
    REQUIRE(widget.findChild<QWidget*>("keyStationRetractEnabled") != nullptr);
    REQUIRE(widget.findChild<QWidget*>("keyStationAdvancedPoseGroup") != nullptr);
    REQUIRE(widget.findChild<QPushButton*>("pickRequirementGeometryFeatureButton") != nullptr);
    REQUIRE(widget.findChild<QPushButton*>("createRequirementTemplateButton") != nullptr);
    REQUIRE(widget.findChild<QPushButton*>("updateRequirementTemplateButton") != nullptr);
    REQUIRE(widget.findChild<QPushButton*>("detachRequirementTemplateButton") != nullptr);
    REQUIRE(widget.findChild<QPushButton*>("createRequirementArrayButton") != nullptr);
    REQUIRE(widget.findChild<QPushButton*>("mirrorRequirementStationButton") != nullptr);
    REQUIRE(widget.findChild<QPushButton*>("importRequirementStationsButton") != nullptr);
    REQUIRE(widget.findChild<QPushButton*>("undoRequirementOperationButton") != nullptr);
    REQUIRE(widget.findChild<QPushButton*>("redoRequirementOperationButton") != nullptr);
    return 0;
}

int testWidgetProjectDocumentSnapshotTracksRequirementEdits()
{
    rws::EngineeringRequirementsWidget widget;
    QTemporaryDir projectDirectory;
    REQUIRE(projectDirectory.isValid());
    const QString projectDocument = projectDirectory.filePath("requirements.json");
    QString error;

    // 项目 Provider 调用 Widget 的无对话框入口。先保存并确认干净，再通过真实按钮
    // 增加工位触发领域变更，最后重新加载资源，验证快照不会被普通导入导出路径污染。
    REQUIRE(widget.saveProjectDocument(projectDocument, &error));
    // rwproj 的正常打开流程会先通过 Provider 调用 loadProjectDocument；该步骤建立
    // 资源路径和初始快照，单独导出文件本身不能替代“打开项目”。
    REQUIRE(widget.loadProjectDocument(projectDocument, &error));
    REQUIRE(!widget.isProjectDocumentDirty());
    QPushButton* addPoseTask = widget.findChild<QPushButton*>("addRequirementPoseTaskButton");
    REQUIRE(addPoseTask != nullptr);
    addPoseTask->click();
    REQUIRE(widget.isProjectDocumentDirty());
    REQUIRE(widget.loadProjectDocument(projectDocument, &error));
    REQUIRE(!widget.isProjectDocumentDirty());
    return 0;
}

int testWidgetResolvesGeometryFeatureUsingLatestJogState()
{
    // 构造一个可在 State 中移动的工装 Frame。默认状态保持原点，而模拟的
    // JOG 状态将其移到 X=0.42m；两者不同才能检验 UI 没有误用默认状态。
    rw::kinematics::StateStructure::Ptr structure = rw::core::ownedPtr(new rw::kinematics::StateStructure());
    const rw::models::WorkCell::Ptr workcell = rw::core::ownedPtr(
        new rw::models::WorkCell(structure, "JogStateWorkCell", ""));
    const rw::kinematics::MovableFrame::Ptr fixture = rw::core::ownedPtr(
        new rw::kinematics::MovableFrame("FixtureFrame"));
    workcell->addFrame(fixture, workcell->getWorldFrame());
    rw::kinematics::State jogState = workcell->getDefaultState();
    fixture->setTransform(rw::math::Transform3D<>(rw::math::Vector3D<>(0.42, 0.0, 0.0)), jogState);

    rws::EngineeringRequirementsWidget widget;
    widget.setWorkCell(workcell.get());
    widget.setCurrentState(jogState);
    widget.findChild<QPushButton*>("addRequirementPoseTaskButton")->click();

    QString error;
    REQUIRE(widget.applyGeometryFeatureFrame("FixtureFrame", &error));
    const rws::RequirementSet requirements = widget.requirementSet();
    REQUIRE(requirements.poseTasks.size() == 1);
    REQUIRE(std::fabs(requirements.poseTasks[0].position[0] - 0.42) < 1e-9);
    return 0;
}

// 正向/负向测试：需求冻结前能从项目 generated/robot-models 自动绑定与设备名称匹配的
// 唯一工程模型（含内容指纹）；出现多个模型时不得按文件名猜测，必须明确失败。
int testWidgetBindsMatchingGeneratedProjectModel()
{
    QTemporaryDir projectDirectory;
    REQUIRE(projectDirectory.isValid());
    const QString modelDirectory = projectDirectory.filePath("generated/robot-models");
    REQUIRE(QDir().mkpath(modelDirectory));

    rws::RobotModelSpec model;
    model.robotName = "ProjectRobot";
    const QString modelPath = modelDirectory + "/ProjectRobot.rmb.json";
    QFile modelFile(modelPath);
    REQUIRE(modelFile.open(QIODevice::WriteOnly | QIODevice::Text));
    REQUIRE(modelFile.write(rws::RobotModelSpecJson::toJson(model).c_str()) > 0);
    modelFile.close();

    rw::kinematics::StateStructure::Ptr structure = rw::core::ownedPtr(new rw::kinematics::StateStructure());
    const rw::kinematics::FixedFrame::Ptr base = rw::core::ownedPtr(
        new rw::kinematics::FixedFrame("ProjectBase", rw::math::Transform3D<>()));
    const rw::kinematics::MovableFrame::Ptr tcp = rw::core::ownedPtr(
        new rw::kinematics::MovableFrame("ProjectTcp"));
    structure->addFrame(base, structure->getRoot());
    structure->addFrame(tcp, base);
    const rw::models::WorkCell::Ptr workcell = rw::core::ownedPtr(
        new rw::models::WorkCell(structure, "ProjectWorkCell", ""));
    workcell->addDevice(rw::core::ownedPtr(new rw::models::SerialDevice(
        base.get(), tcp.get(), model.robotName, structure->getDefaultState())));

    rws::EngineeringRequirementsWidget widget;
    widget.setProjectOutputDirectory(projectDirectory.path());
    widget.setWorkCell(workcell.get());
    QString error;
    REQUIRE(widget.bindGeneratedProjectModel(&error));
    const rws::RequirementSet requirements = widget.requirementSet();
    REQUIRE(requirements.modelBinding.sourcePath == modelPath.toStdString());
    REQUIRE(requirements.modelBinding.robotName == model.robotName);
    REQUIRE(requirements.modelBinding.robotModelFingerprint ==
            rws::RobotModelFingerprint::canonicalSha256(model));

    // 方案 A 只支持一个当前工程模型。出现多个 sidecar 时不能根据文件名或目录顺序
    // 猜测，否则冻结可能绑定错误机器人。
    QFile secondModelFile(modelDirectory + "/AnotherRobot.rmb.json");
    REQUIRE(secondModelFile.open(QIODevice::WriteOnly | QIODevice::Text));
    REQUIRE(secondModelFile.write("{}") > 0);
    secondModelFile.close();
    rws::EngineeringRequirementsWidget ambiguousWidget;
    ambiguousWidget.setProjectOutputDirectory(projectDirectory.path());
    ambiguousWidget.setWorkCell(workcell.get());
    REQUIRE(!ambiguousWidget.bindGeneratedProjectModel(&error));
    REQUIRE(error.contains(QString::fromUtf8("多个")));
    REQUIRE(ambiguousWidget.requirementSet().modelBinding.sourcePath.empty());
    return 0;
}

int testWidgetPreservesBoxSamplingDensityWhenSynchronizing()
{
    // 覆盖率计算依赖每轴采样点数。工程师在表格中修改该值后，再执行新增、保存或冻结等会
    // 触发表格同步的操作时，数值必须保留，不能静默退回 BoxRegion 的默认值 5。
    rws::EngineeringRequirementsWidget widget;
    QPushButton* addRegion = widget.findChild<QPushButton*>("addRequirementBoxRegionButton");
    QTableWidget* regionTable = widget.findChild<QTableWidget*>("engineeringRequirementBoxTable");
    REQUIRE(addRegion != nullptr);
    REQUIRE(regionTable != nullptr);

    addRegion->click();
    REQUIRE(regionTable->rowCount() == 1);
    regionTable->item(0, 11)->setText("9");

    // 第二次新增会调用 syncTablesToRequirements()，可覆盖保存和冻结前的同一同步路径。
    addRegion->click();
    const rws::RequirementSet requirements = widget.requirementSet();
    REQUIRE(requirements.boxRegions.size() == 2);
    REQUIRE(requirements.boxRegions.front().samplesPerAxis == 9);
    return 0;
}

int testWidgetUndoRedoCoversRegularStationAndCoverageEdits()
{
    // 工程师最频繁的操作不是批量模板，而是新增工位、修改工位语义和调整覆盖盒采样密度。
    // 这些操作必须进入同一条撤销/重做历史，不能只对模板、阵列和导入提供可逆性。
    rws::EngineeringRequirementsWidget widget;
    QPushButton* addStation = widget.findChild<QPushButton*>("addRequirementPoseTaskButton");
    QPushButton* addRegion = widget.findChild<QPushButton*>("addRequirementBoxRegionButton");
    QPushButton* undo = widget.findChild<QPushButton*>("undoRequirementOperationButton");
    QPushButton* redo = widget.findChild<QPushButton*>("redoRequirementOperationButton");
    QLineEdit* stationName = widget.findChild<QLineEdit*>("keyStationNameEdit");
    QTableWidget* regionTable = widget.findChild<QTableWidget*>("engineeringRequirementBoxTable");
    REQUIRE(addStation != nullptr);
    REQUIRE(addRegion != nullptr);
    REQUIRE(undo != nullptr);
    REQUIRE(redo != nullptr);
    REQUIRE(stationName != nullptr);
    REQUIRE(regionTable != nullptr);

    addStation->click();
    REQUIRE(widget.requirementSet().poseTasks.size() == 1);
    undo->click();
    REQUIRE(widget.requirementSet().poseTasks.empty());
    redo->click();
    REQUIRE(widget.requirementSet().poseTasks.size() == 1);

    stationName->setText(QString::fromUtf8("装配作业位"));
    REQUIRE(QMetaObject::invokeMethod(stationName, "editingFinished"));
    REQUIRE(widget.requirementSet().poseTasks.front().name == QString::fromUtf8("装配作业位").toStdString());
    undo->click();
    REQUIRE(widget.requirementSet().poseTasks.front().name != QString::fromUtf8("装配作业位").toStdString());
    redo->click();
    REQUIRE(widget.requirementSet().poseTasks.front().name == QString::fromUtf8("装配作业位").toStdString());

    addRegion->click();
    REQUIRE(widget.requirementSet().boxRegions.size() == 1);
    regionTable->item(0, 11)->setText("9");
    REQUIRE(widget.requirementSet().boxRegions.front().samplesPerAxis == 9);
    undo->click();
    REQUIRE(widget.requirementSet().boxRegions.front().samplesPerAxis == 5);
    redo->click();
    REQUIRE(widget.requirementSet().boxRegions.front().samplesPerAxis == 9);
    return 0;
}

int testWidgetEditsPointAtTargetCoordinates()
{
    // 目标点必须有界面入口，不能要求研发工程师为了定义视觉/激光指向工位而手工编辑 JSON。
    // 同时验证属性编辑会写回需求模型，后续冻结才能调用已覆盖的 PointAtTarget 解析分支。
    rws::EngineeringRequirementsWidget widget;
    widget.findChild<QPushButton*>("addRequirementPoseTaskButton")->click();
    QComboBox* mode = widget.findChild<QComboBox*>("keyStationOrientationModeCombo");
    QLineEdit* targetPoint = widget.findChild<QLineEdit*>("keyStationOrientationTargetPointEdit");
    REQUIRE(mode != nullptr);
    REQUIRE(targetPoint != nullptr);
    mode->setCurrentIndex(mode->findData(static_cast<int>(rws::OrientationMode::PointAtTarget)));
    // 单元测试不显示顶层窗口，使用 isHidden() 验证模式切换是否主动隐藏该编辑器。
    REQUIRE(!targetPoint->isHidden());
    targetPoint->setText("0.25, -0.10, 0.40");
    REQUIRE(QMetaObject::invokeMethod(targetPoint, "editingFinished"));
    REQUIRE(widget.requirementSet().poseTasks.front().orientation.targetPoint == "0.25, -0.10, 0.40");
    return 0;
}

int testWidgetAlwaysShowsStationCoordinatesAndLocksRuleOrientation()
{
    rws::EngineeringRequirementsWidget widget;
    widget.findChild<QPushButton*>("addRequirementPoseTaskButton")->click();

    QGroupBox* coordinates = widget.findChild<QGroupBox*>("keyStationAdvancedPoseGroup");
    QComboBox* mode = widget.findChild<QComboBox*>("keyStationOrientationModeCombo");
    QDoubleSpinBox* x = widget.findChild<QDoubleSpinBox*>("keyStationX");
    QDoubleSpinBox* roll = widget.findChild<QDoubleSpinBox*>("keyStationRoll");
    REQUIRE(coordinates != nullptr);
    REQUIRE(mode != nullptr);
    REQUIRE(x != nullptr);
    REQUIRE(roll != nullptr);
    REQUIRE(coordinates->title() == QString::fromUtf8("高级坐标（工位坐标）"));
    REQUIRE(!coordinates->isHidden());
    REQUIRE(!x->isReadOnly());
    REQUIRE(!roll->isReadOnly());

    mode->setCurrentIndex(mode->findData(static_cast<int>(rws::OrientationMode::AlignFrame)));
    REQUIRE(!coordinates->isHidden());
    REQUIRE(!x->isReadOnly());
    REQUIRE(roll->isReadOnly());

    mode->setCurrentIndex(mode->findData(static_cast<int>(rws::OrientationMode::Fixed)));
    REQUIRE(!roll->isReadOnly());
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc > 1 && std::string(argv[1]) == "widget") {
        QApplication app(argc, argv);
        if (testWidgetBuildsEngineeringRequirementWorkflow() != 0)
            return 1;
        if (testWidgetProjectDocumentSnapshotTracksRequirementEdits() != 0)
            return 1;
        if (testWidgetExposesSemanticKeyStationInspector() != 0)
            return 1;
        if (testWidgetResolvesGeometryFeatureUsingLatestJogState() != 0)
            return 1;
        if (testWidgetBindsMatchingGeneratedProjectModel() != 0)
            return 1;
        if (testWidgetPreservesBoxSamplingDensityWhenSynchronizing() != 0)
            return 1;
        if (testWidgetUndoRedoCoversRegularStationAndCoverageEdits() != 0)
            return 1;
        if (testWidgetEditsPointAtTargetCoordinates() != 0)
            return 1;
        return testWidgetAlwaysShowsStationCoordinatesAndLocksRuleOrientation();
    }
    QCoreApplication app(argc, argv);
    (void)app;
    if (testFrozenRequirementCompilesOnlyEngineeringTasks() != 0)
        return 1;
    if (testJsonRoundTripPreservesBindingAndFrozenSnapshot() != 0)
        return 1;
    if (testFreezerRejectsMissingWorkCellTcpForMustStation() != 0)
        return 1;
    if (testFrozenArtifactRoundTripRetainsCompiledEvidence() != 0)
        return 1;
    if (testFrozenArtifactBecomesStaleWhenWorkCellStateChanges() != 0)
        return 1;
    if (testFrozenArtifactBecomesStaleWhenSourceWorkCellFileChanges() != 0)
        return 1;
    if (testKeyStationPersistsEngineeringIntentAndCompilesWorkPose() != 0)
        return 1;
    if (testCompilerKeepsNonBlockingStationDiagnosticsOutOfCompiledTasks() != 0)
        return 1;
    if (testGeometryFrameFeatureResolvesAndCompiles() != 0)
        return 1;
    if (testPointAtTargetResolvesCoordinateTextAndRejectsCoincidentTarget() != 0)
        return 1;
    if (testOrientationRuleInvertsToolZForFrameAndGeometryNormal() != 0)
        return 1;
    if (testTemplateGenerationKeepsTraceabilityAndUniqueIds() != 0)
        return 1;
    if (testTemplateUpdatePreservesDetachedStations() != 0)
        return 1;
    if (testRectangularArrayRecordsGenerationAndDoesNotDuplicateIds() != 0)
        return 1;
    if (testPolylineArrayDistributesStationsAtEqualArcLength() != 0)
        return 1;
    if (testStationImportsAreAtomicAndRetainRecordProvenance() != 0)
        return 1;
    if (testRequirementSetUndoRestoresTheSnapshotBeforeBatchOperation() != 0)
        return 1;
    if (testGeneratedStationJsonRoundTripPreservesProvenance() != 0)
        return 1;
    if (testTemplateParameterVisibilityMatchesProcessSemantics() != 0)
        return 1;
    if (testPalletizingTemplateUsesConfiguredGridDimensions() != 0)
        return 1;
    std::puts("All engineering requirements tests passed.");
    return 0;
}
