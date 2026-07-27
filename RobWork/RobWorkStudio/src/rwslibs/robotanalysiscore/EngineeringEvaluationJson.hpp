#ifndef RWS_ROBOTANALYSISCORE_ENGINEERINGEVALUATIONJSON_HPP
#define RWS_ROBOTANALYSISCORE_ENGINEERINGEVALUATIONJSON_HPP

#include "EngineeringEvaluationTypes.hpp"

#include <string>

namespace rws {

class EngineeringEvaluationJson
{
public:
    static const int SchemaVersion = 1;
    static std::string toJson(const EngineeringEvaluationResult& result);
    static bool fromJson(const std::string& json, EngineeringEvaluationResult& result,
                         std::string* error = nullptr);
};

} // namespace rws

#endif // RWS_ROBOTANALYSISCORE_ENGINEERINGEVALUATIONJSON_HPP
