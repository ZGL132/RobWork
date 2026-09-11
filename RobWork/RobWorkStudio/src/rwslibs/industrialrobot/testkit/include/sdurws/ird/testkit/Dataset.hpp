/**
 * @file   Dataset.hpp
 * @brief  黄金数据集清单模型与装载——DatasetManifest/GoldenDataset＋完整性校验。
 *
 * 设计依据：
 *   - units/testkit.md §4.2.2（manifest 字段表逐行）、§4.5（size＋SHA-256 完整性）、
 *     §5.1（GoldenDataset::load 签名）、§4.2.1（五类与升级规则——kind 合法集）、
 *     附录 A.2（完整示例）
 *   - 需求 NFR-COR-01（解析算例为独立正确性依据首选）；任务契约 TK-T03
 *     （≙WP-02-T03：TK-MAN/TK-INT/lint 字典）
 *
 * 责任边界：装载＝解析（TK-T02 JsonLite）→schema 校验（本文件）→完整性
 * （core ContentDigester，TK-T01 依赖链）→字段交叉校验（本文件）；任何失败抛
 * TestKitError(DatasetInvalid) 且消息含字段路径——不猜测、不补默认关键项。
 *
 * 落位偏差（TK-T03 范围，登记 core/testkit 单元卡变更记录）：§5.1 的
 * toleranceProfile() 成员（ToleranceProfile 随 load 一并装载）归 TK-T04——本任务
 * 提供 toleranceProfileRef()（manifest 内 {id,version} 引用），TK-T04 落地后补全。
 *
 * 确定性：同目录内容装载结果一致（无时间戳——NFR-COR-02）。
 * 线程安全：GoldenDataset 为不可变快照（只读访问），并发只读安全。
 */

#ifndef SDURWS_IRD_TESTKIT_DATASET_HPP
#define SDURWS_IRD_TESTKIT_DATASET_HPP

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <sdurws/ird/core/Digest.hpp>       // ContentDigester（完整性校验）
#include <sdurws/ird/core/Units.hpp>        // UnitToken（units 表校验）
#include <sdurws/ird/testkit/TestPaths.hpp> // TestKitError/TestPaths

namespace sdurws::ird::testkit {

/// 数据集引用（§5.1：datasetId＋语义化版本定位装载目标）。
struct DatasetRef {
    std::string datasetId;   ///< [a-z0-9-]{3,64}，与目录名一致
    std::string version;     ///< 语义化 M.m.p，与版本目录名一致
};

/// 数据集五类（§4.2.1 token 冻结；换类＝新 datasetId）。
enum class DatasetKind {
    AnalyticCase,          ///< analytic-case        解析算例（可独立正确性依据）
    ReferenceImpl,         ///< reference-impl       独立参考实现
    ContractFixture,       ///< contract-fixture     契约夹具
    Regression,            ///< regression           回归样本（不可作正确性依据）
    PerformanceBaseline,   ///< performance-baseline 性能基准
};

/// token 映射（§4.2.1 五 token）。
const char* toToken(DatasetKind k) noexcept;
std::optional<DatasetKind> datasetKindFromToken(std::string_view token) noexcept;

/// referenceSource 子结构（§4.2.2：analytic-case/reference-impl 必填且独立=true）。
struct ReferenceSource {
    std::string method;                        ///< 六枚举 token 之一（见 .cpp 白名单）
    std::string scope;                         ///< 适用范围声明
    bool independentOfProductionImpl = false;  ///< 独立性声明
    std::string description;                   ///< 推导/来源说明

    bool operator==(const ReferenceSource& o) const noexcept
    {
        return method == o.method && scope == o.scope
            && independentOfProductionImpl == o.independentOfProductionImpl
            && description == o.description;
    }
};

/// producer 子结构（生成环境——复现要素）。
struct Producer {
    std::string softwareVersion;   ///< 生成软件/参考实现版本
    std::string algorithmId;       ///< 算法稳定标识
    std::string solverConfigJson;  ///< 求解配置快照（JSON 原文——schema 由域定）
    bool seedPresent = false;      ///< seed 是否提供（随机性参与时必填）
    std::uint64_t seed = 0;        ///< 随机种子
    int threadCount = 1;           ///< ≥1
    std::string generatedAtUtc;    ///< ISO-8601

    bool operator==(const Producer& o) const noexcept
    {
        return softwareVersion == o.softwareVersion && algorithmId == o.algorithmId
            && solverConfigJson == o.solverConfigJson && seedPresent == o.seedPresent
            && seed == o.seed && threadCount == o.threadCount
            && generatedAtUtc == o.generatedAtUtc;
    }
    bool operator!=(const Producer& o) const noexcept { return !(*this == o); }
};

/// edgeCases 子结构（附录 D C4：解析算例/参考实现必填且三布尔全 true）。
struct EdgeCases {
    bool zeroValue = false;
    bool nearZero = false;
    bool signCancellation = false;
    std::vector<std::string> sampleRefs;   ///< 指向 expected 内条目
};

/// 完整性条目（§4.5：覆盖 inputs+expected+generate 全部文件）。
struct IntegrityEntry {
    std::string path;        ///< 版本目录内相对路径
    std::string sha256Hex;   ///< 64 位小写十六进制
    std::uint64_t sizeBytes = 0;

    bool operator==(const IntegrityEntry& o) const noexcept
    {
        return path == o.path && sha256Hex == o.sha256Hex && sizeBytes == o.sizeBytes;
    }
};

/// history 条目（首版也登记；supersededBy 保留旧基线链 §4.7）。
struct HistoryEntry {
    std::string version;
    std::string date;
    std::string change;
    std::string reReviewedBy;
    std::string supersededBy;   ///< 可空串＝未被取代

    bool operator==(const HistoryEntry& o) const noexcept
    {
        return version == o.version && date == o.date && change == o.change
            && reReviewedBy == o.reReviewedBy && supersededBy == o.supersededBy;
    }
    bool operator!=(const HistoryEntry& o) const noexcept { return !(*this == o); }
};

/**
 * @brief manifest 模型（§4.2.2 字段表——全部经装载校验后填充）。
 */
struct DatasetManifest {
    std::string schemaVersion;              ///< ird-golden-manifest/<n>（主版本须=1）
    std::string datasetId;                  ///< [a-z0-9-]{3,64}
    std::string version;                    ///< M.m.p
    DatasetKind kind = DatasetKind::Regression;
    std::string scenarioCategory;           ///< [a-z0-9-]{1,64}
    std::vector<std::string> coveredRequirements;   ///< ≥1 需求 ID
    std::vector<std::string> coveredAt;     ///< AT-xx（可空）
    std::string toleranceProfileId;         ///< 档案 id（存在于 testdata/tolerance/）
    std::string toleranceProfileVersion;    ///< 档案版本
    std::vector<std::string> inputs;        ///< ≥1 相对路径
    std::vector<std::string> expected;      ///< ≥1 相对路径
    std::string parametersJson;             ///< parameters 对象（JSON 原文——域自有 schema）
    std::vector<std::pair<std::string, std::string>> units;   ///< field→UnitToken（已注册）
    std::vector<std::pair<std::string, std::string>> frames;  ///< 约定声明（至少 base）
    bool referenceSourcePresent = false;    ///< 条件字段存在性
    ReferenceSource referenceSource;
    bool producerPresent = false;
    Producer producer;
    bool edgeCasesPresent = false;          ///< 条件字段存在性
    EdgeCases edgeCases;
    std::vector<IntegrityEntry> integrity;  ///< ≥1
    bool generatorPresent = false;
    std::string generatorScript;            ///< generate/ 内相对路径
    std::string generatorInvocation;        ///< 调用命令
    bool generatorCommitted = false;        ///< 脚本入库声明
    std::vector<HistoryEntry> history;      ///< ≥1

    bool operator==(const DatasetManifest& o) const;
    bool operator!=(const DatasetManifest& o) const { return !(*this == o); }
};

/**
 * @brief 黄金数据集装载器（§5.1）：解析→schema 校验→完整性→交叉校验。
 *
 * 前置：manifest 位于 <goldenDataRoot()>/golden/<datasetId>/manifest.json。
 * 行为顺序（§5.1）：解析→schema 校验→integrity 全量校验（§4.5）→字段交叉校验。
 * 错误：任何校验失败抛 TestKitError(DatasetInvalid)（消息含字段路径与原因）；
 * 数据根不可达抛 TestKitError(EnvUnavailable)（TestPaths 语义）。
 */
class GoldenDataset {
public:
    /// 装载并全量校验（错误口径见类注释）。
    static GoldenDataset load(const DatasetRef& ref);

    const DatasetManifest& manifest() const noexcept { return manifest_; }

    /// 解析 inputs 侧相对路径（前置：relPath ∈ manifest().inputs——越界抛 DatasetInvalid）。
    std::filesystem::path resolveInput(std::string_view relPath) const;
    /// 解析 expected 侧相对路径（同上）。
    std::filesystem::path resolveExpected(std::string_view relPath) const;

    /// manifest 内的容差档案引用（{id,version}）——完整 ToleranceProfile 装载随 TK-T04。
    std::pair<std::string, std::string> toleranceProfileRef() const noexcept
    {
        return {manifest_.toleranceProfileId, manifest_.toleranceProfileVersion};
    }

private:
    std::filesystem::path versionDir_;   ///< 版本目录（resolve 基准）
    DatasetManifest manifest_;
};

/// 语义化版本解析：M.m.p 三段非负整数；非法抛 DatasetInvalid（带字段路径）。
void parseSemanticVersion(std::string_view v, std::string_view fieldPath,
                          int* major, int* minor, int* patch);

}  // namespace sdurws::ird::testkit

#endif  // SDURWS_IRD_TESTKIT_DATASET_HPP
