#ifndef RWS_STRUCTUREOPTIMIZATION_CANDIDATECOMPILER_HPP
#define RWS_STRUCTUREOPTIMIZATION_CANDIDATECOMPILER_HPP

#include "AdapterRegistry.hpp"
#include "CandidatePatchApply.hpp"
#include "CandidatePatchMerge.hpp"
#include "CompiledCandidate.hpp"

namespace rws {

struct CandidateCompileRequest
{
    const CanonicalKinematicModel* baseline = nullptr;
    const CompiledDesignSpace* designSpace = nullptr;
    const DesignVector* designVector = nullptr;
    const AdapterRegistry* adapterRegistry = nullptr;
    const AdapterCapabilityQuery* capabilities = nullptr;
};

struct CandidateCompileResult
{
    bool ok = false;
    CompiledCandidate candidate;
    std::vector< StructureOptimizationDiagnostic > diagnostics;
};

/** Pure orchestration boundary for canonical candidate compilation. */
class CandidateCompiler
{
  public:
    static CandidateCompileResult compile(const CandidateCompileRequest& request);
};

}    // namespace rws

#endif    // RWS_STRUCTUREOPTIMIZATION_CANDIDATECOMPILER_HPP
