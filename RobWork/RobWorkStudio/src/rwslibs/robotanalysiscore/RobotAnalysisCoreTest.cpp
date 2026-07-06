#include "RobotAnalysisCsv.hpp"
#include "RobotAnalysisJson.hpp"
#include "RobotAnalysisTypes.hpp"
#include "RobotAnalysisValidation.hpp"

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
}    // namespace

int main ()
{
    rws::TaskPoint point;
    point.id       = "P001";
    point.name     = "Pick";
    point.type     = rws::TaskPointType::Pick;
    point.position = {{0.4, 0.1, 0.2}};
    point.rpyDeg   = {{180.0, 0.0, 90.0}};
    if (point.refFrame != "WORLD")
        return fail ("TaskPoint default refFrame must be WORLD.");
    if (point.tcpFrame != "TCP")
        return fail ("TaskPoint default tcpFrame must be TCP.");
    if (!point.enabled)
        return fail ("TaskPoint should be enabled by default.");
    if (!nearlyEqual (point.tolerance.positionMeters, 0.001))
        return fail ("TaskPoint default position tolerance should be 0.001 m.");

    rws::PayloadSpec payload;
    payload.mass = 2.5;
    payload.cog  = {{0.0, 0.0, 0.1}};
    if (payload.name != "Payload")
        return fail ("PayloadSpec default name must be Payload.");

    rws::RobotDesignContext context;
    context.projectName          = "analysis-core-test";
    context.robotName            = "GenericSixAxis";
    context.modelSpec.robotName  = "GenericSixAxis";
    context.payload              = payload;
    context.taskPoints.push_back (point);
    if (context.baseFrame != "Base")
        return fail ("RobotDesignContext default baseFrame must be Base.");
    if (context.taskPoints.size () != 1)
        return fail ("RobotDesignContext should store task points.");
    if (context.modelSpec.robotName != "GenericSixAxis")
        return fail ("RobotDesignContext should embed RobotModelSpec.");

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
    if (result.jointSummaries.front ().metrics.front ().name != "margin")
        return fail ("AnalysisResult should store joint metrics.");

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

    rws::PayloadSpec invalidPayload = payload;
    invalidPayload.mass            = -0.1;
    const std::vector< rws::AnalysisWarning > invalidPayloadWarnings =
        rws::RobotAnalysisValidation::validatePayload (invalidPayload);
    if (!hasCode (invalidPayloadWarnings, "Payload.Mass.Negative"))
        return fail ("Invalid PayloadSpec should report negative mass.");

    rws::AnalysisResult invalidResult = result;
    invalidResult.score              = 101.0;
    const std::vector< rws::AnalysisWarning > invalidResultWarnings =
        rws::RobotAnalysisValidation::validateAnalysisResult (invalidResult);
    if (!hasCode (invalidResultWarnings, "AnalysisResult.Score.OutOfRange"))
        return fail ("Invalid AnalysisResult should report score outside [0, 100].");

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

    const std::string payloadJson = rws::RobotAnalysisJson::toJson (payload);
    rws::PayloadSpec parsedPayload;
    if (!rws::RobotAnalysisJson::fromJson (payloadJson, parsedPayload))
        return fail ("PayloadSpec JSON should parse successfully.");
    if (parsedPayload.name != payload.name || !nearlyEqual (parsedPayload.mass, payload.mass) ||
        !nearlyEqual (parsedPayload.cog[2], payload.cog[2]))
        return fail ("PayloadSpec JSON round-trip should preserve payload fields.");

    const std::string contextJson = rws::RobotAnalysisJson::toJson (context);
    rws::RobotDesignContext parsedContext;
    if (!rws::RobotAnalysisJson::fromJson (contextJson, parsedContext))
        return fail ("RobotDesignContext JSON should parse successfully.");
    if (parsedContext.projectName != context.projectName ||
        parsedContext.modelSpec.robotName != context.modelSpec.robotName ||
        parsedContext.taskPoints.size () != context.taskPoints.size () ||
        !nearlyEqual (parsedContext.payload.mass, context.payload.mass))
        return fail ("RobotDesignContext JSON round-trip should preserve shared context fields.");

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

    rws::TaskPoint pointWithNote = point;
    pointWithNote.id             = "P002";
    pointWithNote.name           = "Inspect, quoted";
    pointWithNote.type           = rws::TaskPointType::Inspect;
    pointWithNote.position       = {{0.5, 0.2, 0.3}};
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
        !nearlyEqual (parsedCsvPoints[1].position[0], pointWithNote.position[0]) ||
        parsedCsvPoints[1].note != pointWithNote.note)
        return fail ("TaskPoint CSV round-trip should preserve task point fields.");

    std::string csvError;
    if (rws::RobotAnalysisCsv::taskPointsFromCsv ("id,name\nP001", parsedCsvPoints, &csvError))
        return fail ("TaskPoint CSV with an invalid header should fail.");
    if (csvError.empty ())
        return fail ("TaskPoint CSV parse failure should report an error.");

    std::cout << "RobotAnalysisCore type, validation, JSON, and CSV test passed." << std::endl;
    return 0;
}
