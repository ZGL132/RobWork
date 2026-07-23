#ifndef RWS_STRUCTUREOPTIMIZATION_STRUCTURESENSITIVITYANALYZER_HPP
#define RWS_STRUCTUREOPTIMIZATION_STRUCTURESENSITIVITYANALYZER_HPP

#include "StructureOptimizationTypes.hpp"
#include "StructureOptimizationStrategy.hpp"
#include "StructureCandidateCache.hpp"

namespace rws {

//! @brief 灵敏度分析器。
//!
//! 对最佳候选解的每个启用设计变量施加 ±step 扰动，
//! 重新评估并记录得分降幅，从而识别最敏感 / 最脆弱的变量。
class StructureSensitivityAnalyzer {
  public:
    //! @brief 执行灵敏度分析。
    //! @param problem  优化问题定义。
    //! @param best  当前最佳候选解。
    //! @param evaluator  用于重新评估扰动后候选解的评估器。
    //! @param callbacks  取消 / 进度回调。
    //! @param cache  可选评估缓存。
    //! @return 灵敏度分析结果，包含每个扰动的入口、统计量与鲁棒性等级。
    StructureSensitivityResult analyze(
        const StructureOptimizationProblem& problem,
        const StructureCandidateResult& best,
        IStructureCandidateEvaluator& evaluator,
        const StructureOptimizationCallbacks& callbacks,
        StructureCandidateCache* cache = nullptr);
};

} // namespace rws
#endif // RWS_STRUCTUREOPTIMIZATION_STRUCTURESENSITIVITYANALYZER_HPP
