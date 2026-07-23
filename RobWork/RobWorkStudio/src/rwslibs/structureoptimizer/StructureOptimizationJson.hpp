#ifndef RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONJSON_HPP
#define RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONJSON_HPP

#include "StructureOptimizationTypes.hpp"
#include <string>

namespace rws {

//! @brief JSON 序列化 / 反序列化辅助类。
//!
//! 使用 QJsonObject / QJsonDocument 实现。
class StructureOptimizationJson {
  public:
    static const int SchemaVersion = 1;

    //! @brief 将优化问题序列化为 JSON 字符串。
    static std::string problemToJson(const StructureOptimizationProblem& problem);

    //! @brief 从 JSON 字符串反序列化优化问题。
    //! @return true 表示成功, false 表示失败 (error 会携带错误描述)。
    static bool problemFromJson(const std::string& json, StructureOptimizationProblem& problem,
                                std::string* error = nullptr);

    //! @brief 将优化问题 + 结果合并序列化为 JSON 字符串。
    static std::string resultToJson(const StructureOptimizationProblem& problem,
                                    const StructureOptimizationResult& result);
};

} // namespace rws
#endif // RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONJSON_HPP
