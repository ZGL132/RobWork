#include "StructureOptimizationCsv.hpp"

#include <algorithm>
#include <sstream>

namespace rws {

// =============================================================================
//  CSV 值转义辅助
// =============================================================================

//! @brief 将单个字段转义为 CSV 安全格式。
//!        若字段包含逗号、双引号或换行符，则用双引号包裹并转义内部双引号。
static std::string csvEscape(const std::string& field)
{
    if (field.empty())
        return "\"\""; // 空字符串输出为 "" 空字段

    bool needsQuoting = (field.find(',') != std::string::npos ||
                         field.find('"') != std::string::npos ||
                         field.find('\n') != std::string::npos ||
                         field.find('\r') != std::string::npos);

    if (!needsQuoting)
        return field;

    std::string escaped;
    escaped.reserve(field.size() + 4);
    escaped.push_back('"');
    for (char ch : field) {
        if (ch == '"')
            escaped.append("\"\"");
        else
            escaped.push_back(ch);
    }
    escaped.push_back('"');
    return escaped;
}

static std::string csvEscape(double value)
{
    return csvEscape(std::to_string(value));
}

static std::string csvEscape(int value)
{
    return csvEscape(std::to_string(value));
}

static std::string statusToString(StructureCandidateStatus s)
{
    switch (s) {
        case StructureCandidateStatus::Pending:    return "Pending";
        case StructureCandidateStatus::Feasible:   return "Feasible";
        case StructureCandidateStatus::Infeasible: return "Infeasible";
        case StructureCandidateStatus::Failed:     return "Failed";
        case StructureCandidateStatus::Canceled:   return "Canceled";
    }
    return "Unknown";
}

static std::string stageToString(StructureEvaluationStage stage)
{
    return stage == StructureEvaluationStage::Verified ? "Verified" : "Quick";
}

// =============================================================================
//  candidatesCsv
// =============================================================================

std::string StructureOptimizationCsv::candidatesCsv(
    const StructureOptimizationProblem& problem,
    const StructureOptimizationResult& result)
{
    std::ostringstream os;

    // ── 表头 ───────────────────────────────────────────────────────────────
    os << "Index,Status,Feasible,TotalScore,RequiredReachable,RequiredTaskCount,"
          "ManipulabilityP10,JointMarginP10,CollisionFreeRate,WorkspaceCoverage,"
          "WorkspaceOccupiedCells,WorkspaceTotalCells,WorkspaceDataInsufficient,"
          "EvaluationStage,TotalKinematicLength,BaseHeight,EngineeringPreference,ModelBuildSeconds";

    for (std::size_t vi = 0; vi < problem.variables.size(); ++vi) {
        os << "," << csvEscape(problem.variables[vi].id);
    }
    os << "\n";

    // ── 数据行 ──────────────────────────────────────────────────────────────
    for (const auto& c : result.candidates) {
        os << c.index << ","
           << statusToString(c.status) << ","
           << (c.feasible ? "true" : "false") << ","
           << csvEscape(c.totalScore) << ","
           << c.raw.requiredReachableCount << ","
           << c.raw.requiredTaskCount << ","
           << csvEscape(c.raw.manipulabilityP10) << ","
           << csvEscape(c.raw.jointMarginP10) << ","
           << csvEscape(c.raw.collisionFreeRate) << ","
           << csvEscape(c.raw.workspaceCoverage) << ","
           << c.raw.workspaceOccupiedCellCount << ","
           << c.raw.workspaceTotalCellCount << ","
           << (c.raw.workspaceCoverageDataInsufficient ? "true" : "false") << ","
           << stageToString(c.stage) << ","
           << csvEscape(c.raw.totalKinematicLength) << ","
           << csvEscape(c.raw.baseHeight) << ","
           << csvEscape(c.raw.engineeringPreference) << ","
           << csvEscape(c.raw.modelBuildSeconds);

        for (double v : c.values)
            os << "," << csvEscape(v);

        os << "\n";
    }

    return os.str();
}

std::string StructureOptimizationCsv::auditCsv(
    const StructureOptimizationProblem& problem,
    const StructureOptimizationResult& result)
{
    std::ostringstream os;
    os << "Field,Value\n";
    const auto add = [&os](const std::string& field, const std::string& value) {
        os << csvEscape(field) << "," << csvEscape(value) << "\n";
    };
    const auto addInt = [&add](const std::string& field, int value) {
        add(field, std::to_string(value));
    };
    const auto addSize = [&add](const std::string& field, std::size_t value) {
        add(field, std::to_string(value));
    };

    add("Evaluator", problem.evaluation.evaluatorId + "@" +
        problem.evaluation.evaluatorVersion);
    addInt("GeneratedCandidates", result.diagnostics.generatedCandidates);
    addInt("EvaluatedCandidates", result.diagnostics.evaluatedCandidates);
    addInt("QuickEvaluatedCandidates", result.diagnostics.quickEvaluatedCandidates);
    addInt("VerifiedEliteCandidates", result.diagnostics.verifiedEliteCandidates);
    addInt("FinalVerifiedCandidates", result.diagnostics.finalVerifiedCandidates);
    addInt("SensitivityEvaluations", result.diagnostics.sensitivityEvaluations);
    addInt("CacheHits", result.diagnostics.cacheHits);
    addInt("BestCandidate", result.bestCandidateIndex);
    add("SensitivitySource", "verified evaluator");
    add("RobustnessGrade", result.sensitivity.robustnessGrade);

    std::ostringstream criticalVariables;
    for (std::size_t i = 0; i < result.sensitivity.criticalVariableIds.size(); ++i) {
        if (i > 0)
            criticalVariables << ";";
        criticalVariables << result.sensitivity.criticalVariableIds[i];
    }
    add("CriticalVariables", criticalVariables.str());

    add("WorkspaceCoverageEnabled", problem.evaluation.coverageBox.enabled ? "true" : "false");
    addInt("WorkspaceCellsX", problem.evaluation.coverageBox.cells[0]);
    addInt("WorkspaceCellsY", problem.evaluation.coverageBox.cells[1]);
    addInt("WorkspaceCellsZ", problem.evaluation.coverageBox.cells[2]);
    addInt("QuickWorkspaceSamples", problem.evaluation.quickWorkspace.sampleCount);
    addInt("VerifiedWorkspaceSamples", problem.evaluation.verifiedWorkspace.sampleCount);

    for (const StructureCandidateResult& candidate : result.candidates) {
        if (candidate.index == result.bestCandidateIndex) {
            add("WorkspaceCoverage", std::to_string(candidate.raw.workspaceCoverage));
            addSize("WorkspaceOccupiedCells", candidate.raw.workspaceOccupiedCellCount);
            addSize("WorkspaceTotalCells", candidate.raw.workspaceTotalCellCount);
            break;
        }
    }
    return os.str();
}

// =============================================================================
//  taskDetailCsv
// =============================================================================

std::string StructureOptimizationCsv::taskDetailCsv(
    const StructureOptimizationProblem& problem,
    const StructureOptimizationResult& result)
{
    std::ostringstream os;

    // ── 表头 ───────────────────────────────────────────────────────────────
    os << "CandidateIndex,TaskId,TaskName,Required,Reachable,InCollision,"
          "Manipulability,JointMargin,UsableSolutionCount\n";

    // ── 数据行 ──────────────────────────────────────────────────────────────
    for (const auto& c : result.candidates) {
        for (const auto& tm : c.raw.taskMetrics) {
            os << c.index << ","
               << csvEscape(tm.taskId) << ","
               << csvEscape(tm.taskName) << ","
               << (tm.required ? "true" : "false") << ","
               << (tm.reachable ? "true" : "false") << ","
               << (tm.inCollision ? "true" : "false") << ","
               << csvEscape(tm.manipulability) << ","
               << csvEscape(tm.jointMargin) << ","
               << tm.usableSolutionCount << "\n";
        }
    }

    return os.str();
}

} // namespace rws
