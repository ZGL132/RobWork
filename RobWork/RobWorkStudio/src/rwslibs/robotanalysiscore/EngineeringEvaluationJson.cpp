#include "EngineeringEvaluationJson.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace rws {

namespace {

QString statusToString(EngineeringEvaluationStatus status)
{
    switch (status) {
        case EngineeringEvaluationStatus::Success: return "Success";
        case EngineeringEvaluationStatus::Infeasible: return "Infeasible";
        case EngineeringEvaluationStatus::DataInsufficient: return "DataInsufficient";
        case EngineeringEvaluationStatus::Failed: return "Failed";
        case EngineeringEvaluationStatus::Cancelled: return "Cancelled";
    }
    return "Failed";
}

EngineeringEvaluationStatus statusFromString(const QString& value)
{
    if (value == "Success") return EngineeringEvaluationStatus::Success;
    if (value == "Infeasible") return EngineeringEvaluationStatus::Infeasible;
    if (value == "DataInsufficient") return EngineeringEvaluationStatus::DataInsufficient;
    if (value == "Cancelled") return EngineeringEvaluationStatus::Cancelled;
    return EngineeringEvaluationStatus::Failed;
}

QString metricStatusToString(EngineeringMetricStatus status)
{
    return status == EngineeringMetricStatus::Valid ? "Valid" :
           status == EngineeringMetricStatus::Warning ? "Warning" : "Invalid";
}

EngineeringMetricStatus metricStatusFromString(const QString& value)
{
    if (value == "Warning") return EngineeringMetricStatus::Warning;
    if (value == "Invalid") return EngineeringMetricStatus::Invalid;
    return EngineeringMetricStatus::Valid;
}

QString analysisStatusToString(AnalysisStatus status)
{
    switch (status) {
        case AnalysisStatus::Pass: return "Pass";
        case AnalysisStatus::Warning: return "Warning";
        case AnalysisStatus::Fail: return "Fail";
        case AnalysisStatus::Unknown: return "Unknown";
    }
    return "Unknown";
}

AnalysisStatus analysisStatusFromString(const QString& value)
{
    if (value == "Pass") return AnalysisStatus::Pass;
    if (value == "Warning") return AnalysisStatus::Warning;
    if (value == "Fail") return AnalysisStatus::Fail;
    return AnalysisStatus::Unknown;
}

void setError(std::string* error, const std::string& value)
{
    if (error != nullptr)
        *error = value;
}

} // namespace

std::string EngineeringEvaluationJson::toJson(const EngineeringEvaluationResult& result)
{
    QJsonObject root;
    root["schemaVersion"] = SchemaVersion;
    root["type"] = "EngineeringEvaluationResult";
    root["providerId"] = QString::fromStdString(result.providerId);
    root["providerVersion"] = QString::fromStdString(result.providerVersion);
    root["status"] = statusToString(result.status);
    root["elapsedSeconds"] = result.elapsedSeconds;
    root["inputSnapshot"] = QJsonObject{{"modelHash", QString::fromStdString(result.inputSnapshot.modelHash)},
                                         {"taskEnvironmentHash", QString::fromStdString(result.inputSnapshot.taskEnvironmentHash)},
                                         {"configurationHash", QString::fromStdString(result.inputSnapshot.configurationHash)}};
    QJsonArray metrics;
    for (const EngineeringMetric& metric : result.metrics) {
        metrics.append(QJsonObject{{"metricId", QString::fromStdString(metric.metricId)},
                                   {"value", metric.value}, {"unit", QString::fromStdString(metric.unit)},
                                   {"status", metricStatusToString(metric.status)},
                                   {"providerId", QString::fromStdString(metric.providerId)}});
    }
    root["metrics"] = metrics;
    QJsonArray constraints;
    for (const EngineeringConstraintResult& constraint : result.constraints) {
        constraints.append(QJsonObject{{"metricId", QString::fromStdString(constraint.metricId)},
                                       {"comparison", QString::fromStdString(constraint.comparison)},
                                       {"threshold", constraint.threshold}, {"observedValue", constraint.observedValue},
                                       {"hard", constraint.hard}, {"satisfied", constraint.satisfied},
                                       {"failureReason", QString::fromStdString(constraint.failureReason)}});
    }
    root["constraints"] = constraints;
    QJsonArray warnings;
    for (const AnalysisWarning& warning : result.warnings) {
        warnings.append(QJsonObject{{"code", QString::fromStdString(warning.code)},
                                    {"message", QString::fromStdString(warning.message)},
                                    {"source", QString::fromStdString(warning.source)},
                                    {"severity", analysisStatusToString(warning.severity)}});
    }
    root["warnings"] = warnings;
    QJsonArray artifacts;
    for (const EngineeringArtifact& artifact : result.artifacts) {
        artifacts.append(QJsonObject{{"artifactId", QString::fromStdString(artifact.artifactId)},
                                     {"mimeType", QString::fromStdString(artifact.mimeType)},
                                     {"payload", QString::fromStdString(artifact.payload)}});
    }
    root["artifacts"] = artifacts;
    return QJsonDocument(root).toJson(QJsonDocument::Compact).toStdString();
}

bool EngineeringEvaluationJson::fromJson(const std::string& json,
                                         EngineeringEvaluationResult& result,
                                         std::string* error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(
        QByteArray::fromStdString(json), &parseError);
    if (!document.isObject()) {
        setError(error, "EngineeringEvaluation.Json.Invalid: " + parseError.errorString().toStdString());
        return false;
    }
    const QJsonObject root = document.object();
    if (root.value("schemaVersion").toInt() != SchemaVersion ||
        root.value("type").toString() != "EngineeringEvaluationResult") {
        setError(error, "EngineeringEvaluation.Json.UnsupportedSchema");
        return false;
    }
    EngineeringEvaluationResult parsed;
    parsed.providerId = root.value("providerId").toString().toStdString();
    parsed.providerVersion = root.value("providerVersion").toString().toStdString();
    parsed.status = statusFromString(root.value("status").toString());
    parsed.elapsedSeconds = root.value("elapsedSeconds").toDouble();
    const QJsonObject snapshot = root.value("inputSnapshot").toObject();
    parsed.inputSnapshot.modelHash = snapshot.value("modelHash").toString().toStdString();
    parsed.inputSnapshot.taskEnvironmentHash = snapshot.value("taskEnvironmentHash").toString().toStdString();
    parsed.inputSnapshot.configurationHash = snapshot.value("configurationHash").toString().toStdString();
    for (const QJsonValue& value : root.value("metrics").toArray()) {
        const QJsonObject object = value.toObject();
        parsed.metrics.push_back({object.value("metricId").toString().toStdString(), object.value("value").toDouble(),
                                  object.value("unit").toString().toStdString(),
                                  metricStatusFromString(object.value("status").toString()),
                                  object.value("providerId").toString().toStdString()});
    }
    for (const QJsonValue& value : root.value("constraints").toArray()) {
        const QJsonObject object = value.toObject();
        parsed.constraints.push_back({object.value("metricId").toString().toStdString(),
                                     object.value("comparison").toString().toStdString(),
                                     object.value("threshold").toDouble(), object.value("observedValue").toDouble(),
                                     object.value("hard").toBool(true), object.value("satisfied").toBool(false),
                                     object.value("failureReason").toString().toStdString()});
    }
    for (const QJsonValue& value : root.value("warnings").toArray()) {
        const QJsonObject object = value.toObject();
        parsed.warnings.push_back({object.value("code").toString().toStdString(),
                                   object.value("message").toString().toStdString(),
                                   object.value("source").toString().toStdString(),
                                   analysisStatusFromString(object.value("severity").toString())});
    }
    for (const QJsonValue& value : root.value("artifacts").toArray()) {
        const QJsonObject object = value.toObject();
        parsed.artifacts.push_back({object.value("artifactId").toString().toStdString(),
                                   object.value("mimeType").toString().toStdString(),
                                   object.value("payload").toString().toStdString()});
    }
    result = parsed;
    if (error != nullptr)
        error->clear();
    return true;
}

} // namespace rws
