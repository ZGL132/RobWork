#include "KinematicAnalysisTypes.hpp"
#include "KinematicMetrics.hpp"

#include <QCoreApplication>

#include <rw/math/Q.hpp>
#include <rw/math/Jacobian.hpp>

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

static int testMetrics ()
{
    using namespace rw::math;
    rws::KinematicThresholds thresholds;

    // Joint-limit margins: bounds [0, 10], q [5] => margin 0.5; q [0.2] => 0.02.
    {
        Q lo(1, 0.0), hi(1, 10.0), q(1, 5.0);
        std::pair< Q, Q > bounds = {lo, hi};
        const std::vector< double > margins = rws::calculateJointLimitMargins (q, bounds);
        if (const int rc = require (margins.size () == 1, "margin size"))
            return rc;
        if (const int rc = assertNear (margins.front (), 0.5, 1e-9, "margin[0.5]"))
            return rc;
        if (const int rc =
                assertNear (rws::minimumJointLimitMargin (margins), 0.5, 1e-9, "min margin[0.5]"))
            return rc;
        std::vector< rws::AnalysisWarning > warnings;
        const rws::AnalysisStatus s = rws::classifyJointLimitMargins (q, bounds, thresholds, &warnings);
        if (const int rc = require (s == rws::AnalysisStatus::Pass, "classify Pass for q=5"))
            return rc;
        if (const int rc = require (warnings.empty (), "no warnings for q=5"))
            return rc;
    }
    {
        Q lo(1, 0.0), hi(1, 10.0), q(1, 0.2);
        std::pair< Q, Q > bounds = {lo, hi};
        const std::vector< double > margins = rws::calculateJointLimitMargins (q, bounds);
        if (const int rc = require (margins.size () == 1, "margin size"))
            return rc;
        if (const int rc = assertNear (margins.front (), 0.02, 1e-9, "margin[0.02]"))
            return rc;
        std::vector< rws::AnalysisWarning > warnings;
        const rws::AnalysisStatus s = rws::classifyJointLimitMargins (q, bounds, thresholds, &warnings);
        if (const int rc = require (s == rws::AnalysisStatus::Warning, "classify Warning for q=0.2"))
            return rc;
        if (const int rc = require (!warnings.empty (), "warning emitted for q=0.2"))
            return rc;
    }
    {
        Q lo(1, 0.0), hi(1, 10.0), q(1, -1.0);
        std::pair< Q, Q > bounds = {lo, hi};
        std::vector< rws::AnalysisWarning > warnings;
        const rws::AnalysisStatus s = rws::classifyJointLimitMargins (q, bounds, thresholds, &warnings);
        if (const int rc = require (s == rws::AnalysisStatus::Fail, "classify Fail outside bounds"))
            return rc;
        if (const int rc = require (!warnings.empty (), "fail warning emitted outside bounds"))
            return rc;
    }

    // SVD: condition 2 / manipulability 8 for diag [4, 2].
    {
        Jacobian j = Jacobian::zero (3, 2);
        // Set up a diagonal-ish 2-row x 2-col proxy by setting two rows to the identity.
        // We use a 3x2 zero Jacobian and only validate condition via direct matrix
        // construction. Cleaner: build a 2x2 Jacobian via Math::zero and assign.
        Jacobian j2(2, 2);
        j2 (0, 0) = 4.0;
        j2 (0, 1) = 0.0;
        j2 (1, 0) = 0.0;
        j2 (1, 1) = 2.0;
        (void) j;
        const rws::SingularMetrics m = rws::calculateSingularMetrics (j2, thresholds);
        if (const int rc =
                require (m.singularValues.size () == 2, "singularValues size"))
            return rc;
        if (const int rc = assertNear (m.singularValues[0], 4.0, 1e-6, "sigmaMax"))
            return rc;
        if (const int rc = assertNear (m.singularValues[1], 2.0, 1e-6, "sigmaMin"))
            return rc;
        if (const int rc = assertNear (m.conditionNumber, 2.0, 1e-6, "conditionNumber"))
            return rc;
        if (const int rc = assertNear (m.manipulability, 8.0, 1e-6, "manipulability"))
            return rc;
        if (const int rc = require (m.status == rws::AnalysisStatus::Pass, "metric status Pass"))
            return rc;
    }
    // SVD: singular config [4, 0] => infinite condition + Fail.
    {
        Jacobian j(2, 2);
        j (0, 0) = 4.0;
        j (0, 1) = 0.0;
        j (1, 0) = 0.0;
        j (1, 1) = 0.0;
        const rws::SingularMetrics m = rws::calculateSingularMetrics (j, thresholds);
        if (const int rc =
                require (std::isinf (m.conditionNumber), "infinite conditionNumber"))
            return rc;
        if (const int rc = require (m.status == rws::AnalysisStatus::Fail, "metric status Fail"))
            return rc;
        if (const int rc =
                require (!m.warnings.empty (), "warning emitted when singular"))
            return rc;
    }
    return 0;
}

static int runAll ()
{
    if (const int rc = testTypes ())
        return rc;
    return testMetrics ();
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
    else if (suite == "metrics")
        rc = testMetrics ();
    else
        return fail ("Unknown KinematicAnalysis test suite: " + suite);
    if (rc != 0)
        return rc;
    std::cout << "KinematicAnalysis " << suite << " test passed." << std::endl;
    return 0;
}
