#include "StructureOptimizationExportService.hpp"

#include "StructureCandidateExporter.hpp"
#include "StructureOptimizationCsv.hpp"
#include "StructureOptimizationJson.hpp"
#include "StructureOptimizationProjectAdapter.hpp"
#include "StructureOptimizationReportWriter.hpp"

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QTemporaryDir>

namespace rws {

namespace {

bool writeTextFile(const QString& path, const std::string& content, QString* error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error != nullptr)
            *error = file.errorString();
        return false;
    }
    if (file.write(content.data(), static_cast<qint64>(content.size())) < 0 || !file.commit()) {
        if (error != nullptr)
            *error = file.errorString();
        return false;
    }
    return true;
}

bool removePath(const QString& path)
{
    if (QFileInfo(path).isDir())
        return QDir(path).removeRecursively();
    return !QFile::exists(path) || QFile::remove(path);
}

bool movePath(const QString& source, const QString& target)
{
    if (QFileInfo(source).isDir())
        return QDir().rename(source, target);
    return QFile::rename(source, target);
}

const StructureCandidateResult* findCandidate(
    const StructureOptimizationResult& result, int index)
{
    for (const StructureCandidateResult& candidate : result.candidates) {
        if (candidate.index == index)
            return &candidate;
    }
    return nullptr;
}

} // namespace

StructureOptimizationExportResult StructureOptimizationExportService::exportAll(
    const StructureOptimizationProblem& problem, const StructureOptimizationResult& result,
    const StructureOptimizationExportRequest& request)
{
    StructureOptimizationExportResult output;
    if (request.directory.isEmpty()) {
        output.errors << "StructureOptimization.Export.InvalidDirectory";
        return output;
    }

    QDir directory(request.directory);
    if (!directory.exists() && !directory.mkpath(".")) {
        output.errors << "StructureOptimization.Export.CreateDirectoryFailed";
        return output;
    }

    const QStringList names = {
        "project.structure-optimization.json",
        "result.structure-optimization.json",
        "candidates.csv",
        "task-details.csv",
        "audit.csv",
        "report.md"};

    const StructureCandidateResult* selectedCandidate = nullptr;
    QString candidateDirectoryName;
    if (request.exportCandidateModel) {
        selectedCandidate = findCandidate(result, request.selectedCandidateIndex);
        if (selectedCandidate == nullptr || !selectedCandidate->feasible ||
            selectedCandidate->status == StructureCandidateStatus::Infeasible ||
            selectedCandidate->status == StructureCandidateStatus::Failed ||
            selectedCandidate->status == StructureCandidateStatus::Canceled) {
            output.errors << "StructureOptimization.Export.CandidateNotFeasible";
            return output;
        }
        candidateDirectoryName = QString("candidate-%1").arg(selectedCandidate->index);
    }
    if (!request.overwrite) {
        for (const QString& name : names) {
            if (QFile::exists(directory.filePath(name))) {
                output.errors << "StructureOptimization.Export.FileExists: " + name;
                return output;
            }
        }
        if (!candidateDirectoryName.isEmpty() &&
            QFileInfo::exists(directory.filePath(candidateDirectoryName))) {
            output.errors << "StructureOptimization.Export.FileExists: " + candidateDirectoryName;
            return output;
        }
    }

    // 先在目标目录内创建 staging，确保导出失败时 QTemporaryDir 自动清理所有半成品。
    QTemporaryDir staging(directory.filePath(".structure-optimization-export-XXXXXX"));
    if (!staging.isValid()) {
        output.errors << "StructureOptimization.Export.StagingDirectoryFailed";
        return output;
    }

    QString error;
    const QString projectPath = QDir(staging.path()).filePath(names[0]);
    if (!StructureOptimizationProjectAdapter::saveProject(
            projectPath, problem, request.selectedCandidateIndex, &error)) {
        output.errors << error;
        return output;
    }
    output.writtenFiles << projectPath;

    const std::vector<std::pair<QString, std::string> > textFiles = {
        {QDir(staging.path()).filePath(names[1]), StructureOptimizationJson::resultToJson(problem, result)},
        {QDir(staging.path()).filePath(names[2]), StructureOptimizationCsv::candidatesCsv(problem, result)},
        {QDir(staging.path()).filePath(names[3]), StructureOptimizationCsv::taskDetailCsv(problem, result)},
        {QDir(staging.path()).filePath(names[4]), StructureOptimizationCsv::auditCsv(problem, result)},
        {QDir(staging.path()).filePath(names[5]), StructureOptimizationReportWriter::write(problem, result)}};
    for (const auto& entry : textFiles) {
        if (!writeTextFile(entry.first, entry.second, &error)) {
            output.errors << "StructureOptimization.Export.WriteFailed: " + entry.first +
                                 ": " + error;
            return output;
        }
    }

    if (request.exportCandidateModel) {
        const QString modelDirectory = QDir(staging.path()).filePath(candidateDirectoryName);
        QStringList exportErrors;
        if (!StructureCandidateExporter::exportModel(problem, *selectedCandidate, modelDirectory,
                                                      exportErrors)) {
            output.errors.append(exportErrors);
            return output;
        }
    }

    // 发布阶段只移动已完成的工件；发生冲突或移动失败时回滚已发布文件。
    QStringList relativePaths = names;
    if (!candidateDirectoryName.isEmpty())
        relativePaths << candidateDirectoryName;
    QStringList backups;
    QStringList backupTargets;
    QStringList published;
    for (int i = 0; i < relativePaths.size(); ++i) {
        const QString relative = relativePaths[i];
        const QString target = directory.filePath(relative);
        const QString source = QDir(staging.path()).filePath(relative);
        if (QFileInfo::exists(target)) {
            if (!request.overwrite || !movePath(target, QDir(staging.path()).filePath(
                                                    QString(".backup-%1").arg(i)))) {
                for (int j = backups.size() - 1; j >= 0; --j)
                    movePath(backups[j], backupTargets[j]);
                output.errors << "StructureOptimization.Export.FileExists: " + relative;
                return output;
            }
            backups << QDir(staging.path()).filePath(QString(".backup-%1").arg(i));
            backupTargets << target;
        }
        if (!movePath(source, target)) {
            for (const QString& path : published)
                removePath(path);
            for (int j = backups.size() - 1; j >= 0; --j)
                movePath(backups[j], backupTargets[j]);
            output.errors << "StructureOptimization.Export.PublishFailed: " + relative;
            return output;
        }
        published << target;
    }
    for (const QString& backup : backups)
        removePath(backup);
    output.writtenFiles = published;

    output.ok = output.errors.isEmpty();
    return output;
}

} // namespace rws
