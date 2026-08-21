#ifndef RWS_STRUCTUREOPTIMIZATION_PHASE8RESOURCEAUDIT_HPP
#define RWS_STRUCTUREOPTIMIZATION_PHASE8RESOURCEAUDIT_HPP

#include "OptimizationRunStateMachine.hpp"
#include "StructureOptimizationTypes.hpp"

#include <string>
#include <vector>

namespace rws {

struct Phase8ControllerSnapshot
{
    OptimizationRunState state = OptimizationRunState::Idle;
    bool running = false;
    bool paused = false;
    bool baselineRunning = false;
};

struct Phase8ResourceAuditResult
{
    bool passed = false;
    std::vector<std::string> errors;

    bool hasCode(const std::string& code) const;
};

/**
 * @brief 检查 Phase 8 发布前的线程状态和临时资源安全。
 *
 * 该检查器接收控制器快照与不可变结果，不持有线程、不等待 Future，也不在
 * 审计阶段清理文件；它只负责发现“运行中仍可发布”或“结果携带临时路径”等错误。
 */
class Phase8ResourceAudit
{
public:
    static Phase8ResourceAuditResult auditController(
        const Phase8ControllerSnapshot& snapshot);
    static Phase8ResourceAuditResult auditResult(
        const StructureOptimizationResult& result);
};

} // namespace rws

#endif // RWS_STRUCTUREOPTIMIZATION_PHASE8RESOURCEAUDIT_HPP
