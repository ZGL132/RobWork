#ifndef RWS_ENGINEERINGREQUIREMENTS_STATIONIMPORTSERVICE_HPP
#define RWS_ENGINEERINGREQUIREMENTS_STATIONIMPORTSERVICE_HPP

#include "EngineeringRequirementTypes.hpp"

#include <string>
#include <vector>

namespace rws {

/**
 * @brief 单条记录导入诊断信息
 * 
 * 当外部 CSV 或 JSON 文件中某一行/某条记录存在语法错误、数值非法或 ID 冲突时，
 * 系统会记录下具体的记录行号与错误描述，方便工程师快速回溯定位源文件中的“坏数据”。
 */
struct StationImportDiagnostic {
    int recordNumber = 0; ///< 原始文件中的记录行号/索引（从 1 开始计）
    std::string message;  ///< 该行记录的具体错误描述（如 "Invalid numeric value for x"）
};

/**
 * @brief 外部数据导入汇总结果
 * 
 * 保存一次导入操作的最终统计状态及收集到的所有诊断日志。
 */
struct StationImportResult {
    std::vector<StationImportDiagnostic> diagnostics; ///< 逐行收集到的诊断日志集合
    int importedCount = 0;                             ///< 最终成功导入的需求工位总数
};

/**
 * @brief 外部工艺数据导入服务 (Station Import Service)
 * 
 * 核心功能与安全保障机制（原子性导入 All-or-Nothing）：
 * 1. 支持多格式解析：能够解析外部 CSV 表格或 JSON 配置文件中的批量关键工位（KeyStation）；
 * 2. 严格的逐行校验：全面校验数据类型、单位、枚举合法性以及工位 ID 是否与当前需求集冲突；
 * 3. 内存隔离与强原子性：
 *    解析阶段仅在局部的临时容器（`std::vector<PoseTask>`）中构造对象；
 *    **只有当文件中所有记录的语法、数值和 ID 校验全部 100% 通过时，才会一次性追加到 RequirementSet**；
 *    如果存在任何一行“坏数据”，导入直接宣告失败并返回 diagnostic 日志，**绝对不会部分写入或污染当前已有需求**；
 * 4. 溯源保留：自动为导入成功的工位写入 `ImportProvenance`（包含文件路径及原始行号），
 *    确保未来在算法优化或生成报告时，能准确追踪到数据最初来源。
 */
class StationImportService {
  public:
    /**
     * @brief 从 CSV 字符串中解析并原子追加关键工位
     * 
     * 解析与校验流程：
     * 1. 自动处理 UTF-8 BOM 报头与 Windows/Linux 换行符（`\r\n`）；
     * 2. 检查表头必填列（`id, name, refFrame, tcpFrame, x, y, z, roll, pitch, yaw, level, processType`）；
     * 3. 逐行解析 CSV 文本（支持带双引号转义的字段），校验数值与枚举合法性；
     * 4. 检查新增工位 ID 之间以及与已有需求集中工位 ID 是否存在冲突；
     * 5. 全部无误后，写入来源路径 `sourcePath` 与记录行号 `recordNumber`，追加至 `requirements`。
     * 
     * @param[in,out] requirements 目标需求集对象（仅在全部成功时修改）
     * @param csv 待解析的 CSV 文本内容
     * @param sourcePath 外部 CSV 文件的磁盘路径（用于溯源审计）
     * @param[out] result 导入结果与逐行诊断日志汇总
     * @param[out] error 可选错误描述（返回总体阻断原因）
     * @return true 导入成功（已追加到 requirements）；false 导入失败（未修改 requirements）
     */
    static bool appendCsv(RequirementSet& requirements, const std::string& csv,
                          const std::string& sourcePath, StationImportResult& result,
                          std::string* error = nullptr);

    /**
     * @brief 从 JSON 字符串中解析并原子追加关键工位
     * 
     * 解析与校验流程：
     * 1. 校验 JSON 语法规范（支持 JSON 数组或包含 `"stations"` 数组的对象格式）；
     * 2. 逐个提取对象中的位置 `position: [x,y,z]`、姿态 `rpyDeg: [r,p,y]` 以及工艺属性；
     * 3. 执行相同的防重与数值范围校验；
     * 4. 全部无误后追加至 `requirements`，并保留记录索引 `recordNumber`。
     * 
     * @param[in,out] requirements 目标需求集对象（仅在全部成功时修改）
     * @param json 待解析的 JSON 文本内容
     * @param sourcePath 外部 JSON 文件的磁盘路径（用于溯源审计）
     * @param[out] result 导入结果与逐行诊断日志汇总
     * @param[out] error 可选错误描述
     * @return true 导入成功；false 导入失败
     */
    static bool appendJson(RequirementSet& requirements, const std::string& json,
                           const std::string& sourcePath, StationImportResult& result,
                           std::string* error = nullptr);
};

} // namespace rws

#endif