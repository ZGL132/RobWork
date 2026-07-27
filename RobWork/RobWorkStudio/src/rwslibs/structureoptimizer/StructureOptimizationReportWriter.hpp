#ifndef RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONREPORTWRITER_HPP
#define RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONREPORTWRITER_HPP

#include "StructureOptimizationTypes.hpp"

#include <string>

namespace rws {

class StructureOptimizationReportWriter
{
public:
    static std::string write(const StructureOptimizationProblem& problem,
                             const StructureOptimizationResult& result);
};

} // namespace rws

#endif // RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONREPORTWRITER_HPP
