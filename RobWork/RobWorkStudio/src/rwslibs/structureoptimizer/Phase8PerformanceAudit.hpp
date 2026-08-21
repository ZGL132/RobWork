#ifndef RWS_STRUCTUREOPTIMIZATION_PHASE8PERFORMANCEAUDIT_HPP
#define RWS_STRUCTUREOPTIMIZATION_PHASE8PERFORMANCEAUDIT_HPP

#include "StructureOptimizationTypes.hpp"

#include <string>
#include <vector>

namespace rws {

enum class Phase8PerformanceSeverity { Warning, Error };

struct Phase8PerformanceBudget
{
    int maxGeneratedCandidates = 100000;
    double maxTotalSeconds = 3600.0;
    double maxModelBuildSeconds = 1800.0;
    double maxEvaluationSeconds = 3600.0;
    double minimumCacheHitRate = 0.0;
};

struct Phase8PerformanceFinding
{
    Phase8PerformanceSeverity severity = Phase8PerformanceSeverity::Warning;
    std::string code;
    std::string message;
};

struct Phase8PerformanceAuditResult
{
    bool valid = false;
    bool withinBudget = false;
    std::vector<Phase8PerformanceFinding> findings;

    bool hasCode(const std::string& code) const;
};

/**
 * @brief 对优化运行诊断执行发布前性能预算审计。
 *
 * 审计器不测量墙钟时间，只验证运行结果中已经记录的不可变诊断，避免在
 * 报告阶段重新执行评价器或改变候选结果。
 */
class Phase8PerformanceAudit
{
public:
    static Phase8PerformanceAuditResult audit(
        const StructureOptimizationResult& result,
        const Phase8PerformanceBudget& budget = Phase8PerformanceBudget());
};

} // namespace rws

#endif // RWS_STRUCTUREOPTIMIZATION_PHASE8PERFORMANCEAUDIT_HPP
