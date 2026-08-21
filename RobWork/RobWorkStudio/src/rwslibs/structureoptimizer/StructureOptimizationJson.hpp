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
    static const int SchemaVersion = 2;

    //! @brief 将优化问题序列化为 JSON 字符串。
    static std::string problemToJson(const StructureOptimizationProblem& problem);

    //! @brief 从 JSON 字符串反序列化优化问题。
    //! @return true 表示成功, false 表示失败 (error 会携带错误描述)。
    static bool problemFromJson(const std::string& json, StructureOptimizationProblem& problem,
                                std::string* error = nullptr);

    //! @brief 将优化问题 + 结果合并序列化为 JSON 字符串。
    static std::string resultToJson(const StructureOptimizationProblem& problem,
                                    const StructureOptimizationResult& result);

    //! @brief 写出 S60 唯一当前 Envelope；旧 problemToJson 仅保留为兼容入口。
    //! @throw std::invalid_argument 当变量单位无法解释为该种类的 canonical SI 单位时。
    static std::string currentEnvelopeToJson(const StructureOptimizationProblem& problem);

    //! @brief 严格读取当前 Envelope，不接受旧的根类型或缺失 canonical 分区。
    static bool currentEnvelopeFromJson(const std::string& json,
                                        StructureOptimizationProblem& problem,
                                        std::string* error = nullptr);

    //! @brief 对当前 Envelope 的规范紧凑 JSON 计算 SHA-256 指纹。
    static std::string currentEnvelopeFingerprint(const StructureOptimizationProblem& problem);
    static std::string currentEnvelopeFingerprint(const std::string& json);
};

} // namespace rws
#endif // RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONJSON_HPP
