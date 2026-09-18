/**
 * @file   ChannelProtocol.cpp
 * @brief  通道协议（IRDCHN/1）实现——帧编解码、身份块、流装配、seq 序控、
 *         分帧续传与载荷编解码（契约见 ChannelProtocol.hpp 文件头）。
 *
 * 设计依据：units/execution.md §6.5/§3.1/§6.4；任务契约 EX-T06.json
 *   acceptance 2/3。实现要点全部对齐头注释，本文件注释按 AGENTS §2 讲
 *   "为什么"与逐步算法含义。
 */

#include <sdurws/ird/execution/ChannelProtocol.hpp>

#include <cstring>
#include <stdexcept>

namespace sdurws::ird::execution {

namespace {

// ---- 小端读写 helper（协议纪律：全部多字节整数 LE——见头注释） ----

void putU16(std::vector<std::uint8_t>& out, std::uint16_t v)
{
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
}

void putU32(std::vector<std::uint8_t>& out, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
    }
}

void putU64(std::vector<std::uint8_t>& out, std::uint64_t v)
{
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
    }
}

std::uint16_t getU16(const std::uint8_t* p)
{
    return static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
}

std::uint32_t getU32(const std::uint8_t* p)
{
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
        v |= static_cast<std::uint32_t>(p[i]) << (8 * i);
    }
    return v;
}

std::uint64_t getU64(const std::uint8_t* p)
{
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v |= static_cast<std::uint64_t>(p[i]) << (8 * i);
    }
    return v;
}

void putStr(std::vector<std::uint8_t>& out, std::string_view s)
{
    putU32(out, static_cast<std::uint32_t>(s.size()));  // 长度前缀 u32——文本自定界
    out.insert(out.end(), s.begin(), s.end());
}

/// 带上限的文本读取（长度域越出剩余字节→nullopt——帧级解码校验失败）。
std::optional<std::string> getStr(const std::uint8_t*& p, const std::uint8_t* end)
{
    if (end - p < 4) {
        return std::nullopt;
    }
    const std::uint32_t len = getU32(p);
    p += 4;
    // 上限防御：长度域被破坏时不允许按它分配/拷贝（64 MiB 与单帧载荷上
    // 限一致——文本只出现在单帧载荷内）。
    if (len > kMaxFramePayload || static_cast<std::size_t>(end - p) < len) {
        return std::nullopt;
    }
    std::string s(reinterpret_cast<const char*>(p), len);
    p += len;
    return s;
}

void putBytes(std::vector<std::uint8_t>& out, const std::vector<std::uint8_t>& bytes)
{
    putU32(out, static_cast<std::uint32_t>(bytes.size()));
    out.insert(out.end(), bytes.begin(), bytes.end());
}

std::optional<std::vector<std::uint8_t>> getBytes(const std::uint8_t*& p,
                                                  const std::uint8_t* end)
{
    if (end - p < 4) {
        return std::nullopt;
    }
    const std::uint32_t len = getU32(p);
    p += 4;
    if (len > kMaxFramePayload || static_cast<std::size_t>(end - p) < len) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> v(p, p + len);
    p += len;
    return v;
}

/// Digest256→64 字符小写十六进制（ Admission 同款字符表——摘要的日志形态）。
std::string digestToHex(const core::Digest256& digest)
{
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(digest.size() * 2);
    for (const std::uint8_t b : digest) {
        out.push_back(kHex[b >> 4]);
        out.push_back(kHex[b & 0x0F]);
    }
    return out;
}

}  // namespace

// =====================================================================
// 身份块（§6.5 TaskIdentityBlock——五个规范文本，定长域拼接）
// =====================================================================

void encodeIdentityBlock(const core::TaskIdentity& identity,
                         std::array<std::uint8_t, kIdentityBlockSize>& out)
{
    // 前置：五元组必须有效（保留值身份不可上线——ARCH §4.1 身份纪律；
    // 违约属发送侧拼装错误，fail-fast）。
    if (!identity.isValid()) {
        throw ExecutionError(ExecutionErrorCode::InvalidState,
                             "execution/channel: identity block requires a valid five-tuple");
    }
    out.fill(0);  // 定长域：先清零再写文本——attempt 的 NUL 填充由此而来

    // 四个 Id128 的规范文本各恰 36 字节（tag 4＋hex 32）——toCanonical 的
    // 往返严格性（core §4.1）保证定长。
    std::array<std::string, 4> texts{identity.project.toCanonical(),
                                     identity.branch.toCanonical(),
                                     identity.revision.toCanonical(),
                                     identity.run.toCanonical()};
    std::size_t offset = 0;
    for (const std::string& t : texts) {
        std::memcpy(out.data() + offset, t.data(), t.size());
        offset += 36;
    }
    // attempt："att-<十进制>" 右 NUL 补齐至 24（u64 十进制最长 20 位＋tag）。
    const std::string att = identity.attempt.toCanonical();
    std::memcpy(out.data() + offset, att.data(), att.size());
}

std::optional<core::TaskIdentity> decodeIdentityBlock(const std::uint8_t* bytes,
                                                      std::size_t size)
{
    if (size != kIdentityBlockSize) {
        return std::nullopt;
    }
    core::TaskIdentity identity;
    // 四个定长文本域逐个严格解析（core fromCanonical 拒绝大小写/长度/字符
    // 集违约——帧被篡改在此处暴露为 nullopt→协议错误）。四类型各有独立
    // 强类型（core §4.1），解析调用逐类型展开。
    try {
        identity.project = core::ProjectId::fromCanonical(
            std::string(reinterpret_cast<const char*>(bytes), 36));
        identity.branch = core::BranchId::fromCanonical(
            std::string(reinterpret_cast<const char*>(bytes + 36), 36));
        identity.revision = core::RevisionId::fromCanonical(
            std::string(reinterpret_cast<const char*>(bytes + 72), 36));
        identity.run = core::RunId::fromCanonical(
            std::string(reinterpret_cast<const char*>(bytes + 108), 36));
    } catch (const std::exception&) {
        // 任一字段解析失败＝身份块非法（协议错误面，不抛——对端数据
        // 问题走结构化路径，AGENTS §3 错误二分）。
        return std::nullopt;
    }
    // attempt 域（偏移 4×36＝144）：NUL 填充——取到第一个 NUL 为止的规范
    // 文本（空＝非法）。
    const std::uint8_t* attStart = bytes + 144;
    std::size_t attLen = 0;
    while (attLen < 24 && attStart[attLen] != 0) {
        ++attLen;
    }
    if (attLen == 0 || attLen == 24) {
        return std::nullopt;  // 空文本或填满（无终止 NUL）都违反编码纪律
    }
    const std::string attText(reinterpret_cast<const char*>(attStart), attLen);
    try {
        identity.attempt = core::AttemptId::fromCanonical(attText);
    } catch (const std::exception&) {
        return std::nullopt;
    }
    return identity;
}

// =====================================================================
// 帧编解码与流装配
// =====================================================================

std::vector<std::uint8_t> encodeFrame(const ChannelFrame& frame)
{
    // 发送侧拼装违约两处 fail-fast：seq 保留值 0 不可上线（序控基线从 1
    // 起）；五元组无效帧不可上线（ARCH §4.1 五元组随行承诺）。
    if (frame.seq == 0) {
        throw ExecutionError(ExecutionErrorCode::InvalidState,
                             "execution/channel: frame seq 0 is a reserved value");
    }
    if (!frame.identity.isValid()) {
        throw ExecutionError(ExecutionErrorCode::InvalidState,
                             "execution/channel: frame requires a valid five-tuple");
    }
    if (frame.payload.size() > kMaxFramePayload) {
        throw ExecutionError(ExecutionErrorCode::InvalidState,
                             "execution/channel: frame payload exceeds protocol bound");
    }

    std::vector<std::uint8_t> out;
    out.reserve(kFrameFixedHeaderSize + kIdentityBlockSize + frame.payload.size());
    out.insert(out.end(), kFrameMagic.begin(), kFrameMagic.end());  // magic 7B
    putU16(out, kChannelProtocolVersion);                           // protoVersion u16
    out.push_back(static_cast<std::uint8_t>(frame.type));           // msgType u8
    out.push_back(frame.flags);                                     // flags u8
    putU64(out, frame.seq);                                         // seq u64
    putU32(out, static_cast<std::uint32_t>(frame.payload.size()));  // payloadLen u32——自定界
    std::array<std::uint8_t, kIdentityBlockSize> idBlock;
    encodeIdentityBlock(frame.identity, idBlock);                   // 身份块 168B
    out.insert(out.end(), idBlock.begin(), idBlock.end());
    out.insert(out.end(), frame.payload.begin(), frame.payload.end());
    return out;
}

FrameAssembler::Status FrameAssembler::feed(const std::uint8_t* data, std::size_t size,
                                            std::vector<ChannelFrame>& out)
{
    // 协议错误闩：通道字节流一旦出现非法帧，后续字节的对齐已不可信——
    // 恒失败（恢复手段＝任务失败＋通道重建，§6.5）。
    if (m_failed) {
        return Status::ProtocolError;
    }
    m_buffer.insert(m_buffer.end(), data, data + size);

    // 增量解析：每次尝试从消费位凑出一个完整帧，凑不出即止（等下次喂入）。
    for (;;) {
        const std::size_t available = m_buffer.size() - m_offset;
        if (available < kFrameFixedHeaderSize) {
            break;  // 连固定头都未凑齐
        }
        const std::uint8_t* base = m_buffer.data() + m_offset;
        // 校验 1：magic 逐字节（错位/混入垃圾在此暴露）。
        if (std::memcmp(base, kFrameMagic.data(), kFrameMagic.size()) != 0) {
            m_failed = true;
            return Status::ProtocolError;
        }
        // 校验 2：协议主版本（次版本兼容语义阶段 A 不存在——非 1 即拒，
        // §6.5"消息版本"行）。
        const std::uint16_t version = getU16(base + 7);
        if (version != kChannelProtocolVersion) {
            m_failed = true;
            return Status::ProtocolError;
        }
        // 校验 3：载荷长度上限（防长度域被破坏后按它读穿/分配爆掉）。
        const std::uint32_t payloadLen = getU32(base + 19);
        if (payloadLen > kMaxFramePayload) {
            m_failed = true;
            return Status::ProtocolError;
        }
        const std::size_t frameSize =
            kFrameFixedHeaderSize + kIdentityBlockSize + payloadLen;
        if (available < frameSize) {
            break;  // 帧体未凑齐——等下一块字节
        }
        // 校验 4：身份块（五元组随行承诺的帧级核对——ARCH §4.1）。
        const std::uint8_t* idBytes = base + kFrameFixedHeaderSize;
        std::optional<core::TaskIdentity> identity =
            decodeIdentityBlock(idBytes, kIdentityBlockSize);
        if (!identity.has_value() || !identity->isValid()) {
            m_failed = true;
            return Status::ProtocolError;
        }

        // 全部校验通过——取帧并推进消费位（压缩策略：消费位过半即向首
        // 搬移一次，均摊 O(1)，缓冲不无界增长）。
        ChannelFrame frame;
        frame.type = static_cast<ChannelMsgType>(base[9]);
        frame.flags = base[10];
        frame.seq = getU64(base + 11);
        frame.identity = *identity;
        frame.payload.assign(base + kFrameFixedHeaderSize + kIdentityBlockSize,
                             base + frameSize);
        m_offset += frameSize;
        if (m_offset * 2 >= m_buffer.size()) {
            m_buffer.erase(m_buffer.begin(),
                           m_buffer.begin() + static_cast<std::ptrdiff_t>(m_offset));
            m_offset = 0;
        }
        out.push_back(std::move(frame));
    }
    return Status::Ok;
}

// =====================================================================
// seq 序控（§6.5"乱序与重复"）
// =====================================================================

FrameSequencer::FrameSequencer(Config config, ClockFn clock)
    : m_config(config)
    , m_clock(std::move(clock))
{
    // 窗口容量下限 1（0 会使任何越前帧都判溢出——无意义的退化为非法配置）。
    if (m_config.windowCapacity == 0) {
        m_config.windowCapacity = 1;
    }
}

std::vector<ChannelFrame> FrameSequencer::accept(ChannelFrame&& frame, Outcome* outcome)
{
    if (outcome != nullptr) {
        *outcome = Outcome::Delivered;
    }
    if (m_failed) {
        return {};  // 已判协议错误——后续帧全部静默丢弃（错误面已在首次触发时上报）
    }
    if (frame.seq == 0) {
        // 保留值 seq 上线＝协议错误（与窗口溢出同一失败面）。
        m_failed = true;
        if (outcome != nullptr) {
            *outcome = Outcome::WindowOverflow;
        }
        return {};
    }
    // 基线建立：首帧 seq 即期望起点（发送方从 1 起单调，但序控不依赖该
    // 约定——以实测首帧为锚，重连/复用场景同样成立）。
    if (m_nextExpected == 0) {
        m_nextExpected = frame.seq;
    }

    if (frame.seq == m_nextExpected) {
        // 恰为期望帧：基线先推进（直接交付同样消耗一个期望位——否则下
        // 一帧同 seq 会被误判乱序），再尝试连带吐出缓冲中的连续段。
        ++m_nextExpected;
        std::vector<ChannelFrame> delivered = drainInOrder();
        delivered.insert(delivered.begin(), std::move(frame));
        // 连续段交付完毕：若窗口仍空则缺口计时清除（无未决缺口）。
        if (m_buffered.empty()) {
            m_gapOpen = false;
        }
        return delivered;
    }
    if (frame.seq < m_nextExpected || m_buffered.count(frame.seq) != 0) {
        // 重复/回退：已交付或已在窗口——丢弃＋开发诊断面（§6.5"重复/回退
        // →丢弃＋开发诊断"）。
        if (outcome != nullptr) {
            *outcome = Outcome::DuplicateDropped;
        }
        return {};
    }
    if (frame.seq - m_nextExpected > m_config.windowCapacity
        || m_buffered.size() >= m_config.windowCapacity) {
        // 越前过远或窗口满：缺口在可预见时间内无法由管道重传补齐（本地
        // 管道无重传语义）——判协议错误（§6.5"断裂→通道错误→尝试 Failed"）。
        m_failed = true;
        if (outcome != nullptr) {
            *outcome = Outcome::WindowOverflow;
        }
        return {};
    }
    // 普通乱序：入窗口缓冲。首个缺口打开时开始断裂计时（后续帧不清零
    // 计时——"连续 3 个间隔"同源的保守方向：缺口等待只看打开时长）。
    if (!m_gapOpen) {
        m_gapOpen = true;
        m_gapOpenedAt = m_clock ? m_clock() : std::chrono::steady_clock::now();
    }
    m_buffered.emplace(frame.seq, std::move(frame));
    if (outcome != nullptr) {
        *outcome = Outcome::Buffered;
    }
    return {};
}

bool FrameSequencer::gapTimedOut()
{
    if (m_failed || !m_gapOpen) {
        return false;
    }
    const auto now = m_clock ? m_clock() : std::chrono::steady_clock::now();
    if (now - m_gapOpenedAt >= m_config.gapTimeout) {
        // 断裂成立：缺口超时未补齐——协议错误闩（调用方出
        // EX-CHANNEL-PROTOCOL-ERROR 并使尝试 Failed，§6.5）。
        m_failed = true;
        return true;
    }
    return false;
}

std::vector<ChannelFrame> FrameSequencer::drainInOrder()
{
    std::vector<ChannelFrame> delivered;
    for (;;) {
        const auto it = m_buffered.find(m_nextExpected);
        if (it == m_buffered.end()) {
            break;  // 下一期望帧未到——停止推进
        }
        delivered.push_back(std::move(it->second));
        m_buffered.erase(it);
        ++m_nextExpected;
    }
    return delivered;
}

// =====================================================================
// 分帧续传（§6.5"输入传输"）
// =====================================================================

bool isFragmentableType(ChannelMsgType type) noexcept
{
    // 阶段 A 的两类大载荷：入向物化派发、出向最终产出（FinalOutput 的
    // canonical 字节可能达百 MiB 量级——NFR-PERF-03 不整体装载）。批次
    // 类型天然按批分帧，不需要消息级分片。
    switch (type) {
    case ChannelMsgType::DispatchRequest:
    case ChannelMsgType::FinalOutput:
        return true;
    default:
        return false;
    }
}

bool fragmentMessage(ChannelMsgType type, const core::TaskIdentity& identity,
                     std::uint64_t& seqBase, const std::vector<std::uint8_t>& message,
                     std::size_t fragmentSize, std::vector<ChannelFrame>& out)
{
    // 调用方违约三面：类型不可分片（收侧重组器不认识）、单片为 0（死循环）、
    // 消息超防御上限（kMaxMessageBytes——见头注）。
    if (!isFragmentableType(type) || fragmentSize == 0
        || message.size() > kMaxMessageBytes) {
        return false;
    }
    // 分片数＝向上取整；空消息至少 1 片（0 片会让收侧永久等待——收发
    // 双方都以"末片 flags 无 More 位"为完整判据，必须有片存在）。
    const std::size_t fragCount =
        message.empty()
            ? 1u
            : (message.size() + fragmentSize - 1) / fragmentSize;
    for (std::uint32_t i = 0; i < fragCount; ++i) {
        const std::size_t offset = static_cast<std::size_t>(i) * fragmentSize;
        const std::size_t len =
            std::min(fragmentSize, message.size() - offset);
        ChannelFrame frame;
        frame.type = type;
        // 末片清 More 位——收侧以此判定消息完整（单片消息 flags＝0）。
        frame.flags = (i + 1 < fragCount) ? kFlagFragmentMore : 0;
        frame.seq = seqBase;
        frame.identity = identity;
        // 每片载荷＝分片头（总长＋片序＋片数）＋本片字节。
        putU64(frame.payload, static_cast<std::uint64_t>(message.size()));
        putU32(frame.payload, i);
        putU32(frame.payload, static_cast<std::uint32_t>(fragCount));
        frame.payload.insert(frame.payload.end(), message.begin() + offset,
                             message.begin() + offset + len);
        out.push_back(std::move(frame));
        ++seqBase;
    }
    return true;
}

std::vector<ChannelFrame> MessageReassembler::accept(ChannelFrame&& frame, bool* broken)
{
    if (broken != nullptr) {
        *broken = false;
    }
    // 非可分片类型直接透传（Heartbeat/Progress 等小帧——一帧即完整消息）。
    if (!isFragmentableType(frame.type)) {
        std::vector<ChannelFrame> passthrough;
        passthrough.push_back(std::move(frame));
        return passthrough;
    }
    if (m_failed) {
        return {};  // 失败闩——同 FrameAssembler 纪律
    }
    // 分片头解析（载荷不足 16B＝非法分片）。
    if (frame.payload.size() < kFragmentHeaderSize) {
        m_failed = true;
        if (broken != nullptr) {
            *broken = true;
        }
        return {};
    }
    const std::uint8_t* p = frame.payload.data();
    const std::uint64_t totalLen = getU64(p);
    const std::uint32_t index = getU32(p + 8);
    const std::uint32_t fragCount = getU32(p + 12);
    const std::size_t chunkSize = frame.payload.size() - kFragmentHeaderSize;
    // 一致性校验集：总长上限（防分配爆掉）、片数与片序、总长与片数的
    // 粗一致性（总长不得小于已收量）。
    if (totalLen > kMaxMessageBytes || fragCount == 0 || index >= fragCount) {
        m_failed = true;
        if (broken != nullptr) {
            *broken = true;
        }
        return {};
    }
    if (!m_partial.has_value()) {
        // 首片：建立在途消息（totalLen/fragCount 以首片声明为准）。
        m_partial.emplace();
        m_partial->totalLen = totalLen;
        m_partial->fragCount = fragCount;
        m_partial->nextIndex = 0;
        m_partial->bytes.reserve(static_cast<std::size_t>(
            totalLen < kMaxMessageBytes ? totalLen : kMaxMessageBytes));
    } else if (m_partial->totalLen != totalLen || m_partial->fragCount != fragCount) {
        // 后续片与首片声明失配＝通道错乱（协议错误）。
        m_failed = true;
        if (broken != nullptr) {
            *broken = true;
        }
        return {};
    }
    if (index != m_partial->nextIndex) {
        // 片序跳越：序控层本应保证连续——出现跳越即协议错误（不该到达）。
        m_failed = true;
        if (broken != nullptr) {
            *broken = true;
        }
        return {};
    }
    // 累积本片（总长不得被超出）。
    if (m_partial->bytes.size() + chunkSize > m_partial->totalLen) {
        m_failed = true;
        if (broken != nullptr) {
            *broken = true;
        }
        return {};
    }
    m_partial->bytes.insert(
        m_partial->bytes.end(), frame.payload.begin() + kFragmentHeaderSize,
        frame.payload.end());
    ++m_partial->nextIndex;

    // 末片（flags 无 More 位）→ 完整消息出帧（type 不变、payload 换成
    // 重组后的完整消息字节；seq/identity 取末片——帧级字段由交付序保证）。
    if ((frame.flags & kFlagFragmentMore) == 0) {
        if (m_partial->bytes.size() != m_partial->totalLen
            || m_partial->nextIndex != m_partial->fragCount) {
            // 末片到了但字节量/片数不齐——声明与实收失配。
            m_failed = true;
            if (broken != nullptr) {
                *broken = true;
            }
            return {};
        }
        ChannelFrame complete;
        complete.type = frame.type;
        complete.flags = 0;
        complete.seq = frame.seq;
        complete.identity = frame.identity;
        complete.payload = std::move(m_partial->bytes);
        m_partial.reset();
        std::vector<ChannelFrame> out;
        out.push_back(std::move(complete));
        return out;
    }
    return {};  // 还有后续片——继续等
}

// =====================================================================
// 载荷编解码
// =====================================================================

std::vector<std::uint8_t> encodeHello(const HelloPayload& p)
{
    std::vector<std::uint8_t> out;
    putU16(out, p.protoVersion);
    putStr(out, p.manifestDigestHex);
    return out;
}

std::optional<HelloPayload> decodeHello(const std::vector<std::uint8_t>& bytes)
{
    HelloPayload p;
    const std::uint8_t* cursor = bytes.data();
    const std::uint8_t* end = bytes.data() + bytes.size();
    if (end - cursor < 2) {
        return std::nullopt;
    }
    p.protoVersion = getU16(cursor);
    cursor += 2;
    const auto digest = getStr(cursor, end);
    if (!digest.has_value() || cursor != end) {
        return std::nullopt;  // 尾部有多余字节＝载荷形态非法（解码校验）
    }
    p.manifestDigestHex = std::move(*digest);
    return p;
}

std::vector<std::uint8_t> encodeHelloAck(const HelloAckPayload& p)
{
    std::vector<std::uint8_t> out;
    out.push_back(p.accepted ? 1 : 0);
    putU32(out, p.heartbeatIntervalMs);
    putStr(out, p.reason);
    return out;
}

std::optional<HelloAckPayload> decodeHelloAck(const std::vector<std::uint8_t>& bytes)
{
    HelloAckPayload p;
    const std::uint8_t* cursor = bytes.data();
    const std::uint8_t* end = bytes.data() + bytes.size();
    if (end - cursor < 5) {
        return std::nullopt;
    }
    p.accepted = cursor[0] != 0;
    p.heartbeatIntervalMs = getU32(cursor + 1);
    cursor += 5;
    const auto reason = getStr(cursor, end);
    if (!reason.has_value() || cursor != end) {
        return std::nullopt;
    }
    p.reason = std::move(*reason);
    return p;
}

std::vector<std::uint8_t> encodeDispatchRequest(const DispatchRequestPayload& p)
{
    std::vector<std::uint8_t> out;
    putStr(out, p.snapshotIdentity.toCanonical());
    putStr(out, p.modelIdentity.toCanonical());
    putBytes(out, p.snapshotBytes);
    putBytes(out, p.modelBytes);
    return out;
}

std::optional<DispatchRequestPayload> decodeDispatchRequest(
    const std::vector<std::uint8_t>& bytes)
{
    DispatchRequestPayload p;
    const std::uint8_t* cursor = bytes.data();
    const std::uint8_t* end = bytes.data() + bytes.size();
    const auto snapId = getStr(cursor, end);
    const auto modelId = getStr(cursor, end);
    if (!snapId.has_value() || !modelId.has_value()) {
        return std::nullopt;
    }
    // 身份文本严格解析（"cid-<64hex>"）——被篡改的声明身份在此暴露。
    try {
        p.snapshotIdentity = core::ContentIdentity::fromCanonical(*snapId);
        p.modelIdentity = core::ContentIdentity::fromCanonical(*modelId);
    } catch (const std::exception&) {
        return std::nullopt;
    }
    const auto snap = getBytes(cursor, end);
    const auto model = getBytes(cursor, end);
    if (!snap.has_value() || !model.has_value() || cursor != end) {
        return std::nullopt;
    }
    p.snapshotBytes = std::move(*snap);
    p.modelBytes = std::move(*model);
    return p;
}

std::vector<std::uint8_t> encodeProgress(const ProgressReport& report)
{
    std::vector<std::uint8_t> out;
    putU32(out, static_cast<std::uint32_t>(report.percent));  // percent [0,100]——make 已保证非负
    putStr(out, report.phaseToken);
    putU64(out, report.batchesDone);
    putU64(out, report.batchesTotal);
    return out;
}

std::optional<ProgressReport> decodeProgress(const std::vector<std::uint8_t>& bytes)
{
    const std::uint8_t* cursor = bytes.data();
    const std::uint8_t* end = bytes.data() + bytes.size();
    if (end - cursor < 4) {
        return std::nullopt;
    }
    const std::uint32_t percent = getU32(cursor);
    cursor += 4;
    auto phase = getStr(cursor, end);
    if (!phase.has_value() || end - cursor < 16) {
        return std::nullopt;
    }
    const std::uint64_t done = getU64(cursor);
    const std::uint64_t total = getU64(cursor + 8);
    cursor += 16;
    if (cursor != end) {
        return std::nullopt;
    }
    // 值域校验复用 ProgressReport::make（percent>100/越界计数→异常）——
    // 异常在本层翻译为 nullopt（帧级解码校验失败→协议错误面，不向通道
    // 读线程抛）。
    try {
        return ProgressReport::make(static_cast<int>(percent), std::move(*phase), done,
                                    total);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::vector<std::uint8_t> encodeResultBatch(const ResultBatchPayload& p)
{
    std::vector<std::uint8_t> out;
    putU64(out, p.batchIndex);
    putBytes(out, p.bytes);
    return out;
}

std::optional<ResultBatchPayload> decodeResultBatch(const std::vector<std::uint8_t>& bytes)
{
    ResultBatchPayload p;
    const std::uint8_t* cursor = bytes.data();
    const std::uint8_t* end = bytes.data() + bytes.size();
    if (end - cursor < 8) {
        return std::nullopt;
    }
    p.batchIndex = getU64(cursor);
    cursor += 8;
    const auto data = getBytes(cursor, end);
    if (!data.has_value() || cursor != end) {
        return std::nullopt;
    }
    p.bytes = std::move(*data);
    return p;
}

std::vector<std::uint8_t> encodeCheckpointBatch(const CheckpointBatchPayload& p)
{
    std::vector<std::uint8_t> out;
    putU64(out, p.sequence);
    putBytes(out, p.bytes);
    return out;
}

std::optional<CheckpointBatchPayload> decodeCheckpointBatch(
    const std::vector<std::uint8_t>& bytes)
{
    CheckpointBatchPayload p;
    const std::uint8_t* cursor = bytes.data();
    const std::uint8_t* end = bytes.data() + bytes.size();
    if (end - cursor < 8) {
        return std::nullopt;
    }
    p.sequence = getU64(cursor);
    cursor += 8;
    const auto data = getBytes(cursor, end);
    if (!data.has_value() || cursor != end) {
        return std::nullopt;
    }
    p.bytes = std::move(*data);
    return p;
}

std::vector<std::uint8_t> encodeFinalOutput(const FinalOutputPayload& p)
{
    std::vector<std::uint8_t> out;
    putStr(out, p.snapshotIdentity.toCanonical());
    putStr(out, p.modelIdentity.toCanonical());
    putBytes(out, p.outputCanon);
    return out;
}

std::optional<FinalOutputPayload> decodeFinalOutput(const std::vector<std::uint8_t>& bytes)
{
    FinalOutputPayload p;
    const std::uint8_t* cursor = bytes.data();
    const std::uint8_t* end = bytes.data() + bytes.size();
    const auto snapId = getStr(cursor, end);
    const auto modelId = getStr(cursor, end);
    if (!snapId.has_value() || !modelId.has_value()) {
        return std::nullopt;
    }
    try {
        p.snapshotIdentity = core::ContentIdentity::fromCanonical(*snapId);
        p.modelIdentity = core::ContentIdentity::fromCanonical(*modelId);
    } catch (const std::exception&) {
        return std::nullopt;
    }
    const auto canon = getBytes(cursor, end);
    if (!canon.has_value() || cursor != end) {
        return std::nullopt;
    }
    p.outputCanon = std::move(*canon);
    return p;
}

std::vector<std::uint8_t> encodeErrorReport(const ErrorReportPayload& p)
{
    std::vector<std::uint8_t> out;
    out.push_back(p.dev ? 1 : 0);
    putStr(out, p.codeOrChannel);
    putStr(out, p.message);
    return out;
}

std::optional<ErrorReportPayload> decodeErrorReport(const std::vector<std::uint8_t>& bytes)
{
    ErrorReportPayload p;
    const std::uint8_t* cursor = bytes.data();
    const std::uint8_t* end = bytes.data() + bytes.size();
    if (end - cursor < 1) {
        return std::nullopt;
    }
    p.dev = cursor[0] != 0;
    ++cursor;
    const auto code = getStr(cursor, end);
    const auto message = getStr(cursor, end);
    if (!code.has_value() || !message.has_value() || cursor != end) {
        return std::nullopt;
    }
    p.codeOrChannel = std::move(*code);
    p.message = std::move(*message);
    return p;
}

// =====================================================================
// 握手摘要与物化身份 helper
// =====================================================================

std::string manifestDigestHex(std::string_view canonicalManifest)
{
    // 空装配＝装配错误（无评估能力的 worker 不该被启动——fail-fast 在
    // 计算入口，防止两侧对空文本各自约定出"一致但无意义"的摘要）。
    if (canonicalManifest.empty()) {
        throw ExecutionError(ExecutionErrorCode::InvalidState,
                             "execution/channel: manifest text must be non-empty");
    }
    core::ContentDigester digester;
    digester.update(canonicalManifest.data(), canonicalManifest.size());
    return digestToHex(digester.finalize());
}

core::ContentIdentity computeContentIdentity(const std::vector<std::uint8_t>& bytes)
{
    core::ContentDigester digester;
    // 空字节序列走同一摘要路径（与 Admission 的载荷指纹同款纪律——统一
    // 路径保证"空载荷身份"确定性）。
    digester.update(bytes.empty() ? nullptr : bytes.data(), bytes.size());
    return core::ContentIdentity::fromCanonical(
        "cid-" + digestToHex(digester.finalize()));
}

bool contentIdentityMatches(const std::vector<std::uint8_t>& bytes,
                            const core::ContentIdentity& declared)
{
    return computeContentIdentity(bytes) == declared;
}

}  // namespace sdurws::ird::execution
