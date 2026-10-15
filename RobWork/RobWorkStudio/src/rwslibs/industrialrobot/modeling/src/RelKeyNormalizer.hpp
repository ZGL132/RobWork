/**
 * @file   RelKeyNormalizer.hpp
 * @brief  依赖树折叠键的词法规范化（单元私有共享实现——NFR-MNT-04 单点）。
 *
 * 设计依据：
 *   - io 单元 ResourceNode::relPath 契约（正斜杠＋ASCII 小写折叠的根相对
 *     键——io/src/ResourceIo.cpp relativeKeyOf 的**词法**等价形态）；
 *   - R3 审计 A12（2026-10-15）：Import 的 mesh 引用键原先用裸文件名折
 *     叠查树——不按 URDF 所在目录解析、不消解 ./、../ 词法段，合法嵌套
 *     布局被误判"不在依赖树"硬失败；修复＝与 Xacro include 键同一解析。
 *
 * 背景说明：引用键的解析规则＝"按引用所在文件的目录拼接 → 词法消解 →
 * 折叠"。Xacro 的 include 与 URDF 的 mesh 引用同走此规则（io 侧依赖边
 * 抽取的连接语义一致），故本函数是该规则的唯一书写点；两侧调用方各自
 * 传入"引用所在文件的相对键"与"引用原文"。
 *
 * 纯函数；确定性（词法操作——不触文件系统，SA-14）。
 */

#ifndef SDURWS_IRD_MODELING_SRC_REL_KEY_NORMALIZER_HPP
#define SDURWS_IRD_MODELING_SRC_REL_KEY_NORMALIZER_HPP

#include <cctype>
#include <string>
#include <vector>

namespace sdurws::ird::modeling::reldetail {

/**
 * @brief 引用键规范化：按引用所在文件目录解析引用原文，产出依赖树折叠键。
 *
 * @param filenameRaw  [in] 引用原文（URDF mesh filename / xacro include
 *                     filename 属性——正/反斜杠、./、../ 词法段均可出现）
 * @param includingKey [in] 引用所在文件的依赖树相对键（rootRel/树节点键
 *                     ——正斜杠形态；无斜杠＝该文件在根首层，基目录为空）
 * @return 目标的折叠键（正斜杠＋ASCII 小写；词法段已消解——与 io 树节点
 *         relPath 同形态，可直接用于树查找）
 *
 * @note ".." 弹出空基目录的形态（引用逃逸管辖根）产出以 "../" 开头的键
 *       ——调用方以"树中无此节点"的既有面如实拒绝（io 侧在读取期即以
 *       SecPathEscape 拒绝逃逸引用，此形态仅在伪造树中出现）。
 */
inline std::string normalizeIncludeKey(const std::string& filenameRaw,
                                       const std::string& includingKey)
{
    // 基目录：所在文件相对键的父路径（无斜杠＝入口在根——基为空）。
    std::string base;
    const auto slash = includingKey.rfind('/');
    if (slash != std::string::npos) { base = includingKey.substr(0, slash + 1); }

    // 拼接后词法消解（"."跳过、".."弹层；反斜杠统一为正斜杠；折叠小写
    // ——与 io 树节点折叠键同一形态，查重键一致）。
    std::vector<std::string> segments;
    std::string joined = base + filenameRaw;
    for (char& ch : joined) {
        if (ch == '\\') { ch = '/'; }
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    std::size_t i = 0;
    while (i < joined.size()) {
        if (joined[i] == '/') { ++i; continue; }             // 空段（根/双斜杠）——跳过
        const std::size_t start = i;
        while (i < joined.size() && joined[i] != '/') { ++i; }
        const std::string seg = joined.substr(start, i - start);
        if (seg == ".") { continue; }                        // 当前目录——无效果
        if (seg == "..") {
            if (!segments.empty()) { segments.pop_back(); }  // 上一级——弹一层
            continue;
        }
        segments.push_back(seg);
    }
    std::string out;
    for (const std::string& seg : segments) {
        if (!out.empty()) { out += '/'; }
        out += seg;
    }
    return out;
}

}  // namespace sdurws::ird::modeling::reldetail

#endif  // SDURWS_IRD_MODELING_SRC_REL_KEY_NORMALIZER_HPP
