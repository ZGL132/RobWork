#include "EngineeringRequirementTypes.hpp"
#include "GeometryFeatureResolver.hpp"
#include "OrientationRuleResolver.hpp"
#include "RequirementCompiler.hpp"
#include "RequirementFreezer.hpp"
#include "RequirementMigration.hpp"
#include "RequirementSetJson.hpp"
#include "RequirementSetUndoStack.hpp"
#include "StationImportService.hpp"
#include "StationTemplateService.hpp"
#include "EngineeringRequirementsWidget.hpp"

#include <rwslibs/robotmodelbuilder/RobotModelXmlWriter.hpp>
#include <rwslibs/robotmodelbuilder/RobotModelFingerprint.hpp>
#include <rwslibs/robotmodelbuilder/RobotModelProjectPaths.hpp>
#include <rwslibs/robotmodelbuilder/RobotModelSpecJson.hpp>
#include <rwslibs/robotmodelbuilder/WorkCellConverter.hpp>

#include <rws/CallbackProjectDocumentProvider.hpp>
#include <rws/ProjectManager.hpp>
#include <rws/RobWorkStudio.hpp>

#include <rw/core/Ptr.hpp>
#include <rw/loaders/WorkCellLoader.hpp>
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
#include <QFileDialog>
#include <QGroupBox>
#include <QJsonObject>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QToolButton>
#include <QTableWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTimer>

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

int testHistoricalRequirementFreezerAbiRemainsLinkable()
{
    using Freeze = bool (*)(const rws::RequirementSet&, const rw::models::WorkCell&,
                            const rw::kinematics::State&, const rws::RobotModelSpec&,
                            rws::FrozenRequirementArtifact&, std::string*);
    using IsCurrent = bool (*)(const rws::FrozenRequirementArtifact&,
                               const rws::RequirementSet&, const rw::models::WorkCell&,
                               const rw::kinematics::State&, const rws::RobotModelSpec&,
                               std::string*);
    using IsScenarioCurrent = bool (*)(const rws::FrozenRequirementArtifact&,
                                       const rw::models::WorkCell&,
                                       const rw::kinematics::State&, std::string*);
    using ValidateScenario = bool (*)(const rws::FrozenRequirementArtifact&,
                                      const rw::models::WorkCell&,
                                      const rw::kinematics::State&,
                                      rws::FrozenRequirementValidationResult*, std::string*);

    const Freeze freeze = static_cast<Freeze>(&rws::RequirementFreezer::freeze);
    const IsCurrent isCurrent = static_cast<IsCurrent>(&rws::RequirementFreezer::isCurrent);
    const IsScenarioCurrent isScenarioCurrent =
        static_cast<IsScenarioCurrent>(&rws::RequirementFreezer::isScenarioCurrent);
    const ValidateScenario validateScenario =
        static_cast<ValidateScenario>(&rws::RequirementFreezer::validateScenario);

    REQUIRE(freeze != nullptr);
    REQUIRE(isCurrent != nullptr);
    REQUIRE(isScenarioCurrent != nullptr);
    REQUIRE(validateScenario != nullptr);
    return 0;
}

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
    region.tcpFrame = "TCP";
    region.center = {{0.45, 0.0, 0.35}};
    region.size = {{0.3, 0.2, 0.25}};
    region.minimumCoverage = 0.85;
    requirements.boxRegions.push_back(region);

    rws::CompiledRequirementSet compiled;
    std::string error;
    REQUIRE(rws::RequirementCompiler::compile(requirements, compiled, &error));
    REQUIRE(error.empty());
    REQUIRE(compiled.frozen);
    REQUIRE(compiled.poseTasks.size() == 3);
    REQUIRE(compiled.poseTasks[0].id == "station_pick");
    REQUIRE(compiled.poseTasks[1].level == rws::RequirementLevel::Should);
    REQUIRE(compiled.poseTasks[2].compileState == rws::RequirementCompileState::Excluded);
    REQUIRE(!compiled.poseTasks[2].excludedReason.empty());
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

int testRequirementSetJsonRejectsWrongScalarTypes()
{
    rws::RequirementSet requirements;
    requirements.modelBinding.robotModelFingerprint = "model-fingerprint";

    rws::PoseTask task;
    task.id = "strict-json";
    task.name = "Strict JSON";
    task.tcpFrame = "TCP";
    requirements.poseTasks.push_back(task);

    QJsonObject document = rws::RequirementSetJson::toObject(requirements);
    QJsonArray tasks = document.value("poseTasks").toArray();
    QJsonObject taskObject = tasks.at(0).toObject();
    taskObject["positionToleranceMeters"] = "not-a-number";
    tasks[0] = taskObject;
    document["poseTasks"] = tasks;

    rws::RequirementSet parsed;
    std::string error;
    REQUIRE(!rws::RequirementSetJson::fromObject(document, parsed, &error));
    REQUIRE(error.find("positionToleranceMeters") != std::string::npos);

    taskObject["positionToleranceMeters"] = 0.001;
    QJsonObject orientation = taskObject.value("orientation").toObject();
    orientation["mode"] = 7;
    taskObject["orientation"] = orientation;
    tasks[0] = taskObject;
    document["poseTasks"] = tasks;
    error.clear();
    REQUIRE(!rws::RequirementSetJson::fromObject(document, parsed, &error));
    REQUIRE(error.find("mode") != std::string::npos);
    return 0;
}

int testRequirementSetJsonPreservesUnknownFieldsInExtensions()
{
    rws::RequirementSet requirements;
    requirements.name = "Forward compatible requirements";
    requirements.modelBinding.robotModelFingerprint = "model-fingerprint";

    rws::PoseTask task;
    task.id = "future-task";
    task.name = "Future task";
    task.tcpFrame = "TCP";
    requirements.poseTasks.push_back(task);

    rws::BoxRegion region;
    region.id = "future-region";
    region.name = "Future region";
    region.tcpFrame = "TCP";
    requirements.boxRegions.push_back(region);

    QJsonObject document = rws::RequirementSetJson::toObject(requirements);
    document["futureTopLevel"] = QJsonObject{{"revision", 7}};
    QJsonArray tasks = document.value("poseTasks").toArray();
    QJsonObject taskObject = tasks.at(0).toObject();
    taskObject["futureTaskField"] = QJsonArray{1, 2, 3};
    tasks[0] = taskObject;
    document["poseTasks"] = tasks;
    QJsonArray regions = document.value("boxRegions").toArray();
    QJsonObject regionObject = regions.at(0).toObject();
    regionObject["futureRegionField"] = true;
    regions[0] = regionObject;
    document["boxRegions"] = regions;

    rws::RequirementSet parsed;
    std::string error;
    REQUIRE(rws::RequirementSetJson::fromObject(document, parsed, &error));
    const QJsonObject roundTripped = rws::RequirementSetJson::toObject(parsed);
    REQUIRE(roundTripped.value("extensions").toObject().value("futureTopLevel").toObject()
                .value("revision").toInt() == 7);
    REQUIRE(roundTripped.value("poseTasks").toArray().at(0).toObject()
                .value("extensions").toObject().value("futureTaskField").toArray().size() == 3);
    REQUIRE(roundTripped.value("boxRegions").toArray().at(0).toObject()
                .value("extensions").toObject().value("futureRegionField").toBool());

    QJsonObject conflictingExtensions = document;
    conflictingExtensions["extensions"] = QJsonObject{
        {"futureTopLevel", QJsonObject{{"revision", 8}}}};
    error.clear();
    REQUIRE(!rws::RequirementSetJson::fromObject(conflictingExtensions, parsed, &error));
    REQUIRE(error.find("conflicts") != std::string::npos);
    return 0;
}

int testCompilerReportsInvalidMustItemsAndClearsPreviousOutput()
{
    rws::RequirementSet requirements;
    requirements.modelBinding.robotModelFingerprint = "model-fingerprint";

    rws::PoseTask task;
    task.id = "must-invalid";
    task.name = "Must invalid";
    task.tcpFrame.clear();
    requirements.poseTasks.push_back(task);

    rws::CompiledRequirementSet compiled;
    compiled.frozen = true;
    rws::CompiledPoseTask stale;
    stale.id = "stale";
    compiled.poseTasks.push_back(stale);

    std::string error;
    REQUIRE(!rws::RequirementCompiler::compile(requirements, compiled, &error));
    REQUIRE(!compiled.frozen);
    REQUIRE(compiled.poseTasks.size() == 1);
    REQUIRE(compiled.poseTasks.front().id == "must-invalid");
    REQUIRE(compiled.poseTasks.front().compileState == rws::RequirementCompileState::Invalid);
    REQUIRE(!compiled.poseTasks.front().provenance.diagnosticCodes.empty());
    REQUIRE(compiled.poseTasks.front().provenance.diagnostics.size() == 1);
    const rws::RequirementDiagnostic& diagnostic =
        compiled.poseTasks.front().provenance.diagnostics.front();
    REQUIRE(diagnostic.code == "REQ_REQUIRED_FIELD_MISSING");
    REQUIRE(diagnostic.field == "tcpFrame");
    REQUIRE(diagnostic.message.find("TCP") != std::string::npos);
    REQUIRE(diagnostic.source == "engineeringrequirements.compiler");
    REQUIRE(diagnostic.severity == rws::RequirementDiagnosticSeverity::Error);
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
    REQUIRE(compiled.poseTasks.size() == 2);
    REQUIRE(compiled.poseTasks[1].compileState == rws::RequirementCompileState::Excluded);
    REQUIRE(!compiled.poseTasks[1].excludedReason.empty());
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

int testWorkspaceExecutionFieldsRoundTrip()
{
    rws::RequirementSet requirements;
    requirements.modelBinding.robotModelFingerprint = "model-fingerprint";
    rws::BoxRegion region;
    region.id = "verified_region";
    region.tcpFrame = "ToolTCP";
    region.orientationMode = rws::OrientationMode::AlignFrame;
    region.orientationTargetFrame = "FixtureFrame";
    region.fixedRpyDeg = {{10.0, 20.0, 30.0}};
    region.directionSamples = 12;
    region.rollSamples = 3;
    region.minimumOrientationCoverage = 0.75;
    region.collisionFreeRequired = false;
    region.positionToleranceMeters = 0.002;
    region.orientationToleranceDeg = 2.5;
    region.minimumJointMargin = 0.08;
    region.minimumManipulability = 0.01;
    region.minimumVerificationStage = rws::RequirementVerificationStage::Verified;
    requirements.boxRegions.push_back(region);

    const std::string json = rws::RequirementSetJson::toJson(requirements);
    rws::RequirementSet restored;
    std::string error;
    REQUIRE(rws::RequirementSetJson::fromJson(json, restored, &error));
    REQUIRE(restored.boxRegions.size() == 1);
    const rws::BoxRegion& decoded = restored.boxRegions.front();
    REQUIRE(decoded.tcpFrame == region.tcpFrame);
    REQUIRE(decoded.orientationMode == region.orientationMode);
    REQUIRE(decoded.orientationTargetFrame == region.orientationTargetFrame);
    REQUIRE(decoded.directionSamples == 12);
    REQUIRE(decoded.rollSamples == 3);
    REQUIRE(decoded.minimumVerificationStage == rws::RequirementVerificationStage::Verified);
    REQUIRE(std::abs(decoded.minimumOrientationCoverage - 0.75) < 1e-12);
    REQUIRE(!decoded.collisionFreeRequired);
    REQUIRE(std::abs(decoded.minimumJointMargin - 0.08) < 1e-12);
    return 0;
}

int testWorkspaceVerificationPolicyValidation()
{
    rws::RequirementSet requirements;
    requirements.modelBinding.robotModelFingerprint = "model-fingerprint";
    rws::BoxRegion verified;
    verified.id = "verified_grid";
    verified.tcpFrame = "TCP";
    verified.minimumVerificationStage = rws::RequirementVerificationStage::Verified;
    verified.samplesPerAxis = 1;
    requirements.boxRegions.push_back(verified);

    rws::CompiledRequirementSet compiled;
    std::string error;
    REQUIRE(!rws::RequirementCompiler::compile(requirements, compiled, &error));
    REQUIRE(error.find("samplesPerAxis") != std::string::npos);
    const std::vector<rws::RequirementDiagnostic> diagnostics =
        rws::RequirementCompiler::validateDetailed(requirements);
    REQUIRE(!diagnostics.empty());
    REQUIRE(diagnostics.front().code == "REQ_WORKSPACE_GRID_TOO_COARSE");

    requirements.boxRegions.front().level = rws::RequirementLevel::Should;
    REQUIRE(rws::RequirementCompiler::compile(requirements, compiled, &error));
    REQUIRE(compiled.workspaceRegions.size() == 1);
    REQUIRE(compiled.workspaceRegions.front().compileState == rws::RequirementCompileState::Excluded);

    rws::BoxRegion quick = verified;
    quick.id = "quick_grid";
    quick.level = rws::RequirementLevel::Must;
    quick.minimumVerificationStage = rws::RequirementVerificationStage::Quick;
    quick.samplesPerAxis = 1;
    quick.directionSamples = 0;
    requirements.boxRegions.clear();
    requirements.boxRegions.push_back(quick);
    REQUIRE(!rws::RequirementCompiler::compile(requirements, compiled, &error));
    REQUIRE(error.find("directionSamples") != std::string::npos);
    return 0;
}

// 采样上限测试：覆盖盒的逐轴网格数、方向样本数与翻滚样本数都超过安全上限时，
// 详细校验必须生成 REQ_WORKSPACE_SAMPLE_LIMIT_EXCEEDED 诊断，防止畸形需求在下游
// 采样分析中产生无界计算量。
int testWorkspaceSamplingLimitsRejectUnboundedWork()
{
    rws::RequirementSet requirements;
    requirements.modelBinding.robotModelFingerprint = "model-fingerprint";
    rws::BoxRegion region;
    region.id = "oversized_grid";
    region.tcpFrame = "TCP";
    // 三项采样参数均取"上限 + 1"，确保超出上限而非恰好在边界。
    region.samplesPerAxis = rws::MaxWorkspaceSamplesPerAxis + 1;
    region.directionSamples = rws::MaxWorkspaceDirectionSamples + 1;
    region.rollSamples = rws::MaxWorkspaceRollSamples + 1;
    requirements.boxRegions.push_back(region);

    const std::vector<rws::RequirementDiagnostic> diagnostics =
        rws::RequirementCompiler::validateDetailed(requirements);
    // 断言诊断列表中至少出现一次采样上限超限码。
    bool sawLimit = false;
    for (const rws::RequirementDiagnostic& diagnostic : diagnostics)
        sawLimit = sawLimit || diagnostic.code == "REQ_WORKSPACE_SAMPLE_LIMIT_EXCEEDED";
    REQUIRE(sawLimit);
    return 0;
}

int testStableRequirementDiagnosticCodes()
{
    rws::RequirementSet requirements;
    rws::PoseTask task;
    task.id = "pose-invalid";
    task.name = "Invalid pose";
    task.level = rws::RequirementLevel::Should;
    task.refFrame = "WORLD";
    task.tcpFrame = "TCP";
    task.orientation.mode = rws::OrientationMode::AlignFrame;
    task.orientation.targetFrame.clear();
    requirements.poseTasks.push_back(task);

    rws::BoxRegion region;
    region.id = "workspace-invalid";
    region.tcpFrame = "TCP";
    region.level = rws::RequirementLevel::Should;
    region.samplesPerAxis = 1;
    region.minimumVerificationStage = rws::RequirementVerificationStage::Verified;
    requirements.boxRegions.push_back(region);

    const std::vector<rws::RequirementDiagnostic> diagnostics =
        rws::RequirementCompiler::validateDetailed(requirements);
    bool orientationCode = false;
    bool gridCode = false;
    for (const rws::RequirementDiagnostic& diagnostic : diagnostics) {
        orientationCode = orientationCode || diagnostic.code == "REQ_ORIENTATION_TARGET_MISSING";
        gridCode = gridCode || diagnostic.code == "REQ_WORKSPACE_GRID_TOO_COARSE";
    }
    REQUIRE(orientationCode);
    REQUIRE(gridCode);
    return 0;
}

int testRequirementArtifactV3Migration()
{
    rws::FrozenRequirementArtifact legacy;
    legacy.schemaVersion = 3;
    legacy.requirementFingerprint = "requirements-v3";
    legacy.environmentFingerprint = "environment-v3";
    legacy.workcellFingerprint = "workcell-v3";
    legacy.modelBinding.robotModelFingerprint = "model-v3";
    legacy.compiled.frozen = true;
    legacy.compiled.requirementFingerprint = legacy.requirementFingerprint;
    legacy.compiled.modelBinding = legacy.modelBinding;
    rws::WorkspaceDemandRegion region;
    region.id = "legacy_region";
    region.refFrame = "WORLD";
    region.tcpFrame = "TCP";
    region.samplesPerAxis = 3;
    region.minimumVerificationStage = rws::RequirementVerificationStage::Verified;
    legacy.compiled.workspaceRegions.push_back(region);
    legacy.frozenRobotState.deviceName = "robot";
    legacy.frozenRobotState.tcpFrameName = "TCP";
    legacy.frozenRobotState.kinematicFingerprint = "kinematic-v3";
    legacy.frozenRobotState.capturedAt = "2026-08-05T00:00:00Z";
    legacy.frozenRobotState.tcpWorldPose[15] = 1.0;
    legacy.scenario.environmentFingerprint = legacy.environmentFingerprint;
    legacy.scenario.schemaVersion = 2;
    legacy.scenario.snapshotFingerprint = "snapshot-v3";

    const QJsonObject input = rws::FrozenRequirementArtifactJson::toObject(legacy);
    // 先做一次往返解析，验证 v3 工件经 fromObject 加载后：覆盖盒仍被完整保留，
    // 且由于 v3 缺乏 Verified 证据，验证阶段被迁移回填为 Quick。
    rws::FrozenRequirementArtifact parsedLegacy;
    std::string parseError;
    REQUIRE(rws::FrozenRequirementArtifactJson::fromObject(input, parsedLegacy, &parseError));
    REQUIRE(parsedLegacy.compiled.workspaceRegions.size() == 1);
    REQUIRE(parsedLegacy.compiled.workspaceRegions.front().minimumVerificationStage ==
            rws::RequirementVerificationStage::Quick);

    QJsonObject output;
    std::vector<rws::RequirementDiagnostic> diagnostics;
    std::string error;
    REQUIRE(rws::migrateRequirementArtifact(input, output, diagnostics, &error));
    REQUIRE(error.empty());
    REQUIRE(input.value("schemaVersion").toInt() == 3);
    REQUIRE(output.value("schemaVersion").toInt() == 4);
    REQUIRE(output.value("execution").isObject());
    REQUIRE(!diagnostics.empty());
    REQUIRE(diagnostics.front().code == "REQ_V3_REQUIRES_REFREEZE");
    const QJsonObject executionRegion = output.value("execution").toObject()
        .value("workspaceRegions").toArray().at(0).toObject();
    REQUIRE(executionRegion.value("minimumVerificationStage").toString() == "Quick");
    bool outputHasMigrationDiagnostic = false;
    for (const QJsonValue& diagnostic : output.value("diagnostics").toArray())
        outputHasMigrationDiagnostic = outputHasMigrationDiagnostic ||
            diagnostic.toObject().value("code").toString() == "REQ_V3_REQUIRES_REFREEZE";
    REQUIRE(outputHasMigrationDiagnostic);
    bool executionHasMigrationDiagnostic = false;
    for (const QJsonValue& diagnostic : output.value("execution").toObject()
             .value("diagnostics").toArray())
        executionHasMigrationDiagnostic = executionHasMigrationDiagnostic ||
            diagnostic.toObject().value("code").toString() == "REQ_V3_REQUIRES_REFREEZE";
    REQUIRE(executionHasMigrationDiagnostic);
    REQUIRE(!output.value("executionFingerprint").toString().isEmpty());
    return 0;
}

int testRequirementArtifactMigrationRejectsWrongHeaderTypes()
{
    rws::FrozenRequirementArtifact legacy;
    legacy.schemaVersion = 3;
    legacy.requirementFingerprint = "requirements-v3";
    legacy.environmentFingerprint = "environment-v3";
    legacy.workcellFingerprint = "workcell-v3";
    legacy.modelBinding.robotModelFingerprint = "model-v3";
    legacy.compiled.frozen = true;
    legacy.compiled.requirementFingerprint = legacy.requirementFingerprint;
    legacy.compiled.modelBinding = legacy.modelBinding;
    legacy.frozenRobotState.deviceName = "robot";
    legacy.frozenRobotState.tcpFrameName = "TCP";
    legacy.frozenRobotState.kinematicFingerprint = "kinematic-v3";
    legacy.frozenRobotState.capturedAt = "2026-08-05T00:00:00Z";
    legacy.frozenRobotState.tcpWorldPose[15] = 1.0;
    legacy.scenario.environmentFingerprint = legacy.environmentFingerprint;
    legacy.scenario.schemaVersion = 2;
    legacy.scenario.snapshotFingerprint = "snapshot-v3";

    const QJsonObject valid = rws::FrozenRequirementArtifactJson::toObject(legacy);
    for (const QJsonValue& invalidValue :
         {QJsonValue(3.5), QJsonValue(QStringLiteral("3")), QJsonValue(true)}) {
        QJsonObject invalid = valid;
        invalid["schemaVersion"] = invalidValue;
        QJsonObject output;
        std::vector<rws::RequirementDiagnostic> diagnostics;
        std::string error;
        REQUIRE(!rws::migrateRequirementArtifact(invalid, output, diagnostics, &error));
        REQUIRE(error.find("schemaVersion") != std::string::npos);
        REQUIRE(!diagnostics.empty());
        REQUIRE(diagnostics.front().code == "REQ_SCHEMA_UNSUPPORTED");
    }

    QJsonObject invalidType = valid;
    invalidType["type"] = 4;
    QJsonObject output;
    std::vector<rws::RequirementDiagnostic> diagnostics;
    std::string error;
    REQUIRE(!rws::migrateRequirementArtifact(invalidType, output, diagnostics, &error));
    REQUIRE(error.find("type") != std::string::npos);
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

int testFreezerRetainsNonBlockingEnvironmentExclusions()
{
    using namespace rw::kinematics;
    rws::RobotModelSpec model;
    model.robotName = "OptionalEnvironmentRobot";
    rws::RequirementSet requirements;
    requirements.modelBinding.robotName = model.robotName;
    requirements.modelBinding.robotModelFingerprint =
        rws::RobotModelFingerprint::canonicalSha256(model);

    StateStructure::Ptr structure = rw::core::ownedPtr(new StateStructure());
    const FixedFrame::Ptr base = rw::core::ownedPtr(
        new FixedFrame("OptionalEnvironmentBase", rw::math::Transform3D<>()));
    const FixedFrame::Ptr tcp = rw::core::ownedPtr(
        new FixedFrame("OptionalEnvironmentTcp", rw::math::Transform3D<>()));
    structure->addFrame(base, structure->getRoot());
    structure->addFrame(tcp, base);
    const rw::models::WorkCell::Ptr workcell = rw::core::ownedPtr(
        new rw::models::WorkCell(structure, "OptionalEnvironmentWorkCell", ""));
    workcell->addDevice(rw::core::ownedPtr(new rw::models::SerialDevice(
        base.get(), tcp.get(), model.robotName, structure->getDefaultState())));

    rws::BoxRegion region;
    region.id = "optional_missing_frame";
    region.level = rws::RequirementLevel::Should;
    region.refFrame = "MissingOptionalFixture";
    region.tcpFrame = tcp->getName();
    // 除环境诊断外再保留一条编译器级建议诊断(Verified 阶段的覆盖盒存在"过粗"建议)。
    // 工件重载必须完整保留冻结时的诊断记录，不能因重新编译而重复生成或丢失。
    region.minimumVerificationStage = rws::RequirementVerificationStage::Verified;
    region.samplesPerAxis = 1;
    requirements.boxRegions.push_back(region);

    rws::FrozenRequirementArtifact artifact;
    std::string error;
    // Should 级覆盖盒的引用 Frame 在场景中缺失(非阻塞环境诊断)：冻结应成功，
    // 但该覆盖盒在 compiled 与 execution 中都必须标记为 Excluded 并带排除原因，
    // 而不是被静默删除。
    REQUIRE(rws::RequirementFreezer::freeze(requirements, *workcell,
                                             workcell->getDefaultState(), model,
                                             artifact, &error));
    REQUIRE(artifact.compiled.workspaceRegions.size() == 1);
    REQUIRE(artifact.compiled.workspaceRegions.front().compileState ==
            rws::RequirementCompileState::Excluded);
    REQUIRE(!artifact.compiled.workspaceRegions.front().excludedReason.empty());
    // 断言出现 REQ_OPTIONAL_ITEM_EXCLUDED 审计诊断(可选条目被排除的明确记录)。
    bool sawOptionalExclusion = false;
    for (const rws::RequirementDiagnostic& diagnostic : artifact.compiled.diagnostics)
        sawOptionalExclusion = sawOptionalExclusion ||
            diagnostic.code == "REQ_OPTIONAL_ITEM_EXCLUDED";
    REQUIRE(sawOptionalExclusion);
    // 被排除项保留在编译审计快照中，但绝不能作为可执行工作区约束进入执行契约：
    // 因此 execution.workspaceRegions 必须为空。
    REQUIRE(artifact.execution.workspaceRegions.empty());
    // 往返重载后诊断数量必须与冻结时刻完全一致(不被重新编译重复生成)。
    const QJsonObject artifactObject = rws::FrozenRequirementArtifactJson::toObject(artifact);
    rws::FrozenRequirementArtifact restored;
    REQUIRE(rws::FrozenRequirementArtifactJson::fromObject(artifactObject, restored, &error));
    REQUIRE(restored.compiled.diagnostics.size() == artifact.compiled.diagnostics.size());
    // 重载后的执行契约同样不得包含被排除的覆盖盒。
    REQUIRE(restored.execution.workspaceRegions.empty());

    QJsonObject v4WithoutCompiledItems = artifactObject;
    v4WithoutCompiledItems.remove("compiledItems");
    error.clear();
    REQUIRE(!rws::FrozenRequirementArtifactJson::fromObject(
        v4WithoutCompiledItems, restored, &error));
    REQUIRE(error.find("compiledItems") != std::string::npos);

    // 诊断码可随插件版本扩展；条目状态必须由冻结快照恢复，而不能依赖固定码白名单。
    QJsonObject futureDiagnosticArtifact = artifactObject;
    QJsonArray futureItems = futureDiagnosticArtifact.value("compiledItems").toArray();
    for (int index = 0; index < futureItems.size(); ++index) {
        QJsonObject item = futureItems.at(index).toObject();
        if (item.value("id").toString() == "optional_missing_frame") {
            QJsonObject provenance = item.value("provenance").toObject();
            QJsonArray itemDiagnostics = provenance.value("diagnostics").toArray();
            for (int diagnosticIndex = 0; diagnosticIndex < itemDiagnostics.size(); ++diagnosticIndex) {
                QJsonObject diagnostic = itemDiagnostics.at(diagnosticIndex).toObject();
                diagnostic["code"] = "REQ_PLUGIN_FUTURE_ENVIRONMENT_UNAVAILABLE";
                itemDiagnostics[diagnosticIndex] = diagnostic;
            }
            provenance["diagnostics"] = itemDiagnostics;
            item["provenance"] = provenance;
            futureItems[index] = item;
        }
    }
    futureDiagnosticArtifact["compiledItems"] = futureItems;
    rws::FrozenRequirementArtifact futureRestored;
    REQUIRE(rws::FrozenRequirementArtifactJson::fromObject(
        futureDiagnosticArtifact, futureRestored, &error));
    REQUIRE(futureRestored.compiled.workspaceRegions.front().compileState ==
            rws::RequirementCompileState::Excluded);
    REQUIRE(!futureRestored.compiled.workspaceRegions.front().excludedReason.empty());
    REQUIRE(rws::RequirementFreezer::validateExecutionConsistency(futureRestored, &error));
    return 0;
}

// TCP 归属校验测试：工位声明的 TCP Frame 属于场景中"另一台设备"而非绑定机器人。
// 冻结器必须拒绝此类需求(对阻塞诊断直接失败并给出包含 "does not belong" 的明确
// 错误)，防止把别的机器人末端误当作绑定设备 TCP 进入后续 IK/采样。
int testFreezerRejectsTcpFromAnotherDevice()
{
    using namespace rw::kinematics;
    rws::RobotModelSpec model;
    model.robotName = "BoundRobot";
    rws::RequirementSet requirements;
    requirements.modelBinding.robotName = model.robotName;
    requirements.modelBinding.robotModelFingerprint =
        rws::RobotModelFingerprint::canonicalSha256(model);

    rws::PoseTask task;
    task.id = "wrong_device_tcp";
    task.refFrame = "WORLD";
    task.tcpFrame = "OtherRobotTcp";
    requirements.poseTasks.push_back(task);

    StateStructure::Ptr structure = rw::core::ownedPtr(new StateStructure());
    const FixedFrame::Ptr boundBase = rw::core::ownedPtr(
        new FixedFrame("BoundBase", rw::math::Transform3D<>()));
    const FixedFrame::Ptr boundTcp = rw::core::ownedPtr(
        new FixedFrame("BoundRobotTcp", rw::math::Transform3D<>()));
    const FixedFrame::Ptr otherBase = rw::core::ownedPtr(
        new FixedFrame("OtherBase", rw::math::Transform3D<>()));
    const FixedFrame::Ptr otherTcp = rw::core::ownedPtr(
        new FixedFrame("OtherRobotTcp", rw::math::Transform3D<>()));
    structure->addFrame(boundBase, structure->getRoot());
    structure->addFrame(boundTcp, boundBase);
    structure->addFrame(otherBase, structure->getRoot());
    structure->addFrame(otherTcp, otherBase);
    const rw::models::WorkCell::Ptr workcell = rw::core::ownedPtr(
        new rw::models::WorkCell(structure, "MultiDeviceWorkCell", ""));
    workcell->addDevice(rw::core::ownedPtr(new rw::models::SerialDevice(
        boundBase.get(), boundTcp.get(), model.robotName, structure->getDefaultState())));
    workcell->addDevice(rw::core::ownedPtr(new rw::models::SerialDevice(
        otherBase.get(), otherTcp.get(), "OtherRobot", structure->getDefaultState())));

    rws::FrozenRequirementArtifact artifact;
    std::string error;
    REQUIRE(!rws::RequirementFreezer::freeze(requirements, *workcell,
                                              workcell->getDefaultState(), model,
                                              artifact, &error));
    REQUIRE(error.find("does not belong") != std::string::npos);
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
        {"REQ_OPTIONAL_ITEM_EXCLUDED", rws::RequirementDiagnosticSeverity::Warning,
         "optional_missing", rws::RequirementLevel::Should, "", "Excluded from frozen artifact.",
         "engineeringrequirements.test", false});

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
    artifact.publication.revisionNumber = 7;
    artifact.publication.revisionId = "REQ-007";
    artifact.publication.state = "published";
    artifact.publication.publishedAt = artifact.frozenRobotState.capturedAt;

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
    REQUIRE(objectRestored.publication.revisionId == "REQ-007");
    REQUIRE(objectRestored.publication.state == "published");

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

    rws::PoseTask task;
    task.id = "state_task";
    task.name = "State task";
    task.refFrame = "WORLD";
    task.tcpFrame = "ArtifactTcp";
    task.position = {{0.0, 0.0, 0.0}};
    requirements.poseTasks.push_back(task);
    rws::BoxRegion region;
    region.id = "state_region";
    region.name = "State region";
    region.refFrame = "WORLD";
    region.tcpFrame = "ArtifactTcp";
    region.size = {{0.1, 0.1, 0.1}};
    region.samplesPerAxis = 3;
    requirements.boxRegions.push_back(region);

    rws::FrozenRequirementArtifact artifact;
    std::string error;
    REQUIRE(rws::RequirementFreezer::freeze(requirements, *workcell, frozenState, model, artifact, &error));
    REQUIRE(artifact.schemaVersion == 4);
    REQUIRE(artifact.execution.tasks.size() == 1);
    REQUIRE(artifact.execution.workspaceRegions.size() == 1);
    REQUIRE(artifact.execution.workspaceRegions[0].minimumVerificationStage ==
            rws::RequirementExecutionStage::Verified);
    REQUIRE(!artifact.executionFingerprint.empty());
    REQUIRE(rws::RequirementFreezer::isCurrent(artifact, requirements, *workcell, frozenState, model, &error));

    // 篡改"编译快照"中的工位位置后直接反序列化：v4 工件要求执行契约与编译快照
    // 精确一致，因此即使只改 compiledRequirements 的一处数值，也必须被
    // fromObject 的一致性校验拒绝。
    QJsonObject tamperedSnapshot = rws::FrozenRequirementArtifactJson::toObject(artifact);
    QJsonObject compiledRequirements = tamperedSnapshot.value("compiledRequirements").toObject();
    QJsonArray compiledTasks = compiledRequirements.value("poseTasks").toArray();
    if (!compiledTasks.isEmpty()) {
        QJsonObject task = compiledTasks.at(0).toObject();
        QJsonArray position = task.value("position").toArray();
        position.replace(0, position.at(0).toDouble() + 0.01);
        task["position"] = position;
        compiledTasks.replace(0, task);
        compiledRequirements["poseTasks"] = compiledTasks;
        tamperedSnapshot["compiledRequirements"] = compiledRequirements;
        rws::FrozenRequirementArtifact parsedTamperedSnapshot;
        REQUIRE(!rws::FrozenRequirementArtifactJson::fromObject(
            tamperedSnapshot, parsedTamperedSnapshot, &error));
    }

    rws::FrozenRequirementArtifact tamperedExecution = artifact;
    tamperedExecution.execution.workspaceRegions[0].size[0] += 0.01;
    REQUIRE(!rws::RequirementFreezer::isCurrent(
        tamperedExecution, requirements, *workcell, frozenState, model, &error));
    REQUIRE(error.find("execution") != std::string::npos);

    // 保留的冻结诊断是"编译项无法解析"的权威证据。仅把 v4 执行契约中的对应项改为
    // Included，不能把该排除状态"洗白"：工件必须拒绝这种自相矛盾的重载 —— 诊断声称
    // 该工位 TCP 未解析，执行契约却声称已包含，二者不一致时以诊断为准并拒绝。
    QJsonObject contradictoryExclusion = rws::FrozenRequirementArtifactJson::toObject(artifact);
    QJsonArray contradictoryDiagnostics = contradictoryExclusion.value("diagnostics").toArray();
    QJsonObject unresolvedTcp;
    unresolvedTcp["code"] = "REQ_TCP_FRAME_NOT_FOUND";
    unresolvedTcp["requirementId"] = "state_task";
    unresolvedTcp["level"] = "Should";
    unresolvedTcp["message"] = "Injected unresolved TCP diagnostic for consistency testing.";
    unresolvedTcp["blocking"] = false;
    contradictoryDiagnostics.append(unresolvedTcp);
    contradictoryExclusion["diagnostics"] = contradictoryDiagnostics;
    rws::FrozenRequirementArtifact parsedContradictoryExclusion;
    REQUIRE(!rws::FrozenRequirementArtifactJson::fromObject(
        contradictoryExclusion, parsedContradictoryExclusion, &error));

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

int testFrozenArtifactWarnsWhenSourceWorkCellFileChanges()
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
    REQUIRE(artifact.schemaVersion == 4);
    REQUIRE(artifact.execution.provenance.requirementFingerprint == artifact.requirementFingerprint);
    REQUIRE(!artifact.scenario.sourceFileFingerprint.empty());
    REQUIRE(rws::RequirementFreezer::isCurrent(artifact, requirements, *workcell,
                                                 workcell->getDefaultState(), model, &error));

    REQUIRE(sourceFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate));
    REQUIRE(sourceFile.write("<WorkCell name=\"FrozenCellChanged\" />\n") > 0);
    sourceFile.close();
    rws::FrozenRequirementValidationResult validation;
    REQUIRE(rws::RequirementFreezer::validateScenario(artifact, *workcell,
                                                        workcell->getDefaultState(),
                                                        &validation, &error));
    REQUIRE(error.empty());
    REQUIRE(!validation.warnings.empty());
    REQUIRE(validation.warnings.front().find("source WorkCell file") != std::string::npos);
    REQUIRE(rws::RequirementFreezer::isCurrent(artifact, requirements, *workcell,
                                                workcell->getDefaultState(), model, &error));
    return 0;
}

int testRelativeSourceWithoutBaseDoesNotReadCurrentDirectory()
{
    QTemporaryDir projectDirectory;
    QTemporaryDir unrelatedDirectory;
    REQUIRE(projectDirectory.isValid());
    REQUIRE(unrelatedDirectory.isValid());
    const QString workcellPath = projectDirectory.filePath("frozen-cell.wc.xml");
    QFile sourceFile(workcellPath);
    REQUIRE(sourceFile.open(QIODevice::WriteOnly | QIODevice::Text));
    REQUIRE(sourceFile.write("<WorkCell name=\"FrozenCell\" />\n") > 0);
    sourceFile.close();

    rws::RobotModelSpec model;
    model.robotName = "RelativeSourceRobot";
    rws::RequirementSet requirements;
    requirements.modelBinding.robotName = model.robotName;
    requirements.modelBinding.robotModelFingerprint =
        rws::RobotModelFingerprint::canonicalSha256(model);
    rw::kinematics::StateStructure::Ptr structure =
        rw::core::ownedPtr(new rw::kinematics::StateStructure());
    const rw::kinematics::FixedFrame::Ptr base = rw::core::ownedPtr(
        new rw::kinematics::FixedFrame("RelativeBase", rw::math::Transform3D<>()));
    const rw::models::RevoluteJoint::Ptr joint = rw::core::ownedPtr(
        new rw::models::RevoluteJoint("RelativeJoint", rw::math::Transform3D<>()));
    const rw::kinematics::FixedFrame::Ptr tcp = rw::core::ownedPtr(
        new rw::kinematics::FixedFrame("RelativeTcp", rw::math::Transform3D<>()));
    structure->addFrame(base, structure->getRoot());
    structure->addFrame(joint, base);
    structure->addFrame(tcp, joint);
    const rw::models::WorkCell::Ptr workcell = rw::core::ownedPtr(
        new rw::models::WorkCell(structure, "RelativeSourceWorkCell", workcellPath.toStdString()));
    workcell->addDevice(rw::core::ownedPtr(new rw::models::SerialDevice(
        base.get(), tcp.get(), model.robotName, structure->getDefaultState())));

    rws::FrozenRequirementArtifact artifact;
    std::string error;
    REQUIRE(rws::RequirementFreezer::freeze(
        requirements, *workcell, workcell->getDefaultState(), model, artifact, &error,
        projectDirectory.path().toStdString()));
    REQUIRE(QFileInfo(QString::fromStdString(
                artifact.scenario.sourceWorkCellPath)).isRelative());

    const QString previousCwd = QDir::currentPath();
    REQUIRE(QDir::setCurrent(projectDirectory.path()));
    rws::FrozenRequirementValidationResult withoutBase;
    REQUIRE(rws::RequirementFreezer::validateScenario(
        artifact, *workcell, workcell->getDefaultState(), &withoutBase, &error));
    REQUIRE(withoutBase.warnings.size() == 1);
    REQUIRE(withoutBase.warnings.front().find("base directory") != std::string::npos);

    rws::FrozenRequirementValidationResult withBase;
    REQUIRE(rws::RequirementFreezer::validateScenario(
        artifact, *workcell, workcell->getDefaultState(), &withBase, &error,
        projectDirectory.path().toStdString()));
    REQUIRE(withBase.warnings.empty());
    REQUIRE(QDir::setCurrent(previousCwd));
    return 0;
}

int testWidgetManagedLoadUsesExplicitProjectRoot()
{
    QTemporaryDir workspace;
    REQUIRE(workspace.isValid());
    const QString oldRoot = workspace.filePath("old-project");
    const QString newRoot = workspace.filePath("new-project");
    const QString sourcePath = QDir(newRoot).filePath("scenes/main.wc.xml");
    const QString staleSourcePath = QDir(oldRoot).filePath("scenes/main.wc.xml");
    const QString modelPath = QDir(newRoot).filePath("generated/robot-models/main.rmb.json");
    const QString documentPath = QDir(newRoot).filePath(
        "requirements/main.requirements.json");
    REQUIRE(QDir().mkpath(QFileInfo(sourcePath).absolutePath()));
    REQUIRE(QDir().mkpath(QFileInfo(staleSourcePath).absolutePath()));
    REQUIRE(QDir().mkpath(QFileInfo(modelPath).absolutePath()));
    REQUIRE(QDir().mkpath(QFileInfo(documentPath).absolutePath()));
    const auto writeFile = [](const QString& path, const QByteArray& data) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
               file.write(data) == data.size();
    };
    REQUIRE(writeFile(sourcePath, "<WorkCell name=\"ManagedCell\" />\n"));
    REQUIRE(writeFile(staleSourcePath, "<WorkCell name=\"StaleCell\" />\n"));

    rws::RobotModelSpec model;
    model.robotName = "ManagedRootRobot";
    const QByteArray serializedModel =
        QByteArray::fromStdString(rws::RobotModelSpecJson::toJson(model));
    REQUIRE(writeFile(modelPath, serializedModel));
    std::string modelParseError;
    REQUIRE(rws::RobotModelSpecJson::fromJson(
        serializedModel.toStdString(), model, &modelParseError));
    rws::RequirementSet requirements;
    requirements.modelBinding.sourcePath = modelPath.toStdString();
    requirements.modelBinding.robotName = model.robotName;
    requirements.modelBinding.robotModelFingerprint =
        rws::RobotModelFingerprint::canonicalSha256(model);

    rw::kinematics::StateStructure::Ptr structure =
        rw::core::ownedPtr(new rw::kinematics::StateStructure());
    const rw::kinematics::FixedFrame::Ptr base = rw::core::ownedPtr(
        new rw::kinematics::FixedFrame("ManagedRootBase", rw::math::Transform3D<>()));
    const rw::models::RevoluteJoint::Ptr joint = rw::core::ownedPtr(
        new rw::models::RevoluteJoint("ManagedRootJoint", rw::math::Transform3D<>()));
    const rw::kinematics::FixedFrame::Ptr tcp = rw::core::ownedPtr(
        new rw::kinematics::FixedFrame("ManagedRootTcp", rw::math::Transform3D<>()));
    structure->addFrame(base, structure->getRoot());
    structure->addFrame(joint, base);
    structure->addFrame(tcp, joint);
    const rw::models::WorkCell::Ptr workcell = rw::core::ownedPtr(
        new rw::models::WorkCell(structure, "ManagedRootWorkCell", sourcePath.toStdString()));
    workcell->addDevice(rw::core::ownedPtr(new rw::models::SerialDevice(
        base.get(), tcp.get(), model.robotName, structure->getDefaultState())));

    rws::FrozenRequirementArtifact artifact;
    std::string freezeError;
    REQUIRE(rws::RequirementFreezer::freeze(
        requirements, *workcell, workcell->getDefaultState(), model, artifact,
        &freezeError, newRoot.toStdString()));
    QJsonObject project = rws::RequirementSetJson::toObject(requirements);
    QJsonObject binding = project.value("modelBinding").toObject();
    binding["sourcePath"] = modelPath;
    project["modelBinding"] = binding;
    QJsonObject artifactObject = rws::FrozenRequirementArtifactJson::toObject(artifact);
    QJsonObject artifactBinding = artifactObject.value("modelBinding").toObject();
    artifactBinding["sourcePath"] = binding.value("sourcePath");
    artifactObject["modelBinding"] = artifactBinding;
    project["frozenArtifact"] = artifactObject;
    REQUIRE(writeFile(documentPath, QJsonDocument(project).toJson()));

    rws::FrozenRequirementValidationResult newRootValidation;
    rws::FrozenRequirementValidationResult oldRootValidation;
    REQUIRE(rws::RequirementFreezer::validateScenario(
        artifact, *workcell, workcell->getDefaultState(), &newRootValidation,
        &freezeError, newRoot.toStdString()));
    REQUIRE(newRootValidation.warnings.empty());
    REQUIRE(rws::RequirementFreezer::validateScenario(
        artifact, *workcell, workcell->getDefaultState(), &oldRootValidation,
        &freezeError, oldRoot.toStdString()));
    REQUIRE(!oldRootValidation.warnings.empty());

    QString error;
    rws::EngineeringRequirementsWidget widget;
    widget.setProjectOutputDirectory(newRoot);
    REQUIRE(widget.loadProjectDocument(documentPath, &error, newRoot));
    REQUIRE(!widget.requirementSet().frozen);
    widget.setWorkCell(workcell.get());
    widget.setCurrentState(workcell->getDefaultState());
    REQUIRE(widget.requirementSet().frozen);

    widget.setProjectOutputDirectory(oldRoot);
    REQUIRE(widget.loadProjectDocument(documentPath, &error, oldRoot));
    REQUIRE(widget.requirementSet().frozen);
    REQUIRE(widget.statusText().contains("source WorkCell file is missing or has changed"));

    REQUIRE(widget.loadProjectDocument(documentPath, &error, newRoot));
    if (!widget.requirementSet().frozen)
        std::fprintf(stderr, "Managed root load status: %s\n",
                     widget.statusText().toStdString().c_str());
    REQUIRE(widget.requirementSet().frozen);
    REQUIRE(!widget.statusText().contains("source WorkCell file is missing or has changed"));
    return 0;
}

int testFreezeMakesManagedUrScenarioPathsProjectRelative()
{
    const QString projectRoot = QDir(QStringLiteral(ENGINEERINGREQUIREMENTS_TEST_SOURCE_DIR))
                                    .filePath(QStringLiteral(
                                        "RobWork/example/ModelData/XMLDevices/UR-6-85-5-A"));
    const QString workcellPath = QDir(projectRoot).filePath(QStringLiteral("UR.wc.xml"));
    const rw::models::WorkCell::Ptr workcell =
        rw::loaders::WorkCellLoader::Factory::load(workcellPath.toStdString());
    REQUIRE(!workcell.isNull());

    QStringList warnings;
    rws::RobotModelSpec model = rws::WorkCellConverter::convert(
        *workcell, workcell->getDefaultState(), projectRoot.toStdString(), warnings);
    REQUIRE(model.robotName == "UR-6-85-5-A");
    rws::RequirementSet requirements;
    requirements.modelBinding.robotName = model.robotName;
    requirements.modelBinding.robotModelFingerprint =
        rws::RobotModelFingerprint::canonicalSha256(model);

    rws::FrozenRequirementArtifact artifact;
    std::string error;
    REQUIRE(rws::RequirementFreezer::freeze(
        requirements, *workcell, workcell->getDefaultState(), model, artifact, &error,
        projectRoot.toStdString()));
    REQUIRE(!QFileInfo(QString::fromStdString(artifact.scenario.sourceWorkCellPath)).isAbsolute());
    REQUIRE(!QFileInfo(QString::fromStdString(artifact.scenario.sceneSpec.saveDirectory)).isAbsolute());
    for (const rws::DrawableSpec& drawable : artifact.scenario.sceneSpec.drawables) {
        if (!drawable.filePath.empty())
            REQUIRE(!QFileInfo(QString::fromStdString(drawable.filePath)).isAbsolute());
    }
    for (const rws::CollisionModelSpec& collision : artifact.scenario.sceneSpec.collisionModels) {
        if (!collision.filePath.empty())
            REQUIRE(!QFileInfo(QString::fromStdString(collision.filePath)).isAbsolute());
    }
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
    REQUIRE(tabs->tabText(0) == QStringLiteral("Key Stations"));
    REQUIRE(widget.findChild<QPushButton*>("addRequirementPoseTaskButton") != nullptr);
    REQUIRE(widget.findChild<QPushButton*>("addRequirementBoxRegionButton") != nullptr);
    REQUIRE(widget.findChild<QPushButton*>("freezeRequirementSetButton") != nullptr);
    REQUIRE(widget.findChild<QPushButton*>("saveRequirementSetButton") != nullptr);
    REQUIRE(widget.findChild<QTableWidget*>("engineeringRequirementsDiagnosticTable") != nullptr);
    REQUIRE(widget.findChild<QLabel*>("engineeringRequirementsValidationSummaryLabel") != nullptr);
    return 0;
}

int testEngineeringRequirementsWidgetUsesEnglishCopy()
{
    rws::EngineeringRequirementsWidget widget;
    QTabWidget* tabs = widget.findChild<QTabWidget*>("engineeringRequirementsTabs");
    REQUIRE(tabs != nullptr);
    REQUIRE(tabs->tabText(0) == "Key Stations");
    REQUIRE(tabs->tabText(1) == "Workspace Regions");
    REQUIRE(tabs->tabText(2) == "Validate & Publish");

    const auto requireButtonText = [&widget](const char* objectName, const QString& expected) {
        QPushButton* button = widget.findChild<QPushButton*>(objectName);
        REQUIRE(button != nullptr);
        REQUIRE(button->text() == expected);
        return 0;
    };
    REQUIRE(requireButtonText("addRequirementPoseTaskButton", "Add Station") == 0);
    REQUIRE(requireButtonText("captureRequirementTcpButton", "Capture TCP Pose") == 0);
    REQUIRE(requireButtonText("bindRequirementModelButton", "Bind Model") == 0);
    REQUIRE(requireButtonText("freezeRequirementSetButton", "Check and Publish") == 0);
    return 0;
}

int testWidgetExposesSemanticKeyStationInspector()
{
    rws::EngineeringRequirementsWidget widget;
    QWidget* toolbar = widget.findChild<QWidget*>("keyStationCompactToolbar");
    REQUIRE(toolbar != nullptr);
    REQUIRE(toolbar->sizePolicy().verticalPolicy() == QSizePolicy::Fixed);
    REQUIRE(toolbar->findChildren<QPushButton*>().size() == 2);
    REQUIRE(toolbar->findChildren<QToolButton*>().size() == 5);
    REQUIRE(widget.findChild<QToolButton*>("keyStationEditMenu") != nullptr);
    REQUIRE(widget.findChild<QToolButton*>("keyStationTemplateMenu") != nullptr);
    REQUIRE(widget.findChild<QToolButton*>("keyStationGenerateMenu") != nullptr);
    REQUIRE(widget.findChild<QToolButton*>("keyStationMoreMenu") != nullptr);
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

// 冻结/解冻也必须发出 requirementsChanged，使 Provider 脏状态与标题栏同步更新。
int testWidgetFreezeAndUnfreezeEmitRequirementChanges()
{
    QTemporaryDir projectDirectory;
    REQUIRE(projectDirectory.isValid());
    const QString modelDirectory = projectDirectory.filePath("generated/robot-models");
    REQUIRE(QDir().mkpath(modelDirectory));

    rws::RobotModelSpec model;
    model.robotName = "FreezeSignalRobot";
    const QString modelPath = modelDirectory + "/FreezeSignalRobot.rmb.json";
    QFile modelFile(modelPath);
    REQUIRE(modelFile.open(QIODevice::WriteOnly | QIODevice::Text));
    REQUIRE(modelFile.write(rws::RobotModelSpecJson::toJson(model).c_str()) > 0);
    modelFile.close();

    rw::kinematics::StateStructure::Ptr structure =
        rw::core::ownedPtr(new rw::kinematics::StateStructure());
    const rw::kinematics::FixedFrame::Ptr base = rw::core::ownedPtr(
        new rw::kinematics::FixedFrame("FreezeSignalBase", rw::math::Transform3D<>()));
    const rw::kinematics::MovableFrame::Ptr tcp = rw::core::ownedPtr(
        new rw::kinematics::MovableFrame("FreezeSignalTcp"));
    structure->addFrame(base, structure->getRoot());
    structure->addFrame(tcp, base);
    const rw::models::WorkCell::Ptr workcell = rw::core::ownedPtr(
        new rw::models::WorkCell(structure, "FreezeSignalWorkCell", ""));
    workcell->addDevice(rw::core::ownedPtr(new rw::models::SerialDevice(
        base.get(), tcp.get(), model.robotName, structure->getDefaultState())));

    rws::EngineeringRequirementsWidget widget;
    widget.setProjectOutputDirectory(projectDirectory.path());
    widget.setProjectModelPath(modelPath);
    widget.setWorkCell(workcell.get());
    QString error;
    REQUIRE(widget.bindGeneratedProjectModel(&error));

    int changeCount = 0;
    std::vector<std::string> notifications;
    QObject::connect(&widget, &rws::EngineeringRequirementsWidget::requirementsChanged,
                     [&changeCount, &notifications]() {
                         ++changeCount;
                         notifications.push_back("changed");
                     });
    QObject::connect(&widget,
                     &rws::EngineeringRequirementsWidget::freezePublicationRequested,
                     [&notifications]() { notifications.push_back("publish"); });
    QPushButton* freeze = widget.findChild<QPushButton*>("freezeRequirementSetButton");
    QPushButton* unfreeze = widget.findChild<QPushButton*>("unfreezeRequirementSetButton");
    REQUIRE(freeze != nullptr);
    REQUIRE(unfreeze != nullptr);

    freeze->click();
    REQUIRE(widget.requirementSet().frozen);
    REQUIRE(changeCount == 1);
    REQUIRE(notifications.size() == 2);
    REQUIRE(notifications[0] == "changed");
    REQUIRE(notifications[1] == "publish");

    unfreeze->click();
    REQUIRE(!widget.requirementSet().frozen);
    REQUIRE(changeCount == 2);
    return 0;
}

int testWidgetBlocksFreezeUntilManagedWorkCellExists()
{
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const QString projectPath = directory.filePath("RobotDraft/RobotDraft.rwproj");
    rws::ProjectManifest manifest;
    manifest.project.id = "robot-draft";
    manifest.project.name = "RobotDraft";
    rws::ProjectResource source;
    source.id = "robot-source.main";
    source.kind = "robwork.passive-asset";
    source.path = "sources/robot.urdf";
    source.ownership = "project";
    source.required = false;
    rws::ProjectResource model;
    model.id = "robot-model.main";
    model.kind = "robwork.robot-model";
    model.path = "generated/robot-models/robot.rmb.json";
    model.ownership = "generated";
    model.required = false;
    model.dependencies = {source.id};
    manifest.resources = {source, model};
    manifest.entryPoints.insert("robotSource", source.id);
    QString error;
    rws::ProjectManager manager;
    REQUIRE(manager.createProject(projectPath, manifest, &error));
    manager.closeProject();

    const QString projectRoot = QFileInfo(projectPath).absolutePath();
    REQUIRE(QDir().mkpath(QFileInfo(QDir(projectRoot).filePath(source.path)).absolutePath()));
    QFile sourceFile(QDir(projectRoot).filePath(source.path));
    REQUIRE(sourceFile.open(QIODevice::WriteOnly));
    REQUIRE(sourceFile.write("<robot name=\"RobotDraft\"/>") > 0);
    sourceFile.close();
    REQUIRE(QDir().mkpath(QFileInfo(QDir(projectRoot).filePath(model.path)).absolutePath()));
    QFile modelFile(QDir(projectRoot).filePath(model.path));
    REQUIRE(modelFile.open(QIODevice::WriteOnly));
    REQUIRE(modelFile.write("{}") == 2);
    modelFile.close();

    rw::core::PropertyMap properties;
    rws::RobWorkStudio studio(properties);
    rws::CallbackProjectDocumentProvider modelProvider(
        "test.robot-model", "robwork.robot-model",
        [](const QString&, const rws::ProjectDocumentContext&, QString*) { return true; },
        [](const QString&, const rws::ProjectDocumentContext&, QString*) { return true; });
    REQUIRE(studio.registerProjectDocumentProvider(&modelProvider, &error));
    studio.openFile(projectPath.toStdString());
    REQUIRE(!studio.projectDirectory().isEmpty());
    REQUIRE(studio.mainWorkCellResourceId().isEmpty());

    rws::EngineeringRequirementsWidget widget;
    widget.setFreezeReadinessCheck([&studio](QString* readinessError) {
        *readinessError = rws::robotProjectWorkCellReadinessError(&studio);
        return readinessError->isEmpty();
    });
    QPushButton* freeze = widget.findChild<QPushButton*>("freezeRequirementSetButton");
    REQUIRE(freeze != nullptr);
    freeze->click();
    REQUIRE(widget.statusText() == QStringLiteral(
        "The robot project has not generated its managed WorkCell. Review the model in "
        "RobotModelBuilder and run Save and Load first."));
    REQUIRE(!widget.requirementSet().frozen);
    studio.close();
    return 0;
}

// 负向测试：冻结校验失败时绝不发出发布请求，避免下游读到未经验证的冻结工件。
int testWidgetDoesNotRequestPublicationWhenFreezeFails()
{
    rws::EngineeringRequirementsWidget widget;
    int publicationRequests = 0;
    QObject::connect(&widget,
                     &rws::EngineeringRequirementsWidget::freezePublicationRequested,
                     [&publicationRequests]() { ++publicationRequests; });

    QPushButton* freeze = widget.findChild<QPushButton*>("freezeRequirementSetButton");
    REQUIRE(freeze != nullptr);
    freeze->click();
    REQUIRE(!widget.requirementSet().frozen);
    REQUIRE(publicationRequests == 0);
    return 0;
}

// 副本路径策略：有项目时导出到 requirements/exports、导入优先该目录，无项目回退默认。
int testProjectRequirementCopyPathPolicy()
{
    QTemporaryDir projectDirectory;
    REQUIRE(projectDirectory.isValid());

    const QString requirementsDirectory = projectDirectory.filePath("requirements");
    const QString exportDirectory = requirementsDirectory + "/exports";
    REQUIRE(rws::EngineeringRequirementsWidget::requirementCopyExportPath(
                projectDirectory.path()) ==
            exportDirectory + "/requirements-copy.requirements.json");
    REQUIRE(rws::EngineeringRequirementsWidget::requirementCopyImportDirectory(
                projectDirectory.path()) == requirementsDirectory);
    REQUIRE(rws::EngineeringRequirementsWidget::requirementCopyExportPath(QString()) ==
            QStringLiteral("requirements.requirements.json"));

    REQUIRE(QDir().mkpath(exportDirectory));
    REQUIRE(rws::EngineeringRequirementsWidget::requirementCopyImportDirectory(
                projectDirectory.path()) == exportDirectory);
    return 0;
}

// 生成资源基线：beginGeneratedProjectDocument 建立以当前配置为已保存快照的基线，
// 使首次编辑即可参与项目脏状态，副本导入导出使用项目内路径。
int testWidgetUsesProjectRequirementCopyPathsAndGeneratedDocumentBaseline()
{
    QTemporaryDir projectDirectory;
    REQUIRE(projectDirectory.isValid());

    const QString requirementsDirectory = projectDirectory.filePath("requirements");
    const QString primaryDocument = requirementsDirectory + "/main.requirements.json";

    rws::EngineeringRequirementsWidget widget;
    widget.beginGeneratedProjectDocument(primaryDocument);
    REQUIRE(!widget.isProjectDocumentDirty());
    widget.findChild<QPushButton*>("addRequirementPoseTaskButton")->click();
    REQUIRE(widget.isProjectDocumentDirty());
    return 0;
}

// 校验工程需求 Widget 在项目资源关闭/切换时彻底清除上一项目的需求会话:
// 1) 通过"添加关键工位点"与"添加盒体需求区域"按钮产生数据后,数据层
//    (poseTasks/boxRegions)与 UI 层(关键工位列表、盒体区域表格)应同步出现条目;
// 2) 调用 clearProjectDocumentContext() 后,需求集合、冻结标记以及所有对应
//    UI 条目全部清空,保证新项目不会继承旧项目的需求上下文。
int testWidgetProjectCloseClearsRequirementSession()
{
    rws::EngineeringRequirementsWidget widget;
    QPushButton* addStation = widget.findChild<QPushButton*>("addRequirementPoseTaskButton");
    QPushButton* addRegion = widget.findChild<QPushButton*>("addRequirementBoxRegionButton");
    QListWidget* stationList = widget.findChild<QListWidget*>("keyStationList");
    QTableWidget* regionTable = widget.findChild<QTableWidget*>("engineeringRequirementBoxTable");
    REQUIRE(addStation != nullptr);
    REQUIRE(addRegion != nullptr);
    REQUIRE(stationList != nullptr);
    REQUIRE(regionTable != nullptr);

    widget.beginGeneratedProjectDocument(QStringLiteral("old/requirements/main.requirements.json"));
    addStation->click();
    addRegion->click();
    REQUIRE(widget.requirementSet().poseTasks.size() == 1);
    REQUIRE(widget.requirementSet().boxRegions.size() == 1);
    REQUIRE(stationList->count() == 1);
    REQUIRE(regionTable->rowCount() == 1);

    widget.clearProjectDocumentContext();

    REQUIRE(widget.requirementSet().poseTasks.empty());
    REQUIRE(widget.requirementSet().boxRegions.empty());
    REQUIRE(!widget.requirementSet().frozen);
    REQUIRE(stationList->count() == 0);
    REQUIRE(regionTable->rowCount() == 0);
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

// 需求冻结前只绑定项目清单解析出的 robot-model.main。同目录中的其他
// .rmb.json 不是权威资源，不得影响绑定结果。
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
    widget.setProjectModelPath(modelPath);
    widget.setWorkCell(workcell.get());
    QString error;
    REQUIRE(widget.bindGeneratedProjectModel(&error));
    const rws::RequirementSet requirements = widget.requirementSet();
    REQUIRE(requirements.modelBinding.sourcePath == modelPath.toStdString());
    REQUIRE(requirements.modelBinding.robotName == model.robotName);
    REQUIRE(requirements.modelBinding.robotModelFingerprint ==
            rws::RobotModelFingerprint::canonicalSha256(model));

    // Extra sidecars are not authoritative. The manifest-resolved robot-model.main
    // path remains stable even when unrelated files exist in the same directory.
    QFile secondModelFile(modelDirectory + "/AnotherRobot.rmb.json");
    REQUIRE(secondModelFile.open(QIODevice::WriteOnly | QIODevice::Text));
    REQUIRE(secondModelFile.write("{}") > 0);
    secondModelFile.close();
    rws::EngineeringRequirementsWidget ambiguousWidget;
    ambiguousWidget.setProjectOutputDirectory(projectDirectory.path());
    ambiguousWidget.setProjectModelPath(modelPath);
    ambiguousWidget.setWorkCell(workcell.get());
    REQUIRE(ambiguousWidget.bindGeneratedProjectModel(&error));
    REQUIRE(error.isEmpty());
    REQUIRE(ambiguousWidget.requirementSet().modelBinding.sourcePath == modelPath.toStdString());

    // Bind Model 按钮在项目模型可用时必须直接采用 robot-model.main，不能再弹出
    // 文件选择框让用户从同目录旁车中二次选择。
    rws::EngineeringRequirementsWidget buttonWidget;
    buttonWidget.setProjectOutputDirectory(projectDirectory.path());
    buttonWidget.setProjectModelPath(modelPath);
    buttonWidget.setWorkCell(workcell.get());
    int bindingChanges = 0;
    QObject::connect(&buttonWidget, &rws::EngineeringRequirementsWidget::requirementsChanged,
                     [&]() { ++bindingChanges; });
    bool dialogShown = false;
    QTimer dialogResponder;
    dialogResponder.setInterval(5);
    QObject::connect(&dialogResponder, &QTimer::timeout, [&]() {
        for (QWidget* topLevel : QApplication::topLevelWidgets()) {
            QFileDialog* dialog = qobject_cast<QFileDialog*>(topLevel);
            if (dialog == nullptr || !dialog->isVisible())
                continue;
            dialogShown = true;
            static_cast<QDialog*>(dialog)->reject();
            return;
        }
    });
    QPushButton* bind = buttonWidget.findChild<QPushButton*>("bindRequirementModelButton");
    REQUIRE(bind != nullptr);
    dialogResponder.start();
    bind->click();
    QCoreApplication::processEvents();
    dialogResponder.stop();
    REQUIRE(!dialogShown);
    REQUIRE(bindingChanges == 1);
    REQUIRE(buttonWidget.requirementSet().modelBinding.sourcePath == modelPath.toStdString());
    REQUIRE(buttonWidget.requirementSet().modelBinding.robotModelFingerprint ==
            rws::RobotModelFingerprint::canonicalSha256(model));
    return 0;
}

int testWidgetManualBindingResolvesPortableProjectModelBeforeFreezing()
{
    QTemporaryDir workspace;
    REQUIRE(workspace.isValid());
    const QString projectRoot = workspace.filePath("OriginalProject");
    const QString movedProjectRoot = workspace.filePath("MovedProject");
    const QString modelDirectory = QDir(projectRoot).filePath("generated/robot-models");
    const QString geometryPath = QDir(projectRoot).filePath("assets/robot/base.stl");
    const QString workcellPath = QDir(projectRoot).filePath("scenes/main.wc.xml");
    const QString requirementsPath =
        QDir(projectRoot).filePath("requirements/main.requirements.json");
    REQUIRE(QDir().mkpath(modelDirectory));
    REQUIRE(QDir().mkpath(QFileInfo(geometryPath).absolutePath()));
    REQUIRE(QDir().mkpath(QFileInfo(workcellPath).absolutePath()));
    QFile geometryFile(geometryPath);
    REQUIRE(geometryFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    REQUIRE(geometryFile.write("solid base\nendsolid base\n") > 0);
    geometryFile.close();
    QFile workcellFile(workcellPath);
    REQUIRE(workcellFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    REQUIRE(workcellFile.write("<WorkCell name=\"PortableProjectWorkCell\" />\n") > 0);
    workcellFile.close();

    rws::RobotModelSpec portableModel;
    portableModel.robotName = "PortableProjectRobot";
    rws::DrawableSpec drawable;
    drawable.name = "base-visual";
    drawable.refFrame = "PortableProjectBase";
    drawable.shape = "Mesh";
    drawable.filePath = "assets/robot/base.stl";
    portableModel.drawables.push_back(drawable);
    const QString modelPath = modelDirectory + "/PortableProjectRobot.rmb.json";
    QFile modelFile(modelPath);
    REQUIRE(modelFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QByteArray modelBytes =
        QByteArray::fromStdString(rws::RobotModelSpecJson::toJson(portableModel));
    REQUIRE(modelFile.write(modelBytes) == modelBytes.size());
    modelFile.close();

    rws::RobotModelSpec runtimeModel;
    QString pathError;
    REQUIRE(rws::RobotModelProjectPaths::resolveManaged(
        portableModel, projectRoot, runtimeModel, &pathError));
    REQUIRE(runtimeModel.drawables.front().filePath == geometryPath.toStdString());
    REQUIRE(rws::RobotModelFingerprint::canonicalSha256(portableModel) !=
            rws::RobotModelFingerprint::canonicalSha256(runtimeModel));

    rw::kinematics::StateStructure::Ptr structure =
        rw::core::ownedPtr(new rw::kinematics::StateStructure());
    const rw::kinematics::FixedFrame::Ptr base = rw::core::ownedPtr(
        new rw::kinematics::FixedFrame("PortableProjectBase", rw::math::Transform3D<>()));
    const rw::kinematics::MovableFrame::Ptr tcp = rw::core::ownedPtr(
        new rw::kinematics::MovableFrame("PortableProjectTcp"));
    structure->addFrame(base, structure->getRoot());
    structure->addFrame(tcp, base);
    const rw::models::WorkCell::Ptr workcell = rw::core::ownedPtr(
        new rw::models::WorkCell(
            structure, "PortableProjectWorkCell", workcellPath.toStdString()));
    workcell->addDevice(rw::core::ownedPtr(new rw::models::SerialDevice(
        base.get(), tcp.get(), runtimeModel.robotName, structure->getDefaultState())));

    rws::EngineeringRequirementsWidget widget;
    widget.setProjectOutputDirectory(projectRoot);
    widget.setWorkCell(workcell.get());
    QString error;
    int manualBindingChanges = 0;
    QObject::connect(&widget, &rws::EngineeringRequirementsWidget::requirementsChanged,
                     [&]() { ++manualBindingChanges; });
    bool modelSelected = false;
    QTimer dialogResponder;
    dialogResponder.setInterval(5);
    QObject::connect(&dialogResponder, &QTimer::timeout, [&]() {
        for (QWidget* topLevel : QApplication::topLevelWidgets()) {
            QFileDialog* dialog = qobject_cast<QFileDialog*>(topLevel);
            if (dialog == nullptr || !dialog->isVisible())
                continue;
            dialog->selectFile(modelPath);
            static_cast<QDialog*>(dialog)->accept();
            modelSelected = true;
            return;
        }
    });
    QPushButton* bind = widget.findChild<QPushButton*>("bindRequirementModelButton");
    REQUIRE(bind != nullptr);
    dialogResponder.start();
    bind->click();
    dialogResponder.stop();
    REQUIRE(modelSelected);
    REQUIRE(manualBindingChanges == 1);
    REQUIRE(widget.statusText() ==
            QStringLiteral("Model bound. Requirements track the model content fingerprint."));
    REQUIRE(widget.requirementSet().modelBinding.sourcePath == modelPath.toStdString());
    REQUIRE(widget.requirementSet().modelBinding.robotModelFingerprint ==
            rws::RobotModelFingerprint::canonicalSha256(runtimeModel));

    QPushButton* freeze = widget.findChild<QPushButton*>("freezeRequirementSetButton");
    REQUIRE(freeze != nullptr);
    freeze->click();
    REQUIRE(widget.requirementSet().frozen);

    REQUIRE(widget.saveProjectDocument(requirementsPath, &error));
    QFile savedRequirements(requirementsPath);
    REQUIRE(savedRequirements.open(QIODevice::ReadOnly));
    const QJsonDocument savedDocument = QJsonDocument::fromJson(savedRequirements.readAll());
    REQUIRE(savedDocument.isObject());
    const QJsonObject savedArtifact = savedDocument.object().value("frozenArtifact").toObject();
    REQUIRE(!QFileInfo(savedArtifact.value("modelBinding").toObject().value("sourcePath").toString()).isAbsolute());
    REQUIRE(!QFileInfo(savedArtifact.value("compiledRequirements").toObject()
                       .value("modelBinding").toObject().value("sourcePath").toString()).isAbsolute());
    REQUIRE(!QFileInfo(savedArtifact.value("execution").toObject()
                       .value("provenance").toObject().value("sourcePath").toString()).isAbsolute());
    rws::FrozenRequirementArtifact persistedArtifact;
    std::string persistedArtifactError;
    REQUIRE(rws::FrozenRequirementArtifactJson::fromObject(
        savedArtifact, persistedArtifact, &persistedArtifactError));
    savedRequirements.close();
    REQUIRE(QDir(workspace.path()).rename(
        QStringLiteral("OriginalProject"), QStringLiteral("MovedProject")));

    const QString movedWorkcellPath =
        QDir(movedProjectRoot).filePath("scenes/main.wc.xml");
    const rw::models::WorkCell::Ptr movedWorkcell = rw::core::ownedPtr(
        new rw::models::WorkCell(
            structure, "PortableProjectWorkCell", movedWorkcellPath.toStdString()));
    movedWorkcell->addDevice(rw::core::ownedPtr(new rw::models::SerialDevice(
        base.get(), tcp.get(), runtimeModel.robotName, structure->getDefaultState())));
    rws::EngineeringRequirementsWidget movedWidget;
    movedWidget.setWorkCell(movedWorkcell.get());
    REQUIRE(movedWidget.loadProjectDocument(
        QDir(movedProjectRoot).filePath("requirements/main.requirements.json"),
        &error, movedProjectRoot));
    if (!movedWidget.requirementSet().frozen)
        std::fprintf(stderr, "Moved portable Widget status: %s\n",
                     movedWidget.statusText().toStdString().c_str());
    REQUIRE(movedWidget.requirementSet().frozen);
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

int testWidgetAssignsAndPreservesWorkspaceTcpFrame()
{
    rw::kinematics::StateStructure::Ptr structure =
        rw::core::ownedPtr(new rw::kinematics::StateStructure());
    const rw::kinematics::FixedFrame::Ptr base = rw::core::ownedPtr(
        new rw::kinematics::FixedFrame("WidgetBase", rw::math::Transform3D<>()));
    const rw::kinematics::MovableFrame::Ptr tcp = rw::core::ownedPtr(
        new rw::kinematics::MovableFrame("WidgetTcp"));
    structure->addFrame(base, structure->getRoot());
    structure->addFrame(tcp, base);
    const rw::models::WorkCell::Ptr workcell = rw::core::ownedPtr(
        new rw::models::WorkCell(structure, "WidgetWorkCell", ""));
    workcell->addDevice(rw::core::ownedPtr(new rw::models::SerialDevice(
        base.get(), tcp.get(), "WidgetRobot", structure->getDefaultState())));

    rws::EngineeringRequirementsWidget widget;
    widget.setWorkCell(workcell.get());
    QPushButton* addRegion = widget.findChild<QPushButton*>("addRequirementBoxRegionButton");
    QTableWidget* regionTable = widget.findChild<QTableWidget*>("engineeringRequirementBoxTable");
    REQUIRE(addRegion != nullptr);
    REQUIRE(regionTable != nullptr);

    addRegion->click();
    REQUIRE(widget.requirementSet().boxRegions.size() == 1);
    REQUIRE(widget.requirementSet().boxRegions.front().tcpFrame == "WidgetTcp");

    regionTable->item(0, 11)->setText("9");
    REQUIRE(widget.requirementSet().boxRegions.front().tcpFrame == "WidgetTcp");
    regionTable->item(0, 12)->setText("CustomTcp");
    REQUIRE(widget.requirementSet().boxRegions.front().tcpFrame == "CustomTcp");

    addRegion->click();
    REQUIRE(widget.requirementSet().boxRegions.size() == 2);
    REQUIRE(widget.requirementSet().boxRegions.front().tcpFrame == "CustomTcp");
    REQUIRE(widget.requirementSet().boxRegions.back().tcpFrame == "WidgetTcp");
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
    REQUIRE(coordinates->title() == QStringLiteral("Advanced Pose (Station Frame)"));
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
    if (argc > 1 && std::string(argv[1]) == "workspace_execution_fields") {
        QCoreApplication app(argc, argv);
        return testWorkspaceExecutionFieldsRoundTrip();
    }
    if (argc > 1 && std::string(argv[1]) == "strict_json") {
        QCoreApplication app(argc, argv);
        return testRequirementSetJsonRejectsWrongScalarTypes();
    }
    if (argc > 1 && std::string(argv[1]) == "extensions") {
        QCoreApplication app(argc, argv);
        return testRequirementSetJsonPreservesUnknownFieldsInExtensions();
    }
    if (argc > 1 && std::string(argv[1]) == "must_invalid") {
        QCoreApplication app(argc, argv);
        return testCompilerReportsInvalidMustItemsAndClearsPreviousOutput();
    }
    if (argc > 1 && std::string(argv[1]) == "workspace_validation") {
        QCoreApplication app(argc, argv);
        return testWorkspaceVerificationPolicyValidation();
    }
    // 独立运行的采样上限专项测试：./EngineeringRequirementsTest workspace_sampling_limits
    if (argc > 1 && std::string(argv[1]) == "workspace_sampling_limits") {
        QCoreApplication app(argc, argv);
        return testWorkspaceSamplingLimitsRejectUnboundedWork();
    }
    if (argc > 1 && std::string(argv[1]) == "diagnostic_codes") {
        QCoreApplication app(argc, argv);
        return testStableRequirementDiagnosticCodes();
    }
    if (argc > 1 && std::string(argv[1]) == "migration") {
        QCoreApplication app(argc, argv);
        return testRequirementArtifactV3Migration();
    }
    if (argc > 1 && std::string(argv[1]) == "migration_strict") {
        QCoreApplication app(argc, argv);
        return testRequirementArtifactMigrationRejectsWrongHeaderTypes();
    }
    if (argc > 1 && std::string(argv[1]) == "abi") {
        QCoreApplication app(argc, argv);
        return testHistoricalRequirementFreezerAbiRemainsLinkable();
    }
    if (argc > 1 && std::string(argv[1]) == "relative_source_base") {
        QCoreApplication app(argc, argv);
        return testRelativeSourceWithoutBaseDoesNotReadCurrentDirectory();
    }
    if (argc > 1 && std::string(argv[1]) == "managed_project_root") {
        QApplication app(argc, argv);
        return testWidgetManagedLoadUsesExplicitProjectRoot();
    }
    if (argc > 1 && std::string(argv[1]) == "deferred_frozen_artifact") {
        QApplication app(argc, argv);
        return testWidgetManagedLoadUsesExplicitProjectRoot();
    }
    if (argc > 1 && std::string(argv[1]) == "managed_project_gate") {
        QApplication app(argc, argv);
        return testWidgetBlocksFreezeUntilManagedWorkCellExists();
    }
    if (argc > 1 && std::string(argv[1]) == "widget_project_paths") {
        QApplication app(argc, argv);
        return testWidgetUsesProjectRequirementCopyPathsAndGeneratedDocumentBaseline();
    }
    // 独立运行开关:仅执行"项目关闭清空需求会话"测试,便于快速回归验证,
    // 不必等待整个 widget 子套件全部跑完。
    if (argc > 1 && std::string(argv[1]) == "widget_project_close") {
        QApplication app(argc, argv);
        return testWidgetProjectCloseClearsRequirementSession();
    }
    if (argc > 1 && std::string(argv[1]) == "key_station_toolbar") {
        QApplication app(argc, argv);
        return testWidgetExposesSemanticKeyStationInspector();
    }
    if (argc > 1 && std::string(argv[1]) == "widget_auto_bind") {
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        QApplication app(argc, argv);
        return testWidgetBindsMatchingGeneratedProjectModel();
    }
    if (argc > 1 && std::string(argv[1]) == "widget") {
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        QApplication app(argc, argv);
        if (testWidgetBuildsEngineeringRequirementWorkflow() != 0)
            return 1;
        if (testEngineeringRequirementsWidgetUsesEnglishCopy() != 0)
            return 1;
        if (testWidgetProjectDocumentSnapshotTracksRequirementEdits() != 0)
            return 1;
        if (testWidgetFreezeAndUnfreezeEmitRequirementChanges() != 0)
            return 1;
        if (testWidgetDoesNotRequestPublicationWhenFreezeFails() != 0)
            return 1;
        if (testWidgetUsesProjectRequirementCopyPathsAndGeneratedDocumentBaseline() != 0)
            return 1;
        // 项目关闭/切换必须清空上一项目的需求会话,防止新项目被旧项目需求污染。
        if (testWidgetProjectCloseClearsRequirementSession() != 0)
            return 1;
        if (testWidgetExposesSemanticKeyStationInspector() != 0)
            return 1;
        if (testWidgetResolvesGeometryFeatureUsingLatestJogState() != 0)
            return 1;
        if (testWidgetBindsMatchingGeneratedProjectModel() != 0)
            return 1;
        if (testWidgetManualBindingResolvesPortableProjectModelBeforeFreezing() != 0)
            return 1;
        if (testWidgetPreservesBoxSamplingDensityWhenSynchronizing() != 0)
            return 1;
        if (testWidgetAssignsAndPreservesWorkspaceTcpFrame() != 0)
            return 1;
        if (testWidgetUndoRedoCoversRegularStationAndCoverageEdits() != 0)
            return 1;
        if (testWidgetEditsPointAtTargetCoordinates() != 0)
            return 1;
        return testWidgetAlwaysShowsStationCoordinatesAndLocksRuleOrientation();
    }
    if (argc > 1 && std::string(argv[1]) == "copy") {
        QApplication app(argc, argv);
        return testEngineeringRequirementsWidgetUsesEnglishCopy();
    }
    QCoreApplication app(argc, argv);
    (void)app;
    if (testHistoricalRequirementFreezerAbiRemainsLinkable() != 0)
        return 1;
    if (testFrozenRequirementCompilesOnlyEngineeringTasks() != 0)
        return 1;
    if (testStableRequirementDiagnosticCodes() != 0)
        return 1;
    if (testJsonRoundTripPreservesBindingAndFrozenSnapshot() != 0)
        return 1;
    if (testRequirementSetJsonRejectsWrongScalarTypes() != 0)
        return 1;
    if (testRequirementSetJsonPreservesUnknownFieldsInExtensions() != 0)
        return 1;
    if (testCompilerReportsInvalidMustItemsAndClearsPreviousOutput() != 0)
        return 1;
    if (testRequirementArtifactMigrationRejectsWrongHeaderTypes() != 0)
        return 1;
    if (testFreezerRejectsMissingWorkCellTcpForMustStation() != 0)
        return 1;
    // 环境诊断保留与 TCP 归属校验：非阻塞排除项必须留存、跨设备 TCP 必须被拒绝。
    if (testFreezerRetainsNonBlockingEnvironmentExclusions() != 0)
        return 1;
    if (testFreezerRejectsTcpFromAnotherDevice() != 0)
        return 1;
    if (testFrozenArtifactRoundTripRetainsCompiledEvidence() != 0)
        return 1;
    if (testFrozenArtifactBecomesStaleWhenWorkCellStateChanges() != 0)
        return 1;
    if (testFrozenArtifactWarnsWhenSourceWorkCellFileChanges() != 0)
        return 1;
    if (testFreezeMakesManagedUrScenarioPathsProjectRelative() != 0)
        return 1;
    if (testKeyStationPersistsEngineeringIntentAndCompilesWorkPose() != 0)
        return 1;
    if (testCompilerKeepsNonBlockingStationDiagnosticsOutOfCompiledTasks() != 0)
        return 1;
    // 采样密度安全上限：超限的覆盖盒采样请求必须被详细校验拒绝。
    if (testWorkspaceSamplingLimitsRejectUnboundedWork() != 0)
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
    if (testProjectRequirementCopyPathPolicy() != 0)
        return 1;
    std::puts("All engineering requirements tests passed.");
    return 0;
}
