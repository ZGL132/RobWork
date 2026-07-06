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

    std::cout << "RobotAnalysisCore type and validation test passed." << std::endl;
    return 0;
}
