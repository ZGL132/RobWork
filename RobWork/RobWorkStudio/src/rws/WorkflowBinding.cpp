#include "WorkflowBinding.hpp"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace {

void setError (QString* error, const QString& message)
{
    if (error != nullptr)
        *error = message;
}

QString bindingPath (const QString& projectRoot)
{
    return QDir (projectRoot).filePath (QStringLiteral ("workflow/binding.json"));
}

}    // namespace

namespace rws {

bool WorkflowBinding::isValid (QString* error) const
{
    if (projectId.isEmpty () || targetDevice.isEmpty () || tcpFrame.isEmpty () ||
        sceneResourceId.isEmpty () || modelResourceId.isEmpty ()) {
        setError (error, QStringLiteral ("Workflow binding is missing a required identifier."));
        return false;
    }
    if (schemaVersion != SchemaVersion) {
        setError (error, QStringLiteral ("Unsupported workflow binding schema version."));
        return false;
    }
    return true;
}

bool WorkflowBinding::write (const QString& projectRoot, QString* error) const
{
    if (!isValid (error))
        return false;
    QDir root (projectRoot);
    if (!root.mkpath (QStringLiteral ("workflow"))) {
        setError (error, QStringLiteral ("Cannot create workflow binding directory."));
        return false;
    }

    QJsonObject object;
    object.insert (QStringLiteral ("schemaVersion"), schemaVersion);
    object.insert (QStringLiteral ("projectId"), projectId);
    object.insert (QStringLiteral ("targetDevice"), targetDevice);
    object.insert (QStringLiteral ("tcpFrame"), tcpFrame);
    object.insert (QStringLiteral ("sceneResourceId"), sceneResourceId);
    object.insert (QStringLiteral ("modelResourceId"), modelResourceId);
    object.insert (QStringLiteral ("sourceKind"), sourceKind);
    object.insert (QStringLiteral ("sourceFingerprint"), sourceFingerprint);

    QSaveFile file (bindingPath (projectRoot));
    if (!file.open (QIODevice::WriteOnly) || file.write (QJsonDocument (object).toJson ()) < 0 ||
        !file.commit ()) {
        setError (error, QStringLiteral ("Cannot write workflow binding."));
        return false;
    }
    return true;
}

bool WorkflowBinding::read (const QString& projectRoot, WorkflowBinding& binding, QString* error)
{
    QFile file (bindingPath (projectRoot));
    if (!file.open (QIODevice::ReadOnly)) {
        setError (error, QStringLiteral ("Cannot open workflow binding."));
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson (file.readAll (), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject ()) {
        setError (error, QStringLiteral ("Workflow binding is not valid JSON."));
        return false;
    }
    const QJsonObject object = document.object ();
    WorkflowBinding candidate;
    candidate.schemaVersion = object.value (QStringLiteral ("schemaVersion")).toInt ();
    candidate.projectId = object.value (QStringLiteral ("projectId")).toString ();
    candidate.targetDevice = object.value (QStringLiteral ("targetDevice")).toString ();
    candidate.tcpFrame = object.value (QStringLiteral ("tcpFrame")).toString ();
    candidate.sceneResourceId = object.value (QStringLiteral ("sceneResourceId")).toString ();
    candidate.modelResourceId = object.value (QStringLiteral ("modelResourceId")).toString ();
    candidate.sourceKind = object.value (QStringLiteral ("sourceKind")).toString ();
    candidate.sourceFingerprint = object.value (QStringLiteral ("sourceFingerprint")).toString ();
    if (!candidate.isValid (error))
        return false;
    binding = candidate;
    return true;
}

}    // namespace rws
