/**
 * @file   Codec.cpp
 * @brief  project 自有格式 canonical 编解码实现（§4.8）——自含 JSON 引擎。
 *
 * 设计依据：
 *   - units/project.md §4.8（canonical 编码契约：ASCII、固定字段序、无浮点
 *     ——全字符串身份与整数、parse(dump(x))==x；"JsonLite（testkit）不用于
 *     产品格式"——因此本文件自含读写引擎，不引第三方 JSON 库，依赖红线：
 *     第三方一律经 vcpkg 且先在 DTB 登记，本任务无此登记）、§4.4（字段级
 *     契约与表列序）、§8.11（版本判定：旧格式 format-legacy/未来版本
 *     schema-future＋升级指引数据——PM-06/NFR-DEP-04）；
 *   - 任务契约 tasks/foundation/PRJ-T04.json acceptance 1～4（CR-02：摘要
 *     只经 core ContentDigester——本文件除 contentVersionOf 外无任何哈希
 *     计算，digest 类字段全部透传）。
 *
 * 引擎规则（canonical 落值口径，随实现登记——DTB §5.4）：
 *   1. 输出：紧凑无空白、字段序＝各类型 §4.4 表列序、可选字段空值省略
 *      （空列表/空 map/nullopt 不输出）、整数十进制、字符串按下方转义表；
 *      非 ASCII 字符经 \uXXXX（BMP）或 UTF-16 代理对转义——输出恒为纯
 *      ASCII（§4.8）。
 *   2. 输入：严格 canonical——拒绝任何空白（含换行）、拒绝非 ASCII 原始
 *      字节（0x80+ 须以 \uXXXX 转义出现）、拒绝浮点/指数字面量（§4.8 无
 *      浮点）、拒绝 true/false/null（§4.4 契约无布尔/空字段——缺失即可选）、
 *      拒绝重复键、拒绝前导零整数、拒绝截断与尾随内容。带空白或非
 *      canonical 形态的字节流＝损坏/外部篡改产物，按 store-corrupt 拒绝
 *      （PM-02 读校验精神：宁可拒绝也不猜测）。
 *   3. 转义表（对称）：\" \\ \b \f \n \r \t；其余 <0x20 → \u00xx；0x20～
 *      0x7E 可打印 ASCII 原样；≥0x80 → UTF-8 解码 → \uXXXX/代理对。解析
 *      端对称还原（\uXXXX → UTF-8 字节），保证 parse(dump(x))==x 逐字节
 *      保真（含 4 字节 BMP 外字符与 \u0000）。
 *   4. 非法 UTF-8：dump 侧拒绝（std::invalid_argument——域 canonical 契约
 *      归 core §6.3，调用方错误 fail-fast）；parse 侧因输入限 ASCII＋转义
 *      而天然只能产生合法 UTF-8（孤立代理拒绝）。
 *
 * 线程安全：全部函数纯函数（无共享可变状态）。
 */

#include "Codec.hpp"

#include <sdurws/ird/project/StoreTypes.hpp>

#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

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
// JSON 值（轻量 DOM，仅自有格式所需四种节点）
// =====================================================================

/// JSON 节点：字符串/整数/对象/数组。无浮点与字面量（true/false/null 在
/// 词法层即拒绝——§4.8 契约无此类字段）。
struct JsonValue {
    enum class Kind { String, Integer, Object, Array };

    Kind kind = Kind::String;
    std::string str;                                        ///< String 载荷（UTF-8）
    bool negative = false;                                  ///< Integer 符号
    std::uint64_t magnitude = 0;                            ///< Integer 绝对值
    std::vector<std::pair<std::string, JsonValue>> members; ///< Object 成员（保序、无重复）
    std::vector<JsonValue> items;                           ///< Array 元素（保序）
};

/// 按名查找对象成员（重复键已在解析层拒绝——每名至多一个成员）。
const JsonValue* findMember(const JsonValue& obj, std::string_view key)
{
    for (const auto& m : obj.members) {
        if (m.first == key) { return &m.second; }
    }
    return nullptr;
}

// =====================================================================
// 解析器：递归下降，严格 canonical（无空白/无浮点/ASCII——见文件头规则 2）
// =====================================================================

/// 解析深度上限（防恶意深嵌套耗尽栈——非法输入防御；§4.4 最深结构仅 4 层，
/// 64 已富余两个数量级）。
constexpr int kMaxDepth = 64;

class JsonParser {
public:
    explicit JsonParser(std::string_view text)
        : p_(text.data()), end_(text.data() + text.size())
    {
    }

    /// 解析整个文档：一个顶层值＋输入必须恰好耗尽（尾随内容＝截断/拼接
    /// 现场证据，拒绝）。
    JsonValue parseDocument()
    {
        JsonValue v = parseValue(0);
        if (p_ != end_) {
            corrupt("trailing content after JSON document（截断或非 canonical 尾部）");
        }
        return v;
    }

private:
    const char* p_;     ///< 当前扫描位置（耗尽后 peek/next 即报截断）
    const char* end_;   ///< 输入末尾（哨兵）
    bool firstDigitWasZero_ = false;  ///< 当前数字字面量首数字是否为 '0'

    /// 输入是否已耗尽。
    bool atEnd() const { return p_ == end_; }

    /// 读取当前字节（不前进）。耗尽即"截断"——canonical 文档不允许提前结束。
    char peek()
    {
        if (atEnd()) { corrupt("truncated input（文档在结构中间结束）"); }
        return *p_;
    }

    /// 消费一个字节并返回。
    char next()
    {
        const char c = peek();
        ++p_;
        return c;
    }

    /// 期望当前字节为 expected，否则拒绝（结构层语法错误）。
    void expect(char expected)
    {
        const char c = next();
        if (c != expected) {
            corrupt(std::string("syntax error: expected '") + expected + "' got '"
                    + printable(c) + "'");
        }
    }

    /// 控制字符/引号的可打印呈现（错误消息防注入——不透传原始字节）。
    static std::string printable(char c)
    {
        if (c >= 0x20 && c < 0x7F) { return std::string{c}; }
        static constexpr char kHex[] = "0123456789abcdef";
        const auto u = static_cast<unsigned char>(c);
        std::string s = "0x";
        s += kHex[(u >> 4) & 0xF];
        s += kHex[u & 0xF];
        return s;
    }

    /// 值分发：对象/数组/字符串/整数；其余一律拒绝。
    JsonValue parseValue(int depth)
    {
        // 深度上限检查在进入前做（每层 +1——防御在先，不依赖递归自然爆栈）。
        if (depth > kMaxDepth) { corrupt("nesting depth exceeds limit（非法深嵌套）"); }
        const char c = peek();
        switch (c) {
        case '{': return parseObject(depth);
        case '[': return parseArray(depth);
        case '"': {
            JsonValue v;
            v.kind = JsonValue::Kind::String;
            v.str = parseString();
            return v;
        }
        case '-':
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9': {
            JsonValue v;
            v.kind = JsonValue::Kind::Integer;
            parseNumber(v);
            return v;
        }
        default:
            // true/false/null 与一切裸词在词法层拒绝——§4.4 契约无此类字段，
            // 缺失即可选，出现即非 canonical（文件头规则 2）。
            corrupt(std::string("unexpected character '") + printable(c)
                    + "'（接受：对象/数组/字符串/整数）");
        }
    }

    /// 对象：'{' (string ':' value (',' string ':' value)*)? '}'；重复键拒绝。
    JsonValue parseObject(int depth)
    {
        JsonValue v;
        v.kind = JsonValue::Kind::Object;
        expect('{');
        if (peek() == '}') { next(); return v; }
        for (;;) {
            const char q = peek();
            if (q != '"') { corrupt("object key must be a string"); }
            std::string key = parseString();
            // 重复键＝同一对象两义性，canonical 格式不存在（文件头规则 2）。
            for (const auto& m : v.members) {
                if (m.first == key) { corrupt("duplicate object key '" + key + "'"); }
            }
            expect(':');
            v.members.emplace_back(std::move(key), parseValue(depth + 1));
            const char sep = next();
            if (sep == '}') { return v; }
            if (sep != ',') {
                corrupt(std::string("syntax error: expected ',' or '}' got '")
                        + printable(sep) + "'");
            }
        }
    }

    /// 数组：'[' (value (',' value)*)? ']'。
    JsonValue parseArray(int depth)
    {
        JsonValue v;
        v.kind = JsonValue::Kind::Array;
        expect('[');
        if (peek() == ']') { next(); return v; }
        for (;;) {
            v.items.push_back(parseValue(depth + 1));
            const char sep = next();
            if (sep == ']') { return v; }
            if (sep != ',') {
                corrupt(std::string("syntax error: expected ',' or ']' got '")
                        + printable(sep) + "'");
            }
        }
    }

    /// 字符串：'"' 转义/ASCII 主体 '"'；\uXXXX → UTF-8（代理对合成）。
    std::string parseString()
    {
        expect('"');
        std::string out;
        for (;;) {
            if (atEnd()) { corrupt("truncated string（缺收尾引号）"); }
            const unsigned char c = static_cast<unsigned char>(next());
            if (c == '"') { return out; }
            if (c == '\\') { parseEscape(out); continue; }
            if (c < 0x20) {
                // 裸控制字符：canonical 输出必转义（文件头规则 3），出现即非规范。
                corrupt("raw control character in string（须以转义形式出现）");
            }
            if (c >= 0x80) {
                // 非 ASCII 原始字节：磁盘格式 ASCII（§4.8）——可能来自编码
                // 错乱（GBK 误写）或截断拼接，拒绝而非猜测转码。
                corrupt("non-ASCII byte in string（磁盘格式为 ASCII；非 ASCII"
                        " 须以 \\uXXXX 转义出现）");
            }
            out.push_back(static_cast<char>(c));
        }
    }

    /// 解析转义序列并追加解码结果到 out。
    void parseEscape(std::string& out)
    {
        if (atEnd()) { corrupt("truncated escape sequence"); }
        const char e = next();
        switch (e) {
        case '"':  out.push_back('"');  return;
        case '\\': out.push_back('\\'); return;
        case '/':  out.push_back('/');  return;
        case 'b':  out.push_back('\b'); return;
        case 'f':  out.push_back('\f'); return;
        case 'n':  out.push_back('\n'); return;
        case 'r':  out.push_back('\r'); return;
        case 't':  out.push_back('\t'); return;
        case 'u':  parseUnicodeEscape(out); return;
        default:
            corrupt(std::string("invalid escape '\\") + printable(e) + "'");
        }
    }

    /// 读取 4 位十六进制（JSON \u 语义：大小写均可——dump 端输出小写，
    /// 解析端接受规范 JSON 的两种写法；解码结果唯一，不构成 canonical 二义）。
    std::uint32_t parseHex4()
    {
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = next();
            v <<= 4;
            if (c >= '0' && c <= '9') { v |= static_cast<std::uint32_t>(c - '0'); }
            else if (c >= 'a' && c <= 'f') { v |= static_cast<std::uint32_t>(c - 'a' + 10); }
            else if (c >= 'A' && c <= 'F') { v |= static_cast<std::uint32_t>(c - 'A' + 10); }
            else { corrupt("invalid \\u escape（需 4 位十六进制）"); }
        }
        return v;
    }

    /// \uXXXX 解码：BMP 直出 UTF-8；高位代理必须跟随低位代理（孤立代理
    /// 拒绝——不存在合法字符，保留将产生无法 round-trip 的悬空字节）。
    void parseUnicodeEscape(std::string& out)
    {
        const std::uint32_t u1 = parseHex4();
        std::uint32_t cp = u1;
        if (u1 >= 0xD800 && u1 <= 0xDBFF) {
            // 高位代理：必须是 \uDC00-\uDFFF 低代理续体（JSON surrogate pair）。
            if (p_ + 1 >= end_ || *p_ != '\\' || p_[1] != 'u') {
                corrupt("lone high surrogate in \\u escape（缺低位代理续体）");
            }
            p_ += 2;  // 消费 "\u"
            const std::uint32_t u2 = parseHex4();
            if (u2 < 0xDC00 || u2 > 0xDFFF) {
                corrupt("invalid low surrogate in \\u escape（越出 DC00-DFFF）");
            }
            cp = 0x10000 + ((u1 - 0xD800) << 10) + (u2 - 0xDC00);
        } else if (u1 >= 0xDC00 && u1 <= 0xDFFF) {
            corrupt("lone low surrogate in \\u escape（缺高位代理前体）");
        }
        appendUtf8(out, cp);
    }

    /// 码点 → UTF-8 追加（标准最短形式；与 dump 侧 decodeUtf8At 互逆）。
    static void appendUtf8(std::string& out, std::uint32_t cp)
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

    /// 整数：'-'? [0-9]+，前导零拒绝，无小数/指数（§4.8 无浮点），范围
    /// 限 [INT64_MIN, UINT64_MAX]（字段层再做位宽裁剪）。
    void parseNumber(JsonValue& v)
    {
        firstDigitWasZero_ = false;  // 每个数字字面量独立判定前导零
        if (peek() == '-') {
            v.negative = true;
            next();
            if (atEnd()) { corrupt("truncated number（负号后无数字）"); }
        }
        std::size_t digits = 0;
        std::uint64_t mag = 0;
        while (!atEnd()) {
            const char c = *p_;
            if (c < '0' || c > '9') { break; }
            if (digits == 0 && c == '0') { firstDigitWasZero_ = true; }
            ++digits;
            const unsigned d = static_cast<unsigned>(c - '0');
            // 溢出检查：mag*10+d 不得超 UINT64_MAX（负数路径另限 2^63）。
            if (mag > (std::numeric_limits<std::uint64_t>::max() - d) / 10) {
                corrupt("integer magnitude exceeds uint64 range（越界拒绝）");
            }
            mag = mag * 10 + d;
            ++p_;
        }
        if (digits == 0) { corrupt("number requires at least one digit"); }
        // "01" 这类前导零＝同一数值的第二种字节形态，canonical 不允许
        // （确定性 NFR-COR-02：同值必同字节）。
        if (digits > 1 && firstDigitWasZero_) { corrupt("leading zero in number（非 canonical）"); }
        // §4.8"无浮点"：小数点/指数后继在词法层拒绝（而非把 1.5 截成 1）。
        if (!atEnd()) {
            const char c = *p_;
            if (c == '.' || c == 'e' || c == 'E') {
                corrupt("float/exponent literal rejected（自有格式无浮点——§4.8）");
            }
        }
        if (v.negative && mag > 0x8000000000000000ULL) {
            corrupt("integer exceeds int64 range（越界拒绝）");
        }
        v.magnitude = mag;
    }
};

// =====================================================================
// dump 侧字符串转义与 UTF-8 严格解码（规则 1/3/4）
// =====================================================================

/// 在 s[i] 处解码一个 UTF-8 序列（标准最短形式；代理码点/超 U+10FFFF/超长
/// 编码均拒绝——与 parse 侧 appendUtf8 互逆）。
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
 * @brief 把 UTF-8 字符串按 canonical 转义表追加到 out（文件头规则 3）。
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
// parse 复用片段：字段提取（类型不符/缺失/越界/格式非法统一 corrupt）
// =====================================================================

/// 取必填对象成员（缺失＝截断/写缺——store-corrupt 数据侧）。
const JsonValue& requireMember(const JsonValue& obj, const char* key)
{
    if (const JsonValue* v = findMember(obj, key)) { return *v; }
    corrupt(std::string("missing required field '") + key + "'");
}

/// 必填字符串字段（类型须为 JSON 字符串）。
std::string requireStringField(const JsonValue& obj, const char* key)
{
    const JsonValue& v = requireMember(obj, key);
    if (v.kind != JsonValue::Kind::String) {
        corrupt(std::string("field '") + key + "' must be a string");
    }
    return v.str;
}

/// 可选 core 身份字段的探针：字段是否存在（存在性检查先于类型检查）。
bool hasMember(const JsonValue& obj, const char* key)
{
    return findMember(obj, key) != nullptr;
}

/// 必填整数字段的公共检查：kind==Integer 且落在 [lo, hi]（hi 以 uint64
/// 承接 2^64-1；负数侧以 int64 下界承接）。
std::int64_t integerInRange(const JsonValue& obj, const char* key, std::int64_t lo,
                            std::uint64_t hi)
{
    const JsonValue& v = requireMember(obj, key);
    if (v.kind != JsonValue::Kind::Integer) {
        corrupt(std::string("field '") + key + "' must be an integer");
    }
    if (v.negative) {
        // 负数路径：绝对值上限 = max(-lo, INT64_MIN 的 2^63 特例)。
        if (lo >= 0) {
            corrupt(std::string("field '") + key + "' must be non-negative");
        }
        const std::uint64_t magLimit =
            (lo == std::numeric_limits<std::int64_t>::min())
                ? 0x8000000000000000ULL                       // INT64_MIN 允许
                : static_cast<std::uint64_t>(-(lo));          // 其余按 -lo
        if (v.magnitude > magLimit) {
            corrupt(std::string("field '") + key + "' out of range（越界拒绝）");
        }
        // magnitude == 2^63 仅在 lo == INT64_MIN 时可达（上一步已裁剪）——
        // 此时结果恰为 INT64_MIN，直接返回避免有符号取负 UB。
        if (v.magnitude == 0x8000000000000000ULL) {
            return std::numeric_limits<std::int64_t>::min();
        }
        return -static_cast<std::int64_t>(v.magnitude);
    }
    if (v.magnitude > hi) {
        corrupt(std::string("field '") + key + "' exceeds range（越界拒绝）");
    }
    return static_cast<std::int64_t>(v.magnitude);
}

/// 必填 int（int32 范围；schemaVersion 用）。
int requireIntField(const JsonValue& obj, const char* key)
{
    return static_cast<int>(integerInRange(obj, key, std::numeric_limits<int>::min(),
                                           static_cast<std::uint64_t>(
                                               std::numeric_limits<int>::max())));
}

/// 必填 uint64（revisionSeq/sizeBytes 用；负值/超 2^64-1 拒绝）。
std::uint64_t requireUint64Field(const JsonValue& obj, const char* key)
{
    return static_cast<std::uint64_t>(integerInRange(
        obj, key, 0, std::numeric_limits<std::uint64_t>::max()));
}

/// 必填 uint32（payloadFormatVersion 用）。
std::uint32_t requireUint32Field(const JsonValue& obj, const char* key)
{
    return static_cast<std::uint32_t>(integerInRange(
        obj, key, 0, static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())));
}

/// 必填对象字段（类型须为 JSON 对象）。
const JsonValue& requireObjectField(const JsonValue& obj, const char* key)
{
    const JsonValue& v = requireMember(obj, key);
    if (v.kind != JsonValue::Kind::Object) {
        corrupt(std::string("field '") + key + "' must be an object");
    }
    return v;
}

/// 必填数组字段（类型须为 JSON 数组）。
const JsonValue& requireArrayField(const JsonValue& obj, const char* key)
{
    const JsonValue& v = requireMember(obj, key);
    if (v.kind != JsonValue::Kind::Array) {
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
std::string requireTokenField(const JsonValue& obj, const char* key)
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
std::string requirePathField(const JsonValue& obj, const char* key)
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
std::string requireHex64Field(const JsonValue& obj, const char* key)
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
/// 均拒绝）。
template <typename CoreId>
CoreId requireIdField(const JsonValue& obj, const char* key)
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
std::optional<CoreId> optionalIdField(const JsonValue& obj, const char* key)
{
    if (!hasMember(obj, key)) { return std::nullopt; }
    return requireIdField<CoreId>(obj, key);
}

/// 未知字段处理的前向声明（定义见下方"未知字段处理"块——parseRefPair
/// 复用它做嵌套对象的键集校验）。
void rejectUnknownFields(const JsonValue& obj,
                         const std::initializer_list<const char*>& known, bool tolerate);

/// {objectId, contentVersion} 引用对（metadataRef/supersedes；固定两键）。
/// tolerate 语义与 rejectUnknownFields 一致（上级次版本文档的嵌套对象同样
/// 可能追加可选字段——嵌套容忍与顶层一致，§4.8）。
ObjectRefPair parseRefPair(const JsonValue& obj, bool tolerate)
{
    ObjectRefPair ref;
    ref.objectId = requireIdField<ObjectId>(obj, "objectId");
    ref.contentVersion = requireIdField<ContentVersion>(obj, "contentVersion");
    rejectUnknownFields(obj, {"objectId", "contentVersion"}, tolerate);
    return ref;
}

/// 可选引用对字段。
std::optional<ObjectRefPair> optionalRefPairField(const JsonValue& obj, const char* key,
                                                  bool tolerate)
{
    if (!hasMember(obj, key)) { return std::nullopt; }
    return parseRefPair(requireObjectField(obj, key), tolerate);
}

/// 时间戳类字段（ISO-8601 UTC 文本，§4.4 约定）：非空可打印 ASCII（语法
/// 不校验——透传，见 requireTokenField 口径注释）。
std::string requireTimestampField(const JsonValue& obj, const char* key)
{
    return requireTokenField(obj, key);
}

/// 未知字段处理（§4.8 次版本兼容语义的落点）：tolerate=true（上级次版本
/// 文档）跳过；否则拒绝（当前版本的合法文档不含未知字段——出现即损坏或
/// 写入器 bug）。
void rejectUnknownFields(const JsonValue& obj,
                         const std::initializer_list<const char*>& known, bool tolerate)
{
    if (obj.kind != JsonValue::Kind::Object) { return; }
    for (const auto& m : obj.members) {
        bool isKnown = false;
        for (const char* k : known) {
            if (m.first == k) { isKnown = true; break; }
        }
        if (!isKnown && !tolerate) {
            corrupt("unknown field '" + m.first
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
bool parsePreambleVersioned(const JsonValue& root, const char* typeName, bool withFormatId)
{
    if (root.kind != JsonValue::Kind::Object) {
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
    std::string out;
    out += "{\"commandType\":";
    appendJsonString(out, value.commandType, "commandType");
    out += ",\"payloadFormatVersion\":" + std::to_string(value.payloadFormatVersion);
    out += ",\"payloadCanonical\":";
    appendJsonString(out, value.payloadCanonical, "payloadCanonical");  // 域负载透传编码
    if (value.inverse.has_value()) {
        out += ",\"inverse\":{\"commandType\":";
        appendJsonString(out, value.inverse->commandType, "inverse.commandType");
        out += ",\"payloadFormatVersion\":"
             + std::to_string(value.inverse->payloadFormatVersion);
        out += ",\"payloadCanonical\":";
        appendJsonString(out, value.inverse->payloadCanonical, "inverse.payloadCanonical");
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

// ---- parse：六类型，严格拒绝＋版本判定 ----

ProjectStaticIdentity parseStaticIdentity(std::string_view text)
{
    JsonParser parser(text);
    const JsonValue root = parser.parseDocument();
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
    JsonParser parser(text);
    const JsonValue root = parser.parseDocument();
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
    JsonParser parser(text);
    const JsonValue root = parser.parseDocument();
    // 无 schemaVersion/formatId 字段（§4.4.2 表）——版本语义由容器判定
    // （PersistenceFormat.hpp 注释），按当前支持版本严格解析。
    if (root.kind != JsonValue::Kind::Object) {
        corrupt("top-level value must be an object (revision-manifest)");
    }

    RevisionManifest v;
    v.revisionId = requireIdField<RevisionId>(root, "revisionId");
    v.revisionSeq = requireUint64Field(root, "revisionSeq");
    v.parentRevisionId = optionalIdField<RevisionId>(root, "parentRevisionId");
    v.branchId = requireIdField<BranchId>(root, "branchId");
    v.committedAtUtc = requireTimestampField(root, "committedAtUtc");
    v.metadataRef = parseRefPair(requireObjectField(root, "metadataRef"), false);
    const JsonValue& refs = requireArrayField(root, "objectRefs");
    // §4.4.2 约束：objectRefs ≥1（完整对象引用集至少含元数据对象本身）。
    if (refs.items.empty()) { corrupt("objectRefs must not be empty（§4.4.2 ≥1）"); }
    for (const JsonValue& item : refs.items) {
        if (item.kind != JsonValue::Kind::Object) {
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
    if (const JsonValue* intro = findMember(root, "introducedObjects")) {
        if (intro->kind != JsonValue::Kind::Array) {
            corrupt("field 'introducedObjects' must be an array");
        }
        for (const JsonValue& item : intro->items) {
            if (item.kind != JsonValue::Kind::String) {
                corrupt("introducedObjects items must be strings");
            }
            auto cv = ContentVersion::tryFromCanonical(item.str);
            if (!cv.has_value()) {
                corrupt("introducedObjects item is not a canonical cv- text: " + item.str);
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
    JsonParser parser(text);
    const JsonValue root = parser.parseDocument();
    const bool tolerate = parsePreambleVersioned(root, "project-metadata-record", false);

    ProjectMetadataRecord v;
    v.schemaVersion = requireIntField(root, "schemaVersion");
    v.committedBy = requireIdField<RevisionId>(root, "committedBy");
    v.supersedes = optionalRefPairField(root, "supersedes", tolerate);
    v.projectDisplayName = requireStringField(root, "projectDisplayName");
    v.primaryBranchId = requireIdField<BranchId>(root, "primaryBranchId");
    const JsonValue& branches = requireArrayField(root, "branches");
    // §4.4.3 约束：branches ≥1（至少含主分支；INV-M1 唯一性属发布期校验）。
    if (branches.items.empty()) { corrupt("branches must not be empty（§4.4.3 ≥1）"); }
    for (const JsonValue& item : branches.items) {
        if (item.kind != JsonValue::Kind::Object) {
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
    if (const JsonValue* labels = findMember(root, "schemeLabels")) {
        if (labels->kind != JsonValue::Kind::Object) {
            corrupt("field 'schemeLabels' must be an object");
        }
        for (const auto& kv : labels->members) {
            if (kv.second.kind != JsonValue::Kind::String) {
                corrupt("schemeLabels values must be strings");
            }
            v.schemeLabels.emplace(kv.first, kv.second.str);
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
    JsonParser parser(text);
    const JsonValue root = parser.parseDocument();
    // 无 schemaVersion/formatId 字段（§4.4.4 表）——同 RevisionManifest，
    // 版本语义由容器（修订/打开协议）判定，按当前支持版本严格解析。
    if (root.kind != JsonValue::Kind::Object) {
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
    v.payloadCanonical = requireStringField(root, "payloadCanonical");  // 域负载透传（不解释）
    if (const JsonValue* inv = findMember(root, "inverse")) {
        if (inv->kind != JsonValue::Kind::Object) {
            corrupt("field 'inverse' must be an object");
        }
        InverseCommand i;
        i.commandType = requireTokenField(*inv, "commandType");
        i.payloadFormatVersion = requireUint32Field(*inv, "payloadFormatVersion");
        i.payloadCanonical = requireStringField(*inv, "payloadCanonical");
        rejectUnknownFields(*inv,
                            {"commandType", "payloadFormatVersion", "payloadCanonical"},
                            false);
        v.inverse = std::move(i);
    }
    if (const JsonValue* confs = findMember(root, "confirmations")) {
        if (confs->kind != JsonValue::Kind::Array) {
            corrupt("field 'confirmations' must be an array");
        }
        for (const JsonValue& item : confs->items) {
            if (item.kind != JsonValue::Kind::Object) {
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
            const JsonValue& cred = requireObjectField(item, "credential");
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
    JsonParser parser(text);
    const JsonValue root = parser.parseDocument();
    const bool tolerate = parsePreambleVersioned(root, "draft-document", false);

    DraftDocument v;
    v.schemaVersion = requireIntField(root, "schemaVersion");
    v.projectId = requireIdField<ProjectId>(root, "projectId");
    v.branchId = requireIdField<BranchId>(root, "branchId");
    v.moduleId = requireTokenField(root, "moduleId");   // 注册表校验归 PRJ-T12
    v.baseRevisionId = requireIdField<RevisionId>(root, "baseRevisionId");
    v.payload = requireStringField(root, "payload");    // 域负载透传（不解释）
    if (const JsonValue* refs = findMember(root, "externalRefs")) {
        if (refs->kind != JsonValue::Kind::Array) {
            corrupt("field 'externalRefs' must be an array");
        }
        for (const JsonValue& item : refs->items) {
            if (item.kind != JsonValue::Kind::Object) {
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
    JsonParser parser(text);
    const JsonValue root = parser.parseDocument();
    // 无 schemaVersion/formatId 字段（§4.4.7 表）——同 RevisionManifest/
    // CommandRecord，版本语义由容器判定，按当前支持版本的结构严格解析
    // （未知字段拒绝——tolerate 恒 false）。
    if (root.kind != JsonValue::Kind::Object) {
        corrupt("top-level value must be an object (run-manifest)");
    }

    RunManifest v;
    // taskIdentity 五元组（§4.4.7 必填；字段序＝core TaskIdentity 声明序
    // ——project/branch/revision/run/attempt）。身份字段一律 core 规范
    // 文本（§4.4 约定），五个身份逐字段严格解析。
    {
        const JsonValue& task = requireObjectField(root, "taskIdentity");
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
    const JsonValue& items = requireArrayField(root, "items");
    if (items.items.empty()) {
        corrupt("items must not be empty（§4.4.7 ≥1）");
    }
    for (const JsonValue& item : items.items) {
        if (item.kind != JsonValue::Kind::Object) {
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
