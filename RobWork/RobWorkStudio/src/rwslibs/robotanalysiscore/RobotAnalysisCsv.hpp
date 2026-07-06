#ifndef RWS_ROBOTANALYSISCORE_ROBOTANALYSISCSV_HPP
#define RWS_ROBOTANALYSISCORE_ROBOTANALYSISCSV_HPP

#include "RobotAnalysisTypes.hpp"

#include <string>
#include <vector>

namespace rws {

//! @brief CSV helpers for exchanging task point lists with external tools.
class RobotAnalysisCsv
{
  public:
    static std::string taskPointsToCsv (const std::vector< TaskPoint >& points);
    static bool taskPointsFromCsv (const std::string& csv, std::vector< TaskPoint >& points,
                                   std::string* error = nullptr);
};

}    // namespace rws

#endif    // RWS_ROBOTANALYSISCORE_ROBOTANALYSISCSV_HPP
