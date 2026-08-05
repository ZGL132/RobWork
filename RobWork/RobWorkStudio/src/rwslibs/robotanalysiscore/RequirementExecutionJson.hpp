#ifndef RWS_ROBOTANALYSISCORE_REQUIREMENTEXECUTIONJSON_HPP
#define RWS_ROBOTANALYSISCORE_REQUIREMENTEXECUTIONJSON_HPP

#include "RequirementExecutionTypes.hpp"

#include <QJsonObject>

#include <string>

namespace rws {

class RequirementExecutionJson {
  public:
    static QJsonObject toObject(const RequirementExecutionSet& value);
    static bool fromObject(const QJsonObject& object,
                           RequirementExecutionSet& value,
                           std::string* error = nullptr);
    static std::string toJson(const RequirementExecutionSet& value);
    static bool fromJson(const std::string& json,
                         RequirementExecutionSet& value,
                         std::string* error = nullptr);
    static std::string fingerprint(const RequirementExecutionSet& value);
    static bool validate(const RequirementExecutionSet& value,
                         std::string* error = nullptr);
};

} // namespace rws

#endif
