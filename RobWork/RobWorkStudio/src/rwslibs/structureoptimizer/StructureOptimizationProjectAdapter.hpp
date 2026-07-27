#ifndef RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONPROJECTADAPTER_HPP
#define RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONPROJECTADAPTER_HPP

#include "StructureOptimizationTypes.hpp"

#include <QString>

namespace rws {

class StructureOptimizationProjectAdapter
{
public:
    static bool loadProject(const QString& path, StructureOptimizationProblem& out,
                            int* selectedCandidateIndex = nullptr,
                            QString* error = nullptr);
    static bool saveProject(const QString& path, const StructureOptimizationProblem& problem,
                            int selectedCandidateIndex = -1, QString* error = nullptr);
};

} // namespace rws

#endif // RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONPROJECTADAPTER_HPP
