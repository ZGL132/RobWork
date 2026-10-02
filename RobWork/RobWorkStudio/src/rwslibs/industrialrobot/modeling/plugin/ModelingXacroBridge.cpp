/**
 * @file   ModelingXacroBridge.cpp
 * @brief  Xacro 受控展开桥实现（UI-T41）——XacroExpand.hpp 只在本 TU 可见
 *         （与 DhConvert 的同名 ExpandOutcome 头层面冲突隔离，见桥头注）。
 */

#include "ModelingXacroBridge.hpp"

#include <QFileInfo>
#include <QString>

#include <utility>

#include <sdurws/ird/core/Digest.hpp>      // core::ContentDigester（快照摘要）
#include <sdurws/ird/modeling/XacroExpand.hpp>  // XacroExpandService/ValidatedXacroSource（受控展开——仅本 TU）

namespace sdurws::ird::modeling {

ModelXacroExpandResult expandXacroBytes(const std::vector<std::uint8_t>& bytes,
                                        const QString& path)
{
    ModelXacroExpandResult result;
    // 入口快照（digest＝SHA-256 权威判据——io ResourceSnapshot 同源口径；
    // mtime 置 0＝本链不取时钟，预筛非权威字段）。
    io::ResourceSnapshot snapshot;
    snapshot.finalPath = path.toStdWString();
    snapshot.sizeBytes = bytes.size();
    snapshot.mtimeUtc = 0;
    core::ContentDigester digester;
    digester.update(bytes.data(), bytes.size());
    snapshot.contentDigest = digester.finalize();
    result.entrySnapshot = snapshot;
    result.sourceDigest = snapshot.contentDigest;

    // 受控展开（依赖树空——include 文件按缺失叶语义由 io 契约裁决；替换
    // 表空＝无用户参数代入）。
    const XacroExpandService expander;
    ValidatedXacroSource source;
    source.entryBytes = bytes;
    source.entrySnapshot = snapshot;
    std::vector<core::DiagnosticRecord> diags;
    const auto expanded = expander.expand(source, XacroSubstitutionMap{}, diags);
    if (!expanded.expandedBytes.has_value()) {
        result.errorDetail = expanded.error.has_value()
                                 ? expanded.error->detail
                                 : std::string("未知原因");
        return result;
    }
    result.ok = true;
    result.expandedBytes = std::move(*expanded.expandedBytes);
    return result;
}

}  // namespace sdurws::ird::modeling
