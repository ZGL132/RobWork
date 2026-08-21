#ifndef RWS_STRUCTUREOPTIMIZATION_CANDIDATEPATCHMERGE_HPP
#define RWS_STRUCTUREOPTIMIZATION_CANDIDATEPATCHMERGE_HPP

#include "CandidatePatch.hpp"

#include <vector>

namespace rws {

struct CandidatePatchMergeResult
{
    bool ok = false;
    CandidatePatch patch;
    std::vector< StructureOptimizationDiagnostic > diagnostics;
};

/** Merges immutable adapter patches without applying them to a model. */
class CandidatePatchMerger
{
  public:
    static CandidatePatchMergeResult merge(const std::vector< CandidatePatch >& patches);
};

}    // namespace rws

#endif    // RWS_STRUCTUREOPTIMIZATION_CANDIDATEPATCHMERGE_HPP
