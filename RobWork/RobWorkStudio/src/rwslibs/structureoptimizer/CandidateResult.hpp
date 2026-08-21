#ifndef RWS_STRUCTUREOPTIMIZATION_CANDIDATERESULT_HPP
#define RWS_STRUCTUREOPTIMIZATION_CANDIDATERESULT_HPP

#include "ConstraintObjective.hpp"
#include "EvaluationStage.hpp"
#include "StructureOptimizationContracts.hpp"

#include <string>
#include <vector>

namespace rws {

struct CandidateResult {
    std::string candidateId;
    CandidateLifecycle lifecycle = CandidateLifecycle::Pending;
    Feasibility feasibility = Feasibility::NotEvaluated;
    AnalysisEvidenceStage evidenceStage = AnalysisEvidenceStage::Quick;
    Quality quality = Quality::Unknown;
    EvaluationCompletion completion;
    std::vector<EvaluationStageResult> stages;
    std::vector<MetricResult> metrics;
    std::vector<ConstraintResult> constraints;
    std::vector<ObjectiveResult> objectives;
    std::vector<double> representativeQ;
    std::vector<std::string> warnings;
    std::string compileDiagnostic;
    std::string evaluationDiagnostic;

    bool feasible() const { return feasibility == Feasibility::Feasible; }
};

struct CandidateAssemblyInput {
    std::string candidateId;
    bool compileSucceeded = true;
    bool evaluationSucceeded = true;
    CandidateLifecycle lifecycle = CandidateLifecycle::Completed;
    Feasibility feasibility = Feasibility::NotEvaluated;
    AnalysisEvidenceStage evidenceStage = AnalysisEvidenceStage::Quick;
    Quality quality = Quality::Unknown;
    EvaluationCompletion completion;
    std::vector<EvaluationStageResult> stages;
    std::vector<MetricResult> metrics;
    std::vector<ConstraintResult> constraints;
    std::vector<ObjectiveResult> objectives;
    std::vector<double> representativeQ;
    std::string compileDiagnostic;
    std::string evaluationDiagnostic;
    std::vector<std::string> warnings;
};

class CandidateResultAssembler {
  public:
    static CandidateResult assemble(const CandidateAssemblyInput& input);
    static int feasibilityRank(Feasibility feasibility);
    static bool betterForRanking(const CandidateResult& left, const CandidateResult& right);
    static StructureCandidateResult toLegacy(const CandidateResult& result,
                                             int index = -1);
};

} // namespace rws

#endif
