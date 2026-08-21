#ifndef RWS_STRUCTUREOPTIMIZATION_PHASE8ACCEPTANCE_HPP
#define RWS_STRUCTUREOPTIMIZATION_PHASE8ACCEPTANCE_HPP

#include "StructureOptimizationTypes.hpp"

#include <string>
#include <vector>

namespace rws {

struct Phase8AcceptanceFinding
{
    std::string code;
    std::string message;
};

struct Phase8AcceptanceResult
{
    bool passed = false;
    std::vector<Phase8AcceptanceFinding> findings;

    bool hasCode(const std::string& code) const;
};

/**
 * @brief Phase 8 全链验收的纯核心检查器。
 *
 * 该类只检查不可变的项目/结果快照，不访问 QWidget 或 WorkCell，便于在
 * model-only、CI 和发布前审计中复用同一套准入规则。
 */
class Phase8Acceptance
{
public:
    static Phase8AcceptanceResult validateResult(const StructureOptimizationResult& result);

    static Phase8AcceptanceResult compareDeterministic(
        const StructureOptimizationResult& first,
        const StructureOptimizationResult& second);
};

} // namespace rws

#endif // RWS_STRUCTUREOPTIMIZATION_PHASE8ACCEPTANCE_HPP
