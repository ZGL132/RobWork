#include "OptimizationRunJson.hpp"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace rws {

namespace {

QJsonObject refToJson(const OptimizationRunResourceRef& ref)
{
    return QJsonObject{{"resourceId", QString::fromStdString(ref.resourceId)},
                       {"kind", QString::fromStdString(ref.kind)},
                       {"schemaVersion", ref.schemaVersion},
                       {"relativePath", QString::fromStdString(ref.relativePath)},
                       {"sha256", QString::fromStdString(ref.sha256)},
                       {"byteSize", static_cast<qint64>(ref.byteSize)}};
}

OptimizationRunResourceRef refFromJson(const QJsonObject& obj)
{
    OptimizationRunResourceRef ref;
    ref.resourceId = obj.value("resourceId").toString().toStdString();
    ref.kind = obj.value("kind").toString().toStdString();
    ref.schemaVersion = obj.value("schemaVersion").toInt();
    ref.relativePath = obj.value("relativePath").toString().toStdString();
    ref.sha256 = obj.value("sha256").toString().toStdString();
    ref.byteSize = static_cast<std::size_t>(obj.value("byteSize").toVariant().toLongLong());
    return ref;
}

QJsonObject inputToJson(const OptimizationRunInputFingerprint& input)
{
    return QJsonObject{{"projectEnvelopeFingerprint", QString::fromStdString(input.projectEnvelopeFingerprint)},
                       {"modelFingerprint", QString::fromStdString(input.modelFingerprint)},
                       {"environmentFingerprint", QString::fromStdString(input.environmentFingerprint)},
                       {"requirementFingerprint", QString::fromStdString(input.requirementFingerprint)},
                       {"designSpaceFingerprint", QString::fromStdString(input.designSpaceFingerprint)},
                       {"evaluationPlanFingerprint", QString::fromStdString(input.evaluationPlanFingerprint)},
                       {"finalValidationPlanFingerprint", QString::fromStdString(input.finalValidationPlanFingerprint)},
                       {"toolFingerprint", QString::fromStdString(input.toolFingerprint)},
                       {"adapterRegistryFingerprint", QString::fromStdString(input.adapterRegistryFingerprint)},
                       {"compilerVersion", QString::fromStdString(input.compilerVersion)},
                       {"evaluatorId", QString::fromStdString(input.evaluatorId)},
                       {"evaluatorVersion", QString::fromStdString(input.evaluatorVersion)}};
}

void inputFromJson(const QJsonObject& obj, OptimizationRunInputFingerprint& input)
{
    input.projectEnvelopeFingerprint = obj.value("projectEnvelopeFingerprint").toString().toStdString();
    input.modelFingerprint = obj.value("modelFingerprint").toString().toStdString();
    input.environmentFingerprint = obj.value("environmentFingerprint").toString().toStdString();
    input.requirementFingerprint = obj.value("requirementFingerprint").toString().toStdString();
    input.designSpaceFingerprint = obj.value("designSpaceFingerprint").toString().toStdString();
    input.evaluationPlanFingerprint = obj.value("evaluationPlanFingerprint").toString().toStdString();
    input.finalValidationPlanFingerprint = obj.value("finalValidationPlanFingerprint").toString().toStdString();
    input.toolFingerprint = obj.value("toolFingerprint").toString().toStdString();
    input.adapterRegistryFingerprint = obj.value("adapterRegistryFingerprint").toString().toStdString();
    input.compilerVersion = obj.value("compilerVersion").toString().toStdString();
    input.evaluatorId = obj.value("evaluatorId").toString().toStdString();
    input.evaluatorVersion = obj.value("evaluatorVersion").toString().toStdString();
}

} // namespace

std::string optimizationRunSnapshotToJson(const OptimizationRunSnapshot& snapshot)
{
    QJsonObject root;
    root["type"] = "OptimizationRunSnapshot";
    root["schemaVersion"] = snapshot.schemaVersion;
    root["runId"] = QString::fromStdString(snapshot.runId);
    root["startedAt"] = QString::fromStdString(snapshot.startedAt);
    root["completedAt"] = QString::fromStdString(snapshot.completedAt);
    root["status"] = toString(snapshot.status);
    root["input"] = inputToJson(snapshot.input);
    root["plans"] = QJsonObject{{"currentEnvelopeJson", QString::fromStdString(snapshot.currentEnvelopeJson)},
                                 {"evaluationPlanJson", QString::fromStdString(snapshot.evaluationPlanJson)},
                                 {"finalValidationPlanJson", QString::fromStdString(snapshot.finalValidationPlanJson)}};
    root["progress"] = QJsonObject{{"randomSeed", static_cast<qint64>(snapshot.randomSeed)},
                                    {"requestedCandidateCount", static_cast<qint64>(snapshot.requestedCandidateCount)},
                                    {"generatedCandidateCount", static_cast<qint64>(snapshot.generatedCandidateCount)},
                                    {"completedCandidateCount", static_cast<qint64>(snapshot.completedCandidateCount)},
                                    {"nextCandidateIndex", static_cast<qint64>(snapshot.nextCandidateIndex)}};
    root["resources"] = QJsonObject{{"candidateResults", QJsonArray()}, {"evidence", QJsonArray()}};
    QJsonArray candidates;
    for (const auto& ref : snapshot.candidateResults) candidates.append(refToJson(ref));
    QJsonArray evidence;
    for (const auto& ref : snapshot.evidence) evidence.append(refToJson(ref));
    root["resources"].toObject();
    QJsonObject resources = root["resources"].toObject();
    resources["candidateResults"] = candidates;
    resources["evidence"] = evidence;
    root["resources"] = resources;
    root["terminal"] = QJsonObject{{"canceled", snapshot.canceled},
                                    {"diagnostic", QString::fromStdString(snapshot.terminalDiagnostic)},
                                    {"baselineCandidateId", QString::fromStdString(snapshot.baselineCandidateId)},
                                    {"bestCandidateId", QString::fromStdString(snapshot.bestCandidateId)}};
    return QJsonDocument(root).toJson(QJsonDocument::Compact).toStdString();
}

bool optimizationRunSnapshotFromJson(const std::string& json,
                                      OptimizationRunSnapshot& snapshot,
                                      std::string* error)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(json), &parseError);
    if (!doc.isObject()) { if (error) *error = "Optimization snapshot JSON root is invalid."; return false; }
    const QJsonObject root = doc.object();
    if (root.value("type").toString() != "OptimizationRunSnapshot" || root.value("schemaVersion").toInt() != 1) {
        if (error) *error = "Unsupported optimization snapshot schema."; return false;
    }
    OptimizationRunSnapshot parsed;
    parsed.schemaVersion = 1;
    parsed.runId = root.value("runId").toString().toStdString();
    parsed.startedAt = root.value("startedAt").toString().toStdString();
    parsed.completedAt = root.value("completedAt").toString().toStdString();
    if (!optimizationRunSnapshotStatusFromString(root.value("status").toString().toStdString(), parsed.status, error)) return false;
    inputFromJson(root.value("input").toObject(), parsed.input);
    const QJsonObject plans = root.value("plans").toObject();
    parsed.currentEnvelopeJson = plans.value("currentEnvelopeJson").toString().toStdString();
    parsed.evaluationPlanJson = plans.value("evaluationPlanJson").toString().toStdString();
    parsed.finalValidationPlanJson = plans.value("finalValidationPlanJson").toString().toStdString();
    const QJsonObject progress = root.value("progress").toObject();
    parsed.randomSeed = static_cast<unsigned int>(progress.value("randomSeed").toInt());
    parsed.requestedCandidateCount = static_cast<std::size_t>(progress.value("requestedCandidateCount").toInt());
    parsed.generatedCandidateCount = static_cast<std::size_t>(progress.value("generatedCandidateCount").toInt());
    parsed.completedCandidateCount = static_cast<std::size_t>(progress.value("completedCandidateCount").toInt());
    parsed.nextCandidateIndex = static_cast<std::size_t>(progress.value("nextCandidateIndex").toInt());
    const QJsonObject resources = root.value("resources").toObject();
    for (const auto& value : resources.value("candidateResults").toArray()) parsed.candidateResults.push_back(refFromJson(value.toObject()));
    for (const auto& value : resources.value("evidence").toArray()) parsed.evidence.push_back(refFromJson(value.toObject()));
    const QJsonObject terminal = root.value("terminal").toObject();
    parsed.canceled = terminal.value("canceled").toBool();
    parsed.terminalDiagnostic = terminal.value("diagnostic").toString().toStdString();
    parsed.baselineCandidateId = terminal.value("baselineCandidateId").toString().toStdString();
    parsed.bestCandidateId = terminal.value("bestCandidateId").toString().toStdString();
    if (!optimizationRunSnapshotValid(parsed, error)) return false;
    snapshot = std::move(parsed);
    if (error) error->clear();
    return true;
}

std::string candidateResultResourceToJson(const CandidateResult& result, const std::string& resourceId)
{
    QJsonArray warnings;
    for (const std::string& warning : result.warnings)
        warnings.append(QString::fromStdString(warning));
    QJsonObject payload{{"candidateId", QString::fromStdString(result.candidateId)},
                        {"warnings", warnings}, {"representativeQ", QJsonArray()}};
    QJsonArray q;
    for (double value : result.representativeQ) q.append(value);
    payload["representativeQ"] = q;
    QJsonObject root{{"type", "OptimizationRunResource"}, {"schemaVersion", 1},
                     {"kind", "CandidateResult"}, {"resourceId", QString::fromStdString(resourceId)}, {"payload", payload}};
    return QJsonDocument(root).toJson(QJsonDocument::Compact).toStdString();
}

bool candidateResultResourceFromJson(const std::string& json, CandidateResult& result,
                                     std::string* resourceId, std::string* error)
{
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(json));
    if (!doc.isObject() || doc.object().value("type").toString() != "OptimizationRunResource" ||
        doc.object().value("kind").toString() != "CandidateResult") {
        if (error) *error = "Invalid candidate result resource."; return false;
    }
    const QJsonObject root = doc.object();
    const QJsonObject payload = root.value("payload").toObject();
    result = CandidateResult();
    result.candidateId = payload.value("candidateId").toString().toStdString();
    for (const auto& value : payload.value("representativeQ").toArray()) result.representativeQ.push_back(value.toDouble());
    if (resourceId) *resourceId = root.value("resourceId").toString().toStdString();
    if (error) error->clear();
    return !result.candidateId.empty();
}

std::string canonicalOptimizationRunJson(const std::string& json)
{
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(json));
    return doc.isObject() ? doc.toJson(QJsonDocument::Compact).toStdString() : std::string();
}

std::string optimizationRunSha256(const std::string& canonicalJson)
{
    return QCryptographicHash::hash(QByteArray::fromStdString(canonicalJson), QCryptographicHash::Sha256).toHex().toStdString();
}

} // namespace rws
