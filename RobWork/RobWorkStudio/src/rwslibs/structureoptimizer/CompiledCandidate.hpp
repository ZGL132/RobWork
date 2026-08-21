#ifndef RWS_STRUCTUREOPTIMIZATION_COMPILEDCANDIDATE_HPP
#define RWS_STRUCTUREOPTIMIZATION_COMPILEDCANDIDATE_HPP

#include "CanonicalKinematicModel.hpp"
#include "DesignVector.hpp"

#include <map>
#include <string>
#include <vector>

namespace rws {

enum class CandidateCompileStatus { CompileFailed, Compiled };

/** Immutable-by-convention output of the pure canonical candidate compiler. */
struct CompiledCandidate
{
    CandidateCompileStatus status = CandidateCompileStatus::CompileFailed;
    std::string candidateId;
    DesignVector designVector;
    CanonicalKinematicModel kinematicModel;
    std::map< std::string, DerivedValue > derivedValues;
    std::vector< std::string > artifactFingerprints;
    std::vector< StructureOptimizationDiagnostic > diagnostics;
    std::string fingerprint;
};

}    // namespace rws

#endif    // RWS_STRUCTUREOPTIMIZATION_COMPILEDCANDIDATE_HPP
