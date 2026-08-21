#ifndef RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONMIGRATION_HPP
#define RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONMIGRATION_HPP

#include "StructureOptimizationTypes.hpp"

#include <string>
#include <vector>

namespace rws {

//! @brief 迁移来源，持久化审计不能依赖自由文本。
enum class StructureOptimizationMigrationSource {
    Legacy,
    Current
};

//! @brief 单向迁移结果；currentJson 是唯一可继续写入的权威文档。
struct StructureOptimizationMigrationResult {
    StructureOptimizationMigrationSource source = StructureOptimizationMigrationSource::Legacy;
    StructureOptimizationProblem problem;
    std::string currentJson;
    std::vector< std::string > diagnostics;
    bool dirty = false;
};

class StructureOptimizationMigration {
  public:
    //! @brief 识别旧/当前文档并单向迁移到当前 Envelope。
    static bool migrate(const std::string& json,
                        StructureOptimizationMigrationResult& result,
                        std::string* error = nullptr);
};

} // namespace rws

#endif // RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONMIGRATION_HPP
