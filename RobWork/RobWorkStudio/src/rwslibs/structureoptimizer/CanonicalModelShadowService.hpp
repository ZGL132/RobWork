#ifndef RWS_STRUCTUREOPTIMIZATION_CANONICALMODELSHADOWSERVICE_HPP
#define RWS_STRUCTUREOPTIMIZATION_CANONICALMODELSHADOWSERVICE_HPP

#include "KinematicModelImporter.hpp"
#include "StructureOptimizationTypes.hpp"

#include <string>

namespace rws {

/** Keeps the optional Phase-1 canonical snapshot separate from the legacy evaluator input. */
class CanonicalModelShadowService
{
  public:
    /** Imports and fingerprints a canonical baseline without mutating legacy optimization fields. */
    static bool attach(const KinematicImportRequest& request,
                       StructureOptimizationProblem& problem,
                       std::string* error = nullptr);

    /** Compares a freshly imported source model to a persisted canonical baseline. */
    static CanonicalModelShadowStatus assess(const CanonicalModelShadow& shadow,
                                             const CanonicalKinematicModel& currentModel);
};

}    // namespace rws

#endif    // RWS_STRUCTUREOPTIMIZATION_CANONICALMODELSHADOWSERVICE_HPP
