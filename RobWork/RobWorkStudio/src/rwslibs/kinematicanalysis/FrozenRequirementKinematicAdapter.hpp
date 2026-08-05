#ifndef RWS_KINEMATICANALYSIS_FROZENREQUIREMENTKINEMATICADAPTER_HPP
#define RWS_KINEMATICANALYSIS_FROZENREQUIREMENTKINEMATICADAPTER_HPP

#include "KinematicAnalysisTypes.hpp"

#include <rwslibs/robotanalysiscore/RequirementExecutionTypes.hpp>

#include <QJsonObject>

#include <string>
#include <vector>

namespace rw { namespace kinematics { class State; } }
namespace rw { namespace models { class WorkCell; } }

namespace rws {

struct FrozenRequirementArtifact;

/**
 * @brief 已验证冻结需求输入运动学分析的类型(输入契约)。
 *
 * 工位任务点沿用既有 TaskPoint 运动学类型；工作区覆盖盒保留执行契约的规范类型
 * RequirementExecutionRegion，使采样、朝向、接受阈值与验证阶段在适配器边界不会
 * 漂移。本类型仅作为输入契约：适配器只做校验与投影，不运行 IK，也不回写需求工件。
 */
struct FrozenKinematicRequirementInput
{
    std::vector<TaskPoint> tasks; // 关键工位任务点
    std::vector<RequirementExecutionRegion> workspaceRegions; // 工作区覆盖盒(执行契约类型)
};

/**
 * @brief 将冻结工程需求工件转换为运动学分析任务点。
 *
 * 此适配器是 EngineeringRequirements 与 KinematicAnalysis 的只读边界：只接收已经冻结
 * 的 CompiledPoseTask，先复核当前 WorkCell/State，再输出现有 TaskPoint。它不重新解析
 * 工程师的编辑态规则，也不写回需求项目，因此一次分析不会改变已审计的需求工件。
 */
class FrozenRequirementKinematicAdapter
{
  public:
    /**
     * @brief 从独立冻结工件或 EngineeringRequirements 项目根对象解析冻结工件。
     *
     * EngineeringRequirements 的保存格式以可编辑 RequirementSet 为根对象，并把经
     * 冻结校验的审计工件放在 frozenArtifact 字段；审计系统也可以单独保存工件对象。
     * 此函数是两种文件形态的唯一兼容边界。它特别区分“项目尚未冻结”和“选错文件”，
     * 让界面能够给工程师可操作的提示，而不是笼统报告 schema 不支持。
     */
    static bool parseArtifactJson(const QJsonObject& projectOrArtifact,
                                  FrozenRequirementArtifact& artifact,
                                  std::string* error = nullptr);

    /**
     * @brief 在当前 WorkCell 和实时 State 与冻结场景一致时，转换为运动学任务点。
     */
    static bool apply(const FrozenRequirementArtifact& artifact,
                      const rw::models::WorkCell& workcell,
                      const rw::kinematics::State& state,
                      std::vector<TaskPoint>& output,
                      std::string* error = nullptr);
    static bool apply(const FrozenRequirementArtifact& artifact,
                      const rw::models::WorkCell& workcell,
                      const rw::kinematics::State& state,
                      std::vector<TaskPoint>& output,
                      std::string* error,
                      const std::string& artifactBaseDirectory);
    // —— FrozenKinematicRequirementInput 输出重载 ——
    // 同时输出工位任务点与工作区覆盖盒；其余语义与 apply(vector<TaskPoint>&) 一致。
    static bool apply(const FrozenRequirementArtifact& artifact,
                      const rw::models::WorkCell& workcell,
                      const rw::kinematics::State& state,
                      FrozenKinematicRequirementInput& output,
                      std::string* error = nullptr);
    static bool apply(const FrozenRequirementArtifact& artifact,
                      const rw::models::WorkCell& workcell,
                      const rw::kinematics::State& state,
                      FrozenKinematicRequirementInput& output,
                      std::string* error,
                      const std::string& artifactBaseDirectory);

    static bool applyWithValidation(const FrozenRequirementArtifact& artifact,
                      const rw::models::WorkCell& workcell,
                      const rw::kinematics::State& state,
                      std::vector<TaskPoint>& output,
                      std::string* error,
                      bool* robotStateChanged = nullptr,
                      std::vector<std::string>* warnings = nullptr);
    static bool applyWithValidation(const FrozenRequirementArtifact& artifact,
                      const rw::models::WorkCell& workcell,
                      const rw::kinematics::State& state,
                      std::vector<TaskPoint>& output,
                      std::string* error,
                      bool* robotStateChanged,
                      std::vector<std::string>* warnings,
                      const std::string& artifactBaseDirectory);
    // —— FrozenKinematicRequirementInput 校验输出重载 ——
    // 与 vector<TaskPoint> 版本语义一致，额外同时输出工作区覆盖盒。
    static bool applyWithValidation(const FrozenRequirementArtifact& artifact,
                      const rw::models::WorkCell& workcell,
                      const rw::kinematics::State& state,
                      FrozenKinematicRequirementInput& output,
                      std::string* error,
                      bool* robotStateChanged = nullptr,
                      std::vector<std::string>* warnings = nullptr);
    static bool applyWithValidation(const FrozenRequirementArtifact& artifact,
                      const rw::models::WorkCell& workcell,
                      const rw::kinematics::State& state,
                      FrozenKinematicRequirementInput& output,
                      std::string* error,
                      bool* robotStateChanged,
                      std::vector<std::string>* warnings,
                      const std::string& artifactBaseDirectory);
};

} // namespace rws

#endif // RWS_KINEMATICANALYSIS_FROZENREQUIREMENTKINEMATICADAPTER_HPP
