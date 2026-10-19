/**
 * @file   ModelingXacroBridge.hpp
 * @brief  Xacro 受控展开桥（UI-T41）——命令流 TU 与 XacroExpand 的隔离面。
 *
 * 背景：DhConvert.hpp 与 XacroExpand.hpp 在 modeling 命名空间各定义一个
 * 同名值类型 ExpandOutcome（两套域语义，头文件层面不可共 TU）；命令流 TU
 * 经 ModelingUiModule 链已含 DhConvert——Xacro 展开须独立编译单元承载。
 * 本桥把"字节入→URDF 形态字节出"的最小展开面出线（io 快照随行——来源
 * 身份保持）；WP-13-T24补齐离线输入冻结与显式用户参数。
 */
#ifndef IRD_MODELING_PLUGIN_MODELINGXACROBRIDGE_HPP
#define IRD_MODELING_PLUGIN_MODELINGXACROBRIDGE_HPP

#include <QString>

#include <cstdint>
#include <string>
#include <map>
#include <utility>
#include <vector>

#include <sdurws/ird/core/Digest.hpp>      // core::Digest256（来源摘要）
#include <sdurws/ird/io/ResourceIo.hpp>    // io::ResourceSnapshot（入口快照）

namespace sdurws::ird::modeling {

/**
 * @brief WP-13-T24：导入对话期间冻结的离线输入，仅供本插件使用。
 *
 * UI线程拥有全部字节/依赖/参数默认值；先由io安全读取，再让用户填写替换值，
 * 确认后只展开内存快照，避免模态期间文件变化导致预览和实际消费不一致。
 * 失败时ok=false，errorDetail定位原因；不接管任何外部对象，不写项目。
 */
struct ModelXacroInput {
    bool ok = false; ///< 输入完整、安全读取成功才为true。
    std::vector<std::uint8_t> entryBytes; ///< 原始UTF-8字节，SHA-256对应entrySnapshot。
    io::ResourceSnapshot entrySnapshot{}; ///< 实际消费源身份，mtime不参与身份。
    io::ResourceDependencyTree dependencyTree; ///< 仅include边；mesh由展开后URDF映射处理。
    std::map<std::string, std::vector<std::uint8_t>> includeBytes; ///< 导入根相对折叠键→冻结字节。
    std::vector<std::pair<std::string, std::string>> parameterDefaults; ///< 文档序参数名/默认原文。
    std::string errorDetail; ///< 既有io/建模诊断码及相对文件定位。
};

/** @brief 安全冻结入口和include；[in] bytes/path须对应同一文件，失败不产草稿。 */
ModelXacroInput loadXacroInput(const std::vector<std::uint8_t>& bytes, const QString& path);
/** @brief 从用户选定路径直接安全冻结；[in] path非空，避免读取前先用无预算流装入内存。 */
ModelXacroInput loadXacroInput(const QString& path);

/// 展开桥产出值（桥自有类型——不透传 XacroExpand 值面，隔离头依赖）。
struct ModelXacroExpandResult {
    bool ok = false;                          ///< true＝expandedBytes 有效
    std::vector<std::uint8_t> expandedBytes;  ///< 展开产物（URDF 形态字节）
    io::ResourceSnapshot entrySnapshot{};     ///< 入口快照（来源身份——映射报告沿用）
    core::Digest256 sourceDigest{};           ///< 原始 .xacro 摘要（XacroProvenance 输入）
    std::string errorDetail;                  ///< 失败原因（ok=false 时）
    std::vector<std::pair<std::string, std::string>> substitutions; ///< 实际用户替换，输入序保留用于来源。
};

/**
 * @brief 对冻结输入进行受控展开，零磁盘访问，失败不产展开字节。
 * @param input [in] loadXacroInput成功产物；仅借用，调用后不保留引用。
 * @param substitutions [in] 参数名/字面文本，输入序确定性，不执行表达式。
 * @return 成功字节及来源；输入失败直接保留定位原因。仅调用线程使用局部值。
 */
ModelXacroExpandResult expandXacroInput(const ModelXacroInput& input,
    const std::vector<std::pair<std::string, std::string>>& substitutions);

/**
 * @brief 兼容入口：安全冻结并以源默认参数受控展开一次。
 *
 * @param bytes [in] 入口 .xacro 字节（非空——调用方已读文件）
 * @param path  [in] 入口路径文本（快照 finalPath 记录——不作身份）
 * @return 展开结果（ok=false 时 expandedBytes 为空，errorDetail 带原因）
 */
ModelXacroExpandResult expandXacroBytes(const std::vector<std::uint8_t>& bytes,
                                        const QString& path);

}  // namespace sdurws::ird::modeling

#endif  // IRD_MODELING_PLUGIN_MODELINGXACROBRIDGE_HPP
