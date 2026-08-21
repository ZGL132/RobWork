#include "CandidateResult.hpp"

namespace rws {

CandidateResult CandidateResultAssembler::assemble(const CandidateAssemblyInput& input)
{
    CandidateResult result;
    result.candidateId = input.candidateId;
    result.lifecycle = input.lifecycle;
    result.feasibility = input.feasibility;
    result.evidenceStage = input.evidenceStage;
    result.quality = input.quality;
    result.completion = input.completion;
    result.stages = input.stages;
    result.metrics = input.metrics;
    result.constraints = input.constraints;
    result.objectives = input.objectives;
    result.representativeQ = input.representativeQ;
    result.compileDiagnostic = input.compileDiagnostic;
    result.evaluationDiagnostic = input.evaluationDiagnostic;
    result.warnings = input.warnings;

    if (!input.compileSucceeded) {
        result.lifecycle = CandidateLifecycle::Failed;
        result.feasibility = Feasibility::NotEvaluated;
    }
    else if (!input.evaluationSucceeded) {
        result.lifecycle = input.completion.canceled ? CandidateLifecycle::Canceled
                                                      : CandidateLifecycle::Failed;
        // A failed evaluation never provides enough evidence to claim either
        // feasibility or infeasibility, regardless of any provisional value
        // carried by the stage input.
        result.feasibility = Feasibility::DataInsufficient;
    }
    else if (input.completion.canceled) {
        result.lifecycle = CandidateLifecycle::Canceled;
        if (result.feasibility == Feasibility::Feasible)
            result.feasibility = Feasibility::DataInsufficient;
    }
    return result;
}

int CandidateResultAssembler::feasibilityRank(Feasibility feasibility)
{
    switch (feasibility) {
    case Feasibility::Feasible: return 3;
    case Feasibility::Infeasible: return 2;
    case Feasibility::DataInsufficient: return 1;
    case Feasibility::NotEvaluated: return 0;
    }
    return 0;
}

bool CandidateResultAssembler::betterForRanking(const CandidateResult& left,
                                                const CandidateResult& right)
{
    return feasibilityRank(left.feasibility) > feasibilityRank(right.feasibility);
}

StructureCandidateResult CandidateResultAssembler::toLegacy(const CandidateResult& result,
                                                             int index)
{
    StructureCandidateResult legacy;
    legacy.index = index;
    legacy.status = result.lifecycle == CandidateLifecycle::Canceled
                        ? StructureCandidateStatus::Canceled
                        : result.lifecycle == CandidateLifecycle::Failed
                              ? StructureCandidateStatus::Failed
                                    : result.feasibility == Feasibility::Feasible
                                    ? StructureCandidateStatus::Feasible
                                    : result.feasibility == Feasibility::Infeasible
                                          ? StructureCandidateStatus::Infeasible
                                          : StructureCandidateStatus::Pending;
    legacy.stage = result.evidenceStage == AnalysisEvidenceStage::Verified
                       ? StructureEvaluationStage::Verified
                       : StructureEvaluationStage::Quick;
    legacy.feasible = result.feasibility == Feasibility::Feasible;
    legacy.warnings = result.warnings;
    if (!result.compileDiagnostic.empty())
        legacy.warnings.push_back(result.compileDiagnostic);
    if (!result.evaluationDiagnostic.empty())
        legacy.warnings.push_back(result.evaluationDiagnostic);
    for (const ConstraintResult& constraint : result.constraints) {
        if (!constraint.satisfied)
            legacy.violatedConstraints.push_back(constraint.id);
    }
    return legacy;
}

} // namespace rws
