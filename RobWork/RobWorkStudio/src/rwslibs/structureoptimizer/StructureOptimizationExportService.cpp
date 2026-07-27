#include "StructureOptimizationExportService.hpp"

#include "StructureCandidateExporter.hpp"
#include "StructureOptimizationCsv.hpp"
#include "StructureOptimizationJson.hpp"
#include "StructureOptimizationProjectAdapter.hpp"
#include "StructureOptimizationReportWriter.hpp"

#include <QDir>
#include <QFile>
#include <QSaveFile>

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
        "report.md"};
    if (!request.overwrite) {
        for (const QString& name : names) {
            if (QFile::exists(directory.filePath(name))) {
                output.errors << "StructureOptimization.Export.FileExists: " + name;
                return output;
            }
        }
    }

    QString error;
    const QString projectPath = directory.filePath(names[0]);
    if (!StructureOptimizationProjectAdapter::saveProject(
            projectPath, problem, request.selectedCandidateIndex, &error)) {
        output.errors << error;
        return output;
    }
    output.writtenFiles << projectPath;

    const std::vector<std::pair<QString, std::string> > textFiles = {
        {directory.filePath(names[1]), StructureOptimizationJson::resultToJson(problem, result)},
        {directory.filePath(names[2]), StructureOptimizationCsv::candidatesCsv(problem, result)},
        {directory.filePath(names[3]), StructureOptimizationCsv::taskDetailCsv(problem, result)},
        {directory.filePath(names[4]), StructureOptimizationReportWriter::write(problem, result)}};
    for (const auto& entry : textFiles) {
        if (!writeTextFile(entry.first, entry.second, &error)) {
            output.errors << "StructureOptimization.Export.WriteFailed: " + entry.first +
                                 ": " + error;
            for (const QString& written : output.writtenFiles)
                QFile::remove(written);
            output.writtenFiles.clear();
            return output;
        }
        output.writtenFiles << entry.first;
    }

    if (request.exportCandidateModel) {
        const StructureCandidateResult* candidate =
            findCandidate(result, request.selectedCandidateIndex);
        if (candidate == nullptr || !candidate->feasible) {
            output.errors << "StructureOptimization.Export.CandidateNotFeasible";
            return output;
        }
        const QString modelDirectory = directory.filePath(
            QString("candidate-%1").arg(candidate->index));
        QStringList exportErrors;
        if (!StructureCandidateExporter::exportModel(problem, *candidate, modelDirectory,
                                                      exportErrors)) {
            output.errors.append(exportErrors);
            return output;
        }
        output.writtenFiles << modelDirectory;
    }

    output.ok = output.errors.isEmpty();
    return output;
}

} // namespace rws
