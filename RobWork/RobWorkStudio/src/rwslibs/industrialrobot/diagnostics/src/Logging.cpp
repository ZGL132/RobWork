/**
 * @file   Logging.cpp
 * @brief  两级日志管线实现——§7.3 五步管线（格式化/脱敏过滤/级别过滤/节流
 *         采样/异步写入＋flush 窗口）、§7.4 节流与采样、轮转、§7.5 worker
 *         批次重放合并（乱序缓冲/断裂补条）、ILogFileOps 默认文件实现。
 *
 * 设计依据：
 *   - units/diagnostics.md §7.1～§7.6、§7.8、§9.6（ILogger 契约表——任意线程
 *     调用、内部日志线程串行化、关闭 flush 有界）、§10 DT-LOG-1~5 行、
 *     §11 DIAG-T07 行（Logging.*＝管线/节流采样/轮转/WorkerLogBatch 重放；
 *     ILogFileOps 接缝）
 *   - 需求 NFR-REL-05（两级日志；用户级无调用栈/内部哈希）、UX-02（用户级
 *     无哈希/内部名）、§7.6（崩溃前保留窗口——Error 即时 flush）、PM-03
 *     （关闭不因日志永久等待）
 *   - 任务契约 tasks/foundation/DIAG-T07.json acceptance 1~3（DT-LOG-1~5、
 *     P-DIAG-7 工程默认、P-EX-8 只定载荷语义）
 *
 * 实现口径（§7/§9.6 未定判据，按 DTB §5.4 登记于单元卡 §14.4 变更记录）：
 *   1. **序号＝日志线程写入序**：全局单调序号在日志线程渲染行时分配（单一
 *      串行流，无需加锁）——文件内 seq 严格递增＝全局稳定序（§7.4"跨线程按
 *      日志线程入队序"的串行化承载：队列 FIFO 保证处理序＝入队序）。
 *   2. **镜像写入**：Tier-U 行同时写入 Tier-D 文件（Tier-U ⊆ Tier-D——§7.1
 *      "Tier-U 行可由 Tier-D 行重建"；DT-LOG-1"Tier-D 全量"）；Tier-D 文件
 *      保留原文，Tier-U 文件使用模式过滤后的呈现（②过滤只作用于 Tier-U 侧）。
 *   3. **Tier-U 内部模式过滤**（②"Tier-U 额外过滤栈/哈希/内部 token 模式"
 *      的本管线内建承载）：0x 前缀十六进制地址→[ADDR]、≥32 位连续十六进制
 *      （内容哈希/摘要形态）→[HASH]、栈帧形态 " at 路径:行号"→" at [FRAME]"。
 *      模式清单为工程默认；NFR-SEC-07 全量脱敏（凭据/路径/环境变量/资源
 *      内容）归 DIAG-T08 IRedactionService，本任务预留注入点＝renderRecord
 *      的消息加工步骤（登记于单元卡变更记录——DIAG-T08 按卡落地后注入）。
 *   4. **节流窗判定用记录时间戳**（注入 IClock—— ManualClock 确定性可测）；
 *      窗关闭的汇总行在下一同键记录到达时补写或关闭排空时补写（§7.4"每
 *      10 s 汇总一条（含 occurrences）"）。采样"末条"以尾条槽承载：只保
 *      最近一条被采掉的行，在关闭排空时写出（标注 "[sampled-tail]"——进行
 *      中的流没有"末条"可定义，§7.4 采样承诺在关闭路径兑现）。
 *   5. **断裂判定时机**：乱序批次一律先缓冲（缺口可能仍在途）；仅收尾批
 *      （final=true）或关闭排空时对 [期望序号, 收尾序号) 的缺口补条——补条
 *      为 EX-CHANNEL-PROTOCOL-ERROR Dev 行（channel "diag/log-merge"）。
 *      陈旧/重复批（seq<期望）丢弃＋计数行（幂等重放）。
 *   6. **内部行旁路采样**：管线自产行（断裂补条/写失败/队列溢出汇总/节流
 *      汇总/采样尾条）均为 Warning 级或带旁路标记——采样只触及 Debug/Trace
 *      （§7.4），自产行不再被自身机制吞噬；写失败行保留节流（按 code+channel
 *      聚合，故障风暴不刷屏）。
 *
 * 线程模型：log/logDev/replayWorkerBatch/flush/configure 任意线程；互斥量只
 * 护队列与配置快照；节流/采样/轮转/重放/序号状态仅日志线程触碰（串行化即
 * 无锁）。有界等待（flush/deadline、析构排空）用 steady_clock 墙钟——安全
 * 属性不依赖可注入时钟（测试时钟冻结不得造成永久等待，PM-03）。
 */

#include <sdurws/ird/diagnostics/Logging.hpp>

#include <sdurws/ird/diagnostics/Redaction.hpp>  // IRedactionService（§7.3② 步骤②接线——DIAG-T08）

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

#include <sdurws/ird/diagnostics/Errors.hpp>

namespace sdurws::ird::diagnostics {

// =====================================================================
// 词表与基础值语义（头文件声明的实现）
// =====================================================================

std::string_view logTierToken(LogTier tier) noexcept
{
    switch (tier) {
    case LogTier::User: return "user";  ///< Tier-U 用户级（§7.1）
    case LogTier::Dev: return "dev";    ///< Tier-D 开发级（§7.1）
    }
    return "user";  // 不可达（全枚举 switch——新值未登记时编译器告警）
}

std::string_view logLevelToken(LogLevel level) noexcept
{
    switch (level) {
    case LogLevel::Error: return "ERROR";
    case LogLevel::Warning: return "WARN";
    case LogLevel::Info: return "INFO";
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Trace: return "TRACE";
    }
    return "INFO";  // 不可达
}

std::uint8_t logLevelRank(LogLevel level) noexcept
{
    // 秩＝重要性序（Error 最重为 0）：Tier-D 级别过滤与节流比较的纯函数承载。
    return static_cast<std::uint8_t>(level);  // 枚举序即声明序（Error→Trace）
}

bool CorrelationIds::any() const noexcept
{
    // §7.2："至少一个非空才可跨行关联"——全空＝纯开发输出行的判定原语。
    return entryId.has_value() || findingId.has_value() || taskId.has_value()
        || runId.has_value() || attemptId.has_value() || revisionId.has_value();
}

bool CorrelationIds::operator==(const CorrelationIds& o) const
{
    return entryId == o.entryId && findingId == o.findingId && taskId == o.taskId
        && runId == o.runId && attemptId == o.attemptId && revisionId == o.revisionId;
}

bool LogRecord::operator==(const LogRecord& o) const
{
    return timestampUtc == o.timestampUtc && tier == o.tier && level == o.level
        && channel == o.channel && correlationIds == o.correlationIds && code == o.code
        && message == o.message && threadTag == o.threadTag && workerId == o.workerId;
}

bool WorkerLogBatch::operator==(const WorkerLogBatch& o) const
{
    return workerId == o.workerId && task == o.task && seq == o.seq && final == o.final
        && records == o.records;
}

namespace {

// =====================================================================
// 内部常量（管线自产行的 code/channel 承载——稳定码值权威＝StableCodeRegistry
// §4.6 内置表，此处只引用已注册码原文，不新造码）
// =====================================================================

/// 写失败自省码（§4.6 DIAG 行——Dev 级；DT-LOG-3 观测对象）。
constexpr const char* kCodeLogWriteFailed = "DIAG-LOG-WRITE-FAILED";
/// 通道协议断裂码（§4.6 EX 行——Dev 级；§7.5 断裂补条）。
constexpr const char* kCodeChannelProtocol = "EX-CHANNEL-PROTOCOL-ERROR";
/// 管线自产行通道（日志设施自身——词表随单元登记）。
constexpr const char* kChannelLogging = "diag/logging";
/// 重放合并通道（§7.5 断裂补条行）。
constexpr const char* kChannelMerge = "diag/log-merge";

/**
 * @brief 是否管线/脱敏设施的内部自省通道（§7.3② 步骤②的旁路清单——
 *        DIAG-T08 接线口径，登记于单元卡 §14.4 v0.9）。
 *
 * 为什么旁路：这三类通道的行由管线/脱敏设施自产，内容为常量文本＋计数
 * （绝不携带调用方原文——写失败行携带的文件面 token "user"/"dev" 是枚举
 * 字面量而非路径）；若再过脱敏，自省行自身的脱敏失败会经 failureSink 回环
 * 入队（脱敏失败→发失败行→失败行再脱敏再失败……）。自省行不走脱敏即
 * 切断该环（与 v0.8 口径⑥"内部行旁路采样/节流"同一设计方向）。DT-LOG-1~3
 * 既有用例断言自省行文本逐字（含 "file=user" 面）——旁路同时保持 v0.8 行
 * 为零回归。
 */
bool isInternalDiagChannel(const std::string& channel) noexcept
{
    return channel == kChannelLogging                     // 日志设施自省行
        || channel == kChannelMerge                       // 重放断裂补条行
        || channel.compare(kRedactionInternalChannel) == 0;  // 脱敏降级行（Redaction.hpp）
}

/// 截断标注（§7.2"超出截断并标注"）。
constexpr const char* kTruncMark = "[trunc]";

bool isHexChar(char c) noexcept
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

bool isDigitChar(char c) noexcept { return c >= '0' && c <= '9'; }

// =====================================================================
// 纯文本加工原语（管线①格式化与②Tier-U 过滤——确定性、无 locale 依赖）
// =====================================================================

/**
 * @brief 消息定长截断（§7.2：≤4 KiB，超出截断并标注）。
 *
 * UTF-8 边界安全：超限时回退尾部的续字节（0b10xxxxxx），保证切点前是完整
 * 字符序列（避免日志文件出现半个码位——观测面编码稳定）。截断本体不改写
 * 语义（字节前缀保留）。
 */
std::string truncateForLog(const std::string& msg)
{
    if (msg.size() <= kLogMessageMaxBytes) { return msg; }
    std::size_t cut = kLogMessageMaxBytes;
    // 回退续字节：切点若落在多字节序列内部（续字节），向前回退至 lead 字节前。
    while (cut > 0 && (static_cast<unsigned char>(msg[cut]) & 0xC0) == 0x80) { --cut; }
    std::string out = msg.substr(0, cut);
    out += kTruncMark;
    return out;
}

/**
 * @brief 行内转义（DT-LOG-2"行完整"——日志行单行化）。
 *
 * 反斜杠与换行/回退/制表四类字符转义为 "\\n" 等可见形态；其余字节原样
 * 透传（UTF-8 内容不动）。消息是行尾字段（msg= 之后到行尾），转义保证
 * 任何载荷不会产生物理换行、破坏逐行解析。
 */
std::string escapeLineMessage(std::string_view msg)
{
    std::string out;
    out.reserve(msg.size());
    for (const char c : msg) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out += c; break;
        }
    }
    return out;
}

/**
 * @brief Tier-U 内部模式过滤（NFR-REL-05/UX-02：用户级无调用栈/内部哈希）。
 *
 * 三类模式（实现口径 3，登记于单元卡 §14.4）：
 *   1. 十六进制内存地址："0x"/"0X" 前缀＋≥4 位十六进制 → "[ADDR]"
 *      （调用栈/内存地址的机器形态）；
 *   2. 内容哈希：≥32 位连续十六进制（MD5/SHA-1/SHA-256 摘要十六进制形态，
 *      含 64 位 SHA-256）→ "[HASH]"（内部内容哈希——UX-02 明令禁止）；
 *   3. 栈帧：" at <路径或符号>:<十进制行号>" → " at [FRAME]"
 *      （调用栈文本的通用形态，C++/Python 风格栈行均命中）。
 *
 * 替换保留占位符（不保留原文）——与 §7.3②"替换 [REDACTED:<kind>:n]（保留
 * 计数，不保留原文）"同向的保守口径。单趟扫描、优先级判定（地址优先于
 * 哈希，避免 "0x"+长十六进制被二次匹配）——纯函数，同输入同输出。
 */
std::string maskUserPatterns(std::string_view in)
{
    std::string out;
    out.reserve(in.size());
    const std::size_t n = in.size();
    std::size_t i = 0;
    while (i < n) {
        // 模式 1：十六进制内存地址（0x 前缀＋至少 4 位十六进制）。
        if (i + 2 < n && in[i] == '0' && (in[i + 1] == 'x' || in[i + 1] == 'X')
            && isHexChar(in[i + 2])) {
            std::size_t j = i + 2;
            while (j < n && isHexChar(in[j])) { ++j; }
            if (j - (i + 2) >= 4) {
                out += "[ADDR]";
                i = j;
                continue;
            }
        }
        // 模式 2：连续十六进制 ≥32 位＝内容哈希/摘要形态。
        if (isHexChar(in[i])) {
            std::size_t j = i;
            while (j < n && isHexChar(in[j])) { ++j; }
            if (j - i >= 32) {
                out += "[HASH]";
                i = j;
                continue;
            }
        }
        // 模式 3：栈帧 " at <非空格串>:<数字>"（行号后必须是行尾或空格——
        // 避免把普通 " at 10:30" 之外的时间类文本误吞）。
        if (in.compare(i, 4, " at ") == 0) {
            std::size_t k = i + 4;
            std::size_t colon = 0;
            bool hasColon = false;
            while (k < n && in[k] != ' ') {
                if (in[k] == ':') {
                    hasColon = true;
                    colon = k;
                }
                ++k;
            }
            if (hasColon && colon + 1 < n && isDigitChar(in[colon + 1])) {
                std::size_t d = colon + 1;
                while (d < n && isDigitChar(in[d])) { ++d; }
                if (d >= n || in[d] == ' ') {
                    out += " at [FRAME]";
                    i = d;
                    continue;
                }
            }
        }
        out += in[i];
        ++i;
    }
    return out;
}

/// 五元组规范串（日志 ids 字段承载形态：五段 toCanonical 以 '/' 相连——
/// 仅日志呈现，不是身份计算（身份归各所有者单元，PA-1））。
std::string taskCanonical(const core::TaskIdentity& t)
{
    return t.project.toCanonical() + "/" + t.branch.toCanonical() + "/"
         + t.revision.toCanonical() + "/" + t.run.toCanonical() + "/"
         + t.attempt.toCanonical();
}

/// 时间点 → Unix 纪元毫秒（日志行时间字段；UTC 计数，无时区语义）。
std::string epochMillis(std::chrono::system_clock::time_point tp)
{
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        tp.time_since_epoch())
                        .count();
    return std::to_string(ms);
}

/// 异常屏障：文件接缝允许抛异常（testkit FaultInterceptor 形态）——管线
/// 捕获消化为 false（DT-LOG-3"无异常逃逸"由管线保证）。
template <class Fn>
bool guardOpsBool(Fn&& fn) noexcept
{
    try {
        return fn();
    } catch (...) {
        return false;
    }
}

/// 同上（fileSize 轨：异常按 0 处理——轮转基线退化为"从零计"，只影响
/// 轮转时机不影响正确性）。
template <class Fn>
std::uint64_t guardOpsSize(Fn&& fn) noexcept
{
    try {
        return fn();
    } catch (...) {
        return 0;
    }
}

}  // namespace

// =====================================================================
// FileLogFileOps——产品默认文件实现（句柄表＋互斥）
// =====================================================================

struct FileLogFileOps::Impl {
    std::mutex mtx;  ///< 串行化：flush() 调用方与日志线程并发触碰（轮转移位互斥）

    /// 单行写入原语：开-写-关（"ab" 二进制模式——行节数与磁盘字节数一致，
    /// Windows 文本模式的 \n→\r\n 翻译会破坏轮转字节账）。
    ///
    /// 为什么不持常驻句柄：①诊断日志是低容量设施（节流/采样已约束行数），
    /// 每行开关的开销可忽略；②fclose 即冲刷——单行落盘，崩溃丢失窗口小于
    /// §7.6 的 T=2 s 承诺；③不占句柄即不阻塞外部读取/轮转的共享语义
    /// （Windows 句柄共享模式会挡住并发读者——运维 tail 场景必须可读）。
    bool appendOne(const std::filesystem::path& p, std::string_view line)
    {
        std::FILE* f = nullptr;
#ifdef _MSC_VER
        if (fopen_s(&f, p.string().c_str(), "ab") != 0) { f = nullptr; }
#else
        f = std::fopen(p.string().c_str(), "ab");
#endif
        if (f == nullptr) { return false; }
        const bool bodyOk = std::fwrite(line.data(), 1, line.size(), f) == line.size();
        const bool eolOk = std::fputc('\n', f) != EOF;
        // fclose 冲刷缓冲——写失败（磁盘满等）在此暴露；关闭失败视为失败行。
        const bool closeOk = std::fclose(f) == 0;
        return bodyOk && eolOk && closeOk;
    }
};

FileLogFileOps::FileLogFileOps()
    : m_impl{std::make_unique<Impl>()}
{
}

FileLogFileOps::~FileLogFileOps() = default;  // pimpl 完整定义于本翻译单元

bool FileLogFileOps::appendLine(const std::filesystem::path& file, std::string_view line)
{
    std::lock_guard<std::mutex> lock(m_impl->mtx);
    return m_impl->appendOne(file, line);
}

bool FileLogFileOps::flushFile(const std::filesystem::path& file)
{
    // 逐行开-写-关策略下无持久脏缓冲（appendLine 的 fclose 即落盘）——
    // §7.6 flush 原语在此实现下恒成功（幂等面）。
    (void)file;
    return true;
}

bool FileLogFileOps::rotateFile(const std::filesystem::path& file, std::uint32_t keepFiles)
{
    // 世代移位：活动→.1，.1→.2，…，超出 keepFiles-1 的最老档删除
    // （§7.8"轮转覆盖"生命周期——旧档不可达即消亡，无归档语义）。
    std::lock_guard<std::mutex> lock(m_impl->mtx);
    const std::uint32_t keep = std::max<std::uint32_t>(1u, keepFiles);
    std::error_code ec;
    // 自高位向低位移位，避免覆盖：先把 .（keep-2）挪到 .（keep-1），……最后活动→.1。
    for (std::uint32_t i = keep - 1u; i >= 2u; --i) {
        std::filesystem::path older = file;
        older += "." + std::to_string(i - 1u);
        std::filesystem::path newer = file;
        newer += "." + std::to_string(i);
        if (!std::filesystem::exists(older, ec)) { continue; }  // 无该世代档＝跳过
        std::filesystem::remove(newer, ec);                     // 目标位残留先清（best-effort）
        std::filesystem::rename(older, newer, ec);
        if (ec) { return false; }
    }
    std::filesystem::path first = file;
    first += ".1";
    if (std::filesystem::exists(file, ec)) {
        std::filesystem::remove(first, ec);
        std::filesystem::rename(file, first, ec);
        if (ec) { return false; }
    }
    return true;
}

std::uint64_t FileLogFileOps::fileSize(const std::filesystem::path& file)
{
    std::error_code ec;
    const auto size = std::filesystem::file_size(file, ec);
    return ec ? 0u : static_cast<std::uint64_t>(size);
}

// =====================================================================
// LoggingPipeline::Impl——队列/日志线程/五步管线/重放合并
// =====================================================================

// =====================================================================
// 管线内部数据结构（文件局部——仅 Impl 及其方法使用；std::deque 要求完整
// 类型，故先于 Impl 定义）
// =====================================================================

namespace {

/**
 * @brief 队列条目（调用方线程入队 → 日志线程串行消费）。
 *
 * kind 四类：Record（本地日志行——含管线自产行）、Batch（worker 重放批）、
 * Configure（配置切换）、Shutdown（关闭收尾）。internal 行（管线自产）带
 * 节流/采样旁路标记（实现口径 6：自产行不被自身机制吞噬；写失败/断裂补条
 * 行保留节流聚合）。
 */
struct LogItem {
    enum class Kind { Record, Batch, Configure, Shutdown };

    Kind kind = Kind::Record;
    bool applyThrottle = true;    ///< 是否参与节流（§7.4——自产汇总行旁路）
    bool applySampling = true;    ///< 是否参与采样（§7.4——自产行与 Debug/Trace 之外旁路）
    LogRecord record;             ///< Record 载荷
    WorkerLogBatch batch;         ///< Batch 载荷
    LogSinkConfig config;         ///< Configure 载荷
};

/**
 * @brief 节流窗状态（§7.4——键 (code, channel)；仅日志线程触碰）。
 *
 * 首条全量后，窗内同键记录压制并计数；窗关闭时补一条汇总行（含
 * occurrences）。窗判定用记录自身时间戳（注入时钟——确定性）。
 */
struct ThrottleState {
    bool active = false;                                    ///< 是否已有首条（窗口开启）
    std::chrono::system_clock::time_point start{};          ///< 窗口起点（首条时间戳）
    std::uint64_t occurrences = 0;                          ///< 窗内压制数（汇总行消费）
    LogLevel level = LogLevel::Info;                        ///< 窗内首条级别（汇总行级别承载）
    bool hadUser = false;                                   ///< 窗内是否含 Tier-U（汇总行落盘面）
};

/**
 * @brief 采样状态（§7.4——键 channel；仅 Debug/Trace 计数，仅日志线程触碰）。
 *
 * 首条＋每 N 条当场落盘；被采掉的最新一条存尾条槽（"末条"），在关闭排空
 * 时写出（进行中的流没有"末条"可定义——§7.4 承诺在关闭路径兑现）。
 */
struct SampleState {
    std::uint64_t count = 0;                    ///< 该通道 Debug/Trace 累计（采样判定基数）
    std::optional<LogRecord> tail;              ///< 尾条槽（最近一条被采掉行）
};

/**
 * @brief worker 重放流状态（§7.5——键 (workerId, task)；仅日志线程触碰）。
 *
 * expected＝下一个期望批次序号（从 0 起——批次在流内从 0 连续编号）；
 * pending＝乱序到达的缓冲批（序号→批）；closed＝已收 final（后续批一律
 * 陈旧丢弃）。
 */
struct WorkerStream {
    std::uint64_t expected = 0;
    std::map<std::uint64_t, WorkerLogBatch> pending;
    bool closed = false;
};

}  // namespace

struct LoggingPipeline::Impl {
    // ---- 注入依赖（调用方持有，引用生命周期覆盖本管线）----
    IClock& clock;        ///< 时间来源（记录时间戳/节流窗/flush 时间窗）
    ILogFileOps& ops;     ///< 文件接缝（DT-LOG-3 故障注入点）

    // ---- 调用方线程面（互斥量保护）----
    std::mutex mtx;                       ///< 护 queue/processing/stopped/cfg 快照
    std::condition_variable cv;           ///< 队列非空通知（消费者）＋排空通知（flush）
    std::deque<LogItem> queue;               ///< 异步队列（§9.6"异步管线——返回即入队确认"）
    bool processing = false;              ///< 日志线程正在锁外处理条目（flush 排空判据）
    bool stopped = false;                 ///< Shutdown 已入队（其后调用方入队防御性丢弃）
    std::shared_ptr<const LogSinkConfig> cfg;  ///< 配置快照（configure 切换＝整值替换）
    std::atomic<std::uint32_t> capacity{4096}; ///< 队列容量镜像（入队侧无锁读取——queueCapacity）
    std::thread worker;                   ///< 日志线程（§7.3"串行化于日志线程"）

    // ---- NFR-SEC-07 全量脱敏接线（§7.3②——DIAG-T08；§14.4 v0.9 登记）----
    // 专用互斥＋shared_ptr 快照：attach 任意线程调用，renderRecord（日志线
    // 程）与析构排空（关闭线程）在每行渲染前取一次快照——挂接切换的生效点
    // ＝下一行（在途行不回溯，与 §9.5 setPolicy 同语义）。独立于 mtx：渲染
    // 路径在锁外运行，两锁无嵌套即无序要求。mutable＝快照读取为 const 面。
    mutable std::mutex redactionMtx;                          ///< 护 redaction 快照切换
    std::shared_ptr<const IRedactionService> redaction;       ///< 脱敏服务（可空＝未接线）

    /// 当前脱敏服务快照（空＝未接线——只做 Tier-U 呈现过滤的 v0.7 原语义）。
    std::shared_ptr<const IRedactionService> redactionSnapshot() const
    {
        std::lock_guard<std::mutex> lock(redactionMtx);
        return redaction;
    }

    // ---- 崩溃前开发日志快照环形缓冲（§7.6——DIAG-T08 崩溃诊断文件消费）----
    // 容量 kLogDevTailRingLines＝512（§7.6 行数值）；写＝日志线程（渲染时），
    // 读＝崩溃诊断文件写出线程（任意线程）——独立互斥承载并发。
    mutable std::mutex devTailMtx;                            ///< 护环形缓冲并发
    std::deque<std::string> devTail;                          ///< 最近 Dev 侧行（渲染后全文）

    /// 渲染后行进环形缓冲（日志线程调用；覆盖语义＝只保最近 512 行——§7.6）。
    void pushDevTail(const std::string& line)
    {
        std::lock_guard<std::mutex> lock(devTailMtx);
        if (devTail.size() >= kLogDevTailRingLines) {
            devTail.pop_front();  // 覆盖最老行（环形语义）
        }
        devTail.push_back(line);
    }

    // ---- 日志线程私有面（串行化即无锁）----
    struct FileState {
        std::uint64_t size = 0;        ///< 当前大小估计（fileSize 基线＋写增量——轮转判定）
        bool sizeKnown = false;        ///< 基线是否已取（configure/rotate 后重取）
        bool dirty = false;            ///< 有未冲刷行（flush 窗口执行面）
    };
    FileState files[2];                ///< [0]=User [1]=Dev（LogFileId 顺序）
    std::map<std::pair<std::string, std::string>, ThrottleState> throttle;  ///< (code,channel)→窗
    std::map<std::string, SampleState> samples;                             ///< channel→采样态
    std::map<std::pair<std::uint64_t, core::TaskIdentity>, WorkerStream> streams;  ///< 重放流
    std::uint64_t overflowDrops = 0;   ///< 队列满丢弃计数（§9.6——Dev 计数行消费）
    std::uint64_t staleDrops = 0;      ///< 陈旧/重复批次丢弃计数（§7.5 幂等重放）
    std::uint64_t writeFailures = 0;   ///< 写失败累计（§7.6 降级计数——failure 行承载）
    std::uint32_t linesSinceFlush = 0; ///< flush 行窗计数（§7.3⑤ N=256）
    std::chrono::system_clock::time_point lastFlush{};  ///< flush 时间窗基线（T=2 s）
    bool finalizing = false;           ///< 关闭收尾中（自产行改为直接渲染——队列不再消费）
    std::uint64_t writeSeq = 0;        ///< 行序号（实现口径 1：写入序，日志线程私有）
    std::uint64_t threadFaults = 0;    ///< 日志线程逃逸异常计数（runLoop 顶界兜底观测）

    Impl(IClock& clockRef, ILogFileOps& opsRef)
        : clock{clockRef}
        , ops{opsRef}
        , cfg{std::make_shared<const LogSinkConfig>()}  // 默认配置起步（enabled＋空目录＝安全降态）
    {
    }

    // ---- 配置与路径 ----

    /// 目标文件路径（config 解析单点；目录空＝返回空路径＝写失败降态，绝不
    /// 落到进程当前目录——D-10 副作用边界）。
    std::filesystem::path filePathFor(const LogSinkConfig& c, bool user) const
    {
        if (c.directory.empty()) { return {}; }
        return c.directory / (user ? c.userFileName : c.devFileName);
    }

    /// 配置切换（日志线程执行——与渲染串行）。重置文件大小基线（路径可能
    /// 变化）与 flush 计数；节流/采样/重放态跨配置保留（运行中调参不重置
    /// 观测聚合——§9.6 configure 仅"配置"，无重置语义）。
    void applyConfig(const LogSinkConfig& c)
    {
        {
            std::lock_guard<std::mutex> lock(mtx);
            cfg = std::make_shared<const LogSinkConfig>(c);
            capacity.store(c.queueCapacity, std::memory_order_relaxed);
        }
        files[0] = FileState{};
        files[1] = FileState{};
        linesSinceFlush = 0;
        lastFlush = clock.nowUtc();
    }

    /// 当前配置快照（日志线程每条目起点取一次——渲染期间整值稳定）。
    std::shared_ptr<const LogSinkConfig> currentConfig()
    {
        std::lock_guard<std::mutex> lock(mtx);
        return cfg;
    }

    // ---- 入队（调用方线程）----

    /// 日志记录入队（bounded＝受容量约束——§9.6 队列满策略；管线自产行
    /// force 直入不受限——自产行丢失会破坏断裂补条/失败计数语义）。
    void pushRecord(LogRecord record, bool applyThrottle, bool applySampling, bool bounded)
    {
        LogItem item;
        item.kind = LogItem::Kind::Record;
        item.applyThrottle = applyThrottle;
        item.applySampling = applySampling;
        item.record = std::move(record);
        pushItem(std::move(item), bounded);
    }

    void pushItem(LogItem item, bool bounded)
    {
        std::unique_lock<std::mutex> lock(mtx);
        if (stopped) {
            return;  // 关闭后的调用＝契约外（析构注释：调用方须已停止使用）——防御性丢弃
        }
        if (bounded) {
            const std::size_t cap = capacity.load(std::memory_order_relaxed);
            if (queue.size() >= cap) {
                // §9.6：队列满→丢弃最旧 Debug/Trace＋Dev 计数诊断（不阻塞、不抛）。
                // 只丢 Regular 记录条目（Configure/Batch/自产行不可丢——丢配置
                // 或补条会破坏语义）；全不可丢时丢弃新行（实现口径登记 §14.4）。
                bool dropped = false;
                for (auto it = queue.begin(); it != queue.end(); ++it) {
                    if (it->kind == LogItem::Kind::Record
                        && (it->record.level == LogLevel::Debug
                            || it->record.level == LogLevel::Trace)) {
                        queue.erase(it);
                        ++overflowDrops;
                        dropped = true;
                        break;
                    }
                }
                if (!dropped) {
                    ++overflowDrops;  // 无可丢行（全是 Error/Warning/Info）→ 丢新行
                    return;
                }
            }
        }
        queue.push_back(std::move(item));
        lock.unlock();
        cv.notify_all();
    }

    /// 关闭标记入队（幂等——析构入口；强制不受容量约束：关闭信号不可丢）。
    void requestShutdown()
    {
        std::lock_guard<std::mutex> lock(mtx);
        if (stopped) { return; }
        stopped = true;
        LogItem stop;
        stop.kind = LogItem::Kind::Shutdown;
        queue.push_back(std::move(stop));
    }

    // ---- 日志线程主循环 ----

    /// 线程顶界（最后防线——纵深防御兜底）：任何逃逸异常在此吞掉并落
    /// stderr 摘要（§7.6"写出失败→stderr 摘要＋放弃"同款口径）。日志非权威
    /// （§7.8），绝不允许日志线程异常终止进程（terminate→abort）。内部路径
    /// 已逐层屏障（guardOps*/入口校验），本防线面向未知残余抛点。
    void runLoop() noexcept
    {
        try {
            runLoopInner();
        } catch (const std::exception& e) {
            ++threadFaults;
            std::fputs("[ird-diagnostics] logging thread escaped exception: ", stderr);
            std::fputs(e.what(), stderr);
            std::fputs("\n", stderr);
        } catch (...) {
            ++threadFaults;
            std::fputs("[ird-diagnostics] logging thread escaped unknown exception\n",
                       stderr);
        }
    }

    /// 消费循环。持锁不变量：进入 cv.wait 时**恒已持锁**（wait 仅在阻塞期间
    /// 释放、被唤醒后重新持锁返回；若谓词已为真则原样返回——因此每轮末尾
    /// 必须重新 lock 再回 wait，否则"未持锁返回"会使后续 unlock 抛 EPERM）。
    void runLoopInner()
    {
        std::unique_lock<std::mutex> lock(mtx);
        while (true) {
            // 不变量：此处持锁。谓词为假时阻塞等待（原子释放/重取）；为真时
            // 立即返回（仍持锁）。
            cv.wait(lock, [this] { return !queue.empty(); });
            LogItem item = std::move(queue.front());
            queue.pop_front();
            if (item.kind == LogItem::Kind::Shutdown) {
                lock.unlock();
                finalize();  // 收尾：断裂补条/节流汇总/采样尾条/冲刷（§7.6 排空语义）
                return;
            }
            processing = true;
            lock.unlock();       // 锁外渲染——磁盘 I/O 不持队列锁
            processItem(item);
            lock.lock();         // 重取（显式恢复不变量——见函数头注释）
            // 排空边界：队列清空且存在丢弃计数→补一条 Dev 计数行（§9.6"丢弃
            // 最旧 Debug/Trace＋Dev 计数诊断"；§7.5 陈旧批计数同面承载）。
            if (queue.empty() && (overflowDrops > 0 || staleDrops > 0)) {
                LogItem summary;
                summary.kind = LogItem::Kind::Record;
                summary.applyThrottle = false;  // 自产汇总行旁路节流（实现口径 6）
                summary.applySampling = false;
                summary.record.tier = LogTier::Dev;
                summary.record.level = LogLevel::Warning;
                summary.record.channel = kChannelLogging;
                summary.record.message = "[log-overflow] 队列满/陈旧批：丢弃 Debug/Trace 行 "
                                       + std::to_string(overflowDrops) + " 条，陈旧批次 "
                                       + std::to_string(staleDrops) + " 个（尽力而为降级）";
                overflowDrops = 0;
                staleDrops = 0;
                queue.push_back(std::move(summary));
            }
            processing = false;
            lock.unlock();
            cv.notify_all();     // flush() 排空等待者复查
            lock.lock();         // 回 wait 前重取——维持"进入 wait 恒持锁"不变量
        }
    }

    void processItem(LogItem& item)
    {
        switch (item.kind) {
        case LogItem::Kind::Record: {
            // 本地路径：写入时间在渲染时打点（§7.2 timestampUtc＝写入时间，
            // 注入 IClock——单线程渲染无时钟并发面）。
            auto cfgPtr = currentConfig();
            item.record.timestampUtc = clock.nowUtc();
            renderRecord(item.record, *cfgPtr, item.applyThrottle, item.applySampling);
            break;
        }
        case LogItem::Kind::Batch:
            processBatch(item.batch);
            break;
        case LogItem::Kind::Configure:
            applyConfig(item.config);
            break;
        case LogItem::Kind::Shutdown:
            break;  // runLoop 已处理（不可达——防御性保留）
        }
    }

    // ---- 五步管线（§7.3——单条记录；日志线程串行）----

    /**
     * @brief 渲染一条记录（①截断→②脱敏→③过滤→④节流采样→⑤写入＋flush
     *        窗口）。
     *
     * @param rec            [in,out] 记录（message 就地截断/脱敏；timestampUtc
     *                       由调用前打点或为 worker 原值）
     * @param c              [in] 配置快照（本条目渲染期内稳定）
     * @param applyThrottle  [in] 是否参与节流（自产汇总行旁路）
     * @param applySampling  [in] 是否参与采样（自产行旁路）
     */
    void renderRecord(LogRecord& rec, const LogSinkConfig& c, bool applyThrottle,
                      bool applySampling)
    {
        // ①格式化：message 定长截断（§7.2 ≤4 KiB——关联 ID 为结构化字段，
        // 无需字符串规范化；行编码时统一转义）。
        rec.message = truncateForLog(rec.message);

        // ②脱敏（§7.3②"每条消息强制经过 IRedactionService"——NFR-SEC-07
        // 全量脱敏随 DIAG-T08 接线，§14.4 v0.9 登记）：Dev 档全量规则
        // （凭据/令牌/环境变量/用户名/路径按策略）对两 Tier 统一生效——
        // "两 Tier 同一脱敏管线"（§7.1）；Tier-U 文件的"额外内部模式过滤"
        // 仍由下方呈现层承担（v0.8 口径②镜像语义不变）。内部自省通道旁路
        // （isInternalDiagChannel——自产常量文本，再过脱敏会形成失败行回环）。
        // 脱敏服务绝不抛出（§9.5）——此处的 noexcept 保证与管线"环境面不抛"
        // 契约一致；未接线（快照为空）＝v0.7 原语义（装配前合法降态）。
        const std::shared_ptr<const IRedactionService> red = redactionSnapshot();
        if (red != nullptr && !isInternalDiagChannel(rec.channel)) {
            rec.message = red->redact(rec.message, LogTier::Dev);
        }

        // ③级别过滤（§7.3③）：Tier-U 结构性只收 Error/Warning/Info（入口
        // 校验已保证）；Tier-D 按 devMinLevel 配置（秩 ≤ 下限才写）。
        const bool userPass = rec.tier == LogTier::User && c.enabled && c.userEnabled;
        const bool devPass = c.enabled && c.devEnabled
            && logLevelRank(rec.level) <= logLevelRank(c.devMinLevel);
        if (!userPass && !devPass) {
            return;  // 双侧皆不落盘（禁用/过滤）——节流采样状态不推进（§7.8：日志
                     // 关闭只影响日志面）
        }

        // ④节流（§7.4）：同 (code+channel) 高频重复——首条全量＋每窗汇总。
        // 仅带 code 的记录参与（"目录去重命中、循环重试"场景锚定诊断事件）。
        if (applyThrottle && rec.code.has_value() && throttleSuppressed(rec, c)) {
            return;
        }

        // ④采样（§7.4）：仅 Debug/Trace 可采样（Error/Warning/Info 不采样——
        // 用户语义事件不丢）。
        const bool samplable = rec.level == LogLevel::Debug || rec.level == LogLevel::Trace;
        if (applySampling && samplable && samplingDrop(rec, c)) {
            return;
        }

        // ⑤写入：Tier-D 文件保留原文（全量技术细节）；Tier-U 文件用模式过滤
        // 后的呈现（NFR-REL-05/UX-02——镜像语义见文件头实现口径 2）。
        // （采样尾条不在常规写入前补出——"末条"只在关闭排空时落盘，§7.4。）
        const bool failureLine = rec.code.has_value() && *rec.code == kCodeLogWriteFailed
            && rec.channel == kChannelLogging;
        if (devPass) {
            // 渲染后全文先进崩溃快照环形缓冲（§7.6"最近 512 行开发日志快照
            // （重放内存环形缓冲）"——DIAG-T08 崩溃诊断文件经 devTailSnapshot
            // 消费；先于磁盘写——磁盘满时快照仍完整，DT-LIFE-4 场景的价值面），
            // 再写 Tier-D 文件。
            const std::string devLine = formatLine(rec, /*masked=*/false);
            pushDevTail(devLine);
            writeLine(false /*dev*/, devLine, failureLine, c);
        }
        if (userPass) {
            writeLine(true /*user*/, formatLine(rec, true), failureLine, c);
        }

        // flush 窗口（§7.3⑤/§7.6）：Error 即时；每 N 行；距上次 ≥T。
        noteWritten(rec.level, c);
    }

    /**
     * @brief 节流判定与窗管理（§7.4）。
     * @return true＝该记录被压制（不计行）；false＝放行（可能已补写上一窗
     *         汇总行）。
     */
    bool throttleSuppressed(LogRecord& rec, const LogSinkConfig& c)
    {
        const auto key = std::make_pair(*rec.code, rec.channel);
        ThrottleState& st = throttle[key];  // 日志线程私有——无并发面
        const auto win = std::chrono::seconds{c.throttleWindowSeconds};
        if (win.count() == 0) {
            return false;  // 窗宽 0＝节流禁用（配置自由度——工程默认 10 s）
        }
        if (!st.active) {
            // 首条：全量放行并开窗（§7.4"首条全量"）。
            st.active = true;
            st.start = rec.timestampUtc;
            st.occurrences = 0;
            st.level = rec.level;
            st.hadUser = rec.tier == LogTier::User;
            return false;
        }
        if (rec.timestampUtc - st.start < win) {
            // 窗内重复：压制＋计数（"节流只影响日志行数"——目录/envelope 不在此面）。
            ++st.occurrences;
            st.hadUser = st.hadUser || rec.tier == LogTier::User;
            return true;
        }
        // 窗关闭：先补上一窗汇总行（含 occurrences——§7.4），再以本条重开窗。
        if (st.occurrences > 0) { emitThrottleSummary(key, st, c); }
        st.start = rec.timestampUtc;
        st.occurrences = 0;
        st.level = rec.level;
        st.hadUser = rec.tier == LogTier::User;
        return false;
    }

    /// 节流汇总行（自产——旁路节流/采样，避免自吞）。直接渲染（不经队列）：
    /// 与触发"窗关闭"的记录同在日志线程串行流上，直接渲染保证汇总行落在该
    /// 记录之前（上一窗的事实先于新窗首条），序号仍写入序单调。
    void emitThrottleSummary(const std::pair<std::string, std::string>& key,
                             const ThrottleState& st, const LogSinkConfig& c)
    {
        LogRecord r;
        r.timestampUtc = clock.nowUtc();
        // 窗内含 Tier-U 时汇总行走用户面（用户级事件被节流后，用户文件仍可
        // 见"每 10 s 一条"的聚合事实——UX 不静默）；否则仅开发面。
        r.tier = st.hadUser ? LogTier::User : LogTier::Dev;
        r.level = st.level;
        r.channel = key.second;
        r.code = key.first;
        r.message = truncateForLog("[throttled] occurrences=" + std::to_string(st.occurrences)
                                   + "（同 code+channel 窗口聚合，§7.4）");
        renderRecord(r, c, /*applyThrottle=*/false, /*applySampling=*/false);
    }

    /**
     * @brief 采样判定（§7.4：首条＋每 N 条＋末条）。
     * @return true＝被采掉（存尾条槽——"末条"只在关闭排空时写出：进行中的
     *         流没有"末条"可定义，中间被采掉的行由后续覆盖，最终只保流尾）。
     */
    bool samplingDrop(LogRecord& rec, const LogSinkConfig& c)
    {
        SampleState& st = samples[rec.channel];
        // N 取值：通道覆盖表优先，缺省回全局默认（"N 随通道配置，默认 100"）。
        std::uint32_t n = c.sampleEveryDebug;
        const auto it = c.sampleEveryByChannel.find(rec.channel);
        if (it != c.sampleEveryByChannel.end()) { n = it->second; }
        ++st.count;
        if (st.count == 1 || (n != 0 && st.count % n == 0)) {
            return false;  // 首条或第 N 条——落盘
        }
        st.tail = std::move(rec);  // 采掉——最新一条进尾条槽（覆盖上一尾条：只保末条）
        return true;
    }

    /// 自产行出口：finalizing 前经队列（保全局稳定序——实现口径 1）；收尾期
    /// 队列不再消费，直接渲染（序号仍写入序单调）。
    void emitInternal(LogRecord r, const LogSinkConfig& c)
    {
        r.message = truncateForLog(r.message);
        if (finalizing) {
            renderRecord(r, c, /*applyThrottle=*/false, /*applySampling=*/false);
            return;
        }
        pushRecord(std::move(r), /*applyThrottle=*/false, /*applySampling=*/false,
                   /*bounded=*/false);
    }

    // ---- 写入面（⑤——经 ILogFileOps 接缝；轮转与 flush 窗口在此执行）----

    /**
     * @brief 写一行到指定文件（user=true→用户级文件）。
     *
     * 失败语义（DT-LOG-3）：接缝返回 false 或抛异常→计数＋DIAG-LOG-WRITE-FAILED
     * Dev 行（节流聚合；failureLine 抑制位防止失败行自身失败再递归补条）。
     * 绝不向调用方传播（log 返回即入队确认——§9.6）。
     */
    void writeLine(bool user, const std::string& line, bool failureLine,
                   const LogSinkConfig& c)
    {
        const std::filesystem::path path = filePathFor(c, user);
        if (path.empty()) {
            // 目录未配置（装配未完成的安全降态）：计入失败计数，不发失败行
            // （每行都发会风暴——降态是配置问题，装配完成后自然恢复）。
            ++writeFailures;
            return;
        }
        FileState& fs = files[user ? 0 : 1];
        if (!fs.sizeKnown) {
            fs.size = guardOpsSize([&] { return ops.fileSize(path); });
            fs.sizeKnown = true;
        }
        // 轮转判定（§7.6/§7.8——P-DIAG-7 工程默认 5×2 MiB）：越过阈值即轮转
        // （单行超限时轮转后仍写入超限行——单行完整性优先于单文件上限）。
        if (c.rotateMaxBytes > 0 && fs.size + line.size() + 1 > c.rotateMaxBytes) {
            guardOpsBool([&] { return ops.rotateFile(path, c.rotateFiles); });
            fs.size = 0;
            // 轮转结果不单独发失败行：若磁盘真不可用，紧随的 appendLine 失败
            // 会补条；轮转成功与否不改变"尽力而为"语义。
        }
        const bool ok = guardOpsBool([&] { return ops.appendLine(path, line); });
        if (ok) {
            fs.size += line.size() + 1;
            fs.dirty = true;
            return;
        }
        ++writeFailures;
        if (failureLine) {
            return;  // 失败行自身写失败——只计数，不再递归补条（防风暴/防环）
        }
        LogRecord r;
        r.timestampUtc = clock.nowUtc();
        r.tier = LogTier::Dev;
        r.level = LogLevel::Warning;
        r.channel = kChannelLogging;
        r.code = std::string{kCodeLogWriteFailed};
        r.message = "[log-write-failed] file=" + std::string{user ? "user" : "dev"}
                  + " 写失败（累计 " + std::to_string(writeFailures)
                  + " 次；诊断与正式路径不受影响——§7.8）";
        emitInternal(std::move(r), c);
    }

    /// 冲刷有脏缓冲的文件（§7.6 flush 窗口执行面；suppressFailure＝失败行
    /// 渲染路径内——不递归补条）。
    void flushDirty(bool suppressFailure, const LogSinkConfig& c)
    {
        for (int i = 0; i < 2; ++i) {
            if (!files[i].dirty) { continue; }
            const std::filesystem::path path = filePathFor(c, i == 0);
            if (path.empty()) {
                files[i].dirty = false;
                continue;
            }
            if (guardOpsBool([&] { return ops.flushFile(path); })) {
                files[i].dirty = false;
            } else if (!suppressFailure) {
                ++writeFailures;
                LogRecord r;
                r.timestampUtc = clock.nowUtc();
                r.tier = LogTier::Dev;
                r.level = LogLevel::Warning;
                r.channel = kChannelLogging;
                r.code = std::string{kCodeLogWriteFailed};
                r.message = "[log-write-failed] file=" + std::string{i == 0 ? "user" : "dev"}
                          + " flush 失败（累计 " + std::to_string(writeFailures) + " 次）";
                emitInternal(std::move(r), c);
            }
        }
    }

    /// flush 窗口判定（§7.3⑤/§7.6：Error 即时；N 行；T 时——三条件任一）。
    void noteWritten(LogLevel level, const LogSinkConfig& c)
    {
        ++linesSinceFlush;
        const auto now = clock.nowUtc();
        const bool errorNow = level == LogLevel::Error;
        const bool lineWindow = c.flushEveryLines > 0
            && linesSinceFlush >= c.flushEveryLines;
        const bool timeWindow = c.flushInterval.count() > 0
            && now - lastFlush >= c.flushInterval;
        if (errorNow || lineWindow || timeWindow) {
            flushDirty(false, c);
            linesSinceFlush = 0;
            lastFlush = now;
        }
    }

    // ---- 行格式化（①输出编码——键值文本格式，§1.4"自有文本/键值格式"）----

    /**
     * @brief 日志行编码。
     *
     * 形态（空格分隔五定长头字段＋kv 修饰＋行尾消息）：
     *   "<seq> <epochMs> <U|D> <LEVEL> <channel>[ code=<码>][ ids=<k:v;…>]
     *   [ tag=<线程标>][ wk=<workerId>] msg=<转义消息>"
     * 消息恒在行尾（msg= 之后到行尾）——载荷任意内容不破坏前缀解析；转义
     * 保证单行（DT-LOG-2"行完整"）。masked=true 时消息经 Tier-U 模式过滤。
     */
    std::string formatLine(const LogRecord& rec, bool masked)
    {
        ++writeSeq;
        std::string line = std::to_string(writeSeq);
        line += ' ';
        line += epochMillis(rec.timestampUtc);
        line += ' ';
        line += rec.tier == LogTier::User ? 'U' : 'D';
        line += ' ';
        line += logLevelToken(rec.level);
        line += ' ';
        line += rec.channel;
        if (rec.code.has_value()) {
            line += " code=";
            line += *rec.code;
        }
        // 关联 ID 块（§7.2 结构化承载；k:v 以 ';' 相连——观测面解析友好）。
        std::string ids;
        if (rec.correlationIds.entryId.has_value()) {
            ids += "entry:" + std::to_string(*rec.correlationIds.entryId) + ";";
        }
        if (rec.correlationIds.findingId.has_value()) {
            ids += "finding:" + rec.correlationIds.findingId->toCanonical() + ";";
        }
        if (rec.correlationIds.taskId.has_value()) {
            ids += "task:" + taskCanonical(*rec.correlationIds.taskId) + ";";
        }
        if (rec.correlationIds.runId.has_value()) {
            ids += "run:" + rec.correlationIds.runId->toCanonical() + ";";
        }
        if (rec.correlationIds.attemptId.has_value()) {
            ids += "att:" + std::to_string(rec.correlationIds.attemptId->value) + ";";
        }
        if (rec.correlationIds.revisionId.has_value()) {
            ids += "rev:" + rec.correlationIds.revisionId->toCanonical() + ";";
        }
        if (!ids.empty()) {
            ids.pop_back();  // 去尾分号
            line += " ids=" + ids;
        }
        if (!rec.threadTag.empty()) {
            line += " tag=" + rec.threadTag;
        }
        if (rec.workerId.has_value()) {
            line += " wk=" + std::to_string(*rec.workerId);
        }
        line += " msg=";
        line += escapeLineMessage(masked ? maskUserPatterns(rec.message) : rec.message);
        return line;
    }

    // ---- worker 重放合并（§7.5——(workerId, task) 流维度的序号重放）----

    void processBatch(WorkerLogBatch& batch)
    {
        auto cfgPtr = currentConfig();
        const LogSinkConfig& c = *cfgPtr;
        const auto key = std::make_pair(batch.workerId, batch.task);
        WorkerStream& st = streams[key];
        if (st.closed) {
            ++staleDrops;  // 收尾后的迟到批＝通道异常（陈旧丢弃——幂等重放口径）
            return;
        }
        if (batch.seq < st.expected) {
            ++staleDrops;  // 重复/陈旧批：丢弃不重放（通道重传不产生重复行）
            return;
        }
        if (batch.seq == st.expected) {
            // 按期望到达：直接接受，随后连续就绪的缓冲批依次落盘（乱序→恢复
            // 全局稳定序——§7.5"按序号重放合并"）。
            acceptBatch(batch, c);
            st.expected = batch.seq + 1;
            drainReady(st, c);
            if (batch.final) { st.closed = true; }
            return;
        }
        // batch.seq > expected：整批缓冲（缺口可能仍在途——不补条不落盘）。
        if (!batch.final) {
            st.pending.emplace(batch.seq, std::move(batch));
            return;
        }
        // 收尾批越过缺口：断裂判定点——[expected, seq) 中不在缓冲的序号补条
        // （§7.5"批次丢失→主进程补一条 Dev 诊断"），在缓冲的按序落盘。
        for (std::uint64_t s = st.expected; s < batch.seq; ++s) {
            const auto it = st.pending.find(s);
            if (it != st.pending.end()) {
                acceptBatch(it->second, c);
                st.pending.erase(it);
            } else {
                emitGapLine(key, s, c);
            }
        }
        acceptBatch(batch, c);
        st.expected = batch.seq + 1;
        staleDrops += st.pending.size();  // 收尾后的滞留缓冲＝异常残渣（计数丢弃）
        st.pending.clear();
        st.closed = true;
    }

    /// 接受一批：记录规范化（补注 workerId/任务关联——"correlationIds 保留
    /// workerId"§7.5）后走完整管线（原始时间戳保留——§7.4 可重排）。
    void acceptBatch(const WorkerLogBatch& batch, const LogSinkConfig& c)
    {
        for (const LogRecord& src : batch.records) {
            LogRecord rec = src;  // 值拷贝——补注不回写批载荷
            rec.workerId = batch.workerId;
            if (!rec.correlationIds.taskId.has_value()) {
                rec.correlationIds.taskId = batch.task;
            }
            renderRecord(rec, c, /*applyThrottle=*/true, /*applySampling=*/true);
        }
    }

    /// 连续就绪的缓冲批依次接受（乱序恢复序的执行点）。
    void drainReady(WorkerStream& st, const LogSinkConfig& c)
    {
        while (true) {
            const auto it = st.pending.find(st.expected);
            if (it == st.pending.end()) { break; }
            acceptBatch(it->second, c);
            st.pending.erase(it);
            ++st.expected;
        }
    }

    /// 断裂补条（EX-CHANNEL-PROTOCOL-ERROR Dev 行——§7.5；Warning 级不触及
    /// 采样）。直接渲染（不经队列）：批记录在 processBatch 内即时落盘，补条
    /// 必须与被补批记录保持"缺口位在前、后继批在后"的流内次序（同线程串行
    /// 渲染天然保序——序号仍写入序单调）。
    void emitGapLine(const std::pair<std::uint64_t, core::TaskIdentity>& key,
                     std::uint64_t seq, const LogSinkConfig& c)
    {
        LogRecord r;
        r.timestampUtc = clock.nowUtc();
        r.tier = LogTier::Dev;
        r.level = LogLevel::Warning;
        r.channel = kChannelMerge;
        r.code = std::string{kCodeChannelProtocol};
        r.correlationIds.taskId = key.second;
        r.workerId = key.first;
        r.message = truncateForLog("[log-gap] worker 日志通道断裂：期望批次序号 "
                                   + std::to_string(seq) + " 未到达（worker="
                                   + std::to_string(key.first) + "），补 Dev 诊断（§7.5）");
        renderRecord(r, c, /*applyThrottle=*/false, /*applySampling=*/false);
    }

    // ---- 关闭收尾（§7.6 排空＋§7.5 缓冲补条＋§7.4 尾态落盘）----

    void finalize()
    {
        finalizing = true;  // 自产行改直接渲染（队列不再消费）
        auto cfgPtr = currentConfig();
        const LogSinkConfig& c = *cfgPtr;
        // ① worker 重放缓冲排空：缺口补条＋缓冲批按序落盘（无 final 批的
        // 静默丢失在此补条——不悬挂、不丢弃，§7.5 断裂语义闭合）。
        for (auto& [key, st] : streams) {
            while (!st.pending.empty()) {
                const auto it = st.pending.begin();
                const std::uint64_t seq = it->first;
                for (std::uint64_t s = st.expected; s < seq; ++s) {
                    emitGapLine(key, s, c);
                }
                WorkerLogBatch batch = std::move(it->second);
                st.pending.erase(it);
                acceptBatch(batch, c);
                st.expected = seq + 1;
            }
        }
        // ② 节流窗未关闭的聚合行（窗内压制的 occurrences 不能静默消失——
        // §7.4 汇总承诺在关闭路径兑现）。
        for (auto& [key, st] : throttle) {
            if (st.active && st.occurrences > 0) {
                emitThrottleSummary(key, st, c);
                st.occurrences = 0;
            }
        }
        // ③ 采样尾条（"末条"——每通道至多一条，§7.4）。
        for (auto& [channel, st] : samples) {
            if (st.tail.has_value()) {
                LogRecord tail = std::move(*st.tail);
                st.tail.reset();
                tail.message = "[sampled-tail] " + tail.message;
                tail.message = truncateForLog(tail.message);
                renderRecord(tail, c, false, false);
            }
        }
        // ④ 冲刷（§7.6：崩溃/关闭前保留窗口的最后一道——尽力而为）。
        flushDirty(false, c);
        finalizing = false;
    }
};

// =====================================================================
// LoggingPipeline——公共接口转发
// =====================================================================

namespace {

/**
 * @brief 调用方入口校验（AGENTS.md 错误语义：结构性违约 fail-fast）。
 *
 * 校验面（§7.2 字段表＋§7.3③）：通道非空且 ≤48 字符；消息非空；线程标
 * ≤32 字符；tier=User 时 level 不得为 Debug/Trace（"Tier-U sink 只收
 * Error/Warning/Info"）。违约抛 DiagnosticsError(Usage)——仅此入口抛；
 * 队列/磁盘等环境面一律降级（§9.6"不阻塞调用方、不抛"）。
 */
void validateRecordBasics(const LogRecord& rec)
{
    if (rec.channel.empty() || rec.channel.size() > kLogChannelMaxChars) {
        throw DiagnosticsError(DiagnosticsErrorCode::Usage,
                               "log channel 为空或超 48 字符（§7.2 LogChannel）: "
                                   + rec.channel);
    }
    if (rec.message.empty()) {
        throw DiagnosticsError(DiagnosticsErrorCode::Usage,
                               "log message 为空（§7.2 message 必填）");
    }
    if (rec.threadTag.size() > kLogThreadTagMaxChars) {
        throw DiagnosticsError(DiagnosticsErrorCode::Usage,
                               "log threadTag 超 32 字符（§7.2 同 DiagnosticEntry）");
    }
    if (rec.tier == LogTier::User
        && (rec.level == LogLevel::Debug || rec.level == LogLevel::Trace)) {
        throw DiagnosticsError(DiagnosticsErrorCode::Usage,
                               "tier=User 仅允许 Error/Warning/Info（§7.3③ 级别过滤）");
    }
}

/// 配置不变量校验（configure 前置——违约 Usage fail-fast；0 值语义见成员注释）。
void validateConfig(const LogSinkConfig& c)
{
    if (c.rotateFiles == 0) {
        throw DiagnosticsError(DiagnosticsErrorCode::Usage,
                               "rotateFiles=0（轮转至少保留活动文件，§7.6）");
    }
    if (c.sampleEveryDebug == 0) {
        throw DiagnosticsError(DiagnosticsErrorCode::Usage,
                               "sampleEveryDebug=0（采样 N≥1，§7.4）");
    }
    for (const auto& [channel, n] : c.sampleEveryByChannel) {
        if (n == 0) {
            throw DiagnosticsError(DiagnosticsErrorCode::Usage,
                                   "sampleEveryByChannel 含 0 值（channel=" + channel
                                       + "，采样 N≥1，§7.4）");
        }
    }
    if (c.queueCapacity == 0) {
        throw DiagnosticsError(DiagnosticsErrorCode::Usage,
                               "queueCapacity=0（队列容量 ≥1，§9.6）");
    }
}

}  // namespace

LoggingPipeline::LoggingPipeline(IClock& clock, ILogFileOps& fileOps)
    : m_impl{std::make_unique<Impl>(clock, fileOps)}
{
    // 日志线程即启（§7.3 单一管线——构造即可接收记录；配置可后置 configure）。
    Impl* impl = m_impl.get();
    m_impl->worker = std::thread([impl] { impl->runLoop(); });
}

LoggingPipeline::~LoggingPipeline()
{
    // 关闭序列（§9.6 契约表"关闭时 flush 有界——不永久等待"）：
    // ①入队 Shutdown（强制——关闭信号不受容量约束）；②线程处理完此前全部
    // 条目后执行 finalize（断裂补条/汇总/尾条/冲刷）并退出；③join。单条目
    // 处理有界（文件接缝契约：快速返回）→ 排空整体有界。
    m_impl->requestShutdown();
    m_impl->cv.notify_all();
    if (m_impl->worker.joinable()) { m_impl->worker.join(); }
}

void LoggingPipeline::log(LogRecord record)
{
    validateRecordBasics(record);  // 结构性违约 fail-fast（环境面不抛——见实现注释）
    m_impl->pushRecord(std::move(record), /*applyThrottle=*/true, /*applySampling=*/true,
                       /*bounded=*/true);
}

void LoggingPipeline::logDev(std::string_view channel, LogLevel level, std::string message,
                             CorrelationIds ids)
{
    LogRecord record;
    record.tier = LogTier::Dev;
    record.level = level;
    record.channel.assign(channel);
    record.correlationIds = std::move(ids);
    record.message = std::move(message);
    validateRecordBasics(record);
    m_impl->pushRecord(std::move(record), /*applyThrottle=*/true, /*applySampling=*/true,
                       /*bounded=*/true);
}

void LoggingPipeline::logDev(std::string_view channel, std::string message)
{
    // IDevLogSink 双参形态（§9.7 尾注——reportDev 路由面）：级别固定 Debug
    // （开发诊断快记；采样按 §7.4 约束高频行数）。
    logDev(channel, LogLevel::Debug, std::move(message), CorrelationIds{});
}

bool LoggingPipeline::flush(std::chrono::milliseconds deadline)
{
    // 有界等待（§9.6"排空：有界等待；超时放弃＋DIAG-LOG-WRITE-FAILED——
    // 不永久等待"）。墙钟计量（steady_clock）——安全属性不依赖注入时钟。
    const auto until = std::chrono::steady_clock::now() + deadline;
    bool drained = false;
    {
        std::unique_lock<std::mutex> lock(m_impl->mtx);
        m_impl->cv.wait_until(lock, until, [this] {
            return m_impl->queue.empty() && !m_impl->processing;
        });
        drained = m_impl->queue.empty() && !m_impl->processing;
    }
    auto cfgPtr = m_impl->currentConfig();
    if (!drained) {
        // 超时放弃：补失败行（尽力而为——可能同样写不出）并如实返回 false。
        LogRecord r;
        r.timestampUtc = m_impl->clock.nowUtc();
        r.tier = LogTier::Dev;
        r.level = LogLevel::Warning;
        r.channel = kChannelLogging;
        r.code = std::string{kCodeLogWriteFailed};
        r.message = "[log-write-failed] flush 排空超时（有界等待放弃——§9.6/PM-03）";
        m_impl->emitInternal(std::move(r), *cfgPtr);
        return false;
    }
    // 排空成功：冲刷两文件（此窗口内日志线程空闲等待——与 appendLine 的并发
    // 可能仅来自新调用方入队，接缝实现自行串行化——ILoggFileOps 契约）。
    bool ok = true;
    if (cfgPtr->enabled) {
        for (int i = 0; i < 2; ++i) {
            const bool user = i == 0;
            if ((user && !cfgPtr->userEnabled) || (!user && !cfgPtr->devEnabled)) {
                continue;
            }
            const auto path = m_impl->filePathFor(*cfgPtr, user);
            if (path.empty()) { continue; }
            // 接缝允许抛异常——屏障消化为 false（DT-LOG-3"无异常逃逸"）。
            // 先冲刷后与运算：两个文件都尝试冲刷，任一失败即整体 false。
            ok = guardOpsBool([&] { return m_impl->ops.flushFile(path); }) && ok;
        }
    }
    if (!ok) {
        LogRecord r;
        r.timestampUtc = m_impl->clock.nowUtc();
        r.tier = LogTier::Dev;
        r.level = LogLevel::Warning;
        r.channel = kChannelLogging;
        r.code = std::string{kCodeLogWriteFailed};
        r.message = "[log-write-failed] flush 文件冲刷失败（累计 "
                  + std::to_string(m_impl->writeFailures + 1) + " 次）";
        m_impl->emitInternal(std::move(r), *cfgPtr);
        return false;
    }
    return true;
}

void LoggingPipeline::configure(LogSinkConfig config)
{
    validateConfig(config);  // 配置不变量 fail-fast（§9.6 configure 前置）
    LogItem item;
    item.kind = LogItem::Kind::Configure;
    item.config = std::move(config);
    m_impl->pushItem(std::move(item), /*bounded=*/false);  // 配置命令不受容量约束
}

void LoggingPipeline::replayWorkerBatch(WorkerLogBatch batch)
{
    // 载荷校验（P-EX-8：本 API 只消费 WorkerLogBatch 载荷——无任何帧格式
    // 语义）。五元组必须 isValid（TASK-03 边界）；批内记录与 log() 同一
    // 入口不变量（worker 侧管线本应拒绝——主进程复验防带病载荷）。
    if (!batch.task.isValid()) {
        throw DiagnosticsError(DiagnosticsErrorCode::Usage,
                               "replay 批次 task 五元组不 isValid（TASK-03）");
    }
    for (const LogRecord& rec : batch.records) {
        try {
            validateRecordBasics(rec);
        } catch (DiagnosticsError& e) {
            throw DiagnosticsError(e.code(),
                                   "replay 批内记录违约（worker=" + std::to_string(batch.workerId)
                                       + " seq=" + std::to_string(batch.seq) + "）: "
                                       + e.what());
        }
    }
    LogItem item;
    item.kind = LogItem::Kind::Batch;
    item.batch = std::move(batch);
    m_impl->pushItem(std::move(item), /*bounded=*/true);
}

void LoggingPipeline::attachRedactionService(std::shared_ptr<const IRedactionService> service)
{
    // 快照切换（§14.4 v0.9）：切换后新渲染的行生效，在途行不回溯——与
    // §9.5 setPolicy 同款语义。独立互斥（redactionMtx）——渲染路径在队列锁
    // 外取快照，两锁无嵌套。nullptr＝解除挂接（回到 v0.7 仅 Tier-U 呈现
    // 过滤的原语义——测试与装配前降态）。
    std::lock_guard<std::mutex> lock(m_impl->redactionMtx);
    m_impl->redaction = std::move(service);
}

std::vector<std::string> LoggingPipeline::devTailSnapshot(std::size_t maxLines) const
{
    // 崩溃快照读取面（§7.6）：任意线程安全（devTailMtx 护并发）；写入序返回，
    // 最多 maxLines 行（0＝空表；缓冲容量 kLogDevTailRingLines＝512 为硬上限
    // ——§7.6"最近 512 行"）。
    std::lock_guard<std::mutex> lock(m_impl->devTailMtx);
    const std::size_t skip = maxLines >= m_impl->devTail.size()
                                 ? 0
                                 : m_impl->devTail.size() - maxLines;
    std::vector<std::string> out;
    out.reserve(m_impl->devTail.size() - skip);
    for (std::size_t i = skip; i < m_impl->devTail.size(); ++i) {
        out.push_back(m_impl->devTail[i]);
    }
    return out;
}

}  // namespace sdurws::ird::diagnostics
