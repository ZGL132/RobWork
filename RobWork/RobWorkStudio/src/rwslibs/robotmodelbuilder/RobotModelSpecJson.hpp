#ifndef RWS_ROBOTMODELBUILDER_ROBOTMODELSPECJSON_HPP
#define RWS_ROBOTMODELBUILDER_ROBOTMODELSPECJSON_HPP

#include "RobotModelSpec.hpp"
#include <QJsonObject>
#include <string>

namespace rws {

class RobotModelSpecJson
{
  public:
    static const int SchemaVersion = 1;
    static QJsonObject toObject (const RobotModelSpec& spec);
    static bool fromObject (const QJsonObject& dataObject, RobotModelSpec& spec,
                            std::string* error = nullptr);
    static std::string toJson (const RobotModelSpec& spec);
    static bool fromJson (const std::string& json, RobotModelSpec& spec,
                          std::string* error = nullptr);
};

}    // namespace rws

#endif    // RWS_ROBOTMODELBUILDER_ROBOTMODELSPECJSON_HPP
