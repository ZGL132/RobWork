#ifndef RWS_STRUCTUREOPTIMIZATION_CANDIDATEPATCHAPPLY_HPP
#define RWS_STRUCTUREOPTIMIZATION_CANDIDATEPATCHAPPLY_HPP

#include "CanonicalKinematicModel.hpp"
#include "CandidatePatch.hpp"

#include <vector>

namespace rws {

struct CandidatePatchApplyResult
{
    bool ok = false;
    CanonicalKinematicModel model;
    std::vector< std::string > generatedArtifacts;
    std::vector< StructureOptimizationDiagnostic > diagnostics;
};

/** Applies one validated merged patch to a copied canonical baseline. */
class CandidatePatchApplier
{
  public:
    static CandidatePatchApplyResult apply(const CanonicalKinematicModel& baseline,
                                           const CandidatePatch& patch);
};

}    // namespace rws

#endif    // RWS_STRUCTUREOPTIMIZATION_CANDIDATEPATCHAPPLY_HPP
