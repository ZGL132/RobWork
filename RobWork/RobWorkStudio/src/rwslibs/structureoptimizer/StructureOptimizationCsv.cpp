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
          "TotalKinematicLength,BaseHeight,EngineeringPreference,ModelBuildSeconds";

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
