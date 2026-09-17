/**
 * @file   Csv.cpp
 * @brief  CSV 读写器实现——编码流（BOM/UTF-16 解码/严格 UTF-8 校验）、v1
 *         方言标识行解析（字节级精确）、RFC4180 流式行扫描、分隔符嗅探、
 *         逐行错误定位与 canonical 写出。
 *
 * 设计依据：见 Csv.hpp 文件头（units/io.md §5.1~§5.6/§5.8、§9.3/§9.4、
 * 需求 NFR-SEC-03/REQ-05、任务契约 IO-T03 acceptance 1~5）。
 *
 * 实现结构（自上而下）：
 *   1. 常量与 UTF-8 严格校验 DFA；
 *   2. 纯函数：转义/还原/方言行渲染（§5.3/§5.1 唯一实现点）；
 *   3. DecodedStream：文件字节 → 规范化 UTF-8 字节流（BOM 判定剥离、
 *      UTF-16LE/BE 解码、无 BOM 严格 UTF-8 DFA 校验——§5.2 编码行）；
 *   4. RowScanner：RFC4180 字段/行状态机（引号内换行为字段内容、""
 *      字面引号、行尾 CRLF/LF/CR 容错——§5.2 行尧行/E12）；
 *   5. 方言标识行捕获与解析（§5.1 字节级精确判定——首物理行恰为
 *      "#rwcsv1" 记号才算携带标识；带记号但语法不完整＝格式错误拒绝，
 *      完全不带记号＝无标识外部文件零改写）；
 *   6. 分隔符嗅探（§5.2——前 101 逻辑行引号外出现次数统计）；
 *   7. CsvReader/CsvWriter：ICsvReader/ICsvWriter 会话实现（预算检查点
 *      ——§4.4⑤；部分成功——§5.6；原子写出——§9.4 后置）。
 *
 * 线程约束：会话对象单线程（§9.3/§9.4 契约表）；本文件无共享可变状态。
 * 异常纪律：公共接口非抛出（§1.4）——实现体以 try/catch 包裹，任何标准
 * 库异常转为 IO-FORMAT-INTERNAL（防御性内部错误，触及即报缺陷）。
 */

#include <sdurws/ird/io/Csv.hpp>

#include <sdurws/ird/io/Budget.hpp>
#include <sdurws/ird/io/IoDiagnostics.hpp>

#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <exception>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace sdurws::ird::io {

namespace {

// =====================================================================
// 常量（实现侧防御上限与扫描窗口——均注明出处）
// =====================================================================

/// 首物理行（方言标识行捕获）的扫描上限，单位字节。1 MiB＝CsvFieldChars
/// 硬上限（Budget.hpp §4.5.1 表）同量级——标识行是短行（§5.1 语法决定
/// 不超过百余字节），超限仍以 #rwcsv1 开头的行必是伪造/损坏标识；不带
/// 记号的超长首行属于无标识数据，捕获区直接转为行解析器的种子继续流式
/// 解析（内存有界）。
inline constexpr std::size_t kMarkerScanCapBytes = 1u << 20;

/// 单字段累积的防御上限，单位字节。1 MiB＝CsvFieldChars 硬上限
/// （Budget.hpp §4.5.1"CSV 单字段字符数（默认 64 KiB｜硬 1 MiB）"）＋
/// 4 KiB 余量（容纳记账检查点粒度的滑动）。命中即以 IO-SEC-BUDGET-FIELD
/// 拒读——该码的 Budget.hpp 注释明确覆盖"单元格长度/字段数超限"，是本
/// 防御的码面出处；没有此上限，无换行的巨型单字段文件会在任何记账检查
/// 点之前耗尽内存（DoS）。
inline constexpr std::size_t kFieldScanCapBytes = (1u << 20) + 4096;

/// 单行字段数的防御上限。Budget.hpp SecBudgetField 注释同样覆盖"字段数
/// 超限"——百万级字段必然是结构损坏或攻击构造（合法表格列数远低于此）。
inline constexpr std::uint64_t kRowFieldsCap = 1000000;

/// 行错误原文片段的截断长度，单位字节（§5.6 rawSnippet；IoDiagnostics.hpp
/// "CSV 族 snippet 仅开发级保留"）。120 字节足够定位（约 40 个汉字），
/// 防止诊断面被超长单元格撑爆；按 UTF-8 边界回退，不切碎多字节序列。
inline constexpr std::size_t kSnippetMaxBytes = 120;

/// 嗅探采样的逻辑行数上限：表头行＋后续至多 100 行（§5.2 分隔符行原文
/// "对表头行与后续至多 100 行统计"＝合计 101 行）。
inline constexpr std::size_t kSniffMaxRows = 101;

/// 编码预检窗口（probe 的嗅探只严格校验前 64 KiB——§5.2 编码行"全文
/// 或首 64 KiB 预检"；read 交付遍对全文校验，不受此窗口限制）。
inline constexpr std::uint64_t kEncodingPrecheckBytes = 64u * 1024;

/// v1 方言标识记号（§5.1 原文：#rwcsv1——rwcsv＝格式标识，1＝方言版本，
/// 无空格分隔）。字节级精确判定：首物理行第一个空格前的记号必须**恰等**
/// 于本串才算携带标识（#rwcsv12/#rwcsv2 均不是 v1 标识——按无标识外部
/// 文件处理，本软件只拥有 v1；未来版本识别属方言版本升级，P-IO-5）。
constexpr std::string_view kMarkerToken = "#rwcsv1";

// =====================================================================
// UTF-8 严格校验 DFA（无 BOM 编码判定——§5.2 编码行"严格 UTF-8 校验"）
// =====================================================================

/// DFA 状态值：0＝接受态，12＝拒绝态（字节序列非法：超长编码/代理区/
/// 超 U+10FFFF/悬空续字节）。
inline constexpr std::uint32_t kUtf8Reject = 12;

/**
 * UTF-8 严格校验转移表——Björn Höhrmann 的经典压缩 DFA（公共域实现，
 * 语义等价于 WHATWG Encoding Standard 的 UTF-8 解码器：拒绝超长编码、
 * UTF-16 代理区码点与 > U+10FFFF）。用法：
 *   state = kUtf8Dfa[256 + state + kUtf8Dfa[byte]]，逐字节推进；
 *   state == kUtf8Reject 即非法。选用表驱动而非手写分支：转移逻辑唯一、
 *   无分支误写空间，且经年公开检验（确定性 NFR-COR-01 的实现面）。
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

// =====================================================================
// 纯函数助手（片段截断/ASCII 折叠）
// =====================================================================

/**
 * @brief 截断诊断片段至 kSnippetMaxBytes 且不切碎 UTF-8 多字节序列。
 *
 * 回退策略：从第 120 字节向前跳过全部续字节（0x80~0xBF），使截断点落在
 * 序列首字节边界——保证片段仍是合法 UTF-8（诊断呈现链不需要二次消毒）。
 */
IoString truncateSnippet(std::string_view raw)
{
    if (raw.size() <= kSnippetMaxBytes) {
        return IoString(raw);
    }
    std::size_t cut = kSnippetMaxBytes;
    while (cut > 0 && (static_cast<unsigned char>(raw[cut]) & 0xC0) == 0x80) {
        --cut;                                 // 跳过续字节——回退到序列边界
    }
    return IoString(raw.substr(0, cut));
}

/// ASCII 大小写折叠相等（DUPCOL 判定——§5.4"折叠大小写后同名"；折叠域
/// 登记为 ASCII：业务列名约定为 ASCII 标识符，非 ASCII 折叠属业务字段
/// 字典语义，io 不越权——PA-1）。
bool asciiFoldEqual(const std::string& a, const std::string& b)
{
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        const char ca = a[i];
        const char cb = b[i];
        const char la = (ca >= 'A' && ca <= 'Z') ? static_cast<char>(ca - 'A' + 'a') : ca;
        const char lb = (cb >= 'A' && cb <= 'Z') ? static_cast<char>(cb - 'A' + 'a') : cb;
        if (la != lb) {
            return false;
        }
    }
    return true;
}

/// 首个重复列名出现的列号（1 起；DUPCOL 定位——AT-02 口径；无重复返 0）。
std::uint64_t firstDupCol(const std::vector<std::string>& header)
{
    for (std::size_t j = 0; j < header.size(); ++j) {
        for (std::size_t i = 0; i < j; ++i) {
            if (asciiFoldEqual(header[i], header[j])) {
                return j + 1;
            }
        }
    }
    return 0;
}

/// 行原文拼接（诊断片段用——分隔符连接，仅作片段截断输入，非结构还原）。
std::string joinRaw(const std::vector<std::string>& fields)
{
    std::string s;
    for (const std::string& f : fields) {
        if (!s.empty()) {
            s.push_back(',');
        }
        s += f;
    }
    return s;
}

// =====================================================================
// DecodedStream——文件字节 → 规范化 UTF-8 字节流（§5.2 编码行的执行点）
// =====================================================================

/**
 * @brief 编码规范化字节流：二进制读文件 → BOM 判定剥离 →（UTF-16 时）
 *        解码为 UTF-8 →（无 BOM 时）严格 UTF-8 校验透传。
 *
 * 产出以"字节"为单位被下游消费（int：0~255 有效；-1＝流尽；-2＝编码
 * 非法）。设计要点：
 *   - 规范化 UTF-8 产出使下游（标识行捕获/行扫描/嗅探）完全不必感知源
 *     编码——UTF-16 文件与 UTF-8 文件走同一条解析路径（E3：标识行按解
 *     码后判定）；
 *   - 严格校验在流中逐字节进行：无 BOM 且任何位置非法 → next() 返回 -2
 *     → read 以 IO-FORMAT-CSV-ENCODING 稳定拒绝（不猜测 ANSI/GBK、不转
 *     码——IO-D09；acceptance 4 的被测语义）；
 *   - NUL 字节是合法 UTF-8——放行至字段层按 §5.4 处置（IO-FORMAT-CSV-
 *     CHAR），不在编码层拦截（分层职责）。
 *
 * 生命周期：会话内对象（每次打开文件构造一个）；单线程。
 */
class DecodedStream
{
public:
    /// 已产出的规范化字节数（不含种子回灌——种子是已计入的原文重放）；
    /// probe 的预检窗口与嗅探字节上限以此为计量。
    std::uint64_t produced = 0;

    /**
     * @brief 打开文件并完成 BOM 判定（§5.2 编码行①）。
     * @return 成功＝true；失败＝false 且 @p err 已置（资源四分类码）
     */
    bool open(const std::filesystem::path& file, IoError* err)
    {
        // 存在性先行：文件不存在＝IO-RES-NOT-FOUND（四分类之一；不降级
        // 为其他码——§4.2.5 分类一）。
        std::error_code ec;
        const std::filesystem::file_status st = std::filesystem::status(file, ec);
        if (ec || !std::filesystem::exists(st)) {
            *err = IoError{IoErrorCode::ResNotFound, {}, "csv open: path does not exist"};
            return false;
        }
        m_in.open(file, std::ios::binary);
        if (!m_in.is_open()) {
            // 打不开但存在＝权限/占用类环境错误（四分类之二/之四——读
            // 打开失败保守归权限不足，detail 说明方向）。
            *err = IoError{IoErrorCode::ResAccessDenied, {}, "csv open: cannot open for read"};
            return false;
        }
        detectBom();
        return true;
    }

    /// 是否存在并剥离了 BOM（§5.1"与 BOM"行/§5.2 编码行①——报告面）。
    bool sawBom() const noexcept { return m_sawBom; }

    /**
     * @brief 取下一个规范化 UTF-8 字节。
     * @return 0~255＝有效字节；-1＝流尽；-2＝编码非法（无 BOM 非 UTF-8
     *         或 UTF-16 序列残缺/代理不成对——稳定拒绝路径）
     */
    int next()
    {
        // 回退槽优先（peek 过的字节必须原序回到消费流——peek/next 一致性）。
        if (m_peeked) {
            m_peeked = false;
            return m_peekVal;
        }
        // 种子区（标识行判定未命中时回灌的捕获前缀）次之——保证"探测消
        // 费过的首行"对行解析器/嗅探器无感重放。
        if (m_seedPos < m_seed.size()) {
            return static_cast<unsigned char>(m_seed[m_seedPos++]);
        }
        if (m_enc == Enc::Utf8) {
            return nextUtf8();
        }
        return nextUtf16ToUtf8();
    }

    /// 前瞻一字节（不消费）；返回值语义同 next()。实现＝读入回退槽。
    int peek()
    {
        if (!m_peeked) {
            m_peekVal = nextUncached();
            m_peeked = true;
        }
        return m_peekVal;
    }

    /// 标识行判定未命中时回灌捕获前缀（仅 prologue 调用一次）。
    void seed(std::string bytes)
    {
        m_seed = std::move(bytes);
        m_seedPos = 0;
    }

    /// 已消费的原始文件字节数（预算对账——声明大小 vs 实际读入）。
    std::uint64_t rawConsumed() const noexcept { return m_rawConsumed; }

private:
    enum class Enc { Utf8, Utf16Le, Utf16Be };

    /// 绕过回退槽的直接取数（peek 的实现核心——不许递归占槽）。
    int nextUncached()
    {
        if (m_seedPos < m_seed.size()) {
            return static_cast<unsigned char>(m_seed[m_seedPos++]);
        }
        if (m_enc == Enc::Utf8) {
            return nextUtf8();
        }
        return nextUtf16ToUtf8();
    }

    /**
     * @brief BOM 判定与剥离（§5.2 编码行①）。
     *
     * 读取首字节判 BOM：EF BB BF＝UTF-8（剥离）、FF FE＝UTF-16LE、
     * FE FF＝UTF-16BE（均剥离并切换解码器）；无 BOM＝严格 UTF-8 校验
     * 形态。判读至多消耗 3 字节，非命中字节全部进入回退槽头。
     */
    void detectBom()
    {
        std::array<unsigned char, 3> head{};
        std::size_t n = 0;
        while (n < head.size()) {
            const int b = rawNext();
            if (b == -1) {
                break;
            }
            head[n++] = static_cast<unsigned char>(b);
        }
        if (n >= 3 && head[0] == 0xEF && head[1] == 0xBB && head[2] == 0xBF) {
            m_enc = Enc::Utf8;          // UTF-8 BOM：容忍并剥离（§5.1"与 BOM"行）
            m_sawBom = true;
            m_headLen = 0;              // 三字节全部被 BOM 消费
            return;
        }
        if (n >= 2 && head[0] == 0xFF && head[1] == 0xFE) {
            m_enc = Enc::Utf16Le;       // UTF-16LE BOM：判定并解码（§5.2）
            m_sawBom = true;
            m_head[0] = head[2];        // 第 3 字节未被 BOM 消费——回退缓存
            m_headLen = 1;
            return;
        }
        if (n >= 2 && head[0] == 0xFE && head[1] == 0xFF) {
            m_enc = Enc::Utf16Be;
            m_sawBom = true;
            m_head[0] = head[2];
            m_headLen = 1;
            return;
        }
        // 无 BOM：按 §5.2②走严格 UTF-8 校验（非法在 nextUtf8 中产生 -2）。
        m_enc = Enc::Utf8;
        m_headLen = n;
        for (std::size_t i = 0; i < n; ++i) {
            m_head[i] = head[i];
        }
    }

    /// 原始字节读取（带 64 KiB 缓冲；-1＝EOF）。
    int rawNext()
    {
        if (m_headPos < m_headLen) {
            return m_head[m_headPos++];   // BOM 判读期回退槽
        }
        if (m_bufPos >= m_bufLen) {
            m_in.read(m_buf.data(), static_cast<std::streamsize>(m_buf.size()));
            m_bufLen = static_cast<std::size_t>(m_in.gcount());
            m_bufPos = 0;
            if (m_bufLen == 0) {
                return -1;                // 流尽（读错误与 EOF 合并终止——
                                          // 预算对账不依赖精确尾差，读错误
                                          // 属环境故障，短读概率极低）
            }
        }
        ++m_rawConsumed;
        return static_cast<unsigned char>(m_buf[m_bufPos++]);
    }

    /// UTF-8 形态：DFA 逐字节校验后透传（合法序列的组成字节原样产出）。
    int nextUtf8()
    {
        // 暂存序列的已验字节优先产出（多字节序列必须整组放行——逐字节
        // 透传是原文保留的基础，只发尾字节会把 UTF-8 文本错位降采样）。
        if (m_seqPos < m_seq.size()) {
            ++produced;
            return static_cast<unsigned char>(m_seq[m_seqPos++]);
        }
        for (;;) {
            const int b = rawNext();
            if (b == -1) {
                // 流尽时若停在多字节序列中部＝残缺序列＝非法（严格校验）。
                if (m_utf8State != 0) {
                    return -2;
                }
                return -1;
            }
            // DFA 推进：prev==0 表示 b 是新序列首字节——清空暂存重开。
            const std::uint32_t prev = m_utf8State;
            m_utf8State = kUtf8Dfa[256 + m_utf8State + kUtf8Dfa[b]];
            if (m_utf8State == kUtf8Reject) {
                return -2;                // 非 UTF-8 且无 BOM → 稳定拒绝（IO-D09）
            }
            if (prev == 0) {
                m_seq.clear();
                m_seqPos = 0;
            }
            m_seq.push_back(static_cast<char>(b));
            if (m_utf8State == 0) {
                // 序列完成且合法：整组转入暂存，逐字节产出（ASCII 序列
                // 即单字节立即返回；多字节序列首字节本次返回，余者走
                // 顶部的暂存产出分支）。
                m_seqPos = 0;
                ++produced;
                return static_cast<unsigned char>(m_seq[m_seqPos++]);
            }
            // 序列中部的字节先暂存不产出——继续吸收直至序列完成或判非法。
        }
    }

    /// UTF-16 形态：码元解码（含代理对合成）→ 现场编码为 UTF-8 产出。
    int nextUtf16ToUtf8()
    {
        // 已编码未取尽的产出优先。
        if (m_outPos < m_out.size()) {
            ++produced;
            return static_cast<unsigned char>(m_out[m_outPos++]);
        }
        m_outPos = 0;
        m_out.clear();

        // 取一个 16 位码元（字节序按 BOM 判定；残缺半单元＝非法）。
        const int lo = rawNext();
        if (lo == -1) {
            return -1;
        }
        const int hi = rawNext();
        if (hi == -1) {
            return -2;
        }
        std::uint32_t unit = m_enc == Enc::Utf16Le
            ? static_cast<std::uint32_t>(lo | (hi << 8))
            : static_cast<std::uint32_t>((lo << 8) | hi);

        if (unit >= 0xD800 && unit <= 0xDBFF) {
            // 高代理：必须紧跟低代理（DC00~DFFF）合成增补平面码点；残缺
            // 或错配＝非法序列（严格拒绝，不做替换符宽容——转码即猜测，
            // §2.3 非目标 10）。
            const int lo2 = rawNext();
            if (lo2 == -1) {
                return -2;
            }
            const int hi2 = rawNext();
            if (hi2 == -1) {
                return -2;
            }
            const std::uint32_t unit2 = m_enc == Enc::Utf16Le
                ? static_cast<std::uint32_t>(lo2 | (hi2 << 8))
                : static_cast<std::uint32_t>((lo2 << 8) | hi2);
            if (unit2 < 0xDC00 || unit2 > 0xDFFF) {
                return -2;
            }
            unit = 0x10000 + ((unit - 0xD800) << 10) + (unit2 - 0xDC00);
        } else if (unit >= 0xDC00 && unit <= 0xDFFF) {
            // 悬空低代理＝非法。
            return -2;
        }

        // 码点 → UTF-8（RFC 3629 标准四形态；unit 已被上面的校验限定在
        // 合法码点空间，无需二次判界）。
        if (unit < 0x80) {
            m_out.push_back(static_cast<char>(unit));
        } else if (unit < 0x800) {
            m_out.push_back(static_cast<char>(0xC0 | (unit >> 6)));
            m_out.push_back(static_cast<char>(0x80 | (unit & 0x3F)));
        } else if (unit < 0x10000) {
            m_out.push_back(static_cast<char>(0xE0 | (unit >> 12)));
            m_out.push_back(static_cast<char>(0x80 | ((unit >> 6) & 0x3F)));
            m_out.push_back(static_cast<char>(0x80 | (unit & 0x3F)));
        } else {
            m_out.push_back(static_cast<char>(0xF0 | (unit >> 18)));
            m_out.push_back(static_cast<char>(0x80 | ((unit >> 12) & 0x3F)));
            m_out.push_back(static_cast<char>(0x80 | ((unit >> 6) & 0x3F)));
            m_out.push_back(static_cast<char>(0x80 | (unit & 0x3F)));
        }
        ++produced;
        return static_cast<unsigned char>(m_out[m_outPos++]);
    }

    std::ifstream m_in;                                  // 二进制输入流
    std::vector<char> m_buf = std::vector<char>(1u << 16); // 读缓冲（64 KiB）
    std::size_t m_bufPos = 0;
    std::size_t m_bufLen = 0;
    unsigned char m_head[3] = {0, 0, 0};                 // BOM 判读回退槽
    std::size_t m_headPos = 0;
    std::size_t m_headLen = 0;
    Enc m_enc = Enc::Utf8;                               // 判定后的源编码
    bool m_sawBom = false;                               // 是否存在并剥离了 BOM
    std::uint32_t m_utf8State = 0;                       // DFA 状态（0＝接受）
    std::string m_seq;                                   // 当前多字节序列的已验字节暂存
    std::size_t m_seqPos = 0;                            // 暂存产出位置
    std::string m_out;                                   // UTF-16→UTF-8 产出缓冲
    std::size_t m_outPos = 0;
    std::string m_seed;                                  // 标识行未命中时的回灌前缀
    std::size_t m_seedPos = 0;
    bool m_peeked = false;                               // 回退槽占用标志
    int m_peekVal = -1;
    std::uint64_t m_rawConsumed = 0;                     // 原始字节记账（预算对账）
};

// =====================================================================
// RowScanner——RFC4180 字段/行状态机（§5.2 引号/行尧行、E12）
// =====================================================================

/**
 * @brief 流式行扫描器：从 DecodedStream 逐行产出字段（引号解析后的字段
 *        内容，转义前缀尚不剥离——前缀剥离属带标识文件的还原步骤，按
 *        §5.3 只对判定为带标识的文件执行）。
 *
 * 语法决策（每条均注明卡面出处或登记理由）：
 *   - 引号内 CR/LF/CRLF 属字段内容并逐字节保留（§5.2 行尧行/E12——引号
 *     字段内换行按 RFC4180 属字段内容）；原文保留保证 roundtrip 逐字符
 *     一致（§5.5 数据层承诺）；
 *   - "" 在引号字段内＝字面引号（§5.2 引号行"RFC4180 语义"）；
 *   - 引号字段闭合后直到分隔符/行尾的多余字节按数据字符宽容追加。理由：
 *     卡面未定义该形态的处置，而 io 的反目标（§2.3 非目标 9）是"不改写
 *     用户数据"——丢弃或拒绝都会改写/中断用户数据；宽容追加配合写侧
 *     引号化可保证 roundtrip 一致。登记为实现决策（与卡面无冲突）。
 *   - 引号字段之外的字段中部的 " 按普通数据字符（与 §5.2 引号行"无标
 *     识文件中 ' 只是普通数据字符"同一精神：引号只在字段起始处有结构
 *     语义；宽容读取保留原文，写侧重引号化保证还原一致）；
 *   - 行尾 CRLF/LF/CR 三形态均接受且按整组消费（CR 后看 LF——§5.2 行尧行）；
 *   - 文件末尾无行尾：最后一行照常产出；纯空文件产出零行。
 *
 * 防御上限：单字段累积超 kFieldScanCapBytes／单行字段数超 kRowFieldsCap
 * 时停止吸收并置位相应标志（读出侧据此以 IO-SEC-BUDGET-FIELD 拒读——
 * Budget.hpp SecBudgetField 注"单元格长度/字段数超限"）。
 */
class RowScanner
{
public:
    /// v1 引号字符恒为 '"'（§5.1 键集列：quote 仅允许 "，' 与转义前缀
    /// 冲突被标识行解析拒绝；嗅探与扫描共用本常量）。
    static constexpr char kQuote = '"';

    RowScanner(DecodedStream& src, char delimiter) noexcept
        : m_src(src), m_delim(delimiter)
    {
    }

    /// 是否已判出"引号不闭合"（EOF 仍处引号内——§5.4 引号不闭合行）。
    bool unclosedQuote() const noexcept { return m_unclosed; }

    /// 引号不闭合字段的起始物理行号（定位起始行——§5.4 处置列）。
    std::uint64_t unclosedRow() const noexcept { return m_unclosedRow; }

    /// 流中判出编码非法（-2——读出侧转 IO-FORMAT-CSV-ENCODING 致命错）。
    bool encodingError() const noexcept { return m_encodingError; }

    /// 单字段超长防御上限命中标志（读出侧转 IO-SEC-BUDGET-FIELD）。
    bool fieldOverCap() const noexcept { return m_fieldOverCap; }

    /// 命中超长防御的字段的计长（含未吸收部分——诊断 actual 值）。
    std::uint64_t overCapFieldLen() const noexcept { return m_overCapLen; }

    /// 单行字段数防御上限命中标志（同上码面）。
    bool rowFieldsOverCap() const noexcept { return m_rowFieldsOverCap; }

    /// 读出侧同步当前物理行号（引号不闭合"定位起始行"的依据）。
    void setCurrentRow(std::uint64_t rowNo) noexcept { m_curRow = rowNo; }

    /**
     * @brief 读取下一物理行。
     * @param out             [out] 字段内容数组（清空后填充；引号已解析）
     * @param terminatedByEol [out] 行是否以行尾结束（false＝文件末尾无行尾）
     * @return true＝产出一行；false＝流尽且无悬挂行（干净结束）
     */
    bool nextRow(std::vector<std::string>& out, bool& terminatedByEol)
    {
        out.clear();
        m_rowBytes = 0;
        for (;;) {
            // 每个字段一个槽位：parseField 填充最后一个槽位。
            out.emplace_back();
            std::string& slot = out.back();
            const FieldEnd fe = parseField(slot);
            if (fe == FieldEnd::Delim) {
                // 分隔符：同行为续字段；字段数防御上限命中即置位（继续
                // 扫描直至行尾——结构已坏，读出侧会拒读）。
                if (out.size() >= kRowFieldsCap) {
                    m_rowFieldsOverCap = true;
                }
                continue;
            }
            terminatedByEol = (fe == FieldEnd::Eol);
            // 流尽且本行既无字段内容也无任何字节消耗＝干净结束（空文件
            // 或行尾后的 EOF——不产出幽灵空行）。
            if (fe == FieldEnd::Eof && out.size() == 1 && out.back().empty() && m_rowBytes == 0) {
                out.pop_back();
                return false;
            }
            return true;
        }
    }

private:
    enum class FieldEnd { Delim, Eol, Eof };

    /// 消费一字节并计入行字节（行存在性判定依据——见 nextRow Eof 分支）。
    int get()
    {
        const int b = m_src.next();
        if (b >= 0) {
            ++m_rowBytes;
        }
        return b;
    }

    /**
     * @brief 解析一个字段到 @p cur，止于分隔符/行尾/流尽。
     *
     * 行尾按整组消费：CR 后看 LF（CRLF 同吃）；LF、孤立 CR 各自成行尾
     * （§5.2 行尧行"LF/CRLF/CR 均接受"）。编码非法（-2）即时置位并按
     * 流尽返回——读出侧在行处置前先查 encodingError()（编码错误优先于
     * 一切行级判读，避免把非法字节误当结构解析）。
     */
    FieldEnd parseField(std::string& cur)
    {
        cur.clear();
        m_fieldLen = 0;
        const int first = get();
        if (first == -2) {
            m_encodingError = true;
            return FieldEnd::Eof;
        }
        if (first == -1) {
            return FieldEnd::Eof;          // 字段起点即流尽（行循环裁定行存在性）
        }
        if (first == '\r' || first == '\n') {
            // 字段起点即行尾＝空字段＋行终结（空行由此产出单空字段行——
            // §5.4 空行"完全空行（零字段）"的解析形态）；行尾整组消费。
            if (first == '\r' && m_src.peek() == '\n') {
                m_src.next();              // CRLF 整组
            }
            return FieldEnd::Eol;
        }
        if (first == static_cast<unsigned char>(m_delim)) {
            return FieldEnd::Delim;        // 字段起点即分隔符＝空字段（",a" 形态）
        }
        if (first == kQuote) {
            // 引号字段：RFC4180 主形态。
            for (;;) {
                const int c = get();
                if (c == -2) {
                    m_encodingError = true;
                    return FieldEnd::Eof;
                }
                if (c == -1) {
                    // 物理文件结束仍处引号内＝引号不闭合（§5.4）——定位
                    // 起始行，由读出侧致命拒绝（自该行起行结构已不可信）。
                    m_unclosed = true;
                    m_unclosedRow = m_curRow;
                    return FieldEnd::Eof;
                }
                if (c == kQuote) {
                    if (m_src.peek() == kQuote) {
                        m_src.next();      // ""＝字面引号（§5.2 引号行）
                        appendByte(cur, '"');
                        continue;
                    }
                    break;                 // 闭合引号——进入共同尾段
                }
                appendByte(cur, static_cast<char>(c)); // 引号内一切字节为内容（含换行/NUL）
            }
        } else {
            appendByte(cur, static_cast<char>(first)); // 非引号起始＝普通字段
        }
        // 共同尾段：直到分隔符/行尾/流尽；其余字节一律数据追加（含闭合
        // 引号后的尾巴——宽容读取，见类注）。
        for (;;) {
            const int c = m_src.peek();
            if (c == -2) {
                m_encodingError = true;
                return FieldEnd::Eof;
            }
            if (c == -1) {
                return FieldEnd::Eof;
            }
            if (c == static_cast<unsigned char>(m_delim)) {
                m_src.next();
                return FieldEnd::Delim;
            }
            if (c == '\r' || c == '\n') {
                m_src.next();              // 消费行尾首字节
                if (c == '\r' && m_src.peek() == '\n') {
                    m_src.next();          // CRLF 整组消费
                }
                return FieldEnd::Eol;
            }
            appendByte(cur, static_cast<char>(m_src.next()));
        }
    }

    /// 字段内容追加（超长防御：越过 kFieldScanCapBytes 后只计长不存储——
    /// 读出侧将拒读，内容不再增长以兜内存下限）。
    void appendByte(std::string& cur, char b)
    {
        ++m_fieldLen;
        if (cur.size() < kFieldScanCapBytes) {
            cur.push_back(b);
        } else if (!m_fieldOverCap) {
            m_fieldOverCap = true;
            m_overCapLen = m_fieldLen;
        } else {
            m_overCapLen = m_fieldLen;     // 持续计长供诊断 actual
        }
    }

    DecodedStream& m_src;                    // 下游字节流（调用方持有）
    char m_delim;                            // 生效分隔符（探测或标识行）
    std::uint64_t m_curRow = 0;              // 当前物理行号（定位用；读出侧维护）
    std::uint64_t m_rowBytes = 0;            // 当前行已消耗字节数（行存在性判定）
    std::uint64_t m_fieldLen = 0;            // 当前字段计长（防御用）
    bool m_unclosed = false;                 // 引号不闭合标志
    std::uint64_t m_unclosedRow = 0;         // 不闭合字段起始行
    bool m_encodingError = false;            // 流中编码非法标志（致命优先）
    bool m_fieldOverCap = false;             // 单字段超长防御命中
    std::uint64_t m_overCapLen = 0;          // 超长字段的计长
    bool m_rowFieldsOverCap = false;         // 行字段数防御命中
};

// =====================================================================
// 方言标识行捕获与解析（§5.1——字节级精确判定）
// =====================================================================

/// 标识行判定结果（三分——不标记/合法标记/带记号但语法损坏）。
enum class MarkerVerdict {
    NotMarked,   ///< 首物理行不是 v1 标识（按无标识外部文件处理——零改写）
    Ok,          ///< 合法 v1 标识行（dialect 已解析）
    Malformed,   ///< 以 #rwcsv1 开头但语法不完整（E1：IO-FORMAT-CSV-DIALECT 拒绝）
};

/**
 * @brief 解析首物理行是否为 v1 方言标识行（§5.1 判定行的实现）。
 *
 * 词法（§5.1 原文，P-IO-5 冻结语法——不私改）：
 *   - 首记号必须恰等于 "#rwcsv1"（首个空格前；整行恰为记号也合法＝全
 *     缺省）；#rwcsv12 等其他记号不是 v1 标识（NotMarked——字节级精确，
 *     不存在"疑似本软件文件"）；
 *   - 记号后为"单个空格分隔的 key=value 序列"：空记号（连续空格/前后
 *     尾空格）、缺 '='、空键、空值、未知键、重复键、值域外均为语法损坏；
 *   - 键集与值域（v1 封闭）：delimiter ∈ {',', ';', 'tab'}；quote 仅 '"'
 *     （声明 ' 即格式错误——§5.1 明文）；eol ∈ {'CRLF','LF'}；encoding
 *     仅 'utf-8'；
 *   - 缺失键取 v1 缺省（§5.1"缺失键"行）。
 *
 * @param line [in] 首物理行内容（不含行尾字节）
 * @param out  [out] 解析成功的方言（Ok 时有效）
 * @param err  [out] Malformed 时的格式错误（DIALECT，detail 含损坏位置）
 * @return 三分判定（见 MarkerVerdict）
 */
MarkerVerdict parseDialectMarker(std::string_view line, CsvDialect* out, IoError* err)
{
    // 记号判定：整行以 #rwcsv1 开头，且记号后要么行尽、要么紧跟单个空格
    // ——"#rwcsv12"这类前缀重合的记号不属于 v1（字节级精确原则）。
    if (line.size() < kMarkerToken.size()
        || line.compare(0, kMarkerToken.size(), kMarkerToken) != 0) {
        return MarkerVerdict::NotMarked;
    }
    const std::string_view rest = line.substr(kMarkerToken.size());
    if (rest.empty()) {
        *out = CsvDialect::rwDefault();    // 裸记号＝全缺省方言（§5.1 缺失键）
        return MarkerVerdict::Ok;
    }
    if (rest.front() != ' ') {
        // 记号后紧跟非空格字符（如 #rwcsv12、#rwcsv1x）——按字节级精确
        // 原则这不是 v1 标识行，交给无标识路径处理（不报错、不改写）。
        return MarkerVerdict::NotMarked;
    }

    // 键值序列解析：以单个空格切分；任何空记号（双空格/尾空格）即损坏。
    CsvDialect d = CsvDialect::rwDefault();
    bool hasDelimiter = false;
    bool hasQuote = false;
    bool hasEol = false;
    bool hasEncoding = false;
    const std::string malformed = "dialect marker: malformed key=value section";
    std::size_t pos = 1; // rest[0] 已确认是单个空格
    while (pos < rest.size()) {
        const std::size_t sp = rest.find(' ', pos);
        const std::string_view tok =
            rest.substr(pos, (sp == std::string_view::npos) ? std::string_view::npos : sp - pos);
        if (tok.empty()) {
            *err = IoError{IoErrorCode::FormatCsvDialect, {}, malformed + " (empty token)"};
            return MarkerVerdict::Malformed;
        }
        const std::size_t eq = tok.find('=');
        if (eq == std::string_view::npos || eq == 0 || eq + 1 >= tok.size()) {
            *err = IoError{IoErrorCode::FormatCsvDialect, {}, malformed + " (key=value expected)"};
            return MarkerVerdict::Malformed;
        }
        const std::string_view key = tok.substr(0, eq);
        const std::string_view val = tok.substr(eq + 1);
        // 封闭键集分发——未知键/未知值拒绝不猜测（§5.1"未知键/未知值"行）。
        if (key == "delimiter") {
            if (hasDelimiter) {
                *err = IoError{IoErrorCode::FormatCsvDialect, {}, malformed + " (duplicate key)"};
                return MarkerVerdict::Malformed;
            }
            hasDelimiter = true;
            if (val == ",") {
                d.delimiter = ',';
            } else if (val == ";") {
                d.delimiter = ';';
            } else if (val == "tab") {
                d.delimiter = '\t';
            } else {
                *err = IoError{IoErrorCode::FormatCsvDialect, {}, malformed + " (delimiter value)"};
                return MarkerVerdict::Malformed;
            }
        } else if (key == "quote") {
            if (hasQuote) {
                *err = IoError{IoErrorCode::FormatCsvDialect, {}, malformed + " (duplicate key)"};
                return MarkerVerdict::Malformed;
            }
            hasQuote = true;
            // v1 仅允许 '"'——' 与可逆转义前缀冲突（§5.1 键集列明文：
            // 声明 quote=' 判格式错误 IO-FORMAT-CSV-DIALECT）。
            if (val == "\"") {
                d.quote = '"';
            } else {
                *err = IoError{IoErrorCode::FormatCsvDialect, {}, malformed + " (quote must be \")"};
                return MarkerVerdict::Malformed;
            }
        } else if (key == "eol") {
            if (hasEol) {
                *err = IoError{IoErrorCode::FormatCsvDialect, {}, malformed + " (duplicate key)"};
                return MarkerVerdict::Malformed;
            }
            hasEol = true;
            if (val == "CRLF") {
                d.eol = CsvEol::Crlf;
            } else if (val == "LF") {
                d.eol = CsvEol::Lf;
            } else {
                *err = IoError{IoErrorCode::FormatCsvDialect, {}, malformed + " (eol value)"};
                return MarkerVerdict::Malformed;
            }
        } else if (key == "encoding") {
            if (hasEncoding) {
                *err = IoError{IoErrorCode::FormatCsvDialect, {}, malformed + " (duplicate key)"};
                return MarkerVerdict::Malformed;
            }
            hasEncoding = true;
            // v1 仅 utf-8（不做编码猜测转码——§2.3 非目标 10/IO-D09）。
            if (val == "utf-8") {
                d.encoding = CsvEncoding::Utf8;
            } else {
                *err = IoError{IoErrorCode::FormatCsvDialect, {}, malformed + " (encoding value)"};
                return MarkerVerdict::Malformed;
            }
        } else {
            *err = IoError{IoErrorCode::FormatCsvDialect, {}, malformed + " (unknown key)"};
            return MarkerVerdict::Malformed;
        }
        if (sp == std::string_view::npos) {
            break;
        }
        pos = sp + 1;
    }
    *out = d;
    return MarkerVerdict::Ok;
}

// =====================================================================
// 分隔符嗅探（§5.2 分隔符行——无标识外部文件的方言探测）
// =====================================================================

/// 嗅探结果：win＝胜出分隔符；summary＝候选统计摘要（诊断 detail 用）。
struct SniffOutcome {
    bool ok = false;           ///< 是否得到唯一一致且次数最多的候选
    bool encodingInvalid = false; ///< 嗅探窗口内判出编码非法（读出侧转 ENCODING）
    char delim = ',';          ///< ok 时的生效分隔符
    std::string summary;       ///< 候选统计摘要（§5.2"含候选统计摘要"）
};

/**
 * @brief 分隔符嗅探：对前 101 逻辑行统计各候选分隔符在"引号外"的出现
 *        次数，取"一致且次数最多"者（§5.2 分隔符行）。
 *
 * 规则操作化（卡面语义的精确化登记）：
 *   - "一致"＝该候选在**每个采样行**都出现至少一次（行级在在场一致——
 *     出现次数随行内容波动不判不一致，以总次数决胜）；
 *   - "次数最多"＝引号外出现总次数严格大于其他一致候选；并列最高＝无
 *     唯一结果（拒绝，不掷硬币）；
 *   - 采样行数＝前 101 逻辑行（表头行＋后续至多 100 行——§5.2 原文）；
 *   - 全部候选在全部采样行均为 0 次（如单列文件）＝无一致结果 → 拒绝。
 *     单列外部 CSV 因此须经 probe 失败→调用方提示用户显式选择（§5.2 同
 *     行的设计口径——字段映射 UI 归 requirements/ui）。
 *
 * 引号状态机与行扫描器同规则（"" 字面引号；引号内分隔符不计数；引号内
 * 换行不成行）——保证嗅探结论与交付遍解析的引号结构一致。
 *
 * @param src       [in,out] 已完成 BOM 判定（并回灌首行）的解码流
 * @param byteLimit [in] 产出字节上限（0＝不限；probe 传 64 KiB 预检窗口
 *                  ——§5.2 编码行"首 64 KiB 预检"）；达限即止（窗口内
 *                  合法即可，全文校验属 read 交付遍职责）
 * @return 嗅探结果（见 SniffOutcome）
 */
SniffOutcome sniffDelimiter(DecodedStream& src, std::uint64_t byteLimit)
{
    // 候选序＝§5.2 分隔符行原文：',' ';' tab。统计数组按此序。
    constexpr char kCandidates[3] = {',', ';', '\t'};
    std::array<std::uint64_t, 3> totals{};
    std::array<bool, 3> allRowsHave{true, true, true};  // 每候选"每行都出现"
    std::array<std::uint64_t, 3> inRow{};
    std::size_t rows = 0;
    bool inQuote = false;

    // 行末折叠：把本行计数并入积累（局部函数形式——保持嗅探循环连贯）。
    const auto foldRow = [&]() {
        for (std::size_t i = 0; i < 3; ++i) {
            if (inRow[i] > 0) {
                totals[i] += inRow[i];
            } else {
                allRowsHave[i] = false;         // 本行未出现——破坏"一致"
            }
            inRow[i] = 0;
        }
        ++rows;
    };

    SniffOutcome o;
    for (;;) {
        const int b = src.next();
        if (b == -2) {
            // 嗅探窗口内即判出编码非法——读出侧以 ENCODING 稳定拒绝
            // （结构统计对非法流无意义，且不得把编码错降级为方言错）。
            o.encodingInvalid = true;
            o.summary = "encoding invalid during delimiter sniff";
            return o;
        }
        if (b == -1) {
            // 流尽：末行（无行尾）只要消费过候选字节也计入采样。
            if (inRow[0] != 0 || inRow[1] != 0 || inRow[2] != 0) {
                foldRow();
            }
            break;
        }
        const char c = static_cast<char>(b);
        if (inQuote) {
            if (c == '"') {
                // ""＝字面引号（保持引号内）；单引号＝闭合。
                if (src.peek() == '"') {
                    src.next();
                } else {
                    inQuote = false;
                }
            }
            continue;                           // 引号内一切字节不计数
        }
        if (c == '"') {
            inQuote = true;
            continue;
        }
        if (c == '\r' || c == '\n') {
            if (c == '\r' && src.peek() == '\n') {
                src.next();                     // CRLF 整组消费
            }
            foldRow();
            if (rows >= kSniffMaxRows) {
                break;                          // 采样窗口封闭（§5.2：至多 100 行后续）
            }
            continue;
        }
        for (std::size_t i = 0; i < 3; ++i) {
            if (c == kCandidates[i]) {
                ++inRow[i];
            }
        }
        if (byteLimit != 0 && src.produced >= byteLimit) {
            break;                              // 预检窗口封闭（probe 形态）
        }
    }

    // 决胜：一致（每采样行 ≥1 次）且总次数严格最多；并列＝无唯一结果。
    std::size_t best = 3;
    bool unique = false;
    for (std::size_t i = 0; i < 3; ++i) {
        const bool consistent = rows > 0 && allRowsHave[i] && totals[i] > 0;
        if (!consistent) {
            continue;
        }
        if (best == 3 || totals[i] > totals[best]) {
            best = i;
            unique = true;
        } else if (totals[i] == totals[best]) {
            unique = false;                     // 并列最高——拒绝不猜测
        }
    }
    if (best != 3 && unique) {
        o.ok = true;
        o.delim = kCandidates[best];
    }
    // 统计摘要（§5.2"供调用方提示用户显式选择"——进 detail）。
    o.summary = "delimiter sniff: rows=" + std::to_string(rows)
                + " comma=" + std::to_string(totals[0])
                + " semicolon=" + std::to_string(totals[1])
                + " tab=" + std::to_string(totals[2]);
    return o;
}

// =====================================================================
// 错误映射助手（环境错误四分类——§4.2.5）
// =====================================================================

/**
 * @brief std::error_code → io 资源错误码（写暂存/原子替换失败映射）。
 *
 * 四分类映射（§4.2.5）：不存在/权限不足/介质只读/锁竞争；磁盘满映射到
 * IO-PACK-DISK-FULL——§9.12 码表中唯一的磁盘满 token（§7.3"磁盘空间不
 * 足"同义复用，detail 携带 OS 文本；CSV 写出与包导出同属写失败环境错
 * 误）。未识别形态保守归权限不足（ACCESS-DENIED 语义最接近"无法访问"）。
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
// 预算 scope 会话（§9.3 等价调整的执行件——自开自关子 scope／采用调用方 scope）
// =====================================================================

/**
 * @brief 会话级预算 scope 的 RAII 守卫。
 *
 * 语义（Csv.hpp probe/read 注登记的等价调整）：guard 非空且调用方未传
 * scope 句柄时，由会话以产品缺省规格自开子 scope（析构回收关闭）；调用
 * 方传入句柄时只在该 scope 上记账、不越权关闭（§9.3 前置"budget scope
 * 已开 SingleFileBytes/TotalBytes/CsvRowCount/CsvFieldChars"的承载——调
 * 用方可用 tighten 后的规格约束读取会话）。
 */
class BudgetScopeSession
{
public:
    BudgetScopeSession() = default;

    BudgetScopeSession(const BudgetScopeSession&) = delete;
    BudgetScopeSession& operator=(const BudgetScopeSession&) = delete;

    /// 进入会话：开 scope（内部模式）或采用调用方句柄（外部模式）。
    IoResult<void> enter(IBudgetGuard* guard, BudgetScopeId callerScope)
    {
        m_guard = guard;
        if (guard == nullptr) {
            return {};                          // null＝不记账（测试/预览轻量场景）
        }
        if (callerScope.value != 0) {
            m_scope = callerScope;              // 外部 scope：调用方所有，不关闭
            m_external = true;
            return {};
        }
        const IoResult<BudgetScopeId> r = guard->openScope(BudgetSpec::productDefault());
        if (!r) {
            // 缺省规格恒不超硬上限——触及即实现缺陷（防御性内部错误）。
            IoResult<void> rr;
            rr.error = r.error;
            return rr;
        }
        m_scope = r.value;
        return {};
    }

    /// 离开会话：内部 scope 回收关闭（父超限的错误向上传播——§4.5.2）。
    IoResult<void> leave()
    {
        if (m_guard != nullptr && !m_external && m_scope.value != 0) {
            const IoResult<void> r = m_guard->closeScope(m_scope);
            m_scope = BudgetScopeId{};
            return r;
        }
        return {};
    }

    /// 记账一笔（饱和加法在 guard 内完成——失败状态不变，调用方中止）。
    IoResult<void> charge(BudgetDimension dim, std::uint64_t amount)
    {
        if (m_guard == nullptr || m_scope.value == 0) {
            return {};
        }
        return m_guard->charge(m_scope, dim, amount);
    }

    /// 取该维生效限额（诊断三要素的 limit 值——§4.5.2 比较型三要素）。
    std::uint64_t limitOf(BudgetDimension dim) const
    {
        if (m_guard == nullptr || m_scope.value == 0) {
            return 0;
        }
        const BudgetLedgerSnapshot snap = m_guard->ledger(m_scope);
        for (const auto& entry : snap.dimensions) {
            if (entry.first == dim) {
                return entry.second.limit;
            }
        }
        return 0;
    }

    /// 析构兜底关闭（正常路径经 leave()；异常路径防 scope 泄漏——关闭
    /// 错误吞于析构：主错误已随返回值上抛，析构不得抛——§1.4）。
    ~BudgetScopeSession()
    {
        if (m_guard != nullptr && !m_external && m_scope.value != 0) {
            static_cast<void>(m_guard->closeScope(m_scope));
        }
    }

private:
    IBudgetGuard* m_guard = nullptr;        ///< 守卫（非拥有——调用方持有）
    BudgetScopeId m_scope{};                ///< 生效 scope（0＝未开）
    bool m_external = false;                ///< true＝调用方 scope（不关闭）
};

// =====================================================================
// prologue——编码预检＋首物理行捕获＋标识判定（§5.1/§5.2）
// =====================================================================

/// prologue 产物：编码预检与首物理行标识判定之后的会话状态。
struct Prologue {
    bool marked = false;            ///< 是否携带合法 v1 标识行
    CsvDialect dialect;             ///< marked 时的方言（NotMarked 时无意义）
};

/**
 * @brief 首物理行捕获与标识判定（字节级精确——§5.1 判定行）。
 *
 * 捕获首物理行（规范化字节，含行尾字节组）→ 剥离行尾 → 标识行解析：
 *   - Malformed（带 #rwcsv1 记号但语法损坏/超窗无行尾）＝致命 DIALECT
 *     （E1——该文件自称本软件格式却损坏，拒绝优于猜测）；
 *   - Ok（合法标识）＝marked，首行已消费（后续行从行 2 起）；
 *   - NotMarked＝捕获字节整组回灌种子（行解析器/嗅探器从文件首字节语
 *     义无缝续读），零改写路径。
 *
 * 编码非法（-2）在此处即致命（ENCODING——稳定拒绝，§5.2/IO-D09）。
 */
IoResult<Prologue> runPrologue(DecodedStream& src)
{
    Prologue p;
    // 第一步：捕获首物理行（含行尾字节组），越窗即判溢出。行尾三形态在
    // 此与扫描器同规则消费（CR 后看 LF）。
    std::string captured;
    bool sawEol = false;
    while (captured.size() <= kMarkerScanCapBytes) {
        const int b = src.next();
        if (b == -2) {
            IoResult<Prologue> r;
            r.error = IoError{IoErrorCode::FormatCsvEncoding, {},
                              "csv encoding: invalid utf-8 sequence without BOM"};
            return r;
        }
        if (b == -1) {
            break;
        }
        captured.push_back(static_cast<char>(b));
        if (b == '\r') {
            if (src.peek() == '\n') {
                captured.push_back(static_cast<char>(src.next())); // CRLF 整组入捕获
            }
            sawEol = true;
            break;
        }
        if (b == '\n') {
            sawEol = true;
            break;
        }
    }
    const bool overflow = !sawEol && captured.size() > kMarkerScanCapBytes;

    // 第二步：标识判定。判定对象＝行内容（剥离行尾字节组）；溢出形态没
    // 有行尾，按捕获前缀整体判定。
    std::string_view lineForMarker = captured;
    if (sawEol) {
        while (!lineForMarker.empty()
               && (lineForMarker.back() == '\r' || lineForMarker.back() == '\n')) {
            lineForMarker.remove_suffix(1);
        }
    }
    CsvDialect markerDialect;
    IoError markerErr;
    const MarkerVerdict v = parseDialectMarker(lineForMarker, &markerDialect, &markerErr);
    if (v == MarkerVerdict::Malformed || (v == MarkerVerdict::NotMarked && overflow
                                          && lineForMarker.substr(0, kMarkerToken.size())
                                                 == kMarkerToken)) {
        // 带记号但语法损坏（E1）；或带记号且越过扫描上限仍无行尾（伪造
        // 标识）——拒绝。
        IoResult<Prologue> r;
        r.error = markerErr.code == IoErrorCode::FormatCsvDialect
                      ? std::move(markerErr)
                      : IoError{IoErrorCode::FormatCsvDialect, {},
                                "dialect marker: exceeds scan cap without line break"};
        return r;
    }
    if (v == MarkerVerdict::Ok) {
        p.marked = true;
        p.dialect = markerDialect;
        IoResult<Prologue> ok;
        ok.value = p;                       // marked：首行已消费，不回灌
        return ok;
    }

    // 第三步：未标记——捕获前缀整组回灌（含行尾字节组——回灌后行界与
    // 原文件逐字节一致）。
    src.seed(std::move(captured));
    IoResult<Prologue> ok;
    ok.value = p;
    return ok;
}

// =====================================================================
// CsvReader——ICsvReader 会话实现
// =====================================================================

/**
 * @brief CSV 读取器会话（ICsvReader 实现）。
 *
 * 读取流程（read）：
 *   ① 开文件＋预算 scope（等价调整语义见 Csv.hpp probe 注）；
 *   ② 探测遍：prologue（编码/标识判定）→ 未标记时嗅探分隔符（§5.2）；
 *   ③ 交付遍：重开文件单遍流式（marked 文件免嗅探、prologue 后直接交
 *      付——单遍）。嗅探遍与交付遍分离的理由：分隔符必须在解析前确定，
 *      而嗅探需要采样至多 101 行；交付遍的"单遍流式"指内存有界的逐行
 *      交付（§5.6），两遍均不整体载入。两遍之间文件被并发改写属调用方
 *      环境问题（preview 语义可接受——AT-02），登记于类注。
 *   ④ 交付遍逐行处置：编码/引号致命错 → 预算检查点 → NUL/空行/表头/
 *      行列数（§5.4 处置表）→ 转义还原（仅 marked——§5.3）→ 交付回调。
 *
 * 确定性：同文件同选项同输出（嗅探统计与行序均为确定性算法；诊断
 * params 保序构造——NFR-COR-01/02）。
 */
class CsvReader final : public ICsvReader
{
public:
    // ---- ICsvReader ------------------------------------------------
    IoResult<CsvDialect> probe(const std::filesystem::path& file, IBudgetGuard* budget,
                               IoCancelToken* cancel, BudgetScopeId budgetScope) override
    {
        try {
            DecodedStream src;
            IoError openErr;
            if (!src.open(file, &openErr)) {
                return dialectFail(openErr);
            }
            // probe 预算：轻量窗口（预检＋采样 ≤101 行）——按 scope 语义
            // 记账实际读入字节（等价调整语义与 read 一致）。
            BudgetScopeSession budgetSession;
            if (const IoResult<void> r = budgetSession.enter(budget, budgetScope); !r) {
                return dialectFail(r.error);
            }
            const IoResult<Prologue> pro = runPrologue(src);
            if (!pro) {
                return dialectFail(pro.error);
            }
            if (pro.value.marked) {
                return dialectOk(pro.value.dialect); // marked：标识行方言即结论
            }
            // 未标记：嗅探（预检窗口 64 KiB——§5.2 编码行"首 64 KiB 预检"；
            // 全文严格校验属 read 交付遍职责）。
            const SniffOutcome sniff = sniffDelimiter(src, kEncodingPrecheckBytes);
            if (sniff.encodingInvalid) {
                IoResult<CsvDialect> r;
                r.error = IoError{IoErrorCode::FormatCsvEncoding, {}, sniff.summary};
                return r;
            }
            if (const IoResult<void> r = budgetSession.charge(BudgetDimension::SingleFileBytes,
                                                              src.rawConsumed());
                !r) {
                return dialectFail(r.error);
            }
            if (const IoResult<void> r = budgetSession.charge(BudgetDimension::TotalBytes,
                                                              src.rawConsumed());
                !r) {
                return dialectFail(r.error);
            }
            if (const IoResult<void> r = budgetSession.leave(); !r) {
                return dialectFail(r.error);
            }
            if (!sniff.ok) {
                // 无一致结果 → DIALECT（含候选统计摘要——§5.2 分隔符行）。
                IoResult<CsvDialect> r;
                r.error = IoError{IoErrorCode::FormatCsvDialect, {}, sniff.summary};
                return r;
            }
            CsvDialect d = CsvDialect::rwDefault();
            d.delimiter = sniff.delim;
            return dialectOk(d);
        } catch (const std::exception& e) {
            return dialectInternal(e.what());
        } catch (...) {
            return dialectInternal("probe: unknown exception");
        }
    }

    IoResult<RawTable> read(const std::filesystem::path& file, const CsvReadOptions& options,
                            const std::function<bool(std::uint64_t, CsvRowView&&)>& onRow,
                            IBudgetGuard* budget, IoCancelToken* cancel,
                            IoProgressCallback progress, BudgetScopeId budgetScope) override
    {
        try {
            // ① 探测遍：prologue＋（未标记时）嗅探。嗅探遍与交付遍分离
            // （理由见类注③）；marked 文件无嗅探遍，直接进交付遍。
            bool marked = false;
            char delim = ',';
            {
                DecodedStream probeStream;
                IoError openErr;
                if (!probeStream.open(file, &openErr)) {
                    return readFail(openErr);
                }
                BudgetScopeSession probeScope;
                if (const IoResult<void> r = probeScope.enter(budget, budgetScope); !r) {
                    return readFail(r.error);
                }
                const IoResult<Prologue> pro = runPrologue(probeStream);
                if (!pro) {
                    return readFail(pro.error);
                }
                marked = pro.value.marked;
                delim = pro.value.dialect.delimiter;
                if (!marked) {
                    // 无标识：嗅探（全文窗口——编码校验由交付遍兜底）。
                    const SniffOutcome sniff = sniffDelimiter(probeStream, 0);
                    if (sniff.encodingInvalid) {
                        IoResult<RawTable> r;
                        r.error = IoError{IoErrorCode::FormatCsvEncoding, {}, sniff.summary};
                        return r;
                    }
                    if (!sniff.ok) {
                        IoResult<RawTable> r;
                        r.error = IoError{IoErrorCode::FormatCsvDialect, {}, sniff.summary};
                        return r;
                    }
                    delim = sniff.delim;
                    // 嗅探遍读入字节计入会话预算（TotalBytes 累计语义）。
                    if (const IoResult<void> r =
                            chargePassBytes(probeScope, probeStream.rawConsumed());
                        !r) {
                        return readFail(r.error);
                    }
                }
                if (const IoResult<void> r = probeScope.leave(); !r) {
                    return readFail(r.error);
                }
                if (cancel != nullptr && cancel->isCancelled()) {
                    IoResult<RawTable> r;
                    r.error = IoError{IoErrorCode::Cancelled, {}, "csv read: cancelled before delivery pass"};
                    return r;
                }
            }

            // ② 交付遍：重开文件单遍流式（deliver 内再次 prologue——同字
            // 节同判定，marked 文件的标识行方言在此重新生效）。
            DecodedStream src;
            IoError openErr;
            if (!src.open(file, &openErr)) {
                return readFail(openErr);
            }
            BudgetScopeSession scopeSession;
            if (const IoResult<void> r = scopeSession.enter(budget, budgetScope); !r) {
                return readFail(r.error);
            }
            const IoResult<Prologue> pro2 = runPrologue(src);
            if (!pro2) {
                return readFail(pro2.error);
            }
            // 生效方言：带标识＝标识行声明；无标识＝嗅探结果（其余键取
            // v1 缺省——§5.2 嗅探只决定分隔符）。
            CsvDialect effective = pro2.value.dialect;
            if (!pro2.value.marked) {
                effective = CsvDialect::rwDefault();
                effective.delimiter = delim;
            }
            return deliver(src, effective, pro2.value.marked, options, onRow, scopeSession,
                           cancel, std::move(progress));
        } catch (const std::exception& e) {
            return readInternal(e.what());
        } catch (...) {
            return readInternal("read: unknown exception");
        }
    }

private:
    // -----------------------------------------------------------------
    // 交付遍：逐行处置主循环（§5.4 处置表＋§5.6 部分成功的执行点）
    // -----------------------------------------------------------------
    IoResult<RawTable> deliver(DecodedStream& src, const CsvDialect& dialect, bool marked,
                               const CsvReadOptions& options,
                               const std::function<bool(std::uint64_t, CsvRowView&&)>& onRow,
                               BudgetScopeSession& budgetScope, IoCancelToken* cancel,
                               IoProgressCallback progress)
    {
        RawTable table;
        table.report.arityApplied = options.arity;
        table.report.blankApplied = options.blank;
        table.report.marked = marked;
        table.report.dialect = dialect;         // 实际生效方言（标识行声明或嗅探）

        RowScanner scanner(src, dialect.delimiter);
        std::vector<std::string> fields;
        bool terminatedByEol = false;
        // 物理行号（1 起；标识行占行 1——§5.6"含标识行偏移"口径）：
        // marked 时首物理行已被 prologue 消费，行号从 2 起；未 marked 从 1 起。
        std::uint64_t rowNo = marked ? 1 : 0;

        for (;;) {
            // ---- 取消检查点（每行——§9.3 取消行为行）。取消是状态不是
            // 错误（UX-03）：返回 IO-CANCELLED，调用方不落诊断。
            if (cancel != nullptr && cancel->isCancelled()) {
                IoResult<RawTable> r;
                r.error = IoError{IoErrorCode::Cancelled, {},
                                  "csv read: cancelled at row checkpoint"};
                return r;
            }
            ++rowNo;
            scanner.setCurrentRow(rowNo);
            if (!scanner.nextRow(fields, terminatedByEol)) {
                break;                          // 干净流尽
            }

            // ---- 编码致命错优先（-2 不得被当成结构/数据处理——稳定拒
            // 绝语义，§5.2/IO-D09）。
            if (scanner.encodingError()) {
                IoResult<RawTable> r;
                r.error = IoError{IoErrorCode::FormatCsvEncoding, {},
                                  "csv read: invalid utf-8 sequence without BOM"};
                return r;
            }

            // ---- 行预算检查点（每行一笔——§4.4⑤；超限即中止读会话，
            // 已交付行由调用方丢弃——§9.3 后置）。
            if (const IoResult<void> r = budgetScope.charge(BudgetDimension::CsvRowCount, 1);
                !r) {
                return readFail(r.error);
            }

            // ---- 防御上限（字段超长/字段数超限）→ IO-SEC-BUDGET-FIELD
            // （Budget.hpp SecBudgetField 注"单元格长度/字段数超限"）。
            if (scanner.fieldOverCap() || scanner.rowFieldsOverCap()) {
                const std::uint64_t hardCap = scanner.fieldOverCap()
                    ? budgetScope.limitOf(BudgetDimension::CsvFieldChars)
                    : kRowFieldsCap;
                IoResult<RawTable> r;
                r.error = makeComparativeError(
                    IoErrorCode::SecBudgetField,
                    scanner.fieldOverCap() ? scanner.overCapFieldLen() : kRowFieldsCap,
                    hardCap != 0 ? hardCap : kFieldScanCapBytes,
                    scanner.fieldOverCap() ? "chars" : "count",
                    "csv read: field/fields defensive cap exceeded (units/io.md E8)");
                return r;
            }

            // ---- 进度回调（done＝已消费数据行号；total＝0 流式未知）。
            if (progress) {
                progress(IoProgress{rowNo, 0, "csv-read"});
            }

            // ---- 行字符记账（本行各字段总长——字段维检查点）。
            std::uint64_t rowChars = 0;
            for (const std::string& f : fields) {
                rowChars += f.size();
            }
            if (rowChars > 0) {
                if (const IoResult<void> r =
                        budgetScope.charge(BudgetDimension::CsvFieldChars, rowChars);
                    !r) {
                    return readFail(r.error);
                }
            }

            // ---- 致命结构错误：引号不闭合（§5.4——定位起始行；自该行
            // 起行结构不可信，按读失败终止，不做行级容错）。
            if (scanner.unclosedQuote()) {
                IoResult<RawTable> r;
                r.error = makeRowColError(IoErrorCode::FormatCsvQuote, scanner.unclosedRow(), 0,
                                          truncateSnippet(joinRaw(fields)),
                                          "csv read: unclosed quote from this row (units/io.md §5.4)");
                return r;
            }

            // ---- NUL 检查（§5.4 非法字符行：仅 NUL 拒绝——无法往返于文
            // 本工具链；定位首含 NUL 列）。行级错误：错误行不交付。
            {
                std::size_t nulCol = 0;
                for (std::size_t i = 0; i < fields.size(); ++i) {
                    if (fields[i].find('\0') != std::string::npos) {
                        nulCol = i + 1;
                        break;
                    }
                }
                if (nulCol != 0) {
                    recordRowError(table, options, rowNo, nulCol, std::nullopt, joinRaw(fields),
                                   IoErrorCode::FormatCsvChar, "NUL byte in field (units/io.md §5.4)");
                    if (options.stopOnFirstRowError) {
                        break;                  // 调用方选择首错即停（ok 结束）
                    }
                    continue;
                }
            }

            // ---- 空行处置（§5.4：完全空行＝零字段语义的单空字段行；判
            // 定基于文件层字段——转义还原之前，空行没有业务语义）。
            if (fields.size() == 1 && fields[0].empty()) {
                if (options.blank == CsvBlankPolicy::Skip) {
                    ++table.report.blankSkipped;
                    continue;                   // 跳过并计数（§5.4 缺省）
                }
                // Reject 策略：计为行错误（§5.4"可改为拒绝"；卡面未给该
                // 处置专属码——按结构事实归 ARITY，detail 注明策略出处）。
                ++table.report.blankRejected;
                recordRowError(table, options, rowNo, 0, std::nullopt, std::string(),
                               IoErrorCode::FormatCsvArity,
                               "blank row rejected by CsvBlankPolicy::Reject (units/io.md §5.4)");
                if (options.stopOnFirstRowError) {
                    break;
                }
                continue;
            }

            // ---- 转义还原（§5.3：仅带标识文件剥离前缀；无标识零改写——
            // acceptance 3 的被测语义）。表头行与数据行同规则。
            if (marked) {
                for (std::string& f : fields) {
                    if (!f.empty() && f.front() == '\'') {
                        f.erase(f.begin());     // 剥离恰好一个前导 '
                    }
                }
            }

            // ---- 表头行处置（§5.4 表头行：由调用方指定哪一行是表头）。
            if (options.headerRow.has_value() && options.headerRow.value() == rowNo) {
                // 重复列名（ASCII 折叠后同名）＝致命拒绝（§5.4 DUPCOL——
                // 列映射歧义无法按行容错；诊断列出列号与名字）。
                const std::uint64_t dupCol = firstDupCol(fields);
                if (dupCol != 0) {
                    std::string dupDetail;
                    for (std::size_t j = 0; j < fields.size(); ++j) {
                        for (std::size_t i = 0; i < j; ++i) {
                            if (asciiFoldEqual(fields[i], fields[j])) {
                                dupDetail += " col" + std::to_string(j + 1) + "='" + fields[j]
                                             + "' (dup of col" + std::to_string(i + 1) + ");";
                            }
                        }
                    }
                    IoResult<RawTable> r;
                    r.error = makeRowColError(
                        IoErrorCode::FormatCsvDupCol, rowNo, dupCol,
                        truncateSnippet(fields[dupCol - 1]),
                        "csv read: duplicate header names:" + dupDetail + " (units/io.md §5.4)");
                    return r;
                }
                table.report.hasHeader = true;
                table.report.header = fields;   // 表头原文（剥离转义后）
                continue;                       // 表头行不作数据交付
            }

            // ---- 行列数处置（§5.4 缺失列/多余列——默认拒绝并定位，策略
            // 模式按声明处置并记录）。
            if (table.report.hasHeader) {
                const std::size_t expected = table.report.header.size();
                const std::size_t actual = fields.size();
                if (actual < expected) {
                    if (options.arity == CsvArityPolicy::PadTrailing) {
                        fields.resize(expected);    // 补空串（策略处置并记录）
                        ++table.report.rowsPadded;
                    } else {
                        // 默认拒绝：定位行号/期望/实际（§5.4）；列号定位
                        // 首个缺失列；fieldName 取该列表头名（若存在）。
                        std::optional<IoString> fieldName;
                        if (actual < table.report.header.size()) {
                            fieldName = table.report.header[actual];
                        }
                        recordArityError(table, options, rowNo, actual + 1, fieldName,
                                         joinRaw(fields), expected, actual);
                        if (options.stopOnFirstRowError) {
                            break;
                        }
                        continue;
                    }
                } else if (actual > expected) {
                    if (options.arity == CsvArityPolicy::TrimExcess) {
                        fields.resize(expected);    // 截断多余列（策略处置并记录）
                        ++table.report.rowsTrimmed;
                    } else {
                        // 默认拒绝（多余列可能是映射错误信号——§5.4）。
                        recordArityError(table, options, rowNo, expected + 1, std::nullopt,
                                         joinRaw(fields), expected, actual);
                        if (options.stopOnFirstRowError) {
                            break;
                        }
                        continue;
                    }
                }
            }

            // ---- 交付正确行（§5.6：行视图仅在回调内有效；错误行不走此
            // 路——"错误行不进入业务数据模型"）。
            if (options.retainRows) {
                table.rows.push_back(fields);       // 保留行（拷贝进 RawTable）
            }
            if (onRow) {
                CsvRowView view;
                view.rowNo = rowNo;
                view.fields.reserve(fields.size());
                for (const std::string& f : fields) {
                    view.fields.emplace_back(f.data(), f.size()); // 零拷贝视图
                }
                if (!onRow(rowNo, std::move(view))) {
                    break;                          // 调用方要求提前停止（ok 结束）
                }
            }
            ++table.report.dataRows;
        }

        // 会话收尾：原始字节对账入账（探测遍＋交付遍累计——SingleFile
        // 以交付遍实际值入账，TotalBytes 累计两会话）＋scope 回收。
        if (const IoResult<void> r = chargePassBytes(budgetScope, src.rawConsumed()); !r) {
            return readFail(r.error);
        }
        if (const IoResult<void> r = budgetScope.leave(); !r) {
            return readFail(r.error);
        }
        return tableOk(std::move(table));
    }

    // -----------------------------------------------------------------
    // 行错误登记（§5.6 五元组＋maxRowErrors 截断计数）
    // -----------------------------------------------------------------

    /**
     * @brief 登记一条行错误（NUL/空行拒绝等行级处置）。
     *
     * 五元组（§5.6）：rowNo（含标识行偏移）/colNo/fieldName/rawSnippet
     * （截断至 120 字节 UTF-8 边界）/reason（稳定码＋row/column/snippet
     * 三键 params——IoDiagnostics.hpp CSV 族 paramSchema 对齐）。超出
     * maxRowErrors 截断＋计数（§5.6）。
     */
    static void recordRowError(RawTable& table, const CsvReadOptions& options,
                               std::uint64_t rowNo, std::uint64_t colNo,
                               const std::optional<IoString>& fieldName,
                               std::string_view rawRow, IoErrorCode code, std::string detail)
    {
        ++table.report.errorRows;
        if (table.rowErrors.size() < options.maxRowErrors) {
            CsvRowError e;
            e.rowNo = rowNo;
            e.colNo = colNo;
            e.fieldName = fieldName;
            e.rawSnippet = truncateSnippet(rawRow);
            e.reason = makeRowColError(code, rowNo, colNo, e.rawSnippet, std::move(detail));
            table.rowErrors.push_back(std::move(e));
        } else {
            ++table.report.rowErrorsTruncated;      // 超出截断＋计数（§5.6）
        }
    }

    /// 行列数失配的行错误（ARITY——params 追加 expected/actual 比较对，
    /// "定位（行号/期望/实际）"——§5.4）。
    static void recordArityError(RawTable& table, const CsvReadOptions& options,
                                 std::uint64_t rowNo, std::uint64_t colNo,
                                 const std::optional<IoString>& fieldName,
                                 std::string_view rawRow, std::uint64_t expected,
                                 std::uint64_t actual)
    {
        ++table.report.errorRows;
        if (table.rowErrors.size() < options.maxRowErrors) {
            CsvRowError e;
            e.rowNo = rowNo;
            e.colNo = colNo;
            e.fieldName = fieldName;
            e.rawSnippet = truncateSnippet(rawRow);
            e.reason = makeRowColError(IoErrorCode::FormatCsvArity, rowNo, colNo, e.rawSnippet,
                                       "csv read: column count mismatch (units/io.md §5.4)");
            // 追加期望/实际比较参数（诊断三要素之外的结构定位补充）。
            e.reason.params.emplace_back("expected", std::to_string(expected));
            e.reason.params.emplace_back("actual", std::to_string(actual));
            table.rowErrors.push_back(std::move(e));
        } else {
            ++table.report.rowErrorsTruncated;
        }
    }

    // -----------------------------------------------------------------
    // 小工具
    // -----------------------------------------------------------------

    /// 一遍读入字节入账：SingleFileBytes（本遍文件维）＋TotalBytes（会
    /// 话累计维）同额两笔（§4.5.1 两维语义；guard 内饱和加法）。
    static IoResult<void> chargePassBytes(BudgetScopeSession& budgetScope, std::uint64_t bytes)
    {
        if (bytes == 0) {
            return {};
        }
        if (const IoResult<void> r = budgetScope.charge(BudgetDimension::SingleFileBytes, bytes);
            !r) {
            return r;
        }
        return budgetScope.charge(BudgetDimension::TotalBytes, bytes);
    }

    static IoResult<CsvDialect> dialectOk(CsvDialect d)
    {
        IoResult<CsvDialect> r;
        r.value = d;
        return r;
    }
    static IoResult<RawTable> tableOk(RawTable t)
    {
        IoResult<RawTable> r;
        r.value = std::move(t);
        return r;
    }
    static IoResult<CsvDialect> dialectFail(IoError e)
    {
        IoResult<CsvDialect> r;
        r.error = std::move(e);
        return r;
    }
    static IoResult<RawTable> readFail(IoError e)
    {
        IoResult<RawTable> r;
        r.error = std::move(e);
        return r;
    }
    static IoResult<CsvDialect> dialectInternal(const char* what)
    {
        IoResult<CsvDialect> r;
        r.error = IoError{IoErrorCode::FormatInternal, {},
                          std::string("csv probe: internal failure: ") + what};
        return r;
    }
    static IoResult<RawTable> readInternal(const char* what)
    {
        IoResult<RawTable> r;
        r.error = IoError{IoErrorCode::FormatInternal, {},
                          std::string("csv read: internal failure: ") + what};
        return r;
    }
};

// =====================================================================
// CsvWriter——ICsvWriter 会话实现（唯一转义点——§5.7 流程图）
// =====================================================================

/**
 * @brief CSV 写出器会话（ICsvWriter 实现）。
 *
 * 写出协议（§9.4 后置条件行的落实）：
 *   - open：校验方言（§5.1 封闭键集的写出侧镜像）；文件目标即建同目录
 *     暂存文件做存在性检查并即删（尽早暴露权限/占用——失败目标不变；
 *     真正写入在 finish）；
 *   - writeHeader/writeRow：编码进内部 canonical 缓冲（文本＝转义＋引
 *     号化；数值＝to_chars；NUL＝CHAR 拒绝；非有限实数＝INTERNAL 拒绝）；
 *   - finish：缓冲落暂存→刷盘关闭→同卷 rename 原子替换发布目标（成功
 *     ＝目标原子就位；失败＝清理暂存、目标不变）；
 *   - 析构未 finish＝放弃＝删暂存（§9.4 生命周期行"RAII：未 finish 析
 *     构＝放弃＝清理"；§5.6"取消即中止（目标不受影响）"——调用方放弃
 *     /取消时直接丢弃实例即可）。
 *
 * 确定性：canonical 字节序（固定键序方言行＋声明行尾＋to_chars 最短表
 * 示）——同数据两次导出字节相同（§5.5 文件层承诺）。
 */
class CsvWriter final : public ICsvWriter
{
public:
    ~CsvWriter() override
    {
        // RAII 放弃语义：未 finish＝删除暂存（错误吞没——析构不得抛，
        // §1.4；残留概率极低且路径在调用方目标旁、带固定后缀可识别）。
        if (m_opened && !m_finished && m_fileTarget && !m_tempPath.empty()) {
            std::error_code ec;
            std::filesystem::remove(m_tempPath, ec);
        }
    }

    IoResult<void> open(CsvOutputTarget&& target, const CsvWriteOptions& options) override
    {
        try {
            if (m_opened) {
                return contractBreach("csv write: open twice");
            }
            // ---- 方言校验（§5.1 封闭键集的写出侧镜像——拒绝不修正）。
            const CsvDialect& d = options.dialect;
            if (d.delimiter != ',' && d.delimiter != ';' && d.delimiter != '\t') {
                IoResult<void> r;
                r.error = IoError{IoErrorCode::FormatCsvDialect, {},
                                  "csv write: delimiter outside v1 closed set"};
                return r;
            }
            if (d.quote != '"') {
                // quote 恒 '"'——声明 ' 即拒（§5.1："与转义符冲突"）。
                IoResult<void> r;
                r.error = IoError{IoErrorCode::FormatCsvDialect, {},
                                  "csv write: quote must be double quote in v1"};
                return r;
            }
            if (d.encoding != CsvEncoding::Utf8) {
                IoResult<void> r;
                r.error = IoError{IoErrorCode::FormatCsvDialect, {},
                                  "csv write: encoding must be utf-8 in v1"};
                return r;
            }
            m_options = options;
            m_target = std::move(target);
            if (m_target.kind == CsvOutputTarget::Kind::MemoryBuffer) {
                if (m_target.buffer == nullptr) {
                    return contractBreach("csv write: null memory buffer");
                }
                m_fileTarget = false;
            } else {
                m_fileTarget = true;
                if (m_target.filePath.empty()) {
                    return contractBreach("csv write: empty file path");
                }
                // 父目录必须已存在（io 不隐式建目录——目录职责归调用方/
                // 临时区设施）。相对路径父目录为空时按当前目录处理。
                std::filesystem::path parent = m_target.filePath.parent_path();
                if (parent.empty()) {
                    parent = ".";
                }
                std::error_code ec;
                if (!std::filesystem::is_directory(parent, ec)) {
                    IoResult<void> r;
                    r.error = IoError{IoErrorCode::ResNotFound, {},
                                      "csv write: target parent directory missing"};
                    return r;
                }
                // 同目录暂存路径（同卷 rename 的前提——§9.4 原子替换）。
                // 固定后缀命名（确定性；并发写同一目标属调用方契约违约——
                // 会话对象单线程且一实例一目标）。
                m_tempPath = m_target.filePath;
                m_tempPath += ".ird-csv-tmp";
                // 暂存可建性检查（尽早暴露权限/占用——即建即删，真正写
                // 入在 finish；失败时目标不受影响）。
                {
                    std::ofstream staging(m_tempPath, std::ios::binary | std::ios::trunc);
                    if (!staging.is_open()) {
                        // ofstream 不暴露 errno 的可移植读取；目录不存在
                        // 已在前置检查拦截，余下失败保守归权限不足（四分
                        // 类之二——§4.2.5），detail 说明待查方向。
                        IoResult<void> r;
                        r.error = IoError{IoErrorCode::ResAccessDenied, {},
                                          "csv write: cannot create staging file"};
                        return r;
                    }
                }
                std::error_code rmEc;
                std::filesystem::remove(m_tempPath, rmEc); // 检查用文件即删
            }
            // 标识行（§5.1：本软件导出恒带 v1 标识行、恒不带 BOM——写侧
            // 无 BOM 由"从不写 BOM 字节"结构性保证）。
            if (m_options.emitDialectMarker) {
                m_buf += renderDialectMarker(m_options.dialect);
                m_buf += m_options.dialect.eol == CsvEol::Crlf ? "\r\n" : "\n";
            }
            m_opened = true;
            return {};
        } catch (const std::exception& e) {
            return writeInternal(e.what());
        } catch (...) {
            return writeInternal("open: unknown exception");
        }
    }

    IoResult<void> writeHeader(const std::vector<IoString>& names) override
    {
        try {
            if (!m_opened || m_finished || m_broken) {
                return contractBreach("csv write: header outside open session");
            }
            // 表头＝文本字段行（同转义规则——表头同样可能被表格软件误执
            // 行，防护不豁免首行；重复列名不在此查——§9.4"不查重"）。
            return writeTextRow(names);
        } catch (const std::exception& e) {
            m_broken = true;
            return writeInternal(e.what());
        } catch (...) {
            m_broken = true;
            return writeInternal("writeHeader: unknown exception");
        }
    }

    IoResult<void> writeRow(const std::vector<CsvCell>& cells) override
    {
        try {
            if (!m_opened || m_finished || m_broken) {
                return contractBreach("csv write: row outside open session");
            }
            for (std::size_t i = 0; i < cells.size(); ++i) {
                if (i > 0) {
                    m_buf.push_back(m_options.dialect.delimiter);
                }
                const CsvCell& c = cells[i];
                switch (c.kind) {
                case CsvCell::Kind::Empty:
                    break;                      // 空标记＝空字段（不经转义——§5.3）
                case CsvCell::Kind::Text:
                    // 转义＋引号化（唯一转义点）；NUL 命中即拒（会话作
                    // 废——失败后 finish 拒绝，目标不变）。
                    if (const IoResult<void> er = encodeTextField(c.text); !er) {
                        m_broken = true;
                        return er;
                    }
                    break;
                case CsvCell::Kind::Int: {
                    // 十进制 to_chars（无本地化——§5.5 数值规范化行）。
                    // 整数文本不含分隔符/引号/行尾字节，引号化判定自然
                    // 不命中（appendFieldText 内统一判定）。
                    std::array<char, 32> tmp{};
                    const auto res = std::to_chars(tmp.data(), tmp.data() + tmp.size(), c.integer);
                    // C++17 的 string_view 无 (首指针, 尾指针) 迭代对构造
                    // （C++20 才有）——以长度形态构造（确定性等价）。
                    appendFieldText(
                        std::string_view(tmp.data(), static_cast<std::size_t>(res.ptr - tmp.data())));
                    break;
                }
                case CsvCell::Kind::Real: {
                    if (!std::isfinite(c.real)) {
                        // 调用方契约违约（§9.4 前置"isfinite 断言"）——
                        // io 非抛出约束下按防御性内部错误拒绝（不静默落
                        // 盘 nan/inf——那会污染字节一致性与下游解析）。
                        return contractBreach("csv write: non-finite real cell");
                    }
                    // 最短往返表示（'.' 小数点、无本地化——同一 double
                    // 两次写出字节相同，§5.5 文件层字节一致的基础）。
                    std::array<char, 64> tmp{};
                    const auto res = std::to_chars(tmp.data(), tmp.data() + tmp.size(), c.real);
                    appendFieldText(
                        std::string_view(tmp.data(), static_cast<std::size_t>(res.ptr - tmp.data())));
                    break;
                }
                }
            }
            m_buf += m_options.dialect.eol == CsvEol::Crlf ? "\r\n" : "\n";
            return {};
        } catch (const std::exception& e) {
            m_broken = true;
            return writeInternal(e.what());
        } catch (...) {
            m_broken = true;
            return writeInternal("writeRow: unknown exception");
        }
    }

    IoResult<void> finish() override
    {
        try {
            if (!m_opened || m_finished) {
                return contractBreach("csv write: finish outside open session");
            }
            if (m_broken) {
                return contractBreach("csv write: session broken by earlier failure");
            }
            if (m_fileTarget) {
                // 暂存写入（截断重建——open 期的检查文件已删除）。
                {
                    std::ofstream out(m_tempPath, std::ios::binary | std::ios::trunc);
                    if (!out.is_open()) {
                        IoResult<void> r;
                        r.error = IoError{IoErrorCode::ResAccessDenied, {},
                                          "csv finish: cannot open staging file"};
                        return r;
                    }
                    out.write(m_buf.data(), static_cast<std::streamsize>(m_buf.size()));
                    out.flush();
                    if (!out) {
                        IoResult<void> r;
                        r.error = IoError{IoErrorCode::PackDiskFull, {},
                                          "csv finish: staging write failed (disk full?)"};
                        return r;
                    }
                } // 关闭成功后才能替换（Windows 语义：打开句柄会锁 rename）
                // 原子替换：同卷 rename（MSVC std::filesystem::rename 以
                // MoveFileEx(REPLACE_EXISTING) 语义替换既有文件——§9.4 后
                // 置"目标原子就位"；失败＝清理暂存、目标不变）。
                std::error_code ec;
                std::filesystem::rename(m_tempPath, m_target.filePath, ec);
                if (ec) {
                    std::error_code rmEc;
                    std::filesystem::remove(m_tempPath, rmEc); // 清理暂存（尽力）
                    IoResult<void> r;
                    r.error = IoError{mapSystemError(ec), {},
                                      "csv finish: atomic replace failed: " + ec.message()};
                    return r;
                }
            } else {
                m_target.buffer->append(m_buf);     // 内存目标：canonical 字节整体交付
            }
            m_finished = true;
            return {};
        } catch (const std::exception& e) {
            return writeInternal(e.what());
        } catch (...) {
            return writeInternal("finish: unknown exception");
        }
    }

private:
    /// 文本字段编码（§5.3 转义 → RFC4180 引号化判定）。NUL 在此拒绝
    /// （E7 写侧——"写入侧拒绝 NUL 入 CSV"，NUL 无法往返于文本工具链）。
    IoResult<void> encodeTextField(const IoString& text)
    {
        if (text.find('\0') != std::string::npos) {
            IoResult<void> r;
            r.error = IoError{IoErrorCode::FormatCsvChar, {},
                              "csv write: NUL byte in text cell"};
            return r;
        }
        // 前缀转义（唯一实现点——SA-12；数值/空字段不经此规则，本函数
        // 只服务文本字段）→ 引号化落缓冲。
        appendFieldText(escapeCsvText(text));
        return {};
    }

    /// 字段文本落缓冲：含分隔符/引号/行尾字节即整体引号化（RFC4180——
    /// E12 写出列）；引号内 " 加倍。
    void appendFieldText(std::string_view field)
    {
        const bool needsQuote = field.find(m_options.dialect.delimiter) != std::string_view::npos
                                || field.find('"') != std::string_view::npos
                                || field.find('\n') != std::string_view::npos
                                || field.find('\r') != std::string_view::npos;
        if (!needsQuote) {
            m_buf.append(field);
            return;
        }
        m_buf.push_back('"');
        for (const char c : field) {
            if (c == '"') {
                m_buf.push_back('"');           // ""＝字面引号（§5.2 引号行）
            }
            m_buf.push_back(c);
        }
        m_buf.push_back('"');
    }

    /// 表头/文本行整体编码（writeHeader 专用——逐名 NUL 检查后编码）。
    IoResult<void> writeTextRow(const std::vector<IoString>& names)
    {
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (i > 0) {
                m_buf.push_back(m_options.dialect.delimiter);
            }
            if (const IoResult<void> er = encodeTextField(names[i]); !er) {
                m_broken = true;
                return er;                  // NUL 拒绝（E7 写侧）
            }
        }
        m_buf += m_options.dialect.eol == CsvEol::Crlf ? "\r\n" : "\n";
        return {};
    }

    /// 调用方契约违约的防御性拒绝（§9.4 非法调用行；io 对外非抛出约束
    /// 下的 fail-fast 形态——触及即报缺陷，不静默继续）。
    static IoResult<void> contractBreach(const char* what)
    {
        IoResult<void> r;
        r.error = IoError{IoErrorCode::FormatInternal, {}, std::string(what)};
        return r;
    }

    static IoResult<void> writeInternal(const char* what)
    {
        IoResult<void> r;
        r.error = IoError{IoErrorCode::FormatInternal, {},
                          std::string("csv write: internal failure: ") + what};
        return r;
    }

    CsvWriteOptions m_options;                  ///< open 时固化的写出选项
    CsvOutputTarget m_target;                   ///< 输出目标（open 迁移持有语义）
    std::string m_buf;                          ///< canonical 字节缓冲（§5.6"缓冲写出"）
    bool m_opened = false;                      ///< open 成功标志
    bool m_finished = false;                    ///< finish 成功标志
    bool m_broken = false;                      ///< 会话损坏标志（写失败后作废）
    bool m_fileTarget = false;                  ///< 目标类别（文件/内存）
    std::filesystem::path m_tempPath;           ///< 同目录暂存路径（文件目标）
};

} // namespace

// =====================================================================
// 可逆转义编码与方言行渲染（§5.3/§5.1 唯一实现点——公共契约函数，声明
// 见 Csv.hpp；定义置于匿名命名空间之外——公共函数的唯一落位）
// =====================================================================

IoString escapeCsvText(const IoString& text)
{
    // §5.3 导出行：文本字段以 = + - @ ' 之一开头 → 前置恰好一个 '；
    // 其余（含空串）原文原样。只看首字节——ASCII 前缀在 UTF-8 下不可
    // 能与多字节序列混淆（多字节序列首字节 ≥ 0x80）。
    if (!text.empty()) {
        const char c = text.front();
        if (c == '=' || c == '+' || c == '-' || c == '@' || c == '\'') {
            return "'" + text;
        }
    }
    return text;
}

IoString unescapeCsvText(const IoString& field)
{
    // §5.3 导入行（带标识文件）：字段以 ' 开头 → 剥离恰好一个前导 '。
    // 只剥一层是可逆性的正确形态：原文自带前缀的样例（'abc）经转义成
    // ''abc，还原剥一层得 'abc——escape/unescape 构成唯一转义对（SA-12）。
    if (!field.empty() && field.front() == '\'') {
        return field.substr(1);
    }
    return field;
}

IoString renderDialectMarker(const CsvDialect& dialect)
{
    // §5.1 原文词法的 canonical 渲染：键序固定 delimiter→quote→eol→
    // encoding（同方言同字节——§5.5 文件层字节一致的前提）；缺省方言的
    // 产出与卡面示例逐字节相同（P-IO-5：v1 语法按卡面原文实现，不私改）。
    IoString line(kMarkerToken);
    line += " delimiter=";
    // delimiter=tab 按词法写作 "tab"（§5.1 键集列）；',' 与 ';' 渲染原字符。
    if (dialect.delimiter == '\t') {
        line += "tab";
    } else {
        line.push_back(dialect.delimiter);
    }
    // quote 恒 '"'（值渲染为原始字符——§5.1 示例 quote="）。
    line += " quote=\"";
    line += " eol=";
    line += (dialect.eol == CsvEol::Crlf) ? "CRLF" : "LF";
    // encoding v1 恒 utf-8（封闭集——CsvEncoding 单值枚举的类型化表达）。
    line += " encoding=utf-8";
    return line;
}

// =====================================================================
// 工厂（§9.11 访问器落位前的装配面——见 Csv.hpp 注）
// =====================================================================

std::unique_ptr<ICsvReader> makeCsvReader()
{
    return std::make_unique<CsvReader>();
}

std::unique_ptr<ICsvWriter> makeCsvWriter()
{
    return std::make_unique<CsvWriter>();
}

} // namespace sdurws::ird::io
