#ifndef RWS_STRUCTUREOPTIMIZATION_CANDIDATEPATCH_HPP
#define RWS_STRUCTUREOPTIMIZATION_CANDIDATEPATCH_HPP

#include "CandidateCompilerDiagnostics.hpp"
#include "ParameterBinding.hpp"

#include <string>
#include <vector>

namespace rws {

/** Explicit value representation for a patch write; the write target stays typed. */
struct CandidatePatchValue
{
    enum class Kind { Scalar, DiscreteOption, ArtifactReference };

    Kind kind = Kind::Scalar;
    double scalarValue = 0.0;
    std::string textValue;

    static CandidatePatchValue scalar(double value);
    static CandidatePatchValue discreteOption(const std::string& optionId);
    static CandidatePatchValue artifactReference(const std::string& reference);
};

struct CandidatePatchWrite
{
    ReadWriteTarget target;
    CandidatePatchValue value;
};

/** An adapter-produced, data-only edit proposal.  It never owns a live model. */
struct CandidatePatch
{
    std::string adapterId;
    int adapterVersion = 0;
    std::string bindingId;
    /** Operational policy-only patches must not claim structural capability benefit. */
    bool affectsStructuralCapability = true;
    /** Explicit S34 SO(3) composition contract consumed by the future S36 applier. */
    PoseDeltaComposition poseDeltaComposition = PoseDeltaComposition::Unknown;
    std::string poseDeltaGroupId;
    std::vector< CandidatePatchWrite > writes;
    std::vector< std::string > generatedArtifacts;
    std::vector< std::string > derivedValueIds;
    std::vector< StructureOptimizationDiagnostic > diagnostics;
};

struct CandidatePatchValidationResult
{
    bool valid = true;
    std::vector< StructureOptimizationDiagnostic > diagnostics;
};

/** Validates typed patch writes against the adapter's declared write set. */
class CandidatePatchValidator
{
  public:
    static CandidatePatchValidationResult validate(
        const CandidatePatch& patch, const std::vector< ReadWriteTarget >& declaredWriteSet);
};

}    // namespace rws

#endif    // RWS_STRUCTUREOPTIMIZATION_CANDIDATEPATCH_HPP
