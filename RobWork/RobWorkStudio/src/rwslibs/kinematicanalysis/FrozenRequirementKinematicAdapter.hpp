#ifndef RWS_KINEMATICANALYSIS_FROZENREQUIREMENTKINEMATICADAPTER_HPP
#define RWS_KINEMATICANALYSIS_FROZENREQUIREMENTKINEMATICADAPTER_HPP

#include "KinematicAnalysisTypes.hpp"

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
    static bool apply(const FrozenRequirementArtifact& artifact,
                      const rw::models::WorkCell& workcell,
                      const rw::kinematics::State& state,
                      std::vector<TaskPoint>& output,
                      std::string* error = nullptr);
};

} // namespace rws

#endif // RWS_KINEMATICANALYSIS_FROZENREQUIREMENTKINEMATICADAPTER_HPP
