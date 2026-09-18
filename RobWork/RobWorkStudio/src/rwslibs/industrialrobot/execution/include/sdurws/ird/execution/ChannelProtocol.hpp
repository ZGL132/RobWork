/**
 * @file   ChannelProtocol.hpp
 * @brief  主进程↔worker 通道协议（IRDCHN/1）——帧布局、消息类型枚举、
 *         协议版本、五元组身份头、seq 序控（重排/去重/断裂）、分帧续传
 *         与各消息载荷编解码（§6.5；主/worker 共用——worker 目标与测试
 *         均消费，§3.1 组成行）。
 *
 * 设计依据：
 *   - units/execution.md §3.1（ChannelProtocol.hpp 组成行：帧布局、消息
 *     类型枚举、协议版本、五元组身份头——主/worker 共用）、§6.5（通道
 *     协议 IPC：帧布局/消息版本/输入传输/结果分片/心跳/乱序与重复/完整性）、
 *     §6.4（握手 Hello/HelloAck、退出码约定集的通道面）、§3.5（worker
 *     目录布局——通道帧读写）
 *   - ARCHITECTURE.md §4.1（"通道消息全部携带任务身份五元组"——D-15 五元组
 *     全程随行）、§4.5（worker 自报身份不可信——以登记为准）
 *   - 需求 NFR-PERF-03（评估不整体装载——大载荷分帧续传/结果分批流式）、
 *     CON-05（内容寻址——DispatchRequest 物化身份核对）、TASK-03（五元组）
 *   - 任务契约 tasks/foundation/EX-T06.json acceptance 2（EX-CHN-1：IRDCHN/1
 *     帧自定界＋seq 序控——乱序/重复按 seq 重排去重，断裂→协议错误→
 *     Failed＋EX-CHANNEL-PROTOCOL-ERROR）/3（ResultBatch 分批流式）
 *
 * 背景说明（为什么协议在本单元自持而不引第三方 IPC 库）：
 *   离线单机场景（NFR-DEP-03）无对抗者——§6.5 完整性行明文"不对本地管道
 *   做加密"；载体是每 worker 一对单向字节管道（win32/ChannelPair），管道
 *   字节流无消息边界，因此帧协议自定界（长度前缀）是唯一的消息边界来源。
 *   本头是该边界的契约面：编码/解码/序控/重组全部纯逻辑（零 Win32 依赖），
 *   管道 I/O 在 src/win32/ChannelPair（私有实现——R-2 纪律，私有头不出
 *   include/）；worker exe 经链接本库消费同一实现，杜绝主/worker 各写一份
 *   协议的漂移风险（NFR-MNT-04 单点实现）。
 *
 * 字节序与编码纪律：全部多字节整数为小端（LE）；文本一律 UTF-8；帧头
 *   payloadLen 使帧自定界（读到即知帧尾），读侧不依赖任何帧间状态。
 *
 * 线程安全：本头全部函数与类均为纯逻辑/纯值——无共享可变状态；FrameSequencer
 *   与 MessageReassembler 实例非线程安全（每通道读线程各持一个，§6.2）。
 */

#ifndef SDURWS_IRD_EXECUTION_CHANNELPROTOCOL_HPP
#define SDURWS_IRD_EXECUTION_CHANNELPROTOCOL_HPP

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/core/Digest.hpp>    // ContentDigester/ContentIdentity（物化身份核对）
#include <sdurws/ird/core/Identity.hpp>  // TaskIdentity 五元组（帧头身份块）
#include <sdurws/ird/execution/Errors.hpp>     // ExecutionError（发送侧拼装违约 fail-fast）
#include <sdurws/ird/execution/TaskTypes.hpp>  // ProgressReport（进度载荷承载）

namespace sdurws::ird::execution {

// =====================================================================
// 协议常量（§6.5 帧布局——IRDCHN/1）
// =====================================================================

/// 协议主版本（IRDCHN/1 的 "1"）。主版本失配→握手拒绝（ChannelProtocolError，
/// 开发诊断）；次版本追加可选 flags 兼容——阶段 A 无次版本语义，非 1 即拒。
inline constexpr std::uint16_t kChannelProtocolVersion = 1;

/// 帧魔数（"IRDCHN1"——7 字节 ASCII；版本号编入魔数是格式自描述的一部分）。
inline constexpr std::string_view kFrameMagic = "IRDCHN1";

/// 帧头固定段长度：magic(7)＋protoVersion(u16)＋msgType(u8)＋flags(u8)＋
/// seq(u64)＋payloadLen(u32)＝23 字节；其后是身份块与载荷。
inline constexpr std::size_t kFrameFixedHeaderSize = 23;

/// 身份块长度（§6.5"定长域拼接"）：project/branch/revision/run 四个 Id128
/// 规范文本各恰 36 字节（tag+32hex），attempt 规范文本 "att-<十进制>" 右侧
/// NUL 补齐至 24 字节（u64 十进制最长 20 位＋tag 4 字节≤24——定长域成立），
/// 共 4×36＋24＝168 字节。选择"规范文本而非原始字节"是 §6.5 原文（五个
/// 规范文本）——文本形态可直接人工比对日志，代价是每帧 168 字节固定开销
/// （本地管道，无带宽压力）。
inline constexpr std::size_t kIdentityBlockSize = 4 * 36 + 24;

/// 单帧载荷长度上限（64 MiB——分帧续传的片段上限；本地管道场景该上限只
/// 防"长度域被破坏后把内存读穿"的解码事故，非性能参数）。
inline constexpr std::uint32_t kMaxFramePayload = 64u * 1024u * 1024u;

/// 重组后完整消息长度上限（256 MiB——物化快照的防御性上限；超限按协议
/// 错误拒绝，不尝试分配）。
inline constexpr std::uint64_t kMaxMessageBytes = 256ull * 1024ull * 1024ull;

/// flags 位定义：0x01＝本帧之后还有同消息的后续分片（分帧续传——§6.5
/// "大载荷分帧续传（payloadLen 分片＋重组序号）"）。其余位保留（收侧
/// 忽略未知位——次版本 flags 兼容语义，§6.5 消息版本行）。
inline constexpr std::uint8_t kFlagFragmentMore = 0x01;

/// 消息类型（§6.5 MsgType 枚举原文——值序冻结，只可表尾追加）。
enum class ChannelMsgType : std::uint8_t {
    Hello,               ///< worker→主：握手请求（协议版本＋manifest 摘要）
    HelloAck,            ///< 主→worker：握手应答（接受＋心跳间隔 / 拒绝原因）
    DispatchRequest,     ///< 主→worker：派发请求（物化字节分帧续传，§3.3）
    DispatchAccept,      ///< worker→主：派发受理确认（身份核对通过）
    Progress,            ///< worker→主：进度帧（§6.1——经节流后入任务记录）
    Heartbeat,           ///< worker→主：心跳（默认 5 s 间隔，实现参数 D-06）
    ResultBatch,         ///< worker→主：结果批次（分批流式——NFR-PERF-03）
    CheckpointBatch,     ///< worker→主：检查点批次（§8.1——持久化归 EX-T08）
    FinalOutput,         ///< worker→主：最终产出（EvaluationOutput canonical＋绑定身份）
    CancelRequest,       ///< 主→worker：协作取消请求（§7.1 步 2）
    CancelAck,           ///< worker→主：取消确认（§7.1 步 2"CancelAck→Canceled"）
    PauseRequest,        ///< 主→worker：暂停请求（§7.2——检查点边界达成）
    PauseAck,            ///< worker→主：暂停确认（检查点已确认）
    ResumeFromCheckpoint,///< 主→worker：从检查点继续（§7.3——新尝试派发）
    ErrorReport,         ///< worker→主：错误/开发诊断报告（§6.4 边界规则：诊断一律经通道回传）
    Shutdown,            ///< 主→worker：请求正常退出（池空闲回收——§6.4 空闲超时）
};

// =====================================================================
// 帧值类型与身份块编解码
// =====================================================================

/**
 * @brief 一帧通道消息（§6.5 Frame 布局的值形态）。
 *
 * 每帧必携有效五元组（ARCH §4.1"通道消息全部携带任务身份五元组"——包括
 *   握手帧：worker 是为某个任务绑定而启动的，身份在启动时已知）。seq 为
 *   发送方单调计数（每方向独立计数——命令流与数据流各从 1 起）。
 */
struct ChannelFrame {
    ChannelMsgType type = ChannelMsgType::Hello;  ///< 消息类型
    std::uint8_t flags = 0;                       ///< 标志位（kFlagFragmentMore 等；未知位收侧忽略）
    std::uint64_t seq = 0;                        ///< 发送方内单调序号（≥1；0 为保留值不出现于线路上）
    core::TaskIdentity identity;                  ///< 任务身份五元组（必须 isValid——帧级校验项）
    std::vector<std::uint8_t> payload;            ///< 载荷（分片帧＝分片头＋分片字节；见 MessageReassembler）
};

/// 身份块编码（五元组→168 字节定长域；identity 必须 isValid——违约抛
/// ExecutionError(InvalidState)，调用方拼装非法帧属契约违约）。
void encodeIdentityBlock(const core::TaskIdentity& identity,
                         std::array<std::uint8_t, kIdentityBlockSize>& out);

/// 身份块解码（168 字节→五元组；任一字段解析失败返回 nullopt——帧级
/// 校验失败按协议错误处置，不抛：对端数据问题走结构化路径）。
std::optional<core::TaskIdentity> decodeIdentityBlock(
    const std::uint8_t* bytes, std::size_t size);

// =====================================================================
// 帧编解码与流装配（字节流↔帧——自定界的两端）
// =====================================================================

/// 帧编码（完整线路上的一帧字节；seq==0 或 identity 非法抛
/// ExecutionError(InvalidState)——发送侧拼装违约 fail-fast）。
std::vector<std::uint8_t> encodeFrame(const ChannelFrame& frame);

/**
 * @brief 流式帧装配器（读侧设施：字节流分片喂入、吐出完整帧）。
 *
 * 背景：管道读返回的字节块与帧边界无关（字节流无消息边界，§6.6）——
 *   装配器在内部缓冲上做增量解析：凑齐帧头→校验 magic/版本/长度→
 *   凑齐身份块与载荷→吐帧。任何一步校验失败＝帧非法（协议错误面：
 *   EX-CHANNEL-PROTOCOL-ERROR 的来源之一）。
 */
class FrameAssembler {
public:
    /// 解析结论（feed 的返回语义）。
    enum class Status { Ok, ProtocolError };

    /// 单帧字节总长上限（帧头＋身份块＋载荷——防御性上限同 kMaxFramePayload）。
    static constexpr std::size_t kMaxFrameBytes =
        kFrameFixedHeaderSize + kIdentityBlockSize + kMaxFramePayload;

    /// 喂入一段字节流，返回其间凑出的完整帧（按到达序）。协议错误后装配器
    /// 进入失败态（后续 feed 恒返回 ProtocolError 且不再吐帧——错误通道
    /// 已坏，恢复手段是任务失败，§6.5"断裂→通道错误→尝试 Failed"）。
    Status feed(const std::uint8_t* data, std::size_t size, std::vector<ChannelFrame>& out);

    /// 是否已处于协议错误态。
    bool failed() const noexcept { return m_failed; }

private:
    /// 未凑齐一帧的剩余字节（增量解析——vector 保证头解析的连续读；
    /// m_offset 为已消费前缀，过大时向首压缩避免无界增长）。
    std::vector<std::uint8_t> m_buffer;
    std::size_t m_offset = 0;
    bool m_failed = false;              ///< 协议错误闩（见 feed 注）
};

// =====================================================================
// seq 序控（§6.5"乱序与重复"行——EX-CHN-1 的判定本体）
// =====================================================================

/**
 * @brief 接收侧 seq 重排/去重/断裂判定（每数据通道一个实例）。
 *
 * 语义（§6.5 原文逐条）：
 *   - seq 单调：首帧的 seq 即基线（nextExpected）；此后按 seq 连续交付；
 *   - 乱序→按 seq 重排窗口（受限于有界缓冲 Config.windowCapacity——
 *     到达序与 seq 序不一致时先缓冲，缺号补齐后按序吐出）；
 *   - 重复/回退→丢弃＋开发诊断（seq 已交付或已在缓冲——DedupDropped；
 *     调用方据 outcome 出 EX-CHANNEL-PROTOCOL-ERROR 开发诊断）；
 *   - 断裂（缺口超时）→协议错误（缺口打开超过 Config.gapTimeout 仍无
 *     补齐——gapTimedOut()；调用方据以尝试 Failed＋EX-CHANNEL-PROTOCOL-
 *     ERROR）；窗口溢出（缺口过远）同样判协议错误。
 *
 * 时钟：gapTimeout 的度量经注入 ClockFn（生产＝steady_clock，测试注入
 *   ManualClock 虚拟推进——不 sleep，testkit §6.5 纪律）。
 */
class FrameSequencer {
public:
    /// 单帧处理结论（accept 的诊断面——Delivered/Buffered 之外的值都
    /// 是调用方要出开发诊断的异常面）。
    enum class Outcome { Delivered, Buffered, DuplicateDropped, WindowOverflow };

    /// 实现参数（登记单元卡 §15.4：窗口与缺口超时均为实现参数非上游值——
    /// §6.5 只规定"有界缓冲"与"缺口超时"机制，未给数值）。
    struct Config {
        std::size_t windowCapacity = 256;                        ///< 重排窗口帧数上限
        std::chrono::milliseconds gapTimeout{10000};             ///< 缺口超时（断裂判定阈值）
    };

    using ClockFn = std::function<std::chrono::steady_clock::time_point()>;  ///< 注入时钟

    /**
     * @brief 构造。
     *
     * @param config [in] 实现参数（缺省＝上表默认值）
     * @param clock  [in] 时钟函数（空＝steady_clock::now）
     */
    explicit FrameSequencer(Config config = {}, ClockFn clock = nullptr);

    /**
     * @brief 接收一帧，返回因它而变为连续可交付的帧（含它自己；按 seq 序）。
     *
     * @param frame   [in] 待收帧（seq 必须 ≥1——线路保留值，违约视为
     *                WindowOverflow 协议错误面）
     * @param outcome [out] 可选；本帧的处理结论（见 Outcome 注）
     */
    std::vector<ChannelFrame> accept(ChannelFrame&& frame, Outcome* outcome = nullptr);

    /// 缺口超时探测（poll 周期调用；true＝断裂成立——本实例进入协议错误
    /// 态，此后 accept 恒空转；与 WindowOverflow 同一套协议错误处置）。
    bool gapTimedOut();

    /// 是否处于协议错误态（窗口溢出/断裂任一触发后恒 true）。
    bool failed() const noexcept { return m_failed; }

    /// 窗口是否无未决帧（测试/排空断言面）。
    bool idle() const noexcept { return m_failed || m_buffered.empty(); }

    /// 基线序号（下一帧期望值；首帧未到时为 0）。
    std::uint64_t nextExpectedSeq() const noexcept { return m_nextExpected; }

private:
    /// 交付推进（从 nextExpected 起连续弹出缓冲帧；返回弹出集）。
    std::vector<ChannelFrame> drainInOrder();

    Config m_config;        ///< 实现参数
    ClockFn m_clock;        ///< 注入时钟（空＝steady）
    std::uint64_t m_nextExpected = 0;  ///< 基线（首帧 seq；0＝未建立）
    std::map<std::uint64_t, ChannelFrame> m_buffered;  ///< 重排窗口（seq→帧，有序）
    std::chrono::steady_clock::time_point m_gapOpenedAt{};  ///< 缺口打开时刻（断裂计时起点）
    bool m_gapOpen = false; ///< 缺口是否打开（有缓冲且缺号）
    bool m_failed = false;  ///< 协议错误闩
};

// =====================================================================
// 分帧续传（§6.5"输入传输"行——大载荷不整体装载，NFR-PERF-03）
// =====================================================================

/// 分片帧的载荷布局头：totalLen(u64)＋fragIndex(u32)＋fragCount(u32)＝16
/// 字节，其后是本片字节。可分片消息（DispatchRequest/FinalOutput）**恒**
/// 使用分片布局（哪怕一片装下——收侧判定无歧义：这两类消息永远走重组器）。
inline constexpr std::size_t kFragmentHeaderSize = 16;

/// 可分片消息类型判定（收侧重组器据此分流——其余类型一帧即完整消息）。
bool isFragmentableType(ChannelMsgType type) noexcept;

/**
 * @brief 把一条完整消息切成分片帧序列（写侧设施）。
 *
 * @param type       [in] 消息类型（须为可分片类型——否则返回空＝调用方违约）
 * @param identity   [in] 五元组（每片帧头原样携带）
 * @param seqBase    [in,out] 发送序号（输入＝首片 seq，输出＝下一可用 seq；
 *                   每片消耗一个序号——seq 是帧级序控不是消息级）
 * @param message    [in] 完整消息载荷字节
 * @param fragmentSize [in] 单片载荷上限（不含 16 字节分片头；须 ≥1）
 * @param out        [out] 产出的分片帧序列（除末片外 flags 带 kFlagFragmentMore）
 * @return true＝成功；false＝参数违约（类型不可分片/fragmentSize 为 0/
 *         消息超 kMaxMessageBytes——调用方契约违约面，测试断言用）
 */
bool fragmentMessage(ChannelMsgType type, const core::TaskIdentity& identity,
                     std::uint64_t& seqBase, const std::vector<std::uint8_t>& message,
                     std::size_t fragmentSize, std::vector<ChannelFrame>& out);

/**
 * @brief 收侧重组器（把分片帧序列还原为完整消息；每数据通道一个实例）。
 *
 * 纪律：分片帧必须先经 FrameSequencer 按序交付再进入本类（分片续传依赖
 *   帧序——乱序分片在序控层已被重排/丢弃/判断裂）。非可分片类型直接透传。
 *   任何不一致（类型漂移/序号跳越/总长失配/超限）＝协议错误（failed() 闩）。
 */
class MessageReassembler {
public:
    explicit MessageReassembler() = default;

    /**
     * @brief 消费一帧，返回因它而完整的消息帧（payload＝完整消息字节；
     *        非可分片类型原样透传、可分片类型以重组结果返回）。
     *
     * @param frame   [in] 序控后的一帧
     * @param broken  [out] 可选；true＝本帧触发协议错误（重组器进入失败态）
     */
    std::vector<ChannelFrame> accept(ChannelFrame&& frame, bool* broken = nullptr);

    /// 是否处于协议错误态。
    bool failed() const noexcept { return m_failed; }

private:
    /// 重组中的部分消息（每类型至多一条在途——分片帧连续到达，无交错）。
    struct Partial {
        std::uint64_t totalLen = 0;       ///< 声明的完整消息长度
        std::uint32_t nextIndex = 0;      ///< 期望的下一分片序号
        std::uint32_t fragCount = 0;      ///< 声明的分片总数
        std::vector<std::uint8_t> bytes;  ///< 已累积字节
    };

    std::optional<Partial> m_partial;  ///< 在途部分消息（nullopt＝无在途）
    bool m_failed = false;             ///< 协议错误闩
};

// =====================================================================
// 各消息载荷编解码（§6.5 消息集的阶段 A 消费面）
// =====================================================================

/// Hello 载荷：worker→主握手请求（协议版本＋评估器装配 manifest 摘要——
/// §6.4"握手(manifest 摘要+协议版本)"）。
struct HelloPayload {
    std::uint16_t protoVersion = kChannelProtocolVersion;  ///< 协议版本（主版本失配→握手拒绝）
    std::string manifestDigestHex;                         ///< manifest 摘要（64 小写 hex——见 manifestDigestHex）
};
std::vector<std::uint8_t> encodeHello(const HelloPayload& p);
std::optional<HelloPayload> decodeHello(const std::vector<std::uint8_t>& bytes);

/// HelloAck 载荷：主→worker 握手应答。accepted=false 时 worker 应自行
/// 退出（退出码 10——装配拒绝面）；accepted=true 携带心跳间隔（worker
/// 宿主按此间隔发 Heartbeat——两侧同源 HeartbeatPolicy，D-06）。
struct HelloAckPayload {
    bool accepted = false;                   ///< 握手是否通过
    std::uint32_t heartbeatIntervalMs = 0;   ///< 心跳间隔（单位 ms；D-06 默认 5000）
    std::string reason;                      ///< 拒绝原因（开发诊断文本；接受时为空）
};
std::vector<std::uint8_t> encodeHelloAck(const HelloAckPayload& p);
std::optional<HelloAckPayload> decodeHelloAck(const std::vector<std::uint8_t>& bytes);

/// DispatchRequest 载荷：派发物（§3.3 MaterializedDispatch 的线路形态）。
/// 身份核对契约（§6.5"输入传输"行）：worker 重算两份字节的 SHA-256 与
/// 声明身份比对，不等→拒绝执行（ErrorReport＋退出码 11）。
struct DispatchRequestPayload {
    core::ContentIdentity snapshotIdentity;              ///< 物化快照声明身份
    core::ContentIdentity modelIdentity;                 ///< 模型派发物声明身份
    std::vector<std::uint8_t> snapshotBytes;             ///< 物化快照字节（execution 不透明）
    std::vector<std::uint8_t> modelBytes;                ///< 模型派发物字节（execution 不透明）
};
std::vector<std::uint8_t> encodeDispatchRequest(const DispatchRequestPayload& p);
std::optional<DispatchRequestPayload> decodeDispatchRequest(
    const std::vector<std::uint8_t>& bytes);

/// Progress 载荷（§6.1 ProgressReport 的线路形态；解码经
/// ProgressReport::make 校验——非法值＝帧级解码校验失败→协议错误）。
std::vector<std::uint8_t> encodeProgress(const ProgressReport& report);
std::optional<ProgressReport> decodeProgress(const std::vector<std::uint8_t>& bytes);

/// ResultBatch 载荷：批次号＋域载荷片段（execution 视为不透明字节——
/// §6.5"结果分片"行；接纳侧按批次写盘，NFR-PERF-03 分批不整体装载）。
struct ResultBatchPayload {
    std::uint64_t batchIndex = 0;             ///< 批次号（worker 内单调，≥1）
    std::vector<std::uint8_t> bytes;          ///< 域载荷片段（不透明）
};
std::vector<std::uint8_t> encodeResultBatch(const ResultBatchPayload& p);
std::optional<ResultBatchPayload> decodeResultBatch(const std::vector<std::uint8_t>& bytes);

/// CheckpointBatch 载荷：检查点序号＋批次字节（§8.1；持久化编排归
/// EX-T08——EX-T06 只做通道透传，PA-1 不伪造第二持久化路径）。
struct CheckpointBatchPayload {
    std::uint64_t sequence = 0;               ///< 检查点序号（≥1——CheckpointId 纪律）
    std::vector<std::uint8_t> bytes;          ///< 检查点批次字节（不透明）
};
std::vector<std::uint8_t> encodeCheckpointBatch(const CheckpointBatchPayload& p);
std::optional<CheckpointBatchPayload> decodeCheckpointBatch(
    const std::vector<std::uint8_t>& bytes);

/// FinalOutput 载荷：评估产出 canonical 字节＋绑定身份自报（§6.5"结果
/// 分片"行——FinalOutput＝EvaluationOutput canonical 编码＋绑定身份）。
/// 自报身份仅作通道级一致性核对，接纳判定以登记五元组为准（worker 自报
/// 不可信——§6.4 边界规则）。
struct FinalOutputPayload {
    core::ContentIdentity snapshotIdentity;         ///< 绑定自报（快照）
    core::ContentIdentity modelIdentity;            ///< 绑定自报（模型派发物）
    std::vector<std::uint8_t> outputCanon;          ///< EvaluationOutput canonical 字节
};
std::vector<std::uint8_t> encodeFinalOutput(const FinalOutputPayload& p);
std::optional<FinalOutputPayload> decodeFinalOutput(const std::vector<std::uint8_t>& bytes);

/// ErrorReport 载荷：worker 诊断回传通道（§6.4 边界规则"诊断一律经通道
/// ErrorReport 回传"）。dev=true 为开发级自由文本（对应主进程侧
/// reportDev）；dev=false 时 code 为稳定码 token（StableCodeRegistry
/// 已收编面——worker 不私定码值）。
struct ErrorReportPayload {
    bool dev = false;                ///< true＝开发级通道（channel 文本）；false＝稳定码
    std::string codeOrChannel;       ///< 稳定码（如 "EX-CHANNEL-PROTOCOL-ERROR"）或开发通道名
    std::string message;             ///< 明细文本
};
std::vector<std::uint8_t> encodeErrorReport(const ErrorReportPayload& p);
std::optional<ErrorReportPayload> decodeErrorReport(const std::vector<std::uint8_t>& bytes);

// 其余消息类型（DispatchAccept/Heartbeat/CancelRequest/CancelAck/
// PauseRequest/PauseAck/ResumeFromCheckpoint/Shutdown）阶段 A 载荷为空
// （语义由类型本身承载）——不定义编解码对（无消费者不预建，NFR-MNT-04）。

// =====================================================================
// 握手摘要与物化身份helper（主/worker 同源计算——比对才有意义）
// =====================================================================

/**
 * @brief 计算评估器装配 manifest 的握手摘要（§6.4 握手比对值）。
 *
 * 输入＝manifest 的规范文本（"key|version|contractVersion" 行集，装配期
 *   由 L5/装配清单冻结；阶段 A 测试替身用固定文本——两侧同一文本、同一
 *   函数，摘要必然一致；不一致即装配漂移，握手拒绝）。SHA-256 经
 *   core::ContentDigester（CON-05 同源算法，NFR-COR-02 确定性）。
 *
 * @param canonicalManifest [in] manifest 规范文本（非空——空装配无评估
 *                          能力，抛 ExecutionError(InvalidState)）
 * @return 64 字符小写十六进制摘要文本
 */
std::string manifestDigestHex(std::string_view canonicalManifest);

/**
 * @brief 计算一份字节序列的内容身份（cid-<64 hex>——DispatchRequest 身份
 *        核对的参照值生成/重算共用函数，CON-05 内容寻址同源）。
 *
 * @param bytes [in] 物化字节（可为空——空字节有确定摘要，身份核对仍成立）
 * @return 内容身份值
 */
core::ContentIdentity computeContentIdentity(const std::vector<std::uint8_t>& bytes);

/**
 * @brief 物化身份核对（§6.5"worker 侧身份核对"的判定式：重算摘要与声明
 *        身份逐字节相等）。
 *
 * @param bytes    [in] 实收字节
 * @param declared [in] 声明身份（DispatchRequest 载荷携带）
 * @return true＝核对通过；false＝失配（worker 拒绝执行——runtime §9.2
 *         同源纪律，不许"就着错字节继续算"）
 */
bool contentIdentityMatches(const std::vector<std::uint8_t>& bytes,
                            const core::ContentIdentity& declared);

}  // namespace sdurws::ird::execution

#endif  // SDURWS_IRD_EXECUTION_CHANNELPROTOCOL_HPP
