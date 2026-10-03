/**
 * @file   ModelingXacroBridge.hpp
 * @brief  Xacro 受控展开桥（UI-T41）——命令流 TU 与 XacroExpand 的隔离面。
 *
 * 背景：DhConvert.hpp 与 XacroExpand.hpp 在 modeling 命名空间各定义一个
 * 同名值类型 ExpandOutcome（两套域语义，头文件层面不可共 TU）；命令流 TU
 * 经 ModelingUiModule 链已含 DhConvert——Xacro 展开须独立编译单元承载。
 * 本桥把"字节入→URDF 形态字节出"的最小展开面出线（io 快照随行——来源
 * 身份保持），替换表为空（用户参数代入随导入向导任务）。
 */
#ifndef IRD_MODELING_PLUGIN_MODELINGXACROBRIDGE_HPP
#define IRD_MODELING_PLUGIN_MODELINGXACROBRIDGE_HPP

#include <QString>

#include <cstdint>
#include <string>
#include <vector>

#include <sdurws/ird/core/Digest.hpp>      // core::Digest256（来源摘要）
#include <sdurws/ird/io/ResourceIo.hpp>    // io::ResourceSnapshot（入口快照）

namespace sdurws::ird::modeling {

/// 展开桥产出值（桥自有类型——不透传 XacroExpand 值面，隔离头依赖）。
struct ModelXacroExpandResult {
    bool ok = false;                          ///< true＝expandedBytes 有效
    std::vector<std::uint8_t> expandedBytes;  ///< 展开产物（URDF 形态字节）
    io::ResourceSnapshot entrySnapshot{};     ///< 入口快照（来源身份——映射报告沿用）
    core::Digest256 sourceDigest{};           ///< 原始 .xacro 摘要（XacroProvenance 输入）
    std::string errorDetail;                  ///< 失败原因（ok=false 时）
};

/**
 * @brief 受控展开一次（无参数代入——XacroSubstitutionMap 空）。
 *
 * @param bytes [in] 入口 .xacro 字节（非空——调用方已读文件）
 * @param path  [in] 入口路径文本（快照 finalPath 记录——不作身份）
 * @return 展开结果（ok=false 时 expandedBytes 为空，errorDetail 带原因）
 */
ModelXacroExpandResult expandXacroBytes(const std::vector<std::uint8_t>& bytes,
                                        const QString& path);

}  // namespace sdurws::ird::modeling

#endif  // IRD_MODELING_PLUGIN_MODELINGXACROBRIDGE_HPP
