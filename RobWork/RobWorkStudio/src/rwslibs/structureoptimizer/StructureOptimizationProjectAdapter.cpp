#include "StructureOptimizationProjectAdapter.hpp"

#include "StructureOptimizationJson.hpp"
#include "StructureOptimizationValidation.hpp"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace rws {

namespace {

const int ProjectSchemaVersion = 1;

bool hasInvalidContext(const StructureOptimizationProblem& problem)
{
    return problem.context.modelSpec.robotName.empty() ||
           problem.context.modelSpec.transformJoints.empty();
}

void setError(QString* error, const QString& value)
{
    if (error != nullptr)
        *error = value;
}

} // namespace

bool StructureOptimizationProjectAdapter::loadProject(
    const QString& path, StructureOptimizationProblem& out, int* selectedCandidateIndex,
    QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, "StructureOptimization.Project.OpenFailed: " + file.errorString());
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (!document.isObject()) {
        setError(error, "StructureOptimization.Project.InvalidJson: " + parseError.errorString());
        return false;
    }

    const QJsonObject root = document.object();
    if (root.value("type").toString() != "StructureOptimizationProject" ||
        root.value("schemaVersion").toInt() != ProjectSchemaVersion) {
        setError(error, "StructureOptimization.Project.UnsupportedSchema");
        return false;
    }
    if (!root.value("problem").isObject()) {
        setError(error, "StructureOptimization.Project.MissingProblem");
        return false;
    }

    const QJsonDocument problemDocument(root.value("problem").toObject());
    std::string parseMessage;
    StructureOptimizationProblem loaded;
    if (!StructureOptimizationJson::problemFromJson(
            problemDocument.toJson(QJsonDocument::Compact).toStdString(), loaded,
            &parseMessage)) {
        setError(error, "StructureOptimization.Project.InvalidProblem: " +
                            QString::fromStdString(parseMessage));
        return false;
    }
    if (hasInvalidContext(loaded)) {
        setError(error, "StructureOptimization.Context.Invalid: complete RobotModelSpec is required.");
        return false;
    }

    if (selectedCandidateIndex != nullptr)
        *selectedCandidateIndex = root.value("ui").toObject().value("selectedCandidateIndex").toInt(-1);
    out = loaded;
    if (error != nullptr)
        error->clear();
    return true;
}

bool StructureOptimizationProjectAdapter::saveProject(
    const QString& path, const StructureOptimizationProblem& problem, int selectedCandidateIndex,
    QString* error)
{
    if (hasInvalidContext(problem)) {
        setError(error, "StructureOptimization.Context.Invalid: complete RobotModelSpec is required.");
        return false;
    }

    const QJsonDocument problemDocument = QJsonDocument::fromJson(
        QByteArray::fromStdString(StructureOptimizationJson::problemToJson(problem)));
    if (!problemDocument.isObject()) {
        setError(error, "StructureOptimization.Project.SerializeFailed");
        return false;
    }

    QJsonObject root;
    root["schemaVersion"] = ProjectSchemaVersion;
    root["type"] = "StructureOptimizationProject";
    root["problem"] = problemDocument.object();
    QJsonObject ui;
    ui["selectedCandidateIndex"] = selectedCandidateIndex;
    root["ui"] = ui;

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(error, "StructureOptimization.Project.OpenFailed: " + file.errorString());
        return false;
    }
    if (file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0 || !file.commit()) {
        setError(error, "StructureOptimization.Project.SaveFailed: " + file.errorString());
        return false;
    }
    if (error != nullptr)
        error->clear();
    return true;
}

} // namespace rws
