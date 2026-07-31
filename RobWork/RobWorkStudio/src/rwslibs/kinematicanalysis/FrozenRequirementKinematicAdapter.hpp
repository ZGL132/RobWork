#ifndef RWS_KINEMATICANALYSIS_FROZENREQUIREMENTKINEMATICADAPTER_HPP
#define RWS_KINEMATICANALYSIS_FROZENREQUIREMENTKINEMATICADAPTER_HPP

#include "KinematicAnalysisTypes.hpp"

#include <QJsonObject>

#include <string>
#include <vector>

namespace rw { namespace kinematics { class State; } }
namespace rw { namespace models { class WorkCell; } }

namespace rws {

struct FrozenRequirementArtifact;

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

    static bool applyWithValidation(const FrozenRequirementArtifact& artifact,
                      const rw::models::WorkCell& workcell,
                      const rw::kinematics::State& state,
                      std::vector<TaskPoint>& output,
                      std::string* error,
                      bool* robotStateChanged = nullptr);
};

} // namespace rws

#endif // RWS_KINEMATICANALYSIS_FROZENREQUIREMENTKINEMATICADAPTER_HPP
