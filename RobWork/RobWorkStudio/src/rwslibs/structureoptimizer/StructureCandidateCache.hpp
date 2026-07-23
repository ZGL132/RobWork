#ifndef RWS_STRUCTUREOPTIMIZATION_STRUCTURECANDIDATECACHE_HPP
#define RWS_STRUCTUREOPTIMIZATION_STRUCTURECANDIDATECACHE_HPP

#include "StructureOptimizationTypes.hpp"
#include <map>

namespace rws {

class StructureCandidateCache {
  public:
    void put(const StructureOptimizationProblem& problem,
             const std::vector<double>& values,
             StructureEvaluationStage stage,
             const StructureCandidateResult& result);

    bool find(const StructureOptimizationProblem& problem,
              const std::vector<double>& values,
              StructureEvaluationStage stage,
              StructureCandidateResult& result) const;

    void clear();
    std::size_t hitCount() const { return _hits; }
    std::size_t size() const { return _cache.size(); }

  private:
    struct Key {
        std::vector<long long> quantizedValues;
        std::size_t evaluationHash = 0;
        StructureEvaluationStage stage = StructureEvaluationStage::Quick;
        bool operator<(const Key& rhs) const;
    };
    Key makeKey(const StructureOptimizationProblem& problem,
                const std::vector<double>& values,
                StructureEvaluationStage stage) const;

    mutable std::size_t _hits = 0;
    std::map<Key, StructureCandidateResult> _cache;
};

} // namespace rws
#endif
