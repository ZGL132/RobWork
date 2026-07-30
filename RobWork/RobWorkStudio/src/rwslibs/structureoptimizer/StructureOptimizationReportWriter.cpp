#include "StructureOptimizationReportWriter.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace rws {

namespace {

const StructureCandidateResult* findCandidate(
    const StructureOptimizationResult& result, int index)
{
    for (const StructureCandidateResult& candidate : result.candidates) {
        if (candidate.index == index)
            return &candidate;
    }
    return nullptr;
}

const char* workspaceSamplingModeName(WorkspaceSamplingMode mode)
{
    return mode == WorkspaceSamplingMode::Grid ? "Grid" : "RandomUniform";
}

void writeWorkspaceSampling(std::ostringstream& out, const char* label,
                            const WorkspaceSamplingConfig& sampling)
{
    out << "- " << label << " sampling: mode="
        << workspaceSamplingModeName(sampling.mode)
        << ", samples=" << sampling.sampleCount
        << ", grid steps=" << sampling.gridStepsPerJoint
        << ", seed=" << sampling.randomSeed
        << ", collision=" << (sampling.checkCollision ? "enabled" : "disabled")
        << "\n";
}

} // namespace

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
    out << "- Optimization scope: kinematic structure optimization; "
        << "system evaluators not enabled\n";
    out << "- Trajectory evaluator: not enabled\n";
    out << "- Dynamics evaluator: not enabled\n";
    out << "- Drive selection evaluator: not enabled\n";
    out << "- Variables: " << problem.variables.size() << "\n";
    out << "- Tasks: " << problem.tasks.size() << "\n";
    out << "- Constraints: " << problem.constraints.size() << "\n\n";

    const RobotModelProvenance& provenance = problem.context.modelProvenance;
    const std::string sourceModelPath = provenance.sourceModelPath.empty()
                                            ? problem.context.sourceModelPath
                                            : provenance.sourceModelPath;
    const bool tracked = !provenance.sourceFingerprint.empty() &&
                         !provenance.snapshotFingerprint.empty();
    out << "## Model Provenance\n\n";
    out << "- Source status: " << (tracked ? "Tracked" : "Untracked") << "\n";
    out << "- Source model path: " << sourceModelPath << "\n";
    out << "- Source fingerprint: " << provenance.sourceFingerprint << "\n";
    out << "- Snapshot fingerprint: " << provenance.snapshotFingerprint << "\n\n";

    // 需求冻结工件与机器人模型快照是两条独立审计链：前者证明工艺任务和场景状态，
    // 后者证明可变结构的初始模型。仅当适配器实际消费过冻结工件时才输出本节，避免
    // 将人工编辑的旧式优化项目误标为具有工程需求冻结证据。
    if (!problem.requirementProvenance.requirementFingerprint.empty() ||
        !problem.requirementProvenance.workcellFingerprint.empty() ||
        !problem.requirementProvenance.compilerVersion.empty() ||
        !problem.requirementProvenance.frozenAt.empty()) {
        out << "## Engineering Requirement Provenance\n\n";
        out << "- Requirement fingerprint: "
            << problem.requirementProvenance.requirementFingerprint << "\n";
        out << "- WorkCell and State fingerprint: "
            << problem.requirementProvenance.workcellFingerprint << "\n";
        out << "- Requirement compiler: "
            << problem.requirementProvenance.compilerVersion << "\n";
        // 时间统一由需求冻结器以 UTC 写入，报告直接输出原始 ISO-8601 字符串，避免导出机器的
        // 本地时区或显示格式改变审计证据的含义。
        out << "- Frozen at (UTC): "
            << problem.requirementProvenance.frozenAt << "\n\n";
    }

    out << "## Workspace Coverage Configuration\n\n";
    out << "- Enabled: " << (problem.evaluation.coverageBox.enabled ? "true" : "false")
        << "\n";
    out << "- Box minimum: " << problem.evaluation.coverageBox.minimum[0] << ", "
        << problem.evaluation.coverageBox.minimum[1] << ", "
        << problem.evaluation.coverageBox.minimum[2] << "\n";
    out << "- Box maximum: " << problem.evaluation.coverageBox.maximum[0] << ", "
        << problem.evaluation.coverageBox.maximum[1] << ", "
        << problem.evaluation.coverageBox.maximum[2] << "\n";
    out << "- Grid cells: " << problem.evaluation.coverageBox.cells[0] << " x "
        << problem.evaluation.coverageBox.cells[1] << " x "
        << problem.evaluation.coverageBox.cells[2] << "\n";
    writeWorkspaceSampling(out, "Quick", problem.evaluation.quickWorkspace);
    writeWorkspaceSampling(out, "Verified", problem.evaluation.verifiedWorkspace);
    out << "\n";

    out << "## Result\n\n";
    out << "- Baseline candidate: " << result.baselineCandidateIndex << "\n";
    out << "- Best candidate: " << result.bestCandidateIndex << "\n";
    out << "- Canceled: " << (result.canceled ? "true" : "false") << "\n";
    out << "- Generated candidates: " << result.diagnostics.generatedCandidates << "\n";
    out << "- Evaluated candidates: " << result.diagnostics.evaluatedCandidates << "\n";
    out << "- Quick evaluated candidates: "
        << result.diagnostics.quickEvaluatedCandidates << "\n";
    out << "- Verified elite candidates: "
        << result.diagnostics.verifiedEliteCandidates << "\n";
    out << "- Final verified candidates: "
        << result.diagnostics.finalVerifiedCandidates << "\n";
    out << "- Sensitivity evaluations: "
        << result.diagnostics.sensitivityEvaluations << "\n";
    out << "- Cache hits: " << result.diagnostics.cacheHits << "\n";
    const StructureCandidateResult* best = findCandidate(result, result.bestCandidateIndex);
    if (best != nullptr) {
        out << std::fixed << std::setprecision(3);
        out << "- Workspace coverage: " << best->raw.workspaceCoverage << " ("
            << best->raw.workspaceOccupiedCellCount << "/"
            << best->raw.workspaceTotalCellCount << " cells)\n";
    }
    else {
        out << "- Workspace coverage: unavailable\n";
    }
    out << "\n";
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
    out << "- Sensitivity source: verified evaluator\n";
    out << "- Robustness grade: " << result.sensitivity.robustnessGrade << "\n";
    out << "- Maximum score drop: " << result.sensitivity.maximumScoreDrop << "\n";
    out << "- Critical variables: ";
    if (result.sensitivity.criticalVariableIds.empty()) {
        out << "none\n\n";
    }
    else {
        for (std::size_t i = 0; i < result.sensitivity.criticalVariableIds.size(); ++i) {
            if (i > 0)
                out << ", ";
            out << result.sensitivity.criticalVariableIds[i];
        }
        out << "\n\n";
    }
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
