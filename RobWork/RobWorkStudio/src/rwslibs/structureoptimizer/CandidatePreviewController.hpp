#ifndef RWS_STRUCTUREOPTIMIZATION_CANDIDATEPREVIEWCONTROLLER_HPP
#define RWS_STRUCTUREOPTIMIZATION_CANDIDATEPREVIEWCONTROLLER_HPP

#include "StructureOptimizationTypes.hpp"

#include <QString>

#include <memory>

class QTemporaryDir;

namespace rws {

class IWorkCellPreviewHost
{
public:
    virtual ~IWorkCellPreviewHost() = default;
    virtual QString currentWorkCellPath() = 0;
    virtual bool openWorkCell(const QString& path, QString* error) = 0;
};

class CandidatePreviewController
{
public:
    explicit CandidatePreviewController(IWorkCellPreviewHost* host);
    ~CandidatePreviewController();

    bool preview(const StructureOptimizationProblem& problem,
                 const StructureCandidateResult& candidate,
                 QString* error = nullptr);
    void clearPreview();
    int previewedCandidateIndex() const;

private:
    IWorkCellPreviewHost* _host;
    QString _sourceWorkCellPath;
    std::unique_ptr<QTemporaryDir> _temporaryDirectory;
    int _previewedCandidateIndex = -1;
};

} // namespace rws

#endif // RWS_STRUCTUREOPTIMIZATION_CANDIDATEPREVIEWCONTROLLER_HPP
