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

/**
 * @brief 清理 Markdown 表格单元格中的控制字符和分隔符。
 *
 * 姿态解析证据来源于冻结工件，可能包含管线符或换行。报告必须保持一行一个工位，
 * 因此在输出层转义这些字符，而不修改用于审计和 JSON 交接的原始任务注释。
 */
std::string markdownCell(std::string value)
{
    std::replace(value.begin(), value.end(), '|', '/');
    std::replace(value.begin(), value.end(), '\n', ' ');
    std::replace(value.begin(), value.end(), '\r', ' ');
    return value;
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
        !problem.requirementProvenance.executionFingerprint.empty() ||
        !problem.requirementProvenance.compilerVersion.empty() ||
        !problem.requirementProvenance.frozenAt.empty()) {
        out << "## Engineering Requirement Provenance\n\n";
        out << "- Requirement fingerprint: "
            << problem.requirementProvenance.requirementFingerprint << "\n";
        out << "- Execution fingerprint: "
            << problem.requirementProvenance.executionFingerprint << "\n";
        out << "- WorkCell and State fingerprint: "
            << problem.requirementProvenance.workcellFingerprint << "\n";
        out << "- Requirement compiler: "
            << problem.requirementProvenance.compilerVersion << "\n";
        // 时间统一由需求冻结器以 UTC 写入，报告直接输出原始 ISO-8601 字符串，避免导出机器的
        // 本地时区或显示格式改变审计证据的含义。
        out << "- Frozen at (UTC): "
            << problem.requirementProvenance.frozenAt << "\n\n";

        out << "## Frozen Key Stations\n\n";
        out << "| ID | Name | Level | Reference frame | TCP | Position (m) | RPY (deg) | Orientation evidence |\n";
        out << "| --- | --- | --- | --- | --- | --- | --- | --- |\n";
        for (const OptimizationTaskPoint& task : problem.tasks) {
            const TaskPoint& point = task.point;
            const std::string evidencePrefix = "Orientation resolution: ";
            std::string evidence;
            const std::size_t evidenceOffset = point.note.find(evidencePrefix);
            if (evidenceOffset != std::string::npos) {
                evidence = point.note.substr(evidenceOffset + evidencePrefix.size());
                const std::size_t pendingOffset = evidence.find(" | Approach/retract");
                if (pendingOffset != std::string::npos) evidence.erase(pendingOffset);
            }
            out << "| " << markdownCell(point.id) << " | "
                << markdownCell(point.name) << " | "
                << (task.required ? "Must" : "Should") << " | "
                << markdownCell(point.refFrame) << " | "
                << markdownCell(point.tcpFrame) << " | "
                << point.position[0] << ", " << point.position[1] << ", " << point.position[2] << " | "
                << point.rpyDeg[0] << ", " << point.rpyDeg[1] << ", " << point.rpyDeg[2] << " | "
                << markdownCell(evidence) << " |\n";
        }
        out << "\n";
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
    const StructureCandidateResult* baseline = findCandidate(result, result.baselineCandidateIndex);
    const StructureCandidateResult* best = findCandidate(result, result.bestCandidateIndex);
    if (baseline != nullptr) {
        out << "\n## Baseline Comparison\n\n";
        out << "- Baseline score: " << baseline->totalScore << "\n";
        out << "- Baseline reachability: " << baseline->raw.weightedReachability << "\n";
        out << "- Baseline manipulability P10: " << baseline->raw.manipulabilityP10 << "\n";
        out << "- Baseline joint margin P10: " << baseline->raw.jointMarginP10 << "\n";
        out << "- Baseline kinematic length: " << baseline->raw.totalKinematicLength << " m\n";
    }
    if (best != nullptr) {
        out << std::fixed << std::setprecision(3);
        if (baseline != nullptr) {
            out << "- Best score delta: " << (best->totalScore - baseline->totalScore) << "\n";
            out << "- Best reachability delta: "
                << (best->raw.weightedReachability - baseline->raw.weightedReachability) << "\n";
            out << "- Best length delta: "
                << (best->raw.totalKinematicLength - baseline->raw.totalKinematicLength) << " m\n";
        }
        out << "- Workspace coverage: " << best->raw.workspaceCoverage << " ("
            << best->raw.workspaceOccupiedCellCount << "/"
            << best->raw.workspaceTotalCellCount << " cells)\n";

        // 总覆盖率是兼容旧项目的保守概览（多区域时取最差值），不能替代各工装区域的
        // 独立证据。按区域逐行输出，便于研发工程师追溯某个硬约束失败的真实位置。
        if (!best->raw.workspaceRegionMetrics.empty()) {
            out << "\n## Workspace Coverage Results\n\n";
            out << "| Region | Reference frame | Coverage | Occupied cells | Total cells |\n";
            out << "| --- | --- | ---: | ---: | ---: |\n";
            for (const StructureWorkspaceRegionMetric& metric :
                 best->raw.workspaceRegionMetrics) {
                out << "| " << markdownCell(metric.id) << " | "
                    << markdownCell(metric.referenceFrame) << " | "
                    << metric.coverage << " | "
                    << metric.occupiedCellCount << " | "
                    << metric.totalCellCount << " |\n";
            }
        }
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
