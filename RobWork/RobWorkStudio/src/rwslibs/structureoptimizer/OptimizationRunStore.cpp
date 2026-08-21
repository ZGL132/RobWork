#include "OptimizationRunStore.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace rws {

namespace {
bool validRunId(const QString& id)
{
    if (id.isEmpty() || id.size() > 96) return false;
    for (const QChar c : id)
        if (!(c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('-'))) return false;
    return true;
}

QString snapshotPath(const QString& root, const QString& runId)
{
    return QDir(root).filePath(QStringLiteral("runs/%1/snapshot.json").arg(runId));
}

QString resourcePath(const QString& root, const QString& runId, const QString& hash)
{
    return QDir(root).filePath(QStringLiteral("runs/%1/resources/%2.candidate.json").arg(runId, hash));
}
}

OptimizationRunStore::OptimizationRunStore(const std::string& projectRoot)
    : _projectRoot(QFileInfo(QString::fromStdString(projectRoot)).absoluteFilePath().toStdString())
{
}

bool OptimizationRunStore::saveSnapshot(const OptimizationRunSnapshot& snapshot, std::string* error)
{
    if (!optimizationRunSnapshotValid(snapshot, error)) return false;
    const QString root = QString::fromStdString(_projectRoot);
    const QString runId = QString::fromStdString(snapshot.runId);
    if (!validRunId(runId)) { if (error) *error = "Invalid runId."; return false; }
    const QString path = snapshotPath(root, runId);
    QFileInfo existing(path);
    if (existing.exists()) {
        OptimizationRunSnapshot old;
        std::string oldError;
        if (!loadSnapshot(snapshot.runId, old, &oldError)) { if (error) *error = oldError; return false; }
        if (old.status == OptimizationRunSnapshotStatus::Completed &&
            optimizationRunSnapshotToJson(old) != optimizationRunSnapshotToJson(snapshot)) {
            if (error) *error = "Completed optimization snapshot is immutable.";
            return false;
        }
        if (optimizationRunSnapshotToJson(old) == optimizationRunSnapshotToJson(snapshot)) return true;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(QByteArray::fromStdString(optimizationRunSnapshotToJson(snapshot))) < 0 || !file.commit()) {
        if (error) *error = "Failed to atomically write optimization snapshot.";
        return false;
    }
    if (error) error->clear();
    return true;
}

bool OptimizationRunStore::loadSnapshot(const std::string& runId, OptimizationRunSnapshot& snapshot,
                                        std::string* error) const
{
    const QString id = QString::fromStdString(runId);
    if (!validRunId(id)) { if (error) *error = "Invalid runId."; return false; }
    QFile file(snapshotPath(QString::fromStdString(_projectRoot), id));
    if (!file.open(QIODevice::ReadOnly)) { if (error) *error = "Snapshot is unavailable."; return false; }
    return optimizationRunSnapshotFromJson(file.readAll().toStdString(), snapshot, error);
}

bool OptimizationRunStore::publishCandidateResult(const std::string& runId, const CandidateResult& result,
                                                   OptimizationRunResourceRef& ref, std::string* error)
{
    const QString id = QString::fromStdString(runId);
    if (!validRunId(id) || result.candidateId.empty()) { if (error) *error = "Invalid run or candidate id."; return false; }
    const std::string payload = candidateResultResourceToJson(result, "");
    const std::string hash = optimizationRunSha256(std::string("CandidateResult\n") + payload);
    const QString path = resourcePath(QString::fromStdString(_projectRoot), id, QString::fromStdString(hash));
    QDir().mkpath(QFileInfo(path).absolutePath());
    if (QFileInfo::exists(path)) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.readAll().toStdString() != payload) {
            if (error) *error = "Candidate resource content conflict.";
            return false;
        }
    } else {
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(QByteArray::fromStdString(payload)) < 0 || !file.commit()) {
            if (error) *error = "Failed to publish candidate resource.";
            return false;
        }
    }
    ref.resourceId = "sha256:" + hash;
    ref.kind = "CandidateResult";
    ref.schemaVersion = 1;
    ref.relativePath = QStringLiteral("runs/%1/resources/%2.candidate.json").arg(id, QString::fromStdString(hash)).toStdString();
    ref.sha256 = optimizationRunSha256(payload);
    ref.byteSize = payload.size();
    if (error) error->clear();
    return true;
}

OptimizationResourceLoadResult OptimizationRunStore::loadCandidateResult(const OptimizationRunResourceRef& ref) const
{
    OptimizationResourceLoadResult loaded;
    if (ref.kind != "CandidateResult" || ref.relativePath.find("..") != std::string::npos) {
        loaded.availability = OptimizationResourceAvailability::Invalid;
        loaded.diagnostic = "Invalid candidate resource reference.";
        return loaded;
    }
    QFile file(QDir(QString::fromStdString(_projectRoot)).filePath(QString::fromStdString(ref.relativePath)));
    if (!file.open(QIODevice::ReadOnly)) { loaded.diagnostic = "Candidate resource unavailable."; return loaded; }
    const QByteArray bytes = file.readAll();
    if (optimizationRunSha256(bytes.toStdString()) != ref.sha256) {
        loaded.availability = OptimizationResourceAvailability::Corrupt;
        loaded.diagnostic = "Candidate resource checksum mismatch.";
        return loaded;
    }
    std::string resourceId;
    std::string error;
    if (!candidateResultResourceFromJson(bytes.toStdString(), loaded.candidate, &resourceId, &error)) {
        loaded.availability = OptimizationResourceAvailability::Corrupt;
        loaded.diagnostic = error;
        return loaded;
    }
    loaded.availability = OptimizationResourceAvailability::Available;
    return loaded;
}

bool OptimizationRunStore::hasSnapshot(const std::string& runId) const
{
    return QFileInfo::exists(snapshotPath(QString::fromStdString(_projectRoot), QString::fromStdString(runId)));
}

} // namespace rws
