#include "CandidatePreviewController.hpp"

#include "StructureCandidateExporter.hpp"

#include <QDir>
#include <QTemporaryDir>

#include <cmath>

namespace rws {

CandidatePreviewController::CandidatePreviewController(IWorkCellPreviewHost* host)
    : _host(host)
{
}

CandidatePreviewController::~CandidatePreviewController()
{
    clearPreview();
}

bool CandidatePreviewController::preview(
    const StructureOptimizationProblem& problem, const StructureCandidateResult& candidate,
    QString* error)
{
    if (_host == nullptr || !candidate.feasible ||
        candidate.status != StructureCandidateStatus::Feasible) {
        if (error != nullptr)
            *error = candidate.status == StructureCandidateStatus::Pending
                         ? "StructureOptimization.Preview.CandidateStale"
                         : "StructureOptimization.Preview.CandidateNotFeasible";
        return false;
    }
    for (double value : candidate.values) {
        if (!std::isfinite(value)) {
            if (error != nullptr)
                *error = "StructureOptimization.Preview.CandidateArtifactInvalid";
            return false;
        }
    }

    std::unique_ptr<QTemporaryDir> temporary(new QTemporaryDir(
        QDir::tempPath() + "/structure-optimizer-preview-XXXXXX"));
    if (!temporary->isValid()) {
        if (error != nullptr)
            *error = "StructureOptimization.Preview.TempDirectoryFailed";
        return false;
    }

    QStringList exportErrors;
    if (!StructureCandidateExporter::exportModel(problem, candidate, temporary->path(), exportErrors)) {
        if (error != nullptr)
            *error = exportErrors.join("\n");
        return false;
    }

    const QStringList workCells = QDir(temporary->path()).entryList(
        {"*.wc.xml"}, QDir::Files, QDir::Name);
    if (workCells.isEmpty()) {
        if (error != nullptr)
            *error = "StructureOptimization.Preview.WorkCellMissing";
        return false;
    }

    const QString source = _previewedCandidateIndex >= 0
                               ? _sourceWorkCellPath
                               : _host->currentWorkCellPath();
    QString openError;
    const QString workCellPath = QDir(temporary->path()).filePath(workCells.front());
    if (!_host->openWorkCell(workCellPath, &openError)) {
        if (error != nullptr)
            *error = "StructureOptimization.Preview.OpenFailed: " + openError;
        return false;
    }

    // 只有候选工件已经成功加载后才替换当前临时目录和源路径，失败不会污染预览状态。
    _sourceWorkCellPath = source;
    _temporaryDirectory = std::move(temporary);
    _previewedCandidateIndex = candidate.index;
    if (error != nullptr)
        error->clear();
    return true;
}

void CandidatePreviewController::clearPreview()
{
    if (_previewedCandidateIndex >= 0 && _host != nullptr && !_sourceWorkCellPath.isEmpty()) {
        QString ignored;
        _host->openWorkCell(_sourceWorkCellPath, &ignored);
    }
    _temporaryDirectory.reset();
    _sourceWorkCellPath.clear();
    _previewedCandidateIndex = -1;
}

int CandidatePreviewController::previewedCandidateIndex() const
{
    return _previewedCandidateIndex;
}

} // namespace rws
