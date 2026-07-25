// =============================================================================
//  核心功能是在 C++ 内存结构体（RobotModelSpec）与 
//  JSON 文本/对象（QJsonObject / JSON string）之间进行双向转换与校验，
//  用于机器人模型配置文件的持久化存储（保存）与读取加载。
// =============================================================================
#ifndef RWS_ROBOTMODELBUILDER_ROBOTMODELSPECJSON_HPP
#define RWS_ROBOTMODELBUILDER_ROBOTMODELSPECJSON_HPP

#include "RobotModelSpec.hpp"
#include <QJsonObject>
#include <string>

namespace rws {

class RobotModelSpecJson
{
  public:
    static const int SchemaVersion = 2;
    static QJsonObject toObject (const RobotModelSpec& spec);
    static bool fromObject (const QJsonObject& dataObject, RobotModelSpec& spec,
                            std::string* error = nullptr);
    static std::string toJson (const RobotModelSpec& spec);
    static bool fromJson (const std::string& json, RobotModelSpec& spec,
                          std::string* error = nullptr);
};

}    // namespace rws

#endif    // RWS_ROBOTMODELBUILDER_ROBOTMODELSPECJSON_HPP
