/**
 * @file   SafePath.cpp
 * @brief  SafePath 实现——Windows 路径规范化核、七角色校验规则、reparse
 *         point 逐段复核、等价类键与包条目批量预检。
 *
 * 设计依据：
 *   - units/io.md §4.2.1（规范化算法 6 步）、§4.2.2（UNC/盘符/../处置
 *     表）、§4.2.3（symlink/junction/reparse point 检测与规则）、§4.2.4
 *     （等价类键）、§4.3.1（规则总表 SP-1~SP-10）、§4.3.3（合法/非法示例
 *     表——本实现的行为验收基准）、§9.1（接口契约）
 *   - 需求 NFR-SEC-01（P-4/P-5 不得逃逸资源区；P-1 一次性读取例外）
 *   - 任务契约 tasks/foundation/IO-T02.json acceptance 1（V09/V10/V11）
 *
 * 实现纪律（对 review 者的导览）：
 *   1. 校验次序固定（契约→长度→分类→词法→段级/角色规则→实体→等价键），
 *      保证"同输入同错误"（§9.1 确定性）；每段注释标注对应卡面条目。
 *   2. 单条与批量（normalizePackEntries）共用同一校验核 normalizeCore
 *      ——两入口永不分歧（§7.4 步骤③与 §9.1 批量方法的一致性）。
 *   3. Win32 依赖仅 GetFileAttributesW（reparse point 属性复核——§4.2.3；
 *      MSVC 的 std::filesystem 不把 junction 呈现为 symlink，必须补检），
 *      其余全部 std::filesystem/标准库。
 *   4. 全部路径参数在错误 params 中以脱敏 display（UTF-8、去 \\?\）携带
 *      （NFR-SEC-07/§10.3——原始路径不直接进用户文案）。
 */

#include <sdurws/ird/io/SafePath.hpp>

#include <sdurws/ird/io/IoDiagnostics.hpp>   // errorCodeToken——批量违规明细的稳定码面

#include <algorithm>
#include <array>
#include <utility>
#include <vector>

// Win32：仅 reparse point 属性复核需要（§4.2.3）。LEAN_AND_MEAN/NOMINMAX
// 压缩宏污染面——本文件不使用 min/max 宏与大部分 Win32 设施；#ifndef 守
// 卫（测试/工具链可能已定义，重定义即 C4005）。
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace sdurws::ird::io {
namespace {

// =====================================================================
// 常量（全部注明来源——AGENTS.md §2.4 魔法数字纪律）
// =====================================================================

/// OS 硬上限：Win32 通用路径最大长度＝32,767 UTF-16 码元（§4.2.1 步骤 1
/// "空/超长（>32,767 UTF-16 码元，拒绝 IO-SEC-PATH-TOO-LONG）"）。
constexpr std::size_t kOsMaxPathUtf16 = 32767;

/// 设备/长路径前缀（§4.2.1 步骤 1 输入分类：\\?\ 剥离后按盘符/UNC 处理；
/// \\.\ 设备命名空间对文件通道一律非法）。
constexpr wchar_t kLongPathPrefix[] = L"\\\\?\\";
constexpr wchar_t kUncPrefix[]      = L"\\\\";

/// 资源区首段（§4.3.1 SP-3：P-5 解析结果必须落在资源区根 objects/ 或
/// catalog/ 内；§4.3.3 示例表以项目根为基、首段命中其一为合法）。
constexpr wchar_t kObjectsDir[] = L"objects";
constexpr wchar_t kCatalogDir[] = L"catalog";

// =====================================================================
// UTF-16 ↔ UTF-8 确定性转换（display/equivKey 承载——不经 locale，
// NFR-COR-02；std::wstring_convert 已弃用且行为依赖 facet，手写字节级
// 编解码是跨进程一致的最小实现）
// =====================================================================

/**
 * @brief UTF-16（wchar_t＝UTF-16，Windows 平台）转 UTF-8。
 * @param w [in] UTF-16 串
 * @return UTF-8 串；非法代理对以 U+FFFD 替换（display 仅供呈现/比较，
 *         不因恶意构造的代理对产生未定义行为）
 */
std::string utf16ToUtf8(const std::wstring& w)
{
    std::string out;
    out.reserve(w.size());
    for (std::size_t i = 0; i < w.size(); ++i) {
        std::uint32_t cp = static_cast<unsigned short>(w[i]);
        // 高代理：与后一个低代理合成一个码点（UTF-16 语法）。
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < w.size()) {
            const std::uint32_t lo = static_cast<unsigned short>(w[i + 1]);
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                ++i;
            } else {
                cp = 0xFFFD;    // 孤立高代理——替换
            }
        } else if (cp >= 0xD800 && cp <= 0xDFFF) {
            cp = 0xFFFD;        // 孤立低代理——替换
        }
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

/**
 * @brief UTF-8 转 UTF-16（包条目访问器输出为 IoString＝UTF-8，转回宽字
 *        符进校验核；错误字节以 U+FFFD 替换——替换后若命中文法性拒绝
 *        规则照常拒绝，不放大也不缩小错误面）。
 */
std::wstring utf8ToUtf16(const std::string& s)
{
    std::wstring out;
    out.reserve(s.size());
    std::size_t i = 0;
    while (i < s.size()) {
        const unsigned char b = static_cast<unsigned char>(s[i]);
        std::uint32_t cp = 0xFFFD;
        std::size_t extra = 0;
        if (b < 0x80) {
            cp = b;
        } else if ((b & 0xE0) == 0xC0) {
            cp = b & 0x1Fu;
            extra = 1;
        } else if ((b & 0xF0) == 0xE0) {
            cp = b & 0x0Fu;
            extra = 2;
        } else if ((b & 0xF8) == 0xF0) {
            cp = b & 0x07u;
            extra = 3;
        }
        if (extra > 0) {
            bool ok = i + extra < s.size();   // 截断序列＝非法
            if (ok) {
                for (std::size_t k = 1; k <= extra; ++k) {
                    const unsigned char cb = static_cast<unsigned char>(s[i + k]);
                    if ((cb & 0xC0) != 0x80) {
                        ok = false;           // 后续字节不是 10xxxxxx——非法序列
                        break;
                    }
                    cp = (cp << 6) | (cb & 0x3Fu);
                }
            }
            if (!ok) {
                cp = 0xFFFD;
            }
            i += extra;
        }
        if (cp >= 0x10000) {                  // 增补平面→代理对
            cp -= 0x10000;
            out.push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
        } else {
            out.push_back(static_cast<wchar_t>(cp));
        }
        ++i;
    }
    return out;
}

/**
 * @brief Windows 大小写折叠（§4.2.1 步骤 4："对 A-Z 折叠为 a-z；不做
 *        Unicode 简单折叠之外的本地化折叠"）。
 *
 * 只折叠 ASCII A-Z：本项目路径键词表（objects/catalog、条目名、对象 id）
 * 为 ASCII 受控词表；对任意 Unicode 做本地化折叠会引入 locale 依赖、破
 * 坏确定性（NFR-COR-02）。极端非 ASCII 混排的等价键可能未归一——安全
 * 侧影响仅为"互斥表视为不同键"，不产生逃逸（逃逸判定的段比较词表受控）。
 */
std::wstring foldCase(const std::wstring& w)
{
    std::wstring out = w;
    for (wchar_t& ch : out) {
        if (ch >= L'A' && ch <= L'Z') {
            ch = static_cast<wchar_t>(ch - L'A' + L'a');
        }
    }
    return out;
}

/// 段名是否为 Windows 保留名（§4.2.1 步骤 6：CON/PRN/AUX/NUL/COM1-9/
/// LPT1-9——"不含扩展名或等于含扩展名形式"，即 CON.txt 同样保留；比较
/// 在大小写折叠后进行）。
bool isReservedDeviceName(const std::wstring& segment)
{
    // 取扩展名前的基名：第一个 '.' 之前的部分（无点则全段）。
    const std::size_t dot = segment.find(L'.');
    const std::wstring base = foldCase(dot == std::wstring::npos ? segment : segment.substr(0, dot));

    static constexpr std::array<const wchar_t*, 4> kNames = {L"con", L"prn", L"aux", L"nul"};
    for (const wchar_t* n : kNames) {
        if (base == n) {
            return true;
        }
    }
    // COM1-9 / LPT1-9：基名长 4、前三字符为族名、末字符 1~9。
    if (base.size() == 4 && (base.compare(0, 3, L"com") == 0 || base.compare(0, 3, L"lpt") == 0)) {
        return base[3] >= L'1' && base[3] <= L'9';
    }
    return false;
}

/// 段名是否含尾随点/空白（§4.2.1 步骤 7：NTFS 剥离段尾 '.' 与空白——
/// P-4/P-5 含此形态即拒绝，防"写入名≠引用名"的等价分裂）。
bool hasTrailingDotOrSpace(const std::wstring& segment)
{
    if (segment.empty()) {
        return false;   // 空段由段序列检查处置（连续分隔符），不在此判
    }
    const wchar_t last = segment.back();
    return last == L'.' || last == L' ' || last == L'\t';
}

/// 字符是否为 P-4 非法字符（§4.3.3 P-4 表：NUL、':'、'*'、'?'、'"'、
/// '<'、'>'、'|'、控制字符——zip 条目名不得含 Win32 文件名非法字符）。
bool isIllegalPackChar(wchar_t ch)
{
    if (ch < 0x20 || ch == 0x7F) {
        return true;    // 控制字符（C0＋DEL）
    }
    switch (ch) {
    case L':': case L'*': case L'?': case L'"':
    case L'<': case L'>': case L'|':
        return true;
    default:
        return false;
    }
}

// =====================================================================
// 输入分类（§4.2.1 步骤 1）
// =====================================================================

/// 输入形态分类（§4.2.1 步骤 1：盘符/根相对/UNC/设备前缀）。
enum class PathForm { Empty, Relative, DriveAbsolute, RootRelative, UncAbsolute, DevicePrefix };

/**
 * @brief 分类原始输入（不做任何改写）。
 *
 * \\?\ 前缀剥离后再分类剩余部分（\\?\C:\x→盘符；\\?\UNC\s\s→UNC）；
 * \\.\ 设备命名空间不属于文件通道（无合法角色），归入 DevicePrefix 交
 * 由统一拒绝（P-1/P-7 的读/写目标也是文件与目录，不是设备对象）。
 */
PathForm classifyInput(const std::wstring& raw)
{
    if (raw.empty()) {
        return PathForm::Empty;
    }
    if (raw.compare(0, 4, kLongPathPrefix) == 0) {
        const std::wstring rest = raw.substr(4);
        if (rest.compare(0, 4, L"UNC\\") == 0) {
            return PathForm::UncAbsolute;
        }
        if (rest.size() >= 2 && rest[1] == L':') {
            return PathForm::DriveAbsolute;
        }
        return PathForm::DevicePrefix;  // \\?\ 后接非盘符/UNC——设备/非常规
    }
    if (raw.compare(0, 4, L"\\\\.\\") == 0) {
        return PathForm::DevicePrefix;
    }
    if (raw.compare(0, 2, kUncPrefix) == 0) {
        return PathForm::UncAbsolute;
    }
    if (raw.size() >= 2 && raw[1] == L':') {
        return PathForm::DriveAbsolute;
    }
    if (raw.front() == L'\\' || raw.front() == L'/') {
        return PathForm::RootRelative;  // 根相对 \x（§4.2.1 步骤 1 分类）
    }
    return PathForm::Relative;
}

/**
 * @brief 由内部（已统一分隔符）路径生成脱敏 display（§4.3.2：UTF-8、
 *        去除 \\?\；\\?\UNC\s\s 呈现为 \\s\s）。
 */
std::string toDisplay(const std::wstring& extendedOrPlain)
{
    std::wstring plain = extendedOrPlain;
    if (plain.compare(0, 4, kLongPathPrefix) == 0) {
        plain = plain.substr(4);
        if (plain.compare(0, 4, L"UNC\\") == 0) {
            plain = std::wstring(kUncPrefix) + plain.substr(4);
        }
    }
    return utf16ToUtf8(plain);
}

// =====================================================================
// 词法消解（§4.2.1 步骤 2/3；§4.2.2 处置表）
// =====================================================================

/// 词法消解结果。
struct LexicalResult {
    bool escaped = false;        ///< true＝".." 抵穿起点/根（穿越尝试判据，§4.2.2 表）
    bool hadDotSegments = false; ///< true＝输入含 "." 或 ".." 段（P-4 最严校验判据）
    std::vector<std::wstring> segments; ///< 消解后的段序列（保留原大小写——display 用）
};

/**
 * @brief 词法消解：统一分隔符为 '\'、剥离 "."、".." 与前一非 ".." 段
 *        抵消（§4.2.1 步骤 2/3 原文）。
 *
 * 只产出段序列与判据，不组装根成分（根成分由 rootPrefixOf 单独提取，
 * 绝对路径的根——盘符/UNC share——不可被 ".." 抵消，§4.2.1 步骤 3）。
 * 抵穿判据＝**消解后段序列以 ".." 开头**（净逃逸——终点越过起点/根）：
 * §4.2.2 表行 `..\..\x`、`x\..\..\..\y` 与 §4.3.3 P-5 表
 * `objects/../../evil.dll` 均为净逃逸形态；对照 §4.3.3 P-5 ✅ 行
 * `objects/obj-a/../../objects/obj-b/x`——中途下探后回落（净结果仍在
 * 基点之下）不构成穿越。无角色语义的判据产出在此，是否拒绝由调用方
 * 角色规则决定（绝对净逃逸全角色拒绝；相对净逃逸仅 P-4/P-5/P-6 拒绝
 * ——P-1/P-2/P-7 的相对输入保留 ".." 给 canonical 对当前目录消解）。
 */
LexicalResult lexicalNormalize(const std::wstring& raw)
{
    LexicalResult r;
    // 步骤 2：统一分隔符（'/' 与 '\' 均合法，规范化为 '\'——比较用）。
    std::wstring unified(raw);
    std::replace(unified.begin(), unified.end(), L'/', L'\\');

    const PathForm form = classifyInput(unified);
    // 剥去根成分，只对余下部分分词。
    std::wstring body;
    switch (form) {
    case PathForm::DriveAbsolute:
        body = unified.substr(2);                       // 剥 "C:"
        if (!body.empty() && body.front() == L'\\') {
            body = body.substr(1);
        }
        break;
    case PathForm::UncAbsolute: {
        // 剥设备前缀得 "server/share/…"，再剥 server/share 两段（UNC
        // share＝根成分——不可抵消）。
        std::wstring rest = unified;
        if (rest.compare(0, 4, kLongPathPrefix) == 0) {
            rest = rest.substr(4);                      // 剥扩展前缀（4 码元）
            if (rest.compare(0, 4, L"UNC\\") == 0) {
                rest = rest.substr(4);                  // 剥 "UNC" 加分隔符
            }
        } else {
            rest = rest.substr(2);                      // 剥双反斜杠引导
        }
        const std::size_t s1 = rest.find(L'\\');
        if (s1 == std::wstring::npos) {
            body.clear();                               // 只有 server——畸形（rootPrefixOf 处拒）
            break;
        }
        const std::size_t s2 = rest.find(L'\\', s1 + 1);
        body = (s2 == std::wstring::npos) ? std::wstring() : rest.substr(s2 + 1);
        break;
    }
    case PathForm::RootRelative:
        body = unified.substr(1);                       // 剥前导 '\'
        break;
    default:
        body = unified;                                 // 相对/空
        break;
    }

    // 逐段消解：空段（连续分隔符）跳过；"." 剥离；".." 抵消，无可抵消
    // 段时保留 ".."（净逃逸的段证据——绝对路径的根不可消费）。
    std::size_t start = 0;
    while (start <= body.size()) {
        std::size_t sep = body.find(L'\\', start);
        if (sep == std::wstring::npos) {
            sep = body.size();
        }
        const std::wstring seg = body.substr(start, sep - start);
        if (!seg.empty()) {
            if (seg == L".") {
                r.hadDotSegments = true;                // 剥离（§4.2.1 步骤 3）
            } else if (seg == L"..") {
                r.hadDotSegments = true;
                if (!r.segments.empty() && r.segments.back() != L"..") {
                    r.segments.pop_back();              // 与前一非 ".." 段抵消
                } else {
                    r.segments.push_back(L"..");        // 无可抵消——保留（净逃逸判据）
                }
            } else {
                r.segments.push_back(seg);
            }
        }
        if (sep == body.size()) {
            break;
        }
        start = sep + 1;
    }
    // 净逃逸判定：消解后仍以 ".." 开头＝终点越过起点/根（见函数头注释
    // 的卡面行对照——临时下探后回落不是穿越）。
    r.escaped = !r.segments.empty() && r.segments.front() == L"..";
    return r;
}

/**
 * @brief 提取根成分前缀（消解后拼回用）；UNC 缺 share 视为畸形。
 *
 * 返回 nullopt＝畸形（UNC 只有 server 无 share，如 \\server）——所有
 * 角色拒绝（§4.2.2 表仅接受 \\server\share\… 形态）。
 */
std::optional<std::wstring> rootPrefixOf(const std::wstring& unified, PathForm form)
{
    // 先剥设备前缀得到常规形态（\\?\C:\x→C:\x；\\?\UNC\s\s\x→\\s\s\x）
    // ——设备形态不影响根成分语义，只影响前缀拼写。
    std::wstring rest = unified;
    if (rest.compare(0, 4, kLongPathPrefix) == 0) {
        rest = rest.substr(4);
        if (rest.compare(0, 4, L"UNC\\") == 0) {
            rest = std::wstring(kUncPrefix) + rest.substr(4);
        }
    }
    switch (form) {
    case PathForm::DriveAbsolute:
        return rest.substr(0, 2);                       // "C:"——根成分
    case PathForm::UncAbsolute: {
        if (rest.compare(0, 2, kUncPrefix) != 0) {
            return std::nullopt;
        }
        const std::wstring body = rest.substr(2);       // "server\share\…"
        const std::size_t s1 = body.find(L'\\');
        if (s1 == std::wstring::npos) {
            return std::nullopt;                        // 只有 server——畸形
        }
        const std::size_t s2 = body.find(L'\\', s1 + 1);
        // 根成分＝\\server\share（share 后无分隔符时取到串尾）。
        return std::wstring(kUncPrefix) + body.substr(0, s2);
    }
    case PathForm::RootRelative:
        return std::wstring(L"\\");                     // 当前盘符根目录语义
    default:
        return std::wstring();                          // 相对：无根成分
    }
}

/**
 * @brief 组装内部处理用的 \\?\ 扩展路径（§4.2.1 步骤 5："内部处理统一
 *        使用 \\?\ 扩展路径前缀形式"）。
 *
 * 盘符绝对→\\?\C:\…；UNC→\\?\UNC\server\share\…；根相对/相对不加前缀
 * （\\?\ 不接受相对形态）。输入须已统一为 '\' 分隔符。
 */
std::wstring toExtendedForm(const std::wstring& normalizedSlashes)
{
    const PathForm form = classifyInput(normalizedSlashes);
    if (form == PathForm::DriveAbsolute) {
        return std::wstring(kLongPathPrefix) + normalizedSlashes;
    }
    if (form == PathForm::UncAbsolute) {
        if (normalizedSlashes.compare(0, 8, L"\\\\?\\UNC\\") == 0) {
            return normalizedSlashes;                   // 已是 \\?\UNC\ 形态
        }
        // \\server\share\… → \\?\UNC\server\share\…（剥一个前导 '\'）。
        return L"\\\\?\\UNC\\" + normalizedSlashes.substr(2);
    }
    return normalizedSlashes;
}

// =====================================================================
// reparse point 逐段复核（§4.2.3 检测段）
// =====================================================================

/**
 * @brief 逐段 reparse point 复核：对 root＋前 i 段组成的每级前缀调
 *        GetFileAttributesW，检查 FILE_ATTRIBUTE_REPARSE_POINT。
 *
 * 为什么不用 std::filesystem::symlink_status 单独判定：MSVC 运行库对
 * junction 不报 symlink（reparse tag 不区分）——卡面 §4.2.3 明确要求
 * "symlink_status（不跟踪）＋Win32 GetFileAttributes 的
 * FILE_ATTRIBUTE_REPARSE_POINT 复核"，属性位是 symlink 与 junction 的
 * 公共判据。任何一级命中即记录该级扩展路径（诊断定位）。
 *
 * @param root     [in] 基点（项目根/中转根；其自身不检——由打开协议/
 *                 TempAreaManager 保证，本检覆盖基点之下的解析链）
 * @param segments [in] 消解后的段序列（保留原大小写）
 * @param offender [out] 首个含 reparse point 的层级路径（扩展形式）
 * @return true＝链上存在 reparse point
 */
bool chainHasReparsePoint(const std::filesystem::path& root, const std::vector<std::wstring>& segments,
                          std::wstring* offender)
{
    std::wstring acc = toExtendedForm(root.wstring());
    if (!acc.empty() && acc.back() == L'\\') {
        acc.pop_back();
    }
    for (const std::wstring& seg : segments) {
        acc.push_back(L'\\');
        acc += seg;
        const DWORD attrs = ::GetFileAttributesW(acc.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
            if (offender != nullptr) {
                *offender = acc;
            }
            return true;
        }
    }
    return false;
}

// =====================================================================
// 解析核
// =====================================================================

/// 角色 token（错误 params 的 role 值——稳定英文短语，§9.0 stage 同款纪律）。
std::string roleToken(PathRole role)
{
    switch (role) {
    case PathRole::UserSource:         return "user-source";
    case PathRole::ProjectRoot:        return "project-root";
    case PathRole::ImportStaging:      return "import-staging";
    case PathRole::PackEntry:          return "pack-entry";
    case PathRole::ProjectResourceRef: return "project-resource-ref";
    case PathRole::StagingTmp:         return "staging-tmp";
    case PathRole::ExportTarget:       return "export-target";
    }
    return "unknown";
}

/**
 * @brief 归一错误构造（path 类参数以脱敏 display 携带——文件头敏感性
 *        约束；detail 承载规则出处，供开发级日志对照卡面）。
 */
IoError makePathError(IoErrorCode code, const std::string& displayPath, std::string detail,
                      std::string roleTok = {})
{
    IoError e;
    e.code = code;
    if (!roleTok.empty()) {
        e.params.emplace_back("role", std::move(roleTok));
    }
    if (!displayPath.empty()) {
        e.params.emplace_back("path", displayPath);
    }
    e.detail = std::move(detail);
    return e;
}

/// 单条路径校验的输入包（normalize 内部核的参数聚合，避免长参数表）。
struct ValidateInput {
    PathRole role;                    ///< 角色（调用方声明——§4.1）
    std::wstring raw;                 ///< 原始输入
    const std::filesystem::path* base;///< P-5/P-6 基点（其余角色为 nullptr）
    const SafePathRuleSet* rules;     ///< 规则集（长度上限——SP-9）
};

/**
 * @brief SafePath 解析核：单条路径的完整规范化＋角色校验。
 *
 * 校验次序即 §4.4 步骤②的展开，每步注释标注卡面出处；成功时
 * NormalizedPath 各字段就绪，失败时错误已带脱敏 path 参数。
 */
IoResult<NormalizedPath> normalizeCore(const ValidateInput& in)
{
    IoResult<NormalizedPath> out;
    const SafePathRuleSet& rules = *in.rules;

    // ---- ①调用方契约检查（§9.1 契约表"非法调用"行）----
    // P-5/P-6 必须提供 base；其余角色 base 必须为空——防角色语义混用
    //（例如把项目根误传给 UserSource 会静默改变 canonical 基准）。
    if ((in.role == PathRole::ProjectResourceRef || in.role == PathRole::StagingTmp) != (in.base != nullptr)) {
        out.error = makePathError(IoErrorCode::FormatInternal, {},
                                  "base 与角色不匹配（P-5/P-6 必须提供，其余必须为空——§9.1 契约表）",
                                  roleToken(in.role));
        return out;
    }

    // ---- ②长度上限（§4.2.1 步骤 1：OS 硬上限；SP-9：总长/段长）----
    // 输入层先拒超长：超长串不进入段解析（防资源消耗型输入）。
    if (in.raw.size() > kOsMaxPathUtf16) {
        out.error = makePathError(IoErrorCode::SecPathTooLong, utf16ToUtf8(in.raw.substr(0, 64)) + "…",
                                  "输入超过 OS 上限 32767 UTF-16 码元（§4.2.1 步骤 1）", roleToken(in.role));
        return out;
    }
    if (in.raw.size() > rules.maxTotalLength) {
        out.error = makePathError(IoErrorCode::SecPathTooLong, utf16ToUtf8(in.raw),
                                  "总长超 SP-9 上限 " + std::to_string(rules.maxTotalLength) + "（§4.3.1）",
                                  roleToken(in.role));
        return out;
    }

    // ---- ③输入分类（§4.2.1 步骤 1；§4.2.2 表）----
    if (in.raw.empty()) {
        // 空串：P-4 拒绝（§4.2.2 表）；P-5"视为项目根本身（仅允许作为解
        // 析基点，不作资源引用）"→ 同拒；其余角色无路径语义 → 拒。
        out.error = makePathError(IoErrorCode::SecPathEscape, {},
                                  "空路径（§4.2.2：P-4 拒绝；P-5 仅允许作解析基点不作资源引用）",
                                  roleToken(in.role));
        return out;
    }
    const std::wstring unified = [&] {
        std::wstring u = in.raw;
        std::replace(u.begin(), u.end(), L'/', L'\\');  // 步骤 2：统一分隔符
        return u;
    }();
    const PathForm form = classifyInput(unified);
    if (form == PathForm::Empty || form == PathForm::DevicePrefix) {
        // 仅分隔符的退化输入（如 "\\"）与设备命名空间：不属于任何合法
        // 文件路径形态。
        out.error = makePathError(IoErrorCode::SecPathEscape, utf16ToUtf8(in.raw),
                                  "退化/设备形态路径不属于文件通道（§4.2.1 步骤 1 分类）", roleToken(in.role));
        return out;
    }

    // P-4（包内条目）与 P-5（项目内引用）与 P-6（中转片段）要求纯相对
    // ——绝对/根成分/UNC 一律 IO-SEC-PATH-ESCAPE（§4.3.3 P-4 表"/etc/
    // passwd、C:\evil、\\?\C:\evil"；P-5 表"C:\proj\objects\x 绝对形式
    // 持久化引用非法"、"\server\share\objects\x UNC 不得出现"）。
    const bool roleRequiresRelative = in.role == PathRole::PackEntry
                                   || in.role == PathRole::ProjectResourceRef
                                   || in.role == PathRole::StagingTmp;
    if (roleRequiresRelative
        && (form == PathForm::DriveAbsolute || form == PathForm::UncAbsolute || form == PathForm::RootRelative)) {
        out.error = makePathError(IoErrorCode::SecPathEscape, utf16ToUtf8(in.raw),
                                  "P-4/P-5/P-6 要求纯相对路径（根成分/盘符/UNC 禁止——§4.3.3 表、SP-2）",
                                  roleToken(in.role));
        return out;
    }

    // ---- ④词法消解与根成分提取（§4.2.1 步骤 3；§4.2.2 表）----
    const LexicalResult lex = lexicalNormalize(unified);
    const std::optional<std::wstring> rootPrefix = rootPrefixOf(unified, form);
    if (!rootPrefix.has_value()) {
        out.error = makePathError(IoErrorCode::SecPathEscape, utf16ToUtf8(in.raw),
                                  "UNC 缺 share 成分（§4.2.2 仅接受 \\\\server\\share\\… 形态）",
                                  roleToken(in.role));
        return out;
    }

    // ".." 净逃逸（消解后段序列以 ".." 开头——见 lexicalNormalize 的卡
    // 面行对照）：绝对形态（盘符/UNC/根相对——根成分不可越过）全角色拒
    // 绝（§4.2.2 表"C:\x\..\..\y | .. 抵穿盘符根 → IO-SEC-PATH-ESCAPE"
    // ——行无角色限定）；相对净逃逸仅 P-4/P-5/P-6 拒绝（"段序列抵穿起
    // 点"行点名三角色）——P-1/P-2/P-7 的相对输入保留 ".."（canonical 对
    // 当前目录消解；用户源/导出目标不施加资源区式管辖——NFR-SEC-01 例
    // 外不扩大化）。
    if (lex.escaped) {
        const bool absoluteEscape = (form == PathForm::DriveAbsolute || form == PathForm::UncAbsolute
                                     || form == PathForm::RootRelative);
        if (absoluteEscape || roleRequiresRelative) {
            out.error = makePathError(IoErrorCode::SecPathEscape, utf16ToUtf8(in.raw),
                                      absoluteEscape ? ".. 越过根成分（§4.2.2 表）"
                                                     : "消解后越过起点（P-4/P-5/P-6——§4.2.2 表）",
                                      roleToken(in.role));
            return out;
        }
    }

    // ---- ⑤段级规则（角色分层：P-4 最严——§4.1 校验强度列）----
    for (const std::wstring& seg : lex.segments) {
        // 段长（SP-9：单段 ≤255——§4.3.1）。
        if (seg.size() > rules.maxSegmentLength) {
            out.error = makePathError(IoErrorCode::SecPathTooLong, utf16ToUtf8(in.raw),
                                      "段长超 SP-9 上限 " + std::to_string(rules.maxSegmentLength) + "（§4.3.1）",
                                      roleToken(in.role));
            return out;
        }
        if (in.role == PathRole::PackEntry
            && std::any_of(seg.begin(), seg.end(), isIllegalPackChar)) {
            // P-4 非法字符（§4.3.3 P-4 表"段含 ':'"行——NUL/:*?"<>| 与
            // 控制字符 → IO-FORMAT-PACK-ENTRY）。
            out.error = makePathError(IoErrorCode::FormatPackEntry, utf16ToUtf8(in.raw),
                                      "条目段含 Win32 非法字符（§4.3.3 P-4 表）", roleToken(in.role));
            return out;
        }
        if (in.role == PathRole::PackEntry || in.role == PathRole::ProjectResourceRef) {
            // 保留名/尾随点空白仅对受管辖角色拒绝（§4.2.1 步骤 6/7：
            // "对 P-4/P-5 角色拒绝；P-1/P-7 角色放行——系统自身规则裁决"）。
            // P-5 的控制字符同拒（保留字符族——IoError.hpp SecPathReserved）。
            const bool hasControl = std::any_of(seg.begin(), seg.end(),
                                                [](wchar_t c) { return c < 0x20 || c == 0x7F; });
            if (hasControl || isReservedDeviceName(seg) || hasTrailingDotOrSpace(seg)) {
                out.error = makePathError(IoErrorCode::SecPathReserved, utf16ToUtf8(in.raw),
                                          "保留名/尾随点或空白/控制字符（§4.2.1 步骤 6~7——SP-8）",
                                          roleToken(in.role));
                return out;
            }
        }
    }

    // P-4 最严项：输入含 "."/".." 段（消解前）一律拒绝（§4.3.3 P-4 表
    // "payload/../evil → ESCAPE"；SP-2 的最严解读：条目名来自不可信 zip
    // 名，无任何理由允许上溯拼写。对照 P-5"消解后仍在区内即放行"——两
    // 角色差异是卡面明确设计，§4.3.3 两表对照可证）。
    if (in.role == PathRole::PackEntry && lex.hadDotSegments) {
        out.error = makePathError(IoErrorCode::SecPathEscape, utf16ToUtf8(in.raw),
                                  "条目含 ./.. 段（SP-2 最严：包内条目禁止任何上溯拼写——§4.3.3 P-4 表）",
                                  roleToken(in.role));
        return out;
    }

    // ---- ⑥组装规范化路径（根成分＋消解后段）----
    // native 目标形态＝\\?\ 扩展形式（§4.2.1 步骤 5）；P-5/P-6 以 base
    // 为根拼接（基点由 project/调用方协议保证，io 只消费——§4.1 P-2 行
    // "所有权归 project，io 仅接收规范化结果"）。
    std::wstring joined;
    if (in.base != nullptr) {
        joined = toExtendedForm(in.base->wstring());    // 基点先扩展化
        if (joined.back() == L'\\') {
            joined.pop_back();
        }
        for (const std::wstring& seg : lex.segments) {
            joined.push_back(L'\\');
            joined += seg;
        }
    } else {
        joined = *rootPrefix;
        const bool needsSeparator = (form == PathForm::DriveAbsolute || form == PathForm::UncAbsolute);
        for (const std::wstring& seg : lex.segments) {
            if (needsSeparator || !joined.empty()) {
                // 盘符/UNC 根成分后补分隔符；根相对的引导 '\' 已在根前缀内。
                if (needsSeparator || joined.back() != L'\\') {
                    joined.push_back(L'\\');
                }
            }
            joined += seg;
        }
        // 根相对形态末尾多余分隔符清理（"\a\"→"\a"；无段时保留根本身）。
        if (form == PathForm::RootRelative && lex.segments.empty()) {
            joined = L"\\";                             // "\("/")——根本身
        }
    }

    // ---- ⑦角色专属实体检查 ----
    bool exists = false;
    std::uint64_t fsize = 0;
    bool isRegular = false;

    if (in.role == PathRole::ProjectResourceRef) {
        // SP-3：解析结果必须落在资源区根（objects/ 或 catalog/）内——
        // §4.3.3 示例表以"首段折叠后命中 objects|catalog"为判据（基＝
        // 项目根）。段序列为空＝指向项目根本身→不在区内→拒。
        if (lex.segments.empty()) {
            out.error = makePathError(IoErrorCode::SecPathEscape, utf16ToUtf8(in.raw),
                                      "资源引用指向项目根本身，不在资源区内（SP-3——§4.3.3）",
                                      roleToken(in.role));
            return out;
        }
        const std::wstring firstFolded = foldCase(lex.segments.front());
        if (firstFolded != kObjectsDir && firstFolded != kCatalogDir) {
            out.error = makePathError(IoErrorCode::SecPathEscape, utf16ToUtf8(in.raw),
                                      "引用不在资源区（首段非 objects/catalog——SP-3）", roleToken(in.role));
            return out;
        }
        // §4.2.3 P-5：解析链上任何一段为 reparse point → IO-SEC-SYMLINK
        //（"不区分目标是否仍在区内——一律拒绝，规则简单可审计"）。
        std::wstring offender;
        if (chainHasReparsePoint(*in.base, lex.segments, &offender)) {
            out.error = makePathError(IoErrorCode::SecPathSymlink, toDisplay(offender),
                                      "解析链含符号链接/junction/reparse point（§4.2.3 P-5 一律拒绝）",
                                      roleToken(in.role));
            return out;
        }
    } else if (in.role == PathRole::StagingTmp) {
        // §4.2.3 P-6：io 创建的中转区禁止 reparse point（发现即清理重建
        // 会话——清理动作归 TempAreaManager，本层先拒绝）。
        std::wstring offender;
        if (chainHasReparsePoint(*in.base, lex.segments, &offender)) {
            out.error = makePathError(IoErrorCode::SecPathSymlink, toDisplay(offender),
                                      "中转链含 reparse point（§4.2.3 P-6 禁止）", roleToken(in.role));
            return out;
        }
    } else if (in.role == PathRole::UserSource) {
        // P-1 准入校验（§4.1）：存在性（§4.2.5：不存在→IO-RES-NOT-FOUND，
        // 与权限/只读/锁竞争互斥分码）。不施加资源区逃逸检查、不拒绝
        // symlink（§4.2.3 P-1 放行，跟随由 OS 决定；读取快照按最终实体
        // 路径记录——canonical 化的 equivKey 即承载）——NFR-SEC-01 例外
        // 不扩大化（契约 acceptance 4）。
        std::error_code ec;
        const std::filesystem::path target = std::filesystem::weakly_canonical(std::filesystem::path(joined), ec);
        if (ec) {
            out.error = makePathError(IoErrorCode::ResNotFound, toDisplay(joined),
                                      "用户源不可定位（canonical 失败——§4.2.5 分类一）", roleToken(in.role));
            return out;
        }
        const std::filesystem::file_status st = std::filesystem::status(target, ec);
        if (ec || !std::filesystem::exists(st)) {
            out.error = makePathError(IoErrorCode::ResNotFound, toDisplay(target.wstring()),
                                      "用户源不存在（§4.2.5 分类一：ERROR_FILE/PATH_NOT_FOUND 族）",
                                      roleToken(in.role));
            return out;
        }
        exists = true;
        if (st.type() == std::filesystem::file_type::regular) {
            isRegular = true;
            const auto sz = std::filesystem::file_size(target, ec);
            if (!ec) {
                fsize = sz;
            }
        }
    }
    // P-2/P-3/P-7 无实体强校验：P-2 所有权归 project（打开协议保证存在）；
    // P-3 由 io 创建；P-7 导出目标允许尚不存在（可写性预检归
    // IAtomicFileWriter——§4.6，IO-T06 落位）。

    // ---- ⑧等价类键（§4.2.4：折叠＋canonical；失败退化为折叠键）----
    // canonical 取实路径（大小写/短名归真）；weakly_canonical 对不存在
    // 目标退化为词法化——两种途径同键规则（§9.1 确定性"降级口径一致"；
    // §4.2.4"等价键计算失败退化为折叠键＋提示性诊断，不阻断用户源角色"）。
    std::error_code ec;
    std::filesystem::path canon = std::filesystem::weakly_canonical(std::filesystem::path(joined), ec);
    const std::wstring equivWide = ec ? joined : canon.wstring();

    // ---- ⑨产出 NormalizedPath（§4.3.2 各字段）----
    out.value.native = toExtendedForm(equivWide);
    out.value.display = toDisplay(out.value.native);
    // relKey：P-4/P-5 管辖根内相对键（正斜杠、小写折叠——§4.3.2 字段注；
    // 重复条目检测/缓存键用）。
    if (in.role == PathRole::PackEntry || in.role == PathRole::ProjectResourceRef) {
        std::wstring rel;
        for (const std::wstring& seg : lex.segments) {
            if (!rel.empty()) {
                rel.push_back(L'/');
            }
            rel += foldCase(seg);
        }
        out.value.relKey = utf16ToUtf8(rel);
    }
    out.value.equivKey = utf16ToUtf8(foldCase(equivWide));
    out.value.isUnc = (form == PathForm::UncAbsolute);
    if (exists && isRegular) {
        out.value.fileSize = fsize;     // 提示性；不作身份（§4.3.2 字段注）
    }
    return out;
}

/// SafePathResolver 实现（无状态——规则集构造后不可变，§9.1 并发只读）。
class SafePathResolver final : public ISafePathResolver {
public:
    explicit SafePathResolver(SafePathRuleSet rules) : m_rules(std::move(rules)) {}

    IoResult<NormalizedPath> normalize(PathRole role, const std::wstring& rawPath,
                                       const std::filesystem::path& base) const override
    {
        // base 的非空判定走值拷贝（接口以 const& 接收，核内存裸指针——
        // 调用栈内存活期，无所有权转移）。
        ValidateInput in{role, rawPath, nullptr, &m_rules};
        std::filesystem::path baseCopy;
        if (!base.empty()) {
            baseCopy = base;
            in.base = &baseCopy;
        }
        return normalizeCore(in);
    }

    IoResult<void> normalizePackEntries(std::size_t entryCount,
                                        const std::function<IoString(std::size_t)>& entryAt) const override
    {
        IoResult<void> out;
        if (entryCount == 0) {
            return out;     // 空包：合法（无条目即无违规）
        }
        if (!entryAt) {
            out.error = makePathError(IoErrorCode::FormatInternal, {},
                                      "entryAt 访问器为空（§9.1 契约表非法调用行）");
            return out;
        }

        // 折叠键集合（§4.2.4：包条目重复检测；std::set 只做存在性判定，
        // 迭代序不参与结果——确定性由条目序号升序承载）。
        std::set<std::string, std::less<>> seen;

        // "errors 全量列出"（§9.1）的承载：违规明细按条目序号升序累积进
        // detail；IoError.code 取最小序号处错误（确定性）。
        bool anyError = false;
        IoErrorCode firstCode = IoErrorCode::Ok;
        std::string detail;

        for (std::size_t i = 0; i < entryCount; ++i) {
            const std::wstring entry = utf8ToUtf16(entryAt(i));
            ValidateInput in{PathRole::PackEntry, entry, nullptr, &m_rules};
            const IoResult<NormalizedPath> r = normalizeCore(in);
            if (!r) {
                if (!anyError) {
                    firstCode = r.error.code;               // 最小序号错误定码
                    anyError = true;
                }
                detail += "index=" + std::to_string(i) + " code=" + std::string(errorCodeToken(r.error.code))
                          + " entry=" + utf16ToUtf8(entry) + " detail=" + r.error.detail + "\n";
                continue;
            }
            // 折叠键查重（§4.3.3 P-4 表末行：仅大小写异的两条目＝重复——
            // Windows 落盘冲突预防，"后写覆盖先写"的歧义必须整体拒绝）。
            if (!seen.insert(r.value.relKey).second) {
                if (!anyError) {
                    firstCode = IoErrorCode::PackDuplicateEntry;
                    anyError = true;
                }
                detail += "index=" + std::to_string(i) + " code=duplicate-entry entry=" + utf16ToUtf8(entry)
                          + " relKey=" + r.value.relKey + "\n";
            }
        }
        if (anyError) {
            out.error.code = firstCode;
            out.error.detail = "包条目批量预检失败（任一非法即整批拒绝——§9.1）；违规清单：\n" + detail;
            // params 携带条目总数（观测面；逐条明细在 detail——明细容量
            // 随包条目数线性，不入 params 以免诊断信封膨胀）。
            out.error.params.emplace_back("entries", std::to_string(entryCount));
        }
        return out;
    }

private:
    /// 规则集（构造后不可变——本类无任何写路径，并发只读安全）。
    SafePathRuleSet m_rules;
};

} // namespace

// =====================================================================
// 工厂与 EquivKeyMutex（公共接口——声明见 SafePath.hpp）
// =====================================================================

ISafePathResolverPtr makeSafePathResolver(const SafePathRuleSet& rules)
{
    return std::make_shared<SafePathResolver>(rules);
}

bool EquivKeyMutex::tryAcquire(const std::string& equivKey)
{
    // 写路径：独占锁插入（已持有→false——"不产生双份会话"§4.2.4）。
    std::unique_lock lock(m_mutex);
    return m_held.insert(equivKey).second;
}

void EquivKeyMutex::release(const std::string& equivKey)
{
    // 幂等释放（cleanup 重试路径可能重复 release——erase 计数不校验）。
    std::unique_lock lock(m_mutex);
    m_held.erase(equivKey);
}

bool EquivKeyMutex::isHeld(const std::string& equivKey) const
{
    std::shared_lock lock(m_mutex);
    return m_held.find(equivKey) != m_held.end();
}

std::size_t EquivKeyMutex::heldCount() const
{
    std::shared_lock lock(m_mutex);
    return m_held.size();
}

} // namespace sdurws::ird::io
