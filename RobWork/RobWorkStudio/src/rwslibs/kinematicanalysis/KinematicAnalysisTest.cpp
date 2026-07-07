#include "KinematicAnalysisTypes.hpp"

#include <QCoreApplication>

#include <cmath>
#include <iostream>
#include <string>

static bool nearlyEqual (double lhs, double rhs, double eps = 1e-12)
{
    return std::fabs (lhs - rhs) <= eps;
}

static int fail (const std::string& message)
{
    std::cerr << message << std::endl;
    return 1;
}

static int require (bool condition, const std::string& what)
{
    if (!condition)
        return fail ("requirement failed: " + what);
    return 0;
}

static int assertNear (double actual, double expected, double eps, const std::string& what)
{
    if (!nearlyEqual (actual, expected, eps))
        return fail ("expected " + what + " = " + std::to_string (expected) +
                     " but got " + std::to_string (actual));
    return 0;
}

static int testTypes ()
{
    rws::KinematicThresholds thresholds;
    if (const int rc = assertNear (thresholds.nearJointLimitRatio, 0.05, 1e-12, "nearJointLimitRatio"))
        return rc;
    if (const int rc = assertNear (thresholds.conditionWarning, 100.0, 1e-12, "conditionWarning"))
        return rc;
    if (const int rc = assertNear (thresholds.conditionFail, 1000.0, 1e-12, "conditionFail"))
        return rc;
    if (const int rc = assertNear (thresholds.singularValueWarning, 1e-4, 1e-12, "singularValueWarning"))
        return rc;
    if (const int rc = assertNear (thresholds.manipulabilityWarning, 1e-5, 1e-12, "manipulabilityWarning"))
        return rc;
    if (const int rc =
            assertNear (thresholds.positionToleranceMeters, 0.001, 1e-12, "positionToleranceMeters"))
        return rc;
    if (const int rc =
            assertNear (thresholds.orientationToleranceDeg, 1.0, 1e-12, "orientationToleranceDeg"))
        return rc;

    rws::KinematicAnalysisResult result;
    result.header.pluginName = "KinematicAnalysis";
    result.reachableRate    = 0.75;
    result.workspaceSamples.push_back (rws::WorkspaceSample ());
    rws::KinematicFailureReason reason = rws::KinematicFailureReason::Collision;
    if (reason != rws::KinematicFailureReason::Collision)
        return fail ("KinematicFailureReason equality comparison broke.");

    if (const int rc = require (result.header.pluginName == "KinematicAnalysis", "result.pluginName"))
        return rc;
    if (const int rc = assertNear (result.reachableRate, 0.75, 1e-12, "result.reachableRate"))
        return rc;
    if (const int rc = require (result.workspaceSamples.size () == 1, "result.workspaceSamples size"))
        return rc;
    if (const int rc = require (std::string (rws::toString (rws::KinematicFailureReason::Singular)) ==
                                "Singular",
                                "toString Singular"))
        return rc;
    return 0;
}

static int runAll ()
{
    return testTypes ();
}

int main (int argc, char** argv)
{
    QCoreApplication app (argc, argv);
    const std::string suite = argc > 1 ? argv[1] : "all";
    int rc                 = 0;
    if (suite == "all")
        rc = runAll ();
    else if (suite == "types")
        rc = testTypes ();
    else
        return fail ("Unknown KinematicAnalysis test suite: " + suite);
    if (rc != 0)
        return rc;
    std::cout << "KinematicAnalysis " << suite << " test passed." << std::endl;
    return 0;
}
