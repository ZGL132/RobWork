/**
 * @file   main.cpp
 * @brief  worker 进程宿主入口（sdurws_ird_execution_worker）——握手→派发
 *         →身份核对→评估器执行→进度/心跳/检查点/结果回传→终结退出
 *         （units/execution.md §6.4 worker 循环、§6.5 通道协议、§3.1
 *         worker/main.cpp 组成行）。
 *
 * 设计依据：
 *   - units/execution.md §6.4（worker 主循环：握手（manifest 摘要＋协议
 *     版本）→读取不可变输入（物化字节重建快照，身份核对）→注册表实例化
 *     评估器→执行（周期 cancellationRequested/reportProgress）→写检查点/
 *     结果片段（经通道回传；worker 自身仅写自身临时目录）→返回状态＋
 *     退出码。边界规则〔冻结〕：worker 不直接修改项目正式目录；不与 UI
 *     交互（无 Qt、无窗口、无控制台输出；诊断一律经通道 ErrorReport）；
 *     结果必须经主进程核对（自报身份不可信）。退出码约定集：0/10/11/12/
 *     20/21，约定集外＝崩溃）、§6.5（IRDCHN/1 帧——seq 单调；DispatchRequest
 *     物化身份核对；心跳默认 5 s〔实现参数 D-06，间隔由 HelloAck 下发〕）、
 *     §6.6（临时目录 %TEMP%\ird-worker-<pid>-<run>-<attempt>——每尝试
 *     独立；尝试终结 best-effort 清理；清理失败→开发诊断）
 *   - §3.4（worker 目标：WIN32 可执行；链接 sdurws_ird_execution＋评估器
 *     装配清单〔阶段 A 仅测试替身〕；正式评估器链接属 L5 装配决策——
 *     登记为装配目标；计算内核零 Qt）
 *   - 需求 NFR-REL-02（崩溃隔离的 worker 侧纪律）、NFR-PERF-03（分批
 *     流式回传——不整体装载）、CON-05（物化身份核对）
 *   - 任务契约 tasks/foundation/EX-T06.json acceptance 1（EX-WKR-1~3 真进程）、
 *     2（EX-CHN-1 帧/seq 纪律的 worker 侧半区）、3（分批回传/临时目录隔离）
 *
 * 背景说明（阶段 A 装配边界——为什么本 exe 内置"测试替身评估器"）：
 *   正式形态下 worker 宿主经评估器注册表实例化业务评估器（装配清单归
 *   L5，§6.4）；阶段 A 无任何业务评估器接入（§12 阶段 A 交付边界），
 *   本 exe 内置一个**脚本驱动的替身评估器**：行为脚本以文本承载于派发
 *   物的"快照字节"内（execution 视派发物为不透明字节——替身评估器解读
 *   它恰如真实评估器解读物化快照），可编排进度/批次/检查点/最终产出/
 *   崩溃/卡死/序号异常等动作，供 EX-WKR-1~3/EX-CHN-1 等真进程用例实证。
 *   替身输出不构成业务算法正确性证明（§11 替身边界声明，R-7）。
 *
 * 线程模型（三线程＋主流程）：
 *   - 主线程：握手→等派发→身份核对→替身评估器脚本→清理→退出；
 *   - 命令读线程：阻塞读命令流→帧装配→帧队列（Cancel/Pause/Shutdown
 *     由主流程在轮询点消费——§7.1 协作点语义）；
 *   - 心跳线程：按 HelloAck 下发的间隔发 Heartbeat（§6.5 D-06；可由
 *     脚本关闭——卡死用例的注入点）。
 *   数据流写面被主线程与心跳线程共享——序号/身份的读取与推进都在写
 *   互斥内完成（帧不可交错，§6.5 单调序）。
 *
 * 退出码约定（§6.4 表——主进程分类依据）：0 正常；10 启动/装配失败；
 *   11 宿主内部错误（含物化身份核对失败）；12 评估器失败（ErrorReport
 *   先行）；20 协作取消确认；21 暂停确认。约定集外值只出现在强杀/崩溃
 *   （由 OS 或脚本注入，非本文件主动路径）。
 */

#include <sdurws/ird/execution/ChannelProtocol.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// CancelIoEx 需要 NT6 目标宏（框架全局定义 0x0501——此处提升，只影响本
// worker TU 的编译面；运行环境为 Win10+，见单元卡 §1.4）。
#if defined(_WIN32_WINNT) && _WIN32_WINNT < 0x0600
#undef _WIN32_WINNT
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include <shellapi.h>  // CommandLineToArgvW（WIN32 子系统的命令行展开）

#include <atomic>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace sdurws::ird::execution;
namespace core = sdurws::ird::core;

namespace {

// =====================================================================
// 阶段 A 装配清单（测试替身评估器——manifest 文本两侧同源，握手比对值）
// =====================================================================

/// 替身评估器的装配 manifest 规范文本（L5 装配清单的阶段 A 占位——契约
/// 测试以同一文本计算期望摘要，两侧摘要一致＝装配未漂移）。
constexpr const char* kStageAManifest = "stage-a-double|1|7";

// =====================================================================
// 数据流写面（worker→主——主线程与心跳线程共享，互斥串行化）
// =====================================================================

struct DataWriter {
    mutable std::mutex mutex;         ///< 帧写互斥（帧不可交错——字节管道）
    HANDLE handle = nullptr;          ///< 数据流写端（继承句柄）
    std::uint64_t seq = 1;            ///< 数据流发送序号（单调——§6.5"seq 单调"）
    core::TaskIdentity identity;      ///< 当前绑定五元组（帧身份块）

    /// 写一段完整帧字节（互斥内；返回 false＝管道断裂）。
    bool writeBytesLocked(const std::vector<std::uint8_t>& bytes)
    {
        DWORD written = 0;
        // 同步句柄整段写（帧尺寸≪管道缓冲；一次 WriteFile 写完整帧——
        // 中途断裂返回 FALSE，调用方按通道失能终结）。
        return ::WriteFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()),
                           &written, nullptr)
               && written == bytes.size();
    }

    /// 发送一帧（序号与身份的读取/推进都在锁内——与心跳线程的竞争面）。
    bool send(ChannelMsgType type, std::vector<std::uint8_t> payload)
    {
        std::vector<std::uint8_t> bytes;
        {
            std::lock_guard<std::mutex> lock(mutex);
            ChannelFrame frame;
            frame.type = type;
            frame.seq = seq++;
            frame.identity = identity;
            frame.payload = std::move(payload);
            bytes = encodeFrame(frame);
        }
        std::lock_guard<std::mutex> lock(mutex);
        return writeBytesLocked(bytes);
    }

    /// 以指定 seq 发送一帧（序号异常注入——EX-CHN-1 用例的动作面；正常
    /// 路径不使用）。
    bool sendWithSeq(ChannelMsgType type, std::uint64_t seqValue,
                     std::vector<std::uint8_t> payload)
    {
        std::vector<std::uint8_t> bytes;
        {
            std::lock_guard<std::mutex> lock(mutex);
            ChannelFrame frame;
            frame.type = type;
            frame.seq = seqValue;
            frame.identity = identity;
            frame.payload = std::move(payload);
            bytes = encodeFrame(frame);
        }
        std::lock_guard<std::mutex> lock(mutex);
        return writeBytesLocked(bytes);
    }

    /**
     * @brief 分片发送一条大消息（FinalOutput canonical 可能大——分帧续传
     *        与主进程侧同纪律，§6.5"大载荷分帧续传"；单片 64 KiB）。
     */
    bool sendFragmented(ChannelMsgType type, const std::vector<std::uint8_t>& message)
    {
        // 分片在锁外拼装（序号段预留＋身份快照各取一次锁）；写面提交在
        // 第二段锁内，全部片写毕才推进 seq——失败重发场景序号不空洞。
        std::vector<ChannelFrame> frames;
        std::uint64_t cursor = 0;
        core::TaskIdentity binding;
        {
            std::lock_guard<std::mutex> lock(mutex);
            cursor = seq;
            binding = identity;
        }
        if (!fragmentMessage(type, binding, cursor, message, 64 * 1024, frames)) {
            return false;
        }
        std::lock_guard<std::mutex> lock(mutex);
        for (const ChannelFrame& frame : frames) {
            const std::vector<std::uint8_t> bytes = encodeFrame(frame);
            if (!writeBytesLocked(bytes)) {
                return false;  // 断裂——seq 不推进（进程即将终结，无重发）
            }
        }
        seq = cursor;
        return true;
    }
};

// =====================================================================
// 命令读线程（命令流——阻塞读→帧装配→帧队列）
// =====================================================================

struct CommandQueue {
    mutable std::mutex mutex;
    std::deque<ChannelFrame> frames;
    bool broken = false;  ///< 命令管道断裂/系统错误（主流程应尽快终结）

    void push(ChannelFrame frame)
    {
        std::lock_guard<std::mutex> lock(mutex);
        frames.push_back(std::move(frame));
    }

    /// 弹出全部就绪帧（主流程轮询点调用——协作点语义，§7.1）。
    std::vector<ChannelFrame> drain()
    {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<ChannelFrame> out(std::make_move_iterator(frames.begin()),
                                      std::make_move_iterator(frames.end()));
        frames.clear();
        return out;
    }

    bool isBroken() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return broken;
    }
};

/// 命令读线程主体（阻塞 ReadFile——主进程不写时挂起；主进程终结/关管道
/// 时以断裂返回）。
void commandReaderLoop(HANDLE cmdRead, CommandQueue& queue)
{
    FrameAssembler assembler;
    std::vector<std::uint8_t> buffer(4 * 1024);
    std::vector<ChannelFrame> frames;
    for (;;) {
        DWORD got = 0;
        if (!::ReadFile(cmdRead, buffer.data(), static_cast<DWORD>(buffer.size()), &got,
                        nullptr)
            || got == 0) {
            std::lock_guard<std::mutex> lock(queue.mutex);
            queue.broken = true;  // 断裂/EOF——主流程终结
            return;
        }
        // 帧装配失败＝命令流错乱（按协议纪律直接终结——退出码 11 面）。
        if (assembler.feed(buffer.data(), got, frames) != FrameAssembler::Status::Ok) {
            std::lock_guard<std::mutex> lock(queue.mutex);
            queue.broken = true;
            return;
        }
        for (ChannelFrame& frame : frames) {
            queue.push(std::move(frame));
        }
        frames.clear();
    }
}

// =====================================================================
// 心跳线程（§6.5 心跳行——间隔由 HelloAck 下发，D-06 默认 5 s）
// =====================================================================

void heartbeatLoop(DataWriter& writer, std::uint32_t intervalMs, std::atomic<bool>& stopped)
{
    for (;;) {
        // 分片睡眠（50 ms 粒度）——停止标志的响应延迟有界（卡死用例在
        // 关闭心跳后仍能尽快进入静默态）。
        for (std::uint32_t waited = 0; waited < intervalMs; waited += 50) {
            if (stopped.load()) {
                return;
            }
            ::Sleep(50);
        }
        if (stopped.load()) {
            return;
        }
        if (!writer.send(ChannelMsgType::Heartbeat, {})) {
            return;  // 管道断裂——心跳使命结束（主流程自行感知终结）
        }
    }
}

// =====================================================================
// 命令行解析（句柄值＋绑定五元组——主/worker 的私有启动约定）
// =====================================================================

struct StartupArgs {
    HANDLE cmdRead = nullptr;     ///< 命令流读端（继承句柄值）
    HANDLE dataWrite = nullptr;   ///< 数据流写端（继承句柄值）
    core::TaskIdentity identity;  ///< 初始绑定五元组（握手帧身份块）
};

std::optional<StartupArgs> parseArgs()
{
    // WinMain 入口无 argc/argv——经 GetCommandLineW＋CommandLineToArgvW
    // 展开（WIN32 子系统可执行的标准取参方式）。
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    if (argv == nullptr) {
        return std::nullopt;
    }
    StartupArgs args;
    bool ok = true;
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--cmd-h" && i + 1 < argc) {
            // 句柄值十六进制文本（ProcessLauncher 的命令行约定）——句柄
            // 值仅在本进程内核句柄表内有义，直接整型还原。
            args.cmdRead = reinterpret_cast<HANDLE>(
                std::wcstoull(argv[++i], nullptr, 16));
        } else if (arg == L"--data-h" && i + 1 < argc) {
            args.dataWrite = reinterpret_cast<HANDLE>(
                std::wcstoull(argv[++i], nullptr, 16));
        } else if (arg == L"--identity" && i + 1 < argc) {
            // 五个规范文本以 '|' 连接——逐段严格解析（篡改/错绑在启动即
            // 失败：退出码 10 装配面）。规范文本全 ASCII——宽窄转换按值直取。
            const std::wstring joinedWide = argv[++i];
            const std::string joined(joinedWide.begin(), joinedWide.end());
            std::vector<std::string> parts;
            std::string cur;
            for (const char c : joined) {
                if (c == '|') {
                    parts.push_back(cur);
                    cur.clear();
                } else {
                    cur.push_back(c);
                }
            }
            parts.push_back(cur);
            if (parts.size() != 5) {
                ok = false;
                break;
            }
            try {
                args.identity.project = core::ProjectId::fromCanonical(parts[0]);
                args.identity.branch = core::BranchId::fromCanonical(parts[1]);
                args.identity.revision = core::RevisionId::fromCanonical(parts[2]);
                args.identity.run = core::RunId::fromCanonical(parts[3]);
                args.identity.attempt = core::AttemptId::fromCanonical(parts[4]);
            } catch (const std::exception&) {
                ok = false;
            }
        }
        // 其余 token（如 --ird-worker 模式标记）忽略——前向兼容。
    }
    ::LocalFree(argv);
    if (!ok || args.cmdRead == nullptr || args.dataWrite == nullptr
        || !args.identity.isValid()) {
        return std::nullopt;
    }
    return args;
}

// =====================================================================
// worker 临时目录（§6.6：%TEMP%\ird-worker-<pid>-<run>-<attempt>，每尝试独立）
// =====================================================================

/// 计算本尝试的临时目录（命名与 §6.6 行一致；pid 为 worker 自身进程——
/// 隔离评估器中间文件，worker 不写项目正式目录的边界规则由此兜底）。
fs::path workerTempDir(const core::TaskIdentity& identity)
{
    std::vector<wchar_t> tempPath(MAX_PATH + 1);
    const DWORD n = ::GetTempPathW(static_cast<DWORD>(tempPath.size()), tempPath.data());
    const fs::path base =
        (n > 0) ? fs::path(std::wstring(tempPath.data(), n)) : fs::path(L".");
    // 目录名成分：ird-worker-<pid>-<run 规范文本>-<attempt 规范文本>
    // （各段先落命名局部量——迭代器对必须取自同一字符串实例）。
    const std::string runCanon = identity.run.toCanonical();
    const std::string attCanon = identity.attempt.toCanonical();
    const std::wstring dirName =
        L"ird-worker-" + std::to_wstring(::GetCurrentProcessId()) + L"-"
        + std::wstring(runCanon.begin(), runCanon.end()) + L"-"
        + std::wstring(attCanon.begin(), attCanon.end());
    return base / dirName;
}

// =====================================================================
// 替身评估器脚本解释（阶段 A——行为脚本承载于派发物的"快照字节"文本）
// =====================================================================

/// 脚本头部（首行；缺省/异构字节按"普通评估"处理——直接正常终结）。
constexpr const char* kScriptHeader = "stage-a-script:v1";

/// 脚本执行上下文（评估器循环的通道面——§6.4 循环的替身形态）。
struct ScriptContext {
    DataWriter& writer;               ///< 数据流写面（进度/批次/检查点/结果）
    CommandQueue& commands;           ///< 命令队列（协作点轮询源）
    std::atomic<bool>& heartbeatOff;  ///< 心跳关闭标志（卡死注入）
    const fs::path& tempDir;          ///< 本尝试临时目录（write-temp 动作）
    core::ContentIdentity dispatchSnapshotId;  ///< 派发声明的快照身份（final 回显）
    core::ContentIdentity dispatchModelId;     ///< 派发声明的模型身份（final 回显）
    bool cancelSeen = false;          ///< 收到 CancelRequest（§7.1 协作点）
    bool pauseSeen = false;           ///< 收到 PauseRequest
    bool shutdownSeen = false;        ///< 收到 Shutdown
};

/// 消费就绪命令帧（协作点：每步动作前后轮询——§7.1"cancellationRequested
/// 在批次/检查点边界观测"的替身形态）。
void pollCommands(ScriptContext& ctx)
{
    for (ChannelFrame& frame : ctx.commands.drain()) {
        switch (frame.type) {
        case ChannelMsgType::CancelRequest:
            ctx.cancelSeen = true;    // 置位后不回退（取消单向——Controller.hpp 纪律）
            break;
        case ChannelMsgType::PauseRequest:
            ctx.pauseSeen = true;
            break;
        case ChannelMsgType::Shutdown:
            ctx.shutdownSeen = true;
            break;
        default:
            break;  // 派发后的其他主→worker 帧阶段 A 无语义（忽略）
        }
    }
}

/// 确定性批次字节（替身产出——"BATCH:<i>" 文本形态；接纳侧视为不透明
/// 字节，只作批次文件完整性观察）。
std::vector<std::uint8_t> batchBytes(std::uint64_t index)
{
    const std::string text = "BATCH:" + std::to_string(index);
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

/**
 * @brief 执行替身评估器脚本（§6.4"执行评估器"的阶段 A 形态）。
 *
 * 脚本动作集（一行一动作；空行与 '#' 注释跳过）：
 *   progress <percent> <phaseToken> <done> <total>  发 Progress 帧
 *   batches <n>                                     发 n 条 ResultBatch
 *   checkpoint <seq>                                发 CheckpointBatch{seq}
 *   final                                           发 FinalOutput（分片）
 *   heartbeat-off                                   停发心跳（卡死注入）
 *   write-temp <name>                               在临时目录写一个文件
 *   crash                                           以 0xC0000005 退出（真崩溃码）
 *   error <code> <msg>                              发稳定码 ErrorReport
 *   deverror <channel> <msg>                        发开发通道 ErrorReport
 *   exit <code>                                     以约定码退出
 *   misorder                                        seq 越前 +1000（窗口溢出注入）
 *   duplicate                                       以最近 seq 重发上一帧（重复注入）
 *   gap                                             序号跳 3 后发心跳（缺口注入）
 *   hang                                            静默挂起（停响应——卡死本体）
 *
 * 返回：结束码（0 正常 / 20 取消 / 21 暂停 / 脚本 exit 值）。
 */
int runScript(const std::string& script, ScriptContext& ctx)
{
    std::size_t pos = 0;
    std::uint64_t lastSeqSent = 0;  // duplicate 注入的参照（最近发送帧序号）
    ChannelMsgType lastType = ChannelMsgType::Heartbeat;
    std::vector<std::uint8_t> lastPayload;

    while (pos < script.size()) {
        // 协作点：每动作前轮询取消/暂停/关闭（§7.1 边界观测语义）。
        pollCommands(ctx);
        if (ctx.cancelSeen) {
            ctx.writer.send(ChannelMsgType::CancelAck, {});
            return 20;  // §6.4 退出码 20——协作取消确认
        }
        if (ctx.pauseSeen) {
            ctx.writer.send(ChannelMsgType::PauseAck, {});
            return 21;  // §6.4 退出码 21——暂停确认
        }
        if (ctx.shutdownSeen || ctx.commands.isBroken()) {
            return 0;   // 主进程请求退出/通道断裂——安静终结
        }

        // 取一行。
        const std::size_t eol = script.find('\n', pos);
        const std::string line =
            script.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
        pos = (eol == std::string::npos) ? script.size() : eol + 1;
        if (line.empty() || line[0] == '#') {
            continue;
        }
        // 分词（空白分隔；phaseToken/message 为单 token——不含引号语法）。
        std::vector<std::string> tokens;
        {
            std::string cur;
            for (const char c : line) {
                if (c == ' ' || c == '\t' || c == '\r') {
                    if (!cur.empty()) {
                        tokens.push_back(std::move(cur));
                        cur.clear();
                    }
                } else {
                    cur.push_back(c);
                }
            }
            if (!cur.empty()) {
                tokens.push_back(std::move(cur));
            }
        }
        if (tokens.empty()) {
            continue;
        }
        const std::string& op = tokens[0];

        if (op == kScriptHeader) {
            continue;  // 头部行（首行）——形态声明
        } else if (op == "progress" && tokens.size() >= 5) {
            const ProgressReport report = ProgressReport::make(
                std::stoi(tokens[1]), tokens[2],
                static_cast<std::uint64_t>(std::stoull(tokens[3])),
                static_cast<std::uint64_t>(std::stoull(tokens[4])));
            const std::vector<std::uint8_t> payload = encodeProgress(report);
            const std::uint64_t seq = ctx.writer.seq;
            if (ctx.writer.send(ChannelMsgType::Progress, payload)) {
                lastSeqSent = seq;  // duplicate 注入的参照（同 seq 同载荷）
                lastType = ChannelMsgType::Progress;
                lastPayload = payload;
            }
        } else if (op == "batches" && tokens.size() >= 2) {
            const int n = std::stoi(tokens[1]);
            for (int i = 1; i <= n; ++i) {
                // 协作点在批间轮询（§6.4"周期 cancellationRequested"）。
                pollCommands(ctx);
                if (ctx.cancelSeen) {
                    ctx.writer.send(ChannelMsgType::CancelAck, {});
                    return 20;
                }
                ResultBatchPayload batch;
                batch.batchIndex = static_cast<std::uint64_t>(i);
                batch.bytes = batchBytes(batch.batchIndex);
                const std::vector<std::uint8_t> payload = encodeResultBatch(batch);
                const std::uint64_t seq = ctx.writer.seq;
                if (ctx.writer.send(ChannelMsgType::ResultBatch, payload)) {
                    lastSeqSent = seq;
                    lastType = ChannelMsgType::ResultBatch;
                    lastPayload = payload;
                }
            }
        } else if (op == "checkpoint" && tokens.size() >= 2) {
            CheckpointBatchPayload cp;
            cp.sequence = static_cast<std::uint64_t>(std::stoull(tokens[1]));
            cp.bytes = batchBytes(cp.sequence);
            const std::vector<std::uint8_t> payload = encodeCheckpointBatch(cp);
            const std::uint64_t seq = ctx.writer.seq;
            if (ctx.writer.send(ChannelMsgType::CheckpointBatch, payload)) {
                lastSeqSent = seq;
                lastType = ChannelMsgType::CheckpointBatch;
                lastPayload = payload;
            }
        } else if (op == "final") {
            // 最终产出：替身 canonical 字节（确定性形态 "FINAL:<run>"——
            // 契约测试的解码替身据其映射合法 EvaluationOutput；绑定身份
            // 回显派发声明——通道级一致性核对的对端）。分片发送（不整体
            // 装载——NFR-PERF-03 出向面）。
            FinalOutputPayload out;
            out.snapshotIdentity = ctx.dispatchSnapshotId;
            out.modelIdentity = ctx.dispatchModelId;
            const std::string canon =
                "FINAL:" + std::string(ctx.writer.identity.run.toCanonical());
            out.outputCanon.assign(canon.begin(), canon.end());
            if (!ctx.writer.sendFragmented(ChannelMsgType::FinalOutput,
                                           encodeFinalOutput(out))) {
                return 0;  // 断裂——终结
            }
        } else if (op == "heartbeat-off") {
            ctx.heartbeatOff.store(true);
        } else if (op == "write-temp" && tokens.size() >= 2) {
            // 临时目录写入（隔离观察面：文件只出现在 worker 自身临时
            // 目录——§6.4 边界规则的替身兑现）。
            const fs::path name(tokens[1].begin(), tokens[1].end());
            const std::string content = "ird-worker-temp-artifact";
            std::FILE* f = std::fopen((ctx.tempDir / name).string().c_str(), "wb");
            if (f != nullptr) {
                std::fwrite(content.data(), 1, content.size(), f);
                std::fclose(f);
            }
        } else if (op == "crash") {
            // 真实异常码退出（0xC0000005 访问违例族——约定集外→主进程
            // 判 WorkerCrashed，NFR-REL-02 崩溃隔离的注入面；ExitProcess
            // 终止全部线程，无清理路径）。
            ::ExitProcess(0xC0000005u);
        } else if (op == "error" && tokens.size() >= 3) {
            ErrorReportPayload report;
            report.dev = false;
            report.codeOrChannel = tokens[1];
            report.message = tokens[2];
            ctx.writer.send(ChannelMsgType::ErrorReport, encodeErrorReport(report));
        } else if (op == "deverror" && tokens.size() >= 3) {
            ErrorReportPayload report;
            report.dev = true;
            report.codeOrChannel = tokens[1];
            report.message = tokens[2];
            ctx.writer.send(ChannelMsgType::ErrorReport, encodeErrorReport(report));
        } else if (op == "exit" && tokens.size() >= 2) {
            return std::stoi(tokens[1]);  // 约定码退出（如 12 评估器失败）
        } else if (op == "misorder") {
            // 序号越前注入（EX-CHN-1）：跳 +1000 发一条 Progress——接收侧
            // 窗口溢出→协议错误（fatal）。
            const ProgressReport report = ProgressReport::make(77, "misorder", 1, 2);
            ctx.writer.sendWithSeq(ChannelMsgType::Progress, ctx.writer.seq + 1000,
                                   encodeProgress(report));
        } else if (op == "duplicate") {
            // 重复注入（EX-CHN-1）：以最近 seq 原样重发——接收侧去重＋
            // 开发诊断（非致命）。
            if (lastSeqSent != 0) {
                ctx.writer.sendWithSeq(lastType, lastSeqSent, lastPayload);
            }
        } else if (op == "gap") {
            // 缺口注入（EX-CHN-1）：序号跳 3 发心跳——接收侧缓冲＋缺口
            // 超时（断裂）判定。
            ctx.writer.sendWithSeq(ChannelMsgType::Heartbeat, ctx.writer.seq + 3, {});
            {
                std::lock_guard<std::mutex> lock(ctx.writer.mutex);
                ctx.writer.seq += 4;
            }
        } else if (op == "hang") {
            // 卡死本体：停响应静默挂起（心跳通常已由 heartbeat-off 关闭
            // ——EX-WKR-3 的注入面；管道断裂时退出）。
            for (;;) {
                if (ctx.commands.isBroken()) {
                    return 0;
                }
                ::Sleep(200);
            }
        }
        // 未知动作：忽略（前向兼容）。
    }
    return 0;  // 脚本自然结束（无 final 亦按正常完成终结）
}

}  // namespace

// =====================================================================
// 宿主主流程（wWinMain——WIN32 子系统入口）
// =====================================================================

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
    // ---- 启动参数（失败＝装配面缺陷：退出码 10）----
    const std::optional<StartupArgs> args = parseArgs();
    if (!args.has_value()) {
        return 10;
    }

    // ---- 数据写面初始化（帧身份块来源＝启动绑定）----
    DataWriter writer;
    writer.handle = args->dataWrite;
    writer.identity = args->identity;

    // ---- 命令读线程启动（阻塞读——主进程终结/关管道时退出）----
    CommandQueue commands;
    std::thread reader(commandReaderLoop, args->cmdRead, std::ref(commands));

    // ---- 心跳停止标志（线程启动前声明——收尾路径统一可先置位）----
    std::atomic<bool> heartbeatOffHint{false};

    // ---- 线程收尾（每个退出路径共用——std::thread 析构于 joinable 态会
    // std::terminate〔fail-fast 0xC0000409——把正常退出污染成崩溃分类〕；
    // 读线程以 CancelIoEx 解阻塞后 join，心跳线程以停止标志后 join）----
    std::thread heartbeat;  // 心跳线程在握手后启动——空实例先占位（收尾统一）
    auto stopThreads = [&]() {
        heartbeatOffHint.store(true);
        ::CancelIoEx(args->cmdRead, nullptr);  // 解阻塞命令读线程（挂起 ReadFile 失败返回）
        if (reader.joinable()) {
            reader.join();
        }
        if (heartbeat.joinable()) {
            heartbeat.join();
        }
    };

    // ---- 握手（§6.4：Hello〔协议版本＋manifest 摘要〕→等 HelloAck）----
    HelloPayload hello;
    hello.protoVersion = kChannelProtocolVersion;
    hello.manifestDigestHex = manifestDigestHex(kStageAManifest);
    const bool helloSent = writer.send(ChannelMsgType::Hello, encodeHello(hello));
    if (!helloSent) {
        stopThreads();
        return 10;  // 通道即断——装配面失败
    }
    // 有界等待 HelloAck（60 s——主进程握手应在毫秒级；超时＝宿主异常）。
    std::optional<HelloAckPayload> ack;
    // 握手等待期到达的非 HelloAck 帧**保留**（主进程在 HelloAck 之后立即
    // 写派发——同一批 drain 里同时出现两类帧是常态而非错乱；丢弃会让
    // DispatchRequest 永久丢失）。
    std::vector<ChannelFrame> preDispatch;
    for (int waitedMs = 0; waitedMs < 60000 && !ack.has_value(); waitedMs += 20) {
        for (ChannelFrame& frame : commands.drain()) {
            if (frame.type == ChannelMsgType::HelloAck) {
                ack = decodeHelloAck(frame.payload);
            } else {
                preDispatch.push_back(std::move(frame));
            }
        }
        if (commands.isBroken()) {
            break;
        }
        ::Sleep(20);
    }
    if (!ack.has_value() || !ack->accepted) {
        // 拒绝（版本/manifest 失配——主进程不派发并就地终结本进程）或
        // 断裂：装配失败退出码 10（§6.4 约定表"启动失败（依赖/装配错误）"）。
        stopThreads();
        return 10;
    }

    // ---- 心跳线程启动（间隔＝HelloAck 下发值——两侧同源 D-06；停止标志
    // ＝heartbeatOffHint，收尾路径统一经 stopThreads join）----
    heartbeat = std::thread(heartbeatLoop, std::ref(writer), ack->heartbeatIntervalMs,
                            std::ref(heartbeatOffHint));

    // ---- 等待派发（DispatchRequest——物化字节分帧续传；重组器横跨多次
    // 轮询——分片可跨到达批，生命周期在等待循环之外。先消化握手期保留的
    // preDispatch 帧〔可能与 HelloAck 同批到达〕，再轮询新帧）----
    std::optional<DispatchRequestPayload> dispatch;
    MessageReassembler reassembler;
    // 帧消化（DispatchRequest 过滤＋换绑＋重组；lambda 捕获全部在途状态
    // ——ack 保留帧与新到帧共用同一条消化路径；返回 true＝重组断裂）。
    auto feedDispatchCandidate = [&](ChannelFrame&& frame) -> bool {
        if (frame.type != ChannelMsgType::DispatchRequest || dispatch.has_value()) {
            return false;  // 派发前的其他命令帧阶段 A 无消费语义；已派发后忽略
        }
        // 池化复用换绑：派发帧头身份即新绑定（此后全部帧携带它——
        // worker 侧不持第二账本，绑定以主进程派发为准）。
        {
            std::lock_guard<std::mutex> lock(writer.mutex);
            writer.identity = frame.identity;
        }
        bool broken = false;
        std::vector<ChannelFrame> messages = reassembler.accept(std::move(frame), &broken);
        for (ChannelFrame& message : messages) {
            dispatch = decodeDispatchRequest(message.payload);
        }
        return broken;
    };
    for (ChannelFrame& frame : preDispatch) {
        (void)feedDispatchCandidate(std::move(frame));
    }
    bool dispatchBroken = false;
    for (int waitedMs = 0; waitedMs < 120000 && !dispatch.has_value() && !dispatchBroken;
         waitedMs += 20) {
        if (commands.isBroken()) {
            break;
        }
        for (ChannelFrame& frame : commands.drain()) {
            if (feedDispatchCandidate(std::move(frame))) {
                dispatchBroken = true;
                break;
            }
        }
        ::Sleep(20);
    }
    if (!dispatch.has_value()) {
        stopThreads();
        return 11;  // 派发缺失/断裂＝宿主内部错误（§6.4 约定表 11 行）
    }

    // ---- 身份核对（§6.5"worker 侧身份核对"：重算摘要与声明比对；不等
    // 拒绝执行——runtime §9.2 同源纪律）----
    if (!contentIdentityMatches(dispatch->snapshotBytes, dispatch->snapshotIdentity)
        || !contentIdentityMatches(dispatch->modelBytes, dispatch->modelIdentity)) {
        ErrorReportPayload report;
        report.dev = false;
        report.codeOrChannel = "EX-CHANNEL-PROTOCOL-ERROR";
        report.message = "dispatch identity mismatch (materialized bytes digest)";
        writer.send(ChannelMsgType::ErrorReport, encodeErrorReport(report));
        stopThreads();
        return 11;  // §6.4 约定表 11 行——宿主内部错误（装载错）
    }

    // ---- 临时目录（§6.6：每尝试独立；worker 唯一的可写落点）----
    const fs::path tempDir = workerTempDir(writer.identity);
    std::error_code ec;
    fs::create_directories(tempDir, ec);  // 失败不致命——write-temp 动作才需要它

    // ---- 派发受理确认（DispatchAccept——观测面）----
    writer.send(ChannelMsgType::DispatchAccept, {});

    // ---- 替身评估器执行（脚本承载于"快照字节"文本）----
    const std::string script(dispatch->snapshotBytes.begin(), dispatch->snapshotBytes.end());
    ScriptContext ctx{writer,
                      commands,
                      heartbeatOffHint,
                      tempDir,
                      dispatch->snapshotIdentity,
                      dispatch->modelIdentity,
                      false,
                      false,
                      false};
    const int scriptExit = runScript(script, ctx);

    // ---- 尝试终结：线程收尾＋临时目录 best-effort 清理（§6.6；失败→
    // 开发诊断，不影响任务终态——EX-ARC-3 边界）----
    stopThreads();
    fs::remove_all(tempDir, ec);
    if (ec) {
        ErrorReportPayload report;
        report.dev = true;
        report.codeOrChannel = "execution/worker-temp";
        report.message = "worker temp dir cleanup failed: " + ec.message();
        writer.send(ChannelMsgType::ErrorReport, encodeErrorReport(report));
    }

    // 退出码＝脚本结束码（0/12/20/21——全部在 §6.4 约定集内；崩溃/卡死
    // 由强杀或 crash 动作的约定集外码承载，不经此路径）。
    return scriptExit;
}
