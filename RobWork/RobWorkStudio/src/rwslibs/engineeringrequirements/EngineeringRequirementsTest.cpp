#include "EngineeringRequirementTypes.hpp"
#include "GeometryFeatureResolver.hpp"
#include "RequirementCompiler.hpp"
#include "RequirementSetJson.hpp"
#include "RequirementSetUndoStack.hpp"
#include "StationImportService.hpp"
#include "StationTemplateService.hpp"
#include "EngineeringRequirementsWidget.hpp"

#include <rwslibs/robotmodelbuilder/RobotModelXmlWriter.hpp>

#include <rw/core/Ptr.hpp>
#include <rw/kinematics/FixedFrame.hpp>
#include <rw/kinematics/StateStructure.hpp>
#include <rw/math/Constants.hpp>
#include <rw/math/RPY.hpp>
#include <rw/models/WorkCell.hpp>

#include <QCoreApplication>
#include <QApplication>
#include <QPushButton>
#include <QTabWidget>
#include <QDir>

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
    REQUIRE((palletizing & (TemplateParameterRows | TemplateParameterColumns)) == 0U);
    REQUIRE((palletizing & (TemplateParameterLayers | TemplateParameterRowSpacing |
                             TemplateParameterColumnSpacing | TemplateParameterLayerSpacing)) ==
            (TemplateParameterLayers | TemplateParameterRowSpacing |
             TemplateParameterColumnSpacing | TemplateParameterLayerSpacing));

    for (const rws::StationTemplateKind kind : {rws::StationTemplateKind::Inspection,
                                                 rws::StationTemplateKind::ToolChange,
                                                 rws::StationTemplateKind::Handover}) {
        REQUIRE(rws::templateParameterVisibilityMask(kind) == 0U);
    }
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
    if (testGeometryFrameFeatureResolvesAndCompiles() != 0)
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
    std::puts("All engineering requirements tests passed.");
    return 0;
}
