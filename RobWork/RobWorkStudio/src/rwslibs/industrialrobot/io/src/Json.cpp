/**
 * @file   Json.cpp
 * @brief  JSON 读写器实现——严格 UTF-8 预检（BOM 剥离/仅 UTF-8）、受限
 *         递归下降解析（每值位置区间/重复键拒绝/NaN·Inf·溢出拒绝/深度
 *         与字符串预算）、JsonProfile 版本判定＋结构校验、canonical 写
 *         出（同语义同字节）与内容摘要（core ContentDigester）。
 *
 * 设计依据：见 Json.hpp 文件头（units/io.md §5.9.1~§5.9.4、§9.5、需求
 * REQ-12/OPT-12/PM-06/NFR-DEP-04/NFR-SEC-02、SA-12/NFR-MNT-03、任务契
 * 约 IO-T04 acceptance 1~3）。
 *
 * 实现结构（自上而下）：
 *   1. 常量与 UTF-8 严格校验 DFA（与 Csv.cpp 同源的公共域压缩表——两
 *      通道各自持有同表副本，出处与合流说明见表注）；
 *   2. 位置游标（行列跟踪——列按 UTF-8 字节计，JsonSourceSpan 口径）；
 *   3. JSON path 渲染（$.a.b/$[0]/$["键 名"]——诊断逐路径定位面）；
 *   4. 递归下降解析器 Parser（语法→受限 DOM：重复键/深度/字符串长度/
 *      数值溢出/取消窗口/位置区间逐项落地——§5.9.1 全表）；
 *   5. 版本判定（先于 schema——§5.9.2）＋结构校验器（profile 驱动、
 *      Preserve 子树透传、首违例按文档序即返——确定性）；
 *   6. canonical 序列化器（键序来源二态＋to_chars 最短表示＋2 空格缩进
 *      ＋LF——§5.9.3）；
 *   7. JsonProfileRegistry/JsonReader/JsonWriter 会话与工厂。
 *
 * 线程约束：reader 无状态并发安全（registry 只读）；writer 会话单线程。
 * 异常纪律：公共接口非抛出（§1.4）——实现体以 try/catch 包裹，任何标准
 * 库异常（含递归下降的深拷贝 bad_alloc）转为对应错误轨；防御性内部错误
 * 归 IO-FORMAT-INTERNAL（触及即报缺陷）。语法分析自身受深度预算约束，
 * 不存在栈溢出路径（解析递归深度 ≤ JsonDepth 硬上限 128 层）。
 */

#include <sdurws/ird/io/Json.hpp>

#include <sdurws/ird/io/Budget.hpp>
#include <sdurws/ird/io/IoDiagnostics.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sdurws::ird::io {

// JsonValue::findMember —— 线性查找（对象成员数受文档预算约束；解析保
// 证无重复键，命中至多一个）。定义放文件头段，供 DOM 消费方使用。
const JsonValue* JsonValue::findMember(std::string_view key) const noexcept
{
    if (type != Type::Object) {
        return nullptr;
    }
    for (const JsonMember& m : members) {
        if (std::string_view(m.key) == key) {
            return &m.value;
        }
    }
    return nullptr;
}

namespace {

// =====================================================================
// 常量（实现侧窗口与上限——均注明出处）
// =====================================================================

/// 取消检查点窗口，单位字节。64 KiB＝预算上限量级（512 MiB）的万分之一
/// 以下——检查开销可忽略（每窗口一次虚调用），而取消响应延迟上限被钉
/// 在"解析 64 KiB 所需时间"（§9.5 取消行为行"流式解析每块检查点"；
/// parseBytes 一次入内存后按消费字节窗口轮询——同语义的块检查点形态）。
inline constexpr std::uint64_t kCancelWindowBytes = 64u * 1024;

/// 文件读取循环的块大小，单位字节。64 KiB＝常规文件系统缓冲的整数倍、
/// 栈/堆均友好；取消检查点按块轮询（parse 的检查点语义，§9.5）。
inline constexpr std::size_t kFileReadChunkBytes = 64u * 1024;

/// 版本字段键名（§5.9.2 版本字段行原文 "schemaVersion"）。产品 JSON 文
/// 档必须顶层携带；manifest 等内部件不经 profile 校验故不受此约束。
constexpr std::string_view kSchemaVersionKey = "schemaVersion";

/// UTF-8 BOM 字节序列（EF BB BF——§5.9.1 编码行"容忍剥离"）。
constexpr std::string_view kUtf8Bom = "\xEF\xBB\xBF";

// =====================================================================
// UTF-8 严格校验 DFA（无 BOM 编码判定——§5.9.1 编码行"无效序列拒绝"）
// =====================================================================

/// DFA 状态值：0＝接受态，12＝拒绝态（字节序列非法：超长编码/代理区/
/// 超 U+10FFFF/悬空续字节）。与 Csv.cpp 同款语义。
inline constexpr std::uint32_t kUtf8Reject = 12;

/**
 * UTF-8 严格校验转移表——Björn Höhrmann 的经典压缩 DFA（公共域实现，
 * 与 Csv.cpp 的表逐字节同源：JSON 通道与 CSV 通道共用同一"严格 UTF-8"
 * 判定语义——IO-D09 不猜转码；两份副本暂不合流为 src/ 私有共享头，避免
 * 本任务触碰 IO-T03 已落位文件，合流留待后续治理任务）。
 * 用法：state = kUtf8Dfa[256 + state + kUtf8Dfa[byte]]，逐字节推进；
 * state == kUtf8Reject 即非法。
 */
const std::uint8_t kUtf8Dfa[] = {
    // 前 256 项：字节 → 字符类（减少转移表尺寸）。
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,  9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    8,8,2,2,2,2,2,2,2,2,2,2,2,2,2,2,  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
   10,3,3,3,3,3,3,3,3,3,3,3,3,4,3,3, 11,6,6,6,5,8,8,8,8,8,8,8,8,8,8,8,
    // 后 144 项：状态 × 字符类 → 下一状态。
    0,12,24,36,60,96,84,12,12,12,48,72, 12,12,12,12,12,12,12,12,12,12,12,12,
   12, 0,12,12,12,12,12, 0,12, 0,12,12, 12,24,12,12,12,12,12,24,12,24,12,12,
   12,12,12,12,12,12,12,24,12,12,12,12, 12,24,12,12,12,12,12,12,12,24,12,12,
   12,12,12,12,12,12,12,36,12,36,12,12, 12,36,12,12,12,12,12,36,12,36,12,12,
   12,36,12,12,12,12,12,12,12,12,12,12, 12,12,12,12,12,12,12,12,12,12,12,12,
    0,12,12,12,12,12,12,12, 0,12,12,12, 12,12,12,12, 0,12,12,12,12,12,12,12,
    0,12,12,12,12,12,12,12, 0,12,12,12, 12,12,12,12,12,12,12,12,12,12,12,12,
   12,12,12,12,12,12,12,12,12,12,12,12, 12,12,12,12,12,12,12,12,12,12,12,12,
};

/**
 * @brief 严格 UTF-8 校验（全文一遍 DFA）。
 *
 * JSON 通道与 CSV 通道的编码语义差异（§5.9.1 编码行 vs §5.2 编码行）：
 * CSV 接受 UTF-16LE/BE（BOM 判定后解码）；JSON **仅 UTF-8**——UTF-16
 * BOM 在剥离判定处即被拒绝（见 parseBytes 入口），无 BOM 的任意字节流
 * 经本函数判定，非法序列＝IO-FORMAT-JSON-ENCODING 稳定拒绝。
 *
 * @param bytes     [in] 待校验字节流（BOM 剥离后）
 * @param failIndex [out] 首个非法字节的下标（0 起——编码错误定位用）
 * @return true＝合法 UTF-8；false＝非法（failIndex 有效）
 */
bool strictUtf8Check(std::string_view bytes, std::size_t* failIndex)
{
    std::uint32_t state = 0;   // 起始＝接受态
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        const std::uint8_t b = static_cast<std::uint8_t>(bytes[i]);
        state = kUtf8Dfa[256 + state + kUtf8Dfa[b]];
        if (state == kUtf8Reject) {
            *failIndex = i;
            return false;
        }
    }
    // 悬空多字节序列（文档截断在多字节字符中段）同样非法——终态非接受。
    if (state != 0) {
        *failIndex = bytes.size();
        return false;
    }
    return true;
}

// =====================================================================
// std::error_code → io 资源错误码（文件读失败四分类——§4.2.5）
// =====================================================================

/**
 * @brief std::error_code → io 资源错误码（与 Csv.cpp 同名助手同款映射：
 *        不存在→NOT-FOUND、只读介质→READONLY、锁竞争→LOCK-CONFLICT、
 *        磁盘满→PACK-DISK-FULL、其余保守归 ACCESS-DENIED——"无法访问"
 *        语义最近；未合流私有头的理由同 kUtf8Dfa 表注）。
 */
IoErrorCode mapSystemError(const std::error_code& ec)
{
    if (ec == std::errc::no_such_file_or_directory) {
        return IoErrorCode::ResNotFound;
    }
    if (ec == std::errc::no_space_on_device || ec == std::errc::file_too_large) {
        return IoErrorCode::PackDiskFull;
    }
    if (ec == std::errc::read_only_file_system) {
        return IoErrorCode::ResReadonly;
    }
    if (ec == std::errc::device_or_resource_busy || ec == std::errc::no_lock_available
        || ec == std::errc::resource_unavailable_try_again) {
        return IoErrorCode::ResLockConflict;
    }
    return IoErrorCode::ResAccessDenied;
}

// =====================================================================
// 位置游标（行列跟踪——JsonSourceSpan 口径：列按 UTF-8 字节计）
// =====================================================================

/// 当前读取位置（1 起行列——与 JsonSourceSpan 同口径）。
struct SourcePos {
    std::uint32_t line = 1;
    std::uint32_t column = 1;
};

/**
 * @brief 字节游标：维护"当前字节位置"与"前一字节位置"的行列坐标。
 *
 * 行列推进规则：'\n' 换行（行+1、列复位 1）；'\r' 与 '\t' 按单字节推进
 * （JSON 词法中 '\r' 只能出现在空白位且后随 '\n'，作为列推进处理足够
 * 定位；'\t' 列宽按 1 字节计——确定性口径，不依赖终端制表位）。列＝
 * UTF-8 字节列（JsonSourceSpan 类注口径——多字节字符的续字节逐列推进）。
 */
class ByteCursor {
public:
    explicit ByteCursor(std::string_view buf) : m_buf(buf) {}

    std::size_t pos() const noexcept { return m_pos; }
    std::size_t size() const noexcept { return m_buf.size(); }
    bool eof() const noexcept { return m_pos >= m_buf.size(); }
    char peek() const noexcept { return m_pos < m_buf.size() ? m_buf[m_pos] : '\0'; }
    SourcePos lastPos() const noexcept { return m_last; }
    SourcePos curPos() const noexcept { return m_cur; }

    /// 全缓冲视图（词法 token 原文截取——数值 from_chars 输入）。
    std::string_view buf() const noexcept { return m_buf; }
    /// 指定下标字节（越界＝'\0'——前瞻用，调用方先以 size() 判界）。
    char bufAt(std::size_t i) const noexcept { return i < m_buf.size() ? m_buf[i] : '\0'; }

    /// 消费一字节并推进行列坐标。
    void advance() noexcept
    {
        if (m_pos < m_buf.size()) {
            m_last = m_cur;
            if (m_buf[m_pos] == '\n') {
                ++m_cur.line;
                m_cur.column = 1;
            } else {
                ++m_cur.column;
            }
            ++m_pos;
        }
    }

    /// 快进 n 字节（词法扫描数字/字面量 token 用——坐标语义同 advance）。
    void skip(std::size_t n) noexcept
    {
        for (std::size_t i = 0; i < n; ++i) {
            advance();
        }
    }

private:
    std::string_view m_buf;   ///< 被扫描缓冲（BOM 剥离后——坐标相对剥离后文档）
    std::size_t m_pos = 0;    ///< 当前字节下标（0 起）
    SourcePos m_cur{1, 1};    ///< 当前字节坐标（1 起行列）
    SourcePos m_last{1, 1};   ///< 前一已消费字节坐标（初始＝文档起点）
};

// =====================================================================
// JSON path 渲染（诊断逐路径定位——§5.9.4"码＋路径＋行/列位置"）
// =====================================================================

/**
 * @brief 解析/校验期维护的路径段栈与渲染（确定性口径）：
 *   - 根＝"$"；
 *   - 键段：键名仅含 [A-Za-z0-9_-] 时渲染 ".key"，否则渲染
 *     $["<键名>"]（内部 " 与 \ 转义——RFC 8259 字符串语义，确定性文本）；
 *   - 数组段："[<index>]"（十进制）。
 * 渲染在错误发生时一次完成（O(路径深)）——诊断构造不进热路径。
 */
class JsonPath {
public:
    /// 段类型。
    struct Segment {
        bool isIndex = false;   ///< true＝数组下标段；false＝对象键段
        std::size_t index = 0;  ///< isIndex 有效——数组下标（0 起）
        IoString key;           ///< 键段有效——解码后键名（UTF-8）
    };

    void pushKey(IoString key) { m_segs.push_back(Segment{false, 0, std::move(key)}); }
    void pushIndex(std::size_t i) { m_segs.push_back(Segment{true, i, {}}); }
    void pop() { m_segs.pop_back(); }

    /// 渲染为稳定文本（同路径同串——NFR-COR-02）。
    IoString render() const
    {
        if (m_segs.empty()) {
            return IoString("$");
        }
        IoString out;
        out.push_back('$');
        for (const Segment& s : m_segs) {
            if (s.isIndex) {
                out.push_back('[');
                // 下标十进制（无本地化）；size_t 最大 20 位——栈缓冲足够。
                std::array<char, 24> tmp{};
                const auto r = std::to_chars(tmp.data(), tmp.data() + tmp.size(), s.index);
                out.append(tmp.data(), static_cast<std::size_t>(r.ptr - tmp.data()));
                out.push_back(']');
            } else if (plainKey(s.key)) {
                out.push_back('.');
                out.append(s.key);
            } else {
                out.append("[\"");
                for (const char c : s.key) {
                    if (c == '"' || c == '\\') {
                        out.push_back('\\');   // 键名内 " 与 \ 转义（渲染层，不改键语义）
                    }
                    out.push_back(c);
                }
                out.append("\"]");
            }
        }
        return out;
    }

private:
    /// 键名是否可走 .key 简写形态（仅 ASCII 字母/数字/下划线/连字符）。
    static bool plainKey(const IoString& key)
    {
        if (key.empty()) {
            return false;   // 空键走引号形态（"$.": 歧义）
        }
        for (const char c : key) {
            const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
                            || (c >= '0' && c <= '9') || c == '_' || c == '-';
            if (!ok) {
                return false;
            }
        }
        return true;
    }

    std::vector<Segment> m_segs;   ///< 根到当前位置的段栈（文件序）
};

/// 位置区间装配（起点/闭端→JsonSourceSpan）。
JsonSourceSpan makeSpan(SourcePos begin, SourcePos end) noexcept
{
    JsonSourceSpan s;
    s.line = begin.line;
    s.column = begin.column;
    s.lineEnd = end.line;
    s.columnEnd = end.column;
    return s;
}

/// JSON 家族错误构造（params 键序 path/row/column——与 ioCodeDescriptors
/// JSON 族 paramSchema 对齐；区间闭端入 detail——开发级完整定位）。
IoError jsonError(IoErrorCode code, const JsonPath& path, SourcePos begin, SourcePos end,
                  std::string detail)
{
    IoError e;
    e.code = code;
    e.params.emplace_back("path", path.render());
    e.params.emplace_back("row", std::to_string(begin.line));
    e.params.emplace_back("column", std::to_string(begin.column));
    e.detail = std::move(detail);
    e.detail += " [end row=";
    e.detail += std::to_string(end.line);
    e.detail += " column=";
    e.detail += std::to_string(end.column);
    e.detail += "]";
    return e;
}

/// 版本族错误构造（params 键序 file-version/supported——与 ioCodeDescriptors
/// 版本码 paramSchema 对齐；supported 渲染为 "min..max"；升级指引数据＝
/// PM-06 执行侧：判定数据（文件版本/支持区间）随诊断交付，升级器归
/// project——detail 注明归属，io 不代行升级）。
IoError versionError(IoErrorCode code, std::int64_t fileVersion, std::int64_t minSupported,
                     std::int64_t maxSupported)
{
    IoError e;
    e.code = code;
    e.params.emplace_back("file-version", std::to_string(fileVersion));
    e.params.emplace_back("supported",
                          std::to_string(minSupported) + ".." + std::to_string(maxSupported));
    e.detail = "json version gate: read-only reject; upgrader ownership = project (PM-06); "
               "io supplies format probe data only";
    return e;
}

// =====================================================================
// 递归下降解析器（语法＋安全限制→受限 DOM——§5.9.1 全表逐行落地）
// =====================================================================

/// 解析期生效的安全限制（来源＝BudgetSpec::productDefault()——§4.5.1
/// 表默认值；guard 非 null 时另有记账，双轨一致的限额源见 JsonReader 注）。
struct JsonLimits {
    std::uint64_t docBytes;      ///< JsonDocBytes 限额（默认 64 MiB）
    std::uint64_t depth;         ///< JsonDepth 限额（默认 64 层）
    std::uint64_t stringChars;   ///< JsonStringChars 限额（默认 16 Mi 码点）
};

/**
 * @brief 受限递归下降解析器（一个实例一次解析；单线程栈对象）。
 *
 * 深度安全：递归深度受 JsonDepth 限额钳制（默认 64，硬上限 128——超限
 * 在递归进入前拒绝），不存在先深递归后检查的栈溢出窗口。位置区间：每
 * 个值的 span 覆盖其自身全部字节（含定界符）；成员 keySpan 覆盖键 token。
 */
class Parser {
public:
    Parser(std::string_view buf, const JsonLimits& limits, IoCancelToken* cancel)
        : m_cur(buf), m_lim(limits), m_cancel(cancel)
    {
    }

    /**
     * @brief 解析整个文档（根值＋尾随内容检查）。
     *
     * 尾随非空白字节＝语法错误（RFC 8259 单文档语义——两个拼接文档是
     * 常见注入/损坏形态，不猜测"取第一个"）。报告元数据随根装配。
     */
    IoResult<JsonDocument> run()
    {
        IoResult<JsonDocument> out;
        JsonPath path;
        IoResult<JsonValue> root = parseValue(1, path);
        if (!root) {
            out.error = root.error;
            return out;   // 失败：无部分 DOM 外泄（§9.5 后置）——value 保持默认构造
        }
        // 尾随检查：仅允许空白（skipWs 消费后必须 eof）。
        skipWs();
        if (!m_cur.eof()) {
            out.error = jsonError(IoErrorCode::FormatJsonSyntax, path, m_cur.curPos(),
                                  m_cur.curPos(), "json parse: trailing content after document");
            return out;
        }
        out.value.root = std::move(root.value);
        out.value.report.docBytes = m_cur.size();
        out.value.report.bomStripped = m_bomStripped;
        out.value.report.valueCount = m_valueCount;
        out.value.report.maxDepth = m_maxDepth;
        out.value.report.outOfRangeIntegers = m_outOfRangeIntegers;
        return out;
    }

    /// BOM 剥离标记（run 前由调用方处置——报告回填）。
    void setBomStripped() noexcept { m_bomStripped = true; }

private:
    // ---- 基础推进 ----

    /// 跳过空白（space/\t/\n/\r——RFC 8259 ws）。
    void skipWs()
    {
        while (!m_cur.eof()) {
            const char c = m_cur.peek();
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                m_cur.advance();
            } else {
                break;
            }
        }
    }

    /// 取消检查点：每个值节点解析前轮询（§9.5 取消行为行"流式解析每块
    /// 检查点"——值粒度＝最小检查单元，首个值前即生效；命中＝IO-CANCELLED
    /// 状态返回，非错误——UX-03）。
    bool cancelHit(IoError* out)
    {
        if (m_cancel == nullptr) {
            return false;
        }
        m_lastCancelPos = m_cur.pos();   // 窗口推进（观测面——保留供诊断）
        if (m_cancel->isCancelled()) {
            out->code = IoErrorCode::Cancelled;
            out->detail = "json parse: cancelled at value checkpoint";
            return true;
        }
        return false;
    }

    /// 语法错误快捷构造（当前位置单字节区间）。
    IoError syntaxError(const JsonPath& path, std::string detail)
    {
        return jsonError(IoErrorCode::FormatJsonSyntax, path, m_cur.curPos(), m_cur.curPos(),
                         std::move(detail));
    }

    // ---- 值解析（dispatch——语法层第一站）----

    /**
     * @brief 解析一个值（depth＝本值所在层，根＝1）。
     *
     * 层预算在递归进入前检查（先检查后递归——栈安全）；valueCount 计
     * 数含容器与标量（观测面）。每个分支返回的节点都带位置区间。
     */
    IoResult<JsonValue> parseValue(std::uint32_t depth, JsonPath& path)
    {
        IoResult<JsonValue> out;
        // 深度预算（§5.9.1 嵌套深度行）：先比后递归——比较型三要素
        // actual/limit/unit=levels（IO-SEC-BUDGET-JSON-DEPTH）。
        if (depth > m_lim.depth) {
            out.error = makeComparativeError(IoErrorCode::SecBudgetJsonDepth, depth, m_lim.depth,
                                             "levels", "json parse: nesting depth over budget");
            return out;
        }
        if (depth > m_maxDepth) {
            m_maxDepth = depth;   // 观测：实际最大深度（报告面）
        }
        if (cancelHit(&out.error)) {
            return out;
        }
        skipWs();
        if (m_cur.eof()) {
            out.error = syntaxError(path, "json parse: unexpected end of document");
            return out;
        }
        const SourcePos begin = m_cur.curPos();
        const char c = m_cur.peek();
        switch (c) {
        case '{':
            return parseObject(depth, path, begin);
        case '[':
            return parseArray(depth, path, begin);
        case '"':
            return parseString(path, begin);
        case 't':
            return parseLiteral(path, begin, "true", JsonValue::Type::Boolean, true);
        case 'f':
            return parseLiteral(path, begin, "false", JsonValue::Type::Boolean, false);
        case 'n':
            return parseLiteral(path, begin, "null", JsonValue::Type::Null, false);
        default:
            // 数值与非法记号分流：'-'/'.'/'+'/数字是数值字面量的合法或
            // 畸形起始——一律走数值路径（畸形形态归 NUMBER——§5.9.1 数
            // 值行"语法非法"码面）；NaN/Infinity/-Infinity 是"非法 JSON"
            // 的具名记号——同归 NUMBER；其余字节给 SYNTAX（表尾追加码
            // ——见 IoError.hpp 枚举注）。
            if (c == '-' || c == '.' || c == '+' || (c >= '0' && c <= '9')) {
                return parseNumber(path, begin);
            }
            if (c == 'N' || c == 'I') {
                return parseNonFiniteLiteral(path, begin);
            }
            out.error = syntaxError(path, "json parse: unexpected byte");
            return out;
        }
    }

    /// 具名非有限记号（NaN/Infinity/-Infinity——非法 JSON，§5.9.1 语法层
    /// 拒绝，码面 IO-FORMAT-JSON-NUMBER；不接受即"限制"本身——受限读写
    /// 器不提供非有限数值的承载形态）。
    IoResult<JsonValue> parseNonFiniteLiteral(const JsonPath& path, SourcePos begin)
    {
        IoResult<JsonValue> out;
        IoString word;
        while (!m_cur.eof()) {
            const char ch = m_cur.peek();
            if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')) {
                word.push_back(ch);
                m_cur.advance();
            } else {
                break;
            }
        }
        const bool nonFinite = word == "NaN" || word == "Infinity"
                               || (word == "-Infinity");   // '-' 已被外层拦截，防御性完备
        if (nonFinite) {
            out.error = jsonError(IoErrorCode::FormatJsonNumber, path, begin, m_cur.lastPos(),
                                  "json parse: NaN/Infinity literal is not valid JSON (io.md §5.9.1)");
            return out;
        }
        out.error = syntaxError(path, "json parse: unknown token");
        return out;
    }

    /// 字面量（true/false/null——精确匹配；前缀命中但尾随不符＝语法错）。
    IoResult<JsonValue> parseLiteral(const JsonPath& path, SourcePos begin,
                                     std::string_view word, JsonValue::Type type, bool boolVal)
    {
        IoResult<JsonValue> out;
        for (const char expect : word) {
            if (m_cur.eof() || m_cur.peek() != expect) {
                out.error = syntaxError(path, "json parse: malformed literal");
                return out;
            }
            m_cur.advance();
        }
        JsonValue v;
        v.type = type;
        v.boolValue = boolVal;
        v.span = makeSpan(begin, m_cur.lastPos());
        ++m_valueCount;
        out.value = std::move(v);
        return out;
    }

    // ---- 字符串（转义解码＋码点预算＋控制字符拒绝）----

    /**
     * @brief 解析字符串值（调用点：独立值或对象键——键路径复用本函数）。
     *
     * 落点（§5.9.1 逐行）：长度预算按**解码后码点数**计（\uXXXX 记 1、
     * 代理对合计 1——JsonStringChars 的 chars 口径）；原始控制字符
     * （<0x20）拒绝（RFC 8259 string 语法）；\u 转义严格校验（非十六
     * 进制/悬空代理对拒绝——不可交换文本不予承载）；已过 UTF-8 预检的
     * 非 ASCII 原文字节直通拷贝。
     *
     * @param countValue [in] true＝计入 valueCount（值位置调用）；false＝
     *                   对象键 token（键不是值——报告计数不膨胀）
     */
    IoResult<JsonValue> parseString(JsonPath& path, SourcePos begin, bool countValue = true)
    {
        IoResult<JsonValue> out;
        m_cur.advance();   // 消费开引号
        IoString text;
        std::uint64_t chars = 0;   // 解码后码点计数（预算面）
        for (;;) {
            if (m_cur.eof()) {
                out.error = jsonError(IoErrorCode::FormatJsonSyntax, path, begin, m_cur.lastPos(),
                                      "json parse: unterminated string");
                return out;
            }
            const char c = m_cur.peek();
            if (c == '"') {
                m_cur.advance();   // 闭引号
                break;
            }
            if (c == '\\') {
                m_cur.advance();
                if (m_cur.eof()) {
                    out.error = jsonError(IoErrorCode::FormatJsonSyntax, path, begin,
                                          m_cur.lastPos(), "json parse: dangling escape");
                    return out;
                }
                const char esc = m_cur.peek();
                m_cur.advance();
                switch (esc) {
                case '"': text.push_back('"'); ++chars; break;
                case '\\': text.push_back('\\'); ++chars; break;
                case '/': text.push_back('/'); ++chars; break;
                case 'b': text.push_back('\b'); ++chars; break;
                case 'f': text.push_back('\f'); ++chars; break;
                case 'n': text.push_back('\n'); ++chars; break;
                case 'r': text.push_back('\r'); ++chars; break;
                case 't': text.push_back('\t'); ++chars; break;
                case 'u': {
                    // \uXXXX：先取 4 个十六进制位；高代理必须后随 \uDC00-DFFF
                    // 组对（RFC 8259 可交换文本约束——悬空代理拒绝）。
                    std::uint32_t cp = 0;
                    IoError hexErr;
                    if (!readHex4(&cp, &hexErr)) {
                        out.error = jsonError(IoErrorCode::FormatJsonSyntax, path, begin,
                                              m_cur.lastPos(),
                                              "json parse: bad \\u escape" + detailSuffix(hexErr));
                        return out;
                    }
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        // 高代理：期望紧随 "\uXXXX" 的低代理。
                        if (m_cur.pos() + 1 < m_cur.size() && m_cur.peek() == '\\'
                            && m_cur.bufAt(m_cur.pos() + 1) == 'u') {
                            m_cur.advance();   // '\'
                            m_cur.advance();   // 'u'
                            std::uint32_t low = 0;
                            if (!readHex4(&low, &hexErr) || low < 0xDC00 || low > 0xDFFF) {
                                out.error = jsonError(IoErrorCode::FormatJsonSyntax, path, begin,
                                                      m_cur.lastPos(),
                                                      "json parse: unpaired surrogate escape");
                                return out;
                            }
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);   // 合成码点
                        } else {
                            out.error = jsonError(IoErrorCode::FormatJsonSyntax, path, begin,
                                                  m_cur.lastPos(),
                                                  "json parse: unpaired surrogate escape");
                            return out;
                        }
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        out.error = jsonError(IoErrorCode::FormatJsonSyntax, path, begin,
                                              m_cur.lastPos(),
                                              "json parse: unpaired surrogate escape");
                        return out;
                    }
                    appendUtf8(text, cp);
                    ++chars;   // 一个（合成后）码点计 1——JsonStringChars 口径
                    break;
                }
                default:
                    out.error = jsonError(IoErrorCode::FormatJsonSyntax, path, begin,
                                          m_cur.lastPos(), "json parse: unknown escape");
                    return out;
                }
            } else if (static_cast<unsigned char>(c) < 0x20) {
                out.error = jsonError(IoErrorCode::FormatJsonSyntax, path, begin,
                                      m_cur.lastPos(),
                                      "json parse: raw control char inside string");
                return out;
            } else {
                text.push_back(c);   // 非 ASCII 直通（UTF-8 已预检——原样保留）
                if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) {
                    ++chars;         // 非 UTF-8 续字节＝新码点首字节
                }
                m_cur.advance();
                // 不 continue——直通字节同样落到下方码点预算检查（普通长
                // 字符串的预算防线；escape 分支亦经此检查）。
            }
            // 码点预算（§5.9.1 大字符串行）：超限即拒——比较型三要素
            // actual/limit/unit=chars（IO-SEC-BUDGET-JSON-STRING）。
            if (chars > m_lim.stringChars) {
                out.error = makeComparativeError(IoErrorCode::SecBudgetJsonString, chars,
                                                 m_lim.stringChars, "chars",
                                                 "json parse: string length over budget");
                out.error.params.emplace_back("path", path.render());
                out.error.params.emplace_back("row", std::to_string(begin.line));
                out.error.params.emplace_back("column", std::to_string(begin.column));
                return out;
            }
        }
        JsonValue v;
        v.type = JsonValue::Type::String;
        v.stringValue = std::move(text);
        v.span = makeSpan(begin, m_cur.lastPos());
        if (countValue) {
            ++m_valueCount;   // 值位置才计数（对象键不计——parseString 注）
        }
        out.value = std::move(v);
        return out;
    }

    /// 读恰好 4 个十六进制位（大小写均可；失败原因入 err.detail）。
    bool readHex4(std::uint32_t* out, IoError* err)
    {
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            if (m_cur.eof()) {
                err->detail = "truncated \\u escape";
                return false;
            }
            const char h = m_cur.peek();
            m_cur.advance();
            v <<= 4;
            if (h >= '0' && h <= '9') {
                v |= static_cast<std::uint32_t>(h - '0');
            } else if (h >= 'a' && h <= 'f') {
                v |= static_cast<std::uint32_t>(h - 'a' + 10);
            } else if (h >= 'A' && h <= 'F') {
                v |= static_cast<std::uint32_t>(h - 'A' + 10);
            } else {
                err->detail = "non-hex digit in \\u escape";
                return false;
            }
        }
        *out = v;
        return true;
    }

    /// 码点 UTF-8 编码追加（1~4 字节——标准映射，无代理区输入）。
    static void appendUtf8(IoString& out, std::uint32_t cp)
    {
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

    /// 附加 err.detail 到文本（detail 拼接小助手——错误信息可读性）。
    static std::string detailSuffix(const IoError& err)
    {
        return err.detail.empty() ? std::string() : " (" + err.detail + ")";
    }

    // ---- 数值（语法扫描→Integer/Real 二分→溢出拒绝/超范围透传）----

    /**
     * @brief 解析数值（§5.9.1 数值行全语义）：
     *   - 语法严格按 RFC 8259 number 词法扫描（前导零/孤立 '.'/'+' 首位
     *     等非法形态＝SYNTAX）；
     *   - 整数形态（无小数/指数）：from_chars 到 int64——成功＝Integer
     *     无损；超范围＝按原文 String 透传＋report 计数（不静默截断）；
     *   - 实数形态：from_chars 到 double——成功＝Real；out_of_range 时按
     *     量级估算分流：溢出（>DBL_MAX 量级）＝IO-FORMAT-JSON-NUMBER 拒
     *     绝（1e999——V08），下溢（<denorm 量级）＝接受为 0（确定性口径，
     *     detail 不另行提示——JSON 数值语义按 IEEE754 最近值）。
     */
    IoResult<JsonValue> parseNumber(JsonPath& path, SourcePos begin)
    {
        IoResult<JsonValue> out;
        // 先行拦截 "-Infinity"（§5.9.1 NaN 行——'-' 后跟 Infinity 同为
        // 非法 JSON 具名记号）。
        if (m_cur.peek() == '-' && m_cur.pos() + 1 < m_cur.size()
            && m_cur.bufAt(m_cur.pos() + 1) == 'I') {
            // 贪婪吞词后按具名记号拒绝（错误定位覆盖整个记号）。
            m_cur.advance();
            IoResult<JsonValue> named = parseNonFiniteLiteral(path, begin);
            if (!named) {
                return named;
            }
        }
        const std::size_t start = m_cur.pos();
        // 语法扫描：'-'? int frac? exp?（逐段严格，deviation 即 SYNTAX）。
        if (m_cur.peek() == '-') {
            m_cur.advance();
        }
        if (m_cur.eof()) {
            out.error = jsonError(IoErrorCode::FormatJsonNumber, path, m_cur.curPos(), m_cur.curPos(), "json parse: truncated number");
            return out;
        }
        if (m_cur.peek() == '0') {
            m_cur.advance();
            // 0 后不得再跟数字（前导零非法——RFC 8259 int 尾注）。
            if (!m_cur.eof() && m_cur.peek() >= '0' && m_cur.peek() <= '9') {
                out.error = jsonError(IoErrorCode::FormatJsonNumber, path, m_cur.curPos(), m_cur.curPos(), "json parse: leading zero in number");
                return out;
            }
        } else if (m_cur.peek() >= '1' && m_cur.peek() <= '9') {
            while (!m_cur.eof() && m_cur.peek() >= '0' && m_cur.peek() <= '9') {
                m_cur.advance();
            }
        } else {
            out.error = jsonError(IoErrorCode::FormatJsonNumber, path, m_cur.curPos(), m_cur.curPos(), "json parse: malformed number");
            return out;
        }
        bool hasFrac = false;
        if (!m_cur.eof() && m_cur.peek() == '.') {
            hasFrac = true;
            m_cur.advance();
            if (m_cur.eof() || m_cur.peek() < '0' || m_cur.peek() > '9') {
                out.error = jsonError(IoErrorCode::FormatJsonNumber, path, m_cur.curPos(), m_cur.curPos(), "json parse: malformed fraction");
                return out;
            }
            while (!m_cur.eof() && m_cur.peek() >= '0' && m_cur.peek() <= '9') {
                m_cur.advance();
            }
        }
        bool hasExp = false;
        if (!m_cur.eof() && (m_cur.peek() == 'e' || m_cur.peek() == 'E')) {
            hasExp = true;
            m_cur.advance();
            if (!m_cur.eof() && (m_cur.peek() == '+' || m_cur.peek() == '-')) {
                m_cur.advance();
            }
            if (m_cur.eof() || m_cur.peek() < '0' || m_cur.peek() > '9') {
                out.error = jsonError(IoErrorCode::FormatJsonNumber, path, m_cur.curPos(), m_cur.curPos(), "json parse: malformed exponent");
                return out;
            }
            while (!m_cur.eof() && m_cur.peek() >= '0' && m_cur.peek() <= '9') {
                m_cur.advance();
            }
        }
        const std::string_view token = m_cur.buf().substr(start, m_cur.pos() - start);
        const SourcePos end = m_cur.lastPos();

        if (!hasFrac && !hasExp) {
            // 整数形态：int64 无损 or 原文透传（§5.9.1 数值行——不静默截断）。
            std::int64_t iv = 0;
            const auto r = std::from_chars(token.data(), token.data() + token.size(), iv, 10);
            JsonValue v;
            if (r.ec == std::errc::result_out_of_range) {
                // 超范围整数：原文 String 透传＋报告计数（"＋提示"面）。
                v.type = JsonValue::Type::String;
                v.stringValue.assign(token);
                ++m_outOfRangeIntegers;
            } else {
                v.type = JsonValue::Type::Integer;
                v.integerValue = iv;
            }
            v.span = makeSpan(begin, end);
            ++m_valueCount;
            out.value = std::move(v);
            return out;
        }

        // 实数形态：from_chars（general 格式——无前导 '+'，与 JSON 词法相容）。
        double dv = 0.0;
        const auto r = std::from_chars(token.data(), token.data() + token.size(), dv);
        if (r.ec == std::errc::result_out_of_range) {
            // 方向分流：溢出拒绝、下溢接受为 0。量级估算＝小数点前有效
            // 位数-1 ＋ 指数值（钳位扫描，精度足以分辨 1e308 边界两侧）。
            const std::int64_t magnitude = estimateMagnitude(token);
            if (magnitude > 308) {
                out.error = jsonError(IoErrorCode::FormatJsonNumber, path, begin, end,
                                      "json parse: number overflow (out of IEEE754 double range)");
                return out;
            }
            dv = 0.0;   // 下溢：IEEE754 最近值语义＝0（parseNumber 类注）
        } else if (r.ec != std::errc()) {
            out.error = jsonError(IoErrorCode::FormatJsonNumber, path, begin, end,
                                  "json parse: number conversion failed");
            return out;
        }
        JsonValue v;
        v.type = JsonValue::Type::Real;
        v.realValue = dv;
        v.span = makeSpan(begin, end);
        ++m_valueCount;
        out.value = std::move(v);
        return out;
    }

    /**
     * @brief 估算数值 token 的十进制量级（10^magnitude 量级）。
     *
     * magnitude ≈（小数点前有效位数 − 1）＋ 显式指数；无显式指数时不调
     * 用（整数形态走 from_chars(int64) 路径）。指数位数钳位扫描（防指数
     * 本身溢出）；位数不足精度场景（全 0 前导）钳 0——下溢方向恒 ≤308，
     * 不会误判为溢出。
     */
    static std::int64_t estimateMagnitude(std::string_view token)
    {
        std::size_t i = 0;
        if (i < token.size() && token[i] == '-') {
            ++i;
        }
        std::int64_t intDigits = 0;
        while (i < token.size() && token[i] >= '0' && token[i] <= '9') {
            ++intDigits;
            ++i;
        }
        std::int64_t fracDigits = 0;
        bool significantInt = intDigits > 0 && token[i - intDigits] != '0';   // "0.xxx" 整数部分非有效
        if (intDigits == 1 && !token.empty() && token[0] == '0') {
            significantInt = false;
        }
        bool anySigDigit = significantInt;   // 全零尾数（0.000…）恒不溢出——方向哨兵
        if (i < token.size() && token[i] == '.') {
            ++i;
            // 小数部分：若整数部分非有效，量级由首个非零小数位决定（负）。
            std::int64_t leadZeros = 0;
            bool anySig = false;
            while (i < token.size() && token[i] >= '0' && token[i] <= '9') {
                if (!anySig) {
                    if (token[i] == '0') {
                        ++leadZeros;
                    } else {
                        anySig = true;
                        anySigDigit = true;
                    }
                }
                ++i;
            }
            fracDigits = leadZeros;   // 供无有效整数位时取负量级
        }
        std::int64_t expVal = 0;
        if (i < token.size() && (token[i] == 'e' || token[i] == 'E')) {
            ++i;
            bool negExp = false;
            if (i < token.size() && (token[i] == '+' || token[i] == '-')) {
                negExp = token[i] == '-';
                ++i;
            }
            int expDigits = 0;
            while (i < token.size() && token[i] >= '0' && token[i] <= '9') {
                if (expVal < 100000) {   // 钳位累加（指数超 1e5 量级判定已无歧义）
                    expVal = expVal * 10 + (token[i] - '0');
                }
                ++expDigits;
                ++i;
            }
            (void)expDigits;
            if (negExp) {
                expVal = -expVal;
            }
        }
        if (significantInt) {
            return (intDigits - 1) + expVal;
        }
        if (fracDigits > 0) {
            return expVal - fracDigits - 1;
        }
        // 兜底：有效整数位与有效小数位都不存在（全零尾数如 0e999999、
        // 0.00e999）——数值恒为 0，永不溢出（返回大负量级强制走"下溢接
        // 受为 0"分支——防全零尾数被指数误判为溢出）。
        if (!anySigDigit) {
            return -1000000;
        }
        return expVal;   // 其余形态——下溢方向，钳 0 即可
    }

    // ---- 容器（对象/数组——深度递归＋重复键拒绝）----

    /**
     * @brief 解析对象（§5.9.1 重复键行：同层同名键第二次出现即拒绝——
     *        解码后键名比对（\u 转义不改变键身份）；定位＝第二次出现的
     *        键 token 区间（V07"错误码＋JSON 路径＋行列区间"）。
     */
    IoResult<JsonValue> parseObject(std::uint32_t depth, JsonPath& path, SourcePos begin)
    {
        IoResult<JsonValue> out;
        m_cur.advance();   // '{'
        JsonValue obj;
        obj.type = JsonValue::Type::Object;
        // 重复键检测表：解码后键名 → 已有成员下标。作用域限本层（同层
        // 语义）；容量受文档预算约束（每键至少 4 字节开销——键数上界由
        // docBytes 钳制，表内存有界）。
        std::unordered_map<IoString, std::size_t> seen;
        skipWs();
        if (!m_cur.eof() && m_cur.peek() == '}') {
            m_cur.advance();
            obj.span = makeSpan(begin, m_cur.lastPos());
            ++m_valueCount;
            out.value = std::move(obj);
            return out;
        }
        for (;;) {
            skipWs();
            if (m_cur.eof() || m_cur.peek() != '"') {
                out.error = jsonError(IoErrorCode::FormatJsonSyntax, path, begin,
                                      m_cur.lastPos(),
                                      "json parse: expected object key or '}'");
                return out;
            }
            const SourcePos keyBegin = m_cur.curPos();
            IoResult<JsonValue> keyVal = parseString(path, keyBegin, /*countValue=*/false);
            if (!keyVal) {
                return keyVal;   // 键必须为字符串——语法/预算错误直接上抛
            }
            const SourcePos keyEnd = m_cur.lastPos();
            const IoString key = std::move(keyVal.value.stringValue);
            skipWs();
            if (m_cur.eof() || m_cur.peek() != ':') {
                out.error = syntaxError(path, "json parse: expected ':' after object key");
                return out;
            }
            m_cur.advance();
            // 值解析（路径段＝键；深度+1）。
            path.pushKey(key);
            IoResult<JsonValue> val = parseValue(depth + 1, path);
            path.pop();
            if (!val) {
                return val;
            }
            // 重复键：插入前查表（同层第二次出现即拒——静默覆盖即数据丢
            // 失，§5.9.1；定位＝第二次键 token 区间）。
            const auto [it, inserted] = seen.emplace(key, obj.members.size());
            if (!inserted) {
                out.error = jsonError(IoErrorCode::FormatJsonDupKey, path, keyBegin, keyEnd,
                                      "json parse: duplicate object key (second occurrence rejected)");
                return out;
            }
            JsonMember m;
            m.key = key;
            m.keySpan = makeSpan(keyBegin, keyEnd);
            m.value = std::move(val.value);
            obj.members.push_back(std::move(m));
            skipWs();
            if (m_cur.eof()) {
                out.error = jsonError(IoErrorCode::FormatJsonSyntax, path, begin,
                                      m_cur.lastPos(), "json parse: unterminated object");
                return out;
            }
            if (m_cur.peek() == ',') {
                m_cur.advance();
                continue;
            }
            if (m_cur.peek() == '}') {
                m_cur.advance();
                obj.span = makeSpan(begin, m_cur.lastPos());
                ++m_valueCount;
                out.value = std::move(obj);
                return out;
            }
            out.error = syntaxError(path, "json parse: expected ',' or '}' in object");
            return out;
        }
    }

    /// 解析数组（元素递归；路径段＝下标；深度+1——栈安全同对象）。
    IoResult<JsonValue> parseArray(std::uint32_t depth, JsonPath& path, SourcePos begin)
    {
        IoResult<JsonValue> out;
        m_cur.advance();   // '['
        JsonValue arr;
        arr.type = JsonValue::Type::Array;
        skipWs();
        if (!m_cur.eof() && m_cur.peek() == ']') {
            m_cur.advance();
            arr.span = makeSpan(begin, m_cur.lastPos());
            ++m_valueCount;
            out.value = std::move(arr);
            return out;
        }
        std::size_t index = 0;
        for (;;) {
            path.pushIndex(index);
            IoResult<JsonValue> val = parseValue(depth + 1, path);
            path.pop();
            if (!val) {
                return val;
            }
            arr.items.push_back(std::move(val.value));
            skipWs();
            if (m_cur.eof()) {
                out.error = jsonError(IoErrorCode::FormatJsonSyntax, path, begin,
                                      m_cur.lastPos(), "json parse: unterminated array");
                return out;
            }
            if (m_cur.peek() == ',') {
                m_cur.advance();
                ++index;
                continue;
            }
            if (m_cur.peek() == ']') {
                m_cur.advance();
                arr.span = makeSpan(begin, m_cur.lastPos());
                ++m_valueCount;
                out.value = std::move(arr);
                return out;
            }
            out.error = syntaxError(path, "json parse: expected ',' or ']' in array");
            return out;
        }
    }

    // ---- 状态 ----

    ByteCursor m_cur;             ///< 字节游标（BOM 剥离后缓冲）
    JsonLimits m_lim;             ///< 安全限制（productDefault 口径——见 JsonLimits 注）
    IoCancelToken* m_cancel;      ///< 取消令牌（null＝不可取消——调用方所有）
    std::uint64_t m_lastCancelPos = 0;   ///< 上次取消检查的消费位置（窗口轮询）
    bool m_bomStripped = false;   ///< BOM 剥离标记（报告回填）
    std::uint64_t m_valueCount = 0;      ///< 值节点计数（观测面）
    std::uint32_t m_maxDepth = 0;        ///< 实际最大深度（观测面）
    std::uint64_t m_outOfRangeIntegers = 0;  ///< 超范围整数透传计数（提示面）
};

// =====================================================================
// 版本判定＋结构校验器（§5.9.2——版本先于一切 schema 校验；profile 驱动）
// =====================================================================

/**
 * @brief 版本判定（parse 后、schema 校验前——§5.9.2 顺序硬约束）。
 *
 * 判定链（product JSON 文档约定根为 Object）：
 *   1. 根非 Object 或无 "schemaVersion" 成员 → IO-FORMAT-JSON-VERSION-
 *      MISSING（无版本字段的文档无法参与版本线管理——拒绝而非猜测）；
 *   2. 版本值非 Integer → IO-FORMAT-JSON-VERSION-TYPE（布尔/字符串/
 *      实数形态一律拒——版本线是整数序，manifest 的字符串版式
 *      "ird-pack-manifest/1" 走包通道自有判定，不经本函数）；
 *   3. 版本 ∉ supportedVersions：> 最大→FUTURE（写方版本线高于本实现
 *      ——稳定只读拒绝＋升级指引数据）；< 最小→LEGACY（同口径）；
 *      界内空洞→FUTURE（本实现未登记的中间版本，语义不猜测——
 *      JsonProfile 类注）。
 *
 * params＝file-version/supported（PM-06 执行侧的格式探测数据——判定与
 * 升级器归 project，io 不代行升级，versionError 注）。
 */
IoError checkSchemaVersion(const JsonValue& root, const JsonProfile& profile)
{
    if (root.type != JsonValue::Type::Object) {
        // 根非对象：无版本字段承载位——按缺失处置（路径 "$" 无定位）。
        IoError e;
        e.code = IoErrorCode::FormatJsonVersionMissing;
        e.params.emplace_back("file-version", "none");
        e.params.emplace_back("supported",
                              std::to_string(profile.supportedVersions.front()) + ".."
                                  + std::to_string(profile.supportedVersions.back()));
        e.detail = "json version gate: document root is not an object (no schemaVersion carrier)";
        return e;
    }
    const JsonValue* v = root.findMember(kSchemaVersionKey);
    if (v == nullptr) {
        IoError e;
        e.code = IoErrorCode::FormatJsonVersionMissing;
        e.params.emplace_back("file-version", "none");
        e.params.emplace_back("supported",
                              std::to_string(profile.supportedVersions.front()) + ".."
                                  + std::to_string(profile.supportedVersions.back()));
        e.detail = "json version gate: missing top-level schemaVersion";
        return e;
    }
    if (v->type != JsonValue::Type::Integer) {
        IoError e;
        e.code = IoErrorCode::FormatJsonVersionType;
        e.params.emplace_back("file-version", "non-integer");
        e.params.emplace_back("supported",
                              std::to_string(profile.supportedVersions.front()) + ".."
                                  + std::to_string(profile.supportedVersions.back()));
        e.detail = "json version gate: schemaVersion must be an integer";
        return e;
    }
    const std::int64_t fv = v->integerValue;
    const std::int64_t lo = profile.supportedVersions.front();
    const std::int64_t hi = profile.supportedVersions.back();
    for (const std::int64_t supported : profile.supportedVersions) {
        if (supported == fv) {
            return IoError{};   // 版本在册——通过（先于一切 schema 校验）
        }
    }
    if (fv > hi) {
        return versionError(IoErrorCode::FormatJsonVersionFuture, fv, lo, hi);
    }
    if (fv < lo) {
        return versionError(IoErrorCode::FormatJsonVersionLegacy, fv, lo, hi);
    }
    return versionError(IoErrorCode::FormatJsonVersionFuture, fv, lo, hi);   // 界内空洞→FUTURE
}

// =====================================================================
// 结构校验器（profile 驱动——首违例按文档序即返，§5.9.2/§5.9.4）
// =====================================================================

/// profile 形态约束的稳定 token（TYPE 错误 expected 参数的确定性文本）。
std::string_view shapeTypeToken(JsonValueType t) noexcept
{
    switch (t) {
    case JsonValueType::Any: return "any";
    case JsonValueType::Null: return "null";
    case JsonValueType::Boolean: return "boolean";
    case JsonValueType::Integer: return "integer";
    case JsonValueType::Number: return "number";
    case JsonValueType::String: return "string";
    case JsonValueType::Array: return "array";
    case JsonValueType::Object: return "object";
    }
    return "any";
}

/// DOM 值形态的稳定 token（TYPE 错误 actual 参数）。
std::string_view domTypeToken(JsonValue::Type t) noexcept
{
    switch (t) {
    case JsonValue::Type::Null: return "null";
    case JsonValue::Type::Boolean: return "boolean";
    case JsonValue::Type::Integer: return "integer";
    case JsonValue::Type::Real: return "real";
    case JsonValue::Type::String: return "string";
    case JsonValue::Type::Array: return "array";
    case JsonValue::Type::Object: return "object";
    }
    return "null";
}

/**
 * @brief 校验一个值（shape 非 null；递归）。
 *
 * 检查序（确定性首错）：①节点自身形态（type）→②值域（enum）→③数值
 * 范围（range——比较型 actual/limit/unit）→④数组长度（itemBounds）→
 * ⑤子结构（数组元素/对象成员——按文档序）→⑥必填缺失（收尾汇总点，
 * 缺失键按声明序取首个）。该顺序保证：同输入恒同首错（NFR-COR-01）。
 *
 * Preserve 子树在成员遍历处跳过（不进入本函数——透传保留，§5.9.2）。
 */
IoResult<void> validateValue(const JsonValue& v, const JsonShape& shape, JsonPath& path)
{
    IoResult<void> out;
    const SourcePos at{v.span.line, v.span.column};
    const SourcePos atEnd{v.span.lineEnd, v.span.columnEnd};

    // ① 形态约束（Any 跳过；Number 命中 Integer/Real 双形态——1 与 1.0
    // 同数值语义）。
    bool typeOk = true;
    switch (shape.type) {
    case JsonValueType::Any: break;
    case JsonValueType::Null: typeOk = v.isNull(); break;
    case JsonValueType::Boolean: typeOk = v.isBoolean(); break;
    case JsonValueType::Integer: typeOk = v.isInteger(); break;
    case JsonValueType::Number: typeOk = v.isNumber(); break;
    case JsonValueType::String: typeOk = v.isString(); break;
    case JsonValueType::Array: typeOk = v.isArray(); break;
    case JsonValueType::Object: typeOk = v.isObject(); break;
    }
    if (!typeOk) {
        out.error = jsonError(IoErrorCode::FormatJsonType, path, at, atEnd,
                              "json validate: type mismatch");
        out.error.params.emplace_back("expected", std::string(shapeTypeToken(shape.type)));
        out.error.params.emplace_back("actual", std::string(domTypeToken(v.type)));
        return out;
    }

    // ② 值域枚举（String 字节比对/Integer 数值比对——enum 注）。
    if (!shape.enumValues.empty()) {
        bool hit = false;
        if (v.isString()) {
            for (const IoString& e : shape.enumValues) {
                if (e == v.stringValue) {
                    hit = true;
                    break;
                }
            }
        } else if (v.isInteger()) {
            for (const IoString& e : shape.enumValues) {
                // 枚举文本按 int64 解析（profile 声明侧静态契约——解析失
                // 败的枚举项不参与匹配；装配期由格式所有者自检）。
                std::int64_t ev = 0;
                const auto r = std::from_chars(e.data(), e.data() + e.size(), ev, 10);
                if (r.ec == std::errc() && r.ptr == e.data() + e.size() && ev == v.integerValue) {
                    hit = true;
                    break;
                }
            }
        }
        if (!hit) {
            out.error = jsonError(IoErrorCode::FormatJsonType, path, at, atEnd,
                                  "json validate: value outside declared enum (value-domain mismatch)");
            IoString expected;
            for (std::size_t i = 0; i < shape.enumValues.size(); ++i) {
                if (i > 0) {
                    expected.push_back('|');
                }
                expected.append(shape.enumValues[i]);
            }
            out.error.params.emplace_back("expected", std::move(expected));
            out.error.params.emplace_back(
                "actual", v.isString() ? v.stringValue : std::to_string(v.integerValue));
            return out;
        }
    }

    // ③ 数值范围（§5.9.2 类型/范围行"比较型：实际/期望/单位"——闭区间）。
    if (shape.hasRange && v.isNumber()) {
        const double x = v.isInteger() ? static_cast<double>(v.integerValue) : v.realValue;
        if (x < shape.rangeMin || x > shape.rangeMax) {
            out.error = jsonError(IoErrorCode::FormatJsonRange, path, at, atEnd,
                                  "json validate: value outside declared range");
            // 比较型三要素（actual/limit/unit——§4.5.2/diagnostics §8.6）
            // 以 params 追加键交付；数值文本化经 to_chars（无本地化）。
            std::array<char, 64> tmp{};
            auto r = std::to_chars(tmp.data(), tmp.data() + tmp.size(), x);
            out.error.params.emplace_back("actual",
                                          std::string(tmp.data(), static_cast<std::size_t>(r.ptr - tmp.data())));
            r = std::to_chars(tmp.data(), tmp.data() + tmp.size(), shape.rangeMin);
            const std::string minText(tmp.data(), static_cast<std::size_t>(r.ptr - tmp.data()));
            r = std::to_chars(tmp.data(), tmp.data() + tmp.size(), shape.rangeMax);
            const std::string maxText(tmp.data(), static_cast<std::size_t>(r.ptr - tmp.data()));
            out.error.params.emplace_back("limit", "[" + minText + "," + maxText + "]");
            out.error.params.emplace_back("unit", shape.rangeUnit);
            return out;
        }
    }

    // ④ 数组长度约束（越界归 RANGE 比较型——unit=count）。
    if (shape.hasItemBounds && v.isArray()) {
        const std::uint64_t n = v.items.size();
        if (n < shape.minItems || n > shape.maxItems) {
            out.error = jsonError(IoErrorCode::FormatJsonRange, path, at, atEnd,
                                  "json validate: array length outside declared bounds");
            out.error.params.emplace_back("actual", std::to_string(n));
            out.error.params.emplace_back("limit",
                                          "[" + std::to_string(shape.minItems) + ","
                                              + std::to_string(shape.maxItems) + "]");
            out.error.params.emplace_back("unit", std::string("count"));
            return out;
        }
    }

    // ⑤ 子结构递归。
    if (v.isArray()) {
        if (shape.items != nullptr) {
            for (std::size_t i = 0; i < v.items.size(); ++i) {
                path.pushIndex(i);
                const IoResult<void> r = validateValue(v.items[i], *shape.items, path);
                path.pop();
                if (!r) {
                    return r;   // 首错即返（文档序——元素下标升序）
                }
            }
        }
    } else if (v.isObject()) {
        // 成员按文档序遍历（DOM 保序——首错＝文件中最先出现的问题）。
        for (const JsonMember& m : v.members) {
            const JsonProperty* prop = nullptr;
            for (const JsonProperty& p : shape.properties) {
                if (p.key == m.key) {
                    prop = &p;
                    break;
                }
            }
            if (prop == nullptr) {
                // 未声明键：profile 未知字段策略（唯一值 Reject——§5.9.2
                // 未知字段行"NFR-DEP-04 schema 演进必须走版本升级"）。
                // 定位路径含该键本身（push 后渲染再弹出——V07"JSON 路径"
                // 指向违规键）。
                path.pushKey(m.key);
                out.error = jsonError(IoErrorCode::FormatJsonUnknown, path,
                                      SourcePos{m.keySpan.line, m.keySpan.column},
                                      SourcePos{m.keySpan.lineEnd, m.keySpan.columnEnd},
                                      "json validate: unknown key (not declared in profile)");
                path.pop();
                return out;
            }
            if (prop->preserve) {
                continue;   // Preserve 子树：整棵透传（扩展块——不进子校验）
            }
            if (prop->shape != nullptr) {
                path.pushKey(m.key);
                const IoResult<void> r = validateValue(m.value, *prop->shape, path);
                path.pop();
                if (!r) {
                    return r;
                }
            }
        }
        // ⑥ 必填缺失（成员遍历后收尾——缺失键按声明序取首个定位到对象）。
        for (const JsonProperty& p : shape.properties) {
            if (!p.required) {
                continue;
            }
            bool present = false;
            for (const JsonMember& m : v.members) {
                if (m.key == p.key) {
                    present = true;
                    break;
                }
            }
            if (!present) {
                // io 不注入默认值（§5.9.2——默认值语义归业务单元）；定位
                // ＝对象起始（缺失项无自有位置）。
                out.error = jsonError(IoErrorCode::FormatJsonRequired, path, at, atEnd,
                                      "json validate: required key missing (io does not inject defaults)");
                out.error.params.emplace_back("key", p.key);
                return out;
            }
        }
    }
    return out;
}

/**
 * @brief 对既有 DOM 执行 profile 判定全链（版本先、schema 后——§5.9.2）。
 *
 * parse 内链（profileId 给定时）与 validate 公共入口共用本函数。
 */
IoResult<void> validateAgainstProfile(const JsonDocument& doc, const JsonProfile& profile)
{
    // 第一步：版本判定（先于一切 schema 校验——§5.9.2 顺序硬约束）。
    IoError ver = checkSchemaVersion(doc.root, profile);
    if (ver.code != IoErrorCode::Ok) {
        IoResult<void> out;
        out.error = std::move(ver);
        return out;
    }
    // 第二步：结构遍历（profile 驱动、首错即返）。
    JsonPath path;
    return validateValue(doc.root, profile.rootShape, path);
}

// =====================================================================
// canonical 序列化器（§5.9.3——键序来源二态＋to_chars 最短＋2 空格＋LF）
// =====================================================================

/// 缩进单位（§5.9.3"缩进固定 2 空格"——canonical 版面恒定）。
constexpr std::string_view kIndentUnit = "  ";

/// 行尾（§5.9.3"LF 行尾"——canonical 版面恒定）。
constexpr std::string_view kLf = "\n";

/**
 * @brief 递归产出 canonical 字节（out 追加；shape 为 null＝无 profile 的
 *        字典序文档——键序来源二态见 JsonWriteOptions 注）。
 *
 * 数值：Integer 十进制 to_chars；Real 最短往返 to_chars（默认格式——
 * 同一 double 恒同串），非有限＝写出侧拒绝（§5.9.1 NaN 行"写出侧
 * std::isfinite 断言"——防程序化构造的 DOM 回渗非有限值）。字符串转义
 * 见 emitEscapedString。
 */
IoResult<void> emitCanonicalValue(IoString& out, const JsonValue& v, const JsonShape* shape,
                                  const JsonWriteOptions& options, std::size_t depth);

/**
 * @brief 字符串 canonical 转义输出（RFC 8259 string 词法的确定性形态）：
 *        " 与 \ 转义；\b \f \n \r \t 用短转义；其余 <0x20 控制字符用
 *        \u00xx（4 位小写十六进制）；DEL(0x7F) 与非 ASCII 直通（UTF-8
 *        原文——canonical 不重编码，同语义同字节的前提）。
 */
void emitEscapedString(IoString& out, const std::string& text)
{
    out.push_back('"');
    for (const char c : text) {
        switch (c) {
        case '"': out.append("\\\""); break;
        case '\\': out.append("\\\\"); break;
        case '\b': out.append("\\b"); break;
        case '\f': out.append("\\f"); break;
        case '\n': out.append("\\n"); break;
        case '\r': out.append("\\r"); break;
        case '\t': out.append("\\t"); break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                // 控制字符：\u00xx（小写十六进制——确定性；标准要求的
                // 最小转义集之外的统一形态）。
                std::array<char, 8> tmp{};
                std::snprintf(tmp.data(), tmp.size(), "\\u%04x", static_cast<unsigned>(c));
                out.append(tmp.data());
            } else {
                out.push_back(c);
            }
            break;
        }
    }
    out.push_back('"');
}

/**
 * @brief 对象成员 canonical 排序（§5.9.3 写行）：
 *   - shape 非 null：声明序在前（声明表序；未出现的声明键跳过），
 *     未声明键按文件出现序跟后（确定性兜底——经校验的文档不存在）；
 *   - shape 为 null：按键名字典序（std::string 比较＝无符号字节序——
 *     UTF-8 确定性排序，无 locale 参与）。
 */
std::vector<const JsonMember*> orderMembers(const JsonValue& obj, const JsonShape* shape)
{
    std::vector<const JsonMember*> ordered;
    ordered.reserve(obj.members.size());
    if (shape != nullptr) {
        std::vector<bool> taken(obj.members.size(), false);
        for (const JsonProperty& p : shape->properties) {
            for (std::size_t i = 0; i < obj.members.size(); ++i) {
                if (!taken[i] && obj.members[i].key == p.key) {
                    ordered.push_back(&obj.members[i]);
                    taken[i] = true;
                    break;
                }
            }
        }
        for (std::size_t i = 0; i < obj.members.size(); ++i) {
            if (!taken[i]) {
                ordered.push_back(&obj.members[i]);   // 未声明键：文件出现序
            }
        }
        return ordered;
    }
    for (const JsonMember& m : obj.members) {
        ordered.push_back(&m);
    }
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const JsonMember* a, const JsonMember* b) { return a->key < b->key; });
    return ordered;
}

/**
 * @brief 递归产出 canonical 字节（实现——契约见前置声明处）。
 */
IoResult<void> emitCanonicalValue(IoString& out, const JsonValue& v, const JsonShape* shape,
                                  const JsonWriteOptions& options, std::size_t depth)
{
    IoResult<void> outRes;
    switch (v.type) {
    case JsonValue::Type::Null:
        out.append("null");
        return outRes;
    case JsonValue::Type::Boolean:
        out.append(v.boolValue ? "true" : "false");
        return outRes;
    case JsonValue::Type::Integer: {
        // 十进制 to_chars（无本地化——确定性；int64 最长 20 字符）。
        std::array<char, 32> tmp{};
        const auto r = std::to_chars(tmp.data(), tmp.data() + tmp.size(), v.integerValue);
        out.append(tmp.data(), static_cast<std::size_t>(r.ptr - tmp.data()));
        return outRes;
    }
    case JsonValue::Type::Real: {
        if (!std::isfinite(v.realValue)) {
            // §5.9.1 写出侧断言：非有限值拒绝写出（不静默落盘 nan/inf）。
            outRes.error = IoError{IoErrorCode::FormatJsonNumber, {},
                                   "json write: non-finite real value rejected (isfinite assertion)"};
            return outRes;
        }
        // 最短往返表示（'.' 小数点/无本地化——同 DOM 两次写出同字节，
        // §5.9.3 canonical 编码行）。默认格式在 fixed/scientific 间自动
        // 择短（如 1e+21），两者皆为合法 JSON number 词法。
        std::array<char, 64> tmp{};
        const auto r = std::to_chars(tmp.data(), tmp.data() + tmp.size(), v.realValue);
        out.append(tmp.data(), static_cast<std::size_t>(r.ptr - tmp.data()));
        return outRes;
    }
    case JsonValue::Type::String:
        emitEscapedString(out, v.stringValue);
        return outRes;
    case JsonValue::Type::Array: {
        if (v.items.empty()) {
            out.append("[]");   // 空容器单 token（canonical 恒定版面——不折行）
            return outRes;
        }
        const JsonShape* itemShape = (shape != nullptr) ? shape->items.get() : nullptr;
        out.push_back('[');
        for (std::size_t i = 0; i < v.items.size(); ++i) {
            if (i > 0) {
                out.push_back(',');   // 逗号只在项间（项前 LF＋缩进——版面恒定）
            }
            out.append(kLf);
            out.append((depth + 1) * kIndentUnit.size(), ' ');
            if (const IoResult<void> r = emitCanonicalValue(out, v.items[i], itemShape, options,
                                                            depth + 1);
                !r) {
                return r;
            }
        }
        out.append(kLf);
        out.append(depth * kIndentUnit.size(), ' ');
        out.push_back(']');
        return outRes;
    }
    case JsonValue::Type::Object: {
        if (v.members.empty()) {
            out.append("{}");
            return outRes;
        }
        // 键序：声明序（profile 给定）或字典序（无 profile）——§5.9.3。
        const std::vector<const JsonMember*> ordered = orderMembers(v, shape);
        out.push_back('{');
        for (std::size_t i = 0; i < ordered.size(); ++i) {
            const JsonMember& m = *ordered[i];
            if (i > 0) {
                out.push_back(',');
            }
            out.append(kLf);
            out.append((depth + 1) * kIndentUnit.size(), ' ');
            emitEscapedString(out, m.key);
            out.append(": ");
            // 子 shape：按键在声明表中的契约递归（未声明键＝无约束——经
            // 校验的文档不存在该形态；未经校验的 DOM 按 Any 序列化）。
            const JsonShape* child = nullptr;
            if (shape != nullptr) {
                for (const JsonProperty& p : shape->properties) {
                    if (p.key == m.key) {
                        child = p.shape.get();
                        break;
                    }
                }
            }
            if (const IoResult<void> r = emitCanonicalValue(out, m.value, child, options,
                                                            depth + 1);
                !r) {
                return r;
            }
        }
        out.append(kLf);
        out.append(depth * kIndentUnit.size(), ' ');
        out.push_back('}');
        return outRes;
    }
    }
    outRes.error = IoError{IoErrorCode::FormatInternal, {}, "json write: unknown value type"};
    return outRes;
}
// =====================================================================
// canonical 化与内容身份（§5.9.3——自由函数入口）
// =====================================================================

} // namespace（解析/校验/序列化实现段收口——下方为公共实现实体）

IoResult<IoString> canonicalizeJson(const JsonDocument& doc, const JsonWriteOptions& options)
{
    IoResult<IoString> out;
    try {
        // canonical 序列化（键序/版面/数值形态恒定——§5.9.3）；非有限实
        // 数经 emitCanonicalValue 拒绝（错误原样上抛——错误轨道单一权威）。
        IoString bytes;
        const JsonShape* shape =
            (options.profile != nullptr) ? &options.profile->rootShape : nullptr;
        if (const IoResult<void> r = emitCanonicalValue(bytes, doc.root, shape, options, 0); !r) {
            out.error = r.error;
            return out;
        }
        // 根级换行收尾：canonical 文件形态以 LF 结束（POSIX 文本惯例——
        // 与写出器同一实现面，digest 与 write 字节必然一致）。
        bytes.append(kLf);
        out.value = std::move(bytes);
        return out;
    } catch (const std::exception& e) {
        out.error = IoError{IoErrorCode::FormatInternal, {},
                            std::string("json canonicalize: ") + e.what()};
        return out;
    } catch (...) {
        out.error = IoError{IoErrorCode::FormatInternal, {},
                            "json canonicalize: unknown exception"};
        return out;
    }
}

IoResult<core::Digest256> digestCanonicalJson(const JsonDocument& doc,
                                              const JsonWriteOptions& options)
{
    IoResult<core::Digest256> out;
    // 先 canonical 化（失败＝非有限数值——不产出摘要，错误原样上抛）。
    const IoResult<IoString> bytes = canonicalizeJson(doc, options);
    if (!bytes) {
        out.error = bytes.error;
        return out;
    }
    // 摘要算法唯一性（SA-12/NFR-MNT-03/D-05）：core ContentDigester——
    // io 不引入第二摘要算法；本函数是 JSON 通道内容身份的唯一入口。
    core::ContentDigester digester;
    digester.update(bytes.value.data(), bytes.value.size());
    out.value = digester.finalize();
    return out;
}

namespace {

// =====================================================================
// JsonProfileRegistry（pimpl——map＋互斥锁，注册期可变、运行期只读）
// =====================================================================

/// profile 速断（注册期契约校验的公共文本——两处注册期拒绝共用）。
constexpr std::string_view kRegistryContract = "json profile registry";

} // namespace

struct JsonProfileRegistry::Impl {
    std::mutex mutex;                                            ///< 注册期串行（装配期单线程约定＋防御）
    std::map<IoString, std::shared_ptr<const JsonProfile>> byId; ///< id→profile（字典序——遍历确定性）
};

void JsonProfileRegistry::registerProfile(JsonProfile profile)
{
    // 装配期契约校验（BudgetSpec 同款 fail-fast 口径——注册期纯计算，
    // 静默吞掉比抛出更危险）：id 非空、版本表升序非空。
    if (profile.profileId.empty()) {
        throw std::invalid_argument(std::string(kRegistryContract) + ": empty profileId");
    }
    if (profile.supportedVersions.empty()) {
        throw std::invalid_argument(std::string(kRegistryContract) + ": empty supportedVersions ("
                                    + profile.profileId + ")");
    }
    for (std::size_t i = 1; i < profile.supportedVersions.size(); ++i) {
        if (profile.supportedVersions[i] <= profile.supportedVersions[i - 1]) {
            throw std::invalid_argument(std::string(kRegistryContract)
                                        + ": supportedVersions not ascending ("
                                        + profile.profileId + ")");
        }
    }
    auto p = std::make_shared<const JsonProfile>(std::move(profile));
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    // 重复注册＝装配防漏语义（§9.5 前置"未注册＝IO-FORMAT-INTERNAL"的
    // 注册侧镜像）——两份契约同 id 即装配缺陷，logic_error fail-fast。
    if (!m_impl->byId.emplace(p->profileId, std::move(p)).second) {
        throw std::logic_error(std::string(kRegistryContract)
                               + ": duplicate profileId registration");
    }
}

const JsonProfile* JsonProfileRegistry::find(std::string_view profileId) const
{
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    const auto it = m_impl->byId.find(IoString(profileId));
    return it != m_impl->byId.end() ? it->second.get() : nullptr;
}

std::size_t JsonProfileRegistry::size() const
{
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return m_impl->byId.size();
}

JsonProfileRegistry::JsonProfileRegistry() : m_impl(std::make_unique<Impl>()) {}

JsonProfileRegistry::~JsonProfileRegistry() = default;

namespace {

// =====================================================================
// JsonReader（IStructuredDataReader 会话实现——registry 共享只读）
// =====================================================================

/// 解析安全限额（恒为产品默认规格——§4.5.1 表默认值；guard 非 null 时
/// 记账与限额同源同值，双轨一致——JsonReader::parseBytesChecked 注）。
JsonLimits defaultJsonLimits()
{
    const BudgetSpec spec = BudgetSpec::productDefault();
    return JsonLimits{spec.limit(BudgetDimension::JsonDocBytes),
                      spec.limit(BudgetDimension::JsonDepth),
                      spec.limit(BudgetDimension::JsonStringChars)};
}

/**
 * @brief 文档字节预算预检＋记账（§5.9.1 文档大小行"流式预检（先 stat/
 *        首块）"——parse 路径在读全文前调用；parseBytes 路径对入参字节
 *        等价调用）。
 *
 * 双轨语义（ZipScopeSession/Csv BudgetScopeSession 同款三态）：
 *   - guard 非 null＋callerScope＝0：自开内部 scope（产品默认规格）记账
 *     ——超限三要素诊断由守卫产出（状态不变）；
 *   - guard 非 null＋callerScope≠0：在调用方（tighten 后的）scope 上
 *     记账、不越权开关（等价增补的语义落点——调用方规格约束读取会话）；
 *   - guard 为 null＝不记账但不豁免——对产品默认限额做防御性比较
 *     （§5.9.1 预算行语义不变——"仅测试/预览轻量场景"口径，限额仍在）。
 *
 * @param budget   [in] 预算守卫（可空——见上）
 * @param docBytes [in] 文档字节数（BOM 剥离前原量——预算按线上字节计）
 * @param callerScope [in] 调用方 scope 句柄（0＝自开内部 scope）
 * @param scopeOut [out] 生效 scope（内部模式时有效；**失败时本函数自行
 *                 回收内部 scope**，成功时归调用方用毕回收；外部模式原样
 *                 回传——开关权在调用方）
 * @return 成功＝预算内；失败＝三要素/内部错误（内部 scope 已回收）
 */
IoResult<void> precheckDocBytes(IBudgetGuard* budget, std::uint64_t docBytes,
                                BudgetScopeId callerScope, BudgetScopeId* scopeOut)
{
    const JsonLimits lim = defaultJsonLimits();
    if (budget != nullptr) {
        BudgetScopeId scope = callerScope;   // 外部模式：原样采用
        if (callerScope.value == 0) {
            // 内部模式：自开 scope（缺省规格恒合法——触及即实现缺陷）。
            const IoResult<BudgetScopeId> opened = budget->openScope(BudgetSpec::productDefault());
            if (!opened) {
                IoResult<void> out;
                out.error = opened.error;
                return out;
            }
            scope = opened.value;
        }
        // 记账三笔：单文件/会话总量/JSON 文档量（§4.5.2"普通资源读取至
        // 少强制 SingleFileBytes/TotalBytes；JSON 通道叠加各深度维"）。
        // 任一超限＝守卫三要素错误（状态不变）——内部 scope 回收后上抛
        // （外部 scope 归调用方，不越权关闭）。
        for (const BudgetDimension dim :
             {BudgetDimension::SingleFileBytes, BudgetDimension::TotalBytes,
              BudgetDimension::JsonDocBytes}) {
            if (const IoResult<void> r = budget->charge(scope, dim, docBytes); !r) {
                if (callerScope.value == 0) {
                    budget->closeScope(scope);   // 内部 scope：失败路径自回收
                }
                IoResult<void> out;
                out.error = r.error;
                return out;
            }
        }
        *scopeOut = scope;   // 成功：生效 scope 回传（内部模式用毕回收）
        return {};
    }
    // 无守卫：防御性比较（不记账不豁免——限额仍在）。
    if (docBytes > lim.docBytes) {
        IoResult<void> out;
        out.error = makeComparativeError(IoErrorCode::SecBudgetJson, docBytes, lim.docBytes,
                                         "bytes", "json parse: document size over budget");
        return out;
    }
    return {};
}

/**
 * @brief JsonReader 会话（§9.5 契约表逐行承载）。
 *
 * 线程：无状态（registry 共享只读）——并发安全（§9.5 线程约束行）。
 * 生命周期：进程级（调用方可长期持有复用）。profileId 未注册＝
 * IO-FORMAT-INTERNAL（§9.5 前置"装配期防漏"——Dev 级码）。
 */
class JsonReader final : public IStructuredDataReader {
public:
    explicit JsonReader(std::shared_ptr<const JsonProfileRegistry> registry)
        : m_registry(std::move(registry))
    {
    }

    IoResult<JsonDocument> parse(const std::filesystem::path& file, const JsonReadOptions& options,
                                 IBudgetGuard* budget, IoCancelToken* cancel,
                                 BudgetScopeId budgetScope) override
    {
        IoResult<JsonDocument> out;
        try {
            // ① stat 预检（§5.9.1"先 stat"——超预算文件不读全文；错误
            //    四分类映射——§4.2.5）。此处只做**防御性比较**（不记账，
            //    限额恒产品默认）——记账归 parseBytesChecked 的
            //    precheckDocBytes 单点执行（同一文档字节只计一次账；调用
            //    方 tighten 的外部 scope 在执行体内生效——stat 预检的超
            //    前拦截面以产品默认限额为下界，调用方更紧规格由记账兜住）。
            std::error_code ec;
            const std::uintmax_t size = std::filesystem::file_size(file, ec);
            if (ec) {
                out.error =
                    IoError{mapSystemError(ec), {}, "json parse: stat failed: " + ec.message()};
                return out;
            }
            if (size > defaultJsonLimits().docBytes) {
                // 超预算文件不读全文（比较型三要素——§5.9.1 文档大小行）。
                out.error = makeComparativeError(IoErrorCode::SecBudgetJson, size,
                                                 defaultJsonLimits().docBytes, "bytes",
                                                 "json parse: document size over budget (stat precheck)");
                return out;
            }
            // ② 读入字节（块循环——取消检查点按块，§9.5 取消行为行）。
            IoString bytes;
            bytes.reserve(static_cast<std::size_t>(size) + 1);   // +1＝NUL 哨兵余量
            std::ifstream in(file, std::ios::binary);
            if (!in.is_open()) {
                out.error = IoError{IoErrorCode::ResAccessDenied, {},
                                    "json parse: cannot open for read"};
                return out;
            }
            std::array<char, kFileReadChunkBytes> chunk{};
            bool readError = false;
            for (;;) {
                if (cancel != nullptr && cancel->isCancelled()) {
                    // 取消＝状态非错误（UX-03）。
                    IoResult<JsonDocument> cancelled;
                    cancelled.error =
                        IoError{IoErrorCode::Cancelled, {}, "json parse: cancelled at read checkpoint"};
                    return cancelled;
                }
                in.read(chunk.data(), kFileReadChunkBytes);
                const std::streamsize got = in.gcount();
                if (got > 0) {
                    bytes.append(chunk.data(), static_cast<std::size_t>(got));
                }
                if (in.bad()) {
                    readError = true;   // 硬读错误（非 EOF）
                    break;
                }
                if (got == 0) {
                    break;   // EOF——正常结束
                }
            }
            if (readError) {
                out.error = IoError{IoErrorCode::ResAccessDenied, {},
                                    "json parse: read failed during stream"};
                return out;
            }
            // ③ 转内存执行体（记账/编码/受限解析/版本/schema——唯一路径）。
            return parseBytesChecked(std::string_view(bytes), options, budget, cancel,
                                     budgetScope);
        } catch (const std::exception& e) {
            out.error = IoError{IoErrorCode::FormatInternal, {},
                                std::string("json parse: ") + e.what()};
            return out;
        } catch (...) {
            out.error = IoError{IoErrorCode::FormatInternal, {}, "json parse: unknown exception"};
            return out;
        }
    }

    IoResult<JsonDocument> parseBytes(std::string_view utf8, const JsonReadOptions& options,
                                      IBudgetGuard* budget, IoCancelToken* cancel,
                                      BudgetScopeId budgetScope) override
    {
        // 与 parse 共用执行体（单路径——行为一致，无第二解析实现）。
        return parseBytesChecked(utf8, options, budget, cancel, budgetScope);
    }

    IoResult<void> validate(const JsonDocument& doc, const IoString& profileId) const override
    {
        IoResult<void> out;
        try {
            // profile 查找（未注册＝INTERNAL——装配期防漏，§9.5 前置）。
            const JsonProfile* profile =
                m_registry != nullptr ? m_registry->find(profileId) : nullptr;
            if (profile == nullptr) {
                out.error = IoError{IoErrorCode::FormatInternal, {},
                                    "json validate: profileId not registered (assembly leak): "
                                        + profileId};
                return out;
            }
            return validateAgainstProfile(doc, *profile);
        } catch (const std::exception& e) {
            out.error = IoError{IoErrorCode::FormatInternal, {},
                                std::string("json validate: ") + e.what()};
            return out;
        } catch (...) {
            out.error =
                IoError{IoErrorCode::FormatInternal, {}, "json validate: unknown exception"};
            return out;
        }
    }

private:
    /**
     * @brief 解析执行体（parse 与 parseBytes 的唯一实现路径）。
     *
     * 步骤（§5.9.1→§5.9.2 顺序）：
     *   ① BOM 处置：UTF-8 BOM 剥离（容忍）；UTF-16LE/BE BOM＝ENCODING
     *     拒绝（仅 UTF-8——§5.9.1 编码行，与 CSV 的 UTF-16 解码通道不同）；
     *   ② 严格 UTF-8 DFA 预检（全文一遍——编码错误恒先于语法错误报告，
     *     稳定优先序；非法序列定位到行列）；
     *   ③ 字节量预算预检＋记账（precheckDocBytes——双轨限额）；
     *   ④ 受限递归下降解析（语法/重复键/深度/字符串/溢出——Parser）；
     *   ⑤ profileId 给定时：查注册表（未注册＝INTERNAL）→版本判定→
     *     结构校验（validateAgainstProfile——版本先于 schema）。失败＝
     *     无部分 DOM 外泄（value 默认构造——§9.5 后置条件行）。
     */
    IoResult<JsonDocument> parseBytesChecked(std::string_view bytes, const JsonReadOptions& options,
                                             IBudgetGuard* budget, IoCancelToken* cancel,
                                             BudgetScopeId budgetScope)
    {
        IoResult<JsonDocument> out;
        try {
            std::string_view body = bytes;
            bool bomStripped = false;
            // ① BOM 处置（§5.9.1 编码行）。
            if (body.size() >= 3 && body.substr(0, 3) == kUtf8Bom) {
                body.remove_prefix(3);
                bomStripped = true;
            } else if (body.size() >= 2
                       && ((body[0] == '\xFF' && body[1] == '\xFE')
                           || (body[0] == '\xFE' && body[1] == '\xFF'))) {
                out.error = IoError{IoErrorCode::FormatJsonEncoding, {},
                                    "json parse: UTF-16 BOM detected (io json channel is "
                                    "UTF-8 only, io.md §5.9.1)"};
                return out;
            }
            // ② 严格 UTF-8 预检（全文——非法序列定位到字节，行列换算）。
            std::size_t failIdx = 0;
            if (!strictUtf8Check(body, &failIdx)) {
                // 行列换算：行＝'\n' 计数＋1；列＝行内字节偏移＋1（字节列
                // 口径——JsonSourceSpan 类注）。
                std::uint32_t line = 1, col = 1;
                for (std::size_t i = 0; i < failIdx && i < body.size(); ++i) {
                    if (body[i] == '\n') {
                        ++line;
                        col = 1;
                    } else {
                        ++col;
                    }
                }
                out.error = IoError{IoErrorCode::FormatJsonEncoding, {},
                                    "json parse: invalid UTF-8 sequence"};
                out.error.params.emplace_back("path", std::string("$"));
                out.error.params.emplace_back("row", std::to_string(line));
                out.error.params.emplace_back("column", std::to_string(col));
                return out;
            }
            // ③ 字节量预检＋记账（BOM 剥离前原量——线上字节口径；内部
            //    scope 失败时已在 precheckDocBytes 内自回收；外部 scope 的
            //    开关权在调用方——本函数只在内部模式回收）。
            const bool scopeInternal = (budget != nullptr) && (budgetScope.value == 0);
            BudgetScopeId scope;
            if (const IoResult<void> r =
                    precheckDocBytes(budget, bytes.size(), budgetScope, &scope);
                !r) {
                out.error = r.error;
                return out;
            }
            // ④ 受限解析（深度/字符串预算在 Parser 内逐值钳制——不存在
            //    先深递归后检查的窗口）。
            Parser parser(body, defaultJsonLimits(), cancel);
            parser.setBomStripped();   // BOM 剥离标记（报告面回填）
            IoResult<JsonDocument> doc = parser.run();
            if (!doc) {
                out.error = doc.error;
                if (scopeInternal) {
                    budget->closeScope(scope);   // 内部 scope：失败路径回收
                }
                return out;   // 无部分 DOM 外泄（§9.5 后置）
            }
            // ⑤ profile 判定链（版本先于 schema——§5.9.2 顺序硬约束）。
            if (options.profileId != nullptr) {
                const JsonProfile* profile =
                    m_registry != nullptr ? m_registry->find(*options.profileId) : nullptr;
                if (profile == nullptr) {
                    out.error =
                        IoError{IoErrorCode::FormatInternal, {},
                                "json parseBytes: profileId not registered (assembly leak): "
                                    + *options.profileId};
                    if (scopeInternal) {
                        budget->closeScope(scope);
                    }
                    return out;
                }
                if (const IoResult<void> vr = validateAgainstProfile(doc.value, *profile); !vr) {
                    out.error = vr.error;   // 结构/版本失败：无部分 DOM 外泄
                    if (scopeInternal) {
                        budget->closeScope(scope);
                    }
                    return out;
                }
            }
            if (scopeInternal) {
                budget->closeScope(scope);   // 内部 scope：成功路径回收
            }
            out.value = std::move(doc.value);
            return out;
        } catch (const std::exception& e) {
            out.error = IoError{IoErrorCode::FormatInternal, {},
                                std::string("json parseBytes: ") + e.what()};
            return out;
        } catch (...) {
            out.error = IoError{IoErrorCode::FormatInternal, {},
                                "json parseBytes: unknown exception"};
            return out;
        }
    }

    std::shared_ptr<const JsonProfileRegistry> m_registry;   ///< 共享只读（进程级——调用方共同持有）
};

// =====================================================================
// JsonWriter（IJsonWriter 会话实现——单线程单目标；§9.5 会话型）
// =====================================================================

/// 暂存文件后缀（同目录同卷 rename 前提——与 Csv 的 .ird-csv-tmp 同款
/// 约定；命名区分通道，避免两类暂存互相误删）。
constexpr std::string_view kJsonTempSuffix = ".ird-json-tmp";

class JsonWriter final : public IJsonWriter {
public:
    IoResult<void> write(JsonOutputTarget&& target, const JsonDocument& doc,
                         const JsonWriteOptions& options) override
    {
        IoResult<void> out;
        try {
            // 第一步：canonical 字节（键序/版面/数值恒定——§5.9.3；非有
            // 限实数在此拒绝——写出侧 isfinite 断言，§5.9.1 NaN 行）。
            const IoResult<IoString> bytes = canonicalizeJson(doc, options);
            if (!bytes) {
                out.error = bytes.error;
                return out;
            }
            // 第二步：落目标。文件目标＝同目录暂存＋rename 原子替换
            // （§4.6 失败恢复语义：替换前任何失败＝目标不变＋暂存清理）；
            // 内存目标＝缓冲整体替换（canonical 字节含根级 LF）。
            if (target.kind == JsonOutputTarget::Kind::MemoryBuffer) {
                if (target.buffer == nullptr) {
                    out.error = IoError{IoErrorCode::FormatInternal, {},
                                        "json write: null memory buffer (caller contract)"};
                    return out;
                }
                *target.buffer = bytes.value;
                return out;
            }
            std::filesystem::path temp = target.filePath;
            temp += kJsonTempSuffix;
            {
                std::ofstream staging(temp, std::ios::binary | std::ios::trunc);
                if (!staging.is_open()) {
                    out.error = IoError{IoErrorCode::ResAccessDenied, {},
                                        "json write: cannot open staging file"};
                    return out;
                }
                staging.write(bytes.value.data(),
                              static_cast<std::streamsize>(bytes.value.size()));
                staging.flush();
                if (!staging) {
                    out.error = IoError{IoErrorCode::PackDiskFull, {},
                                        "json write: staging write failed (disk full?)"};
                    std::error_code rmEc;
                    std::filesystem::remove(temp, rmEc);   // 暂存清理（尽力）
                    return out;
                }
            }   // 暂存句柄先关（Windows 语义：打开句柄会锁 rename）
            std::error_code ec;
            std::filesystem::rename(temp, target.filePath, ec);
            if (ec) {
                // 替换失败＝清理暂存、目标不变（MSVC rename＝MoveFileEx
                // REPLACE_EXISTING 语义——失败目标未被触碰）。
                std::error_code rmEc;
                std::filesystem::remove(temp, rmEc);
                out.error = IoError{mapSystemError(ec), {},
                                    "json write: atomic replace failed: " + ec.message()};
                return out;
            }
            return out;
        } catch (const std::exception& e) {
            out.error = IoError{IoErrorCode::FormatInternal, {},
                                std::string("json write: ") + e.what()};
            return out;
        } catch (...) {
            out.error = IoError{IoErrorCode::FormatInternal, {}, "json write: unknown exception"};
            return out;
        }
    }
};

} // namespace

// =====================================================================
// 工厂（§9.11 访问器 jsonReader()/jsonWriter() 的实现侧产物）
// =====================================================================

std::unique_ptr<IStructuredDataReader>
    makeStructuredDataReader(std::shared_ptr<const JsonProfileRegistry> registry)
{
    return std::make_unique<JsonReader>(std::move(registry));
}

std::unique_ptr<IJsonWriter> makeJsonWriter()
{
    return std::make_unique<JsonWriter>();
}

} // namespace sdurws::ird::io
