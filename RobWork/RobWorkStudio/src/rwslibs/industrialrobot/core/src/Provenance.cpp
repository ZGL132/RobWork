/**
 * @file   Provenance.cpp
 * @brief  值来源的非模板核心——token 映射、ValueProvenance::make 校验（P-1/语法）。
 *
 * 设计依据：
 *   - units/core.md §4.3（token 冻结表、P-1 不变量、methodTag 语法）、§5.3
 *   - 任务契约 tasks/foundation/CORE-T03.json（UT-MISS 载体）
 */

#include <sdurws/ird/core/Provenance.hpp>

#include <algorithm>

namespace sdurws::ird::core {

// ---------------------------------------------------------------------
// token 映射（§4.3 冻结表；switch 而非数组——枚举增删不静默错位）
// ---------------------------------------------------------------------
const char* toToken(ProvenanceKind kind) noexcept
{
    switch (kind) {
    case ProvenanceKind::UserProvided:      return "user-provided";
    case ProvenanceKind::GeometricEstimate: return "geometric-estimate";
    case ProvenanceKind::CatalogBackfill:   return "catalog-backfill";
    case ProvenanceKind::ImportMapped:      return "import-mapped";
    case ProvenanceKind::DerivedReadOnly:   return "derived-readonly";
    }
    return "unknown";   // 不可达（五枚举全覆盖）；保返回避免 UB
}

std::optional<ProvenanceKind> provenanceKindFromToken(std::string_view token) noexcept
{
    if (token == "user-provided")      { return ProvenanceKind::UserProvided; }
    if (token == "geometric-estimate") { return ProvenanceKind::GeometricEstimate; }
    if (token == "catalog-backfill")   { return ProvenanceKind::CatalogBackfill; }
    if (token == "import-mapped")      { return ProvenanceKind::ImportMapped; }
    if (token == "derived-readonly")   { return ProvenanceKind::DerivedReadOnly; }
    return std::nullopt;   // 未知 token（try 轨——装载层错误收集用）
}

// ---------------------------------------------------------------------
// ValueProvenance::make：P-1 不变量＋methodTag 语法校验
// ---------------------------------------------------------------------
ValueProvenance ValueProvenance::make(ProvenanceKind kind,
                                      std::optional<ObjectId> sourceObject,
                                      std::optional<ContentVersion> sourceVersion,
                                      std::optional<std::string> methodTag)
{
    // 不变量 P-1（§4.3）：sourceVersion 有值 ⇒ sourceObject 有值。
    // 版本只能挂在对象上——无对象的版本没有归属，属调用方契约违约。
    if (sourceVersion.has_value() && !sourceObject.has_value()) {
        throw CoreError("core/provenance/p1: sourceVersion 需要 sourceObject"
                        "（P-1 不变量：版本挂于对象）");
    }
    // 对象/版本若提供则必须合法（保留值"空"不作为有效来源）。
    if (sourceObject.has_value() && !sourceObject->isValid()) {
        throw CoreError("core/provenance/p1: sourceObject 为保留值（全零）——"
                        "无效来源对象");
    }

    // methodTag 语法校验（§4.3：[a-z0-9./_-]、非空、≤64；词表归产生单元——
    // core 只管字符集/长度，不认识具体方法名）。
    if (methodTag.has_value()) {
        const std::string& tag = *methodTag;
        if (tag.empty() || tag.size() > 64) {
            throw CoreError("core/provenance/method-tag: 长度须为 1..64，实际 "
                            + std::to_string(tag.size()));
        }
        const bool ok = std::all_of(tag.begin(), tag.end(), [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                || (c >= '0' && c <= '9')
                || c == '.' || c == '/' || c == '_' || c == '-';
        });
        if (!ok) {
            throw CoreError("core/provenance/method-tag: 含非法字符（允许"
                            " [a-z0-9./_-]）: " + tag);
        }
    }

    ValueProvenance v;
    v.kind = kind;
    v.sourceObject = std::move(sourceObject);
    v.sourceVersion = std::move(sourceVersion);
    v.methodTag = std::move(methodTag);
    return v;
}

bool ValueProvenance::operator==(const ValueProvenance& o) const noexcept
{
    // 四字段全等（optional 的比较含"有无"维度）。
    return kind == o.kind && sourceObject == o.sourceObject
        && sourceVersion == o.sourceVersion && methodTag == o.methodTag;
}

}  // namespace sdurws::ird::core
