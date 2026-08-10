#ifndef RWS_STRUCTUREOPTIMIZER_ENGINEERINGREQUIREMENTARTIFACTADAPTER_HPP
#define RWS_STRUCTUREOPTIMIZER_ENGINEERINGREQUIREMENTARTIFACTADAPTER_HPP

#include "StructureOptimizationTypes.hpp"

#include <string>

namespace rws {

// 前置声明冻结工程需求工件结构体，避免包含重头文件
struct FrozenRequirementArtifact;

/**
 * @brief 工程需求工件适配器类。
 * 
 * 作用：将上游需求插件导出的冻结需求工件（FrozenRequirementArtifact）单向转换并映射为
 * 结构优化引擎内部认识的优化问题结构体（StructureOptimizationProblem）。
 * 
 * 设计哲学：
 * 1. 唯一数据边界：它是上游需求定义与下游结构优化算子之间的唯一数据桥梁；
 * 2. 单向只读：绝不读取编辑态的 RequirementSet，保证审计可追溯；
 * 3. 严格安全边界：不为未实现的轨迹、动力学或驱动语义虚构评价结论，仅映射当前 P2 阶段
 *    能够可靠支持的离散位姿任务和单个 WORLD 坐标系轴对齐覆盖盒。
 */
class EngineeringRequirementArtifactAdapter
{
  public:
    /**
     * @brief 校验冻结工件与当前模型身份后，以工件内容原子化更新优化问题的任务点与覆盖盒。
     * 
     * 内部流程：
     *  1. 检查工件状态，确保其处于“已冻结 (Frozen)”状态且模型身份 (Model Identity) 匹配；
     *  2. 检查工件语义是否在当前 P2 引擎支持范围内（如：拒绝多个覆盖区域、拒绝非 WORLD 坐标系包围盒）；
     *  3. 若校验通过，原子化替换 problem.tasks 与 problem.context.taskPoints 两个集合；
     *  4. 更新 problem.workspaceCoverageBox 空间包围盒参数；
     *  5. 记录需求版本 ID 与生成时间等审计追溯来源信息，返回 true；
     *  6. 若校验失败，写入具体原因至 error 参数，保持 problem 原封不动，返回 false。
     * 
     * @param artifact 上游传入的冻结需求工件对象（只读）
     * @param problem [in, out] 待被更新的目标结构优化问题定义结构体
     * @param error [out] 可选的输出错误描述信息指针（若失败则写入具体原因）
     * @return true  工件合规且成功应用更新到优化问题中
     * @return false 工件未冻结、身份不匹配或包含超纲语义，拒绝更新
     */
    static bool apply(const FrozenRequirementArtifact& artifact,
                      StructureOptimizationProblem& problem,
                      std::string* error = nullptr);
};

} // namespace rws

#endif // RWS_STRUCTUREOPTIMIZER_ENGINEERINGREQUIREMENTARTIFACTADAPTER_HPP