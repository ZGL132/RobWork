#ifndef RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONCSV_HPP
#define RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONCSV_HPP

#include "StructureOptimizationTypes.hpp"
#include <string>
#include <vector>

namespace rws {

//! @brief CSV 导出辅助类。
class StructureOptimizationCsv {
  public:
    //! @brief 生成候选解列表 CSV (每行一个候选解)。
    static std::string candidatesCsv(const StructureOptimizationProblem& problem,
                                     const StructureOptimizationResult& result);

    //! @brief 生成任务点明细 CSV (每行一个候选项的任务点指标)。
    static std::string taskDetailCsv(const StructureOptimizationProblem& problem,
                                     const StructureOptimizationResult& result);
};

} // namespace rws
#endif // RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONCSV_HPP
