#include "OptimizationRunJson.hpp"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <cmath>
#include <limits>

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

bool readNonNegativeSize(const QJsonObject& object, const char* key,
                         std::size_t fallback, std::size_t& output,
                         std::string* error)
{
    const QLatin1String field(key);
    if (!object.contains(field)) {
        output = fallback;
        return true;
    }
    const QJsonValue value = object.value(field);
    const double number = value.toDouble(std::numeric_limits<double>::quiet_NaN());
    if (!value.isDouble() || !std::isfinite(number) || number < 0.0 ||
        std::floor(number) != number ||
        number > static_cast<double>(std::numeric_limits<std::size_t>::max())) {
        if (error != nullptr)
            *error = "Field '" + std::string(key) + "' must be a non-negative integer.";
        return false;
    }
    output = static_cast<std::size_t>(number);
    return true;
}

bool readUnsignedInt(const QJsonObject& object, const char* key,
                     unsigned int fallback, unsigned int& output,
                     std::string* error)
{
    std::size_t value = 0;
    if (!readNonNegativeSize(object, key, fallback, value, error) ||
        value > static_cast<std::size_t>(std::numeric_limits<unsigned int>::max())) {
        if (error != nullptr && error->empty())
            *error = "Field '" + std::string(key) + "' exceeds unsigned integer range.";
        return false;
    }
    output = static_cast<unsigned int>(value);
    return true;
}

bool refFromJson(const QJsonObject& obj, OptimizationRunResourceRef& ref,
                 std::string* error)
{
    ref.resourceId = obj.value("resourceId").toString().toStdString();
    ref.kind = obj.value("kind").toString().toStdString();
    ref.schemaVersion = obj.value("schemaVersion").toInt();
    ref.relativePath = obj.value("relativePath").toString().toStdString();
    ref.sha256 = obj.value("sha256").toString().toStdString();
    if (!readNonNegativeSize(obj, "byteSize", 0, ref.byteSize, error))
        return false;
    if (ref.resourceId.empty() || ref.kind.empty() || ref.relativePath.empty() ||
        ref.sha256.empty() || ref.schemaVersion <= 0) {
        if (error != nullptr) *error = "Optimization resource reference is incomplete.";
        return false;
    }
    return true;
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
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = "Optimization snapshot JSON root is invalid: " +
                             parseError.errorString().toStdString();
        return false;
    }
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
    if (!readUnsignedInt(progress, "randomSeed", parsed.randomSeed, parsed.randomSeed, error) ||
        !readNonNegativeSize(progress, "requestedCandidateCount", parsed.requestedCandidateCount,
                             parsed.requestedCandidateCount, error) ||
        !readNonNegativeSize(progress, "generatedCandidateCount", parsed.generatedCandidateCount,
                             parsed.generatedCandidateCount, error) ||
        !readNonNegativeSize(progress, "completedCandidateCount", parsed.completedCandidateCount,
                             parsed.completedCandidateCount, error) ||
        !readNonNegativeSize(progress, "nextCandidateIndex", parsed.nextCandidateIndex,
                             parsed.nextCandidateIndex, error))
        return false;
    const QJsonObject resources = root.value("resources").toObject();
    for (const auto& value : resources.value("candidateResults").toArray()) {
        OptimizationRunResourceRef ref;
        if (!refFromJson(value.toObject(), ref, error)) return false;
        parsed.candidateResults.push_back(ref);
    }
    for (const auto& value : resources.value("evidence").toArray()) {
        OptimizationRunResourceRef ref;
        if (!refFromJson(value.toObject(), ref, error)) return false;
        parsed.evidence.push_back(ref);
    }
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
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(
        QByteArray::fromStdString(json), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject() ||
        doc.object().value("type").toString() != "OptimizationRunResource" ||
        doc.object().value("schemaVersion").toInt() != 1 ||
        doc.object().value("kind").toString() != "CandidateResult") {
        if (error) *error = "Invalid candidate result resource.";
        return false;
    }
    const QJsonObject root = doc.object();
    const QJsonObject payload = root.value("payload").toObject();
    if (!payload.contains("candidateId") || !payload.contains("representativeQ") ||
        !payload.contains("warnings") || !payload.value("warnings").isArray() ||
        !payload.value("representativeQ").isArray()) {
        if (error) *error = "Candidate result resource payload is incomplete.";
        return false;
    }
    result = CandidateResult();
    result.candidateId = payload.value("candidateId").toString().toStdString();
    for (const auto& value : payload.value("representativeQ").toArray()) {
        if (!value.isDouble() || !std::isfinite(value.toDouble())) {
            if (error) *error = "Candidate representativeQ must contain finite numbers.";
            return false;
        }
        result.representativeQ.push_back(value.toDouble());
    }
    for (const auto& value : payload.value("warnings").toArray()) {
        if (!value.isString()) {
            if (error) *error = "Candidate result warnings must be strings.";
            return false;
        }
        result.warnings.push_back(value.toString().toStdString());
    }
    if (resourceId) *resourceId = root.value("resourceId").toString().toStdString();
    if (result.candidateId.empty() || !resourceId || resourceId->empty()) {
        if (error) *error = "Candidate result resource identifiers are required.";
        return false;
    }
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
