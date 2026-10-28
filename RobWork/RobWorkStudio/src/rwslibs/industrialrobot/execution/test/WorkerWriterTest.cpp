/**
 * @file WorkerWriterTest.cpp
 * @brief EXR-04/05：固定分片生成前的并发发送点，直接验证真实 writer 与命令消费。
 * 同单元测试包含 worker TU；只替换分片调用为调度钩子，编码器仍调用真实函数。
 * 管道由用例独占并关闭；不以脚本替身证明业务算法正确性。
 */
#include <sdurws/ird/execution/ChannelProtocol.hpp>
#include <functional>
namespace sdurws::ird::execution {
/** @brief 分片前执行一次测试调度点，其余参数与返回语义沿用 fragmentMessage。 */
bool auditFragmentMessage(ChannelMsgType, const core::TaskIdentity&, std::uint64_t&,
                          const std::vector<std::uint8_t>&, std::size_t,
                          std::vector<ChannelFrame>&);
}
#define fragmentMessage auditFragmentMessage
#define wWinMain ExecutionWriterTestEntry
#include "../worker/main.cpp"
#undef wWinMain
#undef fragmentMessage
#include <gtest/gtest.h>
#include <iostream>
namespace {
// gtest 串行执行；钩子在当前线程嵌入一次完整心跳发送，固定原竞态的交错。
std::function<void()> writerFragmentHook;
}
namespace sdurws::ird::execution {
bool auditFragmentMessage(ChannelMsgType type, const core::TaskIdentity& identity,
                          std::uint64_t& sequence, const std::vector<std::uint8_t>& bytes,
                          std::size_t fragmentSize, std::vector<ChannelFrame>& out)
{
    if (writerFragmentHook) writerFragmentHook();
    return fragmentMessage(type, identity, sequence, bytes, fragmentSize, out);
}
}
namespace {
/** @brief 用例私有同步管道；仅写入两小帧，容量足够，不依赖读线程或 sleep。 */
struct AuditPipe {
    HANDLE read = nullptr;
    HANDLE write = nullptr;
    /** @brief 创建两个自有端点；失败抛异常使测试失败。 */
    AuditPipe()
    {
        if (!::CreatePipe(&read, &write, nullptr, 4096)) throw std::runtime_error("pipe");
    }
    /** @brief 只关闭本用例创建的端点。 */
    ~AuditPipe() { ::CloseHandle(read); ::CloseHandle(write); }
    /** @brief 非阻塞取得已写入字节并解析；系统错误与畸形帧使断言失败。 */
    std::vector<ChannelFrame> frames()
    {
        DWORD size = 0;
        DWORD got = 0;
        EXPECT_TRUE(::PeekNamedPipe(read, nullptr, 0, nullptr, &size, nullptr));
        std::vector<std::uint8_t> bytes(size);
        if (size != 0) EXPECT_TRUE(::ReadFile(read, bytes.data(), size, &got, nullptr));
        EXPECT_EQ(got, size);
        FrameAssembler assembler;
        std::vector<ChannelFrame> result;
        EXPECT_EQ(assembler.feed(bytes.data(), bytes.size(), result), FrameAssembler::Status::Ok);
        return result;
    }
};

/** @brief 创建彼此独立的五元组；attempt 固定为首尝试。 */
core::TaskIdentity writerIdentity()
{
    core::TaskIdentity identity;
    identity.project = core::ProjectId::generate();
    identity.branch = core::BranchId::generate();
    identity.revision = core::RevisionId::generate();
    identity.run = core::RunId::generate();
    identity.attempt = core::AttemptId{1};
    return identity;
}

/** @brief EXR-04：另一发送者先提交心跳，最终帧必须继续其线路序号。 */
TEST(WorkerWriterRegressionTest, HeartbeatCannotReuseFinalFragmentSequence_EXR04)
{
    AuditPipe pipe;
    DataWriter writer;
    writer.handle = pipe.write;
    writer.identity = writerIdentity();
    writerFragmentHook = [&] { EXPECT_TRUE(writer.send(ChannelMsgType::Heartbeat, {})); };
    const bool sent = writer.sendFragmented(ChannelMsgType::FinalOutput, {1, 2});
    writerFragmentHook = {};
    ASSERT_TRUE(sent);
    const auto frames = pipe.frames();
    ASSERT_EQ(frames.size(), 2u);
    std::cout << "heartbeat seq=" << frames[0].seq << " final seq=" << frames[1].seq << '\n';
    EXPECT_EQ(frames[0].seq, 1u);
    EXPECT_EQ(frames[1].seq, 2u); // 独立预期，不只做 encode/decode 往返。
}

/** @brief EXR-05：直接注入异尝试暂停，也不能改变当前尝试终态。 */
TEST(WorkerWriterRegressionTest, RejectPauseCommandFromAnotherAttempt_EXR05)
{
    AuditPipe pipe;
    DataWriter writer;
    writer.handle = pipe.write;
    writer.identity = writerIdentity();
    CommandQueue queue;
    ChannelFrame pause;
    pause.type = ChannelMsgType::PauseRequest;
    pause.seq = 1;
    pause.identity = writerIdentity();
    queue.push(pause);
    std::atomic<bool> heartbeatOff{false};
    const auto directory = fs::temp_directory_path();
    ScriptContext context{writer, queue, heartbeatOff, directory, {}, {}};
    const int code = runScript("stage-a-script:v1\n", context);
    std::cout << "foreign attempt pause exit=" << code << '\n';
    EXPECT_EQ(code, 0);
}
}
