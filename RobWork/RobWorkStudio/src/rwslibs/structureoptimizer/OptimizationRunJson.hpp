#ifndef RWS_STRUCTUREOPTIMIZATION_OPTIMIZATIONRUNJSON_HPP
#define RWS_STRUCTUREOPTIMIZATION_OPTIMIZATIONRUNJSON_HPP

#include "CandidateResult.hpp"
#include "OptimizationRunSnapshot.hpp"

#include <string>

namespace rws {

std::string optimizationRunSnapshotToJson(const OptimizationRunSnapshot& snapshot);
bool optimizationRunSnapshotFromJson(const std::string& json,
                                     OptimizationRunSnapshot& snapshot,
                                     std::string* error = nullptr);
std::string candidateResultResourceToJson(const CandidateResult& result,
                                          const std::string& resourceId);
bool candidateResultResourceFromJson(const std::string& json, CandidateResult& result,
                                     std::string* resourceId, std::string* error = nullptr);
std::string canonicalOptimizationRunJson(const std::string& json);
std::string optimizationRunSha256(const std::string& canonicalJson);

} // namespace rws

#endif
