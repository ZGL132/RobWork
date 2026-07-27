#ifndef RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONEXPORTSERVICE_HPP
#define RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONEXPORTSERVICE_HPP

#include "StructureOptimizationTypes.hpp"

#include <QString>
#include <QStringList>

namespace rws {

struct StructureOptimizationExportRequest
{
    QString directory;
    int selectedCandidateIndex = -1;
    bool includeAllCandidates = true;
    bool exportCandidateModel = true;
    bool overwrite = false;
};

struct StructureOptimizationExportResult
{
    QStringList writtenFiles;
    QStringList errors;
    bool ok = false;
};

class StructureOptimizationExportService
{
public:
    static StructureOptimizationExportResult exportAll(
        const StructureOptimizationProblem& problem,
        const StructureOptimizationResult& result,
        const StructureOptimizationExportRequest& request);
};

} // namespace rws

#endif // RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONEXPORTSERVICE_HPP
