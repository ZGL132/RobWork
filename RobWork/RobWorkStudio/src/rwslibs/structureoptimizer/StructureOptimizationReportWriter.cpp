#include "StructureOptimizationReportWriter.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace rws {

std::string StructureOptimizationReportWriter::write(
    const StructureOptimizationProblem& problem, const StructureOptimizationResult& result)
{
    std::ostringstream out;
    out << "# Mechanical Arm Structure Optimization Report\n\n";
    out << "## Problem\n\n";
    out << "- Robot: " << problem.context.robotName << "\n";
    out << "- Strategy: " << static_cast<int>(problem.run.strategy) << "\n";
    out << "- Random seed: " << problem.run.randomSeed << "\n";
    out << "- Evaluator: " << problem.evaluation.evaluatorId << "@"
        << problem.evaluation.evaluatorVersion << "\n";
    out << "- Variables: " << problem.variables.size() << "\n";
    out << "- Tasks: " << problem.tasks.size() << "\n";
    out << "- Constraints: " << problem.constraints.size() << "\n\n";
    out << "## Result\n\n";
    out << "- Baseline candidate: " << result.baselineCandidateIndex << "\n";
    out << "- Best candidate: " << result.bestCandidateIndex << "\n";
    out << "- Canceled: " << (result.canceled ? "true" : "false") << "\n";
    out << "- Evaluated candidates: " << result.diagnostics.evaluatedCandidates << "\n\n";
    out << "## Top Candidates\n\n";
    out << "| Index | Feasible | Score | Reachability | Manipulability | Joint margin |\n";
    out << "| --- | --- | ---: | ---: | ---: | ---: |\n";

    std::vector<StructureCandidateResult> candidates = result.candidates;
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const StructureCandidateResult& lhs,
                        const StructureCandidateResult& rhs) {
                         if (lhs.feasible != rhs.feasible)
                             return lhs.feasible;
                         return lhs.totalScore > rhs.totalScore;
                     });
    const std::size_t count = std::min<std::size_t>(10, candidates.size());
    out << std::fixed << std::setprecision(3);
    for (std::size_t i = 0; i < count; ++i) {
        const StructureCandidateResult& candidate = candidates[i];
        out << "| " << candidate.index << " | "
            << (candidate.feasible ? "yes" : "no") << " | "
            << candidate.totalScore << " | "
            << candidate.raw.weightedReachability << " | "
            << candidate.raw.manipulabilityP10 << " | "
            << candidate.raw.minimumJointMargin << " |\n";
    }
    out << "\n## Sensitivity\n\n";
    out << "- Robustness grade: " << result.sensitivity.robustnessGrade << "\n";
    out << "- Maximum score drop: " << result.sensitivity.maximumScoreDrop << "\n\n";
    out << "## Warnings\n\n";
    if (result.warnings.empty())
        out << "None.\n";
    else {
        for (const AnalysisWarning& warning : result.warnings)
            out << "- `" << warning.code << "`: " << warning.message << "\n";
    }
    return out.str();
}

} // namespace rws
