#ifndef RWS_ENGINEERINGREQUIREMENTS_ORIENTATIONRULERESOLVER_HPP
#define RWS_ENGINEERINGREQUIREMENTS_ORIENTATIONRULERESOLVER_HPP

#include "EngineeringRequirementTypes.hpp"
#include <string>
namespace rw { namespace kinematics { class State; } }
namespace rw { namespace models { class WorkCell; } }
namespace rws {
/** @brief 在冻结阶段将工艺姿态规则解析为 P2 可执行的固定 RPY 代表姿态。 */
class OrientationRuleResolver {
  public:
    static bool applyToStation(KeyStation& station, const rw::models::WorkCell& workcell,
                               const rw::kinematics::State& state, std::string* error = nullptr);
};
} // namespace rws
#endif
