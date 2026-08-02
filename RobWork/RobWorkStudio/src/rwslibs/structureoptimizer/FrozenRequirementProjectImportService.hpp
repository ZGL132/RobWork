#ifndef RWS_STRUCTUREOPTIMIZER_FROZENREQUIREMENTPROJECTIMPORTSERVICE_HPP
#define RWS_STRUCTUREOPTIMIZER_FROZENREQUIREMENTPROJECTIMPORTSERVICE_HPP

#include <QString>

#include <string>

namespace rws {

struct FrozenRequirementValidationResult;
struct StructureOptimizationProblem;

} // namespace rws

namespace rw { namespace kinematics { class State; } }
namespace rw { namespace models { class WorkCell; } }

namespace rws {

/**
 * @brief 从需求定义插件导出的冻结需求文件创建结构优化项目。
 *
 * 此服务是 StructureOptimizer 消费 EngineeringRequirements 的文件级边界。它只读取已经
 * 冻结并持有完整审计指纹的 `frozenArtifact`，然后加载其绑定的 RobotModelSpec，并委托
 * EngineeringRequirementArtifactAdapter 生成 P2 可执行的运动学结构优化输入。编辑态需求
 * 即使包含任务点，也不能通过本服务进入优化器。
 */
class FrozenRequirementProjectImportService
{
  public:
    /**
     * @brief 读取需求文件，创建含需求来源审计记录的优化问题。
     *
     * 模型绑定路径支持相对路径，且相对基准固定为需求 JSON 所在目录，确保将需求和模型
     * 作为同一工程目录移动后仍能被一致地解析。函数使用局部副本完成全部读取与校验，
     * 成功前不会修改 `problem`，避免 UI 在导入失败后留下半成品项目。
     */
    static bool createProblem(const QString& requirementPath,
                              const rw::models::WorkCell& workcell,
                              const rw::kinematics::State& state,
                              StructureOptimizationProblem& problem,
                              FrozenRequirementValidationResult* validation = nullptr,
                              std::string* error = nullptr);

    static bool createProblem(const QString& requirementPath,
                              const rw::models::WorkCell& workcell,
                              const rw::kinematics::State& state,
                              StructureOptimizationProblem& problem,
                              FrozenRequirementValidationResult* validation,
                              std::string* error,
                              const QString& artifactBaseDirectory);
};

} // namespace rws

#endif // RWS_STRUCTUREOPTIMIZER_FROZENREQUIREMENTPROJECTIMPORTSERVICE_HPP
