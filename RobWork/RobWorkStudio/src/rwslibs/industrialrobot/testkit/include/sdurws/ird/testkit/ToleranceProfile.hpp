/**
 * @file   ToleranceProfile.hpp
 * @brief  容差档案（ToleranceProfile）——档案解析、条目校验与 fieldPath 解析。
 *
 * 设计依据：
 *   - units/testkit.md §4.3.1（档案字段表）、§4.3.2（三类默认与 allowedMax 规则）、
 *     §4.3.3（比较语义经 core——档案只携带数值）、§5.2（签名）、§8 TK-TOL
 *   - 需求 NFR-COR-01（黄金对照的容差对齐——§10.3 与 core 交接的承接答复）
 *   - 任务契约 tasks/foundation/TK-T04.json（≙WP-02-T04）
 *
 * allowedMax 规则（§4.3.1/§4.3.2）：
 *   - source=appendixD-fixed：allowedMax 必填＝附录 D 对应值——数据集只准更严、
 *     不准放宽（tolerance > allowedMax → 装载失败，档案非法）；
 *   - source=dataset-declared：allowedMax 必填并给出依据；
 *   - 其余两类（analysis-config-default/engineering-policy-default）：allowedMax
 *     可选（默认值归属被测单元配置/策略对象——档案只登记不裁决）。
 *
 * 职责分界：数值基础（C4 公式）＝core closeWithin；本单元＝档案装载与
 * fieldPath→条目解析（resolve），比较组织归 Check（TK-T05）。
 * 线程安全：ToleranceProfile 为不可变快照（load 后只读），并发只读安全。
 */

#ifndef SDURWS_IRD_TESTKIT_TOLERANCEPROFILE_HPP
#define SDURWS_IRD_TESTKIT_TOLERANCEPROFILE_HPP

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/core/Compare.hpp>   // core::Tolerance（C4 两分量）
#include <sdurws/ird/core/Units.hpp>     // core::QuantityKind/UnitToken
#include <sdurws/ird/testkit/TestPaths.hpp>

namespace sdurws::ird::testkit {

/// 容差条目的来源类别（§4.3.2——附录 D"类别说明"的档案化）。
enum class ToleranceSource {
    AppendixDFixed,            ///< appendixD-fixed           附录 D 固定值
    AnalysisConfigDefault,     ///< analysis-config-default   分析配置默认（登记不裁决）
    EngineeringPolicyDefault,  ///< engineering-policy-default 工程策略默认（登记不裁决）
    DatasetDeclared,           ///< dataset-declared          数据集自声明（须附依据）
};

/// token 映射（§4.3.1 source 四 token）。
const char* toToken(ToleranceSource s) noexcept;
std::optional<ToleranceSource> toleranceSourceFromToken(std::string_view token) noexcept;

/// 容差条目（§4.3.1 的 C++ 投影）。
struct ToleranceEntry {
    std::string fieldPath;        ///< 结果字段路径模板（'*'＝逐元素）
    core::QuantityKind kind;      ///< 量纲（core 词表 token）
    core::UnitToken unit;         ///< 声明单位（已注册；比较前经 core 唯一入口转 SI）
    core::Tolerance tolerance;    ///< 本数据集采用的容差
    ToleranceSource source = ToleranceSource::DatasetDeclared;  ///< 数值来源类别
    std::optional<core::Tolerance> allowedMax;  ///< 声明上限（条件必填——§4.3.1）

    bool operator==(const ToleranceEntry& o) const noexcept
    {
        return fieldPath == o.fieldPath && kind == o.kind && unit == o.unit
            && tolerance == o.tolerance && source == o.source
            && allowedMax == o.allowedMax;
    }
    bool operator!=(const ToleranceEntry& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 容差档案（schemaVersion ird-tolerance-profile/1）＋fieldPath 解析（§5.2）。
 */
struct ToleranceProfile {
    std::string profileId;      ///< [a-z0-9-]{3,64}（与目录名一致）
    std::string version;        ///< M.m.p（与文件名 v<version>.json 一致）
    std::string basis;          ///< 依据声明（附录 D 项号＋类别）
    std::vector<ToleranceEntry> entries;   ///< ≥1

    /**
     * @brief 装载并全量校验档案（§5.2 load）。
     *
     * 前置：path 形如 <root>/tolerance/<profileId>/v<version>.json。
     * 校验：schema 前缀/主版本、profileId 句法与目录一致、文件名与 version 一致、
     * basis 非空、entries ≥1、逐条目（kind token 合法/unit 已注册/tolerance 分量
     * ≥0 有限/source 白名单/allowedMax 条件规则/tolerance ≤ allowedMax）。
     *
     * @throws TestKitError(DatasetInvalid) 任一校验失败（消息含字段路径）
     */
    static ToleranceProfile load(const std::filesystem::path& profileVersionJson);

    /**
     * @brief 按已展开的具体字段路径解析条目（§5.2 resolve）。
     *
     * 模板段匹配规则：段数相同；'*' 段匹配"前缀[任意索引]"形态（如 fk[*] ↔ fk[3]）；
     * 其余段须逐字符相等。未命中→抛 TestKitError(ToleranceUndefined)——不默认、
     * 不通过（附录 D C4：报错不默认）。
     *
     * @param concreteFieldPath [in] 已展开索引的具体路径（如 fk[3].tcp.position.x）
     * @return 命中的条目（生命周期随本对象）
     */
    const ToleranceEntry& resolve(std::string_view concreteFieldPath) const;

    bool operator==(const ToleranceProfile& o) const noexcept
    {
        return profileId == o.profileId && version == o.version && basis == o.basis
            && entries == o.entries;
    }
    bool operator!=(const ToleranceProfile& o) const noexcept { return !(*this == o); }
};

}  // namespace sdurws::ird::testkit

#endif  // SDURWS_IRD_TESTKIT_TOLERANCEPROFILE_HPP
