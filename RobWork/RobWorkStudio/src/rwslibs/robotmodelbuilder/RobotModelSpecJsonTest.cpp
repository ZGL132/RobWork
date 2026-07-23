#include "RobotModelSpecJson.hpp"
#include "RobotModelXmlWriter.hpp"

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

// 浮点近似比较
static bool nearlyEqual (double a, double b, double eps = 1e-12)
{
    return std::fabs (a - b) <= eps;
}

static int fail (const std::string& msg)
{
    std::cerr << "FAIL: " << msg << std::endl;
    return 1;
}

// 深层比较两个 RobotModelSpec 的所有字段
static bool sameRobotModelSpec (const rws::RobotModelSpec& a, const rws::RobotModelSpec& b)
{
    if (a.robotName != b.robotName) return false;
    if (a.saveDirectory != b.saveDirectory) return false;
    if (a.mode != b.mode) return false;
    if (a.exportDhJointsAdvanced != b.exportDhJointsAdvanced) return false;
    if (a.showFrameAxes != b.showFrameAxes) return false;
    if (a.generateDrawables != b.generateDrawables) return false;
    if (a.generateScene != b.generateScene) return false;

    // robotBaseFrame
    if (a.robotBaseFrame.name != b.robotBaseFrame.name) return false;

    // sceneFrames
    if (a.sceneFrames.size () != b.sceneFrames.size ()) return false;
    for (std::size_t i = 0; i < a.sceneFrames.size (); ++i) {
        if (a.sceneFrames[i].name != b.sceneFrames[i].name) return false;
    }

    // transformJoints
    if (a.transformJoints.size () != b.transformJoints.size ()) return false;
    for (std::size_t i = 0; i < a.transformJoints.size (); ++i) {
        if (a.transformJoints[i].name != b.transformJoints[i].name) return false;
        for (int j = 0; j < 3; ++j) {
            if (!nearlyEqual (a.transformJoints[i].pos[j], b.transformJoints[i].pos[j]))
                return false;
        }
    }

    // drawables
    if (a.drawables.size () != b.drawables.size ()) return false;
    for (std::size_t i = 0; i < a.drawables.size (); ++i) {
        if (a.drawables[i].name != b.drawables[i].name) return false;
    }

    // collisionModels
    if (a.collisionModels.size () != b.collisionModels.size ()) return false;

    // limits
    if (a.limits.size () != b.limits.size ()) return false;

    // poses
    if (a.poses.size () != b.poses.size ()) return false;

    // dynamics
    if (a.dynamics.links.size () != b.dynamics.links.size ()) return false;
    if (a.dynamics.forceLimits.size () != b.dynamics.forceLimits.size ()) return false;

    // includes
    if (a.includes.size () != b.includes.size ()) return false;

    // collisionSetup
    if (a.collisionSetup.enabled != b.collisionSetup.enabled) return false;

    // proximitySetup
    if (a.proximitySetup.enabled != b.proximitySetup.enabled) return false;

    // sceneGeometries
    if (a.sceneGeometries.size () != b.sceneGeometries.size ()) return false;

    // dhJoints
    if (a.dhJoints.size () != b.dhJoints.size ()) return false;

    return true;
}

static int testFullRoundTrip ()
{
    rws::RobotModelSpec original =
        rws::RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
    original.robotName = "JsonRoundTrip";
    original.collisionSetup.excludePairs.push_back ({"Joint1", "Joint3"});
    original.proximitySetup.enabled = true;
    original.proximitySetup.rules.push_back (
        {rws::ProximityRuleKind::Exclude, "Joint.*", "Tool.*"});

    const std::string json = rws::RobotModelSpecJson::toJson (original);
    rws::RobotModelSpec decoded;
    std::string error;
    if (!rws::RobotModelSpecJson::fromJson (json, decoded, &error))
        return fail ("RobotModelSpec JSON round trip failed: " + error);
    if (!sameRobotModelSpec (original, decoded))
        return fail ("RobotModelSpec JSON round trip changed at least one field.");
    return 0;
}

int main (int, char**)
{
    if (const int rc = testFullRoundTrip ())
        return rc;
    std::cout << "RobotModelSpecJson round trip test passed." << std::endl;
    return 0;
}
