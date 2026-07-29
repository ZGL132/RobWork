#ifndef RWS_ENGINEERINGREQUIREMENTS_REQUIREMENTSETJSON_HPP
#define RWS_ENGINEERINGREQUIREMENTS_REQUIREMENTSETJSON_HPP

#include "EngineeringRequirementTypes.hpp"

#include <QJsonObject>
#include <string>

namespace rws {

class RequirementSetJson {
public:
    static QJsonObject toObject(const RequirementSet& requirements);
    static bool fromObject(const QJsonObject& object, RequirementSet& requirements,
                           std::string* error = nullptr);
    static std::string toJson(const RequirementSet& requirements);
    static bool fromJson(const std::string& json, RequirementSet& requirements,
                         std::string* error = nullptr);
};

} // namespace rws

#endif
