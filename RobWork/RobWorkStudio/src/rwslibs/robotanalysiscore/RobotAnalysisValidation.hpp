#ifndef RWS_ROBOTANALYSISCORE_ROBOTANALYSISVALIDATION_HPP
#define RWS_ROBOTANALYSISCORE_ROBOTANALYSISVALIDATION_HPP

#include "RobotAnalysisTypes.hpp"

#include <vector>

namespace rws {

//! @brief Qt-free validation helpers shared by robot analysis plugins.
class RobotAnalysisValidation
{
  public:
    static std::vector< AnalysisWarning > validateTaskPoint (const TaskPoint& point);
    static std::vector< AnalysisWarning > validatePayload (const PayloadSpec& payload);
    static std::vector< AnalysisWarning > validateAnalysisResult (const AnalysisResult& result);
    static std::vector< AnalysisWarning > validateRobotDesignContext (const RobotDesignContext& context);

    static bool hasErrors (const std::vector< AnalysisWarning >& warnings);
};

}    // namespace rws

#endif    // RWS_ROBOTANALYSISCORE_ROBOTANALYSISVALIDATION_HPP
