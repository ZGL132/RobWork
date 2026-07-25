#include "RobotModelSpecJson.hpp"
#include "RobotModelXmlWriter.hpp"

#include <QDir>
#include <QJsonArray>
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
    if (a.imported.active != b.imported.active) return false;
    if (a.imported.sourceSceneFile != b.imported.sourceSceneFile) return false;
    if (a.imported.sourceDeviceFile != b.imported.sourceDeviceFile) return false;
    if (a.imported.sourceCollisionSetupFile != b.imported.sourceCollisionSetupFile) return false;
    if (a.imported.sourceProximitySetupFile != b.imported.sourceProximitySetupFile) return false;
    if (a.imported.workcellExtensions != b.imported.workcellExtensions) return false;
    if (a.imported.deviceExtensions != b.imported.deviceExtensions) return false;
    if (a.exportLayout.deviceFile != b.exportLayout.deviceFile) return false;
    if (a.exportLayout.sceneFile != b.exportLayout.sceneFile) return false;
    if (a.exportLayout.dynamicWorkCellFile != b.exportLayout.dynamicWorkCellFile) return false;
    if (a.exportLayout.collisionSetupFile != b.exportLayout.collisionSetupFile) return false;
    if (a.exportLayout.proximitySetupFile != b.exportLayout.proximitySetupFile) return false;
    if (a.exportLayout.preserveImportedFileLayout != b.exportLayout.preserveImportedFileLayout)
        return false;

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
    for (std::size_t i = 0; i < a.collisionModels.size (); ++i) {
        if (a.collisionModels[i].enabled != b.collisionModels[i].enabled) return false;
    }

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
    if (a.collisionSetup.excludeBaseToFirstJoint != b.collisionSetup.excludeBaseToFirstJoint)
        return false;
    if (a.collisionSetup.excludePairs.size () != b.collisionSetup.excludePairs.size ())
        return false;
    for (std::size_t i = 0; i < a.collisionSetup.excludePairs.size (); ++i) {
        if (a.collisionSetup.excludePairs[i].enabled !=
            b.collisionSetup.excludePairs[i].enabled) return false;
        if (a.collisionSetup.excludePairs[i].source !=
            b.collisionSetup.excludePairs[i].source) return false;
        if (a.collisionSetup.excludePairs[i].reason !=
            b.collisionSetup.excludePairs[i].reason) return false;
    }

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
    if (!original.collisionModels.empty ()) {
        original.collisionModels.front ().enabled = false;
    }
    rws::FramePairSpec pair;
    pair.first = "Joint1";
    pair.second = "Joint3";
    pair.enabled = false;
    pair.source = "Manual";
    pair.reason = "Round-trip disabled pair";
    original.collisionSetup.excludeBaseToFirstJoint = false;
    original.collisionSetup.excludePairs.push_back (pair);
    original.proximitySetup.enabled = true;
    original.proximitySetup.rules.push_back (
        {rws::ProximityRuleKind::Exclude, "Joint.*", "Tool.*"});
    original.imported.active = true;
    original.imported.sourceSceneFile = "CustomScene.wc.xml";
    original.imported.sourceDeviceFile = "vendor/Robot.wc.xml";
    original.imported.sourceCollisionSetupFile = "vendor/CollisionSetup.xml";
    original.imported.sourceProximitySetupFile = "vendor/ProximitySetup.xml";
    original.exportLayout.preserveImportedFileLayout = true;
    original.exportLayout.deviceFile = "export/JsonRoundTrip.wc.xml";
    original.exportLayout.sceneFile = "export/JsonRoundTripScene.wc.xml";
    original.exportLayout.dynamicWorkCellFile = "export/JsonRoundTrip.dwc.xml";
    original.exportLayout.collisionSetupFile = "export/CollisionSetup.xml";
    original.exportLayout.proximitySetupFile = "export/ProximitySetup.xml";
    original.imported.workcellExtensions.push_back (
        "<ImportedExtension semantic=\"keep\" />");
    original.imported.deviceExtensions.push_back (
        "<VendorDeviceData value=\"preserve\" />");

    const std::string json = rws::RobotModelSpecJson::toJson (original);
    rws::RobotModelSpec decoded;
    std::string error;
    if (!rws::RobotModelSpecJson::fromJson (json, decoded, &error))
        return fail ("RobotModelSpec JSON round trip failed: " + error);
    if (!sameRobotModelSpec (original, decoded))
        return fail ("RobotModelSpec JSON round trip changed at least one field.");
    return 0;
}

static int testLegacyCollisionMetadataIsDiscardedOnSave ()
{
    rws::RobotModelSpec original =
        rws::RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
    rws::CollisionModelSpec collision;
    collision.name = "LegacyCollision";
    collision.refFrame = "Joint1";
    original.collisionModels.push_back (collision);

    QJsonObject legacy = rws::RobotModelSpecJson::toObject (original);
    QJsonArray drawables = legacy.value ("drawables").toArray ();
    QJsonObject drawable = drawables.at (0).toObject ();
    drawable["visualDetail"] = "Both";
    drawable["collisionModel"] = true;
    drawables[0] = drawable;
    legacy["drawables"] = drawables;

    QJsonArray collisionModels = legacy.value ("collisionModels").toArray ();
    QJsonObject collisionModel = collisionModels.at (0).toObject ();
    collisionModel["geometryDetail"] = "Fine";
    collisionModel["source"] = "Imported";
    collisionModels[0] = collisionModel;
    legacy["collisionModels"] = collisionModels;

    rws::RobotModelSpec decoded;
    std::string error;
    QJsonObject legacyRoot;
    legacyRoot["schemaVersion"] = 1;
    legacyRoot["type"] = "RobotModelSpec";
    legacyRoot["data"] = legacy;
    const std::string legacyJson = QJsonDocument (legacyRoot).toJson ().toStdString ();
    if (!rws::RobotModelSpecJson::fromJson (legacyJson, decoded, &error))
        return fail ("Legacy metadata JSON failed to load: " + error);

    const QJsonObject saved = rws::RobotModelSpecJson::toObject (decoded);
    const QJsonObject savedDrawable = saved.value ("drawables").toArray ().at (0).toObject ();
    const QJsonObject savedCollision =
        saved.value ("collisionModels").toArray ().at (0).toObject ();
    if (savedDrawable.contains ("visualDetail") || savedDrawable.contains ("collisionModel") ||
        savedCollision.contains ("geometryDetail") || savedCollision.contains ("source"))
        return fail ("Legacy collision presentation metadata was written back to JSON.");
    if (!savedCollision.value ("enabled").toBool ())
        return fail ("Collision model enabled state was not preserved.");
    return 0;
}

int main (int, char**)
{
    if (const int rc = testFullRoundTrip ())
        return rc;
    if (const int rc = testLegacyCollisionMetadataIsDiscardedOnSave ())
        return rc;
    std::cout << "RobotModelSpecJson round trip test passed." << std::endl;
    return 0;
}
