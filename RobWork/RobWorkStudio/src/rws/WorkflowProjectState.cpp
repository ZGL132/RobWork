#include "WorkflowProjectState.hpp"

#include <QJsonObject>

namespace rws {
namespace {

QJsonObject readObject (const QJsonObject& root, const QString& key)
{
    return root.value (key).toObject ();
}

void putString (QJsonObject& object, const QString& key, const QString& value)
{
    if (value.isEmpty ())
        object.remove (key);
    else
        object.insert (key, value);
}

QString stringValue (const QJsonObject& object, const QString& key)
{
    return object.value (key).toString ();
}

}    // namespace

WorkflowProjectSnapshot WorkflowProjectState::read (const QJsonObject& plugins)
{
    WorkflowProjectSnapshot snapshot;
    const QJsonObject root = plugins.value (QStringLiteral ("workflow")).toObject ();
    if (root.value (QStringLiteral ("schemaVersion")).toInt () != SchemaVersion)
        return snapshot;
    snapshot.fingerprintVersion = root.value (QStringLiteral ("fingerprintVersion")).toInt (1);

    const QJsonObject requirements = readObject (root, QStringLiteral ("requirements"));
    snapshot.requirementsFrozen = requirements.value (QStringLiteral ("frozen")).toBool ();
    snapshot.requirementFingerprint = stringValue (requirements, QStringLiteral ("fingerprint"));
    snapshot.requirementModelFingerprint =
        stringValue (requirements, QStringLiteral ("modelFingerprint"));
    snapshot.requirementSceneFingerprint =
        stringValue (requirements, QStringLiteral ("sceneFingerprint"));

    const QJsonObject kinematics = readObject (root, QStringLiteral ("kinematics"));
    snapshot.kinematicValidationPassed = kinematics.value (QStringLiteral ("passed")).toBool ();
    snapshot.kinematicValidationFingerprint =
        stringValue (kinematics, QStringLiteral ("fingerprint"));
    snapshot.kinematicModelFingerprint =
        stringValue (kinematics, QStringLiteral ("modelFingerprint"));
    snapshot.kinematicRequirementFingerprint =
        stringValue (kinematics, QStringLiteral ("requirementFingerprint"));
    snapshot.kinematicSceneFingerprint =
        stringValue (kinematics, QStringLiteral ("sceneFingerprint"));

    const QJsonObject optimization = readObject (root, QStringLiteral ("optimization"));
    snapshot.optimizationArtifactAvailable =
        optimization.value (QStringLiteral ("available")).toBool ();
    snapshot.optimizationModelFingerprint =
        stringValue (optimization, QStringLiteral ("modelFingerprint"));
    snapshot.optimizationRequirementFingerprint =
        stringValue (optimization, QStringLiteral ("requirementFingerprint"));
    snapshot.optimizationKinematicFingerprint =
        stringValue (optimization, QStringLiteral ("kinematicFingerprint"));
    snapshot.optimizationSceneFingerprint =
        stringValue (optimization, QStringLiteral ("sceneFingerprint"));
    return snapshot;
}

void WorkflowProjectState::write (QJsonObject& plugins, const WorkflowProjectSnapshot& snapshot)
{
    QJsonObject root;
    root.insert (QStringLiteral ("schemaVersion"), SchemaVersion);
    root.insert (QStringLiteral ("fingerprintVersion"), snapshot.fingerprintVersion);

    QJsonObject requirements;
    requirements.insert (QStringLiteral ("frozen"), snapshot.requirementsFrozen);
    putString (requirements, QStringLiteral ("fingerprint"), snapshot.requirementFingerprint);
    putString (requirements, QStringLiteral ("modelFingerprint"),
               snapshot.requirementModelFingerprint);
    putString (requirements, QStringLiteral ("sceneFingerprint"),
               snapshot.requirementSceneFingerprint);
    root.insert (QStringLiteral ("requirements"), requirements);

    QJsonObject kinematics;
    kinematics.insert (QStringLiteral ("passed"), snapshot.kinematicValidationPassed);
    putString (kinematics, QStringLiteral ("fingerprint"),
               snapshot.kinematicValidationFingerprint);
    putString (kinematics, QStringLiteral ("modelFingerprint"),
               snapshot.kinematicModelFingerprint);
    putString (kinematics, QStringLiteral ("requirementFingerprint"),
               snapshot.kinematicRequirementFingerprint);
    putString (kinematics, QStringLiteral ("sceneFingerprint"),
               snapshot.kinematicSceneFingerprint);
    root.insert (QStringLiteral ("kinematics"), kinematics);

    QJsonObject optimization;
    optimization.insert (QStringLiteral ("available"), snapshot.optimizationArtifactAvailable);
    putString (optimization, QStringLiteral ("modelFingerprint"),
               snapshot.optimizationModelFingerprint);
    putString (optimization, QStringLiteral ("requirementFingerprint"),
               snapshot.optimizationRequirementFingerprint);
    putString (optimization, QStringLiteral ("kinematicFingerprint"),
               snapshot.optimizationKinematicFingerprint);
    putString (optimization, QStringLiteral ("sceneFingerprint"),
               snapshot.optimizationSceneFingerprint);
    root.insert (QStringLiteral ("optimization"), optimization);

    plugins.insert (QStringLiteral ("workflow"), root);
}

}    // namespace rws
