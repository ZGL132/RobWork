#ifndef RWS_STRUCTUREOPTIMIZATION_OPTIMIZATIONRUNSTORE_HPP
#define RWS_STRUCTUREOPTIMIZATION_OPTIMIZATIONRUNSTORE_HPP

#include "OptimizationRunJson.hpp"

#include <string>

namespace rws {

struct OptimizationResourceLoadResult {
    OptimizationResourceAvailability availability = OptimizationResourceAvailability::DataUnavailable;
    std::string diagnostic;
    CandidateResult candidate;
};

class OptimizationRunStore {
  public:
    explicit OptimizationRunStore(const std::string& projectRoot);
    bool saveSnapshot(const OptimizationRunSnapshot& snapshot, std::string* error = nullptr);
    bool loadSnapshot(const std::string& runId, OptimizationRunSnapshot& snapshot,
                     std::string* error = nullptr) const;
    bool publishCandidateResult(const std::string& runId, const CandidateResult& result,
                                OptimizationRunResourceRef& ref, std::string* error = nullptr);
    OptimizationResourceLoadResult loadCandidateResult(const OptimizationRunResourceRef& ref) const;
    bool hasSnapshot(const std::string& runId) const;

  private:
    std::string _projectRoot;
};

} // namespace rws

#endif
