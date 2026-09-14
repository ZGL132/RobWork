/**
 * @file   Redaction.cpp
 * @brief  脱敏服务实现——§7.7 规则表全量承载（凭据键值/令牌形态/环境变量/
 *         用户名/路径四策略/User 档内部模式）＋§7.3② 降级链（整条替换＋
 *         DIAG-REDACTION-FAILED 开发诊断，绝不放行原文）。
 *
 * 设计依据：
 *   - units/diagnostics.md §7.7（脱敏规则表）、§7.3②（管线脱敏步骤与失败
 *     降级）、§9.5（IRedactionService 接口/契约表——纯函数、绝不抛出、策略
 *     快照读、写时拷贝切换）、§10 DT-SEC-1~4 行
 *   - 需求 NFR-SEC-07（敏感路径/用户数据/完整外部资源内容不记录；凭据一律
 *     不记录——无配置例外）、NFR-COR-01（确定性——纯文本扫描，不查询进程
 *     环境）、R-7（模式命中计数可观测）
 *   - 任务契约 tasks/foundation/DIAG-T08.json acceptance 1（DT-SEC-1~4）、
 *     acceptance 4（P-DIAG-1/CR-02：Hash 策略的 SHA-256 只经 core
 *     ContentDigester——不私设第二哈希路径，core 冻结 diff 后增量同步）
 *
 * 实现口径（§7.7/§9.5 未定判据，按 DTB §5.4 登记于单元卡 §14.4 v0.9）：
 *   1. **规则执行序**：①凭据键值→②认证方案凭证→③环境变量引用→④用户名
 *      路径段→⑤路径候选四策略→⑥令牌形态→⑦User 档内部模式。凭据优先于路
 *      径（"password=D:\x"整值替换）；用户名先于路径策略（Keep 策略下用户
 *      名也不泄露——§7.7 用户名行无策略条件）；令牌形态在路径之后扫描（路
 *      径已替换，防把 RootOnly 剩余段误判为凭证串）。
 *   2. **纯十六进制长串不按令牌处理**：≥40 字符的凭证串须同时含字母与数字
 *      且非纯十六进制——纯十六进制为摘要形态（内容身份），误伤会让开发诊断
 *      失去价值（R-7）；规范身份"<tag>-<32 hex>"全串 36 字符亦不达 40 阈值。
 *      具名秘密（password=…）由规则①兜住，不依赖形态规则。
 *   3. **Hash 策略输出形态**：[PATH-<16 位小写 hex>]＝SHA-256 前 8 字节——
 *      64 位截断足以支撑"同路径可关联、原文不可逆推"；避开管线 Tier-U
 *      "≥32 位十六进制→[HASH]"呈现过滤（防止二次改写）与 core 规范身份
 *      形态（防混淆）。
 *   4. **RootOnly 的退化形态**：仅文件名无目录层级（D:\m.stl）或仅一级目
 *      录带尾分隔符（D:\data\）时无中间结构可隐藏——输出保持原样/去文件名
 *      形态，不伪造"…"（§7.7 文本口径"保留盘符＋一级目录"；卡内示例
 *      D:\…\file.stl 与该文本不一致，按文本实现——登记）。
 *   5. **相对路径不处理**：§7.7 约束对象是"本机路径"（盘符/UNC/POSIX 绝对
 *      形态）；相对路径不含机器结构，处理即误伤（R-7）。
 *   6. **maxPreviewBytes 为策略位非行为位**：资源预览由资源所有方（evi-
 *      dence/io 产码路径）按上限生成后经 redact 过滤（§7.7"经敏感模式过
 *      滤"的分工）——本服务不产资源预览，仅随策略携带经 policy() 暴露。
 *   7. **降级链不递归**：thread_local 防护保证"失败诊断的发射链路"内不再
 *      进入第二次降级发射；诊断消息为常量文本（绝不回显原文——放行原文即
 *      防线失效）。failureSink 为空＝静默降级（只出字面量）——装配前的合
 *      法降态。
 *   8. **正则方言约束**：std::regex ECMAScript 语法无 lookbehind——左边界
 *      一律用"前导字符捕获组"承载并在替换中回填（右边界可用 lookahead 保
 *      留）；前导字符被命中消费意味着相邻同形候选不重叠（词法扫描惯例）。
 *
 * 线程模型：模式表构造后只读（并发 redact 安全——std::regex const 方法线
 * 程安全）；策略经互斥保护 shared_ptr 快照；统计为原子累计。全部公共入口
 * noexcept——任何异常（含 applyRules 注入）捕获后走降级链。
 */

#include <sdurws/ird/diagnostics/Redaction.hpp>

#include <array>
#include <atomic>
#include <cctype>
#include <functional>
#include <mutex>
#include <regex>
#include <utility>
#include <vector>

#include <sdurws/ird/core/Digest.hpp>  // core::ContentDigester/Digest256（P-DIAG-1/CR-02——Hash 唯一 SHA-256 路径）

namespace sdurws::ird::diagnostics {

namespace {

// ---------------------------------------------------------------------
// 常量词表（§7.7 行原文；安全契约——只增不减，§9.5 稳定性行）
// ---------------------------------------------------------------------

/// 用户身份环境变量集（§7.7 用户名行"%USERPROFILE% 等"的展开——大小写不
/// 敏感比对；命中即 [USER]。TEMP/TMP 值通常内嵌用户目录，一并归入）。
constexpr std::array<std::string_view, 9> kUserEnvNames{
    "USERPROFILE", "HOME", "HOMEPATH", "HOMEDRIVE", "USERNAME",
    "APPDATA", "LOCALAPPDATA", "TEMP", "TMP"};

/// 产品自用环境变量前缀（§7.7 环境变量行"键名白名单 IRD_*"——保留不替换）。
constexpr std::string_view kProductEnvPrefix = "IRD_";

/// User 档截断标注（与日志管线 4 KiB 截断同一呈现约定——§7.2）。
constexpr std::string_view kTruncMarker = "[trunc]";

/// 是否用户身份环境变量名（入参须已大写规范化）。
bool isUserEnvName(const std::string& upperName)
{
    for (const std::string_view n : kUserEnvNames) {
        if (upperName == n) { return true; }
    }
    return false;
}

/// 规则⑥令牌形态的多样性判据：含字母且含数字且非纯十六进制（实现口径 2——
/// 纯十六进制是摘要形态，字母数字混合才是凭证/会话 token 的形态特征）。
bool looksLikeCredentialToken(const std::string& run)
{
    bool hasAlpha = false;
    bool hasDigit = false;
    bool allHex = true;
    for (const char ch : run) {
        const unsigned char u = static_cast<unsigned char>(ch);
        if (std::isalpha(u) != 0) { hasAlpha = true; }
        if (std::isdigit(u) != 0) { hasDigit = true; }
        if (std::isxdigit(u) == 0) { allHex = false; }
    }
    return hasAlpha && hasDigit && !allHex;
}

/// SHA-256 前 8 字节的 16 位小写 hex（P-DIAG-1/CR-02：摘要只经 core
/// ContentDigester；hex 编码是呈现不是第二哈希路径）。
std::string sha256Hex16(const std::string& bytes)
{
    core::ContentDigester digester;
    digester.update(bytes.data(), bytes.size());
    const core::Digest256 digest = digester.finalize();
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(16);
    for (std::size_t i = 0; i < 8; ++i) {
        out += kHex[digest[i] >> 4];
        out += kHex[digest[i] & 0x0F];
    }
    return out;
}

/// UTF-8 边界安全截断（与 Logging.cpp truncateForLog 同约定：超限回退尾部
/// 续字节，切点前是完整字符，再补 "[trunc]" 标注——maxBytes=0 表示不截断）。
std::string truncateUtf8(const std::string& text, std::size_t maxBytes)
{
    if (maxBytes == 0 || text.size() <= maxBytes) {
        return text;
    }
    std::size_t cut = maxBytes;
    // 回退尾部的 UTF-8 续字节（0b10xxxxxx），保证切点前是完整字符。
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) { --cut; }
    return text.substr(0, cut) + std::string{kTruncMarker};
}

}  // namespace

// =====================================================================
// Impl——模式表＋策略快照＋统计＋降级链
// =====================================================================

struct RedactionService::Impl {
    /**
     * @brief 构造（编译全部模式表——模式为产品常量字面量；正则语法错误属
     *        产品缺陷，构造期抛出即装配 fail-fast，不带病运行）。
     *
     * 裸串统一用 R"re(…)re" 定界：模式含 " 与 ) 字符，默认定界会被内容中
     * 的 )" 提前终结（凭据模式的值段含引号——必须自定义定界）。
     */
    explicit Impl(const RedactionPolicy& initialPolicy, IDevLogSink* sink)
        : policy(std::make_shared<const RedactionPolicy>(initialPolicy))
        , failureSink(sink)
        // ①凭据键值（大小写不敏感）：键名∈安全清单，后随 :/=，值段＝带引号
        //   串（含转义）或无空白段。分组：1=键名（结构，保留）2=分隔符
        //   （保留）——值整体消费不捕获。
        , reCredentialKv{
              R"re(\b(password|passwd|pwd|secret|token|apikey|api[_-]key|access[_-]key|private[_-]key|auth[_-]token|authorization|credential|credentials)\b([ \t]*[:=][ \t]*)(?:"(?:[^"\\]|\\.)*"|'(?:[^'\\]|\\.)*'|[^\s,;&|)]+))re",
              std::regex::icase}
        // ②认证方案凭证：Bearer/Basic 后随 ≥8 字符凭证段（方案与空白保留
        //   ——方案名是结构；凭证段整体替换）。**不收 digest/ntlm/negotiate**
        //   ："digest" 与摘要语义同词（"sha256 digest <hex>"是开发诊断常态
        //   文本），收编即系统性误伤（R-7）——§14.4 v0.9 口径登记。
        //   分组：1=方案 2=空白。
        , reBearerScheme{
              R"re(\b(bearer|basic)([ \t]+)([A-Za-z0-9._+/=~-]{8,}))re",
              std::regex::icase}
        // ③环境变量引用：%VAR%（Windows，大小写不敏感）与 ${VAR}（POSIX）。
        , reEnvWin{R"re(%([A-Za-z_][A-Za-z0-9_]{0,63})%)re", std::regex::icase}
        , reEnvPosix{R"re(\$\{([A-Za-z_][A-Za-z0-9_]{0,63})\})re"}
        // ④用户名路径段：<盘>:\Users\<名>（大小写不敏感）与 /home/<名>
        //   （POSIX 行的前导边界字符由分组 1 承载——正则方言约束，口径 8）。
        , reUserWin{
              R"re(\b([A-Za-z]):[\\/]+users[\\/]+([^\\/<>"|?*\r\n\t]*))re",
              std::regex::icase}
        , reUserPosix{R"re((^|[^A-Za-z0-9])/home/([A-Za-z0-9._-]+))re"}
        // ⑤路径候选：盘符绝对/UNC/POSIX 绝对（POSIX 行前导边界字符由分组 1
        //   承载，排除 URL"://"、相对"../"与宏展开残留——口径 5/8；分组 2＝
        //   路径整体）。
        , rePathDrive{R"re(([A-Za-z]):[\\/](?:[^\\/<>"|?*\r\n\t]+[\\/])*[^\\/<>"|?*\r\n\t]*)re"}
        , rePathUnc{R"re(\\\\[^\\/<>"|?*\r\n\t]+(?:[\\/][^\\/<>"|?*\r\n\t]+)+)re"}
        , rePathPosix{
              R"re((^|[^A-Za-z0-9.:/%_$~-])(/(?:[^\\/<>"|?*\r\n\t]+/)+[^\\/<>"|?*\r\n\t]*))re"}
        // ⑥令牌形态：≥40 字符凭证字符集运行（'.' 入字符集——JWT 三段式
        //   "hdr.pld.sig"须整段捕获，分段后各段 <40 会漏；前导边界字符由
        //   分组 1 承载、右边界 lookahead——口径 8；分组 2＝候选串）。
        , reTokenShape{
              R"re((^|[^A-Za-z0-9._+/=-])([A-Za-z0-9._+/=-]{40,})(?![A-Za-z0-9._+/=-]))re"}
        // ⑦User 档内部模式（与 Logging.cpp 呈现过滤同形——NFR-REL-05/UX-02；
        //   十六进制行的前导边界字符由分组 1 承载、右边界 lookahead——口径 8）。
        , reInternalAddr{R"re(0[xX][0-9A-Fa-f]{4,})re"}
        , reInternalHash{R"re((^|[^0-9A-Fa-f])([0-9A-Fa-f]{32,})(?![0-9A-Fa-f]))re"}
        , reInternalFrame{R"re(\bat[ \t]+[^ \t\r\n]+:[0-9]+\b)re"} {}

    // ---- 模式表（构造后只读——并发 redact 的安全性基础）----
    std::regex reCredentialKv;   ///< ①凭据键值
    std::regex reBearerScheme;   ///< ②认证方案凭证
    std::regex reEnvWin;         ///< ③ %VAR%
    std::regex reEnvPosix;       ///< ③ ${VAR}
    std::regex reUserWin;        ///< ④ <盘>:\Users\<名>
    std::regex reUserPosix;      ///< ④ /home/<名>
    std::regex rePathDrive;      ///< ⑤ 盘符路径
    std::regex rePathUnc;        ///< ⑤ UNC
    std::regex rePathPosix;      ///< ⑤ POSIX 绝对
    std::regex reTokenShape;     ///< ⑥ 令牌形态
    std::regex reInternalAddr;   ///< ⑦ 0x 地址（User 档）
    std::regex reInternalHash;   ///< ⑦ ≥32 位十六进制（User 档）
    std::regex reInternalFrame;  ///< ⑦ 栈帧（User 档）

    // ---- 策略（写时拷贝切换；读侧快照）----
    mutable std::mutex policyMutex;                ///< 护策略指针切换
    std::shared_ptr<const RedactionPolicy> policy; ///< 当前策略（不可变快照）

    /// 降级开发诊断路由（非拥有——可为空＝静默降级，见头文件构造注释）。
    IDevLogSink* failureSink;

    // ---- 统计（R-7 可观测面——原子累计）----
    std::atomic<std::uint64_t> credentialHits{0};
    std::atomic<std::uint64_t> tokenHits{0};
    std::atomic<std::uint64_t> envHits{0};
    std::atomic<std::uint64_t> userHits{0};
    std::atomic<std::uint64_t> pathHits{0};
    std::atomic<std::uint64_t> internalHits{0};
    std::atomic<std::uint64_t> failures{0};

    /// 当前策略快照（互斥下拷贝 shared_ptr——写时拷贝切换的读侧）。
    std::shared_ptr<const RedactionPolicy> policySnapshot() const
    {
        std::lock_guard<std::mutex> lock(policyMutex);
        return policy;
    }

    /// 累计单次调用的逐类计数（relaxed 序——只作观测不作同步）。
    void accumulate(const RedactionCounters& c)
    {
        credentialHits.fetch_add(c.credential, std::memory_order_relaxed);
        tokenHits.fetch_add(c.token, std::memory_order_relaxed);
        envHits.fetch_add(c.env, std::memory_order_relaxed);
        userHits.fetch_add(c.user, std::memory_order_relaxed);
        pathHits.fetch_add(c.path, std::memory_order_relaxed);
        internalHits.fetch_add(c.internal, std::memory_order_relaxed);
    }

    /**
     * @brief 降级链（§7.3②/DT-SEC-3）：计数＋一次开发诊断＋整条字面量。
     *
     * thread_local 防护＝失败诊断发射链路内不再二次进入（不递归诊断——
     * 与 §7.6 崩溃写失败同款保守方向）；诊断消息为常量文本，绝不回显原文。
     */
    std::string degrade()
    {
        failures.fetch_add(1, std::memory_order_relaxed);
        static thread_local bool inFailureEmit = false;
        if (!inFailureEmit && failureSink != nullptr) {
            inFailureEmit = true;
            try {
                failureSink->logDev(
                    kRedactionInternalChannel,
                    "DIAG-REDACTION-FAILED 脱敏器自身失败：整条已降级为 "
                    "[REDACTED:redaction-failed]，原文绝不放行（§7.3②/DT-SEC-3）");
            } catch (...) {
                // 路由自身失败一并吞掉——降级链绝不抛出、绝不递归。
            }
            inFailureEmit = false;
        }
        return std::string{kRedactionFailedToken};
    }

    // -----------------------------------------------------------------
    // 规则链（每遍 sregex_iterator 手工重建——替换函数携带逐类计数）
    // -----------------------------------------------------------------

    /// 通用单遍替换：命中前的原文段原样保留，命中段经 make 生成替换文本。
    /// 注意迭代器从 text 全序列起步——m.position()/迭代器区间均相对全序列，
    /// 断点只由 m[0].first/second 推进（不引入二次偏移）。std::string 迭代
    /// 器在 MSVC 调试态是查检类不是 const char*——cregex_iterator 以 data()
    /// 裸指针区间构造（string 连续性保证）。
    static std::string replaceEach(
        const std::string& text, const std::regex& re,
        const std::function<std::string(const std::cmatch&)>& make)
    {
        const char* first = text.data();
        const char* lastByte = first + text.size();
        std::string out;
        out.reserve(text.size());
        const char* last = first;
        for (std::cregex_iterator it(first, lastByte, re), end; it != end; ++it) {
            const std::cmatch& m = *it;
            out.append(last, m[0].first);  // 命中前的原文段
            out.append(make(m));           // 替换文本
            last = m[0].second;
        }
        out.append(last, lastByte);
        return out;
    }

    /// ①凭据键值：保留键名与分隔符，值段→[REDACTED:credential:n]。
    void replaceCredentialKv(std::string& text, RedactionCounters& c) const
    {
        text = replaceEach(text, reCredentialKv,
                           [&c](const std::cmatch& m) {
                               ++c.credential;
                               std::string out = m[1].str();  // 键名（结构，保留）
                               out += m[2].str();             // 分隔符（保留）
                               out += "[REDACTED:credential:"
                                    + std::to_string(c.credential) + "]";
                               return out;
                           });
    }

    /// ②认证方案凭证：方案＋空白保留，凭证段→[REDACTED:token:n]。
    void replaceBearerScheme(std::string& text, RedactionCounters& c) const
    {
        text = replaceEach(text, reBearerScheme,
                           [&c](const std::cmatch& m) {
                               ++c.token;
                               return m[1].str() + m[2].str() + "[REDACTED:token:"
                                    + std::to_string(c.token) + "]";
                           });
    }

    /// 环境变量名归类（口径：IRD_* 白名单保留原引用（含定界符——整匹配
    /// 回填）；用户身份集→[USER]；其余→[REDACTED:env:n]"整体不记录"）。
    std::string classifyEnv(const std::string& fullMatch, const std::string& rawName,
                            RedactionCounters& c) const
    {
        std::string upper;
        upper.reserve(rawName.size());
        for (const char ch : rawName) {
            upper += static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        }
        if (upper.rfind(kProductEnvPrefix, 0) == 0) {
            return fullMatch;  // 产品自用白名单——原样保留（含 %…%/${…} 定界）
        }
        if (isUserEnvName(upper)) {
            ++c.user;
            return "[USER]";
        }
        ++c.env;
        return "[REDACTED:env:" + std::to_string(c.env) + "]";
    }

    /// ③环境变量引用（%VAR% 与 ${VAR} 两遍）。
    void replaceEnvRefs(std::string& text, RedactionCounters& c) const
    {
        text = replaceEach(text, reEnvWin,
                           [this, &c](const std::cmatch& m) {
                               return classifyEnv(m[0].str(), m[1].str(), c);
                           });
        text = replaceEach(text, reEnvPosix,
                           [this, &c](const std::cmatch& m) {
                               return classifyEnv(m[0].str(), m[1].str(), c);
                           });
    }

    /// ④用户名路径段：<盘>:\Users\<名>→[USER]、/home/<名>→/home/[USER]
    /// （无条件——先于路径策略，Keep 下用户名也不泄露；POSIX 行回填前导
    /// 边界字符——口径 8）。
    void replaceUserSegments(std::string& text, RedactionCounters& c) const
    {
        text = replaceEach(text, reUserWin,
                           [&c](const std::cmatch&) {
                               ++c.user;
                               return std::string("[USER]");
                           });
        text = replaceEach(text, reUserPosix,
                           [&c](const std::cmatch& m) {
                               ++c.user;
                               return m[1].str() + "/home/[USER]";
                           });
    }

    /**
     * @brief 路径候选按策略处理（⑤）。
     *
     * 根结构判定（口径 4/5）：盘符（X:\ 或 X:/）、UNC（\\\\host\\share）、
     * POSIX 绝对（/）三类可判定根形态才按策略处理；其余（相对路径/普通词）
     * 原样保留——不猜形态即不误伤（R-7）。
     */
    std::string applyPathPolicy(const std::string& candidate, PathPolicy pp,
                                RedactionCounters& c) const
    {
        ++c.path;
        switch (pp) {
        case PathPolicy::Keep:
            return candidate;

        case PathPolicy::Hash:
            // SHA-256 前 8 字节 hex——同路径同 token 可关联、不可逆推（口径 3）。
            return "[PATH-" + sha256Hex16(candidate) + "]";

        case PathPolicy::Strip: {
            // 仅文件名＝最后一个非空分隔段；无分隔段（非路径词）原样保留。
            const std::size_t pos = candidate.find_last_of("/\\");
            if (pos == std::string::npos) { return candidate; }
            const std::string last = candidate.substr(pos + 1);
            return last.empty() ? candidate : last;
        }

        case PathPolicy::RootOnly:
            return rootOnlyForm(candidate);
        }
        // 枚举冻结（§9.5）——switch 全枚举后不可达；防御性原样返回。
        return candidate;
    }

    /// RootOnly 形态：根＋一级目录＋"…"＋文件名（口径 4——退化形态不伪造"…"）。
    std::string rootOnlyForm(const std::string& candidate) const
    {
        std::string prefix;  ///< 保留的根（盘符/UNC 前两段/POSIX 根）
        std::string rest;    ///< 根之后的段区
        char sep = '\\';     ///< 重建分隔符（按 rest 内主导分隔风格）

        if (candidate.size() >= 3 && std::isalpha(static_cast<unsigned char>(candidate[0])) != 0
            && candidate[1] == ':' && (candidate[2] == '\\' || candidate[2] == '/')) {
            // 盘符路径：保留 "X:\"（含原始分隔风格）。
            prefix = candidate.substr(0, 3);
            rest = candidate.substr(3);
            sep = detectSep(rest);
        } else if (candidate.size() >= 2 && candidate[0] == '\\' && candidate[1] == '\\') {
            // UNC：保留 "\\host\share"（两段——服务器与共享名即"根"）。
            const std::size_t sep1 = candidate.find('\\', 2);
            if (sep1 == std::string::npos) { return candidate; }
            const std::size_t sep2 = candidate.find('\\', sep1 + 1);
            if (sep2 == std::string::npos) { return candidate; }
            prefix = candidate.substr(0, sep2);  // 不含尾分隔符
            rest = candidate.substr(sep2 + 1);
            sep = detectSep(rest);
        } else if (!candidate.empty() && candidate[0] == '/') {
            // POSIX 绝对：保留 "/"。
            prefix = "/";
            rest = candidate.substr(1);
            sep = '/';
        } else {
            // 根形态不可判定（相对路径等）——不猜，原样保留（口径 5）。
            return candidate;
        }

        // 拆非空段（保留尾分隔符信息——有尾分隔符＝末段是目录不是文件名）。
        std::vector<std::string> segs;
        std::string cur;
        for (const char ch : rest) {
            if (ch == '\\' || ch == '/') {
                if (!cur.empty()) { segs.push_back(cur); }
                cur.clear();
            } else {
                cur += ch;
            }
        }
        const bool trailingSep = !rest.empty() && (rest.back() == '\\' || rest.back() == '/');
        if (!cur.empty()) { segs.push_back(cur); }

        if (segs.empty()) {
            return candidate;  // 根之外无内容（"D:\"/"\\h\s"/"/"）——原样保留
        }
        if (segs.size() == 1 && !trailingSep) {
            // 仅根级文件名（D:\m.stl）——无目录结构可隐藏（口径 4）。
            return candidate;
        }

        // 根＋一级目录＋"…"＋文件名（§7.7 文本口径；被隐藏的中间段连同其
        // 原分隔风格一并抹去——重建统一用主导分隔符；盘符根自带分隔符，UNC
        // 根补齐）。
        std::string out = prefix;
        if (out.back() != '\\' && out.back() != '/') {
            out += sep;
        }
        out += segs.front() + sep + "…";
        if (!trailingSep && segs.size() >= 2) {
            out += sep + segs.back();  // 文件名＝最后一个非空段
        }
        return out;
    }

    /// rest 内主导分隔风格（只有 / 用 /，否则统一 \——口径见 rootOnlyForm）。
    static char detectSep(const std::string& rest)
    {
        return rest.find('/') != std::string::npos && rest.find('\\') == std::string::npos
                   ? '/'
                   : '\\';
    }

    /// ⑤路径候选三形态扫描（每命中独立按策略处理；POSIX 行回填前导边界
    /// 字符——口径 8）。
    void replacePaths(std::string& text, const RedactionPolicy& pol, RedactionCounters& c) const
    {
        text = replaceEach(text, rePathDrive,
                           [this, &pol, &c](const std::cmatch& m) {
                               return applyPathPolicy(m.str(), pol.pathPolicy, c);
                           });
        text = replaceEach(text, rePathUnc,
                           [this, &pol, &c](const std::cmatch& m) {
                               return applyPathPolicy(m.str(), pol.pathPolicy, c);
                           });
        text = replaceEach(text, rePathPosix,
                           [this, &pol, &c](const std::cmatch& m) {
                               // 分组 1＝前导边界字符（串首为空），分组 2＝路径。
                               return m[1].str()
                                    + applyPathPolicy(m[2].str(), pol.pathPolicy, c);
                           });
    }

    /// ⑥令牌形态（多样性判据——口径 2；前导边界字符回填——口径 8）。
    void replaceTokenShapes(std::string& text, RedactionCounters& c) const
    {
        text = replaceEach(text, reTokenShape,
                           [&c](const std::cmatch& m) {
                               if (!looksLikeCredentialToken(m[2].str())) {
                                   return m[0].str();  // 摘要/数字形态——保留（R-7 防误伤）
                               }
                               ++c.token;
                               return m[1].str() + "[REDACTED:token:"
                                    + std::to_string(c.token) + "]";
                           });
    }

    /// ⑦User 档内部模式（与 Logging.cpp 呈现过滤同形——NFR-REL-05/UX-02；
    /// 十六进制行回填前导边界字符——口径 8）。
    void replaceInternalPatterns(std::string& text, RedactionCounters& c) const
    {
        text = replaceEach(text, reInternalAddr,
                           [&c](const std::cmatch&) {
                               ++c.internal;
                               return std::string("[ADDR]");
                           });
        text = replaceEach(text, reInternalHash,
                           [&c](const std::cmatch& m) {
                               ++c.internal;
                               return m[1].str() + std::string("[HASH]");
                           });
        text = replaceEach(text, reInternalFrame,
                           [&c](const std::cmatch&) {
                               ++c.internal;
                               return std::string("at [FRAME]");
                           });
    }

    /// 规则主链（RedactionService::applyRules 的默认实现——执行序见口径 1）。
    void runRules(std::string& text, LogTier tier, RedactionCounters& c) const
    {
        c = RedactionCounters{};
        const std::shared_ptr<const RedactionPolicy> pol = policySnapshot();
        replaceCredentialKv(text, c);   // ①
        replaceBearerScheme(text, c);   // ②
        replaceEnvRefs(text, c);        // ③
        replaceUserSegments(text, c);   // ④
        replacePaths(text, *pol, c);    // ⑤
        replaceTokenShapes(text, c);    // ⑥
        if (tier == LogTier::User) {
            replaceInternalPatterns(text, c);  // ⑦（仅 User 档——Tier 语义见头文件）
        }
    }

    /// 单路径处理（redactPath——规则④先行＋单候选策略；见头文件签名注释）。
    std::string runSinglePath(std::string& text, RedactionCounters& c) const
    {
        c = RedactionCounters{};
        replaceUserSegments(text, c);  // 用户名无条件脱敏（§7.7 用户名行）
        const std::shared_ptr<const RedactionPolicy> pol = policySnapshot();
        return applyPathPolicy(text, pol->pathPolicy, c);
    }
};

// =====================================================================
// RedactionService——公共入口（全部 noexcept＋降级链）
// =====================================================================

RedactionService::RedactionService(const RedactionPolicy& policy, IDevLogSink* failureSink)
    : m_impl(std::make_unique<Impl>(policy, failureSink))
{
}

RedactionService::~RedactionService() = default;

std::string RedactionService::redact(std::string_view raw, LogTier tier) const noexcept
{
    try {
        // 纯函数主链：拷贝入参→规则链就地改写→累计观测计数。
        std::string text(raw);
        RedactionCounters counters{};
        applyRules(text, tier, counters);
        m_impl->accumulate(counters);
        return text;
    } catch (...) {
        // 降级铁律（§7.3②）：任何内部失败——整条字面量＋开发诊断，绝不放行
        // 原文、绝不抛出。applyRules 为虚函数：测试子类抛出即复现本路径
        // （DT-SEC-3 确定性注入）。
        return m_impl->degrade();
    }
}

std::string RedactionService::redactPath(std::string_view rawPath) const noexcept
{
    try {
        std::string text(rawPath);
        RedactionCounters counters{};
        const std::string out = m_impl->runSinglePath(text, counters);
        m_impl->accumulate(counters);
        return out;
    } catch (...) {
        return m_impl->degrade();  // 同 redact 降级语义（§9.5"输出必为脱敏后文本"）
    }
}

std::string RedactionService::safeSummary(std::string_view raw, std::size_t maxBytes) const noexcept
{
    try {
        // 双保险入口：User 档全量脱敏（含内部模式——报告面不带栈/地址/哈希）
        // ＋UTF-8 边界安全截断（§9.5"安全摘要导出"）。
        std::string text(raw);
        RedactionCounters counters{};
        applyRules(text, LogTier::User, counters);
        m_impl->accumulate(counters);
        return truncateUtf8(text, maxBytes);
    } catch (...) {
        return m_impl->degrade();
    }
}

void RedactionService::setPolicy(const RedactionPolicy& policy)
{
    // 写时拷贝切换（§9.5）：新调用生效、已写行不回溯——读侧持旧快照完成
    // 当次调用，切换互斥只护指针交换。
    std::lock_guard<std::mutex> lock(m_impl->policyMutex);
    m_impl->policy = std::make_shared<const RedactionPolicy>(policy);
}

RedactionPolicy RedactionService::policy() const
{
    return *m_impl->policySnapshot();
}

RedactionStats RedactionService::stats() const
{
    RedactionStats s;
    s.credentialHits = m_impl->credentialHits.load(std::memory_order_relaxed);
    s.tokenHits = m_impl->tokenHits.load(std::memory_order_relaxed);
    s.envHits = m_impl->envHits.load(std::memory_order_relaxed);
    s.userHits = m_impl->userHits.load(std::memory_order_relaxed);
    s.pathHits = m_impl->pathHits.load(std::memory_order_relaxed);
    s.internalHits = m_impl->internalHits.load(std::memory_order_relaxed);
    s.failures = m_impl->failures.load(std::memory_order_relaxed);
    return s;
}

void RedactionService::applyRules(std::string& text, LogTier tier, RedactionCounters& counters) const
{
    // 默认规则链（产品路径）；测试子类覆写抛出即触发 redact 降级（DT-SEC-3）。
    m_impl->runRules(text, tier, counters);
}

}  // namespace sdurws::ird::diagnostics
