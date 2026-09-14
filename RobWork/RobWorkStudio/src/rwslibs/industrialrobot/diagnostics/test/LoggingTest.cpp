/**
 * @file   LoggingTest.cpp
 * @brief  两级日志用例组（DT-LOG-1~5）与 P-DIAG-7/P-EX-8 处置钉住——两级
 *         分流（Tier-U 无 Dev 行/无栈/无哈希）、关联 ID 与全局稳定序（同
 *         线程 FIFO）、写失败注入（DIAG-LOG-WRITE-FAILED 不阻塞调用方、
 *         flush 有界——ILogFileOps 接缝）、日志禁用不影响正式诊断（§7.8）、
 *         WorkerLogBatch 乱序/断裂重放合并；节流采样/轮转/队列溢出/截断
 *         行为面与入口违约面。
 *
 * 设计依据：
 *   - units/diagnostics.md §10 DT-LOG-1~5 行（§11 任务分工：本套件自证日志
 *     管线；DT-SEC-1~4 脱敏半区随 DIAG-T08）、§7.1（两级模型/Tier-U ⊆
 *     Tier-D）、§7.2（字段表——4 KiB 截断/token 边界）、§7.3（五步管线/
 *     flush 窗口）、§7.4（采样仅 Debug/Trace、节流 (code+channel) 窗、
 *     全局稳定序）、§7.5（WorkerLogBatch 重放/断裂补条）、§7.6（轮转与
 *     flush 窗口）、§7.8（日志非权威）、§9.6（ILogger 契约表——队列满
 *     丢弃最旧 Debug/Trace、flush 有界）
 *   - 需求 NFR-REL-05（两级日志；用户级无调用栈/内部哈希）、UX-02（用户级
 *     无哈希/内部名）、NFR-SEC-07 精神（防线不依赖调用方自觉）
 *   - 任务契约 tasks/foundation/DIAG-T07.json acceptance 1~3 逐条自证：
 *     acceptance 1→DtLog1/DtLog2/DtLog3/DtLog4/DtLog5 组；acceptance 2→
 *     DtPdiag7 组；acceptance 3→DtPex8 组
 *   - 用例名后缀＝矩阵行编号（DT-LOG-x），与 ird-test-report.json 的 trace
 *     追溯字段呼应（AGENTS.md §4.2 验证留痕）
 *
 * 故障注入形态：ILogFileOps 接缝注入（§10 DT-LOG-3"ILogSinkOps 注入失败
 * ——经 ILogFileOps 接缝，testkit D-10 形态"）——本套件自带 FaultFileOps/
 * GatedFileOps 注入实现（返回 false 与抛异常两轨＋阻塞闸门），不链 testkit
 * （T-1——与既有套件同款自持纪律）。真实文件 I/O 用 FileLogFileOps＋临时
 * 目录（每个用例独立目录，互不串扰）。
 *
 * 线程约束：DtLog2 为多线程交错写（DT-LOG-2 观测面）；其余用例单线程。
 * ManualClock 仅日志线程读取（时间戳/节流/flush 窗在渲染时打点——实现口
 * 径），推进只发生在无并发窗口。
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <condition_variable>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/diagnostics/Catalog.hpp>
#include <sdurws/ird/diagnostics/DiagCodes.hpp>
#include <sdurws/ird/diagnostics/Errors.hpp>
#include <sdurws/ird/diagnostics/Factory.hpp>
#include <sdurws/ird/diagnostics/Logging.hpp>

namespace fs = std::filesystem;

namespace {

using namespace sdurws::ird::diagnostics;
namespace core = sdurws::ird::core;
using core::AttemptId;
using core::BranchId;
using core::ObjectId;
using core::ProjectId;
using core::RevisionId;
using core::RunId;
using core::TaskIdentity;

// ---------------------------------------------------------------------
// 夹具辅助（自持不共享——与既有套件同风格）
// ---------------------------------------------------------------------

/// 断言抛出 DiagnosticsError 且错误码为 expected（错误码面钉住——§9.0）。
template <class Fn>
void expectThrowsWithCode(Fn&& fn, DiagnosticsErrorCode expected, const char* what)
{
    try {
        fn();
        FAIL() << what << "：未抛出异常（应拒绝并抛 DiagnosticsError）";
    } catch (const DiagnosticsError& e) {
        EXPECT_EQ(e.code(), expected) << what << "：错误码面不符（what()=" << e.what() << "）";
    } catch (...) {
        FAIL() << what << "：抛出了非 DiagnosticsError 异常（单元唯一异常类型纪律）";
    }
}

/// 确定性测试时钟（§4.2 IClock 注释——ManualClock 兼容形态；仅日志线程
/// 读取——实现口径：时间戳/节流/flush 窗在渲染时打点）。
class ManualClock final : public IClock {
public:
    std::chrono::system_clock::time_point nowUtc() const override { return m_now; }
    void advance(std::chrono::milliseconds delta) { m_now += delta; }

private:
    std::chrono::system_clock::time_point m_now{std::chrono::seconds{2000000}};
};

/// 每用例独立临时目录（RAII 清理——真实文件 I/O 用例的隔离面）。
class TempDirGuard {
public:
    TempDirGuard()
    {
        std::error_code ec;
        m_dir = fs::temp_directory_path(ec) / ("ird-logtest-" + std::to_string(++s_seq));
        fs::create_directories(m_dir, ec);
    }
    ~TempDirGuard()
    {
        std::error_code ec;
        fs::remove_all(m_dir, ec);  // 尽力而为清理——失败不遮蔽用例结论
    }
    const fs::path& path() const { return m_dir; }

private:
    inline static int s_seq = 0;
    fs::path m_dir;
};

/// 合法五元组（TASK-03——五字段全 isValid；replay 前置）。
TaskIdentity makeTaskIdentity(std::uint64_t runSalt)
{
    TaskIdentity task;
    task.project = ProjectId::generate();
    task.branch = BranchId::generate();
    task.revision = RevisionId::generate();
    task.run = RunId::generate();
    task.attempt = AttemptId{runSalt};  // 尝试序号以盐值区分各场景流
    return task;
}

// ---------------------------------------------------------------------
// 日志行解析（观测面——格式契约见 src/Logging.cpp formatLine 注释：
// "<seq> <epochMs> <U|D> <LEVEL> <channel>[ k=v]* msg=<转义消息>"）
// ---------------------------------------------------------------------

struct ParsedLine {
    std::uint64_t seq = 0;
    std::string tier;                       ///< "U"/"D"
    std::string level;                      ///< "ERROR"/"WARN"/"INFO"/"DEBUG"/"TRACE"
    std::string channel;
    std::map<std::string, std::string> kv;  ///< code/ids/tag/wk 等修饰字段
    std::string msg;                        ///< 行尾消息（已按文件原文——含转义形态）
};

/// 解析一行；失败返回 false（调用方断言——"行完整"观测的机械承载）。
bool parseLogLine(const std::string& line, ParsedLine& out)
{
    std::istringstream in(line);
    std::string seqTok, tsTok;
    if (!std::getline(in, seqTok, ' ') || !std::getline(in, tsTok, ' ')) { return false; }
    out.seq = std::stoull(seqTok);
    if (!std::getline(in, out.tier, ' ') || !std::getline(in, out.level, ' ')
        || !std::getline(in, out.channel, ' ')) {
        return false;
    }
    out.kv.clear();
    std::string rest;
    if (!std::getline(in, rest)) { return false; }
    // rest ＝ "k=v k=v ... msg=消息"——消息恒在行尾（msg= 之后到行尾）。
    const std::size_t msgPos = rest.find("msg=");
    if (msgPos == std::string::npos) { return false; }
    out.msg = rest.substr(msgPos + 4);
    std::istringstream head(rest.substr(0, msgPos));
    std::string tok;
    while (head >> tok) {
        const std::size_t eq = tok.find('=');
        if (eq == std::string::npos) { return false; }
        out.kv[tok.substr(0, eq)] = tok.substr(eq + 1);
    }
    return true;
}

/// 读全文（读失败显性失败——不留"读不到＝零命中"的假阳性通道）。
std::string readTextFile(const fs::path& file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        ADD_FAILURE() << "无法读取文件: " << file.string();
        std::error_code ec;
        for (const auto& e : fs::directory_iterator(file.parent_path(), ec)) {
            ADD_FAILURE() << "  dir entry: " << e.path().string();
        }
        return {};
    }
    return std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/// 逐行解析文件（跳过空行；任一行解析失败即用例失败——行完整性断言）。
std::vector<ParsedLine> parseAllLines(const std::string& text, const char* what)
{
    std::vector<ParsedLine> lines;
    std::istringstream in(text);
    std::string raw;
    while (std::getline(in, raw)) {
        if (raw.empty()) { continue; }
        ParsedLine p;
        if (!parseLogLine(raw, p)) {
            ADD_FAILURE() << what << "：存在不可解析行（行完整性破坏）: " << raw;
            continue;
        }
        lines.push_back(std::move(p));
    }
    return lines;
}

/// 从 ids 字段取指定键值（ids 承载形态 "k:v;k:v"——§7.2 关联 ID 结构化编码）。
std::string idsValue(const std::string& ids, const std::string& key)
{
    std::string out;
    std::istringstream in(ids);
    std::string part;
    while (std::getline(in, part, ';')) {
        const std::size_t colon = part.find(':');
        if (colon != std::string::npos && part.substr(0, colon) == key) {
            out = part.substr(colon + 1);
        }
    }
    return out;
}

// ---------------------------------------------------------------------
// ILogFileOps 注入实现（testkit D-10 形态的自持替身——T-1 不链 testkit）
// ---------------------------------------------------------------------

/**
 * @brief 故障注入文件操作（DT-LOG-3）：按路径配置三态——正常写盘/追加返回
 *        false/追加抛异常；flush 可独立配置失败。命中计数供观测点断言。
 */
class FaultFileOps final : public ILogFileOps {
public:
    enum class AppendMode { Ok, FailReturn, FailThrow };
    enum class FlushMode { Ok, FailReturn };

    void failAppend(const fs::path& p, AppendMode m) { m_append[p.string()] = m; }
    void failFlush(const fs::path& p, FlushMode m) { m_flush[p.string()] = m; }
    std::size_t appendHits(const fs::path& p) const { return countAt(m_appendHits, p); }
    std::size_t failHits(const fs::path& p) const { return countAt(m_failHits, p); }

    bool appendLine(const fs::path& file, std::string_view line) override
    {
        ++m_appendHits[file.string()];
        const auto it = m_append.find(file.string());
        const AppendMode mode = it == m_append.end() ? AppendMode::Ok : it->second;
        if (mode == AppendMode::FailThrow) {
            ++m_failHits[file.string()];
            throw std::runtime_error("injected disk fault (append)");  // 抛异常轨——管线须消化
        }
        if (mode == AppendMode::FailReturn) {
            ++m_failHits[file.string()];
            return false;
        }
        std::ofstream out(file, std::ios::binary | std::ios::app);
        if (!out) { return false; }
        out.write(line.data(), static_cast<std::streamsize>(line.size()));
        out.put('\n');
        return static_cast<bool>(out);
    }

    bool flushFile(const fs::path& file) override
    {
        const auto it = m_flush.find(file.string());
        return it != m_flush.end() && it->second == FlushMode::FailReturn ? false : true;
    }

    bool rotateFile(const fs::path& file, std::uint32_t keepFiles) override
    {
        // 简化世代移位（故障注入场景不断言轮转正确性——轮转用 FileLogFileOps 断言）。
        std::error_code ec;
        const fs::path first = file.string() + ".1";
        if (fs::exists(file, ec)) { fs::rename(file, first, ec); }
        (void)keepFiles;
        return !ec;
    }

    std::uint64_t fileSize(const fs::path& file) override
    {
        std::error_code ec;
        const auto s = fs::file_size(file, ec);
        return ec ? 0u : static_cast<std::uint64_t>(s);
    }

private:
    template <class Map>
    static std::size_t countAt(const Map& m, const fs::path& p)
    {
        const auto it = m.find(p.string());
        return it == m.end() ? 0u : static_cast<std::size_t>(it->second);
    }
    std::map<std::string, AppendMode> m_append;
    std::map<std::string, FlushMode> m_flush;
    std::map<std::string, std::uint64_t> m_appendHits;
    std::map<std::string, std::uint64_t> m_failHits;
};

/**
 * @brief 闸门文件操作（队列溢出用例）：close() 后首个 appendLine 阻塞直至
 *        open()（有界保护 10 s——防用例悬挂）；其余行为同正常写盘。
 */
class GatedFileOps final : public ILogFileOps {
public:
    void close() { m_open = false; }
    void open()
    {
        {
            std::lock_guard<std::mutex> lock(m_mtx);
            m_open = true;
        }
        m_cv.notify_all();
    }

    /// 等待日志线程进入首个 appendLine（close 之后调用）——消除"主线程灌入
    /// 时线程尚未阻塞"的竞态，保证溢出场景确定性复现。
    void waitEntered()
    {
        std::unique_lock<std::mutex> lock(m_mtx);
        m_cv.wait(lock, [this] { return m_entered; });
    }

    bool appendLine(const fs::path& file, std::string_view line) override
    {
        {
            std::unique_lock<std::mutex> lock(m_mtx);
            m_entered = true;  // 已进入写盘（闩锁）——通知 waitEntered
            m_cv.notify_all();
            // 有界等待：测试异常路径下 10 s 强制放行，防悬挂（安全属性用墙钟
            // ——与产品 flush 同款口径）。
            m_cv.wait_for(lock, std::chrono::seconds{10}, [this] { return m_open; });
        }
        std::ofstream out(file, std::ios::binary | std::ios::app);
        if (!out) { return false; }
        out.write(line.data(), static_cast<std::streamsize>(line.size()));
        out.put('\n');
        return static_cast<bool>(out);
    }

    bool flushFile(const fs::path&) override { return true; }
    bool rotateFile(const fs::path&, std::uint32_t) override { return true; }
    std::uint64_t fileSize(const fs::path& file) override
    {
        std::error_code ec;
        const auto s = fs::file_size(file, ec);
        return ec ? 0u : static_cast<std::uint64_t>(s);
    }

private:
    std::mutex m_mtx;
    std::condition_variable m_cv;
    bool m_open = true;
    bool m_entered = false;  ///< 日志线程已进入首个 appendLine（闩锁信号）
};

/// 组装配置（公共缺省——目录/文件名固定，其余按用例覆盖）。
LogSinkConfig baseConfig(const fs::path& dir)
{
    LogSinkConfig c;
    c.directory = dir;
    return c;
}

/// 便捷构造一条 Dev 记录（不落配置——本地 log 路径时间戳由管线打点）。
LogRecord makeDevRecord(LogLevel level, std::string channel, std::string message)
{
    LogRecord r;
    r.tier = LogTier::Dev;
    r.level = level;
    r.channel = std::move(channel);
    r.message = std::move(message);
    return r;
}

}  // namespace

// =====================================================================
// DT-LOG-1 两级分流（NFR-REL-05/UX-02——acceptance 1）
// =====================================================================

/**
 * DT-LOG-1：写 Error/Dev/Info 三类记录——Tier-U 文件无 Dev 行/无调用栈/
 * 无内部哈希（模式扫描）；Tier-D 全量（Tier-U ⊆ Tier-D，§7.1）且保留原文。
 */
TEST(DiagLogging, DtLog1_TwoTierSplitKeepsUserFileCleanOfDevLinesAndInternalPatterns)
{
    TempDirGuard dir;
    FileLogFileOps ops;
    ManualClock clock;
    LoggingPipeline logger(clock, ops);
    auto cfg = baseConfig(dir.path());
    cfg.sampleEveryDebug = 1;  // 采样不干预本用例（N=1＝Debug 全保留——§7.4 首条即每条）
    logger.configure(cfg);

    // 用户级 Error：消息故意携带内存地址（0x…）、64 位十六进制内容哈希、
    // 栈帧文本——Tier-U 侧必须被模式过滤（防线不依赖调用方自觉，§9.6）。
    LogRecord userErr;
    userErr.tier = LogTier::User;
    userErr.level = LogLevel::Error;
    userErr.channel = "proj/store";
    userErr.code = std::string{"PRJ-LOCK-HELD"};
    userErr.threadTag = "main";
    userErr.message = "ptr=0x7ffd8a2b10 hash=" + std::string(64, 'a')
                    + " trace at engine/core.cpp:42 lock held";
    logger.log(userErr);

    // 用户级 Info：普通消息——两侧文件都应出现。
    LogRecord userInfo = userErr;
    userInfo.level = LogLevel::Info;
    userInfo.code.reset();
    userInfo.message = "store opened normally";
    logger.log(userInfo);

    // 开发级 Debug：只允许出现在 Tier-D 文件（§7.3③ Tier-U 只收 E/W/I）。
    logger.log(makeDevRecord(LogLevel::Debug, "diag/catalog", "dev-only detail row"));

    ASSERT_TRUE(logger.flush(kLogDefaultFlushDeadline));

    const std::string userText = readTextFile(dir.path() / "user-diagnostics.log");
    const std::string devText = readTextFile(dir.path() / "dev-diagnostics.log");
    ASSERT_FALSE(userText.empty());
    ASSERT_FALSE(devText.empty());

    // ---- Tier-U 文件：无 Dev 行（NFR-REL-05——观察点"Tier-U 文件无 Dev 行"）。
    const auto userLines = parseAllLines(userText, "user file");
    ASSERT_EQ(userLines.size(), 2u) << "用户文件应恰好两条（Error＋Info，无 Dev 镜像反侧）";
    for (const auto& l : userLines) {
        EXPECT_EQ(l.tier, "U") << "用户文件不得出现 D 行（§7.3③）";
    }

    // ---- Tier-U 文件：无内部哈希/无调用栈/无地址（模式扫描——观察点）。
    EXPECT_EQ(userText.find("0x7ffd8a2b10"), std::string::npos) << "内存地址必须被过滤";
    EXPECT_EQ(userText.find(std::string(64, 'a')), std::string::npos) << "内容哈希必须被过滤";
    EXPECT_EQ(userText.find("engine/core.cpp:42"), std::string::npos) << "栈帧必须被过滤";
    EXPECT_NE(userText.find("[ADDR]"), std::string::npos) << "地址占位符应在场";
    EXPECT_NE(userText.find("[HASH]"), std::string::npos) << "哈希占位符应在场";
    EXPECT_NE(userText.find("at [FRAME]"), std::string::npos) << "栈帧占位符应在场";
    // 用户行保留稳定码与级别（§7.1 Tier-U 内容＝稳定码＋脱敏参数——非空白行）。
    EXPECT_NE(userText.find("code=PRJ-LOCK-HELD"), std::string::npos);
    EXPECT_NE(userText.find("ERROR"), std::string::npos);

    // ---- Tier-D 文件：全量（三条都有——含 Dev 行与未过滤原文）。
    const auto devLines = parseAllLines(devText, "dev file");
    ASSERT_EQ(devLines.size(), 3u) << "开发文件＝全量镜像（Tier-U ⊆ Tier-D）＋Dev 行";
    std::size_t devRows = 0;
    bool rawAddressKept = false;
    for (const auto& l : devLines) {
        if (l.tier == "D") { ++devRows; }
        if (l.msg.find("0x7ffd8a2b10") != std::string::npos) { rawAddressKept = true; }
    }
    EXPECT_EQ(devRows, 1u) << "开发文件恰一条 Dev 行";
    EXPECT_TRUE(rawAddressKept) << "Tier-D 保留全量技术细节（未做 Tier-U 模式过滤——§7.1）";
    EXPECT_NE(devText.find("dev-only detail row"), std::string::npos);
}

// =====================================================================
// DT-LOG-2 关联 ID 与全局稳定序（§7.2/§7.4——acceptance 1）
// =====================================================================

/**
 * DT-LOG-2：四线程并发写＋回读——行完整（逐行可解析）、同线程 FIFO（各线
 * 程行内序号严格递增）、全局稳定序（seq 全文件单调递增）。
 */
TEST(DiagLogging, DtLog2_ConcurrentWritesKeepSameThreadFifoAndGlobalStableOrder)
{
    TempDirGuard dir;
    FileLogFileOps ops;
    ManualClock clock;
    LoggingPipeline logger(clock, ops);
    logger.configure(baseConfig(dir.path()));

    constexpr int kThreads = 4;
    constexpr int kPerThread = 150;
    {
        std::vector<std::thread> workers;
        for (int t = 0; t < kThreads; ++t) {
            workers.emplace_back([&logger, t, kPerThread] {
                for (int i = 0; i < kPerThread; ++i) {
                    LogRecord r;
                    r.tier = LogTier::User;
                    r.level = LogLevel::Info;
                    r.channel = "stress/chan";
                    r.threadTag = "t" + std::to_string(t);           // 关联字段：线程标
                    r.correlationIds.entryId = static_cast<DiagEntryId>(i + 1);  // 关联字段：条目序
                    r.message = "thread" + std::to_string(t) + "-line" + std::to_string(i);
                    logger.log(r);  // 返回即入队确认——不等待落盘（§9.6）
                }
            });
        }
        for (auto& w : workers) { w.join(); }
    }
    ASSERT_TRUE(logger.flush(kLogDefaultFlushDeadline));

    const auto lines = parseAllLines(readTextFile(dir.path() / "dev-diagnostics.log"),
                                     "dev file");
    ASSERT_EQ(lines.size(), static_cast<std::size_t>(kThreads * kPerThread))
        << "并发行不得丢失/重复（行完整性）";

    // 全局稳定序：seq 全文件严格递增（§7.4——序号单调，观察点）。
    for (std::size_t i = 1; i < lines.size(); ++i) {
        ASSERT_GT(lines[i].seq, lines[i - 1].seq) << "第 " << i << " 行序号未单调递增";
    }

    // 同线程 FIFO：各线程的 entryId 子序列严格为 1..kPerThread（入队序＝
    // 处理序——队列 FIFO 保证，§7.4"同线程 FIFO"）。
    std::vector<int> next(kThreads, 1);
    for (const auto& l : lines) {
        const int t = l.kv.count("tag") == 1 ? std::atoi(l.kv.at("tag").c_str() + 1) : -1;
        ASSERT_GE(t, 0) << "tag 字段缺失（关联字段观测点）";
        ASSERT_LT(t, kThreads) << "未知线程标";
        const auto idsIt = l.kv.find("ids");
        ASSERT_NE(idsIt, l.kv.end()) << "ids 关联块缺失（§7.2 结构化承载）";
        EXPECT_EQ(idsValue(idsIt->second, "entry"), std::to_string(next[t]))
            << "线程 " << t << " 行序乱（同线程 FIFO 破坏）";
        ++next[t];
    }
    for (int t = 0; t < kThreads; ++t) {
        EXPECT_EQ(next[t], kPerThread + 1) << "线程 " << t << " 行数不符";
    }
}

// =====================================================================
// DT-LOG-3 日志写失败（§7.6/§9.6——acceptance 1；ILogFileOps 接缝注入）
// =====================================================================

/**
 * DT-LOG-3：用户文件追加抛异常＋冲刷失败注入——log 立即返回（不阻塞、无
 * 异常逃逸）、开发文件出现 DIAG-LOG-WRITE-FAILED Dev 行、flush 有界失败
 * （返回 false——§9.6"超时放弃＋DIAG-LOG-WRITE-FAILED"的冲刷失败半区）。
 */
TEST(DiagLogging, DtLog3_WriteFailureInjectedStaysNonBlockingAndBounded)
{
    TempDirGuard dir;
    FaultFileOps ops;
    const fs::path userFile = dir.path() / "user-diagnostics.log";
    ops.failAppend(userFile, FaultFileOps::AppendMode::FailThrow);  // 抛异常轨
    ops.failFlush(userFile, FaultFileOps::FlushMode::FailReturn);
    ManualClock clock;
    LoggingPipeline logger(clock, ops);
    logger.configure(baseConfig(dir.path()));

    // log：返回即入队确认——注入在日志线程侧消化（无异常逃逸＝执行到达此处）。
    EXPECT_NO_THROW({
        for (int i = 0; i < 3; ++i) {
            LogRecord r;
            r.tier = LogTier::User;
            r.level = LogLevel::Warning;
            r.channel = "proj/store";
            r.message = "line that will fail on user sink " + std::to_string(i);
            logger.log(r);
        }
    }) << "写失败注入不得向调用方抛出（§9.6 不阻塞调用方、不抛）";

    // flush 有界失败：用户文件冲刷被注入为失败——返回 false（观察点"flush
    // 有界失败"），且调用在时限内返回（不永久等待）。
    const bool flushed = logger.flush(kLogDefaultFlushDeadline);
    EXPECT_FALSE(flushed) << "冲刷失败必须如实报告 false（尽力而为降态）";

    // 开发文件出现 DIAG-LOG-WRITE-FAILED Dev 行（观察点"注入命中记录"）。
    // 追加失败行（file=user）与 flush 失败行（无 file= 前缀）同码不同消息——
    // 分别断言：至少一条指明失守文件面，其余聚合行不回退为静默。
    const auto lines = parseAllLines(readTextFile(dir.path() / "dev-diagnostics.log"),
                                     "dev file");
    std::size_t failureRows = 0;
    std::size_t userAppendFailureRows = 0;
    for (const auto& l : lines) {
        const auto code = l.kv.find("code");
        if (code != l.kv.end() && code->second == "DIAG-LOG-WRITE-FAILED") {
            ++failureRows;
            EXPECT_EQ(l.tier, "D") << "写失败自省码为 Dev 级（§4.6 DIAG 行）";
            if (l.msg.find("file=user") != std::string::npos) { ++userAppendFailureRows; }
        }
    }
    EXPECT_GE(failureRows, 1u) << "至少一条 DIAG-LOG-WRITE-FAILED 开发行";
    EXPECT_GE(userAppendFailureRows, 1u) << "失败行应指明失守文件面（file=user）";
    EXPECT_GE(ops.failHits(userFile), 1u) << "注入必须真实命中（failHits 计数）";
    EXPECT_EQ(ops.appendHits(userFile), ops.failHits(userFile))
        << "用户文件不应有任何成功写入";
    EXPECT_FALSE(fs::exists(userFile)) << "用户文件不应被创建（全部追加失败）";
}

// =====================================================================
// DT-LOG-4 日志禁用不影响正式诊断（§7.8——acceptance 1）
// =====================================================================

/**
 * DT-LOG-4：禁用日志 sink 后走完整诊断路径——目录条目照常产出（工厂＋目录
 * 与日志完全独立）、reportDev 路由进禁用的 logger 不产生文件（§7.8：日志
 * 非权威——诊断/宿主持久化不依赖日志面）。
 */
TEST(DiagLogging, DtLog4_DisabledLoggingLeavesFormalDiagnosticsIntact)
{
    TempDirGuard dir;
    FileLogFileOps ops;
    ManualClock clock;
    LoggingPipeline logger(clock, ops);  // logger 同时充任 IDevLogSink（§9.7 尾注接缝）
    auto cfg = baseConfig(dir.path());
    cfg.enabled = false;  // DT-LOG-4 前置：禁用日志 sink
    logger.configure(cfg);

    // 正式诊断路径（与日志无编译/运行依赖——§8.10 协作仅经调用方编排）。
    StableCodeRegistry registry;
    registerBuiltinCodes(registry);
    DiagnosticsFactory factory(registry, clock);
    DiagCatalog catalog;

    DiagnosticsSinkImpl sink(factory, catalog, logger, "ui", "test.logpanel");
    EXPECT_NO_THROW(sink.reportDev("diag/test", "dev route into disabled logger"))
        << "reportDev 路由进禁用日志必须安全降级（§7.8）";

    core::DiagnosticRecord rec = core::DiagnosticRecord::make(
        "PRJ-STORE-CORRUPT", ObjectId::generate(), std::optional<std::string>{},
        std::optional<std::string>{}, "store integrity check failed", "checksum mismatch",
        "inspect store file");
    DiagContext ctx;
    ctx.sourceUnit = "project";
    ctx.sourceInterface = "test.store";
    DiagEntryId id = 0;
    EXPECT_NO_THROW({
        const DiagnosticEntry entry = factory.create(rec, ctx);
        id = entry.entryId;
        catalog.append(entry);
    }) << "日志禁用不得影响正式诊断创建/记录（§7.8 观察点）";
    EXPECT_NE(id, 0u);

    const auto projection = catalog.snapshot(DiagQuery{});
    ASSERT_EQ(projection.size(), 1u) << "目录条目在（观察点：目录诊断不受影响）";
    EXPECT_EQ(projection[0].code, "PRJ-STORE-CORRUPT");

    EXPECT_TRUE(logger.flush(kLogDefaultFlushDeadline)) << "禁用态排空应即时成功";
    EXPECT_FALSE(fs::exists(dir.path() / "user-diagnostics.log"))
        << "禁用后用户文件不得产生";
    EXPECT_FALSE(fs::exists(dir.path() / "dev-diagnostics.log"))
        << "禁用后开发文件不得产生";
}

// =====================================================================
// DT-LOG-5 worker 日志回传合并（§7.5——acceptance 1；P-EX-8 载荷语义）
// =====================================================================

/**
 * DT-LOG-5：模拟 WorkerLogBatch 乱序/断裂——乱序批按序号恢复全局稳定序
 * （b0→b1→b2 顺序落盘）、陈旧重复批幂等丢弃＋计数行、断裂（final 收尾越过
 * 缺口）补 EX-CHANNEL-PROTOCOL-ERROR Dev 行（补条存在，观察点）。
 */
TEST(DiagLogging, DtLog5_WorkerBatchReplayRestoresOrderAndFillsChannelGap)
{
    TempDirGuard dir;
    FileLogFileOps ops;
    ManualClock clock;
    LoggingPipeline logger(clock, ops);
    logger.configure(baseConfig(dir.path()));

    // ---- 场景 A（worker 7）：乱序投递 2→1→0，重放后必须按 b0→b1→b2 落盘。
    const TaskIdentity taskA = makeTaskIdentity(1);
    auto batchOf = [&](std::uint64_t seq, bool finalBatch, const std::string& tag,
                       const TaskIdentity& task) {
        WorkerLogBatch b;
        b.workerId = tag == "w9" ? 9u : 7u;
        b.task = task;
        b.seq = seq;
        b.final = finalBatch;
        LogRecord r;
        r.tier = LogTier::User;
        r.level = LogLevel::Warning;
        r.channel = "worker/exec";
        r.message = tag + "-b" + std::to_string(seq);
        b.records.push_back(std::move(r));
        return b;
    };
    EXPECT_NO_THROW(logger.replayWorkerBatch(batchOf(2, false, "w7", taskA)));
    EXPECT_NO_THROW(logger.replayWorkerBatch(batchOf(1, false, "w7", taskA)));
    EXPECT_NO_THROW(logger.replayWorkerBatch(batchOf(0, false, "w7", taskA)));
    EXPECT_NO_THROW(logger.replayWorkerBatch(batchOf(0, false, "w7", taskA)))
        << "重复批应被幂等接受（陈旧丢弃——不产生重复行）";
    ASSERT_TRUE(logger.flush(kLogDefaultFlushDeadline));

    // ---- 场景 B（worker 9）：投递 0，再投 final=2（缺口 1 永不抵达）——
    //      断裂补条（§7.5"批次丢失→主进程补一条 Dev 诊断"）。
    const TaskIdentity taskB = makeTaskIdentity(2);
    EXPECT_NO_THROW(logger.replayWorkerBatch(batchOf(0, false, "w9", taskB)));
    EXPECT_NO_THROW(logger.replayWorkerBatch(batchOf(2, true, "w9", taskB)));
    ASSERT_TRUE(logger.flush(kLogDefaultFlushDeadline));

    const std::string devText = readTextFile(dir.path() / "dev-diagnostics.log");
    const auto lines = parseAllLines(devText, "dev file");

    // ---- 断言 A：乱序恢复全局稳定序（观察点"合并后序号连续性"）。
    const auto indexOf = [&](const std::string& needle) {
        const std::size_t pos = devText.find(needle);
        EXPECT_NE(pos, std::string::npos) << needle << " 应在场";
        return pos;
    };
    EXPECT_LT(indexOf("msg=w7-b0"), indexOf("msg=w7-b1"));
    EXPECT_LT(indexOf("msg=w7-b1"), indexOf("msg=w7-b2")) << "乱序投递须按序号恢复稳定序";
    // 陈旧重复不产生重复行；丢弃事实以计数行登记（§7.5 幂等重放口径）。
    EXPECT_EQ(devText.find("msg=w7-b0"), devText.rfind("msg=w7-b0")) << "重复批不得落两行";
    EXPECT_NE(devText.find("陈旧批次 1"), std::string::npos)
        << "陈旧批丢弃应有 Dev 计数行（不静默吞）";
    // 关联保留：worker 行带 wk=7 与注入的任务五元组（§7.5"保留 workerId"）。
    for (const auto& l : lines) {
        if (l.msg.rfind("w7-", 0) == 0) {
            const std::string wk = l.kv.count("wk") == 1 ? l.kv.at("wk") : std::string{};
            EXPECT_EQ(wk, "7") << "workerId 补注缺失";
            EXPECT_NE(l.kv.find("ids"), l.kv.end()) << "任务关联（ids=task:…）应注入";
        }
    }

    // ---- 断言 B：断裂补条存在且位置正确（b0 → 补条 → b2）。
    std::size_t gapRows = 0;
    std::size_t gapPos = 0;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const auto code = lines[i].kv.find("code");
        if (code != lines[i].kv.end() && code->second == "EX-CHANNEL-PROTOCOL-ERROR") {
            ++gapRows;
            gapPos = i;
            EXPECT_EQ(lines[i].tier, "D") << "补条为 Dev 诊断（§7.5）";
            EXPECT_EQ(lines[i].level, "WARN") << "补条 Warning 级（不触及采样——§7.4）";
            EXPECT_NE(lines[i].msg.find("序号 1"), std::string::npos)
                << "补条应指明缺口序号（期望批次 1 未到达）";
            const std::string wk = lines[i].kv.count("wk") == 1 ? lines[i].kv.at("wk")
                                                                : std::string{};
            EXPECT_EQ(wk, "9") << "补条锚定断裂 worker";
        }
    }
    EXPECT_EQ(gapRows, 1u) << "恰一条断裂补条（场景 A 无缺口不得误报）";
    ASSERT_GE(lines.size(), 2u);
    bool sawB0 = false;
    bool sawB2 = false;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].msg.rfind("w9-b0", 0) == 0) { sawB0 = true; }
        if (i == gapPos) {
            EXPECT_TRUE(sawB0) << "补条应在 b0 之后";
            EXPECT_FALSE(sawB2) << "补条应在 b2 之前（断裂插入流内）";
        }
        if (lines[i].msg.rfind("w9-b2", 0) == 0) { sawB2 = true; }
    }
    EXPECT_TRUE(sawB0 && sawB2) << "场景 B 两批记录都应在场";
}

// =====================================================================
// 节流与采样（§7.4——管线产品面自证；DT-LOG-1~5 的机制承载）
// =====================================================================

/**
 * §7.4 行为面：采样（仅 Debug/Trace——首条＋每 N 条＋末条；Error 不采样）＋
 * 节流（同 (code+channel) 首条全量＋窗关闭汇总含 occurrences）。
 */
TEST(DiagLogging, DtLogThrottleAndSamplingWindowBehavior)
{
    TempDirGuard dir;
    FileLogFileOps ops;
    ManualClock clock;
    std::string devText;
    {
        // 管线收尾（析构）兑现"末条"与窗关闭承诺——尾条/汇总在关闭路径落盘，
        // 故 logger 置于内层作用域，析构后再读文件。
        LoggingPipeline logger(clock, ops);
        auto cfg = baseConfig(dir.path());
        cfg.sampleEveryDebug = 3;  // 采样 N=3：第 1/3/6 条保留，其余进尾条槽
        cfg.throttleWindowSeconds = 10;
        logger.configure(cfg);

        // ---- 采样半区：7 条同通道 Debug（无 code——不参与节流）。
        for (int i = 1; i <= 7; ++i) {
            logger.log(makeDevRecord(LogLevel::Debug, "sample/chan",
                                     "debug-row-" + std::to_string(i)));
        }
        // ---- 节流半区：同 (code+channel) 的 5 条 Error 同刻（窗内）＋窗后 1 条。
        for (int i = 0; i < 5; ++i) {
            LogRecord r;
            r.tier = LogTier::User;
            r.level = LogLevel::Error;
            r.channel = "throttle/chan";
            r.code = std::string{"PRJ-LOCK-HELD"};
            r.message = "lock wait " + std::to_string(i);
            logger.log(r);
        }
        // 时间戳在渲染时打点（§7.2"写入时间"）——先排空确保五条入窗记录
        // 全部以推进前时间开窗，时钟推进才对窗后记录生效（消除竞态）。
        ASSERT_TRUE(logger.flush(kLogDefaultFlushDeadline));
        clock.advance(std::chrono::seconds{11});  // 越过节流窗（§7.4 默认 10 s）
        {
            LogRecord r;
            r.tier = LogTier::User;
            r.level = LogLevel::Error;
            r.channel = "throttle/chan";
            r.code = std::string{"PRJ-LOCK-HELD"};
            r.message = "lock wait after window";
            logger.log(r);
        }
        ASSERT_TRUE(logger.flush(kLogDefaultFlushDeadline));
    }  // 析构：排空采样尾条（§7.4"末条"）

    devText = readTextFile(dir.path() / "dev-diagnostics.log");
    const auto lines = parseAllLines(devText, "dev file");

    // 采样：恰 4 行——第 1/3/6 条＋第 7 条（尾条，带标注）。
    std::vector<std::string> sampled;
    for (const auto& l : lines) {
        if (l.channel == "sample/chan") {
            EXPECT_EQ(l.level, "DEBUG");
            sampled.push_back(l.msg);
        }
    }
    ASSERT_EQ(sampled.size(), 4u) << "N=3 采样应保留第 1/3/6 条＋末条";
    EXPECT_NE(sampled[0].find("debug-row-1"), std::string::npos) << "首条保留";
    EXPECT_NE(sampled[1].find("debug-row-3"), std::string::npos) << "第 N 条保留";
    EXPECT_NE(sampled[2].find("debug-row-6"), std::string::npos) << "每 N 条保留";
    EXPECT_NE(sampled[3].find("[sampled-tail] debug-row-7"), std::string::npos)
        << "末条以尾条标注落盘";

    // 节流：首条全量＋窗关闭汇总（occurrences=4）＋窗后首条全量；Error 不采样。
    // 汇总行直接渲染（与窗后首条同线程串行、先于其落盘）——按内容定位断言。
    std::vector<const ParsedLine*> throttled;
    const ParsedLine* firstRow = nullptr;
    const ParsedLine* summaryRow = nullptr;
    const ParsedLine* afterWindowRow = nullptr;
    for (const auto& l : lines) {
        if (l.channel != "throttle/chan") { continue; }
        throttled.push_back(&l);
        if (l.msg.find("lock wait 0") != std::string::npos) { firstRow = &l; }
        if (l.msg.find("occurrences=4") != std::string::npos) { summaryRow = &l; }
        if (l.msg.find("lock wait after window") != std::string::npos) { afterWindowRow = &l; }
    }
    ASSERT_EQ(throttled.size(), 3u) << "节流后恰 3 行（首条＋汇总＋窗后首条）";
    ASSERT_NE(firstRow, nullptr) << "首条全量应在场";
    ASSERT_NE(summaryRow, nullptr) << "窗关闭汇总含压制计数（§7.4）";
    ASSERT_NE(afterWindowRow, nullptr) << "窗后记录重新全量";
    EXPECT_LT(firstRow->seq, afterWindowRow->seq) << "首条先于窗后记录（同线程 FIFO）";
    for (const auto* l : throttled) {
        const std::string code = l->kv.count("code") == 1 ? l->kv.at("code") : std::string{};
        EXPECT_EQ(code, "PRJ-LOCK-HELD") << "汇总行保留稳定码（日志不以文本代替稳定码）";
    }
}

// =====================================================================
// 队列溢出（§9.6"队列满→丢弃最旧 Debug/Trace＋Dev 计数诊断"）
// =====================================================================

/**
 * §9.6 队列策略：容量 4＋闸门阻塞写盘——灌入 1 Error＋8 Trace，必有最旧
 * Debug/Trace 被丢弃；排空后出现 Dev 计数行（不静默），最新行保全。
 */
TEST(DiagLogging, DtLogOverflowDropsOldestDebugTraceAndCountsDevDiagnostic)
{
    TempDirGuard dir;
    GatedFileOps ops;
    ManualClock clock;
    LoggingPipeline logger(clock, ops);
    auto cfg = baseConfig(dir.path());
    cfg.queueCapacity = 4;
    cfg.sampleEveryDebug = 1;  // N=1＝Debug/Trace 全保留——采样不干预本用例（§7.4 首条即每条）
    logger.configure(cfg);

    ops.close();  // 闸门关闭——日志线程阻塞在首行写盘上，队列得以蓄满
    LogRecord err;
    err.tier = LogTier::User;
    err.level = LogLevel::Error;
    err.channel = "ovf/chan";
    err.message = "overflow-error-keeper";
    logger.log(err);  // Error 不可丢弃（§9.6 只丢 Debug/Trace）
    ops.waitEntered();  // 确认线程已阻塞在写盘——此后灌入必然蓄满队列
    for (int i = 1; i <= 8; ++i) {
        logger.log(makeDevRecord(LogLevel::Trace, "ovf/chan",
                                 "overflow-trace-" + std::to_string(i)));
    }
    ops.open();  // 放行——排空后丢弃计数行出现
    ASSERT_TRUE(logger.flush(kLogDefaultFlushDeadline));

    const std::string devText = readTextFile(dir.path() / "dev-diagnostics.log");
    if (devText.find("[log-overflow]") == std::string::npos) {
        ADD_FAILURE() << "dev 文件全文（调试）:\n" << devText;
    }
    EXPECT_NE(devText.find("[log-overflow]"), std::string::npos)
        << "丢弃事实必须有 Dev 计数行（§9.6 不静默）";
    EXPECT_NE(devText.find("msg=overflow-error-keeper"), std::string::npos)
        << "Error 行必须保全";
    EXPECT_NE(devText.find("msg=overflow-trace-8"), std::string::npos)
        << "最新行保全（丢最旧）";
    EXPECT_EQ(devText.find("msg=overflow-trace-1"), std::string::npos)
        << "最旧 Debug/Trace 被丢弃";
}

// =====================================================================
// 轮转（§7.6/§7.8——P-DIAG-7 工程默认的执行面；真实文件世代移位）
// =====================================================================

/**
 * 轮转世代移位：rotateFiles=2、阈值 100 B——超阈即轮转；保留数硬约束
 * （不产生 .2）、活动＋一代档各持最新行（§7.8"轮转覆盖"＝旧档不可达即
 * 消亡——只移位覆盖，无归档承诺）。
 */
TEST(DiagLogging, DtLogRotationGenerationsShiftWithRealFiles)
{
    TempDirGuard dir;
    FileLogFileOps ops;
    ManualClock clock;
    LoggingPipeline logger(clock, ops);
    auto cfg = baseConfig(dir.path());
    cfg.rotateFiles = 2;
    cfg.rotateMaxBytes = 100;
    logger.configure(cfg);

    const std::string fat(64, 'x');  // 每行 ~100 B——逐行触发轮转
    for (int i = 0; i < 6; ++i) {
        logger.log(makeDevRecord(LogLevel::Info, "rot/chan", "rot-" + std::to_string(i) + fat));
    }
    ASSERT_TRUE(logger.flush(kLogDefaultFlushDeadline));

    const fs::path base = dir.path() / "dev-diagnostics.log";
    ASSERT_TRUE(fs::exists(base)) << "活动文件应在场";
    const fs::path gen1 = dir.path() / "dev-diagnostics.log.1";
    ASSERT_TRUE(fs::exists(gen1)) << "第一代档应在场";
    EXPECT_FALSE(fs::exists(dir.path() / "dev-diagnostics.log.2"))
        << "保留文件数＝2——第二代档不得存在";

    // 覆盖语义：每次写盘前轮转→活动文件恒持最新一行、一代档持次新一行；
    // 更早世代被移位覆盖（§7.8"轮转覆盖"——旧档消亡是设计语义，不是丢行）。
    // 行以 '\n' 结尾——恰一行＝恰一个换行符。
    const std::string baseText = readTextFile(base);
    const std::string gen1Text = readTextFile(gen1);
    const auto newlineCount = [](const std::string& t) {
        return static_cast<std::size_t>(std::count(t.begin(), t.end(), '\n'));
    };
    EXPECT_NE(baseText.find("rot-5"), std::string::npos) << "活动文件持最新行";
    EXPECT_NE(gen1Text.find("rot-4"), std::string::npos) << "一代档持次新行";
    EXPECT_EQ(newlineCount(baseText), 1u) << "活动文件恰一行（逐行轮转）";
    EXPECT_EQ(newlineCount(gen1Text), 1u) << "一代档恰一行";
}

// =====================================================================
// 截断（§7.2"≤4 KiB，超出截断并标注"——含 UTF-8 边界安全）
// =====================================================================

TEST(DiagLogging, DtLogTruncationMarks4KiBUtf8Safe)
{
    TempDirGuard dir;
    FileLogFileOps ops;
    ManualClock clock;
    LoggingPipeline logger(clock, ops);
    logger.configure(baseConfig(dir.path()));

    // 半角超长：5000 字节 → 截断至 ≤4 KiB＋标注。
    logger.log(makeDevRecord(LogLevel::Info, "trunc/chan", std::string(5000, 'a')));
    // 多字节超长：2100 个三字节"汉"（6300 B）——切点必须落在字符边界。
    std::string cjk;
    for (int i = 0; i < 2100; ++i) { cjk += "\xe6\xb1\x89"; }
    logger.log(makeDevRecord(LogLevel::Info, "trunc/chan", cjk));
    ASSERT_TRUE(logger.flush(kLogDefaultFlushDeadline));

    const auto lines = parseAllLines(readTextFile(dir.path() / "dev-diagnostics.log"),
                                     "dev file");
    ASSERT_EQ(lines.size(), 2u);
    constexpr std::size_t markLen = 7;  // "[trunc]"
    EXPECT_EQ(lines[0].msg.size(), kLogMessageMaxBytes + markLen) << "截断＝4 KiB＋标注";
    EXPECT_EQ(lines[0].msg.substr(lines[0].msg.size() - markLen), "[trunc]");
    // 多字节切点回退至字符边界：截净长为 3 的倍数且未超 4 KiB。
    const std::size_t cjkNet = lines[1].msg.size() - markLen;
    EXPECT_LE(cjkNet, kLogMessageMaxBytes) << "截净长不超 4 KiB";
    EXPECT_EQ(cjkNet % 3, 0u) << "三字节字符边界对齐（无半个码位）";
    EXPECT_GE(cjkNet + 3, kLogMessageMaxBytes) << "回退至多 2 字节（贴近上限）";
    EXPECT_EQ(lines[1].msg.substr(lines[1].msg.size() - markLen), "[trunc]");
}

// =====================================================================
// 入口违约面（§7.2/§7.3③/§9.6——AGENTS.md 调用方错误 fail-fast）
// =====================================================================

TEST(DiagLogging, DtLogCallerViolationsFailFastWithUsage)
{
    TempDirGuard dir;
    FileLogFileOps ops;
    ManualClock clock;
    LoggingPipeline logger(clock, ops);

    auto badRecord = [](std::function<void(LogRecord&)> mutate) {
        LogRecord r = makeDevRecord(LogLevel::Warning, "ok/chan", "fine message");
        mutate(r);
        return r;
    };
    expectThrowsWithCode([&] { logger.log(badRecord([](LogRecord& r) { r.channel.clear(); })); },
                         DiagnosticsErrorCode::Usage, "空通道");
    expectThrowsWithCode([&] { logger.log(badRecord([](LogRecord& r) { r.channel.assign(49, 'c'); })); },
                         DiagnosticsErrorCode::Usage, "通道超 48 字符（§7.2）");
    expectThrowsWithCode([&] { logger.log(badRecord([](LogRecord& r) { r.message.clear(); })); },
                         DiagnosticsErrorCode::Usage, "空消息（§7.2 必填）");
    expectThrowsWithCode([&] { logger.log(badRecord([](LogRecord& r) { r.threadTag.assign(33, 't'); })); },
                         DiagnosticsErrorCode::Usage, "线程标超 32 字符（同 DiagnosticEntry）");
    expectThrowsWithCode([&] {
        LogRecord r = makeDevRecord(LogLevel::Warning, "ok/chan", "fine message");
        r.tier = LogTier::User;
        r.level = LogLevel::Debug;  // Tier-U 只收 E/W/I（§7.3③）
        logger.log(r);
    }, DiagnosticsErrorCode::Usage, "Tier-U 携带 Debug 级");

    // 配置不变量（configure 前置——Usage fail-fast）。
    LogSinkConfig badRot = baseConfig(dir.path());
    badRot.rotateFiles = 0;
    expectThrowsWithCode([&] { logger.configure(badRot); },
                         DiagnosticsErrorCode::Usage, "rotateFiles=0");
    LogSinkConfig badSample = baseConfig(dir.path());
    badSample.sampleEveryDebug = 0;
    expectThrowsWithCode([&] { logger.configure(badSample); },
                         DiagnosticsErrorCode::Usage, "sampleEveryDebug=0");
}

// =====================================================================
// P-DIAG-7 处置（acceptance 2——工程默认可配，不作需求语义）
// =====================================================================

/**
 * P-DIAG-7：轮转（5×2 MiB/文件）与 flush 窗口（N=256/T=2 s）默认值钉住——
 * 证明"默认存在且可被整值替换"（工程默认的两面：缺省可用＋可配置）；WP-23
 * 性能验收时校准，修改默认值不构成需求偏差（契约 acceptance 2 原文）。
 */
TEST(DiagLogging, DtPdiag7_DefaultsAreConfigurableEngineeringDefaults)
{
    // 默认值＝单元卡 §14.3 登记值（P-DIAG-7：上游未定义参数——实现工程默认）。
    const LogSinkConfig d;
    EXPECT_EQ(d.rotateFiles, 5u);
    EXPECT_EQ(d.rotateMaxBytes, 2u * 1024u * 1024u);
    EXPECT_EQ(d.flushEveryLines, 256u);
    EXPECT_EQ(d.flushInterval, std::chrono::milliseconds{2000});
    EXPECT_EQ(d.sampleEveryDebug, 100u);
    EXPECT_EQ(d.throttleWindowSeconds, 10u);
    EXPECT_TRUE(d.enabled) << "总开关缺省开——禁用是显式选择（DT-LOG-4 面）";

    // 可配置面：整值替换被接受并在行为面生效（轮转阈值/文件数即刻生效——
    // 见 DtLogRotationGenerationsShiftWithRealFiles；此处验证配置命令排空后
    // 不改变管线可用性）。
    TempDirGuard dir;
    FileLogFileOps ops;
    ManualClock clock;
    LoggingPipeline logger(clock, ops);
    auto custom = baseConfig(dir.path());
    custom.rotateFiles = 2;
    custom.rotateMaxBytes = 128;
    custom.flushEveryLines = 4;
    custom.flushInterval = std::chrono::milliseconds{50};
    logger.configure(custom);
    logger.log(makeDevRecord(LogLevel::Info, "cfg/chan", "custom config accepted"));
    EXPECT_TRUE(logger.flush(kLogDefaultFlushDeadline));
    EXPECT_NE(readTextFile(dir.path() / "dev-diagnostics.log").find("custom config accepted"),
              std::string::npos);
}

// =====================================================================
// P-EX-8 处置（acceptance 3——只定载荷语义，不私定对端帧格式）
// =====================================================================

/**
 * P-EX-8：Logging 公共头/实现不 include execution 侧头（R-1/R-2——
 * execution→diagnostics 才是登记边）、不定义任何帧类型/通道帧表（§12.2
 * 交接：帧类型归 execution §6）。源码扫描钉住（BuildRedLineTest 同款运行
 * 期扫描机制）。
 */
TEST(DiagLogging, DtPex8_HeaderDefinesPayloadOnlyNoExecutionFrameContract)
{
    const fs::path unitRoot{IRD_DIAGNOSTICS_UNIT_ROOT};
    ASSERT_TRUE(fs::exists(unitRoot)) << "IRD_DIAGNOSTICS_UNIT_ROOT 不存在";
    const fs::path header = unitRoot / "diagnostics" / "include" / "sdurws" / "ird"
                          / "diagnostics" / "Logging.hpp";
    const fs::path impl = unitRoot / "diagnostics" / "src" / "Logging.cpp";
    ASSERT_TRUE(fs::exists(header)) << "Logging.hpp 应存在";
    ASSERT_TRUE(fs::exists(impl)) << "Logging.cpp 应存在";

    const std::string headerText = readTextFile(header);
    const std::string implText = readTextFile(impl);
    // 载荷语义承载在场（WorkerLogBatch 为唯一回传形态——§7.5）。
    EXPECT_NE(headerText.find("struct WorkerLogBatch"), std::string::npos);
    // 对端单元零 include（不私定对端帧格式的前提——对端契约不在本单元视野）。
    EXPECT_EQ(headerText.find("#include <sdurws/ird/execution"), std::string::npos)
        << "不得 include execution 侧头（R-1/R-2/P-EX-8）";
    EXPECT_EQ(implText.find("#include <sdurws/ird/execution"), std::string::npos)
        << "实现不得 include execution 侧头";
    // 无帧类型词汇（帧类型/通道帧表归 execution §6——§12.2 交接）。
    EXPECT_EQ(headerText.find("FrameType"), std::string::npos) << "不得私定帧类型";
    EXPECT_EQ(headerText.find("FrameHeader"), std::string::npos);
    EXPECT_EQ(headerText.find("ChannelFrame"), std::string::npos);
}
