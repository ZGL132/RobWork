#include "EngineeringRequirementTypes.hpp"
#include "RequirementCompiler.hpp"
#include "RequirementSetJson.hpp"
#include "EngineeringRequirementsWidget.hpp"

#include <rwslibs/robotmodelbuilder/RobotModelXmlWriter.hpp>

#include <QCoreApplication>
#include <QApplication>
#include <QPushButton>
#include <QTabWidget>
#include <QDir>

#include <cmath>
#include <cstdio>
#include <string>

namespace {

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
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc > 1 && std::string(argv[1]) == "widget") {
        QApplication app(argc, argv);
        if (testWidgetBuildsEngineeringRequirementWorkflow() != 0)
            return 1;
        return testWidgetExposesSemanticKeyStationInspector();
    }
    QCoreApplication app(argc, argv);
    (void)app;
    if (testFrozenRequirementCompilesOnlyEngineeringTasks() != 0)
        return 1;
    if (testJsonRoundTripPreservesBindingAndFrozenSnapshot() != 0)
        return 1;
    if (testKeyStationPersistsEngineeringIntentAndCompilesWorkPose() != 0)
        return 1;
    if (testCompilerKeepsNonBlockingStationDiagnosticsOutOfCompiledTasks() != 0)
        return 1;
    std::puts("All engineering requirements tests passed.");
    return 0;
}
