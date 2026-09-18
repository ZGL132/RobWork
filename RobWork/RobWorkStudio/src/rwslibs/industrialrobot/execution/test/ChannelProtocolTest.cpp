/**
 * @file   ChannelProtocolTest.cpp
 * @brief  通道协议（IRDCHN/1）逻辑面用例组——帧编解码/身份块/流装配/
 *         seq 序控（重排/去重/断裂）/分帧续传/载荷编解码/握手摘要/物化
 *         身份/退出码分类/心跳策略默认值（§6.5、§6.4、§11 EX-CHN-1）。
 *
 * 设计依据：
 *   - units/execution.md §6.5（IRDCHN/1 帧——自定界＋seq 序控＋分帧续传；
 *     "乱序→按 seq 重排窗口；重复/回退→丢弃＋开发诊断；断裂→协议错误"）、
 *     §6.4（退出码约定集表——0/10/11/12/20/21/约定集外）、§3.1
 *     （ChannelProtocol.hpp 组成行——主/worker 共用契约）
 *   - 任务契约 tasks/foundation/EX-T06.json acceptance 2（EX-CHN-1：帧
 *     自定界＋seq 重排去重＋断裂协议错误——本套件验证**协议逻辑面**；
 *     真进程通道面归 contract_test WorkerProcessContractTest）、acceptance 1
 *     （退出码分类表——真进程观测归 contract_test，此处钉分类纯函数）、
 *     acceptance 3（分帧续传/分批载荷的编解码面）
 *
 * 替身边界声明（§11 同源纪律）：本套件全部为纯逻辑用例（零进程/零管道/
 *   零磁盘）——断言的是协议编解码与序控判定的确定性；通道 I/O 行为
 *   （阻塞/超时/断裂）与进程行为（退出/强杀）由 win32 设施与契约测试
 *   的真进程用例承载，不在本套件声明范围。
 *
 * 时钟纪律（testkit §6.5）：缺口超时用例注入 ManualClock 虚拟推进——
 *   不 sleep、不依赖真实时延。
 */

#include <sdurws/ird/execution/ChannelProtocol.hpp>
#include <sdurws/ird/execution/WorkerSupervisor.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace {

using namespace sdurws::ird::execution;
namespace core = sdurws::ird::core;

// =====================================================================
// 夹具：确定性五元组与帧构造
// =====================================================================

/// 每用例独立的完整五元组（core 生成器——值本身无业务含义，只需合法）。
core::TaskIdentity makeIdentity()
{
    core::TaskIdentity id;
    id.project = core::ProjectId::generate();
    id.branch = core::BranchId::generate();
    id.revision = core::RevisionId::generate();
    id.run = core::RunId::generate();
    id.attempt = core::AttemptId{1};
    return id;
}

/// 构造一帧（seq 从 1 起的正常形态——协议纪律）。
ChannelFrame makeFrame(ChannelMsgType type, std::uint64_t seq,
                       std::vector<std::uint8_t> payload)
{
    ChannelFrame frame;
    frame.type = type;
    frame.seq = seq;
    frame.identity = makeIdentity();
    frame.payload = std::move(payload);
    return frame;
}

/// 手工时钟（testkit §6.5 ManualClock 同型——accept 虚拟推进，不 sleep）。
struct ManualClock {
    std::chrono::steady_clock::time_point now{std::chrono::steady_clock::now() + std::chrono::hours(1)};
    std::chrono::steady_clock::time_point operator()()
    {
        return now;
    }
    void advance(std::chrono::milliseconds delta) { now += delta; }
};

/// 确定性批次字节（worker 宿主替身产物的同型形态——"BATCH:<i>" 文本）。
std::vector<std::uint8_t> batchBytes(std::uint64_t index)
{
    const std::string text = "BATCH:" + std::to_string(index);
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

// =====================================================================
// 帧编解码与流装配（§6.5 帧布局——自定界）
// =====================================================================

TEST(ChannelProtocolFrameTest, EncodeDecodeRoundtripPreservesAllFields)
{
    // 帧全字段往返：类型/flags/seq/五元组/载荷逐一保真——自定界解码的
    // 基本契约（EX-CHN-1"IRDCHN/1 帧自定界"）。
    const ChannelFrame src = makeFrame(ChannelMsgType::ResultBatch, 7, {0xDE, 0xAD, 0xBE, 0xEF});
    const std::vector<std::uint8_t> bytes = encodeFrame(src);

    FrameAssembler assembler;
    std::vector<ChannelFrame> out;
    ASSERT_EQ(assembler.feed(bytes.data(), bytes.size(), out), FrameAssembler::Status::Ok);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].type, src.type);
    EXPECT_EQ(out[0].flags, src.flags);
    EXPECT_EQ(out[0].seq, src.seq);
    EXPECT_TRUE(out[0].identity == src.identity);
    EXPECT_EQ(out[0].payload, src.payload);
}

TEST(ChannelProtocolFrameTest, AssemblerSplitsFramesAcrossChunkBoundaries)
{
    // 字节流无消息边界（§6.6）——一帧分多次喂入仍能凑齐（流装配的增量性）。
    const ChannelFrame src = makeFrame(ChannelMsgType::Progress, 3, {1, 2, 3, 4, 5});
    const std::vector<std::uint8_t> bytes = encodeFrame(src);

    FrameAssembler assembler;
    std::vector<ChannelFrame> out;
    const std::size_t half = bytes.size() / 2;
    ASSERT_EQ(assembler.feed(bytes.data(), half, out), FrameAssembler::Status::Ok);
    EXPECT_TRUE(out.empty());  // 半帧不出帧
    ASSERT_EQ(assembler.feed(bytes.data() + half, bytes.size() - half, out),
              FrameAssembler::Status::Ok);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].seq, 3u);
    EXPECT_EQ(out[0].payload, src.payload);
}

TEST(ChannelProtocolFrameTest, AssemblerRejectsBadMagicAndVersion)
{
    // 帧级校验：magic 错位与主版本失配都是协议错误（EX-CHANNEL-PROTOCOL-
    // ERROR 的帧面来源；§6.5"消息版本"行——非 1 即拒）。
    ChannelFrame src = makeFrame(ChannelMsgType::Heartbeat, 1, {});
    std::vector<std::uint8_t> bytes = encodeFrame(src);
    bytes[0] = 'X';  // 破坏 magic
    {
        FrameAssembler assembler;
        std::vector<ChannelFrame> out;
        EXPECT_EQ(assembler.feed(bytes.data(), bytes.size(), out),
                  FrameAssembler::Status::ProtocolError);
        EXPECT_TRUE(assembler.failed());
    }
    bytes = encodeFrame(src);
    bytes[7] = 0x99;  // 破坏 protoVersion 低字节（小端——第 8 字节起）
    {
        FrameAssembler assembler;
        std::vector<ChannelFrame> out;
        EXPECT_EQ(assembler.feed(bytes.data(), bytes.size(), out),
                  FrameAssembler::Status::ProtocolError);
    }
}

TEST(ChannelProtocolFrameTest, AssemblerRejectsOversizePayloadLength)
{
    // 长度域被破坏后不得按它读穿/分配（kMaxFramePayload 上限——帧级
    // 解码校验，§6.5"帧级靠长度＋解码校验"）。
    ChannelFrame src = makeFrame(ChannelMsgType::Heartbeat, 1, {});
    std::vector<std::uint8_t> bytes = encodeFrame(src);
    const std::size_t lenOffset = 19;  // magic7+ver2+type1+flags1+seq8
    bytes[lenOffset] = 0xFF;
    bytes[lenOffset + 1] = 0xFF;
    bytes[lenOffset + 2] = 0xFF;
    bytes[lenOffset + 3] = 0xFF;
    FrameAssembler assembler;
    std::vector<ChannelFrame> out;
    EXPECT_EQ(assembler.feed(bytes.data(), bytes.size(), out),
              FrameAssembler::Status::ProtocolError);
}

TEST(ChannelProtocolFrameTest, EncodeRejectsReservedSeqAndInvalidIdentity)
{
    // 发送侧拼装违约 fail-fast：seq 保留值 0、无效五元组（协议纪律的
    // 源头侧——AGENTS §3 调用方违约 fail-fast）。
    ChannelFrame frame = makeFrame(ChannelMsgType::Heartbeat, 0, {});
    EXPECT_THROW(encodeFrame(frame), ExecutionError);
    frame.seq = 1;
    frame.identity = core::TaskIdentity{};  // 全零保留值
    EXPECT_THROW(encodeFrame(frame), ExecutionError);
}

// =====================================================================
// seq 序控（§6.5"乱序与重复"——EX-CHN-1 判定本体）
// =====================================================================

TEST(ChannelProtocolSequencerTest, InOrderFramesDeliverDirectly)
{
    FrameSequencer sequencer;
    for (std::uint64_t seq = 1; seq <= 3; ++seq) {
        FrameSequencer::Outcome outcome;
        const std::vector<ChannelFrame> delivered =
            sequencer.accept(makeFrame(ChannelMsgType::Heartbeat, seq, {}), &outcome);
        ASSERT_EQ(outcome, FrameSequencer::Outcome::Delivered);
        ASSERT_EQ(delivered.size(), 1u);
        EXPECT_EQ(delivered[0].seq, seq);
    }
}

TEST(ChannelProtocolSequencerTest, OutOfOrderFramesReorderBySeq_EX_CHN_1)
{
    // EX-CHN-1 重排半区：基线由首帧（seq1）确立；seq3 先到入窗口缓冲，
    // seq2 补齐缺口时连带吐出 2,3（按 seq 序交付）。
    FrameSequencer sequencer;
    FrameSequencer::Outcome outcome;
    (void)sequencer.accept(makeFrame(ChannelMsgType::Progress, 1, {1}), &outcome);
    ASSERT_EQ(outcome, FrameSequencer::Outcome::Delivered);  // 基线帧直接交付

    auto buffered = sequencer.accept(makeFrame(ChannelMsgType::Progress, 3, {3}), &outcome);
    EXPECT_EQ(outcome, FrameSequencer::Outcome::Buffered);
    EXPECT_TRUE(buffered.empty());

    buffered = sequencer.accept(makeFrame(ChannelMsgType::Progress, 2, {2}), &outcome);
    EXPECT_EQ(outcome, FrameSequencer::Outcome::Delivered);
    // seq2 补齐缺口——2,3 连续交付。
    ASSERT_EQ(buffered.size(), 2u);
    EXPECT_EQ(buffered[0].seq, 2u);
    EXPECT_EQ(buffered[1].seq, 3u);
    EXPECT_TRUE(sequencer.idle());
}

TEST(ChannelProtocolSequencerTest, DuplicatesAndBackwardsAreDroppedForDevDiagnostic_EX_CHN_1)
{
    // EX-CHN-1 去重半区：重复/回退 seq 丢弃＋开发诊断面（DuplicateDropped
    // ——非致命，通道继续）。
    FrameSequencer sequencer;
    FrameSequencer::Outcome outcome;
    (void)sequencer.accept(makeFrame(ChannelMsgType::Heartbeat, 1, {}), &outcome);
    ASSERT_EQ(outcome, FrameSequencer::Outcome::Delivered);

    (void)sequencer.accept(makeFrame(ChannelMsgType::Heartbeat, 1, {}), &outcome);
    EXPECT_EQ(outcome, FrameSequencer::Outcome::DuplicateDropped);  // 已交付 seq 重发

    (void)sequencer.accept(makeFrame(ChannelMsgType::Heartbeat, 3, {}), &outcome);
    ASSERT_EQ(outcome, FrameSequencer::Outcome::Buffered);
    (void)sequencer.accept(makeFrame(ChannelMsgType::Heartbeat, 3, {}), &outcome);
    EXPECT_EQ(outcome, FrameSequencer::Outcome::DuplicateDropped);  // 窗口内重复

    (void)sequencer.accept(makeFrame(ChannelMsgType::Heartbeat, 2, {}), &outcome);
    EXPECT_FALSE(sequencer.failed());  // 非致命——序控继续
}

TEST(ChannelProtocolSequencerTest, WindowOverflowIsProtocolError_EX_CHN_1)
{
    // EX-CHN-1 断裂面之一：越前超过窗口容量＝缺口不可达（本地管道无重传
    // ——协议错误，任务失败面）。
    FrameSequencer::Config config;
    config.windowCapacity = 4;
    FrameSequencer sequencer(config);
    FrameSequencer::Outcome outcome;
    (void)sequencer.accept(makeFrame(ChannelMsgType::Heartbeat, 1, {}), &outcome);
    // 基线推进到 2 后，seq 7 越前 5 > 窗口 4——缺口不可达（溢出面）。
    (void)sequencer.accept(makeFrame(ChannelMsgType::Heartbeat, 7, {}), &outcome);
    EXPECT_EQ(outcome, FrameSequencer::Outcome::WindowOverflow);
    EXPECT_TRUE(sequencer.failed());
}

TEST(ChannelProtocolSequencerTest, GapTimeoutIsProtocolError_EX_CHN_1)
{
    // EX-CHN-1 断裂面之二：缺口超时（ManualClock 虚拟推进——不 sleep）。
    ManualClock clock;
    FrameSequencer::Config config;
    config.gapTimeout = std::chrono::milliseconds{1000};
    FrameSequencer sequencer(config, [&clock]() { return clock(); });
    FrameSequencer::Outcome outcome;
    (void)sequencer.accept(makeFrame(ChannelMsgType::Heartbeat, 1, {}), &outcome);
    (void)sequencer.accept(makeFrame(ChannelMsgType::Heartbeat, 4, {}), &outcome);  // 缺 2,3
    EXPECT_FALSE(sequencer.gapTimedOut());

    clock.advance(std::chrono::milliseconds{999});
    EXPECT_FALSE(sequencer.gapTimedOut());
    clock.advance(std::chrono::milliseconds{1});
    EXPECT_TRUE(sequencer.gapTimedOut());
    EXPECT_TRUE(sequencer.failed());  // 断裂成立后闩死（通道废弃）
}

TEST(ChannelProtocolSequencerTest, GapClosesOnFillWithoutTimeout)
{
    // 缺口在超时前补齐——正常继续（断裂只在"到点未补齐"成立）。
    ManualClock clock;
    FrameSequencer::Config config;
    config.gapTimeout = std::chrono::milliseconds{500};
    FrameSequencer sequencer(config, [&clock]() { return clock(); });
    FrameSequencer::Outcome outcome;
    (void)sequencer.accept(makeFrame(ChannelMsgType::Heartbeat, 1, {}), &outcome);
    (void)sequencer.accept(makeFrame(ChannelMsgType::Heartbeat, 3, {}), &outcome);
    clock.advance(std::chrono::milliseconds{400});
    (void)sequencer.accept(makeFrame(ChannelMsgType::Heartbeat, 2, {}), &outcome);
    EXPECT_TRUE(sequencer.idle());
    clock.advance(std::chrono::milliseconds{1000});
    EXPECT_FALSE(sequencer.gapTimedOut());  // 缺口已闭合——不判断裂
}

// =====================================================================
// 分帧续传（§6.5"输入传输"——大载荷不整体装载）
// =====================================================================

TEST(ChannelProtocolFragmentTest, FragmentRoundtripLargeMessage)
{
    // 多片往返：写侧分片→读侧重组＝原消息逐字节一致（NFR-PERF-03 机制面）。
    const core::TaskIdentity identity = makeIdentity();
    std::vector<std::uint8_t> message(1000);
    for (std::size_t i = 0; i < message.size(); ++i) {
        message[i] = static_cast<std::uint8_t>(i & 0xFF);
    }
    std::uint64_t seq = 1;
    std::vector<ChannelFrame> frames;
    ASSERT_TRUE(fragmentMessage(ChannelMsgType::DispatchRequest, identity, seq, message,
                                256, frames));
    EXPECT_EQ(frames.size(), 4u);  // 1000/256＝3.9→4 片
    EXPECT_EQ(seq, 5u);            // 序号按帧推进（帧级序控——非消息级）

    MessageReassembler reassembler;
    std::vector<ChannelFrame> complete;
    for (ChannelFrame& frame : frames) {
        bool broken = false;
        std::vector<ChannelFrame> done = reassembler.accept(std::move(frame), &broken);
        ASSERT_FALSE(broken);
        complete.insert(complete.end(), std::make_move_iterator(done.begin()),
                        std::make_move_iterator(done.end()));
    }
    ASSERT_EQ(complete.size(), 1u);
    EXPECT_EQ(complete[0].type, ChannelMsgType::DispatchRequest);
    EXPECT_EQ(complete[0].payload, message);
}

TEST(ChannelProtocolFragmentTest, SingleFragmentAndPassthroughTypes)
{
    // 单片消息（flags 无 More 位）与不可分片类型透传——收侧判定无歧义。
    const core::TaskIdentity identity = makeIdentity();
    std::uint64_t seq = 1;
    std::vector<ChannelFrame> frames;
    ASSERT_TRUE(fragmentMessage(ChannelMsgType::FinalOutput, identity, seq, {0x01, 0x02},
                                4096, frames));
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0].flags & kFlagFragmentMore, 0);  // 末片/单片无 More 位

    MessageReassembler reassembler;
    ChannelFrame heartbeat = makeFrame(ChannelMsgType::Heartbeat, 1, {});
    bool broken = false;
    const std::vector<ChannelFrame> passthrough =
        reassembler.accept(std::move(heartbeat), &broken);
    ASSERT_FALSE(broken);
    ASSERT_EQ(passthrough.size(), 1u);  // 非可分片类型直接透传
    EXPECT_EQ(passthrough[0].type, ChannelMsgType::Heartbeat);
}

TEST(ChannelProtocolFragmentTest, EmptyMessageStillProducesOneFragment)
{
    // 空消息边界：必须有片存在（收侧以"末片无 More 位"为完整判据——
    // 0 片会让收侧永久等待）。
    const core::TaskIdentity identity = makeIdentity();
    std::uint64_t seq = 1;
    std::vector<ChannelFrame> frames;
    ASSERT_TRUE(fragmentMessage(ChannelMsgType::DispatchRequest, identity, seq, {}, 1024,
                                frames));
    ASSERT_EQ(frames.size(), 1u);

    MessageReassembler reassembler;
    bool broken = false;
    const std::vector<ChannelFrame> done =
        reassembler.accept(std::move(frames[0]), &broken);
    ASSERT_FALSE(broken);
    ASSERT_EQ(done.size(), 1u);
    EXPECT_TRUE(done[0].payload.empty());
}

TEST(ChannelProtocolFragmentTest, RejectsNonFragmentableTypeAndOversize)
{
    // 调用方违约面：不可分片类型/0 片尺寸/超上限消息——返回 false 不抛
    // （写侧参数核对面）。
    const core::TaskIdentity identity = makeIdentity();
    std::uint64_t seq = 1;
    std::vector<ChannelFrame> frames;
    EXPECT_FALSE(fragmentMessage(ChannelMsgType::Heartbeat, identity, seq, {1}, 1024,
                                 frames));
    EXPECT_FALSE(fragmentMessage(ChannelMsgType::DispatchRequest, identity, seq, {1}, 0,
                                 frames));
}

// =====================================================================
// 载荷编解码（§6.5 消息集——主/worker 同源的字节级契约）
// =====================================================================

TEST(ChannelProtocolPayloadTest, HelloAndHelloAckRoundtrip)
{
    HelloPayload hello;
    hello.protoVersion = kChannelProtocolVersion;
    hello.manifestDigestHex = manifestDigestHex("stage-a-double|1|7");
    const auto decoded = decodeHello(encodeHello(hello));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->protoVersion, hello.protoVersion);
    EXPECT_EQ(decoded->manifestDigestHex, hello.manifestDigestHex);

    HelloAckPayload ackPayload;
    ackPayload.accepted = true;
    ackPayload.heartbeatIntervalMs = 5000;
    ackPayload.reason = "";
    const auto decodedAck = decodeHelloAck(encodeHelloAck(ackPayload));
    ASSERT_TRUE(decodedAck.has_value());
    EXPECT_TRUE(decodedAck->accepted);
    EXPECT_EQ(decodedAck->heartbeatIntervalMs, 5000u);
}

TEST(ChannelProtocolPayloadTest, DispatchRequestRoundtripAndIdentityCheck)
{
    // 派发物往返＋物化身份核对（§6.5"worker 侧身份核对"的编解码半区）。
    DispatchRequestPayload src;
    src.snapshotBytes = {0x11, 0x22, 0x33};
    src.modelBytes = {0x44, 0x55};
    src.snapshotIdentity = computeContentIdentity(src.snapshotBytes);
    src.modelIdentity = computeContentIdentity(src.modelBytes);
    const auto decoded = decodeDispatchRequest(encodeDispatchRequest(src));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_TRUE(decoded->snapshotIdentity == src.snapshotIdentity);
    EXPECT_TRUE(decoded->modelIdentity == src.modelIdentity);
    EXPECT_EQ(decoded->snapshotBytes, src.snapshotBytes);
    EXPECT_TRUE(contentIdentityMatches(decoded->snapshotBytes, src.snapshotIdentity));
    EXPECT_FALSE(contentIdentityMatches({0x00}, src.snapshotIdentity));  // 失配面
}

TEST(ChannelProtocolPayloadTest, ProgressPayloadRoundtripAndValueGuard)
{
    const auto report = ProgressReport::make(40, "kin.batch", 3, 10);
    const auto decoded = decodeProgress(encodeProgress(report));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_TRUE(*decoded == report);

    // 值域违约＝解码失败（帧级解码校验——percent>100 不可能从合法帧来）。
    std::vector<std::uint8_t> bad = encodeProgress(report);
    bad[0] = 0xFF;  // percent 低字节→255（>100）
    EXPECT_FALSE(decodeProgress(bad).has_value());
}

TEST(ChannelProtocolPayloadTest, BatchAndFinalAndErrorPayloadsRoundtrip)
{
    ResultBatchPayload batch;
    batch.batchIndex = 42;
    batch.bytes = {0xAA, 0xBB};
    const auto decodedBatch = decodeResultBatch(encodeResultBatch(batch));
    ASSERT_TRUE(decodedBatch.has_value());
    EXPECT_EQ(decodedBatch->batchIndex, 42u);
    EXPECT_EQ(decodedBatch->bytes, batch.bytes);

    CheckpointBatchPayload cp;
    cp.sequence = 7;
    cp.bytes = batchBytes(7);
    const auto decodedCp = decodeCheckpointBatch(encodeCheckpointBatch(cp));
    ASSERT_TRUE(decodedCp.has_value());
    EXPECT_EQ(decodedCp->sequence, 7u);

    FinalOutputPayload finalPayload;
    finalPayload.outputCanon = {'F', 'I', 'N'};
    finalPayload.snapshotIdentity = computeContentIdentity(finalPayload.outputCanon);
    finalPayload.modelIdentity = finalPayload.snapshotIdentity;
    const auto decodedFinal = decodeFinalOutput(encodeFinalOutput(finalPayload));
    ASSERT_TRUE(decodedFinal.has_value());
    EXPECT_EQ(decodedFinal->outputCanon, finalPayload.outputCanon);
    EXPECT_TRUE(decodedFinal->snapshotIdentity == finalPayload.snapshotIdentity);

    ErrorReportPayload err;
    err.dev = true;
    err.codeOrChannel = "execution/worker-temp";
    err.message = "cleanup failed";
    const auto decodedErr = decodeErrorReport(encodeErrorReport(err));
    ASSERT_TRUE(decodedErr.has_value());
    EXPECT_TRUE(decodedErr->dev);
    EXPECT_EQ(decodedErr->codeOrChannel, err.codeOrChannel);
    EXPECT_EQ(decodedErr->message, err.message);

    // 截断载荷一律拒绝（解码校验——不猜帧尾）。
    const std::vector<std::uint8_t> full = encodeResultBatch(batch);
    EXPECT_FALSE(decodeResultBatch(std::vector<std::uint8_t>(full.begin(), full.end() - 1))
                     .has_value());
}

// =====================================================================
// 握手摘要与心跳策略（§6.4 握手比对值、D-06 实现参数）
// =====================================================================

TEST(ChannelProtocolHandshakeTest, ManifestDigestIsDeterministicAndHex64)
{
    // 同文本同摘要（NFR-COR-02）；形态＝64 小写 hex（握手比对值契约）。
    // 文本＝worker exe 内置的阶段 A 装配清单（两侧同源——契约测试以同
    // 一文本核对握手一致性）。
    constexpr const char* kManifest = "stage-a-double|1|7";
    const std::string digest1 = manifestDigestHex(kManifest);
    const std::string digest2 = manifestDigestHex(kManifest);
    EXPECT_EQ(digest1, digest2);
    EXPECT_EQ(digest1.size(), 64u);
    EXPECT_THROW(manifestDigestHex(""), ExecutionError);  // 空装配拒绝
}

TEST(ChannelProtocolHandshakeTest, HeartbeatPolicyDefaultsAreD06Parameters)
{
    // D-06 实现参数钉值：默认 5 s×3（acceptance 1 明文"非需求值"——
    // 参数可配，默认值此处冻结为登记口径）。
    const HeartbeatPolicy policy;
    EXPECT_EQ(policy.interval, std::chrono::milliseconds{5000});
    EXPECT_EQ(policy.lossThresholdIntervals, 3u);
    EXPECT_EQ(policy.hungWindow(), std::chrono::milliseconds{15000});
}

// =====================================================================
// 退出码分类（§6.4 退出码约定集表——EX-WKR-2/3 判定本体的纯函数面）
// =====================================================================

TEST(ExitClassificationTest, ConventionSetMapsPerContractTable)
{
    // §6.4 表逐行：0/10/11/12/20/21。
    EXPECT_EQ(classifyWorkerExitCode(0, false), ExitClassification::NormalCompletion);
    EXPECT_EQ(classifyWorkerExitCode(10, false), ExitClassification::LaunchFailed);
    EXPECT_EQ(classifyWorkerExitCode(11, false), ExitClassification::HostInternalError);
    EXPECT_EQ(classifyWorkerExitCode(12, false), ExitClassification::EvaluatorFailed);
    EXPECT_EQ(classifyWorkerExitCode(20, false), ExitClassification::CancelAcknowledged);
    EXPECT_EQ(classifyWorkerExitCode(21, false), ExitClassification::PauseAcknowledged);
}

TEST(ExitClassificationTest, OutsideConventionSetIsCrashedIncludingAccessExceptions)
{
    // 约定集外一律 Crashed——含 0xC0000005 访问违例族（EX-WKR-2 的真实
    // 崩溃码面）与任意其他值。
    EXPECT_EQ(classifyWorkerExitCode(0xC0000005u, false), ExitClassification::Crashed);
    EXPECT_EQ(classifyWorkerExitCode(3, false), ExitClassification::Crashed);
    EXPECT_EQ(classifyWorkerExitCode(0xFFFFFFFFu, false), ExitClassification::Crashed);
}

TEST(ExitClassificationTest, SupervisorForceKillTakesPrecedence)
{
    // 监督方强杀优先于退出码表（TerminateJobObject 的退出码参数不代表
    // worker 自身行为——EX-WKR-3 的 ForceTerminated 分类面）。
    EXPECT_EQ(classifyWorkerExitCode(0, true),
              ExitClassification::ForceTerminatedBySupervisor);
    EXPECT_EQ(classifyWorkerExitCode(0xDEAD10CEu, true),
              ExitClassification::ForceTerminatedBySupervisor);
}

}  // namespace
