#ifndef RWS_STRUCTUREOPTIMIZER_ENGINEERINGREQUIREMENTARTIFACTADAPTER_HPP
#define RWS_STRUCTUREOPTIMIZER_ENGINEERINGREQUIREMENTARTIFACTADAPTER_HPP

#include "StructureOptimizationTypes.hpp"

#include <string>

namespace rws {

struct FrozenRequirementArtifact;

/**
 * @brief 将 EngineeringRequirements 的冻结工件单向转换为结构优化问题。
 *
 * 此适配器是需求定义插件和 StructureOptimizer 的唯一数据边界。它绝不读取编辑态
 * RequirementSet，也不会为未实现的轨迹、动力学或驱动语义虚构评价结论；仅将 P2
 * 已支持的冻结位姿任务和单个 WORLD 覆盖盒映射为现有的运动学结构优化输入。
 */
class EngineeringRequirementArtifactAdapter
{
  public:
    /**
     * @brief 校验冻结工件与当前模型身份后，以工件内容更新优化任务、覆盖盒和审计来源。
     *
     * 该函数成功时会替换 problem.tasks 与 problem.context.taskPoints，因为这两个集合
     * 此后共同代表同一份冻结需求。若工件包含 P2 不能可靠解释的语义，例如多个覆盖
     * 区域、非 WORLD 覆盖盒或未冻结状态，则返回 false 并保持 problem 不变。
     */
    static bool apply(const FrozenRequirementArtifact& artifact,
                      StructureOptimizationProblem& problem,
                      std::string* error = nullptr);
};

} // namespace rws

#endif
