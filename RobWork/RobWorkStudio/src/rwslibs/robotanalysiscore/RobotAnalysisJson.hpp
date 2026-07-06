#ifndef RWS_ROBOTANALYSISCORE_ROBOTANALYSISJSON_HPP
#define RWS_ROBOTANALYSISCORE_ROBOTANALYSISJSON_HPP

#include "RobotAnalysisTypes.hpp"

#include <string>

namespace rws {

//! @brief JSON serialization helpers for RobotAnalysisCore data models.
class RobotAnalysisJson
{
  public:
    static std::string toJson (const TaskPoint& point);
    static std::string toJson (const PayloadSpec& payload);
    static std::string toJson (const RobotDesignContext& context);
    static std::string toJson (const AnalysisResult& result);

    static bool fromJson (const std::string& json, TaskPoint& point, std::string* error = nullptr);
    static bool fromJson (const std::string& json, PayloadSpec& payload, std::string* error = nullptr);
    static bool fromJson (const std::string& json, RobotDesignContext& context,
                          std::string* error = nullptr);
    static bool fromJson (const std::string& json, AnalysisResult& result,
                          std::string* error = nullptr);
};

}    // namespace rws

#endif    // RWS_ROBOTANALYSISCORE_ROBOTANALYSISJSON_HPP
