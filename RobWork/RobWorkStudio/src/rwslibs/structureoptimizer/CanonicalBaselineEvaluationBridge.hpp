#ifndef RWS_STRUCTUREOPTIMIZATION_CANONICALBASELINEEVALUATIONBRIDGE_HPP
#define RWS_STRUCTUREOPTIMIZATION_CANONICALBASELINEEVALUATIONBRIDGE_HPP

#include "AdapterRegistry.hpp"
#include "CandidateResult.hpp"
#include "DesignSpaceCompiler.hpp"
#include "DesignVector.hpp"
#include "EvaluationDeviceBuilder.hpp"
#include "EvaluationPlan.hpp"
#include "StructureOptimizationTypes.hpp"

#include <rwslibs/kinematicanalysis/KinematicAnalysisContext.hpp>

namespace rws {

/** Explicit inputs for evaluating a nominal canonical baseline. */
struct CanonicalBaselineEvaluationRequest
{
    const StructureOptimizationProblem* problem = nullptr;
    const DesignSpaceRegistry* designSpaceRegistry = nullptr;
    const AdapterRegistry* adapterRegistry = nullptr;
    const AdapterCapabilityQuery* adapterCapabilities = nullptr;
    std::vector< DesignVariableDefinition > variables;
    std::vector< ParameterBinding > bindings;
    std::vector< ParameterizationSelection > parameterizationSelections;
    std::vector< DerivedExpression > derivedExpressions;
    std::string deviceName;
    std::string tcpFrame;
    EvaluationPlanCompilerOptions planOptions;
    KinematicThresholds thresholds;
    CancellationToken cancellation;
    bool checkCollision = true;
};

/** Durable audit record for baseline index zero; legacy UI is only a projection. */
struct BaselineEvaluationResult
{
    bool ok = false;
    int baselineIndex = 0;
    DesignVector designVector;
    std::string candidateFingerprint;
    std::string modelFingerprint;
    std::string environmentFingerprint;
    std::string toolFingerprint;
    std::string planFingerprint;
    EvaluationPlan plan;
    CandidateResult candidateResult;
    std::vector< StructureOptimizationDiagnostic > diagnostics;
};

/**
 * Composes the canonical compiler and Verified evaluators for the immutable
 * nominal candidate.  It never reads legacy design values or old evaluators.
 */
class CanonicalBaselineEvaluationBridge
{
  public:
    static BaselineEvaluationResult evaluate(const CanonicalBaselineEvaluationRequest& request);
};

}    // namespace rws

#endif    // RWS_STRUCTUREOPTIMIZATION_CANONICALBASELINEEVALUATIONBRIDGE_HPP
