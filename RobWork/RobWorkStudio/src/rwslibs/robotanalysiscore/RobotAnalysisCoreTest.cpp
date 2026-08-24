#include "RobotAnalysisCsv.hpp"
#include "RobotAnalysisJson.hpp"
#include "RobotAnalysisTypes.hpp"
#include "RobotAnalysisValidation.hpp"
#include "EngineeringEvaluationJson.hpp"
#include "EngineeringEvaluationTypes.hpp"
#include "EngineeringMetricRegistry.hpp"
#include "RequirementExecutionJson.hpp"
#include "RequirementExecutionTypes.hpp"

#include <rwslibs/robotmodelbuilder/RobotModelSpecJson.hpp>

#include <QJsonArray>

#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {
int fail (const std::string& message)
{
    std::cerr << message << std::endl;
    return 1;
}

bool nearlyEqual (const double a, const double b)
{
    return std::fabs (a - b) < 1e-12;
}

bool hasCode (const std::vector< rws::AnalysisWarning >& warnings, const std::string& code)
{
    for (const rws::AnalysisWarning& warning : warnings) {
        if (warning.code == code)
            return true;
    }
    return false;
}

bool contains (const std::string& text, const std::string& fragment)
{
    return text.find (fragment) != std::string::npos;
}

rws::TaskPoint makePoint ()
{
    rws::TaskPoint point;
    point.id       = "P001";
    point.name     = "Pick";
    point.type     = rws::TaskPointType::Pick;
    point.position = {{0.4, 0.1, 0.2}};
    point.rpyDeg   = {{180.0, 0.0, 90.0}};
    return point;
}

rws::PayloadSpec makePayload ()
{
    rws::PayloadSpec payload;
    payload.mass = 2.5;
    payload.cog  = {{0.0, 0.0, 0.1}};
    return payload;
}

rws::RobotDesignContext makeContext ()
{
    rws::RobotDesignContext context;
    context.projectName         = "analysis-core-test";
    context.robotName           = "GenericSixAxis";
    context.modelSpec.robotName = "GenericSixAxis";
    rws::JointTransformSpec baseJoint;
    baseJoint.name = "Base";
    baseJoint.type = "FixedFrame";
    context.modelSpec.transformJoints.push_back (baseJoint);
    rws::JointTransformSpec joint1;
    joint1.name = "Joint1";
    joint1.type = "Revolute";
    context.modelSpec.transformJoints.push_back (joint1);
    context.payload             = makePayload ();
    context.taskPoints.push_back (makePoint ());
    return context;
}

rws::AnalysisResult makeResult ()
{
    rws::AnalysisResult result;
    result.header.pluginName = "RobotAnalysisCoreTest";
    result.status            = rws::AnalysisStatus::Pass;
    result.score             = 100.0;

    rws::JointAnalysisSummary joint;
    joint.jointName = "Joint1";
    joint.status    = rws::AnalysisStatus::Pass;
    rws::MetricValue margin;
    margin.name  = "margin";
    margin.value = 1.25;
    margin.unit  = "ratio";
    joint.metrics.push_back (margin);
    result.jointSummaries.push_back (joint);
    return result;
}

int runTypes ()
{
    const rws::TaskPoint point = makePoint ();
    if (point.refFrame != "WORLD")
        return fail ("TaskPoint default refFrame must be WORLD.");
    if (point.tcpFrame != "TCP")
        return fail ("TaskPoint default tcpFrame must be TCP.");
    if (!point.enabled)
        return fail ("TaskPoint should be enabled by default.");
    if (!nearlyEqual (point.tolerance.positionMeters, 0.001))
        return fail ("TaskPoint default position tolerance should be 0.001 m.");

    const rws::PayloadSpec payload = makePayload ();
    if (payload.name != "Payload")
        return fail ("PayloadSpec default name must be Payload.");

    const rws::RobotDesignContext context = makeContext ();
    if (context.baseFrame != "Base")
        return fail ("RobotDesignContext default baseFrame must be Base.");
    if (context.taskPoints.size () != 1)
        return fail ("RobotDesignContext should store task points.");
    if (context.modelSpec.robotName != "GenericSixAxis")
        return fail ("RobotDesignContext should embed RobotModelSpec.");

    const rws::AnalysisResult result = makeResult ();
    if (result.jointSummaries.front ().metrics.front ().name != "margin")
        return fail ("AnalysisResult should store joint metrics.");
    return 0;
}

int runValidation ()
{
    const rws::TaskPoint point = makePoint ();
    const std::vector< rws::AnalysisWarning > validPointWarnings =
        rws::RobotAnalysisValidation::validateTaskPoint (point);
    if (!validPointWarnings.empty ())
        return fail ("Valid TaskPoint should not emit validation warnings.");

    rws::TaskPoint invalidPoint = point;
    invalidPoint.id.clear ();
    invalidPoint.name.clear ();
    invalidPoint.refFrame.clear ();
    invalidPoint.tcpFrame.clear ();
    invalidPoint.tolerance.positionMeters = -1.0;
    invalidPoint.rpyDeg[1]                = std::numeric_limits< double >::infinity ();
    const std::vector< rws::AnalysisWarning > invalidPointWarnings =
        rws::RobotAnalysisValidation::validateTaskPoint (invalidPoint);
    if (!rws::RobotAnalysisValidation::hasErrors (invalidPointWarnings))
        return fail ("Invalid TaskPoint should emit validation errors.");
    if (!hasCode (invalidPointWarnings, "TaskPoint.Id.Empty"))
        return fail ("Invalid TaskPoint should report empty id.");
    if (!hasCode (invalidPointWarnings, "TaskPoint.Name.Empty"))
        return fail ("Invalid TaskPoint should report empty name.");
    if (!hasCode (invalidPointWarnings, "TaskPoint.RefFrame.Empty"))
        return fail ("Invalid TaskPoint should report empty refFrame.");
    if (!hasCode (invalidPointWarnings, "TaskPoint.TcpFrame.Empty"))
        return fail ("Invalid TaskPoint should report empty tcpFrame.");
    if (!hasCode (invalidPointWarnings, "TaskPoint.Tolerance.PositionNegative"))
        return fail ("Invalid TaskPoint should report negative position tolerance.");
    if (!hasCode (invalidPointWarnings, "TaskPoint.Rpy.NonFinite"))
        return fail ("Invalid TaskPoint should report non-finite RPY.");

    rws::PayloadSpec invalidPayload = makePayload ();
    invalidPayload.mass            = -0.1;
    const std::vector< rws::AnalysisWarning > invalidPayloadWarnings =
        rws::RobotAnalysisValidation::validatePayload (invalidPayload);
    if (!hasCode (invalidPayloadWarnings, "Payload.Mass.Negative"))
        return fail ("Invalid PayloadSpec should report negative mass.");

    rws::AnalysisResult invalidResult = makeResult ();
    invalidResult.score              = 101.0;
    const std::vector< rws::AnalysisWarning > invalidResultWarnings =
        rws::RobotAnalysisValidation::validateAnalysisResult (invalidResult);
    if (!hasCode (invalidResultWarnings, "AnalysisResult.Score.OutOfRange"))
        return fail ("Invalid AnalysisResult should report score outside [0, 100].");

    const rws::RobotDesignContext context = makeContext ();
    const std::vector< rws::AnalysisWarning > validContextWarnings =
        rws::RobotAnalysisValidation::validateRobotDesignContext (context);
    if (!validContextWarnings.empty ())
        return fail ("Valid RobotDesignContext should not emit validation warnings.");

    rws::RobotDesignContext invalidContext = context;
    invalidContext.baseFrame.clear ();
    invalidContext.tcpFrame.clear ();
    invalidContext.refFrame.clear ();
    const std::vector< rws::AnalysisWarning > invalidContextWarnings =
        rws::RobotAnalysisValidation::validateRobotDesignContext (invalidContext);
    if (!hasCode (invalidContextWarnings, "RobotDesignContext.BaseFrame.Empty"))
        return fail ("Invalid RobotDesignContext should report empty baseFrame.");
    if (!hasCode (invalidContextWarnings, "RobotDesignContext.TcpFrame.Empty"))
        return fail ("Invalid RobotDesignContext should report empty tcpFrame.");
    if (!hasCode (invalidContextWarnings, "RobotDesignContext.RefFrame.Empty"))
        return fail ("Invalid RobotDesignContext should report empty refFrame.");
    return 0;
}

int runJson ()
{
    const rws::TaskPoint point   = makePoint ();
    const std::string pointJson = rws::RobotAnalysisJson::toJson (point);
    if (!contains (pointJson, "\"id\":\"P001\""))
        return fail ("TaskPoint JSON should contain the task point id.");
    rws::TaskPoint parsedPoint;
    if (!rws::RobotAnalysisJson::fromJson (pointJson, parsedPoint))
        return fail ("TaskPoint JSON should parse successfully.");
    if (parsedPoint.id != point.id || parsedPoint.name != point.name ||
        parsedPoint.type != point.type || !nearlyEqual (parsedPoint.position[0], point.position[0]) ||
        !nearlyEqual (parsedPoint.rpyDeg[2], point.rpyDeg[2]))
        return fail ("TaskPoint JSON round-trip should preserve core fields.");

    const rws::PayloadSpec payload = makePayload ();
    const std::string payloadJson  = rws::RobotAnalysisJson::toJson (payload);
    rws::PayloadSpec parsedPayload;
    if (!rws::RobotAnalysisJson::fromJson (payloadJson, parsedPayload))
        return fail ("PayloadSpec JSON should parse successfully.");
    if (parsedPayload.name != payload.name || !nearlyEqual (parsedPayload.mass, payload.mass) ||
        !nearlyEqual (parsedPayload.cog[2], payload.cog[2]))
        return fail ("PayloadSpec JSON round-trip should preserve payload fields.");

    rws::RobotDesignContext context = makeContext ();
    context.sourceModelPath         = "legacy-model.rmb.json";
    context.modelProvenance         = {"model.rmb.json", "source-sha", "snapshot-sha"};
    const std::string contextJson         = rws::RobotAnalysisJson::toJson (context);
    rws::RobotDesignContext parsedContext;
    if (!rws::RobotAnalysisJson::fromJson (contextJson, parsedContext))
        return fail ("RobotDesignContext JSON should parse successfully.");
    if (parsedContext.projectName != context.projectName ||
        parsedContext.modelSpec.robotName != context.modelSpec.robotName ||
        parsedContext.taskPoints.size () != context.taskPoints.size () ||
        !nearlyEqual (parsedContext.payload.mass, context.payload.mass))
        return fail ("RobotDesignContext JSON round-trip should preserve shared context fields.");
    if (parsedContext.modelProvenance.sourceModelPath != "model.rmb.json" ||
        parsedContext.modelProvenance.sourceFingerprint != "source-sha" ||
        parsedContext.modelProvenance.snapshotFingerprint != "snapshot-sha" ||
        parsedContext.sourceModelPath != "model.rmb.json")
        return fail ("RobotDesignContext JSON should preserve model provenance.");

    const std::string legacyContextJson =
        "{\"type\":\"RobotDesignContext\",\"data\":{\"projectName\":\"legacy\","
        "\"sourceModelPath\":\"legacy-model.rmb.json\"}}";
    rws::RobotDesignContext legacyContext;
    if (!rws::RobotAnalysisJson::fromJson (legacyContextJson, legacyContext))
        return fail ("Legacy RobotDesignContext JSON should parse successfully.");
    if (legacyContext.sourceModelPath != "legacy-model.rmb.json" ||
        !legacyContext.modelProvenance.sourceFingerprint.empty () ||
        !legacyContext.modelProvenance.snapshotFingerprint.empty () ||
        legacyContext.modelProvenance.sourceModelPath != "legacy-model.rmb.json")
        return fail ("Legacy RobotDesignContext JSON should leave provenance fingerprints empty.");

    rws::AnalysisResult result = makeResult ();
    rws::AnalysisWarning warning;
    warning.code     = "W001";
    warning.message  = "joint margin low";
    warning.source   = "Joint1";
    warning.severity = rws::AnalysisStatus::Warning;
    result.warnings.push_back (warning);
    const std::string resultJson = rws::RobotAnalysisJson::toJson (result);
    rws::AnalysisResult parsedResult;
    if (!rws::RobotAnalysisJson::fromJson (resultJson, parsedResult))
        return fail ("AnalysisResult JSON should parse successfully.");
    if (parsedResult.header.pluginName != result.header.pluginName ||
        parsedResult.status != result.status || !nearlyEqual (parsedResult.score, result.score) ||
        parsedResult.jointSummaries.size () != result.jointSummaries.size () ||
        parsedResult.warnings.size () != result.warnings.size ())
        return fail ("AnalysisResult JSON round-trip should preserve result fields.");

    std::string parseError;
    if (rws::RobotAnalysisJson::fromJson ("{not-json", parsedPoint, &parseError))
        return fail ("Invalid JSON should fail to parse.");
    if (parseError.empty ())
        return fail ("Invalid JSON should report a parse error.");
    // 非对象根(此处为数组)的 JSON 必须在结构层就被拒绝，并给出含 "object" 的错误，
    // 而不是被当作缺失字段的顶层对象继续解析。
    parseError.clear ();
    rws::RequirementExecutionSet restoredExecution;
    if (rws::RequirementExecutionJson::fromJson ("[]", restoredExecution, &parseError) ||
        parseError.find ("object") == std::string::npos)
        return fail ("Non-object requirement execution JSON should report a structural error.");
    return 0;
}

int runCsv ()
{
    const rws::TaskPoint point = makePoint ();
    rws::TaskPoint pointWithNote = point;
    pointWithNote.id             = "P002";
    pointWithNote.name           = "Inspect, quoted";
    pointWithNote.type           = rws::TaskPointType::Inspect;
    pointWithNote.refFrame       = "FixtureA";
    pointWithNote.tcpFrame       = "ToolTip";
    pointWithNote.position       = {{0.5, 0.2, 0.3}};
    pointWithNote.tolerance.allowToolRollFree = true;
    pointWithNote.weight         = 2.5;
    pointWithNote.enabled        = false;
    pointWithNote.note           = "requires \"fine\" alignment";
    const std::vector< rws::TaskPoint > csvPoints = {point, pointWithNote};
    const std::string csv = rws::RobotAnalysisCsv::taskPointsToCsv (csvPoints);
    if (!contains (csv, "id,name,type,refFrame,tcpFrame,x,y,z,rollDeg,pitchDeg,yawDeg"))
        return fail ("TaskPoint CSV should contain a stable header.");
    if (!contains (csv, "\"Inspect, quoted\""))
        return fail ("TaskPoint CSV should quote fields containing commas.");
    if (!contains (csv, "\"requires \"\"fine\"\" alignment\""))
        return fail ("TaskPoint CSV should escape quotes inside quoted fields.");

    std::vector< rws::TaskPoint > parsedCsvPoints;
    if (!rws::RobotAnalysisCsv::taskPointsFromCsv (csv, parsedCsvPoints))
        return fail ("TaskPoint CSV should parse successfully.");
    if (parsedCsvPoints.size () != csvPoints.size ())
        return fail ("TaskPoint CSV round-trip should preserve row count.");
    if (parsedCsvPoints[1].id != pointWithNote.id || parsedCsvPoints[1].name != pointWithNote.name ||
        parsedCsvPoints[1].type != pointWithNote.type ||
        parsedCsvPoints[1].refFrame != pointWithNote.refFrame ||
        parsedCsvPoints[1].tcpFrame != pointWithNote.tcpFrame ||
        !nearlyEqual (parsedCsvPoints[1].position[0], pointWithNote.position[0]) ||
        parsedCsvPoints[1].tolerance.allowToolRollFree !=
            pointWithNote.tolerance.allowToolRollFree ||
        !nearlyEqual (parsedCsvPoints[1].weight, pointWithNote.weight) ||
        parsedCsvPoints[1].enabled != pointWithNote.enabled ||
        parsedCsvPoints[1].note != pointWithNote.note)
        return fail ("TaskPoint CSV round-trip should preserve task point fields.");

    std::string csvError;
    if (rws::RobotAnalysisCsv::taskPointsFromCsv ("id,name\nP001", parsedCsvPoints, &csvError))
        return fail ("TaskPoint CSV with an invalid header should fail.");
    if (csvError.empty ())
        return fail ("TaskPoint CSV parse failure should report an error.");
    return 0;
}

int runContextFullModel ()
{
    rws::RobotDesignContext context = makeContext ();

    rws::JointTransformSpec joint;
    joint.name   = "Joint1";
    joint.type   = "Revolute";
    joint.rpyDeg = {{0, 0, 0}};
    joint.pos    = {{0, 0, 0.3}};
    context.modelSpec.transformJoints.push_back (joint);

    rws::JointTransformSpec joint2;
    joint2.name   = "Joint2";
    joint2.type   = "Revolute";
    joint2.rpyDeg = {{90, 0, 0}};
    joint2.pos    = {{0, 0, 0}};
    context.modelSpec.transformJoints.push_back (joint2);

    context.modelSpec.showFrameAxes = true;

    const std::string contextJson = rws::RobotAnalysisJson::toJson (context);
    if (!contains (contextJson, "\"modelSpec\""))
        return fail ("RobotDesignContext JSON should contain modelSpec key.");

    rws::RobotDesignContext parsedContext;
    if (!rws::RobotAnalysisJson::fromJson (contextJson, parsedContext))
        return fail ("RobotDesignContext with full modelSpec should parse successfully.");

    if (parsedContext.modelSpec.transformJoints.size () != 4)
        return fail ("RobotDesignContext JSON round-trip should preserve transformJoints count.");
    if (parsedContext.modelSpec.transformJoints[2].name != "Joint1" ||
        parsedContext.modelSpec.transformJoints[3].name != "Joint2")
        return fail ("RobotDesignContext JSON round-trip should preserve joint names.");
    if (!parsedContext.modelSpec.showFrameAxes)
        return fail ("RobotDesignContext JSON round-trip should preserve showFrameAxes flag.");

    return 0;
}

int runContextMissingModel ()
{
    rws::RobotDesignContext context;
    context.projectName = "empty-ms-test";
    context.robotName   = "NoModel";
    context.baseFrame   = "Base";
    context.tcpFrame    = "TCP";
    context.refFrame    = "WORLD";
    context.payload     = makePayload ();
    context.taskPoints.push_back (makePoint ());

    const std::vector< rws::AnalysisWarning > warnings =
        rws::RobotAnalysisValidation::validateRobotDesignContext (context);
    if (!hasCode (warnings, "RobotDesignContext.ModelSpec.Incomplete"))
        return fail ("Context with empty modelSpec should report ModelSpec.Incomplete warning.");

    return 0;
}

int runEngineeringEvaluation ()
{
    const rws::EngineeringMetricRegistry& registry =
        rws::EngineeringMetricRegistry::standard ();
    if (registry.find ("kinematics.reachability.weighted") == nullptr)
        return fail ("Standard metric registry should include weighted reachability.");

    rws::EngineeringEvaluationResult result;
    result.providerId = "test.kinematics";
    result.providerVersion = "1.0";
    result.status = rws::EngineeringEvaluationStatus::Success;
    result.elapsedSeconds = 0.125;
    result.inputSnapshot.modelHash = "model-a";
    result.inputSnapshot.configurationHash = "config-a";
    result.metrics.push_back ({"kinematics.reachability.weighted", 1.0, "ratio",
                               rws::EngineeringMetricStatus::Valid, "test.kinematics"});
    result.constraints.push_back ({"kinematics.reachability.weighted", ">=", 1.0, 1.0,
                                   true, true, ""});
    result.artifacts.push_back ({"ik.solutions", "application/json", "{}"});
    result.warnings.push_back ({"Kinematics.NearLimit", "Joint is near its limit.",
                                "test.kinematics", rws::AnalysisStatus::Warning});

    std::vector< rws::AnalysisWarning > warnings;
    if (!registry.validate (result, &warnings) || !warnings.empty ())
        return fail ("A valid engineering result should pass registry validation.");

    const std::string json = rws::EngineeringEvaluationJson::toJson (result);
    rws::EngineeringEvaluationResult decoded;
    std::string error;
    if (!rws::EngineeringEvaluationJson::fromJson (json, decoded, &error))
        return fail ("Engineering evaluation JSON should round trip: " + error);
    if (decoded.metrics.size () != 1 || decoded.metrics.front ().metricId !=
                                            "kinematics.reachability.weighted" ||
        decoded.artifacts.size () != 1 || decoded.warnings.size () != 1 ||
        decoded.warnings.front ().code != "Kinematics.NearLimit" ||
        std::abs (decoded.elapsedSeconds - 0.125) > 1e-12 ||
        decoded.inputSnapshot.modelHash != "model-a")
        return fail ("Engineering evaluation JSON changed the result payload.");

    rws::EngineeringEvaluationResult invalid = result;
    invalid.metrics.push_back (invalid.metrics.front ());
    if (registry.validate (invalid, &warnings) ||
        !hasCode (warnings, "EngineeringMetric.DuplicateId"))
        return fail ("Duplicate metric IDs should be rejected.");

    invalid = result;
    invalid.metrics.front ().metricId = "unknown.metric";
    if (registry.validate (invalid, &warnings) ||
        !hasCode (warnings, "EngineeringMetric.UnknownId"))
        return fail ("Unknown metric IDs should be rejected.");

    invalid = result;
    invalid.metrics.front ().unit = "N";
    if (registry.validate (invalid, &warnings) ||
        !hasCode (warnings, "EngineeringMetric.UnitMismatch"))
        return fail ("Metric units must match the registered metric.");

    invalid = result;
    invalid.metrics.front ().value = std::numeric_limits< double >::infinity ();
    if (registry.validate (invalid, &warnings) ||
        !hasCode (warnings, "EngineeringMetric.Value.NonFinite"))
        return fail ("Non-finite metric values should be rejected.");

    return 0;
}

int runRequirementExecution ()
{
    rws::RequirementExecutionSet value;
    value.schemaVersion = 1;
    value.provenance.requirementFingerprint = "req-sha";
    value.provenance.robotModelFingerprint = "model-sha";
    value.provenance.environmentFingerprint = "env-sha";
    value.provenance.compilerVersion = "EngineeringRequirements.Compiler.4";

    rws::RequirementExecutionTask task;
    task.id = "task-1";
    task.processType = rws::RequirementExecutionProcessType::Pick;
    task.level = rws::RequirementExecutionLevel::Must;
    task.refFrame = "WORLD";
    task.tcpFrame = "TCP";
    task.position = {{0.1, 0.2, 0.3}};
    task.rpyDeg = {{0.0, 90.0, 0.0}};
    task.approach.enabled = true;
    task.approach.axis = rws::RequirementExecutionOffsetAxis::ReferenceZ;
    task.approach.distanceMeters = 0.15;
    task.approach.collisionFreeRequired = false;
    task.retract.enabled = true;
    task.retract.axis = rws::RequirementExecutionOffsetAxis::ToolZ;
    task.retract.distanceMeters = 0.2;
    task.pathValidationPending = true;
    value.tasks.push_back (task);

    rws::RequirementExecutionRegion region;
    region.id = "region-1";
    region.level = rws::RequirementExecutionLevel::Must;
    region.refFrame = "WORLD";
    region.tcpFrame = "TCP";
    region.center = {{0.1, 0.2, 0.3}};
    region.size = {{0.2, 0.2, 0.2}};
    region.sampleSpacingMeters = {{0.1, 0.1, 0.1}};
    region.sampleCounts = {{3, 3, 3}};
    region.orientationMode = rws::RequirementExecutionOrientationMode::Fixed;
    region.orientationTargetFrame = "FixtureFrame";
    region.orientationTargetGeometry = "frame:FixtureFrame";
    region.orientationTargetPoint = "0.1,0.2,0.3";
    region.minimumVerificationStage = rws::RequirementExecutionStage::Verified;
    value.workspaceRegions.push_back (region);

    rws::RequirementExecutionDiagnostic diagnostic;
    diagnostic.code = "REQ_OPTIONAL_ITEM_EXCLUDED";
    diagnostic.requirementId = "optional-1";
    diagnostic.severity = rws::RequirementExecutionDiagnosticSeverity::Warning;
    diagnostic.message = "Optional item was excluded.";
    value.diagnostics.push_back (diagnostic);

    const QJsonObject object = rws::RequirementExecutionJson::toObject (value);
    if (object.value ("schemaVersion").toInt () != 1)
        return fail ("Requirement execution JSON should preserve schemaVersion.");

    QJsonObject extensible = object;
    extensible["futureTopLevel"] = QJsonObject{{"revision", 7}};
    QJsonArray extensibleTasks = extensible.value("tasks").toArray();
    QJsonObject extensibleTask = extensibleTasks.at(0).toObject();
    extensibleTask["futureTaskField"] = QJsonArray{1, 2, 3};
    extensibleTasks[0] = extensibleTask;
    extensible["tasks"] = extensibleTasks;
    QJsonArray extensibleRegions = extensible.value("workspaceRegions").toArray();
    QJsonObject extensibleRegion = extensibleRegions.at(0).toObject();
    extensibleRegion["futureRegionField"] = true;
    extensibleRegions[0] = extensibleRegion;
    extensible["workspaceRegions"] = extensibleRegions;
    rws::RequirementExecutionSet extensibleRestored;
    std::string extensionsError;
    if (!rws::RequirementExecutionJson::fromObject(extensible, extensibleRestored, &extensionsError))
        return fail("Requirement execution extensions should parse: " + extensionsError);
    const QJsonObject extensionsRoundTrip = rws::RequirementExecutionJson::toObject(extensibleRestored);
    if (extensionsRoundTrip.value("extensions").toObject().value("futureTopLevel").toObject()
            .value("revision").toInt() != 7 ||
        extensionsRoundTrip.value("tasks").toArray().at(0).toObject()
            .value("extensions").toObject().value("futureTaskField").toArray().size() != 3 ||
        !extensionsRoundTrip.value("workspaceRegions").toArray().at(0).toObject()
            .value("extensions").toObject().value("futureRegionField").toBool())
        return fail("Requirement execution JSON should preserve unknown fields in extensions.");

    QJsonObject conflictingExtensions = extensible;
    conflictingExtensions["extensions"] = QJsonObject{
        {"futureTopLevel", QJsonObject{{"revision", 8}}}};
    if (rws::RequirementExecutionJson::fromObject(
            conflictingExtensions, extensibleRestored, &extensionsError) ||
        extensionsError.find("conflicts") == std::string::npos)
        return fail("Requirement execution JSON should reject conflicting extension fields.");

    rws::RequirementExecutionSet restored;
    std::string error;
    if (!rws::RequirementExecutionJson::fromObject (object, restored, &error))
        return fail ("Requirement execution JSON should round trip: " + error);
    if (restored.tasks.size () != 1 || restored.workspaceRegions.size () != 1 ||
        restored.diagnostics.size () != 1 || restored.tasks.front ().id != "task-1" ||
        restored.tasks.front ().processType != rws::RequirementExecutionProcessType::Pick ||
        restored.workspaceRegions.front ().sampleCounts != std::array<int, 3>{{3, 3, 3}} ||
        !restored.tasks.front ().approach.enabled ||
        restored.tasks.front ().approach.axis != rws::RequirementExecutionOffsetAxis::ReferenceZ ||
        restored.tasks.front ().approach.distanceMeters != 0.15 ||
        !restored.tasks.front ().retract.enabled ||
        !restored.tasks.front ().pathValidationPending ||
        restored.workspaceRegions.front ().orientationTargetFrame != "FixtureFrame" ||
        restored.workspaceRegions.front ().orientationTargetGeometry != "frame:FixtureFrame" ||
        restored.workspaceRegions.front ().orientationTargetPoint != "0.1,0.2,0.3")
        return fail ("Requirement execution JSON should preserve task, region and diagnostics.");

    const std::string fingerprint = rws::RequirementExecutionJson::fingerprint (value);
    if (fingerprint.empty () || fingerprint !=
        rws::RequirementExecutionJson::fingerprint (restored))
        return fail ("Requirement execution fingerprint should be stable across JSON round trips.");
    rws::RequirementExecutionSet tampered = restored;
    tampered.tasks.front ().position[0] += 0.01;
    if (rws::RequirementExecutionJson::fingerprint (tampered) == fingerprint)
        return fail ("Requirement execution fingerprint should change when execution data changes.");

    // Quick screening may deliberately use a single sample on an axis.  The
    // execution contract must not apply the Verified-only minimum of two.
    value.workspaceRegions.front ().minimumVerificationStage =
        rws::RequirementExecutionStage::Quick;
    value.workspaceRegions.front ().sampleCounts = {{1, 1, 1}};
    const QJsonObject quickObject = rws::RequirementExecutionJson::toObject (value);
    if (!rws::RequirementExecutionJson::fromObject (quickObject, restored, &error) ||
        restored.workspaceRegions.front ().sampleCounts != std::array<int, 3>{{1, 1, 1}} ||
        restored.workspaceRegions.front ().minimumVerificationStage !=
            rws::RequirementExecutionStage::Quick)
        return fail ("Quick requirement execution regions should allow one sample per axis.");

    QJsonObject invalid = object;
    invalid["workspaceRegions"] = QJsonArray {QJsonObject {{"id", "bad"},
                                                            {"orientationMode", "Unknown"}}};
    if (rws::RequirementExecutionJson::fromObject (invalid, restored, &error))
        return fail ("Unknown requirement execution enum should be rejected.");
    if (error.empty ())
        return fail ("Unknown requirement execution enum should report an error.");

    QJsonObject invalidStructure = object;
    invalidStructure["tasks"] = QJsonArray {QJsonObject {
        {"id", ""}, {"position", QJsonArray {0.0, 0.0, 0.0}},
        {"rpyDeg", QJsonArray {0.0, 0.0, 0.0}},
        {"positionToleranceMeters", -0.1}}};
    if (rws::RequirementExecutionJson::fromObject (invalidStructure, restored, &error) ||
        error.find ("id") == std::string::npos)
        return fail ("Invalid execution task structure should be rejected with an id diagnostic.");

    QJsonObject unsupportedSchema = object;
    unsupportedSchema["schemaVersion"] = 2;
    if (rws::RequirementExecutionJson::fromObject (unsupportedSchema, restored, &error) ||
        error.find ("schemaVersion") == std::string::npos)
        return fail ("Unsupported execution schema versions should be rejected explicitly.");

    // 结构严格性：缺失顶层 tasks/workspaceRegions 数组必须被拒绝。
    QJsonObject missingTasks = object;
    missingTasks.remove ("tasks");
    if (rws::RequirementExecutionJson::fromObject (missingTasks, restored, &error) ||
        error.find ("tasks") == std::string::npos)
        return fail ("Requirement execution JSON should reject a missing tasks array.");

    QJsonObject missingRegions = object;
    missingRegions.remove ("workspaceRegions");
    if (rws::RequirementExecutionJson::fromObject (missingRegions, restored, &error) ||
        error.find ("workspaceRegions") == std::string::npos)
        return fail ("Requirement execution JSON should reject a missing workspaceRegions array.");

    // 采样上限：逐轴网格数超出 MaxExecutionWorkspaceSamplesPerAxis 必须被拒绝。
    QJsonObject oversized = object;
    QJsonArray oversizedRegions = oversized.value ("workspaceRegions").toArray ();
    QJsonObject oversizedRegion = oversizedRegions.at (0).toObject ();
    oversizedRegion["sampleCounts"] = QJsonArray {rws::MaxExecutionWorkspaceSamplesPerAxis + 1,
                                                   3, 3};
    oversizedRegions.replace (0, oversizedRegion);
    oversized["workspaceRegions"] = oversizedRegions;
    if (rws::RequirementExecutionJson::fromObject (oversized, restored, &error) ||
        error.find ("invalid values") == std::string::npos)
        return fail ("Requirement execution JSON should reject oversized workspace sampling.");

    // 顶层 type/schemaVersion 必须显式存在(不允许缺省回填)。
    QJsonObject missingType = object;
    missingType.remove ("type");
    if (rws::RequirementExecutionJson::fromObject (missingType, restored, &error) ||
        error.find ("type") == std::string::npos)
        return fail ("Requirement execution JSON should require an explicit type.");

    QJsonObject missingSchema = object;
    missingSchema.remove ("schemaVersion");
    if (rws::RequirementExecutionJson::fromObject (missingSchema, restored, &error) ||
        error.find ("schemaVersion") == std::string::npos)
        return fail ("Requirement execution JSON should require an explicit schemaVersion.");

    // provenance 的每个成员都必须显式存在，缺一个即拒绝。
    QJsonObject missingProvenanceMember = object;
    QJsonObject incompleteProvenance = missingProvenanceMember.value ("provenance").toObject();
    incompleteProvenance.remove ("workcellFingerprint");
    missingProvenanceMember["provenance"] = incompleteProvenance;
    if (rws::RequirementExecutionJson::fromObject (missingProvenanceMember, restored, &error) ||
        error.find ("provenance") == std::string::npos)
        return fail ("Requirement execution JSON should require every provenance member.");

    // 数组元素类型错误：position 数组混入非数字元素必须被拒绝，并指出 "position"。
    QJsonObject wrongArrayType = object;
    QJsonArray wrongPosition = wrongArrayType.value ("tasks").toArray ().at (0).toObject ()
        .value ("position").toArray ();
    wrongPosition.replace (1, "not-a-number");
    QJsonObject wrongTask = wrongArrayType.value ("tasks").toArray ().at (0).toObject ();
    wrongTask["position"] = wrongPosition;
    QJsonArray wrongTasks = wrongArrayType.value ("tasks").toArray ();
    wrongTasks.replace (0, wrongTask);
    wrongArrayType["tasks"] = wrongTasks;
    if (rws::RequirementExecutionJson::fromObject (wrongArrayType, restored, &error) ||
        error.find ("position") == std::string::npos)
        return fail ("Requirement execution JSON should reject wrong-typed array values.");

    // 网格计数数组字段类型错误：混入字符串必须被拒绝，并指出 "sampleCounts"。
    QJsonObject wrongScalarType = object;
    QJsonArray wrongRegions = wrongScalarType.value ("workspaceRegions").toArray ();
    QJsonObject wrongRegion = wrongRegions.at (0).toObject ();
    wrongRegion["sampleCounts"] = QJsonArray {3, "bad", 3};
    wrongRegions.replace (0, wrongRegion);
    wrongScalarType["workspaceRegions"] = wrongRegions;
    if (rws::RequirementExecutionJson::fromObject (wrongScalarType, restored, &error) ||
        error.find ("sampleCounts") == std::string::npos)
        return fail ("Requirement execution JSON should reject wrong-typed grid values.");

    // 方向样本数上限独立生效：即使逐轴网格数合法，directionSamples 超限也必须拒绝。
    QJsonObject independentDirectionLimit = object;
    QJsonArray directionRegions = independentDirectionLimit.value ("workspaceRegions").toArray ();
    QJsonObject directionRegion = directionRegions.at (0).toObject ();
    directionRegion["directionSamples"] = rws::MaxExecutionWorkspaceDirectionSamples + 1;
    directionRegions.replace (0, directionRegion);
    independentDirectionLimit["workspaceRegions"] = directionRegions;
    if (rws::RequirementExecutionJson::fromObject (independentDirectionLimit, restored, &error) ||
        error.find ("invalid values") == std::string::npos)
        return fail ("Requirement execution JSON should enforce direction sample limits independently.");

    // 翻滚样本数上限同样独立生效。
    QJsonObject independentRollLimit = object;
    QJsonArray rollRegions = independentRollLimit.value ("workspaceRegions").toArray ();
    QJsonObject rollRegion = rollRegions.at (0).toObject ();
    rollRegion["rollSamples"] = rws::MaxExecutionWorkspaceRollSamples + 1;
    rollRegions.replace (0, rollRegion);
    independentRollLimit["workspaceRegions"] = rollRegions;
    if (rws::RequirementExecutionJson::fromObject (independentRollLimit, restored, &error) ||
        error.find ("invalid values") == std::string::npos)
        return fail ("Requirement execution JSON should enforce roll sample limits independently.");
    return 0;
}

int runAll ()
{
    if (const int rc = runTypes ())
        return rc;
    if (const int rc = runValidation ())
        return rc;
    if (const int rc = runJson ())
        return rc;
    if (const int rc = runCsv ())
        return rc;
    if (const int rc = runContextFullModel ())
        return rc;
    if (const int rc = runContextMissingModel ())
        return rc;
    return runEngineeringEvaluation ();
}
}    // namespace

int main (int argc, char** argv)
{
    const std::string suite = argc > 1 ? argv[1] : "all";
    int rc                 = 0;
    if (suite == "all")
        rc = runAll ();
    else if (suite == "types")
        rc = runTypes ();
    else if (suite == "validation")
        rc = runValidation ();
    else if (suite == "json")
        rc = runJson ();
    else if (suite == "csv")
        rc = runCsv ();
    else if (suite == "requirementExecution")
        rc = runRequirementExecution ();
    else if (suite == "contextFullModel")
        rc = runContextFullModel ();
    else if (suite == "contextMissingModel")
        rc = runContextMissingModel ();
    else if (suite == "engineering")
        rc = runEngineeringEvaluation ();
    else
        return fail ("Unknown RobotAnalysisCore test suite: " + suite);

    if (rc != 0)
        return rc;
    std::cout << "RobotAnalysisCore " << suite << " test passed." << std::endl;
    return 0;
}
