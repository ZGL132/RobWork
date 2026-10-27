/**
 * @file   Codec.cpp
 * @brief  project 自有格式 canonical 编解码实现（§4.8）——dump 自持转义
 *         编码器＋parse 走 Boost.JSON 公共渠道（REUSE-T54）。
 *
 * 设计依据：
 *   - units/project.md §4.8（canonical 编码契约：ASCII、固定字段序、无浮点
 *     ——全字符串身份与整数、parse(dump(x))==x；"JsonLite（testkit）不用于
 *     产品格式"）、§4.4（字段级契约与表列序）、§8.11（版本判定：旧格式
 *     format-legacy/未来版本 schema-future＋升级指引数据——PM-06/NFR-DEP-04）；
 *   - 任务契约 tasks/REUSE-T54.json（P5/T05-project 解析家族迁移批）：
 *     Boost.JSON 渠道经所有者 2026-10-26 A4 裁决批准（vcpkg 公共依赖，批准
 *     登记见 traceability/reuse/REUSE-T43/byte-primitives-decision.md §6）；
 *   - 任务契约 tasks/foundation/PRJ-T04.json acceptance 1～4（CR-02：摘要
 *     只经 core ContentDigester——本文件除 contentVersionOf 外无任何哈希
 *     计算，digest 类字段全部透传）。
 *
 * 引擎分工（REUSE-T54 落值口径，随实现登记——DTB §5.4）：
 *   - dump 侧自持（不换）：§4.8 要求输出恒为纯 ASCII（非 ASCII 经小写
 *     \uXXXX/代理对转义），Boost.JSON 序列化器输出原生 UTF-8（探针实证
 *     traceability/reuse/REUSE-T54/probe），不满足字节面契约——故编码器
 *     （appendJsonString/decodeUtf8At/appendUnicodeEscape/appendHexString）
 *     为产品冻结契约的载体，保持原实现逐字节不变（NFR-COR-02：同值必同
 *     字节，黄金字节用例钉死字段序与紧凑形态）。
 *   - parse 侧两段：①canonical 词法门 scanCanonical（自持——Boost.JSON
 *     是宽容解析器，探针实证其接受结构空白/浮点/bool/null 且把重复键坍缩
 *     保留后值，这些"非 canonical 即损坏"的判定（PM-02 宁可拒绝不猜测）
 *     必须由本单元自持；旧 JsonValue/JsonParser 递归下降 DOM 全族就此删除，
 *     词法门为单遍线性扫描，无 DOM 构建无递归）；②boost::json::parse 构建
 *     DOM（成熟第三方原语承担结构/转义/整数解析——core.Compare.closeWithin
 *     Boost 定宽算术同型先例），解析异常一律转 store-corrupt。
 *   - 词法门规则（§4.8 canonical 落值口径，与被删旧引擎逐条对应）：
 *       1) 拒绝 ≥0x80 原始字节（磁盘格式 ASCII——GBK 误写/截断拼接防线）
 *          与 <0x20 裸控制字符（canonical 只允许转义形态）；
 *       2) 字符串外仅允许结构字符 { } [ ] : , 与 " 与数字/负号——结构空白
 *          （空格/制表/换行）、浮点（.）、指数（e/E）、true/false/null 字面
 *          量、一切裸词由此落入默认拒绝（§4.4 契约无布尔/空字段——缺失即
 *          可选；无浮点）；
 *       3) 整数：'-'? [0-9]+，前导零拒绝（同值第二字节形态——确定性
 *          NFR-COR-02），负号后必须有数字；无小数/指数；
 *       4) 字符串内：可打印 ASCII 原样；转义对称表 \" \\ \/ \b \f \n \r \t
 *          \uXXXX（hex 大小写均可——解码结果唯一）；孤立代理拒绝（不存在
 *          合法字符），代理对合成；
 *       5) 重复键拒绝（同一对象两义性，canonical 不存在——boost 坍缩重复
 *          键故必须自持：以解码后键文本比对，转义写法的同键同样命中）；
 *       6) 嵌套深度 ≤64（kMaxDepth——防恶意深嵌套耗尽栈；§4.4 最深结构仅
 *          4 层，64 已富余两个数量级；boost max_depth 同值双重防线）。
 *   - 版本判定/未知字段容忍（§8.11/§4.8 次版本兼容）、身份字段 core 单源
 *     解析（CoreId::tryFromCanonical——身份事实单源 core）、payloadCanonical
 *     hex 文本编码（UI-T39）语义零变化。
 *
 * 线程安全：全部函数纯函数（无共享可变状态）。
 */

#include "Codec.hpp"

#include <sdurws/ird/project/StoreTypes.hpp>

#include <boost/json.hpp>
#include <boost/system/system_error.hpp>

#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bj = boost::json;

namespace sdurws::ird::project::codec {
namespace {

// 实现细节全部收敛于匿名命名空间——不向库外暴露任何符号（R-2 纪律）。

// =====================================================================
// 错误辅助：数据侧错误统一 StoreError（detail 前缀 "project/codec:"）
// =====================================================================

/**
 * @brief 抛出 store-corrupt（数据侧错误：语法/结构/字段/值域非法）。
 *
 * @param why [in] 具体原因（英文 key=value/短语——detail 为机器可读开发
 *            诊断，§5.0 约定；用户文案归 diagnostics 供文案链路）
 */
[[noreturn]] void corrupt(const std::string& why)
{
    throw StoreError(StoreErrorCode::StoreCorrupt, "project/codec: " + why);
}

/**
 * @brief schemaVersion 主版本判定（§8.11 行 1/2——NFR-DEP-04 数据侧）。
 *
 * 判定只看主版本（编码见 kSchemaVersionScale 注释）：
 *   - 主版本 > 当前支持 → SchemaFuture（未来版本）：detail 携带升级指引
 *     数据三键——document（文档自报版本＝"项目版本"展示源）、supported
 *     （当前支持版本）、upgrade（升级工具入口 token，阶段 B ISchemaUpgrader
 *     §8.11 冻结前的固定 token）；不自动升级（PM-06）。
 *   - 主版本 < 当前支持（含 0.x/负值——非法版本同样走"比支持的旧"）→
 *     FormatLegacy（重构前/旧格式）：稳定只读拒绝（§8.11 行 1）。
 *   - 主版本相同：次版本高于本实现＝上级版本追加了可选字段 → 返回 true
 *     （容忍未知字段——§4.8"追加可选字段＝次版本兼容"）。
 *
 * @param documentVersion [in] 文档自报 schemaVersion（int32 范围内）
 * @param typeName        [in] 诊断用类型名（如 "draft-document"）
 * @return 是否容忍未知字段（同主版本且次版本更新）
 */
bool checkSchemaVersion(int documentVersion, const char* typeName)
{
    // 整数除法向零取整：负版本的"主版本"截断为 ≤0，落入 legacy 分支——
    // 语义正确（负/零主版本不可能比当前更新）。
    const int docMajor = documentVersion / kSchemaVersionScale;
    if (docMajor > kSchemaVersionMajorCurrent) {
        // 未来版本：拒绝＋升级指引数据（document/supported/upgrade 三键
        // 机器可读——PM-06"显示当前版本、项目版本、升级工具入口"的数据面）。
        throw StoreError(StoreErrorCode::SchemaFuture,
                         "project/codec: schema-future document="
                             + std::to_string(documentVersion)
                             + " supported=" + std::to_string(kSchemaVersionCurrent)
                             + " upgrade=ISchemaUpgrader-stage-b type=" + typeName);
    }
    if (docMajor < kSchemaVersionMajorCurrent) {
        // 旧格式：稳定只读拒绝，不提供读取（§8.11 行 1；原文件不动归
        // 打开协议载体 PRJ-T08——本层只负责判定与拒绝）。
        throw StoreError(StoreErrorCode::FormatLegacy,
                         "project/codec: format-legacy document="
                             + std::to_string(documentVersion)
                             + " supported=" + std::to_string(kSchemaVersionCurrent)
                             + " type=" + typeName);
    }
    // 同主版本：次版本高于当前实现 → 文档可能含本实现未知的追加可选字段。
    const int docMinor = documentVersion % kSchemaVersionScale;
    return docMinor > kSchemaVersionMinorCurrent;
}

/**
 * @brief formatId 魔数检查（§8.11 行 1：标识不符＝旧格式——不提供读取）。
 *
 * @param formatId [in] 文档自报 formatId
 */
void checkFormatId(const std::string& formatId)
{
    if (formatId != kFormatId) {
        throw StoreError(StoreErrorCode::FormatLegacy,
                         "project/codec: format-legacy document-format-id=" + formatId
                             + " supported=" + kFormatId);
    }
}

// =====================================================================
// canonical 词法门（单遍线性扫描——旧 JsonValue/JsonParser 递归下降全族
// 的替代；只做词法 canonical 判定与重复键/深度防御，DOM 构建归 Boost.JSON）
// =====================================================================

/// 嵌套深度上限（防恶意深嵌套耗尽栈——非法输入防御；§4.4 最深结构仅 4 层，
/// 64 已富余两个数量级；与 boost::json parse_options::max_depth 同值双重
/// 防线——词法门先行给出稳定诊断，boost 层为兜底）。
constexpr int kMaxDepth = 64;

/**
 * @brief 对象/数组栈帧：结构上下文＋已出现键集合（重复键判定载体）。
 *
 * keys 存解码后键文本（与被删旧引擎的解码比对语义一致——转义写法的同键
 * 同样命中；canonical 键均为无需转义的固定字段名，此集合通常极小）。
 */
struct ScanFrame {
    bool isObject = false;
    std::set<std::string> keys;
};

/// 码点 → UTF-8 追加（标准最短形式；与 dump 侧 decodeUtf8At 互逆——
/// 转义键文本的解码还原用）。
void appendUtf8CodePoint(std::string& out, std::uint32_t cp)
{
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
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

/**
 * @brief 读取 4 位十六进制（JSON \u 语义：大小写均可——dump 端输出小写，
 *        解析端接受规范 JSON 的两种写法；解码结果唯一，不构成 canonical
 *        二义；与被删旧引擎 parseHex4 同语义）。
 *
 * @param p   [in,out] 扫描位置（前进 4 字节）
 * @param end [in] 输入末尾哨兵
 * @return 码元值
 */
std::uint32_t scanHex4(const char*& p, const char* end)
{
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
        if (p == end) { corrupt("truncated \\u escape（不足 4 位十六进制）"); }
        const char c = *p++;
        v <<= 4;
        if (c >= '0' && c <= '9') { v |= static_cast<std::uint32_t>(c - '0'); }
        else if (c >= 'a' && c <= 'f') { v |= static_cast<std::uint32_t>(c - 'a' + 10); }
        else if (c >= 'A' && c <= 'F') { v |= static_cast<std::uint32_t>(c - 'A' + 10); }
        else { corrupt("invalid \\u escape（需 4 位十六进制）"); }
    }
    return v;
}

/**
 * @brief \uXXXX 解码：BMP 直出 UTF-8；高位代理必须跟随低位代理（孤立代理
 *        拒绝——不存在合法字符，保留将产生无法 round-trip 的悬空字节；
 *        与被删旧引擎 parseUnicodeEscape 同语义；boost 默认同样拒绝孤立
 *        代理——探针实证——本层先行给出稳定诊断）。
 */
void scanUnicodeEscape(const char*& p, const char* end, std::string& out)
{
    const std::uint32_t u1 = scanHex4(p, end);
    std::uint32_t cp = u1;
    if (u1 >= 0xD800 && u1 <= 0xDBFF) {
        // 高位代理：必须是 \uDC00-\uDFFF 低代理续体（JSON surrogate pair）。
        if (end - p < 2 || p[0] != '\\' || p[1] != 'u') {
            corrupt("lone high surrogate in \\u escape（缺低位代理续体）");
        }
        p += 2;  // 消费 "\u"
        const std::uint32_t u2 = scanHex4(p, end);
        if (u2 < 0xDC00 || u2 > 0xDFFF) {
            corrupt("invalid low surrogate in \\u escape（越出 DC00-DFFF）");
        }
        cp = 0x10000 + ((u1 - 0xD800) << 10) + (u2 - 0xDC00);
    } else if (u1 >= 0xDC00 && u1 <= 0xDFFF) {
        corrupt("lone low surrogate in \\u escape（缺高位代理前体）");
    }
    appendUtf8CodePoint(out, cp);
}

/**
 * @brief 扫描一个字符串字面量（起点为已定位的开口引号）并产出解码文本。
 *
 * 词法规则（文件头规则 4）：可打印 ASCII 原样；转义对称表 \" \\ \/ \b \f
 * \n \r \t \uXXXX；裸控制字符与非 ASCII 原始字节拒绝（磁盘格式 ASCII）。
 *
 * @param p   [in,out] 扫描位置（进入时指向开口引号，返回时越过闭口引号）
 * @param end [in] 输入末尾哨兵
 * @param out [out] 解码后的 UTF-8 文本（重复键判定的比对载体）
 */
void scanString(const char*& p, const char* end, std::string& out)
{
    ++p;  // 越过开口引号
    out.clear();
    for (;;) {
        if (p == end) { corrupt("truncated string（缺收尾引号）"); }
        const unsigned char c = static_cast<unsigned char>(*p++);
        if (c == '"') { return; }
        if (c != '\\') {
            if (c < 0x20) {
                // 裸控制字符：canonical 输出必转义（文件头规则 1），出现即非规范。
                corrupt("raw control character in string（须以转义形式出现）");
            }
            if (c >= 0x80) {
                // 非 ASCII 原始字节：磁盘格式 ASCII（§4.8）——可能来自编码
                // 错乱（GBK 误写）或截断拼接，拒绝而非猜测转码。
                corrupt("non-ASCII byte in string（磁盘格式为 ASCII；非 ASCII"
                        " 须以 \\uXXXX 转义出现）");
            }
            out.push_back(static_cast<char>(c));
            continue;
        }
        if (p == end) { corrupt("truncated escape sequence"); }
        const char e = *p++;
        switch (e) {
        case '"':  out.push_back('"');  break;
        case '\\': out.push_back('\\'); break;
        case '/':  out.push_back('/');  break;
        case 'b':  out.push_back('\b'); break;
        case 'f':  out.push_back('\f'); break;
        case 'n':  out.push_back('\n'); break;
        case 'r':  out.push_back('\r'); break;
        case 't':  out.push_back('\t'); break;
        case 'u':  scanUnicodeEscape(p, end, out); break;
        default:
            corrupt(std::string("invalid escape '\\") + e + "'");
        }
    }
}

/**
 * @brief 扫描一个数字字面量（'-'? [0-9]+；前导零拒绝；无小数/指数——
 *        '.'/'e'/'E' 不在数字体字符集内，数字体结束后的该类字符由主循环
 *        的字符串外字符集拒绝，与被删旧引擎的词法层拒绝同型——§4.8 无浮点）。
 *
 * @param p     [in,out] 扫描位置（进入时指向首字符 '-' 或数字）
 * @param end   [in] 输入末尾哨兵
 * @param first [in] 首字符（'-' 或数字）
 */
void scanNumber(const char*& p, const char* end, char first)
{
    if (first == '-') {
        ++p;
        if (p == end || *p < '0' || *p > '9') {
            corrupt("number requires at least one digit（负号后无数字）");
        }
    }
    std::size_t digits = 0;
    bool firstDigitWasZero = false;
    while (p != end && *p >= '0' && *p <= '9') {
        if (digits == 0 && *p == '0') { firstDigitWasZero = true; }
        ++digits;
        ++p;
    }
    // "01" 这类前导零＝同一数值的第二种字节形态，canonical 不允许
    // （确定性 NFR-COR-02：同值必同字节；boost 层亦拒绝——探针实证）。
    if (digits > 1 && firstDigitWasZero) {
        corrupt("leading zero in number（非 canonical）");
    }
}

/**
 * @brief canonical 词法门主扫描（文件头"词法门规则"全条目；扫描通过后
 *        输入才交给 boost::json::parse——两段中的第一段）。
 *
 * 上下文校验取"足以自持"的最小集：结构配对/成员逗号序等纯结构合法性交
 * boost 兜底（同为 store-corrupt，仅诊断文案不同）；本层自持的是 boost
 * 不判定的 canonical 词法面（空白/ASCII/浮点/字面量/重复键/深度）。
 *
 * @param text [in] 磁盘字节（整个文档）
 */
void scanCanonical(const std::string_view text)
{
    const char* p = text.data();
    const char* const end = text.data() + text.size();
    std::vector<ScanFrame> stack;
    std::string lastString;       ///< 最近一个字符串的解码文本（键候选）
    bool haveLastString = false;  ///< 最近字符串是否可用（防止 "{:" 误判）

    while (p != end) {
        const char c = *p;
        if (c == '"') {
            scanString(p, end, lastString);  // 整段字符串内部前进（含转义）
            haveLastString = true;
            continue;
        }
        const auto u = static_cast<unsigned char>(c);
        if (u >= 0x80) {
            // 非 ASCII 原始字节：磁盘格式 ASCII（§4.8）——任何位置拒绝。
            corrupt("non-ASCII byte outside string（磁盘格式为 ASCII；非 ASCII"
                    " 须以 \\uXXXX 转义出现）");
        }
        if (u < 0x20) {  // MUTATION-T54
            // 结构空白（空格/制表/换行）与裸控制字符：带空白的字节流＝损坏/
            // 外部篡改产物（PM-02 读校验精神——宁可拒绝也不猜测；字符串内
            // 的同类字节已在 scanString 内拒绝，此处只见字符串外）。
            corrupt("whitespace/control byte outside string（canonical 紧凑"
                    "无空白——非 canonical 形态拒绝）");
        }
        switch (c) {
        case '{':
        case '[': {
            if (static_cast<int>(stack.size()) >= kMaxDepth) {
                corrupt("nesting depth exceeds limit（非法深嵌套）");
            }
            ScanFrame frame;
            frame.isObject = (c == '{');
            stack.push_back(std::move(frame));
            haveLastString = false;
            break;
        }
        case '}':
        case ']': {
            if (stack.empty() || stack.back().isObject != (c == '}')) {
                corrupt(std::string("unbalanced structure: unexpected '") + c + "'");
            }
            stack.pop_back();
            haveLastString = false;
            break;
        }
        case ':': {
            // 键归属判定＋重复键拒绝（boost 坍缩重复键——必须自持；
            // 以解码后文本比对，转义写法的同键同样命中）。
            if (stack.empty() || !stack.back().isObject || !haveLastString) {
                corrupt("object key must be a string（':' 缺键或非对象上下文）");
            }
            const auto inserted = stack.back().keys.insert(lastString);
            if (!inserted.second) {
                corrupt("duplicate object key '" + lastString + "'");
            }
            haveLastString = false;
            break;
        }
        case ',':
        case ' ':  // MUTATION-T54
            break;
        case '-':
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
            scanNumber(p, end, c);  // 数字体内部前进
            continue;               // 已前进，跳过下方单字节步进
        default:
            // 浮点 '.'、指数 'e/E'、true/false/null 字面量、一切裸词——
            // §4.4 契约无布尔/空字段（缺失即可选）、无浮点（文件头规则 2）。
            corrupt(std::string("unexpected character '") + c
                    + "'（接受：对象/数组/字符串/整数）");
        }
        ++p;  // 单字节结构字符统一前进（字符串/数字体已由子函数内部前进）
    }
    if (!stack.empty()) {
        corrupt("truncated input（文档在结构中间结束）");
    }
}

/**
 * @brief 两段式解析：canonical 词法门 → boost::json::parse 构建 DOM。
 *
 * boost 层（parse_options 显式 max_depth=kMaxDepth，其余默认＝标准 JSON
 * 严格面：拒绝注释/尾逗号/裸控制字符/孤立代理/无效 UTF-8——探针实证）
 * 的任何解析失败一律转 store-corrupt（数据侧——磁盘内容损坏/非 canonical
 * 统一归类，§5.0 错误二分）；不透传 boost 异常类型（错误契约稳定为
 * StoreError，码语义不变）。
 *
 * @param text [in] 磁盘字节（整个文档）
 * @return 顶层 DOM 值（对象性检查由各 parse 入口自持——顶层非对象的诊断
 *         带类型名，与被删旧引擎 parseDocument＋入口判定同语义）
 */
bj::value parseCanonicalJson(std::string_view text)
{
    scanCanonical(text);
    bj::parse_options options;
    options.max_depth = static_cast<unsigned>(kMaxDepth);
    try {
        return bj::parse(text, bj::storage_ptr(), options);
    } catch (const boost::system::system_error& e) {
        corrupt(std::string("boost-json structure rejected: ") + e.code().message()
                + "（结构层拒绝——截断/拼接/非法结构）");
    } catch (const std::bad_alloc&) {
        corrupt("boost-json allocation failed（文档超预算拒绝）");
    }
}

// =====================================================================
// dump 侧字符串转义与 UTF-8 严格解码（§4.8 纯 ASCII 输出——Boost.JSON
// 序列化器输出原生 UTF-8 不满足字节面契约，故本侧自持不换，REUSE-T54）
// =====================================================================

/// 在 s[i] 处解码一个 UTF-8 序列（标准最短形式；代理码点/超 U+10FFFF/超长
/// 编码均拒绝——与 scanUnicodeEscape 的 UTF-8 合成互逆）。
///
/// @param s [in] UTF-8 字节串
/// @param i [in] 起始字节下标
/// @return {码点, 消耗字节数}；非法返回 nullopt
std::optional<std::pair<char32_t, std::size_t>> decodeUtf8At(const std::string& s,
                                                             std::size_t i)
{
    const auto byte = [&](std::size_t k) -> std::optional<unsigned> {
        if (k >= s.size()) { return std::nullopt; }
        return static_cast<unsigned>(static_cast<unsigned char>(s[k]));
    };
    const unsigned b0 = *byte(i);
    if (b0 < 0x80) { return std::make_pair(static_cast<char32_t>(b0), std::size_t{1}); }
    auto cont = [&](std::size_t k) -> std::optional<unsigned> {
        const auto b = byte(k);
        if (!b || (*b & 0xC0) != 0x80) { return std::nullopt; }
        return *b & 0x3F;
    };
    if ((b0 & 0xE0) == 0xC0) {
        const auto c1 = cont(i + 1);
        if (!c1) { return std::nullopt; }
        const char32_t cp = ((b0 & 0x1Fu) << 6) | *c1;
        if (cp <= 0x7F) { return std::nullopt; }  // 超长编码拒绝
        return std::make_pair(cp, std::size_t{2});
    }
    if ((b0 & 0xF0) == 0xE0) {
        const auto c1 = cont(i + 1);
        const auto c2 = cont(i + 2);
        if (!c1 || !c2) { return std::nullopt; }
        const char32_t cp = ((b0 & 0x0Fu) << 12) | (*c1 << 6) | *c2;
        if (cp <= 0x7FF) { return std::nullopt; }              // 超长编码拒绝
        if (cp >= 0xD800 && cp <= 0xDFFF) { return std::nullopt; }  // 代理码点非法
        return std::make_pair(cp, std::size_t{3});
    }
    if ((b0 & 0xF8) == 0xF0) {
        const auto c1 = cont(i + 1);
        const auto c2 = cont(i + 2);
        const auto c3 = cont(i + 3);
        if (!c1 || !c2 || !c3) { return std::nullopt; }
        const char32_t cp = ((b0 & 0x07u) << 18) | (*c1 << 12) | (*c2 << 6) | *c3;
        if (cp <= 0xFFFF || cp > 0x10FFFF) { return std::nullopt; }  // 超长/越界拒绝
        return std::make_pair(cp, std::size_t{4});
    }
    return std::nullopt;
}

/// 非 ASCII 码点 → \uXXXX（BMP）或代理对（追加小写 hex——固定小写＝
/// canonical 确定性的字节面约定）。
void appendUnicodeEscape(std::string& out, char32_t cp)
{
    auto hex4 = [&out](std::uint32_t v) {
        static constexpr char kHex[] = "0123456789abcdef";
        out += "\\u";
        for (int shift = 12; shift >= 0; shift -= 4) {
            out += kHex[(v >> shift) & 0xF];
        }
    };
    if (cp <= 0xFFFF) {
        hex4(static_cast<std::uint32_t>(cp));
        return;
    }
    // BMP 外：UTF-16 代理对（H=D800..DBFF，L=DC00..DFFF）。
    const std::uint32_t v = static_cast<std::uint32_t>(cp) - 0x10000;
    hex4(0xD800 | (v >> 10));
    hex4(0xDC00 | (v & 0x3FF));
}

/**
 * @brief 把 UTF-8 字符串按 canonical 转义表追加到 out（§4.8 转义对称表）。
 *
 * @param out       [out] 输出缓冲（追加）
 * @param utf8      [in] 待转义字符串（须为合法 UTF-8）
 * @param whatField [in] 字段名（错误消息定位用）
 *
 * @throws std::invalid_argument utf8 含无效 UTF-8 字节序列（域 canonical
 *         契约违约——core §6.3；调用方错误 fail-fast，不做静默替换/丢字节）
 */
void appendJsonString(std::string& out, const std::string& utf8, const char* whatField)
{
    out += '"';
    for (std::size_t i = 0; i < utf8.size();) {
        const unsigned char c = static_cast<unsigned char>(utf8[i]);
        switch (c) {
        case '"':  out += "\\\""; ++i; continue;
        case '\\': out += "\\\\"; ++i; continue;
        case '\b': out += "\\b";  ++i; continue;
        case '\f': out += "\\f";  ++i; continue;
        case '\n': out += "\\n";  ++i; continue;
        case '\r': out += "\\r";  ++i; continue;
        case '\t': out += "\\t";  ++i; continue;
        default: break;
        }
        if (c < 0x20) {
            // 其余控制字符 → \u00xx（小写 hex）。
            static constexpr char kHex[] = "0123456789abcdef";
            out += "\\u00";
            out += kHex[(c >> 4) & 0xF];
            out += kHex[c & 0xF];
            ++i;
            continue;
        }
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));  // 0x20..0x7F 原样
            ++i;
            continue;
        }
        // 非 ASCII：严格 UTF-8 解码 → \uXXXX/代理对。无效序列＝域负载违反
        // canonical 契约（core §6.3"UTF-8"）——fail-fast，不猜测。
        const auto decoded = decodeUtf8At(utf8, i);
        if (!decoded) {
            throw std::invalid_argument(std::string("project/codec: 字段 ")
                                        + whatField
                                        + " 含无效 UTF-8 字节序列（域 canonical"
                                          " 契约——core.md §6.3；拒绝编码而非静默替换）");
        }
        appendUnicodeEscape(out, decoded->first);
        i += decoded->second;
    }
    out += '"';
}

// =====================================================================
// dump 复用片段（身份/摘要字段均为 core 规范文本或 64hex——ASCII 安全直出）
// =====================================================================

/// 字节序列的 hex 文本编码（UI-T39——payloadCanonical 持久化编码：域负载
/// 是不透明任意字节〔§6.4；CommandRecord 载体＝std::string 字节串〕，JSON
/// 承载取小写 hex——每字节两字符，往返确定性与 core 摘要 hex 同一口径）。
void appendHexString(std::string& out, const std::string& bytes)
{
    static constexpr char kHex[] = "0123456789abcdef";
    out += '"';
    for (const char raw : bytes) {
        const unsigned char b = static_cast<unsigned char>(raw);
        out += kHex[(b >> 4) & 0xF];
        out += kHex[b & 0xF];
    }
    out += '"';
}

/// hex 文本解码回字节序列（parse 对称半区——奇数长度/非 hex 字符＝
/// store-corrupt，不猜测；域负载透传通道，hex→bytes 语义不换）。
std::string decodeHexString(const std::string& hex)
{
    static constexpr int kInvalid = -1;
    const auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') { return c - '0'; }
        if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
        return kInvalid;
    };
    if (hex.size() % 2 != 0) {
        corrupt("hex-encoded byte field has odd length");
    }
    std::string out;
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        const int hi = nibble(hex[i]);
        const int lo = nibble(hex[i + 1]);
        if (hi == kInvalid || lo == kInvalid) {
            corrupt("hex-encoded byte field has non-hex character");
        }
        out.push_back(static_cast<char>((hi << 4) | lo));
    }
    return out;
}

/// 身份字段规范化文本（ProjectId → "prj-…"）。
std::string canonical(ProjectId id) { return id.toCanonical(); }

/// 身份字段规范化文本（BranchId → "brn-…"）。
std::string canonical(BranchId id) { return id.toCanonical(); }

/// 身份字段规范化文本（RevisionId → "rev-…"）。
std::string canonical(RevisionId id) { return id.toCanonical(); }

/// 身份字段规范化文本（RunId → "run-…"；PRJ-T14 增量——RunManifest
/// taskIdentity 的 run 字段编码。RunId 不在 PersistenceFormat 的 using
/// 清单内（该头零消费者时未引入），此处以全限定名声明）。
std::string canonical(sdurws::ird::core::RunId id) { return id.toCanonical(); }

/// 身份字段规范化文本（ObjectId → "obj-…"）。
std::string canonical(ObjectId id) { return id.toCanonical(); }

/// 身份字段规范化文本（ContentVersion → "cv-…"）。
std::string canonical(ContentVersion cv) { return cv.toCanonical(); }

/// {objectId, contentVersion} 引用对 → 内嵌 JSON 对象（固定两键序）。
void appendRefPair(std::string& out, const ObjectRefPair& ref)
{
    out += "{\"objectId\":\"";
    out += canonical(ref.objectId);
    out += "\",\"contentVersion\":\"";
    out += canonical(ref.contentVersion);
    out += "\"}";
}

// =====================================================================
// parse 复用片段：字段提取（类型不符/缺失/越界/格式非法统一 corrupt；
// 载体＝boost::json DOM——语义与被删旧引擎逐字段一致，REUSE-T54）
// =====================================================================

/// 取必填对象成员（缺失＝截断/写缺——store-corrupt 数据侧）。
const bj::value& requireMember(const bj::value& obj, const char* key)
{
    if (obj.is_object()) {
        const auto& members = obj.get_object();
        if (const auto it = members.find(key); it != members.end()) { return it->value(); }
    }
    corrupt(std::string("missing required field '") + key + "'");
}

/// 必填字符串字段（类型须为 JSON 字符串；嵌入 NUL 经 size 保留——\u0000
/// 往返保真，探针实证 boost 与被删旧引擎同语义）。
std::string requireStringField(const bj::value& obj, const char* key)
{
    const bj::value& v = requireMember(obj, key);
    if (!v.is_string()) {
        corrupt(std::string("field '") + key + "' must be a string");
    }
    const auto& s = v.get_string();
    return std::string(s.c_str(), s.size());
}

/// 可选 core 身份字段的探针：字段是否存在（存在性检查先于类型检查）。
bool hasMember(const bj::value& obj, const char* key)
{
    return obj.is_object() && obj.get_object().find(key) != obj.get_object().end();
}

/**
 * @brief 必填整数字段的公共检查：JSON 整数（int64/uint64 kind）且落在
 *        [lo, hi]（hi 以 uint64 承接 2^64-1；负数侧以 int64 下界承接）。
 *
 * 负数侧约束完全由 n >= lo 表达——与被删旧引擎"绝对值 magLimit = -lo
 * （INT64_MIN 特例 2^63）"口径等价（magLimit = -lo ⇔ n ≥ lo，含下界特例）；
 * 越出整数表示的输入（2^64/负 int64 下溢）被 boost 解析为 double kind
 * （探针实证）——同样拒绝（"must be an integer"），与被删旧引擎的
 * "越界拒绝"同码不同文案——错误分类（StoreCorrupt）不变。
 */
std::int64_t integerInRange(const bj::value& obj, const char* key, std::int64_t lo,
                            std::uint64_t hi)
{
    const bj::value& v = requireMember(obj, key);
    if (v.is_int64()) {
        const std::int64_t n = v.get_int64();
        if (n < lo) {
            // 负数侧约束（与被删旧引擎"绝对值 magLimit = -lo"口径等价——
            // magLimit = -lo ⇔ n ≥ lo；n ≥ 0 且 lo < 0 的负值不可能小于 lo）。
            corrupt(std::string("field '") + key + "' out of range（越界拒绝）");
        }
        if (n >= 0 && static_cast<std::uint64_t>(n) > hi) {
            // 正数侧上界（boost 对 ≤INT64_MAX 的整数取 int64 kind——探针
            // 实证——2^31 等正越界值走此路径，与旧 magnitude>hi 检查同型）。
            corrupt(std::string("field '") + key + "' exceeds range（越界拒绝）");
        }
        return n;
    }
    if (v.is_uint64()) {
        const std::uint64_t n = v.get_uint64();
        if (n > hi) {
            corrupt(std::string("field '") + key + "' exceeds range（越界拒绝）");
        }
        // 经 int64 中转与被删旧引擎同型（hi ≤ 2^64-1 时中转有损但
        // requireUint64Field 原样转回，往返恒等）。
        return static_cast<std::int64_t>(n);
    }
    corrupt(std::string("field '") + key + "' must be an integer");
}

/// 必填 int（int32 范围；schemaVersion 用）。
int requireIntField(const bj::value& obj, const char* key)
{
    return static_cast<int>(integerInRange(obj, key, std::numeric_limits<int>::min(),
                                           static_cast<std::uint64_t>(
                                               std::numeric_limits<int>::max())));
}

/// 必填 uint64（revisionSeq/sizeBytes 用；负值/超 2^64-1 拒绝）。
std::uint64_t requireUint64Field(const bj::value& obj, const char* key)
{
    return static_cast<std::uint64_t>(integerInRange(
        obj, key, 0, std::numeric_limits<std::uint64_t>::max()));
}

/// 必填 uint32（payloadFormatVersion 用）。
std::uint32_t requireUint32Field(const bj::value& obj, const char* key)
{
    return static_cast<std::uint32_t>(integerInRange(
        obj, key, 0, static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())));
}

/// 必填对象字段（类型须为 JSON 对象）。
const bj::value& requireObjectField(const bj::value& obj, const char* key)
{
    const bj::value& v = requireMember(obj, key);
    if (!v.is_object()) {
        corrupt(std::string("field '") + key + "' must be an object");
    }
    return v;
}

/// 必填数组字段（类型须为 JSON 数组）。
const bj::value& requireArrayField(const bj::value& obj, const char* key)
{
    const bj::value& v = requireMember(obj, key);
    if (!v.is_array()) {
        corrupt(std::string("field '") + key + "' must be an array");
    }
    return v;
}

/// token 类字段（objectTypeToken/moduleId/externalRefId/state/createdWith-
/// ToolVersion 等）的落值口径：非空、可打印 ASCII（空格 0x20 亦拒——纯
/// token 语义）、≤128 字符。落值依据（DTB §5.4 实现口径登记）：§4.4 各表
/// 仅对 commandType 冻结语法，其余 token 字段无语法冻结——本口径防止失控
/// 字段（超长/控制字符）入库，不收紧语义（词表校验归登记域：模块注册表
/// PRJ-T12、io 状态机阶段 B）。**不适用于路径类字段**（absolutePath 含
/// 空格/非 ASCII 完全合法——用 requirePathField）。
std::string requireTokenField(const bj::value& obj, const char* key)
{
    const std::string s = requireStringField(obj, key);
    if (s.empty()) { corrupt(std::string("field '") + key + "' must not be empty"); }
    if (s.size() > 128) {
        corrupt(std::string("field '") + key + "' exceeds 128 characters");
    }
    for (const char c : s) {
        const auto u = static_cast<unsigned char>(c);
        if (u <= 0x20 || u >= 0x7F) {
            corrupt(std::string("field '") + key
                    + "' must be printable ASCII token（非打印字符拒绝）");
        }
    }
    return s;
}

/// 路径类字段（externalRefs[].absolutePath）的落值口径：非空、无控制字符
/// （C0/C1——转义注入的换行/NUL 破坏清单文件的行结构，且无合法路径含控制
/// 字符）、≤4096 字节（长路径预算；与 io SafePath 的最终校验分工——§5.7
/// 责任划分：解析/存在性/安全归 io，本层只挡结构级失控）。允许空格与
/// 非 ASCII（Windows 长路径/中文路径均为合法登记对象——UTF-8 经 \uXXXX
/// 转义承载，§4.8）。
std::string requirePathField(const bj::value& obj, const char* key)
{
    const std::string s = requireStringField(obj, key);
    if (s.empty()) { corrupt(std::string("field '") + key + "' must not be empty"); }
    if (s.size() > 4096) {
        corrupt(std::string("field '") + key + "' exceeds 4096 bytes");
    }
    for (const char c : s) {
        const auto u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7F) {
            corrupt(std::string("field '") + key
                    + "' must not contain control characters");
        }
    }
    return s;
}

/// 摘要文本字段：64 个小写十六进制字符（manifestDigest/digest256/
/// contentHash256/findingDigest/commandDigest——§4.3/§4.4 摘要列口径；
/// 大写拒绝＝core tryParseDigest 同纪律）。透传不重算（CR-02）。
std::string requireHex64Field(const bj::value& obj, const char* key)
{
    const std::string s = requireStringField(obj, key);
    if (s.size() != 64) {
        corrupt(std::string("field '") + key + "' must be 64 hex chars");
    }
    for (const char c : s) {
        const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        if (!ok) {
            corrupt(std::string("field '") + key
                    + "' must be lowercase hex（64 位小写十六进制）");
        }
    }
    return s;
}

/// core 规范文本身份字段的通用提取：解析失败＝格式非法（store-corrupt——
/// 数据侧；§4.4 约定"身份字段一律 core 规范文本"，tag 错/长度错/大写 hex
/// 均拒绝；身份事实单源 core——语义零变化，REUSE-T54 红线）。
template <typename CoreId>
CoreId requireIdField(const bj::value& obj, const char* key)
{
    const std::string s = requireStringField(obj, key);
    auto parsed = CoreId::tryFromCanonical(s);
    if (!parsed.has_value()) {
        corrupt(std::string("field '") + key + "' is not a canonical id: " + s);
    }
    return *parsed;
}

/// 可选 core 身份字段（缺省 nullopt；出现则须合法规范文本）。
template <typename CoreId>
std::optional<CoreId> optionalIdField(const bj::value& obj, const char* key)
{
    if (!hasMember(obj, key)) { return std::nullopt; }
    return requireIdField<CoreId>(obj, key);
}

/// 未知字段处理的前向声明（定义见下方"未知字段处理"块——parseRefPair
/// 复用它做嵌套对象的键集校验）。
void rejectUnknownFields(const bj::value& obj,
                         const std::initializer_list<const char*>& known, bool tolerate);

/// {objectId, contentVersion} 引用对（metadataRef/supersedes；固定两键）。
/// tolerate 语义与 rejectUnknownFields 一致（上级次版本文档的嵌套对象同样
/// 可能追加可选字段——嵌套容忍与顶层一致，§4.8）。
ObjectRefPair parseRefPair(const bj::value& obj, bool tolerate)
{
    ObjectRefPair ref;
    ref.objectId = requireIdField<ObjectId>(obj, "objectId");
    ref.contentVersion = requireIdField<ContentVersion>(obj, "contentVersion");
    rejectUnknownFields(obj, {"objectId", "contentVersion"}, tolerate);
    return ref;
}

/// 可选引用对字段。
std::optional<ObjectRefPair> optionalRefPairField(const bj::value& obj, const char* key,
                                                  bool tolerate)
{
    if (!hasMember(obj, key)) { return std::nullopt; }
    return parseRefPair(requireObjectField(obj, key), tolerate);
}

/// 时间戳类字段（ISO-8601 UTC 文本，§4.4 约定）：非空可打印 ASCII（语法
/// 不校验——透传，见 requireTokenField 口径注释）。
std::string requireTimestampField(const bj::value& obj, const char* key)
{
    return requireTokenField(obj, key);
}

/// 未知字段处理（§4.8 次版本兼容语义的落点）：tolerate=true（上级次版本
/// 文档）跳过；否则拒绝（当前版本的合法文档不含未知字段——出现即损坏或
/// 写入器 bug）。boost 对象为插入序向量——成员序即文档序，与被删旧引擎
/// 的保序成员遍历同型。
void rejectUnknownFields(const bj::value& obj,
                         const std::initializer_list<const char*>& known, bool tolerate)
{
    if (!obj.is_object()) { return; }
    for (const auto& m : obj.get_object()) {
        const std::string_view key(m.key_c_str(), m.key().size());
        bool isKnown = false;
        for (const char* k : known) {
            if (key == k) { isKnown = true; break; }
        }
        if (!isKnown && !tolerate) {
            corrupt(std::string("unknown field '") + std::string(key)
                    + "'（当前版本不接受；上级次版本追加字段须以更高次版本号声明——§4.8）");
        }
        // tolerate：静默跳过——已知字段语义不变，未知可选字段不参与
        // round-trip（降级读仅用于只读展示，写路径永远写当前版本——
        // Codec.hpp 头注纪律）。
    }
}

/// 带版本判定的公共前奏：顶层对象检查 → schemaVersion 判定 → formatId
/// 检查（有 formatId 的类型）→ 返回容忍标志。版本判定先于其余字段：未知
/// schemaVersion 的拒绝优先于一切结构错误（§8.11——版本不符的文档不做
/// 内容解读）。
bool parsePreambleVersioned(const bj::value& root, const char* typeName, bool withFormatId)
{
    if (!root.is_object()) {
        corrupt(std::string("top-level value must be an object (") + typeName + ")");
    }
    const int docVersion = requireIntField(root, "schemaVersion");
    const bool tolerate = checkSchemaVersion(docVersion, typeName);
    if (withFormatId) { checkFormatId(requireStringField(root, "formatId")); }
    return tolerate;
}

}  // namespace

// =====================================================================
// 公共入口实现（契约见 Codec.hpp；namespace sdurws::ird::project::codec）
// =====================================================================

// ---- dump：六类型，字段序＝§4.4 表列序 ----

std::string dump(const ProjectStaticIdentity& value)
{
    // §4.2 左列序：formatId, schemaVersion, projectId, createdAtUtc,
    // createdWithToolVersion。token 类字段经 appendJsonString 转义兜底。
    std::string out;
    out += "{\"formatId\":";
    appendJsonString(out, value.formatId, "formatId");
    out += ",\"schemaVersion\":" + std::to_string(value.schemaVersion);
    out += ",\"projectId\":\"" + canonical(value.projectId) + "\"";
    out += ",\"createdAtUtc\":";
    appendJsonString(out, value.createdAtUtc, "createdAtUtc");
    out += ",\"createdWithToolVersion\":";
    appendJsonString(out, value.createdWithToolVersion, "createdWithToolVersion");
    out += "}";
    return out;
}

std::string dump(const HeadRecord& value)
{
    // §4.4.1 表列序：formatId, schemaVersion, projectId, revisionId,
    // revisionSeq, branchId, manifestDigest。
    std::string out;
    out += "{\"formatId\":";
    appendJsonString(out, value.formatId, "formatId");
    out += ",\"schemaVersion\":" + std::to_string(value.schemaVersion);
    out += ",\"projectId\":\"" + canonical(value.projectId) + "\"";
    out += ",\"revisionId\":\"" + canonical(value.revisionId) + "\"";
    out += ",\"revisionSeq\":" + std::to_string(value.revisionSeq);
    out += ",\"branchId\":\"" + canonical(value.branchId) + "\"";
    out += ",\"manifestDigest\":";
    appendJsonString(out, value.manifestDigest, "manifestDigest");
    out += "}";
    return out;
}

std::string dump(const RevisionManifest& value)
{
    // §4.4.2 表列序：revisionId, revisionSeq, parentRevisionId?, branchId,
    // committedAtUtc, metadataRef, objectRefs[], introducedObjects?。
    // 可选空值省略：parentRevisionId 无＝不输出；introducedObjects 空列表
    // ＝不输出（canonical 省略规则，与 parse 侧缺省空值对称——round-trip）。
    std::string out;
    out += "{\"revisionId\":\"" + canonical(value.revisionId) + "\"";
    out += ",\"revisionSeq\":" + std::to_string(value.revisionSeq);
    if (value.parentRevisionId.has_value()) {
        out += ",\"parentRevisionId\":\"" + canonical(*value.parentRevisionId) + "\"";
    }
    out += ",\"branchId\":\"" + canonical(value.branchId) + "\"";
    out += ",\"committedAtUtc\":";
    appendJsonString(out, value.committedAtUtc, "committedAtUtc");
    out += ",\"metadataRef\":";
    appendRefPair(out, value.metadataRef);
    out += ",\"objectRefs\":[";
    for (std::size_t i = 0; i < value.objectRefs.size(); ++i) {
        if (i > 0) { out += ','; }
        const ObjectRef& r = value.objectRefs[i];
        out += "{\"objectId\":\"" + canonical(r.objectId) + "\",\"contentVersion\":\""
             + canonical(r.contentVersion) + "\",\"objectTypeToken\":";
        appendJsonString(out, r.objectTypeToken, "objectTypeToken");
        out += ",\"digest256\":";
        appendJsonString(out, r.digest256, "digest256");
        out += '}';
    }
    out += ']';
    if (!value.introducedObjects.empty()) {
        out += ",\"introducedObjects\":[";
        for (std::size_t i = 0; i < value.introducedObjects.size(); ++i) {
            if (i > 0) { out += ','; }
            out += '"' + canonical(value.introducedObjects[i]) + '"';
        }
        out += ']';
    }
    out += '}';
    return out;
}

std::string dump(const ProjectMetadataRecord& value)
{
    // §4.4.3 表列序：schemaVersion, committedBy, supersedes?,
    // projectDisplayName, primaryBranchId, branches[], schemeLabels?。
    std::string out;
    out += "{\"schemaVersion\":" + std::to_string(value.schemaVersion);
    out += ",\"committedBy\":\"" + canonical(value.committedBy) + "\"";
    if (value.supersedes.has_value()) {
        out += ",\"supersedes\":";
        appendRefPair(out, *value.supersedes);
    }
    out += ",\"projectDisplayName\":";
    appendJsonString(out, value.projectDisplayName, "projectDisplayName");  // UTF-8 → \uXXXX
    out += ",\"primaryBranchId\":\"" + canonical(value.primaryBranchId) + "\"";
    out += ",\"branches\":[";
    for (std::size_t i = 0; i < value.branches.size(); ++i) {
        if (i > 0) { out += ','; }
        const BranchRecord& b = value.branches[i];
        out += "{\"branchId\":\"" + canonical(b.branchId) + "\",\"label\":";
        appendJsonString(out, b.label, "label");
        out += ",\"baseRevisionId\":\"" + canonical(b.baseRevisionId) + "\"";
        out += ",\"tipRevisionId\":\"" + canonical(b.tipRevisionId) + "\"";
        out += ",\"createdAtUtc\":";
        appendJsonString(out, b.createdAtUtc, "createdAtUtc");
        out += '}';
    }
    out += ']';
    if (!value.schemeLabels.empty()) {
        // std::map 按键字典序迭代——canonical 键序（确定性，NFR-COR-02）。
        out += ",\"schemeLabels\":{";
        bool first = true;
        for (const auto& kv : value.schemeLabels) {
            if (!first) { out += ','; }
            first = false;
            appendJsonString(out, kv.first, "schemeLabels key");
            out += ':';
            appendJsonString(out, kv.second, "schemeLabels value");
        }
        out += '}';
    }
    out += '}';
    return out;
}

std::string dump(const CommandRecord& value)
{
    // §4.4.4 表列序：commandType, payloadFormatVersion, payloadCanonical,
    // inverse?, confirmations?, summary。
    // UI-T39：payloadCanonical 的持久化编码由"UTF-8 字符串直写"改为
    // **hex 文本**——payload 契约是"域 canonical 负载字节（不透明；project
    // 原样存储——§6.4）"，域负载是任意字节（requirements 的紧凑二进制
    // 载荷含 magic/长度前缀与 double 原始字节，必然含非 UTF-8 序列）；
    // 旧实现经 appendJsonString 的严格 UTF-8 校验即拒绝合法域负载（生产
    // 装配注册处理器后的首次真实提交实证）。hex 编码字节安全且往返确定
    // （同字节→同文本——NFR-COR-02）；磁盘兼容负担为零：生产路径此前
    // 从未有域 payload 落盘（处理器未注册＝提交恒 unknown-command 拒绝）。
    std::string out;
    out += "{\"commandType\":";
    appendJsonString(out, value.commandType, "commandType");
    out += ",\"payloadFormatVersion\":" + std::to_string(value.payloadFormatVersion);
    out += ",\"payloadCanonical\":";
    appendHexString(out, value.payloadCanonical);  // 域负载透传（hex 编码——字节安全）
    if (value.inverse.has_value()) {
        out += ",\"inverse\":{\"commandType\":";
        appendJsonString(out, value.inverse->commandType, "inverse.commandType");
        out += ",\"payloadFormatVersion\":"
             + std::to_string(value.inverse->payloadFormatVersion);
        out += ",\"payloadCanonical\":";
        appendHexString(out, value.inverse->payloadCanonical);
        out += '}';
    }
    if (!value.confirmations.empty()) {
        out += ",\"confirmations\":[";
        for (std::size_t i = 0; i < value.confirmations.size(); ++i) {
            if (i > 0) { out += ','; }
            const ConfirmationRecord& c = value.confirmations[i];
            out += "{\"findingDigest\":";
            appendJsonString(out, c.findingDigest, "findingDigest");
            out += ",\"policyContentId\":";
            appendJsonString(out, c.policyContentId, "policyContentId");
            out += ",\"commandDigest\":";
            appendJsonString(out, c.commandDigest, "commandDigest");
            out += ",\"baseRevisionId\":";
            appendJsonString(out, c.baseRevisionId, "baseRevisionId");
            out += ",\"credential\":{\"principal\":";
            appendJsonString(out, c.credential.principal, "credential.principal");
            out += ",\"confirmedAtUtc\":";
            appendJsonString(out, c.credential.confirmedAtUtc, "credential.confirmedAtUtc");
            out += "}}";
        }
        out += ']';
    }
    out += ",\"summary\":";
    appendJsonString(out, value.summary, "summary");  // 人读摘要：UTF-8 → \uXXXX
    out += '}';
    return out;
}

std::string dump(const DraftDocument& value)
{
    // §4.4.5 表列序：schemaVersion, projectId/branchId/moduleId,
    // baseRevisionId, payload, externalRefs?, savedAtUtc, origin。
    std::string out;
    out += "{\"schemaVersion\":" + std::to_string(value.schemaVersion);
    out += ",\"projectId\":\"" + canonical(value.projectId) + "\"";
    out += ",\"branchId\":\"" + canonical(value.branchId) + "\"";
    out += ",\"moduleId\":";
    appendJsonString(out, value.moduleId, "moduleId");
    out += ",\"baseRevisionId\":\"" + canonical(value.baseRevisionId) + "\"";
    out += ",\"payload\":";
    appendJsonString(out, value.payload, "payload");  // 域负载逐字节转义透传（不解释）
    if (!value.externalRefs.empty()) {
        out += ",\"externalRefs\":[";
        for (std::size_t i = 0; i < value.externalRefs.size(); ++i) {
            if (i > 0) { out += ','; }
            const ExternalRefRecord& r = value.externalRefs[i];
            out += "{\"externalRefId\":";
            appendJsonString(out, r.externalRefId, "externalRefId");
            out += ",\"absolutePath\":";
            appendJsonString(out, r.absolutePath, "absolutePath");
            out += ",\"contentHash256\":";
            appendJsonString(out, r.contentHash256, "contentHash256");
            out += ",\"sizeBytes\":" + std::to_string(r.sizeBytes);
            out += ",\"recordedAtUtc\":";
            appendJsonString(out, r.recordedAtUtc, "recordedAtUtc");
            out += ",\"state\":";
            appendJsonString(out, r.state, "state");
            out += '}';
        }
        out += ']';
    }
    out += ",\"savedAtUtc\":";
    appendJsonString(out, value.savedAtUtc, "savedAtUtc");
    out += ",\"origin\":\"" + std::string(toToken(value.origin)) + "\"";
    out += '}';
    return out;
}

std::string dump(const RunManifest& value)
{
    // §4.4.7 表列序：taskIdentity（project/branch/revision/run/attempt
    // ——core TaskIdentity 声明序）、items[]、runKind、evaluationKey、
    // finalizedAtUtc、manifestDigest。manifestDigest 透传（CR-02——
    // 归档端口调用前自行计算回填，见 Codec.hpp 本函数契约）。attempt
    // 的规范文本＝"att-<十进制>"（core AttemptId::toCanonical）。
    std::string out;
    out += "{\"taskIdentity\":{\"project\":\"" + canonical(value.taskIdentity.project)
           + "\",\"branch\":\"" + canonical(value.taskIdentity.branch)
           + "\",\"revision\":\"" + canonical(value.taskIdentity.revision)
           + "\",\"run\":\"" + canonical(value.taskIdentity.run)
           + "\",\"attempt\":\"" + value.taskIdentity.attempt.toCanonical()
           + "\"}";
    out += ",\"items\":[";
    for (std::size_t i = 0; i < value.items.size(); ++i) {
        if (i > 0) { out += ','; }
        const RunManifestItem& r = value.items[i];
        out += "{\"relPath\":";
        appendJsonString(out, r.relPath, "relPath");
        out += ",\"sha256\":";
        appendJsonString(out, r.sha256, "sha256");
        out += ",\"sizeBytes\":" + std::to_string(r.sizeBytes);
        out += '}';
    }
    out += ']';
    out += ",\"runKind\":";
    appendJsonString(out, value.runKind, "runKind");
    out += ",\"evaluationKey\":";
    appendJsonString(out, value.evaluationKey, "evaluationKey");
    out += ",\"finalizedAtUtc\":";
    appendJsonString(out, value.finalizedAtUtc, "finalizedAtUtc");
    out += ",\"manifestDigest\":";
    appendJsonString(out, value.manifestDigest, "manifestDigest");
    out += '}';
    return out;
}

// ---- parse：六类型，严格拒绝＋版本判定（两段式：scanCanonical 词法门
// ＋boost DOM；语义提取与被删旧引擎逐字段一致）----

ProjectStaticIdentity parseStaticIdentity(std::string_view text)
{
    const bj::value root = parseCanonicalJson(text);
    const bool tolerate = parsePreambleVersioned(root, "project-static-identity", true);

    ProjectStaticIdentity v;
    v.schemaVersion = requireIntField(root, "schemaVersion");
    v.projectId = requireIdField<ProjectId>(root, "projectId");
    v.createdAtUtc = requireTimestampField(root, "createdAtUtc");
    v.createdWithToolVersion = requireTokenField(root, "createdWithToolVersion");
    rejectUnknownFields(root,
                        {"formatId", "schemaVersion", "projectId", "createdAtUtc",
                         "createdWithToolVersion"},
                        tolerate);
    return v;
}

HeadRecord parseHeadRecord(std::string_view text)
{
    const bj::value root = parseCanonicalJson(text);
    const bool tolerate = parsePreambleVersioned(root, "head-record", true);

    HeadRecord v;
    v.schemaVersion = requireIntField(root, "schemaVersion");
    v.projectId = requireIdField<ProjectId>(root, "projectId");
    v.revisionId = requireIdField<RevisionId>(root, "revisionId");
    v.revisionSeq = requireUint64Field(root, "revisionSeq");
    v.branchId = requireIdField<BranchId>(root, "branchId");
    v.manifestDigest = requireHex64Field(root, "manifestDigest");
    rejectUnknownFields(root,
                        {"formatId", "schemaVersion", "projectId", "revisionId",
                         "revisionSeq", "branchId", "manifestDigest"},
                        tolerate);
    return v;
}

RevisionManifest parseRevisionManifest(std::string_view text)
{
    const bj::value root = parseCanonicalJson(text);
    // 无 schemaVersion/formatId 字段（§4.4.2 表）——版本语义由容器判定
    // （PersistenceFormat.hpp 注释），按当前支持版本严格解析。
    if (!root.is_object()) {
        corrupt("top-level value must be an object (revision-manifest)");
    }

    RevisionManifest v;
    v.revisionId = requireIdField<RevisionId>(root, "revisionId");
    v.revisionSeq = requireUint64Field(root, "revisionSeq");
    v.parentRevisionId = optionalIdField<RevisionId>(root, "parentRevisionId");
    v.branchId = requireIdField<BranchId>(root, "branchId");
    v.committedAtUtc = requireTimestampField(root, "committedAtUtc");
    v.metadataRef = parseRefPair(requireObjectField(root, "metadataRef"), false);
    const bj::value& refs = requireArrayField(root, "objectRefs");
    // §4.4.2 约束：objectRefs ≥1（完整对象引用集至少含元数据对象本身）。
    if (refs.get_array().empty()) { corrupt("objectRefs must not be empty（§4.4.2 ≥1）"); }
    for (const bj::value& item : refs.get_array()) {
        if (!item.is_object()) {
            corrupt("objectRefs items must be objects");
        }
        ObjectRef r;
        r.objectId = requireIdField<ObjectId>(item, "objectId");
        r.contentVersion = requireIdField<ContentVersion>(item, "contentVersion");
        r.objectTypeToken = requireTokenField(item, "objectTypeToken");
        r.digest256 = requireHex64Field(item, "digest256");
        rejectUnknownFields(item,
                            {"objectId", "contentVersion", "objectTypeToken", "digest256"},
                            false);
        v.objectRefs.push_back(std::move(r));
    }
    if (hasMember(root, "introducedObjects")) {
        const bj::value& intro = root.get_object().find("introducedObjects")->value();
        if (!intro.is_array()) {
            corrupt("field 'introducedObjects' must be an array");
        }
        for (const bj::value& item : intro.get_array()) {
            if (!item.is_string()) {
                corrupt("introducedObjects items must be strings");
            }
            const auto& s = item.get_string();
            auto cv = ContentVersion::tryFromCanonical(std::string(s.c_str(), s.size()));
            if (!cv.has_value()) {
                corrupt("introducedObjects item is not a canonical cv- text: "
                        + std::string(s.c_str(), s.size()));
            }
            v.introducedObjects.push_back(*cv);
        }
    }
    rejectUnknownFields(root,
                        {"revisionId", "revisionSeq", "parentRevisionId", "branchId",
                         "committedAtUtc", "metadataRef", "objectRefs", "introducedObjects"},
                        false);
    return v;
}

ProjectMetadataRecord parseMetadataRecord(std::string_view text)
{
    const bj::value root = parseCanonicalJson(text);
    const bool tolerate = parsePreambleVersioned(root, "project-metadata-record", false);

    ProjectMetadataRecord v;
    v.schemaVersion = requireIntField(root, "schemaVersion");
    v.committedBy = requireIdField<RevisionId>(root, "committedBy");
    v.supersedes = optionalRefPairField(root, "supersedes", tolerate);
    v.projectDisplayName = requireStringField(root, "projectDisplayName");
    v.primaryBranchId = requireIdField<BranchId>(root, "primaryBranchId");
    const bj::value& branches = requireArrayField(root, "branches");
    // §4.4.3 约束：branches ≥1（至少含主分支；INV-M1 唯一性属发布期校验）。
    if (branches.get_array().empty()) {
        corrupt("branches must not be empty（§4.4.3 ≥1）");
    }
    for (const bj::value& item : branches.get_array()) {
        if (!item.is_object()) {
            corrupt("branches items must be objects");
        }
        BranchRecord b;
        b.branchId = requireIdField<BranchId>(item, "branchId");
        b.label = requireStringField(item, "label");
        b.baseRevisionId = requireIdField<RevisionId>(item, "baseRevisionId");
        b.tipRevisionId = requireIdField<RevisionId>(item, "tipRevisionId");
        b.createdAtUtc = requireTimestampField(item, "createdAtUtc");
        rejectUnknownFields(item,
                            {"branchId", "label", "baseRevisionId", "tipRevisionId",
                             "createdAtUtc"},
                            tolerate);
        v.branches.push_back(std::move(b));
    }
    if (hasMember(root, "schemeLabels")) {
        const bj::value& labels = root.get_object().find("schemeLabels")->value();
        if (!labels.is_object()) {
            corrupt("field 'schemeLabels' must be an object");
        }
        for (const auto& kv : labels.get_object()) {
            if (!kv.value().is_string()) {
                corrupt("schemeLabels values must be strings");
            }
            const auto& s = kv.value().get_string();
            v.schemeLabels.emplace(std::string(kv.key_c_str(), kv.key().size()),
                                   std::string(s.c_str(), s.size()));
        }
    }
    rejectUnknownFields(root,
                        {"schemaVersion", "committedBy", "supersedes", "projectDisplayName",
                         "primaryBranchId", "branches", "schemeLabels"},
                        tolerate);
    return v;
}

CommandRecord parseCommandRecord(std::string_view text)
{
    const bj::value root = parseCanonicalJson(text);
    // 无 schemaVersion/formatId 字段（§4.4.4 表）——同 RevisionManifest，
    // 版本语义由容器（修订/打开协议）判定，按当前支持版本严格解析。
    if (!root.is_object()) {
        corrupt("top-level value must be an object (command-record)");
    }

    CommandRecord v;
    v.commandType = requireTokenField(root, "commandType");
    // §4.4.4 冻结语法 ^[a-z0-9-]{3,64}（处理器注册 token；在非空 ASCII
    // 口径之上收紧到表冻结规则）。
    {
        const std::string& t = v.commandType;
        bool syntaxOk = t.size() >= 3 && t.size() <= 64;
        for (const char c : t) {
            const bool ok =
                (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
            if (!ok) { syntaxOk = false; break; }
        }
        if (!syntaxOk) {
            corrupt("field 'commandType' violates ^[a-z0-9-]{3,64}（§4.4.4 冻结语法）: "
                    + t);
        }
    }
    v.payloadFormatVersion = requireUint32Field(root, "payloadFormatVersion");
    // 域负载透传（不解释）——hex 文本解码回字节（UI-T39：与 dump 的
    // appendHexString 对称；奇长/非 hex 字符＝store-corrupt 不猜测）。
    v.payloadCanonical = decodeHexString(requireStringField(root, "payloadCanonical"));
    if (hasMember(root, "inverse")) {
        const bj::value& inv = root.get_object().find("inverse")->value();
        if (!inv.is_object()) {
            corrupt("field 'inverse' must be an object");
        }
        InverseCommand i;
        i.commandType = requireTokenField(inv, "commandType");
        i.payloadFormatVersion = requireUint32Field(inv, "payloadFormatVersion");
        i.payloadCanonical = decodeHexString(requireStringField(inv, "payloadCanonical"));
        rejectUnknownFields(inv,
                            {"commandType", "payloadFormatVersion", "payloadCanonical"},
                            false);
        v.inverse = std::move(i);
    }
    if (hasMember(root, "confirmations")) {
        const bj::value& confs = root.get_object().find("confirmations")->value();
        if (!confs.is_array()) {
            corrupt("field 'confirmations' must be an array");
        }
        for (const bj::value& item : confs.get_array()) {
            if (!item.is_object()) {
                corrupt("confirmations items must be objects");
            }
            ConfirmationRecord c;
            c.findingDigest = requireHex64Field(item, "findingDigest");
            // policyContentId＝策略内容身份（CON-06）——cid- core 规范文本
            // （§4.4"身份字段一律 core 规范文本"；解析后再规范化——保证
            // canonical 形态不变式）。
            c.policyContentId =
                requireIdField<core::ContentIdentity>(item, "policyContentId")
                    .toCanonical();
            c.commandDigest = requireHex64Field(item, "commandDigest");
            // baseRevisionId＝确认所针对的输入版本——rev- 规范文本（§6.7）。
            c.baseRevisionId =
                requireIdField<RevisionId>(item, "baseRevisionId").toCanonical();
            const bj::value& cred = requireObjectField(item, "credential");
            c.credential.principal = requireStringField(cred, "principal");
            c.credential.confirmedAtUtc = requireTimestampField(cred, "confirmedAtUtc");
            rejectUnknownFields(cred, {"principal", "confirmedAtUtc"}, false);
            rejectUnknownFields(item,
                                {"findingDigest", "policyContentId", "commandDigest",
                                 "baseRevisionId", "credential"},
                                false);
            v.confirmations.push_back(std::move(c));
        }
    }
    v.summary = requireStringField(root, "summary");  // 人读摘要（处理器生成，原样持久化）
    rejectUnknownFields(root,
                        {"commandType", "payloadFormatVersion", "payloadCanonical",
                         "inverse", "confirmations", "summary"},
                        false);
    return v;
}

DraftDocument parseDraftDocument(std::string_view text)
{
    const bj::value root = parseCanonicalJson(text);
    const bool tolerate = parsePreambleVersioned(root, "draft-document", false);

    DraftDocument v;
    v.schemaVersion = requireIntField(root, "schemaVersion");
    v.projectId = requireIdField<ProjectId>(root, "projectId");
    v.branchId = requireIdField<BranchId>(root, "branchId");
    v.moduleId = requireTokenField(root, "moduleId");   // 注册表校验归 PRJ-T12
    v.baseRevisionId = requireIdField<RevisionId>(root, "baseRevisionId");
    v.payload = requireStringField(root, "payload");    // 域负载透传（不解释）
    if (hasMember(root, "externalRefs")) {
        const bj::value& refs = root.get_object().find("externalRefs")->value();
        if (!refs.is_array()) {
            corrupt("field 'externalRefs' must be an array");
        }
        for (const bj::value& item : refs.get_array()) {
            if (!item.is_object()) {
                corrupt("externalRefs items must be objects");
            }
            ExternalRefRecord r;
            r.externalRefId = requireTokenField(item, "externalRefId");
            r.absolutePath = requirePathField(item, "absolutePath");
            r.contentHash256 = requireHex64Field(item, "contentHash256");
            r.sizeBytes = requireUint64Field(item, "sizeBytes");
            r.recordedAtUtc = requireTimestampField(item, "recordedAtUtc");
            r.state = requireTokenField(item, "state");  // 词表归阶段 B io 契约冻结
            rejectUnknownFields(item,
                                {"externalRefId", "absolutePath", "contentHash256",
                                 "sizeBytes", "recordedAtUtc", "state"},
                                tolerate);
            v.externalRefs.push_back(std::move(r));
        }
    }
    v.savedAtUtc = requireTimestampField(root, "savedAtUtc");
    const std::string originToken = requireStringField(root, "origin");
    auto origin = draftOriginFromToken(originToken);
    if (!origin.has_value()) {
        corrupt("field 'origin' is not one of autosave/manual/apply-retained"
                "（§4.4.5 冻结三值）: "
                + originToken);
    }
    v.origin = *origin;
    rejectUnknownFields(root,
                        {"schemaVersion", "projectId", "branchId", "moduleId",
                         "baseRevisionId", "payload", "externalRefs", "savedAtUtc",
                         "origin"},
                        tolerate);
    return v;
}

RunManifest parseRunManifest(std::string_view text)
{
    const bj::value root = parseCanonicalJson(text);
    // 无 schemaVersion/formatId 字段（§4.4.7 表）——同 RevisionManifest/
    // CommandRecord，版本语义由容器判定，按当前支持版本的结构严格解析
    // （未知字段拒绝——tolerate 恒 false）。
    if (!root.is_object()) {
        corrupt("top-level value must be an object (run-manifest)");
    }

    RunManifest v;
    // taskIdentity 五元组（§4.4.7 必填；字段序＝core TaskIdentity 声明序
    // ——project/branch/revision/run/attempt）。身份字段一律 core 规范
    // 文本（§4.4 约定），五个身份逐字段严格解析。
    {
        const bj::value& task = requireObjectField(root, "taskIdentity");
        core::TaskIdentity t;
        t.project = requireIdField<core::ProjectId>(task, "project");
        t.branch = requireIdField<core::BranchId>(task, "branch");
        t.revision = requireIdField<core::RevisionId>(task, "revision");
        t.run = requireIdField<core::RunId>(task, "run");
        // attempt＝"att-<十进制>"（core AttemptId 规范文本；0 与溢出在
        // 解析边界拒绝——core 契约）。
        t.attempt = requireIdField<core::AttemptId>(task, "attempt");
        if (!t.isValid()) {
            corrupt("taskIdentity has invalid member（五元组须全有效）");
        }
        rejectUnknownFields(task, {"project", "branch", "revision", "run", "attempt"},
                            false);
        v.taskIdentity = t;
    }
    // items ≥1（§4.4.7 必填（≥1）——空运行不发布 manifest）。
    const bj::value& items = requireArrayField(root, "items");
    if (items.get_array().empty()) {
        corrupt("items must not be empty（§4.4.7 ≥1）");
    }
    for (const bj::value& item : items.get_array()) {
        if (!item.is_object()) {
            corrupt("items entries must be objects");
        }
        RunManifestItem r;
        r.relPath = requireStringField(item, "relPath");  // 相对路径透传
        r.sha256 = requireHex64Field(item, "sha256");     // 透传不重算（CR-02）
        r.sizeBytes = requireUint64Field(item, "sizeBytes");
        rejectUnknownFields(item, {"relPath", "sha256", "sizeBytes"}, false);
        v.items.push_back(std::move(r));
    }
    v.runKind = requireTokenField(root, "runKind");
    // evaluationKey＝execution 登记透传（评估键；非空 ASCII token 口径，
    // 词表归 execution——本层只做 token 形态校验）。
    v.evaluationKey = requireTokenField(root, "evaluationKey");
    v.finalizedAtUtc = requireTimestampField(root, "finalizedAtUtc");
    v.manifestDigest = requireHex64Field(root, "manifestDigest");  // 幂等判据（§10.1）
    rejectUnknownFields(root,
                        {"taskIdentity", "items", "runKind", "evaluationKey",
                         "finalizedAtUtc", "manifestDigest"},
                        false);
    return v;
}

// ---- CR-02 唯一摘要入口 ----

core::ContentVersion contentVersionOf(std::string_view payloadBytes)
{
    // 唯一哈希路径：core ContentDigester（SHA-256，FIPS 180-2——CORE-T02
    // 经已知向量钉住）。本文件其余任何位置不出现哈希计算（CR-02：编码器
    // 不私设第二哈希路径；digest 类字段全部透传）。
    core::ContentDigester digester;
    digester.update(payloadBytes.data(), payloadBytes.size());  // 空视图合法（nBytes=0）
    core::ContentVersion version;
    version.bytes = digester.finalize();
    return version;
}

}  // namespace sdurws::ird::project::codec

namespace sdurws::ird::project {

// DraftOrigin 的 token 转换：声明位于本命名空间（PersistenceFormat.hpp——
// 磁盘格式契约面），故实现同址；token 值冻结于 §4.4.5 origin 字段。

const char* toToken(DraftOrigin origin) noexcept
{
    switch (origin) {
    case DraftOrigin::Autosave:      return "autosave";
    case DraftOrigin::Manual:        return "manual";
    case DraftOrigin::ApplyRetained: return "apply-retained";
    }
    return "unknown";  // 防御：switch 全覆盖后不可达（Provenance toToken 同款）
}

std::optional<DraftOrigin> draftOriginFromToken(std::string_view token) noexcept
{
    if (token == "autosave") { return DraftOrigin::Autosave; }
    if (token == "manual") { return DraftOrigin::Manual; }
    if (token == "apply-retained") { return DraftOrigin::ApplyRetained; }
    return std::nullopt;
}

}  // namespace sdurws::ird::project
