/**
 * @file   JsonLite.cpp
 * @brief  JsonLite 实现——递归下降严格解析（行列/字段路径追踪）＋确定性序列化。
 *
 * 设计依据：
 *   - units/testkit.md §4.1（受限范围与拒绝清单）、§5/§7.2（TestKitError 语义）、
 *     §8 TK-JSON（往返/拒绝矩阵）
 *   - 任务契约 tasks/foundation/TK-T02.json（≙WP-02-T02）
 *
 * 实现结构：解析器为一个文件内局部类（持文本指针＋路径栈），parseJson 仅做
 * 入口装配——错误统一经 fail() 抛 TestKitError(DatasetInvalid)，消息格式：
 *   "json-parse: line <L>, column <C>: <原因>（路径 <path>）"
 * 列号按 UTF-8 首字节计（多字节字符占一列——定位语义对中文文本同样可读）。
 */

#include <sdurws/ird/testkit/JsonLite.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <system_error>

namespace sdurws::ird::testkit {
namespace {

/// 嵌套深度上限（防恶意/意外深嵌套的栈溢出——远超合法数据集形态）。
constexpr std::size_t kMaxDepth = 200;

/**
 * @brief 递归下降解析器（文本只读；位置与路径为可变状态）。
 *
 * 字段路径形态：根为 "$"；对象键记 "$.a.b"、数组记 "$[2]"——与 manifest
 * 校验（TK-T03）的消息口径一致，便于人工对照。
 */
class Parser {
public:
    explicit Parser(std::string_view text)
        : text_(text)
    {
    }

    JsonValue run()
    {
        // BOM（EF BB BF）在文件头出现即拒绝（§4.1 拒绝清单）——不做剥离容错。
        if (text_.size() >= 3 && static_cast<unsigned char>(text_[0]) == 0xEF
            && static_cast<unsigned char>(text_[1]) == 0xBB
            && static_cast<unsigned char>(text_[2]) == 0xBF) {
            fail("不允许 UTF-8 BOM（受限格式拒绝带 BOM 文本）");
        }
        skipWs();
        if (eof()) {
            fail("空输入（未找到任何 JSON 值）");
        }
        JsonValue v = parseValue(0);
        skipWs();
        if (!eof()) {
            fail("尾随内容（顶层值之后仍有非空白字符）");
        }
        return v;
    }

private:
    // ---- 基础游标 ----
    bool eof() const noexcept { return pos_ >= text_.size(); }
    char peek() const noexcept { return text_[pos_]; }

    void advance() noexcept
    {
        ++pos_;
        ++col_;
    }

    /// 换行计数（\n 计一行、列回 1；\r 按 JSON 空白处理但不单独成行——
    /// \r\n 由 \n 计行，孤 \r 罕见且 JSON 规范允许作空白）。
    void skipWs()
    {
        while (!eof()) {
            const char c = peek();
            if (c == '\n') {
                ++pos_;
                ++line_;
                col_ = 1;
            } else if (c == ' ' || c == '\t' || c == '\r') {
                advance();
            } else {
                break;
            }
        }
    }

    /// 吃掉期望的单字符；不符即 fail（结构标量的语法骨架）。
    void expect(char c, const char* what)
    {
        if (eof() || peek() != c) {
            fail(std::string{"期望 '"} + c + "'（" + what + "）");
        }
        advance();
    }

    /// 统一错误出口：行列即时快照＋当前路径（TestKitError:DatasetInvalid）。
    [[noreturn]] void fail(std::string reason) const
    {
        throw TestKitError(TestKitErrorKind::DatasetInvalid,
                           "json-parse: line " + std::to_string(line_)
                               + ", column " + std::to_string(col_) + ": " + reason
                               + "（路径 " + path_ + "）");
    }

    // ---- 值解析（depth 防栈溢出） ----
    JsonValue parseValue(std::size_t depth)
    {
        if (depth > kMaxDepth) {
            fail("嵌套超过 " + std::to_string(kMaxDepth) + " 层（拒绝深嵌套）");
        }
        skipWs();
        if (eof()) {
            fail("值位置意外到达文本末尾");
        }
        switch (peek()) {
        case '{': return parseObject(depth);
        case '[': return parseArray(depth);
        case '"': {
            JsonValue v;
            v.kind = JsonValue::Kind::String;
            v.text = parseString();
            return v;
        }
        case 't': expectLiteral("true"); {
            JsonValue v; v.kind = JsonValue::Kind::Bool; v.boolean = true; return v;
        }
        case 'f': expectLiteral("false"); {
            JsonValue v; v.kind = JsonValue::Kind::Bool; v.boolean = false; return v;
        }
        case 'n': expectLiteral("null"); {
            JsonValue v; v.kind = JsonValue::Kind::Null; return v;
        }
        default:
            // 数字或非法字面（NaN/Infinity/注释等在此给出针对性消息）。
            if (peek() == '-' || (peek() >= '0' && peek() <= '9')) {
                return parseNumber();
            }
            if (peek() == 'N') { fail("NaN 字面被拒绝（受限格式不收非有限数）"); }
            if (peek() == 'I') { fail("Infinity 字面被拒绝"); }
            if (peek() == '/') { fail("注释被拒绝（// 与 /* 均不收——受限格式无注释）"); }
            fail(std::string{"非法值起始字符: '"} + peek() + "'");
        }
    }

    /// 字面量整体匹配（true/false/null/NaN 等逐字符——防止 "trueX" 之类越界吞并）。
    void expectLiteral(const char* lit)
    {
        for (const char* p = lit; *p != '\0'; ++p) {
            if (eof() || peek() != *p) {
                fail(std::string{"字面量不完整或不合法（期望 \""} + lit + "\"）");
            }
            advance();
        }
    }

    // ---- 对象：键序＝插入序；重复键拒绝（§4.1） ----
    JsonValue parseObject(std::size_t depth)
    {
        expect('{', "对象起始");
        JsonValue v;
        v.kind = JsonValue::Kind::Object;
        const std::size_t myPathLen = path_.size();
        skipWs();
        if (!eof() && peek() == '}') {          // 空对象合法
            advance();
            return v;
        }
        while (true) {
            skipWs();
            if (eof() || peek() != '"') {
                if (!eof() && peek() == '}') { fail("对象尾逗号（键值对后多一个 ','）"); }
                fail("对象键必须是以引号开始的字符串");
            }
            const std::size_t keyLine = line_;
            const std::size_t keyCol = col_;
            const std::string key = parseString();
            // 重复键拒绝：以"键所在行列"报错（比冒号后位置更可定位），不篡改游标。
            for (const auto& kv : v.members) {
                if (kv.first == key) {
                    path_.resize(myPathLen);
                    path_ += '.';
                    path_ += key;
                    throw TestKitError(TestKitErrorKind::DatasetInvalid,
                                       "json-parse: line " + std::to_string(keyLine)
                                           + ", column " + std::to_string(keyCol)
                                           + ": 重复键 \"" + key
                                           + "\"（键序不保证语义，受限格式直接拒绝）（路径 " + path_ + "）");
                }
            }
            path_.resize(myPathLen);
            path_ += '.';
            path_ += key;
            skipWs();
            expect(':', "键值分隔");
            v.members.emplace_back(key, parseValue(depth + 1));
            path_.resize(myPathLen);
            skipWs();
            if (!eof() && peek() == ',') {
                advance();
                continue;
            }
            if (!eof() && peek() == '}') {
                advance();
                path_.resize(myPathLen);
                return v;
            }
            fail("对象期望 ',' 或 '}'");
        }
    }

    // ---- 数组：有序；同样拒绝尾逗号 ----
    JsonValue parseArray(std::size_t depth)
    {
        expect('[', "数组起始");
        JsonValue v;
        v.kind = JsonValue::Kind::Array;
        const std::size_t myPathLen = path_.size();
        skipWs();
        if (!eof() && peek() == ']') {
            advance();
            return v;
        }
        std::size_t index = 0;
        while (true) {
            path_.resize(myPathLen);
            path_ += '[';
            path_ += std::to_string(index);
            path_ += ']';
            v.items.push_back(parseValue(depth + 1));
            ++index;
            path_.resize(myPathLen);
            skipWs();
            if (!eof() && peek() == ',') {
                advance();
                skipWs();
                if (!eof() && peek() == ']') { fail("数组尾逗号（元素后多一个 ','）"); }
                continue;
            }
            if (!eof() && peek() == ']') {
                advance();
                path_.resize(myPathLen);
                return v;
            }
            fail("数组期望 ',' 或 ']'");
        }
    }

    // ---- 字符串：标准转义＋\uXXXX（含代理对）；控制字符拒绝 ----
    std::string parseString()
    {
        expect('"', "字符串起始");
        std::string out;
        while (true) {
            if (eof()) {
                fail("字符串未闭合（到达文本末尾）");
            }
            const unsigned char c = static_cast<unsigned char>(peek());
            if (c == '"') {
                advance();
                return out;
            }
            if (c == '\\') {
                advance();
                if (eof()) { fail("转义符后到达文本末尾"); }
                const char e = peek();
                advance();
                switch (e) {
                case '"':  out.push_back('"');  break;
                case '\\': out.push_back('\\'); break;
                case '/':  out.push_back('/');  break;
                case 'b':  out.push_back('\b'); break;
                case 'f':  out.push_back('\f'); break;
                case 'n':  out.push_back('\n'); break;
                case 'r':  out.push_back('\r'); break;
                case 't':  out.push_back('\t'); break;
                case 'u':  out.append(parseUnicodeEscape()); break;
                default:
                    fail(std::string{"非法转义 \\"} + e + "（仅 \" \\ / b f n r t u）");
                }
                continue;
            }
            if (c < 0x20) {
                fail("字符串内裸控制字符（须转义）");
            }
            // UTF-8 字节原样收集（合法性由装载层/文件编码保证——受限解析器不重编码）。
            out.push_back(static_cast<char>(c));
            advance();
        }
    }

    /// \uXXXX 十六进制体解析；高代理后必须紧跟 \uDC00-\uDFFF 低代理（成对合成）。
    std::string parseUnicodeEscape()
    {
        const unsigned cp = parseHex4();
        if (cp >= 0xD800u && cp <= 0xDBFFu) {                       // 高代理
            if (pos_ + 1 < text_.size() && text_[pos_] == '\\' && text_[pos_ + 1] == 'u') {
                advance(); advance();
                const unsigned low = parseHex4();
                if (low < 0xDC00u || low > 0xDFFFu) {
                    fail("高代理后未跟低代理（\\uD800-\\uDBFF 须成对）");
                }
                const unsigned full = 0x10000u + ((cp - 0xD800u) << 10) + (low - 0xDC00u);
                return utf8Encode(full);
            }
            fail("高代理后未跟低代理（\\uD800-\\uDBFF 须成对）");
        }
        if (cp >= 0xDC00u && cp <= 0xDFFFu) {
            fail("孤立低代理（\\uDC00-\\uDFFF 不能单独出现）");
        }
        return utf8Encode(cp);
    }

    unsigned parseHex4()
    {
        unsigned v = 0;
        for (int i = 0; i < 4; ++i) {
            if (eof()) { fail("\\u 转义不足 4 个十六进制位"); }
            const char c = peek();
            unsigned nib;
            if (c >= '0' && c <= '9')      nib = static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') nib = static_cast<unsigned>(c - 'a') + 10u;
            else if (c >= 'A' && c <= 'F') nib = static_cast<unsigned>(c - 'A') + 10u;
            else { fail("\\u 转义含非十六进制字符"); }
            v = (v << 4) | nib;
            advance();
        }
        return v;
    }

    /// 码点 → UTF-8 字节序列（1~4 字节标准编码）。
    static std::string utf8Encode(unsigned cp)
    {
        std::string s;
        if (cp < 0x80u) {
            s.push_back(static_cast<char>(cp));
        } else if (cp < 0x800u) {
            s.push_back(static_cast<char>(0xC0u | (cp >> 6)));
            s.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else if (cp < 0x10000u) {
            s.push_back(static_cast<char>(0xE0u | (cp >> 12)));
            s.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
            s.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else {
            s.push_back(static_cast<char>(0xF0u | (cp >> 18)));
            s.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
            s.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
            s.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        }
        return s;
    }

    // ---- 数字：JSON 语法＋IEEE double 精确区间（非有限拒绝） ----
    JsonValue parseNumber()
    {
        const std::size_t startPos = pos_;
        const std::size_t startLine = line_;
        const std::size_t startCol = col_;       // 数字级错误按"起始位置"定位
        if (peek() == '-') {
            advance();
            if (eof() || peek() < '0' || peek() > '9') {
                if (!eof() && peek() == 'I') { fail("-Infinity 字面被拒绝"); }
                if (!eof() && peek() == 'N') { fail("-NaN 字面被拒绝"); }
                fail("数字缺少整数部分（'-' 后必须是数字）");
            }
        }
        // 整数部分：0 或 [1-9][0-9]*（前导零拒绝——"01" 非法）。
        if (eof() || peek() < '0' || peek() > '9') {
            fail("数字缺少整数部分");
        }
        if (peek() == '0') {
            advance();
            if (!eof() && peek() >= '0' && peek() <= '9') {
                fail("前导零（'0' 后不得直接跟数字）");
            }
        } else {
            while (!eof() && peek() >= '0' && peek() <= '9') { advance(); }
        }
        // 小数部分。
        if (!eof() && peek() == '.') {
            advance();
            if (eof() || peek() < '0' || peek() > '9') { fail("小数点后缺数字"); }
            while (!eof() && peek() >= '0' && peek() <= '9') { advance(); }
        }
        // 指数部分。
        if (!eof() && (peek() == 'e' || peek() == 'E')) {
            advance();
            if (!eof() && (peek() == '+' || peek() == '-')) { advance(); }
            if (eof() || peek() < '0' || peek() > '9') { fail("指数缺数字"); }
            while (!eof() && peek() >= '0' && peek() <= '9') { advance(); }
        }
        // 语法段提取后经 strtod（标准六进制/Infinity 等已被语法层排除）。
        const std::string token{text_.substr(startPos, pos_ - startPos)};
        double value = 0.0;
        const std::string tokenz = token + '\0';
        const char* begin = tokenz.c_str();
        char* end = nullptr;
        value = std::strtod(begin, &end);
        if (end != begin + token.size()) {
            fail("数字解析不完整（内部错误形态）");
        }
        if (!std::isfinite(value)) {
            // 溢出按数字起始位置报告（扫描已前进——游标快照避免行列漂移）。
            line_ = startLine;
            col_ = startCol;
            fail("数字超出 IEEE double 可表示范围（结果非有限）");
        }
        JsonValue v;
        v.kind = JsonValue::Kind::Number;
        v.number = value;
        return v;
    }

    std::string_view text_;   ///< 整段输入（不拥有）
    std::size_t pos_ = 0;     ///< 当前字节偏移
    std::size_t line_ = 1;    ///< 当前行（自 1）
    std::size_t col_ = 1;     ///< 当前列（自 1；UTF-8 首字节计列）
    std::string path_ = "$";  ///< 当前字段路径（错误消息定位）
};

// ---------------------------------------------------------------------
// 序列化辅助
// ---------------------------------------------------------------------

/// 字符串转义输出（'"'、'\\'、控制字符短形式或 \u00XX；非 ASCII 透传）。
void dumpEscaped(const std::string& s, std::string* out)
{
    out->push_back('"');
    for (const unsigned char c : s) {
        switch (c) {
        case '"':  out->append("\\\""); break;
        case '\\': out->append("\\\\"); break;
        case '\b': out->append("\\b"); break;
        case '\f': out->append("\\f"); break;
        case '\n': out->append("\\n"); break;
        case '\r': out->append("\\r"); break;
        case '\t': out->append("\\t"); break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04X", static_cast<unsigned>(c));
                out->append(buf);
            } else {
                out->push_back(static_cast<char>(c));   // UTF-8 字节透传
            }
        }
    }
    out->push_back('"');
}

/// 数字最短往返输出（std::to_chars——确定性与按位可还原；失败为不可达防御）。
void dumpNumber(double v, std::string* out)
{
    char buf[64];
    const auto res = std::to_chars(buf, buf + sizeof(buf), v);
    if (res.ec != std::errc()) {
        throw TestKitError(TestKitErrorKind::Usage, "json-dump: 数字序列化失败（不可达防御分支）");
    }
    out->append(buf, static_cast<std::size_t>(res.ptr - buf));
}

/// 递归序列化（紧凑单行形态；pretty 由下方 P::go 承载）。
void dumpValue(const JsonValue& v, std::string* out)
{
    switch (v.kind) {
    case JsonValue::Kind::Null:   out->append("null"); break;
    case JsonValue::Kind::Bool:   out->append(v.boolean ? "true" : "false"); break;
    case JsonValue::Kind::Number: dumpNumber(v.number, out); break;
    case JsonValue::Kind::String: dumpEscaped(v.text, out); break;
    case JsonValue::Kind::Array:
        out->push_back('[');
        for (std::size_t i = 0; i < v.items.size(); ++i) {
            if (i > 0) { out->push_back(','); }
            dumpValue(v.items[i], out);
        }
        out->push_back(']');
        break;
    case JsonValue::Kind::Object:
        out->push_back('{');
        for (std::size_t i = 0; i < v.members.size(); ++i) {
            if (i > 0) { out->push_back(','); }
            dumpEscaped(v.members[i].first, out);
            out->push_back(':');
            dumpValue(v.members[i].second, out);
        }
        out->push_back('}');
        break;
    default:
        throw TestKitError(TestKitErrorKind::Usage, "json-dump: 未知值类别（防御分支）");
    }
}

}  // namespace

// ---------------------------------------------------------------------
// 公共接口
// ---------------------------------------------------------------------

const JsonValue* JsonValue::find(std::string_view key) const noexcept
{
    if (kind != Kind::Object) { return nullptr; }
    for (const auto& kv : members) {
        if (key == kv.first) { return &kv.second; }
    }
    return nullptr;
}

bool JsonValue::deepEquals(const JsonValue& o) const
{
    if (kind != o.kind) { return false; }
    switch (kind) {
    case Kind::Null:   return true;
    case Kind::Bool:   return boolean == o.boolean;
    case Kind::Number: {
        // 按位比较（确定性口径：-0.0 与 0.0 视为不同——序列化输出本就不同）。
        if (std::isnan(number) || std::isnan(o.number)) { return false; }
        return std::memcmp(&number, &o.number, sizeof(double)) == 0;
    }
    case Kind::String: return text == o.text;
    case Kind::Array:
        if (items.size() != o.items.size()) { return false; }
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (!items[i].deepEquals(o.items[i])) { return false; }
        }
        return true;
    case Kind::Object: {
        // 语义等价：键集相同且逐键值相等（键序无关——dump 层才保证插入序）。
        if (members.size() != o.members.size()) { return false; }
        for (const auto& kv : members) {
            const JsonValue* other = o.find(kv.first);
            if (other == nullptr || !kv.second.deepEquals(*other)) { return false; }
        }
        return true;
    }
    }
    return false;
}

JsonValue parseJson(std::string_view text)
{
    Parser p(text);
    return p.run();
}

std::string dumpJson(const JsonValue& value, bool pretty)
{
    if (!pretty) {
        std::string out;
        dumpValue(value, &out);
        return out;
    }
    // 简易 pretty 打印器：递归带深度。
    struct P {
        static void go(const JsonValue& v, int depth, std::string* o) {
            const auto nl = [&](int d) { o->push_back('\n'); o->append(static_cast<std::size_t>(d) * 2, ' '); };
            switch (v.kind) {
            case JsonValue::Kind::Null:   o->append("null"); break;
            case JsonValue::Kind::Bool:   o->append(v.boolean ? "true" : "false"); break;
            case JsonValue::Kind::Number: dumpNumber(v.number, o); break;
            case JsonValue::Kind::String: dumpEscaped(v.text, o); break;
            case JsonValue::Kind::Array:
                if (v.items.empty()) { o->append("[]"); break; }
                o->push_back('[');
                for (std::size_t i = 0; i < v.items.size(); ++i) {
                    if (i > 0) { o->push_back(','); }
                    nl(depth + 1);
                    go(v.items[i], depth + 1, o);
                }
                nl(depth);
                o->push_back(']');
                break;
            case JsonValue::Kind::Object:
                if (v.members.empty()) { o->append("{}"); break; }
                o->push_back('{');
                for (std::size_t i = 0; i < v.members.size(); ++i) {
                    if (i > 0) { o->push_back(','); }
                    nl(depth + 1);
                    dumpEscaped(v.members[i].first, o);
                    o->append(": ");
                    go(v.members[i].second, depth + 1, o);
                }
                nl(depth);
                o->push_back('}');
                break;
            default:
                throw TestKitError(TestKitErrorKind::Usage, "json-dump: 未知值类别（防御分支）");
            }
        }
    };
    // 人读形态：两空格缩进，对象键后 ": "。
    std::string out;
    P::go(value, 0, &out);
    return out;
}

}  // namespace sdurws::ird::testkit
