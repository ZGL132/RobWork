#include "RobotAnalysisTypes.hpp"

#include <cmath>
#include <iostream>
#include <string>

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

    std::cout << "RobotAnalysisCore type test passed." << std::endl;
    return 0;
}
